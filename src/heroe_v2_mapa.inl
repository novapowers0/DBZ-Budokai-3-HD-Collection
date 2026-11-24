// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04, V05-B): dibujo del Mapa del mundo (MapaUI, maqueta 04_mapa).
// Solo DIBUJO: el cursor de saga, A/B/LB y los avisos siguen en MapaUI.
// V05-B (como la maqueta): la lista elige la SAGA y el terreno ensena SUS combates como medallones con la foto redonda del
// escenario (selector de escenarios, data_usi 2022_11..21/28), el numero en una bolita y el rival debajo, unidos por una
// linea de puntos amarilla; el actual con aro amarillo y una punta encima (sin la figura 3D del heroe de pie).
// Fondo: la pieza de mod `heroe/mapa_tierra` (o `heroe/mapa_planeta_verde` en Namek) si existe; si no, PROVISIONAL, la
// panoramica del escenario del lugar de la saga (data_usi 2043_29..41, fondos del selector de escenarios; Namek para la de
// Ginyu); sin ella, el terreno de siempre (Tierra: mar e islas por codigo). La maqueta usaba una captura del vuelo de
// Dragon Universe: no es una textura del juego (ver ESTADO_V03_V04.md).
// Piezas de mod que se usan si existen (ENCARGO_GPT.md, tanda 1): heroe/nodo_pendiente|actual|hecho|bloqueado|jefe,
// heroe/ruta_punto, heroe/marcador_heroe, heroe/mapa_tierra, heroe/mapa_planeta_verde.
// Se incluye en heroe_ui.cpp junto a los otros heroe_v2_*.inl, antes del mapa: Punto/Tierra/Esfera se declaran aqui.
// Kit: heroe_ui_v2.h. Medidas en px del lienzo 1600x900 (= maqueta 1280x720 x 1.25).
namespace {

ImVec2 Punto(int i);
void Tierra(ImDrawList* dl, const S::Area& a);
void Esfera(ImDrawList* dl, ImVec2 c, float r, int estrellas, int alfa);

// Registro de serie (0..16) -> fila de escenarios.cpp (kFila de Nombre(): mismo orden que sus nombres). Las fotos y las
// panoramicas del selector van en ese orden de filas: heroe_esc_<fila> / heroe_pan_<fila> (ingame/native_ui.cpp).
constexpr int kFilaEsc[17] = {8, 5, 3, 9, 7, 10, 1, 6, 11, 4, 2, 3, 9, 7, 6, 4, 0};
int FilaEsc(int registro) { return registro >= 0 && registro < 17 ? kFilaEsc[registro] : -1; }
S::Pic PiezaEsc(const char* prefijo, int registro) {   // altas de mods (17+): sin pieza
  const int f = FilaEsc(registro);
  return f < 0 ? S::Pic{} : S::Img((std::string(prefijo) + std::to_string(f)).c_str());
}

// Bolita numerada 1-8 del Torneo (2049_16) de diametro d; sin la pieza (o n > 8), circulo naranja con la cifra.
void BolitaV2(ImDrawList* dl, const S::Area& q, float cx, float cy, float d, int n, bool apagada) {
  const S::Pic p = S::Img(v2::kPiezaBolitas);
  if (p && n >= 1 && n <= 8) {
    const float x = 4.0f + 60.0f * float(n - 1);
    S::DrawPic(dl, p, q.At(cx - d * 0.5f, cy - d * 0.5f), q.At(cx + d * 0.5f, cy + d * 0.5f), ImVec4(x, 3, x + 46, 48),
               apagada ? IM_COL32(110, 110, 120, 255) : IM_COL32(255, 255, 255, 255));
    return;
  }
  const ImVec2 c = q.At(cx, cy);
  dl->AddCircleFilled(c, d * 0.5f * q.s, apagada ? IM_COL32(90, 90, 100, 240) : IM_COL32(240, 140, 30, 255), 24);
  dl->AddCircle(c, d * 0.5f * q.s, IM_COL32(40, 20, 0, 255), 24, 2 * q.s);
  v2::Texto(dl, q, cx, cy, d * 0.62f, Num(n), 1, 0, false, apagada);
}

// Radar del Dragon abajo a la derecha (aro 2062_09 + rejilla 2062_11 + bolas 2045_23). Sin las piezas: el radar de hoy.
void RadarV2(ImDrawList* dl, const S::Area& q, float t, int esferas, float d = 250, float y1 = 828) {   // Habitacion: radar pequeno
  constexpr float x1 = 1575;   // d = ancho del aro; (x1, y1) = su esquina de abajo a la derecha
  const float cx = x1 - d * 0.5f, cy = y1 - d * 0.5f, rr = d * 0.43f;   // el aro es redondo abajo; el boton, arriba
  const S::Pic aro = S::Img(v2::kPiezaRadarAro), rej = S::Img(v2::kPiezaRadarRejilla);
  const ImVec2 c = q.At(cx, cy);
  if (rej) {
    S::DrawPic(dl, rej, q.At(cx - rr, cy - rr), q.At(cx + rr, cy + rr), ImVec4(7, 3, 600, 600));
  } else {
    dl->AddCircleFilled(c, rr * q.s, IM_COL32(20, 70, 30, 255), 48);
    for (int g = 1; g <= 3; ++g) dl->AddCircle(c, rr * q.s * g / 3, IM_COL32(90, 220, 110, 160), 48, 1.5f * q.s);
    dl->AddLine(q.At(cx - rr, cy), q.At(cx + rr, cy), IM_COL32(90, 220, 110, 160), 1.5f * q.s);
    dl->AddLine(q.At(cx, cy - rr), q.At(cx, cy + rr), IM_COL32(90, 220, 110, 160), 1.5f * q.s);
  }
  const float barrido = t * 2.0f;   // el barrido y los puntos que parpadean: como el radar de hoy
  dl->AddLine(c, ImVec2(c.x + std::cos(barrido) * rr * q.s, c.y + std::sin(barrido) * rr * q.s), IM_COL32(180, 255, 190, 200), 2.5f * q.s);
  const S::Pic bola = S::Img(v2::kPiezaEstrella);
  for (int i = 0; i < 7; ++i) {
    const float ang = 6.2831853f * i / 7 - 1.5708f;
    if (i < esferas) {
      const float ex = cx + std::cos(ang) * d * 0.17f, ey = cy + std::sin(ang) * d * 0.17f, er = d * 0.06f;
      if (bola) S::DrawPic(dl, bola, q.At(ex - er, ey - er), q.At(ex + er, ey + er));
      else Esfera(dl, q.At(ex, ey), er * q.s, i + 1, 255);
    } else if (std::fmod(t * 2.0f + i * 0.3f, 2.0f) < 1.3f) {
      dl->AddCircleFilled(ImVec2(c.x + std::cos(ang * 1.7f) * rr * 0.8f * q.s, c.y + std::sin(ang * 1.7f) * rr * 0.8f * q.s),
                          5 * q.s, IM_COL32(255, 200, 60, 255), 10);
    }
  }
  if (aro) S::DrawPic(dl, aro, q.At(cx - d * 0.5f, y1 - d * 682.0f / 642.0f), q.At(x1, y1), ImVec4(13, 4, 655, 686));
  else dl->AddCircle(c, (rr + 6) * q.s, IM_COL32(250, 200, 40, 255), 48, 8 * q.s);
  v2::Texto(dl, q, cx, y1 + 26, v2::kLetraPeque, std::string(T::T("Esferas: ", "Dragon Balls: ")) + Num(esferas) + " / 7", 1, 2,
            false, false, std::max(d + 20, 260.0f));
}

// Fondo a toda la imagen. true si ha pintado el arte (mod o panoramica); false: el que llama pinta el terreno de siempre.
bool FondoMapaV2(ImDrawList* dl, const S::Area& a, int lugar) {
  const bool verde = FilaEsc(lugar) == 6 && S::Img("heroe/mapa_planeta_verde");   // Namek
  if (v2::Fondo(dl, a, verde ? "heroe/mapa_planeta_verde" : "heroe/mapa_tierra")) return true;
  const S::Pic p = PiezaEsc("heroe_pan_", lugar);
  if (!p) return false;
  // la panoramica (512x512) solo es opaca arriba y se funde hacia abajo: debajo, su franja de suelo opaca estirada
  // (filas 112-124) para que el fundido acabe en el mismo suelo; encima, el recorte 16:9 de arriba a toda la imagen
  FondoSinAnfitrion(dl, a);
  const S::Imagen g = S::GuestImage(ImGui::GetIO());
  S::DrawPic(dl, p, g.p0, g.p1, ImVec4(0, 112, p.w, 124));
  S::DrawPic(dl, p, g.p0, g.p1, ImVec4(0, 0, p.w, p.w * 9.0f / 16.0f));
  dl->AddRectFilled(g.p0, g.p1, IM_COL32(0, 8, 28, 40));
  S::Sides(dl, a);
  return true;
}

// Segundo pergamino, arriba a la derecha y al reves (el rollo a la derecha), con el nombre del lugar: la cinta del titulo
// (pieza "pergamino", 2033) espejada y la letra de los rotulos con perfil, como S::Banner.
void CartelLugarV2(ImDrawList* dl, const S::Area& q, const std::string& lugar) {
  constexpr float k = 1.35f, w = 448 * k, h = 130 * k, x0 = 1385 - w, y0 = 38;
  if (const S::Pic sc = S::Img("pergamino")) {
    S::DrawPic(dl, sc, q.At(x0, y0), q.At(x0 + w, y0 + h), ImVec4(4, 2, 452, 132), IM_COL32_WHITE, true);
  } else {
    v2::Caja(dl, q, x0 + 150, y0 + 30, x0 + w - 40, y0 + 100);
  }
  const auto& fonts = dbz3::launcher::ui::GetFonts();
  ImFont* f = fonts.banner ? fonts.banner : ImGui::GetFont();
  // POST2: letra de la maqueta 04 ("Planeta Namek" de x ~640 a 1050 de 1280: mayusculas de ~56 px del lienzo), centrada
  // en la cinta y no en el hueco del rollo; puede pasar por encima del cordon como en la maqueta
  float px = 84 * q.s;
  const float room = 500 * q.s, tw0 = f->CalcTextSizeA(px, FLT_MAX, 0, lugar.c_str()).x;
  if (tw0 > room) px *= room / tw0;
  const ImVec2 ts = f->CalcTextSizeA(px, FLT_MAX, 0, lugar.c_str());
  const ImVec2 c = q.At(x0 + w * 0.5f - 30, y0 + 70);   // POST2: centro de la cinta (maqueta: x ~1056, y ~98 del lienzo)
  const ImVec2 o(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
  const float d = px * 0.06f;
  for (int i = 0; i < 8; ++i) {
    const float an = 0.785398f * i;
    dl->AddText(f, px, ImVec2(o.x + std::cos(an) * d, o.y + std::sin(an) * d), IM_COL32(90, 35, 10, 255), lugar.c_str());
  }
  dl->AddText(f, px, o, IM_COL32(255, 200, 50, 255), lugar.c_str());
}

// Medallon de un combate: foto redonda del escenario dentro de un aro, bolita con el numero arriba a la derecha y el
// rival debajo. estado: 0 por jugar, 1 actual, 2 hecho, 3 bloqueado. Con las piezas de mod heroe/nodo_*, su aro.
constexpr float kNodoD = 120;   // diametro del medallon (maqueta: 95 px a 1280)
void NodoV2(ImDrawList* dl, const S::Area& q, float t, float cx, float cy, int registro, int n, int estado, bool jefe,
            const std::string& nombre) {
  const char* aro = estado == 1 ? "heroe/nodo_actual" : estado == 2 ? "heroe/nodo_hecho" : estado == 3 ? "heroe/nodo_bloqueado"
                    : jefe ? "heroe/nodo_jefe" : "heroe/nodo_pendiente";
  const S::Pic pa = S::Img(aro);
  const float din = pa ? kNodoD * 180.0f / 256.0f : kNodoD - 10;   // hueco del aro de mod: 180 de 256 px (encargo)
  const ImVec2 c = q.At(cx, cy);
  dl->AddCircleFilled(c, din * 0.5f * q.s, IM_COL32(12, 24, 38, 255), 40);
  if (const S::Pic f = PiezaEsc("heroe_esc_", registro)) {
    const float r = din * 0.5f / 0.86f;   // la foto del selector llena ~86 % de su textura (borde transparente)
    S::DrawPic(dl, f, q.At(cx - r, cy - r), q.At(cx + r, cy + r), ImVec4(0, 0, 0, 0),
               estado == 3 ? IM_COL32(105, 110, 125, 255) : IM_COL32_WHITE);
  } else {
    v2::Texto(dl, q, cx, cy, din * 0.5f, "?", 1, 0, false, estado == 3);
  }
  if (pa) {
    S::DrawPic(dl, pa, q.At(cx - kNodoD * 0.5f, cy - kNodoD * 0.5f), q.At(cx + kNodoD * 0.5f, cy + kNodoD * 0.5f));
  } else if (estado == 1) {   // el actual: aro amarillo que late y un halo claro
    dl->AddCircle(c, (din * 0.5f + 12) * q.s, IM_COL32(255, 240, 120, 80), 48, 6 * q.s);
    dl->AddCircle(c, (din * 0.5f + 4 + 1.5f * std::sin(t * 6)) * q.s, IM_COL32(255, 230, 60, 255), 48, 6 * q.s);
  } else {
    const ImU32 col = estado == 3 ? IM_COL32(90, 100, 115, 230) : estado == 2 || jefe ? IM_COL32(235, 170, 50, 240)
                                                                                       : IM_COL32(20, 30, 42, 235);
    dl->AddCircle(c, (din * 0.5f + 2) * q.s, col, 48, 3.5f * q.s);
  }
  if (estado == 2 && !pa) {   // hecho: visto bueno abajo a la izquierda
    const ImVec2 v = q.At(cx - din * 0.36f, cy + din * 0.34f);
    dl->AddCircleFilled(v, 13 * q.s, IM_COL32(20, 120, 40, 255), 20);
    dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(v.x - 6 * q.s, v.y), ImVec2(v.x - 1 * q.s, v.y + 5 * q.s),
                                          ImVec2(v.x + 7 * q.s, v.y - 6 * q.s)}.data(),
                    3, IM_COL32_WHITE, 0, 3 * q.s);
  }
  BolitaV2(dl, q, cx + kNodoD * 0.36f, cy - kNodoD * 0.36f, 36, n, estado == 3);
  v2::Texto(dl, q, cx, cy + kNodoD * 0.5f + 18, v2::kLetraMin, nombre, 1, 0, false, estado == 3, 220);
}

// Linea de puntos amarilla entre dos medallones (de borde a borde). Con heroe/ruta_punto, sus puntos.
void RutaV2(ImDrawList* dl, const S::Area& q, ImVec2 a0, ImVec2 a1) {
  const float dx = a1.x - a0.x, dy = a1.y - a0.y, l = std::sqrt(dx * dx + dy * dy), m = kNodoD * 0.5f + 6;
  if (l < 2 * m + 10) return;
  const float ux = dx / l, uy = dy / l;
  const S::Pic pt = S::Img("heroe/ruta_punto");
  constexpr float paso = 20;
  for (float s = m; s + 10 <= l - m; s += paso) {
    const float x = a0.x + ux * s, y = a0.y + uy * s;
    if (pt) S::DrawPic(dl, pt, q.At(x - 7, y - 7), q.At(x + 7, y + 7));
    else dl->AddLine(q.At(x, y), q.At(x + ux * 11, y + uy * 11), IM_COL32(255, 232, 120, 240), 5 * q.s);
  }
}

// Marca del jugador sobre el medallon actual: heroe/marcador_heroe si existe; si no, una punta amarilla que baja y sube.
void MarcadorV2(ImDrawList* dl, const S::Area& q, float t, float cx, float cy) {
  const float y = cy - kNodoD * 0.5f - 34 + 5 * std::sin(t * 4);
  if (const S::Pic p = S::Img("heroe/marcador_heroe")) {
    S::DrawPic(dl, p, q.At(cx - 30, y - 30), q.At(cx + 30, y + 30));
    return;
  }
  const ImVec2 a = q.At(cx - 20, y - 16), b = q.At(cx + 20, y - 16), c = q.At(cx, y + 16);
  dl->AddTriangleFilled(ImVec2(a.x - 3 * q.s, a.y - 3 * q.s), ImVec2(b.x + 3 * q.s, b.y - 3 * q.s), ImVec2(c.x, c.y + 4 * q.s),
                        IM_COL32(60, 30, 0, 255));
  dl->AddTriangleFilled(a, b, c, IM_COL32(255, 210, 40, 255));
  dl->AddTriangleFilled(a, ImVec2((a.x + b.x) * 0.5f, a.y), c, IM_COL32(255, 240, 140, 255));
}

// Sitios de los medallones (px del lienzo): el camino de la maqueta (Saibaman, Recoome, Ginyu) y, para sagas mas largas,
// hacia arriba. ponytail: 6 sitios fijos; una saga con mas combates ensena los 6 primeros (las de hoy tienen 3).
constexpr float kNodos[][2] = {{690, 600}, {1040, 712}, {1250, 556}, {1130, 395}, {870, 375}, {610, 420}};

// caja = (kMx0, kMy0, kMx1, kMy1) del mapa de siempre (solo para el terreno de reserva); cur = g_mapa_cur.
void DibujarMapaV2(ImDrawList* dl, const S::Area& a, float t, int total, int hechas, int cur, ImVec4 caja, const std::string& aviso) {
  const H::rutas::EstadoMapa st = H::rutas::CalcularEstadoMapa(
      H::EtapaRuta(), total, H::VueltaRuta(), H::RutaCompletada(), H::Saga(),
      H::ColeccionVueltasOn() && H::Rutas());
  const int sig = st.sig;
  const int vuelta = st.vuelta_idx;
  const bool abierta = (cur <= sig || cur < hechas || vuelta > 0) && cur < H::kSagasJugables;   // la maqueta: lo que aun no se alcanza, "???"
  const auto combates = H::CombatesDeSaga(cur);
  const int lugar = combates.empty() ? -1 : combates.back().second;   // el lugar de la saga: el de su ultimo combate
  const S::Area q = v2::Lienzo(a);
  if (!FondoMapaV2(dl, a, lugar)) {   // reserva: el terreno de siempre (mar e islas por codigo) a toda la pantalla
    FondoSinAnfitrion(dl, a);
    constexpr float K = 1.29f, MX = 410, MY = 100;
    S::Area qm = q;
    qm.s = q.s * K;
    qm.p0 = q.At(MX - caja.x * K, MY - caja.y * K);
    const float ys0 = qm.At(0, caja.y + 12).y, ys1 = qm.At(0, caja.w - 12).y;
    auto col = [&](float y) {
      const float f = (y - ys0) / std::max(1.0f, ys1 - ys0);
      auto c = [&](float v0, float v1) { return int(std::clamp(v0 + (v1 - v0) * f, 0.0f, 255.0f)); };
      return IM_COL32(c(30, 10), c(90, 40), c(170, 110), 255);
    };
    dl->AddRectFilledMultiColor(q.p0, ImVec2(q.p1.x, ys0), col(q.p0.y), col(q.p0.y), col(ys0), col(ys0));
    dl->AddRectFilledMultiColor(ImVec2(q.p0.x, ys0), ImVec2(q.p1.x, ys1), col(ys0), col(ys0), col(ys1), col(ys1));
    dl->AddRectFilledMultiColor(ImVec2(q.p0.x, ys1), q.p1, col(ys1), col(ys1), col(q.p1.y), col(q.p1.y));
    Tierra(dl, qm);
  }
  // combates de la saga elegida: hecha = todos hechos; la siguiente = el 1 es el actual (la saga se juega entera desde
  // el primero); el resto, bloqueados
  const int n = std::min(int(combates.size()), int(std::size(kNodos)));
  const int actual = cur < hechas ? n - 1 : abierta ? 0 : -1;   // donde esta el jugador (marca)
  for (int k = 0; k + 1 < n; ++k) RutaV2(dl, q, ImVec2(kNodos[k][0], kNodos[k][1]), ImVec2(kNodos[k + 1][0], kNodos[k + 1][1]));
  for (int k = 0; k < n; ++k) {
    const auto& [rival, esc] = combates[size_t(k)];
    const int estado = cur < hechas ? 2 : !abierta ? 3 : k == 0 ? 1 : 0;
    const std::string nombre = abierta ? (rival >= 0 ? M::Nombre(rival) : std::string("?")) : std::string("???");
    NodoV2(dl, q, t, kNodos[k][0], kNodos[k][1], esc, k + 1, estado, k == n - 1, nombre);
  }
  // POST2: el avatar del centro de la maqueta 04 (alli la nave de DU; MAQUETAS.md: "en el juego, el del heroe"): el modelo
  // del heroe (la misma vista que el creador) sobre el camino, pies en y 556 como la nave, sin peana
  {
    // PM-37: en Namek (rutas) el avatar es la nave de Freezer (ID 27, traje 2), como el de DU y la maqueta 04
    VistaElegida v;
    if (dbz3::heroe::NaveEnMapa()) {
      const double ahora = dbz3::RelojT(ImGui::GetTime());
      v.p = Vista(27, 2, ahora);   // si falta, la genera (vista_heroe.py --solo 27_2)
      v.m3 = Malla3D(27, 2, ahora);
    }
    if (!v.m3 && !v.p) v = ElegirVista();
    constexpr float alto = 330, ancho = alto * 342.0f / 464.0f, cx = 780, pies = 556, y0 = pies - 0.94f * alto;
    bool pintada = false;
    if (v.m3) pintada = Pintar3D(dl, *v.m3, q.At(cx - ancho * 0.5f, y0), q.At(cx + ancho * 0.5f, y0 + alto), g_giro * 6.2831853f / 8.0f);
    if (!pintada) {
      const S::Pic tira = v.p ? v.p : v.m3 && !CuerpoUT() ? Vista(H::Cuerpo(), H::Traje(), dbz3::RelojT(ImGui::GetTime())) : S::Pic{};
      if (tira) PintarTira(dl, q, tira, cx - 125, pies - 300, cx + 125, pies);
      else Cara(dl, q, CuerpoUT() ? H::EstiloId() : H::Cuerpo(), cx, pies - 150, 140);
    }
  }
  if (actual >= 0 && actual < n) MarcadorV2(dl, q, t, kNodos[actual][0], kNodos[actual][1]);
  // encima del terreno: titulo y pergamino del lugar
  v2::Titulo(dl, q, (vuelta > 0 ? std::string(T::T("Mapa del mundo", "World map")) + " - " + T::T("Vuelta ", "Lap ") + std::to_string(vuelta + 1)
                                : std::string(T::T("Mapa del mundo", "World map"))).c_str());
  // POST2: el pergamino del lugar siempre (en la saga 2 bloqueada faltaba): "???" mientras no se alcanza, como los nodos
  CartelLugarV2(dl, q, abierta && lugar >= 0 ? dbz3::escenarios::Nombre(lugar) : std::string("???"));
  {   // lista de sagas a la izquierda: bolita, nombre ("???" si aun no se alcanza), esfera si esta superada
    constexpr float x0 = 22, x1 = 425, y0 = 152;
    const float fila = std::min(57.0f, 456.0f / float(std::max(1, total)));
    v2::Panel(dl, q, x0, y0, x1, y0 + 30 + fila * float(total));
    for (int i = 0; i < total; ++i) {
      const float cy = y0 + 15 + fila * (float(i) + 0.5f);
      const bool sel = i == cur, ab = (i <= sig || i < hechas || vuelta > 0) && i < H::kSagasJugables;
      if (sel) v2::Franja(dl, q, x0 + 14, cy - fila * 0.5f + 2, x1 - 14, cy + fila * 0.5f - 2, t);
      BolitaV2(dl, q, x0 + 40, cy, fila * 0.6f, i + 1, !ab);
      // la maqueta: la elegida en cian, las demas en blanco, "???" apagado
      v2::Texto(dl, q, x0 + 72, cy, v2::kLetraMin + 2, ab ? std::string(H::NombreSaga(i)) : std::string("???"), 0, sel ? 1 : 0, sel,
                !ab, x1 - x0 - 130);
      if (i < hechas) Esfera(dl, q.At(x1 - 34, cy), fila * 0.22f * q.s, 1, 255);
    }
  }
  RadarV2(dl, q, t, __builtin_popcount(unsigned(H::Esferas())));
  {   // rotulo de abajo a la izquierda (maqueta): "Combate 1: Saibaman - Llanuras" / "Maestro de la saga: Goku" + estado
    constexpr float x0 = 18, x1 = 916, y0 = 752, y1 = 836;
    if (!aviso.empty()) {   // el aviso de A (bloqueada, completada, en desarrollo) encima, borde naranja
      v2::Caja(dl, q, x0, y0 - 50, x1, y0 - 8);
      dl->AddRect(q.At(x0, y0 - 50), q.At(x1, y0 - 8), IM_COL32(240, 150, 40, 230), 14 * q.s, 0, 2.5f * q.s);
      v2::Texto(dl, q, x0 + 24, y0 - 29, v2::kLetraPeque, aviso, 0, 0, false, false, x1 - x0 - 48);
    }
    v2::Caja(dl, q, x0, y0, x1, y1, 205);
    std::string linea = Num(cur + 1) + ". " + (abierta ? std::string(H::NombreSaga(cur)) : std::string("???"));
    if (abierta && actual >= 0 && actual < n) {
      const auto& [rival, esc] = combates[size_t(actual)];
      linea = std::string(T::T("Combate ", "Battle ")) + Num(actual + 1) + ": " + (rival >= 0 ? M::Nombre(rival) : std::string("?")) +
              " - " + dbz3::escenarios::Nombre(esc);
    }
    const char* estado = (st.nueva_vuelta && cur == sig) ? T::T("Nueva vuelta", "New lap")
                         : cur < hechas ? T::T("Saga completada", "Saga cleared")
                         : cur >= H::kSagasJugables ? T::T("En desarrollo", "In development")
                         : cur > sig ? T::T("Bloqueada", "Locked") : nullptr;   // la siguiente: A Empezar ya va en la barra
    v2::Texto(dl, q, x0 + 22, y0 + 27, v2::kLetraFila - 4, linea, 0, 2, false, false, x1 - x0 - (estado ? 300 : 44));
    if (estado) v2::Texto(dl, q, x1 - 22, y0 + 27, v2::kLetraMin, estado, 2, 0, false, true, 250);
    if (abierta) {
      if (const int m = H::MaestroQueEntrena(cur); m >= 0)
        v2::Texto(dl, q, x0 + 22, y0 + 62, v2::kLetraMin, std::string(T::T("Maestro de la saga: ", "Saga master: ")) + M::Nombre(m), 0,
                  0, false, true, x1 - x0 - 44);
    }
  }
  if (cur == sig && cur < H::kSagasJugables) {
    const char* accion_a = st.nueva_vuelta ? T::T("Nueva vuelta", "New lap") : T::T("Empezar", "Start");
    v2::BarraAyuda(dl, q, {{"A", accion_a}, {"LB", T::T("Saga", "Saga")}, {"B", T::T("Atrás", "Back")}}, 960);
  }
  else
    v2::BarraAyuda(dl, q, {{"LB", T::T("Saga", "Saga")}, {"B", T::T("Atrás", "Back")}}, 960);
}

}  // namespace
