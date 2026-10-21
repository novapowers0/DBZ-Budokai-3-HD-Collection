// dbz3 - ReXGlue Recompiled Project
// Dragon Ball Z Budokai 3 (PAL / Xbox 360 HD Collection)
// Adapted from original Budokai 1 main.cpp

// Generated image configs are defined in generated/dbz3_init.cpp and (dual
// region) generated_eu/dbz3_eu_init.cpp. Forward-declared here instead of
// including the codegen pch headers: each pch defines helper symbols
// (get_jmp_buf_map, ppc_longjmp, ...) that would collide if both were pulled
// into this one translation unit.
#include <rex/image_info.h>
extern const rex::PPCImageInfo PPCImageConfig;
#if defined(DBZ3_DUAL_REGION) || defined(DBZ3_EU_VARIANT)
extern const rex::PPCImageInfo PPCImageConfigEU;
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/function.h>
#include <rex/system/xthread.h>
#include <rex/system/kernel_state.h>
#include <rex/system.h>
#include <rex/graphics/graphics_system.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>
#include <rex/rex_app.h>
#include <rex/hook.h>
#include "hooks.h"

#ifdef _WIN32
// DBZ3 1.4.3 EX: los drivers de NVIDIA (Optimus) y AMD (PowerXpress) solo leen
// estas exportaciones en el .EXE; en rexgpu-xenos.dll (donde las pone el SDK)
// no tienen efecto, y los portatiles hibridos arrancaban con la integrada.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
#ifndef DBZ3_EU_VARIANT
#include "roster_ext.h"
#include "stage_ext.h"
#endif
#include "region.h"
#include "mods.h"
#include "launcher/settings.h"
#if REX_PLATFORM_WIN32
#include <commdlg.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif
#include "launcher/launcher_state.h"
#include "launcher/mod_pipeline.h"
#include "launcher/ui_kit.h"
#include "launcher/update_check.h"
#include "ingame/quick_settings.h"
#include "launcher/i18n.h"
#include "ingame/menu.h"
#include "ingame/online_panel.h"
#include "ingame/tagteam_panel.h"
#include "net/room.h"
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/kernel/init.h>
#include <rex/assert.h>
#include <rex/system/flags.h>
#include <rex/audio/flags.h>

#include <imgui.h>

#include <spdlog/spdlog.h>

#include <rex/system/flags.h>

REXCVAR_DECLARE(bool, dbz3_skip_launcher);

// dbz3 - Window focus, shared with the in-game overlay (which dims the picture
// while the window is in the background) and with the audio driver through the
// `dbz3_window_focused` cvar (which silences the mix while unfocused).
std::atomic<bool> g_window_focused{true};
namespace dbz3 {
// Pantalla "juego en segundo plano" activa: las pantallas propias (TAG TEAM, ONLINE) no se dibujan
// encima de ella.
bool GameDimmed() { return dbz3::settings::DimUnfocused() && !g_window_focused.load(); }
// Launcher (F4), menu rapido (F1) o menu de desarrollo (F10) abiertos sobre el juego: las entradas
// propias del menu (TAG TEAM, ONLINE) y sus pantallas no se pintan encima.
std::atomic<bool> g_overlay_open{false};
bool OverlayOpen() { return g_overlay_open.load() || rex::ui::QuickMenuDialog::IsOpen(); }
}  // namespace dbz3

#include <atomic>
#include <chrono>
#include <filesystem>
#include <utility>
#include <thread>
#include <exception>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <atomic>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace dbz3 {
std::atomic<void*> g_hwnd{nullptr};
// Icono de la ventana con una imagen del propio juego (leida de los ficheros del jugador, no se reparte)
void SetWindowIconRgba(const uint8_t* rgba, int w, int h) {
#if REX_PLATFORM_WIN32
  HWND hwnd = static_cast<HWND>(g_hwnd.load());
  if (!hwnd || !rgba || w <= 0 || h <= 0) return;
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = w;
  bi.bV5Height = -h;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color || !bits) return;
  auto* p = static_cast<uint8_t*>(bits);
  for (int i = 0; i < w * h; ++i) {   // RGBA -> BGRA
    p[4 * i] = rgba[4 * i + 2];
    p[4 * i + 1] = rgba[4 * i + 1];
    p[4 * i + 2] = rgba[4 * i];
    p[4 * i + 3] = rgba[4 * i + 3];
  }
  HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
  ICONINFO ii{TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  if (!icon) return;
  SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
#else
  (void)rgba; (void)w; (void)h;
#endif
}
}  // namespace dbz3

namespace {
// Startup timing helper: logs the elapsed milliseconds since first call so the
// "long black screen before the launcher" reports can be diagnosed from logs.
double UptimeMs() {
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
void PhaseLog(const char* name) {
  REXLOG_INFO("dbz3: phase '{}' at {:.0f} ms since start", name, UptimeMs());
}
}  // namespace

class DebugOverlayDialog : public rex::ui::ImGuiDialog {
public:
    DebugOverlayDialog(rex::ui::ImGuiDrawer* imgui_drawer)
        : ImGuiDialog(imgui_drawer) {}
protected:
    void OnDraw(ImGuiIO& io) override {
        // Dimmed "the game is in the background" screen. Drawn whether or not
        // the FPS counter is on: it makes it obvious that the window is not
        // active, and hides the picture so it cannot be read while the player is
        // away (that is also why emulators dim the screen when they pause on
        // focus loss). It is purely visual, the game keeps running.
        if (dbz3::settings::DimUnfocused() && !g_window_focused.load()) {
            dbz3::i18n::SetLanguage(dbz3::settings::Language());
            const ImVec2 display = io.DisplaySize;
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 205));
            const char* title =
                dbz3::i18n::T("Juego en segundo plano", "Game in the background");
            const char* subtitle = dbz3::settings::MuteUnfocused()
                                       ? dbz3::i18n::T("El audio esta silenciado.",
                                                       "Audio is muted.")
                                       : dbz3::i18n::T("El juego sigue en marcha.",
                                                       "The game is still running.");
            const char* hint = dbz3::i18n::T("Vuelve a la ventana para seguir jugando.",
                                             "Return to the window to keep playing.");
            ImFont* font = ImGui::GetFont();
            const float title_size = ImGui::GetFontSize() * 1.9f;
            const ImVec2 title_extent = font->CalcTextSizeA(title_size, FLT_MAX, 0.0f, title);
            const ImVec2 sub_extent = ImGui::CalcTextSize(subtitle);
            const ImVec2 hint_extent = ImGui::CalcTextSize(hint);
            const float center_x = display.x * 0.5f;
            const float center_y = display.y * 0.5f;
            const float gap = ImGui::GetFontSize() * 0.6f;
            draw_list->AddText(font, title_size,
                               ImVec2(center_x - title_extent.x * 0.5f, center_y - title_size),
                               IM_COL32(255, 255, 255, 235), title);
            draw_list->AddText(ImVec2(center_x - sub_extent.x * 0.5f, center_y + gap),
                               IM_COL32(230, 230, 230, 220), subtitle);
            draw_list->AddText(ImVec2(center_x - hint_extent.x * 0.5f,
                                      center_y + gap + ImGui::GetTextLineHeight() * 1.6f),
                               IM_COL32(190, 190, 190, 200), hint);
        }
        // The FPS counter is the SDK's frame rate panel now (F3, v1.4.0):
        // dbz3_show_fps is forwarded to its `debug_overlay` cvar.
    }
};

class Dbz3App : public rex::ReXApp {
public:
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
        return std::make_unique<Dbz3App>(ctx);
    }

    Dbz3App(rex::ui::WindowedAppContext& ctx)
        : ReXApp(ctx, "dbz3",
#if defined(DBZ3_DUAL_REGION)
                 // Dual-region: the base is constructed with the US config; the
                 // ResolveImageInfo override switches to the EU config at setup
                 // time when an EU/PAL default.xex is detected.
                 PPCImageConfig,
#else
                 // Single-region builds (US or EU) use their own config symbol.
                 PPCImageConfig,
#endif
                 "[game_directory]") {
        AddPositionalOption("game_directory");
    }

#if defined(DBZ3_DUAL_REGION)
    // Pick the image config (and its func_mappings) that matches the on-disk
    // default.xex before Runtime::Setup. US and EU guest images share address
    // ranges, so only the active region's mappings may be registered.
    const rex::PPCImageInfo& ResolveImageInfo(const rex::PathConfig& paths) const override {
        const auto status = dbz3::settings::CheckDefaultXex(paths.game_data_root);
        if (status == dbz3::settings::XexStatus::kEu) {
            REXLOG_INFO("dbz3: dual-region core detected EU/PAL default.xex - using EU image config");
            return PPCImageConfigEU;
        }
        if (status == dbz3::settings::XexStatus::kUs) {
            REXLOG_INFO("dbz3: dual-region core detected US/NA default.xex - using US image config");
        }
        return PPCImageConfig;
    }
#endif

    // Called before Runtime::Setup() - configure GPU plugin
    void OnPreSetup(rex::RuntimeConfig& config) override {
        PhaseLog("OnPreSetup");
        // antes de crear la GPU: un fallo de D3D12 al arrancar (rex::FatalError -> abort) cerraba en negro sin
        // ventana ni registro (auditoria9/4C: "se pone en negro y se cierra"); OnPostSetup lo vuelve a poner
        SetupCrashHandler();
        // Pre-flight check before Runtime::Setup(): without a default.xex the
        // runtime cannot be constructed, and the framework teardown that follows
        // a failed Setup used to end in a confusing 0xC0000005 access violation
        // (the window opened, then died with no message). Fail early with a
        // clear, actionable message instead.
        const std::filesystem::path game_root = dbz3::EffectiveGameRoot();
        const auto& boot = dbz3::settings::CurrentBootSource();
        const std::filesystem::path xex_probe =
            boot.xex.empty() ? (game_root / "default.xex") : boot.xex;
        if (!std::filesystem::is_regular_file(xex_probe)) {
          REXLOG_ERROR("dbz3: no executable found (probed '{}')", xex_probe.string());
          // lo tecnico va al log; el aviso habla como un jugador (propuesta del frente QoL, 2026-10-15)
          REXLOG_INFO("dbz3: se busca default.xex, yae3_xenon.xex o DBZ3/yae3_xenon.xex con us/ (o eu/) al lado, "
                      "tambien en assets/; un .zip/.rar sin extraer o una copia 'Games on Demand' (Content/0000...) "
                      "no sirven");
          dbz3::i18n::SetLanguage(dbz3::settings::Language());
          using dbz3::i18n::T;
          const std::string msg =
              std::string(T("No encuentro tu copia de Budokai 3 HD.\n\n"
                            "Pon la imagen del disco (ISO de Xbox 360) junto a dbz3.exe, o elÃ­gela ahora. "
                            "Si tienes el juego extraÃ­do en una carpeta, elige esa carpeta (la que tiene "
                            "DBZ3, o default.xex y us/).\n\n"
                            "SÃ­: elegir la imagen del disco (.iso)\n"
                            "No: elegir la carpeta del juego extraÃ­do\n"
                            "Cancelar: salir\n\n"
                            "Carpeta buscada:\n",
                            "I can't find your copy of Budokai 3 HD.\n\n"
                            "Put the disc image (Xbox 360 ISO) next to dbz3.exe, or pick it now. "
                            "If you have the game extracted in a folder, pick that folder (the one with "
                            "DBZ3, or default.xex and us/).\n\n"
                            "Yes: pick the disc image (.iso)\n"
                            "No: pick the folder with the extracted game\n"
                            "Cancel: quit\n\n"
                            "Folder searched:\n")) +
              game_root.string();
#if REX_PLATFORM_WIN32
          // El launcher no puede abrirse sin el ejecutable del juego (Runtime::Setup lo necesita): aqui se
          // ofrecen sus dos botones (imagen de disco / carpeta extraida) y se reinicia con lo elegido. Antes,
          // con "No" el programa se cerraba sin mas (un jugador se quedo sin forma de llegar, 2026-10-12).
          const char* caption = T("DBZ Budokai 3 - Datos del juego", "DBZ Budokai 3 - Game data");
          auto restart = [](bool same_args) {
            wchar_t self[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, self, MAX_PATH);
            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            // ISO: con los mismos argumentos (antes se perdian). Carpeta: sin ellos, o un --game_directory
            // le ganaria a la carpeta elegida y el aviso volveria a salir
            std::wstring cmd = same_args ? std::wstring(GetCommandLineW()) : std::wstring();
            if (CreateProcessW(self, same_args ? cmd.data() : nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                               &si, &pi)) {
              CloseHandle(pi.hThread);
              CloseHandle(pi.hProcess);
            }
            rex::FlushLogging();
            std::_Exit(0);
          };
          auto warn = [&](const char* text) {
            MessageBoxA(nullptr, text, caption, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
          };
          while (!std::getenv("DBZ3_NO_DIALOGS")) {   // pruebas automaticas: solo el log
            const int pick = MessageBoxA(nullptr, msg.c_str(), caption,
                                         MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
            if (pick == IDYES) {
              wchar_t file[MAX_PATH] = {};
              OPENFILENAMEW ofn = {};
              ofn.lStructSize = sizeof(ofn);
              ofn.lpstrFilter = std::strcmp(dbz3::i18n::LangCode(), "es") == 0
                                    ? L"Imagen de disco (*.iso)\0*.iso;*.xiso\0Todos los archivos\0*.*\0"
                                    : L"Disc image (*.iso)\0*.iso;*.xiso\0All files\0*.*\0";
              ofn.lpstrFile = file;
              ofn.nMaxFile = MAX_PATH;
              ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
              if (!GetOpenFileNameW(&ofn)) continue;   // cancelado: otra vez la pregunta
              if (dbz3::settings::IsValidIso(std::filesystem::path(file))) {
                dbz3::settings::SetIsoPath(rex::path_to_utf8(std::filesystem::path(file)));
                dbz3::settings::SaveUserSettings();
                REXLOG_INFO("dbz3: imagen de disco elegida en el aviso de arranque; se reinicia");
                restart(true);
              }
              warn(T("Ese archivo no es una imagen de disco de Xbox 360 (tiene que ser el disco de\n"
                     "Budokai 3 o de la HD Collection, completo).",
                     "That file is not an Xbox 360 disc image (it has to be the full Budokai 3\n"
                     "or HD Collection disc)."));
            } else if (pick == IDNO) {
              std::filesystem::path folder;
              const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
              IFileOpenDialog* dlg = nullptr;
              if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
                DWORD opts = 0;
                dlg->GetOptions(&opts);
                dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                IShellItem* res = nullptr;
                PWSTR path = nullptr;
                if (SUCCEEDED(dlg->Show(nullptr)) && SUCCEEDED(dlg->GetResult(&res)) &&
                    SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                  folder = std::filesystem::path(std::wstring(path));
                  CoTaskMemFree(path);
                }
                if (res) res->Release();
                dlg->Release();
              }
              if (SUCCEEDED(com)) CoUninitialize();
              if (folder.empty()) continue;   // cancelado: otra vez la pregunta
              if (dbz3::settings::IsValidGameDataDir(folder)) {
                dbz3::settings::SetGameDirOverride(rex::path_to_utf8(folder));
                dbz3::settings::SetIsoPath("");   // la carpeta elegida gana (como en el launcher)
                dbz3::settings::SaveUserSettings();
                REXLOG_INFO("dbz3: carpeta del juego elegida en el aviso de arranque; se reinicia");
                restart(false);
              }
              const std::string leaf = rex::path_to_utf8(folder.filename());
              warn(leaf == "us" || leaf == "eu"
                       ? T("Has elegido la carpeta us/ (o eu/). Elige la carpeta que la contiene.",
                           "You picked the us/ (or eu/) folder itself. Pick the folder that contains it.")
                       : T("Esa carpeta no tiene el juego. Elige la que contiene DBZ3, o default.xex y us/.",
                           "That folder does not have the game. Pick the one that contains DBZ3, or "
                           "default.xex and us/."));
            } else {
              break;   // Cancelar: salir
            }
          }
#else
          std::fprintf(stderr, "%s\n", msg.c_str());
#endif
          rex::FlushLogging();
          std::_Exit(1);
        }
        config.gpu_plugin = "xenos";
        config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
        config.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
        config.kernel_init = rex::kernel::InitializeKernel;

        // User settings: load dbz3_user.toml (must run after the SDK config
        // so user values win) and forward the friendly cvars onto the SDK's
        // startup cvars before the window/presentation is created. Wrapped in
        // try/catch so a bad user value can never prevent the game from booting.
        try {
            dbz3::settings::LoadUserSettings();
            REXLOG_INFO("dbz3: LoadUserSettings done");
            // Force the launcher to always open windowed, regardless of what
            // the user config or a previous session may have stored. The chosen
            // fullscreen mode is applied on Play via ApplyWindowSizeToSdk.
            REXCVAR_SET(fullscreen, false);
            dbz3::settings::ApplyUserSettingsToSdk();
            REXLOG_INFO("dbz3: ApplyUserSettingsToSdk done");
        } catch (const std::exception& e) {
            REXLOG_ERROR("dbz3: settings apply failed ({}), continuing with defaults", e.what());
        } catch (...) {
            REXLOG_ERROR("dbz3: settings apply failed (unknown error), continuing with defaults");
        }
    }

    // Called after runtime is fully initialized, before window creation
    void OnPostSetup() override {
        PhaseLog("OnPostSetup");
        REXLOG_INFO("OnPostSetup - Runtime initialized, graphics system should be ready");
        // Allow draws with invalid fetch constants (shadow passes etc). Mirrors
        // what dbz1's ApplyUserSettingsToSdk does with the user toml. Must run
        // after the GPU plugin is loaded so the cvar registry is live.
        rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
        // Runtime/GPU cvars (vsync, MSAA, aniso, FSR) only exist now that the
        // GPU plugin has registered them.
        dbz3::settings::ApplyRuntimeSettingsToSdk(false);
        // 1.4.3 EX: el launcher sin tope se repintaba a la frecuencia del monitor
        // (143-240 Hz) y en reposo gastaba ~30 % de una RTX 4070: en portatiles y
        // graficas integradas eso basta para que todo vaya a tirones. 60 es fluido;
        // al pulsar Jugar se aplica el tope del usuario (ApplyRuntimeSettingsToSdk(true)).
        rex::cvar::SetFlagByName("frame_cap", "60");
#ifdef _WIN32
        // 1.4.3 EX: el guest necesita sus hilos a tiempo cada 16,7 ms. Con otros
        // programas pesados abiertos (navegador, render de video, antivirus) se
        // medio 45-52 FPS y tirones de 100 ms en un 12600KF. "Por encima de lo
        // normal" (no "alta" ni "tiempo real") le da preferencia sin ahogar
        // al resto del sistema.
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif
        SetupCrashHandler();
    }

    // dbz3 - Window focus. Drives the "mute while in the background" feature
    // (the audio driver reads the `dbz3_window_focused` cvar) and the dimmed
    // overlay drawn by DebugOverlayDialog.
    void OnWindowFocusChanged(bool focused) override {
        g_window_focused.store(focused);
        rex::cvar::SetFlagByName("dbz3_window_focused", focused ? "true" : "false");
        REXLOG_INFO("dbz3: window {}", focused ? "focused" : "in the background");
    }

    // Launcher fonts (Segoe UI + icons) go into the shared atlas before it is built.
    void OnConfigureFonts(ImFontAtlas* atlas) override {
        dbz3::launcher::ui::LoadFonts(atlas);
    }

    // In-game quick settings (F1 / Back + Start), see ingame/quick_settings.cpp.
    void OnConfigureQuickMenu(rex::ui::QuickMenuConfig& menu) override {
        dbz3::ingame::ConfigureQuickMenu(menu, [this]() { OpenFullSettings(); });
        // Only over the game: the pre-game launcher has all of this already.
        menu.can_open = [this, &menu]() {
            if (!launched_.load(std::memory_order_acquire) || launcher_dialog_) return false;
            // The language may have changed in the launcher after the menu was built.
            dbz3::ingame::RefreshQuickMenuLanguage(menu, [this]() { OpenFullSettings(); });
            return true;
        };
    }

    // The launcher dialog over the game (F4 and the quick menu's "All settings").
    void OpenFullSettings() {
        if (!imgui_drawer_ || launcher_dialog_) return;
        launcher_dialog_ = new dbz3::launcher::LauncherDialog(
            imgui_drawer_, [this]() { launcher_dialog_ = nullptr; dbz3::g_overlay_open = ingame_menu_ != nullptr; });
        dbz3::g_overlay_open = true;
        // Over the game: the game must not also read the pad meanwhile.
        launcher_dialog_->SetInGame(launched_.load(std::memory_order_acquire));
    }

    // Called after ImGui drawer is created - add custom dialogs
    void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
        PhaseLog("OnCreateDialogs");
        if (!debug_overlay_) {
            debug_overlay_ = std::make_unique<DebugOverlayDialog>(drawer);
        }
        imgui_drawer_ = drawer;
        // nombre del juego en la ventana (el SDK pone "dbz3" y su sello de compilacion)
        if (window()) {
            window()->SetTitle("Dragon Ball Z Budokai 3 HD");
            dbz3::g_hwnd = window()->GetNativeWindowHandle();
        }
        if (!drop_listener_added_ && window()) {
            window()->AddListener(&drop_listener_);
            drop_listener_added_ = true;
        }
        // The launcher reads the controllers on the UI thread, outside its paint.
        dbz3::launcher::SetUiDefer([this](std::function<void()> fn) {
            app_context().CallInUIThreadDeferred(std::move(fn));
        });
        // Vistas previas de imagenes en el launcher (personajes nuevos).
        dbz3::launcher::SetPreviewDrawer(immediate_drawer());

        // Pre-game launcher. Its "Play" button dismisses the dialog and
        // triggers the gated module launch (see LaunchModule override below).
        // NOTE: the dialog self-deletes on Close(), so we hold a raw pointer
        // (not unique_ptr) and null it from the on_play callback.
        // NOTE: skip-launcher boots directly; drawing the dialog here would let
        // its PLAY/Enter shortcut fire a second LaunchModule (guarded, but
        // pointless). Skip creating it in that dev fast path.
        if (!launcher_dialog_ && !REXCVAR_GET(dbz3_skip_launcher)) {
            launcher_dialog_ = new dbz3::launcher::LauncherDialog(
                drawer, [this]() {
                    launcher_dialog_ = nullptr;
                    // Persist the launcher choices (region, mods, language,
                    // video, audio, input) before launching so they are applied
                    // on the next boot even if the user forgot "Save settings".
                    dbz3::settings::SaveUserSettings();
                    // The launcher lets the user change the region (us/eu) after
                    // the runtime's initial VFS setup. Re-mount the game drive
                    // (folder mode: <root>; ISO mode: the disc image) and re-apply
                    // the region mount now (before the guest module launches) so
                    // game:\us points at the currently selected region's assets.
                    // Mods (per-entry AFS and whole-file) are served directly from
                    // mods/ by the runtime's override hooks, with no overlay or
                    // duplication.
                    dbz3::RelocateGameData(dbz3::EffectiveGameRoot());
                    // Apply the chosen window size and fullscreen mode to the
                    // actual host window before the module launches. The SDK
                    // only sets fullscreen at window creation, so we must do
                    // it here at runtime. (Borderless = SDL fullscreen desktop;
                    // exclusive would need SDK changes.)
                    dbz3::settings::ApplyWindowSizeToSdk();
                    if (auto* w = window()) {
                        const bool full = dbz3::settings::FullscreenMode() != "windowed";
                        if (full != w->IsFullscreen()) {
                            w->SetFullscreen(full);
                        }
                    }
                    ReXApp::LaunchModule();
                });
        }

        // F4 opens the settings launcher in-game (hot-reloadable options).
        rex::ui::RegisterBind("bind_dbz3_settings", "F4", "Open settings", [this]() {
            OpenFullSettings();
        });

        // F2 enciende/apaga el pack HD de texturas en vivo (lo mismo que su opcion de F1).
        rex::ui::RegisterBind("bind_dbz3_hd_pack", "F2", "Toggle the HD texture pack", []() {
            const bool on = rex::cvar::GetFlagByName("dbz3_texture_packs_on") != "false";
            rex::cvar::SetFlagByName("dbz3_texture_packs_on", on ? "false" : "true");
            dbz3::settings::SaveUserSettings();
            REXLOG_INFO("dbz3: F2: pack HD {}", on ? "apagado" : "encendido");
        });

        // F10 toggles the in-game dev overlay (only meaningful once running).
        rex::ui::RegisterBind("bind_dbz3_ingame", "F10", "Toggle in-game dev overlay", [this]() {
            if (!launched_.load(std::memory_order_acquire)) {
                return;  // Launcher not dismissed yet.
            }
            if (ingame_menu_) {
                ingame_menu_.reset();
            } else {
                ingame_menu_ = std::make_unique<dbz3::ingame::InGameMenu>(imgui_drawer_);
            }
            dbz3::g_overlay_open = ingame_menu_ != nullptr || launcher_dialog_ != nullptr;
        });

    }

    // Called immediately before the main guest thread is created
    void OnPreLaunchModule() override {
        PhaseLog("OnPreLaunchModule");
        REXLOG_INFO("OnPreLaunchModule - about to launch guest thread");
#ifndef DBZ3_EU_VARIANT
        dbz3::roster::GuardGeneratedMod();
#endif
        // Re-mount the game drive (folder or ISO) and re-apply the region device
        // so game:\us points at the currently selected region's assets (covers
        // the skip-launcher fast path too).
        dbz3::RelocateGameData(dbz3::EffectiveGameRoot());
    }

    // Called after the main guest thread is created but before it starts executing
    void OnPostLaunchModule(rex::system::XThread* thread) override {
        (void)thread;
        PhaseLog("OnPostLaunchModule");
        REXLOG_INFO("OnPostLaunchModule - guest thread created and resumed");
        launched_.store(true, std::memory_order_release);
#ifndef DBZ3_EU_VARIANT
        // Personajes/trajes declarados por mods (roster.toml): la imagen del guest
        // ya esta cargada y su hilo aun no ha ejecutado nada.
        if (auto* rt = runtime()) {
            dbz3::roster::ApplyAtLaunch(rt->memory());
            dbz3::stages::ApplyAtLaunch(rt->memory());
        }
#endif
        if (auto* rt = runtime()) {
            if (window()) {
                rt->set_display_window(window());
            } else {
            }
            if (imgui_drawer_) {
                rt->set_imgui_drawer(imgui_drawer_);
                // panel ONLINE del Duelo: siempre creado, solo se dibuja al pedirlo (SELECT)
                online_panel_ = std::make_unique<dbz3::ingame::OnlinePanel>(imgui_drawer_);
                tagteam_panel_ = std::make_unique<dbz3::ingame::TagTeamPanel>(imgui_drawer_);
                // PRUEBA (DBZ3_TEST_OVERLAY=N): abre el launcher sobre el juego a los N s (sin teclado real)
                // PRUEBA (DBZ3_TEST_PACK_TOGGLE_MS=N): alterna el pack HD cada N ms.
                // Sirve para las 200 transiciones de G10 sin depender del foco/teclado.
                if (const char* ms = std::getenv("DBZ3_TEST_PACK_TOGGLE_MS")) {
                    const int delay = std::atoi(ms);
                    if (delay > 0) {
                        std::thread([delay]() {
                            while (true) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                                const bool on = rex::cvar::GetFlagByName("dbz3_texture_packs_on") != "false";
                                rex::cvar::SetFlagByName("dbz3_texture_packs_on", on ? "false" : "true");
                                REXLOG_INFO("dbz3: prueba: pack HD {}", on ? "apagado" : "encendido");
                            }
                        }).detach();
                    }
                }                if (const char* t = std::getenv("DBZ3_TEST_OVERLAY")) {
                    std::thread([this, sec = std::atoi(t)]() {
                        std::this_thread::sleep_for(std::chrono::seconds(sec));
                        app_context().CallInUIThreadDeferred([this]() { OpenFullSettings(); });
                    }).detach();
                }
            } else {
            }
        } else {
        }
    }

    // Called after path defaults are computed, before Runtime is constructed
    void OnConfigurePaths(rex::PathConfig& paths) override {
        auto exe_dir = rex::filesystem::GetExecutableFolder();
        REXLOG_INFO("OnConfigurePaths - exe_dir: {}", exe_dir.string());
        // OnConfigurePaths runs in SetupEnvironment, before OnPreSetup loads
        // the user settings. Load them here too so dbz3_region is already the
        // persisted value when we build the region/mod overlay below.
        try {
            dbz3::settings::LoadUserSettings();
        } catch (const std::exception& e) {
            REXLOG_ERROR("dbz3: OnConfigurePaths LoadUserSettings failed ({}), using defaults", e.what());
        }
        // Game directory: arg or default to the project root (disc root).
        // The exe lives in out/build/win-amd64-release, so the project root
        // is three levels up. The VFS maps game:\ and d:\ to this root, so
        // D:\us\ resolves to <game_dir>/us/ (the actual game data).
        // Both layouts are supported:
        //   - dev:      <root>/us, <root>/eu (project root next to out/build)
        //   - standalone release: <exe_dir>/us or <exe_dir>/assets/{default.xex,us,eu}
        // Fall back to the exe folder if nothing is found.
        auto FindGameRoot = [](const std::filesystem::path& base) -> std::filesystem::path {
            // Accept either region folder (us/ or eu/), in the flat layout
            // (base/us), the assets/ layout (base/assets/us) and the RETAIL DISC
            // layout (base/DBZ3/us â€” the disc keeps Budokai 3 in DBZ3/ next to
            // the HD Collection's own default.xex menu). An EU-only user has eu/
            // but no us/ â€” rejecting it made auto-detection fall back to the exe
            // folder and fail with "Entrypoint XEX not found".
            // Matches IsValidGameDataDir/banner.
            const char* kSub[] = {"", "assets", "DBZ3", "assets/DBZ3", "DBZ3/assets"};
            for (const char* sub : kSub) {
                const auto dir = sub[0] == '\0' ? base : base / sub;
                if (std::filesystem::is_directory(dir / "us") ||
                    std::filesystem::is_directory(dir / "eu")) {
                    return dir;
                }
            }
            return {};
        };
        std::filesystem::path game_dir;
        if (auto arg = GetArgument("game_directory")) {
            game_dir = *arg;
            REXLOG_INFO("OnConfigurePaths - game_dir from arg: {}", game_dir.string());
        } else {
            // Priority order for locating the game assets:
            //   1) next to the exe (standalone release layout: dbz3.exe + us/ + default.xex)
            //   2) the parent of the exe folder (release layout with assets in <release>/assets)
            //   3) the project root (dev layout: out/build/win-amd64-release, 3 levels up)
            if (auto root = FindGameRoot(exe_dir); !root.empty()) {
                game_dir = root;
                REXLOG_INFO("OnConfigurePaths - game_dir default (next to exe): {}", game_dir.string());
            } else if (auto root = FindGameRoot(exe_dir.parent_path()); !root.empty()) {
                game_dir = root;
                REXLOG_INFO("OnConfigurePaths - game_dir default (parent): {}", game_dir.string());
            } else if (auto root = FindGameRoot(exe_dir.parent_path().parent_path().parent_path());
                       !root.empty()) {
                game_dir = root;
                REXLOG_INFO("OnConfigurePaths - game_dir default (project root): {}", game_dir.string());
            } else {
                game_dir = exe_dir;
                REXLOG_INFO("OnConfigurePaths - game_dir default (fallback exe dir): {}", game_dir.string());
            }
            // A folder explicitly picked in the launcher ("Seleccionar carpeta
            // de datos...", persisted in dbz3_game_dir) outranks auto-detection.
            const std::string override = dbz3::settings::GameDirOverride();
            if (!override.empty() && dbz3::settings::IsValidGameDataDir(override)) {
                game_dir = override;
                REXLOG_INFO("OnConfigurePaths - game_dir from user override: {}", override);
            } else if (!override.empty()) {
                REXLOG_WARN("OnConfigurePaths - stored game_dir override '{}' is not a valid game "
                            "data folder, ignoring it", override);
            }
        }
        REXLOG_INFO("OnConfigurePaths - game_dir final: {}", game_dir.string());

        // The runtime boots <game_data_root>/default.xex while the game drive
        // serves the assets. Locate the real Budokai 3 executable wherever the
        // user keeps it: a retail disc dump has it at DBZ3/yae3_xenon.xex and its
        // root default.xex is the HD Collection's own menu (which this core
        // cannot boot), and extracted dumps often keep the original name too.
        // The executable is staged in a small cache when its name differs, so no
        // manual renaming is ever required and the user's files are never
        // modified.
        std::vector<std::filesystem::path> hunt_roots;
        hunt_roots.push_back(game_dir);
        if (!game_dir.parent_path().empty()) hunt_roots.push_back(game_dir.parent_path());
        hunt_roots.push_back(exe_dir);
        if (!exe_dir.parent_path().empty()) hunt_roots.push_back(exe_dir.parent_path());
        dbz3::settings::BootSource boot = dbz3::settings::ResolveBootSource(
            game_dir, hunt_roots, dbz3::settings::XexCacheDir());
        dbz3::settings::SetCurrentBootSource(boot);
        REXLOG_INFO(
            "OnConfigurePaths - boot source: xex='{}' status={} data_root='{}' "
            "redirect_default_xex={} note='{}'",
            boot.xex.string(), dbz3::settings::XexStatusLabel(boot.status),
            boot.data_root.string(), boot.redirect_default_xex ? "yes" : "no", boot.note);

        // Use the resolved data folder directly as the game drive root (no
        // overlay, no duplicate assets). The runtime mounts game:\ to that folder
        // and the game reads D:\us\... which resolves to <root>/us (or, for the
        // eu region, to <root>/eu via the ApplyRegionMount device). Mods are
        // served by the runtime's AFS/whole-file override hooks from mods/,
        // without copying anything.
        game_dir_ = boot.data_root.empty() ? game_dir : boot.data_root;
        // The runtime's pre-flight and its region detection read
        // <game_data_root>/default.xex, so point it at the folder that actually
        // holds the bootable executable (the staging cache when the executable
        // had another name, otherwise the data folder itself).
        paths.game_data_root = boot.xex.empty() ? game_dir : boot.xex.parent_path();
        // Keep the effective game data root in sync so region mounting (which
        // reads this root, not the runtime's fixed copy) always uses the folder
        // we resolved here.
        dbz3::SetEffectiveGameRoot(game_dir_);
        // User data (save games/memory cards + the xex/iso caches): normally
        // <exe_dir>/user_data/dbz3 so the release stays portable. If that folder
        // cannot be written (installed under Program Files, read-only share) the
        // settings layer switches to the per-user app data folder so saves keep
        // working instead of failing silently. The probe creates the folders.
        paths.user_data_root = dbz3::settings::UserDataRoot();
        paths.cache_root = paths.user_data_root / "cache";
        paths.metadata_root = exe_dir / "metadata";
        // 1.4.3 EX: la release trae en shader_cache/ los shaders y pipelines de
        // TODO el juego. En una instalacion nueva se copian (solo si faltan: nunca
        // se pisa la cache del jugador), asi el primer combate no compila nada
        // sobre la marcha (efectos que tardan en salir o tirones).
        {
            std::error_code ec;
            const auto shipped = exe_dir / "shader_cache";
            const auto shareable = paths.cache_root / "shaders" / "shareable";
            if (std::filesystem::is_directory(shipped, ec)) {
                std::filesystem::create_directories(shareable, ec);
                for (auto [it_1, iec_1] = std::pair{std::filesystem::directory_iterator(shipped, ec), std::error_code{}}; !iec_1 && it_1 != std::filesystem::directory_iterator(); it_1.increment(iec_1)) {
    const auto& f = *it_1;
                    const auto dst = shareable / f.path().filename();
                    // a .tmp y renombrar: un cierre a mitad de copia dejaba un fichero cortado que,
                    // como ya existia, nunca se volvia a copiar
                    auto tmp = dst;
                    tmp += ".tmp";
                    if (f.is_regular_file(ec) && !std::filesystem::exists(dst, ec) &&
                        std::filesystem::copy_file(f.path(), tmp, std::filesystem::copy_options::overwrite_existing, ec)) {
                        std::filesystem::rename(tmp, dst, ec);
                        REXLOG_INFO("dbz3: cache de shaders inicial copiada: {}{}",
                                    f.path().filename().string(), ec ? " (ERROR al renombrar)" : "");
                    }
                }
            }
        }
        REXLOG_INFO("OnConfigurePaths - user_data_root: {} ({})",
                    paths.user_data_root.string(),
                    dbz3::settings::UserDataIsPortable() ? "portable" : "per-user");
        REXLOG_INFO("OnConfigurePaths - game_data_root set to: {}", paths.game_data_root.string());
        REXLOG_INFO("OnConfigurePaths - game drive root set to: {}", game_dir_.string());

        // ISO mode: play directly from a disc image instead of an extracted
        // us/eu/ folder. The runtime still needs a real default.xex for its
        // pre-flight checks and region detection, so we extract ONLY that file
        // (few MB, cached) from the GDFX image -- nothing else is ever copied.
        // At Play time RemountGameDrive mounts the whole disc as game:.
        std::filesystem::path iso_path;
        const std::string iso_override = dbz3::settings::IsoPath();
        if (dbz3::settings::IsValidIso(iso_override)) {
          iso_path = iso_override;
        } else if (!dbz3::settings::IsValidGameDataDir(game_dir) ||
                   boot.status == dbz3::settings::XexStatus::kHdMenu ||
                   boot.status == dbz3::settings::XexStatus::kMissing) {
          // Auto-detect: if no extracted folder was found, or the folder exists
          // but holds only the HD Collection's menu (a disc dump copied as-is:
          // us/ is there, so the folder looks valid, but default.xex is the
          // menu and nothing can boot from it), look for a disc image next to
          // the game / executable and play from it directly. The real Budokai 3
          // executable lives inside the image (DBZ3/yae3_xenon.xex) and so does
          // the game data, so this is the difference between "Play does nothing"
          // and the game starting.
          iso_path = dbz3::settings::FindIsoInDir(game_dir);
          if (iso_path.empty()) {
            iso_path = dbz3::settings::FindIsoInDir(exe_dir);
          }
          if (!iso_path.empty()) {
            REXLOG_INFO(
                "OnConfigurePaths - folder cannot boot ({}) - using the disc "
                "image next to the game instead: {}",
                dbz3::settings::XexStatusLabel(boot.status), iso_path.string());
          }
        }
        if (!iso_path.empty()) {
          dbz3::settings::SetIsoPath(iso_path.string());
          const auto iso_cache = dbz3::settings::UserDataRoot() / "iso_cache";
          const auto xex_dst = iso_cache / "default.xex";
          // Re-extract when the source disc changed: the cache is only the
          // region-detection copy, and a stale one (from a different ISO) makes
          // the launcher pick the wrong core/region for the disc actually being
          // played -> "No function registered at <addr>" at boot.
          if (!dbz3::settings::EnsureIsoXexCache(iso_path, iso_cache)) {
            REXLOG_ERROR("OnConfigurePaths - could not extract default.xex from {}",
                         iso_path.string());
          }
          if (std::filesystem::is_regular_file(xex_dst)) {
            // The disc image is mounted as the game drive at Play time, so the
            // runtime would load game:\default.xex straight from the disc - which
            // on a RETAIL HD Collection disc is the collection's menu, not
            // Budokai 3. Serve the cached executable instead: it is identical for
            // a repacked image and the real executable when the disc keeps it at
            // DBZ3/yae3_xenon.xex. Same trick for the assets: the retail disc
            // stores them under DBZ3/, so us\... is resolved there too.
            dbz3::settings::BootSource iso_boot;
            iso_boot.data_root = iso_cache;
            iso_boot.xex = xex_dst;
            iso_boot.redirect_default_xex = true;
            const std::string iso_xex_src = dbz3::settings::IsoXexSourcePath();
            iso_boot.iso_prefix_dbz3 = iso_xex_src.rfind("DBZ3", 0) == 0;
            iso_boot.status = dbz3::settings::ClassifyXexFile(xex_dst);
            iso_boot.note = "ISO: " + (iso_xex_src.empty() ? std::string("default.xex")
                                                           : iso_xex_src);
            // Only play from the image when it really carries this port's
            // executable: a disc whose candidates are all foreign (a menu-only
            // image, a corrupted one) must not send the runtime after a file it
            // cannot boot - keep the folder source so the banner explains it.
            if (!iso_boot.usable()) {
              REXLOG_WARN(
                  "OnConfigurePaths - disc image {} has no bootable Budokai 3 "
                  "executable ({}); keeping the folder source",
                  iso_path.string(), dbz3::settings::XexStatusLabel(iso_boot.status));
              dbz3::settings::SetIsoPath("");
            } else {
              dbz3::settings::SetCurrentBootSource(iso_boot);
              REXLOG_INFO(
                  "OnConfigurePaths - ISO xex source '{}' status {} prefix_dbz3={}",
                  iso_xex_src, dbz3::settings::XexStatusLabel(iso_boot.status),
                  iso_boot.iso_prefix_dbz3 ? "yes" : "no");
              game_dir_ = iso_cache;
              paths.game_data_root = iso_cache;
              dbz3::SetEffectiveGameRoot(iso_cache);
              REXLOG_INFO("OnConfigurePaths - ISO mode: {} (xex cache {})",
                          iso_path.string(), iso_cache.string());
            }
          } else {
            dbz3::settings::SetIsoPath("");
          }
        }
    }

    // Called when the main guest thread exits
    void OnGuestThreadExit(rex::system::XThread* thread) override {
        (void)thread;
        REXLOG_CRITICAL("=== GUEST THREAD EXITED ===");
        REXLOG_CRITICAL("This should not happen during normal gameplay - likely XamLoaderTerminateTitle called for module transition");
        REXLOG_INFO("Attempting to relaunch module after transition...");
        if (auto* rt = runtime()) {
            REXLOG_INFO("Runtime available, checking kernel state...");
            if (rt->kernel_state()) {
                REXLOG_INFO("Kernel state available, checking if title is terminating...");
                REXLOG_INFO("terminating_title flag: {}", rt->kernel_state()->is_terminating_title() ? "true" : "false");
            } else {
                REXLOG_ERROR("No kernel state available!");
            }
            REXLOG_INFO("Calling LaunchModule()....");
            if (auto main_thread = rt->LaunchModule()) {
                REXLOG_INFO("Module relaunched successfully");
                // una segunda transicion asignaba sobre un hilo aun "joinable": std::terminate (cierre)
                if (transition_thread_.joinable()) transition_thread_.detach();
                transition_thread_ = std::thread([this, main_thread = std::move(main_thread)]() mutable {
                    REXLOG_INFO("Waiting for relaunched module thread...");
                    main_thread->Wait(0, 0, 0, nullptr);
                    REXLOG_INFO("Execution complete after relaunch");
                    if (!shutting_down_.load(std::memory_order_acquire)) {
                        app_context().CallInUIThread([this]() {
                            app_context().QuitFromUIThread();
                        });
                    }
                });
            } else {
                REXLOG_ERROR("LaunchModule() returned null - failed to relaunch module");
                shutting_down_.store(true, std::memory_order_release);
                app_context().QuitFromUIThread();
            }
        } else {
            REXLOG_ERROR("No runtime available, quitting");
            shutting_down_.store(true, std::memory_order_release);
            app_context().QuitFromUIThread();
        }
    }

    // Called before cleanup begins
    void OnShutdown() override {
        REXLOG_INFO("OnShutdown called");
        dbz3::SessionEnd(true);   // partida cerrada bien: sus mods pasan a "ultima partida buena"
        if (drop_listener_added_ && window()) {
            window()->RemoveListener(&drop_listener_);
            drop_listener_added_ = false;
        }
    }

protected:
    // Gate the module launch behind the pre-game launcher dialog. The
    // launcher's Play button calls ReXApp::LaunchModule() directly.
    void LaunchModule() override {
        if (REXCVAR_GET(dbz3_skip_launcher)) {
            // R8 (pruebas): el mismo paso que JUGAR del launcher, escenarios de los juegos de PS2 del jugador
            if (std::getenv("DBZ3_TEST_ESCENARIOS_AUTO")) {
                dbz3::launcher::ModPipeline stages;
                stages.BuildStages();
                stages.Wait();
                REXLOG_INFO("dbz3: escenarios de PS2 (prueba): {}", stages.Output());
            }
            // Skip-launcher is a dev/test fast path. Still guard the XEX so a
            // missing/unrecognized executable does not hit the cryptic "No
            // function registered" guest exit.
            // The resolved boot source knows the executable even when it is not
            // named default.xex (retail disc dumps: DBZ3/yae3_xenon.xex).
            auto status = dbz3::settings::CurrentBootSource().status;
            if (dbz3::settings::CurrentBootSource().xex.empty()) {
                status = dbz3::settings::CheckDefaultXex(dbz3::EffectiveGameRoot());
            }
#if defined(DBZ3_DUAL_REGION)
            const bool xex_ok = (status == dbz3::settings::XexStatus::kUs ||
                                 status == dbz3::settings::XexStatus::kEu);
#elif defined(DBZ3_EU_VARIANT)
            const bool xex_ok = (status == dbz3::settings::XexStatus::kEu);
#else
            const bool xex_ok = (status == dbz3::settings::XexStatus::kUs);
#endif
            if (!xex_ok) {
#if defined(DBZ3_DUAL_REGION)
                REXLOG_ERROR(
                    "dbz3: skip_launcher without a recognized default.xex "
                    "(expected US/NA yae3_xenon.xex or EU/PAL yae3_xenon_eu.xex)");
                if (!std::getenv("DBZ3_NO_DIALOGS")) rex::ShowSimpleMessageBox(
                    rex::SimpleMessageBoxType::Error,
                    "default.xex is missing or unrecognized. The dual-region core "
                    "boots either the US/NA (yae3_xenon.xex) or EU/PAL "
                    "(yae3_xenon_eu.xex) executable.");
#elif defined(DBZ3_EU_VARIANT)
                REXLOG_ERROR(
                    "dbz3: skip_launcher with US/NA default.xex - this EU/PAL core "
                    "only supports the EU/PAL executable");
                if (!std::getenv("DBZ3_NO_DIALOGS")) rex::ShowSimpleMessageBox(
                    rex::SimpleMessageBoxType::Error,
                    "default.xex is the US/NA executable but this is the EU/PAL "
                    "core. Replace default.xex with your EU/PAL one "
                    "(yae3_xenon_eu.xex).");
#else
                REXLOG_ERROR(
                    "dbz3: skip_launcher with EU/PAL default.xex - the recompiled "
                    "port only supports the US/NA executable");
                if (!std::getenv("DBZ3_NO_DIALOGS")) rex::ShowSimpleMessageBox(
                    rex::SimpleMessageBoxType::Error,
                    "default.xex is the EU/PAL executable. This core is recompiled "
                    "only from the US/NA one (yae3_xenon.xex) and cannot boot it. "
                    "Replace default.xex with your US/NA copy.");
#endif
                return;
            }
// Diagnostic (optional): dump the decrypted guest image for offline
            // function pointer analysis when DBZ3_DUMP_IMAGE=1 is set.
            if (std::getenv("DBZ3_DUMP_IMAGE")) {
                if (auto* rt = runtime()) {
                    uint8_t* mb = rt->virtual_membase();
                    if (mb) {
                        const char* name = (status == dbz3::settings::XexStatus::kEu)
                                               ? "dbz3_eu_image.bin"
                                               : "dbz3_us_image.bin";
                        FILE* f = fopen(name, "wb");
                        if (f) {
                            fwrite(mb + 0x82000000, 1, 0x826D0000 - 0x82000000, f);
                            fclose(f);
                            REXLOG_INFO("dbz3: dumped guest image to {}", name);
                        }
                    }
                }
            }
            // Sin launcher tambien queda en el log la linea `entorno` (version de cada
            // componente, SO, RAM): la necesitan el diagnostico del Mod Kit y los reportes.
            dbz3::launcher::InstalledComponents();
            // Arranque rapido desde la ISO: la misma copia de sus .afs que hace JUGAR (una vez, segundos) y se vuelve
            // a montar el disco para que la sirva
            if (dbz3::settings::IsIsoMode()) {
                if (dbz3::settings::IsoDataPending(false) > 0) {
                    std::string error;
                    if (dbz3::settings::PrepareIsoData(false, nullptr, &error)) {
                        dbz3::RelocateGameData(dbz3::EffectiveGameRoot());
                    }
                }
            }
            REXLOG_INFO("dbz3: skip_launcher set, booting directly");
            ReXApp::LaunchModule();
            return;
        }
        REXLOG_INFO("dbz3: launcher shown, waiting for Play");
    }

    // Handle window close request (Alt+F4 / the window X button on Windows).
    // Observed hang: closing during gameplay reached this point ("Window close
    // requested" logged) but never proceeded to OnClosing's hard exit - the
    // window stayed "Not Responding" until the process was killed. TerminateTitle
    // / PerformClose / focus-loss can block on a straggler guest thread, so just
    // hard-exit here instead: the user asked to close the app. OnClosing() keeps
    // the same hard-exit as a safety net for other paths.
    bool OnWindowCloseRequested() override {
        REXLOG_INFO("Window close requested - exiting dbz3");
        shutting_down_.store(true, std::memory_order_release);
        // salida normal del jugador (OnShutdown no llega: _Exit): sus mods pasan a "ultima partida buena"
        try { dbz3::SessionEnd(true); } catch (...) {}
        dbz3::net::UpnpCloseMapped();   // _Exit no pasa por atexit: el puerto del router se cierra aqui
        rex::FlushLogging();
        std::_Exit(0);
    }

private:
    std::unique_ptr<DebugOverlayDialog> debug_overlay_;
    dbz3::launcher::LauncherDialog* launcher_dialog_ = nullptr;
    std::unique_ptr<dbz3::ingame::InGameMenu> ingame_menu_;
    std::unique_ptr<dbz3::ingame::OnlinePanel> online_panel_;
    std::unique_ptr<dbz3::ingame::TagTeamPanel> tagteam_panel_;
    std::atomic<bool> launched_{false};
    std::atomic<bool> shutting_down_{false};
    std::thread transition_thread_;
    rex::ui::ImGuiDrawer* imgui_drawer_ = nullptr;
    // Arrastrar y soltar sobre la ventana: mods (.zip) y texturas editadas (launcher).
    struct DropListener : rex::ui::WindowListener {
        void OnFileDrop(rex::ui::FileDropEvent& e) override {
            dbz3::launcher::QueueDroppedFile(e.filename());
        }
    } drop_listener_;
    bool drop_listener_added_ = false;
    // Project root used to build the region/mod overlay (set in OnConfigurePaths).
    std::filesystem::path game_dir_;
    static std::atomic<bool> crash_logged_;

    static void SetupCrashHandler() {
#if REX_PLATFORM_WIN32
        SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
            if (crash_logged_.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
            // A debugger is attached: let it take the exception (don't swallow
            // it and don't pop a dialog while debugging).
            if (IsDebuggerPresent()) return EXCEPTION_CONTINUE_SEARCH;

            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            char timestamp[64];
            struct tm local_tm;
            localtime_s(&local_tm, &time_t);
            strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &local_tm);
            char dump_path[MAX_PATH] = {};
            // Writing the minidump is optional (Dev tab toggle). Default off so
            // the game folder stays clean; the exception is still logged.
            if (dbz3::settings::CrashDumpEnabled()) {
              snprintf(dump_path, sizeof(dump_path), "crash_%s.dmp", timestamp);
              HANDLE hFile = CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
              if (hFile != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei;
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers = FALSE;
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(hFile);
              }
            }
            const uint32_t code = ep->ExceptionRecord->ExceptionCode;
            const void* addr = ep->ExceptionRecord->ExceptionAddress;
            REXLOG_CRITICAL("UNHANDLED EXCEPTION: Code=0x{:08X} Addr={:p}", code, addr);
            try { dbz3::SessionEnd(false); } catch (...) {}   // el launcher lo vera al abrir
            // Diagnostic: dump the faulting x64 context and, if RCX points at a
            // PPCContext (recompiled code keeps the guest context in RCX), dump
            // the guest register state that matters for indirect dispatches.
            if (ep->ContextRecord && ep->ContextRecord->ContextFlags & CONTEXT_AMD64) {
                auto* c = ep->ContextRecord;
                REXLOG_CRITICAL("fault ctx: RIP={:p} RSP={:p} RBP={:p}", (void*)c->Rip, (void*)c->Rsp, (void*)c->Rbp);
                REXLOG_CRITICAL("fault ctx: RAX=0x{:016X} RCX=0x{:016X} RDX=0x{:016X} R8=0x{:016X} R9=0x{:016X}",
                                c->Rax, c->Rcx, c->Rdx, c->R8, c->R9);
                REXLOG_CRITICAL("fault ctx: RDI=0x{:016X} RSI=0x{:016X} R10=0x{:016X} R11=0x{:016X}", c->Rdi, c->Rsi, c->R10, c->R11);
                // PPCContext layout (rex/ppc/context.h): r3@0x00 r0@0x08 r1@0x10
                // r2@0x18 r4@0x20 r5@0x28 ... r10@0x50 r11@0x58 ... r31@0xF8
                // lr@0x100 ctr@0x108 xer@0x110 cr0@0x124 ... cr6@0x13C
                rex::FlushLogging();   // lo esencial queda escrito aunque lo de abajo fallara
                // En un fallo de lectura del guest, RCX suele ser la direccion que fallo, NO el
                // PPCContext. Escaneamos todos los registros buscando el que apunte a un bloque
                // legible cuyo LR (@+0x100) sea una direccion de codigo del guest, para recuperar
                // el estado del guest (LR + registros) en el punto exacto del fallo.
                const auto readable_ctx = [](const uint8_t* g) {
                  if (reinterpret_cast<uintptr_t>(g) < 0x10000) return false;
                  MEMORY_BASIC_INFORMATION mbi = {};
                  return VirtualQuery(g, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT &&
                         !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                         reinterpret_cast<uintptr_t>(g) + 0x140 <=
                             reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                };
                const struct { const char* n; uint64_t v; } cands[] = {
                    {"RCX", c->Rcx}, {"RDI", c->Rdi}, {"RSI", c->Rsi}, {"RDX", c->Rdx},
                    {"R8", c->R8},   {"R9", c->R9},   {"R10", c->R10}, {"R11", c->R11},
                    {"RAX", c->Rax}, {"RBX", c->Rbx}, {"RBP", c->Rbp},
                };
                for (const auto& cd : cands) {
                  const uint8_t* g = reinterpret_cast<const uint8_t*>(cd.v);
                  if (!readable_ctx(g)) continue;
                  uint64_t lr = 0;
                  std::memcpy(&lr, g + 0x100, 8);
                  if (lr < 0x82000000ull || lr >= 0x83000000ull) continue;   // LR = codigo del guest
                  const auto rd32 = [&](size_t o) { uint32_t v; std::memcpy(&v, g + o, 4); return v; };
                  REXLOG_CRITICAL("guest ctx via {} = 0x{:016X}", cd.n, cd.v);
                  REXLOG_CRITICAL("guest: r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X}", rd32(0x00), rd32(0x20), rd32(0x28), rd32(0x30));
                  REXLOG_CRITICAL("guest: r7=0x{:08X} r8=0x{:08X} r9=0x{:08X} r10=0x{:08X}", rd32(0x38), rd32(0x40), rd32(0x48), rd32(0x50));
                  REXLOG_CRITICAL("guest: r11=0x{:08X} r28=0x{:08X} r29=0x{:08X} r30=0x{:08X} r31=0x{:08X}",
                                  rd32(0x58), rd32(0xE0), rd32(0xE8), rd32(0xF0), rd32(0xF8));
                  REXLOG_CRITICAL("guest: lr=0x{:08X} ctr=0x{:08X} xer=0x{:08X} cr6=0x{:02X}",
                                  uint32_t(lr), rd32(0x108), rd32(0x110), g[0x13C]);
                  break;
                }
            }
            // Flush the log so the message box below points at a complete file.
            rex::FlushLogging();
            // en el idioma del jugador (antes solo en espanol)
            dbz3::i18n::SetLanguage(dbz3::settings::Language());
            using dbz3::i18n::T;
            char msg[4096];
            int len = snprintf(msg, sizeof(msg), "%s\n\n%s: 0x%08X (%p)\n\n",
                               T("DBZ Budokai 3 HD Collection se cerro inesperadamente.",
                                 "DBZ Budokai 3 HD Collection closed unexpectedly."),
                               T("Excepcion", "Exception"), code, addr);
            if (len < 0 || len >= int(sizeof(msg))) len = 0;
            const std::string log_path = dbz3::settings::LatestLogPath().string();
            if (!log_path.empty()) {
              snprintf(msg + len, sizeof(msg) - static_cast<size_t>(len), "%s\n%s\n",
                       T("El registro del juego esta en:", "The game log is in:"), log_path.c_str());
            } else {
              snprintf(msg + len, sizeof(msg) - static_cast<size_t>(len), "%s\n",
                       T("El registro no se pudo localizar (logs/ junto al juego).",
                         "The log could not be found (logs/ next to the game)."));
            }
            if (dump_path[0] != '\0') {
              size_t cur = strnlen(msg, sizeof(msg));
              snprintf(msg + cur, sizeof(msg) - cur, "\n%s\n%s\n",
                       T("Minidump guardado en:", "Minidump saved to:"), dump_path);
            }
            size_t end = strnlen(msg, sizeof(msg));
            snprintf(msg + end, sizeof(msg) - end, "\n%s",
                     T("Si el problema persiste, comparte el registro para diagnostico.",
                       "If the problem persists, share the log so it can be diagnosed."));
            // pruebas automaticas (DBZ3_NO_DIALOGS): sin ventana encima del escritorio del usuario
            if (!std::getenv("DBZ3_NO_DIALOGS"))
                MessageBoxA(nullptr, msg, "DBZ Budokai 3 - Error",
                            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
            return EXCEPTION_EXECUTE_HANDLER;
        });
        std::set_terminate([]() {
            REXLOG_CRITICAL("std::terminate called!");
            // Log the failing thread's stack (the terminate handler runs
            // synchronously from the exception unwinder, so the captured frames
            // include the throw site). Helps diagnose uncaught exceptions from
            // user crash reports.
            void* frames[64] = {};
            USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
            for (USHORT i = 0; i < n; ++i) {
                REXLOG_CRITICAL("terminate stack[{:02d}]: {:p}", i, frames[i]);
            }
            rex::FlushLogging();
            crash_logged_ = true;   // el abort() de abajo no saca otra ventana (SIGABRT)
            if (!std::getenv("DBZ3_NO_DIALOGS"))
            dbz3::i18n::SetLanguage(dbz3::settings::Language());
            MessageBoxA(nullptr,
                        dbz3::i18n::T("DBZ Budokai 3 HD Collection se cerro inesperadamente "
                                      "(terminacion del runtime). Consulta el registro en "
                                      "logs/ junto al juego.",
                                      "DBZ Budokai 3 HD Collection closed unexpectedly "
                                      "(runtime termination). See the log in logs/ next to "
                                      "the game."),
                        "DBZ Budokai 3 - Error",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
            std::abort();
        });
        // abort() sin excepcion (rex::FatalError: GPU que no arranca o se pierde) no pasa por lo de arriba: el
        // jugador solo veia la pantalla en negro y el cierre. Ventana con el registro y la prueba de Vulkan.
        std::signal(SIGABRT, [](int) {
            if (crash_logged_.exchange(true)) return;
            REXLOG_CRITICAL("abort(): error fatal del runtime (grafica o arranque)");
            rex::FlushLogging();
            if (std::getenv("DBZ3_NO_DIALOGS")) return;
            dbz3::i18n::SetLanguage(dbz3::settings::Language());
            const std::string log_path = dbz3::settings::LatestLogPath().string();
            const std::string msg =
                std::string(dbz3::i18n::T("DBZ Budokai 3 HD Collection se cerro por un error grave (normalmente de la "
                                          "tarjeta grafica).",
                                          "DBZ Budokai 3 HD Collection closed because of a fatal error (usually the "
                                          "graphics card).")) +
                "\n\n" + dbz3::i18n::T("El registro del juego esta en:", "The game log is in:") + "\n" +
                (log_path.empty() ? std::string("logs/") : log_path) + "\n\n" +
                dbz3::i18n::T("Prueba en el lanzador: Motor grafico -> Vulkan, y actualiza el driver de la grafica. "
                              "Si sigue, comparte el registro.",
                              "Try in the launcher: Graphics engine -> Vulkan, and update your graphics driver. "
                              "If it persists, share the log.");
            MessageBoxA(nullptr, msg.c_str(), "DBZ Budokai 3 - Error",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        });
#endif
    }
};

std::atomic<bool> Dbz3App::crash_logged_{false};

XE_DEFINE_WINDOWED_APP(dbz3, Dbz3App::Create)

// External kernel imports (stubs for Windows kernel functions)
REX_EXTERN(__imp__IoInvalidDeviceRequest);
REX_EXTERN(__imp__ObReferenceObject);
REX_EXTERN(__imp__IoDeleteDevice);
REX_EXTERN(__imp__IoCompleteRequest);
REX_EXTERN(__imp__NtWriteFileGather);
REX_EXTERN(__imp__RtlUpcaseUnicodeChar);
REX_EXTERN(__imp__ObIsTitleObject);
REX_EXTERN(__imp__IoCheckShareAccess);
REX_EXTERN(__imp__IoSetShareAccess);
REX_EXTERN(__imp__IoRemoveShareAccess);
REX_EXTERN(__imp__XeCryptBnQwBeSigVerify);
REX_EXTERN(__imp__XeKeysGetKey);
REX_EXTERN(__imp__XeCryptRotSumSha);

