/*
 *      Copyright (c) 2018~now Palo Kisa <palo.kisa@gmail.com>
 *
 *      Copyright (C) 2013 Anton Fedchin
 *      http://github.com/afedchin/xbmc-addon-iptvsimple/
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

#include "client.h"

#include "PVRIptvData.h"
#include "xbmc_pvr_dll.h"

#include <iostream>
#include <atomic>

using namespace ADDON;

#ifdef TARGET_WINDOWS
#define snprintf _snprintf
#ifdef CreateDirectory
#undef CreateDirectory
#endif
#ifdef DeleteFile
#undef DeleteFile
#endif
#endif


ADDON_STATUS   m_CurStatus      = ADDON_STATUS_UNKNOWN;
static std::shared_ptr<PVRIptvData> m_data;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
static std::string g_strUserPath   = "";
static std::string g_strClientPath = "";
static int g_iEpgMaxDays = 0;

std::unique_ptr<CHelper_libXBMC_addon> XBMC;
std::unique_ptr<CHelper_libXBMC_pvr> PVR;

std::string PathCombine(const std::string &strPath, const std::string &strFileName)
{
  std::string strResult = strPath;
  if (strResult.at(strResult.size() - 1) == '\\' ||
      strResult.at(strResult.size() - 1) == '/')
  {
    strResult.append(strFileName);
  }
  else
  {
    strResult.append("/");
    strResult.append(strFileName);
  }

  return strResult;
}

std::string GetClientFilePath(const std::string &strFileName)
{
  return PathCombine(g_strClientPath, strFileName);
}

std::string GetUserFilePath(const std::string &strFileName)
{
  return PathCombine(g_strUserPath, strFileName);
}

static void ReadSettings(PVRIptvConfiguration & cfg)
{
  char buffer[1024];

  if (XBMC->GetSetting("userName", &buffer))
  {
    cfg.userName = buffer;
  }

  if (XBMC->GetSetting("password", &buffer))
  {
    cfg.password = buffer;
  }

  if (!XBMC->GetSetting("streamQuality", &cfg.streamQuality))
  {
    cfg.streamQuality = 0;
  }

  if (!XBMC->GetSetting("fullChannelEpgRefresh", &cfg.fullChannelEpgRefresh))
  {
    cfg.fullChannelEpgRefresh = 24;
  }
  // make it seconds
  cfg.fullChannelEpgRefresh *= 3600;

  if (!XBMC->GetSetting("loadingsRefresh", &cfg.loadingsRefresh))
  {
    cfg.loadingsRefresh = 60;
  }

  if (!XBMC->GetSetting("keepAliveDelay", &cfg.keepAliveDelay))
  {
    cfg.loadingsRefresh = 20;
  }

  if (!XBMC->GetSetting("epgCheckDelay", &cfg.epgCheckDelay))
  {
    cfg.epgCheckDelay = 1;
  }
  // make it seconds
  cfg.epgCheckDelay *= 60;

  if (!XBMC->GetSetting("useH265", &cfg.useH265))
  {
    cfg.useH265 = false;
  }

  if (!XBMC->GetSetting("useAdaptive", &cfg.useAdaptive))
  {
    cfg.useAdaptive = false;
  }

  if (!XBMC->GetSetting("showLockedChannels", &cfg.showLockedChannels))
  {
    cfg.showLockedChannels = true;
  }
}

static PVR_ERROR FillStreamProperties(const properties_t & props, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  if (*iPropertiesCount < props.size())
    return PVR_ERROR_INVALID_PARAMETERS;

  unsigned i = 0;
  for (const auto & prop : props)
  {
    XBMC->Log(LOG_DEBUG, "%s properties[%s]=%s", __FUNCTION__, prop.first.c_str(), prop.second.c_str());
    strncpy(properties[i].strName, prop.first.c_str(), sizeof(properties[i].strName) - 1);
    strncpy(properties[i].strValue, prop.second.c_str(), sizeof(properties[i].strValue) - 1);
    ++i;
  }
  *iPropertiesCount = i;

  return PVR_ERROR_NO_ERROR;
}

extern "C" {

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
  {
    return ADDON_STATUS_UNKNOWN;
  }

  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC.reset(new CHelper_libXBMC_addon);
  if (!XBMC->RegisterMe(hdl))
  {
    XBMC.reset(nullptr);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR.reset(new CHelper_libXBMC_pvr);
  if (!PVR->RegisterMe(hdl))
  {
    PVR.reset(nullptr);
    XBMC.reset(nullptr);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s - Creating the %s", __FUNCTION__, GetBackendName());

  m_CurStatus     = ADDON_STATUS_UNKNOWN;
  g_strUserPath   = pvrprops->strUserPath;
  g_strClientPath = pvrprops->strClientPath;

  if (!XBMC->DirectoryExists(g_strUserPath.c_str()))
  {
    XBMC->CreateDirectory(g_strUserPath.c_str());
  }

  PVRIptvConfiguration cfg;
  ReadSettings(cfg);
  cfg.epgMaxDays = pvrprops->iEpgMaxDays;

  std::atomic_store(&m_data, std::shared_ptr<PVRIptvData>{nullptr}); // be sure that the previous one is deleted before new is constructed
  std::atomic_store(&m_data, std::make_shared<PVRIptvData>(std::move(cfg)));
  m_CurStatus = ADDON_STATUS_OK;

  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

void ADDON_Destroy()
{
  std::atomic_store(&m_data, std::shared_ptr<PVRIptvData>{nullptr});
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // just force our data to be re-created
  return ADDON_STATUS_NEED_RESTART;
}

void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/
PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers          = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsChannelScan     = false;
  pCapabilities->bSupportsChannelSettings = false;
  pCapabilities->bHandlesInputStream      = false;
  pCapabilities->bHandlesDemuxing         = false;
  pCapabilities->bSupportsRecordingPlayCount = false;
  pCapabilities->bSupportsLastPlayedPosition = false;
  pCapabilities->bSupportsRecordingEdl     = false;
  pCapabilities->bSupportsRecordingsRename = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo   = false;
  pCapabilities->iRecordingsLifetimesSize  = 0;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "PVR sledovanitv.cz (unofficial)";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = "";
  return strBackendVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetDriveSpace(iTotal, iUsed);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, int iChannelUid, time_t iStart, time_t iEnd)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetEPGForChannel(handle, iChannelUid, iStart, iEnd);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SetEPGTimeFrame(int iDays)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->SetEPGTimeFrame(iDays);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR IsEPGTagPlayable(const EPG_TAG* tag, bool* bIsPlayable)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->IsEPGTagPlayable(tag, bIsPlayable);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR IsEPGTagRecordable(const EPG_TAG* tag, bool* bIsRecordable)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->IsEPGTagRecordable(tag, bIsRecordable);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG* tag, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  auto data = std::atomic_load(&m_data);
  if (!tag || !properties || !iPropertiesCount || !data)
    return PVR_ERROR_SERVER_ERROR;

  std::string stream_url, stream_type;
  PVR_ERROR ret = data->GetEPGStreamUrl(tag, stream_url, stream_type);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  return FillStreamProperties(data->GetStreamProperties(stream_url, stream_type, false), properties, iPropertiesCount);
}

int GetChannelsAmount(void)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetChannelsAmount();

  return -1;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetChannels(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  auto data = std::atomic_load(&m_data);
  if (!channel || !properties || !iPropertiesCount || !data)
    return PVR_ERROR_SERVER_ERROR;

  std::string stream_url, stream_type;
  PVR_ERROR ret = data->GetChannelStreamUrl(channel, stream_url, stream_type);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  return FillStreamProperties(data->GetStreamProperties(stream_url, stream_type, true), properties, iPropertiesCount);
}

int GetChannelGroupsAmount(void)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetChannelGroupsAmount();

  return -1;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetChannelGroups(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetChannelGroupMembers(handle, group);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "sledovanitv.cz");
  snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");

  return PVR_ERROR_NO_ERROR;
}

int GetRecordingsAmount(bool deleted)
{
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  if (deleted)
    return 0;

  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetRecordingsAmount();

  return 0;
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  if (deleted)
    return PVR_ERROR_NO_ERROR;

  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetRecordings(handle);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING* recording, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  auto data = std::atomic_load(&m_data);
  if (!recording || !properties || !iPropertiesCount || !data)
    return PVR_ERROR_SERVER_ERROR;

  std::string stream_url, stream_type;
  PVR_ERROR ret = data->GetRecordingStreamUrl(recording->strRecordingId, stream_url, stream_type);
  if (PVR_ERROR_NO_ERROR != ret)
    return ret;

  return FillStreamProperties(data->GetStreamProperties(stream_url, stream_type, false), properties, iPropertiesCount);
}

/** TIMER FUNCTIONS */
int GetTimersAmount(void)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetTimersAmount();

  return -1;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->GetTimers(handle);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  XBMC->Log(LOG_DEBUG, "%s - size: %d", __FUNCTION__, *size);
  int pos = 0;
  types[pos].iId = pos + 1;
  types[pos].iAttributes = PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME;
  types[pos].strDescription[0] = '\0'; // let Kodi generate the description
  types[pos].iPrioritiesSize = 0; // no priorities needed
  //types[pos].priorities
  //types[pos].iPrioritiesDefault = 0;
  types[pos].iLifetimesSize = 0; // no lifetime settings supported yet
  //types[pos].lifetimes
  //types[pos].iLifetimesDefault = 0;
  types[pos].iPreventDuplicateEpisodesSize = 0;
  //types[pos].preventDuplicateEpisodes
  //types[pos].iPreventDuplicateEpisodesDefault = 0;
  types[pos].iRecordingGroupSize = 0;
  //types[pos].maxRecordings
  //types[pos].iRecordingGroupDefault = 0;
  types[pos].iMaxRecordingsSize = 0;
  //types[pos].maxRecordings
  //types[pos].iMaxRecordingsDefault = 0;
  XBMC->Log(LOG_DEBUG, "%s - attributes: 0x%x", __FUNCTION__, types[pos].iAttributes);

  ++pos;
  types[pos].iId = pos + 1;
  types[pos].iAttributes = PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME;
  types[pos].strDescription[0] = '\0'; // let Kodi generate the description
  types[pos].iPrioritiesSize = 0; // no priorities needed
  //types[pos].priorities
  //types[pos].iPrioritiesDefault = 0;
  types[pos].iLifetimesSize = 0; // no lifetime settings supported yet
  //types[pos].lifetimes
  //types[pos].iLifetimesDefault = 0;
  types[pos].iPreventDuplicateEpisodesSize = 0;
  //types[pos].preventDuplicateEpisodes
  //types[pos].iPreventDuplicateEpisodesDefault = 0;
  types[pos].iRecordingGroupSize = 0;
  //types[pos].maxRecordings
  //types[pos].iRecordingGroupDefault = 0;
  types[pos].iMaxRecordingsSize = 0;
  //types[pos].maxRecordings
  //types[pos].iMaxRecordingsDefault = 0;
  XBMC->Log(LOG_DEBUG, "%s - attributes: 0x%x", __FUNCTION__, types[pos].iAttributes);

  ++pos;
  types[pos].iId = pos + 1;
  types[pos].iAttributes = PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME;
  types[pos].strDescription[0] = '\0'; // let Kodi generate the description
  types[pos].iPrioritiesSize = 0; // no priorities needed
  //types[pos].priorities
  //types[pos].iPrioritiesDefault = 0;
  types[pos].iLifetimesSize = 0; // no lifetime settings supported yet
  //types[pos].lifetimes
  //types[pos].iLifetimesDefault = 0;
  types[pos].iPreventDuplicateEpisodesSize = 0;
  //types[pos].preventDuplicateEpisodes
  //types[pos].iPreventDuplicateEpisodesDefault = 0;
  types[pos].iRecordingGroupSize = 0;
  //types[pos].maxRecordings
  //types[pos].iRecordingGroupDefault = 0;
  types[pos].iMaxRecordingsSize = 0;
  //types[pos].maxRecordings
  //types[pos].iMaxRecordingsDefault = 0;
  XBMC->Log(LOG_DEBUG, "%s - attributes: 0x%x", __FUNCTION__, types[pos].iAttributes);

  *size = pos + 1;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  XBMC->Log(LOG_DEBUG, "%s - type %d", __FUNCTION__, timer.iTimerType);
  auto data = std::atomic_load(&m_data);
  if (!data)
    return PVR_ERROR_SERVER_ERROR;

  return data->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->DeleteRecord(timer.iClientIndex);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  auto data = std::atomic_load(&m_data);
  if (data)
    return data->DeleteRecord(recording.strRecordingId);

  return PVR_ERROR_SERVER_ERROR;
}

bool CanSeekStream(void)
{
  return false;
}

bool CanPauseStream(void)
{
  return false;
}

bool IsRealTimeStream()
{
  return true;
}

const char *GetBackendHostname(void)
{
  return "";
}

/** UNUSED API FUNCTIONS */
PVR_ERROR DialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DialogAddChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
void CloseRecordedStream(void) {}
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long LengthRecordedStream(void) { return -1; }
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
bool OpenLiveStream(const PVR_CHANNEL &channel) { return false; }
void CloseLiveStream(void) {}
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) { return -1; }
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
void FillBuffer(bool mode) {}
void PauseStream(bool bPaused) {}
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {}
void OnSystemSleep() { }
void OnSystemWake() { }
void OnPowerSavingActivated() { }
void OnPowerSavingDeactivated() { }
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamReadChunkSize(int* chunksize) { return PVR_ERROR_NOT_IMPLEMENTED; }

} // extern "C"
