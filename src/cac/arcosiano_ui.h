#pragma once
#include <array>
#include <string>

// Seis selectores conservados por los cuatro enteros históricos de la ficha.
// El identificador compuesto nunca es una entrada AFS.
namespace dbz3::arcosiano {
inline constexpr int kCantidad[] = {8, 6, 4, 4, 3, 3};
inline constexpr const char* kZonaEs[] = {"Cabeza", "Cuernos", "Cara / boca", "Placas", "Gema", "Cola"};
inline constexpr const char* kZonaEn[] = {"Head", "Horns", "Face / mouth", "Plates", "Gem", "Tail"};
inline constexpr const char* kNombres[][8] = {
    {"Orbe", "Estela", "Velamen", "Bifurca", "Estrato", "Visera", "Martillo", "Aguja"},
    {"Horizonte", "Ascua", "Ráfaga", "Averno", "Brotes", "Despejado"},
    {"Cuña", "Respirador", "Bastión", "Lúnula"},
    {"Arco", "Vértice", "Escama", "Baluarte"},
    {"Lente", "Rombo", "Tríada"}, {"Látigo", "Quilla", "Maza"}};

inline std::array<int, 6> Leer(const int (&ids)[4]) {
  int n = ids[0] >= 3800 && ids[0] < 3944 ? ids[0] - 3800 : 8 * 5;
  return {n % 8, n / 8 % 6, ids[1] >= 3960 && ids[1] < 3964 ? ids[1] - 3960 : 0,
          ids[2] >= 3980 && ids[2] < 3984 ? ids[2] - 3980 : 0, n / 48,
          ids[3] >= 3990 && ids[3] < 3993 ? ids[3] - 3990 : 0};
}
inline void Cambiar(int (&ids)[4], int zona, int paso) {
  if (zona < 0 || zona >= 6) return;
  auto tipos = Leer(ids);
  tipos[zona] = (tipos[zona] + paso % kCantidad[zona] + kCantidad[zona]) % kCantidad[zona];
  ids[0] = 3800 + tipos[0] + 8 * tipos[1] + 48 * tipos[4];
  ids[1] = 3960 + tipos[2]; ids[2] = 3980 + tipos[3]; ids[3] = 3990 + tipos[5];
}
inline std::string Nombre(const int (&ids)[4], int zona, bool ingles) {
  if (zona < 0 || zona >= 6) return {};
  const auto tipos = Leer(ids);
  return std::string(ingles ? "Type " : "Tipo ") + std::to_string(tipos[zona] + 1) + " / " +
         std::to_string(kCantidad[zona]) + "  " + kNombres[zona][tipos[zona]];
}
}  // namespace dbz3::arcosiano
