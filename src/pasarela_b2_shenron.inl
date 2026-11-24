// Bloque de Shenron del Dragon World de B2 (datos y ayudas), extraido de pasarela_b2.cpp.
// Movimiento textual: mismo espacio de nombres (dbz3::pasarela::b2, anonimo), mismo orden de
// inicializacion de los globales y mismas dependencias. No cambiar el orden ni anadir logica aqui
// sin banco; la escena se dibuja en el bloque kDeseo de DibujarTablero (pasarela_b2.cpp).
std::vector<Tex> g_tdeseos, g_tshen_frases, g_tshen_marco;
int g_deseo = 0;
// Shenron en 3D: el modelo de B2 (CMN 327: #AMO hijo 0 con su #AMT hijo 1; el 328 es el sitio, las Bolas y la luz)
// pintado con el lector de modelos de PS2 de P4 (ps2_modelo.h) con una camara fija que lo encuadra entero.
ps2::Modelo g_shen_mod;
std::vector<ps2::Tex> g_shen_tx;
ps2::Tex g_shen_nubes;   // nubes del cielo de la escena del deseo (CMN 328 hijo 1, textura 0)
ps2::Modelo g_shen_sitio;   // la escena del deseo (CMN 328 hijo 0 + #AMT hijo 1), en las coordenadas de Shenron
std::vector<ps2::Tex> g_shen_sitio_tx;
ps2::Modelo g_shen_bolas;   // CMN 328 hijos 7 (#AMO de 57 huesos), 8 (#AMT) y 9 (#AMM): las 7 Bolas de la aparicion
std::vector<ps2::Tex> g_shen_bolas_tx;
ps2::Amp g_shen_bolas_amp, g_shen_luz_amp;
ps2::Modelo g_shen_luz;
std::vector<ps2::Tex> g_shen_luz_tx;
// PUL-B2 (11/11), orientacion de las Bolas a camara: en B2 el unico giro hacia la camara es el de los huesos del #AMO con
// bandera +0x30 & 0xC000 (FUN_00141B70: 0x8000 = desde la posicion del hueso, 0x4000 = eje -z de la vista; solo guinada).
// Ningun hueso de 328/7 (Bolas: 0x200/202/204/8000020F/C800020F), 328/11 (luz), 328/0 ni 328/15 la lleva: el original
// NO las orienta; su giro sale de su #AMM (328/9). Por eso el port no anade ninguna orientacion.
// true: las ranuras 5/6 (Bolas y columna de luz) se dibujan con su #AMP de la ISO; ya no hay 2D de brillo inventados.
constexpr bool kShenAmpValidado = true;
// false: la ranura 6 (columna de luz, CMN 328 hijos 11-14) apagada. Medido a los 9 s (cuadro 119 de su clip): los huesos
// EF_ORA02/03/ORAORA/ORA01 miden de 536 a 9960 unidades (la camara esta a 180 del suelo) y suman alfa 1,6 aditivo: velo
// amarillo a pantalla llena; ni la textura (alfa 24-180) ni el #AMP (estado 0 = visibles de 33 a 359) lo evitan.
// F7 (10/11): se queda como valor de compatibilidad; el dibujo usa ShenLuzB2() (dbz3_b2_shenron), y la luz ya sale con
// el alfa por vertice de ps2_modelo (dbz3_ps2_alfa_vertice), que es lo que faltaba para sus bordes suaves.
constexpr bool kShenLuz = false;

// F7: ajuste unico de la mejora de Shenron (OFF de serie; la fase final lo enciende desde la configuracion de prueba).
// - Bolas: se cargan con color de material (las estrellas son discos sin textura de material (0,8, 0, 0)) y con el
//   alfa REAL de sus texturas (el agujero de la cupula deja ver el disco naranja y las estrellas), no con alfa 110 fijo.
// - Luz: la columna de los hijos 11-14 se dibuja con el #AMP y el alfa por vertice de sus 8 mallas.
REXCVAR_DEFINE_BOOL(dbz3_b2_shenron, true, "DBZ3/Mods",
                    "Shenron de B2: Bolas con estrellas y alfa real, columna de luz con bordes suaves (experimental)");
// B2-P13 (12/11): el sitio (CMN 328 hijo 0 + #AMT hijo 1, cielo nocturno con nubes) sigue en escena cuando sale Shenron,
// como en el original (grabacion shenron_b2_S02c, fotos 53-70); antes solo se pintaba con las Bolas y quedaba fondo plano.
// Encendido de serie el 12/11 (coordinador): el sombreado plano del cuerpo es igual con el ajuste apagado o encendido.
REXCVAR_DEFINE_BOOL(dbz3_b2_shenron_original, true, "DBZ3/Dev",
                    "Shenron de B2: cielo nocturno del sitio tambien con Shenron (B2-P13)");
// POST/B2-P13-HD (12/11): Shenron con el modelo de B3 HD (data_cmn 534, sk_534.sk de vista_b2.py) y la toon de B3, animado con
// las anims de B2 de la escena por el sufijo de hueso (PonerShenronB3, pasarela_nave.cpp). Apagado: se queda el de PS2.
REXCVAR_DEFINE_BOOL(dbz3_b2_shenron_hd, false, "DBZ3/Dev", "Shenron de B2 con el modelo de B3 HD (B2-P13-HD)");
REXCVAR_DEFINE_BOOL(dbz3_b2_shenron_luz_alfa_pal, true, "DBZ3/Dev", "Conservar el alfa PS2 de la columna de luz de Shenron (CMN 328/12; se lee al cargar la escena)");
std::vector<uint8_t> g_shen_amo;   // #AMO de CMN 327 hijo 0 (nombres de huesos)
const ps2::Anim* g_shen_anim_uso = nullptr;   // anim e instante de este cuadro
float g_shen_t_uso = 0;
inline bool ShenBolasB2() {
  // El pintor de los triangulos sin textura con color por vertice necesita su propio ajuste (ps2_modelo.cpp); se
  // sincroniza una vez para que un solo interruptor encienda la escena entera.
  static const bool una = [] {
    if (REXCVAR_GET(dbz3_b2_shenron)) rex::cvar::SetFlagByName("dbz3_ps2_alfa_vertice", "true");
    return true;
  }();
  (void)una;
  return REXCVAR_GET(dbz3_b2_shenron);
}
inline bool ShenLuzB2() { return ShenBolasB2(); }
// Camara de la escena: #AMC del hijo 18 (5 clips: 420 Bolas, 360 subida con la luz, 290 y 240 Shenron, 2 final), que el
// #SPX 329 encadena con 0x50 (FUN_0022b070 -> FUN_0012c370) en su hilo 2: clip 0 en el cuadro 1, 1 en 421, espera 100,
// 2 en 881, 3 en 1171 y 4 en 1411. Claves {cuadro, ojo xyz} / {cuadro, objetivo xyz} / {cuadro, fov rad}, como el lector
// de pasarela.cpp (LeerCamaraCruda).
struct ClipCam {
  std::vector<std::array<float, 4>> ojo, obj;
  std::vector<std::array<float, 2>> fov;
  int nf = 0;
};
std::vector<ClipCam> g_shen_cam;
// Ranuras 3D del #SPX 329 (tabla 0x66CD90, 0x1100 por ranura): la ranura = el id del hijo en la cabecera del #AMB 328
// ({desplazamiento, tamano, tipo, id}): 0 sitio (hijos 0-6), 5 Bolas (7 #AMO, 8 #AMT, 9 #AMM, 10 #AMP), 6 luz (11 #AMO,
// 12 #AMT: sol, rayos, destello, estrias y la textura del haz; 13 #AMM; 14 #AMP), 7 hierba (15-17). Builtins (tabla
// 0x379640, despacho FUN_0014c740 caso 7, descompilados con DecompEn.java): 0x3A = FUN_0022cd90 y 0x33 = FUN_0022d3d0
// ponen la animacion de la ranura, 0x30 = FUN_0022e430 su actualizador, 0x36/0x37 = FUN_0022d000/0022cfb0 sus campos
// +0x10b4..0x10bc; 0xF0 = FUN_00226100 desplaza el UV de la ranura que lo pide (u +0,1 y v +0,01 por cuadro, 180 cuadros).
bool g_shen_cargado = false;
// PUL-B2 (dbz3_b2_fx_nativo): sonido de la escena. El #SPX 329 carga en el cuadro 1 el cuerpo VAG CMN 321 (0x15D), el
// indice HD CMN 322 (0x15C) y la secuencia SCEI CMN 323 (0x15B), y los pasa al IOP con 0xC6 (FUN_00227F60, banco) y 0xC5
// (FUN_002281F0 -> FUN_00147110, RPC 0x11, secuencia); ninguna otra orden del guion toca el sonido. Se sintetiza con el
// reproductor SEQ/VAG de IW (iw_midi.h, sin cambios): 4 notas tecla 60 (programas 0-3 = VAG 0-3, 22050 Hz) en los cuadros
// 4, 196, 292, 364 y 759 (480 PPQ, tempo 800000 us), 17,7 s a 48 kHz.
std::vector<int16_t> g_shen_son;
uint8_t g_shen_son_rutas = 3;
ps2::Modelo g_shen_hige[2];   // bigotes izquierdo/derecho: CMN 327 hijos 2/3 (#AMO LHIGE/RHIGE de 13 huesos, textura 0 del hijo 1)
ps2::Anim g_shen_hige_anim[2][3];   // sus animaciones: CMN 328 hijos 4 (izq.) y 5 (der.), 290/240/400 cuadros
ps2::Anim g_shen_anim[3];   // anims 0/1/2 del #AMM de CMN 328 hijo 3
int g_shen = 0;   // 0 "Te concedere un deseo", 1 la lista, 2 "Si, eso es aceptable", 3 "Ahora debo irme..."
// Deseo -> capsula de B3 (por nombre: SkillList.dson). Orden de la lista: 540 #0-5 (portal, trajes de Bulma 2-6), 539 #0-4
// (Fusion <Tiencha>, Pothala <Vegetto>(Vegeta), Pothala <Gokutan>(Goku), Pothala <Gokutan>(Mr. Satan), Pothala
// <Kibitoshin>), 538 #0-28 ("Avance" de cada personaje). Solo Pothala <Vegetto>(Vegeta) tiene equivalente: la 192
// (Potara <Vegito> de Vegeta). Sin equivalente en B3: Tiencha, Gokutan, Kibitoshin, los trajes de Bulma (no es
// luchadora en B3) y los 29 "Avance" (B3 no tiene esas capsulas).
// SHENRON §3.3: la tabla de recompensas vive en res/datos/b2_deseos_recompensa.toml (lector generico de datos.h).
// Solo el deseo 7 (Pothala <Vegeta>) -> capsula 192 esta acreditado; el resto queda a 0 (sin equivalente en B3, NO CONFIRMADO).
int DeseoB3(int k) {
  return int(dbz3::datos::Num("b2_deseos_recompensa", ("deseo.capsula_b3." + std::to_string(k)).c_str(), k == 7 ? 192 : 0));
}
// Sorteo de los deseos (FUN_0029b120, b2.c:242316-242425; relleno de las bolsas en LAB_0029b4b0, sin funcion en Ghidra).
// Tablas del ELF EU {bandera, requisitos, textura}: 0x37A560 (540: portal y trajes de Bulma, encadenados), 0x37A220 (538:
// "Avance" k = bandera 314+k, pide 461+k) y 0x37A4E0 (539). Las banderas son bytes de la partida (save+0x984+bandera); el
// port guarda las suyas en tablero_b2_dw<bandera>. ponytail: los requisitos que el port no produce (461-489, 253, 220) quedan
// a 0, como en una partida nueva de B2; si se averigua que los enciende, basta con escribir su clave.
// B2-P16 (dbz3_b2_requisitos_inventario, apagado; propuesta F6 02_solF6): esos 31 son indices del inventario de capsulas
// (FUN_00293740, orden 70/8: byte[0x699050+ID] += 1; FUN_00238320 lo copia a partida+0x984, donde el sorteo lee la
// "bandera"). Su productor es ganar la capsula: con el ajuste se consulta nave_b2_<ID>, el inventario que ya guarda
// GanarCapsula. Banco EE F6: 93/93 (31 IDs x nuevo / al maximo / ventana ocupada).
struct DeseoB2 { int bandera, req[3], nreq; };
const DeseoB2 kDeseos540[6] = {{503, {0}, 0}, {505, {503}, 1}, {506, {505}, 1}, {507, {506}, 1}, {508, {507}, 1}, {509, {508}, 1}};
const DeseoB2 kDeseos539[5] = {{237, {323, 324, 220}, 3}, {266, {318, 253}, 2}, {279, {314, 253}, 2}, {290, {326, 253}, 2},
                               {301, {328, 253}, 2}};
bool DwBandera(int f) {
  if (REXCVAR_GET(dbz3_b2_requisitos_inventario) && ((f >= 461 && f <= 489) || f == 253 || f == 220)) {
    const int n = LeerProgreso("nave_b2_" + std::to_string(f), 0);
    REXLOG_INFO("dbz3 tablero b2: B2-P16: requisito {} desde el inventario: {} capsula(s)", f, n);
    return n > 0;
  }
  if (f == 503 && LeerProgreso("tablero_b2_nave", 0) > 0) return true;   // portal ya pedido antes de este sorteo: se conserva
  return LeerProgreso("tablero_b2_dw" + std::to_string(f), 0) > 0;
}
int DeseoBandera(int k) { return k < 6 ? kDeseos540[k].bandera : k < 11 ? kDeseos539[k - 6].bandera : 314 + (k - 11); }
std::vector<int> g_filas;   // filas de la lista: indice de g_tdeseos (540 #0-5, 539 #0-4 en 6-10, 538 #0-28 en 11-39)
void SortearDeseos() {
  // FUN_00124ea8 (b2.c:242349): generador DISTINTO del LFSR de Bolas; no usar B2Rng.
  // La semilla original es NO CONFIRMADA; DBZ3_TEST_B2_DESEO_RNG solo fija la semilla para repetir capturas.
  static const uint64_t s0 = [] {
    if (const char* e = std::getenv("DBZ3_TEST_B2_DESEO_RNG")) return uint64_t(std::strtoull(e, nullptr, 0));
    return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
  }();
  static uint64_t s = s0;
  auto rnd = [] { s = s * 0x5851F42D4C957F2Dull + 1; return int((s >> 32) & 0x7FFFFFFF); };   // FUN_00124ea8
  auto cumple = [](const DeseoB2& d) {
    for (int r = 0; r < d.nreq; ++r)
      if (!DwBandera(d.req[r])) return false;
    return true;
  };
  int fija = -1, cand0 = 0;
  std::vector<int> c1, ok1, c2, ok2;   // 1 = 538, 2 = 539: candidatas (sin conceder) y con requisitos
  for (int i = 0; i < 6; ++i)
    if (!DwBandera(kDeseos540[i].bandera)) {
      ++cand0;
      if (fija < 0 && cumple(kDeseos540[i])) fija = i;
    }
  for (int k = 0; k < 29; ++k)
    if (!DwBandera(314 + k)) {
      c1.push_back(11 + k);
      if (DwBandera(461 + k)) ok1.push_back(11 + k);
    }
  for (int i = 0; i < 5; ++i)
    if (!DwBandera(kDeseos539[i].bandera)) {
      c2.push_back(6 + i);
      if (cumple(kDeseos539[i])) ok2.push_back(6 + i);
    }
  g_filas.clear();
  if (cand0 > 0 && fija >= 0) g_filas.push_back(fija);
  auto sacar = [&](std::vector<int>& v) {   // pool[i] = ultimo y c-- (FUN_0029b120)
    const int i = rnd() % int(v.size());
    g_filas.push_back(v[size_t(i)]);
    v[size_t(i)] = v.back();
    v.pop_back();
  };
  if (!ok1.empty())
    for (int k = ok2.empty() && ok1.size() > 1 ? 2 : 1; k > 0; --k) sacar(ok1);
  if (!ok2.empty())
    for (int k = g_filas.size() < 3 && ok1.empty() && ok2.size() > 1 ? 2 : 1; k > 0; --k) sacar(ok2);
  if (g_filas.size() < 3 && fija < 0 && (!ok1.empty() || !ok2.empty())) {   // una mas sin quitarla de su bolsa
    const auto& v = !c1.empty() ? ok1 : ok2;
    if (!v.empty()) g_filas.push_back(v[size_t(rnd() % int(v.size()))]);
  }
  if (g_filas.empty() && (!c2.empty() || !c1.empty())) {   // ninguna cumple: se ofrece una candidata al azar
    const auto& v = c1.empty() ? c2 : c1;
    g_filas.push_back(v[size_t(rnd() % int(v.size()))]);
  }
  if (g_filas.size() > 3) g_filas.resize(3);
  std::string s2;
  for (int f : g_filas) s2 += " " + std::to_string(f);
  REXLOG_INFO("dbz3 tablero b2: Shenron: sorteo de {} deseos:{}", g_filas.size(), s2);
}
void DarCapsulaB3(int c) {   // inventario de capsulas de la partida (u8 por ID en 0x824BA110 + 0x2AFE9; hasta 9)
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!c || !base) return;
  constexpr dbz3::GA kPartida = 0x824BA110;
  const auto a = kPartida + 0x2AFE9 + c;
  const uint8_t n = REX_LOAD_U8(a);
  if (n < 9) REX_STORE_U8(a, uint8_t(n + 1));
  REXLOG_INFO("dbz3 tablero b2: deseo: capsula {} de B3 ({} -> {})", c, n, std::min(9, n + 1));
}
