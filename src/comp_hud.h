// R16 COMP: presentacion del marcador, sin modificar reglas ni memoria del combate.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace dbz3::comp::hud {
// Hilo de interfaz, junto a los demas dialogos persistentes del combate.
#if defined(DBZ3_EU_VARIANT)
inline void Crear() {}  // El build EU puro no enlaza los ganchos US.
#else
void Crear();
#endif

// Coordenadas originales IW, antes de escalar al rectangulo presentado.
// Sin dependencias del renderer: se cotejan con la emision del interprete IW.
namespace geometria {
struct Vertice { float x, y, u, v; };
inline std::array<Vertice, 4> Segmento(int lado, int bloque, float fraccion, bool relleno) {
  const float f = std::clamp(fraccion, 0.0f, 1.0f);
  const float x = lado ? 511.0f - 11.0f * bloque : 79.0f + 11.0f * bloque;
  const float x0 = x + (relleno ? 1.0f : 0.0f), x1 = x + (relleno ? 8.0f : 9.0f);
  const float y = relleno ? 74.5f : 75.0f;
  const float alto = relleno ? float(int(20.0f * f)) : 21.0f;
  const float sesgo = (lado ? -6.0f : 6.0f) * (relleno ? f : 1.0f);
  return {{{x0, y, 0, alto}, {x1, y, 9, alto},
           {x1 + sesgo, y - alto, 9, 0}, {x0 + sesgo, y - alto, 0, 0}}};
}
inline std::array<Vertice, 6> Fatiga(int lado, float valor, float x, float y) {
  // El estado visual original contiene un entero, no los bits de un float.
  const float f = std::floor(std::clamp(valor, 0.0f, 1000.0f));
  std::array<Vertice, 6> v = {{{5, 0, 5, 0.5f}, {0, 8, 0, 8}, {18, 14, 18, 14},
                              {41, 14, 41, 14}, {60, 6, 60, 6}, {56, 0, 56, 0}}};
  const int n = f < 333.3f ? 0 : f < 666.6f ? 1 : 2;
  const float t = (f - (n == 0 ? 0.0f : n == 1 ? 333.3f : 666.6f)) / 333.3f;
  const float fx = v[n + 1].x + (v[n + 2].x - v[n + 1].x) * t;
  const float fy = v[n + 1].y + (v[n + 2].y - v[n + 1].y) * t;
  for (int j = n + 2; j < 5; ++j) v[j] = {fx, fy, fx, fy};
  const float fin = 5.0f + (f / 1000.0f) * 51.0f;
  v[5] = {fin, 0, fin, 0};
  for (auto& p : v) {
    p.x = lado ? 599.0f - x - p.x : x + p.x;
    p.y += y;
  }
  return v;
}
}  // namespace geometria
}  // namespace dbz3::comp::hud
