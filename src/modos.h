// dbz3 - API de los modos del HUB (modos.cpp: retos, ranking, laboratorio, torneo) y de los retos de rol
// (rpg.cpp: selector, Simulacion, Tienda, Heroe, Torres, Abyss, Kumite de jefes, Torneo de 4). Solo US.
// Docs: docs/14_frentes/modos.md y docs/14_frentes/rpg.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawList;
namespace dbz3::ingame::style {
struct Area;
}

namespace dbz3::modos {

int TorneoIdReal(int plaza);   // modos.cpp (S5-I1): ID real de una plaza COM del Torneo con personaje de mod, -1 = el de su casilla

// 0-2: los retos de modos.cpp; 3-10: los de rpg.cpp (StartPropio), cada uno con su record (10: Mision 100).
enum Tipo : int {
  kTorre = 0, kSupervivencia = 1, kKumite = 2, kSimulacion = 3, kTienda = 4, kHeroe = 5, kTorres = 6, kAbyss = 7,
  kJefes = 8, kTorneo4 = 9, kMision = 10, kTipos = 11
};
enum Fase : int { kNada = 0, kCombate = 1, kEntre = 2, kFin = 3 };
enum Resultado : int { kNinguno = -1, kDerrota = 0, kVictoria = 1, kAbandono = 2, kGuardado = 3 };

struct Estado {
  int fase = kNada, tipo = kTorre;
  int ganados = 0, total = 0;   // combates ganados / del reto
  int rival = -1;               // ID del rival en curso o del siguiente (al terminar, el ultimo)
  int resultado = kNinguno;     // del ultimo combate; al terminar, el del reto
  int vida = 0, vida_max = 0;   // del jugador para el combate siguiente
  int jugador = 0;
  int recibido = 0;             // dano que recibio el jugador en el ultimo combate (0 = perfecto)
  int escenario = 0;            // del combate en curso o del siguiente (dbz3::escenarios::Nombre)
  int propio = 0;               // reto de rpg.cpp
};
// Puesto del ranking (Elo por personaje).
struct Puesto {
  int id = 0, elo = 1000, ganados = 0, perdidos = 0, empates = 0;
};
// Reto de rpg.cpp: sus combates y las reglas del siguiente (efectos durante la pelea, en sub_820FD678).
struct Pelea {
  int rival = 0, traje = 0, escenario = 0;
  int vida = 0;       // del rival (0 = la normal)
  int tiempo = 0;     // limite en s (0 = sin limite)
  int regen = 0;      // vida/s que recupera el rival
  int objetivo = 0;   // aguantar estos segundos = victoria
  bool clima = false;  // el cielo se oscurece (clima.cpp)
  float gigante = 0;   // escala del rival (gigantes.cpp; 0 = normal, 0.5..3)
  bool intro = false;  // intro de cine de 10 s antes del combate (cine.cpp)
  int forma = 0;       // studies 8 (equipo A): forma inicial del rival (hub.cpp rec+20/+22; fusiones nativas de B3:
                       // 64 f3 Gotenks, 70 f4 Gogeta SS4, 72 f2 Vegito, registros 64..104 del char372)
};
struct Reglas {
  int dano_hecho = 100, dano_recibido = 100;   // % del dano que hace / recibe el jugador
  int armadura = 0;                            // % del dano del jugador que absorbe el rival (jefe)
  int ventana = 0;                             // > 0: cada `ventana` s la armadura cae 3 s (hueco castigable)
  int ki_drena = 0;                            // % del tope de ki que pierde el jugador cada segundo (< 0: lo gana)
  int ncaps = -1;                              // capsulas del reto para el jugador (-1 = ninguna)
  int caps[7] = {};
  bool reemplazar = false;                     // true: solo esas; false: se suman a las suyas (hasta 7)
};

// Texto en el idioma del juego (es, en, fr, de, it; el resto en ingles).
const char* L(const char* es, const char* en, const char* fr, const char* de, const char* it);

bool Start(int tipo, int jugador, int traje);   // 0 torre, 1 supervivencia, 2 kumite (sigue el guardado si hay)
// Reto propio (tipo 3-9) desde el combate `ganados`; siempre con pausa entre combates (Continue/Abort).
// pausado: no pide el combate; se queda entre combates (reanudar una partida: su tienda/cartas/cuadro primero).
bool StartPropio(int tipo, const std::string& nombre, const std::string& palabra, int jugador, int traje, int vida,
                 int vida_max, const std::vector<Pelea>& peleas, int ganados, bool reintentar, int premio,
                 bool pausado = false);
bool Continue();
void Abort();
Estado Get();
void SetVida(int vida, int vida_max);
// Luchador del combate siguiente (entre combates de un reto propio; Modo Heroe: otro estilo). Si ya estaba pedido
// encadenado con el anterior, se cancela y se pide con el nuevo al seguir.
void SetJugador(int id);
void SetReglas(const Reglas& r);
void SetSiguiente(const Pelea& p);              // rival del combate siguiente (Torneo de 4: el finalista)
bool ArmaduraActiva();                          // armadura del jefe ahora mismo (marcador)
void CerrarFin();                               // la pantalla final de un reto propio ya se vio
void UltimoDuelo(int* id, int* traje);          // ultimo luchador del Duelo
bool EsJugable(int id);
std::string Nombre(int id);                     // nombre del luchador en el idioma del juego
const char* NombreModo(int tipo);
bool Record(int tipo, int* id, int* mejor, int* total, int* completados);
bool Guardado(int tipo, int* id, int* ganados);
bool PerfilJugador(int* elo, int* tramo, const char** nombre_tramo);   // ficha 01 (panel Online)
int Ranking(Puesto* out, int max);

}  // namespace dbz3::modos

namespace dbz3::rpg {
void Premio(int zeni);       // Zenie al monedero del HUB (reto completado)
bool Activa();               // tiene una pantalla que ensenar (menu principal)
bool Busy();                 // selector o reto propio abierto (el HUB espera)
bool Heroe();                // pantallas del Modo Heroe (su fondo propio)
bool Mision100();            // pantalla de Mision 100 (su fondo y anfitrion propios)
void AbrirSelector(int entrada);   // 0 torre, 1 supervivencia, 2 kumite, 3 Mision 100 (entradas del HUB)
void AbrirHeroe();   // Modo Heroe como entrada propia del HUB (no depende del Kumite)
bool ProbarMision(int n, int jugador);   // PRUEBAS: empieza la mision n (1-100) de la Mision 100
bool EquiposMision();               // la mision en curso lleva equipos (tagteam_slots.cpp no los borra)
void Dibujar(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado);
// durante la pelea de un reto propio: sus reglas en la linea y (px del area) del bloque del HUD de los modos
void Hud(ImDrawList* dl, const dbz3::ingame::style::Area& a, float y);
}  // namespace dbz3::rpg
