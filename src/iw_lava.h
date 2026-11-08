// Namek, EU2A5DE0/2A5F40: decision de contacto, no solucion de movimiento/WCF.
#pragma once
#include <cmath>
#include <cstdio>

namespace dbz3::iwlava {
inline bool Superficie3(int estado,int tipo,float y,float suelo,bool aire) {
  // EU2A6008..6024: sub.s, abs.s, c.le.s contra 0x3C23D70A.
  // La esfera de pared (+1F0) NO interviene en este predicado.
  return !(estado>=0x26&&estado<0x28) && estado!=0x1F && estado!=0x17 && estado!=0x18 &&
      tipo==3 && !aire && std::isfinite(y) && std::isfinite(suelo) && std::fabs(suelo-y)<=.01f;
}
inline bool Superficie4(int estado,int pies,int cuerpo,float y,float suelo) {
  return !(estado>=0x26&&estado<0x2A) && estado!=0x20 &&
      (cuerpo==4 || (pies==4 && std::isfinite(y) && std::isfinite(suelo) && std::fabs(suelo-y)<=2.0f));
}
// Solo lectura bajo TEST_IWX_LAVA + DBZ3_TEST_IWMINI. No fuerza contacto/pose.
inline void Medir(const char* ruta,int estado,float x,float y,float z,float suelo,
                  int pies,int cuerpo,bool aire,int gracia,bool tres,bool cuatro,bool anterior,
                  float vida) {
  struct Archivo {
    FILE* f=nullptr;bool intento=false;unsigned n=0;
    ~Archivo(){if(f)std::fclose(f);}
  };
  static Archivo a;
  if(!ruta||!*ruta)return;
  if(!a.intento) {
    a.intento=true;a.f=std::fopen(ruta,"wx");
    if(a.f)std::fputs("tick,estado,x,y,z,suelo,pies,cuerpo,aire,gracia,tres,cuatro,anterior,vida\n",a.f);
  }
  if(!a.f||a.n>=4096)return;
  std::fprintf(a.f,"%u,%d,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%d,%d,%d,%d,%.9g\n",
      ++a.n,estado,x,y,z,suelo,pies,cuerpo,int(aire),gracia,int(tres),int(cuatro),int(anterior),vida);
  std::fflush(a.f);
}
} // namespace dbz3::iwlava
