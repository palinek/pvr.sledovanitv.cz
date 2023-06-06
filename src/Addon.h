/*
 *      Copyright (c) 2023~now Palo Kisa <palo.kisa@gmail.com>
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

#pragma once

#include "kodi/AddonBase.h"

namespace sledovanitvcz
{
  class ATTR_DLL_LOCAL Addon : public kodi::addon::CAddonBase
  {
  public:
    Addon() = default;

    virtual ADDON_STATUS Create() override;
    virtual ADDON_STATUS SetSetting(const std::string & settingName, const kodi::addon::CSettingValue & settingValue) override;
    virtual ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo & instance, KODI_ADDON_INSTANCE_HDL & hdl) override;
    virtual void DestroyInstance(const kodi::addon::IInstanceInfo & instance, const KODI_ADDON_INSTANCE_HDL hdl) override;

  private:
    static void TryMigrate(const kodi::addon::IInstanceInfo & instance);
  };

} //namespace sledovanitvcz
