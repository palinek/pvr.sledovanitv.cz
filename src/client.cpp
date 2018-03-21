/*
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "client.h"

#include "p8-platform/util/util.h"
#include "PVRIptvData.h"
#include "kodi/xbmc_pvr_dll.h"
#include "kodi/libKODI_guilib.h"

#include <iostream>
#include <memory>

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
PVRIptvData   *m_data           = NULL;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_strUserPath   = "";
std::string g_strClientPath = "";

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr   *PVR  = NULL;

//std::string g_strTvgPath    = "";
//std::string g_strM3UPath    = "";
//std::string g_strLogoPath   = "";
std::string g_strUserName	= "";
std::string g_strPassword	= "";
bool g_bHdEnabled = true;
//int         g_iEPGTimeShift = 0;
//int         g_iStartNumber  = 1;
//bool        g_bTSOverride   = true;
//bool        g_bCacheM3U     = false;
//bool        g_bCacheEPG     = false;

extern std::string PathCombine(const std::string &strPath, const std::string &strFileName)
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

extern std::string GetClientFilePath(const std::string &strFileName)
{
  return PathCombine(g_strClientPath, strFileName);
}

extern std::string GetUserFilePath(const std::string &strFileName)
{
  return PathCombine(g_strUserPath, strFileName);
}

extern "C" {

void ADDON_ReadSettings(void)
{
  char buffer[1024];
  //int iPathType = 0;

  if (XBMC->GetSetting("userName", &buffer))
  {
    g_strUserName = buffer;
  }

  if (XBMC->GetSetting("password", buffer))
  {
    g_strPassword = buffer;
  }

  if (!XBMC->GetSetting("enableHd", &g_bHdEnabled))
  {
    g_bHdEnabled = true;
  }
}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
  {
    return ADDON_STATUS_UNKNOWN;
  }

  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
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

  ADDON_ReadSettings();

  m_data = new PVRIptvData;
  m_CurStatus = ADDON_STATUS_OK;

  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

void ADDON_Destroy()
{
  delete m_data;
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

bool ADDON_HasSettings()
{
  return true;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // reset cache and restart addon

  std::string strFile = GetUserFilePath(M3U_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  strFile = GetUserFilePath(TVG_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  return ADDON_STATUS_NEED_RESTART;
}

void ADDON_Stop()
{
  if (m_data != NULL)
  {
    delete m_data;
    m_data = NULL;
  }
}

void ADDON_FreeSettings()
{
}

void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

const char* GetPVRAPIVersion(void)
{
  static const char *strApiVersion = XBMC_PVR_API_VERSION;
  return strApiVersion;
}

const char* GetMininumPVRAPIVersion(void)
{
  static const char *strMinApiVersion = XBMC_PVR_MIN_API_VERSION;
  return strMinApiVersion;
}

const char* GetGUIAPIVersion(void)
{
  static const char *strGuiApiVersion = KODI_GUILIB_API_VERSION;
  return strGuiApiVersion;
}

const char* GetMininumGUIAPIVersion(void)
{
  static const char *strMinGuiApiVersion = KODI_GUILIB_MIN_API_VERSION;
  return strMinGuiApiVersion;
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsTimers          = true;
  pCapabilities->bHandlesInputStream      = false;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "Sledovanitv.cz PVR Add-on";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = PVR_CLIENT_VERSION;
  return strBackendVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  //TODO
  *iTotal = 0;
  *iUsed  = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (m_data)
    return m_data->GetEPGForChannel(handle, channel, iStart, iEnd);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelsAmount(void)
{
  if (m_data)
    return m_data->GetChannelsAmount();

  return -1;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannels(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelGroupsAmount(void)
{
  if (m_data)
    return m_data->GetChannelGroupsAmount();

  return -1;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannelGroups(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (m_data)
    return m_data->GetChannelGroupMembers(handle, group);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "Sledovanitv.cz");
  snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");

  return PVR_ERROR_NO_ERROR;
}

bool CanPauseStream(void)
{
  return true;
}

int GetRecordingsAmount(bool deleted)
{
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  if (deleted)
    return 0;

  if (m_data)
    return m_data->GetRecordingsAmount();

  return 0;
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  if (deleted)
    return PVR_ERROR_NO_ERROR;

  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);
  if (m_data)
    return m_data->GetRecordings(handle);

  return PVR_ERROR_SERVER_ERROR;
}

/** SEEK */
bool CanSeekStream(void)
{
  return true;
}

/** TIMER FUNCTIONS */
int GetTimersAmount(void)
{
  if (m_data)
    return m_data->GetTimersAmount();

  return -1;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  if (m_data)
    return m_data->GetTimers(handle);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  XBMC->Log(LOG_DEBUG, "%s - size: %d", __FUNCTION__, *size);
  int pos = 0;
  types[pos].iId = pos + 1;
  types[pos].iAttributes = PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME;
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

  ++pos;
  types[pos].iId = pos + 1;
  types[pos].iAttributes = PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME;
  strncpy(types[pos].strDescription, "Virtual for timeshift", sizeof (types[pos].strDescription));
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
  strncpy(types[pos].strDescription, "Virtual for timeshift", sizeof (types[pos].strDescription));
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
  if (!m_data)
    return PVR_ERROR_SERVER_ERROR;

  if (timer.iTimerType < 3)
    return m_data->AddTimer(timer, false/*normal*/);
  else
    return m_data->AddTimer(timer, true/*virtual*/);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  if (m_data)
    return m_data->DeleteRecord(timer.iClientIndex);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  if (m_data)
    return m_data->DeleteRecord(recording.strRecordingId);

  return PVR_ERROR_SERVER_ERROR;
}

bool IsTimeshifting(void)
{
  return true;
}

const char *GetBackendHostname(void)
{
  return "";
}

/** UNUSED API FUNCTIONS */
const char * GetLiveStreamURL(const PVR_CHANNEL &channel)  { return ""; }
PVR_ERROR DialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
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
long long PositionRecordedStream(void) { return -1; }
long long LengthRecordedStream(void) { return -1; }
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
bool OpenLiveStream(const PVR_CHANNEL &channel) { return false; }
void CloseLiveStream(void) {}
bool SwitchChannel(const PVR_CHANNEL &channel) { return false; }
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties) { return PVR_ERROR_NOT_IMPLEMENTED; }
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) { return -1; }
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long PositionLiveStream(void) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
unsigned int GetChannelSwitchDelay(void) { return 0; }
void PauseStream(bool bPaused) {}
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
time_t GetPlayingTime() { return 0; }
time_t GetBufferTimeStart() { return 0; }
time_t GetBufferTimeEnd() { return 0; }
void OnSystemSleep() { }
void OnSystemWake() { }
void OnPowerSavingActivated() { }
void OnPowerSavingDeactivated() { }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool IsRealTimeStream() { return true; }

}
