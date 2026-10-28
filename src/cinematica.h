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
  bool sin_ayuda = false;    // DS13: no dibujar el rotulo "A Siguiente / START Saltar" en este paso (las tomas del READY de B1 no lo tienen)
  // DS16: pose de brazos de B1 compuesta sobre el hueso del luchador 1 (Mr. Satan): las direcciones de brazo y antebrazo
  // medidas en la toma 1 de B1 (0xC0) se aplican a la tabla de huesos del dibujo cada cuadro. El resto del cuerpo sigue
  // con el clip de B3 del paso.
  bool brazos_b1 = false;
  // DS16: separacion final de los dos luchadores en este paso (0 = la de la escena). Se interpola en todo el paso desde
  // la separacion de la escena: B1 los lleva al centro del ring antes del READY (30 -> 10 unidades).
  float distancia_final = 0.0f;
  bool fov_acerca = false;   // A3b: B3 proyecta con su 0,785 fijo (escribir mgr+0x1C4 no cambia nada): el fov de cam_fov se logra acercando el ojo al objetivo en tan(fov/2)/tan(0,3925)
  // DS168: usa el mismo camino de camara directa que `fov_acerca` (y publica la camara para los props) pero NO acerca
  // el ojo al objetivo. En la toma del coche de la llegada el sujeto esta al lado de la camara: acercar el ojo 39,5 %
  // (0,491 -> 0,785) lo dejaba detras y, como el objetivo baja bajo el suelo, hundia la camara en el terreno.
  bool fov_acerca_sin_mover = false;
  // DS31: no dibujar el cuerpo del lado 2 (X+3261/X+4925, los flags que usa el juego para el perdedor y el relevo).
  // En el final de B1 Cell solo existe en el primer plano (su clip dura 650 cuadros): despues no sale en ningun
  // encuadre (el de la reportera incluido) y en el puerto el luchador de B3 seguia ahi.
  bool ocultar_lado2 = false;
  // s9b: true = ojo/objetivo van en el marco de los dos luchadores (origen = punto medio, x hacia el lado 2, y arriba,
  // z = perpendicular a su izquierda) en vez de en coordenadas del mundo: asi una toma vale en cualquier escenario.
  bool rel = false;
  // DS143: ancla de la camara de B1 sobre un luchador. `cam_ancla` son las posiciones de ese actor en el mundo de B1
  // por cuadro ({cuadro, x, y, z}) y `cam_ancla_lado` el lado del port (0/1) que lo representa; la camara se desplaza
  // cada cuadro por (posicion del luchador - posicion de B1). Asi las tomas de B1 valen con los luchadores en su sitio
  // del escenario de B3 (sin sacarlos del ring). -1 = camara de B1 tal cual (planos del ring).
  std::vector<std::array<float, 4>> cam_ancla;
  int cam_ancla_lado = -1;
  // DS185: el objetivo de B1 es un punto del CUERPO del actor, y el de B3 se DIBUJA en la pose de la escena (Satan en
  // guardia, con la cadera ~3 u mas baja que el bind): con el `y` absoluto de B1 el luchador se hundia en el 2.o tramo
  // del clip 5 y en el 7 (solo el pelo por el borde inferior; DS170). El objetivo se lleva a la MISMA fraccion de la
  // recta cintura->cabeza del cuerpo DIBUJADO y el ojo se desplaza lo mismo (el angulo de la toma no cambia).
  // Los dos puntos salen del esqueleto que pinta el juego ([X+2952]+1032, anclajes 0=cintura y 3=cabeza de X+2956);
  // NO de la tabla X+1616, que solo se refresca en los estados 3/4/5/8 y en la llegada conserva la escena anterior.
  // `cuerpo_suelo_b3` = altura de la cadera del modelo de B3 sobre la suela que se resto en `cam_ancla` (Satan 11,438);
  // `cuerpo_cabeza_b1` = hueso de la cabeza de B1 sobre su cintura (STN_HEAD 6,942 - STN_WAIST -0,972 = 7,914).
  // DS186: `cuerpo_cintura_b1` = cintura de B1 sobre el suelo de la escena en las tomas `rel` (marco de los luchadores)
  // o cuya camara va girada sobre el ancla (previa): alli `cam_ancla` no existe y el dato va aparte. 0 = no usar.
  bool objetivo_cuerpo_b3 = false;
  float cuerpo_suelo_b3 = 0.0f, cuerpo_cabeza_b1 = 7.914f;
  float cuerpo_cintura_b1 = 0.0f;
  // DS145: giro (radianes, alrededor de Y) que se aplica al desplazamiento de la camara de B1 relativo al actor antes de
  // llevarlo al sitio del luchador de B3. B1 coloca a los actores de la escena de los Juegos de Cell mirando a lo largo
  // de Z (Cell en z=20,3 mira a Goku en z=-12) y B3 enfrenta a los dos lados a lo largo de X: +pi/2 lleva la toma de B1
  // al mismo lado del cuerpo de B3 (la revision de DS145: sin el, el primer plano de Cell salia casi de perfil).
  float cam_ancla_giro = 0.0f;
  std::function<void()> al_empezar;   // S5-A3: se llama al empezar el paso (hilo del juego); p.ej. la musica del combate
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
// DS178: al acabar un combate "solo escena", saltar la victoria/resultado del combate de B3 (estados 5-7: 3,0 s de
// pose + 0,2 s) llamando a la salida al resultado del propio juego (sub_820FF820, escribe el estado 8 y rellena el
// resultado) en cuanto el combate llega al estado 5. Una sola vez por combate; DBZ3_SIN_RESULTADO_RAPIDO=1 lo deja como
// estaba. QuitarResultadoRapido lo desarma si el combate acaba sin pasar por el estado 5.
void PedirResultadoRapido();
void QuitarResultadoRapido();
// DS30: camara exacta del paso en curso (ojo, objetivo y giro) para lo que se pinta fuera del juego (props del final de
// la Leyenda de Mr. Satan). Evita leer mgr+0x60: la camara del guest alterna entre el valor nativo y el de la escena (la
// mezcla 0,8/0,2) y el prop temblaba. false si no hay escena con camara fijada (`fov_acerca`).
bool CamaraEscena(float ojo[3], float objetivo[3], float* giro);
// DS145: desplazamiento que el ancla del paso en curso aplica a la camara de B1 (posicion del luchador de B3 menos la
// del actor de B1). Los props que se pintan en el mundo de B1 (coche/humo de la llegada) tienen que moverse lo mismo.
// false si el paso no tiene ancla o si el ancla lleva giro (las tomas de la previa, sin props).
bool AnclaDelta(float d[3]);
// S1: camara del primer plano de la cara de la sonda (DBZ3_TEST_CARA) en un combate nativo, sin escena. DS154: con
// DBZ3_TEST_CARA_HUESO (codo/mano derecha o izquierda) y dbz3_cam.req encuadra de cerca las articulaciones del brazo.
void AplicarCamaraSonda(uint8_t* base);

}  // namespace dbz3::cinematica
