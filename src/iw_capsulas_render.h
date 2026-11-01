// Consumidor propio CapsulePick. Programas y constantes se leen del ELF del
// jugador. El VU ejecuta sus pares originales; un opcode desconocido FALLA.
#pragma once
#include "iw_spx_capsulas.h"
#include <optional>

namespace dbz3::iw::capsrender {
using Bytes=spx::capsulas::Bytes;
using Qword=std::array<uint32_t,4>;
inline float F(uint32_t x){return spx::Float(x);}
inline uint32_t B(float x){return spx::Bits(x);}
inline int Signo(uint32_t x,unsigned n){const uint32_t s=1u<<(n-1);return int((x^s)-s);}
struct Vec3 {float x=0,y=0,z=0;};
// COP2 usa el redondeo VU hacia cero. Mantenerlo local: cambiar el modo
// flotante del hilo UI afectaria tambien a los consumidores ajenos.
inline float TruncarVU(double x) {
  const float r=float(x);
  return std::isfinite(r) && ((x>0&&double(r)>x)||(x<0&&double(r)<x))?std::nextafter(r,0.f):r;
}
inline float ProductoPS2(float a,float b){return TruncarVU(double(a)*b);}
inline Vec3 Mas(Vec3 a,Vec3 b){return {TruncarVU(double(a.x)+b.x),TruncarVU(double(a.y)+b.y),TruncarVU(double(a.z)+b.z)};}
inline Vec3 Por(Vec3 a,float b){return {TruncarVU(double(a.x)*b),TruncarVU(double(a.y)*b),TruncarVU(double(a.z)*b)};}
inline Vec3 Unitario(Vec3 a){const float n=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);if(!n)return {};return Por(a,1/n);}
struct DefAme {
  uint32_t offset=0,tipo=0,flags=0,hijo=0,hermano=0;
  Vec3 pos;std::array<float,4> quat{0,0,0,1};std::array<float,48> f{};
  int tex=-1,mezcla=0;std::vector<std::array<float,5>> anim;
};
struct PartAme {
  // Tipo0 guarda ancho/alto finales en +50/+54 y sus bases en +58/+5c.
  // Tipos1/3 guardan semitamanio y escala separada (no son el mismo layout).
  Vec3 p,v;float edad=0,tamano=0,escala=1,angulo=0,anim_t=0,ancho_base=0,alto_base=0;
  uint32_t flags=0,cuadro=0;std::array<float,4> color{},uv{0,0,1,1},quat{0,0,0,1};
};
struct NodoAme {
  size_t def=0;int hijo=-1;std::vector<size_t> hijos;
  bool terminar=false,retirado=false;float edad=0;std::vector<float> acumulado;
  std::vector<PartAme> particulas;
};
// Especializacion por FORMATO, no por un cronograma de cuadros.972 contiene
// emisores esfera y particulas0/1/3, con hijos de particula en +24. Se conserva
// el orden postorder de277150 y los nacimientos de276D30 al morir el padre.
class Ame {
 public:
  std::vector<DefAme> defs;std::vector<NodoAme> nodos;
  uint64_t cuadros=0;size_t nacidas=0;bool retirado=false;
  explicit Ame(const Bytes& b) {
    using namespace spx::capsulas;
    if(!Magic(b,"#AME")||U(b,4)!=2)throw std::runtime_error("Capsulas AME: version no soportada");
    std::map<uint32_t,size_t> mapa;std::vector<uint32_t> raices;std::map<uint32_t,bool> vistos;
    std::function<void(uint32_t,bool)> leer=[&](uint32_t p,bool raiz) {
      while(p) {
        if(vistos[p])throw std::runtime_error("Capsulas AME: ciclo de nodos");vistos[p]=true;
        DefAme d;d.offset=p;d.tipo=U(b,p+4);d.flags=U(b,p+8);d.hijo=U(b,p+0x24);d.hermano=U(b,p+0x28);
        d.pos={spx::capsulas::F(b,p+0x30),spx::capsulas::F(b,p+0x34),spx::capsulas::F(b,p+0x38)};
        for(size_t j=0;j<4;++j)d.quat[j]=spx::capsulas::F(b,p+0x40+j*4);
        for(size_t j=0;j<48 && uint64_t(p)+0x54+4*j<=b.size();++j)d.f[j]=spx::capsulas::F(b,p+0x50+j*4);
        if(d.tipo!=0x10000 && d.tipo!=0x20000 && d.tipo!=0x20001 && d.tipo!=0x20003)
          throw std::runtime_error("Capsulas AME: clase sin consumidor propio");
        if(raiz && d.tipo!=0x10000)throw std::runtime_error("Capsulas AME: raiz no esfera");
        if(!raiz && d.tipo==0x10000)throw std::runtime_error("Capsulas AME: emisor anidado fuera del banco972");
        if(d.tipo==0x10000) {
          if(d.quat!=std::array<float,4>{0,0,0,1})throw std::runtime_error("Capsulas AME: giro de emisor no probado");
          if(d.f[9]<0)throw std::runtime_error("Capsulas AME: emisor sin tope no probado");
        } else {
          d.mezcla=int(U(b,p+0xa0)&3);d.tex=int32_t(U(b,p+0xa4));
          if(d.tipo==0x20003 && (d.flags&0x60))throw std::runtime_error("Capsulas AME: orientacion aleatoria no probada");
          if(d.flags&0x8000) {
            const auto n=U(b,p+0xc4);if(!n || uint64_t(p)+0xc8+20ull*n>b.size())throw std::runtime_error("Capsulas AME: atlas truncado");
            for(size_t j=0;j<n;++j) {
              std::array<float,5> a;for(size_t k=0;k<5;++k)a[k]=spx::capsulas::F(b,p+0xc8+20*j+4*k);
              d.anim.push_back(a);
            }
          }
        }
        mapa[p]=defs.size();defs.push_back(d);if(raiz)raices.push_back(p);
        if(d.hijo)leer(d.hijo,false);p=d.hermano;
      }
    };
    leer(U(b,12),true);
    if(defs.size()!=U(b,8))throw std::runtime_error("Capsulas AME: conteo de nodos distinto");
    std::map<size_t,size_t> registrado;
    std::function<size_t(size_t)> part=[&](size_t d) {
      NodoAme n;n.def=d;
      if(defs[d].hijo) {
        const auto h=mapa.at(defs[d].hijo);
        if(defs[h].tipo>>16!=2 || defs[h].hermano)throw std::runtime_error("Capsulas AME: cadena no lineal");
        n.hijo=int(part(h));
      }
      const auto i=nodos.size();nodos.push_back(std::move(n));registrado[d]=i;return i;
    };
    for(auto p:raices) {
      const auto d=mapa.at(p);const auto i=nodos.size();NodoAme n;n.def=d;n.edad=-defs[d].f[1];nodos.push_back(std::move(n));
      for(auto h=defs[d].hijo;h;h=defs[mapa.at(h)].hermano) {const auto id=part(mapa.at(h));nodos[i].hijos.push_back(id);}
      nodos[i].acumulado.assign(nodos[i].hijos.size(),0);
    }
  }
  static uint32_t Memory(uint32_t& s,int bits){while(bits-->0)s=(s<<1)|(((s>>31)^(s>>4))&1);return s-1;}
  static float Azar(uint32_t& s){return float(Memory(s,8)&255)/255.f;}
  PartAme Nacer(size_t id,Vec3 base,Vec3 velocidad,uint32_t& sem,const PartAme* padre=nullptr,const DefAme* dp=nullptr) {
    const auto& d=defs[nodos[id].def];PartAme p;p.edad=-d.f[1];p.flags=p.edad<0?1:0;p.p=Mas(base,d.pos);p.v=Por(velocidad,d.f[2]);p.quat=d.quat;
    p.escala=d.f[6];for(size_t j=0;j<4;++j)p.color[j]=d.f[12+j];
    const float dimension=d.f[3]+ProductoPS2(Azar(sem),d.f[4]);
    if(d.tipo==0x20000) {
      p.ancho_base=dimension;p.alto_base=ProductoPS2(dimension,d.f[5]+1);
      p.tamano=ProductoPS2(p.ancho_base,d.f[6]);p.escala=ProductoPS2(p.alto_base,d.f[6]);
    } else p.tamano=ProductoPS2(.5f,dimension);
    if(d.tipo==0x20001) {
      p.angulo=d.f[8]+ProductoPS2(Azar(sem),d.f[9]);if((d.flags&0x20)&&(Memory(sem,1)&1))p.flags|=4;
    }
    if(d.flags&0x8000) {
      if(d.flags&0x80000)p.cuadro=Memory(sem,32)%uint32_t(d.anim.size());
      for(size_t j=0;j<4;++j)p.uv[j]=d.anim[p.cuadro][j+1];
    } else if(d.flags&0x2000)for(size_t j=0;j<4;++j)p.uv[j]=d.f[22+j];
    if((d.flags&0x20000)&&(Memory(sem,1)&1)){p.flags|=8;std::swap(p.uv[0],p.uv[2]);}
    if((d.flags&0x40000)&&(Memory(sem,1)&1)){p.flags|=16;std::swap(p.uv[1],p.uv[3]);}
    // Init siempre consume azar.276D30 hereda los flips DESPUES.
    if(padre&&dp&&dp->tipo==d.tipo) {
      if(d.tipo==0x20001&&(dp->flags&d.flags&0x20))p.flags=(p.flags&~4u)|(padre->flags&4);
      for(auto [flag,raw,a,b]:{std::array<unsigned,4>{8,0x20000,0,2},{16,0x40000,1,3}})
        if((dp->flags&d.flags&raw)&&((p.flags^padre->flags)&flag)){p.flags^=flag;std::swap(p.uv[a],p.uv[b]);}
    }
    ++nacidas;return p;
  }
  void Cuadro(uint32_t& sem,float paso=1,float dt=1.f/60) {
    if(!std::isfinite(paso)||!std::isfinite(dt)||paso<=0||dt<0)throw std::runtime_error("Capsulas AME: paso temporal no valido");
    if(retirado)return;++cuadros;
    for(size_t i=0;i<nodos.size();++i) {
      auto& n=nodos[i];const auto& d=defs[n.def];if(n.retirado)continue;
      if(n.terminar&&n.particulas.empty()) {
        if(n.hijo>=0)nodos[size_t(n.hijo)].terminar=true;
        for(auto h:n.hijos)nodos[h].terminar=true;
        n.retirado=true;continue;
      }
      if(d.tipo==0x10000) {
        n.edad+=paso;
        if(n.edad<=0)continue;
        if(d.f[0]!=-1 && n.edad>=d.f[0]) {n.retirado=true;for(auto h:n.hijos)nodos[h].terminar=true;continue;}
        for(size_t j=0;j<n.hijos.size();++j) {
          auto& acum=n.acumulado[j];acum+=d.f[10]*paso;
          while(acum>=1) {
            acum-=1;const auto h=n.hijos[j];if(float(nodos[h].particulas.size())>=d.f[9])continue;
            Vec3 direccion{Azar(sem)-.5f,Azar(sem)-.5f,Azar(sem)-.5f};direccion=Unitario(direccion);
            const float r=d.f[3]+ProductoPS2(Azar(sem),d.f[4]),v=d.f[6]+ProductoPS2(Azar(sem),d.f[7]);
            nodos[h].particulas.push_back(Nacer(h,Mas(d.pos,Por(direccion,r)),Por(direccion,v),sem));
          }
        }
      } else {
        std::vector<PartAme> vivas;
        for(auto p:n.particulas) {
          p.edad+=paso;p.flags&=~1u;
          if(p.edad<=0){p.flags|=1;vivas.push_back(p);continue;}
          p.p=Mas(p.p,Por(p.v,dt));
          if(d.f[0]>=0 && p.edad>=d.f[0]) {
            if(n.hijo>=0) {
              const auto h=size_t(n.hijo);nodos[h].particulas.push_back(Nacer(h,p.p,p.v,sem,&p,&d));
            }
            continue;
          }
          const float t=d.f[0]<0?0:ProductoPS2(p.edad,1.f/d.f[0]),escala=ProductoPS2(d.f[6],1-t)+ProductoPS2(d.f[7],t);
          if(d.tipo==0x20000){p.tamano=ProductoPS2(p.ancho_base,escala);p.escala=ProductoPS2(p.alto_base,escala);}else p.escala=escala;
          if(d.flags&16)for(size_t j=0;j<4;++j)p.color[j]=d.f[12+j]+(d.f[16+j]-d.f[12+j])*t;
          if(d.tipo==0x20001&&(d.flags&8))p.angulo+=ProductoPS2(((p.flags&4)?-1.f:1.f)*d.f[11],dt);
          if(d.tipo==0x20003&&(d.flags&4)&&d.f[11]!=0)throw std::runtime_error("Capsulas AME: rotacion sprite no probada");
          if(d.flags&0x8000) {
            if(!(p.flags&2) && (p.anim_t+=paso)>=d.anim[p.cuadro][0]) {
              p.anim_t=0;++p.cuadro;
              if(p.cuadro==d.anim.size()){if(d.flags&0x10000)p.cuadro=0;else{--p.cuadro;p.flags|=2;}}
            }
            for(size_t j=0;j<4;++j)p.uv[j]=d.anim[p.cuadro][j+1];
            if(p.flags&8)std::swap(p.uv[0],p.uv[2]);if(p.flags&16)std::swap(p.uv[1],p.uv[3]);
          } else if(d.flags&0x4000) {
            const float du=ProductoPS2(d.f[26],dt)*((p.flags&8)?-1:1),dv=ProductoPS2(d.f[27],dt)*((p.flags&16)?-1:1);
            p.uv[0]+=du;p.uv[2]+=du;p.uv[1]+=dv;p.uv[3]+=dv;
          }
          vivas.push_back(p);
        }
        n.particulas=std::move(vivas);
      }
    }
  }
  int Nodos()const {if(retirado)return -1;int n=0;for(const auto& x:nodos)n+=!x.retirado;return n?n:-1;}
  void Retirar(){retirado=true;for(auto& n:nodos){n.particulas.clear();n.retirado=true;}}
};
// Adaptador D4/D5/D6. El anfitrion entrega el Contexto RNG compartido y el
// consumidor de dibujo; dibujar no avanza ni consume azar.
struct ObjetoAme final : spx::capsulas::ObjetoAme {
  Ame estado;
  std::function<void(Ame&)> avanzar;
  std::function<bool(const Ame&)> capturar;
  std::function<void(const std::string&)> informar;
  explicit ObjetoAme(const Bytes& b):estado(b){}
  bool Dibujar() override {return estado.Nodos()<0 || (capturar&&capturar(estado));}
  bool Regular() override {
    if(estado.Nodos()<0)return true;
    if(!avanzar){if(informar)informar("AME972: falta Contexto RNG compartido");return false;}
    try{avanzar(estado);return true;}catch(const std::exception& e){if(informar)informar(e.what());return false;}
  }
  bool Retirar() override {estado.Retirar();return true;}
  int Nodos()const override {return estado.Nodos();}
};
struct VU {
  std::array<Qword,32> vf{};
  std::array<uint16_t,16> vi{};
  std::array<Qword,1024> mem{};
  std::vector<std::array<uint32_t,2>> code;
  std::array<float,4> acc{};
  float q=1,immediate=0;
  uint32_t clip=0,mac=0,status=0,pc=0,top=0,itop=0;
  size_t ejecutadas=0;
  std::vector<Bytes> gif;
  VU(){vf[0]={0,0,0,B(1)};}
  static bool Componente(unsigned mask,unsigned c){return (mask&(8u>>c))!=0;}
  static int32_t Entero(float x,unsigned escala) {
    const double n=double(x)*double(uint32_t(1)<<escala);
    if(!std::isfinite(n))throw std::runtime_error("VU: conversion no finita fuera del perfil probado");
    if(n>=2147483647.)return 2147483647;if(n<=-2147483648.)return std::numeric_limits<int32_t>::min();
    return int32_t(n);
  }
  void Kick(unsigned addr) {
    Bytes out;bool fin=false;
    for(unsigned tags=0;tags<1024 && !fin;++tags) {
      const auto tag=mem[addr++&1023];const unsigned loops=tag[0]&32767,flg=(tag[1]>>26)&3,nr=(tag[1]>>28)?tag[1]>>28:16;
      fin=(tag[0]&0x8000)!=0;
      auto poner=[&](const Qword& w){for(auto x:w)for(unsigned j=0;j<4;++j)out.push_back(uint8_t(x>>(8*j)));};
      poner(tag);const unsigned n=flg==0?loops*nr:flg==1?(loops*nr+1)/2:loops;
      if(n>1024)throw std::runtime_error("VU: GIF fuera de memoria local");
      for(unsigned j=0;j<n;++j)poner(mem[addr++&1023]);
    }
    if(!fin)throw std::runtime_error("VU: GIF sin EOP");gif.push_back(std::move(out));
  }
  void Ejecutar(uint32_t entrada,size_t presupuesto=2000000) {
    pc=entrada;std::optional<uint32_t> salto;int terminar=-1;
    for(size_t pasos=0;pasos<presupuesto;++pasos) {
      if(pc>=code.size())throw std::runtime_error("VU: PC fuera del microprograma");
      const auto [lo,hi]=code[pc];const auto viejo_salto=salto;salto.reset();
      const unsigned mask=(hi>>21)&15,ft=(hi>>16)&31,fs=(hi>>11)&31,fd=(hi>>6)&31,fn=hi&63;
      Qword resultado{};unsigned destino=fd;bool escribir=false;uint32_t nuevo_mac=0;
      auto flags=[&](float x,unsigned c){if(x==0)nuevo_mac|=8u>>c;if(std::signbit(x))nuevo_mac|=0x80u>>c;};
      auto bin=[&](unsigned tipo,std::optional<unsigned> bc,bool acumulador=false,std::optional<float> escalar={}) {
        for(unsigned c=0;c<4;++c)if(Componente(mask,c)) {
          const float x=F(vf[fs][c]),y=escalar?*escalar:F(vf[ft][bc?*bc:c]);float z=0;
          switch(tipo){case 0:z=x+y;break;case 1:z=x-y;break;case 2:z=acc[c]+x*y;break;case 3:z=acc[c]-x*y;break;case 4:z=std::max(x,y);break;case 5:z=std::min(x,y);break;case 6:z=x*y;break;default:throw std::runtime_error("VU: ALU");}
          if(!std::isfinite(z))throw std::runtime_error("VU: excepcion FMAC fuera del perfil finito");
          flags(z,c);if(acumulador)acc[c]=z;else resultado[c]=B(z);
        }
        escribir=!acumulador;mac=nuevo_mac;status=(status&~15u)|(mac?1u:0u);
      };
      if(fn<0x1c)bin(fn>>2,fn&3);
      else if(fn>=0x3c) {
        const unsigned ext=(fd<<2)|(fn&3);
        if(ext<16)bin(ext>>2,ext&3,true);
        else if(ext>=0x18 && ext<=0x1b)bin(6,ext&3,true);
        else if(ext>=0x10 && ext<=0x17) {
          destino=ft;escribir=true;const unsigned shift=std::array<unsigned,4>{0,4,12,15}[ext&3];
          for(unsigned c=0;c<4;++c)if(Componente(mask,c))resultado[c]=ext<0x14?B(float(int32_t(vf[fs][c]))/float(1u<<shift)):uint32_t(Entero(F(vf[fs][c]),shift));
        } else if(ext==0x1c)bin(6,{},true,q);
        else if(ext==0x1d){destino=ft;escribir=true;for(unsigned c=0;c<4;++c)resultado[c]=vf[fs][c]&0x7fffffff;}
        else if(ext==0x1e)bin(6,{},true,immediate);
        else if(ext==0x1f) {
          const float w=std::abs(F(vf[ft][3]));uint32_t bits=0;
          for(unsigned c=0;c<3;++c){const float x=F(vf[fs][c]);if(x>w)bits|=1u<<(2*c);if(x<-w)bits|=2u<<(2*c);}
          clip=((clip<<6)|bits)&0xffffff;
        } else if(ext==0x2f){}
        else if(ext==0x2e){for(unsigned c=0;c<3;++c)acc[c]=F(vf[fs][(c+1)%3])*F(vf[ft][(c+2)%3]);}
        else if(ext==0x20||ext==0x21||ext==0x22||ext==0x23||ext==0x24||ext==0x25||ext==0x26||ext==0x27)
          bin((ext&4)?((ext&1)?3:1):((ext&1)?2:0),{},true,(ext&2)?immediate:q);
        else if(ext==0x28||ext==0x29||ext==0x2a||ext==0x2c||ext==0x2d)bin(ext==0x2a?6:ext==0x28?0:ext==0x29?2:ext==0x2c?1:3,{},true);
        else throw std::runtime_error("VU: upper especial="+std::to_string(ext)+" pc="+std::to_string(pc));
      } else if(fn>=0x28 && fn<=0x2f) {
        if(fn==0x2e) {escribir=true;for(unsigned c=0;c<3;++c)resultado[c]=B(acc[c]-F(vf[fs][(c+2)%3])*F(vf[ft][(c+1)%3]));}
        else bin(fn==0x28?0:fn==0x29?2:fn==0x2a?6:fn==0x2b?4:fn==0x2c?1:fn==0x2d?3:5,{});
      } else if(fn==0x1c||fn==0x1e)bin(6,{},false,fn==0x1c?q:immediate);
      else if(fn==0x1d||fn==0x1f)bin(fn==0x1d?4:5,{},false,immediate);
      else if(fn>=0x20 && fn<=0x27)bin((fn&4)?((fn&1)?3:1):((fn&1)?2:0),{},false,(fn&2)?immediate:q);
      else throw std::runtime_error("VU: upper="+std::to_string(fn));

      if(hi&0x80000000)immediate=F(lo);
      else {
        const unsigned op=lo>>25,it=(lo>>16)&31,is=(lo>>11)&31,id=(lo>>6)&31,func=lo&63,lmask=(lo>>21)&15;
        const unsigned ti=it&15,si=is&15,di=id&15;const int off=Signo(lo&2047,11);
        auto addr=[&](){return unsigned(int(vi[si])+off)&1023;};
        auto load=[&](unsigned a){for(unsigned c=0;c<4;++c)if(Componente(lmask,c))vf[it][c]=mem[a&1023][c];};
        auto store=[&](unsigned a){for(unsigned c=0;c<4;++c)if(Componente(lmask,c))mem[a&1023][c]=vf[is][c];};
        auto iload=[&](unsigned a){for(unsigned c=0;c<4;++c)if(Componente(lmask,c))vi[ti]=uint16_t(mem[a&1023][c]);};
        auto istore=[&](unsigned a){for(unsigned c=0;c<4;++c)if(Componente(lmask,c))mem[a&1023][c]=vi[ti];};
        auto branch=[&](bool b){if(b)salto=uint32_t(int(pc)+1+off);};
        if(op==0)load(addr());
        else if(op==1)store((unsigned(int(vi[ti])+off))&1023);
        else if(op==4)iload(addr());
        else if(op==5)istore(addr());
        else if(op==8||op==9){const unsigned imm=(lo&2047)|((lo>>10)&0x7800);vi[ti]=uint16_t(op==8?vi[si]+imm:vi[si]-imm);}
        else if(op==0x20)branch(true);
        else if(op==0x21){vi[ti]=uint16_t(pc+2);branch(true);}
        else if(op==0x24)salto=vi[si];
        else if(op==0x25){salto=vi[si];vi[ti]=uint16_t(pc+2);}
        else if(op==0x28||op==0x29)branch((vi[si]==vi[ti])==(op==0x28));
        else if(op>=0x2c&&op<=0x2f){const auto x=int16_t(vi[si]);branch(op==0x2c?x<0:op==0x2d?x>0:op==0x2e?x<=0:x>=0);}
        else if(op==0x10)vi[1]=clip==(lo&0xffffff);
        else if(op==0x11)clip=lo&0xffffff;
        else if(op==0x12)vi[1]=(clip&(lo&0xffffff))!=0;
        else if(op==0x13)vi[1]=(clip|(lo&0xffffff))==0xffffff;
        else if(op==0x18||op==0x1a||op==0x1b)vi[ti]=uint16_t(op==0x18?mac==vi[si]:op==0x1a?mac&vi[si]:mac|vi[si]);
        else if(op==0x14||op==0x16||op==0x17)vi[ti]=uint16_t(op==0x14?status==(lo&4095):op==0x16?status&(lo&4095):status|(lo&4095));
        else if(op==0x15)status=lo&4095;
        else if(op==0x40) {
          if(func==0x30)vi[di]=uint16_t(vi[si]+vi[ti]);
          else if(func==0x31)vi[di]=uint16_t(vi[si]-vi[ti]);
          else if(func==0x32)vi[ti]=uint16_t(vi[si]+Signo(id,5));
          else if(func==0x34)vi[di]=vi[si]&vi[ti];
          else if(func==0x35)vi[di]=vi[si]|vi[ti];
          else {
            const auto ext=(id<<2)|(func&3);
            if(ext==0x30||ext==0x31){const auto src=vf[is];for(unsigned c=0;c<4;++c)if(Componente(lmask,c))vf[it][c]=src[(c+(ext==0x31?1:0))&3];}
            else if(ext==0x34){load(vi[si]);++vi[si];}
            else if(ext==0x35){store(vi[ti]);++vi[ti];}
            else if(ext==0x36){--vi[si];load(vi[si]);}
            else if(ext==0x37){--vi[ti];store(vi[ti]);}
            else if(ext==0x38||ext==0x39||ext==0x3a){
              const float a=F(vf[is][(lo>>21)&3]),b=F(vf[it][(lo>>23)&3]);
              status&=~0x30u;
              if(ext!=0x39 && b==0) {
                // DIV/RSQRT no lanzan excepcion en VU. El ultimo delay slot
                // puede dividir el vertice de precarga que ya no se dibuja.
                // Se conserva el patron VU (no es un NaN IEEE consumible por
                // nuestra ALU finita); si se usa, la ALU lo rechaza arriba.
                const uint32_t flag=a==0?0x10u:0x20u;status|=flag|(flag<<6);
                q=F(((B(a)^B(b))&0x80000000u)|0x7fffffffu);
              } else {
                if(ext!=0x38 && b<0)status|=0x410u;
                q=ext==0x38?a/b:ext==0x39?std::sqrt(std::abs(b)):a/std::sqrt(std::abs(b));
              }
            } else if(ext==0x3b){}
            else if(ext==0x3c)vi[ti]=uint16_t(vf[is][(lo>>21)&3]);
            else if(ext==0x3d){for(unsigned c=0;c<4;++c)if(Componente(lmask,c))vf[it][c]=uint32_t(int32_t(int16_t(vi[si])));}
            else if(ext==0x3e)iload(vi[si]);
            else if(ext==0x3f)istore(vi[si]);
            else if(ext==0x68)vi[ti]=uint16_t(top);
            else if(ext==0x69)vi[ti]=uint16_t(itop);
            else if(ext==0x6c)Kick(vi[si]);
            else throw std::runtime_error("VU: lower especial="+std::to_string(ext)+" pc="+std::to_string(pc));
          }
        } else throw std::runtime_error("VU: lower="+std::to_string(op)+" pc="+std::to_string(pc));
      }
      if(escribir&&destino)for(unsigned c=0;c<4;++c)if(Componente(mask,c))vf[destino][c]=resultado[c];
      vi[0]=0;vf[0]={0,0,0,B(1)};++ejecutadas;
      if(hi&0x40000000)terminar=1;
      else if(terminar==1)terminar=0;
      pc=viejo_salto?*viejo_salto:pc+1;
      if(terminar==0)return;
    }
    throw std::runtime_error("VU: microprograma no termina");
  }
};
struct Programas {
  struct Eleccion {bool dibujar=true,contexto_unico=false;uint32_t registro=0,callback=0;int32_t clave=0;};
  Bytes elf;
  std::map<unsigned,std::vector<std::array<uint32_t,2>>> cache;
  explicit Programas(Bytes b):elf(std::move(b)){}
  uint32_t Leer(uint32_t va)const {
    // SLES/SLUS son ELF32: resolver segmentos, no asumir offset80 para otros.
    using namespace spx::capsulas;
    const auto ph=U(elf,28);const auto stride=uint16_t(U(elf,40)>>16),n=uint16_t(U(elf,44));
    for(size_t i=0;i<n;++i){const auto p=ph+i*stride;if(U(elf,p)==1 && va>=U(elf,p+8) && uint64_t(va)+4<=uint64_t(U(elf,p+8))+U(elf,p+16))return U(elf,U(elf,p+4)+va-U(elf,p+8));}
    throw std::runtime_error("Capsulas: VA del ELF ausente="+std::to_string(va));
  }
  const std::vector<std::array<uint32_t,2>>& Cargar(unsigned family,unsigned index) {
    if(family!=1&&family!=3&&family!=5&&family!=7)throw std::runtime_error("Capsulas: familia VU desconocida");
    return CargarRegistro(Leer(0x40fad0+4*family)+16*index);
  }
  const std::vector<std::array<uint32_t,2>>& CargarRegistro(uint32_t registro) {
    const unsigned key=registro;auto found=cache.find(key);if(found!=cache.end())return found->second;
    uint32_t pc=Leer(registro+4)&0x0fffffff;
    if(!pc)throw std::runtime_error("Capsulas: shader VU no existe para formato");
    std::vector<uint32_t> stream,stack;
    for(unsigned count=0;count<1024;++count) {
      const uint32_t a=Leer(pc),addr=Leer(pc+4)&0x0fffffff,n=a&65535,op=(a>>28)&7;
      stream.push_back(Leer(pc+8));stream.push_back(Leer(pc+12));
      const uint32_t data=(op==0||op==3||op==4)?addr:pc+16;
      for(unsigned j=0;j<n*4;++j)stream.push_back(Leer(data+4*j));
      const uint32_t after=pc+16+((op==1||op==2||op==5||op==6||op==7)?n*16:0);
      if(op==0||op==7)break;
      if(op==5){stack.push_back(after);pc=addr;}else if(op==6){if(stack.empty())break;pc=stack.back();stack.pop_back();}else pc=op==2?addr:after;
      if(count==1023)throw std::runtime_error("Capsulas: cadena MPG no termina");
    }
    std::vector<std::array<uint32_t,2>> code;
    for(size_t p=0;p<stream.size();) {
      const auto w=stream[p++];const unsigned cmd=(w>>24)&127,n=(w>>16)&255;
      if(cmd==0x4a) {
        const auto first=w&65535,count=n?n:256;if(p+count*2>stream.size())throw std::runtime_error("Capsulas: MPG truncado");
        code.resize(std::max(code.size(),size_t(first+count)));
        for(size_t j=0;j<count;++j)code[first+j]={stream[p+2*j],stream[p+2*j+1]};p+=count*2;
      } else if(cmd==0x20)p+=1;else if(cmd==0x30||cmd==0x31)p+=4;
      else if(cmd==0x50||cmd==0x51)p+=(w&65535)*4;
      else if(cmd>0x17)throw std::runtime_error("Capsulas: VIF de carga desconocido");
    }
    if(code.empty())throw std::runtime_error("Capsulas: microprograma vacio");return cache.emplace(key,std::move(code)).first->second;
  }
  static unsigned Indice(uint32_t flags) {
    if((flags&0x2000)||(flags&9)!=9)flags&=~0x4000u;
    return (flags&7)+((flags&0x4000)?0x60:0)+((flags&0x80)?0x30:0)+((flags&0x40)?0x10:(flags&0x2000)?0x20:0)+((flags&8)?8:0);
  }
  Eleccion Elegir(uint32_t flags,unsigned modo_luz)const {
    Eleccion out;
    if(flags&0x3c00000) {
      // Selector1B1130 original. Solo CPU escalar y su tabla del ELF: no se
      // adivina que shader corresponde al pase negro/expansion.
      std::array<uint32_t,32> r{};r[4]=0x900000;r[5]=7;r[6]=flags;r[28]=0x4a3df0;r[31]=0xfffffffc;
      std::map<uint32_t,uint32_t> ram{{0x900000,0},{0x49c66c,modo_luz},{0x49c6b0,0},{0x4ab674,0}};
      auto load=[&](uint32_t a){auto i=ram.find(a);return i==ram.end()?Leer(a):i->second;};
      uint32_t pc=0x1b1130;std::optional<uint32_t> salto;
      for(unsigned n=0;;++n) {
        if(pc==0xfffffffc)break;
        if(n>=1024 || pc<0x1b1130 || pc>=0x1b1800 || (pc&3))throw std::runtime_error("Capsulas: selector fuera de su perfil escalar");
        const auto w=Leer(pc),op=w>>26,s=(w>>21)&31,t=(w>>16)&31,d=(w>>11)&31,sh=(w>>6)&31,fn=w&63;
        const auto previo=salto;salto.reset();uint32_t siguiente=pc+4;const int32_t imm=int16_t(w&65535);
        auto branch=[&](bool cond,bool likely){if(cond)salto=pc+4+uint32_t(imm*4);else if(likely)siguiente=pc+8;};
        if(op==0) {
          if(fn==0)r[d]=r[t]<<sh;else if(fn==2)r[d]=r[t]>>sh;else if(fn==3)r[d]=uint32_t(int32_t(r[t])>>sh);
          else if(fn==8)salto=r[s];else if(fn==0x21||fn==0x2d)r[d]=r[s]+r[t];else if(fn==0x23)r[d]=r[s]-r[t];
          else if(fn==0x24)r[d]=r[s]&r[t];else if(fn==0x25)r[d]=r[s]|r[t];else if(fn==0x26)r[d]=r[s]^r[t];
          else if(fn==0x2a)r[d]=int32_t(r[s])<int32_t(r[t]);else if(fn==0x2b)r[d]=r[s]<r[t];
          else throw std::runtime_error("Capsulas: selector SPECIAL sin soporte fn="+std::to_string(fn)+" pc="+std::to_string(pc));
        } else if(op==2)salto=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2);
        else if(op==4||op==0x14)branch(r[s]==r[t],op==0x14);
        else if(op==5||op==0x15)branch(r[s]!=r[t],op==0x15);
        else if(op==6||op==0x16)branch(int32_t(r[s])<=0,op==0x16);
        else if(op==7||op==0x17)branch(int32_t(r[s])>0,op==0x17);
        else if(op==9)r[t]=r[s]+uint32_t(imm);
        else if(op==0xb)r[t]=r[s]<uint32_t(imm);
        else if(op==0xc)r[t]=r[s]&(w&65535);
        else if(op==0xd)r[t]=r[s]|(w&65535);
        else if(op==0xf)r[t]=(w&65535)<<16;
        else if(op==0x23)r[t]=load(r[s]+uint32_t(imm));
        else if(op==0x2b) {
          const uint32_t a=r[s]+uint32_t(imm);
          if(a!=0x900000&&a!=0x49c6b0&&a!=0x4ab674)throw std::runtime_error("Capsulas: selector intenta escritura fuera de sus resultados");
          ram[a]=r[t];
        } else throw std::runtime_error("Capsulas: selector opcode sin soporte");
        r[0]=0;pc=previo?*previo:siguiente;
      }
      out.registro=ram.at(0x900000);out.callback=ram.at(0x4ab674);out.clave=int32_t(r[2]);
      out.contexto_unico=ram.at(0x49c6b0)!=0;
      if(out.registro)return out;
      if(out.clave==-1){out.dibujar=false;return out;}
    }
    const auto i=Indice(flags);out.registro=Leer(0x40fad0+7*4)+16*i;out.clave=int32_t(0x700+i);
    return out;
  }
  void AplicarCallback(uint32_t callback,Bytes& bloque,const std::function<uint32_t(uint32_t)>& leer)const {
    if(!callback)return;
    if(bloque.size()<0x140 || !leer)throw std::runtime_error("Capsulas: callback sin bloque/globales originales");
    auto poner=[&](size_t p,uint32_t v){for(unsigned k=0;k<4;++k)bloque[p+k]=uint8_t(v>>(8*k));};
    if(callback==0x1b09d0) {
      for(unsigned k=0;k<16;++k)poner(0x88+4*k,leer(0x4c5120+4*k));
      poner(0x110,5);poner(0x114,0);
    } else if(callback==0x1b0a40)poner(0x24,leer(0x49be7c)); // expansion real, no desplazar la malla a ojo.
    else if(callback>=0x1b0a50 && callback<=0x1b0ba0 && (callback-0x1b0a50)%0x70==0) {
      const auto i=(callback-0x1b0a50)/0x70;
      for(unsigned k=0;k<4;++k)poner(0x48+4*k,B(TruncarVU(double(spx::capsulas::F(bloque,0x48+4*k))*F(leer(0x4c4ff0-16*i+4*k)))));
      poner(0x88,leer(0x4c4e54-4*i));poner(0x8c,leer(0x4c4e94-4*i));
    } else if(callback>=0x1b0c10 && callback<=0x1b0c70 && (callback-0x1b0c10)%0x20==0) {
      const auto i=(callback-0x1b0c10)/0x20;
      poner(0x88,leer(0x4c4e44-4*i));poner(0x8c,leer(0x4c4e84-4*i));
    } else if(callback==0x1b0c90)poner(0x24,leer(0x49c9b0));
    else if(callback==0x1b0ca0){poner(0xf0,0x54000);poner(0x24,leer(0x49c9b0));}
    else if(callback==0x1b0cc0)poner(0xf0,0x50000);
    else throw std::runtime_error("Capsulas: callback VU fuera del formato portado="+std::to_string(callback));
  }
};
struct Imagen {int w=0,h=0;Bytes rgba;uint32_t flags=0;};
struct Material {
  uint32_t flags=0,flags2=0;int tex=-1,rampa=-1;
  std::array<float,4> ka{},kd{};std::array<uint64_t,2> alpha{},clamp{};
};
inline std::vector<Material> Materiales(const Bytes& b) {
  using namespace spx::capsulas;
  std::vector<Material> out;const size_t a=U(b,U(b,28)),axes=a+U(b,a+20);
  auto q=[&](size_t p){return uint64_t(U(b,p))|uint64_t(U(b,p+4))<<32;};
  for(size_t i=0;i<U(b,a+16);++i) {
    const auto arm=U(b,axes+80*i+0x34);if(!arm)continue;const auto group=U(b,a+arm+4);if(!group)continue;
    const size_t g=a+group,table=g+U(b,g+4);
    for(size_t k=0;k<U(b,g);++k) {
      const size_t p=g+U(b,table+4*k);const auto flags=U(b,p),tam=U(b,p+0x90);
      if((flags&65535)==65535 || tam<0x60000000 || tam>=0x70000000)continue;
      Material m;m.flags=flags;m.flags2=U(b,p+4);m.tex=int32_t(U(b,p+8));m.rampa=int32_t(U(b,p+12));
      for(size_t j=0;j<4;++j){m.ka[j]=spx::capsulas::F(b,p+0x20+4*j);m.kd[j]=spx::capsulas::F(b,p+0x30+4*j);}
      m.alpha={q(p+0x60),q(p+0x80)};m.clamp={q(p+0x50),q(p+0x70)};out.push_back(m);
    }
  }
  return out;
}
struct Vertice {float x=0,y=0,z=0,s=0,t=0,q=1;std::array<float,4> color{128,128,128,128};float fog=255;bool adc=false;};
struct GsEstado {uint64_t alpha=0x44,clamp=0,test=0x5000d;bool textura=true,blend=false,fog=false;unsigned tfx=0;std::array<uint8_t,3> fogcolor{};};
// Rasterizador del GS propio de capsulas: z/alpha-test y ecuacion ALPHA en
// enteros128, textura STQ perspectiva, CLAMP/REPEAT, sin pintor ImGui 3D.
struct Lienzo {
  unsigned w=640,h=448;Bytes rgba;std::vector<uint32_t> depth;size_t triangulos=0,pixeles=0;
  Lienzo(){Limpiar();}
  void Limpiar(){rgba.assign(size_t(w)*h*4,0);for(size_t p=3;p<rgba.size();p+=4)rgba[p]=255;depth.assign(size_t(w)*h,0);triangulos=pixeles=0;}
  static int Coord(int x,int tam,unsigned modo,unsigned minimo,unsigned maximo) {
    if(!tam)return 0;
    if(modo==0)return (x%tam+tam)%tam;
    if(modo==1)return std::clamp(x,0,tam-1);
    if(modo==2)return std::clamp(x,int(minimo),int(maximo));
    return (x&int(minimo))|int(maximo);
  }
  static std::array<int,4> Texel(const Imagen& im,float u,float v,uint64_t clamp) {
    if(!im.w||!im.h||im.rgba.size()<size_t(im.w)*im.h*4)throw std::runtime_error("GS: AMT no consumible");
    const float fx=u*im.w-.5f,fy=v*im.h-.5f;const int ix=int(std::floor(fx)),iy=int(std::floor(fy));
    const float ax=fx-ix,ay=fy-iy;std::array<int,4> out{};
    for(unsigned c=0;c<4;++c) {
      float suma=0;
      for(unsigned yy=0;yy<2;++yy)for(unsigned xx=0;xx<2;++xx) {
        const int x=Coord(ix+int(xx),im.w,unsigned(clamp&3),unsigned((clamp>>4)&1023),unsigned((clamp>>14)&1023));
        const int y=Coord(iy+int(yy),im.h,unsigned((clamp>>2)&3),unsigned((clamp>>24)&1023),unsigned((clamp>>34)&1023));
        if(x<0||x>=im.w||y<0||y>=im.h)throw std::runtime_error("GS: CLAMP fuera de AMT");
        suma+=im.rgba[(size_t(y)*im.w+x)*4+c]*(xx?ax:1-ax)*(yy?ay:1-ay);
      }
      out[c]=std::clamp(int(suma+.5f),0,255);
    }
    out[3]=(out[3]*128+127)/255;return out;
  }
  void Pixel(unsigned x,unsigned y,uint32_t z,std::array<int,4> c,const GsEstado& s) {
    const size_t p=size_t(y)*w+x;auto* dst=&rgba[p*4];
    bool alpha=true;
    if(s.test&1) {
      const auto mode=(s.test>>1)&7,ref=(s.test>>4)&255;const auto a=unsigned(c[3]);
      alpha=mode==0?false:mode==1?true:mode==2?a<ref:mode==3?a<=ref:mode==4?a==ref:mode==5?a>=ref:mode==6?a>ref:a!=ref;
    }
    const auto fail=(s.test>>12)&3;
    if(!alpha && !fail)return;
    if((s.test&(1u<<14)) && (((dst[3]&128)!=0)!=bool(s.test&(1u<<15))))return;
    if(s.test&(1u<<16)) {
      const auto mode=(s.test>>17)&3;if(mode==0 || (mode==2&&z<depth[p]) || (mode==3&&z<=depth[p]))return;
    }
    if(alpha || fail==2)depth[p]=z;
    if(!alpha && fail==2)return;
    if(s.blend) {
      const unsigned a=s.alpha&3,b=(s.alpha>>2)&3,cc=(s.alpha>>4)&3,d=(s.alpha>>6)&3;
      for(unsigned j=0;j<3;++j) {
        auto color=[&](unsigned k){return k==0?c[j]:k==1?int(dst[j]):0;};
        const int v=cc==0?c[3]:cc==1?(int(dst[3])*128+127)/255:int((s.alpha>>32)&255);
        c[j]=std::clamp(((color(a)-color(b))*v>>7)+color(d),0,255);
      }
    }
    for(unsigned j=0;j<3;++j)dst[j]=uint8_t(std::clamp(c[j],0,255));
    if(alpha || fail!=3)dst[3]=uint8_t(std::clamp(c[3]*255/128,0,255));++pixeles;
  }
  void Tri(Vertice a,Vertice b,Vertice c,const Imagen* imagen,const GsEstado& s) {
    auto edge=[](const Vertice& p,const Vertice& q,float x,float y){return (q.x-p.x)*(y-p.y)-(q.y-p.y)*(x-p.x);};
    float area=edge(a,b,c.x,c.y);if(area==0)return;if(area<0){std::swap(b,c);area=-area;}
    const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y}))));
    const int x1=std::min(int(w)-1,int(std::ceil(std::max({a.x,b.x,c.x})))),y1=std::min(int(h)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
    auto top=[](const Vertice& p,const Vertice& q){return q.y<p.y || (q.y==p.y&&q.x>p.x);};
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
      const float e0=edge(b,c,x+.5f,y+.5f),e1=edge(c,a,x+.5f,y+.5f),e2=edge(a,b,x+.5f,y+.5f);
      if(e0<0||e1<0||e2<0 ||(e0==0&&!top(b,c))||(e1==0&&!top(c,a))||(e2==0&&!top(a,b)))continue;
      const float u=e0/area,v=e1/area,t=e2/area,q=a.q*u+b.q*v+c.q*t;
      std::array<int,4> col;for(unsigned j=0;j<4;++j)col[j]=std::clamp(int(a.color[j]*u+b.color[j]*v+c.color[j]*t),0,255);
      if(s.textura) {
        if(!imagen||q==0)throw std::runtime_error("GS: texture/STQ sin consumidor");
        const auto tex=Texel(*imagen,(a.s*u+b.s*v+c.s*t)/q,(a.t*u+b.t*v+c.t*t)/q,s.clamp);
        const int alpha=col[3];
        for(unsigned j=0;j<3;++j)col[j]=s.tfx==1?tex[j]:std::clamp((tex[j]*col[j]>>7)+(s.tfx>=2?alpha:0),0,255);
        col[3]=s.tfx==0?tex[3]*alpha/128:s.tfx==2?std::min(255,tex[3]+alpha):tex[3];
      }
      if(s.fog) {const int f=std::clamp(int(a.fog*u+b.fog*v+c.fog*t),0,255);for(unsigned j=0;j<3;++j)col[j]=(col[j]*f+s.fogcolor[j]*(255-f))/255;}
      const float z=a.z*u+b.z*v+c.z*t;Pixel(unsigned(x),unsigned(y),uint32_t(std::clamp(z,0.f,16777215.f)),col,s);
    }
    ++triangulos;
  }
  void Gif(const Bytes& bytes,const Imagen* imagen,GsEstado estado) {
    using namespace spx::capsulas;size_t p=0;std::vector<Vertice> vertices;Vertice v;unsigned prim=3,n=0;
    while(p+16<=bytes.size()) {
      const uint32_t lo=U(bytes,p),hi=U(bytes,p+4);const uint64_t regs=uint64_t(U(bytes,p+8))|uint64_t(U(bytes,p+12))<<32;
      const unsigned loops=lo&32767,format=(hi>>26)&3,nregs=(hi>>28)?hi>>28:16;
      if(format!=0)throw std::runtime_error("GS: GIF no packed sin implementar");
      if(hi&(1u<<14)){prim=(hi>>15)&2047;estado.textura=(prim&16)!=0;estado.blend=(prim&64)!=0;estado.fog=(prim&32)!=0;}
      p+=16;
      for(unsigned i=0;i<loops*nregs;++i) {
        const auto reg=unsigned((regs>>(4*(i%nregs)))&15);if(p+16>bytes.size())throw std::runtime_error("GS: GIF truncado");
        if(reg==1){for(unsigned j=0;j<4;++j)v.color[j]=float(U(bytes,p+4*j)&255);}
        else if(reg==2){v.s=spx::capsulas::F(bytes,p);v.t=spx::capsulas::F(bytes,p+4);v.q=spx::capsulas::F(bytes,p+8);}
        else if(reg==4||reg==5||reg==12||reg==13) {
          v.x=float(U(bytes,p)&65535)/16-(2048-float(w)/2);v.y=float(U(bytes,p+4)&65535)/16-(2048-float(h)/2);
          v.z=float(reg==4||reg==12?(U(bytes,p+8)>>4)&0xffffff:U(bytes,p+8));v.fog=float((U(bytes,p+12)>>4)&255);v.adc=(U(bytes,p+12)&0x8000)!=0;
          vertices.push_back(v);++n;const auto type=prim&7;
          if(!v.adc && (type==3?n%3==0:type==4||type==5?n>=3:false)) {
            if(type==3)Tri(vertices[n-3],vertices[n-2],vertices[n-1],imagen,estado);
            else if(type==4)Tri(vertices[n-3],vertices[n-2],vertices[n-1],imagen,estado);
            else Tri(vertices[0],vertices[n-2],vertices[n-1],imagen,estado);
          }
        } else if(reg!=15)throw std::runtime_error("GS: registro GIF sin implementar="+std::to_string(reg));
        p+=16;
      }
    }
  }
};
} // namespace dbz3::iw::capsrender
