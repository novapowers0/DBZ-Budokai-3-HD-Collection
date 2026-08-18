// dbz3 - User settings layer for the launcher / quality-of-life features.
//
// User-facing cvars live in dbz3_user.toml next to the executable so the
// advanced SDK cvars are never mixed with player options. This layer defines
// the friendly cvars, loads/saves them, and maps them onto the SDK's own
// cvars (resolution, vsync, present_effect, native_2x_msaa, ...).

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbz3::settings {

// Load dbz3_user.toml (no-op if it does not exist). Must be called after
// rex::cvar::LoadConfig for the SDK config so user values win.
void LoadUserSettings();

// Write all user cvars to dbz3_user.toml.
void SaveUserSettings();

// Absolute path of the user settings file (next to the executable).
std::filesystem::path UserSettingsPath();

// Apply user video/audio/input cvars onto the SDK's cvars. Called before
// window creation (in OnPreSetup) so they take effect at boot. Does NOT set
// "fullscreen" - that is applied on Play to keep the launcher windowed.
void ApplyUserSettingsToSdk();

// Apply the selected fullscreen mode ("fullscreen" cvar). Called on Play so
// the launcher stays windowed and the game opens in the chosen mode.
void ApplyWindowSizeToSdk();

// Apply the runtime/GPU cvars (vsync, MSAA, aniso, FSR) that only exist after
// Runtime::Setup registers them. Call from OnPostSetup.
void ApplyRuntimeSettingsToSdk(bool for_game);

// --- Video -----------------------------------------------------------------

// Internal render scale (1x..4x) applied to the guest 720p framebuffer via the
// SDK's draw_resolution_scale_x/y. This is real supersampling: 2x = 1440p
// internal, 3x = 2160p internal. Reduces aliasing without changing the window.
int32_t ResolutionScale();
void SetResolutionScale(int32_t scale);

// --- Language --------------------------------------------------------------
// Game text language (affects the data_XX.afs text pack). Values map to Xbox
// 360 XGetLanguage ids: 1=EN, 2=JP, 3=DE, 4=FR, 5=ES, 6=IT.
int32_t Language();
void SetLanguage(int32_t xbox_language_id);
// Human-readable language name for a given Xbox language id.
const char* LanguageName(int32_t xbox_language_id);

// --- Region ----------------------------------------------------------------
// Asset region: "us" or "eu". Selects which asset folder (us/ or eu/) the game
// reads from, with mods/ overrides layered on top. The recompiled XEX is the
// US one (the EU XEX is a different build and cannot run), so this only swaps
// assets (text/audio/video packs), keeping the US binary.
std::string Region();
void SetRegion(const std::string& region);
// Build the "active region" overlay next to the exe and return the game data
// root the runtime should mount. Hardlinks (with copy fallback) map every file
// the game may open to the highest-priority source:
//   mods/<mod>/<region>/file  >  <project>/<region>/file  >  <project>/us/file
// Returns project_root unchanged when region==us and no mods are present.
std::filesystem::path PrepareRegionData(const std::filesystem::path& project_root);

// --- Mods ------------------------------------------------------------------
// Names of mod folders under mods/ that are usable for the current region
// (each has a <region>/ subfolder with at least one file).
std::vector<std::string> ListAvailableMods();
// Whether a given mod folder name is currently enabled.
bool IsModEnabled(const std::string& mod_name);
// Toggle a mod's enabled state (persisted via dbz3_enabled_mods).
void SetModEnabled(const std::string& mod_name, bool enabled);

// Fullscreen mode: "windowed", "borderless", "exclusive".
std::string FullscreenMode();
void SetFullscreenMode(const std::string& mode);

// VSync toggle (hot-reloadable).
bool VsyncEnabled();
void SetVsyncEnabled(bool enabled);

bool VrrEnabled();
void SetVrrEnabled(bool enabled);

// Frame cap in FPS (0 = uncapped).
int32_t FrameCap();
void SetFrameCap(int32_t cap);

// Detect the current display refresh rate in Hz (Win32 EnumDisplaySettings).
// Returns 0.0 if it cannot be determined (callers should fall back to 60.0).
double DetectRefreshRate();

// Clamp/validate a requested frame cap so it can never stall the presenter
// (e.g. a cap below ~15 FPS or above a sane maximum is rejected). Returns the
// effective, safe cap value.
int32_t SafeFrameCap(int32_t requested);

// Choose a frame cap that duplicates the guest content evenly on the current
// display: the highest exact divisor of the monitor refresh rate that is <=
// `requested` (and >= 15). Removes judder on any panel, VRR or not.
int32_t RefreshRateCleanCap(int32_t requested);

// Gamma (0.5 - 2.0).
double Gamma();
void SetGamma(double gamma);

// MSAA: native 2x multisample for guest 2x MSAA surfaces.
bool Native2xMsaa();
void SetNative2xMsaa(bool enabled);

// Anisotropic filtering override (0 = disabled, else 1/2/4/8/16).
int32_t AnisotropicOverride();
void SetAnisotropicOverride(int32_t level);

// Upscaling effect: "bilinear", "cas", "fsr".
std::string PresentEffect();
void SetPresentEffect(const std::string& effect);

// FSR quality mode: "auto", "native_aa", "quality", "balanced", "performance".
std::string FsrQualityMode();
void SetFsrQualityMode(const std::string& mode);

// FSR sharpness reduction in stops (0.0 - 2.0).
double FsrSharpness();
void SetFsrSharpness(double sharpness);

// CAS additional sharpness (0.0 - 1.0).
double CasSharpness();
void SetCasSharpness(double sharpness);

// --- Audio -----------------------------------------------------------------

double MasterVolume();
void SetMasterVolume(double v);
double MusicVolume();
void SetMusicVolume(double v);
double SfxVolume();
void SetSfxVolume(double v);
double VoiceVolume();
void SetVoiceVolume(double v);

// --- Input -----------------------------------------------------------------

double Deadzone();
void SetDeadzone(double v);
bool RumbleEnabled();
void SetRumbleEnabled(bool enabled);

// --- Dev -------------------------------------------------------------------

bool DevMode();
void SetDevMode(bool enabled);
bool DiagLogging();
void SetDiagLogging(bool enabled);
bool CrashDumpEnabled();
void SetCrashDumpEnabled(bool enabled);

// Show an in-game FPS counter overlay (60fps debug). See Dev tab.
bool ShowFps();
void SetShowFps(bool enabled);

// --- Graphics backend ------------------------------------------------------
// Host graphics backend: "d3d12" or "vulkan".
std::string GpuBackend();
void SetGpuBackend(const std::string& backend);

}  // namespace dbz3::settings
