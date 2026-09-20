// dbz3 - Pre-game launcher screen implementation.
// Dark modern style with Dragon Ball accent colors (orange/blue).

#include "launcher_state.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "settings.h"
#include "i18n.h"
#include "update_check.h"
#include "../region.h"

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

#include <algorithm>
#include <array>
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
constexpr ImVec4 kPanelBg(0.10f, 0.11f, 0.13f, 1.0f);
constexpr ImVec4 kPanelBgAlt(0.14f, 0.15f, 0.18f, 1.0f);
constexpr ImVec4 kTextMain(0.92f, 0.92f, 0.94f, 1.0f);
constexpr ImVec4 kTextDim(0.55f, 0.57f, 0.62f, 1.0f);

// Double slider with named min/max (avoids rvalue-address issues with clang).
bool SliderD(const char* label, double* value, double min, double max, const char* fmt) {
  return ImGui::SliderScalar(label, ImGuiDataType_Double, value, &min, &max, fmt);
}

// dbz3::ModTypeLabel returns the raw English id used in the manifest; the UI
// shows a translated label instead (the search matcher keeps the raw id).
const char* ModTypeLabelText(const std::string& type) {
  if (type == "swap_b3") return i18n::T("cambio B3", "swap B3");
  if (type == "port_b3") return i18n::T("port B3", "port B3");
  if (type == "audio") return i18n::T("audio", "audio");
  if (type == "moveset") return i18n::T("moveset", "moveset");
  if (type == "data") return i18n::T("datos", "data");
  return i18n::T("otro", "other");
}

void PushSectionHeader(const char* title) {
  ImGui::TextColored(kDragonOrange, "%s", title);
  ImGui::Separator();
  ImGui::Spacing();
}

// Case-insensitive ASCII substring test, used by the search boxes in the Mods
// and Model Swap tabs. Empty needle matches everything.
bool IContains(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  auto lower = [](char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
  };
  const size_t n = needle.size();
  if (hay.size() < n) return false;
  for (size_t i = 0; i + n <= hay.size(); ++i) {
    size_t j = 0;
    while (j < n && lower(hay[i + j]) == lower(needle[j])) ++j;
    if (j == n) return true;
  }
  return false;
}

// Searchable character combo shared by the Model Swap and Textures tabs. The
// popup holds a filter box so the 180+ entry catalog is usable; each row shows
// the bin and a [NO JUGABLE] tag when the slot is not playable.
bool CharacterCombo(const char* id, const char* filter_id,
                    const std::vector<B3Char>& chars, int& index,
                    char* search_buf, size_t search_size) {
  // Keep the preview string alive for the whole combo draw (DisplayName()
  // returns a temporary; its c_str() must not outlive this statement).
  std::string preview = (index >= 0 && index < static_cast<int>(chars.size()))
                            ? chars[index].DisplayName()
                            : std::string(i18n::T("Selecciona...", "Select..."));
  if (!ImGui::BeginCombo(id, preview.c_str())) {
    return false;
  }
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint(filter_id, i18n::T("Filtrar...", "Filter..."),
                           search_buf, search_size);
  const std::string q = search_buf;
  int shown = 0;
  bool changed = false;
  for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
    const std::string dn = chars[i].DisplayName();
    if (!IContains(dn, q)) continue;
    ++shown;
    std::string label = dn + "  [bin " + std::to_string(chars[i].bin) + "]";
    if (!chars[i].playable) {
      label += i18n::T("  [NO JUGABLE]", "  [NOT PLAYABLE]");
    }
    const bool selected = (index == i);
    if (ImGui::Selectable(label.c_str(), selected)) {
      index = i;
      changed = true;
    }
    if (selected) ImGui::SetItemDefaultFocus();
  }
  if (shown == 0) {
    ImGui::TextDisabled("%s", i18n::T("Sin resultados.", "No results."));
  }
  ImGui::EndCombo();
  return changed;
}

// Small colored "pill" badge (type / ON-OFF). Drawn as a filled rounded label.
void DrawBadge(const char* text, ImVec4 col) {
  const ImVec2 pad(8.0f, 2.0f);
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p1(p.x + ts.x + pad.x * 2.0f, p.y + ts.y + pad.y * 2.0f);
  dl->AddRectFilled(p, p1, ImGui::GetColorU32(col), 4.0f);
  dl->AddRect(p, p1, ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.55f)), 4.0f);
  dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f)), text);
  ImGui::Dummy(ImVec2(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f));
}

// Edits a MnK keybind cvar with an ImGui text input. The cvar holds a
// comma-separated list of VirtualKey names (e.g. "Space,W" or "Shift+Up");
// empty means unbound.
void DrawKeybind(const char* label, std::string& cvar_value) {
  char buf[64] = {};
  std::memcpy(buf, cvar_value.c_str(),
              std::min(cvar_value.size(), sizeof(buf) - 1));
  if (ImGui::InputText(label, buf, sizeof(buf))) {
    cvar_value = buf;
  }
}

// Height reserved at the bottom of every settings tab for the always-visible
// footer (config summary + Reset/Save + PLAY). Keeps the primary action on
// screen on every tab and removes the window-level scrollbar that pushed PLAY
// below the fold.
constexpr float kFooterHeight = 76.0f;

// Display name for an Xbox language id (1=EN, 3=DE, 4=FR, 5=ES, 6=IT), used in
// the footer summary. ASCII-safe so it renders with the base font.
const char* LanguageDisplayName(int xbox_language_id) {
  switch (xbox_language_id) {
    case 2:
      return "Japanese";
    case 3:
      return "Deutsch";
    case 4:
      return "Francais";
    case 5:
      return "Espanol";
    case 6:
      return "Italiano";
    default:
      return "English";
  }
}

// Native folder/file pickers. Windows uses the IFileOpenDialog COM dialog; on
// other platforms there is no native shell dialog wired up yet, so the pickers
// report "cancelled" (callers keep the default paths). The plan for Linux is a
// portable dialog (SDL file dialog or zenity/kdialog).
#if REX_PLATFORM_WIN32
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

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring out(len > 1 ? len - 1 : 0, L'\0');
  if (len > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
  return out;
}

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
  std::string out(len > 1 ? len - 1 : 0, '\0');
  if (len > 1) {
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr,
                        nullptr);
  }
  return out;
}

// Abre el dialogo nativo de Windows para elegir un archivo (filtro
// `filter_ext`, p.ej. "*.zip"). Rellena `out` (UTF-8) si el usuario eligio.
bool PickFile(std::string& out, const char* filter_desc, const char* filter_ext,
              const std::string& initial) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOpenDialog* dlg = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
    const std::wstring wdesc = Utf8ToWide(filter_desc);
    const std::wstring wext = Utf8ToWide(filter_ext);
    COMDLG_FILTERSPEC spec[2] = {
        {wdesc.c_str(), wext.c_str()},
        {L"Todos los archivos", L"*.*"},
    };
    dlg->SetFileTypes(2, spec);
    if (!initial.empty()) {
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
          out = WideToUtf8(std::wstring(path));
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
#else  // !REX_PLATFORM_WIN32
namespace {

std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'') quoted += "'\\''";
    else quoted += c;
  }
  quoted += "'";
  return quoted;
}

bool RunPicker(const std::string& command, std::string& out) {
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return false;
  std::array<char, 512> buffer{};
  std::string result;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    result += buffer.data();
  }
  const int status = pclose(pipe);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  if (status != 0 || result.empty()) return false;
  out = result;
  return true;
}

bool PickFolder(std::string& out, const std::string& initial) {
  const std::string start = initial.empty() ? "." : initial;
  return RunPicker("zenity --file-selection --directory --filename=" +
                      ShellQuote(start + "/") + " 2>/dev/null",
                  out) ||
         RunPicker("kdialog --getexistingdirectory " + ShellQuote(start) +
                       " 2>/dev/null",
                   out);
}

bool PickFile(std::string& out, const char* filter_desc, const char* filter_ext,
              const std::string& initial) {
  (void)filter_desc;
  const std::string start = initial.empty() ? "." : initial;
  const std::string pattern = filter_ext && *filter_ext ? filter_ext : "*";
  return RunPicker("zenity --file-selection --filename=" +
                      ShellQuote(start + "/" + pattern) + " 2>/dev/null",
                  out) ||
         RunPicker("kdialog --getopenfilename " + ShellQuote(start) + " " +
                       ShellQuote(pattern) + " 2>/dev/null",
                   out);
}

}  // namespace
#endif  // REX_PLATFORM_WIN32

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
  style.WindowPadding = ImVec2(12, 8);
  style.FramePadding = ImVec2(7, 4);
  style.ItemSpacing = ImVec2(7, 4);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.CellPadding = ImVec2(6, 2);
  style.ScrollbarSize = 10.0f;
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
  // The launcher UI language follows the game's selected text language. Keep it
  // updated every frame so changing the "Language" combo re-translates the whole
  // launcher immediately.
  i18n::SetLanguage(dbz3::settings::Language());

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
  ImGui::SetWindowFontScale(1.3f);
  ImGui::Text("DRAGON BALL Z: BUDOKAI 3");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::TextColored(kTextDim, "Recompiled with ReXGlue  -  HD Collection (PAL)");

  // --- Update check (GitHub releases, Dusk-style) -----------------------------
  // Started once; the HTTPS request runs on a background thread so the UI never
  // blocks, and a failure is only a dim note (it must never gate Play). When a
  // newer release exists the user gets a one-click download instead of running
  // an outdated build.
  if (dbz3::settings::UpdateCheckEnabled()) {
    dbz3::launcher::StartUpdateCheck();
    // The installed version is always on screen (so the user never has to guess
    // what build they are running), next to the state of the check and a manual
    // re-check button.
    const std::string cur_label = dbz3::launcher::CurrentVersionLabel();
    if (!cur_label.empty()) {
      ImGui::TextDisabled(i18n::T("Version instalada: v%s", "Installed version: v%s"),
                          cur_label.c_str());
      ImGui::SameLine();
    }
    switch (dbz3::launcher::GetUpdateState()) {
    case dbz3::launcher::UpdateState::kChecking:
      ImGui::TextDisabled("%s", i18n::T("Buscando actualizaciones...",
                                        "Checking for updates..."));
      break;
    case dbz3::launcher::UpdateState::kAvailable: {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
      ImGui::Text(i18n::T("Nueva version disponible: v%s", "New version available: v%s"),
                  dbz3::launcher::LatestVersion().c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Descargar", "Download"))) {
        dbz3::launcher::OpenUrl(dbz3::launcher::LatestReleaseUrl());
      }
      break;
    }
    case dbz3::launcher::UpdateState::kFailed:
      ImGui::TextDisabled("%s", i18n::T("No se pudo comprobar la actualizacion.",
                                        "Could not check for updates."));
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Reintentar", "Retry"))) {
        dbz3::launcher::RequestUpdateCheck();
      }
      break;
    case dbz3::launcher::UpdateState::kUpToDate:
      ImGui::TextDisabled("%s", i18n::T("Version actualizada.", "Up to date."));
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Buscar actualizaciones", "Check for updates"))) {
        dbz3::launcher::RequestUpdateCheck();
      }
      break;
    }
  }

  ImGui::Separator();

  // --- Game data validation banner (P1) -------------------------------------
  // Detects a missing/misplaced asset source BEFORE the user hits Play (which
  // would otherwise end in an "Entrypoint XEX not found" crash) and offers a
  // folder/ISO picker that relocates the game data in-place (no restart needed).
  // Two sources are supported: an extracted folder (us//eu/ + default.xex) and
  // a disc image (.iso) whose game drive is mounted at Play time.
  const auto game_root = dbz3::EffectiveGameRoot();
  const bool iso_mode = dbz3::settings::IsIsoMode();
  const std::string sel_region = dbz3::settings::ResolveRegion(game_root);
  const bool root_ok = !game_root.empty() && std::filesystem::is_directory(game_root);
  // Auto-correct the region selection when the chosen source is missing but the
  // other one exists (e.g. EU-only data with the default "us"): keeps the
  // banner, the footer summary and the region mount at Play time in sync with
  // what is actually present. In ISO mode the region comes from the disc's own
  // default.xex (extracted to the ISO cache folder at startup).
  // The boot source resolved at startup (or on the last source switch) is
  // authoritative: it reports the executable the runtime will really run, which
  // is the staged copy when Budokai 3's file has another name or lives in a
  // subfolder (retail discs keep it as DBZ3/yae3_xenon.xex).
  const auto& boot = dbz3::settings::CurrentBootSource();
  const auto xex_status = boot.status;
  if (iso_mode && (xex_status == dbz3::settings::XexStatus::kUs ||
                   xex_status == dbz3::settings::XexStatus::kEu)) {
    const std::string iso_region = xex_status == dbz3::settings::XexStatus::kEu ? "eu" : "us";
    if (sel_region != iso_region) {
      dbz3::settings::SetRegion(iso_region);
    }
  } else if (sel_region != dbz3::settings::Region()) {
    dbz3::settings::SetRegion(sel_region);
  }
  const bool region_ok = root_ok && std::filesystem::is_directory(game_root / sel_region);
  const bool us_ok = root_ok && std::filesystem::is_directory(game_root / "us");
  const bool xex_present =
      !boot.xex.empty() && std::filesystem::is_regular_file(boot.xex);
  const auto xex_status_final = boot.status;
  // Each core is recompiled from one executable: the US/NA core boots only the
  // US xex and the EU/PAL core only the EU xex. A known xex of the OTHER
  // variant blocks Play (the guest would exit with "No function registered").
  const bool xex_expected = dbz3::settings::XexIsExpected(xex_status_final);
  // Block only on a KNOWN wrong-variant xex (EU xex on the US core or vice
  // versa). An unknown xex (modified/patched dump) is NOT blocked: it shows
  // the amber note below and the user can still try to launch, since a
  // compatible-but-modified copy of the right variant works fine. Blocking it
  // silently disabled Play for those users while the Enter shortcut (not gated
  // by BeginDisabled) still launched the game.
  const bool xex_blocked =
      xex_present && (xex_status_final == dbz3::settings::XexStatus::kDbz1 ||
                      xex_status_final == dbz3::settings::XexStatus::kHdMenu);
  const bool xex_ok = xex_present && !xex_blocked;
  const bool assets_ready =
      (iso_mode ? xex_ok : (region_ok || us_ok) && xex_ok) && !xex_blocked;

  if (assets_ready) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
    if (iso_mode) {
      const std::filesystem::path iso = dbz3::settings::IsoPath();
      ImGui::Text(i18n::T("[OK] Disco: %s", "[OK] Disc: %s"), iso.filename().string().c_str());
    } else {
      ImGui::Text(i18n::T("[OK] Datos del juego en: %s", "[OK] Game data at: %s"),
                  game_root.string().c_str());
    }
    if (sel_region != "us") {
      ImGui::SameLine();
      ImGui::TextDisabled("(%s %s)", i18n::T("region", "region"), sel_region.c_str());
    }
    ImGui::PopStyleColor();
    if (iso_mode) {
      // ISO mode reads the game data straight off the disc; the per-entry AFS
      // override hooks are wired to host files, so mods need the extracted
      // folder. Inform the user instead of silently running without them.
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
      ImGui::TextWrapped(
          i18n::T("Modo disco: se juega tal cual del ISO. Los mods requieren la "
                  "carpeta extraida (elige 'Carpeta extraida' como origen).",
                  "Disc mode: plays straight from the ISO. Mods need the "
                  "extracted folder (choose 'Extracted folder' as the source)."));
      ImGui::PopStyleColor();
    }
    if (boot.redirect_default_xex && !boot.note.empty()) {
      // The executable was found somewhere other than `<data>/default.xex` (e.g.
      // DBZ3/yae3_xenon.xex on a retail disc dump) and staged automatically.
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 0.98f, 1.0f));
      ImGui::TextWrapped(
          i18n::T("Ejecutable detectado: %s (no hay que renombrar nada)",
                  "Executable detected: %s (nothing to rename)"),
          boot.note.c_str());
      ImGui::PopStyleColor();
    }
    if (xex_status == dbz3::settings::XexStatus::kUnknown) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
      ImGui::TextWrapped(
          i18n::T("Nota: default.xex no es un ejecutable estandar de Budokai 3 "
                  "(version modificada o de otra region). Si el juego se cierra "
                  "al inicio, sustituyelo por el default.xex de tu copia.",
                  "Note: default.xex is not a standard Budokai 3 executable "
                  "(modified build or another region). If the game closes at "
                  "startup, replace it with the default.xex from your copy."));
      ImGui::PopStyleColor();
    }
  } else if (xex_blocked) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    if (xex_status_final == dbz3::settings::XexStatus::kDbz1) {
      ImGui::TextWrapped(
          i18n::T("Este es el ejecutable de DBZ Budokai HD Collection (DBZ1), no "
                  "de Budokai 3. Este launcher solo arranca Budokai 3 (dbz3.exe). "
                  "Usa el launcher de DBZ1 (dbz1.exe) con este ejecutable, o pon "
                  "el default.xex de tu copia de Budokai 3.",
                  "This is the DBZ Budokai HD Collection (DBZ1) executable, not "
                  "Budokai 3. This launcher only boots Budokai 3 (dbz3.exe). Use "
                  "the DBZ1 launcher (dbz1.exe) with this executable, or place "
                  "the default.xex from your Budokai 3 copy."));
    } else if (xex_status_final == dbz3::settings::XexStatus::kHdMenu) {
      ImGui::TextWrapped(
          i18n::T("Ese es el MENU de la HD Collection (el default.xex de la raiz "
                  "del disco), no Budokai 3. Copia el ejecutable de Budokai 3 "
                  "(esta dentro de la carpeta DBZ3/, llamado yae3_xenon.xex) junto "
                  "a dbz3.exe, o elige el .iso del disco abajo: el launcher saca el "
                  "ejecutable del disco por su cuenta.",
                  "That is the HD Collection MENU (the disc's root default.xex), "
                  "not Budokai 3. Copy Budokai 3's executable (inside the DBZ3/ "
                  "folder, named yae3_xenon.xex) next to dbz3.exe, or pick the "
                  "disc's .iso below: the launcher extracts the executable from "
                  "the disc by itself."));
    } else {
#if defined(DBZ3_EU_VARIANT)
    ImGui::TextWrapped(
        i18n::T("Este es el nucleo EU/PAL y default.xex es el ejecutable US/NA. "
                "Cada nucleo es una recompilacion de UN ejecutable: este solo "
                "arranca el EU/PAL (yae3_xenon_eu.xex). Sustituye default.xex por "
                "el EU/PAL, o usa el launcher principal (que elige el nucleo "
                "correcto por si solo).",
                "This is the EU/PAL core and default.xex is the US/NA executable. "
                "Each core is a recompilation of ONE executable: this one only "
                "boots the EU/PAL one (yae3_xenon_eu.xex). Replace default.xex "
                "with the EU/PAL one, or use the main launcher (which picks the "
                "correct core automatically)."));
#else
    ImGui::TextWrapped(
        i18n::T("default.xex es el ejecutable EU/PAL. Este nucleo esta recompilado "
                "SOLO desde el ejecutable US/NA (yae3_xenon.xex): el EU no puede "
                "arrancar aqui (el juego se cierra al inicio). Sustituye default.xex "
                "por el US/NA, o usa el launcher principal (que elige el nucleo "
                "EU/PAL por si solo). La region EU/PAL y el idioma se eligen aqui.",
                "default.xex is the EU/PAL executable. This core is recompiled ONLY "
                "from the US/NA executable (yae3_xenon.xex): the EU one cannot boot "
                "here (the game closes at startup). Replace default.xex with the "
"US/NA one, or use the main launcher (which picks the EU/PAL core "
                 "automatically). The EU/PAL region and language are chosen here."));
#endif
    }
    ImGui::PopStyleColor();
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    ImGui::Text(i18n::T("No se encontraron los datos del juego.", "Game data not found."));
    ImGui::PopStyleColor();
    std::string missing;
    if (!root_ok) {
      missing = i18n::T(
          "Pon tu ISO de Budokai 3 (o la carpeta con los datos) junto a dbz3.exe "
          "y pulsa PLAY. Tambien puedes elegir el origen abajo.",
          "Place your Budokai 3 ISO (or the game data folder) next to dbz3.exe "
          "and press PLAY. You can also pick the source below.");
    } else {
      if (!xex_ok) {
        missing += i18n::T(
            "Falta el archivo del juego (default.xex). Copialo junto a dbz3.exe. ",
            "The game executable (default.xex) is missing. Copy it next to "
            "dbz3.exe. ");
      }
      if (!region_ok && !us_ok) {
        missing += i18n::T(
            "Faltan las carpetas us/ o eu/ (texto, audio y video del juego). ",
            "The us/ or eu/ folders (game text, audio and video) are missing. ");
      }
    }
    if (!banner_error_.empty()) {
      missing = banner_error_;
    }
    ImGui::TextWrapped("%s", missing.c_str());
  }

  // --- Data source selector (always visible) ----------------------------------
  // Lets the user choose between an extracted folder (us//eu/ + default.xex) and
  // a disc image (.iso) at ANY time -- not only when assets are missing. The
  // active source is highlighted; clicking either one opens its picker, and
  // picking a source switches the game drive over in-place (no restart needed).
  const bool src_folder = !iso_mode;
  const ImVec4 kSrcActive(0.42f, 0.31f, 0.15f, 1.0f);
  if (src_folder) {
    ImGui::PushStyleColor(ImGuiCol_Button, kSrcActive);
  }
  if (ImGui::Button(i18n::T("Carpeta extraida", "Extracted folder"), ImVec2(170, 0))) {
    std::string picked;
    if (PickFolder(picked, game_root.string())) {
      if (dbz3::settings::IsValidGameDataDir(picked)) {
        dbz3::settings::SetGameDirOverride(picked);
        // Picking an extracted folder leaves ISO mode: the folder wins.
        dbz3::settings::SetIsoPath("");
        dbz3::settings::SaveUserSettings();
        if (dbz3::RelocateGameData(picked)) {
          banner_error_.clear();
          REXLOG_INFO("dbz3: game data relocated to {}", picked);
        } else {
          banner_error_ = i18n::T("No se pudo montar la carpeta elegida.",
                                  "Could not mount the chosen folder.");
          REXLOG_ERROR("dbz3: failed to relocate game data to {}", picked);
        }
      } else {
        // Common beginner mistakes: picked us/ or eu/ directly (instead of the
        // folder CONTAINING them), or an assets/ subfolder. Give a targeted hint.
        const std::string leaf = std::filesystem::path(picked).filename().string();
        if (leaf == "us" || leaf == "eu") {
          banner_error_ =
              i18n::T("Has elegido la carpeta us/ (o eu/) directamente. Elige la "
                      "carpeta que las CONTIENE (la que tiene us/ y eu/ dentro).",
                      "You picked the us/ (or eu/) folder directly. Choose the "
                      "folder that CONTAINS it (the one with us/ and eu/ inside).");
        } else {
          banner_error_ =
              i18n::T("La carpeta elegida no tiene los datos del juego (le falta "
                      "us/, eu/ o default.xex). Elige la carpeta que los contiene.",
                      "The chosen folder has no game data (missing us/, eu/ or "
                      "default.xex). Choose the folder that contains them.");
        }
      }
    }
  }
  if (src_folder) {
    ImGui::PopStyleColor();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "%s",
        i18n::T("Usa la carpeta con los datos ya extraidos (default.xex + us/ + eu/). "
                "Es la unica opcion que permite mods.",
                "Use the folder with the already-extracted data (default.xex + us/ + eu/). "
                "This is the only option that supports mods."));
  }
  ImGui::SameLine();
  if (!src_folder) {
    ImGui::PushStyleColor(ImGuiCol_Button, kSrcActive);
  }
  if (ImGui::Button(i18n::T("ISO (.iso)", "ISO (.iso)"), ImVec2(120, 0))) {
    std::string picked;
    if (PickFile(picked, i18n::T("Imagen de disco Xbox 360 (.iso)", "Xbox 360 disc image (.iso)"),
                 "*.iso", game_root.string())) {
      if (dbz3::settings::IsValidIso(picked)) {
        dbz3::settings::SetIsoPath(picked);
        // Picking an ISO leaves folder mode: the disc wins.
        dbz3::settings::SetGameDirOverride("");
        dbz3::settings::SaveUserSettings();
        // Re-point the game drive at the new ISO right away (no restart).
        dbz3::RelocateGameData(dbz3::EffectiveGameRoot());
        banner_error_.clear();
        REXLOG_INFO("dbz3: game ISO set to {}", picked);
      } else {
        banner_error_ = i18n::T("El archivo no parece una imagen de disco Xbox 360.",
                                "The file does not look like an Xbox 360 disc image.");
        REXLOG_ERROR("dbz3: invalid ISO selected: {}", picked);
      }
    }
  }
  if (!src_folder) {
    ImGui::PopStyleColor();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "%s",
        i18n::T("Juega directamente desde la imagen del disco, sin extraer nada. "
                "Requiere el .iso de Budokai 3 HD.",
                "Play directly from the disc image without extracting anything. "
                "Requires the Budokai 3 HD .iso."));
  }
  ImGui::SameLine();
  ImGui::TextDisabled(
      i18n::T("Elige de donde leer los datos del juego: una carpeta ya extraida o "
              "el ISO del disco.",
              "Choose where the game data comes from: an already-extracted folder "
              "or the disc ISO."));
  ImGui::Separator();

  // Settings-file health notice. An older build could leave dbz3_user.toml with
  // an unescaped Windows path, which made the WHOLE file fail to parse and
  // silently threw away every option. Tell the user when that happened (and when
  // the file was repaired) instead of letting their settings vanish in silence.
  const auto config_state = dbz3::settings::LastConfigLoadState();
  if (config_state == dbz3::settings::ConfigLoadState::kRepaired) {
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%s",
                       i18n::T("Tus ajustes se han recuperado (el archivo de "
                               "configuracion estaba danado y se ha reparado).",
                               "Your settings were recovered (the config file was "
                               "damaged and has been repaired)."));
  } else if (config_state == dbz3::settings::ConfigLoadState::kInvalid) {
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                       i18n::T("No se pudo leer tu archivo de ajustes (dbz3_user.toml): "
                               "tiene un error de formato. Se ha guardado una copia en "
                               "dbz3_user.toml.bak y se usan los valores por defecto. "
                               "Corrige o borra el archivo para empezar limpio.",
                               "Your settings file (dbz3_user.toml) could not be read: it "
                               "has a format error. A copy was saved as dbz3_user.toml.bak "
                               "and the default values are in use. Fix or delete the file "
                               "to start clean."));
  }

  // Tab bar.
  if (ImGui::BeginTabBar("##launcher_tabs")) {
    if (ImGui::BeginTabItem(i18n::T("Video", "Video"))) {
      DrawVideoTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Escalado", "Upscaling"))) {
      DrawUpscaleTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Audio", "Audio"))) {
      DrawAudioTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Controles", "Input"))) {
      DrawInputTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Mods", "Mods"))) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Mods nativos", "Native mods"))) {
      DrawNativeModsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Cambio de modelo", "Model Swap"))) {
      DrawModelSwapTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem(i18n::T("Texturas", "Textures"))) {
      DrawTexturesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(i18n::T("Desarrollo", "Dev"))) {
      DrawDevTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::Separator();

  // --- Footer: always visible (every tab reserves kFooterHeight for it). -----
  // Primary action zone (big green PLAY, high contrast, never below the fold),
  // a one-line summary of what will launch, and the asset region picker (a
  // game-data choice, moved here from the Mods tab so it is always on screen).
  ImGui::TextColored(kTextDim, i18n::T("Inicio: %s - %s - %dx - %s - %s",
                             "Launch: %s - %s - %dx - %s - %s"),
                     iso_mode ? std::filesystem::path(dbz3::settings::IsoPath()).filename().string().c_str()
                              : (dbz3::settings::Region() == "eu"
                                     ? i18n::T("Europa (PAL)", "Europe (PAL)")
                                     : i18n::T("USA (NTSC)", "USA (NTSC)")),
                     dbz3::settings::GpuBackend() == "vulkan" ? "Vulkan" : "D3D12",
                     dbz3::settings::ResolutionScale(),
                     dbz3::settings::PresentEffect().c_str(),
                     LanguageDisplayName(dbz3::settings::Language()));
  ImGui::SameLine();
  const char* region_items[] = {i18n::T("USA (NTSC)", "USA (NTSC)"),
                                       i18n::T("Europa (PAL)", "Europe (PAL)")};
  static const char* region_vals[] = {"us", "eu"};
  int region_idx = dbz3::settings::Region() == "eu" ? 1 : 0;
  ImGui::SetNextItemWidth(150);
  // In ISO mode the region comes from the disc's own default.xex, so the
  // selector is informational only (the disc already contains its region's data).
  ImGui::BeginDisabled(iso_mode);
  if (ImGui::Combo("##region_footer", &region_idx, region_items, 2)) {
    dbz3::settings::SetRegion(region_vals[region_idx]);
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", iso_mode
        ? i18n::T("Region del disco (la elige el propio ISO).",
                  "Disc region (chosen by the ISO itself).")
        : i18n::T("Paquete de texto/audio/video. Requiere reinicio.",
                  "Text/audio/video pack. Restart required."));
  }

  ImGui::Spacing();

  // Utility zone (left) + primary action (right).
  ImGui::SetCursorPosX(16);
  if (ImGui::Button(i18n::T("Restablecer valores", "Reset to defaults"), ImVec2(180, 0))) {
    rex::cvar::SetFlagByName("dbz3_resolution_scale", "1");
    rex::cvar::SetFlagByName("dbz3_language", "1");
    rex::cvar::SetFlagByName("dbz3_region", "us");
    rex::cvar::SetFlagByName("dbz3_mod_profile", "vanilla");
    rex::cvar::SetFlagByName("dbz3_fullscreen_mode", "windowed");
    rex::cvar::SetFlagByName("dbz3_vsync", "true");
    rex::cvar::SetFlagByName("dbz3_frame_cap", "60");
    rex::cvar::SetFlagByName("dbz3_quality_preset", "auto");
    rex::cvar::SetFlagByName("dbz3_gpu_backend", "d3d12");
    rex::cvar::SetFlagByName("dbz3_native_2x_msaa", "true");
    rex::cvar::SetFlagByName("dbz3_anisotropic", "5");
    rex::cvar::SetFlagByName("dbz3_hd_textures", "1");
    rex::cvar::SetFlagByName("dbz3_hd_texture_max_texels", "524288");
    rex::cvar::SetFlagByName("dbz3_present_effect", "fsr");
    rex::cvar::SetFlagByName("dbz3_fsr_quality", "quality");
    rex::cvar::SetFlagByName("dbz3_fsr_sharpness", "0.2");
    rex::cvar::SetFlagByName("dbz3_cas_sharpness", "0.0");
    rex::cvar::SetFlagByName("dbz3_vrr", "false");
    rex::cvar::SetFlagByName("dbz3_fxaa", "none");
    rex::cvar::SetFlagByName("dbz3_present_dither", "false");
    rex::cvar::SetFlagByName("dbz3_async_shaders", "true");
    rex::cvar::SetFlagByName("dbz3_occlusion_queries", "true");
    rex::cvar::SetFlagByName("dbz3_master_volume", "1.0");
    rex::cvar::SetFlagByName("dbz3_mute", "false");
    rex::cvar::SetFlagByName("dbz3_deadzone", "0.1");
    rex::cvar::SetFlagByName("dbz3_rumble", "true");
    rex::cvar::SetFlagByName("dbz3_input_backend", "xinput");
    rex::cvar::SetFlagByName("dbz3_mnk_mode", "true");
    rex::cvar::SetFlagByName("dbz3_mnk_mouse", "false");
    rex::cvar::SetFlagByName("dbz3_mnk_sensitivity", "1.0");
#define DBZ3_RESET_KEYBIND(name) rex::cvar::ResetToDefault("dbz3_keybind_" #name)
    DBZ3_RESET_KEYBIND(a);
    DBZ3_RESET_KEYBIND(b);
    DBZ3_RESET_KEYBIND(x);
    DBZ3_RESET_KEYBIND(y);
    DBZ3_RESET_KEYBIND(left_trigger);
    DBZ3_RESET_KEYBIND(right_trigger);
    DBZ3_RESET_KEYBIND(left_shoulder);
    DBZ3_RESET_KEYBIND(right_shoulder);
    DBZ3_RESET_KEYBIND(lstick_up);
    DBZ3_RESET_KEYBIND(lstick_down);
    DBZ3_RESET_KEYBIND(lstick_left);
    DBZ3_RESET_KEYBIND(lstick_right);
    DBZ3_RESET_KEYBIND(lstick_press);
    DBZ3_RESET_KEYBIND(rstick_up);
    DBZ3_RESET_KEYBIND(rstick_down);
    DBZ3_RESET_KEYBIND(rstick_left);
    DBZ3_RESET_KEYBIND(rstick_right);
    DBZ3_RESET_KEYBIND(rstick_press);
    DBZ3_RESET_KEYBIND(dpad_up);
    DBZ3_RESET_KEYBIND(dpad_down);
    DBZ3_RESET_KEYBIND(dpad_left);
    DBZ3_RESET_KEYBIND(dpad_right);
    DBZ3_RESET_KEYBIND(back);
    DBZ3_RESET_KEYBIND(start);
    DBZ3_RESET_KEYBIND(guide);
#undef DBZ3_RESET_KEYBIND
    // Reset also returns the mods to vanilla (all disabled).
    dbz3::ApplyProfile("vanilla");
    mods_loaded_ = false;
  }
  ImGui::SameLine(0, 10);
  if (ImGui::Button(i18n::T("Guardar ajustes", "Save settings"), ImVec2(180, 0))) {
    dbz3::settings::SaveUserSettings();
  }
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 16.0f - 300.0f);
  // PLAY is gated on the assets being found: pressing it with no game data
  // would crash before the guest even starts (P1). The banner above offers the
  // folder picker to fix it.
  ImGui::BeginDisabled(!assets_ready);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.62f, 0.28f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.74f, 0.36f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.11f, 0.50f, 0.22f, 1.0f));
  if (ImGui::Button("PLAY", ImVec2(300, 42)) ||
      (assets_ready && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
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
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();

  ImGui::End();
}

void LauncherDialog::DrawVideoTab() {
  // Two side-by-side columns so every control fits in the fixed 1280x720
  // launcher window without scrollbars (compact one-screen layout).
  const float avail_x = ImGui::GetContentRegionAvail().x;
  const float col_w = (avail_x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

  ImGui::BeginChild("##video_left", ImVec2(col_w, -kFooterHeight), false);
  {
    // Focus behaviour, first thing in the first tab: it is what a non-technical
    // user looks for ("what happens if I switch to Discord?"), and it is safe by
    // design -- it silences and dims, it never freezes the game.
    PushSectionHeader(i18n::T("Al salir de la ventana", "When you leave the window"));
    bool mute_unfocused = dbz3::settings::MuteUnfocused();
    if (ImGui::Checkbox(i18n::T("Silenciar el audio", "Mute audio"), &mute_unfocused)) {
      dbz3::settings::SetMuteUnfocused(mute_unfocused);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Si pasas a otra ventana, el sonido se corta solo y vuelve al regresar. "
          "El juego sigue en marcha.",
          "If you switch to another window, the sound cuts out and comes back when "
          "you return. The game keeps running."));
    }
    bool dim_unfocused = dbz3::settings::DimUnfocused();
    if (ImGui::Checkbox(i18n::T("Oscurecer la pantalla", "Dim the screen"),
                        &dim_unfocused)) {
      dbz3::settings::SetDimUnfocused(dim_unfocused);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Mientras el juego esta detras, la imagen se oscurece y avisa de que "
          "sigue en marcha: se ve de un vistazo desde la barra de tareas.",
          "While the game is behind, the picture darkens and says it is still "
          "running: obvious at a glance from the taskbar."));
    }
    ImGui::Spacing();

    PushSectionHeader(i18n::T("Calidad de imagen", "Image Quality"));

    // Detected GPU + one-click quality presets. Detection is a cheap DXGI
    // query and only runs once (cached in settings.cpp).
    static bool gpu_info_checked = false;
    static std::string gpu_name;
    static int gpu_tier = 1;
    if (!gpu_info_checked) {
      gpu_info_checked = true;
      gpu_name = dbz3::settings::DetectGpuName();
      gpu_tier = dbz3::settings::DetectGpuTier();
    }
    if (!gpu_name.empty()) {
      ImGui::TextDisabled(i18n::T("GPU: %s", "GPU: %s"), gpu_name.c_str());
      ImGui::SameLine();
      const char* tier_label = gpu_tier == 0 ? i18n::T("Baja", "Low")
                                             : (gpu_tier == 2 ? i18n::T("Alta", "High")
                                                              : i18n::T("Media", "Medium"));
      ImGui::TextColored(kTextDim, " - %s: %s", i18n::T("nivel detectado", "detected tier"),
                         tier_label);
    } else {
      ImGui::TextDisabled(i18n::T("GPU: no detectado", "GPU: not detected"));
    }

    const char* preset_items[] = {
        i18n::T("Automatico (recomendado)", "Automatic (recommended)"),
        i18n::T("Rendimiento", "Performance"),
        i18n::T("Equilibrado", "Balanced"),
        i18n::T("Calidad", "Quality"),
        i18n::T("Personalizado", "Custom")};
    static const char* preset_vals[] = {"auto", "performance", "balanced", "quality", "manual"};
    int preset_idx = 0;
    std::string preset = dbz3::settings::QualityPreset();
    for (int i = 0; i < 5; i++) {
      if (preset == preset_vals[i]) preset_idx = i;
    }
    if (ImGui::Combo(i18n::T("Modo de calidad", "Quality mode"), &preset_idx, preset_items, 5)) {
      dbz3::settings::SetQualityPreset(preset_vals[preset_idx]);
      // Apply immediately: named presets persist their values, "auto" detects
      // the GPU now, "manual" leaves the individual controls untouched.
      dbz3::settings::ApplyQualityPreset();
      dbz3::settings::SaveUserSettings();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Elige que priorizar. Rendimiento = lo mas fluido en equipos modestos; "
          "Equilibrado = buena imagen con poco coste; Calidad = la mejor imagen "
          "sin disparar la GPU. Automatico detecta tu GPU y elige por ti. Ninguno "
          "sube la escala interna (el supersampling se ajusta aparte, abajo). Las "
          "opciones de abajo solo se tocan en Personalizado.",
          "Choose what to prioritize. Performance = smoothest on modest PCs; "
          "Balanced = good image at low cost; Quality = best image without "
          "maxing out the GPU. Automatic detects your GPU and picks for you. None "
          "raises the internal scale (supersampling is set separately, below). The "
          "controls below are only touched in Custom."));
    }
    // Visibility for the preset: always show what it currently resolves to, so
    // "auto" isn't a black box (it applies in-memory and re-evaluates on boot).
    {
      const int p_scale = dbz3::settings::ResolutionScale();
      const bool p_msaa = dbz3::settings::Native2xMsaa();
      const int p_aniso = dbz3::settings::AnisotropicOverride();
      const std::string p_eff = dbz3::settings::PresentEffect();
      ImGui::TextColored(kTextDim,
                         i18n::T("Activo: %s -> %dx, MSAA %s, aniso %d, %s",
                                 "Applied: %s -> %dx, MSAA %s, aniso %d, %s"),
                         preset_items[preset_idx], p_scale, p_msaa ? "ON" : "OFF", p_aniso, p_eff.c_str());
    }

    const char* scale_items[] = {
        i18n::T("1x (nativa 720p) - recomendado", "1x (native 720p) - recommended"),
        i18n::T("2x (interna 1440p) - consume mas GPU", "2x (1440p internal) - higher GPU"),
        i18n::T("3x (interna 2160p) - consume mucho mas GPU", "3x (2160p internal) - much higher GPU"),
        i18n::T("4x (interna 2880p) - solo GPUs de gama alta", "4x (2880p internal) - high-end GPUs only")};
    int scale = dbz3::settings::ResolutionScale();
    int scale_idx = scale - 1;
    if (scale_idx < 0) scale_idx = 0;
    if (scale_idx > 3) scale_idx = 3;
    if (ImGui::Combo(i18n::T("Escala de render interna", "Internal render scale"), &scale_idx,
                     scale_items, 4)) {
      dbz3::settings::SetResolutionScale(scale_idx + 1);
      // Persist immediately: the user often marks the scale and then launches (or
      // closes) without pressing "Save settings". Saving here guarantees the chosen
      // internal resolution is always applied on the next boot.
      dbz3::settings::SaveUserSettings();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Supersampling del framebuffer de 720p: el juego renderiza de verdad a "
          "mayor resolucion y luego se reduce. Reduce muy bien el aliasing, pero "
          "multiplica el consumo de GPU y potencia (cada paso hacia arriba cuesta "
          "aprox. el doble). Empieza por 1x. Requiere reinicio.",
          "Supersampling of the 720p framebuffer: the game really renders at a "
          "higher resolution and is then downscaled. It greatly reduces aliasing, "
          "but multiplies GPU load and power draw (each step up costs roughly "
          "twice as much). Start at 1x. Restart required."));
    }
    // Aviso fuerte y accion de revertir: un usuario puede subir la escala sin
    // entender que ahi esta el coste real (no en las texturas HD). Se le da el
    // dato claro y el boton para volver a lo razonable en un clic.
    if (scale > 1) {
      ImGui::PushStyleColor(ImGuiCol_Text, kDragonOrangeDim);
      ImGui::TextWrapped("%s",
                         i18n::T("Supersampling activo: la GPU trabajara mucho mas "
                                 "(no es necesario para jugar perfectamente a 720p).",
                                 "Supersampling on: the GPU will work much harder "
                                 "(not needed to play 720p flawlessly)."));
      ImGui::PopStyleColor();
      if (ImGui::SmallButton(i18n::T("Volver a nativo (1x)", "Back to native (1x)"))) {
        dbz3::settings::SetResolutionScale(1);
        dbz3::settings::SetQualityPreset("manual");
        dbz3::settings::SaveUserSettings();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T(
            "Deja la escala interna en 1x (render nativo 720p). Es como mejor "
            "rinde y la calidad ya es alta gracias al escalado FSR/CAS.",
            "Sets the internal scale back to 1x (native 720p render). It performs "
            "best this way and quality is already high thanks to FSR/CAS."));
      }
    }

    // HD textures (WIP): emulator-style internal filter. It upscales the game's
    // textures (DXT and the large native RGBA8 ones) at Nx in the host texture
    // cache with a bicubic pass and a generated mip chain, without touching the
    // game's files or its memory budget. The stutter caused by the mip
    // generation was fixed (bounded block sampling), so it now holds 60 FPS in
    // combat; the cost is GPU load / VRAM (x2 is light, x3 is heavy), so it is
    // opt-in and off by default, and capped at x3. The advanced VRAM knob lives
    // in the Dev tab.
    static const char* hdtex_items[] = {
        i18n::T("Desactivado (recomendado)", "Off (recommended)"),
        i18n::T("Nitidas (uso ligero)", "Sharp (light)"),
        i18n::T("Muy nitidas (exigente)", "Very sharp (demanding)")};
    int hdtex = dbz3::settings::HdTextures();
    int hdtex_idx = hdtex - 1;
    if (hdtex_idx < 0) hdtex_idx = 0;
    if (hdtex_idx > 2) hdtex_idx = 2;
    if (ImGui::Combo(i18n::T("Mejora de texturas (experimental)",
                             "Texture enhancement (experimental)"),
                     &hdtex_idx, hdtex_items, 3)) {
      dbz3::settings::SetHdTextures(hdtex_idx + 1);
      // Persist immediately (same reason as the render scale: the user may just
      // launch or close without pressing "Save settings").
      dbz3::settings::SaveUserSettings();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Aumenta la nitidez de las texturas del juego (caras, ropa, escenarios). "
          "Funciona con la GPU: sube bastante el consumo y el uso de VRAM. Empieza "
          "por \"Nitidas\"; si notas ralentizaciones, vuelve a Desactivado. "
          "Requiere reiniciar el juego.",
          "Sharpens the game's textures (faces, clothes, stages). It runs on the "
          "GPU: noticeably raises power draw and VRAM usage. Start with \"Sharp\"; "
          "if you notice slowdowns, switch back to Off. Requires restarting the "
          "game."));
    }
    if (dbz3::settings::HdTextures() > 1) {
      ImGui::TextColored(kDragonOrangeDim, "%s",
                         i18n::T("Experimental: mayor consumo de GPU y VRAM",
                                 "Experimental: higher GPU and VRAM usage"));
    }

    bool msaa = dbz3::settings::Native2xMsaa();
    if (ImGui::Checkbox(i18n::T("MSAA 2x nativo", "Native 2x MSAA"), &msaa)) {
      dbz3::settings::SetNative2xMsaa(msaa);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Suaviza los bordes (menos dientes de sierra) con un coste moderado de "
          "GPU. Se nota sobre todo con la escala interna en 1x; si subes la escala, "
          "puedes quitarlo sin perder mucha calidad.",
          "Smooths edges (less aliasing) at a moderate GPU cost. It helps most with "
          "the internal scale at 1x; if you raise the scale, you can turn it off "
          "without losing much quality."));
    }

    static const char* aniso_items[] = {"Off", "1x", "2x", "4x", "8x", "16x"};
    static const int aniso_values[] = {0, 1, 2, 3, 4, 5};
    int aniso = dbz3::settings::AnisotropicOverride();
    int aniso_idx = 0;
    for (int i = 0; i < 6; i++) {
      if (aniso == aniso_values[i]) aniso_idx = i;
    }
    if (ImGui::Combo(i18n::T("Filtrado anisotropico", "Anisotropic filtering"), &aniso_idx,
                     aniso_items, 6)) {
      dbz3::settings::SetAnisotropicOverride(aniso_values[aniso_idx]);
    }

    PushSectionHeader(i18n::T("Idioma", "Language"));

    static const char* lang_items[] = {"English", "Japanese", "German", "French", "Spanish", "Italian"};
    static const int lang_ids[] = {1, 2, 3, 4, 5, 6};
    int lang = dbz3::settings::Language();
    int lang_idx = 0;
    for (int i = 0; i < 6; i++) {
      if (lang == lang_ids[i]) lang_idx = i;
    }
    if (ImGui::Combo(i18n::T("Idioma del launcher y del juego", "Launcher and game language"),
                     &lang_idx, lang_items, 6)) {
      dbz3::settings::SetLanguage(lang_ids[lang_idx]);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Traduce el launcher y el texto del juego a este idioma. "
          "Se aplica en el proximo arranque.",
          "Translates the launcher and the in-game text to this language. "
          "Applies on the next launch."));
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##video_right", ImVec2(0, -kFooterHeight), false);
  {
    PushSectionHeader(i18n::T("Pantalla", "Display"));

    const char* modes[] = {i18n::T("Ventana", "Windowed"),
                                  i18n::T("Sin bordes", "Borderless"),
                                  i18n::T("Pantalla completa exclusiva", "Exclusive Fullscreen")};
    int mode_idx = 0;
    std::string mode = dbz3::settings::FullscreenMode();
    if (mode == "borderless") mode_idx = 1;
    else if (mode == "exclusive") mode_idx = 2;
    if (ImGui::Combo(i18n::T("Modo de pantalla", "Fullscreen mode"), &mode_idx, modes, 3)) {
      dbz3::settings::SetFullscreenMode(mode_idx == 0 ? "windowed" : (mode_idx == 1 ? "borderless" : "exclusive"));
    }

    ImGui::TextDisabled(i18n::T("Velocidad del juego: fija a 60 FPS (sincronizada)",
                                "Game speed: fixed 60 FPS (synchronized)"));
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "El ritmo del juego se sincroniza a 60 Hz (vblank del guest). "
          "No es configurable: sin esta sincronizacion el juego corre acelerado.",
          "The game logic is synchronized to 60 Hz (guest vblank). "
          "Not configurable: without it the game runs too fast."));
    }

    int cap = dbz3::settings::FrameCap();
    if (ImGui::SliderInt(i18n::T("Limite de fotogramas (FPS)", "Frame cap (FPS)"), &cap, 0, 240,
                         cap == 0 ? i18n::T("Sin limite", "Uncapped") : "%d FPS")) {
      dbz3::settings::SetFrameCap(dbz3::settings::SafeFrameCap(cap));
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Limita la velocidad de presentacion en pantalla, NO la velocidad "
          "del juego (esa es siempre 60). 60 = fluido; 30 = menos carga en "
          "GPUs integradas; 0 = sin limite.",
          "Limits the on-screen present rate, NOT the game speed (always 60). "
          "60 = smooth; 30 = less load on integrated GPUs; 0 = uncapped."));
    }

    bool vrr = dbz3::settings::VrrEnabled();
    if (ImGui::Checkbox(i18n::T("Frecuencia variable (G-Sync/FreeSync)",
                                "Variable refresh rate (G-Sync/FreeSync)"), &vrr)) {
      dbz3::settings::SetVrrEnabled(vrr);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Sincroniza el monitor con cada fotograma para evitar saltos "
          "en pantallas de alta frecuencia. Si esta desactivado, el "
          "juego ajusta el ritmo solo para que se vea fluido a 60 Hz.",
          "Syncs the monitor to every frame for even pacing on high-refresh "
          "panels. If off, the game paces itself to look smooth at 60 Hz."));
    }

    // Detected once (EnumDisplaySettingsW is a system call; no need to repeat
    // it every frame). Just informational in the UI.
    static double cached_hz = 0.0;
    if (cached_hz == 0.0) {
      cached_hz = dbz3::settings::DetectRefreshRate();
    }
    const int hz_int = static_cast<int>(cached_hz + 0.5);
    if (hz_int >= 30) {
      ImGui::TextDisabled(i18n::T("Monitor: %d Hz", "Monitor: %d Hz"), hz_int);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T(
            "El launcher no depende del refresco del monitor. El "
            "juego se ve fluido a cualquier Hz.",
            "The launcher does not depend on the monitor refresh. "
            "The game looks smooth at any Hz."));
      }
    } else {
      ImGui::TextDisabled(i18n::T("Monitor: no detectado (se asume 60 Hz).",
                                  "Monitor: not detected (assumed 60 Hz)."));
    }

    ImGui::Spacing();
    static const char* backends[] = {"Direct3D 12", "Vulkan (experimental)"};
    static const char* backend_vals[] = {"d3d12", "vulkan"};
    int backend_idx = 0;
    std::string backend = dbz3::settings::GpuBackend();
    for (int i = 0; i < 2; i++) {
      if (backend == backend_vals[i]) backend_idx = i;
    }
    if (ImGui::Combo("##gpu_backend", &backend_idx, backends, 2)) {
      dbz3::settings::SetGpuBackend(backend_vals[backend_idx]);
    }
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "%s", i18n::T("Motor grafico", "Graphics backend"));
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Vulkan es experimental: el combate 3D corre notablemente mas lento "
          "que D3D12 en hardware NVIDIA. Usa D3D12 salvo que necesites Vulkan "
          "por compatibilidad.",
          "Vulkan is experimental: 3D combat runs notably slower "
          "than D3D12 on NVIDIA hardware. Use D3D12 unless you need "
          "Vulkan for platform compatibility."));
    }
    ImGui::TextWrapped(i18n::T("API de render del host. Requiere reinicio.",
                               "Host rendering API. Restart required."));
  }
  ImGui::EndChild();
}


void LauncherDialog::DrawUpscaleTab() {
  ImGui::BeginChild("##upscale_settings", ImVec2(0, -kFooterHeight), false);

  PushSectionHeader(i18n::T("Escalado", "Upscaling"));

  const char* effects[] = {"Bilinear", i18n::T("CAS (nitidez)", "CAS (sharpen)"),
                                "FSR 1 (FidelityFX)"};
  static const char* effect_values[] = {"bilinear", "cas", "fsr"};
  int eff_idx = 0;
  std::string eff = dbz3::settings::PresentEffect();
  for (int i = 0; i < 3; i++) {
    if (eff == effect_values[i]) eff_idx = i;
  }
  if (ImGui::Combo(i18n::T("Efecto", "Effect"), &eff_idx, effects, 3)) {
    dbz3::settings::SetPresentEffect(effect_values[eff_idx]);
    // Persist immediately so the chosen upscaling effect survives a launch/close
    // without the user pressing "Save settings".
    dbz3::settings::SaveUserSettings();
  }

  if (dbz3::settings::PresentEffect() == "fsr") {
    ImGui::TextWrapped(i18n::T(
        "FSR escala el render interno al tamano de la pantalla.\n"
        "Ideal con una escala interna baja (1x) en pantallas 1080p o superiores.",
        "FSR upscales the internal render to the display size.\n"
        "Best paired with a low internal scale (1x) on a 1080p+ display."));
    ImGui::Spacing();
    double sharp = dbz3::settings::FsrSharpness();
    ImGui::SetNextItemWidth(260);
    if (SliderD(i18n::T("Nitidez RCAS", "RCAS sharpness"), &sharp, 0.0, 2.0, "%.2f")) {
      dbz3::settings::SetFsrSharpness(sharp);
      dbz3::settings::SaveUserSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(i18n::T("0 = mas nitido, 2 = mas suave",
                                "0 = sharper, 2 = softer"));
  } else if (dbz3::settings::PresentEffect() == "cas") {
    ImGui::TextWrapped(i18n::T(
        "CAS aplica nitidez adaptativa al contraste despues del escalado.\n"
        "Muy bueno con una escala interna alta (2x-3x).",
        "CAS applies contrast-adaptive sharpening after scaling.\n"
        "Great with a high internal scale (2x-3x)."));
    ImGui::Spacing();
    double sharp = dbz3::settings::CasSharpness();
    ImGui::SetNextItemWidth(260);
    if (SliderD(i18n::T("Nitidez adicional", "Additional sharpness"), &sharp, 0.0,
                1.0, "%.2f")) {
      dbz3::settings::SetCasSharpness(sharp);
      dbz3::settings::SaveUserSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(i18n::T("0 = solo CAS, 1 = maxima nitidez",
                                "0 = CAS only, 1 = max sharpness"));
  } else {
    ImGui::TextWrapped(i18n::T("Escalado bilineal, el camino mas simple. Sin nitidez.",
                               "Bilinear upscaling - the simplest path. No sharpening."));
  }

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Antialiasing y filtros", "Anti-aliasing and filters"));

  const char* fxaa_items[] = {i18n::T("Desactivado", "Off"), "FXAA",
                              i18n::T("FXAA extremo", "FXAA extreme")};
  static const char* fxaa_vals[] = {"none", "fxaa", "fxaa_extreme"};
  int fxaa_idx = 0;
  std::string fxaa = dbz3::settings::Fxaa();
  for (int i = 0; i < 3; i++) {
    if (fxaa == fxaa_vals[i]) fxaa_idx = i;
  }
  if (ImGui::Combo(i18n::T("Suavizado de bordes (FXAA)", "Edge smoothing (FXAA)"),
                   &fxaa_idx, fxaa_items, 3)) {
    dbz3::settings::SetFxaa(fxaa_vals[fxaa_idx]);
    dbz3::settings::SaveUserSettings();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Suaviza los bordes en el ultimo paso. Muy barato: ideal si subir la "
        "escala interna o el MSAA te cuesta demasiado rendimiento. Se combina "
        "con FSR/CAS.",
        "Smooths edges in the final step. Very cheap: ideal if raising the "
        "internal scale or MSAA costs you too much performance. Composes with "
        "FSR/CAS."));
  }

  bool dither = dbz3::settings::PresentDither();
  if (ImGui::Checkbox(i18n::T("Tramado de color (dither)", "Color dithering"),
                      &dither)) {
    dbz3::settings::SetPresentDither(dither);
    dbz3::settings::SaveUserSettings();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Aplica tramado a la imagen final: menos bandas en los degradados, a "
        "cambio de un poco de ruido.",
        "Dithers the final image: fewer gradient bands, at the cost of a little "
        "noise."));
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim,
                     i18n::T("El efecto elegido se aplica en el proximo arranque (requiere reinicio).",
                             "The chosen effect applies on the next boot (restart required)."));

  ImGui::EndChild();
}

void LauncherDialog::DrawAudioTab() {
  ImGui::BeginChild("##audio_settings", ImVec2(0, -kFooterHeight), false);

  PushSectionHeader(i18n::T("Volumen", "Volume"));

  // Real controls: the SDK multiplies the guest mix by `audio_gain` (hot
  // reloadable, applied in the SDL callback) and `audio_mute` hard-silences it.
  // Both are applied to the running game as soon as they change, so the effect
  // is audible without pressing Play again.
  double master = dbz3::settings::MasterVolume();
  if (SliderD(i18n::T("Volumen general", "Master volume"), & master, 0.0, 1.0, "%.2f")) {
    dbz3::settings::SetMasterVolume(master);
    dbz3::settings::ApplyRuntimeSettingsToSdk(false);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", i18n::T("0 = silencio, 1 = maximo",
                                    "0 = silent, 1 = max"));

  bool mute = dbz3::settings::AudioMute();
  if (ImGui::Checkbox(i18n::T("Silenciar todo el audio", "Mute all audio"), &mute)) {
    dbz3::settings::SetAudioMute(mute);
    dbz3::settings::ApplyRuntimeSettingsToSdk(false);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Silencia el juego por completo (equivale a volumen 0).",
        "Silences the game completely (same as volume 0)."));
  }

  ImGui::Spacing();
  ImGui::TextWrapped(i18n::T(
      "El juego mezcla todo el audio en una sola pista, asi que solo se puede "
      "ajustar el volumen general o silenciarlo. Las pistas de voz (japones/"
      "ingles) se eligen dentro del juego.",
      "The game mixes all audio into a single stream, so only the master volume "
      "or silence can be adjusted. Voice tracks (Japanese/English) are selected "
      "in-game."));

  ImGui::EndChild();
}

void LauncherDialog::DrawInputTab() {
  ImGui::BeginChild("##input_settings", ImVec2(0, -kFooterHeight), false);

  PushSectionHeader(i18n::T("Mando", "Controller"));

  const char* backend_items[] = {i18n::T("XInput (nativo)", "XInput (native)"),
                                        i18n::T("SDL (mandos genericos)", "SDL (generic pads)")};
  static const char* backend_values[] = {"xinput", "sdl"};
  std::string backend = dbz3::settings::InputBackend();
  int backend_idx = backend == "sdl" ? 1 : 0;
  if (ImGui::Combo(i18n::T("Backend del mando", "Controller backend"), &backend_idx,
                   backend_items, 2)) {
    dbz3::settings::SetInputBackend(backend_values[backend_idx]);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "XInput = mandos de PC estandar, sin init extra. SDL = mandos genericos, "
        "pero puede colgar con RTSS/OBS. Requiere reinicio.",
        "XInput = standard PC pads, no extra init. SDL = generic pads, "
        "but may hang with RTSS/OBS. Restart to apply."));
  }

  double deadzone = dbz3::settings::Deadzone();
  if (SliderD(i18n::T("Zona muerta de los sticks", "Analog stick deadzone"), &deadzone, 0.0, 0.9,
              "%.2f")) {
    dbz3::settings::SetDeadzone(deadzone);
  }

  bool rumble = dbz3::settings::RumbleEnabled();
  if (ImGui::Checkbox(i18n::T("Activar vibracion", "Enable vibration"), &rumble)) {
    dbz3::settings::SetRumbleEnabled(rumble);
  }

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Teclado / Raton", "Keyboard / Mouse"));

  bool mnk = dbz3::settings::MnkMode();
  if (ImGui::Checkbox(i18n::T("Emular mando con teclado/raton",
                              "Enable keyboard/mouse emulation"), &mnk)) {
    dbz3::settings::SetMnkMode(mnk);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Emula un mando con el teclado. Necesario para jugar sin pad. Usa las teclas de abajo.",
        "Emulates a controller with the keyboard. Required to play "
        "without a pad. Use the keybinds below."));
  }

  bool mnk_mouse = dbz3::settings::MnkMouse();
  if (ImGui::Checkbox(i18n::T("Usar el raton para el stick derecho",
                              "Use mouse for the right stick"), &mnk_mouse)) {
    dbz3::settings::SetMnkMouse(mnk_mouse);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Mueve el stick derecho con el raton (ademas de las teclas rstick_*).",
        "Moves the right stick with the mouse (in addition to the rstick_* keys)."));
  }

  if (dbz3::settings::MnkMouse()) {
    double sens = dbz3::settings::MnkSensitivity();
    if (SliderD(i18n::T("Sensibilidad del raton", "Mouse sensitivity"), &sens, 0.1,
                5.0, "%.2f")) {
      dbz3::settings::SetMnkSensitivity(sens);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", i18n::T("1.0 = por defecto", "1.0 = default"));
  }

  ImGui::Spacing();
  ImGui::Text(i18n::T("Mapeo de teclas (MnK)", "Keyboard (MnK) mapping"));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Los nombres de tecla siguen VirtualKey (p.ej. Space, W, Up, LMB, RMB, MMB). "
        "Coma = alternativas, Shift+/Ctrl+/Alt+ = modificadores. Vacio = sin asignar.",
        "Key names follow VirtualKey (e.g. Space, W, Up, LMB, RMB, MMB). "
        "Comma = alternatives, Shift+/Ctrl+/Alt+ = modifiers. Empty = unbound."));
  }

  // Three-column grid so all 24 keybinds fit in the 720p window without
  // scrolling (compact one-screen layout).
  {
    const float kb_w =
        ImGui::GetContentRegionAvail().x / 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f / 3.0f;
    int kb_col = 0;
#define DBZ3_DRAW_KEYBIND(name)                                        \
  do {                                                                 \
    std::string name = dbz3::settings::Keybind(#name);                 \
    ImGui::SetNextItemWidth(kb_w);                                     \
    DrawKeybind(#name, name);                                          \
    dbz3::settings::SetKeybind(#name, name);                           \
    if (kb_col % 3 != 2) ImGui::SameLine();                            \
    ++kb_col;                                                          \
  } while (0)
    DBZ3_DRAW_KEYBIND(a);
    DBZ3_DRAW_KEYBIND(b);
    DBZ3_DRAW_KEYBIND(x);
    DBZ3_DRAW_KEYBIND(y);
    DBZ3_DRAW_KEYBIND(left_trigger);
    DBZ3_DRAW_KEYBIND(right_trigger);
    DBZ3_DRAW_KEYBIND(left_shoulder);
    DBZ3_DRAW_KEYBIND(right_shoulder);
    DBZ3_DRAW_KEYBIND(lstick_up);
    DBZ3_DRAW_KEYBIND(lstick_down);
    DBZ3_DRAW_KEYBIND(lstick_left);
    DBZ3_DRAW_KEYBIND(lstick_right);
    DBZ3_DRAW_KEYBIND(lstick_press);
    DBZ3_DRAW_KEYBIND(rstick_up);
    DBZ3_DRAW_KEYBIND(rstick_down);
    DBZ3_DRAW_KEYBIND(rstick_left);
    DBZ3_DRAW_KEYBIND(rstick_right);
    DBZ3_DRAW_KEYBIND(rstick_press);
    DBZ3_DRAW_KEYBIND(dpad_up);
    DBZ3_DRAW_KEYBIND(dpad_down);
    DBZ3_DRAW_KEYBIND(dpad_left);
    DBZ3_DRAW_KEYBIND(dpad_right);
    DBZ3_DRAW_KEYBIND(back);
    DBZ3_DRAW_KEYBIND(start);
    DBZ3_DRAW_KEYBIND(guide);
#undef DBZ3_DRAW_KEYBIND
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim, i18n::T(
      "El remapeo completo de botones tambien esta disponible en el menu "
      "de ajustes en juego (F4).",
      "Full button remapping is also available in the in-game Settings overlay (F4)."));

  ImGui::EndChild();
}

void LauncherDialog::DrawNativeModsTab() {
  ImGui::BeginChild("##native_mods_settings", ImVec2(0, -kFooterHeight), true);
  ImGui::SeparatorText(i18n::T("Mods nativos", "Native mods"));
  ImGui::TextWrapped("%s", i18n::T(
      "Estos mods cambian el progreso o el comportamiento del juego. No sustituyen modelos, texturas ni archivos AFS.",
      "These mods change game progress or behavior. They do not replace models, textures, or AFS files."));
  ImGui::Spacing();

  const auto saves = dbz3::FindNativeSaveFiles();
  if (saves.empty()) {
    ImGui::TextColored(kDragonOrangeDim, "%s", i18n::T(
        "No se encontro ningun guardado. Arranca el juego y guarda al menos una partida; despues vuelve aqui.",
        "No save was found. Start the game and save at least once, then come back here."));
  } else {
    ImGui::TextDisabled(i18n::T("Guardados detectados: %d", "Detected saves: %d"),
                       static_cast<int>(saves.size()));
    for (size_t i = 0; i < saves.size(); ++i) {
      const auto& save = saves[i];
      ImGui::BulletText("%s", save.parent_path().filename().string().c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", save.string().c_str());
      }
      if (!dbz3::IsSupportedNativeSave(save)) {
        ImGui::SameLine();
        ImGui::TextColored(kDragonOrangeDim, "%s",
                           i18n::T("formato no reconocido", "unknown format"));
      } else {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(i18n::T("Crear copia de seguridad",
                                       "Create backup"))) {
          std::filesystem::path backup;
          std::string error;
          if (dbz3::BackupNativeSave(save, backup, error)) {
            native_mods_status_ = std::string(i18n::T(
                "Copia creada: ", "Backup created: ")) + backup.string();
          } else {
            native_mods_status_ = std::string(i18n::T(
                "No se pudo crear la copia: ", "Backup failed: ")) + error;
          }
        }
        ImGui::PopID();
      }
    }
  }

  ImGui::Separator();
  for (const auto& mod : dbz3::NativeModCatalog()) {
    ImGui::PushID(mod.id.c_str());
    ImGui::Text("%s", mod.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", mod.scope.c_str());
    ImGui::TextWrapped("%s", mod.description.c_str());
    if (mod.state == dbz3::NativeModState::kReady) {
      if (ImGui::Button(i18n::T("Aplicar", "Apply"))) {
        native_mods_status_ = std::string(i18n::T(
            "Este mod aun necesita validacion del formato del guardado.",
            "This mod still needs save-format validation."));
      }
    } else if (mod.state == dbz3::NativeModState::kNeedsResearch) {
      ImGui::BeginDisabled();
      ImGui::Button(i18n::T("En investigacion", "Under research"));
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T(
            "No se activa hasta validar el guardado y su copia de seguridad. Esto evita corromper tu partida.",
            "It will not activate until the save format and backup flow are validated. This prevents save corruption."));
      }
    } else {
      ImGui::BeginDisabled();
      ImGui::Button(i18n::T("Sin codigo encontrado", "No known code"));
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T(
            "No existe actualmente un parche Xenia conocido para este juego. Se necesita investigacion propia del guest.",
            "There is currently no known Xenia patch for this game. Guest-specific research is required."));
      }
    }
    ImGui::Separator();
    ImGui::PopID();
  }

  if (!native_mods_status_.empty()) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "%s",
                       native_mods_status_.c_str());
  }
  ImGui::EndChild();
}

void LauncherDialog::DrawModsTab() {
  ImGui::BeginChild("##mods_settings", ImVec2(0, -kFooterHeight), true);

  // Refresh the cached list when the async pipeline finishes: a swap/texture
  // build just wrote a new mod into mods/ and the "activates itself and shows up
  // in the Mods tab" promise must hold without a manual Refresh.
  if (mod_pipeline_.Generation() != last_pipeline_gen_) {
    last_pipeline_gen_ = mod_pipeline_.Generation();
    mods_loaded_ = false;
  }

  // Cached mod list: rebuilt lazily (first draw, after toggles/installs/edits,
  // or via the Refresh button) so we don't recurse the whole mods folder every
  // single frame.
  if (!mods_loaded_) {
    mods_cache_ = dbz3::ListMods();
    mods_loaded_ = true;
  }

  ImGui::SeparatorText(i18n::T("Centro de mods", "Mod center"));
  ImGui::TextDisabled(i18n::T(
      "Sobrescriben entradas dentro de los .afs del juego (modelos, movesets, "
      "texturas) sin reempaquetar. Cada mod es una carpeta en 'mods/'.",
      "Override entries inside the game's .afs containers (models, move sets, "
      "textures) without repacking. Each mod is a folder under 'mods/'."));

  // ISO mode: the per-entry AFS override hooks resolve to host files on the
  // extracted folder, so mods are NOT applied when playing straight from a
  // .iso. Warn prominently instead of silently ignoring every mod.
  if (dbz3::settings::IsIsoMode()) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
    ImGui::TextWrapped(i18n::T(
        "Aviso: estas en modo disco (ISO). Los mods NO se aplican al jugar "
        "directamente del .iso. Para usarlos, elige 'Carpeta extraida' como "
        "origen de los datos (en el selector de origen).",
        "Warning: you are in disc mode (ISO). Mods are NOT applied when playing "
        "straight from the .iso. To use them, pick 'Extracted folder' as the "
        "data source (in the source selector)."));
    ImGui::PopStyleColor();
  }
  ImGui::Separator();

  // --- Search + bulk actions toolbar ---------------------------------------
  {
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##mods_search",
                             i18n::T("Buscar por nombre, autor, origen...",
                                     "Search by name, author, source..."),
                             mods_search_buf_, sizeof(mods_search_buf_));
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Activar todos", "Enable all"), ImVec2(0, 0))) {
      for (const dbz3::ModInfo& m : mods_cache_) {
        if (!m.enabled) dbz3::SetModEnabled(m.name, true);
      }
      mods_loaded_ = false;
      mods_status_ =
          std::string(i18n::T("Todos los mods activados.", "All mods enabled."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Desactivar todos", "Disable all"), ImVec2(0, 0))) {
      for (const dbz3::ModInfo& m : mods_cache_) {
        if (m.enabled) dbz3::SetModEnabled(m.name, false);
      }
      mods_loaded_ = false;
      mods_status_ = std::string(i18n::T("Todos los mods desactivados (vanilla).",
                                         "All mods disabled (vanilla)."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Refrescar", "Refresh"), ImVec2(0, 0))) {
      mods_loaded_ = false;
      mods_status_ =
          std::string(i18n::T("Lista de mods actualizada.", "Mod list refreshed."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Abrir carpeta", "Open folder"), ImVec2(0, 0))) {
      const std::filesystem::path mods_dir = dbz3::ModsRoot();
      std::error_code ec;
      std::filesystem::create_directories(mods_dir, ec);
      std::string cmd = "explorer \"" + mods_dir.string() + "\"";
      std::system(cmd.c_str());
    }
    ImGui::Spacing();
  }

  // --- Mods center: profiles + install from .zip ---------------------------
  {
    const std::vector<std::string> profiles = dbz3::ListProfiles();
    std::string cur = dbz3::settings::ModProfile();
    bool cur_valid = (cur == "vanilla");
    if (!cur_valid) {
      for (const auto& p : profiles) {
        if (p == cur) {
          cur_valid = true;
          break;
        }
      }
    }
    if (!cur_valid) {
      cur = "vanilla";
    }

    ImGui::Text("%s", i18n::T("Perfil:", "Profile:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("##mod_profile", cur.c_str())) {
      if (ImGui::Selectable("vanilla", cur == "vanilla")) {
        if (cur != "vanilla") {
          dbz3::ApplyProfile("vanilla");
          dbz3::settings::SetModProfile("vanilla");
          mods_loaded_ = false;
          mods_status_ = std::string(i18n::T(
              "Perfil 'vanilla' aplicado (todos los mods desactivados)",
              "Profile 'vanilla' applied (all mods disabled)"));
        }
      }
      for (const auto& p : profiles) {
        if (ImGui::Selectable(p.c_str(), cur == p)) {
          dbz3::ApplyProfile(p);
          dbz3::settings::SetModProfile(p);
          mods_loaded_ = false;
          mods_status_ =
              std::string(i18n::T("Perfil aplicado: ", "Profile applied: ")) + p;
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Un perfil activa/desactiva un conjunto de mods de una vez. "
          "'vanilla' desactiva todos (juego original).",
          "A profile toggles a set of mods at once. 'vanilla' disables all "
          "(original game)."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Guardar como...", "Save as..."), ImVec2(0, 0))) {
      new_profile_buf_[0] = '\0';
      profile_name_dialog_ = true;
    }
    ImGui::SameLine();
    if (cur != "vanilla") {
      if (ImGui::Button(i18n::T("Borrar perfil", "Delete profile"), ImVec2(0, 0))) {
        dbz3::DeleteProfile(cur);
        dbz3::settings::SetModProfile("vanilla");
        mods_loaded_ = false;
        mods_status_ =
            std::string(i18n::T("Perfil borrado: ", "Profile deleted: ")) + cur;
      }
    }

    // Install from zip (right-aligned).
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 16.0f - 200.0f);
    if (ImGui::Button(i18n::T("Instalar mod (.zip)...", "Install mod (.zip)..."),
                      ImVec2(200, 0))) {
      std::string picked;
      const std::string mods_dir = dbz3::ModsRoot().string();
      if (PickFile(picked, i18n::T("Archivo de mod (.zip)", "Mod zip file (.zip)"),
                   "*.zip", mods_dir)) {
        std::string modname, err;
        if (dbz3::InstallModFromZip(picked, modname, err)) {
          mods_status_ =
              std::string(i18n::T("Mod instalado: ", "Mod installed: ")) + modname;
          mods_loaded_ = false;
        } else {
          mods_status_ =
              std::string(i18n::T("Error al instalar el mod: ",
                                  "Error installing mod: ")) + err;
        }
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Selecciona un archivo .zip con tu mod (carpeta con manifest.txt y "
          "los overrides us/eu). Se descomprime a la carpeta mods y se "
          "activa.",
          "Pick a .zip with your mod (a folder with manifest.txt and the "
          "us/eu overrides). It is extracted to the mods folder and enabled."));
    }

    // Save-as-profile dialog.
    if (profile_name_dialog_) {
      ImGui::Separator();
      ImGui::Text("%s", i18n::T("Guardar estado actual como perfil:",
                                "Save current state as profile:"));
      ImGui::SetNextItemWidth(300);
      ImGui::InputText("##new_profile", new_profile_buf_, sizeof(new_profile_buf_));
      ImGui::SameLine();
      if (ImGui::Button(i18n::T("Guardar", "Save"), ImVec2(0, 0))) {
        std::string pname = new_profile_buf_;
        while (!pname.empty() &&
               (pname.back() == ' ' || pname.back() == '\t')) {
          pname.pop_back();
        }
        if (pname.empty() || pname == "vanilla") {
          mods_status_ =
              std::string(i18n::T("Nombre de perfil invalido.",
                                  "Invalid profile name."));
        } else {
          std::vector<std::string> enabled;
          for (const dbz3::ModInfo& m : dbz3::ListMods()) {
            if (m.enabled) enabled.push_back(m.name);
          }
          dbz3::SaveProfile(pname, enabled);
          dbz3::settings::SetModProfile(pname);
          mods_status_ =
              std::string(i18n::T("Perfil guardado: ", "Profile saved: ")) + pname;
        }
        profile_name_dialog_ = false;
      }
      ImGui::SameLine();
      if (ImGui::Button(i18n::T("Cancelar", "Cancel"), ImVec2(0, 0))) {
        profile_name_dialog_ = false;
      }
    }

    // Transient status line (click to dismiss).
    if (!mods_status_.empty()) {
      ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "%s",
                         mods_status_.c_str());
      if (ImGui::IsItemClicked()) {
        mods_status_.clear();
      }
    }
  }
  ImGui::Separator();

  // Apply the search filter over the cached list.
  std::vector<dbz3::ModInfo> mods;
  {
    const std::string query = mods_search_buf_;
    for (const dbz3::ModInfo& m : mods_cache_) {
      if (IContains(m.name, query) || IContains(m.display_name, query) ||
          IContains(m.description, query) || IContains(m.author, query) ||
          IContains(m.source, query) || IContains(m.target, query) ||
          IContains(dbz3::ModTypeLabel(m.type), query)) {
        mods.push_back(m);
      }
    }
  }
  if (mods_cache_.empty()) {
    ImGui::TextWrapped(i18n::T(
        "No hay mods instalados. Los mods se colocan en la "
        "carpeta 'mods' junto al ejecutable, cada uno en su "
        "propia subcarpeta con un manifest.txt.",
        "No mods installed. Mods go in the 'mods' folder next to the "
        "executable, each in its own subfolder with a manifest.txt."));
    if (ImGui::Button(i18n::T("Abrir carpeta de mods", "Open mods folder"), ImVec2(220, 0))) {
      const std::filesystem::path mods_dir = dbz3::ModsRoot();
      std::error_code ec;
      std::filesystem::create_directories(mods_dir, ec);
      std::string cmd = "explorer \"" + mods_dir.string() + "\"";
      std::system(cmd.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Instalar mod (.zip)...", "Install mod (.zip)..."),
                      ImVec2(200, 0))) {
      std::string picked;
      if (PickFile(picked, i18n::T("Archivo de mod (.zip)", "Mod zip file (.zip)"),
                   "*.zip", dbz3::ModsRoot().string())) {
        std::string modname, err;
        if (dbz3::InstallModFromZip(picked, modname, err)) {
          mods_status_ =
              std::string(i18n::T("Mod instalado: ", "Mod installed: ")) + modname;
          mods_loaded_ = false;
        } else {
          mods_status_ =
              std::string(i18n::T("Error al instalar el mod: ",
                                  "Error installing mod: ")) + err;
        }
      }
    }
    ImGui::TextDisabled(i18n::T(
        "Crea 'mods/' si no existe y la abre en el Explorador. "
        "Copia aqui tu mod descargado y se listara y activara "
        "automaticamente.",
        "Creates 'mods/' if missing and opens it in Explorer. Drop your "
        "downloaded mod here and it will be listed and activated automatically."));
    ImGui::EndChild();
    return;
  }

  if (mods.empty()) {
    ImGui::TextDisabled(i18n::T("Ningun mod coincide con la busqueda.",
                                "No mod matches the search."));
    ImGui::EndChild();
    return;
  }

  int enabled_count = 0;
  for (const dbz3::ModInfo& mod : mods) {
    if (mod.enabled) ++enabled_count;
  }
  ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.0f),
                     i18n::T("%d mods (%d activados)", "%d mods (%d enabled)"),
                     static_cast<int>(mods.size()), enabled_count);
  ImGui::Separator();

  const float table_w = ImGui::GetContentRegionAvail().x;
  if (ImGui::BeginTable("##mods_table", 4,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_PadOuterX |
                            ImGuiTableFlags_NoHostExtendX)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn(i18n::T("Mod", "Mod"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(i18n::T("Tipo", "Type"), ImGuiTableColumnFlags_WidthFixed,
                            std::min(130.0f, table_w * 0.18f));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 56.0f);
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
        mods_loaded_ = false;
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
ImGui::TextDisabled(i18n::T("%d archivo%s", "%d file%s"), mod.file_count,
                      mod.file_count == 1 ? "" : i18n::T("s", "s"));

      ImGui::TableSetColumnIndex(2);
      const int type_col = dbz3::ModTypeColor(mod.type);
      ImVec4 tc = type_col < 0
                      ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                      : ImVec4(((type_col >> 16) & 0xFF) / 255.0f,
                               ((type_col >> 8) & 0xFF) / 255.0f,
                               (type_col & 0xFF) / 255.0f, 1.0f);
      DrawBadge(ModTypeLabelText(mod.type), tc);
      if (mod.enabled) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "ON");
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(i18n::T("%s\n%s\nAutor: %s\nVersion: %s\nTipo: %s\nOrigen: %s\nDestino: %s",
                                  "%s\n%s\nAuthor: %s\nVersion: %s\nType: %s\nSource: %s\nTarget: %s"),
                          title.c_str(),
                          mod.description.empty()
                              ? i18n::T("(sin descripcion)", "(no description)")
                              : mod.description.c_str(),
                          mod.author.empty() ? "-" : mod.author.c_str(),
                          mod.version.empty() ? "-" : mod.version.c_str(),
                          ModTypeLabelText(mod.type), mod.source.c_str(),
                          mod.target.c_str());
      }

      ImGui::TableSetColumnIndex(3);
      if (ImGui::SmallButton(("##folder_" + mod.name).c_str())) {
        std::string cmd = "explorer \"" + (dbz3::ModsRoot() / mod.name).string() + "\"";
        std::system(cmd.c_str());
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T("Abrir carpeta del mod", "Open mod folder"));
      }
      ImGui::SameLine();
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
        ImGui::SetTooltip("%s", i18n::T("Editar descripcion / autor / version (manifest.txt)",
                                  "Edit description / author / version (manifest.txt)"));
      }
    }
    ImGui::EndTable();
  }

  // Inline edit dialog for the selected mod's manifest.
  if (editing_mod_) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.90f, 0.85f, 0.50f, 1.0f), i18n::T("Editar mod: %s", "Edit mod: %s"),
                       edit_mod_name_.c_str());
    ImGui::Text(i18n::T("Titulo", "Title"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("##edit_name", edit_name_buf_, sizeof(edit_name_buf_));
    ImGui::Text(i18n::T("Descripcion", "Description"));
    ImGui::InputTextMultiline("##edit_desc", edit_desc_buf_,
                              sizeof(edit_desc_buf_), ImVec2(-1.0f, 64.0f));
    ImGui::Text(i18n::T("Autor", "Author"));
    ImGui::InputText("##edit_author", edit_author_buf_,
                     sizeof(edit_author_buf_));
    ImGui::Text(i18n::T("Version", "Version"));
    ImGui::InputText("##edit_version", edit_version_buf_,
                     sizeof(edit_version_buf_));
    if (ImGui::Button(i18n::T("Guardar", "Save"), ImVec2(120, 0))) {
      if (edit_name_buf_[0] != '\0') {
        dbz3::SetModManifestValue(edit_mod_name_, "name", edit_name_buf_);
      }
      dbz3::SetModManifestValue(edit_mod_name_, "description", edit_desc_buf_);
      dbz3::SetModManifestValue(edit_mod_name_, "author", edit_author_buf_);
      dbz3::SetModManifestValue(edit_mod_name_, "version", edit_version_buf_);
      editing_mod_ = false;
      pending_manifest_reload_ = true;
      mods_loaded_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Cancelar", "Cancel"), ImVec2(120, 0))) {
      editing_mod_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(i18n::T("El texto se guarda en %s/manifest.txt",
                                "The text is saved to %s/manifest.txt"),
                        edit_mod_name_.c_str());
  }

  ImGui::EndChild();
}

void LauncherDialog::DrawModelSwapTab() {
  ImGui::BeginChild("##model_swap", ImVec2(0, -kFooterHeight), true);

  ImGui::SeparatorText(i18n::T("Cambio de modelo B3 HD -> B3 HD", "Model swap B3 HD -> B3 HD"));
  ImGui::TextDisabled(i18n::T(
      "Intercambia el bin #AMB completo de un personaje HD del "
      "B3 por el de otro (swap nativo). Genera el mod y lo activa.",
      "Swaps the full #AMB bin of one B3 HD character for another (native "
      "swap). Generates the mod and activates it."));

  const bool iso = dbz3::settings::IsIsoMode();
  if (iso) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
    ImGui::TextWrapped(i18n::T(
        "Estas en modo disco (ISO): el mod generado NO se aplicara mientras "
        "juegues directamente del .iso. Elige 'Carpeta extraida' como origen "
        "de datos para poder usarlo.",
        "You are in disc mode (ISO): the generated mod will NOT apply while you "
        "play straight from the .iso. Pick 'Extracted folder' as the data "
        "source to use it."));
    ImGui::PopStyleColor();
  }

  // Lazy-load the B3 catalog once (first draw).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped(i18n::T(
        "El catalogo de personajes (catalog_b3.cat) no se "
        "encontro o esta vacio.",
        "The character catalog (catalog_b3.cat) was not found or is empty."));
    ImGui::TextWrapped(i18n::T(
        "El cambio de modelo necesita la carpeta 'mod center hd' "
        "junto al ejecutable, con catalog_b3.cat y swap_b3.py. "
        "No viene incluida en el ZIP de release: descargala del "
        "repositorio (carpeta 'mod center hd') o desde un "
        "release completo, y colocala al lado de dbz3.exe.",
        "Model swap needs the 'mod center hd' folder next to the executable, "
        "with catalog_b3.cat and swap_b3.py. It is not included in the release "
        "ZIP: download it from the repository ('mod center hd' folder) or from "
        "a full release, and place it next to dbz3.exe."));
    ImGui::TextDisabled(i18n::T("Esperado en: %s", "Expected at: %s"),
                        (rex::filesystem::GetExecutableFolder() /
                         "mod center hd" / "catalog_b3.cat")
                            .string()
                            .c_str());
    ImGui::EndChild();
    return;
  }

  // Source AFS: auto-detected by default; the user can point to a custom
  // data_cmn.afs if it lives elsewhere.
  ImGui::SeparatorText(i18n::T("Archivo de modelos (data_cmn.afs)",
                               "Model file (data_cmn.afs)"));
  bool auto_afs = afs_path_auto_;
  if (ImGui::Checkbox(i18n::T("Usar la ruta automatica (us/data_cmn.afs)",
                              "Use the automatic path (us/data_cmn.afs)"), &auto_afs)) {
    afs_path_auto_ = auto_afs;
    if (auto_afs) {
      mod_pipeline_.SetAfsPath("");
    }
  }
  ImGui::BeginDisabled(afs_path_auto_);
  if (ImGui::InputText("##afs_path", afs_path_buf_, sizeof(afs_path_buf_))) {
    mod_pipeline_.SetAfsPath(afs_path_buf_);
  }
  if (ImGui::Button(i18n::T("Buscar...", "Browse..."), ImVec2(120, 0))) {
    // Basic file picker via a native dialog is out of scope here; we let the
    // user paste the full path. Show a hint.
    ImGui::OpenPopup("afs_hint");
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled(i18n::T("Ruta completa al data_cmn.afs del juego",
                              "Full path to the game's data_cmn.afs"));
  if (ImGui::BeginPopup("afs_hint")) {
    ImGui::TextWrapped(i18n::T(
        "Pega la ruta completa, por ejemplo:\n"
        "C:\\...\\us\\data_cmn.afs\n"
        "o la que corresponda a tu instalacion.",
        "Paste the full path, for example:\n"
        "C:\\...\\us\\data_cmn.afs\n"
        "or whichever matches your install."));
    ImGui::EndPopup();
  }

  if (pipeline_src_idx_ >= (int)chars.size()) pipeline_src_idx_ = -1;
  if (pipeline_dst_idx_ >= (int)chars.size()) pipeline_dst_idx_ = -1;

  ImGui::Text(i18n::T("Personaje HD (origen)", "HD character (source)"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  CharacterCombo("##swap_src", "##swap_src_filter", chars, pipeline_src_idx_,
                 swap_src_search_buf_, sizeof(swap_src_search_buf_));
  ImGui::SameLine();
  ImGui::TextDisabled("(%d %s)", static_cast<int>(chars.size()),
                      i18n::T("personajes", "characters"));

  ImGui::Text(i18n::T("Slot destino", "Destination slot"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  CharacterCombo("##swap_dst", "##swap_dst_filter", chars, pipeline_dst_idx_,
                 swap_dst_search_buf_, sizeof(swap_dst_search_buf_));
  ImGui::SameLine();
  ImGui::TextDisabled("(%d %s)", static_cast<int>(chars.size()),
                      i18n::T("personajes", "characters"));

  const bool same_pair =
      pipeline_src_idx_ >= 0 && pipeline_src_idx_ == pipeline_dst_idx_;
  const bool can_swap = pipeline_src_idx_ >= 0 && pipeline_dst_idx_ >= 0 &&
                        !same_pair && !iso;
  ImGui::BeginDisabled(!can_swap || mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Cambiar B3 -> B3", "Swap B3 -> B3"), ImVec2(220, 0))) {
    mod_pipeline_.SwapB3ToB3(chars[pipeline_src_idx_],
                             chars[pipeline_dst_idx_]);
  }
  ImGui::EndDisabled();
  if (same_pair) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s",
                       i18n::T("Origen y destino son el mismo personaje.",
                               "Source and destination are the same character."));
  }

  if (pipeline_src_idx_ >= 0 && pipeline_dst_idx_ >= 0) {
    const B3Char& src = chars[pipeline_src_idx_];
    const B3Char& dst = chars[pipeline_dst_idx_];
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.14f, 0.17f, 1.0f));
    ImGui::BeginChild("##swap_preview", ImVec2(0, 108.0f), true);
    ImGui::TextColored(kDragonOrange, "%s", i18n::T("Vista previa", "Preview"));
    ImGui::Text(i18n::T("Origen:  %s", "Source:  %s"), src.DisplayName().c_str());
    if (!src.playable) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s",
                         i18n::T("(no jugable)", "(not playable)"));
    }
    ImGui::TextDisabled("bin %d", src.bin);
    ImGui::Text(i18n::T("Destino: %s", "Target: %s"), dst.DisplayName().c_str());
    if (!dst.playable) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s",
                         i18n::T("(no jugable)", "(not playable)"));
    }
    ImGui::TextDisabled("slot %d", dst.bin);
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  ImGui::Separator();
  if (mod_pipeline_.IsRunning()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), i18n::T("Trabajando...", "Working..."));
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled(i18n::T("Hecho.", "Done."));
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
  if (!iso) {
    ImGui::TextDisabled(i18n::T(
        "El mod generado se activa solo y se lista en la pestana Mods.",
        "The generated mod activates itself and shows up in the Mods tab."));
  }

  ImGui::EndChild();
}

void LauncherDialog::DrawTexturesTab() {
  ImGui::BeginChild("##textures", ImVec2(0, -kFooterHeight), true);

  ImGui::SeparatorText(i18n::T("Mod de texturas (B3 HD)", "Texture mod (B3 HD)"));
  ImGui::TextDisabled(i18n::T(
      "Extrae las texturas de un personaje como imagenes PNG "
      "editables, y al reconstruir reinserta tus ediciones.",
      "Extracts a character's textures as editable PNG images, and "
      "re-inserts your edits when you rebuild."));

  // Lazy-load the catalog (shared with Model Swap).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped(i18n::T(
        "El catalogo de personajes (catalog_b3.cat) no se "
        "encontro o esta vacio.",
        "The character catalog (catalog_b3.cat) was not found or is empty."));
    ImGui::TextWrapped(i18n::T(
        "El mod de texturas necesita la carpeta 'mod center hd' "
        "junto al ejecutable, con catalog_b3.cat y texture_b3.py. "
        "No viene incluida en el ZIP de release: descargala del "
        "repositorio (carpeta 'mod center hd') o desde un "
        "release completo, y colocala al lado de dbz3.exe.",
        "The texture mod needs the 'mod center hd' folder next to the "
        "executable, with catalog_b3.cat and texture_b3.py. It is not included "
        "in the release ZIP: download it from the repository ('mod center hd' "
        "folder) or from a full release, and place it next to dbz3.exe."));
    ImGui::TextDisabled(i18n::T("Esperado en: %s", "Expected at: %s"),
                        (rex::filesystem::GetExecutableFolder() /
                         "mod center hd" / "catalog_b3.cat")
                            .string()
                            .c_str());
    ImGui::EndChild();
    return;
  }

  if (tex_src_idx_ >= (int)chars.size()) tex_src_idx_ = -1;

  ImGui::Text(i18n::T("Personaje (origen de las texturas)", "Character (texture source)"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  CharacterCombo("##tex_src", "##tex_src_filter", chars, tex_src_idx_,
                 tex_search_buf_, sizeof(tex_search_buf_));
  ImGui::SameLine();
  ImGui::TextDisabled("(%d %s)", static_cast<int>(chars.size()),
                      i18n::T("personajes", "characters"));

  // Slot destino: por defecto el mismo bin del origen (solo texturas), o un
  // personaje distinto (compatible con swaps de modelo: el bin del origen con
  // sus texturas editadas se coloca en el slot del destino).
  if (tex_dst_idx_ >= (int)chars.size()) tex_dst_idx_ = -1;
  ImGui::Text(i18n::T("Slot destino (donde se aplican las texturas)",
                      "Destination slot (where the textures are applied)"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  std::string dst_display;
  if (tex_dst_idx_ >= 0) {
    dst_display = chars[tex_dst_idx_].DisplayName();
  }
  const char* dst_label =
      tex_dst_idx_ >= 0
          ? dst_display.c_str()
          : i18n::T("El mismo personaje (sin swap)", "Same character (no swap)");
  if (ImGui::BeginCombo("##tex_dst", dst_label)) {
    if (ImGui::Selectable(i18n::T("El mismo personaje (sin swap)",
                                  "Same character (no swap)"), tex_dst_idx_ < 0)) {
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
  ImGui::TextDisabled(i18n::T(
      "Si eliges otro personaje, el bin del origen con sus "
      "texturas editadas se coloca en el slot de ese personaje "
      "(para combinar con un swap de modelo).",
      "If you pick another character, the source bin with its edited textures "
      "is placed in that character's slot (to combine with a model swap)."));

  // Nombre del mod de texturas.
  ImGui::Text(i18n::T("Nombre del mod", "Mod name"));
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
  ImGui::Text(i18n::T("Carpeta de texturas (PNG)", "Texture folder (PNG)"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(360);
  ImGui::InputText("##tex_dir", tex_dir_buf_, sizeof(tex_dir_buf_));
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Examinar...", "Browse..."))) {
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
    ImGui::TextDisabled("(%s: %s)", i18n::T("por defecto", "default"), def_dir.c_str());
  }

  const bool can_extract = tex_src_idx_ >= 0;
  ImGui::BeginDisabled(!can_extract || mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Extraer texturas a PNG", "Extract textures to PNG"), ImVec2(220, 0))) {
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
    if (ImGui::Button(i18n::T("Abrir carpeta de texturas", "Open textures folder"),
                      ImVec2(220, 0))) {
      std::string cmd = "explorer \"" + active_tex_dir + "\"";
      std::system(cmd.c_str());
    }
    ImGui::TextDisabled(i18n::T("Edita los PNG en: %s", "Edit the PNGs in: %s"),
                        active_tex_dir.c_str());
  } else {
    ImGui::TextDisabled(i18n::T("Extrae primero las texturas para editar los PNG.",
                                "Extract the textures first to edit the PNGs."));
  }

  ImGui::Separator();
  ImGui::BeginDisabled(!tex_dir_exists || mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Reconstruir mod con texturas editadas",
                            "Rebuild mod with edited textures"), ImVec2(280, 0))) {
    const int dst_slot = tex_dst_idx_ >= 0 ? chars[tex_dst_idx_].bin : -1;
    mod_pipeline_.BuildTextures(mod_name, dst_slot, active_tex_dir);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled(i18n::T(
      "Reinsere los PNG editados de la carpeta, recompila el "
      "bin y genera el mod activo (combinable con un swap de "
      "modelo).",
      "Re-inserts the edited PNGs from the folder, recompiles the bin and "
      "generates the active mod (combinable with a model swap)."));

  // Listar los PNG extraidos de la carpeta activa (para identificar las
  // texturas sin abrir el explorador).
  if (tex_dir_exists) {
    std::vector<std::pair<std::string, std::uintmax_t>> pngs;
    // Non-throwing overload: the folder may vanish between the is_directory
    // check and here (or be unreadable); a thrown filesystem_error out of
    // OnDraw would take the launcher down.
    std::error_code dir_ec;
    for (const auto& e :
         std::filesystem::directory_iterator(active_tex_dir, dir_ec)) {
      if (dir_ec) break;
      std::error_code fec;
      if (e.is_regular_file(fec) && !fec &&
          e.path().extension() == ".png") {
        pngs.emplace_back(e.path().filename().string(), e.file_size(fec));
      }
    }
    if (!pngs.empty()) {
      ImGui::Text(i18n::T("%zu texturas (PNG):", "%zu textures (PNG):"), pngs.size());
      ImGui::SameLine();
      ImGui::TextDisabled(i18n::T("haz clic en 'Abrir carpeta' para verlas",
                                  "click 'Open folder' to view them"));
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
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), i18n::T("Trabajando...", "Working..."));
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled(i18n::T("Hecho.", "Done."));
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
  ImGui::BeginChild("##dev_settings", ImVec2(0, -kFooterHeight), false);

  PushSectionHeader(i18n::T("Diagnostico", "Diagnostics"));

  bool dev_mode = dbz3::settings::DevMode();
  if (ImGui::Checkbox(i18n::T("Activar modo Dev (overlay F10)",
                              "Enable Dev mode (F10 overlay)"), &dev_mode)) {
    dbz3::settings::SetDevMode(dev_mode);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Anade un overlay en juego (F10) con diagnostico y opciones de prueba.",
        "Adds an in-game overlay (F10) with diagnostics and test switches."));
  }

  bool show_fps = dbz3::settings::ShowFps();
  if (ImGui::Checkbox(i18n::T("Mostrar contador de FPS en juego (debug 60fps)",
                              "Show FPS counter in-game (60fps debug)"), &show_fps)) {
    dbz3::settings::SetShowFps(show_fps);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Muestra una ventana pequena con los FPS actuales mientras juegas. "
        "Util para verificar el limite de fotogramas / el modo 60fps.",
        "Displays a small corner window with the current FPS while playing. "
        "Useful to verify the frame cap / debug the 60fps mode."));
  }

  bool async_shaders = dbz3::settings::AsyncShaderCompilation();
  if (ImGui::Checkbox(i18n::T("Compilar shaders en segundo plano",
                              "Compile shaders asynchronously"), &async_shaders)) {
    dbz3::settings::SetAsyncShaderCompilation(async_shaders);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "En segundo plano (por defecto) los shaders se compilan en paralelo: "
        "carga mas rapida, pero puede dar un tiron la primera vez que aparece "
        "cada efecto. Desactivalo para compilarlos al momento (sin tirones, "
        "carga inicial mas lenta).",
        "Asynchronous (default) compiles shaders in parallel: faster loads, but "
        "may hitch the first time each effect appears. Turn it off to compile "
        "them on the spot (no hitching, slower initial load)."));
  }

  bool occ = dbz3::settings::OcclusionQueries();
  if (ImGui::Checkbox(i18n::T("Consultas de oclusion del juego",
                              "Game occlusion queries"), &occ)) {
    dbz3::settings::SetOcclusionQueries(occ);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Permite al juego descartar geometria oculta (por defecto). Si la GPU "
        "se queda corta, desactivarlas elimina esperas del host a cambio de "
        "dibujar de mas: util para diagnosticar.",
        "Lets the game discard hidden geometry (default). On slow GPUs, turning "
        "them off removes host waits at the cost of extra overdraw: useful for "
        "diagnosis."));
  }

  bool perf_logging = dbz3::settings::PerfLogging();
  if (ImGui::Checkbox(i18n::T("Registro de rendimiento (cada 5 s)",
                              "Performance log (every 5 s)"), &perf_logging)) {
    dbz3::settings::SetPerfLogging(perf_logging);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Escribe una linea cada 5 segundos con los FPS reales, el peor frame del "
        "intervalo y si la ventana tiene el foco. Es la forma de distinguir una "
        "bajada real de un alt-tab: Windows limita a la mitad la presentacion de "
        "una ventana visible sin foco.",
        "Writes one line every 5 seconds with the real FPS, the worst frame in "
        "the interval and whether the window has focus. The way to tell a real "
        "drop from an alt-tab: Windows halves the presentation of a visible "
        "unfocused window."));
  }

  bool io_logging = dbz3::settings::IoLogging();
  if (ImGui::Checkbox(i18n::T("Registro de E/S de disco (cada 5 s)",
                              "Disk I/O log (every 5 s)"), &io_logging)) {
    dbz3::settings::SetIoLogging(io_logging);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Escribe una linea cada 5 segundos con las lecturas de los AFS "
        "(cantidad, MB, latencia media/p95/p99, maximos y lecturas lentas) y una "
        "linea por cada lectura lenta. Es la forma de ver si un tiron viene del "
        "disco.",
        "Writes one line every 5 seconds with AFS reads (count, MB, average/"
        "p95/p99 latency, worst and slow reads) plus one line per slow read. "
        "The way to tell whether a hitch comes from the disk."));
  }

  bool readahead = dbz3::settings::IoReadahead();
  if (ImGui::Checkbox(i18n::T("Lectura anticipada de disco",
                              "Disk readahead"), &readahead)) {
    dbz3::settings::SetIoReadahead(readahead);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Lee bloques mas grandes de una vez y sirve las lecturas siguientes "
        "desde memoria: ayuda en discos mecanicos y en las cargas. Se desactiva "
        "solo mientras haya mods instalados.",
        "Reads bigger chunks at once and serves the following reads from RAM: "
        "helps on mechanical disks and during loads. Disabled automatically "
        "while mods are installed."));
  }

  bool texdump = dbz3::settings::TextureDumpEnabled();
  if (ImGui::Checkbox(i18n::T("Volcado de texturas para mods (dev)",
                              "Texture dump for mods (dev)"), &texdump)) {
    dbz3::settings::SetTextureDumpEnabled(texdump);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Escribe cada textura unica como DDS + index.jsonl para autorar packs de "
        "texturas (estilo PCSX2). Solo para desarrollo; requiere reiniciar y no "
        "tiene efecto con la mejora de texturas HD activada.",
        "Writes every unique texture as DDS + index.jsonl for authoring texture "
        "packs (PCSX2-style). Development only; requires a restart and has no "
        "effect while HD texture enhancement is on."));
  }

  // Ajuste avanzado de la mejora de texturas: cuanto se gasta en VRAM. Se
  // expone solo aqui (Dev) y en lenguaje no tecnico.
  if (dbz3::settings::HdTextures() > 1) {
    static const char* hd_texmins_items[] = {
        i18n::T("Bajo (maximo ahorro, texturas medianas y menores)",
                "Low (max savings, medium and smaller textures)"),
        i18n::T("Medio (equilibrado)", "Medium (balanced)"),
        i18n::T("Alto (mas texturas, mas VRAM)", "High (more textures, more VRAM)")};
    // 0.25 M / 0.5 M / 1 M texeles.
    static const int32_t hd_texmins_vals[] = {1 << 18, 1 << 19, 1 << 20};
    int mins_idx = 1;
    const int32_t cur = dbz3::settings::HdTextureMaxTexels();
    for (int i = 0; i < 3; i++) {
      if (cur == hd_texmins_vals[i]) mins_idx = i;
    }
    if (ImGui::Combo(i18n::T("Texturas HD: cuanto gastar (avanzado)",
                             "HD textures: spending (advanced)"),
                     &mins_idx, hd_texmins_items, 3)) {
      dbz3::settings::SetHdTextureMaxTexels(hd_texmins_vals[mins_idx]);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Cuantas texturas se mejoran segun su tamano. \"Bajo\" solo toca las "
          "pequenas y medianas (mucho menos consumo); \"Alto\" tambien las "
          "grandes de escenario (nota mas, gasta mas). Cambia la VRAM y el uso "
          "de GPU de la mejora de texturas.",
          "Which textures get enhanced based on size. \"Low\" only touches small "
          "and medium ones (much less cost); \"High\" also the big stage "
          "textures (more noticeable, more cost). Changes the VRAM and GPU usage "
          "of the texture enhancement."));
    }
  }

  bool diag = dbz3::settings::DiagLogging();
  if (ImGui::Checkbox(i18n::T("Registro de diagnostico GPU (logs + .bmp)",
                              "GPU diagnostic logging (logs + .bmp dumps)"), &diag)) {
    dbz3::settings::SetDiagLogging(diag);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Escribe diagnostico GPU por fotograma (logs, readbacks y dumps .bmp). "
        "Genera archivos grandes. Mantener apagado normalmente.",
        "Writes per-frame GPU diagnostics (logs, readbacks and .bmp dumps). "
        "Generates large files. Keep off normally."));
  }

  bool crashdump = dbz3::settings::CrashDumpEnabled();
  if (ImGui::Checkbox(i18n::T("Guardar minidump de crash (crash_*.dmp)",
                              "Write crash minidump (crash_*.dmp)"), &crashdump)) {
    dbz3::settings::SetCrashDumpEnabled(crashdump);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Escribe un minidump cuando el juego falla. Mantener apagado normalmente.",
      "Writes a minidump file when the game crashes. Keep off normally."));
  }

  bool upd_check = dbz3::settings::UpdateCheckEnabled();
  if (ImGui::Checkbox(i18n::T("Buscar actualizaciones al abrir",
                              "Check for updates on launch"), &upd_check)) {
    dbz3::settings::SetUpdateCheckEnabled(upd_check);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Consulta en GitHub cual es la ultima version publicada al abrir el "
        "launcher (solo lectura, menos de 1 KB). Desactivalo si no quieres "
        "ninguna conexion.",
        "Asks GitHub for the latest published release when the launcher opens "
        "(read-only, under 1 KB). Turn it off if you prefer no connections."));
  }

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Datos del usuario", "User data"));
  ImGui::TextWrapped(i18n::T("Guardado y cache: %s", "Saves and cache: %s"),
                     dbz3::settings::UserDataRoot().string().c_str());
  ImGui::TextWrapped("%s", dbz3::settings::UserDataIsPortable()
      ? i18n::T("Carpeta portable (junto al juego).",
                "Portable folder (next to the game).")
      : i18n::T("La carpeta del juego no es escribible: se usa la carpeta de usuario.",
                "The game folder is not writable: using the per-user folder."));

  ImGui::EndChild();
}

}  // namespace dbz3::launcher
