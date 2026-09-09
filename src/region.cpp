// dbz3 - Region selection support (project-side, no SDK changes).

#include "region.h"

#include "launcher/settings.h"

#include <rex/cvar.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <filesystem>
#include <memory>
#include <string>

// dbz3_region and dbz3_iso_path are project cvars (src/launcher/settings.cpp).
REXCVAR_DECLARE(std::string, dbz3_region);
REXCVAR_DECLARE(std::string, dbz3_iso_path);

namespace dbz3 {

namespace {

constexpr const char* kDrive = "\\Device\\Harddisk0\\Partition1";
constexpr const char* kNullMount = "\\Device\\Harddisk0";
constexpr const char* kRegionMount = "\\Device\\Harddisk0\\Partition1\\us";

// Effective game data root, kept in sync with the source the launcher points
// at (folder mode: the folder that directly contains us/ and eu/; ISO mode:
// the ISO cache folder holding the extracted default.xex). Stored here (not on
// the Runtime, whose copy is fixed at Setup) so region mounting and the
// launcher always agree on where the assets live, including after a runtime
// relocation (RelocateGameData).
std::filesystem::path g_effective_game_root;

// The guest hardcodes game:\us\... paths. In ISO mode the disc image contains
// BOTH region subfolders (us/ and eu/, mirroring the extracted layout) and the
// launcher picks one. Instead of stacking a second device under Partition1\us
// (the VFS matches devices by FIRST mount-path prefix, so a base at Partition1
// would always shadow it), remap us\ -> eu\ here, inside the single disc
// device: no shadowing, no ordering hazard.
class RegionDiscDevice : public rex::filesystem::DiscImageDevice {
 public:
  RegionDiscDevice(const std::string_view mount_path, const std::filesystem::path& iso,
                   std::string region)
      : DiscImageDevice(mount_path, iso), region_(std::move(region)) {}

  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    if (region_ == "eu") {
      // game:\us\... -> disc \eu\...
      if (path == "us") {
        return DiscImageDevice::ResolvePath("eu");
      }
      if (path.starts_with("us\\")) {
        std::string remapped = "eu" + std::string(path.substr(2));  // \us\... -> \eu\...
        return DiscImageDevice::ResolvePath(remapped);
      }
    }
    return DiscImageDevice::ResolvePath(path);
  }

 private:
  std::string region_;
};

bool IsoPathActive() {
  const std::string iso = REXCVAR_GET(dbz3_iso_path);
  return !iso.empty() && std::filesystem::is_regular_file(iso);
}

// Register the disc image device as the game drive base. The VFS matches the
// FIRST registered device whose mount path is a prefix of the requested path,
// so the ISO device MUST be registered before the NullDevice at \Device\Harddisk0
// (which is a prefix of \Device\Harddisk0\Partition1). We therefore tear down
// base + region + null devices and rebuild them in the safe order.
bool MountIsoDrive() {
  rex::Runtime* rt = rex::Runtime::instance();
  if (!rt || !rt->file_system()) return false;
  auto* fs = rt->file_system();

  const std::string iso = REXCVAR_GET(dbz3_iso_path);
  const std::string region = REXCVAR_GET(dbz3_region);

  // Tear down anything that was mounted before (SetupVfs or a previous remount).
  fs->UnregisterDevice(kDrive);
  fs->UnregisterDevice(kNullMount);
  fs->UnregisterDevice(kRegionMount);

  auto device = std::make_unique<RegionDiscDevice>(kDrive, iso, region);
  if (!device->Initialize()) {
    REXLOG_ERROR("MountIsoDrive: failed to initialize disc image {}", iso);
    return false;
  }
  if (!fs->RegisterDevice(std::move(device))) {
    REXLOG_ERROR("MountIsoDrive: failed to register disc image device");
    return false;
  }

  // Re-create the NullDevice for raw HDD partition accesses (cache/stfc reads).
  // Registered AFTER the ISO device so Partition1 requests hit the disc image.
  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"), std::string("\\Cache1")};
  auto null_device = std::make_unique<rex::filesystem::NullDevice>(kNullMount, null_paths);
  if (null_device->Initialize()) {
    fs->RegisterDevice(std::move(null_device));
  }

  fs->RegisterSymbolicLink("game:", kDrive);
  fs->RegisterSymbolicLink("d:", kDrive);
  REXLOG_INFO("MountIsoDrive: MOUNTED ISO {} at {} (region {})", iso, kDrive, region);
  return true;
}

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

  if (IsoPathActive()) {
    return MountIsoDrive();
  }

  auto abs_root = std::filesystem::absolute(root);
  if (!std::filesystem::is_directory(abs_root)) {
    REXLOG_WARN("RemountGameDrive: folder does not exist: {}", abs_root.string());
    return false;
  }

  // Folder mode: mount the folder as the hard disk root and point game:/d: at
  // it. Re-registering is safe before the guest module launches. The base is
  // registered BEFORE the NullDevice so Partition1 requests win the prefix
  // match; the region device (Partition1\us) is applied by ApplyRegionMount.
  fs->UnregisterDevice(kDrive);
  fs->UnregisterDevice(kNullMount);
  fs->UnregisterDevice(kRegionMount);

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

  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"), std::string("\\Cache1")};
  auto null_device = std::make_unique<rex::filesystem::NullDevice>(kNullMount, null_paths);
  if (null_device->Initialize()) {
    fs->RegisterDevice(std::move(null_device));
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

  // ISO mode: region remapping happens inside the disc device (RegionDiscDevice
  // rewrites us\ -> eu\). Make sure the drive is actually the ISO.
  if (IsoPathActive()) {
    return MountIsoDrive();
  }

  // Always drop any previously applied region device so the launcher's current
  // selection (possibly changed after the initial SetupVfs) wins.
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