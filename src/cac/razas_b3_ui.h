#pragma once
#include <array>
#include <cstdlib>
#include <string>

// Recetas de piezas del AFS local; mismo contrato que cac/razas_b3.py.
// Los cuatro enteros existentes preservan el guardado (todos <= 4095).
namespace dbz3::razas_b3 {
inline bool Experimental() {
  const char* v = std::getenv("DBZ3_CAC_RAZAS_EXPERIMENTAL");
  return v && std::string(v) == "1";
}
inline constexpr int kCuradas[3][2][4] = {
    {{0, 0, 0, 0}, {3, 0, 1, 1}},
    {{0, 0, 0, 0}, {3, 0, 3, 0}},
    {{0, 0, 0, 0}, {1, 0, 1, 0}}};
inline int Conjunto(int familia, const int (&ids)[4]) {
  const int base = familia == 1 ? 4000 : 4080;
  return ids[0] == base + kCuradas[familia - 1][1][0] ? 1 : 0;
}
inline int Familia(int cuerpo) {
  if (cuerpo == 27 || cuerpo == 38) return 1;
  if (cuerpo == 11) return 2;
  if (cuerpo == 34 || cuerpo == 35 || cuerpo == 36) return 3;
  return 0;
}
inline constexpr const char* kFreezerEs[] = {"Cabeza / cresta", "Cuernos", "Cara / boca", "Cuerpo / placas", "Gema", "Cola"};
inline constexpr const char* kFreezerEn[] = {"Head / crest", "Horns", "Face / mouth", "Body / plates", "Gem", "Tail"};
inline constexpr const char* kNamekEs[] = {"Cabeza / orejas", "Antenas", "Cuerpo / traje", "Cara / ceño"};
inline constexpr const char* kNamekEn[] = {"Head / ears", "Antennae", "Body / costume", "Face / brow"};
inline constexpr const char* kMajinEs[] = {"Cabeza", "Antena", "Cuerpo / masa", "Cara"};
inline constexpr const char* kMajinEn[] = {"Head", "Antenna", "Body / build", "Face"};
inline int Numero(int familia) { return !familia ? 0 : !Experimental() ? 1 : familia == 1 ? 6 : 4; }
inline int Cantidad(int familia, int zona) {
  if (!Experimental()) return 2;
  constexpr int freezer[] = {8, 6, 8, 4, 3, 4};
  return familia == 1 ? freezer[zona] : zona == 0 ? 8 : 4;
}
inline int Valor(int id, int limite, int base = 4000) { return id >= base && id < base + limite ? id - base : 0; }
inline std::array<int, 6> Leer(int familia, const int (&ids)[4]) {
  if (familia == 1) {
    const int a = Valor(ids[0], 48), b = Valor(ids[1], 24);
    return {a % 8, a / 8, b % 8, Valor(ids[2], 4), b / 8, Valor(ids[3], 4)};
  }
  return {Valor(ids[0], 8, 4080), Valor(ids[1], 4, 4080), Valor(ids[2], 4, 4080), Valor(ids[3], 4, 4080), 0, 0};
}
inline void Cambiar(int familia, int (&ids)[4], int zona, int paso) {
  if (zona < 0 || zona >= Numero(familia)) return;
  if (!Experimental()) {
    const int siguiente = (Conjunto(familia, ids) + paso % 2 + 2) % 2;
    for (int i = 0; i < 4; ++i) ids[i] = (familia == 1 ? 4000 : 4080) + kCuradas[familia - 1][siguiente][i];
    return;
  }
  auto t = Leer(familia, ids);
  const int n = Cantidad(familia, zona);
  t[zona] = (t[zona] + paso % n + n) % n;
  if (familia == 1) {
    ids[0] = 4000 + t[0] + 8 * t[1]; ids[1] = 4000 + t[2] + 8 * t[4];
    ids[2] = 4000 + t[3]; ids[3] = 4000 + t[5];
  } else {
    for (int i = 0; i < 4; ++i) ids[i] = 4080 + t[i];
  }
}
inline const char* Etiqueta(int familia, int zona, bool ingles) {
  if (!Experimental()) return ingles ? "Native set" : "Conjunto nativo";
  if (familia == 1) return ingles ? kFreezerEn[zona] : kFreezerEs[zona];
  if (familia == 2) return ingles ? kNamekEn[zona] : kNamekEs[zona];
  return ingles ? kMajinEn[zona] : kMajinEs[zona];
}
inline std::string Nombre(int familia, const int (&ids)[4], int zona) {
  if (!Experimental()) {
    constexpr const char* nombres[3][2] = {{"Freezer I", "Freezer IV"}, {"Piccolo", "Piccolo Daimao"}, {"Majin Buu", "Super Buu"}};
    const int k = Conjunto(familia, ids);
    return "Type " + std::to_string(k + 1) + " / 2 - " + nombres[familia - 1][k];
  }
  return "Type " + std::to_string(Leer(familia, ids)[zona] + 1) + " / " +
         std::to_string(Cantidad(familia, zona));
}
}  // namespace dbz3::razas_b3
