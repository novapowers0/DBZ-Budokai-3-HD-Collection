// EU codegen workaround: this routine dispatches a function pointer read
// from 0x8201E348. SDK 0.10 incorrectly emits an integer switch with case 0,
// even with the explicit indirect-call hint. Preserve all PPC-visible effects
// and route the target through the normal bounds-checked function dispatcher.
#include "dbz3_eu_pch.h"
#include <rex/hook.h>
REX_HOOK_RAW(sub_820F2370) {
    ctx.r11.s64=-2113798144;
    ctx.r10.u64=__builtin_rotateleft64(ctx.r3.u32|(ctx.r3.u64<<32),2)&0xfffffffc;
    ctx.r11.s64+=-7352;
    ctx.r11.u64=REX_LOAD_U32(ctx.r10.u32+ctx.r11.u32);
    ctx.cr6.compare<uint32_t>(ctx.r11.u32,0,ctx.xer);
    if(ctx.cr6.eq)return;
    ctx.r3.u64=ctx.r4.u64;
    ctx.ctr.u64=ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.r11.u32);
}
// The same SDK misclassification affects this eight-instruction EU thunk.
// The table at 0x82322B08 holds callable guest addresses, not integer cases.
REX_HOOK_RAW(sub_820BB8C8) {
    ctx.r3.u64=REX_LOAD_U32(ctx.r3.u32+48);
    ctx.r11.s64=-2110652416;
    ctx.r11.s64+=11016;
    ctx.r10.s64=int16_t(REX_LOAD_U16(ctx.r3.u32+82));
    ctx.r10.u64=__builtin_rotateleft64(ctx.r10.u32|(ctx.r10.u64<<32),2)&0xfffffffc;
    ctx.r11.u64=REX_LOAD_U32(ctx.r10.u32+ctx.r11.u32);
    ctx.ctr.u64=ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.r11.u32);
}
