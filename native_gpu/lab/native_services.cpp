// Own guest service adapter. Legacy ring storage is retired, never interpreted.
// CPU mesh/asset inputs have been copied into native-owned IR before recycling.
// Adapted runtime thread/MMIO integration follows ReXGlue's BSD-licensed API.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/chrono/clock.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "native_services.h"
#include "native_memory_watch.h"
#include "native_stream.h"
namespace dbzlab {namespace {
class Services;
std::atomic<Services*> active=nullptr;
class Services final:public rex::system::IGraphicsSystem {
 rex::runtime::FunctionDispatcher* dispatcher=nullptr;
 rex::memory::Memory* memory=nullptr;
 rex::system::object_ref<rex::system::XHostThread> timer;
 std::atomic<bool> running=false;
 bool presentation=false;
 std::atomic<uint32_t> callback=0,userData=0,readback=0,writePointer=0;
 std::atomic<uint32_t> ringBase=0,ringLog2=0;
 std::atomic<uint64_t> ticks=0,interrupts=0,bufferLabels=0,displayLabels=0,ringRetirements=0,invalidLabels=0;
 std::array<std::atomic<uint32_t>,0x4000> registers{};
 std::mutex statusMutex,unknownMutex;
 std::mutex paceMutex,auditMutex;
 std::condition_variable paceEvent;
 uint64_t lastPresentedVblank=0;
 uint32_t auditedBuffers=0;
 uint32_t auditedCounters=0;
 std::atomic<uint64_t> pacedFrames=0;
 std::map<uint32_t,uint64_t> unknownReads,unknownWrites;
 std::unique_ptr<PhysicalWriteEpochs> writeEpochs;
public:
 ~Services() override {Shutdown();}
 uint32_t counter() const {return uint32_t(ticks.load());}
 bool write(uint8_t* base,uint32_t address,uint32_t value,uint32_t endian=2){
  if(address<0x10000||(address&3)||address>0xfffffffbu){++invalidLabels;return false;}
  switch(endian){case 0:break;case 1:value=((value&0x00ff00ffu)<<8)|((value&0xff00ff00u)>>8);break;case 2:value=_byteswap_ulong(value);break;case 3:value=(value<<16)|(value>>16);break;default:++invalidLabels;return false;}SIZE_T done=0;
  auto* p=base+uint64_t(address)+(address>=0xe0000000u?0x1000u:0u);
  bool result=WriteProcessMemory(GetCurrentProcess(),p,&value,4,&done)&&done==4;
  if(!result)++invalidLabels;return result;
 }
 void complete(uint8_t* base,uint32_t label,uint32_t serial,uint32_t cursor){
  if(write(base,label+4,cursor)){MemoryBarrier();if(write(base,label,serial))++bufferLabels;}
 }
 void display(uint8_t* base,uint32_t label,uint32_t endian,uint32_t begin,uint32_t end){
  uint32_t value=counter();if(!write(base,label&~3u,value,endian))return;++displayLabels;
  std::lock_guard lock(auditMutex);if(auditedCounters>=64||end<=begin||end-begin>512||(end-begin)%4)return;
  auto guest=[&](uint32_t address){return base+uint64_t(address)+(address>=0xe0000000u?0x1000u:0u);};SIZE_T done=0;uint32_t actual=0;std::vector<uint8_t> bytes(end-begin);
  if(!ReadProcessMemory(GetCurrentProcess(),guest(begin+4),bytes.data(),bytes.size(),&done)||done!=bytes.size()||!ReadProcessMemory(GetCurrentProcess(),guest(label&~3u),&actual,4,&done)||done!=4)return;
  auto prefix="native-service-counter-"+std::to_string(auditedCounters++);
  std::ofstream(prefix+"-emission.bin",std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  std::ofstream(prefix+".json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"label\":"<<(label&~3u)<<",\"endian\":"<<endian<<",\"counter\":"<<value<<",\"actual_host_word\":"<<actual<<"}\n";
 }
 void pace(){std::unique_lock lock(paceMutex);paceEvent.wait(lock,[&](){return !running||ticks.load()>lastPresentedVblank;});lastPresentedVblank=ticks.load();++pacedFrames;}
 void audit(uint8_t* base,uint32_t device,uint32_t begin,uint32_t end,uint32_t label,uint32_t serial,uint32_t cursor){
  std::lock_guard lock(auditMutex);if(auditedBuffers>=64)return;
  if(end<=begin||end-begin>512||(end-begin)%4)return;
  std::vector<uint8_t> bytes(end-begin),cpu(0x6400);SIZE_T done=0;
  auto guest=[&](uint32_t address){return base+uint64_t(address)+(address>=0xe0000000u?0x1000u:0u);};
  if(!ReadProcessMemory(GetCurrentProcess(),guest(begin+4),bytes.data(),bytes.size(),&done)||done!=bytes.size()||!ReadProcessMemory(GetCurrentProcess(),guest(device),cpu.data(),cpu.size(),&done)||done!=cpu.size())return;
  uint32_t actual[2]{};if(!ReadProcessMemory(GetCurrentProcess(),guest(label),actual,8,&done)||done!=8)return;
  auto prefix="native-service-fence-"+std::to_string(auditedBuffers++);
  std::ofstream(prefix+"-emission.bin",std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  std::ofstream(prefix+"-device.bin",std::ios::binary).write(reinterpret_cast<const char*>(cpu.data()),cpu.size());
  std::ofstream(prefix+".json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"device\":"<<device<<",\"label\":"<<label<<",\"serial\":"<<serial<<",\"cursor\":"<<cursor<<",\"actual_serial\":"<<_byteswap_ulong(actual[0])<<",\"actual_cursor\":"<<_byteswap_ulong(actual[1])<<",\"returned_cursor\":"<<end<<"}\n";
 }
 rex::X_STATUS SetupPresentation(rex::ui::WindowedAppContext*) override {presentation=true;return 0;}
 bool has_presentation() const override {return presentation;}
 rex::X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* functions,rex::system::KernelState* kernel) override {
  dispatcher=functions;memory=functions->memory();active.store(this);
  if(auto* audit=std::getenv("DBZ3_NATIVE_MEMORY_AUDIT");audit&&std::string(audit)=="1"){
   writeEpochs=std::make_unique<PhysicalWriteEpochs>(*memory);streamSetMemoryWatch(writeEpochs.get(),PhysicalWriteEpochs::armThunk,PhysicalWriteEpochs::signatureThunk);
  }
  if(!memory->AddVirtualMappedRange(0x7fc80000,0xffff0000,0xffff,this,readThunk,writeThunk))return 0xc0000001u;
  running=true;
  timer=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(kernel,128*1024,0,[this](){
   using rex::chrono::Clock;uint64_t last=Clock::QueryGuestTickCount();const uint64_t interval=std::max(uint64_t(1),Clock::guest_tick_frequency()/60);
   while(running){
    auto now=Clock::QueryGuestTickCount();if(now-last>=interval){
     uint64_t elapsed=(now-last)/interval;last+=elapsed*interval;
     {std::lock_guard lock(paceMutex);ticks.fetch_add(elapsed);}paceEvent.notify_all();
     auto function=callback.load(std::memory_order_acquire);
     if(function){auto* thread=rex::system::XThread::GetCurrentThread();thread->SetActiveCpu(2);uint64_t args[]={0,userData.load()};dispatcher->ExecuteInterrupt(thread->thread_state(),function,args,2);++interrupts;}
     if(counter()%60==0)status();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   return 0;
  }));
  timer->set_name("Native GPU vblank");timer->Create();status();return 0;
 }
 void SetInterruptCallback(uint32_t address,uint32_t data) override {userData=data;callback.store(address,std::memory_order_release);status();}
 void InitializeRingBuffer(uint32_t address,uint32_t log2) override {ringBase=address;ringLog2=log2;status();}
 void EnableReadPointerWriteBack(uint32_t address,uint32_t) override {readback=address;retire(writePointer);status();}
 void retire(uint32_t value){
  writePointer=value;++ringRetirements;
  // Read-pointer writeback is a physical buffer ABI, not a guest command.
  uint32_t address=readback.load();if(address&&address<0x20000000u){auto* p=memory->TranslatePhysical<uint32_t*>(address);uint32_t swapped=_byteswap_ulong(value);std::memcpy(p,&swapped,4);}
  registers[0x1c4]=value;
 }
 static uint32_t readThunk(void*,void* context,uint32_t address){return static_cast<Services*>(context)->readRegister(address);}
 static void writeThunk(void*,void* context,uint32_t address,uint32_t value){static_cast<Services*>(context)->writeRegister(address,value);}
 uint32_t readRegister(uint32_t address){
  auto index=(address&0xffff)/4;
  switch(index){case 0xf00:return 0x08100748;case 0xf01:return 0x200e;case 0x194c:return 720;case 0x1951:return 1;case 0x1961:return (1280<<16)|720;case 0x1c4:return writePointer.load();default:break;}
  {std::lock_guard lock(unknownMutex);++unknownReads[index];}return registers[index].load();
 }
 void writeRegister(uint32_t address,uint32_t value){
  auto index=(address&0xffff)/4;registers[index]=value;
  if(index==0x1c5)retire(value);else if(index!=0x1844){std::lock_guard lock(unknownMutex);++unknownWrites[index];}
 }
 void status(){
  std::lock_guard lock(statusMutex);std::ofstream f("native-services-status.json");
  f<<"{\"scope\":\"Experimental native guest display/buffer service adapter. No guest GPU packet execution; high-level buffer ownership and display-counter callbacks.\",\"process_id\":"<<GetCurrentProcessId()<<",\"reference_gpu_active\":false,\"vblanks\":"<<ticks<<",\"interrupts\":"<<interrupts<<",\"paced_frames\":"<<pacedFrames<<",\"callback\":"<<callback<<",\"callback_data\":"<<userData<<",\"buffer_completions\":"<<bufferLabels<<",\"display_counter_writes\":"<<displayLabels<<",\"legacy_ring_retirements\":"<<ringRetirements<<",\"ring_base\":"<<ringBase<<",\"ring_size_log2\":"<<ringLog2<<",\"readback\":"<<readback<<",\"write_pointer\":"<<writePointer<<",\"invalid_label_writes\":"<<invalidLabels;
  if(writeEpochs)f<<",\"memory_write_audit\":{\"notifications\":"<<writeEpochs->writeCount()<<",\"arm_calls\":"<<writeEpochs->armCount()<<",\"errors\":"<<writeEpochs->errorCount()<<'}';
  std::lock_guard unknownLock(unknownMutex);auto output=[&](const char* name,const auto& values){f<<",\""<<name<<"\":{";bool comma=false;for(auto [key,value]:values){if(comma)f<<',';comma=true;f<<'"'<<key<<"\":"<<value;}f<<'}';};output("other_mmio_reads",unknownReads);output("other_mmio_writes",unknownWrites);f<<"}\n";
 }
 void Shutdown() override {
  if(running.exchange(false)&&timer){paceEvent.notify_all();timer->Wait(0,0,0,nullptr);timer.reset();status();}
  if(writeEpochs){streamSetMemoryWatch(nullptr,nullptr,nullptr);writeEpochs.reset();}
  auto* expected=this;active.compare_exchange_strong(expected,nullptr);
 }
};
}
bool nativeServicesEnabled(){static bool enabled=[](){auto* value=std::getenv("DBZ3_NATIVE_GRAPHICS");return value&&std::string(value)=="1";}();return enabled;}
std::unique_ptr<rex::system::IGraphicsSystem> createNativeServices(){return std::make_unique<Services>();}
void nativeCompleteBuffer(uint8_t* base,uint32_t label,uint32_t serial,uint32_t cursor){if(auto* services=active.load())services->complete(base,label,serial,cursor);}
void nativeWriteDisplayCounter(uint8_t* base,uint32_t label,uint32_t endian,uint32_t begin,uint32_t end){if(auto* services=active.load())services->display(base,label,endian,begin,end);}
void nativePaceFrame(){if(auto* services=active.load())services->pace();}
void nativeAuditBuffer(uint8_t* base,uint32_t device,uint32_t begin,uint32_t end,uint32_t label,uint32_t serial,uint32_t cursor){if(auto* services=active.load())services->audit(base,device,begin,end,label,serial,cursor);}
void nativeServicesStatus(){if(auto* services=active.load())services->status();}
}
