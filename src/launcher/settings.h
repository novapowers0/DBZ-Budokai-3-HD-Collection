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

// Result of the last LoadUserSettings() call, so the launcher can tell the user
// when their settings file had to be repaired (or was unusable) instead of
// silently losing every option. A Windows path saved unescaped turned the whole
// file into a parse error on the next start ("unknown escape sequence '\G'").
enum class ConfigLoadState {
  kMissing,   // no settings file yet (fresh install)
  kOk,        // loaded cleanly
  kRepaired,  // had an invalid escape; auto-fixed and loaded
  kInvalid,   // still unparseable after repair: kept on disk, not loaded
};
ConfigLoadState LastConfigLoadState();

// Write all user cvars to dbz3_user.toml.
void SaveUserSettings();

// Absolute path of the user settings file (next to the executable).
std::filesystem::path UserSettingsPath();

// Root folder for the per-user runtime data: save games/memory cards, plus the
// xex and iso caches. Normally <exe_dir>/user_data/dbz3 so the release stays
// portable (everything next to the executable). When the executable folder is
// not writable - the game installed under Program Files, a read-only share, a
// locked-down OneDrive folder - falls back to the per-user application data
// folder so saves keep working instead of failing silently. Resolved once and
// cached (the probe creates the folders, so it must not run every frame).
std::filesystem::path UserDataRoot();
// False when UserDataRoot() fell back to the per-user folder (the portable
// location was not writable). For logs / the Dev tab.
bool UserDataIsPortable();

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
// reads from, with mods/ overrides layered on top. The dual-region core detects
// the XEX by checksum, so this only swaps the asset packs, keeping one binary.
std::string Region();
void SetRegion(const std::string& region);

// Effective asset region for `root`. Prefers the user-selected region when its
// folder (us/ or eu/) exists; otherwise falls back to whichever of the two
// folders exists so EU-only (or US-only) data works out of the box even with
// the default "us" selection. Returns the selection unchanged when neither
// folder exists. Does not mutate the cvar.
std::string ResolveRegion(const std::filesystem::path& root);

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

// --- ISO disc image --------------------------------------------------------
// Path of the Xbox 360 disc image (.iso) to play from instead of an extracted
// us/eu/ folder. Empty = folder mode. Set by the launcher's ISO picker (or
// auto-detected when a *.iso is found next to the game). The game is mounted
// directly from the GDFX image: no extraction, no repack.
std::string IsoPath();
void SetIsoPath(const std::string& path);

// True when ISO mode is active: an ISO path is set and the file exists.
bool IsIsoMode();

// First *.iso in `dir` (any depth 0). Empty when none. Used to auto-activate
// ISO mode when the user drops a disc image next to the game.
std::filesystem::path FindIsoInDir(const std::filesystem::path& dir);

// True if `iso` looks like a usable Xbox 360 disc image (regular file, .iso
// extension). The GDFX header itself is verified when the device initializes.
bool IsValidIso(const std::filesystem::path& iso);

// Extract `default.xex` (few MB) from the ISO into `dst`. The runtime needs a
// real file for its pre-flight checks and region detection; this is the ONLY
// thing ever extracted from the disc. Returns true on success.
bool ExtractDefaultXexFromIso(const std::filesystem::path& iso,
                              const std::filesystem::path& dst);

// Like ExtractDefaultXexFromIso, but also looks for the executable where a
// RETAIL disc keeps it (DBZ3/yae3_xenon.xex, DBZ3/yae3_xenon_eu.xex) - the disc's
// root default.xex is the HD Collection's menu, not Budokai 3. The first
// candidate that classifies as this port's executable wins; when none does, the
// last candidate read is left in `dst` so the launcher can still report what the
// disc contained. `out_source` (optional) receives the path inside the image
// that provided the file (e.g. "DBZ3/yae3_xenon.xex"). Returns true when a
// bootable executable was extracted.
bool ExtractGameXexFromIso(const std::filesystem::path& iso,
                           const std::filesystem::path& dst,
                           std::string* out_source);

// Which file inside the cached disc image provided the executable (e.g.
// "DBZ3/yae3_xenon.xex"), as recorded by EnsureIsoXexCache. Empty when unknown.
std::string IsoXexSourcePath();

// Ensure `cache_dir/default.xex` is the executable of the CURRENT ISO. The
// cached file is used to detect the disc's region before the guest boots, but
// the runtime executes the xex mounted from the disc. If the cache was produced
// from a DIFFERENT disc (e.g. the user swapped a US ISO for an EU one) the
// region/config chosen would not match the running executable and the guest
// dies with "No function registered at <addr>". This records the source disc's
// identity (path + size + mtime) next to the cache and re-extracts when it
// changes. Returns true when cache_dir/default.xex is present and valid.
bool EnsureIsoXexCache(const std::filesystem::path& iso,
                       const std::filesystem::path& cache_dir);

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
  kDbz1 = 4,     // known DBZ Budokai HD Collection (DBZ1) executable — a DIFFERENT title
  kHdMenu = 5,   // the HD Collection's own menu/launcher (the disc's root default.xex)
};
// Status of `root/default.xex`, cached by (path, size, mtime) so the per-frame
// launcher banner does not re-hash a ~4.9MB file every frame.
XexStatus CheckDefaultXex(const std::filesystem::path& root);

// Status of any executable file (no `<root>/default.xex` convention): used while
// hunting for the real Budokai 3 executable in a disc dump, where it is named
// `yae3_xenon.xex` / `yae3_xenon_eu.xex` and the root default.xex is the HD
// Collection's menu (see kHdMenu). Cheap for non-XEX2 files (magic + size).
XexStatus ClassifyXexFile(const std::filesystem::path& xex);

// Human-readable label for logs and the launcher banner.
const char* XexStatusLabel(XexStatus status);

// --- Locating the game executable (any layout) ------------------------------
// Retail discs keep Budokai 3 at DBZ3/yae3_xenon.xex (4 890 624 B) and the root
// default.xex is the HD Collection menu, which this core cannot boot. Users also
// extract it as yae3_xenon.xex next to the data, inside assets/, or nested a
// level deeper. This resolver finds a REAL Budokai 3 executable (by size + MD5)
// anywhere under the given roots and reports the asset folder it belongs to, so
// no manual renaming is ever required.
struct GameExecutable {
  std::filesystem::path xex;        // the executable found (host path)
  std::filesystem::path data_root;  // folder that holds us/ (or eu/) for it
  XexStatus status = XexStatus::kMissing;
  std::string found_hint;           // e.g. "DBZ3/yae3_xenon.xex"

  // True when `status` is an executable this core can actually boot.
  bool usable() const;
};

// Search `roots` (in order, bounded recursion) for a bootable Budokai 3
// executable. `data_root` is the folder that directly contains us/ or eu/ next
// to the executable (falling back to the executable's own folder). Returns an
// empty .xex when nothing usable is found.
GameExecutable FindGameExecutable(const std::vector<std::filesystem::path>& roots);

// Make `exe` bootable as `<cache_dir>/default.xex` (the runtime loads
// `game:\default.xex`). Copies only when the cache is stale or from a different
// file; never writes to the game folder. Returns the path of the bootable copy
// (empty on failure).
std::filesystem::path EnsureXexCache(const std::filesystem::path& exe,
                                     const std::filesystem::path& cache_dir);

// Small cache folder next to the executable (user_data/dbz3/xex_cache). Holds
// the staged `default.xex` when the game's own executable is named differently
// or lives in a subfolder, so the user's files are never modified.
std::filesystem::path XexCacheDir();

// --- Boot source (what the runtime will actually run) -----------------------
// The runtime loads `game:\default.xex` and threads the whole game through it.
// Retail disc dumps place Budokai 3 at DBZ3/yae3_xenon.xex (the root
// default.xex is the HD Collection's menu, which this core cannot boot), and
// extracted dumps often keep the original name too. This describes what was
// resolved so the VFS can serve the right file and the launcher can explain the
// state to the user without any manual renaming.
struct BootSource {
  std::filesystem::path data_root;    // folder mounted as game: (holds us/ or eu/)
  std::filesystem::path xex;          // executable the runtime must treat as default.xex
  bool redirect_default_xex = false;  // serve <data_root>/default.xex from `xex`
  bool iso_prefix_dbz3 = false;       // retail ISO: resolve the us/ folder under DBZ3/
  XexStatus status = XexStatus::kMissing;
  std::string note;  // short, human-readable summary for logs and the banner

  bool usable() const;
};

// Current boot source (set while configuring paths / relocating game data).
const BootSource& CurrentBootSource();
void SetCurrentBootSource(const BootSource& source);

// Resolve how to boot from `data_root`: use <data_root>/default.xex when it is a
// real Budokai 3 executable, otherwise hunt for it (DBZ3/yae3_xenon.xex, a
// renamed copy, a nested folder) and stage a copy in `cache_dir` so the drive
// can serve it as default.xex. `extra_roots` are searched first (the folder the
// user picked, the executable's folder, ...). Never writes to the game folder.
BootSource ResolveBootSource(const std::filesystem::path& data_root,
                             const std::vector<std::filesystem::path>& extra_roots,
                             const std::filesystem::path& cache_dir);

// Whether `status` is the executable THIS core was recompiled from. On the
// dual-region core (DBZ3_DUAL_REGION) both the US and EU executables are
// compiled in, so both are expected. On the US/NA core that is the US xex; on
// the EU/PAL core (DBZ3_EU_VARIANT) the EU one. A known wrong-variant xex must
// be blocked; kMissing/kUnknown are not expected either but are handled with
// their own messaging.
inline bool XexIsExpected(XexStatus status) {
#if defined(DBZ3_DUAL_REGION)
  return status == XexStatus::kUs || status == XexStatus::kEu;
#elif defined(DBZ3_EU_VARIANT)
  return status == XexStatus::kEu;
#else
  return status == XexStatus::kUs;
#endif
}

// Path of the most recent log file (exe_dir/logs/dbz3_*.log). Empty when the
// logs folder does not exist yet. Used by the crash dialog.
std::filesystem::path LatestLogPath();

// Fullscreen mode: "windowed", "borderless", "exclusive".
std::string FullscreenMode();
void SetFullscreenMode(const std::string& mode);

// VSync toggle (hot-reloadable).
bool VsyncEnabled();
void SetVsyncEnabled(bool enabled);

bool VrrEnabled();
void SetVrrEnabled(bool enabled);

// Active mod profile ("vanilla" = all mods disabled).
std::string ModProfile();
void SetModProfile(const std::string& name);

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

// MSAA: native 2x multisample for guest 2x MSAA surfaces.
bool Native2xMsaa();
void SetNative2xMsaa(bool enabled);

// Anisotropic filtering override (0 = disabled, else 1/2/4/8/16).
int32_t AnisotropicOverride();
void SetAnisotropicOverride(int32_t level);

// HD textures: runtime upscaling of the game's textures (1 = off, 2/3/4 =
// factor). Emulator-style internal filter: the host texture is created at Nx and
// filled with a bicubic pass; the game's files and guest memory are untouched.
int32_t HdTextures();
void SetHdTextures(int32_t factor);

int32_t HdTextureMaxTexels();
void SetHdTextureMaxTexels(int32_t texels);

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

// FXAA applied to the guest output at swap time (SDK's swap_post_effect):
// "none", "fxaa" (cheap) or "fxaa_extreme" (stronger). Runs before the
// upscaling effect, so it composes with FSR/CAS. Good anti-aliasing on weak
// GPUs that cannot afford the internal render scale or MSAA.
std::string Fxaa();
void SetFxaa(const std::string& mode);

// Dithering of the final presented image (SDK's present_dither): trades a
// little noise for smoother gradients on 8-bit displays.
bool PresentDither();
void SetPresentDither(bool enabled);

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

// Output gain (0.0 - 1.0) applied by the SDK's audio callback (audio_gain), and
// a hard mute (audio_mute). The guest mixes all channels into one stream, so
// per-category volume (music/SFX/voice) is not separable and is not offered.
double MasterVolume();
void SetMasterVolume(double v);
bool AudioMute();
void SetAudioMute(bool mute);

// Launcher update check (GitHub releases). On by default.
bool UpdateCheckEnabled();
void SetUpdateCheckEnabled(bool enabled);

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

// Mouse sensitivity for the right stick (SDK's mnk_sensitivity, 0.01 - 10.0).
// Only used when MnkMouse() is on.
double MnkSensitivity();
void SetMnkSensitivity(double v);

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

// Host shader compilation (SDK's async_shader_compilation). On (default) compiles
// in parallel: faster loads but can hitch when a new pipeline appears. Off
// compiles synchronously: no hitching, slower first-time loads.
bool AsyncShaderCompilation();
void SetAsyncShaderCompilation(bool enabled);

// Let the guest use occlusion queries (SDK's occlusion_query_enable). Off can
// reduce GPU stalls on some titles/drivers, at the cost of overdraw.
bool OcclusionQueries();
void SetOcclusionQueries(bool enabled);

// AFS I/O diagnostics (runtime `dbz3_io_logging`): one summary line every 5 s
// with read rate, latency percentiles and slow reads. A diagnostic, so it is
// off by default (Dev tab).
bool IoLogging();
void SetIoLogging(bool enabled);

// Performance diagnostics (runtime `dbz3_perf_logging`): one line every 5 s
// with the real FPS, the worst frame and the window focus state. A diagnostic,
// so it is off by default (Dev tab).
bool PerfLogging();
void SetPerfLogging(bool enabled);

// Sequential readahead of AFS containers (runtime `dbz3_io_readahead`): reads a
// bigger chunk once and serves the following reads from RAM. Helps on slow
// (mechanical) disks; disabled automatically while mods are installed.
bool IoReadahead();
void SetIoReadahead(bool enabled);

// Dev texture dump (runtime `dbz3_texture_dump`, a directory path): writes each
// unique guest texture as a DDS + index.jsonl for authoring texture packs
// (PCSX2-style). Off by default; requires a restart. The dump is large, so the
// folder is user-chosen (`dbz3_texture_dump`, persisted) instead of living under
// user_data on the install drive.
bool TextureDumpEnabled();
void SetTextureDumpEnabled(bool enabled);
std::string TextureDumpDir();
void SetTextureDumpDir(const std::string& dir);
std::string DefaultTextureDumpDir();

// Mute the mix while the game window is in the background (SDK
// `dbz3_mute_unfocused`, read by the audio callback). Standard emulator
// behaviour; the app writes the window focus state on every focus change.
bool MuteUnfocused();
void SetMuteUnfocused(bool enabled);

// Darken the picture while the window is in the background (in-game overlay).
// Purely visual: it makes the state obvious and hides the picture while away.
bool DimUnfocused();
void SetDimUnfocused(bool enabled);

// --- Graphics backend ------------------------------------------------------
// Host graphics backend: "d3d12" or "vulkan".
std::string GpuBackend();
void SetGpuBackend(const std::string& backend);

}  // namespace dbz3::settings
