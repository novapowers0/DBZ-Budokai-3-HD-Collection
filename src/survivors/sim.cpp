// Dragon Budokai Survivors: simulacion (ver sim.h).
#include "sim.h"

#include <cmath>
#include <cstring>

namespace dbz3::survivors {

uint32_t Sim::Azar() {   // xorshift64*
  rng_ ^= rng_ >> 12, rng_ ^= rng_ << 25, rng_ ^= rng_ >> 27;
  return uint32_t((rng_ * 0x2545F4914F6CDD1DULL) >> 32);
}

void Sim::Aparecer(int i) {
  const float a = Azar01() * 6.2831853f, r = kAnilloMin + (kAnilloMax - kAnilloMin) * Azar01();
  x[i] = jx + std::cos(a) * r, y[i] = jy + std::sin(a) * r;
  fase[i] = Azar01();
  tapado_[i] = 0;
}

void Sim::Iniciar(uint64_t semilla, int nn) {
  rng_ = semilla ? semilla : 0x9E3779B97F4A7C15ULL;
  jx = jy = 0, n = 0, pasos = pares = contactos = 0;
  PonerN(nn);
}

void Sim::PonerN(int nn) {
  nn = nn < 0 ? 0 : nn > kMax ? kMax : nn;
  for (int i = n; i < nn; ++i) Aparecer(i), rad[i] = kRadio, vel[i] = kVelEnemigo;
  n = nn;
}

void Sim::Quitar(int i) {
  if (i < 0 || i >= n) return;
  --n;
  x[i] = x[n], y[i] = y[n], fase[i] = fase[n], rad[i] = rad[n], vel[i] = vel[n], tapado_[i] = tapado_[n];
}

void Sim::Paso(float dx, float dy) {
  const float l = std::sqrt(dx * dx + dy * dy);
  if (l > 1.0f) dx /= l, dy /= l;
  jx += dx * kVelJugador * kDt, jy += dy * kVelJugador * kDt;

  // 1) perseguir (se paran al tocar al jugador) y reaparecer los que se quedan lejos. El que en el paso anterior pisaba a uno que
  // tiene delante (mas cerca del jugador) no avanza: sin esto la presion crece capa a capa y en el centro del monton se pisan enteros.
  rmax_ = 0;
  for (int i = 0; i < n; ++i) {
    rmax_ = rad[i] > rmax_ ? rad[i] : rmax_;
    const float toque = rad[i] + kRadioJugador;
    const float vx = jx - x[i], vy = jy - y[i], d2 = vx * vx + vy * vy;
    if (d2 > kLejos * kLejos) { Aparecer(i); continue; }
    if (d2 > toque * toque && !tapado_[i]) {
      const float k = vel[i] * kDt / std::sqrt(d2);
      x[i] += vx * k, y[i] += vy * k;
    }
  }

  // 2) rejilla: celda de cada uno, conteo, prefijos y reparto
  const float x0 = jx - kLado * celda * 0.5f, y0 = jy - kLado * celda * 0.5f;
  gx_ = x0, gy_ = y0;
  std::memset(inicio_, 0, sizeof inicio_);
  for (int i = 0; i < n; ++i) {
    const int cx = int(std::floor((x[i] - x0) / celda)), cy = int(std::floor((y[i] - y0) / celda));
    celda_[i] = (cx < 0 || cy < 0 || cx >= kLado || cy >= kLado) ? -1 : cy * kLado + cx;
    if (celda_[i] >= 0) ++inicio_[celda_[i] + 1];
  }
  for (int c = 0; c < kLado * kLado; ++c) inicio_[c + 1] += inicio_[c];
  {
    int lleno[kLado * kLado];   // ponytail: 16 KB en pila por paso; a un miembro si se sube kLado
    std::memcpy(lleno, inicio_, sizeof lleno);
    for (int i = 0; i < n; ++i)
      if (celda_[i] >= 0) orden_[lleno[celda_[i]]++] = i;
  }

  // 3) separacion (Jacobi: cada uno acumula su empuje y se aplica al final -> no depende del orden) y contacto con el jugador
  pares = contactos = 0;
  for (int i = 0; i < n; ++i) {
    ex_[i] = ey_[i] = 0, tapado_[i] = 0;
    const int c = celda_[i];
    if (c < 0) continue;
    const int cx = c % kLado, cy = c / kLado;
    for (int oy = cy - 1; oy <= cy + 1; ++oy) {
      if (oy < 0 || oy >= kLado) continue;
      for (int ox = cx - 1; ox <= cx + 1; ++ox) {
        if (ox < 0 || ox >= kLado) continue;
        const int cc = oy * kLado + ox;
        for (int k = inicio_[cc]; k < inicio_[cc + 1]; ++k) {
          const int j = orden_[k];
          if (j == i) continue;
          ++pares;   // cada par se mira dos veces (una desde cada uno)
          const float vx = x[i] - x[j], vy = y[i] - y[j];
          const float d2 = vx * vx + vy * vy, dmin = rad[i] + rad[j];
          if (d2 >= dmin * dmin) continue;
          const float d = std::sqrt(d2);
          if (d < 1e-5f) { ex_[i] += (i < j ? 0.5f : -0.5f) * dmin; continue; }   // encima: se separan en x segun el indice
          const float e = (dmin - d) * 0.5f / d;
          ex_[i] += vx * e, ey_[i] += vy * e;
          if (vx * (x[i] - jx) + vy * (y[i] - jy) > 0) tapado_[i] = 1;   // j esta entre i y el jugador
        }
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    // contacto con el jugador: sale entero de el (como un vecino que no se mueve), sumado al empuje de los demas
    const float toque = rad[i] + kRadioJugador;
    const float vx = x[i] - jx, vy = y[i] - jy, d2 = vx * vx + vy * vy;
    if (d2 < toque * toque) {
      ++contactos;
      const float d = std::sqrt(d2);
      if (d > 1e-5f) ex_[i] += vx * (toque - d) / d, ey_[i] += vy * (toque - d) / d;
      else ex_[i] += toque;
    }
    x[i] += ex_[i], y[i] += ey_[i];
  }
  ++pasos;
}

uint64_t Sim::Hash() const {
  uint64_t h = 1469598103934665603ULL;
  auto mete = [&](const void* p, size_t b) {
    const auto* c = static_cast<const uint8_t*>(p);
    for (size_t k = 0; k < b; ++k) h = (h ^ c[k]) * 1099511628211ULL;
  };
  mete(&n, sizeof n), mete(&jx, sizeof jx), mete(&jy, sizeof jy);
  mete(x, sizeof(float) * size_t(n)), mete(y, sizeof(float) * size_t(n)), mete(fase, sizeof(float) * size_t(n));
  return h;
}

}  // namespace dbz3::survivors
