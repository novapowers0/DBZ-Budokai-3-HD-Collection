// Reglas de raza para Heroe. Las listas se cargan desde razas_reglas.toml;
// ausencia o errores se comunican al llamador para no modificar sus guardados.
#pragma once

#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <toml++/toml.hpp>

namespace dbz3::cac::reglas_raza {
struct Regla {
  std::set<int> tecnicas;
  std::set<std::string> formas;
};

struct Tabla {
  std::array<Regla, 6> razas;
  std::set<int> comunes;
  bool lista = false;
  // R23 HER-07: la tabla de ataques por raza esta a medias (R16_terraRAZ_REGLAS: "quedan por identificar"); mientras el toml no
  // diga `tecnicas_completas = true`, no filtra ataques (antes dejaba al Saiyan con 1 ataque y escondia los premios).
  bool tecnicas_completas = false;

  bool Tecnica(int raza, int capsula) const {
    if (lista && !tecnicas_completas) return capsula > 0;
    return lista && raza >= 0 && raza < 6 && capsula > 0 &&
           (comunes.count(capsula) || razas[raza].tecnicas.count(capsula));
  }
  bool Forma(int raza, const std::string& id) const {
    return lista && raza >= 0 && raza < 6 && razas[raza].formas.count(id);
  }
};

inline Tabla Cargar(const std::filesystem::path& ruta) {
  Tabla t;
  try {
    const auto doc = toml::parse_file(ruta.string());
    if (doc["version"].value_or(0) != 1) return {};
    const auto* comunes = doc["comunes"].as_array();
    const auto* razas = doc["raza"].as_array();
    if (!comunes || !razas || razas->size() != 6) return {};
    for (const auto& n : *comunes) {
      const auto v = n.value<int>();
      if (!v || *v <= 0 || !t.comunes.insert(*v).second) return {};
    }
    std::array<bool, 6> vistas{};
    for (const auto& nodo : *razas) {
      const auto* r = nodo.as_table();
      if (!r) return {};
      const int id = (*r)["id"].value_or(-1);
      if (id < 0 || id >= 6 || vistas[id]) return {};
      vistas[id] = true;
      const auto* tecnicas = (*r)["tecnicas"].as_array();
      const auto* formas = (*r)["formas"].as_array();
      if (!tecnicas || !formas) return {};
      for (const auto& n : *tecnicas) {
        const auto v = n.value<int>();
        if (!v || *v <= 0 || !t.razas[id].tecnicas.insert(*v).second) return {};
      }
      for (const auto& n : *formas) {
        const auto v = n.value<std::string>();
        if (!v || v->empty() || !t.razas[id].formas.insert(*v).second) return {};
      }
    }
    t.tecnicas_completas = doc["tecnicas_completas"].value_or(false);
    t.lista = true;
  } catch (const toml::parse_error&) {
    return {};
  }
  return t;
}
}  // namespace dbz3::cac::reglas_raza
