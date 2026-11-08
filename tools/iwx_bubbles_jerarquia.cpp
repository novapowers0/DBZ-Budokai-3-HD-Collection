// Contrasta el adaptador de dibujo con la paleta WORLD guardada por el original.
#include "iw_bubbles_pose.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
namespace p=dbz3::iwbubbles::poses;
using M=std::array<float,12>;
std::vector<uint8_t> Leer(const std::filesystem::path& path){
  std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error(path.string());
  return {std::istreambuf_iterator<char>(f),{}};
}
M Multiplicar(const M& a,const M& b){
  M r{};for(size_t i=0;i<3;++i)for(size_t j=0;j<4;++j)
    r[4*i+j]=a[4*i]*b[j]+a[4*i+1]*b[4+j]+a[4*i+2]*b[8+j]+(j==3?a[4*i+3]:0);
  return r;
}
int main(int argc,char** argv){try{
  if(argc!=2)throw std::runtime_error("Usar: fixture_jerarquia.bin");
  const auto raw=Leer(argv[1]);const p::Bytes b{raw};
  constexpr size_t stride=4+16+128+64;
  if(raw.size()!=47*stride)throw std::runtime_error("Fixture de47 huesos incompleto");
  const M identidad{1,0,0,0,0,1,0,0,0,0,1,0};
  std::array<M,47> antes,despues;float error_antes=0,error_despues=0;
  for(size_t i=0;i<47;++i){const auto off=i*stride;
    const auto padre=b.Leer<int32_t>(off);const auto reposo=b.Leer<p::V4>(off+4);
    const auto live=b.Leer<p::Hueso>(off+20);
    if(padre>=int(i)||padre< -1)throw std::runtime_error("Padre fuera de orden");
    antes[i]=Multiplicar(padre<0?identidad:antes[size_t(padre)],p::MatrizDibujo(live,{}));
    despues[i]=Multiplicar(padre<0?identidad:despues[size_t(padre)],p::MatrizDibujo(live,reposo));
    for(size_t f=0;f<3;++f)for(size_t c=0;c<4;++c){
      const float esperado=b.Leer<float>(off+148+(4*c+f)*4);
      if(!std::isfinite(esperado)||!std::isfinite(despues[i][4*f+c]))throw std::runtime_error("Matriz no finita");
      error_antes=std::max(error_antes,std::fabs(antes[i][4*f+c]-esperado));
      error_despues=std::max(error_despues,std::fabs(despues[i][4*f+c]-esperado));
    }
  }
  std::cout<<std::setprecision(9)<<"{\"huesos\":47,\"antes\":"<<error_antes
    <<",\"despues\":"<<error_despues<<",\"visual\":false}\n";
  // Guarda negativa: omitir las longitudes debe reproducir el colapso medido.
  return error_antes>3.f&&error_despues<.00001f?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
