// Opt-in CPU state edges around the original driver call. Private diagnostics.
#pragma once
#include <windows.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace dbzlab {
class InputStateAudit {
 uint8_t* base;uint32_t device,category;std::vector<uint8_t> before;std::string prefix;
 static bool enabled(){static bool yes=[](){auto* value=std::getenv("DBZ3_NATIVE_INPUT_AUDIT");return value&&std::string(value)=="1";}();return yes;}
 bool read(uint32_t address,void* data,size_t size){SIZE_T done=0;return ReadProcessMemory(GetCurrentProcess(),base+uint64_t(address)+(address>=0xe0000000u?0x1000u:0u),data,size,&done)&&done==size;}
 static uint32_t word(const std::vector<uint8_t>& data,uint32_t offset){uint32_t value;std::memcpy(&value,data.data()+offset,4);return _byteswap_ulong(value);}
 static void file(const std::string& path,const std::vector<uint8_t>& bytes){std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
 std::vector<uint8_t> programs(const std::vector<uint8_t>& cpu){
  std::vector<uint8_t> result(32);
  for(uint32_t kind=0;kind<2;++kind){uint32_t object=word(cpu,kind?13068:13072);if(object<0x10000)continue;
   uint32_t fields[4]={kind?24u:32u,kind?40u:872u,kind?44u:876u,kind?64u:896u};uint32_t variant=0;read(object+fields[3],&variant,4);variant=_byteswap_ulong(variant);if(variant<=32768){fields[1]+=variant;fields[2]+=variant;}for(uint32_t k=0;k<4;++k)read(object+fields[k],result.data()+kind*16+k*4,4);
  }
  return result;
 }
public:
 InputStateAudit(uint8_t* memory,uint32_t address,uint32_t type):base(memory),device(address),category(type){
  if(!enabled())return;before.resize(0x6400);if(!read(device,before.data(),before.size())){before.clear();return;}
  static std::mutex mutex;static std::set<std::array<uint32_t,8>> seen;
  std::lock_guard lock(mutex);std::array<uint32_t,8> key{category,word(before,13072),word(before,13068),word(before,10368),word(before,10372),word(before,10376),word(before,0x480),word(before,0x48c)};
  if(seen.size()>=256||!seen.insert(key).second){before.clear();return;}
  auto folder="lab-input-audit/"+std::to_string(GetCurrentProcessId());std::filesystem::create_directories(folder);prefix=folder+"/state-"+std::to_string(seen.size());
  file(prefix+"-programs-before.bin",programs(before));
 }
 ~InputStateAudit(){
  if(before.empty())return;std::vector<uint8_t> after(before.size());if(!read(device,after.data(),after.size()))return;
  file(prefix+"-before.bin",before);file(prefix+"-after.bin",after);file(prefix+"-programs-after.bin",programs(after));
  std::ofstream(prefix+".json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"category\":"<<category<<",\"device\":"<<device<<"}\n";
 }
};
}
