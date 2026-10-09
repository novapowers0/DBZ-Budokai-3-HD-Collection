// dbz3 - Pre-game launcher screen implementation.
// Dark modern style with Dragon Ball accent colors (orange/blue).

#include "launcher_state.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>

#include "settings.h"
#include "i18n.h"
#include "ui_kit.h"
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
#include <fstream>
#include <mutex>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dbz3::launcher {

namespace {

namespace ui = dbz3::launcher::ui;

// DBZ palette (defined in ui_kit.h; these names are the ones the tabs use).
constexpr ImVec4 kDragonOrange = ui::kAccent;
constexpr ImVec4 kDragonOrangeDim = ui::kAccentDim;
constexpr ImVec4 kPanelBg = ui::kBg;
constexpr ImVec4 kPanelBgAlt = ui::kCard;
constexpr ImVec4 kTextMain = ui::kText;
constexpr ImVec4 kTextDim = ui::kTextDim;

// Component colors, named so the same meaning always looks the same.
constexpr ImVec4 kBorder = ui::kLine;
constexpr ImVec4 kFrameBg = ui::kFrame;
constexpr ImVec4 kFrameBgHovered = ui::kFrameHover;
constexpr ImVec4 kFrameBgActive = ui::kFrameActive;
constexpr ImVec4 kRowBg = ui::kCardHeader;
constexpr ImVec4 kTabBg = ui::kCard;
constexpr ImVec4 kOk = ui::kOk;
constexpr ImVec4 kOkSoft(0.55f, 0.85f, 0.60f, 1.0f);
constexpr ImVec4 kWarn = ui::kWarn;
constexpr ImVec4 kError = ui::kError;
constexpr ImVec4 kInfo(0.55f, 0.78f, 0.98f, 1.0f);
constexpr ImVec4 kGold(1.00f, 0.72f, 0.30f, 1.0f);
constexpr ImVec4 kSrcActive(0.96f, 0.55f, 0.11f, 0.30f);
constexpr ImVec4 kPlayIdle(0.93f, 0.47f, 0.07f, 1.0f);
constexpr ImVec4 kPlayHovered(1.00f, 0.58f, 0.16f, 1.0f);
constexpr ImVec4 kPlayActive(0.80f, 0.38f, 0.04f, 1.0f);

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
  if (type == "personaje") return i18n::T("personaje", "character");
  if (type == "generado") return i18n::T("automatico", "automatic");
  return i18n::T("otro", "other");
}

void PushSectionHeader(const char* title) { ui::SectionTitle(title); }

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
    std::string label = dn;
    if (!chars[i].playable) {
      label += i18n::T("  [NO JUGABLE]", "  [NOT PLAYABLE]");
    }
    label += "##" + std::to_string(i);       // nombres repetidos (trajes): ID propio
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

// Friendly button name for a keybind suffix, following the selected glyph set
// ("xbox", "playstation" or "switch"). The runtime's button mapping is fixed,
// so this only changes the label shown next to each MnK keybind (LT vs L2 vs
// ZL, ...): pure cosmetics. ASCII only (the launcher uses the base font).
const char* ButtonGlyph(const std::string& set, const char* suffix) {
  const bool ps = set == "playstation";
  const bool sw = set == "switch";
  if (std::strcmp(suffix, "a") == 0) return "A";
  if (std::strcmp(suffix, "b") == 0) return "B";
  if (std::strcmp(suffix, "x") == 0) return "X";
  if (std::strcmp(suffix, "y") == 0) return "Y";
  if (std::strcmp(suffix, "left_trigger") == 0) return ps ? "L2" : (sw ? "ZL" : "LT");
  if (std::strcmp(suffix, "right_trigger") == 0) return ps ? "R2" : (sw ? "ZR" : "RT");
  if (std::strcmp(suffix, "left_shoulder") == 0) return ps ? "L1" : (sw ? "L" : "LB");
  if (std::strcmp(suffix, "right_shoulder") == 0) return ps ? "R1" : (sw ? "R" : "RB");
  if (std::strcmp(suffix, "lstick_up") == 0) return "LS-Up";
  if (std::strcmp(suffix, "lstick_down") == 0) return "LS-Down";
  if (std::strcmp(suffix, "lstick_left") == 0) return "LS-Left";
  if (std::strcmp(suffix, "lstick_right") == 0) return "LS-Right";
  if (std::strcmp(suffix, "lstick_press") == 0) return "L3";
  if (std::strcmp(suffix, "rstick_up") == 0) return "RS-Up";
  if (std::strcmp(suffix, "rstick_down") == 0) return "RS-Down";
  if (std::strcmp(suffix, "rstick_left") == 0) return "RS-Left";
  if (std::strcmp(suffix, "rstick_right") == 0) return "RS-Right";
  if (std::strcmp(suffix, "rstick_press") == 0) return "R3";
  if (std::strcmp(suffix, "dpad_up") == 0) return "D-Pad Up";
  if (std::strcmp(suffix, "dpad_down") == 0) return "D-Pad Down";
  if (std::strcmp(suffix, "dpad_left") == 0) return "D-Pad Left";
  if (std::strcmp(suffix, "dpad_right") == 0) return "D-Pad Right";
  if (std::strcmp(suffix, "back") == 0) return ps ? "Share" : (sw ? "Minus" : "Back");
  if (std::strcmp(suffix, "start") == 0) return ps ? "Options" : (sw ? "Plus" : "Start");
  if (std::strcmp(suffix, "guide") == 0) return ps ? "PS" : (sw ? "Home" : "Guide");
  return suffix;
}

// Height reserved at the bottom of every settings tab for the always-visible
// footer (config summary + Reset/Save + PLAY). Keeps the primary action on
// screen on every tab and removes the window-level scrollbar that pushed PLAY
// below the fold.
constexpr float kFooterHeight = 96.0f;

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
  style.WindowRounding = 10.0f;
  style.FrameRounding = 7.0f;
  style.GrabRounding = 7.0f;
  style.ChildRounding = 10.0f;
  style.TabRounding = 8.0f;
  style.PopupRounding = 10.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabMinSize = 14.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBarBorderSize = 0.0f;
  style.TabBarOverlineSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.WindowPadding = ImVec2(20, 14);
  style.FramePadding = ImVec2(9, 5);
  style.ItemSpacing = ImVec2(10, 7);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.CellPadding = ImVec2(8, 5);
  style.ScrollbarSize = 12.0f;
  style.SeparatorTextBorderSize = 1.0f;
  style.SeparatorTextPadding = ImVec2(0, 6);
  style.Colors[ImGuiCol_WindowBg] = kPanelBg;
  style.Colors[ImGuiCol_ChildBg] = kPanelBgAlt;
  style.Colors[ImGuiCol_PopupBg] = kPanelBgAlt;
  style.Colors[ImGuiCol_Text] = kTextMain;
  style.Colors[ImGuiCol_TextDisabled] = kTextDim;
  style.Colors[ImGuiCol_TextSelectedBg] =
      ImVec4(kDragonOrange.x, kDragonOrange.y, kDragonOrange.z, 0.35f);
  style.Colors[ImGuiCol_Border] = kBorder;
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  style.Colors[ImGuiCol_FrameBg] = kFrameBg;
  style.Colors[ImGuiCol_FrameBgHovered] = kFrameBgHovered;
  style.Colors[ImGuiCol_FrameBgActive] = kFrameBgActive;
  style.Colors[ImGuiCol_TitleBg] = kDragonOrangeDim;
  style.Colors[ImGuiCol_TitleBgActive] = kDragonOrangeDim;
  style.Colors[ImGuiCol_Button] = kFrameBg;
  style.Colors[ImGuiCol_ButtonHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_ButtonActive] = kDragonOrange;
  style.Colors[ImGuiCol_Header] = kRowBg;
  style.Colors[ImGuiCol_HeaderHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_HeaderActive] = kDragonOrangeDim;
  style.Colors[ImGuiCol_Tab] = kTabBg;
  style.Colors[ImGuiCol_TabHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_TabSelected] = kDragonOrange;
  style.Colors[ImGuiCol_TabSelectedOverline] = kDragonOrange;
  style.Colors[ImGuiCol_TabDimmed] = kTabBg;
  style.Colors[ImGuiCol_TabDimmedSelected] = kDragonOrangeDim;
  style.Colors[ImGuiCol_SliderGrab] = kDragonOrange;
  style.Colors[ImGuiCol_SliderGrabActive] = kDragonOrange;
  style.Colors[ImGuiCol_CheckMark] = kDragonOrange;
  style.Colors[ImGuiCol_Separator] = kBorder;
  style.Colors[ImGuiCol_SeparatorHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_SeparatorActive] = kDragonOrange;
  style.Colors[ImGuiCol_ScrollbarBg] = kPanelBg;
  style.Colors[ImGuiCol_ScrollbarGrab] = kFrameBgActive;
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_ScrollbarGrabActive] = kDragonOrange;
  style.Colors[ImGuiCol_ResizeGrip] = kFrameBg;
  style.Colors[ImGuiCol_ResizeGripHovered] = kDragonOrangeDim;
  style.Colors[ImGuiCol_ResizeGripActive] = kDragonOrange;
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
  // Over a running game (F4) the changes apply right away (v1.4.0: live options).
  if (in_game_) {
    dbz3::settings::ApplyRuntimeSettingsToSdk(true);
  }
}

void LauncherDialog::RefreshAssets() {
  const double now = ImGui::GetTime();
  if (assets_.valid && now < assets_.next_refresh) {
    return;
  }
  AssetProbe p;
  p.game_root = dbz3::EffectiveGameRoot();
  p.iso_mode = dbz3::settings::IsIsoMode();
  p.sel_region = dbz3::settings::ResolveRegion(p.game_root);
  p.root_ok = !p.game_root.empty() && std::filesystem::is_directory(p.game_root);
  p.region_ok = p.root_ok && std::filesystem::is_directory(p.game_root / p.sel_region);
  p.us_ok = p.root_ok && std::filesystem::is_directory(p.game_root / "us");
  const auto& boot = dbz3::settings::CurrentBootSource();
  p.xex_status = boot.status;
  p.redirect_default_xex = boot.redirect_default_xex;
  p.boot_note = boot.note;
  p.xex_present = !boot.xex.empty() && std::filesystem::is_regular_file(boot.xex);
  p.xex_blocked = p.xex_present && (p.xex_status == dbz3::settings::XexStatus::kDbz1 ||
                                    p.xex_status == dbz3::settings::XexStatus::kHdMenu);
  p.xex_ok = p.xex_present && !p.xex_blocked;
  p.assets_ready =
      (p.iso_mode ? p.xex_ok : (p.region_ok || p.us_ok) && p.xex_ok) && !p.xex_blocked;
  p.valid = true;
  p.next_refresh = now + 0.4;
  assets_ = p;
}

// Archivos soltados sobre la ventana (lo pide main.cpp desde el evento de la ventana, en otro
// hilo): se procesan en el siguiente OnDraw.
namespace {
std::mutex g_drop_mutex;
std::vector<std::filesystem::path> g_dropped;
}  // namespace
void QueueDroppedFile(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(g_drop_mutex);
  g_dropped.push_back(path);
}

// Registro de una herramienta (Python): resumen claro y el texto tecnico plegado (se puede
// copiar para pedir ayuda). Antes el registro crudo, con rutas y trazas, era lo primero.
static bool ToolOutputFailed(const std::string& out) {
  return out.find("Traceback") != std::string::npos || out.find("ERROR") != std::string::npos ||
         out.find("\n!! ") != std::string::npos || out.rfind("!! ", 0) == 0;
}

static void DrawToolLog(const std::string& out, char* buf, size_t cap, const char* id, float h) {
  if (out.empty()) return;
  if (ToolOutputFailed(out)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui::kWarn, "%s", i18n::T(
        "Algo ha fallado. Abre 'Detalles tecnicos' para ver el motivo (puedes copiarlo para pedir ayuda).",
        "Something went wrong. Open 'Technical details' to see why (you can copy it to ask for help)."));
    ImGui::PopTextWrapPos();
  }
  if (ImGui::CollapsingHeader((std::string(i18n::T("Detalles tecnicos", "Technical details")) + "##" + id).c_str())) {
    std::memcpy(buf, out.c_str(), std::min(out.size(), cap - 1));
    buf[std::min(out.size(), cap - 1)] = '\0';
    ImGui::InputTextMultiline((std::string("##") + id).c_str(), buf, cap, ImVec2(-1.0f, h),
                              ImGuiInputTextFlags_ReadOnly);
  }
}

// Confirmacion de acciones que pierden datos: true el frame en que se pulsa `yes`.
// Se abre con ImGui::OpenPopup(id) (mismo id, mismo nivel de la pila de IDs).
static bool ConfirmModal(const char* id, const char* text, const char* yes) {
  bool ok = false;
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(yes, ImVec2(220, 0))) {
      ok = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Cancelar", "Cancel"), ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return ok;
}

// Problema de un mod (mods.cpp) en lenguaje para usuarios no tecnicos.
static std::string ModProblemText(int kind, const std::string& d) {
  switch (kind) {
    case dbz3::ModInfo::kEmpty:
      return i18n::T("Esta vacio: no hace nada.", "It is empty: it does nothing.");
    case dbz3::ModInfo::kNested:
      return std::string(i18n::T("Esta metido dentro de otra carpeta ('", "It is inside an extra folder ('")) + d +
             i18n::T("'), asi el juego no lo ve. Pulsa 'Arreglar'.", "'), so the game can't see it. Press 'Fix'.");
    case dbz3::ModInfo::kMissingFile:
      return std::string(i18n::T("Le falta un archivo: ", "A file is missing: ")) + d;
    case dbz3::ModInfo::kBadTextureName:
      return d + i18n::T(" imagen(es) con un nombre que el juego no reconoce: se ignoran. Usa el nombre "
                         "original de la textura capturada.",
                         " image(s) with a name the game doesn't recognize: they are ignored. Keep the "
                         "original name of the captured texture.");
    case dbz3::ModInfo::kBadTextureSize:
      return std::string(i18n::T("Tamano no valido en ", "Invalid size in ")) + d +
             i18n::T(": tiene que medir 1, 2, 3 o 4 veces el original (el tamano esta en el nombre).",
                     ": it must be 1, 2, 3 or 4 times the original size (the size is in the name).");
    case dbz3::ModInfo::kBadToml:
      return std::string(i18n::T("Error en el archivo de configuracion: ", "Error in the settings file: ")) + d;
  }
  return d;
}

// Arrastrar y soltar (lo mas pedido para instalar mods sin buscar carpetas): un .zip se instala
// como mod; un .png/.dds con nombre de textura de pack va a "Mi pack de texturas".
void LauncherDialog::HandleDroppedFiles() {
  std::vector<std::filesystem::path> files;
  {
    std::lock_guard<std::mutex> lock(g_drop_mutex);
    files.swap(g_dropped);
  }
  for (const auto& f : files) {
    std::string ext = f.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::string name;
    std::string err;
    if (ext == ".zip") {
      if (dbz3::InstallModFromZip(rex::path_to_utf8(f), name, err)) {
        mods_status_ = std::string(i18n::T("Mod instalado: ", "Mod installed: ")) + name;
      } else {
        mods_status_ = std::string(i18n::T("Error al instalar el mod: ", "Error installing mod: ")) + err;
      }
    } else if ((ext == ".png" || ext == ".dds") &&
               dbz3::settings::IsTexturePackFileName(f.stem().string())) {
      const auto pack = dbz3::ModsRoot() / "Mi pack de texturas";
      std::error_code ec;
      std::filesystem::create_directories(pack, ec);
      std::filesystem::copy_file(f, pack / f.filename(), std::filesystem::copy_options::overwrite_existing, ec);
      mods_status_ = ec ? std::string(i18n::T("No se pudo copiar: ", "Could not copy: ")) + f.filename().string()
                        : std::string(i18n::T("Textura anadida a 'Mi pack de texturas': ",
                                              "Texture added to 'My texture pack': ")) + f.filename().string();
    } else {
      mods_status_ = std::string(i18n::T(
          "Suelta un mod (.zip) o una textura editada (.png con su nombre original): ",
          "Drop a mod (.zip) or an edited texture (.png with its original name): ")) + f.filename().string();
    }
    mods_loaded_ = false;
    request_tab_ = 1;   // muestra el resultado en la pestana Mods
  }
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  // The launcher UI language follows the game's selected text language. Keep it
  // updated every frame so changing the "Language" combo re-translates the whole
  // launcher immediately.
  i18n::SetLanguage(dbz3::settings::Language());
  PollController(io);
  HandleDroppedFiles();
  // Ctrl+Tab / Ctrl+Shift+Tab switch tabs from the keyboard.
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    tab_request_ = (tab_index_ + (io.KeyShift ? 9 : 1)) % 10;
  }

  // One-shot repair (--dbz3_repair=true / REX_DBZ3_REPAIR=1): run once, show the
  // report and clear the flag so it never triggers again. The footer button runs
  // RepairInstallation directly.
  if (!repair_checked_) {
    repair_checked_ = true;
    if (rex::cvar::Query<bool>("dbz3_repair")) {
      rex::cvar::SetFlagByName("dbz3_repair", "false");
      repair_report_ = dbz3::settings::RepairInstallation();
      repair_popup_ = true;
    }
  }

  // Fill the entire host window. The host window is always 1280x720 windowed
  // during the launcher (fullscreen/resolution are applied on Play), so the
  // launcher is a single full window with no "window inside window" effect.
  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  const ui::Fonts& fonts = ui::GetFonts();
  ui::FontScope body_font(fonts.body);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

  // Header: the game's name on the left, version and updates on the right, over
  // a thin accent gradient so the window reads as a game launcher at a glance.
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float ww = ImGui::GetWindowWidth();
    dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ww, wp.y + 86.0f),
                                ImGui::GetColorU32(ImVec4(0.96f, 0.55f, 0.11f, 0.20f)),
                                ImGui::GetColorU32(ImVec4(0.27f, 0.56f, 0.98f, 0.10f)),
                                ImGui::GetColorU32(ImVec4(0.27f, 0.56f, 0.98f, 0.0f)),
                                ImGui::GetColorU32(ImVec4(0.96f, 0.55f, 0.11f, 0.0f)));
    dl->AddRectFilled(ImVec2(wp.x, wp.y + 86.0f), ImVec2(wp.x + ww, wp.y + 88.0f),
                      ImGui::GetColorU32(kDragonOrange));
  }
  ImGui::BeginGroup();
  {
    ui::FontScope f(fonts.bold);
    ImGui::TextColored(kDragonOrange, "%s", "DRAGON BALL Z");
  }
  {
    ui::FontScope f(fonts.title);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f);
    ImGui::TextUnformatted("BUDOKAI 3");
  }
  ImGui::SameLine(0.0f, 14.0f);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
  ui::Pill(nullptr, "HD Collection  -  ReXGlue", ui::kBlue);
  {
    const std::string cur = dbz3::launcher::CurrentVersionLabel();
    if (!cur.empty()) {
      ImGui::SameLine(0.0f, 8.0f);
      ui::Pill(nullptr, ("v" + cur).c_str(), kTextDim);
    }
  }
  ImGui::EndGroup();
  // Right side of the header: update state. Placed with an absolute position
  // (not SameLine: that would inherit the tall title's text baseline).
  ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 380.0f, 38.0f));
  ImGui::BeginGroup();
  if (fonts.sm) ImGui::PushFont(fonts.sm);

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
    (void)cur_label;  // shown as a pill next to the title
    switch (dbz3::launcher::GetUpdateState()) {
    case dbz3::launcher::UpdateState::kChecking:
      ImGui::TextDisabled("%s", i18n::T("Buscando actualizaciones...",
                                        "Checking for updates..."));
      break;
    case dbz3::launcher::UpdateState::kAvailable: {
      ImGui::PushStyleColor(ImGuiCol_Text, kOk);
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
  if (fonts.sm) ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::SetCursorPosY(98.0f);

  // --- Mixed installs (exe vs runtime DLLs) ---------------------------------
  // Updating by copying only some of the files over an old folder leaves a build
  // that cannot be identified from its log: one of the reports that motivated
  // this came from a folder labelled v1.2.1 while its DLLs were already newer.
  // The versions of every component are in the log (`dbz3: entorno ...`) and in
  // the Dev tab; here the user is simply told what to do about it.
  {
    const std::string mismatch = dbz3::launcher::InstalledVersionMismatch();
    if (!mismatch.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kDragonOrange);
      ImGui::TextWrapped(
          i18n::T("Aviso: los archivos instalados no son de la misma version (%s no coincide "
                  "con dbz3.exe o no se puede identificar).",
                  "Warning: the installed files are not the same version (%s does not match "
                  "dbz3.exe or cannot be identified)."),
          mismatch.c_str());
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s", i18n::T("Descomprime el zip completo en una carpeta nueva: mezclar el exe de "
                          "una version con las DLLs de otra produce fallos que no se pueden "
                          "reproducir.",
                          "Unzip the full package into a new folder: mixing the exe of one "
                          "version with the DLLs of another causes failures that cannot be "
                          "reproduced."));
      }
    }
  }


  // --- Game data validation banner (P1) -------------------------------------
  // Detects a missing/misplaced asset source BEFORE the user hits Play (which
  // would otherwise end in an "Entrypoint XEX not found" crash) and offers a
  // folder/ISO picker that relocates the game data in-place (no restart needed).
  // Two sources are supported: an extracted folder (us//eu/ + default.xex) and
  // a disc image (.iso) whose game drive is mounted at Play time.
  RefreshAssets();
  const auto& game_root = assets_.game_root;
  const bool iso_mode = assets_.iso_mode;
  const std::string& sel_region = assets_.sel_region;
  const bool root_ok = assets_.root_ok;
  // Auto-correct the region selection when the chosen source is missing but the
  // other one exists (e.g. EU-only data with the default "us"): keeps the
  // banner, the footer summary and the region mount at Play time in sync with
  // what is actually present. In ISO mode the region comes from the disc's own
  // default.xex (extracted to the ISO cache folder at startup).
  const auto xex_status = assets_.xex_status;
  if (iso_mode && (xex_status == dbz3::settings::XexStatus::kUs ||
                   xex_status == dbz3::settings::XexStatus::kEu)) {
    const std::string iso_region = xex_status == dbz3::settings::XexStatus::kEu ? "eu" : "us";
    if (sel_region != iso_region) {
      dbz3::settings::SetRegion(iso_region);
      assets_.sel_region = iso_region;
    }
  } else if (sel_region != dbz3::settings::Region()) {
    dbz3::settings::SetRegion(sel_region);
  }
  const bool region_ok = assets_.region_ok;
  const bool us_ok = assets_.us_ok;
  const bool xex_present = assets_.xex_present;
  const auto xex_status_final = assets_.xex_status;
  const bool xex_blocked = assets_.xex_blocked;
  const bool xex_ok = assets_.xex_ok;
  const bool assets_ready = assets_.assets_ready;

  if (assets_ready) {
    ui::Pill(ICON_OK, iso_mode ? i18n::T("Disco listo", "Disc ready")
                               : i18n::T("Datos del juego listos", "Game data ready"),
             kOk);
    ImGui::SameLine(0.0f, 10.0f);
    {
      ui::FontScope f(fonts.sm);
      ImGui::AlignTextToFramePadding();
      if (iso_mode) {
        const std::filesystem::path iso = dbz3::settings::IsoPath();
        ImGui::TextColored(kTextDim, "%s", iso.filename().string().c_str());
      } else {
        ImGui::TextColored(kTextDim, "%s", game_root.string().c_str());
      }
      if (sel_region != "us") {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s %s)", i18n::T("region", "region"), sel_region.c_str());
      }
    }
    if (iso_mode) {
      // ISO mode reads the game data straight off the disc; the per-entry AFS
      // override hooks are wired to host files, so mods need the extracted
      // folder. Inform the user instead of silently running without them.
      ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
      ImGui::TextWrapped(
          i18n::T("Modo disco: se juega tal cual del ISO. Los mods requieren la "
                  "carpeta extraida (elige 'Carpeta extraida' como origen).",
                  "Disc mode: plays straight from the ISO. Mods need the "
                  "extracted folder (choose 'Extracted folder' as the source)."));
      ImGui::PopStyleColor();
    }
    if (assets_.redirect_default_xex && !assets_.boot_note.empty()) {
      // The executable was found somewhere other than `<data>/default.xex` (e.g.
      // DBZ3/yae3_xenon.xex on a retail disc dump) and staged automatically.
      ImGui::PushStyleColor(ImGuiCol_Text, kInfo);
      ImGui::TextWrapped(
          i18n::T("Ejecutable detectado: %s (no hay que renombrar nada)",
                  "Executable detected: %s (nothing to rename)"),
          assets_.boot_note.c_str());
      ImGui::PopStyleColor();
    }
    if (xex_status == dbz3::settings::XexStatus::kUnknown) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
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
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
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
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
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
  if (assets_ready) {
    // Everything is fine: the source switch sits on the same row, at the right.
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 20.0f - 190.0f - 150.0f - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f);
  }
  const bool src_folder = !iso_mode;
  if (src_folder) {
    ImGui::PushStyleColor(ImGuiCol_Button, kSrcActive);
  }
  if (ui::IconButton(ICON_FOLDER, i18n::T("Carpeta extraida", "Extracted folder"), ImVec2(190, 0))) {
    std::string picked;
    if (PickFolder(picked, game_root.string())) {
      if (dbz3::settings::IsValidGameDataDir(picked)) {
        dbz3::settings::SetGameDirOverride(picked);
        // Picking an extracted folder leaves ISO mode: the folder wins.
        dbz3::settings::SetIsoPath("");
        dbz3::settings::SaveUserSettings();
        if (dbz3::RelocateGameData(picked)) {
          banner_error_.clear();
          InvalidateAssets();
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
  if (ui::IconButton(ICON_DISC, i18n::T("Imagen ISO", "ISO image"), ImVec2(150, 0))) {
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
        InvalidateAssets();
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
  if (!assets_ready) {
    ImGui::SameLine();
    ui::FontScope f(fonts.sm);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(
        "%s", i18n::T("Origen de los datos del juego: una carpeta ya extraida o el ISO del disco.",
                      "Where the game data comes from: an already-extracted folder or the disc ISO."));
  }
  ImGui::Spacing();

  // Settings-file health notice. An older build could leave dbz3_user.toml with
  // an unescaped Windows path, which made the WHOLE file fail to parse and
  // silently threw away every option. Tell the user when that happened (and when
  // the file was repaired) instead of letting their settings vanish in silence.
  const auto config_state = dbz3::settings::LastConfigLoadState();
  if (config_state == dbz3::settings::ConfigLoadState::kRepaired) {
    ImGui::TextColored(kOkSoft, "%s",
                       i18n::T("Tus ajustes se han recuperado (el archivo de "
                               "configuracion estaba danado y se ha reparado).",
                               "Your settings were recovered (the config file was "
                               "damaged and has been repaired)."));
  } else if (config_state == dbz3::settings::ConfigLoadState::kInvalid) {
    ImGui::TextColored(kError, "%s",
                       i18n::T("No se pudo leer tu archivo de ajustes (dbz3_user.toml): "
                               "tiene un error de formato. Se ha guardado una copia en "
                               "dbz3_user.toml.bak y se usan los valores por defecto. "
                               "Corrige o borra el archivo para empezar limpio.",
                               "Your settings file (dbz3_user.toml) could not be read: it "
                               "has a format error. A copy was saved as dbz3_user.toml.bak "
                               "and the default values are in use. Fix or delete the file "
                               "to start clean."));
  }

  // Tab bar: icon + name, bigger targets, semibold.
  auto tab_label = [](const char* icon, const char* name, const char* id) {
    static std::string buf[12];
    static int slot = 0;
    std::string& out = buf[slot++ % 12];
    out = std::string(icon) + "  " + name + "###" + id;
    return out.c_str();
  };
  // Tab items are laid out and drawn as each BeginTabItem runs, so their style
  // is pushed around that call only (the tab contents keep the normal style).
  int tab_counter = 0;
  const int tab_request = tab_request_;
  tab_request_ = -1;
  auto begin_tab = [&](const char* label, ImGuiTabItemFlags flags = 0) {
    const int index = tab_counter++;
    if (index == tab_request) flags |= ImGuiTabItemFlags_SetSelected;
    if (fonts.bold) ImGui::PushFont(fonts.bold);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 8));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, kFrameBgHovered);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ui::kAccentSoft);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, kDragonOrange);
    const bool open = ImGui::BeginTabItem(label, nullptr, flags);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    if (fonts.bold) ImGui::PopFont();
    if (open) {
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      tab_index_ = index;
    }
    return open;
  };
  ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 3.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 1.0f);
  const bool tabs_open = ImGui::BeginTabBar("##launcher_tabs");
  ImGui::PopStyleVar(2);
  if (tabs_open) {
    if (begin_tab(tab_label(ICON_VIDEO, i18n::T("Video", "Video"), "tab_video"))) {
      DrawVideoTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_UPSCALE, i18n::T("Escalado", "Upscaling"), "tab_upscale"))) {
      DrawUpscaleTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_AUDIO, i18n::T("Audio", "Audio"), "tab_audio"))) {
      DrawAudioTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_INPUT, i18n::T("Controles", "Input"), "tab_input"))) {
      DrawInputTab();
      ImGui::EndTabItem();
    }
    const int want_tab = request_tab_;
    request_tab_ = 0;
    if (begin_tab(tab_label(ICON_MODS, i18n::T("Mods", "Mods"), "tab_mods"),
                            want_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_NATIVE, i18n::T("Mods nativos", "Native mods"), "tab_native"))) {
      DrawNativeModsTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_SWAP, i18n::T("Cambio de modelo", "Model Swap"), "tab_swap"))) {
      DrawModelSwapTab();
      ImGui::EndTabItem();
    }

    if (begin_tab(tab_label(ICON_TEXTURES, i18n::T("Texturas", "Textures"), "tab_tex"))) {
      DrawTexturesTab();
      ImGui::EndTabItem();
    }
    if (begin_tab(tab_label(ICON_CHARS, i18n::T("Personajes nuevos", "New characters"), "tab_chars"),
                            want_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawNewCharactersTab();
      ImGui::EndTabItem();
    }
    // la ultima pestana: diagnostico para quien lo necesite (el modo Dev se activa aqui)
    if (begin_tab(tab_label(ICON_DEV, i18n::T("Avanzado", "Advanced"), "tab_dev"))) {
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
  // Footer band (slightly lighter than the window, separated by a hairline).
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    const float top = ImGui::GetCursorScreenPos().y - 6.0f;
    dl->AddRectFilled(ImVec2(wp.x, top), ImVec2(wp.x + ws.x, wp.y + ws.y), ImGui::GetColorU32(ui::kCard));
    dl->AddLine(ImVec2(wp.x, top), ImVec2(wp.x + ws.x, top), ImGui::GetColorU32(kBorder));
  }
  if (fonts.sm) ImGui::PushFont(fonts.sm);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, i18n::T("Se jugara con:  %s  |  %s  |  %dx  |  %s  |  %s",
                             "Launching with:  %s  |  %s  |  %dx  |  %s  |  %s"),
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
  if (fonts.sm) ImGui::PopFont();
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
  ImGui::PushStyleColor(ImGuiCol_Button, kFrameBg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDragonOrangeDim);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDragonOrange);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  if (ui::IconButton(ICON_RESET, i18n::T("Restablecer", "Reset defaults"), ImVec2(170, 38))) {
    ImGui::OpenPopup("###confirm_reset");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T("Vuelve a los ajustes de fabrica (pide confirmacion).",
                                    "Back to factory settings (asks first)."));
  }
  if (ConfirmModal((std::string(i18n::T("Restablecer", "Reset")) + "###confirm_reset").c_str(),
                   i18n::T("Restablecer todos los ajustes? Se perderan tus teclas y se desactivaran "
                           "todos los mods (no se borra ninguno).",
                           "Reset all settings? Your keys will be lost and all mods will be turned off "
                           "(none is deleted)."),
                   i18n::T("Si, restablecer", "Yes, reset"))) {
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
    InvalidateAssets();
  }
  ImGui::SameLine(0, 10);
  if (ui::IconButton(ICON_SAVE, i18n::T("Guardar ajustes", "Save settings"), ImVec2(180, 38))) {
    dbz3::settings::SaveUserSettings();
  }
  ImGui::SameLine(0, 10);
  if (ui::IconButton(ICON_REPAIR, i18n::T("Reparar instalacion", "Repair install"), ImVec2(200, 38))) {
    repair_report_ = dbz3::settings::RepairInstallation();
    repair_popup_ = true;
    InvalidateAssets();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Comprueba y repara los archivos del juego (ajustes, DLLs, datos de "
        "usuario). Util si algo no arranca o los ajustes no se guardan.",
        "Checks and repairs the game files (settings, DLLs, user data). Useful "
        "if something does not start or settings are not saved."));
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  DrawInputHints();
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 20.0f - 320.0f);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 30.0f);
  // PLAY is gated on the assets being found: pressing it with no game data
  // would crash before the guest even starts (P1). The banner above offers the
  // folder picker to fix it.
  ImGui::BeginDisabled(!assets_ready || launching_);
  ImGui::PushStyleColor(ImGuiCol_Button, kPlayIdle);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPlayHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPlayActive);
  if (fonts.h2) ImGui::PushFont(fonts.h2);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
  const std::string play_label = std::string(ICON_PLAY) + "   " +
      (launching_ ? i18n::T("Preparando...", "Preparing...") : i18n::T("JUGAR", "PLAY")) + "###play";
  const bool play_pressed = ImGui::Button(play_label.c_str(), ImVec2(320, 62));
  const ImVec2 play_min = ImGui::GetItemRectMin();
  ImGui::PopStyleVar();
  if (fonts.h2) ImGui::PopFont();
  if (broken_mods_ < 0) {
    broken_mods_ = 0;
    active_mods_ = 0;
    for (const dbz3::ModInfo& m : dbz3::ListMods()) {
      if (m.enabled) ++active_mods_;
      if (m.enabled && !m.problems.empty()) ++broken_mods_;
    }
  }
  if (active_mods_ > 0 && dbz3::settings::IsIsoMode()) {
    // Modo disco: los mods solo se sirven desde la carpeta extraida. Aviso junto a JUGAR (el
    // de la seccion de origen pasaba desapercibido: "dice instalado pero no hace efecto").
    char warn[192];
    std::snprintf(warn, sizeof(warn),
                  i18n::T("! Juegas desde la ISO: tus %d mod(s) activos NO se cargan (usa la carpeta extraida)",
                          "! Playing from the ISO: your %d active mod(s) will NOT load (use the extracted folder)"),
                  active_mods_);
    ImGui::GetWindowDrawList()->AddText(ImVec2(play_min.x, play_min.y - ImGui::GetTextLineHeight() - 4.0f),
                                        ImGui::GetColorU32(kGold), warn);
  } else if (broken_mods_ > 0) {
    // Encima del boton, sin mover el layout (debajo se cortaba y salia una barra de scroll).
    char warn[128];
    std::snprintf(warn, sizeof(warn), i18n::T("! %d mod(s) con problemas: pulsa para verlos###broken",
                                             "! %d mod(s) with problems: click to see them###broken"), broken_mods_);
    const ImVec2 keep = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos(ImVec2(play_min.x, play_min.y - ImGui::GetTextLineHeight() - 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    if (ImGui::Selectable(warn, false, 0, ImGui::CalcTextSize(warn, nullptr, true))) request_tab_ = 1;
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(keep);
  }
  const bool pad_play = pad_play_;
  pad_play_ = false;
  if (!assets_ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", i18n::T("Faltan los datos del juego: mira el aviso de arriba.",
                                    "Game data missing: see the notice above."));
  }
  if (launching_) {
    ImGui::GetWindowDrawList()->AddText(ImVec2(play_min.x, play_min.y - ImGui::GetTextLineHeight() - 4.0f),
                                        ImGui::GetColorU32(kGold),
                                        i18n::T("Preparando los personajes nuevos, un momento...",
                                                "Preparing the new characters, one moment..."));
  }
  bool launch_now = false;
  // Enter juega, salvo mientras se escribe en un campo de texto (buscador, nombre...).
  const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput &&
                     !ImGui::IsAnyItemActive();
  if (!launching_ && (play_pressed || (assets_ready && (pad_play || enter)))) {
    dbz3::settings::SaveUserSettings();
    dbz3::settings::ApplyUserSettingsToSdk();
    dbz3::settings::ApplyRuntimeSettingsToSdk(true);
    dbz3::settings::ApplyWindowSizeToSdk();
    REXLOG_INFO("dbz3: launcher Play pressed, starting game");
    // Personajes nuevos: regenerar el mod combinado "_roster" si cambio algo
    // (instantaneo si esta al dia; los mods no se aplican en modo ISO).
    if (!dbz3::settings::IsIsoMode() && ModPipeline::HasCharacterSources()) {
      if (ModPipeline::RosterToolAvailable()) {
        // en segundo plano: la ventana sigue viva y dice que esta montando (antes se congelaba)
        mod_pipeline_.Wait();
        mod_pipeline_.BuildRoster();
        launching_ = true;
      } else {
        // v1.4.1: el pack de personajes trae el _roster ya montado y funciona sin el kit
        // de modding; antes se lanzaba igualmente roster_build.py y cada partida dejaba
        // "can't open file ... roster_build.py" (exit 2) en el log.
        REXLOG_INFO("dbz3: personajes nuevos: se usa el _roster ya montado (sin el kit de "
                    "modding no se reconstruye; para cambiar personajes instala el kit)");
      }
    }
    if (!launching_) launch_now = true;
  }
  if (launching_ && !mod_pipeline_.IsRunning()) {
    mod_pipeline_.Wait();
    REXLOG_INFO("dbz3: personajes nuevos: {}", mod_pipeline_.Output());
    launching_ = false;
    launch_now = true;
  }
  if (launch_now) {
    Close();
    if (on_play_) {
      on_play_();
    }
  }
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();

  // Repair report (modal). Opened by the footer button or the one-shot cvar.
  if (repair_popup_) {
    ImGui::OpenPopup("repair_result");
    repair_popup_ = false;
  }
  if (ImGui::BeginPopupModal("repair_result", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(kDragonOrange, "%s",
                       i18n::T("Resultado de la reparacion", "Repair result"));
    ImGui::Separator();
    ImGui::BeginChild("##repair_body", ImVec2(620, 300), true);
    ImGui::TextUnformatted(repair_report_.c_str());
    ImGui::EndChild();
    ImGui::Spacing();
    if (ImGui::Button(i18n::T("Cerrar", "Close"), ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}

void LauncherDialog::DrawVideoTab() {
  // Two side-by-side columns so every control fits in the fixed 1280x720
  // launcher window without scrollbars (compact one-screen layout).
  const float avail_x = ImGui::GetContentRegionAvail().x;
  const float col_w = (avail_x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

  ImGui::BeginChild("##video_left", ImVec2(col_w, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  {
    // Focus behaviour, first thing in the first tab: it is what a non-technical
    // user looks for ("what happens if I switch to Discord?"), and it is safe by
    // design -- it silences and dims, it never freezes the game.
    PushSectionHeader(i18n::T("Al salir de la ventana", "When you leave the window"));
    bool mute_unfocused = dbz3::settings::MuteUnfocused();
    if (ImGui::Checkbox(i18n::T("Silenciar el audio", "Mute audio"), &mute_unfocused)) {
      dbz3::settings::SetMuteUnfocused(mute_unfocused);
    }
    ui::Tip(i18n::T(
          "Si pasas a otra ventana, el sonido se corta solo y vuelve al regresar. "
          "El juego sigue en marcha.",
          "If you switch to another window, the sound cuts out and comes back when "
          "you return. The game keeps running."));
    bool dim_unfocused = dbz3::settings::DimUnfocused();
    if (ImGui::Checkbox(i18n::T("Oscurecer la pantalla", "Dim the screen"),
                        &dim_unfocused)) {
      dbz3::settings::SetDimUnfocused(dim_unfocused);
    }
    ui::Tip(i18n::T(
          "Mientras el juego esta detras, la imagen se oscurece y avisa de que "
          "sigue en marcha: se ve de un vistazo desde la barra de tareas.",
          "While the game is behind, the picture darkens and says it is still "
          "running: obvious at a glance from the taskbar."));
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
    ui::RowLabel(i18n::T("Modo de calidad", "Quality mode"));
    if (ImGui::Combo("##DrawVideoTab_1", &preset_idx, preset_items, 5)) {
      dbz3::settings::SetQualityPreset(preset_vals[preset_idx]);
      // Apply immediately: named presets persist their values, "auto" detects
      // the GPU now, "manual" leaves the individual controls untouched.
      dbz3::settings::ApplyQualityPreset();
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(i18n::T(
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
    ui::RowLabel(i18n::T("Escala de render interna", "Internal render scale"));
    if (ImGui::Combo("##DrawVideoTab_2", &scale_idx,
                     scale_items, 4)) {
      dbz3::settings::SetResolutionScale(scale_idx + 1);
      // Persist immediately: the user often marks the scale and then launches (or
      // closes) without pressing "Save settings". Saving here guarantees the chosen
      // internal resolution is always applied on the next boot.
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(i18n::T(
          "Supersampling del framebuffer de 720p: el juego renderiza de verdad a "
          "mayor resolucion y luego se reduce. Reduce muy bien el aliasing, pero "
          "multiplica el consumo de GPU y potencia (cada paso hacia arriba cuesta "
          "aprox. el doble). Empieza por 1x. Requiere reinicio.",
          "Supersampling of the 720p framebuffer: the game really renders at a "
          "higher resolution and is then downscaled. It greatly reduces aliasing, "
          "but multiplies GPU load and power draw (each step up costs roughly "
          "twice as much). Start at 1x. Restart required."));
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
      ui::Tip(i18n::T(
            "Deja la escala interna en 1x (render nativo 720p). Es como mejor "
            "rinde y la calidad ya es alta gracias al escalado FSR/CAS.",
            "Sets the internal scale back to 1x (native 720p render). It performs "
            "best this way and quality is already high thanks to FSR/CAS."));
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
    ui::RowLabel(i18n::T("Mejora de texturas (experimental)",
                             "Texture enhancement (experimental)"));
    if (ImGui::Combo("##DrawVideoTab_3",
                     &hdtex_idx, hdtex_items, 3)) {
      dbz3::settings::SetHdTextures(hdtex_idx + 1);
      // Persist immediately (same reason as the render scale: the user may just
      // launch or close without pressing "Save settings").
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(i18n::T(
          "Aumenta la nitidez de las texturas del juego (caras, ropa, escenarios). "
          "Funciona con la GPU: sube bastante el consumo y el uso de VRAM. Empieza "
          "por \"Nitidas\"; si notas ralentizaciones, vuelve a Desactivado. "
          "Requiere reiniciar el juego.",
          "Sharpens the game's textures (faces, clothes, stages). It runs on the "
          "GPU: noticeably raises power draw and VRAM usage. Start with \"Sharp\"; "
          "if you notice slowdowns, switch back to Off. Requires restarting the "
          "game."));
    if (dbz3::settings::HdTextures() > 1) {
      ImGui::TextColored(kDragonOrangeDim, "%s",
                         i18n::T("Experimental: mayor consumo de GPU y VRAM",
                                 "Experimental: higher GPU and VRAM usage"));
      // La combinacion con el supersampling es la que produce el "va a 30": con
      // la escala interna por encima de 1x el framebuffer ya es enorme y, si el
      // frame deja de llegar al intervalo de presentacion, el vsync cae a media
      // tasa (60 -> 30 exactos). Se dice aqui, junto a la opcion, y no solo en el
      // log: es la causa mas habitual de un reporte de "va lento con texturas HD".
      if (dbz3::settings::ResolutionScale() > 1) {
        ImGui::TextColored(kDragonOrangeDim, "%s",
                           i18n::T("Aviso: escala interna + mejora de texturas multiplican el "
                                   "coste. Si notas tirones o el juego baja a 30, deja la escala "
                                   "en 1x (abajo) o desactiva la mejora.",
                                   "Note: internal scale + texture enhancement multiply the "
                                   "cost. If you notice stutters or the game drops to 30, set "
                                   "the scale back to 1x (below) or turn the enhancement off."));
      }
    }

    bool msaa = dbz3::settings::Native2xMsaa();
    if (ImGui::Checkbox(i18n::T("MSAA 2x nativo", "Native 2x MSAA"), &msaa)) {
      dbz3::settings::SetNative2xMsaa(msaa);
    }
    ui::Tip(i18n::T(
          "Suaviza los bordes (menos dientes de sierra) con un coste moderado de "
          "GPU. Se nota sobre todo con la escala interna en 1x; si subes la escala, "
          "puedes quitarlo sin perder mucha calidad.",
          "Smooths edges (less aliasing) at a moderate GPU cost. It helps most with "
          "the internal scale at 1x; if you raise the scale, you can turn it off "
          "without losing much quality."));

    static const char* aniso_items[] = {"Off", "1x", "2x", "4x", "8x", "16x"};
    static const int aniso_values[] = {0, 1, 2, 3, 4, 5};
    int aniso = dbz3::settings::AnisotropicOverride();
    int aniso_idx = 0;
    for (int i = 0; i < 6; i++) {
      if (aniso == aniso_values[i]) aniso_idx = i;
    }
    ui::RowLabel(i18n::T("Filtrado anisotropico", "Anisotropic filtering"));
    if (ImGui::Combo("##DrawVideoTab_4", &aniso_idx,
                     aniso_items, 6)) {
      dbz3::settings::SetAnisotropicOverride(aniso_values[aniso_idx]);
    }

  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##video_right", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  {
    PushSectionHeader(i18n::T("Pantalla", "Display"));

    const char* modes[] = {i18n::T("Ventana", "Windowed"),
                                  i18n::T("Sin bordes", "Borderless"),
                                  i18n::T("Pantalla completa exclusiva", "Exclusive Fullscreen")};
    int mode_idx = 0;
    std::string mode = dbz3::settings::FullscreenMode();
    if (mode == "borderless") mode_idx = 1;
    else if (mode == "exclusive") mode_idx = 2;
    ui::RowLabel(i18n::T("Modo de pantalla", "Fullscreen mode"));
    if (ImGui::Combo("##DrawVideoTab_6", &mode_idx, modes, 3)) {
      dbz3::settings::SetFullscreenMode(mode_idx == 0 ? "windowed" : (mode_idx == 1 ? "borderless" : "exclusive"));
    }

    ImGui::TextDisabled(i18n::T("Velocidad del juego: fija a 60 FPS (sincronizada)",
                                "Game speed: fixed 60 FPS (synchronized)"));
    ui::Tip(i18n::T(
          "El ritmo del juego se sincroniza a 60 Hz (vblank del guest). "
          "No es configurable: sin esta sincronizacion el juego corre acelerado.",
          "The game logic is synchronized to 60 Hz (guest vblank). "
          "Not configurable: without it the game runs too fast."));

    int cap = dbz3::settings::FrameCap();
    ui::RowLabel(i18n::T("Limite de fotogramas (FPS)", "Frame cap (FPS)"));
    if (ImGui::SliderInt("##DrawVideoTab_7", &cap, 0, 240,
                         cap == 0 ? i18n::T("Sin limite", "Uncapped") : "%d FPS")) {
      dbz3::settings::SetFrameCap(dbz3::settings::SafeFrameCap(cap));
    }
    ui::Tip(i18n::T(
          "Limita la velocidad de presentacion en pantalla, NO la velocidad "
          "del juego (esa es siempre 60). 60 = fluido; 30 = menos carga en "
          "GPUs integradas; 0 = sin limite.",
          "Limits the on-screen present rate, NOT the game speed (always 60). "
          "60 = smooth; 30 = less load on integrated GPUs; 0 = uncapped."));

    bool vrr = dbz3::settings::VrrEnabled();
    if (ImGui::Checkbox(i18n::T("Frecuencia variable (G-Sync/FreeSync)",
                                "Variable refresh rate (G-Sync/FreeSync)"), &vrr)) {
      dbz3::settings::SetVrrEnabled(vrr);
    }
    ui::Tip(i18n::T(
          "Sincroniza el monitor con cada fotograma para evitar saltos "
          "en pantallas de alta frecuencia. Si esta desactivado, el "
          "juego ajusta el ritmo solo para que se vea fluido a 60 Hz.",
          "Syncs the monitor to every frame for even pacing on high-refresh "
          "panels. If off, the game paces itself to look smooth at 60 Hz."));

    // Detected once (EnumDisplaySettingsW is a system call; no need to repeat
    // it every frame). Just informational in the UI.
    static double cached_hz = 0.0;
    if (cached_hz == 0.0) {
      cached_hz = dbz3::settings::DetectRefreshRate();
    }
    const int hz_int = static_cast<int>(cached_hz + 0.5);
    if (hz_int >= 30) {
      ImGui::TextDisabled(i18n::T("Monitor: %d Hz", "Monitor: %d Hz"), hz_int);
      ui::Tip(i18n::T(
            "El launcher no depende del refresco del monitor. El "
            "juego se ve fluido a cualquier Hz.",
            "The launcher does not depend on the monitor refresh. "
            "The game looks smooth at any Hz."));
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
    ui::RowLabel(i18n::T("Motor grafico", "Graphics backend"),
                 i18n::T("API de render del host. Requiere reinicio.",
                         "Host rendering API. Restart required."));
    if (ImGui::Combo("##gpu_backend", &backend_idx, backends, 2)) {
      dbz3::settings::SetGpuBackend(backend_vals[backend_idx]);
    }
    ui::Tip(i18n::T(
          "Vulkan es experimental: el combate 3D corre notablemente mas lento "
          "que D3D12 en hardware NVIDIA. Usa D3D12 salvo que necesites Vulkan "
          "por compatibilidad.",
          "Vulkan is experimental: 3D combat runs notably slower "
          "than D3D12 on NVIDIA hardware. Use D3D12 unless you need "
          "Vulkan for platform compatibility."));

    ImGui::Spacing();
    PushSectionHeader(i18n::T("Idioma", "Language"));

    static const char* lang_items[] = {"English", "Japanese", "German", "French", "Spanish", "Italian"};
    static const int lang_ids[] = {1, 2, 3, 4, 5, 6};
    int lang = dbz3::settings::Language();
    int lang_idx = 0;
    for (int i = 0; i < 6; i++) {
      if (lang == lang_ids[i]) lang_idx = i;
    }
    ui::RowLabel(i18n::T("Idioma del launcher y del juego", "Launcher and game language"));
    if (ImGui::Combo("##DrawVideoTab_5",
                     &lang_idx, lang_items, 6)) {
      dbz3::settings::SetLanguage(lang_ids[lang_idx]);
    }
    ui::Tip(i18n::T(
          "Traduce el launcher y el texto del juego a este idioma. "
          "Se aplica en el proximo arranque.",
          "Translates the launcher and the in-game text to this language. "
          "Applies on the next launch."));
  }
  ImGui::EndChild();
}


void LauncherDialog::DrawUpscaleTab() {
  ImGui::BeginChild("##upscale_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  PushSectionHeader(i18n::T("Escalado", "Upscaling"));

  const char* effects[] = {"Bilinear", i18n::T("CAS (nitidez)", "CAS (sharpen)"),
                           "FSR 1 (FidelityFX)", "AMD FSR 3 (beta)", "NVIDIA DLSS (beta)"};
  static const char* effect_values[] = {"bilinear", "cas", "fsr", "fsr3", "dlss"};
  int eff_idx = 0;
  std::string eff = dbz3::settings::PresentEffect();
  for (int i = 0; i < 5; i++) {
    if (eff == effect_values[i]) eff_idx = i;
  }
  ui::RowLabel(i18n::T("Efecto", "Effect"));
  if (ImGui::Combo("##DrawUpscaleTab_1", &eff_idx, effects, 5)) {
    dbz3::settings::SetPresentEffect(effect_values[eff_idx]);
    // Persist immediately so the chosen upscaling effect survives a launch/close
    // without the user pressing "Save settings".
    dbz3::settings::SaveUserSettings();
  }

  eff = dbz3::settings::PresentEffect();
  if (eff == "fsr" || eff == "fsr3" || eff == "dlss") {
    if (eff == "fsr") {
      ImGui::TextWrapped(i18n::T(
          "FSR escala el render interno al tamano de la pantalla.\n"
          "Ideal con una escala interna baja (1x) en pantallas 1080p o superiores.",
          "FSR upscales the internal render to the display size.\n"
          "Best paired with a low internal scale (1x) on a 1080p+ display."));
    } else {
      ImGui::TextWrapped(i18n::T(
          "Escalado temporal: usa los fotogramas anteriores para suavizar bordes y recuperar "
          "detalle. En pruebas (beta).\n"
          "DLSS necesita una grafica NVIDIA RTX y D3D12; si no la hay, se usa FSR 3.",
          "Temporal upscaling: uses previous frames to smooth edges and recover detail. "
          "Beta.\n"
          "DLSS needs an NVIDIA RTX card and D3D12; otherwise FSR 3 is used."));
    }
    ImGui::Spacing();
    {
      const char* render_items[] = {i18n::T("Nativa (maxima calidad)", "Native (best quality)"),
                                    i18n::T("Calidad", "Quality"), i18n::T("Equilibrado", "Balanced"),
                                    i18n::T("Rendimiento", "Performance"),
                                    i18n::T("Ultra rendimiento", "Ultra performance")};
      static const char* render_vals[] = {"native", "quality", "balanced", "performance",
                                          "ultra_performance"};
      int render_idx = 0;
      const std::string cur = dbz3::settings::FsrRender();
      for (int i = 0; i < 5; i++) {
        if (cur == render_vals[i]) render_idx = i;
      }
      ui::RowLabel(i18n::T("Mas FPS", "More FPS"),
                   i18n::T("Renderiza por debajo de la escala interna y el escalado la recupera.",
                           "Renders below the internal scale and the upscaler brings it back up."));
      if (ImGui::Combo("##DrawUpscaleTab_render", &render_idx, render_items, 5)) {
        dbz3::settings::SetFsrRender(render_vals[render_idx]);
        dbz3::settings::SaveUserSettings();
      }
      ui::Tip(i18n::T("Como en los juegos de PC, pero solo hay escalas enteras: a 3x, Calidad "
                      "renderiza a 2x y se reescala a 3x; a 2x solo Rendimiento baja (a 1x). Da "
                      "FPS de verdad en equipos justos. Con 1x no hay nada por debajo.",
                      "Like PC games, but only whole scales exist: at 3x, Quality renders at 2x "
                      "and is scaled to 3x; at 2x only Performance goes lower (to 1x). Real FPS "
                      "on modest PCs. At 1x there is nothing below."));
    }
    // DLSS has no sharpening of its own.
    if (eff != "dlss") {
      double sharp = dbz3::settings::FsrSharpness();
      ImGui::SetNextItemWidth(260);
      ui::RowLabel(i18n::T("Nitidez RCAS", "RCAS sharpness"));
      if (SliderD("##DrawUpscaleTab_2", &sharp, 0.0, 2.0, "%.2f")) {
        dbz3::settings::SetFsrSharpness(sharp);
        dbz3::settings::SaveUserSettings();
      }
      ImGui::SameLine();
      ImGui::TextDisabled(i18n::T("0 = mas nitido, 2 = mas suave",
                                  "0 = sharper, 2 = softer"));
    }
  } else if (dbz3::settings::PresentEffect() == "cas") {
    ImGui::TextWrapped(i18n::T(
        "CAS aplica nitidez adaptativa al contraste despues del escalado.\n"
        "Muy bueno con una escala interna alta (2x-3x).",
        "CAS applies contrast-adaptive sharpening after scaling.\n"
        "Great with a high internal scale (2x-3x)."));
    ImGui::Spacing();
    double sharp = dbz3::settings::CasSharpness();
    ImGui::SetNextItemWidth(260);
    ui::RowLabel(i18n::T("Nitidez adicional", "Additional sharpness"));
    if (SliderD("##DrawUpscaleTab_3", &sharp, 0.0,
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
  ui::RowLabel(i18n::T("Suavizado de bordes (FXAA)", "Edge smoothing (FXAA)"));
  if (ImGui::Combo("##DrawUpscaleTab_4",
                   &fxaa_idx, fxaa_items, 3)) {
    dbz3::settings::SetFxaa(fxaa_vals[fxaa_idx]);
    dbz3::settings::SaveUserSettings();
  }
  ui::Tip(i18n::T(
        "Suaviza los bordes en el ultimo paso. Muy barato: ideal si subir la "
        "escala interna o el MSAA te cuesta demasiado rendimiento. Se combina "
        "con FSR/CAS.",
        "Smooths edges in the final step. Very cheap: ideal if raising the "
        "internal scale or MSAA costs you too much performance. Composes with "
        "FSR/CAS."));

  bool dither = dbz3::settings::PresentDither();
  if (ImGui::Checkbox(i18n::T("Tramado de color (dither)", "Color dithering"),
                      &dither)) {
    dbz3::settings::SetPresentDither(dither);
    dbz3::settings::SaveUserSettings();
  }
  ui::Tip(i18n::T(
        "Aplica tramado a la imagen final: menos bandas en los degradados, a "
        "cambio de un poco de ruido.",
        "Dithers the final image: fewer gradient bands, at the cost of a little "
        "noise."));

  ImGui::Spacing();
  ImGui::TextColored(kTextDim,
                     i18n::T("Se aplica al momento. En partida tambien puedes cambiarlo con F1 o "
                             "Back + Start en el mando.",
                             "Applies right away. In game you can also change it with F1 or Back + "
                             "Start on the controller."));

  ImGui::EndChild();
}

void LauncherDialog::DrawAudioTab() {
  ImGui::BeginChild("##audio_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  PushSectionHeader(i18n::T("Volumen", "Volume"));

  // Real controls: the SDK multiplies the guest mix by `audio_gain` (hot
  // reloadable, applied in the SDL callback) and `audio_mute` hard-silences it.
  // Both are applied to the running game as soon as they change, so the effect
  // is audible without pressing Play again.
  double master = dbz3::settings::MasterVolume();
  ui::RowLabel(i18n::T("Volumen general", "Master volume"));
  if (SliderD("##DrawAudioTab_1", & master, 0.0, 1.0, "%.2f")) {
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
  ui::Tip(i18n::T(
        "Silencia el juego por completo (equivale a volumen 0).",
        "Silences the game completely (same as volume 0)."));

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
  ImGui::BeginChild("##input_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  PushSectionHeader(i18n::T("Mando", "Controller"));

  const char* backend_items[] = {i18n::T("XInput (nativo)", "XInput (native)"),
                                        i18n::T("SDL (mandos genericos)", "SDL (generic pads)")};
  static const char* backend_values[] = {"xinput", "sdl"};
  std::string backend = dbz3::settings::InputBackend();
  int backend_idx = backend == "sdl" ? 1 : 0;
  ui::RowLabel(i18n::T("Backend del mando", "Controller backend"));
  if (ImGui::Combo("##DrawInputTab_1", &backend_idx,
                   backend_items, 2)) {
    dbz3::settings::SetInputBackend(backend_values[backend_idx]);
  }
  ui::Tip(i18n::T(
        "XInput = mandos de PC estandar, sin init extra. SDL = mandos genericos, "
        "pero puede colgar con RTSS/OBS. Requiere reinicio.",
        "XInput = standard PC pads, no extra init. SDL = generic pads, "
        "but may hang with RTSS/OBS. Restart to apply."));

  const char* glyph_items[] = {"Xbox", "PlayStation", "Switch"};
  static const char* glyph_values[] = {"xbox", "playstation", "switch"};
  std::string glyphs = dbz3::settings::InputGlyphs();
  int glyph_idx = glyphs == "playstation" ? 1 : (glyphs == "switch" ? 2 : 0);
  ui::RowLabel(i18n::T("Tipo de mando", "Button labels"));
  if (ImGui::Combo("##DrawInputTab_2", &glyph_idx, glyph_items, 3)) {
    dbz3::settings::SetInputGlyphs(glyph_values[glyph_idx]);
  }
  ui::Tip(i18n::T(
        "Cambia los nombres de los botones que se ven abajo (LT/L2/ZL, ...). "
        "Solo afecta a las etiquetas del launcher, no al mapeo del juego.",
        "Changes the button names shown below (LT/L2/ZL, ...). Only affects "
        "the launcher labels, not the game mapping."));

  double deadzone = dbz3::settings::Deadzone();
  ui::RowLabel(i18n::T("Zona muerta de los sticks", "Analog stick deadzone"));
  if (SliderD("##DrawInputTab_3", &deadzone, 0.0, 0.9,
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
  ui::Tip(i18n::T(
        "Emula un mando con el teclado. Necesario para jugar sin pad. Usa las teclas de abajo.",
        "Emulates a controller with the keyboard. Required to play "
        "without a pad. Use the keybinds below."));

  bool mnk_mouse = dbz3::settings::MnkMouse();
  if (ImGui::Checkbox(i18n::T("Usar el raton para el stick derecho",
                              "Use mouse for the right stick"), &mnk_mouse)) {
    dbz3::settings::SetMnkMouse(mnk_mouse);
  }
  ui::Tip(i18n::T(
        "Mueve el stick derecho con el raton (ademas de las teclas rstick_*).",
        "Moves the right stick with the mouse (in addition to the rstick_* keys)."));

  if (dbz3::settings::MnkMouse()) {
    double sens = dbz3::settings::MnkSensitivity();
    ui::RowLabel(i18n::T("Sensibilidad del raton", "Mouse sensitivity"));
    if (SliderD("##DrawInputTab_4", &sens, 0.1,
                5.0, "%.2f")) {
      dbz3::settings::SetMnkSensitivity(sens);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", i18n::T("1.0 = por defecto", "1.0 = default"));
  }

  ImGui::Spacing();
  ImGui::Text(i18n::T("Mapeo de teclas (MnK)", "Keyboard (MnK) mapping"));
  ui::Tip(i18n::T(
        "Los nombres de tecla siguen VirtualKey (p.ej. Space, W, Up, LMB, RMB, MMB). "
        "Coma = alternativas, Shift+/Ctrl+/Alt+ = modificadores. Vacio = sin asignar.",
        "Key names follow VirtualKey (e.g. Space, W, Up, LMB, RMB, MMB). "
        "Comma = alternatives, Shift+/Ctrl+/Alt+ = modifiers. Empty = unbound."));

  // Three-column grid so all 24 keybinds fit in the 720p window without
  // scrolling (compact one-screen layout).
  {
    const float kb_w =
        ImGui::GetContentRegionAvail().x / 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f / 3.0f;
    int kb_col = 0;
#define DBZ3_DRAW_KEYBIND(name)                                          \
  do {                                                                   \
    std::string kb_val = dbz3::settings::Keybind(#name);                 \
    const char* kb_lbl = ButtonGlyph(glyphs, #name);                     \
    const float kb_lw = ImGui::CalcTextSize(kb_lbl).x;                   \
    std::string kb_tag = std::string(kb_lbl) + "##kb_" #name;            \
    float kb_iw = kb_w - kb_lw - ImGui::GetStyle().ItemInnerSpacing.x - 6.0f; \
    if (kb_iw < 40.0f) kb_iw = 40.0f;                                    \
    ImGui::SetNextItemWidth(kb_iw);                                      \
    DrawKeybind(kb_tag.c_str(), kb_val);                                 \
    dbz3::settings::SetKeybind(#name, kb_val);                           \
    if (kb_col % 3 != 2) ImGui::SameLine();                              \
    ++kb_col;                                                            \
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
  ImGui::BeginChild("##native_mods_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  ui::SectionTitle(i18n::T("Mods nativos", "Native mods"));
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
  // "Brillo HD": the HD remaster's rim light on the character models.
  {
    double rim = dbz3::settings::HdRimLight();
    bool rim_on = rim > 0.0;
    ImGui::Text("%s", i18n::T("Brillo HD de los personajes", "HD shine on characters"));
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", i18n::T("Graficos", "Graphics"));
    ImGui::TextWrapped("%s", i18n::T(
        "El remaster HD anade un halo brillante en el borde de los modelos. Quitalo para un "
        "aspecto mas plano, como en PS2, o bajalo a tu gusto. Se aplica al momento.",
        "The HD remaster adds a bright halo on the edges of the models. Turn it off for a "
        "flatter, PS2-like look, or lower it to taste. Applies right away."));
    bool changed = false;
    if (ImGui::Checkbox(i18n::T("Activado", "Enabled"), &rim_on)) {
      rim = rim_on ? 1.0 : 0.0;
      changed = true;
    }
    if (rim_on) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(220.0f);
      double pct = rim * 100.0;
      if (SliderD("##hd_rim_light", &pct, 5.0, 100.0, "%.0f %%")) {
        rim = pct / 100.0;
        changed = true;
      }
    }
    if (changed) {
      dbz3::settings::SetHdRimLight(rim);
      dbz3::settings::ApplyRuntimeSettingsToSdk(false);
      dbz3::settings::SaveUserSettings();
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
      const bool avail = mod.id != "save_100" || dbz3::Save100Available();
      if (!avail) ImGui::BeginDisabled();
      if (ImGui::Button(i18n::T("Aplicar (reemplaza tu partida)", "Apply (replaces your save)"))) {
        ImGui::OpenPopup("###confirm_save100");
      }
      if (!avail) ImGui::EndDisabled();
      if (ConfirmModal((std::string(i18n::T("Partida", "Save")) + "###confirm_save100").c_str(),
                       i18n::T("Tu partida se sustituira por esta. Antes se hace una copia de seguridad "
                               "(la recuperas con 'Restaurar mi partida'). Continuar?",
                               "Your save will be replaced with this one. A backup is made first "
                               "(get it back with 'Restore my save'). Continue?"),
                       i18n::T("Si, aplicar", "Yes, apply"))) {
        dbz3::ApplySave100(native_mods_status_);
      }
      ImGui::SameLine();
      if (ImGui::Button(i18n::T("Restaurar mi partida", "Restore my save"))) {
        ImGui::OpenPopup("###confirm_restore");
      }
      if (ConfirmModal((std::string(i18n::T("Partida", "Save")) + "###confirm_restore").c_str(),
                       i18n::T("Vuelve a la ultima copia de seguridad de tu partida (la de ahora se "
                               "sustituye). Continuar?",
                               "Goes back to the last backup of your save (the current one is "
                               "replaced). Continue?"),
                       i18n::T("Si, restaurar", "Yes, restore"))) {
        dbz3::RestoreLastSaveBackup(native_mods_status_);
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
            "Todavia no hay forma de activarlo en este juego.",
            "Not available for this game yet."));
      }
    }
    ImGui::Separator();
    ImGui::PopID();
  }

  if (!native_mods_status_.empty()) {
    ImGui::TextColored(kOk, "%s",
                       native_mods_status_.c_str());
  }
  ImGui::EndChild();
}

void LauncherDialog::DrawModsTab() {
  ImGui::BeginChild("##mods_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

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
    broken_mods_ = 0;
    active_mods_ = 0;
    for (const dbz3::ModInfo& m : mods_cache_) {
      if (m.enabled) ++active_mods_;
      if (m.enabled && !m.problems.empty()) ++broken_mods_;
    }
    // Re-detect texture packs (folders with the dump's `<hash>_<W>x<H>_<F>.dds`
    // files) so enabling/disabling a pack is reflected before Play.
    dbz3::settings::RefreshTexturePacks();
    mods_loaded_ = true;
  }

  ui::SectionTitle(i18n::T("Centro de mods", "Mod center"));
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
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextWrapped(i18n::T(
        "Aviso: estas en modo disco (ISO). Los mods NO se aplican al jugar "
        "directamente del .iso. Para usarlos, elige 'Carpeta extraida' como "
        "origen de los datos (en el selector de origen).",
        "Warning: you are in disc mode (ISO). Mods are NOT applied when playing "
        "straight from the .iso. To use them, pick 'Extracted folder' as the "
        "data source (in the source selector)."));
    ImGui::PopStyleColor();
  }

  // Texturas faciles: capturar las texturas del juego, prepararlas como PNG con el nombre que
  // reconoce un pack y editarlas en "mods/Mi pack de texturas" (el runtime acepta PNG).
  // Lo mas pedido en foros de modding: no mezclar captura y carga, PNG directos, sin pasos raros.
  {
    const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
    const std::filesystem::path edit_dir = exe_dir / "texturas" / "para_editar";
    const std::filesystem::path my_pack = dbz3::ModsRoot() / "Mi pack de texturas";
    std::string cap_dir = dbz3::settings::TextureDumpDir();
    if (cap_dir.empty()) cap_dir = dbz3::settings::DefaultTextureDumpDir();
    if (tex_easy_captures_ < 0) {
      tex_easy_captures_ = 0;
      std::ifstream idx(std::filesystem::path(cap_dir) / "index.jsonl");
      for (std::string line; std::getline(idx, line);) {
        if (!line.empty()) ++tex_easy_captures_;
      }
    }
    auto open_dir = [](const std::filesystem::path& d) {
      std::error_code ec;
      std::filesystem::create_directories(d, ec);
      std::system(("explorer \"" + d.string() + "\"").c_str());
    };
    ImGui::Spacing();
    ui::SectionTitle(i18n::T("Texturas faciles (PNG)", "Easy textures (PNG)"));
    ImGui::TextWrapped("%s", i18n::T(
        "1. Activa 'Capturar texturas' y juega hasta ver lo que quieres cambiar.\n"
        "2. Pulsa 'Preparar PNG' y abre la carpeta: estan ordenados por tamano.\n"
        "3. Edita el PNG que quieras SIN cambiarle el nombre (puede ser 2, 3 o 4 veces mas "
        "grande) y guardalo en 'Mi pack de texturas'.\n"
        "4. Juega: se aplica solo. Para quitarlo, desactiva el pack en la lista de abajo.",
        "1. Turn on 'Capture textures' and play until you see what you want to change.\n"
        "2. Press 'Prepare PNGs' and open the folder: they are sorted by size.\n"
        "3. Edit any PNG WITHOUT renaming it (it can be 2, 3 or 4 times bigger) and save it "
        "in 'My texture pack'.\n"
        "4. Play: it is applied automatically. To remove it, disable the pack in the list below."));
    bool capture = dbz3::settings::TextureDumpEnabled();
    if (ImGui::Checkbox(i18n::T("Capturar texturas mientras juego", "Capture textures while playing"),
                        &capture)) {
      dbz3::settings::SetTextureDumpEnabled(capture);   // sin carpeta: texturas/capturas
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(i18n::T(
        "Guarda cada textura que aparece en pantalla (personajes, escenarios, menus) en la "
        "carpeta 'texturas/capturas' junto al juego. Desactivalo cuando termines: ocupa espacio.",
        "Saves every texture shown on screen (characters, stages, menus) to the "
        "'texturas/capturas' folder next to the game. Turn it off when done: it uses disk space."));
    if (capture && dbz3::settings::HdTextures() > 1) {
      ImGui::PushStyleColor(ImGuiCol_Text, kGold);
      ImGui::TextWrapped("%s", i18n::T(
          "La captura no funciona con la mejora de texturas HD activada.",
          "Capturing does not work while HD texture enhancement is on."));
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Desactivar la mejora", "Turn enhancement off"))) {
        dbz3::settings::SetHdTextures(1);
        dbz3::settings::SaveUserSettings();
      }
    }
    if (tex_easy_job_ && !mod_pipeline_.IsRunning()) {
      tex_easy_job_ = false;
      const std::string out = mod_pipeline_.Output();
      const bool ok = out.find("Listo:") != std::string::npos;
      tex_easy_status_ = ok ? std::string(i18n::T("PNG preparados en 'texturas/para_editar'.",
                                                  "PNGs ready in 'texturas/para_editar'."))
                            : out.substr(out.size() > 600 ? out.size() - 600 : 0);
      if (ok) open_dir(edit_dir);
    }
    ImGui::BeginDisabled(tex_easy_captures_ == 0 || mod_pipeline_.IsRunning());
    char prep[96];
    std::snprintf(prep, sizeof(prep), i18n::T("Preparar PNG (%d capturadas)", "Prepare PNGs (%d captured)"),
                  tex_easy_captures_);
    if (ImGui::Button(prep)) {
      tex_easy_job_ = true;
      tex_easy_status_ = i18n::T("Preparando PNG...", "Preparing PNGs...");
      mod_pipeline_.ConvertTextureCaptures(cap_dir, edit_dir.string());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Abrir PNG para editar", "Open PNGs to edit"))) open_dir(edit_dir);
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Abrir 'Mi pack de texturas'", "Open 'My texture pack'"))) {
      std::error_code ec;
      std::filesystem::create_directories(my_pack, ec);
      const auto readme = my_pack / "LEEME.txt";
      if (!std::filesystem::exists(readme, ec)) {
        std::ofstream(readme) << "Pega aqui los PNG editados de texturas/para_editar SIN cambiarles el nombre.\n"
                                 "Paste the edited PNGs from texturas/para_editar here WITHOUT renaming them.\n";
      }
      open_dir(my_pack);
      mods_loaded_ = false;
    }
    if (!tex_easy_status_.empty()) ImGui::TextDisabled("%s", tex_easy_status_.c_str());
    const std::string packs = dbz3::settings::TexturePacksList();
    if (!packs.empty()) {
      ImGui::Text("%s", i18n::T("Packs de texturas activos:", "Active texture packs:"));
      ImGui::SameLine();
      ImGui::TextDisabled("%s", packs.c_str());
    }
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
        ImGui::OpenPopup("###confirm_delprofile");
      }
      if (ConfirmModal((std::string(i18n::T("Borrar perfil", "Delete profile")) + "###confirm_delprofile").c_str(),
                       i18n::T("Borrar este perfil? Tus mods no se borran: solo la lista de cuales "
                               "estan activos.",
                               "Delete this profile? Your mods are not deleted: only the list of "
                               "which ones are on."),
                       i18n::T("Si, borrar", "Yes, delete"))) {
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
        static std::string overwrite_warned;   // 2a pulsacion = sustituir
        const std::vector<std::string> profs = dbz3::ListProfiles();
        const bool exists = std::find(profs.begin(), profs.end(), pname) != profs.end();
        if (pname.empty() || pname == "vanilla") {
          mods_status_ =
              std::string(i18n::T("Nombre de perfil invalido.",
                                  "Invalid profile name."));
        } else if (exists && overwrite_warned != pname) {
          overwrite_warned = pname;
          mods_status_ = i18n::T("Ya existe un perfil con ese nombre: pulsa Guardar otra vez para sustituirlo.",
                                 "A profile with that name exists: press Save again to replace it.");
        } else {
          overwrite_warned.clear();
          std::vector<std::string> enabled;
          for (const dbz3::ModInfo& m : dbz3::ListMods()) {
            if (m.enabled) enabled.push_back(m.name);
          }
          dbz3::SaveProfile(pname, enabled);
          dbz3::settings::SetModProfile(pname);
          mods_status_ =
              std::string(i18n::T("Perfil guardado: ", "Profile saved: ")) + pname;
          profile_name_dialog_ = false;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button(i18n::T("Cancelar", "Cancel"), ImVec2(0, 0))) {
        profile_name_dialog_ = false;
      }
    }

    // Transient status line (click to dismiss).
    if (!mods_status_.empty()) {
      ImGui::TextColored(kOk, "%s",
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
  int broken = 0;
  for (const dbz3::ModInfo& mod : mods) {
    if (mod.enabled && !mod.problems.empty()) ++broken;
  }
  if (broken) {
    ImGui::SameLine();
    ImGui::TextColored(kGold, i18n::T("  -  %d con problemas (en amarillo, abajo)",
                                      "  -  %d with problems (in yellow, below)"), broken);
  }
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
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
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
      if (mod.type == "generado") {
        ImGui::TextDisabled("%s", i18n::T(
            "Se crea solo con los personajes nuevos activos (pestana Personajes nuevos). "
            "No hace falta tocarlo.",
            "Built automatically from the enabled new characters (New characters tab). "
            "No need to touch it."));
      } else if (mod.type == "personaje") {
        ImGui::TextDisabled("%s", i18n::T("Personaje nuevo: se edita en la pestana Personajes nuevos "
                                          "(boton Editar de la derecha).",
                                          "New character: edit it in the New characters tab "
                                          "(Edit button on the right)."));
      }
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
      for (const auto& [kind, detail] : mod.problems) {
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::TextWrapped("! %s", ModProblemText(kind, detail).c_str());
        ImGui::PopStyleColor();
        if (kind == dbz3::ModInfo::kNested &&
            ImGui::SmallButton((std::string(i18n::T("Arreglar", "Fix")) + "##fix_" + mod.name).c_str())) {
          mods_status_ = dbz3::FixNestedMod(mod.name)
                             ? std::string(i18n::T("Mod arreglado: ", "Mod fixed: ")) + mod.name
                             : std::string(i18n::T("No se pudo arreglar solo; abre su carpeta: ",
                                                   "Could not fix it automatically; open its folder: ")) + mod.name;
          mods_loaded_ = false;
        }
      }

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
        ImGui::TextColored(kOk, "ON");
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
      if (ImGui::SmallButton((std::string(i18n::T("Carpeta", "Folder")) + "##folder_" + mod.name).c_str())) {
        std::string cmd = "explorer \"" + (dbz3::ModsRoot() / mod.name).string() + "\"";
        std::system(cmd.c_str());
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", i18n::T("Abrir carpeta del mod", "Open mod folder"));
      }
      ImGui::SameLine();
      if (ImGui::SmallButton((std::string(i18n::T("Editar", "Edit")) + "##edit_" + mod.name).c_str())) {
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
      if (mod.type == "personaje") {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.25f, 0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.34f, 0.70f, 1.0f));
        const bool go = ImGui::SmallButton((std::string(i18n::T("Editar", "Edit")) + "##char_" + mod.name).c_str());
        ImGui::PopStyleColor(2);
        if (go) {
          nc_selected_ = mod.name;
          request_tab_ = 2;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", i18n::T("Editar este personaje (icono, retratos, plaza, capsulas)",
                                          "Edit this character (icon, portraits, slot, capsules)"));
        }
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
  ImGui::BeginChild("##model_swap", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  ui::SectionTitle(i18n::T("Cambio de modelo B3 HD -> B3 HD", "Model swap B3 HD -> B3 HD"));
  ImGui::TextDisabled(i18n::T(
      "Intercambia el bin #AMB completo de un personaje HD del "
      "B3 por el de otro (swap nativo). Genera el mod y lo activa.",
      "Swaps the full #AMB bin of one B3 HD character for another (native "
      "swap). Generates the mod and activates it."));

  const bool iso = dbz3::settings::IsIsoMode();
  if (iso) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
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
  ui::SectionTitle(i18n::T("Archivo de modelos (data_cmn.afs)",
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
    ImGui::TextColored(kGold, "%s",
                       i18n::T("Origen y destino son el mismo personaje.",
                               "Source and destination are the same character."));
  }

  if (pipeline_src_idx_ >= 0 && pipeline_dst_idx_ >= 0) {
    const B3Char& src = chars[pipeline_src_idx_];
    const B3Char& dst = chars[pipeline_dst_idx_];
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTabBg);
    ImGui::BeginChild("##swap_preview", ImVec2(0, 108.0f), true);
    ImGui::TextColored(kDragonOrange, "%s", i18n::T("Vista previa", "Preview"));
    ImGui::Text(i18n::T("Origen:  %s", "Source:  %s"), src.DisplayName().c_str());
    if (!src.playable) {
      ImGui::SameLine();
      ImGui::TextColored(kGold, "%s",
                         i18n::T("(no jugable)", "(not playable)"));
    }
    ImGui::TextDisabled("bin %d", src.bin);
    ImGui::Text(i18n::T("Destino: %s", "Target: %s"), dst.DisplayName().c_str());
    if (!dst.playable) {
      ImGui::SameLine();
      ImGui::TextColored(kGold, "%s",
                         i18n::T("(no jugable)", "(not playable)"));
    }
    ImGui::TextDisabled("slot %d", dst.bin);
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  ImGui::Separator();
  if (mod_pipeline_.IsRunning()) {
    ImGui::TextColored(kGold, i18n::T("Trabajando...", "Working..."));
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled(i18n::T("Hecho.", "Done."));
  }
  DrawToolLog(mod_pipeline_.Output(), output_buf_, sizeof(output_buf_), "swap_out", 160.0f);
  if (!iso) {
    ImGui::TextDisabled(i18n::T(
        "El mod generado se activa solo y se lista en la pestana Mods.",
        "The generated mod activates itself and shows up in the Mods tab."));
  }

  ImGui::EndChild();
}

namespace {

// Donantes posibles: personajes con casilla propia en el select (los IDs 22-26 y 31
// son los recortados de fabrica: los ocupan los personajes nuevos).
struct DonorEntry {
  int id;
  const char* name;      // espanol
  const char* en;        // ingles
  const char* Name() const { return i18n::T(name, en); }
};
constexpr DonorEntry kDonors[] = {
    {0, "Goku", "Goku"},                     {1, "Goku (nino)", "Goku (kid)"},
    {2, "Gohan (nino)", "Gohan (kid)"},      {3, "Gohan (adolescente)", "Gohan (teen)"},
    {4, "Gohan (adulto)", "Gohan (adult)"},  {5, "Gran Saiyaman", "Great Saiyaman"},
    {6, "Goten", "Goten"},                   {7, "Vegeta", "Vegeta"},
    {8, "Trunks", "Trunks"},                 {9, "Trunks (nino)", "Trunks (kid)"},
    {10, "Krilin", "Krillin"},               {11, "Piccolo", "Piccolo"},
    {12, "Ten Shin Han", "Tien"},            {13, "Yamcha", "Yamcha"},
    {14, "Mr. Satan", "Hercule"},            {15, "Videl", "Videl"},
    {16, "Kaio-shin", "Supreme Kai"},        {17, "Uub", "Uub"},
    {18, "Raditz", "Raditz"},                {19, "Nappa", "Nappa"},
    {20, "Ginyu", "Ginyu"},                  {21, "Recoome", "Recoome"},
    {27, "Freezer", "Frieza"},               {28, "Androide 16", "Android 16"},
    {29, "Androide 17", "Android 17"},       {30, "Androide 18", "Android 18"},
    {32, "Dr. Gero", "Dr. Gero"},            {33, "Cell", "Cell"},
    {34, "Majin Buu", "Majin Buu"},          {35, "Super Buu", "Super Buu"},
    {36, "Kid Buu", "Kid Buu"},              {37, "Dabura", "Dabura"},
    {38, "Cooler", "Cooler"},                {39, "Bardock", "Bardock"},
    {40, "Broly", "Broly"},                  {41, "Omega Shenron", "Omega Shenron"},
    {42, "Saibaman", "Saibaman"},            {43, "Cell Jr.", "Cell Jr."},
};
constexpr int kDonorCount = int(sizeof(kDonors) / sizeof(kDonors[0]));

// Plazas: los unicos IDs de personaje libres del juego (recortados de fabrica).
struct FreeSlot {
  int id;
  const char* original;
};
// Plazas: los 6 IDs recortados de fabrica y los 20 IDs extra (44-63, vacios en todas
// las tablas por personaje del juego; ver src/roster_ext.cpp).
constexpr FreeSlot kFreeSlots[] = {
    {22, "Guldo"},    {23, "Jeice"},    {24, "Burter"},   {25, "Zarbon"},   {26, "Dodoria"},
    {31, "Android 19"}, {44, "Extra 1"}, {45, "Extra 2"},  {46, "Extra 3"},  {47, "Extra 4"},
    {48, "Extra 5"},  {49, "Extra 6"},  {50, "Extra 7"},  {51, "Extra 8"},  {52, "Extra 9"},
    {53, "Extra 10"}, {54, "Extra 11"}, {55, "Extra 12"}, {56, "Extra 13"}, {57, "Extra 14"},
    {58, "Extra 15"}, {59, "Extra 16"}, {60, "Extra 17"}, {61, "Extra 18"}, {62, "Extra 19"},
    {63, "Extra 20"}};
constexpr int kFreeSlotCount = int(sizeof(kFreeSlots) / sizeof(kFreeSlots[0]));

// "(?)" con explicacion al pasar el raton.
void Help(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

const char* CapsuleKindLabel(const std::string& k) {
  if (k == "definitiva") return i18n::T("Definitiva", "Ultimate");
  if (k == "transformacion") return i18n::T("Transformacion", "Transformation");
  return i18n::T("Especial", "Special");
}

// [[capsula]] de un personaje.toml (nombre, tipo, forma), en orden.
std::vector<std::array<std::string, 3>> TomlCapsules(const std::string& text) {
  std::vector<std::array<std::string, 3>> out;
  std::istringstream in(text);
  std::string line;
  bool in_cap = false;
  while (std::getline(in, line)) {
    std::string t = line;
    t.erase(0, t.find_first_not_of(" \t"));
    if (!t.empty() && t[0] == '[') {
      in_cap = t.rfind("[[capsula]]", 0) == 0;
      if (in_cap) out.push_back({"?", "especial", "1"});
      continue;
    }
    if (!in_cap || out.empty()) continue;
    const auto eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string k = t.substr(0, eq);
    k.erase(k.find_last_not_of(" \t") + 1);
    std::string v = t.substr(eq + 1);
    v.erase(0, v.find_first_not_of(" \t\""));
    v.erase(v.find_last_not_of(" \t\"\r") + 1);
    if (k == "nombre") out.back()[0] = v;
    if (k == "tipo") out.back()[1] = v;
    if (k == "forma") out.back()[2] = v;
  }
  return out;
}

rex::ui::ImmediateDrawer* g_preview_drawer = nullptr;

std::string ReadSmallFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Valor de "clave = ..." (texto, entero o lista) de un personaje.toml sencillo.
std::string TomlValue(const std::string& text, const std::string& key) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq);
    k.erase(k.find_last_not_of(" \t") + 1);
    if (k != key) continue;
    std::string v = line.substr(eq + 1);
    v.erase(0, v.find_first_not_of(" \t\""));
    v.erase(v.find_last_not_of(" \t\"\r") + 1);
    return v;
  }
  return {};
}

int TomlInt(const std::string& text, const std::string& key, int def) {
  const std::string v = TomlValue(text, key);
  return v.empty() ? def : std::atoi(v.c_str());
}

// "[1.0, 0, 0.05, 12]" -> 4 floats (los que falten se quedan como estan).
void TomlFloats(const std::string& text, const std::string& key, float (&out)[4]) {
  std::string v = TomlValue(text, key);
  for (char& ch : v) {
    if (ch == '[' || ch == ']' || ch == ',') ch = ' ';
  }
  std::istringstream in(v);
  for (float& f : out) {
    float x;
    if (!(in >> x)) break;
    f = x;
  }
}

int SourceIndex(const std::string& v) {
  if (v == "imagen") return 1;
  if (v == "terminado") return 2;
  return 0;
}

const char* SourceName(int i) { return i == 1 ? "imagen" : i == 2 ? "terminado" : "modelo"; }

const char* DonorName(int id) {
  for (const auto& d : kDonors) {
    if (d.id == id) return d.Name();
  }
  return "?";
}

int DonorIndex(int id) {
  for (int i = 0; i < kDonorCount; ++i) {
    if (kDonors[i].id == id) return i;
  }
  return -1;
}

void DonorCombo(const char* label, int& idx, bool allow_default) {
  const char* def = i18n::T("(la del donante)", "(the donor's)");
  const char* preview = (idx >= 0 && idx < kDonorCount) ? kDonors[idx].Name() : def;
  ImGui::SetNextItemWidth(260);
  if (ImGui::BeginCombo(label, preview)) {
    if (allow_default && ImGui::Selectable(def, idx < 0)) idx = -1;
    for (int i = 0; i < kDonorCount; ++i) {
      if (ImGui::Selectable(kDonors[i].Name(), idx == i)) idx = i;
    }
    ImGui::EndCombo();
  }
}

std::string AdjArg(const float (&a)[4]) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.1f", a[0], a[1], a[2], a[3]);
  return buf;
}

// Name banner as it will look on the select wheel, redrawn on every keystroke.
void LiveBanner(const char* name) {
  ImGui::BeginGroup();
  {
    ui::FontScope f(ui::GetFonts().sm);
    ImGui::TextColored(ui::kTextDim, "%s", i18n::T("Vista previa en directo", "Live preview"));
  }
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(300.0f, 46.0f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilledMultiColor(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(23, 46, 102, 255),
                              IM_COL32(12, 22, 52, 255), IM_COL32(12, 22, 52, 255),
                              IM_COL32(23, 46, 102, 255));
  dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(255, 255, 255, 40), 6.0f);
  if (!ui::NameBanner(name, size.y, size.x)) {
    ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, p.y + 12.0f));
    ImGui::TextUnformatted(name);
  }
  ImGui::EndGroup();
}

}  // namespace

void SetPreviewDrawer(rex::ui::ImmediateDrawer* drawer) { g_preview_drawer = drawer; }


namespace {
std::function<void(std::function<void()>)> g_ui_defer;

rex::input::InputSystem* LauncherInput() {
  auto* runtime = rex::Runtime::instance();
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}
}  // namespace

void SetUiDefer(std::function<void(std::function<void()>)> defer) { g_ui_defer = std::move(defer); }

LauncherDialog::~LauncherDialog() { SetInGame(false); }

void LauncherDialog::SetInGame(bool in_game) {
  in_game_ = in_game;
  auto* input = LauncherInput();
  if (!input) return;
  if (in_game && !input_blocked_) {
    input->AddUIInputBlocker();
    input_blocked_ = true;
  } else if (!in_game && input_blocked_) {
    input->RemoveUIInputBlocker();
    input_blocked_ = false;
  }
}

void LauncherDialog::PollController(ImGuiIO& io) {
  // Read the pads for the next frame (outside this paint).
  if (g_ui_defer && !pad_->pending) {
    pad_->pending = true;
    g_ui_defer([pad = pad_]() {
      PadShared next;
      if (auto* input = LauncherInput()) {
        for (uint32_t user = 0; user < rex::input::kMaxGuestUsers; ++user) {
          rex::input::X_INPUT_STATE state = {};
          if (input->GetStateForUI(user, &state) != 0) continue;  // X_ERROR_SUCCESS
          next.buttons |= static_cast<uint16_t>(state.gamepad.buttons);
          const int16_t lx = state.gamepad.thumb_lx, ly = state.gamepad.thumb_ly;
          if (std::abs(int(lx)) + std::abs(int(ly)) > std::abs(int(next.lx)) + std::abs(int(next.ly))) {
            next.lx = lx;
            next.ly = ly;
          }
        }
      }
      next.valid = true;
      *pad = next;  // pending back to false
    });
  }
  // Keyboard or mouse used: show keys.
  if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
      !io.InputQueueCharacters.empty()) {
    pad_mode_ = false;
  }
  for (int k = ImGuiKey_Tab; k <= ImGuiKey_F12; ++k) {
    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) pad_mode_ = false;
  }
  if (!pad_->valid) return;
  const uint16_t b = pad_->buttons;
  const uint16_t pressed = b & ~pad_last_buttons_;
  pad_last_buttons_ = b;
  const int16_t kStick = 16000;
  const bool stick = std::abs(int(pad_->lx)) > kStick || std::abs(int(pad_->ly)) > kStick;
  if (pressed || stick) pad_mode_ = true;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  auto key = [&](ImGuiKey k, uint16_t mask) { io.AddKeyEvent(k, (b & mask) != 0); };
  key(ImGuiKey_GamepadDpadUp, 0x0001);
  key(ImGuiKey_GamepadDpadDown, 0x0002);
  key(ImGuiKey_GamepadDpadLeft, 0x0004);
  key(ImGuiKey_GamepadDpadRight, 0x0008);
  key(ImGuiKey_GamepadFaceDown, 0x1000);   // A: activate
  key(ImGuiKey_GamepadFaceRight, 0x2000);  // B: back
  key(ImGuiKey_GamepadFaceLeft, 0x4000);   // X
  key(ImGuiKey_GamepadFaceUp, 0x8000);     // Y
  auto axis = [&](ImGuiKey k, float v) { io.AddKeyAnalogEvent(k, v > 0.3f, std::max(0.0f, v)); };
  axis(ImGuiKey_GamepadLStickUp, pad_->ly / 32767.0f);
  axis(ImGuiKey_GamepadLStickDown, -pad_->ly / 32767.0f);
  axis(ImGuiKey_GamepadLStickLeft, -pad_->lx / 32767.0f);
  axis(ImGuiKey_GamepadLStickRight, pad_->lx / 32767.0f);
  constexpr int kTabCount = 10;
  if (pressed & 0x0100) tab_request_ = (tab_index_ + kTabCount - 1) % kTabCount;  // LB
  if (pressed & 0x0200) tab_request_ = (tab_index_ + 1) % kTabCount;              // RB
  if (pressed & 0x0010) pad_play_ = true;                                         // START
}

void LauncherDialog::DrawInputHints() {
  // Keycaps for whatever was used last: the controller's buttons (in the chosen
  // glyph set) or the keyboard's keys.
  const std::string set = dbz3::settings::InputGlyphs();
  const bool ps = set == "playstation", sw = set == "switch";
  struct Hint {
    const char* key;
    const char* text;
  };
  std::vector<Hint> hints;
  if (pad_mode_) {
    hints = {{ps ? "Cross" : (sw ? "B" : "A"), i18n::T("Elegir", "Select")},
             {ps ? "Circle" : (sw ? "A" : "B"), i18n::T("Atras", "Back")},
             {ps ? "L1 / R1" : (sw ? "L / R" : "LB / RB"), i18n::T("Pestanas", "Tabs")},
             {ps ? "OPTIONS" : (sw ? "+" : "START"), in_game_ ? i18n::T("Cerrar", "Close") : i18n::T("Jugar", "Play")}};
  } else {
    hints = {{"Tab", i18n::T("Moverse", "Move")},
             {"Ctrl + Tab", i18n::T("Pestanas", "Tabs")},
             {"Enter", in_game_ ? i18n::T("Cerrar", "Close") : i18n::T("Jugar", "Play")}};
  }
  const ui::Fonts& fonts = ui::GetFonts();
  ui::FontScope f(fonts.sm);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = ImGui::GetFontSize() + 6.0f;
  // Right-aligned on the summary row.
  float width = 0.0f;
  for (const auto& hint : hints) {
    width += ImGui::CalcTextSize(hint.key).x + 14.0f + 6.0f + ImGui::CalcTextSize(hint.text).x + 18.0f;
  }
  // On the button row, between Repair and PLAY (38 px tall buttons).
  ImGui::SameLine(0.0f, 28.0f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.y += (38.0f - h) * 0.5f;
  for (const auto& hint : hints) {
    const ImVec2 ks = ImGui::CalcTextSize(hint.key);
    const ImVec2 a(p.x, p.y), b(p.x + ks.x + 14.0f, p.y + h);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(pad_mode_ ? ui::kAccentDim : ui::kFrame), 5.0f);
    dl->AddRect(a, b, ImGui::GetColorU32(ui::kLine), 5.0f);
    dl->AddText(ImVec2(a.x + 7.0f, a.y + 3.0f), ImGui::GetColorU32(ui::kText), hint.key);
    p.x = b.x + 6.0f;
    dl->AddText(ImVec2(p.x, a.y + 3.0f), ImGui::GetColorU32(ui::kTextDim), hint.text);
    p.x += ImGui::CalcTextSize(hint.text).x + 18.0f;
  }
  ImGui::Dummy(ImVec2(width, 38.0f));
}

const LauncherDialog::PreviewImage* LauncherDialog::Preview(const std::filesystem::path& rgba) {
  std::error_code ec;
  const auto stamp = std::filesystem::last_write_time(rgba, ec);
  if (ec) return nullptr;
  auto& img = previews_[rgba.string()];
  if (img.tex && img.stamp == stamp) return &img;
  if (!g_preview_drawer) return nullptr;
  const std::string data = ReadSmallFile(rgba);
  if (data.size() < 8) return nullptr;
  uint32_t w = 0, h = 0;
  std::memcpy(&w, data.data(), 4);
  std::memcpy(&h, data.data() + 4, 4);
  if (w == 0 || h == 0 || w > 4096 || h > 4096 || data.size() < 8 + size_t(w) * h * 4) return nullptr;
  img.tex = g_preview_drawer->CreateTexture(w, h, rex::ui::ImmediateTextureFilter::kLinear, false,
                                            reinterpret_cast<const uint8_t*>(data.data() + 8));
  img.w = int(w);
  img.h = int(h);
  img.stamp = stamp;
  return img.tex ? &img : nullptr;
}

void LauncherDialog::PreviewWidget(const std::filesystem::path& rgba, float scale) {
  if (const PreviewImage* img = Preview(rgba)) {
    ImGui::Image(reinterpret_cast<ImTextureID>(img->tex.get()), ImVec2(img->w * scale, img->h * scale));
  } else {
    ImGui::Dummy(ImVec2(84 * scale, 84 * scale));
  }
}

std::map<std::string, int> LauncherDialog::AssignSlots() const {
  // Igual que roster_build.assign_ids: primero las plazas pedidas (en orden de
  // carpeta; si dos piden la misma gana la primera), luego la primera libre.
  std::map<std::string, int> out;
  std::vector<int> used;
  auto taken = [&](int id) { return std::find(used.begin(), used.end(), id) != used.end(); };
  for (const auto& cs : char_sources_) {
    if (!cs.enabled) continue;
    for (const auto& s : kFreeSlots) {
      if (s.id == cs.req_id && !taken(s.id)) {
        out[cs.folder] = s.id;
        used.push_back(s.id);
      }
    }
  }
  for (const auto& cs : char_sources_) {
    if (!cs.enabled || out.count(cs.folder)) continue;
    out[cs.folder] = -1;
    for (const auto& s : kFreeSlots) {
      if (!taken(s.id)) {
        out[cs.folder] = s.id;
        used.push_back(s.id);
        break;
      }
    }
  }
  return out;
}

void LauncherDialog::RunCharacterPreview(const std::string& mod, std::vector<std::string> args) {
  if (mod_pipeline_.IsRunning()) {      // se lanza al terminar el trabajo actual
    pending_preview_mod_ = mod;
    pending_preview_ = std::move(args);
    return;
  }
  mod_pipeline_.PreviewCharacter(mod, args);
}

void LauncherDialog::DrawSlotTable(const std::map<std::string, int>& slots) {
  ui::SectionTitle(i18n::T("1. Plazas", "1. Slots"));
  int used = 0;
  for (const auto& [folder, id] : slots) used += id >= 0 ? 1 : 0;
  ImGui::Text(i18n::T("%d de %d plazas ocupadas", "%d of %d slots taken"), used, kFreeSlotCount);
  Help(i18n::T(
      "Cada personaje nuevo necesita una plaza (un numero de personaje libre dentro del juego). "
      "Hay 26: las 6 de los personajes que se quedaron fuera del juego original (Guldo, Jeice, "
      "Burter, Zarbon, Dodoria y Androide 19) y 20 extra. Donde aparece en la rueda del select "
      "se elige aparte (\"Casilla tras\").",
      "Each new character needs a slot (a free character number inside the game). There are 26: "
      "the 6 characters cut from the original game (Guldo, Jeice, Burter, Zarbon, Dodoria and "
      "Android 19) and 20 extra ones. Where it shows up on the select wheel is chosen separately "
      "(\"Cell after\")."));
  ImGui::SameLine();
  if (ImGui::SmallButton(slots_expanded_ ? i18n::T("Ocultar lista", "Hide list")
                                         : i18n::T("Ver todas", "Show all"))) {
    slots_expanded_ = !slots_expanded_;
  }
  if (slots_expanded_ &&
      ImGui::BeginTable("##slots", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 190))) {
    ImGui::TableSetupColumn(i18n::T("Plaza", "Slot"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn(i18n::T("Estado", "Status"));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableHeadersRow();
    for (const auto& s : kFreeSlots) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d  %s", s.id, s.original);
      ImGui::TableNextColumn();
      const CharacterSource* owner = nullptr;
      for (const auto& cs : char_sources_) {
        auto it = slots.find(cs.folder);
        if (it != slots.end() && it->second == s.id) owner = &cs;
      }
      if (owner) {
        ImGui::TextColored(kGold, i18n::T("Ocupada: %s (carpeta %s)", "Taken: %s (folder %s)"),
                           owner->name.empty() ? owner->folder.c_str() : owner->name.c_str(),
                           owner->folder.c_str());
      } else {
        ImGui::TextColored(kOk, "%s", i18n::T("Libre", "Free"));
      }
      ImGui::TableNextColumn();
      if (owner) {
        ImGui::PushID(s.id);
        if (ImGui::SmallButton(i18n::T("Editar", "Edit"))) nc_selected_ = owner->folder;
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  // Conflictos: plaza pedida ocupada por otro, o sin plaza.
  for (const auto& cs : char_sources_) {
    if (!cs.enabled) continue;
    auto it = slots.find(cs.folder);
    const int got = it == slots.end() ? -1 : it->second;
    if (got < 0) {
      ImGui::TextColored(kError, i18n::T("%s no cabe: no quedan plazas (desactiva otro).",
                                         "%s does not fit: no slots left (disable another)."),
                         cs.folder.c_str());
    } else if (cs.req_id >= 0 && cs.req_id != got) {
      ImGui::TextColored(kWarn, i18n::T("%s pide la plaza %d (ocupada): usara la %d.",
                                        "%s asks for slot %d (taken): it will use %d."),
                         cs.folder.c_str(), cs.req_id, got);
    }
  }
}

void LauncherDialog::DrawCharacterEditor(CharacterSource& cs, const std::map<std::string, int>& slots) {
  const auto dir = ModPipeline::ModsDir() / cs.folder;
  const auto vista = dir / "ui" / "_vista";
  if (ed_loaded_for_ != cs.folder) {
    ed_loaded_for_ = cs.folder;
    std::snprintf(ed_name_buf_, sizeof(ed_name_buf_), "%s", cs.name.c_str());
    ed_import_buf_[0] = '\0';
    std::error_code ec;
    if (!std::filesystem::exists(vista / "icono.rgba", ec)) RunCharacterPreview(cs.folder, {});
  }
  ImGui::PushID(cs.folder.c_str());
  ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  ImGui::TextColored(kDragonOrange, "%s", cs.name.empty() ? cs.folder.c_str() : cs.name.c_str());
  ImGui::SameLine();
  if (cs.iw_port) {
    ImGui::TextDisabled(i18n::T("(carpeta %s, moveset propio)", "(folder %s, own moveset)"), cs.folder.c_str());
  } else {
    ImGui::TextDisabled(i18n::T("(carpeta %s, moveset de %s)", "(folder %s, %s moveset)"), cs.folder.c_str(),
                        DonorName(cs.donor));
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(i18n::T("Ver en Mods", "Show in Mods"))) {
    std::snprintf(mods_search_buf_, sizeof(mods_search_buf_), "%s", cs.folder.c_str());
    request_tab_ = 1;
  }

  // Vista previa: icono de la rueda, rotulo y retratos P1/P2 tal como se veran.
  ImGui::BeginGroup();
  PreviewWidget(vista / "icono.rgba", 1.25f);
  PreviewWidget(vista / "rotulo.rgba", 0.66f);
  ImGui::EndGroup();
  ImGui::SameLine();
  PreviewWidget(vista / "retrato_p1.rgba", 0.42f);
  ImGui::SameLine();
  PreviewWidget(vista / "retrato_p2.rgba", 0.42f);
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::TextDisabled("%s", i18n::T("Barra de vida", "Health bar"));
  PreviewWidget(vista / "hud.rgba", 0.75f);
  ImGui::EndGroup();
  Help(i18n::T("Cara pequena junto a la barra de vida en combate. Se genera del modelo, como las "
               "del juego (una por forma). Para usar tu propia imagen, pon ui/hud.png (256x128) "
               "en la carpeta del personaje.",
               "Small face next to the health bar in battle. Generated from the model, like the "
               "game's (one per form). To use your own image, put ui/hud.png (256x128) in the "
               "character folder."));
  if (!g_preview_drawer) {
    ImGui::TextDisabled("%s", i18n::T("(vista previa no disponible: abre la carpeta ui/_vista)",
                                      "(preview unavailable: open the ui/_vista folder)"));
  }

  const bool busy = mod_pipeline_.IsRunning();
  // Nombre, plaza y posicion en la rueda.
  ImGui::SetNextItemWidth(220);
  ImGui::InputText(i18n::T("Nombre", "Name"), ed_name_buf_, sizeof(ed_name_buf_));
  if (ImGui::IsItemDeactivatedAfterEdit() && ed_name_buf_[0] && cs.name != ed_name_buf_) {
    cs.name = ed_name_buf_;
    RunCharacterPreview(cs.folder, {"--nombre", cs.name, "--guardar", "--solo", "icono"});
  }
  ImGui::SameLine(0.0f, 18.0f);
  LiveBanner(ed_name_buf_);
  {
    auto it = slots.find(cs.folder);
    const int got = it == slots.end() ? -1 : it->second;
    char cur[96];
    if (cs.req_id < 0) {
      std::snprintf(cur, sizeof(cur), i18n::T("Automatica (%d)", "Automatic (%d)"), got);
    } else {
      std::snprintf(cur, sizeof(cur), "%d", cs.req_id);
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo(i18n::T("Plaza", "Slot"), cur)) {
      if (ImGui::Selectable(i18n::T("Automatica (primera libre)", "Automatic (first free)"), cs.req_id < 0)) {
        cs.req_id = -1;
        RunCharacterPreview(cs.folder, {"--id", "-1", "--guardar", "--solo", "icono"});
      }
      for (const auto& s : kFreeSlots) {
        std::string owner;
        for (const auto& o : char_sources_) {
          auto jt = slots.find(o.folder);
          if (o.folder != cs.folder && jt != slots.end() && jt->second == s.id) owner = o.folder;
        }
        char label[128];
        if (owner.empty()) {
          std::snprintf(label, sizeof(label), i18n::T("%d (%s) - libre", "%d (%s) - free"), s.id, s.original);
        } else {
          std::snprintf(label, sizeof(label), i18n::T("%d (%s) - ocupada por %s", "%d (%s) - taken by %s"),
                        s.id, s.original, owner.c_str());
        }
        if (ImGui::Selectable(label, cs.req_id == s.id)) {
          cs.req_id = s.id;
          RunCharacterPreview(cs.folder, {"--id", std::to_string(s.id), "--guardar", "--solo", "icono"});
        }
      }
      ImGui::EndCombo();
    }
  }
  int after_idx = cs.after >= 0 ? DonorIndex(cs.after) : -1;
  const int before = after_idx;
  DonorCombo(i18n::T("Casilla tras", "Cell after"), after_idx, true);
  if (after_idx != before) {
    cs.after = after_idx >= 0 ? kDonors[after_idx].id : -1;
    RunCharacterPreview(cs.folder, {"--despues-de", std::to_string(cs.after), "--guardar", "--solo", "icono"});
  }

  // Port con imagenes propias (p.ej. Infinite World): conservarlas o generarlas desde el
  // modelo 3D como el resto de Budokai 3 (cambia las dos fuentes de golpe).
  if (cs.orig_icon_src >= 0) {
    ui::SectionTitle(i18n::T("Imagenes originales del mod", "Mod's original images"));
    ImGui::TextWrapped("%s", i18n::T(
        "Este personaje trae sus propias imagenes (por ejemplo, las de un port de Infinite World). "
        "Puedes conservarlas o generar el icono y los retratos desde su modelo 3D, con el estilo "
        "del resto de Budokai 3.",
        "This character ships its own images (for example, from an Infinite World port). Keep them, "
        "or generate the icon and portraits from its 3D model in the style of the rest of Budokai 3."));
    const bool keep = cs.icon_src == cs.orig_icon_src && cs.portrait_src == cs.orig_portrait_src;
    const bool model = cs.icon_src == 0 && cs.portrait_src == 0;
    if (ImGui::RadioButton(i18n::T("Conservar las originales", "Keep the originals"), keep && !model) && !keep) {
      cs.icon_src = cs.orig_icon_src;
      cs.portrait_src = cs.orig_portrait_src;
      RunCharacterPreview(cs.folder, {"--imagenes", "originales", "--guardar"});
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(i18n::T("Modelo 3D (estilo Budokai 3)", "3D model (Budokai 3 style)"), model) && !model) {
      cs.icon_src = cs.portrait_src = 0;
      RunCharacterPreview(cs.folder, {"--imagenes", "modelo", "--guardar"});
    }
    if (!keep && !model) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", i18n::T("(personalizado abajo)", "(custom below)"));
    }
  }

  // Icono y retratos: fuente + ajuste fino (se guarda y se regenera al soltar).
  const char* sources[] = {i18n::T("Render del modelo", "Model render"),
                           i18n::T("Arte propio", "Own art"),
                           i18n::T("Imagen terminada", "Finished image")};
  struct Part {
    const char* title;
    const char* key;      // icono | retrato
    int* src;
    float* adj;
    float yaw_def;
  } parts[] = {{i18n::T("Icono de la rueda", "Wheel icon"), "icono", &cs.icon_src, cs.icon_adj, 3.0f},
               {i18n::T("Retratos P1/P2", "P1/P2 portraits"), "retrato", &cs.portrait_src, cs.portrait_adj,
                35.0f}};
  for (auto& p : parts) {
    ui::SectionTitle(p.title);
    ImGui::PushID(p.key);
    const std::string key = p.key;
    for (int i = 0; i < 3; ++i) {
      if (i) ImGui::SameLine();
      if (ImGui::RadioButton(sources[i], *p.src == i) && *p.src != i) {
        *p.src = i;
        RunCharacterPreview(cs.folder, {"--" + key + "-fuente", SourceName(i), "--guardar", "--solo", key});
      }
    }
    bool edited = false;
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat(i18n::T("Zoom", "Zoom"), &p.adj[0], 0.5f, 1.8f, "%.2f");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::SliderFloat(i18n::T("Horizontal", "Horizontal"), &p.adj[1], -0.4f, 0.4f, "%.2f");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::SliderFloat(i18n::T("Vertical", "Vertical"), &p.adj[2], -0.4f, 0.4f, "%.2f");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    if (*p.src == 0) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(130);
      ImGui::SliderFloat(i18n::T("Giro", "Turn"), &p.adj[3], -80.0f, 80.0f, "%.0f");
      edited |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(i18n::T("Restablecer", "Reset"))) {
      p.adj[0] = 1.0f;
      p.adj[1] = p.adj[2] = 0.0f;
      p.adj[3] = p.yaw_def;
      edited = true;
    }
    if (edited) {
      float a[4] = {p.adj[0], p.adj[1], p.adj[2], p.adj[3]};
      RunCharacterPreview(cs.folder, {"--" + key + "-ajuste", AdjArg(a), "--guardar", "--solo", key});
    }
    ImGui::PopID();
  }

  // Capsulas (habilidades): las propias del personaje o las del donante.
  ui::SectionTitle(i18n::T("Capsulas (habilidades)", "Capsules (skills)"));
  if (cs.capsules.empty()) {
    ImGui::TextWrapped(i18n::T(
        "Usa las capsulas de su donante (%s): mismos ataques, con sus nombres. Anade capsulas "
        "propias para darle nombres y una transformacion con su propia capsula.",
        "Uses its donor's capsules (%s): same attacks, with their names. Add own capsules to "
        "give it its own names and a transformation with its own capsule."),
        DonorName(cs.donor));
  } else {
    ImGui::TextWrapped("%s", cs.iw_port
        ? i18n::T("Port con moveset propio: sus especiales y su definitiva se ligan, en este orden, "
                  "a estas capsulas. Su modo hiper (LT / L2) y el Dragon Rush funcionan como en "
                  "Budokai 3.",
                  "Port with its own moveset: its specials and ultimate are linked, in this order, "
                  "to these capsules. Its hyper mode (LT / L2) and Dragon Rush work as in Budokai 3.")
        : i18n::T("Capsulas propias: salen con su nombre en combate y en la lista de habilidades. "
                  "Las especiales y definitivas sustituyen, en orden, a las del donante; una "
                  "transformacion lleva su propia capsula.",
                  "Own capsules: shown with their name in battle and in the skill list. Specials "
                  "and ultimates replace the donor's in order; a transformation gets its own capsule."));
  }
  if (cs.iw_port) {
    // port: traer las capsulas que pide su moveset (nombres del juego de origen si se conocen)
    const char* games[] = {i18n::T("Detectar", "Detect"), "Infinite World", "Budokai 1", "Budokai 2",
                           "Budokai 3"};
    const char* game_ids[] = {"auto", "iw", "b1", "b2", "b3"};
    ImGui::SetNextItemWidth(170);
    ImGui::Combo("##cap_game", &ed_cap_game_, games, 5);
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(i18n::T("Traer las capsulas de su juego", "Bring capsules from its game"))) {
      mod_pipeline_.EditCapsules(cs.folder, {"--importar", game_ids[ed_cap_game_]});
    }
    ImGui::EndDisabled();
    Help(i18n::T(
        "Lee el moveset del port y crea una capsula por cada ataque que la pida (especiales, "
        "definitiva, transformaciones), con el nombre de su juego cuando se conoce (Infinite "
        "World: lista de capsulas de \"modding resources\"). Un port de Budokai 3 se queda con "
        "las capsulas originales. Si ya tenia capsulas, se guarda una copia "
        "(personaje.toml.antes_de_importar).",
        "Reads the port's moveset and creates one capsule for each attack that needs one "
        "(specials, ultimate, transformations), named as in its game when known (Infinite "
        "World: capsule list in \"modding resources\"). A Budokai 3 port keeps the original "
        "capsules. Existing capsules are backed up (personaje.toml.antes_de_importar)."));
  }
  for (size_t i = 0; i < cs.capsules.size(); ++i) {
    const auto& cap = cs.capsules[i];
    ImGui::PushID(int(i));
    ImGui::BulletText("%s", cap.name.c_str());
    ImGui::SameLine();
    if (cap.kind == "transformacion") {
      ImGui::TextDisabled(i18n::T("(%s, forma %d)", "(%s, form %d)"), CapsuleKindLabel(cap.kind), cap.form);
    } else {
      ImGui::TextDisabled("(%s)", CapsuleKindLabel(cap.kind));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || i == 0);
    if (ImGui::SmallButton(i18n::T("Subir", "Up"))) {
      mod_pipeline_.EditCapsules(cs.folder, {"--subir", std::to_string(i)});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::SmallButton(i18n::T("Quitar", "Remove"))) {
      ImGui::OpenPopup("###confirm_delcap");
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T("Quitar esta capsula del personaje", "Remove this capsule from the character"));
    }
    if (ConfirmModal((std::string(i18n::T("Quitar", "Remove")) + "###confirm_delcap").c_str(),
                     i18n::T("Quitar esta capsula del personaje?",
                             "Remove this capsule from the character?"),
                     i18n::T("Si, quitar", "Yes, remove"))) {
      mod_pipeline_.EditCapsules(cs.folder, {"--quitar", std::to_string(i)});
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  {
    const char* kinds[] = {i18n::T("Especial", "Special"), i18n::T("Definitiva", "Ultimate"),
                           i18n::T("Transformacion", "Transformation")};
    const char* kind_ids[] = {"especial", "definitiva", "transformacion"};
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##cap_name", i18n::T("Nombre de la capsula", "Capsule name"),
                             ed_cap_name_buf_, sizeof(ed_cap_name_buf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##cap_kind", &ed_cap_kind_, kinds, 3);
    if (ed_cap_kind_ == 2) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90);
      ImGui::InputInt(i18n::T("Forma", "Form"), &ed_cap_form_);
      ed_cap_form_ = std::clamp(ed_cap_form_, 1, 7);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || ed_cap_name_buf_[0] == '\0');
    if (ImGui::Button(i18n::T("Anadir capsula", "Add capsule"))) {
      std::vector<std::string> a = {"--anadir", ed_cap_name_buf_, kind_ids[ed_cap_kind_]};
      if (ed_cap_kind_ == 2) a.push_back(std::to_string(ed_cap_form_));
      mod_pipeline_.EditCapsules(cs.folder, a);
      ed_cap_name_buf_[0] = '\0';
    }
    ImGui::EndDisabled();
    Help(i18n::T(
        "Especial: ataque con ki (->E, <-E...). Definitiva: el golpe final en modo hiper. "
        "Transformacion: la capsula que necesita para transformarse a esa forma (1 = la "
        "primera transformacion). Los cambios se aplican al pulsar PLAY o \"Reconstruir ahora\".",
        "Special: ki attack (->E, <-E...). Ultimate: the finisher in hyper mode. "
        "Transformation: the capsule it needs to transform into that form (1 = the first "
        "transformation). Changes apply on PLAY or \"Rebuild now\"."));
  }

  // Importar arte propio (PNG/JPG; mejor con fondo transparente).
  ui::SectionTitle(i18n::T("Arte propio", "Own art"));
  ImGui::SetNextItemWidth(-330);
  ImGui::InputText("##import", ed_import_buf_, sizeof(ed_import_buf_));
  ImGui::SameLine();
  if (ImGui::Button("...", ImVec2(30, 0))) {
    std::string picked;
    if (PickFile(picked, i18n::T("Imagen (PNG/JPG)", "Image (PNG/JPG)"), "*.png;*.jpg;*.jpeg", dir.string())) {
      std::snprintf(ed_import_buf_, sizeof(ed_import_buf_), "%s", picked.c_str());
    }
  }
  ImGui::BeginDisabled(ed_import_buf_[0] == '\0');
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Como cara", "As face"), ImVec2(90, 0))) {
    cs.icon_src = 1;
    RunCharacterPreview(cs.folder, {"--cara", ed_import_buf_, "--guardar", "--solo", "icono"});
  }
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Como icono", "As icon"), ImVec2(90, 0))) {
    cs.icon_src = 2;
    RunCharacterPreview(cs.folder, {"--icono", ed_import_buf_, "--guardar", "--solo", "icono"});
  }
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Como retrato", "As portrait"), ImVec2(100, 0))) {
    cs.portrait_src = 1;
    RunCharacterPreview(cs.folder, {"--retrato", ed_import_buf_, "--guardar", "--solo", "retrato"});
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", i18n::T(
      "Cara: se encaja en el circulo con el fondo y el aro oficiales. Icono: se usa tal cual. "
      "Retrato: se encaja y se le pone el cielo azul (P1) / rojo (P2).",
      "Face: fitted into the circle with the official background and ring. Icon: used as is. "
      "Portrait: fitted, with the blue (P1) / red (P2) sky added."));

  ImGui::BeginDisabled(busy && pending_preview_mod_.empty());
  if (ImGui::Button(i18n::T("Regenerar vista previa", "Refresh preview"), ImVec2(200, 0))) {
    RunCharacterPreview(cs.folder, {});
  }
  ImGui::EndDisabled();
  ImGui::EndChild();
  ImGui::PopID();
}

namespace {

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  return out;
}

// Short, translated description of each import source (the script's own notes are Spanish).
const char* ImportSourceNote(const std::string& id) {
  if (id == "b1") {
    return i18n::T("Modelo, golpes, combos y gritos del Budokai 1 original; definitiva de su equivalente en Budokai 3.",
                   "Model, moves, combos and yells from the original Budokai 1; ultimate from its Budokai 3 counterpart.");
  }
  if (id == "b2") {
    return i18n::T("Modelo de Budokai 2; golpes, tecnicas y definitiva de su equivalente en Budokai 3.",
                   "Budokai 2 model; moves, techniques and ultimate from its Budokai 3 counterpart.");
  }
  if (id == "b3") {
    return i18n::T("Modelos de la comunidad (.amb / .amo + .amt) de 'modding resources'.",
                   "Community models (.amb / .amo + .amt) from 'modding resources'.");
  }
  if (id == "iw") {
    return i18n::T("Modelo, voces, gritos, golpes, tecnicas y definitiva de Infinite World (con modo hiper).",
                   "Infinite World model, voices, yells, moves, techniques and ultimate (with hyper mode).");
  }
  if (id == "sdbh") {
    return i18n::T("Modelos HD de Heroes (boca, 7 caras, rampas); golpes de Shin Budokai o del donante.",
                   "Heroes HD models (mouth, 7 faces, ramps); Shin Budokai or donor moves.");
  }
  return i18n::T("Modelos de PSP con sus formas; golpes, combos y camara de Shin Budokai (o del donante).",
                 "PSP models with their forms; Shin Budokai moves, combos and camera (or the donor's).");
}

std::string ModSlug(const std::string& source, const std::string& name) {
  std::string out = "imp_" + source + "_";
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!out.empty() && out.back() != '_') {
      out += '_';
    }
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out;
}

}  // namespace

void LauncherDialog::ParseImporterOutput() {
  const std::string out = mod_pipeline_.Output();
  const int what = imp_pending_;
  imp_pending_ = 0;
  if (what == 1) imp_sources_.clear();
  if (what == 2) imp_entries_.clear();
  std::istringstream in(out);
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto cols = SplitTabs(line);
    if (what == 1 && cols.size() >= 5 && cols[0] == "fuente") {
      imp_sources_.push_back({cols[1], cols[2], cols[3], cols[4]});
    } else if (what == 2 && cols.size() >= 8 && cols[0] == "personaje") {
      ImportEntry e;
      e.key = cols[1];
      e.name = cols[2];
      e.donor = cols[3].empty() ? -1 : std::atoi(cols[3].c_str());
      e.kind = cols[4];
      e.count = std::atoi(cols[5].c_str());
      e.port = cols[6] == "1";
      e.suggested = cols[7];
      imp_entries_.push_back(e);
    } else if (!line.empty()) {
      last = line;
    }
  }
  if (what == 3) {
    const bool ok = out.find("listo: ") != std::string::npos;
    imp_message_ = ok ? std::string(i18n::T("Importado. Ya aparece abajo en \"Instalados\"; se anade al "
                                            "juego al pulsar JUGAR.",
                                            "Imported. It is listed below under \"Installed\" and is "
                                            "added to the game when you press PLAY."))
                      : std::string(i18n::T("No se pudo importar. Abre 'Detalles tecnicos' para ver el motivo.",
                                            "Import failed. Open 'Technical details' to see why."));
  } else {
    // queries leave no log behind; a failed one says why
    if ((what == 1 && imp_sources_.empty()) || (what == 2 && imp_entries_.empty())) {
      imp_message_ = last.empty() ? std::string(i18n::T("No se encontro nada.", "Nothing found.")) : last;
    } else {
      imp_message_.clear();
    }
    mod_pipeline_.ClearOutput();
  }
}

void LauncherDialog::DrawImporter() {
  ui::BeginCard("##importer", ICON_DOWNLOAD,
                i18n::T("Importar personaje de otro juego", "Import a character from another game"),
                i18n::T("Elige el juego, despues el personaje, y pulsa Importar.",
                        "Pick the game, then the character, and press Import."));
  if (!ModPipeline::ToolsInstalled()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui::kWarn, "%s  %s", ICON_WARN, i18n::T(
        "Para importar o crear personajes instala el \"Kit de modding\" (DBZ3HD-Kit-Modding.zip): "
        "copialo junto a dbz3.exe y sigue su LEEME_KIT.txt. Los packs de personajes ya hechos "
        "funcionan sin el kit.",
        "To import or create characters install the \"Modding kit\" (DBZ3HD-Kit-Modding.zip): "
        "copy it next to dbz3.exe and follow its LEEME_KIT.txt. Ready-made character packs work "
        "without the kit."));
    ImGui::PopTextWrapPos();
    if (ImGui::Button(i18n::T("Abrir la carpeta del juego", "Open the game folder"))) {
      std::system(("explorer \"" + ModPipeline::ModsDir().parent_path().string() + "\"").c_str());
    }
    ui::EndCard();
    return;
  }
  if (!imp_loaded_ && !mod_pipeline_.IsRunning()) {
    imp_loaded_ = true;
    imp_pending_ = 1;
    mod_pipeline_.ImporterQuery({"fuentes"});
  }
  if (imp_pending_ != 0 && !mod_pipeline_.IsRunning()) {
    ParseImporterOutput();
  }
  const ui::Fonts& fonts = ui::GetFonts();

  // 1) Game tiles: found / missing / in development.
  if (imp_sources_.empty()) {
    ImGui::TextDisabled("%s", imp_pending_ == 1 ? i18n::T("Buscando juegos...", "Looking for games...")
                                                : imp_message_.c_str());
  }
  const float avail = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const int per_row = avail > 1150.0f ? 4 : avail > 760.0f ? 3 : 2;
  const float tile_w = (avail - gap * float(per_row - 1)) / float(per_row);
  const float tile_h = 58.0f;
  for (size_t i = 0; i < imp_sources_.size(); ++i) {
    const auto& src = imp_sources_[i];
    const bool ready = src.state == "listo";
    const bool dev = src.state == "desarrollo";
    const bool sel = src.id == imp_source_;
    if (i % per_row != 0) ImGui::SameLine();
    ImGui::PushID(src.id.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tile_w, tile_h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + tile_w, p.y + tile_h);
    dl->AddRectFilled(p, q, ImGui::GetColorU32(sel ? ui::kAccentSoft : hovered ? ui::kFrameHover : ui::kFrame),
                      8.0f);
    dl->AddRect(p, q, ImGui::GetColorU32(sel ? ui::kAccent : ui::kLine), 8.0f, 0, sel ? 2.0f : 1.0f);
    {
      ui::FontScope f(fonts.bold);
      const char* title = src.id == "b3" ? i18n::T("Budokai 3 (mods de la comunidad)", "Budokai 3 (community mods)")
                                         : src.name.c_str();
      dl->AddText(ImVec2(p.x + 12.0f, p.y + 8.0f), ImGui::GetColorU32(ready ? ui::kText : ui::kTextDim), title);
    }
    {
      ui::FontScope f(fonts.sm);
      const char* status = ready ? i18n::T(ICON_OK "  Listo para importar", ICON_OK "  Ready to import")
                           : dev ? i18n::T(ICON_WARN "  En desarrollo", ICON_WARN "  In development")
                                 : i18n::T(ICON_ERROR "  No encontrado (copialo en ps2_games)", ICON_ERROR "  Not found (copy it into ps2_games)");
      dl->AddText(ImVec2(p.x + 12.0f, p.y + tile_h - ImGui::GetFontSize() - 9.0f),
                  ImGui::GetColorU32(ready ? ui::kOk : dev ? ui::kWarn : ui::kTextDim), status);
    }
    if (hovered) {
      ImGui::SetTooltip("%s%s%s", ImportSourceNote(src.id), src.path.empty() ? "" : "\n",
                        src.path.c_str());
    }
    if (clicked && !mod_pipeline_.IsRunning()) {
      if (ready) {
        imp_source_ = src.id;
        imp_entries_.clear();
        imp_selected_ = -1;
        imp_search_[0] = '\0';
        imp_message_.clear();
        imp_pending_ = 2;
        mod_pipeline_.ImporterQuery({"lista", src.id});
      } else {
        imp_source_.clear();
        imp_message_ = ImportSourceNote(src.id);
      }
    }
    ImGui::PopID();
  }

  bool any_missing = false;
  for (const auto& src : imp_sources_) any_missing |= src.state != "listo" && src.state != "desarrollo";
  if (any_missing && !mod_pipeline_.IsRunning()) {
    if (ImGui::SmallButton(i18n::T("Buscar de nuevo", "Search again"))) {
      imp_loaded_ = false;          // vuelve a mirar ps2_games (un juego recien copiado)
      imp_source_.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", i18n::T("Copia el juego (ISO o carpeta) en la carpeta ps2_games y pulsa Buscar de nuevo.",
                                      "Copy the game (ISO or folder) into the ps2_games folder, then press Search again."));
  }

  // 2) Characters of the chosen game.
  if (!imp_source_.empty()) {
    ImGui::Dummy(ImVec2(0, 4));
    if (imp_pending_ == 2) {
      ImGui::TextDisabled("%s", i18n::T("Leyendo el juego... (la primera vez puede tardar un poco)",
                                        "Reading the game... (the first time can take a moment)"));
    } else {
      ImGui::SetNextItemWidth(std::min(360.0f, avail));
      ImGui::InputTextWithHint("##imp_search",
                               i18n::T(ICON_SEARCH "  Buscar personaje...", ICON_SEARCH "  Search character..."),
                               imp_search_, sizeof(imp_search_));
      std::string needle = imp_search_;
      for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      std::vector<int> shown;
      for (int i = 0; i < int(imp_entries_.size()); ++i) {
        std::string low = imp_entries_[i].name;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (needle.empty() || low.find(needle) != std::string::npos) shown.push_back(i);
      }
      ImGui::SameLine();
      ImGui::TextDisabled(i18n::T("%d personajes", "%d characters"), int(shown.size()));
      const float row_h = 24.0f + ImGui::GetStyle().ItemSpacing.y;
      const float list_h = std::clamp(row_h * float(shown.size()) + 12.0f, 40.0f, 230.0f);
      ImGui::BeginChild("##imp_list", ImVec2(0, list_h), ImGuiChildFlags_Borders);
      for (const int i : shown) {
        const auto& e = imp_entries_[i];
        ImGui::PushID(i);
        if (ImGui::Selectable("##row", imp_selected_ == i, 0, ImVec2(0, 24.0f))) {
          imp_selected_ = i;
          std::snprintf(imp_name_, sizeof(imp_name_), "%s",
                        e.suggested.empty() ? e.name.c_str() : e.suggested.c_str());
          imp_donor_idx_ = -1;
          imp_message_.clear();
        }
        ImGui::SameLine(10.0f);
        {
          ui::FontScope f(fonts.bold);
          ImGui::TextUnformatted(e.name.c_str());
        }
        ImGui::SameLine(std::max(300.0f, ImGui::GetWindowWidth() * 0.40f));
        char note[192];
        if (e.kind == "b1") {
          std::snprintf(note, sizeof(note), i18n::T("%d modelos  -  golpes, combos y gritos de B1",
                                                    "%d models  -  B1 moves, combos and yells"), e.count);
        } else if (e.kind == "trajes") {
          std::snprintf(note, sizeof(note), e.count == 1 ? i18n::T("1 traje", "1 costume")
                                                         : i18n::T("%d trajes", "%d costumes"), e.count);
        } else {
          std::snprintf(note, sizeof(note), e.count == 1 ? i18n::T("1 modelo", "1 model")
                                                         : i18n::T("%d formas", "%d forms"), e.count);
        }
        std::string line = note;
        if (e.kind == "sb") line += i18n::T("  -  golpes de Shin Budokai", "  -  Shin Budokai moves");
        if (e.port) {
          line += i18n::T("  -  golpes propios (port de la comunidad)", "  -  own moves (community port)");
        } else if (e.donor >= 0) {
          line += std::string(i18n::T("  -  base: ", "  -  base: ")) + DonorName(e.donor);
        }
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::PopID();
      }
      ImGui::EndChild();
    }
  }

  // 3) Name, donor and Import.
  if (!imp_source_.empty() && imp_pending_ != 2 && imp_selected_ >= 0 &&
      imp_selected_ < int(imp_entries_.size())) {
    const auto& e = imp_entries_[imp_selected_];
    ImGui::Dummy(ImVec2(0, 4));
    ui::RowLabel(i18n::T("Nombre en el select", "Select-screen name"),
                 i18n::T("Asi se vera su rotulo en la rueda.", "How its banner will look on the wheel."));
    const float input_x = ImGui::GetCursorPosX();
    ImGui::InputText("##imp_name", imp_name_, sizeof(imp_name_));
    if (imp_name_[0]) {
      ImGui::SetCursorPosX(input_x);
      LiveBanner(imp_name_);
    }
    ui::RowLabel(i18n::T("Golpes base (donante)", "Base moves (donor)"),
                 i18n::T("El personaje del juego del que toma lo que le falte (agarres, modo hiper...).",
                         "The game character it takes whatever it lacks from (throws, hyper mode...)."));
    {
      std::string auto_label = i18n::T("Automatico", "Automatic");
      if (e.donor >= 0) auto_label += std::string(" (") + DonorName(e.donor) + ")";
      const char* preview = imp_donor_idx_ >= 0 ? kDonors[imp_donor_idx_].Name() : auto_label.c_str();
      if (ImGui::BeginCombo("##imp_donor", preview)) {
        if (ImGui::Selectable(auto_label.c_str(), imp_donor_idx_ < 0)) imp_donor_idx_ = -1;
        for (int i = 0; i < kDonorCount; ++i) {
          if (ImGui::Selectable(kDonors[i].Name(), imp_donor_idx_ == i)) imp_donor_idx_ = i;
        }
        ImGui::EndCombo();
      }
    }
    ImGui::Dummy(ImVec2(0, 4));
    const bool busy = mod_pipeline_.IsRunning();
    ImGui::BeginDisabled(busy || imp_name_[0] == '\0');
    ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::kAccent);
    const bool do_import = ui::IconButton(ICON_DOWNLOAD, i18n::T("Importar", "Import"), ImVec2(220.0f, 40.0f));
    ImGui::PopStyleColor(3);
    if (do_import) {
      // unique mod folder from game + name
      std::string mod = ModSlug(imp_source_, imp_name_);
      std::error_code ec;
      const auto dir = ModPipeline::ModsDir();
      std::string unique = mod;
      for (int n = 2; std::filesystem::exists(dir / unique, ec); ++n) unique = mod + "_" + std::to_string(n);
      ModPipeline::ImportRequest r;
      r.source = imp_source_;
      r.key = e.key;
      r.mod = unique;
      r.name = imp_name_;
      r.donor = imp_donor_idx_ >= 0 ? kDonors[imp_donor_idx_].id : -1;
      imp_pending_ = 3;
      imp_message_.clear();
      mod_pipeline_.ImportCharacter(r);
      nc_selected_ = unique;
    }
    ImGui::EndDisabled();
    if (imp_pending_ == 3) {
      ImGui::SameLine();
      ImGui::TextColored(ui::kWarn, "%s", i18n::T("Importando... (un minuto como mucho)",
                                                  "Importing... (a minute at most)"));
    }
  }
  if (!imp_message_.empty()) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushTextWrapPos(0.0f);
    const bool imp_ok = imp_message_.rfind(i18n::T("Importado", "Imported"), 0) == 0;
    ImGui::TextColored(imp_ok ? ui::kOk : ui::kWarn, "%s", imp_message_.c_str());
    ImGui::PopTextWrapPos();
    if (!imp_ok && imp_pending_ == 0 && !mod_pipeline_.IsRunning() && !mod_pipeline_.Output().empty()) {
      if (ImGui::CollapsingHeader(i18n::T("Detalles tecnicos##imp", "Technical details##imp"))) {
        const std::string out = mod_pipeline_.Output();
        std::memcpy(output_buf_, out.c_str(), std::min(out.size(), sizeof(output_buf_) - 1));
        output_buf_[std::min(out.size(), sizeof(output_buf_) - 1)] = '\0';
        ImGui::InputTextMultiline("##imp_out", output_buf_, sizeof(output_buf_), ImVec2(-1.0f, 140.0f),
                                  ImGuiInputTextFlags_ReadOnly);
      }
    }
  }
  ui::EndCard();
}

void LauncherDialog::DrawNewCharactersTab() {
  ImGui::BeginChild("##new_chars", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  // Plain-language notice for non-technical users: what this tab does, that it
  // is optional and experimental, and how to undo it.
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.20f, 0.05f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::kWarn);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("##chars_notice", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    {
      ui::FontScope f(ui::GetFonts().bold);
      ImGui::TextColored(ui::kWarn, "%s  %s", ICON_WARN,
                         i18n::T("Seccion avanzada y experimental (opcional)",
                                 "Advanced, experimental section (optional)"));
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(i18n::T(
        "Aqui se anaden personajes NUEVOS al juego (de Budokai 1, 2, Infinite World o de la "
        "comunidad). No hace falta tocar nada de esto para jugar. Los personajes importados son "
        "experimentales: alguna tecnica, animacion o efecto puede no ser perfecto.",
        "This adds NEW characters to the game (from Budokai 1, 2, Infinite World or the "
        "community). You don't need anything here to play. Imported characters are experimental: "
        "some technique, animation or effect may not be perfect."));
    ImGui::TextColored(ui::kTextDim, "%s", i18n::T(
        "Si algo va mal: desmarca el personaje en \"Instalados\" (vuelve a como estaba) y pulsa "
        "JUGAR. Tus partidas guardadas no se tocan.",
        "If something goes wrong: untick the character under \"Installed\" (back to how it was) "
        "and press PLAY. Your saves are never touched."));
    // v1.4.2.2: los personajes nuevos funcionan tambien con el ejecutable europeo (EU/PAL).
    // v1.4.2.1: la causa mas comun de "no salen los personajes": jugar desde el ISO.
    if (dbz3::settings::IsIsoMode()) {
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::TextColored(ui::kError, "%s  %s", ICON_WARN, i18n::T(
          "Estas jugando desde el ISO: asi los mods y los personajes nuevos NO se cargan. Extrae el "
          "ISO a una carpeta y eligela en Inicio > 'Carpeta extraida'.",
          "You are playing from the ISO: mods and new characters are NOT loaded that way. Extract "
          "the ISO to a folder and pick it in Home > 'Extracted folder'."));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 4));
  }
  ui::SectionTitle(i18n::T("Personajes nuevos (casillas extra en el select)",
                               "New characters (extra select cells)"));
  ImGui::TextWrapped("%s", i18n::T(
      "Anade personajes en casillas NUEVAS de la rueda del select, sin sustituir a "
      "ninguno. Cada uno es un mod con su modelo; el icono, el rotulo y los retratos se "
      "generan solos con el estilo del juego. Usa el moveset de un personaje \"donante\" "
      "(o el suyo propio si es un port) y puede llevar sus propias capsulas.",
      "Adds characters in NEW select-wheel cells without replacing anyone. Each one is a "
      "mod with its model; the icon, name banner and portraits are generated in the game's "
      "style. It uses a \"donor\" character's moveset (or its own if it is a port) and can "
      "have its own capsules."));
  ImGui::TextDisabled("%s", i18n::T(
      "Pasos: 1) mira las plazas libres, 2) activa o elige un personaje para editarlo, "
      "3) crea uno nuevo abajo. Todo se aplica solo al pulsar PLAY.",
      "Steps: 1) check the free slots, 2) enable or pick a character to edit it, 3) create "
      "a new one below. Everything applies automatically on PLAY."));
  if (dbz3::settings::IsIsoMode()) {
    ImGui::TextColored(kGold, "%s", i18n::T("Modo disco (ISO): los mods no se aplican.",
                                            "Disc mode (ISO): mods do not apply."));
  }
  if (!rex::cvar::Query<bool>("dbz3_afs_append")) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextWrapped("%s", i18n::T(
        "Tu copia del juego no puede cargar personajes nuevos (falta un archivo actualizado): "
        "vuelve a descargar el juego completo. Los demas mods funcionan.",
        "Your game copy can't load new characters (an updated file is missing): download the "
        "full game again. Other mods still work."));
    ImGui::PopStyleColor();
  }

  DrawImporter();

  // Al terminar un trabajo: vista previa en cola y relectura de los mods fuente.
  if (!mod_pipeline_.IsRunning() && !pending_preview_mod_.empty()) {
    const std::string mod = std::move(pending_preview_mod_);
    pending_preview_mod_.clear();
    mod_pipeline_.PreviewCharacter(mod, pending_preview_);
    pending_preview_.clear();
  }
  if (!char_sources_loaded_ || mod_pipeline_.Generation() != char_sources_gen_) {
    char_sources_loaded_ = true;
    char_sources_gen_ = mod_pipeline_.Generation();
    char_sources_.clear();
    std::error_code ec;
    const auto dir = ModPipeline::ModsDir();
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      const auto toml = e.path() / "personaje.toml";
      if (!e.is_directory(ec) || !std::filesystem::exists(toml, ec)) continue;
      const std::string text = ReadSmallFile(toml);
      CharacterSource cs;
      cs.folder = e.path().filename().string();
      cs.name = TomlValue(text, "nombre");
      cs.donor = TomlInt(text, "donante", -1);
      cs.req_id = TomlInt(text, "id", -1);
      cs.after = TomlInt(text, "despues_de", -1);
      const bool has_icon = std::filesystem::exists(e.path() / "ui" / "icono.png", ec);
      const bool has_por = std::filesystem::exists(e.path() / "ui" / "retrato_p1.png", ec);
      // mismo criterio que original_sources() / image_source() de roster_build.py: sin
      // fuente elegida se usan las imagenes propias del mod y, si no trae, el modelo
      const bool has_face = std::filesystem::exists(e.path() / "ui" / "cara.png", ec);
      const bool has_art = std::filesystem::exists(e.path() / "ui" / "retrato.png", ec);
      if (has_icon || has_por) {  // solo imagenes terminadas (cara/retrato.png = reserva)
        cs.orig_icon_src = has_icon ? 2 : has_face ? 1 : 0;
        cs.orig_portrait_src = has_por ? 2 : has_art ? 1 : 0;
      }
      const std::string isrc = TomlValue(text, "icono_fuente");
      const std::string psrc = TomlValue(text, "retrato_fuente");
      cs.icon_src = isrc.empty() ? std::max(cs.orig_icon_src, 0) : SourceIndex(isrc);
      cs.portrait_src = psrc.empty() ? std::max(cs.orig_portrait_src, 0) : SourceIndex(psrc);
      TomlFloats(text, "icono_ajuste", cs.icon_adj);
      TomlFloats(text, "retrato_ajuste", cs.portrait_adj);
      cs.enabled = !std::filesystem::exists(e.path() / ".disabled", ec);
      for (const auto& cap : TomlCapsules(text)) {
        cs.capsules.push_back({cap[0], cap[1], std::max(1, std::atoi(cap[2].c_str()))});
      }
      cs.iw_port = !TomlValue(text, "camara").empty();
      char_sources_.push_back(cs);
    }
    // mismo orden que roster_build.py (orden alfabetico de carpeta)
    std::sort(char_sources_.begin(), char_sources_.end(),
              [](const CharacterSource& a, const CharacterSource& b) { return a.folder < b.folder; });
    roster_manifest_ = ReadSmallFile(dir / "_roster" / "manifest.txt");
  }
  const auto slots = AssignSlots();
  DrawSlotTable(slots);

  ui::SectionTitle(i18n::T("2. Instalados (pulsa uno para editarlo)", "2. Installed (click one to edit it)"));
  if (char_sources_.empty()) {
    ImGui::TextDisabled("%s", i18n::T("Ninguno todavia.", "None yet."));
  }
  for (auto& cs : char_sources_) {
    ImGui::PushID(cs.folder.c_str());
    bool on = cs.enabled;
    if (ImGui::Checkbox("##on", &on)) {
      dbz3::SetModEnabled(cs.folder, on);
      cs.enabled = on;
      mods_loaded_ = false;
    }
    ImGui::SameLine();
    PreviewWidget(ModPipeline::ModsDir() / cs.folder / "ui" / "_vista" / "icono.rgba", 0.30f);
    ImGui::SameLine();
    auto it = slots.find(cs.folder);
    char label[160];
    if (!cs.enabled) {
      std::snprintf(label, sizeof(label), "%s", cs.name.empty() ? cs.folder.c_str() : cs.name.c_str());
    } else if (it != slots.end() && it->second >= 0) {
      std::snprintf(label, sizeof(label), i18n::T("%s  (plaza %d)", "%s  (slot %d)"),
                    cs.name.empty() ? cs.folder.c_str() : cs.name.c_str(), it->second);
    } else {
      std::snprintf(label, sizeof(label), i18n::T("%s  (sin plaza)", "%s  (no slot)"),
                    cs.name.empty() ? cs.folder.c_str() : cs.name.c_str());
    }
    if (ImGui::Selectable(label, nc_selected_ == cs.folder, 0, ImVec2(0, 25))) {
      nc_selected_ = nc_selected_ == cs.folder ? std::string() : cs.folder;
    }
    ImGui::PopID();
  }
  for (auto& cs : char_sources_) {
    if (cs.folder == nc_selected_) DrawCharacterEditor(cs, slots);
  }
  ImGui::BeginDisabled(mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Reconstruir ahora", "Rebuild now"), ImVec2(180, 0))) {
    mod_pipeline_.BuildRoster(true);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", i18n::T("(tambien se hace solo al pulsar PLAY)",
                                    "(also done automatically on PLAY)"));
  if (!roster_manifest_.empty() &&
      ImGui::TreeNode(i18n::T("Detalle del ultimo montaje", "Last build details"))) {
    ImGui::TextUnformatted(roster_manifest_.c_str());
    ImGui::TreePop();
  }

  // Crear uno nuevo.
  ui::SectionTitle(i18n::T("3. Crear personaje", "3. Create character"));
  ImGui::SetNextItemWidth(260);
  ImGui::InputText(i18n::T("Nombre (rotulo)", "Name (banner)"), nc_name_buf_, sizeof(nc_name_buf_));
  if (nc_name_buf_[0]) {
    ImGui::SameLine(0.0f, 18.0f);
    LiveBanner(nc_name_buf_);
  }
  // carpeta: se deduce del nombre (como al importar); escribir una propia es opcional
  const std::string nc_auto_mod = nc_name_buf_[0] ? ModSlug("pj", nc_name_buf_) : std::string();
  ImGui::SetNextItemWidth(260);
  ImGui::InputTextWithHint(i18n::T("Carpeta (opcional)", "Folder (optional)"),
                           nc_auto_mod.empty() ? i18n::T("se pone sola", "set automatically") : nc_auto_mod.c_str(),
                           nc_mod_buf_, sizeof(nc_mod_buf_));
  DonorCombo(i18n::T("Donante (moveset y tecnicas)", "Donor (moveset and techniques)"), nc_donor_idx_,
             false);
  Help(i18n::T("El personaje del juego del que toma los golpes, las tecnicas, la voz y las "
               "capsulas (mientras no le pongas capsulas propias).",
               "The game character it takes its moves, techniques, voice and capsules from "
               "(until you give it its own capsules)."));
  DonorCombo(i18n::T("Casilla tras", "Cell after"), nc_after_idx_, true);
  Help(i18n::T("Su casilla aparece en la rueda justo despues de este personaje.",
               "Its cell shows up on the wheel right after this character."));
  {
    char cur[96];
    if (nc_slot_choice_ < 0) {
      std::snprintf(cur, sizeof(cur), "%s", i18n::T("Automatica (primera libre)", "Automatic (first free)"));
    } else {
      std::snprintf(cur, sizeof(cur), "%s", i18n::T("Elegida", "Chosen"));
      for (const auto& s : kFreeSlots) {
        if (s.id == nc_slot_choice_) std::snprintf(cur, sizeof(cur), "%s", s.original);
      }
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(i18n::T("Plaza", "Slot"), cur)) {
      if (ImGui::Selectable(i18n::T("Automatica (primera libre)", "Automatic (first free)"), nc_slot_choice_ < 0)) {
        nc_slot_choice_ = -1;
      }
      for (const auto& s : kFreeSlots) {
        std::string owner;
        for (const auto& [folder, id] : slots) {
          if (id == s.id) owner = folder;
        }
        char label[128];
        if (owner.empty()) {
          std::snprintf(label, sizeof(label), i18n::T("%s - libre##%d", "%s - free##%d"), s.original, s.id);
        } else {
          std::snprintf(label, sizeof(label), i18n::T("%s - ocupada por %s##%d", "%s - taken by %s##%d"),
                        s.original, owner.c_str(), s.id);
        }
        if (ImGui::Selectable(label, nc_slot_choice_ == s.id)) nc_slot_choice_ = s.id;
      }
      ImGui::EndCombo();
    }
  }
  nc_forms_ = std::clamp(nc_forms_, 1, 6);
  {
    char cur[48];
    std::snprintf(cur, sizeof(cur), nc_forms_ == 1 ? i18n::T("1 forma", "1 form") : i18n::T("%d formas", "%d forms"),
                  nc_forms_);
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(i18n::T("Formas por traje", "Forms per costume"), cur)) {
      for (int f = 1; f <= 6; ++f) {
        char it[48];
        std::snprintf(it, sizeof(it), f == 1 ? i18n::T("1 forma", "1 form") : i18n::T("%d formas", "%d forms"), f);
        if (ImGui::Selectable(it, f == nc_forms_)) nc_forms_ = f;
      }
      ImGui::EndCombo();
    }
    Help(i18n::T("Transformaciones (normal, Super Saiyan...): una forma por modelo de cada traje.",
                 "Transformations (normal, Super Saiyan...): one form per model of each costume."));
  }
  ImGui::TextDisabled("%s", i18n::T(
      "Modelos: una ruta por linea, uno por traje (con varias formas: traje 1 forma 1, traje 1 "
      "forma 2, ...). Admite #AMB de B3 PS2, #AMB HD o LZX.",
      "Models: one path per line, one per costume (with several forms: costume 1 form 1, "
      "costume 1 form 2, ...). Accepts B3 PS2 #AMB, HD #AMB or LZX."));
  ImGui::InputTextMultiline("##nc_models", nc_models_buf_, sizeof(nc_models_buf_), ImVec2(-1.0f, 70.0f));
  if (ImGui::Button(i18n::T("Anadir modelo...", "Add model..."))) {
    std::string picked;
    if (PickFile(picked, i18n::T("Modelo (.amb / .bin)", "Model (.amb / .bin)"), "*.amb;*.bin;*.amo",
                 ModPipeline::ModsDir().string())) {
      std::string cur = nc_models_buf_;
      if (!cur.empty() && cur.back() != '\n') cur += '\n';
      cur += picked + '\n';
      std::snprintf(nc_models_buf_, sizeof(nc_models_buf_), "%s", cur.c_str());
    }
  }
  ImGui::TextDisabled("%s", i18n::T(
      "Icono y retratos: se generan desde el modelo (opcional: tu arte, aqui o despues en el editor).",
      "Icon and portraits: rendered from the model (optional: your own art, here or later in the editor)."));
  ImGui::SetNextItemWidth(-260);
  ImGui::InputText(i18n::T("Arte de la cara (opcional)", "Face art (optional)"), nc_face_buf_,
                   sizeof(nc_face_buf_));
  ImGui::SetNextItemWidth(-260);
  ImGui::InputText(i18n::T("Arte del retrato (opcional)", "Portrait art (optional)"), nc_portrait_buf_,
                   sizeof(nc_portrait_buf_));
  ModPipeline::NewCharacter nc;
  nc.name = nc_name_buf_;
  nc.mod = nc_mod_buf_[0] ? std::string(nc_mod_buf_) : nc_auto_mod;
  nc.donor = kDonors[std::clamp(nc_donor_idx_, 0, kDonorCount - 1)].id;
  nc.after = nc_after_idx_ >= 0 ? kDonors[nc_after_idx_].id : -1;
  nc.slot = nc_slot_choice_;
  nc.forms_per_costume = nc_forms_;
  nc.face = nc_face_buf_;
  nc.portrait = nc_portrait_buf_;
  {
    std::istringstream in(nc_models_buf_);
    std::string line;
    while (std::getline(in, line)) {
      line.erase(0, line.find_first_not_of(" \t\""));
      line.erase(line.find_last_not_of(" \t\"\r") + 1);
      if (!line.empty()) nc.models.push_back(line);
    }
  }
  const bool can_create = !nc.name.empty() && !nc.mod.empty() && !nc.models.empty();
  ImGui::BeginDisabled(!can_create || mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Crear", "Create"), ImVec2(180, 0))) {
    mod_pipeline_.CreateCharacter(nc);
    nc_selected_ = nc.mod;
  }
  ImGui::EndDisabled();
  if (!can_create && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", nc.name.empty() ? i18n::T("Escribe primero un nombre.", "Type a name first.")
                                            : i18n::T("Anade al menos un modelo.", "Add at least one model."));
  }

  ImGui::Separator();
  if (mod_pipeline_.IsRunning()) {
    ImGui::TextColored(kGold, "%s", i18n::T("Trabajando...", "Working..."));
  }
  DrawToolLog(mod_pipeline_.Output(), output_buf_, sizeof(output_buf_), "nc_out", 140.0f);
  ImGui::EndChild();
}

void LauncherDialog::DrawTexturesTab() {
  ImGui::BeginChild("##textures", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  ui::SectionTitle(i18n::T("Mod de texturas (B3 HD)", "Texture mod (B3 HD)"));
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
      const std::string label = chars[i].DisplayName() + "##" + std::to_string(i);
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
    // ':' solo es valido como letra de unidad (C:\...), nunca dentro de un
    // componente: rechazarlo entero descartaba toda ruta absoluta y caia al
    // default silenciosamente.
    const std::string cfg = tex_dir_buf_[0] ? tex_dir_buf_ : "";
    bool cfg_valid = !cfg.empty();
    if (cfg_valid) {
      for (std::size_t i = 0; i < cfg.size(); ++i) {
        const char c = cfg[i];
        const bool drive_colon = (c == ':' && i == 1);
        if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*' || (c == ':' && !drive_colon)) {
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
    ImGui::TextColored(kGold, i18n::T("Trabajando...", "Working..."));
  } else if (!mod_pipeline_.Output().empty()) {
    ImGui::TextDisabled(i18n::T("Hecho.", "Done."));
  }
  DrawToolLog(mod_pipeline_.Output(), output_buf_, sizeof(output_buf_), "tex_out", 160.0f);

  ImGui::EndChild();
}

void LauncherDialog::DrawDevTab() {
  ImGui::BeginChild("##dev_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  PushSectionHeader(i18n::T("Diagnostico", "Diagnostics"));

  bool dev_mode = dbz3::settings::DevMode();
  if (ImGui::Checkbox(i18n::T("Activar modo Dev (overlay F10)",
                              "Enable Dev mode (F10 overlay)"), &dev_mode)) {
    dbz3::settings::SetDevMode(dev_mode);
  }
  ui::Tip(i18n::T(
        "Anade un overlay en juego (F10) con diagnostico y opciones de prueba.",
        "Adds an in-game overlay (F10) with diagnostics and test switches."));

  bool show_fps = dbz3::settings::ShowFps();
  if (ImGui::Checkbox(i18n::T("Mostrar contador de FPS en juego (debug 60fps)",
                              "Show FPS counter in-game (60fps debug)"), &show_fps)) {
    dbz3::settings::SetShowFps(show_fps);
  }
  ui::Tip(i18n::T(
        "Muestra una ventana pequena con los FPS actuales mientras juegas. "
        "Util para verificar el limite de fotogramas / el modo 60fps.",
        "Displays a small corner window with the current FPS while playing. "
        "Useful to verify the frame cap / debug the 60fps mode."));

  bool async_shaders = dbz3::settings::AsyncShaderCompilation();
  if (ImGui::Checkbox(i18n::T("Compilar shaders en segundo plano",
                              "Compile shaders asynchronously"), &async_shaders)) {
    dbz3::settings::SetAsyncShaderCompilation(async_shaders);
  }
  ui::Tip(i18n::T(
        "En segundo plano (por defecto) los shaders se compilan en paralelo: "
        "carga mas rapida, pero puede dar un tiron la primera vez que aparece "
        "cada efecto. Desactivalo para compilarlos al momento (sin tirones, "
        "carga inicial mas lenta).",
        "Asynchronous (default) compiles shaders in parallel: faster loads, but "
        "may hitch the first time each effect appears. Turn it off to compile "
        "them on the spot (no hitching, slower initial load)."));

  bool occ = dbz3::settings::OcclusionQueries();
  if (ImGui::Checkbox(i18n::T("Consultas de oclusion del juego",
                              "Game occlusion queries"), &occ)) {
    dbz3::settings::SetOcclusionQueries(occ);
  }
  ui::Tip(i18n::T(
        "Permite al juego descartar geometria oculta (por defecto). Si la GPU "
        "se queda corta, desactivarlas elimina esperas del host a cambio de "
        "dibujar de mas: util para diagnosticar.",
        "Lets the game discard hidden geometry (default). On slow GPUs, turning "
        "them off removes host waits at the cost of extra overdraw: useful for "
        "diagnosis."));

  bool perf_logging = dbz3::settings::PerfLogging();
  if (ImGui::Checkbox(i18n::T("Registro de rendimiento (cada 5 s)",
                              "Performance log (every 5 s)"), &perf_logging)) {
    dbz3::settings::SetPerfLogging(perf_logging);
  }
  ui::Tip(i18n::T(
        "Escribe una linea cada 5 segundos con los FPS reales, el peor frame del "
        "intervalo y si la ventana tiene el foco. Es la forma de distinguir una "
        "bajada real de un alt-tab: Windows limita a la mitad la presentacion de "
        "una ventana visible sin foco.",
        "Writes one line every 5 seconds with the real FPS, the worst frame in "
        "the interval and whether the window has focus. The way to tell a real "
        "drop from an alt-tab: Windows halves the presentation of a visible "
        "unfocused window."));

  bool io_logging = dbz3::settings::IoLogging();
  if (ImGui::Checkbox(i18n::T("Registro de E/S de disco (cada 5 s)",
                              "Disk I/O log (every 5 s)"), &io_logging)) {
    dbz3::settings::SetIoLogging(io_logging);
  }
  ui::Tip(i18n::T(
        "Escribe una linea cada 5 segundos con las lecturas de los AFS "
        "(cantidad, MB, latencia media/p95/p99, maximos y lecturas lentas) y una "
        "linea por cada lectura lenta. Es la forma de ver si un tiron viene del "
        "disco.",
        "Writes one line every 5 seconds with AFS reads (count, MB, average/"
        "p95/p99 latency, worst and slow reads) plus one line per slow read. "
        "The way to tell whether a hitch comes from the disk."));

  bool readahead = dbz3::settings::IoReadahead();
  if (ImGui::Checkbox(i18n::T("Lectura anticipada de disco",
                              "Disk readahead"), &readahead)) {
    dbz3::settings::SetIoReadahead(readahead);
  }
  ui::Tip(i18n::T(
        "Lee bloques mas grandes de una vez y sirve las lecturas siguientes "
        "desde memoria: ayuda en discos mecanicos y en las cargas. Se desactiva "
        "solo mientras haya mods instalados.",
        "Reads bigger chunks at once and serves the following reads from RAM: "
        "helps on mechanical disks and during loads. Disabled automatically "
        "while mods are installed."));

  bool texdump = dbz3::settings::TextureDumpEnabled();
  if (ImGui::Checkbox(i18n::T("Volcado de texturas para mods (dev)",
                              "Texture dump for mods (dev)"), &texdump)) {
    dbz3::settings::SetTextureDumpEnabled(texdump);
  }
  ui::Tip(i18n::T(
        "Escribe cada textura unica como DDS + index.jsonl para autorar packs de "
        "texturas (estilo PCSX2). Ocupa cientos de MB: elige una carpeta con "
        "espacio. Solo para desarrollo; requiere reiniciar y no tiene efecto con "
        "la mejora de texturas HD activada.",
        "Writes every unique texture as DDS + index.jsonl for authoring texture "
        "packs (PCSX2-style). It can take hundreds of MB: pick a folder with "
        "room. Development only; requires a restart and has no effect while HD "
        "texture enhancement is on."));
  if (texdump) {
    // Carpeta destino: el volcado puede ocupar cientos de MB, asi que nunca
    // vive en el disco de instalacion por defecto: el usuario la elige y se
    // recuerda entre sesiones (`dbz3_texture_dump_dir`).
    std::string dir = dbz3::settings::TextureDumpDir();
    if (dir.empty()) dir = dbz3::settings::DefaultTextureDumpDir();
    static char texdump_buf[512];
    std::snprintf(texdump_buf, sizeof(texdump_buf), "%s", dir.c_str());
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::InputText(i18n::T("Carpeta del volcado", "Dump folder"),
                         texdump_buf, sizeof(texdump_buf))) {
      dbz3::settings::SetTextureDumpDir(texdump_buf);
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Elegir carpeta...", "Choose folder..."))) {
      std::string picked;
      if (PickFolder(picked, dir) && !picked.empty()) {
        dbz3::settings::SetTextureDumpDir(picked);
      }
    }
    ui::Tip(i18n::T(
          "Carpeta donde se escriben los DDS. Elige una unidad con espacio "
          "libre; el volcado puede ocupar cientos de MB.",
          "Folder the DDS files are written to. Pick a drive with free space; "
          "the dump can take hundreds of MB."));
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
    ui::RowLabel(i18n::T("Texturas HD: cuanto gastar (avanzado)",
                             "HD textures: spending (advanced)"));
    if (ImGui::Combo("##DrawDevTab_1",
                     &mins_idx, hd_texmins_items, 3)) {
      dbz3::settings::SetHdTextureMaxTexels(hd_texmins_vals[mins_idx]);
    }
    ui::Tip(i18n::T(
          "Cuantas texturas se mejoran segun su tamano. \"Bajo\" solo toca las "
          "pequenas y medianas (mucho menos consumo); \"Alto\" tambien las "
          "grandes de escenario (nota mas, gasta mas). Cambia la VRAM y el uso "
          "de GPU de la mejora de texturas.",
          "Which textures get enhanced based on size. \"Low\" only touches small "
          "and medium ones (much less cost); \"High\" also the big stage "
          "textures (more noticeable, more cost). Changes the VRAM and GPU usage "
          "of the texture enhancement."));
  }

  bool diag = dbz3::settings::DiagLogging();
  if (ImGui::Checkbox(i18n::T("Registro de diagnostico GPU (logs + .bmp)",
                              "GPU diagnostic logging (logs + .bmp dumps)"), &diag)) {
    dbz3::settings::SetDiagLogging(diag);
  }
  ui::Tip(i18n::T(
        "Escribe diagnostico GPU por fotograma (logs, readbacks y dumps .bmp). "
        "Genera archivos grandes. Mantener apagado normalmente.",
        "Writes per-frame GPU diagnostics (logs, readbacks and .bmp dumps). "
        "Generates large files. Keep off normally."));

  bool crashdump = dbz3::settings::CrashDumpEnabled();
  if (ImGui::Checkbox(i18n::T("Guardar minidump de crash (crash_*.dmp)",
                              "Write crash minidump (crash_*.dmp)"), &crashdump)) {
    dbz3::settings::SetCrashDumpEnabled(crashdump);
  }
  ui::Tip(i18n::T(
        "Escribe un minidump cuando el juego falla. Mantener apagado normalmente.",
      "Writes a minidump file when the game crashes. Keep off normally."));

  bool upd_check = dbz3::settings::UpdateCheckEnabled();
  if (ImGui::Checkbox(i18n::T("Buscar actualizaciones al abrir",
                              "Check for updates on launch"), &upd_check)) {
    dbz3::settings::SetUpdateCheckEnabled(upd_check);
  }
  ui::Tip(i18n::T(
        "Consulta en GitHub cual es la ultima version publicada al abrir el "
        "launcher (solo lectura, menos de 1 KB). Desactivalo si no quieres "
        "ninguna conexion.",
        "Asks GitHub for the latest published release when the launcher opens "
        "(read-only, under 1 KB). Turn it off if you prefer no connections."));

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Datos del usuario", "User data"));
  ImGui::TextWrapped(i18n::T("Guardado y cache: %s", "Saves and cache: %s"),
                     dbz3::settings::UserDataRoot().string().c_str());
  ImGui::TextWrapped("%s", dbz3::settings::UserDataIsPortable()
      ? i18n::T("Carpeta portable (junto al juego).",
                "Portable folder (next to the game).")
      : i18n::T("La carpeta del juego no es escribible: se usa la carpeta de usuario.",
                "The game folder is not writable: using the per-user folder."));

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Versiones", "Versions"));
  // Coherencia de los ficheros instalados: actualizar copiando solo el exe (o
  // solo las DLLs) encima de una carpeta vieja deja un build que no se puede
  // identificar desde su log -- paso justo en los reportes que motivaron esto.
  // El log lleva la linea `dbz3: entorno ...` con el sistema, la RAM y TODAS las
  // versiones; aqui se ven en pantalla.
  for (const auto& component : dbz3::launcher::InstalledComponents()) {
    ImGui::TextWrapped("%s: %s", component.file_name.c_str(),
                       component.version.empty() ? "?" : component.version.c_str());
  }
  {
    const std::string mismatch = dbz3::launcher::InstalledVersionMismatch();
    if (!mismatch.empty()) {
      ImGui::TextColored(kDragonOrange, "%s",
                         i18n::T("Mezcla de versiones detectada.",
                                 "Mixed versions detected."));
      ImGui::TextWrapped(i18n::T(
          "Descomprime el zip completo en una carpeta nueva: mezclar el exe de una version con "
          "las DLLs de otra produce fallos que no se pueden reproducir.",
          "Unzip the full package into a new folder: mixing the exe of one version with the DLLs "
          "of another causes failures that cannot be reproduced."));
    }
  }

  ImGui::EndChild();
}

}  // namespace dbz3::launcher
