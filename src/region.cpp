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

namespace {

// Effective game data root, kept in sync with the folder the launcher points
// at. Stored here (not on the Runtime, whose copy is fixed at Setup) so region
// mounting and the launcher always agree on where the assets live, including
// after a runtime relocation (RelocateGameData).
std::filesystem::path g_effective_game_root;

}  // namespace

std::filesystem::path EffectiveGameRoot() {
  if (!g_effective_game_root.empty()) return g_effective_game_root;
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->game_data_root() : std::filesystem::path{};
}

void SetEffectiveGameRoot(const std::filesystem::path& root) {
  g_effective_game_root = std::filesystem::absolute(root);
  REXLOG_INFO("EffectiveGameRoot set to {}", g_effective_game_root.string());
}

bool RemountGameDrive(const std::filesystem::path& root) {
  rex::Runtime* rt = rex::Runtime::instance();
  if (!rt || !rt->file_system()) {
    REXLOG_WARN("RemountGameDrive: runtime/filesystem not ready");
    return false;
  }
  auto* fs = rt->file_system();

  auto abs_root = std::filesystem::absolute(root);
  if (!std::filesystem::is_directory(abs_root)) {
    REXLOG_WARN("RemountGameDrive: folder does not exist: {}", abs_root.string());
    return false;
  }

  // Mirror Runtime::SetupVfs: mount the folder as the hard disk root and point
  // game:/d: at it. Re-registering is safe before the guest module launches.
  constexpr const char* kDrive = "\\Device\\Harddisk0\\Partition1";
  fs->UnregisterDevice(kDrive);

  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      kDrive, abs_root, !REXCVAR_GET(allow_game_relative_writes));
  if (!device->Initialize()) {
    REXLOG_ERROR("RemountGameDrive: failed to initialize device for {}", abs_root.string());
    return false;
  }
  if (!fs->RegisterDevice(std::move(device))) {
    REXLOG_ERROR("RemountGameDrive: failed to register device for {}", abs_root.string());
    return false;
  }
  fs->RegisterSymbolicLink("game:", kDrive);
  fs->RegisterSymbolicLink("d:", kDrive);
  g_effective_game_root = abs_root;
  REXLOG_INFO("RemountGameDrive: MOUNTED {} at {}", abs_root.string(), kDrive);
  return true;
}

bool RelocateGameData(const std::filesystem::path& root) {
  if (!RemountGameDrive(root)) return false;
  return ApplyRegionMount();
}

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

  // The PAL region folder is named "eu" (mirrors the ISO layout). The effective
  // game data root already points at the folder that directly contains us/ and
  // eu/, so the region folder is <root>/<region>.
  auto abs_game_root = EffectiveGameRoot();
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
