// C2: prueba sin juego del modulo cac (src/cac/cac.cpp): 3 heroes, guardado v2 y migracion v1 -> v2 con copia.
// Compilar y correr:  python "studies 5/ENTREGAS/C2/heroe_e2e.py" --solo cac
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "cac/cac.h"

namespace fs = std::filesystem;
namespace C = dbz3::cac;
static int g_fallos = 0;
#define CHECK(c, ...)                                         \
  do {                                                        \
    if (!(c)) {                                               \
      ++g_fallos;                                             \
      std::printf("FALLO %s:%d  %s  ", __FILE__, __LINE__, #c); \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
    }                                                         \
  } while (0)

static std::string Leer(const fs::path& p) {
  std::ifstream i(p, std::ios::binary);
  std::stringstream ss;
  ss << i.rdbuf();
  return ss.str();
}

int main(int argc, char** argv) {
  const fs::path dir = argc > 1 ? argv[1] : "D:/tmp/c2_cac_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path f = dir / "cac.toml";
  // --- v1 de verdad: 5 heroes (la v1 admitia 8), nombres con acento y comillas escapadas
  {
    std::ofstream o(f, std::ios::binary);
    o << "version = 1\nactivo = 1\n\n";
    for (int i = 0; i < 5; ++i) {
      o << "[[heroe]]\nnombre = \"Heroe" << i << (i == 0 ? " \\\"viejo\\\" ñ" : "") << "\"\nraza = " << (i % 3)
        << "\ncuerpo = " << i << "\nestilo = 0\nmoveset = -1\nxp = " << 10 * i << "\nnivel = 3\nsaga = " << i
        << "\nmaestros = 3\nentreno = [100, 40, 0, 0, 0, 0]\nprogreso = 2\n\n";
    }
  }
  const std::string original = Leer(f);
  CHECK(C::Leer(f.string()), "Leer v1");
  CHECK(C::MigradoDeV1(), "marca de migracion");
  CHECK(C::NumSlots() == 3, "3 activos, hay %d", C::NumSlots());
  CHECK(C::NumArchivados() == 2, "2 archivados, hay %d", C::NumArchivados());
  CHECK(C::ArchivadosPorLimite() == 2, "por limite %d", C::ArchivadosPorLimite());
  CHECK(C::Slot(0).nombre.rfind("Heroe0", 0) == 0, "el heroe viejo queda en la ficha 1: '%s'", C::Slot(0).nombre.c_str());
  CHECK(C::Slot(0).saga == 0 && C::Slot(2).saga == 2, "progreso por ficha intacto");
  CHECK(C::Slot(0).entreno[1] == 40 && C::Slot(0).maestros == 3, "maestros y entreno conservados");
  CHECK(C::SlotActivo() == 1, "activo = 1 (era 1 en la v1)");
  CHECK(fs::exists(f.string() + ".v1.bak"), "copia .v1.bak");
  CHECK(Leer(f.string() + ".v1.bak") == original, "la copia es identica al v1 original");
  CHECK(C::SlotsLlenos(), "lleno");
  CHECK(C::NuevoSlot() == -1, "con 3 activos no se crea otro");
  // --- ids estables y unicos
  std::set<std::string> ids;
  for (int i = 0; i < C::NumSlots(); ++i) ids.insert(C::Slot(i).id);
  for (const C::Heroe& h : C::Archivados()) ids.insert(h.id);
  CHECK(ids.size() == 5 && !ids.count(""), "5 ids distintos y no vacios (%zu)", ids.size());
  const std::string id0 = C::Slot(0).id;
  // --- inventario y equipo
  C::Heroe& h0 = C::SlotEditable(0);
  h0.inv = {6, 7, 9, 7, 5000, 0};   // duplicados y fuera de rango: se limpian
  h0.equipo[0] = 6;
  h0.equipo[1] = 99;   // no esta en el inventario
  h0.equipo[2] = 6;    // repetido
  h0.equipo[3] = 9;
  h0.b3_piezas[0] = 416;   // Luna 6: fids B3 se guardan en la ficha
  h0.b3_piezas[1] = 298;
  h0.b3_piezas[2] = 128;
  h0.b3_piezas[3] = 264;
  C::AjustarSlot(0);
  CHECK(h0.inv.size() == 3, "inventario limpio (%zu)", h0.inv.size());
  CHECK(h0.equipo[0] == 6 && h0.equipo[1] == 0 && h0.equipo[2] == 0 && h0.equipo[3] == 9, "equipo valido: %d %d %d %d",
        h0.equipo[0], h0.equipo[1], h0.equipo[2], h0.equipo[3]);
  CHECK(h0.b3_piezas[0] == 416 && h0.b3_piezas[1] == 298 && h0.b3_piezas[2] == 128 && h0.b3_piezas[3] == 264,
        "fids de piezas B3 validados y conservados");
  h0.desbloqueos = {"ki_dorado_goku", "kamehameha_rojo"};   // C3: desbloqueos con maestros: se guardan y se leen
  CHECK(C::Guardar(f.string()), "Guardar v2");
  const std::string v2 = Leer(f);
  CHECK(v2.find("version = 2") == 0 && v2.find("schema = \"dbz3-cac/2\"") != std::string::npos, "cabecera v2");
  CHECK(v2.find("b3_piezas = [416, 298, 128, 264]") != std::string::npos, "fids B3 escritos en cac.toml");
  CHECK(v2.find("[[archivado]]") != std::string::npos, "archivados guardados");
  CHECK(Leer(f.string() + ".v1.bak") == original, "la copia v1 no se pisa al guardar");
  // --- releer el v2
  CHECK(C::Leer(f.string()), "Leer v2");
  CHECK(!C::MigradoDeV1(), "ya no es migracion");
  CHECK(C::NumSlots() == 3 && C::NumArchivados() == 2, "3 + 2 tras releer (%d + %d)", C::NumSlots(), C::NumArchivados());
  CHECK(C::Slot(0).id == id0, "el id no cambia");
  CHECK(C::Slot(0).inv.size() == 3 && C::Slot(0).equipo[3] == 9, "inventario/equipo vuelven");
  CHECK(C::Slot(0).b3_piezas[0] == 416 && C::Slot(0).b3_piezas[3] == 264, "fids B3 vuelven tras releer");
  CHECK(C::Slot(0).nombre.find("viejo") != std::string::npos, "nombre con comillas escapadas: '%s'", C::Slot(0).nombre.c_str());
  // --- archivar y crear uno nuevo; duplicar cuando esta lleno no hace nada
  C::DuplicarSlot(0);
  CHECK(C::NumSlots() == 3, "duplicar lleno: nada");
  C::ArchivarSlot(2);
  CHECK(C::NumSlots() == 2 && C::NumArchivados() == 3, "archivar");
  const int nuevo = C::NuevoSlot();
  CHECK(nuevo == 2 && C::NumSlots() == 3 && !C::Slot(2).id.empty(), "nuevo heroe en la ficha 3");
  C::ArchivarSlot(2);
  C::RestaurarArchivado(C::NumArchivados() - 1);
  CHECK(C::NumSlots() == 3, "restaurar");
  // --- el heroe inicial (sin cac.toml) recibe id al guardar y lo conserva
  {
    fs::create_directories(dir / "nuevo");
    C::SlotEditable(0) = C::Heroe{};   // sin id
    CHECK(C::Slot(0).id.empty(), "nace sin id");
    CHECK(C::Guardar((dir / "nuevo" / "cac.toml").string()), "guardar");
    const std::string id1 = C::Slot(0).id;
    CHECK(!id1.empty(), "ya tiene id");
    CHECK(C::Guardar((dir / "nuevo" / "cac.toml").string()), "guardar otra vez");
    CHECK(C::Slot(0).id == id1, "el id no cambia al guardar de nuevo");
  }
  // --- una v1 con UN solo heroe (el caso real de casi todos)
  fs::create_directories(dir / "uno");
  {
    std::ofstream o(dir / "uno" / "cac.toml", std::ios::binary);
    o << "version = 1\nactivo = 0\n\n[[heroe]]\nnombre = \"Kakaroto\"\nraza = 0\nsaga = 4\nmaestros = 7\nprogreso = 2\n";
  }
  CHECK(C::Leer((dir / "uno" / "cac.toml").string()), "Leer v1 de un heroe");
  CHECK(C::NumSlots() == 1 && C::NumArchivados() == 0 && C::Slot(0).nombre == "Kakaroto" && C::Slot(0).saga == 4,
        "el unico heroe queda en la ficha 1 con su progreso");
  CHECK(fs::exists((dir / "uno" / "cac.toml.v1.bak").string()), "copia del v1 de un heroe");
  // --- revision de codigo: con mas de 3 heroes el ACTIVO no se archiva (v1 y v2) y un v2 recortado deja copia
  for (const int ver : {1, 2}) {
    const fs::path d = dir / ("activo_v" + std::to_string(ver));
    fs::create_directories(d);
    {
      std::ofstream o(d / "cac.toml", std::ios::binary);
      o << "version = " << ver << "\nactivo = 4\n\n";
      for (int i = 0; i < 5; ++i) o << "[[heroe]]\nnombre = \"A" << i << "\"\nsaga = " << i << "\n\n";
    }
    CHECK(C::Leer((d / "cac.toml").string()), "Leer v%d de 5 heroes", ver);
    CHECK(C::NumSlots() == 3 && C::NumArchivados() == 2 && C::ArchivadosPorLimite() == 2, "3 + 2 archivados");
    CHECK(C::Slot(C::SlotActivo()).nombre == "A4" && C::Slot(C::SlotActivo()).saga == 4, "el activo sigue siendo A4: '%s'",
          C::Slot(C::SlotActivo()).nombre.c_str());
    CHECK(ver == 1 ? fs::exists((d / "cac.toml.v1.bak").string()) : fs::exists((d / "cac.toml.recorte.bak").string()),
          "copia previa (v%d)", ver);
  }
  // --- version futura: no se toca
  fs::create_directories(dir / "fut");
  {
    std::ofstream o(dir / "fut" / "cac.toml", std::ios::binary);
    o << "version = 9\n";
  }
  CHECK(!C::Leer((dir / "fut" / "cac.toml").string()), "version futura se rechaza");
  {   // C3: los desbloqueos del heroe sobreviven a guardar y volver a leer
    CHECK(C::Leer(f.string()), "releer con desbloqueos");
    CHECK(C::Slot(0).desbloqueos.size() == 2 && C::Slot(0).desbloqueos[1] == "kamehameha_rojo", "desbloqueos: %zu",
          C::Slot(0).desbloqueos.size());
  }
  std::printf(g_fallos ? "cac_test: %d FALLOS\n" : "cac_test: OK\n", g_fallos);
  return g_fallos ? 1 : 0;
}
