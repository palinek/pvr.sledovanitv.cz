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

#include <set>
#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <chrono>
#include <algorithm>
#include <functional>

#include "Data.h"
#include "CallLimiter.hh"
#include "kodi/General.h"
#include "kodi/Filesystem.h"
#include "kodi/gui/dialogs/Numeric.h"

#if defined(TARGET_WINDOWS)
# define LOCALTIME_R(src, dst) localtime_s(dst, src)
# define GMTIME_R(src, dst) gmtime_s(dst, src)
#else
# define LOCALTIME_R(src, dst) localtime_r(src, dst)
# define GMTIME_R(src, dst) gmtime_r(src, dst)
#endif

namespace sledovanitvcz
{

static unsigned DiffBetweenUtcAndLocalTime(const time_t * when = nullptr, int * isdst = nullptr)
{
  time_t tloc;
  if (0 == when)
    time(&tloc);
  else
    tloc = *when;

  struct tm tm1;
  LOCALTIME_R(&tloc, &tm1);
  auto l_isdst = tm1.tm_isdst;
  if (isdst)
    *isdst = l_isdst;
  GMTIME_R(&tloc, &tm1);
  tm1.tm_isdst = l_isdst;
  time_t t2 = mktime(&tm1);

  return tloc - t2;
}

static inline unsigned DiffBetweenPragueAndLocalTime(const time_t * when = nullptr)
{
  int isdst = -1;
  auto diff =  DiffBetweenUtcAndLocalTime(when, &isdst);
  // Note: Prague(Czech) is in Central Europe Time -> CET or CEST == UTC+1 or UTC+2 == +3600 or +7200
  return diff - (isdst > 0 ? 7200 : 3600);
}

Data::Data()
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
  , m_epgMinTime{time(nullptr)}
  , m_epgMaxTime{time(nullptr) + 3600}
  , m_epgMaxFutureDays{EpgMaxFutureDays()}
  , m_epgMaxPastDays{EpgMaxPastDays()}
  , m_bEGPLoaded{false}
  , m_iLastStart{0}
  , m_iLastEnd{0}
  , m_manager{
    kodi::addon::GetSettingEnum<ApiManager::ServiceProvider_t>("serviceProvider", ApiManager::SP_DEFAULT)
    , kodi::addon::GetSettingString("userName")
    , kodi::addon::GetSettingString("password")
    , kodi::addon::GetSettingString("deviceId")
    , kodi::addon::GetSettingString("productId")
  }
{

  SetEPGMaxDays(m_epgMaxFutureDays, m_epgMaxPastDays);

  m_thread = std::thread{[this] { Process(); }};
}

bool Data::LoadRecordingsJob()
{
  if (!KeepAlive())
    return false;

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
  return load;
}

void Data::SetLoadRecordings()
{
  std::lock_guard<std::mutex> critical(m_mutex);
  m_bLoadRecordings = true;
}

void Data::TriggerFullRefresh()
{
  kodi::Log(ADDON_LOG_INFO, "%s triggering channels/EGP full refresh", __FUNCTION__);
  m_iLastEnd = 0;
  m_iLastStart = 0;

  int future_days = 0, past_days = 0;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    future_days = m_epgMaxFutureDays;
    past_days = m_epgMaxPastDays;
  }
  SetEPGMaxDays(future_days, past_days);
  LoadPlayList();
}

bool Data::LoadEPGJob()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s will check if EGP loading needed", __FUNCTION__);
  time_t min_epg, max_epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    min_epg = m_epgMinTime;
    max_epg = m_epgMaxTime;
  }
  bool updated = false;
  if (KeepAlive() && 0 == m_iLastEnd)
  {
    // the first run...load just needed data as soon as posible
    LoadEPG(time(nullptr), true);
    updated = true;
  } else
  {
    if (KeepAlive() && max_epg > m_iLastEnd)
    {
      time_t start = m_iLastEnd + DiffBetweenUtcAndLocalTime(&m_iLastEnd);
      LoadEPG(start - (start % 86400) - DiffBetweenUtcAndLocalTime(&start), false);
      updated = true;
    }
    if (KeepAlive() && min_epg < m_iLastStart)
    {
      time_t start = m_iLastStart - 86400;
      start += DiffBetweenUtcAndLocalTime(&start);
      LoadEPG(start - (start % 86400) - DiffBetweenUtcAndLocalTime(&start), false);
      updated = true;
    }
  }
  if (KeepAlive())
    ReleaseUnneededEPG();
  return updated;
}

void Data::ReleaseUnneededEPG()
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
  kodi::Log(ADDON_LOG_DEBUG, "%s min_epg=%s max_epg=%s", __FUNCTION__, ApiManager::formatTime(min_epg).c_str(), ApiManager::formatTime(max_epg).c_str());

  for (const auto & epg_channel : *epg)
  {
    auto & epg_data = epg_channel.second.epg;
    std::vector<time_t> to_delete;
    for (auto entry_i = epg_data.cbegin(); entry_i != epg_data.cend(); ++entry_i)
    {
      const EpgEntry & entry = entry_i->second;
      if (entry_i->second.startTime > max_epg || entry_i->second.endTime < min_epg)
      {
        kodi::Log(ADDON_LOG_DEBUG, "Removing TV show: %s - %s, start=%s end=%s", epg_channel.second.strName.c_str(), entry.strTitle.c_str()
            , ApiManager::formatTime(entry.startTime).c_str(), ApiManager::formatTime(entry.endTime).c_str());
        // notify about the epg change...and delete it
        kodi::addon::PVREPGTag tag;
        tag.SetSeriesNumber(EPG_TAG_INVALID_SERIES_EPISODE);
        tag.SetEpisodeNumber(EPG_TAG_INVALID_SERIES_EPISODE);
        tag.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);
        tag.SetUniqueBroadcastId(entry.iBroadcastId);
        tag.SetUniqueChannelId(entry.iChannelId);
        EpgEventStateChange(tag, EPG_EVENT_DELETED);

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

    {
      std::lock_guard<std::mutex> critical(m_mutex);
      m_epg = std::move(epg_copy);
    }
  }

  // narrow the loaded time info (if needed)
  m_iLastStart = std::max(m_iLastStart, min_epg);
  m_iLastEnd = std::min(m_iLastEnd, max_epg);
}

void Data::KeepAliveJob()
{
  if (!KeepAlive())
    return;

  kodi::Log(ADDON_LOG_DEBUG, "keepAlive:: trigger");
  if (!m_manager.keepAlive())
  {
    LoginLoop();
  }
}

void Data::LoginLoop()
{
  unsigned login_delay = 0;
  for ( ; KeepAlive(); --login_delay)
  {
    if (0 >= login_delay)
    {
      if (m_manager.login())
      {
        ConnectionStateChange("Connected", PVR_CONNECTION_STATE_CONNECTED, "");
        break;
      }
      else
      {
        ConnectionStateChange("Disconnected", PVR_CONNECTION_STATE_DISCONNECTED, "");
        login_delay = 30; // try in 30 seconds
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds{1});
  }
}

bool Data::WaitForChannels() const
{
  std::unique_lock<std::mutex> critical(m_mutex);
  return m_waitCond.wait_for(critical, std::chrono::seconds{5}, [this] { return m_bChannelsLoaded; });
}

void Data::Process(void)
{
  kodi::Log(ADDON_LOG_DEBUG, "keepAlive:: thread started");

  LoginLoop();

  LoadPlayList();

  bool epg_updated = false;

  auto keep_alive_job = getCallLimiter(std::bind(&Data::KeepAliveJob, this), std::chrono::seconds{m_keepAliveDelay}, true);
  auto trigger_full_refresh = getCallLimiter(std::bind(&Data::TriggerFullRefresh, this), std::chrono::seconds{m_fullChannelEpgRefresh}, true);
  auto trigger_load_recordings = getCallLimiter(std::bind(&Data::SetLoadRecordings, this), std::chrono::seconds{m_loadingsRefresh}, true);
  auto epg_dummy_trigger = getCallLimiter([] {}, std::chrono::seconds{m_epgCheckDelay}, false); // using the CallLimiter just to test if the epg should be done

  bool work_done = true;
  while (KeepAlive())
  {
    if (!work_done)
      std::this_thread::sleep_for(std::chrono::seconds{1});

    work_done = false;

    work_done |= LoadRecordingsJob();

    // trigger full refresh once a time
    work_done |= trigger_full_refresh.Call();
    // trigger loading of recordings once a time
    work_done |= trigger_load_recordings.Call();

    if (epg_dummy_trigger.Call() || epg_updated)
    {
      // perform epg loading in next cycle if something updated in this one
      epg_updated = LoadEPGJob();
      work_done = true;
    } else
    {
      epg_updated = false;
    }

    // do keep alive call once a time
    work_done |= keep_alive_job.Call();
  }
  kodi::Log(ADDON_LOG_DEBUG, "keepAlive:: thread stopped");
}

Data::~Data(void)
{
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_bKeepAlive = false;
  }
  m_thread.join();
  kodi::Log(ADDON_LOG_DEBUG, "%s destructed", __FUNCTION__);
}

ADDON_STATUS Data::Create()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the PVR sledovanitv.cz (unofficial)", __FUNCTION__);

  if (!kodi::vfs::DirectoryExists(UserPath()))
  {
    kodi::vfs::CreateDirectory(UserPath());
  }

  m_streamQuality = kodi::addon::GetSettingEnum<ApiManager::StreamQuality_t>("streamQuality", ApiManager::SQ_DEFAULT);
  m_fullChannelEpgRefresh = kodi::addon::GetSettingInt("fullChannelEpgRefresh", 24) * 3600; // make it seconds
  m_loadingsRefresh = kodi::addon::GetSettingInt("loadingsRefresh", 60);
  m_keepAliveDelay = kodi::addon::GetSettingInt("keepAliveDelay", 20);
  m_epgCheckDelay = kodi::addon::GetSettingInt("epgCheckDelay", 1) * 60; // make it seconds
  m_useH265 = kodi::addon::GetSettingBoolean("useH265", false);
  m_useAdaptive = kodi::addon::GetSettingBoolean("useAdaptive", false);
  m_showLockedChannels = kodi::addon::GetSettingBoolean("showLockedChannels", true);
  m_showLockedOnlyPin = kodi::addon::GetSettingBoolean("showLockedOnlyPin", true);

  return ADDON_STATUS_OK;
}

ADDON_STATUS Data::SetSetting(const std::string & settingName, const kodi::addon::CSettingValue & settingValue)
{
  // just force our data to be re-created
  return ADDON_STATUS_NEED_RESTART;
}

PVR_ERROR Data::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetSupportsChannelSettings(false);
  capabilities.SetHandlesInputStream(false);
  capabilities.SetHandlesDemuxing(false);
  capabilities.SetSupportsRecordingPlayCount(false);
  capabilities.SetSupportsLastPlayedPosition(false);
  capabilities.SetSupportsRecordingEdl(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetBackendName(std::string& name)
{
  name = "PVR sledovanitv.cz (unofficial)";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetBackendVersion(std::string& version)
{
  version = "";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

bool Data::KeepAlive()
{
  std::lock_guard<std::mutex> critical(m_mutex);
  return m_bKeepAlive;
}

bool Data::LoadEPG(time_t iStart, bool bSmallStep)
{
  const int step = bSmallStep ? 3600 : 86400;
  kodi::Log(ADDON_LOG_DEBUG, "%s last start %s, start %s, last end %s, end %s", __FUNCTION__, ApiManager::formatTime(m_iLastStart).c_str()
      , ApiManager::formatTime(iStart).c_str(), ApiManager::formatTime(m_iLastEnd).c_str(), ApiManager::formatTime(iStart + step).c_str());
  if (m_bEGPLoaded && m_iLastStart != 0 && iStart >= m_iLastStart && iStart + step <= m_iLastEnd)
    return false;

  Json::Value root;

  if (!m_manager.getEpg(iStart, bSmallStep, std::string() /*ChannelsList()*/, root))
  {
    kodi::Log(ADDON_LOG_INFO, "Cannot parse EPG data. EPG not loaded.");
    m_bEGPLoaded = true;
    return false;
  }

  if (m_iLastEnd == 0)
  {
    // the first run
    m_iLastStart = m_iLastEnd = iStart;
  } else
  {
    if (m_iLastStart > iStart)
      m_iLastStart = iStart;
    if (iStart + step > m_iLastEnd)
      m_iLastEnd = iStart + step;
  }

  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
    // extend min/max (if needed)
    m_epgMinTime = std::min(m_epgMinTime, m_iLastStart);
    m_epgMaxTime = std::max(m_epgMaxTime, m_iLastEnd);
  }

  auto epg_copy = std::make_shared<epg_container_t>(*epg);

  Json::Value json_channels = root["channels"];
  Json::Value::Members chIds = json_channels.getMemberNames();
  for (Json::Value::Members::iterator i = chIds.begin(); i != chIds.end(); i++)
  {
    std::string strChId = *i;

    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&strChId] (const Channel & ch) { return ch.strId == strChId; });
    if (channel_i != channels->cend())
    {
      EpgChannel & epgChannel = (*epg_copy)[strChId];
      epgChannel.strId = strChId;

      Json::Value epgData = json_channels[strChId];
      for (unsigned int j = 0; j < epgData.size(); j++)
      {
        Json::Value epgEntry = epgData[j];

        const time_t start_time = ParseDateTime(epgEntry.get("startTime", "").asString());
        const time_t end_time = ParseDateTime(epgEntry.get("endTime", "").asString());
        EpgEntry iptventry;
        iptventry.iBroadcastId = start_time; // unique id for channel (even if time_t is wider, int should be enough for short period of time)
        iptventry.iGenreType = 0;
        iptventry.iGenreSubType = 0;
        iptventry.iChannelId = channel_i->iUniqueId;
        iptventry.strTitle = epgEntry.get("title", "").asString();
        iptventry.strPlot = epgEntry.get("description", "").asString();
        iptventry.startTime = start_time;
        iptventry.endTime = end_time;
        iptventry.strEventId = epgEntry.get("eventId", "").asString();
        iptventry.strIconPath = epgEntry.get("poster", "").asString();
        std::string availability = epgEntry.get("availability", "none").asString();
        iptventry.availableTimeshift = availability == "timeshift" || availability == "pvr";
        iptventry.strRecordId = epgEntry["recordId"].asString();

        kodi::Log(ADDON_LOG_DEBUG, "Loading TV show: %s - %s, start=%s(epoch=%llu)", strChId.c_str(), iptventry.strTitle.c_str()
            , epgEntry.get("startTime", "").asString().c_str(), static_cast<long long unsigned>(start_time));

        // notify about the epg change...and store it
        kodi::addon::PVREPGTag tag;
        tag.SetSeriesNumber(EPG_TAG_INVALID_SERIES_EPISODE);
        tag.SetEpisodeNumber(EPG_TAG_INVALID_SERIES_EPISODE);
        tag.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);

        tag.SetUniqueBroadcastId(iptventry.iBroadcastId);
        tag.SetUniqueChannelId(iptventry.iChannelId);
        tag.SetTitle(iptventry.strTitle);
        tag.SetStartTime(iptventry.startTime);
        tag.SetEndTime(iptventry.endTime);
        tag.SetPlotOutline(iptventry.strPlotOutline);
        tag.SetPlot(iptventry.strPlot);
        tag.SetIconPath(iptventry.strIconPath);
        tag.SetGenreType(EPG_GENRE_USE_STRING);        //iptventry.iGenreType;
        tag.SetGenreSubType(0);                        //iptventry.iGenreSubType;
        tag.SetGenreDescription(iptventry.strGenreString);

        auto result = epgChannel.epg.emplace(iptventry.startTime, iptventry);
        bool value_changed = !result.second;
        if (value_changed)
        {
          epgChannel.epg[iptventry.startTime] = std::move(iptventry);
        }

        EpgEventStateChange(tag, value_changed ? EPG_EVENT_UPDATED : EPG_EVENT_CREATED);
      }
    }
  }

  // atomic assign new version of the epg all epgs
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_epg = epg_copy;
  }

  m_bEGPLoaded = true;
  kodi::Log(ADDON_LOG_INFO, "EPG Loaded.");

  return true;
}

bool Data::LoadRecordings()
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
    kodi::Log(ADDON_LOG_INFO, "Cannot parse recordings.");
    return false;
  }

  available_duration = root["summary"].get("availableDuration", 0).asInt() / 60 * 1024; //report minutes as MB
  recorded_duration = root["summary"].get("recordedDuration", 0).asInt() / 60 * 1024;

  Json::Value records = root["records"];
  for (unsigned int i = 0; i < records.size(); i++)
  {
    Json::Value record = records[i];
    const std::string title = record.get("title", "").asString();
    const std::string locked = record.get("channelLocked", "none").asString();
    std::string directory;
    if (locked != "none")
    {
      //Note: std::make_unique is available from c++14
      directory = kodi::addon::GetLocalizedString(30201);
      directory += " - ";
      directory += locked;
      kodi::Log(ADDON_LOG_INFO, "Timer/recording '%s' is locked(%s)", title.c_str(), locked.c_str());
    }
    std::string str_ch_id = record.get("channel", "").asString();
    const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&str_ch_id] (const Channel & ch) { return ch.strId == str_ch_id; });
    Recording iptvrecording;
    Timer iptvtimer;
    time_t startTime = ParseDateTime(record.get("startTime", "").asString());
    int duration = record.get("duration", 0).asInt();
    time_t now;
    time(&now);

    if ((startTime + duration) < now)
    {
      char buf[256];
      sprintf(buf, "%d", record.get("id", 0).asInt());
      iptvrecording.strRecordId = buf;
      iptvrecording.strTitle = std::move(title);

      if (channel_i != channels->cend())
      {
        iptvrecording.strChannelName = channel_i->strChannelName;
        iptvrecording.iChannelUid = channel_i->iUniqueId;
      } else
      {
        iptvrecording.iChannelUid = PVR_CHANNEL_INVALID_UID;
      }
      iptvrecording.startTime = startTime;
      iptvrecording.strPlotOutline = record.get("event", "").get("description", "").asString();
      iptvrecording.duration = duration;
      iptvrecording.bRadio = channel_i->bIsRadio;
      iptvrecording.iLifeTime = (ParseDateTime(record.get("expires", "").asString() + "00:00") - now) / 86400;
      iptvrecording.strDirectory = std::move(directory);
      iptvrecording.bIsPinLocked = locked == "pin";

      kodi::Log(ADDON_LOG_DEBUG, "Loading recording '%s'", iptvrecording.strTitle.c_str());

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
      iptvtimer.strTitle = std::move(title);
      iptvtimer.iLifeTime = (ParseDateTime(record.get("expires", "").asString() + "00:00") - now) / 86400;
      iptvtimer.strDirectory = std::move(directory);

      kodi::Log(ADDON_LOG_DEBUG, "Loading timer '%s'", iptvtimer.strTitle.c_str());

      new_timers->push_back(iptvtimer);
    }

  }

  bool changed_r = new_recordings->size() != recordings->size();
  for (size_t i = 0; !changed_r && i < new_recordings->size(); ++i)
  {
    const auto & old_rec = (*recordings)[i];
    const auto & new_rec = (*new_recordings)[i];
    if (new_rec.strRecordId != old_rec.strRecordId || new_rec.strStreamUrl != old_rec.strStreamUrl)
    {
      changed_r = true;
      break;
    }
  }
  if (changed_r)
  {
    for (auto & recording : *new_recordings)
    {
      std::string channel_id;
      recording.strStreamUrl = m_manager.getRecordingUrl(recording.strRecordId, channel_id);
      // get the stream type based on channel
      recording.strStreamType = ChannelStreamType(channel_id);
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
      TriggerRecordingUpdate();
    }

    if (changed_t)
    {
      m_timers = std::move(new_timers);
      TriggerTimerUpdate();
    }
    m_recordingAvailableDuration = available_duration;
    m_recordingRecordedDuration = recorded_duration;
  }

  return true;
}

bool Data::LoadPlayList(void)
{
  if (!KeepAlive())
    return false;

  Json::Value root;

  if (!m_manager.getPlaylist(m_streamQuality, m_useH265, m_useAdaptive, root))
  {
    kodi::Log(ADDON_LOG_INFO, "Cannot get/parse playlist.");
    return false;
  }

  /*
  std::string qualities = m_manager.getStreamQualities();
  kodi::Log(ADDON_LOG_DEBUG, "Stream qualities: %s", qualities.c_str());
  */

  //channels
  auto new_channels = std::make_shared<channel_container_t>();
  Json::Value channels = root["channels"];
  for (unsigned int i = 0; i < channels.size(); i++)
  {
    Json::Value channel = channels[i];
    const std::string locked = channel.get("locked", "none").asString();
    if (locked != "none")
    {
      if (!m_showLockedChannels || (m_showLockedOnlyPin && locked != "pin"))
      {
        kodi::Log(ADDON_LOG_INFO, "Skipping locked(%s) channel#%u %s", locked.c_str(), i + 1, channel.get("name", "").asString().c_str());
        continue;
      }
    }

    Channel iptvchan;

    iptvchan.strId = channel.get("id", "").asString();
    iptvchan.strChannelName = channel.get("name", "").asString();
    iptvchan.strGroupId = channel.get("group", "").asString();
    iptvchan.strStreamURL = channel.get("url", "").asString();
    iptvchan.strStreamType = channel.get("streamType", "").asString();
    iptvchan.iUniqueId = i + 1;
    iptvchan.iChannelNumber = i + 1;
    kodi::Log(ADDON_LOG_DEBUG, "Channel#%d %s, URL: %s", iptvchan.iUniqueId, iptvchan.strChannelName.c_str(), iptvchan.strStreamURL.c_str());
    iptvchan.strIconPath = channel.get("logoUrl", "").asString();
    iptvchan.bIsRadio = channel.get("type", "").asString() != "tv";
    iptvchan.bIsPinLocked = locked == "pin";

    new_channels->push_back(iptvchan);
  }

  auto new_groups = std::make_shared<group_container_t>();
  Json::Value groups = root["groups"];
  for (const auto & group_id : groups.getMemberNames())
  {
    ChannelGroup group;
    group.bRadio = false; // currently there is no way to distinguish group types in the returned json
    group.strGroupId = group_id;
    group.strGroupName = groups[group_id].asString();
    for (const auto & channel : *new_channels)
    {
      if (channel.strGroupId == group_id && !channel.bIsRadio)
        group.members.push_back(channel.iUniqueId);
    }
    new_groups->push_back(std::move(group));
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d channels.", new_channels->size());
  kodi::QueueFormattedNotification(QUEUE_INFO, "%d channels loaded.", new_channels->size());


  bool channels_loaded;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_channels = std::move(new_channels);
    m_groups = std::move(new_groups);
    channels_loaded = m_bChannelsLoaded;
  }
  m_waitCond.notify_all();
  if (channels_loaded)
  {
    TriggerChannelUpdate();
    TriggerChannelGroupsUpdate();
  }

  return true;
}

PVR_ERROR Data::GetChannelsAmount(int& amount)
{
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
  }

  amount = channels->size();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s %s", __FUNCTION__, radio ? "radio" : "tv");
  WaitForChannels();

  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
  }

  for (const auto & channel : *channels)
  {
    if (channel.bIsRadio == radio)
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bIsRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
      kodiChannel.SetEncryptionSystem(channel.iEncryptionSystem);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);

      results.Add(kodiChannel);
    }
  }

  {
    std::lock_guard<std::mutex> critical(m_mutex);
    m_bChannelsLoaded = true;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  std::string streamUrl, streamType;
  PVR_ERROR ret = GetChannelStreamUrl(channel, streamUrl, streamType);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  properties = StreamProperties(streamUrl, streamType, true);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("sledovanitv.cz");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetChannelStreamUrl(const kodi::addon::PVRChannel& channel, std::string & streamUrl, std::string & streamType)
{
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
  }

  auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [channel] (const Channel & c) { return c.iUniqueId == channel.GetUniqueId(); });
  if (channels->cend() == channel_i)
  {
    kodi::Log(ADDON_LOG_INFO, "%s can't find channel %d", __FUNCTION__, channel.GetUniqueId());
    return PVR_ERROR_INVALID_PARAMETERS;
  }

  if (!PinCheckUnlock(channel_i->bIsPinLocked))
    return PVR_ERROR_REJECTED;

  streamUrl = channel_i->strStreamURL;
  streamType = channel_i->strStreamType;
  return PVR_ERROR_NO_ERROR;

}

PVR_ERROR Data::GetChannelGroupsAmount(int& amount)
{
  decltype (m_groups) groups;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    groups = m_groups;
  }
  amount = groups->size();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s %s", __FUNCTION__, radio ? "radio" : "tv");
  WaitForChannels();

  decltype (m_groups) groups;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    groups = m_groups;
  }

  for (const auto & group : *groups)
  {
    if (group.bRadio == radio)
    {
      kodi::addon::PVRChannelGroup kodiGroup;

      kodiGroup.SetIsRadio(radio);
      kodiGroup.SetGroupName(group.strGroupName);

      results.Add(kodiGroup);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s %s", __FUNCTION__, group.GetGroupName().c_str());
  WaitForChannels();

  decltype (m_groups) groups;
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    groups = m_groups;
    channels = m_channels;
  }

  std::vector<kodi::addon::PVRChannelGroupMember> kodi_group_members;
  auto group_i = std::find_if(groups->cbegin(), groups->cend(), [&group] (ChannelGroup const & g) { return g.strGroupName == group.GetGroupName(); });
  if (group_i != groups->cend())
  {
    int order = 0;
    for (const auto & member : group_i->members)
    {
      if (member < 0 || member >= channels->size())
        continue;

      const Channel &channel = (*channels)[member];
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(group.GetGroupName());
      kodiGroupMember.SetChannelUniqueId(channel.iUniqueId);
      kodiGroupMember.SetChannelNumber(++order);

      kodi_group_members.push_back(std::move(kodiGroupMember));
    }
  }
  for (const auto & kodiGroupMember : kodi_group_members)
  {
    results.Add(kodiGroupMember);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s %i, from=%s to=%s", __FUNCTION__, channelUid, ApiManager::formatTime(start).c_str(), ApiManager::formatTime(end).c_str());
  std::lock_guard<std::mutex> critical(m_mutex);
  // Note: For future scheduled timers Kodi requests EPG (this function) with
  // start & end as given by the timer timespan. But we don't want to narrow
  // our EPG interval in such cases.
  m_epgMinTime = start < m_epgMinTime ? start : m_epgMinTime;
  m_epgMaxTime = end > m_epgMaxTime ? end : m_epgMaxTime;
  return PVR_ERROR_NO_ERROR;
}

static PVR_ERROR GetEPGData(const kodi::addon::PVREPGTag& tag
    , const channel_container_t * channels
    , const epg_container_t * epg
    , epg_entry_container_t::const_iterator & epg_i
    , bool * isChannelPinLocked = nullptr
    )
{
  auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [tag] (const Channel & c) { return c.iUniqueId == tag.GetUniqueChannelId(); });
  if (channels->cend() == channel_i)
  {
    kodi::Log(ADDON_LOG_INFO, "%s can't find channel %d", __FUNCTION__, tag.GetUniqueChannelId());
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  if (isChannelPinLocked)
    *isChannelPinLocked = channel_i->bIsPinLocked;

  auto ch_epg_i = epg->find(channel_i->strId);

  if (epg->cend() == ch_epg_i || (epg_i = ch_epg_i->second.epg.find(tag.GetUniqueBroadcastId())) == ch_epg_i->second.epg.cend())
  {
    kodi::Log(ADDON_LOG_INFO, "%s can't find EPG data for channel %s, time %d", __FUNCTION__, channel_i->strId.c_str(), tag.GetUniqueBroadcastId());
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable)
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

  isPlayable = epg_i->second.availableTimeshift && tag.GetStartTime() < time(nullptr);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable)
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

  isRecordable = epg_i->second.availableTimeshift && !RecordingExists(epg_i->second.strRecordId) && tag.GetStartTime() < time(nullptr);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  std::string streamUrl, streamType;
  PVR_ERROR ret = GetEPGStreamUrl(tag, streamUrl, streamType);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  properties = StreamProperties(streamUrl, streamType, false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetEPGStreamUrl(const kodi::addon::PVREPGTag& tag, std::string & streamUrl, std::string & streamType)
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }

  bool isPinLocked;
  epg_entry_container_t::const_iterator epg_i;
  PVR_ERROR ret = GetEPGData(tag, channels.get(), epg.get(), epg_i, &isPinLocked);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  if (!PinCheckUnlock(isPinLocked))
    return PVR_ERROR_REJECTED;

  if (RecordingExists(epg_i->second.strRecordId))
    return GetRecordingStreamUrl(epg_i->second.strRecordId, streamUrl, streamType);

  std::string channel_id;
  int duration;
  if (!m_manager.getTimeShiftInfo(epg_i->second.strEventId, streamUrl, channel_id, duration))
    return PVR_ERROR_INVALID_PARAMETERS;
  // get the stream type based on channel
  streamType = ChannelStreamType(channel_id);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::SetEPGMaxFutureDays(int iFutureDays)
{
  return SetEPGMaxDays(iFutureDays, -1);
}

PVR_ERROR Data::SetEPGMaxPastDays(int iPastDays)
{
  return SetEPGMaxDays(-1, iPastDays);
}

PVR_ERROR Data::SetEPGMaxDays(int iFutureDays, int iPastDays)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s iFutureDays=%d, iPastDays=%d", __FUNCTION__, iFutureDays, iPastDays);
  time_t now = time(nullptr);
  std::lock_guard<std::mutex> critical(m_mutex);
  m_epgMaxFutureDays = (iFutureDays == -1 ? m_epgMaxFutureDays : iFutureDays);
  m_epgMaxPastDays = (iPastDays == -1 ? m_epgMaxPastDays : iPastDays);
  m_epgMinTime = now - m_epgMaxPastDays * 86400;
  m_epgMaxTime = now + m_epgMaxFutureDays * 86400;

  return PVR_ERROR_NO_ERROR;
}

int Data::ParseDateTime(std::string strDate)
{
  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(tm));

  sscanf(strDate.c_str(), "%04d-%02d-%02d %02d:%02d", &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour, &timeinfo.tm_min);
  timeinfo.tm_sec = 0;

  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  time_t t = mktime(&timeinfo);
  return t - DiffBetweenPragueAndLocalTime(&t);
}

PVR_ERROR Data::GetRecordingsAmount(bool deleted, int& amount)
{
  decltype (m_recordings) recordings;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
  }
  amount = recordings->size();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  decltype (m_recordings) recordings;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
  }
  auto insert_lambda = [&results] (const Recording & rec)
  {
    kodi::addon::PVRRecording kodiRecord;

    kodiRecord.SetRecordingId(rec.strRecordId);
    kodiRecord.SetTitle(rec.strTitle);
    kodiRecord.SetDirectory(rec.strDirectory);
    kodiRecord.SetChannelName(rec.strChannelName);
    kodiRecord.SetRecordingTime(rec.startTime);
    kodiRecord.SetPlotOutline(rec.strPlotOutline);
    kodiRecord.SetPlot(rec.strPlotOutline);
    kodiRecord.SetDuration(rec.duration);
    kodiRecord.SetLifetime(rec.iLifeTime);
    kodiRecord.SetChannelUid(rec.iChannelUid);
    kodiRecord.SetChannelType(rec.bRadio ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV);

    results.Add(kodiRecord);
  };

  std::for_each(recordings->cbegin(), recordings->cend(), insert_lambda);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetRecordingStreamProperties(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  std::string streamUrl, streamType;
  PVR_ERROR ret = GetRecordingStreamUrl(recording.GetRecordingId(), streamUrl, streamType);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  properties = StreamProperties(streamUrl, streamType, false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetRecordingStreamUrl(const std::string & recording, std::string & streamUrl, std::string & streamType)
{
  decltype (m_recordings) recordings;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
  }
  auto rec_i = std::find_if(recordings->cbegin(), recordings->cend(), [recording] (const Recording & r) { return recording == r.strRecordId; });
  if (recordings->cend() == rec_i)
    return PVR_ERROR_INVALID_PARAMETERS;

  if (!PinCheckUnlock(rec_i->bIsPinLocked))
    return PVR_ERROR_REJECTED;

  streamUrl = rec_i->strStreamUrl;
  streamType = rec_i->strStreamType;
  return PVR_ERROR_NO_ERROR;
}

bool Data::RecordingExists(const std::string & recordId) const
{
  decltype (m_recordings) recordings;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    recordings = m_recordings;
  }
  return recordings->cend() != std::find_if(recordings->cbegin(), recordings->cend(), [&recordId] (const Recording & r) { return recordId == r.strRecordId; });
}

PVR_ERROR Data::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  int id = 0;
  kodi::addon::PVRTimerType type;

  type.SetId(++id);
  type.SetAttributes(PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME);
  kodi::Log(ADDON_LOG_DEBUG, "%s - id %i attributes: 0x%x", __FUNCTION__, id, type.GetAttributes());
  types.push_back(type);

  type.SetId(++id);
  type.SetAttributes(PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME);
  kodi::Log(ADDON_LOG_DEBUG, "%s - id %i attributes: 0x%x", __FUNCTION__, id, type.GetAttributes());
  types.push_back(type);

  type.SetId(++id);
  type.SetAttributes(PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME);
  kodi::Log(ADDON_LOG_DEBUG, "%s - id %i attributes: 0x%x", __FUNCTION__, id, type.GetAttributes());
  types.push_back(type);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::GetTimersAmount(int& amount)
{
  decltype (m_timers) timers;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    timers = m_timers;
  }
  amount = timers->size();
  return PVR_ERROR_NO_ERROR;
}


PVR_ERROR Data::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  decltype (m_timers) timers;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    timers = m_timers;
  }

  for (const auto & timer : *timers)
  {
    kodi::addon::PVRTimer kodiTimer;

    kodiTimer.SetClientIndex(timer.iClientIndex);
    kodiTimer.SetClientChannelUid(timer.iClientChannelUid);
    kodiTimer.SetStartTime(timer.startTime);
    kodiTimer.SetEndTime(timer.endTime);
    kodiTimer.SetState(timer.state);
    kodiTimer.SetTimerType(1); // Note: this must match some type from GetTimerTypes()
    kodiTimer.SetLifetime(timer.iLifeTime);
    kodiTimer.SetTitle(timer.strTitle);
    kodiTimer.SetSummary(timer.strSummary);
    kodiTimer.SetDirectory(timer.strDirectory);

    results.Add(kodiTimer);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Data::AddTimer(const kodi::addon::PVRTimer& timer)
{
  decltype (m_channels) channels;
  decltype (m_epg) epg;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
    epg = m_epg;
  }

  const auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&timer] (const Channel & ch) { return ch.iUniqueId == timer.GetClientChannelUid(); });
  if (channel_i == channels->cend())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - channel not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }
  const auto epg_channel_i = epg->find(channel_i->strId);
  if (epg_channel_i == epg->cend())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - epg channel not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }

  const auto epg_i = epg_channel_i->second.epg.find(timer.GetStartTime());
  if (epg_i == epg_channel_i->second.epg.cend())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - event not found", __FUNCTION__);
    return PVR_ERROR_SERVER_ERROR;
  }

  const EpgEntry & epg_entry = epg_i->second;
  std::string record_id;
  if (m_manager.addTimer(epg_entry.strEventId, record_id))
  {
    // update the record_id into EPG
    // Note: the m_epg/epg is read-only, so the keys must exist
    auto epg_copy = std::make_shared<epg_container_t>(*epg);
    (*epg_copy)[channel_i->strId].epg[timer.GetStartTime()].strRecordId = record_id;
    {
      std::lock_guard<std::mutex> critical(m_mutex);
      m_epg = epg_copy;
    }
    SetLoadRecordings();
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Data::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (m_manager.deleteRecord(recording.GetRecordingId()))
  {
    SetLoadRecordings();
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Data::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  if (m_manager.deleteRecord(std::to_string((timer.GetClientIndex()))))
  {
    SetLoadRecordings();
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Data::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    total = m_recordingAvailableDuration;
    used = m_recordingRecordedDuration;
  }
  return PVR_ERROR_NO_ERROR;
}

bool Data::LoggedIn() const
{
  return m_manager.loggedIn();
}

std::vector<kodi::addon::PVRStreamProperty> Data::StreamProperties(const std::string & url, const std::string & streamType, bool isLive) const
{
  static const std::set<std::string> ADAPTIVE_TYPES = {"mpd", "ism", "hls"};

  std::vector<kodi::addon::PVRStreamProperty> properties;
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  if (m_useAdaptive && 0 < ADAPTIVE_TYPES.count(streamType))
  {
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back("inputstream.adaptive.manifest_type", streamType);
  }
  if (isLive)
    properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "true");
  return properties;
}

std::string Data::ChannelsList() const
{
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
  }
  std::ostringstream os;
  bool first = true;
  std::for_each(channels->cbegin(), channels->cend(), [&os, &first] (channel_container_t::const_reference chan)
      {
        if (first)
          first = false;
        else
          os << ",";
        os << chan.strId;
      });
  return os.str();
}

std::string Data::ChannelStreamType(const std::string & channelId) const
{
  decltype (m_channels) channels;
  {
    std::lock_guard<std::mutex> critical(m_mutex);
    channels = m_channels;
  }

  std::string stream_type = "unknown";
  auto channel_i = std::find_if(channels->cbegin(), channels->cend(), [&channelId] (const Channel & c) { return c.strId == channelId; });
  if (channels->cend() == channel_i)
    kodi::Log(ADDON_LOG_INFO, "%s can't find channel %s", __FUNCTION__, channelId.c_str());
  else
    stream_type = channel_i->strStreamType;
  return stream_type;
}

bool Data::PinCheckUnlock(bool isPinLocked)
{
  if (!isPinLocked)
    return true;

  if (!m_manager.pinUnlocked())
  {
    //Note: std::make_unique is available from c++14
    std::string pin;
    if (kodi::gui::dialogs::Numeric::ShowAndGetNumber(pin, kodi::addon::GetLocalizedString(30202)))
    {
      if (!m_manager.pinUnlock(pin))
      {
        kodi::Log(ADDON_LOG_ERROR, "PIN-unlocking failed");
        return false;
      }
    } else
    {
      kodi::Log(ADDON_LOG_ERROR, "PIN-entering cancelled");
      return false;
    }
  }
  // unlocking can lead to unlock of recordings
  SetLoadRecordings();
  return true;
}

} // namespace sledovanitvcz

ADDONCREATOR(sledovanitvcz::Data)
