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

#include <json/json.h>
#ifdef TARGET_WINDOWS
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "WSOCK32.lib")
#else
#include <unistd.h>
#endif

#include <fstream>
#include <iostream>

#include "client.h"
#include "apimanager.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <openssl/sha.h>

using namespace ADDON;

const std::string ApiManager::API_URL = "https://sledovanitv.cz/api/";
const std::string ApiManager::TIMESHIFTINFO_URL = "https://sledovanitv.cz/playback/timeshiftInfo";
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

ApiManager::ApiManager(const std::string & userName, const std::string & userPassword)
  : m_userName{userName}
  , m_userPassword{userPassword}
  , m_sessionId{std::make_shared<std::string>()}
{
  XBMC->Log(LOG_NOTICE, "Loading ApiManager");
}

std::string ApiManager::call(const std::string & urlPath, const ApiParamMap & paramsMap, bool putSessionVar) const
{
  if (putSessionVar)
  {
    auto session_id = std::atomic_load(&m_sessionId);
    // if we need to put the sessionVar, but not logged in... do nothing
    if (session_id->empty())
      return std::string();
  }
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

std::string ApiManager::apiCall(const std::string &function, const ApiParamMap & paramsMap, bool putSessionVar /*= true*/) const
{
  std::string url = API_URL;
  url += function;
  return call(url, paramsMap, putSessionVar);
}

bool ApiManager::isSuccess(const std::string &response, Json::Value & root)
{
  std::string jsonReaderError;
  Json::CharReaderBuilder jsonReaderBuilder;
  std::unique_ptr<Json::CharReader> const reader(jsonReaderBuilder.newCharReader());

  if (reader->parse(response.c_str(), response.c_str() + response.size(), &root, &jsonReaderError))
  {
    bool success = root.get("status", 0).asInt() == 1;
    if (!success)
      XBMC->Log(LOG_ERROR, "Error indicated in response. status: %d, error: %s", root.get("status", 0).asInt(), root.get("error", "").asString().c_str());
    return success;
  }

  XBMC->Log(LOG_ERROR, "Error parsing response. Response is: %*s, reader error: %s", std::min(response.size(), static_cast<size_t>(1024)), response.c_str(), jsonReaderError.c_str());
  return false;
}

bool ApiManager::isSuccess(const std::string &response)
{
  Json::Value root;
  return isSuccess(response, root);
}

bool ApiManager::pairDevice()
{
  bool new_pairing = false;
  std::string pairJson = readPairFile();

  Json::Value root;
  if (pairJson.empty() || !isSuccess(pairJson, root) || root.get("userName", "").asString() != m_userName)
  {
    new_pairing = true;
    ApiParamMap params;

    char hostName[256];
    gethostname(hostName, 256);

    std::string macAddr;
#ifndef TARGET_WINDOWS
    constexpr char const * const iface_possibilities[] = {
      "/sys/class/net/eth0/address"
        , "/sys/class/net/wlan0/address"
        , "/sys/class/net/eth1/address"
        , "/sys/class/net/wlan1/address"
    };
    for (const auto & file : iface_possibilities)
    {
      std::ifstream ifs(file);
      if (ifs.is_open())
      {
        std::getline(ifs, macAddr);
      }
      if (!macAddr.empty())
      {
        macAddr.erase(std::remove(macAddr.begin(), macAddr.end(), ':'), macAddr.end());
        break;
      }
    }
#else
    std::unique_ptr<IP_ADAPTER_ADDRESSES, void (*)(void *)> pAddresses{static_cast<IP_ADAPTER_ADDRESSES *>(malloc(15 * 1024)), &free};
    ULONG outBufLen = 0;

    if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, pAddresses.get(), &outBufLen) == ERROR_BUFFER_OVERFLOW)
    {
        pAddresses.reset(static_cast<IP_ADAPTER_ADDRESSES *>(malloc(outBufLen)));
    }

    if (GetAdaptersAddresses(AF_UNSPEC, 0,  NULL,  pAddresses.get(), &outBufLen) == NO_ERROR)
    {
      IP_ADAPTER_ADDRESSES * p_addr = pAddresses.get();
      while (p_addr)
      {
        if (p_addr->PhysicalAddressLength > 0)
        {
          std::ostringstream addr;
          for (int i = 0; i < p_addr->PhysicalAddressLength; ++i)
            addr << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(p_addr->PhysicalAddress[i]);
          macAddr = addr.str();
          break;
        }
        p_addr = p_addr->Next;
      }
    } else
    {
      XBMC->Log(LOG_NOTICE, "GetAdaptersAddresses failed...");
    }
#endif

    if (macAddr.empty())
    {
      XBMC->Log(LOG_NOTICE, "Unable to get MAC address, using a dummy for serial");
      macAddr = "11223344";
    }

    // compute SHA1 of string representation of MAC address
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<unsigned char const *>(macAddr.c_str()), macAddr.size(), hash);
    std::ostringstream serial;
    for (const auto c : hash)
      serial << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);

    params["username"] = m_userName;
    params["password"] = m_userPassword;
    params["type"] = "androidportable";
    params["product"] = hostName;
    params["serial"] = serial.str();
    params["unit"] = "default";
    //params["checkLimit"] = "1";

    pairJson = apiCall("create-pairing", params, false);
  }

  if (isSuccess(pairJson, root))
  {
    int devId = root.get("deviceId", 0).asInt();
    std::string passwd = root.get("password", "").asString();

    char buf[256];
    sprintf(buf, "%d", devId);
    m_deviceId = buf;
    m_password = passwd;

    XBMC->Log(LOG_DEBUG, "Device ID: %d, Password: %s", devId, passwd.c_str());

    const bool paired = !m_deviceId.empty() && !m_password.empty();

    if (paired && new_pairing)
    {
      // add the userName to written json
      root["userName"] = m_userName;
      std::ostringstream os;
      os << root;
      createPairFile(os.str());
    }
    return paired;
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
  param["unit"] = "default";

  Json::Value root;

  std::string new_session_id;
  if (isSuccess(apiCall("device-login", param, false), root))
  {
    new_session_id = root.get("PHPSESSID", "").asString();

    if (new_session_id.empty())
    {
      XBMC->Log(LOG_ERROR, "Cannot perform device login");
    }
    else
    {
      XBMC->Log(LOG_INFO, "Device logged in. Session ID: %s", new_session_id.c_str());
    }
  }

  const bool success = !new_session_id.empty();
  if (!success)
  {
    m_deviceId.clear();
    m_password.clear();
    createPairFile(std::string{}); // truncate any "old" pairing response
  }

  std::atomic_store(&m_sessionId, std::make_shared<const std::string>(std::move(new_session_id)));

  return success;
}

bool ApiManager::getPlaylist(StreamQuality_t quality, bool useH265, bool useAdaptive, Json::Value & root)
{
  ApiParamMap params;
  params["format"] = "m3u8";
  params["quality"] = std::to_string(quality);
  std::string caps = useAdaptive ? "adaptive" : "";
  if (useH265)
  {
    if (!caps.empty())
      caps += ',';
    caps += "h265";
  }
  params["capabilities"] = std::move(caps);
  return isSuccess(apiCall("playlist", params), root);
}

bool ApiManager::getStreamQualities(Json::Value & root)
{
    return isSuccess(apiCall("get-stream-qualities", ApiParamMap()), root);
}

bool ApiManager::getEpg(time_t start, bool smallDuration, const std::string & channels, Json::Value & root)
{
  ApiParamMap params;

  params["time"] = formatTime(start);
  params["duration"] = smallDuration ? "60" : "1439";
  params["detail"] = "1";
  if (!channels.empty())
    params["channels"] = std::move(channels);

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
    , int & duration) const
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

bool ApiManager::addTimer(const std::string &eventId, std::string & recordId)
{
  ApiParamMap param;
  param["eventId"] = eventId;

  Json::Value root;

  if (isSuccess(apiCall("record-event", param), root))
  {
    recordId = root.get("recordId", "").asString();
    return true;
  }
  return false;
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

bool ApiManager::loggedIn() const
{
  auto session_id = std::atomic_load(&m_sessionId);
  return !session_id->empty();
}

std::string ApiManager::urlEncode(const std::string &str)
{
  std::string strOut;
  strOut.append(url_encode(str.c_str()));

  return strOut;
}

std::string ApiManager::buildQueryString(const ApiParamMap & paramMap, bool putSessionVar) const
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

  std::shared_ptr<const std::string> session_id = std::atomic_load(&m_sessionId);

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

  void *fileHandle = XBMC->OpenFileForWrite(url.c_str(), true);
  if (fileHandle)
  {
    XBMC->WriteFile(fileHandle, content.c_str(), content.length());
    XBMC->CloseFile(fileHandle);
  }
}
