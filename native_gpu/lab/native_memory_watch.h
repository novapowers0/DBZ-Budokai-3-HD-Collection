// Optional CPU-write ownership diagnostics. No framebuffer bytes are read.
#pragma once
#include <rex/memory.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <stdexcept>
namespace dbzlab {
class PhysicalWriteEpochs {
 static constexpr uint32_t limit=0x20000000,page=4096;
 rex::memory::Memory& memory;void* callback=nullptr;
 std::array<std::atomic<uint64_t>,limit/page> epochs{};
 std::atomic<uint64_t> writes{0},arms{0},errors{0};
 // Accessed only by the serialized frontend; callbacks touch atomics only.
 std::map<std::array<uint32_t,2>,uint64_t> armed;
 static std::pair<uint32_t,uint32_t> changed(void* context,uint32_t address,uint32_t length,bool){
  auto& self=*static_cast<PhysicalWriteEpochs*>(context);
  uint64_t end=std::min(uint64_t(limit),uint64_t(address)+length);
  if(address>=end)return {0,UINT32_MAX};
  uint64_t serial=self.writes.fetch_add(1,std::memory_order_acq_rel)+1;
  for(uint32_t p=address/page;p<(end+page-1)/page;++p)self.epochs[p].store(serial,std::memory_order_release);
  return {address,uint32_t(end-address)};
 }
public:
 explicit PhysicalWriteEpochs(rex::memory::Memory& source):memory(source){callback=memory.RegisterPhysicalMemoryInvalidationCallback(changed,this);if(!callback)throw std::runtime_error("Register native CPU write audit");}
 ~PhysicalWriteEpochs(){if(callback)memory.UnregisterPhysicalMemoryInvalidationCallback(callback);}
 uint64_t signature(uint32_t address,uint32_t length) const {
  uint64_t end=uint64_t(address)+length;if(!length||address>=limit||end>limit)return 0;
  uint64_t result=0;for(uint32_t p=address/page;p<(end+page-1)/page;++p)result=std::max(result,epochs[p].load(std::memory_order_acquire));return result;
 }
 void arm(uint32_t address,uint32_t length){
  if(!length||address>=limit||uint64_t(address)+length>limit)return;
  std::array<uint32_t,2> key{address,length};auto epoch=signature(address,length);
  auto found=armed.find(key);if(found!=armed.end()&&found->second==epoch)return;
  memory.EnablePhysicalMemoryAccessCallbacks(address,length,true,false);armed[key]=epoch;++arms;
 }
 uint64_t writeCount() const {return writes.load(std::memory_order_acquire);}
 uint64_t armCount() const {return arms.load(std::memory_order_acquire);}
 uint64_t errorCount() const {return errors.load(std::memory_order_acquire);}
 static void armThunk(void* context,uint32_t address,uint32_t length) noexcept {auto& self=*static_cast<PhysicalWriteEpochs*>(context);try{self.arm(address,length);}catch(...){++self.errors;}}
 static uint64_t signatureThunk(void* context,uint32_t address,uint32_t length){return static_cast<PhysicalWriteEpochs*>(context)->signature(address,length);}
};
}
