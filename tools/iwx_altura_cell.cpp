// Lectura real del ELF del jugador y rechazos de entradas incompatibles.
#include "../src/iw_seguimiento.h"
#include <fstream>
#include <iterator>

int main(int argc,char** argv) {
  if(argc!=2)return 2;
  std::ifstream f(argv[1],std::ios::binary);
  if(!f)return 2;
  std::vector<uint8_t> elf((std::istreambuf_iterator<char>(f)),{});
  using dbz3::iwseguimiento::AlturaDescriptorEU;
  float goku=0,cell=0;
  if(!AlturaDescriptorEU(elf,0,goku)||!AlturaDescriptorEU(elf,33,cell)||goku!=9||cell!=10)return 1;
  unsigned guardas=0;
  auto rechaza=[&](const std::vector<uint8_t>& e,int jugador=33) {
    float sin_cambio=123;
    const bool ok=!AlturaDescriptorEU(e,jugador,sin_cambio)&&sin_cambio==123;
    guardas+=ok?1:0;return ok;
  };
  auto mala=elf;mala[4]=2;if(!rechaza(mala))return 1;
  mala=elf;mala[28]=mala[29]=mala[30]=mala[31]=255;if(!rechaza(mala))return 1;
  mala.assign(elf.begin(),elf.begin()+51);if(!rechaza(mala))return 1;
  if(!rechaza(elf,100))return 1;
  auto offset=[&](uint32_t va) {
    auto u=[&](size_t o){uint32_t r;std::memcpy(&r,elf.data()+o,4);return r;};
    for(size_t i=0,ph=u(28),paso=elf[42]|(unsigned(elf[43])<<8),n=elf[44]|(unsigned(elf[45])<<8);i<n;++i) {
      const size_t h=ph+i*paso;
      if(u(h)==1&&va>=u(h+8)&&uint64_t(va)+4<=uint64_t(u(h+8))+u(h+16))return size_t(u(h+4)+va-u(h+8));
    }
    return elf.size();
  };
  const auto tabla=offset(0x44FE10+33*4),altura=offset(0x44F0F0+12);
  if(tabla+4>elf.size()||altura+4>elf.size())return 1;
  mala=elf;mala[tabla]^=1;if(!rechaza(mala))return 1;
  mala=elf;const uint32_t nan=0x7FC00000;std::memcpy(mala.data()+altura,&nan,4);if(!rechaza(mala))return 1;
  std::printf("{\"goku\":%.9g,\"cell\":%.9g,\"guardas\":%u,\"respaldo_intacto\":true}\n",goku,cell,guardas);
  return guardas==6?0:1;
}
