// Driver black-box: usa exclusivamente la API publica del nucleo de produccion.
// El oraculo NO vive aqui: iw_spx_original_prueba.py ejecuta instrucciones retail.
#include "../src/iw_spx.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace S = dbz3::iw::spx;
namespace {
uint32_t U(std::istream& f) {
  uint8_t b[4]; if(!f.read(reinterpret_cast<char*>(b),4)) throw std::runtime_error("fixture truncado");
  return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
}
std::vector<uint8_t> B(std::istream& f) {
  std::vector<uint8_t> b(U(f)); if(!f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()))) throw std::runtime_error("bytes truncados"); return b;
}
template<class T> void Lista(const T& a) {
  std::cout<<'['; bool primero=true;
  for(auto x:a) { if(!primero) std::cout<<','; primero=false; std::cout<<uint32_t(x); }
  std::cout<<']';
}
void Estado(S::Instancia& m,const S::Hilo& h) {
  std::cout<<"\"r\":"; Lista(h.r); std::cout<<",\"f\":"; Lista(h.f);
  std::vector<uint8_t> mem,stack;
  for(unsigned i=0;i<64;++i) mem.push_back(m.Leer8(0x950000+i));
  for(unsigned i=0;i<32;++i) stack.push_back(m.Leer8(h.r[15]+0x1000-32+i));
  std::cout<<",\"mem\":"; Lista(mem); std::cout<<",\"stack\":"; Lista(stack);
}
}
int main(int argc,char** argv) {
  try {
    if(argc!=2) throw std::runtime_error("uso: driver fixtures.bin");
    std::ifstream f(argv[1],std::ios::binary);
    if(!f || U(f)!=0x5350584F) throw std::runtime_error("fixture SPXO invalido");
    const auto casos=U(f);
    for(uint32_t caso=0;caso<casos;++caso) {
      auto nombre=B(f); auto bytes=B(f); const auto ticks=U(f),opciones=U(f),ret0=U(f),ret1=U(f);
      std::vector<uint32_t> poll(ticks),pausa(ticks);
      for(auto& v:poll) v=U(f); for(auto& v:pausa) v=U(f);
      std::vector<std::pair<uint32_t,uint32_t>> ri,fi;
      auto pares=[&](auto& v) { const auto n=U(f); for(uint32_t i=0;i<n;++i) { const auto a=U(f),b=U(f); v.emplace_back(a,b); } };
      pares(ri); pares(fi);
      std::vector<std::pair<uint32_t,std::vector<uint8_t>>> patches;
      const auto nm=U(f); for(uint32_t i=0;i<nm;++i) { const auto a=U(f); patches.emplace_back(a,B(f)); }
      S::Configuracion cfg; cfg.guion=0x800000; cfg.temporal=0x900000; cfg.pilas=0x910000;
      cfg.registros=0x920000; cfg.compartida=0x930000; cfg.locales=0x940000;
      S::Instancia m(cfg); std::vector<std::string> cb;
      uint32_t cuadro=0;
      auto instalar=[&](S::Hilo& h) {
        if(opciones&1) h.previo=[&](S::Instancia&,S::Hilo& t) { ++t.r[6]; cb.push_back("pre"); };
        if(opciones&2) h.posterior=[&](S::Instancia&,S::Hilo& t) { ++t.r[7]; cb.push_back("post"); };
        if(opciones&4) h.sondeo=[&](S::Instancia&,S::Hilo& t) { ++t.r[5]; cb.push_back("poll"); const auto r=poll.at(cuadro-1); if((r&255)==1) t.sondeo={}; return r; };
      };
      m.ganchos.crear=[&](S::Instancia&,S::Hilo& h,const S::Hilo* p) {
        instalar(h); cb.push_back("crear:"+std::to_string(p?p->r[0]:0));
      };
      m.ganchos.inicio=[&](S::Instancia& v,uint32_t) { v.Escribir32(0x80001c,0x55); cb.push_back("inicio"); };
      m.ganchos.terminar=[&](S::Instancia&,S::Hilo&) { cb.push_back("terminar"); };
      m.ganchos.fin=[&](S::Instancia&) { cb.push_back("fin"); };
      m.Registrar(0xA04000,0,[&](S::Instancia&,S::Hilo&) { cb.push_back("nativo0"); return ret0; });
      m.Registrar(0xA04000,1,[&](S::Instancia&,S::Hilo&) { cb.push_back("nativo1"); return ret1; });
      if(!m.Cargar(bytes,0xA04000) || m.Iniciar()!=0) throw std::runtime_error(m.Error());
      m.Mapear(0x950000,std::vector<uint8_t>(256));
      auto& h=m.HiloEn(0);
      const std::string name(nombre.begin(),nombre.end());
      std::cout<<"{\"case\":\""<<name<<"\",\"kind\":\"initial\",\"state\":"<<m.Estado()
        <<",\"live\":"<<m.Vivos()<<",\"priority\":"<<h.prioridad<<",\"header\":"<<m.Leer32(0x80001c)<<',';
      Estado(m,h); std::cout<<",\"callbacks\":[";
      for(size_t i=0;i<cb.size();++i) { if(i) std::cout<<','; std::cout<<'"'<<cb[i]<<'"'; }
      std::cout<<"]}\n";
      for(auto [i,v]:ri) h.r.at(i)=v;
      for(auto [i,v]:fi) h.f.at(i)=v;
      for(const auto& p:patches) for(size_t j=0;j<p.second.size();++j) m.Escribir8(p.first+uint32_t(j),p.second[j]);
      m.traza=[&](const S::Traza& t) {
        if(t.fase!=S::Traza::Fase::Opcode && t.fase!=S::Traza::Fase::Nativo) return;
        std::cout<<"{\"case\":\""<<name<<"\",\"kind\":\""<<(t.fase==S::Traza::Fase::Opcode?"op":"native")
          <<"\",\"tick\":"<<cuadro<<",\"slot\":"<<t.ranura<<",\"pc\":"<<t.pc<<",\"code\":"<<t.codigo<<",\"ret\":"<<t.retorno<<',';
        Estado(m,m.HiloEn(size_t(t.ranura))); std::cout<<"}\n";
      };
      for(cuadro=1;cuadro<=ticks;++cuadro) {
        cb.clear(); m.Pausar(pausa[cuadro-1]!=0);
        const bool ok=m.Tick();
        std::cout<<"{\"case\":\""<<name<<"\",\"kind\":\"tick\",\"tick\":"<<cuadro<<",\"ok\":"<<ok
          <<",\"state\":"<<m.Estado()<<",\"live\":"<<m.Vivos()<<",\"wait\":"<<h.espera<<',';
        Estado(m,h); std::cout<<",\"callbacks\":[";
        for(size_t i=0;i<cb.size();++i) { if(i) std::cout<<','; std::cout<<'"'<<cb[i]<<'"'; }
        std::cout<<"],\"children\":[";
        for(size_t i=1;i<2;++i) {
          const auto& c=m.HiloEn(i);
          std::cout<<"{\"active\":"<<c.activo<<",\"priority\":"<<c.prioridad<<",\"r\":"; Lista(c.r); std::cout<<'}';
        }
        std::cout<<"]}\n";
        if(!ok) break;
      }
    }
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
