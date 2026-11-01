// Conversion AP usada por anim_iw.cpp y prueba independiente (sin arrancar el juego).
// IW EU 23F7E0..2403F0 / B3 US tabla 8201F588, handlers 82121818..82121D80.
// Para ejecutar la prueba: compilar este header como C++ con DBZ3_IW_AP_PRUEBA
// y DBZ3_IW_AP_NATIVAS apuntando a los cuerpos generated extraidos, sin modificarlos.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace dbz3::anim::ap {
constexpr uint32_t kHrIw = 4096;  // banco IW separado de los HR del #CSK anfitrion
using Registro = std::array<uint8_t, 16>;
using Hr = std::array<uint8_t, 128>;
struct Pistas {
  uint32_t n = 0;
  std::vector<uint8_t> datos;    // BE; offsets de registros relativos al comienzo de datos
  std::vector<Hr> hr;
};
inline bool Rango(const std::vector<uint8_t>& b, size_t p, size_t n) {
  return p <= b.size() && n <= b.size() - p;
}
inline uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
inline uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void Poner16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
inline void Poner32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
// No convertir +2 como u16: ambos motores leen ID@2 y activacion@3 como BYTES.
// AP1: HR u32@4, flags u16@8, ANCLA u16@10, radio u8@12, xyz s8@13..15.
// sub_82112AB0 pasa ANCLA por [X+2956], no por el indice de la pista #AMM.
inline Registro ConvertirRegistro(const uint8_t* p, uint16_t gestor) {
  Registro r{};
  Poner16(r.data(), Le16(p));
  r[2] = p[2]; r[3] = p[3];
  Poner32(r.data() + 4, Le32(p + 4));
  if (gestor == 1) {
    Poner16(r.data() + 8, Le16(p + 8));
    Poner16(r.data() + 10, Le16(p + 10));
    std::memcpy(r.data() + 12, p + 12, 4);
  } else {
    Poner32(r.data() + 8, Le32(p + 8));
    Poner32(r.data() + 12, Le32(p + 12));
  }
  return r;
}
// Salida transaccional: una pista invalida no deja una tabla guest parcialmente utilizable.
inline bool Convertir(const std::vector<uint8_t>& b, uint32_t bloque, uint32_t primer_hr, Pistas& salida) {
  if (!Rango(b, bloque, 48) || !Rango(b, 0, 32)) return false;
  const uint32_t n = Le32(b.data() + bloque + 40), off = Le32(b.data() + bloque + 44);
  if (n && (!off || off > b.size() || n > (b.size() - off) / 8)) return false;
  Pistas p;
  for (uint32_t j = 0; j < n; ++j) {
    const uint8_t* l = b.data() + off + 8 * j;
    if (Le16(l) > 7) return false;
    if (Le16(l) != 7) ++p.n;
  }
  p.datos.resize(8 * p.n);
  std::map<uint32_t, uint32_t> hr_usados;
  uint32_t linea = 0;
  for (uint32_t j = 0; j < n; ++j) {
    const uint8_t* l = b.data() + off + 8 * j;
    const uint16_t gestor = Le16(l), nr = Le16(l + 2);
    const uint32_t registros = Le32(l + 4);
    // El n del original y del nativo es s16. Sin truncar ni limitar a 64 registros.
    if (nr > 32767 || (nr && (!registros || !Rango(b, registros, size_t(nr) * 16)))) return false;
    if (gestor == 7) continue;  // exclusivamente DispararEvento: no duplicar colas nativas
    const uint32_t dst = uint32_t(p.datos.size()), cab = 8 * linea++;
    p.datos.resize(p.datos.size() + 16 * nr);
    Poner16(p.datos.data() + cab, gestor);
    Poner16(p.datos.data() + cab + 2, nr);
    Poner32(p.datos.data() + cab + 4, dst);
    for (uint32_t k = 0; k < nr; ++k) {
      const uint8_t* src = b.data() + registros + 16 * k;
      Registro r = ConvertirRegistro(src, gestor);
      if (gestor == 1 && src[3]) {
        const uint16_t flags = Le16(src + 8), ancla = Le16(src + 10);
        // IW agrega 200/400 y dos contadores que B3 no tiene (ocupan sus manos).
        // No reinterpretarlos como hitboxes B3 ni corromper el contexto.
        if ((flags & 0x600) || ancla >= 22) return false;
        if ((flags & 0x10F) != 0x100) {
          const uint32_t id = Le32(src + 4), nh = Le32(b.data() + 24), oh = Le32(b.data() + 28);
          if (id >= nh || !oh || !Rango(b, size_t(oh) + size_t(id) * 128, 128)) return false;
          auto it = hr_usados.find(id);
          if (it == hr_usados.end()) {
            Hr h{};
            const uint8_t* s = b.data() + oh + size_t(id) * 128;
            for (uint32_t q = 0; q < 128; q += 16) {
              for (uint32_t w = 0; w < 8; w += 2) Poner16(h.data() + q + w, Le16(s + q + w));
              Poner32(h.data() + q + 8, Le32(s + q + 8));
              if (Le16(s + q + 4) == 2) {  // knockaway: dos direcciones s16, no float
                Poner16(h.data() + q + 12, Le16(s + q + 12));
                Poner16(h.data() + q + 14, Le16(s + q + 14));
              } else Poner32(h.data() + q + 12, Le32(s + q + 12));
            }
            it = hr_usados.emplace(id, primer_hr + uint32_t(p.hr.size())).first;
            p.hr.push_back(h);
          }
          Poner32(r.data() + 4, it->second);
        }
      }
      std::memcpy(p.datos.data() + dst + 16 * k, r.data(), r.size());
    }
  }
  salida = std::move(p);
  return true;
}
template<class Reservar, class Escribir>
uint32_t Instalar(const Pistas& p, Reservar reservar, Escribir escribir) {
  if (!p.n) return 0;
  const uint32_t g = reservar(uint32_t(p.datos.size()));
  if (!g) return 0;
  for (uint32_t k = 0; k < p.datos.size(); ++k) escribir(g + k, p.datos[k]);
  for (uint32_t j = 0; j < p.n; ++j) {
    uint8_t ptr[4];
    Poner32(ptr, g + Be32(p.datos.data() + 8 * j + 4));
    for (uint32_t k = 0; k < 4; ++k) escribir(g + 8 * j + 4 + k, ptr[k]);
  }
  return g;
}
}  // namespace dbz3::anim::ap

#ifdef DBZ3_IW_AP_PRUEBA
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <rex/ppc/context.h>

// Se ejecutan los cuerpos recompilados REALES, con memoria BE dispersa y sin DLL,
// renderer, ventana o recursos del juego. Solo se sustituye el acceso a memoria.
static std::unordered_map<uint32_t, uint8_t> memoria_ap;
static uint64_t LeerAp(uint32_t a, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i) v = (v << 8) | memoria_ap[a + i];
  return v;
}
static void EscribirAp(uint32_t a, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i) memoria_ap[a + n - 1 - i] = uint8_t(v >> (8 * i));
}
#define REX_LOAD_U8(a) uint8_t(LeerAp(uint32_t(a), 1))
#define REX_LOAD_U16(a) uint16_t(LeerAp(uint32_t(a), 2))
#define REX_LOAD_U32(a) uint32_t(LeerAp(uint32_t(a), 4))
#define REX_LOAD_U64(a) LeerAp(uint32_t(a), 8)
#define REX_STORE_U8(a,v) EscribirAp(uint32_t(a), (v), 1)
#define REX_STORE_U16(a,v) EscribirAp(uint32_t(a), (v), 2)
#define REX_STORE_U32(a,v) EscribirAp(uint32_t(a), (v), 4)
#define REX_STORE_U64(a,v) EscribirAp(uint32_t(a), (v), 8)
#define REX_FUNC_PROLOGUE() ((void)0)
#define DEFINE_REX_FUNC(n) void n(PPCContext& ctx, uint8_t* base)
#define REX_CALL_INDIRECT_FUNC(a) ResolverAp(uint32_t(a))(ctx, base)
#include DBZ3_IW_AP_NATIVAS

namespace dbz3::anim::ap::prueba {
static int bien = 0, total = 0;
static void Exigir(bool ok, const char* que) {
  ++total;
  if (ok) ++bien;
  else std::cerr << "FAIL " << que << '\n';
}
static void Le(std::vector<uint8_t>& b, size_t a, uint32_t v, unsigned n = 4) {
  for (unsigned i = 0; i < n; ++i) b[a + i] = uint8_t(v >> (8 * i));
}
struct EstadoIw {
  uint16_t flags = 0, alta = 0;
  uint32_t v4 = 0x3F800000, v3 = 0;
  uint8_t manos[3] = {};
  std::vector<Registro> activos;
};
// Oraculo PS2 separado de la conversion: lee LE directamente y sigue los cuerpos
// EU23FF80/23FD40/23FC20/23FB30/23FA40/23F920/23F7E0 y 2400E0/240220.
static void Original(const std::vector<uint8_t>& b, EstadoIw& s, float de, float a, uint32_t bloque = 0x20) {
  s.alta = 0;
  for (unsigned j = 0; j < Le32(b.data() + bloque + 40); ++j) {
    const auto* l = b.data() + Le32(b.data() + bloque + 44) + 8 * j;
    const unsigned gestor = Le16(l), n = Le16(l + 2);
    unsigned k = 0;
    const auto* r = b.data() + Le32(l + 4);
    while (k < n && Le16(r + 16 * k) <= de) ++k;
    for (; k < n && Le16(r + 16 * k) <= a; ++k) {
      const uint8_t* p = r + 16 * k;
      const uint32_t v = Le32(p + 4);
      if (gestor == 0 || gestor == 2) {
        const uint16_t bit = gestor == 0 ? 0x20 : 0x40;
        s.flags = p[3] ? s.flags | bit : s.flags & ~bit;
      } else if (gestor == 3) s.v3 = v;
      else if (gestor == 4) s.v4 = v;
      else if (gestor == 5) s.flags = (s.flags | Le16(p + 4)) & ~(Le32(p + 8) & 31);
      else if (gestor == 6 && v < 3) s.manos[v] = uint8_t(Le32(p + 8));
      else if (gestor == 1) {
        if (p[3]) {
          uint16_t f = Le16(p + 8);
          if (s.activos.size() < 8) {
            Registro rr; std::memcpy(rr.data(), p, 16);
            if (f & 0xF1) { f |= 1; rr[8] = uint8_t(f); rr[9] = uint8_t(f >> 8); }
            s.activos.insert(s.activos.begin(), rr);
          }
          const unsigned cat = f & 0x70F;
          s.alta |= cat == 1 ? 1 : cat == 2 ? 4 : cat == 4 ? 2 : cat == 8 ? 8 : 0;
        } else {
          for (auto i = s.activos.begin(); i != s.activos.end(); ++i)
            if ((*i)[2] == p[2]) { s.activos.erase(i); break; }
        }
      }
    }
  }
}
static std::vector<uint8_t> Fixture() {
  std::vector<uint8_t> b(0x1000);
  Le(b, 0x48, 8); Le(b, 0x4C, 0x80); Le(b, 24, 1); Le(b, 28, 0xE00);
  for (unsigned t = 0; t < 8; ++t) {
    Le(b, 0x80 + 8 * t, t, 2); Le(b, 0x82 + 8 * t, 4, 2);
    Le(b, 0x84 + 8 * t, 0x100 + 0x80 * t);
    for (unsigned k = 0; k < 4; ++k) {
      const unsigned p = 0x100 + 0x80 * t + 16 * k;
      Le(b, p, 2 + 2 * k, 2); b[p + 2] = uint8_t(k % 2); b[p + 3] = uint8_t(k < 2);
      Le(b, p + 4, 0x3F000000 + 0x800000 * k);
      Le(b, p + 8, 0x12345678); Le(b, p + 12, 0xA1B2C3D4);
      if (t == 1) {
        Le(b, p + 4, 0); Le(b, p + 8, k % 2 ? 4 : 1, 2); Le(b, p + 10, 19, 2);
        b[p + 12] = 25; b[p + 13] = 0xF1; b[p + 14] = 17; b[p + 15] = 0xE2;
      } else if (t == 5) { Le(b, p + 4, 0xFEDC001F); Le(b, p + 8, k + 1); }
      else if (t == 6) { Le(b, p + 4, k % 3); Le(b, p + 8, k ? k : 255); }
    }
  }
  // HR conocido: 8 ramas, dano u16, knockaway con 2 angulos al final.
  for (unsigned k = 0; k < 8; ++k) {
    const unsigned p = 0xE00 + 16 * k;
    Le(b, p, 137 + k, 2); Le(b, p + 4, 2, 2);
    Le(b, p + 8, 0x3F800000); Le(b, p + 12, 0x1122, 2); Le(b, p + 14, 0x3344, 2);
  }
  return b;
}
static void Comparar(const EstadoIw& s, uint32_t c) {
  Exigir(LeerAp(c + 452, 2) == s.flags, "flags AP0/2/5 contra IW");
  Exigir(LeerAp(c + 440, 4) == s.v3 && LeerAp(c + 436, 4) == s.v4, "valores AP3/4 contra IW");
  for (unsigned i = 0; i < 3; ++i) Exigir(LeerAp(c + 430 + i, 1) == s.manos[i], "AP6 manos (B3 no usa offsets PS2)");
  Exigir(LeerAp(c + 424, 1) == s.activos.size(), "AP1 altas/bajas consumidas por lista nativa");
  Exigir(LeerAp(c + 454, 2) == s.alta, "AP1 mascara transitoria contra IW");
  uint32_t nodo = uint32_t(LeerAp(c + 416, 4));
  unsigned contadores[5] = {};
  for (const auto& r : s.activos) {
    const uint32_t p = uint32_t(LeerAp(nodo + 32, 4));
    Exigir(nodo && LeerAp(p + 2, 1) == r[2], "AP1 orden y registro referenciado por consumidor");
    const uint16_t ancla = Le16(r.data() + 10);
    Exigir(LeerAp(p + 10, 2) == (ancla == 19 ? 3 : ancla) &&
        LeerAp(p + 12, 4) == Be32(r.data() + 12), "AP1 ancla19->3/radio/xyz no invertidos");
    const unsigned f = Le16(r.data() + 8) & 0x70F;
    if (f == 1) ++contadores[0]; else if (f == 4) ++contadores[1];
    else if (f == 2) ++contadores[2]; else if (f == 8) ++contadores[3]; else if (f == 256) ++contadores[4];
    nodo = uint32_t(LeerAp(nodo + 44, 4));
  }
  Exigir(!nodo, "AP1 cadena sin nodos sobrantes");
  for (unsigned i = 0; i < 5; ++i) Exigir(LeerAp(c + 425 + i, 1) == contadores[i], "AP1 contador nativo por categoria");
  Exigir(!LeerAp(c + 476, 2) && !LeerAp(c + 472, 2), "AP7 no despachada por el motor nativo");
}
static void Corpus(const char* ruta) {
  std::ifstream afs(ruta, std::ios::binary);
  if (!afs) throw std::runtime_error("no se puede leer el AFS del corpus");
  uint8_t cab[8]; afs.read(reinterpret_cast<char*>(cab), 8);
  if (std::memcmp(cab, "AFS", 3)) throw std::runtime_error("corpus no es AFS");
  std::vector<uint8_t> tab(8 * Le32(cab + 4)); afs.read(reinterpret_cast<char*>(tab.data()), tab.size());
  unsigned clips = 0, registros[8] = {};
  for (unsigned ent : {204u,912u,913u,914u,915u,916u,917u,918u,919u,920u,921u,922u,923u,924u,925u,926u,927u,928u,929u,930u,931u,932u}) {
    const uint32_t off = Le32(tab.data() + 8 * ent), tam = Le32(tab.data() + 8 * ent + 4);
    std::vector<uint8_t> amb(tam); afs.seekg(off); afs.read(reinterpret_cast<char*>(amb.data()), tam);
    if (amb.size() < 32 || std::memcmp(amb.data(), "#AMB", 4)) continue;
    for (unsigned hijo = 0; hijo < Le32(amb.data() + 16); ++hijo) {
      const uint8_t* e = amb.data() + Le32(amb.data() + 20) + 16 * hijo;
      const uint32_t o = Le32(e), n = Le32(e + 4);
      if (!Rango(amb, o, n) || n < 32 || std::memcmp(amb.data() + o, "#BSK", 4)) continue;
      const std::vector<uint8_t> b(amb.begin() + o, amb.begin() + o + n);
      for (unsigned cod = 0; cod < Le32(b.data() + 16); ++cod) {
        const uint32_t bloque_iw = Le32(b.data() + Le32(b.data() + 20) + 4 * cod);
        if (!bloque_iw) continue;
        Pistas p;
        const bool ok = Convertir(b, bloque_iw, kHrIw, p);
        Exigir(ok, "conversion corpus real IW");
        if (!ok) throw std::runtime_error("AP real sin convertir: entrada=" + std::to_string(ent) + " codigo=" + std::to_string(cod));
        if (!p.n) continue;
        ++clips;
        const uint32_t nl = Le32(b.data() + bloque_iw + 40), ap = Le32(b.data() + bloque_iw + 44);
        for (unsigned j = 0; j < nl; ++j) registros[Le16(b.data() + ap + 8 * j)] += Le16(b.data() + ap + 8 * j + 2);
        for (bool eu : {false, true}) {
          memoria_ap.clear();
          const uint32_t t = Instalar(p, [](uint32_t) { return 0x10000u; }, [](uint32_t a, uint8_t v) { EscribirAp(a,v,1); });
          constexpr uint32_t c = 0x20000, bloque = 0x30000;
          EscribirAp(bloque + 40, p.n, 4); EscribirAp(bloque + 44, t, 4);
          EscribirAp(0x820007CC, 0xBF800000, 4); EscribirAp(0x820007A4, 0x3F800000, 4);
          PrepararTablaAp(eu);
          PPCContext ctx{}; ctx.r1.u64 = 0x80000;
          float anterior = -1; EstadoIw s;
          for (float f : {0.0f,0.0f,0.5f,2.0f,8.0f,32.0f,10000.0f,0.0f,32.0f}) {
            if (f < anterior) { ctx.r3.u64 = c; ResetAp(eu,ctx,nullptr); s = {}; anterior = -1; }
            Original(b,s,anterior,f,bloque_iw);
            ctx.r3.u64 = c; ctx.r4.u64 = bloque; ctx.f1.f64 = f; EvaluarAp(eu,ctx,nullptr);
            Comparar(s,c); anterior = f;
          }
        }
      }
    }
  }
  std::cout << "Corpus real: " << clips << " codigos con AP0..6; registros por gestor:";
  for (auto n : registros) std::cout << ' ' << n;
  std::cout << '\n';
}
}  // namespace dbz3::anim::ap::prueba

int main(int argc, char** argv) {
  using namespace dbz3::anim::ap;
  using namespace dbz3::anim::ap::prueba;
  auto b = Fixture();
  Pistas p; Exigir(Convertir(b, 0x20, kHrIw, p), "convertir las 8 pistas");
  Exigir(p.n == 7 && p.hr.size() == 1, "AP0..6 preservadas/AP7 separada/HR deduplicado");
  Exigir(Be32(p.hr[0].data() + 12) == 0x11223344 && p.hr[0][1] == 137, "HR knockaway y dano convertidos");
  for (bool eu : {false, true}) {
    memoria_ap.clear();
    uint32_t siguiente = 0x10000;
    const uint32_t tabla = Instalar(p, [&](uint32_t n) { auto a = siguiente; siguiente += n; return a; },
        [](uint32_t a, uint8_t v) { EscribirAp(a, v, 1); });
    constexpr uint32_t c = 0x20000, bloque = 0x30000;
    EscribirAp(bloque + 40, p.n, 4); EscribirAp(bloque + 44, tabla, 4);
    // Cero, uno y menos uno de la imagen retail (tambien los usa el reset EU).
    EscribirAp(0x820007CC, 0xBF800000, 4); EscribirAp(0x820007A4, 0x3F800000, 4);
    EscribirAp(0x8201F60C, 0, 4); EscribirAp(0x8201F604, 0, 4);
    PrepararTablaAp(eu);
    PPCContext ctx{}; ctx.r1.u64 = 0x80000;
    float anterior = -1;
    EstadoIw s;
    for (float f : {0.0f, 0.0f, 1.5f, 2.0f, 2.0f, 3.5f, 4.0f, 6.0f, 8.0f, 8.0f, 0.0f, 8.0f}) {
      if (f < anterior) {
        ctx.r3.u64 = c; ResetAp(eu, ctx, nullptr);
        s = {}; anterior = -1;
      }
      Original(b, s, anterior, f);
      ctx.r3.u64 = c; ctx.r4.u64 = bloque; ctx.f1.f64 = f;
      EvaluarAp(eu, ctx, nullptr);
      Comparar(s, c);
      anterior = f;
    }
    // Cambio de bloque: el dispatcher mismo reinicia banderas/lista/manos/valores.
    ctx.r3.u64 = c; ctx.r4.u64 = bloque + 0x100; ctx.f1.f64 = 0;
    EvaluarAp(eu, ctx, nullptr); Comparar(EstadoIw{}, c);
  }
  // Dos bordes que el corpus de campo no ejercita: capacidad8 y pistas largas.
  for (unsigned borde = 0; borde < 2; ++borde) {
    auto largo = b;
    largo.resize(0x3000);
    const unsigned gestor = borde ? 3 : 1, nr = borde ? 96 : 24;
    Le(largo,0x82+8*gestor,nr,2); Le(largo,0x84+8*gestor,0x1000);
    for (unsigned k=0;k<nr;++k) {
      const unsigned e=0x1000+16*k;
      std::memcpy(largo.data()+e,b.data()+0x100+0x80*gestor,16);
      Le(largo,e,borde ? k : k<12 ? 2 : 4,2);
      if (borde) Le(largo,e+4,0x3F000000+0x800000*(k%2));
      else { largo[e+2]=uint8_t(k%12); largo[e+3]=uint8_t(k<12); }
    }
    Pistas pista; Exigir(Convertir(largo,0x20,kHrIw,pista), "AP>64 y saturacion: conversion sin truncar");
    for (bool eu : {false,true}) {
      memoria_ap.clear(); PrepararTablaAp(eu);
      const uint32_t t=Instalar(pista,[](uint32_t) { return 0x10000u; },[](uint32_t a,uint8_t v) { EscribirAp(a,v,1); });
      EscribirAp(0x30028,pista.n,4); EscribirAp(0x3002C,t,4);
      EscribirAp(0x820007CC,0xBF800000,4); EscribirAp(0x820007A4,0x3F800000,4);
      PPCContext c{}; c.r1.u64=0x80000; EstadoIw s; float de=-1;
      for (float f : {2.0f,3.0f,4.0f,100.0f}) {
        Original(largo,s,de,f); c.r3.u64=0x20000; c.r4.u64=0x30000; c.f1.f64=f;
        EvaluarAp(eu,c,nullptr); Comparar(s,0x20000); de=f;
      }
    }
  }
  Pistas intacto = p;
  // El contador de lineas es u32; el limite64 era del port, no del original.
  auto muchas = b;
  muchas.resize(0x6000);
  Le(muchas,0x48,96); Le(muchas,0x4C,0x4000);
  for (unsigned i=0;i<96;++i) {
    Le(muchas,0x4000+8*i,3,2); Le(muchas,0x4002+8*i,1,2);
    Le(muchas,0x4004+8*i,0x280);
  }
  Pistas largas;
  Exigir(Convertir(muchas,0x20,kHrIw,largas) && largas.n==96,
         "mas de64 lineas AP validas no se descartan");
  Le(muchas,0x48,0x40000000);
  Exigir(!Convertir(muchas,0x20,kHrIw,largas), "contador de lineas no desborda validacion");
  auto malo = b; Le(malo, 0x84, 0xFFFFFFF0);
  Exigir(!Convertir(malo, 0x20, kHrIw, p) && p.datos == intacto.datos, "offset fuera de rango/transaccion");
  malo = b; Le(malo, 0x88, 8, 2);
  Exigir(!Convertir(malo, 0x20, kHrIw, p), "gestor fuera de tabla rechazado");
  malo = b; Le(malo, 0x188, 0x200, 2);
  Exigir(!Convertir(malo, 0x20, kHrIw, p), "categoria exclusiva IW no se reinterpreta en B3");
  malo = b; Le(malo, 0x18A, 22, 2);
  Exigir(!Convertir(malo, 0x20, kHrIw, p), "ancla fuera de tabla rechazada");
  Exigir(!Instalar(p, [](uint32_t) { return 0u; }, [](uint32_t, uint8_t) {}), "fallo de reserva");
  // Mutacion causal del bug propuesto: swap u16@2 invierte ID/activo y cambia el resultado REAL.
  memoria_ap.clear();
  uint32_t t = Instalar(intacto, [](uint32_t) { return 0x10000u; }, [](uint32_t a, uint8_t v) { EscribirAp(a,v,1); });
  const uint32_t r = uint32_t(LeerAp(t + 4,4));
  EscribirAp(r + 2, 1, 1); EscribirAp(r + 3, 0, 1);
  EscribirAp(0x823D75D8, 0x20000, 4);
  PPCContext ctx{}; ctx.r1.u64 = 0x80000; ctx.r3.u64 = t; ctx.f1.f64 = -1; ctx.f2.f64 = 2;
  sub_82121818(ctx, nullptr);
  Exigir(LeerAp(0x20000 + 452, 2) == 0, "mutacion endian impide activar cabeza (causal)");
  // Antes/despues causal: los siete efectos desaparecen al volver a bloque9/10=0.
  unsigned recuperados = 0;
  for (unsigned gestor = 0; gestor < 7; ++gestor) {
    auto uno = b; Le(uno,0x48,1); Le(uno,0x4C,0x80 + 8 * gestor);
    Pistas pista; Exigir(Convertir(uno,0x20,kHrIw,pista), "pista aislada para prueba causal");
    uint64_t huella[2] = {};
    for (unsigned con = 0; con < 2; ++con) {
      memoria_ap.clear(); PrepararTablaAp(false);
      const uint32_t tabla = Instalar(pista, [](uint32_t) { return 0x10000u; }, [](uint32_t a,uint8_t v) { EscribirAp(a,v,1); });
      EscribirAp(0x30028, con ? pista.n : 0, 4); EscribirAp(0x3002C, con ? tabla : 0, 4);
      EscribirAp(0x820007CC,0xBF800000,4); EscribirAp(0x820007A4,0x3F800000,4);
      PPCContext c{}; c.r1.u64=0x80000; c.r3.u64=0x20000; c.r4.u64=0x30000; c.f1.f64=2;
      EvaluarAp(false,c,nullptr);
      const uint32_t campo = gestor==1 ? 424 : gestor==3 ? 440 : gestor==4 ? 436 : gestor==6 ? 430 : 452;
      huella[con]=LeerAp(0x20000+campo,gestor==3||gestor==4 ? 4 : gestor==1||gestor==6 ? 1 : 2);
    }
    if (huella[0]!=huella[1]) ++recuperados;
    Exigir(huella[0]!=huella[1], "eliminar AP0..6 cambia el efecto nativo correspondiente (causal)");
  }
  std::cout << "Antes (AP descartadas): 0/7 efectos; despues: " << recuperados << "/7\n";
  if (argc > 1) Corpus(argv[1]);
  std::cout << "AP IW vs nativo US/EU: " << bien << '/' << total << (bien == total ? " PASS\n" : " FAIL\n");
  return bien == total ? 0 : 1;
}
#endif
