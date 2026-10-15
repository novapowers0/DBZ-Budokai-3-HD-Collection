// Isolated CPU-memory API contract. No guest/game/graphics plugin is loaded.
#include <rex/memory.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>
#include "native_memory_watch.h"
struct Watch {
 std::atomic<uint64_t> calls{0};uint32_t first=0,length=0;
 static std::pair<uint32_t,uint32_t> changed(void* context,uint32_t first,uint32_t length,bool){
  auto& watch=*static_cast<Watch*>(context);watch.first=first;watch.length=length;++watch.calls;return {first,length};
 }
};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
 try{
  rex::memory::Memory memory;check(memory.Initialize(),"Initialize memory");
  uint32_t address=memory.SystemHeapAlloc(8192,4096,rex::memory::kSystemHeapPhysical);check(address!=0,"Allocate physical pages");
  uint32_t physical=memory.GetPhysicalAddress(address);check(physical<0x20000000&&!(physical&4095),"Physical address");
  auto* bytes=memory.TranslateVirtual<volatile uint8_t*>(address);bytes[0]=1;bytes[4096]=2;
  Watch watch;void* handle=memory.RegisterPhysicalMemoryInvalidationCallback(Watch::changed,&watch);check(handle!=nullptr,"Register callback");
  memory.EnablePhysicalMemoryAccessCallbacks(physical,8192,true,false);
  uint8_t value=bytes[0];check(value==1&&watch.calls==0,"Reads preserve write watch");
  bytes[0]=3;check(watch.calls==1,"First guest virtual write notifies");
  check(watch.first<=physical&&uint64_t(watch.first)+watch.length>physical,"Correct invalidation region");
  bytes[1]=4;check(watch.calls==1,"Invalidation is one-shot for touched page");
  bytes[4096]=5;check(watch.calls==2,"Second page has independent write watch");
  memory.EnablePhysicalMemoryAccessCallbacks(physical,4096,true,false);bytes[2]=6;check(watch.calls==3,"Rearm notifies again");
  memory.EnablePhysicalMemoryAccessCallbacks(physical,4096,true,false);
  auto* backing=memory.TranslatePhysical<volatile uint8_t*>(physical);backing[3]=7;check(watch.calls==3,"Host physical provider writes do not notify");
  bytes[4]=8;check(watch.calls==4,"Guest virtual view remains watched after physical provider write");
  memory.UnregisterPhysicalMemoryInvalidationCallback(handle);
  auto epochs=std::make_unique<dbzlab::PhysicalWriteEpochs>(memory);epochs->arm(physical,8192);epochs->arm(physical,8192);check(epochs->armCount()==1,"Unchanged arm cache");
  auto old=epochs->signature(physical,8192);bytes[16]=9;check(epochs->signature(physical,8192)>old,"Epoch changes on guest CPU write");check(epochs->signature(physical+4096,4096)==0,"Unrelated page epoch unchanged");
  old=epochs->signature(physical,8192);epochs->arm(physical,8192);check(epochs->armCount()==2,"Changed epoch rearms pages");bytes[4097]=10;check(epochs->signature(physical,8192)>old,"Later page write changes combined epoch");
  epochs.reset();memory.SystemHeapFree(address);
  std::ofstream("physical-write-watch-contracts.json")<<"{\"scope\":\"Isolated ReX CPU memory notification contract, no game or graphics plugin.\",\"callbacks\":4,\"reads_preserve_watch\":true,\"one_shot_pages\":true,\"rearm\":true,\"physical_provider_write_does_not_notify\":true}\n";
  std::cout<<"Physical write-watch CPU contracts passed\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
