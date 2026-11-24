// dbz3 - Modo Heroe: Evolucion por raza (maqueta 12, DISENO 7.1). Regla PURA (sin juego, sin ImGui): que formas abre cada
// raza segun las marcas de la partida. La usan heroe.cpp (ComprobarDesbloqueos, tipo `forma` de res/heroe/desbloqueos.toml)
// y tools/test_heroe_formas.cpp (assert sin juego). Solo vale con el ajuste dbz3_heroe_evolucion (apagado de serie).
//
// Una forma = una entrada tipo "forma" de desbloqueos.toml:
//   raza   0 Saiyan, 1 Humano, 2 Namekiano, 3 Raza de Freezer, 4 Majin, 5 Androide (orden de las pestanas, no de kRazas)
//   rama   "" tronco comun, "A" o "B" (la rama que abre cada camino)
//   previa id de la forma anterior de la cadena ("" = parte de la forma base de la raza)
//   marca  marca de ruta que abre el camino ("" = no hace falta; hoy son marcas de prueba, DBZ3_TEST_HEROE_MARCAS)
//   vence  rival (id de luchador) al que hay que ganar para despertarla (-1: se abre sola al ser alcanzable)
//   abre_con id de otro desbloqueo (p. ej. ssj1) que ya da la forma (-1/"": ninguno)
#pragma once

#include <set>
#include <string>
#include <vector>

namespace dbz3::heroe::formas {

struct Forma {
  std::string id, rama, previa, marca, abre_con;
  int raza = 0;
  int vence = -1;
};

// Una forma es ALCANZABLE si su camino esta abierto: la marca (si pide) esta puesta y la forma anterior ya es del heroe.
inline bool Alcanzable(const Forma& f, const std::set<std::string>& tiene, const std::set<std::string>& marcas) {
  if (!f.marca.empty() && !marcas.count(f.marca)) return false;
  return f.previa.empty() || tiene.count(f.previa) != 0;
}

// Una alcanzable se CONSIGUE cuando cumple su condicion: ganar a `vence` (rival), tener el desbloqueo `abre_con`, o nada
// (se abre sola al ser alcanzable). rival < 0: no se acaba de ganar ningun combate.
inline bool CumpleCondicion(const Forma& f, const std::set<std::string>& tiene, int rival) {
  if (f.abre_con.empty() && f.vence < 0) return true;
  return (!f.abre_con.empty() && tiene.count(f.abre_con) != 0) || (f.vence >= 0 && rival == f.vence);
}

// Formas de `raza` que se abren ahora, en cadena (una abre la siguiente en la misma llamada). `tiene` = ids ya conseguidos
// (g_desbloqueos); no se modifica: devuelve los ids NUEVOS en el orden en que se abren.
inline std::vector<std::string> NuevasAbiertas(const std::vector<Forma>& todas, int raza, std::set<std::string> tiene,
                                                const std::set<std::string>& marcas, int rival) {
  std::vector<std::string> nuevas;
  for (bool cambio = true; cambio;) {
    cambio = false;
    for (const Forma& f : todas) {
      if (f.raza != raza || tiene.count(f.id)) continue;
      if (!Alcanzable(f, tiene, marcas) || !CumpleCondicion(f, tiene, rival)) continue;
      tiene.insert(f.id);
      nuevas.push_back(f.id);
      cambio = true;
    }
  }
  return nuevas;
}

// Estado de cada forma para la pantalla: 0 abierta, 1 alcanzable pero con condicion pendiente ("?" con su condicion),
// 2 camino cerrado (su marca no esta puesta o la forma anterior no se tiene: "?" atenuado).
inline int Estado(const Forma& f, const std::set<std::string>& tiene, const std::set<std::string>& marcas) {
  if (tiene.count(f.id)) return 0;
  return Alcanzable(f, tiene, marcas) ? 1 : 2;
}

}  // namespace dbz3::heroe::formas
