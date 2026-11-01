// XV5: controlador visual propio para cadenas SCD; no afecta golpes ni logica.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dbz3::xv2 {
struct V {
  float x = 0, y = 0, z = 0;
  V operator+(V b) const { return {x+b.x, y+b.y, z+b.z}; }
  V operator-(V b) const { return {x-b.x, y-b.y, z-b.z}; }
  V operator*(float k) const { return {x*k, y*k, z*k}; }
};
inline float Dot(V a, V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(V a) { return std::sqrt(Dot(a,a)); }
inline V Unit(V a) { const float l=Length(a); return l>1e-7f ? a*(1/l) : V{0,1,0}; }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
struct M {
  float v[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  V Dir(V p) const { return {p.x*v[0]+p.y*v[4]+p.z*v[8],p.x*v[1]+p.y*v[5]+p.z*v[9],p.x*v[2]+p.y*v[6]+p.z*v[10]}; }
  V Point(V p) const { return Dir(p)+V{v[12],v[13],v[14]}; }
  V InvDir(V p) const { return {p.x*v[0]+p.y*v[1]+p.z*v[2],p.x*v[4]+p.y*v[5]+p.z*v[6],p.x*v[8]+p.y*v[9]+p.z*v[10]}; }
};
struct Node {
  uint32_t bone=0;
  int parent=-1;
  bool pinned=true;
  V bind, target, p, previous;
  float radius=.045f;
};
struct Sphere { V center; float radius=0; };
struct Controller {
  std::vector<Node> nodes;
  bool initialized=false;
  uint64_t steps=0, contacts=0;
  float max_offset=0;
  void Reset() {
    for (auto& n:nodes) n.p=n.previous=n.target;
    initialized=true;
  }
  // Un paso por cuadro del guest, no por pase de sombra ni reloj del host.
  void Step(const std::vector<Sphere>& body) {
    if (nodes.empty()) return;
    bool reset=!initialized;
    for (const auto& n:nodes) reset |= !Finite(n.target) || Length(n.target-n.p)>40;
    if (reset) Reset();
    constexpr float dt=1.0f/60.0f;
    for (auto& n:nodes) {
      if (n.pinned) { n.p=n.previous=n.target; continue; }
      const V velocity=(n.p-n.previous)*.88f;
      n.previous=n.p;
      n.p=n.p+velocity+((n.target-n.p)*100+V{0,-10,0})*(dt*dt);
    }
    for (int pass=0;pass<6;++pass) {
      for (auto& n:nodes) {
        if (n.pinned) continue;
        if (n.parent>=0 && size_t(n.parent)<nodes.size()) {
          const auto& p=nodes[size_t(n.parent)];
          const float rest=Length(n.target-p.target);
          n.p=p.p+Unit(n.p-p.p)*rest;
        }
        // Limite de resorte: impide saltos al teletransportarse/cambiar de pose.
        const V offset=n.p-n.target;
        const float cap=std::max(.12f,n.parent>=0 ? Length(n.target-nodes[size_t(n.parent)].target)*.65f : .2f);
        if (Length(offset)>cap) n.p=n.target+Unit(offset)*cap;
        for (const auto& s:body) {
          const float r=s.radius+n.radius;
          // El bind ya puede tocar el cuerpo: no se expulsa una costura cosida
          // hacia una superficie distinta de la original.
          const float allowed=std::min(r,Length(n.target-s.center));
          const V d=n.p-s.center;
          if (Length(d)+1e-5f<allowed) { n.p=s.center+Unit(d)*allowed; ++contacts; }
        }
        if (!Finite(n.p)) n.p=n.previous=n.target;
      }
    }
    for (const auto& n:nodes) max_offset=std::max(max_offset,Length(n.p-n.target));
    ++steps;
  }
};
// Implementacion y gancho de cuadro en xv2_fisica_runtime.inl (hooks.cpp).
void BeforeDraw(uint8_t* base, uint32_t model);
}  // namespace dbz3::xv2
