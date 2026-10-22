// CPU-state adaptation only. The renderer receives native typed raster data.
#pragma once
#include "../native_gpu.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace dbzlab {
inline DbzNativeRaster nativeRaster(const uint8_t* d){
 auto word=[&](size_t offset){uint32_t v;std::memcpy(&v,d+offset,4);return _byteswap_ulong(v);};
 auto number=[&](size_t offset){return std::bit_cast<float>(word(offset));};
 DbzNativeRaster out{};out.stencil={0,0,255,255,{7,0,0,0},{7,0,0,0}};out.alpha.function=7;
 uint32_t z=word(10548),front=word(10496),back=word(10492),r=word(10568);
 if(z&1){
  if((z&128)&&((front&0xffffff)!=(back&0xffffff)))throw std::runtime_error("Different front/back stencil references or masks");
  out.stencil.enabled=1;out.stencil.reference=uint8_t(front);out.stencil.read_mask=uint8_t(front>>8);out.stencil.write_mask=uint8_t(front>>16);
  auto face=[&](uint8_t* v,uint32_t shift){v[0]=(z>>shift)&7;v[1]=(z>>(shift+3))&7;v[2]=(z>>(shift+9))&7;v[3]=(z>>(shift+6))&7;};
  face(out.stencil.front,8);face(out.stencil.back,(z&128)?20:8);
  if(r&4)for(int k=0;k<4;++k)std::swap(out.stencil.front[k],out.stencil.back[k]);
 }
 uint32_t color=word(10556);if(color&8){out.alpha.enabled=1;out.alpha.function=color&7;out.alpha.reference=number(10500);}
 if(!std::isfinite(out.alpha.reference))throw std::runtime_error("Nonfinite alpha-test reference");
 float xs=number(10504),xo=number(10508),ys=number(10512),yo=number(10516),zs=number(10520),zo=number(10524);
 for(float v:{xs,xo,ys,yo,zs,zo})if(!std::isfinite(v))throw std::runtime_error("Nonfinite viewport");
 if(xs<0||ys>0||zs<0||zo<0||zo+zs>1)throw std::runtime_error("Unsupported viewport orientation or depth range");
 float vp[]={xo-std::abs(xs),yo-std::abs(ys),2*std::abs(xs),2*std::abs(ys),zo,zo+zs};std::copy_n(vp,6,out.viewport);
 auto signed15=[](uint32_t v){return int32_t(v&0x3fff)-int32_t(v&0x4000);};
 uint32_t tl=word(10424),br=word(10428),w=word(10436),end=word(10440),offset=(w&0x80000000)?0:word(10432);
 int32_t ox=signed15(offset),oy=signed15(offset>>16);
 out.scissor[0]=std::max(signed15(tl),int32_t(w&0x3fff)+ox);out.scissor[1]=std::max(signed15(tl>>16),int32_t((w>>16)&0x3fff)+oy);
 out.scissor[2]=std::max(out.scissor[0],std::min(signed15(br),int32_t(end&0x3fff)+ox));out.scissor[3]=std::max(out.scissor[1],std::min(signed15(br>>16),int32_t((end>>16)&0x3fff)+oy));
 return out;
}
inline bool sameStencilPipeline(const DbzNativeStencil& a,const DbzNativeStencil& b){
 return a.enabled==b.enabled&&a.read_mask==b.read_mask&&a.write_mask==b.write_mask&&std::memcmp(a.front,b.front,4)==0&&std::memcmp(a.back,b.back,4)==0;
}
}
