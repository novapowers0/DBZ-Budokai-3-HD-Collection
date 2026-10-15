// Guarded, opt-in native world component. Reference submission still executes.
// Runtime shader bytes identify a checked material family; they are not compiled
// or interpreted by the host renderer. Positions/colors/UVs come from CPU data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#include "native_indexed.h"
#include "texture_identity.h"
#include "../native_gpu.h"
#include "live_raster.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace dbzlab {namespace {
struct FpuScope {unsigned saved=_mm_getcsr();FpuScope(){_mm_setcsr(0x1f80);}~FpuScope(){_mm_setcsr(saved);}};
bool read(const void* p,void* out,size_t n){SIZE_T done=0;return ReadProcessMemory(GetCurrentProcess(),p,out,n,&done)&&done==n;}
uint32_t be32(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return _byteswap_ulong(v);}
float number(const uint8_t* p){return std::bit_cast<float>(be32(p));}
const uint8_t* guest(uint8_t* base,uint32_t p){return base+p+(p>=0xe0000000u?0x1000u:0u);}
uint32_t word(uint8_t* base,uint32_t p){uint8_t b[4];if(!read(guest(base,p),b,4))throw std::runtime_error("World guest read failed");return be32(b);}
uint64_t hash(const void* data,size_t n){uint64_t h=14695981039346656037ull;auto* p=static_cast<const uint8_t*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;}
uint32_t pipeline(const uint8_t* d,bool raster=false){
 uint32_t z=be32(d+10548),b=be32(d+10552),r=be32(d+10568),mask=be32(d+10460)&15;
 if((z&1)&&!raster)throw std::runtime_error("World stencil unsupported");
 uint32_t src=b&31,dst=(b>>8)&31,op=(b>>5)&7,mode=0;
 if(op==0&&src==1&&dst==0)mode=0;else if(op==0&&src==6&&dst==7)mode=1;
 else if(op==0&&src==1&&dst==1)mode=2;else if(op==0&&src==8&&dst==0)mode=3;
 else if(op==0&&src==6&&dst==1)mode=4;
 else throw std::runtime_error("World blend unsupported");
 uint32_t cull=r&3;if(cull==3)throw std::runtime_error("World cull unsupported");if(r&4)cull=((cull&1)<<1)|((cull&2)>>1);
 uint32_t flags=((z>>1)&3)|(cull<<2)|(mode<<4)|(((z>>4)&7)<<8)|(mask<<16);
 auto a=(b>>16)&31,ad=(b>>24)&31,ao=(b>>21)&7;
 if(a==1&&ad==1&&ao==0)flags|=0x800;
 else if(!(a==1&&(ad==7||ad==0)&&ao==0))throw std::runtime_error("World alpha blend unsupported");
 if(raster&&ad==0&&(mode==1||mode==2||mode==4))flags|=DBZ_NATIVE_ALPHA_DEST_ZERO;
 if(raster&&ad==7&&mode!=1)flags|=DBZ_NATIVE_ALPHA_DEST_INVERSE;
 return flags;
}
#pragma pack(push,1)
struct Entry {uint64_t vs,ps,indices,texture;uint32_t primitive,stride,indexWidth,indexCount,first,count,flags,textureBytes,width,height,format,vsBytes,psBytes;};
struct Extra {uint64_t texture;uint32_t textureBytes,width,height,format,kind;};
#pragma pack(pop)
static_assert(sizeof(Entry)==84);
static_assert(sizeof(Extra)==28);
struct Draw {Entry info;Extra extra{};DbzNativeRaster raster{};std::vector<uint32_t> source;bool seen=false;};
struct State {
 HMODULE module=nullptr;DbzNativeRenderer renderer=nullptr;
 decltype(&dbz_native_create) create=nullptr;decltype(&dbz_native_destroy) destroy=nullptr;
 decltype(&dbz_native_update_frame_vertices) updateVertices=nullptr;decltype(&dbz_native_update_draw) updateDraw=nullptr;
 decltype(&dbz_native_render) render=nullptr;decltype(&dbz_native_capture) capture=nullptr;decltype(&dbz_native_last_error) lastError=nullptr;
 decltype(&dbz_native_set_draw_visible) setVisible=nullptr;
 decltype(&dbz_native_update_palette) updatePalette=nullptr;
 decltype(&dbz_native_update_raster) updateRaster=nullptr;bool version4=false;
 std::vector<Draw> draws;std::vector<DbzNativeFrameVertex> vertices;std::vector<std::array<float,16>> matrices;
 std::unordered_map<uint64_t,uint64_t> textureHashes;
 std::vector<std::vector<float>> palettes;
 uint32_t vbAddress=0;std::vector<uint8_t> vb;
 uint64_t frames=0,changed=0,previous=0,matches=0,partial=0,maxMatched=0;
 uint64_t skinnedFrames=0,skinnedChanged=0,skinnedMatches=0,maxSkinned=0,previousPose=0;
 uint64_t observed=0,candidates=0,indexMatches=0,shaderMatches=0,textureMatches=0,presents=0;std::string failure;
 State(){
  const char* scene=std::getenv("DBZ3_NATIVE_WORLD_SCENE");if(!scene||!*scene)return;
  const char* dll=std::getenv("DBZ3_NATIVE_RENDERER");const char* shader=std::getenv("DBZ3_NATIVE_SHADERS");if(!dll||!shader)throw std::runtime_error("World bridge paths missing");
  std::filesystem::path path=scene;path.replace_extension(".indexed");std::ifstream f(path,std::ios::binary);char magic[8];uint32_t count=0;f.read(magic,8);f.read(reinterpret_cast<char*>(&count),4);
  version4=std::memcmp(magic,"DBZIDX04",8)==0;bool version3=version4||std::memcmp(magic,"DBZIDX03",8)==0;
  if(!f||(!version3&&std::memcmp(magic,"DBZIDX02",8))||!count||count>10000)throw std::runtime_error("Invalid world manifest");
  size_t capacity=0;draws.resize(count);for(auto& d:draws){auto& e=d.info;f.read(reinterpret_cast<char*>(&e),sizeof(e));
   if(!f||!e.count||e.count>1000000||e.first>1000000-e.count||e.stride!=44||e.indexWidth!=2||!e.indexCount||e.indexCount>1000000||e.vsBytes>65536||e.psBytes>65536||e.textureBytes>16*1024*1024)throw std::runtime_error("World manifest limits exceeded");
   if(version3){f.read(reinterpret_cast<char*>(&d.extra),sizeof(d.extra));auto& x=d.extra;
    if(x.kind>(version4?9u:5u)||x.textureBytes>16*1024*1024||x.width>8192||x.height>8192||((x.kind==1||x.kind==2)&&(!x.textureBytes||!x.width||!x.height)))throw std::runtime_error("Invalid character manifest");
   }
   if(version4)f.read(reinterpret_cast<char*>(&d.raster),sizeof(d.raster));
   d.source.resize(e.count);f.read(reinterpret_cast<char*>(d.source.data()),e.count*4);capacity=std::max(capacity,size_t(e.first)+e.count);
  }if(!f||f.peek()!=EOF)throw std::runtime_error("Truncated world manifest");vertices.resize(capacity);matrices.resize(count);palettes.resize(count);
  module=LoadLibraryW(std::filesystem::path(dll).c_str());if(!module)throw std::runtime_error("World renderer DLL load failed");
  struct ModuleGuard {HMODULE& m;bool ok=false;~ModuleGuard(){if(!ok&&m){FreeLibrary(m);m=nullptr;}}} guard{module};
#define GET(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(module,name));if(!member)throw std::runtime_error("World renderer ABI missing " name)
  GET(create,"dbz_native_create");GET(destroy,"dbz_native_destroy");GET(updateVertices,"dbz_native_update_frame_vertices");GET(updateDraw,"dbz_native_update_draw");GET(render,"dbz_native_render");GET(capture,"dbz_native_capture");GET(lastError,"dbz_native_last_error");
  GET(setVisible,"dbz_native_set_draw_visible");
  if(version3){GET(updatePalette,"dbz_native_update_palette");}
  if(version4){GET(updateRaster,"dbz_native_update_raster");}
#undef GET
  FpuScope fpu;renderer=create(nullptr,scene,shader);if(!renderer)throw std::runtime_error(lastError());guard.ok=true;
 }
 ~State(){FpuScope fpu;if(renderer&&destroy)destroy(renderer);if(module)FreeLibrary(module);}
 void check(int result){if(!result)throw std::runtime_error(lastError());}
 void status(){std::string error=failure;for(auto& c:error){if(c=='"')c='\'';else if(c=='\\')c='/';else if(static_cast<unsigned char>(c)<32)c=' ';}
  std::ofstream f("native-world-status.json");f<<"{\"scope\":\"Partial native world / optional skinned component; reference GPU remains active\",\"native_frames\":"<<frames<<",\"changed_geometry_or_camera_frames\":"<<changed<<",\"matched_draws\":"<<matches<<",\"component_draws\":"<<draws.size()<<",\"max_draws_matched_in_frame\":"<<maxMatched<<",\"frames_with_partial_component\":"<<partial<<",\"skinned_frames\":"<<skinnedFrames<<",\"changed_pose_frames\":"<<skinnedChanged<<",\"matched_skinned_draws\":"<<skinnedMatches<<",\"max_skinned_draws_in_frame\":"<<maxSkinned<<",\"observed_indexed_draws\":"<<observed<<",\"count_candidates\":"<<candidates<<",\"index_matches\":"<<indexMatches<<",\"shader_matches\":"<<shaderMatches<<",\"texture_matches\":"<<textureMatches<<",\"error\":\""<<error<<"\"}\n";
 }
 uint64_t programHash(uint8_t* base,uint32_t object,bool vertex,uint32_t expected){
  if(object<0x10000)return 0;uint32_t variant=word(base,object+(vertex?896:64));if(variant>32768)return 0;
  uint32_t header=object+(vertex?872:40)+variant,offset=word(base,header),size=word(base,header+4),p=word(base,object+(vertex?32:24));
  if(size!=expected||!size||uint64_t(p)+offset+size>0x100000000ull)return 0;
  std::vector<uint8_t> data(size);return read(guest(base,p+offset),data.data(),size)?hash(data.data(),size):0;
 }
 bool textureMatchesAt(const uint8_t* d,uint8_t* base,uint32_t slot,uint64_t identity,uint32_t bytes,uint32_t width,uint32_t height,uint32_t format){
  uint32_t fetch[6];for(int k=0;k<6;++k)fetch[k]=be32(d+0x480+slot*24+k*4);
  uint32_t physical=fetch[1]&0xfffff000u;
  if((fetch[0]&3)!=2||(fetch[1]&63)!=format||(fetch[2]&8191)+1!=width||((fetch[2]>>13)&8191)+1!=height||uint64_t(physical)+bytes>0x20000000)return false;
  uint64_t key=hash(fetch,sizeof(fetch))^bytes;auto found=textureHashes.find(key);
  if(found==textureHashes.end()){std::vector<uint8_t> texture(bytes);if(!read(base+0x100000000ull+physical,texture.data(),texture.size()))return false;found=textureHashes.emplace(key,textureContentHash(texture.data(),texture.size(),fetch)).first;}
  return found->second==identity;
 }
 void observe(uint32_t dev,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base){
  ++observed;if(!renderer||count==0||count>1000000)return;FpuScope fpu;
  if(std::none_of(draws.begin(),draws.end(),[&](auto& d){return !d.seen&&d.info.primitive==primitive&&d.info.indexCount==count;}))return;
  ++candidates;
  std::array<uint8_t,0x6400> device;if(!read(guest(base,dev),device.data(),device.size()))return;auto* d=device.data();
  if(d[12904]*4!=44)return;uint32_t f0=be32(d+0x778),f1=be32(d+0x77c);if((f0&3)!=3||(f1&3)!=2)return;
  uint32_t vertexAddress=f0&0x1ffffffc,vertexBytes=((f1>>2)&0xffffff)*4;if(!vertexBytes||vertexBytes>16*1024*1024||uint64_t(vertexAddress)+vertexBytes>0x20000000)return;
  uint32_t ib=be32(d+12812),ibFlags=word(base,ib);if(ibFlags&0x80000000u)return;
  uint64_t ip=uint64_t(word(base,ib+24))+uint64_t(indexStart)*2;if(ip>=0x100000000ull)return;
  uint32_t indexAddress=(uint32_t(ip)&0x1fffffff)+(ip>=0xe0000000u?0x1000u:0u);if(uint64_t(indexAddress)+count*2>0x20000000)return;
  std::vector<uint8_t> indexData(size_t(count)*2);if(!read(base+0x100000000ull+indexAddress,indexData.data(),indexData.size()))return;uint64_t ih=hash(indexData.data(),indexData.size());
  for(size_t i=0;i<draws.size();++i){auto& draw=draws[i];auto& e=draw.info;if(draw.seen||e.primitive!=primitive||e.indexCount!=count||e.indices!=ih)continue;
   ++indexMatches;
   if(programHash(base,be32(d+13072),true,e.vsBytes)!=e.vs||programHash(base,be32(d+13068),false,e.psBytes)!=e.ps)continue;
   ++shaderMatches;
   const auto& x=draw.extra;bool character=x.kind>=1&&x.kind<=7,skinned=x.kind==1||x.kind==3||x.kind==5||x.kind==6,outline=x.kind==3||x.kind==4,colored=x.kind==5,mask=x.kind==6||x.kind==7,projected=x.kind==8,effect=x.kind==9;
   if(!outline&&!mask&&!projected&&!effect&&!textureMatchesAt(d,base,0,e.texture,e.textureBytes,e.width,e.height,e.format))continue;
   if((x.kind==1||x.kind==2)&&!textureMatchesAt(d,base,1,x.texture,x.textureBytes,x.width,x.height,x.format))continue;
   ++textureMatches;
   DbzNativeRaster raster{};
   try {
    uint32_t flags=pipeline(d,version4)|(mask?DBZ_NATIVE_TARGET_MASK:0)|(projected?DBZ_NATIVE_SAMPLE_MASK:0);if(flags!=e.flags)continue;
    if(version4){
     raster=nativeRaster(d);if(!sameStencilPipeline(raster.stencil,draw.raster.stencil))continue;
     uint32_t pitch=be32(d+10368)&0x3fff;if(pitch!=(mask?560u:1280u))continue;
    }
   }catch(const std::runtime_error&){continue;}
   if(projected){uint32_t f0=be32(d+0x480),f1=be32(d+0x484),f2=be32(d+0x488),f3=be32(d+0x48c);if((f1&63)!=6||(f2&8191)+1!=512||((f2>>13)&8191)+1!=512||(f1&0xfffff000)!=be32(d+10780)||((f0>>10)&7)!=2||((f0>>13)&7)!=2||((f3>>19)&3)||((f3>>21)&3))continue;}
   if(vbAddress!=vertexAddress||vb.size()!=vertexBytes){vb.resize(vertexBytes);if(!read(base+0x100000000ull+vertexAddress,vb.data(),vb.size())){vb.clear();return;}vbAddress=vertexAddress;}
   float c[16][4];for(int j=0;j<16;++j)for(int k=0;k<4;++k)c[j][k]=number(d+1920+j*16+k*4);
   float matrix[16]={};for(int row=0;row<4;++row)for(int col=0;col<4;++col)for(int k=0;k<4;++k)matrix[col*4+row]+=c[row+8][k]*c[k][col];
   float material[20]={c[13][0],c[13][1],number(d+6016+60)!=0.f?1.f:0.f,1};
   for(int k=0;k<4;++k){material[4+k]=c[12][k];material[8+k]=c[14][k];material[12+k]=number(d+6016+32+k*4);material[16+k]=number(d+6016+48+k*4);}
   std::array<bool,128> used{};
   if(character){
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)matrix[col*4+row]=c[row][col];
    material[3]=mask?(skinned?11.f:12.f):outline?(skinned?7.f:8.f):colored?10.f:skinned?4.f:6.f;
    for(int k=0;k<3;++k){material[8+k]=number(d+6016+64+k*4);material[12+k]=number(d+6016+224+k*4);}
    material[11]=number(d+1920+39*16);material[15]=number(d+6016+16);
    if(outline){std::fill_n(material,20,0.f);material[3]=skinned?7.f:8.f;material[4]=number(d+1920+16*16);for(int k=0;k<4;++k)material[8+k]=number(d+1920+17*16+k*4);}
    if(colored){material[3]=10;for(int k=0;k<4;++k){material[8+k]=c[14][k];material[12+k]=number(d+6016+32+k*4);}}
    if(mask){std::fill_n(material,20,0.f);material[3]=skinned?11.f:12.f;material[8]=number(d+6016+255*16);}
   }
   if(projected){std::fill_n(material,20,0.f);material[3]=2;for(int k=0;k<4;++k){material[8+k]=c[14][k];material[12+k]=number(d+6016+32+k*4);}}
   for(uint32_t j=0;j<e.count;++j){int64_t index=int64_t(draw.source[j])+vertexBase;if(index<0||uint64_t(index)*44+44>vb.size())return;
    const uint8_t* p=vb.data()+index*44;auto& v=vertices[e.first+j];for(int k=0;k<3;++k){v.position[k]=number(p+k*4);v.normal[k]=0;}v.uv[0]=number(p+36);v.uv[1]=number(p+40);
    v.color[0]=p[33]/255.f;v.color[1]=p[34]/255.f;v.color[2]=p[35]/255.f;v.color[3]=p[32]/255.f;
    if(character){
     uint32_t bone=skinned?p[19]:0;float weight=skinned?number(p+12):0;if(bone>127||!std::isfinite(weight)||weight<0||weight>1)return;used[bone]=true;
     for(int k=0;k<3;++k)v.normal[k]=number(p+20+k*4);
     if(colored){v.normal[0]=float(bone);v.normal[1]=weight;v.normal[2]=0;}
     else {v.color[0]=float(bone);v.color[1]=weight;v.color[2]=v.color[3]=0;}
     v.fog=1;continue;
    }
    if(projected){
     float world[4]={};for(int k=0;k<4;++k){world[k]=c[11][k];for(int axis=0;axis<3;++axis)world[k]+=v.position[axis]*c[8+axis][k];}
     for(int k=0;k<2;++k){v.uv[k]=0;for(int axis=0;axis<4;++axis)v.uv[k]+=world[axis]*number(d+1920+(18+axis)*16+k*4);}
     v.fog=1;continue;
    }
    float z=matrix[8]*v.position[0]+matrix[9]*v.position[1]+matrix[10]*v.position[2]+matrix[11];
    v.fog=std::min(std::max(std::clamp((c[12][0]-z)*c[12][1],0.f,1.f),1-c[12][3]),1-c[12][2]);
   }
   if(character){
    auto& palette=palettes[i];palette.assign((skinned?388:4)*4,0);
    for(int row=0;row<4;++row)std::copy_n(c[row+8],4,palette.data()+row*4);
    if(skinned){
    uint32_t p0=be32(d+0x770),p1=be32(d+0x774),address=p0&0x1ffffffc;
    if((p0&3)!=3||(p1&3)!=2||((p1>>2)&0xffffff)*4!=6144||uint64_t(address)+6144>0x20000000)return;
    std::array<uint8_t,6144> bytes;if(!read(base+0x100000000ull+address,bytes.data(),bytes.size()))return;
    for(uint32_t bone=0;bone<128;++bone)if(used[bone])for(int k=0;k<12;++k)palette[16+bone*12+k]=number(bytes.data()+bone*48+k*4);
    }
    check(updatePalette(renderer,uint32_t(i),palette.data(),uint32_t(palette.size()/4)));
   }
   if(version4)check(updateRaster(renderer,uint32_t(i),&raster));
   check(updateDraw(renderer,uint32_t(i),matrix,material));std::copy_n(matrix,16,matrices[i].begin());draw.seen=true;++matches;if(skinned)++skinnedMatches;break;
  }
 }
 void present(){
  if(!renderer)return;FpuScope fpu;++presents;uint64_t seen=std::count_if(draws.begin(),draws.end(),[](auto& d){return d.seen;});maxMatched=std::max(maxMatched,seen);
  for(size_t i=0;i<draws.size();++i)check(setVisible(renderer,uint32_t(i),draws[i].seen?1:0));
  if(seen){
   uint64_t pose=0,skin=0;for(size_t i=0;i<draws.size();++i)if(draws[i].seen&&(draws[i].extra.kind==1||draws[i].extra.kind==3||draws[i].extra.kind==5||draws[i].extra.kind==6)){++skin;pose^=hash(palettes[i].data(),palettes[i].size()*4)*(i*2+1);}
   maxSkinned=std::max(maxSkinned,skin);if(skin){if(skinnedFrames&&pose!=previousPose)++skinnedChanged;previousPose=pose;++skinnedFrames;}
   if(seen<draws.size())++partial;
   uint64_t current=hash(vertices.data(),vertices.size()*sizeof(vertices[0]))^hash(matrices.data(),matrices.size()*sizeof(matrices[0]));
   for(size_t i=0;i<palettes.size();++i)if(!palettes[i].empty())current^=hash(palettes[i].data(),palettes[i].size()*4)*(i*2+1);
   if(frames&&current!=previous)++changed;previous=current;
   check(updateVertices(renderer,vertices.data(),uint32_t(vertices.size())));check(render(renderer));++frames;
   if(frames==1||frames==15||frames==30||frames==60){std::string name="native-world-"+std::to_string(frames)+".ppm";check(capture(renderer,name.c_str()));status();}
  }
  for(auto& d:draws)d.seen=false;textureHashes.clear();vb.clear();vbAddress=0;
  if((seen&&frames==0)||presents%300==0)status();
 }
};
std::mutex guard;std::unique_ptr<State> state;bool initialized=false,disabled=false;
void fail(const std::exception& e){if(state){state->failure=e.what();state->status();}else std::ofstream("native-world-error.txt")<<e.what()<<'\n';disabled=true;}
}
void worldObserve(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base) noexcept {std::lock_guard lock(guard);if(disabled)return;try{if(!initialized){initialized=true;state=std::make_unique<State>();}state->observe(device,primitive,vertexBase,indexStart,count,base);}catch(const std::exception& e){fail(e);}}
void worldPresent() noexcept {
 std::lock_guard lock(guard);
 try {
  if(std::filesystem::exists("native-world-reload.req")){
   std::filesystem::remove("native-world-reload.req");
   auto replacement=std::make_unique<State>();
   state=std::move(replacement);initialized=true;disabled=false;
   std::ofstream("native-world-run.json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"reloaded\":true}\n";
   state->status();return;
  }
  if(state&&!disabled)state->present();
 }catch(const std::exception& e){fail(e);}
}
void worldShutdown() noexcept {std::lock_guard lock(guard);if(state){state->status();state.reset();}disabled=true;}
}
