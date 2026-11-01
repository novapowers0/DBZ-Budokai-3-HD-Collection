// AP8: #AMK contiene SELECTORES de boca, no matrices ni un numero de pose B3.
// EU1BA5D0/1BA850/1BA720 y206E20/206EE0/207180; copia de boca25FA90.
#pragma once
#include "iw_ap_eventos.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <string>

namespace dbz3::anim::apev::boca {
struct Clave {int16_t cuadro=0;uint8_t visema=0,variante=0;};
struct Clip {int32_t duracion=0;std::vector<Clave> claves;};
class BancoAMK {
 public:
  bool Cargar(const std::vector<uint8_t>& b) {
    if(!Rango(b,0,32) || U32(b,0)!=0x4b4d4123 || U32(b,16)!=4)return false;
    const size_t n=U32(b,20),tabla=U32(b,24);
    if(!Rango(b,tabla,0) || n>(b.size()-tabla)/16)return false;
    std::vector<Clip> nuevo(n);
    for(size_t i=0;i<n;++i) {
      const size_t p=tabla+16*i,cantidad=U32(b,p+4),datos=U32(b,p+12);
      nuevo[i].duracion=int32_t(U32(b,p));
      if(!cantidad)continue;
      if(nuevo[i].duracion<0 || !datos || !Rango(b,datos,0) || cantidad>(b.size()-datos)/4)return false;
      for(size_t k=0;k<cantidad;++k) {
        const size_t q=datos+4*k;
        Clave c{int16_t(uint16_t(b[q])|uint16_t(b[q+1])<<8),b[q+2],b[q+3]};
        if(c.visema>5 || c.variante>=128 || (k && c.cuadro<nuevo[i].claves.back().cuadro))return false;
        nuevo[i].claves.push_back(c);
      }
    }
    clips_=std::move(nuevo);return true; // error no destruye un banco anterior
  }
  const Clip* Obtener(uint32_t id)const {
    return id<clips_.size() && !clips_[id].claves.empty()?&clips_[id]:nullptr;
  }
  size_t Cantidad()const{return clips_.size();}
 private:std::vector<Clip> clips_;
};
struct Seleccion {uint8_t visema=0;int variante=0;size_t pose=0,clave=0;};
class Estado {
 public:
  uint32_t id=0xffffffffu;float cuadro=0;bool ignorar_pausa=false;
  bool IniciarAP8(const BancoAMK& banco,uint32_t valor,bool congelado=false,bool corte8bd8=false) {
    if(congelado || corte8bd8)return false; // AP8 bloqueado conserva la boca actual
    id=uint32_t(int32_t(int16_t(uint16_t(valor))))|0x10000u;cuadro=0;ignorar_pausa=false;
    if(int32_t(id)<0 || !banco.Obtener(id&65535)){Reiniciar();return false;}
    return true; // despues de validar, copiar personaje -> hablante (invertir=1)
  }
  void Reiniciar(){id=0xffffffffu;cuadro=0;ignorar_pausa=false;}
  void Avanzar(const BancoAMK& banco,float paso,bool pausa,bool congelado=false) {
    if(congelado || int32_t(id)<0 || (pausa&&!ignorar_pausa))return;
    const auto c=banco.Obtener(id&65535);if(!c){Reiniciar();return;}
    cuadro+=paso;if(float(c->duracion-1)<=cuadro)Reiniciar(); // no bucle
  }
  bool Seleccionar(const BancoAMK& banco,size_t poses,Seleccion& salida)const {
    if(poses<6)return false;
    Clave c;size_t key=0;
    if(int32_t(id)>=0) {
      const auto clip=banco.Obtener(id&65535);if(!clip)return false;
      //1BA850: solo el ultimo usa time-1; los interiores van por time RAW.
      if(float(clip->claves.back().cuadro-1)<=cuadro)key=clip->claves.size()-1;
      else if(cuadro>=1) {
        while(key+1<clip->claves.size() && float(clip->claves[key+1].cuadro)<=cuadro)++key;
      }
      c=clip->claves[key];
    } //206EE0 invalido -> Char_PushAnimIds(0,0): pose neutra0
    const int variante=std::min<int>(c.variante,int(poses/6)-1);
    salida={c.visema,variante,size_t(variante*6+c.visema),key};return salida.pose<poses;
  }
};

using MatrizHueso=std::array<uint32_t,32>; //128B completos, incluidos locales/flags
using NueveMatrices=std::array<MatrizHueso,9>;
using Indices=std::array<int16_t,9>;
inline constexpr Indices kNueve={0,1,2,3,4,5,6,7,8};
// Los indices vienen de nombres o de la estructura cargada, JAMAS del idAP8.
inline Indices PorNombres(const std::array<std::string,9>& originales,std::span<const std::string> anfitrion) {
  Indices out;out.fill(-1);
  for(size_t i=0;i<9;++i)for(size_t j=0;j<anfitrion.size() && j<=32767;++j)
    if(!originales[i].empty() && originales[i]==anfitrion[j]){out[i]=int16_t(j);break;}
  return out;
}
//25FA90: copia ordenada de nueve registros128B, ignora parejas con s16<0.
// Validacion adicional del host antes de escribir; un indice fuera de rango
// devuelve-1, no una boca aplicada parcialmente.
inline int Copiar9(std::span<const MatrizHueso> fuente,const Indices& a,
                   std::span<MatrizHueso> destino,const Indices& b) {
  for(size_t i=0;i<9;++i)
    if(a[i]>=0 && b[i]>=0 && (size_t(a[i])>=fuente.size() || size_t(b[i])>=destino.size()))return -1;
  int n=0;for(size_t i=0;i<9;++i)if(a[i]>=0 && b[i]>=0){destino[size_t(b[i])]=fuente[size_t(a[i])];++n;}
  return n;
}
//136A60 +13CEA0/13CE40/13C7A0: nlerp por hemisferio, normalizacion,
// posicion/escala interpoladas y reconstruccion de la matriz LOCAL. No se
// interpola una matriz4x4 ni se inventa una rotacion a partir del visema.
inline void MezclarMatriz(MatrizHueso& actual,const MatrizHueso& objetivo,float factor) {
  auto f=[](uint32_t v){return std::bit_cast<float>(v);};
  auto w=[](float v){return std::bit_cast<uint32_t>(v);};
  float q[4],t[4];for(size_t k=0;k<4;++k){q[k]=f(actual[16+k]);t[k]=f(objetivo[16+k]);}
  const float dot=((q[3]*t[3]+q[2]*t[2])+q[1]*t[1])+q[0]*t[0];
  const float inverso=1.0f-factor;
  for(size_t k=0;k<4;++k)q[k]=factor*(dot<0?-t[k]:t[k])+inverso*q[k];
  const float norma=((q[3]*q[3]+q[2]*q[2])+q[1]*q[1])+q[0]*q[0];
  const float rec=norma>0?1.0f/std::sqrt(norma):1.0f;
  for(size_t k=0;k<4;++k){q[k]*=rec;actual[16+k]=w(q[k]);}
  for(size_t k=20;k<28;++k){const float a=f(actual[k]),b=f(objetivo[k]);actual[k]=w(a+factor*(b-a));}
  const float x=q[0],y=q[1],z=q[2],qw=q[3];
  const float yy=y+y,zz=z+z,xx=x+x,xx2=x*xx,xy=x*yy,xz=x*zz,y2=y*yy,yz=y*zz,z2=z*zz;
  const float wx=qw*xx,wy=qw*yy,wz=qw*zz;
  const float m[12]={1-(y2+z2),xy+wz,xz-wy,0,xy-wz,1-(xx2+z2),yz+wx,0,xz+wy,yz-wx,1-(xx2+y2),0};
  for(size_t k=0;k<12;++k)actual[k]=w(m[k]);
  for(size_t k=0;k<4;++k)actual[12+k]=actual[20+k];
}
// Consumidor de matrices: requiere las POSES AMM reales del modelo de boca,
// no inventa rotaciones a partir de los selectoresAMK. Paleta vacia = pendiente.
// La infraestructura existente anim::efecto::CopiarBoca es la misma operacion
// para memoria guest: usarla al iniciar(1) y tras evaluar la pose del hablante(0).
class Motor {
 public:
  Estado estado;
  NueveMatrices respaldo{},hablante{};
  bool Iniciar(const BancoAMK& banco,uint32_t valor,std::span<const MatrizHueso> cuerpo,
               const Indices& indices,bool congelado=false,bool corte8bd8=false) {
    if(!estado.IniciarAP8(banco,valor,congelado,corte8bd8))return false;
    if(Copiar9(cuerpo,indices,respaldo,kNueve)<0){estado.Reiniciar();return false;}
    hablante=respaldo;listo_=true;
    return true;
  }
  int Aplicar(const BancoAMK& banco,std::span<const NueveMatrices> poses,
              std::span<MatrizHueso> cuerpo,const Indices& indices,bool congelado=false) {
    if(congelado)return 0;if(!listo_)return -1;
    Seleccion s;if(!estado.Seleccionar(banco,poses.size(),s))return -1;
    if(int32_t(estado.id)>=0) {
      if(ultimo_id_!=estado.id){actual_var_=anterior_var_=255;ultima_clave_=size_t(-1);ultimo_cuadro_=-1;}
      if(ultimo_cuadro_<estado.cuadro)ultima_clave_=size_t(-1); //206EE0 invalida cursor al avanzar
      if(ultima_clave_!=s.clave){anterior_var_=actual_var_;actual_var_=s.variante;ultima_clave_=s.clave;}
      ultimo_cuadro_=estado.cuadro;
    } else {anterior_var_=actual_var_;actual_var_=0;ultima_clave_=size_t(-1);}
    ultimo_id_=estado.id;
    const float factor=anterior_var_==actual_var_?.6f:.75f;
    for(size_t i=0;i<9;++i)MezclarMatriz(hablante[i],poses[s.pose][i],factor);
    return Copiar9(hablante,kNueve,cuerpo,indices);
  }
 private:uint32_t ultimo_id_=0xffffffffu;size_t ultima_clave_=size_t(-1);int actual_var_=255,anterior_var_=255;float ultimo_cuadro_=-1;bool listo_=false;
};
} // namespace dbz3::anim::apev::boca
