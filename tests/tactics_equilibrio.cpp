// Dragon Tactics H5: equilibrio. Mide, con bots contra bots (sin juego ni graficos), la tasa de victoria por unidad, por sinergia y por coste, y la duracion
// de la partida. Devuelve 0 si ninguna unidad (en igualdad de coste) ni sinergia queda fuera de 40-60 % y la partida dura 20-30 min estimados.
// Uso: tactics_equilibrio [carpeta "mod center hd"] [duelos de equipos (20000)] [torneos (1500)]   (desde la raiz del repo)
//  A) duelos de equipos de 3 unidades de 1 estrella con el MISMO coste total, sin sinergias: cada unidad frente a unidades de su misma potencia economica.
//  B) torneos de 8 bots de la misma calidad (los 8 al maximo): victorias por unidad en el tablero, por sinergia activa y por coste, y rondas de la partida.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/tactics/partida.h"

using namespace dbz3::tactics;

namespace {
constexpr int kColumnasCentro[6] = {2, 3, 1, 4, 0, 5};
constexpr double kPrepSegundos = 25.0, kResultadoSegundos = 5.0;   // supuestos de tiempo humano por ronda: preparacion y letrero de resultado

struct Cuenta {
  double gana = 0, total = 0;
  void Sumar(int ganador, int yo) { total += 1, gana += ganador < 0 ? 0.5 : ganador == yo ? 1.0 : 0.0; }
  double Pct() const { return total ? 100.0 * gana / total : 50.0; }
};

// Coloca como el bot: cuerpo a cuerpo delante (centro primero), a distancia detras.
void Poner(sim::Sim& s, const Catalogo& cat, const Economia& eco, const std::vector<int>& unidades, int equipo) {
  const auto def = [&](int u) -> const sim::DefUnidad& { return cat.unidades[size_t(eco.unidades[size_t(u)].cat)]; };
  std::vector<int> cuerpo, dist;
  for (int u : unidades) (def(u).alcance <= 1 ? cuerpo : dist).push_back(u);
  std::vector<int> usadas;
  const auto cuadros = [](bool delante) {
    std::vector<int> v;
    for (int k = 0; k < 3; ++k) for (int c : kColumnasCentro) v.push_back((delante ? k : 2 - k) * 6 + c);
    return v;
  };
  const auto colocar = [&](const std::vector<int>& us, bool delante) {
    for (int u : us)
      for (int c : cuadros(delante))
        if (std::find(usadas.begin(), usadas.end(), c) == usadas.end()) {
          usadas.push_back(c);
          s.Anadir(def(u), equipo, {equipo == 0 ? c % 6 : 5 - c % 6, equipo == 0 ? 3 + c / 6 : 2 - c / 6});
          break;
        }
  };
  colocar(cuerpo, true);
  colocar(dist, false);
}

int Combate(const Catalogo& cat, const Economia& eco, const std::vector<int>& a, const std::vector<int>& b, uint64_t semilla) {
  sim::Sim s(semilla);
  Poner(s, cat, eco, a, 0);
  Poner(s, cat, eco, b, 1);
  while (!s.Terminada()) s.Paso();
  return s.Resumen().ganador;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string datos = argc > 1 ? argv[1] : "mod center hd";
  const int duelos = argc > 2 ? std::atoi(argv[2]) : 20000, torneos = argc > 3 ? std::atoi(argv[3]) : 1500;
  Catalogo cat;
  Economia eco;
  std::string err;
  if (!CargarCatalogo(datos + "/tactics_units.toml", datos + "/tactics_manifest.toml", &cat, &err) || !CargarEconomia(datos + "/tactics_economia.toml", cat, &eco, &err)) {
    std::printf("no se pudieron cargar los datos:\n%s", err.c_str());
    return 2;
  }
  const int U = int(eco.unidades.size());
  int fallos = 0;
  const auto fuera = [&](double pct) { return pct > 60.0 || pct < 40.0; };

  // ---- A) equipos de 3 con el mismo coste total
  std::vector<Cuenta> unidad_a(static_cast<size_t>(U)), coste_a(kMaxCoste + 1u);
  sim::Rng az = sim::Flujo(12345, 77);
  for (int d = 0; d < duelos;) {
    std::vector<int> a, b;
    for (std::vector<int>* t : {&a, &b})
      while (t->size() < 3) {
        const int u = int(az.Hasta(uint32_t(U)));
        if (std::find(t->begin(), t->end(), u) == t->end()) t->push_back(u);
      }
    int ca = 0, cb = 0;
    for (int u : a) ca += eco.unidades[size_t(u)].coste;
    for (int u : b) cb += eco.unidades[size_t(u)].coste;
    if (ca != cb) continue;
    const bool inv = d & 1;   // la mitad de las veces cada equipo juega en el lado contrario
    const int g = inv ? Combate(cat, eco, b, a, 9000 + uint64_t(d)) : Combate(cat, eco, a, b, 9000 + uint64_t(d));
    const int ga = inv ? (g < 0 ? -1 : 1 - g) : g;   // ganador visto desde `a`: 0 = a, 1 = b
    for (int u : a) unidad_a[size_t(u)].Sumar(ga, 0), coste_a[size_t(eco.unidades[size_t(u)].coste)].Sumar(ga, 0);
    for (int u : b) unidad_a[size_t(u)].Sumar(ga, 1), coste_a[size_t(eco.unidades[size_t(u)].coste)].Sumar(ga, 1);
    ++d;
  }
  std::printf("A) %d duelos de equipos de 3 con el mismo coste total (1 estrella, sin sinergias)\n", duelos);
  for (int c = 1; c <= kMaxCoste; ++c) {
    std::printf("  coste %d: %.1f %%  |", c, coste_a[size_t(c)].Pct());
    for (int u = 0; u < U; ++u)
      if (eco.unidades[size_t(u)].coste == c) {
        std::printf(" %s %.1f%%%s", eco.unidades[size_t(u)].id.c_str(), unidad_a[size_t(u)].Pct(), fuera(unidad_a[size_t(u)].Pct()) ? "(!)" : "");
        fallos += fuera(unidad_a[size_t(u)].Pct());
      }
    std::printf("\n");
    fallos += fuera(coste_a[size_t(c)].Pct());
  }

  // ---- B) torneos de bots iguales
  Economia iguales = eco;
  iguales.calidad_bots.assign(7, 100);
  std::vector<Cuenta> unidad_b(static_cast<size_t>(U)), coste_b(kMaxCoste + 1u), sin_b(eco.sinergias.size()), sin_no(eco.sinergias.size());
  double rondas = 0, ticks = 0, duelos_n = 0, rondas_max = 0;
  std::vector<double> reparto_rondas;
  for (int t = 0; t < torneos; ++t) {
    Partida p(cat, iguales, uint64_t(100000 + t), 8);
    while (!p.Acabada() && p.Ronda() < 200) {
      p.Jugar(0);
      struct Lado { std::vector<Pieza> piezas; std::vector<SinActiva> sin; };
      std::vector<Lado> lados(8);
      p.Listo();
      for (int i = 0; i < 8; ++i)
        if (p.J(i).vivo) {
          for (int s = kBanco; s < kSlots; ++s) if (p.J(i).pz[s].hay()) lados[size_t(i)].piezas.push_back(p.J(i).pz[s]);
          lados[size_t(i)].sin = p.Sinergias(i);
        }
      const std::vector<Duelo> ds = p.Duelos();
      if (p.DueloHumano() >= 0) while (!p.Combate().Terminada()) p.Combate().Paso();
      p.TerminarCombate();
      for (size_t k = 0; k < ds.size(); ++k) {
        if (ds[k].b == kNeutral) continue;   // H10: la ronda neutral no mide el equilibrio entre unidades
        const sim::Resultado& r = p.Resultados()[k];
        ticks += r.ticks, duelos_n += 1;
        for (int e = 0; e < 2; ++e) {
          const int dueno = e == 0 ? ds[k].a : ds[k].b;
          if (e == 1 && ds[k].copia) continue;   // la copia no es un jugador: solo cuenta el que la enfrenta
          const Lado& l = lados[size_t(dueno)];
          if (l.piezas.empty()) continue;
          for (const Pieza& z : l.piezas) unidad_b[size_t(z.unidad)].Sumar(r.ganador, e), coste_b[size_t(eco.unidades[size_t(z.unidad)].coste)].Sumar(r.ganador, e);
          std::vector<bool> activa(eco.sinergias.size());
          for (const SinActiva& s : l.sin) if (s.umbral >= 0) activa[size_t(s.sinergia)] = true;
          for (size_t s = 0; s < activa.size(); ++s) (activa[s] ? sin_b : sin_no)[s].Sumar(r.ganador, e);
        }
      }
      p.SiguienteRonda();
    }
    rondas += p.Ronda(), rondas_max = std::max<double>(rondas_max, p.Ronda());
  }
  std::printf("B) %d torneos de 8 bots iguales (%d duelos): ", torneos, int(duelos_n));
  const double r_media = rondas / double(torneos), combate = ticks / duelos_n / sim::kHz;
  std::printf("rondas por partida %.1f (max %.0f); un combate dura %.1f s de media\n", r_media, rondas_max, combate);
  for (int c = 1; c <= kMaxCoste; ++c) std::printf("  coste %d en el tablero: %.1f %% de victorias (%d apariciones)\n", c, coste_b[size_t(c)].Pct(), int(coste_b[size_t(c)].total));
  std::printf("  por unidad:");
  for (int u = 0; u < U; ++u) std::printf(" %s %.1f%%", eco.unidades[size_t(u)].id.c_str(), unidad_b[size_t(u)].Pct());
  std::printf("\n  por sinergia (activa / sin ella):");
  for (size_t s = 0; s < eco.sinergias.size(); ++s) {
    std::printf(" %s %.1f%% / %.1f%%%s", eco.sinergias[s].id.c_str(), sin_b[s].Pct(), sin_no[s].Pct(), fuera(sin_b[s].Pct()) ? "(!)" : "");
    fallos += fuera(sin_b[s].Pct());
  }
  const double minutos = r_media * (combate + kPrepSegundos + kResultadoSegundos) / 60.0;
  std::printf("\n  duracion estimada de la partida a x1: %.1f min (%.1f rondas x (%.0f s de combate + %.0f s de preparacion + %.0f s de resultado))\n", minutos, r_media, combate, kPrepSegundos,
              kResultadoSegundos);
  if (minutos < 20.0 || minutos > 30.0) ++fallos;
  std::printf("\n%d fuera de limites\n", fallos);
  return fallos ? 1 : 0;
}
