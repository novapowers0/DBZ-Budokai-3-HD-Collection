// dbz3 - Guarda de registros no volatiles de los manejadores de accion del luchador (CRP, 2026-10-23).
//
// sub_8210E700 recorre los luchadores (cfg+35096 / cfg+35100) y llama al manejador de su accion (X+3244) por la
// tabla 0x8201F090. Despues comprueba X+4924 y, si es 0, llama a la entrada fija de la tabla (+180). El cierre
// 0x701912EC del 23/10 08:18 (Goku contra Broly ID 40, escenario 2, lr 0x8210E798, ctr 0xFFFFFFFF) es esa segunda
// llamada con el registro r27 (base de la tabla) ya roto: el destino leido deja de ser el `blr` de 0x82308398 y el
// despachador acaba leyendo una direccion basura. La tabla esta en .rdata (no se puede escribir); la unica via es
// que un manejador (o algo de su arbol) no haya conservado los no volatiles del llamante.
//
// Aqui se envuelven los manejadores UNICOS de la tabla US y se devuelven r14..r31 a los valores de entrada: es lo
// que la ABI PPC exige de ellos. Con la lista y la accion ya saneadas en ports_guard.cpp, el fotograma no puede
// quedar con la base de la tabla cambiada. Solo US: las funciones EU no estan en src/eu_map_gen.h.
//
// Prueba A/B (solo pruebas): DBZ3_TEST_REG_MALO=1 rompe r27 a proposito en la primera llamada del fotograma; con
// DBZ3_TEST_SIN_GUARDAS=1 la guarda no lo devuelve y el juego cae en 0x8210E798 con destino 0xFFFFFFFF (igual que
// el 23/10); sin SIN_GUARDAS se devuelve y el combate sigue.
#include <cstdint>
#include <cstdlib>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>

#include "generated/dbz3_init.h"

namespace {

bool SinGuardas() {
  static const bool v = std::getenv("DBZ3_TEST_SIN_GUARDAS") != nullptr;
  return v;
}

bool RegMalo() {
  static const bool v = std::getenv("DBZ3_TEST_REG_MALO") != nullptr;
  return v;
}

bool RangoLuchador(uint32_t o) {
  return (o >= 0x20000000u && o < 0x50000000u) || (o >= 0x82000000u && o < 0x83000000u);
}

}  // namespace

#define GUARDA_REG(n)                                                                    \
  REX_EXTERN(__imp__sub_##n);                                                            \
  REX_HOOK_RAW(sub_##n) {                                                                \
    const uint32_t obj = ctx.r3.u32;                                                     \
    const uint64_t r14 = ctx.r14.u64, r15 = ctx.r15.u64, r16 = ctx.r16.u64;              \
    const uint64_t r17 = ctx.r17.u64, r18 = ctx.r18.u64, r19 = ctx.r19.u64;              \
    const uint64_t r20 = ctx.r20.u64, r21 = ctx.r21.u64, r22 = ctx.r22.u64;              \
    const uint64_t r23 = ctx.r23.u64, r24 = ctx.r24.u64, r25 = ctx.r25.u64;              \
    const uint64_t r26 = ctx.r26.u64, r27 = ctx.r27.u64, r28 = ctx.r28.u64;              \
    const uint64_t r29 = ctx.r29.u64, r30 = ctx.r30.u64, r31 = ctx.r31.u64;              \
    const uint32_t lr_entrada = ctx.lr;                                                  \
    __imp__sub_##n(ctx, base);                                                           \
    if (RegMalo() && lr_entrada == 0x8210E77Cu && RangoLuchador(obj)) {                  \
      /* prueba A/B: la 2a llamada de sub_8210E700 leera 0xFFFFFFFF de *(r27+180).      \
         Solo el caso FAIL (sin guardas) toca X+4924 para forzar esa 2a llamada. */      \
      static uint32_t scratch = 0;                                                       \
      static bool inyectado = false;                                                     \
      if (!scratch) {                                                                    \
        scratch = rex::system::kernel_state()->memory()->SystemHeapAlloc(0x200, 0x20);    \
        if (scratch) REX_STORE_U32(scratch + 180, 0xFFFFFFFFu);                          \
      }                                                                                  \
      if (scratch) {                                                                     \
        if (SinGuardas() && !inyectado) { REX_STORE_U8(obj + 4924, 0); inyectado = true; } \
        ctx.r27.u64 = scratch;                                                           \
      }                                                                                  \
    }                                                                                    \
    if (!SinGuardas()) {                                                                 \
      ctx.r14.u64 = r14, ctx.r15.u64 = r15, ctx.r16.u64 = r16;                           \
      ctx.r17.u64 = r17, ctx.r18.u64 = r18, ctx.r19.u64 = r19;                           \
      ctx.r20.u64 = r20, ctx.r21.u64 = r21, ctx.r22.u64 = r22;                           \
      ctx.r23.u64 = r23, ctx.r24.u64 = r24, ctx.r25.u64 = r25;                           \
      ctx.r26.u64 = r26, ctx.r27.u64 = r27, ctx.r28.u64 = r28;                           \
      ctx.r29.u64 = r29, ctx.r30.u64 = r30, ctx.r31.u64 = r31;                           \
    }                                                                                    \
  }

// Manejadores unicos de la tabla de acciones 0x8201F090 (52 entradas, US).
GUARDA_REG(82104DD0)
GUARDA_REG(82103BB8)
GUARDA_REG(82103D08)
GUARDA_REG(82103ED8)
GUARDA_REG(82104208)
GUARDA_REG(82104328)
GUARDA_REG(82104468)
GUARDA_REG(821045B0)
GUARDA_REG(82104668)
GUARDA_REG(82104838)
GUARDA_REG(8210C3D0)
GUARDA_REG(82104A50)
GUARDA_REG(82104B90)
GUARDA_REG(8210CC90)
GUARDA_REG(82105358)
GUARDA_REG(82105730)
GUARDA_REG(82105A08)
GUARDA_REG(821065A8)
GUARDA_REG(82105FA8)
GUARDA_REG(821077B8)
GUARDA_REG(821069F0)
GUARDA_REG(82106B90)
GUARDA_REG(82106F80)
GUARDA_REG(82107948)
GUARDA_REG(82107A90)
GUARDA_REG(82107FF8)
GUARDA_REG(82108048)
GUARDA_REG(82108338)
GUARDA_REG(821087D8)
GUARDA_REG(82108E58)
GUARDA_REG(82308398)
