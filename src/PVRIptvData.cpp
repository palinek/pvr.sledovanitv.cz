/*
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <json/json.h>

#include "PVRIptvData.h"
#include "apimanager.h"

using namespace std;
using namespace ADDON;

extern bool g_bHdEnabled;

const std::string PVRIptvData::VIRTUAL_TIMESHIFT_ID = "VIRTUAL_TIMESHIFT_ID";

PVRIptvData::PVRIptvData(void)
{
  m_iLastStart    = 0;
  m_iLastEnd      = 0;

  m_bEGPLoaded = false;

  m_bKeepAlive = true;
  m_bLoadRecordings = true;

  m_groups = std::make_shared<group_container_t>();
  m_channels = std::make_shared<channel_container_t>();
  m_epg = std::make_shared<epg_container_t>();
  m_recordings = std::make_shared<recording_container_t>();
  m_timers = std::make_shared<timer_container_t>();

  CreateThread();
}

void PVRIptvData::LoadRecordingsJob()
{
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

void *PVRIptvData::Process(void)
{
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread started");

  m_manager.login();

  LoadPlayList();

  // load epg - 4 days .. for timeshift
  time_t min_epg = time(0) - 4 * 86400;
  unsigned epg_delay = 0;

  unsigned int counter = 0;
  while (m_bKeepAlive)
  {
    LoadRecordingsJob();

    if (0 == epg_delay)
    {
      epg_delay = 2;
      bool update = false;
      time_t now = time(nullptr);
      if (0 == m_iLastEnd)
      {
        // the first run...load just needed data as soon as posible
        LoadEPG(now, true);
        update = true;
      } else
      {
        if (now + 86400 > m_iLastEnd)
        {
          LoadEPG(m_iLastEnd, false);
          update = true;
        }
        if (min_epg < m_iLastStart)
        {
          LoadEPG(m_iLastStart - 86400, false);
          update = true;
        }
      }
      if (update)
      {
        const auto channels = m_channels;
        for (const auto & channel : *channels)
        {
          PVR->TriggerEpgUpdate(channel.iUniqueId);
        }
      }
    } else
    {
      --epg_delay;
    }

    if (counter >= 20)
    {
      XBMC->Log(LOG_DEBUG, "keepAlive:: trigger");
      counter = 0;
      if (!m_manager.keepAlive())
      {
        m_manager.login();
      }
      SetLoadRecordings();
    }

    ++counter;
    Sleep(1000);
  }
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread stopped");
  return NULL;
}

PVRIptvData::~PVRIptvData(void)
{
  m_bKeepAlive = false;
  if (IsRunning())
  {
    StopThread();
  }
}

bool PVRIptvData::LoadEPG(time_t iStart, bool bSmallStep)
{
  const int step = bSmallStep ? 3600 : 86400;
  XBMC->Log(LOG_DEBUG, "Read EPG last start %d, start %d, last end %d, end %d", m_iLastStart, iStart, m_iLastEnd, iStart + step);
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
  auto epg_copy = std::make_shared<epg_container_t>(const_cast<epg_container_t &>(*epg));

  Json::Value json_channels = root["channels"];
  Json::Value::Members chIds = json_channels.getMemberNames();
  for (Json::Value::Members::iterator i = chIds.begin(); i != chIds.end(); i++)
  {
    std::string strChId = *i;

    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&strChId] (const PVRIptvChannel & ch) { return ch.strTvgId == strChId; });
    if (channel_i != channels->cend())
    {
      PVRIptvEpgChannel & epgChannel = (*epg_copy)[strChId];
      epgChannel.strId = strChId;
      epgChannel.strName = channel_i->strChannelName;

      Json::Value epgData = json_channels[strChId];
      for (unsigned int j = 0; j < epgData.size(); j++)
      {
        Json::Value epgEntry = epgData[j];

        const time_t start_time = ParseDateTime(epgEntry.get("startTime", "").asString());
        PVRIptvEpgEntry iptventry;
        ventry.iBroadcastId = start_time; // unique id for channel (even if time_t is wider, int should be enough for short period of time)
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

        epgChannel.epg[iptventry.startTime] = std::move(iptventry);
      }
    }

  }

  // atomic assign new version of the epg
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

  Json::Value root;

  if (!m_manager.getPvr(root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse recordings.");
    return false;
  }

  Json::Value records = root["records"];
  for (unsigned int i = 0; i < records.size(); i++)
  {
    Json::Value record = records[i];
    std::string str_ch_id = record.get("channel", "").asString();
    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&str_ch_id] (const PVRIptvChannel & ch) { return ch.strTvgId == str_ch_id; });
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
  if (changed_r || changed_t)
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
  }

  return true;
}

bool PVRIptvData::LoadPlayList(void)
{
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

  auto new_channels = std::make_shared<channel_container_t>();
  Json::Value channels = root["channels"];
  for (unsigned int i = 0; i < channels.size(); i++)
  {
    Json::Value channel = channels[i];
    PVRIptvChannel iptvchan;

    iptvchan.strTvgId = channel.get("id", "").asString();
    iptvchan.strChannelName = channel.get("name", "").asString();
    iptvchan.strStreamURL = channel.get("url", "").asString();

    XBMC->Log(LOG_DEBUG, "Channel %s, URL: %s", iptvchan.strChannelName.c_str(), iptvchan.strStreamURL.c_str());

    std::string strUrl = iptvchan.strStreamURL;

    if (g_bHdEnabled)
    {
      size_t qIndex = strUrl.find("quality");
      strUrl.replace(strUrl.begin() + qIndex, strUrl.end(), "quality=40");
    }

    iptvchan.strStreamURL = strUrl;
    iptvchan.iUniqueId = i + 1;
    iptvchan.iChannelNumber = i + 1;
    iptvchan.strLogoPath = channel.get("logoUrl", "").asString();
    iptvchan.bRadio = channel.get("type", "").asString() != "tv";

    new_channels->push_back(iptvchan);
  }

  XBMC->Log(LOG_NOTICE, "Loaded %d channels.", new_channels->size());
  XBMC->QueueNotification(QUEUE_INFO, "%d channels loaded.", new_channels->size());

  m_channels = std::move(new_channels);
  PVR->TriggerChannelUpdate();

  return true;
}

int PVRIptvData::GetChannelsAmount(void)
{
  auto channels = m_channels;
  return channels->size();
}

PVR_ERROR PVRIptvData::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  auto channels = m_channels;

  std::vector<PVR_CHANNEL> xbmc_channels;
  for (const auto & channel : *channels)
  {
    if (channel.bRadio == bRadio)
    {
      PVR_CHANNEL xbmcChannel;
      memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));

      xbmcChannel.iUniqueId         = channel.iUniqueId;
      xbmcChannel.bIsRadio          = channel.bRadio;
      xbmcChannel.iChannelNumber    = channel.iChannelNumber;
      strncpy(xbmcChannel.strChannelName, channel.strChannelName.c_str(), sizeof(xbmcChannel.strChannelName) - 1);
      strncpy(xbmcChannel.strStreamURL, channel.strStreamURL.c_str(), sizeof(xbmcChannel.strStreamURL) - 1);
      xbmcChannel.iEncryptionSystem = channel.iEncryptionSystem;
      strncpy(xbmcChannel.strIconPath, channel.strLogoPath.c_str(), sizeof(xbmcChannel.strIconPath) - 1);
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

int PVRIptvData::GetChannelGroupsAmount(void)
{
  auto groups = m_groups;
  return groups->size();
}

PVR_ERROR PVRIptvData::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  auto groups = m_groups;

  std::vector<PVR_CHANNEL_GROUP> xbmc_groups;
  for (const auto & group : *groups)
  {
    if (group.bRadio == bRadio)
    {
      PVR_CHANNEL_GROUP xbmcGroup;
      memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));

      xbmcGroup.bIsRadio = bRadio;
      strncpy(xbmcGroup.strGroupName, group.strGroupName.c_str(), sizeof(xbmcGroup.strGroupName) - 1);

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
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    epg = m_epg;
    channels = m_channels;
  }
  std::vector<EPG_TAG> xbmc_tags;
  XBMC->Log(LOG_DEBUG, "Read EPG for channel %s, from=%s to=%s", channel.strChannelName, ApiManager::formatTime(iStart).c_str(), ApiManager::formatTime(iEnd).c_str());
  const auto myChannel = std::find_if(channels->cbegin(), channels->cend(), [&channel] (const PVRIptvChannel & c) { return c.iUniqueId == channel.iUniqueId; });
  if (myChannel == channels->cend())
  {
    XBMC->Log(LOG_DEBUG, "Channel not found");
    return PVR_ERROR_NO_ERROR;
  }

  const auto epg_i = epg->find(myChannel->strTvgId);
  if (epg_i == epg->cend() || epg_i->second.epg.size() == 0)
  {
    XBMC->Log(LOG_DEBUG, "EPG not found");
    return PVR_ERROR_NO_ERROR;
  }

  for (const auto myTag : epg_i->second.epg)
  {
    EPG_TAG tag;
    memset(&tag, 0, sizeof(EPG_TAG));

    tag.iUniqueBroadcastId  = myTag.second.iBroadcastId;
    tag.strTitle            = strdup(myTag.second.strTitle.c_str());
    tag.iChannelNumber      = myTag.second.iChannelId;
    tag.startTime           = myTag.second.startTime;
    tag.endTime             = myTag.second.endTime;
    tag.strPlotOutline      = strdup(myTag.second.strPlotOutline.c_str());
    tag.strPlot             = strdup(myTag.second.strPlot.c_str());
    tag.strIconPath         = strdup(myTag.second.strIconPath.c_str());
    tag.iGenreType          = EPG_GENRE_USE_STRING;        //myTag.second.iGenreType;
    tag.iGenreSubType       = 0;                           //myTag.second.iGenreSubType;
    tag.strGenreDescription = strdup(myTag.second.strGenreString.c_str());

    xbmc_tags.push_back(std::move(tag));
  }
  for (const auto & tag : xbmc_tags)
  {
    PVR->TransferEpgEntry(handle, &tag);
    free(const_cast<char *>(tag.strTitle));
    free(const_cast<char *>(tag.strPlotOutline));
    free(const_cast<char *>(tag.strPlot));
    free(const_cast<char *>(tag.strIconPath));
    free(const_cast<char *>(tag.strGenreDescription));
  }

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
  decltype (m_recordings) recordings;
  decltype (m_virtualTimeshiftRecording) virtual_recording;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
    virtual_recording = m_virtualTimeshiftRecording;
  }
  return recordings->size() + (virtual_recording ? 1 : 0);
}

PVR_ERROR PVRIptvData::GetRecordings(ADDON_HANDLE handle)
{
  decltype (m_recordings) recordings;
  decltype (m_virtualTimeshiftRecording) virtual_recording;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
    virtual_recording = m_virtualTimeshiftRecording;
  }

  std::vector<PVR_RECORDING> xbmc_records;
  auto insert_lambda = [&xbmc_records] (const PVRIptvRecording & rec)
  {
    PVR_RECORDING xbmcRecord;
    memset(&xbmcRecord, 0, sizeof(PVR_RECORDING));

    strncpy(xbmcRecord.strRecordingId, rec.strRecordId.c_str(), sizeof(xbmcRecord.strRecordingId) - 1);
    strncpy(xbmcRecord.strTitle, rec.strTitle.c_str(), sizeof(xbmcRecord.strTitle) - 1);
    strncpy(xbmcRecord.strDirectory, rec.strDirectory.c_str(), sizeof(xbmcRecord.strDirectory) - 1);
    strncpy(xbmcRecord.strStreamURL, rec.strStreamUrl.c_str(), sizeof(xbmcRecord.strStreamURL) - 1);
    strncpy(xbmcRecord.strChannelName, rec.strChannelName.c_str(), sizeof(xbmcRecord.strChannelName) - 1);
    xbmcRecord.recordingTime = rec.startTime;
    strncpy(xbmcRecord.strPlotOutline, rec.strPlotOutline.c_str(), sizeof(xbmcRecord.strPlotOutline) - 1);
    strncpy(xbmcRecord.strPlot, rec.strPlotOutline.c_str(), sizeof(xbmcRecord.strPlot) - 1);
    xbmcRecord.iDuration = rec.duration;

    xbmc_records.push_back(std::move(xbmcRecord));
  };

  std::for_each(recordings->cbegin(), recordings->cend(), insert_lambda);

  if (virtual_recording)
    insert_lambda(*virtual_recording);

  for (const auto & xbmcRecord : xbmc_records)
  {
    PVR->TransferRecordingEntry(handle, &xbmcRecord);
  }

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
    strncpy(xbmcTimer.strTitle, timer.strTitle.c_str(), sizeof(xbmcTimer.strTitle) - 1);
    strncpy(xbmcTimer.strSummary, timer.strSummary.c_str(), sizeof(xbmcTimer.strSummary) - 1);

    xbmc_timers.push_back(std::move(xbmcTimer));
  }
  for (const auto & xbmcTimer : xbmc_timers)
  {
    PVR->TransferTimerEntry(handle, &xbmcTimer);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::AddTimer(const PVR_TIMER &timer, bool virtualTimeshift)
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  decltype (m_virtualTimeshiftRecording) virtual_recording;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
    virtual_recording = m_virtualTimeshiftRecording;
  }

  const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&timer] (const PVRIptvChannel & ch) { return ch.iUniqueId == timer.iClientChannelUid; });
  if (channel_i == channels->cend())
  {
    XBMC->Log(LOG_ERROR, "%s - channel not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }
  const auto epg_channel_i = epg->find(channel_i->strTvgId);
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
  if (virtualTimeshift)
  {
    // create the timeshift "virtual" recording entry
    if (epg_entry.availableTimeshift)
    {
      std::shared_ptr<PVRIptvRecording> recording = std::make_shared<PVRIptvRecording>();
      if (m_manager.getTimeShiftInfo(epg_entry.strEventId, recording->strStreamUrl, recording->duration))
      {
        std::string title = "Timeshift - ";
        title += channel_i->strChannelName;
        title += " - ";
        title += epg_entry.strTitle;

        recording->strRecordId = VIRTUAL_TIMESHIFT_ID;
        recording->strTitle = title;
        //recording->strDirectory = directory;
        recording->strChannelName = channel_i->strChannelName;
        recording->startTime = epg_entry.startTime;
        recording->strPlotOutline = epg_entry.strPlot;
        recording->duration = epg_entry.endTime - epg_entry.startTime;

        m_virtualTimeshiftRecording = std::move(recording);
        PVR->TriggerRecordingUpdate();
        return PVR_ERROR_NO_ERROR;
      }
    }
  } else
  {
    if (m_manager.addTimer(epg_entry.strEventId))
    {
      SetLoadRecordings();
      return PVR_ERROR_NO_ERROR;
    }
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR PVRIptvData::DeleteRecord(const string &strRecordId)
{
  if (strRecordId == VIRTUAL_TIMESHIFT_ID)
  {
    m_virtualTimeshiftRecording.reset();
    PVR->TriggerRecordingUpdate();
    return PVR_ERROR_NO_ERROR;
  }
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
