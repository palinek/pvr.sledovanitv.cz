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

#include <json/json.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif
#include <fstream>
#include <iostream>

#include "client.h"
#include "apimanager.h"
#include <ctime>

using namespace ADDON;

const std::string ApiManager::API_URL = "http://sledovanitv.cz/api/";
const std::string ApiManager::TIMESHIFTINFO_URL = "http://sledovanitv.cz/playback/timeshiftInfo";
const std::string ApiManager::PAIR_FILE = "pairinfo";

/* Converts a hex character to its integer value */
char from_hex(char ch)
{
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code)
{
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

char *url_encode(const char *str)
{
  char *pstr = (char*) str, *buf = (char *)malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr)
{
  if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
    *pbuf++ = *pstr;
  else if (*pstr == ' ')
    *pbuf++ = '+';
  else
    *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
  pstr++;
  }
  *pbuf = '\0';
  return buf;
}

std::string ApiManager::formatTime(time_t t)
{
  std::string buf(17, ' ');
  std::strftime(const_cast<char *>(buf.data()), buf.size(), "%Y-%m-%d %H:%M", std::localtime(&t));
  return buf;
}

ApiManager::ApiManager()
  : m_sessionId{std::make_shared<std::string>()}
{
  XBMC->Log(LOG_NOTICE, "Loading ApiManager");
}

std::string ApiManager::call(const std::string & urlPath, const ApiParamMap & paramsMap, bool putSessionVar)
{
  std::string url = urlPath;
  url += '?';
  url += buildQueryString(paramsMap, putSessionVar);
  std::string response;

  void *fh = XBMC->OpenFile(url.c_str(), XFILE::READ_NO_CACHE);
  if (fh)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fh, buffer, 1024))
      response.append(buffer, bytesRead);
    XBMC->CloseFile(fh);
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Cannot open url");
  }

  return response;
}

std::string ApiManager::apiCall(const std::string &function, const ApiParamMap & paramsMap)
{
  std::string url = API_URL;
  url += function;
  return call(url, paramsMap, true);
}

bool ApiManager::isSuccess(const std::string &response, Json::Value & root)
{
  Json::Reader reader;

  if (reader.parse(response, root))
  {
    bool success = root.get("status", 0).asInt() == 1;
    if (!success)
      XBMC->Log(LOG_ERROR, "Error indicated in response. status: %s, error: %s", root.get("status", "0").asString().c_str(), root.get("error", "").asString().c_str());
    return success;
  }

  XBMC->Log(LOG_ERROR, "Error parsing response. Response is: %*s, reader error: %s", response.c_str(), 1024, reader.getFormatedErrorMessages().c_str());
  return false;
}

bool ApiManager::isSuccess(const std::string &response)
{
  Json::Value root;
  return isSuccess(response, root);
}

bool ApiManager::pairDevice()
{
  std::string pairJson = readPairFile();

  if (pairJson.empty())
  {
    ApiParamMap params;

#ifndef _WIN32
    char hostName[256];
    gethostname(hostName, 256);

    std::string macAddr;
    std::ifstream ifs("/sys/class/net/eth0/address");
    if (ifs.is_open())
    {
      std::getline(ifs, macAddr);
    }
#else
    char *hostName = "Kodi Win32";
    std::string macAddr = "11:22:33:44";
#endif

    params["username"] = g_strUserName;
    params["password"] = g_strPassword;
    params["type"] = "androidtv";
    params["product"] = hostName;
    params["serial"] = macAddr;

    pairJson = apiCall("create-pairing", params);
  }

  Json::Value root;

  if (isSuccess(pairJson, root))
  {
    int devId = root.get("deviceId", 0).asInt();
    std::string passwd = root.get("password", "").asString();

    char buf[256];
    sprintf(buf, "%d", devId);
    m_deviceId = buf;
    m_password = passwd;

    XBMC->Log(LOG_DEBUG, "Device ID: %d, Password: %s", devId, passwd.c_str());

    if (!m_deviceId.empty() && !m_password.empty())
    {
      createPairFile(pairJson);
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Error in pairing response.");
  }

  return false;
}

bool ApiManager::login()
{
  if (m_deviceId.empty() && m_password.empty())
  {
    if (!pairDevice())
    {
      XBMC->Log(LOG_ERROR, "Cannot pair device");
      return false;
    }
  }

  ApiParamMap param;
  param["deviceId"] = m_deviceId;
  param["password"] = m_password;

  Json::Value root;

  auto new_session_id = std::make_shared<std::string>();
  if (isSuccess(apiCall("device-login", param), root))
  {
    *new_session_id = root.get("PHPSESSID", "").asString();

    if (new_session_id->empty())
    {
      XBMC->Log(LOG_ERROR, "Cannot perform device login");
    }
    else
    {
      XBMC->Log(LOG_INFO, "Device logged in. Session ID: %s", new_session_id->c_str());
    }
  }

  const bool success = !new_session_id->empty();

  m_sessionId = new_session_id;

  return success;
}

bool ApiManager::getPlaylist(Json::Value & root)
{
  ApiParamMap params;
  params["format"] = "androidtv";
  return isSuccess(apiCall("playlist", params), root);
}

bool ApiManager::getStreamQualities(Json::Value & root)
{
    return isSuccess(apiCall("get-stream-qualities", ApiParamMap()), root);
}

bool ApiManager::getEpg(time_t start, bool smallDuration, Json::Value & root)
{
  ApiParamMap params;

  params["time"] = formatTime(start);
  params["duration"] = smallDuration ? "60" : "1439";
  params["detail"] = "1";

  return isSuccess(apiCall("epg", params), root);
}

bool ApiManager::getPvr(Json::Value & root)
{
  return isSuccess(apiCall("get-pvr", ApiParamMap()), root);
}

std::string ApiManager::getRecordingUrl(const std::string &recId)
{
  ApiParamMap param;
  param["recordId"] = recId;
  param["format"] = "m3u8";

  Json::Value root;

  if (isSuccess(apiCall("record-timeshift", param), root))
  {
    return root.get("url", "").asString();
  }

  return "";
}

bool ApiManager::getTimeShiftInfo(const std::string &eventId
    , std::string & streamUrl
    , int & duration)
{
  ApiParamMap param;
  param["eventId"] = eventId;
  param["format"] = "m3u8";

  Json::Value root;

  if (isSuccess(apiCall("event-timeshift", param), root))
  {
    streamUrl = root.get("url", "").asString();
    duration = root.get("duration", 0).asInt();
    return true;
  }

  return false;
}

bool ApiManager::addTimer(const std::string &eventId)
{
  ApiParamMap param;
  param["eventId"] = eventId;

  return isSuccess(apiCall("record-event", param));
}

bool ApiManager::deleteRecord(const std::string &recId)
{
  ApiParamMap param;
  param["recordId"] = recId;

  return isSuccess(apiCall("delete-record", param));
}

bool ApiManager::keepAlive()
{
    ApiParamMap param;
    return isSuccess(apiCall("keepalive", param));
}

std::string ApiManager::urlEncode(const std::string &str)
{
  std::string strOut;
  strOut.append(url_encode(str.c_str()));

  return strOut;
}

std::string ApiManager::buildQueryString(const ApiParamMap & paramMap, bool putSessionVar)
{
  XBMC->Log(LOG_DEBUG, "%s - size %d", __FUNCTION__, paramMap.size());
  std::string strOut;
  for (const auto & param : paramMap)
  {
    if (!strOut.empty())
    {
      strOut += "&";
    }

    strOut += param.first + "=" + urlEncode(param.second);
  }

  std::shared_ptr<const std::string> session_id = m_sessionId;

  if (putSessionVar)
    strOut += "&PHPSESSID=";
  strOut += *session_id;

  return strOut;
}

std::string ApiManager::readPairFile()
{
  std::string url = GetUserFilePath(PAIR_FILE);
  std::string strContent;

  XBMC->Log(LOG_DEBUG, "Openning file %s", url.c_str());

  void* fileHandle = XBMC->OpenFile(url.c_str(), 0);
  if (fileHandle)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
    strContent.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);
  }

  return strContent;
}

void ApiManager::createPairFile(const std::string &content)
{
  std::string url = GetUserFilePath(PAIR_FILE);

  void *fileHandle = XBMC->OpenFileForWrite(url.c_str(), false);
  if (fileHandle)
  {
    XBMC->WriteFile(fileHandle, content.c_str(), content.length());
  }
}
