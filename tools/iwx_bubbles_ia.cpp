// Una decision de IA con anclas/estado del campo PAL previo a EE17796.
// Fixture externo: 4uint32(EE,estado,jugador,siguiente),9float(anclas/frente),3flags.
#include "../src/iw_bubbles_gp2.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace bb=dbz3::iwbubbles;
struct SinAzar { unsigned Memory(unsigned) { throw std::runtime_error("RNG inesperado"); } };
int main(int argc,char** argv){
  try{
    if(argc!=2)throw std::runtime_error("falta fixture externo");
    std::ifstream f(argv[1],std::ios::binary);
    std::array<std::uint32_t,4> meta{};std::array<float,9> v{};std::array<std::uint32_t,3> flags{};
    if(!f.read(reinterpret_cast<char*>(meta.data()),sizeof meta) ||
       !f.read(reinterpret_cast<char*>(v.data()),sizeof v) ||
       !f.read(reinterpret_cast<char*>(flags.data()),sizeof flags) || f.peek()!=EOF)
      throw std::runtime_error("fixture incompleto o sobrante");
    if(meta[0]!=17796 || meta[1]!=0 || meta[2]!=0 || meta[3]!=7)
      throw std::runtime_error("no es la decision natural17796");
    for(float x:v)if(!std::isfinite(x))throw std::runtime_error("ancla no finita");
    bb::Maquina m;m.estado=int(meta[1]);m.anclas.actual640={v[0],v[1],v[2]};m.frente={v[6],v[7],v[8]};
    bb::Entrada e;e.estado_jugador=int(meta[2]);e.jugador={v[3],v[4],v[5]};
    SinAzar rng;m.IA(e,rng);const int elegido=m.Selector();
    const bool ok=elegido==int(meta[3])&&m.flag210==bool(flags[0])&&m.flag214==bool(flags[1])&&m.flag218==bool(flags[2]);
    std::cout<<"EE="<<meta[0]<<" distancia="<<m.Distancia(e)<<" frente="<<m.Frente(e)
      <<" seleccion="<<elegido<<" flags="<<m.flag210<<m.flag214<<m.flag218<<" RNG=0 PASS="<<ok<<'\n';
    return ok?0:1;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
