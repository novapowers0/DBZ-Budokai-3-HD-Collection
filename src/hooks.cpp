// dbz3 - ReXGlue codegen bug workarounds
//
// Two functions in the recompiled output have bctr jump-table dispatches that
// the rexglue codegen miscompiles. These REX_HOOK_RAW overrides reimplement
// the dispatch correctly. Keep them until the underlying codegen bugs are
// fixed upstream.
//
// 1. sub_820F2280 (combat/event dispatch, jump table 0x8201E350):
//    The generated `switch (ctx.r3.u32)` reads r3 AFTER the `mr r3,r4`, so it
//    indexes with r4 (the argument) instead of the original r3 index. Handlers
//    0 and 1 are inline blocks (loc_820F22A8 / loc_820F22B8), not registered
//    functions, so they must be handled directly; the rest are real functions.
//
// 2. sub_820BB938 (sound ID dispatch, jump table 0x82322B08):
//    The generated `switch (ctx.r11.u32)` only has `case 0`, but r11 holds the
//    handler ADDRESS (e.g. 0x820BB958), never 0, so any non-zero sound ID
//    traps. Reimplement with dynamic dispatch.

#include "hooks.h"

#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/thread.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "generated/dbz3_init.h"

#include "xv2_fisica_runtime.inl"
#include "eu_map.h"

REX_HOOK_RAW(sub_820F2280) {
    // r3 = original index, r4 = argument passed to the handler
    uint32_t index = ctx.r3.u32;
    // lis r11,-32254 -> 0x82020000 ; addi r11,r11,-7344 -> 0x8201E350
    uint32_t handler = REX_LOAD_U32((0x82020000 - 7344) + index * 4);
    // cmplwi cr6,r11,0 ; beqlr cr6
    if (handler == 0) {
        return;
    }
    // mr r3,r4
    ctx.r3.u64 = ctx.r4.u64;
    // loc_820F22A8 (index 0): lwz r5,8(r3); lwz r4,4(r3); lwz r3,0(r3); b 0x820e54f8
    if (handler == 0x820F22A8) {
        ctx.r5.u64 = REX_LOAD_U32(ctx.r3.u32 + 8);
        ctx.r4.u64 = REX_LOAD_U32(ctx.r3.u32 + 4);
        ctx.r3.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
        sub_820E54F8(ctx, base);
        return;
    }
    // loc_820F22B8 (index 1): b 0x820e0560
    if (handler == 0x820F22B8) {
        sub_820E0560(ctx, base);
        return;
    }
    // mtctr r11 ; bctr
    REX_CALL_INDIRECT_FUNC(handler);
}

REX_HOOK_RAW(sub_820BB938) {
    // lwz r3,48(r3) - load inner object
    ctx.r3.u64 = REX_LOAD_U32(ctx.r3.u32 + 48);
    // lha r10,82(r3); rlwinm r10,r10,2,0,29
    int32_t idx = int16_t(REX_LOAD_U16(ctx.r3.u32 + 82));
    // lis r11,-32206 -> 0x82320000; addi r11,r11,11016 -> 0x82322B08
    uint32_t handler = REX_LOAD_U32((0x82320000 + 11016) + idx * 4);
    // mtctr r11 ; bctr
    if (handler == 0) {
        return;
    }
    ctx.ctr.u64 = handler;
    REX_CALL_INDIRECT_FUNC(handler);
}

// Espera del bucle principal (sub_82231110, EU sub_8222F8C0; unico llamador que
// la usa, medido en solLAT2). Cada espera gira como el original los primeros
// dbz3_espera_giro_us y solo despues duerme dbz3_espera_dormir_us por vuelta:
// las esperas cortas (respuesta al mando) no pagan el despertar del planificador
// y las largas dejan de quemar CPU. solLAT3 (n>=300): combate mediana 5,55->5,51 ms,
// p95 12,0->7,3, 43-47 -> 58-60 FPS con 2 nucleos; menu CPU 108->32 %.
// dbz3_espera_dormir_us=0 vuelve al giro original. Se leen al arrancar.
REXCVAR_DEFINE_INT32(dbz3_espera_dormir_us, 100, "DBZ3/Rendimiento",
                     "Ahorro de CPU en el bucle principal: microsegundos de sueno por vuelta (0 = giro original; requiere reiniciar)");
REXCVAR_DEFINE_INT32(dbz3_espera_giro_us, 1000, "DBZ3/Rendimiento",
                     "Ahorro de CPU: microsegundos que gira cada espera antes de dormir (requiere reiniciar)");
namespace {
thread_local std::chrono::steady_clock::time_point g_espera_inicio{};
}
DBZ3_HOOK(sub_82231110, dbz3eu_sub_8222F8C0) {
    g_espera_inicio = std::chrono::steady_clock::now();
    orig(ctx, base);
}
DBZ3_HOOK(sub_82235918, dbz3eu_sub_822340C8) {
    const uint32_t retorno = uint32_t(ctx.lr); // orig modifica LR; guardar antes.
    orig(ctx, base);
    static const int dormir_us = static_cast<int>(REXCVAR_GET(dbz3_espera_dormir_us));
    static const int giro_us = std::max(0, static_cast<int>(REXCVAR_GET(dbz3_espera_giro_us)));
    const uint32_t medido = dbz3::g_guest_eu ? 0x8222F958 : 0x822311A8;
    if (dormir_us <= 0 || retorno != medido || ctx.r3.u32 == 0) {
        return;
    }
    if (std::chrono::steady_clock::now() - g_espera_inicio < std::chrono::microseconds(giro_us)) {
        return; // fase de giro: igual que el original
    }
    static const bool sonda = [] {
        const char* s = std::getenv("DBZ3_TEST_ESPERA_SONDA");
        return s && s[0] == '1' && s[1] == 0;
    }();
    if (!sonda) {
        rex::thread::Sleep(std::chrono::microseconds(dormir_us));
        return;
    }
    // Solo arnes: medir el tiempo real bloqueado, no confundirlo con el plazo
    // solicitado ni con el instante (desconocido) en que se cumple la espera.
    using Reloj = std::chrono::steady_clock;
    struct Medida {
        Reloj::time_point inicio = Reloj::now();
        uint64_t n = 0, total = 0, maximo = 0, mas1ms = 0, mas5ms = 0;
    };
    thread_local Medida m;
    const auto antes = Reloj::now();
    rex::thread::Sleep(std::chrono::microseconds(dormir_us));
    const auto despues = Reloj::now();
    const auto us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(despues - antes).count());
    ++m.n;
    m.total += us;
    if (us > m.maximo) m.maximo = us;
    if (us > 1000) ++m.mas1ms;
    if (us > 5000) ++m.mas5ms;
    if (despues - m.inicio >= std::chrono::seconds(5)) {
        REXLOG_INFO("dbz3: espera_sonda lr={:08X} solicitado_us={} n={} total_us={} max_us={} mas1ms={} mas5ms={}",
                    retorno, dormir_us, m.n, m.total, m.maximo, m.mas1ms, m.mas5ms);
        m = Medida{};
    }
}
