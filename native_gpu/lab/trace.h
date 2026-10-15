#pragma once
#include <cstdint>
struct PPCContext;
namespace dbzlab {
constexpr uint32_t guestAddress(const char* name) {
    uint32_t value=0;
    for(const char* p=name;*p;++p) if(p[0]=='s' && p[1]=='u' && p[2]=='b' && p[3]=='_') {
        p+=4;
        for(int i=0;i<8;++i) {
            char c=p[i];uint32_t d=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:16;
            if(d>15)return 0;value=(value<<4)|d;
        }
        return value;
    }
    return 0;
}
void enter(uint32_t address,PPCContext& ctx,uint8_t* base);
void indexed(uint32_t device,uint32_t primitive,uint32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base);
void up(uint32_t device,uint32_t primitive,uint32_t count,uint32_t vertices,uint32_t stride,uint8_t* base);
void resolve(uint32_t device,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t source_rect,uint32_t destination_point);
}
