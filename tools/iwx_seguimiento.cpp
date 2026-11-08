// Fixture externa de RAM PS2: referencia anterior/actual, seguido anterior/nuevo.
#include "../src/iw_seguimiento.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

int main(int argc,char** argv) {
  if(argc!=2&&argc!=3)return 2;
  const float k=argc==3?std::strtof(argv[2],nullptr):.3f;
  if(k!=.12f&&k!=.3f)return 2;
  FILE* f=std::fopen(argv[1],"rb");
  if(!f)return 2;
  uint32_t n=0;
  if(std::fread(&n,sizeof(n),1,f)!=1||!n||n>100000){std::fclose(f);return 2;}
  using namespace dbz3::iwseguimiento;
  float maximo=0,antes=0;
  auto error=[](V3 a,V3 b){return std::max({std::fabs(a.x-b.x),std::fabs(a.y-b.y),std::fabs(a.z-b.z)});};
  for(uint32_t i=0;i<n;++i) {
    V3 r[4];
    if(std::fread(r,sizeof(r),1,f)!=1){std::fclose(f);return 2;}
    Memoria m;
    m.Elegir(r[0],true);
    const auto usada=m.Elegir(r[1],false);
    maximo=std::max(maximo,error(Mezclar(r[2],usada,k),r[3]));
    antes=std::max(antes,error(Mezclar(r[2],r[1],k),r[3]));
    // Un reinicio no debe conservar la referencia de otra sesion.
    if(error(m.Elegir(r[1],true),r[1])!=0){std::fclose(f);return 3;}
  }
  const bool completo=std::fgetc(f)==EOF;
  std::fclose(f);
  std::printf("{\"pares\":%u,\"maximo_cpp\":%.9g,\"maximo_fase_actual\":%.9g,\"reinicios\":true}\n",n,maximo,antes);
  return completo&&maximo<=.001f&&antes>.001f?0:1;
}
