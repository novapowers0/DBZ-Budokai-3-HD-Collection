// dbz3 launcher - reparto de numeros (IDs) de lo que anaden los mods, para que el juego no choque al arrancar.
//
// Escenarios (stage.toml `entrada`/`menu`, entradas de data_cmn.afs >= 5000): si un escenario de un mod pide un
// numero que ya usa otro mod activo (o uno generado desde las ISO, carpetas con origen.txt), el launcher le da otro
// libre (5200+). mods/_ids_escenarios.toml apunta el numero de cada escenario de mod (movido o no): el mismo mod
// conserva siempre su numero, aunque se reinstale, se desactive un tiempo o llegue otro antes por orden alfabetico. Se renombra su carpeta us/data_cmn.afs/<n> y se reescribe su stage.toml (nada se
// borra). Los generados (campos_iw.py) nunca se mueven: tienen sus numeros fijos 5000-5199.
// El numero de escenario que guarda la partida es el registro (17+), no esta entrada: cambiarla no toca partidas.
//
// Capsulas (`[[capsula]] id` de los roster.toml): roster_build.py las reparte de forma estable (roster_ids.toml).
// Si dos mods activos traen la misma, no se puede arreglar sin rehacer el mod: se bloquea JUGAR y se dice cual
// desactivar.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbz3::launcher::ids {

struct Cambio {      // escenario movido a un numero libre
  std::string mod, con;   // `con` = el mod con el que chocaba
  int antes = 0, despues = 0;
};

struct Bloqueo {     // choque que no se arregla solo: hay que desactivar `mod`
  std::string mod, con;
  int numero = 0;
  bool capsula = false;
};

struct Revision {
  std::vector<Cambio> cambios;
  std::vector<Bloqueo> bloqueos;
};

constexpr int kPrimeraLibre = 5200;   // 5000-5199: escenarios generados desde las ISO (campos_iw.py)

// `activos`: carpetas de los mods activos. Arregla lo que puede (en disco) y devuelve lo hecho y lo que bloquea.
Revision Revisar(const std::filesystem::path& mods_root, std::vector<std::string> activos);

}  // namespace dbz3::launcher::ids
