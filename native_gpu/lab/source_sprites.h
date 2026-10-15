// Native adaptation of the checked 24-byte screen/world sprite families.
#pragma once
#include "source_geometry.h"
namespace dbzlab {
inline void sourceSpriteMaterial(const uint8_t* d,uint32_t kind,DbzNativeFrameDraw& draw){
 if(kind!=10&&kind!=11&&kind!=13&&kind!=15&&kind!=22)throw std::runtime_error("Unsupported source sprite material");
 float c[14][4];for(int row=0;row<14;++row)for(int col=0;col<4;++col)c[row][col]=sourceFloat(d+1920+row*16+col*4);
 std::fill_n(draw.view_projection,16,0.f);std::fill_n(draw.material,20,0.f);
 for(int row=0;row<4;++row)for(int col=0;col<4;++col)for(int k=0;k<4;++k)draw.view_projection[col*4+row]+=c[row+8][k]*c[k][col];
 auto* control=draw.material;control[0]=c[13][0];control[1]=c[13][1];control[2]=sourceFloat(d+6016+60)!=0?1.f:0.f;control[3]=1;
 for(int k=0;k<4;++k){control[4+k]=c[12][k];control[8+k]=1;control[12+k]=sourceFloat(d+6016+32+k*4);control[16+k]=sourceFloat(d+6016+48+k*4);}
 if(kind==10||kind==13||kind==15){
  if(!(std::isfinite(c[0][2])&&std::isfinite(c[0][3])&&c[0][2]<=c[0][3]&&c[0][0]>0&&c[1][1]<0))throw std::runtime_error("Unsupported source screen projection");
  if(std::abs(c[0][1])+std::abs(c[1][0])+std::abs(c[1][2])+std::abs(c[1][3])+std::abs(c[2][0])+std::abs(c[2][1])>1e-7f)throw std::runtime_error("Unsupported source screen cross terms");
  std::fill_n(draw.view_projection,16,0.f);auto* m=draw.view_projection;
  m[0]=c[0][0];m[3]=c[3][0];m[5]=c[1][1];m[7]=c[3][1];m[10]=m[15]=1;
 }
 if(kind==13||kind==22){control[2]=0;control[3]=13;std::fill_n(control+4,4,0.f);std::fill_n(control+16,4,0.f);}
 if(kind==15){control[0]=control[1]=0;control[3]=DBZ_NATIVE_SOLID_COLOR_FOG;}
 for(float v:draw.view_projection)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source sprite matrix");
 for(float v:draw.material)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source sprite material");
}
inline void sourceSpriteVertex(const uint8_t* p,const uint8_t* d,uint32_t kind,const DbzNativeFrameDraw& draw,DbzNativeFrameVertex& v){
 v={};for(int k=0;k<3;++k)v.position[k]=sourceFloat(p+k*4);
 if(kind==10||kind==13||kind==15){
  float z=std::clamp(v.position[2],sourceFloat(d+1920+8),sourceFloat(d+1920+12));
  float denominator=z*sourceFloat(d+1920+44)+sourceFloat(d+1920+60);
  if(!std::isfinite(denominator)||denominator==0)throw std::runtime_error("Invalid source screen depth divisor");
  v.position[2]=(z*sourceFloat(d+1920+40)+sourceFloat(d+1920+56))/denominator;
 }
 if(kind!=15){v.uv[0]=sourceFloat(p+16);v.uv[1]=sourceFloat(p+20);}
 v.color[0]=p[13]/255.f;v.color[1]=p[14]/255.f;v.color[2]=p[15]/255.f;v.color[3]=p[12]/255.f;
 const auto* matrix=draw.view_projection;const auto* fog=draw.material+4;
 float z=matrix[8]*v.position[0]+matrix[9]*v.position[1]+matrix[10]*v.position[2]+matrix[11];
 v.fog=std::min(std::max(std::clamp((fog[0]-z)*fog[1],0.f,1.f),1-fog[3]),1-fog[2]);
 if(kind==13||kind==22)v.fog=1;
 float values[13];std::memcpy(values,&v,sizeof(values));for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Nonfinite source sprite vertex");
}
}
