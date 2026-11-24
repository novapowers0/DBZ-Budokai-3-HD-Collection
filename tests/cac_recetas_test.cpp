// CAC-08/10/19 (cac1): migracion cac.toml v2 -> v3 sin perdidas + ida y vuelta de capas, recetas y formas.
// Uso: cac_recetas_test <dir-nuevo> <cac.toml v2 real>...   (copia cada v2 al dir y lo migra; nunca toca el original)
#include "src/cac/cac.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <stdexcept>
namespace C = dbz3::cac;
namespace fs = std::filesystem;
static void Exigir(bool ok, const std::string& t) { if (!ok) throw std::runtime_error(t); }
static std::string Todo(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Igual(const C::Heroe& a, const C::Heroe& b, const std::string& ctx) {
  auto hs = [](const C::Hsv& x, const C::Hsv& y) { return x.h == y.h && x.s == y.s && x.v == y.v; };
  Exigir(a.id == b.id && a.nombre == b.nombre && a.raza == b.raza && a.cuerpo == b.cuerpo && a.traje == b.traje, ctx + ": identidad");
  Exigir(a.cara == b.cara && a.pelo == b.pelo && a.ropa == b.ropa && a.color == b.color && a.voz == b.voz && a.aura == b.aura, ctx + ": aspecto");
  Exigir(a.aura_col == b.aura_col && a.ki_col == b.ki_col && a.estilo == b.estilo && a.moveset == b.moveset, ctx + ": estilo");
  for (int i = 0; i < C::kNumPartes; ++i) Exigir(hs(a.parte[i], b.parte[i]), ctx + ": parte");
  Exigir(a.logo == b.logo && a.scouter == b.scouter && hs(a.pelo_c1, b.pelo_c1) && hs(a.pelo_c2, b.pelo_c2) && hs(a.piel_c, b.piel_c) && hs(a.ojos_c, b.ojos_c), ctx + ": pelo/piel");
  for (int i = 0; i < 7; ++i) Exigir(a.xv2[i] == b.xv2[i], ctx + ": xv2");
  for (int i = 0; i < 4; ++i) Exigir(a.b3_piezas[i] == b.b3_piezas[i] && a.tecnicas[i] == b.tecnicas[i] && a.equipo[i] == b.equipo[i], ctx + ": b3/tecnicas");
  Exigir(a.b3_accesorio == b.b3_accesorio && a.ocultar == b.ocultar && a.ocultar_de == b.ocultar_de, ctx + ": accesorio/ocultar");
  Exigir(a.xp == b.xp && a.nivel == b.nivel && a.saga == b.saga && a.ph == b.ph && a.ph_vida == b.ph_vida && a.ph_dano == b.ph_dano &&
         a.maestros == b.maestros && a.progreso == b.progreso && a.esferas == b.esferas && a.deseos == b.deseos, ctx + ": progreso");
  for (int i = 0; i < C::kNumEntreno; ++i) Exigir(a.entreno[i] == b.entreno[i], ctx + ": entreno");
  Exigir(a.inv == b.inv && a.desbloqueos == b.desbloqueos, ctx + ": inventario");
  for (int i = 0; i < C::kNumCapas; ++i) Exigir(a.capas[i].fid == b.capas[i].fid && a.capas[i].parte == b.capas[i].parte && a.capas[i].quitar == b.capas[i].quitar, ctx + ": capas");
  for (int i = 0; i < C::kMaxRecetas; ++i) Exigir(a.recetas[i].nombre == b.recetas[i].nombre && a.recetas[i].ficha == b.recetas[i].ficha, ctx + ": recetas");
  for (int i = 0; i < C::kMaxFormasReceta; ++i) Exigir(a.forma_receta[i] == b.forma_receta[i] && a.forma_estallido[i] == b.forma_estallido[i], ctx + ": formas");
}
int main(int argc, char** argv) {
  try {
    Exigir(argc >= 2 && !fs::exists(argv[1]), "se necesita un directorio nuevo");
    const fs::path dir = argv[1];
    fs::create_directories(dir);
    for (int k = 2; k < argc; ++k) {   // migracion de fichas reales
      const fs::path v2 = dir / ("real" + std::to_string(k - 1) + ".toml");
      fs::copy_file(argv[k], v2);
      const std::string original = Todo(v2);
      Exigir(C::Leer(v2.string()), "leer v2 real");
      const int n = C::NumSlots();
      std::vector<C::Heroe> antes;
      for (int i = 0; i < n; ++i) antes.push_back(C::Slot(i));
      Exigir(fs::exists(v2.string() + ".v2.bak") && Todo(v2.string() + ".v2.bak") == original, "falta la copia .v2.bak intacta");
      for (const auto& h : antes)
        for (const auto& c : h.capas) Exigir(c.fid == -1, "una ficha v2 migrada debe nacer sin capas");
      Exigir(C::Guardar(v2.string()), "guardar v3");
      C::SlotEditable(0) = C::Heroe{};
      Exigir(C::Leer(v2.string()) && C::NumSlots() == n, "leer v3");
      for (int i = 0; i < n; ++i) Igual(antes[size_t(i)], C::Slot(i), "real" + std::to_string(k - 1) + " ficha " + std::to_string(i));
      std::cout << "PASS migracion v2->v3 de " << argv[k] << ": " << n << " fichas sin perdidas, .v2.bak intacta\n";
    }
    // ida y vuelta de lo nuevo
    const std::string p = (dir / "nuevo.toml").string();
    C::Heroe& h = C::SlotEditable(0);
    h = C::Heroe{};
    h.nombre = "Capas";
    h.capas[0] = {1203, 2, 5}; h.capas[2] = {40, 9, 0};
    h.recetas[0] = {"Traje \"uno\"", "cuerpo=0;traje=1;partes=1,2,3|0,0,0|0,0,0|0,0,0;pelo=3"};
    h.recetas[2] = {"Dos", "cuerpo=1;traje=0;capa1=12:3;nombre=X"};
    h.forma_receta[1] = 2; h.forma_estallido[1] = "#FFAA00";
    h.forma_receta[3] = 1;   // apunta a una receta VACIA: debe quedar -1
    C::AjustarSlot(0);
    Exigir(h.forma_receta[3] == -1, "forma sobre receta vacia no se limpia");
    const C::Heroe esperado = C::Slot(0);
    Exigir(C::Guardar(p), "guardar nuevo");
    C::SlotEditable(0) = C::Heroe{};
    Exigir(C::Leer(p), "leer nuevo");
    Igual(esperado, C::Slot(0), "nuevo");
    std::cout << "PASS capas[4], 4 recetas y formas-receta: ida y vuelta\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL cac_recetas: " << e.what() << '\n';
    return 1;
  }
}
