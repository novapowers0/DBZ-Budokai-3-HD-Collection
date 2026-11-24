// Dragon Budokai Survivors: progreso entre partidas (tanda 4): zeni acumulados, mejoras permanentes de la tienda, escenarios desbloqueados,
// medallas y jefes derrotados. Sin ImGui ni nada del juego (tests/survivors_juego_test.cpp lo compila solo).
// Archivo aparte (modo.cpp: UserDataRoot()/survivors_progreso.toml; en pruebas, otro): no toca ningun guardado existente.
// Escritura atomica (.tmp + rename, la version anterior queda en .bak, como hub_modos.toml). Sin archivo o corrupto: progreso de serie
// (y si el archivo no se lee pero el .bak si, el .bak).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "juego.h"

namespace dbz3::survivors {

enum Medalla : uint8_t { kSinMedalla, kBronce, kPlata, kOro, kPlatino };
struct Progreso {
  int zeni = 0;
  int mejoras[kMejorasPerm] = {};          // nivel 0..5 (orden de Datos::mejoras)
  uint32_t desbloqueados = 1;              // bit por escenario (el primero, siempre)
  uint8_t medalla[kMaxEscenarios] = {};    // la mejor de cada escenario
  int jefes = 0, partidas = 0, victorias = 0;
  // ---- P2 (meta v2): coleccion, logros, records y totales. Se guardan siempre (claves nuevas, opcionales al leer); solo se MUESTRAN con
  // dbz3_survivors_meta_v2.
  uint64_t vistos_tec = 0, vistos_pas = 0;   // bit por indice de Datos::tecnicas (base y evoluciones) / Datos::pasivas
  uint32_t vistos_enem = 0, vistos_jefe = 0; // bit por indice de Datos::enemigos / Datos::jefes
  uint64_t logros = 0;                       // bit por logro de logros.toml (hasta 64)
  uint32_t luchadores = 0;                   // bit por personaje desbloqueado por un logro (los que no tienen regla estan abiertos)
  int cofres = 0, total_derrotados = 0, total_zeni = 0, evoluciones = 0, mejor_pts = 0;
  int record[kMaxEscenarios][2] = {};        // mejor puntuacion (formula del ranking) por escenario y dificultad
  int opc_volumen = 100, opc_zoom10 = 15, opc_sacudida = 1, opc_autoapunte = 1;   // Opciones del menu v2: volumen del modo (0..100), zoom de serie x10, sacudida de pantalla
  bool ultimo_record = false;                // (no se guarda) la ultima partida anotada batio el record de su escenario y dificultad
  bool Desbloqueado(int e) const { return e == 0 || (e >= 0 && e < 32 && (desbloqueados >> e) & 1u); }
};

// ---- logros.toml (C07 / C08): una condicion y su recompensa (zeni y/o un desbloqueo)
constexpr int kMaxLogros = 64, kIdiomas = 6;   // es, en, fr, de, it, pt
struct Logro {
  std::string id, cond, arg, icono = "ki_01", desbloquea;   // desbloquea: "luchador:<id>" (vacio = solo zeni)
  int valor = 1, zeni = 0;
  std::string txt[kIdiomas], desc[kIdiomas];
};
// cond (por partida): derrotados, tiempo (s), nivel, jefes, racha, rotos, zeni, cofres, evoluciones, tecnicas_max (tecnicas al nivel 5),
// victoria, victoria_dificil, medalla_platino, victoria_personaje:<id>, victoria_escenario:<id>, derrotar_jefe:<id>, evolucion:<id>;
// de toda la vida (total_): total_derrotados, total_zeni, total_partidas, total_victorias, total_jefes, total_cofres, total_logros.
bool LeerLogros(const std::filesystem::path& p, std::vector<Logro>* out, std::string* aviso);
const std::string& TextoLogro(const Logro& l, bool desc, const char* idioma);   // idioma "es", "en", "fr", "de", "it", "pt" (otro = en)
// Cumplidos nuevos en esta partida: `cofres` = cofres abiertos en ella; `fin` = la partida acabo (las condiciones de victoria y de toda la
// vida solo cuentan entonces). Marca el bit, suma los zeni de la recompensa y aplica el desbloqueo; llena `nuevos` con los indices.
int EvaluarLogros(const Datos& d, const std::vector<Logro>& ls, const Juego& j, int cofres, bool fin, Progreso* g, std::vector<int>* nuevos);
// Condicion en palabras que falta para abrir al luchador `i` (id del logro que lo abre, o -1 si esta abierto / no tiene regla)
int LogroQueAbre(const std::vector<Logro>& ls, const Datos& d, const Progreso& g, int personaje);
int PuntosPartida(int segundos, int bajas, bool victoria);   // la formula del ranking mundial (ranking_online.cpp)
int64_t TotalGastado(const Datos& d, const Progreso& g);         // zeni que cuestan las mejoras ya compradas
int DevolverTodo(const Datos& d, Progreso* g);               // las mejoras vuelven a 0 y los zeni vuelven exactos; devuelve lo devuelto, o -1 si no cabe (sin tocar el progreso)

// false = no habia archivo legible (out queda de serie); 'aviso' explica por que (vacio si simplemente no existe)
bool LeerProgreso(const std::filesystem::path& p, Progreso* out, std::string* aviso);
bool GuardarProgreso(const std::filesystem::path& p, const Progreso& g);

int Precio(const Datos& d, const Progreso& g, int mejora);   // precio del siguiente nivel; -1 = ya al maximo o no existe
bool Comprar(const Datos& d, int mejora, Progreso* g);      // true si se ha comprado (resta el precio y sube el nivel)
Config ConfigDe(const Progreso& g, int escenario, int dificultad);   // lo que entra en Juego::Iniciar

// Al acabar una partida: suma sus zeni, la medalla del escenario y los desbloqueos (el siguiente al ganar o al llegar al minuto
// 10). Devuelve el indice del escenario recien desbloqueado (-1 = ninguno nuevo).
Medalla MedallaDe(bool victoria, float minuto, int dificultad);
int Anotar(const Juego& j, int n_escenarios, Progreso* g);

}  // namespace dbz3::survivors
