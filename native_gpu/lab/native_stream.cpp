// Dynamic indexed frontend. Consumes current CPU meshes/assets; no scene manifest.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#include "native_stream.h"
#include "source_geometry.h"
#include "source_sprites.h"
#include "source_post.h"
#include "source_texture.h"
#include "live_raster.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
namespace dbzlab {
namespace {void* presentationWindow=nullptr;struct MemoryWatch {void* context=nullptr;StreamMemoryArm arm=nullptr;StreamMemoryEpoch epoch=nullptr;} memoryWatch;}
void streamSetPresentationWindow(void* window){presentationWindow=window;}
namespace {
struct FpuScope {unsigned saved=_mm_getcsr();FpuScope(){_mm_setcsr(0x1f80);}~FpuScope(){_mm_setcsr(saved);}};
struct MeasuredScope {uint64_t& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();~MeasuredScope(){total+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());}};
bool read(const void* p,void* out,size_t n){SIZE_T done=0;return ReadProcessMemory(GetCurrentProcess(),p,out,n,&done)&&done==n;}
const uint8_t* guest(uint8_t* base,uint32_t address){return base+address+(address>=0xe0000000u?0x1000u:0u);}
uint32_t word(uint8_t* base,uint32_t address){uint8_t b[4];if(!read(guest(base,address),b,4))throw std::runtime_error("Source guest object unreadable");return sourceWord(b);}
uint64_t hash(const void* data,size_t count){uint64_t value=14695981039346656037ull;auto* p=static_cast<const uint8_t*>(data);for(size_t i=0;i<count;++i){value^=p[i];value*=1099511628211ull;}return value;}
struct NativeFailure:std::runtime_error {using std::runtime_error::runtime_error;};
struct Library {HMODULE value=nullptr;~Library(){if(value)FreeLibrary(value);}};
#pragma pack(push,1)
struct Material {uint64_t vs,ps;uint32_t vsBytes,psBytes,kind;float diffuse[3];};
#pragma pack(pop)
static_assert(sizeof(Material)==40);
struct TextureEntry {uint32_t w,h,format,swizzle,id;std::vector<uint8_t> bytes;};
struct SourceTextureEntry {uint32_t id;std::vector<uint8_t> raw;};
struct Topology {uint32_t primitive,width;std::vector<uint8_t> raw;std::vector<uint32_t> source,indices;};
struct DrawData {DbzNativeFrameDraw draw{};std::vector<float> palette;};
struct VideoTexture {uint32_t id=UINT32_MAX,w=0,h=0,format=0;std::vector<uint8_t> bytes;};
struct VideoSlot {VideoTexture luma,chroma;};
struct ResolveTexture {uint32_t id=UINT32_MAX,w=0,h=0,type=0,depthInfo=0,object=0,bytes=0;uint64_t frame=UINT64_MAX,cpuEpoch=0;};
struct SourceTarget {uint32_t id=0,w=1280,h=720;};
struct State {
 Library library;DbzNativeRenderer renderer=nullptr;
 decltype(&dbz_native_create_stream) create=nullptr;decltype(&dbz_native_destroy) destroy=nullptr;
 decltype(&dbz_native_add_texture_blocks) addTexture=nullptr;decltype(&dbz_native_set_frame) setFrame=nullptr;
 decltype(&dbz_native_update_texture_blocks) updateTexture=nullptr;
 decltype(&dbz_native_add_snapshot_texture) addSnapshot=nullptr;decltype(&dbz_native_set_frame_operations) setOperations=nullptr;
 decltype(&dbz_native_add_render_target) addTarget=nullptr;
 decltype(&dbz_native_render) render=nullptr;decltype(&dbz_native_capture) capture=nullptr;decltype(&dbz_native_last_error) lastError=nullptr;
 decltype(&dbz_native_get_stats) getStats=nullptr;
 std::vector<Material> materials;std::unordered_map<uint64_t,std::vector<TextureEntry>> textures;
 std::map<std::array<uint32_t,13>,SourceTextureEntry> sourceTextures;
 std::vector<uint8_t> textureScratch;
 size_t sourceTextureBytes=0;
 uint64_t sourceTextureHits=0,sourceTextureDecodes=0;
 std::set<std::array<uint64_t,6>> diagnosticStates;
 std::unordered_map<uint64_t,std::vector<Topology>> topologies;size_t topologyBytes=0;uint32_t white=UINT32_MAX,zero=UINT32_MAX;
 std::vector<DbzNativeFrameVertex> vertices;std::vector<uint32_t> indices;std::vector<DrawData> draws;
 uint64_t frames=0,sceneFrames=0,observed=0,accepted=0,rejected=0,maximum=0,changed=0,previous=0,newTopologies=0,registeredTextures=0,skinnedDraws=0;
 uint64_t observedUp=0,acceptedUp=0,rejectedUp=0,indexedFrames=0,frameIndexed=0,maxIndexed=0;
 std::vector<VideoSlot> videoSlots;uint32_t nextVideoSlot=0;uint64_t videoFrames=0,videoTextureUpdates=0;
 std::map<uint32_t,ResolveTexture> resolveTextures;std::vector<DbzNativeFrameOperation> operations;
 std::map<std::array<uint32_t,4>,ResolveTexture> resolveStorage;
 std::map<std::array<uint32_t,2>,SourceTarget> targets;
 std::set<std::array<uint32_t,2>> depthOwners;bool maskProduced=false;
 uint64_t observedResolves=0,acceptedResolves=0,rejectedResolves=0;
 bool mainColorCleared=false;
 uint64_t depthRestoreServices=0,noEffectServices=0,depthResampleServices=0;
 uint64_t sourceCpuNs=0,uploadNs=0,renderPresentNs=0;
 std::map<std::array<uint64_t,4>,uint64_t> unknownMaterials;uint64_t unknownOverflow=0;
 std::map<std::string,uint64_t> reasons,upReasons,resolveReasons;std::array<uint64_t,23> kinds{};std::string failure;
 bool videoDiagnosticWritten=false;
 State(){
  const char* enabled=std::getenv("DBZ3_NATIVE_STREAM");if(!enabled||!*enabled||std::string(enabled)=="0")return;
  const char* dll=std::getenv("DBZ3_NATIVE_RENDERER"),*shaders=std::getenv("DBZ3_NATIVE_SHADERS"),*registry=std::getenv("DBZ3_NATIVE_MATERIALS");
  if(!dll||!shaders||!registry)throw std::runtime_error("Dynamic native renderer paths missing");
  std::ifstream f(registry,std::ios::binary);char magic[8]{};uint32_t count=0;f.read(magic,8);f.read(reinterpret_cast<char*>(&count),4);
  if(!f||std::memcmp(magic,"DBZMAT01",8)||!count||count>1024)throw std::runtime_error("Invalid material registry");
  materials.resize(count);f.read(reinterpret_cast<char*>(materials.data()),count*sizeof(Material));if(!f||f.peek()!=EOF)throw std::runtime_error("Truncated material registry");
  for(const auto& m:materials){if(m.kind>22||!m.vsBytes||!m.psBytes||m.vsBytes>65536||m.psBytes>65536)throw std::runtime_error("Invalid material registry entry");for(float v:m.diffuse)if(!std::isfinite(v))throw std::runtime_error("Invalid registry diffuse");}
  library.value=LoadLibraryW(std::filesystem::path(dll).c_str());if(!library.value)throw std::runtime_error("Dynamic native DLL load failed");
#define GET(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(library.value,name));if(!member)throw std::runtime_error("Dynamic native ABI missing " name)
  GET(create,"dbz_native_create_stream");GET(destroy,"dbz_native_destroy");GET(addTexture,"dbz_native_add_texture_blocks");GET(updateTexture,"dbz_native_update_texture_blocks");GET(setFrame,"dbz_native_set_frame");GET(render,"dbz_native_render");GET(capture,"dbz_native_capture");GET(lastError,"dbz_native_last_error");
  GET(getStats,"dbz_native_get_stats");
  GET(addSnapshot,"dbz_native_add_snapshot_texture");GET(setOperations,"dbz_native_set_frame_operations");
  GET(addTarget,"dbz_native_add_render_target");
#undef GET
  FpuScope fpu;renderer=create(presentationWindow,1280,720,shaders);if(!renderer)throw NativeFailure(lastError());
  if(presentationWindow){auto pacing=reinterpret_cast<decltype(&dbz_native_set_present_interval)>(GetProcAddress(library.value,"dbz_native_set_present_interval"));if(!pacing)throw NativeFailure("Native caller-paced presentation API missing");check(pacing(renderer,0));}
 }
 ~State(){FpuScope fpu;if(renderer)destroy(renderer);}
 void check(int result){if(!result)throw NativeFailure(lastError());}
 std::pair<uint64_t,uint32_t> program(uint8_t* base,uint32_t object,bool vertex){
  if(object<0x10000)throw std::runtime_error("Source shader object missing");uint32_t variant=word(base,object+(vertex?896:64));if(variant>32768)throw std::runtime_error("Source shader variant exceeds bounds");
  uint32_t header=object+(vertex?872:40)+variant,offset=word(base,header),size=word(base,header+4),address=word(base,object+(vertex?32:24));
  if(!size||size>65536||uint64_t(address)+offset+size>0x100000000ull)throw std::runtime_error("Source shader program exceeds bounds");
  std::vector<uint8_t> data(size);if(!read(guest(base,address+offset),data.data(),size))throw std::runtime_error("Source shader unreadable");return {hash(data.data(),data.size()),size};
 }
 const Topology& topology(std::vector<uint8_t> raw,uint32_t primitive,uint32_t width){
  uint64_t key=hash(raw.data(),raw.size())^primitive^(uint64_t(width)<<32);
  if(auto found=topologies.find(key);found!=topologies.end())for(const auto& t:found->second)if(t.primitive==primitive&&t.width==width&&t.raw==raw)return t;
  if(topologyBytes>64*1024*1024){topologies.clear();topologyBytes=0;}
  Topology t{};t.primitive=primitive;t.width=width;t.raw=std::move(raw);std::vector<uint32_t> original(t.raw.size()/width);
  for(size_t i=0;i<original.size();++i)original[i]=width==4?sourceWord(t.raw.data()+i*4):uint32_t(t.raw[i*2])<<8|t.raw[i*2+1];
  t.source=original;std::sort(t.source.begin(),t.source.end());t.source.erase(std::unique(t.source.begin(),t.source.end()),t.source.end());
  std::vector<uint32_t> remap;remap.reserve(original.size());for(auto index:original)remap.push_back(uint32_t(std::lower_bound(t.source.begin(),t.source.end(),index)-t.source.begin()));
  if(primitive==4)t.indices=std::move(remap);
  else if(primitive==5)for(size_t i=1;i+1<remap.size();++i){t.indices.push_back(remap[0]);t.indices.push_back(remap[i]);t.indices.push_back(remap[i+1]);}
  else for(size_t i=0;i+2<remap.size();++i){t.indices.push_back(remap[i+(i&1)]);t.indices.push_back(remap[i+1-(i&1)]);t.indices.push_back(remap[i+2]);}
  topologyBytes+=t.raw.size()+t.source.size()*4+t.indices.size()*4;++newTopologies;auto& bucket=topologies[key];bucket.push_back(std::move(t));return bucket.back();
 }
 uint32_t texture(const uint8_t* d,uint32_t slot,uint8_t* base){
  uint32_t fetch[6];for(int k=0;k<6;++k)fetch[k]=sourceWord(d+0x480+slot*24+k*4);
  // This game deliberately leaves type=0 on valid descriptors. Its checked
  // reference configuration enables that convention; preserve the CPU asset.
  if((fetch[0]&3)==0||(fetch[0]&3)==2){
   if(!(fetch[1]&0xfffff000)&&!(fetch[5]&0xfffff000)){if(zero==UINT32_MAX){const uint8_t rgba[]={0,0,0,0};check(addTexture(renderer,DBZ_NATIVE_RGBA8,1,1,rgba,4,0x688,&zero));++registeredTextures;}return zero;}
   fetch[0]|=2;
  }
  auto layout=sourceTextureLayout(fetch);
  if(auto found=resolveTextures.find(layout.address);found!=resolveTextures.end()){
   const auto& entry=found->second;if(entry.type!=DBZ_NATIVE_COPY_COLOR||entry.w!=layout.width||entry.h!=layout.height||layout.format!=DBZ_NATIVE_RGBA8||((layout.swizzle&511)!=0x88&&(layout.swizzle&511)!=0xa)||((layout.swizzle>>9)&7)!=3)throw std::runtime_error("Rendered texture view conversion pending");return entry.id;
  }
  std::array<uint32_t,13> binding{layout.address,layout.width,layout.height,layout.format,layout.swizzle,layout.pitch,layout.block,layout.bytes,layout.endian,layout.packedX,layout.packedY,uint32_t(layout.tiled),uint32_t(layout.allocation)};
  textureScratch.resize(layout.allocation);
  if(!read(base+0x100000000ull+layout.address,textureScratch.data(),textureScratch.size()))throw std::runtime_error("Source texture allocation unreadable");
  if(auto found=sourceTextures.find(binding);found!=sourceTextures.end()&&found->second.raw==textureScratch){++sourceTextureHits;return found->second.id;}
  ++sourceTextureDecodes;auto data=linearizeSourceTexture(textureScratch.data(),textureScratch.size(),layout);
  auto remember=[&](uint32_t id){
   auto found=sourceTextures.find(binding);size_t previous=found==sourceTextures.end()?0:found->second.raw.size();
   if(sourceTextureBytes-previous+textureScratch.size()>64*1024*1024){sourceTextures.clear();sourceTextureBytes=0;previous=0;}
   auto& entry=sourceTextures[binding];sourceTextureBytes=sourceTextureBytes-previous+textureScratch.size();entry.id=id;entry.raw.swap(textureScratch);return id;
  };
  uint64_t key=hash(data.data(),data.size())^(uint64_t(layout.width)<<32)^layout.height^(uint64_t(layout.format)<<48)^layout.swizzle;
  auto& bucket=textures[key];for(const auto& t:bucket)if(t.w==layout.width&&t.h==layout.height&&t.format==layout.format&&t.swizzle==layout.swizzle&&t.bytes==data)return remember(t.id);
  uint32_t id;check(addTexture(renderer,layout.format,layout.width,layout.height,data.data(),uint32_t(data.size()),layout.swizzle,&id));++registeredTextures;
  bucket.push_back({layout.width,layout.height,layout.format,layout.swizzle,id,std::move(data)});return remember(id);
 }
 void videoTexture(VideoTexture& cached,uint32_t format,uint32_t w,uint32_t h,std::vector<uint8_t> bytes){
  if(cached.id==UINT32_MAX){check(addTexture(renderer,format,w,h,bytes.data(),uint32_t(bytes.size()),0x688,&cached.id));++registeredTextures;}
  else if(cached.w!=w||cached.h!=h||cached.format!=format||cached.bytes!=bytes){check(updateTexture(renderer,cached.id,format,w,h,bytes.data(),uint32_t(bytes.size()),0x688));++videoTextureUpdates;}
  cached.w=w;cached.h=h;cached.format=format;cached.bytes=std::move(bytes);
 }
 void video(const uint8_t* d,uint8_t* base,DbzNativeFrameDraw& draw){
  if(nextVideoSlot>=64)throw std::runtime_error("Video draw slot limit exceeded");
  std::array<SourceTexture,3> layouts;std::array<std::vector<uint8_t>,3> planes;uint32_t clamp[3]{};
  for(uint32_t slot=0;slot<3;++slot){
   uint32_t f[6];for(int k=0;k<6;++k)f[k]=sourceWord(d+0x480+slot*24+k*4);
   if((f[0]&3)==0)f[0]|=2;auto layout=sourceTextureLayout(f);uint32_t filter=slot?0:1;
   uint32_t u=(f[0]>>10)&7,v=(f[0]>>13)&7;
   if(layout.format!=DBZ_NATIVE_R8||!layout.address||(layout.swizzle&7)||(u!=0&&u!=2)||u!=v||((f[3]>>19)&3)!=filter||((f[3]>>21)&3)!=filter||(f[3]&1)||((f[3]>>13)&63)||((f[4]>>2)&255)){
    if(!videoDiagnosticWritten){videoDiagnosticWritten=true;std::ofstream out("native-video-unsupported.json");out<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"slot\":"<<slot<<",\"fetch\":[";for(int k=0;k<6;++k){if(k)out<<',';out<<f[k];}out<<"]}\n";}
    throw std::runtime_error("Unsupported video plane format or sampler");
   }
   std::vector<uint8_t> raw(layout.allocation);if(!read(base+0x100000000ull+layout.address,raw.data(),raw.size()))throw std::runtime_error("Video plane unreadable");
   layouts[slot]=layout;clamp[slot]=u==2;planes[slot]=linearizeSourceTexture(raw.data(),raw.size(),layout);
  }
  if(layouts[1].width!=layouts[2].width||layouts[1].height!=layouts[2].height)throw std::runtime_error("Video chroma planes have different dimensions");
  std::vector<uint8_t> chroma(planes[1].size()*2);for(size_t i=0;i<planes[1].size();++i){chroma[2*i]=planes[1][i];chroma[2*i+1]=planes[2][i];}
  if(nextVideoSlot==videoSlots.size())videoSlots.emplace_back();auto& slot=videoSlots[nextVideoSlot];
  videoTexture(slot.luma,DBZ_NATIVE_R8,layouts[0].width,layouts[0].height,std::move(planes[0]));
  videoTexture(slot.chroma,DBZ_NATIVE_RG8,layouts[1].width,layouts[1].height,std::move(chroma));
  draw.base_texture=slot.luma.id;draw.ramp_texture=slot.chroma.id;draw.material[4]=float(clamp[1]);draw.material[5]=float(clamp[2]);draw.material[6]=float(clamp[0]);++nextVideoSlot;
 }
 void reject(const std::string& reason){++rejected;++reasons[reason];}
 void diagnose(uint8_t* base,uint32_t dev,uint32_t category,uint32_t a,uint32_t b,uint32_t c,uint32_t stride,const char* reason) noexcept {
  try{
   std::vector<uint8_t> d(0x6400);if(!read(guest(base,dev),d.data(),d.size()))return;
   std::array<uint64_t,6> key{category,hash(reason,std::strlen(reason)),sourceWord(d.data()+0x480),sourceWord(d.data()+0x48c),sourceWord(d.data()+13072),sourceWord(d.data()+13068)};
   if(diagnosticStates.size()>=64||!diagnosticStates.insert(key).second)return;
   auto folder="native-unsupported/"+std::to_string(GetCurrentProcessId());std::filesystem::create_directories(folder);auto prefix=folder+"/state-"+std::to_string(diagnosticStates.size());
   std::ofstream(prefix+"-device.bin",std::ios::binary).write(reinterpret_cast<const char*>(d.data()),d.size());
   std::vector<uint8_t> payload;
   if(category==1&&b&&stride&&uint64_t(b)*stride<=4*1024*1024&&uint64_t(c)+uint64_t(b)*stride<=0x100000000ull){payload.resize(size_t(b)*stride);if(read(guest(base,c),payload.data(),payload.size()))std::ofstream(prefix+"-vertices.bin",std::ios::binary).write(reinterpret_cast<const char*>(payload.data()),payload.size());}
   if(category==2){payload.resize(128);if(read(guest(base,b),payload.data(),payload.size()))std::ofstream(prefix+"-destination.bin",std::ios::binary).write(reinterpret_cast<const char*>(payload.data()),payload.size());}
   std::string message=reason;for(auto& ch:message)if(ch=='"'||ch=='\\'||uint8_t(ch)<32)ch=' ';
   std::ofstream metadata(prefix+".json");metadata<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"frame\":"<<frames<<",\"category\":"<<category<<",\"a\":"<<a<<",\"b\":"<<b<<",\"c\":"<<c<<",\"stride\":"<<stride<<",\"reason\":\""<<message<<'"';
   uint32_t address=sourceWord(d.data()+0x484)&0xfffff000,bound=sourceWord(d.data()+12920);
   if(auto found=resolveTextures.find(address);found!=resolveTextures.end()){
    const auto& entry=found->second;uint64_t epoch=memoryWatch.epoch?memoryWatch.epoch(memoryWatch.context,address,entry.bytes):0;
    metadata<<",\"rendered_alias\":{\"address\":"<<address<<",\"saved_object\":"<<entry.object<<",\"bound_object\":"<<bound<<",\"width\":"<<entry.w<<",\"height\":"<<entry.h<<",\"type\":"<<entry.type<<",\"watched_bytes\":"<<entry.bytes<<",\"saved_cpu_epoch\":"<<entry.cpuEpoch<<",\"current_cpu_epoch\":"<<epoch<<'}';
    std::array<uint8_t,128> object{};if(bound>=0x10000&&bound<=0xffffff80u&&read(guest(base,bound),object.data(),object.size()))std::ofstream(prefix+"-texture-object.bin",std::ios::binary).write(reinterpret_cast<const char*>(object.data()),object.size());
   }
   metadata<<"}\n";
  }catch(...){/* Diagnostic failure cannot alter the game's typed rejection. */}
 }
 SourceTarget target(uint32_t pitch,uint32_t depthInfo){
  if(depthInfo&(1u<<16))throw std::runtime_error("Floating-point guest depth target pending");
  if(pitch==1280)return {};
  if(pitch==560)return {DBZ_NATIVE_MASK_SURFACE,512,512};
  if(pitch!=640&&pitch!=320&&pitch!=160&&pitch!=80)throw std::runtime_error("Unsupported source target dimensions");
  std::array<uint32_t,2> key{pitch,depthInfo};if(auto found=targets.find(key);found!=targets.end())return found->second;
  SourceTarget entry{0,pitch,pitch*9/16};uint32_t texture;DbzNativeClear clear{{0,0,0,0},1,1,7};check(addTarget(renderer,entry.w,entry.h,&clear,&entry.id,&texture));++registeredTextures;targets.emplace(key,entry);return entry;
 }
 SourceTarget colorTarget(const uint8_t* d,DbzNativeFrameDraw& draw){
  uint32_t color=sourceWord(d+10372);if((color&0xfff)||(color&0x03ff0000))throw std::runtime_error("Separate or non-RGBA8 source color target pending");
  auto entry=target(sourceWord(d+10368)&0x3fff,sourceWord(d+10376));const auto* v=draw.raster.viewport;
  if(entry.id==DBZ_NATIVE_MASK_SURFACE)throw std::runtime_error("Mask target requires a checked mask material");
  if(v[0]<0||v[1]<0||v[0]+v[2]>entry.w||v[1]+v[3]>entry.h)throw std::runtime_error("Source viewport exceeds its native target");draw.reserved=entry.id;return entry;
 }
 void resolve(uint32_t dev,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t sourceRect,uint32_t destinationPoint){
  if(!renderer)return;++observedResolves;FpuScope fpu;
  try{
   if(flags&~0x104u)throw std::runtime_error("Resolve clear or other intent pending");bool depth=(flags&4)!=0,clearColor=(flags&0x100)!=0;uint32_t type=depth?DBZ_NATIVE_COPY_DEPTH:DBZ_NATIVE_COPY_COLOR;
   if(destinationPoint){std::array<uint8_t,8> point;if(!read(guest(base,destinationPoint),point.data(),point.size())||sourceWord(point.data())||sourceWord(point.data()+4))throw std::runtime_error("Offset destination resolve pending");}
   std::array<uint8_t,0x6400> device;if(!read(guest(base,dev),device.data(),device.size()))throw std::runtime_error("Source resolve device unreadable");const auto* d=device.data();
   uint32_t pitch=sourceWord(d+10368)&0x3fff,colorInfo=sourceWord(d+10372),depthInfo=sourceWord(d+10376),colorBase=colorInfo&0xfff;
   bool depthAlias=!depth&&colorBase!=0;
   if(depthAlias){if(colorInfo&0x03ff0000)throw std::runtime_error("Unsupported packed depth target format");depthInfo=(depthInfo&~0xfffu)|colorBase;if(!depthOwners.count({pitch,depthInfo}))throw std::runtime_error("Packed depth alias has no native depth producer");type=DBZ_NATIVE_COPY_DEPTH;}
   else if(colorBase||colorInfo&0x03ff0000)throw std::runtime_error("Separate or non-RGBA8 resolve target pending");
   if(clearColor&&(sourceWord(d+10800)||sourceWord(d+10804)||((sourceWord(d+10372)>>16)&15)))throw std::runtime_error("Nonzero or non-RGBA8 resolve clear pending");
   std::array<uint8_t,128> texture;if(destination<0x10000||!read(guest(base,destination),texture.data(),texture.size()))throw std::runtime_error("Source resolve destination unreadable");
   uint32_t f1=sourceWord(texture.data()+32),f2=sourceWord(texture.data()+36),w=(f2&8191)+1,h=((f2>>13)&8191)+1,address=sourceWord(d+10780);
   auto source=target(pitch,depthInfo);uint32_t copyPitch=sourceWord(d+10784);
   if(source.id==DBZ_NATIVE_MASK_SURFACE&&!maskProduced)throw std::runtime_error("Mask resolve has no current native producer");
   if(clearColor&&depthAlias)throw std::runtime_error("Packed depth color clear pending");
   if((f1&63)!=(depth?22u:6u)||w!=source.w||h!=source.h||!address||address>=0x20000000||(address&4095)||((f1&0x1ffff000)+0x1000)!=address||(copyPitch&65535)!=((w+31)&~31u)||(copyPitch>>16)!=h||((sourceWord(texture.data()+48)>>9)&3)!=1)throw std::runtime_error("Unsupported native resolve layout");
   if(sourceRect){std::array<uint8_t,16> rect;if(!read(guest(base,sourceRect),rect.data(),rect.size())||sourceWord(rect.data())||sourceWord(rect.data()+4)||sourceWord(rect.data()+8)!=w||sourceWord(rect.data()+12)!=h)throw std::runtime_error("Partial source resolve rectangle pending");}
   if(operations.size()+1+clearColor>10000)throw std::runtime_error("Native resolve operation limit exceeded");
   // A guest allocation is reused for differently sized/color/depth images.
   // Pending draws retain their immutable native image IDs; a fresh resolve
   // selects the matching storage version for subsequent reads of this alias.
   auto& storage=resolveStorage[{address,type,w,h}];if(storage.id==UINT32_MAX){check(addSnapshot(renderer,type,w,h,&storage.id));storage.w=w;storage.h=h;storage.type=type;++registeredTextures;}
   operations.push_back({uint32_t(draws.size()),type,source.id,storage.id,{}});storage.frame=frames;if(type==DBZ_NATIVE_COPY_DEPTH)storage.depthInfo=depthInfo;
   storage.object=destination;storage.bytes=uint32_t((uint64_t(copyPitch&65535)*((h+31)&~31u)*4+4095)&~4095ull);
   if(memoryWatch.arm)memoryWatch.arm(memoryWatch.context,address,storage.bytes);storage.cpuEpoch=memoryWatch.epoch?memoryWatch.epoch(memoryWatch.context,address,storage.bytes):0;resolveTextures[address]=storage;
   if(type==DBZ_NATIVE_COPY_DEPTH)depthOwners.insert({pitch,depthInfo});
   if(clearColor){operations.push_back({uint32_t(draws.size()),DBZ_NATIVE_CLEAR_TARGET,source.id,0,{{0,0,0,0},1,0,1}});if(!source.id)mainColorCleared=true;}++acceptedResolves;
  }catch(const NativeFailure&){throw;}catch(const std::exception& e){++rejectedResolves;++resolveReasons[e.what()];diagnose(base,dev,2,flags,destination,sourceRect,destinationPoint,e.what());}
 }
 uint32_t resolvedTexture(const uint8_t* d,uint32_t slot=0,bool depth=false){
  uint32_t fetch[6];for(int k=0;k<6;++k)fetch[k]=sourceWord(d+0x480+slot*24+k*4);if((fetch[0]&3)==0)fetch[0]|=2;
  uint32_t address=fetch[1]&0xfffff000,w=(fetch[2]&8191)+1,h=((fetch[2]>>13)&8191)+1;
  if(depth){
   uint32_t f0=fetch[0],f3=fetch[3],mag=(f3>>19)&3,min=(f3>>21)&3;if((f0&3)!=2||(f0&0x3fc)||(fetch[1]&63)!=22||(fetch[1]&(1u<<10))||((fetch[5]>>9)&3)!=1||((f0>>10)&7)!=2||((f0>>13)&7)!=2||mag!=min||mag>1||(f3&1)||((f3>>13)&63)||((f3>>1)&7))throw std::runtime_error("Unsupported native depth input or sampler");
  }else{auto layout=sourceTextureLayout(fetch);if(layout.format!=DBZ_NATIVE_RGBA8||((layout.swizzle&511)!=0x88&&(layout.swizzle&511)!=0xa))throw std::runtime_error("Packed depth or other copy input pending");}
  auto found=resolveTextures.find(address);if(found==resolveTextures.end()||found->second.frame==UINT64_MAX)throw std::runtime_error("Native copy input has no initialized GPU resolve");
  if(found->second.w!=w||found->second.h!=h||found->second.type!=(depth?DBZ_NATIVE_COPY_DEPTH:DBZ_NATIVE_COPY_COLOR))throw std::runtime_error("Native copy input dimensions or type differ from resolve");return found->second.id;
 }
 void restoreDepth(const uint8_t* d,uint32_t primitive,uint32_t count,uint32_t address,uint32_t stride,uint8_t* base){
  uint32_t depthInfo=sourceWord(d+10376),colorInfo=sourceWord(d+10372);
  auto destination=target(sourceWord(d+10368)&0x3fff,depthInfo);
  // Z_WRITE is inert with Z_ENABLE disabled. The game leaves it set during
  // packed color/depth copies; stencil and depth testing must still be off.
  if(primitive!=8||count!=3||stride!=16||(colorInfo&0xfff)!=(depthInfo&0xfff)||((colorInfo>>16)&15)||(sourceWord(d+10548)&3)||(sourceWord(d+10460)&15)!=15||((sourcePipeline(d)>>4)&7)||sourceFloat(d+6016)!=0||sourceFloat(d+6020)!=0)throw std::runtime_error("Unsupported depth restore intent");
  auto raster=nativeRaster(d);const float viewport[]={0,0,float(destination.w),float(destination.h),0,1};const int32_t scissor[]={0,0,int32_t(destination.w),int32_t(destination.h)};if(raster.alpha.enabled||!std::equal(std::begin(viewport),std::end(viewport),raster.viewport)||!std::equal(std::begin(scissor),std::end(scissor),raster.scissor))throw std::runtime_error("Partial depth restore pending");
  uint32_t f[6];for(int k=0;k<6;++k)f[k]=sourceWord(d+0x480+k*4);if((f[0]&3)==0)f[0]|=2;
  if((f[0]&3)!=2||(f[0]&0x3fc)||(f[1]&63)!=6||(f[1]&1024)||((f[5]>>9)&3)!=1||((f[0]>>10)&7)!=2||((f[0]>>13)&7)!=2||(f[2]&8191)+1!=destination.w||((f[2]>>13)&8191)+1!=destination.h||((f[3]>>1)&4095)!=0x688||(f[3]&1)||((f[3]>>13)&63))throw std::runtime_error("Unsupported packed depth restore binding");
  auto found=resolveTextures.find(f[1]&0xfffff000);if(found==resolveTextures.end()||found->second.frame==UINT64_MAX||found->second.type!=DBZ_NATIVE_COPY_DEPTH||found->second.depthInfo!=depthInfo)throw std::runtime_error("Depth restore has no initialized native saved depth");
  if(uint64_t(address)+48>0x100000000ull)throw std::runtime_error("Depth restore vertex address overflow");std::array<uint8_t,48> raw;if(!read(guest(base,address),raw.data(),raw.size()))throw std::runtime_error("Depth restore vertices unreadable");const float positions[][2]={{0,0},{float(destination.w),0},{0,float(destination.h)}};
  for(int i=0;i<3;++i)for(int k=0;k<2;++k)if(sourceFloat(raw.data()+i*16+k*4)!=positions[i][k])throw std::runtime_error("Partial depth restore geometry pending");
  if(operations.size()>=10000)throw std::runtime_error("Native restore operation limit exceeded");operations.push_back({uint32_t(draws.size()),DBZ_NATIVE_RESTORE_DEPTH,destination.id,found->second.id,{}});++depthRestoreServices;
 }
 void resampleDepth(const uint8_t* d,uint32_t primitive,uint32_t count,uint32_t address,uint32_t stride,uint8_t* base){
  uint32_t color=sourceWord(d+10372),pitch=sourceWord(d+10368)&0x3fff,depthInfo=(sourceWord(d+10376)&~0xfffu)|(color&0xfff);
  if(primitive!=8||count!=3||stride!=16||!(color&0xfff)||(color&0x03ff0000)||(sourceWord(d+10548)&3)||(sourceWord(d+10460)&15)!=15||((sourcePipeline(d)>>4)&7))throw std::runtime_error("Unsupported depth resample intent");
  auto destination=target(pitch,depthInfo);if(destination.id==DBZ_NATIVE_MASK_SURFACE)throw std::runtime_error("Mask depth resampling pending");
  uint32_t f[6];for(int k=0;k<6;++k)f[k]=sourceWord(d+0x480+k*4);if((f[0]&3)==0)f[0]|=2;
  auto found=resolveTextures.find(f[1]&0xfffff000);
  if((f[0]&3)!=2||(f[0]&0x3fc)||(f[1]&63)!=6||(f[1]&1024)||((f[5]>>9)&3)!=1||((f[3]>>1)&4095)!=0x688||found==resolveTextures.end()||found->second.frame==UINT64_MAX||found->second.type!=DBZ_NATIVE_COPY_DEPTH||found->second.w!=(f[2]&8191)+1||found->second.h!=((f[2]>>13)&8191)+1)throw std::runtime_error("Depth resample has no matching native saved depth");
  DrawData item;auto& draw=item.draw;draw.raster=nativeRaster(d);draw.flags=0x703;draw.base_texture=draw.ramp_texture=found->second.id;std::fill_n(draw.diffuse,3,1.f);sourceCopyMaterial(d,draw);if(draw.material[4]!=1||draw.raster.alpha.enabled)throw std::runtime_error("Depth resampling requires unchecked-alpha point sampling");draw.material[3]=DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL;draw.reserved=destination.id;sourcePostProjection(draw,destination.w,destination.h);
  const int32_t scissor[]={0,0,int32_t(destination.w),int32_t(destination.h)};if(!std::equal(std::begin(scissor),std::end(scissor),draw.raster.scissor))throw std::runtime_error("Partial depth resampling pending");
  if(uint64_t(address)+48>0x100000000ull)throw std::runtime_error("Depth resample vertex address overflow");std::array<uint8_t,48> raw;if(!read(guest(base,address),raw.data(),raw.size()))throw std::runtime_error("Depth resample vertices unreadable");const float expected[][4]={{0,0,0,0},{float(destination.w),0,1,0},{0,float(destination.h),0,1}};
  for(int i=0;i<3;++i)for(int k=0;k<4;++k)if(sourceFloat(raw.data()+i*16+k*4)!=expected[i][k])throw std::runtime_error("Partial depth resample geometry pending");
  if(found->second.w==destination.w&&found->second.h==destination.h){if(operations.size()>=10000)throw std::runtime_error("Native restore operation limit exceeded");operations.push_back({uint32_t(draws.size()),DBZ_NATIVE_RESTORE_DEPTH,destination.id,found->second.id,{}});}
  else{auto a=sourcePostVertex(raw.data()),b=sourcePostVertex(raw.data()+16),c=sourcePostVertex(raw.data()+32),last=a;for(int k=0;k<2;++k){last.position[k]=b.position[k]+c.position[k]-a.position[k];last.uv[k]=b.uv[k]+c.uv[k]-a.uv[k];}append(draw,std::move(item),{a,b,c,last},{0,1,2,2,1,3});}
  depthOwners.insert({pitch,depthInfo});++depthResampleServices;
 }
 const Material& material(uint8_t* base,const uint8_t* d){
  auto vs=program(base,sourceWord(d+13072),true),ps=program(base,sourceWord(d+13068),false);
  auto found=std::find_if(materials.begin(),materials.end(),[&](const auto& m){return m.vs==vs.first&&m.ps==ps.first&&m.vsBytes==vs.second&&m.psBytes==ps.second;});
  if(found==materials.end()){
   std::array<uint64_t,4> key{vs.first,ps.first,vs.second,ps.second};
   if(unknownMaterials.size()<128||unknownMaterials.count(key))++unknownMaterials[key];else ++unknownOverflow;
   throw std::runtime_error("Unknown source material");
  }return *found;
 }
 void append(DbzNativeFrameDraw& draw,DrawData&& item,const std::vector<DbzNativeFrameVertex>& converted,const std::vector<uint32_t>& mesh){
  if(vertices.size()+converted.size()>1000000||indices.size()+mesh.size()>6000000||draws.size()>=10000)throw std::runtime_error("Source native frame exceeds limits");
  draw.first_index=uint32_t(indices.size());draw.index_count=uint32_t(mesh.size());uint32_t offset=uint32_t(vertices.size());
  vertices.insert(vertices.end(),converted.begin(),converted.end());for(auto index:mesh)indices.push_back(index+offset);draws.push_back(std::move(item));
  if(draw.flags&DBZ_NATIVE_TARGET_MASK)maskProduced=true;
 }
 void observe(uint32_t dev,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base){
  MeasuredScope measured{sourceCpuNs};
  if(!renderer)return;++observed;FpuScope fpu;
  try{
   if(!count||count>1000000||(primitive!=4&&primitive!=5&&primitive!=6)||(primitive==4&&count%3)||((primitive==5||primitive==6)&&count<3))throw std::runtime_error("Unsupported source indexed primitive");
   std::array<uint8_t,0x6400> device;if(!read(guest(base,dev),device.data(),device.size()))throw std::runtime_error("Source device unreadable");const auto* d=device.data();
   if(d[12904]*4!=44)throw std::runtime_error("Unsupported source vertex stride");
   uint32_t vf0=sourceWord(d+0x778),vf1=sourceWord(d+0x77c),vertexAddress=vf0&0x1ffffffc,vertexBytes=((vf1>>2)&0xffffff)*4;
   if((vf0&3)!=3||(vf1&3)!=2||!vertexBytes||vertexBytes>16*1024*1024||uint64_t(vertexAddress)+vertexBytes>0x20000000)throw std::runtime_error("Unsupported source vertex fetch");
   const auto& m=material(base,d);uint32_t kind=m.kind;if(kind==10||kind==11||kind==13||kind>=15)throw std::runtime_error("Sprite material requires UP vertex layout");
   bool mask=kind==6||kind==7;uint32_t pitch=sourceWord(d+10368)&0x3fff;
   if(mask&&pitch!=560)throw std::runtime_error("Unsupported mask target pitch");
   DrawData item;auto& draw=item.draw;draw.flags=sourcePipeline(d)|(mask?DBZ_NATIVE_TARGET_MASK:0)|(kind==8?DBZ_NATIVE_SAMPLE_MASK:0);draw.raster=nativeRaster(d);std::copy_n(m.diffuse,3,draw.diffuse);
   if(!mask)colorTarget(d,draw);
   draw.base_texture=draw.ramp_texture=UINT32_MAX;sourceMaterial(d,kind,draw,item.palette);
   uint32_t ib=sourceWord(d+12812),indexWidth=(word(base,ib)&0x80000000u)?4u:2u;
   uint64_t pointer=uint64_t(word(base,ib+24))+uint64_t(indexStart)*indexWidth;if(pointer>=0x100000000ull)throw std::runtime_error("Source index address overflow");
   uint32_t address=(uint32_t(pointer)&0x1fffffff)+(pointer>=0xe0000000u?0x1000u:0u);if(uint64_t(address)+uint64_t(count)*indexWidth>0x20000000)throw std::runtime_error("Source index allocation exceeds bounds");
   std::vector<uint8_t> raw(size_t(count)*indexWidth);if(!read(base+0x100000000ull+address,raw.data(),raw.size()))throw std::runtime_error("Source indices unreadable");const auto& mesh=topology(std::move(raw),primitive,indexWidth);
   int64_t first=int64_t(mesh.source.front())+vertexBase,last=int64_t(mesh.source.back())+vertexBase;
   if(first<0||uint64_t(last+1)*44>vertexBytes)throw std::runtime_error("Source vertex index out of range");
   std::vector<uint8_t> vb(size_t(last-first+1)*44);if(!read(base+0x100000000ull+vertexAddress+first*44,vb.data(),vb.size()))throw std::runtime_error("Source vertices unreadable");
   std::vector<DbzNativeFrameVertex> converted(mesh.source.size());std::array<bool,128> used{};
   for(size_t i=0;i<mesh.source.size();++i){size_t offset=size_t(int64_t(mesh.source[i])+vertexBase-first)*44;used[sourceVertex(vb.data()+offset,d,kind,draw,converted[i])]=true;}
   if(sourceSkin(kind)){
    uint32_t a=sourceWord(d+0x770),b=sourceWord(d+0x774),physical=a&0x1ffffffc;
    if((a&3)!=3||(b&3)!=2||((b>>2)&0xffffff)*4!=6144||uint64_t(physical)+6144>0x20000000)throw std::runtime_error("Unsupported source palette fetch");
    std::array<uint8_t,6144> palette;if(!read(base+0x100000000ull+physical,palette.data(),palette.size()))throw std::runtime_error("Source palette unreadable");
    for(uint32_t bone=0;bone<128;++bone)if(used[bone])for(int k=0;k<12;++k)item.palette[16+bone*12+k]=sourceFloat(palette.data()+bone*48+k*4);
   }
   for(float v:item.palette)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source palette");
   if(kind==0||kind==1||kind==2||kind==5)draw.base_texture=texture(d,0,base);
   if(kind==1||kind==2)draw.ramp_texture=texture(d,1,base);
   if(kind==9){if(white==UINT32_MAX){const uint8_t rgba[]={255,255,255,255};check(addTexture(renderer,DBZ_NATIVE_RGBA8,1,1,rgba,4,0x688,&white));++registeredTextures;}draw.base_texture=white;}
   if(kind==8){uint32_t f0=sourceWord(d+0x480),f1=sourceWord(d+0x484),f2=sourceWord(d+0x488),f3=sourceWord(d+0x48c);if((f1&63)!=6||(f2&8191)+1!=512||((f2>>13)&8191)+1!=512||(f1&0xfffff000)!=sourceWord(d+10780)||((f0>>10)&7)!=2||((f0>>13)&7)!=2||((f3>>19)&3)||((f3>>21)&3))throw std::runtime_error("Unsupported projected mask binding or sampler");
    if(auto found=resolveTextures.find(f1&0xfffff000);found!=resolveTextures.end()&&found->second.frame!=UINT64_MAX&&found->second.type==DBZ_NATIVE_COPY_COLOR&&found->second.w==512&&found->second.h==512){draw.flags&=~DBZ_NATIVE_SAMPLE_MASK;draw.base_texture=found->second.id;}
   }
   draw.palette_rows=sourcePaletteRows(kind);append(draw,std::move(item),converted,mesh.indices);++accepted;++frameIndexed;++kinds[kind];if(sourceSkin(kind))++skinnedDraws;
   if(!mask&&(draw.flags&2))depthOwners.insert({pitch,sourceWord(d+10376)});
  }catch(const NativeFailure&){throw;}catch(const std::exception& e){reject(e.what());diagnose(base,dev,0,primitive,uint32_t(vertexBase),indexStart,count,e.what());}
 }
 void observeUp(uint32_t dev,uint32_t primitive,uint32_t count,uint32_t address,uint32_t stride,uint8_t* base){
  MeasuredScope measured{sourceCpuNs};
  if(!renderer)return;++observedUp;FpuScope fpu;
  try{
   if(stride!=24&&stride!=16&&stride!=20)throw std::runtime_error("Unsupported UP vertex stride");
   if(!count||count>1000000||(primitive!=3&&primitive!=4&&primitive!=5&&primitive!=6&&primitive!=8)||((primitive==4||primitive==8)&&count%3)||((primitive==5||primitive==6)&&count<3)||(primitive==3&&count<2))throw std::runtime_error("Unsupported UP primitive");
   std::array<uint8_t,0x6400> device;if(!read(guest(base,dev),device.data(),device.size()))throw std::runtime_error("Source device unreadable");const auto* d=device.data();
   if(!sourceWord(d+13068)){
    auto vs=program(base,sourceWord(d+13072),true);bool known=std::any_of(materials.begin(),materials.end(),[&](const auto& m){return m.kind==21&&m.vs==vs.first&&m.vsBytes==vs.second;});uint32_t z=sourceWord(d+10548);
    if(!known||stride!=16||primitive!=8||(sourceWord(d+10460)&15)||(z&7)!=2||((z>>4)&7))throw std::runtime_error("Unsupported null pixel shader intent");
    ++noEffectServices;++acceptedUp;return;
   }
   const auto& m=material(base,d);
   if(primitive==3&&m.kind!=10&&m.kind!=11&&m.kind!=15)throw std::runtime_error("Unsupported UP line material");
   if(m.kind==21){restoreDepth(d,primitive,count,address,stride,base);++acceptedUp;++kinds[21];return;}
   if(m.kind==17&&(sourceWord(d+10372)&0xfff)){resampleDepth(d,primitive,count,address,stride,base);++acceptedUp;++kinds[17];return;}
   if(m.kind>=17&&m.kind<=20){
    bool screen=m.kind==17;if((screen&&(primitive!=8||stride!=16))||(!screen&&(primitive==8||stride!=20)))throw std::runtime_error("Unsupported native copy vertex layout");
    DrawData item;auto& draw=item.draw;draw.raster=nativeRaster(d);draw.flags=sourcePipeline(d);auto destination=colorTarget(d,draw);draw.base_texture=resolvedTexture(d);draw.ramp_texture=UINT32_MAX;std::copy_n(m.diffuse,3,draw.diffuse);
    if(screen)sourcePostProjection(draw,destination.w,destination.h);else{draw.flags|=DBZ_NATIVE_INPUT_CLIP;draw.view_projection[0]=draw.view_projection[5]=draw.view_projection[10]=draw.view_projection[15]=1;}
    draw.flags&=~DBZ_NATIVE_SAMPLE_SCENE;sourceCopyMaterial(d,draw);sourcePostPalette(d,m.kind,draw,item.palette);if(m.kind==20){draw.ramp_texture=resolvedTexture(d,1,true);draw.material[7]=((sourceWord(d+0x4a4)>>19)&3)==1?1.f:0.f;}
    if(uint64_t(address)+uint64_t(count)*stride>0x100000000ull)throw std::runtime_error("Copy vertex address overflow");std::vector<uint8_t> raw(size_t(count)*stride);if(!read(guest(base,address),raw.data(),raw.size()))throw std::runtime_error("Copy vertices unreadable");
    std::vector<DbzNativeFrameVertex> converted;std::vector<uint32_t> mesh;
    if(screen){for(uint32_t i=0;i<count;i+=3){uint32_t first=uint32_t(converted.size());auto a=sourcePostVertex(raw.data()+i*16),b=sourcePostVertex(raw.data()+(i+1)*16),c=sourcePostVertex(raw.data()+(i+2)*16),last=a;for(int k=0;k<2;++k){last.position[k]=b.position[k]+c.position[k]-a.position[k];last.uv[k]=b.uv[k]+c.uv[k]-a.uv[k];}converted.insert(converted.end(),{a,b,c,last});for(uint32_t v:{0u,1u,2u,2u,1u,3u})mesh.push_back(first+v);}}
    else{for(uint32_t i=0;i<count;++i)converted.push_back(sourceClipPostVertex(raw.data()+i*20));if(primitive==4)for(uint32_t i=0;i<count;++i)mesh.push_back(i);else if(primitive==5)for(uint32_t i=1;i+1<count;++i){mesh.push_back(0);mesh.push_back(i);mesh.push_back(i+1);}else for(uint32_t i=0;i+2<count;++i){mesh.push_back(i+(i&1));mesh.push_back(i+1-(i&1));mesh.push_back(i+2);}}
    append(draw,std::move(item),converted,mesh);++acceptedUp;++kinds[m.kind];return;
   }
   if(m.kind==16){
    if(primitive!=8||stride!=16)throw std::runtime_error("Unsupported FXAA rectangle layout");
    if((sourceWord(d+10368)&0x3fff)!=1280)throw std::runtime_error("Separate FXAA target omitted");
    uint32_t fetch[6];for(int k=0;k<6;++k)fetch[k]=sourceWord(d+0x480+k*4);if((fetch[0]&3)==0)fetch[0]|=2;auto layout=sourceTextureLayout(fetch);
    uint32_t clampU=(fetch[0]>>10)&7,clampV=(fetch[0]>>13)&7;
    if(layout.format!=DBZ_NATIVE_RGBA8||layout.width!=1280||layout.height!=720||((layout.swizzle&511)!=0x88&&(layout.swizzle&511)!=0xa)||(clampU!=0&&clampU!=2)||clampU!=clampV||((fetch[3]>>19)&3)!=1||((fetch[3]>>21)&3)!=1||(fetch[3]&1)||((fetch[3]>>13)&63))throw std::runtime_error("Unsupported FXAA scene input or sampler");
    DrawData item;auto& draw=item.draw;draw.base_texture=draw.ramp_texture=UINT32_MAX;draw.flags=sourcePipeline(d);draw.raster=nativeRaster(d);std::copy_n(m.diffuse,3,draw.diffuse);sourceFxaaMaterial(d,draw,1280,720);
    auto resolved=resolveTextures.find(layout.address);
    if(resolved!=resolveTextures.end()&&resolved->second.frame!=UINT64_MAX&&resolved->second.type==DBZ_NATIVE_COPY_COLOR){draw.base_texture=resolved->second.id;draw.flags&=~DBZ_NATIVE_SAMPLE_SCENE;}
    else if(mainColorCleared)throw std::runtime_error("FXAA source has no preserved native resolve after clear");
    if(uint64_t(address)+uint64_t(count)*16>0x100000000ull)throw std::runtime_error("Postprocess vertex address overflow");
    std::vector<uint8_t> raw(count*16);if(!read(guest(base,address),raw.data(),raw.size()))throw std::runtime_error("Postprocess vertices unreadable");
    std::vector<DbzNativeFrameVertex> converted;std::vector<uint32_t> mesh;converted.reserve(count/3*4);mesh.reserve(count*2);
    for(uint32_t i=0;i<count;i+=3){uint32_t first=uint32_t(converted.size());auto a=sourcePostVertex(raw.data()+i*16),b=sourcePostVertex(raw.data()+(i+1)*16),c=sourcePostVertex(raw.data()+(i+2)*16),last=a;
     for(int k=0;k<2;++k){last.position[k]=b.position[k]+c.position[k]-a.position[k];last.uv[k]=b.uv[k]+c.uv[k]-a.uv[k];}
     converted.insert(converted.end(),{a,b,c,last});for(uint32_t index:{0u,1u,2u,2u,1u,3u})mesh.push_back(first+index);
    }
    append(draw,std::move(item),converted,mesh);++acceptedUp;++kinds[16];return;
   }
   if(primitive==8)throw std::runtime_error("Unsupported UP primitive");
   if(m.kind!=10&&m.kind!=11&&m.kind!=13&&m.kind!=15&&m.kind!=22)throw std::runtime_error("Unsupported UP material family");
   if(stride!=(m.kind==15?16u:24u))throw std::runtime_error("UP material vertex layout mismatch");
   uint32_t pitch=sourceWord(d+10368)&0x3fff;bool mask=m.kind==15&&pitch==560;
   DrawData item;auto& draw=item.draw;draw.flags=sourcePipeline(d)|(mask?DBZ_NATIVE_TARGET_MASK:0)|(primitive==3?DBZ_NATIVE_LINE_LIST:0);draw.raster=nativeRaster(d);std::copy_n(m.diffuse,3,draw.diffuse);
   if(!mask)colorTarget(d,draw);
   draw.base_texture=draw.ramp_texture=UINT32_MAX;sourceSpriteMaterial(d,m.kind,draw);
   uint64_t bytes=uint64_t(count)*stride;if(uint64_t(address)+bytes>0x100000000ull)throw std::runtime_error("UP vertex address overflow");
   std::vector<uint8_t> raw(static_cast<size_t>(bytes));if(!read(guest(base,address),raw.data(),raw.size()))throw std::runtime_error("UP vertices unreadable");
   std::vector<DbzNativeFrameVertex> converted(count);for(uint32_t i=0;i<count;++i)sourceSpriteVertex(raw.data()+i*stride,d,m.kind,draw,converted[i]);
   std::vector<uint32_t> mesh;if(primitive==3){mesh.reserve((count-1)*2);for(uint32_t i=0;i+1<count;++i){mesh.push_back(i);mesh.push_back(i+1);}}
   else if(primitive==4){mesh.resize(count);for(uint32_t i=0;i<count;++i)mesh[i]=i;}
   else if(primitive==5){mesh.reserve((count-2)*3);for(uint32_t i=1;i+1<count;++i){mesh.push_back(0);mesh.push_back(i);mesh.push_back(i+1);}}
   else{mesh.reserve((count-2)*3);for(uint32_t i=0;i+2<count;++i){mesh.push_back(i+(i&1));mesh.push_back(i+1-(i&1));mesh.push_back(i+2);}}
   if(m.kind==13||m.kind==22)video(d,base,draw);else if(m.kind!=15)draw.base_texture=texture(d,0,base);
   append(draw,std::move(item),converted,mesh);++acceptedUp;++kinds[m.kind];
  }catch(const NativeFailure&){throw;}catch(const std::exception& e){++rejectedUp;++upReasons[e.what()];diagnose(base,dev,1,primitive,count,address,stride,e.what());}
 }
 void status(){
  auto safe=[](std::string text){for(auto& c:text){if(c=='"')c='\'';else if(c=='\\')c='/';else if(static_cast<unsigned char>(c)<32)c=' ';}return text;};
  std::ofstream f("native-stream-status.json");f<<"{\"scope\":\"Native D3D12 dynamic frontend; unsupported calls counted. Presentation and guest service mode are recorded separately\",\"process_id\":"<<GetCurrentProcessId()<<",\"native_frames\":"<<frames<<",\"frames_with_draws\":"<<sceneFrames<<",\"frames_with_indexed_draws\":"<<indexedFrames<<",\"changed_pose_or_geometry_frames\":"<<changed<<",\"observed_indexed_draws\":"<<observed<<",\"accepted_indexed_draws\":"<<accepted<<",\"rejected_indexed_draws\":"<<rejected<<",\"max_draws_in_frame\":"<<maximum<<",\"max_indexed_draws_in_frame\":"<<maxIndexed<<",\"observed_up_draws\":"<<observedUp<<",\"accepted_up_draws\":"<<acceptedUp<<",\"rejected_up_draws\":"<<rejectedUp<<",\"new_source_topologies\":"<<newTopologies<<",\"registered_textures\":"<<registeredTextures<<",\"skinned_draws\":"<<skinnedDraws<<",\"material_draw_counts\":[";
  for(size_t i=0;i<kinds.size();++i){if(i)f<<',';f<<kinds[i];}f<<"],\"rejections\":{";bool comma=false;for(const auto& [reason,count]:reasons){if(comma)f<<',';comma=true;f<<'"'<<safe(reason)<<"\":"<<count;}f<<"},\"up_rejections\":{";comma=false;for(const auto& [reason,count]:upReasons){if(comma)f<<',';comma=true;f<<'"'<<safe(reason)<<"\":"<<count;}f<<"},\"unknown_materials\":[";
  comma=false;for(const auto& [key,count]:unknownMaterials){if(comma)f<<',';comma=true;f<<"{\"vs_fnv\":\""<<std::hex<<key[0]<<"\",\"ps_fnv\":\""<<key[1]<<"\",\"vs_bytes\":"<<std::dec<<key[2]<<",\"ps_bytes\":"<<key[3]<<",\"draws\":"<<count<<'}';}
  DbzNativeStats stats{};getStats(renderer,&stats);
  f<<"],\"native_service_counts\":{\"depth_stencil_restores\":"<<depthRestoreServices<<",\"depth_stencil_resamples\":"<<depthResampleServices<<",\"draws_without_writes\":"<<noEffectServices<<"},\"native_offscreen_targets\":"<<targets.size();
  f<<",\"timing_ns\":{\"source_cpu\":"<<sourceCpuNs<<",\"frame_upload\":"<<uploadNs<<",\"render_submit_present\":"<<renderPresentNs<<"}";
  f<<",\"source_texture_cache\":{\"hits\":"<<sourceTextureHits<<",\"decodes\":"<<sourceTextureDecodes<<",\"bytes\":"<<sourceTextureBytes<<"}";
  f<<",\"observed_resolves\":"<<observedResolves<<",\"accepted_resolves\":"<<acceptedResolves<<",\"rejected_resolves\":"<<rejectedResolves<<",\"resolve_rejections\":{";comma=false;for(const auto& [reason,count]:resolveReasons){if(comma)f<<',';comma=true;f<<'"'<<safe(reason)<<"\":"<<count;}f<<"},\"resolve_texture_slots\":"<<resolveTextures.size();
  f<<",\"unknown_material_overflow\":"<<unknownOverflow<<",\"video_frames\":"<<videoFrames<<",\"video_texture_slots\":"<<videoSlots.size()<<",\"video_texture_updates\":"<<videoTextureUpdates<<",\"renderer_resources\":{\"owned_objects\":"<<stats.owned_objects<<",\"pipeline_count\":"<<stats.pipeline_count<<",\"texture_uploads\":"<<stats.texture_uploads<<"},\"error\":\""<<safe(failure)<<"\"}\n";
 }
 void present(){
  if(!renderer)return;FpuScope fpu;std::vector<DbzNativeFrameDraw> frame;frame.reserve(draws.size());uint64_t pose=hash(vertices.data(),vertices.size()*sizeof(vertices[0]));
  for(auto& item:draws){item.draw.palette=item.palette.empty()?nullptr:item.palette.data();frame.push_back(item.draw);pose^=hash(item.palette.data(),item.palette.size()*4)+hash(item.draw.view_projection,64);}
  DbzNativeClear clear{{.025f,.038f,.07f,1},1,1,7};
  {MeasuredScope measured{uploadNs};check(setFrame(renderer,vertices.data(),uint32_t(vertices.size()),indices.data(),uint32_t(indices.size()),frame.data(),uint32_t(frame.size()),&clear));check(setOperations(renderer,operations.data(),uint32_t(operations.size())));}
  {MeasuredScope measured{renderPresentNs};check(render(renderer));}++frames;
  if(nextVideoSlot){++videoFrames;if(videoFrames==1||videoFrames==15||videoFrames==60){std::string name="native-video-stream-"+std::to_string(videoFrames)+".ppm";check(capture(renderer,name.c_str()));status();}}
  if(frameIndexed){++indexedFrames;maxIndexed=std::max(maxIndexed,frameIndexed);if(indexedFrames==1||indexedFrames==15||indexedFrames==60){std::string name="native-indexed-stream-"+std::to_string(indexedFrames)+".ppm";check(capture(renderer,name.c_str()));status();}}
  maximum=std::max(maximum,uint64_t(draws.size()));if(!draws.empty()){
   if(sceneFrames&&pose!=previous)++changed;previous=pose;++sceneFrames;
   if(sceneFrames==1||sceneFrames==15||sceneFrames==30||sceneFrames==60){std::string name="native-stream-"+std::to_string(sceneFrames)+".ppm";check(capture(renderer,name.c_str()));status();}
  }
  vertices.clear();indices.clear();draws.clear();operations.clear();depthOwners.clear();maskProduced=false;frameIndexed=0;nextVideoSlot=0;mainColorCleared=false;if(frames%300==0)status();
  if(frames%30==0){std::error_code ec;if(std::filesystem::exists("native-stream-capture.req",ec)){check(capture(renderer,"native-stream-requested.ppm"));std::filesystem::remove("native-stream-capture.req",ec);status();}}
 }
};
std::mutex mutex;std::unique_ptr<State> state;bool initialized=false,disabled=false;
void fail(const std::exception& e){disabled=true;if(state){state->failure=e.what();state->status();}else std::ofstream("native-stream-error.txt")<<e.what()<<'\n';}
}
void streamSetMemoryWatch(void* context,StreamMemoryArm arm,StreamMemoryEpoch epoch){std::lock_guard lock(mutex);memoryWatch={context,arm,epoch};}
void streamObserve(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t start,uint32_t count,uint8_t* base) noexcept {std::lock_guard lock(mutex);if(disabled)return;try{if(!initialized){initialized=true;state=std::make_unique<State>();}state->observe(device,primitive,vertexBase,start,count,base);}catch(const std::exception& e){fail(e);}}
void streamObserveUp(uint32_t device,uint32_t primitive,uint32_t count,uint32_t vertices,uint32_t stride,uint8_t* base) noexcept {std::lock_guard lock(mutex);if(disabled)return;try{if(!initialized){initialized=true;state=std::make_unique<State>();}state->observeUp(device,primitive,count,vertices,stride,base);}catch(const std::exception& e){fail(e);}}
void streamResolve(uint32_t device,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t sourceRect,uint32_t destinationPoint) noexcept {std::lock_guard lock(mutex);if(disabled)return;try{if(!initialized){initialized=true;state=std::make_unique<State>();}state->resolve(device,flags,destination,base,sourceRect,destinationPoint);}catch(const std::exception& e){fail(e);}}
void streamPresent() noexcept {std::lock_guard lock(mutex);if(state&&!disabled)try{state->present();}catch(const std::exception& e){fail(e);}}
void streamShutdown() noexcept {std::lock_guard lock(mutex);if(state){state->status();state.reset();}disabled=true;}
void streamStatus() noexcept {std::lock_guard lock(mutex);if(state)state->status();}
bool streamCapture(const char* path) noexcept {std::lock_guard lock(mutex);if(!state||!state->renderer||disabled)return false;try{FpuScope fpu;state->check(state->capture(state->renderer,path));return true;}catch(const std::exception& e){fail(e);return false;}}
}
