// dbz3 - Modo Heroe, rutas (ajuste dbz3_heroe_rutas, APAGADO de serie): cruce de caminos sobre el mapa de Dragon Universe
// (maqueta E:\DBZ3HD_trabajo\heroe_rediseno\maqueta_1_cruce, 1920x1080; aqui sus medidas de 1280x720 x 1,25 = lienzo 1600x900).
// Solo DIBUJO, encima del mapa nativo y solo mientras hay cruce y se vuela (campaigns.cpp avisa cada cuadro y atiende X / Y;
// heroe.cpp da los caminos). Arte: piezas de B3 que el juego extrae en ejecucion (peana 2022_06, fotos de escenario 2022_11..22,
// placa de DU 2020_09, radar 2062_09/11, bola 2045_23, glifos del mando); el punteado amarillo y los textos son primitivas.
// La zona del avatar (810-1090 x 300-680 de 1920x1080 = 675-908 x 250-567 del lienzo) queda libre.
// Se incluye en modos.cpp (fuera de sus espacios de nombres); lo llama ModosPanel::OnDraw cada cuadro. Kit: heroe_ui_v2.h.
#pragma once

#include <cmath>

#include "escenarios.h"
#include "heroe_rutas.h"
#include "heroe_ui.h"
#include "heroe_ui_v2.h"
#include "launcher/i18n.h"
#include "modos.h"

REXCVAR_DEFINE_BOOL(dbz3_heroe_rotulo_cruce, true, "DBZ3/Mods",
                    "Hero crossroads: distinguish path letters from buttons");

namespace dbz3::campaigns {
bool CruceEnMapa();   // campaigns.cpp: mapa de DU del Heroe libre con un cruce abierto (aviso de hace < 0,25 s)
}

namespace dbz3::heroe_ui::v2 {

namespace cruce {
constexpr float k = 1.25f;   // maqueta 1280x720 -> lienzo 1600x900
// Registro de escenario (0..16) -> fila del selector (heroe_esc_<fila>), como heroe_v2_mapa.inl.
constexpr int kFilaEsc[17] = {8, 5, 3, 9, 7, 10, 1, 6, 11, 4, 2, 3, 9, 7, 6, 4, 0};
inline S::Pic Foto(int registro) {
  return registro >= 0 && registro < 17 ? S::Img(("heroe_esc_" + std::to_string(kFilaEsc[registro])).c_str()) : S::Pic{};
}
// Camino punteado amarillo de p0 a p1 (px de la maqueta), con comba `curva` (negativa: hacia arriba).
inline void Punteado(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1, float curva) {
  auto pt = [&](float t) { return q.At((x0 + (x1 - x0) * t) * k, (y0 + (y1 - y0) * t + curva * std::sin(3.14159265f * t)) * k); };
  for (int i = 0; i < 100; i += 6) dl->AddLine(pt(i / 100.0f), pt((i + 3) / 100.0f), IM_COL32(255, 235, 120, 255), 6.5f * k * q.s);
}
// Marcador: peana + foto redonda + letra de boton + nombre del camino + escenario (centro cx, cy en px de la maqueta).
inline void Marcador(ImDrawList* dl, const S::Area& q, float cx, float cy, const S::Pic& foto, const char* boton, const std::string& nombre,
                     const std::string& sub, float dia = 120, float ped = 170, float letra = 22, float letra_sub = 17) {
  Peana(dl, q, cx * k, (cy + dia * 0.5f - 2) * k, ped * k);
  const ImVec2 f0 = q.At((cx - dia * 0.5f) * k, (cy - dia * 0.5f - 10) * k), f1 = q.At((cx + dia * 0.5f) * k, (cy + dia * 0.5f - 10) * k);
  dl->AddCircleFilled(ImVec2((f0.x + f1.x) * 0.5f, (f0.y + f1.y) * 0.5f), dia * 0.47f * k * q.s, IM_COL32(10, 22, 40, 255), 48);
  if (foto) S::DrawPic(dl, foto, f0, f1);
  else dl->AddCircleFilled(ImVec2((f0.x + f1.x) * 0.5f, (f0.y + f1.y) * 0.5f), dia * 0.5f * k * q.s, IM_COL32(40, 70, 110, 235), 48);
  if (boton) S::Glyph(dl, boton, q.At((cx + dia * 0.5f - 14) * k, (cy - dia * 0.5f + 1) * k), 34 * k * q.s);
  Texto(dl, q, cx * k, (cy + dia * 0.5f + 26) * k, letra * k, nombre, 1, 2, false, false, 300);
  if (!sub.empty()) Texto(dl, q, cx * k, (cy + dia * 0.5f + 26 + letra + 4) * k, letra_sub * k, sub, 1, 0, false, false, 300);
}
// Cara del rival recortada en un circulo (maqueta 1, punto 8): el retrato de dialogo de B3 recortado a la cara (retratos/<id>,
// native_ui), el retrato del mod del villano revivido (rutas/rival_<mod>, lo deja el montaje) o la cara del marcador; sin nada,
// la foto "?" del selector.
inline void Cara(ImDrawList* dl, const S::Area& q, int id, const std::string& mod, float cx, float cy, float r) {
  const ImVec2 c = q.At(cx, cy);
  dl->AddCircleFilled(c, (r + 2) * q.s, IM_COL32(8, 18, 34, 235), 40);
  S::Pic p = !mod.empty() ? S::Img(("rutas/rival_" + mod).c_str()) : S::Pic{};
  if (!p && id >= 0) p = S::Img(("retratos/" + std::to_string(id)).c_str());
  if (!p && id >= 0) p = S::Img(("caras/" + std::to_string(id)).c_str());
  if (p) {
    const float lado = std::min(p.w, p.h), u0 = (p.w - lado) * 0.5f / p.w, v0 = (p.h - lado) * 0.5f / p.h;
    dl->AddImageRounded(p.tex, ImVec2(c.x - r * q.s, c.y - r * q.s), ImVec2(c.x + r * q.s, c.y + r * q.s), ImVec2(u0, v0), ImVec2(1 - u0, 1 - v0),
                        IM_COL32_WHITE, r * q.s);
  } else if (const S::Pic f = S::Img("heroe_esc_desconocido")) {
    S::DrawPic(dl, f, ImVec2(c.x - r * q.s, c.y - r * q.s), ImVec2(c.x + r * q.s, c.y + r * q.s));
  }
  dl->AddCircle(c, r * q.s, IM_COL32(255, 230, 60, 255), 48, 3.5f * q.s);
}
// Radar del Dragon con las 7 Esferas (las no recogidas, atenuadas) y su contador, abajo a la derecha.
inline void Radar(ImDrawList* dl, const S::Area& q, int esferas) {
  namespace T = dbz3::i18n;
  const float d = 190 * k, x1 = (1280 - 24) * k, y1 = (720 - 70) * k, cx = x1 - d * 0.5f, cy = y1 - d * 0.5f, rr = d * 0.43f;
  const S::Pic aro = S::Img(kPiezaRadarAro), rej = S::Img(kPiezaRadarRejilla), bola = S::Img(kPiezaEstrella);
  if (rej) S::DrawPic(dl, rej, q.At(cx - rr, cy - rr), q.At(cx + rr, cy + rr), ImVec4(7, 3, 600, 600));
  else dl->AddCircleFilled(q.At(cx, cy), rr * q.s, IM_COL32(20, 70, 30, 255), 48);
  for (int i = 0; i < 7; ++i) {
    const float ang = 6.2831853f * i / 7 - 1.5708f, ex = cx + std::cos(ang) * d * 0.17f, ey = cy + std::sin(ang) * d * 0.17f, er = d * 0.06f;
    const ImU32 tinte = i < esferas ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 70);
    if (bola) S::DrawPic(dl, bola, q.At(ex - er, ey - er), q.At(ex + er, ey + er), ImVec4(0, 0, 0, 0), tinte);
    else dl->AddCircleFilled(q.At(ex, ey), er * q.s, i < esferas ? IM_COL32(250, 150, 30, 255) : IM_COL32(250, 150, 30, 70), 16);
  }
  if (aro) S::DrawPic(dl, aro, q.At(cx - d * 0.5f, y1 - d * 682.0f / 642.0f), q.At(x1, y1), ImVec4(13, 4, 655, 686));
  else dl->AddCircle(q.At(cx, cy), (rr + 6) * q.s, IM_COL32(250, 200, 40, 255), 48, 8 * q.s);
  Texto(dl, q, cx, (720 - 54) * k, 20 * k, std::string(T::T("Esferas ", "Dragon Balls ")) + std::to_string(esferas) + " / 7", 1, 2);
}
}  // namespace cruce

inline void DibujarCruce(ImDrawList* dl) {
  if (!dbz3::campaigns::CruceEnMapa()) return;   // sin cruce en el mapa (o ajuste apagado: campaigns.cpp no avisa): nada
  dbz3::heroe::InfoCruce c;
  if (!dbz3::heroe::CruceRuta(c) || c.caminos.size() < 2) return;
  if (static bool dicho = false; !dicho) {
    dicho = true;
    REXLOG_INFO("dbz3 heroe: cruce v2: dibujado ({} | {})", c.caminos[0].nombre, c.caminos[1].nombre);
  }
  namespace T = dbz3::i18n;
  using cruce::k;
  const S::Area q = LienzoImagen();
  {   // R23 (usuario): el mapa, quieto y atenuado detras, para que el cruce se lea como menu (campaigns.cpp para el mando)
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    dl->AddRectFilled(g.p0, g.p1, IM_COL32(6, 12, 24, 110));
  }
  Titulo(dl, q, T::T("Cruce de caminos", "Crossroads"));
  // caminos punteados desde el avatar (640, 335 de la maqueta) a cada marcador; no pasan por la zona libre del avatar
  cruce::Punteado(dl, q, 535, 345, 410, 335, -50);
  cruce::Punteado(dl, q, 745, 345, 845, 335, -50);
  const dbz3::heroe::InfoCamino &a = c.caminos[0], &b = c.caminos[1];
  auto lugar = [](const dbz3::heroe::InfoCamino& x) { return x.lugar.empty() ? dbz3::escenarios::Nombre(x.escenario) : x.lugar; };
  const bool rotulo = REXCVAR_GET(dbz3_heroe_rotulo_cruce);
  cruce::Marcador(dl, q, 330, 345, cruce::Foto(a.escenario), "X", (rotulo ? T::T("Camino A: ", "Path A: ") : "A: ") + a.nombre, lugar(a));
  cruce::Marcador(dl, q, 925, 345, cruce::Foto(b.escenario), "Y", (rotulo ? T::T("Camino B: ", "Path B: ") : "B: ") + b.nombre, lugar(b));
  if (!c.opcional.empty()) {   // opcional sorteado: foto "?" hasta acercarse
    cruce::Punteado(dl, q, 530, 285, 245, 225, -30);
    cruce::Marcador(dl, q, 175, 205 - 6, S::Img("heroe_esc_desconocido"), nullptr, T::T("Opcional (sorteado)", "Optional (random)"), c.opcional,
                    96, 120, 18, 16);
  }
  // placa de DU abajo en el centro: pregunta del tramo y el rival que espera en cada camino
  constexpr float pw = 780, ph = 138, px = (1280 - pw) / 2 - 40, py = 720 - ph - 52;
  if (const S::Pic pl = S::Img(kPiezaPlacaDu)) S::DrawPic(dl, pl, q.At(px * k, py * k), q.At((px + pw) * k, (py + ph) * k), ImVec4(1, 7, 442, 67));
  else Caja(dl, q, px * k, py * k, (px + pw) * k, (py + ph) * k, 215);
  Texto(dl, q, (px + pw * 0.5f) * k, (py + 52) * k, 27 * k, c.pregunta, 1, 0, false, false, (pw - 330) * k);
  Texto(dl, q, (px + pw * 0.5f) * k, (py + 86) * k, 19 * k, T::T("Elige un camino: el otro se cierra", "Pick a path: the other one closes"), 1, 0, false, false,
        (pw - 330) * k);
  const struct { float cx; const dbz3::heroe::InfoCamino* cam; const char* boton; } lados[2] = {{px + 92, &a, "X"}, {px + pw - 92, &b, "Y"}};
  for (const auto& l : lados) {
    const float cy = py + 12 + 37;
    cruce::Cara(dl, q, l.cam->rival, l.cam->rival_mod, l.cx * k, cy * k, 37 * k);
    S::Glyph(dl, l.boton, q.At((l.cx + 37 - 12) * k, (py + 6 + 14) * k), 28 * k * q.s);
    Texto(dl, q, l.cx * k, (py + 12 + 74 + 14) * k, 18 * k, l.cam->rival_nombre.empty() ? dbz3::modos::Nombre(l.cam->rival) : l.cam->rival_nombre, 1, 0, false, false, 170);
  }
  cruce::Radar(dl, q, c.esferas);
  BarraAyuda(dl, q, {{"X", T::T("Camino A", "Path A")}, {"Y", T::T("Camino B", "Path B")}, {"B", T::T("Atrás", "Back")}}, 585, kAyudaY, 40);
}

}  // namespace dbz3::heroe_ui::v2
