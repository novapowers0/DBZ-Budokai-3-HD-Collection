// dbz3 - ReXGlue Recompiled Project
// Dragon Ball Z Budokai 3 (PAL / Xbox 360 HD Collection)
// Adapted from original Budokai 1 main.cpp

#include "generated/dbz3_init.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/function.h>
#include <rex/system/xthread.h>
#include <rex/system/kernel_state.h>
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
#include "launcher/settings.h"
#include "launcher/launcher_state.h"
#include "ingame/menu.h"
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

#include <atomic>
#include <filesystem>
#include <thread>
#include <exception>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

class DebugOverlayDialog : public rex::ui::ImGuiDialog {
public:
    DebugOverlayDialog(rex::ui::ImGuiDrawer* imgui_drawer)
        : ImGuiDialog(imgui_drawer) {}
protected:
    void OnDraw(ImGuiIO& io) override {
        // Only visible when the user enables the FPS counter (Dev tab).
        if (!dbz3::settings::ShowFps()) {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(220, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::Begin("Debug##overlay", nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        }
        ImGui::End();
    }
};

class Dbz3App : public rex::ReXApp {
public:
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
        return std::make_unique<Dbz3App>(ctx);
    }

    Dbz3App(rex::ui::WindowedAppContext& ctx)
        : ReXApp(ctx, "dbz3", PPCImageConfig, "[game_directory]") {
        OutputDebugStringA("Dbz3App constructor START\n");
        AddPositionalOption("game_directory");
        OutputDebugStringA("Dbz3App constructor END\n");
    }

    // Called before Runtime::Setup() - configure GPU plugin
    void OnPreSetup(rex::RuntimeConfig& config) override {
        OutputDebugStringA("OnPreSetup START\n");
        config.gpu_plugin = "xenos";
        config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
        config.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
        config.kernel_init = rex::kernel::InitializeKernel;
        OutputDebugStringA("OnPreSetup END\n");

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
        OutputDebugStringA("OnPostSetup START\n");
        REXLOG_INFO("OnPostSetup - Runtime initialized, graphics system should be ready");
        // Allow draws with invalid fetch constants (shadow passes etc). Mirrors
        // what dbz1's ApplyUserSettingsToSdk does with the user toml. Must run
        // after the GPU plugin is loaded so the cvar registry is live.
        rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
        // Runtime/GPU cvars (vsync, MSAA, aniso, FSR) only exist now that the
        // GPU plugin has registered them.
        dbz3::settings::ApplyRuntimeSettingsToSdk(false);
        SetupCrashHandler();
        OutputDebugStringA("OnPostSetup END\n");
    }

    // Called after ImGui drawer is created - add custom dialogs
    void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
        OutputDebugStringA("OnCreateDialogs START\n");
        if (!debug_overlay_) {
            debug_overlay_ = std::make_unique<DebugOverlayDialog>(drawer);
        }
        imgui_drawer_ = drawer;

        // Pre-game launcher. Its "Play" button dismisses the dialog and
        // triggers the gated module launch (see LaunchModule override below).
        // NOTE: the dialog self-deletes on Close(), so we hold a raw pointer
        // (not unique_ptr) and null it from the on_play callback.
        if (!launcher_dialog_) {
            launcher_dialog_ = new dbz3::launcher::LauncherDialog(
                drawer, [this]() {
                    launcher_dialog_ = nullptr;
                    // Persist the launcher choices (region, mods, language,
                    // video, audio, input) before launching so they are applied
                    // on the next boot even if the user forgot "Save settings".
                    dbz3::settings::SaveUserSettings();
                    // The region/mod overlay is built in OnConfigurePaths, but
                    // the launcher lets the user change region/mods afterwards.
                    // Rebuild it now (before the guest module launches) so the
                    // selected region and enabled mods take effect immediately.
                    // The VFS mounts the overlay directory, and PrepareRegionData
                    // re-populates it from the chosen region + mods.
                    if (!game_dir_.empty()) {
                        try {
                            dbz3::settings::PrepareRegionData(game_dir_);
                        } catch (const std::exception& e) {
                            REXLOG_ERROR("dbz3: rebuild region overlay on Play failed ({}), "
                                         "continuing with the overlay from startup", e.what());
                        }
                    }
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
            if (!imgui_drawer_) return;
            if (launcher_dialog_) return;
            launcher_dialog_ = new dbz3::launcher::LauncherDialog(
                imgui_drawer_, [this]() { launcher_dialog_ = nullptr; });
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
        });

        OutputDebugStringA("OnCreateDialogs END\n");
    }

    // Called immediately before the main guest thread is created
    void OnPreLaunchModule() override {
        OutputDebugStringA("OnPreLaunchModule START\n");
        REXLOG_INFO("OnPreLaunchModule - about to launch guest thread");
        OutputDebugStringA("OnPreLaunchModule END\n");
    }

    // Called after the main guest thread is created but before it starts executing
    void OnPostLaunchModule(rex::system::XThread* thread) override {
        (void)thread;
        OutputDebugStringA("OnPostLaunchModule START\n");
        REXLOG_INFO("OnPostLaunchModule - guest thread created and resumed");
        launched_.store(true, std::memory_order_release);
        if (auto* rt = runtime()) {
            OutputDebugStringA("OnPostLaunchModule - runtime available\n");
            if (window()) {
                rt->set_display_window(window());
                OutputDebugStringA("Set display window on runtime\n");
            } else {
                OutputDebugStringA("Window is null!\n");
            }
            if (imgui_drawer_) {
                rt->set_imgui_drawer(imgui_drawer_);
                OutputDebugStringA("Set ImGui drawer on runtime\n");
            } else {
                OutputDebugStringA("ImGui drawer is null!\n");
            }
        } else {
            OutputDebugStringA("Runtime is null!\n");
        }
        OutputDebugStringA("OnPostLaunchModule END\n");
    }

    // Called after path defaults are computed, before Runtime is constructed
    void OnConfigurePaths(rex::PathConfig& paths) override {
        auto exe_dir = rex::filesystem::GetExecutableFolder();
        OutputDebugStringA("OnConfigurePaths START\n");
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
        // D:\us\ resolves to project_root/us/ (the actual game data).
        // Fall back to exe_dir/.. if not found.
        std::filesystem::path game_dir;
        if (auto arg = GetArgument("game_directory")) {
            game_dir = *arg;
            REXLOG_INFO("OnConfigurePaths - game_dir from arg: {}", game_dir.string());
        } else {
            // Priority order for locating the game assets:
            //   1) next to the exe (standalone release layout: dbz3.exe + us/ + default.xex)
            //   2) the project root (dev layout: out/build/win-amd64-release, 3 levels up)
            //   3) the parent of the exe folder
            if (std::filesystem::is_directory(exe_dir / "us")) {
                game_dir = exe_dir;
                REXLOG_INFO("OnConfigurePaths - game_dir default (next to exe): {}", game_dir.string());
            } else {
                auto project_root = exe_dir.parent_path().parent_path().parent_path();
                if (std::filesystem::is_directory(project_root / "us")) {
                    game_dir = project_root;
                    REXLOG_INFO("OnConfigurePaths - game_dir default (project root): {}", game_dir.string());
                } else {
                    game_dir = exe_dir.parent_path();
                    REXLOG_INFO("OnConfigurePaths - game_dir default (parent): {}", game_dir.string());
                }
            }
        }
        REXLOG_INFO("OnConfigurePaths - game_dir final: {}", game_dir.string());
        // Region + mods overlay: build an "active_region" overlay next to the
        // exe that layers mod files over the chosen region's assets, and mount
        // that as the game drive. The runtime loads game:\default.xex and the
        // game reads D:\us\... which resolve inside the overlay, so the US
        // binary plays with the selected region's text/audio/video packs.
        game_dir_ = game_dir;
        try {
            paths.game_data_root = dbz3::settings::PrepareRegionData(game_dir);
        } catch (const std::exception& e) {
            REXLOG_ERROR("dbz3: PrepareRegionData failed ({}), falling back to project root",
                         e.what());
            paths.game_data_root = game_dir;
        }
        paths.user_data_root = exe_dir / "user_data" / GetName();
        paths.cache_root = paths.user_data_root / "cache";
        paths.metadata_root = exe_dir / "metadata";
        REXLOG_INFO("OnConfigurePaths - game_data_root set to: {}", paths.game_data_root.string());
        OutputDebugStringA("OnConfigurePaths END\n");
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
    }

protected:
    // Gate the module launch behind the pre-game launcher dialog. The
    // launcher's Play button calls ReXApp::LaunchModule() directly.
    void LaunchModule() override {
        if (REXCVAR_GET(dbz3_skip_launcher)) {
            REXLOG_INFO("dbz3: skip_launcher set, booting directly");
            ReXApp::LaunchModule();
            return;
        }
        REXLOG_INFO("dbz3: launcher shown, waiting for Play");
    }

    // Handle window close request
    bool OnWindowCloseRequested() override {
        REXLOG_INFO("Window close requested");
        shutting_down_.store(true, std::memory_order_release);
        if (auto* rt = runtime(); rt && rt->kernel_state()) {
            rt->kernel_state()->TerminateTitle();
        }
        return true;
    }

private:
    std::unique_ptr<DebugOverlayDialog> debug_overlay_;
    dbz3::launcher::LauncherDialog* launcher_dialog_ = nullptr;
    std::unique_ptr<dbz3::ingame::InGameMenu> ingame_menu_;
    std::atomic<bool> launched_{false};
    std::atomic<bool> shutting_down_{false};
    std::thread transition_thread_;
    rex::ui::Window* window_ = nullptr;
    rex::ui::ImGuiDrawer* imgui_drawer_ = nullptr;
    // Project root used to build the region/mod overlay (set in OnConfigurePaths).
    std::filesystem::path game_dir_;
    static std::atomic<bool> crash_logged_;

    static void SetupCrashHandler() {
#if REX_PLATFORM_WIN32
        SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
            if (crash_logged_.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&time_t));
            char dump_path[MAX_PATH];
            snprintf(dump_path, sizeof(dump_path), "crash_%s.dmp", timestamp);
            // Writing the minidump is optional (Dev tab toggle). Default off so
            // the game folder stays clean; the exception is still logged.
            if (dbz3::settings::CrashDumpEnabled()) {
              HANDLE hFile = CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
              if (hFile != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei;
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers = FALSE;
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(hFile);
                OutputDebugStringA("Crash dump written to ");
                OutputDebugStringA(dump_path);
                OutputDebugStringA("\n");
              }
            }
            REXLOG_CRITICAL("UNHANDLED EXCEPTION: Code=0x{:08X} Addr={:p}", 
                ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
            return EXCEPTION_EXECUTE_HANDLER;
        });
        std::set_terminate([]() {
            REXLOG_CRITICAL("std::terminate called!");
            std::abort();
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