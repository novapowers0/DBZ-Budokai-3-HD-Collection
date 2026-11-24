// TACTICS-V04: piezas comunes de las maquetas v2 de Dragon Tactics (studies 5/ENTREGAS/TACTICS_VISUAL/MAQUETAS_V2.md).
// Solo se usan con el ajuste dbz3_tactics_visual_v2 encendido (apagado de serie). Se incluye en entrada.cpp tras dibujo.inc.
constexpr float kV2Titulo = 30, kV2Fila = 24, kV2Num = 34, kV2Pie = 20;   // los 4 tamanos de letra fijos de la maqueta
bool V2() { return rex::cvar::GetFlagByName("dbz3_tactics_visual_v2") == "true"; }
// Panel: marco nativo de las listas + titulo dorado arriba con raya naranja debajo. Devuelve la y (lienzo) donde empieza el contenido.
// R3 (medidas_v05/03): marco fino de la maqueta: raya naranja (215,140,30) de 2,5 con perfil marron oscuro, metida 6 del borde,
// y un cuadradito hueco de 16 en cada esquina (centrado en la esquina de la raya). Sustituye a S::Frame (pesado, con barras que sobresalen).
void MarcoV2(ImDrawList* dl, const Lienzo& q, float x0, float y0, float x1, float y1) {
  const ImU32 osc = IM_COL32(62, 26, 8, 255), nar = IM_COL32(215, 140, 30, 255);
  const float i = 6;
  dl->AddRect(q.At(x0 + i, y0 + i), q.At(x1 - i, y1 - i), osc, 3 * q.s, 0, 5.0f * q.s);
  dl->AddRect(q.At(x0 + i, y0 + i), q.At(x1 - i, y1 - i), nar, 3 * q.s, 0, 2.5f * q.s);
  const float cx[2] = {x0 + i, x1 - i}, cy[2] = {y0 + i, y1 - i};
  for (float ex : cx)
    for (float ey : cy) {
      dl->AddRect(q.At(ex - 8, ey - 8), q.At(ex + 8, ey + 8), osc, 3 * q.s, 0, 5.0f * q.s);
      dl->AddRect(q.At(ex - 8, ey - 8), q.At(ex + 8, ey + 8), nar, 3 * q.s, 0, 2.5f * q.s);
    }
}
// R3: icono de zenis de la maqueta (maletin gris). ponytail: provisional dibujado, hasta "tac_zenis.png" de ENCARGO_GPT.md (pieza 4).
// (cx, cy) centro y ancho w del lienzo; alto = 0,62 w.
void ZenisV2(ImDrawList* dl, const Lienzo& q, float cx, float cy, float w) {
  const float h = w * 0.62f, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
  if (const S::Pic z = S::Img("tac_zenis")) { S::DrawPic(dl, z, q.At(x0, y0), q.At(x0 + w, y0 + h), ImVec4(0, 0, 0, 0), IM_COL32_WHITE); return; }   // arte de GPT 112x70
  dl->AddRectFilled(q.At(x0, y0 + h * 0.25f), q.At(x0 + w, y0 + h), IM_COL32(70, 72, 78, 255), 3 * q.s);
  dl->AddRectFilled(q.At(x0 + w * 0.06f, y0), q.At(x0 + w * 0.94f, y0 + h * 0.72f), IM_COL32(168, 170, 176, 255), 3 * q.s);
  dl->AddRectFilled(q.At(x0 + w * 0.06f, y0), q.At(x0 + w * 0.94f, y0 + h * 0.2f), IM_COL32(215, 217, 222, 255), 3 * q.s);
  dl->AddRect(q.At(x0 + w * 0.06f, y0), q.At(x0 + w * 0.94f, y0 + h * 0.72f), IM_COL32(40, 40, 46, 255), 3 * q.s, 0, 1.5f * q.s);
  dl->AddRectFilled(q.At(cx - w * 0.12f, y0 + h * 0.3f), q.At(cx + w * 0.12f, y0 + h * 0.5f), IM_COL32(90, 92, 100, 255), 2 * q.s);
}
float PanelV2(ImDrawList* dl, const Lienzo& q, float x0, float y0, float x1, float y1, const char* titulo, int alfa = 215) {
  dl->AddRectFilled(q.At(x0 + 6, y0 + 6), q.At(x1 - 6, y1 - 6), IM_COL32(18, 44, 58, alfa), 3 * q.s);   // R3: el azul verdoso de la maqueta (20,47,59)
  MarcoV2(dl, q, x0, y0, x1, y1);
  if (!titulo || !*titulo) return y0 + 12;
  Txt(dl, q, x0 + 22, y0 + 26, kV2Titulo, titulo, 0, true, false, x1 - x0 - 44);
  dl->AddRectFilled(q.At(x0 + 16, y0 + 46), q.At(x1 - 16, y0 + 49), IM_COL32(255, 150, 30, 255));
  return y0 + 58;
}
// Placa azul de la ficha de Dragon Universe (pieza nativa tac_placa_du; sin ella, caja azul): etiqueta a la izquierda y valor a la derecha.
void PlacaV2(ImDrawList* dl, const Lienzo& q, float x0, float y0, float x1, float y1, const std::string& etiqueta, const std::string& valor, bool sel = false) {
  if (const S::Pic p = S::Img("tac_placa_du")) S::DrawPic(dl, p, q.At(x0, y0), q.At(x1, y1), ImVec4(1, 7, 442, 67));   // el dibujo ocupa 1,7-442,67 de la textura de 512x128
  else Caja(dl, q, x0, y0, x1, y1, IM_COL32(120, 170, 230, 255), IM_COL32(24, 60, 120, 225), 1.5f);
  if (sel) dl->AddRect(q.At(x0, y0), q.At(x1, y1), IM_COL32(255, 220, 60, 255), 6 * q.s, 0, 3 * q.s);
  const float cy = (y0 + y1) * 0.5f, w = x1 - x0 - 24;
  if (valor.empty()) return Txt(dl, q, x0 + 12, cy, kV2Fila, etiqueta, 0, sel, false, w);
  Txt(dl, q, x0 + 12, cy, kV2Fila, etiqueta, 0, false, false, w * 0.6f);
  Txt(dl, q, x1 - 12, cy, kV2Fila, valor, 2, true, false, w * 0.4f);
}
void PestanaV2(ImDrawList* dl, const Lienzo& q, float x, float y, const std::string& nombre);
// Bocadillo claro de ancho fijo y ALTO ajustado al texto (nunca se sale): (x0, y1) = esquina inferior izquierda; `cab` = alto libre arriba
// para una cabecera; `nombre` = pestana de quien habla ("" = sin ella). Devuelve la y (lienzo) de su borde superior.
float GloboV2(ImDrawList* dl, const Lienzo& q, float x0, float y1, float ancho, const std::string& texto, const std::string& nombre, float cab = 0) {
  const auto& fu = dbz3::launcher::ui::GetFonts();
  ImFont* f = std::strcmp(T::LangCode(), "ja") == 0 && fu.menu ? fu.menu : fu.speech ? fu.speech : ImGui::GetFont();
  const float fs = 30 * q.s, wrap = (ancho - 48) * q.s;   // V05: letra del bocadillo de las maquetas 04/09 (24 se leia pequena)
  const float y0 = y1 - (f->CalcTextSizeA(fs, 1e9f, wrap, texto.c_str()).y / q.s + cab + 40);
  dl->AddRectFilled(q.At(x0, y0), q.At(x0 + ancho, y1), IM_COL32(250, 248, 236, 245), 16 * q.s);
  dl->AddRect(q.At(x0, y0), q.At(x0 + ancho, y1), IM_COL32(40, 44, 60, 255), 16 * q.s, 0, 2.5f * q.s);
  if (!nombre.empty()) PestanaV2(dl, q, x0, y0, nombre);
  dl->AddText(f, fs, q.At(x0 + 24, y0 + 16 + cab), IM_COL32(28, 28, 32, 255), texto.c_str(), nullptr, wrap);
  return y0;
}
// TACTICS-V05: texto de varias lineas con la letra del juego (la de Txt) y borde suave; sustituye a TextoSuave (letra de maquina de ImGui) en v2.
void TextoV2(ImDrawList* dl, float fs, ImVec2 p, ImU32 col, const char* t, float wrap = 0) {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float g = std::max(1.0f, fs * 0.07f);
  static const float d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& o : d) dl->AddText(f, fs, ImVec2(p.x + o[0] * g, p.y + o[1] * g), IM_COL32(0, 0, 0, 190), t, nullptr, wrap);
  dl->AddText(f, fs, p, col, t, nullptr, wrap);
}
// Una linea de color libre (rojo, verde...) en (x, y) del lienzo; al: 0 izquierda, 1 centro, 2 derecha; y = centro vertical.
void TxtColV2(ImDrawList* dl, const Lienzo& q, float x, float y, float fs, const std::string& t, ImU32 col, int al) {
  ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float f = fs * q.s;
  const ImVec2 sz = fm->CalcTextSizeA(f, 1e9f, 0, t.c_str()), p = q.At(x, y);
  TextoV2(dl, f, ImVec2(p.x - (al == 1 ? sz.x * 0.5f : al == 2 ? sz.x : 0.0f), p.y - sz.y * 0.5f), col, t.c_str());
}
// Cara en circulo (la de caras/<b3>, recortada al centro) con aro de color `aro`; sin la pieza, un circulo oscuro.
void CaraRedondaV2(ImDrawList* dl, const S::Pic& pic, ImVec2 c, float r, ImU32 aro, ImU32 tinte = IM_COL32_WHITE) {
  dl->AddCircleFilled(c, r + 2.5f * r / 20, aro, 32);
  dl->AddCircleFilled(c, r, IM_COL32(30, 52, 96, 255), 32);
  if (pic) {
    const float ar = pic.w / std::max(1.0f, pic.h), u0 = ar > 1 ? 0.5f - 0.5f / ar : 0, u1 = ar > 1 ? 0.5f + 0.5f / ar : 1;
    // R3: los retratos de B3 tienen la cara abajo a la izquierda sobre fondo azul; las maquetas llenan el circulo con la cara:
    // ventana del 72 % centrada en (0,42, 0,56) del retrato. ponytail: encuadre fijo, por personaje si alguno queda cortado.
    const float k = 0.72f, cu = u0 + (u1 - u0) * 0.42f, cv = 0.56f, hu = (u1 - u0) * k * 0.5f, hv = k * 0.5f;
    dl->AddImageRounded(pic.tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(cu - hu, cv - hv), ImVec2(cu + hu, cv + hv), tinte, r);
  }
}
// Logo: la cinta del titulo del juego (pergamino del Duelo + letra dorada, S::BannerBeta) a 0,6 en la esquina de arriba a la izquierda.
// ponytail: provisional hasta la pieza "logo Dragon Tactics" de ENCARGOS_ASSETS/tactics/ENCARGO_GPT.md
void LogoV2(ImDrawList* dl, const Lienzo& q, float escala = 0.6f) {
  S::Area a;
  a.p0 = q.At(-4, -10), a.p1 = q.At(380, 110), a.s = q.s * escala;
  S::BannerBeta(dl, a, "Dragon Tactics");
}
// Cifra o rotulo con la letra dorada del titulo (S::TitleText), centrado en (cx, cy), de `alto` y como mucho `ancho` (lienzo).
void OroV2(ImDrawList* dl, const Lienzo& q, float cx, float cy, float alto, float ancho, const std::string& t) {
  S::Area a;
  a.p0 = q.p0, a.p1 = q.At(1600, 900), a.s = q.s;
  const float k = alto / 72.0f;
  S::TitleText(dl, a, cx, cy, ancho / k, t.c_str(), k);
}
// Rotulo grande con la letra del titulo (banner) en un color propio y perfil oscuro ("Derrota" rojo), centrado en (cx, cy) del lienzo.
void RotuloV2(ImDrawList* dl, const Lienzo& q, float cx, float cy, float alto, float ancho, const char* t, ImU32 col) {
  const auto& fu = dbz3::launcher::ui::GetFonts();
  ImFont* f = fu.banner ? fu.banner : ImGui::GetFont();
  float fs = alto * q.s;
  const float w = f->CalcTextSizeA(fs, 1e9f, 0, t).x;
  if (w > ancho * q.s) fs *= ancho * q.s / w;
  const ImVec2 sz = f->CalcTextSizeA(fs, 1e9f, 0, t), p(q.At(cx, cy).x - sz.x * 0.5f, q.At(cx, cy).y - sz.y * 0.5f);
  const float g = std::max(1.5f, fs * 0.06f);
  for (int i = 0; i < 12; ++i) {
    const float a = 6.2831853f * float(i) / 12.0f;
    dl->AddText(f, fs, ImVec2(p.x + std::cos(a) * g, p.y + std::sin(a) * g), IM_COL32(30, 8, 4, 255), t);
  }
  dl->AddText(f, fs, p, col, t);
}
// Ayuda con los botones del juego (S::Glyph) en una pastilla oscura alineada a la derecha en x1, centrada en y (lienzo): "Y Velocidad x1   B Salir".
void AyudaGlifosV2(ImDrawList* dl, const Lienzo& q, float x1, float y, const std::vector<std::pair<const char*, std::string>>& items) {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float h = 44 * q.s, fs = 26 * q.s, hueco = 30 * q.s;   // R3: boton de 44 (maqueta 05: 26 px a 720p)
  float total = 0;
  for (const auto& [b, t] : items) total += S::Glyph(nullptr, b, ImVec2(), h) + 8 * q.s + f->CalcTextSizeA(fs, 1e9f, 0, t.c_str()).x + hueco;
  const ImVec2 d = q.At(x1, y);
  float x = d.x - total;
  dl->AddRectFilled(ImVec2(x - 16 * q.s, d.y - 26 * q.s), ImVec2(d.x + 6 * q.s, d.y + 26 * q.s), IM_COL32(6, 10, 26, 210), 12 * q.s);
  for (const auto& [b, t] : items) {
    x += S::Glyph(dl, b, ImVec2(x, d.y), h) + 8 * q.s;
    const float tw = f->CalcTextSizeA(fs, 1e9f, 0, t.c_str()).x;
    S::MenuTextB3(dl, ImVec2(x + tw * 0.5f, d.y), fs, t.c_str(), false, false, 0);
    x += tw + hueco;
  }
}
// Fila de menu en turquesa con perfil (S::MenuText, la letra de las filas del selector del juego), alineada a la izquierda en x.
void FilaMenuV2(ImDrawList* dl, const Lienzo& q, float x, float y, float size, const std::string& t, bool sel, float maxw) {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float w = std::min(f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x, maxw * q.s);
  (void)sel;   // V05: la fila elegida la marca el resalte; la letra, blanca en todas (maquetas 01/01b)
  S::MenuTextB3(dl, ImVec2(q.At(x, y).x + w * 0.5f, q.At(x, y).y), size * q.s, t.c_str(), false, false, maxw * q.s);
}
// La ayuda, en una sola barra abajo a la derecha.
void AyudaV2(ImDrawList* dl, const Lienzo& q, const std::string& texto) {
  if (texto.empty()) return;
  dl->AddRectFilled(q.At(900, 866), q.At(1592, 896), IM_COL32(6, 10, 26, 190), 6 * q.s);
  Txt(dl, q, 1580, 881, kV2Pie, texto, 2, false, false, 668);
}
// Pestana con el nombre de quien habla; (x, y) = esquina superior izquierda del bocadillo (la pestana queda encima).
void PestanaV2(ImDrawList* dl, const Lienzo& q, float x, float y, const std::string& nombre) {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float w = std::min(520.0f, f->CalcTextSizeA(24 * q.s, 1e9f, 0, nombre.c_str()).x / q.s + 36);
  Caja(dl, q, x + 18, y - 34, x + 18 + w, y + 2, IM_COL32(255, 150, 30, 255), IM_COL32(16, 30, 70, 240), 2.0f);
  Txt(dl, q, x + 18 + w * 0.5f, y - 16, 24, nombre, 1, true, false, w - 20);
}
