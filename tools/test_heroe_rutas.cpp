// Test SIN JUEGO de la regla de rutas del Modo Heroe (src/heroe_rutas.h). Lo lanza `python heroe_sagas_a_du.py --comprobar`
// (mod center hd), que le pasa los tramos REALES de res/heroe/sagas.toml y su tabla de casos (tsv; "-" = vacio):
//   T etapa condicion cruce prioridad opcionales      C etapa marcas(a,b) tramo cruce(i,j)      M semilla tramo mezcla
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "../src/heroe_rutas.h"

using namespace dbz3::heroe::rutas;

static std::string V(const std::string& s) { return s == "-" ? std::string() : s; }
static std::vector<std::string> Partir(const std::string& s) {
  std::vector<std::string> v;
  std::stringstream ss(s);
  for (std::string x; std::getline(ss, x, ',');)
    if (!x.empty()) v.push_back(x);
  return v;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream f(argv[1]);
  std::vector<Tramo> t;
  int casos = 0, fallos = 0;
  for (std::string l; std::getline(f, l);) {
    std::stringstream ss(l);
    std::string tipo;
    ss >> tipo;
    if (tipo == "T") {
      Tramo x;
      std::string cond, cruce;
      ss >> x.etapa >> cond >> cruce >> x.prioridad >> x.opcionales;
      x.condicion = V(cond), x.cruce = V(cruce);
      t.push_back(x);
    } else if (tipo == "C") {
      int etapa = 0, tramo = 0;
      std::string marcas, cruce;
      ss >> etapa >> marcas >> tramo >> cruce;
      std::set<std::string> m;
      for (const auto& x : Partir(V(marcas))) m.insert(x);
      std::vector<int> esperado;
      for (const auto& x : Partir(V(cruce))) esperado.push_back(std::stoi(x));
      const Siguiente s = Elegir(t, etapa, m);
      ++casos;
      if (s.tramo != tramo || s.cruce != esperado) {
        ++fallos;
        std::printf("FALLO etapa %d marcas %s: tramo %d (esperado %d), cruce %zu\n", etapa, marcas.c_str(), s.tramo, tramo, s.cruce.size());
      }
    } else if (tipo == "M") {
      unsigned long long semilla = 0, tramo = 0, esperado = 0;
      ss >> semilla >> tramo >> esperado;
      ++casos;
      if (Mezcla(uint32_t(semilla), uint32_t(tramo)) != uint32_t(esperado)) {
        ++fallos;
        std::printf("FALLO mezcla %llu %llu\n", semilla, tramo);
      }
    }
  }
  std::printf("test_heroe_rutas (C++): %d tramos, %d casos, %d fallos\n", int(t.size()), casos, fallos);
  return fallos ? 1 : 0;
}
