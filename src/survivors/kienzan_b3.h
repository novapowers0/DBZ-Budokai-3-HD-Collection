// SUR2: lector acotado al Kienzan normal del BSP 539 de B3 HD (US/EU).
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace dbz3::survivors::kienzan {

inline constexpr int kBsp = 539;
using Bytes = std::span<const uint8_t>;
struct Punto { float x, y; };
struct Vertice { float x, y, z, u, v; };
struct Material { uint32_t textura, rampa, flags, flags2; };
struct Tramo { unsigned vertice, vertices, indice, indices; };
inline constexpr std::array<Tramo, 2> kTramos{{{0, 120, 0, 288}, {120, 4, 288, 6}}};

struct Malla {
  std::array<Vertice, 124> v{};
  std::array<uint16_t, 294> i{};
  std::array<Material, 2> mat{};
  float radio = 0;
};
struct Fuente {
  Malla malla;
  std::array<Bytes, 2> dds;  // prestadas del BSP; subir antes de liberar los bytes
};

inline bool Elegible(bool malla, bool texturas, bool disco, bool hostil, std::string_view id) {
  return malla && texturas && disco && !hostil && id == "kienzan";
}

namespace detalle {
inline uint32_t U(Bytes b, size_t p) {
  return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | uint32_t(b[p + 3]);
}
inline float F(Bytes b, size_t p) { return std::bit_cast<float>(U(b, p)); }
inline bool Cabe(Bytes b, size_t p, size_t n) { return p <= b.size() && n <= b.size() - p; }
inline bool Magic(Bytes b, const char* m) { return Cabe(b, 0, 4) && std::memcmp(b.data(), m, 4) == 0; }
inline Bytes Hijo(Bytes b, unsigned i) {
  if (!Magic(b, "#AMB") || !Cabe(b, 0, 0x18) || i >= U(b, 0x10)) return {};
  const size_t t = U(b, 0x14);
  if (!Cabe(b, t, size_t(i + 1) * 16)) return {};
  const size_t p = U(b, t + 16 * i), n = U(b, t + 16 * i + 4);
  return Cabe(b, p, n) ? b.subspan(p, n) : Bytes{};
}
// Huella de compatibilidad, no autenticacion. Los offsets fijos solo se usan para esta variante.
inline bool Coincide(Bytes b, size_t n, uint64_t hash) {
  if (b.size() != n) return false;
  uint64_t h = 14695981039346656037ull;
  for (const auto c : b) h = (h ^ c) * 1099511628211ull;
  return h == hash;
}
inline Bytes Textura(Bytes z, unsigned id) {
  if (!Magic(z, "#AZT") || !Cabe(z, 0, 0x18) || id >= U(z, 0x10) || id > 1) return {};
  const size_t t = U(z, 0x14);
  if (!Cabe(z, t, size_t(id + 1) * 4)) return {};
  const size_t d = U(z, t + 4 * id);
  if (!d || !Cabe(z, d, 28) || U(z, d) != id) return {};
  const size_t p = U(z, d + 20), n = U(z, d + 24);
  return Cabe(z, p, n) ? z.subspan(p, n) : Bytes{};
}

using Matriz = std::array<std::array<float, 4>, 3>;
inline Matriz Local(Bytes b, size_t o) {
  const float x = F(b, o), y = F(b, o + 4), z = F(b, o + 8), w = F(b, o + 12);
  return {{{1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w), F(b, o + 16)},
           {2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w), F(b, o + 20)},
           {2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y), F(b, o + 24)}}};
}
inline Matriz Componer(const Matriz& padre, const Matriz& local) {
  Matriz m{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 4; ++j) {
      m[i][j] = j == 3 ? padre[i][3] : 0;
      for (int k = 0; k < 3; ++k) m[i][j] += padre[i][k] * local[k][j];
    }
  return m;
}
}  // namespace detalle

inline std::optional<Fuente> Leer(Bytes bsp) {
  using namespace detalle;
  const Bytes awo = Hijo(bsp, 6), ace = Hijo(Hijo(bsp, 0), 4), azt = Hijo(bsp, 2);
  if (!Coincide(ace, 0x230, 0x9a2c4daad6b8fc4dull) || !Coincide(awo, 0x4120, 0x4d86c221d704479cull)) return {};

  constexpr size_t awg = 0x740, ejes = 0xba0, vb = 0x27c0, ib = 0x3d10, mats = 0xb00;
  Fuente out{};
  auto& m = out.malla;
  for (unsigned k = 0; k < m.mat.size(); ++k) {
    const size_t p = mats + k * 0x50;
    m.mat[k] = {U(awo, p + 0x30), U(awo, p + 0x34), U(awo, p + 0x38), U(awo, p + 0x3c)};
    out.dds[k] = Textura(azt, m.mat[k].textura);
  }
  if (!Coincide(out.dds[0], 4224, 0x105ff7fd2fb257d6ull) || !Coincide(out.dds[1], 1152, 0x4263a0468421197eull)) return {};

  std::array<Matriz, 28> mundo{};
  unsigned siguiente_v = 0, siguiente_i = 0;
  for (unsigned hueso = 0; hueso < mundo.size(); ++hueso) {
    const size_t o = ejes + 80 * hueso;
    const auto local = Local(awo, o);
    const size_t padre = U(awo, o + 0x40);
    if (padre) {
      if (awg + padre < ejes || awg + padre >= o || (awg + padre - ejes) % 80) return {};
      mundo[hueso] = Componer(mundo[(awg + padre - ejes) / 80], local);
    } else {
      mundo[hueso] = local;
    }
    const size_t arm = U(awo, o + 0x34);
    if (!arm) continue;
    if (!Cabe(awo, awg + arm, 8)) return {};
    const size_t rel = U(awo, awg + arm + 4);
    if (!rel) continue;
    const size_t d = awg + rel;
    if (!Cabe(awo, d, 0x60) || U(awo, d) != 1 || U(awo, d + 0x30) != 4) return {};
    const unsigned mat = U(awo, d + 0x24) >> 16;
    if (mat >= kTramos.size()) return {};
    const auto lote = kTramos[mat];
    const unsigned a = U(awo, d + 0x38), nv = U(awo, d + 0x3c);
    const unsigned b = U(awo, d + 0x40), nt = U(awo, d + 0x44);
    if (a != siguiente_v || b != siguiente_i || a < lote.vertice || b < lote.indice ||
        a >= lote.vertice + lote.vertices || b >= lote.indice + lote.indices ||
        !nv || nv > lote.vertice + lote.vertices - a || !nt || nt > (lote.indice + lote.indices - b) / 3) return {};
    for (unsigned j = a; j < a + nv; ++j) {
      const size_t p = vb + 44 * j;
      const auto& w = mundo[hueso];  // el slot 0 de la ventana no es el hueso del draw rigido
      const auto x = F(awo, p), y = F(awo, p + 4), z = F(awo, p + 8);
      const auto f = [&](int k) { return w[k][0] * x + w[k][1] * y + w[k][2] * z + w[k][3]; };
      m.v[j] = {f(0), f(1), f(2), F(awo, p + 36), F(awo, p + 40)};
      m.radio = std::max(m.radio, std::hypot(m.v[j].x, m.v[j].z));
    }
    for (unsigned k = b; k < b + 3 * nt; ++k) {
      const size_t p = ib + 2 * k;
      const auto j = uint16_t(uint16_t(awo[p]) << 8 | awo[p + 1]);
      if (j < a || j >= a + nv) return {};
      m.i[k] = j;
    }
    siguiente_v += nv;
    siguiente_i += 3 * nt;
  }
  if (siguiente_v != m.v.size() || siguiente_i != m.i.size() || !std::isfinite(m.radio) || m.radio <= 0) return {};
  return out;
}

using Proyeccion = std::array<Punto, 124>;
inline std::optional<Proyeccion> Proyectar(const Malla& m, Punto centro, float r, float sp, float cp, float ang,
                                          Punto clip_min, Punto clip_max) {
  if (!std::isfinite(r) || r <= 0 || !std::isfinite(ang) || !std::isfinite(m.radio) || m.radio <= 0 ||
      !(clip_min.x < clip_max.x && clip_min.y < clip_max.y)) return {};
  const float c = std::cos(ang), s = std::sin(ang), escala = 1.1f * r / m.radio;
  Proyeccion p{};
  Punto min = clip_max, max = clip_min;
  for (unsigned k = 0; k < p.size(); ++k) {
    const auto& v = m.v[k];
    p[k] = {centro.x + escala * (c * v.x + s * v.z),
            centro.y + escala * (sp * (-s * v.x + c * v.z) - cp * v.y)};
    if (!std::isfinite(p[k].x) || !std::isfinite(p[k].y)) return {};
    min.x = std::min(min.x, p[k].x); min.y = std::min(min.y, p[k].y);
    max.x = std::max(max.x, p[k].x); max.y = std::max(max.y, p[k].y);
  }
  if (max.x <= clip_min.x || max.y <= clip_min.y || min.x >= clip_max.x || min.y >= clip_max.y) return {};
  for (const auto lote : kTramos) {
    bool area = false;
    for (unsigned i = lote.indice; i < lote.indice + lote.indices; i += 3) {
      const auto a = p[m.i[i]], b = p[m.i[i + 1]], c0 = p[m.i[i + 2]];
      const float doble = (b.x - a.x) * (c0.y - a.y) - (b.y - a.y) * (c0.x - a.x);
      area |= std::isfinite(doble) && std::abs(doble) >= .01f;
    }
    if (!area) return {};
  }
  return p;
}

}  // namespace dbz3::survivors::kienzan
