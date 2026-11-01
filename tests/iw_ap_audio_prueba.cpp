// Prueba sin juego ni salida sonora. El runner extrae iw_iop.h literalmente
// (solo sustituye sus includes de interfaz) a DBZ3_IW_IOP_CORE.
#include "../src/iw_ap_eventos.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <stdexcept>
#include <string>

namespace dbz3::pasarela {
std::vector<uint8_t> HijoAmb(const std::vector<uint8_t>& b,uint32_t k) {
  using namespace anim::apev;
  if (U32(b,0)!=0x424D4123 || k>=U32(b,16)) return {};
  const size_t t=U32(b,20)+size_t(k)*16;
  if (!Rango(b,t,8)) return {};
  const size_t o=U32(b,t),n=U32(b,t+4);
  if (!Rango(b,o,n)) return {};
  return {b.begin()+o,b.begin()+o+n};
}
}
namespace dbz3::settings { bool AudioMute() {return true;} double MasterVolume() {return 0;} }
#define REXLOG_INFO(...) ((void)0)
#undef _WIN32
#include DBZ3_IW_IOP_CORE

using namespace dbz3::anim::apev;
void Comprobar(bool ok,const char* s) { if(!ok) throw std::runtime_error(s); }
std::vector<uint8_t> Fichero(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  if(!f) throw std::runtime_error("falta fuente original: "+p.string());
  return {std::istreambuf_iterator<char>(f),{}};
}
std::vector<uint8_t> Entrada(const std::filesystem::path& p,int id) {
  std::ifstream f(p,std::ios::binary);std::vector<uint8_t> t(8);
  f.read(reinterpret_cast<char*>(t.data()),8);
  if(!f || id<0 || uint32_t(id)>=U32(t,4)) return {};
  f.seekg(8+size_t(id)*8);f.read(reinterpret_cast<char*>(t.data()),8);
  std::vector<uint8_t> b(U32(t,4));f.seekg(U32(t,0));f.read(reinterpret_cast<char*>(b.data()),b.size());
  if(!f) return {};return b;
}
uint64_t HashPcm(const std::vector<int16_t>& pcm) {
  uint64_t h=14695981039346656037ull;
  for(auto v:pcm) for(uint8_t b:{uint8_t(v),uint8_t(uint16_t(v)>>8)}) h=(h^b)*1099511628211ull;
  return h;
}
int main(int argc,char** argv) {
  try {
    Comprobar(argc==2,"uso: prueba <carpeta original USA>");
    const std::filesystem::path game=argv[1];const auto elf=Fichero(game/"SLUS_218.42");
    // No elegir un banco por numero de evento ni por el alias B3.
    Comprobar(BancoActor(elf,922)==1472,"Goku: tabla ELF");
    Comprobar(BancoActor(elf,921)==1470,"Gohan nino: tabla ELF");
    Comprobar(BancoActor(elf,925)==1478,"Piccolo: tabla ELF");
    Comprobar(BancoActor(elf,924)==1476,"Krilin: tabla ELF");
    Comprobar(BancoActor(elf,204)==-1 && BancoActor(elf,915)==-1,"comun no inventa actor");
    for(size_t n:{size_t(0),size_t(31),size_t(51),size_t(100)})
      Comprobar(BancoActor({elf.begin(),elf.begin()+n},922)==-1,"ELF truncado");
    Comprobar(Resolver(1,66,-1,false).sonido==66,"tipo1 conserva valor");
    Comprobar(Resolver(1,66,-1,false).banco==954,"tipo1 banco954");
    Comprobar(Resolver(2,51,1476,false).banco==1476,"tipo2 banco del actor");
    Comprobar(Resolver(2,51,-1,false).ruta==Ruta::Pendiente,"tipo2 sin actor pendiente");
    Comprobar(Resolver(3,400,1470,false).ruta==Ruta::SilencioOriginal,"centinela400");
    Comprobar(Resolver(3,1005,1470,false).ruta==Ruta::SilencioOriginal,"filtro japones");
    Comprobar(Resolver(3,2005,1470,true).ruta==Ruta::SilencioOriginal,"filtro ingles");
    Comprobar(Resolver(3,2005,1470,false).sonido==5,"descontar2000");
    Comprobar(Resolver(3,109,1470,false).ruta==Ruta::Pendiente,"sin RNG original no voz fija");
    Comprobar(Resolver(1,1006,-1,false).ruta==Ruta::Pendiente,"sin handle no one-shot");
    for(uint32_t t:{0u,4u,5u,6u,7u,8u,9u,10u})
      Comprobar(Resolver(t,6,1472,false).ruta==Ruta::Pendiente,"no fingir consumidor");
    for(int mini=0;mini<18;++mini) for(int actor=0;actor<20;++actor) for(int v:{5,6})
      std::cout<<"CAMPO "<<v<<' '<<mini<<' '<<actor<<' '<<SonidoCampo(v,mini,actor,0)<<'\n';
    Comprobar(SonidoCampo(5,0,6,1)==-1,"tipo6 solo jugador0");
    // Todos los eventos de los BSK del corpus, no una lista de ids de prueba.
    std::map<std::array<int,3>,dbz3::iwiop::Voz> cache;
    int total=0,resueltos=0,pendientes=0;
    for(int cmn=204;cmn<=932;++cmn) {
      if(cmn!=204 && cmn<912) continue;
      const auto amb=Entrada(game/"USR/DATA_CMN.AFS",cmn);
      for(uint32_t hijo=0;hijo<U32(amb,16);++hijo) {
        const auto b=dbz3::pasarela::HijoAmb(amb,hijo);
        if(U32(b,0)!=0x4B534223) continue;
        for(uint32_t cod=0;cod<U32(b,16);++cod) {
          const size_t blk=U32(b,U32(b,20)+4*cod);if(!blk)continue;
          for(uint32_t lin=0;lin<U32(b,blk+40);++lin) {
            const size_t l=U32(b,blk+44)+8*lin;
            if((U32(b,l)&65535)!=7)continue;
            const auto num=U32(b,l)>>16;
            for(uint32_t i=0;i<num;++i) {
              const size_t e=U32(b,l+4)+16*i;
              const auto t=U32(b,e+4),v=U32(b,e+8);
              if(t!=1 && t!=2 && t!=3)continue;
              ++total;
              const auto a=Resolver(t,v,BancoActor(elf,cmn),false);
              if(a.ruta!=Ruta::Cmn && a.ruta!=Ruta::Voz) {++pendientes;continue;}
              const std::array<int,3> key={int(a.ruta),a.banco,a.sonido};
              auto [it,nueva]=cache.try_emplace(key);
              if(nueva) {
                const char* afs=a.ruta==Ruta::Cmn?"DATA_CMN.AFS":"ADX_USA.AFS";
                it->second=dbz3::iwiop::Leer(Entrada(game/"USR"/afs,a.banco),a.sonido);
                const auto& voz=it->second;
                std::cout<<"PCM "<<afs<<' '<<a.banco<<' '<<a.sonido<<' '<<voz.muestra<<' '
                         <<voz.pitch<<' '<<voz.pcm.size()<<' '<<HashPcm(voz.pcm)<<'\n';
              }
              if(!it->second.pcm.empty())++resueltos;else ++pendientes;
            }
          }
        }
      }
    }
    const auto combo=dbz3::iwiop::Leer(Entrada(game/"USR/DATA_CMN.AFS",954),0x21);
    Comprobar(!combo.pcm.empty() && combo.muestra==0x49,"954: sonido21 != muestra21");
    Comprobar(!cache.at({int(Ruta::Cmn),954,66}).pcm.empty(),"evento66 PCM original");
    Comprobar(!cache.at({int(Ruta::Voz),1470,11}).pcm.empty(),"voz Gohan11 PCM original");
    std::cout<<"CORPUS "<<total<<' '<<resueltos<<' '<<pendientes<<"\nPASS sin ventana ni salida sonora\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
