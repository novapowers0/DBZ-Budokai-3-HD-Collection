// dbz3 - Launcher visual kit (see ui_kit.h).

#include "ui_kit.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <string>

#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dbz3::launcher::ui {

namespace {

Fonts g_fonts;

// Latin (Spanish/French/German/Italian accents), punctuation and arrows. The
// SDK's ImGui backend bakes the whole atlas up front, so the ranges stay small.
const ImWchar kTextRanges[] = {0x0020, 0x017F, 0x2010, 0x2027, 0x2190, 0x2193, 0};
// Only the icons ui_kit.h defines (the icon font has thousands; baking them all
// made an atlas the GPU could not create and the launcher stayed black).
const ImWchar kIconRanges[] = {0xE721, 0xE721, 0xE72C, 0xE72C, 0xE73E, 0xE73E, 0xE740, 0xE740,
                               0xE74E, 0xE74E, 0xE767, 0xE768, 0xE771, 0xE771, 0xE774, 0xE774,
                               0xE7A7, 0xE7A7, 0xE7BA, 0xE7BA, 0xE7F4, 0xE7F4, 0xE7FC, 0xE7FC,
                               0xE895, 0xE896, 0xE8AB, 0xE8AB, 0xE8B7, 0xE8B7, 0xE8FA, 0xE8FA,
                               0xE90F, 0xE90F, 0xE946, 0xE946, 0xE958, 0xE958, 0xEA39, 0xEA39,
                               0xEA86, 0xEA86, 0xEC7A, 0xEC7A, 0};

std::string FirstExisting(std::initializer_list<const char*> paths) {
  for (const char* p : paths) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
  }
  return {};
}

float SystemDpiScale() {
#if defined(_WIN32)
  // GetDpiForSystem is Windows 10+; resolve it dynamically so older systems
  // just rasterize at 1x.
  using Fn = UINT(WINAPI*)();
  if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
    if (auto fn = reinterpret_cast<Fn>(GetProcAddress(u, "GetDpiForSystem"))) {
      const UINT dpi = fn();
      if (dpi >= 96) return std::clamp(float(dpi) / 96.0f, 1.0f, 3.0f);
    }
  }
#endif
  return 1.0f;
}

ImFont* AddFont(ImFontAtlas* atlas, const std::string& path, float size,
                const std::string& icons, float density) {
  if (path.empty()) return nullptr;
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.RasterizerDensity = density;
  ImFont* f = atlas->AddFontFromFileTTF(path.c_str(), size, &cfg, kTextRanges);
  if (f && !icons.empty()) {
    ImFontConfig ic;
    ic.MergeMode = true;
    ic.OversampleH = 2;
    ic.RasterizerDensity = density;
    ic.GlyphOffset = ImVec2(0.0f, size * 0.12f);
    ic.GlyphMinAdvanceX = size * 1.1f;
    atlas->AddFontFromFileTTF(icons.c_str(), size * 0.92f, &ic, kIconRanges);
  }
  return f;
}

}  // namespace

void LoadFonts(ImFontAtlas* atlas) {
  const std::string regular = FirstExisting({
      "C:\\Windows\\Fonts\\segoeui.ttf",
      "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "Z:\\usr\\share\\fonts\\truetype\\dejavu\\DejaVuSans.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",  // macOS
  });
  if (regular.empty()) {
    REXLOG_WARN("dbz3 launcher: no system UI font found; using the default font");
    return;
  }
  const std::string semibold = FirstExisting({
      "C:\\Windows\\Fonts\\seguisb.ttf",
      "C:\\Windows\\Fonts\\segoeuib.ttf",
      "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/System/Library/Fonts/Supplemental/Arial Bold.ttf",  // macOS
  });
  const std::string black = FirstExisting({
      "C:\\Windows\\Fonts\\seguibl.ttf",
      "C:\\Windows\\Fonts\\segoeuib.ttf",
      "/System/Library/Fonts/Supplemental/Arial Black.ttf",  // macOS
  });
  const std::string icons = FirstExisting({
      "C:\\Windows\\Fonts\\SegoeIcons.ttf",
      "C:\\Windows\\Fonts\\segmdl2.ttf",
  });
  const std::string bold_path = semibold.empty() ? regular : semibold;
  const float density = SystemDpiScale();
  g_fonts.body = AddFont(atlas, regular, 16.0f, icons, density);
  g_fonts.bold = AddFont(atlas, bold_path, 16.0f, icons, density);
  g_fonts.sm = AddFont(atlas, regular, 13.5f, icons, density);
  g_fonts.h2 = AddFont(atlas, bold_path, 19.0f, icons, density);
  g_fonts.title = AddFont(atlas, black.empty() ? bold_path : black, 30.0f, {}, density);
  // Same face as the generated name banners (roster_build.py FONT).
  g_fonts.banner = AddFont(atlas, FirstExisting({"C:\\Windows\\Fonts\\comicbd.ttf",
                                                    "/System/Library/Fonts/Supplemental/Comic Sans MS Bold.ttf"}), 44.0f, {}, density);
  // Same face as the generated capsule names (capsulas.py); menus of the game look like it.
  const std::string rounded = FirstExisting({"C:\\Windows\\Fonts\\ARLRDBD.TTF",
                                                "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf"});
  g_fonts.menu = AddFont(atlas, rounded.empty() ? bold_path : rounded, 48.0f, {}, density);
  g_fonts.speech = AddFont(atlas, bold_path, 30.0f, {}, density);
  REXLOG_INFO("dbz3 launcher: fonts {} / {} (icons: {}), density {:.2f}", regular, bold_path,
              icons.empty() ? "none" : icons, density);
}

const Fonts& GetFonts() { return g_fonts; }

void BeginCard(const char* id, const char* icon, const char* title, const char* subtitle,
               float height) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
  ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding;
  if (height == 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
  ImGui::BeginChild(id, ImVec2(0.0f, height), flags);
  ImGui::PopStyleVar(2);
  if (title && *title) {
    {
      FontScope f(GetFonts().h2);
      if (icon && *icon) {
        ImGui::TextColored(kAccent, "%s", icon);
        ImGui::SameLine(0.0f, 10.0f);
      }
      ImGui::TextUnformatted(title);
    }
    if (subtitle && *subtitle) {
      FontScope f(GetFonts().sm);
      ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
      ImGui::TextWrapped("%s", subtitle);
      ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), ImGui::GetColorU32(kLine), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
  }
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void SectionTitle(const char* title) {
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetTextLineHeight();
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + 3.0f, p.y + h - 1.0f),
                                            ImGui::GetColorU32(kAccent), 2.0f);
  ImGui::SetCursorScreenPos(ImVec2(p.x + 11.0f, p.y));
  {
    FontScope f(GetFonts().bold);
    ImGui::TextColored(kAccent, "%s", title);
  }
  ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

void RowLabel(const char* label, const char* help) {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float label_w = std::clamp(avail * 0.42f, 170.0f, 330.0f);
  const float x0 = ImGui::GetCursorPosX();
  ImGui::BeginGroup();
  ImGui::AlignTextToFramePadding();
  ImGui::PushTextWrapPos(x0 + label_w - 14.0f);
  ImGui::TextUnformatted(label);
  if (help && *help) {
    FontScope f(GetFonts().sm);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("%s", help);
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::SameLine(x0 + label_w);
  ImGui::SetNextItemWidth(std::min(avail - label_w, 440.0f));
}

void Tip(const char* text) {
  const bool item_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
  ImGui::SameLine(0.0f, 6.0f);
  {
    FontScope f(GetFonts().sm);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(kTextDim.x, kTextDim.y, kTextDim.z, 0.8f), "%s", ICON_INFO);
  }
  if (item_hovered || ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void Pill(const char* icon, const char* text, ImVec4 color) {
  FontScope f(GetFonts().sm);
  std::string s;
  if (icon && *icon) {
    s = icon;
    s += "  ";
  }
  s += text;
  const ImVec2 pad(10.0f, 3.0f);
  const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p.x + ts.x + pad.x * 2.0f, p.y + ts.y + pad.y * 2.0f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, p1, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f)), 99.0f);
  dl->AddRect(p, p1, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.55f)), 99.0f);
  dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(color), s.c_str());
  ImGui::Dummy(ImVec2(p1.x - p.x, p1.y - p.y));
}

bool IconButton(const char* icon, const char* label, ImVec2 size) {
  std::string s = icon && *icon ? std::string(icon) + "   " + label : std::string(label);
  FontScope f(GetFonts().bold);
  return ImGui::Button(s.c_str(), size);
}

void Hint(const char* text) {
  FontScope f(GetFonts().sm);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool NameBanner(const char* text, float height, float max_width) {
  ImFont* f = GetFonts().banner;
  if (!f || !text || !*text) {
    ImGui::Dummy(ImVec2(max_width, height));
    return f != nullptr;
  }
  // roster_build: 64 px text with a 7 px stroke, cropped, scaled to 78 % of the
  // banner height and squeezed horizontally when wider than the free width.
  const float base = f->LegacySize;
  const float stroke = base * 7.0f / 64.0f;
  const ImVec2 ts = f->CalcTextSizeA(base, FLT_MAX, 0.0f, text);
  const float ink_w = ts.x + stroke * 2.0f, ink_h = ts.y + stroke * 2.0f;
  const float margin = 13.0f * height / 32.0f;
  const float th = height * 0.78f;
  const float scale_y = th / ink_h;
  const float scale_x = std::min(scale_y, (max_width - margin - 4.0f) / ink_w);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Glyphs are laid out at the base size first, which can reach past the
  // window's clip rect before the squeeze: lay out unclipped, scale, then the
  // final quads sit inside the reserved area.
  const ImVec4 outer = dl->_CmdHeader.ClipRect;   // the window's clip, kept for the final quads
  dl->PushClipRectFullScreen();
  const int c0 = dl->CmdBuffer.Size - 1;
  const int v0 = dl->VtxBuffer.Size;
  const ImVec2 o(p.x + stroke, p.y + stroke);
  const ImU32 black = IM_COL32(0, 0, 0, 255);
  for (int k = 0; k < 16; ++k) {
    const float a = k * 3.14159265f / 8.0f;
    dl->AddText(f, base, ImVec2(o.x + std::cos(a) * stroke, o.y + std::sin(a) * stroke), black, text);
  }
  dl->AddText(f, base, ImVec2(o.x + stroke * 0.5f, o.y), black, text);
  dl->AddText(f, base, o, IM_COL32(255, 255, 255, 255), text);
  // Scale the glyph quads in place: x and y independently (the squeeze).
  const float oy = p.y + (height - th) * 0.5f;
  for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
    ImDrawVert& v = dl->VtxBuffer[i];
    v.pos.x = p.x + margin + (v.pos.x - p.x) * scale_x;
    v.pos.y = oy + (v.pos.y - p.y) * scale_y;
  }
  // The scissor of the commands drawn above goes back to the window's clip rect,
  // so a scrolled-away banner doesn't show over the footer.
  for (int c = std::max(c0, 0); c < dl->CmdBuffer.Size; ++c) {
    dl->CmdBuffer[c].ClipRect = outer;
  }
  dl->PopClipRect();
  ImGui::Dummy(ImVec2(max_width, height));
  return true;
}

}  // namespace dbz3::launcher::ui
