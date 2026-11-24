// dbz3 - Modo Heroe: rutas (grafo de tramos, DISENO 12/11 §1, maqueta 1 "cruce"). Regla PURA (sin juego): que tramo se
// juega despues segun las marcas de la partida y la semilla. La usan heroe.cpp (solo con el ajuste dbz3_heroe_rutas,
// apagado de serie) y tools/test_heroe_rutas.cpp; heroe_sagas_a_du.py lleva la MISMA regla en Python (ruta_siguiente).
//
// Un tramo = una [[saga]] de res/heroe/sagas.toml (las 8 de siempre + las `variante = true`), con:
//   etapa      orden en la partida (0, 1, 2...); los tramos de una misma etapa compiten
//   condicion  "a|b" = hace falta alguna de esas marcas ("" = siempre); "!a" = no debe estar
//   cruce      no vacio: el tramo sale como camino en el mapa; si hay 2+ candidatos con cruce, elige el jugador
//   prioridad  sin cruce, gana el candidato de mas prioridad (empate: el primero)
#pragma once

#include <algorithm>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dbz3::heroe::rutas {

// H02: identidades de la primera busqueda, distintas de las Esferas que se poseen.
// Un solo registro corto en desbloqueos de cac.toml; gastar no lo reinicia.
struct BusquedaEsferas {
  int cobrados = 0, decision = 0;   // decision: 0 pendiente, 1 revivir, 2 seguir sin revivir
  bool Cobrar(int evento, int& posesion) {
    if (evento < 0 || evento > 6 || decision || (cobrados & (1 << evento))) return false;
    cobrados |= 1 << evento;
    posesion |= 1 << evento;
    return true;
  }
  bool Confirmar(bool revivir, int& posesion) {
    if (decision || posesion != 127) return false;
    decision = revivir ? 1 : 2;
    posesion = 0;
    return true;
  }
  std::string Registro() const { return "h02_v1_" + std::to_string(cobrados) + "_" + std::to_string(decision); }
  bool Leer(const std::string& registro) {
    if (registro.rfind("h02_v1_", 0) != 0) return false;
    std::stringstream s(registro.substr(7));
    int c = -1, d = -1;
    char sep = 0;
    if (!(s >> c >> sep >> d) || sep != '_' || !s.eof() || c < 0 || c > 127 || d < 0 || d > 2) return false;
    cobrados = c, decision = d;
    return true;
  }
};

struct Tramo {
  int etapa = -1;   // -1: fuera de la ruta (no se elige nunca)
  std::string condicion, cruce, marca;
  int prioridad = 0;
  int opcionales = 0;   // cuantos rivales opcionales sorteables lleva
};

// Una condicion "a|b|!c": alguna alternativa se cumple (una "!x" se cumple si x NO esta).
inline bool Cumple(const std::string& condicion, const std::set<std::string>& marcas) {
  if (condicion.empty()) return true;
  std::stringstream ss(condicion);
  for (std::string a; std::getline(ss, a, '|');) {
    if (a.empty()) continue;
    if (a[0] == '!' ? !marcas.count(a.substr(1)) : marcas.count(a) != 0) return true;
  }
  return false;
}

struct Siguiente {
  int tramo = -1;            // -1: no queda tramo (final)
  std::vector<int> cruce;    // 2+ = el jugador elige en el mapa (tramo = el primero, el de la intro)
};

// Primer tramo jugable desde la etapa `desde` (se saltan las etapas sin candidatos).
inline Siguiente Elegir(const std::vector<Tramo>& t, int desde, const std::set<std::string>& marcas) {
  int max_etapa = -1;
  for (const Tramo& x : t) max_etapa = std::max(max_etapa, x.etapa);
  for (int e = std::max(desde, 0); e <= max_etapa; ++e) {
    std::vector<int> c, cr;
    for (int i = 0; i < int(t.size()); ++i)
      if (t[i].etapa == e && Cumple(t[i].condicion, marcas)) {
        c.push_back(i);
        if (!t[i].cruce.empty()) cr.push_back(i);
      }
    if (c.empty()) continue;
    Siguiente s;
    if (cr.size() >= 2) {
      s.tramo = cr[0], s.cruce = cr;
      return s;
    }
    s.tramo = c[0];
    for (int i : c)
      if (t[i].prioridad > t[s.tramo].prioridad) s.tramo = i;
    return s;
  }
  return {};
}

// Mezcla de la semilla con el tramo (la misma en Python: heroe_sagas_a_du.mezcla). 32 bits.
inline uint32_t Mezcla(uint32_t semilla, uint32_t tramo) {
  uint32_t x = semilla ^ (tramo * 2654435761u);
  x ^= x >> 16, x *= 0x45D9F3Bu, x ^= x >> 16;
  return x;
}
// Rival opcional sorteado del tramo (0..opcionales-1), o -1 si no lleva.
inline int Opcional(const Tramo& t, uint32_t semilla, int tramo) {
  return t.opcionales > 0 ? int(Mezcla(semilla, uint32_t(tramo)) % uint32_t(t.opcionales)) : -1;
}

}  // namespace dbz3::heroe::rutas

// Cruce abierto en el mapa (heroe.cpp -> heroe_v2_cruce.inl): solo con el ajuste dbz3_heroe_rutas.
namespace dbz3::heroe {
struct InfoCamino {
  int tramo = -1, escenario = 0, rival = -1;   // escenario del primer combate; rival que distingue el camino
  std::string nombre;                           // "Nave de Freezer"
  std::string rival_nombre;                     // nombre del rival de sagas.toml ("Recoome", "Jeice"); vacio: el del juego
  std::string rival_mod;                        // villano revivido (mod cut_*): su retrato es rutas/rival_<mod> (lo deja el montaje)
  std::string lugar;                            // lugar bajo la foto ("Namek"); vacio: el nombre del escenario
};
struct InfoCruce {
  std::vector<InfoCamino> caminos;
  std::string pregunta;   // texto del tramo en el idioma del ajuste
  std::string opcional;   // "Dodoria o Zarbon" si el opcional sorteado sigue pendiente; vacio si no hay o ya esta hecho
  int esferas = 0;
};
bool CruceRuta(InfoCruce& o);   // false: no hay cruce abierto
int CaminoDelCruce(int i);      // tramo del camino i (0 = el de la intro), o -1
bool NaveEnMapa();              // rutas en Namek: el avatar del mapa es la nave de Freezer
}  // namespace dbz3::heroe
