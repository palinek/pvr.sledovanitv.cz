#include "Addon.h"

#include "Data.h"
#include "kodi/General.h"
#include <memory>

namespace sledovanitvcz
{
  ADDON_STATUS Addon::Create()
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the PVR sledovanitv.cz (unofficial)", __FUNCTION__);
    return ADDON_STATUS_OK;
  }

  ADDON_STATUS Addon::SetSetting(const std::string & settingName, const kodi::addon::CSettingValue & settingValue)
  {
    // just force our data to be re-created
    return ADDON_STATUS_NEED_RESTART;
  }

  ADDON_STATUS Addon::CreateInstance(const kodi::addon::IInstanceInfo & instance, KODI_ADDON_INSTANCE_HDL & hdl)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s - Creating instance %d PVR sledovanitv.cz (unofficial)", __FUNCTION__, instance.GetNumber());

    if (!instance.IsType(ADDON_INSTANCE_PVR))
      return ADDON_STATUS_UNKNOWN;

    TryMigrate(instance);

    hdl = new Data{instance};
    return ADDON_STATUS_OK;
  }

  void Addon::DestroyInstance(const kodi::addon::IInstanceInfo & instance, const KODI_ADDON_INSTANCE_HDL hdl)
  {
    // From parent doc:
    /// @warning This call is only used to inform that the associated instance
    /// is terminated. The deletion is carried out in the background.

    // So we don't need to do anything here
    //KODI_ADDON_INSTANCE_STRUCT * inst_struct = instance;
    //delete inst_struct->pvr;
  }

  void Addon::TryMigrate(const kodi::addon::IInstanceInfo & instance)
  {
    auto test = std::make_unique<kodi::addon::IAddonInstance>(instance);
    // pre-multi-instance migration
    if (test->GetInstanceSettingString("kodi_addon_instance_name").empty())
    {
      kodi::QueueFormattedNotification(QUEUE_INFO, "Migrating pre-multi-instance settings to 'Migrated Add-on Config'...");
      test->SetInstanceSettingString("kodi_addon_instance_name", "Migrated Add-on Config");
      test->SetInstanceSettingEnum<ApiManager::ServiceProvider_t>("serviceProvider", kodi::addon::GetSettingEnum<ApiManager::ServiceProvider_t>("serviceProvider", ApiManager::SP_DEFAULT));
      test->SetInstanceSettingString("userName", kodi::addon::GetSettingString("userName"));
      test->SetInstanceSettingString("password", kodi::addon::GetSettingString("password"));
      test->SetInstanceSettingString("deviceId", kodi::addon::GetSettingString("deviceId"));
      test->SetInstanceSettingString("productId", kodi::addon::GetSettingString("productId"));
      test->SetInstanceSettingEnum<ApiManager::StreamQuality_t>("streamQuality", ApiManager::SQ_DEFAULT);
      test->SetInstanceSettingInt("fullChannelEpgRefresh", kodi::addon::GetSettingInt("fullChannelEpgRefresh", 24));
      test->SetInstanceSettingInt("loadingsRefresh", kodi::addon::GetSettingInt("loadingsRefresh", 60));
      test->SetInstanceSettingInt("keepAliveDelay", kodi::addon::GetSettingInt("keepAliveDelay", 20));
      test->SetInstanceSettingInt("epgCheckDelay", kodi::addon::GetSettingInt("epgCheckDelay", 1));
      test->SetInstanceSettingBoolean("useH265", kodi::addon::GetSettingBoolean("useH265", false));
      test->SetInstanceSettingBoolean("useAdaptive", kodi::addon::GetSettingBoolean("useAdaptive", false));
      test->SetInstanceSettingBoolean("showLockedChannels", kodi::addon::GetSettingBoolean("showLockedChannels", true));
      test->SetInstanceSettingBoolean("showLockedOnlyPin", kodi::addon::GetSettingBoolean("showLockedOnlyPin", true));
    }
  }
} //namespace sledovanitvcz

ADDONCREATOR(sledovanitvcz::Addon)
