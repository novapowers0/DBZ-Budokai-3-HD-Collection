// dbz3 - Modelos 3D de PS2 (#AMO/#AMG/#AMT/#AMM de IW, B1, B2) leidos de la copia del jugador y pintados sobre el
// combate con ImGui (frente P4, 2026-10-16; antes dentro de iw_minijuegos.cpp). Ver ps2_modelo.h.
#include "ps2_modelo.h"

#include "ingame/game_style.h"   // L05: GuestImage

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "launcher/launcher_state.h"

namespace dbz3::ps2 {
namespace {
namespace P = dbz3::pasarela;
// Normal unitaria; (0, 0, 0) = "sin normal" (el modelo no la trae, o basura: no finita o enorme, como las de algunas
// listas de pesos de B2, que daban NaN y triangulos por toda la pantalla en el borde del toon)
V3 Unitaria(const V3& v) {
  const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return std::isfinite(l) && l > 1e-6f && l < 1e6f ? V3{v.x / l, v.y / l, v.z / l} : V3{};
}
float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
}  // namespace

// Primera pista del #AMM con claves en los cuadros 1, 2... (la del hueso que gira).
Giro LeerGiro(const std::vector<uint8_t>& b) {
  Giro g;
  auto u32 = [&](size_t o) {
    uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
  };
  if (b.size() < 0x40 || std::memcmp(b.data(), "#AMM", 4) != 0) return g;
  constexpr float kVuelta = 6.2831853f / 65536.0f;
  for (size_t o = 0x20; o + 16 <= b.size(); o += 4) {
    if (u32(o + 4) != 1 || u32(o + 12) != 2 || !(u32(o) > 0 && u32(o) < u32(o + 8) && u32(o + 8) < 0x20000)) continue;
    g.claves.push_back({0, 0.0f});
    size_t p = o;
    for (; p + 8 <= b.size(); p += 8) {
      const uint32_t v = u32(p), fr = u32(p + 4);
      if (int(fr) <= g.claves.back().first || fr > 1000) break;
      g.claves.push_back({int(fr), float(v) * kVuelta});
    }
    g.cuadros = g.claves.back().first + 1;
    // cierre: el aro trae 32769 (media vuelta); sin el, una vuelta entera
    const uint32_t cierre = p + 4 <= b.size() ? u32(p) : 0;
    g.vuelta = cierre > 0x4000 && cierre <= 0x10000 ? float(cierre & ~1u) * kVuelta : 6.2831853f;
    break;
  }
  return g;
}

bool LeerAmo(const std::vector<uint8_t>& b, int variante, Malla& out, Esqueleto* esq, bool colores, bool material,
             uint32_t piel_rampas) {
  auto u32 = [&](size_t o) -> uint32_t {
    if (o + 4 > b.size()) return 0;
    uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
  };
  auto f32 = [&](size_t o) -> float {
    if (o + 4 > b.size()) return 0;
    float v;
    std::memcpy(&v, &b[o], 4);
    return v;
  };
  if (b.size() < 0x40 || std::memcmp(b.data(), "#AMO", 4) != 0) return false;
  const uint32_t tabla = u32(0x14), namg = u32(0x18), amgs = u32(0x1C);
  if (!namg) return false;
  const uint32_t a = u32(amgs + 4 * uint32_t(std::clamp(variante, 0, int(namg) - 1)));
  if (a + 0x20 > b.size() || std::memcmp(&b[a], "#AMG", 4) != 0) return false;
  const uint32_t nb = std::min<uint32_t>(u32(a + 0x10), 255), ejes = u32(a + 0x14);
  std::vector<M34> mundo(nb);
  std::vector<uint32_t> dirs;   // (con esq) direccion de cada vertice de cada triangulo, para casar los pesos
  int siguiente_parte = 0;
  if (esq) esq->padre.assign(nb, -1), esq->local.assign(nb, M34{});
  for (uint32_t i = 0; i < nb; ++i) {
    const size_t e = a + ejes + 80 * i;
    const float x = f32(e), y = f32(e + 4), z = f32(e + 8), w = f32(e + 12);
    const float sx = f32(e + 32), sy = f32(e + 36), sz = f32(e + 40);
    M34 l;
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int f = 0; f < 3; ++f) {
      l.m[f * 4 + 0] = r[f * 3 + 0] * sx;
      l.m[f * 4 + 1] = r[f * 3 + 1] * sy;
      l.m[f * 4 + 2] = r[f * 3 + 2] * sz;
    }
    l.m[3] = f32(e + 16);
    l.m[7] = f32(e + 20);
    l.m[11] = f32(e + 24);
    const uint32_t padre = u32(tabla + 32 * i + 0x10);
    const uint32_t ip = padre >= tabla ? (padre - tabla) / 32 : ~0u;
    mundo[i] = padre && ip < i ? mundo[ip] * l : l;
    if (esq) esq->padre[i] = padre && ip < i ? int(ip) : -1, esq->local[i] = l;
  }
  for (uint32_t i = 0; i < nb; ++i) {
    const uint32_t brazo = u32(a + ejes + 80 * i + 0x34);
    const uint32_t grupo = brazo ? u32(a + brazo + 4) : 0;
    if (!grupo) continue;
    const size_t g = a + grupo;
    const uint32_t n = std::min<uint32_t>(u32(g), 256), t = u32(g + 4);
    for (uint32_t k = 0; k < n; ++k) {
      const size_t po = a + grupo + u32(g + t + 4 * k);
      const uint32_t vtype = u32(po), tex = u32(po + 8), tam = u32(po + 0x90);
      const uint32_t rampa = u32(po + 0x0C);   // indice de su rampa (sombra) en el #AMT
      const bool piel = material && rampa < 32 && (piel_rampas >> rampa & 1);   // IW: piel gris (B2 la hornea: FFD2AA)
      if ((vtype & 0xFFFF) == 0xFFFF || tam < 0x60000000 || tam >= 0x70000000) continue;
      if (material)   // F1: sonda de las partes (Goku del mapa de IW)
        REXLOG_INFO("dbz3 ps2: parte hueso {} vtype 0x{:X} tex {} material {:.2f} {:.2f} {:.2f} {:.2f} | +0x20 {:.2f} {:.2f} "
                    "{:.2f} {:.2f} +0x40 {:.2f} {:.2f} {:.2f} {:.2f}", i, vtype, int32_t(tex), f32(po + 0x30), f32(po + 0x34),
                    f32(po + 0x38), f32(po + 0x3C), f32(po + 0x20), f32(po + 0x24), f32(po + 0x28), f32(po + 0x2C),
                    f32(po + 0x40), f32(po + 0x44), f32(po + 0x48), f32(po + 0x4C));
       const int parte_actual = siguiente_parte++;
       size_t pos = po + 0xA0;
      const size_t fin = std::min(b.size(), pos + size_t(tam - 0x60000000) * 16);
      while (pos + 0x20 <= fin) {
        const uint32_t vif = u32(pos + 0xC), ft = u32(pos + 0x10), cnt = u32(pos + 0x14), num = (vif >> 16) & 0xFF;
        if (cnt == 0 || cnt > 0xFFFF || ((vif >> 24) & 0x7F) != 0x6C || num < 1) break;
        const uint32_t paso = (num - 1) * 16 / cnt;
        const size_t vp = pos + 0x20;
        uint32_t esperado = 16 + (vtype & 1 ? 16 : 0) + (vtype & 2 ? 16 : 0) + (vtype & 4 ? 16 : 0);
        if (esperado != paso || vp + size_t(cnt) * paso > b.size()) break;
        std::vector<std::pair<V3, ImVec2>> vs(cnt);
        std::vector<V3> vn(cnt);          // normales (vtype & 1: xyz tras la posicion), giradas como el hueso
        std::vector<uint32_t> dv(cnt);   // direccion del vertice desde el #AMG (la que llevan los pesos)
        // sin textura (B2: el pelo de Goku, vtype 0x190, textura -1): el color de material de la parte (+0x30, RGBA)
        const bool sin_tex = colores && (!(vtype & 4) || tex == 0xFFFFFFFFu);
        auto mat8 = [&](size_t q2) { return int(std::clamp(f32(q2), 0.0f, 1.0f) * 255.0f + 0.5f); };
        const bool tinte = sin_tex || (colores && material && !(vtype & 2));   // F1: material sobre la textura
        std::vector<ImU32> vc(cnt, piel    ? IM_COL32(255, 210, 170, 255)
                                   : tinte ? IM_COL32(mat8(po + 0x30), mat8(po + 0x34), mat8(po + 0x38), mat8(po + 0x3C))
                                           : IM_COL32_WHITE);
        const bool con_color = colores && ((vtype & 2) || tinte || piel);
        for (uint32_t x = 0; x < cnt; ++x) {
          const size_t o = vp + size_t(x) * paso;
          const V3 p{f32(o), f32(o + 4), f32(o + 8)};
          size_t q = o + 16 + (vtype & 1 ? 16 : 0) + (vtype & 2 ? 16 : 0);
          const ImVec2 uv = vtype & 4 ? ImVec2(f32(q), f32(q + 4)) : ImVec2(0, 0);
          vs[x] = {mundo[i].por(p), uv};
          dv[x] = uint32_t(o - a);
          if (vtype & 1) vn[x] = Unitaria(mundo[i].gira(V3{f32(o + 16), f32(o + 20), f32(o + 24)}));
          if (con_color && (vtype & 2)) {   // RGBA en floats 0..1 tras la normal
            const size_t cc = o + 16 + (vtype & 1 ? 16 : 0);
            auto c8 = [&](size_t q2) { return int(std::clamp(f32(q2), 0.0f, 1.0f) * 255.0f + 0.5f); };
            vc[x] = IM_COL32(c8(cc), c8(cc + 4), c8(cc + 8), c8(cc + 12));
          }
        }
        auto tri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
          Malla::Tri tr;
          for (int c = 0; c < 3; ++c) {
            const uint32_t k = c == 0 ? i0 : c == 1 ? i1 : i2;
            tr.p[c] = vs[k].first;
            tr.uv[c] = vs[k].second;
            tr.col[c] = vc[k];
            tr.n[c] = vn[k];
            if (esq) dirs.push_back(dv[k]);
          }
           tr.tex = int(tex);
           tr.parte = parte_actual;
          // B2 (colores): el bit alto es mezcla con el alfa de la textura (la carta de las fichas del tablero), opaca; la
          // translucidez de B2 viene del alfa del color de vertice (nubes de la escena de Shenron)
          tr.trans = (vtype & 0x80000000u) != 0 && !colores;
          tr.con_color = con_color;
          tr.hueso[0] = tr.hueso[1] = tr.hueso[2] = uint8_t(i);
          out.tris.push_back(tr);
        };
        if (ft == 1) {
          for (uint32_t x = 0; x + 2 < cnt; ++x) x % 2 ? tri(x + 1, x, x + 2) : tri(x, x + 1, x + 2);
        } else {
          for (uint32_t x = 0; x + 2 < cnt; x += 3) tri(x, x + 1, x + 2);
        }
        pos = vp + size_t(cnt) * paso;
      }
    }
  }
  if (esq && !out.tris.empty()) {
    // Pesos del cuerpo (grupo del hueso 0, en mundo): cada hueso con malla pesada tiene en su eje +0x34 un registro
    // [hueso, 0, lista]; lista (desde el #AMG) = [0, 0, 0, n] + n x [peso f32, vertices, desplazamiento, 0]; en el
    // desplazamiento, por vertice 32 B [x y z en el espacio del hueso, destino u32, normal, 0], con destino = direccion
    // del vertice desde el #AMG (comprobado en Bubbles: las 2382 posiciones pesadas en reposo caen en sus vertices;
    // E:\DBZ3HD_trabajo\P4\pesos_amo2.py). Posicion = suma de peso x eje_animado x local, entre la suma de pesos.
    esq->inv_mundo.resize(nb);
    for (uint32_t i = 0; i < nb; ++i) esq->inv_mundo[i] = mundo[i].Inversa();
    std::unordered_map<uint32_t, int32_t> de;   // destino -> lista de pesos
    for (uint32_t i = 1; i < nb; ++i) {
      const uint32_t br = u32(a + ejes + 80 * i + 0x34), lst = br ? u32(a + br + 8) : 0;
      if (!lst) continue;
      const uint32_t n = std::min<uint32_t>(u32(a + lst + 12), 64);
      for (uint32_t e = 0; e < n; ++e) {
        // trozos de 32 B [peso, n32, loc32, n16, loc16, ...] (awo_tools/amo2awo.py: ch = s + 16 + 32 * c); de 16 en 16
        // se perdian la mitad de las listas y todas las de 16 B: vertices rigidos al hueso 0 y la ropa en rayas
        // (auditoria10/E: 1906 de 2598 destinos en el Goku de B2)
        const size_t en = a + lst + 16 + 32 * e;
        const float w = f32(en);
        const uint32_t nv = std::min<uint32_t>(u32(en + 4), 4096), off = u32(en + 8);
        const uint32_t n16 = std::min<uint32_t>(u32(en + 12), 4096), off16 = u32(en + 16);
        for (uint32_t k = 0; k < n16 && a + off16 + 16 * (k + 1) <= b.size(); ++k) {   // 16 B: xyz local + destino
          const size_t q = a + off16 + 16 * size_t(k);
          auto [it, nuevo] = de.try_emplace(u32(q + 12), int32_t(esq->pesos.size()));
          if (nuevo) esq->pesos.emplace_back();
          esq->pesos[size_t(it->second)].push_back({uint8_t(i), w, V3{f32(q), f32(q + 4), f32(q + 8)}, V3{}});
        }
        for (uint32_t k = 0; k < nv && a + off + 32 * (k + 1) <= b.size(); ++k) {
          const size_t q = a + off + 32 * size_t(k);
          auto [it, nuevo] = de.try_emplace(u32(q + 12), int32_t(esq->pesos.size()));
          if (nuevo) esq->pesos.emplace_back();
          esq->pesos[size_t(it->second)].push_back(
              {uint8_t(i), w, V3{f32(q), f32(q + 4), f32(q + 8)}, V3{f32(q + 16), f32(q + 20), f32(q + 24)}});
        }
      }
    }
    esq->pesos_de.assign(out.tris.size() * 3, -1);
    for (size_t t = 0; t < out.tris.size(); ++t)
      for (int c = 0; c < 3; ++c)
        if (out.tris[t].hueso[c] == 0 && t * 3 + size_t(c) < dirs.size())
          if (auto it = de.find(dirs[t * 3 + size_t(c)]); it != de.end()) esq->pesos_de[t * 3 + size_t(c)] = it->second;
    esq->crudo = out;
  }
  return !out.tris.empty();
}

// Animacion `anim` del #AMM de IW (PS2) para un esqueleto de `nb` huesos (mismo orden; el de Bubbles lo es).
Anim LeerAnim(const std::vector<uint8_t>& b, uint32_t anim, size_t nb) {
  Anim a;
  auto u32 = [&](size_t o) -> uint32_t {
    uint32_t v = 0;
    if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
    return v;
  };
  if (b.size() < 0x20 || std::memcmp(b.data(), "#AMM", 4) != 0 || anim >= u32(0x10) || u32(0x18) != nb) return a;
  const size_t e = u32(0x14) + 16 * size_t(anim);
  const uint32_t fl = u32(e), off = u32(e + 12), per = fl & 0x10 ? 3 : 2;
  a.cuadros = int(u32(e + 8));
  a.ang.resize(nb);
  a.pos.resize(nb);
  for (size_t j = 0; j < nb; ++j) {
    if (const uint32_t p = u32(off + 4 * (per * j)); p) {
      const uint32_t nk = std::min<uint32_t>(u32(p + 8), 4096);
      for (uint32_t k = 0; k < nk && p + 12 + 8 * (k + 1) <= b.size(); ++k) {
        std::array<uint16_t, 4> c;
        std::memcpy(c.data(), &b[p + 12 + 8 * k], 8);
        a.ang[j].push_back(c);
      }
    }
    if (const uint32_t p = u32(off + 4 * (per * j + 1)); p) {
      const uint32_t nk = std::min<uint32_t>(u32(p + 8), 4096);
      for (uint32_t k = 0; k < nk && p + 12 + 16 * (k + 1) <= b.size(); ++k) {
        std::array<float, 4> c;
        std::memcpy(c.data(), &b[p + 12 + 16 * k], 16);
        c[0] = float(u32(p + 12 + 16 * k));   // el cuadro es u32
        a.pos[j].push_back(c);
      }
    }
  }
  return a;
}

// Malla de `esq` en el cuadro `t` (segundos, en bucle a 60 por segundo) de `an`, con el mismo centro que Centrar en x/z
// (en la animacion el suelo ya es y = 0: la pista de BBR_WAIST lleva la altura).
void Posar(const Esqueleto& esq, const Anim& an, float t, Malla& out) {
  const size_t nb = esq.padre.size();
  // cuadro con fraccion: entre dos claves se interpola (antes se quedaba en la ultima clave y la pose iba a saltos;
  // auditoria7/G: Goku y las fichas del tablero de B2, Shenron, el Goku del mapa de IW)
  const float fc = std::fmod(t * 60.0f, float(std::max(an.cuadros, 1)));
  std::vector<M34> w(nb), piel(nb);
  for (size_t i = 0; i < nb; ++i) {
    M34 l = esq.local[i];
    if (i < an.ang.size() && !an.ang[i].empty()) {
      const auto& ks = an.ang[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && float(ks[ia + 1][0]) <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float fa = float(ks[ia][0]), fb = float(ks[ib][0]);
      const float u = fb > fa ? std::clamp((fc - fa) / (fb - fa), 0.0f, 1.0f) : 0.0f;
      auto ang = [&](int e) {   // u16 de 1/65536 de vuelta: por el camino corto
        const int d = int(int16_t(uint16_t(ks[ib][size_t(e)] - ks[ia][size_t(e)])));
        return (float(ks[ia][size_t(e)]) + u * float(d)) * 9.5873799e-5f;
      };
      const float x = ang(1), y = ang(2), z = ang(3);
      const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
      const float r[9] = {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,   // Rz Ry Rx
                          sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                          -sy,     cy * sx,                cy * cx};
      if (an.relativa && !an.absoluta) {   // B2: giro de reposo por el de la clave
        const M34& rep = esq.local[i];
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k)
            l.m[f * 4 + k] = rep.m[f * 4] * r[k] + rep.m[f * 4 + 1] * r[3 + k] + rep.m[f * 4 + 2] * r[6 + k];
      } else {
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = r[f * 3 + k];
      }
    }
    if (i < an.pos.size() && !an.pos[i].empty()) {
      const auto& ks = an.pos[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && ks[ia + 1][0] <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float u = ks[ib][0] > ks[ia][0] ? std::clamp((fc - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0.0f, 1.0f) : 0.0f;
      float c[4];
      for (int e = 1; e < 4; ++e) c[e] = ks[ia][size_t(e)] + u * (ks[ib][size_t(e)] - ks[ia][size_t(e)]);
      if (an.relativa && !an.absoluta) {   // B2: la posicion de reposo mas la de la clave
        l.m[3] = esq.local[i].m[3] + c[1], l.m[7] = esq.local[i].m[7] + c[2], l.m[11] = esq.local[i].m[11] + c[3];
      } else {
        l.m[3] = c[1], l.m[7] = c[2], l.m[11] = c[3];
      }
    }
    w[i] = esq.padre[i] >= 0 ? w[size_t(esq.padre[i])] * l : l;
    piel[i] = w[i] * esq.inv_mundo[i];
  }
  out.tris = esq.crudo.tris;
  for (size_t t = 0; t < out.tris.size(); ++t) {
    Malla::Tri& tr = out.tris[t];
    for (int c = 0; c < 3; ++c) {
      V3 p, nn;   // posicion y normal (la normal, con el giro del mismo eje)
      const int32_t lp = t * 3 + size_t(c) < esq.pesos_de.size() ? esq.pesos_de[t * 3 + size_t(c)] : -1;
      const M34& rig = piel[tr.hueso[c] < nb ? tr.hueso[c] : 0];
      if (lp >= 0) {   // pesado: suma de peso x eje_animado x local, entre la suma de pesos
        float sw = 0;
        for (const Esqueleto::Peso& q : esq.pesos[size_t(lp)]) {
          const M34& eje = w[q.hueso < nb ? q.hueso : 0];
          const V3 v = eje.por(q.local), vn = eje.gira(q.normal);
          p = {p.x + v.x * q.w, p.y + v.y * q.w, p.z + v.z * q.w};
          nn = {nn.x + vn.x * q.w, nn.y + vn.y * q.w, nn.z + vn.z * q.w};
          sw += q.w;
        }
        if (sw > 1e-6f && sw < 0.999f) {   // peso parcial: el resto, al padre del hueso, en su reposo (auditoria10/E)
          const int pa = esq.padre[esq.pesos[size_t(lp)][0].hueso < nb ? esq.pesos[size_t(lp)][0].hueso : 0];
          const M34& rp = piel[pa >= 0 && size_t(pa) < nb ? size_t(pa) : 0];
          const V3 r = rp.por(tr.p[c]), rn = rp.gira(tr.n[c]);
          const float k = 1.0f - sw;
          p = {p.x + r.x * k, p.y + r.y * k, p.z + r.z * k};
          nn = {nn.x + rn.x * k, nn.y + rn.y * k, nn.z + rn.z * k};
        } else if (sw > 1e-6f) {
          p = {p.x / sw, p.y / sw, p.z / sw};
        } else if (an.relativa) {   // B2 (frente R2): lista sin peso: rigido a su hueso (si no, iba al origen: picos)
          p = rig.por(tr.p[c]);
          nn = rig.gira(tr.n[c]);
        }
      } else {
        p = rig.por(tr.p[c]);
        nn = rig.gira(tr.n[c]);
      }
      if (nn.x == 0 && nn.y == 0 && nn.z == 0) nn = rig.gira(tr.n[c]);   // los de 16 B no traen normal
      tr.p[c] = {p.x - esq.centro.x, p.y, p.z - esq.centro.z};
      tr.n[c] = Unitaria(nn);
    }
  }
}
// Pone el origen en el centro de la base del modelo (centro x/z, y minima): asi se coloca en el suelo de su punto.
void Centrar(Malla& m) {
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, y0 = 1e9f;
  for (const auto& t : m.tris)
    for (const V3& p : t.p) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z),
                            y0 = std::min(y0, p.y);
  const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
  for (auto& t : m.tris)
    for (V3& p : t.p) p = {p.x - cx, p.y - y0, p.z - cz};
}
// #AMO del hijo `amo` de un #AMB y sus texturas (hijo `amt`).
uint32_t RampasPiel(const std::vector<uint8_t>& amt) {
  uint32_t m = 0;
  uint32_t idx = 0;
  if (amt.size() > 0x18) std::memcpy(&idx, &amt[0x14], 4);
  for (int j = 0, n = std::min(P::CuantasAmt(amt), 32); j < n; ++j) {
    uint32_t e = 0, fl = 0;
    if (size_t(idx) + 4 * size_t(j) + 4 <= amt.size()) std::memcpy(&e, &amt[idx + 4 * size_t(j)], 4);
    if (size_t(e) + 8 <= amt.size()) std::memcpy(&fl, &amt[e + 4], 4);
    if (!(fl & 0x80000000u)) continue;   // solo las rampas
    const P::Imagen im = P::TexturaAmt(amt, j);
    double r = 0, bl = 0;
    size_t px = 0;
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4, ++px) r += im.rgba[i], bl += im.rgba[i + 2];
    if (px && (bl - r) / double(px) >= 40.0) m |= 1u << j;   // azulada = la de la piel (piel 87-98, el resto < 5)
  }
  return m ? m : 1u << 3;
}

// #AMO y #AMT sueltos (entradas propias de DATA_CMN: personajes de IW como Bubbles 118/119 o Piccolo 573/574).
Modelo CargarSuelto(const std::vector<uint8_t>& amo, const std::vector<uint8_t>& amt, bool animable, bool colores,
                    bool material) {
  Modelo m;
  if (animable) m.esq = std::make_shared<Esqueleto>();
  if (!LeerAmo(amo, 0, m.malla, m.esq.get(), colores, material, material ? RampasPiel(amt) : 0)) return m;
  if (m.esq) {   // el mismo centro x/z que Centrar
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (const auto& t : m.malla.tris)
      for (const V3& p : t.p) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
    m.esq->centro = {(x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f};
  }
  Centrar(m.malla);
  for (int j = 0, n = P::CuantasAmt(amt); j < n; ++j) {
    P::Imagen im = P::TexturaAmt(amt, j);
    uint32_t idx = 0, e = 0, fl = 0x80000000u;
    if (amt.size() > 0x18) {
      std::memcpy(&idx, &amt[0x14], 4);
      if (size_t(idx) + 4 * size_t(j) + 4 <= amt.size()) std::memcpy(&e, &amt[idx + 4 * size_t(j)], 4);
      if (size_t(e) + 8 <= amt.size()) std::memcpy(&fl, &amt[e + 4], 4);
    }
    if (colores && !(fl & 0x80000000u))
      for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
    m.tex.push_back(std::move(im));
  }
  return m;
}
Modelo CargarModelo(const std::vector<uint8_t>& amb, uint32_t amo, uint32_t amt, int variante) {
  Modelo m;
  if (!LeerAmo(P::HijoAmb(amb, amo), variante, m.malla)) return m;
  Centrar(m.malla);
  const std::vector<uint8_t> t = P::HijoAmb(amb, amt);
  for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) m.tex.push_back(P::TexturaAmt(t, j));
  m.giro = LeerGiro(P::HijoAmb(amb, amo + 1));   // el #AMM va justo despues del #AMO (887/2, 887/6)
  return m;
}

Modelo CargarDeAfs(pasarela::AfsPs2& afs, size_t amo, size_t amt, bool animable) {
  return CargarSuelto(afs.Entrada(amo), afs.Entrada(amt), animable);
}

// ---- Pintado (hilo de la interfaz) ----
Camara CamaraCombate(uint8_t* base, ImVec2 pantalla) {
  Camara c;
  ImVec2 p0(0, 0);
  const ImGuiIO& io = ImGui::GetIO();
  if (pantalla.x == io.DisplaySize.x && pantalla.y == io.DisplaySize.y) {
    const dbz3::ingame::style::Imagen gi = dbz3::ingame::style::GuestImage(io);   // L05: el 3D, en la imagen del juego
    pantalla = gi.Size();
    p0 = gi.p0;
  }
  const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(0x823D6CF4)) : 0;
  if (!mgr) return c;
  for (uint32_t i = 0; i < 16; ++i) c.m[i] = LoadF(base, mgr + 0x60 + 4 * i);   // vista (mgr+0x60)
  const float fov = LoadF(base, mgr + 0x1C4);                                    // fov vertical
  if (fov > 0.1f && fov < 3.0f) c.fov = fov;
  const float cerca = LoadF(base, mgr + 0x20), lejos = LoadF(base, mgr + 0x24);   // planos (Camara::Prof)
  if (cerca > 0.01f && lejos > cerca * 2.0f && lejos < 1.0e7f) c.za = cerca, c.zb = lejos;
  c.c = ImVec2(p0.x + pantalla.x * 0.5f, p0.y + pantalla.y * 0.5f);
  c.h = ImVec2(pantalla.x * 0.5f, pantalla.y * 0.5f);
  c.ok = true;
  return c;
}

// R15: ImmediateTextureFilter::kDbz3LinearDepth del SDK (rexglue-sdk-0.10/include/rex/ui/immediate_drawer.h): lineal y
// probada contra el z-buffer del juego; sus vertices llevan u = dos mitades (u, v) y v = la profundidad.
constexpr auto kFiltroProf = static_cast<rex::ui::ImmediateTextureFilter>(2);
int EstadoProfundidad() {
  static const bool apagado = std::getenv("DBZ3_SIN_ZBUF") != nullptr;   // pruebas: comparar sin el z-buffer
  static int cuadro = -1, estado = 0;
  const int f = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -2;
  if (!apagado && f != cuadro) {
    cuadro = f;
    estado = std::atoi(rex::cvar::GetFlagByName("dbz3_ui_depth_estado").c_str());
  }
  return apagado ? 0 : estado;
}

Tex Crear(const P::Imagen& im, bool prof) {
  Tex x;
  auto* dr = dbz3::launcher::PreviewDrawer();
  if (!im || !dr) return x;
  // Texturas con el alfa de la CLUT a 0 en todos los pixeles (las de cuerpo de Bubbles, DATA_CMN 119 t00/t03/t05/t06):
  // la PS2 las pinta sin alfa (solo RGB); con el alfa tal cual saldrian invisibles. Se dan opacas.
  bool sin_alfa = true;
  for (size_t i = 3; i < im.rgba.size() && sin_alfa; i += 4) sin_alfa = im.rgba[i] == 0;
  std::vector<uint8_t> opaca;
  if (sin_alfa) {
    opaca = im.rgba;
    for (size_t i = 3; i < opaca.size(); i += 4) opaca[i] = 255;
  }
  // repetidas, como el GS de la PS2 por defecto (CLAMP = REPEAT): las UV de los modelos de B2 salen de [0, 1] (el
  // cuerpo de Shenron, v hasta -4.9) y sujetas a la orilla se veian de un color liso (frente R2)
  x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), prof ? kFiltroProf : rex::ui::ImmediateTextureFilter::kLinear,
                          true, sin_alfa ? opaca.data() : im.rgba.data());
  x.w = float(im.w);
  x.h = float(im.h);
  x.prof = prof && x.t;
  return x;
}
std::vector<Tex> Texturas(const Modelo& m) {
  std::vector<Tex> v;
  const bool prof = EstadoProfundidad() >= 1;
  for (const P::Imagen& im : m.tex) v.push_back(Crear(im, prof));
  return v;
}

// Modelo 3D de IW en `pos` (girado `giro` en vertical): sus triangulos, proyectados, a la cola de este cuadro.
void Escena::Poner(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro,
                   float esc) {
  PonerT(cam, malla, tx, pos, giro, esc, nullptr);
}
void Escena::Poner(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro,
                   float esc, const Toon& toon) {
  PonerT(cam, malla, tx, pos, giro, esc, &toon);
}
void Escena::PonerT(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro,
                    float esc, const Toon* toon) {
  const float cg = std::cos(giro), sg = std::sin(giro);
  auto amundo = [&](const V3& v) {
    return V3{pos.x + (v.x * cg + v.z * sg) * esc, pos.y + v.y * esc, pos.z + (-v.x * sg + v.z * cg) * esc};
  };
  auto gira = [&](const V3& n) { return V3{n.x * cg + n.z * sg, n.y, -n.x * sg + n.z * cg}; };
  // normal en el espacio de la vista (filas de la vista del combate: (m0 m4 m8), (m1 m5 m9), (m2 m6 m10); delante -z)
  auto vista = [&](const V3& n) {
    return V3{cam.m[0] * n.x + cam.m[4] * n.y + cam.m[8] * n.z, cam.m[1] * n.x + cam.m[5] * n.y + cam.m[9] * n.z,
              cam.m[2] * n.x + cam.m[6] * n.y + cam.m[10] * n.z};
  };
  constexpr V3 kLuz{-0.4512f, 0.5514f, 0.7018f};   // luz de B3 en vista (la de los iconos, memoria b3hd-toon-render)
  auto tonado = [](ImU32 c, float f) {
    auto ch = [&](int s) { return ImU32(std::clamp(float((c >> s) & 0xFF) * f, 0.0f, 255.0f)) << s; };
    return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
  };
  // R15: los triangulos sin textura (y el borde toon) se prueban contra el z-buffer con una textura blanca
  const bool prof_sin_tex = cam.za > 0 && EstadoProfundidad() >= 1;
  for (const Malla::Tri& tr : malla.tris) {
    Tri tp;
    bool ok = true;
    float ks = 0;
    // DS179: centro en mundo del triangulo (media de sus 3 vertices ya colocados). Lo usa la prueba de tapado para
    // exigir que el triangulo este DENTRO de la esfera del luchador en 3D, no solo dentro de su circulo en pantalla.
    V3 wm{};
    for (int c = 0; c < 3 && ok; ++c) {
      float k;
      const V3 w = amundo(tr.p[c]);
      wm.x += w.x, wm.y += w.y, wm.z += w.z;
      ok = cam.P(w, tp.p[c], k);
      ks += k;
      tp.uv[c] = tr.uv[c];
      tp.z[c] = ok ? cam.Prof(k) : -1.0f;
    }
    // detras de la camara: Camara::P ya corta en el plano cercano (z de vista > -1). Antes tambien se quitaba lo que daba
    // mas de 45 px por unidad (a menos de ~19 de la camara del combate): el disco recien lanzado (R13) no se veia.
    if (!ok) continue;
    tp.w = {wm.x / 3.0f, wm.y / 3.0f, wm.z / 3.0f};
    tp.k = ks / 3.0f;
    tp.tex = tr.tex >= 0 && tr.tex < int(tx.size()) && tx[size_t(tr.tex)] ? tx[size_t(tr.tex)].id() : ImTextureID{};
    tp.prof = tp.tex ? tx[size_t(tr.tex)].prof : prof_sin_tex;
    tp.tapable = tapable_;   // DS179: la aureola (parte del luchador) no la tapa el propio luchador
    tp.prioritario = !tapable_;   // ... ni se tira cuando la lista de dibujo va llena
    tp.col = tr.trans ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE;
    tp.vcol = tr.con_color;
    for (int c = 0; c < 3; ++c) tp.cols[c] = tr.col[c];
    if (toon) {
      // toon: 3 tonos por vertice segun n.l en vista; y el borde: si la cara mira hacia atras (normal media en vista con
      // z < 0), una copia empujada `borde` por las normales, negra y un poco mas lejos (se pinta antes)
      V3 nv[3];
      float zm = 0;
      bool normales = true;
      for (int c = 0; c < 3; ++c) {
        const bool con_n = tr.n[c].x != 0 || tr.n[c].y != 0 || tr.n[c].z != 0;
        normales = normales && con_n;
        nv[c] = vista(gira(tr.n[c]));
        zm += nv[c].z;
        const float d = nv[c].x * kLuz.x + nv[c].y * kLuz.y + nv[c].z * kLuz.z;
        // sin normal (aros, monedas y nave de IW: vtype 0x36/0xB6, sin el bit 1): su color, sin tono ni borde
        const float f = !con_n ? 1.0f : d > toon->cortes[0] ? toon->tonos[0] : d > toon->cortes[1] ? toon->tonos[1] : toon->tonos[2];
        tp.cols[c] = tonado(tr.con_color ? tr.col[c] : tp.col, f);
      }
      tp.vcol = true;
      if (normales && zm < 0 && !tr.trans && toon->borde > 0) {
        Tri b;
        bool okb = true;
        for (int c = 0; c < 3 && okb; ++c) {
          const V3 n = gira(tr.n[c]), w = amundo(tr.p[c]);
          float k;
          okb = cam.P(V3{w.x + n.x * toon->borde * esc, w.y + n.y * toon->borde * esc, w.z + n.z * toon->borde * esc},
                      b.p[c], k);
          b.z[c] = okb ? cam.Prof(k) : -1.0f;
        }
        // por el mismo camino que su triangulo: con camara sin profundidad (za = 0, p. ej. la de R2 para Shenron) el borde
        // iba con AddTriangleFilled entre triangulos de vertices empaquetados (R15) y salian abanicos por toda la pantalla
        b.prof = tp.prof || prof_sin_tex;
        if (okb) {
          b.k = tp.k * 0.995f;
          b.w = tp.w;   // DS179: el borde pertenece al mismo triangulo (misma prueba de tapado en 3D)
          b.tapable = tp.tapable;
          b.prioritario = tp.prioritario;
          b.borde = true;
          b.vcol = true;
          b.cols[0] = b.cols[1] = b.cols[2] = IM_COL32(0, 0, 0, 255);
          tris_.push_back(b);
        }
      }
      // rampa de B3: el triangulo cortado por los cambios de tramo, cada trozo con la textura cocida de su tramo
      const auto* tramos = (toon->rampa && normales && !tr.trans && tr.tex >= 0 && size_t(tr.tex) < toon->rampa->size())
                               ? &(*toon->rampa)[size_t(tr.tex)] : nullptr;
      if (tramos && tramos->size() > 1) {
        struct Pt { ImVec2 p, uv; float z, l; };
        Pt v[3];
        for (int c = 0; c < 3; ++c) {
          const float d = nv[c].x * kLuz.x + nv[c].y * kLuz.y + nv[c].z * kLuz.z;
          v[c] = {tp.p[c], tp.uv[c], tp.z[c], 0.5f + 0.5f * d};
        }
        auto mezcla = [](const Pt& a, const Pt& b, float t) {
          return Pt{ImVec2(a.p.x + (b.p.x - a.p.x) * t, a.p.y + (b.p.y - a.p.y) * t),
                    ImVec2(a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t), a.z + (b.z - a.z) * t,
                    a.l + (b.l - a.l) * t};
        };
        // recorte de un poligono por l >= x (arriba = true) o l < x
        auto corta = [&](const std::vector<Pt>& in, float x, bool arriba) {
          std::vector<Pt> out;
          for (size_t i = 0; i < in.size(); ++i) {
            const Pt& a = in[i];
            const Pt& b = in[(i + 1) % in.size()];
            const bool da = arriba ? a.l >= x : a.l < x, db = arriba ? b.l >= x : b.l < x;
            if (da) out.push_back(a);
            if (da != db && std::fabs(b.l - a.l) > 1e-9f) out.push_back(mezcla(a, b, (x - a.l) / (b.l - a.l)));
          }
          return out;
        };
        for (size_t s = 0; s < tramos->size(); ++s) {
          const int ti = (*tramos)[s].second;
          std::vector<Pt> poli(v, v + 3);
          if (s > 0) poli = corta(poli, (*tramos)[s].first, true);
          if (s + 1 < tramos->size() && poli.size() >= 3) poli = corta(poli, (*tramos)[s + 1].first, false);
          if (poli.size() < 3) continue;
          Tri q = tp;
          q.tex = ti >= 0 && ti < int(tx.size()) && tx[size_t(ti)] ? tx[size_t(ti)].id() : tp.tex;
          q.vcol = false;
          for (size_t k = 1; k + 1 < poli.size(); ++k) {
            const Pt* tri3[3] = {&poli[0], &poli[k], &poli[k + 1]};
            for (int c = 0; c < 3; ++c) q.p[c] = tri3[c]->p, q.uv[c] = tri3[c]->uv, q.z[c] = tri3[c]->z;
            tris_.push_back(q);
          }
        }
        continue;
      }
    }
    tris_.push_back(tp);
  }
}
// Oclusion por el luchador: sus 22 esferas de cuerpo (las de los golpes del juego: centros X+1616 + 16 i, radios
// [[X+2996] + 24 + 4 i], gigantes.cpp). DS179: se guarda tambien la esfera en MUNDO y la prueba exige que el
// triangulo este dentro de ella en 3D, no solo dentro de su circulo en pantalla: con solo el circulo, un objeto
// pegado al borde (la aureola, a 4,19 del centro de la cabeza con radio 3,2) caia dentro del 30 % de mas y se
// borraba entero segun el cuadro. ponytail: sin el z-buffer del juego el escenario de B3 no tapa los modelos.
void Escena::Tapar(const Camara& cam, uint8_t* base, uint32_t x) {
  tapas_.clear();
  const uint32_t rt = REX_LOAD_U32(x + 2996);
  for (uint32_t i = 0; i < 22; ++i) {
    const uint32_t a = x + 1616 + 16 * i;
    const V3 c{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
    const float r = rt ? LoadF(base, rt + 24 + 4 * i) : 0.0f;
    Tapa tp;
    if (r <= 0.0f || r > 20.0f || !cam.P(c, tp.c, tp.k)) continue;
    tp.r = r * 1.3f * tp.k;   // circulo en pantalla: un poco mas (el pelo y la ropa salen de las esferas)
    // DS179: la esfera en MUNDO va con su radio DE VERDAD: el 1,3 de arriba es solo el alcance en pantalla. Con el
    // 1,3 tambien en 3D, la esfera del cuello (3,5 -> 4,55) llegaba a la aureola (4,19 sobre la cabeza) y la borraba;
    // con el radio real, lo que se tapa esta dentro del cuerpo del luchador y no al lado.
    tp.c3 = c, tp.r3 = r;
    tapas_.push_back(tp);
  }
}
// R15: media precision (IEEE 754 binary16) para empaquetar las UV; sin subnormales (|x| < 6e-5 = 0) ni infinitos.
static uint16_t Media(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  const uint32_t s = (x >> 16) & 0x8000u, m = x & 0x7FFFFFu;
  const int e = int((x >> 23) & 0xFF) - 127 + 15;
  if (e <= 0) return uint16_t(s);
  if (e >= 31) return uint16_t(s | 0x7BFFu);
  const uint32_t h = (uint32_t(e) << 10 | m >> 13) + ((m >> 12) & 1u);   // redondeo al mas cercano
  return uint16_t(s | std::min(h, 0x7BFFu));
}
// u = las dos mitades (u abajo, v arriba), v = la profundidad del juego (dbz3_ui_depth.vs del SDK)
static ImVec2 Empaquetar(ImVec2 uv, float z) {
  const uint32_t p = uint32_t(Media(uv.x)) | (uint32_t(Media(uv.y)) << 16);
  float u;
  std::memcpy(&u, &p, 4);
  return ImVec2(u, z);
}
void Escena::Pintar(ImDrawList* dl) {
  // Indices de ImGui de 16 bits: una lista de dibujo pasada de 65535 vertices da la vuelta y une vertices que no son
  // (abanicos enormes por toda la pantalla: el Shenron de B2 con el borde toon, 11325 triangulos + bordes, medido).
  // Si no caben, fuera primero los bordes; y si aun no, los mas lejanos.
  const size_t cabe = (65535u - std::min<size_t>(dl->_VtxCurrentIdx, 65535u)) / 3;
  const size_t puestos = tris_.size();
  if (tris_.size() > cabe) {
    tris_.erase(std::remove_if(tris_.begin(), tris_.end(), [](const Tri& t) { return t.borde; }), tris_.end());
    static int avisos = 0;
    if (avisos < 3) {
      ++avisos;
      REXLOG_WARN("dbz3 ps2: {} triangulos no caben en la lista de dibujo (caben {}): sin borde toon", tris_.size(), cabe);
    }
  }
  std::stable_sort(tris_.begin(), tris_.end(), [](const Tri& a, const Tri& b) { return a.k < b.k; });
  if (tris_.size() > cabe) {
    // DS179: antes se tiraban los mas lejanos sin mirar; la aureola (mitad del cuadro) caia en ese saco si la escena
    // venia cargada. Ahora se respeta `prioritario` (lo puesto con Tapable(false)): primero se tira lo demas.
    size_t sobran = tris_.size() - cabe;
    std::vector<char> fuera(tris_.size(), 0);
    for (size_t i = 0; i < tris_.size() && sobran; ++i)
      if (!tris_[i].prioritario) fuera[i] = 1, --sobran;
    for (size_t i = 0; i < tris_.size() && sobran; ++i)   // si aun no cabe, tambien los prioritarios
      if (!fuera[i]) fuera[i] = 1, --sobran;
    size_t j = 0;
    for (size_t i = 0; i < tris_.size(); ++i)
      if (!fuera[i]) tris_[j++] = tris_[i];
    tris_.resize(j);
  }
  descartados_ = puestos - tris_.size();   // DS179: los que se quedan fuera por el tope de la lista
  static Tex blanca;                            // 1x1 blanca para los triangulos sin textura con profundidad
  bool pedir = false;
  for (const Tri& tp : tris_) {
    const bool probado = tp.prof && tp.z[0] >= 0;
    pedir |= probado;
    const float cx = (tp.p[0].x + tp.p[1].x + tp.p[2].x) / 3.0f, cy = (tp.p[0].y + tp.p[1].y + tp.p[2].y) / 3.0f;
    bool tapado = false;
    // las esferas siguen (coordinador, 2026-10-16: respaldo hasta probar el z-buffer en todos los modos); con el z-buffer
    // solo sobran alrededor del luchador. DS179: ademas del circulo en pantalla, el triangulo tiene que estar DENTRO de
    // la esfera en 3D: un objeto pegado por fuera de la cabeza (la aureola) caia dentro del circulo y desaparecia.
    static const bool sin_esferas = std::getenv("DBZ3_ZBUF_SIN_ESFERAS") != nullptr;   // prueba: solo el z-buffer
    if (tp.tapable && !(sin_esferas && probado))
    for (const Tapa& t : tapas_)
      if (tp.k < t.k && (cx - t.c.x) * (cx - t.c.x) + (cy - t.c.y) * (cy - t.c.y) < t.r * t.r &&
          (tp.w.x - t.c3.x) * (tp.w.x - t.c3.x) + (tp.w.y - t.c3.y) * (tp.w.y - t.c3.y) +
                  (tp.w.z - t.c3.z) * (tp.w.z - t.c3.z) <
              t.r3 * t.r3)
        tapado = true;
    if (tapado) {
      ++ocultos_;
      continue;
    }
    ++pintados_;
    if (tp.prof) {
      if (!tp.tex && !blanca) {
        P::Imagen im;
        im.w = im.h = 1;
        im.rgba = {255, 255, 255, 255};
        blanca = Crear(im, true);
      }
      const ImTextureID id = tp.tex ? tp.tex : blanca.id();
      if (id) {
        dl->PushTextureID(id);
        dl->PrimReserve(3, 3);
        for (int c = 0; c < 3; ++c) {
          const ImU32 col = !tp.tex ? (tp.vcol ? tp.cols[0] : IM_COL32(200, 200, 200, 255)) : tp.vcol ? tp.cols[c] : tp.col;
          dl->PrimVtx(tp.p[c], Empaquetar(tp.tex ? tp.uv[c] : ImVec2(0.5f, 0.5f), tp.z[c]), col);
        }
        dl->PopTextureID();
        continue;
      }
    }
    if (!tp.tex) {
      dl->AddTriangleFilled(tp.p[0], tp.p[1], tp.p[2], tp.vcol ? tp.cols[0] : IM_COL32(200, 200, 200, 255));
      continue;
    }
    dl->PushTextureID(tp.tex);
    dl->PrimReserve(3, 3);
    for (int c = 0; c < 3; ++c) dl->PrimVtx(tp.p[c], tp.uv[c], tp.vcol ? tp.cols[c] : tp.col);
    dl->PopTextureID();
  }
  // El SDK copia el z-buffer de cada cuadro solo desde que alguien lo pide. ponytail: se queda pedido toda la sesion
  // (una copia de la profundidad por cuadro, barata); apagarlo al salir del minijuego si algun dia cuenta.
  // Sonda (DBZ3_ZBUF_TRAZA=1): cada 2 s, estado y profundidad de un vertice de un triangulo probado (frente R15).
  static const bool traza = std::getenv("DBZ3_ZBUF_TRAZA") != nullptr;
  static int vez = 0;
  if (traza && ++vez % 120 == 0) {
    size_t np = 0, nz = 0;
    for (const Tri& tp : tris_) np += tp.prof, nz += tp.prof && tp.z[0] >= 0;
    REXLOG_INFO("ps2: zbuf estado {} ('{}') triangulos {} con prof {} con z {}", EstadoProfundidad(),
                rex::cvar::GetFlagByName("dbz3_ui_depth_estado"), tris_.size(), np, nz);
    for (const Tapa& t : tapas_)   // esferas del luchador: centro en pantalla y px por unidad (d = f / k)
      REXLOG_INFO("ps2: zbuf luchador ({:.0f}, {:.0f}) k {:.3f}", t.c.x, t.c.y, t.k);
  }
  if (traza && vez % 120 == 0)
    for (const Tri& tp : tris_)
      if (tp.prof && tp.z[0] >= 0) {
        REXLOG_INFO("ps2: zbuf estado {} vertice ({:.0f}, {:.0f}) z {:.6f} k {:.2f}", EstadoProfundidad(), tp.p[0].x,
                    tp.p[0].y, tp.z[0], tp.k);
        break;
      }
  static const bool sonda_pantalla = std::getenv("DBZ3_ZBUF_DEBUG") != nullptr;   // con el SDK en modo sonda
  if (sonda_pantalla && !blanca) {
    P::Imagen im;
    im.w = im.h = 1;
    im.rgba = {255, 255, 255, 255};
    blanca = Crear(im, true);
  }
  if (sonda_pantalla && blanca) {   // toda la pantalla: el color es la profundidad de la escena (r + g / 255)
    const ImVec2 t = ImGui::GetIO().DisplaySize;
    const ImVec2 q[4] = {{0, 0}, {t.x, 0}, {t.x, t.y}, {0, t.y}};
    dl->PushTextureID(blanca.id());
    dl->PrimReserve(6, 6);
    for (int i : {0, 1, 2, 0, 2, 3}) dl->PrimVtx(q[i], Empaquetar(ImVec2(0.5f, 0.5f), 0.0f), IM_COL32_WHITE);
    dl->PopTextureID();
  }
  static bool pedido = false;
  if (pedir && !pedido) pedido = rex::cvar::SetFlagByName("dbz3_ui_depth", "true");
}

}  // namespace dbz3::ps2
