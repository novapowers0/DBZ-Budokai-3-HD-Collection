#pragma once
#include <cstdint>
namespace dbz3::pasarela::nave::original2p {
inline constexpr bool kDibujo2PNormal = false;
struct Lado {
  int personaje_b2 = -1, cara = 0, coronas = 0;
  int vida_frente = 0, vida_estela = 0, porcentaje_objetivo = 0, porcentaje_presentado = 0;
  int burbuja_nodo = 0, estado_popup = 0;
  uint32_t flags_vista = 0;
  float tiempo_ko = 0, tiempo_critico = 0;
};
struct Muestra {
  bool productores_autoritativos = false;
  int modo = -1, lado_activo = -1;
  float tick_modo = 0;
  Lado lado[2];
  int marco_modo11[2] = {}, vida_modo11[2] = {}, ancla_modo12[2] = {}, alineacion_modo12[2] = {};
};
}  // namespace dbz3::pasarela::nave::original2p
