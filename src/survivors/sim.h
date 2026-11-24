// Dragon Budokai Survivors (prueba tecnica): simulacion determinista a paso fijo de 60 Hz. Cientos de enemigos persiguen al jugador en un
// plano (x, y; en el dibujo y = z del mundo). Sin ImGui ni nada del juego (tests/survivors_sim_test.cpp la compila sola) y sin
// memoria dinamica: todo son arrays fijos de kMax enemigos.
//
// Separacion entre enemigos y contacto con el jugador por REJILLA ESPACIAL uniforme centrada en el jugador (celda >= 2 radios: un
// enemigo solo puede tocar a los de su celda y las 8 vecinas). Cada paso se rehace con conteo + prefijos (orden de enemigos por
// celda en un array plano, sin punteros ni listas). Idea tomada de bones-ai/c-blob-survival (MIT); el codigo es propio.
#pragma once

#include <cstdint>

namespace dbz3::survivors {

class Sim {
 public:
  static constexpr int kMax = 2048;
  static constexpr float kDt = 1.0f / 60.0f;
  static constexpr float kRadio = 0.45f;          // enemigo (unidades de mundo; una figura mide ~1,8)
  static constexpr float kRadioJugador = 0.5f;
  static constexpr float kVelEnemigo = 2.5f;      // unidades por segundo
  static constexpr float kVelJugador = 5.0f;
  static constexpr float kAnilloMin = 22.0f, kAnilloMax = 26.0f;   // reaparicion (fuera de la pantalla)
  static constexpr float kLejos = 30.0f;                           // mas lejos: reaparece en el anillo
  static constexpr float kCelda = 1.0f;                            // >= 2 * kRadio
  static constexpr int kLado = 64;                                 // celdas por lado (cubre +-32 alrededor del jugador)

  void Iniciar(uint64_t semilla, int n);
  void PonerN(int n);              // sube: los nuevos salen en el anillo; baja: se quitan los ultimos
  void Paso(float dx, float dy);   // entrada del jugador (se acota a longitud 1)
  uint64_t Hash() const;           // FNV-1a del estado (determinismo)
  void Quitar(int i);              // el ultimo pasa a ocupar i (el que llama mueve igual sus propios arrays); invalida la rejilla

  // Enemigos de las celdas que tocan el rectangulo [x0,x1]x[y0,y1] segun la rejilla del ultimo Paso (los de fuera de la rejilla,
  // a mas de kLado/2 celdas del jugador, no salen). f(i) recibe el indice; el llamante hace la prueba exacta de forma.
  template <class F>
  void Recorrer(float x0, float y0, float x1, float y1, F&& f) const {
    const float m = rmax_;   // la separacion movio a cada uno menos que su radio desde que se hizo la rejilla
    int cx0 = int((x0 - m - gx_) / celda), cy0 = int((y0 - m - gy_) / celda);
    int cx1 = int((x1 + m - gx_) / celda), cy1 = int((y1 + m - gy_) / celda);
    if (x1 + m < gx_ || y1 + m < gy_) return;
    cx0 = cx0 < 0 ? 0 : cx0, cy0 = cy0 < 0 ? 0 : cy0;
    cx1 = cx1 >= kLado ? kLado - 1 : cx1, cy1 = cy1 >= kLado ? kLado - 1 : cy1;
    for (int cy = cy0; cy <= cy1; ++cy)
      for (int c = cy * kLado + cx0, fin = cy * kLado + cx1; c <= fin; ++c)
        for (int k = inicio_[c]; k < inicio_[c + 1]; ++k) f(orden_[k]);
  }

  int n = 0;
  float jx = 0, jy = 0;
  float x[kMax], y[kMax];
  float fase[kMax];   // 0..1: desfase propio del ciclo de andar (presentacion)
  float rad[kMax], vel[kMax];   // radio y velocidad de cada uno (los nuevos salen con kRadio y kVelEnemigo)
  float celda = kCelda;         // lado de la celda: >= 2 * el radio mayor
  uint64_t pares = 0, contactos = 0;   // del ultimo paso: pares mirados en la rejilla y enemigos tocando al jugador
  uint64_t pasos = 0;

 private:
  uint32_t Azar();
  float Azar01() { return float(Azar() >> 8) * (1.0f / 16777216.0f); }
  void Aparecer(int i);

  uint64_t rng_ = 1;
  float ex_[kMax], ey_[kMax];          // empuje acumulado del paso
  uint8_t tapado_[kMax];               // pisaba a uno que tiene delante (no avanza el paso siguiente)
  int celda_[kMax];                    // celda de cada enemigo (-1 = fuera de la rejilla)
  int inicio_[kLado * kLado + 1];      // prefijos: enemigos de la celda c en orden_[inicio_[c] .. inicio_[c + 1])
  int orden_[kMax];
  float gx_ = 0, gy_ = 0, rmax_ = kRadio;   // esquina de la rejilla y radio mayor del ultimo Paso
};

}  // namespace dbz3::survivors
