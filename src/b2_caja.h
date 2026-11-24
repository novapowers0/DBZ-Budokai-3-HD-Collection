// Lista B de PP_FIXBX: operaciones de FUN_001a8200, contrastadas con el ELF PAL.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dbz3::b2caja {
// VU0 redondea hacia cero. El doble conserva el producto/suma de dos float;
// se recorta CADA operacion, sin fusionar el vmul y el vmadd.
inline float Chop(double x) {
  float f = float(x);
  if (std::abs(double(f)) > std::abs(x)) f = std::nextafter(f, 0.0f);
  return std::abs(f) < 0x1p-126f ? std::copysign(0.0f, f) : f;
}
inline float Color(float a, float b, float t) {
  const float uno = Chop(1.0 - double(t));
  const float acc = Chop(double(a) * uno);
  const float mezcla = Chop(double(acc) + Chop(double(b) * t));
  const float delta = Chop(double(mezcla) - a);
  return Chop(double(a) + Chop(double(delta) * t));
}
struct Muestra {
  size_t clave = 0;
  std::array<float, 4> rgba{1, 1, 1, 1};
};
template<class Lista>
Muestra Evaluar(const Lista& claves, float cuadro, float duracion, uint32_t flags) {
  Muestra out;
  if (claves.empty()) return out;
  float t = 0;
  if (cuadro > 0) {
    const float fin = std::max(0.0f, duracion - 1.0f);
    if (claves.size() == 1 || (cuadro >= fin && !(flags & 4))) out.clave = claves.size() - 1;
    else {
      if (fin > 0) while (cuadro > fin) cuadro = Chop(double(cuadro) - fin);
      // Igual que el original: busca con int(cuadro) y retrocede si el float
      // aun queda antes de esa clave. No adelantar 0,2 cuadros una transicion.
      while (out.clave + 1 < claves.size() && claves[out.clave].cuadro < int(cuadro)) ++out.clave;
      if (cuadro < claves[out.clave].cuadro && out.clave) --out.clave;
      const float resto = Chop(double(cuadro) - claves[out.clave].cuadro);
      if (resto != 0 && out.clave + 1 < claves.size())
        // div.s del perfil R5900: vecino par; VU0 sigue truncando despues.
        t = resto / (claves[out.clave + 1].cuadro - claves[out.clave].cuadro);
    }
  }
  const auto& a = claves[out.clave];
  out.rgba = {a.r, a.g, a.b, a.a};
  if (t != 0) {
    const auto& b = claves[out.clave + 1];
    const float v[] = {b.r, b.g, b.b, b.a};
    for (size_t i = 0; i < 4; ++i) out.rgba[i] = Color(out.rgba[i], v[i], t);
  }
  return out;
}
inline uint32_t RgbaGs(const std::array<float, 4>& c) {
  uint32_t out = 0;
  for (int i = 0; i < 4; ++i) out |= uint32_t(std::clamp(int(Chop(double(c[i]) * 128.0)), 0, 255)) << (8 * i);
  return out;
}
} // namespace dbz3::b2caja
