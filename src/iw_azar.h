// Estado Memory(n) compartido por los consumidores IW portados. No es el RNG B3.
#pragma once
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace dbz3::iwazar {
class Contexto {
 public:
  // El original conserva la semilla entre escenas. Una carga repetida de bancos
  // NO reinicia el estado; al restaurar un estado se usa Cambiar explicitamente.
  bool Iniciar(const std::vector<uint8_t>& elf) {
    std::lock_guard lock(mutex_);
    if(iniciado_)return true;
    uint32_t semilla=0;
    if(!LeerSemilla(elf,semilla))return false;
    estado_=semilla;iniciado_=true;return true;
  }
  uint32_t Memory(int bits) {
    std::lock_guard lock(mutex_);
    for(int i=0;i<bits;++i)estado_=(estado_<<1)|(((estado_>>31)^(estado_>>4))&1);
    return estado_-1;
  }
  uint32_t Cambiar(uint32_t semilla) {
    std::lock_guard lock(mutex_);
    const auto anterior=estado_;estado_=semilla;iniciado_=true;return anterior;
  }
  uint32_t Estado() const {std::lock_guard lock(mutex_);return estado_;}
  // AME recibe uint32_t&. Se protege una evaluacion completa para que voces y
  // particulas no pierdan actualizaciones al ejecutarse en hilos distintos.
  template<class Funcion> decltype(auto) ConEstado(Funcion&& funcion) {
    std::lock_guard lock(mutex_);
    return std::forward<Funcion>(funcion)(estado_);
  }
 private:
  // .reginfo -> GP-7FE8 -> PT_LOAD. Mismo dato retail que lee el decoder,
  // sin incorporar dependencias del audio/SDK al estado compartido.
  static bool LeerSemilla(const std::vector<uint8_t>& elf,uint32_t& semilla) {
    auto rango=[&](size_t p,size_t n){return p<=elf.size() && n<=elf.size()-p;};
    auto u=[&](size_t p) {return uint32_t(elf[p])|uint32_t(elf[p+1])<<8|
        uint32_t(elf[p+2])<<16|uint32_t(elf[p+3])<<24;};
    if(!rango(0,52) || u(0)!=0x464C457F || elf[4]!=1 || elf[5]!=1)return false;
    const size_t sh=u(32),ss=u(44)>>16,sn=u(48)&65535;
    if(ss<40 || !rango(sh,ss*sn))return false;
    uint32_t gp=0;
    for(size_t i=0;i<sn;++i) {
      const size_t p=sh+i*ss;
      if(u(p+4)!=0x70000006)continue;
      const size_t off=u(p+16);if(u(p+20)<24 || !rango(off,24))return false;
      gp=u(off+20);break;
    }
    if(gp<0x7FE8)return false;
    const uint32_t direccion=gp-0x7FE8;
    const size_t ph=u(28),ps=u(40)>>16,pn=u(44)&65535;
    if(ps<32 || !rango(ph,ps*pn))return false;
    for(size_t i=0;i<pn;++i) {
      const size_t p=ph+i*ps;
      if(u(p)!=1 || direccion<u(p+8))continue;
      const size_t delta=size_t(direccion-u(p+8)),bytes=u(p+16);
      if(delta>bytes || bytes-delta<4)continue;
      const size_t off=size_t(u(p+4))+delta;if(!rango(off,4))return false;
      semilla=u(off);return true;
    }
    return false;
  }
  mutable std::mutex mutex_;
  uint32_t estado_=0;
  bool iniciado_=false;
};
inline Contexto& Global(){static Contexto contexto;return contexto;}
} // namespace dbz3::iwazar
