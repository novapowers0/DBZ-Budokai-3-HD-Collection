// dbz3 - Region selection support (project-side, no SDK changes).

#include "region.h"

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <filesystem>
#include <memory>
#include <string>

// dbz3_region is a project cvar (src/launcher/settings.cpp), shared storage.
REXCVAR_DECLARE(std::string, dbz3_region);

namespace dbz3 {

bool ApplyRegionMount() {
  rex::Runtime* rt = rex::Runtime::instance();
  if (!rt || !rt->file_system()) {
    REXLOG_WARN("ApplyRegionMount: runtime/filesystem not ready");
    return false;
  }
  auto* fs = rt->file_system();

  const std::string region = REXCVAR_GET(dbz3_region);
  REXLOG_INFO("ApplyRegionMount: dbz3_region='{}'", region);

  // Always drop any previously applied region device so the launcher's current
  // selection (possibly changed after the initial SetupVfs) wins.
  constexpr const char* kRegionMount = "\\Device\\Harddisk0\\Partition1\\us";
  fs->UnregisterDevice(kRegionMount);

  if (region.empty() || region == "us") {
    REXLOG_INFO("ApplyRegionMount: no override, using default us");
    return true;
  }

  // The PAL region folder is named "eu" (mirrors the ISO layout). game_data_root
  // already points at the folder that directly contains us/ and eu/, so the
  // region folder is game_data_root/<region>.
  auto abs_game_root = std::filesystem::absolute(rt->game_data_root());
  auto region_path = abs_game_root / region;
  if (!std::filesystem::is_directory(region_path)) {
    REXLOG_WARN("ApplyRegionMount: region folder '{}' does not exist at {}",
                region, region_path.string());
    return false;
  }

  auto region_device = std::make_unique<rex::filesystem::HostPathDevice>(
      kRegionMount, region_path, /*read_only=*/true);
  if (!region_device->Initialize()) {
    REXLOG_WARN("ApplyRegionMount: failed to initialize device for {}", region_path.string());
    return false;
  }
  if (!fs->RegisterDevice(std::move(region_device))) {
    REXLOG_WARN("ApplyRegionMount: failed to register device for {}", region_path.string());
    return false;
  }
  REXLOG_INFO("ApplyRegionMount: MOUNTED {} at {}", region_path.string(), kRegionMount);
  return true;
}

}  // namespace dbz3
