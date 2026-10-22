// dbz3 - User settings layer implementation.

#include "settings.h"
#include "i18n.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>

#include <toml++/toml.hpp>

#include <filesystem>
#include <set>
#include <utility>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <system_error>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>
// The launcher only uses DXGI for GPU detection (name + performance tier).
#pragma comment(lib, "dxgi.lib")
// Sensible per-platform backend defaults. Windows uses D3D12 + XInput; other
// platforms (Linux/BSD/macOS) use Vulkan + SDL, which is what the SDK selects
// natively on those platforms.
#define DBZ3_DEFAULT_GPU_BACKEND "d3d12"
#define DBZ3_DEFAULT_INPUT_BACKEND "xinput"
#else
#define DBZ3_DEFAULT_GPU_BACKEND "vulkan"
#define DBZ3_DEFAULT_INPUT_BACKEND "sdl"
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
    .lifecycle(rex::cvar::Lifecycle::kHotReload);  // live since v1.4.0 (draw scale changes live)

REXCVAR_DEFINE_INT32(dbz3_language, 1, "DBZ3/Language",
                     "Game text language (Xbox XGetLanguage id: 1=EN 2=JP 3=DE 4=FR 5=ES 6=IT)")
    .range(1, 6)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Idioma del launcher y de los textos propios que el juego no trae (portugues): "" = el de dbz3_language; "pt" =
// portugues de Brasil (el texto del juego queda en ingles). Lo pone SetLanguage(9).
REXCVAR_DEFINE_STRING(dbz3_ui_language, "", "DBZ3/Language", "Launcher/mod text language override: empty or pt")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(dbz3_region, "us", "DBZ3/Language",
                      "Asset region: us (NTSC) or eu (PAL). Selects the us/ or eu/ asset folder "
                      "(text/audio/video packs). The dual-region core auto-detects the XEX, so "
                      "this only swaps which asset folder is mounted.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_game_dir, "", "DBZ3/Paths",
                      "Override for the game data folder (the one that directly contains us/ and "
                      "eu/). Empty = auto-detect (next to the exe, project root, parent). Set by "
                      "the launcher's 'Seleccionar carpeta de datos...'.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_iso_path, "", "DBZ3/Paths",
                      "Xbox 360 disc image (.iso) to play from instead of an extracted folder. "
                      "Empty = folder mode. Set by the launcher's ISO picker / auto-detect.")
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

REXCVAR_DEFINE_BOOL(dbz3_native_2x_msaa, true, "DBZ3/Video",
                    "Native 2x MSAA for guest 2x MSAA surfaces")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(dbz3_anisotropic, 5, "DBZ3/Video",
    "Anisotropic filtering override (0 = off, 1..5)")
    .range(0, 5)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// HD textures: runtime (host-side) upscaling of the game's textures, emulator
// style. 1 = off; 2/3 = factor. No game file is modified and the guest's
// memory budget is untouched (the upscale happens in the host texture cache).
// Implemented in rexgpu-xenos (`dbz3_texture_upscale`); this is the friendly,
// persistent launcher-side value forwarded at startup/Play.
// (WIP) Filtro interno tipo emulador (capa exterior, no toca ficheros del juego).
// Escala texturas DXT y RGBA8 nativas y genera la cadena de mips; el fix del
// coste de mips quito los tirones. El coste principal es VRAM.
// El tope es x3 (no x4): x4 multiplica la VRAM y el coste casi sin ganancia
// visible sobre x3, y es la opcion que un usuario puede "abusar" sin entenderla.
REXCVAR_DEFINE_INT32(dbz3_hd_textures, 1, "DBZ3/Video",
    "HD textures (WIP): runtime texture upscale factor (1 = off, 2/3)")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Area maxima (texeles del nivel 0) de una textura que se sube de resolucion.
// Evita que texturas enormes (2048x1024+) multipliquen la VRAM. 0 = sin limite.
// Implementado en rexgpu-xenos (`dbz3_upscale_max_texels`). AJUSTE AVANZADO:
// vive en el tab Dev, no en la vista normal (no es un concepto para el usuario
// promedio: se explica con ejemplos, no en "Mpx"). Default 0.5 M texeles
// (1024x512 / 512x1024): deja fuera las grandes de escenario y mantiene el
// consumo bajo control sin que el usuario tenga que tocar nada.
REXCVAR_DEFINE_INT32(dbz3_hd_texture_max_texels, 524288, "DBZ3/Video",
    "HD textures (WIP): max texture area to upscale in texels (0 = no limit)")
    .range(0, 67108864)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(dbz3_present_effect, "fsr", "DBZ3/Video",
                      "Upscaling effect: bilinear, cas, fsr")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// FSR render resolution (v1.4.0, like a PC game's upscaler quality): with FSR
// the game can render BELOW the internal scale and FSR brings it back up, for
// real FPS. "native" renders at the internal scale (the old behaviour). Only
// whole scales exist: at 3x, quality/balanced/performance render at 2x.
REXCVAR_DEFINE_STRING(dbz3_fsr_render, "native", "DBZ3/Video",
                      "FSR render resolution: native, quality, balanced, performance, "
                      "ultra_performance")
    .allowed({"native", "quality", "balanced", "performance", "ultra_performance"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(dbz3_fsr_quality, "quality", "DBZ3/Video",
                      "FSR quality: auto, native_aa, quality, balanced, performance")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(dbz3_fsr_sharpness, 0.2, "DBZ3/Video",
                      "FSR sharpness reduction in stops (0.0 - 2.0)")
    .range(0.0, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_cas_sharpness, 0.0, "DBZ3/Video",
                      "CAS additional sharpness (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// FXAA on the guest output (SDK swap_post_effect). Cheap anti-aliasing applied
// at swap time, before the upscaling effect, so it composes with FSR/CAS.
REXCVAR_DEFINE_STRING(dbz3_fxaa, "none", "DBZ3/Video",
                      "FXAA: none, fxaa, fxaa_extreme")
    .allowed({"none", "fxaa", "fxaa_extreme"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// "Brillo HD" (rim light) on character models: 0 = off, 1 = original (SDK
// dbz3_rim_light_scale, scales the model shaders' rim strength constant).
REXCVAR_DEFINE_DOUBLE(dbz3_hd_rim_light, 1.0, "DBZ3/Video",
                      "HD rim light on character models (0 = off, 1 = original)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Experimental relight (SDK dbz3_relight_level): subtle ambient occlusion on
// the 3D scene, never on the HUD or the menus. Off by default (costs GPU time).
REXCVAR_DEFINE_INT32(dbz3_relight, 0, "DBZ3/Video",
                     "Experimental ambient occlusion on the 3D scene: 0 = off, 1 = low, 2 = high")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Dithering of the final image (SDK present_dither).
REXCVAR_DEFINE_BOOL(dbz3_present_dither, false, "DBZ3/Video",
                    "Dither the final image (smoother gradients)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Widescreen on wide/ultrawide monitors. The HD Collection always renders its
// 3D scene and HUD at a native 16:9 (1280x720); on a 21:9/32:9 panel the SDK's
// presenter has to decide what to do with the extra width. "keep" (default)
// shows the 16:9 image centered with bars (pillarbox) so nothing is distorted;
// "stretch" fills the whole window, stretching the image along X (no bars, but
// the characters look wider). This only drives the SDK's present_letterbox
// cvar; it does not touch the game files or its own HUD layout (the guest HUD
// is drawn inside the 16:9 image, so it always stays glued to the scene).
REXCVAR_DEFINE_STRING(dbz3_aspect, "keep", "DBZ3/Video",
                      "Widescreen on wide/ultrawide monitors: keep (bars, no distortion) or stretch (fill, distorted)")
    .allowed({"keep", "stretch"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// One-click quality profile: "auto" (detect the GPU tier and apply the
// recommended settings on every launch), "performance", "balanced", "quality",
// or "manual" (the individual scale/MSAA/aniso/effect options are used as-is).
// Old presets ("low"/"medium"/"high"/"ultra") are accepted as aliases and
// mapped to the new ones at load, and old installs (toml without this key) are
// migrated to "manual" so an existing custom setup is never silently changed.
REXCVAR_DEFINE_STRING(dbz3_quality_preset, "unset", "DBZ3/Video",
                      "Quality preset: auto, performance, balanced, quality, manual")
    .allowed({"unset", "auto", "performance", "balanced", "quality", "manual",
              "low", "medium", "high", "ultra"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(dbz3_master_volume, 1.0, "DBZ3/Audio", "Master volume (0.0 - 1.0)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// The guest mixes every channel into ONE stream, so per-category volume (music /
// SFX / voice) cannot be separated on the host side. The launcher therefore only
// offers what really works: an output gain (dbz3_master_volume -> the SDK's
// audio_gain, applied in the SDL callback) plus a silence toggle (dbz3_mute ->
// the SDK's audio_mute).
REXCVAR_DEFINE_BOOL(dbz3_mute, false, "DBZ3/Audio", "Mute all audio output")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Focus behaviour (see also the SDK's dbz3_mute_unfocused, forwarded below).
// `dbz3_dim_unfocused` is only used by the in-game overlay: it darkens the
// picture while the window is in the background so the state is obvious (and
// so the paused/background picture cannot be read while the player is away).
REXCVAR_DEFINE_BOOL(dbz3_dim_unfocused, true, "DBZ3/Video",
                    "Dim the picture while the window is in the background")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Saltar pantalla de cargar partida al iniciar si ya existe una partida guardada (Paso 4).
REXCVAR_DEFINE_BOOL(dbz3_skip_load_screen, true, "DBZ3/General",
                    "Saltar pantalla de cargar partida al iniciar si ya existe una partida guardada")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Update check (GitHub releases). On by default; the request only reads the
// latest release tag/URL over HTTPS and can be turned off in the Dev tab.
REXCVAR_DEFINE_BOOL(dbz3_update_check, true, "DBZ3/Dev",
                    "Check GitHub for a newer release when the launcher opens")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(dbz3_deadzone, 0.1, "DBZ3/Input", "Left stick deadzone (0.0 - 1.0)")
    .range(0.0, 0.9)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dbz3_rumble, true, "DBZ3/Input", "Enable controller vibration")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Controller backend: "xinput" (native, avoids the SDL_INIT_GAMEPAD hang with
// RTSS/OBS) or "sdl" (generic pads, needs the SDL gamepad layer). Forwarded to
// the SDK's input_backend before the input drivers are created in Setup.
REXCVAR_DEFINE_STRING(dbz3_input_backend, DBZ3_DEFAULT_INPUT_BACKEND, "DBZ3/Input",
                      "Controller backend: xinput or sdl")
    .allowed({"xinput", "sdl"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Controller glyph set: which button names the launcher shows next to the MnK
// keybinds (LT/L2/ZL, ...). Cosmetic only; the runtime mapping is unchanged.
REXCVAR_DEFINE_STRING(dbz3_input_glyphs, "xbox", "DBZ3/Input",
                      "Launcher button labels: xbox, playstation or switch")
    .allowed({"xbox", "playstation", "switch"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

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

// Mouse -> right stick sensitivity (SDK mnk_sensitivity). Only used when
// dbz3_mnk_mouse is on.
REXCVAR_DEFINE_DOUBLE(dbz3_mnk_sensitivity, 1.0, "DBZ3/Input",
                      "Mouse sensitivity for the right stick")
    .range(0.01, 10.0)
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

// Host shader compilation policy (SDK async_shader_compilation). On = compile in
// parallel (default: faster loads, may hitch when a new pipeline first appears);
// off = compile synchronously (no hitching, slower first-time loads). Useful to
// tell a shader-compilation hitch apart from a real performance problem.
REXCVAR_DEFINE_BOOL(dbz3_async_shaders, true, "DBZ3/Dev",
                    "Compile shaders asynchronously (off = no hitching)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Guest occlusion queries (SDK occlusion_query_enable). Off skips them, trading
// overdraw for fewer GPU sync stalls: a diagnostic lever for slow GPUs.
REXCVAR_DEFINE_BOOL(dbz3_occlusion_queries, true, "DBZ3/Dev",
                    "Let the guest use occlusion queries")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Grafica (fabricante:modelo:subsistema:revision:driver) en la que Direct3D 12 ya funciono: con la misma
// grafica y el mismo driver no se repite la prueba al arrancar (cargaba el driver dos veces: 0,2-0,4 s).
REXCVAR_DEFINE_STRING(dbz3_d3d12_ok, "", "DBZ3/Launcher", "Adapter key on which Direct3D 12 last worked");

REXCVAR_DEFINE_BOOL(dbz3_skip_launcher, false, "DBZ3/Dev",
                    "Skip the pre-game launcher and boot straight into the game")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// One-shot self-repair. Set by the launcher's "Reparar instalacion" button (via
// RepairInstallation) or from the command line as --dbz3_repair=true /
// REX_DBZ3_REPAIR=1; the launcher reads and clears it on the next draw.
REXCVAR_DEFINE_BOOL(dbz3_repair, false, "DBZ3/Dev",
                    "Repair the installation on the next launcher start")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Destination folder for the dev texture dump. The dump can be hundreds of MB
// (every unique texture as a DDS), so it must NOT default to the install/build
// drive: the user picks a folder here and it is remembered across sessions.
//
// NOTE: this deliberately reuses the GPU plugin's cvar name
// (`dbz3_texture_dump`, defined in rexgpu-xenos). The plugin reads its own
// registry from `dbz3_user.toml` at startup, so this launcher-side cvar is what
// persists the path to that file (the exe's registry and the plugin's are
// separate; the TOML is the bridge).
REXCVAR_DEFINE_STRING(dbz3_texture_dump, "", "DBZ3/Dev",
                      "Folder for the dev texture dump (empty = off)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Texture packs: folders inside `mods/` with `<hash>_<W>x<H>_<FOURCC>.dds`
// (or .png) that replace the game's textures at runtime (PCSX2 style). The
// launcher detects them (see RefreshTexturePacks) and persists the list here;
// the GPU plugin reads it (same cvar name).
REXCVAR_DEFINE_STRING(dbz3_texture_packs, "", "DBZ3/Dev",
                      "Active texture pack folders, ';'-separated (empty = off)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Live switch for the texture packs (F1 menu): off = original textures, no restart.
// The GPU plugin reads it by name every frame.
REXCVAR_DEFINE_BOOL(dbz3_texture_packs_on, true, "DBZ3/Video",
                    "Use the installed HD texture packs (off = original textures)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Tiras doradas de los lados en los menus (las del juego y las de nuestras pantallas). Apagado: fondos a toda
// la ventana, sin tiras.
REXCVAR_DEFINE_BOOL(dbz3_ui_side_borders, false, "DBZ3/Video",
                    "Golden side borders on menus (off = full-width backgrounds)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(dbz3_gpu_backend, DBZ3_DEFAULT_GPU_BACKEND, "DBZ3/Video",
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

namespace {

// Re-write a config file escaping `\` and `"` inside double-quoted string
// values (`name = "value"`). The SDK's writer emits the raw value, so a Windows
// path such as E:\Game Roms\... lands as an invalid escape sequence and the
// WHOLE file fails to parse on the next start ("unknown escape sequence '\G'"),
// silently discarding every user setting. Escaping after each save keeps the
// file loadable (and self-heals files written by older builds).
bool EscapeTomlStrings(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string line;
  std::string out;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq != std::string::npos) {
      size_t i = eq + 1;
      while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
      if (i < line.size() && line[i] == '"') {
        const size_t close = line.rfind('"');
        if (close > i) {
          const std::string raw = line.substr(i + 1, close - i - 1);
          // Decode the escapes a previous pass may have added, then re-encode:
          // the pass has to be idempotent (SaveConfig only rewrites the file
          // when a cvar is modified, so this can run over an already-escaped
          // file, and doubling every backslash would corrupt the paths).
          std::string value;
          value.reserve(raw.size());
          for (size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] == '\\' && k + 1 < raw.size() &&
                (raw[k + 1] == '\\' || raw[k + 1] == '"')) {
              value.push_back(raw[k + 1]);
              ++k;
            } else {
              value.push_back(raw[k]);
            }
          }
          std::string esc;
          esc.reserve(value.size() + 8);
          for (const char c : value) {
            if (c == '\\' || c == '"') {
              esc.push_back('\\');
            }
            esc.push_back(c);
          }
          if (esc != raw) {
            line = line.substr(0, i + 1) + esc + line.substr(close);
          }
        }
      }
    }
    out += line;
    out += '\n';
  }
  in.close();
  // Escritura atomica (bugs.md P2): primero a .tmp y luego se cambia por el bueno; si el juego muere a mitad,
  // el dbz3_user.toml anterior sigue entero.
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os << out;
    os.close();
    if (!os) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {   // p. ej. un antivirus con el fichero abierto: se escribe directamente como antes (el .tmp
              // se queda y se reescribe la proxima vez)
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os << out;
    return os.good();
  }
  return true;
}

// True when `path` parses as TOML. Used to detect a settings file that an older
// build wrote with an unescaped Windows path ("unknown escape sequence '\G'"):
// such a file is silently discarded by the runtime, so the launcher repairs it
// before loading and tells the user.
//
// Reads the bytes and parses the text rather than toml::parse_file(path): the
// latter takes a UTF-8 path, and std::filesystem::path::string() yields the
// native ANSI encoding on Windows, so a folder with non-ASCII characters
// ("Música", "Juegos") would fail to open and look like a corrupt file.
bool TomlParses(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try {
    (void)toml::parse(text);
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

}  // namespace

namespace dbz3::settings {

namespace {
// Cached: the probe creates folders/files, so it must not run per frame.
std::filesystem::path g_user_data_root;
bool g_user_data_portable = true;

// Result of the last LoadUserSettings(): lets the launcher warn when the file
// had to be repaired or is still unusable (see ConfigLoadState).
ConfigLoadState g_config_load_state = ConfigLoadState::kMissing;
// Bloqueo de sesión: un fallo de respaldo protege también el auto-guardado.
bool g_settings_write_blocked = false;
bool PreserveSettings(const std::filesystem::path& path) {
  std::error_code ec;
  auto backup = std::filesystem::path(path.string() + ".backup");
  unsigned suffix = 0;
  while (std::filesystem::exists(backup, ec) && !ec) {
    backup = path.string() + ".backup." + std::to_string(++suffix);
  }
  if (ec || !std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, ec)) return false;
  REXLOG_INFO("dbz3: ajustes preservados en {}", backup.string());
  return true;
}

// True when `dir` can be created and written to. Used to keep the release
// portable (everything next to the exe) without breaking when the install
// folder is read-only (Program Files, a locked-down share, OneDrive).
bool IsWritableDir(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }
  const auto probe = dir / ".dbz3_write_test";
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "dbz3";
    if (!out) return false;
  }
  std::filesystem::remove(probe, ec);
  return true;
}
}  // namespace

std::filesystem::path UserSettingsPath() {
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  const auto portable = exe_dir / "dbz3_user.toml";
  // Normally the settings file lives next to the executable (portable). If that
  // folder cannot be written, saving would fail silently and every option would
  // reset on the next launch, so use the per-user data folder instead.
  if (std::filesystem::exists(portable) || IsWritableDir(exe_dir)) {
    return portable;
  }
  return UserDataRoot() / "dbz3_user.toml";
}

std::filesystem::path UserDataRoot() {
  if (!g_user_data_root.empty()) return g_user_data_root;
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  const auto portable = exe_dir / "user_data" / "dbz3";
  if (IsWritableDir(portable)) {
    g_user_data_portable = true;
    g_user_data_root = portable;
    return g_user_data_root;
  }
  const auto fallback = rex::filesystem::GetUserFolder() / "dbz3";
  if (IsWritableDir(fallback)) {
    g_user_data_portable = false;
    g_user_data_root = fallback;
    REXLOG_WARN("dbz3: '{}' is not writable; storing user data (saves) in '{}'",
                portable.string(), fallback.string());
    return g_user_data_root;
  }
  // Nothing writable: keep the portable path so behaviour is unchanged.
  g_user_data_root = portable;
  return g_user_data_root;
}

bool UserDataIsPortable() {
  UserDataRoot();
  return g_user_data_portable;
}

// Primer arranque (sin dbz3_user.toml): el launcher, los avisos previos y el texto del juego salen en el
// idioma de Windows (es/it/de/fr; el resto, ingles). Nunca pisa un idioma elegido (entorno o linea de comandos).
static void ApplyFirstRunLanguage() {
  if (rex::cvar::HasNonDefaultValue("dbz3_language") || std::getenv("REX_DBZ3_LANGUAGE")) return;
  int32_t id = 1;
#if REX_PLATFORM_WIN32
  switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH: id = 5; break;
    case LANG_ITALIAN: id = 6; break;
    case LANG_GERMAN: id = 3; break;
    case LANG_FRENCH: id = 4; break;
    case LANG_JAPANESE: id = 2; break;
    case LANG_PORTUGUESE: id = 9; break;
    default: break;
  }
#else
  if (const char* l = std::getenv("LANG")) {
    const std::string s(l);
    id = s.rfind("es", 0) == 0 ? 5 : s.rfind("it", 0) == 0 ? 6 : s.rfind("de", 0) == 0 ? 3
       : s.rfind("fr", 0) == 0 ? 4 : s.rfind("ja", 0) == 0 ? 2 : s.rfind("pt", 0) == 0 ? 9 : 1;
  }
#endif
  if (id != 1) {
    SetLanguage(id);
    REXLOG_INFO("dbz3: primer arranque: idioma {} (el de Windows)", id);
  }
}

void LoadUserSettings() {
  const auto path = UserSettingsPath();
  if (!std::filesystem::exists(path)) {
    g_config_load_state = ConfigLoadState::kMissing;
    REXLOG_INFO("dbz3: no user settings file at {}, using defaults", path.string());
    ApplyFirstRunLanguage();
    return;
  }
  // Self-heal a file an older build left with an unescaped Windows path: it
  // fails to parse as a whole, so the runtime would ignore EVERY setting. Repair
  // first (idempotent), then load, and remember what happened for the UI.
  if (!TomlParses(path)) {
    if (!PreserveSettings(path)) {
      g_settings_write_blocked = true;
      g_config_load_state = ConfigLoadState::kInvalid;
      REXLOG_ERROR("dbz3: respaldo imposible: ajustes intactos, guardado bloqueado");
      return;
    }
    REXLOG_WARN("dbz3: user settings at {} failed to parse; attempting repair", path.string());
    EscapeTomlStrings(path);
    if (TomlParses(path)) {
      g_config_load_state = ConfigLoadState::kRepaired;
      REXLOG_INFO("dbz3: user settings repaired at {}", path.string());
    } else {
      // Still broken (not just an escape problem). Keep a copy of the user's
      // file BEFORE anything else: the launcher auto-saves on close, which would
      // otherwise overwrite it with defaults and destroy their setup. The
      // original stays next to it as dbz3_user.toml.bak and the UI explains it.
      // (This runs once per boot: LoadUserSettings is called twice.)
      if (g_config_load_state != ConfigLoadState::kInvalid) {
        g_config_load_state = ConfigLoadState::kInvalid;
        std::error_code ec;
        if (!PreserveSettings(path)) {
          g_settings_write_blocked = true;
          ec = std::make_error_code(std::errc::permission_denied);
        }
        if (ec) {
          REXLOG_ERROR("dbz3: user settings at {} are invalid (backup failed: {}); not loaded",
                       path.string(), ec.message());
        } else {
          REXLOG_ERROR("dbz3: user settings at {} are invalid; preserved in unique backup (see log), not loaded",
                       path.string());
        }
      }
      return;
    }
  } else if (g_config_load_state != ConfigLoadState::kRepaired &&
             g_config_load_state != ConfigLoadState::kInvalid) {
    // Keep the "repaired"/"invalid" notice: LoadUserSettings runs twice per boot
    // (path setup, then pre-setup) and the second pass already sees the fixed
    // file, so unconditionally writing kOk would swallow the message.
    g_config_load_state = ConfigLoadState::kOk;
  }
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
}

ConfigLoadState LastConfigLoadState() { return g_config_load_state; }

void SaveUserSettings() {
  if (g_settings_write_blocked) {
    REXLOG_WARN("dbz3: guardado omitido para conservar ajustes sin respaldo");
    return;
  }
  // Persist the friendly dbz3_* cvars plus the derived SDK cvars the user
  // controls (draw_resolution_scale_x/y). SaveConfig writes all modified
  // cvars; ensure the SDK scale is kept in sync with the friendly option.
  rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(ResolutionScale()));
  rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(ResolutionScale()));
  const auto path = UserSettingsPath();
  rex::cvar::SaveConfig(path);
  // Escape the quoted values (Windows paths) so the file still parses on the
  // next start - see EscapeTomlStrings.
  if (!EscapeTomlStrings(path)) {
    REXLOG_WARN("dbz3: could not normalise the escaped config at {}", path.string());
  }
  REXLOG_INFO("dbz3: user settings saved to {}", path.string());
}

std::string RepairInstallation() {
  std::string report;
  auto line = [&](const std::string& s) {
    report += s;
    report += '\n';
    REXLOG_INFO("dbz3 repair: {}", s);
  };
  line(i18n::T("[OK] Reparacion del launcher iniciada", "[OK] Launcher repair started"));
  bool missing_required = false;      // falta una DLL imprescindible
  bool settings_quarantined = false;  // los ajustes ilegibles se apartaron

  // 1. User settings: quarantine an unreadable file, then write a clean one.
  bool settings_ok_to_write = true;
  const auto settings_path = UserSettingsPath();
  if (std::filesystem::exists(settings_path)) {
    bool parses = TomlParses(settings_path);
    if (!parses) {
      if (PreserveSettings(settings_path)) {
        EscapeTomlStrings(settings_path);
        parses = TomlParses(settings_path);
      } else {
        settings_ok_to_write = false;
        g_settings_write_blocked = true;
        line(i18n::T("[!] No se pudo respaldar: se conservan los ajustes originales",
                     "[!] Backup failed: the original settings are kept"));
      }
    }
    if (!parses && settings_ok_to_write) {
      std::error_code ec;
      std::filesystem::path quarantine = settings_path.string() + ".invalid";
      unsigned suffix = 0;
      while (std::filesystem::exists(quarantine, ec) && !ec) {
        quarantine = settings_path.string() + ".invalid." + std::to_string(++suffix);
      }
      if (!ec) std::filesystem::rename(settings_path, quarantine, ec);
      if (ec) {
        settings_ok_to_write = false;
        g_settings_write_blocked = true;
        line(std::string(i18n::T("[!] No se pudo apartar el archivo de ajustes: ",
                                "[!] Could not move the settings file aside: ")) + ec.message());
      } else {
        settings_quarantined = true;
        line(std::string(i18n::T("[i] Archivo de ajustes ilegible apartado en: ",
                                "[i] Unreadable settings file moved to: ")) + quarantine.string());
      }
    }
  }
  if (settings_ok_to_write) {
    g_settings_write_blocked = false;
    SaveUserSettings();
    line(std::string(i18n::T("[i] Ajustes guardados en: ", "[i] Settings saved to: ")) + UserSettingsPath().string());
  }

  // 2. Runtime DLLs next to the executable.
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  struct DllCheck {
    const char* name;
    bool required;
  };
  const DllCheck dlls[] = {
      {"rexruntime.dll", true},
      {"rexgpu-xenos.dll", true},
      {"amd_fidelityfx_dx12.dll", false},
  };
  for (const DllCheck& d : dlls) {
    const auto file = exe_dir / d.name;
    std::error_code ec;
    const bool exists = std::filesystem::exists(file, ec);
    if (exists && std::filesystem::is_regular_file(file, ec)) {
      const auto size = std::filesystem::file_size(file, ec);
      if (ec || size == 0) {
        missing_required = missing_required || d.required;
        line(std::string("[!] ") + d.name + i18n::T(" no se puede leer (0 bytes)", " cannot be read (0 bytes)"));
      } else {
        line(std::string("[OK] ") + d.name + " (" + std::to_string(size) + " B)");
      }
    } else {
      missing_required = missing_required || d.required;
      line(std::string(d.required ? i18n::T("[!] FALTA ", "[!] MISSING ")
                                   : i18n::T("[i] No encontrado (opcional): ", "[i] Not found (optional): ")) + d.name);
    }
  }

  // 3. SDL controller mappings (only used by the SDL backend).
  if (std::filesystem::exists(exe_dir / "gamecontrollerdb.txt")) {
    line(i18n::T("[OK] gamecontrollerdb.txt presente", "[OK] gamecontrollerdb.txt present"));
  } else {
    line(i18n::T("[i] gamecontrollerdb.txt no encontrado (solo afecta al backend SDL)",
                 "[i] gamecontrollerdb.txt not found (only matters for the SDL backend)"));
  }

  // 4. User-data folder (saves, xex/iso caches).
  const auto data_root = UserDataRoot();
  std::error_code ec;
  std::filesystem::create_directories(data_root, ec);
  line(std::string(ec ? "[!] " : "[OK] ") + i18n::T("Datos de usuario: ", "User data: ") + data_root.string() +
       (UserDataIsPortable() ? " (portable)" : i18n::T(" (fuera del ejecutable)", " (outside the game folder)")));

  line(i18n::T("[OK] Reparacion completada.", "[OK] Repair finished."));

  // Primero, en una frase, lo que entiende un jugador y que hacer; la lista de comprobaciones va
  // detras de una linea en blanco (el launcher la pliega en "Detalle tecnico").
  std::string summary;
  if (missing_required) {
    summary = i18n::T("Falta una parte del juego (archivos .dll). Descomprime otra vez el zip completo del "
                      "juego en una carpeta nueva y copia ahí tus carpetas mods y user_data (tus partidas).",
                      "A part of the game is missing (.dll files). Unzip the full game package again into a "
                      "new folder and copy your mods and user_data folders (your saves) there.");
  } else if (!settings_ok_to_write) {
    summary = i18n::T("No se pudo arreglar el archivo de ajustes: puede que la carpeta del juego sea de solo "
                      "lectura. Mueve el juego a una carpeta tuya (por ejemplo, Documentos) y vuelve a "
                      "pulsar «Reparar instalación».",
                      "The settings file could not be fixed: the game folder may be read-only. Move the game "
                      "to a folder of your own (for example, Documents) and press 'Repair install' again.");
  } else if (settings_quarantined) {
    summary = i18n::T("Tus ajustes estaban dañados: se han apartado (no se borran) y ahora se usan los de "
                      "fábrica. Ya puedes jugar.",
                      "Your settings were damaged: they were moved aside (not deleted) and the defaults are "
                      "now in use. You can play now.");
  } else {
    summary = i18n::T("Todo en orden: los archivos del juego están completos y tus ajustes se han guardado "
                      "de nuevo. No se ha tocado ninguna partida ni ningún mod.",
                      "All good: the game files are complete and your settings were saved again. No save "
                      "or mod was touched.");
  }
  return summary + "\n\n" + report;
}

int32_t ResolutionScale() { return REXCVAR_GET(dbz3_resolution_scale); }

void SetResolutionScale(int32_t scale) { REXCVAR_SET(dbz3_resolution_scale, scale); }

int32_t Language() { return REXCVAR_GET(dbz3_language); }

int32_t UiLanguage() { return REXCVAR_GET(dbz3_ui_language) == "pt" ? 9 : Language(); }

void SetLanguage(int32_t xbox_language_id) {
  REXCVAR_SET(dbz3_ui_language, std::string(xbox_language_id == 9 ? "pt" : ""));
  REXCVAR_SET(dbz3_language, xbox_language_id == 9 ? 1 : xbox_language_id);   // portugues: el juego, en ingles
}

const char* LanguageName(int32_t xbox_language_id) {
  switch (xbox_language_id) {
    case 1:  return "English";
    case 2:  return "Japanese";
    case 3:  return "German";
    case 4:  return "French";
    case 5:  return "Spanish";
    case 6:  return "Italian";
    case 9:  return "Portuguese (Brazil)";
    default: return "English";
  }
}

std::string Region() { return REXCVAR_GET(dbz3_region); }

void SetRegion(const std::string& region) { REXCVAR_SET(dbz3_region, region); }

std::string ResolveRegion(const std::filesystem::path& root) {
  const std::string sel = Region();
  if (root.empty()) {
    return sel;
  }
  if (std::filesystem::is_directory(root / sel)) {
    return sel;
  }
  // The selected folder is missing (e.g. EU-only data with the default "us").
  // Prefer whichever of us/eu actually exists. "eu" first so an EU-only folder
  // does not accidentally stay "us" when both are absent.
  if (std::filesystem::is_directory(root / "eu")) return "eu";
  if (std::filesystem::is_directory(root / "us")) return "us";
  return sel;
}

std::string GameDirOverride() { return REXCVAR_GET(dbz3_game_dir); }

void SetGameDirOverride(const std::string& path) { REXCVAR_SET(dbz3_game_dir, path); }

bool IsValidGameDataDir(const std::filesystem::path& root) {
  if (std::filesystem::is_directory(root / "us") ||
      std::filesystem::is_directory(root / "eu") ||
      std::filesystem::is_regular_file(root / "default.xex")) {
    return true;
  }
  // Retail disc dumps keep Budokai 3 inside DBZ3/ (next to the HD Collection's
  // own default.xex menu) and extracted copies often keep the original name, so
  // accept every layout the boot resolver can work with.
  const std::filesystem::path dirs[] = {root / "DBZ3", root / "assets",
                                        root / "assets" / "DBZ3"};
  for (const auto& dir : dirs) {
    if (std::filesystem::is_directory(dir / "us") ||
        std::filesystem::is_directory(dir / "eu")) {
      return true;
    }
  }
  const std::filesystem::path xexes[] = {root / "yae3_xenon.xex",
                                         root / "yae3_xenon_eu.xex",
                                         root / "DBZ3" / "yae3_xenon.xex",
                                         root / "DBZ3" / "yae3_xenon_eu.xex",
                                         root / "assets" / "default.xex"};
  for (const auto& xex : xexes) {
    if (XexIsExpected(ClassifyXexFile(xex))) {
      return true;
    }
  }
  return false;
}

std::string IsoPath() { return REXCVAR_GET(dbz3_iso_path); }

void SetIsoPath(const std::string& path) { REXCVAR_SET(dbz3_iso_path, path); }

bool IsIsoMode() {
  const std::string p = IsoPath();
  return !p.empty() && std::filesystem::is_regular_file(p);
}

std::filesystem::path FindIsoInDir(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return {};
  }
  for (auto [it_1, iec_1] = std::pair{std::filesystem::directory_iterator(dir, ec), std::error_code{}}; !iec_1 && it_1 != std::filesystem::directory_iterator(); it_1.increment(iec_1)) {
    const auto& entry = *it_1;
    if (entry.is_regular_file() && IsValidIso(entry.path())) return entry.path();
  }
  return {};
}

// Imagen de disco de Xbox 360 con cualquier extension (o sin ella: Windows oculta las extensiones):
// se mira la marca del disco en los mismos sitios que el lector (disc_image_device.cpp).
bool IsValidIso(const std::filesystem::path& iso) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(iso, ec)) {
    return false;
  }
  std::string ext = iso.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  // tambien un .iso: uno de PS2 o a medio descargar no se da por bueno (el juego callaba el fallo)
  (void)ext;
  if (std::filesystem::file_size(iso, ec) < (256u << 20) || ec) return false;
  std::ifstream f(iso, std::ios::binary);
  for (const uint64_t off : {0x00000000ull, 0x0000FB20ull, 0x00020600ull, 0x02080000ull, 0x0FD90000ull}) {
    char magic[20] = {};
    if (f.seekg(std::streamoff(off + 32 * 2048)) && f.read(magic, 20) && std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
      return true;
    }
    f.clear();
  }
  return false;
}

// Portable MD5 (RFC 1321), public-domain style. Used to fingerprint the ~4.9MB
// XEX so CheckDefaultXex can tell the US/NA executable from the EU/PAL one
// without depending on Windows CryptoAPI (keeps the launcher buildable on Linux).
namespace {

struct Md5Digest {
  static constexpr uint32_t kK[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
  };
  static constexpr int kS[64] = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
  };

  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  uint8_t block[64] = {};
  size_t block_len = 0;
  uint64_t total_bytes = 0;

  static uint32_t Rot(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
  static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
  static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
  static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
  static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

  void ProcessBlock(const uint8_t* p) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = uint32_t(p[i * 4]) | (uint32_t(p[i * 4 + 1]) << 8) |
             (uint32_t(p[i * 4 + 2]) << 16) | (uint32_t(p[i * 4 + 3]) << 24);
    }
    uint32_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 64; ++i) {
      const int round = i / 16;
      const int j = i % 16;
      const uint32_t mi = (round == 0) ? uint32_t(j)
                         : (round == 1) ? uint32_t((5 * j + 1) & 15)
                         : (round == 2) ? uint32_t((3 * j + 5) & 15)
                         : uint32_t((7 * j) & 15);
      const uint32_t f = (round == 0) ? F(b, c, d)
                       : (round == 1) ? G(b, c, d)
                       : (round == 2) ? H(b, c, d)
                       : I(b, c, d);
      a += f + m[mi] + kK[i];
      a = Rot(a, kS[i]);
      a += b;
      const uint32_t tmp = a;
      a = d;
      d = c;
      c = b;
      b = tmp;
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }

 public:
  void Update(const uint8_t* data, size_t len) {
    total_bytes += len;
    while (len > 0) {
      const size_t take = std::min(len, sizeof(block) - block_len);
      std::memcpy(block + block_len, data, take);
      block_len += take;
      data += take;
      len -= take;
      if (block_len == sizeof(block)) {
        ProcessBlock(block);
        block_len = 0;
      }
    }
  }
  void Final(uint8_t out[16]) {
    const uint64_t bit_len = total_bytes * 8;
    uint8_t pad = 0x80;
    Update(&pad, 1);
    uint8_t zero = 0;
    while (block_len != 56) {
      Update(&zero, 1);
    }
    for (int i = 0; i < 8; ++i) {
      const uint8_t byte = static_cast<uint8_t>(bit_len >> (8 * i));
      Update(&byte, 1);
    }
    const uint32_t words[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>(words[i]);
      out[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 8);
      out[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 16);
      out[i * 4 + 3] = static_cast<uint8_t>(words[i] >> 24);
    }
  }
};

// Read the XEX2 entry-point address from the (unencrypted) optional header.
// The market variant keeps a fixed entry point even across minor re-prints or
// re-dumps of the same region, so this identifies US vs EU when the MD5 does
// not match a known hash (modified bytes, different pressing, decrypted dump).
// Returns 0 when the file is not a readable XEX2.
uint32_t ReadXexEntryPoint(const std::filesystem::path& xex) {
  std::ifstream f(xex, std::ios::binary);
  if (!f) return 0;
  uint8_t hdr[0x18 + 16 * 8] = {};
  f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
  if (f.gcount() < 0x18) return 0;
  if (std::memcmp(hdr, "XEX2", 4) != 0) return 0;
  auto be32 = [](const uint8_t* p) -> uint32_t {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  };
  const uint32_t count = be32(hdr + 0x14);
  if (count > 16) return 0;  // the retail XEX has 14-15 optional headers
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = hdr + 0x18 + i * 8;
    if (be32(e) == 0x00010100) {  // XEX_HEADER_ENTRY_POINT
      return be32(e + 4);
    }
  }
  return 0;
}

}  // namespace

namespace {

// MD5 of a file as 32 uppercase hex characters. Empty string on failure.
std::string FileMd5Hex(const std::filesystem::path& file) {
  Md5Digest md5;
  std::ifstream f(file, std::ios::binary);
  if (!f) return {};
  uint8_t buf[65536];
  while (f) {
    f.read(reinterpret_cast<char*>(buf), sizeof(buf));
    const std::streamsize n = f.gcount();
    if (n > 0) {
      md5.Update(buf, static_cast<size_t>(n));
    }
  }
  uint8_t digest[16];
  md5.Final(digest);
  std::string hex;
  hex.reserve(32);
  for (size_t i = 0; i < sizeof(digest); ++i) {
    char tmp[3];
    snprintf(tmp, sizeof(tmp), "%02X", digest[i]);
    hex += tmp;
  }
  return hex;
}

// Retail executable sizes (bytes). Checked before hashing so a hunt over a
// whole disc dump never reads a 4.9MB file unless it is a plausible candidate.
constexpr uintmax_t kSizeDbz3 = 4890624;    // yae3_xenon.xex / yae3_xenon_eu.xex
constexpr uintmax_t kSizeDbz1 = 4464640;    // DBZ1's own executable (other port)
constexpr uintmax_t kSizeHdMenu = 3317760;  // HD Collection menu (disc root default.xex)

}  // namespace

XexStatus ClassifyXexFile(const std::filesystem::path& xex) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(xex, ec)) return XexStatus::kMissing;
  const uintmax_t size = std::filesystem::file_size(xex, ec);

  // Cache by (path, size, mtime) so the per-frame banner does not re-hash the
  // ~4.9MB executable every frame.
  static std::string cached_path;
  static uintmax_t cached_size = 0;
  static std::filesystem::file_time_type cached_mtime{};
  static XexStatus cached_status = XexStatus::kUnknown;
  const auto mtime = std::filesystem::last_write_time(xex, ec);
  if (cached_path == xex.string() && cached_size == size && cached_mtime == mtime) {
    return cached_status;
  }

  // The retail disc files are fixed: every US/NA copy hashes A53E..., every
  // EU/PAL copy C37E... (the raw bytes are encrypted with the region's key).
  // DBZ Budokai HD Collection (the DBZ1 sister project) ships the SAME
  // executable for both US and EU (5A6A..., 4464640 B). It is recognized here
  // so the launcher can tell the user "this is the DBZ1 game, use its launcher"
  // instead of letting the DBZ3 core crash on a foreign executable. The disc's
  // root default.xex is the HD Collection's own menu (3317760 B), which this
  // core cannot boot either (it is a different program, not Budokai 3).
  static constexpr char kUsMd5[] = "A53E324B5D2A65EBCBF648E4F85A7271";
  static constexpr char kEuMd5[] = "C37EB979B762DA0AB5B8C9BA8037CE4E";
  static constexpr char kDbz1Md5[] = "5A6AB28A4911851FCA955B5925CDFEBB";
  XexStatus status = XexStatus::kUnknown;
  if (size == kSizeDbz3 || size == kSizeDbz1 || size == kSizeHdMenu) {
    const std::string hex = FileMd5Hex(xex);
    if (hex == kUsMd5) {
      status = XexStatus::kUs;
    } else if (hex == kEuMd5) {
      status = XexStatus::kEu;
    } else if (hex == kDbz1Md5) {
      status = XexStatus::kDbz1;
    } else if (size == kSizeHdMenu) {
      status = XexStatus::kHdMenu;
    }
  }

  // Fallback for a modified/re-pressed/decrypted dump whose MD5 is unknown: the
  // US/NA and EU/PAL executables keep distinct XEX entry points, which survive
  // minor edits. Without this, a valid copy of the wrong variant would be left
  // as kUnknown and the dual core would silently pick the US image config (the
  // default branch of ResolveImageInfo), booting an EU exe with US code -> the
  // guest exits with "No function registered at <addr>".
  if (status == XexStatus::kUnknown && size != kSizeDbz1 && size != kSizeHdMenu) {
    const uint32_t entry = ReadXexEntryPoint(xex);
    if (entry == 0x8221DDB0u) {
      status = XexStatus::kUs;
    } else if (entry == 0x8221C570u) {
      status = XexStatus::kEu;
    } else if (entry == 0x820D54A8u || entry == 0x820D54C8u) {
      // Entry point observed for the HD Collection's menu: booting it with this
      // core reports "No function registered at 820D54A8/820D54C8" (it is the
      // disc's root default.xex, not Budokai 3).
      status = XexStatus::kHdMenu;
    }
  }

  cached_path = xex.string();
  cached_size = size;
  cached_mtime = mtime;
  cached_status = status;
  return status;
}

XexStatus CheckDefaultXex(const std::filesystem::path& root) {
  return ClassifyXexFile(root / "default.xex");
}

const char* XexStatusLabel(XexStatus status) {
  switch (status) {
    case XexStatus::kMissing: return "missing";
    case XexStatus::kUs: return "US/NA Budokai 3";
    case XexStatus::kEu: return "EU/PAL Budokai 3";
    case XexStatus::kUnknown: return "unrecognized executable";
    case XexStatus::kDbz1: return "DBZ1 executable";
    case XexStatus::kHdMenu: return "HD Collection menu";
  }
  return "unrecognized executable";
}

bool GameExecutable::usable() const { return XexIsExpected(status); }

namespace {

bool HasXexExtension(const std::filesystem::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == ".xex";
}

bool IsRegionFolderName(const std::filesystem::path& p) {
  const auto name = p.filename().string();
  return name == "us" || name == "eu";
}

// The asset folder that belongs to `xex`: the folder that directly contains
// us/ or eu/. On a retail disc that is DBZ3/ (right next to the executable); in
// other layouts it is often one level up (assets/) or one level down.
std::filesystem::path DataRootFor(const std::filesystem::path& xex) {
  std::error_code ec;
  const auto dir = xex.parent_path();
  if (std::filesystem::is_directory(dir / "us", ec) ||
      std::filesystem::is_directory(dir / "eu", ec)) {
    return dir;
  }
  const std::filesystem::path candidates[] = {
      dir.parent_path(),
      dir / "assets",
      dir.parent_path() / "assets",
      dir / "DBZ3",
      dir.parent_path() / "DBZ3",
  };
  for (const auto& cand : candidates) {
    if (cand.empty()) continue;
    if (std::filesystem::is_directory(cand / "us", ec) ||
        std::filesystem::is_directory(cand / "eu", ec)) {
      return cand;
    }
  }
  return dir;
}

// Directories that never hold the executable and can be enormous (the AFS asset
// folders alone are ~1.5GB), so the hunt never descends into them.
bool SkipDir(const std::filesystem::path& dir) {
  const auto name = dir.filename().string();
  static const char* kSkip[] = {"us",          "eu",        "mods",        "mods_archivo",
                                "_archivo_mods", "user_data", "logs",      "cache",
                                "iso_cache",   "xex_cache", "shaders",     "metadata",
                                "System Volume Information", "$RECYCLE.BIN"};
  for (const char* s : kSkip) {
    if (name == s) return true;
  }
  return false;
}

}  // namespace

GameExecutable FindGameExecutable(const std::vector<std::filesystem::path>& roots) {
  std::error_code ec;
  GameExecutable best;
  GameExecutable fallback;
  int visited_dirs = 0;
  static constexpr int kMaxDirs = 4000;

  auto record = [&](const std::filesystem::path& xex) -> bool {
    if (!HasXexExtension(xex)) return false;
    const XexStatus st = ClassifyXexFile(xex);
    if (st == XexStatus::kMissing) return false;
    if (XexIsExpected(st)) {
      if (best.xex.empty()) {
        best.xex = xex;
        best.status = st;
        best.data_root = DataRootFor(xex);
        best.found_hint = xex.filename().string();
      }
      return true;  // bootable: stop hunting
    }
    // Recognized but not bootable by this core (DBZ1 / the HD Collection menu /
    // an unknown build): remembered only to explain the failure to the user.
    if (fallback.xex.empty()) {
      fallback.xex = xex;
      fallback.status = st;
      fallback.data_root = DataRootFor(xex);
      fallback.found_hint = xex.filename().string();
    }
    return false;
  };

  // Conventional spots: the folder itself, DBZ3/ (retail disc), assets/ and
  // assets/DBZ3/ (extracted release layouts).
  static const char* kNames[] = {"default.xex", "yae3_xenon.xex", "yae3_xenon_eu.xex"};
  auto scan_flat = [&](const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    for (const char* n : kNames) {
      if (record(dir / n)) return true;
    }
    return false;
  };

  // Bounded recursive hunt (depth <= 3) for the executable under any other
  // name/location the user may have ended up with.
  std::function<bool(const std::filesystem::path&, int)> scan_deep =
      [&](const std::filesystem::path& dir, int depth) -> bool {
    if (depth > 3 || visited_dirs >= kMaxDirs) return false;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    ++visited_dirs;
    std::vector<std::filesystem::path> subdirs;
    for (auto [it_2, iec_2] = std::pair{std::filesystem::directory_iterator(dir, ec), std::error_code{}}; !iec_2 && it_2 != std::filesystem::directory_iterator(); it_2.increment(iec_2)) {
    const auto& entry = *it_2;
      if (entry.is_directory(ec)) {
        if (!SkipDir(entry.path())) subdirs.push_back(entry.path());
      } else if (entry.is_regular_file(ec)) {
        if (record(entry.path())) return true;
      }
    }
    for (const auto& sub : subdirs) {
      if (scan_deep(sub, depth + 1)) return true;
    }
    return false;
  };

  // The FIRST root is the folder the launcher actually points at (or the data
  // folder it resolved): that one gets the bounded recursive hunt, so a dump the
  // user dropped a level or two deeper is still found. The remaining roots are
  // only probed at the conventional spots - recursing over a parent folder could
  // otherwise pick up an unrelated copy of the game from elsewhere on the disk.
  for (size_t i = 0; i < roots.size(); ++i) {
    const auto& root = roots[i];
    if (root.empty() || !std::filesystem::is_directory(root, ec)) continue;
    REXLOG_INFO("FindGameExecutable: searching {}", root.string());
    if (scan_flat(root)) break;
    if (scan_flat(root / "DBZ3")) break;
    if (scan_flat(root / "assets")) break;
    if (scan_flat(root / "assets" / "DBZ3")) break;
    if (i == 0 && scan_deep(root, 0)) break;
  }

  if (!best.xex.empty()) {
    REXLOG_INFO("FindGameExecutable: found {} ({}) at {} -> data root {}",
                best.found_hint, XexStatusLabel(best.status), best.xex.string(),
                best.data_root.string());
    return best;
  }
  if (!fallback.xex.empty()) {
    REXLOG_WARN("FindGameExecutable: only found {} ({}) at {} - not bootable by this core",
                fallback.found_hint, XexStatusLabel(fallback.status),
                fallback.xex.string());
    return fallback;
  }
  REXLOG_INFO("FindGameExecutable: no executable found");
  return {};
}

std::filesystem::path EnsureXexCache(const std::filesystem::path& exe,
                                     const std::filesystem::path& cache_dir) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(exe, ec)) {
    return {};
  }
  const auto dst = cache_dir / "default.xex";
  const auto stamp = cache_dir / "source.stamp";

  // Identity of the source executable: absolute path + size + mtime. Only the
  // executable is ever copied (few MB): the AFS assets stay where they are.
  const auto exe_abs = std::filesystem::absolute(exe, ec).string();
  const auto exe_size = std::filesystem::file_size(exe, ec);
  const auto exe_mtime = static_cast<long long>(
      std::filesystem::last_write_time(exe, ec).time_since_epoch().count());
  const std::string want = exe_abs + "\n" + std::to_string(exe_size) + "\n" +
                           std::to_string(exe_mtime) + "\n";

  std::string have;
  if (std::filesystem::is_regular_file(dst, ec) &&
      std::filesystem::is_regular_file(stamp, ec)) {
    std::ifstream in(stamp, std::ios::binary);
    if (in) {
      have.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
  }
  if (have != want) {
    std::filesystem::create_directories(cache_dir, ec);
    REXLOG_INFO("EnsureXexCache: copying {} -> {}", exe.string(), dst.string());
    std::filesystem::copy_file(exe, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      REXLOG_ERROR("EnsureXexCache: copy failed for {}: {}", exe.string(), ec.message());
      return {};
    }
    std::ofstream out(stamp, std::ios::binary | std::ios::trunc);
    if (out) {
      out << want;
    }
  }
  return std::filesystem::is_regular_file(dst, ec) ? dst : std::filesystem::path{};
}

namespace {

// Read `path` (guest path inside the disc image, e.g. "default.xex") from the
// ISO via the SDK's GDFX reader. Returns the file bytes on success.
bool ReadFileFromIso(const std::filesystem::path& iso, const char* path,
                     std::vector<uint8_t>& out) {
  rex::filesystem::DiscImageDevice device("\\Device\\IsoProbe", iso);
  if (!device.Initialize()) {
    return false;
  }
  rex::filesystem::Entry* entry = device.ResolvePath(path);
  if (!entry) {
    return false;
  }
  rex::filesystem::File* file = nullptr;
  if (XFAILED(entry->Open(rex::filesystem::FileAccess::kGenericRead, &file)) || !file) {
    return false;
  }
  const size_t size = entry->size();
  out.resize(size);
  size_t got = 0;
  const auto status = file->ReadSync({out.data(), size}, 0, &got);
  delete file;
  return XSUCCEEDED(status) && got == size;
}

}  // namespace

namespace {

bool WriteBytesToFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  f.close();
  return f.good();
}

}  // namespace

bool ExtractGameXexFromIso(const std::filesystem::path& iso,
                           const std::filesystem::path& dst,
                           std::string* out_source) {
  if (out_source) out_source->clear();
  // A repacked image keeps Budokai 3 as the root default.xex, while a RETAIL HD
  // Collection disc keeps it at DBZ3/yae3_xenon.xex (its root default.xex is the
  // collection's own menu, which this core cannot boot). Try the known spots and
  // keep the first that really is this port's executable.
  static const char* kCandidates[] = {
      "default.xex",
      "DBZ3/yae3_xenon.xex",
      "DBZ3/yae3_xenon_eu.xex",
      "yae3_xenon.xex",
      "yae3_xenon_eu.xex",
      "DBZ3/default.xex",
  };

  // Each candidate is classified from its OWN temporary file: the classifier
  // caches by path+size+mtime, and rewriting the same path could be mistaken for
  // the previously classified file.
  const auto scratch = dst.parent_path() / "candidates";
  std::filesystem::path last_bytes_path;
  std::string last_source;

  for (const char* cand : kCandidates) {
    std::vector<uint8_t> bytes;
    if (!ReadFileFromIso(iso, cand, bytes) || bytes.empty()) continue;
    const auto temp = scratch / std::filesystem::path(cand).filename();
    if (!WriteBytesToFile(temp, bytes)) continue;
    last_bytes_path = temp;
    last_source = cand;

    const auto status = ClassifyXexFile(temp);
    if (XexIsExpected(status)) {
      std::error_code ec;
      std::filesystem::copy_file(temp, dst, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        REXLOG_ERROR("ExtractGameXexFromIso: could not stage {}: {}", cand, ec.message());
        return false;
      }
      if (out_source) *out_source = cand;
      REXLOG_INFO("ExtractGameXexFromIso: using '{}' ({})", cand, XexStatusLabel(status));
      return true;
    }
    REXLOG_INFO("ExtractGameXexFromIso: '{}' is {} - not bootable, trying next", cand,
                XexStatusLabel(status));
  }

  // Nothing bootable: keep the last readable candidate so the launcher can still
  // tell the user what the disc contains (the HD Collection menu, DBZ1, ...).
  if (!last_bytes_path.empty()) {
    std::error_code ec;
    std::filesystem::copy_file(last_bytes_path, dst,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (out_source) *out_source = last_source;
    REXLOG_WARN("ExtractGameXexFromIso: no bootable Budokai 3 executable in {} (kept '{}')",
                iso.string(), last_source);
  }
  return false;
}

bool ExtractDefaultXexFromIso(const std::filesystem::path& iso,
                              const std::filesystem::path& dst) {
  return ExtractGameXexFromIso(iso, dst, nullptr);
}

std::string IsoXexSourcePath() {
  const auto stamp = UserDataRoot() / "iso_cache" / "source.stamp";
  std::ifstream in(stamp, std::ios::binary);
  if (!in) return {};
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    last = line;
  }
  return last;
}

bool EnsureIsoXexCache(const std::filesystem::path& iso,
                       const std::filesystem::path& cache_dir) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(iso, ec)) {
    return false;
  }
  const auto xex_dst = cache_dir / "default.xex";
  const auto stamp = cache_dir / "source.stamp";

  // Identity of the disc the cache must match: absolute path + size + mtime.
  const auto iso_abs = std::filesystem::absolute(iso, ec).string();
  const auto iso_size = std::filesystem::file_size(iso, ec);
  const auto iso_mtime =
      static_cast<long long>(std::filesystem::last_write_time(iso, ec).time_since_epoch().count());
  const std::string identity = iso_abs + "\n" + std::to_string(iso_size) + "\n" +
                               std::to_string(iso_mtime) + "\n";

  // The stamp's 4th line records which file inside the image provided the
  // executable (e.g. "DBZ3/yae3_xenon.xex" for a retail disc). A stamp written
  // by an older build has only three lines and no source: re-extract once so the
  // retail layout (and the DBZ3 prefix it needs) is picked up.
  std::string have_raw;
  if (std::filesystem::is_regular_file(xex_dst, ec) &&
      std::filesystem::is_regular_file(stamp, ec)) {
    std::ifstream in(stamp, std::ios::binary);
    if (in) {
      have_raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
  }
  std::vector<std::string> have;
  {
    size_t start = 0;
    while (start <= have_raw.size()) {
      const size_t pos = have_raw.find('\n', start);
      if (pos == std::string::npos) {
        if (start < have_raw.size()) have.push_back(have_raw.substr(start));
        break;
      }
      have.push_back(have_raw.substr(start, pos - start));
      start = pos + 1;
    }
  }
  const bool have_identity =
      have.size() >= 3 && (have[0] + "\n" + have[1] + "\n" + have[2] + "\n") == identity;
  const bool have_source = have.size() >= 4 && !have[3].empty();

  if (!have_identity || !have_source) {
    // The cached xex belongs to another disc (or is missing): re-extract so the
    // region detection matches the executable the runtime will actually run.
    REXLOG_INFO("EnsureIsoXexCache: (re)extracting the executable from {}", iso.string());
    std::string source;
    const bool bootable = ExtractGameXexFromIso(iso, xex_dst, &source);
    // A disc without a bootable executable still gets its cache written so the
    // launcher can report what it is (HD Collection menu / DBZ1 / unknown).
    if (std::filesystem::is_regular_file(xex_dst, ec)) {
      std::ofstream out(stamp, std::ios::binary | std::ios::trunc);
      if (out) {
        out << identity << source << "\n";
      }
    }
    (void)bootable;
  }
  return std::filesystem::is_regular_file(xex_dst, ec);
}

// --- ISO + mods: copia de los .afs (ver settings.h) ---------------------------------------------------------------
std::filesystem::path IsoDataCacheDir() { return UserDataRoot() / "iso_datos"; }

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}

// Identidad de la ISO actual (ruta + tamano + fecha), como el sello del xex.
std::string IsoIdentity() {
  std::error_code ec;
  const std::filesystem::path iso = std::filesystem::u8path(IsoPath());
  if (!std::filesystem::is_regular_file(iso, ec)) return {};
  const auto size = std::filesystem::file_size(iso, ec);
  const auto mtime =
      static_cast<long long>(std::filesystem::last_write_time(iso, ec).time_since_epoch().count());
  return rex::path_to_utf8(std::filesystem::absolute(iso, ec)) + "\n" + std::to_string(size) + "\n" +
         std::to_string(mtime) + "\n";
}

// Archivos que nombran los mods activos: mods/<mod>/<x> y mods/<mod>/us|eu/<x> (mismas reglas que el runtime:
// fuera "<mod>.disabled", el marcador ".disabled" y lo que empieza por '.').
std::set<std::string> ModTouchedNames() {
  std::set<std::string> out;
  std::error_code ec;
  const std::filesystem::path root = rex::filesystem::GetExecutableFolder() / "mods";
  for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator();
       it.increment(ec)) {
    const std::string mod = Lower(rex::path_to_utf8(it->path().filename()));
    std::error_code fec;
    if (!it->is_directory(fec) || mod.empty() || mod[0] == '.' ||
        (mod.size() > 9 && mod.compare(mod.size() - 9, 9, ".disabled") == 0) ||
        std::filesystem::exists(it->path() / ".disabled", fec)) {
      continue;
    }
    for (const std::filesystem::path& dir : {it->path(), it->path() / "us", it->path() / "eu"}) {
      for (auto f = std::filesystem::directory_iterator(dir, fec); !fec && f != std::filesystem::directory_iterator();
           f.increment(fec)) {
        out.insert(Lower(rex::path_to_utf8(f->path().filename())));
      }
    }
  }
  return out;
}

struct IsoCopy {
  rex::filesystem::Entry* entry;
  std::filesystem::path rel;   // dentro de IsoDataCacheDir(): <region>/<archivo> o <archivo>
};

// Lo que se copia: los .afs (menos las voces adx_*, 650 MB cada una, salvo `voices`: el montaje de personajes nuevos
// lee sus tablas) y lo que nombre un mod activo, de la raiz de Budokai 3 (DBZ3\ en el disco original) y de la carpeta de la region elegida.
void IsoCopyList(rex::filesystem::DiscImageDevice& dev, std::vector<IsoCopy>& out, bool voices) {
  rex::filesystem::Entry* root = dev.ResolvePath("");
  if (!root) return;
  if (auto* d = root->GetChild("DBZ3"); d && (d->GetChild("us") || d->GetChild("eu"))) root = d;
  const std::string region = Region() == "eu" ? "eu" : "us";
  const std::set<std::string> names = ModTouchedNames();
  auto pick = [&](rex::filesystem::Entry* dir, const std::filesystem::path& rel) {
    for (const auto& c : dir->children()) {
      if (c->attributes() & rex::filesystem::kFileAttributeDirectory) continue;
      const std::string n = Lower(c->name());
      const bool afs = n.size() > 4 && n.compare(n.size() - 4, 4, ".afs") == 0 && (voices || n.rfind("adx_", 0) != 0);
      if (afs || names.count(n)) out.push_back({c.get(), rel / std::filesystem::u8path(c->name())});
    }
  };
  pick(root, {});
  if (auto* r = root->GetChild(region)) pick(r, region);
}

}  // namespace

bool IsoDataCacheValid() {
  const std::string id = IsoIdentity();
  if (id.empty()) return false;
  std::ifstream in(IsoDataCacheDir() / "iso.stamp", std::ios::binary);
  const std::string have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return have == id;
}

long long IsoDataPending(bool voices) {
  rex::filesystem::DiscImageDevice dev("\\Device\\IsoDatos", std::filesystem::u8path(IsoPath()));
  if (!dev.Initialize()) return -1;
  std::vector<IsoCopy> list;
  IsoCopyList(dev, list, voices);
  const bool valid = IsoDataCacheValid();
  long long pending = 0;
  std::error_code ec;
  for (const IsoCopy& c : list) {
    const auto p = IsoDataCacheDir() / c.rel;
    if (!valid || !std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) != c.entry->size()) {
      pending += (long long)c.entry->size();
    }
  }
  return pending;
}

bool PrepareIsoData(bool voices, std::atomic<int>* progress, std::string* error) {
  auto fail = [&](std::string msg) {
    REXLOG_WARN("dbz3: ISO + mods: {}", msg);
    if (error) *error = std::move(msg);
    return false;
  };
  const std::filesystem::path iso = std::filesystem::u8path(IsoPath());
  rex::filesystem::DiscImageDevice dev("\\Device\\IsoDatos", iso);
  if (!dev.Initialize()) return fail(i18n::T("no se pudo leer la ISO", "the ISO could not be read"));
  std::vector<IsoCopy> list;
  IsoCopyList(dev, list, voices);
  if (list.empty()) return fail(i18n::T("la ISO no tiene los datos de Budokai 3", "the ISO has no Budokai 3 data"));
  const std::filesystem::path dir = IsoDataCacheDir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const bool valid = IsoDataCacheValid();
  std::vector<const IsoCopy*> todo;
  unsigned long long total = 0, done = 0;
  for (const IsoCopy& c : list) {
    const auto p = dir / c.rel;
    if (!valid || !std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) != c.entry->size()) {
      todo.push_back(&c);
      total += c.entry->size();
    }
  }
  if (todo.empty()) return true;
  const auto space = std::filesystem::space(dir, ec);
  if (!ec && space.available < total + (64ull << 20)) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), i18n::T("falta espacio: hacen falta %.1f GB libres en el disco del juego",
                                            "not enough space: %.1f GB free are needed on the game's drive"),
                  double(total) / (1024.0 * 1024.0 * 1024.0));
    return fail(buf);
  }
  // mientras se copia, el sello no vale (otra ISO a medias no puede pasar por la de antes)
  { std::ofstream(dir / "iso.stamp", std::ios::binary | std::ios::trunc); }
  REXLOG_INFO("dbz3: ISO + mods: copiando {} archivos ({} MB) a {}", todo.size(), total >> 20, rex::path_to_utf8(dir));
  std::vector<uint8_t> buf(8u << 20);
  for (const IsoCopy* c : todo) {
    const auto dst = dir / c->rel;
    auto part = dst;
    part += ".part";
    std::filesystem::create_directories(dst.parent_path(), ec);
    rex::filesystem::File* file = nullptr;
    if (XFAILED(c->entry->Open(rex::filesystem::FileAccess::kGenericRead, &file)) || !file) {
      return fail(std::string(i18n::T("no se pudo abrir ", "could not open ")) + rex::path_to_utf8(c->rel));
    }
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    bool ok = bool(out);
    for (size_t off = 0, size = c->entry->size(); ok && off < size;) {
      size_t got = 0;
      const size_t n = std::min(buf.size(), size - off);
      ok = XSUCCEEDED(file->ReadSync({buf.data(), n}, off, &got)) && got == n;
      if (ok) out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(n));
      ok = ok && out.good();
      off += n;
      done += n;
      if (progress && total) progress->store(int(done * 1000 / total));
    }
    delete file;
    out.close();
    if (!ok || !out.good()) {
      std::filesystem::remove(part, ec);
      return fail(std::string(i18n::T("no se pudo copiar ", "could not copy ")) + rex::path_to_utf8(c->rel) +
                  i18n::T(" (revisa el espacio libre)", " (check the free space)"));
    }
    std::filesystem::rename(part, dst, ec);   // reemplaza la copia vieja de esta misma cache
    if (ec) return fail(std::string(i18n::T("no se pudo guardar ", "could not save ")) + rex::path_to_utf8(c->rel));
  }
  std::ofstream(dir / "iso.stamp", std::ios::binary | std::ios::trunc) << IsoIdentity();
  if (progress) progress->store(1000);
  REXLOG_INFO("dbz3: ISO + mods: listo ({} MB)", total >> 20);
  return true;
}

std::filesystem::path XexCacheDir() {
  // Lives with the rest of the user data (portable unless the exe folder is not
  // writable, see UserDataRoot).
  return UserDataRoot() / "xex_cache";
}

bool BootSource::usable() const { return XexIsExpected(status) && !xex.empty(); }

namespace {
BootSource g_boot_source;
}  // namespace

const BootSource& CurrentBootSource() { return g_boot_source; }
void SetCurrentBootSource(const BootSource& source) { g_boot_source = source; }

BootSource ResolveBootSource(const std::filesystem::path& data_root,
                             const std::vector<std::filesystem::path>& extra_roots,
                             const std::filesystem::path& cache_dir) {
  BootSource src;
  src.data_root = data_root;

  // 1) The canonical spot: <data_root>/default.xex. When it already is a real
  //    Budokai 3 executable nothing has to be staged and the game folder is
  //    served as-is (the documented layout).
  const auto canonical = data_root / "default.xex";
  const XexStatus canonical_status = ClassifyXexFile(canonical);
  if (XexIsExpected(canonical_status)) {
    src.xex = canonical;
    src.status = canonical_status;
    src.note = std::string(XexStatusLabel(canonical_status)) + " (default.xex)";
    REXLOG_INFO("ResolveBootSource: canonical {} -> {}", canonical.string(), src.note);
    return src;
  }
  REXLOG_INFO("ResolveBootSource: {} is {} - hunting for the real executable",
              canonical.string(), XexStatusLabel(canonical_status));

  // 2) Hunt for the real executable around the data folder: the retail disc
  //    keeps it as DBZ3/yae3_xenon.xex, dumps often as yae3_xenon.xex, and the
  //    user may have nested it somewhere else.
  // The data folder goes first: it gets the recursive hunt (see
  // FindGameExecutable), the extra roots are only probed at the usual spots.
  std::vector<std::filesystem::path> roots;
  roots.push_back(data_root);
  for (const auto& extra : extra_roots) {
    roots.push_back(extra);
  }
  const GameExecutable found = FindGameExecutable(roots);
  if (found.usable()) {
    // The drive can serve it directly when it is already named default.xex at
    // the mount root.
    if (found.xex == canonical) {
      src.xex = canonical;
      src.status = found.status;
      src.note = std::string(XexStatusLabel(found.status)) + " (default.xex)";
      return src;
    }
    const auto staged = EnsureXexCache(found.xex, cache_dir);
    if (!staged.empty()) {
      src.xex = staged;
      src.status = found.status;
      src.redirect_default_xex = true;
      src.note = found.found_hint + " -> default.xex";
      if (!found.data_root.empty() && std::filesystem::is_directory(found.data_root)) {
        src.data_root = found.data_root;
      }
      REXLOG_INFO("ResolveBootSource: {} ({}) staged at {}; game data root {}",
                  found.xex.string(), XexStatusLabel(found.status), staged.string(),
                  src.data_root.string());
      return src;
    }
    src.status = found.status;
    src.note = "found " + found.found_hint + " but could not stage it";
    REXLOG_ERROR("ResolveBootSource: {}", src.note);
    return src;
  }

  // 3) Nothing bootable anywhere: keep whatever is at the canonical spot so the
  //    launcher can tell the user exactly what it is (DBZ1 / HD menu / unknown).
  src.xex = canonical;
  src.status = canonical_status;
  src.note = XexStatusLabel(canonical_status);
  REXLOG_WARN("ResolveBootSource: no bootable Budokai 3 executable found (canonical: {})",
              src.note);
  return src;
}

std::filesystem::path LatestLogPath() {
  const auto logs_dir = rex::filesystem::GetExecutableFolder() / "logs";
  if (!std::filesystem::is_directory(logs_dir)) return {};
  std::filesystem::path newest;
  std::filesystem::file_time_type newest_time{};
  std::error_code ec;
  for (auto [it_3, iec_3] = std::pair{std::filesystem::directory_iterator(logs_dir, ec), std::error_code{}}; !iec_3 && it_3 != std::filesystem::directory_iterator(); it_3.increment(iec_3)) {
    const auto& entry = *it_3;
    std::error_code fec;
    if (!entry.is_regular_file(fec)) continue;
    if (entry.path().extension() != ".log") continue;
    const auto t = entry.last_write_time(fec);
    if (fec) continue;
    if (newest.empty() || t > newest_time) {
      newest = entry.path();
      newest_time = t;
    }
  }
  return newest;
}

// v1.4.1: DRED (diagnostico de "device removed") deja de ir siempre encendido: sus
// breadcrumbs y el seguimiento de page faults tienen coste en cada lista de comandos
// y en cada recurso creado, y desde que se activo por defecto (v1.3.0) llegaron los
// reportes de bajones en equipos potentes. Ahora se arma solo para la sesion
// siguiente a un fallo de la GPU (la sesion anterior registro "D3D12 device
// removed"), que es justo cuando hace falta el informe. Un `d3d12_dred = true`
// puesto a mano en el toml se respeta.
static bool PreviousSessionLostGpu() {
  const auto logs_dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  if (!std::filesystem::is_directory(logs_dir, ec)) return false;
  // La sesion actual ya ha creado su log (el mas reciente): mirar el anterior.
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> logs;
  for (auto [it_4, iec_4] = std::pair{std::filesystem::directory_iterator(logs_dir, ec), std::error_code{}}; !iec_4 && it_4 != std::filesystem::directory_iterator(); it_4.increment(iec_4)) {
    const auto& entry = *it_4;
    if (entry.is_regular_file(ec) && entry.path().extension() == ".log") {
      logs.emplace_back(entry.last_write_time(ec), entry.path());
    }
  }
  if (logs.size() < 2) return false;
  std::sort(logs.begin(), logs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::ifstream in(logs[1].second, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  const std::streamoff tail = std::min<std::streamoff>(size, 256 * 1024);
  in.seekg(size - tail);
  std::string text(static_cast<size_t>(tail), '\0');
  in.read(text.data(), tail);
  return text.find("D3D12 device removed:") != std::string::npos;
}

void ArmGpuCrashDiagnostics() {
  if (rex::cvar::HasNonDefaultValue("d3d12_dred")) return;
  if (PreviousSessionLostGpu()) {
    rex::cvar::SetFlagByName("d3d12_dred", "true");
    REXLOG_INFO("dbz3: la sesion anterior perdio la GPU (device removed): DRED activado para esta "
                "sesion");
  }
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

bool Native2xMsaa() { return REXCVAR_GET(dbz3_native_2x_msaa); }
void SetNative2xMsaa(bool enabled) { REXCVAR_SET(dbz3_native_2x_msaa, enabled); }
int32_t AnisotropicOverride() { return REXCVAR_GET(dbz3_anisotropic); }

void SetAnisotropicOverride(int32_t level) { REXCVAR_SET(dbz3_anisotropic, level); }

int32_t HdTextures() { return REXCVAR_GET(dbz3_hd_textures); }

void SetHdTextures(int32_t factor) { REXCVAR_SET(dbz3_hd_textures, factor); }

int32_t HdTextureMaxTexels() { return REXCVAR_GET(dbz3_hd_texture_max_texels); }

void SetHdTextureMaxTexels(int32_t texels) {
  REXCVAR_SET(dbz3_hd_texture_max_texels, texels);
}

std::string PresentEffect() { return REXCVAR_GET(dbz3_present_effect); }
void SetPresentEffect(const std::string& effect) { REXCVAR_SET(dbz3_present_effect, effect); }

std::string FsrQualityMode() { return REXCVAR_GET(dbz3_fsr_quality); }
std::string FsrRender() { return REXCVAR_GET(dbz3_fsr_render); }
void SetFsrRender(const std::string& mode) { REXCVAR_SET(dbz3_fsr_render, mode); }
void SetFsrQualityMode(const std::string& mode) { REXCVAR_SET(dbz3_fsr_quality, mode); }

double FsrSharpness() { return REXCVAR_GET(dbz3_fsr_sharpness); }
void SetFsrSharpness(double sharpness) { REXCVAR_SET(dbz3_fsr_sharpness, sharpness); }

double CasSharpness() { return REXCVAR_GET(dbz3_cas_sharpness); }
void SetCasSharpness(double sharpness) { REXCVAR_SET(dbz3_cas_sharpness, sharpness); }

std::string Fxaa() { return REXCVAR_GET(dbz3_fxaa); }
void SetFxaa(const std::string& mode) { REXCVAR_SET(dbz3_fxaa, mode); }

double HdRimLight() { return REXCVAR_GET(dbz3_hd_rim_light); }
void SetHdRimLight(double strength) { REXCVAR_SET(dbz3_hd_rim_light, std::clamp(strength, 0.0, 1.0)); }
int32_t Relight() { return std::clamp(REXCVAR_GET(dbz3_relight), 0, 2); }
void SetRelight(int32_t level) { REXCVAR_SET(dbz3_relight, std::clamp(level, 0, 2)); }
bool PresentDither() { return REXCVAR_GET(dbz3_present_dither); }
void SetPresentDither(bool enabled) { REXCVAR_SET(dbz3_present_dither, enabled); }

// Wide/ultrawide presentation: "keep" (bars, no distortion) or "stretch"
// (fill the window, distorted). Drives the SDK's present_letterbox.
std::string AspectMode() { return REXCVAR_GET(dbz3_aspect); }
void SetAspectMode(const std::string& mode) { REXCVAR_SET(dbz3_aspect, mode); }

// ---------------------------------------------------------------------------
// Quality presets + GPU detection
// ---------------------------------------------------------------------------

namespace {

// Enumerate the primary (first non-software) DXGI adapter and return its
// description. Cached after the first call. Windows-only: on other platforms
// the launcher has no DXGI and reports an unknown GPU (tier 1/medium).
#if REX_PLATFORM_WIN32
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
  // Pick the best non-software adapter. Iterating all of them and keeping the
  // one with the most dedicated video memory handles hybrid/Optimus laptops:
  // the discrete GPU almost always has far more dedicated VRAM than the iGPU,
  // so the iGPU is NOT selected just because it enumerates first.
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  SIZE_T best_vram = 0;
  bool found = false;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      const std::wstring name(desc.Description);
      const bool is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                               name.find(L"Microsoft Basic Render Driver") != std::wstring::npos;
      if (!is_software && desc.DedicatedVideoMemory > best_vram) {
        best_vram = desc.DedicatedVideoMemory;
        cached = desc;
        found = true;
      }
    }
    adapter.Reset();
  }
  if (found) {
    cached_initialized = true;
    if (desc_out) *desc_out = cached;
    return true;
  }
  return false;
}

// 1.4.3 EX: true si algun adaptador hardware crea un dispositivo D3D12 de
// nivel 11_0 (lo mismo que exige el runtime). Si no (GPU muy antigua, driver
// roto, maquina virtual, escritorio remoto), el juego arranca con Vulkan en
// vez de cerrarse con "Unable to initialize Direct3D 12". Se mide una vez.
bool D3D12Usable() {
  static int cached = -1;
  if (cached >= 0) {
    return cached == 1;
  }
  cached = 0;
  // Clave de todas las graficas de verdad, con su driver: una actualizacion, una grafica nueva o una que
  // desaparece (portatil hibrido) vuelven a probar.
  std::string key;
  {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        LARGE_INTEGER umd{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
          char buf[96];
          std::snprintf(buf, sizeof(buf), "%04x:%04x:%08x:%02x:%llx;", desc.VendorId, desc.DeviceId, desc.SubSysId,
                        desc.Revision, static_cast<unsigned long long>(umd.QuadPart));
          key += buf;
        }
        adapter.Reset();
      }
    }
  }
  if (!key.empty() && REXCVAR_GET(dbz3_d3d12_ok) == key) {
    cached = 1;
    return true;
  }
  HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  if (!d3d12) {
    return false;
  }
  using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, int, REFIID, void**);
  auto create_device =
      reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d12, "D3D12CreateDevice"));
  // IID de ID3D12Device (d3d12.h); con ppDevice nulo solo se comprueba.
  static const GUID kIidD3D12Device = {
      0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (create_device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; cached == 0 && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i) {
      DXGI_ADAPTER_DESC1 desc{};
      if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
          SUCCEEDED(create_device(adapter.Get(), 0xb000 /* D3D_FEATURE_LEVEL_11_0 */,
                                  kIidD3D12Device, nullptr))) {
        cached = 1;
      }
      adapter.Reset();
    }
  }
  FreeLibrary(d3d12);
  if (cached == 1 && !key.empty()) REXCVAR_SET(dbz3_d3d12_ok, key);   // se guarda con los demas ajustes
  return cached == 1;
}
#endif  // REX_PLATFORM_WIN32

}  // namespace

std::string DetectGpuName() {
#if REX_PLATFORM_WIN32
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
#else
  // No DXGI on non-Windows platforms: report an unknown GPU (the quality preset
  // "auto" then defaults to medium tier).
  return {};
#endif
}

int32_t DetectGpuTier() {
#if REX_PLATFORM_WIN32
  DXGI_ADAPTER_DESC1 desc{};
  if (!GetPrimaryGpu(&desc)) {
    return 1;  // unknown -> medium
  }
  const std::wstring name(desc.Description);
  const size_t vram_mb = desc.DedicatedVideoMemory / (1024 * 1024);
  const bool is_intel = name.find(L"Intel") != std::wstring::npos;
  const bool is_arc = name.find(L"Arc") != std::wstring::npos;
  // 1.4.3 EX: las APU de AMD (Radeon 680M/780M/890M, Vega 8, "AMD Radeon(TM)
  // Graphics") son integradas aunque la BIOS les reserve 2-4 GB "dedicados":
  // antes salian como "alta" (MSAA) y daban tirones. Las dedicadas llevan "RX",
  // "Pro", "FirePro" o "Instinct" en el nombre.
  const bool is_amd = desc.VendorId == 0x1002;
  const bool is_amd_apu = is_amd && name.find(L"RX") == std::wstring::npos &&
                          name.find(L"Pro") == std::wstring::npos &&
                          name.find(L"Instinct") == std::wstring::npos;
  if ((is_intel && !is_arc) || is_amd_apu) {
    // Integrated: never auto-assign the top tier. Modern iGPUs with
    // plenty of shared memory still get "medium"; old ones get "low".
    return vram_mb >= 2048 ? 1 : 0;
  }
  // 1.4.3 EX: "alta" (MSAA 2x) desde 6 GB; las de 3-4 GB (RX 570/580 4 GB,
  // GTX 1650, GTX 970) van a "media", que es fluida en todas ellas.
  if (vram_mb >= 6000) {
    return 2;  // high
  }
  if (vram_mb >= 1536) {
    return 1;  // medium
  }
  return 0;  // low
#else
  // No DXGI on non-Windows platforms: unknown GPU -> medium tier.
  return 1;
#endif
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

// Recommended quality values for a tier. Everything tops out at internal 1x
// (plus MSAA where affordable): raising the internal scale above 1x multiplies
// the GPU load for a marginal visual gain, so it is left as an explicit manual
// choice, never a preset.
struct QualityConfig {
  int32_t scale;
  bool msaa;
  int32_t aniso;
  const char* effect;
};

QualityConfig QualityConfigForTier(int32_t tier) {
  switch (tier) {
    case 0:
      // Modest GPU: keep it native, no extras.
      return {1, false, 0, "bilinear"};
    case 1:
      // Mid GPU: FSR + 4x aniso, no MSAA.
      return {1, false, 3, "fsr"};
    default:
      // Good GPU: FSR + MSAA + 16x aniso.
      return {1, true, 5, "fsr"};
  }
}

QualityConfig QualityConfigForPreset(const std::string& preset) {
  // Old preset names are kept as aliases so a toml written by an earlier build
  // still resolves to a sensible profile after the rename.
  if (preset == "performance" || preset == "low") {
    // Max FPS, minimal cost: native resolution, no filtering, no MSAA.
    return {1, false, 0, "bilinear"};
  }
  if (preset == "balanced" || preset == "medium") {
    // Good image with little cost: FSR upscale + 4x aniso, no MSAA.
    return {1, false, 3, "fsr"};
  }
  // "quality" (default), plus legacy "high"/"ultra": FSR + MSAA + 16x aniso at
  // native internal res. Legacy "ultra" asked for 2x supersampling, but that is
  // the exact case that multiplies GPU load for little gain, so it is folded
  // into the same native-resolution profile (users who want it can raise the
  // internal scale manually).
  return {1, true, 5, "fsr"};
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

std::string QualityPreset() {
  const auto value = REXCVAR_GET(dbz3_quality_preset);
  return value == "unset" ? "auto" : value;
}

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
    if (REXCVAR_GET(dbz3_quality_preset) == "unset") REXCVAR_SET(dbz3_quality_preset, "auto");
    ApplyQualityPreset();
  }
}

double MasterVolume() { return REXCVAR_GET(dbz3_master_volume); }
void SetMasterVolume(double v) { REXCVAR_SET(dbz3_master_volume, v); }
bool AudioMute() { return REXCVAR_GET(dbz3_mute); }
void SetAudioMute(bool mute) { REXCVAR_SET(dbz3_mute, mute); }
bool UpdateCheckEnabled() { return REXCVAR_GET(dbz3_update_check); }
void SetUpdateCheckEnabled(bool enabled) { REXCVAR_SET(dbz3_update_check, enabled); }

double Deadzone() { return REXCVAR_GET(dbz3_deadzone); }
void SetDeadzone(double v) { REXCVAR_SET(dbz3_deadzone, v); }

bool RumbleEnabled() { return REXCVAR_GET(dbz3_rumble); }
void SetRumbleEnabled(bool enabled) { REXCVAR_SET(dbz3_rumble, enabled); }

std::string InputBackend() { return REXCVAR_GET(dbz3_input_backend); }
void SetInputBackend(const std::string& backend) { REXCVAR_SET(dbz3_input_backend, backend); }

std::string InputGlyphs() { return REXCVAR_GET(dbz3_input_glyphs); }
void SetInputGlyphs(const std::string& glyph_set) { REXCVAR_SET(dbz3_input_glyphs, glyph_set); }

bool MnkMode() { return REXCVAR_GET(dbz3_mnk_mode); }
void SetMnkMode(bool enabled) { REXCVAR_SET(dbz3_mnk_mode, enabled); }

bool MnkMouse() { return REXCVAR_GET(dbz3_mnk_mouse); }
void SetMnkMouse(bool enabled) { REXCVAR_SET(dbz3_mnk_mouse, enabled); }

double MnkSensitivity() { return REXCVAR_GET(dbz3_mnk_sensitivity); }
void SetMnkSensitivity(double v) { REXCVAR_SET(dbz3_mnk_sensitivity, v); }

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

bool AsyncShaderCompilation() { return REXCVAR_GET(dbz3_async_shaders); }
void SetAsyncShaderCompilation(bool enabled) { REXCVAR_SET(dbz3_async_shaders, enabled); }

bool OcclusionQueries() { return REXCVAR_GET(dbz3_occlusion_queries); }
void SetOcclusionQueries(bool enabled) { REXCVAR_SET(dbz3_occlusion_queries, enabled); }

// AFS I/O diagnostics live in the runtime (rexruntime.dll, src/filesystem/afs.cpp)
// and are read by the host read path; set through the SDK registry by name.
bool IoLogging() { return GetSdkBool("dbz3_io_logging"); }
void SetIoLogging(bool enabled) { SetSdkBool("dbz3_io_logging", enabled); }

// Performance diagnostics live in the GPU runtime (rexgpu-xenos.dll,
// src/ui/d3d12/d3d12_presenter.cpp); set through the SDK registry by name.
bool PerfLogging() { return GetSdkBool("dbz3_perf_logging"); }
void SetPerfLogging(bool enabled) { SetSdkBool("dbz3_perf_logging", enabled); }

bool IoReadahead() { return GetSdkBool("dbz3_io_readahead"); }
void SetIoReadahead(bool enabled) { SetSdkBool("dbz3_io_readahead", enabled); }

// Dev texture dump (runtime `dbz3_texture_dump`, a directory path): writes each
// unique guest texture as a DDS + index.jsonl so texture packs can be authored.
// Enabled while the path is non-empty. The dump can be huge (hundreds of MB),
// so it lives OUTSIDE the project/build tree: a user-chosen folder (persisted
// via `dbz3_texture_dump_dir`, empty = off) rather than under user_data.
bool TextureDumpEnabled() { return !REXCVAR_GET(dbz3_texture_dump).empty(); }
std::string TextureDumpDir() { return REXCVAR_GET(dbz3_texture_dump); }
void SetTextureDumpDir(const std::string& dir) { REXCVAR_SET(dbz3_texture_dump, dir); }
// Suggested location when the user enables the dump without picking a folder:
// next to the project (a sibling of the install drive, where there is room).
std::string DefaultTextureDumpDir() {
  // Junto al juego (antes una ruta de desarrollo en D:, que no existe en otros equipos).
  return (rex::filesystem::GetExecutableFolder() / "texturas" / "capturas").string();
}
void SetTextureDumpEnabled(bool enabled) {
  if (!enabled) {
    REXCVAR_SET(dbz3_texture_dump, std::string());
    return;
  }
  if (REXCVAR_GET(dbz3_texture_dump).empty()) {
    REXCVAR_SET(dbz3_texture_dump, DefaultTextureDumpDir());
  }
}

// Texture packs: a pack is a folder in `mods/` containing files named like the
// dev dump (`<hash>_<W>x<H>_<FOURCC>.dds`). The launcher detects them and
// persists the list so the GPU plugin replaces those textures at runtime.
namespace {
std::filesystem::path LauncherModsRoot() {
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

bool LooksLikeTexturePackFile(const std::string& stem) {
  if (stem.size() < 20 || stem[16] != '_') {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(stem[i]))) {
      return false;
    }
  }
  size_t pos = 17;
  if (!std::isdigit(static_cast<unsigned char>(stem[pos]))) {
    return false;
  }
  while (pos < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pos]))) {
    ++pos;
  }
  if (pos >= stem.size() || stem[pos] != 'x') {
    return false;
  }
  ++pos;
  return pos < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pos]));
}

// UTF-8 text of a path: path::string() throws on names outside the Windows code page.
std::string PathUtf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

// \\?\ path on Windows: a pack inside an already long game folder goes past 260
// characters in its subfolders and the walk failed with "The system cannot find the
// path specified" (user log 2026-10-09: HD pack + HD language pack).
std::filesystem::path LongPath(const std::filesystem::path& p) {
#ifdef _WIN32
  std::error_code ec;
  std::filesystem::path full = std::filesystem::absolute(p, ec);
  std::wstring w = full.lexically_normal().make_preferred().wstring();
  if (ec || w.rfind(L"\\\\", 0) == 0) {
    return p;
  }
  return std::filesystem::path(L"\\\\?\\" + w);
#else
  return p;
#endif
}

bool IsTexturePackDir(const std::filesystem::path& plain_dir) {
  std::error_code ec;
  const std::filesystem::path dir = LongPath(plain_dir);
  // Recursivo: un pack puede ir ordenado en subcarpetas (personajes/, escenarios/...).
  for (auto [it_5, iec_5] = std::pair{std::filesystem::recursive_directory_iterator(
           dir, std::filesystem::directory_options::skip_permission_denied, ec), std::error_code{}}; !iec_5 && it_5 != std::filesystem::recursive_directory_iterator(); it_5.increment(iec_5)) {
    const auto& entry = *it_5;
    std::error_code fec;
    if (!entry.is_regular_file(fec)) {
      continue;
    }
    std::string ext = PathUtf8(entry.path().extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".dds" && ext != ".png") {
      continue;
    }
    if (LooksLikeTexturePackFile(PathUtf8(entry.path().stem()))) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool TexturePacksEnabled() { return !rex::cvar::GetFlagByName("dbz3_texture_packs").empty(); }
std::string TexturePacksList() { return rex::cvar::GetFlagByName("dbz3_texture_packs"); }

bool IsTexturePackFileName(const std::string& stem) { return LooksLikeTexturePackFile(stem); }

bool IsTexturePackMod(const std::string& mod_dir) {
  std::error_code ec;
  const std::filesystem::path dir(mod_dir);
  return std::filesystem::is_directory(dir, ec) && IsTexturePackDir(dir);
}

void RefreshTexturePacks() {
  const std::filesystem::path mods_root = LauncherModsRoot();
  std::vector<std::string> dirs;
  std::error_code ec;
  if (std::filesystem::is_directory(mods_root, ec)) {
    for (auto [it_6, iec_6] = std::pair{std::filesystem::directory_iterator(mods_root, ec), std::error_code{}}; !iec_6 && it_6 != std::filesystem::directory_iterator(); it_6.increment(iec_6)) {
    const auto& mod_entry = *it_6;
      std::error_code fec;
      if (!mod_entry.is_directory(fec)) {
        continue;
      }
      if (std::filesystem::exists(mod_entry.path() / ".disabled", fec)) {
        continue;
      }
      // Igual que ListMods: "<mod>.disabled" es un mod desactivado y ".install_*" un zip a medio
      // instalar; antes ambos se cargaban como pack aunque la pestana Mods no los mostrara.
      const std::string folder = PathUtf8(mod_entry.path().filename());
      if (folder.empty() || folder[0] == '.' ||
          (folder.size() > 9 && folder.compare(folder.size() - 9, 9, ".disabled") == 0)) {
        continue;
      }
      try {
        if (IsTexturePackDir(mod_entry.path())) {
          dirs.push_back(PathUtf8(mod_entry.path()));  // UTF-8: the GPU plugin reads it so
        }
      } catch (const std::exception& ex) {
        REXLOG_WARN("dbz3: texture pack '{}' skipped: {}", PathUtf8(mod_entry.path()), ex.what());
      }
    }
  }
  std::sort(dirs.begin(), dirs.end());
  std::string list;
  for (size_t i = 0; i < dirs.size(); ++i) {
    if (i != 0) {
      list += ';';
    }
    list += dirs[i];
  }
  // Set through the registry by name: the plugin's own storage is the ignored
  // duplicate, so a plain REXCVAR_SET here would not reach the plugin.
  // Solo si cambia: cada escritura hace que el plugin rehaga el indice del pack (con un pack HD, un
  // tiron de ~1,2 s en el launcher cada vez que se releia la lista de mods).
  if (rex::cvar::GetFlagByName("dbz3_texture_packs") != list) rex::cvar::SetFlagByName("dbz3_texture_packs", list);
  REXLOG_INFO("dbz3: texture packs detected in '{}': {} -> '{}'", PathUtf8(mods_root), dirs.size(),
              list);
}

// Focus behaviour. MuteUnfocused is registered by the SDK's audio driver
// (dbz3_mute_unfocused) and forwarded through the SDK registry; the dim option
// only exists in the launcher and is read by the in-game overlay.
bool MuteUnfocused() { return GetSdkBool("dbz3_mute_unfocused"); }
void SetMuteUnfocused(bool enabled) { SetSdkBool("dbz3_mute_unfocused", enabled); }

bool DimUnfocused() { return REXCVAR_GET(dbz3_dim_unfocused); }
void SetDimUnfocused(bool enabled) { REXCVAR_SET(dbz3_dim_unfocused, enabled); }

bool SkipLoadScreen() { return REXCVAR_GET(dbz3_skip_load_screen); }
void SetSkipLoadScreen(bool enabled) { REXCVAR_SET(dbz3_skip_load_screen, enabled); }

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
  ArmGpuCrashDiagnostics();
  // Re-detect the texture packs in mods/ (a pack is a folder with the dump's
  // `<hash>_<W>x<H>_<FOURCC>.dds` files). This runs before the GPU plugin loads,
  // so it must be set here and not only saved to the TOML.
  RefreshTexturePacks();
  REXCVAR_SET(present_effect, REXCVAR_GET(dbz3_present_effect));
  REXCVAR_SET(user_language, static_cast<uint32_t>(Language()));
  // Host graphics backend (d3d12/vulkan). Read by the runtime when it loads
  // the GPU plugin during SetupPresentation, so it must be set before then.
  std::string backend = GpuBackend();
#if REX_PLATFORM_WIN32
  if (backend != "vulkan" && !D3D12Usable()) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      REXLOG_WARN(
          "dbz3: Direct3D 12 no disponible en este equipo (GPU sin D3D12 11_0, driver o escritorio "
          "remoto): se usa Vulkan. Actualiza el driver de la grafica si el juego no arranca.");
    }
    backend = "vulkan";
  }
#endif
  rex::cvar::SetFlagByName("gpu_backend", backend);
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

  // Input: backend (before the input drivers are created in Setup), deadzone
  // (applied on the merged state), rumble (gates vibration), MnK emulation
  // toggle + mouse mode, and the keyboard bindings.
  rex::cvar::SetFlagByName("input_backend", InputBackend());
  SetSdkDouble("deadzone", Deadzone());
  SetSdkBool("rumble", RumbleEnabled());
  SetSdkBool("mnk_mode", MnkMode());
  SetSdkBool("mnk_mouse", MnkMouse());
  SetSdkDouble("mnk_sensitivity", MnkSensitivity());
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
  // HD textures (runtime texture upscale). The graphics texture cache is
  // initialized right after these settings are applied, so the value here is
  // what governs the upscale pipeline for this session (hence "restart
  // required" in the UI).
  SetSdkInt("dbz3_texture_upscale", REXCVAR_GET(dbz3_hd_textures));
  SetSdkInt("dbz3_upscale_max_texels", REXCVAR_GET(dbz3_hd_texture_max_texels));
  // The upscaler and its render resolution apply live (v1.4.0). dbz3_fsr_quality
  // is the pre-1.4 value: it never lowered the render resolution, so it is not
  // forwarded (its old default "quality" would now halve the picture at 2x).
  SetSdkString("present_effect", REXCVAR_GET(dbz3_present_effect));
  {
    const std::string render = REXCVAR_GET(dbz3_fsr_render);
    SetSdkString("present_fsr_quality_mode", render == "native" ? "nativeaa" : render);
  }
  // The frame rate panel (F3) is the Dev tab's "Show FPS".
  SetSdkBool("debug_overlay", REXCVAR_GET(dbz3_show_fps));
  SetSdkDouble("present_fsr_sharpness_reduction", REXCVAR_GET(dbz3_fsr_sharpness));
  SetSdkDouble("present_cas_additional_sharpness", REXCVAR_GET(dbz3_cas_sharpness));
  // Present-time filters and GPU plugin switches (these cvars are registered by
  // the time this runs: OnPostSetup). swap_post_effect runs before the upscaler,
  // so FXAA composes with FSR/CAS.
  SetSdkString("swap_post_effect", REXCVAR_GET(dbz3_fxaa));
  SetSdkBool("present_dither", REXCVAR_GET(dbz3_present_dither));
  // Widescreen (issue #35): present_letterbox=false stretches the 16:9 guest
  // image to fill a wider window (no bars, distorted); keeping it true leaves
  // the 16:9 image centered with bars. The cvar is read at paint time, so this
  // is live.
  SetSdkBool("present_letterbox", REXCVAR_GET(dbz3_aspect) != "stretch");
  SetSdkDouble("dbz3_rim_light_scale", HdRimLight());
  SetSdkInt("dbz3_relight_level", Relight());
  SetSdkBool("async_shader_compilation", REXCVAR_GET(dbz3_async_shaders));
  SetSdkBool("occlusion_query_enable", REXCVAR_GET(dbz3_occlusion_queries));
  // Real audio controls. `audio_gain` is the output gain the SDL callback
  // multiplies the mix by and `audio_mute` hard-silences it, both registered by
  // the SDK's audio driver. (The old code wrote a non-existent `master_volume`
  // cvar and forced audio_mute=false, which is why the volume sliders and the
  // mute toggle never did anything.)
  SetSdkDouble("audio_gain", REXCVAR_GET(dbz3_master_volume));
  SetSdkBool("audio_mute", REXCVAR_GET(dbz3_mute));
  // Focus behaviour: the SDK's audio driver silences the mix while the window is
  // in the background (the app writes `dbz3_window_focused` on focus changes).
  SetSdkBool("dbz3_mute_unfocused", MuteUnfocused());
  REXCVAR_SET(dbz1_diag_logging, DiagLogging() && DevMode());

  // The D3D12 presenter always calls Present(0) (never waits for vsync), so
  // the "vsync" cvar has no host-present meaning: it only paces the guest
  // vblank above. The host present rate is throttled by the real `frame_cap`
  // cvar (added to the 0.10 presenter) which the launcher maps to dbz3_frame_cap.
  // The cap is applied only for the game; before Play the launcher's ImGui
  // repaints are capped at 60 by main.cpp (OnPostSetup).
  REXCVAR_SET(host_present_from_non_ui_thread, true);
  // The game paces its main loop by the guest vblank; keep it at 60 Hz (never
  // raise it or the game logic would run faster than intended).
  REXCVAR_SET(video_mode_refresh_rate, 60.0);
  int32_t cap = SafeFrameCap(FrameCap());
  if (for_game) {
    SetSdkInt("frame_cap", cap);
  }
  REXLOG_INFO(
      "dbz3: applied runtime settings -> internal_scale={}x vsync={} msaa={} aniso={} "
      "hd_tex={}x "
      "fsr_quality={} fsr_sharpness={} cas_sharpness={} fxaa={} dither={} aspect={} "
      "async_shaders={} occ_queries={} mnk_sens={} "
      "audio_gain={} mute={} mute_unfocused={} dim_unfocused={} host_present={} vrr={} cap={}",
      scale, GetSdkBool("vsync") ? "true" : "false",
      GetSdkBool("native_2x_msaa") ? "true" : "false", GetSdkInt("anisotropic_override"),
      GetSdkInt("dbz3_texture_upscale"),
      GetSdkString("present_fsr_quality_mode"), GetSdkDouble("present_fsr_sharpness_reduction"),
      GetSdkDouble("present_cas_additional_sharpness"), GetSdkString("swap_post_effect"),
      GetSdkBool("present_dither") ? "true" : "false",
      REXCVAR_GET(dbz3_aspect),
      GetSdkBool("async_shader_compilation") ? "true" : "false",
      GetSdkBool("occlusion_query_enable") ? "true" : "false",
      GetSdkDouble("mnk_sensitivity"),
      GetSdkDouble("audio_gain"),
      GetSdkBool("audio_mute") ? "true" : "false",
      GetSdkBool("dbz3_mute_unfocused") ? "true" : "false",
      DimUnfocused() ? "true" : "false",
      REXCVAR_GET(host_present_from_non_ui_thread) ? "true" : "false",
      GetSdkBool("d3d12_allow_variable_refresh_rate_and_tearing") ? "true" : "false",
      for_game ? GetSdkInt("frame_cap") : 0);
}

}  // namespace dbz3::settings
