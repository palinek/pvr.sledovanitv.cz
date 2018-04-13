/*
 *      Copyright (c) 2018~now Palo Kisa <palo.kisa@gmail.com>
 *
 *      Copyright (C) 2014 Josef Rokos
 *      http://github.com/PepaRokos/xbmc-pvr-addons/
 *
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this addon; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <chrono>
#include <algorithm>

#include "PVRIptvData.h"
#include "apimanager.h"

using namespace std;
using namespace ADDON;

template <int N> void strAssign(char (&dst)[N], const std::string & src)
{
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0'; // just to be sure
}

PVRIptvData::PVRIptvData(const std::string & userName
    , const std::string & password
    , bool hdEnabled
    , int iEpgMaxDays)
  : m_bKeepAlive{true}
  , m_bLoadRecordings{true}
  , m_bChannelsLoaded{false}
  , m_groups{std::make_shared<group_container_t>()}
  , m_channels{std::make_shared<channel_container_t>()}
  , m_epg{std::make_shared<epg_container_t>()}
  , m_recordings{std::make_shared<recording_container_t>()}
  , m_timers{std::make_shared<timer_container_t>()}
  , m_recordingAvailableDuration{0}
  , m_recordingRecordedDuration{0}
  , m_epgMinTime{time(nullptr) - 86400}
  , m_epgMaxTime{time(nullptr)}
  , m_epgMaxDays{iEpgMaxDays}
  , m_bEGPLoaded{false}
  , m_iLastStart{0}
  , m_iLastEnd{0}
  , m_epgLastFullRefresh{m_epgMaxTime}
  , m_bHdEnabled{hdEnabled}
  , m_manager{userName, password}
{

  SetEPGTimeFrame(m_epgMaxDays);

  CreateThread();
}

void PVRIptvData::LoadRecordingsJob()
{
  if (!KeepAlive())
    return;

  bool load = false;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    if (m_bLoadRecordings)
    {
      load = true;
      m_bLoadRecordings = false;
    }
  }
  if (load)
  {
    LoadRecordings();
  }
}

void PVRIptvData::SetLoadRecordings()
{
  std::lock_guard<std::mutex> critical(m_mutex);
  m_bLoadRecordings = true;
}

bool PVRIptvData::LoadEPGJob()
{
  // trigger full refresh once a time (because for large timespans
  // there doesn't need to be all future dates on the server)
  // TODO: make the full refresh time configurable
  time_t now = time(nullptr);
  if (m_epgLastFullRefresh + 86400/*one day*/ < now)
  {
    XBMC->Log(LOG_INFO, "%s triggering EGP full refresh", __FUNCTION__);
    m_epgLastFullRefresh = now;
    m_iLastEnd = 0;
    m_iLastStart = 0;

    int epg_max_days = 0;
    {
      std::lock_guard<std::mutex> critical(m_mutex);
      epg_max_days = m_epgMaxDays;
    }
    SetEPGTimeFrame(epg_max_days);
    LoadPlayList();
  }

  time_t min_epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    min_epg = m_epgMinTime;
  }
  bool updated = false;
  if (KeepAlive() && 0 == m_iLastEnd)
  {
    // the first run...load just needed data as soon as posible
    LoadEPG(now, true);
    updated = true;
  } else
  {
    if (KeepAlive() && m_epgMaxTime > m_iLastEnd)
    {
      LoadEPG(m_iLastEnd, false);
      updated = true;
    }
    if (KeepAlive() && min_epg < m_iLastStart)
    {
      LoadEPG(m_iLastStart - 86400, false);
      updated = true;
    }
  }
  if (KeepAlive())
    ReleaseUnneededEPG();
  return updated;
}

void PVRIptvData::ReleaseUnneededEPG()
{
  decltype (m_epg) epg;
  time_t min_epg, max_epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    min_epg = m_epgMinTime;
    max_epg = m_epgMaxTime;
    epg = m_epg;
  }
  auto epg_copy = std::make_shared<epg_container_t>();
  XBMC->Log(LOG_DEBUG, "%s min_epg=%s max_epg=%s", __FUNCTION__, ApiManager::formatTime(min_epg).c_str(), ApiManager::formatTime(max_epg).c_str());

  for (const auto & epg_channel : *epg)
  {
    auto & epg_data = epg_channel.second.epg;
    std::vector<time_t> to_delete;
    for (auto entry_i = epg_data.cbegin(); entry_i != epg_data.cend(); ++entry_i)
    {
      const PVRIptvEpgEntry & entry = entry_i->second;
      if (entry_i->second.startTime > max_epg || entry_i->second.endTime < min_epg)
      {
        XBMC->Log(LOG_DEBUG, "Removing TV show: %s - %s, start=%s end=%s", epg_channel.second.strName.c_str(), entry.strTitle.c_str()
            , ApiManager::formatTime(entry.startTime).c_str(), ApiManager::formatTime(entry.endTime).c_str());
        // notify about the epg change...and delete it
        EPG_TAG tag;
        memset(&tag, 0, sizeof(EPG_TAG));
        tag.iUniqueBroadcastId = entry.iBroadcastId;
        tag.iUniqueChannelId = entry.iChannelId;
        PVR->EpgEventStateChange(&tag, EPG_EVENT_DELETED);

        to_delete.push_back(entry_i->first);
      }
    }
    if (!to_delete.empty())
    {
      auto & epg_copy_channel = (*epg_copy)[epg_channel.first];
      epg_copy_channel = epg_channel.second;
      for (const auto key_delete : to_delete)
      {
        epg_copy_channel.epg.erase(key_delete);
      }
    }
  }

  // check if something deleted, if so make copy and atomically reassign
  if (!epg_copy->empty())
  {
    for (const auto & epg_channel : *epg)
    {
      if (epg_copy->count(epg_channel.first) <= 0)
        (*epg_copy)[epg_channel.first] = epg_channel.second;
    }

    m_epg = std::move(epg_copy);
  }

  // narrow the loaded time info (if needed)
  m_iLastStart = std::max(m_iLastStart, min_epg);
  m_iLastEnd = std::min(m_iLastEnd, max_epg);
}

void PVRIptvData::KeepAliveJob()
{
  if (!KeepAlive())
    return;

  XBMC->Log(LOG_DEBUG, "keepAlive:: trigger");
  if (!m_manager.keepAlive())
  {
    LoginLoop();
  }
  SetLoadRecordings();
}

void PVRIptvData::LoginLoop()
{
  unsigned login_delay = 0;
  for (bool should_try = true; KeepAlive() && should_try; --login_delay)
  {
    if (0 >= login_delay)
    {
      if (m_manager.login())
        should_try = false;
      else
        login_delay = 120; // try in two minutes
    }
    Sleep(1000);
  }
}

bool PVRIptvData::WaitForChannels() const
{
  std::unique_lock<std::mutex> critical(m_mutex);
  return m_waitCond.wait_for(critical, std::chrono::seconds{5}, [this] { return m_bChannelsLoaded; });
}

void *PVRIptvData::Process(void)
{
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread started");

  LoginLoop();

  LoadPlayList();

  unsigned epg_delay = 0;

  unsigned int counter = 0;
  while (KeepAlive())
  {
    if (0 < epg_delay)
      Sleep(1000);
    LoadRecordingsJob();

    if (0 == epg_delay)
    {
      // perform epg loading in next cycle if something updated in this one
      if (!LoadEPGJob())
        epg_delay = 60;
    } else
    {
      --epg_delay;
    }

    if (counter >= 20)
    {
      KeepAliveJob();
      counter = 0;
    }

    ++counter;
  }
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread stopped");
  return NULL;
}

PVRIptvData::~PVRIptvData(void)
{
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_bKeepAlive = false;
  }
  if (IsRunning())
  {
    StopThread(0);
  }
  XBMC->Log(LOG_DEBUG, "%s destructed", __FUNCTION__);
}

bool PVRIptvData::KeepAlive()
{
  std::lock_guard<std::mutex> critical(m_mutex);
  return m_bKeepAlive;
}

bool PVRIptvData::LoadEPG(time_t iStart, bool bSmallStep)
{
  const int step = bSmallStep ? 3600 : 86400;
  XBMC->Log(LOG_DEBUG, "%s last start %s, start %s, last end %s, end %s", __FUNCTION__, ApiManager::formatTime(m_iLastStart).c_str()
      , ApiManager::formatTime(iStart).c_str(), ApiManager::formatTime(m_iLastEnd).c_str(), ApiManager::formatTime(iStart + step).c_str());
  if (m_bEGPLoaded && m_iLastStart != 0 && iStart >= m_iLastStart && iStart + step <= m_iLastEnd)
    return false;

  Json::Value root;

  if (!m_manager.getEpg(iStart, bSmallStep, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse EPG data. EPG not loaded.");
    m_bEGPLoaded = true;
    return false;
  }

  if (m_iLastStart == 0 || m_iLastStart > iStart)
    m_iLastStart = iStart;
  if (iStart + step > m_iLastEnd)
    m_iLastEnd = iStart + step;

  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }
  auto epg_copy = std::make_shared<epg_container_t>(*epg);

  Json::Value json_channels = root["channels"];
  Json::Value::Members chIds = json_channels.getMemberNames();
  for (Json::Value::Members::iterator i = chIds.begin(); i != chIds.end(); i++)
  {
    std::string strChId = *i;

    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&strChId] (const PVRIptvChannel & ch) { return ch.strId == strChId; });
    if (channel_i != channels->cend())
    {
      PVRIptvEpgChannel & epgChannel = (*epg_copy)[strChId];
      epgChannel.strId = strChId;

      Json::Value epgData = json_channels[strChId];
      for (unsigned int j = 0; j < epgData.size(); j++)
      {
        Json::Value epgEntry = epgData[j];

        const time_t start_time = ParseDateTime(epgEntry.get("startTime", "").asString());
        PVRIptvEpgEntry iptventry;
        iptventry.iBroadcastId = start_time; // unique id for channel (even if time_t is wider, int should be enough for short period of time)
        iptventry.iGenreType = 0;
        iptventry.iGenreSubType = 0;
        iptventry.iChannelId = channel_i->iUniqueId;
        iptventry.strTitle = epgEntry.get("title", "").asString();
        iptventry.strPlot = epgEntry.get("description", "").asString();
        iptventry.startTime = start_time;
        iptventry.endTime = ParseDateTime(epgEntry.get("endTime", "").asString());
        iptventry.strEventId = epgEntry.get("eventId", "").asString();
        iptventry.availableTimeshift = epgEntry.get("availability", "none").asString() == "timeshift";

        XBMC->Log(LOG_DEBUG, "Loading TV show: %s - %s, start=%s", strChId.c_str(), iptventry.strTitle.c_str(), epgEntry.get("startTime", "").asString().c_str());

        // notify about the epg change...and store it
        EPG_TAG tag;
        memset(&tag, 0, sizeof(EPG_TAG));

        tag.iUniqueBroadcastId  = iptventry.iBroadcastId;
        tag.iUniqueChannelId    = iptventry.iChannelId;
        tag.strTitle            = strdup(iptventry.strTitle.c_str());
        tag.startTime           = iptventry.startTime;
        tag.endTime             = iptventry.endTime;
        tag.strPlotOutline      = strdup(iptventry.strPlotOutline.c_str());
        tag.strPlot             = strdup(iptventry.strPlot.c_str());
        tag.strIconPath         = strdup(iptventry.strIconPath.c_str());
        tag.iGenreType          = EPG_GENRE_USE_STRING;        //iptventry.iGenreType;
        tag.iGenreSubType       = 0;                           //iptventry.iGenreSubType;
        tag.strGenreDescription = strdup(iptventry.strGenreString.c_str());

        auto result = epgChannel.epg.emplace(iptventry.startTime, iptventry);
        bool value_changed = !result.second;
        if (value_changed)
        {
          epgChannel.epg[iptventry.startTime] = std::move(iptventry);
        }

        PVR->EpgEventStateChange(&tag, value_changed ? EPG_EVENT_UPDATED : EPG_EVENT_CREATED);

        free(const_cast<char *>(tag.strTitle));
        free(const_cast<char *>(tag.strPlotOutline));
        free(const_cast<char *>(tag.strPlot));
        free(const_cast<char *>(tag.strIconPath));
        free(const_cast<char *>(tag.strGenreDescription));

      }
    }
  }

  // atomic assign new version of the epg all epgs
  m_epg = epg_copy;

  m_bEGPLoaded = true;
  XBMC->Log(LOG_NOTICE, "EPG Loaded.");

  return true;
}

bool PVRIptvData::LoadRecordings()
{
  decltype (m_channels) channels;
  decltype (m_recordings) recordings;
  decltype (m_timers) timers;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    recordings = m_recordings;
    timers = m_timers;
  }

  auto new_recordings = std::make_shared<recording_container_t>();
  auto new_timers = std::make_shared<timer_container_t>();
  long long available_duration = 0;
  long long recorded_duration = 0;

  Json::Value root;

  if (!m_manager.getPvr(root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse recordings.");
    return false;
  }

  available_duration = root["summary"].get("availableDuration", 0).asInt() / 60 * 1024; //report minutes as MB
  recorded_duration = root["summary"].get("recordedDuration", 0).asInt() / 60 * 1024;

  Json::Value records = root["records"];
  for (unsigned int i = 0; i < records.size(); i++)
  {
    Json::Value record = records[i];
    std::string str_ch_id = record.get("channel", "").asString();
    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&str_ch_id] (const PVRIptvChannel & ch) { return ch.strId == str_ch_id; });
    PVRIptvRecording iptvrecording;
    PVRIptvTimer iptvtimer;
    time_t startTime = ParseDateTime(record.get("startTime", "").asString());
    int duration = record.get("duration", 0).asInt();
    time_t now;
    time(&now);

    if ((startTime + duration) < now)
    {
      char buf[256];
      sprintf(buf, "%d", record.get("id", 0).asInt());
      iptvrecording.strRecordId = buf;
      iptvrecording.strTitle = record.get("title", "").asString();

      XBMC->Log(LOG_DEBUG, "Loading recording '%s'", iptvrecording.strTitle.c_str());

      if (channel_i != channels->cend())
      {
        iptvrecording.strChannelName = channel_i->strChannelName;
      }
      iptvrecording.startTime = startTime;
      iptvrecording.strPlotOutline = record.get("event", "").get("description", "").asString();
      iptvrecording.duration = duration;
      iptvrecording.bRadio = channel_i->bIsRadio;

      new_recordings->push_back(iptvrecording);
    }
    else
    {
      iptvtimer.iClientIndex = record.get("id", 0).asInt();
      if (channel_i != channels->cend())
      {
        iptvtimer.iClientChannelUid = channel_i->iUniqueId;
      }
      iptvtimer.startTime = ParseDateTime(record.get("startTime", "").asString());
      iptvtimer.endTime = iptvtimer.startTime + record.get("duration", 0).asInt();

      if (startTime < now && (startTime + duration) >= now)
      {
        iptvtimer.state = PVR_TIMER_STATE_RECORDING;
      }
      else
      {
        iptvtimer.state = PVR_TIMER_STATE_SCHEDULED;
      }
      iptvtimer.strTitle = record.get("title", "").asString();

      XBMC->Log(LOG_DEBUG, "Loading timer '%s'", iptvtimer.strTitle.c_str());

      new_timers->push_back(iptvtimer);
    }

  }

  bool changed_r = new_recordings->size() != recordings->size();
  for (size_t i = 0; !changed_r && i < new_recordings->size(); ++i)
  {
    const auto & old_rec = (*recordings)[i];
    const auto & new_rec = (*new_recordings)[i];
    if (new_rec.strRecordId != old_rec.strRecordId)
    {
      changed_r = true;
      break;
    }
  }
  if (changed_r)
  {
    for (auto & recording : *new_recordings)
    {
      recording.strStreamUrl = m_manager.getRecordingUrl(recording.strRecordId);
    }
  }
  bool changed_t = new_timers->size() != timers->size();
  for (size_t i = 0; !changed_t && i < new_timers->size(); ++i)
  {
    const auto & old_timer = (*timers)[i];
    const auto & new_timer = (*new_timers)[i];
    if (new_timer.iClientIndex != old_timer.iClientIndex)
    {
      changed_t = true;
      break;
    }
  }
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    if (changed_r)
    {
      m_recordings = std::move(new_recordings);
      PVR->TriggerRecordingUpdate();
    }

    if (changed_t)
    {
      m_timers = std::move(new_timers);
      PVR->TriggerTimerUpdate();
    }
    m_recordingAvailableDuration = available_duration;
    m_recordingRecordedDuration = recorded_duration;
  }

  return true;
}

bool PVRIptvData::LoadPlayList(void)
{
  if (!KeepAlive())
    return false;

  Json::Value root;

  if (!m_manager.getPlaylist(root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot get/parse playlist.");
    return false;
  }

  /*
  std::string qualities = m_manager.getStreamQualities();
  XBMC->Log(LOG_DEBUG, "Stream qualities: %s", qualities.c_str());
  */

  //channels
  auto new_channels = std::make_shared<channel_container_t>();
  Json::Value channels = root["channels"];
  for (unsigned int i = 0; i < channels.size(); i++)
  {
    Json::Value channel = channels[i];
    PVRIptvChannel iptvchan;

    iptvchan.strId = channel.get("id", "").asString();
    iptvchan.strChannelName = channel.get("name", "").asString();
    iptvchan.strGroupId = channel.get("group", "").asString();
    iptvchan.strStreamURL = channel.get("url", "").asString();

    XBMC->Log(LOG_DEBUG, "Channel %s, URL: %s", iptvchan.strChannelName.c_str(), iptvchan.strStreamURL.c_str());

    std::string strUrl = iptvchan.strStreamURL;

    if (m_bHdEnabled)
    {
      size_t qIndex = strUrl.find("quality");
      strUrl.replace(strUrl.begin() + qIndex, strUrl.end(), "quality=40");
    }

    iptvchan.strStreamURL = strUrl;
    iptvchan.iUniqueId = i + 1;
    iptvchan.iChannelNumber = i + 1;
    iptvchan.strIconPath = channel.get("logoUrl", "").asString();
    iptvchan.bIsRadio = channel.get("type", "").asString() != "tv";

    new_channels->push_back(iptvchan);
  }

  auto new_groups = std::make_shared<group_container_t>();
  Json::Value groups = root["groups"];
  for (const auto & group_id : groups.getMemberNames())
  {
    PVRIptvChannelGroup group;
    group.bRadio = false; // currently there is no way to distinguish group types in the returned json
    group.strGroupId = group_id;
    group.strGroupName = groups[group_id].asString();
    for (const auto & channel : *new_channels)
    {
      if (channel.strGroupId == group_id)
        group.members.push_back(channel.iUniqueId);
    }
    new_groups->push_back(std::move(group));
  }

  XBMC->Log(LOG_NOTICE, "Loaded %d channels.", new_channels->size());
  XBMC->QueueNotification(QUEUE_INFO, "%d channels loaded.", new_channels->size());


  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_channels = std::move(new_channels);
    m_groups = std::move(new_groups);
    m_bChannelsLoaded = true;
  }
  m_waitCond.notify_all();
  PVR->TriggerChannelUpdate();
  PVR->TriggerChannelGroupsUpdate();

  return true;
}

int PVRIptvData::GetChannelsAmount(void)
{
  auto channels = m_channels;
  return channels->size();
}

PVR_ERROR PVRIptvData::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  XBMC->Log(LOG_DEBUG, "%s %s", __FUNCTION__, bRadio ? "radio" : "tv");
  WaitForChannels();

  auto channels = m_channels;

  std::vector<PVR_CHANNEL> xbmc_channels;
  for (const auto & channel : *channels)
  {
    if (channel.bIsRadio == bRadio)
    {
      PVR_CHANNEL xbmcChannel;
      memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));

      xbmcChannel.iUniqueId         = channel.iUniqueId;
      xbmcChannel.bIsRadio          = channel.bIsRadio;
      xbmcChannel.iChannelNumber    = channel.iChannelNumber;
      strAssign(xbmcChannel.strChannelName, channel.strChannelName);
      xbmcChannel.iEncryptionSystem = channel.iEncryptionSystem;
      strAssign(xbmcChannel.strIconPath, channel.strIconPath);
      xbmcChannel.bIsHidden         = false;

      xbmc_channels.push_back(std::move(xbmcChannel));
    }
  }

  for (const auto & xbmcChannel : xbmc_channels)
  {
    PVR->TransferChannelEntry(handle, &xbmcChannel);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetChannelStreamUrl(const PVR_CHANNEL* channel, std::string & streamUrl) const
{
  auto channels = m_channels;

  auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [channel] (const PVRIptvChannel & c) { return c.iChannelNumber == channel->iUniqueId; });
  if (channels->cend() == channel_i)
  {
    XBMC->Log(LOG_NOTICE, "%s can't find channel %d", __FUNCTION__, channel->iUniqueId);
    return PVR_ERROR_INVALID_PARAMETERS;
  }

  streamUrl = channel_i->strStreamURL;
  return PVR_ERROR_NO_ERROR;

}

int PVRIptvData::GetChannelGroupsAmount(void)
{
  auto groups = m_groups;
  return groups->size();
}

PVR_ERROR PVRIptvData::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  WaitForChannels();

  auto groups = m_groups;

  std::vector<PVR_CHANNEL_GROUP> xbmc_groups;
  for (const auto & group : *groups)
  {
    if (group.bRadio == bRadio)
    {
      PVR_CHANNEL_GROUP xbmcGroup;
      memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));

      xbmcGroup.bIsRadio = bRadio;
      strAssign(xbmcGroup.strGroupName, group.strGroupName);

      xbmc_groups.push_back(std::move(xbmcGroup));
    }
  }

  for (const auto & xbmcGroup : xbmc_groups)
  {
    PVR->TransferChannelGroup(handle, &xbmcGroup);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  WaitForChannels();

  decltype (m_groups) groups;
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    groups = m_groups;
    channels = m_channels;
  }

  std::vector<PVR_CHANNEL_GROUP_MEMBER> xbmc_group_members;
  auto group_i = std::find_if(groups->cbegin(), groups->cend(), [&group] (PVRIptvChannelGroup const & g) { return g.strGroupName == group.strGroupName; });
  if (group_i != groups->cend())
  {
    for (const auto & member : group_i->members)
    {
      if (member < 0 || member >= channels->size())
        continue;

      const PVRIptvChannel &channel = (*channels)[member];
      PVR_CHANNEL_GROUP_MEMBER xbmcGroupMember;
      memset(&xbmcGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));

      strncpy(xbmcGroupMember.strGroupName, group.strGroupName, sizeof(xbmcGroupMember.strGroupName) - 1);
      xbmcGroupMember.iChannelUniqueId = channel.iUniqueId;
      xbmcGroupMember.iChannelNumber   = channel.iChannelNumber;

      xbmc_group_members.push_back(std::move(xbmcGroupMember));
    }
  }
  for (const auto & xbmcGroupMember : xbmc_group_members)
  {
    PVR->TransferChannelGroupMember(handle, &xbmcGroupMember);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  XBMC->Log(LOG_DEBUG, "%s %s, from=%s to=%s", __FUNCTION__, channel.strChannelName, ApiManager::formatTime(iStart).c_str(), ApiManager::formatTime(iEnd).c_str());
  std::lock_guard<std::mutex> critical(m_mutex);
  m_epgMinTime = iStart;
  m_epgMaxTime = iEnd;
  return PVR_ERROR_NO_ERROR;
}

static PVR_ERROR GetEPGData(const EPG_TAG* tag
    , const channel_container_t * channels
    , const epg_container_t * epg
    , epg_entry_container_t::const_iterator & epg_i
    )
{
  auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [tag] (const PVRIptvChannel & c) { return c.iChannelNumber == tag->iUniqueChannelId; });
  if (channels->cend() == channel_i)
  {
    XBMC->Log(LOG_NOTICE, "%s can't find channel %d", __FUNCTION__, tag->iUniqueChannelId);
    return PVR_ERROR_INVALID_PARAMETERS;
  }

  auto ch_epg_i = epg->find(channel_i->strId);

  if (epg->cend() == ch_epg_i || (epg_i = ch_epg_i->second.epg.find(tag->iUniqueBroadcastId)) == ch_epg_i->second.epg.cend())
  {
    XBMC->Log(LOG_NOTICE, "%s can't EPG data for find channel %s, time %d", __FUNCTION__, channel_i->strId.c_str(), tag->iUniqueBroadcastId);
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::IsEPGTagPlayable(const EPG_TAG* tag, bool* bIsPlayable) const
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }

  epg_entry_container_t::const_iterator epg_i;
  PVR_ERROR ret = GetEPGData(tag, channels.get(), epg.get(), epg_i);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  *bIsPlayable = epg_i->second.availableTimeshift && tag->startTime < time(nullptr);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetEPGStreamUrl(const EPG_TAG* tag, std::string & streamUrl) const
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }

  epg_entry_container_t::const_iterator epg_i;
  PVR_ERROR ret = GetEPGData(tag, channels.get(), epg.get(), epg_i);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  int duration;
  if (!m_manager.getTimeShiftInfo(epg_i->second.strEventId, streamUrl, duration))
    return PVR_ERROR_INVALID_PARAMETERS;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::SetEPGTimeFrame(int iDays)
{
  XBMC->Log(LOG_DEBUG, "%s iDays=%d", __FUNCTION__, iDays);
  time_t now = time(nullptr);
  std::lock_guard<std::mutex> critical(m_mutex);
  m_epgMaxTime = now + iDays * 86400;
  m_epgMaxDays = iDays;

  return PVR_ERROR_NO_ERROR;
}

int PVRIptvData::ParseDateTime(std::string strDate)
{
  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(tm));

  sscanf(strDate.c_str(), "%04d-%02d-%02d %02d:%02d", &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour, &timeinfo.tm_min);
  timeinfo.tm_sec = 0;

  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  return mktime(&timeinfo);
}

int PVRIptvData::GetRecordingsAmount()
{
  auto recordings = m_recordings;
  return recordings->size();
}

PVR_ERROR PVRIptvData::GetRecordings(ADDON_HANDLE handle)
{
  auto recordings = m_recordings;
  std::vector<PVR_RECORDING> xbmc_records;
  auto insert_lambda = [&xbmc_records] (const PVRIptvRecording & rec)
  {
    PVR_RECORDING xbmcRecord;
    memset(&xbmcRecord, 0, sizeof(PVR_RECORDING));

    strAssign(xbmcRecord.strRecordingId, rec.strRecordId);
    strAssign(xbmcRecord.strTitle, rec.strTitle);
    strAssign(xbmcRecord.strDirectory, rec.strDirectory);
    strAssign(xbmcRecord.strChannelName, rec.strChannelName);
    xbmcRecord.recordingTime = rec.startTime;
    strAssign(xbmcRecord.strPlotOutline, rec.strPlotOutline);
    strAssign(xbmcRecord.strPlot, rec.strPlotOutline);
    xbmcRecord.iDuration = rec.duration;
    xbmcRecord.channelType = rec.bRadio ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV;

    xbmc_records.push_back(std::move(xbmcRecord));
  };

  std::for_each(recordings->cbegin(), recordings->cend(), insert_lambda);

  for (const auto & xbmcRecord : xbmc_records)
  {
    PVR->TransferRecordingEntry(handle, &xbmcRecord);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetRecordingStreamUrl(const PVR_RECORDING* recording, std::string & streamUrl) const
{
  auto recordings = m_recordings;
  auto rec_i = std::find_if(recordings->cbegin(), recordings->cend(), [recording] (const PVRIptvRecording & r) { return recording->strRecordingId == r.strRecordId; });
  if (recordings->cend() == rec_i)
    return PVR_ERROR_INVALID_PARAMETERS;

  streamUrl = rec_i->strStreamUrl;
  return PVR_ERROR_NO_ERROR;
}

int PVRIptvData::GetTimersAmount()
{
  const auto timers = m_timers;
  return timers->size();
}


PVR_ERROR PVRIptvData::GetTimers(ADDON_HANDLE handle)
{
  const auto timers = m_timers;

  std::vector<PVR_TIMER> xbmc_timers;
  for (const auto & timer : *timers)
  {
    PVR_TIMER xbmcTimer;
    memset(&xbmcTimer, 0, sizeof(PVR_TIMER));

    xbmcTimer.iClientIndex = timer.iClientIndex;
    xbmcTimer.iClientChannelUid = timer.iClientChannelUid;
    xbmcTimer.startTime = timer.startTime;
    xbmcTimer.endTime = timer.endTime;
    xbmcTimer.state = timer.state;
    strAssign(xbmcTimer.strTitle, timer.strTitle);
    strAssign(xbmcTimer.strSummary, timer.strSummary);

    xbmc_timers.push_back(std::move(xbmcTimer));
  }
  for (const auto & xbmcTimer : xbmc_timers)
  {
    PVR->TransferTimerEntry(handle, &xbmcTimer);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::AddTimer(const PVR_TIMER &timer)
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }

  const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&timer] (const PVRIptvChannel & ch) { return ch.iUniqueId == timer.iClientChannelUid; });
  if (channel_i == channels->cend())
  {
    XBMC->Log(LOG_ERROR, "%s - channel not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }
  const auto epg_channel_i = epg->find(channel_i->strId);
  if (epg_channel_i == epg->cend())
  {
    XBMC->Log(LOG_ERROR, "%s - epg channel not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }

  const auto epg_i = epg_channel_i->second.epg.find(timer.startTime);
  if (epg_i == epg_channel_i->second.epg.cend())
  {
    XBMC->Log(LOG_ERROR, "%s - event not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }

  const PVRIptvEpgEntry & epg_entry = epg_i->second;
  if (m_manager.addTimer(epg_entry.strEventId))
  {
    SetLoadRecordings();
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR PVRIptvData::DeleteRecord(const string &strRecordId)
{
  if (m_manager.deleteRecord(strRecordId))
  {
    SetLoadRecordings();
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR PVRIptvData::DeleteRecord(int iRecordId)
{
  std::ostringstream os;
  os << iRecordId;

  return DeleteRecord(os.str());
}

PVR_ERROR PVRIptvData::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    *iTotal = m_recordingAvailableDuration;
    *iUsed = m_recordingRecordedDuration;
  }
  return PVR_ERROR_NO_ERROR;
}

bool PVRIptvData::LoggedIn() const
{
  return m_manager.loggedIn();
}
