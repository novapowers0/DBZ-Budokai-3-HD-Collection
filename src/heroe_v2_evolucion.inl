// dbz3 - Modo Heroe, Evolucion por raza (maqueta_4_evolucion de E:\DBZ3HD_trabajo\heroe_rediseno; ajuste dbz3_heroe_evolucion,
// APAGADO de serie; se abre desde Habitacion > Evolucion). 6 pestanas de raza (LB/RB), el arbol de formas de la raza activa
// (tronco comun, rama A arriba y rama B abajo) leido de res/heroe/desbloqueos.toml tipo "forma" (regla: heroe_formas.h), el heroe
// a la derecha sobre su peana y la ficha de la forma elegida. Estados: abierta (aro dorado), actual (grande, con brillo) y
// bloqueada ("?" con su condicion debajo; mas apagada si su camino esta cerrado).
// Arte que falta (ENCARGOS_ASSETS\heroe\ENCARGO_evolucion_piezas.md): el cuerpo de Shenron del selector de escenarios como camino,
// el aro de burbuja del selector y los cuerpos enteros de cada forma. Mientras: caminos punteados dorados, aro dibujado y cabezas
// nativas (CaraRedonda de heroe_v2_maestros.inl).
// Se incluye en heroe_ui.cpp junto a los otros heroe_v2_*.inl (usa FiguraV2, FondoMapaV2, CaraRedonda, PlacaDuV2).
// Medidas en px del lienzo 1600x900 (= maqueta 1920x1080 x 0,8333).
namespace {

constexpr int kRazasEvo = 6;
// pestanas: nombre y luchador de la cabeza nativa (Goku, Krilin, Piccolo, Freezer, Super Bu, Cell)
const char* const kRazaEvoEs[kRazasEvo] = {"Saiyan", "Humano", "Namekiano", "Raza de Freezer", "Majin", "Androide"};
const char* const kRazaEvoEn[kRazasEvo] = {"Saiyan", "Human", "Namekian", "Frieza Race", "Majin", "Android"};
constexpr int kCaraRazaEvo[kRazasEvo] = {0, 10, 11, 27, 35, 33};
// forma base de cada raza (su cabeza en la raiz del arbol)
constexpr int kCaraBaseEvo[kRazasEvo] = {0, 10, 11, 27, 34, 30};
const char* const kBaseEvo[kRazasEvo] = {"Saiyan", "Humano", "Namekiano", "Freezer", "Majin Bu", "Androide"};
const char* const kBaseEvoEn[kRazasEvo] = {"Saiyan", "Human", "Namekian", "Frieza", "Majin Buu", "Android"};
const char* const kRasgoEs[kRazasEvo] = {
    "Más fuerte cuanto peor va el combate; se transforma.", "Sin transformaciones fuertes; más técnicas y mejor guardia.",
    "Se regenera; la evolución sube el aura y el poder, no el aspecto.", "Empieza contenido y gana una forma por saga.",
    "Mucha vida, poco ataque; absorbe para crecer.", "No gasta ki al cargar; evoluciona por absorción."};
const char* const kRasgoEn[kRazasEvo] = {
    "Stronger the worse the fight goes; transforms.", "No strong transformations; more techniques and better guard.",
    "Regenerates; evolution raises aura and power, not looks.", "Starts restrained and gains a form each saga.",
    "Lots of health, low attack; absorbs to grow.", "Spends no ki while charging; evolves by absorption."};

// R23 HER-12: el rasgo de la raza del heroe en una linea, para la ayuda del creador (DISENO 7.3)
std::string RasgoRazaTexto(bool corto) {   // corto: solo la frase (la placa del creador es estrecha)
  const int r = std::clamp(H::RazaEvolucion(), 0, kRazasEvo - 1);
  if (corto) return T::T(kRasgoEs[r], kRasgoEn[r]);
  return std::string(T::T("Rasgo ", "Trait ")) + T::T(kRazaEvoEs[r], kRazaEvoEn[r]) + ": " + T::T(kRasgoEs[r], kRasgoEn[r]);
}
int g_evo_raza = 0;   // pestana
int g_evo_cur = 0;    // nodo elegido (0 = forma base; i = i-esima forma de la raza)

struct NodoEvo {
  float x = 0, y = 0;
  int depth = 0;
  int padre = -1;   // indice del nodo anterior (-1: raiz)
  bool actual = false;
  const H::InfoForma* f = nullptr;   // nullptr: forma base
  float r = 71;     // radio de la burbuja (lienzo)
};
float PasoEvo(int maxd) { return std::min(200.0f, 600.0f / float(std::max(maxd, 1))); }   // el ultimo nodo nunca pasa de x 1046
float RadioEvo(float paso) { return std::clamp(paso * 0.5f - 29.0f, 30.0f, 71.0f); }

// R23 HER-04 v3 (maqueta 12 medida a 1600x900): base a la izquierda (x 446, y 487), rama A arriba (filas escalonadas como la
// maqueta: 329/292/342), tronco en el medio y rama B abajo (608); paso 200 px (se aprieta si hay mas de 3 pasos). La forma actual
// es mucho mayor (r 108 frente a 71) y lleva el aro de oro grueso.
std::vector<NodoEvo> DisponerEvo(const std::vector<H::InfoForma>& fs, int raza) {
  constexpr float x0 = 446, ymid = 480, yb = 598;
  constexpr float kYa[5] = {317, 317, 284, 334, 292};
  std::vector<NodoEvo> n(fs.size() + 1);
  n[0].x = x0, n[0].y = ymid;
  auto indice = [&](const std::string& id) {
    for (size_t i = 0; i < fs.size(); ++i)
      if (fs[i].id == id) return int(i) + 1;
    return 0;
  };
  int maxd = 1;
  for (size_t i = 0; i < fs.size(); ++i) {
    NodoEvo& d = n[i + 1];
    d.f = &fs[i];
    d.padre = indice(fs[i].previa);
    d.depth = n[size_t(d.padre)].depth + 1;   // las "previa" van antes en el toml: ya esta calculado
    maxd = std::max(maxd, d.depth);
    d.y = fs[i].rama == "A" ? kYa[std::min(d.depth, 4)] : fs[i].rama == "B" ? yb : ymid;
  }
  const float paso = PasoEvo(maxd);
  for (NodoEvo& d : n) d.x = x0 + paso * float(d.depth);
  // "actual" = la forma abierta mas lejana con cuerpo (cara); las que se ganan con capsula (Absorcion) no son una forma en la
  // que estes (maqueta 12: Super Bu es la actual y Absorcion su capsula). Sin ninguna, la base.
  int mejor = 0, prof = 0;
  for (size_t i = 1; i < n.size(); ++i)
    if (n[i].f->estado == 0 && n[i].f->cara >= 0 && n[i].depth > prof) mejor = int(i), prof = n[i].depth;
  n[size_t(mejor)].actual = true;
  const float r = RadioEvo(paso);
  for (NodoEvo& d : n) d.r = d.actual ? std::min(96.0f, paso - r - 12) : d.f ? r : std::min(79.0f, r + 8);
  (void)raza;
  return n;
}

std::vector<std::string> LineasEvo(const std::string& s, float ancho, float px) {
  std::vector<std::string> l(1);
  for (size_t i = 0; i < s.size();) {
    size_t fin = s.find(' ', i);
    if (fin == std::string::npos) fin = s.size();
    const std::string pal = s.substr(i, fin - i);
    if (!l.back().empty() && v2::Ancho(px, l.back() + " " + pal) > ancho) l.emplace_back();
    l.back() += (l.back().empty() ? "" : " ") + pal;
    i = fin + 1;
  }
  return l;
}

// Camino punteado del mapa DU (maqueta 12, mismo trazo que el cruce): curva de padre a hijo con tangentes horizontales,
// puntos amarillos con borde oscuro cada ~9 px, solo fuera de las dos burbujas.
void CaminoPunteadoEvo(ImDrawList* dl, const S::Area& q, const NodoEvo& a, const NodoEvo& b, bool vivo) {
  const float mx = (a.x + b.x) * 0.5f;
  auto P = [&](float u) {
    const float v = 1 - u;
    return ImVec2(v * v * v * a.x + 3 * v * v * u * mx + 3 * v * u * u * mx + u * u * u * b.x, v * v * v * a.y + 3 * v * v * u * a.y + 3 * v * u * u * b.y + u * u * u * b.y);
  };
  const ImU32 borde = IM_COL32(60, 34, 0, 240), rel = vivo ? IM_COL32(255, 232, 70, 255) : IM_COL32(235, 200, 110, 230);
  ImVec2 prev = P(0);
  float acum = 12;
  for (int i = 1; i <= 200; ++i) {
    const ImVec2 c = P(float(i) / 200);
    acum += std::hypot(c.x - prev.x, c.y - prev.y);
    prev = c;
    if (acum < 12) continue;
    acum = 0;
    if (std::hypot(c.x - a.x, c.y - a.y) < a.r + 10 || std::hypot(c.x - b.x, c.y - b.y) < b.r + 10) continue;
    dl->AddCircleFilled(q.At(c.x, c.y), 6.4f * q.s, borde, 12);
    dl->AddCircleFilled(q.At(c.x, c.y), 4.6f * q.s, rel, 12);
  }
}

// "?" de lo que falta (maqueta 12 y 13): moneda dorada (disco naranja, centro mas claro) con el "?" marron. c y r en pantalla.
void MonedaInterrogacion(ImDrawList* dl, ImVec2 c, float r, int al) {
  dl->AddCircleFilled(c, r, IM_COL32(205, 120, 20, al), 48);
  dl->AddCircleFilled(ImVec2(c.x - r * 0.08f, c.y - r * 0.08f), r * 0.82f, IM_COL32(250, 185, 45, al), 48);
  dl->AddCircle(c, r, IM_COL32(120, 60, 10, al), 48, std::max(1.5f, r * 0.05f));
  ImFont* f = ImGui::GetFont();
  const float tam = r * 1.5f;
  const ImVec2 ts = f->CalcTextSizeA(tam, FLT_MAX, 0, "?");
  dl->AddText(f, tam, ImVec2(c.x - ts.x * 0.5f + r * 0.04f, c.y - ts.y * 0.5f + r * 0.04f), IM_COL32(60, 25, 0, al / 2), "?");
  dl->AddText(f, tam, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(110, 50, 5, al), "?");
}

// R23 HER-04 (juez): la cara del nodo es el retrato de dialogo de B3 recortado en circulo (como el cruce de rutas y la maqueta 12);
// sin retrato, la cara del selector. Nada sale del circulo.
// R23 v3: cabeza redonda del selector (maqueta 12: 2027_03/07/08 ampliadas) para los luchadores que la tienen
bool CabezaSelectorEvo(ImDrawList* dl, ImVec2 c, float r, int id, int alfa) {
  struct Ca { int id; const char* pieza; int i; };
  static const Ca kCa[] = {{10, "heroe_cabezas_15", 0}, {11, "heroe_cabezas_15", 1}, {0, "heroe_cabezas_13", 0}, {18, "heroe_cabezas_16", 1}, {27, "heroe_cabezas_16", 3},
                           {33, "heroe_cabezas_18", 1}, {34, "heroe_cabezas_18", 2}, {35, "heroe_cabezas_18", 3}, {36, "heroe_cabezas_18", 4}};
  for (const Ca& k : kCa) {
    if (k.id != id) continue;
    const S::Pic p = S::Img(k.pieza);
    if (!p) return false;
    const float x0 = 6.0f + 84.0f * float(k.i);
    S::DrawPic(dl, p, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec4(x0, 6, x0 + 72, 78), IM_COL32(alfa, alfa, alfa, 255));
    return true;
  }
  return false;
}

void CaraNodoEvo(ImDrawList* dl, const S::Area& q, int id, float cx, float cy, float r, int alfa) {
  const ImVec2 c = q.At(cx, cy);
  if (CabezaSelectorEvo(dl, c, r * 1.04f * q.s, id, alfa)) return;
  dl->AddCircleFilled(c, r * q.s, IM_COL32(40, 70, 110, 235), 48);
  S::Pic p = S::Img(("caras/" + std::to_string(id)).c_str());   // R23 v3 (juez): la cabeza entera del selector, como la maqueta
  if (!p) p = S::Img(("retratos/" + std::to_string(id)).c_str());
  if (!p) return;
  const float lado = std::min(p.w, p.h), u0 = (p.w - lado) * 0.5f / p.w, v0 = (p.h - lado) * 0.5f / p.h;
  dl->AddImageRounded(p.tex, ImVec2(c.x - r * q.s, c.y - r * q.s), ImVec2(c.x + r * q.s, c.y + r * q.s), ImVec2(u0, v0),
                      ImVec2(1 - u0, 1 - v0), IM_COL32(alfa, alfa, alfa, 255), r * q.s);
}

void NodoDibujarEvo(ImDrawList* dl, const S::Area& q, float t, const NodoEvo& n, int cara_base, bool sel, bool pista, float r) {
  const bool base = n.f == nullptr;
  const int estado = base ? 0 : n.f->estado;
  const ImVec2 c = q.At(n.x, n.y);
  if (n.actual) {   // brillo del nodo actual (efecto, colores oro del juego): halo ancho que late
    const float pulso = 0.5f + 0.5f * std::sin(t * 3.0f);
    dl->AddCircleFilled(c, (r + 22) * q.s, IM_COL32(255, 190, 50, int(40 + 35 * pulso)), 64);
    dl->AddCircleFilled(c, (r + 12) * q.s, IM_COL32(255, 210, 90, int(60 + 40 * pulso)), 64);
  }
  if (estado == 0) {   // abierta: cabeza nativa con aro dorado
    if (!base && n.f->cara < 0) {   // forma que se gana con una capsula (Absorcion): la botella nativa en la burbuja
      dl->AddCircleFilled(c, r * q.s, IM_COL32(40, 70, 110, 235), 40);
      CapsulaV2(dl, q, n.x, n.y, r * 1.2f, true);
    } else {
      CaraNodoEvo(dl, q, base ? cara_base : n.f->cara, n.x, n.y, r, 255);
    }
    dl->AddCircle(c, (r - 2) * q.s, IM_COL32(150, 210, 250, 210), 64, 3 * q.s);   // borde azul de la burbuja del selector
    dl->AddCircle(c, (r + 4) * q.s, IM_COL32(255, 205, 60, 255), 64, (n.actual ? 8 : 5) * q.s);
    if (n.actual) dl->AddCircle(c, (r + 10) * q.s, IM_COL32(200, 110, 20, 230), 64, 2.5f * q.s);
  } else if (pista && n.f->cara >= 0) {   // primer paso aun cerrado: su cabeza en sombra y el nombre (maqueta: Kid Bu)
    CaraNodoEvo(dl, q, n.f->cara, n.x, n.y, r, 80);
    dl->AddCircle(c, (r + 3) * q.s, IM_COL32(232, 150, 40, 200), 48, 4 * q.s);
  } else {   // bloqueada: burbuja oscura con "?" naranja vivo (maqueta 12; R23 HER-04: el juez pedia el naranja sin apagar)
    dl->AddCircleFilled(c, (r + 5) * q.s, IM_COL32(16, 28, 48, 235), 48);   // aro oscuro de la burbuja (maqueta)
    MonedaInterrogacion(dl, c, r * q.s, 255);   // nitida siempre (juez r1)   // maqueta 12; camino cerrado = mas apagada
  }
  if (sel) dl->AddCircle(c, (r + (n.actual ? 16 : 10)) * q.s, IM_COL32(150, 245, 240, 255), 48, 4 * q.s);
}

// -1 = "todavia sin elegir": el cursor empieza en el nodo actual (la forma abierta mas lejana)
int AjustarEvo(const std::vector<NodoEvo>& n, int cur) {
  if (cur < 0)
    for (size_t i = 0; i < n.size(); ++i)
      if (n[i].actual) return int(i);
  return std::clamp(cur, 0, int(n.size()) - 1);
}

void DibujarEvolucionV2(ImDrawList* dl, const S::Area& a, float t) {
  FondoSinAnfitrion(dl, a);
  if (const S::Pic pan = S::Img("heroe_pan_6")) {   // Namek: panoramica del selector desenfocada (9 tomas desplazadas); la maqueta: vuelo de DU
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    const float s = (g.p1.x - g.p0.x) / 1600.0f;
    S::DrawPic(dl, pan, g.p0, g.p1, ImVec4(0, 112, pan.w, 124));
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
        S::DrawPic(dl, pan, ImVec2(g.p0.x + dx * 12 * s, g.p0.y + dy * 12 * s), ImVec2(g.p1.x + dx * 12 * s, g.p1.y + dy * 12 * s),
                   ImVec4(0, 0, pan.w, pan.w * 9.0f / 16.0f), IM_COL32(255, 255, 255, 70));
  }
  {
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    dl->AddRectFilled(g.p0, g.p1, IM_COL32(12, 22, 35, 100));
  }
  const S::Area q = v2::Lienzo(a);
  v2::Titulo(dl, q, T::T("Evolución", "Evolution"));
  const int mi_raza = H::RazaEvolucion();
  // pestanas de raza (izquierda)
  for (int i = 0; i < kRazasEvo; ++i) {
    const float y0 = 178 + 100.0f * i, y1 = y0 + 90, cy = (y0 + y1) * 0.5f;
    const bool sel = i == g_evo_raza;
    if (sel) {   // R23 HER-04 (juez): la elegida es una pildora cian redondeada (maqueta 12), no la franja con textura
      const float rr = (y1 - y0) * 0.5f * q.s;
      dl->AddRectFilled(q.At(33, y0), q.At(317, y1), IM_COL32(60, 165, 222, 245), rr);
      dl->AddRectFilled(q.At(41, y0 + 5), q.At(309, (y0 + y1) * 0.5f), IM_COL32(150, 232, 250, 120), rr * 0.8f, ImDrawFlags_RoundCornersTop);
      dl->AddRect(q.At(33, y0), q.At(317, y1), IM_COL32(230, 255, 255, 255), rr, 0, 2.5f * q.s);
    } else {
      v2::Caja(dl, q, 33, y0, 300, y1, 150);
    }
    if (!CabezaSelectorEvo(dl, q.At(83, cy), 41 * q.s, kCaraRazaEvo[i], 255)) {   // la cabeza del selector, como la maqueta
      dl->PushClipRect(q.At(40, cy - 43), q.At(126, cy + 43), true);
      CaraRedonda(dl, q, kCaraRazaEvo[i], 83, cy, 38, 255, sel);
      dl->PopClipRect();
    }
    {   // R23 HER-04: un nombre largo ("Raza de Freezer") va en letra pequena entera, no cortado con ".." (maqueta)
      const std::string nom = T::T(kRazaEvoEs[i], kRazaEvoEn[i]);
      const bool cabe = v2::Ancho(v2::kLetraFila * q.s, nom) <= 172 * q.s;
      v2::Texto(dl, q, 136, cy, cabe ? v2::kLetraFila : v2::kLetraMin, nom, 0, 0, sel, !sel, cabe ? 176 : 0);
    }
    if (i == mi_raza && !sel) dl->AddCircleFilled(q.At(290, cy), 6 * q.s, IM_COL32(255, 200, 60, 255), 12);   // punto: tu raza
  }
  // arbol de la raza activa
  const std::vector<H::InfoForma> fs = H::FormasDeRaza(g_evo_raza);
  const std::vector<NodoEvo> nodos = DisponerEvo(fs, g_evo_raza);
  g_evo_cur = AjustarEvo(nodos, g_evo_cur);
  const float paso = PasoEvo([&] { int m = 1; for (const NodoEvo& n : nodos) m = std::max(m, n.depth); return m; }());
  for (size_t i = 1; i < nodos.size(); ++i) CaminoPunteadoEvo(dl, q, nodos[size_t(nodos[i].padre)], nodos[i], nodos[i].f->estado == 0);
  for (size_t i = 0; i < nodos.size(); ++i) {
    const NodoEvo& n = nodos[i];
    // R23 HER-04 (maqueta 12): solo el primer paso de cada camino ensena su cabeza en sombra (Kid Bu); los pasos mas hondos aun
    // cerrados son "?" con su condicion debajo
    const bool pista = n.f && n.f->estado != 0 && n.padre == 0;
    // R23 CAC: en la pestana de tu raza la raiz eres tu (la cabeza de tu cuerpo nativo y tu nombre), no el modelo de la raza
    const bool propia = g_evo_raza == mi_raza;
    NodoDibujarEvo(dl, q, t, n, propia && H::Cuerpo() < 44 ? H::Cuerpo() : kCaraBaseEvo[g_evo_raza], int(i) == g_evo_cur, pista, n.r);
    const bool base = n.f == nullptr;
    const int estado = base ? 0 : n.f->estado;
    const float placa = std::min(n.r + (n.actual ? 2.0f : 22.0f), paso * 0.5f - 6 + (n.actual ? 12.0f : 0.0f)), yp = n.y + n.r + 8;
    if (!base && !n.f->rama.empty() && (n.padre == 0 || nodos[size_t(n.padre)].f->rama != n.f->rama)) {
      // nombre del camino en pastilla sobre el primer nodo de la rama (rosa la A, cian la B: maqueta 12)
      const float w = v2::Ancho(v2::kLetraPeque * q.s, n.f->camino) / q.s + 34, yc = n.y - n.r - (n.actual ? 26.0f : 24.0f);
      v2::Caja(dl, q, n.x - w * 0.5f, yc - 18, n.x + w * 0.5f, yc + 18, 225);
      const ImU32 tinta = n.f->rama == "A" ? IM_COL32(255, 120, 130, estado == 2 ? 150 : 255) : IM_COL32(110, 215, 250, estado == 2 ? 150 : 255);
      ImFont* f = v2::Letra();
      const ImVec2 ts = f->CalcTextSizeA(v2::kLetraPeque * q.s, FLT_MAX, 0, n.f->camino.c_str());
      const ImVec2 pc = q.At(n.x, yc);
      dl->AddText(f, v2::kLetraPeque * q.s, ImVec2(pc.x - ts.x * 0.5f + 2 * q.s, pc.y - ts.y * 0.5f + 2 * q.s), IM_COL32(0, 0, 0, 200), n.f->camino.c_str());
      dl->AddText(f, v2::kLetraPeque * q.s, ImVec2(pc.x - ts.x * 0.5f, pc.y - ts.y * 0.5f), tinta, n.f->camino.c_str());
    }
    // nombre en placa naranja bajo el nodo (letra de fila, como la maqueta); bloqueada: "?" y su condicion en dorado
    CajaFinaHab(dl, q, n.x - placa, yp, n.x + placa, yp + 44);
    v2::Texto(dl, q, n.x, yp + 22, v2::kLetraPeque, base ? (propia ? H::NombreMostrado() : std::string(T::T(kBaseEvo[g_evo_raza], kBaseEvoEn[g_evo_raza]))) : estado == 0 || pista ? n.f->nombre : std::string("?"), 1,
              0, int(i) == g_evo_cur, false, 2 * placa - 16);
    auto Chip = [&](float y, const std::string& s, bool oro) {   // linea de texto sobre ficha oscura
      const float px = 20 * q.s, w = v2::Ancho(px, s) / q.s + 16;
      v2::Caja(dl, q, n.x - w * 0.5f, y - 13, n.x + w * 0.5f, y + 13, 200);
      S::MenuTextB3(dl, q.At(n.x, y), px, s.c_str(), oro, estado == 2);
    };
    if (base) Chip(yp + 58, T::T("Inicio", "Start"), false);
    if (!base && estado != 0) {   // texto de desbloqueo bajo cada forma cerrada (maqueta: "Vence a Gohan en la Saga de Bu")
      const std::string cond = g_evo_raza == H::RazaEvolucion() ? n.f->condicion : std::string(T::T("Solo con esta raza", "Only with this race"));
      const std::vector<std::string> ls = LineasEvo(cond, std::max(150.0f, 2 * placa) * q.s, 20 * q.s);
      for (size_t k = 0; k < ls.size() && k < 2; ++k) Chip(yp + 58 + 25.0f * k, ls[k], true);
    }
  }
  // rasgo de la raza, una linea, bajo el arbol
  v2::Caja(dl, q, 33, 804, 1567, 844, 200);
  {
    const std::string et = std::string(T::T("Rasgo ", "Trait ")) + T::T(kRazaEvoEs[g_evo_raza], kRazaEvoEn[g_evo_raza]) + ":";
    const float tam = v2::kLetraPeque;
    ImFont* f = v2::Letra();
    const ImVec2 p = q.At(55, 824);
    const float w = f->CalcTextSizeA(tam * q.s, FLT_MAX, 0, et.c_str()).x;
    dl->AddText(f, tam * q.s, ImVec2(p.x + 2 * q.s, p.y - tam * q.s * 0.5f + 2 * q.s), IM_COL32(0, 0, 0, 200), et.c_str());
    dl->AddText(f, tam * q.s, ImVec2(p.x, p.y - tam * q.s * 0.5f), IM_COL32(110, 215, 250, 255), et.c_str());
    v2::Texto(dl, q, 55 + w / q.s + 18, 824, tam, T::T(kRasgoEs[g_evo_raza], kRasgoEn[g_evo_raza]), 0, 0, false, false, 1450 - w / q.s);
  }
  // el heroe a la derecha sobre su peana (sin tapar) y la ficha de la forma elegida debajo
  {
    constexpr float k = 0.82f;   // R23 v3: heroe algo menor y mas abajo (maqueta 12: peana en y 642)
    S::Area qf = q;
    qf.s = q.s * k;
    qf.p0 = q.At(1342 - v2::kFigCx * k, -14);
    qf.p1 = ImVec2(qf.p0.x + 1600.0f * qf.s, qf.p0.y + 900.0f * qf.s);
    FiguraV2(dl, qf);
  }
  {
    const NodoEvo& n = nodos[size_t(g_evo_cur)];
    const bool base = n.f == nullptr;
    const int estado = base ? 0 : n.f->estado;
    constexpr float fx0 = 1127, fx1 = 1561, fy0 = 668, fy1 = 798;   // R23 v3: ficha baja y compacta (maqueta 12)
    S::Frame(dl, q.At(fx0, fy0), q.At(fx1, fy1), q.s * 0.6f);   // marco fino (juez r1)
    v2::Texto(dl, q, fx0 + 26, fy0 + 28, v2::kLetraFila, base ? std::string(T::T("Forma base", "Base form")) : estado == 0 ? n.f->nombre : std::string("???"), 0, 2,
              false, false, fx1 - fx0 - 60);
    std::string l1, l2, l3;   // R23 v3: tres lineas como la maqueta; la ultima (de donde viene) en dorado
    if (base) {
      l1 = T::T("Tu punto de partida.", "Your starting point.");
      l2 = T::T("Todas las formas de la raza salen de aquí.", "Every form of the race starts here.");
    } else if (estado == 0) {
      l1 = n.f->condicion;
      // R23 CAC: lo que pasa en combate con TU cuerpo y estilo (la forma nativa la trae el cuerpo con su propio estilo)
      l2 = n.f->combate == 1   ? std::string(T::T("En combate: P+K+G con ki", "In battle: P+K+G with ki"))
           : n.f->combate == 0 ? std::string(T::T("En combate: tu cuerpo y estilo no la tienen", "In battle: your body and style lack it"))
           : n.f->capsula > 0  ? std::string(T::T("Cápsula de forma en cada combate", "Form capsule in every fight"))
                               : std::string(T::T("Forma adquirida", "Form acquired"));
      l3 = n.f->camino.empty() ? std::string(T::T("Abierta en el tronco común", "Opened on the main trunk"))
                               : std::string(T::T("Abierta al elegir el ", "Opened by choosing the ")) + n.f->camino;
    } else {
      l1 = g_evo_raza == H::RazaEvolucion() ? n.f->condicion : std::string(T::T("Otra raza", "Other race"));
      l2 = estado == 2 ? std::string(T::T("Camino cerrado por ahora.", "Path closed for now.")) : std::string(T::T("Camino abierto: falta la condición.", "Path open: condition pending."));
      if (!n.f->camino.empty()) l3 = n.f->camino;
    }
    const float ancho = fx1 - fx0 - 52;
    std::vector<std::string> ls = LineasEvo(l1, ancho * q.s, 20 * q.s);   // la condicion larga en 2 lineas, no cortada
    if (ls.size() > 2) ls.resize(2);
    const size_t n1 = ls.size();
    ls.push_back(l2);
    if (!l3.empty()) ls.push_back(l3);
    const float paso = ls.size() > 3 ? 20.0f : 22.0f;
    for (size_t k = 0; k < ls.size(); ++k)
      v2::Texto(dl, q, fx0 + 28, fy0 + 54 + paso * float(k), 20, ls[k], 0, k == n1 + 1 ? 2 : 0, false, false, ancho);
  }
  v2::BarraAyuda(dl, q, {{"A", T::T("Ver forma", "View form")}, {"LB", T::T("Raza anterior", "Previous race")}, {"RB", T::T("Raza siguiente", "Next race")}, {"B", T::T("Volver a la Habitación", "Back to the Room")}}, 990, 872);
}

// Nodo vecino en la direccion (dx, dy): el mas cercano que quede de ese lado (con mas peso al desvio lateral).
int VecinoEvo(const std::vector<NodoEvo>& n, int cur, float dx, float dy) {
  int mejor = -1;
  float mejor_d = 1e9f;
  for (size_t i = 0; i < n.size(); ++i) {
    if (int(i) == cur) continue;
    const float vx = n[i].x - n[size_t(cur)].x, vy = n[i].y - n[size_t(cur)].y;
    const float main_ = vx * dx + vy * dy, lat = std::abs(vx * dy) + std::abs(vy * dx);
    if (main_ <= 1) continue;
    const float d = main_ + 2 * lat;
    if (d < mejor_d) mejor = int(i), mejor_d = d;
  }
  return mejor;
}

}  // namespace
