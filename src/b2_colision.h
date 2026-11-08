// Resolución MAS -> MAD -> entrada SPX de FUN_001B3BF0.
// El punto de contacto lo aporta la colisión REAL del motor; no consulta ringout.
#pragma once
#include "escenarios_b2_recursos.h"
#include <functional>
#include <cmath>

namespace dbz3::escenarios::b2 {
inline int EntradaDeSuperficie(const Recurso& r, uint32_t grupo) {
  if (r.version < 2) return -1;
  const Vista mad = r.Bloque(5), mas = r.Bloque(6);
  if (!mad.Rango(0, 16) || !mas.Rango(0, 16) || grupo >= mas.U(4)) return -1;
  const uint32_t tabla = mas.U(8);
  if (!mas.Rango(size_t(tabla) + 4ull * grupo, 4)) return -1;
  const uint32_t g = mas.U(size_t(tabla) + 4ull * grupo);
  if (!mas.Rango(g, 12)) return -1;
  const uint32_t n = mas.U(g + 4), lista = mas.U(g + 8);
  if (n > 4096 || !mas.Rango(lista, size_t(n) * 16)) return -1;
  int entrada = -1;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t q = lista + 16 * i, tipo = mas.U(q), propiedad = mas.U(q + 4);
    if (tipo != 1 || tipo >= mad.U(4)) continue;
    const uint32_t mt = mad.U(8);
    if (!mad.Rango(size_t(mt) + 4ull * tipo, 4)) return -1;
    const uint32_t mg = mad.U(size_t(mt) + 4ull * tipo);
    if (!mad.Rango(mg, 16) || propiedad >= mad.U(mg + 8)) return -1;
    const uint32_t pl = mad.U(mg + 12);
    if (!mad.Rango(size_t(pl) + 4ull * propiedad, 4)) return -1;
    const uint32_t p = mad.U(size_t(pl) + 4ull * propiedad);
    if (!mad.Rango(p, 8)) return -1;
    const uint32_t flags = mad.U(p), valor = mad.U(p + 4);
    if ((flags & 1u) && valor > 0 && valor <= 4096) entrada = int(valor - 1);
  }
  const Vista spx = r.Bloque(2);
  if (!spx.Rango(0, 28) || entrada < 0 || uint32_t(entrada) >= spx.U(24)) return -1;
  return entrada;
}

struct Impacto {
  bool hay=false;
  int entrada=-1;
  uint32_t superficie=~0u, registro=0;
  float t=2;
  std::array<float,3> punto{},normal{};
};
inline Impacto Rayo(const Recurso& r,uint32_t variante,const std::array<float,3>& a,
                    const std::array<float,3>& z,const std::function<bool(uint32_t)>& seleccionada={}) {
  Impacto mejor;
  const Vista m=r.Bloque(3),mad=r.Bloque(5),mas=r.Bloque(6);
  if(!m.Rango(0,40)||variante>=m.U(4))return mejor;
  auto fv=[&](size_t p){const uint32_t u=m.U(p);float f;std::memcpy(&f,&u,4);return f;};
  auto vec=[&](size_t p){return std::array<float,3>{fv(p),fv(p+4),fv(p+8)};};
  auto sub=[](auto a,auto b){return std::array<float,3>{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
  auto dot=[](auto a,auto b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
  auto cross=[](auto a,auto b){return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
  auto norm=[&](auto v){return std::sqrt(dot(v,v));};
  const uint32_t ol=m.U(32);if(!m.Rango(size_t(ol)+4ull*variante,4))return mejor;
  const uint32_t ob=m.U(size_t(ol)+4ull*variante);if(!m.Rango(ob,0xe0))return mejor;
  const uint32_t n=m.U(ob+0x98),tabla=m.U(ob+0xb0);
  if(n>10000||!m.Rango(tabla,size_t(n)*48))return mejor;
  // Lista real de las hojas del árbol C8, no todas las paredes del modelo.
  std::vector<uint32_t> refs,nodos;
  uint32_t nodo=m.U(ob+0xc8);
  while(nodo&&nodos.size()<10000) {
    if(!m.Rango(nodo,80)||std::find(nodos.begin(),nodos.end(),nodo)!=nodos.end())return mejor;
    nodos.push_back(nodo);const uint32_t cnt=m.U(nodo+0x30),lst=m.U(nodo+0x4c);
    if(cnt>10000||!m.Rango(lst,size_t(cnt)*4))return mejor;
    for(uint32_t i=0;i<cnt;++i)refs.push_back(m.U(lst+4*i));
    nodo=m.U(nodo+0x44);
  }
  const auto delta=sub(z,a);
  for(uint32_t k=0;k<n;++k) {
    const uint32_t rec=tabla+48*k;
    if(std::find(refs.begin(),refs.end(),rec)==refs.end()||(seleccionada&&!seleccionada(k)))continue;
    const uint32_t geo=m.U(rec),block=m.U(rec+8),amo=m.U(rec+28);
    if(!m.Rango(geo,32)||!m.Rango(block,12))continue;
    const uint64_t pool=uint64_t(amo)+m.U(block+4)+16;
    std::array<std::array<float,3>,3> v;
    bool bien=true;
    for(unsigned j=0;j<3;++j) {
      const uint64_t p=pool+16ull*m.U(geo+20+4*j);
      if(!m.Rango(size_t(p),12)){bien=false;break;}v[j]=vec(size_t(p));
    }
    if(!bien)continue;
    const auto normal=vec(geo);const float den=dot(delta,normal);
    if(std::fabs(den)<.001f)continue;
    const float t=dot(sub(v[0],a),normal)/den;
    if(t<0||t>1||t>=mejor.t)continue;
    std::array<float,3> p;for(unsigned j=0;j<3;++j)p[j]=a[j]+t*delta[j];
    const float area=norm(cross(sub(v[1],v[0]),sub(v[2],v[1])));
    float partes=0;for(unsigned j=0;j<3;++j)partes+=norm(cross(sub(v[j],p),sub(v[(j+1)%3],p)));
    if(partes>area+.01f)continue;
    const uint32_t atributo=m.U(geo+16);
    // Un atributo >=0 exige al menos una propiedad activa (&1), igual que
    // 1B5340. Un grupo inválido no se transforma en una superficie de suceso.
    bool activo=int32_t(atributo)<0;
    if(atributo<mas.U(4)) {
      const uint32_t g=mas.U(mas.U(8)+4*atributo);
      if(mas.Rango(g,12)&&mas.Rango(mas.U(g+8),size_t(mas.U(g+4))*16))
        for(uint32_t j=0;j<mas.U(g+4);++j) {
          const uint32_t q=mas.U(g+8)+16*j,cat=mas.U(q),idx=mas.U(q+4);
          if(cat>=mad.U(4))continue;const uint32_t mg=mad.U(mad.U(8)+4*cat);
          if(!mad.Rango(mg,16)||idx>=mad.U(mg+8))continue;
          const uint32_t prop=mad.U(mad.U(mg+12)+4*idx);
          if(mad.Rango(prop,8)&&(mad.U(prop)&1))activo=true;
        }
    }
    if(!activo)continue;
    mejor.hay=true;mejor.t=t;mejor.punto=p;mejor.normal=normal;mejor.superficie=atributo;
    mejor.registro=k;mejor.entrada=EntradaDeSuperficie(r,atributo);
  }
  return mejor;
}
// FUN_001B6520(x,z,0): suelo B2, no el suelo HD de CA2C0. El original
// usa propiedades &1; CA2C0 busca &2/&8 y en Base elige la plataforma6.133
// (registro94, flags18) en vez del suelo0 (registro46, flags17).
inline float AlturaSuelo(const Recurso& r,uint32_t variante,float x,float z,
                        const std::function<bool(uint32_t)>& seleccionada={}) {
  const Vista m=r.Bloque(3),mad=r.Bloque(5),mas=r.Bloque(6);
  auto fv=[&](size_t p){const uint32_t u=m.U(p);float f;std::memcpy(&f,&u,4);return f;};
  if(!std::isfinite(x)||!std::isfinite(z)||!m.Rango(0,40)||variante>=m.U(4))return 0;
  const uint32_t ol=m.U(32);
  if(!m.Rango(size_t(ol)+4ull*variante,4))return 0;
  const uint32_t ob=m.U(size_t(ol)+4ull*variante);
  if(!m.Rango(ob,0xe0))return 0;
  const uint32_t tabla=m.U(ob+0xb0),n=m.U(ob+0x98);
  if(n>10000||!m.Rango(tabla,size_t(n)*48))return 0;
  uint32_t nodo=m.U(ob+0xbc);
  // 1B1AC0: descender por X o Z hasta la hoja que contiene la consulta.
  for(unsigned pasos=0;nodo;++pasos) {
    if(pasos>=10000||!m.Rango(nodo,80))return 0;
    const bool derecha=fv(nodo+0x24)==fv(nodo+0x28)?x>=fv(nodo):z>=fv(nodo+8);
    const uint32_t hijo=m.U(nodo+(derecha?0x38:0x3c));
    if(!hijo)break;nodo=hijo;
  }
  uint32_t izq=nodo,der=nodo;bool izquierda=true;unsigned ni=0,nd=0;
  for(unsigned pasos=0;nodo&&pasos<10000;++pasos) {
    if(!m.Rango(nodo,80))return 0;
    const uint32_t cnt=m.U(nodo+0x30),lista=m.U(nodo+0x4c);
    if(cnt>10000||!m.Rango(lista,size_t(cnt)*4))return 0;
    for(uint32_t i=0;i<cnt;++i) {
      const uint32_t rec=m.U(lista+4*i);
      if(rec<tabla||(rec-tabla)%48||(rec-tabla)/48>=n||
         (seleccionada&&!seleccionada((rec-tabla)/48)))continue;
      const uint32_t geo=m.U(rec),bloque=m.U(rec+8);
      if(!m.Rango(geo,32)||!m.Rango(bloque,12))continue;
      const uint32_t atributo=m.U(geo+16);
      if(!mas.Rango(0,12)||atributo>=mas.U(4)||!mad.Rango(0,12)||
         !mas.Rango(size_t(mas.U(8))+4ull*atributo,4))continue;
      const uint32_t g=mas.U(size_t(mas.U(8))+4ull*atributo);
      if(!mas.Rango(g,12)||!mas.Rango(mas.U(g+8),size_t(mas.U(g+4))*16))continue;
      bool activa=false;
      for(uint32_t j=0;j<mas.U(g+4);++j) {
        const uint32_t q=mas.U(g+8)+16*j,cat=mas.U(q),idx=mas.U(q+4);
        if(cat>=mad.U(4)||!mad.Rango(size_t(mad.U(8))+4ull*cat,4))continue;
        const uint32_t mg=mad.U(size_t(mad.U(8))+4ull*cat);
        if(!mad.Rango(mg,16)||idx>=mad.U(mg+8)||
           !mad.Rango(size_t(mad.U(mg+12))+4ull*idx,4))continue;
        const uint32_t prop=mad.U(size_t(mad.U(mg+12))+4ull*idx);
        if(mad.Rango(prop,4)&&(mad.U(prop)&1)){activa=true;break;}
      }
      if(!activa)continue;
      const uint64_t pool=uint64_t(m.U(rec+28))+m.U(bloque+4)+16;
      std::array<std::array<float,3>,3> v;
      bool bien=true;
      for(unsigned j=0;j<3;++j) {
        const uint64_t p=pool+16ull*m.U(geo+20+4*j);
        if(!m.Rango(size_t(p),12)){bien=false;break;}
        v[j]={fv(size_t(p)),fv(size_t(p)+4),fv(size_t(p)+8)};
      }
      const float nx=fv(geo),ny=fv(geo+4),nz=fv(geo+8);
      if(!bien||ny==0)continue;
      const float y=((v[0][0]*nx+v[0][1]*ny+v[0][2]*nz)-(nx*x+nz*z))/ny;
      if(!std::isfinite(y))continue;
      std::array<bool,3> signo;
      for(unsigned j=0;j<3;++j) {
        const auto& a=v[j];const auto& b=v[(j+1)%3];
        signo[j]=(a[2]-z)*(b[0]-x)-(a[0]-x)*(b[2]-z)<0;
      }
      if(signo[0]==signo[1]&&signo[1]==signo[2])return y;
    }
    if(izquierda){if(izq)izq=nodo=m.U(izq+0x40);else if(der)der=nodo=m.U(der+0x44);++ni;}
    else {if(der)der=nodo=m.U(der+0x44);else if(izq)izq=nodo=m.U(izq+0x40);++nd;}
    izquierda=!izquierda;
    if(izq==der||(ni>=3&&nd>=3&&ni==nd))break;
  }
  return 0;
}
} // namespace dbz3::escenarios::b2
