// dbz3 - Pre-game launcher screen implementation.
// Dark modern style with Dragon Ball accent colors (orange/blue).

#include "launcher_state.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>

#include "../mods.h"
#include "settings.h"
#include "i18n.h"
#include "ui_kit.h"
#include "update_check.h"
#include "novas_mods.h"
#include "novedades.h"
#include "../region.h"
#include "../ingame/game_style.h"
#include "../pasarela.h"
#include "../mods_opcionales.h"

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


// Ultimo bloque de "Novedades" ya visto: la ventana se abre sola una vez tras cada actualizacion.
REXCVAR_DEFINE_STRING(dbz3_news_seen, "", "DBZ3/Launcher", "Last What's new block shown by the launcher");
// "Primeros pasos": se abre sola la primera vez (y siempre desde el boton Ayuda de la cabecera).
REXCVAR_DEFINE_BOOL(dbz3_first_steps_seen, false, "DBZ3/Launcher", "First steps window already shown");

namespace dbz3::launcher {

namespace {
constexpr int kLauncherTabCount = 11;
constexpr int kGameTab = 6;   // "Juego" (antes "Mods nativos"): funciones nuevas, guardados, brillo HD

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
  if (type == "texturas") return i18n::T("pack de texturas", "texture pack");
  if (type == "campana") return i18n::T("historia", "story");
  return i18n::T("otro", "other");
}

// Color de la etiqueta de tipo (mods.cpp ModTypeColor; los packs de texturas, en azul).
ImVec4 ModTypeColorVec(const std::string& type) {
  if (type == "texturas") return ui::kBlue;
  if (type == "campana") return ImVec4(0.80f, 0.45f, 0.20f, 1.0f);
  const int c = dbz3::ModTypeColor(type);
  return c < 0 ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
               : ImVec4(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f);
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
// the footer summary (the launcher fonts carry Latin-1, so accents render).
const char* LanguageDisplayName(int xbox_language_id) {
  switch (xbox_language_id) {
    case 2:
      return "Japanese";
    case 3:
      return "Deutsch";
    case 4:
      return "Français";
    case 5:
      return "Español";
    case 6:
      return "Italiano";
    case 9:
      return "Portugu\xC3\xAAs (Brasil)";
    default:
      return "English";
  }
}

// Nombre de cada idioma en su propia lengua (asi lo encuentra quien no entiende el idioma actual).
constexpr const char* kLanguageItems[] = {"English", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E (Japanese)", "Deutsch",
                                          "Fran\xC3\xA7" "ais", "Espa\xC3\xB1" "ol", "Italiano",
                                          "Portugu\xC3\xAA" "s (Brasil)"};
constexpr int kLanguageIds[] = {1, 2, 3, 4, 5, 6, 9};   // 9: portugues (launcher y textos propios; el juego en ingles)

// Nombre corto del escalado elegido (resumen de abajo y pestana Video), como lo ve el jugador.
const char* EffectDisplayName(const std::string& v) {
  if (v == "fsr") return "FSR 1";
  if (v == "fsr3") return "FSR 3";
  if (v == "dlss") return "DLSS";
  if (v == "cas") return "CAS";
  return i18n::T("Bilineal", "Bilinear");
}

// Tecla pulsada -> nombre que entiende el emulador de mando (rexglue ui/keybinds.cpp kKeyNames).
const char* ImGuiKeyToBindName(ImGuiKey k) {
  if (k >= ImGuiKey_A && k <= ImGuiKey_Z) {
    static const char* const kLetters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                           "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    return kLetters[k - ImGuiKey_A];
  }
  if (k >= ImGuiKey_0 && k <= ImGuiKey_9) {
    static const char* const kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    return kDigits[k - ImGuiKey_0];
  }
  if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) {
    static const char* const kPad[] = {"Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4",
                                       "Numpad5", "Numpad6", "Numpad7", "Numpad8", "Numpad9"};
    return kPad[k - ImGuiKey_Keypad0];
  }
  if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) {
    static const char* const kF[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    return kF[k - ImGuiKey_F1];
  }
  switch (k) {
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter: return "Return";
    case ImGuiKey_KeypadEnter: return "NumpadEnter";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Backspace: return "Backspace";
    case ImGuiKey_Delete: return "Delete";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return "Shift";
    case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return "Control";
    case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return "Alt";
    case ImGuiKey_Semicolon: return "Semicolon";
    case ImGuiKey_Apostrophe: return "Quote";
    case ImGuiKey_Comma: return "Comma";
    case ImGuiKey_Period: return "Period";
    case ImGuiKey_Slash: return "Slash";
    case ImGuiKey_Backslash: return "Backslash";
    case ImGuiKey_LeftBracket: return "LBracket";
    case ImGuiKey_RightBracket: return "RBracket";
    case ImGuiKey_GraveAccent: return "Backtick";
    case ImGuiKey_Minus: return "Minus";
    case ImGuiKey_Equal: return "Plus";
    case ImGuiKey_KeypadAdd: return "NumpadPlus";
    case ImGuiKey_KeypadSubtract: return "NumpadMinus";
    case ImGuiKey_KeypadMultiply: return "NumpadStar";
    case ImGuiKey_KeypadDivide: return "NumpadSlash";
    case ImGuiKey_CapsLock: return "CapsLock";
    default: return nullptr;
  }
}

// Mensaje de estado de la pestana Mods: en amarillo si es un fallo (antes todo salia en verde).
bool IsBadStatus(const std::string& s) {
  for (const char* p : {i18n::T("Error al instalar el mod: ", "Error installing mod: "),
                        i18n::T("No se pudo arreglar solo; abre su carpeta: ",
                                "Could not fix it automatically; open its folder: "),
                        i18n::T("No se pudo copiar: ", "Could not copy: "),
                        i18n::T("Nombre de perfil invalido.", "Invalid profile name."),
                        i18n::T("Suelta un mod (.zip) o una textura editada (.png con su nombre original): ",
                                "Drop a mod (.zip) or an edited texture (.png with its original name): "),
                        i18n::T("No se pudo crear la copia: ", "Backup failed: ")})
    if (s.rfind(p, 0) == 0) return true;
  return false;
}

// Bloque de novedades de esta version: el mas nuevo que no la supera (novedades.h).
const NewsBlock* CurrentNews() {
  const std::string cur = CurrentVersion();
  for (const NewsBlock& b : kNewsBlocks)
    if (cur.empty() || !IsVersionNewer(b.version, cur)) return &b;
  return nullptr;
}

// Native folder/file pickers. Windows uses the IFileOpenDialog COM dialog; on
// other platforms there is no native shell dialog wired up yet, so the pickers
// report "cancelled" (callers keep the default paths). The plan for Linux is a
// portable dialog (SDL file dialog or zenity/kdialog).
#if REX_PLATFORM_WIN32
bool PickFolder(std::string& out, const std::string& initial) {
  // COM-BALANCE: solo se deshace lo que se hizo (RPC_E_CHANGED_MODE no cuenta).
  const HRESULT com_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOpenDialog* dlg = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (!initial.empty()) {
      // Intentar partir de la carpeta actual (si existe).
      const std::filesystem::path init = std::filesystem::u8path(initial);
      std::error_code ec;
      if (std::filesystem::is_directory(init, ec)) {
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
          // UTF-16 -> UTF-8 de verdad (antes se truncaba cada caracter a un byte: con tildes la
          // carpeta elegida no se encontraba)
          out = rex::path_to_utf8(std::filesystem::path(std::wstring(path)));
          CoTaskMemFree(path);
          ok = true;
        }
        res->Release();
      }
    }
    dlg->Release();
  }
  if (SUCCEEDED(com_init)) CoUninitialize();
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
              const std::string& initial, HWND owner = nullptr) {
  const HRESULT com_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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
      // Si `initial` es un archivo (p.ej. data_cmn.afs) se abre en su carpeta.
      std::filesystem::path init = std::filesystem::u8path(initial);
      std::error_code ec;
      if (!std::filesystem::is_directory(init, ec) && init.has_parent_path()) init = init.parent_path();
      if (std::filesystem::is_directory(init, ec)) {
        IShellItem* item = nullptr;
        std::wstring winit = init.wstring();
        if (SUCCEEDED(SHCreateItemFromParsingName(winit.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item)))) {
          dlg->SetFolder(item);
          item->Release();
        }
      }
    }
    if (SUCCEEDED(dlg->Show(owner))) {
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
  if (SUCCEEDED(com_init)) CoUninitialize();
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
// Pruebas sin raton: DBZ3_TEST_SCROLL=<px> fija el scroll de las pestanas que lo llaman.
void TestScroll() {
  static const char* px = std::getenv("DBZ3_TEST_SCROLL");
  if (px) ImGui::SetScrollY(float(std::atof(px)));
}

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

// Aviso encima de JUGAR, partido en lineas al ancho del boton (una linea larga se salia de la ventana).
// Aviso de carga con los puntos animados (la traduccion se busca con los tres puntos).
static std::string LoadingDots(const char* text) {
  std::string s = text;
  while (!s.empty() && s.back() == '.') s.pop_back();
  return s + std::string(size_t(int(ImGui::GetTime() * 2.5) % 4), '.');
}

static void TextAbovePlay(const ImVec2& play_min, float bottom_gap, ImVec4 color, const char* text) {
  const float wrap = 320.0f;
  const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, wrap);
  ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                      ImVec2(play_min.x, play_min.y - bottom_gap - size.y),
                                      ImGui::GetColorU32(color), text, nullptr, wrap);
}

// Esc o B del mando: cierra la ventana emergente abierta (con mando no habia forma de salir sin buscar el boton).
static bool CloseKeyPressed() {
  return ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
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
    if (ImGui::Button(i18n::T("Cancelar", "Cancel"), ImVec2(120, 0)) || CloseKeyPressed()) {
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
             i18n::T(": tiene que medir un numero exacto de veces el original (x1, x2, x4, x8...), igual "
                     "en ancho y alto y como mucho 16384 px (el tamano esta en el nombre).",
                     ": it must be a whole number of times the original size (x1, x2, x4, x8...), the "
                     "same in width and height and at most 16384 px (the size is in the name).");
    case dbz3::ModInfo::kBadToml:
      return std::string(i18n::T("Error en el archivo de configuracion: ", "Error in the settings file: ")) + d;
  }
  return d;
}

// Arrastrar y soltar (lo mas pedido para instalar mods sin buscar carpetas): un .zip se instala
// como mod; un .png/.dds con nombre de textura de pack va a "Mi pack de texturas".
void LauncherDialog::HandleDroppedFiles() {
  // Pruebas sin raton: DBZ3_TEST_DROP="a.zip;b.zip" los suelta al abrir; DBZ3_TEST_FIX=<mod>
  // pulsa "Arreglar" sobre ese mod (el resultado va al log).
  static const bool test_once = [] {
#ifdef _WIN32   // getenv da la pagina ANSI: con tildes no es UTF-8
    if (const wchar_t* v = _wgetenv(L"DBZ3_TEST_DROP")) {
      const std::wstring s(v);
#else
    if (const char* v = std::getenv("DBZ3_TEST_DROP")) {
      const std::string s(v);
#endif
      for (size_t p = 0; p < s.size();) {
        const size_t q = std::min(s.find(';', p), s.size());
        if (q > p) QueueDroppedFile(std::filesystem::path(s.substr(p, q - p)));
        p = q + 1;
      }
    }
    // DBZ3_TEST_PS2="<juego>,<ruta>;<juego>,<ruta>": registra esas ISO de PS2 sin dialogo (pruebas de la caja de PS2)
    if (const char* t = std::getenv("DBZ3_TEST_PS2")) {
      std::string all = t;
      for (size_t p = 0; p < all.size();) {
        size_t q = all.find(';', p);
        if (q == std::string::npos) q = all.size();
        const std::string one = all.substr(p, q - p);
        if (one.size() > 2) {
          const std::string r = dbz3::pasarela::RegistrarIsoPs2(one[0] - '0', std::filesystem::u8path(one.substr(2)));
          REXLOG_INFO("dbz3: prueba ps2 {} -> '{}'", one, r);
        }
        p = q + 1;
      }
    }
    if (const char* m = std::getenv("DBZ3_TEST_FIX"))
      REXLOG_INFO("dbz3: arreglar '{}' -> {}", m, dbz3::FixNestedMod(m) ? "ok" : "no");
    return true;
  }();
  (void)test_once;
  static const char* test_tab = std::getenv("DBZ3_TEST_TAB");   // abre esa pestana (indice)
  if (test_tab) { tab_request_ = std::atoi(test_tab); test_tab = nullptr; }
  static int test_play = std::getenv("DBZ3_TEST_PLAY") ? 90 : -1;   // pulsa JUGAR tras ~1,5 s
  if (test_play > 0 && --test_play == 0) pad_play_ = true;
  // DBZ3_TEST_TOUR=<ms> (videos del tutorial): Video, Mods, Nova's Mods y Juego, <ms> en cada una, y despues JUGAR
  static const int tour_ms = std::getenv("DBZ3_TEST_TOUR") ? std::atoi(std::getenv("DBZ3_TEST_TOUR")) : 0;
  if (tour_ms > 0) {
    static const double t0 = ImGui::GetTime();
    static int paso = -1;
    static const int kTour[] = {0, 4, 5, 6};
    const int k = int((ImGui::GetTime() - t0) * 1000.0 / tour_ms);
    if (k != paso && k < int(std::size(kTour))) tab_request_ = kTour[k];
    if (k != paso && k == int(std::size(kTour))) pad_play_ = true;
    paso = k;
  }
  // DBZ3_TEST_NOVA=<id> (con DBZ3_TEST_TAB=5): pulsa "Instalar" de ese mod de Nova's Mods
  static const char* test_nova = std::getenv("DBZ3_TEST_NOVA");
  if (test_nova && GetNovaState() == NovaState::kReady) {
    for (const NovaMod& m : NovaCatalog()) {
      if (m.id == test_nova) InstallNovaMod(m);
    }
    test_nova = nullptr;
  }
  std::vector<std::filesystem::path> files;
  {
    std::lock_guard<std::mutex> lock(g_drop_mutex);
    files.swap(g_dropped);
  }
  for (const auto& f : files) {
    // path::string() convierte a la pagina ANSI y LANZA con nombres que no caben (tildes en
    // otra pagina, japones...): todo en UTF-8, y un archivo raro nunca cierra el launcher
    const std::string fname = rex::path_to_utf8(f.filename());
    try {
    std::string ext = rex::path_to_utf8(f.extension());
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
               dbz3::settings::IsTexturePackFileName(rex::path_to_utf8(f.stem()))) {
      const auto pack = dbz3::ModsRoot() / "Mi pack de texturas";
      std::error_code ec;
      std::filesystem::create_directories(pack, ec);
      std::filesystem::copy_file(f, pack / f.filename(), std::filesystem::copy_options::overwrite_existing, ec);
      mods_status_ = ec ? std::string(i18n::T("No se pudo copiar: ", "Could not copy: ")) + fname
                        : std::string(i18n::T("Textura anadida a 'Mi pack de texturas': ",
                                              "Texture added to 'My texture pack': ")) + fname;
    } else {
      mods_status_ = std::string(i18n::T(
          "Suelta un mod (.zip) o una textura editada (.png con su nombre original): ",
          "Drop a mod (.zip) or an edited texture (.png with its original name): ")) + fname;
    }
    } catch (const std::exception& ex) {
      mods_status_ = std::string(i18n::T("Error al instalar el mod: ", "Error installing mod: ")) + fname;
      REXLOG_ERROR("dbz3: soltar {}: {}", fname, ex.what());
    }
    REXLOG_INFO("dbz3: soltado {} -> {}", rex::path_to_utf8(f), mods_status_);
    mods_loaded_ = false; broken_mods_ = -1;
    request_tab_ = 1;   // muestra el resultado en la pestana Mods
  }
}

// Coste de pintar el launcher (CPU de OnDraw): el primer frame siempre va al log; con el registro
// de rendimiento activo, una linea cada 5 s con la media y el peor frame de la pestana abierta.
struct DrawTimer {
  const int& tab;
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  ~DrawTimer() {
    static bool first = true;
    static int frames = 0, last_tab = -1;
    static double sum = 0.0, worst = 0.0, next_log = 0.0;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (first) {
      first = false;
      REXLOG_INFO("dbz3 launcher perf: primer frame {:.1f} ms", ms);
      return;
    }
    const double now = ImGui::GetTime();
    if (tab != last_tab || now >= next_log) {
      if (frames && dbz3::settings::PerfLogging())
        REXLOG_INFO("dbz3 launcher perf: pestana {} media {:.2f} ms peor {:.2f} ms ({} frames)", last_tab,
                    sum / frames, worst, frames);
      frames = 0, sum = worst = 0.0, last_tab = tab, next_log = now + 5.0;
    }
    ++frames, sum += ms, worst = std::max(worst, ms);
  }
};

// Lista de mods en segundo plano. Con un pack HD instalado (12k imagenes) ListMods tardaba ~3 s y el
// launcher se quedaba congelado al abrirse, al entrar en Mods y tras cada casilla; ahora se pide aqui,
// se lee en otro hilo y se aplica al llegar (lo que mueve carpetas sigue en este hilo).
void LauncherDialog::PollModsScan() {
  // el kit acaba de escribir un mod (swap/texturas): que aparezca sin pulsar Refrescar
  if (mod_pipeline_.Generation() != last_pipeline_gen_) {
    last_pipeline_gen_ = mod_pipeline_.Generation();
    mods_loaded_ = false;
  }
  // los "se pisan" llegan despues de la lista (con un pack HD tardan mas que ella)
  if (mods_scan_ && mods_scan_->taken && mods_scan_->stage.load(std::memory_order_acquire) == 2) {
    mods_conflicts_ = std::move(mods_scan_->conflicts);
    mods_scan_.reset();
  }
  if (mods_scan_ && !mods_scan_->taken && mods_scan_->stage.load(std::memory_order_acquire) >= 1) {
    mods_scan_->taken = true;
    mods_cache_ = std::move(mods_scan_->mods);
    mods_ready_ = true;
    active_mods_ = broken_mods_ = 0;
    int auto_fixed = 0;
    const bool can_fix = !launching_ && !mod_pipeline_.IsRunning();   // no mover carpetas mientras el kit lee
    for (const dbz3::ModInfo& m : mods_cache_) {
      if (m.enabled) ++active_mods_;
      if (!m.enabled || m.problems.empty()) continue;
      // se cura solo: un mod metido en una carpeta de mas (copiado a mano) se saca sin pedir
      // nada; FixNestedMod es transaccional y devuelve false si no hay nada seguro que hacer
      const bool nested = std::any_of(m.problems.begin(), m.problems.end(),
                                      [](const auto& p) { return p.first == dbz3::ModInfo::kNested; });
      if (can_fix && nested && dbz3::FixNestedMod(m.name)) {
        ++auto_fixed;
        continue;
      }
      ++broken_mods_;
    }
    if (auto_fixed) {
      mods_status_ = std::string(i18n::T("Mods colocados solos en su sitio: ", "Mods moved into place automatically: ")) +
                     std::to_string(auto_fixed);
      mods_loaded_ = false;
    }
    // packs de texturas activos: lo que se ve en JUGAR sigue a la lista
    dbz3::settings::RefreshTexturePacks();
    // F0-12: personajes nuevos sin el kit -> se juega con el montaje que traia el pack (aviso visible)
    kit_missing_ = ModPipeline::HasCharacterSources() &&
                   !ModPipeline::RosterToolAvailable();
  }
  if (!mods_loaded_ && !mods_scan_) {
    mods_loaded_ = true;   // un cambio mientras se lee lo vuelve a poner a false: otra lectura al acabar
    auto scan = std::make_shared<ModsScan>();
    mods_scan_ = scan;
    // hilo suelto: si se pulsa JUGAR a mitad, el launcher no espera a que termine
    std::thread([scan] {
      const auto t0 = std::chrono::steady_clock::now();
      try {
        scan->mods = dbz3::ListMods();
        for (dbz3::ModInfo& m : scan->mods)   // "otro" que en realidad es un pack de texturas: que lo diga
          if (m.type == "other" && dbz3::settings::IsTexturePackMod((dbz3::ModsRoot() / m.name).string()))
            m.type = "texturas";
      } catch (const std::exception& e) {
        REXLOG_WARN("dbz3: lista de mods incompleta: {}", e.what());
      }
      const auto t1 = std::chrono::steady_clock::now();
      const size_t n = scan->mods.size();
      scan->stage.store(1, std::memory_order_release);
      try {
        scan->conflicts = dbz3::FindConflicts();
      } catch (const std::exception& e) {
        REXLOG_WARN("dbz3: comprobacion de mods que se pisan incompleta: {}", e.what());
      }
      REXLOG_INFO("dbz3 launcher perf: {} mods leidos en {} ms en segundo plano (+{} ms comprobando si se pisan)", n,
                  std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count());
      scan->stage.store(2, std::memory_order_release);
    }).detach();
  }
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  const DrawTimer draw_timer{tab_index_};
  // The launcher UI language follows the game's selected text language. Keep it
  // updated every frame so changing the "Language" combo re-translates the whole
  // launcher immediately.
  i18n::SetLanguage(dbz3::settings::Language());
  PollController(io);
  HandleDroppedFiles();
  PollModsScan();
  // Ctrl+Tab / Ctrl+Shift+Tab switch tabs from the keyboard.
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    tab_request_ = (tab_index_ + (io.KeyShift ? kLauncherTabCount - 1 : 1)) % kLauncherTabCount;
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
    if (const auto logo = dbz3::ingame::style::Img("hub/logo_512")) {
      const float h = 70.0f, w = logo.w * h / logo.h;
      dbz3::ingame::style::DrawPic(dl, logo, ImVec2(wp.x + ww - 24.0f - w, wp.y + 8.0f),
                                   ImVec2(wp.x + ww - 24.0f, wp.y + 8.0f + h));
    }
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
    if (CurrentNews()) {
      ImGui::SameLine(0.0f, 8.0f);
      ui::FontScope f(fonts.sm);
      ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentSoft);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 99.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
      if (ImGui::Button((std::string(ICON_STAR "  ") + i18n::T("Novedades", "What's new") + "###news_btn").c_str()))
        news_pending_ = true;
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
    }
    // Ayuda: los primeros pasos, siempre a mano (la primera vez se abren solos).
    ImGui::SameLine(0.0f, 8.0f);
    {
      ui::FontScope f(fonts.sm);
      ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentSoft);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 99.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
      if (ImGui::Button((std::string(ICON_INFO "  ") + i18n::T("Primeros pasos", "First steps") + "###help_btn").c_str()))
        first_steps_pending_ = true;
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", i18n::T("Qué hacer para empezar a jugar, en 4 pasos.",
                                        "What to do to start playing, in 4 steps."));
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
      ImGui::TextDisabled("%s", LoadingDots(i18n::T("Buscando actualizaciones...", "Checking for updates...")).c_str());
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
      if (const std::string why = dbz3::launcher::LastNetworkError(); !why.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip(i18n::T("Detalle tecnico: %s", "Technical detail: %s"), why.c_str());
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
          i18n::T("Aviso: hay archivos de dos versiones distintas del juego (%s). Descomprime el zip "
                  "completo en una carpeta nueva y copia ahí tus carpetas mods y user_data.",
                  "Warning: there are files from two different versions of the game (%s). Unzip the full "
                  "package into a new folder and copy your mods and user_data folders there."),
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
    if (iso_mode) DrawIsoPrepBox();
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
          "Pon tu ISO de Budokai 3 HD (o la carpeta con los datos) junto a dbz3.exe, o elígela con "
          "los botones «Carpeta extraída» o «Imagen ISO». Después pulsa JUGAR.",
          "Place your Budokai 3 HD ISO (or the game data folder) next to dbz3.exe, or pick it with "
          "the 'Extracted folder' or 'ISO image' buttons. Then press PLAY.");
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
  const float src_y = ImGui::GetCursorPosY();
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
          banner_error_ = i18n::T("No se pudo abrir la carpeta elegida. Comprueba que no está en un disco "
                                  "desconectado o elige la ISO del juego.",
                                  "The chosen folder could not be opened. Check that it is not on a "
                                  "disconnected drive, or pick the game's ISO.");
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
        i18n::T("Usa la carpeta con los datos del juego ya extraidos (la que tiene DBZ3, o default.xex y us/). "
                "Los mods funcionan igual que con la ISO.",
                "Use the folder with the already-extracted game data (the one with DBZ3, or default.xex and us/). "
                "Mods work the same as with the ISO."));
  }
  ImGui::SameLine();
  ImGui::SetCursorPosY(src_y);
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
        banner_error_ = i18n::T("Ese archivo no es una ISO de Xbox 360. Elige la ISO de Budokai 3 HD "
                                "(Xbox 360), no la de PS2.",
                                "That file is not an Xbox 360 ISO. Pick the Budokai 3 HD ISO (Xbox 360), "
                                "not the PS2 one.");
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
        i18n::T("Juega desde la imagen del disco (.iso de Xbox 360 de Budokai 3 HD / HD Collection), sin "
                "extraerla. Con mods, la primera vez se copian ~0,5 GB de sus datos (una sola vez).",
                "Play from the disc image (Xbox 360 .iso of Budokai 3 HD / HD Collection) without extracting it. "
                "With mods, ~0.5 GB of its data is copied the first time (only once)."));
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
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kError, "%s",
                       i18n::T("Tus ajustes no se han podido leer (el archivo está dañado) y se usan los "
                               "de fábrica. Pulsa «Reparar instalación» abajo para empezar con ajustes "
                               "limpios: tu archivo viejo se guarda aparte, no se pierde.",
                               "Your settings could not be read (the file is damaged), so the defaults are "
                               "in use. Press 'Repair install' below to start with clean settings: your old "
                               "file is kept aside, not lost."));
    ImGui::PopTextWrapPos();
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
  const char* tab_tip = nullptr;   // lo pone tip() antes de cada pestana: se ve al pasar el raton por ella
  auto tip = [&](const char* es, const char* en) { tab_tip = i18n::T(es, en); return true; };
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
    if (tab_tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tab_tip);
    tab_tip = nullptr;
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
    const int want_tab = request_tab_;
    request_tab_ = 0;
    if (tip("Pantalla, calidad de imagen, FPS e idioma.",
                  "Display, image quality, FPS and language.") &&
        begin_tab(tab_label(ICON_VIDEO, i18n::T("Video", "Video"), "tab_video"),
                  want_tab == 4 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawVideoTab();
      ImGui::EndTabItem();
    }
    if (tip("Nitidez y suavizado de la imagen (FSR, FXAA). Si no sabes, déjalo como está.",
                  "Image sharpness and smoothing (FSR, FXAA). If unsure, leave it as is.") &&
        begin_tab(tab_label(ICON_UPSCALE, i18n::T("Escalado", "Upscaling"), "tab_upscale"))) {
      DrawUpscaleTab();
      ImGui::EndTabItem();
    }
    if (tip("Volumen general y silencio.",
                  "Master volume and mute.") &&
        begin_tab(tab_label(ICON_AUDIO, i18n::T("Audio", "Audio"), "tab_audio"))) {
      DrawAudioTab();
      ImGui::EndTabItem();
    }
    if (tip("Mando o teclado: botones y sensibilidad.",
                  "Controller or keyboard: buttons and sensitivity.") &&
        begin_tab(tab_label(ICON_INPUT, i18n::T("Controles", "Input"), "tab_input"))) {
      DrawInputTab();
      ImGui::EndTabItem();
    }
    if (tip("Tus mods instalados: actívalos o desactívalos con la casilla. Para instalar uno, arrastra su .zip a esta ventana.",
                  "Your installed mods: turn them on or off with the box. To install one, drag its .zip onto this window.") &&
        begin_tab(tab_label(ICON_MODS, i18n::T("Mods", "Mods"), "tab_mods"),
                            want_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (tip("Mods listos para jugar: un clic los descarga e instala.",
                  "Ready-to-play mods: one click downloads and installs them.") &&
        begin_tab(tab_label(ICON_DOWNLOAD, "Nova's Mods", "tab_novas"),
                  want_tab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawNovasModsTab();
      ImGui::EndTabItem();
    }
    if (tip("Funciones del juego: arranque rápido, FPS, partida al 100% y brillo HD.",
                  "Game features: quick start, FPS, 100% save and HD shine.") &&
        begin_tab(tab_label(ICON_NATIVE, i18n::T("Juego", "Game"), "tab_native"))) {
      DrawNativeModsTab();
      ImGui::EndTabItem();
    }
    if (tip("Cambia el modelo de un personaje por el de otro (crea un mod).",
                  "Swap a character's model for another one's (creates a mod).") &&
        begin_tab(tab_label(ICON_SWAP, i18n::T("Modelos", "Model Swap"), "tab_swap"))) {
      DrawModelSwapTab();
      ImGui::EndTabItem();
    }

    if (tip("Packs de texturas HD y captura de texturas para editarlas.",
                  "HD texture packs and texture capture to edit them.") &&
        begin_tab(tab_label(ICON_TEXTURES, i18n::T("Texturas", "Textures"), "tab_tex"))) {
      DrawTexturesTab();
      ImGui::EndTabItem();
    }
    if (tip("Personajes nuevos importados de otros juegos (necesita el Kit de modding para crearlos).",
                  "New characters imported from other games (creating them needs the modding kit).") &&
        begin_tab(tab_label(ICON_CHARS, i18n::T("Personajes", "New characters"), "tab_chars"),
                            want_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawNewCharactersTab();
      ImGui::EndTabItem();
    }
    // la ultima pestana: diagnostico para quien lo necesite (el modo Dev se activa aqui)
    if (tip("Diagnóstico y opciones para quien reporta fallos. No hace falta tocarlo.",
                  "Diagnostics and options for bug reports. You do not need to touch it.") &&
        begin_tab(tab_label(ICON_DEV, i18n::T("Avanzado", "Advanced"), "tab_dev"))) {
      DrawDevTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  if (tab_index_ != kGameTab) saves_loaded_ = false;   // al volver a Juego se releen los guardados

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
                     EffectDisplayName(dbz3::settings::PresentEffect()),
                     LanguageDisplayName(dbz3::settings::UiLanguage()));
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
        : i18n::T("Versión de tus datos del juego: USA o Europa. Si solo tienes una, el launcher la "
                  "elige solo.",
                  "Version of your game data: USA or Europe. If you only have one, the launcher picks it "
                  "by itself."));
  }
  // Ayudas de teclado / mando a la derecha de esta fila (en la de los botones no cabian las 4 del mando:
  // "START Jugar" quedaba debajo de JUGAR, y en aleman aun peor)
  DrawInputHints();

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
    static const char* const kResetNames[] = {
        "dbz3_resolution_scale",
        "dbz3_low_res",
        "dbz3_language",
        "dbz3_region",
        "dbz3_mod_profile",
        "dbz3_fullscreen_mode",
        "dbz3_vsync",
        "dbz3_frame_cap",
        "dbz3_gpu_backend",
        "dbz3_native_2x_msaa",
        "dbz3_anisotropic",
        "dbz3_hd_textures",
        "dbz3_hd_texture_max_texels",
        "dbz3_present_effect",
        "dbz3_fsr_sharpness",
        "dbz3_cas_sharpness",
        "dbz3_vrr",
        "dbz3_fxaa",
        "dbz3_present_dither",
        "dbz3_aspect",
        "dbz3_async_shaders",
        "dbz3_occlusion_queries",
        "dbz3_master_volume",
        "dbz3_mute",
        "dbz3_deadzone",
        "dbz3_rumble",
        "dbz3_input_backend",
        "dbz3_mnk_mode",
        "dbz3_mnk_mouse",
        "dbz3_mnk_sensitivity",
    };
    for (const char* name : kResetNames) rex::cvar::ResetToDefault(name);
    // auto es una eleccion persistible, incluso con sentinel unset en Fase 0.
    rex::cvar::SetFlagByName("dbz3_quality_preset", "auto");
    // dbz3_fsr_quality es compatibilidad legacy; no controla el render.
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
    mods_loaded_ = false; broken_mods_ = -1;
    InvalidateAssets();
  }
  ImGui::SameLine(0, 10);
  {
    // Los ajustes ya se guardan solos (JUGAR, cerrar, cada cambio importante): el boton confirma que se ha hecho.
    const bool just_saved = ImGui::GetTime() < saved_flash_until_;
    if (ui::IconButton(just_saved ? ICON_OK : ICON_SAVE,
                       just_saved ? i18n::T("Guardado", "Saved") : i18n::T("Guardar ajustes", "Save settings"),
                       ImVec2(180, 38))) {
      dbz3::settings::SaveUserSettings();
      saved_flash_until_ = ImGui::GetTime() + 2.0;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", i18n::T("Los ajustes también se guardan solos al pulsar JUGAR o al cerrar.",
                                      "Settings are also saved automatically when you press PLAY or close."));
  }
  ImGui::SameLine(0, 10);
  if (ui::IconButton(ICON_REPAIR, i18n::T("Reparar instalacion", "Repair install"), ImVec2(200, 38))) {
    repair_report_ = dbz3::settings::RepairInstallation();
    repair_popup_ = true;
    InvalidateAssets();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", i18n::T(
        "Revisa los archivos del juego y tus ajustes, y arregla lo que pueda. Úsalo si el juego no "
        "arranca o tus ajustes no se guardan. No toca tus partidas ni tus mods.",
        "Checks the game files and your settings, and fixes what it can. Use it if the game does not "
        "start or your settings are not saved. It does not touch your saves or mods."));
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 20.0f - 320.0f);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 30.0f);
  // PLAY is gated on the assets being found: pressing it with no game data
  // would crash before the guest even starts (P1). The banner above offers the
  // folder picker to fix it.
  ImGui::BeginDisabled(!assets_ready || launching_ || iso_prep_);
  ImGui::PushStyleColor(ImGuiCol_Button, kPlayIdle);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPlayHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPlayActive);
  if (fonts.h2) ImGui::PushFont(fonts.h2);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
  const std::string play_label = std::string(ICON_PLAY) + "   " +
      (launching_ ? i18n::T("Preparando...", "Preparing...") : i18n::T("JUGAR", "PLAY")) + "###play";
  const bool play_pressed = ImGui::Button(play_label.c_str(), ImVec2(320, 62));
  const ImVec2 play_min = ImGui::GetItemRectMin();
  const float play_w = ImGui::GetItemRectSize().x;   // la barra de progreso mide lo que JUGAR
  // antes de pintar el aviso de mods: despues el "ultimo item" ya no es JUGAR
  const bool play_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  ImGui::PopStyleVar();
  if (fonts.h2) ImGui::PopFont();
  // Jugar sin mods (vanilla), solo esta vez: reinicia el juego con una carpeta de mods vacia; los mods del jugador ni se
  // tocan ni se mueven, y el siguiente arranque normal los tiene como estaban. Util para pruebas y como respaldo del online.
  if (!dbz3::VanillaSession() && !launching_ && !iso_prep_) {
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(play_min.x, play_min.y + 66.0f));
    if (ImGui::SmallButton(i18n::T("Jugar vanilla (sin mods, solo esta vez)", "Play vanilla (no mods, this time only)"))) {
      dbz3::settings::SaveUserSettings();
      dbz3::RelaunchVanilla("", false);
    }
    ImGui::SetCursorScreenPos(keep);
  } else if (dbz3::VanillaSession()) {
    ImGui::SetCursorScreenPos(ImVec2(play_min.x, play_min.y + 66.0f));
    ImGui::TextColored(kGold, "%s", i18n::T("Sesion vanilla: sin mods (tus mods no se tocan)", "Vanilla session: no mods (your mods are untouched)"));
  }
  if (!launching_ && !iso_prep_error_.empty() && dbz3::settings::IsIsoMode()) {
    // ISO + mods: la copia de los .afs fallo (espacio, ISO ilegible): se dice por que y el siguiente JUGAR juega sin mods
    const std::string warn = std::string("! ") + i18n::T("Mods desde la ISO: ", "Mods from the ISO: ") + iso_prep_error_ +
                             i18n::T(". JUGAR otra vez = sin mods.", ". PLAY again = without mods.");
    TextAbovePlay(play_min, 4.0f, kGold, warn.c_str());
  } else if (!launching_ && dbz3::settings::IsIsoMode() && !dbz3::settings::IsoDataCacheValid()) {
    TextAbovePlay(play_min, 4.0f, kInfo,
                  i18n::T("Desde la ISO: la primera vez se preparan sus datos para mods y extras (~0,5 GB, segundos).",
                          "From the ISO: the first time its data is prepared for mods and extras (~0.5 GB, seconds)."));
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
    // ImGui exige un item tras mover el cursor; sin el salia "MESSAGE FROM DEAR IMGUI ...
    // SetCursorPos() to extend window boundaries" en cuanto un mod tenia problemas (1.4.4.5).
    // El Dummy vacio se pone en el borde inferior de JUGAR (keep - ItemSpacing): deja el cursor
    // en `keep` sin agrandar el contenido (si no, salia una barra de scroll).
    ImGui::SetCursorPos(ImVec2(keep.x, keep.y - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
  } else if (kit_missing_) {
    TextAbovePlay(play_min, 4.0f, kGold,
                  i18n::T("Personajes nuevos listos. Para cambiarlos hace falta el Kit de modding.",
                          "New characters ready. Changing them needs the modding kit."));
  }
  const bool pad_play = pad_play_;
  pad_play_ = false;
  if (!assets_ready && play_hovered) {
    ImGui::SetTooltip("%s", i18n::T("Faltan los datos del juego: mira el aviso de arriba.",
                                    "Game data missing: see the notice above."));
  }
  if (launching_ || iso_prep_) {
    // Barra de carga sobre JUGAR: roster_build dice que personaje esta montando
    // ("@progreso k/n nombre"); hasta la primera linea, la barra va y viene.
    std::string what;
    float frac = mod_pipeline_.Progress(&what);
    std::string text = i18n::T("Preparando los personajes nuevos, un momento...",
                               "Preparing the new characters, one moment...");
    if (frac >= 0.0f && !what.empty()) text += "  (" + what + ")";
    if (iso_prep_) {   // ISO + mods: la copia de los .afs (solo la primera vez)
      frac = float(iso_prep_->progress.load()) / 1000.0f;
      text = i18n::T("Preparando la ISO para mods y extras (solo la primera vez)...",
                     "Preparing the ISO for mods and extras (first time only)...");
    }
    const ImVec2 bar_min(play_min.x, play_min.y - 10.0f);
    const ImVec2 bar_max(play_min.x + play_w, play_min.y - 4.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    TextAbovePlay(play_min, 14.0f, kGold, text.c_str());
    dl->AddRectFilled(bar_min, bar_max, IM_COL32(255, 255, 255, 40), 3.0f);
    if (frac >= 0.0f) {
      dl->AddRectFilled(bar_min, ImVec2(bar_min.x + play_w * std::max(frac, 0.03f), bar_max.y),
                        ImGui::GetColorU32(kGold), 3.0f);
    } else {
      const float t = float(std::fmod(ImGui::GetTime() * 0.6, 1.0));
      const float x0 = bar_min.x + play_w * std::max(0.0f, t - 0.25f);
      const float x1 = bar_min.x + play_w * std::min(1.0f, t + 0.05f);
      dl->AddRectFilled(ImVec2(x0, bar_min.y), ImVec2(x1, bar_max.y), ImGui::GetColorU32(kGold), 3.0f);
    }
  }
  bool launch_now = false, start_jobs = false;
  // Enter juega, salvo mientras se escribe en un campo de texto (buscador, nombre...).
  // Enter / START no juegan con una ventana o un desplegable abiertos (Enter en "Restablecer?" lanzaba el juego)
  const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  // ni mientras se asigna una tecla en Controles (Enter puede ser la tecla elegida)
  if (tab_index_ != 3) kb_listen_.clear();
  const bool kb_busy = !kb_listen_.empty() || kb_listen_wait_ < 0;
  if (kb_listen_wait_ < 0) kb_listen_wait_ = 0;
  const bool enter = !popup && !kb_busy && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
                     !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive();
  if (!launching_ && (play_pressed || (assets_ready && ((pad_play && !popup) || enter)))) {
    dbz3::settings::SaveUserSettings();
    dbz3::settings::ApplyUserSettingsToSdk();
    dbz3::settings::ApplyRuntimeSettingsToSdk(true);
    dbz3::settings::ApplyWindowSizeToSdk();
    REXLOG_INFO("dbz3: launcher Play pressed, starting game");
    // ISO + mods: antes de todo se copian (una vez) los .afs que pueden llevar mods; el override por entrada necesita
    // archivos reales y el disco montado los sirve desde esa copia (region.cpp). Si ya fallo una vez, se juega sin mods.
    // (siempre desde la ISO, no solo con mods: el arte nativo de los modos nuevos tambien lee esos archivos)
    const bool iso_mods = dbz3::settings::IsIsoMode() && iso_prep_error_.empty();
    const bool voices = ModPipeline::HasCharacterSources() && ModPipeline::RosterToolAvailable();
    if (iso_mods && dbz3::settings::IsoDataPending(voices) != 0) {
      StartIsoPrep(true);
    } else {
      start_jobs = true;
    }
    iso_prep_error_.clear();
  }
  if (iso_prep_ && iso_prep_->done) {
    if (!iso_prep_->ok) iso_prep_error_ = iso_prep_->error.empty() ? "?" : iso_prep_->error;
    if (iso_prep_->ok && iso_prep_->then_play) start_jobs = true;
    if (iso_prep_->then_play) launching_ = false;
    iso_prep_.reset();
    mods_loaded_ = false;
  }
  if (start_jobs) {
    // Personajes nuevos: regenerar el mod combinado "_roster" si cambio algo (instantaneo si esta al dia). Desde la
    // ISO, con la copia de sus .afs (UsDir).
    const bool data_ok = !dbz3::settings::IsIsoMode() || dbz3::settings::IsoDataCacheValid();
    if (data_ok && ModPipeline::HasCharacterSources()) {
      if (ModPipeline::RosterToolAvailable()) {
        // en segundo plano: la ventana sigue viva y dice que esta montando (antes se congelaba);
        // si otra tarea sigue en marcha, el montaje arranca cuando acabe (sin congelar la ventana)
        roster_queued_ = true;
        launching_ = true;
      } else {
        // v1.4.1: el pack de personajes trae el _roster ya montado y funciona sin el kit
        // de modding; antes se lanzaba igualmente roster_build.py y cada partida dejaba
        // "can't open file ... roster_build.py" (exit 2) en el log.
        REXLOG_INFO("dbz3: personajes nuevos: se usa el _roster ya montado (sin el kit de "
                    "modding no se reconstruye; para cambiar personajes instala el kit)");
      }
    }
    // R8: despues, los escenarios nuevos desde los juegos de PS2 del jugador (campos de IW, B1/B2). Se intenta
    // SIEMPRE: BuildStages deja el motivo en la pestana Mods aunque falten datos, la ISO o el Kit (G11).
    // `data_ok` solo decide los personajes; que falle su montaje no puede impedir el diagnostico de escenarios.
    stages_pending_ = true;
    roster_ran_ = launching_;
    if (stages_pending_) launching_ = true;
    if (!launching_) launch_now = true;
  }
  if (roster_queued_ && !mod_pipeline_.IsRunning()) {
    roster_queued_ = false;
    mod_pipeline_.Wait();
    mod_pipeline_.BuildRoster();
  }
  if (launching_ && stages_running_ && !mod_pipeline_.IsRunning()) {
    // R8: acabo el paso de escenarios. Si los personajes fallaron, NO se juega: se muestra su aviso con el
    // detalle de escenarios ya recogido (G11: antes el fallo de personajes impedia correr este paso).
    mod_pipeline_.Wait();
    stages_running_ = false;
    launching_ = false;
    REXLOG_INFO("dbz3: escenarios de PS2: {}", mod_pipeline_.Output());
    if (roster_failed_) {
      repair_report_ += "\n\n" + mod_pipeline_.Output();
      repair_popup_ = true;
      launch_now = false;
    } else {
      launch_now = true;
    }
  }
  if (launching_ && !stages_running_ && !roster_ran_ && stages_pending_ && !mod_pipeline_.IsRunning()) {
    stages_pending_ = false;   // sin personajes que montar: directamente los escenarios
    stages_running_ = true;
    mod_pipeline_.Wait();
    mod_pipeline_.BuildStages();
  }
  if (launching_ && roster_ran_ && !roster_queued_ && !mod_pipeline_.IsRunning()) {
    mod_pipeline_.Wait();
    roster_ran_ = false;
    REXLOG_INFO("dbz3: personajes nuevos: {}", mod_pipeline_.Output());
    launching_ = false;
    // A4: exit 0 no basta: el _roster publicado tiene que estar completo
    const bool roster_ok = mod_pipeline_.State() == ModPipeline::JobState::kOk && ModPipeline::RosterArtifactsOk();
    launch_now = roster_ok;
    if (stages_pending_) {   // R8: los escenarios van SIEMPRE, salga bien o mal el montaje de personajes
      stages_pending_ = false;
      stages_running_ = true;
      launching_ = true;
      launch_now = false;
      if (!roster_ok) {   // G11: se guarda el aviso; se enseña al acabar los escenarios (con su detalle)
        roster_failed_ = true;
        repair_report_ = i18n::T("No se pudieron preparar los personajes nuevos. Puedes jugar con los de la última "
                                 "vez (botón de abajo) o desactivar el personaje que acabas de añadir en la "
                                 "pestaña Mods y volver a pulsar JUGAR.",
                                 "The new characters could not be prepared. You can play with the previous ones "
                                 "(button below) or turn off the character you just added in the Mods tab and "
                                 "press PLAY again.");
      }
      mod_pipeline_.BuildStages();
    }
    if (!roster_ok && !stages_running_) {
      repair_report_ = i18n::T("No se pudieron preparar los personajes nuevos. Puedes jugar con los de la última "
                               "vez (botón de abajo) o desactivar el personaje que acabas de añadir en la "
                               "pestaña Mods y volver a pulsar JUGAR.",
                               "The new characters could not be prepared. You can play with the previous ones "
                               "(button below) or turn off the character you just added in the Mods tab and "
                               "press PLAY again.");
      repair_report_ += "\n\n" + mod_pipeline_.Output();
      repair_popup_ = true;
      roster_failed_ = true;   // el popup ofrece jugar con el montaje anterior (no se toco: publicacion atomica)
    }
  }
  if (play_previous_roster_) {
    play_previous_roster_ = false;
    REXLOG_WARN("dbz3: personajes nuevos: se juega con el montaje anterior tras un fallo");
    launch_now = true;
  }
  if (launch_now) {
    dbz3::SessionStart();   // si el juego se cierra de golpe, el proximo launcher lo sabra
    Close();
    if (on_play_) {
      on_play_();
    }
  }
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();

  // Launcher que se cura solo: la ultima partida se cerro de golpe con mods nuevos desde la
  // ultima que fue bien -> se ofrece desactivarlos (reversible: marca .disabled, nada se borra).
  if (!session_checked_) {
    session_checked_ = true;
    try { session_suspects_ = dbz3::SessionSuspects(); } catch (const std::exception&) {}
    if (!session_suspects_.empty()) ImGui::OpenPopup("###ultima_partida");
  }
  if (ImGui::BeginPopupModal((std::string(i18n::T("Mods", "Mods")) + "###ultima_partida").c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(kDragonOrange, "%s", i18n::T("La ultima partida se cerro de golpe",
                                                    "The last game closed unexpectedly"));
    ImGui::Separator();
    ImGui::TextWrapped("%s", i18n::T(
        "Estos mods son nuevos desde la ultima partida que fue bien. Puedes desactivarlos para jugar "
        "seguro (no se borra nada: se vuelven a activar en la pestana Mods).",
        "These mods are new since the last game that went fine. You can disable them to play safe "
        "(nothing is deleted: re-enable them in the Mods tab)."));
    for (const auto& m : session_suspects_) ImGui::BulletText("%s", m.c_str());
    ImGui::Spacing();
    if (ImGui::Button(i18n::T("Desactivarlos", "Disable them"), ImVec2(200, 0))) {
      for (const auto& m : session_suspects_) dbz3::SetModEnabled(m, false);
      dbz3::SessionForget();
      mods_status_ = i18n::T("Mods nuevos desactivados tras el cierre inesperado.",
                             "New mods disabled after the unexpected close.");
      mods_loaded_ = false; broken_mods_ = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Dejarlos como estan", "Keep them"), ImVec2(200, 0))) {
      dbz3::SessionForget();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Repair report (modal). Opened by the footer button or the one-shot cvar.
  if (repair_popup_) {
    ImGui::OpenPopup("###repair_result");
    repair_popup_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(i18n::T("Aviso", "Notice")) + "###repair_result").c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(kDragonOrange, "%s",
                       roster_failed_ ? i18n::T("Personajes nuevos", "New characters")
                                      : i18n::T("Resultado de la reparacion", "Repair result"));
    ImGui::Separator();
    // primero lo que entiende un jugador; el informe tecnico (trazas de Python...) plegado
    const size_t cut = repair_report_.find("\n\n");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 620.0f);
    ImGui::TextUnformatted(repair_report_.substr(0, cut).c_str());
    ImGui::PopTextWrapPos();
    if (cut != std::string::npos && ImGui::TreeNode(i18n::T("Detalle tecnico", "Technical details"))) {
      ImGui::BeginChild("##repair_body", ImVec2(620, 260), true);
      ImGui::TextUnformatted(repair_report_.c_str() + cut + 2);
      ImGui::EndChild();
      ImGui::TreePop();
    }
    ImGui::Spacing();
    if (roster_failed_) {   // A4/F0-12: opcion explicita de jugar con lo ultimo que funciono
      if (ImGui::Button(i18n::T("Jugar con los personajes de antes", "Play with the previous characters"),
                        ImVec2(300, 0))) {
        roster_failed_ = false;
        play_previous_roster_ = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
    }
    if (ImGui::Button(i18n::T("Cerrar", "Close"), ImVec2(120, 0)) || CloseKeyPressed()) {
      roster_failed_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  DrawFirstSteps();   // instalacion nueva: antes que las novedades (que en ese caso no salen)
  DrawNews();   // despues de los otros avisos: si hay uno abierto, espera su turno
  ImGui::End();
}

void LauncherDialog::DrawNews() {
  const NewsBlock* news = CurrentNews();
  if (!news) return;
  if (!news_checked_) {
    news_checked_ = true;
    const bool seen = rex::cvar::Query<std::string>("dbz3_news_seen") == news->version;
    if (std::getenv("DBZ3_TEST_NEWS")) {
      news_pending_ = true;
    } else if (!seen && !in_game_ && !std::getenv("DBZ3_NO_DIALOGS")) {
      // primera instalacion (sin ajustes previos): no hay nada "nuevo" que contar
      if (dbz3::settings::LastConfigLoadState() == dbz3::settings::ConfigLoadState::kMissing)
        rex::cvar::SetFlagByName("dbz3_news_seen", news->version);
      else
        news_pending_ = true;
    }
  }
  if (news_pending_ && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    news_pending_ = false;
    ImGui::OpenPopup("###news");
  }
  if (!ImGui::BeginPopupModal("###news", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  {
    ui::FontScope f(ui::GetFonts().h2);
    ImGui::TextColored(kDragonOrange, "%s", ICON_STAR);
    ImGui::SameLine();
    ImGui::Text(i18n::T("Novedades de la version %s", "What's new in version %s"), news->version);
  }
  ImGui::Separator();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 640.0f);
  for (int i = 0; i < news->count; ++i) {
    const NewsItem& item = news->items[i];
    if (item.section_es) ui::SectionTitle(i18n::T(item.section_es, item.section_en));
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextUnformatted(i18n::T(item.es, item.en));
  }
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::Separator();
  bool close = CloseKeyPressed();
  ImGui::PushStyleColor(ImGuiCol_Button, kPlayIdle);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPlayHovered);
  if (ui::IconButton(ICON_OK, i18n::T("Entendido", "Got it"), ImVec2(200, 36))) close = true;
  ImGui::PopStyleColor(2);
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine();
  if (ui::IconButton(ICON_GLOBE, i18n::T("Todas las notas (web)", "All release notes (web)"), ImVec2(0, 36)))
    OpenUrl("https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases");
  if (close) {
    rex::cvar::SetFlagByName("dbz3_news_seen", news->version);
    dbz3::settings::SaveUserSettings();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Primeros pasos: lo minimo para jugar, con el estado real de cada paso (datos del juego, idioma).
void LauncherDialog::DrawFirstSteps() {
  if (!first_steps_checked_) {
    first_steps_checked_ = true;
    if (std::getenv("DBZ3_TEST_FIRST")) {
      first_steps_pending_ = true;
    } else if (!rex::cvar::Query<bool>("dbz3_first_steps_seen") && !in_game_ && !std::getenv("DBZ3_NO_DIALOGS") &&
               dbz3::settings::LastConfigLoadState() == dbz3::settings::ConfigLoadState::kMissing) {
      first_steps_pending_ = true;   // instalacion nueva: se abre sola una vez
    }
  }
  if (first_steps_pending_ && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    first_steps_pending_ = false;
    ImGui::OpenPopup("###first_steps");
  }
  if (!ImGui::BeginPopupModal("###first_steps", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  const ui::Fonts& fonts = ui::GetFonts();
  {
    ui::FontScope f(fonts.h2);
    ImGui::TextColored(kDragonOrange, "%s", ICON_INFO);
    ImGui::SameLine();
    ImGui::TextUnformatted(i18n::T("Primeros pasos", "First steps"));
  }
  ImGui::Separator();
  const float wrap = 640.0f;
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
  auto step = [&](int n, const char* title) {
    ImGui::Spacing();
    ui::FontScope b(fonts.bold);
    ImGui::TextColored(kGold, "%d.", n);
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
  };
  step(1, i18n::T("Tu copia del juego", "Your copy of the game"));
  if (assets_.assets_ready) {
    ImGui::TextColored(kOk, "%s  %s", ICON_OK,
                       i18n::T("Encontrada. No hay que hacer nada más.", "Found. Nothing else to do."));
  } else {
    ImGui::TextColored(kGold, "%s  %s", ICON_WARN, i18n::T("Aún no la encuentro.", "Not found yet."));
    ui::Hint(i18n::T("Pon la ISO de Budokai 3 HD (Xbox 360) junto a dbz3.exe, o pulsa «Carpeta extraída» o "
                     "«Imagen ISO» (arriba a la derecha) y elígela. El juego no viene incluido: usa tu copia.",
                     "Put the Budokai 3 HD ISO (Xbox 360) next to dbz3.exe, or press 'Extracted folder' or "
                     "'ISO image' (top right) and pick it. The game is not included: use your own copy."));
  }
  step(2, i18n::T("Idioma", "Language"));
  {
    int lang_idx = 0;
    for (int i = 0; i < 7; i++)
      if (dbz3::settings::UiLanguage() == kLanguageIds[i]) lang_idx = i;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("##first_lang", &lang_idx, kLanguageItems, 7)) {
      dbz3::settings::SetLanguage(kLanguageIds[lang_idx]);
      dbz3::settings::SaveUserSettings();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", i18n::T("El launcher y los textos del juego.", "The launcher and the game texts."));
  }
  step(3, i18n::T("Mando o teclado", "Controller or keyboard"));
  ui::Hint(i18n::T("Conecta el mando y listo; sin mando se juega con el teclado (teclas en la pestaña "
                   "Controles). En esta ventana, con mando: LB/RB cambian de pestaña, A elige, B vuelve y "
                   "START juega.",
                   "Plug in your controller and you are set; without one, the keyboard works (keys in the "
                   "Input tab). In this window, with a controller: LB/RB switch tabs, A selects, B goes back "
                   "and START plays."));
  step(4, i18n::T("Extras (opcional)", "Extras (optional)"));
  ui::Hint(i18n::T("En Nova's Mods instalas personajes, historias y texturas HD con un clic; en Mods los "
                   "activas o los quitas. Los modos nuevos (HUB, Tag Team, Online) se encienden o apagan en "
                   "la pestaña Juego.",
                   "In Nova's Mods you install characters, stories and HD textures in one click; in Mods you "
                   "turn them on or off. The new modes (HUB, Tag Team, Online) are switched on or off in "
                   "the Game tab."));
  ImGui::Spacing();
  ImGui::TextUnformatted(i18n::T("Después, pulsa JUGAR. Esta ayuda sigue en el botón «Primeros pasos» de arriba.",
                                 "Then press PLAY. This help stays in the 'First steps' button at the top."));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::Separator();
  bool close = CloseKeyPressed();
  ImGui::PushStyleColor(ImGuiCol_Button, kPlayIdle);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPlayHovered);
  if (ui::IconButton(ICON_OK, i18n::T("Entendido", "Got it"), ImVec2(200, 36))) close = true;
  ImGui::PopStyleColor(2);
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine();
  if (ui::IconButton(ICON_DOWNLOAD, i18n::T("Ver Nova's Mods", "Open Nova's Mods"), ImVec2(0, 36))) {
    request_tab_ = 3;
    close = true;
  }
  if (close) {
    rex::cvar::SetFlagByName("dbz3_first_steps_seen", "true");
    dbz3::settings::SaveUserSettings();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Ficha de un mod: lo que se sabe de el y lo que se puede hacer, en un solo sitio.
void LauncherDialog::DrawModCard() {
  if (!mod_card_.empty()) {
    card_mod_ = mod_card_;
    mod_card_.clear();
    ImGui::OpenPopup("###mod_card");
  }
  if (!ImGui::BeginPopupModal("###mod_card", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  const auto it = std::find_if(mods_cache_.begin(), mods_cache_.end(),
                               [&](const dbz3::ModInfo& m) { return m.name == card_mod_; });
  if (it == mods_cache_.end()) {   // desinstalado o renombrado mientras estaba abierta
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  dbz3::ModInfo& mod = *it;
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600.0f);
  {
    ui::FontScope f(ui::GetFonts().h2);
    ImGui::TextUnformatted(mod.display_name.empty() ? mod.name.c_str() : mod.display_name.c_str());
  }
  ImGui::SameLine(0.0f, 12.0f);
  DrawBadge(ModTypeLabelText(mod.type), ModTypeColorVec(mod.type));
  ui::Hint(("mods/" + mod.name).c_str());
  ImGui::Separator();
  bool on = mod.enabled;
  {
    ui::FontScope b(ui::GetFonts().bold);
    if (ImGui::Checkbox(i18n::T("Activado", "Enabled"), &on)) {
      dbz3::SetModEnabled(mod.name, on);
      if (on) dbz3::StampModVersion(mod.name, dbz3::launcher::CurrentVersion());
      mod.enabled = on;
      mods_loaded_ = false;
      broken_mods_ = -1;
    }
  }
  ImGui::Spacing();
  if (!mod.description.empty()) ImGui::TextUnformatted(mod.description.c_str());
  else ui::Hint(i18n::T("Este mod no trae descripcion.", "This mod has no description."));
  ImGui::Spacing();
  auto field = [](const char* label, const std::string& value) {
    if (value.empty()) return;
    ImGui::TextColored(kTextDim, "%s", label);
    ImGui::SameLine(130.0f);
    ImGui::TextUnformatted(value.c_str());
  };
  field(i18n::T("Autor", "Author"), mod.author);
  field(i18n::T("Version", "Version"), mod.version);
  field(i18n::T("Cambia", "Changes"),
        mod.source.empty() ? mod.target : mod.target.empty() ? mod.source : mod.source + "  ->  " + mod.target);
  field(i18n::T("Archivos", "Files"), std::to_string(mod.file_count));
  for (const auto& [kind, detail] : mod.problems) {
    ImGui::TextColored(kGold, "! %s", ModProblemText(kind, detail).c_str());
    if (kind == dbz3::ModInfo::kNested && ImGui::SmallButton(i18n::T("Arreglar", "Fix"))) {
      mods_status_ = dbz3::FixNestedMod(mod.name)
                         ? std::string(i18n::T("Mod arreglado: ", "Mod fixed: ")) + mod.name
                         : std::string(i18n::T("No se pudo arreglar solo; abre su carpeta: ",
                                               "Could not fix it automatically; open its folder: ")) + mod.name;
      mods_loaded_ = false;
      broken_mods_ = -1;
    }
  }
  for (const auto& c : mods_conflicts_) {
    if (c.winner == mod.name)
      ImGui::TextColored(kGold, i18n::T("Tapa a '%s' en %d cosas.", "Covers '%s' in %d items."), c.loser.c_str(),
                         c.resources);
    else if (c.loser == mod.name)
      ImGui::TextColored(kGold, i18n::T("'%s' lo tapa en %d cosas: desactiva uno si quieres ver el otro.",
                                        "'%s' covers it in %d items: disable one to see the other."),
                         c.winner.c_str(), c.resources);
  }
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::Separator();
  if (ui::IconButton(ICON_FOLDER, i18n::T("Abrir carpeta", "Open folder"))) {
    std::system(("explorer \"" + (dbz3::ModsRoot() / mod.name).string() + "\"").c_str());
  }
  if (mod.type == "personaje") {
    ImGui::SameLine();
    if (ui::IconButton(ICON_CHARS, i18n::T("Editar personaje", "Edit character"))) {
      nc_selected_ = mod.name;
      request_tab_ = 2;
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Cerrar", "Close"), ImVec2(120, 0)) || CloseKeyPressed())
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
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
      static const char* const kAnisoNames[] = {"Off", "1x", "2x", "4x", "8x", "16x"};
      ImGui::TextColored(kTextDim,
                         i18n::T("Ahora: escala %dx  ·  bordes MSAA %s  ·  filtrado %s  ·  %s",
                                 "Now: scale %dx  ·  MSAA edges %s  ·  filtering %s  ·  %s"),
                         p_scale, p_msaa ? i18n::T("sí", "on") : i18n::T("no", "off"),
                         kAnisoNames[std::clamp(p_aniso, 0, 5)], EffectDisplayName(p_eff));
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
          "El juego se dibuja a más resolución y luego se reduce: bordes mucho más limpios, pero la "
          "tarjeta gráfica trabaja mucho más (cada paso cuesta más o menos el doble). Empieza por 1x.",
          "The game is drawn at a higher resolution and then scaled down: much cleaner edges, but the "
          "graphics card works much harder (each step costs roughly twice as much). Start at 1x."));
    {   // issue #29: 480p para PCs flojos (el juego dibuja a 640x480 el solo con un modo de video no HD)
      bool low = dbz3::settings::LowResEnabled();
      if (ImGui::Checkbox(i18n::T("Resolucion baja 480p (PC flojo, reinicia el juego)",
                                  "Low resolution 480p (weak PC, restart the game)"), &low)) {
        dbz3::settings::SetLowResEnabled(low);
        dbz3::settings::SaveUserSettings();
      }
      ui::Tip(i18n::T(
            "El juego se dibuja a 640x480 (4:3, como en una Xbox 360 con tele normal) y se estira a la ventana. "
            "Va bastante mas ligero en tarjetas graficas integradas. Beta: el menu y las barras de vida cambian de forma.",
            "The game is drawn at 640x480 (4:3, like an Xbox 360 on a standard TV) and stretched to the window. "
            "Much lighter on integrated GPUs. Beta: menus and health bars change layout."));
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
    const char* hdtex_items[] = {
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
    if (ImGui::Checkbox(i18n::T("Bordes suaves (MSAA 2x)", "Smooth edges (2x MSAA)"), &msaa)) {
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
    ui::Tip(i18n::T("Suelo y paredes nítidos aunque estén lejos. 16x casi no cuesta nada.",
                    "Sharp floors and walls even far away. 16x costs almost nothing."));

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

    // Aspecto en pantallas anchas (#35): el juego HD siempre se dibuja a 16:9
    // (1280x720) en su escena 3D y su HUD; en un monitor 21:9/32:9 el presentador
    // decide que hacer con el ancho de mas. "Mantener" = 16:9 centrado con
    // franjas (por defecto, nada deformado); "Estirar" = llenar toda la ventana,
    // estirando a lo ancho (sin franjas, pero los personajes salen mas anchos).
    // Solo cambia la presentacion; no toca archivos del juego ni el HUD propio.
    {
      const char* aspect_items[] = {i18n::T("Mantener proporcion (franjas)", "Keep ratio (bars)"),
                                    i18n::T("Estirar (llenar, deformado)", "Stretch (fill, distorted)")};
      int aspect_idx = dbz3::settings::AspectMode() == "stretch" ? 1 : 0;
      ui::RowLabel(i18n::T("Aspecto en pantallas anchas", "Wide-screen aspect"));
      if (ImGui::Combo("##DrawVideoTab_aspect", &aspect_idx, aspect_items, 2)) {
        dbz3::settings::SetAspectMode(aspect_idx == 1 ? "stretch" : "keep");
        // Se aplica en caliente; se guarda ya para que sobreviva a un JUGAR/cierre
        // sin pulsar "Guardar ajustes" (como el resto de ajustes de Video).
        dbz3::settings::ApplyRuntimeSettingsToSdk(false);
        dbz3::settings::SaveUserSettings();
      }
      ui::Tip(i18n::T(
            "En monitores muy anchos (21:9, 32:9) el juego, que es 16:9, puede salir "
            "con franjas a los lados o estirado. Mantener las franjas es lo recomendado: "
            "se ve en su proporcion. Estirar llena la pantalla, pero la imagen sale "
            "deformada a lo ancho.",
            "On very wide monitors (21:9, 32:9) the game, which is 16:9, can show bars "
            "on the sides or be stretched. Keeping the bars is recommended: it stays in "
            "proportion. Stretching fills the screen, but the picture is stretched "
            "sideways."));
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
                 i18n::T("Direct3D 12 es el recomendado. Al cambiarlo, reinicia el juego.",
                         "Direct3D 12 is recommended. Restart the game after changing it."));
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

    int lang = dbz3::settings::UiLanguage();
    int lang_idx = 0;
    for (int i = 0; i < 7; i++) {
      if (lang == kLanguageIds[i]) lang_idx = i;
    }
    ui::RowLabel(i18n::T("Idioma del launcher y del juego", "Launcher and game language"));
    if (ImGui::Combo("##DrawVideoTab_5",
                     &lang_idx, kLanguageItems, 7)) {
      dbz3::settings::SetLanguage(kLanguageIds[lang_idx]);
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(in_game_ ? i18n::T("El launcher cambia al momento; los textos del juego, la próxima vez que lo abras.",
                               "The launcher changes right away; the game texts, the next time you open it.")
                     : i18n::T("El launcher cambia al momento; los textos del juego, al pulsar JUGAR.",
                               "The launcher changes right away; the game texts, when you press PLAY."));
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
                   i18n::T("Dibuja a menos resolución y el escalado la recupera: más fluido en equipos justos.",
                           "Draws at a lower resolution and the upscaler restores it: smoother on modest PCs."));
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
      ui::RowLabel(i18n::T("Nitidez", "Sharpness"));
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

  // Experimental relight (SSAO on the 3D scene, runtime dbz3_relight_level).
  ImGui::Spacing();
  PushSectionHeader(i18n::T("Iluminación (experimental)", "Lighting (experimental)"));
  {
    const char* relight_items[] = {i18n::T("No", "Off"), i18n::T("Baja", "Low"),
                                   i18n::T("Alta", "High")};
    int relight = dbz3::settings::Relight();
    ui::RowLabel(i18n::T("Sombras de contacto (SSAO)", "Contact shadows (SSAO)"));
    if (ImGui::Combo("##relight", &relight, relight_items, 3)) {
      dbz3::settings::SetRelight(relight);
      dbz3::settings::ApplyRuntimeSettingsToSdk(false);
      dbz3::settings::SaveUserSettings();
    }
    ui::Tip(i18n::T(
        "Oscurece un poco los rincones, los pliegues de la ropa y donde los personajes tocan el "
        "suelo. Solo en la escena 3D: la interfaz y los menús no cambian. Solo con Direct3D 12. "
        "Experimental: cuesta rendimiento (Alta, más). Si el juego va a tirones, bájalo o apágalo.",
        "Slightly darkens corners, clothing folds and where the characters touch the ground. "
        "Only on the 3D scene: the HUD and the menus don't change. Direct3D 12 only. "
        "Experimental: costs performance (High, more). If the game stutters, lower it or turn "
        "it off."));
    ImGui::SameLine(0.0f, 10.0f);
    ui::Pill(ICON_WARN, i18n::T("Experimental: cuesta rendimiento", "Experimental: costs performance"),
             kWarn);
  }

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
  // En porcentaje (antes 0.00-1.00: "0 = silencio, 1 = maximo"); el ajuste guardado sigue siendo 0-1.
  double master_pct = dbz3::settings::MasterVolume() * 100.0;
  ui::RowLabel(i18n::T("Volumen general", "Master volume"));
  if (SliderD("##DrawAudioTab_1", &master_pct, 0.0, 100.0, "%.0f %%")) {
    dbz3::settings::SetMasterVolume(master_pct / 100.0);
    dbz3::settings::ApplyRuntimeSettingsToSdk(false);
  }

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
      "El juego mezcla todo el sonido en una sola pista, así que aquí solo se ajusta el volumen general. "
      "Las voces (japonés o inglés) se eligen dentro del juego.",
      "The game mixes all audio into a single stream, so only the master volume "
      "or silence can be adjusted. Voice tracks (Japanese/English) are selected "
      "in-game."));

  ImGui::EndChild();
}

void LauncherDialog::DrawInputTab() {
  ImGui::BeginChild("##input_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);

  PushSectionHeader(i18n::T("Mando", "Controller"));

  const char* backend_items[] = {i18n::T("Mandos de Xbox (XInput, recomendado)", "Xbox controllers (XInput, recommended)"),
                                 i18n::T("Cualquier mando (SDL: PlayStation, Switch...)",
                                         "Any controller (SDL: PlayStation, Switch...)")};
  static const char* backend_values[] = {"xinput", "sdl"};
  std::string backend = dbz3::settings::InputBackend();
  int backend_idx = backend == "sdl" ? 1 : 0;
  ui::RowLabel(i18n::T("Sistema de mandos", "Controller system"));
  if (ImGui::Combo("##DrawInputTab_1", &backend_idx,
                   backend_items, 2)) {
    dbz3::settings::SetInputBackend(backend_values[backend_idx]);
  }
  ui::Tip(i18n::T(
        "Con un mando de Xbox deja la primera opción. Si tienes otro mando (PlayStation, Switch...) "
        "también funciona solo; elige la segunda si no lo detecta. Al cambiarlo, reinicia el juego.",
        "With an Xbox controller keep the first option. Other controllers (PlayStation, Switch...) also "
        "work on their own; pick the second one if yours is not detected. Restart the game after changing it."));

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
  ui::Tip(i18n::T("Cuánto hay que mover el stick antes de que el juego lo note. Súbelo si el personaje se "
                  "mueve solo.",
                  "How far the stick must move before the game notices. Raise it if your fighter moves on "
                  "its own."));

  bool rumble = dbz3::settings::RumbleEnabled();
  if (ImGui::Checkbox(i18n::T("Activar vibracion", "Enable vibration"), &rumble)) {
    dbz3::settings::SetRumbleEnabled(rumble);
  }

  ImGui::Spacing();
  PushSectionHeader(i18n::T("Teclado / Raton", "Keyboard / Mouse"));

  bool mnk = dbz3::settings::MnkMode();
  if (ImGui::Checkbox(i18n::T("Jugar con teclado (y ratón)",
                              "Play with keyboard (and mouse)"), &mnk)) {
    dbz3::settings::SetMnkMode(mnk);
  }
  ui::Tip(i18n::T(
        "El teclado hace de mando. Necesario para jugar sin mando; las teclas de cada botón están abajo.",
        "The keyboard acts as a controller. Needed to play without one; the key for each button is below."));

  bool mnk_mouse = dbz3::settings::MnkMouse();
  if (ImGui::Checkbox(i18n::T("Usar el raton para el stick derecho",
                              "Use mouse for the right stick"), &mnk_mouse)) {
    dbz3::settings::SetMnkMouse(mnk_mouse);
  }
  ui::Tip(i18n::T(
        "Mueve el stick derecho con el ratón (además de con sus teclas).",
        "Moves the right stick with the mouse (as well as with its keys)."));

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
  ImGui::Text("%s", i18n::T("Teclas de cada botón", "Key for each button"));
  ui::Tip(i18n::T(
        "Pulsa el botón del teclado que hay junto a cada casilla y después la tecla que quieras (Esc = "
        "cancelar). También se puede escribir: varias teclas separadas por comas (Space,W); vacío = sin "
        "tecla; LMB/RMB/MMB = botones del ratón.",
        "Press the keyboard button next to each box and then the key you want (Esc = cancel). You can also "
        "type: several keys separated by commas (Space,W); empty = no key; LMB/RMB/MMB = mouse buttons."));

  // Asignar pulsando: la siguiente tecla (no un clic) pasa a ser la del boton. Esc o un clic cancelan.
  if (!kb_listen_.empty()) {
    if (kb_listen_wait_ > 0) {
      --kb_listen_wait_;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
               ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      kb_listen_.clear();
    } else {
      for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
        if (const char* name = ImGuiKeyToBindName(static_cast<ImGuiKey>(k))) {
          dbz3::settings::SetKeybind(kb_listen_, name);
          dbz3::settings::SaveUserSettings();
          kb_listen_.clear();
          kb_listen_wait_ = -1;   // esta pulsacion no cuenta para nada mas (Enter no lanza el juego)
          break;
        }
      }
    }
  }

  // Three-column grid so all 24 keybinds fit in the 720p window without
  // scrolling (compact one-screen layout).
  {
    const float kb_w =
        ImGui::GetContentRegionAvail().x / 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f / 3.0f;
    const float btn_w = ImGui::GetFrameHeight() + 4.0f;
    int kb_col = 0;
    auto draw_kb = [&](const char* name) {
      ImGui::PushID(name);
      std::string kb_val = dbz3::settings::Keybind(name);
      const char* kb_lbl = ButtonGlyph(glyphs, name);
      const float kb_lw = ImGui::CalcTextSize(kb_lbl).x;
      const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
      float kb_iw = kb_w - kb_lw - btn_w - sp * 2.0f - 6.0f;
      if (kb_iw < 40.0f) kb_iw = 40.0f;
      const bool listening = kb_listen_ == name;
      if (listening) {
        ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentSoft);
        ImGui::Button(i18n::T("Pulsa una tecla...", "Press a key..."), ImVec2(kb_iw, 0.0f));
        ImGui::PopStyleColor();
      } else {
        ImGui::SetNextItemWidth(kb_iw);
        DrawKeybind("##kb", kb_val);
        if (dbz3::settings::Keybind(name) != kb_val) dbz3::settings::SetKeybind(name, kb_val);
      }
      ImGui::SameLine(0.0f, sp);
      if (ImGui::Button(ICON_KEYBOARD, ImVec2(btn_w, 0.0f))) {
        kb_listen_ = name;
        kb_listen_wait_ = 2;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", i18n::T("Asignar pulsando una tecla", "Assign by pressing a key"));
      ImGui::SameLine(0.0f, sp);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(kb_lbl);
      ImGui::PopID();
      if (kb_col % 3 != 2) ImGui::SameLine();
      ++kb_col;
    };
    for (const char* name : {"a", "b", "x", "y", "left_trigger", "right_trigger", "left_shoulder", "right_shoulder",
                             "lstick_up", "lstick_down", "lstick_left", "lstick_right", "lstick_press", "rstick_up",
                             "rstick_down", "rstick_left", "rstick_right", "rstick_press", "dpad_up", "dpad_down",
                             "dpad_left", "dpad_right", "back", "start", "guide"})
      draw_kb(name);
    if (kb_col % 3 != 0) ImGui::NewLine();
  }

  ImGui::Spacing();
  ImGui::TextColored(kTextDim, "%s", i18n::T(
      "Con el juego abierto también puedes cambiar todo esto: pulsa F4.",
      "With the game running you can also change all this: press F4."));

  ImGui::EndChild();
}

namespace {

// Funciones del juego que se pueden quitar (Tag Team, Online, HUB, modos...). Cada modulo registra su
// interruptor como cvar y aparece solo en la pestana Juego: no hace falta tocar el launcher. Las conocidas
// llevan su texto aqui; las demas usan la descripcion del cvar, "texto en espanol | English text".
struct FeatureText {
  const char* cvar;
  const char* es;
  const char* en;
  const char* help_es;
  const char* help_en;
  bool beta;
};
constexpr FeatureText kFeatureTexts[] = {
    {"dbz3_hub_enabled", "HUB", "HUB",
     "Fila HUB en el menu principal: Tag Team, Online, combate rapido y los demas modos juntos. Por ahora "
     "solo con el juego USA (NTSC).",
     "HUB row in the main menu: Tag Team, Online, quick battle and the other modes together. For now only "
     "with the USA (NTSC) game.", true},
    {"dbz3_tag_team_enabled", "Tag Team", "Tag Team",
     "Equipos de 2 o 3 luchadores con relevos (SELECT). Entrada TAG TEAM bajo el menu principal.",
     "Teams of 2 or 3 fighters with switches (SELECT). TAG TEAM entry under the main menu.", true},
    {"dbz3_online_enabled", "Online", "Online",
     "Combates por internet o en red local con un codigo de sala. Entrada ONLINE en el menu Duelo.",
     "Internet or local network fights with a room code. ONLINE entry in the Duel menu.", true},
    {"dbz3_form_select_enabled", "Selector de transformacion", "Transformation selector",
     "LT/RT en la seleccion de personaje: empiezas transformado (con su capsula). En online no se usa.",
     "LT/RT at character select: start transformed (needs its capsule). Not used online.", true},
    // frente escenarios (clima.cpp, gigantes.cpp, cine.cpp): apagados de serie; el jefe gigante es un numero
    // (escala: casilla + deslizador)
    {"dbz3_clima", "Clima vivo", "Living weather",
     "El cielo del escenario se oscurece poco a poco durante el combate. Solo se ve: no cambia el combate. No se "
     "usa en el online.",
     "The stage sky slowly darkens during the fight. Display only: the fight does not change. Not used "
     "online.", false},
    {"dbz3_jefe_gigante", "Jefe gigante", "Giant boss",
     "El rival que controla la consola sale más grande, como un jefe gigante, y la cámara se abre para verlo. "
     "Solo cambia su tamaño en pantalla. No se usa en el online.",
     "The rival controlled by the console appears bigger, like a giant boss, and the camera widens to show it. "
     "Only its on-screen size changes. Not used online.", false},
    {"dbz3_cine_intro", "Intro de cine", "Cinematic intro",
     "Al empezar cada combate, 10 s de cámara de cine sobre el escenario y los luchadores; A la salta. No se usa "
     "en el online.",
     "When each fight starts, 10 s of cinematic camera over the stage and the fighters; A skips it. Not used "
     "online.", false},
    // combate.cpp (DBZ3/Combate): mismos textos que el menu F1 (quick_settings.cpp), asi fr/de/it salen del mismo sitio
    {"dbz3_combate_aviso", "Aviso de golpe", "Hit warning",
     "Un aro bajo tu barra de vida cuando un golpe del rival va a llegar: es el momento de pulsar la guardia "
     "para el contraataque. Solo se ve: no cambia el combate.",
     "A ring under your health bar when a rival hit is about to land: the moment to press guard for the "
     "counter. Display only: the fight does not change.", false},
    {"dbz3_combate_reglas_v0", "Reglas competitivas v0", "Competitive rules v0",
     "AVISO: cambia el dano de 2 golpes de Goku (patada 50 a 70, adelante + punetazo 35 a 50). Apagado es el "
     "juego original. No se usa en el online.",
     "WARNING: changes the damage of 2 Goku hits (kick 50 to 70, forward + punch 35 to 50). Off is the "
     "original game. Not used online.", false},
    {"dbz3_combate_definitiva_directa", "Definitiva directa", "Direct ultimate",
     "La definitiva sin entrar en el modo hiper: su misma entrada, pagando una vez el ki de su capsula. No se "
     "usa en el online.",
     "The ultimate without entering hyper mode: same input, paying its capsule's ki once. Not used online.", false},
    {"dbz3_combate_sonda", "Sonda del laboratorio", "Lab probe",
     "Guarda cada cuadro y cada golpe del combate en logs/combate_*.csv para medir los golpes (frame data).",
     "Saves every frame and hit of the fight to logs/combate_*.csv to measure moves (frame data).", false},
};

// Categorias que ya tienen su pestana (Video, Audio...): sus cvars no se repiten en Juego.
bool OwnTabCategory(const std::string& c) {
  for (const char* k : {"DBZ3/Video", "DBZ3/Audio", "DBZ3/Input", "DBZ3/Language", "DBZ3/Paths", "DBZ3/Dev",
                        "DBZ3/Launcher"})
    if (c == k) return true;
  return c.rfind("DBZ3/", 0) != 0;
}
bool TestCategory(const std::string& c) { return c == "DBZ3/Pruebas" || c == "DBZ3/Test" || c == "DBZ3/Tests"; }

struct Feature {
  std::string cvar, name, help;
  bool beta = false, test = false, restart = false;
  std::vector<std::string> values;   // cvar de texto con valores permitidos (un combo); vacio = interruptor
  bool scale = false;                // cvar numerico de escala (0 = apagado): casilla + deslizador 100-250 %
};

// Lista de funciones registradas. El registro no cambia tras el arranque: se lee una vez por idioma.
const std::vector<Feature>& Features() {
  static std::vector<Feature> list;
  static std::string lang;
  if (lang == i18n::LangCode()) return list;
  lang = i18n::LangCode();
  list.clear();
  const bool es = lang == "es";
  for (const FeatureText& t : kFeatureTexts) {
    if (const rex::cvar::FlagEntry* f = rex::cvar::GetFlagInfo(t.cvar)) {
      list.push_back({t.cvar, i18n::T(t.es, t.en), i18n::T(t.help_es, t.help_en), t.beta, false,
                      f->lifecycle == rex::cvar::Lifecycle::kRequiresRestart, {},
                      f->type == rex::cvar::FlagType::Double});
    }
  }
  for (const rex::cvar::FlagEntry& f : rex::cvar::GetRegistry()) {
    if (f.is_debug_only || OwnTabCategory(f.category)) continue;
    if (std::any_of(std::begin(kFeatureTexts), std::end(kFeatureTexts),
                    [&](const FeatureText& t) { return f.name == t.cvar; }))
      continue;
    const bool combo = f.type == rex::cvar::FlagType::String && f.constraints.HasAllowedValues();
    if (f.type != rex::cvar::FlagType::Boolean && !combo) continue;
    Feature x;
    x.cvar = f.name;
    x.test = TestCategory(f.category);
    x.restart = f.lifecycle == rex::cvar::Lifecycle::kRequiresRestart;
    if (combo) x.values = f.constraints.allowed_values;
    // "Nombre: ayuda | Name: help" -> la parte del idioma; sin " | ", el texto tal cual
    std::string d = f.description;
    if (const size_t bar = d.find(" | "); bar != std::string::npos) d = es ? d.substr(0, bar) : d.substr(bar + 3);
    x.beta = d.find("(beta)") != std::string::npos;
    if (const size_t colon = d.find(": "); colon != std::string::npos && colon < 48) {
      x.name = d.substr(0, colon);
      x.help = d.substr(colon + 2);
    } else {
      x.name = d.empty() ? f.name : d;
    }
    list.push_back(std::move(x));
  }
  return list;
}

// Una fila: interruptor (o combo) + nombre + BETA + una linea de ayuda debajo. true si cambio.
bool FeatureRow(const Feature& f) {
  ImGui::PushID(f.cvar.c_str());
  bool changed = false;
  if (f.scale) {   // jefe gigante: casilla (0 = apagado) y, encendido, su tamano (125-250 %)
    const double v = rex::cvar::Query<double>(f.cvar);
    bool on = v > 0.0;
    {
      ui::FontScope b(ui::GetFonts().bold);
      changed = ImGui::Checkbox(f.name.c_str(), &on);
    }
    if (changed) rex::cvar::SetFlagByName(f.cvar, on ? "1.75" : "0");
    if (on && !changed) {
      ImGui::SameLine(0.0f, 16.0f);
      int pct = int(v * 100.0 + 0.5);
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::SliderInt("##escala", &pct, 125, 250, "%d %%")) {
        pct = (pct + 12) / 25 * 25;
        rex::cvar::SetFlagByName(f.cvar, std::to_string(pct / 100.0));
        changed = true;
      }
      ImGui::SameLine(0.0f, 8.0f);
      ImGui::TextUnformatted(i18n::T("Tamaño", "Size"));
    }
  } else if (f.values.empty()) {
    bool on = rex::cvar::Query<bool>(f.cvar);
    {
      ui::FontScope b(ui::GetFonts().bold);
      changed = ImGui::Checkbox(f.name.c_str(), &on);
    }
    if (changed) rex::cvar::SetFlagByName(f.cvar, on ? "true" : "false");
  } else {
    ui::RowLabel(f.name.c_str());
    const std::string cur = rex::cvar::Query<std::string>(f.cvar);
    if (ImGui::BeginCombo("##v", cur.c_str())) {
      for (const std::string& v : f.values) {
        if (ImGui::Selectable(v.c_str(), v == cur)) {
          rex::cvar::SetFlagByName(f.cvar, v);
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
  }
  if (f.beta) {
    ImGui::SameLine(0.0f, 10.0f);
    const float pill_h = (ui::GetFonts().sm ? ui::GetFonts().sm->LegacySize : ImGui::GetFontSize()) + 6.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (ImGui::GetFrameHeight() - pill_h) * 0.5f));
    ui::Pill(nullptr, "BETA", ui::kBlue);
  }
  if (!f.help.empty() || f.restart) {
    ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
    std::string help = f.help;
    if (f.restart) help += std::string(help.empty() ? "" : " ") + i18n::T("(al reiniciar el juego)", "(after restarting the game)");
    ui::Hint(help.c_str());
    ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::Dummy(ImVec2(0.0f, 2.0f));
  ImGui::PopID();
  if (changed) dbz3::settings::SaveUserSettings();
  return changed;
}

}  // namespace

// Contenido que sale de las copias de PS2 del jugador (Budokai 1, Budokai 2, Infinite World): estado y un boton para
// elegir cada ISO (pasarela::RegistrarIsoPs2 guarda su ruta en ps2_games; no se copia nada).
void LauncherDialog::DrawPs2Box() {
  namespace Pa = dbz3::pasarela;
  ui::SectionTitle(i18n::T("Contenido de tus juegos de PS2", "Content from your PS2 games"));
  ui::Hint(i18n::T("Algunos modos nuevos usan tu copia de otros juegos de Dragon Ball Z de PlayStation 2. Pulsa «Elegir "
                   "ISO» y busca el archivo .iso de ese juego: no se copia ni se cambia nada, solo se recuerda dónde está.",
                   "Some new modes use your copy of other Dragon Ball Z PlayStation 2 games. Press 'Pick ISO' and find "
                   "that game's .iso file: nothing is copied or changed, it only remembers where it is."));
  struct Juego {
    const char* nombre;
    const char* es;
    const char* en;
  };
  static const Juego kJuegos[3] = {
      {"Dragon Ball Z Budokai (PS2)", "Leyenda de Mr. Satán y saga de Namek", "Mr. Satan's legend and the Namek saga"},
      {"Dragon Ball Z Budokai 2 (PS2)", "Campaña de Budokai 2 (tablero) y Nave de Babidi",
       "Budokai 2 campaign (board) and Babidi's ship"},
      {"Dragon Ball Z Infinite World (PS2)", "Campaña de Infinite World, sus minijuegos y animaciones",
       "Infinite World campaign, its minigames and animations"},
  };
  static double leido = -10;
  static std::string ruta[3];
  if (ImGui::GetTime() - leido > 2.0) {   // mirar ps2_games no cuesta, pero no en cada fotograma
    leido = ImGui::GetTime();
    for (int i = 0; i < 3; ++i) ruta[i] = rex::path_to_utf8(Pa::IsoPs2(i).filename());
  }
  for (int i = 0; i < 3; ++i) {
    ImGui::PushID(i);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kGold, "%s", kJuegos[i].nombre);
    ImGui::SameLine(330);
    if (ruta[i].empty()) {
      ImGui::TextColored(kDragonOrangeDim, "%s %s", ICON_WARN, i18n::T("Falta", "Missing"));
    } else {   // el nombre del archivo va en la linea de debajo (uno largo pisaba el boton)
      ImGui::TextColored(kOk, "%s %s", ICON_OK, i18n::T("Lista", "Ready"));
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 170);
    if (ImGui::Button((std::string(ICON_DISC " ") + i18n::T("Elegir ISO", "Pick ISO")).c_str(), ImVec2(170, 0))) {
      const std::string r = Pa::ElegirIsoPs2(i);
      ps2_msg_ = r == "-" ? std::string() : r.empty() ? std::string(i18n::T("¡Listo! Ya puedes jugarlo.", "Done! You can play it now.")) : r;
      leido = -10;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T("Busca el archivo .iso de ese juego de PS2 (tu copia). No se copia nada.",
                                      "Find that PS2 game's .iso file (your copy). Nothing is copied."));
    }
    ImGui::TextDisabled("   %s%s%s", i18n::T(kJuegos[i].es, kJuegos[i].en), ruta[i].empty() ? "" : "  ·  ",
                        ruta[i].c_str());
    ImGui::PopID();
  }
  if (!ps2_msg_.empty()) ImGui::TextColored(kInfo, "%s", ps2_msg_.c_str());
  ImGui::Spacing();
}

void LauncherDialog::DrawNativeModsTab() {
  // Guardados: se buscan al abrir la pestana y tras cada accion (antes, en cada frame: el recorrido de
  // user_data costaba ~1 ms por frame y crece con cada copia de seguridad).
  if (!saves_loaded_) {
    saves_loaded_ = true;
    saves_.clear();
    for (const auto& p : dbz3::FindNativeSaveFiles()) saves_.push_back({p, dbz3::IsSupportedNativeSave(p)});
    save100_ok_ = dbz3::Save100Available();
    native_catalog_ = dbz3::NativeModCatalog();
  }
  const float avail_x = ImGui::GetContentRegionAvail().x;
  const float col_w = (avail_x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

  ImGui::BeginChild("##game_left", ImVec2(col_w, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  TestScroll();
  DrawPs2Box();   // lo primero: sin estas ISO, varios modos nuevos salen en gris
  ui::SectionTitle(i18n::T("Modos de juego nuevos", "New game modes"));
  ui::Hint(i18n::T("Vienen activados. Si uno no te interesa, quitalo: su entrada desaparece de los menus del juego.",
                   "They come enabled. If you don't want one, turn it off: its entry disappears from the game menus."));
  ImGui::Spacing();
  int tests = 0;
  for (const Feature& f : Features()) {
    if (f.test) {
      ++tests;
      continue;
    }
    FeatureRow(f);
  }

  ImGui::Spacing();
  ui::SectionTitle(i18n::T("Arranque y pruebas", "Startup and testing"));
  bool skip = rex::cvar::Query<bool>("dbz3_skip_launcher");
  if (ImGui::Checkbox(i18n::T("Arranque rapido: abrir directamente el juego", "Quick start: open the game directly"),
                      &skip)) {
    rex::cvar::SetFlagByName("dbz3_skip_launcher", skip ? "true" : "false");
    dbz3::settings::SaveUserSettings();
  }
  ui::Tip(i18n::T("Al abrir, el juego empieza sin pasar por esta pantalla. Para volver a verla, pulsa F4 durante "
                  "la partida y desmarca esta casilla.",
                  "On launch the game starts without this screen. To see it again, press F4 while playing and "
                  "untick this box."));
  bool fps = dbz3::settings::ShowFps();
  if (ImGui::Checkbox(i18n::T("Mostrar los FPS en partida", "Show FPS while playing"), &fps)) {
    dbz3::settings::SetShowFps(fps);
    dbz3::settings::SaveUserSettings();
  }
  ui::Tip(i18n::T("Un contador pequeno en una esquina (F3 lo muestra u oculta en partida).",
                  "A small counter in a corner (F3 toggles it while playing)."));
  if (tests) {
    for (const Feature& f : Features())
      if (f.test) FeatureRow(f);
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##game_right", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  ui::SectionTitle(i18n::T("Tu partida guardada", "Your save"));
  if (saves_.empty()) {
    ui::Hint(i18n::T("Aun no hay ninguna partida guardada. Juega y guarda una vez; despues vuelve aqui.",
                     "There is no save yet. Play and save once, then come back here."));
  } else {
    for (size_t i = 0; i < saves_.size(); ++i) {
      const auto& save = saves_[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kOk, "%s", ICON_OK);
      ImGui::SameLine();
      ImGui::TextUnformatted(rex::path_to_utf8(save.path.parent_path().filename()).c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rex::path_to_utf8(save.path).c_str());
      ImGui::SameLine();
      if (!save.supported) {
        ImGui::TextColored(kWarn, "%s", i18n::T("formato no reconocido", "unknown format"));
      } else if (ImGui::SmallButton(i18n::T("Crear copia de seguridad", "Create backup"))) {
        std::filesystem::path backup;
        std::string error;
        native_mods_status_ = dbz3::BackupNativeSave(save.path, backup, error)
                                  ? std::string(i18n::T("Copia creada: ", "Backup created: ")) + rex::path_to_utf8(backup)
                                  : std::string(i18n::T("No se pudo crear la copia: ", "Backup failed: ")) + error;
        saves_loaded_ = false;
      }
      ImGui::PopID();
    }
  }
  for (const auto& mod : native_catalog_) {
    if (mod.state != dbz3::NativeModState::kReady) continue;   // los "sin codigo" solo confundian
    ImGui::PushID(mod.id.c_str());
    ImGui::Spacing();
    {
      ui::FontScope b(ui::GetFonts().bold);
      ImGui::TextUnformatted(mod.name.c_str());
    }
    ui::Hint(mod.description.c_str());
    {
      const bool avail = mod.id != "save_100" || save100_ok_;
      ImGui::BeginDisabled(!avail);
      if (ImGui::Button(i18n::T("Aplicar (reemplaza tu partida)", "Apply (replaces your save)"))) {
        ImGui::OpenPopup("###confirm_save100");
      }
      ImGui::EndDisabled();
      if (!avail)   // issue #20: el boton en gris sin explicar por que
        ui::Hint(i18n::T("Falta la carpeta mods_nativos\\partida_100 junto a dbz3.exe: vuelve a instalar la version.",
                         "The mods_nativos\\partida_100 folder next to dbz3.exe is missing: reinstall the release."));
      if (ConfirmModal((std::string(i18n::T("Partida", "Save")) + "###confirm_save100").c_str(),
                       i18n::T("Tu partida se sustituira por esta. Antes se hace una copia de seguridad "
                               "(la recuperas con 'Restaurar mi partida'). Continuar?",
                               "Your save will be replaced with this one. A backup is made first "
                               "(get it back with 'Restore my save'). Continue?"),
                       i18n::T("Si, aplicar", "Yes, apply"))) {
        dbz3::ApplySave100(native_mods_status_);
        saves_loaded_ = false;
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
        saves_loaded_ = false;
      }
    }
    ImGui::PopID();
  }
  if (!native_mods_status_.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kOk, "%s", native_mods_status_.c_str());
    ImGui::PopTextWrapPos();
  }

  ImGui::Spacing();
  ui::SectionTitle(i18n::T("Aspecto de los personajes", "Character look"));
  // "Brillo HD": the HD remaster's rim light on the character models.
  {
    double rim = dbz3::settings::HdRimLight();
    bool rim_on = rim > 0.0;
    bool changed = false;
    {
      ui::FontScope b(ui::GetFonts().bold);
      changed = ImGui::Checkbox(i18n::T("Brillo HD", "HD shine"), &rim_on);
    }
    if (changed) rim = rim_on ? 1.0 : 0.0;
    if (rim_on) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(220.0f);
      double pct = rim * 100.0;
      if (SliderD("##hd_rim_light", &pct, 5.0, 100.0, "%.0f %%")) {
        rim = pct / 100.0;
        changed = true;
      }
    }
    ui::Hint(i18n::T(
        "El remaster HD anade un halo brillante en el borde de los modelos. Quitalo para un "
        "aspecto mas plano, como en PS2, o bajalo a tu gusto. Se aplica al momento.",
        "The HD remaster adds a bright halo on the edges of the models. Turn it off for a "
        "flatter, PS2-like look, or lower it to taste. Applies right away."));
    if (changed) {
      dbz3::settings::SetHdRimLight(rim);
      dbz3::settings::ApplyRuntimeSettingsToSdk(false);
      dbz3::settings::SaveUserSettings();
    }
  }
  ImGui::EndChild();
}

void LauncherDialog::StartIsoPrep(bool then_play) {
  if (iso_prep_) return;
  const bool voices = ModPipeline::HasCharacterSources() && ModPipeline::RosterToolAvailable();
  iso_prep_ = std::make_shared<IsoPrep>();
  iso_prep_->then_play = then_play;
  iso_prep_error_.clear();
  std::thread([p = iso_prep_, voices] {
    p->ok = dbz3::settings::PrepareIsoData(voices, &p->progress, &p->error);
    p->done = true;
  }).detach();
  if (then_play) launching_ = true;
}

// ISO + mods: estado de la copia de sus .afs y el boton para hacerla ya (JUGAR tambien la hace sola).
void LauncherDialog::DrawIsoPrepBox() {
  ImGui::PushTextWrapPos(0.0f);
  if (iso_prep_) {
    ImGui::TextColored(kInfo, "%s %s %d%%", ICON_DOWNLOAD,
                       i18n::T("Preparando la ISO para los mods...", "Preparing the ISO for mods..."),
                       iso_prep_->progress.load() / 10);
    ImGui::ProgressBar(float(iso_prep_->progress.load()) / 1000.0f, ImVec2(-1.0f, 6.0f), "");
  } else if (dbz3::settings::IsoDataCacheValid()) {
    ImGui::TextColored(kOk, "%s %s", ICON_OK, i18n::T(
        "Juegas desde la ISO y tus mods funcionan (sus datos ya estan preparados).",
        "You play from the ISO and your mods work (their data is ready)."));
  } else {
    ImGui::TextColored(kGold, "%s %s", ICON_WARN, i18n::T(
        "Juegas desde la ISO: para los mods y los extras se copia una vez parte de sus datos (~0,5 GB; con "
        "personajes nuevos, ~2 GB). JUGAR lo hace solo la primera vez, o pulsa el boton.",
        "You play from the ISO: for mods and extras part of its data is copied once (~0.5 GB; with new "
        "characters, ~2 GB). PLAY does it by itself the first time, or press the button."));
    if (ImGui::Button((std::string(ICON_DOWNLOAD " ") +
                       i18n::T("Preparar la ISO para mods", "Prepare the ISO for mods")).c_str())) {
      StartIsoPrep(false);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Copia los archivos del juego que cambian los mods a user_data/dbz3/iso_datos. La ISO no se toca.",
          "Copies the game files that mods change to user_data/dbz3/iso_datos. The ISO is not touched."));
    }
  }
  if (!iso_prep_error_.empty()) {
    ImGui::TextColored(kGold, "%s %s%s", ICON_WARN, i18n::T("No se pudo: ", "It failed: "), iso_prep_error_.c_str());
  }
  ImGui::PopTextWrapPos();
}

void LauncherDialog::DrawNovasModsTab() {
  ImGui::BeginChild("##novas_mods", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  StartNovaCatalog();
  ui::SectionTitle("Nova's Mods");
  ImGui::TextWrapped("%s", i18n::T(
      "Mods del proyecto listos para jugar: un clic los descarga e instala, y desde aqui se actualizan. "
      "Despues se activan y desactivan en la pestana Mods como cualquier otro.",
      "Project mods, ready to play: one click downloads and installs them, and they update from here. "
      "Then you turn them on and off in the Mods tab like any other mod."));
  ImGui::Spacing();
  const std::string status = NovaStatus();
  if (status != nova_last_status_) {   // instalado: la pestana Mods vuelve a leer la carpeta
    nova_last_status_ = status;
    mods_loaded_ = false; broken_mods_ = -1;
  }
  // El resultado arriba, a la vista (antes salia al final de la lista, debajo de todos los mods) y en
  // amarillo si ha fallado.
  if (!status.empty()) {
    const bool ok = status.find(i18n::T(": instalado. Se aplica al pulsar JUGAR.",
                                        ": installed. It applies when you press PLAY.")) != std::string::npos;
    auto ends = [&](const char* t) {
      const size_t n = std::strlen(t);
      return status.size() >= n && status.compare(status.size() - n, n, t) == 0;
    };
    const bool working = !ok && (ends("...") || ends("%") || ends(" MB"));   // descargando / instalando
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ok ? kOk : working ? kInfo : kGold, "%s %s", ok ? ICON_OK : working ? ICON_DOWNLOAD : ICON_WARN,
                       status.c_str());
    ImGui::PopTextWrapPos();
    if (ok) {
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Ver en Mods", "See in Mods"))) request_tab_ = 1;
    }
  }
  if (ModPipeline::RosterToolAvailable()) DrawPackIW();   // necesita el Kit (importar.py)
  switch (GetNovaState()) {
    case NovaState::kNoKey:
      ImGui::TextColored(kDragonOrangeDim, "%s", i18n::T("Llega en la proxima actualizacion del launcher.",
                                                         "Coming in the next launcher update."));
      break;
    case NovaState::kLoading:
      ImGui::TextDisabled("%s", LoadingDots(i18n::T("Cargando la lista...", "Loading the list...")).c_str());
      break;
    case NovaState::kFailed:
      ImGui::TextColored(kDragonOrangeDim, "%s", i18n::T("No se pudo cargar la lista. Comprueba tu conexion.",
                                                         "The list could not be loaded. Check your connection."));
      ImGui::SameLine();
      if (ImGui::Button(i18n::T("Reintentar", "Retry"))) RefreshNovaCatalog();
      if (const std::string why = dbz3::launcher::LastNetworkError(); !why.empty()) {
        ImGui::TextDisabled(i18n::T("Detalle tecnico: %s", "Technical detail: %s"), why.c_str());
      }
      break;
    case NovaState::kReady: {
      // filtro por tipo (botones) y buscador en la misma fila
      static const char* const kTipo[4] = {"", "personaje", "campana", "texturas"};
      const char* const kChip[4] = {i18n::T("Todos", "All"), i18n::T("Personajes", "Characters"),
                                    i18n::T("Historias", "Stories"), i18n::T("Texturas", "Textures")};
      for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        const bool on = novas_tipo_ == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
        if (ImGui::Button(kChip[i])) novas_tipo_ = i;
        if (on) ImGui::PopStyleColor();
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##novas_filter", i18n::T("Buscar por nombre o autor...", "Search by name or author..."),
                               novas_search_, sizeof(novas_search_));
      ImGui::Spacing();
      int shown = 0;
      for (const NovaMod& m : NovaCatalog()) {
        if (novas_tipo_ > 0 && m.tipo != kTipo[novas_tipo_]) continue;
        if (!IContains(m.name + " " + m.id + " " + m.tipo + " " + m.description + " " + m.autor, novas_search_)) continue;
        ++shown;
        ImGui::PushID(m.id.c_str());
        const char* kind = m.tipo == "personaje" ? i18n::T("Personaje", "Character")
                           : m.tipo == "campana" ? i18n::T("Historia", "Story")
                           : m.tipo == "texturas" ? i18n::T("Pack de texturas", "Texture pack") : "Mod";
        const ImVec4 kind_col = m.tipo == "personaje" ? ui::kBlue : m.tipo == "campana" ? kGold
                                : m.tipo == "texturas" ? ImVec4(0.70f, 0.45f, 0.95f, 1.0f) : kTextDim;
        // tarjeta: a la izquierda el mod; a la derecha su estado y el boton
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kCard);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::BeginChild("##card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        const float bw = 200.0f, x_btn = ImGui::GetWindowContentRegionMax().x - bw;
        ImGui::BeginGroup();
        {
          ui::FontScope f(ui::GetFonts().bold);
          ImGui::TextColored(kGold, "%s", m.name.c_str());
        }
        ImGui::SameLine();
        ui::Pill(nullptr, kind, kind_col);
        ImGui::SameLine();
        if (m.size >= 1000000000LL) {   // packs HD: "5,96 GB" (lo que pesa la descarga)
          std::string gb = std::to_string(double(m.size) / 1e9).substr(0, 4);
          if (std::string(i18n::T(",", ".")) == ",") std::replace(gb.begin(), gb.end(), '.', ',');
          ImGui::TextDisabled("v%s  ·  %s GB", m.version.c_str(), gb.c_str());
        } else {
          ImGui::TextDisabled("v%s  ·  %.1f MB", m.version.c_str(), double(m.size) / (1024.0 * 1024.0));
        }
        ImGui::PushTextWrapPos(x_btn - 16.0f);
        ImGui::TextWrapped("%s", m.description.c_str());
        ImGui::PopTextWrapPos();
        if (!m.autor.empty()) {
          // credito clicable: cada enlace del catalogo abre las redes del autor en el navegador
          ImGui::TextDisabled("%s %s", i18n::T("Autor:", "Author:"), m.autor.c_str());
          for (size_t li = 0; li < m.enlaces.size(); ++li) {
            const std::string& url = m.enlaces[li];
            const bool x = url.find("://x.com/") != std::string::npos || url.find("twitter.com/") != std::string::npos;
            const std::string label = x ? "X / Twitter"
                                        : url.find("linktr.ee/") != std::string::npos ? std::string("Linktree")
                                                                                      : i18n::T("Web", "Website");
            ImGui::SameLine();
            ImGui::PushID(int(li));
            if (ImGui::SmallButton(label.c_str())) OpenUrl(url);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", url.c_str());
            ImGui::PopID();
          }
        }
        ImGui::EndGroup();
        const ImVec2 card_top = ImGui::GetItemRectMin();
        const std::string have = NovaInstalledVersion(m.id);
        const bool update = !have.empty() && (have == "?" || IsVersionNewer(m.version, have));   // nunca bajar de version
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + x_btn, card_top.y));
        ImGui::BeginGroup();
        if (have.empty()) ui::Pill(nullptr, i18n::T("No instalado", "Not installed"), kTextDim);
        else if (update) ui::Pill(ICON_UPDATE, i18n::T("Actualización", "Update available"), kGold);
        else ui::Pill(ICON_OK, i18n::T("Instalado", "Installed"), kOk);
        if (NovaBusy(m.id)) {
          ImGui::BeginDisabled();
          ImGui::Button(i18n::T("Descargando...", "Downloading..."), ImVec2(bw, 40));
          ImGui::EndDisabled();
          ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bw);
          ImGui::TextColored(kInfo, "%s", status.c_str());   // "Descargando X... 42%"
          ImGui::PopTextWrapPos();
        } else if (have.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
          if (ImGui::Button((std::string(ICON_DOWNLOAD " ") + i18n::T("Instalar", "Install")).c_str(), ImVec2(bw, 40)))
            InstallNovaMod(m);
          ImGui::PopStyleColor();
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", i18n::T("Lo descarga y lo deja activado. Se usa al pulsar JUGAR.",
                                            "Downloads it and turns it on. It is used when you press PLAY."));
        } else if (update) {
          if (ImGui::Button((std::string(ICON_UPDATE " ") + i18n::T("Actualizar", "Update")).c_str(), ImVec2(bw, 40)))
            InstallNovaMod(m);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", i18n::T("Baja la versión nueva. La anterior se guarda en mods_respaldo.",
                                            "Downloads the new version. The old one is kept in mods_respaldo."));
        } else if (ImGui::Button(i18n::T("Ver en Mods", "See in Mods"), ImVec2(bw, 40))) {
          request_tab_ = 1;
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PopID();
      }
      if (!shown) ImGui::TextDisabled("%s", i18n::T("Sin resultados.", "No results."));
      break;
    }
  }
  ImGui::EndChild();
}

void LauncherDialog::DrawModsTab() {
  ImGui::BeginChild("##mods_settings", ImVec2(0, -kFooterHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
  TestScroll();

  ui::SectionTitle(i18n::T("Centro de mods", "Mod center"));
  ui::Hint(i18n::T(
      "Tus mods: marca la casilla para activarlo o quítala para desactivarlo (se aplica al pulsar JUGAR). "
      "Para instalar uno, arrastra su .zip a esta ventana, pulsa «Instalar mod (.zip)...» o descárgalo en "
      "Nova's Mods. Pulsa el nombre de un mod para ver su ficha.",
      "Your mods: tick the box to turn one on or untick it to turn it off (it applies when you press PLAY). "
      "To install one, drag its .zip onto this window, press 'Install mod (.zip)...' or download it from "
      "Nova's Mods. Click a mod's name to see its card."));

  // ISO: los mods necesitan la copia de sus .afs (DrawIsoPrepBox; JUGAR tambien la hace sola la primera vez)
  if (dbz3::settings::IsIsoMode()) {
    ImGui::Spacing();
    DrawIsoPrepBox();
  }

  // Mods opcionales (encargo 13): los datos de B2/IW (musica, videos, sonidos) que el juego lee de su opcional.toml.
  // Vienen desactivados; la casilla los activa como a cualquier mod (mods.cpp) y cada uno avisa de la ISO que
  // necesita (pasarela::IsoPs2; se elige en la pestana Juego, "Contenido de tus juegos de PS2").
  {
    const auto opc = dbz3::opcional::Listar();
    if (opc && !opc->empty()) {
      ui::SectionTitle(i18n::T("Mods opcionales (Budokai 2 / Infinite World)",
                               "Optional mods (Budokai 2 / Infinite World)"));
      ui::Hint(i18n::T(
          "Añaden música, vídeos y sonidos originales de tus juegos de PS2, apagados hasta que los marques. "
          "Cada uno necesita la ISO del juego que dice (pestaña Juego).",
          "They add original music, videos and sounds from your PS2 games, off until you tick them. "
          "Each one needs the ISO of the game it says (Game tab)."));
      static double iso_leido = -10;
      static bool iso_lista[3] = {};
      if (ImGui::GetTime() - iso_leido > 2.0) {
        iso_leido = ImGui::GetTime();
        for (int i = 0; i < 3; ++i) iso_lista[i] = !dbz3::pasarela::IsoPs2(i).empty();
      }
      const char* const kIdiomaOpc[7] = {"en", "ja", "de", "fr", "es", "it", "pt"};   // el orden de kLanguageIds
      std::string idioma = "en";
      for (int i = 0; i < 7; ++i)
        if (dbz3::settings::UiLanguage() == kLanguageIds[i]) idioma = kIdiomaOpc[i];
      for (const dbz3::opcional::Mod& m : *opc) {
        const bool es_iw = m.id.rfind("iw", 0) == 0;   // "iw.*" necesita Infinite World; el resto, Budokai 2
        const int juego = es_iw ? 2 : 1;
        const std::string titulo = dbz3::opcional::Nombre(m, idioma);
        ImGui::PushID(m.carpeta.c_str());
        bool on = m.activo;
        if (ImGui::Checkbox("##opc_on", &on)) {
          dbz3::SetModEnabled(m.carpeta, on);
          dbz3::opcional::Refrescar();
          mods_loaded_ = false; broken_mods_ = -1;
          mods_status_ = std::string(on ? i18n::T("Mod opcional activado: ", "Optional mod enabled: ")
                                        : i18n::T("Mod opcional desactivado: ", "Optional mod disabled: ")) + titulo;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", m.activo ? i18n::T("Activado: se usa al pulsar JUGAR. Quita la marca para apagarlo.",
                                                     "On: used when you press PLAY. Untick to turn it off.")
                                           : i18n::T("Apagado: no se usa. Marca la casilla para activarlo.",
                                                     "Off: not used. Tick the box to turn it on."));
        ImGui::SameLine();
        ImGui::TextUnformatted(titulo.c_str());
        ImGui::SameLine();
        if (iso_lista[juego]) {
          ImGui::TextColored(kOk, "%s %s", ICON_OK, i18n::T("Lista", "Ready"));
        } else {
          ImGui::TextColored(kGold, "%s %s", ICON_WARN, i18n::T("Falta", "Missing"));
          ImGui::SameLine();
          ImGui::TextDisabled("%s", es_iw ? i18n::T("Necesita tu ISO de Infinite World.",
                                                     "Needs your Infinite World ISO.")
                                           : i18n::T("Necesita tu ISO de Budokai 2.", "Needs your Budokai 2 ISO."));
          ImGui::SameLine();
          if (ImGui::SmallButton(i18n::T("Elegir ISO", "Pick ISO"))) {
            dbz3::pasarela::ElegirIsoPs2(juego);
            iso_leido = -10;   // se vuelve a mirar: puede haberla elegido
          }
        }
        const std::string desc = dbz3::opcional::Descripcion(m, idioma);
        if (!desc.empty()) ImGui::TextDisabled("%s", desc.c_str());
        ImGui::PopID();
      }
      ImGui::Separator();
    }
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
    // el resultado se recoge aunque la seccion este plegada
    if (tex_easy_job_ && !mod_pipeline_.IsRunning()) {
      tex_easy_job_ = false;
      const std::string out = mod_pipeline_.Output();
      const bool ok = out.find("Listo:") != std::string::npos;
      tex_easy_status_ = ok ? std::string(i18n::T("PNG preparados en 'texturas/para_editar'.",
                                                  "PNGs ready in 'texturas/para_editar'."))
                            : out.substr(out.size() > 600 ? out.size() - 600 : 0);
      if (ok) open_dir(edit_dir);
    }
    ImGui::Spacing();
    // Herramienta de creadores: plegada (antes ocupaba media pestana y la lista de mods quedaba abajo).
    const bool tex_open = ImGui::CollapsingHeader(
        (std::string(i18n::T("Crear tu propio pack de texturas (PNG)", "Make your own texture pack (PNG)")) +
         "###tex_easy").c_str(),
        dbz3::settings::TextureDumpEnabled() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (tex_open) {
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
    if (ImGui::Button(i18n::T("Refrescar capturas", "Refresh captures"))) tex_easy_captures_ = -1;
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
      mods_loaded_ = false; broken_mods_ = -1;
    }
    if (!tex_easy_status_.empty()) ImGui::TextDisabled("%s", tex_easy_status_.c_str());
    const std::string packs = dbz3::settings::TexturePacksList();
    if (!packs.empty()) {
      ImGui::Text("%s", i18n::T("Packs de texturas activos:", "Active texture packs:"));
      ImGui::SameLine();
      ImGui::TextDisabled("%s", packs.c_str());
    }
    }   // tex_open
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
      for (dbz3::ModInfo& m : mods_cache_) {
        if (!m.enabled) dbz3::SetModEnabled(m.name, true);
        m.enabled = true;
      }
      mods_loaded_ = false; broken_mods_ = -1;
      mods_status_ =
          std::string(i18n::T("Todos los mods activados.", "All mods enabled."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Desactivar todos", "Disable all"), ImVec2(0, 0))) {
      for (dbz3::ModInfo& m : mods_cache_) {
        if (m.enabled) dbz3::SetModEnabled(m.name, false);
        m.enabled = false;
      }
      mods_loaded_ = false; broken_mods_ = -1;
      mods_status_ = std::string(i18n::T("Todos los mods desactivados (juego original).",
                                         "All mods disabled (original game)."));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::T("Refrescar", "Refresh"), ImVec2(0, 0))) {
      mods_loaded_ = false; broken_mods_ = -1;
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
    ImGui::SetNextItemWidth(200);
    const char* vanilla_label = i18n::T("Original (sin mods)", "Original (no mods)");
    // "vanilla" con mods activados a mano no es "sin mods": se dice que no hay perfil elegido
    const char* profile_preview = cur != "vanilla" ? cur.c_str()
                                  : active_mods_ > 0 ? i18n::T("Ninguno (a mano)", "None (manual)")
                                                     : vanilla_label;
    if (ImGui::BeginCombo("##mod_profile", profile_preview)) {
      ImGui::InputTextWithHint("##profiles_filter", i18n::T("Filtrar perfiles...", "Filter profiles..."),
                               profiles_search_, sizeof(profiles_search_));
      if (ImGui::Selectable(vanilla_label, cur == "vanilla")) {
        if (cur != "vanilla") {
          dbz3::ApplyProfile("vanilla");
          dbz3::settings::SetModProfile("vanilla");
          mods_loaded_ = false; broken_mods_ = -1;
          mods_status_ = std::string(i18n::T(
              "Juego original: todos los mods desactivados (no se borra ninguno).",
              "Original game: all mods turned off (none is deleted)."));
        }
      }
      int profiles_shown = 0;
      for (const auto& p : profiles) {
        if (!IContains(p, profiles_search_)) continue;
        ++profiles_shown;
        if (ImGui::Selectable(p.c_str(), cur == p)) {
          dbz3::ApplyProfile(p);
          dbz3::settings::SetModProfile(p);
          mods_loaded_ = false; broken_mods_ = -1;
          mods_status_ =
              std::string(i18n::T("Perfil aplicado: ", "Profile applied: ")) + p;
        }
      }
      if (!profiles_shown && profiles_search_[0]) ImGui::TextDisabled("%s", i18n::T("Sin resultados.", "No results."));
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Un perfil guarda qué mods tienes activos para cambiar de golpe (por ejemplo, uno para jugar online). "
          "«Original (sin mods)» los desactiva todos.",
          "A profile remembers which mods are on, to switch all at once (for example, one for online). "
          "'Original (no mods)' turns them all off."));
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
        mods_loaded_ = false; broken_mods_ = -1;
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
          mods_loaded_ = false; broken_mods_ = -1;
        } else {
          mods_status_ =
              std::string(i18n::T("Error al instalar el mod: ",
                                  "Error installing mod: ")) + err;
        }
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", i18n::T(
          "Elige el .zip del mod que has descargado: se instala y se activa solo. También puedes "
          "arrastrar el .zip a esta ventana.",
          "Pick the .zip of the mod you downloaded: it is installed and turned on by itself. You can also "
          "drag the .zip onto this window."));
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
          for (const dbz3::ModInfo& m : dbz3::ListMods(false)) {
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
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(IsBadStatus(mods_status_) ? kGold : kOk, "%s", mods_status_.c_str());
      ImGui::PopTextWrapPos();
      if (ImGui::IsItemClicked()) {
        mods_status_.clear();
      }
    }

    // G11: estado del paso de escenarios de PS2, visible sin abrir el registro. La linea se guarda en
    // mods/_escenarios_ps2.txt y sobrevive al reinicio del launcher.
    const ModPipeline::StageReport stages = mod_pipeline_.StagesReport();
    if (stages.ran) {
      char line[512];
      if (stages.failed > 0) {
        std::snprintf(line, sizeof(line),
                      i18n::T("Escenarios de PS2: %d creados / %d fallaron: %s",
                              "PS2 stages: %d created / %d failed: %s"),
                      stages.created, stages.failed, stages.reason.c_str());
      } else {
        std::snprintf(line, sizeof(line),
                      i18n::T("Escenarios de PS2: %d creados / 0 fallaron",
                              "PS2 stages: %d created / 0 failed"),
                      stages.created);
      }
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(stages.failed > 0 ? kGold : kOk, "%s", line);
      ImGui::PopTextWrapPos();
      if (ImGui::IsItemHovered()) {
        const std::string tip = std::string(i18n::T("Registro: ", "Log: ")) +
                                rex::path_to_utf8(ModPipeline::StagesRegistryPath());
        ImGui::SetTooltip("%s", tip.c_str());
      }
    } else {
      ImGui::TextDisabled("%s", i18n::T(
          "Escenarios de PS2: sin datos todavia (se preparan al pulsar JUGAR).",
          "PS2 stages: no data yet (they are prepared when you press PLAY)."));
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
  if (!mods_ready_) {
    ImGui::TextDisabled("%s", i18n::T("Leyendo tus mods...", "Reading your mods..."));
    ImGui::EndChild();
    return;
  }
  if (mods_cache_.empty()) {
    ImGui::TextWrapped("%s", i18n::T(
        "Aún no tienes mods. Lo más fácil: descarga uno en Nova's Mods (un clic). También puedes arrastrar "
        "el .zip de un mod a esta ventana.",
        "No mods yet. The easiest way: download one from Nova's Mods (one click). You can also drag a "
        "mod's .zip onto this window."));
    if (ui::IconButton(ICON_DOWNLOAD, i18n::T("Ir a Nova's Mods", "Go to Nova's Mods"), ImVec2(220, 0)))
      request_tab_ = 3;
    ImGui::SameLine();
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
          mods_loaded_ = false; broken_mods_ = -1;
        } else {
          mods_status_ =
              std::string(i18n::T("Error al instalar el mod: ",
                                  "Error installing mod: ")) + err;
        }
      }
    }
    ui::Hint(i18n::T(
        "«Abrir carpeta de mods» abre la carpeta donde viven los mods: lo que copies ahí aparece en la lista.",
        "'Open mods folder' opens the folder where mods live: whatever you copy there shows up in this list."));
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
                     i18n::T("Mods: %d  ·  activados: %d", "Mods: %d  ·  enabled: %d"),
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
  if (mods_scan_) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", i18n::T("  (actualizando la lista...)", "  (updating the list...)"));
  }
  // A9: dos mods que cambian lo mismo -> se dice cual se ve (el mismo orden que usa el juego)
  for (const auto& c : mods_conflicts_) {
    ImGui::TextColored(kGold, i18n::T("Se pisan: se ve '%s' en lugar de '%s' (%d cosas). Desactiva uno si quieres el otro.",
                                      "Overlap: '%s' is shown instead of '%s' (%d items). Disable one to get the other."),
                       c.winner.c_str(), c.loser.c_str(), c.resources);
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
        if (current) dbz3::StampModVersion(mod.name, dbz3::launcher::CurrentVersion());
        for (dbz3::ModInfo& c : mods_cache_)
          if (c.name == mod.name) c.enabled = current;   // se ve ya; la lista nueva llega en segundo plano
        mods_loaded_ = false; broken_mods_ = -1;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", mod.enabled ? i18n::T("Activado: se usa al pulsar JUGAR. Quita la marca para apagarlo.",
                                                      "On: used when you press PLAY. Untick to turn it off.")
                                            : i18n::T("Apagado: no se usa. Marca la casilla para activarlo.",
                                                      "Off: not used. Tick the box to turn it on."));

      ImGui::TableSetColumnIndex(1);
      const std::string& title =
          mod.display_name.empty() ? mod.name : mod.display_name;
      ImVec4 title_col = mod.enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                     : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, title_col);
      if (ImGui::Selectable((title + "##card_" + mod.name).c_str(), false, 0,
                            ImVec2(ImGui::CalcTextSize(title.c_str()).x, 0.0f)))
        mod_card_ = mod.name;
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", i18n::T("Ver la ficha del mod", "See the mod's card"));
      if (mod.type == "generado") {
        ImGui::TextDisabled("%s", i18n::T(
            "Se crea solo con los personajes nuevos activos (pestana Personajes). "
            "No hace falta tocarlo.",
            "Built automatically from the enabled new characters (New characters tab). "
            "No need to touch it."));
      } else if (mod.type == "personaje") {
        ImGui::TextDisabled("%s", i18n::T("Personaje nuevo: se edita en la pestana Personajes "
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
          mods_loaded_ = false; broken_mods_ = -1;
        }
      }

      ImGui::TableSetColumnIndex(2);
      DrawBadge(ModTypeLabelText(mod.type), ModTypeColorVec(mod.type));
      ImGui::SameLine();
      {   // D4: el mod se activo con otra version del juego
        const std::string ahora = dbz3::launcher::CurrentVersion();
        if (!mod.made_for.empty() && !ahora.empty() && mod.made_for != ahora) {
          DrawBadge(i18n::T("OTRA VERSION", "OTHER VERSION"), ImVec4(0.55f, 0.38f, 0.10f, 1.0f));
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(i18n::T("Este mod se activo con la version %s y ahora corre la %s: si algo falla, desactivalo.",
                                      "This mod was enabled on version %s and now version %s is running: disable it if something fails."),
                              mod.made_for.c_str(), ahora.c_str());
          }
          ImGui::SameLine();
        }
      }
      if (mod.enabled) ImGui::TextColored(kOk, "%s", i18n::T("ACTIVO", "ON"));
      else ImGui::TextDisabled("%s", i18n::T("APAGADO", "OFF"));
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
        ImGui::SetTooltip("%s", i18n::T("Cambiar el nombre, la descripción, el autor o la versión del mod",
                                        "Change the mod's name, description, author or version"));
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

  if (static const char* test_card = std::getenv("DBZ3_TEST_CARD"); test_card && *test_card) {
    mod_card_ = test_card;   // pruebas sin raton: abre la ficha de ese mod una vez
    test_card = nullptr;
  }
  DrawModCard();

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
      mods_loaded_ = false; broken_mods_ = -1;
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

  ui::SectionTitle(i18n::T("Cambiar el modelo de un personaje", "Change a character's model"));
  ui::Hint(i18n::T(
      "Elige de quién es el modelo y a qué personaje se lo pones: al jugar con el segundo verás el modelo del "
      "primero. Se crea un mod que se activa solo y que puedes quitar en la pestaña Mods.",
      "Pick whose model to use and which character gets it: playing as the second one shows the first one's "
      "model. A mod is created and turned on by itself; you can remove it in the Mods tab."));

  const bool iso = dbz3::settings::IsIsoMode();
  if (iso) DrawIsoPrepBox();   // desde la ISO, el cambio de modelo lee la copia de sus .afs (UsDir)

  // Lazy-load the B3 catalog once (first draw).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped("%s", i18n::T(
        "Esta herramienta viene en el Kit de modding (DBZ3HD-Kit-Modding.zip): descomprímelo junto a "
        "dbz3.exe y vuelve a abrir esta pestaña. Para jugar no hace falta.",
        "This tool comes with the modding kit (DBZ3HD-Kit-Modding.zip): unzip it next to dbz3.exe and "
        "open this tab again. It is not needed to play."));
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
  // Casi nadie lo necesita (la ruta automatica funciona): plegado, salvo si ya se eligio otra ruta.
  if (ImGui::CollapsingHeader((std::string(i18n::T("Opciones avanzadas: archivo de modelos (data_cmn.afs)",
                                                   "Advanced: model file (data_cmn.afs)")) + "###swap_adv").c_str(),
                              afs_path_auto_ ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
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
    std::string picked;
    if (PickFile(picked, "data_cmn.afs", "*.afs", std::string(afs_path_buf_)) &&
        !picked.empty() && picked.size() < sizeof(afs_path_buf_)) {
      std::snprintf(afs_path_buf_, sizeof(afs_path_buf_), "%s", picked.c_str());
      afs_path_auto_ = false;
      mod_pipeline_.SetAfsPath(afs_path_buf_);
    }
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
  }   // opciones avanzadas
  ImGui::Spacing();

  if (pipeline_src_idx_ >= (int)chars.size()) pipeline_src_idx_ = -1;
  if (pipeline_dst_idx_ >= (int)chars.size()) pipeline_dst_idx_ = -1;

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", i18n::T("Modelo de", "Model of"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  CharacterCombo("##swap_src", "##swap_src_filter", chars, pipeline_src_idx_,
                 swap_src_search_buf_, sizeof(swap_src_search_buf_));
  ImGui::SameLine();
  ImGui::TextDisabled("(%d %s)", static_cast<int>(chars.size()),
                      i18n::T("personajes", "characters"));

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", i18n::T("Se lo pones a", "Give it to"));
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
  if (ImGui::Button(i18n::T("Crear el cambio", "Create the swap"), ImVec2(220, 0))) {
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
bool PickFileDialog(std::string& out, const char* filter_desc, const char* filter_ext) {
#if REX_PLATFORM_WIN32
  return PickFile(out, filter_desc, filter_ext, std::string(), GetForegroundWindow());   // encima del juego
#else
  return PickFile(out, filter_desc, filter_ext, std::string());
#endif
}
std::string PathUtf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }
rex::ui::ImmediateDrawer* PreviewDrawer() { return g_preview_drawer; }


namespace {
std::function<void(std::function<void()>)> g_ui_defer;

rex::input::InputSystem* LauncherInput() {
  auto* runtime = rex::Runtime::instance();
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}
}  // namespace

void SetUiDefer(std::function<void(std::function<void()>)> defer) { g_ui_defer = std::move(defer); }
void DeferUi(std::function<void()> fn) {
  if (g_ui_defer) g_ui_defer(std::move(fn));
}

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
  if (static const bool test_pad = std::getenv("DBZ3_TEST_PAD") != nullptr; test_pad)
    pad_mode_ = true;   // pruebas: capturas con las ayudas del mando sin mando conectado
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
  constexpr int kTabCount = kLauncherTabCount;
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
  // A la derecha de la fila del resumen, pegadas a JUGAR (que ocupa las dos filas del pie).
  ImGui::SameLine(0.0f, 16.0f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 20.0f - 320.0f - 18.0f;
  if (right - width > p.x) p.x = right - width;
  const float row_h = ImGui::GetFrameHeight();
  ImGui::SetCursorScreenPos(p);
  p.y += (row_h - h) * 0.5f;
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
  ImGui::Dummy(ImVec2(width - 18.0f, row_h));
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
  // Igual que roster_build.plan_ids (resumido): el ID guardado en mods/roster_ids.toml se conserva y
  // los de mods quitados o desactivados quedan reservados; luego las plazas pedidas (en orden de
  // carpeta; si dos piden la misma gana la primera) y la primera libre.
  std::map<std::string, int> out;
  std::vector<int> used;
  auto taken = [&](int id) { return std::find(used.begin(), used.end(), id) != used.end(); };
  {
    std::istringstream in(ReadSmallFile(ModPipeline::ModsDir() / "roster_ids.toml"));
    bool ids = false;
    for (std::string ln; std::getline(in, ln);) {
      if (!ln.empty() && ln[0] == '[') ids = ln.rfind("[ids]", 0) == 0;
      const size_t q = ln.find("\" = ");
      if (!ids || ln.empty() || ln[0] != '"' || q == std::string::npos) continue;
      const std::string folder = ln.substr(1, q - 1);
      const int id = std::atoi(ln.c_str() + q + 4);
      const auto cs = std::find_if(char_sources_.begin(), char_sources_.end(),
                                   [&](const CharacterSource& c) { return c.folder == folder; });
      if (cs != char_sources_.end() && cs->enabled && !cs->costume &&
          (cs->req_id < 0 || cs->req_id == id) && !taken(id))
        out[folder] = id;
      used.push_back(id);   // activo con su ID, o reservado para cuando vuelva
    }
  }
  for (const auto& cs : char_sources_) {
    if (out.count(cs.folder)) continue;
    if (!cs.enabled || cs.costume) continue;   // los trajes van sobre su personaje, sin plaza
    for (const auto& s : kFreeSlots) {
      if (s.id == cs.req_id && !taken(s.id)) {
        out[cs.folder] = s.id;
        used.push_back(s.id);
      }
    }
  }
  for (const auto& cs : char_sources_) {
    if (!cs.enabled || cs.costume || out.count(cs.folder)) continue;
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
    if (!cs.enabled || cs.costume) continue;
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
      if (got < 0) std::snprintf(cur, sizeof(cur), "%s", i18n::T("Automatica (primera libre)", "Automatic (first free)"));
      else std::snprintf(cur, sizeof(cur), i18n::T("Automatica (%d)", "Automatic (%d)"), got);
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
        "primera transformacion). Los cambios se aplican al pulsar JUGAR o \"Reconstruir ahora\".",
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

std::vector<std::string> SplitTabs(const std::string& line);

}  // namespace

// Pack de Infinite World: una casilla por personaje de IW con receta oficial (ports_oficiales.toml, fuente iw). Marcar =
// crearlo con la copia de IW del jugador (importar.py oficial); desmarcar = apartarlo a mods_respaldo/. Sin catalogo:
// el pack no trae nada del juego, solo las recetas del Kit. Hace falta para cubrir las misiones de IW con personajes
// que B3 no tiene (Goku GT, Baby, Super 17, Janemba, Pikkon, Vegeta GT, Gran Saiyaman 2, Pan).
void LauncherDialog::DrawPackIW() {
  const bool trabajo = mod_pipeline_.IsRunning();
  if (pack_consulta_ && !trabajo) {   // la consulta acabo: leer sus filas
    pack_consulta_ = false;
    pack_gen_ = mod_pipeline_.Generation();
    pack_filas_.clear();
    std::istringstream in(mod_pipeline_.Output());
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      const auto c = SplitTabs(l);   // oficial, id, nombre, fuente, listo|falta, 1|0, preset
      if (c.size() >= 6 && c[0] == "oficial" && c[3] == "iw") pack_filas_.push_back({c[1], c[2], c[4] == "listo", c[5] == "1"});
    }
    pack_quiero_.clear();
    for (const PackFila& f : pack_filas_) pack_quiero_[f.id] = f.instalado;
    mods_loaded_ = false;
    broken_mods_ = -1;
  }
  if (!pack_consulta_ && !trabajo && pack_gen_ != mod_pipeline_.Generation()) {   // al entrar y tras cada trabajo
    pack_consulta_ = true;
    mod_pipeline_.ImporterQuery({"oficiales", "--mods", ModPipeline::ModsDir().string()});
  }
  const bool ocupado = trabajo || pack_consulta_;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::BeginChild("##pack_iw", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  {
    ui::FontScope f(ui::GetFonts().bold);
    ImGui::TextColored(kGold, "%s", i18n::T("Pack Infinite World", "Infinite World pack"));
  }
  ImGui::SameLine();
  ui::Pill(nullptr, "BETA", ui::kBlue);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", i18n::T(
      "Los personajes de Infinite World que piden sus misiones. Se crean aquí con TU copia de Infinite World (Juego > "
      "Elegir ISO): el pack no trae nada del juego. Marca los que quieras y pulsa Aplicar.",
      "The Infinite World characters its missions need. They are built here from YOUR Infinite World copy (Game > Pick "
      "ISO): the pack ships nothing from the game. Tick the ones you want and press Apply."));
  ImGui::PopTextWrapPos();
  if (pack_filas_.empty() && !ocupado)
    ImGui::TextDisabled("%s", i18n::T("No hay recetas de Infinite World en este Kit.", "No Infinite World recipes in this Kit."));
  if (ocupado) ImGui::BeginDisabled();
  for (const PackFila& f : pack_filas_) {
    ImGui::PushID(f.id.c_str());
    bool quiero = pack_quiero_[f.id];
    if (ImGui::Checkbox(f.nombre.c_str(), &quiero)) pack_quiero_[f.id] = quiero;
    ImGui::SameLine(260.0f);
    if (f.instalado) ui::Pill(ICON_OK, i18n::T("Instalado", "Installed"), kOk);
    else ui::Pill(nullptr, i18n::T("No instalado", "Not installed"), kTextDim);
    if (!f.listo) {
      ImGui::SameLine();
      ImGui::TextColored(kGold, "%s", i18n::T("Falta tu copia de Infinite World", "Your Infinite World copy is missing"));
    }
    ImGui::PopID();
  }
  auto aplicar = [this](int todos) {   // -1 lo marcado, 1 todos, 0 ninguno
    std::vector<std::string> instalar, quitar;
    for (const PackFila& f : pack_filas_) {
      const bool q = todos < 0 ? pack_quiero_[f.id] : todos == 1;
      if (q && !f.instalado && f.listo) instalar.push_back(f.id);
      if (!q && f.instalado) quitar.push_back(f.id);
    }
    if (instalar.empty() && quitar.empty()) {
      pack_msg_ = i18n::T("Nada que cambiar.", "Nothing to change.");
      return;
    }
    pack_msg_ = i18n::T("Aplicando los cambios... (se montan al pulsar JUGAR)", "Applying changes... (built when you press PLAY)");
    mod_pipeline_.RunOfficialPorts(instalar, quitar);
  };
  if (ImGui::Button(i18n::T("Aplicar", "Apply"), ImVec2(160.0f, 34.0f))) aplicar(-1);
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Instalar todos", "Install all"), ImVec2(180.0f, 34.0f))) aplicar(1);
  ImGui::SameLine();
  if (ImGui::Button(i18n::T("Quitar todos", "Remove all"), ImVec2(180.0f, 34.0f))) aplicar(0);
  if (ocupado) ImGui::EndDisabled();
  if (trabajo && !pack_msg_.empty()) {
    std::string ultima;
    std::istringstream in(mod_pipeline_.Output());
    for (std::string l; std::getline(in, l);)
      if (!l.empty()) ultima = l;
    ImGui::TextColored(kInfo, "%s %s", ICON_DOWNLOAD, ultima.c_str());
  } else if (!pack_msg_.empty()) {
    ImGui::TextDisabled("%s", pack_msg_.c_str());
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::Spacing();
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
    return i18n::T("Modelo de Budokai 2 con su pelo y cara; golpes y combos de B2; tecnicas, definitiva y voz del donante.",
                   "Budokai 2 model with its hair and face; B2 moves and combos; techniques, ultimate and voice from the donor.");
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
  if (what == 2) {
    imp_entries_.clear();
    imp_presets_.clear();
    imp_preset_ = 0;
  }
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
    } else if (what == 2 && cols.size() >= 3 && cols[0] == "preset") {
      imp_presets_.emplace_back(cols[1], cols[2]);
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
    // queries leave no log behind; a failed one says why (P0-6: error != lista vacia)
    imp_query_failed_ = mod_pipeline_.ExitCode() != 0;
    if ((what == 1 && imp_sources_.empty()) || (what == 2 && imp_entries_.empty())) {
      imp_message_ = last.empty() ? std::string(imp_query_failed_
                                                    ? i18n::T("La consulta fallo.", "The query failed.")
                                                    : i18n::T("No se encontro nada.", "Nothing found."))
                                  : last;
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
    if (imp_pending_ == 1) {
      ImGui::TextDisabled("%s", i18n::T("Buscando juegos...", "Looking for games..."));
    } else {
      // error o nada encontrado: se dice cual y se puede volver a mirar (antes se quedaba vacio)
      if (imp_query_failed_) {
        ImGui::TextColored(ui::kWarn, "%s  %s", ICON_WARN, imp_message_.c_str());
      } else {
        ImGui::TextDisabled("%s", imp_message_.c_str());
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(i18n::T("Reintentar", "Retry"))) {
        imp_loaded_ = false;
        imp_message_.clear();
      }
    }
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
    // Preset de presets_importar.toml: respeta el juego de origen o solo el modelo; las
    // opciones sueltas se cambian aqui y despues en su personaje.toml.
    // La lista sale del propio presets_importar.toml (importar.py lista): un preset nuevo aparece solo.
    ui::RowLabel(i18n::T("Preset", "Preset"),
                 i18n::T("Original: golpes, tecnicas, formas y voces del juego de origen. Solo modelos: el "
                         "cuerpo nuevo con todo lo demas del donante.",
                         "Original: moves, skills, forms and voices from the source game. Models only: the "
                         "new body with everything else from the donor."));
    {
      auto label = [](const std::string& n) -> std::string {
        if (n == "original") return i18n::T("Original (respeta el juego de origen)", "Original (faithful to the source)");
        if (n == "solo_modelos") return i18n::T("Solo modelos (golpes del donante)", "Models only (donor moves)");
        return n;
      };
      imp_preset_ = std::clamp(imp_preset_, 0, std::max(0, int(imp_presets_.size()) - 1));
      const std::string cur = imp_presets_.empty() ? label("original") : label(imp_presets_[size_t(imp_preset_)].first);
      if (ImGui::BeginCombo("##imp_preset", cur.c_str())) {
        for (int i = 0; i < int(imp_presets_.size()); ++i) {
          if (ImGui::Selectable(label(imp_presets_[size_t(i)].first).c_str(), imp_preset_ == i)) imp_preset_ = i;
          if (ImGui::IsItemHovered() && !imp_presets_[size_t(i)].second.empty()) {
            ImGui::SetTooltip("%s", imp_presets_[size_t(i)].second.c_str());
          }
        }
        ImGui::EndCombo();
      }
      // lo que conserva de verdad cada fuente (B2 y SDBH: golpes propios, voz del donante)
      if (!imp_presets_.empty() && !imp_presets_[size_t(imp_preset_)].second.empty()) {
        ImGui::TextDisabled("%s", imp_presets_[size_t(imp_preset_)].second.c_str());
      }
      ImGui::Checkbox(i18n::T("Fisica del pelo y la ropa del donante", "Donor hair and cloth physics"),
                      &imp_physics_);
      ImGui::Checkbox(i18n::T("Pies al suelo (ajustar la cadera)", "Feet on the ground (adjust hips)"),
                      &imp_ground_);
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
      if (!imp_presets_.empty()) r.preset = imp_presets_[size_t(imp_preset_)].first;   // vacio: el primero
      if (imp_physics_) r.options.push_back("fisica=donante");
      if (!imp_ground_) r.options.push_back("altura=false");
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
  TestScroll();
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
      DrawIsoPrepBox();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 4));
  }
  {
    ui::SectionTitle(i18n::T("Creador de personajes (Modo Héroe) (beta)", "Character creator (Hero Mode) (beta)"));
    const bool xv2 = dbz3::settings::Xv2Instalado();
    bool piezas = xv2 && dbz3::settings::CacXv2Piezas();
    ImGui::BeginDisabled(!xv2);
    if (ImGui::Checkbox(i18n::T("Usar piezas de Xenoverse 2 (opcional)", "Use Xenoverse 2 parts (optional)"),
                        &piezas)) {
      dbz3::settings::SetCacXv2Piezas(piezas);
      dbz3::settings::SaveUserSettings();
    }
    ImGui::EndDisabled();
    {   // piezas XV2 aun muy verdes (ojos, brazos): se marca como experimental (23/10, pedido del usuario)
      ImGui::SameLine(0.0f, 10.0f);
      const float pill_h = (ui::GetFonts().sm ? ui::GetFonts().sm->LegacySize : ImGui::GetFontSize()) + 6.0f;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (ImGui::GetFrameHeight() - pill_h) * 0.5f));
      ui::Pill(nullptr, "EXPERIMENTAL", ui::kBlue);
    }
    ImGui::TextDisabled("%s", xv2
        ? i18n::T("Apagado: el creador usa pelo, torso y piernas de los personajes nativos de tu Budokai 3. "
                  "Activado: ofrece ademas las piezas de tu Xenoverse 2 instalado (nunca se reparte arte).",
                  "Off: the creator uses hair, torso and legs from your native Budokai 3 characters. "
                  "On: it also offers parts from your installed Xenoverse 2 (no art is shipped).")
        : i18n::T("Xenoverse 2 no instalado: el creador usa los personajes nativos de B3 (opcion no disponible).",
                  "Xenoverse 2 not installed: the creator uses native B3 characters (option unavailable)."));
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
      "3) crea uno nuevo abajo. Todo se aplica solo al pulsar JUGAR.",
      "Steps: 1) check the free slots, 2) enable or pick a character to edit it, 3) create "
      "a new one below. Everything applies automatically on PLAY."));
  if (dbz3::settings::IsIsoMode()) DrawIsoPrepBox();
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
    for (auto [it_1, iec_1] = std::pair{std::filesystem::directory_iterator(dir, ec), std::error_code{}}; !iec_1 && it_1 != std::filesystem::directory_iterator(); it_1.increment(iec_1)) {
    const auto& e = *it_1;
      // A3: un traje.toml tambien es una fuente (roster_build lo monta), pero no ocupa plaza
      const auto ptoml = e.path() / "personaje.toml";
      const bool costume = !std::filesystem::exists(ptoml, ec) &&
                           std::filesystem::exists(e.path() / "traje.toml", ec);
      if (!e.is_directory(ec) || (!costume && !std::filesystem::exists(ptoml, ec))) continue;
      const std::string text = ReadSmallFile(costume ? e.path() / "traje.toml" : ptoml);
      CharacterSource cs;
      cs.folder = e.path().filename().string();
      cs.name = TomlValue(text, "nombre");
      cs.costume = costume;
      if (costume) {
        cs.target_id = TomlInt(text, "personaje", -1);
        cs.enabled = !std::filesystem::exists(e.path() / ".disabled", ec);
        char_sources_.push_back(cs);
        continue;
      }
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
  ImGui::InputTextWithHint("##characters_filter", i18n::T("Filtrar personajes...", "Filter characters..."),
                           characters_search_, sizeof(characters_search_));
  int characters_shown = 0;
  for (auto& cs : char_sources_) {
    if (!IContains(cs.name + " " + cs.folder, characters_search_)) continue;
    ++characters_shown;
    ImGui::PushID(cs.folder.c_str());
    bool on = cs.enabled;
    if (ImGui::Checkbox("##on", &on)) {
      dbz3::SetModEnabled(cs.folder, on);
      cs.enabled = on;
      mods_loaded_ = false; broken_mods_ = -1;
    }
    ImGui::SameLine();
    PreviewWidget(ModPipeline::ModsDir() / cs.folder / "ui" / "_vista" / "icono.rgba", 0.30f);
    ImGui::SameLine();
    auto it = slots.find(cs.folder);
    char label[160];
    if (cs.costume) {   // solo informativa: sin plaza, donante ni capsulas (se edita en el Kit)
      ImGui::Text(i18n::T("%s  (traje del ID %d)", "%s  (costume of ID %d)"),
                  cs.name.empty() ? cs.folder.c_str() : cs.name.c_str(), cs.target_id);
      ImGui::PopID();
      continue;
    }
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
  if (!characters_shown && characters_search_[0]) ImGui::TextDisabled("%s", i18n::T("Sin resultados.", "No results."));
  for (auto& cs : char_sources_) {
    if (cs.folder == nc_selected_ && !cs.costume) DrawCharacterEditor(cs, slots);
  }
  ImGui::BeginDisabled(mod_pipeline_.IsRunning());
  if (ImGui::Button(i18n::T("Reconstruir ahora", "Rebuild now"), ImVec2(180, 0))) {
    mod_pipeline_.BuildRoster(true);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", i18n::T("(tambien se hace solo al pulsar JUGAR)",
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

  ui::SectionTitle(i18n::T("Retocar las texturas de un personaje", "Retouch a character's textures"));
  ui::Hint(i18n::T(
      "1) Elige el personaje y pulsa «Extraer texturas a PNG».  2) Edita los PNG con cualquier programa de "
      "dibujo.  3) Pulsa «Crear el mod con mis PNG»: se activa solo y lo quitas en la pestaña Mods. "
      "Para cambiar cualquier otra textura del juego (menús, escenarios), usa «Crear tu propio pack de "
      "texturas» en la pestaña Mods.",
      "1) Pick the character and press 'Extract textures to PNG'.  2) Edit the PNGs with any drawing "
      "program.  3) Press 'Create the mod from my PNGs': it turns on by itself and you remove it in the "
      "Mods tab. To change any other game texture (menus, stages), use 'Make your own texture pack' in "
      "the Mods tab."));

  // Lazy-load the catalog (shared with Model Swap).
  if (!catalog_load_attempted_) {
    catalog_load_attempted_ = true;
    mod_pipeline_.LoadCatalog();
  }
  const auto& chars = mod_pipeline_.B3();

  if (!mod_pipeline_.CatalogLoaded() || chars.empty()) {
    ImGui::TextWrapped("%s", i18n::T(
        "Esta herramienta viene en el Kit de modding (DBZ3HD-Kit-Modding.zip): descomprímelo junto a "
        "dbz3.exe y vuelve a abrir esta pestaña. Para jugar no hace falta.",
        "This tool comes with the modding kit (DBZ3HD-Kit-Modding.zip): unzip it next to dbz3.exe and "
        "open this tab again. It is not needed to play."));
    ImGui::TextDisabled(i18n::T("Esperado en: %s", "Expected at: %s"),
                        (rex::filesystem::GetExecutableFolder() /
                         "mod center hd" / "catalog_b3.cat")
                            .string()
                            .c_str());
    ImGui::EndChild();
    return;
  }

  if (tex_src_idx_ >= (int)chars.size()) tex_src_idx_ = -1;

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", i18n::T("Personaje", "Character"));
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
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", i18n::T("Se aplica a", "Applies to"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(340);
  std::string dst_display;
  if (tex_dst_idx_ >= 0) {
    dst_display = chars[tex_dst_idx_].DisplayName();
  }
  const char* dst_label =
      tex_dst_idx_ >= 0
          ? dst_display.c_str()
          : i18n::T("El mismo personaje", "Same character");
  if (ImGui::BeginCombo("##tex_dst", dst_label)) {
    if (ImGui::Selectable(i18n::T("El mismo personaje", "Same character"), tex_dst_idx_ < 0)) {
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
  ui::Hint(i18n::T(
      "Déjalo en «El mismo personaje» salvo que quieras combinarlo con un cambio de modelo.",
      "Leave it on 'Same character' unless you want to combine it with a model swap."));

  // Nombre del mod de texturas.
  ImGui::Text(i18n::T("Nombre del mod", "Mod name"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(300);
  ImGui::InputTextWithHint("##tex_mod", i18n::T("Opcional: se pone solo", "Optional: filled in for you"),
                           tex_mod_buf_, sizeof(tex_mod_buf_));
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
  ImGui::InputTextWithHint("##tex_dir", i18n::T("Opcional: la carpeta del mod", "Optional: the mod's folder"),
                           tex_dir_buf_, sizeof(tex_dir_buf_));
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
  if (ImGui::Button(i18n::T("Crear el mod con mis PNG",
                            "Create the mod from my PNGs"), ImVec2(280, 0))) {
    const int dst_slot = tex_dst_idx_ >= 0 ? chars[tex_dst_idx_].bin : -1;
    mod_pipeline_.BuildTextures(mod_name, dst_slot, active_tex_dir);
  }
  ImGui::EndDisabled();
  ui::Hint(i18n::T(
      "Mete tus PNG editados en el juego y deja el mod activado. Puedes repetirlo cuantas veces quieras.",
      "Puts your edited PNGs into the game and leaves the mod on. You can repeat it as often as you like."));

  // Listar los PNG extraidos de la carpeta activa (para identificar las
  // texturas sin abrir el explorador).
  if (tex_dir_exists) {
    std::vector<std::pair<std::string, std::uintmax_t>> pngs;
    // Non-throwing overload: the folder may vanish between the is_directory
    // check and here (or be unreadable); a thrown filesystem_error out of
    // OnDraw would take the launcher down.
    std::error_code dir_ec;
    for (auto [it_2, iec_2] = std::pair{std::filesystem::directory_iterator(active_tex_dir, dir_ec), std::error_code{}}; !iec_2 && it_2 != std::filesystem::directory_iterator(); it_2.increment(iec_2)) {
    const auto& e = *it_2;
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
  ui::Hint(i18n::T("Para jugar no hace falta tocar nada de aquí: son opciones para buscar fallos o crear mods. "
                   "Si alguien de la comunidad te pide un registro, lo encontrarás en la carpeta logs del juego.",
                   "You don't need to touch anything here to play: these options are for tracking down problems "
                   "or making mods. If someone in the community asks for a log, it is in the game's logs folder."));
  ImGui::Spacing();

  bool dev_mode = dbz3::settings::DevMode();
  if (ImGui::Checkbox(i18n::T("Activar modo Dev (overlay F10)",
                              "Enable Dev mode (F10 overlay)"), &dev_mode)) {
    dbz3::settings::SetDevMode(dev_mode);
  }
  ui::Tip(i18n::T(
        "Anade un overlay en juego (F10) con diagnostico y opciones de prueba.",
        "Adds an in-game overlay (F10) with diagnostics and test switches."));

  bool show_fps = dbz3::settings::ShowFps();
  if (ImGui::Checkbox(i18n::T("Mostrar los FPS en partida", "Show FPS while playing"), &show_fps)) {
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
    const char* hd_texmins_items[] = {
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
