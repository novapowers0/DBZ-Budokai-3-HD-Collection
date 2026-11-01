// Consumidores AP IW con estado propio. Nunca escribir estos offsets PS2 en B3.
#pragma once
#include "iw_ap_eventos.h"
#include "iw_eventos.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace dbz3::anim::apev {
enum class Consumo {Aplicado,SinEfectoOriginal,Pendiente,DatosInvalidos};
struct Estado0 {
  int16_t cuenta29=0,cuenta2b=0;
  bool flag206e=false,flag206f=false,oculto_ccd=false;
  float cuadro_objetivo18e4=-1,cuadro_c68=0,velocidad_cb4=1;
};
// Animations2 solo llama205660 en estas escenas IW, no con el estado de B3.
inline bool EscenaAP0(int escena) {return escena==2 || escena==3 || escena==4 || escena==7;}
inline Consumo Consumir0(int escena,uint32_t valor,Estado0& s) {
  if(escena<0)return Consumo::Pendiente; // contexto IW no aportado, no inventar silencio
  if(!EscenaAP0(escena))return Consumo::SinEfectoOriginal;
  const int v=int16_t(uint16_t(valor));
  switch(v) {
    case 0x29:s.cuenta29=int16_t(std::min(3,int(int16_t(uint16_t(s.cuenta29)+1))));return Consumo::Aplicado;
    case 0x2b:s.cuenta2b=int16_t(std::min(3,int(int16_t(uint16_t(s.cuenta2b)+1))));return Consumo::Aplicado;
    case 0x3c:s.flag206e=true;return Consumo::Aplicado;
    case 0x3d:s.flag206e=false;return Consumo::Aplicado;
    case 0x50:s.oculto_ccd=true;return Consumo::Aplicado;
    case 0x51:s.oculto_ccd=false;return Consumo::Aplicado;
    case 0x47:
      if(s.cuadro_objetivo18e4<0)return Consumo::SinEfectoOriginal;
      s.cuadro_c68=s.cuadro_objetivo18e4;s.velocidad_cb4=.1f;return Consumo::Aplicado;
    default:return v<0 || v>200?Consumo::SinEfectoOriginal:Consumo::Pendiente;
  }
}

struct RegistroEfectos4 {int codigo_b60=0,primero24=-1,comando48=-1;};
enum class Ruta4 {ProyectilIW,TablaEfectosIW,ComandoIW};
struct Pedido4 {int id=0;bool contrario=false;Ruta4 ruta=Ruta4::ProyectilIW;bool paquete_actor=false;};
// AP4 llama1BF610 con paquete {actor,0}, NO con {actor,valor}. Por eso los
// contadores de las ramas5/6/7 de esa funcion no son los eventos AP4=5/6/7.
// Se porta aqui el consumidor comun1BEC60, incluido destino/registro por actor.
inline Pedido4 Preparar4(uint32_t valor,RegistroEfectos4& propio,RegistroEfectos4& rival) {
  const int id=int16_t(uint16_t(valor));const bool contrario=(id>=200&&id<300)||(id>=400&&id<500);
  auto& s=contrario?rival:propio;
  if(s.primero24==-1) {s.primero24=s.codigo_b60;if(id>=300&&id<304)s.comando48=s.codigo_b60;}
  const auto ruta=id<4 || id>349?Ruta4::ProyectilIW:id<300?Ruta4::TablaEfectosIW:Ruta4::ComandoIW;
  // El comando300..349 recibe el paquete {actor,0} original; las otras dos
  // ramas reciben directamente el actor seleccionado. No son el mismo ABI.
  return {id,contrario,ruta,ruta==Ruta4::ComandoIW};
}

struct V3 {float x=0,y=0,z=0;};
struct Impacto5 {
  int tipo=-1,ancla_propia=0,ancla_rival=-1,lado=0;
  bool invertir=false;
  V3 posicion,direccion;
};
// Modelo+4DC/4E0 == personaje+B1C/B20.22F090 lee TODOS los codigos y pasa a
//1FBE00: digitos decimales, no bitfields ni IDs del renderer B3.
inline bool Preparar5(uint32_t valor,std::span<const V3> propio,std::span<const V3> rival,int lado,Impacto5& salida) {
  const int v=int32_t(valor),resto=v%1000000,op=resto/10000,ab=(resto%10000)/100,tipo=resto%100;
  const int a=ab>0?ab-1:0,b=op>0?op-1:-1;
  if(tipo<0 || tipo>=20 || propio.empty() || rival.empty() || a<0 || size_t(a)>=propio.size() || (b>=0&&size_t(b)>=rival.size()))return false;
  Impacto5 r;r.tipo=tipo;r.ancla_propia=a;r.ancla_rival=b;r.lado=lado;r.invertir=v/1000000==1;
  const auto pa=propio[size_t(a)];
  if(b<0) {
    r.posicion=pa;r.direccion={rival[0].x-propio[0].x,rival[0].y-propio[0].y,rival[0].z-propio[0].z};
  } else {
    const auto pb=rival[size_t(b)];r.posicion={pa.x+(pb.x-pa.x)*.5f,pa.y+(pb.y-pa.y)*.5f,pa.z+(pb.z-pa.z)*.5f};
    r.direccion={pb.x-pa.x,pb.y-pa.y,pb.z-pa.z};
  }
  if(r.invertir)r.direccion={-r.direccion.x,-r.direccion.y,-r.direccion.z};
  const float longitud=std::sqrt(r.direccion.x*r.direccion.x+r.direccion.y*r.direccion.y+r.direccion.z*r.direccion.z);
  if(longitud>0) {const float k=1.0f/longitud;r.direccion={r.direccion.x*k,r.direccion.y*k,r.direccion.z*k};}
  salida=r;return true;
}
// Solo resolver la funcion IW de la tabla; tipo0..19 NO es un indice AME.
inline uint32_t FuncionImpacto5(const std::vector<uint8_t>& elf,int tipo) {
  if(tipo<0 || tipo>=20)return 0;
  const uint32_t tabla=elf.size()==3786800?0x4941E0u:elf.size()==3784752?0x4939E0u:0;
  if(!tabla)return 0;const size_t p=ElfOff(elf,tabla+4*uint32_t(tipo),4);
  return p==elf.size()?0:U32(elf,p);
}
struct EstadoExterno {
  std::array<uint32_t,7> cola5{};uint8_t cuenta5=0;
  float valor244=0; // modelo+514 == personaje+B54, persiste entre evaluaciones
  void Reiniciar(){cuenta5=0;valor244=0;}
  void Actualizar(const eventos::Lote& lote) {
    cuenta5=lote.usados[5];for(size_t i=0;i<cuenta5;++i)cola5[i]=lote.colas[5][i].valor;
    if(lote.usados[9])valor244=float(int32_t(lote.colas[9][0].valor))/10.0f;
  }
  // Leer no vacia la cola:22F090 no la borra; la siguiente evaluacion AP si.
  template<class Emitir>
  void Leer5(Emitir emitir)const {for(size_t i=0;i<cuenta5;++i)emitir(cola5[i]);}
  float FactorMovimiento(float defecto)const{return valor244==0.0f?defecto:valor244;}
  // EU299F40..299F88: primero escala*cac, despues velocidad y finalmenteB54.
  float VelocidadCampo(float velocidad,float escala,float cac)const {
    const float a=escala*cac;const float b=velocidad*a;return b*FactorMovimiento(.4f);
  }
};
} // namespace dbz3::anim::apev
