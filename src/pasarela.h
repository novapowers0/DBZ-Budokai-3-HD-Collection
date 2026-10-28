// dbz3 - Pasarela de modos (src/pasarela.cpp: Leyenda de Mr. Satan; src/pasarela_misiones.cpp: saga de B1, tablero,
// misiones de B2) y lectura de la ISO de Budokai 1 del jugador (src/pasarela_b1.cpp).
// Nada del juego de origen va en la version: todo se lee en tiempo de ejecucion de la copia del jugador.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "ingame/game_style.h"

namespace dbz3::pasarela {

struct Imagen {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
  explicit operator bool() const { return w > 0; }
};

// Un combate de B1 (registro de 128 B del ELF; ver docs/16_pasarela_modos/): los de "La leyenda de Mr. Satan" y los
// de la historia (misma estructura, modo 0, +0x28 capitulo).
struct CombateB1 {
  int jugador_b1 = 21;    // +0x20 personaje del lado 1
  int forma_jugador = 0;  // +0x22
  int ki_jugador = 300;   // +0x2C (0xFFFF = el que le quedo)
  int capitulo = -1;      // +0x28 (historia: orden dentro de la saga del protagonista)
  int modo = 0;           // +0x70 (3 Mr. Satan, 0 historia)
  int rival_b1 = 0;       // ID de personaje de B1 (registro +0x50)
  int rival = 0;          // ID de B3 equivalente
  int variante = 0;       // +0x51 (sin confirmar)
  int forma = 0;          // +0x52 forma con la que empieza (Goku 4 = SSJ, Gohan 1 = SSJ, Cell 2 = perfecto)
  int cantidad = 1;       // +0x56 rivales seguidos (Cell Jr: 5)
  int vida_rival = 3000;  // +0x5A
  int ki_rival = 300;     // +0x5C
  bool arrastra_vida = false;   // vida del jugador 0xFFFF en +0x2A: no se recupera (Krilin, Gohan)
  int vida_jugador = 3000;
  int escenario_b1 = 0;   // +0x71 (14 = CHG_CELL RING)
  std::string escenario_nombre;   // nombre del registro de escenario de B1
  int musica = 0;         // +0x72 (hipotesis)
  int tiempo = -2;        // +0x74 en cuadros (5940 = 99 s; -1 sin limite; -2 el de las opciones)
};

// Piezas de la pantalla de resultado del modo (entrada 668 del AFS de cada idioma) y del titulo (666).
struct ResultadoB1 {
  Imagen tablon;     // 668 #15: tablon con el logo y "NEXT BATTLE"
  Imagen retadores;  // 668 #16: "Challenger 1st".."11th" (3 x 4 de 136 x 72)
  Imagen cruz;       // 668 #17: aspa roja
  Imagen total;      // 668 #18: Mr. Satan y "Total ... Pts."
  Imagen ahora;      // 668 #19: "Now!! ... Pts."
  Imagen tienes;     // 668 #21: "You have ... Pts."
  Imagen cifras;     // 668 #22: 0-9 (5 x 2 de 48 x 64)
  Imagen chibis;     // 668 #24: caras de los 11 retadores (7 x 3 de 72 x 72; despues, sus siluetas)
  Imagen letrero;    // 666 #6: letrero de madera del titulo
  Imagen pulsa;      // 666 #5: "Pulsa START" del titulo
  Imagen dificultad[3];   // 666 #7-9: Novato, Experto, Avanzado
  Imagen barra;      // 666 #3: degradado negro vertical (16 x 88)
  Imagen copyright;  // 666 #4: "(c) 1989 BIRD STUDIO/SHUEISHA... (c) BANDAI 2002" del titulo (488 x 40)
  Imagen tienes_peq; // 668 #20: "You have ... Pts." pequeno
  Imagen cifras_peq; // 668 #23: 0-9 pequenas (4 x 3 de 40 x 40)
  Imagen clave;      // 668 #25: tablon "Password"
  Imagen letras[2];  // 668 #3 minusculas y '-', #4 mayusculas (7 x 4 de 48 x 64)
  // prologo "Mr. Satan's Style" (SATAN DEMO MAIN, satanmode.c 0x22E380): filosofia (voz 1261), cara de Mr. Satan,
  // y 6 consejos (#8 "The Art of Combat!" voz 1262; #10-#14 voces 1264-1268, cada uno tras el golpe 1263)
  Imagen estilo;      // 668 #6
  Imagen cara_satan;  // 668 #7
  Imagen consejos[6]; // 668 #8, #10, #11, #12, #13, #14
};

struct DatosB1 {
  std::string iso, elf, afs;      // de donde salio (para el registro)
  std::filesystem::path ruta;     // la ISO (voces de ADX_JP.AFS, se leen al oirlas)
  uint32_t tabla = 0;             // offset de la tabla en el ELF
  std::vector<CombateB1> combates;
  std::vector<CombateB1> historia;   // combates del modo historia (los registros de antes de la tabla del modo)
  std::string idioma;             // "es", "en"... de las imagenes
  std::vector<Imagen> charlas;    // banco de charlas: 2 por combate desde el 2.o (charla de Mr. Satan, aviso)
  Imagen logo, fondo, satan;      // pantalla del modo: logo, fondo azul y dibujo de Mr. Satan
  ResultadoB1 res;
};

// ISO de Budokai 1 en ps2_games (junto a dbz3.exe; DBZ3_PS2_GAMES la sustituye en las pruebas); vacio si no hay.
std::filesystem::path BuscarIsoB1();

// Juegos de PS2 del jugador elegidos con un boton (launcher o el propio juego): ps2_games/<nombre>.ruta guarda la ruta de
// su ISO (no se copia nada) y los buscadores la aceptan como si estuviera ahi. juego: 0 = Budokai 1, 1 = Budokai 2,
// 2 = Infinite World. Registrar: "" si vale; si no, el motivo para el jugador (su ISO es de otro juego...).
std::filesystem::path IsoPs2(int juego);   // la que usara el juego (vacia = falta)
std::string RegistrarIsoPs2(int juego, const std::filesystem::path& iso);
// Dialogo para elegirla (Windows); bloquea hasta que el jugador cierra. "" = elegida y valida, "-" = cancelado.
std::string ElegirIsoPs2(int juego);
// Lo mismo sin parar el juego (un hilo): el aviso dice que hacer mientras ("Elige la ISO en la ventana...") y despues el
// resultado ("¡Listo!" o el motivo). Vacio si no se ha pedido nada.
void ElegirIsoPs2EnSegundoPlano(int juego);
std::string AvisoEleccionPs2();
// Lee la ISO (sin extraerla): tabla de combates del ELF y, en el idioma `lang` (es/en/fr/de/it), las imagenes.
bool LeerLeyendaSatan(const std::filesystem::path& iso, const std::string& lang, DatosB1& out, std::string& error);
// ID de B3 que hace de un personaje de B1 (el donante del importador; Zarbon/Dodoria: Cooler/Nappa).
int B1aB3(int id_b1);
// Voces de B1 (ADX_JP.AFS de la ISO; satanmode.c: charla del retador r (1..10) = 1269 + 2 (r - 1) y la siguiente,
// presentacion 1261): las lee, decodifica y suena en otro hilo, una tras otra (corta la anterior). Volumen 0..1.
void OirVozB1(const std::filesystem::path& iso, std::vector<int> entradas, float volumen);
void CallarVozB1();
// F5: entrada `entrada` (ADX) del AFS `afs` del juego `juego` (AfsPs2), en otro hilo por su propio waveOut (se mezcla
// con lo demas). Volumen 0..1. CallarAdxPs2 corta todos los que suenan.
void OirAdxPs2(const std::filesystem::path& juego, const std::string& afs, int entrada, float volumen);
void CallarAdxPs2();
// Musica (ADX estereo en bucle) de los modos de PS2: una a la vez; la nueva corta la anterior.
void OirMusicaPs2(const std::filesystem::path& juego, const std::string& afs, int entrada, float volumen);
void CallarMusicaPs2();
// Efecto corto PS-ADPCM (VAG) de un banco IECS: entrada `cuerpo` del AFS = datos, `cuerpo`+1 = indice; `id` = sonido del
// banco. Los del mismo `tipo` (0..7) se cortan entre si. periodo_ms > 0: se repite cada tanto hasta cortarlo.
// solo_decodificar: valida y registra el recurso sin abrir salida de audio (pruebas silenciadas).
void OirSonidoPs2(const std::filesystem::path& juego, const std::string& afs, int cuerpo, int id, int tipo, float volumen,
                  int periodo_ms = 0, bool solo_decodificar = false);
void CallarSonidosPs2();

// Infinite World (ficha 02 C): titulo y texto de una mision (imagenes de DATA_USA.AFS de la copia del jugador).
struct MisionIW {
  int numero = 0;          // 0 = "Training No.1"; la entrada de DATA_USA es 132 + numero
  Imagen titulo, texto;
};
struct DatosIW {
  std::string origen;
  std::vector<MisionIW> misiones;   // en el orden pedido
};
// ---- Lectura generica de los juegos de PS2 del jugador (B1, B2, IW; ISO o carpeta extraida), para portear sus
// pantallas con sus graficos originales leidos en marcha (nunca se reparten). Las texturas 2D de los tres son #AMT
// (4/8 bits con CLUT, sin swizzle). Hilo propio: cada AfsPs2 abre su fichero.
class AfsPs2 {
 public:
  bool Abrir(const std::filesystem::path& juego, const std::string& afs);   // p. ej. "DATA_CMN.AFS" (sin mayusculas)
  size_t Cuantas() const;
  std::vector<uint8_t> Entrada(size_t k, uint32_t n = 0xFFFFFFFF);
 private:
  bool AbrirUna(const std::filesystem::path& juego, const std::string& afs);
  struct Impl;
  std::shared_ptr<Impl> d_;
};
std::vector<uint8_t> HijoAmb(const std::vector<uint8_t>& amb, uint32_t c);   // hijo c de un #AMB (vacio si no hay)
int CuantasAmt(const std::vector<uint8_t>& amt);                              // texturas de un #AMT (-1 si no es)
Imagen TexturaAmt(const std::vector<uint8_t>& amt, int j);                    // RGBA de la j-esima
std::filesystem::path BuscarJuego(const std::string& clave);                  // ps2_games/<nombre con `clave`>
// Carpeta extraida o ISO de Infinite World en ps2_games (vacio si no hay).
std::filesystem::path BuscarIW();
bool LeerMisionesIW(const std::filesystem::path& iw, const std::vector<int>& numeros, DatosIW& out, std::string& error);

// Para el HUB (parche en docs/14_frentes/pasarela.md): disponible = hay ISO de Budokai 1 y se pudo leer.
bool Disponible();
const char* Motivo();
bool Start();
bool Busy();

// ---- Piezas comunes de la pasarela (pasarela.cpp) ----

// Datos de la ISO de B1 ya leidos (nullptr = sin ISO o sin leer); pide la lectura si hace falta.
std::shared_ptr<const DatosB1> DatosLeidos();

// Avance: user_data/dbz3/pasarela/progreso.toml (una clave por modo; se reescribe entero conservando las demas).
int LeerProgreso(const std::string& clave, int defecto = 0);
void GuardarProgreso(const std::string& clave, int valor);
// Escalafon del Dojo (rangos al estilo Tekken, auditoria8/A): puntos de escalafon (PE) acumulados en progreso.toml
// (rango_pe), que nunca bajan. 11 peldanos: Kyu 5..Kyu 1, Dan 1..Dan 5 y Maestro.
int RangoPE();
int RangoNivel(int pe);              // 0 = Kyu 5 .. 10 = Maestro
const char* RangoNombre(int nivel);  // en el idioma del juego
// Anti-farmeo: suma `pe_por_paso` por cada paso que `valor` mejora lo guardado en `clave` (y `pe_primera` la primera
// vez); repetir lo mismo no da nada. Devuelve los PE sumados.
int RangoMejora(const std::string& clave, int valor, int pe_por_paso, int pe_primera = 0);

// Personaje de un mod instalado (mods/_roster/roster.toml + personaje.toml del mod) por su origen:
// fuente "b1"/"b2"/"iw"... y clave del importador ("13" Zarbon de B1, "XGXL" Fat Gotenks de B2). -1 = no esta.
int IdPort(const std::string& fuente, const std::string& clave);
// Traje en el juego (rec+18) del traje extra de la carpeta de mod `carpeta` (traje.toml) del personaje `id`; -1 si no
// esta instalado.
int TrajeExtra(int id, const std::string& carpeta);
// Nombre para mostrar (los de B3 en el idioma del juego; los de mods, el de su autor).
std::string NombreDe(int id);

// Reglas del combate en curso que se aplican desde fuera del juego (memoria del luchador; sin ganchos).
struct Reglas {
  int nivel_com = -1;         // nivel de la IA del COM (mgr+35573: 0 Novato, 1 Experto, 2 Avanzado); -1 el de serie
  float ki_rival = 0;         // ki inicial del rival en barras de B1 (300 = 3); 0 = el de serie
  float ki_jugador = 0;       // ki inicial del jugador del registro del original (300 = 3 barras); 0 = el de serie
  float armadura_rival = 0;   // parte del dano al rival que se anula (0.5 = la mitad): Androide 16
  bool solo_ki = false;       // el jugador solo hace dano con ki (golpes al 25 %): Mr. Satan herido
  float ki_lento = 0;         // parte del ki que gana el jugador que se pierde (0.5): Piccolo
  int traje_jugador = 0;      // traje del jugador (rec+18; los extra de traje.toml van tras los del juego)
  int traje_rival = 0;
  bool traza = false;         // pruebas: vida y ki de los dos cada segundo en el registro
  bool capsulas = true;       // cada lado con la lista Normal de capsulas de su personaje (tecnicas, como el Duelo)
  int capsulas_rival[8] = {}; // pruebas: [cuantas, 7 IDs] en lugar de la lista Normal del rival
  int golpes = 0;             // reto: golpes al rival para cumplirlo (al llegar, el combate acaba a favor)
  bool sin_atacar = false;    // reto: si el jugador quita vida al rival, falla (el combate acaba en contra)
  std::atomic<int>* contador = nullptr;   // golpes al rival en vivo (marcador del reto en pantalla)
  bool puntos_b1 = false;     // Leyenda de Mr. Satan: puntos por suceso como B1 (HIT, COMBO FIN, THROW...; Fin::puntos)
  // casillas de condicion del Dragon World de B2 (frente R2)
  float vida_baja = 0;        // parte de su vida maxima que pierde el jugador por segundo (no baja de 1)
  bool sin_bloqueo = false;   // el boton de guardia del jugador no hace nada (el que solo da G, en su configuracion)
  bool ki_cero = false;       // el jugador empieza con el ki a 0
  float ataque = 0;           // dano extra que hace el jugador (daga de B2: 0.10 = +10 %)
  float defensa = 0;          // parte del dano que recibe el jugador que se anula (armadura de B2: 0.10)
};
// Fin del combate pedido (lado 0 = jugador).
struct Fin {
  int ganador = -1;           // -1 = no acabo (salio desde la pausa o no empezo)
  int vida[2] = {0, 0}, vida_max[2] = {0, 0};
  int ki[2] = {0, 0};         // ki al acabar (mismo indice que vida; la leyenda lo arrastra: 0xFFFF de B1)
  int segundos = -1;          // tiempo que sobraba; -1 sin limite
  bool empezo = false;
  int golpes = 0;             // golpes que quitaron vida al rival
  bool ataco = false;         // el jugador quito vida al rival (reto sin atacar)
  int puntos = 0;             // Reglas::puntos_b1: puntos de los sucesos del combate (sin LIFE ni TIME), >= 0
  int combo_max = 0;          // golpes del combo mayor del jugador (como el "N HITS" de B3; todos los combates)
  int combo_dano_max = 0;     // vida que le quito al rival ese combo
  // Contadores exactos del rango de IW (LoadWaitBattleMainExitEnd, EU 0x208EA0): +0x24 = mayor racha de carga de ki
  // del RIVAL (cuadros; +0x8c44, Battle_KiChargeTick EU 0x2334F0) y +0x28 = mayor dano de un combo del RIVAL
  // (+0x8c48, HitCountFilter EU 0x233320). El +0x1c (golpe directo del jugador) es `golpes > 0` y el +0x20
  // (jugador +0x8c3c) solo lo escriben los guiones MMI2 (RAMEvents EU 0x22F9D0): 0 en el port.
  int rival_ki_max = 0;
  int rival_combo_dano_max = 0;
};
// Vigila el combate pedido con dbz3::hub::RequestBattleEx (hilo propio): aplica las reglas mientras se pelea y llama
// a `fin` cuando el conector suelta el combate. `quien` = prefijo del registro ("pasarela", "misiones").
void VigilarCombate(const char* quien, const Reglas& reglas, std::function<void(const Fin&)> fin);
// Estado del combate vigilado (mgr+35128: 1 vuelo, 2 entradas, 3 READY, 4 pelea, 5-7 final); 0 = ninguno.
int EstadoCombate();
// Escenario de B3 (indice del registro de escenarios) cuyo mapa es `mapa` (entrada de data_cmn): 50 Juegos de Cell,
// 60 Namek, 46 Templo de Kami... (stage_ext.h). 0 si no hay.
int EscenarioPorMapa(int mapa);
// Escenario de B3 para el nombre de un escenario de B1 ("CHG_NAMEC" -> Namek).
int EscenarioB3(const std::string& nombre_b1);
// Forma inicial de B3 (rec+20) del personaje `id` de B3 para la forma `forma_b1` de su registro de B1.
int FormaB3(int id, int forma_b1);

// Dibujo (hilo de la interfaz): cara del marcador (del juego o de su mod) en un marco del juego, con el nombre debajo.
ImTextureID CaraDe(int id);
void DibujarCara(ImDrawList* dl, const ingame::style::Area& a, int id, float x0, float y0, float w, bool on,
                 const char* nombre, float t, bool espejo);

}  // namespace dbz3::pasarela
