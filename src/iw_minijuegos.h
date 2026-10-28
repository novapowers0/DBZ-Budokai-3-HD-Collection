// dbz3 - Minijuegos del mapa de Dragon Mission de Infinite World (src/iw_minijuegos.cpp; frente P4, 2026-10-16).
// Se registran solos en pasarela::RegistrarMinijuegoIW: elegir un "?" del mapa (pasarela_iw.cpp) llama a Empezar.
#pragma once

#include <cstdint>

struct ImDrawList;

namespace dbz3::iwmini {

// Empieza el minijuego de la casilla del #DMD 1093 (clave 0x7D0-0x7E1, fila). false = aun no portado.
bool Empezar(int clave, int fila);
// Cargando, en su combate o con el resultado sin recoger.
bool Activo();
// Gancho de sub_82103908 (tagteam.cpp, cambiar la accion del luchador): true = no dejar que el jugador entre en el
// andar de combate de B3 (acciones 2/3) durante el andar libre de IW; sus pasos reiniciaban cada cuadro la carrera
// de IW (auditoria7/A). La marcha entera la pone Fotograma.
bool AnularAndar(uint8_t* base, uint32_t x, uint32_t accion);
// Dificultad de la ficha del mapa para el siguiente minijuego (PantallaDM::Dificultad: 0 Very Easy .. 4 Very Hard),
// tal cual: es el mismo indice que el descriptor +0xC del MSNMW (0..5, 5 = Z; lista de 334/1 t16).
void Dificultad(int ficha);

struct Resultado {
  int clave = 0, fila = 0;
  bool superado = false;   // llego a la meta antes del limite
  int centesimas = 0;      // TIME (lo que tardo; centesimas, como +0x74 del bloque de resultados)
  // `puntos` es el valor historico que el mapa usa para SCORE y ZENI. Las pantallas de resultados de este fichero
  // usan `score` y `zeni` (separados): `score` = TOTAL SCORE de Results_ComputeTotalsAndRewards (EU 0x2E9AF0) y
  // `zeni` = premio de su +0x13 (tabla del rango x multiplicador de dificultad). -1 = no informado (se usa `puntos`).
  int puntos = 0;
  int score = -1;
  int zeni = -1;
  int saldo = -1;          // monedero de IW tras sumar el premio (fila TOTAL ZENI); -1 = LeerProgreso("iw_zeni")+zeni
  int rango = -1;          // 0 C, 1 B, 2 A, 3 S, 4 Z (como PantallaDM::Rango); -1 = el modulo no lo da (por puntos)
  int monedas = 0;         // objetos cogidos
  int objetos = -1;        // objetos de la fila OBJECTS/BRICKS DESTROYED; -1 = esa pantalla no la lleva
  int objetos_total = -1;  // total de objetos (MSNMW: EU 0x323150 +0x165a); la fila es el % (objetos*100/total)
  int great = -1, good = -1, normal = -1;   // GREAT/GOOD/NORMAL SCORE (Results_ComputeTotalsAndRewards); -1 = sin filas
  int offence = -1, defence = -1;           // OFFENCE/DEFENCE SUCCESS RATE (QTE de dos carriles, DATA_USA 5/7 t20/t21)
  int dificultad = 2;      // 0 Very Easy .. 5 Z (descriptor +0xC; multiplica el premio de Results_*)
  bool con_tiempo = true;  // la pantalla ensena TIME (Results_* usa el tiempo; MSN019 no)
};
// Una vez, al acabar: el mapa lo ensena con PantallaDM::Resultado (TIME, RANK, SCORE).
bool TomarResultado(Resultado& r);

// Filas que enseña la pantalla de resultado de cada minijuego (claves 0x7D0-0x7E1). Tabla unica: la usan la pantalla
// del modulo (DibujarResultadoIW) y la del mapa (PantallaDM::Resultado). Evita que el mapa anada SCORE/TOTAL SCORE a
// los minijuegos que no los llevan (0x7D3/0x7D6/0x7D7/0x7D8/0x7DE/0x7E0...). Fuente por familia en el informe DS153.
enum FilaResultadoIW : unsigned {
  kResTime = 1u << 0,
  kResRank = 1u << 1,
  kResObjects = 1u << 2,
  kResScore = 1u << 3,
  kResGreat = 1u << 4,
  kResGood = 1u << 5,
  kResNormal = 1u << 6,
  kResEarned = 1u << 7,
  kResTotal = 1u << 8,
  kResOffence = 1u << 9,
  kResDefence = 1u << 10,
};
unsigned FilasResultadoIW(int clave);
// true = ese minijuego ya pinta su propia pantalla (iw_mini_disparo.cpp / iw_mini_botones.cpp): el mapa no repite otra.
bool ClaveConPanelPropioIW(int clave);

// Tablas de resultados de IW (Results_* del decompilado EU):
//  - RangoResultadoIW: GetRankIndexByThresholds (EU 0x2E92F0), cortes 60/70/80/90/100 -> 0..4; -1 si fuera (0 o >100).
//  - PremioResultadoIW: Results_ComputeTotalsAndRewards (EU 0x2E9AF0), premio {6500,6800,7100,7400,7700}[rango] x
//    {0.6,0.8,1.0,1.3,1.6,2.0}[dificultad], a la centena (resto < 50 abajo, si no arriba); sin rango = 100.
int RangoResultadoIW(int puntuacion);
int PremioResultadoIW(int puntuacion, int dificultad);
// Pantalla de resultados de IW con los sprites #AMA (DATA_USA 334/1, 333/0, 335+titulo y 4/0 para GREAT/GOOD/NORMAL):
// titulo, TIME (centesimas), letra de rango, objetos o GREAT/GOOD/NORMAL segun el tipo, EARNED ZENI contando y TOTAL
// ZENI (monedero). Dibuja en el rectangulo de pantalla (x0,y0,w,h) que representa el lienzo 640x448 del original.
// `t` = segundos desde que salio; `saltar` = todo a la vista (A). Devuelve true cuando el conteo ha terminado.
bool DibujarResultadoIW(ImDrawList* dl, float x0, float y0, float w, float h, const Resultado& r, float t,
                        bool saltar);

// Ganchos del hilo del juego (modo libre: un solo luchador con camara detras):
// clima.cpp sub_820C2058 (empieza el dibujo, tras la logica): camara, el otro luchador oculto y la salida.
void Fotograma(uint8_t* base, uint32_t estado);
// DS177: camara con la que se DIBUJA el campo de IW. La calcula este modulo cada cuadro (entrada, carrera y final); el
// gancho de sub_820F4300 (iw_mini_vuelo.cpp) la escribe en la struct de camara ACTIVA antes del original (DS163: es la
// fuente de la vista; escribir mgr+0x60 despues no cambia el 3D) y publica la foto del cuadro. `activa` = false la
// suelta y devuelve el fov de la struct (vuelve la camara del juego). `fov` = el que fija FovIW en mgr+0x1C4.
void CamaraMinijuego(bool activa, const float ojo[3], const float obj[3], float fov);
// DS177: vista (4x4) y fov vertical (rad) con los que el juego DIBUJO el ultimo cuadro (foto publicada tras
// sub_820F4300, ya con la vista aplicada). La escena 3D del panel debe usar ESTA, no mgr+0x60/+0x1C4. false = sin foto
// (EU o antes del primer dibujo): se sigue con ps2::CamaraCombate.
bool CamaraDelRenderIw(float m[16], float* fov);
// combate.cpp sub_8208FD58, antes (leido = false: devuelve el registro como lo dejo el juego) y despues de leer el
// mando `usuario` (leido = true: la palanca pasa a ser relativa a la camara).
void Mando(uint8_t* base, uint32_t usuario, bool leido);
// PRUEBA (el piloto de los entrenamientos de IW, pasarela_misiones.cpp): palanca izquierda a la derecha (1), a la izquierda
// (-1) o suelta (0) en el mando 0, escrita despues de cada lectura (como la marcha de P4): la cruceta no llena el
// historial analogico que mira el teletransporte de B3.
void PadPrueba(int dir);
// net/netplay_hooks.cpp sub_8221C178 (XInput del usuario 0) mientras Activo(): la palanca derecha (x, s16) para la
// camara; alli se le quita al juego (en la HD lanza tecnicas).
void PalancaDerecha(int16_t x, int16_t y = 0);
// Y de la palanca derecha (XInput s16, arriba +): cabeceo de la camara del vuelo.
int PalancaDerechaY();

// ---- Minijuegos de otros frentes (R12 vuelo 7/17, R13 disparo 3/8/9/15, R14 botones 5/6/11/12/13/18) ----
// Registro (en la inicializacion estatica de su archivo, p. ej.
//   const bool g_reg = (dbz3::iwmini::RegistrarModulo(0x7D7, &MiEmpezar), true);
// ): elegir esa casilla del mapa llama a `Modulo(clave, fila, dificultad 0..5 del MSNMW)`, que monta su minijuego y
// devuelve false si no puede (el mapa sigue). Mientras corre, Activo() es true (y la palanca derecha y la cruceta no le
// llegan al luchador de B3: ver PalancaDerecha); los ganchos de P4 (Fotograma, Mando, panel) no hacen nada. Al acabar
// llama UNA vez a Terminar(r) con r.clave/r.fila de su Empezar; el mapa lo recoge con TomarResultado (TIME, RANK, SCORE).
// Un modulo registrado tiene preferencia sobre el de P4 en esa clave.
using Modulo = bool (*)(int clave, int fila, int dificultad);
void RegistrarModulo(int clave, Modulo m);
void Terminar(const Resultado& r);
// Serie de camara del port (tools/lector_camara.py): los modulos la activan solo en la fase de juego (no en READY,
// intro ni resultado), para que `comparar_modo.py iw` compare lo mismo que el dorado del original.
void SerieJuego(bool activa);
// Familia B (una camara fija por ronda): la serie se escribe solo al cambiar de clip, como el TSV del original, para
// que cada ronda pese lo mismo (el piloto automatico tarda mas en la ronda 1 y desviaba las medianas).
void SeriePorClip(bool activa);
void SerieClip(int clip);
// HUD de IW (DATA_USA 6/0 de la copia del jugador, leido la primera vez): TIME arriba a la derecha con `segundos`
// (lo que queda) y abajo a la izquierda el aro con la cara del personaje `cara` (ID de B3) en su `forma` (PintarCara)
// y la barra amarilla llena `barra` (0..1). Hilo de la
// interfaz (dentro de un dibujo de ImGui), cada cuadro. Otras ayudas ya publicas: combate::OcultarHud(true/false)
// (sin el marcador de B3), combate::SetMuneco(1/-1) (rival quieto con mando virtual, sin IA), cine::Camara(base, ojo,
// obj) (camara del combate, desde clima.cpp), hub::RequestBattleEx (combate), ps2::* (modelos 3D de PS2, ps2_modelo.h).
void HudIW(float segundos, float barra, int cara = 0, int forma = 0);
// HUD de los minijuegos de VUELO (0x7D6/0x7E0), de DATA_USA 7/0 (#AMT) y 7/1 (#AMA0): TIME y el medidor del rival
// con su cabeza (t4 Vegeta / t5 Goten). `medidor` 0..1 = avance del rival; `variante` 0 = Vegeta, 1 = Goten.
// DS68: el rival del vuelo es ese sprite, no un luchador 3D. Hilo de la interfaz.
void HudVuelo(float segundos, float medidor, int variante, int aros = 0, int total = 0, int estado = 0);
// Cara del personaje `id` (ID de B3) en `forma` en el cuadrado (x0, y0, lado px): la de IW (6/0) si es de ese personaje
// y forma; si no, la nativa de B3 (caras/<id>) en circulo. `espejo` = volteada. Hilo de la interfaz.
void PintarCara(ImDrawList* dl, float x0, float y0, float lado, int id, int forma = 0, bool espejo = false);

}  // namespace dbz3::iwmini
