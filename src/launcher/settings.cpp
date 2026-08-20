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

#if REX_PLATFORM_WIN32
#include <windows.h>
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

REXCVAR_DEFINE_STRING(dbz3_enabled_mods, "*", "DBZ3/Mods",
                      "Comma-separated list of enabled mod folders under mods/. "
                      "'*' (default) enables every detected mod. Empty disables all.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_fullscreen_mode, "windowed", "DBZ3/Video",
                      "Fullscreen mode: windowed, borderless, exclusive")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(dbz3_vsync, true, "DBZ3/Video", "Vertical sync")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_vrr, true, "DBZ3/Video",
                    "Variable refresh rate (G-Sync/FreeSync). Syncs the monitor "
                    "to each presented frame for even pacing on high-refresh panels.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(dbz3_frame_cap, 0, "DBZ3/Video",
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

// Mods root: mods/ next to the executable.
static std::filesystem::path ModsRoot() {
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

bool DevMode() { return REXCVAR_GET(dbz3_dev_mode); }
void SetDevMode(bool enabled) {
  REXCVAR_SET(dbz3_dev_mode, enabled);
  // The SDK diagnostic .bmp/readback dumps are gated by Dev mode too (see
  // SetDiagLogging): toggling Dev mode re-asserts the effective diag flag so
  // the GPU plugin stops writing frontbuf_*.bmp/black_*.bmp the moment Dev
  // mode is turned off.
  rex::cvar::SetFlagByName("dbz1_diag_logging",
                           (DiagLogging() && enabled) ? "true" : "false");
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
  rex::cvar::SetFlagByName("dbz1_diag_logging",
                           (enabled && DevMode()) ? "true" : "false");
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
  REXCVAR_SET(present_effect, REXCVAR_GET(dbz3_present_effect));
  REXCVAR_SET(user_language, static_cast<uint32_t>(Language()));
  // Host graphics backend (d3d12/vulkan). Read by the runtime when it loads
  // the GPU plugin during SetupPresentation, so it must be set before then.
  rex::cvar::SetFlagByName("gpu_backend", GpuBackend());
  // VRR must be set BEFORE the swapchain is created (the D3D12 presenter reads
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
  REXLOG_INFO("dbz3: applied user settings -> present_effect={} internal_scale={}x lang={} backend={}",
              REXCVAR_GET(present_effect), scale, LanguageName(Language()), GpuBackend());
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
  SetSdkBool("vsync", REXCVAR_GET(dbz3_vsync));
  SetSdkBool("native_2x_msaa", REXCVAR_GET(dbz3_native_2x_msaa));
  SetSdkInt("anisotropic_override", REXCVAR_GET(dbz3_anisotropic));
  SetSdkString("present_fsr_quality_mode", REXCVAR_GET(dbz3_fsr_quality));
  SetSdkDouble("present_fsr_sharpness_reduction", REXCVAR_GET(dbz3_fsr_sharpness));
  SetSdkDouble("present_cas_additional_sharpness", REXCVAR_GET(dbz3_cas_sharpness));
  SetSdkBool("audio_mute", false);
  SetSdkBool("dbz1_diag_logging", DiagLogging() && DevMode());
  SetSdkDouble("master_volume", REXCVAR_GET(dbz3_master_volume));
  SetSdkString("audio_output_device", std::string());

  // The D3D12 presenter always calls Present(0) (never waits for vsync), so the
  // "vsync" cvar has no effect there. The frame cap caps the host present rate
  // only; it never touches the guest vblank pacing or the game speed.
  // NOTE: this mirrors the working dbz1 setup. We do NOT force the cap to 60
  // nor clamp it to a clean divisor of the monitor refresh: doing so stalled
  // the launcher at high refresh rates and made the game feel laggy. The frame
  // cap is applied verbatim (0 = uncapped) exactly like dbz1.
  REXCVAR_SET(host_present_from_non_ui_thread, true);
  REXCVAR_SET(d3d12_allow_variable_refresh_rate_and_tearing, VrrEnabled());
  // The game paces its main loop by the guest vblank; keep it at 60 Hz (never
  // raise it or the game logic would run faster than intended).
  REXCVAR_SET(video_mode_refresh_rate, 60.0);
  // Host present pacing: use the user's frame cap verbatim. 0 = uncapped.
  int32_t cap = SafeFrameCap(FrameCap());
  if (cap == 0 && for_game) {
    // Uncapped is fine on VRR displays. Keep it verbatim like dbz1.
    cap = 0;
  }
  SetSdkInt("frame_cap", cap);
  REXLOG_INFO(
      "dbz3: applied runtime settings -> internal_scale={}x vsync={} msaa={} aniso={} "
      "fsr_quality={} fsr_sharpness={} cas_sharpness={} master_vol={} host_present={} vrr={} cap={}",
      scale, GetSdkBool("vsync") ? "true" : "false",
      GetSdkBool("native_2x_msaa") ? "true" : "false", GetSdkInt("anisotropic_override"),
      GetSdkString("present_fsr_quality_mode"), GetSdkDouble("present_fsr_sharpness_reduction"),
      GetSdkDouble("present_cas_additional_sharpness"), GetSdkDouble("master_volume"),
      REXCVAR_GET(host_present_from_non_ui_thread) ? "true" : "false",
      REXCVAR_GET(d3d12_allow_variable_refresh_rate_and_tearing) ? "true" : "false",
      GetSdkInt("frame_cap"));
}

}  // namespace dbz3::settings
