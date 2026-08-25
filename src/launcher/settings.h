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

// --- Game data folder ------------------------------------------------------
// Override for the game data folder (the one that directly contains us/ and
// eu/). Empty = auto-detect (next to the exe / project root). Set by the
// launcher's "Seleccionar carpeta de datos..." so the folder survives restarts.
std::string GameDirOverride();
void SetGameDirOverride(const std::string& path);

// True if `root` looks like a game data folder: it directly contains a us/ or
// eu/ asset folder, or a default.xex entrypoint. Used by the launcher to
// validate a user-picked folder before pointing the game at it.
bool IsValidGameDataDir(const std::filesystem::path& root);

// XEX entrypoint compatibility status. Each core is a recompilation of ONE
// executable: the US/NA core only boots the US xex (yae3_xenon.xex), and the
// EU/PAL core (DBZ3_EU_VARIANT) only boots the EU xex (yae3_xenon_eu.xex).
// A core given the wrong variant's xex exits immediately with
// "No function registered" (different code layout). Region (assets us/ vs eu/)
// and language are handled by the launcher on whichever core is running.
enum class XexStatus {
  kMissing = 0,  // default.xex does not exist
  kUs = 1,       // known US/NA executable
  kEu = 2,       // known EU/PAL executable
  kUnknown = 3,  // present but not a known variant (informational note)
};
// Status of `root/default.xex`, cached by (path, size, mtime) so the per-frame
// launcher banner does not re-hash a ~4.9MB file every frame.
XexStatus CheckDefaultXex(const std::filesystem::path& root);

// Whether `status` is the executable THIS core was recompiled from. On the
// US/NA core that is the US xex; on the EU/PAL core (DBZ3_EU_VARIANT) the EU
// one. A known wrong-variant xex must be blocked; kMissing/kUnknown are not
// expected either but are handled with their own messaging.
inline bool XexIsExpected(XexStatus status) {
#if defined(DBZ3_EU_VARIANT)
  return status == XexStatus::kEu;
#else
  return status == XexStatus::kUs;
#endif
}

// Path of the most recent log file (exe_dir/logs/dbz3_*.log). Empty when the
// logs folder does not exist yet. Used by the crash dialog.
std::filesystem::path LatestLogPath();
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

// --- Quality presets --------------------------------------------------------
// One-click quality profiles that set the internal render scale, MSAA, aniso
// and upscaling effect together. Values: "auto" (detect the GPU tier and apply
// the recommended profile on every launch), "low", "medium", "high", "ultra",
// or "manual" (the individual controls below are used as-is).
std::string QualityPreset();
void SetQualityPreset(const std::string& preset);

// Name of the detected primary GPU (for the launcher UI). Empty when it cannot
// be determined.
std::string DetectGpuName();
// Detected GPU performance tier: 0 = low (old integrated), 1 = medium,
// 2 = high (modern discrete).
int32_t DetectGpuTier();
// Human-readable tier label: "Low" / "Medium" / "High".
const char* GpuTierLabel(int32_t tier);

// Apply the current quality preset to the individual quality cvars (scale/MSAA/
// aniso/effect). For "auto" it uses the detected GPU tier; for a named preset
// it also persists the resulting values. Safe to call at launch and from the UI.
void ApplyQualityPreset();
// Apply the "auto" preset at most once per process (used from
// ApplyUserSettingsToSdk so fresh installs get GPU-appropriate defaults
// without stomping a user's in-session tweaks when Play is pressed).
void ApplyQualityPresetIfAuto();

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

// Controller backend: "xinput" (native, default) or "sdl" (generic pads).
std::string InputBackend();
void SetInputBackend(const std::string& backend);

// Keyboard/mouse controller emulation (MnK). On by default: the keyboard must
// work out of the box on PC.
bool MnkMode();
void SetMnkMode(bool enabled);

// Use the mouse for the right stick (in addition to the rstick_* keys).
bool MnkMouse();
void SetMnkMouse(bool enabled);

// Read/write a dbz3_keybind_<name> cvar by suffix (e.g. "a", "dpad_up").
// These wrappers persist to dbz3_user.toml; ApplyUserSettingsToSdk forwards
// them to the runtime's keybind_* cvars.
std::string Keybind(const std::string& name);
void SetKeybind(const std::string& name, const std::string& value);

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
