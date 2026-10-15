// Checked high-level material/mesh adaptation; no reference GPU output input.
#pragma once
#include "source_pipeline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace dbzlab {
inline bool sourceSkin(uint32_t kind){return kind==1||kind==3||kind==5||kind==6||kind==12;}
inline bool sourceCharacter(uint32_t kind){return (kind>=1&&kind<=7)||kind==12;}
inline uint32_t sourcePaletteRows(uint32_t kind){return sourceCharacter(kind)?sourceSkin(kind)?388:4:0;}
inline void sourceMaterial(const uint8_t* d,uint32_t kind,DbzNativeFrameDraw& draw,std::vector<float>& palette){
 float c[40][4];for(int j=0;j<40;++j)for(int k=0;k<4;++k)c[j][k]=sourceFloat(d+1920+j*16+k*4);
 auto ps=[&](int j,int k=0){return sourceFloat(d+6016+j*16+k*4);};
 auto* matrix=draw.view_projection;auto* m=draw.material;
 std::fill_n(matrix,16,0.f);std::fill_n(m,20,0.f);
 for(int row=0;row<4;++row)for(int col=0;col<4;++col)for(int k=0;k<4;++k)matrix[col*4+row]+=c[row+8][k]*c[k][col];
 m[0]=c[13][0];m[1]=c[13][1];m[2]=ps(3,3)!=0?1.f:0.f;m[3]=1;
 for(int k=0;k<4;++k){m[4+k]=c[12][k];m[8+k]=c[14][k];m[12+k]=ps(2,k);m[16+k]=ps(3,k);}
 if(sourceCharacter(kind)){
  bool skin=sourceSkin(kind),outline=kind==3||kind==4,mask=kind==6||kind==7,colored=kind==5||kind==12;
  for(int row=0;row<4;++row)for(int col=0;col<4;++col)matrix[col*4+row]=c[row][col];
  m[3]=mask?(skin?11.f:12.f):outline?(skin?7.f:8.f):colored?10.f:skin?4.f:6.f;
  for(int k=0;k<3;++k){m[8+k]=ps(4,k);m[12+k]=ps(14,k);}m[11]=c[39][0];m[15]=ps(1);
  if(kind==1||kind==2){m[19]=ps(14,3);draw.flags|=DBZ_NATIVE_TOON_CAMERA_W;}
  if(outline){std::fill_n(m,20,0.f);m[3]=skin?7.f:8.f;m[4]=c[16][0];for(int k=0;k<4;++k)m[8+k]=c[17][k];}
  if(colored){m[3]=10;for(int k=0;k<4;++k){m[8+k]=c[14][k];m[12+k]=ps(2,k);}}
  if(mask){std::fill_n(m,20,0.f);m[3]=skin?11.f:12.f;m[8]=ps(255);}
  palette.assign(sourcePaletteRows(kind)*4,0.f);for(int row=0;row<4;++row)std::copy_n(c[row+8],4,palette.data()+row*4);
 }
 if(kind==8){std::fill_n(m,20,0.f);m[3]=2;for(int k=0;k<4;++k){m[8+k]=c[14][k];m[12+k]=ps(2,k);}}
 if(kind==9||kind==12||kind==14)m[0]=m[1]=0;
 if(kind==12||kind==14)std::fill_n(m+12,4,0.f); // Flat-color PS has no texture or blend-color instruction.
 for(float v:draw.view_projection)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source matrix");
 for(float v:draw.material)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source material");
}
inline uint32_t sourceVertex(const uint8_t* p,const uint8_t* d,uint32_t kind,const DbzNativeFrameDraw& draw,DbzNativeFrameVertex& v){
 for(int k=0;k<3;++k){v.position[k]=sourceFloat(p+k*4);v.normal[k]=0;}
 v.uv[0]=sourceFloat(p+36);v.uv[1]=sourceFloat(p+40);v.color[0]=p[33]/255.f;v.color[1]=p[34]/255.f;v.color[2]=p[35]/255.f;v.color[3]=p[32]/255.f;
 uint32_t bone=0;v.fog=1;
 if(sourceCharacter(kind)){
  bool skin=sourceSkin(kind);bone=skin?p[19]:0;float weight=skin?sourceFloat(p+12):0;
  if(bone>127||!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Invalid source bone or weight");
  if(kind!=6&&kind!=7)for(int k=0;k<3;++k)v.normal[k]=sourceFloat(p+20+k*4);
  if(kind==5||kind==12){v.normal[0]=float(bone);v.normal[1]=weight;v.normal[2]=0;}
  else{v.color[0]=float(bone);v.color[1]=weight;v.color[2]=v.color[3]=0;}
 }else if(kind==8){
  float world[4]={};for(int k=0;k<4;++k){world[k]=sourceFloat(d+1920+11*16+k*4);for(int axis=0;axis<3;++axis)world[k]+=v.position[axis]*sourceFloat(d+1920+(8+axis)*16+k*4);}
  for(int k=0;k<2;++k){v.uv[k]=0;for(int axis=0;axis<4;++axis)v.uv[k]+=world[axis]*sourceFloat(d+1920+(18+axis)*16+k*4);}
 }else{
  const auto* matrix=draw.view_projection;const auto* fog=draw.material+4;
  float z=matrix[8]*v.position[0]+matrix[9]*v.position[1]+matrix[10]*v.position[2]+matrix[11];
  v.fog=std::min(std::max(std::clamp((fog[0]-z)*fog[1],0.f,1.f),1-fog[3]),1-fog[2]);
  if(kind==9||kind==14)v.uv[0]=v.uv[1]=0;
 }
 if(kind==12)v.uv[0]=v.uv[1]=0;
 float values[13];std::memcpy(values,&v,sizeof(values));for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Nonfinite source vertex");
 return bone;
}
}
