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

#include "generated/dbz3_init.h"

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
