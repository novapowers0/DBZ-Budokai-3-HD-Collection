// Checked screen-space color postprocess intent, independent of guest packets.
#pragma once
#include "source_geometry.h"
namespace dbzlab {
inline void sourcePostProjection(DbzNativeFrameDraw& draw,uint32_t width,uint32_t height){
 std::fill_n(draw.view_projection,16,0.f);auto* matrix=draw.view_projection;
 matrix[0]=2.f/width;matrix[3]=-1;matrix[5]=-2.f/height;matrix[7]=1;matrix[10]=matrix[15]=1;
 draw.raster.viewport[0]=draw.raster.viewport[1]=0;draw.raster.viewport[2]=float(width);draw.raster.viewport[3]=float(height);draw.raster.viewport[4]=0;draw.raster.viewport[5]=1;
}
inline void sourceFxaaMaterial(const uint8_t* d,DbzNativeFrameDraw& draw,uint32_t width,uint32_t height){
 sourcePostProjection(draw,width,height);
 std::fill_n(draw.material,20,0.f);auto* m=draw.material;m[3]=DBZ_NATIVE_FXAA;
 m[4]=sourceFloat(d+1920);m[5]=sourceFloat(d+1924);m[12]=sourceFloat(d+6016);m[13]=sourceFloat(d+6020);
 m[6]=((sourceWord(d+0x480)>>10)&7)==2?1.f:0.f;
 for(int k=0;k<4;++k)m[8+k]=1;
 for(int k:{4,5,12,13})if(!std::isfinite(m[k])||m[k]<0||m[k]>1)throw std::runtime_error("Invalid FXAA sample positioning");
 if(m[12]==0||m[13]==0)throw std::runtime_error("Invalid FXAA pixel step");
 draw.flags|=DBZ_NATIVE_SAMPLE_SCENE;
}
inline DbzNativeFrameVertex sourcePostVertex(const uint8_t* p){
 DbzNativeFrameVertex v{};v.position[0]=sourceFloat(p);v.position[1]=sourceFloat(p+4);v.uv[0]=sourceFloat(p+8);v.uv[1]=sourceFloat(p+12);v.fog=1;
 std::fill_n(v.color,4,1.f);for(float value:{v.position[0],v.position[1],v.uv[0],v.uv[1]})if(!std::isfinite(value))throw std::runtime_error("Nonfinite postprocess vertex");return v;
}
inline DbzNativeFrameVertex sourceClipPostVertex(const uint8_t* p){
 DbzNativeFrameVertex v{};for(int k=0;k<3;++k)v.position[k]=sourceFloat(p+k*4);for(int k=0;k<2;++k)v.uv[k]=sourceFloat(p+12+k*4);v.fog=1;std::fill_n(v.color,4,1.f);
 for(float value:{v.position[0],v.position[1],v.position[2],v.uv[0],v.uv[1]})if(!std::isfinite(value))throw std::runtime_error("Nonfinite postprocess vertex");return v;
}
inline void sourceCopyMaterial(const uint8_t* d,DbzNativeFrameDraw& draw){
 std::fill_n(draw.material,20,0.f);draw.material[3]=DBZ_NATIVE_IMAGE_COPY;for(int k=8;k<12;++k)draw.material[k]=1;
 uint32_t f0=sourceWord(d+0x480),f3=sourceWord(d+0x48c),clampU=(f0>>10)&7,clampV=(f0>>13)&7,mag=(f3>>19)&3,min=(f3>>21)&3;
 if(clampU>2||clampV>2||mag!=min||mag>1||(f3&1)||((f3>>13)&63))throw std::runtime_error("Unsupported native copy sampler");
 draw.material[4]=mag==0?1.f:0.f;draw.material[5]=clampU==1?2.f:clampU==2?1.f:0.f;
 draw.material[6]=clampV==1?3.f:clampV==2?2.f:1.f; // zero retains legacy V=U
}
inline void sourcePostPalette(const uint8_t* d,uint32_t kind,DbzNativeFrameDraw& draw,std::vector<float>& palette){
 if(kind==19){
  draw.material[3]=DBZ_NATIVE_GAUSSIAN_13;palette.assign(52,0.f);for(int row=0;row<13;++row)for(int k=0;k<3;++k)palette[row*4+k]=sourceFloat(d+6016+row*16+k*4);draw.palette_rows=13;
 }else if(kind==20){
  draw.material[3]=DBZ_NATIVE_DEPTH_OF_FIELD_9;
  palette={0,0,0,0,.1111111119389534f,0,-2,0,.0020833334419876337f,-.004166666883975267f,.004166666883975267f,-.0020833334419876337f,.0011718750465661287f,-.0011718750465661287f,-.0023437500931322575f,.0023437500931322575f};
  for(int k=0;k<4;++k)palette[k]=sourceFloat(d+6032+k*4);if(palette[0]<=0||palette[1]<=palette[0]||palette[3]<=0)throw std::runtime_error("Invalid depth-of-field focus parameters");draw.palette_rows=4;
 }
 for(float value:palette)if(!std::isfinite(value))throw std::runtime_error("Nonfinite postprocess parameters");
}
}
