// Incluido una sola vez por hooks.cpp. XV5S esta fuera de los nombres del AWG;
// un modelo nativo o un conversor sin SCD no tiene el marcador y sale sin estado.
#include "xv2_fisica.h"
#include "eu_map.h"
#include <rex/logging.h>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <unordered_map>

namespace dbz3::xv2 {
namespace {
thread_local uint64_t frame=0;
bool Ptr(uint32_t p) { return p>=0x10000000u && p<0xA0000000u && !(p&3); }
uint32_t Address(uint32_t owner,uint32_t p) { return p && p<0x1000000u ? owner+p : p; }
float F(uint8_t* base,uint32_t p) { return std::bit_cast<float>(REX_LOAD_U32(p)); }
M Matrix(uint8_t* base,uint32_t p) {
  M m;
  for (int i=0;i<16;++i) m.v[i]=F(base,p+4*uint32_t(i));
  return m;
}
struct Vertex {
  uint32_t index=0, bone=0;
  std::array<uint32_t,4> node{};
  std::array<float,4> weight{};
  V bind;
};
struct Instance {
  uint32_t awg=0, marker=0, vb=0, count=0;
  uint64_t last=~uint64_t{0};
  Controller physics;
  std::vector<Vertex> vertices;
  std::vector<M> bones;
  std::vector<Sphere> body;
  double max_us=0;
};
thread_local std::unordered_map<uint32_t,Instance> instances;
bool Load(uint8_t* base,uint32_t g,uint32_t meta,uint32_t nb,Instance& s) {
  const uint32_t nn=REX_LOAD_U32(meta+8), nv=REX_LOAD_U32(meta+12), bytes=REX_LOAD_U32(meta+16);
  const uint32_t count=REX_LOAD_U32(meta+20);
  const uint32_t vb=Address(g,REX_LOAD_U32(g+0x30))-REX_LOAD_U32(g+0x2C);
  if (REX_LOAD_U32(meta+4)!=1 || !nn || nn>256 || !nv || nv>65535 ||
      bytes!=32+nn*32+nv*40 || REX_LOAD_U32(meta+24)!=nb ||
      !Ptr(vb) || count>65535 || count*44!=REX_LOAD_U32(g+0x2C)) return false;
  s={}; s.awg=g; s.marker=meta; s.vb=vb; s.count=count;
  s.bones.resize(nb); s.body.reserve(4); s.physics.nodes.resize(nn);
  for (uint32_t i=0;i<nn;++i) {
    const uint32_t p=meta+32+i*32;
    auto& n=s.physics.nodes[i];
    n.bone=REX_LOAD_U32(p); n.parent=int(REX_LOAD_U32(p+4))-1;
    n.pinned=REX_LOAD_U32(p+8)!=0;
    n.bind={F(base,p+16),F(base,p+20),F(base,p+24)}; n.radius=F(base,p+28);
    if (n.bone>=nb || n.parent>=int(i) || n.parent < -1 || !Finite(n.bind) ||
        !std::isfinite(n.radius) || n.radius<0 || n.radius>2) return false;
  }
  s.vertices.resize(nv);
  for (uint32_t i=0;i<nv;++i) {
    const uint32_t p=meta+32+nn*32+i*40;
    auto& v=s.vertices[i]; v.index=REX_LOAD_U32(p); v.bone=REX_LOAD_U32(p+4);
    if (v.index>=count || v.bone>=nb) return false;
    float sum=0;
    for (uint32_t j=0;j<4;++j) {
      v.node[j]=REX_LOAD_U32(p+8+j*4); v.weight[j]=F(base,p+24+j*4); sum+=v.weight[j];
      if (v.node[j]>nn || !std::isfinite(v.weight[j]) || v.weight[j]<0 || v.weight[j]>1) return false;
    }
    if (std::abs(sum-1)>1e-3f) return false;
    const uint32_t a=vb+v.index*44;
    v.bind={F(base,a),F(base,a+4),F(base,a+8)};
    if (!Finite(v.bind)) return false;
  }
  REXLOG_INFO("dbz3 xv2 fisica: SCD activada nodos={} vertices={} modelo_awg={:08X} region={}",nn,nv,g,g_guest_eu?"eu":"us");
  return true;
}
}  // namespace

void BeforeDraw(uint8_t* base,uint32_t model) {
  if (!Ptr(model)) return;
  const uint32_t g=REX_LOAD_U32(model+4);
  if (!Ptr(g) || (REX_LOAD_U32(g)!=0x23415747u && REX_LOAD_U32(g)!=0x21415747u)) return;  // #AWG / !AWG tras relocacion
  const uint32_t nb=REX_LOAD_U32(g+16), names=Address(g,REX_LOAD_U32(g+28));
  if (!nb || nb>256 || !Ptr(names)) return;
  const uint32_t meta=names+nb*32;
  if (REX_LOAD_U32(meta)!=0x58563553u) { instances.erase(model); return; } // XV5S
  const uint32_t table=REX_LOAD_U32(model+1032);
  if (!Ptr(table)) return;
  if (!instances.count(model)) {
    if (instances.size()>=16) instances.clear();
    Instance s;
    if (!Load(base,g,meta,nb,s)) return;
    instances.emplace(model,std::move(s));
  }
  auto& s=instances.at(model);
  if (s.awg!=g || s.marker!=meta) { instances.erase(model); return; }
  if (s.last==frame) return;
  s.last=frame;
  const auto start=std::chrono::steady_clock::now();
  // B3: matrices por filas, traslacion en 48/52/56. Se pasa a mundo con
  // el luchador propietario para que correr/volar tambien produzca inercia.
  M world;
  constexpr GA cfg=0x823D6CF4;
  const uint32_t manager=REX_LOAD_U32(cfg);
  if (Ptr(manager)) {
    for (uint32_t side=0;side<2;++side) {
      const uint32_t x=manager+2096+side*8240;
      if (REX_LOAD_U32(x+2952)==model || REX_LOAD_U32(x+4616)==model) {
        world=Matrix(base,x);
        std::swap(world.v[1],world.v[4]); std::swap(world.v[2],world.v[8]); std::swap(world.v[6],world.v[9]);
        break;
      }
    }
  }
  for (uint32_t i=0;i<nb;++i) s.bones[i]=Matrix(base,table+i*64);
  for (auto& n:s.physics.nodes) n.target=world.Point(s.bones[n.bone].Point(n.bind));
  s.body.clear();
  // Esferas del cuerpo a partir de huesos reales, no coordenadas de pantalla.
  // Se escogen por sufijo para que no se asuma el orden de Goku en Vegeta/Piccolo.
  for (uint32_t i=0;i<nb;++i) {
    const char* label=reinterpret_cast<const char*>(base+names+i*32);
    char name[33]{}; std::memcpy(name,label,32);
    if (std::strstr(name,"_HEAD") || std::strstr(name,"_CHEST") || std::strstr(name,"_WAIST")) {
      const float r=std::strstr(name,"_HEAD") ? .75f : 1.2f;
      s.body.push_back({world.Point(s.bones[i].Point({})),r});
    }
  }
  s.physics.Step(s.body);
  for (const auto& v:s.vertices) {
    V delta;
    for (size_t k=0;k<4;++k) if (v.node[k]) {
      const auto& n=s.physics.nodes[v.node[k]-1];
      delta=delta+(n.p-n.target)*v.weight[k];
    }
    const V p=v.bind+s.bones[v.bone].InvDir(world.InvDir(delta));
    if (!Finite(p)) continue;
    const uint32_t a=s.vb+v.index*44;
    REX_STORE_U32(a,std::bit_cast<uint32_t>(p.x));
    REX_STORE_U32(a+4,std::bit_cast<uint32_t>(p.y));
    REX_STORE_U32(a+8,std::bit_cast<uint32_t>(p.z));
  }
  const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
  s.max_us=std::max(s.max_us,us);
  if (s.physics.steps==120 || s.physics.steps%600==0)
    REXLOG_INFO("dbz3 xv2 fisica: cuadros={} contactos={} desplazamiento_max={:.5f} coste_max_us={:.1f}",
                s.physics.steps,s.physics.contacts,s.physics.max_offset,s.max_us);
}
}  // namespace dbz3::xv2

DBZ3_HOOK_EU(sub_82088F70) {
  ++dbz3::xv2::frame;
  orig(ctx,base);
}

// Paso de animacion: conserva el modelo en r3 (generated.27 / EU.17).
// Se usan las matrices de la ultima pose completa; el motor compone la nueva
// despues. El render estatico 0x8208F3A0 usa otro tipo de instancia.
DBZ3_HOOK(sub_8208DE08, dbz3eu_sub_8208DDD0) {
  const uint32_t model=ctx.r3.u32;
  orig(ctx,base);
  dbz3::xv2::BeforeDraw(base,model);
}
