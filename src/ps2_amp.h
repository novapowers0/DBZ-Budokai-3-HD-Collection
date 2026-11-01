// #AMP de B2: estado discreto por nombre de hueso, no emisor de particulas.
// EU144D60 enlaza nombres; EU144E60 escribe el valor short; EU13F0F0 elige
// la ultima clave <= cuadro. El llamador decide el reloj y el bucle del clip.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dbz3::ps2 {
struct Amp {
  struct Clave { int16_t cuadro, valor; };
  uint32_t cuadros = 0;
  std::vector<std::vector<Clave>> huesos;
  explicit operator bool() const { return cuadros != 0 && !huesos.empty(); }
  // Una pista ausente conserva el estado del consumidor (EU144E60).
  bool Estado(size_t hueso, float cuadro, int16_t& valor) const {
    if (hueso >= huesos.size() || huesos[hueso].empty() || !std::isfinite(cuadro)) return false;
    const auto& p = huesos[hueso];
    auto it = std::upper_bound(p.begin(), p.end(), cuadro,
        [](float f, const Clave& c) { return f < float(c.cuadro); });
    valor = (it == p.begin() ? p.front() : *--it).valor;
    return true;
  }
};

inline Amp LeerAmp(const std::vector<uint8_t>& b, const std::vector<uint8_t>& amo, uint32_t clip = 0) {
  auto cabe = [](const auto& d, size_t o, size_t n) { return o <= d.size() && n <= d.size() - o; };
  auto u = [](const auto& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o+1]) << 8 | uint32_t(d[o+2]) << 16 | uint32_t(d[o+3]) << 24;
  };
  auto nombre = [](const auto& d, size_t o) {
    size_t n = 0;
    while (n < 32 && d[o+n]) ++n;
    return std::string(reinterpret_cast<const char*>(d.data()+o), n);
  };
  if (!cabe(b, 0, 32) || !cabe(amo, 0, 40) || std::memcmp(b.data(), "#AMP", 4) ||
      std::memcmp(amo.data(), "#AMO", 4) || clip >= u(b,16)) return {};
  const size_t nc = u(b,16), tc = u(b,20), np = u(b,24), tn = u(b,28);
  const size_t nh = u(amo,16), nombres = u(amo,36);
  // Limites por bytes antes de multiplicar: no se reservan tablas desde un dato truncado.
  if (nc > b.size()/16 || np > b.size()/32 || nh > amo.size()/32 ||
      !cabe(b,tc,nc*16) || !cabe(b,tn,np*32) || !cabe(amo,nombres,nh*32)) return {};
  const size_t c = tc + size_t(clip)*16, tabla = u(b,c+12);
  if (!cabe(b,tabla,np*4) || !u(b,c+8)) return {};
  std::vector<std::vector<Amp::Clave>> pistas(np);
  for (size_t i = 0; i < np; ++i) {
    const size_t p = u(b,tabla+i*4);
    if (!p) continue;
    if (!cabe(b,p,12)) return {};
    const size_t n = u(b,p+8);
    if (!n || n > b.size()/4 || !cabe(b,p+12,n*4)) return {};
    for (size_t j = 0; j < n; ++j) {
      const size_t k = p+12+j*4;
      const auto f = int16_t(uint16_t(b[k]) | uint16_t(b[k+1]) << 8);
      const auto v = int16_t(uint16_t(b[k+2]) | uint16_t(b[k+3]) << 8);
      if (f < 0 || (!pistas[i].empty() && f < pistas[i].back().cuadro)) return {};
      pistas[i].push_back({f,v});
    }
  }
  Amp out;
  out.cuadros = u(b,c+8);
  out.huesos.resize(nh);
  for (size_t i = 0; i < nh; ++i)
    for (size_t j = 0; j < np; ++j)
      if (nombre(amo,nombres+i*32) == nombre(b,tn+j*32)) {
        out.huesos[i] = pistas[j];
        break;
      }
  return out;
}
}  // namespace dbz3::ps2
