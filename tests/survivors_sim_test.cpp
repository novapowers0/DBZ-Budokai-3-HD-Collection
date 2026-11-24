// Dragon Budokai Survivors: pruebas de la simulacion (src/survivors/sim.cpp), sin juego ni graficos. Devuelve 0 si todo pasa.
// Uso: survivors_sim_test   (o a mano: clang++ -O2 -std=c++17 -I. tests/survivors_sim_test.cpp src/survivors/sim.cpp)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "src/survivors/sim.h"

using dbz3::survivors::Sim;

static int g_fallos = 0, g_comprobaciones = 0;
#define CHECK(c) do { ++g_comprobaciones; if (!(c)) { std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fallos; } } while (0)

// el jugador anda en circulo (como el modo automatico de la prueba del juego)
static void Andar(Sim& s, int pasos) {
  for (int k = 0; k < pasos; ++k) {
    const float a = float(s.pasos) * Sim::kDt * 0.5f;
    s.Paso(std::cos(a), std::sin(a));
  }
}

int main() {
  // 1) determinismo: misma semilla -> mismo estado tras 600 pasos (y cambiando N por el camino)
  {
    auto a = std::make_unique<Sim>(), b = std::make_unique<Sim>();
    a->Iniciar(1234, 300), b->Iniciar(1234, 300);
    Andar(*a, 300), Andar(*b, 300);
    a->PonerN(600), b->PonerN(600);
    Andar(*a, 300), Andar(*b, 300);
    CHECK(a->Hash() == b->Hash());
    auto c = std::make_unique<Sim>();
    c->Iniciar(4321, 300), Andar(*c, 300), c->PonerN(600), Andar(*c, 300);
    CHECK(c->Hash() != a->Hash());
    std::printf("determinismo: hash %016llx\n", (unsigned long long)a->Hash());
  }
  // 2) solape: jugador quieto, 600 enemigos, 1200 pasos para que se amontonen; ningun par pisado mas de la tolerancia
  {
    auto s = std::make_unique<Sim>();
    s->Iniciar(77, 600);
    for (int k = 0; k < 1200; ++k) s->Paso(0, 0);
    const float dmin = 2 * Sim::kRadio, tol = 0.45f * dmin;   // medido: ~0,37 en unos pocos pares del centro del monton (el resto, casi nada)
    float peor = 0;
    for (int i = 0; i < s->n; ++i)
      for (int j = i + 1; j < s->n; ++j) {
        const float d = std::hypot(s->x[i] - s->x[j], s->y[i] - s->y[j]);
        if (dmin - d > peor) peor = dmin - d;
      }
    std::printf("solape: peor %.3f (tolerancia %.3f), contactos con el jugador %llu\n", peor, tol, (unsigned long long)s->contactos);
    CHECK(peor <= tol);
    CHECK(s->contactos > 0);
  }
  // 3) tiempo medio por paso
  for (int n : {100, 300, 600, 1000, 2000}) {
    auto s = std::make_unique<Sim>();
    s->Iniciar(9, n);
    Andar(*s, 600);   // que se formen las nubes alrededor del jugador
    uint64_t pares = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int pasos = 1200;
    for (int k = 0; k < pasos; ++k) { Andar(*s, 1); pares += s->pares; }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / pasos;
    std::printf("N=%d  %.4f ms/paso  pares/paso %llu\n", n, ms, (unsigned long long)(pares / pasos));
    CHECK(ms < 4.0);
  }
  std::printf("%d comprobaciones, %d fallos\n", g_comprobaciones, g_fallos);
  return g_fallos ? 1 : 0;
}
