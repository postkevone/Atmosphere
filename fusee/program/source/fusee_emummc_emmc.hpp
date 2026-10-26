#pragma once

#include <vapours.hpp>
#include <exosphere/secmon/secmon_emummc_context.hpp>
#include "fs/fusee_fs_storage.hpp"

namespace ams::nxboot {

    bool IsEmmcEmummcEnabledValue(const char *value);

    void ConfigureEmmcEmummc(secmon::EmummcConfiguration &cfg, u32 sector);

    void InitializeEmmcEmummcStorages(fs::IStorage &mmc_user_storage, const secmon::EmummcConfiguration &cfg, fs::IStorage **out_boot0, fs::IStorage **out_user);

}
