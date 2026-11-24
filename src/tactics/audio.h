// Dragon Tactics H5: mezcla de sonido del combate. Puro (sin el juego): decide que efectos de los eventos de la simulacion llegan a sonar.
// Con 12 unidades pueden caer decenas de golpes por segundo; el gestor de efectos del juego no los apila sin limite, asi que cada sonido
// ocupa una voz mientras dura y hay un maximo de voces a la vez. Las tecnicas y los KO tienen prioridad: se reservan voces para ellos.
#pragma once
#include <algorithm>

namespace dbz3::tactics {

enum class PrioSe { Golpe = 0, Tecnica = 1, Muerte = 2 };

class MezclaSe {
 public:
  static constexpr int kVoces = 6;                       // voces a la vez
  static constexpr int kTope[3] = {4, 5, kVoces};        // golpes: 4 (dejan 2 libres); tecnicas: 5; KO: las 6
  // TACTICS-10: admision antes de las voces. El mismo sonido (banco, id) que empezo hace menos de kJunta s ya esta sonando: se junta con el
  // (en un cuadro, dos golpes iguales son un solo golpe mas fuerte). Y como mucho kPorCuadro sonidos nuevos por cuadro (`ahora` es el mismo en
  // todo el cuadro; los KO no cuentan para el tope). Lo juntado no ocupa voz ni cuenta como descarte.
  static constexpr int kPorCuadro = 2;
  static constexpr double kJunta = 0.12;
  // `ahora` en segundos (tiempo real: a x2/x4 caen mas eventos por segundo y se descartan mas); `dura` = lo que ocupa la voz.
  bool Pedir(double ahora, PrioSe prio, double dura, int banco = -1, int id = -1) {
    if (ahora != cuadro_) cuadro_ = ahora, n_cuadro_ = 0;
    for (int i = 0; i < kVoces; ++i)
      if (fin_[i] > ahora && banco_[i] == banco && id_[i] == id && prioridad_[i] >= int(prio) && ahora - ini_[i] < kJunta) return ++juntados_, false;
    if (prio != PrioSe::Muerte && n_cuadro_ >= kPorCuadro) return ++juntados_, false;
    if (Ocupadas(ahora) >= kTope[int(prio)]) {
      for (int i = 0; i < kVoces; ++i)   // voces llenas y ese mismo sonido ya suena (el golpe comun): se junta con el, no se pierde
        if (fin_[i] > ahora && banco_[i] == banco && id_[i] == id && prioridad_[i] >= int(prio)) return ++juntados_, false;
      ++descartados_[int(prio)];
      return false;
    }
    for (int i = 0; i < kVoces; ++i)
      if (fin_[i] <= ahora) {
        fin_[i] = ahora + dura, ini_[i] = ahora, banco_[i] = banco, id_[i] = id, prioridad_[i] = int(prio);
        break;
      }
    ++sonados_[int(prio)];
    ++n_cuadro_;
    return true;
  }
  int Ocupadas(double ahora) const {
    int n = 0;
    for (double f : fin_) n += f > ahora;
    return n;
  }
  int Sonados(PrioSe p) const { return sonados_[int(p)]; }
  int Descartados(PrioSe p) const { return descartados_[int(p)]; }
  int Juntados() const { return juntados_; }

 private:
  double fin_[kVoces] = {}, ini_[kVoces] = {};
  int banco_[kVoces] = {}, id_[kVoces] = {}, prioridad_[kVoces] = {};
  int sonados_[3] = {}, descartados_[3] = {}, juntados_ = 0;
  double cuadro_ = -1;
  int n_cuadro_ = 0;
};

}  // namespace dbz3::tactics
