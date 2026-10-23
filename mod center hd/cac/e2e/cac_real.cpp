// C2: migracion v1 -> v2 con una partida REAL (copia de un cac.toml v1 que escribio el juego). Uso: cac_real <v1.toml> <carpeta>
#include <toml++/toml.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cac/cac.h"

namespace fs = std::filesystem;
namespace C = dbz3::cac;
static int fallos = 0;
#define CHECK(c, ...)                                  \
  do {                                                 \
    if (!(c)) {                                        \
      ++fallos;                                        \
      std::printf("FALLO %d: %s  ", __LINE__, #c);     \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
    }                                                  \
  } while (0)
static std::string Leer(const fs::path& p) {
  std::ifstream i(p, std::ios::binary);
  std::stringstream ss;
  ss << i.rdbuf();
  return ss.str();
}
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const fs::path dir = argv[2];
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path f = dir / "cac.toml";
  fs::copy_file(argv[1], f);
  const std::string original = Leer(f);
  const toml::table t = toml::parse_file(f.string());
  const auto* arr = t["heroe"].as_array();
  const int n_v1 = arr ? int(arr->size()) : 0;
  CHECK(t["version"].value_or(1) == 1, "el fichero de prueba es v1");
  CHECK(C::Leer(f.string()), "Leer");
  CHECK(C::MigradoDeV1(), "marca de migracion");
  CHECK(fs::exists(f.string() + ".v1.bak") && Leer(f.string() + ".v1.bak") == original, "copia identica");
  CHECK(C::NumSlots() == std::min(n_v1, C::kMaxSlots), "%d heroes v1 -> %d activos", n_v1, C::NumSlots());
  for (int i = 0; i < C::NumSlots(); ++i) {   // cada campo de la v1 llega igual a su ficha
    const toml::table* h = (*arr)[size_t(i)].as_table();
    const C::Heroe& s = C::Slot(i);
    CHECK(s.nombre == (*h)["nombre"].value_or(std::string{"Heroe"}), "nombre %d: '%s'", i, s.nombre.c_str());
    CHECK(s.raza == (*h)["raza"].value_or(0), "raza %d", i);
    CHECK(s.cuerpo == (*h)["cuerpo"].value_or(0), "cuerpo %d (%d)", i, s.cuerpo);
    CHECK(s.estilo == (*h)["estilo"].value_or(0), "estilo %d", i);
    CHECK(s.saga == (*h)["saga"].value_or(0), "saga %d", i);
    CHECK(s.maestros == (*h)["maestros"].value_or(0), "maestros %d", i);
    CHECK(s.xp == (*h)["xp"].value_or(0), "xp %d", i);
    CHECK(s.traje == (*h)["traje"].value_or(0), "traje %d", i);
    CHECK(!s.id.empty(), "id %d", i);
  }
  const std::string nombre0 = C::Slot(0).nombre;
  CHECK(C::Guardar(f.string()), "Guardar v2");
  CHECK(Leer(f.string() + ".v1.bak") == original, "la copia v1 sigue intacta");
  CHECK(C::Leer(f.string()) && !C::MigradoDeV1(), "releer v2");
  CHECK(C::Slot(0).nombre == nombre0, "el heroe viejo sigue en la ficha 1");
  std::printf(fallos ? "cac_real: %d FALLOS\n" : "cac_real: OK (%d heroes v1)\n", fallos ? fallos : n_v1);
  return fallos ? 1 : 0;
}
