// dbz3 - Conector nativo de combate para el HUB (modos propios lanzados con las piezas del juego).
//
// El juego arranca un combate desde la escena 24/25 con la configuracion global CFG (0x8245EFC0, 384 B; la
// copia sub_820FE668 a mgr+1680). Camino (docs/12_conectores/10_arranque_combate_nativo.md, s9), sin pilotar
// menus ni pulsar botones:
//   menu principal (espera 0x821D7C30) -> sub_82179408 (CFG por defecto) + nuestros datos -> se destruye el
//   menu (sub_8218BA00) -> la tarea de escenas pasa a la espera de la 24 (0x821D7400) -> se pide la 25
//   (sub_821D2778(25,0) + sub_821D27C8(10)) -> combate -> sub_821D3628 con resultado 2 -> menu principal.
// US y EU: cada gancho, llamada y direccion con su pareja EU (src/eu_map.h, frente eu).
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "generated/dbz3_init.h"
#include "eu_map.h"

namespace dbz3::tag { void OnScreen(int screen); }
namespace dbz3::campaigns { void ForgetStoryBattle(); void DuAuto(int slot); void DuSesionFin(); }

namespace dbz3::hub {
namespace {
constexpr dbz3::GA kCfg = 0x8245EFC0, kReqCur = 0x8247A990, kSceneTask = 0x8247A9A0, kGame = 0x824BA110;

struct Request {
  int id[2] = {0, 7};
  int suit[2] = {0, 0};
  int pad[2] = {0, -1};   // 0 mando 1, 1 mando 2, -1 COM
  int stage = 0;
  int time_s = 0;         // 0 el de las opciones, > 0 segundos, < 0 sin tiempo (CFG+348 en s; doc 10 s2.2)
  int hp[2] = {0, 0};     // 0 la normal; > 0 vida inicial y maxima (rec+36/+40, 1000 por barra); < 0 solo la
                          // inicial (-hp), la maxima la de serie (vida que no se recupera: la barra no se acorta)
  int form[2] = {0, 0};   // forma inicial (rec+20; 0 la base)
  int mode = 0;           // DS170: modo de la CFG (CFG+338); 0 = el de siempre (1 = presentacion del Duelo)
};
Request g_req;
// 0 nada; 1 pedido (lo recoge la espera del menu principal); 3 combate nuestro en curso; 4 encadenado: el
// siguiente combate sale tras este sin pasar por el menu principal
std::atomic<int> g_phase{0};
std::atomic<bool> g_chain{false}, g_hold{false}, g_nocaps{false};
// DS170: modo de la CFG para el siguiente combate pedido (0 = el de siempre). Lo usan los combates "solo escena"
// (pasarela.cpp) para no gastar 2,65 s de vuelo de entrada que el negro tapa: con el modo 5 el combate va directo
// al READY (hub.cpp, comentario de Launch; medido en las pruebas de D1_final). Se consume al pedir.
std::atomic<int> g_mode_next{0};

Request Make(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2) {
  Request r;
  r.id[0] = id1;
  r.id[1] = id2;
  r.pad[1] = p2_com ? -1 : 1;
  r.stage = stage;
  r.time_s = time_s;
  r.hp[0] = hp1;
  r.hp[1] = hp2;
  r.form[0] = form1;
  r.form[1] = form2;
  return r;
}
}  // namespace

// Pide un combate desde el menu principal (cualquier hilo; lo lanza el hilo del juego en su siguiente tick).
// Con tiempo, vida y forma inicial por combate (Leyenda de Mr. Satan, retos): 0 = lo normal en cada uno.
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2) {
  if (g_phase.load() != 0) {
    g_nocaps = false;   // pedido sin combate: NoCapsulesNext no pasa al siguiente
    return false;
  }
  g_req = Make(id1, id2, stage, p2_com, time_s, hp1, hp2, form1, form2);
  g_req.mode = g_mode_next.exchange(0);   // DS170
  g_phase = 1;
  return true;
}
bool RequestBattle(int id1, int id2, int stage, bool p2_com) {
  return RequestBattleEx(id1, id2, stage, p2_com, 0, 0, 0, 0, 0);
}
// DS170: modo de la CFG del siguiente combate pedido (0 = el de siempre).
void BattleModeNext(int mode) { g_mode_next = mode; }
bool OwnBattle() { return g_phase.load() == 3; }
// C3: abre Dragon Universe en la casilla `slot` (la del Heroe) sin pasar por su titulo, su menu ni su rueda de
// personajes (el Heroe es exclusivo de su modo). Lo lanza el menu principal en su siguiente tick.
std::atomic<int> g_du_slot{-1};
bool RequestDu(int slot) {
  if (g_phase.load() != 0 || g_du_slot.load() >= 0) return false;
  g_du_slot = slot;
  return true;
}

// A2nave: abre el Edit Skills NATIVO (pantalla 22) como la fila 4 del menu principal; al salir con B el juego vuelve al
// menu principal (5). Lo lanza el menu principal en su siguiente tick (mismo patron que RequestDu).
std::atomic<bool> g_edit_skills{false};
bool RequestEditSkills() {
  if (g_phase.load() != 0 || g_edit_skills.load()) return false;
  g_edit_skills = true;
  return true;
}

// Durante un combate nuestro (OwnBattle): el siguiente sale al acabar este, sin cargar el menu principal (retos,
// Torre...). Mientras HoldChain(true), espera en negro tras el combate (para una pantalla propia entre medias).
bool ChainBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2) {
  if (g_phase.load() != 3) {
    g_nocaps = false;
    return false;
  }
  g_req = Make(id1, id2, stage, p2_com, time_s, hp1, hp2, form1, form2);
  g_req.mode = g_mode_next.exchange(0);   // DS170
  g_chain = true;
  return true;
}
void HoldChain(bool hold) { g_hold = hold; }
// El siguiente combate sin capsulas (el Laboratorio mide golpes sin modificadores de dano); solo ese combate.
void NoCapsulesNext() { g_nocaps = true; }
// A2nave: el siguiente combate (solo el jugador 1) con su lista «Custom» del Edit Skills (partida +4928 + 4624 por casilla; vacia =
// la Normal). Lo pide pasarela_misiones.cpp con la eleccion Normal/Custom de la rueda de la nave.
std::atomic<bool> g_custom1{false};
void CustomCapsulesNext(bool on) { g_custom1 = on; }
// Deja el encadenado pedido (el combate acaba y se vuelve al menu principal como siempre). En la espera en negro
// (fase 4) tambien: sub_821D7D18 sigue y crea el menu principal.
void CancelChain() {
  g_chain = false;
  int four = 4;
  if (g_phase.compare_exchange_strong(four, 0)) g_hold = false;
}
}  // namespace dbz3::hub

namespace {
// Rellena la CFG con g_req y pide la escena 25 desde una tarea de escenas parada (menu principal o tras un combate).
void Launch(PPCContext& ctx, uint8_t* base, bool from_menu) {
  using namespace dbz3::hub;
  const Request& q = g_req;
  const PPCContext saved = ctx;
  DBZ3_CALL_EU(sub_82179408)(ctx, base);   // CFG por defecto (modo 5, tiempo de las opciones)
  const bool nocaps = g_nocaps.exchange(false);
  const bool custom1 = g_custom1.exchange(false);
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t rec = kCfg + 16 + 80 * k;
    REX_STORE_U16(rec + 16, uint16_t(q.id[k]));
    REX_STORE_U16(rec + 18, uint16_t(q.suit[k]));
    REX_STORE_U16(rec + 26, uint16_t(int16_t(q.pad[k])));
    if (q.form[k] > 0) REX_STORE_U16(rec + 20, uint16_t(q.form[k]));
    // s7 06: como la forma de campana (tagteam_slots.cpp BeforeBattleSetup, rec+20 y rec+22): el resolver se salta
    // la forma impuesta y sin el maximo el juego no carga la forma pedida
    if (q.form[k] > 0) REX_STORE_U16(rec + 22, uint16_t(q.form[k]));
    if (q.hp[k] != 0) REX_STORE_U32(rec + 36, uint32_t(q.hp[k] > 0 ? q.hp[k] : -q.hp[k]));
    if (q.hp[k] > 0) REX_STORE_U32(rec + 40, uint32_t(q.hp[k]));
    if (custom1 && k == 0 && uint32_t(q.id[0]) < 38 && !nocaps) {   // lista Custom del jugador 1 (casilla del select = ID)
      const dbz3::GA lst = dbz3::GA(0x824BA110) + 4928u + 4624u * uint32_t(q.id[0]);
      if (const char* tc = std::getenv("DBZ3_TEST_CUSTOM1"); tc) {   // PRUEBA: pone esta lista Custom (hasta 7 IDs) en la partida de id 0
        int c[7] = {-1, -1, -1, -1, -1, -1, -1};
        std::sscanf(tc, "%d,%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]);
        for (uint32_t i = 0; i < 7; ++i) REX_STORE_U16(lst + 2 * i, uint16_t(c[i] < 0 ? 0xFFFF : c[i]));
      }
      uint16_t ids[7] = {};
      uint32_t n = 0;
      for (uint32_t c = 0; c < 7; ++c)
        if (const uint16_t cap = REX_LOAD_U16(lst + 2 * c); cap != 0xFFFF && cap != 0) ids[n++] = cap;
      if (n) {
        REX_STORE_U16(rec + 52, uint16_t(n));
        for (uint32_t c = 0; c < 7; ++c) REX_STORE_U16(rec + 54 + 2 * c, ids[c]);
      }
      REXLOG_INFO("dbz3 hub: jugador 1 (id {}): lista Custom con {} capsulas ({} {} {})", q.id[0], n, ids[0], ids[1], ids[2]);
    }
    // Capsulas: sub_82179408 deja las listas vacias y sin ellas nadie lanza tecnicas ni se transforma. Como en el
    // Duelo, la lista Normal del personaje (tabla 0x8234ABB8 + 96 * ID: +80 cuantas, +82 7 IDs; la que el Torneo
    // pone al COM, docs/12_conectores/12_torneo_como_motor.md s4.1; medido por el frente campanas)
    if (const uint32_t id = uint32_t(q.id[k]); !nocaps && !REX_LOAD_U16(rec + 52) && id < 105) {
      const dbz3::GA tab = dbz3::GA(0x8234ABB8) + 96u * id;
      // solo IDs reales: una lista con huecos a 0 (Super Bu tras el arranque, medido por campanas) equiparia la 0
      uint16_t ids[7] = {};
      uint32_t n = 0;
      for (uint32_t c = 0, m = std::min<uint32_t>(REX_LOAD_U16(tab + 80), 7); c < m; ++c)
        if (const uint16_t cap = REX_LOAD_U16(tab + 82 + 2 * c)) ids[n++] = cap;
      REX_STORE_U16(rec + 52, uint16_t(n));
      for (uint32_t c = 0; c < 7; ++c) REX_STORE_U16(rec + 54 + 2 * c, ids[c]);
      REXLOG_INFO("dbz3 hub: lado {} (id {}): {} capsulas de serie ({} {} {} ...)", k + 1, id, n,
                  REX_LOAD_U16(rec + 54), REX_LOAD_U16(rec + 56), REX_LOAD_U16(rec + 58));
    }
  }
  // PRUEBA (DBZ3_TEST_CAPS="c1,c2,..." hasta 7): capsulas del jugador 1 (rec+52 cuantas, rec+54 IDs; >= 595 las
  // resuelve roster_ext) para probar tecnicas, definitivas y transformaciones de los ports
  if (static const char* tc = std::getenv("DBZ3_TEST_CAPS"); tc) {
    int c[7] = {}, n = std::sscanf(tc, "%d,%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]);
    n = n < 0 ? 0 : n;
    REX_STORE_U16(kCfg + 16 + 52, uint16_t(n));
    for (int i = 0; i < 7; ++i) REX_STORE_U16(kCfg + 16 + 54 + 2 * uint32_t(i), uint16_t(i < n ? c[i] : 0));
    REXLOG_INFO("dbz3 hub: prueba: {} capsulas para el jugador 1 ({})", n, tc);
  }
  REX_STORE_U16(kCfg + 340, uint16_t(q.stage));
  // Modo 1 (no el 5 que deja sub_82179408): el combate empieza con la presentacion nativa del Duelo, vuelo por el
  // escenario y entrada de los luchadores (START la salta). Medido 2026-10-15: estados 1 (vuelo, 14 s), 2 (entradas,
  // 6 s), 3 READY, 4 pelea; la vuelta por "Return to Main Menu" da resultado 2. Con el 5 iba directo al READY.
  // (CFG+352 != -1 en el modo 5 no sirve: es un indice y cascaba.) DBZ3_HUB_MODE=5 para probar sin presentacion.
  // DS170: q.mode (hub::BattleModeNext) permite pedirlo por combate; el entorno sigue mandando (pruebas).
  const char* mode = std::getenv("DBZ3_HUB_MODE");
  REX_STORE_U16(kCfg + 338, uint16_t(mode ? std::atoi(mode) : q.mode ? q.mode : 1));
  if (q.time_s) REX_STORE_U32(kCfg + 348, q.time_s > 0 ? uint32_t(q.time_s) : 0xFFFFFFFFu);
  REX_STORE_U8(kGame + 1, 6);   // G+1 = 6: la vuelta de la escena 25
  ctx = saved;
  if (from_menu) {
    DBZ3_CALL_EU(sub_8218BA00)(ctx, base);    // fuera el menu principal (lo que hace su confirmacion)
    ctx = saved;
  } else {
    // la peticion de la escena 5 que traia la vuelta del combate se descarta (sub_821D2778 solo escribe la actual
    // si esta libre; si no, la deja en la siguiente, kReqCur-4)
    REX_STORE_U32(kReqCur, 0xFFFFFFFFu);
    REX_STORE_U32(kReqCur - 4, 0xFFFFFFFFu);
    // lo que hace la llegada al menu principal (online_panel.cpp OnlineTick): el Tag Team cierra el combate
    // anterior (si no, el siguiente no se ve como combate nuevo) y la forma de la historia se olvida
    dbz3::tag::OnScreen(5);
    dbz3::campaigns::ForgetStoryBattle();
  }
  REX_STORE_U32(REX_LOAD_U32(kSceneTask) + 28, dbz3::GA(0x821D7400));   // la tarea de escenas: espera de la 24
  ctx.r3.u64 = 25;
  ctx.r4.u64 = 0;
  DBZ3_CALL_EU(sub_821D2778)(ctx, base);
  ctx = saved;
  ctx.r3.u64 = 10;
  DBZ3_CALL_EU(sub_821D27C8)(ctx, base);      // "salida terminada", como sub_82177300 (Duelo)
  ctx = saved;
  g_phase = 3;
  REXLOG_INFO("dbz3 hub: combate nativo{}: id {} contra id {} (escenario {}, mandos {} / {}, tiempo {}, vida {} / {}, "
              "forma {} / {}, modo {})", from_menu ? "" : " encadenado", q.id[0], q.id[1], q.stage, q.pad[0], q.pad[1],
              q.time_s, q.hp[0], q.hp[1], q.form[0], q.form[1],
              mode ? std::atoi(mode) : q.mode ? q.mode : 1);
}
}  // namespace

namespace {
// Con el menu principal entero (creado y fuera de su entrada) su destruccion (sub_8218BA00, dentro de
// Launch) y el arranque del combate son seguros. Si un modo pide el combate siguiente nada mas acabar el
// anterior (los cinco Cell Jr. seguidos de la Leyenda de Mr. Satan), el menu se pide y se destruye a
// medias; su arbol de nodos (que sub_8218BA00 y sub_8218A638 sueltan con sub_820ACCF0) queda con un puntero
// malo y al tercer combate seguido el juego lee guest 0xF8 (r30 = 0xD0; generado dbz3_recomp.43.cpp:2355,
// lr = 0x820ACCF8 = sub_820ACCF0). Aqui se espera a que el menu este creado y en reposo antes de lanzarlo.
bool MenuListo(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x8245F230));   // gestor del menu (docs/14_frentes/hub.md)
  const uint32_t task = mgr ? REX_LOAD_U32(mgr + 16) : 0;
  const uint32_t obj = task ? REX_LOAD_U32(task + 48) : 0;
  // +148: 2 entrando, 3 activo, 4 sale con B, 5 sale con A
  return obj != 0 && REX_LOAD_U16(obj + 148) >= 3;
}
}  // namespace

// Edit Skills: lo que hace la A del menu principal en su fila 4 (sub_82179328 + escena 22) con las piezas de salida del menu.
bool LaunchEditSkills(PPCContext& ctx, uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x8245F230)), task = mgr ? REX_LOAD_U32(mgr + 16) : 0;
  const uint32_t obj = task ? REX_LOAD_U32(task + 48) : 0;
  if (!obj || REX_LOAD_U32(task + 28) != dbz3::GA(0x8218C5D8)) return false;   // el menu aun no esta activo: otro tick
  const PPCContext saved = ctx;
  REX_STORE_U8(obj + 150, 4);
  ctx.r3.u64 = obj;
  DBZ3_CALL_EU(sub_8218C588)(ctx, base);
  ctx = saved;
  const uint32_t datos = dbz3::GA(0x82456480);
  std::memset(base + datos, 0, 36048);   // lo que hace sub_82179328 (memset del area de trabajo de Edit Skills)
  ctx.r3.u64 = 22;
  ctx.r4.u64 = 0;
  DBZ3_CALL_EU(sub_821D2778)(ctx, base);
  ctx = saved;
  ctx.r3.u64 = 10;
  DBZ3_CALL_EU(sub_821D27C8)(ctx, base);
  ctx = saved;
  REX_STORE_U32(task + 28, dbz3::GA(0x8218C4C8));
  REXLOG_INFO("dbz3 hub: Edit Skills nativo abierto desde la nave (escena 22)");
  return true;
}

// Dragon Universe del Heroe: lo que hace la A del menu principal en su fila "Dragon Universe" (la 0): las piezas de salida
// del menu (sub_8218C588, +148 = 5), la peticion de la escena 6 (titulo de DU) y el estado "saliendo" del menu
// (sub_8218C4C8) que lo destruye al acabar la animacion. Destruir el menu a mano (sub_8218BA00) cerraba el juego al cargar
// la 6. El resto (pasar el titulo, su menu y su rueda sin elegir a nadie): campaigns.cpp (DuAuto, sub_821D78F8, sub_821CA020).
void LaunchDu(PPCContext& ctx, uint8_t* base, int slot) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x8245F230)), task = mgr ? REX_LOAD_U32(mgr + 16) : 0;
  const uint32_t obj = task ? REX_LOAD_U32(task + 48) : 0;
  if (!obj || REX_LOAD_U32(task + 28) != dbz3::GA(0x8218C5D8)) return;   // el menu aun no esta activo: otro tick
  const PPCContext saved = ctx;
  REX_STORE_U8(obj + 150, 0);
  ctx.r3.u64 = obj;
  DBZ3_CALL_EU(sub_8218C588)(ctx, base);
  ctx = saved;
  ctx.r3.u64 = 6;
  ctx.r4.u64 = 0;
  DBZ3_CALL_EU(sub_821D2778)(ctx, base);
  ctx = saved;
  ctx.r3.u64 = 10;
  DBZ3_CALL_EU(sub_821D27C8)(ctx, base);   // "salida terminada", como el Duelo y Launch
  ctx = saved;
  REX_STORE_U32(task + 28, dbz3::GA(0x8218C4C8));
  dbz3::campaigns::DuAuto(slot);
  dbz3::hub::g_du_slot = -1;
  REXLOG_INFO("dbz3 hub: Dragon Universe del Heroe: casilla {} (sin su rueda de personajes)", slot);
}

// Espera del menu principal (G+0 = 5): aqui se lanza el combate pedido.
DBZ3_HOOK_EU(sub_821D7C30) {
  using namespace dbz3::hub;
  // PRUEBA (DBZ3_TEST_BATTLE="id1,id2,escenario"): a los 3 s en el menu principal, un combate 1P VS COM
  if (static const char* tb = std::getenv("DBZ3_TEST_BATTLE"); tb) {
    static uint32_t ticks = 0;
    static int done = 0;
    ticks = REX_LOAD_U8(kGame) == 5 ? ticks + 1 : 0;
    if (done < 2 && ticks == 180 && g_phase.load() == 0) {   // dos seguidos: ida y vuelta al menu
      int a = 0, b = 7, st = 0, t = 0, h1 = 0, h2 = 0, f1 = 0, f2 = 0;   // ",tiempo,vida1,vida2,forma1,forma2" opcional
      std::sscanf(tb, "%d,%d,%d,%d,%d,%d,%d,%d", &a, &b, &st, &t, &h1, &h2, &f1, &f2);
      if (RequestBattleEx(a, b, st, true, t, h1, h2, f1, f2)) ++done;
    }
  }
  {   // C4: en el menu principal (1,5 s seguidos) la sesion de DU del Heroe ha terminado (ya no se reanuda sola)
    static uint32_t en_menu = 0;
    en_menu = REX_LOAD_U8(kGame) == 5 ? en_menu + 1 : 0;
    if (en_menu == 90) dbz3::campaigns::DuSesionFin();
  }
  // PRUEBA (DBZ3_TEST_DU_HEROE=casilla): a los 3 s en el menu principal abre DU del Heroe (hub::RequestDu)
  if (static const char* td = std::getenv("DBZ3_TEST_DU_HEROE"); td) {
    static uint32_t ticks = 0;
    static bool done = false;
    ticks = REX_LOAD_U8(kGame) == 5 ? ticks + 1 : 0;
    if (!done && ticks == 180 && RequestDu(std::atoi(td))) done = true;
  }
  if (g_edit_skills.load()) {
    if (int32_t(REX_LOAD_U32(kReqCur)) == 22) g_edit_skills = false;
    else if (int32_t(REX_LOAD_U32(kReqCur)) == -1 && MenuListo(base) && LaunchEditSkills(ctx, base)) g_edit_skills = false;
  }
  if (const int ds = g_du_slot.load(); ds >= 0) {
    if (int32_t(REX_LOAD_U32(kReqCur)) == 6) {
      g_du_slot = -1;   // la fila ya pidio la escena 6
    } else if (int32_t(REX_LOAD_U32(kReqCur)) == -1 && MenuListo(base)) {
      LaunchDu(ctx, base, ds);   // y el despachador de este mismo estado carga la escena 6 como con la fila del menu
    }
  }
  if (g_phase.load() == 1 && int32_t(REX_LOAD_U32(kReqCur)) == -1) {
    // Si el menu aun no esta entero (el modo pide el siguiente combate en cuanto acaba el anterior: los
    // Cell Jr. seguidos), se deja correr el menu este tick y se reintenta el siguiente; tope de 10 s.
    static uint32_t esperas = 0;
    if (MenuListo(base) || esperas++ > 600) {
      esperas = 0;
      Launch(ctx, base, true);
      return;
    }
  }
  orig(ctx, base);
}

// Tras un combate (estado 0x821D6A08 de la tarea de escenas, ya fundido a negro): con la escena 5 pedida crea el
// menu principal. Con un combate encadenado, en su lugar se lanza el siguiente (o se espera en negro con HoldChain).
DBZ3_HOOK_EU(sub_821D7D18) {
  using namespace dbz3::hub;
  if (g_phase.load() == 4 && ctx.r3.u32 == 5) {
    if (g_hold.load()) {
      REX_STORE_U32(REX_LOAD_U32(kSceneTask) + 28, dbz3::GA(0x821D6A08));   // otra vez aqui en el siguiente tick
      return;
    }
    Launch(ctx, base, false);
    return;
  }
  orig(ctx, base);
}

// sub_8216BF78 destruye la interfaz de la escena 24 (byte 0x82456430): en nuestro camino nunca se creo.
DBZ3_HOOK_EU(sub_8216BF78) {
  if (dbz3::hub::g_phase.load() == 3 && REX_LOAD_U8(dbz3::GA(0x824BA110)) == 25) return;
  orig(ctx, base);
}

// Fin del combate: con uno nuestro, resultado 2 = volver al menu principal (sin "otra vez" ni menus del Duelo).
DBZ3_HOOK_EU(sub_821D3628) {
  if (dbz3::hub::g_phase.load() == 3) {
    // "Otra vez" no pasa por aqui (revancha dentro de la escena); "Elegir personajes" y "Menu principal"
    // dan 4 (medido 2026-10-15), que con G+1 = 6 lleva a la escena 24 (la seleccion del Duelo, que en nuestro
    // camino no existe: se quedaba ahi). Todo resultado que no sea 2 se lleva a 2 = menu principal (el HUB
    // decide despues a donde volver).
    // PRUEBA (DBZ3_TEST_CHAIN=1 con DBZ3_TEST_BATTLE): el primer combate encadena el segundo (el mismo)
    if (static int tested = 0; std::getenv("DBZ3_TEST_CHAIN") && !tested++) {
      const auto& q = dbz3::hub::g_req;
      dbz3::hub::ChainBattleEx(q.id[0], q.id[1], q.stage, true, q.time_s, q.hp[0], q.hp[1], q.form[0], q.form[1]);
    }
    const bool chain = dbz3::hub::g_chain.exchange(false);
    REXLOG_INFO("dbz3 hub: fin del combate nativo (resultado del juego {}): {}", ctx.r3.s32,
                chain ? "sigue el combate encadenado" : "al menu principal");
    ctx.r3.u64 = 2;
    dbz3::hub::g_phase = chain ? 4 : 0;
  }
  orig(ctx, base);
}
