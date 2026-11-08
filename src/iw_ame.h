// dbz3 - Reproductor reducido de los efectos #AME de Infinite World (frente R13, tercera vuelta). Sacado de
// iw_mini_disparo.cpp para usarlo tambien en los minijuegos a pie (el polvo del sprint, 934/26: auditoria7/O).
#pragma once

#include <imgui.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <functional>
#include <vector>

#include "ps2_modelo.h"
#include "iw_gs_blend.h"

namespace dbz3::iwame {
using ps2::V3;
inline V3 Mas(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Por(const V3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Largo(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// ---- Efectos de IW (#AME v2, el mismo formato que usa B3): reproductor reducido (frente R13, tercera vuelta).
// Motor de IW (decomp EU 0x26F9C0-0x278340; tablas EU 0x4724A0 emisores y 0x472510 particulas, bloques de 16 B
// [iniciar, actualizar, pintar]): cabecera "#AME", 2, n.o de nodos, raiz 0x10; nodo +4 tipo (clase << 16 | subtipo),
// +8 banderas, +0x24 primer hijo, +0x28 hermano (desplazamientos en el archivo), +0x30 posicion.
// Emisor (0x1000x: 0 esfera, 1 cono, 2 caja, 3 radial): +0x50 duracion (-1 sin fin), +0x54 retraso, +0x58 factor de la
// velocidad del reloj, +0x5C/+0x60 radio (base + azar), +0x68/+0x6C velocidad (base + azar, por segundo), +0x74 tope
// de vivas por grupo, +0x78 por cuadro, +0x7C forma (cono: semiangulo +0x7C * pi).
// Particula (0x2000x: 0 linea, 1 cuadro girado, 2 rayo, 3 sprite, 4 modelo): +0x50 vida, +0x54 retraso, +0x58 factor
// de la velocidad del emisor, +0x5C/+0x60 tamano (medio = 0,5 (base + azar)), +0x68 -> +0x6C factor a lo largo de la
// vida, +0x70 + azar +0x74 angulo y +0x7C giro por segundo (cuadro girado, bandera 0x8), +0x80 -> +0x90 color (bandera
// 0x10), +0xA0 estado de mezcla y +0xA4 la textura del atlas del paquete (en el rayo +0xC0/+0xC4), +0xA8 rect UV
// (bandera 0x2000), +0xC4 cuadros de animacion y desde +0xC8 {duracion, u0, v0, u1, v1} (bandera 0x8000). Cada cuadro:
// edad + 1, posicion += velocidad / 60. ponytail: sin los afectores (nodos 0x3000x), sin la mezcla aditiva de la PS2
// (ImGui mezcla por alfa), sin el modelo (tipo 4); lineas y rayos como estelas a lo largo de su velocidad.
namespace ame {
struct Nodo {
  struct Afector {
    int tipo = 0; uint32_t flags = 0; V3 v;
    float fuerza = 0, exponente = 0, eje_z = 0;
  };
  bool emisor = false;
  int tipo = 0;
  uint32_t flags = 0;
  V3 pos;
  std::array<float, 4> q{0, 0, 0, 1};
  float f[48] = {};   // floats desde +0x50
  int tex = -1, cuadros = 0;
  int estado = 0;   // +0xA0 (rayo +0xC0): palabra de mezcla de 0x472480 = ALPHA del GS: 0 0x44 normal, 1 0x48 suma,
                    // 2 0x42 resta, 3 0x06
  std::vector<std::array<float, 5>> anim;   // {duracion, u0, v0, u1, v1}
  std::vector<Afector> afectores;
};
struct Efecto {   // un #AME: los emisores de la raiz con sus particulas hijas
  struct Grupo {
    Nodo emisor;
    std::vector<Nodo> parts;
    std::vector<std::shared_ptr<Efecto>> hijos; // definiciones; cada nacimiento tiene su estado propio
  };
  std::vector<Grupo> grupos;
  std::vector<Nodo> raices;   // EU 0x277270: particulas directas, sin emisor (933/29)
  explicit operator bool() const { return !grupos.empty() || !raices.empty(); }
};
inline Efecto Leer(const std::vector<uint8_t>& b, bool particulas_raiz = false) {
  Efecto e;
  if (b.size() < 0x20 || std::memcmp(b.data(), "#AME", 4) != 0) return e;
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
  auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
  auto nodo = [&](size_t o) {
    Nodo n;
    const uint32_t t = u32(o + 4);
    n.emisor = (t >> 16) == 1;
    n.tipo = int(t & 0xFFFF);
    n.flags = u32(o + 8);
    n.pos = {f32(o + 0x30), f32(o + 0x34), f32(o + 0x38)};
    for (size_t i = 0; i < 4; ++i) n.q[i] = f32(o + 0x40 + i * 4);
    for (size_t i = 0; i < 48; ++i) n.f[i] = f32(o + 0x50 + 4 * i);
    if (!n.emisor) {
      const size_t s = n.tipo == 2 ? 0x20 : n.tipo == 4 ? 0x10 : 0;
      // Tipo4: selector AMO en+5C (EU2716E0), no la derivaUV+ B4.
      n.tex = int(u32(o + (n.tipo==4 ? 0x5C : 0xA4+s)));
      n.estado = int(u32(o + 0xA0 + s) & 3);
      if (n.tipo != 4 && (n.flags & 0x8000)) {
        n.cuadros = int(std::min<uint32_t>(u32(o + 0xC4 + s), 64));
        for (int k = 0; k < n.cuadros; ++k) {
          std::array<float, 5> a{};
          for (int j = 0; j < 5; ++j) a[size_t(j)] = f32(o + 0xC8 + s + 20 * size_t(k) + 4 * size_t(j));
          n.anim.push_back(a);
        }
      }
    }
    // EU277830: hijos30000 se aplican despues del movimiento.
    if (!n.emisor)
      for (size_t c=u32(o+0x24), k=0;c>=0x10&&c+0x44<=b.size()&&k<32;c=u32(c+0x28),++k)
        if ((u32(c+4)>>16)==3)
          n.afectores.push_back({int(u32(c+4)&65535),u32(c+8),
              {f32(c+0x30),f32(c+0x34),f32(c+0x38)},f32(c+0x40),f32(c+0x44),f32(c+0x48)});
    return n;
  };
  std::function<Efecto::Grupo(size_t,int)> grupo = [&](size_t o,int profundidad) {
    Efecto::Grupo g;
    g.emisor=nodo(o);
    if(profundidad>=32) return g;
    for(size_t c=u32(o+0x24),k=0;c>=0x10&&c+0x60<=b.size()&&k<32;c=u32(c+0x28),++k) {
      const uint32_t clase=u32(c+4)>>16;
      if(clase==2) g.parts.push_back(nodo(c));
      else if(clase==1) {
        auto hijo=std::make_shared<Efecto>();
        hijo->grupos.push_back(grupo(c,profundidad+1));
        g.hijos.push_back(std::move(hijo));
      }
    }
    return g;
  };
  size_t o = u32(0x0C);
  for (int guarda = 0; o >= 0x10 && o + 0x60 <= b.size() && guarda < 256; ++guarda) {
    const uint32_t t = u32(o + 4);
    if ((t >> 16) == 1) {   // emisor: sus particulas por +0x24 y +0x28
      e.grupos.push_back(grupo(o,0));
    } else if (particulas_raiz && (t >> 16) == 2) {
      e.raices.push_back(nodo(o));
    }
    o = u32(o + 0x28);
    if (o == 0) break;
  }
  return e;
}
struct Particula {
  V3 p, v;
  float edad = 0, vida = 1, medio = 1, ang = 0;
  float giro_signo=1,cuadro_t=0;
  size_t cuadro=0;
  bool inv_u=false,inv_v=false;
  const Nodo* n = nullptr;
};
struct Instancia {
  const Efecto* ef = nullptr;
  int atlas = 0;   // que atlas (0 el del paquete del minijuego)
  V3 origen, dir{0, 0, 1};
  float edad = 0, esc = 1;
  std::vector<float> acum;
  std::vector<std::vector<float>> acum_hijos;
  std::vector<std::vector<Particula>> ps;   // por grupo y particula hija: aplanado por grupo
  bool vivo = true;
  bool modelos = false;   // tambien las particulas de modelo (tipo 4); las pinta el que llama (PintarModelos)
  bool mezcla_gs = false; // consumidor con atlas original y suma real del SDK
  struct Hija { size_t grupo = 0, definicion = 0; std::shared_ptr<Instancia> in; };
  std::vector<Hija> hijas;
  V3 velocidad;
  float giro_dibujo=0;
};
// Memory(n), EU127CA0: registro de desplazamiento con taps31/4.
inline uint32_t Mem(uint32_t& s,int bits) {
  while(bits-->0) s=(s<<1)|(((s>>31)^(s>>4))&1);
  return s-1;
}
inline float U(uint32_t& s) {
  return float(Mem(s,8)&255)/255;
}
inline V3 Cruz(const V3& a,const V3& b) {
  return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline V3 Rotar(const std::array<float,4>& q,const V3& v) {
  const V3 xyz{q[0],q[1],q[2]};
  const V3 t=Por(Cruz(xyz,v),2);
  return Mas(v,Mas(Por(t,q[3]),Cruz(xyz,t)));
}
inline Particula Nacer(const Nodo& n,float escala,bool gs,uint32_t& sem) {
  Particula p;p.n=&n;p.edad=-std::max(n.f[1],0.0f);p.vida=n.f[0]>0 ? n.f[0] : 60;
  if(n.tipo==2 || (n.tipo==4&&gs)) p.medio=escala;
  else p.medio=.5f*(n.f[3]+U(sem)*n.f[4])*escala;
  if(n.tipo==1) {
    p.ang=(n.f[8]+U(sem)*n.f[9])*0.01745329252f;
    if(n.flags&0x20) p.giro_signo=(Mem(sem,1)&1) ? -1.0f : 1.0f;
  }
  if(n.tipo!=4) {
    if(n.flags&0x20000) p.inv_u=(Mem(sem,1)&1)!=0;
    if(n.flags&0x40000) p.inv_v=(Mem(sem,1)&1)!=0;
  }
  return p;
}
inline void Afectar(Particula&,const Nodo::Afector&,const V3&,const V3&,const V3&,const V3&,float,uint32_t&);
inline void PasoParticula(Particula& p,uint32_t& sem,const Instancia& in,const V3& r,const V3& u,const V3& f) {
  ++p.edad;if(p.edad<=0) return;
  p.p=Mas(p.p,Por(p.v,1.0f/60));
  if(in.mezcla_gs) for(const auto& a:p.n->afectores) Afectar(p,a,in.origen,r,u,f,1.0f/60,sem);
  if(p.n->tipo==1&&(p.n->flags&8)) p.ang+=p.giro_signo*p.n->f[11]/60*0.01745329252f;
  if((p.n->flags&0x8000)&&!p.n->anim.empty()) {
    p.cuadro_t+=1;
    if(p.cuadro_t>=p.n->anim[p.cuadro][0]) {
      p.cuadro_t=0;
      if(p.cuadro+1<p.n->anim.size()) ++p.cuadro;
      else if(p.n->flags&0x10000) p.cuadro=0;
    }
  }
}
// EU271060..2715D0. Bit1=centro relativo, no una habilitacion.
inline void Afectar(Particula& p,const Nodo::Afector& a,const V3& origen,
                    const V3& r,const V3& u,const V3& f,float dt,uint32_t& sem) {
  auto rotar=[&](V3 v) { return Mas(Por(r,v.x),Mas(Por(u,v.y),Por(f,v.z))); };
  if(a.tipo==0||a.tipo==1) {
    V3 v=Por(a.v,a.fuerza);
    if(a.flags&2) v=rotar(v);
    p.p=Mas(p.p,Por(v,a.tipo==0 ? dt*dt*.5f : dt));
    if(a.tipo==0) p.v=Mas(p.v,Por(v,dt));
  } else if(a.tipo>=2&&a.tipo<=4) {
    const V3 centro=(a.flags&1) ? Mas(origen,a.v) : a.v;
    const V3 d=Mas(p.p,Por(centro,-1));
    if(a.tipo==3) {
      V3 eje{a.fuerza,a.exponente,a.eje_z};
      if(a.flags&2) eje=rotar(eje);
      const V3 w=Cruz(eje,d);
      p.v=Mas(p.v,Por(w,dt));
      p.v=Mas(p.v,Por(Cruz(eje,w),dt));
    } else {
      const float largo=Largo(d);
      // VRSQRT(1,0) satura a maximo finito: vector exactamente cero sigue cero.
      const V3 n=largo==0 ? V3{} : Por(d,1/largo);
      if(a.tipo==2) p.p=Mas(p.p,Por(n,a.fuerza*dt/std::pow(largo,a.exponente)));
      else { p.p=Mas(p.p,Por(n,a.fuerza*dt*dt*.5f)); p.v=Mas(p.v,Por(n,a.fuerza*dt)); }
    }
  }
}
// Un cuadro (60 por segundo): emision y particulas.
inline void Cuadro(Instancia& in, uint32_t& sem) {
  if (!in.ef) return;
  const size_t ng = in.ef->grupos.size() + (in.ef->raices.empty() ? 0 : 1);
  if (in.acum.size() != ng) {
    in.acum.assign(ng,0);in.ps.assign(ng,{});in.acum_hijos.assign(ng,{});
  }
  in.ps.resize(ng); // descartar solo la copia de dibujo de las hijas del cuadro anterior
  in.edad += 1;
  in.origen=Mas(in.origen,Por(in.velocidad,1.0f/60));
  const V3 f = in.dir;
  V3 r{f.z, 0, -f.x};
  r = Largo(r) < 1e-3f ? V3{1, 0, 0} : Por(r, 1.0f / Largo(r));
  const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
  bool queda = false;
  for (size_t g = 0; g < ng; ++g) {
    if (g == in.ef->grupos.size()) {
      // Un solo nacimiento por raiz; coordenadas locales para que el consumidor
      // aplique la matriz COMPLETA del hueso, tambien durante la animacion.
      auto& lista = in.ps[g];
      if (in.edad == 1)
        for (const Nodo& n : in.ef->raices) {
          if (n.tipo == 4 ? !in.modelos : n.tex < 0) continue;
          Particula p=Nacer(n,in.esc,in.mezcla_gs,sem);
          p.p = n.pos;
          lista.push_back(p);
        }
      for (auto& p : lista) PasoParticula(p,sem,in,r,u,f);
      lista.erase(std::remove_if(lista.begin(), lista.end(), [](const Particula& p) { return p.edad >= p.vida; }), lista.end());
      if (!lista.empty()) queda = true;
      continue;
    }
    const auto& gr = in.ef->grupos[g];
    const float* E = gr.emisor.f;
    auto& lista = in.ps[g];
    const float dur = E[0] < 0 ? (in.mezcla_gs ? std::numeric_limits<float>::infinity() : 600.0f) : E[0], edad = in.edad - E[1];
    if (edad <= 0 && dur > 0) queda = true; // futuras emisiones (933/15: retraso14/16)
    if (edad > 0 && edad < dur) {
      queda = true;
      const size_t nh=gr.parts.size()+gr.hijos.size();
      auto& acumuladores=in.acum_hijos[g];
      if(acumuladores.size()!=nh) acumuladores.assign(nh,0);
      auto contexto=[&](V3 v) {return Mas(Por(r,v.x),Mas(Por(u,v.y),Por(f,v.z)));};
      // EU276570/275350: acumulador y tope independientes por hijo.
      for(size_t hi=0;hi<nh;++hi) {
        const Nodo* n=hi<gr.parts.size() ? &gr.parts[hi] : nullptr;
        if(n&&(n->tipo==4 ? !in.modelos : n->tex<0)) continue;
        acumuladores[hi]+=std::max(E[10],0.0f);
        while(acumuladores[hi]>=1) {
          acumuladores[hi]-=1;
          size_t vivas=0;
          if(n) vivas=size_t(std::count_if(lista.begin(),lista.end(),[&](const auto& p) {return p.n==n;}));
          else for(const auto& h:in.hijas) if(h.grupo==g&&h.definicion==hi-gr.parts.size()) {
            const auto& e=h.in->ef->grupos[0].emisor;
            if(h.in->vivo&&(e.f[0]<0||h.in->edad-e.f[1]<e.f[0])) ++vivas;
          }
          if(E[9]>=0&&float(vivas)>=E[9]) continue; // no consume azar sin plaza
          V3 d{0,0,1},extra{};
          if(gr.emisor.tipo==0) {
            d={U(sem)-.5f,U(sem)-.5f,U(sem)-.5f};
            const float l=Largo(d);if(l>0) d=Por(d,1/l);
            d=Rotar(gr.emisor.q,d);
          } else {
            const uint16_t ang=uint16_t(std::lround(U(sem)*65536));
            const float ph=float(ang)*6.28318530718f/65536;
            if(gr.emisor.tipo==1) {
              const float th=U(sem)*E[11]*3.14159265359f;
              d={std::sin(th)*std::cos(ph),std::sin(th)*std::sin(ph),std::cos(th)};
            } else if(gr.emisor.tipo==3) {
              const float a=E[12]*0.01745329252f;
              extra=Rotar(gr.emisor.q,{E[11]*std::sin(ph),0,E[11]*std::cos(ph)});
              d=Rotar(gr.emisor.q,{std::sin(a)*std::sin(ph),std::cos(a),std::sin(a)*std::cos(ph)});
            }
          }
          const float axial=E[3]+U(sem)*E[4],velocidad=E[6]+U(sem)*E[7];
          const V3 base=Mas(in.origen,Por(contexto(gr.emisor.pos),in.esc));
          const V3 off=Por(contexto(Mas(extra,Por(d,axial))),in.esc);
          const V3 vel=Por(contexto(d),velocidad*in.esc);
          if(n) {
            auto p=Nacer(*n,in.esc,in.mezcla_gs,sem);
            p.p=Mas(Mas(base,off),Por(n->pos,in.esc));
            p.v=in.mezcla_gs ? Mas(vel,Por(in.velocidad,n->f[2])) : vel;
            lista.push_back(p);
          } else {
            const size_t hijo=hi-gr.parts.size();
            auto a=std::make_shared<Instancia>();a->ef=gr.hijos[hijo].get();a->origen=Mas(base,off);
            a->velocidad=Mas(vel,Por(in.velocidad,a->ef->grupos[0].emisor.f[2]));
            a->dir=in.dir;a->esc=in.esc;a->modelos=in.modelos;a->mezcla_gs=in.mezcla_gs;
            in.hijas.push_back({g,hijo,std::move(a)});
          }
        }
      }
    }
    for (Particula& p : lista) PasoParticula(p,sem,in,r,u,f);
    lista.erase(std::remove_if(lista.begin(), lista.end(), [](const Particula& p) { return p.edad >= p.vida; }),
                lista.end());
    if (!lista.empty()) queda = true;
  }
  for(auto& h:in.hijas) if(h.in->vivo) {
    Cuadro(*h.in,sem);
    if(h.in->vivo) queda=true;
    for(const auto& lista:h.in->ps) in.ps.push_back(lista);
  }
  in.hijas.erase(std::remove_if(in.hijas.begin(),in.hijas.end(),[](const auto& h) { return !h.in->vivo; }),in.hijas.end());
  in.vivo = queda;
}
// `suma`: el atlas con alfa = brillo (la mezcla de suma del GS, Cs·As + Cd, con la mezcla por alfa de ImGui).
// B2: la resta (0x42, Cd - Cs·As) ya se pinta; ImGui no permite cambiar el blend por draw, asi que se aproxima
// oscureciendo con el brillo de Cs (negro con alfa = brillo·alfa). No es 1:1 (marcado [H]).
inline void Pintar(const Instancia& in, const ps2::Camara& cam, const std::vector<ps2::Tex>& atlas_n,
                    const std::vector<ps2::Tex>& suma, ImDrawList* dl, float tope_px, bool gs_propio=false) {
  if(!cam.ok) return;
  const float foco=cam.Foco();
  static const bool forzar_alfa = std::getenv("DBZ3_TEST_AME_ALFA") != nullptr;
  int mezcla=0;
  for (const auto& lista : in.ps)
    for (const Particula& p : lista) {
      const Nodo& n = *p.n;
      if (n.tipo == 4) continue;   // tipo 4 (modelo): lo pinta quien lo pidio; la resta (estado 2) si se pinta
      // ponytail: tambien la mezcla normal (0x44) con alfa = brillo: con el alfa de las texturas tal cual salian
      // manchas oscuras (sus texturas de efecto son grises con alfa plano); la 3 (0x06) va normal
      const std::vector<ps2::Tex>& atlas = gs_propio||(in.mezcla_gs&&n.estado!=2)||n.estado==3 ? atlas_n : suma;
      if (p.edad <= 0 || n.tex < 0 || n.tex >= int(atlas.size()) || !atlas[size_t(n.tex)]) continue;
      const float t = std::clamp(p.edad / p.vida, 0.0f, 1.0f);
      const float* F = n.f;
      auto color=[&](int inicial,int final) {
        float c[4];
        for(int i=0;i<4;++i) c[i]=std::clamp(F[inicial+i]+((n.flags&16) ? (F[final+i]-F[inicial+i])*t : 0),0.0f,1.0f);
        if(n.estado==2&&!gs_propio) {
          c[3]*=std::max({c[0],c[1],c[2]});c[0]=c[1]=c[2]=0;
        }
        if(forzar_alfa && (n.estado==1 || n.tipo==2)) c[3]=1.0f;
        return IM_COL32(int(c[0]*255),int(c[1]*255),int(c[2]*255),int(c[3]*255));
      };
      const ImU32 c=color(12,16),ce=n.tipo==2 ? color(20,24) : c;
      if(!(c>>24)&&!(ce>>24)) continue;
      ImVec2 uv0(0, 0), uv1(1, 1);
      const int uv=n.tipo==2 ? 30 : 22,deriva=n.tipo==2 ? 34 : 26;
      if(n.flags&0x2000) {uv0=ImVec2(F[uv],F[uv+1]);uv1=ImVec2(F[uv+2],F[uv+3]);}
      if((n.flags&0x8000)&&!n.anim.empty()) {
        const auto& f=n.anim[std::min(p.cuadro,n.anim.size()-1)];
        uv0=ImVec2(f[1],f[2]);uv1=ImVec2(f[3],f[4]);
      }
      if(p.inv_u) std::swap(uv0.x,uv1.x);
      if(p.inv_v) std::swap(uv0.y,uv1.y);
      if(n.flags&0x4000) {
        const float du=F[deriva]*p.edad/60*(p.inv_u ? -1 : 1),dv=F[deriva+1]*p.edad/60*(p.inv_v ? -1 : 1);
        uv0.x+=du;uv1.x+=du;uv0.y+=dv;uv1.y+=dv;
      }
      const ImTextureID id = atlas[size_t(n.tex)].id();
      const int nueva_mezcla=gs_propio ? n.estado : in.mezcla_gs&&n.estado==1 ? 1 : 0;
      // ALPHA0x48 = Cs*As+Cd: el backend ya lo admite. Restablecer siempre
      // alfa al terminar para que ningun HUD herede el estado del efecto.
      if (mezcla!=nueva_mezcla) {
        dl->AddCallback(nueva_mezcla==2 ? iwgs::Resta : nueva_mezcla==3 ? iwgs::Inversa :
                        nueva_mezcla==1 ? rex::ui::kImDrawBlendAdditive : rex::ui::kImDrawBlendAlpha,nullptr);
        mezcla=nueva_mezcla;
      }
      if(n.tipo==2) {
        // EU272FA0: extremo +direccion*longitud, dos anchos y dos colores.
        const float lv=Largo(p.v);if(lv==0) continue;
        const V3 d=Por(p.v,1/lv);
        const V3 atras{cam.m[2],cam.m[6],cam.m[10]},der{cam.m[0],cam.m[4],cam.m[8]},arr{cam.m[1],cam.m[5],cam.m[9]};
        const V3 ojo=Por(Mas(Por(der,cam.m[12]),Mas(Por(arr,cam.m[13]),Por(atras,cam.m[14]))),-1);
        V3 normal=Cruz(d,Mas(p.p,Por(ojo,-1)));const float ln=Largo(normal);if(ln==0) continue;normal=Por(normal,1/ln);
        const float l=p.medio*(F[3]+(F[4]-F[3])*t),wb=p.medio*(F[5]+(F[6]-F[5])*t),we=p.medio*(F[7]+(F[8]-F[7])*t);
        const V3 e=Mas(p.p,Por(d,l));
        const V3 mundo[4]={Mas(e,Por(normal,we)),Mas(e,Por(normal,-we)),Mas(p.p,Por(normal,-wb)),Mas(p.p,Por(normal,wb))};
        struct V {V3 p;ImVec2 uv;ImU32 c;};
        V pol[8]={{mundo[0],uv0,ce},{mundo[1],ImVec2(uv1.x,uv0.y),ce},
                  {mundo[2],uv1,c},{mundo[3],ImVec2(uv0.x,uv1.y),c}},rec[8];
        auto z=[&](V3 v) {return cam.m[2]*v.x+cam.m[6]*v.y+cam.m[10]*v.z+cam.m[14];};
        auto lerp=[&](const V& a,const V& b,float t) {
          V v;v.p=Mas(a.p,Por(Mas(b.p,Por(a.p,-1)),t));
          v.uv=ImVec2(a.uv.x+(b.uv.x-a.uv.x)*t,a.uv.y+(b.uv.y-a.uv.y)*t);
          v.c=0;
          for(unsigned sh:{0u,8u,16u,24u}) {
            const float ca=float((a.c>>sh)&255),cb=float((b.c>>sh)&255);
            v.c|=uint32_t(std::clamp(int(ca+(cb-ca)*t+.5f),0,255))<<sh;
          }
          return v;
        };
        int nr=0;bool cortado=false;
        for(int i=0;i<4;++i) {
          const V& a=pol[(i+3)%4];const V& b=pol[i];const float za=z(a.p)+1,zb=z(b.p)+1;
          if((za<=0)!=(zb<=0)) {rec[nr++]=lerp(a,b,za/(za-zb));cortado=true;}
          if(zb<=0) rec[nr++]=b;else cortado=true;
        }
        if(nr<3) continue;
        ImVec2 xy[8];bool valido=true;
        for(int i=0;i<nr;++i) {
          const auto& v=rec[i].p;const float k=foco/std::max(1.0f,-z(v));
          xy[i]=ImVec2(cam.c.x+(cam.m[0]*v.x+cam.m[4]*v.y+cam.m[8]*v.z+cam.m[12])*k,
              cam.c.y-(cam.m[1]*v.x+cam.m[5]*v.y+cam.m[9]*v.z+cam.m[13])*k);
          valido=valido&&std::isfinite(xy[i].x)&&std::isfinite(xy[i].y);
        }
        if(!valido) continue;
        static std::atomic<bool> rayo_medido{false};
        if(n.tex==41&&std::getenv("DBZ3_SOLIW9_VUELO_PASOS")&&!rayo_medido.exchange(true))
          REXLOG_INFO("dbz3 iwame9: rayo41 geometria original L {:.3f} wb {:.3f} we {:.3f} alfa_base {} alfa_ext {} UV {:.3f},{:.3f}..{:.3f},{:.3f}",
                      l,wb,we,c>>24,ce>>24,uv0.x,uv0.y,uv1.x,uv1.y);
        dl->PushTextureID(id);dl->PrimReserve((nr-2)*3,nr);const unsigned b=dl->_VtxCurrentIdx;
        if(!cortado) for(unsigned i:{0u,1u,3u,3u,1u,2u}) dl->PrimWriteIdx(ImDrawIdx(b+i));
        else for(int i=1;i<nr-1;++i) {dl->PrimWriteIdx(ImDrawIdx(b));dl->PrimWriteIdx(ImDrawIdx(b+i));dl->PrimWriteIdx(ImDrawIdx(b+i+1));}
        for(int i=0;i<nr;++i) dl->PrimWriteVtx(xy[i],rec[i].uv,rec[i].c);
        dl->PopTextureID();
      } else {
        // EU274990/273C10: tipo0 es sprite de dos dimensiones, no una estela.
        ImVec2 s;float k=0;if(!cam.P(p.p,s,k,foco)) continue;
        const float tam=p.medio*(F[6]+(F[7]-F[6])*t);
        const float rx=n.tipo==3 ? std::clamp(tam*k,0.0f,tope_px) : tam*k;
        const float ry=rx*(n.tipo==3 ? 1 : 1+F[5]);
        const float a=n.tipo==1 ? p.ang : 0,ca=std::cos(a),sa=std::sin(a);
        dl->AddImageQuad(id,ImVec2(s.x-ca*rx-sa*ry,s.y+sa*rx-ca*ry),ImVec2(s.x+ca*rx-sa*ry,s.y-sa*rx-ca*ry),
            ImVec2(s.x+ca*rx+sa*ry,s.y-sa*rx+ca*ry),ImVec2(s.x-ca*rx+sa*ry,s.y+sa*rx+ca*ry),
            uv0,ImVec2(uv1.x,uv0.y),uv1,ImVec2(uv0.x,uv1.y),c);
      }
    }
  if (mezcla) dl->AddCallback(rex::ui::kImDrawBlendAlpha,nullptr);
}
// EU2716E0/2719A0: selectorAMG+5C, escala70->80 y color90->A0.
// Mallas sin Centrar; la restaGS y orientaciones aleatorias siguen pendientes.
inline size_t PintarModelos(const Instancia& in,const ps2::Camara& cam,
    const std::vector<ps2::Malla>& modelos,const std::vector<ps2::Tex>& atlas,ImDrawList* dl) {
  size_t dibujados=0;
  ps2::Escena escena;
  for (const auto& lista:in.ps) for (const auto& p:lista) {
    const auto& n=*p.n;
    if (n.tipo!=4||p.edad<=0||n.tex<0||size_t(n.tex)>=modelos.size()||!modelos[size_t(n.tex)]||n.estado==2) continue;
    const float t=std::clamp(p.edad/p.vida,0.0f,1.0f);
    float q[4];std::copy(n.q.begin(),n.q.end(),q);
    if (n.flags&4) {
      const float ang=n.f[7]*p.edad/60, sn=std::sin(ang/2), cs=std::cos(ang/2);
      const float x=n.f[4]*sn,y=n.f[5]*sn,z=n.f[6]*sn;
      const float a=q[0],b=q[1],c=q[2],d=q[3];
      q[0]=d*x+a*cs+b*z-c*y;q[1]=d*y-a*z+b*cs+c*x;
      q[2]=d*z+a*y-b*x+c*cs;q[3]=d*cs-a*x-b*y-c*z;
    }
    if(in.giro_dibujo!=0) {
      const float s=std::sin(in.giro_dibujo*.5f),c=std::cos(in.giro_dibujo*.5f);
      const float x=q[0],y=q[1],z=q[2],w=q[3];
      q[0]=c*x+s*z;q[1]=c*y+s*w;q[2]=c*z-s*x;q[3]=c*w-s*y;
    }
    const float x=q[0],y=q[1],z=q[2],w=q[3];
    const ps2::M34 rot{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0,
        2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0,
        2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0}};
    float sc[3],col[4];
    for(int i=0;i<3;++i) sc[i]=(n.f[8+i]+(n.f[12+i]-n.f[8+i])*t)*p.medio;
    for(int i=0;i<4;++i) col[i]=std::clamp(n.f[16+i]+((n.flags&16)?(n.f[20+i]-n.f[16+i])*t:0),0.0f,1.0f);
    ps2::Malla m=modelos[size_t(n.tex)];
    for(auto& tr:m.tris) {
      tr.trans=true;tr.con_color=true;
      for(int i=0;i<3;++i) {
        const auto v=tr.p[i];tr.p[i]=Mas(p.p,rot.gira({v.x*sc[0],v.y*sc[1],v.z*sc[2]}));
        if(n.flags&32) {tr.uv[i].x+=n.f[25]*p.edad/60;tr.uv[i].y+=n.f[26]*p.edad/60;}
        const ImU32 c=tr.col[i];
        tr.col[i]=IM_COL32(int(col[0]*(c&255)),int(col[1]*((c>>8)&255)),int(col[2]*((c>>16)&255)),int(col[3]*(c>>24)));
      }
    }
    escena.Empezar();escena.Poner(cam,m,atlas,{},0,1);
    if(n.estado==1) dl->AddCallback(rex::ui::kImDrawBlendAdditive,nullptr);
    escena.Pintar(dl);
    if(n.estado==1) dl->AddCallback(rex::ui::kImDrawBlendAlpha,nullptr);
    dibujados+=escena.Pintados();
  }
  return dibujados;
}
// Aura de cuerpo entero (31/10). EU 0x1D3BC0 Aura_UpdateAll / 0x1D3930 Aura_DrawAll: 2 emisores 0x10004 (plantillas EU
// 0x447E60/0x447FE0, "Back Aura"; EU 0x2709B0) repartidos por 5 tramos de puntos de cuerpo, con rayos (tipo 2, EU
// 0x2739D0/0x273530/0x272FA0) en coordenadas relativas a la raiz. Medido por PINE en el final de 0x7D2 y en juego:
// ki 1,0; tasa 1,2 y 1,3; hijos 20/25 cuadros; alfa 0,5 x (d/30)^2 cerca de la camara.
// ponytail: ki fijo en 1 (lo medido; la fila de ki del luchador no existe en B3); estados 0x13/0x17 no frenan la estela.
struct AuraCuerpo {
  struct Rayo { V3 p, v; float edad = 0; bool inv_u = false, inv_v = false; };
  Nodo n[2];                 // nodo del #AME del paquete del personaje (ids de la fila de la forma)
  const float* pr = nullptr; // fila de 0x448460
  const float* al[2] = {nullptr, nullptr};  // filas de 0x448780
  std::vector<Rayo> rayos[2];
  float acum[2] = {0, 0};
  V3 w0{0, 1, 0}, previa;
  int cuenta = 0;
  bool hay_previa = false;
  uint32_t sem = 29;
  explicit operator bool() const { return pr != nullptr; }
};
inline constexpr float kAuraParam[3][20] = {   // EU 0x448460 (0x50 B por fila): las de las formas que usan los campos
    {0.3f, 0.9f, 7, 20, 1.2f, 15, 15, 4, 1, 20, 0.3f, 1, 4, 8, 0.5f, 15, 9, 4, 1, 25},
    {0.3f, 1, 7, 12, 1.5f, 20, 18, 6, 2, 10, 0.12f, 2, 4, 12, 0.3f, 17, 10, 4, 2, 10},
    {0.3f, 1, 6, 15, 1.5f, 20, 14, 4, 0.5f, 20, 0.06f, 0.5f, 6, 15, 0.2f, 17, 12.3f, 3, 0.75f, 25}};
inline constexpr float kAuraAlfa[6][4] = {{1, 0, 1, 0}, {0.5f, 0, 0.5f, 0}, {0.2f, 0, 0.5f, 0},   // EU 0x448780
                                          {0.5f, 0, 1, 0}, {1, 0, 0.1f, 0}, {1, 0, 1, 1}};
// EU 0x448840 + 42 personaje + 7 forma: {+0x59, +0x5A, +0x5B, +0x5C}. Las tres primeras son iguales en los personajes
// de los campos. +0x59 elige los #AME del paquete: 2 -> 1,2; 3 -> 3,4; 4 -> 5,6 (EU 0x1D4680 Auras).
inline constexpr int kAuraForma[3][4] = {{2, 0, 1, 2}, {3, 1, 5, 0}, {4, 2, 0, 3}};
// `puntos`: [0] raiz, y los tramos 1-4, 1-5, 4-6, 5-7, 3-2 de la tabla EU 0x449D50 (PuntoCuerpo).
inline void AuraPaso(AuraCuerpo& a, const V3 (&puntos)[8], const V3& ojo, float& alfa) {
  const V3 raiz = puntos[0];
  // EU 0x1D2030 Aura_UpdateFollowPosition: la estela apunta hacia atras al moverse y vuelve a (0,1,0) parado.
  const V3 paso = a.hay_previa ? Mas(a.previa, Por(raiz, -1)) : V3{};
  const float dist = Largo(paso);
  const V3 atras = dist > 1e-4f ? Por(paso, 1 / dist) : V3{0, 1, 0};
  auto hacia = [&](const V3& obj, float k) { a.w0 = Mas(obj, Por(Mas(a.w0, Por(obj, -1)), k)); };
  if (dist >= 0.1f && dist < 2.5f && a.cuenta <= 0x1D) hacia(atras, 0.96f), ++a.cuenta;
  else if (dist >= 2.5f && dist < 10 && a.cuenta <= 0x1D) hacia(atras, 0.9f), ++a.cuenta;
  else if (dist >= 10) hacia(atras, 0.8f), ++a.cuenta;
  else hacia({0, 1, 0}, 0.9f), a.cuenta = std::max(0, a.cuenta - 1);
  a.previa = raiz;
  a.hay_previa = true;
  // EU 0x1D41C0 Aura_CalcAlpha con ki 1 y EU 0x1D4360: cerca de la camara, tasa y alfa x (d/30)^2.
  const V3 al_ojo = Mas(ojo, Por(raiz, -1));
  const float d = Largo(al_ojo);
  const float cerca = d < 30 ? (d / 30) * (d / 30) : 1.0f;
  alfa = cerca;
  const V3 al_ojo_n = d > 1e-4f ? Por(al_ojo, 1 / d) : V3{0, 0, 1};
  static constexpr int kTramo[5][2] = {{1, 2}, {1, 3}, {2, 4}, {3, 5}, {6, 7}};
  const float tasa[2] = {(a.pr[1] + a.pr[0]) * cerca, (a.pr[11] + a.pr[10]) * cerca};
  const float atras_k[2] = {-3, -3}, delante_k[2] = {4, 0};   // plantillas +0x54 y +0x58
  const float vel[2] = {a.pr[2], a.pr[12]};
  for (int e = 0; e < 2; ++e) {
    for (auto& r : a.rayos[e]) ++r.edad, r.p = Mas(r.p, Por(r.v, 1.0f / 60));
    const float vida = a.pr[e ? 19 : 9];
    a.rayos[e].erase(std::remove_if(a.rayos[e].begin(), a.rayos[e].end(), [&](const auto& r) { return r.edad >= vida; }), a.rayos[e].end());
    a.acum[e] += tasa[e];
    while (a.acum[e] >= 1) {
      a.acum[e] -= 1;
      const float u = U(a.sem);
      const int t = int(Mem(a.sem, 8) % 5);
      V3 p0 = Mas(puntos[kTramo[t][0]], Por(raiz, -1)), p1 = Mas(puntos[kTramo[t][1]], Por(raiz, -1));
      const bool pierna = t == 2 || t == 3;
      if (pierna) p1.y -= 3;
      V3 p = Mas(p0, Por(Mas(p1, Por(p0, -1)), u));
      const float escala = !pierna || 0.3f < U(a.sem) ? atras_k[e] : delante_k[e];
      const V3 hacia_ojo = Por(al_ojo_n, escala);
      p = Mas(p, hacia_ojo);
      V3 lado = Cruz(a.w0, hacia_ojo);
      const float ll = Largo(lado);
      lado = ll > 1e-5f ? Por(lado, 1 / ll) : V3{};
      float s = U(a.sem);
      if (!(Mem(a.sem, 1) & 1)) s = -s;
      V3 dir = Mas(Por(lado, s), Por(a.w0, 3));
      const float ld = Largo(dir);
      dir = ld > 1e-5f ? Por(dir, 1 / ld) : V3{0, 1, 0};
      AuraCuerpo::Rayo r;
      r.p = Mas(p, a.n[e].pos);
      r.v = Por(dir, vel[e] * a.n[e].f[2]);
      if (a.n[e].flags & 0x20000) r.inv_u = (Mem(a.sem, 1) & 1) != 0;
      if (a.n[e].flags & 0x40000) r.inv_v = (Mem(a.sem, 1) & 1) != 0;
      a.rayos[e].push_back(r);
    }
  }
}
// EU 0x272FA0, con la parte de textura (bandera 0x1000): cuatro vertices; el color +0x40 en la punta y el +0x30 en la base.
inline ps2::Malla AuraMalla(const AuraCuerpo& a, const V3& raiz, const V3& ojo, float alfa) {
  ps2::Malla m;
  for (int e = 0; e < 2; ++e) {
    const Nodo& n = a.n[e];
    const float vida = a.pr[e ? 19 : 9], l0 = n.f[3], l1 = a.pr[e ? 16 : 6], w0 = a.pr[e ? 17 : 7], w1 = a.pr[e ? 18 : 8];
    const float* al = a.al[e];
    for (const auto& r : a.rayos[e]) {
      if (r.edad <= 0) continue;
      const float t = std::clamp(r.edad / vida, 0.0f, 1.0f);
      const float lv = Largo(r.v);
      if (lv < 1e-5f) continue;
      const V3 d = Por(r.v, 1 / lv), b = Mas(r.p, raiz);
      V3 nor = Cruz(d, Mas(b, Por(ojo, -1)));
      const float ln = Largo(nor);
      if (ln < 1e-5f) continue;
      nor = Por(nor, 1 / ln);
      const float L = l0 + (l1 - l0) * t, w = w0 + (w1 - w0) * t;
      const V3 punta = Mas(b, Por(d, L));
      const V3 v[4] = {Mas(punta, Por(nor, w)), Mas(punta, Por(nor, -w)), Mas(b, Por(nor, w)), Mas(b, Por(nor, -w))};
      const float u0 = r.inv_u ? 1.f : 0.f, u1 = 1 - u0, v0 = r.inv_v ? 1.f : 0.f, v1 = 1 - v0;
      const ImVec2 uv[4] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
      const int a_base = int(std::clamp((al[0] + (al[1] - al[0]) * t) * alfa, 0.0f, 1.0f) * 255);
      const int a_punta = int(std::clamp((al[2] + (al[3] - al[2]) * t) * alfa, 0.0f, 1.0f) * 255);
      const ImU32 cp = IM_COL32(255, 255, 255, a_punta), cb = IM_COL32(255, 255, 255, a_base);
      const ImU32 c[4] = {cp, cp, cb, cb};
      for (const auto& idx : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{2, 1, 3}}) {
        ps2::Malla::Tri tr;
        tr.tex = n.tex; tr.trans = true; tr.con_color = true;
        for (int i = 0; i < 3; ++i) tr.p[i] = v[idx[size_t(i)]], tr.uv[i] = uv[idx[size_t(i)]], tr.col[i] = c[idx[size_t(i)]];
        m.tris.push_back(tr);
      }
    }
  }
  return m;
}
}  // namespace ame

}  // namespace dbz3::iwame
