// iw_cutscene.h - UN reproductor de escenas de camara de IW (dia 1 / equipo A, 6/11): Cutscene_GotoLine (EU 0x3424F0) y
// Cutscene_Update (EU 0x342170). Sustituye las cadenas escritas a mano del vuelo (intro v0/v1, DS113) y del visor de disparo.
//
// Original (struct param_1): +0 tabla de escenas {n lineas, lineas*}, +4 escena, +8 linea, +0xC tiempo t, +0x10 fin = cuadros-1
// del clip de la linea, +0x14 mascara de actores, +0x28 = libro<<16 | clip. Linea (8 B) = {s16 clip, s8 libro, s8 n actores,
// ptr actores}; actor (4 B) = {s8 indice, s8 efecto, s16 anim}.
//  - GotoLine(escena, linea): t = 0, fin = cuadros(clip)-1, marca los actores, CharModel_SetAnimBlend(1.0, 0, actor, anim) y
//    muestrea la camara en t = 0 (FUN_00127280, ver iw_amc.h). Con linea >= n lineas (o escena -1) la escena ACABA (-1).
//  - Update: si t < fin: t = min(t + dt, fin) y muestrea; si no: GotoLine(escena, linea + 1).
// Con dt = 1 cada linea dura exactamente `cuadros` pasos (t = 0..cuadros-1). Las lineas sin cuadros se saltan.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace dbz3::iwcut {

struct Actor {
  int indice = -1;      // 0 jugador, 1 y 2 actores del guion
  int efecto = -1;
  uint32_t anim = 0;    // codigo de animacion de IW (CharModel_SetAnimBlend)
};
struct Linea {
  int libro = 0, clip = 0, cuadros = 0;   // `cuadros` = tabla del #AMC +8 (iw_amc.h Clip::nf); 0 = no existe
  std::vector<Actor> actores;
};
struct Paso {
  bool fin = false;     // la escena acabo (GotoLine -> -1)
  int linea = 0;        // linea actual
  float t = 0;          // cuadro del clip dentro de la linea (0 .. cuadros-1)
  bool entra = false;   // primer cuadro de la linea (GotoLine: actores, sonidos, SetAnimBlend)
  const Linea* l = nullptr;
};

struct Escena {
  std::vector<Linea> lineas;
  int linea = -1;
  float t = 0;
  bool fin = false;

  void Reinicia() { linea = -1, t = 0, fin = false, entrando_ = true; }
  // GotoLine(escena, k): salta lineas sin cuadros; false si la escena acaba.
  bool Ir(int k) {
    while (k >= 0 && k < int(lineas.size()) && lineas[size_t(k)].cuadros <= 0) ++k;
    t = 0;
    linea = k;
    fin = k < 0 || k >= int(lineas.size());
    return !fin;
  }
  // Un paso: la primera vez GotoLine(0); despues Update(dt).
  Paso Tick(float dt = 1.0f) {
    if (fin) return Salida(true);
    if (linea < 0) {
      Ir(0);
    } else {
      const float fin_l = float(lineas[size_t(linea)].cuadros - 1);
      if (t < fin_l) t = std::min(t + dt, fin_l), entrando_ = false;
      else Ir(linea + 1), entrando_ = true;
    }
    return fin ? Salida(true) : Salida(false);
  }

 private:
  bool entrando_ = true;
  Paso Salida(bool f) const {
    Paso p;
    p.fin = f;
    p.linea = linea;
    p.t = t;
    p.entra = !f && entrando_ && t == 0;
    p.l = f || linea < 0 ? nullptr : &lineas[size_t(linea)];
    return p;
  }
};

// Posicion absoluta: la linea y el cuadro local de un reloj `f` (0 = primer cuadro de la escena) sobre las lineas encadenadas,
// el mismo recorrido que Tick() pero sin estado (entrada del visor de disparo, donde manda el contador del estado).
// false si f cae despues de la ultima linea. Las lineas sin cuadros se saltan.
inline bool Posicion(const std::vector<Linea>& lineas, int f, int& linea, float& local) {
  int acum = 0;
  for (size_t k = 0; k < lineas.size(); ++k) {
    if (lineas[k].cuadros <= 0) continue;
    if (f < acum + lineas[k].cuadros) return linea = int(k), local = float(f - acum), true;
    acum += lineas[k].cuadros;
  }
  return false;
}

}  // namespace dbz3::iwcut
