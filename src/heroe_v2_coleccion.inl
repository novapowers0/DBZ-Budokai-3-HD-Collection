// dbz3 - Modo Heroe, Coleccion (HEROE-REC R5; maqueta 13_coleccion de E:\DBZ3HD_trabajo\heroe_rediseno\maqueta_5_coleccion;
// ajuste dbz3_heroe_recompensas, APAGADO de serie). Se abre desde Habitacion > Colección (6.ª fila). Fondo de la Habitacion
// oscurecido, 7 pestañas (LB/RB), rejilla de 2 x 6 cartas con paginas, el heroe a la derecha sin tapar, ficha de la elegida
// y la tira de hitos 25/50/75/100 %. Lo conseguido sale de heroe.cpp (H::Premios, heroe_recompensas.inl); lo que falta se ve
// como "?" con su condicion escrita. Pestañas sin contenido todavia: Trajes, Capsulas, Escenarios y musica, Luchadores
// (ver INFORME de HEROE-REC: sus consumidores estan por hacer); Formas lee la Evolucion si esta encendida.
// Se incluye en heroe_ui.cpp tras CabeceraCarta (usa FiguraV2, FondoSinAnfitrion, CaraRedonda, CabeceraCarta).
// Medidas en px del lienzo 1600x900 (= maqueta 1920x1080 x 0,8333).
namespace {

constexpr int kColPest = 7, kColCols = 2, kColFilas = 6, kColPorPagina = kColCols * kColFilas;
const char* const kColPestEs[kColPest] = {"Trajes y partes", "Técnicas", "Formas", "Cápsulas", "Títulos y logos", "Escenarios y música", "Luchadores"};
const char* const kColPestEn[kColPest] = {"Costumes", "Techniques", "Forms", "Capsules", "Titles & logos", "Stages, music", "Fighters"};
constexpr int kColCaraPest[kColPest] = {10, -1, 0, -1, -1, -1, 18};   // cabeza nativa de cada pestaña; -1 = icono propio

int g_col_pest = 1, g_col_cur = 0;

// R23 HER-05 v3: las 7 pestañas de la maqueta 13 siempre (la que aun no tiene premios dice "Aún no hay nada aquí").
std::vector<int> PestanasCol() { return {0, 1, 2, 3, 4, 5, 6}; }
int SiguientePestanaCol(int pest, int paso) {
  const std::vector<int> v = PestanasCol();
  int k = 0;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] == pest) k = int(i);
  return v[size_t((k + paso + int(v.size())) % int(v.size()))];
}
std::string g_col_visto_pend;   // la celda de la que se acaba de salir: pierde su NUEVO

// Laurel de los titulos (arte de GPT, res/heroe/premio_titulo.png); sin el fichero, la estrella nativa.
S::Pic PiezaTitulo() {
  const S::Pic p = PicPng(Juego() / "res" / "heroe" / "premio_titulo.png", dbz3::RelojT(ImGui::GetTime()));
  return p ? p : S::Img(v2::kPiezaEstrella);
}

struct CeldaCol {
  std::string id, nombre, cond;
  int tipo = 0;   // 0 tecnica, 1 titulo, 2 forma, 3 escenario, 4 luchador
  int cap = 0, cara = -1, ki = 0;
  bool ok = false, nuevo = false;
};
std::vector<CeldaCol> CeldasCol(int pest) {
  std::vector<CeldaCol> v;
  if (pest == 1 || pest == 4 || pest == 5 || pest == 6) {
    const std::string tipo = pest == 1 ? "tecnica" : pest == 4 ? "titulo" : pest == 5 ? "escenario" : "luchador";
    for (const H::InfoPremio& p : H::Premios()) {
      if (p.tipo != tipo) continue;
      CeldaCol c;
      c.id = p.id, c.nombre = p.nombre, c.cond = p.condicion, c.tipo = pest == 1 ? 0 : pest == 4 ? 1 : pest == 5 ? 3 : 4, c.cap = p.capsula, c.cara = p.cara;
      c.ok = p.conseguido, c.nuevo = p.nuevo;
      for (const H::Tecnica& t : H::CatalogoTecnicas())
        if (t.cap == p.capsula) c.ki = t.ki;
      v.push_back(std::move(c));
    }
  } else if (pest == 2 && H::EvolucionActiva()) {
    for (const H::InfoForma& f : H::FormasDeRaza(H::RazaEvolucion())) {
      CeldaCol c;
      c.id = f.id, c.nombre = f.nombre, c.cond = f.condicion, c.tipo = 2, c.cara = f.cara, c.ok = f.estado == 0;
      v.push_back(std::move(c));
    }
  }
  std::stable_partition(v.begin(), v.end(), [](const CeldaCol& c) { return c.ok; });   // R23: lo conseguido primero
  return v;
}

void ChipNuevoCol(ImDrawList* dl, const S::Area& q, float x1, float y) {   // degradado oro con perfil marron (efecto, sin textura nativa)
  const float w = 74, h = 22;   // R23 v3: pastilla redonda naranja con letra blanca perfilada (maqueta 13)
  dl->AddRectFilled(q.At(x1 - w - 2, y - h * 0.5f - 2), q.At(x1 + 2, y + h * 0.5f + 2), IM_COL32(110, 50, 5, 255), 13 * q.s);
  dl->AddRectFilled(q.At(x1 - w, y - h * 0.5f), q.At(x1, y + h * 0.5f), IM_COL32(240, 140, 25, 255), 12 * q.s);
  dl->AddRectFilled(q.At(x1 - w + 4, y - h * 0.5f + 2), q.At(x1 - 4, y), IM_COL32(255, 205, 90, 255), 9 * q.s, ImDrawFlags_RoundCornersTop);
  {
    const char* tx = T::T("NUEVO", "NEW");
    ImFont* f = v2::Letra();
    const float px = 18 * q.s;
    const ImVec2 ts = f->CalcTextSizeA(px, FLT_MAX, 0, tx), c = q.At(x1 - w * 0.5f, y);
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        if (dx || dy) dl->AddText(f, px, ImVec2(c.x - ts.x * 0.5f + dx * 1.5f * q.s, c.y - ts.y * 0.5f + dy * 1.5f * q.s), IM_COL32(90, 35, 0, 255), tx);
    dl->AddText(f, px, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, tx);
  }
}

// Una celda de la rejilla: cabecera real de la carta (tecnica) o placa con cabeza/estrella (titulo, forma); "?" y la
// condicion debajo si falta.
void CeldaColDibujar(ImDrawList* dl, const S::Area& q, double ahora, const CeldaCol& c, float x0, float y0, float w, bool sel) {
  const float h = 64;
  if (c.tipo == 0 && PicPng(CartaPng(c.cap), ahora)) {   // R23 HER-05: tambien la bloqueada, apagada (35 %) con el "?" encima (maqueta 13)
    if (!c.ok) dl->AddRectFilled(q.At(x0, y0), q.At(x0 + w, y0 + h), IM_COL32(4, 7, 12, 250), 4 * q.s);   // fondo oscuro (juez r1)
    if (!c.ok) dl->AddRect(q.At(x0 - 1, y0 - 1), q.At(x0 + w + 1, y0 + h + 1), IM_COL32(105, 32, 24, 220), 4 * q.s, 0, 1.5f * q.s);   // borde rojizo (maqueta 13)
    CabeceraCarta(dl, q, c.cap, x0, y0, w, ahora, c.ok ? 255 : 50);
  } else {
    v2::Caja(dl, q, x0, y0, x0 + w, y0 + h, c.ok ? 200 : 110);
    if (c.cara >= 0) CaraRedonda(dl, q, c.cara, x0 + 38, y0 + h * 0.5f, 24, c.ok ? 255 : 90, false);
    else if (const S::Pic e = PiezaTitulo()) S::DrawPic(dl, e, q.At(x0 + 14, y0 + 12), q.At(x0 + 62, y0 + 60), ImVec4(0, 0, 0, 0), c.ok ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 160));
    v2::Texto(dl, q, x0 + 80, y0 + h * 0.5f, v2::kLetraFila, c.ok ? c.nombre : std::string("???"), 0, 0, false, !c.ok, w - 100 - (c.ok && c.nuevo ? 84 : 0));
  }
  if (!c.ok) MonedaInterrogacion(dl, q.At(x0 + 36, y0 + h * 0.5f), 25 * q.s, 255);   // moneda "?" como Evolucion (maqueta 13)
  if (sel) dl->AddRect(q.At(x0 - 3, y0 - 3), q.At(x0 + w + 3, y0 + h + 3), IM_COL32(150, 245, 240, 255), 8 * q.s, 0, 3 * q.s);
  if (c.ok && c.nuevo) ChipNuevoCol(dl, q, x0 + w - 8, y0 + 20);
  {
    const float px = 20 * q.s, cw = std::min(w - 4, v2::Ancho(px, c.cond) / q.s + 18);
    v2::Caja(dl, q, x0, y0 + h + 2, x0 + cw, y0 + h + 24, 215);
    v2::Texto(dl, q, x0 + 9, y0 + h + 13, 19, c.cond, 0, c.ok ? 0 : 2, false, false, cw - 14);
  }
}

void HitosCol(ImDrawList* dl, const S::Area& q, int pct, float x0, float y0, float x1, float y1) {
  v2::Caja(dl, q, x0, y0, x1, y1, 170);
  static const int kHito[4] = {25, 50, 75, 100};
  const std::vector<H::InfoPremio> prem = H::Premios();
  const float bx0 = x0 + 14, bx1 = x1 - 14, by = y1 - 14, ancho = (x1 - x0 - 28) / 4;
  for (int i = 0; i < 4; ++i) {
    const bool ok = pct >= kHito[i];
    const float cx = x0 + 28 + ancho * float(i);
    const S::Pic e = S::Img(v2::kPiezaEstrella);
    if (e && ok) {   // la Esfera de 1 estrella del juego (maqueta 13: naranja pasada, gris pendiente)
      S::DrawPic(dl, e, q.At(cx - 25, y0 + 1), q.At(cx + 25, y0 + 51), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    } else if (e) {   // pendiente: orbe gris del juego con la estrella de la Esfera en sombra encima
      v2::Orbe(dl, q, cx, y0 + 26, 24, IM_COL32(130, 130, 136, 255));
      S::DrawPic(dl, e, q.At(cx - 25, y0 + 1), q.At(cx + 25, y0 + 51), ImVec4(0, 0, 0, 0), IM_COL32(70, 70, 76, 90));
    } else
      v2::Orbe(dl, q, cx, y0 + 24, 19, ok ? IM_COL32(245, 160, 30, 255) : IM_COL32(110, 110, 110, 200));
    v2::Texto(dl, q, cx + 34, y0 + 18, v2::kLetraPeque, Num(kHito[i]) + " %", 0, 0, false, !ok);
    std::string premio = T::T("Título", "Title");
    for (const H::InfoPremio& p : prem)   // R23 HER-05: el TIPO de premio del hito, como la maqueta 13 (Título / Escenario / Luchador)
      if (p.id == "r_h" + Num(kHito[i]))
        premio = p.tipo == "luchador" ? T::T("Luchador", "Fighter") : p.tipo == "escenario" ? T::T("Escenario", "Stage") : p.tipo == "tecnica" ? T::T("Técnica", "Technique") : T::T("Título", "Title");
    v2::Texto(dl, q, cx + 34, y0 + 42, v2::kLetraMin, premio, 0, 0, false, !ok, ancho - 44);
  }
  v2::Progreso(dl, q, bx0, by, bx1, float(pct) / 100.0f, -1, pct >= 100, 8);
  for (int i = 1; i < 4; ++i) {   // marcas de los hitos sobre la barra (maqueta 13)
    const float xm = bx0 + (bx1 - bx0) * 0.25f * float(i);
    dl->AddLine(q.At(xm, by - 10), q.At(xm, by + 10), IM_COL32(240, 250, 255, 255), 2.5f * q.s);
  }
}

// R23 HER-05 v3: el medidor de Edit Skills DE VERDAD (maqueta 13), piezas de la pantalla Edit Skills del juego (data_usi 1985):
// placa azul 1985_16 (20,2-384,64; como en el juego sigue hasta el borde derecho), capsula 1985_15 (cuerpo 10,7-185,52 con la
// ventana 20,25-146,34 hueca; detras, la misma capsula en crema 9,68-185,112 con la ventana negra; relleno cian 2,127-131,138)
// y el % con las cifras de 1985_08 ("1234567890%"). Sin las piezas, placa y barra dibujadas.
void TextoCianCol(ImDrawList* dl, const S::Area& q, float x, float y, float tam, const std::string& t) {   // texto cian con sombra, x = izquierda
  ImFont* f = v2::Letra();
  const ImVec2 p = q.At(x, y), ts = f->CalcTextSizeA(tam * q.s, FLT_MAX, 0, t.c_str());
  for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
      if (dx || dy) dl->AddText(f, tam * q.s, ImVec2(p.x + dx * 1.6f * q.s, p.y - ts.y * 0.5f + dy * 1.6f * q.s), IM_COL32(4, 22, 40, 255), t.c_str());
  dl->AddText(f, tam * q.s, ImVec2(p.x, p.y - ts.y * 0.5f), IM_COL32(120, 222, 250, 255), t.c_str());
}

void MedidorCapsulaCol(ImDrawList* dl, const S::Area& q, int pct) {
  const S::Pic placa = S::Img("edit_placa"), caps = S::Img("edit_capsula"), cif = S::Img("edit_cifras");
  const float fr = std::clamp(float(pct) / 100.0f, 0.0f, 1.0f);
  if (!placa || !caps) {
    v2::Caja(dl, q, 1104, 23, 1567, 96, 200);
    v2::Progreso(dl, q, 1124, 60, 1350, fr, -1, pct >= 100, 18);
  } else {
    S::DrawPic(dl, placa, q.At(1100, 22), q.At(1600 + 60, 97), ImVec4(20, 2, 384, 64));   // el extremo recto queda fuera de la pantalla
    constexpr float k = 1.12f, x0 = 1116, y0 = 34;   // capsula a 196x50 del lienzo (deja sitio al rotulo "Total")
    auto X = [&](float sx) { return x0 + (sx - 10) * k; };
    auto Y = [&](float sy) { return y0 + (sy - 7) * k; };
    S::DrawPic(dl, caps, q.At(X(9), Y(7) - 1), q.At(X(185), Y(52)), ImVec4(9, 68, 185, 113));   // fondo: ventana negra
    if (fr > 0) S::DrawPic(dl, caps, q.At(X(20), Y(24.5f)), q.At(X(20) + (X(146) - X(20)) * fr, Y(35.5f)), ImVec4(2, 127, 2 + 129 * fr, 139),
                           pct >= 100 ? IM_COL32(255, 210, 90, 255) : IM_COL32_WHITE);
    S::DrawPic(dl, caps, q.At(X(10), Y(7)), q.At(X(185), Y(52)), ImVec4(10, 7, 185, 52));
  }
  v2::Texto(dl, q, 1320, 60, 20, T::T("Total", "Total"), 0, 0, false, false, 52);   // el % es de toda la coleccion, no de la pestaña
  // el % con las cifras del juego, alineado a la derecha en x 1552
  static const float kCx[11][2] = {{284, 311}, {6, 24}, {35, 60}, {65, 92}, {97, 123}, {130, 155}, {157, 184}, {189, 215}, {220, 247}, {251, 278}, {318, 353}};
  if (!cif) {
    v2::Texto(dl, q, 1545, 60, 56, Num(pct) + " %", 2, 0);
    return;
  }
  const std::string t = Num(pct);
  std::vector<int> g;
  for (const char ch : t) g.push_back(ch - '0');
  g.push_back(10);
  constexpr float h = 46, k2 = h / 31.0f, gap = 3;
  float w = 0;
  for (const int i : g) w += (kCx[i][1] - kCx[i][0] + 1) * k2 + gap;
  w += 8;   // hueco antes del %
  float x = 1556 - w;
  for (size_t j = 0; j < g.size(); ++j) {
    const int i = g[j];
    if (i == 10) x += 8;
    const float cw = (kCx[i][1] - kCx[i][0] + 1) * k2;
    S::DrawPic(dl, cif, q.At(x, 60 - h * 0.5f), q.At(x + cw, 60 + h * 0.5f), ImVec4(kCx[i][0], 5, kCx[i][1] + 1, 36));
    x += cw + gap;
  }
}

void DibujarColeccionV2(ImDrawList* dl, const S::Area& a, float t, double ahora) {
  if (!v2::Fondo(dl, a, v2::kFondoFicha)) FondoSinAnfitrion(dl, a);
  {
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    dl->AddRectFilled(g.p0, g.p1, IM_COL32(0, 0, 0, 77));   // oscurecido 30 %
  }
  const S::Area q = v2::Lienzo(a);
  // el heroe, de cuerpo entero sobre su peana, sin tapar. R23 v3: lo PRIMERO tras el fondo; la vista 3D se salta si la lista
  // ya pasa de su presupuesto de vertices (heroe_ui.cpp Pintar3D), y con 7 pestañas y la rejilla llena pasaba
  {
    constexpr float k = 0.76f, fx = 1345, pies = 640;   // R23 HER-05: la ficha ya no tapa los pies
    v2::Peana(dl, q, fx, pies, 340);
    S::Area qf = q;
    qf.s = q.s * k;
    qf.p0 = q.At(fx - v2::kFigCx * k, pies - v2::kFigPies * k);
    qf.p1 = ImVec2(qf.p0.x + 1600.0f * qf.s, qf.p0.y + 900.0f * qf.s);
    FiguraV2(dl, qf);
  }
  v2::Titulo(dl, q, T::T("Colección", "Collection"));
  const int pct = H::ColeccionPorcentaje();
  MedidorCapsulaCol(dl, q, pct);
  // pestañas (izquierda): solo las que tienen contenido, cada una con su icono (cabeza, botella de capsula, laurel o foto de escenario)
  const std::vector<int> pests = PestanasCol();
  S::Frame(dl, q.At(42, 146), q.At(312, 742), q.s * 0.45f);   // marco fino de la maqueta 13
  for (size_t r = 0; r < pests.size(); ++r) {
    const int i = pests[r];
    const float cy = 202 + 80.5f * float(r), y0 = cy - 36;
    const bool sel = i == g_col_pest;
    if (sel) {
      v2::Franja(dl, q, 54, y0, 298, y0 + 72, t);
      dl->AddRect(q.At(54, y0), q.At(298, y0 + 72), IM_COL32(150, 245, 240, 255), 0, 0, 2 * q.s);
    }
    // R23 v3 (maqueta 13): iconos sueltos, sin burbuja ni aro: cabeza redonda del selector, botella de capsula, foto o laurel
    const int cara = kColCaraPest[i];
    if (cara >= 0) {
      CaraNodoEvo(dl, q, cara, 88, cy, 32, 255);   // cabeza del selector o cara recortada dentro del circulo (Luchadores se salia)
    } else if (i == 1 || i == 3) {
      CapsulaV2(dl, q, 88, cy, 64, true);
    } else if (i == 5) {
      if (const S::Pic f = S::Img("heroe_esc_6")) {
        const ImVec2 c = q.At(88, cy);
        dl->AddImageRounded(f.tex, ImVec2(c.x - 32 * q.s, c.y - 26 * q.s), ImVec2(c.x + 32 * q.s, c.y + 26 * q.s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 26 * q.s);
      }
    } else if (const S::Pic e = (i == 4 ? PiezaTitulo() : S::Img(v2::kPiezaEstrella))) {
      S::DrawPic(dl, e, q.At(58, cy - 30), q.At(118, cy + 30), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    }
    {   // blanco como la maqueta, sin contadores. R23 HER-05: el que no cabe ("Escenarios y música") va en dos lineas, no cortado
      const std::string nom = T::T(kColPestEs[i], kColPestEn[i]);
      const size_t sp = nom.rfind(' ');
      const float tam = v2::Ancho((v2::kLetraPeque + 2) * q.s, nom) <= 168 * q.s ? v2::kLetraPeque + 2 : 24;
      if (v2::Ancho(tam * q.s, nom) <= 168 * q.s || sp == std::string::npos) {
        TextoCianCol(dl, q, 132, cy, tam, nom);
      } else {
        TextoCianCol(dl, q, 132, cy - 14, v2::kLetraPeque, nom.substr(0, sp));
        TextoCianCol(dl, q, 132, cy + 14, v2::kLetraPeque, nom.substr(sp + 1));
      }
    }
  }
  // cabecera de la pestaña y rejilla
  const std::vector<CeldaCol> cs = CeldasCol(g_col_pest);
  const int n = int(cs.size()), paginas = std::max(1, (n + kColPorPagina - 1) / kColPorPagina), pag = n ? std::clamp(g_col_cur, 0, n - 1) / kColPorPagina : 0;
  v2::Caja(dl, q, 350, 142, 1083, 182, 200);
  TextoCianCol(dl, q, 372, 162, v2::kLetraFila, T::T(kColPestEs[g_col_pest], kColPestEn[g_col_pest]));
  int ok = 0;
  for (const CeldaCol& c : cs) ok += c.ok;
  v2::Texto(dl, q, 690, 162, v2::kLetraPeque, std::string(T::T("Conseguidas ", "Earned ")) + Num(ok) + T::T(" de ", " of ") + Num(n), 1, 0, false, false, 260);
  v2::Texto(dl, q, 1040, 162, v2::kLetraPeque, std::string(T::T("Página ", "Page ")) + Num(pag + 1) + T::T(" de ", " of ") + Num(paginas), 2, 0, false, false, 170);
  if (const S::Pic fl = S::Img("flechas")) {   // 2062_04: izquierda (4,8-36,43), derecha (53,8-85,43)
    S::DrawPic(dl, fl, q.At(848, 150), q.At(870, 174), ImVec4(4, 8, 36, 43));
    S::DrawPic(dl, fl, q.At(1050, 150), q.At(1072, 174), ImVec4(53, 8, 85, 43));
  }
  if (n == 0) {
    v2::Caja(dl, q, 350, 196, 1083, 740, 150);
    v2::Texto(dl, q, 716, 440, v2::kLetraFila, T::T("Aún no hay nada aquí en esta versión beta.", "Nothing here yet in this beta."), 1, 0, false, true, 640);
    if (g_col_pest == 2)
      v2::Texto(dl, q, 716, 490, v2::kLetraPeque, T::T("Las formas salen con Evolución encendida.", "Forms appear with Evolution turned on."), 1, 0, false, true, 640);
  }
  for (int k = 0; k < kColPorPagina; ++k) {
    const int i = pag * kColPorPagina + k;
    if (i >= n) break;
    const float x0 = 350 + 375.0f * float(k % kColCols), y0 = 198 + 93.0f * float(k / kColCols);
    CeldaColDibujar(dl, q, ahora, cs[size_t(i)], x0, y0, 358, i == g_col_cur);
  }
  // hitos y pie
  HitosCol(dl, q, pct, 350, 760, 1083, 836);
  // ficha de la elegida
  S::Frame(dl, q.At(1125, 677), q.At(1558, 807), q.s * 0.45f);   // marco fino (juez r1)
  if (n > 0) {
    const CeldaCol& c = cs[size_t(std::clamp(g_col_cur, 0, n - 1))];
    v2::Texto(dl, q, 1148, 702, 38, c.ok ? c.nombre : std::string("???"), 0, 2, false, false, c.nuevo ? 270 : 400);
    if (c.ok && c.nuevo) ChipNuevoCol(dl, q, 1536, 700);
    std::string sub;
    if (c.tipo == 0)
      for (const H::Tecnica& tc : H::CatalogoTecnicas())
        if (tc.cap == c.cap) sub = std::string(T::T("Técnica de ", "Technique of ")) + M::Nombre(tc.donante);
    if (c.tipo == 1) sub = T::T("Título del Héroe", "Hero title");
    if (c.tipo == 2) sub = T::T("Forma de tu raza", "Form of your race");
    if (c.tipo == 3) sub = T::T("Escenario", "Stage");
    const bool ki = c.tipo == 0 && c.ki > 0;
    const float ancho = ki ? 280 : 390;
    v2::Texto(dl, q, 1148, 731, v2::kLetraMin, sub, 0, 0, false, false, ancho);
    v2::Texto(dl, q, 1148, 754, v2::kLetraMin, c.ok ? T::T("Se ganó:", "Earned:") : T::T("Cómo se gana:", "How to earn:"), 0, 0, false, false, ancho);
    v2::Texto(dl, q, 1148, 777, v2::kLetraMin, c.cond, 0, 2, false, false, ancho);
    if (ki) {
      const int nb = std::min(c.ki / 10, 5);
      for (int b = 0; b < nb; ++b) v2::Orbe(dl, q, 1490 - 17.0f * float(nb - 1) + 34.0f * float(b), 736, 15, IM_COL32(120, 200, 255, 255));
      v2::Texto(dl, q, 1490, 766, v2::kLetraMin, Num(nb) + (nb == 1 ? T::T(" barra de ki", " ki bar") : T::T(" barras de ki", " ki bars")), 1, 0, false, false, 130);
    }
  }
  v2::BarraAyuda(dl, q, {{"A", T::T("Ver", "View")}, {"LB", ""}, {"RB", T::T("Pestaña", "Tab")}, {"B", T::T("Volver a la Habitación", "Back to the Room")}}, 771, 870, 46);   // abajo a la izquierda, como la maqueta
}

}  // namespace

void AbrirColeccion() {
  g_col_pest = 1;
  g_col_cur = 0;
  g_col_visto_pend.clear();
  if (const char* e = std::getenv("DBZ3_TEST_COL_PEST")) g_col_pest = std::clamp(std::atoi(e), 0, kColPest - 1);   // PRUEBA: pestana inicial
  if (const char* e = std::getenv("DBZ3_TEST_COL_CUR")) g_col_cur = std::max(0, std::atoi(e));                  // PRUEBA: celda inicial
  PedirVista(H::Cuerpo(), H::Traje());
  AsegurarCartas();   // las cabeceras de las cartas (cap_N.png), en segundo plano si faltan
  const std::vector<H::InfoPremio> ps = H::Premios();
  int ok = 0, nuevos = 0;
  for (const H::InfoPremio& p : ps) ok += p.conseguido, nuevos += p.nuevo;
  REXLOG_INFO("dbz3 heroe: Coleccion abierta: {} % ({} de {} conseguidos, {} nuevos)", H::ColeccionPorcentaje(), ok, ps.size(), nuevos);
}

Accion Coleccion(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  static double antes = 0;
  const float dt = float(std::clamp(ahora - antes, 0.0, 0.1));
  antes = ahora;
  ControlesVistaV2(a, p, dt, ahora);
  auto marcar_visto = [&](int pest, int cur) {   // salir de una celda (o A en ella) quita su NUEVO
    const std::vector<CeldaCol> cs = CeldasCol(pest);
    if (cur < 0 || cur >= int(cs.size()) || !cs[size_t(cur)].nuevo) return;
    H::ColeccionVisto(cs[size_t(cur)].id);
    REXLOG_INFO("dbz3 heroe: Coleccion: visto {}", cs[size_t(cur)].id);
  };
  if (p & kB) {
    marcar_visto(g_col_pest, g_col_cur);
    REXLOG_INFO("dbz3 heroe: Coleccion: atras");
    return Accion::kVolver;
  }
  if (p & (kLb | kRb)) {
    marcar_visto(g_col_pest, g_col_cur);
    g_col_pest = SiguientePestanaCol(g_col_pest, (p & kRb) ? 1 : -1);
    g_col_cur = 0;
    REXLOG_INFO("dbz3 heroe: Coleccion: pestana {} ({} celdas)", g_col_pest, CeldasCol(g_col_pest).size());
  }
  const int n = int(CeldasCol(g_col_pest).size());
  if (n > 0) {
    const int antes_cur = g_col_cur = std::clamp(g_col_cur, 0, n - 1);
    if (p & kRight) g_col_cur = std::min(n - 1, g_col_cur + (g_col_cur % kColCols == 0 ? 1 : 0));
    if (p & kLeft) g_col_cur -= g_col_cur % kColCols == 1 ? 1 : 0;
    if (p & kDown) g_col_cur = std::min(n - 1, g_col_cur + kColCols);
    if (p & kUp) g_col_cur = std::max(0, g_col_cur - kColCols);
    if (g_col_cur != antes_cur) {
      marcar_visto(g_col_pest, antes_cur);
      const std::vector<CeldaCol> cs = CeldasCol(g_col_pest);
      REXLOG_INFO("dbz3 heroe: Coleccion: celda {} ({}, {})", g_col_cur, cs[size_t(g_col_cur)].id, cs[size_t(g_col_cur)].ok ? "conseguida" : "bloqueada");
    }
    if (p & kA) marcar_visto(g_col_pest, g_col_cur);
  }
  DibujarColeccionV2(dl, a, t, ahora);
  return Accion::kNada;
}

// Para el aviso del resultado (rpg.cpp): la cabecera real de la carta de una tecnica y la cabeza redonda, en el lienzo 1600x900 `q`.
bool AvisoCabeceraTecnica(ImDrawList* dl, const S::Area& q, int cap, float x0, float y0, float w) {
  static bool pedido = false;
  if (!pedido) pedido = (AsegurarCartas(), true);   // las cabeceras cap_N.png, una vez y en segundo plano si faltan
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  if (!PicPng(CartaPng(cap), ahora)) return false;   // aun sin cabecera: el que llama pone cara y nombre
  CabeceraCarta(dl, q, cap, x0, y0, w, ahora, 255);
  return true;
}
void AvisoCara(ImDrawList* dl, const S::Area& q, int id, float cx, float cy, float r) { CaraRedonda(dl, q, id, cx, cy, r, 255, false); }
S::Pic AvisoPiezaTitulo() { return PiezaTitulo(); }
