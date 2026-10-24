// dbz3 - Online: ganchos del guest para el lockstep (ver netplay.h).
//
// sub_820902F8 (EU 820902C0): sondeo de los 4 mandos, una vez por iteracion del bucle principal
//   = un tick. Se arma el tick antes y, si nadie lo consumio, se juega con el mando en reposo.
// sub_8221C178 (EU 8221A940): thunk de XamInputGetState (r3 = usuario, r4 = X_INPUT_STATE de
//   destino, 0x8238B680 + 16*usuario). Usuario 0: se lee el mando local real (original), se
//   intercambia el tick con el rival y se escribe el del anfitrion; usuario 1: el del invitado
//   (siempre "conectado"). Evidencias: docs/08_online/ONLINE.md.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <thread>

#include "generated/dbz3_init.h"
#include "../eu_map.h"
#include "netplay.h"
#include "rollback.h"
#include "../tagteam.h"
#include "../ingame/menu_pilot.h"
#include "../ingame/online_panel.h"
#include "../ingame/tagteam_panel.h"
#include "../iw_minijuegos.h"

namespace dbz3::tag {
void OnScreen(int screen);   // tagteam.cpp
bool OnPad(PPCContext& ctx, uint8_t* base, uint32_t user, uint16_t buttons, uint8_t lt);   // tagteam.cpp
uint16_t PoseButtons(uint32_t user);   // tagteam.cpp: pose de entrada del relevo (Y+B)
bool PoseVirtualPad(uint16_t* buttons, int16_t* ly);   // tagteam.cpp: mando 4 virtual del COM en pose
bool ExitStick(uint32_t user, int16_t* ly);   // tagteam.cpp: salida del relevo del humano
void Tick(PPCContext& ctx, uint8_t* base);
}

namespace dbz3::hub { bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2); }
REXCVAR_DEFINE_BOOL(dbz3_net_rollback, false, "DBZ3/Online",
                    "Online: rollback (prediccion del mando del rival + resimulacion). Apagado por defecto; solo vale si los dos PCs lo piden. Solo imagen US");
REXCVAR_DEFINE_INT32(dbz3_net_rollback_ventana, 2, "DBZ3/Online",
                     "Online con rollback: ticks sin confirmar permitidos (0 = lockstep, maximo 6). El retardo de entrada baja en esa cantidad");
REXCVAR_DEFINE_BOOL(dbz3_net_sumas, false, "DBZ3/Online",
                    "Online: suma por fotograma de la region del combate (R1). Los dos PCs deben tenerla igual; solo registra");

namespace {

constexpr dbz3::GA kRng = 0x82310670;     // estado del LFSR del juego (misma direccion en US y EU: tools/eu_map.py)

void WritePad(uint8_t* base, uint32_t out, const dbz3::net::Pad& p, uint32_t packet) {
  REX_STORE_U32(out + 0, packet);
  REX_STORE_U16(out + 4, p.buttons);
  REX_STORE_U8(out + 6, p.lt);
  REX_STORE_U8(out + 7, p.rt);
  REX_STORE_U16(out + 8, uint16_t(p.lx));
  REX_STORE_U16(out + 10, uint16_t(p.ly));
  REX_STORE_U16(out + 12, uint16_t(p.rx));
  REX_STORE_U16(out + 14, uint16_t(p.ry));
}

dbz3::net::Pad ReadPad(uint8_t* base, uint32_t at) {
  dbz3::net::Pad p;
  p.buttons = REX_LOAD_U16(at + 4);
  p.lt = REX_LOAD_U8(at + 6);
  p.rt = REX_LOAD_U8(at + 7);
  p.lx = int16_t(REX_LOAD_U16(at + 8));
  p.ly = int16_t(REX_LOAD_U16(at + 10));
  p.rx = int16_t(REX_LOAD_U16(at + 12));
  p.ry = int16_t(REX_LOAD_U16(at + 14));
  return p;
}

uint32_t Rng(uint8_t* base) { return REX_LOAD_U32(kRng); }

// Al empezar el online el anfitrion va solo a 1P VS 2P: su mando lleva abajo, abajo y A (el
// cursor del Duelo esta en COM VS COM, la fila de ONLINE; abajo da la vuelta a 1P VS COM). Va por
// el lockstep como cualquier pulsacion, asi que los dos juegos hacen lo mismo. -1 = nada.
int g_autopick = -1;
uint16_t AutoPick(uint8_t* base) {
  if (g_autopick < 0) return 0;
  constexpr uint16_t kDown = 0x0002, kA = 0x1000;
  constexpr struct { int from, to; uint16_t b; } kSteps[] = {{30, 38, kDown}, {58, 66, kDown}, {86, 94, kA}};
  const int f = g_autopick++;
  if (f == 0 && base[dbz3::GuestAddr(0x824BA110, 0x824BA100)] != 14) g_autopick = -1;   // no esta en el Duelo
  if (f > 94) g_autopick = -1;
  for (const auto& st : kSteps) {
    if (f >= st.from && f < st.to) return st.b;
  }
  return 0;
}

}  // namespace

namespace {
// R1/R2 (rollback.h): todo apagado salvo cvar o variables de entorno de pruebas.
dbz3::net::rb::DumpRing* g_dump = nullptr;
const char* g_who = "x";
void OnSumBad(uint32_t tick, uint32_t) { if (g_dump) g_dump->Dump(tick, g_who); }
#ifdef _WIN32
dbz3::net::rb::Gate g_gate;
#endif
dbz3::net::rb::FrameLog g_flog;
#ifdef _WIN32
// R4: estado del nucleo del rollback (el codigo esta mas abajo, RbBegin)
struct RbState {
  dbz3::net::rb::Core core;
  uint32_t mgr = 0, t0 = 0, t0_rb = 0, fights = 0;   // t0 = tick de sesion en que empezo el combate (state 4)
  bool in_fight = false;
  std::chrono::steady_clock::time_point last{};
  std::vector<float> frame_ms, resim_ms;
  uint32_t sums_cmp0 = 0, sums_bad0 = 0;
};
RbState g_rbs;
dbz3::net::rb::SumCsv g_csv;
const char* g_who_name(const dbz3::net::Session& s) { return s.role() == dbz3::net::Role::kHost ? "host" : "guest"; }
#endif

// PRUEBAS (DBZ3_NET_AUTO=host | guest): crea / se une a la sala por si solo en el menu principal, sin pasar por la
// pantalla ONLINE (dos copias en un PC, DBZ3_NET_LOCAL). El anfitrion empieza cuando los dos estan enlazados. Con
// DBZ3_TEST_BATTLE el juego lanza luego un combate normal, ya con el lockstep en marcha.
void RbAuto(uint8_t* base, dbz3::net::Session& s) {
  static const char* mode = std::getenv("DBZ3_NET_AUTO");
  static const bool solo = std::getenv("DBZ3_RB_SOLO") != nullptr;   // R3: combate 1P VS 2P sin red, para medir el guardado
  if (!mode && !solo) return;
  if (!mode) mode = "solo";
  static uint32_t calls = 0, last_log = 0;
  static int step = 0;
  ++calls;
  const bool host = mode[0] == 'h';
  // arranca en el menu principal; ya online, el anfitrion y el invitado lanzan el mismo combate (sin DBZ3_TEST_BATTLE)
  static uint32_t menu_ticks = 0, act_ticks = 0, retry_at = 0;
  static bool fired = false;
  const bool in_menu = base[dbz3::GuestAddr(0x824BA110, 0x824BA100)] == 5;
  menu_ticks = in_menu ? menu_ticks + 1 : 0;
  if ((s.active() || solo) && in_menu) ++act_ticks;
  if ((s.active() || solo) && !fired && act_ticks > 150) {
    fired = true;
    const char* ts = std::getenv("DBZ3_RB_TIME");
    const bool ok = dbz3::hub::RequestBattleEx(0, 7, 2, solo ? std::getenv("DBZ3_RB_COM") != nullptr : true, ts ? std::atoi(ts) : 0, 0, 0, 0, 0);
    REXLOG_INFO("dbz3 rb: auto: combate pedido ({})", ok ? "ok" : "rechazado");
  }
  // PRUEBAS (DBZ3_RB_FIGHTS=n): tras cada combate, de vuelta en el menu, pide el siguiente hasta n
#ifdef _WIN32   // g_rbs (rollback) solo existe en Windows
  {
    static const int total = std::getenv("DBZ3_RB_FIGHTS") ? std::atoi(std::getenv("DBZ3_RB_FIGHTS")) : 1;
    static uint32_t seen = 0, quiet = 0;
    if (fired && !solo && s.active() && !g_rbs.in_fight && g_rbs.fights > seen && in_menu) {
      if (++quiet > 120) {   // 2 s en el menu tras el combate
        seen = g_rbs.fights;
        quiet = 0;
        REXLOG_INFO("dbz3 rb: auto: combate {} de {} terminado", seen, total);
        if (int(seen) < total) { fired = false; act_ticks = 0; }
      }
    }
  }
#endif
  if (solo) return;
  if (step == 0 && menu_ticks > (host ? 60u : 120u) && calls >= retry_at) {
    std::string err;
    s.Close();
    const bool ok = s.ConfigureIdentity(std::string(32, 'a'), true, false);
    s.SetProfile(1000, -1);
    const auto envi = [](const char* k, int d) { const char* v = std::getenv(k); return v && *v ? std::atoi(v) : d; };
    // PRUEBAS: DBZ3_NET_PORT = puerto del anfitrion, DBZ3_NET_JOIN_PORT = al que llama el invitado (el frontal de tools/red_sim.py),
    // DBZ3_NET_DELAY = retardo fijo (0 = AUTO)
    const uint16_t port = uint16_t(envi("DBZ3_NET_PORT", 7777)), jport = uint16_t(envi("DBZ3_NET_JOIN_PORT", 7777));
    const bool up = ok && (host ? s.Host(port, uint8_t(envi("DBZ3_NET_DELAY", 0)), &err) : s.Join("127.0.0.1", jport, &err));
    REXLOG_INFO("dbz3 rb: auto {}: {} {}", mode, up ? "en marcha" : "FALLO", err);
    step = up ? 1 : 3;
  }
  if (step >= 1 && calls % 300 == 0) {
    int e = 0, tr = 0;
    dbz3::net::CombatRules cr;
    REXLOG_INFO("dbz3 rb: auto estado: enlace {} {} (mods {}, partida {}, sala del otro {})", int(s.link()), s.Status(), s.mods_match(), s.save_ready(),
                s.PeerRoom(&e, &tr, &cr));
  }
  if (step >= 1 && host && (s.link() == dbz3::net::Link::kConnected || s.link() == dbz3::net::Link::kStarting))
    s.SetUiSnapshot(base + dbz3::GuestAddr(dbz3::net::Session::kUiSnap, dbz3::net::Session::kUiSnapEu), dbz3::net::Session::kUiSnapSize);
  if (step == 1 && !host && (s.link() == dbz3::net::Link::kError || s.link() == dbz3::net::Link::kLost)) { step = 0; retry_at = calls + 300; }   // el anfitrion aun no escucha
  if (step == 1) {
    s.Poll();
    if (host && s.link() == dbz3::net::Link::kConnected && s.mods_match()) s.RequestStart();   // sin la sala/equipo del otro no hace nada: se repite
    if (s.link() == dbz3::net::Link::kStarting || s.active()) { step = 2; REXLOG_INFO("dbz3 rb: auto: empezando"); }
  }
  if (step == 2) {
    s.Poll();
    if (s.active() && s.tick() >= last_log + 600) {
      last_log = s.tick();
      REXLOG_INFO("dbz3 rb: auto tick {}: sumas comparadas {} distintas {} (RNG: {})", s.tick(), s.sums_compared(), s.sums_bad(),
                  s.desynced() ? "DESINCRONIA" : "ok");
    }
  }
}

// Antes de orig: estado con el que empieza el tick (el del final del anterior, igual en los dos PCs).
bool RbFrame(uint8_t* base, dbz3::net::Session& s) {
  static const bool flog = std::getenv("DBZ3_RB_LOG") != nullptr;
  if (flog) g_flog.Frame(base, dbz3::net::rb::Fight(base));
  static const bool sums_env = std::getenv("DBZ3_NET_SUMAS") != nullptr;
  const bool sums = (REXCVAR_GET(dbz3_net_sumas) || sums_env) && s.active() && !s.rb_mode();   // con rollback, las sumas salen de las instantaneas confirmadas
#ifdef _WIN32
  const bool gate = g_gate.active();
#else
  const bool gate = false;
#endif
  if (!sums && !gate) return false;
  const uint32_t m = dbz3::net::rb::Fight(base);
  if (sums && m) {
    if (!g_dump && std::getenv("DBZ3_RB_DUMP")) {
      g_dump = new dbz3::net::rb::DumpRing;
      g_who = s.role() == dbz3::net::Role::kHost ? "host" : "guest";
      dbz3::net::Session::sum_hook = &OnSumBad;
    }
    const auto sm = dbz3::net::rb::Compute(base, m);
    if (g_dump) g_dump->Put(base, m, s.tick());
    s.NoteSum(s.tick(), sm.b);
#ifdef _WIN32
    if (g_rbs.in_fight) g_csv.Put(g_who_name(s), g_rbs.fights, s.tick() - g_rbs.t0, sm);
    if (g_rbs.in_fight) dbz3::net::rb::DumpAt(g_who_name(s), g_rbs.fights, s.tick() - g_rbs.t0, base + m);
    if (g_rbs.in_fight) dbz3::net::rb::DumpStatics(base, g_who_name(s), g_rbs.fights, s.tick() - g_rbs.t0);
#endif
  }
#ifdef _WIN32
  if (gate) g_gate.Frame(base, m);
#endif
  return m != 0;
}
}  // namespace

namespace dbz3::net { extern std::atomic<uint32_t> g_vblank; }
// R7: callback de vblank del D3D del guest (r3 == 0 = vblank); solo cuenta, no cambia nada. Solo imagen US.
REX_HOOK_RAW(sub_82230E48) {
  if (ctx.r3.u32 == 0) dbz3::net::g_vblank.fetch_add(1, std::memory_order_relaxed);
  __imp__sub_82230E48(ctx, base);
}

#ifdef _WIN32
static bool g_in_cosm = false;   // dentro de las rutinas con LFSR propio (WithCosmRng)
// R3 (pruebas, DBZ3_RB_RNGCALLS): quien llama al paso del LFSR (sub_82084A78)
// PRUEBA (DBZ3_RB_ORACLE): llamadas al LFSR de la logica por llamante, del tick jugado (fwd) y del resimulado (res)
static std::map<uint32_t, uint32_t> g_calls_fwd[4], g_calls_res;
// DBZ3_RB_DUMP: llamadas al LFSR de la logica por llamante de la ULTIMA ejecucion de cada tick (jugado o resimulado)
struct TickCalls { uint32_t tick = 0xFFFFFFFFu; std::map<uint32_t, uint32_t> c; };
static TickCalls g_ticklog[64];
static uint32_t g_exec_tick = 0xFFFFFFFFu;
static bool g_ticklog_on = false;
static int g_calls_mode = 0;   // 0 no cuenta, 1 jugando (indice g_calls_idx), 2 resimulando
static uint32_t g_calls_idx = 0;
DBZ3_HOOK(sub_82084A78, dbz3eu_sub_82084A78) {
  if (g_gate.rngcalls() && !g_in_cosm) g_gate.NoteRng(ctx.lr);
  if (g_calls_mode && !g_in_cosm) ++(g_calls_mode == 2 ? g_calls_res : g_calls_fwd[g_calls_idx & 3])[uint32_t(ctx.lr)];
  if (g_ticklog_on && !g_in_cosm && g_exec_tick != 0xFFFFFFFFu) ++g_ticklog[g_exec_tick & 63].c[uint32_t(ctx.lr)];
  orig(ctx, base);
}
#endif

#ifdef _WIN32
// R3 (pruebas, DBZ3_RB_RESIM): un tick de la logica = el cuerpo del bucle principal sub_82165B48 (generated/dbz3_recomp.14.cpp
// 0x82165C28) desde el sondeo de mandos hasta la vuelta a el, sin la espera del vsync (vcall de [r30+40]) ni el test de salida.
// Cada bit de la mascara incluye una de las llamadas; el resultado de la prueba dice cuales hacen falta. Solo imagen US.
#define RB_EXT(n) REX_EXTERN(n); DBZ3_EXTERN_EU(DBZ3EU_##n)
RB_EXT(sub_820860B8);
RB_EXT(sub_82308398);
RB_EXT(sub_820927E0);
RB_EXT(sub_821DA2B0);
RB_EXT(sub_820921E0);
RB_EXT(sub_82084A78);
REX_EXTERN(sub_822BD7D8);   // solo prueba (mascara bit 17), solo US
RB_EXT(sub_82095530);
RB_EXT(sub_820A2AD8);
RB_EXT(sub_820A32D0);
RB_EXT(sub_82095628);
RB_EXT(sub_82082338);
RB_EXT(sub_82088F70);
RB_EXT(sub_82084AC0);
RB_EXT(sub_82084820);
RB_EXT(sub_8208B3E0);
void ResimTick(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig, uint32_t mask) {
  struct Step { void (*fn)(PPCContext&, uint8_t*); int r3; };   // r3: 0 nada, 1 = 32, 2 = [r31+25184], 3 = [r1+80]
  const Step kSteps[16] = {   // sin static: la direccion depende de la imagen (US/EU) que arranco
      {DBZ3_CALL_EU(sub_820860B8), 0}, {DBZ3_CALL_EU(sub_82308398), 0}, {DBZ3_CALL_EU(sub_820927E0), 0}, {DBZ3_CALL_EU(sub_821DA2B0), 0}, {DBZ3_CALL_EU(sub_820921E0), 0}, {DBZ3_CALL_EU(sub_82084A78), 1},
      {DBZ3_CALL_EU(sub_82308398), 0}, {DBZ3_CALL_EU(sub_82095530), 2}, {DBZ3_CALL_EU(sub_820A2AD8), 3}, {DBZ3_CALL_EU(sub_820A32D0), 3}, {DBZ3_CALL_EU(sub_82095628), 2}, {DBZ3_CALL_EU(sub_82082338), 0},
      {DBZ3_CALL_EU(sub_82088F70), 0}, {DBZ3_CALL_EU(sub_82084AC0), 0}, {DBZ3_CALL_EU(sub_82084820), 0}, {DBZ3_CALL_EU(sub_8208B3E0), 0}};
  static const bool trace = std::getenv("DBZ3_RB_RESIMTRACE") != nullptr;
  const uint32_t r31 = ctx.r31.u32, r1 = ctx.r1.u32, r30 = ctx.r30.u32;
  orig(ctx, base);                                                          // mandos (siempre)
  for (int b = 0; b < 16; ++b) {
    if (b == 9) {
    if ((mask >> 16) & 1) {   // vcall del bucle: [[r30+40]+0][+4]() (lo que hace al final de cada iteracion; puede esperar al vsync)
      if (trace) REXLOG_INFO("dbz3 rb: resim vcall");
      ctx.r3.u64 = REX_LOAD_U32(ctx.r30.u32 + 40);
      ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
      ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 4);
      ctx.ctr.u64 = ctx.r11.u64;
      REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    }
    if ((mask >> 17) & 1) { ctx.r3.u64 = REX_LOAD_U32(ctx.r1.u32 + 80); sub_822BD7D8(ctx, base); }
    }
    if (!((mask >> b) & 1)) continue;
    const Step& st = kSteps[b];
    if (st.r3 == 1) ctx.r3.s64 = 32;
    else if (st.r3 == 2) ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 25184);
    else if (st.r3 == 3) ctx.r3.u64 = REX_LOAD_U32(ctx.r1.u32 + 80);
    const auto t0 = std::chrono::steady_clock::now();
    st.fn(ctx, base);
    if (trace) {
      static int n = 0;
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (ms > 0.3 && n++ < 60) REXLOG_INFO("dbz3 rb: resim paso {} tardo {:.2f} ms", b, ms);
    }
  }
  // R4: sub_820921E0 (bit 4 de la mascara, tail call a sub_8209DA08) suma 1 al contador de tick 0x8238D6B4 y no hace falta para la
  // simulacion (R3), pero el contador se compara con estado guardado: sin el, tras resimular queda atrasado. Medido con DBZ3_RB_ORACLE.
  if (!((mask >> 4) & 1)) REX_STORE_U32(dbz3::GA(0x8238D6B4), REX_LOAD_U32(dbz3::GA(0x8238D6B4)) + 1);
  ctx.r31.u64 = r31;
  ctx.r30.u64 = r30;
  ctx.r1.u64 = r1;
}


// R3: sub_82153FD8 y sub_82150310 (efectos / sonido: objetos en 0x8243xxxx y 0x7018xxxx fuera del gestor) gastan numeros del
// mismo LFSR que la logica de pelea, y cuantas veces los gastan depende de su estado (no guardado): tras restaurar el LFSR de
// la logica se desvia en el tick 1. Solucion medida: llevan su propio LFSR (cosmetico); la logica ya no los ve. Los dos PCs
// hacen lo mismo, asi que no rompe el lockstep. Apagado salvo DBZ3_RB_COSMRNG (pruebas). Solo imagen US (sin pareja EU medida).
uint32_t g_cosm_rng = 0x1234567u;
void WithCosmRng(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig) {
  static const bool env = std::getenv("DBZ3_RB_COSMRNG") != nullptr;
  if (!(env || dbz3::net::Session::Get().rb_on())) { orig(ctx, base); return; }   // R4: con rollback siempre
  if (g_in_cosm) { orig(ctx, base); return; }   // anidada: ya corre con el LFSR propio (si no, la salida de la interna cortaba el aislamiento)
  const uint32_t game = REX_LOAD_U32(kRng);
  REX_STORE_U32(kRng, g_cosm_rng);
  g_in_cosm = true;
  orig(ctx, base);
  g_in_cosm = false;
  g_cosm_rng = REX_LOAD_U32(kRng);
  REX_STORE_U32(kRng, game);
}
DBZ3_HOOK_EU(sub_82153FD8) {
  if (g_gate.rngcalls()) g_gate.NoteObj(0x82153FD8, uint32_t(ctx.lr), ctx.r3.u32);
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_820CD040) {   // reserva de canal de efecto/voz (tabla de 40 B en 0x823A5B10: ocupado o libre cambia el numero de pasos)
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_821554E8) {
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_8213EA28) {   // R4 (medido con DBZ3_RB_ORACLE): otro consumidor cosmetico del LFSR (efectos)
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_820CCF20) {
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_8214FC10) {   // R4: consumidor del LFSR que se disparaba en ticks distintos tras un rollback (medido con DBZ3_RB_DUMP)
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_82155228) {
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_82140BF8) {
  WithCosmRng(ctx, base, orig);
}
DBZ3_HOOK_EU(sub_82150310) {
  if (g_gate.rngcalls()) g_gate.NoteObj(0x82150310, uint32_t(ctx.lr), ctx.r3.u32);
  WithCosmRng(ctx, base, orig);
}


// (sigue el bloque solo Windows: el LFSR cosmetico y el rollback no existen en Linux)
// ---------------------------------------------------------------- R4: nucleo del rollback (ver rollback.h, netplay.h)
namespace {
// PRUEBA (DBZ3_RB_ORACLE=3): paginas de memoria del invitado que toca el hilo del juego durante una resimulacion. Se protegen con PAGE_NOACCESS las
// regiones RW comprometidas; el primer acceso de cada pagina cae en el VEH, que la apunta (solo si es el hilo del juego) y la reabre.
struct PageTrap {
  static inline std::vector<std::pair<uint8_t*, size_t>> regs;
  static inline std::set<uint32_t> touched;   // numero de pagina de 4 KB (direccion del invitado >> 12)
  static inline uint8_t* gbase = nullptr;
  static inline DWORD tid = 0;
  static inline std::atomic<bool> armed{false};
  static LONG CALLBACK H(EXCEPTION_POINTERS* e) {
    if (!armed.load() || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t* a = reinterpret_cast<uint8_t*>(e->ExceptionRecord->ExceptionInformation[1]);
    if (a < gbase || a >= gbase + 0xC0000000ull) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t* pg = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(a) & ~uintptr_t(0xFFF));
    DWORD old = 0;
    VirtualProtect(pg, 0x1000, PAGE_READWRITE, &old);
    if (GetCurrentThreadId() == tid) touched.insert(uint32_t(pg - gbase) >> 12);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  static void Arm(uint8_t* base) {
    static void* veh = AddVectoredExceptionHandler(1, H);
    (void)veh;
    gbase = base; tid = GetCurrentThreadId(); touched.clear(); regs.clear();
    uint8_t* p = base;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < base + 0xC0000000ull && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
      uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress);
      const uint64_t g0 = uint64_t(b - base);
      if (mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE && !(g0 >= 0x90000000ull && g0 < 0xA0000000ull) && g0 + mbi.RegionSize <= 0xC0000000ull)
        regs.push_back({b, mbi.RegionSize});
      p = b + mbi.RegionSize;
    }
    armed = true;
    for (auto& r : regs) { DWORD old; VirtualProtect(r.first, r.second, PAGE_NOACCESS, &old); }
  }
  static void Disarm() {
    for (auto& r : regs) { DWORD old; VirtualProtect(r.first, r.second, PAGE_READWRITE, &old); }   // primero reabrir: un fallo de otro hilo con armed=false no se atenderia
    armed = false;
  }
};
}  // namespace

namespace {
// Pasos del bucle principal que se repiten al resimular (bit n = n-esima llamada de kSteps; ver ResimTick). 0x0024 = mandos +
// sub_820927E0 + LFSR (R3: basta para los 5 bloques con los mismos mandos). DBZ3_RB_MASK la cambia (pruebas).
uint32_t RbMask() {
  static const uint32_t m = std::getenv("DBZ3_RB_MASK") ? uint32_t(std::strtoul(std::getenv("DBZ3_RB_MASK"), nullptr, 16)) : 0x0024u;
  return m;
}
using Clk = std::chrono::steady_clock;
double MsSince(Clk::time_point t) { return std::chrono::duration<double, std::milli>(Clk::now() - t).count(); }

void RbWriteSummary(dbz3::net::Session& s) {
  auto& R = g_rbs;
  const auto& st = s.rb_stats();
  const auto f = dbz3::net::rb::OutDir() / "rb_resumen.csv";
  const bool nuevo = !std::filesystem::exists(f);
  std::ofstream o(f, std::ios::app);
  if (nuevo)
    o << "rol,combate,ticks,rollbacks,d1,d2,d3,d4,d5,d6,d7,d8,ticks_resim,esperas,predichos,max_sin_confirmar,retardo,ping_ms,"
         "frame_p50,frame_p95,frame_p99,frame_max,resim_ms_tick_p50,resim_ms_tick_p95,resim_ms_tick_max,guardar_ms_p50,restaurar_ms_p50,"
         "sumas_comparadas,sumas_malas,paradas_sync,reparaciones\n";
  char line[640];
  using dbz3::net::rb::Pctl;
  std::snprintf(line, sizeof line, "%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%u,%u,%u,%u\n",
                g_who_name(s), R.fights, st.ticks, st.rollbacks, st.depth[1], st.depth[2], st.depth[3], st.depth[4], st.depth[5], st.depth[6],
                st.depth[7], st.depth[8], st.resim_ticks, st.stalls, st.predicted, st.max_unconf, unsigned(s.delay()), s.ping_ms(),
                Pctl(R.frame_ms, 0.5), Pctl(R.frame_ms, 0.95), Pctl(R.frame_ms, 0.99), Pctl(R.frame_ms, 1), Pctl(R.resim_ms, 0.5),
                Pctl(R.resim_ms, 0.95), Pctl(R.resim_ms, 1), Pctl(R.core.save_ms_, 0.5), Pctl(R.core.rest_ms_, 0.5),
                s.sums_compared() - R.sums_cmp0, s.sums_bad() - R.sums_bad0, st.lead_stalls, st.repairs);
  o << line;
  REXLOG_INFO("dbz3 rb: resumen del combate {}: {} ticks, {} rollbacks (prof 1:{} 2:{} 3:{} 4+:{}), {} esperas, frame p50/p99 {:.1f}/{:.1f} ms, "
              "sumas {} / malas {}",
              R.fights, st.ticks, st.rollbacks, st.depth[1], st.depth[2], st.depth[3], st.depth[4] + st.depth[5] + st.depth[6] + st.depth[7] + st.depth[8],
              st.stalls, Pctl(R.frame_ms, 0.5), Pctl(R.frame_ms, 0.99), s.sums_compared() - R.sums_cmp0, s.sums_bad() - R.sums_bad0);
}

// Inicio del combate (state 4), con o sin rollback: lo usan los guiones de prueba y los registros
void RbTrack(uint8_t* base, dbz3::net::Session& s) {
  auto& R = g_rbs;
  if (!s.active()) { R.in_fight = false; return; }
  const bool f = dbz3::net::rb::Fight(base) != 0;
  if (f && !R.in_fight) {
    R.in_fight = true; R.t0 = s.tick(); ++R.fights;
    // PRUEBAS (DBZ3_RB_SEED): el RNG del combate parte igual en todas las corridas (la carga lo deja distinto cada vez)
    if (const char* sd = std::getenv("DBZ3_RB_SEED")) { REX_STORE_U32(dbz3::net::rb::kRngAddr, uint32_t(std::strtoul(sd, nullptr, 0)) | 1u); }
  }
  if (!f && !s.rb_mode()) { if (R.in_fight) g_csv.Flush(); R.in_fight = false; }
}

// Dentro del combate con rollback: reconcilia con la red, resimula si el rival hizo otra cosa de lo predicho, espera si hay
// ya demasiados ticks sin confirmar, guarda la instantanea de este tick y manda las sumas de lo confirmado.
void OnSumBadRb(uint32_t tick, uint32_t mask) {   // DBZ3_RB_DUMP: vuelca la instantanea del tick malo (si sigue en el anillo) para compararla con la del otro PC
  const uint8_t* d = g_rbs.core.Get(tick);
  if (!d) return;
  std::ofstream(dbz3::net::rb::OutDir() / (std::string("dump_") + g_who_name(dbz3::net::Session::Get()) + "_bad" + std::to_string(tick - g_rbs.t0) + ".bin"),
                std::ios::binary).write(reinterpret_cast<const char*>(d), dbz3::net::rb::kMgrLen);
  (void)mask;
  static int shown = 0;
  if (shown++ < 12) {   // quien gasto numeros del LFSR de la logica en los ticks cercanos (comparar los dos logs)
    for (uint32_t k = tick > 1 ? tick - 1 : 0; k <= tick; ++k) {
      const auto& tl = g_ticklog[k & 63];
      if (tl.tick != k) continue;
      std::string txt;
      char b[48];
      for (auto& [lr, c] : tl.c) { std::snprintf(b, sizeof b, " %08X:%u", lr, c); txt += b; }
      REXLOG_INFO("dbz3 rb: LFSR tick rel {}:{}", k - g_rbs.t0, txt);
    }
  }
}

void RbBegin(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig, dbz3::net::Session& s) {
  auto& R = g_rbs;
  static const bool dump = std::getenv("DBZ3_RB_DUMP") != nullptr;
  // R7: antes pisaba sum_hook en cada tick tambien en lockstep -> OnSumBad (volcado de RbFrame) nunca se llamaba y DBZ3_RB_DUMP no escribia nada
  if (dump && s.active() && s.rb_on()) { dbz3::net::Session::sum_hook = &OnSumBadRb; g_ticklog_on = true; }
  if (!s.active() || !s.rb_on()) return;
  const uint32_t m = dbz3::net::rb::Fight(base);
  const uint32_t t = s.tick();
  bool leaving = false;
  if (!s.rb_mode()) {
    if (!m || dbz3::tag::DynamicTeam(0).size() >= 2 || dbz3::tag::DynamicTeam(1).size() >= 2) return;   // Tag Team (relevos): sin rollback
    R.core.Init(m);
    R.mgr = m;
    R.frame_ms.clear(); R.resim_ms.clear();
    R.sums_cmp0 = s.sums_compared(); R.sums_bad0 = s.sums_bad();
    R.last = {};
    R.t0_rb = t;
    s.RbEnter(t);
  } else if (!m) {
    leaving = true;   // el combate acabo: antes de seguir en lockstep, todo lo anterior tiene que estar confirmado
  }
  if (!leaving) {
    if (R.last != Clk::time_point{}) R.frame_ms.push_back(float(MsSince(R.last)));
    R.last = Clk::now();
  }
  // Sincronia (como GGPO): si este PC lleva mas de LEAD_MAX ticks de ventaja sobre el rival, se salta un vsync (el juego sigue a 60 Hz
  // y el rival lo alcanza); asi los rollbacks se reparten entre los dos en vez de caer todos en el que va por delante.
  if (!leaving) {
    static const double lead_max = std::getenv("DBZ3_RB_LEAD") ? std::atof(std::getenv("DBZ3_RB_LEAD")) : 0.3;
    static uint32_t last_stall = 0;
    const double lead = s.RbLead(t);
    static double lead_sum = 0;
    static uint32_t lead_n = 0;
    lead_sum += lead;
    if (++lead_n % 120 == 0) { REXLOG_INFO("dbz3 rb: ventaja de reloj {:.2f} ticks (media {:.2f}; >0 = este PC va por delante)", lead, lead_sum / lead_n); }
    if (t >= R.t0_rb + 20 && t - last_stall >= 8 && lead > lead_max) {
      last_stall = t;
      ++s.rb_stats().lead_stalls;
      std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }
  }
  static const bool oracle_on = std::getenv("DBZ3_RB_ORACLE") != nullptr;
  if (oracle_on && !leaving) { g_calls_mode = 1; g_calls_idx = t; g_calls_fwd[t & 3].clear(); }
  // vuelve a la instantanea del tick u y resimula hasta t; false si ya no esta en el anillo
  auto resim = [&](uint32_t u, const char* why) {
    const auto t0 = Clk::now();
    if (!R.core.Restore(base, u)) return false;
    for (uint32_t k = u; k < t; ++k) {
      s.RbBeginResim(k);
      if (g_ticklog_on) { g_exec_tick = k; g_ticklog[k & 63].tick = k; g_ticklog[k & 63].c.clear(); }
      ResimTick(ctx, base, orig, RbMask());   // mandos + sub_820927E0 + paso del LFSR
      R.core.Save(base, k + 1);
    }
    s.RbEndResim();
    const double ms = MsSince(t0);
    R.resim_ms.push_back(float(ms / double(t - u)));
    s.RbNoteResim(t - u, ms / double(t - u));
    static int logged = 0;
    if (logged++ < 40) REXLOG_INFO("dbz3 rb: {} en el tick {} desde {} ({} ticks, {:.2f} ms, confirmado hasta {})", why, t, u, t - u, ms, s.rb_conf());
    return true;
  };
  // Reparacion: el anfitrion manda su instantanea del primer tick malo; el invitado la adopta y resimula
  if (!leaving) {
    if (s.role() == dbz3::net::Role::kHost) {
      s.RbSnapPump();
      const uint32_t bad = s.RbBadTick();
      if (bad != 0xFFFFFFFFu && !s.rb_snap_busy()) {
        uint32_t k = bad;
        while (k < t && !R.core.Get(k)) ++k;   // el ring es corto: la mas vieja que siga estando
        if (k < t && s.rb_repairs() < 30 && s.RbSnapSend(k, R.core.Get(k), R.core.size())) {
          REXLOG_WARN("dbz3 rb: DESINCRONIA en el tick {} (relativo {}): se manda la instantanea del tick {} al invitado", bad, bad - R.t0, k);
          s.RbRepaired(k);
          ++s.rb_stats().repairs;
        } else {
          s.RbRepaired(bad);   // no hay instantanea o demasiadas reparaciones: se deja (lo vera el informe)
        }
      }
    } else {
      uint32_t k = 0;
      std::vector<uint8_t> data;
      if (s.RbSnapTake(&k, &data)) {
        if (k < t && R.core.Adopt(k, data.data(), data.size()) && resim(k, "reparacion")) {
          ++s.rb_stats().repairs;
          REXLOG_WARN("dbz3 rb: reparacion aplicada: estado del anfitrion en el tick {} y {} ticks resimulados", k, t - k);
        } else {
          REXLOG_WARN("dbz3 rb: instantanea del anfitrion del tick {} no aplicable (tick actual {})", k, t);
        }
        s.RbRepaired(k);
      }
    }
  }
  uint64_t wait0 = 0;
  for (;;) {
    const uint32_t u = s.RbScan(t);
    if (!s.active()) { s.RbLeave(); return; }
    if (u != 0xFFFFFFFFu) {
      if (!resim(u, "rollback")) {
        REXLOG_ERROR("dbz3 rb: sin instantanea del tick {} (actual {}): se cierra la partida", u, t);
        s.RequestLeave();
        s.RbLeave();
        return;
      }
      continue;   // la red pudo traer mas: vuelve a mirar
    }
    const int w = s.RbWait(t, &wait0, leaving);
    if (w < 0) { s.RbLeave(); return; }
    if (w > 0) break;
  }
  // ORACULO 4 (DBZ3_RB_ORACLE=4): conjunto de escrituras de UN tick de simulacion. Hash por bloque de 128 B de toda la memoria comprometida antes
  // y despues de resimular un tick (solo mandos + sub_820927E0 + LFSR); los bloques que cambian son lo que la simulacion escribe. Se cuentan las
  // muestras en que cada uno cambia; el informe (escrituras_<pid>.txt) separa lo que ya se guarda (region) de lo que no.
  {
    static const bool oracle4 = std::getenv("DBZ3_RB_ORACLE") && std::atoi(std::getenv("DBZ3_RB_ORACLE")) == 4;
    static uint32_t muestras4 = 0;
    if (oracle4 && !leaving && t >= R.t0 + 8 && t % 10 == 0 && s.rb_conf() >= t && R.core.Get(t - 1) && muestras4 < 24) {
      struct Blocks { std::vector<uint64_t> h; std::vector<uint32_t> a; };
      auto grab = [&](Blocks& out) {
        out.h.clear(); out.a.clear();
        std::vector<uint8_t> buf(1 << 20);
        uint8_t* pp = base;
        MEMORY_BASIC_INFORMATION mbi;
        while (pp < base + 0xC0000000ull && VirtualQuery(pp, &mbi, sizeof(mbi)) == sizeof(mbi)) {
          uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress);
          uint8_t* e = b + mbi.RegionSize;
          const uint64_t g0 = uint64_t(b - base);
          const bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0 && !(g0 >= 0x90000000ull && g0 < 0xA0000000ull);
          if (ok)
            for (uint8_t* q = b; q < e && q < base + 0xC0000000ull; q += buf.size()) {
              const size_t n = std::min<size_t>(buf.size(), size_t(e - q));
              SIZE_T got = 0;
              if (!ReadProcessMemory(GetCurrentProcess(), q, buf.data(), n, &got) || got != n) continue;
              for (size_t o = 0; o + 128 <= n; o += 128) { out.a.push_back(uint32_t(q - base) + uint32_t(o)); out.h.push_back(XXH3_64bits(buf.data() + o, 128)); }
            }
          pp = e;
        }
      };
      Blocks h0, h1;
      R.core.Restore(base, t - 1);
      grab(h0);
      s.RbBeginResim(t - 1, false);
      ResimTick(ctx, base, orig, RbMask());
      s.RbEndResim();
      grab(h1);
      static std::map<uint32_t, uint32_t> escr;
      if (h0.a == h1.a) {
        for (size_t i = 0; i < h0.a.size(); ++i) if (h0.h[i] != h1.h[i]) ++escr[h0.a[i]];
      }
      ++muestras4;
      if (muestras4 % 4 == 0) {
        std::ofstream o(dbz3::net::rb::OutDir() / ("escrituras_" + std::to_string(GetCurrentProcessId()) + ".txt"));
        o << "muestras " << muestras4 << ", gestor 0x" << std::hex << R.mgr << std::dec << "; bloques de 128 B escritos por la simulacion en un tick (en >= la mitad de las muestras), fusionados\n";
        uint32_t ini = 0, fin = 0, cnt = 0, lines = 0;
        uint64_t total = 0;
        auto flush = [&]() {
          if (!ini) return;
          total += uint64_t(fin - ini);
          const bool region = ini + 128 > R.mgr && fin <= R.mgr + 35200 + 128;
          if (lines++ < 500) { char l[96]; std::snprintf(l, sizeof l, "%08X+%u (%u bloques)%s\n", ini, fin - ini, cnt, region ? "  [gestor/luchadores: ya guardado]" : ""); o << l; }
        };
        for (auto& [a, c] : escr) {
          if (c * 2 < muestras4) continue;
          if (ini && a <= fin + 128) { fin = a + 128; ++cnt; } else { flush(); ini = a; fin = a + 128; cnt = 1; }
        }
        flush();
        o << "total " << total << " bytes en " << lines << " grupos\n";
      }
    }
  }
  // PRUEBA DE PUREZA (DBZ3_RB_ORACLE=1, red sin retraso: todo confirmado): vuelve 2 ticks, resimula con mandos FALSOS, vuelve otra vez
  // y resimula con los verdaderos; el resultado tiene que ser el estado de partida. Si no, hay estado que depende del mando y no se restaura.
  static const bool oracle = std::getenv("DBZ3_RB_ORACLE") != nullptr && std::atoi(std::getenv("DBZ3_RB_ORACLE")) != 4;
  if (oracle && !leaving && t >= R.t0 + 4 && t % 5 == 0 && s.rb_conf() >= t && R.core.Get(t - 2) && R.core.HasXr(t - 2)) {
    static uint32_t runs = 0, bad = 0;
    // PRUEBA (DBZ3_RB_XR=HEAP): aprende, en 8 muestras, que bloques de 128 B del monton 0x40100000..0x40180000 escribe la simulacion en un tick
    // y los anade al anillo (restaurarlos). Tras aprender, el anillo se vacia y la prueba sigue sola cuando vuelve a haber instantaneas.
    bool learning_now = false;
    {
      static const char* xr = std::getenv("DBZ3_RB_XR");
      static const bool heap = xr && std::strcmp(xr, "HEAP") == 0;
      static uint32_t learn = 0;
      static std::map<uint32_t, uint32_t> cnt;
      if (heap && learn < 8) {
        auto grab = [&](std::vector<uint8_t>& o) {
          o.assign(0x80000, 0);
          for (uint32_t i = 0; i < 0x80000; i += 0x1000) ReadProcessMemory(GetCurrentProcess(), base + 0x40100000u + i, o.data() + i, 0x1000, nullptr);
        };
        std::vector<uint8_t> h0, h1;
        R.core.Restore(base, t - 2); grab(h0);
        s.RbBeginResim(t - 2, false); ResimTick(ctx, base, orig, RbMask());
        grab(h1);
        s.RbBeginResim(t - 1, false); ResimTick(ctx, base, orig, RbMask());
        s.RbEndResim();
        for (uint32_t o = 0; o < 0x80000; o += 128) if (std::memcmp(h0.data() + o, h1.data() + o, 128)) ++cnt[0x40100000u + o];
        if (++learn == 8) {
          std::vector<dbz3::net::rb::Core::Run> v;
          uint32_t ini = 0, fin = 0;
          for (auto& [a, c] : cnt) {
            if (c * 2 < 8) continue;
            if (ini && a <= fin) fin = a + 128; else { if (ini) v.push_back({ini, fin - ini}); ini = a; fin = a + 128; }
          }
          if (ini) v.push_back({ini, fin - ini});
          uint32_t tot = 0; for (auto& r : v) tot += r.n;
          R.core.AddRuns(v);
          REXLOG_INFO("dbz3 rb: XR=HEAP aprendidos {} tramos, {} bytes", v.size(), tot);
        }
        learning_now = true;
      }
    }
    if (!learning_now) {
    std::vector<uint8_t> ref, after;
    R.core.Live(base, ref);
    // ORACULO 2 (DBZ3_RB_ORACLE=2): que memoria FUERA de la region guardada cambia por culpa de los mandos falsos. Se compara
    // 'resimular con los verdaderos' dos veces (ruido: relojes) contra 'falsos y luego verdaderos' en 3 MB de estaticos + 512 KB de monton.
    static const bool oracle2 = std::getenv("DBZ3_RB_ORACLE") && std::atoi(std::getenv("DBZ3_RB_ORACLE")) == 2;
    if (oracle2 && runs < 120 && runs % 2 == 1) {
      auto grab = [&](std::vector<uint8_t>& out) {
        out.assign(0x300000 + 0x80000, 0);
        for (uint32_t o = 0; o < 0x300000; o += 0x10000) ReadProcessMemory(GetCurrentProcess(), base + 0x82300000u + o, out.data() + o, 0x10000, nullptr);
        for (uint32_t o = 0; o < 0x80000; o += 0x10000) ReadProcessMemory(GetCurrentProcess(), base + 0x70180000u + o, out.data() + 0x300000 + o, 0x10000, nullptr);
      };
      auto run_true = [&]() { R.core.Restore(base, t - 2); for (uint32_t k = t - 2; k < t; ++k) { s.RbBeginResim(k, false); ResimTick(ctx, base, orig, RbMask()); } s.RbEndResim(); };
      std::vector<uint8_t> a1, a2, b2;
      run_true(); grab(a1);
      run_true(); grab(a2);
      R.core.Restore(base, t - 2); for (uint32_t k = t - 2; k < t; ++k) { s.RbBeginResim(k, true); ResimTick(ctx, base, orig, RbMask()); } s.RbEndResim();
      run_true(); grab(b2);
      // acumula, por bloque de 16 bytes, en cuantas muestras cambio lo que el ruido (a1 contra a2) no explica
      static std::map<uint32_t, uint32_t> cuenta;
      static uint32_t muestras = 0;
      for (size_t i = 0; i < a1.size(); ++i)
        if (b2[i] != a1[i] && a2[i] == a1[i]) {
          const uint32_t addr = i < 0x300000 ? 0x82300000u + uint32_t(i) : 0x70180000u + uint32_t(i - 0x300000);
          ++cuenta[addr & ~15u];
          i |= 15;
        }
      ++muestras;
      if (muestras >= 6 && muestras % 3 == 0) {   // bloques que cambian en al menos la mitad de las muestras, fusionados
        std::string d;
        uint32_t ini = 0, fin = 0, n = 0;
        auto flush = [&]() { if (ini) { char l[48]; std::snprintf(l, sizeof l, " %08X+%u", ini, fin - ini); d += l; ++n; } };
        for (auto& [a, c] : cuenta) {
          if (c * 2 < muestras) continue;
          if (ini && a <= fin + 16) fin = a + 16; else { flush(); ini = a; fin = a + 16; }
        }
        flush();
        REXLOG_INFO("dbz3 rb: ORACULO2 {} muestras: bloques fuera de la region que cambian en >= la mitad por mandos falsos ({}):{}", muestras, n, d);
      }
    }
    // ORACULO 3 (DBZ3_RB_ORACLE=3): mapa completo de la fuga. Hash por bloque de 64 B de TODA la memoria comprometida del invitado (< 0xC0000000, sin
    // el espejo 0x9...) tras (A1) resimular con los verdaderos, (A2) otra vez (ruido) y (B) falsos y luego verdaderos. Fuga = bloques donde B != A1 y
    // A2 == A1, fuera de la region guardada. Informe en fuga_<pid>.txt con los grupos de bloques y cuantas muestras los repitieron.
    static const bool oracle3 = std::getenv("DBZ3_RB_ORACLE") && std::atoi(std::getenv("DBZ3_RB_ORACLE")) == 3;
    if (oracle3 && runs < 24 && runs % 2 == 1) {
      struct Blocks { std::vector<uint64_t> h; std::vector<uint32_t> a; };   // hash de cada bloque de 128 B y su direccion (crecientes)
      auto grab = [&](Blocks& out) {
        out.h.clear(); out.a.clear();
        std::vector<uint8_t> buf(1 << 20);
        uint8_t* p = base;
        MEMORY_BASIC_INFORMATION mbi;
        while (p < base + 0xC0000000ull && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
          uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress);
          uint8_t* e = b + mbi.RegionSize;
          const uint64_t g0 = uint64_t(b - base);
          const bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0 && !(g0 >= 0x90000000ull && g0 < 0xA0000000ull);
          if (ok) {
            for (uint8_t* q = b; q < e && q < base + 0xC0000000ull;) {
              const size_t n = std::min<size_t>(buf.size(), size_t(e - q));
              SIZE_T got = 0;
              if (ReadProcessMemory(GetCurrentProcess(), q, buf.data(), n, &got) && got == n) {
                for (size_t o = 0; o + 128 <= n; o += 128) { out.a.push_back(uint32_t(q - base) + uint32_t(o)); out.h.push_back(XXH3_64bits(buf.data() + o, 128)); }
              }
              q += n;
            }
          }
          p = e;
        }
      };
      auto run_true = [&]() { R.core.Restore(base, t - 2); for (uint32_t k = t - 2; k < t; ++k) { s.RbBeginResim(k, false); ResimTick(ctx, base, orig, RbMask()); } s.RbEndResim(); };
      Blocks a1, a2, b2;
      R.core.Restore(base, t - 2);
      PageTrap::Arm(base);
      for (uint32_t k = t - 2; k < t; ++k) { s.RbBeginResim(k, false); ResimTick(ctx, base, orig, RbMask()); }
      PageTrap::Disarm();
      s.RbEndResim();
      const std::set<uint32_t> tocadas = PageTrap::touched;   // paginas que lee o escribe la simulacion en los 2 ticks verdaderos
      grab(a1);
      run_true(); grab(a2);
      R.core.Restore(base, t - 2); for (uint32_t k = t - 2; k < t; ++k) { s.RbBeginResim(k, true); ResimTick(ctx, base, orig, RbMask()); } s.RbEndResim();
      run_true(); grab(b2);
      static std::map<uint32_t, uint32_t> fuga, fugaT;   // fugaT: bloques con fuga cuya pagina toco la simulacion verdadera
      static uint32_t muestras = 0;
      uint32_t nuevos = 0;
      {   // recorre las tres listas a la vez (las direcciones crecen); solo cuentan los bloques que estan en las tres
        size_t i = 0, j = 0, k = 0;
        while (i < a1.a.size() && j < a2.a.size() && k < b2.a.size()) {
          const uint32_t x = std::max({a1.a[i], a2.a[j], b2.a[k]});
          while (i < a1.a.size() && a1.a[i] < x) ++i;
          while (j < a2.a.size() && a2.a[j] < x) ++j;
          while (k < b2.a.size() && b2.a[k] < x) ++k;
          if (i >= a1.a.size() || j >= a2.a.size() || k >= b2.a.size()) break;
          if (a1.a[i] == x && a2.a[j] == x && b2.a[k] == x) {
            if (b2.h[k] != a1.h[i] && a2.h[j] == a1.h[i] && !(x + 128 > R.mgr && x < R.mgr + 35200)) { ++fuga[x]; ++nuevos; if (tocadas.count(x >> 12)) ++fugaT[x]; }
            ++i; ++j; ++k;
          }
        }
      }
      ++muestras;
      // PRUEBA (DBZ3_RB_XR=FUGAT): a la 4.a muestra anade al anillo SOLO los bloques con fuga cuya pagina toca la simulacion verdadera (fugaT, en >= la
      // mitad de las muestras), sin la pila de hilo (>= 0x70000000) ni el gestor; despues el ORACULO tick mide si los 150-450 bytes bajan.
      {
        static const char* xrf = std::getenv("DBZ3_RB_XR");
        static bool aplicado = false;
        if (!aplicado && muestras == 4 && xrf && std::strcmp(xrf, "FUGAT") == 0) {
          aplicado = true;
          std::vector<dbz3::net::rb::Core::Run> v;
          uint32_t ini = 0, fin = 0, tot = 0;
          for (auto& [a, c] : fugaT) {
            if (c * 2 < muestras || a >= 0x70000000u || (a + 128 > R.mgr && a < R.mgr + 35200)) continue;
            if (ini && a <= fin) fin = a + 128; else { if (ini) v.push_back({ini, fin - ini}); ini = a; fin = a + 128; }
          }
          if (ini) v.push_back({ini, fin - ini});
          for (auto& r : v) tot += r.n;
          R.core.AddRuns(v);
          std::string d;
          for (auto& r : v) { char l[32]; std::snprintf(l, sizeof l, " %08X+%u", r.a, r.n); d += l; }
          REXLOG_INFO("dbz3 rb: XR=FUGAT anadidos {} tramos, {} bytes:{}", v.size(), tot, d);
        }
      }
      // Quien apunta a los buffers que fugan: con 8 muestras, para cada grupo recurrente (>= 16 bloques y >= 3 apariciones por bloque) busca por
      // toda la memoria palabras de 32 bits (big endian) iguales a su inicio (o a un bloque de los 4 primeros) y las clasifica (gestor, luchador, estaticos, monton)
      if (muestras == 8) {
        std::vector<std::pair<uint32_t, uint32_t>> grupos;   // (inicio, longitud)
        uint32_t ini = 0, fin = 0, cnt = 0, sum = 0;
        auto flush = [&]() { if (ini && cnt >= 16 && sum >= 3 * cnt) grupos.push_back({ini, fin - ini}); };
        for (auto& [a, c] : fuga) {
          if (ini && a <= fin + 256) { fin = a + 128; ++cnt; sum += c; } else { flush(); ini = a; fin = a + 128; cnt = 1; sum = c; }
        }
        flush();
        std::ofstream po(dbz3::net::rb::OutDir() / ("fuga_ptr_" + std::to_string(GetCurrentProcessId()) + ".txt"));
        po << "grupos recurrentes: " << grupos.size() << ", mgr 0x" << std::hex << R.mgr << std::dec << "\n";
        for (auto& [st, ln] : grupos) { char l[64]; std::snprintf(l, sizeof l, "  grupo %08X+%u\n", st, ln); po << l; }
        std::vector<uint8_t> buf(1 << 20);
        uint8_t* pp = base;
        MEMORY_BASIC_INFORMATION mbi;
        uint32_t hits = 0;
        while (pp < base + 0xC0000000ull && VirtualQuery(pp, &mbi, sizeof(mbi)) == sizeof(mbi) && hits < 400) {
          uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress);
          uint8_t* e = b + mbi.RegionSize;
          const uint64_t g0 = uint64_t(b - base);
          const bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0 && !(g0 >= 0x90000000ull && g0 < 0xA0000000ull);
          if (ok)
            for (uint8_t* q = b; q < e && q < base + 0xC0000000ull; q += buf.size()) {
              const size_t n = std::min<size_t>(buf.size(), size_t(e - q));
              SIZE_T got = 0;
              if (!ReadProcessMemory(GetCurrentProcess(), q, buf.data(), n, &got) || got != n) continue;
              for (size_t o = 0; o + 4 <= n; o += 4) {
                uint32_t v;
                std::memcpy(&v, buf.data() + o, 4);
                v = _byteswap_ulong(v);
                for (auto& [st, ln] : grupos) {
                  if (v < st || v >= st + 512 || (v & 3)) continue;   // el inicio del grupo o uno de los 4 primeros bloques
                  const uint32_t holder = uint32_t(q - base) + uint32_t(o);
                  char l[160];
                  const long rm = long(holder) - long(R.mgr);
                  if (rm >= 0 && rm < 35200) {
                    const long rf = rm - 2096;
                    if (rf >= 0 && rf < 2L * 8240) std::snprintf(l, sizeof l, "  %08X (luchador %ld + 0x%lX) -> %08X (grupo %08X + 0x%X)\n", holder, rf / 8240, rf % 8240, v, st, v - st);
                    else std::snprintf(l, sizeof l, "  %08X (gestor + 0x%lX) -> %08X (grupo %08X + 0x%X)\n", holder, rm, v, st, v - st);
                  } else {
                    std::snprintf(l, sizeof l, "  %08X -> %08X (grupo %08X + 0x%X)\n", holder, v, st, v - st);
                  }
                  po << l;
                  ++hits;
                }
              }
            }
          pp = e;
        }
        po << "palabras encontradas: " << hits << "\n";
      }
      if (muestras % 2 == 0 && muestras >= 4) {   // bloques con fuga en >= la mitad de las muestras Y en paginas tocadas por la simulacion verdadera
        std::ofstream o(dbz3::net::rb::OutDir() / ("fugaT_" + std::to_string(GetCurrentProcessId()) + ".txt"));
        uint32_t ini = 0, fin = 0, cnt = 0, lines = 0;
        uint64_t total = 0;
        auto flush = [&]() { if (ini) { total += fin - ini; if (lines++ < 600) { char l[96]; std::snprintf(l, sizeof l, "%08X+%u bloques %u\n", ini, fin - ini, cnt); o << l; } } };
        o << "muestras " << muestras << ", paginas tocadas ult. muestra " << tocadas.size() << ", fuga total " << fuga.size() << " bloques\n";
        for (auto& [a, c] : fugaT) {
          if (c * 2 < muestras) continue;
          if (ini && a <= fin) { fin = a + 128; ++cnt; } else { flush(); ini = a; fin = a + 128; cnt = 1; }
        }
        flush();
        o << "total " << total << " bytes en " << lines << " grupos\n";
      }
      if (muestras % 2 == 0 || muestras <= 2) {
        std::ofstream o(dbz3::net::rb::OutDir() / ("fuga_" + std::to_string(GetCurrentProcessId()) + ".txt"));
        o << "muestras " << muestras << ", bloques de 128 B con fuga (ultima muestra " << nuevos << ", distintos acumulados " << fuga.size() << ")\n";
        uint32_t ini = 0, fin = 0, cnt = 0, sum = 0, lines = 0;
        auto flush = [&]() { if (ini && lines++ < 400) { char l[96]; std::snprintf(l, sizeof l, "%08X+%u bloques %u (apariciones %u)\n", ini, fin - ini, cnt, sum); o << l; } };
        for (auto& [a, c] : fuga) {
          if (ini && a <= fin + 256) { fin = a + 128; ++cnt; sum += c; } else { flush(); ini = a; fin = a + 128; cnt = 1; sum = c; }
        }
        flush();
      }
    }
    std::string rngtxt;
    for (int pass = 0; pass < 2; ++pass) {
      R.core.Restore(base, t - 2);
      for (uint32_t k = t - 2; k < t; ++k) {
        g_calls_mode = pass == 1 ? 2 : 0;
        g_calls_res.clear();
        s.RbBeginResim(k, pass == 0);
        ResimTick(ctx, base, orig, RbMask());
        if (pass == 1) {   // llamantes del LFSR que gastaron distinto en la resimulacion que cuando se jugo el tick
          std::map<uint32_t, std::pair<uint32_t, uint32_t>> m;
          for (auto& [lr, c] : g_calls_fwd[k & 3]) m[lr].first = c;
          for (auto& [lr, c] : g_calls_res) m[lr].second = c;
          static std::set<uint32_t> visto;   // solo los llamantes nuevos (sin los del bucle principal, que son del propio paso de la prueba)
          for (auto& [lr, c] : m)
            if (c.first != c.second && (lr >> 8) != 0x82165C && visto.insert(lr).second) {
              char b[64];
              std::snprintf(b, sizeof b, " [tick %u lr %08X: %u/%u]", k, lr, c.first, c.second);
              rngtxt += b;
            }
        }
      }
      s.RbEndResim();
    }
    g_calls_mode = 1;
    if (!rngtxt.empty()) REXLOG_INFO("dbz3 rb: ORACULO LFSR distinto (jugado/resimulado):{}", rngtxt);
    R.core.Live(base, after);
    std::string txt;
    const size_t n = R.core.Describe(ref, after, &txt);
    ++runs;
    bad += n != 0;
    if ((n && bad <= 30) || runs % 8 == 0 || runs == 1)
      REXLOG_INFO("dbz3 rb: ORACULO tick {}: {} bytes distintos tras mandos falsos + verdaderos ({} pruebas, {} malas){}", t, n, runs, bad, txt);  }
  }
  if (g_ticklog_on) { g_exec_tick = leaving ? 0xFFFFFFFFu : t; if (!leaving) { g_ticklog[t & 63].tick = t; g_ticklog[t & 63].c.clear(); } }
  if (std::getenv("DBZ3_RB_CNT") && t - R.t0_rb < 8)
    REXLOG_INFO("dbz3 rb: contadores tick {}: D6B4 {} 823D0A40 {:08X} 823CEA10 {:08X} D6D8 {}", t, REX_LOAD_U32(0x8238D6B4), REX_LOAD_U32(0x823D0A40), REX_LOAD_U32(0x823CEA10), REX_LOAD_U32(0x8238D6D8));
  if (!leaving && s.rb_conf() >= t) dbz3::net::rb::DumpStatics(base, g_who_name(s), R.fights, t - R.t0);   // estado confirmado: comparable entre PCs
  if (!leaving) R.core.Save(base, t);
  // sumas de los ticks ya confirmados (la instantanea de k es correcta si todo lo anterior a k lo esta)
  const uint32_t last_ok = leaving ? (t ? t - 1 : 0) : std::min(s.rb_conf(), t);
  for (uint32_t k = s.rb_sum_next(); k <= last_ok; ++k) {
    dbz3::net::rb::Sums sm;
    if (R.core.SumsAt(k, &sm)) {
      s.NoteSum(k, sm.b);
      g_csv.Put(g_who_name(s), R.fights, k - R.t0, sm);
      dbz3::net::rb::DumpAt(g_who_name(s), R.fights, k - R.t0, R.core.Get(k));
    }
    s.RbSumDone(k);
  }
  if (leaving) {
    s.RbLeave();
    RbWriteSummary(s);
    g_csv.Flush();
  }
}
}  // namespace
#endif

DBZ3_HOOK(sub_820902F8, dbz3eu_sub_820902C0) {
  auto& s = dbz3::net::Session::Get();
  dbz3::tag::Tick(ctx, base);
  if (!s.active()) dbz3::ingame::pilot::Tick(base);
  if (s.TakeStart(base) && s.role() == dbz3::net::Role::kHost) g_autopick = 0;   // empieza el online
  if (s.active()) s.Arm();
  RbAuto(base, s);
  // Revancha / combates 2.. de una sesion online: tagteam.cpp (sub_82109638) alinea los dos PCs (BattleBarrier) y siembra el RNG solo cuando
  // g_in_battle se reinicio en un menu (OnScreen 5/14), y OnScreen solo lo llamaba OnlineTick, que no corre con la sesion activa (el gancho de
  // mandos va por el camino online): el 2.o combate empezaba en ticks distintos y con otro RNG. Se avisa aqui cada tick de menu con sesion.
  if (s.active()) {
    const int screen = base[dbz3::GuestAddr(0x824BA110, 0x824BA100)];
    if (screen == 5 || screen == 14) dbz3::tag::OnScreen(screen);
    // Revancha: "Battle Again?" se elige en la misma pantalla del combate (15), que no cambia; sin esto el
    // reinicio de luchadores de la pelea siguiente no veia un combate nuevo y no alineaba los dos PCs ni
    // sembraba el RNG (ni salia "empieza un combate"). El estado 7 es el menu final del combate.
    if (screen == 15) {
      const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
      if (mgr && REX_LOAD_U32(mgr + 35128) == 7) dbz3::tag::OnScreen(5);
    }
  }
#ifdef _WIN32
  RbTrack(base, s);
  RbBegin(ctx, base, orig, s);
#endif
  const bool rb_fight = RbFrame(base, s);
#ifdef _WIN32
  if (g_gate.ResimPending() && !dbz3::g_guest_eu) {
    const auto t0 = std::chrono::steady_clock::now();
    for (uint32_t r = 1; r <= 8; ++r) {
      g_gate.ScriptAt(g_gate.SaveN() + int32_t(r) - 1);   // el mando del tick que se resimula
      ResimTick(ctx, base, orig, g_gate.ResimMask());
      g_gate.ResimCheck(base, r);
    }
    g_gate.ScriptAt(g_gate.CurN());
    g_gate.ResimDone();
    static int logged = 0;
    if (logged++ < 3)
      REXLOG_INFO("dbz3 rb: resimulados 8 ticks, mascara {:04X}, {:.3f} ms", g_gate.ResimMask(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
#endif
  const auto rb_t0 = std::chrono::steady_clock::now();
  orig(ctx, base);
#ifdef _WIN32
  if (g_gate.active())
    g_gate.OrigMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rb_t0).count(), rb_fight);
#endif
  if (s.active() && s.armed()) s.RunTick({}, Rng(base));   // nadie leyo al usuario 0 este tick
}

namespace {
// SELECT del mando de ese usuario para el Tag Team (relevo); se oculta al juego si lo usa.
void TagPad(PPCContext& ctx, uint8_t* base, uint32_t user, uint32_t out) {
  const uint16_t b = REX_LOAD_U16(out + 4);
  if (dbz3::tag::OnPad(ctx, base, user, b, REX_LOAD_U8(out + 6))) REX_STORE_U16(out + 4, uint16_t(b & ~0x0020));
  if (const uint16_t pose = dbz3::tag::PoseButtons(user)) REX_STORE_U16(out + 4, uint16_t(REX_LOAD_U16(out + 4) | pose));
  if (int16_t ly = 0; dbz3::tag::ExitStick(user, &ly)) REX_STORE_U16(out + 10, uint16_t(ly));
}
}  // namespace

// Mando virtual de las cinematicas (src/cinematica.cpp, frente R3): mientras un usuario tiene botones (>= 0), lee
// estos de XInput, conectado y sin palancas, en vez de su mando (el juego los traduce como los de verdad: Y+B = carga
// de ki, como la pose del relevo del Tag Team). Cualquier hilo; < 0 lo devuelve a su mando.
namespace {
std::atomic<int32_t> g_pad_cine[4] = {-1, -1, -1, -1};
}
namespace dbz3::cinematica {
void Tick(PPCContext& ctx, uint8_t* base);   // src/cinematica.cpp: animaciones de la escena (con contexto)
}
namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones) {
  if (usuario < 4) g_pad_cine[usuario] = botones;
}
}  // namespace dbz3::net

DBZ3_HOOK(sub_8221C178, dbz3eu_sub_8221A940) {
  auto& s = dbz3::net::Session::Get();
  const uint32_t user = ctx.r3.u32;
  const uint32_t out = ctx.r4.u32;
  // Tag Team: el COM que entra hace la pose con un mando 4 virtual (conectado solo durante la pose)
  uint16_t fake = 0;
  int16_t fake_ly = 0;
  if (user == 3 && out && !s.active() && dbz3::tag::PoseVirtualPad(&fake, &fake_ly)) {
    static uint32_t packet = 0;
    REX_STORE_U32(out + 0, ++packet);
    REX_STORE_U16(out + 4, fake);
    REX_STORE_U16(out + 6, 0);
    REX_STORE_U16(out + 8, 0);
    REX_STORE_U16(out + 10, uint16_t(fake_ly));
    REX_STORE_U32(out + 12, 0);
    ctx.r3.u64 = 0;   // ERROR_SUCCESS: conectado
    return;
  }
#ifdef _WIN32
  if (user < 2 && out && !s.active()) {   // R3 (pruebas, DBZ3_RB_SCRIPT): mandos por guion durante el combate
    if (const int32_t sid = g_gate.ScriptId(); sid >= 0) {
      uint16_t b = 0;
      int16_t lx = 0, ly = 0;
      dbz3::net::rb::ScriptPad(user, uint32_t(sid), b, lx, ly);
      REX_STORE_U32(out + 0, uint32_t(sid) + 1);
      REX_STORE_U16(out + 4, b);
      REX_STORE_U16(out + 6, 0);
      REX_STORE_U16(out + 8, uint16_t(lx));
      REX_STORE_U16(out + 10, uint16_t(ly));
      REX_STORE_U32(out + 12, 0);
      ctx.r3.u64 = 0;
      return;
    }
  }
#endif
  if (const int32_t v = user < 4 ? g_pad_cine[user].load() : -1; v >= 0 && out && !s.active()) {
    static int32_t visto[4] = {-1, -1, -1, -1};
    if (visto[user] != v) REXLOG_INFO("dbz3 net: mando virtual {} (cinematica): botones {:04X}", user, uint32_t(v));
    visto[user] = v;
    dbz3::cinematica::Tick(ctx, base);
    static uint32_t packet = 0;
    REX_STORE_U32(out + 0, ++packet);
    REX_STORE_U16(out + 4, uint16_t(v));
    REX_STORE_U16(out + 6, 0);
    REX_STORE_U32(out + 8, 0);
    REX_STORE_U32(out + 12, 0);
    ctx.r3.u64 = 0;
    return;
  }
#ifdef _WIN32
  if (s.resimulating() && user <= 1 && out) {   // R4: resimulacion del rollback: el mando del tick que se repite (sin lockstep, sin efectos de menu)
    WritePad(base, out, s.Applied(user), s.resim_tick() + 1);
    REX_STORE_U16(out + 4, uint16_t(REX_LOAD_U16(out + 4) & ~0x0C00));
    ctx.r3.u64 = 0;
    return;
  }
#endif
  if (user == 1 && out && s.cut_fight() && dbz3::ingame::BattleNow()) {   // corte en combate: P2 neutro y conectado
    static uint32_t packet = 0;
    REX_STORE_U32(out + 0, ++packet);
    REX_STORE_U16(out + 4, 0);
    REX_STORE_U16(out + 6, 0);
    REX_STORE_U32(out + 8, 0);
    REX_STORE_U32(out + 12, 0);
    ctx.r3.u64 = 0;
    return;
  }
  if (!s.active() || user > 1 || !out) {
    orig(ctx, base);
    if (user == 1) dbz3::ingame::NotePad2(ctx.r3.u32 == 0);
    if (user <= 1 && out && ctx.r3.u32 == 0) {
      const uint64_t r3 = ctx.r3.u64;
      if (user == 0) {
        // Minijuegos de IW (frente P4): en la HD la palanca derecha lanza tecnicas (Kamehameha, Masenko; el teclado de
        // pruebas la lleva en las flechas): mientras dure el minijuego no le llega al juego y se la queda la camara
        // (iwmini::PalancaDerecha). La cruceta, igual: se quita y, con la palanca izquierda suelta, pasa a ella.
        if (dbz3::iwmini::Activo()) {
          dbz3::iwmini::PalancaDerecha(int16_t(REX_LOAD_U16(out + 12)), int16_t(REX_LOAD_U16(out + 14)));
          REX_STORE_U32(out + 12, 0);
        }
        if (const uint16_t b = REX_LOAD_U16(out + 4); (b & 0x000F) && dbz3::iwmini::Activo()) {
          if (REX_LOAD_U16(out + 8) == 0 && REX_LOAD_U16(out + 10) == 0) {
            REX_STORE_U16(out + 8, uint16_t(int16_t(b & 0x0008 ? 32767 : b & 0x0004 ? -32767 : 0)));
            REX_STORE_U16(out + 10, uint16_t(int16_t(b & 0x0001 ? 32767 : b & 0x0002 ? -32767 : 0)));
          }
          REX_STORE_U16(out + 4, uint16_t(b & ~0x000F));
        }
        dbz3::ingame::OnlineTick(base, REX_LOAD_U16(out + 4));   // X en los menus
        // la palanca izquierda arriba/abajo cuenta como la cruceta (el teclado usa la palanca)
        constexpr uint16_t kUpDown = 0x0003;
        const uint16_t real = REX_LOAD_U16(out + 4);
        const int16_t ly = int16_t(REX_LOAD_U16(out + 10));
        const uint16_t ext = real | (ly < -16000 ? 0x0002 : 0) | (ly > 16000 ? 0x0001 : 0);
        if (dbz3::ingame::pilot::Busy()) {   // TAG TEAM lleva al juego por sus menus
          const uint16_t piloted = dbz3::ingame::pilot::Filter(ext, 0);
          if (dbz3::ingame::pilot::Busy()) {
            REX_STORE_U16(out + 4, piloted);
            REX_STORE_U32(out + 8, 0);    // sin palancas mientras pilota
            REX_STORE_U32(out + 12, 0);
          }
        } else {                            // nuestras filas en los menus del juego
          const uint16_t res = dbz3::ingame::TeamPickFilter(dbz3::ingame::DuelMenuFilter(base, ext), 0);
          if (dbz3::ingame::MenuRowsTracked()) {
            // menu principal y Duelo: la palanca llega al juego ya convertida en cruceta con el mismo
            // umbral que cuenta las filas (un toque suave movia el cursor del juego y no la cuenta:
            // la fila ONLINE salia entre COM VS 2P y COM VS COM)
            REX_STORE_U16(out + 4, res);
            REX_STORE_U32(out + 8, 0);
            REX_STORE_U32(out + 12, 0);
          } else if (res != ext) {
            REX_STORE_U16(out + 4, uint16_t(res & ~(kUpDown & ~real)));
            if ((ext & ~res) & kUpDown) REX_STORE_U16(out + 10, 0);
          }
        }
      } else {                              // jugador 2: piloto y tira del Tag Team (1P VS 2P)
        const uint16_t real = REX_LOAD_U16(out + 4);
        uint16_t res = dbz3::ingame::pilot::User() == 1 ? dbz3::ingame::pilot::Filter(real, 1) : real;
        res = dbz3::ingame::TeamPickFilter(res, 1);
        if (res != real) REX_STORE_U16(out + 4, res);
      }
      dbz3::ingame::FormPickPad(int(user), REX_LOAD_U8(out + 6), REX_LOAD_U8(out + 7));
      TagPad(ctx, base, user, out);
      ctx.r3.u64 = r3;
    }
    return;
  }
  if (user == 0 && s.armed()) {
    orig(ctx, base);                                     // mando local real -> out
    dbz3::net::Pad local = ctx.r3.u32 == 0 ? ReadPad(base, out) : dbz3::net::Pad{};
#ifdef _WIN32
    static const bool script = std::getenv("DBZ3_RB_SCRIPT") != nullptr;
    if (script && g_rbs.in_fight) {   // PRUEBAS: mando por guion segun el tick del combate (igual en todas las corridas)
      uint16_t b = 0;
      int16_t lx = 0, ly = 0;
      dbz3::net::rb::ScriptPad(s.role() == dbz3::net::Role::kHost ? 0 : 1, s.tick() - g_rbs.t0, b, lx, ly);
      local = {};
      local.buttons = b;
      local.lx = lx;
      local.ly = ly;
    }
#endif
    local.buttons |= AutoPick(base) | dbz3::tag::NetReadyBits();   // relevo cargado (Tag Team)
    s.RunTick(local, Rng(base));                         // puede esperar al rival
  }
  if (!s.active()) {                                     // se perdio la conexion en este tick
    ctx.r3.u64 = user;
    ctx.r4.u64 = out;
    orig(ctx, base);
    return;
  }
  WritePad(base, out, s.Applied(user), s.tick());
  dbz3::tag::NetApplied(user, s.Applied(user).buttons);   // el juego no ve los bits del relevo
  REX_STORE_U16(out + 4, uint16_t(REX_LOAD_U16(out + 4) & ~0x0C00));
  dbz3::ingame::FormPickPad(int(user), REX_LOAD_U8(out + 6), REX_LOAD_U8(out + 7));   // misma forma inicial
  TagPad(ctx, base, user, out);                          // mismo mando en los dos PCs: mismo relevo
  ctx.r3.u64 = 0;                                        // X_ERROR_SUCCESS: los dos conectados
}
