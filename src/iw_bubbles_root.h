// IW EU 31A7C0/239A40/2F5DA0 y tarea5. Integracion candidata, normal apagado.
// Trasladado del helper staged; sin offsets B3 ni dependencias de suelo_iw.
#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace dbz3::iwbubbles::noche {
inline constexpr bool kNormalValidado=false;
inline constexpr bool kBackendColisionExactaDisponible=false;
//EE observado redondea hacia0 (reloj y ADD.S de raiz). Double conserva exactamente los
//operandos float32 de estos relojes; volatile impide fusionar/reducir precision.
inline float ClockTrunc(double exacto){
  volatile float redondeado=static_cast<float>(exacto);float r=redondeado;
  if(std::fabs(static_cast<double>(r))>std::fabs(exacto)){
    std::uint32_t bits=0;std::memcpy(&bits,&r,4);
    if(bits&0x7fffffffu){--bits;std::memcpy(&r,&bits,4);}
  }
  return r;
}
inline float ClockAdd(float a,float b){volatile double v=static_cast<double>(a)+static_cast<double>(b);return ClockTrunc(v);}
inline float ClockMul(float a,float b){volatile double v=static_cast<double>(a)*static_cast<double>(b);return ClockTrunc(v);}
struct ClockPaso {float avanzado=0,siguiente=0;bool fin=false;};
inline ClockPaso AvanzarClock(float cuadro,float paso,float longitud){
  const float avanzado=ClockAdd(cuadro,paso);const bool fin=longitud>0&&avanzado>=longitud;
  return {avanzado,fin?0.f:avanzado,fin}; //sin fmod ni coeficientes adicionales.
}
struct V { float x=0,y=0,z=0; };
inline V Mas(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V Menos(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V Por(V a,float k){return {a.x*k,a.y*k,a.z*k};}
inline V Unidad(V a){const float l=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);return l>0?Por(a,1/l):V{};}
struct R3 {
  // Matriz de rotacion CURRENT de Work+0/4/8. Sin traslacion ni escala.
  std::array<float,9> m{1,0,0,0,1,0,0,0,1};
  V Girar(V a)const{return {m[0]*a.x+m[1]*a.y+m[2]*a.z,
      m[3]*a.x+m[4]*a.y+m[5]*a.z,m[6]*a.x+m[7]*a.y+m[8]*a.z};}
};
// Posar resta esq.centro.x/z, pero el original conserva el origen del actor.
// Compensar SOLO el origen de dibujo animado; no mover la simulacion/colision.
inline V OrigenDibujoAnimado(V actor40,const R3& instancia,V centroEsqueleto){
  centroEsqueleto.y=0;return Mas(actor40,instancia.Girar(centroEsqueleto));
}
struct Root {
  V posicion40;
  V local50; // (*(model+8+4*model[40C]))+50: BBR usa indice1, NO bone0.
  R3 rotacion;
  std::uint32_t flags18e8=0;
  float alturaBB8c=0; // solo bit8: *(actor+BB8)+C. No confundir con *(B84).
};
struct MezclaEntrada { std::uint32_t cb4=0,cb8=0; };
inline MezclaEntrada Mezcla349(unsigned estado,std::int16_t cc4,bool faseE0=false){
  if(estado==0||estado==1)return {0x3ba3d70au,cc4==0?0x3cf5c28fu:0x3c23d70au};
  if(estado==2)return {0x3f800000u,0x3c23d70au};
  if(estado==5)return {0u,0x3dcccccdu};
  if(estado==6)return {0x3f800000u,0u};
  if(estado==4&&faseE0)return {0x3dcccccdu,0x3e99999au};
  if(estado==3||estado==4)return {0u,0x3c23d70au};
  if(estado==9)return {0x3ba3d70au,cc4==0?0x3cf5c28fu:0x3c23d70au};
  return {};
}
// Consume EL VECTOR ACTUAL, no una diferencia de frames. Bits2/4 independientes.
inline void ConsumirRaiz(Root& s) {
  const V d=s.rotacion.Girar(s.local50);
   //2F5DA0/EE19761: ADD.S hacia0 al publicar X/Z, no redondeo host al cercano.
   if(s.flags18e8&1u){s.posicion40.x=ClockAdd(s.posicion40.x,d.x);s.posicion40.z=ClockAdd(s.posicion40.z,d.z);s.local50.x=0;s.local50.z=0;}
  if(s.flags18e8&2u){s.posicion40.y+=d.y;s.local50.y=0;}
  if(s.flags18e8&4u){s.posicion40.y+=d.y;s.local50.y-=d.y;}
  else if(s.flags18e8&8u)s.local50.y=s.alturaBB8c;
  s.flags18e8=0;
}
struct Anclas;
template<class Host> inline void FinEstado9(Root& s,Anclas& a,std::int16_t& cc4,Host& h){
  cc4=0;s.flags18e8=1;h.RootAndBones(s,a);h.EntrarEstado0();
}
struct Anclas {
  V actual640,prev60,prev70;
  float alturaB84=0;
  bool aire2061=false;
};
template<class Host> inline void Sincronizar239A40(Root& s,Anclas& a,std::uint32_t mascara,Host& h) {
  h.CameraLocation(s,a,mascara);
  if(mascara&1u){
    a.prev70=a.actual640;a.prev70.y+=a.alturaB84;
    a.prev60=a.actual640;a.prev60.y=s.posicion40.y+(a.aire2061?a.alturaB84:0);
  }
}
struct Grid {
  float min_x=0,max_x=0,min_z=0,max_z=0;
  std::uint32_t flags=0;
  bool Valida()const{return max_x>min_x&&max_z>min_z;}
};
inline void Eje(float& x,float minimo,float maximo){
  if(x<minimo)x+=maximo-minimo;else if(x>=maximo)x-=maximo-minimo;
}
inline V Canonica(V p,const Grid& g){
  if(g.flags&1u)Eje(p.x,g.min_x,g.max_x);
  if(g.flags&2u)Eje(p.z,g.min_z,g.max_z);
  return p;
}
inline V ReferenciaCercana(V anterior,V actual,const Grid& g){
  V d=Menos(anterior,actual);
  if(g.flags&1u)Eje(d.x,-(g.max_x-g.min_x)*.5f,(g.max_x-g.min_x)*.5f);
  if(g.flags&2u)Eje(d.z,-(g.max_z-g.min_z)*.5f,(g.max_z-g.min_z)*.5f);
  return Mas(actual,d);
}
inline V EnvolverActorExterior(V actor40,V referenciaCamaraC0,const Grid& g){
  return ReferenciaCercana(actor40,referenciaCamaraC0,g);
}
struct Contacto {
  V normal;float plano_d=0;std::uint16_t material=0,tipo=0;
};
struct Barrido { bool toca=false;V corregida;Contacto contacto; };
struct Rayo { bool toca=false;V punto;Contacto contacto; };
struct Work {
  V anterior90;std::uint64_t tareaB8=0;Contacto contacto238;
  std::uint32_t tipoEsfera1f0=0,tipoSuelo1e8=0;
  float altura18d0=0,factor184c=0;
};
inline bool DesviarPorChoque(bool toca,std::uint16_t tipo){return toca&&(tipo==2||tipo==5);}
enum class Resultado { Aplicado, GridInvalida, FaltaBarridoExacto, FaltaTipoRayo };
template<class Host> inline Resultado Wrap31A7C0(Root& s,Anclas& a,Work& w,const Grid& g,Host& h){
  if(!g.Valida())return Resultado::GridInvalida;
  if(!h.BarridoExactoDisponible())return Resultado::FaltaBarridoExacto;
  if(!h.RayoConTipoDisponible())return Resultado::FaltaTipoRayo;
  const V viejo640=a.actual640;
  const V destino=Canonica(viejo640,g);
  const V desde=ReferenciaCercana(w.anterior90,destino,g);
  h.Fase(1);const Barrido b=h.Sweep(desde,destino,.5f,2u);
  const V corregida=Canonica(b.corregida,g);
  w.tipoEsfera1f0=b.toca?b.contacto.tipo:0;
  if(b.toca)w.contacto238=b.contacto;
  s.posicion40=Mas(s.posicion40,Menos(corregida,viejo640));
  h.Fase(2);h.RootAndBones(s,a);
  if(DesviarPorChoque(b.toca,b.contacto.tipo)&&w.tareaB8!=0){
    h.DestruirTarea(w.tareaB8);w.tareaB8=0;
    h.Fase(3);w.tareaB8=h.CrearTarea5(w.contacto238.normal);
  }
  V arriba=corregida,abajo=corregida;arriba.y+=4;abajo.y-=500;
  h.Fase(4);const Rayo r=h.Ray(arriba,abajo,7u);
  if(r.toca){w.contacto238=r.contacto;w.tipoSuelo1e8=r.contacto.tipo;}
  w.altura18d0=r.toca?r.punto.y:abajo.y;
  return Resultado::Aplicado;
}
template<class Host> inline Resultado TickMover(Root& s,Anclas& a,Work& w,const Grid& g,Host& h){
  if(!g.Valida())return Resultado::GridInvalida;
  if(!h.BarridoExactoDisponible())return Resultado::FaltaBarridoExacto;
  if(!h.RayoConTipoDisponible())return Resultado::FaltaTipoRayo;
  if(!w.tareaB8)h.MoverPorStick(s,w);
  w.anterior90=a.actual640;h.RotarHaciaMovimiento(s,w);
  Sincronizar239A40(s,a,0xffffffffu,h);
  const Resultado r=Wrap31A7C0(s,a,w,g,h);
  if(r!=Resultado::Aplicado)return r;
  if(!a.aire2061)s.posicion40.y=w.altura18d0;
  w.factor184c=.1f;return Resultado::Aplicado;
}
struct Tarea5 {
  unsigned modo=5,jugador=1,fase=0;
  float transcurrido=0,duracion=0,stick_x=.1f;
  V velocidad,vector30;
};
template<class Rng> inline Tarea5 PrepararTarea5(V normalContacto,int dificultad,
                    float tiempo8b80,float cac,float b54,Rng& rng){
  Tarea5 t;(void)rng.Memory(255);
  t.duracion=float(rng.Memory(32)%unsigned(200-20*dificultad));
  V d=normalContacto;d.y=0;d=Unidad(d);
  const bool arriba=(rng.Memory(32)&1u)!=0;
  const float mezcla=float(rng.Memory(32)%7u)*.1f;
  const V perpendicular=arriba?V{-d.z,0,d.x}:V{d.z,0,-d.x};
  d=Unidad(Mas(d,Por(Menos(perpendicular,d),mezcla)));
  t.velocidad=Por(d,tiempo8b80*cac*.25f*(b54==0?.4f:b54));
  t.vector30=Por(Unidad(t.velocidad),t.duracion);return t;
}
template<class Anim,class Esqueleto> inline bool MuestrearLocal(const Anim& clip,const Esqueleto& esq,
                                    unsigned indice,float cuadro,V& salida){
  if(indice>=esq.local.size())return false;
  salida={esq.local[indice].m[3],esq.local[indice].m[7],esq.local[indice].m[11]};
  if(indice>=clip.pos.size()||clip.pos[indice].empty())return true;
  const auto& ks=clip.pos[indice];std::size_t i=0;
  while(i+1<ks.size()&&ks[i+1][0]<=cuadro)++i;
  const std::size_t j=i+1<ks.size()?i+1:i;
  const float d=ks[j][0]-ks[i][0];
  float t=d>0?(cuadro-ks[i][0])/d:0;if(t<0)t=0;if(t>1)t=1;
  salida={ks[i][1]+t*(ks[j][1]-ks[i][1]),ks[i][2]+t*(ks[j][2]-ks[i][2]),ks[i][3]+t*(ks[j][3]-ks[i][3])};
  return true;
}
} // namespace dbz3::iwbubbles::noche
