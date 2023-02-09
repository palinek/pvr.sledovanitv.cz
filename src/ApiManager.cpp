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
#if defined(TARGET_POSIX)
#include <unistd.h>
#endif
#if defined(TARGET_LINUX) || defined(TARGET_FREEBSD) || defined(TARGET_DARWIN)
#include <net/if.h>
#include <ifaddrs.h>
# if defined(TARGET_LINUX)
#include <linux/if_packet.h>
# else //defined(TARGET_FREEBSD) || defined(TARGET_DARWIN)
#include <net/if_types.h>
#include <net/if_dl.h>
# endif
# if defined(TARGET_ANDROID)
#include <android/api-level.h>
# endif
#elif defined(TARGET_WINDOWS)
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "WSOCK32.lib")
#endif

#include <fstream>
#include <iostream>

#include "ApiManager.h"
#include "picosha2.h"
#include "kodi/General.h"
#include "kodi/Filesystem.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <chrono>

namespace sledovanitvcz
{

const std::string ApiManager::API_URL[SP_END] = { "https://sledovanitv.cz/api/", "https://api.moderntv.eu/api/" };
const std::string ApiManager::API_UNIT[SP_END] = { "default", "modernitv" };
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

#if defined(TARGET_ANDROID) && __ANDROID_API__ < 24
// Note: declare dummy "ifaddr" functions to make this compile on pre API-24 versions
#warning "Compiling for ANDROID with API version < 24, getting MAC addres will not be available."
static int getifaddrs(struct ifaddrs ** /*ifap*/) { errno = ENOTSUP; return -1; }
static void freeifaddrs(struct ifaddrs * /*ifa*/) {}
#endif
static std::string get_mac_address()
{
  std::string mac_addr;
#if defined(TARGET_ANDROID) && __ANDROID_API__ < 24
  kodi::Log(ADDON_LOG_INFO, "Can't get MAC address with target Android API < 24 (no getifaddrs() support)");
#endif
#if defined(TARGET_LINUX) || defined(TARGET_FREEBSD) || defined(TARGET_DARWIN)
    struct ifaddrs * addrs;
    if (0 != getifaddrs(&addrs))
    {
      kodi::Log(ADDON_LOG_INFO, "While getting MAC address getifaddrs() failed, %s", strerror(errno));
      return mac_addr;
    }
    std::unique_ptr<struct ifaddrs, decltype (&freeifaddrs)> if_addrs{addrs, &freeifaddrs};
    for (struct ifaddrs * p = if_addrs.get(); p; p = p->ifa_next)
    {
#if defined(TARGET_LINUX)
      if (nullptr != p->ifa_addr && p->ifa_addr->sa_family == AF_PACKET && 0 == (p->ifa_flags & IFF_LOOPBACK))
      {
        struct sockaddr_ll * address = reinterpret_cast<struct sockaddr_ll *>(p->ifa_addr);
        std::ostringstream addr;
        for (int i = 0; i < address->sll_halen; ++i)
          addr << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(address->sll_addr[i]);
        mac_addr = addr.str();
        break;
      }
#else
      if (nullptr != p->ifa_addr && p->ifa_addr->sa_family == AF_LINK)
      {
        struct sockaddr_dl * address = reinterpret_cast<struct sockaddr_dl *>(p->ifa_addr);
        if (address->sdl_type == IFT_LOOP)
          continue;
        std::ostringstream addr;
        for (int i = 0; i < address->sdl_alen; ++i)
          addr << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(*(address->sdl_data + address->sdl_nlen + i));
        mac_addr = addr.str();
        break;
      }
#endif
    }
#elif defined(TARGET_WINDOWS)
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
          mac_addr = addr.str();
          break;
        }
        p_addr = p_addr->Next;
      }
    } else
    {
      kodi::Log(ADDON_LOG_INFO, "GetAdaptersAddresses failed...");
    }
#endif
    return mac_addr;
}

std::string ApiManager::formatTime(time_t t)
{
  std::string buf(17, ' ');
  std::strftime(const_cast<char *>(buf.data()), buf.size(), "%Y-%m-%d %H:%M", std::localtime(&t));
  return buf;
}

ApiManager::ApiManager(ServiceProvider_t serviceProvider
    , const std::string & userName
    , const std::string & userPassword
    , const std::string & overridenMac
    , const std::string & product)
  : m_serviceProvider{serviceProvider}
  , m_userName{userName}
  , m_userPassword{userPassword}
  , m_overridenMac{overridenMac}
  , m_product{product}
  , m_pinUnlocked{false}
  , m_sessionId{std::make_shared<std::string>()}
{
  kodi::Log(ADDON_LOG_INFO, "Loading ApiManager");
}

std::string ApiManager::call(const std::string & urlPath, const ApiParams_t & paramsMap, bool putSessionVar) const
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
  // add User-Agent header... TODO: make it configurable
  url += "|User-Agent=okhttp%2F3.12.0";
  std::string response;

  kodi::vfs::CFile fh;
  if (fh.OpenFile(url, ADDON_READ_NO_CACHE))
  {
    char buffer[1024];
    while (int bytesRead = fh.Read(buffer, 1024))
      response.append(buffer, bytesRead);
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Cannot open url");
  }

  return response;
}

std::string ApiManager::apiCall(const std::string &function, const ApiParams_t & paramsMap, bool putSessionVar /*= true*/) const
{
  std::string url = API_URL[m_serviceProvider];
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
      kodi::Log(ADDON_LOG_ERROR, "Error indicated in response. status: %d, error: %s", root.get("status", 0).asInt(), root.get("error", "").asString().c_str());
    return success;
  }

  kodi::Log(ADDON_LOG_ERROR, "Error parsing response. Response is: %*s, reader error: %s", std::min(response.size(), static_cast<size_t>(1024)), response.c_str(), jsonReaderError.c_str());
  return false;
}

bool ApiManager::isSuccess(const std::string &response)
{
  Json::Value root;
  return isSuccess(response, root);
}

bool ApiManager::deletePairing(const Json::Value & root)
{
  // try to delete pairing
  const std::string old_dev_id = root.get("deviceId", "").asString();
  if (old_dev_id.empty())
    return true; // no previous pairing

  const std::string old_password = root.get("password", "").asString();
  ApiParams_t params;
  params.emplace_back("deviceId", old_dev_id);
  params.emplace_back("password", old_password);
  params.emplace_back("unit", API_UNIT[m_serviceProvider]);
  const std::string response = apiCall("delete-pairing", params, false);
  Json::Value del_root;
  if (isSuccess(response, del_root)
      || (del_root.get("error", "").asString() == "no device")
      || (del_root.get("error", "").asString() == "not logged")
      )
  {
    kodi::Log(ADDON_LOG_INFO, "Previous pairing(deviceId:%s) deleted (or no such device)", old_dev_id.c_str());
    return true;
  }

  return false;
}

bool ApiManager::pairDevice(Json::Value & root)
{
  bool new_pairing = false;
  std::string pairJson = readPairFile();

  std::string macAddr = m_overridenMac.empty() ? get_mac_address() : m_overridenMac;
  if (macAddr.empty())
  {
    std::ostringstream os;
    os << std::chrono::high_resolution_clock::now().time_since_epoch().count();
    macAddr = os.str();
    kodi::Log(ADDON_LOG_INFO, "Unable to get MAC address, using a dummy(%s) for serial", macAddr.c_str());
  }
  // compute SHA256 of string representation of MAC address
  m_serial = picosha2::hash256_hex_string(macAddr);


  if (pairJson.empty() || !isSuccess(pairJson, root)
      || root.get("userName", "").asString() != m_userName
      || root.get("serial", "").asString() != m_serial
      )
  {
    // remove pairing if any exising
    if (!deletePairing(root))
      return false;

    new_pairing = true;
    ApiParams_t params;

    std::string product = m_product;
    if (product.empty())
    {
      char host_name[256];
      gethostname(host_name, 256);
      product = host_name;
    }

    params.emplace_back("username", m_userName);
    params.emplace_back("password", m_userPassword);
    params.emplace_back("type", "androidportable");
    params.emplace_back("serial", m_serial);
    params.emplace_back("product", product);
    params.emplace_back("unit", API_UNIT[m_serviceProvider]);
    params.emplace_back("checkLimit", "1");

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

    kodi::Log(ADDON_LOG_DEBUG, "Device ID: %d, Password: %s", devId, passwd.c_str());

    const bool paired = !m_deviceId.empty() && !m_password.empty();

    if (paired && new_pairing)
    {
      // add the userName to written json
      root["userName"] = m_userName;
      root["serial"] = m_serial;
      createPairFile(root);
    }
    return paired;
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Error in pairing response.");
  }

  return false;
}

bool ApiManager::login()
{
  m_pinUnlocked = false;
  Json::Value pairing_root;
  if (m_deviceId.empty() && m_password.empty())
  {
    if (!pairDevice(pairing_root))
    {
      kodi::Log(ADDON_LOG_ERROR, "Cannot pair device");
      return false;
    }
  }

  ApiParams_t param;
  param.emplace_back("deviceId", m_deviceId);
  param.emplace_back("password", m_password);
  param.emplace_back("version", "2.6.21");
  param.emplace_back("lang", "en");
  param.emplace_back("unit", API_UNIT[m_serviceProvider]);

  Json::Value root;

  std::string new_session_id;
  const std::string response = apiCall("device-login", param, false);
  if (isSuccess(response, root))
  {
    new_session_id = root.get("PHPSESSID", "").asString();

    if (new_session_id.empty())
    {
      kodi::Log(ADDON_LOG_ERROR, "Cannot perform device login");
    }
    else
    {
      kodi::Log(ADDON_LOG_INFO, "Device logged in. Session ID: %s", new_session_id.c_str());
    }
  } else if (response.empty()) {
    kodi::Log(ADDON_LOG_INFO, "No login response. Is something wrong with network or remote servers?");
    // don't do anything, let the state as is to give it another try
    return false;
  }

  const bool success = !new_session_id.empty();
  if (!success)
  {
    m_deviceId.clear();
    m_password.clear();
    // change userName in stored pairing response to "re-pair" this device
    pairing_root["userName"] = "";
    createPairFile(pairing_root);
  }

  std::atomic_store(&m_sessionId, std::make_shared<const std::string>(std::move(new_session_id)));

  return success;
}

bool ApiManager::pinUnlock(const std::string & pin)
{
  ApiParams_t params;
  params.emplace_back("pin", pin);

  bool result = isSuccess(apiCall("pin-unlock", params));
  if (result)
    m_pinUnlocked = true;
  return result;
}

bool ApiManager::pinUnlocked() const
{
  return m_pinUnlocked;
}

bool ApiManager::getPlaylist(StreamQuality_t quality, bool useH265, bool useAdaptive, Json::Value & root)
{
  ApiParams_t params;
  params.emplace_back("uuid", m_serial);
  params.emplace_back("format", "m3u8");
  params.emplace_back("quality", std::to_string(quality));
  std::string caps = useH265 ? "h265" : "";
  if (useAdaptive)
  {
    if (!caps.empty())
      caps += ',';
    caps += "adaptive2";
  }
  params.emplace_back("capabilities", std::move(caps));
  return isSuccess(apiCall("playlist", params), root);
}

bool ApiManager::getStreamQualities(Json::Value & root)
{
    return isSuccess(apiCall("get-stream-qualities", ApiParams_t{}), root);
}

bool ApiManager::getEpg(time_t start, bool smallDuration, const std::string & channels, Json::Value & root)
{
  ApiParams_t params;

  params.emplace_back("time", formatTime(start));
  params.emplace_back("duration", smallDuration ? "60" : "1439");
  params.emplace_back("detail", "description,poster");
  params.emplace_back("allowOrder", "1");
  if (!channels.empty())
    params.emplace_back("channels", std::move(channels));

  return isSuccess(apiCall("epg", params), root);
}

bool ApiManager::getPvr(Json::Value & root)
{
  return isSuccess(apiCall("get-pvr", ApiParams_t()), root);
}

std::string ApiManager::getRecordingUrl(const std::string &recId, std::string & channel)
{
  ApiParams_t param;
  param.emplace_back("recordId", recId);
  param.emplace_back("format", "m3u8");

  Json::Value root;

  if (isSuccess(apiCall("record-timeshift", param), root))
  {
    channel = root.get("channel", "").asString();
    return root.get("url", "").asString();
  }

  return "";
}

bool ApiManager::getTimeShiftInfo(const std::string &eventId
    , std::string & streamUrl
    , std::string & channel
    , int & duration) const
{
  ApiParams_t param;
  param.emplace_back("eventId", eventId);
  param.emplace_back("format", "m3u8");

  Json::Value root;

  if (isSuccess(apiCall("event-timeshift", param), root))
  {
    streamUrl = root.get("url", "").asString();
    channel = root.get("channel", "").asString();
    duration = root.get("duration", 0).asInt();
    return true;
  }

  return false;
}

bool ApiManager::addTimer(const std::string &eventId, std::string & recordId)
{
  ApiParams_t param;
  param.emplace_back("eventId", eventId);

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
  ApiParams_t param;
  param.emplace_back("recordId", recId);

  return isSuccess(apiCall("delete-record", param));
}

bool ApiManager::keepAlive()
{
    ApiParams_t param;
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

std::string ApiManager::buildQueryString(const ApiParams_t & paramMap, bool putSessionVar) const
{
  kodi::Log(ADDON_LOG_DEBUG, "%s - size %d", __FUNCTION__, paramMap.size());
  std::string strOut;
  for (const auto & param : paramMap)
  {
    if (!strOut.empty())
    {
      strOut += "&";
    }

    strOut += std::get<0>(param) + "=" + urlEncode(std::get<1>(param));
  }

  if (putSessionVar)
  {
    auto session_id = std::atomic_load(&m_sessionId);
    strOut += "&PHPSESSID=";
    strOut += *session_id;
  }

  return strOut;
}

std::string ApiManager::readPairFile()
{
  std::string url = kodi::addon::GetUserPath(PAIR_FILE);
  std::string strContent;

  kodi::Log(ADDON_LOG_DEBUG, "Openning file %s", url.c_str());

  kodi::vfs::CFile fileHandle;
  if (fileHandle.OpenFile(url, 0))
  {
    char buffer[1024];
    while (int bytesRead = fileHandle.Read(buffer, 1024))
    strContent.append(buffer, bytesRead);
  }

  return strContent;
}

void ApiManager::createPairFile(Json::Value & contentRoot)
{
  std::string url = kodi::addon::GetUserPath(PAIR_FILE);

  kodi::vfs::CFile fileHandle;
  if (fileHandle.OpenFileForWrite(url, true))
  {
    std::ostringstream os;
    os << contentRoot;
    const std::string & content = os.str();
    fileHandle.Write(content.c_str(), content.length());
  }
}

} // namespace sledovanitvcz
