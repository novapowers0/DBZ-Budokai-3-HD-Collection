// dbz3 - Motor grafico "Direct3D 12 nativo": ganchos del driver grafico del guest EU/PAL.
// Mismos puntos que native_gpu/lab/graphics_hooks.cpp (direcciones de la imagen EU, ver
// native_gpu/lab/README.md), sin la instrumentacion de investigacion del laboratorio. Fuera del
// motor nativo cada gancho llama directamente a la funcion original.
#if defined(DBZ3_DUAL_REGION)
#include "generated/dbz3_init.h"
// nucleo dual: el codigo EU lleva el prefijo dbz3eu_ (tools/prefix_eu_codegen.py)
#define DBZ3_NATIVE_EU(sym) dbz3eu_##sym
#else
#include "dbz3_eu_init.h"
#define DBZ3_NATIVE_EU(sym) sym
#endif
#include <rex/hook.h>

#include "native_gpu/lab/native_services.h"
#include "native_gpu/lab/native_stream.h"

#define DBZ3_NATIVE_IMP_(sym) __imp__##sym
#define DBZ3_NATIVE_IMP(sym) DBZ3_NATIVE_IMP_(sym)
#define DBZ3_NATIVE_EXTERN_(sym) REX_EXTERN(__imp__##sym)
#define DBZ3_NATIVE_EXTERN(sym) DBZ3_NATIVE_EXTERN_(sym)
#define DBZ3_NATIVE_HOOK_(sym) REX_HOOK_RAW(sym)
#define DBZ3_NATIVE_HOOK(sym) DBZ3_NATIVE_HOOK_(sym)

#define kSwap DBZ3_NATIVE_EU(sub_82237B30)
#define kBufferDone DBZ3_NATIVE_EU(sub_82230210)
#define kDrawIndexed DBZ3_NATIVE_EU(sub_8223A6B8)
#define kDrawUp DBZ3_NATIVE_EU(sub_8223A670)
#define kResolve DBZ3_NATIVE_EU(sub_8223B560)
#define kDisplayCounter DBZ3_NATIVE_EU(sub_822485C0)
#define kFlipCounter DBZ3_NATIVE_EU(sub_82248650)

DBZ3_NATIVE_EXTERN(kSwap);
DBZ3_NATIVE_EXTERN(kBufferDone);
DBZ3_NATIVE_EXTERN(kDrawIndexed);
DBZ3_NATIVE_EXTERN(kDrawUp);
DBZ3_NATIVE_EXTERN(kResolve);
DBZ3_NATIVE_EXTERN(kDisplayCounter);
DBZ3_NATIVE_EXTERN(kFlipCounter);

DBZ3_NATIVE_HOOK(kBufferDone) {
  if (!dbzlab::nativeServicesEnabled()) return DBZ3_NATIVE_IMP(kBufferDone)(ctx, base);
  uint32_t device = ctx.r3.u32, begin = ctx.r4.u32;
  uint32_t label = REX_LOAD_U32(device + 11024), serial = REX_LOAD_U32(device + 11036);
  uint32_t cursor = REX_LOAD_U32(device + 48) | (REX_LOAD_U32(device + 15324) & 3);
  DBZ3_NATIVE_IMP(kBufferDone)(ctx, base);
  dbzlab::nativeCompleteBuffer(base, label, serial, cursor);
  dbzlab::nativeAuditBuffer(base, device, begin, ctx.r3.u32, label, serial, cursor);
}

DBZ3_NATIVE_HOOK(kDisplayCounter) {
  if (!dbzlab::nativeServicesEnabled()) return DBZ3_NATIVE_IMP(kDisplayCounter)(ctx, base);
  uint32_t label = ctx.r4.u32, device = ctx.r3.u32, begin = REX_LOAD_U32(device + 48);
  DBZ3_NATIVE_IMP(kDisplayCounter)(ctx, base);
  dbzlab::nativeWriteDisplayCounter(base, label, label & 3, begin, ctx.r3.u32);
}

DBZ3_NATIVE_HOOK(kFlipCounter) {
  if (!dbzlab::nativeServicesEnabled()) return DBZ3_NATIVE_IMP(kFlipCounter)(ctx, base);
  uint32_t device = ctx.r3.u32, label = 0, begin = REX_LOAD_U32(device + 48);
  if (REX_LOAD_U32(device + 22056) == 2) {
    uint32_t current = REX_LOAD_U32(device + 22064), next = current == 3 ? 0 : current + 1;
    if (next != REX_LOAD_U32(device + 22060))
      label = REX_LOAD_U32(device + 22052) + (next * 2 + (ctx.r4.u32 ? 0 : 1)) * 4;
  }
  DBZ3_NATIVE_IMP(kFlipCounter)(ctx, base);
  if (label) dbzlab::nativeWriteDisplayCounter(base, label, 2, begin, REX_LOAD_U32(device + 48));
}

DBZ3_NATIVE_HOOK(kDrawIndexed) {
  if (dbzlab::nativeServicesEnabled())
    dbzlab::streamObserve(ctx.r3.u32, ctx.r4.u32, int32_t(ctx.r5.u32), ctx.r6.u32, ctx.r7.u32, base);
  DBZ3_NATIVE_IMP(kDrawIndexed)(ctx, base);
}

DBZ3_NATIVE_HOOK(kDrawUp) {
  if (dbzlab::nativeServicesEnabled())
    dbzlab::streamObserveUp(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, base);
  DBZ3_NATIVE_IMP(kDrawUp)(ctx, base);
}

DBZ3_NATIVE_HOOK(kResolve) {
  if (!dbzlab::nativeServicesEnabled()) return DBZ3_NATIVE_IMP(kResolve)(ctx, base);
  uint32_t device = ctx.r3.u32, flags = ctx.r4.u32, destination = ctx.r8.u32, source_rect = ctx.r5.u32,
           destination_point = ctx.r9.u32;
  DBZ3_NATIVE_IMP(kResolve)(ctx, base);
  dbzlab::streamResolve(device, flags, destination, base, source_rect, destination_point);
}

DBZ3_NATIVE_HOOK(kSwap) {
  if (dbzlab::nativeServicesEnabled()) {
    dbzlab::nativePaceFrame();
    dbzlab::streamPresent();
  }
  DBZ3_NATIVE_IMP(kSwap)(ctx, base);
}
