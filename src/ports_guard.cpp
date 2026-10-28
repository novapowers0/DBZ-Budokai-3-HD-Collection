// dbz3 - Frente Ports: guarda de la ranura de tecnicas (BSP) con mas de 64 recursos (2026-10-15).
//
// sub_82126490 (procesar la ranura de tecnicas j del lado k; ranura = *0x82333994 + 1152k + 544j + 80) apunta
// cada recurso del BSP (hijos de [0], hijos de [1] y hijos de la raiz desde el 2, en ese orden) en
// ranura+32+4i y, si el recurso es un #AWO, crea su instancia en ranura+288+4i. Hay sitio para 64 (los BSP del
// juego llegan a 52). El de Vegeta GT de Infinite World tiene 65: el recurso 64 (#CWV) cae en ranura+288, la
// instancia 0. Durante el combate no pasa nada (sub_820CF580 busca en ranura+32+4*indice y encuentra el #CWV),
// pero al acabar sub_821266A8 libera las 64 "instancias" con sub_82089C10 y libera ese bloque del fichero:
// lectura de 0x10 y el juego se cierra (D:\DBZ3HD\_pruebas\20261015-015446_modos_test_vegeta_gt_sin_modos,
// lr 0x82089C34, r31 = el #CWV en +0xA1700 del BSP).
// Arreglo: antes de liberar, las entradas desbordadas (punteros a datos del BSP, no instancias) se ponen a 0.
// US y EU (la base y las funciones EU, de src/eu_map_gen.h). roster_build.py avisa de los BSP de mas de 64.
//
// Traje de epoca enlazado (ficha 07, 2026-10-15): un traje extra de traje.toml con `enlazado = "<mod>"` lleva en
// mods/_roster/roster.toml `traje_enlazado = [traje, ID]`. Justo antes de montar el combate (sub_820FE668), el
// jugador que lleva ese traje pasa a ser el personaje ID (traje 0), con su moveset, tecnicas, aura y sus capsulas por
// defecto (precedencia: el set del traje manda sobre el del jugador para el personaje base). En el select, mientras
// se mira ese traje, un rotulo lo avisa (nombre de epoca + "golpes y capsulas propios").
#include <imgui.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <toml++/toml.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <utility>
#include <vector>
#include <bit>        // S1-F07: std::bit_cast (guarda de sub_821117B8)
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "form_selection_state.h"
#include "mods.h"
#include "ports_guard.h"
#include "roster_ext.h"
#include "modos.h"

bool dbz3_select_live();               // select_ext.cpp
int64_t dbz3_select_browse_ms(int k);
int64_t dbz3_select_now_ms();
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3

namespace {
constexpr dbz3::GA kSlotsBase = 0x82333994;
constexpr uint32_t kCap = 64;
// S5-I1 (pruebas): DBZ3_TEST_SIN_GUARDAS=1 apaga las guardas de este fichero (camara, evento 9, BSP > 64) para
// comprobar que el fallo del jugador sale sin ellas (lr del log) y desaparece con ellas. Sin la variable no cambia nada.
bool SinGuardas() {
  static const bool v = std::getenv("DBZ3_TEST_SIN_GUARDAS") != nullptr;
  return v;
}
}  // namespace

// Logica de los luchadores (sub_8210E700, cada fotograma): antes de dejar que el juego recorra la lista
// (cfg+35096 cuenta / cfg+35100 punteros) y llame por la tabla de acciones 0x8201F090 (52 entradas), se
// comprueba que la lista y la accion (+3244) son validas. Con la lista a medias (cuenta 3..8, puntero 0 o
// un valor que no es un objeto) el juego llama a un puntero basura y cae con 0xC0000005 (medido 23/10
// 08:18, lr 0x8210E798, lectura de 0x701912EC con Goku contra Broly ID 40 en el escenario 2). Tambien se
// repara la accion fuera de la tabla y la llamada fija. DBZ3_TEST_LUCHADOR_MALO=1/2 fuerza el fallo (A/B).
namespace dbz3::ports {
bool GuardLuchadores(uint8_t* base) {
  constexpr dbz3::GA kCfg = 0x823D6CF4;   // *(0x823D6CF4) = configuracion del combate
  constexpr uint32_t kCuenta = 35096, kLista = 35100, kAccion = 3244;
  constexpr int kAcciones = 52;
  constexpr uint32_t kTablaUS = 0x8201F090, kTablaEU = 0x8201F088;
  constexpr uint32_t kCodeLo = 0x82080000, kCodeHi = 0x82310000;
  const auto objeto = [](uint32_t o) {
    return (o >= 0x20000000u && o < 0x50000000u) || (o >= 0x82000000u && o < 0x83000000u);
  };
  const uint32_t tabla = dbz3::g_guest_eu ? kTablaEU : kTablaUS;
  const uint32_t esperado = dbz3::g_guest_eu ? 0x82306B68u : 0x82308398u;   // llamada fija de sub_8210E700
  const uint32_t cfg = REX_LOAD_U32(kCfg);
  if (!cfg) return true;
  // Pruebas A/B: 1 rompe la accion del primer luchador, 2 la cuenta de la lista. Sin guarda, el juego cae por
  // el puntero basura (la tabla esta en .rdata: no se puede escribir, por eso no hay un modo 3).
  if (const char* m = std::getenv("DBZ3_TEST_LUCHADOR_MALO"); m && *m) {
    if (const uint32_t o0 = REX_LOAD_U32(cfg + kLista); o0 && *m == '1') REX_STORE_U16(o0 + kAccion, 60);
    if (*m == '2') REX_STORE_U32(cfg + kCuenta, 8);
  }
  if (SinGuardas()) return true;
  const int32_t n = int32_t(REX_LOAD_U32(cfg + kCuenta));
  if (n < 0 || n > 8) {
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 8)
      REXLOG_WARN("dbz3 ports: lista de luchadores con cuenta {} (0..8): se salta el fotograma", n);
    return false;
  }
  static uint32_t ultimo[8] = {};
  static bool hay[8] = {};
  for (int32_t i = 0; i < n; ++i) {
    const uint32_t celda = cfg + kLista + 4 * uint32_t(i);
    uint32_t obj = REX_LOAD_U32(celda);
    if (objeto(obj) && !(obj & 3)) {
      ultimo[i] = obj;
      hay[i] = true;
    }
    if (!objeto(obj) || (obj & 3)) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: luchador {} con puntero no valido {:08X}: {}", i, obj,
                    hay[i] ? "se usa el ultimo bueno" : "se salta el fotograma");
      if (!hay[i]) return false;
      obj = ultimo[i];
      REX_STORE_U32(celda, obj);
    }
    int16_t acc = int16_t(REX_LOAD_U16(obj + kAccion));
    if (acc < 0 || acc >= kAcciones) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: luchador {} con accion {} fuera de la tabla (0..{}): pasa a 15", i, acc,
                    kAcciones - 1);
      acc = 15;
      REX_STORE_U16(obj + kAccion, uint16_t(acc));
    }
    const uint32_t manejador = REX_LOAD_U32(tabla + 4 * uint32_t(acc));
    if (manejador < kCodeLo || manejador >= kCodeHi) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: accion {} con manejador no valido {:08X}: pasa a 15", acc, manejador);
      REX_STORE_U16(obj + kAccion, 15);
      const uint32_t h = REX_LOAD_U32(tabla + 4 * 15);
      if (h < kCodeLo || h >= kCodeHi) return false;
    }
  }
  const uint32_t segundo = REX_LOAD_U32(tabla + 180);   // la llamada fija de sub_8210E700
  // La tabla esta en .rdata (solo lectura), asi que su valor no puede cambiar en marcha: si no es el esperado
  // (o no es un puntero de codigo) se salta el fotograma en vez de llamar a un destino basura (ctr 0xFFFFFFFF
  // del cierre de 08:18). En EU no se compara con el esperado (la tabla EU no esta volcada), solo el rango.
  if ((!dbz3::g_guest_eu && segundo != esperado) || segundo < kCodeLo || segundo >= kCodeHi) {
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 8)
      REXLOG_WARN("dbz3 ports: segunda llamada del fotograma {:08X} (se esperaba {:08X}): se salta", segundo, esperado);
    return false;
  }
  return true;
}
}  // namespace dbz3::ports

DBZ3_HOOK_EU(sub_821266A8) {
  if (!SinGuardas()) {
    const uint32_t base0 = REX_LOAD_U32(kSlotsBase);
    const uint32_t k = ctx.r3.u32, j = ctx.r4.u32;
    if (base0 && k < 2 && j < 2) {
      const uint32_t slot = base0 + 1152 * k + 544 * j + 80;
      const uint32_t bin = REX_LOAD_U32(slot + 4);
      if (bin) {
        const uint32_t n01 = REX_LOAD_U32(slot + 16);   // recursos de [0] y [1]
        const uint32_t ntop = REX_LOAD_U32(bin + 16);  // hijos de la raiz
        const uint32_t total = n01 + (ntop > 2 ? ntop - 2 : 0);
        if (total > kCap && total < 2 * kCap) {
          for (uint32_t g = kCap; g < total; ++g) REX_STORE_U32(slot + 32 + 4 * g, 0);
          REXLOG_WARN("dbz3 ports: tecnicas del lado {} con {} recursos (el juego admite {}): {} sin liberar "
                      "(eran datos del fichero, no instancias)", k, total, kCap, total - kCap);
        }
      }
    }
  }
  orig(ctx, base);
}

// Efecto de tecnica sin funcion (s9, 2026-10-19): sub_82130DA0(luchador r3, ...) busca con sub_820CD6C0(*(r3+4944) como
// s16, 1) el registro del efecto y llama por la tabla 0x823339A0[tipo] (tipo = s16 en *(registro+32+12)+16) sin mirar si
// la entrada existe. Con Paikuhan (port de IW, capsulas 616-618) el tipo no tiene funcion: llamada a 0 y cierre
// (lr 0x82130F24, medido: tipo = s16 en r4+16; D:\DBZ3HD\_pruebas61019-*_s56_test_iw_dm_087, mision 87 de Dragon Mission). Si la entrada es 0
// se salta el efecto entero (sin el, la tecnica sigue; solo falta su chispa) en vez de cerrar el juego.
DBZ3_HOOK_EU(sub_82130DA0) {
  // el descriptor del efecto es el 2.o argumento (r4; la funcion lo guarda en registro+32+12 y llama por su tipo, +16)
  if (const uint32_t desc = ctx.r4.u32) {
    constexpr dbz3::GA kTablaEfectos = 0x823339A0;
    const int16_t tipo = int16_t(REX_LOAD_U16(desc + 16));
    if (tipo < 0 || tipo > 4096 || !REX_LOAD_U32(uint32_t(kTablaEfectos) + 4 * uint32_t(tipo))) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: efecto de tecnica de tipo {} sin funcion (descriptor {:08X}): se salta", tipo, desc);
      ctx.r3.u64 = 0;   // como cuando sub_820CD6C0 no encuentra el registro (salto directo al final con r3 = 0)
      return;
    }
  }
  orig(ctx, base);
}

// Cadenas de fisica del modelo (feedback 1.4.6, Tag Team Janemba + Super Bu, logs/dbz3_1034.log): sub_82134640
// (cargar modelo: r27 ID, r23 indice de forma, r25 traje, r24 jugador) lee el registro de 12 B
// char96[ID].lista[byte(+18+8*indice) + por_traje*traje] y pasa su +4 a esta funcion, que lee alli 3 cuentas y
// sus punteros. Un registro fuera de la lista (traje o indice que el ID no tiene) trae basura (0x54): cierre en
// lr 0x82138910. Un +4 que no es puntero se trata como "sin cadenas" (0, lo mismo que los ports sin `fisica`).
DBZ3_HOOK_EU(sub_821388D0) {
  if (const uint32_t p = ctx.r3.u32; p && p < 0x1000) {
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 8)
      REXLOG_WARN("dbz3 ports: cadenas de fisica no validas ({:X}) en el modelo de id {} forma {} traje {} jugador {} "
                  "(registro {:08X}): se carga sin ellas", p, ctx.r27.s32, ctx.r23.s32, ctx.r25.s32, ctx.r24.s32,
                  ctx.r28.u32);
    ctx.r3.u64 = 0;
    return;
  }
  orig(ctx, base);
}

// Clip de camara que el fichero no tiene (R9, 2026-10-16): sub_82082468(destino r3, #ACC r4, clip r5, frame f1) lee
// la tabla de clips (+0x14; 16 B: +8 frames, +12 claves) sin mirar la cuenta (+0x10). Con la camara de un port que
// tiene menos clips que la del donante (roster montado antes de cam_pad_clips de roster_build.py) lee basura: cierre
// al acabar el vuelo de la presentacion (lr 0x820825A8) o en la pose de victoria (Pan de IW, 21 clips, pide el 23:
// frames 0x40D4A316 y claves en 0x1, lr 0x82082470; D:\DBZ3HD\_pruebas\20261016-024812_modos_test_iw_mapa).
// Se usa el ultimo clip que hay; sin clips no se mueve la camara. El montaje actual ya rellena los clips del donante.
DBZ3_HOOK_EU(sub_82082468) {
  // S5-I1 (pruebas): DBZ3_TEST_CAM_CLIP=1 pide un clip que el fichero no tiene (como el 30 de Baby en #36)
  static const bool test_clip = std::getenv("DBZ3_TEST_CAM_CLIP") != nullptr;
  if (test_clip && ctx.r4.u32) ctx.r5.u64 = REX_LOAD_U32(ctx.r4.u32 + 16) + 100000;
  if (SinGuardas()) return orig(ctx, base);
  if (const uint32_t acc = ctx.r4.u32) {
    const uint32_t n = REX_LOAD_U32(acc + 16);
    if (ctx.r5.u32 >= n) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: clip de camara {} y la camara ({:08X}) tiene {}: {}", ctx.r5.u32, acc, n,
                    n ? "se usa el ultimo" : "no se mueve");
      if (!n) return;
      ctx.r5.u64 = n - 1;
    }
  }
  orig(ctx, base);
}

// Ordenes de guion (#SPX) de Infinite World que B3 no tiene (R9, 2026-10-16). El interprete (sub_8209F568) busca la
// orden N en filas[N >> 4][N & 15] (tabla 0x82332670) sin mirar el largo de la fila. La fila 8 de B3 tiene 4 ordenes
// (0x80-0x83) y la de IW 12 (tabla del ELF USA 0x457AC0): las 0x86 y 0x88 de IW (0x23AF00: nivel de las capsulas
// 0x49-0x4D del lado p[0]; 0x23AD00: dato del registro de su capsula actual, 1 si no hay) caen en B3 en las 0x92/0x94,
// sub_8211E520, que hace luchador = mgr[8797 + p[1]] y cambia su estado: con un solo argumento p[1] es basura y el
// juego se cerraba (Goku GT de IW, definitiva 359: p[1] = 0x4CE81EF, lr 0x8209F668). Aqui, con clase/fila de entrada
// (r10/4, r9/4 del interprete): las de IW se saltan sin tocar nada (su resultado se descarta en el guion: `12 10 04`;
// escribir en cmd+0, donde IW deja el resultado, colgaba la definitiva: en B3 cmd+0 es del interprete), y cualquier
// otra llamada con p[1] que no es un lado tambien.
DBZ3_HOOK_EU(sub_8211E520) {
  const uint32_t cmd = ctx.r4.u32, p = cmd ? REX_LOAD_U32(cmd + 44) : 0;
  const uint32_t clase = ctx.r10.u32 / 4, fila = ctx.r9.u32 / 4;
  const bool iw = clase == 8 && (fila == 6 || fila == 8);
  if (cmd && (iw || (p && REX_LOAD_U32(p + 4) > 1))) {
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 8)
      REXLOG_WARN("dbz3 ports: orden de guion {:X}{} (lado {:X}, pc {:08X}): {}", clase * 16 + fila,
                  iw ? " de Infinite World" : "", p ? REX_LOAD_U32(p + (iw ? 0 : 4)) : 0u, REX_LOAD_U32(cmd + 36),
                  "se salta");
    ctx.r3.u64 = 1;
    return;
  }
  orig(ctx, base);
}

// Las otras ordenes de IW que usan sus ports (Goku GT: 0x35-0x37 aura/malla y 0x99-0x9A rafagas de ki, filas 3 y 9
// mas largas en IW) caen en B3 en las 0x60-0x62 y 0xA0-0xA1 y cambiaban otra cosa: se saltan.
namespace {
// true (y avisa) si la orden que llega (clase r10/4, fila r9/4) es la de IW que cae en este manejador de B3
bool SaltaOrdenIW(PPCContext& ctx, uint8_t* base, uint32_t c, uint32_t f) {
  if (!ctx.r4.u32 || ctx.r10.u32 / 4 != c || ctx.r9.u32 / 4 != f) return false;
  static std::atomic<int> avisos{0};
  if (avisos.fetch_add(1) < 8)
    REXLOG_WARN("dbz3 ports: orden de guion {:X} de Infinite World (pc {:08X}): se salta", c * 16 + f,
                REX_LOAD_U32(ctx.r4.u32 + 36));
  ctx.r3.u64 = 1;
  return true;
}
}  // namespace
DBZ3_HOOK_EU(sub_8211E148) { if (!SaltaOrdenIW(ctx, base, 3, 5)) orig(ctx, base); }
DBZ3_HOOK_EU(sub_8211E178) { if (!SaltaOrdenIW(ctx, base, 3, 6)) orig(ctx, base); }
DBZ3_HOOK_EU(sub_8211E1B0) { if (!SaltaOrdenIW(ctx, base, 3, 7)) orig(ctx, base); }
DBZ3_HOOK_EU(sub_8211E6A0) { if (!SaltaOrdenIW(ctx, base, 9, 9)) orig(ctx, base); }
DBZ3_HOOK_EU(sub_8211E6F8) { if (!SaltaOrdenIW(ctx, base, 9, 10)) orig(ctx, base); }
DBZ3_HOOK_EU(sub_8211E578) { if (!SaltaOrdenIW(ctx, base, 8, 7)) orig(ctx, base); }   // Vegeta saga Saiyan de IW
DBZ3_HOOK_EU(sub_8211E2B8) { if (!SaltaOrdenIW(ctx, base, 3, 11)) orig(ctx, base); }

// Nodo de efecto reutilizado en uso (R9, 2026-10-16): sub_82155990 coge el siguiente de un anillo de 128 nodos
// (punteros en 0x824370B0, indice en 0x823EE81C) sin mirar si sigue enlazado en la lista ordenada (centinela
// 0x823EE820; nodo: +0 siguiente, +4 anterior, +12 prioridad), lo pone a cero y lo vuelve a insertar: si seguia
// en la lista, sus vecinos apuntan a un nodo con siguiente = 0 y el recorrido lee 0xC (Goku GT de IW con muchas
// definitivas: lr 0x82155A04, D:\DBZ3HD\_pruebas\20261016-051458_ports_test_combate_port). Antes de reutilizarlo
// se saca de la lista; el efecto que lo tenia pierde ese nodo, como antes, pero la lista queda bien.
DBZ3_HOOK_EU(sub_82155990) {
  constexpr dbz3::GA kIdx = 0x823EE81C, kPool = 0x824370B0;
  const uint32_t i = REX_LOAD_U32(kIdx);
  if (const uint32_t node = i < 128 ? REX_LOAD_U32(kPool + 4 * i) : 0) {
    const uint32_t nx = REX_LOAD_U32(node), pv = REX_LOAD_U32(node + 4);
    if (nx && pv && nx != node && REX_LOAD_U32(pv) == node && REX_LOAD_U32(nx + 4) == node) {
      REX_STORE_U32(pv, nx);
      REX_STORE_U32(nx + 4, pv);
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 4)
        REXLOG_WARN("dbz3 ports: nodo de efecto {} reutilizado aun en la lista ({:08X}): se saca antes", i, node);
    }
  }
  orig(ctx, base);
}

// S1-F07 (F07.1, cierre lr 0x8210C974; issue #33, dbz3_061.log de 1.4.5.2 con 16 ports de capsulas propias):
// sub_8210C700 (tick del luchador X) llama a sub_821117B8(X) cuando X+3256 - X+6220 pasa de un tope. Si el temporizador
// X+6220 supera la constante 0x8201F60C (EU 0x8201F604), sub_821117B8 busca en la lista del luchador (P = *(X+2932):
// cuenta P+40, tabla *(P+44) de 8 B {u16 tipo, s16 indice, u32 tabla}) la de tipo 1 y lee el u16 de
// tabla + 16 * indice - 16 para el evento 9 de sub_820D85A8 (despues sub_82122488(1, 33)); luego X+6224, X+8193 = 1 y
// X+6220 = constante 0x820007CC. Un port sin esa entrada trae tabla 0: lectura de 0x0 y cierre (r10 = r11 = 0x10 en el
// log, justo tras lhz r11,-16(r11)). Con la lista mala se deja X+6220 igual al tope: con t == tope la funcion salta el
// evento (ble) y hace el resto, como siempre. DBZ3_TEST_F07_LISTA=1 lo fuerza en cualquier luchador (prueba).
namespace {
bool ListaEvento9Valida(uint8_t* base, uint32_t x) {
  static const bool forzar = std::getenv("DBZ3_TEST_F07_LISTA") != nullptr;
  if (forzar) return false;
  const auto ok = [](uint32_t a) { return a >= 0x00100000u && a < 0xF0000000u; };
  const uint32_t p = REX_LOAD_U32(x + 2932);
  if (!ok(p)) return false;
  const int32_t n = int32_t(REX_LOAD_U32(p + 40));
  uint32_t e = REX_LOAD_U32(p + 44);
  if (!ok(e)) return false;
  for (int32_t i = 0; i < n && i < 4096; ++i)   // como el juego: avanza mientras no sea de tipo 1
    if (REX_LOAD_U16(e) != 1) e += 8;
  if (!ok(e)) return false;
  const uint32_t tabla = REX_LOAD_U32(e + 4);
  const uint32_t a = tabla + uint32_t(int32_t(int16_t(REX_LOAD_U16(e + 2)))) * 16u - 16u;
  return ok(tabla) && ok(a);
}
}  // namespace
DBZ3_HOOK_EU(sub_821117B8) {
  if (const uint32_t x = ctx.r3.u32) {
    constexpr dbz3::GA kTope = 0x8201F60C;
    const float t = std::bit_cast<float>(REX_LOAD_U32(x + 6220));
    const uint32_t tope_bits = REX_LOAD_U32(kTope);
    if (SinGuardas()) {   // S5-I1 (pruebas): con DBZ3_TEST_F07_LISTA deja de verdad la entrada sin tabla (el port del log)
      if (std::getenv("DBZ3_TEST_F07_LISTA") && t > std::bit_cast<float>(tope_bits)) {
        const uint32_t p = REX_LOAD_U32(x + 2932);
        const int32_t n = p ? int32_t(REX_LOAD_U32(p + 40)) : 0;
        uint32_t e = p ? REX_LOAD_U32(p + 44) : 0;
        for (int32_t i = 0; e && i < n && i < 4096; ++i)
          if (REX_LOAD_U16(e) != 1) e += 8;
        if (e) REX_STORE_U32(e + 4, 0);
      }
      return orig(ctx, base);
    }
    if (t > std::bit_cast<float>(tope_bits) && !ListaEvento9Valida(base, x)) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_WARN("dbz3 ports: luchador {:08X} (id {}) sin la entrada de tipo 1 en su lista {:08X}: se salta su "
                    "evento 9 (antes, cierre en lr 0x8210C974)", x, REX_LOAD_U32(x + 7420), REX_LOAD_U32(x + 2932));
      REX_STORE_U32(x + 6220, tope_bits);
    }
  }
  orig(ctx, base);
}

namespace {
constexpr dbz3::GA kCfg = 0x8245EFC0;

struct Enlace { int id = -1; std::string nombre; };
// (ID, traje) -> personaje enlazado; se lee una vez (el roster no cambia con el juego abierto)
const std::map<std::pair<int, int>, Enlace>& Enlaces() {
  static const std::map<std::pair<int, int>, Enlace> m = [] {
    std::map<std::pair<int, int>, Enlace> r;
    std::ifstream f(dbz3::ModsRoot() / "_roster" / "roster.toml", std::ios::binary);
    if (!f) return r;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
      const toml::table t = toml::parse(ss.str());
      std::map<int, std::string> nombres;
      const toml::array* a = t["personaje"].as_array();
      if (!a) return r;
      for (const toml::node& n : *a)
        if (const toml::table* x = n.as_table())
          if (auto nom = (*x)["nombre"].value<std::string>()) nombres[int((*x)["id"].value_or(int64_t(-1)))] = *nom;
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        const toml::array* e = x ? (*x)["traje_enlazado"].as_array() : nullptr;
        if (!e || e->size() != 2) continue;
        const int id = int((*x)["id"].value_or(int64_t(-1)));
        const int traje = int((*e)[0].value_or(int64_t(-1))), dest = int((*e)[1].value_or(int64_t(-1)));
        if (id < 0 || traje < 0 || dest < 0 || dest >= 64) continue;
        r[{id, traje}] = {dest, nombres.count(dest) ? nombres[dest] : std::string()};
        REXLOG_INFO("dbz3 ports: traje {} del personaje {} enlazado al personaje {} ({})", traje + 1, id, dest,
                    r[{id, traje}].nombre);
      }
    } catch (const toml::parse_error&) {
    }
    return r;
  }();
  return m;
}

namespace S = dbz3::ingame::style;
class TrajePanel : public rex::ui::ImGuiDialog {
 public:
  explicit TrajePanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (Enlaces().empty() || !dbz3_select_live() || dbz3::GameDimmed() || dbz3::OverlayOpen()) return;
    const S::Area a = S::GameArea(io);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (int side = 0; side < 2; ++side) {
      if (dbz3_select_now_ms() - dbz3_select_browse_ms(side) > 300) continue;
      const auto snap = dbz3::forms::Copy(side).snapshot;
      const auto it = Enlaces().find({snap.id, snap.suit});
      if (it == Enlaces().end()) continue;
      const float cx = side == 0 ? 210.0f : 990.0f, y = 400.0f;   // bajo la ayuda LT/RT de la forma (292-330)
      // DS138: tope de ancho (380 u) para que el nombre y la linea de traje de época no salgan del area
      // (en de/it la traduccion pasa de 45 caracteres y el texto centrado en 210 se salia por la izquierda).
      if (!it->second.nombre.empty())
        S::MenuText(dl, a.At(cx, y), 26.0f * a.s, it->second.nombre.c_str(), true, false, 380.0f * a.s);
      S::MenuText(dl, a.At(cx, y + 28.0f), 18.0f * a.s,
                  dbz3::i18n::T("Traje de época: golpes y cápsulas propios", "Era costume: own moves and capsules"),
                  false, false, 380.0f * a.s);
    }
  }
};

std::atomic<bool> g_panel{false};
void CrearPanel() {   // el panel vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!g_panel && rt && rt->imgui_drawer()) g_panel = true, new TrajePanel(rt->imgui_drawer());
}
}  // namespace

// Comprobacion que hace la espera de la escena 24 (sub_821D7400) justo antes de pasar la CFG a sub_820FE668: si
// devuelve 0 con la escena 25 pedida, el combate se monta a continuacion. Es el ultimo punto tras los trajes que
// pone modos.cpp (su gancho de sub_821D7400 escribe la CFG antes de esta llamada). Tras montarlo se devuelve la CFG
// como estaba (la casilla y el traje del select, el ranking de los retos); la revancha vuelve a pasar por aqui.
DBZ3_HOOK_EU(sub_821D7FE8) {
  static uint32_t llamadas = 0;
  if (!g_panel && llamadas++ % 600 == 0) dbz3::launcher::DeferUi(&CrearPanel);   // hasta que haya dibujante
  struct Hecho { int id = -1, traje = 0, dest = -1; uint8_t caps[16] = {}; };   // caps: rec+52..+67
  static Hecho hecho[2];
  const uint32_t lr = uint32_t(ctx.lr);
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t rec = kCfg + 16 + 80 * k;
    if (hecho[k].dest >= 0 && REX_LOAD_U16(rec + 16) == uint32_t(hecho[k].dest) && REX_LOAD_U16(rec + 18) == 0) {
      REX_STORE_U16(rec + 16, uint16_t(hecho[k].id));
      REX_STORE_U16(rec + 18, uint16_t(hecho[k].traje));
      for (uint32_t b = 0; b < 16; ++b) REX_STORE_U8(rec + 52 + b, hecho[k].caps[b]);
    }
    hecho[k].dest = -1;
  }
  orig(ctx, base);
  if (lr == dbz3::GA(0x821D7428) && ctx.r3.u32 == 0 && REX_LOAD_U32(dbz3::GA(0x8247A990)) == 25 &&
      !Enlaces().empty()) {
    for (uint32_t k = 0; k < 2; ++k) {
      const uint32_t rec = kCfg + 16 + 80 * k;
      const int id = int(REX_LOAD_U16(rec + 16)), traje = int(REX_LOAD_U16(rec + 18));
      const auto it = Enlaces().find({id, traje});
      if (it == Enlaces().end()) continue;
      hecho[k] = {id, traje, it->second.id};
      for (uint32_t b = 0; b < 16; ++b) hecho[k].caps[b] = REX_LOAD_U8(rec + 52 + b);
      REX_STORE_U16(rec + 16, uint16_t(it->second.id));
      REX_STORE_U16(rec + 18, 0);
      // capsulas del traje = las del personaje enlazado (char96 +80 cuantas, +82 IDs; como roster_ext al
      // cambiar de ID en sub_821007E8): el set del jugador para Vegeta no vale para otro moveset
      const uint32_t c96 = dbz3::GA(0x8234ABB8) + uint32_t(it->second.id) * 96;
      for (uint32_t b = 0; b < 16; ++b) REX_STORE_U8(rec + 52 + b, REX_LOAD_U8(c96 + 80 + b));
      REXLOG_INFO("dbz3 ports: jugador {} con el traje {} del personaje {}: combate con el personaje {} ({})", k + 1,
                  traje + 1, id, it->second.id, it->second.nombre);
    }
  }
}

// El Duelo del juego no pasa por la espera de la escena 24: sub_820FFA80 (llamada desde 0x821D634C) arma la CFG del
// combate en la pila con el ultimo luchador del Duelo (partida G = 0x824BA110: +31+k ID, +33+k traje, u8) y las
// capsulas de 0x8245EF92 + 16k (u16 cuantas, 7 u16), y llama a sub_820FE668. Se cambia la fuente mientras dura y
// se devuelve despues (el select vuelve con su casilla y su traje; la revancha vuelve a pasar por aqui).
DBZ3_HOOK_EU(sub_820FFA80) {
  struct Hecho { int id = -1, traje = 0, dest = -1; uint8_t caps[16] = {}; };
  Hecho hecho[2];
  constexpr dbz3::GA kG = 0x824BA110, kCaps = 0x8245EF92;
  if (!Enlaces().empty()) {
    for (uint32_t k = 0; k < 2; ++k) {
      const int id = int(REX_LOAD_U8(kG + 31 + k)), traje = int(REX_LOAD_U8(kG + 33 + k));
      const auto it = Enlaces().find({id, traje});
      if (it == Enlaces().end()) continue;
      hecho[k] = {id, traje, it->second.id};
      const uint32_t caps = kCaps + 16 * k, c96 = dbz3::GA(0x8234ABB8) + uint32_t(it->second.id) * 96;
      for (uint32_t b = 0; b < 16; ++b) hecho[k].caps[b] = REX_LOAD_U8(caps + b), REX_STORE_U8(caps + b, REX_LOAD_U8(c96 + 80 + b));
      REX_STORE_U8(kG + 31 + k, uint8_t(it->second.id));
      REX_STORE_U8(kG + 33 + k, 0);
      REXLOG_INFO("dbz3 ports: jugador {} con el traje {} del personaje {}: combate con el personaje {} ({}) (Duelo)",
                  k + 1, traje + 1, id, it->second.id, it->second.nombre);
    }
  }
  orig(ctx, base);
  for (uint32_t k = 0; k < 2; ++k) {
    if (hecho[k].dest < 0 || REX_LOAD_U8(kG + 31 + k) != uint32_t(hecho[k].dest)) continue;
    REX_STORE_U8(kG + 31 + k, uint8_t(hecho[k].id));
    REX_STORE_U8(kG + 33 + k, uint8_t(hecho[k].traje));
    for (uint32_t b = 0; b < 16; ++b) REX_STORE_U8(kCaps + 16 * k + b, hecho[k].caps[b]);
  }
}

// S5-I1 (issue #33, Torneo): rotulo de nombre del VS e iconos del cuadro de un personaje con celda nueva.
// El juego dibuja esos sprites en cada cuadro con sub_820B04C8(descriptor r3, escala f1) tomando el descriptor de la
// tabla de sprites de la pagina: icono = tabla[casilla + 65] (sub_821A36D8, entrada de 20 B en r3: +8 casilla, +10
// mando), nombre del VS = tabla[casilla + 109] (sub_821A3CB0, lado en r27). Una celda nueva comparte la casilla de su
// anfitrion (Vegeta para Baby), asi que salia la cara y el nombre del anfitrion. Aqui, para el lado humano cuyo
// PickedCellFor existe, se clona el descriptor con la textura del icono / rotulo propios (los de la rueda: select_ext /
// roster_build, el mismo descriptor [tex, 3, 1.0, 0, rect] en +0xB0) y se pasa ese. Sprites del cuadro: en EU -0x1220.
namespace {
thread_local uint32_t t_entrada = 0;   // entrada del cuadro que dibuja sub_821A36D8

// celda nueva (indice) del humano de la entrada, o -1
int CeldaDeEntrada(uint8_t* base, uint32_t entry, int* casilla) {
  if (!entry) return -1;
  const int cas = int16_t(REX_LOAD_U16(entry + 8)), mando = int16_t(REX_LOAD_U16(entry + 10));
  if (cas < 0 || cas > 37) return -1;
  if (mando < 0) {   // COM con personaje de mod (torneo.toml): modos.cpp guarda el ID que juega esa plaza
    const uint32_t obj = REX_LOAD_U32(dbz3::GA(0x824681D0));
    if (!obj || entry < obj + 748 || entry >= obj + 748 + 20 * 32) return -1;
    const int id = dbz3::modos::TorneoIdReal(int((entry - obj - 748) / 20));
    for (int vc = 0; id >= 0 && vc < dbz3::roster::VirtualCellCount(); ++vc)
      if (int(dbz3::roster::GetVirtualCell(vc).id) == id) {
        if (casilla) *casilla = cas;
        return vc;
      }
    return -1;
  }
  const int host = int16_t(REX_LOAD_U16(dbz3::GA(0x82020618) + 2 * uint32_t(cas)));
  for (int k = 0; k < 2; ++k) {
    const int id = dbz3::roster::PickedCellFor(k, host);
    if (id < 0) continue;
    for (int vc = 0; vc < dbz3::roster::VirtualCellCount(); ++vc)
      if (int(dbz3::roster::GetVirtualCell(vc).id) == id) {
        if (casilla) *casilla = cas;
        return vc;
      }
  }
  return -1;
}

// descriptor clonado de `src` con la textura/rect r = {tex, x0, y0, x1, y1}; con name tambien su quad (+0x20)
uint32_t ClonarDescriptor(uint8_t* base, uint32_t src, int clave, const int32_t r[5], bool name) {
  static std::map<int, uint32_t> bufs;
  static std::mutex mu;
  if (r[0] < 0 || src < 0x40000000u || (src & 3)) return 0;
  uint32_t blk = 0;   // primer bloque [tex, 3, 1.0f, 0, x0, y0, x1, y1] del descriptor (su sitio cambia con la imagen)
  for (uint32_t o = 0x30; o < 0x1C0 && !blk; o += 4)
    if (REX_LOAD_U32(src + o + 4) == 3 && REX_LOAD_U32(src + o + 8) == 0x3F800000u && REX_LOAD_U32(src + o + 12) == 0)
      blk = o;
  if (!blk) return 0;
  std::lock_guard<std::mutex> lk(mu);
  uint32_t& b = bufs[clave];
  if (!b) b = dbz3::roster::GuestAlloc(0x200);
  if (!b) return 0;
  constexpr uint32_t n = 0x200;
  for (uint32_t o = 0; o < n; o += 4) {
    uint32_t v = REX_LOAD_U32(src + o);
    if (v >= src && v < src + n) v = v - src + b;   // punteros internos
    REX_STORE_U32(b + o, v);
  }
  const auto f = [](float x) { return std::bit_cast<uint32_t>(x); };
  REX_STORE_U32(b + blk, uint32_t(r[0]));
  for (int k = 0; k < 4; ++k) REX_STORE_U32(b + blk + 0x10 + 4 * k, f(float(r[1 + k])));
  if (name) {
    const float w = float(r[3] - r[1]) / 2, h = float(r[4] - r[2]) / 2;
    REX_STORE_U32(b + 0x20, f(-w));
    REX_STORE_U32(b + 0x24, f(-h));
    REX_STORE_U32(b + 0x28, f(w));
    REX_STORE_U32(b + 0x2C, f(h));
  }
  return b;
}
}  // namespace

DBZ3_HOOK_EU(sub_821A36D8) {
  t_entrada = ctx.r3.u32;
  orig(ctx, base);
  t_entrada = 0;
}

DBZ3_HOOK_EU(sub_820B04C8) {
  static const bool traza = std::getenv("DBZ3_TEST_SPR") != nullptr;
  const uint32_t lr = uint32_t(ctx.lr), desc = ctx.r3.u32;
  if (desc && lr >= 0x82190000 && lr < 0x821B0000) {
    const uint32_t d = dbz3::g_guest_eu ? 0x1220 : 0;
    const uint32_t site = lr + d;   // en terminos US
    if (t_entrada && (site == 0x821A3888 || site == 0x821A3958)) {   // icono del cuadro
      int cas = -1;
      if (traza) REXLOG_INFO("dbz3 spr: icono entrada {:08X} casilla {} mando {} img {:X} celda {}", t_entrada, int16_t(REX_LOAD_U16(t_entrada + 8)), int16_t(REX_LOAD_U16(t_entrada + 10)), REX_LOAD_U32(desc + 0x14), CeldaDeEntrada(base, t_entrada, &cas));
      if (const int vc = CeldaDeEntrada(base, t_entrada, &cas); vc >= 0 && REX_LOAD_U32(desc + 0x14) == uint32_t(cas + 65))
      {
        static const char* fuerza = std::getenv("DBZ3_TEST_ICONO_TEX");   // prueba: tex a la fuerza (rect 0,0,56,56)
        int32_t forzado[5] = {fuerza ? std::atoi(fuerza) : -1, 0, 0, 56, 56};
        if (const uint32_t nd = ClonarDescriptor(base, desc, vc * 4 + (site == 0x821A3888 ? 0 : 1),
                                                 fuerza ? forzado : dbz3::roster::GetVirtualCell(vc).tor_icon, false))
          ctx.r3.u64 = nd;
      }
    } else if (site == 0x821A422C) {   // rotulo de nombre del VS (lado = r27)
      const uint32_t obj = REX_LOAD_U32(dbz3::GA(0x824681D0));
      const int idx = int8_t(REX_LOAD_U8(dbz3::GA(0x8245F270) + 36688 + ctx.r27.u32));
      if (obj && ctx.r27.u32 < 2 && idx >= 0 && idx < 32) {
        int cas = -1;
        if (traza) REXLOG_INFO("dbz3 spr: VS nombre lado {} idx {} img {:X} lr {:08X} tex@60 {} tex@B0 {}", ctx.r27.u32, idx, REX_LOAD_U32(desc + 0x14), lr, REX_LOAD_U32(desc + 0x60), REX_LOAD_U32(desc + 0xB0));
        if (const int vc = CeldaDeEntrada(base, obj + 748 + 20 * uint32_t(idx), &cas);
            vc >= 0 && REX_LOAD_U32(desc + 0x14) == uint32_t(cas + 109))
          if (const uint32_t nd = ClonarDescriptor(base, desc, vc * 4 + 2, dbz3::roster::GetVirtualCell(vc).tor_name, true)) {
            ctx.r3.u64 = nd;
            if (traza) {
              const auto& n = dbz3::roster::GetVirtualCell(vc).tor_name;
              const uint32_t banco = REX_LOAD_U32(dbz3::GA(0x8238CE10));
              REXLOG_INFO("dbz3 spr: VS nombre lado {} casilla {} -> clon {:08X} tex {} rect {} {} {} {} banco {:08X} n {} primera {}", ctx.r27.u32, cas, nd, n[0], n[1], n[2], n[3], n[4], banco, banco ? REX_LOAD_U32(banco + 16) : 0, banco ? REX_LOAD_U32(banco + 24) : 0);
            }
          }
      }
    }
  }
  if (traza && desc && lr >= 0x821A0000 && lr < 0x821B0000) {
    static std::map<std::pair<uint32_t, uint32_t>, int> vistos;
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    if (vistos.emplace(std::make_pair(lr, desc), 1).second && vistos.size() < 60)
      REXLOG_INFO("dbz3 spr: lr {:08X} desc {:08X} img {:X} -> {:08X}", lr, desc, REX_LOAD_U32(desc + 0x14), ctx.r3.u32);
  }
  orig(ctx, base);
}

// Igual que arriba para el dibujo inmediato (sub_820B08E8(descriptor r3), estados 0/1 de la entrada del cuadro): el
// icono es el primer descriptor que dibuja sub_821A36D8 (lr 0x821A37E0 / 0x821A3854; EU -0x1220).
DBZ3_HOOK_EU(sub_820B08E8) {
  const uint32_t lr = uint32_t(ctx.lr), desc = ctx.r3.u32;
  const uint32_t site = lr + (dbz3::g_guest_eu ? 0x1220 : 0);
  static const bool traza8 = std::getenv("DBZ3_TEST_SPR") != nullptr;
  if (traza8 && desc && t_entrada) {
    static std::map<std::pair<uint32_t, uint32_t>, int> vistos8;
    static std::mutex mu8;
    std::lock_guard<std::mutex> lk(mu8);
    if (vistos8.emplace(std::make_pair(site, t_entrada), 1).second)
      REXLOG_INFO("dbz3 spr: 08E8 site {:08X} entrada {:08X} estado {} casilla {} mando {} img {:X}", site, t_entrada,
                  REX_LOAD_U8(t_entrada + 4), int16_t(REX_LOAD_U16(t_entrada + 8)), int16_t(REX_LOAD_U16(t_entrada + 10)),
                  REX_LOAD_U32(desc + 0x14));
  }
  if (desc && t_entrada && (site == 0x821A37E0 || site == 0x821A3854 || site == 0x821A3868 || site == 0x821A37F4)) {
    int cas = -1;
    if (const int vc = CeldaDeEntrada(base, t_entrada, &cas); vc >= 0 && REX_LOAD_U32(desc + 0x14) == uint32_t(cas + 65)) {
      static const char* fuerza = std::getenv("DBZ3_TEST_ICONO_TEX");
      int32_t forzado[5] = {fuerza ? std::atoi(fuerza) : -1, 0, 0, 56, 56};
      if (const uint32_t nd = ClonarDescriptor(base, desc, vc * 4 + 3, fuerza ? forzado : dbz3::roster::GetVirtualCell(vc).tor_icon, false))
        ctx.r3.u64 = nd;
    }
  }
  orig(ctx, base);
}

// S5-I1 (issue #30, Broly): traza de las ORDENES de guion (#SPX). sub_8209F568 (interprete) busca la orden N en
// filas[N >> 4][N & 15] (filas = *(*(vm + 48) + 68)) y llama al manejador por ctr. Con DBZ3_TEST_SPXTRAZA=1, la primera vez
// que corre el interprete se cambia en la tabla de despacho del runtime cada manejador por una plantilla que anota
// (ms, ID del luchador (X+7420), orden(es) que lo usan) y llama al original. Solo US. Solo para pruebas.
namespace {
constexpr int kSpxMax = 400;
PPCFunc* g_spx_orig[kSpxMax];
std::vector<int> g_spx_ids[kSpxMax];
int g_spx_n = 0;
std::atomic<bool> g_spx_on{false};

template <int I>
void SpxThunk(PPCContext& ctx, uint8_t* base) {
  using namespace std::chrono;
  const int64_t ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  const uint32_t x = ctx.r4.u32;
  const uint32_t mgr = REX_LOAD_U32(0x823D6CF4);   // gestor del combate: los luchadores estan dentro (X = mgr + 2096 ...)
  const int32_t fid = mgr && x >= mgr && x + 7424 < mgr + 35584 ? int32_t(REX_LOAD_U32(x + 7420)) : -1;
  static const int filtro = std::atoi(std::getenv("DBZ3_TEST_SPXTRAZA"));   // solo ese luchador (ID)
  if (filtro > 0 && fid != filtro) {
    g_spx_orig[I](ctx, base);
    return;
  }
  static std::atomic<int> lineas{0};
  if (lineas.fetch_add(1) > 60000) {
    g_spx_orig[I](ctx, base);
    return;
  }
  std::string ids;
  for (int v : g_spx_ids[I]) ids += fmt::format("{:X} ", v);
  REXLOG_INFO("dbz3 spx: t {} luchador {} orden {}", ms, fid, ids);
  g_spx_orig[I](ctx, base);
}

template <int... Is>
constexpr auto SpxTabla(std::integer_sequence<int, Is...>) {
  return std::array<PPCFunc*, sizeof...(Is)>{&SpxThunk<Is>...};
}
const auto g_spx_thunks = SpxTabla(std::make_integer_sequence<int, kSpxMax>{});

void SpxInstalar(uint32_t vm, uint8_t* base) {
  const uint32_t obj = REX_LOAD_U32(vm + 48);
  const uint32_t tab = obj ? REX_LOAD_U32(obj + 68) : 0;
  const auto code = [](uint32_t a) { return a >= 0x82080000u && a < 0x82300000u && !(a & 3); };
  if (!tab) return;
  std::map<uint32_t, int> por_manejador;
  for (uint32_t c = 0; c < 64; ++c) {
    const uint32_t fila = REX_LOAD_U32(tab + 4 * c);
    if (!(fila >= 0x40000000u && fila < 0xA0000000u)) break;
    for (uint32_t r = 0; r < 16; ++r) {
      const uint32_t h = REX_LOAD_U32(fila + 4 * r);
      if (!code(h)) continue;
      auto it = por_manejador.find(h);
      if (it == por_manejador.end() && g_spx_n < kSpxMax) it = por_manejador.emplace(h, g_spx_n++).first;
      if (it != por_manejador.end()) g_spx_ids[it->second].push_back(int(c * 16 + r));
    }
  }
  for (const auto& [h, i] : por_manejador) {
    PPCFunc*& slot = REX_LOOKUP_FUNC(base, h);
    if (!slot) continue;
    g_spx_orig[i] = slot;
    slot = g_spx_thunks[size_t(i)];
  }
  REXLOG_INFO("dbz3 spx: {} manejadores de ordenes envueltos (tabla {:08X})", por_manejador.size(), tab);
}
}  // namespace

DBZ3_HOOK_EU(sub_8209F568) {
  static const bool traza = std::getenv("DBZ3_TEST_SPXTRAZA") != nullptr && !dbz3::g_guest_eu;
  if (traza && !g_spx_on.exchange(true)) SpxInstalar(ctx.r3.u32, base);
  orig(ctx, base);
}
