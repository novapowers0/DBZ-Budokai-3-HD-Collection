// NMC: seis selectores sobre los campos de ficha ya persistentes.
// Enganche de heroe_ui.cpp pendiente de liberar la reserva de solHEROEb.
#ifndef DBZ3_CAC_NAMEK_ORIGINAL_UI_H_
#define DBZ3_CAC_NAMEK_ORIGINAL_UI_H_

#include "cac.h"
#include <string>

namespace dbz3::cac::namek {
inline constexpr int kCantidad[] = {8, 4, 3, 4, 6, 4};
inline constexpr int kBase[] = {3600, 3640, 3680, 3700};
inline constexpr const char* kZonaEs[] = {"Cabeza", "Orejas", "Parches musculares", "Ropa", "Antenas", "Boca / mandíbula"};
inline constexpr const char* kZonaEn[] = {"Head", "Ears", "Muscle patches", "Outfit", "Antennae", "Mouth / jaw"};
inline constexpr const char* kCabeza[] = {"Semilla", "Aguja", "Bastión", "Estela", "Cáliz", "Guijarro", "Órbita", "Duna"};
inline constexpr const char* kOrejas[] = {"Lanza", "Remo", "Hoja", "Muesca"};
inline constexpr const char* kParches[] = {"Costillas", "Rombos", "Canales"};
inline constexpr const char* kRopa[] = {"Surco", "Nómada", "Umbral", "Estrato"};
inline constexpr const char* kAntenas[] = {"Espiral", "Brote", "Tallo", "Gemelas", "Viento", "Sauce"};
inline constexpr const char* kBoca[] = {"Serena", "Firme", "Proa", "Velo"};

inline bool Activo(const Heroe& h) {
  if (h.raza != 2) return false;
  for (int id : h.b3_piezas) if (id >= 3600 && id < 3720) return true;
  return false;
}
inline int Tipo(const Heroe& h, int zona) {
  if (zona < 0 || zona >= 6) return -1;
  int t = zona < 4 ? h.b3_piezas[zona] - kBase[zona] : zona == 4 ? h.pelo : h.cara;
  if (zona >= 4 && !Activo(h)) return -1;
  return t >= 0 && t < kCantidad[zona] ? t : -1;
}
inline std::string Nombre(const Heroe& h, int zona, bool ingles) {
  int t = Tipo(h, zona);
  if (t < 0) return "Base";
  const char* const* names[] = {kCabeza, kOrejas, kParches, kRopa, kAntenas, kBoca};
  return std::string(ingles ? "Type " : "Tipo ") + std::to_string(t + 1) + " · " + names[zona][t];
}
inline void Cambiar(Heroe& h, int zona, int dir) {
  if (h.raza != 2 || zona < 0 || zona >= 6 || !dir) return;
  // Activar una parte estrena el conjunto propio completo: no mezcla arte nativo.
  if (!Activo(h)) {
    for (int z = 0; z < 4; ++z) h.b3_piezas[z] = kBase[z];
    h.pelo = h.cara = 0;
    if (dir > 0) return;
  }
  int t = Tipo(h, zona);
  t = (t + (dir > 0 ? 1 : -1) + kCantidad[zona]) % kCantidad[zona];
  if (zona < 4) h.b3_piezas[zona] = kBase[zona] + t;
  else if (zona == 4) h.pelo = t;
  else h.cara = t;
}
}  // namespace dbz3::cac::namek
#endif
