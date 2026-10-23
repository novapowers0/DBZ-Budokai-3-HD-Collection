#pragma once
// X0h (studies 5): reloj de pared "de juego" para los ganchos de prueba. Con DBZ3_NO_DIALOGS y DBZ3_TEST_TURBO=N el
// guest corre N veces mas rapido; los temporizadores que miran el reloj de pared (autopiloto, esperas de la Leyenda,
// minijuegos de IW...) deben correr N veces mas rapido tambien. Sin esas variables (jugadores) devuelve el reloj normal.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
namespace dbz3 {
inline int RelojFactor() {
  static const int k = [] {
    if (!std::getenv("DBZ3_NO_DIALOGS")) return 1;
    const char* e = std::getenv("DBZ3_TEST_TURBO");
    return e ? std::clamp(std::atoi(e), 1, 16) : 1;
  }();
  return k;
}
inline int64_t RelojMs() {
  return RelojFactor() * std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
}
// ImGui::GetTime() / DeltaTime en reloj de juego (los tableros, minijuegos y cartelas se animan con ellos)
inline double RelojT(double t) { return t * RelojFactor(); }
inline float RelojDt(float dt) { return dt * float(RelojFactor()); }
}  // namespace dbz3
