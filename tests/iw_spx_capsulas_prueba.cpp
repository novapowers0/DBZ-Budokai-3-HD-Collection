// Standalone sin SDK/ventanas. Usa las copias del jugador, solo lectura.
#include "../src/iw_spx_capsulas.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
namespace C = dbz3::iw::spx::capsulas;
namespace S = dbz3::iw::spx;
namespace {
int n=0;
void Ver(bool b,const char* s) { ++n; if(!b) throw std::runtime_error(s); }
C::Bytes Leer(const std::string& p) {
  std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("no se abre "+p);
  return C::Bytes(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
C::Bytes Entrada(uint32_t id) {
  const auto archivo=(id>>16)?"DATA_USA.AFS":"DATA_CMN.AFS";
  std::ifstream f(std::string("ps2_games/Infinite World (USA)/USR/")+archivo,std::ios::binary);
  if(!f) throw std::runtime_error("AFS no disponible");
  C::Bytes tabla(8); f.seekg(8+8*(id&65535)); f.read(reinterpret_cast<char*>(tabla.data()),8);
  const auto off=C::U(tabla,0), tam=C::U(tabla,4); C::Bytes out(tam);
  f.seekg(off); f.read(reinterpret_cast<char*>(out.data()),tam); if(!f) throw std::runtime_error("AFS truncado"); return out;
}
void Manifest(const C::Bytes& elf) {
  std::set<uint32_t> reales;
  for(int id:{993,994}) {
    const auto b=Entrada(uint32_t(id));
    for(size_t i=36;i+8<b.size();++i) {
      if(b[i]!=1 || (b[i+1]!=16 && b[i+1]!=32 && b[i+1]!=48)) continue;
      const unsigned z=b[i+1]==16?1:b[i+1]==32?2:4;
      if(i+5+z>b.size() || b[i+2+z]!=2 || b[i+3+z]!=128 || b[i+4+z]!=2) continue;
      uint32_t cod=0; for(unsigned j=0;j<z;++j) cod|=uint32_t(b[i+2+j])<<(8*j); reales.insert(cod);
    }
  }
  Ver(reales.size()==33,"inventario real33");
  for(auto e:C::kFunciones) {
    Ver(reales.erase(e.id)==1,"sin builtin de relleno");
    const auto u=[&](uint32_t a) { return C::U(elf,a-0x100000+0x80); };
    Ver(u(u(S::kCapsulasEU+4*(e.id>>4))+4*(e.id&15))==e.funcion,"tabla original confirma destino EU");
  }
  Ver(reales.empty(),"todos los IDs literales declarados");
}
void BloqueoReal(const C::Bytes& elf,int id) {
  C::Escena c; std::vector<std::string> logs;
  c.host.log=[&](const std::string& s){logs.push_back(s);};
  Ver(c.Cargar(elf,Entrada,id),"cargar45 recursos reales");
  Ver(c.camara.valida && c.camara.fov>0,"camara AMC real muestreada");
  Ver(!c.Tick(0),"sin consumidor IOP bloquea");
  Ver(c.ultimo_id==0x71 && c.vm.Error().find("IOP")!=std::string::npos,"bloqueo en IOP71, no confundir camara");
  Ver(!logs.empty() && logs.back().find("BLOQUEO")!=std::string::npos,"evidencia bloqueo en log");
  Ver(c.audio.cuerpo==(id==993?39:42) && c.actores[1].modelo==(id==993?9:12),"SetupLayersA/B del original");
  std::cout<<"SPX"<<id<<" sin IOP: cuadro="<<c.vm.Cuadro()<<" id="<<std::hex<<c.ultimo_id<<std::dec<<" "<<c.vm.Error()<<'\n';
}
void Consumidores(const C::Bytes& elf,int id) {
  C::Escena c;
  int pedidos=0; std::vector<C::Bytes> bancos;
  c.host.preparar_iop=[&](const C::Escena& s,bool ab) {
    if(ab) { bancos.push_back(s.Banco(s.audio.cuerpo)); bancos.push_back(s.Banco(s.audio.indice)); }
    else bancos.push_back(s.Banco(s.audio.secuencia));
    ++pedidos; return true;  // consumidor SIN audio: copia durable real de bancos para la prueba.
  };
  c.host.iop_ocupado=[] { return false; };
  // No se hacen pasar por implementados los consumidores de pose/luz faltantes.
  Ver(c.Cargar(elf,Entrada,id),"cargar escena con consumidor de bancos");
  for(int f=0;f<1000 && !c.vm.Bloqueada();++f) c.Tick(0);
  std::cout<<"SPX"<<id<<" bancos conectados: cuadro="<<c.vm.Cuadro()<<" id="<<std::hex<<c.ultimo_id<<std::dec<<" "<<c.vm.Error()<<'\n';
  Ver(c.vm.Bloqueada(),"pose/AML requiere consumidor real");
  Ver(pedidos==2 && bancos.size()==3 && c.audio.preparado,"71/72 preparan tres bancos");
  Ver(c.llamadas[0x45]>0 && c.llamadas[0x63]>0 && c.llamadas[0x119]==size_t(id==993),"nativos iniciales ejercidos por script");
  Ver(c.ambiente[0]>0.79f && c.ambiente[0]<0.81f,"ambiente sale del bytecode");
  Ver(c.ultimo_id==0x42 || c.ultimo_id==0x60 || c.ultimo_id==0x46,"bloqueo consumidor identificado");
  Ver(c.vm.Vivos()>16,"SPX real necesita configuracion IW32, no16");
}
struct Codigo {
  C::Bytes b;
  void I(uint32_t v) { b.push_back(0x30); for(unsigned j=0;j<4;++j) b.push_back(uint8_t(v>>(8*j))); }
  void N(uint32_t id,std::initializer_list<uint32_t> args={}) {
    for(auto i=args.end();i!=args.begin();) { --i; b.push_back(8); I(*i); }
    b.push_back(0x80); I(id);
    if(args.size()) { b.push_back(0x12); b.push_back(0x10); b.push_back(uint8_t(args.size()*4)); b.push_back(0x0B); }
  }
  C::Bytes Guion() const {
    C::Bytes out(36,0); std::memcpy(out.data(),"#SPX",4);
    out[0x14]=36; out[0x18]=1; out.insert(out.end(),b.begin(),b.end()); return out;
  }
};
void RectangulosEInput(const C::Bytes& elf) {
  C::Escena c; Ver(c.Cargar(elf,Entrada,994),"cargar recursos para rectangulos"); c.vm.Parar();
  Codigo code;
  code.N(0x93,{S::Bits(128),S::Bits(64),S::Bits(32),S::Bits(16),0});
  code.N(0x94,{S::Bits(40),S::Bits(80),0});
  code.N(0x95,{S::Bits(0),S::Bits(100),S::Bits(200),0});
  code.N(0x96,{3,0}); code.N(0x90,{1,0}); code.b.push_back(0x92);
  code.N(0x97,{S::Bits(0),S::Bits(128),S::Bits(128),S::Bits(128),S::Bits(2),0});
  code.N(0x116,{0}); code.b.push_back(0x92); code.b.push_back(0x95);
  std::vector<C::Rectangulo> enviados;
  c.host.rectangulo=[&](const C::Escena&,int slot,const C::Rectangulo& r) { Ver(slot==0,"slot rectangulo"); enviados.push_back(r); return true; };
  Ver(c.vm.Cargar(code.Guion(),S::kCapsulasEU) && c.vm.Iniciar()==0 && c.Tick(0),"nativos rectangulo ejecutados");
  Ver(c.Dibujar() && enviados.size()==1 && c.dibujos==1,"VM activa dibujo real al consumidor");
  Ver(enviados[0].rect==std::array<float,4>{159.5f,79.5f,240.5f,120.5f},"geometria exacta94/95/96, incluye +1 de96");
  Ver(enviados[0].color==std::array<float,4>{16,32,64,128},"orden RGBA original93");
  Ver(c.Tick(0) && c.Dibujar(),"97 primer paso y dibujo");
  Ver(enviados.back().color==std::array<float,4>{72,80,96,64},"97 mezcla con1/restante, no tween inventado");
  Ver(c.Tick(0) && c.Dibujar(),"97 segundo paso");
  Ver(enviados.back().color==std::array<float,4>{128,128,128,0},"97 destino al agotarse tiempo");
  Ver(c.Tick(8) && c.vm.HiloEn(0).r[0]==1,"input PS2 real a116 tras sondeo");
  // Sin renderer nunca se cuenta un dibujo como consumido.
  c.host.rectangulo={}; Ver(!c.Dibujar() && c.vm.Bloqueada(),"sin renderer bloquea en vez de shadow");
}
void UvOriginal(const C::Bytes& elf) {
  C::Escena c; Ver(c.Cargar(elf,Entrada,993),"recursos UV"); c.vm.Parar();
  Codigo code; code.N(0x47,{S::Bits(0.125f),S::Bits(0.25f),2}); code.b.push_back(0x92); code.b.push_back(0x95);
  Ver(c.vm.Cargar(code.Guion(),S::kCapsulasEU) && c.vm.Iniciar()==0 && c.Tick(0),"47 inicia sondeo");
  const auto addr=c.vm.HiloEn(0).r[14];
  Ver(S::Float(c.vm.Leer32(addr+0x70))==0.25f && S::Float(c.vm.Leer32(addr+0x74))==0.125f,
      "UV se publica en temporal+70/+74 como el original");
  Ver(c.Tick(0) && c.trabajos[0].u==0.5f && c.trabajos[0].v==0.25f,"47 avanza durante dos sondeos reales");
  Ver(c.Tick(0) && !c.vm.HiloEn(0).sondeo,"47 limpia espera al agotar contador");
}
}
int main() {
  try {
    const auto elf=Leer("D:/tmp/SLES_553.47"); Manifest(elf);
    for(int id:{993,994}) { BloqueoReal(elf,id); Consumidores(elf,id); }
    RectangulosEInput(elf);
    UvOriginal(elf);
    std::cout<<"PASS "<<n<<" comprobaciones, corpus real993/994, sin ventanas\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<"FAIL "<<n<<": "<<e.what()<<'\n'; return 1; }
}
