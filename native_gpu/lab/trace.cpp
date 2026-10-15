#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "trace.h"
#include "source_texture.h"
#include <rex/ppc/context.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace dbzlab {
namespace {
constexpr uint32_t first=0x82220000,last=0x82260000;
std::array<std::atomic<uint64_t>,(last-first)/4> calls{};
std::mutex mutex;
const std::string traceRoot="lab-trace/"+std::to_string(GetCurrentProcessId())+"/";
uint64_t frame=0;int remaining=0;uint32_t device=0;uint64_t event=0;
bool uiPending=false;
bool indexPending=false;
std::ofstream events;
int requestedFrames(){const char* value=std::getenv("DBZ3_CAPTURE_FRAMES");if(!value)return 3;char* end=nullptr;long count=std::strtol(value,&end,10);return end&&!*end&&count>=3&&count<=30?int(count):3;}
std::unordered_map<uint64_t,std::vector<std::filesystem::path>> blobs;
uint64_t writtenBytes=0,linkedBytes=0,linkedFiles=0;
void dump(const std::filesystem::path& path,const void* ptr,size_t size) {
    std::vector<uint8_t> bytes(size);SIZE_T done=0;
    if(!ReadProcessMemory(GetCurrentProcess(),ptr,bytes.data(),size,&done)||done!=size)return;
    uint64_t identity=14695981039346656037ull;
    if(size>=4096){
        for(uint8_t b:bytes){identity^=b;identity*=1099511628211ull;}identity^=size;
        for(const auto& original:blobs[identity]){
            std::ifstream f(original,std::ios::binary|std::ios::ate);
            if(!f||f.tellg()!=std::streampos(size))continue;
            std::vector<uint8_t> previous(size);f.seekg(0);f.read(reinterpret_cast<char*>(previous.data()),size);
            if(!f||previous!=bytes)continue;
            std::error_code error;std::filesystem::create_hard_link(original,path,error);
            if(!error){linkedBytes+=size;++linkedFiles;return;}
        }
    }
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),size);
    if(f){writtenBytes+=size;if(size>=4096)blobs[identity].push_back(path);}
}
uint32_t read32(const uint8_t* base,uint32_t address) {
    uint32_t value=0;SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base+address+(address>=0xe0000000u?0x1000u:0u),&value,4,&read)||read!=4)return 0;
    return _byteswap_ulong(value);
}
void shader(const std::string& prefix,uint32_t field,uint32_t dev,uint8_t* base){
    uint32_t object=read32(base,dev+field);if(object<0x10000)return;
    auto address=[&](uint32_t p){return base+p+(p>=0xe0000000u?0x1000u:0u);};
    dump(prefix+"-shader-"+std::to_string(field)+".bin",address(object),4096);
    bool vertex=field==13072;
    uint32_t variant=read32(base,object+(vertex?896:64));if(variant>32768)return;
    uint32_t header=object+(vertex?872:40)+variant;
    uint32_t offset=read32(base,header),size=read32(base,header+4);
    uint32_t program=read32(base,object+(vertex?32:24));
    if(program<0x10000||size==0||size>65536||uint64_t(program)+offset+size>0x100000000ull)return;
    dump(prefix+(vertex?"-vs-program.bin":"-ps-program.bin"),address(program+offset),size);
}
void saveCounts() {
    std::ofstream f(traceRoot+"calls.json");f<<"{\"frames\":"<<frame<<",\"capture_storage\":{\"written_bytes\":"<<writtenBytes<<",\"hardlinked_bytes\":"<<linkedBytes<<",\"hardlinked_files\":"<<linkedFiles<<"},\"calls\":[";bool comma=false;
    for(size_t i=0;i<calls.size();++i)if(auto count=calls[i].load(std::memory_order_relaxed)) {
        if(comma)f<<',';comma=true;
        f<<"{\"guest\":\""<<std::hex<<first+4*i<<std::dec<<"\",\"count\":"<<count<<'}';
    }
    f<<"]}\n";
}
void texture(const std::string& prefix,uint32_t slot,uint32_t dev,uint8_t* base){
    uint32_t fetch[6];for(uint32_t i=0;i<6;++i)fetch[i]=read32(base,dev+0x480+slot*24+i*4);
    std::ofstream meta(prefix+"-texture"+std::to_string(slot)+".json");meta<<'[';
    for(int i=0;i<6;++i){if(i)meta<<',';meta<<fetch[i];}meta<<']';
    try{
        if((fetch[0]&3)==0)fetch[0]|=2;
        auto layout=sourceTextureLayout(fetch);
        if(layout.address)dump(prefix+"-texture"+std::to_string(slot)+".bin",base+0x100000000ull+layout.address,layout.allocation);
    }catch(const std::exception&){/* Unsupported diagnostic assets stay explicit. */}
}
}
void enter(uint32_t address,PPCContext& ctx,uint8_t* base) {
    if(address<first||address>=last)return;
    calls[(address-first)/4].fetch_add(1,std::memory_order_relaxed);
    std::lock_guard lock(mutex);
    if(address==0x82237B30) {
        std::filesystem::create_directories(traceRoot);++frame;device=ctx.r3.u32;
        if(remaining>0) {
            uint32_t cursor=read32(base,device+0x30);
            dump(traceRoot+"device-"+std::to_string(frame)+".bin",base+device,0x6400);
            // Diagnostic command bytes identify higher-level call semantics.
            // They are never submitted by the native renderer.
            if(cursor>=0xA0000000u&&cursor<0xC0000000u)
                dump(traceRoot+"commands-"+std::to_string(frame)+".bin",base+(cursor-0x20000),0x20000);
            events<<"{\"frame\":"<<frame<<",\"cursor\":\""<<std::hex<<cursor<<std::dec<<"\"}\n";
            if(--remaining==0){events.close();saveCounts();}
        }
        std::error_code ec;
        uiPending=std::filesystem::exists("lab-ui.req",ec);
        indexPending=std::filesystem::exists("lab-indexed.req",ec);
        if(remaining==0&&std::filesystem::exists("lab-capture.req",ec)) {
            std::filesystem::rename("lab-capture.req",traceRoot+"request-"+std::to_string(frame)+".done",ec);
            if(!ec){remaining=requestedFrames();events.open(traceRoot+"events-"+std::to_string(frame)+".jsonl");event=0;}
        }
        if(frame%300==0)saveCounts();
    }
    if(address==0x8223A6B8&&remaining==0&&indexPending){
        std::error_code ec;std::filesystem::rename("lab-indexed.req",traceRoot+"indexed-request-"+std::to_string(frame)+".done",ec);
        if(!ec){indexPending=false;remaining=3;events.open(traceRoot+"events-indexed-"+std::to_string(frame)+".jsonl");event=0;}
    }
    if(address==0x8223A670&&remaining==0&&uiPending&&ctx.r7.u32==24){
        uint32_t format=read32(base,ctx.r3.u32+0x484)&63;
        float nearDepth=std::bit_cast<float>(read32(base,ctx.r3.u32+0x788));
        float farDepth=std::bit_cast<float>(read32(base,ctx.r3.u32+0x78c));
        if((format==6||format==18||format==19||format==20)&&nearDepth>=1&&nearDepth<=2&&farDepth>65000){
            std::error_code ec;std::filesystem::rename("lab-ui.req",traceRoot+"ui-request-"+std::to_string(frame)+".done",ec);
            if(!ec){uiPending=false;remaining=3;events.open(traceRoot+"events-ui-"+std::to_string(frame)+".jsonl");event=0;}
        }
    }
    if(remaining>0 && events && event++<30000) {
        events<<"{\"frame\":"<<frame<<",\"guest\":\""<<std::hex<<address<<"\",\"lr\":\""<<ctx.lr<<"\",\"args\":[\""<<ctx.r3.u32;
        for(auto v:{ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32})events<<"\",\""<<v;
        events<<"\"]}\n"<<std::dec;
    }
}
void indexed(uint32_t dev,uint32_t primitive,uint32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base){
    std::lock_guard lock(mutex);if(remaining<=0||count==0||count>1000000)return;
    std::string prefix=traceRoot+"indexed-"+std::to_string(frame)+"-"+std::to_string(++event);
    uint32_t vf0=read32(base,dev+0x778),vf1=read32(base,dev+0x77c);
    uint32_t vertexAddress=vf0&0x1ffffffcu,vertexBytes=((vf1>>2)&0xffffff)*4;
    if((vf0&3)!=3||!vertexBytes||vertexBytes>16*1024*1024||uint64_t(vertexAddress)+vertexBytes>0x20000000)return;
    uint32_t ib=read32(base,dev+12812);if(ib<0x10000)return;
    uint32_t ibFlags=read32(base,ib),indexGuest=read32(base,ib+24);
    uint32_t width=(ibFlags&0x80000000u)?4:2;uint64_t indexOffset=uint64_t(indexStart)*width;
    uint64_t indexPointer=uint64_t(indexGuest)+indexOffset;if(indexPointer>=0x100000000ull)return;
    uint32_t indexAddress=(uint32_t(indexPointer)&0x1fffffffu)+(indexPointer>=0xe0000000u?0x1000u:0u);
    if(indexAddress+uint64_t(count)*width>0x20000000)return;
    dump(prefix+"-vertices.bin",base+0x100000000ull+vertexAddress,vertexBytes);
    dump(prefix+"-indices.bin",base+0x100000000ull+indexAddress,size_t(count)*width);
    dump(prefix+"-device.bin",base+dev,0x6400);
    // Character programs use vf1 patched to fetch slot 94. The palette is
    // independent from the main 44-byte vertex stream at slot 95.
    uint32_t palette0=read32(base,dev+0x770),palette1=read32(base,dev+0x774);
    uint32_t paletteAddress=palette0&0x1ffffffcu,paletteBytes=((palette1>>2)&0xffffff)*4;
    if((palette0&3)==3&&paletteBytes&&paletteBytes<=65536&&uint64_t(paletteAddress)+paletteBytes<=0x20000000)
        dump(prefix+"-palette.bin",base+0x100000000ull+paletteAddress,paletteBytes);
    std::ofstream meta(prefix+".json");meta<<"{\"frame\":"<<frame<<",\"primitive\":"<<primitive<<",\"base_vertex\":"<<int32_t(vertexBase)
        <<",\"count\":"<<count<<",\"index_start\":"<<indexStart<<",\"index_stride\":"<<width<<",\"index_flags\":"<<ibFlags
        <<",\"vertex_address\":"<<vertexAddress<<",\"vertex_bytes\":"<<vertexBytes<<",\"vertex_endian\":"<<(vf1&3)
        <<",\"stride_hint\":"<<uint32_t(*(base+dev+12904))*4<<"}\n";
    for(uint32_t slot=0;slot<2;++slot)texture(prefix,slot,dev,base);
    for(uint32_t field:{13068u,13072u})shader(prefix,field,dev,base);
}
void up(uint32_t dev,uint32_t primitive,uint32_t count,uint32_t vertices,uint32_t stride,uint8_t* base){
    std::lock_guard lock(mutex);uint64_t bytes=uint64_t(count)*stride;
    if(remaining<=0||!count||stride<12||stride>128||bytes>4*1024*1024||uint64_t(vertices)+bytes>0x100000000ull)return;
    std::string prefix=traceRoot+"up-"+std::to_string(frame)+"-"+std::to_string(++event);
    dump(prefix+"-vertices.bin",base+vertices+(vertices>=0xe0000000u?0x1000u:0u),size_t(bytes));
    dump(prefix+"-device.bin",base+dev,0x6400);
    for(uint32_t slot=0;slot<8;++slot)texture(prefix,slot,dev,base);
    for(uint32_t field:{13068u,13072u})shader(prefix,field,dev,base);
    std::ofstream meta(prefix+".json");meta<<"{\"frame\":"<<frame<<",\"primitive\":"<<primitive<<",\"count\":"<<count<<",\"stride\":"<<stride<<",\"address\":"<<vertices<<",\"texture_fetch\":[";
    for(int i=0;i<6;++i){if(i)meta<<',';meta<<read32(base,dev+0x480+i*4);}meta<<"]}\n";
}
void resolve(uint32_t dev,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t sourceRect,uint32_t destinationPoint){
    std::lock_guard lock(mutex);if(remaining<=0)return;
    std::string prefix=traceRoot+"resolve-"+std::to_string(frame)+"-"+std::to_string(++event);
    dump(prefix+"-device.bin",base+dev,0x6400);
    if(destination>=0x10000)dump(prefix+"-destination.bin",base+destination+(destination>=0xe0000000u?0x1000u:0u),128);
    if(sourceRect)dump(prefix+"-source-rect.bin",base+sourceRect+(sourceRect>=0xe0000000u?0x1000u:0u),16);
    if(destinationPoint)dump(prefix+"-destination-point.bin",base+destinationPoint+(destinationPoint>=0xe0000000u?0x1000u:0u),8);
    std::ofstream meta(prefix+".json");meta<<"{\"frame\":"<<frame<<",\"flags\":"<<flags<<",\"destination\":"<<destination
        <<",\"copy_address\":"<<read32(base,dev+10780)<<",\"copy_pitch\":"<<read32(base,dev+10784)<<",\"depth_clear\":"<<read32(base,dev+10796)<<",\"source_rect\":"<<sourceRect<<",\"destination_point\":"<<destinationPoint<<"}\n";
}
}
