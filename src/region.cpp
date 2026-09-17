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

#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

// --- small path helpers -----------------------------------------------------

bool IEqualsAscii(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// True for "default.xex" (the path the runtime boots), with any leading or
// trailing separators ignored. The VFS hands the device the path with its mount
// prefix already stripped ("some\\PATH.foo").
bool IsDefaultXexRequest(std::string_view path) {
  while (!path.empty() && (path.front() == '\\' || path.front() == '/')) {
    path.remove_prefix(1);
  }
  while (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
    path.remove_suffix(1);
  }
  return IEqualsAscii(path, "default.xex");
}

std::string PathLeaf(const std::string& path) {
  const auto pos = path.find_last_of("\\/");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

// A HostPathDevice over the tiny staging folder that holds the executable the
// runtime must load as `game:\default.xex`. Used to serve that one path from
// outside the game folder (the retail disc keeps Budokai 3 as
// DBZ3\yae3_xenon.xex, not as default.xex).
std::unique_ptr<rex::filesystem::HostPathDevice> MakeXexStubDevice(
    const std::filesystem::path& xex_path, const char* mount) {
  if (xex_path.empty()) return nullptr;
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      mount, xex_path.parent_path(), /*read_only=*/true);
  if (!device->Initialize()) {
    return nullptr;
  }
  return device;
}

// Serves an extracted game data folder as the game drive. One path is special:
// `default.xex` is served from the staged executable when the real one has a
// different name (or lives in a subfolder), so nothing in the user's folder has
// to be renamed or copied over.
class GameDataHostDevice : public rex::filesystem::HostPathDevice {
 public:
  GameDataHostDevice(const std::string_view mount_path, const std::filesystem::path& host_path,
                     bool read_only, const std::filesystem::path& xex_override)
      : HostPathDevice(mount_path, host_path, read_only), xex_override_(xex_override) {}

  bool Initialize() override {
    xex_device_ = MakeXexStubDevice(xex_override_, "\\Device\\Dbz3XexStaging");
    if (!xex_override_.empty() && !xex_device_) {
      REXLOG_WARN("GameDataHostDevice: could not open the staged executable {}",
                  xex_override_.string());
    }
    return HostPathDevice::Initialize();
  }

  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    if (xex_device_ && IsDefaultXexRequest(path)) {
      if (auto* entry = xex_device_->ResolvePath("default.xex")) {
        return entry;
      }
    }
    return HostPathDevice::ResolvePath(path);
  }

 private:
  std::filesystem::path xex_override_;
  std::unique_ptr<rex::filesystem::HostPathDevice> xex_device_;
};

// The guest hardcodes game:\us\... paths. In ISO mode the disc image contains
// BOTH region subfolders (us/ and eu/, mirroring the extracted layout) and the
// launcher picks one. Instead of stacking a second device under Partition1\us
// (the VFS matches devices by FIRST mount-path prefix, so a base at Partition1
// would always shadow it), remap us\ -> eu\ here, inside the single disc
// device: no shadowing, no ordering hazard.
//
// Two more retail-disc realities are handled here:
//  * The disc's root default.xex is the HD Collection's own menu, not Budokai 3,
//    so `default.xex` is served from the staged executable instead.
//  * On a retail disc the game data lives under DBZ3\ (DBZ3\us\data_cmn.afs,
//    DBZ3\adx_usa.afs, ...), while the extracted layout has it at the root, so
//    the prefixed path is tried first and the plain one used as a fallback.
class RegionDiscDevice : public rex::filesystem::DiscImageDevice {
 public:
  RegionDiscDevice(const std::string_view mount_path, const std::filesystem::path& iso,
                   std::string region, std::filesystem::path xex_override, bool prefix_dbz3)
      : DiscImageDevice(mount_path, iso),
        region_(std::move(region)),
        xex_override_(std::move(xex_override)),
        prefix_dbz3_(prefix_dbz3) {}

  bool Initialize() override {
    xex_device_ = MakeXexStubDevice(xex_override_, "\\Device\\Dbz3IsoXexStaging");
    return DiscImageDevice::Initialize();
  }

  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    if (xex_device_ && IsDefaultXexRequest(path)) {
      if (auto* entry = xex_device_->ResolvePath("default.xex")) {
        return entry;
      }
    }

    std::string mapped(path);
    if (region_ == "eu") {
      // game:\us\... -> disc \eu\...
      if (mapped == "us") {
        mapped = "eu";
      } else if (mapped.rfind("us\\", 0) == 0 || mapped.rfind("us/", 0) == 0) {
        mapped = "eu" + mapped.substr(2);
      }
    }

    if (prefix_dbz3_ && !mapped.empty() && mapped.front() != '\\') {
      // 1) DBZ3\<path> (the data_cmn.afs and friends live under DBZ3\us\),
      // 2) DBZ3\<leaf> (adx_*.afs / *.sfd live at the DBZ3 root), 3) plain.
      const std::string candidates[] = {"DBZ3\\" + mapped, "DBZ3\\" + PathLeaf(mapped), mapped};
      for (const auto& candidate : candidates) {
        if (auto* entry = DiscImageDevice::ResolvePath(candidate)) {
          return entry;
        }
      }
      return nullptr;
    }

    return DiscImageDevice::ResolvePath(mapped);
  }

 private:
  std::string region_;
  std::filesystem::path xex_override_;
  bool prefix_dbz3_ = false;
  std::unique_ptr<rex::filesystem::HostPathDevice> xex_device_;
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

  // The runtime loads game:\default.xex from the drive; on a retail disc that
  // file is the HD Collection's menu, so serve the staged Budokai 3 executable
  // instead, and resolve the assets under DBZ3\ when the disc uses that layout.
  const auto& boot = dbz3::settings::CurrentBootSource();
  const std::filesystem::path xex_override = boot.redirect_default_xex ? boot.xex
                                                                      : std::filesystem::path{};
  auto device = std::make_unique<RegionDiscDevice>(kDrive, iso, region, xex_override,
                                                  boot.iso_prefix_dbz3);
  if (!device->Initialize()) {
    REXLOG_ERROR("MountIsoDrive: failed to initialize disc image {}", iso);
    return false;
  }
  if (boot.iso_prefix_dbz3) {
    REXLOG_INFO("MountIsoDrive: retail disc layout - resolving assets under DBZ3\\");
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

  // Serve `default.xex` from the staged executable when the game's own copy has
  // another name or lives in a subfolder (retail disc dumps keep it as
  // DBZ3\yae3_xenon.xex). Everything else comes straight from the user's folder.
  const auto& boot = dbz3::settings::CurrentBootSource();
  const std::filesystem::path xex_override = boot.redirect_default_xex ? boot.xex
                                                                      : std::filesystem::path{};
  auto device = std::make_unique<GameDataHostDevice>(
      kDrive, abs_root, !REXCVAR_GET(allow_game_relative_writes), xex_override);
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
  // The launcher can switch the data source at any time (another dump, the
  // extras folder, an ISO...). Re-resolve the executable for the new root so the
  // drive serves the right default.xex and the banner stays truthful.
  if (!IsoPathActive() && std::filesystem::is_directory(root)) {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    std::vector<std::filesystem::path> roots{root};
    if (!root.parent_path().empty()) roots.push_back(root.parent_path());
    roots.push_back(exe_dir);
    if (!exe_dir.parent_path().empty()) roots.push_back(exe_dir.parent_path());
    const auto boot =
        dbz3::settings::ResolveBootSource(root, roots, dbz3::settings::XexCacheDir());
    dbz3::settings::SetCurrentBootSource(boot);
    // The assets may live in a subfolder of what the user picked (retail discs
    // keep them in DBZ3/), so mount the folder the resolver settled on.
    if (!boot.data_root.empty() && std::filesystem::is_directory(boot.data_root)) {
      return RemountGameDrive(boot.data_root) && ApplyRegionMount();
    }
  }
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