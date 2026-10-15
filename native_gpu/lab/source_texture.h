// CPU asset addressing only. Native renderer receives linear RGBA/BC blocks.
#pragma once
#include "texture_identity.h"
#include "../native_gpu.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace dbzlab {
struct SourceTexture {
 uint32_t width,height,format,swizzle,address,pitch,block,bytes,endian,packedX,packedY;
 bool tiled;
 size_t allocation;
};
inline SourceTexture sourceTextureLayout(const uint32_t* f){
 uint32_t format=f[1]&63;
 if((f[0]&3)!=2||(f[1]&(1u<<10))||((f[5]>>9)&3)!=1||(f[0]&0x3fc))throw std::runtime_error("Unsupported source texture binding, dimension or sign "+std::to_string(f[0]&3)+","+std::to_string((f[1]>>10)&1)+","+std::to_string((f[5]>>9)&3)+","+std::to_string(f[0]&0x3fc));
 if(format!=2&&format!=6&&format!=15&&format!=18&&format!=19&&format!=20)throw std::runtime_error("Unsupported source texture format");
 SourceTexture d{};d.width=(f[2]&8191)+1;d.height=((f[2]>>13)&8191)+1;
 if(d.width>4096||d.height>4096)throw std::runtime_error("Source texture exceeds bounds");
 d.format=format==2?DBZ_NATIVE_R8:(format==6||format==15)?DBZ_NATIVE_RGBA8:format==18?DBZ_NATIVE_BC1:format==19?DBZ_NATIVE_BC2:DBZ_NATIVE_BC3;
 d.block=(format==2||format==6||format==15)?1:4;d.bytes=format==2?1:format==6?4:format==15?2:format==18?8:16;d.pitch=(((f[0]>>22)&511)*32+d.block-1)/d.block;
 d.swizzle=(f[3]>>1)&4095;d.address=f[1]&0xfffff000;d.endian=(f[1]>>6)&3;d.tiled=(f[0]>>31)!=0;
 for(int c=0;c<4;++c)if(((d.swizzle>>(3*c))&7)>5)throw std::runtime_error("Invalid source texture swizzle");
 if(format==2){uint32_t composed=0;for(int c=0;c<4;++c){uint32_t select=(d.swizzle>>(3*c))&7;composed|=(select<4?0:select)<<(3*c);}d.swizzle=composed;}
 uint32_t w=(d.width+d.block-1)/d.block,h=(d.height+d.block-1)/d.block;
 if((f[5]&0x800)&&std::min(d.width,d.height)<=16){
  auto logCeil=[](uint32_t n){uint32_t bits=0;for(--n;n;n>>=1)++bits;return bits;};
  if(logCeil(d.width)>logCeil(d.height))d.packedY=16/d.block;else d.packedX=16/d.block;
 }
 if(!d.pitch||d.pitch<w||(!d.tiled&&d.pitch<w+d.packedX))throw std::runtime_error("Invalid source texture pitch");
 uint64_t pitch=d.tiled?(d.pitch+31)&~31u:d.pitch,rows=h+d.packedY;
 if(d.tiled)rows=(rows+31)&~31ull;
 d.allocation=size_t(pitch*rows*d.bytes);
 if(d.tiled)d.allocation=(d.allocation+4095)&~size_t(4095);
 if(d.allocation>16*1024*1024||uint64_t(d.address)+d.allocation>0x20000000)throw std::runtime_error("Source texture allocation exceeds bounds");
 return d;
}
inline std::vector<uint8_t> linearizeSourceTexture(const uint8_t* raw,size_t size,const SourceTexture& d){
 uint32_t w=(d.width+d.block-1)/d.block,h=(d.height+d.block-1)/d.block,logBytes=d.bytes==1?0:d.bytes==2?1:d.bytes==4?2:d.bytes==8?3:4;
 uint32_t xorMask=d.endian==1?1:d.endian==2?3:d.endian==3?2:0;
 std::vector<uint8_t> data(size_t(w)*h*d.bytes);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  uint64_t offset=d.tiled?textureTileOffset(x+d.packedX,y+d.packedY,d.pitch,logBytes):(uint64_t(y+d.packedY)*d.pitch+x+d.packedX)*d.bytes;
  if(offset+d.bytes>size)throw std::runtime_error("Truncated source texture");
  for(uint32_t b=0;b<d.bytes;++b){uint64_t at=(offset+b)^xorMask;if(at>=size)throw std::runtime_error("Truncated source texture endian group");data[(size_t(y)*w+x)*d.bytes+b]=raw[at];}
 }
 if(d.bytes==2&&d.format==DBZ_NATIVE_RGBA8){
  // Guest R4G4B4A4 has R in the low nibble. Each UNORM value n/15
  // is exactly representable in RGBA8 as (n*17)/255.
  std::vector<uint8_t> rgba(size_t(w)*h*4);
  for(size_t i=0;i<size_t(w)*h;++i){uint32_t packed=uint32_t(data[i*2])|(uint32_t(data[i*2+1])<<8);for(uint32_t c=0;c<4;++c)rgba[i*4+c]=uint8_t(((packed>>(c*4))&15)*17);}
  return rgba;
 }
 return data;
}
}
