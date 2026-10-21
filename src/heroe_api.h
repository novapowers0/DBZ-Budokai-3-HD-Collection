// dbz3 - Modo Heroe: lo que las pantallas al estilo Ultimate Tenkaichi (heroe_ui.cpp, studies 8 H3/H4) necesitan de
// heroe.cpp. heroe.cpp guarda el heroe de la ficha activa en sus globales (cuerpo, traje, estilo, progreso); el aspecto
// UT (HSV por parte, logo, scouter, pelo y sus 2 colores) vive directamente en la ficha de cac.toml (cac::Heroe).
// Todo se llama con el candado de rpg.cpp (g_mu) puesto.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace dbz3::heroe {

// --- aspecto (Create) ---
int Cuerpo();                 // ID del cuerpo (luchador cuyo modelo lleva el heroe)
std::string NombreCuerpo();   // G06a: nombre de la constitución/cara del catálogo UT (100..105)
int Traje();                  // traje del cuerpo (0..NumTrajes-1)
int NumTrajes();              // trajes del cuerpo (tabla del select si esta en memoria)
void PonerTraje(int t);
const std::string& Nombre();  // nombre del heroe

// --- sagas para el mapa (studies 8 fase 2, H5) ---
int Saga();                                           // la siguiente por jugar (las de antes, completadas)
int SagasTotal();
const char* NombreSaga(int saga);
int MaestroQueEntrena(int saga);                          // ID del maestro que se entrena (-1 ninguno)
std::vector<std::pair<int, int>> CombatesDeSaga(int saga);   // (rival, escenario) de cada combate

// --- ficha -> modelo (studies 8 fase 2, H2) ---
std::string ArgsFicha(int id, int traje);   // texto de vista_heroe.py --ficha
bool FichaNativa();                         // sin colores/pelo/logo/scouter: el modelo del juego tal cual
int FichaNumero();                          // 1..N (mods/heroe_fichaN)
int TrajeCombate(int* estado);
std::string ArgsPropio();                   // studies 9: ArgsFicha + nombre: lo que monta mods/heroe_propio (el heroe con ID propio)
int IdPropio();                             // su ID real en el roster de esta sesion (-1: no esta montado)
int DonanteCombate();                       // donante (moveset) con el que se monta el heroe propio
int Luchador(int estilo);                   // el que pelea: IdPropio() o, sin el, el estilo              // 0 nativa, 1 en combate, 2 al reiniciar, 3 sin hacer

// --- movesets (studies 8 fase 2): -1 = el del estilo; el resto, donantes validos (char372 con moveset) ---
int EstiloId();
int MovesetId();
std::vector<int> Movesets();
void PonerMoveset(int id);
int CapsulasMoveset(int id);   // capsulas por defecto del donante (char96 +80)

// --- maestros (Select Master, H4) ---
constexpr int kMaestros = 6;
struct InfoMaestro {
  int id = -1;                 // ID del luchador
  int dano = 100, recibido = 100, vida = 0, ki = 0;   // modificadores (%, vida extra, ki < 0: se recarga solo)
  int capsula = 0;             // capsula de tecnica que da entrenado
  const char* capsula_n = "";  // su nombre
  const char* tecnicas = "";   // sus tecnicas
  int entreno = 0;             // % de entrenamiento (0..100)
  bool entrenado = false;      // 100 % o saga completada
  bool desbloqueado = true;    // s9: se elige solo al llegar a su saga (o entrenado)
  int saga = -1;               // s9: la saga en la que se desbloquea (-1: desde el principio)
};
InfoMaestro Maestro(int i);
int MaestroActual();           // indice del maestro del estilo elegido, -1 si el estilo no es maestro
int EntrenoMinimo();           // 60: % con el actual para poder cambiar
bool PuedeCambiarA(int i);     // regla del 60 %
bool ElegirMaestro(int i);     // el estilo pasa a ser el maestro i (false: bloqueado por la regla)

}  // namespace dbz3::heroe
