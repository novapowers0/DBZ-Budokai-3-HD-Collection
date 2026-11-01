// Datos esperados producidos por las funciones retail, no por este header.
#include "../src/iw_spx_capsulas.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace C=dbz3::iw::spx::capsulas;
namespace S=dbz3::iw::spx;
size_t comprobaciones=0;
void Ver(bool b,const char* s){++comprobaciones;if(!b)throw std::runtime_error(s);}
struct Datos {
  C::Bytes b;size_t p=0;
  uint32_t U(){auto v=C::U(b,p);p+=4;return v;}
  C::Bytes Blob(){const auto n=U();Ver(p+n<=b.size(),"fixture truncado");C::Bytes o(b.begin()+p,b.begin()+p+n);p+=n;return o;}
};
struct Objeto final:C::ObjetoAme {
  int n,despues;std::vector<uint32_t>& acciones;
  Objeto(int a,int b,std::vector<uint32_t>& e):n(a),despues(b),acciones(e){}
  bool Dibujar() override{acciones.push_back(2);return true;}
  bool Regular() override{acciones.push_back(1);n=despues;return true;}
  bool Retirar() override{acciones.push_back(3);n=-1;return true;}
  int Nodos()const override{return n;}
};
void Ciclos(Datos& d) {
  const auto casos=d.U();
  for(uint32_t k=0;k<casos;++k) {
    const auto op=d.U(),presente=d.U(),pausa=d.U();const int antes=S::Signo(d.U()),luego=S::Signo(d.U());
    C::CicloAme ciclo;std::vector<uint32_t> acciones;
    auto o=std::make_shared<Objeto>(antes,luego,acciones);if(presente)Ver(ciclo.Instalar(3,o),"instalar objeto");
    Ver(ciclo.Consumir(op,3,pausa!=0),"consumir D4/5/6");
    Ver(bool(ciclo.slots[3])==bool(d.U()),"slot final coincide con retail");
    const int esperado=S::Signo(d.U());if(presente)Ver(o->n==esperado,"timer48 retail, -1 es retirado");
    const auto n=d.U();Ver(acciones.size()==n,"numero de operaciones retail");
    for(auto a:acciones)Ver(a==d.U(),"orden draw/update/free retail");
  }
}
void Estado(Datos& d,const C::EstadoGS& e) {
  for(auto v:e.mascara)Ver(v==d.U(),"GS mascara retail");
  for(auto v:e.valor)Ver(v==d.U(),"GS valor retail");
  for(auto v:e.alpha)Ver(v==uint64_t(d.U()),"GS ALPHA retail");
  for(auto c:e.color)for(float v:c)Ver(S::Bits(v)==d.U(),"GS color retail");
  Ver(S::Bits(e.expansion)==d.U(),"GS expansion retail");
}
void Captura() {
  C::Escena e;e.capturar_pases=true;
  C::Bytes b(36);std::memcpy(b.data(),"#SPX",4);b[20]=36;b[24]=1;b.insert(b.end(),{0x92,0x92,0x95});
  Ver(e.vm.Cargar(b,S::kCapsulasEU)&&e.vm.Iniciar()==0,"crear task");
  e.trabajos[0].dibujar_actor=0;e.trabajos[0].dibujar_rect=0;
  std::vector<int> orden;int pose=7;
  e.host.capturar_actor=[&](const C::Escena&,int,const C::Actor&,const C::Trabajo&)->C::Consumidores::Pase {
    const int capturada=pose;return [&,capturada]{orden.push_back(capturada);return true;};
  };
  e.host.capturar_rectangulo=[&](const C::Escena&,int,const C::Rectangulo&)->C::Consumidores::Pase {
    return [&]{orden.push_back(99);return true;};
  };
  // Un D6 en un sondeo precede al draw posterior del mismo task; un D5
  // posterior puede mutar el objeto antes del flush. La muestra debe persistir.
  e.vm.HiloEn(0).sondeo=[&](S::Instancia&,S::Hilo& h){const int a=pose;e.EncolarPase([&,a]{orden.push_back(a-1);return true;});h.sondeo={};return 3u;};
  Ver(e.Tick(0),"tick captura");pose=50;
  Ver(e.Dibujar()&&orden==std::vector<int>{6,7,99},"orden AME/actor/rect y pose congelada");
  orden.clear();Ver(e.Dibujar()&&orden==std::vector<int>{6,7,99},"presentacion extra no avanza ni repite simulacion");
  Ver(e.Tick(0)&&e.pases.size()==2,"cada tick sustituye la captura anterior");
}
void Matrices(Datos& d) {
  std::array<C::Luz,2> luces;
  for(auto& l:luces) {
    l.valida=true;l.flags=1;
    for(auto& x:l.direccion)x=S::Float(d.U());for(auto& x:l.color)x=S::Float(d.U());
  }
  std::array<float,4> ambiente;for(auto& x:ambiente)x=S::Float(d.U());
  const auto m=C::PrepararLuces(luces,ambiente);
  for(const auto& row:m.direcciones)for(float v:row)Ver(v==S::Float(d.U()),"matriz de direccion12C070 retail");
  for(const auto& row:m.colores)for(float v:row)Ver(v==S::Float(d.U()),"matriz de color12C070 retail");
}
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("fixture requerido");std::ifstream f(argv[1],std::ios::binary);
    Datos d{C::Bytes(std::istreambuf_iterator<char>(f),{})};Ver(d.U()==0x46324350,"firmaF2");
    Ciclos(d);const auto planes=C::PasesNieblaGS({});for(const auto& s:planes)Estado(d,s);Matrices(d);
    const auto ame=d.Blob();const auto motivo=C::LimiteMotorAme(ame);
    Ver(motivo.find("cadena")!=std::string::npos,"AME972 completo NO se acepta con motor que pierde hijos");
    Captura();Ver(d.p==d.b.size(),"fixture consumido");
    std::cout<<"PASS F2 "<<comprobaciones<<" comprobaciones; "<<motivo<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL F2 "<<comprobaciones<<" "<<e.what()<<'\n';return 1;}
}
