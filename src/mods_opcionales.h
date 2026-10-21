// dbz3 - Mods opcionales (opcional.toml): lector comun de los datos que el port lee de sus mods activos.
// Encargo studies 3/13 (mods opcionales M1-M11; E:\DBZ3HD_trabajo\R7\mods_opcionales\mods). Un mod opcional es una
// carpeta normal de mods/ (manifest.txt con type=opcional) con un opcional.toml de numeros verificados; el gancho
// del juego enganchado a el solo actua si esta activo (sin .disabled), como el resto del sistema (mods.cpp).
// Este fichero no reparte nada del juego: solo lee archivos del jugador (mods/ y su ISO de PS2).
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

namespace dbz3::opcional {

// Un mod opcional instalado: su carpeta en mods/, su id logico y su opcional.toml ya leido.
struct Mod {
  std::string carpeta;   // nombre de la carpeta en mods/ (p. ej. "opc_b2_musica_combate")
  std::string id;        // id logico ("b2.musica_combate"): [opcional] id del toml o, si falta, derivado de la
                         // carpeta ("opc_b2_musica_combate" -> "b2.musica_combate")
  bool activo = false;   // sin .disabled (el marker del sistema de mods)
  toml::table datos;     // opcional.toml entero (tablas [opcional], [pistas], [eventos]...)
  toml::table textos;    // textos.toml del mod (nombre_<idioma>/descripcion_<idioma>; vacio si no lo trae)
};

// Mods instalados con opcional.toml (cache de 2 s; el launcher llama a Refrescar() tras activar/desactivar).
std::shared_ptr<const std::vector<Mod>> Listar();
void Refrescar();   // tira la cache

// ¿El mod opcional `id` esta activo? `id` = id logico o nombre de carpeta. Para las pruebas de tools/pruebas.py,
// la variable DBZ3_TEST_OPCIONAL ("id1,id2" separados por comas) los da por activos sin tocar mods/.
bool Activo(const std::string& id);
// ¿Esta instalado y apagado? Lo usa el gancho de M11: con el mod instalado manda su casilla (activado: se ve;
// apagado: no) y, si no esta instalado, vale el defecto del port. Con DBZ3_TEST_OPCIONAL nunca cuenta como apagado.
bool Desactivado(const std::string& id);

// Valor de `clave` del opcional.toml del mod `id`: primero en la raiz del fichero y, si no esta, dentro de
// [opcional]. `clave` admite un punto ("pistas.74"); las listas/tablas usan Lista()/Claves().
long long Entero(const std::string& id, const std::string& clave, long long defecto = 0);
std::string Texto(const std::string& id, const std::string& clave, const std::string& defecto = "");
std::vector<int> Lista(const std::string& id, const std::string& clave);          // valores de un array de enteros
std::vector<std::string> Claves(const std::string& id, const std::string& tabla); // claves de una tabla (en orden)

// Nombre y descripcion del mod en el idioma pedido ("es","en","it","de","fr","pt","ja"), de su textos.toml
// (nombre_<idioma>/descripcion_<idioma>); si el mod no trae textos.toml, del manifest.txt (solo es/en).
std::string Nombre(const Mod& m, const std::string& idioma);
std::string Descripcion(const Mod& m, const std::string& idioma);

// Ruta real de `ruta` (p. ej. "USR/MOVIE_EU.SFD") dentro de la copia de PS2 `juego` del jugador: "b2" o "iw"
// (o el nombre con el que se busca en ps2_games). La carpeta extraida se usa tal cual; si es un .iso, el fichero
// se extrae a user_data/dbz3/cache_ps2 (una vez, sin tocar la ISO). Vacia si no esta.
std::filesystem::path ArchivoDeJuegoPs2(const std::string& juego, const std::string& ruta);

}  // namespace dbz3::opcional

// G05: selector de musica de combate. El backend vive aqui para que la previa de combate y Opciones solo
// tengan que pedir/elegir una pista; la reproduccion usa el reproductor ADX que ya existe (OirMusicaPs2).
// Nada se reparte: las pistas salen de la copia del jugador (B3 HD y sus ISO de PS2) o de sus mods opcionales.
namespace dbz3::musica {

enum class Modo {
  kOriginal = 0,   // por defecto: la musica propia del modo (no se toca; B2/IW siguen con la suya)
  kFija = 1,       // una pista concreta, siempre
  kAleatoria = 2,  // una pista al azar por combate (determinista con la semilla que se pase)
};

struct Pista {
  std::string fuente;              // "b3", "b2", "iw", "b1" o "mod:<carpeta>"
  std::string nombre;              // texto para la lista ("Budokai 2 #17 (mod ...)")
  std::string afs;                 // contenedor dentro del juego ("ADX_US.AFS"...)
  std::filesystem::path juego;     // carpeta/ISO que abre pasarela::AfsPs2
  int entrada = -1;                // indice de la pista en el AFS
  int hz = 0;
  int canales = 1;
  double segundos = 0.0;
};

struct Opcion {
  Modo modo = Modo::kOriginal;
  std::string fuente;              // "" = cualquiera; si no, la de Pista::fuente
  int entrada = -1;                // -1 = la primera de esa fuente
  std::string partida;             // id de partida; "" = ajuste global
};

// Pistas ADX validas (musica) de B3 HD, de las copias de PS2 del jugador y de los mods opcionales activos
// que declaren [pistas]. Cache de 5 s; la UI puede llamarla al abrir su pantalla.
std::vector<Pista> Pistas();
void RefrescarPistas();

// Persistencia: user_data/dbz3/musica.toml (global) + musica_partida_<id>.toml (se guarda con la partida).
Opcion Leer(const std::string& partida = "");
void Guardar(const Opcion& opcion);
std::filesystem::path RutaRegistro(const std::string& partida = "");

// Pista elegida; vacia si Modo::kOriginal. `semilla` fija la eleccion aleatoria del combate.
Pista Elegir(const Opcion& opcion, uint32_t semilla);

// Ambitos con musica propia que NO se debe pisar (tablero/menus de B2 e IW, nave, menus de B1).
bool ModoConMusicaNativa(const std::string& ambito);

// Previa: reproduce en bucle y corta la anterior. `volumen` 0..1.
void Escuchar(const Pista& pista, float volumen);
void Parar();

}  // namespace dbz3::musica
