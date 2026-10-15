// Identify source texture content without hashing uninitialized pitch/tile
// padding. This is CPU asset addressing, independent of the reference GPU.
#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
namespace dbzlab {
inline uint64_t textureTileOffset(uint32_t x,uint32_t y,uint32_t pitch,uint32_t logBytes){
 pitch=(pitch+31)&~31u;
 uint64_t macro=(uint64_t(x>>5)+uint64_t(y>>5)*(pitch>>5))<<(logBytes+7);
 uint64_t micro=uint64_t((x&7)+((y&14)<<2))<<logBytes;
 uint64_t offset=macro+((micro&~15ull)<<1)+(micro&15)+((y&1)<<4);
 return ((offset&~511ull)<<3)+((y&16)<<7)+((offset&448)<<2)+(((((y&8)>>2)+(x>>3))&3)<<6)+(offset&63);
}
inline uint64_t textureContentHash(const uint8_t* raw,size_t size,const uint32_t* fetch){
 if((fetch[0]&3)!=2)return 0;
 uint32_t format=fetch[1]&63,block=format>=18&&format<=20?4:1;
 if(format!=6&&format!=18&&format!=19&&format!=20)return 0;
 uint32_t bytes=format==18?8:format==6?4:16,logBytes=bytes==4?2:bytes==8?3:4;
 uint32_t width=(fetch[2]&8191)+1,height=((fetch[2]>>13)&8191)+1;
 if(width>4096||height>4096)return 0;
 uint32_t w=(width+block-1)/block,h=(height+block-1)/block,pitch=(((fetch[0]>>22)&511)*32+block-1)/block;
 if(pitch<w)return 0;
 uint32_t packedX=0,packedY=0;
 if((fetch[5]&0x800)&&std::min(width,height)<=16){
  auto logCeil=[](uint32_t n){uint32_t bits=0;for(--n;n;n>>=1)++bits;return bits;};
  if(logCeil(width)>logCeil(height))packedY=16/block;else packedX=16/block;
 }
 uint32_t endian=(fetch[1]>>6)&3,xorMask=endian==1?1:endian==2?3:endian==3?2:0;
 uint64_t value=14695981039346656037ull;
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  uint64_t offset=(fetch[0]>>31)?textureTileOffset(x+packedX,y+packedY,pitch,logBytes):(uint64_t(y+packedY)*pitch+x+packedX)*bytes;
  if(offset+bytes>size)return 0;
  for(uint32_t b=0;b<bytes;++b){value^=raw[offset+(b^xorMask)];value*=1099511628211ull;}
 }
 uint32_t swizzle=(fetch[3]>>1)&4095;
 for(int i=0;i<4;++i){value^=(swizzle>>(i*8))&255;value*=1099511628211ull;}
 return value;
}
}
