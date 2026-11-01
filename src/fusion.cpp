// dbz3 - Fusion en combate (ronda 3, frente B; docs/14_frentes/RONDA3_ENJAMBRE.md).
//
// En B3 la fusion (Gogeta, Gotenks, Vegito) no es una forma: es una tecnica que cambia el personaje en pleno
// combate. Aqui igual, con el cambio de personaje del relevo del Tag Team (tagteam.cpp Fuse: cargas del juego,
// misma vida, pose de entrada = carga de ki): Yamcha (13) o Ten Shin Han (12), en un combate normal, con 3
// barras de ki o mas y sin forma a la que subir, MANTIENEN el boton de transformarse (P+K+G) 1 s y al soltar pasan a
// Yamhan (mod vis_b2_yamhan, nombre interno "YAMHAN"); gasta las 3 barras. Sin el mod, o online, no hace nada.
// Nova's Mods (nova_recetas.py): si el resultado trae capsula_fusion, hay que llevarla equipada; Yamhan puede salir mal
// (YAMHAN S, el delgado de B2: las dos filas de la misma capsula 237 en la tabla de formas del ELF de B2); Goku y Mr. Satan
// pasan a Gotan (Potara: B2 no tiene fila de fallo). ponytail: 1 de cada 4, sin medir la tasa de B2; se ajusta en kFallo.
// PRUEBA: DBZ3_TEST_FUSION=N[,1] -> el lado 1 se fusiona a los N s de pelea (con el ki que haga falta; ,1 = sale mal).
// Absorciones de Bu de B2: ver BuAlConectar. PRUEBA: DBZ3_TEST_BU_ABS=K -> siempre la absorcion K (0-3), 9 = la nativa.
#include <fstream>
#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "net/netplay.h"
#include "pasarela.h"
#include "roster_ext.h"

REXCVAR_DEFINE_BOOL(dbz3_fusion_nativa, true, "DBZ3/Combate", "Potara de Goku como la nativa: tecnica con el baile de Vegito, sin relevo");

REX_EXTERN(dbz3eu_sub_82103A78);
REX_EXTERN(dbz3eu_sub_821150B8);
REX_EXTERN(dbz3eu_sub_82115370);

namespace dbz3::tag { bool Fuse(PPCContext& ctx, uint8_t* base, int side, int id); }

namespace dbz3::fusion {
namespace {

constexpr dbz3::GA kMgr = 0x823D6CF4, kPadRec = 0x8238B780;   // luchador k: mgr + 2096 + 8240k; mando: 184 B por hueco
constexpr uint32_t kAct = 3244, kForm = 2980, kId = 7420, kPadSlot = 5028, kKi = 6100;
constexpr float kCost = 3000.0f;       // 3 barras (1 barra = 1000)
constexpr uint32_t kHold = 60;         // ticks manteniendo P+K+G
constexpr uint32_t kFallo = 25;        // % de fusiones que salen mal (si la receta tiene fallo)
struct Receta { int a, b; const char* ok; const char* fallo; };
constexpr Receta kRecetas[] = {{12, 13, "YAMHAN", "YAMHAN S"}, {0, 14, "GOTAN", nullptr}};

float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t v = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  REX_STORE_U32(a, v);
}

// ID del resultado en el roster de mods (roster.toml, nombre interno); -1 si el mod no esta
int IdDe(const char* nombre) { return dbz3::roster::IdPorNombre(nombre); }
const Receta* RecetaDe(uint32_t id) {
  for (const Receta& r : kRecetas)
    if (int(id) == r.a || int(id) == r.b) return &r;
  return nullptr;
}
// Con capsula_fusion, el luchador debe llevarla (lista del luchador X+7412: +52 cuantas, +54 IDs)
bool LlevaCapsula(uint8_t* base, uint32_t x, int to) {
  const int cap = dbz3::roster::FusionCapsule(uint32_t(to));
  if (cap <= 0) return true;
  const uint32_t lista = REX_LOAD_U32(x + 7412);
  if (!lista) return false;
  for (uint32_t k = 0, n = std::min<uint32_t>(REX_LOAD_U16(lista + 52), 7); k < n; ++k)
    if (REX_LOAD_U16(lista + 54 + 2 * k) == cap) return true;
  return false;
}

// P, K y G mantenidos en el mando del luchador (registro de 184 B: +12 pulsados, bit 0x100 << j; la configuracion
// del jugador da a cada j su boton: mgr + (hueco + 4382) * 8 + j = 1 P, 2 K, 4 G; como formas::PruebaMando)
bool PkgHeld(uint8_t* base, uint32_t mgr, uint32_t x, bool con_e = false) {
  const int16_t slot = int16_t(REX_LOAD_U16(x + kPadSlot));
  if (slot < 0 || slot > 3) return false;   // el COM no tiene mando
  const uint16_t held = REX_LOAD_U16(kPadRec + 184 * uint32_t(slot) + 12);
  static const bool probe = std::getenv("DBZ3_FUSION_SONDA") != nullptr;   // botones que ve el combate
  if (static uint16_t last = 0; probe && held != last) {
    last = held;
    REXLOG_INFO("dbz3 fusion sonda: hueco {} botones {:04X} (config {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X})", slot,
                held, REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8), REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 1),
                REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 2), REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 3),
                REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 4), REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 5),
                REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 6), REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + 7));
  }
  for (const uint8_t want : {1, 2, 4, 8}) {
    if (want == 8 && !con_e) break;
    bool ok = false;
    for (uint32_t j = 0; j < 8 && !ok; ++j)
      ok = REX_LOAD_U8(mgr + (uint32_t(slot) + 4382) * 8 + j) == want && (held & (0x100u << j));
    if (!ok) return false;
  }
  return true;
}

// la subida nativa (sub_82119108 solo calcula la forma a la que subiria P+K+G) no tiene adonde ir
bool NoFormLeft(PPCContext& ctx, uint8_t* base, uint32_t x) {
  PPCContext c = ctx;
  c.r3.u64 = x;
  c.r4.u64 = 0;
  DBZ3_CALL_EU(sub_82119108)(c, base);
  return c.r3.s32 <= int32_t(REX_LOAD_U32(x + kForm));
}

constexpr uint32_t kChar372 = 0x82329CF0;
constexpr int kPesoNativo = 30;
// caps: las capsulas del absorbido en B3 (las pide su #BCM, y Bu no las lleva): al absorber pasan a su lista de combate
struct Absorcion { const char* nombre; int peso; int victima, victima2; int excl[3]; uint16_t caps[7]; int ki; };
constexpr Absorcion kAbsorciones[] = {{"BU VEGETA", 15, 7, 7, {7, 72, -1}, {38, 39, 40, 41, 43, 44}, 6}, {"BU CELL", 20, 33, 33, {33, -1, -1}, {117, 118, 119}, 6},
                                      {"BU FREEZA", 20, 27, 27, {27, -1, -1}, {100, 101, 102}, 6}, {"BU TENYAM", 25, 12, 13, {12, 13, -1}, {63, 64, 65}, 5}};
std::array<const Absorcion*, 2> g_bu_hereda{};
// Voces del absorbido: el guion de su definitiva pide frases por indice en el banco del luchador (X+7924, el de sub_82109148); con el de Bu
// el indice se salia y el juego caia (medido: lr 0x82155A58, cola de sonido). Se pide al conectar; se pone al llegar a la forma.
constexpr uint32_t kLangFlag = 7920, kLang = 7924;
std::array<uint32_t, 2> g_bu_lang{};        // nodo pedido, aun sin poner
std::array<uint32_t, 2> g_bu_lang_puesto{}; // el del absorbido (se suelta al acabar el combate)
std::array<uint32_t, 2> g_bu_lang_suyo{};   // el de Bu
struct Puesto { uint32_t dir = 0, fid = 0; };
std::array<Puesto, 64> g_puestos{};
std::array<uint32_t, 2> g_bu_forma{};
std::array<bool, 2> g_bu_sorteado{};
void BuDevuelveFilas(uint8_t* base) {   // en orden inverso: una fila puede estar puesta dos veces
  for (auto p = g_puestos.rbegin(); p != g_puestos.rend(); ++p) {
    if (p->dir) REX_STORE_U32(p->dir, p->fid);
    *p = {};
  }
}
void BuRestaura(uint8_t* base) {
  BuDevuelveFilas(base);
  g_bu_sorteado = {};
  g_bu_hereda = {};
  g_bu_lang = {};
  g_bu_forma = {};
  dbz3::roster::ClearFormFiles(35);
}
// Pone la boca del modelo nuevo en todas las entradas de bocas del ID: la boca va aparte del modelo (c96 +12, 8 B por entrada) y con
// la de otro personaje se deforma al hablar (cada modelo ordena sus huesos de boca a su manera).
void BuPonBoca(uint8_t* base, uint32_t id, uint32_t fid) {
  const uint32_t c96 = 0x8234ABB8u + 96 * id, lista = REX_LOAD_U32(c96 + 12);
  const uint32_t n = std::max<uint32_t>(1, REX_LOAD_U8(c96 + 4)) * std::max<uint32_t>(1, REX_LOAD_U8(c96 + 6));
  for (uint32_t k = 0; lista && fid && k < n; ++k)
    for (Puesto& p : g_puestos)
      if (!p.dir) {
        p = {lista + 8 * k, REX_LOAD_U32(lista + 8 * k)};
        REX_STORE_U32(lista + 8 * k, fid);
        break;
      }
}
uint32_t BocaDe(uint8_t* base, uint32_t id, uint32_t forma) {
  const uint32_t c96 = 0x8234ABB8u + 96 * id, lista = REX_LOAD_U32(c96 + 12);
  return lista ? REX_LOAD_U32(lista + 8 * uint32_t(REX_LOAD_U8(c96 + 16 + 8 * forma + 3))) : 0;
}
uint32_t FilaFid(uint8_t* base, uint32_t id, uint32_t forma, uint32_t traje) {
  const uint32_t c96 = 0x8234ABB8u + 96 * id, lista = REX_LOAD_U32(c96 + 8);
  return lista ? lista + 12 * (traje * std::max<uint32_t>(1, REX_LOAD_U8(c96 + 5)) + forma) : 0;
}
// Pone en la fila de la forma nativa `id` el modelo de otra fila, solo durante este combate
void BuPon(uint8_t* base, uint32_t id, uint32_t forma, uint32_t traje, uint32_t fid) {
  const uint32_t dir = FilaFid(base, id, forma, traje);
  if (!dir || !fid) return;
  for (Puesto& p : g_puestos)
    if (!p.dir) {
      p = {dir, REX_LOAD_U32(dir)};
      REX_STORE_U32(dir, fid);
      return;
    }
}

}  // namespace

// Fusion por el camino nativo. El juego convierte al preparar el combate (sub_821007E8, filas en codigo A,C,B,F): Goku + 187 -> ficha 72 (Vegito),
// Goku + 167 -> ficha 68 (Gogeta); su arbol lanza la tecnica en modo hiper con P+K+G+E (codigo 0x259, ranura SPX 0: minijuego, baile y cambio de forma).
// Aqui, quien lleva la capsula del mod entra por esa misma fila, con la ficha prestada durante el combate: forma 0 el que empieza (con sus golpes),
// 1 el companero del baile, 2 el fallo y 3 el fusionado. El baile esta en los ficheros de la ficha, asi que quien no es Goku pasa antes a la forma 4
// (el mismo modelo con esos ficheros). ponytail: tabla compartida; solo un personaje por combate, y un rival con esa capsula nativa ve lo mismo.
struct Nativa { int a, pareja; const char* ok; const char* mal; int ficha; uint16_t cap_nativa; };
constexpr Nativa kNativas[] = {{0, 14, "GOTAN", nullptr, 72, 187}, {14, 0, "GOTAN", nullptr, 72, 187},
                               {12, 13, "YAMHAN", "YAMHAN S", 68, 167}, {13, 12, "YAMHAN", "YAMHAN S", 68, 167}};
constexpr uint32_t kFormaBaile = 4, kCodigoFusion = 0x259;
struct FilaPuesta { uint32_t dir, antes; };
std::array<FilaPuesta, 32> g_nativa{};
int g_nativa_ficha = -1, g_nativa_a = -1;
std::array<int, 2> g_baile{};   // 0 nada, 1 cargando la forma del baile, 2 ya lanzado
void NativaPon(uint8_t* base, uint32_t dir, uint32_t v) {
  for (FilaPuesta& f : g_nativa)
    if (!f.dir) {
      f = {dir, REX_LOAD_U32(dir)};
      REX_STORE_U32(dir, v);
      return;
    }
}
void NativaDevuelve(uint8_t* base) {
  for (auto it = g_nativa.rbegin(); it != g_nativa.rend(); ++it)
    if (it->dir) {
      REX_STORE_U32(it->dir, it->antes);
      *it = {};
    }
  if (g_nativa_ficha >= 0) dbz3::roster::ClearFormFiles(g_nativa_ficha);
  g_nativa_ficha = g_nativa_a = -1;
  g_baile = {};
}
uint32_t TecnicaDe(uint8_t* base, uint32_t id) { return REX_LOAD_U32(0x82333208u + 4 * id); }
void NativaFila(uint8_t* base, uint32_t r, uint32_t f, int id) {
  const uint32_t fila = r + 212 + 20 * f, g = kChar372 + 372 * uint32_t(id);
  NativaPon(base, fila, (REX_LOAD_U32(fila) & 0xFFFF0000u) | uint32_t(id));
  NativaPon(base, fila + 4, REX_LOAD_U32(fila + 4) & 0xFFFFu);   // su forma 0 en la tabla de modelos
  NativaPon(base, fila + 16, REX_LOAD_U32(g + 212 + 16));
  NativaPon(base, r + 4 * f, REX_LOAD_U32(g));
  dbz3::roster::SetFormFile(int((r - kChar372) / 372), int(f), int(TecnicaDe(base, uint32_t(id))), int(REX_LOAD_U32(g + 0x20)));
}
void NativaAlPreparar(uint8_t* base, uint32_t sel) {
  if (!REXCVAR_GET(dbz3_fusion_nativa) || dbz3::net::Session::Get().link() != dbz3::net::Link::kIdle) return;
  const int id = REX_LOAD_U16(sel + 16);
  for (const Nativa& n : kNativas) {
    if (n.a != id) continue;
    // PRUEBA (DBZ3_TEST_FUSION_EXITO): sin nadie que acierte el minijuego sale el fallo (forma 2); ahi el fusionado bueno, para verlo
    const int ok = IdDe(n.ok), mal = n.mal && !std::getenv("DBZ3_TEST_FUSION_EXITO") ? IdDe(n.mal) : ok;
    const int cap = ok >= 0 ? dbz3::roster::FusionCapsule(uint32_t(ok)) : 0;
    if (cap <= 0 || mal < 0) return;
    for (uint32_t k = 0, c = std::min<uint32_t>(REX_LOAD_U16(sel + 52), 7); k < c; ++k) {
      if (REX_LOAD_U16(sel + 54 + 2 * k) != cap) continue;
      if (g_nativa_a >= 0 && g_nativa_a != n.a) return;   // la ficha ya la usa otro en este combate
      REX_STORE_U16(sel + 54 + 2 * k, n.cap_nativa);
      REX_STORE_U16(sel + 16, 0);   // la fila nativa de la tabla es la de Goku
      if (g_nativa_a >= 0) return;
      const uint32_t r = kChar372 + 372 * uint32_t(n.ficha);
      if (n.a != 0) {   // empieza en la forma 4, con sus golpes; la 0 es la de la ficha (el guion del baile la da por protagonista), con su modelo
        const uint32_t f4 = r + 212 + 20 * kFormaBaile, g = kChar372 + 372 * uint32_t(n.a);
        NativaPon(base, f4, uint32_t(n.a));
        for (uint32_t q = 4; q < 16; q += 4) NativaPon(base, f4 + q, REX_LOAD_U32(r + 212 + q));
        NativaPon(base, f4 + 16, REX_LOAD_U32(g + 212 + 16));
        NativaPon(base, r + 4 * kFormaBaile, REX_LOAD_U32(g));
        dbz3::roster::SetFormFile(n.ficha, int(kFormaBaile), int(TecnicaDe(base, uint32_t(n.a))), int(REX_LOAD_U32(g + 0x20)));
        NativaPon(base, r + 0xD0, std::max<uint32_t>(REX_LOAD_U32(r + 0xD0), kFormaBaile + 1));
        NativaPon(base, r + 212, uint32_t(n.a));
        NativaPon(base, r + 212 + 16, REX_LOAD_U32(g + 212 + 16));
      }
      NativaPon(base, r + 212 + 20, uint32_t(n.pareja));   // el companero del baile
      NativaFila(base, r, 2, mal);
      NativaFila(base, r, 3, ok);
      g_nativa_ficha = n.ficha;
      g_nativa_a = n.a;
      REXLOG_INFO("dbz3 fusion: id {} lleva la capsula {}: ficha {} con {} (id {})", id, cap, n.ficha, n.ok, ok);
      return;
    }
    return;
  }
}
// Quien no es Goku: P+K+G+E en modo hiper con la capsula -> forma del baile y codigo de la tecnica, como el nodo del arbol de la ficha.
void NativaBaile(PPCContext& ctx, uint8_t* base, uint32_t mgr) {
  if (g_nativa_a <= 0) return;
  for (uint32_t p = 0; p < 2; ++p) {
    const uint32_t x = mgr + 2096 + 8240 * p;
    if (int(REX_LOAD_U32(x + kId)) != g_nativa_ficha) continue;
    PPCContext c = ctx;
    c.r3.u64 = x;
    const auto carga = [&](uint32_t forma) {
      c.r4.u64 = forma;
      DBZ3_CALL_EU(sub_82100A10)(c, base);
      c = ctx;
      c.r3.u64 = x;
      c.r4.u64 = forma;
      DBZ3_CALL_EU(sub_82100B78)(c, base);
      c = ctx;
      c.r3.u64 = x;
    };
    const auto lista = [&](uint32_t forma) {
      const uint32_t model = REX_LOAD_U32(x + 7608 + 4 * forma), nodo = REX_LOAD_U32(x + 7672 + 28 * forma + 20);
      if (!model || REX_LOAD_U32(model + 36) || (forma && nodo && REX_LOAD_U8(nodo + 1)) || (REX_LOAD_U16(x + kAct) > 2 && REX_LOAD_U16(x + kAct) != 15)) return false;   // el de la forma 0 esta cargado desde el principio (+1 sigue a 1)
      c.r4.u64 = forma;
      DBZ3_CALL2(sub_82115270, dbz3eu_sub_82115370)(c, base);   // el cambio de la nativa 0x91 (sub_82115330 no baja a la forma 0)
      return true;
    };
    if (g_baile[p] == 0) {
      // ponytail: en la forma 4 el juego no entra en modo hiper (sin averiguar por que); la tecnica sale directa con la misma pulsacion
      // y esa pulsacion puede haber lanzado ya otro golpe en este cuadro: sale al acabar, si es enseguida
      static std::array<int, 2> pulsada{};
      if (PkgHeld(base, mgr, x, true)) pulsada[p] = 60;
      else if (pulsada[p] > 0) --pulsada[p];
      // (accion 15: la entrada al modo hiper que esa pulsacion acaba de lanzar; se corta)
      const uint16_t accion = REX_LOAD_U16(x + kAct);
      if (!pulsada[p] || REX_LOAD_U32(x + kForm) != kFormaBaile || (accion > 2 && accion != 15) || LoadF(base, x + kKi) < kCost) continue;
      pulsada[p] = 0;
      carga(0);
      g_baile[p] = 1;
      REXLOG_INFO("dbz3 fusion: lado {}: carga el baile", p + 1);
      continue;
    }
    if (g_baile[p] == 1) {
      if (lista(0)) g_baile[p] = 2;
      continue;
    }
    if (g_baile[p] == 7) {   // en el baile, aun en la forma 0
      if (REX_LOAD_U32(x + kForm) != 0) g_baile[p] = 10;
      continue;
    }
    if (g_baile[p] == 10) {   // se acabo la fusion: el juego vuelve a la forma 0; a sus golpes
      if (REX_LOAD_U32(x + kForm) == 0 && REX_LOAD_U16(x + kAct) <= 2) {
        carga(kFormaBaile);
        g_baile[p] = 8;
      }
      continue;
    }
    if (g_baile[p] == 8) {
      if (lista(kFormaBaile)) g_baile[p] = 0;
      continue;
    }
    // los ficheros de la forma se ponen en cuadros siguientes al cambio (tecnicas_ext): el guion del baile tiene que estar ya
    if (++g_baile[p] <= 6) continue;
    const uint16_t antes = REX_LOAD_U16(x + kAct);
    if (const char* poke = std::getenv("DBZ3_TEST_FUSION_POKE"); poke) {   // PRUEBA: "desplazamiento:valor,..." escritos en la ficha del luchador justo antes del baile
      for (const char* q = poke; *q;) {
        const uint32_t off = uint32_t(std::atoi(q));
        while (*q && *q != ':') ++q;
        if (*q) ++q;
        REX_STORE_U32(x + off, uint32_t(std::atoi(q)));
        while (*q && *q != ',') ++q;
        if (*q) ++q;
      }
    }
    REX_STORE_U8(x + 8208, 1);     // modo hiper: de el depende el tiempo de la fusion (sub_82118DE0)
    REX_STORE_U16(x + kAct, 15);   // la accion de las definitivas
    REX_STORE_U16(x + 3246, antes);
    REX_STORE_U16(x + 3248, 0);
    c = ctx;
    c.r3.u64 = x;
    c.r4.u64 = 0;
    DBZ3_CALL2(sub_82103978, dbz3eu_sub_82103A78)(c, base);   // fin de la accion en curso, como cinematica.cpp
    REX_STORE_U32(x + 2928, kCodigoFusion);
    c = ctx;
    c.r3.u64 = x;
    DBZ3_CALL2(sub_82114FB8, dbz3eu_sub_821150B8)(c, base);
    REXLOG_INFO("dbz3 fusion: lado {}: empieza el baile", p + 1);
  }
}

void Tick(PPCContext& ctx, uint8_t* base) {
  static std::array<uint32_t, 2> hold{}, armed{};
  static uint32_t fight = 0;
  static const int test_s = std::getenv("DBZ3_TEST_FUSION") ? std::atoi(std::getenv("DBZ3_TEST_FUSION")) : -1;
  static const bool test_fallo = std::getenv("DBZ3_TEST_FUSION") && std::strchr(std::getenv("DBZ3_TEST_FUSION"), ',');
  const uint32_t mgr = REX_LOAD_U32(kMgr);
  if (!mgr || REX_LOAD_U32(mgr + 35128) != 4 || dbz3::net::Session::Get().link() != dbz3::net::Link::kIdle) {
    hold = {};
    armed = {};
    if (mgr && REX_LOAD_U32(mgr + 35128) >= 5) NativaDevuelve(base);
    // fin del combate: Bu recupera su banco antes de que el juego suelte el del luchador
    for (int p = 0; mgr && REX_LOAD_U32(mgr + 35128) >= 5 && p < 2; ++p) {
      const uint32_t x = mgr + 2096 + 8240 * uint32_t(p);
      if (g_bu_lang_puesto[size_t(p)]) {
        REX_STORE_U32(x + kLang, g_bu_lang_suyo[size_t(p)]);
        PPCContext c = ctx;
        c.r3.u64 = g_bu_lang_puesto[size_t(p)];
        DBZ3_CALL_EU(sub_82083A68)(c, base);
        g_bu_lang_puesto[size_t(p)] = 0;
      }
    }
    if (!mgr || REX_LOAD_U32(mgr + 35128) < 4) {
      if (fight) BuRestaura(base);
      fight = 0;
    }
    return;
  }
  ++fight;
  // las voces: en cuanto llegan y el cambio de forma ya ocurrio
  for (int p = 0; p < 2; ++p) {
    const uint32_t x = mgr + 2096 + 8240 * uint32_t(p), n = g_bu_lang[size_t(p)];
    if (!n || !g_bu_forma[size_t(p)] || REX_LOAD_U32(x + kForm) != g_bu_forma[size_t(p)] || REX_LOAD_U16(x + kAct) > 2 || REX_LOAD_U8(n + 1) || !REX_LOAD_U32(n + 192)) continue;
    g_bu_lang_suyo[size_t(p)] = REX_LOAD_U32(x + kLang);
    g_bu_lang_puesto[size_t(p)] = n;
    g_bu_lang[size_t(p)] = 0;
    REX_STORE_U32(x + kLangFlag, 0);
    REX_STORE_U32(x + kLang, n);
    REXLOG_INFO("dbz3 fusion: lado {}: voces del absorbido", p + 1);
  }
  // Bu ya en la forma absorbida (el cambio nativo, con el modelo bueno desde el principio): sus capsulas pasan a la lista de combate
  // del luchador (X+7412: +52 cuantas, +54 IDs; no es la de la partida). Las filas prestadas vuelven: si el rival es uno de esos IDs y
  // se transforma luego, debe ver las suyas.
  for (int p = 0; p < 2; ++p) {
    const uint32_t x = mgr + 2096 + 8240 * uint32_t(p);
    const Absorcion* a = g_bu_hereda[size_t(p)];
    if (!a || REX_LOAD_U32(x + kForm) != g_bu_forma[size_t(p)] || REX_LOAD_U16(x + kAct) > 2) continue;
    if (const uint32_t lista = REX_LOAD_U32(x + 7412)) {
      uint32_t n = 0;
      for (const uint16_t c : a->caps)
        if (c) {
          REX_STORE_U16(lista + 54 + 2 * n, c);   // la macro evalua la direccion dos veces: sin ++ dentro
          ++n;
        }
      REX_STORE_U16(lista + 52, uint16_t(n));
      REXLOG_INFO("dbz3 fusion: lado {}: {} en la forma {}: hereda {} capsulas", p + 1, a->nombre, g_bu_forma[size_t(p)], n);
    }
    g_bu_hereda[size_t(p)] = nullptr;
    BuDevuelveFilas(base);
  }
  NativaBaile(ctx, base, mgr);
  for (int p = 0; p < 2; ++p) {
    const uint32_t x = mgr + 2096 + 8240 * uint32_t(p);
    const uint32_t id = REX_LOAD_U32(x + kId);
    const bool test = p == 0 && test_s >= 0 && fight >= uint32_t(test_s) * 60 && fight < uint32_t(test_s) * 60 + 600;
    hold[size_t(p)] = PkgHeld(base, mgr, x) ? hold[size_t(p)] + 1 : 0;
    const Receta* receta = RecetaDe(id);
    if (receta && hold[size_t(p)] == kHold)
      REXLOG_INFO("dbz3 fusion: lado {} mantiene P+K+G (id {}, ki {:.0f}, accion {})", p + 1, id, LoadF(base, x + kKi),
                  REX_LOAD_U16(x + kAct));
    // mientras se mantiene el juego lo tiene en otra accion (16): la fusion sale al soltar (hasta 1,5 s despues)
    if (hold[size_t(p)] >= kHold) armed[size_t(p)] = fight + 90;
    const bool want = test || (armed[size_t(p)] && fight <= armed[size_t(p)]);
    if (!receta || REX_LOAD_U16(x + kAct) > 2 || !want) continue;
    if (test && LoadF(base, x + kKi) < kCost) StoreF(base, x + kKi, kCost);
    const int ok = IdDe(receta->ok);
    if (ok < 0 || !LlevaCapsula(base, x, ok) || LoadF(base, x + kKi) < kCost || !NoFormLeft(ctx, base, x)) {
      if (test && fight % 120 == 0)
        REXLOG_INFO("dbz3 fusion: prueba: no sale (id {}, capsula {}, ki {:.0f}, sin forma {})", ok, ok >= 0 && LlevaCapsula(base, x, ok), LoadF(base, x + kKi), NoFormLeft(ctx, base, x));
      continue;
    }
    int to = ok;
    if (const int mal = receta->fallo ? IdDe(receta->fallo) : -1; mal >= 0) {
      static uint32_t azar = uint32_t(fight * 2654435761u);
      azar = azar * 1664525u + 1013904223u + fight;
      if (test ? test_fallo : (azar >> 16) % 100 < kFallo) to = mal;
    }
    if (!dbz3::tag::Fuse(ctx, base, p, to)) {
      if (test && fight % 120 == 0) REXLOG_INFO("dbz3 fusion: prueba: el relevo no acepta");
      continue;
    }
    StoreF(base, x + kKi, LoadF(base, x + kKi) - kCost);
    hold[size_t(p)] = armed[size_t(p)] = 0;
    REXLOG_INFO("dbz3 fusion: lado {}: id {} -> {} (id {}{}), ki que queda {:.0f}{}", p + 1, id, receta->ok, to, to == ok ? "" : ", sale mal",
                LoadF(base, x + kKi), test ? " (prueba)" : "");
  }
}

// Medido en B2 (PCSX2, FUN_001ef5b0, tablas ELF 0x393B38/0x393B40): al conectar la definitiva se sortea con pesos a quien absorbe y
// los del rival NO entran (contra Cell nunca Bu con Cell). Aqui el sorteo de B2 sobre lo que B3 no tiene; el resto de pesos (Gotenks 10,
// Gohan 10, y Piccolo, que B2 no tiene, otros 10) deja el sorteo nativo. ponytail: el peso de Piccolo no sale de ningun juego (kPesoNativo).
// La orden de guion 0x17 (sub_82100C10) solo se da con el golpe conectado.
void BuAlConectar(PPCContext& ctx, uint8_t* base, uint32_t x, uint32_t forma) {
  if (dbz3::net::Session::Get().link() != dbz3::net::Link::kIdle) return;   // el sorteo usa el reloj de cada PC: online, lo nativo
  const uint32_t mgr = REX_LOAD_U32(kMgr);
  if (!mgr || x < mgr + 2096 || REX_LOAD_U32(x + kId) != 35 || forma < 1 || forma > 3) return;
  const uint32_t p = (x - (mgr + 2096)) / 8240;
  if (p > 1 || g_bu_sorteado[p]) return;
  g_bu_sorteado[p] = true;
  const uint32_t rival = REX_LOAD_U32(mgr + 2096 + 8240 * (1 - p) + kId);
  int total = kPesoNativo, ids[4];
  for (int k = 0; k < 4; ++k) {
    const Absorcion& a = kAbsorciones[k];
    const bool suyo = int(rival) == a.excl[0] || int(rival) == a.excl[1] || int(rival) == a.excl[2];
    ids[k] = suyo ? -1 : IdDe(a.nombre);
    if (ids[k] >= 0) total += a.peso;
  }
  if (total == kPesoNativo) return;   // sin el mod: lo nativo, sin tocar nada
  static uint32_t azar = uint32_t(mgr ^ REX_LOAD_U32(0x8245EFC0u));
  azar = azar * 1664525u + 1013904223u + uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
  int t = int((azar >> 16) % uint32_t(total));
  static const int forzado = std::getenv("DBZ3_TEST_BU_ABS") ? std::atoi(std::getenv("DBZ3_TEST_BU_ABS")) : -1;   // PRUEBA
  int elegido = -1;
  for (int k = 0; k < 4 && elegido < 0; ++k) {
    if (ids[k] < 0) continue;
    if (forzado >= 0 ? forzado == k : t < kAbsorciones[k].peso) elegido = k;
    t -= kAbsorciones[k].peso;
  }
  if (elegido < 0) {
    REXLOG_INFO("dbz3 fusion: Bu (lado {}) conecta contra el id {}: absorbe con la forma nativa {}", p + 1, rival, forma);
    return;
  }
  const Absorcion& a = kAbsorciones[elegido];
  const uint32_t traje = REX_LOAD_U16(0x8245EFC0u + 16 + 80 * p + 18);
  // Filas de Bu en la tabla de formas (evid_tabla_b3): 1-3 = 84, 78, 79; 4-6, la victima = 11 m0, 64 m2, 4 m2
  constexpr uint32_t kFormaId[] = {0, 84, 78, 79}, kVictima[] = {0, 11, 64, 4}, kVictimaM[] = {0, 0, 2, 2};
  if (const uint32_t d = FilaFid(base, uint32_t(ids[elegido]), 0, 0)) BuPon(base, kFormaId[forma], 0, traje, REX_LOAD_U32(d));
  BuPonBoca(base, kFormaId[forma], BocaDe(base, uint32_t(ids[elegido]), 0));
  // la victima de la cinematica: la forma final de Freezer y Cell; Ten y Yamcha los dos
  const uint32_t fv = a.victima == 27 ? 3 : a.victima == 33 ? 2 : 0;
  if (const uint32_t d = FilaFid(base, uint32_t(a.victima), fv, 0)) {
    // todas las filas del ID de la victima: la m2 de la tabla depende del traje que elija el juego para esa forma
    const uint32_t c96 = 0x8234ABB8u + 96 * kVictima[forma], filas = uint32_t(REX_LOAD_U8(c96 + 4)) * std::max<uint32_t>(1, REX_LOAD_U8(c96 + 5));
    for (uint32_t k = 0; k < filas; ++k) BuPon(base, kVictima[forma], k, 0, REX_LOAD_U32(d));
    BuPonBoca(base, kVictima[forma], BocaDe(base, uint32_t(a.victima), fv));
    REXLOG_INFO("dbz3 fusion: victima: {} filas del id {} -> modelo {}", filas, kVictima[forma], REX_LOAD_U32(d));
  }
  if (a.victima2 != a.victima)
    if (const uint32_t d = FilaFid(base, uint32_t(a.victima2), 0, 0)) BuPon(base, 90, 2, 0, REX_LOAD_U32(d));
  // su escena de absorcion (animaciones de B2): el mod guarda una por forma nativa en sus filas 1-3 (+4 * forma del registro de 372 B)
  const uint32_t rec = REX_LOAD_U32(x + 7416);
  if (const uint32_t anm = rec ? REX_LOAD_U32(kChar372 + 372 * uint32_t(ids[elegido]) + 4 * forma) : 0xFFFFFFFFu; anm != 0xFFFFFFFFu && anm) {
    for (Puesto& q : g_puestos)
      if (!q.dir) {
        q = {rec + 4 * forma, REX_LOAD_U32(rec + 4 * forma)};
        REX_STORE_U32(rec + 4 * forma, anm);
        break;
      }
    REXLOG_INFO("dbz3 fusion: escena de absorcion de B2: forma {} -> fichero {}", forma, anm);
  }
  // sus golpes van en ese mismo fichero (BSK de la forma); arbol de golpes, guion y efectos de tecnica, los nativos del absorbido
  dbz3::roster::SetFormFile(35, int(forma), int(REX_LOAD_U32(0x82333208u + 4 * uint32_t(a.victima))), int(REX_LOAD_U32(kChar372 + 372 * uint32_t(a.victima) + 0x20)));
  // barras de ki a las que tiende la forma: +9 de su entrada (char372 +212 + 20 * forma; ver roster_ext)
  if (rec) {
    const uint32_t d = rec + 212 + 20 * forma + 8;
    for (Puesto& q : g_puestos)
      if (!q.dir) {
        q = {d, REX_LOAD_U32(d)};
        REX_STORE_U32(d, (q.fid & 0xFF00FFFFu) | (uint32_t(a.ki) << 16));
        break;
      }
  }
  g_bu_hereda[p] = &a;
  g_bu_forma[p] = forma;
  {
    PPCContext c = ctx;
    c.r3.u64 = REX_LOAD_U32(kChar372 + 372 * uint32_t(a.victima) + 40) | 0x30000u;   // como sub_82109148: AFS 3 = lang_*
    c.r4.u64 = 0;
    c.r5.u64 = 1;
    DBZ3_CALL_EU(sub_82083968)(c, base);
    g_bu_lang[p] = c.r3.u32 != 0xFFFFFFFFu ? c.r3.u32 : 0;
  }
  REXLOG_INFO("dbz3 fusion: Bu (lado {}) conecta contra el id {}: absorbe {} (id {}, sobre la forma {})", p + 1, rival, a.nombre, ids[elegido], forma);
}

// PRUEBA: DBZ3_TEST_ULT=1 -> el lado 1 pulsa P+K+G+E cada segundo si esta libre (entra en modo hiper y lanza la definitiva),
// como manejarlo a mano sin depender de que la IA se decida; antes pone al rival a 30. =2: lo aleja (definitiva fallada).
// Registro del mando: como formas::PruebaMando.
void PruebaMando(uint8_t* base, uint32_t user) {
  // PRUEBA: DBZ3_TEST_ESP=1..5 -> el lado 1 pulsa cada 2 s direccion + E (9 las recorre; 1 E sola, 2 derecha, 3 izquierda, 4 arriba, 5 abajo):
  // la entrada real de una tecnica especial (como el boton de cancelacion de combate.cpp); ki lleno, rival a 30. Solo via pruebas.py.
  if (static const int esp0 = std::getenv("DBZ3_TEST_ESP") ? std::atoi(std::getenv("DBZ3_TEST_ESP")) : 0; esp0 > 0 && user == 0) {
    static uint32_t t = 0;
    const int esp = esp0 == 9 ? int((t / 120) % 5) + 1 : esp0;   // 9: recorre las cinco
    const uint32_t m = REX_LOAD_U32(kMgr);
    if (m && REX_LOAD_U16(m + 2096 + kAct) <= 2 && ++t % 120 == 0) {
      StoreF(base, m + 2096 + kKi, 7000.0f);
      const uint32_t pad = REX_LOAD_U16(m + 2096 + kPadSlot), r = kPadRec;
      for (uint32_t j = 0; j < 8; ++j)
        if (REX_LOAD_U8(m + (pad + 4382) * 8 + j) == 8) {
          const uint16_t eb = uint16_t(0x100u << j), dir = uint16_t(esp == 2 ? 0x20 : esp == 3 ? 0x80 : esp == 4 ? 0x10 : esp == 5 ? 0x40 : 0);
          REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | eb));
          REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | eb));
          REX_STORE_U32(r + 72 + 4 * j, 1);
          REX_STORE_U16(r + 108, uint16_t(REX_LOAD_U16(r + 108) | dir));
          REX_STORE_U16(r + 110, uint16_t(REX_LOAD_U16(r + 110) | dir));
          REX_STORE_U8(r + 26, uint8_t(esp == 2 ? 0x7F : esp == 3 ? 0x81 : 0));
          REXLOG_INFO("dbz3 fusion: prueba especial: direccion {:02X} + E (cod {:X})", dir, REX_LOAD_U32(m + 2096 + 2928));
          break;
        }
    }
  }
  // PRUEBA: DBZ3_TEST_FUSION_PULSOS=N -> durante la definitiva (accion 15: el baile con su minijuego) el lado 1 pulsa los cuatro botones de cara
  // (bits 0x1000..0x8000 del registro del mando) cada 12 cuadros (N=1) o uno solo rotando cada 12 cuadros (N=2). Solo via pruebas.py.
  if (static const int pul = std::getenv("DBZ3_TEST_FUSION_PULSOS") ? std::atoi(std::getenv("DBZ3_TEST_FUSION_PULSOS")) : 0; pul > 0 && user == 0) {
    static uint32_t f = 0;
    const uint32_t m = REX_LOAD_U32(kMgr);
    if (m && REX_LOAD_U16(m + 2096 + kAct) == 15) {
      ++f;
      const uint32_t r = kPadRec;
      const uint32_t per = pul >= 3 ? 6 : 12, mant = pul >= 3 ? 3 : 4;   // =3: un boton cada 6 cuadros (mas intentos por etapa del baile)
      uint16_t bits = f % per >= mant ? 0 : pul == 1 ? uint16_t(0xF000) : uint16_t(0x1000u << (f / per % 4));
      if (pul == 4) {   // =4: un boton por etapa (X Y A Y B) en los 10 cuadros antes de cada salto medido (121/201/316/361/413, contados desde la etapa 490)
        static const uint32_t fin[5] = {121, 201, 316, 361, 413};
        static const uint16_t btn[5] = {0x4000, 0x8000, 0x1000, 0x8000, 0x2000};
        static uint32_t f490 = 0;
        if (REX_LOAD_U32(m + 2096 + 2928) == 0x490 && !f490) f490 = f;
        bits = 0;
        for (uint32_t k = 0; f490 && k < 5; ++k)
          if (f - f490 + 1 >= fin[k] - 10 && f - f490 + 1 < fin[k]) bits = btn[k];
      }
      static uint32_t cod_ant = 0;
      if (const uint32_t cod = REX_LOAD_U32(m + 2096 + 2928); cod != cod_ant) {   // etapa del baile: que boton se pulsaba al avanzar
        REXLOG_INFO("dbz3 fusion: prueba: etapa {:X} -> {:X} (cuadro {}, boton {:04X})", cod_ant, cod, f, uint32_t(0x1000u << ((f > 0 ? f - 1 : 0) / per % 4)));
        cod_ant = cod;
      }
      if (bits) {
        REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
        REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
        for (uint32_t j = 4; j < 8; ++j)
          if (bits >> (8 + j) & 1) REX_STORE_U32(r + 72 + 4 * j, 1);
        if (f % 12 == 0 && pul < 3) REXLOG_INFO("dbz3 fusion: prueba: pulso {:04X} (cuadro {}, cod {:X})", bits, f, REX_LOAD_U32(m + 2096 + 2928));
      }
    } else {
      f = 0;
    }
  }
  // PRUEBA: DBZ3_TEST_FUSION_DIAG=1 -> cada 0,5 s estado del lado 1: forma, accion, codigo y por cada forma su modelo y nodo de tecnicas
  if (static const bool diag = std::getenv("DBZ3_TEST_FUSION_DIAG") != nullptr; diag && user == 0) {
    static uint32_t n = 0, c259 = 0;
    const uint32_t m = REX_LOAD_U32(kMgr), x = m + 2096;
    if (m && REX_LOAD_U16(x + kAct) == 15 && REX_LOAD_U32(x + 2928) == 0x259 && ++c259 == 30) {   // volcado de la ficha del luchador (8240 B) 30 cuadros tras empezar 0x259
      std::ofstream(std::string(std::getenv("DBZ3_TEST_FUSION_DIAG")) + ".bin", std::ios::binary).write(reinterpret_cast<const char*>(base + x), 8240);
    }
    if (m && ++n % 30 == 0) {
      std::string s;
      for (uint32_t f = 0; f < 5; ++f) {
        const uint32_t mo = REX_LOAD_U32(x + 7608 + 4 * f), nodo = REX_LOAD_U32(x + 7672 + 28 * f + 20);
        s += fmt::format(" f{}: modelo {:08X} +36 {:X} nodo {:08X} +1 {:X};", f, mo, mo ? REX_LOAD_U32(mo + 36) : 0, nodo, nodo ? REX_LOAD_U8(nodo + 1) : 0);
      }
      REXLOG_INFO("dbz3 fusion: diag: forma {} accion {} cod {:X} baile {} X+6720 {}{}", REX_LOAD_U32(x + kForm), REX_LOAD_U16(x + kAct), REX_LOAD_U32(x + 2928), g_baile[0], REX_LOAD_U32(x + 6720), s);
    }
  }
  static const int on = std::getenv("DBZ3_TEST_ULT") ? std::atoi(std::getenv("DBZ3_TEST_ULT")) : 0;
  static uint32_t tick = 0;
  const uint32_t mgr = on > 0 && user == 0 ? REX_LOAD_U32(kMgr) : 0;
  if (!mgr || REX_LOAD_U16(mgr + 2096 + kAct) > 2 || ++tick % 60) return;
  if (on == 4 && (tick % 180 || REX_LOAD_U32(mgr + 2096 + kForm) != 0)) return;   // =4: P+K+G de verdad para la fusion, cada 3 s, sin definitiva
  if (on == 3 && g_bu_forma[0]) return;   // =3: como 1, pero ya absorbido deja de pulsar (tomas cercanas)
  StoreF(base, mgr + 2096 + kKi, 7000.0f);
  {  // X+64: posicion (x, y, z): solo en el suelo; el alto no se toca
    const uint32_t a = mgr + 2096 + 64, b = mgr + 2096 + 8240 + 64;
    const float dx = LoadF(base, b) - LoadF(base, a), dz = LoadF(base, b + 8) - LoadF(base, a + 8);
    const float d = std::max(0.01f, std::sqrt(dx * dx + dz * dz)), quiere = std::getenv("DBZ3_TEST_ULT_DIST") ? float(std::atof(std::getenv("DBZ3_TEST_ULT_DIST"))) : on == 2 ? 120.0f : 30.0f;   // 30: la del principio; mas cerca, P+K+G+E toma el agarre de algunos
    StoreF(base, b, LoadF(base, a) + dx / d * quiere);
    StoreF(base, b + 8, LoadF(base, a + 8) + dz / d * quiere);
    REXLOG_INFO("dbz3 fusion: prueba: distancia al rival {:.1f} -> {:.1f}", d, quiere);
  }
  const uint32_t pad = REX_LOAD_U16(mgr + 2096 + kPadSlot), r = kPadRec + 184 * user;
  uint16_t bits = 0;
  for (const uint8_t want : {1, 2, 4, 8})
    for (uint32_t j = 0; j < 8 && (on != 4 || want != 8); ++j)
      if (REX_LOAD_U8(mgr + (pad + 4382) * 8 + j) == want) {
        bits |= uint16_t(0x100u << j);
        REX_STORE_U32(r + 72 + 4 * j, 1);
        break;
      }
  REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
  REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
  REXLOG_INFO("dbz3 fusion: prueba: forma {} siguiente {} ki {:.0f} baile {} tiempo {} {} vuelta {} fin {}", REX_LOAD_U32(mgr + 2096 + kForm), REX_LOAD_U32(mgr + 2096 + 2984), LoadF(base, mgr + 2096 + kKi), g_baile[0], REX_LOAD_U32(mgr + 2096 + 6720), REX_LOAD_U32(mgr + 2096 + 6724), REX_LOAD_U32(mgr + 2096 + 6728), REX_LOAD_U8(mgr + 2096 + 8207));
  REXLOG_INFO("dbz3 fusion: prueba: P+K+G+E (botones {:04X}, id {}, modo hiper {}, accion {}, cod {:X})", bits, REX_LOAD_U32(mgr + 2096 + kId), REX_LOAD_U8(mgr + 2096 + 8208), REX_LOAD_U16(mgr + 2096 + kAct), REX_LOAD_U32(mgr + 2096 + 2928));
}

// Cada capsula dice en que formas vale (#SKC +14, mascara por X+2980: la definitiva de Ten, solo la 0; la de Freezer, de la 3 a la 5). Las
// heredadas por Bu valen en la forma en que este; sin esto el juego las daba por no disponibles segun en cual cayera el sorteo.
bool BuFormaDeCapsula(uint8_t* base, uint32_t x, uint32_t cap, uint32_t& forma) {
  const uint32_t mgr = REX_LOAD_U32(kMgr);
  if (!mgr || x < mgr + 2096 || REX_LOAD_U32(x + kId) != 35) return false;
  const uint32_t p = (x - mgr - 2096) / 8240, skc = REX_LOAD_U32(dbz3::GA(0x8237560C));
  forma = REX_LOAD_U32(x + kForm);
  if (p > 1 || !skc || cap >= 1024 || !g_bu_forma[p] || forma != g_bu_forma[p]) return false;
  const uint8_t mascara = REX_LOAD_U8(skc + 40 * cap + 14);
  if (!mascara || (mascara >> forma & 1)) return false;
  uint32_t vale = 0;
  while (!(mascara >> vale & 1)) ++vale;
  REX_STORE_U32(x + kForm, vale);
  return true;
}

void AlPrepararCombate(uint8_t* base, uint32_t sel) { NativaAlPreparar(base, sel); }
void TrasPrepararCombate(uint8_t* base, uint32_t sel) {   // quien no es Goku empieza con sus golpes (la tabla nativa acaba de poner la forma 0)
  if (g_nativa_a > 0 && REX_LOAD_U16(sel + 16) == uint32_t(g_nativa_ficha)) {
    REX_STORE_U16(sel + 20, uint16_t(kFormaBaile));
    REX_STORE_U16(sel + 22, uint16_t(kFormaBaile));
  }
}

int IdBuAbsorbido(int c) {
  return c >= 49 && c <= 52 ? IdDe(kAbsorciones[c - 49].nombre) : -1;
}

}  // namespace dbz3::fusion

DBZ3_HOOK(sub_82115980, dbz3eu_sub_82115A80) {   // r3 = X, r4 = capsula: se puede usar ahora
  const uint32_t x = ctx.r3.u32;
  uint32_t forma = 0;
  const bool prestada = dbz3::fusion::BuFormaDeCapsula(base, x, ctx.r4.u32, forma);
  orig(ctx, base);
  if (prestada) REX_STORE_U32(x + dbz3::fusion::kForm, forma);
}

DBZ3_HOOK(sub_82100C10, dbz3eu_sub_82100D10) {   // EU: misma funcion (llama a 82100B10 y 82100C78), +0x100 como sus vecinas
  dbz3::fusion::BuAlConectar(ctx, base, ctx.r3.u32, ctx.r4.u32);
  orig(ctx, base);
}

// Guarda comun de la pose (1/11, subFusion): un clip cuyo descriptor no tiene tabla de pistas (+12 = 0) mientras el modelo
// tiene huesos con pista asignada hacia el sampler del juego (sub_8208D518), que lee la direccion 0 y cierra todo (access violation).
// Pasa con cualquier forma cuyo banco de animaciones no trae el clip que pide el guion (un mod sin moveset en la forma de fallo
// de una fusion). Aqui se salta esa pose (el modelo se queda como estaba) y se avisa; las demas poses no se tocan.
// r3 = modelo, r4 = estado del clip (+0 almacen<<16|clip, +8 cuadro). Mapa hueso->pista: modelo+688+almacen*512 (lha por hueso).
DBZ3_HOOK(sub_8208DC60, dbz3eu_sub_8208DC28) {
  const uint32_t model = ctx.r3.u32, st = ctx.r4.u32;
  const uint32_t w = REX_LOAD_U32(st), banco = w >> 16;
  const uint32_t acm = REX_LOAD_U32(model + 4 * (banco + 282));
  const uint32_t datos = acm ? REX_LOAD_U32(acm + 20) : 0;
  const uint32_t desc = datos + ((w << 4) & 0xFFFF0);
  bool mala = !datos;
  uint32_t nb = 0, mapeados = 0;
  if (datos && !REX_LOAD_U32(desc + 12)) {
    const uint32_t sk = REX_LOAD_U32(model + 4);
    nb = sk ? std::min<uint32_t>(REX_LOAD_U32(sk + 16), 256) : 0;
    const uint32_t mapa = model + 688 + banco * 512;
    for (uint32_t k = 0; k < nb; ++k)
      if (int16_t(REX_LOAD_U16(mapa + 2 * k)) >= 0) ++mapeados;
    mala = mapeados > 0;
  }
  if (mala) {
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 6)
      REXLOG_INFO("dbz3 pose: clip {:X}:{:03X} sin pistas (modelo {:08X}, banco {:08X}, datos {:08X}, descriptor {:08X} [{:08X} {:08X} {:08X} {:08X}], huesos {} con pista {}): pose saltada",
                  banco, w & 0xFFFF, model, acm, datos, desc, datos ? REX_LOAD_U32(desc) : 0, datos ? REX_LOAD_U32(desc + 4) : 0,
                  datos ? REX_LOAD_U32(desc + 8) : 0, datos ? REX_LOAD_U32(desc + 12) : 0, nb, mapeados);
    return;
  }
  orig(ctx, base);
}
