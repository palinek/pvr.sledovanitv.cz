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
  static std::string formatTime(time_t t);

public:
  ApiManager();

  bool pairDevice();
  bool login();
  bool getPlaylist(Json::Value & root);
  bool getStreamQualities(Json::Value & root);
  bool getEpg(time_t start, bool smallDuration, Json::Value & root);
  bool getPvr(Json::Value & root);
  std::string getRecordingUrl(const std::string &recId);
  bool getTimeShiftInfo(const std::string &eventId
      , std::string & streamUrl
      , int & duration);
  bool addTimer(const std::string &eventId);
  bool deleteRecord(const std::string &recId);
  bool keepAlive();

private:
  std::string urlEncode(const std::string &str);
  std::string buildQueryString(const ApiParamMap & paramMap, bool putSessionVar);
  std::string readPairFile();
  void createPairFile(const std::string &content);
  std::string call(const std::string & urlPath, const ApiParamMap & paramsMap, bool putSessionVar);
  std::string apiCall(const std::string &function, const ApiParamMap & paramsMap);
  bool isSuccess(const std::string &response, Json::Value & root);
  bool isSuccess(const std::string &response);

  static const std::string API_URL;
  static const std::string TIMESHIFTINFO_URL;
  static const std::string PAIR_FILE;
  std::string m_deviceId;
  std::string m_password;
  std::shared_ptr<const std::string> m_sessionId;
};

#endif // APIMANAGER_H
