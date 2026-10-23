// dbz3 - Cinematicas en el motor de B3 (frente R3, 2026-10-16): una escena por pasos dentro del combate pedido
// (hub::RequestBattleEx), con los modelos y el escenario de B3. Al empezar la pelea (estado 4) los dos luchadores se
// quedan quietos (o hacen la pose de los botones del paso, la del propio personaje), la camara va por planos
// (general, luchador 1, luchador 2) con bandas de cine, y abajo sale el subtitulo (retrato + imagen del juego de
// origen o texto propio) con su voz. A la salta; al acabar la camara vuelve a la del combate y se pelea.
// Para la Leyenda de Mr. Satan (pasarela.cpp) y despues IW y B2. Ver docs/16_pasarela_modos/01_leyenda_mr_satan.md.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "pasarela.h"

namespace dbz3::cinematica {

enum Plano : int { kGeneral = 0, kLado1 = 1, kLado2 = 2 };

struct Paso {
  float segundos = 3.0f;
  int plano = kGeneral;
  uint16_t botones[2] = {0, 0};   // botones de Xbox (XInput: X golpe, Y patada, B ki, A guardia) de cada lado
  int anim[2] = {-1, -1};         // pose del propio personaje (codigo X+2928, kPose*) de cada lado; -1 su guardia
  int cara = -1;                  // retrato del subtitulo: ID de B3 (pasarela::CaraDe); -1 ninguno
  int cara_alt = -1;              // si `cara` no tiene imagen (el heroe sin su icono propio todavia): esta
  pasarela::Imagen texto;         // subtitulo: imagen del juego de origen (charla 669 de B1...)
  std::string linea;              // o texto propio (si no hay imagen)
  std::vector<int> voces;         // entradas de ADX_JP.AFS de B1 (pasarela::OirVozB1) que suenan con el paso
  // Escena de MOTOR de IW (tablas del ELF de la copia del jugador, volcadas con
  // studies 3/ENTREGAS/04_iw_escenas_motor/generar.py): duracion real de la linea en cuadros de 60 Hz
  // (tabla de clips del banco; 0 = usar segundos) y camara del clip de la linea, por cuadro.
  // {cuadro, x, y, z} para ojo/objetivo; {cuadro, radianes} para fov/roll. Vacio = camara de plano.
  int cuadros = 0;
  std::vector<std::array<float, 4>> cam_ojo, cam_obj;
  std::vector<std::array<float, 2>> cam_fov, cam_roll;
  bool estilo_b1 = false;    // A3b: sin bandas de cine y con el subtitulo de B1 (blanco con borde negro, abajo al centro de la imagen)
  bool fov_acerca = false;   // A3b: B3 proyecta con su 0,785 fijo (escribir mgr+0x1C4 no cambia nada): el fov de cam_fov se logra acercando el ojo al objetivo en tan(fov/2)/tan(0,3925)
  // s9b: true = ojo/objetivo van en el marco de los dos luchadores (origen = punto medio, x hacia el lado 2, y arriba,
  // z = perpendicular a su izquierda) en vez de en coordenadas del mundo: asi una toma vale en cualquier escenario.
  bool rel = false;
  std::function<void()> al_empezar;   // S5-A3: se llama al empezar el paso (hilo del juego); p.ej. la musica del combate
};

// S1 (Leyenda de Mr. Satan): cara del luchador 1 por tramos de la escena. En B1 la cara es una malla fija con textura;
// en B3 cada expresion es una malla (XSTN_L01/L18/...). El campo del registro del luchador que elige la malla se
// escribe desde el gancho Fotograma (hilo del juego, al empezar el dibujo) cada cuadro del tramo.
struct TramoCara {
  float desde = 0, hasta = 0;   // segundos desde el principio de la escena
  int valor = 0;                // valor del campo (ver docs de la sonda DBZ3_TEST_CARA)
};
// Y+B mantenidos (XInput, mando virtual del lado; como la pose del relevo del Tag Team, acciones 20/21). ponytail:
// medido 2026-10-16 al empezar la pelea: da la accion 15 unos 40 cuadros y vuelve a 0, sin carga visible. Sin usar.
constexpr uint16_t kCargarKi = 0xA000;
// Poses medidas con la sonda DBZ3_CINE_TRAZA (accion 15 + codigo; las del propio personaje): entrada del lado 1 y del
// lado 2 (estado 2 del combate) y victoria (estados 6-8).
constexpr int kPoseEntrada1 = 0x281, kPoseEntrada2 = 0x280, kPoseVictoria = 0x2A0;

struct Escena {
  std::vector<Paso> pasos;
  std::filesystem::path iso_voces;   // ISO de B1 de las voces
  float volumen = 1.0f;
  float distancia = 0;   // > 0: los dos a esta distancia (unidades de B3) durante la escena, con su punto medio
  // S1: cara del luchador 1 (Satan) por tramos. `campo_cara` = offset en el registro X del luchador (0 = apagado).
  int campo_cara = 0;
  std::vector<TramoCara> caras;
  // S1 (A3 O3 #1): al empezar y al acabar la escena (hilo del juego). La leyenda los usa para callar la mezcla de B3
  // mientras suena la musica de B1 (pasarela.cpp) y para devolverla.
  std::function<void()> al_empezar_escena, al_terminar_escena;
};

// La escena se reproduce al empezar la pelea del combate siguiente (cualquier hilo).
void Empezar(Escena e);
bool Activa();   // pedida o en curso
void Cancelar();
// Los ganchos ya llaman (Fotograma corrio alguna vez): sin ellos la escena no se ve y el modo sigue con sus pantallas.
bool Conectada();

// Ganchos (hilo del juego), una llamada cada uno: al empezar el dibujo (clima.cpp, sub_820C2058, como
// iwmini::Fotograma) y tras leer cada mando (combate.cpp, sub_8208FD58 despues de orig, como iwmini::Mando).
void Fotograma(uint8_t* base, uint32_t estado);
void Mando(uint8_t* base, uint32_t usuario);
// A3b: tras la funcion de camara del juego (sub_820F47E0, ver cinematica.cpp): pone la camara de la escena (ojo, objetivo, vista) sin mezcla.
void AplicarCamaraDirecta(uint8_t* base);
// S1: camara del primer plano de la cara de la sonda (DBZ3_TEST_CARA) en un combate nativo, sin escena.
void AplicarCamaraSonda(uint8_t* base);

}  // namespace dbz3::cinematica
