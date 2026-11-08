// Candidato acotado: parte anterior al dibujo de IW EU 31A2F0..31A4E4.
// No integrado: faltan creador, anclas originales, wrap de dibujo y sonido.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace dbz3::iwbubbles::objetos {
inline constexpr bool kNormalValidado=false;
struct Objeto {
  std::array<float,4> posicion{},velocidad{};
  float tiempo=0,limite=0;
  std::uint32_t giro=0;
  std::int16_t tipo=0;
};
enum class Salida { Dibujar, Caducado, Contacto };
// distancia2 es el resultado publicado de 142470 (distancia al cuadrado).
// La pausa no impide caducar ni recoger: ambas comprobaciones van antes.
inline Salida PasoAntesDeDibujar(Objeto& o,float distancia2,bool pausa,float paso){
  if(o.limite<o.tiempo)return Salida::Caducado;
  if(distancia2<80.f)return Salida::Contacto;
  if(pausa)return Salida::Dibujar;
  if(o.tipo==2||o.tipo==4){
    for(unsigned i=0;i<3;++i)o.velocidad[i]*=.9f;
    // 1424A0: VMUL.xyz, VADDx.y, VADDx.z; W se conserva.
    const float z2=o.velocidad[2]*o.velocidad[2];
    const float x2=o.velocidad[0]*o.velocidad[0];
    const float y2=o.velocidad[1]*o.velocidad[1];
    const float xy=x2+y2;
    if(std::sqrt(xy+z2)>1.f){
      for(unsigned i=0;i<3;++i)o.posicion[i]+=o.velocidad[i];
      o.giro+=0x71cu;
    }
  }else if(o.tipo==0||o.tipo==1||o.tipo==3){
    for(unsigned i=0;i<3;++i)o.posicion[i]+=o.velocidad[i];
    if(o.tipo!=1)o.giro+=0x38e3u;
  }
  o.posicion[1]=0;
  o.tiempo+=paso;
  return Salida::Dibujar;
}
} // namespace dbz3::iwbubbles::objetos
