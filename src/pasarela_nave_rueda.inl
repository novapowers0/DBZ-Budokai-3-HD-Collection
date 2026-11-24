// Rueda de personajes y figuras 3D de la Nave de Babidi (B2), extraido de pasarela_nave.cpp.
// Movimiento textual: mismo espacio de nombres (dbz3::pasarela::nave, anonimo) y mismo orden de
// inicializacion. Incluido en el sitio exacto del bloque; el padre conserva la burbuja de escenarios
// (DibBurbuja) y el resultado/premios. Reutiliza Capa/Tex/Anim2/PoseAma2/LienzoDe/Fig/Pag/g_sel de la TU.
// ===================================================================================================================
// A2nave (studies 5): rueda de personajes ("Select Characters"), menu Normal/Custom/Edit Skills y burbuja de escenarios
// ("Stage Selection") NATIVAS de B2, leidas de la ISO del jugador en marcha (nunca se reparten):
//   rueda  = #AMA DATA_CMN 901 + #AMT DATA_<idioma> 1127 (77 texturas: palabras del menu, 30 caras en 6 tiras de 5, 30
//            nombres, circulos 1P/2P, rotulo 1P/2P/COM, flechas);  burbuja = #AMA DATA_CMN 897 + #AMT DATA_CMN 898.
// Las dos son un anillo 3D (FUN_002A9950 / FUN_002AF4A0) visto de lado: los puntos de un circulo de radio R, girado
// `cab` (cabeceo) y `roll` (giro del anillo), trasladado a (x0, y0, z0). Medido por PINE en PCSX2 (anillo vivo, 30 + 10
// puntos) y con la formula del ELF; cuadra al decimal:
//     v  = (R sin a, -R cos a sin cab + R0 sin th0, R cos a cos cab - R0 cos th0)     a = 2 pi (arco i / n + roll) / 65536
//     x = x0 + vx - cizalla vz,  y = y0 - vy,  z = z0 + zs vz          (zs = -10; la rueda tiene cizalla -1, la burbuja 0)
//   Rueda  : x0 161, y0 346, z0 1199, R 180, cab 0x3000 (67,5 grados), arco 0x10000, cizalla -1, giro de 10 cuadros
//            (5 con L1/R1), entrada de 60 cuadros: R 600 -> 180, cab -0x4000 -> 0x3000, roll -8 f^2.
//   Burbuja: x0 160, y0 360, z0 1199, R 180, cab 0x1800 (33,75 grados), arco 0xA000 (la burbuja NO da la vuelta),
//            giro de 10 cuadros, entrada de 45 cuadros (mismas formulas).
//   Escala de cada cara (FUN_002A97C0): 1 - (z - z0) 0,0002 y 1,2 la elegida; de cada casilla (FUN_002AF360):
//            1 - (z - z0) 0,0001. Se dibuja de lejos (z grande) a cerca.
// ===================================================================================================================
// Una capa dibujable (los objetos se ordenan por z: grande = lejos = primero)
struct Capa {
  float z;
  ImTextureID tex;
  ImVec2 p[4], uv[4];
  ImU32 col;
  void (*fn)(ImDrawList*) = nullptr;   // el modelo 3D grande (se pinta entre las caras lejanas y las cercanas)
  bool circ = false;   // cara de B3 recortada en el aro de B2 (centro p[0], radio p[1].x, grosor del aro p[1].y)
};
std::vector<Capa> g_capas;
void VaciarCapas(ImDrawList* dl) {
  std::stable_sort(g_capas.begin(), g_capas.end(), [](const Capa& a, const Capa& b) { return a.z > b.z; });
  for (const Capa& c : g_capas) {
    if (c.fn) {
      c.fn(dl);
      continue;
    }
    if (!c.circ) {
      dl->AddImageQuad(c.tex, c.p[0], c.p[1], c.p[2], c.p[3], c.uv[0], c.uv[1], c.uv[2], c.uv[3], c.col);
      continue;
    }
    const ImVec2 ce = c.p[0];
    const float r = c.p[1].x, g = c.p[1].y;
    dl->AddCircleFilled(ce, r, IM_COL32(20, 40, 30, c.col >> 24), 40);
    dl->AddImageRounded(c.tex, ImVec2(ce.x - r, ce.y - r), ImVec2(ce.x + r, ce.y + r), c.uv[0], c.uv[2], c.col, r);
    dl->AddCircle(ce, r + g * 0.5f, IM_COL32(195, 85, 48, c.col >> 24), 40, g);        // aro naranja medido en las caras de B2
    dl->AddCircle(ce, r + g * 1.1f, IM_COL32(40, 18, 10, c.col >> 24), 40, g * 0.5f);   // y su filo oscuro
  }
  g_capas.clear();
}
struct Pose2 {
  float x = 0, y = 0, z = 0, sx = 1, sy = 1, rot = 0, a = 1;
  uint32_t niebla = 0x01000000;
  float m[3] = {1, 1, 1};
};
// Pose de la animacion en el cuadro c: claves (lineal) y alfa de la linea de piezas; bucle = vuelve al principio.
Pose2 PoseAma2(const Anim2& A, float c, bool bucle) {
  Pose2 q;
  if (A.k.empty()) return q;
  const float kd = A.k.back().t, pd = A.p.empty() ? 0.0f : A.p.back().t;
  float ck = c, cp = c;
  if (bucle) {
    ck = kd > 0 ? std::fmod(std::max(0.0f, c), kd) : 0.0f;
    cp = pd > 0 ? std::fmod(std::max(0.0f, c), pd) : 0.0f;
  } else {
    ck = std::min(c, kd);
    cp = std::min(c, pd);
  }
  const Clave2* k0 = &A.k.front();
  const Clave2* k1 = k0;
  for (size_t i = 1; i < A.k.size(); ++i) {
    k1 = &A.k[i];
    if (ck < k1->t) break;
    k0 = k1;
  }
  const float f = k1->t > k0->t ? std::clamp((ck - k0->t) / (k1->t - k0->t), 0.0f, 1.0f) : 1.0f;
  auto m = [&](float a, float b) { return a + (b - a) * f; };
  q.x = m(k0->x, k1->x);
  q.y = m(k0->y, k1->y);
  q.z = m(k0->z, k1->z);
  q.sx = m(k0->sx, k1->sx);
  q.sy = m(k0->sy, k1->sy);
  q.rot = m(k0->rot, k1->rot);
  q.a = m(k0->a, k1->a);
  float pa = 1.0f;
  if (!A.p.empty()) {
    pa = A.p.front().alpha;
    q.niebla = A.p.front().niebla;
    for (int j = 0; j < 3; ++j) q.m[j] = A.p.front().m[j];
    for (size_t i = 0; i < A.p.size(); ++i) {
      if (cp < A.p[i].t) break;
      pa = A.p[i].alpha;
      q.niebla = A.p[i].niebla;
      for (int j = 0; j < 3; ++j) q.m[j] = A.p[i].m[j];
      if (i + 1 < A.p.size() && A.p[i + 1].t > A.p[i].t) {
        const float g = std::clamp((cp - A.p[i].t) / (A.p[i + 1].t - A.p[i].t), 0.0f, 1.0f);
        pa += (A.p[i + 1].alpha - A.p[i].alpha) * g;
        q.niebla = 0;
        for (int ch = 0; ch < 4; ++ch) {
          const int de = int((A.p[i].niebla >> (8 * ch)) & 255), hasta = int((A.p[i + 1].niebla >> (8 * ch)) & 255);
          q.niebla |= uint32_t(de + int(float(hasta - de) * g)) << (8 * ch);
        }
        for (int j = 0; j < 3; ++j) q.m[j] += (A.p[i + 1].m[j] - A.p[i].m[j]) * g;
      }
    }
  }
  q.a *= pa;
  return q;
}
ImTextureID TexturaBurbuja(int id, uint32_t color) {
  if (id < 0 || id >= int(g_burb_cpu.size()) || !g_burb_cpu[size_t(id)]) return 0;
  Tex& tx = g_burb_niebla[size_t(id)];
  if (!tx.t || g_burb_color[size_t(id)] != color) {
    Imagen im = g_burb_cpu[size_t(id)];
    const float f = float(color >> 24) / 255.0f;
    // GS FOG: mezcla RGB con FOGCOL, conservando el alfa de la textura. Una
    // capa blanca translucida por encima NO equivale a esto en los bordes.
    for (size_t k = 0; k + 3 < im.rgba.size(); k += 4) for (int ch = 0; ch < 3; ++ch)
      im.rgba[k + ch] = uint8_t(float(im.rgba[k + ch]) * (1 - f) + float((color >> (8 * ch)) & 255) * f);
    if (auto* dr = dbz3::launcher::PreviewDrawer()) {
      tx.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
      tx.w = float(im.w); tx.h = float(im.h);
      g_burb_color[size_t(id)] = color;
    }
  }
  return reinterpret_cast<ImTextureID>(tx.t.get());
}
// Pone la animacion k de `set` en el cuadro c. pos/esc/z/alfa opcionales: pos = (x, y) final en lugar del de las claves
// (si hay), `d` = desplazamiento, esc = escala extra, z < 0: el de las claves.
void Spr2(const Lienzo& L, const Ama2& set, int k, float c, bool bucle, ImVec2 d = ImVec2(0, 0), const ImVec2* pos = nullptr,
          float esc = 1.0f, float alfa = 1.0f, float z = -1e9f, ImU32 tinte = IM_COL32_WHITE) {
  if (k < 0 || k >= int(set.a.size())) return;
  const Anim2& A = set.a[size_t(k)];
  if (A.p.empty()) return;
  const Pose2 q = PoseAma2(A, c, bucle);
  const Pz2& p = A.p.front();
  const auto& texturas = &set == &g_cab_seleccion ? g_pz : set.tex;
  if (p.tex < 0 || p.tex >= int(texturas.size()) || !texturas[size_t(p.tex)].t) return;
  const Tex& t = texturas[size_t(p.tex)];
  const float al = q.a * alfa;
  if (al <= 0.001f || q.sx == 0 || q.sy == 0) return;
  const float sx = q.sx * esc, sy = q.sy * esc;
  const float x = (pos ? pos->x : q.x) + d.x, y = (pos ? pos->y : q.y) + d.y;
  Capa cp;
  cp.z = z > -1e8f ? z : q.z;
  cp.tex = reinterpret_cast<ImTextureID>(t.t.get());
  if (&set == &g_burb) if (auto tex = TexturaBurbuja(p.tex, q.niebla)) cp.tex = tex;
  cp.p[0] = L.P(x + A.x0 * sx, y + A.y0 * sy);
  cp.p[1] = L.P(x + (A.x1 + 1) * sx, y + A.y0 * sy);
  cp.p[2] = L.P(x + (A.x1 + 1) * sx, y + (A.y1 + 1) * sy);
  cp.p[3] = L.P(x + A.x0 * sx, y + (A.y1 + 1) * sy);
  cp.uv[0] = ImVec2(p.u0 / t.w, p.v0 / t.h);
  cp.uv[1] = ImVec2(p.u1 / t.w, p.v0 / t.h);
  cp.uv[2] = ImVec2(p.u1 / t.w, p.v1 / t.h);
  cp.uv[3] = ImVec2(p.u0 / t.w, p.v1 / t.h);
  const ImU32 a8 = ImU32(std::clamp(al, 0.0f, 1.0f) * float(tinte >> 24));
  auto cm = [&](ImU32 ch, float m) { return ImU32(std::clamp(float(ch) * m, 0.0f, 255.0f)); };
  cp.col = cm(tinte & 0xFF, q.m[0]) | (cm((tinte >> 8) & 0xFF, q.m[1]) << 8) | (cm((tinte >> 16) & 0xFF, q.m[2]) << 16) | (a8 << 24);
  g_capas.push_back(cp);
}
// Duracion de una animacion para la tarea (entrada -> reposo)
float DurAma2(const Ama2& set, int k) { return k >= 0 && k < int(set.a.size()) ? set.a[size_t(k)].dur : 0.0f; }
// Objeto con entrada `e` (una vez) y reposo `r` (en bucle) desde el cuadro `desde`.
void Obj2(const Lienzo& L, const Ama2& set, int e, int r, float c, ImVec2 d = ImVec2(0, 0), const ImVec2* pos = nullptr,
          float esc = 1.0f, float alfa = 1.0f, float z = -1e9f) {
  if (c < 0) return;
  const float de = e >= 0 ? DurAma2(set, e) : 0.0f;
  if (e >= 0 && c < de) return Spr2(L, set, e, c, false, d, pos, esc, alfa, z);
  Spr2(L, set, r, c - de, true, d, pos, esc, alfa, z);
}

// ---- Anillo 3D
struct Anillo {
  float x0, y0, z0, zs, cizalla, R0, th0, arco;   // th0 en 1/65536 de vuelta
};
constexpr float kPi2 = 6.28318530718f;
void AnilloPos(const Anillo& r, float R, float cab /*1/65536*/, float ang /*1/65536*/, float& x, float& y, float& z) {
  const float a = ang * (kPi2 / 65536.0f), cb = cab * (kPi2 / 65536.0f), t0 = r.th0 * (kPi2 / 65536.0f);
  const float vx = R * std::sin(a);
  const float vy = -R * std::cos(a) * std::sin(cb) + r.R0 * std::sin(t0);
  const float vz = R * std::cos(a) * std::cos(cb) - r.R0 * std::cos(t0);
  x = r.x0 + vx - r.cizalla * vz;
  y = r.y0 - vy;
  z = r.z0 + vz * r.zs;
}
// Parametros de la entrada en el cuadro e (0..T): f = T - e cuadros por recorrer
struct AnilloEntrada {
  float R, cab, roll;
};
AnilloEntrada EntradaAnillo(float e, float T, float cab_fin) {
  const float f = std::max(0.0f, T - e);
  return {180.0f + f * (600.0f - 180.0f) / T, cab_fin + f * (-16384.0f - cab_fin) / T, -(8.0f * f * f)};
}
constexpr Anillo kAnilloRueda = {161, 346, 1199, -10, -1, 180, 0x3000, 65536};
constexpr Anillo kAnilloBurb = {160, 360, 1199, -10, 0, 180, 0x1800, 0xA000};
// Aro de las caras de B2 medido (F8, sin juego): la celda de cada cara (#AMA 901, anims 36..65) es de 96 x 96 y el aro
// naranja de la textura de caras (#AMT DATA_UK 1127, textura 18) queda, en coordenadas locales de la celda, centrado en
// (0; 0) con radio interior 23,8 y exterior 26,5 (grosor 2,7), medido por ajuste de circulo al arco visible.
// Antes el port dibujaba radio 21, grosor 5 y centro (-1; 8): el retrato de B3 quedaba 2,8 px mas pequeno y 8 px mas bajo.
constexpr float kCaraCelda = 96.0f, kAroCx = 0.0f, kAroCy = 0.0f, kAroR = 23.8f, kAroG = 2.7f;



// ---- Estado de la rueda y de la burbuja (el giro avanza por cuadros de 60 Hz del dibujo)
struct Giro2 {
  float pos = 0;          // posicion mostrada (en casillas)
  int obj = 0;            // casilla elegida
  float c = -1;           // cuadros desde que empezo el giro (< 0: parado)
  float dur = 10, ini = 0, fin = 0;
  float t_ant = -1;       // reloj de la ultima actualizacion (s)
  bool Girando() const { return c >= 0; }
  void Mover(int a, int n, bool vuelta, float cuadros = 10) {   // a = +-1
    if (Girando()) return;
    int nuevo = obj + a;
    if (vuelta) nuevo = (nuevo % n + n) % n;
    else if (nuevo < 0 || nuevo >= n) return;
    ini = pos;
    fin = pos + float(a);
    obj = nuevo;
    c = 0;
    dur = cuadros;
  }
  void Saltar(int destino, int n, float cuadros = 10) {   // salto directo (por el camino corto en la rueda; recto en la burbuja)
    if (Girando() || destino == obj) return;
    ini = pos;
    fin = float(destino);
    obj = destino;
    c = 0;
    dur = cuadros;
    (void)n;
  }
  void Paso(float dt_cuadros) {
    if (!Girando()) return;
    c += dt_cuadros;
    if (c >= dur) {
      pos = fin;
      c = -1;
    } else {
      pos = ini + (fin - ini) * (c / dur);
    }
  }
  void Fijar(int i) {
    pos = ini = fin = float(i);
    obj = i;
    c = -1;
  }
};
Giro2 g_giro_rueda, g_giro_burb;
int g_traje_rueda = 0;
int TrajesDe(const Entrada& e) {
  if (e.b2 < 0) return 1;   // los extras conservan su selector de B3
  const auto& elf = ElfB2();
  const size_t o = 0x37AB10 - 0x100000 + 0x80 + size_t(e.b2);
  return o < elf.size() ? std::max(1, int(elf[o])) : 1;
}
void RestaurarRueda(int lado) {
  const auto& e = g_elegida[lado];
  g_pag = 0;
  int casilla = 0;
  bool hallada = false;
  for (size_t p = 0; p < g_pags.size() && !hallada; ++p)
    for (size_t i = 0; i < g_pags[p].size(); ++i)
      if (g_pags[p][i].b3 == e.id) { g_pag = int(p); casilla = int(i); hallada = true; break; }
  g_giro_rueda.Fijar(casilla);
  g_traje_rueda = casilla < int(Pag().size()) ? std::clamp(e.traje, 0, TrajesDe(Pag()[size_t(casilla)]) - 1) : 0;
  // IV excluye el personaje; las otras plantas solo la pareja personaje/traje.
  if (lado == 1 && casilla < int(Pag().size()) && Pag()[size_t(casilla)].b3 == g_j3) {
    if (g_prueba == 3) { g_giro_rueda.Fijar((casilla + 1) % (int(Pag().size()) + 1)); g_traje_rueda = 0; }
    else if (g_traje_rueda == g_elegida[0].traje)
      g_traje_rueda = (g_traje_rueda + 1) % TrajesDe(Pag()[size_t(casilla)]);
  }
  REXLOG_INFO("dbz3 nave: restaura {} id {} traje {} pagina {} casilla {}", lado ? "COM/2P" : "1P",
              g_giro_rueda.obj < int(Pag().size()) ? Pag()[size_t(g_giro_rueda.obj)].b3 : -1,
              g_traje_rueda, g_pag + 1, g_giro_rueda.obj);
}
float g_t_rueda = 0, g_t_esfera = 0, g_t_menu = 0;   // reloj (s) de la entrada de cada pantalla
int g_sitio_vis = 0;                                  // casilla de la burbuja (0..9; 9 = "?")
int g_menu_sel = 0;           // Normal / Custom / Edit Skills
int g_lista_capsulas = 0;     // FUN_00260CF0: 0 Normal, 1 Custom; Edit Skills abre su pantalla aparte.
float g_c_rueda = 0;          // cuadros desde que entro la rueda (el menu la deja puesta)
bool g_rueda_com = false;     // false: elige el jugador (1P), true: el rival (COM)

// solP: PCSX2 2.9.103/PINE, SLES-51839: confirmacion de personaje 52 campos PAL
// (60 cuadros de animacion + despacho), burbuja 67 campos (AMA 19: 81), salida
// 25 campos (30). Antes las tres rutas cambiaban de pantalla en el mismo dibujo.
enum class SelFase { quieta, menuConfirma, ruedaConfirma, ruedaSale, burbujaConfirma, burbujaSale, menuSale };
struct SelTransicion {
  SelFase fase = SelFase::quieta;
  float t0 = 0, c = 0;
  int destino = kPrueba;
  bool combate = false;
  struct Punto { float x, y, z, vx, vy; };
  std::vector<Punto> puntos;
} g_sel;
float g_burb_imagen_t = 0;
float g_fig_flash = 0, g_fig_salida_x = 0;
bool g_rueda_continua = false;
bool SaliendoRueda() { return g_sel.fase == SelFase::ruedaSale; }
bool SaliendoBurbuja() { return g_sel.fase == SelFase::burbujaSale; }
void SalidaSeleccion(bool burbuja, int destino, bool combate, float t) {
  g_sel.fase = burbuja ? SelFase::burbujaSale : SelFase::ruedaSale;
  g_sel.t0 = t; g_sel.c = 0; g_sel.destino = destino; g_sel.combate = combate;
  g_sel.puntos.clear();
  const auto& r = burbuja ? kAnilloBurb : kAnilloRueda;
  const auto& g = burbuja ? g_giro_burb : g_giro_rueda;
  const int n = burbuja ? int(g_cel.size()) : int(Pag().size()) + 1;
  for (int i = 0; i < n; ++i) {
    float d = float(i) - g.pos;
    if (!burbuja) d -= float(n) * std::round(d / float(n));
    SelTransicion::Punto p;
    AnilloPos(r, r.R0, r.th0, r.arco * d / float(n), p.x, p.y, p.z);
    const float ang = float(std::rand() % 65535) * (kPi2 / 65536.0f);
    // FUN_002A9F60 / FUN_002AF720: velocidad 26 px/cuadro (31,2 en PAL).
    p.vx = 26.0f * std::cos(ang); p.vy = 26.0f * std::sin(ang);
    g_sel.puntos.push_back(p);
  }
  REXLOG_INFO("dbz3 nave: transicion {} salida 30 cuadros", burbuja ? "burbuja" : "rueda");
}


// ---- El modelo 3D grande del luchador elegido (A2nave). B2 (FUN_002AE010/002AE9B0, b2.c): cuerpo = #AMB[#AMO,#AMT] de la tabla
// de personajes del ELF (0x389540 + id * 0x2C8: +0x74 cuerpos por traje, +0x114 cabeza/pelo, +0x30/+0x32 cuantos por traje);
// la pose sale del banco fijo DATA_CMN 931 (#AMM, nodo 0x3A3): cuadro 16 * DAT_0037B8B0[id] de su animacion 0 [H: las
// pistas se buscan por nombre de hueso en B2 y aqui por indice]. Luces y camara: tabla 0x37ABD0. Se pinta entre las caras
// lejanas (z > 1500) y las cercanas, como B2 (FUN_002AA460 / FUN_002AA3D0).
struct Fig {
  ps2::Modelo m, cab;
  std::vector<ps2::Tex> tx;
  ps2::Anim an;
  std::string prefijo;   // B3M: prefijo de los huesos (GOK, VGT...)
  std::vector<std::string> suf;   // B3M: sufijo del nombre de cada hueso del cuerpo (GOK_RARM1 -> RARM1), para pasar la animacion a un modelo de B3
  float alto = 0, cuadro = 0;
  int hueso = -1;
  bool ok = false;
};
std::map<int, Fig> g_figs;
std::vector<uint8_t> g_amm931;
int g_fig_id = -1;
ImVec2 g_fig_pie(0, 0);
float g_fig_px = 0, g_fig_alto_ref = 0;
const uint8_t kPose37B8B0[40] = {1, 1, 0, 3, 3, 0, 2, 1, 0, 3, 2, 2, 3, 1, 1, 5, 2, 3, 4, 1, 1, 2, 1, 1, 1, 3, 0, 3, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 0};   // [36] el ELF trae 6 (T-pose en el cuadro 0, medido en la hoja 31/10); el id 36 no es del plantel de B2: 1, la guardia comun
// prueba DBZ3_SEL_REL: 0 absoluto (por defecto), 1 giro relativo al reposo, 2 giro y posicion
int modo_rel() { static const int m = std::getenv("DBZ3_SEL_REL") ? std::atoi(std::getenv("DBZ3_SEL_REL")) : 0; return m; }
std::vector<ps2::M34> EjesMundo(const ps2::Esqueleto& esq, const ps2::Anim& an, float t) {
  const size_t nb = esq.padre.size();
  static const char* fija = std::getenv("DBZ3_SEL_POSEFRAC");   // prueba: fotograma fijo (fraccion de la animacion)
  const float fc = fija ? std::atof(fija) * float(std::max(an.cuadros - 1, 1)) : std::fmod(t * 60.0f, float(std::max(an.cuadros, 1)));
  std::vector<ps2::M34> w(nb);
  for (size_t i = 0; i < nb; ++i) {
    ps2::M34 l = esq.local[i];
    if (i < an.ang.size() && !an.ang[i].empty()) {
      const auto& ks = an.ang[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && float(ks[ia + 1][0]) <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float fa = float(ks[ia][0]), fb = float(ks[ib][0]);
      const float u = fb > fa ? std::clamp((fc - fa) / (fb - fa), 0.0f, 1.0f) : 0.0f;
      auto ang = [&](int e) {
        const int d = int(int16_t(uint16_t(ks[ib][size_t(e)] - ks[ia][size_t(e)])));
        return (float(ks[ia][size_t(e)]) + u * float(d)) * 9.5873799e-5f;
      };
      const float x = ang(1), y = ang(2), z = ang(3);
      const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
      const float r[9] = {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, -sy, cy * sx, cy * cx};
      if (an.relativa && modo_rel() >= 1) {   // B2: giro de reposo por el de la clave (como Posar)
        const ps2::M34& rep = esq.local[i];
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = rep.m[f * 4] * r[k] + rep.m[f * 4 + 1] * r[3 + k] + rep.m[f * 4 + 2] * r[6 + k];
      } else {
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = r[f * 3 + k];
      }
    }
    if (i < an.pos.size() && !an.pos[i].empty()) {
      const auto& ks = an.pos[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && ks[ia + 1][0] <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float u = ks[ib][0] > ks[ia][0] ? std::clamp((fc - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0.0f, 1.0f) : 0.0f;
      float c[4];
      for (int e = 1; e < 4; ++e) c[e] = ks[ia][size_t(e)] + u * (ks[ib][size_t(e)] - ks[ia][size_t(e)]);
      if (an.relativa && modo_rel() >= 2) l.m[3] = esq.local[i].m[3] + c[1], l.m[7] = esq.local[i].m[7] + c[2], l.m[11] = esq.local[i].m[11] + c[3];
      else l.m[3] = c[1], l.m[7] = c[2], l.m[11] = c[3];
    }
    w[i] = esq.padre[i] >= 0 ? w[size_t(esq.padre[i])] * l : l;
  }
  return w;
}
// pbank/panim: banco #AMM de pose propio (panel comic: CMN 844 Saibaman anim 0, CMN 827 Goku anim 4; medido en PCSX2 en p2, modelo+0x45c / +0x41c);
// 0 = el banco de la rueda (CMN 931, anim por personaje).
const Fig& FigDe(int id_b2, int traje = 0, int pbank = 0, int panim = 0) {   // traje = `forma` de FUN_002AE9B0 (el Saibaman: su color)
  const int clave = id_b2 + 100 * traje + 10000 * pbank + 100000000 * panim;
  auto it = g_figs.find(clave);
  if (it != g_figs.end()) return it->second;
  Fig& f = g_figs[clave];
  const std::vector<uint8_t> elf = ElfB2();
  AfsPs2 cmn;
  if (elf.size() < 0x200000 || id_b2 < 0 || id_b2 > 40 || !cmn.Abrir(g_iso, "DATA_CMN.AFS")) return f;
  const size_t rec = 0x389540 - 0x100000 + 0x80 + size_t(id_b2) * 0x2C8;   // VA 0x100000 = offset 0x80 del ELF
  if (rec + 0x2C8 > elf.size()) return f;
  const int nc = int(elf[rec + 0x30]) | int(elf[rec + 0x31]) << 8, nh = int(elf[rec + 0x32]) | int(elf[rec + 0x33]) << 8;
  if (traje < 0 || (traje > 0 && (nc <= 0 || rec + 0x114 + 4 * size_t(traje * std::max(nh, 1)) + 4 > elf.size()))) return f;
  const uint32_t cuerpo = U32(elf, rec + 0x74 + 4 * size_t(traje * nc)), cabeza = U32(elf, rec + 0x114 + 4 * size_t(traje * nh));
  const bool con_cab = int8_t(elf[rec + 0x25B]) >= 0 && nh > 0 && cabeza > 0;
  if (cuerpo == 0) return f;
  const std::vector<uint8_t> amb = cmn.Entrada(cuerpo);
  f.m = ps2::CargarSuelto(HijoAmb(amb, 0), HijoAmb(amb, 1), true, true, true);
  if (!f.m.malla || !f.m.esq) return f;
  if (g_amm931.empty()) g_amm931 = cmn.Entrada(931);
  const std::vector<uint8_t> amm_pb = pbank > 0 ? cmn.Entrada(uint32_t(pbank)) : std::vector<uint8_t>();
  const std::vector<uint8_t>& amm = pbank > 0 ? amm_pb : g_amm931;
  {   // pose por NOMBRE de hueso, como B2 (FUN_0013F370: se compara el nombre desde el final hasta el "_"): la animacion
    // DAT_0037B8B0[id] de DATA_CMN 931 (7 animaciones sobre los 47 huesos FRZ_*) se aplica a los huesos del modelo con el mismo sufijo
    auto nombres = [](const std::vector<uint8_t>& b, size_t ini, size_t n) {
      std::vector<std::string> v;
      for (size_t i = 0; i < n && ini + 32 * (i + 1) <= b.size(); ++i) v.emplace_back(reinterpret_cast<const char*>(&b[ini + 32 * i]), strnlen(reinterpret_cast<const char*>(&b[ini + 32 * i]), 32));
      return v;
    };
    auto sufijo = [](const std::string& n) { const size_t k = n.rfind('_'); return k == std::string::npos ? n : n.substr(k + 1); };
    const std::vector<uint8_t> amo = HijoAmb(amb, 0);
    size_t ini = 0;
    for (size_t k = 0; k + 6 < amo.size(); ++k)
      if (std::memcmp(&amo[k], "_BODY", 5) == 0 && (amo[k + 5] == 0 || (amo[k + 5] == '_' && amo[k + 6] == '1' && amo[k + 7] == 0))) {   // B3M: Gohan = XGHL_BODY_1
        ini = k;
        while (ini > 0 && (std::isupper(amo[ini - 1]) || std::isdigit(amo[ini - 1]) || amo[ini - 1] == '_')) --ini;
        break;
      }
    const size_t nb = f.m.esq->padre.size();
    const uint32_t nt = amm.size() > 0x20 ? U32(amm, 0x18) : 0, off_n = amm.size() > 0x20 ? U32(amm, 0x1C) : 0;
    static const char* forzar = std::getenv("DBZ3_SEL_POSEIDX");   // prueba: misma animacion para todos
    const uint32_t pose = pbank > 0 ? uint32_t(panim) : forzar ? uint32_t(std::atoi(forzar)) : kPose37B8B0[std::clamp(id_b2, 0, 39)];
    const ps2::Anim base = ps2::LeerAnim(amm, pose, nt);
    REXLOG_INFO("dbz3 nave: pose id B2 {} anim {} cuadros {}", id_b2, pose, base.cuadros);
    if (ini > 0 && nt > 0 && base) {
      const std::vector<std::string> mn = nombres(amo, ini, nb), an = nombres(amm, off_n, nt);
      f.an.cuadros = base.cuadros;
      f.an.relativa = true;   // piel de B2 (ps2_modelo.h, frente R2)
      f.an.absoluta = true;   // MEDIDO en PCSX2: los giros de CMN 931 son absolutos (error 0.000 en los 28 huesos con pista), no sobre el reposo
      f.an.ang.resize(nb);
      f.an.pos.resize(nb);
      if (mn.size() > 1) { const size_t u = mn[1].find('_'); f.prefijo = u == std::string::npos ? mn[1] : mn[1].substr(0, u); }
      for (size_t j = 0; j < nb && j < mn.size(); ++j) {   // B3M: el sufijo de vista_b2.py (tras el primer '_' sin la X: GHL_BODY_1 -> BODY_1)
        const size_t u = mn[j].find('_');
        f.suf.push_back(u == std::string::npos ? mn[j] : mn[j].substr(u + 1));
      }
      int hechos = 0;
      for (size_t j = 0; j < nb && j < mn.size(); ++j)
        for (size_t t = 0; t < an.size(); ++t)
          if (!mn[j].empty() && sufijo(mn[j]) == sufijo(an[t])) {
            f.an.ang[j] = base.ang[t];
            f.an.pos[j] = base.pos[t];
            ++hechos;
            break;
          }
      REXLOG_INFO("dbz3 nave: pose de la rueda id B2 {}: animacion {} de CMN 931, {} de {} huesos con pista por nombre", id_b2, pose, hechos, nb);
    }
    f.cuadro = 0;
  }
  for (const auto& tr : f.m.malla.tris)
    for (const auto& v : tr.p) f.alto = std::max(f.alto, v.y);
  f.tx = ps2::Texturas(f.m);
  if (con_cab) {
    const std::vector<uint8_t> ac = cmn.Entrada(cabeza);
    f.cab = ps2::CargarSuelto(HijoAmb(ac, 0), HijoAmb(ac, 1), true, true, true);
    if (f.cab.esq)
      for (auto& x : ps2::Texturas(f.cab)) f.tx.push_back(std::move(x));
    const auto& esq = *f.m.esq;
    std::vector<ps2::M34> wb(esq.padre.size());
    float ymax = -1e9f;
    for (size_t b = 0; b < esq.padre.size(); ++b) {
      wb[b] = esq.padre[b] >= 0 ? wb[size_t(esq.padre[b])] * esq.local[b] : esq.local[b];
      if (wb[b].m[7] > ymax) ymax = wb[b].m[7], f.hueso = int(b);
    }
    if (f.hueso >= 0 && esq.padre[size_t(f.hueso)] >= 0) f.hueso = esq.padre[size_t(f.hueso)];
  }
  f.ok = true;
  REXLOG_INFO("dbz3 nave: modelo de la rueda: id B2 {} cuerpo {} ({} tri, {} huesos, alto {:.1f}) cabeza {} pose cuadro {}", id_b2, cuerpo,
              f.m.malla.tris.size(), f.m.esq->padre.size(), f.alto, con_cab ? int(cabeza) : -1, int(f.cuadro));
  return f;
}
// F8 (noche 10/11): encuadre medido en los DATOS de B2, sin juego. Apagado por defecto; con el encendido la rueda usa
// la escala de personaje 0x3892A0, la camara 0x37ABD0 y el aro nativo medido en #AMA 901 + #AMT 1127 (ver F8/medir_rueda_b2.py).
// Con el apagado queda exactamente lo entregado en 1.5.6.x.
REXCVAR_DEFINE_BOOL(dbz3_nave_rueda_datos, false, "DBZ3/Mods",
                    "Rueda de la nave: encuadre y aro calculados de los datos de B2 (escala, camara y aro de las caras).");
bool RuedaDatos() { return rex::cvar::GetFlagByName("dbz3_nave_rueda_datos") == "true"; }
// Foco de la camara de la rueda (FUN_002AE570): 5863,36 px por unidad a distancia 1; DBZ3_SEL_CAMFILE lo cambia en pruebas.
constexpr float kFocoRueda = 5863.36f;
// Distancia de la camara de la rueda de un personaje de B2 (registro 0x37ABD0 + 0x10 + id*0x2C, campo +16), con el
// estiramiento de pantalla de B2 (448/392) y en valor absoluto. -1 si no hay registro.
float DistanciaCamB2(int id) {
  const std::vector<uint8_t>& elf = ElfB2();
  const size_t row = size_t(0x37ABD0) - 0x100000 + 0x80 + 0x10 + size_t(id) * 0x2C;
  if (id < 0 || id > 0x3E || row + 0x2C > elf.size()) return -1.0f;
  float d;
  std::memcpy(&d, elf.data() + row + 16, 4);
  return std::fabs(d) * (448.0f / 392.0f);
}
// Alto (ymax - ymin) de los modelos de B2 de la rueda, para dar a un extra el tamano del de altura parecida.
struct AltoB2 { int id; float h; };
constexpr AltoB2 kAltosB2[] = {{0, 14.05f}, {2, 10.23f}, {28, 16.85f}, {24, 16.47f}, {33, 13.85f}, {6, 17.12f}, {4, 12.14f}, {8, 16.22f}, {7, 15.37f}, {21, 17.30f}, {35, 13.80f},
                               {32, 18.36f}, {29, 15.67f}, {30, 19.05f}, {31, 13.86f}, {25, 16.61f}, {20, 20.67f}, {18, 15.52f}, {17, 15.91f}, {16, 20.05f}, {15, 13.75f},
                               {11, 16.61f}, {12, 21.57f}, {9, 18.30f}, {10, 19.11f}, {3, 12.74f}, {5, 15.27f}, {27, 9.19f}, {26, 8.83f}};
// Factor de respaldo SIN el 0,93 fijo: el tamano que B2 daria a un modelo de esta altura con la camara real de los tres
// personajes de kAltosB2 de altura mas parecida (misma formula con la que PintaExtra elige su altura de pantalla).
float FacPorAltura(float alto_b2) {
  std::array<const AltoB2*, 3> cer{};
  for (auto& c : cer) {
    const AltoB2* mejor = nullptr;
    for (const AltoB2& r : kAltosB2) {
      if (std::find(cer.begin(), cer.end(), &r) != cer.end()) continue;
      if (!mejor || std::fabs(r.h - alto_b2) < std::fabs(mejor->h - alto_b2)) mejor = &r;
    }
    c = mejor;
  }
  float fac = 0;
  for (const AltoB2* c : cer) {
    const float dz = DistanciaCamB2(c->id);
    if (dz > 1.0f) fac += (kFocoRueda / dz) * (c->h + 1.1f) / (1.38f * 450.0f);
  }
  return fac > 0 ? fac / 3.0f : 1.0f;
}
// factor de tamano medido contra B2 (barrido de las 29 casillas de la pagina 1); el resto sale de FacPorAltura (datos)
float FacDe(int id, float alto_modelo) {
  switch (id) {
    case 0: return 0.78f;
    case 2: return 0.77f;
    case 28: return 0.99f;
    case 24: return 0.94f;
    case 33: return 0.93f;
    case 6: return 0.90f;
    case 4: return 1.00f;
    case 8: return 1.05f;
    case 7: return 0.89f;
    case 21: return 0.89f;
    case 35: return 0.91f;
    case 32: return 0.92f;
    case 29: return 0.95f;
    case 30: return 0.90f;
    case 31: return 0.94f;
    case 25: return 0.85f;
    case 20: return 0.86f;
    case 18: return 0.93f;
    case 17: return 0.91f;
    case 16: return 0.98f;
    case 15: return 0.70f;
    case 11: return 0.90f;
    case 12: return 1.01f;
    case 9: return 1.00f;
    case 10: return 0.97f;
    case 3: return 0.92f;
    case 5: return 1.02f;
    case 27: return 0.87f;
    case 26: return 0.77f;
  }
  return RuedaDatos() ? FacPorAltura(alto_modelo) : 0.93f;   // 0,93 solo con dbz3_nave_rueda_datos apagado (1.5.6.x)
}

// ---- Modelo 3D de B3 HD de los luchadores sin registro de B2 (extras, puertos, mods): malla HMV1 de vista_nave.py (la hace en
// segundo plano con el data_cmn.afs del jugador; nada se reparte), pintada con ps2::Escena como la vista previa del Heroe.
// borde toon de una figura (fraccion de su alto): el de B2/IW (0,9 %) o, con el modelo de B3, el fino de B3 HD
float BordeFig(int estado_b3) {
  static const float b3 = std::getenv("DBZ3_FIG_BORDE_B3") ? float(std::atof(std::getenv("DBZ3_FIG_BORDE_B3"))) : 0.005f;
  return estado_b3 == 2 ? b3 : 0.009f;
}
struct ModB3 {
  ps2::Malla malla;
  std::vector<ps2::Tex> tx;
  float alto = 0;
  std::vector<std::vector<std::pair<float, int>>> rampa;   // "RMP1" tras los triangulos: tramos de la rampa de B3 por textura
};
std::map<int, ModB3> g_modb3;
std::set<int> g_modb3_pidiendo, g_modb3_fallo;
std::mutex g_modb3_mu;
int g_fig_b3 = -1;
std::filesystem::path RutaVistaNave(int id) { return dbz3::ModsRoot().parent_path() / "ingame_ui" / "nave_vista" / (std::to_string(id) + ".malla"); }
// fid (entrada de data_cmn) del traje 0 de un luchador: tabla char96 0x8234ABB8 + 96 * ID (+18 modelo de la forma 0, +8 lista de fids)
int FidB3(int id, int traje = 0, int forma = 0) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base || id < 0 || id >= 105) return -1;
  const dbz3::GA tab = dbz3::GA(0x8234ABB8) + 96u * uint32_t(id);
  const uint32_t ptr = REX_LOAD_U32(tab + 8);
  const uint32_t ntr = REX_LOAD_U8(tab + 4), nmod = std::max<uint32_t>(REX_LOAD_U8(tab + 5), 1);
  uint32_t md = REX_LOAD_U8(tab + 18);
  if (forma > 0 && forma < 8) {   // B3M: la forma k (8 B desde +16: u16 ID, u8 modelo dentro del traje)
    const uint32_t m = REX_LOAD_U8(tab + 16 + 8 * uint32_t(forma) + 2);
    if (m < nmod) md = m;
  }
  if (ptr < 0x80000000u || ptr > 0xA0000000u) return -1;
  const uint32_t t = (traje > 0 && uint32_t(traje) < ntr) ? uint32_t(traje) : 0;   // B3M: el traje de B2 en el de B3 con el mismo numero (si no, el 0)
  const uint32_t fid = REX_LOAD_U32(ptr + 12 * (t * nmod + md));   // la lista es de 12 B por modelo: fid, puntero, 0 (medido en la imagen del guest)
  return fid < 100000 ? int(fid) : -1;
}
// ID de B3 de la forma (u16 en +16 + 8 * forma): su pose del select
int IdFormaB3(int id, int forma) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base || id < 0 || id >= 105) return id;
  const dbz3::GA tab = dbz3::GA(0x8234ABB8) + 96u * uint32_t(id);
  const uint32_t v = forma > 0 && forma < 8 ? uint32_t(REX_LOAD_U16(tab + 16 + 8 * uint32_t(forma))) : uint32_t(id);
  return v < 105 ? int(v) : id;
}
// forma de B3 que enseña B2 en sus figuras grandes (como el tablero: Cell perfecto y Freezer en su forma final)
int FormaFigB3(int b3) { return b3 == 33 ? 2 : b3 == 27 ? 3 : 0; }
bool LeerMalla3(const std::filesystem::path& f, ModB3& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 4;
  auto u32 = [&](uint32_t& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  auto f32 = [&](float& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  if (d.size() < 16 || std::memcmp(d.data(), "HMV1", 4) != 0) return false;
  uint32_t ntex = 0, ntri = 0;
  float alto = 0;
  if (!u32(ntex) || !u32(ntri) || !f32(alto) || ntex > 256 || ntri > 200000) return false;
  std::vector<pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  ps2::Malla malla;
  malla.tris.resize(ntri);
  for (auto& t : malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      for (float& x : v) if (!f32(x)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.malla = std::move(malla);
  m.rampa.clear();   // 25/10: "RMP1" (vista_heroe.exportar_malla): tramos de la rampa de B3 por textura
  uint32_t n_orig = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "RMP1", 4) == 0 && (o += 4, u32(n_orig)) && n_orig <= ntex) {
    m.rampa.resize(n_orig);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t n = 0;
      if (!u32(n) || n > 8) { bien = false; break; }
      for (uint32_t k = 0; k < n && bien; ++k) {
        float l0 = 0;
        uint32_t ti = 0;
        if (!f32(l0) || !u32(ti) || ti >= ntex) bien = false;
        else fila.emplace_back(l0, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(ps2::Crear(im, false));
  m.alto = alto;
  return alto > 0 && !m.malla.tris.empty();
}
#if defined(_WIN32)
bool LanzarOculto(const std::wstring& cmd, DWORD* code) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
  *code = 1;
  if (WaitForSingleObject(pi.hProcess, 180000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
#endif
// Lanza un script de mod center hd/cac con el Python del juego (oculto, tope 180 s). true si salio con 0.
bool CorrerVista(const char* script_nombre, const std::wstring& args) {
#if defined(_WIN32)
  namespace fs = std::filesystem;
  const fs::path juego = dbz3::ModsRoot().parent_path(), script = juego / "mod center hd" / "cac" / script_nombre;
  std::error_code ec;
  if (!fs::exists(script, ec)) {
    REXLOG_WARN("dbz3 nave: falta {} (sin modelo B3: cara grande)", script.string());
    return false;
  }
  // La carpeta us/ (eu/) que usa el juego, para que el script no dependa de encontrarla solo (dbz3_user.toml, rutas)
  std::wstring datos;
  if (const fs::path root = dbz3::HostDataRoot(); !root.empty()) {
    const fs::path us = root / (dbz3::settings::Region() == "eu" ? "eu" : "us");
    if (fs::exists(us / "data_cmn.afs", ec)) datos = L" --us \"" + us.wstring() + L"\"";
  }
  std::vector<std::wstring> pys;
  if (const char* e = std::getenv("DBZ3_PYTHON"); e && *e) pys.push_back(L"\"" + fs::path(e).wstring() + L"\"");
  if (fs::exists(juego / "python" / "python.exe", ec)) pys.push_back(L"\"" + (juego / "python" / "python.exe").wstring() + L"\"");
  pys.push_back(L"py -3");
  pys.push_back(L"python");
  for (const std::wstring& py : pys) {
    const std::wstring cmd = py + L" -I -X utf8 \"" + script.wstring() + L"\" --juego \"" + juego.wstring() + L"\"" + datos + L" " + args;
    DWORD code = 1;
    if (!LanzarOculto(cmd, &code) || code == 9009) continue;
    if (code != 0)
      REXLOG_WARN("dbz3 nave: {} {} salio con {} (sin modelo B3: cara grande)", script_nombre, fs::path(args).string(), code);
    return code == 0;
  }
  REXLOG_WARN("dbz3 nave: sin Python para {} (ni python\\ del juego ni del sistema): sin modelo B3", script_nombre);
#else
  (void)script_nombre; (void)args;
#endif
  return false;
}
void GenerarModB3(int id, int fid) {
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_modb3_pidiendo.count(id) || g_modb3_fallo.count(id)) return;
    g_modb3_pidiendo.insert(id);
  }
  std::thread([id, fid] {
    std::error_code ec;
    const bool ok = CorrerVista("vista_nave.py", L"--id " + std::to_wstring(id) + L" --fid " + std::to_wstring(fid)) && std::filesystem::exists(RutaVistaNave(id), ec);
    REXLOG_INFO("dbz3 nave: modelo 3D de B3 id {} (fid {}) {}", id, fid, ok ? "generado" : "no se pudo generar (se queda la cara grande)");
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_pidiendo.erase(id);
    if (!ok) g_modb3_fallo.insert(id);
  }).detach();
}
// hilo de dibujo: el modelo si ya esta; si no, lo pide (una vez) y devuelve null
const ModB3* ModeloB3(int id) {
  if (auto it = g_modb3.find(id); it != g_modb3.end()) return it->second.alto > 0 ? &it->second : nullptr;
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_modb3_pidiendo.count(id) || g_modb3_fallo.count(id)) return nullptr;
  }
  std::error_code ec;
  if (std::filesystem::exists(RutaVistaNave(id), ec)) {
    ModB3& m = g_modb3[id];
    if (LeerMalla3(RutaVistaNave(id), m)) {
      REXLOG_INFO("dbz3 nave: modelo 3D de B3 id {} cargado ({} triangulos, alto {:.2f})", id, m.malla.tris.size(), m.alto);
      return &m;
    }
    m = ModB3{};
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_fallo.insert(id);
    return nullptr;
  }
  const int fid = FidB3(id);
  if (fid < 0) {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_fallo.insert(id);
    return nullptr;
  }
  GenerarModB3(id, fid);
  return nullptr;
}
// ---- B3M (25/10): el modelo de B3 CON HUESOS para las figuras grandes de B2 (vineta del tablero y rueda de la nave).
// vista_b2.py lo exporta de data_cmn.afs del jugador (HMS1: malla en el espacio de su hueso, piel rigida como model_render + el
// esqueleto de reposo). La animacion es la de B2 (banco #AMM del original) pasada hueso a hueso por el SUFIJO del nombre
// (GOK_RARM1 -> RARM1; medido: el reposo de B2 y B3 coincide en 26 de 29 luchadores, ver B3M/INFORME.md); los huesos de B3 sin
// hueso de B2 (pelo, cara) heredan del padre. La posicion de la cadera (pista) se escala por el largo de pierna B3/B2.
struct SkB3 {
  ps2::Malla malla;   // p / n en el espacio del hueso (Tri::hueso)
  std::vector<ps2::Tex> tx;
  std::vector<std::string> suf;
  ps2::Esqueleto esq;   // padre + local (reposo; con el giro del select nativo de B3 en los huesos que lo traen)
  std::vector<ps2::M34> reposo;   // local sin el giro del select (para el largo de pierna)
  std::vector<pasarela::Imagen> imgs;   // las texturas en CPU (para recolorear)
  float alto = 0;
  std::vector<std::vector<std::pair<float, int>>> rampa;   // HMS3: tramos de la rampa de B3 por textura (Escena::Toon::rampa)
  // malla posada (Dragon Tactics T4): se reescriben solo p / n de la copia fija (lo demas de cada Tri no cambia) y, si piden la misma
  // animacion en el mismo instante, sigue valida sin volver a posar. Se usa en el acto (Poner), no se guarda nada entre figuras.
  mutable ps2::Malla posada;
  mutable const Fig* posada_de = nullptr;
  mutable float posada_t = 0;
  // R23 BOCA-03: poses LIPS de la forma (bloque "LIP1" del .sk): boca_h = huesos de boca; boca_p[pose*nh + i] = su local.
  std::vector<int> boca_h;
  std::vector<ps2::M34> boca_p;
  mutable std::vector<ps2::M34> boca_cur;   // el "hablante": locales actuales, mezclados cada cuadro hacia la pose pedida
  mutable int boca_var = -1, boca_var_ant = -1, boca_pose = -1;
  bool boca_formato = false;   // el .sk trae el bloque LIP1 (aunque sea vacio): ya es del horno con boca
};
std::set<int> g_sk_boca_pidiendo;   // fids que se rehornean con la boca (los .sk de antes del 20/11 no la traen)
std::set<int> g_sk_recargar;        // fids rehorneados: se vuelven a leer en el hilo de dibujo
// R23 BOCA-03: pose de boca pedida por el guion de B2 para cada luchador (id de B2): variante*6 + visema; sin entrada = sin boca.
std::map<int, int> g_boca_b2;
int g_boca_fig = -1;   // id de B2 de la figura que se esta posando (FiguraB3 -> PosarSk)
std::map<int, SkB3> g_sk;   // por fid
std::set<int> g_sk_pidiendo, g_sk_fallo;
std::filesystem::path RutaSk(int fid) { return dbz3::ModsRoot().parent_path() / "ingame_ui" / "nave_vista" / ("sk_" + std::to_string(fid) + ".sk"); }
bool LeerSk(const std::filesystem::path& f, SkB3& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 4;
  auto u32 = [&](uint32_t& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  auto f32 = [&](float& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  const bool hms3 = d.size() >= 4 && std::memcmp(d.data(), "HMS3", 4) == 0;
  if (d.size() < 20 || (!hms3 && std::memcmp(d.data(), "HMS2", 4) != 0)) return false;
  uint32_t ntex = 0, ntri = 0, nb = 0;
  float alto = 0;
  if (!u32(ntex) || !u32(ntri) || !u32(nb) || !f32(alto) || ntex > 256 || ntri > 200000 || nb == 0 || nb > 256) return false;
  std::vector<pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  m.esq = ps2::Esqueleto{};
  m.suf.clear();
  m.reposo.clear();
  for (uint32_t i = 0; i < nb; ++i) {
    uint32_t pa = 0;
    if (!u32(pa) || o + 24 + 48 > d.size()) return false;
    m.esq.padre.push_back(int(int32_t(pa)));
    m.suf.emplace_back(reinterpret_cast<const char*>(d.data() + o), strnlen(reinterpret_cast<const char*>(d.data() + o), 24));
    o += 24;
    ps2::M34 l;
    for (float& x : l.m) if (!f32(x)) return false;
    m.reposo.push_back(l);
    uint32_t con = 0;
    float g[9];
    if (!u32(con)) return false;
    for (float& x : g) if (!f32(x)) return false;
    if (con) {   // B3M: el giro local del select nativo de B3 (cola, capa, pelo: lo que B2 no anima)
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) l.m[r * 4 + c] = g[r * 3 + c];
    }
    m.esq.local.push_back(l);
  }
  m.malla = ps2::Malla{};
  m.malla.tris.resize(ntri);
  for (auto& t : m.malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      uint32_t h = 0;
      for (float& x : v) if (!f32(x)) return false;
      if (!u32(h)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
      t.hueso[c] = uint8_t(std::min<uint32_t>(h, nb - 1));
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.rampa.clear();
  uint32_t n_orig = 0;
  if (hms3 && u32(n_orig) && n_orig <= ntex) {
    m.rampa.resize(n_orig);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t n = 0;
      if (!u32(n) || n > 8) { bien = false; break; }
      for (uint32_t k = 0; k < n && bien; ++k) {
        float l0 = 0;
        uint32_t ti = 0;
        if (!f32(l0) || !u32(ti) || ti >= ntex) bien = false;
        else fila.emplace_back(l0, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  m.boca_h.clear();
  m.boca_p.clear();
  m.boca_formato = false;
  if (hms3 && !m.rampa.empty() && o + 12 <= d.size() && std::memcmp(d.data() + o, "LIP1", 4) == 0) {   // R23 BOCA-03
    o += 4;
    m.boca_formato = true;
    uint32_t np = 0, nh = 0;
    if (u32(np) && u32(nh) && np <= 64 && nh <= 16 && o + (size_t(nh) + size_t(np) * nh * 12) * 4 <= d.size()) {
      for (uint32_t i = 0; i < nh; ++i) { uint32_t h = 0; u32(h); m.boca_h.push_back(int(h)); }
      m.boca_p.resize(size_t(np) * nh);
      for (auto& M : m.boca_p) for (float& x : M.m) f32(x);
      if (std::any_of(m.boca_h.begin(), m.boca_h.end(), [&](int h) { return h < 0 || h >= int(nb); })) m.boca_h.clear(), m.boca_p.clear();
    }
  }
  {   // utileria de escena de B3 (Mr. Satan: CAMERA = camara con tripode, GAME = consola): B3 la oculta en el combate y B2 no la anima
    auto util = [&](uint8_t h) { return h < m.suf.size() && (m.suf[h] == "CAMERA" || m.suf[h] == "GAME"); };
    m.malla.tris.erase(std::remove_if(m.malla.tris.begin(), m.malla.tris.end(), [&](const ps2::Malla::Tri& t) { return util(t.hueso[0]) || util(t.hueso[1]) || util(t.hueso[2]); }), m.malla.tris.end());
  }
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(ps2::Crear(im, false));
  m.imgs = std::move(imgs);
  m.alto = alto;
  return alto > 0 && !m.malla.tris.empty();
}
// hilo de dibujo. 0 = no hay (sin B3 / fallo), 1 = generandose, 2 = listo (`*sk`)
int SkEstado(int fid, const SkB3** sk, int cid = -1) {
  if (fid < 0 || std::getenv("DBZ3_SEL_SIN_SK")) { static bool d = false; if (!d) { d = true; REXLOG_INFO("dbz3 nave: B3 con huesos: sin fid ({})", fid); } return 0; }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_sk_recargar.erase(fid)) {   // R23 BOCA-03: rehorneado con la boca: se relee (si falla, se queda el de antes)
      SkB3 nuevo;
      if (LeerSk(RutaSk(fid), nuevo)) g_sk[fid] = std::move(nuevo);
    }
  }
  if (auto it = g_sk.find(fid); it != g_sk.end()) {
    if (it->second.alto > 0) {
      *sk = &it->second;
      // R23 BOCA-03: un .sk viejo (sin LIP1) de alguien que va a hablar se rehornea en segundo plano; mientras, el de antes
      if (!it->second.boca_formato && g_boca_fig >= 0 && g_boca_b2.count(g_boca_fig)) {
        std::lock_guard<std::mutex> lk(g_modb3_mu);
        if (g_sk_boca_pidiendo.insert(fid).second)
          std::thread([fid, cid] {
            std::error_code e2;
            const bool ok = CorrerVista("vista_b2.py", L"--fid " + std::to_wstring(fid) + (cid >= 0 ? L" --id " + std::to_wstring(cid) : L"")) && std::filesystem::exists(RutaSk(fid), e2);
            REXLOG_INFO("dbz3 nave: modelo B3 fid {} rehorneado con la boca: {}", fid, ok ? "si" : "no");
            std::lock_guard<std::mutex> lk2(g_modb3_mu);
            if (ok) g_sk_recargar.insert(fid);
          }).detach();
      }
      return 2;
    }
    return 0;
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_sk_fallo.count(fid)) return 0;
    if (g_sk_pidiendo.count(fid)) return 1;
  }
  std::error_code ec;
  if (std::filesystem::exists(RutaSk(fid), ec)) {
    SkB3& m = g_sk[fid];
    if (LeerSk(RutaSk(fid), m)) {
      REXLOG_INFO("dbz3 nave: modelo B3 con huesos fid {} cargado ({} triangulos, {} huesos, alto {:.2f})", fid, m.malla.tris.size(), m.suf.size(), m.alto);
      *sk = &m;
      return 2;
    }
    REXLOG_INFO("dbz3 nave: B3 con huesos fid {}: el fichero no se pudo leer", fid);
    m = SkB3{};
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_fallo.insert(fid);
    return 0;
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_pidiendo.insert(fid);
  }
  std::thread([fid, cid] {
    std::error_code e2;
    const bool ok = CorrerVista("vista_b2.py", L"--fid " + std::to_wstring(fid) + (cid >= 0 ? L" --id " + std::to_wstring(cid) : L"")) && std::filesystem::exists(RutaSk(fid), e2);
    REXLOG_INFO("dbz3 nave: modelo B3 con huesos fid {} {}", fid, ok ? "generado" : "no se pudo generar");
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_pidiendo.erase(fid);
    if (!ok) g_sk_fallo.insert(fid);
  }).detach();
  return 1;
}
// largo de pierna en reposo (cadera -> punta del pie) de un esqueleto, por sufijos; 0 si falta algun hueso
float LargoPierna(const std::vector<int>& padre, const std::vector<ps2::M34>& local, const std::vector<std::string>& suf) {
  std::vector<ps2::M34> w(padre.size());
  int cad = -1, pie = -1;
  for (size_t b = 0; b < padre.size(); ++b) {
    w[b] = padre[b] >= 0 ? w[size_t(padre[b])] * local[b] : local[b];
    if (b < suf.size()) {
      if (suf[b] == "WAIST") cad = int(b);
      else if (suf[b] == "NLF" || (pie < 0 && suf[b] == "LFOOT1")) pie = int(b);
    }
  }
  return cad >= 0 && pie >= 0 ? w[size_t(cad)].m[7] - w[size_t(pie)].m[7] : 0.0f;
}
// la animacion de B2 de `f` en los huesos de `sk` (cache por pareja)
const ps2::Anim& AnimSk(const SkB3& sk, const Fig& f) {
  static std::map<std::pair<const SkB3*, const Fig*>, ps2::Anim> cache;
  const auto key = std::make_pair(&sk, &f);
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  ps2::Anim& a = cache[key];
  if (f.suf.empty() || !f.an || f.suf.size() != f.an.ang.size()) return a;
  const float l2 = LargoPierna(f.m.esq->padre, f.m.esq->local, f.suf), l3 = LargoPierna(sk.esq.padre, sk.reposo, sk.suf);
  const float k = (l2 > 0.5f && l3 > 0.5f) ? std::clamp(l3 / l2, 0.5f, 2.0f) : 1.0f;
  a.cuadros = f.an.cuadros;
  a.relativa = f.an.relativa;
  a.absoluta = f.an.absoluta;
  a.ang.resize(sk.suf.size());
  a.pos.resize(sk.suf.size());
  int hechos = 0;
  for (size_t j = 0; j < sk.suf.size(); ++j)
    for (size_t i = 0; i < f.suf.size(); ++i)
      if (sk.suf[j] == f.suf[i]) {
        a.ang[j] = f.an.ang[i];
        a.pos[j] = f.an.pos[i];
        for (auto& c : a.pos[j]) c[1] *= k, c[2] *= k, c[3] *= k;
        ++hechos;
        break;
      }
  REXLOG_INFO("dbz3 nave: B3 con huesos: {} de {} huesos de B3 con pista de B2 ({} huesos), escala de cadera {:.3f}", hechos, sk.suf.size(), f.suf.size(), k);
  // Las placas de armadura saiyan (*PAT: hombreras y faldon) no tienen pista en B2 ni giro nativo en B3: con el giro de reposo (el
  // del T-pose de B3, ejes cambiados 90 grados) quedaban como discos planos; con giro cero (absoluto) cuelgan de su padre
  // (medido en la hoja del roster 31/10, ids 9-12). DBZ3_TEST_SK_SINPISTA=2: lo mismo para todo hueso sin pista (prueba).
  const char* sinpista = std::getenv("DBZ3_TEST_SK_SINPISTA");
  for (size_t j = 0; j < sk.suf.size(); ++j)
    if (a.ang[j].empty() && ((sinpista && sinpista[0] == '2') || sk.suf[j].find("PAT") != std::string::npos)) a.ang[j] = {{0, 0, 0, 0}};
  if (hechos < int(sk.suf.size()) && std::getenv("DBZ3_TEST_B2_HOJA")) {   // prueba: que huesos quedan sin pista (se quedan en el reposo de B3)
    std::string sin, ocio;
    for (size_t j = 0; j < sk.suf.size(); ++j)
      if (j < a.ang.size() && a.ang[j].empty()) sin += sk.suf[j] + " ";
    for (const auto& n2 : f.suf)
      if (std::find(sk.suf.begin(), sk.suf.end(), n2) == sk.suf.end()) ocio += n2 + " ";
    REXLOG_INFO("dbz3 hoja: sin pista B3 [{}] | B2 sin pareja [{}]", sin, ocio);
  }
  return a;
}
// R23 BOCA-03: el motor de boca de Dimps sobre la figura de la vineta (B2 FUN_001f5d50 / B3 sub_820F97C8 + sub_82136FD0):
// cada cuadro el hablante se mezcla hacia la pose LIPS pedida (nlerp del giro, lerp de la posicion; 0,6 si no cambia la
// variante, 0,75 si cambia) y se copia a los huesos de boca del cuerpo; aqui se recalculan sus ejes de mundo con su padre.
ps2::M34 MezclaLocal(const ps2::M34& a, const ps2::M34& b, float k) {
  auto quat = [](const ps2::M34& M, float q[4]) {   // matriz de giro (filas) -> cuaternion (x, y, z, w)
    const float* m = M.m;
    const float tr = m[0] + m[5] + m[10];
    if (tr > 0) { const float s = std::sqrt(tr + 1.0f) * 2; q[3] = 0.25f * s; q[0] = (m[9] - m[6]) / s; q[1] = (m[2] - m[8]) / s; q[2] = (m[4] - m[1]) / s; }
    else if (m[0] > m[5] && m[0] > m[10]) { const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2; q[3] = (m[9] - m[6]) / s; q[0] = 0.25f * s; q[1] = (m[1] + m[4]) / s; q[2] = (m[2] + m[8]) / s; }
    else if (m[5] > m[10]) { const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2; q[3] = (m[2] - m[8]) / s; q[0] = (m[1] + m[4]) / s; q[1] = 0.25f * s; q[2] = (m[6] + m[9]) / s; }
    else { const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2; q[3] = (m[4] - m[1]) / s; q[0] = (m[2] + m[8]) / s; q[1] = (m[6] + m[9]) / s; q[2] = 0.25f * s; }
  };
  float qa[4], qb[4];
  quat(a, qa), quat(b, qb);
  const float dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
  float q[4], n = 0;
  for (int i = 0; i < 4; ++i) q[i] = qa[i] * (1 - k) + (dot < 0 ? -qb[i] : qb[i]) * k, n += q[i] * q[i];
  n = n > 0 ? 1.0f / std::sqrt(n) : 1.0f;
  for (float& v : q) v *= n;
  const float x = q[0], y = q[1], z = q[2], ww = q[3];
  ps2::M34 r;
  const float R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * ww), 2 * (x * z + y * ww), 2 * (x * y + z * ww), 1 - 2 * (x * x + z * z),
                      2 * (y * z - x * ww), 2 * (x * z - y * ww), 2 * (y * z + x * ww), 1 - 2 * (x * x + y * y)};
  for (int f = 0; f < 3; ++f) {
    for (int c = 0; c < 3; ++c) r.m[f * 4 + c] = R[f * 3 + c];
    r.m[f * 4 + 3] = a.m[f * 4 + 3] + (b.m[f * 4 + 3] - a.m[f * 4 + 3]) * k;
  }
  return r;
}
void BocaLips(const SkB3& sk, std::vector<ps2::M34>& w) {
  const size_t nh = sk.boca_h.size();
  if (!nh || g_boca_fig < 0) return;
  const auto it = g_boca_b2.find(g_boca_fig);
  if (it == g_boca_b2.end()) return;
  const int np = int(sk.boca_p.size() / nh);
  const int pose = std::clamp(it->second, 0, np - 1), var = pose / 6;
  if (sk.boca_cur.size() != nh) {   // arranque (FUN_001f1770 -> FUN_001f57f0(.., 1)): el hablante copia el cuerpo
    sk.boca_cur.clear();
    for (int h : sk.boca_h) sk.boca_cur.push_back(sk.esq.local[size_t(h)]);
  }
  if (pose != sk.boca_pose) sk.boca_var_ant = sk.boca_var, sk.boca_var = var, sk.boca_pose = pose;
  const float k = sk.boca_var_ant == sk.boca_var ? 0.6f : 0.75f;
  for (size_t i = 0; i < nh; ++i) sk.boca_cur[i] = MezclaLocal(sk.boca_cur[i], sk.boca_p[size_t(pose) * nh + i], k);
  for (size_t i = 0; i < nh; ++i) {   // los huesos de boca van despues de su padre (HEAD, M_JAW) en el orden del .sk
    const int h = sk.boca_h[i], p = sk.esq.padre[size_t(h)];
    w[size_t(h)] = p >= 0 ? w[size_t(p)] * sk.boca_cur[i] : sk.boca_cur[i];
  }
  // los hijos de los huesos de boca (si los hay) se quedan con el eje de antes: en B3 son hojas (7 huesos M_*)
}
// Malla de B3 posada con la animacion de B2 de `f` en el segundo `t`; mismo marco que la de B2 (Posar: x/z menos el centro de
// la malla de B2). false si no se puede (sin pistas): se queda el modelo de B2.
bool PosarSk(const SkB3& sk, const Fig& f, float t, const ps2::Malla*& out) {
  const ps2::Anim& a = AnimSk(sk, f);
  if (!a) return false;
  out = &sk.posada;
  const bool habla = !sk.boca_h.empty() && g_boca_fig >= 0 && g_boca_b2.count(g_boca_fig);   // la boca cambia aunque t no
  if (!habla && sk.posada_de == &f && sk.posada_t == t && sk.posada.tris.size() == sk.malla.tris.size()) return true;
  std::vector<ps2::M34> w = EjesMundo(sk.esq, a, t);
  BocaLips(sk, w);
  static const bool sin_centro = std::getenv("DBZ3_SEL_SK_SINCENTRO") != nullptr;   // prueba: B3 en coordenadas crudas del modelo (como el juego)
  const ps2::V3 c = (f.m.esq && !sin_centro) ? f.m.esq->centro : ps2::V3{};
  { static bool dicho = false; if (!dicho && f.m.esq) { dicho = true; REXLOG_INFO("dbz3 nave: B3 con huesos: centro de la malla de B2 ({:.3f}, {:.3f})", f.m.esq->centro.x, f.m.esq->centro.z); } }
  if (sk.posada.tris.size() != sk.malla.tris.size()) sk.posada.tris = sk.malla.tris;
  for (size_t i = 0; i < sk.malla.tris.size(); ++i) {
    const ps2::Malla::Tri& src = sk.malla.tris[i];
    ps2::Malla::Tri& tr = sk.posada.tris[i];
    for (int k = 0; k < 3; ++k) {
      const ps2::M34& W = w[src.hueso[k]];
      const ps2::V3 p = W.por(src.p[k]);
      tr.p[k] = {p.x - c.x, p.y, p.z - c.z};
      tr.n[k] = W.gira(src.n[k]);
    }
  }
  sk.posada_de = &f, sk.posada_t = t;
  return true;
}
// ---- B3M: colores del Saibaman. B2 trae 5 (modo = traje: 0 verde, 1 salmon, 2 azul, 3 amarillo, 4 rosa) y B3 solo 2 (los modos 0 y 1,
// por traje); los otros 3 se recolorean desde el verde de B3 con el color medio de la piel (texturas 0, 2, 5 del cuerpo de B2) y de la
// armadura (8, 9) de ese traje de B2. ponytail: indices de textura medidos (B3M/INFORME.md) solo para el Saibaman (id 22).
struct Hsv { float h, s, v; };
Hsv HsvDe(float r, float g, float b) {   // r, g, b en 0..1; h en grados
  const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  float h = 0;
  if (d > 1e-6f) h = mx == r ? 60.0f * std::fmod((g - b) / d + 6.0f, 6.0f) : mx == g ? 60.0f * ((b - r) / d + 2.0f) : 60.0f * ((r - g) / d + 4.0f);
  return {h, mx > 0 ? d / mx : 0.0f, mx};
}
void RgbDe(Hsv c, float& r, float& g, float& b) {
  const float hh = std::fmod(c.h + 720.0f, 360.0f) / 60.0f, ch = c.v * c.s, x = ch * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f)), m = c.v - ch;
  float rr = 0, gg = 0, bb = 0;
  const int k = int(hh);
  if (k == 0) rr = ch, gg = x; else if (k == 1) rr = x, gg = ch; else if (k == 2) gg = ch, bb = x; else if (k == 3) gg = x, bb = ch; else if (k == 4) rr = x, bb = ch; else rr = ch, bb = x;
  r = rr + m; g = gg + m; b = bb + m;
}
Hsv MediaHsv(const std::vector<pasarela::Imagen>& tex, std::initializer_list<size_t> idx) {
  double r = 0, g = 0, b = 0, n = 0;
  for (size_t i : idx) {
    if (i >= tex.size()) continue;
    const auto& im = tex[i];
    for (size_t k = 0; k + 3 < im.rgba.size(); k += 4)
      if (im.rgba[k + 3] > 128) r += im.rgba[k], g += im.rgba[k + 1], b += im.rgba[k + 2], n += 1;
  }
  return n > 0 ? HsvDe(float(r / n / 255.0), float(g / n / 255.0), float(b / n / 255.0)) : Hsv{0, 0, 1};
}
// copia de `base` recoloreada al traje `modo` de B2 (f = el Saibaman de B2 de ese traje); cache por (modo)
const SkB3* SkTinte(const SkB3& base, const Fig& f, int modo) {
  static std::map<std::pair<const SkB3*, int>, SkB3> cache;
  const auto key = std::make_pair(&base, modo);
  if (auto it = cache.find(key); it != cache.end()) return &it->second;
  SkB3& o = cache[key];
  o.malla = base.malla;   // las texturas de GPU no se copian (unique_ptr): se rehacen abajo
  o.suf = base.suf;
  o.esq = base.esq;
  o.reposo = base.reposo;
  o.alto = base.alto;
  const Hsv piel2 = MediaHsv(f.m.tex, {0, 2, 5}), arma2 = MediaHsv(f.m.tex, {8, 9});
  const Hsv piel3{65.0f, 0.63f, 0.81f}, arma3{80.0f, 0.87f, 0.45f};   // medidos en el verde de B3 (fid 371)
  o.imgs = base.imgs;
  for (auto& im : o.imgs) {
    for (size_t k = 0; k + 3 < im.rgba.size(); k += 4) {
      if (im.rgba[k + 3] < 8) continue;
      Hsv c = HsvDe(im.rgba[k] / 255.0f, im.rgba[k + 1] / 255.0f, im.rgba[k + 2] / 255.0f);
      if (c.s < 0.12f) continue;
      const bool piel = c.h >= 45.0f && c.h < 72.0f, arma = c.h >= 72.0f && c.h < 110.0f;
      if (!piel && !arma) continue;
      const Hsv& t = piel ? piel2 : arma2;
      const Hsv& b = piel ? piel3 : arma3;
      c.h += t.h - b.h;
      c.s = std::clamp(c.s * (t.s / std::max(b.s, 0.05f)), 0.0f, 1.0f);
      c.v = std::clamp(c.v * (t.v / std::max(b.v, 0.05f)), 0.0f, 1.0f);
      float r, g, bl;
      RgbDe(c, r, g, bl);
      im.rgba[k] = uint8_t(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
      im.rgba[k + 1] = uint8_t(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
      im.rgba[k + 2] = uint8_t(std::clamp(bl * 255.0f + 0.5f, 0.0f, 255.0f));
    }
    o.tx.push_back(ps2::Crear(im, false));
  }
  REXLOG_INFO("dbz3 nave: Saibaman B3 recoloreado al modo {} de B2 (piel h {:.0f} s {:.2f} v {:.2f}, armadura h {:.0f} s {:.2f} v {:.2f})", modo, piel2.h, piel2.s, piel2.v, arma2.h, arma2.s, arma2.v);
  return &o;
}
// 0 = no aplica (se queda el modelo de B2), 1 = el de B3 se esta generando (no se dibuja nada), 2 = `m` (malla posada, valida hasta la
// siguiente figura del mismo modelo) / `tx` / `alto` / `rampa` (la toon de esa figura) son los de B3
int FiguraB3(int id_b2, int traje, const Fig& f, float t, const ps2::Malla*& m, const std::vector<ps2::Tex>*& tx, float& alto,
             const std::vector<std::vector<std::pair<float, int>>>*& rampa) {
  const int b3 = B3DeB2(id_b2);
  if (b3 < 0 || !f.ok || std::getenv("DBZ3_SEL_FORZAR_B2")) return 0;
  const SkB3* sk = nullptr;
  g_boca_fig = id_b2;   // SkEstado mira si va a hablar (rehorneo con la boca)
  const int e = SkEstado(FidB3(b3, traje, FormaFigB3(b3)), &sk, IdFormaB3(b3, FormaFigB3(b3)));
  g_boca_fig = -1;
  if (e != 2) return e;
  if (id_b2 == 22 && traje >= 2 && traje <= 4) sk = SkTinte(*sk, f, traje);   // B3M: modos azul / amarillo / rosa del Saibaman de B2
  g_boca_fig = id_b2;
  const bool posada = PosarSk(*sk, f, t, m);
  g_boca_fig = -1;
  if (!posada) return 0;
  tx = &sk->tx;
  alto = sk->alto;
  rampa = sk->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? nullptr : &sk->rampa;
  return 2;
}
// alto de la malla de B3 (brazos abajo) / alto del modelo de B2: Goku 19,6 / 14,0, Piccolo 20,4 / 17,1, Krillin 13,3 / 12,1, Frieza 16,6 / 13,7
constexpr float kEscalaB3 = 1.2f;
void PintaExtra(ImDrawList* dl) {
  const ModB3* m = ModeloB3(g_fig_b3);
  if (!m) {   // sin modelo (generandose o sin Python / data_cmn): la cara grande de B3 HD
    {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      if (!g_modb3_fallo.count(g_fig_b3)) return;
    }
    if (const ImTextureID tex = CaraDe(g_fig_b3)) {
      const float t = g_fig_px * 0.55f;
      const ImVec2 c(g_fig_pie.x, g_fig_pie.y - g_fig_px * 0.62f);
      dl->AddImage(tex, ImVec2(c.x - t * 0.5f, c.y - t * 0.5f), ImVec2(c.x + t * 0.5f, c.y + t * 0.5f));
    }
    return;
  }
  const float hb2 = m->alto / kEscalaB3;
  // Los extras no tienen registro de B2: su factor sale de la camara real de los tres personajes de B2 de altura mas
  // parecida (FacPorAltura). El 0,93 queda solo con dbz3_nave_rueda_datos apagado (lo entregado en 1.5.6.x).
  const float fac = RuedaDatos() ? FacPorAltura(hb2) : 0.93f;
  std::array<const AltoB2*, 3> cer{};
  for (auto& c : cer) {
    const AltoB2* mejor = nullptr;
    for (const AltoB2& r : kAltosB2) {
      if (std::find(cer.begin(), cer.end(), &r) != cer.end()) continue;
      if (!mejor || std::fabs(r.h - hb2) < std::fabs(mejor->h - hb2)) mejor = &r;
    }
    c = mejor;
  }
  float href = 0;
  for (const AltoB2* c : cer) href += c->h / 3;
  const AltoB2* ref = cer[0];
  // 1,38: lo que mide en pantalla un modelo de B2 por encima de su altura nominal (cabeza medida contra PCSX2: Goku, Teen Gohan...)
  // tope: la cabeza siempre dentro de la pantalla (los de B2 mas altos, Cell y Frieza, ya salen casi recortados)
  const float alto_px = std::min(1.38f * g_fig_px * fac * hb2 / (href + 1.1f), 0.95f * g_fig_px);
  ps2::Camara cam;
  cam.fov = 0.30f;   // perspectiva suave: las colas y capas de B3 (Cooler) hacia la camara no deben agrandar el cuerpo
  cam.c = g_fig_pie;
  cam.h = ImVec2(450, 450);
  const float fl = 450.0f / std::tan(cam.fov * 0.5f), d = m->alto * fl / std::max(alto_px, 1.0f);
  { static std::set<int> v; if (v.insert(g_fig_b3).second) REXLOG_INFO("dbz3 nave: extra B3 {}: alto {:.2f} hb2 {:.2f} ref B2 {} fac {:.2f} alto_px {:.1f} d {:.2f} g_fig_px {:.1f}", g_fig_b3, m->alto, hb2, ref->id, fac, alto_px, d, g_fig_px); }
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = m->alto * (m->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? 0.009f : BordeFig(2));
  toon.rampa = m->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? nullptr : &m->rampa;   // 25/10: la rampa de B3
  // HMS3 ya lleva cocido base-rampa, tambien cuando la rampa tiene UN solo
  // tramo (ojos, cara, pelo). Escena solo recorta con >1: no sombrear dos veces.
  if (toon.rampa) for (float& tono : toon.tonos) tono = 1.0f;
  static const float giro = std::getenv("DBZ3_SEL_GIRO") ? float(std::atof(std::getenv("DBZ3_SEL_GIRO"))) : 0.35f;
  esc.Poner(cam, m->malla, m->tx, {0, 0, 0}, giro, 1.0f, toon);
  esc.Pintar(dl);
}
// ---- A2n: camara real de la rueda de B2. FUN_002AE570: registro 0x37ABD0 + tipo*0x66C + 0x10 + id*0x2C
// (esta rueda usa el bloque base, tipo 0):
// [0]/[1] x (1P/COM), [2] y, [3] desplazamiento vertical, [4] distancia (el original la escala por 8/7);
// la escala final de perspectiva usa [4] en FUN_002AE570. El giro procede de la cabecera de camara [1]/[2].
struct CamRueda { float cx = 0, cy = 0, dy = 0, dz = 0, yaw = 0, F = 0, esc = 1; bool ok = false; };
Lienzo g_fig_L{1, 1, ImVec2(0, 0)};
CamRueda CamDeRueda(int id, bool com) {
  CamRueda r;
  static float F0 = 5863.36f, dcx = 0, dcy = 0, gx = 0, ayx = 1;
  if (static const char* fa = std::getenv("DBZ3_SEL_CAMFILE"); fa) {   // prueba: "F dcx dcy giro_extra" releido cada vez
    if (FILE* fp = std::fopen(fa, "r")) {
      float v[4] = {F0, 0, 0, 0};
      if (std::fscanf(fp, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]) >= 1) { F0 = v[0]; dcx = v[1]; dcy = v[2]; gx = v[3]; }
      std::fclose(fp);
    }
  }
  const std::vector<uint8_t>& elf = ElfB2();
  const size_t base = size_t(0x37ABD0) - 0x100000 + 0x80;
  const size_t row = base + 0x10 + size_t(id) * 0x2C;
  if (id < 0 || id > 0x3E || row + 0x2C > elf.size()) return r;
  auto fl = [&](size_t o) { float v; std::memcpy(&v, elf.data() + o, 4); return v; };
  const uint32_t w = U32(elf, base + 4 + 4 * (com ? 1 : 0));
  r.yaw = float(int16_t(uint16_t(w & 0xFFFF))) * (6.2831853f / 65536.0f) + gx;
  r.cx = fl(row + (com ? 4 : 0)) + dcx;
  r.cy = fl(row + 8) + dcy;
  r.dy = fl(row + 12);
  r.dz = fl(row + 16) * (448.0f / 392.0f);
  r.F = F0;
  {   // escala del personaje: tabla de floats DAT_003892A0[id] (FUN_002AE010 la aplica a la raiz del modelo: FUN_00149E40)
    const size_t o = size_t(0x3892A0) - 0x100000 + 0x80 + 4 * size_t(id);
    if (o + 4 <= elf.size()) { const float e = fl(o); if (e > 0.2f && e < 3.0f) r.esc = e; }
  }
  r.ok = std::isfinite(r.cx) && std::isfinite(r.cy) && r.dz < -1.0f;
  return r;
}
void PonerFiguraRueda(ps2::Escena& esc, ps2::Camara cam, const ps2::Malla& m, const std::vector<ps2::Tex>& tx,
                     ps2::V3 pos, float giro, const ps2::Escena::Toon& toon) {
  cam.c.x += g_fig_salida_x * g_fig_L.s;
  // FUN_002AABA0: blanco -> material nativo. Mezclar el material, no superponer
  // triangulos translucidos (las caras ocultas acumularian alfa y crearían facetas).
  const std::vector<Imagen>* imgs = nullptr;
  if (g_fig_flash > 0) for (const auto& [id, sk] : g_sk) if (&sk.tx == &tx) { imgs = &sk.imgs; break; }
  if (!imgs || imgs->size() != tx.size()) { esc.Poner(cam, m, tx, pos, giro, 1.0f, toon); return; }
  static const void* de = nullptr;
  static int paso = -1;
  static std::vector<ps2::Tex> claras;
  const int actual = int(std::round(g_fig_flash * 60));
  const float f = float(actual) / 60.0f;
  if (de != imgs || paso != actual) {
    claras.clear();
    for (const auto& im0 : *imgs) {
      Imagen im = im0;
      for (size_t k = 0; k + 3 < im.rgba.size(); k += 4)
        for (int ch = 0; ch < 3; ++ch) im.rgba[k + ch] = uint8_t(float(im.rgba[k + ch]) * (1 - f) + 255 * f);
      claras.push_back(ps2::Crear(im));
    }
    de = imgs; paso = actual;
  }
  ps2::Malla clara = m;
  for (auto& tr : clara.tris) if (tr.con_color) for (auto& col : tr.col) {
    ImU32 nuevo = col & IM_COL32_A_MASK;
    for (int ch = 0; ch < 3; ++ch) nuevo |= ImU32(float((col >> (ch * 8)) & 255) * (1 - f) + 255 * f) << (ch * 8);
    col = nuevo;
  }
  auto luz = toon;
  luz.borde *= 1 - f;
  for (auto& tono : luz.tonos) tono = tono * (1 - f) + f;
  esc.Poner(cam, clara, claras, pos, giro, 1.0f, luz);
}
void PintaFig(ImDrawList* dl) {
  if (static const bool sin = std::getenv("DBZ3_SEL_SINMODELO") != nullptr; sin) return;
  if (g_fig_id < 0) {
    if (g_fig_b3 >= 0) PintaExtra(dl);
    return;
  }
  const Fig& f = FigDe(g_fig_id, g_traje_rueda);
  if (!f.ok || f.alto <= 0) return;
  static ps2::Malla pose;
  const bool anima = bool(f.an);
  if (anima) ps2::Posar(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())), pose);
  ps2::Malla m = anima ? pose : f.m.malla;
  const std::vector<ps2::Tex>* txp = &f.tx;
  float alto_f = f.alto;
  const std::vector<std::vector<std::pair<float, int>>>* rampa = nullptr;
  const ps2::Malla* pm = nullptr;
  const int estado_b3 = anima ? FiguraB3(g_fig_id, g_traje_rueda, f, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())), pm, txp, alto_f, rampa) : 0;   // B3M
  if (estado_b3 == 2) m = *pm;
  if (estado_b3 == 1) return;   // el de B3 se esta generando (primer uso)
  // Nunca el modelo de B2 de un luchador que tiene B3 (peticion del usuario): sin modelo B3, su cara grande de B3 HD
  if (const int b3 = B3DeB2(g_fig_id); estado_b3 == 0 && b3 >= 0 && !std::getenv("DBZ3_SEL_FORZAR_B2")) {
    static std::set<int> avisados;
    if (avisados.insert(g_fig_id).second) REXLOG_WARN("dbz3 nave: id B2 {} sin modelo B3 en la rueda: cara grande de B3 HD", g_fig_id);
    if (const ImTextureID tex = CaraDe(b3)) {
      const float t = g_fig_px * 0.55f;
      const ImVec2 c(g_fig_pie.x, g_fig_pie.y - g_fig_px * 0.62f);
      dl->AddImage(tex, ImVec2(c.x - t * 0.5f, c.y - t * 0.5f), ImVec2(c.x + t * 0.5f, c.y + t * 0.5f));
    }
    return;
  }
  float ymin = 1e9f, ymax = -1e9f;
  for (const auto& tr : m.tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y), ymax = std::max(ymax, v.y);
  if (f.cab.esq && f.hueso >= 0 && anima && estado_b3 != 2) {
    const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())));
    const ps2::M34 W = w[size_t(f.hueso)];
    const int base_tex = int(f.tx.size()) - int(f.cab.tex.size());
    for (const auto& tr0 : f.cab.esq->crudo.tris) {
      ps2::Malla::Tri tr = tr0;
      for (int c = 0; c < 3; ++c) {
        tr.p[c] = W.por(tr0.p[c]);
        tr.p[c].x -= f.m.esq->centro.x;
        tr.p[c].z -= f.m.esq->centro.z;
        tr.n[c] = W.gira(tr0.n[c]);
      }
      tr.tex = tr0.tex + base_tex;
      if (g_fig_id == 0 && tr0.tex == 2) {   // B2G 24/10: la cara de Goku (textura 2) sin el tinte de color: salia naranja y sin blancos de ojos
        tr.con_color = false; for (auto& cc : tr.col) cc = IM_COL32_WHITE; }
      m.tris.push_back(tr);
    }
  }
  { static uint64_t vistos = 0; if (g_fig_id < 64 && !(vistos >> g_fig_id & 1)) { vistos |= 1ull << g_fig_id; REXLOG_INFO("dbz3 nave: figura id B2 {} alto de modelo {:.3f} ymin {:.3f} ymax {:.3f}", g_fig_id, (ymax - ymin + 1.1f), ymin, ymax);
      if (f.m.esq->padre.size() > 1) {   // A2n: largo de pierna en reposo (cadera -> suela) para el reescalado de la pista de la cadera
        std::vector<ps2::M34> rw(f.m.esq->padre.size());
        float mn = 1e9f, mx = -1e9f;
        for (size_t b = 0; b < rw.size(); ++b) { rw[b] = f.m.esq->padre[b] >= 0 ? rw[size_t(f.m.esq->padre[b])] * f.m.esq->local[b] : f.m.esq->local[b]; mn = std::min(mn, rw[b].m[7]); mx = std::max(mx, rw[b].m[7]); }
        REXLOG_INFO("dbz3 nave: figura id B2 {} reposo-mundo y min {:.3f} max {:.3f}", g_fig_id, mn, mx);
      }
      if (anima && f.m.esq->padre.size() > 1) { const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime()))); REXLOG_INFO("dbz3 nave: figura id B2 {} cadera mundo ({:.3f}, {:.3f}, {:.3f}) huesos {} reposo ({:.3f}, {:.3f}, {:.3f}) padre1 {}", g_fig_id, w[1].m[3], w[1].m[7], w[1].m[11], w.size(), f.m.esq->local[1].m[3], f.m.esq->local[1].m[7], f.m.esq->local[1].m[11], f.m.esq->padre[1]); } } }
  if (!std::getenv("DBZ3_SEL_VIEJO")) {   // A2n: la camara REAL de la rueda de B2 (FUN_002AE570): registro 0x37ABD0 + id*0x2C
    const CamRueda cr = CamDeRueda(g_fig_id, g_rueda_com);
    if (cr.ok) {
      const float ay = 448.0f / 392.0f;   // el diseno de B2 es 640 x 392 y la pantalla (GS) 448 filas: SOLO el centro de pantalla (196 + cy) va por ay
      // B3M (25/10): la malla NO se estira en vertical. Medido sobre el original de A2n (Goku, Piccolo, Vegeta; B3M/herramientas/rueda_hip.py):
      // con el estiramiento Goku salia ~25 % mas alto/grande (el "Goku mas grande" de A2n); sin el, la cabeza cae donde el original.
      // DBZ3_SEL_AYMALLA=1 vuelve al estiramiento anterior.
      static const float ay_m = std::getenv("DBZ3_SEL_AYMALLA") ? ay : 1.0f;
      for (auto& tr : m.tris)
        for (auto& v : tr.p) v.y *= ay_m;
      ps2::Camara cam;
      const float fd = cr.F * g_fig_L.s;
      cam.h = ImVec2(450, 450);
      cam.fov = 2.0f * std::atan(450.0f / fd);
      cam.c = g_fig_L.P(320.0f + (g_rueda_com && !std::getenv("DBZ3_SEL_COMX_VIEJO") ? -cr.cx : cr.cx), (196.0f + cr.cy) * ay);   // eB2e: la fila COM (cx > 0) deja la figura a la izquierda como en el original (PCSX2, planta IV)
      cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
      cam.m[13] = cr.dy * ay_m;
      cam.m[14] = cr.dz;
      cam.ok = true;
      ps2::Escena esc;
      esc.Empezar();
      ps2::Escena::Toon toon;
      toon.borde = alto_f * BordeFig(estado_b3);
      toon.rampa = estado_b3 == 2 ? rampa : nullptr;
      if (toon.rampa) for (float& tono : toon.tonos) tono = 1.0f;
      ps2::V3 desp{0, 0, 0};   // la tabla de escala solo multiplica la POSICION de la raiz (medido: cadera x 0,164 -> 0,131 en Vegeta, malla sin escalar)
      if (anima && f.m.esq->padre.size() > 1) {
        const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())));
        const float k = cr.esc - 1.0f, hx = w[1].m[3] * k, hy = w[1].m[7] * k, hz = w[1].m[11] * k, cg = std::cos(cr.yaw), sg = std::sin(cr.yaw);
        desp = {hx * cg + hz * sg, hy * ay_m, -hx * sg + hz * cg};
      }
      PonerFiguraRueda(esc, cam, m, *txp, desp, cr.yaw, toon);
      esc.Pintar(dl);
      return;
    }
  }
  // px por unidad del modelo igual para todos (el de Goku: alto de referencia), anclado por los pies
  const float alto_modelo = ymax - ymin + 1.1f;
  // B2 encuadra a todos a una altura de pantalla parecida (Cell, Piccolo y Goku salen del mismo tamano): altura normalizada.
  // 29 casillas medidas contra PCSX2 (A2nave, ronda 4); sin registro en esa tabla, el factor sale de la camara real de B2
  // (FacPorAltura) y no de un 0,93 fijo. Con dbz3_nave_rueda_datos la rueda ni llega aqui (usa CamDeRueda).
  const float fac = FacDe(g_fig_id, alto_modelo);
  const float alto_px = g_fig_px * fac;
  ps2::Camara cam;
  cam.fov = 0.785f;
  cam.c = g_fig_pie;
  cam.h = ImVec2(450, 450);
  const float fl = 450.0f / std::tan(cam.fov * 0.5f), d = alto_modelo * fl / std::max(alto_px, 1.0f);
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = alto_f * BordeFig(estado_b3);
  toon.rampa = estado_b3 == 2 ? rampa : nullptr;
  if (toon.rampa) for (float& tono : toon.tonos) tono = 1.0f;
  static const float giro = std::getenv("DBZ3_SEL_GIRO") ? float(std::atof(std::getenv("DBZ3_SEL_GIRO"))) : 0.35f;
  PonerFiguraRueda(esc, cam, m, *txp, {0, -ymin, 0}, giro, toon);
  esc.Pintar(dl);
}

// ---- La rueda: anillo de 30 caras + circulo azul + nombre + flechas y rotulo 1P/2P/COM + (modelo 3D: aparte)
// `rotulo`: 0 = 1P, 1 = COM, 2 = 2P (nave 2P). Imagenes del #AMA de la rueda: 118 (1P), 35 (COM) y 119 (2P).
void DibRueda(ImDrawList* dl, const Lienzo& L, float c, int rotulo, bool menu) {
  if (!g_rueda.ok()) return;
  const std::vector<Entrada>& pg = Pag();
  const int n = int(pg.size()) + 1;   // + el "?"
  const Giro2& g = g_giro_rueda;
  float R = 180.0f, cab = float(kAnilloRueda.th0), roll = 0;
  if (c < 60.0f) {
    const AnilloEntrada e = EntradaAnillo(c, 60.0f, float(kAnilloRueda.th0));
    R = e.R;
    cab = e.cab;
    roll = e.roll;
  }
  for (int i = 0; i < n; ++i) {
    float d = float(i) - g.pos;
    d -= float(n) * std::round(d / float(n));
    float x, y, z;
    AnilloPos(kAnilloRueda, R, cab, kAnilloRueda.arco * d / float(n) + roll, x, y, z);
    if (SaliendoRueda() && i < int(g_sel.puntos.size())) {
      const auto& p = g_sel.puntos[size_t(i)];
      x = p.x + p.vx * g_sel.c; y = p.y + p.vy * g_sel.c; z = p.z;
    }
    float s = 1.0f - (z - kAnilloRueda.z0) * 0.0002f;
    if (i == g.obj && c >= 60.0f && s >= 0.97f) s = 1.2f;   // la elegida (FUN_002A97C0)
    const ImVec2 p(x, y);
    const int cara = i == n - 1 ? 29 : pg[size_t(i)].cara;
    if (cara >= 0) {
      Spr2(L, g_rueda, 36 + cara, 0, false, ImVec2(0, 0), &p, s, 1.0f, z);
    } else if (const ImTextureID tex = CaraDe(pg[size_t(i)].b3)) {   // sin cara de B2: la de B3 HD en el mismo aro
      Capa cp;
      cp.z = z;
      cp.tex = tex;
      cp.circ = true;
      // F8: marco y escala medidos en el aro nativo de B2 (celda 96, centro y radios de kAro*); apagado = 1.5.6.x.
      const bool datos = RuedaDatos();
      if (datos) { static bool avisado = false; if (!avisado) { avisado = true; REXLOG_INFO("dbz3 nave: aro de B2 medido activo (celda {:.0f}, centro {:.1f},{:.1f}, radio {:.1f}, grosor {:.1f})", kCaraCelda, kAroCx, kAroCy, kAroR, kAroG); } }
      cp.p[0] = L.P(x + (datos ? kAroCx : -1.0f) * s, y + (datos ? kAroCy : 8.0f) * s);
      cp.p[1] = ImVec2((datos ? kAroR : 21.0f) * s * L.s, (datos ? kAroG : 5.0f) * s * L.s);
      cp.uv[0] = ImVec2(0.0f, 0.0f);
      cp.uv[2] = ImVec2(1.0f, 1.0f);
      cp.col = IM_COL32_WHITE;
      g_capas.push_back(cp);
    }
  }
  {   // el modelo grande del elegido, en z = 1500 (entre las caras lejanas y las cercanas)
    const int sel = g.obj;
    const int id_b2 = sel < n - 1 ? pg[size_t(sel)].b2 : -1;
    // Decision del usuario (23/10): el modelo NATIVO de B3 con su pose de B3 para todo luchador que existe en B3 (ahorra IDs y evita
    // la pose de B2); el de B2 solo si el de B3 no se pudo generar (sin Python / data_cmn) o el luchador no esta en B3.
    const int b3_sel = sel < n - 1 ? pg[size_t(sel)].b3 : -1;
    bool b3_ok = b3_sel >= 0;
    if (b3_ok) {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      b3_ok = !g_modb3_fallo.count(b3_sel);
    }
    // B3M: con el modelo de B3 CON HUESOS (vista_b2.py) la figura va por la camara real de la rueda (PintaFig) con la animacion de B2
    int sk_est = 0;
    if (id_b2 >= 0 && b3_ok && c >= 25.0f && !std::getenv("DBZ3_SEL_FORZAR_B2")) {
      const SkB3* sk_ = nullptr;
      sk_est = SkEstado(FidB3(b3_sel, g_traje_rueda, FormaFigB3(b3_sel)), &sk_, IdFormaB3(b3_sel, FormaFigB3(b3_sel)));
    }
    g_fig_id = (id_b2 >= 0 && (!b3_ok || sk_est >= 1 || std::getenv("DBZ3_SEL_FORZAR_B2")) && c >= 25.0f) ? id_b2 : -1;
    g_fig_b3 = (b3_ok && g_fig_id < 0 && c >= 25.0f) ? b3_sel : -1;
    static const float ref = [] { const Fig& r = FigDe(0); return r.ok ? r.alto : 8.0f; }();
    g_fig_alto_ref = ref;
    const float alto_goku = std::getenv("DBZ3_SEL_ALTO") ? float(std::atof(std::getenv("DBZ3_SEL_ALTO"))) : 450.0f;
    g_fig_px = alto_goku * L.s * L.k;
    g_fig_L = L;
    g_fig_pie = L.P(std::getenv("DBZ3_SEL_X") ? float(std::atof(std::getenv("DBZ3_SEL_X"))) : 125.0f,
                    std::getenv("DBZ3_SEL_Y") ? float(std::atof(std::getenv("DBZ3_SEL_Y"))) : 500.0f);
    if (g_fig_id >= 0 || g_fig_b3 >= 0) {
      Capa cp;
      cp.z = 1500.0f;
      cp.fn = PintaFig;
      g_capas.push_back(cp);
    }
  }
  const ImVec2 o(161, 0);
  const float cc = std::max(0.0f, c - 20.0f);   // los rotulos entran tras media entrada del anillo (FUN_002AA280: 60 cuadros)
  if (SaliendoRueda()) {
    Spr2(L, g_rueda, 19, g_sel.c, false, o);
    Spr2(L, g_rueda, 23, g_sel.c, false, o);
    const int sel = g.obj, cara = sel == n - 1 ? 29 : pg[size_t(sel)].cara;
    const float alfa = PoseAma2(g_rueda.a[23], g_sel.c, false).a;
    if (cara >= 0) Spr2(L, g_rueda, cara == 0 ? 67 : 68 + cara, 0, false, o, nullptr, 1, alfa);
    return;
  }
  Obj2(L, g_rueda, 17, 18, cc, o);               // circulo azul bajo la cara (FUN_002AB* tabla 0x37AB50)
  Obj2(L, g_rueda, 21, 22, cc, o);               // la banda oscura del nombre
  const int i = g.obj;
  const int cara_sel = i == n - 1 ? 29 : pg[size_t(i)].cara;
  if (cara_sel >= 0) Obj2(L, g_rueda, -1, cara_sel == 0 ? 67 : 68 + cara_sel, cc, o);   // nombre nativo (anim 0x43 la primera, 0x44 + i las demas)
  Obj2(L, g_rueda, 13, 14, cc, o);                      // flechas
  const ImVec2 po(161, 296);
  Spr2(L, g_rueda, rotulo == 1 ? 35 : rotulo == 2 ? 119 : 118, 0, false, ImVec2(0, 0), &po, 1.0f, std::min(1.0f, cc / 15.0f), 1100.0f);   // 1P / 2P / COM
  (void)menu;
}

// Spr2 con la imagen (rect, textura, z) de `img` y el movimiento / escala / alfa de `mot`; `base_y` = la y de reposo de
// `mot` (la imagen se desplaza lo que `mot` se aparta de ella).
void Spr2b(const Lienzo& L, const Ama2& set, int img, int mot, float c, bool bucle, ImVec2 d, float base_y, float tinte_m) {
  if (img < 0 || mot < 0 || img >= int(set.a.size()) || mot >= int(set.a.size())) return;
  const Anim2 &I = set.a[size_t(img)], &M = set.a[size_t(mot)];
  if (I.p.empty() || I.k.empty() || M.k.empty()) return;
  const Pose2 q = PoseAma2(M, c, bucle);
  const Pz2& p = I.p.front();
  if (p.tex < 0 || p.tex >= int(set.tex.size()) || !set.tex[size_t(p.tex)].t) return;
  const Tex& t = set.tex[size_t(p.tex)];
  const float al = q.a;
  if (al <= 0.001f) return;
  const float x = I.k[0].x + d.x, y = I.k[0].y + (q.y - base_y) + d.y;
  Capa cp;
  cp.z = q.z;
  cp.tex = reinterpret_cast<ImTextureID>(t.t.get());
  if (&set == &g_burb) if (auto tex = TexturaBurbuja(p.tex, q.niebla)) cp.tex = tex;
  cp.p[0] = L.P(x + I.x0 * q.sx, y + I.y0 * q.sy);
  cp.p[1] = L.P(x + (I.x1 + 1) * q.sx, y + I.y0 * q.sy);
  cp.p[2] = L.P(x + (I.x1 + 1) * q.sx, y + (I.y1 + 1) * q.sy);
  cp.p[3] = L.P(x + I.x0 * q.sx, y + (I.y1 + 1) * q.sy);
  cp.uv[0] = ImVec2(p.u0 / t.w, p.v0 / t.h);
  cp.uv[1] = ImVec2(p.u1 / t.w, p.v0 / t.h);
  cp.uv[2] = ImVec2(p.u1 / t.w, p.v1 / t.h);
  cp.uv[3] = ImVec2(p.u0 / t.w, p.v1 / t.h);
  const ImU32 m = ImU32(std::clamp(q.m[0] * tinte_m, 0.0f, 1.0f) * 255.0f);
  cp.col = m | (m << 8) | (m << 16) | (ImU32(std::clamp(al, 0.0f, 1.0f) * 255.0f) << 24);
  g_capas.push_back(cp);
}
// ---- El menu Normal / Custom / Edit Skills (tabla 0x37ABA0): panel (10 -> 11) y tres palabras: cada una con su imagen
// (anims 0 / 8 / 9) y el movimiento de la 0 (entrada) y luego el de la 6 (la elegida: late 1,15 -> 1,05) o el de la 2 (apagada, a media luz)
void DibMenuRueda(const Lienzo& L, float c, int sel) {
  if (!g_rueda.ok()) return;
  const ImVec2 o(161, 0);
  const bool confirma = g_sel.fase == SelFase::menuConfirma;
  const bool cierra = confirma || g_sel.fase == SelFase::menuSale;
  if (cierra) Spr2(L, g_rueda, 12, g_sel.c, false, o);
  else Obj2(L, g_rueda, 10, 11, c, o);
  const int img[3] = {0, 8, 9};
  const float de = DurAma2(g_rueda, 0);
  for (int k = 0; k < 3; ++k) {
    if (cierra) Spr2b(L, g_rueda, img[k], k == sel ? 3 : 4, g_sel.c, false, o, 160.0f, 1.0f);
    else if (c < de) Spr2b(L, g_rueda, img[k], 0, c, false, o, 160.0f, 1.0f);
    else Spr2b(L, g_rueda, img[k], k == sel ? 6 : 2, c - de, k == sel, o, 160.0f, 1.0f);
  }
}
