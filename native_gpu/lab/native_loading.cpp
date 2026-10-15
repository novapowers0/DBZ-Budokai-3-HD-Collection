// Narrow, opt-in native bridge for the captured loading-screen sprite pair.
// Original guest calls still execute, maintaining the reference path and IRQs.
// The native draw consumes CPU vertices, colors, UVs, camera and cached assets.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#include <rex/ppc/context.h>
#include "native_loading.h"
#include "../native_gpu.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace dbzlab { namespace {
#pragma pack(push,1)
struct Entry {uint64_t hash;uint32_t bytes,width,height,format,first,count,flags;};
#pragma pack(pop)
static_assert(sizeof(Entry)==36);
struct FpuScope {unsigned saved=_mm_getcsr();FpuScope(){_mm_setcsr(0x1f80);}~FpuScope(){_mm_setcsr(saved);}};
uint64_t hashBytes(const void* data,size_t size){uint64_t h=14695981039346656037ull;auto* p=static_cast<const uint8_t*>(data);for(size_t i=0;i<size;++i){h^=p[i];h*=1099511628211ull;}return h;}
bool read(const void* ptr,void* target,size_t count){SIZE_T done=0;return ReadProcessMemory(GetCurrentProcess(),ptr,target,count,&done)&&done==count;}
uint32_t be32(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return _byteswap_ulong(v);}
float beFloat(const uint8_t* p){return std::bit_cast<float>(be32(p));}
const uint8_t* address(uint8_t* base,uint32_t p){return base+p+(p>=0xe0000000u?0x1000u:0u);}
uint32_t pipeline(const uint8_t* d){
 uint32_t z=be32(d+10548),b=be32(d+10552),r=be32(d+10568),mask=be32(d+10460)&15;
 if(z&1)throw std::runtime_error("Loading stencil unsupported");
 uint32_t src=b&31,dst=(b>>8)&31,op=(b>>5)&7,mode=0;
 if(op==0&&src==1&&dst==0)mode=0;else if(op==0&&src==6&&dst==7)mode=1;
 else if(op==0&&src==1&&dst==1)mode=2;else if(op==0&&src==8&&dst==0)mode=3;
 else throw std::runtime_error("Loading blend unsupported");
 uint32_t cull=r&3;if(cull==3)throw std::runtime_error("Loading cull unsupported");
 if(r&4)cull=((cull&1)<<1)|((cull&2)>>1);
 uint32_t flags=((z>>1)&3)|(cull<<2)|(mode<<4)|(((z>>4)&7)<<8)|(mask<<16);
 if(((b>>16)&31)==1&&((b>>24)&31)==1&&((b>>21)&7)==0)flags|=0x800;
 return flags;
}
struct State {
 HMODULE module=nullptr;DbzNativeRenderer renderer=nullptr;
 decltype(&dbz_native_create) create=nullptr;decltype(&dbz_native_destroy) destroy=nullptr;
 decltype(&dbz_native_update_frame_vertices) updateVertices=nullptr;decltype(&dbz_native_update_draw) updateDraw=nullptr;
 decltype(&dbz_native_render) render=nullptr;decltype(&dbz_native_capture) capture=nullptr;decltype(&dbz_native_last_error) lastError=nullptr;
 std::vector<Entry> entries;std::vector<DbzNativeFrameVertex> vertices;std::vector<bool> seen;
 uint64_t frames=0,changes=0,matches=0,previous=0,skipped=0;bool failed=false;std::string failure;
 State(){
  const char* scene=std::getenv("DBZ3_NATIVE_UI_SCENE");if(!scene||!*scene)return;
  const char* dll=std::getenv("DBZ3_NATIVE_RENDERER");const char* shader=std::getenv("DBZ3_NATIVE_SHADERS");
  if(!dll||!shader)throw std::runtime_error("Native loading bridge paths missing");
  std::filesystem::path path=scene;path.replace_extension(".bridge");std::ifstream f(path,std::ios::binary);
  char magic[8];uint32_t count=0;f.read(magic,8);f.read(reinterpret_cast<char*>(&count),4);
  if(!f||std::memcmp(magic,"DBZUI001",8)||!count||count>64)throw std::runtime_error("Invalid loading bridge manifest");
  entries.resize(count);f.read(reinterpret_cast<char*>(entries.data()),count*sizeof(Entry));if(!f||f.peek()!=EOF)throw std::runtime_error("Truncated loading bridge manifest");
  size_t capacity=0;for(const auto& e:entries){if(!e.count||e.count>10000||e.first>1000000||e.bytes>16*1024*1024)throw std::runtime_error("Loading manifest exceeds limits");capacity=std::max(capacity,size_t(e.first)+e.count);}
  vertices.resize(capacity);seen.resize(count);
  module=LoadLibraryW(std::filesystem::path(dll).c_str());if(!module)throw std::runtime_error("Native renderer DLL load failed");
  struct ModuleGuard {HMODULE& value;bool committed=false;~ModuleGuard(){if(!committed&&value){FreeLibrary(value);value=nullptr;}}} moduleGuard{module};
#define GET(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(module,name));if(!member)throw std::runtime_error("Native renderer ABI missing " name)
  GET(create,"dbz_native_create");GET(destroy,"dbz_native_destroy");GET(updateVertices,"dbz_native_update_frame_vertices");GET(updateDraw,"dbz_native_update_draw");GET(render,"dbz_native_render");GET(capture,"dbz_native_capture");GET(lastError,"dbz_native_last_error");
#undef GET
  FpuScope fpu;renderer=create(nullptr,scene,shader);if(!renderer)throw std::runtime_error(lastError());
  moduleGuard.committed=true;
 }
 ~State(){if(renderer&&destroy)destroy(renderer);if(module)FreeLibrary(module);}
 void check(int result){if(!result)throw std::runtime_error(lastError());}
 void status(const char* error=""){
  if(*error)failure=error;
  std::string text=failure;std::replace(text.begin(),text.end(),'"','\'');std::replace(text.begin(),text.end(),'\\','/');
  for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';
  std::ofstream f("native-loading-status.json");f<<"{\"scope\":\"Native loading sprites only; reference renderer remains active\",\"native_frames\":"<<frames<<",\"changed_geometry_frames\":"<<changes<<",\"texture_matches\":"<<matches<<",\"unsupported_pipeline_draws\":"<<skipped<<",\"error\":\""<<text<<"\"}\n";
 }
 void observe(PPCContext& ctx,uint8_t* base){
  if(!renderer||failed||ctx.r4.u32!=4||ctx.r7.u32!=24)return;
  FpuScope fpu;
  std::array<uint8_t,0x6400> device;if(!read(address(base,ctx.r3.u32),device.data(),device.size()))return;
  const auto* d=device.data();uint32_t f0=be32(d+0x480),f1=be32(d+0x484),f2=be32(d+0x488);
  if((f0&3)!=2)return;uint32_t format=f1&63,w=(f2&8191)+1,h=((f2>>13)&8191)+1;
  for(size_t i=0;i<entries.size();++i){auto& e=entries[i];if(e.width!=w||e.height!=h||e.format!=format||e.count!=ctx.r5.u32)continue;
   std::vector<uint8_t> texture(e.bytes);uint32_t physical=f1&0xfffff000u;
   if(uint64_t(physical)+e.bytes>0x20000000||!read(base+0x100000000ull+physical,texture.data(),texture.size())||hashBytes(texture.data(),texture.size())!=e.hash)continue;
   if(pipeline(d)!=e.flags){++skipped;return;}
   float c[16];for(int j=0;j<16;++j)c[j]=beFloat(d+1920+j*4);
   if(!(c[2]>=1&&c[2]<=2&&c[3]>65000&&c[0]>0&&c[5]<0))return;
   if(std::abs(c[1])+std::abs(c[4])+std::abs(c[8])+std::abs(c[9])>1e-7)return;
   std::vector<uint8_t> raw(size_t(e.count)*24);if(!read(address(base,ctx.r6.u32),raw.data(),raw.size()))return;
   float matrix[16]={c[0],0,0,c[12],0,c[5],0,c[13],0,0,1,0,0,0,0,1};
   float fog[4],material[20]={};for(int j=0;j<4;++j)fog[j]=beFloat(d+1920+192+j*4);
   material[0]=beFloat(d+1920+208);material[1]=beFloat(d+1920+212);material[2]=beFloat(d+6016+60)!=0;material[3]=1;
   for(int j=0;j<4;++j){material[4+j]=fog[j];material[8+j]=1;material[12+j]=beFloat(d+6016+32+j*4);material[16+j]=beFloat(d+6016+48+j*4);}
   for(uint32_t j=0;j<e.count;++j){const auto* p=raw.data()+j*24;auto& v=vertices[e.first+j];
    v.position[0]=beFloat(p);v.position[1]=beFloat(p+4);float z=std::clamp(beFloat(p+8),c[2],c[3]);v.position[2]=(z*c[10]+c[14])/(z*c[11]+c[15]);
    v.normal[0]=v.normal[1]=v.normal[2]=0;v.uv[0]=beFloat(p+16);v.uv[1]=beFloat(p+20);
    v.color[0]=p[13]/255.f;v.color[1]=p[14]/255.f;v.color[2]=p[15]/255.f;v.color[3]=p[12]/255.f;
    float value=std::clamp((fog[0]-v.position[2])*fog[1],0.f,1.f);v.fog=std::min(std::max(value,1-fog[3]),1-fog[2]);
   }
   check(updateDraw(renderer,uint32_t(i),matrix,material));seen[i]=true;++matches;break;
  }
 }
 void present(){
  if(!renderer||failed)return;
  if(std::all_of(seen.begin(),seen.end(),[](bool v){return v;})){
   FpuScope fpu;uint64_t current=hashBytes(vertices.data(),vertices.size()*sizeof(vertices[0]));
   if(frames&&current!=previous)++changes;previous=current;
   check(updateVertices(renderer,vertices.data(),uint32_t(vertices.size())));check(render(renderer));++frames;
   if(frames==1||frames==15||frames==30||frames==60){std::string name="native-loading-"+std::to_string(frames)+".ppm";check(capture(renderer,name.c_str()));status();}
  }
  std::fill(seen.begin(),seen.end(),false);
 }
};
std::mutex guard;std::unique_ptr<State> state;bool initialized=false,disabled=false;
void fail(const std::exception& error){if(state){state->failed=true;state->status(error.what());}else std::ofstream("native-loading-error.txt")<<error.what()<<'\n';disabled=true;}
} // namespace
void shadowObserve(PPCContext& ctx,uint8_t* base) noexcept {std::lock_guard lock(guard);if(disabled)return;try{if(!initialized){initialized=true;state=std::make_unique<State>();}state->observe(ctx,base);}catch(const std::exception& e){fail(e);}}
void shadowPresent() noexcept {std::lock_guard lock(guard);if(!state||disabled)return;try{state->present();}catch(const std::exception& e){fail(e);}}
void shadowShutdown() noexcept {std::lock_guard lock(guard);if(state){state->status();state.reset();}disabled=true;}
}
