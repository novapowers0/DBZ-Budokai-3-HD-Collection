// Test-only C entry points. This library links no game runtime or reference GPU.
#include "native_indexed.h"
#include "live_raster.h"
#include "source_texture.h"
#include "native_stream.h"
#include "source_sprites.h"
extern "C" __declspec(dllexport) void replay_draw(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base){dbzlab::worldObserve(device,primitive,vertexBase,indexStart,count,base);}
extern "C" __declspec(dllexport) void replay_present(){dbzlab::worldPresent();}
extern "C" __declspec(dllexport) void replay_shutdown(){dbzlab::worldShutdown();}
extern "C" __declspec(dllexport) int replay_raster(const uint8_t* device,DbzNativeRaster* raster){try{*raster=dbzlab::nativeRaster(device);return 1;}catch(...){return 0;}}
extern "C" __declspec(dllexport) uint32_t replay_linearize(const uint8_t* raw,uint32_t size,const uint32_t* fetch,uint8_t* output,uint32_t capacity){try{auto layout=dbzlab::sourceTextureLayout(fetch);auto data=dbzlab::linearizeSourceTexture(raw,size,layout);if(data.size()>capacity||!output)return 0;std::memcpy(output,data.data(),data.size());return uint32_t(data.size());}catch(...){return 0;}}
extern "C" __declspec(dllexport) void replay_stream_draw(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t start,uint32_t count,uint8_t* base){dbzlab::streamObserve(device,primitive,vertexBase,start,count,base);}
extern "C" __declspec(dllexport) void replay_stream_up(uint32_t device,uint32_t primitive,uint32_t count,uint32_t vertices,uint32_t stride,uint8_t* base){dbzlab::streamObserveUp(device,primitive,count,vertices,stride,base);}
extern "C" __declspec(dllexport) void replay_stream_resolve(uint32_t device,uint32_t flags,uint32_t destination,uint8_t* base){dbzlab::streamResolve(device,flags,destination,base);}
extern "C" __declspec(dllexport) void replay_stream_resolve_rect(uint32_t device,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t sourceRect,uint32_t destinationPoint){dbzlab::streamResolve(device,flags,destination,base,sourceRect,destinationPoint);}
extern "C" __declspec(dllexport) int replay_sprite(const uint8_t* vertices,uint32_t count,const uint8_t* device,uint32_t kind,DbzNativeFrameVertex* output,DbzNativeFrameDraw* draw){try{*draw={};dbzlab::sourceSpriteMaterial(device,kind,*draw);for(uint32_t i=0;i<count;++i)dbzlab::sourceSpriteVertex(vertices+i*(kind==15?16:24),device,kind,*draw,output[i]);return 1;}catch(...){return 0;}}
extern "C" __declspec(dllexport) void replay_stream_present(){dbzlab::streamPresent();}
extern "C" __declspec(dllexport) void replay_stream_shutdown(){dbzlab::streamShutdown();}
extern "C" __declspec(dllexport) void replay_stream_status(){dbzlab::streamStatus();}
extern "C" __declspec(dllexport) int replay_stream_capture(const char* path){return dbzlab::streamCapture(path)?1:0;}
