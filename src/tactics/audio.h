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
  // `ahora` en segundos (tiempo real: a x2/x4 caen mas eventos por segundo y se descartan mas); `dura` = lo que ocupa la voz.
  bool Pedir(double ahora, PrioSe prio, double dura) {
    if (Ocupadas(ahora) >= kTope[int(prio)]) {
      ++descartados_[int(prio)];
      return false;
    }
    for (double& f : fin_)
      if (f <= ahora) {
        f = ahora + dura;
        break;
      }
    ++sonados_[int(prio)];
    return true;
  }
  int Ocupadas(double ahora) const {
    int n = 0;
    for (double f : fin_) n += f > ahora;
    return n;
  }
  int Sonados(PrioSe p) const { return sonados_[int(p)]; }
  int Descartados(PrioSe p) const { return descartados_[int(p)]; }

 private:
  double fin_[kVoces] = {};
  int sonados_[3] = {}, descartados_[3] = {};
};

}  // namespace dbz3::tactics
