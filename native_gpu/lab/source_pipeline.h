#pragma once
#include "../native_gpu.h"
#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>
namespace dbzlab {
inline uint32_t sourceWord(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return _byteswap_ulong(v);}
inline float sourceFloat(const uint8_t* p){return std::bit_cast<float>(sourceWord(p));}
inline uint32_t sourcePipeline(const uint8_t* d,bool typed=true){
 uint32_t z=sourceWord(d+10548),b=sourceWord(d+10552),r=sourceWord(d+10568),mask=sourceWord(d+10460)&15;
 if((z&1)&&!typed)throw std::runtime_error("Source stencil requires typed raster");
 uint32_t src=b&31,dst=(b>>8)&31,op=(b>>5)&7,mode=0;
 if(op==0&&src==1&&dst==0)mode=0;else if(op==0&&src==6&&dst==7)mode=1;
 else if(op==0&&src==1&&dst==1)mode=2;else if(op==0&&src==8&&dst==0)mode=3;else if(op==0&&src==6&&dst==1)mode=4;else if(op==4&&src==6&&dst==1)mode=5;else if(op==1&&src==1&&dst==6)mode=6;else throw std::runtime_error("Unsupported source blend "+std::to_string(src)+","+std::to_string(dst)+","+std::to_string(op));
 uint32_t cull=r&3;if(cull==3)throw std::runtime_error("Both source faces culled");if(r&4)cull=((cull&1)<<1)|((cull&2)>>1);
 uint32_t flags=((z>>1)&3)|(cull<<2)|(mode<<4)|(((z>>4)&7)<<8)|(mask<<16);
 uint32_t a=(b>>16)&31,ad=(b>>24)&31,ao=(b>>21)&7;
 if(a==1&&ad==1&&ao==0)flags|=0x800;else if(!(a==1&&(ad==7||ad==0)&&ao==0))throw std::runtime_error("Unsupported source alpha blend "+std::to_string(a)+","+std::to_string(ad)+","+std::to_string(ao));
 if(typed&&ad==0&&(mode==1||mode==2||mode==4||mode==5))flags|=DBZ_NATIVE_ALPHA_DEST_ZERO;
 if(typed&&ad==7&&mode!=1)flags|=DBZ_NATIVE_ALPHA_DEST_INVERSE;
 return flags;
}
}
