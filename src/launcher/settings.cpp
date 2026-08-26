// dbz3 - User settings layer implementation.

#include "settings.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <system_error>
#include <cstdio>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <dxgi.h>
#include <wincrypt.h>
#include <wrl/client.h>
// The launcher only uses DXGI for GPU detection (name + performance tier).
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")
#endif

// ---------------------------------------------------------------------------
// User-facing cvars (persisted to dbz3_user.toml)
// ---------------------------------------------------------------------------
// NOTE: dbz3_* cvars are defined here (single storage, compiled into dbz3.exe).
// SDK cvars (resolution, vsync, present_effect, ...) are DECLAREd at global
// scope below and read/written through the runtime's cvar registry.

REXCVAR_DEFINE_INT32(dbz3_resolution_scale, 1, "DBZ3/Video",
                     "Internal render scale (1x-4x supersampling of the 720p framebuffer)")
    .range(1, 4)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(dbz3_language, 1, "DBZ3/Language",
                     "Game text language (Xbox XGetLanguage id: 1=EN 2=JP 3=DE 4=FR 5=ES 6=IT)")
    .range(1, 6)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_region, "us", "DBZ3/Language",
                      "Asset region: us (NTSC) or eu (PAL). Selects the us/ or eu/ asset folder "
                      "(text/audio/video packs). The recompiled binary is always the US XEX.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_game_dir, "", "DBZ3/Paths",
                      "Override for the game data folder (the one that directly contains us/ and "
                      "eu/). Empty = auto-detect (next to the exe, project root, parent). Set by "
                      "the launcher's 'Seleccionar carpeta de datos...'.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_enabled_mods, "*", "DBZ3/Mods",
                      "Comma-separated list of enabled mod folders under mods/. "
                      "'*' (default) enables every detected mod. Empty disables all.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_mod_profile, "vanilla", "DBZ3/Mods",
                      "Active mod profile (a named set of enabled mods). "
                      "'vanilla' = all mods disabled. Applied by the launcher's "
                      "Mods tab.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(dbz3_fullscreen_mode, "windowed", "DBZ3/Video",
                      "Fullscreen mode: windowed, borderless, exclusive")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(dbz3_vsync, true, "DBZ3/Video", "Vertical sync")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_vrr, true, "DBZ3/Video",
                    "Variable refresh rate (G-Sync/FreeSync). Syncs the monitor "
                    "to each presented frame for even pacing on high-refresh panels.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(dbz3_frame_cap, 60, "DBZ3/Video",
                     "Frame cap in FPS (0 = uncapped)")
    .range(0, 240)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_gamma, 1.0, "DBZ3/Video", "Gamma correction (0.5 - 2.0)")
    .range(0.5, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_native_2x_msaa, true, "DBZ3/Video",
                    "Native 2x MSAA for guest 2x MSAA surfaces")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(dbz3_anisotropic, 5, "DBZ3/Video",
                     "Anisotropic filtering override (0 = off, 1..5)")
    .range(0, 5)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_present_effect, "fsr", "DBZ3/Video",
                      "Upscaling effect: bilinear, cas, fsr")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_fsr_quality, "quality", "DBZ3/Video",
                      "FSR quality: auto, native_aa, quality, balanced, performance")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(dbz3_fsr_sharpness, 0.2, "DBZ3/Video",
                      "FSR sharpness reduction in stops (0.0 - 2.0)")
    .range(0.0, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(dbz3_cas_sharpness, 0.0, "DBZ3/Video",
                      "CAS additional sharpness (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// One-click quality profile: "auto" (detect the GPU tier and apply the
// recommended settings on every launch), "low", "medium", "high", "ultra", or
// "manual" (the individual scale/MSAA/aniso/effect options are used as-is).
// Old installs (toml without this key) are migrated to "manual" so an existing
// custom setup is never silently changed.
REXCVAR_DEFINE_STRING(dbz3_quality_preset, "auto", "DBZ3/Video",
                      "Quality preset: auto, low, medium, high, ultra, manual")
    .allowed({"auto", "low", "medium", "high", "ultra", "manual"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(dbz3_master_volume, 1.0, "DBZ3/Audio", "Master volume (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_music_volume, 1.0, "DBZ3/Audio", "Music volume (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_sfx_volume, 1.0, "DBZ3/Audio", "SFX volume (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_voice_volume, 1.0, "DBZ3/Audio", "Voice volume (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_deadzone, 0.1, "DBZ3/Input", "Left stick deadzone (0.0 - 1.0)")
    .range(0.0, 0.9)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_rumble, true, "DBZ3/Input", "Enable controller vibration")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Controller backend: "xinput" (native, avoids the SDL_INIT_GAMEPAD hang with
// RTSS/OBS) or "sdl" (generic pads, needs the SDL gamepad layer). Forwarded to
// the SDK's input_backend before the input drivers are created in Setup.
REXCVAR_DEFINE_STRING(dbz3_input_backend, "xinput", "DBZ3/Input",
                      "Controller backend: xinput or sdl")
    .allowed({"xinput", "sdl"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Keyboard/mouse controller emulation (MnK driver in the runtime). Defaults to
// ON: Budokai 3 is a gamepad game and the keyboard must work out of the box on
// PC. Disable it if you only ever use a real pad.
REXCVAR_DEFINE_BOOL(dbz3_mnk_mode, true, "DBZ3/Input",
                    "Enable keyboard/mouse controller emulation")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Mouse -> right stick (with the SDK's mnk_sensitivity). Off means the right
// stick comes only from the rstick_* keys.
REXCVAR_DEFINE_BOOL(dbz3_mnk_mouse, false, "DBZ3/Input",
                    "Use the mouse for the right stick")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// MnK keybinds. These dbz3_* wrappers live in the launcher's registry so they
// persist to dbz3_user.toml; the values are forwarded to the shared keybind_*
// cvars (defined in rexinput -> rexruntime.dll) in ApplyUserSettingsToSdk.
// Defaults mirror the SDK 0.10 mnk_input_driver.cpp (except start, where the
// bare X key would double as the pause button). Empty = unbound.
#define DBZ3_KEYBIND(name, default_val)                                             \
  REXCVAR_DEFINE_STRING(dbz3_keybind_##name, default_val, "DBZ3/Input", "Key: " #name) \
      .lifecycle(rex::cvar::Lifecycle::kHotReload)
DBZ3_KEYBIND(a, "Semicolon,Space");
DBZ3_KEYBIND(b, "Quote,Backspace");
DBZ3_KEYBIND(x, "L");
DBZ3_KEYBIND(y, "P");
DBZ3_KEYBIND(left_trigger, "Q,I");
DBZ3_KEYBIND(right_trigger, "E,O");
DBZ3_KEYBIND(left_shoulder, "1");
DBZ3_KEYBIND(right_shoulder, "3");
DBZ3_KEYBIND(lstick_up, "W");
DBZ3_KEYBIND(lstick_down, "S");
DBZ3_KEYBIND(lstick_left, "A");
DBZ3_KEYBIND(lstick_right, "D");
DBZ3_KEYBIND(lstick_press, "F");
DBZ3_KEYBIND(rstick_up, "Up");
DBZ3_KEYBIND(rstick_down, "Down");
DBZ3_KEYBIND(rstick_left, "Left");
DBZ3_KEYBIND(rstick_right, "Right");
DBZ3_KEYBIND(rstick_press, "K");
DBZ3_KEYBIND(dpad_up, "Shift+Up");
DBZ3_KEYBIND(dpad_down, "Shift+Down");
DBZ3_KEYBIND(dpad_left, "Shift+Left");
DBZ3_KEYBIND(dpad_right, "Shift+Right");
DBZ3_KEYBIND(back, "Z,Tab");
DBZ3_KEYBIND(start, "Return");
DBZ3_KEYBIND(guide, "");
#undef DBZ3_KEYBIND

REXCVAR_DEFINE_BOOL(dbz3_dev_mode, false, "DBZ3/Dev", "Enable the F10 dev-mode overlay")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_diag_logging, false, "DBZ3/Dev",
                    "Write per-frame GPU diagnostics to dbz3_gpu_diag.log")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(dbz3_diag_crashdump, false, "DBZ3/Dev",
                    "Write a crash_*.dmp minidump on an unhandled exception")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(dbz3_show_fps, false, "DBZ3/Dev",
                    "Show the in-game FPS counter overlay (60fps debug)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_skip_launcher, false, "DBZ3/Dev",
                    "Skip the pre-game launcher and boot straight into the game")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_gpu_backend, "d3d12", "DBZ3/Video",
                      "Host graphics backend: d3d12 or vulkan")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// ---------------------------------------------------------------------------
// SDK cvars that the friendly dbz3_* options map onto.
// NOTE: only some SDK cvars are exported as linkable symbols by the runtime
// (resolution, present_effect, user_language, audio_mute, fullscreen). The
// rest are read/written via rex::cvar::GetFlagByName / SetFlagByName, which
// resolves them by name at runtime without needing a linkable symbol.
// ---------------------------------------------------------------------------
REXCVAR_DECLARE(std::string, resolution);
REXCVAR_DECLARE(bool, fullscreen);
REXCVAR_DECLARE(std::string, present_effect);
REXCVAR_DECLARE(uint32_t, user_language);
REXCVAR_DECLARE(bool, audio_mute);
REXCVAR_DECLARE(bool, host_present_from_non_ui_thread);
REXCVAR_DECLARE(bool, d3d12_allow_variable_refresh_rate_and_tearing);
REXCVAR_DECLARE(double, video_mode_refresh_rate);
// dbz1_diag_logging lives in rexruntime.dll (shared diagnostic flag). It IS
// exported by WINDOWS_EXPORT_ALL_SYMBOLS, so the exe can link its accessor and
// write it directly with REXCVAR_SET (same storage the GPU plugin reads).
// NOTE: do NOT use SetFlagByName for it: that resolves in the EXE's own cvar
// registry, where this flag is not registered, so it silently does nothing.
REXCVAR_DECLARE(bool, dbz1_diag_logging);

static std::string GetSdkString(const char* name) {
  return rex::cvar::Query<std::string>(name);
}
static void SetSdkString(const char* name, const std::string& value) {
  rex::cvar::SetFlagByName(name, value);
}
static bool GetSdkBool(const char* name) {
  return rex::cvar::Query<bool>(name);
}
static void SetSdkBool(const char* name, bool value) {
  rex::cvar::SetFlagByName(name, value ? "true" : "false");
}
static int32_t GetSdkInt(const char* name) {
  return rex::cvar::Query<int32_t>(name);
}
static void SetSdkInt(const char* name, int32_t value) {
  rex::cvar::SetFlagByName(name, std::to_string(value));
}
static double GetSdkDouble(const char* name) {
  return rex::cvar::Query<double>(name);
}
static void SetSdkDouble(const char* name, double value) {
  rex::cvar::SetFlagByName(name, std::to_string(value));
}

namespace dbz3::settings {

std::filesystem::path UserSettingsPath() {
  return rex::filesystem::GetExecutableFolder() / "dbz3_user.toml";
}

void LoadUserSettings() {
  const auto path = UserSettingsPath();
  if (std::filesystem::exists(path)) {
    rex::cvar::LoadConfig(path);
    REXLOG_INFO("dbz3: user settings loaded from {}", path.string());
    // Quality presets were added after earlier releases: an existing toml that
    // does not mention dbz3_quality_preset belongs to a user who already set up
    // their options manually, so default it to "manual" (never auto-apply and
    // change their current setup). Fresh installs (no toml) keep the "auto"
    // default and get GPU-appropriate defaults on first run.
    if (!rex::cvar::HasNonDefaultValue("dbz3_quality_preset")) {
      REXLOG_INFO("dbz3: existing user settings have no quality preset -> using 'manual'");
      REXCVAR_SET(dbz3_quality_preset, "manual");
    }
  } else {
    REXLOG_INFO("dbz3: no user settings file at {}, using defaults", path.string());
  }
}

void SaveUserSettings() {
  // Persist the friendly dbz3_* cvars plus the derived SDK cvars the user
  // controls (draw_resolution_scale_x/y). SaveConfig writes all modified
  // cvars; ensure the SDK scale is kept in sync with the friendly option.
  rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(ResolutionScale()));
  rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(ResolutionScale()));
  const auto path = UserSettingsPath();
  rex::cvar::SaveConfig(path);
  REXLOG_INFO("dbz3: user settings saved to {}", path.string());
}

int32_t ResolutionScale() { return REXCVAR_GET(dbz3_resolution_scale); }

void SetResolutionScale(int32_t scale) { REXCVAR_SET(dbz3_resolution_scale, scale); }

int32_t Language() { return REXCVAR_GET(dbz3_language); }

void SetLanguage(int32_t xbox_language_id) { REXCVAR_SET(dbz3_language, xbox_language_id); }

const char* LanguageName(int32_t xbox_language_id) {
  switch (xbox_language_id) {
    case 1:  return "English";
    case 2:  return "Japanese";
    case 3:  return "German";
    case 4:  return "French";
    case 5:  return "Spanish";
    case 6:  return "Italian";
    default: return "English";
  }
}

std::string Region() { return REXCVAR_GET(dbz3_region); }

void SetRegion(const std::string& region) { REXCVAR_SET(dbz3_region, region); }

std::string GameDirOverride() { return REXCVAR_GET(dbz3_game_dir); }

void SetGameDirOverride(const std::string& path) { REXCVAR_SET(dbz3_game_dir, path); }

bool IsValidGameDataDir(const std::filesystem::path& root) {
  return std::filesystem::is_directory(root / "us") ||
         std::filesystem::is_directory(root / "eu") ||
         std::filesystem::is_regular_file(root / "default.xex");
}

XexStatus CheckDefaultXex(const std::filesystem::path& root) {
  const auto xex = root / "default.xex";
  if (!std::filesystem::is_regular_file(xex)) return XexStatus::kMissing;

  // Cache by (path, size, mtime) so the per-frame banner does not re-hash the
  // ~4.9MB executable every frame.
  static std::string cached_path;
  static uintmax_t cached_size = 0;
  static std::filesystem::file_time_type cached_mtime{};
  static XexStatus cached_status = XexStatus::kUnknown;
  auto mtime = std::filesystem::last_write_time(xex);
  const uintmax_t size = std::filesystem::file_size(xex);
  if (cached_path == xex.string() && cached_size == size && cached_mtime == mtime) {
    return cached_status;
  }

  // The retail disc files are fixed: every US/NA copy hashes A53E..., every
  // EU/PAL copy C37E... (the raw bytes are encrypted with the region's key).
  static constexpr char kUsMd5[] = "A53E324B5D2A65EBCBF648E4F85A7271";
  static constexpr char kEuMd5[] = "C37EB979B762DA0AB5B8C9BA8037CE4E";
  XexStatus status = XexStatus::kUnknown;
  if (size == 4890624) {  // fast reject: both known variants are this size
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
      FILE* f = nullptr;
      _wfopen_s(&f, xex.c_str(), L"rb");
      if (f) {
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
          CryptHashData(hash, buf, static_cast<DWORD>(n), 0);
        }
        fclose(f);
      }
      BYTE md5[16];
      DWORD md5_len = sizeof(md5);
      CryptGetHashParam(hash, HP_HASHVAL, md5, &md5_len, 0);
      std::string hex;
      hex.reserve(32);
      for (DWORD i = 0; i < md5_len; ++i) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", md5[i]);
        hex += tmp;
      }
      if (hex == kUsMd5) {
        status = XexStatus::kUs;
      } else if (hex == kEuMd5) {
        status = XexStatus::kEu;
      }
      CryptDestroyHash(hash);
    }
    if (prov) CryptReleaseContext(prov, 0);
  }

  cached_path = xex.string();
  cached_size = size;
  cached_mtime = mtime;
  cached_status = status;
  return status;
}

std::filesystem::path LatestLogPath() {
  const auto logs_dir = rex::filesystem::GetExecutableFolder() / "logs";
  if (!std::filesystem::is_directory(logs_dir)) return {};
  std::filesystem::path newest;
  std::filesystem::file_time_type newest_time{};
  for (const auto& entry : std::filesystem::directory_iterator(logs_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".log") continue;
    const auto t = entry.last_write_time();
    if (newest.empty() || t > newest_time) {
      newest = entry.path();
      newest_time = t;
    }
  }
  return newest;
}

// Mods root: mods/ next to the executable, or the nearest "mods" folder up to
// 3 levels up (the release core exe lives in dbz3_avx2/dbz3_legacy while the
// mods stay next to the game data).
static std::filesystem::path ModsRoot() {
  std::filesystem::path probe = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  for (int depth = 0; depth < 4; ++depth) {
    const std::filesystem::path candidate = probe / "mods";
    if (std::filesystem::is_directory(candidate, ec)) {
      return candidate;
    }
    probe = probe.parent_path();
  }
  return rex::filesystem::GetExecutableFolder() / "mods";
}

// Split a comma-separated list into trimmed tokens.
static std::vector<std::string> SplitList(const std::string& list) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : list) {
    if (c == ',') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else if (c != ' ') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::vector<std::string> ListAvailableMods() {
  std::vector<std::string> mods;
  const auto root = ModsRoot();
  if (!std::filesystem::is_directory(root)) return mods;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_directory()) continue;
    const std::string name = entry.path().filename().string();
    // A mod is usable if it has a us/ or eu/ subfolder with content.
    bool usable = false;
    for (const auto& region : {"us", "eu"}) {
      auto p = entry.path() / region;
      if (std::filesystem::is_directory(p) && !std::filesystem::is_empty(p)) {
        usable = true;
        break;
      }
    }
    if (usable) mods.push_back(name);
  }
  std::sort(mods.begin(), mods.end());
  return mods;
}

// Join a vector into a comma-separated string (helper for SetModEnabled).
static std::string JoinList(const std::vector<std::string>& items) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out += ",";
    out += items[i];
  }
  return out;
}

bool IsModEnabled(const std::string& mod_name) {
  // Source of truth is the ".disabled" marker in the mod folder (what the
  // launcher's Mods tab toggles via dbz3::SetModEnabled). The legacy
  // `dbz3_enabled_mods` cvar list is NOT used for mounting: otherwise toggling
  // a mod in the launcher (which only touches the marker) would leave it
  // mounted by PrepareRegionData. A mod is enabled iff it has no .disabled
  // marker in its folder.
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  return !std::filesystem::exists(exe_dir / "mods" / mod_name / ".disabled", ec);
}

void SetModEnabled(const std::string& mod_name, bool enabled) {
  const std::string list = REXCVAR_GET(dbz3_enabled_mods);
  std::vector<std::string> tokens;
  if (list != "*") tokens = SplitList(list);
  auto it = std::find(tokens.begin(), tokens.end(), mod_name);
  const bool present = it != tokens.end();
  if (enabled && !present) {
    tokens.push_back(mod_name);
  } else if (!enabled && present) {
    tokens.erase(it);
  }
  // Keep the "all" shorthand when every available mod is on.
  std::vector<std::string> available = ListAvailableMods();
  bool all = true;
  for (const auto& m : available) {
    if (std::find(tokens.begin(), tokens.end(), m) == tokens.end()) all = false;
  }
  REXCVAR_SET(dbz3_enabled_mods, all ? "*" : JoinList(tokens));
}

// Legacy helper kept for signature compatibility. Assets are no longer staged
// into an "active_region" overlay: the game drive now points directly at the
// game folder (project_root) and the region (us/eu) is mounted by
// dbz3::ApplyRegionMount. Mods are served by the runtime's override hooks from
// mods/, so no duplication or staging happens at all.
std::filesystem::path PrepareRegionData(const std::filesystem::path& project_root) {
  REXLOG_INFO("dbz3: PrepareRegionData no longer stages an overlay; using {} directly",
              std::filesystem::absolute(project_root).string());
  return project_root;
}

std::string FullscreenMode() { return REXCVAR_GET(dbz3_fullscreen_mode); }
void SetFullscreenMode(const std::string& mode) { REXCVAR_SET(dbz3_fullscreen_mode, mode); }

bool VsyncEnabled() { return REXCVAR_GET(dbz3_vsync); }
void SetVsyncEnabled(bool enabled) { REXCVAR_SET(dbz3_vsync, enabled); }

bool VrrEnabled() { return REXCVAR_GET(dbz3_vrr); }
void SetVrrEnabled(bool enabled) { REXCVAR_SET(dbz3_vrr, enabled); }

std::string ModProfile() { return REXCVAR_GET(dbz3_mod_profile); }
void SetModProfile(const std::string& name) { REXCVAR_SET(dbz3_mod_profile, name); }

int32_t FrameCap() { return REXCVAR_GET(dbz3_frame_cap); }
void SetFrameCap(int32_t cap) { REXCVAR_SET(dbz3_frame_cap, cap); }

double DetectRefreshRate() {
#if REX_PLATFORM_WIN32
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  // Query the current display mode of the primary monitor. If the app is
  // running on a non-primary monitor this still returns the primary rate,
  // which is a good default; SDL-level per-window detection is out of scope.
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
    const DWORD hz = dm.dmDisplayFrequency;
    if (hz >= 30 && hz <= 500) {
      return static_cast<double>(hz);
    }
  }
#endif
  return 0.0;  // unknown -> caller falls back to 60.0
}

int32_t SafeFrameCap(int32_t requested) {
  // Never let the cap stall the presenter: anything below ~15 FPS would make
  // the game feel hung, and anything absurd (> 1000) is invalid. 0 = uncapped.
  if (requested == 0) {
    return 0;
  }
  if (requested < 15) {
    return 15;
  }
  if (requested > 1000) {
    return 1000;
  }
  return requested;
}

// Pick a frame cap that duplicates the guest content evenly on the current
// display: the highest exact divisor of the monitor refresh rate that is <=
// `requested` (and >= 15). This removes judder deterministically on any panel,
// VRR or not. Falls back to `requested` when the monitor is unknown.
int32_t RefreshRateCleanCap(int32_t requested) {
  if (requested == 0) {
    return 0;
  }
  const double hz = DetectRefreshRate();
  const int hz_int = static_cast<int>(hz + 0.5);
  if (hz_int < 30) {
    // Monitor unknown -> keep the requested cap (SafeFrameCap already validated).
    return requested;
  }
  // Look for the largest divisor of the refresh rate that is <= requested.
  int best = 0;
  for (int d = std::min(requested, hz_int); d >= 15; --d) {
    if (hz_int % d == 0) {
      best = d;
      break;
    }
  }
  // A divisor was found (hz_int itself always divides hz_int, and hz_int >= 30
  // > 15, so the loop never leaves best == 0 for valid inputs).
  return best != 0 ? best : requested;
}

double Gamma() { return REXCVAR_GET(dbz3_gamma); }
void SetGamma(double gamma) { REXCVAR_SET(dbz3_gamma, gamma); }

bool Native2xMsaa() { return REXCVAR_GET(dbz3_native_2x_msaa); }
void SetNative2xMsaa(bool enabled) { REXCVAR_SET(dbz3_native_2x_msaa, enabled); }

int32_t AnisotropicOverride() { return REXCVAR_GET(dbz3_anisotropic); }
void SetAnisotropicOverride(int32_t level) { REXCVAR_SET(dbz3_anisotropic, level); }

std::string PresentEffect() { return REXCVAR_GET(dbz3_present_effect); }
void SetPresentEffect(const std::string& effect) { REXCVAR_SET(dbz3_present_effect, effect); }

std::string FsrQualityMode() { return REXCVAR_GET(dbz3_fsr_quality); }
void SetFsrQualityMode(const std::string& mode) { REXCVAR_SET(dbz3_fsr_quality, mode); }

double FsrSharpness() { return REXCVAR_GET(dbz3_fsr_sharpness); }
void SetFsrSharpness(double sharpness) { REXCVAR_SET(dbz3_fsr_sharpness, sharpness); }

double CasSharpness() { return REXCVAR_GET(dbz3_cas_sharpness); }
void SetCasSharpness(double sharpness) { REXCVAR_SET(dbz3_cas_sharpness, sharpness); }

// ---------------------------------------------------------------------------
// Quality presets + GPU detection
// ---------------------------------------------------------------------------

namespace {

// Enumerate the primary (first non-software) DXGI adapter and return its
// description. Cached after the first call.
bool GetPrimaryGpu(DXGI_ADAPTER_DESC1* desc_out) {
  static DXGI_ADAPTER_DESC1 cached{};
  static bool cached_initialized = false;
  if (cached_initialized) {
    if (desc_out) *desc_out = cached;
    return true;
  }
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return false;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      const std::wstring name(desc.Description);
      const bool is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                               name.find(L"Microsoft Basic Render Driver") != std::wstring::npos;
      if (!is_software) {
        cached = desc;
        cached_initialized = true;
        if (desc_out) *desc_out = cached;
        return true;
      }
    }
    adapter.Reset();
  }
  return false;
}

}  // namespace

std::string DetectGpuName() {
  DXGI_ADAPTER_DESC1 desc{};
  if (!GetPrimaryGpu(&desc)) {
    return {};
  }
  // Convert the UTF-16 name to UTF-8 for the UI.
  std::string narrow;
  const int wide_len = lstrlenW(desc.Description);
  if (wide_len <= 0) {
    return {};
  }
  int needed = WideCharToMultiByte(CP_UTF8, 0, desc.Description, wide_len, nullptr, 0, nullptr,
                                   nullptr);
  if (needed > 0) {
    narrow.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, wide_len, narrow.data(), needed, nullptr,
                        nullptr);
  }
  return narrow;
}

int32_t DetectGpuTier() {
  DXGI_ADAPTER_DESC1 desc{};
  if (!GetPrimaryGpu(&desc)) {
    return 1;  // unknown -> medium
  }
  const std::wstring name(desc.Description);
  const size_t vram_mb = desc.DedicatedVideoMemory / (1024 * 1024);
  const bool is_intel = name.find(L"Intel") != std::wstring::npos;
  const bool is_arc = name.find(L"Arc") != std::wstring::npos;
  if (is_intel && !is_arc) {
    // Integrated Intel: never auto-assign the top tier. Modern iGPUs with
    // plenty of shared memory still get "medium"; old ones get "low".
    return vram_mb >= 2048 ? 1 : 0;
  }
  if (vram_mb >= 4096) {
    return 2;  // high
  }
  if (vram_mb >= 1536) {
    return 1;  // medium
  }
  return 0;  // low
}

const char* GpuTierLabel(int32_t tier) {
  switch (tier) {
    case 0:
      return "Low";
    case 1:
      return "Medium";
    case 2:
      return "High";
    default:
      return "?";
  }
}

// Recommended quality values for a tier. Tier 2 (high) tops out at native 1x
// supersampling + MSAA; "ultra" (2x supersampling) is always a manual choice.
struct QualityConfig {
  int32_t scale;
  bool msaa;
  int32_t aniso;
  const char* effect;
};

QualityConfig QualityConfigForTier(int32_t tier) {
  switch (tier) {
    case 0:
      return {1, false, 0, "bilinear"};  // low: everything off, minimal cost
    case 1:
      return {1, false, 3, "fsr"};  // medium: FSR upscale, 4x aniso, no MSAA
    default:
      return {1, true, 5, "fsr"};  // high: FSR + MSAA + 16x aniso
  }
}

QualityConfig QualityConfigForPreset(const std::string& preset) {
  if (preset == "low") {
    return {1, false, 0, "bilinear"};
  }
  if (preset == "medium") {
    return {1, false, 3, "fsr"};
  }
  if (preset == "ultra") {
    return {2, true, 5, "fsr"};
  }
  return {1, true, 5, "fsr"};  // high
}

void ApplyQualityConfig(const QualityConfig& cfg, bool persist) {
  SetResolutionScale(cfg.scale);
  SetNative2xMsaa(cfg.msaa);
  SetAnisotropicOverride(cfg.aniso);
  SetPresentEffect(cfg.effect);
  if (persist) {
    SaveUserSettings();
  }
}

std::string QualityPreset() { return REXCVAR_GET(dbz3_quality_preset); }

void SetQualityPreset(const std::string& preset) { REXCVAR_SET(dbz3_quality_preset, preset); }

void ApplyQualityPreset() {
  const std::string preset = QualityPreset();
  if (preset == "manual") {
    return;
  }
  if (preset == "auto") {
    // Detect the GPU and apply the recommended profile in memory (not persisted:
    // "auto" re-evaluates on every launch, so it also adapts if the user later
    // moves to a different machine).
    ApplyQualityConfig(QualityConfigForTier(DetectGpuTier()), /*persist=*/false);
    return;
  }
  ApplyQualityConfig(QualityConfigForPreset(preset), /*persist=*/true);
}

void ApplyQualityPresetIfAuto() {
  // Apply the "auto" preset at most once per process. ApplyUserSettingsToSdk
  // runs both at OnPreSetup (before the launcher shows) and again when Play is
  // pressed; without this guard, "auto" would recompute and discard an option
  // the user just changed in the launcher during the same session.
  static bool applied = false;
  if (applied) {
    return;
  }
  applied = true;
  if (QualityPreset() == "auto") {
    ApplyQualityPreset();
  }
}

double MasterVolume() { return REXCVAR_GET(dbz3_master_volume); }
void SetMasterVolume(double v) { REXCVAR_SET(dbz3_master_volume, v); }
double MusicVolume() { return REXCVAR_GET(dbz3_music_volume); }
void SetMusicVolume(double v) { REXCVAR_SET(dbz3_music_volume, v); }
double SfxVolume() { return REXCVAR_GET(dbz3_sfx_volume); }
void SetSfxVolume(double v) { REXCVAR_SET(dbz3_sfx_volume, v); }
double VoiceVolume() { return REXCVAR_GET(dbz3_voice_volume); }
void SetVoiceVolume(double v) { REXCVAR_SET(dbz3_voice_volume, v); }

double Deadzone() { return REXCVAR_GET(dbz3_deadzone); }
void SetDeadzone(double v) { REXCVAR_SET(dbz3_deadzone, v); }

bool RumbleEnabled() { return REXCVAR_GET(dbz3_rumble); }
void SetRumbleEnabled(bool enabled) { REXCVAR_SET(dbz3_rumble, enabled); }

std::string InputBackend() { return REXCVAR_GET(dbz3_input_backend); }
void SetInputBackend(const std::string& backend) { REXCVAR_SET(dbz3_input_backend, backend); }

bool MnkMode() { return REXCVAR_GET(dbz3_mnk_mode); }
void SetMnkMode(bool enabled) { REXCVAR_SET(dbz3_mnk_mode, enabled); }

bool MnkMouse() { return REXCVAR_GET(dbz3_mnk_mouse); }
void SetMnkMouse(bool enabled) { REXCVAR_SET(dbz3_mnk_mouse, enabled); }

// Read/write a dbz3_keybind_<name> cvar by suffix (e.g. "a", "dpad_up").
std::string Keybind(const std::string& name) {
  return rex::cvar::Query<std::string>("dbz3_keybind_" + name);
}
void SetKeybind(const std::string& name, const std::string& value) {
  rex::cvar::SetFlagByName("dbz3_keybind_" + name, value);
}

bool DevMode() { return REXCVAR_GET(dbz3_dev_mode); }
void SetDevMode(bool enabled) {
  REXCVAR_SET(dbz3_dev_mode, enabled);
  // The SDK diagnostic .bmp/readback dumps are gated by Dev mode too (see
  // SetDiagLogging): toggling Dev mode re-asserts the effective diag flag so
  // the GPU plugin stops writing frontbuf_*.bmp/black_*.bmp the moment Dev
  // mode is turned off. REXCVAR_SET writes the shared runtime flag directly
  // (SetFlagByName would not find it in the exe's own registry).
  REXCVAR_SET(dbz1_diag_logging, DiagLogging() && enabled);
}

bool DiagLogging() { return REXCVAR_GET(dbz3_diag_logging); }
void SetDiagLogging(bool enabled) {
  REXCVAR_SET(dbz3_diag_logging, enabled);
  // Propagate to the SDK's shared dbz1_diag_logging cvar (defined in
  // rexruntime.dll) so the GPU plugin's diagnostic logging/readbacks/.bmp
  // dumps are gated by the same toggle. Default off -> no log/.bmp clutter.
  //
  // The .bmp/readback dumps only run when Dev mode is ALSO enabled: they are
  // diagnostic-only and should never produce frontbuf_*.bmp/black_*.bmp in
  // normal play, even if this toggle is left on by accident.
  REXCVAR_SET(dbz1_diag_logging, enabled && DevMode());
}

bool CrashDumpEnabled() { return REXCVAR_GET(dbz3_diag_crashdump); }
void SetCrashDumpEnabled(bool enabled) { REXCVAR_SET(dbz3_diag_crashdump, enabled); }

bool ShowFps() { return REXCVAR_GET(dbz3_show_fps); }
void SetShowFps(bool enabled) { REXCVAR_SET(dbz3_show_fps, enabled); }

std::string GpuBackend() { return REXCVAR_GET(dbz3_gpu_backend); }
void SetGpuBackend(const std::string& backend) { REXCVAR_SET(dbz3_gpu_backend, backend); }

void ApplyUserSettingsToSdk() {
  // NOTE: do NOT set the "resolution" or "fullscreen" cvars here. Both would
  // resize/fullscreen the host window before the pre-game launcher is shown,
  // making the launcher huge or borderless. They are applied on Play via
  // ApplyWindowSizeToSdk() so the launcher always opens windowed at 1280x720.
  // If the quality preset is "auto", apply the GPU-detected profile first so
  // the individual cvars below carry the recommended values (once per process,
  // see ApplyQualityPresetIfAuto).
  ApplyQualityPresetIfAuto();
  REXCVAR_SET(present_effect, REXCVAR_GET(dbz3_present_effect));
  REXCVAR_SET(user_language, static_cast<uint32_t>(Language()));
  // Host graphics backend (d3d12/vulkan). Read by the runtime when it loads
  // the GPU plugin during SetupPresentation, so it must be set before then.
  rex::cvar::SetFlagByName("gpu_backend", GpuBackend());  // VRR must be set BEFORE the swapchain is created (the D3D12 presenter reads
  // this cvar while creating the swap chain in SetupPresentation). This runs in
  // OnPreSetup, ahead of the swapchain creation, so the swapchain gets
  // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING only if the user enabled VRR. Default is
  // OFF (clean-divisor frame cap alone gives even pacing on any panel).
  rex::cvar::SetFlagByName("d3d12_allow_variable_refresh_rate_and_tearing",
                           VrrEnabled() ? "true" : "false");
  // Set the internal render scale BEFORE the GPU plugin registers its cvars
  // (Runtime::Setup). The SDK defers unknown-config values until the cvar
  // registers, so TextureCache/RenderTargetCache pick up the right scale.
  int32_t scale = ResolutionScale();
  rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(scale));
  rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(scale));

  // Input: backend (before the input drivers are created in Setup), deadzone
  // (applied on the merged state), rumble (gates vibration), MnK emulation
  // toggle + mouse mode, and the keyboard bindings.
  rex::cvar::SetFlagByName("input_backend", InputBackend());
  SetSdkDouble("deadzone", Deadzone());
  SetSdkBool("rumble", RumbleEnabled());
  SetSdkBool("mnk_mode", MnkMode());
  SetSdkBool("mnk_mouse", MnkMouse());
#define DBZ3_FORWARD_KEYBIND(name) \
  SetSdkString("keybind_" #name, Keybind(#name))
  DBZ3_FORWARD_KEYBIND(a);
  DBZ3_FORWARD_KEYBIND(b);
  DBZ3_FORWARD_KEYBIND(x);
  DBZ3_FORWARD_KEYBIND(y);
  DBZ3_FORWARD_KEYBIND(left_trigger);
  DBZ3_FORWARD_KEYBIND(right_trigger);
  DBZ3_FORWARD_KEYBIND(left_shoulder);
  DBZ3_FORWARD_KEYBIND(right_shoulder);
  DBZ3_FORWARD_KEYBIND(lstick_up);
  DBZ3_FORWARD_KEYBIND(lstick_down);
  DBZ3_FORWARD_KEYBIND(lstick_left);
  DBZ3_FORWARD_KEYBIND(lstick_right);
  DBZ3_FORWARD_KEYBIND(lstick_press);
  DBZ3_FORWARD_KEYBIND(rstick_up);
  DBZ3_FORWARD_KEYBIND(rstick_down);
  DBZ3_FORWARD_KEYBIND(rstick_left);
  DBZ3_FORWARD_KEYBIND(rstick_right);
  DBZ3_FORWARD_KEYBIND(rstick_press);
  DBZ3_FORWARD_KEYBIND(dpad_up);
  DBZ3_FORWARD_KEYBIND(dpad_down);
  DBZ3_FORWARD_KEYBIND(dpad_left);
  DBZ3_FORWARD_KEYBIND(dpad_right);
  DBZ3_FORWARD_KEYBIND(back);
  DBZ3_FORWARD_KEYBIND(start);
  DBZ3_FORWARD_KEYBIND(guide);
#undef DBZ3_FORWARD_KEYBIND

  REXLOG_INFO("dbz3: applied user settings -> present_effect={} internal_scale={}x lang={} backend={} preset={}",
              REXCVAR_GET(present_effect), scale, LanguageName(Language()), GpuBackend(),
              QualityPreset());
}

// Apply the selected window size and fullscreen mode ("resolution"/"fullscreen"
// cvars). Called on Play so the launcher stays windowed at 1280x720 and the
// Apply the selected fullscreen mode ("fullscreen" cvar). Called on Play so
// the launcher stays windowed and the game opens in the chosen mode.
void ApplyWindowSizeToSdk() {
  REXCVAR_SET(fullscreen, REXCVAR_GET(dbz3_fullscreen_mode) != "windowed");
  REXLOG_INFO("dbz3: applied window mode -> fullscreen={}",
              REXCVAR_GET(fullscreen) ? "true" : "false");
}

void ApplyRuntimeSettingsToSdk(bool for_game) {
  // Re-assert the render scale (in case the cvar was read already), plus the
  // runtime-only cvars that only exist after the GPU plugin has registered.
  int32_t scale = ResolutionScale();
  SetSdkInt("draw_resolution_scale_x", scale);
  SetSdkInt("draw_resolution_scale_y", scale);
  // The SDK's `vsync` cvar drives the GUEST vblank pacing (GraphicsSystem's
  // vsync worker): with it enabled the guest ticks at 60 Hz and the game runs
  // at its intended speed. With it disabled the guest vblank runs at ~1000 Hz
  // and the game logic runs ~16x too fast (the classic "juego acelerado").
  // The game MUST run at 60 Hz, so always force it on here regardless of the
  // launcher toggle (which no longer exists for this reason).
  if (!REXCVAR_GET(dbz3_vsync)) {
    REXLOG_WARN("dbz3: vsync was disabled; forcing it ON so the game runs at its 60 Hz speed");
  }
  SetSdkBool("vsync", true);
  SetSdkBool("native_2x_msaa", REXCVAR_GET(dbz3_native_2x_msaa));
  SetSdkInt("anisotropic_override", REXCVAR_GET(dbz3_anisotropic));
  SetSdkString("present_fsr_quality_mode", REXCVAR_GET(dbz3_fsr_quality));
  SetSdkDouble("present_fsr_sharpness_reduction", REXCVAR_GET(dbz3_fsr_sharpness));
  SetSdkDouble("present_cas_additional_sharpness", REXCVAR_GET(dbz3_cas_sharpness));
  SetSdkBool("audio_mute", false);
  REXCVAR_SET(dbz1_diag_logging, DiagLogging() && DevMode());
  SetSdkDouble("master_volume", REXCVAR_GET(dbz3_master_volume));
  SetSdkString("audio_output_device", std::string());

  // The D3D12 presenter always calls Present(0) (never waits for vsync), so
  // the "vsync" cvar has no host-present meaning: it only paces the guest
  // vblank above. The host present rate is throttled by the real `frame_cap`
  // cvar (added to the 0.10 presenter) which the launcher maps to dbz3_frame_cap.
  // The cap is applied only for the game: the launcher keeps its own ImGui
  // repaints uncapped (frame_cap stays 0), preserving the pre-game UI behavior.
  REXCVAR_SET(host_present_from_non_ui_thread, true);
  REXCVAR_SET(d3d12_allow_variable_refresh_rate_and_tearing, VrrEnabled());
  // The game paces its main loop by the guest vblank; keep it at 60 Hz (never
  // raise it or the game logic would run faster than intended).
  REXCVAR_SET(video_mode_refresh_rate, 60.0);
  int32_t cap = SafeFrameCap(FrameCap());
  if (for_game) {
    SetSdkInt("frame_cap", cap);
  }
  REXLOG_INFO(
      "dbz3: applied runtime settings -> internal_scale={}x vsync={} msaa={} aniso={} "
      "fsr_quality={} fsr_sharpness={} cas_sharpness={} master_vol={} host_present={} vrr={} cap={}",
      scale, GetSdkBool("vsync") ? "true" : "false",
      GetSdkBool("native_2x_msaa") ? "true" : "false", GetSdkInt("anisotropic_override"),
      GetSdkString("present_fsr_quality_mode"), GetSdkDouble("present_fsr_sharpness_reduction"),
      GetSdkDouble("present_cas_additional_sharpness"), GetSdkDouble("master_volume"),
      REXCVAR_GET(host_present_from_non_ui_thread) ? "true" : "false",
      REXCVAR_GET(d3d12_allow_variable_refresh_rate_and_tearing) ? "true" : "false",
      for_game ? GetSdkInt("frame_cap") : 0);
}

}  // namespace dbz3::settings
