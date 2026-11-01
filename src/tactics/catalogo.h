// Dragon Tactics H2/T9: catalogo de unidades (tactics_units.toml) validado. Sin dependencias del juego: solo toml++ y sim.h.
#pragma once
#include <array>
#include <string>
#include <algorithm>
#include <vector>

#include "src/tactics/sim.h"

namespace dbz3::tactics {

struct Catalogo {
  std::vector<sim::DefUnidad> unidades;
  const sim::DefUnidad* Buscar(const std::string& id) const;
  // H9: por unidad (mismo indice que `unidades`), el {luchador B3, forma, fid del modelo} del modelo de cada estrella (1 a 4); vacio si no se paso manifiesto.
  // Con menos formas que estrellas se usa la ultima (Forma()).
  std::vector<std::vector<std::array<int, 3>>> formas;
  std::array<int, 3> Forma(size_t unidad, int estrellas) const {
    const auto& f = formas[unidad];
    return f[size_t(std::min<int>(std::max(estrellas, 1), int(f.size())) - 1)];
  }
};

// Carga y valida. ruta_manifest (opcional, "" = no comprobar): cada id debe existir en tactics_manifest.toml, que es de donde la
// presentacion saca modelo y clips. Devuelve false con todos los problemas encontrados en *error (uno por linea).
bool CargarCatalogo(const std::string& ruta_unidades, const std::string& ruta_manifest, Catalogo* out, std::string* error);

}  // namespace dbz3::tactics
