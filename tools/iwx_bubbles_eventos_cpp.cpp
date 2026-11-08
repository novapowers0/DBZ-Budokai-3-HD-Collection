// Solicitudes BBR contra fixture externo producido por instrucciones del ELF.
// No emula el creador de objetos ni acredita su trayectoria/dibujo/sonido.
#include "../src/iw_bubbles_gp2.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace bb=dbz3::iwbubbles;
struct SinAzar { unsigned Memory(unsigned) { throw std::runtime_error("RNG inesperado"); } };
struct Pose { bb::V Evaluar(unsigned,float) {return {};} void AplicarRaiz(bb::V) {} };
static std::uint32_t leer(std::ifstream& f) {
  std::uint32_t v=0;
  if(!f.read(reinterpret_cast<char*>(&v),4))throw std::runtime_error("fixture incompleto");
  return v;
}
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("falta fixture original");
    std::ifstream f(argv[1],std::ios::binary);char magic[8]{};
    if(!f.read(magic,8)||std::memcmp(magic,"BBEVT001",8))throw std::runtime_error("cabecera invalida");
    const auto n=leer(f);if(n!=144&&n!=168)throw std::runtime_error("cantidad de casos originales invalida");
    unsigned diferencias=0,eventos=0,repetidos=0;
    for(unsigned i=0;i<n;++i) {
      const auto estado=leer(f),dificultad=leer(f),bits=leer(f),latch=leer(f),latch_final=leer(f),count=leer(f);
      if(estado<7||estado>9||dificultad>6||count>4)throw std::runtime_error("caso fuera de rango");
      std::array<std::array<std::uint32_t,2>,4> esperado{};
      for(unsigned k=0;k<count;++k){esperado[k][0]=leer(f);esperado[k][1]=leer(f);}
      bb::Maquina m;m.Entrar(int(estado));m.latch224=latch!=0;m.frente={-1,0,0};
      std::memcpy(&m.cuadro,&bits,4);m.largos.fill(1000);
      bb::Entrada e;e.jugador={1000,0,0};e.dificultad=int(dificultad);e.paso=0;e.factor_velocidad=0;
      SinAzar rng;Pose pose;bb::wcf::Backend colision;
      m.Paso(e,rng,pose,colision);
      bool ok=m.n_eventos==int(count)&&m.latch224==bool(latch_final);
      for(int k=0;k<m.n_eventos;++k){
        std::uint32_t actual=0;std::memcpy(&actual,&m.eventos[k].desvio,4);
        ok=ok&&k<int(count)&&m.eventos[k].tipo==int(esperado[k][0])&&actual==esperado[k][1];
      }
      eventos+=unsigned(m.n_eventos);
      if(!ok){++diferencias;if(diferencias<=6)std::cerr<<"caso="<<i<<" estado="<<estado<<" dificultad="<<dificultad<<" cuadro="<<m.cuadro<<" eventos="<<m.n_eventos<<'\n';}
      m.Paso(e,rng,pose,colision);if(m.n_eventos)++repetidos;
    }
    if(f.peek()!=EOF)throw std::runtime_error("fixture sobrante");
    std::cout<<"{\"casos\":"<<n<<",\"diferencias\":"<<diferencias<<",\"eventos\":"<<eventos
             <<",\"repetidos\":"<<repetidos<<",\"normal\":false}\n";
    return diferencias||repetidos?1:0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
