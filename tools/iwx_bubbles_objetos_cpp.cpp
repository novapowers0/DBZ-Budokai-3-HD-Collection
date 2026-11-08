// Reproduce fixtures EE de la fase anterior al dibujo; no inicia el juego.
#include "../src/iw_bubbles_objetos.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
using namespace dbz3::iwbubbles::objetos;
template<class T> T Leer(std::ifstream& f){
  T v{};if(!f.read(reinterpret_cast<char*>(&v),sizeof(v)))throw std::runtime_error("fixture truncada");return v;
}
static std::uint32_t Bits(float v){std::uint32_t b;std::memcpy(&b,&v,4);return b;}
int main(int argc,char** argv){try{
  if(argc!=2)throw std::runtime_error("uso: iwx_bubbles_objetos_cpp fixture");
  std::ifstream f(argv[1],std::ios::binary);
  char magic[8]{};f.read(magic,8);
  if(std::memcmp(magic,"BBOBJ001",8))throw std::runtime_error("firma desconocida");
  const auto n=Leer<std::uint32_t>(f);
  if(n>100000)throw std::runtime_error("demasiados casos");
  unsigned fallos=0;
  for(unsigned c=0;c<n;++c){
    Objeto o;o.tipo=static_cast<std::int16_t>(Leer<std::uint32_t>(f));
    const bool pausa=Leer<std::uint32_t>(f)!=0;
    const float distancia=Leer<float>(f),paso=Leer<float>(f);
    for(float& v:o.posicion)v=Leer<float>(f);
    for(float& v:o.velocidad)v=Leer<float>(f);
    o.tiempo=Leer<float>(f);o.limite=Leer<float>(f);o.giro=Leer<std::uint32_t>(f);
    const auto esperado=Leer<std::uint32_t>(f);
    std::array<std::uint32_t,11> valores{};for(auto& b:valores)b=Leer<std::uint32_t>(f);
    const auto r=PasoAntesDeDibujar(o,distancia,pausa,paso);
    std::array<std::uint32_t,11> actual{};
    for(unsigned i=0;i<4;++i){actual[i]=Bits(o.posicion[i]);actual[i+4]=Bits(o.velocidad[i]);}
    actual[8]=Bits(o.tiempo);actual[9]=Bits(o.limite);actual[10]=o.giro;
    if(static_cast<unsigned>(r)!=esperado||actual!=valores){
      if(fallos<8){std::fprintf(stderr,"caso %u tipo %d salida %u/%u",c,int(o.tipo),unsigned(r),esperado);
        for(unsigned i=0;i<11;++i)if(actual[i]!=valores[i])std::fprintf(stderr," campo%u=%08X/%08X",i,actual[i],valores[i]);
        std::fputc('\n',stderr);}
      ++fallos;
    }
  }
  if(f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("bytes sobrantes");
  std::printf("{\"casos\":%u,\"diferencias\":%u,\"normal\":false,\"integrado\":false}\n",n,fallos);
  return fallos?1:0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 2;}}
