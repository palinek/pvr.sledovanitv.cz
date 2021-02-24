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

#ifndef sledovanitcz_ApiManager_h
#define sledovanitcz_ApiManager_h

#include <string>
#include <vector>
#include <memory>

namespace Json
{
  class Value;
}

namespace sledovanitvcz
{

typedef std::vector<std::tuple<std::string, std::string> > ApiParams_t;

class ApiManager
{
public:
  enum StreamQuality_t
  {
    SQ_DEFAULT = 0
      , SQ_SD = 20
      , SQ_HD = 40
  };
  enum ServiceProvider_t
  {
    SP_DEFAULT = 0
      , SP_SLEDOVANITV_CZ = SP_DEFAULT
      , SP_MODERNITV_CZ = 1
      , SP_END
  };
public:
  static std::string formatTime(time_t t);

public:
  ApiManager(ServiceProvider_t serviceProvider
      , const std::string & userName
      , const std::string & userPassword
      , const std::string & overridenMac //!< device identifier (value for overriding the MAC address detection)
      , const std::string & product //!< product identifier (value for overriding the hostname detection)
      );

  bool login();
  bool pinUnlock(const std::string & pin);
  bool getPlaylist(StreamQuality_t quality, bool useH265, bool useAdaptive, Json::Value & root);
  bool getStreamQualities(Json::Value & root);
  bool getEpg(time_t start, bool smallDuration, const std::string & channels, Json::Value & root);
  bool getPvr(Json::Value & root);
  std::string getRecordingUrl(const std::string &recId, std::string & channel);
  bool getTimeShiftInfo(const std::string &eventId
      , std::string & streamUrl
      , std::string & channel
      , int & duration) const;
  bool addTimer(const std::string & eventId, std::string & recordId);
  bool deleteRecord(const std::string &recId);
  bool keepAlive();
  bool loggedIn() const;
  bool pinUnlocked() const;

private:
  static std::string urlEncode(const std::string &str);
  static std::string readPairFile();
  static void createPairFile(Json::Value & contentRoot);
  static bool isSuccess(const std::string &response, Json::Value & root);
  static bool isSuccess(const std::string &response);

  std::string buildQueryString(const ApiParams_t & paramMap, bool putSessionVar) const;
  std::string call(const std::string & urlPath, const ApiParams_t & paramsMap, bool putSessionVar) const;
  std::string apiCall(const std::string &function, const ApiParams_t & paramsMap, bool putSessionVar = true) const;
  bool pairDevice(Json::Value & root);
  bool deletePairing(const Json::Value & root);

  static const std::string API_URL[SP_END];
  static const std::string API_UNIT[SP_END];
  static const std::string PAIR_FILE;
  const ServiceProvider_t m_serviceProvider;
  const std::string m_userName;
  const std::string m_userPassword;
  const std::string m_overridenMac;
  const std::string m_product;
  std::string m_serial;
  std::string m_deviceId;
  std::string m_password;
  bool m_pinUnlocked;
  std::shared_ptr<const std::string> m_sessionId;
};

} // namespace sledovanitvcz
#endif // sledovanitcz_ApiManager_h
