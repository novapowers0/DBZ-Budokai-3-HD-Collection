// iw_amc.h - UN reproductor de camara #AMC de Infinite World (dia 1 / equipo A, 6/11).
// Original (SLES-55347 EU): FUN_00127280 (muestreo; Ghidra lo llama "UiAnim_Start" por error, es la camara) y
// FUN_00127160 (la pone en la camara: matriz de vista). Sustituye a las 4 copias de iw_minijuegos.cpp,
// iw_mini_botones.cpp, iw_mini_vuelo.cpp e iw_mini_disparo.cpp.
//
// Formato (docs/03_formatos/CAMARA_ACC.md): cabecera +0x10 = n clips, +0x14 = tabla de 16 B por clip
// {flags, ?, cuadros, offset}; el clip trae 4 punteros a pistas: 0 ojo, 1 objetivo, 2 giro, 3 fov. Cada pista:
// +8 = n claves; claves desde +12: 16 B {cuadro, x, y, z} (0/1) o 8 B {cuadro, valor} (2/3).
//
// FUN_00127280(f, salida, #AMC, clip), reglas literales:
//  - si f >= cuadros-1: ojo/objetivo/giro/fov = ULTIMA clave de cada pista (el clip "acaba" y se queda ahi);
//  - si no: la clave i es la ultima con cuadro <= f (f < 1 -> 0) y se interpola LINEAL con la clave i+1 con
//    t = (f - cuadro_i) / (cuadro_i+1 - cuadro_i) SOLO si cuadro_i != f (sin recorte de t);
//  - el fov solo se escribe si el flag 0x10 del clip esta puesto, y se recorta a [0.08726647, 3.0543263] (5 a 175 grados)
//    antes y despues de interpolar. El giro (roll) no se recorta;
//  - ojo y objetivo son posiciones absolutas del mundo del clip (w = 1). FUN_00127160 las usa para la vista: la
//    matriz de vista es R * (p - ojo), es decir ojo = -t * R^T (la traslacion de la vista es -R * ojo).
// Bucle y "fin de clip" NO son del #AMC: los lleva quien lo llama (cuadro modulo cuadros, o sujetar el ultimo cuadro).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace dbz3::iwamc {

constexpr float kFovMin = 0.08726647f, kFovMax = 3.0543263f;   // 5 y 175 grados (FUN_00127280)
constexpr uint32_t kFlagFov = 0x10;                             // el clip trae pista de fov

struct Clip {
  int nf = 0;                                    // cuadros del clip (tabla +8)
  uint32_t flags = 0;                            // tabla +0
  std::vector<std::array<float, 4>> ojo, obj;    // {cuadro, x, y, z}
  std::vector<std::array<float, 2>> roll, fov;   // {cuadro, radianes}
};

struct Pose {
  float ojo[3] = {0, 0, 0}, obj[3] = {0, 0, 0};
  float roll = 0, fov = 0;
  bool hay_roll = false, hay_fov = false;
};

inline uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
  return v;
}
inline float F32(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t v = U32(b, o);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

// Numero de clips del #AMC (0 si no lo es).
inline uint32_t NumClips(const std::vector<uint8_t>& amc) {
  if (amc.size() < 0x20 || std::memcmp(amc.data(), "#AMC", 4) != 0) return 0;
  return U32(amc, 0x10);
}

// Lee el clip `id`. false si no existe / esta vacio (sin cuadros o sin pistas de ojo y objetivo).
inline bool Leer(const std::vector<uint8_t>& amc, int id, Clip& c) {
  const uint32_t n = NumClips(amc), tab = U32(amc, 0x14);
  if (id < 0 || uint32_t(id) >= n) return false;
  const size_t e = size_t(tab) + 16 * size_t(id);
  c = Clip{};
  c.flags = U32(amc, e);
  c.nf = int(U32(amc, e + 8));
  const size_t off = U32(amc, e + 12);
  if (c.nf <= 0 || off + 16 > amc.size()) return false;
  for (int k = 0; k < 4; ++k) {
    const size_t t = U32(amc, off + 4 * size_t(k));
    if (t + 12 > amc.size()) continue;
    const uint32_t nk = U32(amc, t + 8);
    if (nk == 0 || nk > 5000) continue;
    for (uint32_t i = 0; i < nk; ++i) {
      if (k < 2) {
        const size_t o = t + 12 + 16 * size_t(i);
        if (o + 16 > amc.size()) break;
        (k == 0 ? c.ojo : c.obj).push_back({float(U32(amc, o)), F32(amc, o + 4), F32(amc, o + 8), F32(amc, o + 12)});
      } else {
        const size_t o = t + 12 + 8 * size_t(i);
        if (o + 8 > amc.size()) break;
        (k == 2 ? c.roll : c.fov).push_back({float(U32(amc, o)), F32(amc, o + 4)});
      }
    }
  }
  return !c.ojo.empty() && !c.obj.empty();
}

// Interpola una pista {cuadro, v...} en el cuadro f (parte "si no" de FUN_00127280, sin el caso del final).
template <size_t N>
inline void Pista(const std::vector<std::array<float, N>>& k, float f, float* out) {
  if (k.empty()) return;
  size_t i = 0;
  if (f >= 1.0f)
    while (i + 1 < k.size() && k[i + 1][0] <= f) ++i;
  for (size_t c = 1; c < N; ++c) out[c - 1] = k[i][c];
  if (k[i][0] == f || i + 1 >= k.size()) return;
  const float d = k[i + 1][0] - k[i][0];
  if (d == 0.0f) return;
  const float t = (f - k[i][0]) / d;
  for (size_t c = 1; c < N; ++c) out[c - 1] = k[i][c] + (k[i + 1][c] - k[i][c]) * t;
}
template <size_t N>
inline void Ultima(const std::vector<std::array<float, N>>& k, float* out) {
  if (k.empty()) return;
  for (size_t c = 1; c < N; ++c) out[c - 1] = k.back()[c];
}
inline float RecortaFov(float v) { return std::min(std::max(v, kFovMin), kFovMax); }

// FUN_00127280: la camara del clip en el cuadro f.
inline Pose Muestrea(const Clip& c, float f) {
  Pose p;
  const bool fin = f >= float(c.nf - 1);
  if (fin) {
    Ultima(c.ojo, p.ojo);
    Ultima(c.obj, p.obj);
    if (!c.roll.empty()) Ultima(c.roll, &p.roll);
  } else {
    Pista(c.ojo, f, p.ojo);
    Pista(c.obj, f, p.obj);
    if (!c.roll.empty()) Pista(c.roll, f, &p.roll);
  }
  p.hay_roll = !c.roll.empty();
  if ((c.flags & kFlagFov) && !c.fov.empty()) {
    if (fin) Ultima(c.fov, &p.fov);
    else Pista(c.fov, f, &p.fov);
    p.fov = RecortaFov(p.fov);
    p.hay_fov = true;
  }
  return p;
}

// Cuadro que el original usa con un contador t: en bucle vuelve a 0 al acabar, si no sujeta el ultimo cuadro.
inline float CuadroDe(const Clip& c, float t, bool bucle) {
  if (bucle && c.nf > 1) {
    float m = t - float(c.nf) * float(int(t / float(c.nf)));
    return m < 0 ? m + float(c.nf) : m;
  }
  return std::min(t, float(std::max(c.nf - 1, 0)));
}

}  // namespace dbz3::iwamc
