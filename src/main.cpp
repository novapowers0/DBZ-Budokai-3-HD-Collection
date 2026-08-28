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
#include "region.h"
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstring>

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
        OutputDebugStringA("Dbz3App constructor START\n");
        AddPositionalOption("game_directory");
        OutputDebugStringA("Dbz3App constructor END\n");
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
                    // the runtime's initial VFS setup. Re-apply the region mount
                    // now (before the guest module launches) so game:\us points
                    // at the currently selected region's assets. Mods (per-entry
                    // AFS and whole-file) are served directly from mods/ by the
                    // runtime's override hooks, with no overlay or duplication.
                    dbz3::ApplyRegionMount();
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
        // Re-apply the region device mount so game:\us points at the currently
        // selected region's assets (covers the skip-launcher fast path too).
        dbz3::ApplyRegionMount();
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
        // D:\us\ resolves to <game_dir>/us/ (the actual game data).
        // Both layouts are supported:
        //   - dev:      <root>/us, <root>/eu (project root next to out/build)
        //   - standalone release: <exe_dir>/us or <exe_dir>/assets/{default.xex,us,eu}
        // Fall back to the exe folder if nothing is found.
        auto FindGameRoot = [](const std::filesystem::path& base) -> std::filesystem::path {
            // Accept either region folder (us/ or eu/), in both the flat layout
            // (base/us, base/eu) and the assets/ layout (base/assets/us,
            // base/assets/eu). An EU-only user has eu/ but no us/ — rejecting
            // it made auto-detection fall back to the exe folder and fail with
            // "Entrypoint XEX not found". Matches IsValidGameDataDir/banner.
            if (std::filesystem::is_directory(base / "us"))
                return base;
            if (std::filesystem::is_directory(base / "eu"))
                return base;
            if (std::filesystem::is_directory(base / "assets" / "us"))
                return base / "assets";
            if (std::filesystem::is_directory(base / "assets" / "eu"))
                return base / "assets";
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
        // Use the game folder directly as the game drive root (no overlay, no
        // duplicate assets). The runtime mounts game:\ to game_data_root and the
        // game reads D:\us\... which resolves to game_dir/us (or, for the eu
        // region, to game_dir/eu via the ApplyRegionMount device). Mods are
        // served by the runtime's AFS/whole-file override hooks from mods/,
        // without copying anything.
        game_dir_ = game_dir;
        paths.game_data_root = game_dir;
        // Keep the effective game data root in sync so region mounting (which
        // reads this root, not the runtime's fixed copy) always uses the folder
        // we resolved here.
        dbz3::SetEffectiveGameRoot(game_dir);
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
            // Skip-launcher is a dev/test fast path. Still guard the XEX so a
            // missing/unrecognized executable does not hit the cryptic "No
            // function registered" guest exit.
            auto status = dbz3::settings::CheckDefaultXex(dbz3::EffectiveGameRoot());
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
                rex::ShowSimpleMessageBox(
                    rex::SimpleMessageBoxType::Error,
                    "default.xex is missing or unrecognized. The dual-region core "
                    "boots either the US/NA (yae3_xenon.xex) or EU/PAL "
                    "(yae3_xenon_eu.xex) executable.");
#elif defined(DBZ3_EU_VARIANT)
                REXLOG_ERROR(
                    "dbz3: skip_launcher with US/NA default.xex - this EU/PAL core "
                    "only supports the EU/PAL executable");
                rex::ShowSimpleMessageBox(
                    rex::SimpleMessageBoxType::Error,
                    "default.xex is the US/NA executable but this is the EU/PAL "
                    "core. Replace default.xex with your EU/PAL one "
                    "(yae3_xenon_eu.xex).");
#else
                REXLOG_ERROR(
                    "dbz3: skip_launcher with EU/PAL default.xex - the recompiled "
                    "port only supports the US/NA executable");
                rex::ShowSimpleMessageBox(
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
        rex::FlushLogging();
        std::_Exit(0);
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
            // A debugger is attached: let it take the exception (don't swallow
            // it and don't pop a dialog while debugging).
            if (IsDebuggerPresent()) return EXCEPTION_CONTINUE_SEARCH;

            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&time_t));
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
                const uint8_t* g = reinterpret_cast<const uint8_t*>(c->Rcx);
                const auto rd32 = [&](size_t o) { uint32_t v; std::memcpy(&v, g + o, 4); return v; };
                const auto rd64 = [&](size_t o) { uint64_t v; std::memcpy(&v, g + o, 8); return v; };
                REXLOG_CRITICAL("guest(ctx=0x{:016X}): r3=0x{:08X} r4=0x{:08X} r11=0x{:08X}", c->Rcx, rd32(0x00), rd32(0x20), rd32(0x58));
                REXLOG_CRITICAL("guest: r28=0x{:08X} r29=0x{:08X} r30=0x{:08X} r31=0x{:08X}", rd32(0xE0), rd32(0xE8), rd32(0xF0), rd32(0xF8));
                REXLOG_CRITICAL("guest: lr=0x{:08X} ctr=0x{:08X} xer=0x{:08X} cr6=0x{:02X}",
                                rd64(0x100), rd32(0x108), rd32(0x110), g[0x13C]);
            }
            // Flush the log so the message box below points at a complete file.
            rex::FlushLogging();
            char msg[4096];
            int len = snprintf(msg, sizeof(msg),
                               "DBZ Budokai 3 HD Collection se cerro inesperadamente.\n\n"
                               "Excepcion: 0x%08X en %p\n\n",
                               code, addr);
            const std::string log_path = dbz3::settings::LatestLogPath().string();
            if (!log_path.empty()) {
              snprintf(msg + len, sizeof(msg) - static_cast<size_t>(len),
                       "El registro del juego esta en:\n%s\n", log_path.c_str());
            } else {
              snprintf(msg + len, sizeof(msg) - static_cast<size_t>(len),
                       "El registro no se pudo localizar (logs/ junto al juego).\n");
            }
            if (dump_path[0] != '\0') {
              size_t cur = strnlen(msg, sizeof(msg));
              snprintf(msg + cur, sizeof(msg) - cur,
                       "\nMinidump guardado en:\n%s\n", dump_path);
            }
            size_t end = strnlen(msg, sizeof(msg));
            snprintf(msg + end, sizeof(msg) - end,
                     "\nSi el problema persiste, comparte el registro para diagnostico.");
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
            MessageBoxA(nullptr,
                        "DBZ Budokai 3 HD Collection se cerró inesperadamente "
                        "(terminación del runtime). Consulta el registro en "
                        "logs/ junto al juego.",
                        "DBZ Budokai 3 - Error",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
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