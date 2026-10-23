// dbz3 - Minijuegos de VUELO de Infinite World ("# FLIGHT #", frente R12, 2026-10-16): 7 (0x7D6) "Go to Guru's
// House! Don't get caught by Vegeta!" y 17 (0x7E0) "Race with Goten? Well, do your best!". Registrados en
// iw_minijuegos (RegistrarModulo). RE: docs/14_frentes/RONDA4_PULIDO.md "R12" y E:/DBZ3HD_trabajo/R12/.
//
// Del original (ELF USA, decomp EU 0x339620-0x345000; el modulo vive en *(gp-0x6FD0) = G):
//  - Carga (USA 0x33E040 / EU 0x33E540): variante v = 0 (Vegeta) o 1 (Goten) en G+0x4F0, personaje en G+0x4F4,
//    dificultad 0..5 en G+0x4F8. Personajes (0x482860): v0 Krilin 10 o Gohan nino 2 contra Vegeta 7 (traje 3);
//    v1 Gohan 4 o Trunks nino 9 contra Goten 6. Campo de vuelo (0x482940): v0 DATA_CMN 883 (MFNMCA, Namek: nubes,
//    cielo, 112 losetas de 4000 y la casa del Gran Patriarca, 883/7 `A1_SAITYOU`), v1 881 (MFEARA, Tierra). Datos:
//    DATA_CMN 891 (#AMB: aro RING_A01 3, moneda ITEMA11 7, objetos ITEMA02/03/08/10, flecha ITEMA04, #MSF 23 + v).
//  - #MSF: cabecera (+0x14 hover 16, +0x18/0x1C/0x20 empuje de los estados 3/5/6 = 10/34/56, +0x24 28, +0x28 10,
//    +0x34/0x38/0x3C freno 0.98, +0x44.. giro por estado en 1/65536 de vuelta {192, 128, 96, 96, 96}, +0x58/0x5C
//    duracion de los objetos 2 y 3 = 180, +0x60 + 4 (tipo & 0xFF) radio de recogida) y 6 bloques por dificultad
//    (+0xA8): salida {estado, giro, x, z}, meta {flags, tipo, x, z}, L1 (puntos de control 0x100), L2 (monedas y
//    objetos), +0x14 medidor inicial, +0x18/+0x1C/+0x20 subida del medidor (base, estado 5, estado 6), +0x24 cuadros
//    de congelado, +0x28 reaparicion, +0x2C puntos de moneda {1000, 500, 100}, +0x38 rangos en segundos (el 5.o es el
//    limite), +0x4C puntos por rango, +0x60 puntos al fallar.
//  - Vuelo (Viewer_StateFlyUpdate0/3/5/6, EU 0x340D70...): altura fija 400; la palanca vertical da la velocidad
//    (estado 0: hover x v|v|; 3/5/6: max(v x freno, empuje x v^2)), la horizontal gira (giro -= h x tasa), R1 (0x800)
//    = estados 5/6 (el 6 solo con el objeto 2 activo; el objeto 3 lo impide). Palanca: zona muerta 64, (v -+ 64) / 64
//    (Viewer_ReadControlAxes 0x33E8B0). El mundo da la vuelta (Node_WrapPositionInGrid): periodo 16000 / 24000.
//  - Objetos (Viewer_UpdateObjectCollection 0x33B7D0): en el plano, con barrido; v0 muestra los puntos de control de
//    uno en uno, v1 todos; con todos cogidos sale la meta. Monedas suman sus puntos, 2 = velocidad (estado 6), 3 = sin
//    R1, 8/0x108 = congela el medidor 300 cuadros.
//  - Rival = MEDIDOR (FlyingMinigameSpritesScripts 0x33C660): G+0x51C += base + extra del estado 5/6 por cuadro; a 1
//    Vegeta te atrapa / Goten llega antes. v0 ademas pierde al agotar el limite. En IW el rival solo sale en las
//    escenas; aqui se le ve volar con su modelo de B3 (ponytail: posicion derivada del medidor, ver Rival()).
//  - Resultado (Viewer_StateCheckResult/Success/Fail 0x33C190..): meta con t <= limite: rango = 4 - (primer umbral
//    de +0x38 que supera t), puntos = monedas + puntos del rango; si no, rango -1 y +0x60 puntos.
#include "reloj_prueba.h"
#include "iw_minijuegos.h"
#include "iw_mini_vuelo.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "cine.h"
#include "combate.h"
#include "escenarios.h"
#include "ingame/game_style.h"
#include "eu_map.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "ps2_modelo.h"
#include "suelo_iw.h"

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::iwmini {
// X de la palanca derecha (iw_minijuegos.cpp; netplay_hooks.cpp la guarda junto a la Y): el visor la usa en
// Pad_GetStickAxes (EU 0x3394F0), la de mayor magnitud de las dos.
int PalancaDerechaX();
}  // namespace dbz3::iwmini

namespace dbz3::iwmini {
// Hilo del juego, cada cuadro: lo llama Fotograma de iw_minijuegos.cpp (clima.cpp sub_820C2058, empieza el dibujo
// tras la logica: ahi la camara y los luchadores puestos son los que se pintan). Sin un vuelo en marcha no hace nada.
void PasoVuelo(uint8_t* base);
// Hilo del juego: Mando de iw_minijuegos.cpp (antes y despues de leer el mando `usuario`).
void MandoVuelo(uint8_t* base, uint32_t usuario, bool leido);
namespace {

namespace P = dbz3::pasarela;
using ps2::Modelo;
using ps2::Tex;
using ps2::V3;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kFin = 35569, kEstado = 35128, kVida = 6080, kVidaMax = 6084;
constexpr uint32_t kCopias[3] = {48, 64, 80};
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;   // P4: X+6062 = angulo - 15759, X+6074 = angulo - 15968
constexpr uint16_t kR1 = 0x0800;                       // registro del mando +12 (disposicion de PS2)
constexpr uint16_t kArriba = 0x0010, kDerecha = 0x0020, kAbajo = 0x0040, kIzquierda = 0x0080;   // +108
constexpr float kAltura = 400.0f;      // altura de vuelo de IW (la salida y los objetos: 0x43C80000)
constexpr float kPeriodo[2] = {16000.0f, 24000.0f};   // 4 x 4 / 6 x 6 losetas de 4000 (#MCF +0x14)
constexpr float kMedio = 2952.0f;      // ancho del campo de R8 (+-1476): el mundo de IW se junta a el
// B3 no deja a los luchadores a mas de ~150 (medido con DBZ3_R12_SONDA: con 553 el juego los juntaba 200 cada uno y el
// jugador se pintaba detras de la camara): el rival va como mucho a kRivalB3 del jugador.
constexpr float kRivalB3 = 120.0f;
constexpr uint32_t kOcultoA = 3261, kOcultoB = 4925;   // sin cuerpo A ni B (tagteam.cpp Veil; P4 lo usa en el pivote)

// Tipos de objeto del #MSF
constexpr int kMeta = 0, kControl = 0x100;
bool EsMoneda(int t) { return t == 0x201 || t == 0x101 || t == 1; }

struct Obj {
  int tipo = 0, flags = 0;
  float x = 0, z = 0, r = 1;
  bool activo = false;
  float reaparece = 0;
};

struct Juego {
  int clave = 0, fila = -1, v = 0, dif = 2, pj = 1;
  int jugador = 2, rival = 7, escenario = 7;
  // #MSF
  float hover = 16, empuje[3] = {10, 34, 56}, vmin = 28, vfreno = 10, freno[3] = {0.98f, 0.98f, 0.98f};
  int giro0 = 192, giro[3] = {96, 96, 96};
  float dur2 = 180, dur3 = 180;
  float radio[16] = {};
  float medidor0 = 0, sube[3] = {0, 0, 0}, congela = 300, reaparece = 120;
  int puntos_moneda[3] = {1000, 500, 100}, rango_s[5] = {60, 70, 80, 90, 110}, puntos_rango[5] = {}, puntos_fallo = 100;
  int salida_estado = 0, salida_giro = 0;
  float sx = 0, sz = 0;
  Obj meta;
  std::vector<Obj> objs;   // L1 (puntos de control) y luego L2
  int n_control = 0;
  float periodo = 16000, escala = 0.18f;
  bool campo = false, auto_ = false, vuelo_real = false;   // vuelo_real: el campo de vuelo 883/881 de R8
  // modelos de IW
  Modelo m_aro, m_moneda, m_flecha, m_casa, m_item[4];   // item: 2, 3, 8, 10
  P::Imagen clear;   // DATA_USA 334/3 t69 "CLEAR" (el mismo que P4)
  // estado (hilo del juego)
  float x = 0, z = 0, vel = 0, medidor = 0, congelado = 0, t568 = 0, t56c = 0, t_ms = 0;
  int yaw = 0, estado = 0, restantes = 0, monedas = 0, puntos = 0, cuadros = 0;
  int cam_yaw = -1;   // R7: rumbo de la camara, sigue a yaw a 1/16 por cuadro (Viewer_UpdateOrbitCamera L427262)
  // aplicar_estudios (entrega 16 P2): +0x604 cabeceo (palanca derecha Y) y +0x608 balanceo (palanca X) del visor de vuelo
  int cabeceo = -0x800, balanceo = 0;
  float cam_x = 0, cam_y = 0;   // ejes de Pad_GetStickAxes de este cuadro (0 fuera del mando)
  float zoom = 0, zoom_meta = 0;   // R7: +0x610 / +0x614 del visor de vuelo (distancia extra y su meta)
  int est_cam = 0;
  int virada = 0, media = 0;   // cuadros que quedan de la virada (0x321, 22) y de la media vuelta (0x32D, 44)
  size_t siguiente = 0;   // v0: punto de control visible
  bool abierta = false, colocado = false, fin = false, superado = false;
  std::deque<V3> rastro;  // Vegeta: por donde ha pasado el jugador (coordenadas de IW sin dar la vuelta)
  float ux = 0, uz = 0;   // jugador sin dar la vuelta (para el rastro)
  V3 rival_p;
  int rival_yaw = 0;
};

std::mutex g_mu;
std::shared_ptr<Juego> g_j;
// Mando del jugador como lo leyo el juego (hilo del juego: MandoVuelo lo guarda, PasoVuelo lo usa)
struct Pad {
  bool hay = false;
  uint16_t b12 = 0, b14 = 0, d108 = 0, d110 = 0;
  uint8_t p26 = 0, p27 = 0;
  uint32_t b16 = 0;            // +16/+18: otro campo de botones (medido con DBZ3_R12_SONDA)
  uint32_t cuenta[16] = {};    // +0x28 + 4 bit: cuadros pulsado (> 0) o desde que se solto (< 0); P4 pone -1 en +92
};
Pad g_pad, g_dejado;
std::atomic<bool> g_corre{false};
std::atomic<int> g_pj_mapa{-1};   // personaje de IW que lleva el jugador en el mapa (PersonajeDelMapa)

uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}
float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
void Poner(uint8_t* base, uint32_t x, const V3& p) {
  for (uint32_t o : kCopias) {
    StoreF(base, x + o, p.x);
    StoreF(base, x + o + 4, p.y);
    StoreF(base, x + o + 8, p.z);
  }
}
void Mirar(uint8_t* base, uint32_t x, int yaw) {
  REX_STORE_U16(x + kMiradaA, uint16_t(yaw - 15759));
  REX_STORE_U16(x + kMiradaB, uint16_t(yaw - 15968));
}
float Ang(int yaw) { return float(yaw & 0xFFFF) / 65536.0f * 6.2831853f; }
using dbz3::iwvuelo::Envolver;   // iw_mini_vuelo.h (mundo que da la vuelta)

uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
  return v;
}
float F32(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t u = U32(b, o);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

bool Cargar(Juego& j, std::string& error) {
  P::AfsPs2 cmn;
  if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) return error = "sin DATA_CMN.AFS de Infinite World", false;
  const std::vector<uint8_t> amb = cmn.Entrada(891);
  const std::vector<uint8_t> m = P::HijoAmb(amb, uint32_t(23 + j.v));
  if (m.size() < 0x200 || std::memcmp(m.data(), "#MSF", 4) != 0) return error = "sin #MSF en DATA_CMN 891", false;
  iwvuelo::Msf f;
  if (!iwvuelo::LeerMsf(m, j.dif, f, error)) return false;
  j.hover = f.hover;
  std::copy(f.empuje, f.empuje + 3, j.empuje);
  std::copy(f.freno, f.freno + 3, j.freno);
  std::copy(f.giro, f.giro + 3, j.giro);
  j.vmin = f.vmin;
  j.vfreno = f.vfreno;
  j.giro0 = f.giro0;
  j.dur2 = f.dur2;
  j.dur3 = f.dur3;
  std::copy(f.radio, f.radio + 16, j.radio);
  j.medidor0 = f.medidor0;
  std::copy(f.sube, f.sube + 3, j.sube);
  j.congela = f.congela;
  j.reaparece = f.reaparece;
  std::copy(f.puntos_moneda, f.puntos_moneda + 3, j.puntos_moneda);
  std::copy(f.rango_s, f.rango_s + 5, j.rango_s);
  std::copy(f.puntos_rango, f.puntos_rango + 5, j.puntos_rango);
  j.puntos_fallo = f.puntos_fallo;
  j.salida_estado = f.salida_estado;
  j.salida_giro = f.salida_giro;
  j.sx = f.sx;
  j.sz = f.sz;
  auto obj = [](const iwvuelo::Objeto& o) {
    Obj x;
    x.tipo = o.tipo;
    x.flags = o.flags;
    x.x = o.x;
    x.z = o.z;
    x.r = o.r;
    return x;
  };
  j.meta = obj(f.meta);
  j.objs.clear();
  for (const iwvuelo::Objeto& o : f.lista) j.objs.push_back(obj(o));
  j.n_control = f.n_control;
  // modelos de IW (891 y la meta del campo de vuelo: la casa del Gran Patriarca 883/7 o la de 881/7)
  j.m_aro = ps2::CargarModelo(amb, 3, 2, 0);
  j.m_moneda = ps2::CargarModelo(amb, 7, 6, 0);
  j.m_flecha = ps2::CargarModelo(amb, 1, 0, 0);
  j.m_item[0] = ps2::CargarModelo(amb, 11, 10, 0);
  j.m_item[1] = ps2::CargarModelo(amb, 14, 13, 0);
  j.m_item[2] = ps2::CargarModelo(amb, 17, 16, 0);
  j.m_item[3] = ps2::CargarModelo(amb, 20, 19, 0);
  j.m_casa = ps2::CargarModelo(cmn.Entrada(j.v == 0 ? 883 : 881), 7, 8, 0);
  if (P::AfsPs2 usa; usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) j.clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
  REXLOG_INFO("dbz3 r12: flecha {} triangulos, objetos {} {} {} {}", j.m_flecha.malla.tris.size(),
              j.m_item[0].malla.tris.size(), j.m_item[1].malla.tris.size(), j.m_item[2].malla.tris.size(),
              j.m_item[3].malla.tris.size());
  REXLOG_INFO("dbz3 r12: #MSF {} dificultad {}: salida ({:.0f}, {:.0f}) giro {}, meta ({:.0f}, {:.0f}), {} puntos de "
              "control y {} objetos, limite {} s, medidor {:.4f}/{:.4f}/{:.4f}; modelos aro {} moneda {} casa {}",
              23 + j.v, j.dif, j.sx, j.sz, j.salida_giro, j.meta.x, j.meta.z, j.n_control,
              j.objs.size() - size_t(j.n_control), j.rango_s[4], j.sube[0], j.sube[1], j.sube[2],
              j.m_aro.malla.tris.size(), j.m_moneda.malla.tris.size(), j.m_casa.malla.tris.size());
  return true;
}

// Pone la partida en la salida (READY y primer cuadro)
void Reiniciar(Juego& j) {
  j.x = j.ux = j.sx;
  j.z = j.uz = j.sz;
  j.yaw = j.salida_giro;
  if (const char* e = std::getenv("DBZ3_R12_SALIDA")) {   // pruebas: "x,z,giro" (p. ej. delante de un objeto)
    float x = 0, z = 0;
    int g = 0;
    if (std::sscanf(e, "%f,%f,%d", &x, &z, &g) == 3) j.x = j.ux = x, j.z = j.uz = z, j.yaw = g & 0xFFFF;
  }
  j.estado = j.salida_estado;
  j.vel = j.t568 = j.t56c = j.congelado = j.t_ms = 0;
  j.medidor = j.medidor0;
  j.monedas = j.puntos = j.cuadros = 0;
  j.cabeceo = -0x800, j.balanceo = 0;   // CameraFocusWhileFlying(52.2, -0x800): +0x604 = pitch, +0x608 = 0
  j.restantes = j.n_control;
  j.siguiente = 0;
  j.abierta = j.fin = j.superado = false;
  j.meta.activo = false;
  for (size_t k = 0; k < j.objs.size(); ++k) {
    Obj& o = j.objs[k];
    // v1 (flags del #MSF: todos visibles); v0: solo el primer punto de control (FlyingMinigameFuncs, G+0x504)
    o.activo = k >= size_t(j.n_control) || j.v == 1 || k == 0;
    o.reaparece = 0;
  }
  j.rastro.clear();
}

// Rival de B3 (ponytail: en IW el rival es solo el medidor del HUD y sale en las escenas): Vegeta va por el rastro
// del jugador a (1 - medidor) x kRivalB3 por detras (llega a el con el medidor lleno; con la camara a 52 solo se le ve
// cuando ya esta cerca); Goten recorre la salida, los puntos de control del #MSF en su orden y la meta, y va por la
// fraccion del medidor (1 = llega); lejos del jugador (> kRivalB3) no se le pinta.
void Rival(Juego& j) {
  auto en = [&](const V3& a, const V3& b, float u) { return V3{a.x + (b.x - a.x) * u, 0, a.z + (b.z - a.z) * u}; };
  if (j.v == 0) {
    float falta = (1.0f - std::clamp(j.medidor, 0.0f, 1.0f)) * kRivalB3 / j.escala;
    V3 p{j.ux, 0, j.uz}, d{0, 0, 0};
    for (auto it = j.rastro.rbegin(); it != j.rastro.rend(); ++it) {
      const float l = std::hypot(it->x - p.x, it->z - p.z);
      if (l >= falta && l > 0) {
        d = {p.x - it->x, 0, p.z - it->z};
        p = en(p, *it, falta / l);
        falta = 0;
        break;
      }
      falta -= l;
      d = {p.x - it->x, 0, p.z - it->z};
      p = *it;
    }
    if (falta > 0) p = {p.x - std::sin(Ang(j.yaw)) * falta, 0, p.z - std::cos(Ang(j.yaw)) * falta};
    j.rival_p = p;
    if (d.x != 0 || d.z != 0) j.rival_yaw = int(std::lround(std::atan2(d.x, d.z) / 6.2831853f * 65536.0f));
    else j.rival_yaw = j.yaw;
    return;
  }
  std::vector<V3> ruta{{j.sx, 0, j.sz}};
  for (int k = 0; k < j.n_control; ++k) {
    const V3& a = ruta.back();
    ruta.push_back({a.x + Envolver(j.objs[size_t(k)].x - a.x, j.periodo), 0, a.z + Envolver(j.objs[size_t(k)].z - a.z, j.periodo)});
  }
  {
    const V3& a = ruta.back();
    ruta.push_back({a.x + Envolver(j.meta.x - a.x, j.periodo), 0, a.z + Envolver(j.meta.z - a.z, j.periodo)});
  }
  float total = 0;
  for (size_t k = 1; k < ruta.size(); ++k) total += std::hypot(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z);
  float falta = std::clamp(j.medidor, 0.0f, 1.0f) * total;
  for (size_t k = 1; k < ruta.size(); ++k) {
    const float l = std::hypot(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z);
    if (falta <= l || k + 1 == ruta.size()) {
      j.rival_p = en(ruta[k - 1], ruta[k], l > 0 ? std::min(falta / l, 1.0f) : 0);
      j.rival_yaw = int(std::lround(std::atan2(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z) / 6.2831853f * 65536.0f));
      break;
    }
    falta -= l;
  }
}

// IW -> B3: el mundo de IW se junta al campo de R8 (+-1476) alrededor del centro; la altura, 400 x escala sobre el
// suelo del campo si lo hay
V3 AB3(const Juego& j, float x, float z) {
  const float rx = Envolver(x, j.periodo) * j.escala, rz = Envolver(z, j.periodo) * j.escala;
  // En los campos de vuelo 883/881 la logica de B3 pega al luchador a su suelo (sin #WCF: 0, el mar; medido con
  // DBZ3_R12_SONDA) y la camara del juego lo sigue alli: por eso, mientras dura el vuelo, la consulta de suelo del juego
  // responde 400 (suelo_iw::SueloFijo) y el luchador vuela "en el suelo" a la altura de IW.
  float y = kAltura * j.escala;
  if (j.campo) y = std::max(y, dbz3::suelo_iw::Altura(rx, rz, 0.0f) + 30.0f);
  return {rx, y, rz};
}

void Acabar(uint8_t* base, uint32_t mgr, Juego& j, bool meta, const char* por) {
  j.fin = true;
  j.superado = meta && j.t_ms <= float(j.rango_s[4]) * 1000.0f;   // Viewer_StateCheckResult
  REX_STORE_U8(mgr + kFin, uint8_t(j.superado ? 1 : 2));          // el combate acaba como un ring out
  REXLOG_INFO("dbz3 r12: fin ({}): tiempo {:.2f} s, medidor {:.3f}, monedas {} ({} puntos), superado {}", por,
              j.t_ms / 1000.0f, j.medidor, j.monedas, j.puntos, j.superado);
}

// Pose del jugador en cada estado del vuelo (auditoria7/J, visor de vuelo de IW: DATA_CMN 915; Viewer_StateFlyUpdate0
// 0x340D70, 3 0x3403A0, 5 0x33FEF0, Viewer_StatePoseBank5 0x33F040): reposo 0x320 (800), subir 0x328, bajar 0x324, de
// lado 0x329/0x32A, crucero 0x322, planeo 0x323 (palanca < 0.25), frenazo 0x32E, impulso con R1 0x32C. ponytail: sin la
// mezcla (CharModel_SetAnimBlend 1/5..1/20 cuadros); virada 0x321 y media vuelta 0x32D con sus cuadros (Volar)
uint32_t PoseVuelo(const Juego& j, float h, float v) {
  if (j.media > 0) return 0x32D;
  if (j.virada > 0) return 0x321;
  if (j.estado == 5 || j.estado == 6) return 0x32C;
  if (j.estado == 3) return v >= 0.25f ? 0x322 : v < 0 && j.vel >= j.vmin ? 0x32E : 0x323;
  if (v > 0) return 0x328;
  if (v < 0) return 0x324;
  if (h != 0) return h > 0 ? 0x329 : 0x32A;
  return 0x320;
}

// Un cuadro de vuelo (hilo del juego): Viewer_StateFlyUpdate0/3/5/6 con los numeros del #MSF.
void Volar(Juego& j, float h, float v, bool r1) {
  if (v < 0 && j.estado == 0) v *= 0.8f;
  // cambios de estado: R1 = 5 (6 con el objeto 2), sin R1 vuelve a 3; el objeto 3 impide R1
  const bool impulso = r1 && j.t56c <= 0 && v >= -0.75f;
  if (impulso) j.estado = j.t568 > 0 ? 6 : 5;
  else if (j.estado == 5 || j.estado == 6) j.estado = 3;
  else if (j.estado == 0 && v > 0.5f) j.estado = 3, j.virada = 22;   // la virada (estado 2, 0x321) y al crucero
  // media vuelta (estado 7, auditoria7/J): R1 con la palanca atras y sin lado; al acabar 0x32D, el rumbo da la vuelta
  if (j.media == 0 && j.estado == 3 && r1 && v < -0.75f && h == 0) j.media = 44;
  if (j.media > 0 && --j.media == 0) j.yaw ^= 0x8000;
  if (j.virada > 0) --j.virada;
  else if (j.estado == 3 && v <= 0 && j.vel < 0.5f) j.estado = 0;
  const int tasa = j.estado == 0 ? j.giro0 : j.giro[j.estado == 3 ? 0 : j.estado == 5 ? 1 : 2];
  j.yaw = (j.yaw - int(h * float(tasa))) & 0xFFFF;
  if (j.estado == 0) {
    j.vel = j.hover * v * std::fabs(v);
  } else {
    const int k = j.estado == 3 ? 0 : j.estado == 5 ? 1 : 2;
    const float empuje = v > 0 ? j.empuje[k] * v * v : 0.0f;
    j.vel = std::max(j.vel * j.freno[k], empuje);
    if (k == 0 && v < 0 && j.vel >= j.vmin) j.vel = j.vfreno;   // frenazo (0x32E: +0x560 = +0x28)
  }
  const float a = Ang(j.yaw), dx = std::sin(a) * j.vel, dz = std::cos(a) * j.vel;
  j.ux += dx;
  j.uz += dz;
  j.x = Envolver(j.x + dx, j.periodo);
  j.z = Envolver(j.z + dz, j.periodo);
}

// Objetos (Viewer_UpdateObjectCollection): barrido en el plano desde el sitio anterior
void Recoger(uint8_t* base, uint32_t mgr, Juego& j, float x0, float z0) {
  auto toca = [&](const Obj& o) {
    const float ax = Envolver(x0 - o.x, j.periodo), az = Envolver(z0 - o.z, j.periodo);
    const float bx = Envolver(j.x - o.x, j.periodo), bz = Envolver(j.z - o.z, j.periodo);
    const float sx = bx - ax, sz = bz - az, l2 = sx * sx + sz * sz;
    const float u = l2 > 0 ? std::clamp(-(ax * sx + az * sz) / l2, 0.0f, 1.0f) : 0.0f;
    const float cx = ax + sx * u, cz = az + sz * u;
    return cx * cx + cz * cz <= o.r * o.r;
  };
  for (size_t k = 0; k < j.objs.size(); ++k) {
    Obj& o = j.objs[k];
    if (!o.activo) {
      if (o.reaparece > 0 && (o.reaparece -= 1.0f) <= 0) o.activo = true;
      continue;
    }
    if (!toca(o)) continue;
    o.activo = false;
    if (o.flags & 0x4000) o.reaparece = j.reaparece;
    if (o.tipo == kControl) {
      --j.restantes;
      if (j.v == 0 && k + 1 < size_t(j.n_control)) j.objs[k + 1].activo = true, j.siguiente = k + 1;
      REXLOG_INFO("dbz3 r12: punto de control {} ({} quedan)", k, j.restantes);
      if (j.restantes <= 0 && !j.abierta) {
        j.abierta = j.meta.activo = true;
        REXLOG_INFO("dbz3 r12: meta abierta en ({:.0f}, {:.0f})", j.meta.x, j.meta.z);
      }
    } else if (EsMoneda(o.tipo)) {
      ++j.monedas;
      j.puntos += j.puntos_moneda[(o.tipo >> 4) % 3];
    } else if (o.tipo == 2) {
      j.t568 = j.dur2;
      j.t56c = 0;
    } else if (o.tipo == 3) {
      j.t56c = j.dur3;
      j.t568 = 0;
    } else if (o.tipo == 8 || o.tipo == 0x108) {
      j.congelado = j.congela;   // ponytail: en IW el efecto vuela al medidor antes de congelarlo
    }
    REXLOG_INFO("dbz3 r12: objeto {} (tipo 0x{:X}) cogido", k, o.tipo);
  }
  if (j.meta.activo && toca(j.meta)) Acabar(base, mgr, j, true, "META");
}

// Piloto de prueba ("auto"): gira hacia el objetivo y vuela con R1 mientras el medidor lo permite
void Piloto(const Juego& j, float& h, float& v, bool& r1) {
  const Obj* o = j.meta.activo ? &j.meta : nullptr;
  float mejor = 1e30f;
  for (int k = 0; k < j.n_control; ++k) {
    const Obj& c = j.objs[size_t(k)];
    if (!c.activo) continue;
    const float d = std::hypot(Envolver(c.x - j.x, j.periodo), Envolver(c.z - j.z, j.periodo));
    if (!j.meta.activo && d < mejor) mejor = d, o = &c;
  }
  if (!o) {
    h = 0, v = 1, r1 = false;
    return;
  }
  const float dx = Envolver(o->x - j.x, j.periodo), dz = Envolver(o->z - j.z, j.periodo);
  float dif = std::atan2(dx, dz) - Ang(j.yaw);
  while (dif > 3.14159265f) dif -= 6.2831853f;
  while (dif < -3.14159265f) dif += 6.2831853f;
  h = std::clamp(-dif * 4.0f, -1.0f, 1.0f);   // giro -= h: a la izquierda (dif > 0) con h < 0
  // radio de giro = vel / (tasa en radianes): si el objetivo queda dentro, frena para no dar vueltas a su alrededor
  const float d = std::hypot(dx, dz), radio = std::max(j.vel, 1.0f) / (96.0f / 65536.0f * 6.2831853f);
  v = std::fabs(dif) > 0.3f && d < radio * 2.0f ? 0.3f : std::fabs(dif) > 1.2f ? 0.6f : 1.0f;
  r1 = j.medidor < 0.6f && std::fabs(dif) < 0.15f && d > 1500.0f;
}

}  // namespace

// Hilo del juego (Mando de iw_minijuegos.cpp, gancho de combate.cpp sub_8208FD58): el vuelo se queda el mando y el
// luchador de B3 no lo ve (ni anda ni ataca: solo START y SELECT), como P4 con la palanca derecha y la cruceta. Antes de
// cada lectura se le devuelve al juego el registro como lo dejo (mantiene las direcciones de un cuadro a otro).
void MandoVuelo(uint8_t* base, uint32_t usuario, bool leido) {
  if (usuario != 0) return;
  const uint32_t r = kPads;
  if (!leido) {
    if (g_dejado.hay) {
      REX_STORE_U16(r + 12, g_dejado.b12);
      REX_STORE_U16(r + 14, g_dejado.b14);
      REX_STORE_U16(r + 108, g_dejado.d108);
      REX_STORE_U16(r + 110, g_dejado.d110);
      REX_STORE_U8(r + 26, g_dejado.p26);
      REX_STORE_U8(r + 27, g_dejado.p27);
      REX_STORE_U32(r + 16, g_dejado.b16);
      for (uint32_t k = 0; k < 16; ++k) REX_STORE_U32(r + 0x28 + 4 * k, g_dejado.cuenta[k]);
      g_dejado.hay = false;
    }
    return;
  }
  if (!g_corre.load()) return;
  g_pad = {true,          REX_LOAD_U16(r + 12), REX_LOAD_U16(r + 14), REX_LOAD_U16(r + 108), REX_LOAD_U16(r + 110),
           REX_LOAD_U8(r + 26), REX_LOAD_U8(r + 27)};
  g_pad.b16 = REX_LOAD_U32(r + 16);
  for (uint32_t k = 0; k < 16; ++k) g_pad.cuenta[k] = REX_LOAD_U32(r + 0x28 + 4 * k);
  g_dejado = g_pad;
  if (static const bool sonda = std::getenv("DBZ3_R12_SONDA") != nullptr; sonda && (g_pad.b12 & 0xFFF0)) {
    std::string w;
    for (uint32_t o = 0; o < 184; o += 4) w += fmt::format(" {:X}:{:08X}", o, REX_LOAD_U32(r + o));
    REXLOG_INFO("dbz3 r12: sonda mando{}", w);
  }
  constexpr uint16_t kDeja = 0x0009;   // SELECT 0x01 y START 0x08 (la pausa); el resto son golpes, ki, guardia...
  REX_STORE_U16(r + 12, uint16_t(g_pad.b12 & kDeja));
  REX_STORE_U16(r + 14, uint16_t(g_pad.b14 & kDeja));
  REX_STORE_U16(r + 108, 0);
  REX_STORE_U16(r + 110, 0);
  REX_STORE_U8(r + 26, 0);
  REX_STORE_U8(r + 27, 0);
  REX_STORE_U32(r + 16, g_pad.b16 & (uint32_t(kDeja) << 16 | kDeja));
  for (uint32_t k = 0; k < 16; ++k)   // los botones quitados, "sueltos" como hace P4 con el circulo (+92 = -1)
    if (!(kDeja >> k & 1) && int32_t(g_pad.cuenta[k]) > 0) REX_STORE_U32(r + 0x28 + 4 * k, uint32_t(-1));
}

void PasoVuelo(uint8_t* base) {
  if (!g_corre.load()) return;
  std::shared_ptr<Juego> jp;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    jp = g_j;
  }
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!jp || !mgr) return;
  Juego& j = *jp;
  const uint32_t estado = REX_LOAD_U32(mgr + kEstado);
  if (estado != 3 && estado != 4) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  std::lock_guard<std::mutex> lk(g_mu);
  if (estado == 3 || !j.colocado) {
    Reiniciar(j);
    if (estado == 4) {
      j.colocado = true;
      // vuelo: el scatto en el aire de IW (0x1D, R10). ponytail: el 0x320 del #BSK de 915 (auditoria7/F, visor de vuelo)
      // resulto ser una pose de pie (anim 73 del almacen comun, 36 cuadros; captura 20261016-133040): sin la de vuelo
      // vuelo de crucero de IW: 0x32C del visor de vuelo (DATA_CMN 915, su #AMM propio; cuerpo horizontal, capturas
      // 20261016-145048). DBZ3_IW_VUELO=<hex> otra del visor; =0 el scatto aereo de antes
      static const uint32_t pose = [] {
        const char* v = std::getenv("DBZ3_IW_VUELO");
        return v ? uint32_t(std::strtoul(v, nullptr, 16)) : 0x322u;
      }();
      if (pose) {
        dbz3::anim::Bucle(x0, 0x320);   // el jugador, en reposo hasta que se mueva (PoseVuelo)
        dbz3::anim::Bucle(x1, pose);
      } else {
        dbz3::anim::Scatto(x0, dbz3::anim::kBucle, true);
        dbz3::anim::Scatto(x1, dbz3::anim::kBucle, true);
      }
      REXLOG_INFO("dbz3 r12: empieza el vuelo en ({:.0f}, {:.0f}) giro {}", j.x, j.z, j.yaw);
    }
  } else if (!j.fin && !dbz3::GameDimmed() && !dbz3::OverlayOpen()) {
    // mando del jugador (registro 0): palanca +26/+27 (s8, arriba negativo) o direcciones +108 (teclado)
    // (lo guarda MandoVuelo al leer el juego el mando, antes de quitarselo al luchador de B3)
    const uint16_t d108 = g_pad.d108, dd = uint16_t(d108 | (d108 >> 8));
    int px = int(int8_t(g_pad.p26)), py = -int(int8_t(g_pad.p27));
    if (dd & (kArriba | kAbajo | kDerecha | kIzquierda)) {
      px = (dd & kDerecha) ? 127 : (dd & kIzquierda) ? -127 : 0;
      py = (dd & kArriba) ? 127 : (dd & kAbajo) ? -127 : 0;
    }
    float h = iwvuelo::EjeIW(px), v = iwvuelo::EjeIW(py);
    bool r1 = (g_pad.b12 & kR1) != 0;
    if (j.auto_) Piloto(j, h, v, r1);
    // Pad_GetStickAxes (EU 0x3394F0): X = la de mayor magnitud de las dos palancas (+0xD8/+0xDA); Y = la
    // derecha (+0xD9). Las dos por el mismo decodificado que la izquierda (EjeIW), tambien la Y: sin el,
    // cam_y valia x128 (el mando son -32768..32767) y +0x604 (cabeceo) saturado al tope con cualquier
    // toque (antes: j.cam_y = PalancaDerechaY() / 256). [H] signo de la Y sin medir contra IW.
    const float der_x = iwvuelo::EjeIW(std::clamp(dbz3::iwmini::PalancaDerechaX() / 256, -127, 127));
    const float der_y = iwvuelo::EjeIW(std::clamp(dbz3::iwmini::PalancaDerechaY() / 256, -127, 127));
    j.cam_x = std::fabs(der_x) > std::fabs(h) ? der_x : h;
    j.cam_y = der_y;
    const float ax = j.x, az = j.z;
    Volar(j, h, v, r1);
    if (static const bool poses = !std::getenv("DBZ3_IW_VUELO"); poses) dbz3::anim::Bucle(x0, PoseVuelo(j, h, v));
    j.rastro.push_back({j.ux, 0, j.uz});
    if (j.rastro.size() > 2400) j.rastro.pop_front();
    j.t568 = std::max(0.0f, j.t568 - 1.0f);
    j.t56c = std::max(0.0f, j.t56c - 1.0f);
    // medidor del rival (FlyingMinigameSpritesScripts): congelado o base + extra del estado 5/6
    if (j.congelado > 0) {
      j.congelado = std::max(0.0f, j.congelado - 1.0f);
    } else {
      j.medidor += j.sube[0] + (j.estado == 6 ? j.sube[2] : j.estado == 5 ? j.sube[1] : 0.0f);
      j.medidor = std::max(0.0f, j.medidor);
    }
    j.t_ms += 1000.0f / 60.0f;
    ++j.cuadros;
    Recoger(base, mgr, j, ax, az);
    if (!j.fin && j.medidor >= 1.0f) Acabar(base, mgr, j, false, j.v == 0 ? "Vegeta te atrapa" : "Goten llega antes");
    if (!j.fin && j.v == 0 && j.t_ms >= float(j.rango_s[4]) * 1000.0f) Acabar(base, mgr, j, false, "TIEMPO");
    static int traza = 0;
    if (++traza % 120 == 0)
      REXLOG_INFO("dbz3 r12: ({:.0f}, {:.0f}) giro {} estado {} vel {:.1f} medidor {:.3f} t {:.1f} s quedan {} zoom {:.1f} "
                  "cabeceo {} balanceo {}",
                  j.x, j.z, j.yaw, j.estado, j.vel, j.medidor, j.t_ms / 1000.0f, j.restantes, j.zoom, j.cabeceo, j.balanceo);
  }
  Rival(j);
  // los dos luchadores de B3 donde dice el vuelo, mirando hacia su marcha, sin KO; camara de CameraFocusWhileFlying
  V3 p = AB3(j, j.x, j.z), q = AB3(j, j.rival_p.x, j.rival_p.z);
  {   // el campo de R8 tiene mesetas mas altas que 400 x escala: se sube antes de llegar (60 u por delante) y sin saltos
    const float a0 = Ang(j.yaw);
    const V3 delante = AB3(j, j.x + std::sin(a0) * 60.0f / j.escala, j.z + std::cos(a0) * 60.0f / j.escala);
    static float y = 0;
    const float meta_y = std::max(p.y, delante.y);
    y = (estado == 3 || std::fabs(y - meta_y) > 200.0f) ? meta_y : y + (meta_y - y) * 0.12f;
    p.y = y;
    q.y = std::max(q.y, y);
  }
  // rival lejos (Goten por el recorrido): oculto y aparcado detras del jugador
  const bool lejos = std::hypot(q.x - p.x, q.z - p.z) > kRivalB3;
  if (lejos) q = {p.x - std::sin(Ang(j.yaw)) * 60.0f, p.y, p.z - std::cos(Ang(j.yaw)) * 60.0f};
  REX_STORE_U8(x1 + kOcultoA, lejos ? 1 : 0);
  REX_STORE_U8(x1 + kOcultoB, lejos ? 1 : 0);
  static int sonda = 0;
  if (std::getenv("DBZ3_R12_SONDA") && ++sonda % 30 == 0 && sonda < 1200) {
    float cam[3] = {}, mira[3] = {};
    dbz3::cine::LeerCamara(base, cam, mira);
    REXLOG_INFO("dbz3 r12: sonda estado {}: luchador ({:.1f}, {:.1f}, {:.1f}) puesto ({:.1f}, {:.1f}, {:.1f}) suelo {:.1f} "
                "camara del juego ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, {:.1f}, {:.1f})",
                estado, LoadF(base, x0 + 64), LoadF(base, x0 + 68), LoadF(base, x0 + 72), p.x, p.y, p.z,
                dbz3::suelo_iw::Altura(p.x, p.z, -999.0f), cam[0], cam[1], cam[2], mira[0], mira[1], mira[2]);
  }
  Poner(base, x0, p);
  Poner(base, x1, q);
  Mirar(base, x0, j.yaw);
  Mirar(base, x1, j.rival_yaw);
  StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));
  const float pie[3] = {p.x, p.y, p.z};
  float ojo[3], obj[3];
  // R7: la camara de IW no gira de golpe con el rumbo: +0x600 += (rumbo - camara) >> 4 por cuadro (EU L427262-298).
  // aplicar_estudios (Viewer_UpdateOrbitCamera EU L427242-427281, dt = 1 cuadro): cabeceo += -96 * Y, limitado a
  // [-0x1400, +0xC00]; balanceo hacia -2048 * X a 1/8 en los estados 2/3/5/6 y decae 7/8 en el resto.
  // ponytail: la X de la palanca derecha no se mira (netplay_hooks se la da a iwmini::PalancaDerecha); vale la izquierda.
  j.cabeceo = std::clamp(int(int16_t(uint16_t(j.cabeceo + int(int16_t(int(j.cam_y * -96.0f)))))), -0x1400, 0xC00);
  if (j.estado == 2 || j.estado == 3 || j.estado == 5 || j.estado == 6)
    j.balanceo += (int(int16_t(int(j.cam_x * -2048.0f))) - j.balanceo) >> 3;
  else
    j.balanceo = j.balanceo * 7 >> 3;
  j.cam_x = j.cam_y = 0;
  j.cam_yaw = j.cam_yaw < 0 ? j.yaw : (j.cam_yaw + (int(int16_t(uint16_t((j.yaw - j.cam_yaw) & 0xFFFF))) >> 4)) & 0xFFFF;
  // R7 quinta tanda, zoom de IW (Viewer_UpdateOrbitCamera EU L427252-427326; valores de L427394-427401): distancia =
  // 52,2 + z, z va hacia la meta m a +5 por cuadro (subiendo) y -1 (bajando); con z o m negativos, +0,5 / -1. Al entrar
  // en el impulso (estados 5/6, Viewer_StatePoseBank5/6) m = +28 (80,2); en 2/3/5/6 al llegar a 28 m = 0; fuera de 5/6
  // m > 0 pasa a 0. ponytail: sin m = -10 (42,2, picado 0x32E del estado 4, que el port no tiene).
  {
    const bool imp = j.estado == 5 || j.estado == 6, era = j.est_cam == 5 || j.est_cam == 6;
    if (imp && !era) j.zoom_meta = 28.0f;
    if ((j.estado == 3 || imp) ? 28.0f - 0.1f <= j.zoom : j.zoom <= -10.0f + 0.1f) j.zoom_meta = 0;
    if (!imp && j.zoom_meta > 0) j.zoom_meta = 0;
    const bool neg = j.zoom < 0 || j.zoom_meta < 0;
    const float sube = neg ? 0.5f : 5.0f, baja = 1.0f;
    j.zoom = j.zoom < j.zoom_meta ? std::min(j.zoom + sube, j.zoom_meta) : std::max(j.zoom - baja, j.zoom_meta);
    j.est_cam = j.estado;
  }
  iwvuelo::CamaraVuelo(pie, j.cam_yaw, ojo, obj, 8.0f, 52.2f + j.zoom, j.cabeceo);
  // FlyingZaxis(-roll) (L427328). [H] el signo: medido solo a ojo en capturas.
  dbz3::cine::Camara(base, ojo, obj, -float(j.balanceo) / 65536.0f * 6.2831853f);
}

namespace {

const ps2::Escena::Toon kToon{};   // toon de IW en los objetos (R8 / ps2_modelo)

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_corre.load()) return;
    std::shared_ptr<Juego> jp;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      jp = g_j;
    }
    uint8_t* base = Base();
    if (!jp || !base) return;
    const int st = P::EstadoCombate();
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (st < 3 || st > 8 || !mgr) return;   // iw_fix: hasta el 8: B3 pintaba su menu azul "Luchar otra vez?" ~1,7 s tras el fundido
    if (jp.get() != hecho_) {
      hecho_ = jp.get();
      tx_aro_ = ps2::Texturas(jp->m_aro);
      tx_moneda_ = ps2::Texturas(jp->m_moneda);
      tx_flecha_ = ps2::Texturas(jp->m_flecha);
      tx_casa_ = ps2::Texturas(jp->m_casa);
      for (int k = 0; k < 4; ++k) tx_item_[k] = ps2::Texturas(jp->m_item[k]);
      // ITEMA08_BLV_CHARACTER (891/16): t0 cara de Goten en su aro, t1 la flecha roja, t2 cara de Vegeta: el objeto 8
      // (congela el medidor) lleva la cara del rival; la malla apunta a la t0, en v0 va la de Vegeta
      if (jp->v == 0 && tx_item_[2].size() > 2) std::swap(tx_item_[2][0], tx_item_[2][2]);
      clear_ = ps2::Crear(jp->clear);
      fin_t_ = -1;
    }
    const Juego& j = *jp;
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    const ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    float cam_ojo[3] = {}, cam_mira[3] = {};
    const bool cam_ok = dbz3::cine::LeerCamara(base, cam_ojo, cam_mira);
    escena_.Empezar();
    float segundos = 0, barra = 1;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      const V3 yo = AB3(j, j.x, j.z);
      // cada objeto en su copia mas cercana al jugador (el mundo da la vuelta)
      auto sitio = [&](float x, float z) { return AB3(j, j.x + Envolver(x - j.x, j.periodo), j.z + Envolver(z - j.z, j.periodo)); };
      auto cerca = [&](const V3& s) { return std::hypot(s.x - yo.x, s.z - yo.z) < 3000.0f * j.escala; };   // niebla de IW
      const V3* flecha_a = nullptr;
      V3 objetivo;
      for (size_t k = 0; k < j.objs.size(); ++k) {
        const Obj& o = j.objs[k];
        if (!o.activo) continue;
        const V3 s = sitio(o.x, o.z);
        // flecha (G+0x544): v0 el primer punto de control visible de la lista, v1 el mas cercano
        if (o.tipo == kControl &&
            (!flecha_a || (j.v == 1 && std::hypot(s.x - yo.x, s.z - yo.z) < std::hypot(objetivo.x - yo.x, objetivo.z - yo.z))))
          objetivo = s, flecha_a = &objetivo;
        if (!cerca(s)) continue;
        const Modelo* mo = o.tipo == kControl ? &j.m_aro : EsMoneda(o.tipo) ? &j.m_moneda
                           : o.tipo == 2                 ? &j.m_item[0]
                           : o.tipo == 3                 ? &j.m_item[1]
                           : (o.tipo & 0xFF) == 8        ? &j.m_item[2]
                                                         : &j.m_item[3];
        const auto& tx = mo == &j.m_aro ? tx_aro_ : mo == &j.m_moneda ? tx_moneda_ : tx_item_[mo - j.m_item];
        // la malla va con la y minima en 0: el aro (radio 24) centrado a la altura del cuerpo (+8)
        const float y = mo == &j.m_aro ? s.y + 8.0f - 24.0f : s.y + 4.0f;
        // los objetos _BLV_ (ITEMA02/03/08/10) son carteles: de cara a la camara (ponytail: frente de la malla en +z)
        const float giro = mo == &j.m_aro || mo == &j.m_moneda || !cam_ok ? mo->giro.En(t)
                                                                           : std::atan2(cam_ojo[0] - s.x, cam_ojo[2] - s.z);
        if (mo->malla) escena_.Poner(cam, mo->malla, tx, {s.x, y, s.z}, giro, 1.0f, kToon);
      }
      if (j.meta.activo) {
        V3 s = sitio(j.meta.x, j.meta.z);
        objetivo = s;
        flecha_a = &objetivo;
        s.y = j.campo ? dbz3::suelo_iw::Altura(s.x, s.z, 0.0f) : 0.0f;
        // la casa del Gran Patriarca (883/7) / el objeto de 881/7: R8 los deja fuera del campo para el minijuego.
        // ponytail: en la meta (su sitio propio en el campo, por sus huesos, sin leer)
        if (j.m_casa.malla && cerca(s)) escena_.Poner(cam, j.m_casa.malla, tx_casa_, s, 0, 1.0f, kToon);
      }
      // flecha (ITEMA04_ARROW, World_CharacterLoader USA 0x33B1E8): matriz de la camara (0x40F800) por (0, -9, -45)
      // de 0x482AA0 = 45 por delante del ojo y 9 por debajo, a su tamano, girada hacia el objetivo (G+0x544: v0 el
      // punto de control visible, v1 el mas cercano, la meta al final). ponytail: sin la inclinacion -G+0x604.
      float fl[3];
      if (flecha_a && j.m_flecha.malla && iwvuelo::DelanteDeCamara(base, 45.0f, 9.0f, fl)) {
        const V3 f{fl[0], fl[1], fl[2]};
        // la punta de ITEMA04_ARROW va hacia -z de la malla (visto: con +z apuntaba al reves)
        const float g = std::atan2(flecha_a->x - f.x, flecha_a->z - f.z) + 3.14159265f;
        escena_.Poner(cam, j.m_flecha.malla, tx_flecha_, f, g, 1.0f, kToon);
      }
      segundos = j.v == 0 ? std::max(0.0f, float(j.rango_s[4]) - j.t_ms / 1000.0f) : j.t_ms / 1000.0f;
      barra = std::clamp(1.0f - j.medidor, 0.0f, 1.0f);
    }
    escena_.Tapar(cam, base, mgr + kX0);
    escena_.Tapar(cam, base, mgr + kX0 + kXPaso);
    escena_.Pintar(ImGui::GetBackgroundDrawList());
    HudIW(segundos, barra, j.jugador);   // F1: cara del que juega (PintarCara)
    // Al acabar: fundido a negro sobre el final de combate de B3 ("RING OUT!") con CLEAR si llego a la meta, como P4
    bool fin = false, meta = false;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      fin = j.fin;
      meta = j.superado;
    }
    if (!fin) return;
    if (fin_t_ < 0) fin_t_ = t;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * std::clamp((t - fin_t_) / 0.6f, 0.0f, 1.0f))));
    if (!meta || !clear_) return;
    const auto a = dbz3::ingame::style::GameArea(io);
    const float e = 6.0f * a.s * (0.8f + 0.2f * std::min(1.0f, (t - fin_t_) * 2.0f));
    const ImVec2 c = a.At(600, 430);
    fg->AddImage(clear_.id(), ImVec2(c.x - clear_.w * e * 0.5f, c.y - clear_.h * e * 0.5f),
                 ImVec2(c.x + clear_.w * e * 0.5f, c.y + clear_.h * e * 0.5f));
  }

 private:
  const Juego* hecho_ = nullptr;
  std::vector<Tex> tx_aro_, tx_moneda_, tx_flecha_, tx_casa_, tx_item_[4];
  ps2::Escena escena_;
  Tex clear_;
  float fin_t_ = -1;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

bool Empezar(int clave, int fila, int dificultad) {
  // I03 (studies 4): el vuelo ya se ha validado con el xex y la ISO europeos (0x7D6 y 0x7E0 PASS en EU), asi que
  // esta disponible en las dos regiones. Si la carga falla, Empezar devuelve false y el mapa sigue.
  const bool eu = dbz3::g_guest_eu;
  REXLOG_INFO("dbz3 iwmini vuelo region={} clave 0x{:X}: disponible", eu ? "eu" : "us", clave);
  auto j = std::make_shared<Juego>();
  j->clave = clave;
  j->fila = fila;
  j->v = clave == 0x7E0 ? 1 : 0;
  j->dif = std::clamp(dificultad, 0, 5);
  // Personaje (G+0x4F4, Viewer_BeginMinigameLoad/Alt): 1 si el descriptor de la mision (USA 0x5C05C0) +0x10 == 2 (v0) o
  // == 9 (v1), si no 0. Ese +0x10 lo pone Dragon Mission (USA 0x2FA558) = tabla 0x47D450 [DM+0x374] = el personaje que
  // el jugador lleva en el mapa (0 Goku, 2 Gohan nino, 9 Trunks nino, 10 Krilin...). Solo Gohan nino (v0) o Trunks nino
  // (v1) cambian: con cualquier otro van Krilin (v0) y Gohan (v1). El mapa (pasarela_misiones.cpp) lo pasa con
  // PersonajeDelMapa; DBZ3_IWVUELO_PJ=0/1 (pruebas) lo fuerza.
  const char* pj = std::getenv("DBZ3_IWVUELO_PJ");
  j->pj = pj ? std::clamp(std::atoi(pj), 0, 1) : g_pj_mapa.load() == (j->v == 0 ? 2 : 9) ? 1 : 0;
  constexpr int kJugador[2][2] = {{10, 2}, {4, 9}}, kRival[2] = {7, 6};
  j->jugador = kJugador[j->v][j->pj];
  j->rival = kRival[j->v];
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->periodo = kPeriodo[j->v];
  j->escala = kMedio / j->periodo;
  std::string error;
  if (!Cargar(*j, error)) {
    REXLOG_WARN("dbz3 r12: {}", error);
    return false;
  }
  // campo: el de vuelo de IW (883 / 881) si R8 lo instala (coordenadas de IW, escala 1; cajas de +-8000 / +-12000 con
  // paredes y sin vuelta: la vuelta del mundo la hace Volar/AB3, como IW); si no, el campo de IW
  // de R8 mas parecido (900 Namek, 903 llanura, juntando el mundo) y si tampoco, el escenario de B3 (Namek 7, Llanuras 5)
  int campo = dbz3::escenarios::CampoIW(j->v == 0 ? 883 : 881);
  j->vuelo_real = campo >= 0;
  if (j->vuelo_real) j->escala = 1.0f;
  else campo = dbz3::escenarios::CampoIW(j->v == 0 ? 900 : 903);
  j->campo = campo >= 0;
  j->escenario = campo >= 0 ? campo : (j->v == 0 ? 7 : 5);
  REXLOG_INFO("dbz3 r12: clave 0x{:X} (variante {}, dificultad {}): {} contra {}, escenario {} ({}), escala {:.3f}",
              clave, j->v, j->dif, j->jugador, j->rival, j->escenario, j->campo ? "campo de IW de R8" : "B3",
              j->escala);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_j = j;
  }
  dbz3::launcher::DeferUi(&CreatePanel);
  if (!dbz3::hub::RequestBattleEx(j->jugador, j->rival, j->escenario, true, -1, 0, 0, 0, 0)) {
    REXLOG_WARN("dbz3 r12: el conector no acepto el combate");
    return false;
  }
  dbz3::combate::SetMuneco(1);       // el rival sin IA ni ataques: lo mueve el medidor
  dbz3::combate::OcultarHud(true);   // sin marcador de B3: HUD de IW
  if (j->vuelo_real) dbz3::suelo_iw::SueloFijo(kAltura, true);   // el luchador a 400 (ver AB3)
  g_corre = true;
  P::Reglas r;
  r.capsulas = false;
  P::VigilarCombate("r12", r, [j](const P::Fin&) {
    g_corre = false;
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    dbz3::suelo_iw::SueloFijo(0.0f, false);
    if (uint8_t* base = Base())
      if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
        dbz3::anim::Scatto(mgr + kX0, dbz3::anim::kApagado);
        dbz3::anim::Scatto(mgr + kX0 + kXPaso, dbz3::anim::kApagado);
        dbz3::anim::Bucle(mgr + kX0, 0);
        dbz3::anim::Bucle(mgr + kX0 + kXPaso, 0);
      }
    Resultado res;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      res.clave = j->clave;
      res.fila = j->fila;
      res.superado = j->fin && j->superado;
      res.centesimas = int(j->t_ms / 10.0f);
      int i = 0;
      while (i < 4 && int(j->t_ms / 1000.0f) >= j->rango_s[i]) ++i;   // Viewer_StateResultSuccess
      res.rango = res.superado ? 4 - i : 0;
      res.puntos = res.superado ? j->puntos + j->puntos_rango[i] : j->puntos_fallo;
      res.monedas = j->monedas;
    }
    REXLOG_INFO("dbz3 r12: resultado superado {} rango {} puntos {} tiempo {} cs", res.superado, "CBASZ"[res.rango],
                res.puntos, res.centesimas);
    Terminar(res);
  });
  return true;
}

const bool g_registro = (RegistrarModulo(0x7D6, &Empezar), RegistrarModulo(0x7E0, &Empezar), true);

}  // namespace
}  // namespace dbz3::iwmini

// ---- iw_mini_vuelo.h: piezas del motor de vuelo para otros minijuegos (R13) ----
namespace dbz3::iwvuelo {
namespace {
uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
  return v;
}
float F32(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t u = U32(b, o);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
}  // namespace

bool LeerMsf(const std::vector<uint8_t>& m, int dificultad, Msf& j, std::string& error) {
  if (m.size() < 0x200 || std::memcmp(m.data(), "#MSF", 4) != 0) return error = "no es un #MSF", false;
  j.hover = F32(m, 0x14);
  for (int k = 0; k < 3; ++k) {
    j.empuje[k] = F32(m, 0x18 + 4 * size_t(k));
    j.freno[k] = F32(m, 0x34 + 4 * size_t(k));
    j.giro[k] = int(U32(m, 0x4C + 4 * size_t(k)));
  }
  j.vmin = F32(m, 0x24);
  j.vfreno = F32(m, 0x28);
  j.giro0 = int(U32(m, 0x44));
  j.dur2 = F32(m, 0x58);
  j.dur3 = F32(m, 0x5C);
  for (int k = 0; k < 16; ++k) j.radio[k] = F32(m, 0x60 + 4 * size_t(k));
  const std::vector<uint32_t> bl = Bloques(m, 0xA8);
  const size_t b = bl[size_t(std::clamp(dificultad, 0, 5))];
  if (!b || b + 0x64 > m.size()) return error = "bloque de dificultad fuera del #MSF", false;
  const size_t o_sal = U32(m, b), o_meta = U32(m, b + 4), o_l1 = U32(m, b + 8), o_l2 = U32(m, b + 12);
  j.medidor0 = F32(m, b + 0x14);
  for (int k = 0; k < 3; ++k) j.sube[k] = F32(m, b + 0x18 + 4 * size_t(k));
  j.congela = F32(m, b + 0x24);
  j.reaparece = F32(m, b + 0x28);
  for (int k = 0; k < 3; ++k) j.puntos_moneda[k] = int(U32(m, b + 0x2C + 4 * size_t(k)));
  for (int k = 0; k < 5; ++k) {
    j.rango_s[k] = int(U32(m, b + 0x38 + 4 * size_t(k)));
    j.puntos_rango[k] = int(U32(m, b + 0x4C + 4 * size_t(k)));
  }
  j.puntos_fallo = int(U32(m, b + 0x60));
  j.salida_estado = int(U32(m, o_sal));
  j.salida_giro = int(U32(m, o_sal + 4));
  j.sx = F32(m, o_sal + 8);
  j.sz = F32(m, o_sal + 12);
  auto obj = [&](size_t o) {
    Objeto x;
    x.flags = int(U32(m, o) & 0xFFFF);
    x.tipo = int(U32(m, o) >> 16);
    x.x = F32(m, o + 4);
    x.z = F32(m, o + 8);
    x.r = j.radio[x.tipo & 0xF];
    return x;
  };
  j.meta = obj(o_meta);
  j.lista.clear();
  for (size_t o : {o_l1, o_l2}) {
    const uint32_t n = U32(m, o);
    if (n > 256 || o + 4 + 12 * size_t(n) > m.size()) return error = "lista de objetos rota en el #MSF", false;
    for (uint32_t k = 0; k < n; ++k) j.lista.push_back(obj(o + 4 + 12 * size_t(k)));
    if (o == o_l1) j.n_control = int(n);
  }
  return true;
}

std::vector<uint32_t> Bloques(const std::vector<uint8_t>& g, uint32_t en, int n) {
  std::vector<uint32_t> v;
  for (int k = 0; k < n; ++k) {
    const uint32_t o = U32(g, en + 4 * size_t(k));
    v.push_back(o < g.size() ? o : 0);
  }
  return v;
}

float EjeIW(int v) { return v > 64 ? float(v - 64) / 64.0f : v < -64 ? float(v + 64) / 64.0f : 0.0f; }

float Envolver(float d, float periodo) {
  d = std::fmod(d + periodo * 0.5f, periodo);
  if (d < 0) d += periodo;
  return d - periodo * 0.5f;
}

void CamaraVuelo(const float p[3], int yaw, float ojo[3], float obj[3], float alto, float dist, int cabeceo) {
  const float a = float(yaw & 0xFFFF) / 65536.0f * 6.2831853f, c = -float(cabeceo) / 65536.0f * 6.2831853f;
  ojo[0] = p[0] - std::sin(a) * dist * std::cos(c);
  ojo[1] = p[1] + alto + dist * std::sin(c);
  ojo[2] = p[2] - std::cos(a) * dist * std::cos(c);
  obj[0] = p[0];
  obj[1] = p[1] + alto;
  obj[2] = p[2];
}

bool DelanteDeCamara(uint8_t* base, float adelante, float abajo, float out[3]) {
  float ojo[3], mira[3];
  if (!dbz3::cine::LeerCamara(base, ojo, mira)) return false;
  float fx = mira[0] - ojo[0], fy = mira[1] - ojo[1], fz = mira[2] - ojo[2];
  const float fl = std::max(std::sqrt(fx * fx + fy * fy + fz * fz), 1e-3f);
  fx /= fl, fy /= fl, fz /= fl;
  const float rl = std::max(std::hypot(fz, fx), 1e-3f), rx = -fz / rl, rz = fx / rl;   // derecha = f x arriba
  const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;                     // arriba = derecha x f
  out[0] = ojo[0] + fx * adelante - ux * abajo;
  out[1] = ojo[1] + fy * adelante - uy * abajo;
  out[2] = ojo[2] + fz * adelante - uz * abajo;
  return true;
}

bool Proyectar(uint8_t* base, float ancho, float alto, const float p[3], float& x, float& y, float& k) {
  const ps2::Camara cam = ps2::CamaraCombate(base, ImVec2(ancho, alto));
  ImVec2 o;
  if (!cam.ok || !cam.P(ps2::V3{p[0], p[1], p[2]}, o, k)) return false;
  x = o.x;
  y = o.y;
  return true;
}

}  // namespace dbz3::iwvuelo

namespace dbz3::iwvuelo {
void PersonajeDelMapa(int id_iw) { dbz3::iwmini::g_pj_mapa = id_iw; }
}  // namespace dbz3::iwvuelo
