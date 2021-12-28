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

#ifndef sledovanitvcz_Data_h
#define sledovanitvcz_Data_h

#include <vector>
#include "kodi/addon-instance/PVR.h"
#include <thread>
#include "ApiManager.h"
#include <mutex>
#include <memory>
#include <condition_variable>
#include <map>

namespace sledovanitvcz
{

struct EpgEntry
{
  unsigned    iBroadcastId;
  int         iChannelId;
  int         iGenreType;
  int         iGenreSubType;
  time_t      startTime;
  time_t      endTime;
  std::string strTitle;
  std::string strPlotOutline;
  std::string strPlot;
  std::string strIconPath;
  std::string strGenreString;
  std::string strEventId;
  bool availableTimeshift;
  std::string strRecordId; // optionally recorded
};

typedef std::map<time_t, EpgEntry> epg_entry_container_t;
struct EpgChannel
{
  std::string                  strId;
  std::string                  strName;
  epg_entry_container_t epg;
};

struct Channel
{
  bool        bIsRadio;
  int         iUniqueId;
  int         iChannelNumber;
  int         iEncryptionSystem;
  int         iTvgShift;
  std::string strChannelName;
  std::string strIconPath;
  std::string strStreamURL;
  std::string strId;
  std::string strGroupId;
  std::string strStreamType;
  bool        bIsPinLocked;
};

struct ChannelGroup
{
  bool              bRadio;
  std::string       strGroupId;
  std::string       strGroupName;
  std::vector<int>  members;
};

struct Recording
{
  std::string		strRecordId;
  std::string		strTitle;
  std::string		strStreamUrl;
  std::string		strPlotOutline;
  std::string		strPlot;
  std::string		strChannelName;
  time_t		startTime;
  int			duration;
  std::string strDirectory;
  bool bRadio;
  int iLifeTime;
  std::string strStreamType;
  int iChannelUid;
  bool bIsPinLocked;
};

struct Timer
{
  unsigned int    iClientIndex;
  int             iClientChannelUid;
  time_t          startTime;
  time_t          endTime;
  PVR_TIMER_STATE state;                                     /*!< @brief (required) the state of this timer */
  std::string     strTitle;
  std::string     strSummary;
  int             iLifetime;
  bool            bIsRepeating;
  time_t          firstDay;
  int             iWeekdays;
  int             iEpgUid;
  unsigned int    iMarginStart;
  unsigned int    iMarginEnd;
  int             iGenreType;
  int             iGenreSubType;
  int iLifeTime;
  std::string strDirectory;
};

typedef std::vector<ChannelGroup> group_container_t;
typedef std::vector<Channel> channel_container_t;
typedef std::map<std::string, EpgChannel> epg_container_t;
typedef std::vector<Recording> recording_container_t;
typedef std::vector<Timer> timer_container_t;
typedef std::map<std::string, std::string> properties_t;

class ATTR_DLL_LOCAL Data : public kodi::addon::CAddonBase,
                            public kodi::addon::CInstancePVRClient
{
public:
  Data();
  virtual ~Data(void);

  ADDON_STATUS Create() override;
  ADDON_STATUS SetSetting(const std::string & settingName, const kodi::addon::CSettingValue & settingValue) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable) override;
  PVR_ERROR IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable) override;
  PVR_ERROR GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR SetEPGMaxFutureDays(int iFutureDays) override;
  PVR_ERROR SetEPGMaxPastDays(int iPastDays) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingStreamProperties(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;

  bool LoggedIn() const;

protected:
  static int ParseDateTime(std::string strDate);

protected:
  bool KeepAlive();
  void KeepAliveJob();
  bool LoadPlayList(void);
  bool LoadEPG(time_t iStart, bool bSmallStep);
  void ReleaseUnneededEPG();
  //! \return true if actual update was performed
  bool LoadEPGJob();
  bool LoadRecordings();
  bool LoadRecordingsJob();
  void SetLoadRecordings();
  void LoginLoop();
  bool WaitForChannels() const;
  void TriggerFullRefresh();
  bool RecordingExists(const std::string & recordId) const;
  std::string ChannelsList() const;
  std::string ChannelStreamType(const std::string & channelId) const;
  bool PinCheckUnlock(bool isPinLocked);
  std::vector<kodi::addon::PVRStreamProperty> StreamProperties(const std::string & url, const std::string & streamType, bool isLive) const;
  PVR_ERROR GetChannelStreamUrl(const kodi::addon::PVRChannel& channel, std::string & streamUrl, std::string & streamType);
  PVR_ERROR GetEPGStreamUrl(const kodi::addon::PVREPGTag& tag, std::string & streamUrl, std::string & streamType);
  PVR_ERROR GetRecordingStreamUrl(const std::string & recording, std::string & streamUrl, std::string & streamType);
  PVR_ERROR SetEPGMaxDays(int iFutureDays, int iPastDays);

protected:
  void Process(void);

private:
  bool                              m_bKeepAlive;
  bool                              m_bLoadRecordings;
  mutable std::mutex                m_mutex;
  bool                              m_bChannelsLoaded;
  mutable std::condition_variable   m_waitCond;
  std::thread                       m_thread;

  // stored data from backend (used by multiple threads...)
  std::shared_ptr<const group_container_t> m_groups;
  std::shared_ptr<const channel_container_t> m_channels;
  std::shared_ptr<const epg_container_t> m_epg;
  std::shared_ptr<const recording_container_t> m_recordings;
  std::shared_ptr<const timer_container_t> m_timers;
  long long m_recordingAvailableDuration;
  long long m_recordingRecordedDuration;
  time_t m_epgMinTime;
  time_t m_epgMaxTime;
  int m_epgMaxFutureDays;
  int m_epgMaxPastDays;

  // data used only by "job" thread
  bool m_bEGPLoaded;
  time_t m_iLastStart;
  time_t m_iLastEnd;
  ApiManager::StreamQuality_t m_streamQuality;
  unsigned m_fullChannelEpgRefresh; //!< delay (seconds) between full channel/EPG refresh
  unsigned m_loadingsRefresh; //!< delay (seconds) between loadings refresh
  unsigned m_keepAliveDelay; //!< delay (seconds) between keepalive calls
  unsigned m_epgCheckDelay; //!< delay (seconds) between checking if EPG load is needed
  bool m_useH265; //!< flag, if h265 codec should be requested
  bool m_useAdaptive; //!< flag, if inpustream.adaptive (aka adaptive bitrate streaming) should be used/requested
  bool m_showLockedChannels; //!< flag, if unavailable/locked channels should be presented
  bool m_showLockedOnlyPin; //!< flag, if PIN-locked only channels should be presented

  ApiManager                        m_manager;
};

} //namespace sledovanitvcz
#endif // sledovanitvcz_Data_h
