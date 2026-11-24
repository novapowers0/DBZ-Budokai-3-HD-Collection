// dbz3 - Dragon Rogue (ROG-04): evoluciones. Diseno: studies 5/ENTREGAS/ROGUE_DISENO.md 1.4.
// Transformacion = forma inicial del jugador (`form1` de RequestBattleEx, via Reglas::forma1 en modos.cpp) con las
// capsulas que esa forma exige equipadas (el juego recorta la forma a las capsulas que lleva: tagteam_slots.cpp).
// Fusion = el luchador pasa a la ficha nativa de fusion (64-104) en su forma fusionada (modos::SetJugador).
// Tabla de formas REAL, leida del juego: char372 (0x82329CF0 + 372*id) +0xD0 = n.o de formas y +212 + 20*f = capsula
// que exige la forma f (u16), +214 = ID del modelo, +218 = tipo (byte +6). docs/03_formatos/CAPSULAS_B3.md y
// docs/07_ports/GOHAN_FUTURO_EXPLORACION_2026-10-06/04_transformaciones/evid_tabla_b3.md (volcado de las 44 fichas).
//   tipo 1/5/6 con capsula = transformacion (Kaioken, SSJ..SSJ4, formas de Freezer y Cell, Nail/Kami...);
//   tipo 7 = tecnica (nave, control mental, absorcion de Bu) y 8 = cambio de modelo: no son evoluciones;
//   fichas 64-104: tipo 3/5 con capsula = el fusionado (64 Gotenks f3, 72 Vegito f2, 70 Gogeta SSJ4 f4...).
// Solo imagen US (firma "_charasel.cpp", como heroe.cpp); sin ella no hay evoluciones (la carta no se ofrece).
#include <rex/runtime.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "rogue.h"

namespace dbz3::rogue {
namespace {
constexpr uint32_t kChar372 = 0x82329CF0, kSignature = 0x82020700;
const uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  const uint8_t* b = rt ? rt->virtual_membase() : nullptr;
  return b && std::memcmp(b + kSignature, "_charasel.cpp", 13) == 0 ? b : nullptr;
}
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

struct Forma {
  int f, capsula, modelo, tipo;
};
std::vector<Forma> FormasDe(int id) {
  std::vector<Forma> v;
  const uint8_t* b = Base();
  if (!b || id < 0 || id >= 105) return v;
  const uint8_t* r = b + kChar372 + 372u * uint32_t(id);
  const uint32_t n = Be32(r + 0xD0);
  for (uint32_t f = 0; f < std::min<uint32_t>(n, 8); ++f) {
    const uint8_t* q = r + 212 + 20 * f;
    v.push_back({int(f), Be16(q), Be16(q + 2), q[6]});
  }
  return v;
}
// Las transformaciones de verdad del luchador, en orden (forma 1, 2...).
std::vector<Forma> Reales(int jugador) {
  std::vector<Forma> v;
  for (const Forma& x : FormasDe(jugador))
    if (x.f > 0 && x.capsula > 0 && (x.tipo == 1 || x.tipo == 5 || x.tipo == 6)) v.push_back(x);
  return v;
}
}  // namespace

int Transformaciones(int jugador) { return int(Reales(jugador).size()); }

// Ficha de fusion del luchador: una ficha 64-104 en la que es el que empieza (forma 0, o 1 si la 0 es solo modelo) y
// que tiene una forma fusionada con capsula. Antes las de Potara (tipo 5: no hay fusion fallida). -1 = no tiene.
int FichaFusion(int jugador, int* forma) {
  int mejor = -1, mejor_f = 0, mejor_tipo = 0;
  for (int id = 64; id < 105; ++id) {
    const std::vector<Forma> fs = FormasDe(id);
    if (fs.size() < 2) continue;
    const Forma& ini = fs[0].tipo != 8 ? fs[0] : fs[1];
    if (ini.modelo != jugador || ini.tipo == 8) continue;
    for (const Forma& x : fs)
      if (x.capsula > 0 && (x.tipo == 3 || x.tipo == 5)) {
        if (mejor < 0 || (x.tipo == 5 && mejor_tipo != 5)) mejor = id, mejor_f = x.f, mejor_tipo = x.tipo;
        break;
      }
  }
  if (forma) *forma = mejor_f;
  return mejor;
}

// `paso` = n.o de transformacion de las cartas (0 = ninguna; se recorta a las que tiene); `fusion` = carta Fusion.
// Las capsulas: las de toda la cadena hasta esa forma (SSJ2 exige la de SSJ, que exige la de Kaioken: SKC +16).
Evolucion EvolucionDe(int jugador, int paso, bool fusion) {
  Evolucion e;
  if (const char* t = std::getenv("DBZ3_TEST_ROGUE_FORMA")) {   // PRUEBAS (ROG-04): forma pedida tal cual
    e.forma = std::clamp(std::atoi(t), 0, 7);
    return e;
  }
  if (fusion) {
    int f = 0;
    if (const int id = FichaFusion(jugador, &f); id >= 0) {
      e.id = id;
      e.forma = f;
      e.caps.push_back(FormasDe(id)[size_t(f)].capsula);
      return e;
    }
  }
  const std::vector<Forma> v = Reales(jugador);
  paso = std::clamp(paso, 0, int(v.size()));
  if (paso == 0) return e;
  e.forma = v[size_t(paso - 1)].f;
  for (int i = 0; i < paso; ++i) e.caps.push_back(v[size_t(i)].capsula);
  return e;
}
}  // namespace dbz3::rogue
