// Prueba de src/launcher/ids_mods.cpp con mods falsos (escenarios y capsulas que chocan).
//   clang++ -std=c++17 -O1 tools/test_ids_mods.cpp src/launcher/ids_mods.cpp -o E:/DBZ3HD_trabajo/launcher_ids/t.exe
//   E:/DBZ3HD_trabajo/launcher_ids/t.exe E:/DBZ3HD_trabajo/launcher_ids/mods
// La carpeta de mods se aparta (no se borra) a mods_<hora> antes de empezar.
#include "../src/launcher/ids_mods.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using dbz3::launcher::ids::Revisar;

static int fallos = 0;
#define CHECK(c)                                              \
  do {                                                        \
    if (!(c)) { std::printf("FALLA linea %d: %s\n", __LINE__, #c); ++fallos; } \
  } while (0)

static void Escribe(const fs::path& f, const std::string& s) {
  fs::create_directories(f.parent_path());
  std::ofstream(f, std::ios::binary) << s;
}

static void Escenario(const fs::path& root, const std::string& mod, int entrada, bool generado = false) {
  const fs::path d = root / mod;
  Escribe(d / "stage.toml", "[[escenario]]\nnombre = \"" + mod + "\"\nentrada = " + std::to_string(entrada) +
                                "    # data_cmn\nmenu = " + std::to_string(entrada + 100) + "\nid_se = 45\ntema = 16\n");
  Escribe(d / "us" / "data_cmn.afs" / std::to_string(entrada) / "geom.bin", "G" + mod);
  Escribe(d / "us" / "data_cmn.afs" / std::to_string(entrada + 100) / "geom.bin", "M" + mod);
  if (generado) Escribe(d / "origen.txt", "x");
}

static int Entrada(const fs::path& root, const std::string& mod) {
  std::ifstream in(root / mod / "stage.toml");
  for (std::string l; std::getline(in, l);)
    if (l.rfind("entrada", 0) == 0) return std::atoi(l.c_str() + l.find('=') + 1);
  return -1;
}

static bool Bin(const fs::path& root, const std::string& mod, int n, const std::string& quien) {
  std::ifstream in(root / mod / "us" / "data_cmn.afs" / std::to_string(n) / "geom.bin");
  std::string s((std::istreambuf_iterator<char>(in)), {});
  return s == quien;
}

int main(int argc, char** argv) {
  const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path("E:/DBZ3HD_trabajo/launcher_ids/mods");
  std::error_code ec;
  if (argc > 2 && std::string(argv[2]) == "--revisar") {   // la revision de JUGAR sobre una carpeta mods real
    std::vector<std::string> act;
    for (const auto& d : fs::directory_iterator(root))
      if (d.is_directory() && !fs::exists(d.path() / ".disabled") && d.path().extension() != ".disabled")
        act.push_back(d.path().filename().u8string());
    const auto r = Revisar(root, act);
    for (const auto& c : r.cambios) std::printf("cambio: %s %d -> %d (con %s)\n", c.mod.c_str(), c.antes, c.despues, c.con.c_str());
    for (const auto& b : r.bloqueos) std::printf("bloqueo: %s %d (con %s)\n", b.mod.c_str(), b.numero, b.con.c_str());
    return 0;
  }
  if (fs::exists(root)) fs::rename(root, root.string() + "_" + std::to_string(std::time(nullptr)), ec);
  Escenario(root, "Campo IW 884", 5060, true);   // generado desde la ISO: numero fijo
  Escenario(root, "Nova Arena", 5060);           // choca con el generado
  Escenario(root, "Mi Ring", 5010);
  Escenario(root, "Otro Ring", 5010);            // choca con Mi Ring (va detras por orden alfabetico)
  std::vector<std::string> act = {"Campo IW 884", "Nova Arena", "Mi Ring", "Otro Ring"};

  auto r = Revisar(root, act);
  CHECK(r.bloqueos.empty());
  CHECK(r.cambios.size() == 2);
  CHECK(Entrada(root, "Campo IW 884") == 5060);
  CHECK(Entrada(root, "Mi Ring") == 5010);
  CHECK(Entrada(root, "Nova Arena") == 5200);
  CHECK(Bin(root, "Nova Arena", 5200, "GNova Arena") && Bin(root, "Nova Arena", 5300, "MNova Arena"));
  CHECK(Entrada(root, "Otro Ring") == 5201);
  CHECK(Bin(root, "Otro Ring", 5201, "GOtro Ring") && Bin(root, "Otro Ring", 5301, "MOtro Ring"));

  r = Revisar(root, act);   // segunda vez: nada que hacer
  CHECK(r.cambios.empty() && r.bloqueos.empty());

  // reinstalar "Otro Ring" (vuelve con su 5010 original): recupera el MISMO numero, 5201
  fs::rename(root / "Otro Ring", root / "_viejo_Otro Ring", ec);
  Escenario(root, "Otro Ring", 5010);
  // un mod nuevo que va ANTES por orden alfabetico y pide el 5010: no se lo quita a Mi Ring
  Escenario(root, "AAA Nuevo", 5010);
  act.push_back("AAA Nuevo");
  r = Revisar(root, act);
  CHECK(r.bloqueos.empty());
  CHECK(Entrada(root, "Otro Ring") == 5201);
  CHECK(Entrada(root, "Mi Ring") == 5010);
  CHECK(Entrada(root, "AAA Nuevo") == 5202);
  // Nova Arena desactivado: su 5200 sigue reservado para cuando vuelva
  Escenario(root, "Zeta", 5200);
  act = {"Campo IW 884", "Mi Ring", "Otro Ring", "AAA Nuevo", "Zeta"};
  r = Revisar(root, act);
  CHECK(Entrada(root, "Zeta") == 5203);

  // capsulas: dos mods con la misma -> bloquea y culpa al que no es generado
  Escribe(root / "_roster" / "roster.toml", "[[capsula]]\n# Uno\nid = 596\nregistro = \"00\"\n\n[[capsula]]\nid = 597\n");
  Escribe(root / "Pack suelto" / "roster.toml", "[[personaje]]\ncapsulas = [596]\n\n[[capsula]]\nid = 597\n");
  act.push_back("_roster");
  act.push_back("Pack suelto");
  r = Revisar(root, act);
  CHECK(r.bloqueos.size() == 1);
  CHECK(!r.bloqueos.empty() && r.bloqueos[0].capsula && r.bloqueos[0].mod == "Pack suelto" && r.bloqueos[0].numero == 597);

  std::printf(fallos ? "%d FALLOS\n" : "OK: todas las comprobaciones\n", fallos);
  return fallos ? 1 : 0;
}
