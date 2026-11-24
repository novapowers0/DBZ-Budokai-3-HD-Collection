// Test SIN JUEGO de la regla raza + marcas -> formas abiertas (src/heroe_formas.h). Lo lanza tools/test_heroe_formas.py,
// que le pasa las formas REALES de res/heroe/desbloqueos.toml (tsv: id raza rama previa marca abre_con vence; "-" = vacio).
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "../src/heroe_formas.h"

using namespace dbz3::heroe::formas;
using S = std::set<std::string>;

static std::vector<Forma> Cargar(const char* ruta) {
  std::ifstream f(ruta);
  std::vector<Forma> v;
  for (std::string l; std::getline(f, l);) {
    std::stringstream ss(l);
    Forma x;
    std::string raza, vence, rama, previa, marca, abre;
    ss >> x.id >> raza >> rama >> previa >> marca >> abre >> vence;
    auto v0 = [](std::string s) { return s == "-" ? std::string() : s; };
    x.raza = std::stoi(raza), x.rama = v0(rama), x.previa = v0(previa), x.marca = v0(marca), x.abre_con = v0(abre),
    x.vence = std::stoi(vence);
    v.push_back(x);
  }
  return v;
}
static S Conj(const std::vector<std::string>& v) { return S(v.begin(), v.end()); }

#define IGUAL(obtenido, que, ...)                                                                                \
  do {                                                                                                          \
    const S o = (obtenido), e = S{__VA_ARGS__};                                                                     \
    if (o != e) {                                                                                               \
      std::cerr << "FALLA " << que << ": salio {";                                                              \
      for (auto& s : o) std::cerr << s << " ";                                                                  \
      std::cerr << "} y se esperaba {";                                                                         \
      for (auto& s : e) std::cerr << s << " ";                                                                  \
      std::cerr << "}\n";                                                                                       \
      return 1;                                                                                                 \
    }                                                                                                           \
    ++hechos;                                                                                                   \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const std::vector<Forma> T = Cargar(argv[1]);
  int hechos = 0;
  assert(!T.empty());
  // Saiyan: el tronco (SSJ, SSJ2) se gana vencindo; las ramas dependen de la marca
  IGUAL(Conj(NuevasAbiertas(T, 0, {}, {}, 27)), "Saiyan sin marcas, vence a Freezer", "f_sai_ssj");
  IGUAL(Conj(NuevasAbiertas(T, 0, {}, {}, -1)), "Saiyan sin marcas ni victoria: nada", );
  IGUAL(Conj(NuevasAbiertas(T, 0, {"f_sai_ssj"}, {}, 33)), "Saiyan, vence a Cell", "f_sai_ssj2");
  IGUAL(Conj(NuevasAbiertas(T, 0, {"f_sai_ssj", "f_sai_ssj2"}, {"maestro_goku"}, 36)), "Saiyan maestro Goku: Kaioken y SSJ3", "f_sai_kaioken", "f_sai_ssj3");
  IGUAL(Conj(NuevasAbiertas(T, 0, {"f_sai_ssj", "f_sai_ssj2"}, {"villanos_revividos"}, 35)), "Saiyan villanos revividos: Majin, sin SSJ3 ni Kaioken", "f_sai_majin");
  IGUAL(Conj(NuevasAbiertas(T, 0, {"f_sai_ssj", "f_sai_ssj2"}, {"villanos_revividos"}, 36)), "Majin pide ganar a Super Bu (35)", );
  // abre_con: el desbloqueo ssj1 ya existente da la forma sin ganar de nuevo
  IGUAL(Conj(NuevasAbiertas(T, 0, {"ssj1"}, {}, -1)), "Saiyan con ssj1 ya conseguido", "f_sai_ssj");
  // Humano: una rama por marca
  IGUAL(Conj(NuevasAbiertas(T, 1, {}, {}, 33)), "Humano sin marcas: nada", );
  IGUAL(Conj(NuevasAbiertas(T, 1, {}, {"opc_namek"}, -1)), "Humano opcional de Namek", "f_hum_potencial");
  IGUAL(Conj(NuevasAbiertas(T, 1, {}, {"maestro_krilin"}, -1)), "Humano maestro Krilin", "f_hum_maestria");
  // Raza de Freezer: cadena de la Nave, una forma por victoria
  IGUAL(Conj(NuevasAbiertas(T, 3, {}, {"camino_nave"}, -1)), "Freezer camino de la Nave: 2.a forma sola", "f_fre_2");
  IGUAL(Conj(NuevasAbiertas(T, 3, {"f_fre_2"}, {"camino_nave"}, 33)), "Freezer vence a Cell: 3.a y final en cadena", "f_fre_3", "f_fre_final");
  IGUAL(Conj(NuevasAbiertas(T, 3, {"f_fre_2", "f_fre_3", "f_fre_final"}, {"camino_nave"}, 36)), "Freezer 100 %", "f_fre_100");
  IGUAL(Conj(NuevasAbiertas(T, 3, {}, {}, 33)), "Freezer sin el camino de la Nave: cerrada", );
  IGUAL(Conj(NuevasAbiertas(T, 3, {"f_fre_2"}, {}, -1)), "Freezer: sin la marca la 3.a no se abre aunque tenga la 2.a", );
  // Majin y Androide: la marca de otra raza no se cuela
  IGUAL(Conj(NuevasAbiertas(T, 4, {}, {"camino_nave", "camino_lab"}, 33)), "Majin con marcas de otras razas: nada", );
  IGUAL(Conj(NuevasAbiertas(T, 4, {}, {"camino_puro"}, -1)), "Majin camino puro: Kid Bu", "f_maj_kid");
  IGUAL(Conj(NuevasAbiertas(T, 4, {}, {"camino_maldad"}, -1)), "Majin camino de la maldad: Super Bu", "f_maj_super");
  IGUAL(Conj(NuevasAbiertas(T, 4, {"f_maj_super"}, {"camino_maldad"}, 33)), "Majin: absorcion", "f_maj_absorcion");
  IGUAL(Conj(NuevasAbiertas(T, 4, {"f_maj_super"}, {"camino_maldad", "camino_puro"}, -1)), "Majin ya con Super Bu: el camino puro sigue su curso aparte", "f_maj_kid");
  IGUAL(Conj(NuevasAbiertas(T, 5, {}, {"camino_ciudad"}, -1)), "Androide camino de la Ciudad: Accel Dance", "f_and_accel");
  IGUAL(Conj(NuevasAbiertas(T, 5, {}, {"camino_lab"}, 33)), "Androide Laboratorio, vence a Cell", "f_and_absorcion", "f_and_perfecto");
  // ya conseguidas no se repiten
  IGUAL(Conj(NuevasAbiertas(T, 2, {"f_nam_nail"}, {"camino_vegeta"}, 0)), "Namekiano: Nail ya conseguida, no se repite", );
  // Estado para la pantalla: 0 abierta, 1 alcanzable pendiente, 2 camino cerrado
  const auto buscar = [&](const char* id) -> const Forma& {
    for (const Forma& f : T)
      if (f.id == id) return f;
    std::cerr << "falta la forma " << id << "\n";
    std::exit(3);
  };
  const Forma& kid = buscar("f_maj_kid");
  if (Estado(kid, {}, {}) != 2 || Estado(kid, {}, {"camino_puro"}) != 1 || Estado(kid, {"f_maj_kid"}, {}) != 0) {
    std::cerr << "FALLA Estado de Kid Bu\n";
    return 1;
  }
  hechos += 3;
  const Forma& abs_ = buscar("f_maj_absorcion");   // alcanzable solo con Super Bu y la marca
  if (Estado(abs_, {}, {"camino_maldad"}) != 2 || Estado(abs_, {"f_maj_super"}, {"camino_maldad"}) != 1) {
    std::cerr << "FALLA Estado de Absorcion\n";
    return 1;
  }
  hechos += 2;
  std::printf("OK: %d comprobaciones, %zu formas\n", hechos, T.size());
  return 0;
}
