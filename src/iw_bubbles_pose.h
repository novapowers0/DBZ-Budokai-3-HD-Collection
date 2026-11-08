// Bubbles: TARGET, LIVE y boca independientes del lector Euler generico.
// Contrato EU 137B50/136A60/207180/25FA90; contrastes en INFORME_solIWX.
// Recursos y tabla trigonometrica proceden exclusivamente de la copia del jugador.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace dbz3::iwbubbles::poses {
struct V4 {
  float x=0,y=0,z=0,w=1;
  float& operator[](size_t i){return (&x)[i];}
  float operator[](size_t i)const{return (&x)[i];}
};
struct Target {V4 q,p,s{1,1,1,1};uint32_t cache[4]{};};
struct Hueso {float matriz[16]{};V4 q,p,s{1,1,1,1};uint32_t reserva[4]{};};
static_assert(sizeof(Target)==64 && sizeof(Hueso)==128);
inline float Truncar(double x){
  float r=float(x);
  if(std::fabs(double(r))>std::fabs(x)){
    uint32_t u;std::memcpy(&u,&r,4);
    if(u&0x7fffffffu){--u;std::memcpy(&r,&u,4);}
  }
  return r;
}
inline V4 Interpolar(V4 a,V4 b,float t){
  V4 r;for(size_t i=0;i<3;++i)r[i]=Truncar(double(a[i])+(double(b[i])-a[i])*t);return r;
}
inline V4 Girar(V4 a,V4 b,float t){
  double dot=0;for(size_t i=0;i<4;++i)dot+=double(a[i])*b[i];
  if(dot<0)for(size_t i=0;i<4;++i)b[i]=-b[i];
  V4 r;double n=0;
  for(size_t i=0;i<4;++i){r[i]=float(double(a[i])+(double(b[i])-a[i])*t);n+=double(r[i])*r[i];}
  if(!(n>0)||!std::isfinite(n))throw std::runtime_error("cuaternion BBR invalido");
  const double inv=1/std::sqrt(n);for(size_t i=0;i<4;++i)r[i]=float(r[i]*inv);return r;
}
inline void Matriz(Hueso& h){
  const float x=h.q.x,y=h.q.y,z=h.q.z,w=h.q.w;
  const float m[16]={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
    2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
    2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,h.p.x,h.p.y,h.p.z,1};
  std::copy(m,m+16,h.matriz); // La escala se aplica despues, al construir la jerarquia.
}
inline std::array<float,12> MatrizDibujo(const Hueso& h,V4 reposo){
  // EU1384F0: primero traslada por ejeAMG+10, despues concatena LIVE.
  // LIVE.p es un desplazamiento: no sustituye la longitud estatica del hueso.
  // No modificar LIVE: lo consumen tambien la boca y la locomocion.
  std::array<float,12> m{};
  for(size_t f=0;f<3;++f){
    for(size_t c=0;c<3;++c)m[f*4+c]=h.matriz[c*4+f]*h.s[c];
    m[f*4+3]=reposo[f]+h.matriz[12+f];
  }
  return m;
}
struct Bytes {
  const std::vector<uint8_t>& b;
  template<class T>T Leer(size_t p)const{
    if(p>b.size()||sizeof(T)>b.size()-p)throw std::runtime_error("recurso BBR truncado");
    T r;std::memcpy(&r,b.data()+p,sizeof r);return r;
  }
  uint32_t U(size_t p)const{return Leer<uint32_t>(p);}
  std::string Nombre(size_t p)const{
    if(p>b.size()||32>b.size()-p)throw std::runtime_error("nombre BBR truncado");
    size_t n=0;while(n<32 && b[p+n])++n;return std::string(reinterpret_cast<const char*>(b.data()+p),n);
  }
};
inline std::vector<float> LeerLut(const std::vector<uint8_t>& elf){
  // Tabla de seno retail redondeada a seis decimales: std::sin NO es equivalente.
  if(elf.size()<52 || std::memcmp(elf.data(),"\x7f" "ELF",4) || elf[4]!=1 || elf[5]!=1)
    throw std::runtime_error("falta ELF PS2 para LUT BBR");
  const float firma[4]={0,.000096f,.000192f,.000288f};
  std::vector<float> resultado;
  for(size_t p=0;p+16385*4<=elf.size();p+=4){
    if(std::memcmp(elf.data()+p,firma,sizeof firma))continue;
    std::vector<float> tabla(16385);std::memcpy(tabla.data(),elf.data()+p,tabla.size()*4);
    bool valida=tabla.back()==1.f && tabla[8192]==.707107f;
    for(size_t i=1;valida&&i<tabla.size();++i)
      valida=std::isfinite(tabla[i])&&tabla[i]>=tabla[i-1]&&tabla[i]<=1;
    if(!valida)continue;
    if(!resultado.empty())throw std::runtime_error("LUT BBR ambigua en ELF");
    resultado=std::move(tabla);
  }
  if(resultado.empty())throw std::runtime_error("LUT BBR no encontrada en ELF");
  return resultado;
}
template<size_t N>struct Banco {
  std::vector<uint8_t> amo,amm;
  std::array<int16_t,N> mapa{};
  uint32_t ejes=0;
  void Iniciar(std::vector<uint8_t> a,std::vector<uint8_t> m){
    amo=std::move(a);amm=std::move(m);Bytes ab{amo},mb{amm};
    if(amo.size()<40||amm.size()<32||std::memcmp(amo.data(),"#AMO",4)||std::memcmp(amm.data(),"#AMM",4)||ab.U(16)!=N)
      throw std::runtime_error("AMO/AMM BBR incompatible");
    const uint32_t amg=ab.U(ab.U(28));ejes=amg+ab.U(amg+20);
    for(size_t i=0;i<N;++i){
      mapa[i]=-1;const auto nombre=ab.Nombre(ab.U(36)+32*i);
      for(uint32_t j=0;j<mb.U(24);++j)if(nombre==mb.Nombre(mb.U(28)+32*j)){
        if(mapa[i]>=0)throw std::runtime_error("hueso AMM BBR duplicado");mapa[i]=int16_t(j);
      }
      ab.Leer<V4>(ejes+80*i+32);
    }
  }
  unsigned Cuadros(unsigned clip)const{
    const Bytes b{amm};if(clip>=b.U(16))throw std::runtime_error("clip BBR fuera del banco");
    return b.U(b.U(20)+16*clip+8);
  }
  std::array<Target,N> Reposo()const{
    const Bytes a{amo};std::array<Target,N> salida;
    for(size_t i=0;i<N;++i){salida[i].q=a.Leer<V4>(ejes+80*i);salida[i].s=a.Leer<V4>(ejes+80*i+32);}
    return salida;
  }
  std::array<Target,N> Muestrear(unsigned clip,float f,const std::vector<float>& lut)const{
    if(lut.size()!=16385||!std::isfinite(f)||f<0)throw std::runtime_error("muestreo BBR invalido");
    Cuadros(clip);const Bytes a{amo},b{amm};const auto desc=b.U(20)+16*clip;
    const auto flags=b.U(desc),pistas=flags&16?3u:2u,off=b.U(desc+12);
    auto seno=[&](unsigned ang){ang&=65535;unsigned j=ang&16383;if(ang&16384)j=16384-j;return ang&32768?-lut[j]:lut[j];};
    auto q=[&](size_t p){
      const unsigned x=b.Leer<uint16_t>(p+2)>>1,y=b.Leer<uint16_t>(p+4)>>1,z=b.Leer<uint16_t>(p+6)>>1;
      const double sx=seno(x),cx=seno(x+16384),sy=seno(y),cy=seno(y+16384),sz=seno(z),cz=seno(z+16384);
      return V4{float(sx*cy*cz-cx*sy*sz),float(cx*sy*cz+sx*cy*sz),float(cx*cy*sz-sx*sy*cz),float(cx*cy*cz+sx*sy*sz)};
    };
    auto curva=[&](uint32_t p,bool giro){
      const unsigned n=b.U(p+8),stride=giro?8:16;
      if(!n||n>10000)throw std::runtime_error("curva BBR invalida");
      auto tiempo=[&](unsigned i){return giro?unsigned(b.Leer<uint16_t>(p+12+stride*i)):b.U(p+12+stride*i);};
      unsigned ia=0;while(ia+1<n&&tiempo(ia+1)<=f)++ia;const auto ib=std::min(ia+1,n-1);
      const float delta=float(tiempo(ib))-float(tiempo(ia));
      const float t=delta>0?std::clamp((f-float(tiempo(ia)))/delta,0.f,1.f):0;
      if(giro){const auto qa=q(p+12+stride*ia);return t>0?Girar(qa,q(p+12+stride*ib),t):qa;}
      return Interpolar(b.Leer<V4>(p+16+stride*ia),b.Leer<V4>(p+16+stride*ib),t);
    };
    std::array<Target,N> salida;
    for(size_t i=0;i<N;++i){
      auto& h=salida[i];h.q=a.Leer<V4>(ejes+80*i);h.s=a.Leer<V4>(ejes+80*i+32);
      if(mapa[i]<0)continue;
      auto ptr=[&](unsigned c){return b.U(off+4*(pistas*unsigned(mapa[i])+c));};
      if((flags&1)&&ptr(0))h.q=curva(ptr(0),true);
      if(const auto p=ptr(flags&1?1:0))h.p=curva(p,false);
      if((flags&16)&&ptr(2))h.s=curva(ptr(2),false);
    }
    return salida;
  }
};
template<size_t N>struct Instancia {
  std::array<Hueso,N> live{};
  float u=1,rate=0;
  bool iniciada=false;
  void Iniciar(const std::array<Target,N>& pose){
    for(size_t i=0;i<N;++i){live[i].q=Girar(pose[i].q,pose[i].q,0);live[i].p=pose[i].p;live[i].s=pose[i].s;Matriz(live[i]);}
    u=1;rate=0;iniciada=true;
  }
  float AplicarFactor(const std::array<Target,N>& pose,float beta){
    if(!iniciada||!std::isfinite(beta)||beta<0||beta>1)throw std::runtime_error("mezcla BBR sin LIVE");
    for(size_t i=0;i<N;++i){auto& h=live[i];
      if(beta==1){h.q=Girar(pose[i].q,pose[i].q,0);h.p=pose[i].p;h.s=pose[i].s;}
      else if(beta>0){h.q=Girar(h.q,pose[i].q,beta);h.p=Interpolar(h.p,pose[i].p,beta);h.s=Interpolar(h.s,pose[i].s,beta);}
      Matriz(h);
    }
    return beta;
  }
  float Aplicar(const std::array<Target,N>& pose,float paso){
    const float a=Truncar(double(u)-1),t=Truncar(double(a)*a);
    const float beta=paso>0?std::clamp(float((1-double(t))*u+double(t)*u*paso),0.f,1.f):0;
    AplicarFactor(pose,beta);u=std::min(1.f,Truncar(double(u)+double(rate)*paso));return beta;
  }
};
struct Recursos {
  Banco<47> cuerpo;Banco<8> cara;std::vector<float> lut;
  std::array<unsigned,0x11E> clips{};
  std::array<std::pair<unsigned,unsigned>,7> boca{}; // destino corporal, origen facial
  void Iniciar(std::vector<uint8_t> amo,std::vector<uint8_t> amm,const std::vector<uint8_t>& bsk,
               std::vector<uint8_t> facial_amo,std::vector<uint8_t> facial_amm,const std::vector<uint8_t>& elf){
    cuerpo.Iniciar(std::move(amo),std::move(amm));cara.Iniciar(std::move(facial_amo),std::move(facial_amm));lut=LeerLut(elf);
    const Bytes b{bsk},a{cuerpo.amo},f{cara.amo};
    if(bsk.size()<32||std::memcmp(bsk.data(),"#BSK",4))throw std::runtime_error("BSK BBR invalido");
    clips.fill(~0u);
    for(unsigned code:{0xD2,0xD3,0xD4,0xDD,0xDF,0xE0,0xE1,0xD5,0xD6,0xE2,0xD8,0x11D}){
      const auto p=b.U(b.U(20)+4*code);
      if(!p||b.Leer<uint16_t>(p+2)!=3)throw std::runtime_error("BSK BBR exige almacen propio");
      clips[code]=b.Leer<uint16_t>(p);cuerpo.Cuadros(clips[code]);
    }
    auto sinX=[](std::string n){if(!n.empty()&&n[0]=='X')n.erase(0,1);return n;};
    size_t n=0;
    for(unsigned j=0;j<8;++j){const auto nombre=sinX(f.Nombre(f.U(36)+32*j));
      if(nombre.find("_M_")==std::string::npos)continue;
      unsigned destino=47;
      for(unsigned i=0;i<47;++i)if(sinX(a.Nombre(a.U(36)+32*i))==nombre){
        if(destino!=47)throw std::runtime_error("destino boca BBR ambiguo");destino=i;
      }
      if(destino==47||n==boca.size())throw std::runtime_error("mapa boca BBR incompatible");boca[n++]={destino,j};
    }
    if(n!=boca.size())throw std::runtime_error("faltan siete huesos faciales BBR");
  }
  std::array<Target,47> Muestrear(unsigned codigo,float cuadro)const{
    if(codigo>=clips.size()||clips[codigo]==~0u)throw std::runtime_error("codigo BBR sin resolver");
    return cuerpo.Muestrear(clips[codigo],cuadro,lut);
  }
  void PublicarCara(Instancia<47>& cuerpo_live,const Instancia<8>& cara_live)const{
    if(!cuerpo_live.iniciada||!cara_live.iniciada)throw std::runtime_error("boca BBR sin LIVE");
    for(const auto& par:boca)cuerpo_live.live[par.first]=cara_live.live[par.second];
  }
};
} // namespace dbz3::iwbubbles::poses
