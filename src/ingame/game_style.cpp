// dbz3 - Ver game_style.h. Medidas tomadas de capturas del menu de Duelo a 1600x900 (area 4:3
// de 1200x900 desde x=200): marco 320..885 x 150..580, filas cada 82 px, bocadillo abajo.
#include "game_style.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

#include <rex/cvar.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

#include "../launcher/launcher_state.h"
#include "../launcher/settings.h"   // S1-F06: AspectMode (GuestImage)
#include "../launcher/ui_kit.h"
#include "../mods.h"
#include "native_ui.h"

namespace dbz3::ingame::style {
namespace {

ImFont* MenuFont() {
  const auto& f = dbz3::launcher::ui::GetFonts();
  return f.menu ? f.menu : ImGui::GetFont();
}

// Nueve trozos: las esquinas (m px de la textura) no se deforman; k = px de pantalla por px de textura.
// top_mid (x0, x1 px de la textura, opcional): tramo del borde de arriba que se estira en vez del entero.
void NineSlice(ImDrawList* dl, const Pic& p, ImVec4 src, float m, ImVec2 q0, ImVec2 q1, float k,
               ImU32 tint = IM_COL32_WHITE, ImVec2 top_mid = ImVec2(0, 0)) {
  const float xs[4] = {q0.x, q0.x + m * k, q1.x - m * k, q1.x}, ys[4] = {q0.y, q0.y + m * k, q1.y - m * k, q1.y};
  const float us[4] = {src.x / p.w, (src.x + m) / p.w, (src.z - m) / p.w, src.z / p.w};
  const float vs[4] = {src.y / p.h, (src.y + m) / p.h, (src.w - m) / p.h, src.w / p.h};
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) {
      const bool clean = j == 0 && i == 1 && top_mid.y > top_mid.x;
      dl->AddImage(p.tex, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]),
                   ImVec2(clean ? top_mid.x / p.w : us[i], vs[j]), ImVec2(clean ? top_mid.y / p.w : us[i + 1], vs[j + 1]),
                   tint);
    }
}

// Texto con perfil y relleno en dos tonos (arriba/abajo), como las letras del juego.
void OutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* text, ImU32 top,
                  ImU32 bottom, ImU32 outline, float thick) {
  const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  const int steps = 12;
  for (int i = 0; i < steps; ++i) {
    const float a = 6.2831853f * float(i) / steps;
    dl->AddText(font, size, ImVec2(pos.x + std::cos(a) * thick, pos.y + std::sin(a) * thick), outline, text);
  }
  const float mid = pos.y + ts.y * 0.52f;
  dl->PushClipRect(ImVec2(pos.x - 4, pos.y - 4), ImVec2(pos.x + ts.x + 4, mid), true);
  dl->AddText(font, size, pos, top, text);
  dl->PopClipRect();
  dl->PushClipRect(ImVec2(pos.x - 4, mid), ImVec2(pos.x + ts.x + 4, pos.y + ts.y + 4), true);
  dl->AddText(font, size, pos, bottom, text);
  dl->PopClipRect();
}

}  // namespace

Imagen GuestImage(const ImGuiIO& io) {
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  Imagen g{ImVec2(0, 0), ImVec2(W, H)};
  if (W <= 0 || H <= 0) return g;
  static int cuadro = -1;   // se lee una vez por cuadro (se puede cambiar en el menu F1)
  static bool estirar = false;
  if (cuadro != ImGui::GetFrameCount()) {
    cuadro = ImGui::GetFrameCount();
    estirar = dbz3::settings::AspectMode() == "stretch";
  }
  if (estirar) return g;
  // S5-I1 (issue #29): con la resolucion baja (480p) el juego dibuja 640x480 (4:3), no 16:9
  static const bool baja = dbz3::settings::LowResEnabled();
  const float an = baja ? 4.0f : 16.0f, al = baja ? 3.0f : 9.0f;
  if (W * al > H * an) {   // mas ancha que la imagen: bandas a los lados
    const float w = H * an / al;
    g.p0.x = (W - w) * 0.5f;
    g.p1.x = g.p0.x + w;
  } else {                      // mas alta: bandas arriba y abajo
    const float h = W * al / an;
    g.p0.y = (H - h) * 0.5f;
    g.p1.y = g.p0.y + h;
  }
  return g;
}

Area GameArea(const ImGuiIO& io) {
  // S1-F06: el area 4:3 de los menus del juego es el centro de su imagen 16:9 (3/4 del ancho, todo el alto)
  Area a;
  const Imagen g = GuestImage(io);
  const ImVec2 sz = g.Size();
  float h = sz.y, w = h * 4.0f / 3.0f;
  if (w > sz.x) {   // solo con "stretch" en una ventana mas alta que 4:3: que quepa a lo ancho
    w = sz.x;
    h = w * 0.75f;
  }
  a.p0 = ImVec2(g.p0.x + (sz.x - w) * 0.5f, g.p0.y + (sz.y - h) * 0.5f);
  a.p1 = ImVec2(a.p0.x + w, a.p0.y + h);
  a.s = h / 900.0f;
  return a;
}

Pic Img(const char* name) {
  struct Entry {
    std::unique_ptr<rex::ui::ImmediateTexture> tex;
    Pic pic;
  };
  static std::map<std::string, Entry> cache;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return {};
  auto [it, fresh] = cache.try_emplace(name);
  if (!fresh) return it->second.pic;
  auto upload = [&](const uint8_t* px, int w, int h) {
    it->second.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px);
    if (it->second.tex) it->second.pic = {reinterpret_cast<ImTextureID>(it->second.tex.get()), float(w), float(h)};
  };
  const std::filesystem::path root = dbz3::ModsRoot();
  const std::string file = std::string(name) + ".png";
  std::vector<std::filesystem::path> cands;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
    const std::string n = e.path().filename().string();
    const bool off = n.size() > 9 && n.compare(n.size() - 9, 9, ".disabled") == 0;
    if (e.is_directory(ec) && !off && !std::filesystem::exists(e.path() / ".disabled", ec)) {
      cands.push_back(e.path() / "ui_ingame" / file);
    }
  }
  std::sort(cands.begin(), cands.end());   // como los mods de AFS: gana el primero por nombre
  // sin mod: la pieza del juego (native_ui, sacada de los AFS del jugador) y si no, ingame_ui/
  // (lo nuestro: titulos). Mientras se extrae no se guarda el fallo, se reintenta.
  const bool in_mod = std::any_of(cands.begin(), cands.end(), [&](const auto& f) { return std::filesystem::exists(f, ec); });
  if (!in_mod && std::strncmp(name, "hub/", 4) != 0) {   // hub/*: siempre nuestras (pack Dragon HUB), tambien en el launcher
    if (!native_ui::Ready()) {
      cache.erase(it);
      return {};
    }
    native_ui::Image img;
    if (native_ui::Take(name, img)) {
      upload(img.rgba.data(), img.w, img.h);
      return it->second.pic;
    }
  }
  cands.push_back(root.parent_path() / "ingame_ui" / file);
  for (const auto& f : cands) {
    std::ifstream in(f, std::ios::binary);
    if (!in) continue;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0;
    const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
    if (px.empty()) continue;
    upload(px.data(), w, h);
    break;
  }
  return it->second.pic;
}

void DrawPic(ImDrawList* dl, const Pic& p, ImVec2 p0, ImVec2 p1, ImVec4 src, ImU32 tint, bool mirror) {
  if (!p) return;
  if (src.z <= 0) src = ImVec4(0, 0, p.w, p.h);
  ImVec2 uv0(src.x / p.w, src.y / p.h), uv1(src.z / p.w, src.w / p.h);
  if (mirror) std::swap(uv0.x, uv1.x);
  dl->AddImage(p.tex, p0, p1, uv0, uv1, tint);
}

void Frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s) {
  const ImU32 gold = IM_COL32(232, 140, 32, 255), dark = IM_COL32(110, 50, 10, 255);
  if (const Pic pn = Img("panel")) {   // el interior de las listas del juego (data_usi 2033)
    DrawPic(dl, pn, p0, p1, ImVec4(4, 7, 309, 350), IM_COL32(255, 255, 255, 215));
  } else {
    dl->AddRectFilledMultiColor(p0, p1, IM_COL32(70, 82, 130, 200), IM_COL32(70, 82, 130, 200),
                                IM_COL32(45, 52, 95, 215), IM_COL32(45, 52, 95, 215));
  }
  // marco del juego (data_usi 2033): la linea va 14 px dentro del recorte 8,5-402,447
  if (const Pic m = Img("marco")) {
    const float k = 1.2f * s, in = 14 * k;
    NineSlice(dl, m, ImVec4(8, 5, 402, 447), 46, ImVec2(p0.x - in, p0.y - in), ImVec2(p1.x + in, p1.y + in), k);
    return;
  }
  const float t = 7.0f * s, g = 10.0f * s;
  dl->AddRect(ImVec2(p0.x - g, p0.y - g), ImVec2(p1.x + g, p1.y + g), dark, 6.0f * s, 0, t + 3 * s);
  dl->AddRect(ImVec2(p0.x - g, p0.y - g), ImVec2(p1.x + g, p1.y + g), gold, 6.0f * s, 0, t);
  // remates de las esquinas (cuadrados con borde, como el marco del juego)
  const float k = 22.0f * s;
  for (const ImVec2 c : {ImVec2(p0.x - g, p0.y - g), ImVec2(p1.x + g, p0.y - g), ImVec2(p0.x - g, p1.y + g),
                         ImVec2(p1.x + g, p1.y + g)}) {
    dl->AddRectFilled(ImVec2(c.x - k * 0.5f, c.y - k * 0.5f), ImVec2(c.x + k * 0.5f, c.y + k * 0.5f), gold, 3 * s);
    dl->AddRect(ImVec2(c.x - k * 0.5f, c.y - k * 0.5f), ImVec2(c.x + k * 0.5f, c.y + k * 0.5f), dark, 3 * s, 0, 2.5f * s);
  }
  // barras de arriba y abajo (el juego pone una pieza mas corta encima y debajo del marco)
  const float bw = (p1.x - p0.x) * 0.55f, cx = (p0.x + p1.x) * 0.5f;
  for (const float y : {p0.y - g - 9 * s, p1.y + g + 9 * s}) {
    dl->AddRectFilled(ImVec2(cx - bw * 0.5f, y - 5 * s), ImVec2(cx + bw * 0.5f, y + 5 * s), gold, 3 * s);
    dl->AddRect(ImVec2(cx - bw * 0.5f, y - 5 * s), ImVec2(cx + bw * 0.5f, y + 5 * s), dark, 3 * s, 0, 2 * s);
  }
}

void Highlight(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float t) {
  // el agua azul del juego (data_usi 2033, 512x128) bajo el texto elegido, moviendose despacio:
  // una ventana del ancho de la franja que va y viene por la textura
  if (const Pic w = Img("agua")) {
    const float aspect = (p1.x - p0.x) / std::max(1.0f, p1.y - p0.y);
    const float vh = std::min(1.0f, 0.7f * 4.0f / std::max(1.0f, aspect));   // alto usado de la textura
    const float span = aspect * vh * 0.25f;                                   // ancho usado (0..1)
    const float u = (std::sin(t * 0.35f) * 0.5f + 0.5f) * std::max(0.0f, 1.0f - span);
    const float v = (std::sin(t * 0.23f) * 0.5f + 0.5f) * (1.0f - vh);
    dl->AddImage(w.tex, p0, p1, ImVec2(u, v), ImVec2(u + span, v + vh), IM_COL32(255, 255, 255, 240));
    dl->AddRect(p0, p1, IM_COL32(165, 244, 230, 170), 0, 0, 2.0f);   // filo turquesa (franja 2033)
    return;
  }
  dl->AddRectFilledMultiColor(p0, p1, IM_COL32(70, 110, 200, 235), IM_COL32(110, 150, 230, 235),
                              IM_COL32(60, 95, 185, 235), IM_COL32(40, 70, 160, 235));
  // ondas claras que se mueven, como el agua de la franja del juego
  const float h = p1.y - p0.y;
  for (int i = 0; i < 6; ++i) {
    const float x = p0.x + std::fmod(t * 60.0f + i * 97.0f, std::max(1.0f, p1.x - p0.x));
    dl->AddLine(ImVec2(x, p0.y + h * 0.25f), ImVec2(x + h * 0.6f, p0.y + h * 0.45f), IM_COL32(200, 230, 255, 70), 3.0f);
  }
}

void MenuText(ImDrawList* dl, ImVec2 center, float size, const char* text, bool selected, bool disabled, float max_w) {
  ImFont* font = MenuFont();
  ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  if (max_w > 0 && ts.x > max_w) {   // no se sale del marco: letra mas pequena
    size *= max_w / ts.x;
    ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  }
  const ImVec2 pos(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
  // como las filas del juego (data_usi 2055): turquesa, perfil negro fino y aro claro por fuera
  const float th = size * 0.05f;
  auto draw = [&](ImU32 top, ImU32 bottom, ImU32 ring) {
    OutlinedText(dl, font, size, pos, text, ring, ring, ring, th * 2.0f);
    OutlinedText(dl, font, size, pos, text, top, bottom, IM_COL32(0, 0, 0, 255), th);
  };
  // las no elegidas, un turquesa algo mas apagado pero legible sobre el panel oscuro; las apagadas, gris claro
  if (disabled) {
    draw(IM_COL32(150, 158, 172, 235), IM_COL32(118, 126, 142, 235), IM_COL32(40, 44, 58, 170));
  } else if (selected) {
    draw(IM_COL32(2, 210, 231, 255), IM_COL32(2, 182, 200, 255), IM_COL32(223, 223, 223, 210));
  } else {
    draw(IM_COL32(40, 178, 195, 255), IM_COL32(22, 148, 165, 255), IM_COL32(185, 198, 205, 175));
  }
}

void MenuTextB3(ImDrawList* dl, ImVec2 center, float size, const char* text, bool selected, bool disabled, float max_w) {
  ImFont* font = MenuFont();
  ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  if (max_w > 0 && ts.x > max_w) {
    size *= max_w / ts.x;
    ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  }
  const ImVec2 pos(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
  const ImU32 top = disabled ? IM_COL32(170, 176, 188, 255) : selected ? IM_COL32(255, 232, 64, 255) : IM_COL32(255, 255, 255, 255);
  const ImU32 bottom = disabled ? IM_COL32(140, 146, 160, 255) : selected ? IM_COL32(255, 200, 24, 255) : IM_COL32(255, 255, 255, 255);
  OutlinedText(dl, font, size, pos, text, top, bottom, IM_COL32(17, 12, 8, 255), size * 0.075f);
}

void Banner(ImDrawList* dl, const Area& a, const char* text, const char* title) {
  // medido en el Duelo (1600x900): pergamino (data_usi 2033, recorte 4,2-452,132) a 1.35 desde
  // 12,20 del area y el titulo de unos 93 px de alto desde 44,34
  const Pic tp = title ? Img(title) : Pic{};
  if (const Pic sc = Img("pergamino")) {
    const float k = 1.35f * a.s;
    DrawPic(dl, sc, a.At(12, 20), ImVec2(a.At(12, 20).x + 448 * k, a.At(12, 20).y + 130 * k), ImVec4(4, 2, 452, 132));
  } else {
    const ImVec2 p0 = a.At(20, 10);
    const float s = a.s, w = 560 * s, h = 110 * s;
    const ImVec2 q(p0.x + 40 * s, p0.y + 20 * s), r(q.x + w, q.y + h * 0.78f);
    dl->AddQuadFilled(q, ImVec2(r.x, q.y + 18 * s), ImVec2(r.x - 20 * s, r.y + 18 * s), ImVec2(q.x, r.y),
                      IM_COL32(235, 240, 245, 255));
    dl->AddRectFilled(ImVec2(p0.x + 18 * s, p0.y), ImVec2(p0.x + 58 * s, p0.y + h + 20 * s), IM_COL32(40, 130, 60, 255), 12 * s);
  }
  if (tp) {
    const float th = 100 * a.s, tw = tp.w * th / tp.h;   // nuestros titulos llevan perfil y sombra: 160 px = 93 del juego
    DrawPic(dl, tp, a.At(40, 30), ImVec2(a.At(40, 30).x + tw, a.At(40, 30).y + th));
    return;
  }
  const auto& fonts = dbz3::launcher::ui::GetFonts();
  ImFont* font = fonts.banner ? fonts.banner : ImGui::GetFont();
  // un titulo largo ("Supervivencia con tienda") se encoge para no salirse del pergamino (texto de x 60 a 450)
  float size = 72 * a.s;
  const float w = font->CalcTextSizeA(size, FLT_MAX, 0, text).x, room = 390 * a.s;
  if (w > room) size *= room / w;
  const ImVec2 at = a.At(60, 45);
  OutlinedText(dl, font, size, ImVec2(at.x, at.y + (72 * a.s - size) * 0.45f), text, IM_COL32(255, 215, 60, 255),
               IM_COL32(245, 120, 20, 255), IM_COL32(90, 35, 10, 255), size * 0.06f);
}

void TitleText(ImDrawList* dl, const Area& a, float cx, float cy, float ancho, const char* text, float escala) {
  const auto& fonts = dbz3::launcher::ui::GetFonts();
  ImFont* font = fonts.banner ? fonts.banner : ImGui::GetFont();
  float size = 72 * a.s * escala;
  const float w = font->CalcTextSizeA(size, FLT_MAX, 0, text).x, room = ancho * a.s * escala;
  if (w > room) size *= room / w;
  const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text), c = a.At(cx, cy);
  OutlinedText(dl, font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), text, IM_COL32(255, 215, 60, 255),
               IM_COL32(245, 120, 20, 255), IM_COL32(90, 35, 10, 255), size * 0.06f);
}

void Speech(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s, const char* text, float tail_x) {
  const auto& fonts = dbz3::launcher::ui::GetFonts();
  ImFont* font = fonts.speech ? fonts.speech : ImGui::GetFont();
  if (const Pic b = Img("bocadillo")) {
    // bocadillo del juego (data_usi 2055): cuerpo 5,32-585,196 en nueve trozos y el pico (340..450
    // x 4..42, centro en x 395) encima, en su sitio o reflejado si sale hacia la izquierda
    // (el borde de arriba se estira desde 60..330: en 340..450 la textura lleva el hueco donde se pega el pico)
    const float k = 1.1f * s;
    NineSlice(dl, b, ImVec4(5, 32, 585, 196), 30, p0, p1, k, IM_COL32_WHITE, ImVec2(60, 330));
    if (tail_x >= 0) {
      const bool left = tail_x < 0.5f;
      const float cx = p0.x + (p1.x - p0.x) * tail_x, x0 = left ? cx - (450 - 395) * k : cx - (395 - 340) * k;
      DrawPic(dl, b, ImVec2(x0, p0.y - 28 * k), ImVec2(x0 + 110 * k, p0.y + 10 * k), ImVec4(340, 4, 450, 42),
              IM_COL32_WHITE, left);
    }
  } else {
    if (tail_x >= 0) {
      const float cx = p0.x + (p1.x - p0.x) * tail_x, dir = tail_x < 0.5f ? -1.0f : 1.0f;
      dl->AddTriangleFilled(ImVec2(cx - 30 * s, p0.y + 2), ImVec2(cx + 30 * s, p0.y + 2),
                            ImVec2(cx + dir * 50 * s, p0.y - 40 * s), IM_COL32(248, 246, 236, 235));
    }
    dl->AddRectFilled(p0, p1, IM_COL32(248, 246, 236, 235), 26 * s);
    dl->AddRect(p0, p1, IM_COL32(80, 130, 110, 255), 26 * s, 0, 3 * s);
  }
  float size = 40.0f * s;
  const float wrap = (p1.x - p0.x) - 70 * s;   // como el Duelo
  ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, text);
  // texto largo (codigo de sala + red + mods): letra mas pequena hasta que quepa en el bocadillo
  for (const float room = (p1.y - p0.y) - 24 * s; ts.y > room && size > 18.0f * s;) {
    size *= 0.92f;
    ts = font->CalcTextSizeA(size, FLT_MAX, wrap, text);
  }
  // si aun no cabe (aleman, frances, codigos largos), se recorta dentro del bocadillo y no se sale
  dl->PushClipRect(p0, p1, true);
  dl->AddText(font, size, ImVec2(p0.x + ((p1.x - p0.x) - ts.x) * 0.5f, p0.y + ((p1.y - p0.y) - ts.y) * 0.5f),
              IM_COL32(25, 25, 25, 255), text, nullptr, wrap);
  dl->PopClipRect();
}

namespace mode {
void Stripe(ImDrawList* dl, const Area& a, float x0, float y0, float x1, float y1, float t) {
  Highlight(dl, a.At(x0 + kLine, y0), a.At(x1 - kLine, y1), t);
}
void Bubble(ImDrawList* dl, const Area& a, const char* text) {
  // a la derecha del anfitrion (no le tapa el cuerpo), con el pico hacia el
  Speech(dl, a.At(400, 652), a.At(kX1, 848), a.s, text, 0.14f);   // (0.14: el pico no sale del borde)
}
void Help(ImDrawList* dl, const Area& a, const std::vector<std::pair<const char*, const char*>>& items) {
  HelpLine(dl, a.At(600, 872), 38 * a.s, items);
}
}  // namespace mode

bool Borders() { return rex::cvar::GetFlagByName("dbz3_ui_side_borders") == "true"; }

void Backdrop(ImDrawList* dl, const Area& a, const char* image) {
  // fondos del juego: dibujo 4:3 en la esquina de una textura de 1024 (900x672 o 912x684); los nuestros, 16:9
  // enteros. Sin bordes (o con un fondo ancho) llenan toda la ventana, recortando lo que sobra.
  if (const Pic p = image ? Img(image) : Pic{}) {
    const bool wide = p.w > p.h * 1.5f;
    const float w = wide ? p.w : p.w == p.h ? 900.0f : p.w, h = wide ? p.h : w * 0.75f;
    if (!wide && Borders()) {
      DrawPic(dl, p, a.p0, a.p1, ImVec4(0, 0, w, h));
      return;
    }
    // S1-F06: a toda la imagen del juego (en 4:3 / 16:10 no tapa sus bandas negras)
    const Imagen g = GuestImage(ImGui::GetIO());
    const ImVec2 d = g.Size();
    const float k = std::max(d.x / w, d.y / h), x0 = g.p0.x + (d.x - w * k) * 0.5f, y0 = g.p0.y + (d.y - h * k) * 0.5f;
    dl->PushClipRect(g.p0, g.p1, true);
    DrawPic(dl, p, ImVec2(x0, y0), ImVec2(x0 + w * k, y0 + h * k), ImVec4(0, 0, w, h));
    dl->PopClipRect();
    return;
  }
  dl->AddRectFilledMultiColor(a.p0, a.p1, IM_COL32(250, 170, 70, 255), IM_COL32(250, 170, 70, 255),
                              IM_COL32(120, 160, 230, 255), IM_COL32(120, 160, 230, 255));
  // rayas diagonales suaves de relleno (sustituto del dibujo de fondo)
  for (float x = a.p0.x - (a.p1.y - a.p0.y); x < a.p1.x; x += 60 * a.s) {
    dl->AddLine(ImVec2(x, a.p1.y), ImVec2(x + (a.p1.y - a.p0.y), a.p0.y), IM_COL32(255, 255, 255, 22), 18 * a.s);
  }
}

void Host(ImDrawList* dl, const Area& a, const char* image, float x, float y, bool mirror) {
  const Pic p = Img(image);
  if (!p) return;
  const float k = 1.33f * a.s;   // como los Kaio del Duelo
  // con bordes se corta en el area 4:3 (las tiras lo tapan); sin ellos, en la ventana
  if (Borders()) dl->PushClipRect(a.p0, a.p1, true);
  else {   // S1-F06: en la imagen del juego
    const Imagen g = GuestImage(ImGui::GetIO());
    dl->PushClipRect(g.p0, g.p1, true);
  }
  DrawPic(dl, p, a.At(x, y), ImVec2(a.At(x, y).x + p.w * k, a.At(x, y).y + p.h * k), ImVec4(0, 0, 0, 0),
          IM_COL32_WHITE, mirror);
  dl->PopClipRect();
}

void Sides(ImDrawList* dl, const Area& a) {
  // data_usi 2033, 128x1024: tira opaca en x 14..63 y su sombra a la izquierda; es la de la derecha
  // y la izquierda va reflejada (la sombra cae siempre hacia dentro), a 1.33 como en el Duelo. Solo con la
  // opcion "Bordes dorados" (F1)
  const Pic p = Borders() ? Img("borde") : Pic{};
  if (!p) return;
  const float k = 1.33f * a.s, w = 64 * k, h = std::min(p.h * k, a.p1.y - a.p0.y);
  const ImVec4 src(0, 0, 64, h / k);
  DrawPic(dl, p, ImVec2(a.p1.x - w, a.p0.y), ImVec2(a.p1.x, a.p0.y + h), src);
  DrawPic(dl, p, a.p0, ImVec2(a.p0.x + w, a.p0.y + h), src, IM_COL32_WHITE, true);
}

float Glyph(ImDrawList* dl, const char* button, ImVec2 pos, float h) {
  if (const char* plus = std::strchr(button, '+')) {   // "BACK+LB": los dos con un + entre medias
    const std::string first(button, plus);
    float w = Glyph(dl, first.c_str(), pos, h);
    ImFont* font = MenuFont();
    const ImVec2 ps = font->CalcTextSizeA(h * 0.6f, FLT_MAX, 0, "+");
    if (dl) {
      OutlinedText(dl, font, h * 0.6f, ImVec2(pos.x + w + 2, pos.y - ps.y * 0.5f), "+", IM_COL32(255, 255, 255, 255),
                   IM_COL32(220, 220, 220, 255), IM_COL32(0, 0, 0, 255), 1.5f);
    }
    w += ps.x + 4;
    return w + Glyph(dl, plus + 1, ImVec2(pos.x + w, pos.y), h);
  }
  // botones del juego: tira LB LT RB RT Y B A X (data_usi 2062), BACK (2033) y START (2062)
  struct G {
    const char* name;
    const char* pic;
    ImVec4 src;
  };
  static const G kGlyphs[] = {
      {"LB", "botones", ImVec4(4, 2, 56, 46)},     {"LT", "botones", ImVec4(74, 2, 108, 46)},
      {"RB", "botones", ImVec4(126, 2, 178, 46)},  {"RT", "botones", ImVec4(194, 2, 233, 46)},
      {"Y", "botones", ImVec4(249, 2, 284, 46)},   {"B", "botones", ImVec4(297, 2, 332, 46)},
      {"A", "botones", ImVec4(346, 2, 381, 46)},   {"X", "botones", ImVec4(393, 2, 428, 46)},
      {"BACK", "boton_back", ImVec4(76, 1, 127, 46)}, {"START", "boton_start", ImVec4(10, 4, 47, 32)}};
  for (const G& g : kGlyphs) {
    if (std::strcmp(g.name, button) != 0) continue;
    if (const Pic p = Img(g.pic)) {
      const float w = (g.src.z - g.src.x) * h / (g.src.w - g.src.y);
      if (dl) DrawPic(dl, p, ImVec2(pos.x, pos.y - h * 0.5f), ImVec2(pos.x + w, pos.y + h * 0.5f), g.src);
      return w;
    }
  }
  // sin la pieza: circulo con el nombre
  ImFont* font = MenuFont();
  const float fs = h * (std::strlen(button) > 2 ? 0.35f : 0.55f);
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, button);
  const float w = std::max(h, ts.x + h * 0.4f);
  if (!dl) return w;
  dl->AddRectFilled(ImVec2(pos.x, pos.y - h * 0.45f), ImVec2(pos.x + w, pos.y + h * 0.45f), IM_COL32(40, 110, 230, 255),
                    h * 0.45f);
  dl->AddText(font, fs, ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), button);
  return w;
}

void HelpLine(ImDrawList* dl, ImVec2 center, float h, const std::vector<std::pair<const char*, const char*>>& items) {
  // como "BACK : Help" del juego: boton y texto amarillo con perfil oscuro
  ImFont* font = MenuFont();
  const float fs = h * 0.62f, gap = h * 0.8f;
  float total = 0;
  for (const auto& [b, t] : items) {
    total += Glyph(nullptr, b, ImVec2(), h) + h * 0.2f + font->CalcTextSizeA(fs, FLT_MAX, 0, t).x + gap;
  }
  float x = center.x - (total - gap) * 0.5f;
  for (const auto& [b, t] : items) {
    x += Glyph(dl, b, ImVec2(x, center.y), h) + h * 0.2f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
    OutlinedText(dl, font, fs, ImVec2(x, center.y - ts.y * 0.5f), t, IM_COL32(240, 200, 60, 255),
                 IM_COL32(220, 150, 30, 255), IM_COL32(30, 20, 10, 255), fs * 0.07f);
    x += ts.x + gap;
  }
}

void Arrows(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float h, float t) {
  const float bob = std::sin(t * 6.0f) * h * 0.08f, cy = (p0.y + p1.y) * 0.5f, w = h * 32.0f / 34.0f;
  if (const Pic p = Img("flechas")) {   // data_usi 2062: izquierda 5,8-37,42 y derecha 54,8-86,42
    DrawPic(dl, p, ImVec2(p0.x - w - bob, cy - h * 0.5f), ImVec2(p0.x - bob, cy + h * 0.5f), ImVec4(5, 8, 37, 42));
    DrawPic(dl, p, ImVec2(p1.x + bob, cy - h * 0.5f), ImVec2(p1.x + w + bob, cy + h * 0.5f), ImVec4(54, 8, 86, 42));
    return;
  }
  const ImU32 c = IM_COL32(90, 200, 255, 255);
  dl->AddTriangleFilled(ImVec2(p0.x - bob, cy - h * 0.4f), ImVec2(p0.x - bob, cy + h * 0.4f), ImVec2(p0.x - w - bob, cy), c);
  dl->AddTriangleFilled(ImVec2(p1.x + bob, cy - h * 0.4f), ImVec2(p1.x + w + bob, cy), ImVec2(p1.x + bob, cy + h * 0.4f), c);
}



void PartnerCard(ImDrawList* dl, ImVec2 p0, float s, bool mirror, const char* name, ImTextureID face_tex, float hp,
                 bool down, const char* button, bool switching, float t) {
  // medidas a 900 px de alto: ficha 270x40 inclinada como las barras del juego, cara 64x40
  const float w = 270 * s, h = 40 * s, sk = 14 * s, face = 64 * s;
  auto X = [&](float dx) { return mirror ? p0.x - dx : p0.x + dx; };
  const ImVec2 quad[4] = {ImVec2(X(0), p0.y), ImVec2(X(w), p0.y), ImVec2(X(w - sk), p0.y + h),
                          ImVec2(X(-sk), p0.y + h)};
  const float flash = switching ? 0.5f + 0.5f * std::sin(t * 18.0f) : 0.0f;
  dl->AddConvexPolyFilled(quad, 4, down ? IM_COL32(45, 45, 55, 220) : IM_COL32(18, 40, 120, 225));
  ImFont* font = MenuFont();
  // cara: la del marcador del juego (gris si cayo); sin textura, la inicial
  const ImVec2 f0(std::min(X(0), X(face)), p0.y + 1 * s), f1(std::max(X(0), X(face)), p0.y + h - 1 * s);
  if (face_tex) {
    // recorte con la proporcion de la ficha (64x40 = 1.6, como la textura de 192x120)
    const ImVec2 uv0(mirror ? 0.80f : 0.05f, 0.0f), uv1(mirror ? 0.05f : 0.80f, 0.75f);
    dl->AddImage(face_tex, f0, f1, uv0, uv1, down ? IM_COL32(110, 110, 120, 255) : IM_COL32(255, 255, 255, 255));
  } else {
    const ImVec2 c((f0.x + f1.x) * 0.5f, (f0.y + f1.y) * 0.5f);
    dl->AddCircleFilled(c, h * 0.45f, down ? IM_COL32(80, 80, 90, 255) : IM_COL32(245, 140, 30, 255), 24);
    const char initial[2] = {name && *name ? name[0] : '?', 0};
    const float fs = 30 * s;
    const ImVec2 is = font->CalcTextSizeA(fs, FLT_MAX, 0, initial);
    OutlinedText(dl, font, fs, ImVec2(c.x - is.x * 0.5f, c.y - is.y * 0.5f), initial, IM_COL32(255, 255, 255, 255),
                 IM_COL32(255, 220, 140, 255), IM_COL32(60, 25, 0, 255), 1.5f * s);
  }
  dl->AddPolyline(quad, 4, switching ? IM_COL32(255, 230, 90, int(140 + 115 * flash))
                                    : IM_COL32(150, 200, 255, 230), ImDrawFlags_Closed, 2.0f * s);
  // nombre
  float ns = 19 * s;
  ImVec2 nsz = font->CalcTextSizeA(ns, FLT_MAX, 0, name);
  const float nroom = w - face - 16 * s;   // ancho libre de la ficha tras la cara
  if (nsz.x > nroom) {
    ns *= nroom / nsz.x;
    nsz = font->CalcTextSizeA(ns, FLT_MAX, 0, name);
  }
  const float nx = mirror ? X(face + 8 * s) - nsz.x : X(face + 8 * s);
  OutlinedText(dl, font, ns, ImVec2(nx, p0.y + 2 * s), name, IM_COL32(255, 255, 255, 255),
               IM_COL32(200, 225, 255, 255), IM_COL32(5, 15, 50, 255), 1.3f * s);
  // barra de vida
  const float bx0 = face + 6 * s, bx1 = w - sk - 8 * s, by0 = p0.y + h - 15 * s, by1 = p0.y + h - 6 * s;
  const ImVec2 t0(std::min(X(bx0), X(bx1)), by0), t1(std::max(X(bx0), X(bx1)), by1);
  dl->AddRectFilled(t0, t1, IM_COL32(10, 15, 30, 255));
  if (down) {
    const char* ko = "KO";
    const ImVec2 ks = font->CalcTextSizeA(16 * s, FLT_MAX, 0, ko);
    OutlinedText(dl, font, 16 * s, ImVec2((t0.x + t1.x - ks.x) * 0.5f, by0 - 5 * s), ko, IM_COL32(255, 90, 70, 255),
                 IM_COL32(200, 30, 20, 255), IM_COL32(0, 0, 0, 255), 1.2f * s);
  } else {
    const float len = (bx1 - bx0) * std::clamp(hp, 0.0f, 1.0f);
    const ImVec2 f0 = mirror ? ImVec2(t1.x - len, by0) : t0, f1 = mirror ? t1 : ImVec2(t0.x + len, by1);
    const ImU32 col = hp > 0.5f ? IM_COL32(70, 230, 60, 255) : hp > 0.25f ? IM_COL32(240, 210, 40, 255)
                                                                         : IM_COL32(240, 70, 40, 255);
    dl->AddRectFilledMultiColor(f0, f1, col, col, IM_COL32(20, 110, 20, 255), IM_COL32(20, 110, 20, 255));
  }
  dl->AddRect(t0, t1, IM_COL32(180, 210, 255, 200), 0, 0, 1.0f * s);
  if (button && !down) {   // el boton que lo hace entrar, con el dibujo del mando del juego
    const float gh = 34 * s, gw = Glyph(nullptr, button, ImVec2(), gh);
    Glyph(dl, button, ImVec2(mirror ? X(w + 4 * s) - gw : X(w + 4 * s), p0.y + h * 0.5f), gh);   // a la derecha de la ficha
  }
}

void TeamStrip(ImDrawList* dl, ImVec2 p0, float s, bool mirror, int slots, const std::vector<std::string>& names,
               const std::vector<ImTextureID>& faces, bool active, bool done, float t) {
  const float w = 84 * s, h = 84 * s, gap = 8 * s, tab = 22 * s;
  ImFont* font = MenuFont();
  const ImU32 base_top = mirror ? IM_COL32(200, 40, 60, 235) : IM_COL32(30, 90, 210, 235);
  const ImU32 base_bot = mirror ? IM_COL32(110, 10, 30, 235) : IM_COL32(10, 40, 120, 235);
  const float pulse = active ? 0.5f + 0.5f * std::sin(t * 5.0f) : 0.0f;
  for (int i = 0; i < slots; ++i) {
    const float x = mirror ? p0.x - (i + 1) * w - i * gap : p0.x + i * (w + gap);
    const ImVec2 b0(x, p0.y + tab), b1(x + w, p0.y + tab + h);
    // pestana con el numero (como la de Tenkaichi 3)
    const ImVec2 t0(x + w * 0.25f, p0.y), t1(x + w * 0.75f, p0.y + tab + 2 * s);
    dl->AddRectFilled(t0, t1, IM_COL32(150, 170, 190, 255), 5 * s, ImDrawFlags_RoundCornersTop);
    char num[4];
    std::snprintf(num, sizeof(num), "%d", i + 1);
    const ImVec2 ns = font->CalcTextSizeA(20 * s, FLT_MAX, 0, num);
    OutlinedText(dl, font, 20 * s, ImVec2((t0.x + t1.x - ns.x) * 0.5f, t0.y + 1 * s), num, IM_COL32(40, 50, 90, 255),
                 IM_COL32(20, 30, 60, 255), IM_COL32(230, 240, 255, 255), 1.2f * s);
    const bool filled = i < int(names.size());
    const bool next = active && i == int(names.size());
    dl->AddRectFilledMultiColor(b0, b1, base_top, base_top, base_bot, base_bot);
    const ImTextureID tex = i < int(faces.size()) ? faces[size_t(i)] : ImTextureID{};
    if (filled && tex) {
      const ImVec2 i0(b0.x + 2 * s, b0.y + 2 * s), i1(b1.x - 2 * s, b1.y - 20 * s);
      // 80x62 (1.29): recorte de 0.62x0.75 de 192x120 (1.32)
      dl->AddImage(tex, i0, i1, ImVec2(mirror ? 0.70f : 0.08f, 0.0f), ImVec2(mirror ? 0.08f : 0.70f, 0.75f));
    }
    if (filled) {
      const char initial[2] = {names[size_t(i)].empty() ? '?' : names[size_t(i)][0], 0};
      const ImVec2 c((b0.x + b1.x) * 0.5f, b0.y + h * 0.42f);
      if (!tex) {
        dl->AddCircleFilled(c, h * 0.3f, IM_COL32(245, 140, 30, 255), 28);
        dl->AddCircle(c, h * 0.3f, IM_COL32(255, 235, 170, 255), 28, 2 * s);
        const ImVec2 is = font->CalcTextSizeA(34 * s, FLT_MAX, 0, initial);
        OutlinedText(dl, font, 34 * s, ImVec2(c.x - is.x * 0.5f, c.y - is.y * 0.5f), initial,
                     IM_COL32(255, 255, 255, 255), IM_COL32(255, 220, 140, 255), IM_COL32(60, 25, 0, 255), 1.5f * s);
      }
      std::string nm = names[size_t(i)];
      float fs = 15 * s;
      while (nm.size() > 3 && font->CalcTextSizeA(fs, FLT_MAX, 0, nm.c_str()).x > w - 6 * s) nm.pop_back();
      const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, nm.c_str());
      OutlinedText(dl, font, fs, ImVec2((b0.x + b1.x - ts.x) * 0.5f, b1.y - ts.y - 3 * s), nm.c_str(),
                   IM_COL32(255, 255, 255, 255), IM_COL32(200, 225, 255, 255), IM_COL32(5, 15, 50, 255), 1.1f * s);
    }
    const ImU32 border = next ? IM_COL32(255, 230, 90, int(150 + 105 * pulse)) : IM_COL32(170, 190, 215, 255);
    dl->AddRect(b0, b1, border, 4 * s, 0, (next ? 4.0f : 2.5f) * s);
  }
  if (done) {   // placa de equipo listo (la "END" de Tenkaichi 3)
    const float total = slots * w + (slots - 1) * gap;
    const ImVec2 e0(mirror ? p0.x - total : p0.x, p0.y + tab + h + 8 * s);
    const ImVec2 e1(e0.x + total, e0.y + 34 * s);
    dl->AddRectFilledMultiColor(e0, e1, IM_COL32(255, 235, 90, 240), IM_COL32(255, 235, 90, 240),
                                IM_COL32(220, 160, 20, 240), IM_COL32(220, 160, 20, 240));
    dl->AddRect(e0, e1, IM_COL32(255, 255, 230, 255), 17 * s, 0, 2 * s);
    const char* txt = "OK";
    const ImVec2 ts = font->CalcTextSizeA(26 * s, FLT_MAX, 0, txt);
    OutlinedText(dl, font, 26 * s, ImVec2((e0.x + e1.x - ts.x) * 0.5f, e0.y + 3 * s), txt, IM_COL32(255, 120, 20, 255),
                 IM_COL32(200, 60, 10, 255), IM_COL32(60, 20, 0, 255), 1.5f * s);
  }
}

}  // namespace dbz3::ingame::style
