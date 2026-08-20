// dbz3 - Pre-game launcher screen implementation.
// Dark modern style with Dragon Ball accent colors (orange/blue).

#include "launcher_state.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "settings.h"

#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace dbz3::launcher {

namespace {

// DBZ-inspired palette.
constexpr ImVec4 kDragonOrange(0.96f, 0.54f, 0.10f, 1.0f);
constexpr ImVec4 kDragonOrangeDim(0.70f, 0.40f, 0.08f, 1.0f);
constexpr ImVec4 kDragonBlue(0.20f, 0.45f, 0.85f, 1.0f);
constexpr ImVec4 kPanelBg(0.10f, 0.11f, 0.13f, 1.0f);
constexpr ImVec4 kPanelBgAlt(0.14f, 0.15f, 0.18f, 1.0f);
constexpr ImVec4 kTextMain(0.92f, 0.92f, 0.94f, 1.0f);
constexpr ImVec4 kTextDim(0.55f, 0.57f, 0.62f, 1.0f);

// Double slider with named min/max (avoids rvalue-address issues with clang).
bool SliderD(const char* label, double* value, double min, double max, const char* fmt) {
  return ImGui::SliderScalar(label, ImGuiDataType_Double, value, &min, &max, fmt);
}

void PushSectionHeader(const char* title) {
  ImGui::TextColored(kDragonOrange, "%s", title);
  ImGui::Separator();
  ImGui::Spacing();
}

// Abre el dialogo nativo de Windows para elegir una carpeta. Devuelve true y
// rellena `out` si el usuario eligio una; false si la cancelo.
bool PickFolder(std::string& out, const std::string& initial) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOpenDialog* dlg = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (!initial.empty()) {
      // Intentar partir de la carpeta actual (si existe).
      std::filesystem::path init = initial;
      if (std::filesystem::is_directory(init)) {
        IShellItem* item = nullptr;
        std::wstring winit = init.wstring();
        if (SUCCEEDED(SHCreateItemFromParsingName(winit.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item)))) {
          dlg->SetFolder(item);
          item->Release();
        }
      }
    }
    if (SUCCEEDED(dlg->Show(nullptr))) {
      IShellItem* res = nullptr;
      if (SUCCEEDED(dlg->GetResult(&res))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          std::wstring wpath(path);
          out.assign(wpath.begin(), wpath.end());
          CoTaskMemFree(path);
          ok = true;
        }
        res->Release();
      }
    }
    dlg->Release();
  }
  CoUninitialize();
  return ok;
}

}  // namespace

void LauncherDialog::ApplyTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.ChildRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.WindowPadding = ImVec2(16, 16);
  style.FramePadding = ImVec2(10, 6);
  style.ItemSpacing = ImVec2(10, 8);
  style.Colors[ImGuiCol_WindowBg] = kPanelBg;
  style.Colors[ImGuiCol_ChildBg] = kPanelBgAlt;
  style.Colors[ImGuiCol_Text] = kTextMain;
  style.Colors[ImGuiCol_TextDisabled] = kTextDim;
  style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.27f, 1.0f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.29f, 0.33f, 1.0f);
  style.Colors[ImGuiCol_TitleBg] = kDragonOrangeDim;
  style.Colors[ImGuiCol_TitleBgActive] = kDragonOrangeDim;
  style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_ButtonActive] = kDragonOrange;
  style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.19f, 0.22f, 1.0f);
  style.Colors[ImGuiCol_HeaderHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_HeaderActive] = kDragonOrangeDim;
  style.Colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.17f, 1.0f);
  style.Colors[ImGuiCol_TabHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_TabActive] = kDragonOrange;
  style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.14f, 0.17f, 1.0f);
  style.Colors[ImGuiCol_TabUnfocusedActive] = kDragonOrangeDim;
  style.Colors[ImGuiCol_SliderGrab] = kDragonOrange;
  style.Colors[ImGuiCol_SliderGrabActive] = kDragonOrange;
  style.Colors[ImGuiCol_CheckMark] = kDragonOrange;
  style.Colors[ImGuiCol_CheckMark] = kDragonOrange;
}

LauncherDialog::LauncherDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_play)
    : ImGuiDialog(drawer), on_play_(std::move(on_play)) {
  ApplyTheme();
}

void LauncherDialog::OnClose() {
  // Robustness: persist whatever the user selected in the launcher, even if they
  // close the dialog (Play button or window X) without pressing "Save settings".
  // The Play button also saves explicitly; this guarantees nothing is ever lost.
  dbz3::settings::SaveUserSettings();
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  // Fill the entire host window. The host window is always 1280x720 windowed
  // during the launcher (fullscreen/resolution are applied on Play), so the
  // launcher is a single full window with no "window inside window" effect.
  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

  // Header banner.
  ImGui::PushStyleColor(ImGuiCol_Text, kDragonOrange);
  ImGui::SetWindowFontScale(1.5f);
  ImGui::Text("DRAGON BALL Z: BUDOKAI 3");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::TextColored(kTextDim, "Recompiled with ReXGlue  -  HD Collection (PAL)");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Tab bar.
  if (ImGui::BeginTabBar("##launcher_tabs")) {
    if (ImGui::BeginTabItem("Video")) {
      DrawVideoTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Upscaling")) {
      DrawUpscaleTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
      DrawAudioTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Input")) {
      DrawInputTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Mods")) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Model Swap")) {
      DrawModelSwapTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Texturas")) {
      DrawTexturesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Dev")) {
      DrawDevTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Footer buttons.
  ImGui::SetCursorPosX(16);
  if (ImGui::Button("Reset to defaults", ImVec2(190, 0))) {
    rex::cvar::SetFlagByName("dbz3_resolution_scale", "1");
    rex::cvar::SetFlagByName("dbz3_language", "1");
    rex::cvar::SetFlagByName("dbz3_region", "us");
    rex::cvar::SetFlagByName("dbz3_enabled_mods", "*");
    rex::cvar::SetFlagByName("dbz3_fullscreen_mode", "windowed");
    rex::cvar::SetFlagByName("dbz3_vsync", "true");
    rex::cvar::SetFlagByName("dbz3_frame_cap", "60");
    rex::cvar::SetFlagByName("dbz3_gpu_backend", "d3d12");
    rex::cvar::SetFlagByName("dbz3_gamma", "1.0");
    rex::cvar::SetFlagByName("dbz3_native_2x_msaa", "true");
    rex::cvar::SetFlagByName("dbz3_anisotropic", "5");
    rex::cvar::SetFlagByName("dbz3_present_effect", "fsr");
    rex::cvar::SetFlagByName("dbz3_fsr_quality", "quality");
    rex::cvar::SetFlagByName("dbz3_fsr_sharpness", "0.2");
    rex::cvar::SetFlagByName("dbz3_cas_sharpness", "0.0");
    rex::cvar::SetFlagByName("dbz3_master_volume", "1.0");
    rex::cvar::SetFlagByName("dbz3_music_volume", "1.0");
    rex::cvar::SetFlagByName("dbz3_sfx_volume", "1.0");
    rex::cvar::SetFlagByName("dbz3_voice_volume", "1.0");
    rex::cvar::SetFlagByName("dbz3_deadzone", "0.1");
    rex::cvar::SetFlagByName("dbz3_rumble", "true");
  }
  ImGui::SameLine(0, 20);
  if (ImGui::Button("Save settings", ImVec2(190, 0))) {
    dbz3::settings::SaveUserSettings();
  }
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 16.0f - 280.0f);
  if (ImGui::Button("PLAY", ImVec2(280, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    dbz3::settings::SaveUserSettings();
    dbz3::settings::ApplyUserSettingsToSdk();
    dbz3::settings::ApplyRuntimeSettingsToSdk(true);
    dbz3::settings::ApplyWindowSizeToSdk();
    REXLOG_INFO("dbz3: launcher Play pressed, starting game");
    Close();
    if (on_play_) {
      on_play_();
    }
  }

  ImGui::End();
}

void LauncherDialog::DrawVideoTab() {
  ImGui::BeginChild("##video_settings", ImVec2(0, 0), false);

  PushSectionHeader("Image Quality");

  static const char* scale_items[] = {"1x (native 720p)", "2x (1440p internal)", "3x (2160p internal)",
                                      "4x (2880p internal)"};
  int scale = dbz3::settings::ResolutionScale();
  int scale_idx = scale - 1;
  if (scale_idx < 0) scale_idx = 0;
  if (scale_idx > 3) scale_idx = 3;
  if (ImGui::Combo("Internal render scale", &scale_idx, scale_items, 4)) {
    dbz3::settings::SetResolutionScale(scale_idx + 1);
    // Persist immediately: the user often marks the scale and then launches (or
    // closes) without pressing "Save settings". Saving here guarantees the chosen
    // internal resolution is always applied on the next boot.
    dbz3::settings::SaveUserSettings();
  }
  ImGui::TextWrapped("Supersampling of the 720p framebuffer. Reduces aliasing. Restart.");

  bool msaa = dbz3::settings::Native2xMsaa();
  if (ImGui::Checkbox("Native 2x MSAA", &msaa)) {
    dbz3::settings::SetNative2xMsaa(msaa);
  }
  ImGui::TextWrapped("Host 2x MSAA for guest 2x MSAA surfaces.");

  static const char* aniso_items[] = {"Off", "1x", "2x", "4x", "8x", "16x"};
  static const int aniso_values[] = {0, 1, 2, 3, 4, 5};
  int aniso = dbz3::settings::AnisotropicOverride();
  int aniso_idx = 0;
  for (int i = 0; i < 6; i++) {
    if (aniso == aniso_values[i]) aniso_idx = i;
  }
  if (ImGui::Combo("Anisotropic filtering", &aniso_idx, aniso_items, 6)) {
    dbz3::settings::SetAnisotropicOverride(aniso_values[aniso_idx]);
  }

  ImGui::Spacing();
  PushSectionHeader("Language");

  static const char* lang_items[] = {"English", "Japanese", "German", "French", "Spanish", "Italian"};
  static const int lang_ids[] = {1, 2, 3, 4, 5, 6};
  int lang = dbz3::settings::Language();
  int lang_idx = 0;
  for (int i = 0; i < 6; i++) {
    if (lang == lang_ids[i]) lang_idx = i;
  }
  if (ImGui::Combo("Text language", &lang_idx, lang_items, 6)) {
    dbz3::settings::SetLanguage(lang_ids[lang_idx]);
  }
  ImGui::TextWrapped("Game text/UI language. Restart required.");

  ImGui::Spacing();
  PushSectionHeader("Display");

  static const char* modes[] = {"Windowed", "Borderless", "Exclusive Fullscreen"};
  int mode_idx = 0;
  std::string mode = dbz3::settings::FullscreenMode();
  if (mode == "borderless") mode_idx = 1;
  else if (mode == "exclusive") mode_idx = 2;
  if (ImGui::Combo("Fullscreen mode", &mode_idx, modes, 3)) {
    dbz3::settings::SetFullscreenMode(mode_idx == 0 ? "windowed" : (mode_idx == 1 ? "borderless" : "exclusive"));
  }

  bool vsync = dbz3::settings::VsyncEnabled();
  if (ImGui::Checkbox("VSync", &vsync)) {
    dbz3::settings::SetVsyncEnabled(vsync);
  }

  int cap = dbz3::settings::FrameCap();
  if (ImGui::SliderInt("Frame cap (FPS)", &cap, 0, 240, cap == 0 ? "Uncapped" : "%d FPS")) {
    dbz3::settings::SetFrameCap(dbz3::settings::SafeFrameCap(cap));
  }

  bool vrr = dbz3::settings::VrrEnabled();
  if (ImGui::Checkbox("Variable refresh rate (G-Sync/FreeSync)", &vrr)) {
    dbz3::settings::SetVrrEnabled(vrr);
  }
  ImGui::TextColored(kTextDim,
                     "Sincroniza el monitor con cada fotograma para evitar saltos "
                     "en pantallas de alta frecuencia. Si esta desactivado, el "
                     "juego ajusta el ritmo solo para que se vea fluido a 60 Hz.");

  // Detected once (EnumDisplaySettingsW is a system call; no need to repeat it
  // every frame). Just informational in the UI.
  static double cached_hz = 0.0;
  if (cached_hz == 0.0) {
    cached_hz = dbz3::settings::DetectRefreshRate();
  }
  const int hz_int = static_cast<int>(cached_hz + 0.5);
  if (hz_int >= 30) {
    ImGui::TextDisabled("Monitor: %d Hz", hz_int);
    ImGui::TextColored(kTextDim,
                       "El launcher no depende del refresco del monitor. El "
                       "juego se ve fluido a cualquier Hz.");
  } else {
    ImGui::TextDisabled("Monitor: no detectado (se asume 60 Hz).");
  }

  static const char* backends[] = {"Direct3D 12", "Vulkan (experimental)"};
  static const char* backend_vals[] = {"d3d12", "vulkan"};
  int backend_idx = 0;
  std::string backend = dbz3::settings::GpuBackend();
  for (int i = 0; i < 2; i++) {
    if (backend == backend_vals[i]) backend_idx = i;
  }
  if (ImGui::Combo("Graphics backend", &backend_idx, backends, 2)) {
    dbz3::settings::SetGpuBackend(backend_vals[backend_idx]);
  }
  if (backend_idx == 1) {
    ImGui::TextWrapped("Vulkan is experimental: 3D combat runs notably slower "
                       "than D3D12 on NVIDIA hardware. Use D3D12 unless you need "
                       "Vulkan for platform compatibility.");
  }
  ImGui::TextWrapped("Host rendering API. Restart required.");

  double gamma = dbz3::settings::Gamma();
  if (SliderD("Gamma", &gamma, 0.5, 2.0, "%.2f")) {
    dbz3::settings::SetGamma(gamma);
  }

  ImGui::EndChild();
}

void LauncherDialog::DrawUpscaleTab() {
  ImGui::BeginChild("##upscale_settings", ImVec2(0, 0), false);

  PushSectionHeader("Upscaling");

  static const char* effects[] = {"Bilinear", "CAS (sharpen)", "FSR 1 (FidelityFX)"};
  static const char* effect_values[] = {"bilinear", "cas", "fsr"};
  int eff_idx = 0;
  std::string eff = dbz3::settings::PresentEffect();
  for (int i = 0; i < 3; i++) {
    if (eff == effect_values[i]) eff_idx = i;
  }
  if (ImGui::Combo("Effect", &eff_idx, effects, 3)) {
    dbz3::settings::SetPresentEffect(effect_values[eff_idx]);
    // Persist immediately so the chosen upscaling effect survives a launch/close
    // without the user pressing "Save settings".
    dbz3::settings::SaveUserSettings();
  }

  if (dbz3::settings::PresentEffect() == "fsr") {
    ImGui::TextWrapped("FSR upscales the internal render to the display size.\n"
                       "Best paired with a low internal scale (1x) on a 1080p+ display.");
  } else if (dbz3::settings::PresentEffect() == "cas") {
    ImGui::TextWrapped("CAS applies contrast-adaptive sharpening after scaling.\n"
                       "Great with a high internal scale (2x-3x).");
  } else {
    ImGui::TextWrapped("Bilinear upscaling - the simplest path. No sharpening.");
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim, "The chosen effect applies on the next boot (restart required).");

  ImGui::EndChild();
}

void LauncherDialog::DrawAudioTab() {
  ImGui::BeginChild("##audio_settings", ImVec2(0, 0), false);

  PushSectionHeader("Volume");

  double master = dbz3::settings::MasterVolume();
  if (SliderD("Master volume", & master, 0.0, 1.0, "%.2f")) {
    dbz3::settings::SetMasterVolume(master);
  }
  double music = dbz3::settings::MusicVolume();
  if (SliderD("Music volume", & music, 0.0, 1.0, "%.2f")) {
    dbz3::settings::SetMusicVolume(music);
  }
  double sfx = dbz3::settings::SfxVolume();
  if (SliderD("SFX volume", & sfx, 0.0, 1.0, "%.2f")) {
    dbz3::settings::SetSfxVolume(sfx);
  }
  double voice = dbz3::settings::VoiceVolume();
  if (SliderD("Voice volume", & voice, 0.0, 1.0, "%.2f")) {
    dbz3::settings::SetVoiceVolume(voice);
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim, "Language/voice tracks are selected in-game (Japanese/English).");

  ImGui::EndChild();
}

void LauncherDialog::DrawInputTab() {
  ImGui::BeginChild("##input_settings", ImVec2(0, 0), false);

  PushSectionHeader("Controller");

  double deadzone = dbz3::settings::Deadzone();
  if (SliderD("Left stick deadzone", & deadzone, 0.0, 0.9, "%.2f")) {
    dbz3::settings::SetDeadzone(deadzone);
  }

  bool rumble = dbz3::settings::RumbleEnabled();
  if (ImGui::Checkbox("Enable vibration", &rumble)) {
    dbz3::settings::SetRumbleEnabled(rumble);
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim, "Full button remapping is available in the in-game Settings overlay.");

  ImGui::EndChild();
}

void LauncherDialog::DrawModsTab() {
  ImGui::BeginChild("##mods_settings", ImVec2(0, -40), true);

  ImGui::TextWrapped(
      "Mods override entries inside the game's .afs containers (models, move "
      "sets, textures) without repacking. A mod is a folder here:");
  ImGui::TextDisabled("mods/<name>/<region>/<file.afs>  (+ manifest.txt)");
  ImGui::Separator();

  // Asset region (us/eu).
  ImGui::Text("Asset region");
  ImGui::SameLine();
  static const char* region_items[] = {"USA (NTSC)", "Europe (PAL)"};
  static const char* region_vals[] = {"us", "eu"};
  int region_idx = 0;
  std::string region = dbz3::settings::Region();
  for (int i = 0; i < 2; i++) {
    if (region == region_vals[i]) region_idx = i;
  }
  ImGui::SetNextItemWidth(220);
  if (ImGui::Combo("##region", &region_idx, region_items, 2)) {
    dbz3::settings::SetRegion(region_vals[region_idx]);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Text/audio/video pack. Restart required.");
  ImGui::Separator();

  const std::vector<dbz3::ModInfo> mods = dbz3::ListMods();
  if (mods.empty()) {
    ImGui::TextWrapped("No hay mods instalados. Los mods se colocan en la "
                       "carpeta 'mods' junto al ejecutable, cada uno en su "
                       "propia subcarpeta con un manifest.txt.");
    if (ImGui::Button("Abrir carpeta de mods", ImVec2(220, 0))) {
      const std::filesystem::path mods_dir = dbz3::ModsRoot();
      std::error_code ec;
      std::filesystem::create_directories(mods_dir, ec);
      std::string cmd = "explorer \"" + mods_dir.string() + "\"";
      std::system(cmd.c_str());
    }
    ImGui::TextDisabled("Crea 'mods/' si no existe y la abre en el Explorador. "
                        "Copia aqui tu mod descargado y se listara y activara "
                        "automaticamente.");
    ImGui::EndChild();
    return;
  }

  int enabled_count = 0;
  for (const dbz3::ModInfo& mod : mods) {
    if (mod.enabled) ++enabled_count;
  }
  ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.0f), "%d mods (%d enabled)",
                     static_cast<int>(mods.size()), enabled_count);
  ImGui::Separator();

  const float table_w = ImGui::GetContentRegionAvail().x;
  if (ImGui::BeginTable("##mods_table", 4,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_NoHostExtendX)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                            std::min(130.0f, table_w * 0.18f));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableHeadersRow();

    for (const dbz3::ModInfo& mod : mods) {
      ImGui::TableNextRow();
      const float row_h = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y;

      ImGui::TableSetColumnIndex(0);
      bool current = mod.enabled;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           (row_h - ImGui::GetFrameHeight()) * 0.5f);
      if (ImGui::Checkbox(("##mod_" + mod.name).c_str(), &current)) {
        dbz3::SetModEnabled(mod.name, current);
      }

      ImGui::TableSetColumnIndex(1);
      const std::string& title =
          mod.display_name.empty() ? mod.name : mod.display_name;
      ImVec4 title_col = mod.enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                     : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
      ImGui::TextColored(title_col, "%s", title.c_str());
      if (!mod.description.empty()) {
        ImGui::TextDisabled("%s", mod.description.c_str());
      } else if (mod.source.empty() && mod.target.empty()) {
        ImGui::TextDisabled("%s", mod.name.c_str());
      }
      if (!mod.source.empty() || !mod.target.empty()) {
        std::string route = mod.source;
        if (!route.empty()) route += " -> ";
        route += mod.target;
        ImGui::TextDisabled("%s", route.c_str());
      }
      ImGui::TextDisabled("%d file%s", mod.file_count,
                          mod.file_count == 1 ? "" : "s");

      ImGui::TableSetColumnIndex(2);
      const int type_col = dbz3::ModTypeColor(mod.type);
      ImVec4 tc = type_col < 0
                      ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                      : ImVec4(((type_col >> 16) & 0xFF) / 255.0f,
                               ((type_col >> 8) & 0xFF) / 255.0f,
                               (type_col & 0xFF) / 255.0f, 1.0f);
      ImGui::TextColored(tc, "%s", dbz3::ModTypeLabel(mod.type));
      if (mod.enabled) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "ON");
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s\nAuthor: %s\nVersion: %s\nType: %s\nSource: %s\nTarget: %s",
                          title.c_str(),
                          mod.description.empty() ? "(no description)" : mod.description.c_str(),
                          mod.author.empty() ? "-" : mod.author.c_str(),
                          mod.version.empty() ? "-" : mod.version.c_str(),
                          dbz3::ModTypeLabel(mod.type), mod.source.c_str(),
                          mod.target.c_str());
      }

      ImGui::TableSetColumnIndex(3);
      if (ImGui::SmallButton(("##edit_" + mod.name).c_str())) {
        editing_mod_ = true;
        edit_mod_name_ = mod.name;
        std::string n = dbz3::GetModManifestValue(mod.name, "name");
        std::string d = dbz3::GetModManifestValue(mod.name, "description");
        std::string a = dbz3::GetModManifestValue(mod.name, "author");
        std::string v = dbz3::GetModManifestValue(mod.name, "version");
        if (n.empty()) n = mod.name;
        std::memcpy(edit_name_buf_, n.c_str(),
                    std::min(n.size(), sizeof(edit_name_buf_) - 1));
        edit_name_buf_[std::min(n.size(), sizeof(edit_name_buf_) - 1)] = '\0';
        std::memcpy(edit_desc_buf_, d.c_str(),
                    std::min(d.size(), sizeof(edit_desc_buf_) - 1));
        edit_desc_buf_[std::min(d.size(), sizeof(edit_desc_buf_) - 1)] = '\0';
        std::memcpy(edit_author_buf_, a.c_str(),
                    std::min(a.size(), sizeof(edit_author_buf_) - 1));
        edit_author_buf_[std::min(a.size(), sizeof(edit_author_buf_) - 1)] = '\0';
        std::memcpy(edit_version_buf_, v.c_str(),
                    std::min(v.size(), sizeof(edit_version_buf_) - 1));
        edit_version_buf_[std::min(v.size(), sizeof(edit_version_buf_) - 1)] = '\0';
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Editar descripcion / autor / version (manifest.txt)");
      }
    }
    ImGui::EndTable();
  }

  // Inline edit dialog for the selected mod's manifest.
  if (editing_mod_) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.90f, 0.85f, 0.50f, 1.0f), "Editar mod: %s",
                       edit_mod_name_.c_str());
    ImGui::Text("Titulo");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("##edit_name", edit_name_buf_, sizeof(edit_name_buf_));
    ImGui::Text("Descripcion");
    ImGui::InputTextMultiline("##edit_desc", edit_desc_buf_,
                              sizeof(edit_desc_buf_), ImVec2(-1.0f, 64.0f));
    ImGui::Text("Autor");
    ImGui::InputText("##edit_author", edit_author_buf_,
                     sizeof(edit_author_buf_));
    ImGui::Text("Version");
    ImGui::InputText("##edit_version", edit_version_buf_,
                     sizeof(edit_version_buf_));
    if (ImGui::Button("Guardar", ImVec2(120, 0))) {
      if (edit_name_buf_[0] != '\0') {
        dbz3::SetModManifestValue(edit_mod_name_, "name", edit_name_buf_);
      }
      dbz3::SetModManifestValue(edit_mod_name_, "description", edit_desc_buf_);
      dbz3::SetModManifestValue(edit_mod_name_, "author", edit_author_buf_);
      dbz3::SetModManifestValue(edit_mod_name_, "version", edit_version_buf_);
      editing_mod_ = false;
      pending_manifest_reload_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
      editing_mod_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("El texto se guarda en %s/manifest.txt",
                        edit_mod_name_.c_str());
  }

  ImGui::EndChild();
}

void LauncherDialog::DrawModelSwapTab() {
  ImGui::BeginChild("##model_swap", ImVec2(0, -40), true);

  ImGui::SeparatorText("Model swap B3 HD -> B3 HD");
  ImGui::TextDisabled("Intercambia el bin #AMB completo de un personaje HD del "
                      "B3 por el de otro (swap nativo). Genera el mod y lo activa.");

  // Lazy-load the B3 catalog once (first draw).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped("El catalogo de personajes (catalog_b3.cat) no se "
                       "encontro o esta vacio.");
    ImGui::TextWrapped("El model swap necesita la carpeta 'mod center hd' "
                       "junto al ejecutable, con catalog_b3.cat y swap_b3.py. "
                       "No viene incluida en el ZIP de release: descargala del "
                       "repositorio (carpeta 'mod center hd') o desde un "
                       "release completo, y colocala al lado de dbz3.exe.");
    ImGui::TextDisabled("Esperado en: %s",
                        (rex::filesystem::GetExecutableFolder() /
                         "mod center hd" / "catalog_b3.cat")
                            .string()
                            .c_str());
    ImGui::EndChild();
    return;
  }

  // Source AFS: auto-detected by default; the user can point to a custom
  // data_cmn.afs if it lives elsewhere.
  ImGui::SeparatorText("Archivo de modelos (data_cmn.afs)");
  bool auto_afs = afs_path_auto_;
  if (ImGui::Checkbox("Usar la ruta automatica (us/data_cmn.afs)", &auto_afs)) {
    afs_path_auto_ = auto_afs;
    if (auto_afs) {
      mod_pipeline_.SetAfsPath("");
    }
  }
  ImGui::BeginDisabled(afs_path_auto_);
  if (ImGui::InputText("##afs_path", afs_path_buf_, sizeof(afs_path_buf_))) {
    mod_pipeline_.SetAfsPath(afs_path_buf_);
  }
  if (ImGui::Button("Buscar...", ImVec2(120, 0))) {
    // Basic file picker via a native dialog is out of scope here; we let the
    // user paste the full path. Show a hint.
    ImGui::OpenPopup("afs_hint");
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("Ruta completa al data_cmn.afs del juego");
  if (ImGui::BeginPopup("afs_hint")) {
    ImGui::TextWrapped("Pega la ruta completa, por ejemplo:\n"
                       "C:\\...\\us\\data_cmn.afs\n"
                       "o la que corresponda a tu instalacion.");
    ImGui::EndPopup();
  }

  if (pipeline_src_idx_ >= (int)chars.size()) pipeline_src_idx_ = -1;
  if (pipeline_dst_idx_ >= (int)chars.size()) pipeline_dst_idx_ = -1;

  ImGui::Text("Personaje HD (origen)");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  if (ImGui::BeginCombo("##swap_src", pipeline_src_idx_ >= 0
                            ? chars[pipeline_src_idx_].DisplayName().c_str()
                            : "Selecciona...")) {
    for (int i = 0; i < (int)chars.size(); ++i) {
      const bool selected = (pipeline_src_idx_ == i);
      if (ImGui::Selectable(chars[i].DisplayName().c_str(), selected)) {
        pipeline_src_idx_ = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%d personajes)", static_cast<int>(chars.size()));

  ImGui::Text("Slot destino");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  if (ImGui::BeginCombo("##swap_dst", pipeline_dst_idx_ >= 0
                            ? chars[pipeline_dst_idx_].DisplayName().c_str()
                            : "Selecciona...")) {
    for (int i = 0; i < (int)chars.size(); ++i) {
      const bool selected = (pipeline_dst_idx_ == i);
      if (ImGui::Selectable(chars[i].DisplayName().c_str(), selected)) {
        pipeline_dst_idx_ = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%d personajes)", static_cast<int>(chars.size()));

  const bool can_swap = pipeline_src_idx_ >= 0 && pipeline_dst_idx_ >= 0;
  ImGui::BeginDisabled(!can_swap || mod_pipeline_.IsRunning());
  if (ImGui::Button("Swap B3 -> B3", ImVec2(220, 0))) {
    mod_pipeline_.SwapB3ToB3(chars[pipeline_src_idx_],
                             chars[pipeline_dst_idx_]);
  }
  ImGui::EndDisabled();
  if (can_swap) {
    const B3Char& src = chars[pipeline_src_idx_];
    const B3Char& dst = chars[pipeline_dst_idx_];
    ImGui::TextDisabled("%s (bin %d)%s -> %s (slot %d)%s",
                        src.DisplayName().c_str(), src.bin,
                        src.playable ? "" : "  [NO JUGABLE]",
                        dst.DisplayName().c_str(), dst.bin,
                        dst.playable ? "" : "  [NO JUGABLE]");
  }

  ImGui::Separator();
  if (mod_pipeline_.IsRunning()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Working...");
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled("Done.");
  }
  const std::string out = mod_pipeline_.Output();
  if (!out.empty()) {
    std::memcpy(output_buf_, out.c_str(),
                std::min(out.size(), sizeof(output_buf_) - 1));
    output_buf_[std::min(out.size(), sizeof(output_buf_) - 1)] = '\0';
    ImGui::InputTextMultiline("##swap_out", output_buf_,
                              sizeof(output_buf_), ImVec2(-1.0f, 160.0f),
                              ImGuiInputTextFlags_ReadOnly);
  }
  ImGui::TextDisabled("El mod generado se activa solo y se lista en la pestaña Mods.");

  ImGui::EndChild();
}

void LauncherDialog::DrawTexturesTab() {
  ImGui::BeginChild("##textures", ImVec2(0, -40), true);

  ImGui::SeparatorText("Mod de texturas (B3 HD)");
  ImGui::TextDisabled("Extrae las texturas de un personaje como imagenes PNG "
                      "editables, y al reconstruir reinserta tus ediciones.");

  // Lazy-load the catalog (shared with Model Swap).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped("El catalogo de personajes (catalog_b3.cat) no se "
                       "encontro o esta vacio.");
    ImGui::TextWrapped("El mod de texturas necesita la carpeta 'mod center hd' "
                       "junto al ejecutable, con catalog_b3.cat y texture_b3.py. "
                       "No viene incluida en el ZIP de release: descargala del "
                       "repositorio (carpeta 'mod center hd') o desde un "
                       "release completo, y colocala al lado de dbz3.exe.");
    ImGui::TextDisabled("Esperado en: %s",
                        (rex::filesystem::GetExecutableFolder() /
                         "mod center hd" / "catalog_b3.cat")
                            .string()
                            .c_str());
    ImGui::EndChild();
    return;
  }

  if (tex_src_idx_ >= (int)chars.size()) tex_src_idx_ = -1;

  ImGui::Text("Personaje (origen de las texturas)");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  if (ImGui::BeginCombo("##tex_src", tex_src_idx_ >= 0
                          ? chars[tex_src_idx_].DisplayName().c_str()
                          : "Selecciona...")) {
    for (int i = 0; i < (int)chars.size(); ++i) {
      const bool selected = (tex_src_idx_ == i);
      const std::string label =
          chars[i].DisplayName() + "  [bin " + std::to_string(chars[i].bin) + "]";
      if (ImGui::Selectable(label.c_str(), selected)) {
        tex_src_idx_ = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%d personajes)", static_cast<int>(chars.size()));

  // Slot destino: por defecto el mismo bin del origen (solo texturas), o un
  // personaje distinto (compatible con swaps de modelo: el bin del origen con
  // sus texturas editadas se coloca en el slot del destino).
  if (tex_dst_idx_ >= (int)chars.size()) tex_dst_idx_ = -1;
  ImGui::Text("Slot destino (donde se aplican las texturas)");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  std::string dst_display;
  if (tex_dst_idx_ >= 0) {
    dst_display = chars[tex_dst_idx_].DisplayName();
  }
  const char* dst_label =
      tex_dst_idx_ >= 0 ? dst_display.c_str() : "El mismo personaje (sin swap)";
  if (ImGui::BeginCombo("##tex_dst", dst_label)) {
    if (ImGui::Selectable("El mismo personaje (sin swap)", tex_dst_idx_ < 0)) {
      tex_dst_idx_ = -1;
    }
    if (tex_dst_idx_ < 0) ImGui::SetItemDefaultFocus();
    for (int i = 0; i < (int)chars.size(); ++i) {
      const bool selected = (tex_dst_idx_ == i);
      const std::string label =
          chars[i].DisplayName() + "  [bin " + std::to_string(chars[i].bin) + "]";
      if (ImGui::Selectable(label.c_str(), selected)) {
        tex_dst_idx_ = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("Si eliges otro personaje, el bin del origen con sus "
                      "texturas editadas se coloca en el slot de ese personaje "
                      "(para combinar con un swap de modelo).");

  // Nombre del mod de texturas.
  ImGui::Text("Nombre del mod");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(300);
  ImGui::InputText("##tex_mod", tex_mod_buf_, sizeof(tex_mod_buf_));
  std::string mod_name = tex_mod_buf_;
  if (mod_name.empty() && tex_src_idx_ >= 0) {
    mod_name = "tex_" + std::to_string(chars[tex_src_idx_].bin);
  }

  // Carpeta de texturas: editable. Por defecto es la automatica del mod
  // (mods/<mod>/textures), pero el usuario puede elegir cualquier carpeta
  // (p.ej. donde ya esta editando). Se auto-rellena al extraer.
  ImGui::Text("Carpeta de texturas (PNG)");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(360);
  ImGui::InputText("##tex_dir", tex_dir_buf_, sizeof(tex_dir_buf_));
  ImGui::SameLine();
  if (ImGui::Button("Examinar...")) {
    std::string picked;
    std::string start = tex_dir_buf_[0] ? tex_dir_buf_ : "";
    if (PickFolder(picked, start)) {
      std::snprintf(tex_dir_buf_, sizeof(tex_dir_buf_), "%s", picked.c_str());
    }
  }
  if (tex_dir_buf_[0] == '\0' && !mod_name.empty()) {
    // Muestra la ruta por defecto aunque el buffer este vacio.
    const std::string def_dir =
        (dbz3::ModsRoot() / mod_name / "textures").string();
    ImGui::SameLine();
    ImGui::TextDisabled("(default: %s)", def_dir.c_str());
  }

  const bool can_extract = tex_src_idx_ >= 0;
  ImGui::BeginDisabled(!can_extract || mod_pipeline_.IsRunning());
  if (ImGui::Button("Extraer texturas a PNG", ImVec2(220, 0))) {
    // Solo usar la carpeta configurada si no contiene caracteres invalidos
    // de Windows; si no, usar la automatica (una ruta invalida persistente
    // en el campo romperia la extraccion con cualquier personaje).
    const std::string cfg = tex_dir_buf_[0] ? tex_dir_buf_ : "";
    bool cfg_valid = !cfg.empty();
    if (cfg_valid) {
      for (char c : cfg) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' ||
            c == '?' || c == '*') {
          cfg_valid = false;
          break;
        }
      }
    }
    const std::string dir = cfg_valid ? cfg : "";
    mod_pipeline_.ExtractTextures(chars[tex_src_idx_], mod_name, dir);
    // Al extraer a la ruta por defecto, dejar la carpeta configurada.
    if (tex_dir_buf_[0] == '\0') {
      const std::string def_dir =
          (dbz3::ModsRoot() / mod_name / "textures").string();
      std::snprintf(tex_dir_buf_, sizeof(tex_dir_buf_), "%s", def_dir.c_str());
    }
  }
  ImGui::EndDisabled();

  // Determinar la carpeta activa de texturas (la configurada, o el default).
  std::string active_tex_dir;
  if (tex_dir_buf_[0] != '\0') {
    active_tex_dir = tex_dir_buf_;
  } else if (!mod_name.empty()) {
    active_tex_dir = (dbz3::ModsRoot() / mod_name / "textures").string();
  }
  const bool tex_dir_exists =
      !active_tex_dir.empty() &&
      std::filesystem::is_directory(active_tex_dir) &&
      std::filesystem::is_regular_file(
          std::filesystem::path(active_tex_dir) / "textures_meta.json");
  if (tex_dir_exists) {
    ImGui::SameLine();
    if (ImGui::Button("Abrir carpeta de texturas", ImVec2(220, 0))) {
      std::string cmd = "explorer \"" + active_tex_dir + "\"";
      std::system(cmd.c_str());
    }
    ImGui::TextDisabled("Edita los PNG en: %s", active_tex_dir.c_str());
  } else {
    ImGui::TextDisabled("Extrae primero las texturas para editar los PNG.");
  }

  ImGui::Separator();
  ImGui::BeginDisabled(!tex_dir_exists || mod_pipeline_.IsRunning());
  if (ImGui::Button("Reconstruir mod con texturas editadas", ImVec2(280, 0))) {
    const int dst_slot = tex_dst_idx_ >= 0 ? chars[tex_dst_idx_].bin : -1;
    mod_pipeline_.BuildTextures(mod_name, dst_slot, active_tex_dir);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("Reinsere los PNG editados de la carpeta, recompila el "
                      "bin y genera el mod activo (combinable con un swap de "
                      "modelo).");

  // Listar los PNG extraidos de la carpeta activa (para identificar las
  // texturas sin abrir el explorador).
  if (tex_dir_exists) {
    std::vector<std::pair<std::string, std::uintmax_t>> pngs;
    for (const auto& e : std::filesystem::directory_iterator(active_tex_dir)) {
      if (e.is_regular_file() && e.path().extension() == ".png") {
        pngs.emplace_back(e.path().filename().string(), e.file_size());
      }
    }
    if (!pngs.empty()) {
      ImGui::Text("%zu texturas (PNG):", pngs.size());
      ImGui::SameLine();
      ImGui::TextDisabled("haz clic en 'Abrir carpeta' para verlas");
      int per_row = 4;
      for (size_t i = 0; i < pngs.size(); ++i) {
        if (i % per_row != 0) ImGui::SameLine();
        ImGui::Selectable(pngs[i].first.c_str(), false);
        if ((i % per_row) == per_row - 1) ImGui::NewLine();
      }
      if (pngs.size() % per_row != 0) ImGui::NewLine();
    }
    ImGui::Separator();
  }

  if (mod_pipeline_.IsRunning()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Working...");
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled("Done.");
  }
  const std::string out = mod_pipeline_.Output();
  if (!out.empty()) {
    std::memcpy(output_buf_, out.c_str(),
                std::min(out.size(), sizeof(output_buf_) - 1));
    output_buf_[std::min(out.size(), sizeof(output_buf_) - 1)] = '\0';
    ImGui::InputTextMultiline("##tex_out", output_buf_,
                              sizeof(output_buf_), ImVec2(-1.0f, 160.0f),
                              ImGuiInputTextFlags_ReadOnly);
  }

  ImGui::EndChild();
}

void LauncherDialog::DrawDevTab() {
  ImGui::BeginChild("##dev_settings", ImVec2(0, 0), false);

  PushSectionHeader("Diagnostics");

  bool dev_mode = dbz3::settings::DevMode();
  if (ImGui::Checkbox("Enable Dev mode (F10 overlay)", &dev_mode)) {
    dbz3::settings::SetDevMode(dev_mode);
  }
  ImGui::TextColored(kTextDim, "Adds an in-game overlay (F10) with diagnostics and test switches.");

  bool show_fps = dbz3::settings::ShowFps();
  if (ImGui::Checkbox("Show FPS counter in-game (60fps debug)", &show_fps)) {
    dbz3::settings::SetShowFps(show_fps);
  }
  ImGui::TextColored(kTextDim, "Displays a small corner window with the current FPS while playing. "
                               "Useful to verify the frame cap / debug the 60fps mode.");

  bool diag = dbz3::settings::DiagLogging();
  if (ImGui::Checkbox("GPU diagnostic logging (logs + .bmp dumps)", &diag)) {
    dbz3::settings::SetDiagLogging(diag);
  }
  ImGui::TextColored(kTextDim, "Writes per-frame GPU diagnostics (logs, readbacks and .bmp dumps). "
                               "Generates large files. Keep off normally.");

  bool crashdump = dbz3::settings::CrashDumpEnabled();
  if (ImGui::Checkbox("Write crash minidump (crash_*.dmp)", &crashdump)) {
    dbz3::settings::SetCrashDumpEnabled(crashdump);
  }
  ImGui::TextColored(kTextDim, "Writes a minidump file when the game crashes. Keep off normally.");

  ImGui::EndChild();
}

}  // namespace dbz3::launcher
