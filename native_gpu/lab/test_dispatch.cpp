// Exercise both EU pointer-dispatch repairs with real PPC contexts and the
// SDK function-table ABI, without booting the game or touching player data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include "dbz3_eu_pch.h"
REX_EXTERN(sub_820F2370);
REX_EXTERN(sub_820BB8C8);
static uint64_t argument=0;static uint32_t target=0;static int invoked=0;
static void callback(PPCContext& ctx,uint8_t*) {
    argument=ctx.r3.u64;target=ctx.ctr.u32;++invoked;ctx.r3.u64=0xcafebabe;
}
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    uint8_t* base=nullptr;
    try {
        base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x90000000,MEM_RESERVE,PAGE_NOACCESS));require(base,"reserve guest arena");
        auto page=[&](uint32_t address){require(VirtualAlloc(base+(address&~0xfffu),0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit guest page");};
        constexpr uint32_t callee=0x820F2398;
        uint32_t slot=uint32_t(REX_IMAGE_BASE+REX_IMAGE_SIZE+(callee-REX_CODE_BASE)*2);
        for(uint32_t address:{0x8201E348u,0x82322B08u,0x40000000u,0x40001000u,slot})page(address);
        *reinterpret_cast<PPCFunc**>(base+slot)=callback;
        PPCContext ctx{};ctx.r3.u32=2;ctx.r4.u64=0x1122334455667788ull;
        REX_STORE_U32(0x8201E348+8,callee);
        sub_820F2370(ctx,base);
        require(invoked==1&&argument==0x1122334455667788ull&&target==callee&&ctx.r3.u32==0xcafebabe,"event pointer dispatch");
        ctx.r3.u32=1;REX_STORE_U32(0x8201E348+4,0);
        sub_820F2370(ctx,base);require(invoked==1&&ctx.r3.u32==1&&ctx.cr6.eq,"null event returns without dispatch");
        ctx.r3.u32=0x40000000;REX_STORE_U32(0x40000000+48,0x40001000);REX_STORE_U16(0x40001000+82,3);REX_STORE_U32(0x82322B08+12,callee);
        sub_820BB8C8(ctx,base);
        require(invoked==2&&argument==0x40001000&&target==callee&&ctx.r3.u32==0xcafebabe,"state pointer dispatch");
        VirtualFree(base,0,MEM_RELEASE);std::puts("EU pointer dispatch: 3 checks passed");return 0;
    }catch(const std::exception& e){if(base)VirtualFree(base,0,MEM_RELEASE);std::fprintf(stderr,"%s\n",e.what());return 1;}
}
