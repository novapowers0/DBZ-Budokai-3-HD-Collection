#include "dbz3_eu_pch.h"
#include <rex/hook.h>
#include "trace.h"
#include "native_loading.h"
#include "native_indexed.h"
#include "native_stream.h"
#include "reference_capture.h"
#include "native_services.h"
#include "native_input_audit.h"
REX_EXTERN(__imp__sub_8223A6B8);
REX_EXTERN(__imp__sub_8223A670);
REX_EXTERN(__imp__sub_82237B30);
REX_EXTERN(__imp__sub_8223B560);
REX_EXTERN(__imp__sub_82230210);
REX_EXTERN(__imp__sub_822485C0);
REX_EXTERN(__imp__sub_82248650);
REX_HOOK_RAW(sub_82230210) {
    uint32_t device=ctx.r3.u32,begin=ctx.r4.u32;
    uint32_t label=REX_LOAD_U32(device+11024),serial=REX_LOAD_U32(device+11036);
    uint32_t cursor=REX_LOAD_U32(device+48)|(REX_LOAD_U32(device+15324)&3);
    __imp__sub_82230210(ctx,base);
    if(dbzlab::nativeServicesEnabled()){
        dbzlab::nativeCompleteBuffer(base,label,serial,cursor);
        dbzlab::nativeAuditBuffer(base,device,begin,ctx.r3.u32,label,serial,cursor);
    }
}
REX_HOOK_RAW(sub_822485C0) {
    uint32_t label=ctx.r4.u32,device=ctx.r3.u32,begin=REX_LOAD_U32(device+48);
    __imp__sub_822485C0(ctx,base);
    if(dbzlab::nativeServicesEnabled())dbzlab::nativeWriteDisplayCounter(base,label,label&3,begin,ctx.r3.u32);
}
REX_HOOK_RAW(sub_82248650) {
    uint32_t device=ctx.r3.u32,label=0,begin=REX_LOAD_U32(device+48);
    if(REX_LOAD_U32(device+22056)==2){
        uint32_t current=REX_LOAD_U32(device+22064),next=current==3?0:current+1;
        if(next!=REX_LOAD_U32(device+22060))label=REX_LOAD_U32(device+22052)+(next*2+(ctx.r4.u32?0:1))*4;
    }
    __imp__sub_82248650(ctx,base);
    if(label&&dbzlab::nativeServicesEnabled())dbzlab::nativeWriteDisplayCounter(base,label,2,begin,REX_LOAD_U32(device+48));
}
REX_HOOK_RAW(sub_8223A6B8) {
    dbzlab::referenceBeforeIndexed();
    uint32_t device=ctx.r3.u32,primitive=ctx.r4.u32,vertexBase=ctx.r5.u32,indexStart=ctx.r6.u32,count=ctx.r7.u32;
    dbzlab::InputStateAudit audit(base,device,0);
    bool native=dbzlab::nativeServicesEnabled();
    if(native)dbzlab::streamObserve(device,primitive,int32_t(vertexBase),indexStart,count,base);
    __imp__sub_8223A6B8(ctx,base);
    if(!native)dbzlab::streamObserve(device,primitive,int32_t(vertexBase),indexStart,count,base);
    dbzlab::worldObserve(device,primitive,int32_t(vertexBase),indexStart,count,base);
    dbzlab::indexed(device,primitive,vertexBase,indexStart,count,base);
}
REX_HOOK_RAW(sub_8223A670) {
    uint32_t device=ctx.r3.u32,primitive=ctx.r4.u32,count=ctx.r5.u32,vertices=ctx.r6.u32,stride=ctx.r7.u32;
    dbzlab::InputStateAudit audit(base,device,1);
    dbzlab::shadowObserve(ctx,base);
    bool native=dbzlab::nativeServicesEnabled();
    if(native)dbzlab::streamObserveUp(device,primitive,count,vertices,stride,base);
    __imp__sub_8223A670(ctx,base);
    if(!native)dbzlab::streamObserveUp(device,primitive,count,vertices,stride,base);
    dbzlab::up(device,primitive,count,vertices,stride,base);
}
REX_HOOK_RAW(sub_8223B560) {
    uint32_t device=ctx.r3.u32,flags=ctx.r4.u32,destination=ctx.r8.u32,sourceRect=ctx.r5.u32,destinationPoint=ctx.r9.u32;
    dbzlab::InputStateAudit audit(base,device,2);
    __imp__sub_8223B560(ctx,base);
    dbzlab::streamResolve(device,flags,destination,base,sourceRect,destinationPoint);
    dbzlab::resolve(device,flags,destination,base,sourceRect,destinationPoint);
}
REX_HOOK_RAW(sub_82237B30) {
    if(dbzlab::nativeServicesEnabled())dbzlab::nativePaceFrame();
    dbzlab::shadowPresent();
    dbzlab::worldPresent();
    dbzlab::streamPresent();
    __imp__sub_82237B30(ctx,base);
    dbzlab::referenceAfterSwap();
}
