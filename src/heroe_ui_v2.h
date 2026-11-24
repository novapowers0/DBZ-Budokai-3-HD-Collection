// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04, maquetas de studies 5/ENTREGAS/HEROE_VISUAL): kit de dibujo compartido.
// Solo se usa con el ajuste `dbz3_heroe_ui_nueva` (APAGADO de serie, heroe_ui.cpp). Sin el, nada de esto se ejecuta.
//
// Como se usa (para escribir otra pantalla en su heroe_v2_<pantalla>.inl):
//   const S::Area q = v2::Lienzo(a);   // lienzo 1600x900 sobre la imagen 16:9 (= maqueta 1280x720 x 1.25)
//   Todas las medidas del kit van en px de ese lienzo. Como `q` es un S::Area, los ayudantes que ya existen
//   (S::Frame via a.At, S::BannerBeta, S::mode::Stripe, Marco/Texto/Barra/Cara de heroe_ui.cpp) dibujan sobre el
//   lienzo sin cambios.
//   Orden tipico: Fondo -> Titulo -> Peana + figura -> Panel -> Filas -> Explicacion -> BarraAyuda.
//
// Arte: SOLO piezas de B3 que el juego extrae en ejecucion (ingame/native_ui.cpp, S::Img) o primitivas. Si una pieza
// no esta en la tabla nativa, S::Img devuelve vacio y el kit dibuja un sustituto sobrio (nunca un PNG nuevo).
// Las piezas que el kit pide y aun NO estan en la tabla: ver kPiezaPeana/kPiezaOrbe/kFondo* (y ESTADO_V03_V04.md).
#pragma once

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>

#include "ingame/game_style.h"
#include "launcher/ui_kit.h"

namespace dbz3::heroe_ui::v2 {
namespace S = dbz3::ingame::style;

// Nombres de piezas nativas (ingame/native_ui.cpp kPieces). Las marcadas FALTA aun no estan en la tabla: el kit cae al
// sustituto. Cuando se anadan alli con este nombre, salen solas (lineas listas en APLICAR.md).
constexpr const char* kFondoCreador = "heroe_kami";        // FALTA: data_usi 2039_13 (Palacio de Kami)
constexpr const char* kFondoFicha = "heroe_kamehouse";     // FALTA: 2062_10 (Kame House)
constexpr const char* kFondoMaestros = "fondo_duelo";      // ya esta: 2055_09 (prado del Duelo)
constexpr const char* kFondoShenron = "heroe_noche";       // FALTA: 2045_11 (cielo de noche)
constexpr const char* kPiezaPeana = "heroe_peana";         // FALTA: 2022_06 (peana cian del selector, recorte 6,3-304,69)
constexpr const char* kPiezaOrbe = "heroe_orbe";           // FALTA: 2043_45 (orbe gris, recorte 1,1-226,226; se tine)
constexpr const char* kPiezaBarra = "heroe_barra_torneo";  // 2047_24 (barra azul del Torneo; R23 HER-10: ya en la tabla)
// HEROE-V04 (mapa y retoques): estas y las de arriba (salvo kPiezaBarra, que aun no se pinta) tienen fila con el hash
// COMPROBADO en la copia E:\DBZ3HD_trabajo\heroe_visual\src\ingame\native_ui.cpp (verificar_piezas.py). Mientras esa
// copia no pase a src/, siguen cayendo al sustituto.
constexpr const char* kPiezaPlacaDu = "heroe_placa_du";           // 2020_09 placa de fila de DU (1,7-442,67)
constexpr const char* kPiezaBolitas = "heroe_bolitas";            // 2049_16 bolitas 1-8 (n-1: 4+60n,3-50+60n,48)
constexpr const char* kPiezaRadarAro = "heroe_radar_aro";         // 2062_09 aro del radar (13,4-655,686)
constexpr const char* kPiezaRadarRejilla = "heroe_radar_rejilla"; // 2062_11 rejilla verde del radar (7,3-600,600)
constexpr const char* kPiezaEstrella = "bola_estrella";           // 2045_23 bola de 1 estrella (ya en la tabla)

// El ajuste `dbz3_heroe_ui_nueva` se define en heroe_ui.cpp dentro de su espacio anonimo (REXCVAR_DECLARE no llega desde
// otro .cpp): las pantallas de rpg.cpp lo leen con esto, como S::Borders lee el suyo.
inline bool Activa() { return rex::cvar::GetFlagByName("dbz3_heroe_ui_nueva") == "true"; }

// Tamanos de letra (px del lienzo): tres, no mas (pauta: maximo tres tamanos por pantalla).
constexpr float kLetraTitulo = 38, kLetraFila = 32, kLetraPeque = 26;
constexpr float kLetraMin = 22;   // como Tactics (kMinTxt 18 en su lienzo): lo que no cabe se acorta con ".."

// Huecos fijos de la disposicion "personaje a la izquierda, un panel a la derecha" (maqueta 01, px del lienzo).
constexpr float kFigCx = 312, kFigPies = 800;               // centro y pies del personaje
constexpr float kPanX0 = 625, kPanX1 = 1575, kPanY0 = 88;   // panel de la derecha (marco naranja)
constexpr float kAyudaY = 864;                              // centro de la barra de ayuda

// ---------------------------------------------------------------------------------------------------------------------
// Lienzo: 1600x900 sobre la imagen del juego (16:9). Con "Bordes dorados" (las tiras tapan los lados) o en una imagen
// 4:3 (480p), cabe entero dentro del area 4:3 (mas pequeno). Es un S::Area: q.At(x, y) y q.s como siempre.
// ---------------------------------------------------------------------------------------------------------------------
inline S::Area Lienzo(const S::Area& a) {
  ImVec2 p0 = a.p0, p1 = a.p1;
  if (!S::Borders()) {
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    p0 = g.p0, p1 = g.p1;
  }
  const float w = p1.x - p0.x, h = p1.y - p0.y, s = std::min(w / 1600.0f, h / 900.0f);
  S::Area q;
  q.s = s;
  q.p0 = ImVec2(p0.x + (w - 1600.0f * s) * 0.5f, p0.y + (h - 900.0f * s) * 0.5f);
  q.p1 = ImVec2(q.p0.x + 1600.0f * s, q.p0.y + 900.0f * s);
  return q;
}

inline ImFont* Letra() {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu;
  return f ? f : ImGui::GetFont();
}
inline float Ancho(float tam_px, const std::string& s) { return Letra()->CalcTextSizeA(tam_px, FLT_MAX, 0, s.c_str()).x; }

// Texto con la letra de los menus de B3. al: 0 izquierda, 1 centro, 2 derecha de x. maxw > 0: se encoge hasta
// kLetraMin y, si aun no cabe, se acorta con "..". estilo: 0 = blanco (valores, Edit Skills), 1 = turquesa (etiquetas
// del selector; `sel` la aviva), 2 = dorado (cabeceras). dis = apagado (gris).
inline void Texto(ImDrawList* dl, const S::Area& q, float x, float y, float tam, const std::string& s0, int al = 0,
                  int estilo = 0, bool sel = false, bool dis = false, float maxw = 0) {
  std::string s = s0;
  float px = tam * q.s, w = Ancho(px, s);
  if (maxw > 0 && w > maxw * q.s) {
    px = std::max(kLetraMin * q.s, px * maxw * q.s / w);
    while (s.size() > 2 && Ancho(px, s + "..") > maxw * q.s) {
      size_t n = s.size() - 1;
      while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;   // no partir un caracter UTF-8
      s.resize(n);
    }
    if (s != s0) s += "..";
    w = Ancho(px, s);
  }
  const ImVec2 p = q.At(x, y);
  const ImVec2 c(p.x + (al == 0 ? w * 0.5f : al == 1 ? 0.0f : -w * 0.5f), p.y);
  if (estilo == 1) S::MenuText(dl, c, px, s.c_str(), sel, dis);
  else S::MenuTextB3(dl, c, px, s.c_str(), estilo == 2, dis);
}

// ---------------------------------------------------------------------------------------------------------------------
// Fondo de la pantalla: la pieza `pieza` del juego a toda la imagen, con un velo suave para que el panel se lea, y las
// tiras doradas si el jugador las tiene. Devuelve false si la pieza no esta: entonces el que llama pone SU fondo de
// hoy (p. ej. `if (!(nueva && v2::Fondo(dl, a, v2::kFondoCreador))) FondoSinAnfitrion(...)`).
// ---------------------------------------------------------------------------------------------------------------------
inline bool Fondo(ImDrawList* dl, const S::Area& a, const char* pieza) {
  if (!pieza || !S::Img(pieza)) return false;
  S::Backdrop(dl, a, pieza);
  const S::Imagen g = S::GuestImage(ImGui::GetIO());
  dl->AddRectFilled(g.p0, g.p1, IM_COL32(0, 8, 28, 40));
  S::Sides(dl, a);
  return true;
}

// Cinta-pergamino del titulo arriba a la izquierda, con BETA (regla del modo: el Heroe siempre lleva BETA).
inline void Titulo(ImDrawList* dl, const S::Area& q, const char* texto) { S::BannerBeta(dl, q, texto); }

// Caja oscura redondeada (pastillas, placa del nombre, explicacion, barra de ayuda): primitiva, sin arte.
inline void Caja(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1, int alfa = 190) {
  dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), IM_COL32(8, 18, 34, alfa), 14 * q.s);
}

// Panel de Edit Skills: interior oscuro (pieza "panel") y marco naranja de esquinas (pieza "marco"), via S::Frame.
inline void Panel(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1) {
  S::Frame(dl, q.At(x0, y0), q.At(x1, y1), q.s);
}
// Franja de agua bajo la fila elegida (pieza "agua"), dentro del panel x0..x1 sin pisar su linea.
inline void Franja(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1, float t) {
  S::Highlight(dl, q.At(x0, y0), q.At(x1, y1), t);
}

// Pastilla pequena con texto gris (p. ej. "Ficha 1/1" arriba a la derecha), alineada a la derecha en x1.
inline void Pastilla(ImDrawList* dl, const S::Area& q, float x1, float y, const std::string& s) {
  const float w = std::min(520.0f, Ancho(kLetraPeque * q.s, s) / q.s + 36);
  Caja(dl, q, x1 - w, y, x1, y + 48);
  Texto(dl, q, x1 - w * 0.5f, y + 24, kLetraPeque, s, 1, 0, false, true, w - 30);
}

// Orbe de color (piezas de color de la lista y cabecera del editor). Con la pieza: el orbe gris del juego tenido.
// Sin ella: circulo con perfil y brillo.
inline void Orbe(ImDrawList* dl, const S::Area& q, float cx, float cy, float r, ImU32 col) {
  if (const S::Pic o = S::Img(kPiezaOrbe)) {
    // el gris del juego multiplicado por el color, algo aclarado para que no salga oscuro
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    c = ImVec4(std::min(1.0f, c.x * 1.35f + 0.1f), std::min(1.0f, c.y * 1.35f + 0.1f), std::min(1.0f, c.z * 1.35f + 0.1f), c.w);
    S::DrawPic(dl, o, q.At(cx - r, cy - r), q.At(cx + r, cy + r), ImVec4(1, 1, 226, 226), ImGui::ColorConvertFloat4ToU32(c));
    return;
  }
  const ImVec2 c = q.At(cx, cy);
  dl->AddCircleFilled(c, (r + 2) * q.s, IM_COL32(10, 8, 6, 255), 32);
  dl->AddCircleFilled(c, r * q.s, col, 32);
  dl->AddCircleFilled(ImVec2(c.x - r * 0.35f * q.s, c.y - r * 0.4f * q.s), r * 0.32f * q.s, IM_COL32(255, 255, 255, 120), 20);
}

// Peana cian de la pantalla de seleccion bajo los pies (cx, pies), `ancho` px. Sin la pieza: elipse cian con aro.
inline void Peana(ImDrawList* dl, const S::Area& q, float cx, float pies, float ancho = 375) {
  if (const S::Pic p = S::Img(kPiezaPeana)) {
    const float h = ancho * 66.0f / 298.0f;   // recorte 6,3-304,69
    S::DrawPic(dl, p, q.At(cx - ancho * 0.5f, pies - h * 0.5f), q.At(cx + ancho * 0.5f, pies + h * 0.5f), ImVec4(6, 3, 304, 69));
    return;
  }
  const ImVec2 c = q.At(cx, pies), r(ancho * 0.5f * q.s, ancho * 0.11f * q.s);
  dl->AddEllipseFilled(c, r, IM_COL32(150, 245, 240, 170));
  dl->AddEllipseFilled(c, ImVec2(r.x * 0.8f, r.y * 0.7f), IM_COL32(200, 255, 250, 120));
  dl->AddEllipse(c, r, IM_COL32(60, 190, 200, 220), 0, 0, 2.5f * q.s);
}

// Placa del nombre bajo el personaje (acaba en y1): nombre en dorado a la izquierda, `derecha` en gris a la derecha y
// una linea pequena opcional debajo (`nota`, p. ej. "En combate: al confirmar").
inline void Placa(ImDrawList* dl, const S::Area& q, float x0, float x1, float y1, const std::string& nombre,
                  const std::string& derecha, const std::string& nota = {}) {
  const float y0 = y1 - (nota.empty() ? 72 : 92), w = x1 - x0, yl = nota.empty() ? y0 + 36 : y0 + 30;
  Caja(dl, q, x0, y0, x1, y1);
  Texto(dl, q, x0 + 22, yl, kLetraFila, nombre, 0, 2, false, false, w * 0.38f);
  Texto(dl, q, x1 - 22, yl, kLetraPeque, derecha, 2, 0, false, true, w * 0.62f - 44);
  if (!nota.empty()) Texto(dl, q, x0 + 22, y0 + 66, kLetraMin, nota, 0, 0, false, true, w - 44);
}

// Fila etiqueta | valor como Edit Skills, dentro del panel x0..x1 con centro vertical y y alto `alto`:
// franja de agua si `sel`, etiqueta turquesa a la izquierda, orbe opcional (orbe != 0) en la columna xv y valor blanco
// en una columna fija detras; con `flechas` y `sel`, las flechas del juego a los lados del valor.
// HEROE-V05: etiqueta fina de las maquetas 01/02 (creador y editor de color): turquesa apagado con un perfil oscuro fino,
// sin el aro claro de S::MenuText; `sel` la aviva (cian claro). Alineada a la izquierda en x; maxw > 0 la encoge.
inline void Etiqueta(ImDrawList* dl, const S::Area& q, float x, float y, float tam, const std::string& s, bool sel, float maxw = 0) {
  float px = tam * q.s;
  const float w = Ancho(px, s);
  if (maxw > 0 && w > maxw * q.s) px = std::max(kLetraMin * q.s, px * maxw * q.s / w);
  const ImVec2 sz = Letra()->CalcTextSizeA(px, FLT_MAX, 0, s.c_str());
  const ImVec2 p = q.At(x, y);
  const ImVec2 o(p.x, p.y - sz.y * 0.5f);
  const float d = std::max(1.0f, 1.6f * q.s);
  const ImU32 perfil = sel ? IM_COL32(4, 30, 40, 235) : IM_COL32(4, 16, 22, 200);
  static const float dx[8] = {-1, 1, 0, 0, -0.7f, 0.7f, -0.7f, 0.7f}, dy[8] = {0, 0, -1, 1, -0.7f, -0.7f, 0.7f, 0.7f};
  for (int k = 0; k < 8; ++k) dl->AddText(Letra(), px, ImVec2(o.x + dx[k] * d, o.y + dy[k] * d), perfil, s.c_str());
  dl->AddText(Letra(), px, o, sel ? IM_COL32(120, 228, 242, 255) : IM_COL32(46, 112, 120, 255), s.c_str());
}

inline void Fila(ImDrawList* dl, const S::Area& q, float t, float x0, float x1, float y, float alto, const std::string& etiqueta,
                 const std::string& valor, bool sel, float xv, ImU32 orbe = 0, bool flechas = false, bool fina = false,
                 float xval = 0) {
  // fina (HEROE-V05): etiqueta con Etiqueta (maqueta 01) en vez del turquesa de S::MenuText; xval > 0: x fija del valor
  if (sel) Franja(dl, q, x0, y - alto * 0.5f + 2, x1, y + alto * 0.5f - 2, t);
  if (fina) Etiqueta(dl, q, x0 + 22, y, kLetraFila, etiqueta, sel, xv - x0 - 40);
  else Texto(dl, q, x0 + 22, y, kLetraFila, etiqueta, 0, 1, sel, false, xv - x0 - 40);
  if (orbe) Orbe(dl, q, xv + alto * 0.36f, y, alto * 0.36f, orbe);
  const float vx = xval > 0 ? xval : xv + alto * 0.72f + 30, vmax = x1 - vx - 60;
  Texto(dl, q, vx, y, kLetraFila - 2, valor, 0, 0, false, false, vmax);
  if (sel && flechas) {
    const float vw = std::min(vmax * q.s, Ancho((kLetraFila - 2) * q.s, valor)) / q.s;
    S::Arrows(dl, q.At(vx - 10, y - alto * 0.4f), q.At(vx + vw + 10, y + alto * 0.4f), alto * 0.55f * q.s, t);
  }
}

// Explicacion de la fila elegida bajo el panel (pauta 6): una linea. Solo si el texto no cabe (resultado de Exportar,
// avisos de error) la caja crece, hasta `max_lineas`; lo que sobre se acorta con "..". Devuelve el y de abajo.
inline float Explicacion(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, const std::string& s, int max_lineas = 3) {
  const float ancho = (x1 - x0 - 56) * q.s, px = kLetraPeque * q.s;
  std::vector<std::string> lineas(1);
  size_t i = 0;
  while (i < s.size()) {   // corte por palabras con la medida real de la letra
    if (s[i] == '\n') {
      lineas.emplace_back();
      ++i;
      continue;
    }
    const size_t fin = std::min(s.find_first_of(" \n", i + 1), s.size());
    const std::string palabra = s.substr(i, fin - i);
    if (!lineas.back().empty() && Ancho(px, lineas.back() + palabra) > ancho) {
      lineas.emplace_back();
      lineas.back() = palabra.substr(palabra[0] == ' ' ? 1 : 0);
    } else {
      lineas.back() += palabra;
    }
    i = fin;
  }
  if (int(lineas.size()) > max_lineas) {
    lineas.resize(size_t(max_lineas));
    lineas.back() += "...";
  }
  const float paso = 34, y1 = y0 + 26 + paso * float(lineas.size());
  Caja(dl, q, x0, y0, x1, y1);
  for (size_t k = 0; k < lineas.size(); ++k)
    Texto(dl, q, x0 + 28, y0 + 13 + paso * (float(k) + 0.5f), kLetraPeque, lineas[k], 0, 0, false, false, x1 - x0 - 56);
  return y1;
}

// Barra de ayuda unica abajo a la derecha (acaba en x1, centro y): glifos del juego + 1-2 palabras, un solo tamano.
inline void BarraAyuda(ImDrawList* dl, const S::Area& q, const std::vector<std::pair<const char*, const char*>>& items,
                       float x1 = 1585, float y = kAyudaY, float alto = 40) {
  // alto (HEROE-V05): alto de los glifos; 40 = la barra de abajo, menos para una segunda linea discreta
  const float h = alto * q.s, fs = h * 0.62f, gap = h * 0.8f;   // = medidas de S::HelpLine
  float total = 0;
  for (const auto& [b, txt] : items)
    total += S::Glyph(nullptr, b, ImVec2(), h) + (*txt ? h * 0.2f + Letra()->CalcTextSizeA(fs, FLT_MAX, 0, txt).x + gap : h * 0.15f);   // = S::HelpLine
  total -= gap;
  const float w = total / q.s + 48;
  Caja(dl, q, x1 - w, y - 28, x1, y + 28);
  S::HelpLine(dl, q.At(x1 - w * 0.5f, y), h, items);
}

// Barra de progreso del Torneo: azul en curso, dorada al completar; `marca` (0..1, < 0 sin marca) = raya roja (el 60 %
// de los maestros). Con la pieza 2047_24 (FALTA) se usaria su relleno; hoy, primitivas con los mismos colores.
inline void Progreso(ImDrawList* dl, const S::Area& q, float x0, float y, float x1, float frac, float marca = -1,
                     bool completo = false, float alto = 18) {
  const ImVec2 p0 = q.At(x0, y - alto * 0.5f), p1 = q.At(x1, y + alto * 0.5f);
  const float r = alto * 0.5f * q.s;
  dl->AddRectFilled(ImVec2(p0.x - 3 * q.s, p0.y - 3 * q.s), ImVec2(p1.x + 3 * q.s, p1.y + 3 * q.s), IM_COL32(6, 10, 20, 255), r + 3 * q.s);
  const float xf = p0.x + (p1.x - p0.x) * std::clamp(frac, 0.0f, 1.0f);
  if (xf > p0.x + 1) {
    if (const S::Pic b = S::Img(kPiezaBarra)) {   // R23 HER-10: el relleno es la barra del juego, cortada al %; dorada al completar
      const float fr = std::clamp(frac, 0.0f, 1.0f);
      S::DrawPic(dl, b, p0, ImVec2(xf, p1.y), ImVec4(12, 14, 12 + 440 * fr, 70), completo ? IM_COL32(255, 200, 80, 255) : IM_COL32_WHITE);
    } else {
    const ImU32 c0 = completo ? IM_COL32(255, 220, 90, 255) : IM_COL32(110, 190, 255, 255);
    const ImU32 c1 = completo ? IM_COL32(210, 140, 20, 255) : IM_COL32(30, 90, 210, 255);
    dl->AddRectFilledMultiColor(p0, ImVec2(xf, p0.y + (p1.y - p0.y) * 0.5f), c0, c0, c1, c1);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + (p1.y - p0.y) * 0.5f), ImVec2(xf, p1.y), c1, c1, c1, c1);
    }
  }
  if (marca >= 0) {
    const float xm = p0.x + (p1.x - p0.x) * std::clamp(marca, 0.0f, 1.0f);
    dl->AddLine(ImVec2(xm, p0.y - 4 * q.s), ImVec2(xm, p1.y + 4 * q.s), IM_COL32(230, 40, 40, 255), 3 * q.s);
  }
}

// Lienzo 1600x900 sobre la imagen del juego SIEMPRE (sin mirar "Bordes dorados"): para dibujar junto a algo que pinta el
// juego (HUD del combate, menu de deseos de Shenron), que no se mueve con los bordes. (HUD, resultado y Shenron)
inline S::Area LienzoImagen() {
  const S::Imagen g = S::GuestImage(ImGui::GetIO());
  const float w = g.p1.x - g.p0.x, h = g.p1.y - g.p0.y, s = std::min(w / 1600.0f, h / 900.0f);
  S::Area q;
  q.s = s;
  q.p0 = ImVec2(g.p0.x + (w - 1600.0f * s) * 0.5f, g.p0.y + (h - 900.0f * s) * 0.5f);
  q.p1 = ImVec2(q.p0.x + 1600.0f * s, q.p0.y + 900.0f * s);
  return q;
}

// HEROE-V05-B: barra fina de la ficha (maqueta 03: Salud / Dano / Guardia): canal oscuro redondeado con borde y relleno
// turquesa con brillo arriba. Primitivas (el Torneo no tiene una barra turquesa).
inline void BarraFina(ImDrawList* dl, const S::Area& q, float x0, float y, float x1, float frac, float alto = 16) {
  const ImVec2 p0 = q.At(x0, y - alto * 0.5f), p1 = q.At(x1, y + alto * 0.5f);
  const float r = alto * 0.5f * q.s, b = 2.5f * q.s;
  dl->AddRectFilled(ImVec2(p0.x - b, p0.y - b), ImVec2(p1.x + b, p1.y + b), IM_COL32(4, 10, 20, 255), r + b);
  dl->AddRectFilled(p0, p1, IM_COL32(18, 34, 52, 255), r);
  const float xf = p0.x + (p1.x - p0.x) * std::clamp(frac, 0.0f, 1.0f);
  if (xf > p0.x + 2 * r) {
    dl->AddRectFilled(p0, ImVec2(xf, p1.y), IM_COL32(40, 160, 196, 255), r);
    dl->AddRectFilled(ImVec2(p0.x + r * 0.5f, p0.y + 1.5f * q.s), ImVec2(xf - r * 0.5f, p0.y + (p1.y - p0.y) * 0.45f),
                      IM_COL32(150, 230, 245, 110), r * 0.6f);
  }
}

}  // namespace dbz3::heroe_ui::v2
