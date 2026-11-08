// EU299A70: referencia previa a la actualizacion del actor, medida por campo PAL.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace dbz3::iwseguimiento {
// Descriptores EU observados en actor+BB8: Goku (0), Cell (33).
// No interpretar otra revision con estas direcciones ni cambiar el respaldo.
inline bool AlturaDescriptorEU(const std::vector<uint8_t>& elf,int jugador,float& altura) {
  if(elf.size()<52 || std::memcmp(elf.data(),"\x7f" "ELF\x01\x01",6) ||
      elf[18]!=8 || elf[19]!=0 || (jugador!=0 && jugador!=33))return false;
  auto u=[&](size_t o)->uint32_t {
    return o<=elf.size()-4 ? uint32_t(elf[o])|(uint32_t(elf[o+1])<<8)|
        (uint32_t(elf[o+2])<<16)|(uint32_t(elf[o+3])<<24) : 0;
  };
  const size_t ph=u(28),paso=elf[42]|(unsigned(elf[43])<<8),n=elf[44]|(unsigned(elf[45])<<8);
  if(paso<32 || n>1024 || uint64_t(ph)+uint64_t(paso)*n>elf.size())return false;
  auto leer=[&](uint32_t va,uint32_t& valor)->bool {
    for(size_t i=0;i<n;++i) {
      const size_t h=ph+i*paso;
      if(u(h)!=1 || va<u(h+8) || uint64_t(va)+4>uint64_t(u(h+8))+u(h+16))continue;
      const uint64_t o=uint64_t(u(h+4))+va-u(h+8);
      if(o+4>elf.size())return false;
      valor=u(size_t(o));return true;
    }
    return false;
  };
  uint32_t goku=0,cell=0,descriptor=0,bits=0;
  if(!leer(0x44FE10,goku) || goku!=0x44E670 || !leer(0x44FE10+33*4,cell) || cell!=0x44F0F0 ||
      !leer(0x44FE10+uint32_t(jugador)*4,descriptor) || !leer(descriptor+12,bits))return false;
  float valor;std::memcpy(&valor,&bits,4);
  if(!std::isfinite(valor) || valor<=0 || valor>64)return false;
  altura=valor;return true;
}
struct V3 { float x=0,y=0,z=0; };
struct Memoria {
  V3 previa;
  bool hay=false;
  V3 Elegir(V3 actual,bool reiniciar) {
    const V3 usada=hay&&!reiniciar?previa:actual;
    previa=actual;hay=true;
    return usada;
  }
};
inline V3 Mezclar(V3 anterior,V3 destino,float k) {
  return {anterior.x+(destino.x-anterior.x)*k,
           anterior.y+(destino.y-anterior.y)*k,
           anterior.z+(destino.z-anterior.z)*k};
}
// Sonda de solo lectura, separada del filtro: permite reconstruir el punto
// publicado sin usar el cache de anclas del guest (anterior al Poner del port).
inline void MedirAncla(const char* ruta,int estado,int indice,V3 publicada,V3 cache,V3 local,
                       const float matriz[16]) {
  struct Archivo {
    FILE* f=nullptr;bool intento=false;unsigned n=0;
    ~Archivo(){if(f)std::fclose(f);}
  };
  static Archivo a;
  if(!ruta||!*ruta)return;
  if(!a.intento) {
    a.intento=true;a.f=std::fopen(ruta,"wx");
    if(a.f)std::fputs("tick,estado,indice,publicadax,publicaday,publicadaz,cachex,cachey,cachez,localx,localy,localz,m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15\n",a.f);
  }
  if(!a.f||a.n>=4096)return;
  std::fprintf(a.f,"%u,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
      ++a.n,estado,indice,publicada.x,publicada.y,publicada.z,cache.x,cache.y,cache.z,
      local.x,local.y,local.z);
  for(int i=0;i<16;++i)std::fprintf(a.f,",%.9g",matriz[i]);
  std::fputc('\n',a.f);std::fflush(a.f);
}
// Solo se llama bajo TEST_IWX_SEGUIMIENTO y DBZ3_TEST_IWMINI; no es log normal.
inline void Medir(const char* ruta,int estado,bool raiz,bool reiniciar,V3 actual,V3 usada,V3 antes,V3 despues) {
  struct Archivo {
    FILE* f=nullptr;bool intento=false;unsigned n=0;
    ~Archivo(){if(f)std::fclose(f);}
  };
  static Archivo a;
  if(!a.intento) {
    a.intento=true;a.f=std::fopen(ruta,"wx");
    if(a.f)std::fputs("tick,estado,raiz,reiniciar,actualx,actualy,actualz,usadax,usaday,usadaz,antesx,antesy,antesz,despuesx,despuesy,despuesz\n",a.f);
  }
  if(!a.f||a.n>=4096)return;
  std::fprintf(a.f,"%u,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
      ++a.n,estado,int(raiz),int(reiniciar),actual.x,actual.y,actual.z,
      usada.x,usada.y,usada.z,antes.x,antes.y,antes.z,despues.x,despues.y,despues.z);
  std::fflush(a.f);
}
} // namespace dbz3::iwseguimiento
