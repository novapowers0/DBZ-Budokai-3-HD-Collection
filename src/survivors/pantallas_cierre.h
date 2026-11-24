#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace dbz3::survivors::cierre {

inline constexpr float kPoseDerrota = 1.9f, kPoseVictoria = 2.4f;
inline constexpr char kBeta[] = "BETA";

// La prueba fuerza solo la transicion en una partida aislada con piloto.
enum class FinalPrueba { No, Derrota, Victoria };
inline FinalPrueba Forzar(bool piloto, float segundos, std::string_view orden) {
  if (!piloto || segundos < 3.0f) return FinalPrueba::No;
  if (orden == "derrota") return FinalPrueba::Derrota;
  if (orden == "victoria") return FinalPrueba::Victoria;
  return FinalPrueba::No;
}

inline float Escala(float w, float h) {
  if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1) return 0;
  return std::min(h / 1080.0f, w / 1280.0f);
}

struct Cuadro { float x, y, w, h; };
inline Cuadro Resultados(float w, float h, float k, bool ranking, float entrada) {
  const float ancho = 1150 * k, alto = (1150 * 800.f / 1200.f + (ranking ? 100 : 0)) * k;
  return {(w - ancho) * 0.5f, (h - alto) * 0.5f + (1 - entrada) * 32 * k, ancho, alto};
}
inline Cuadro Derrota(float w, float k) { return {w * 0.5f - 270 * k, 58 * k, 540 * k, 122 * k}; }

inline constexpr std::array<std::string_view, 7> kIdiomas = {"es", "en", "fr", "de", "it", "pt", "ja"};
inline constexpr const char* kMedallas[4][7] = {
    {"Bronce", "Bronze", "Bronze", "Bronze", "Bronzo", "Bronze", "ブロンズ"},
    {"Plata", "Silver", "Argent", "Silber", "Argento", "Prata", "銀"},
    {"Oro", "Gold", "Or", "Gold", "Oro", "Ouro", "金"},
    {"Platino", "Platinum", "Platine", "Platin", "Platino", "Platina", "プラチナ"},
};
inline const char* TextoMedalla(unsigned medalla, std::string_view idioma) {
  if (medalla >= std::size(kMedallas)) return "";
  const auto i = std::find(kIdiomas.begin(), kIdiomas.end(), idioma);
  return kMedallas[medalla][i == kIdiomas.end() ? 1 : i - kIdiomas.begin()];
}

}  // namespace dbz3::survivors::cierre
