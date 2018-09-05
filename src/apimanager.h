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

#ifndef APIMANAGER_H
#define APIMANAGER_H

#include <string>
#include <map>
#include <memory>

typedef std::map<std::string, std::string> ApiParamMap;

namespace Json
{
  class Value;
}

class ApiManager
{
public:
  enum StreamQuality_t
  {
    SQ_DEFAULT = 0
      , SQ_SD = 20
      , SQ_HD = 40
  };
public:
  static std::string formatTime(time_t t);

public:
  ApiManager(const std::string & userName, const std::string & userPassword);

  bool pairDevice();
  bool login();
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

private:
  static std::string urlEncode(const std::string &str);
  static std::string readPairFile();
  static void createPairFile(const std::string &content);
  static bool isSuccess(const std::string &response, Json::Value & root);
  static bool isSuccess(const std::string &response);

  std::string buildQueryString(const ApiParamMap & paramMap, bool putSessionVar) const;
  std::string call(const std::string & urlPath, const ApiParamMap & paramsMap, bool putSessionVar) const;
  std::string apiCall(const std::string &function, const ApiParamMap & paramsMap, bool putSessionVar = true) const;

  static const std::string API_URL;
  static const std::string TIMESHIFTINFO_URL;
  static const std::string PAIR_FILE;
  const std::string m_userName;
  const std::string m_userPassword;
  std::string m_deviceId;
  std::string m_password;
  std::shared_ptr<const std::string> m_sessionId;
};

#endif // APIMANAGER_H
