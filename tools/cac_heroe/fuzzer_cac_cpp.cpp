// NCAC · Fuzzer C++ de guardado y recetas sobre src/cac/cac.cpp (basado en tests/cac_recetas_test.cpp y cac_guardado_test.cpp).
// Comprueba TODOS los campos de dbz3::cac::Heroe (activos + archivados, 4 recetas, 8 formas-receta, estallidos,
// capas[4] con mascaras de 47 bits, ocultar hasta bit 63, texcol, material17, pintura17, foto_pose, foto_yaw,
// pelo_fid_cac2, orig[4], moveset_logico) en N guardados aleatorios + casos frontera + migracion v2->v3.
#include "src/cac/cac.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace C = dbz3::cac;
namespace fs = std::filesystem;

static std::vector<std::string> g_fallos;

static void Check(bool cond, const std::string& msg) {
  if (!cond) g_fallos.push_back(msg);
}

static void CompararHeroeCompleto(const C::Heroe& a, const C::Heroe& b, const std::string& ctx) {
  auto hs = [](const C::Hsv& x, const C::Hsv& y) { return x.h == y.h && x.s == y.s && x.v == y.v; };
  Check(a.id == b.id, ctx + ": id");
  Check(a.nombre == b.nombre, ctx + ": nombre ('" + a.nombre + "' vs '" + b.nombre + "')");
  Check(a.raza == b.raza && a.cuerpo == b.cuerpo && a.traje == b.traje, ctx + ": raza/cuerpo/traje");
  Check(a.cara == b.cara && a.pelo == b.pelo && a.ropa == b.ropa, ctx + ": cara/pelo/ropa");
  Check(a.pelo_fid_cac2 == b.pelo_fid_cac2, ctx + ": pelo_fid_cac2 (" + std::to_string(a.pelo_fid_cac2) + " vs " + std::to_string(b.pelo_fid_cac2) + ")");
  Check(a.foto_pose == b.foto_pose && a.foto_yaw == b.foto_yaw, ctx + ": foto_pose/foto_yaw (" +
        std::to_string(a.foto_pose) + "," + std::to_string(a.foto_yaw) + " vs " +
        std::to_string(b.foto_pose) + "," + std::to_string(b.foto_yaw) + ")");
  Check(a.color == b.color && a.voz == b.voz && a.aura == b.aura, ctx + ": color/voz/aura");
  Check(a.material17 == b.material17, ctx + ": material17");
  Check(a.pintura17 == b.pintura17, ctx + ": pintura17");
  Check(a.texcol == b.texcol, ctx + ": texcol");
  Check(a.aura_col == b.aura_col && a.ki_col == b.ki_col, ctx + ": aura_col/ki_col");
  Check(a.estilo == b.estilo && a.moveset == b.moveset && a.moveset_logico == b.moveset_logico, ctx + ": estilo/moveset");
  for (int i = 0; i < C::kNumPartes; ++i) Check(hs(a.parte[i], b.parte[i]), ctx + ": parte[" + std::to_string(i) + "]");
  Check(a.logo == b.logo && a.scouter == b.scouter, ctx + ": logo/scouter");
  Check(hs(a.pelo_c1, b.pelo_c1) && hs(a.pelo_c2, b.pelo_c2) && hs(a.piel_c, b.piel_c) && hs(a.ojos_c, b.ojos_c), ctx + ": hsv_pelo/piel/ojos");
  for (int i = 0; i < 7; ++i) Check(a.xv2[i] == b.xv2[i], ctx + ": xv2[" + std::to_string(i) + "]");
  for (int i = 0; i < 4; ++i) Check(a.b3_piezas[i] == b.b3_piezas[i], ctx + ": b3_piezas[" + std::to_string(i) + "]");
  for (int i = 0; i < 4; ++i) Check(a.orig[i] == b.orig[i], ctx + ": orig[" + std::to_string(i) + "]");
  Check(a.b3_accesorio == b.b3_accesorio, ctx + ": b3_accesorio");
  Check(a.ocultar == b.ocultar && a.ocultar_de == b.ocultar_de, ctx + ": ocultar (" +
        std::to_string(a.ocultar) + "/" + std::to_string(a.ocultar_de) + " vs " +
        std::to_string(b.ocultar) + "/" + std::to_string(b.ocultar_de) + ")");
  for (int i = 0; i < C::kNumCapas; ++i) {
    Check(a.capas[i].fid == b.capas[i].fid && a.capas[i].parte == b.capas[i].parte && a.capas[i].quitar == b.capas[i].quitar,
          ctx + ": capas[" + std::to_string(i) + "]");
  }
  for (int i = 0; i < C::kMaxRecetas; ++i) {
    Check(a.recetas[i].nombre == b.recetas[i].nombre && a.recetas[i].ficha == b.recetas[i].ficha,
          ctx + ": recetas[" + std::to_string(i) + "]");
  }
  for (int i = 0; i < C::kMaxFormasReceta; ++i) {
    Check(a.forma_receta[i] == b.forma_receta[i] && a.forma_estallido[i] == b.forma_estallido[i],
          ctx + ": forma[" + std::to_string(i) + "] (rec=" + std::to_string(a.forma_receta[i]) + " vs " +
          std::to_string(b.forma_receta[i]) + ", est='" + a.forma_estallido[i] + "' vs '" + b.forma_estallido[i] + "')");
  }
  for (int i = 0; i < C::kNumTecnicas; ++i) Check(a.tecnicas[i] == b.tecnicas[i], ctx + ": tecnicas");
  Check(a.xp == b.xp && a.nivel == b.nivel && a.saga == b.saga && a.ph == b.ph &&
        a.ph_vida == b.ph_vida && a.ph_dano == b.ph_dano && a.maestros == b.maestros &&
        a.progreso == b.progreso && a.esferas == b.esferas && a.deseos == b.deseos, ctx + ": progreso");
  for (int i = 0; i < C::kNumEntreno; ++i) Check(a.entreno[i] == b.entreno[i], ctx + ": entreno");
  Check(a.inv == b.inv && a.desbloqueos == b.desbloqueos, ctx + ": inv/desbloqueos");
  for (int i = 0; i < 4; ++i) Check(a.equipo[i] == b.equipo[i], ctx + ": equipo");
}

static C::Hsv RndHsv(std::mt19937& rng) {
  std::uniform_int_distribution<int> dh(0, 360), dsv(-100, 100);
  return {dh(rng), dsv(rng), dsv(rng)};
}

static C::Heroe RndHeroe(std::mt19937& rng, int idx) {
  C::Heroe h;
  static const char* kNombres[] = {
      "Goku \"Z\"", "Heroe\\Final", "Ñandú SSJ3", "Line1\nLine2\tTab", "Trunks 2026", "Vegeta's Pride"};
  static const char* kEstallidos[] = {"", "#FFAA00", "#64B4FF", "#DF50FF", "#FFFFFF"};
  static const int kCuerpos[] = {0, 7, 8, 10, 11, 12, 13, 15, 27, 28, 29, 30, 33, 34, 35, 36, 38, 39};
  h.nombre = kNombres[rng() % std::size(kNombres)];
  h.raza = int(rng() % 6);
  h.cuerpo = kCuerpos[rng() % std::size(kCuerpos)];
  h.traje = int(rng() % 4);
  h.cara = int(rng() % 14);
  h.pelo = int(rng() % 11);
  h.pelo_fid_cac2 = (rng() % 3 == 0) ? (rng() % 2 ? 75 : 80) : -1;
  h.foto_pose = int(rng() % 3);
  h.foto_yaw = int(rng() % 81) - 40;
  h.color = int(rng() % 32);
  h.voz = int(rng() % 32);
  h.aura = int(rng() % 8);
  h.estilo = int(rng() % 40);
  h.moveset = int(rng() % 65) - 1;
  h.moveset_logico = (rng() % 3 == 0) ? int64_t(64 + (rng() % 200)) : -1;
  for (auto& p : h.parte) p = RndHsv(rng);
  h.pelo_c1 = RndHsv(rng);
  h.pelo_c2 = RndHsv(rng);
  h.piel_c = RndHsv(rng);
  h.ojos_c = RndHsv(rng);
  h.logo = int(rng() % 8);
  h.scouter = int(rng() % 2);
  for (int i = 0; i < 4; ++i) h.b3_piezas[i] = (rng() % 2) ? int(rng() % 4096) : -1;
  h.b3_accesorio = (rng() % 2) ? int(rng() % 4096) : -1;
  for (int i = 0; i < 4; ++i) h.orig[i] = (rng() % 2) ? int(rng() % 4000) : -1;
  h.ocultar = (int64_t(rng() & 0x7FFFFFFF) << 31) | int64_t(rng() & 0x7FFFFFFF);
  h.ocultar_de = h.cuerpo * 100 + h.traje;
  if (rng() % 2) {
    std::ostringstream tc;
    int nt = 1 + int(rng() % 3);
    for (int t = 0; t < nt; ++t) {
      if (t) tc << '|';
      auto c = RndHsv(rng);
      tc << (rng() % 24) << ',' << (rng() % 6) << ',' << c.h << ',' << c.s << ',' << c.v;
    }
    h.texcol = tc.str();
  }
  if (rng() % 2) h.material17 = "0,1,2|3,4,5";
  if (rng() % 2) h.pintura17 = "0,a1b2c3d4";
  if (rng() % 2) h.aura_col = "#F0C020";
  if (rng() % 2) h.ki_col = "#D02020";
  for (int i = 0; i < C::kNumCapas; ++i) {
    if (rng() % 2) {
      h.capas[i].fid = int(rng() % 4096);
      h.capas[i].parte = int(rng() % 4);
      h.capas[i].quitar = (int64_t(rng()) << 16) ^ int64_t(rng());
    }
  }
  for (int i = 0; i < C::kMaxRecetas; ++i) {
    if (rng() % 3 != 0) {
      h.recetas[i].nombre = std::string("Traje \"") + std::to_string(idx) + "_" + std::to_string(i) + "\"";
      h.recetas[i].ficha = "cuerpo=" + std::to_string(h.cuerpo) + ";traje=" + std::to_string(h.traje) +
                           ";partes=10,20,30|0,0,0|0,0,0|0,0,0;pelo=" + std::to_string(rng() % 11);
    }
  }
  for (int i = 1; i < C::kMaxFormasReceta; ++i) {
    if (rng() % 2) {
      h.forma_receta[i] = int(rng() % C::kMaxRecetas);
      h.forma_estallido[i] = kEstallidos[rng() % std::size(kEstallidos)];
    }
  }
  h.xp = int(rng() % 60);
  h.nivel = 1 + int(rng() % 20);
  h.saga = int(rng() % 10);
  h.ph = int(rng() % 30);
  h.ph_vida = int(rng() % 10);
  h.ph_dano = int(rng() % 10);
  h.maestros = int(rng() & 63);
  h.progreso = C::kProgresoFicha;
  for (int i = 0; i < C::kNumEntreno; ++i) h.entreno[i] = int(rng() % 101);
  h.inv = {2, 6, 378, 385};
  h.equipo[0] = 2;
  h.equipo[1] = 6;
  h.esferas = int(rng() & 127);
  h.deseos = int(rng() % 10);
  h.desbloqueos = {"ssj1", "ki_dorado_goku"};
  return h;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Uso: fuzzer_cac_cpp <dir> <iteraciones> [semilla]\n";
    return 2;
  }
  const fs::path dir = argv[1];
  const int iters = std::stoi(argv[2]);
  const unsigned seed = argc >= 4 ? unsigned(std::stoul(argv[3])) : 20261124u;
  fs::create_directories(dir);
  std::mt19937 rng(seed);

  // 1. Fuzz de ida y vuelta en cac.toml con 1..3 slots activos + archivados
  for (int it = 0; it < iters; ++it) {
    // Reset estado global a 1 slot limpio
    while (C::NumSlots() > 1) C::BorrarSlot(C::NumSlots() - 1);
    const std::string p = (dir / ("fuzz_" + std::to_string(it) + ".toml")).string();
    const int nslots = 1 + int(rng() % C::kMaxSlots);
    C::SlotEditable(0) = RndHeroe(rng, it * 10);
    C::AjustarSlot(0);
    for (int s = 1; s < nslots; ++s) {
      int idx = C::NuevoSlot();
      if (idx >= 0) {
        C::SlotEditable(idx) = RndHeroe(rng, it * 10 + s);
        C::AjustarSlot(idx);
      }
    }
    C::ElegirSlot(int(rng() % C::NumSlots()));
    const int activo_antes = C::SlotActivo();
    std::vector<C::Heroe> antes;
    for (int s = 0; s < C::NumSlots(); ++s) antes.push_back(C::Slot(s));
    Check(C::Guardar(p), "fuzz#" + std::to_string(it) + ": Guardar fallo");
    // Contaminar memoria antes de Leer
    for (int s = 0; s < C::NumSlots(); ++s) C::SlotEditable(s) = C::Heroe{};
    Check(C::Leer(p), "fuzz#" + std::to_string(it) + ": Leer fallo");
    Check(C::NumSlots() == int(antes.size()), "fuzz#" + std::to_string(it) + ": NumSlots");
    Check(C::SlotActivo() == activo_antes, "fuzz#" + std::to_string(it) + ": SlotActivo");
    for (size_t s = 0; s < antes.size() && int(s) < C::NumSlots(); ++s) {
      CompararHeroeCompleto(antes[s], C::Slot(int(s)), "fuzz#" + std::to_string(it) + ".slot" + std::to_string(s));
    }
  }

  // 2. Casos frontera criticos (ocultar bit 63, forma_receta[0], forma_estallido invalido, RestaurarColoresCac2)
  {
    while (C::NumSlots() > 1) C::BorrarSlot(C::NumSlots() - 1);
    const std::string p = (dir / "frontera.toml").string();
    C::Heroe& h = C::SlotEditable(0);
    h = C::Heroe{};
    h.nombre = "Frontera";
    h.recetas[0] = {"R0", "cuerpo=0;traje=0;partes=10,20,30|0,0,0|0,0,0|0,0,0"};
    // Frontera A: forma_receta[0] es la forma base (no deberia aceptar receta de forma: solo 1..7)
    h.forma_receta[0] = 0;
    h.forma_estallido[0] = "#FFAA00";
    // Frontera B: forma_estallido[1] con texto no valido (no es #RRGGBB)
    h.forma_receta[1] = 0;
    h.forma_estallido[1] = "NO_ES_HEX;inyeccion=1";
    // Frontera C: ocultar negativo / bit 63
    h.ocultar = int64_t(1ULL << 62);
    h.ocultar_de = 0;
    // Frontera D: pelo_fid_cac2=75 con pelo=3 a la vez (dos pelos activos en la ficha)
    h.pelo = 3;
    h.pelo_fid_cac2 = 75;
    C::AjustarSlot(0);
    std::cout << "EDGE forma_receta[0]=" << h.forma_receta[0]
              << " forma_estallido[1]='" << h.forma_estallido[1] << "'"
              << " pelo=" << h.pelo << " pelo_fid_cac2=" << h.pelo_fid_cac2 << "\n";
    const C::Heroe snap = C::Slot(0);
    Check(C::Guardar(p), "frontera: Guardar");
    C::SlotEditable(0) = C::Heroe{};
    Check(C::Leer(p), "frontera: Leer");
    CompararHeroeCompleto(snap, C::Slot(0), "frontera");

    // Frontera E: RestaurarColoresCac2 preserva piezas/capas/recetas y limpia colores
    C::Heroe r = snap;
    r.parte[0] = {120, 50, 50};
    r.pelo_c1 = {45, 20, 20};
    r.texcol = "0,1,120,20,10";
    r.pintura17 = "0,deadbeef";
    C::RestaurarColoresCac2(r);
    Check(r.parte[0].h == 0 && r.pelo_c1.h == 0 && r.texcol.empty() && r.pintura17.empty(),
          "frontera: RestaurarColoresCac2 no limpio colores");
    Check(r.recetas[0].ficha == snap.recetas[0].ficha && r.ocultar == snap.ocultar,
          "frontera: RestaurarColoresCac2 altero recetas/ocultar");
  }

  // 3. Caso frontera F: TOML corrupto con texcol/capas/formas fuera de rango
  {
    const std::string p = (dir / "hostil.toml").string();
    std::ofstream f(p);
    f << "version = 3\nactivo = 0\n[[heroe]]\nnombre = \"Hostil\"\n"
      << "cuerpo = 0\ntraje = 0\ntexcol = \"0,9,999,0,0\"\n"
      << "capas = [[268, 99, 7], [-1, 0, 0], [999999, 0, 0], [268, 0, -5]]\n"
      << "receta_nombres = [\"R0\", \"\", \"\", \"\"]\n"
      << "receta_fichas = [\"cuerpo=0;traje=0\", \"\", \"\", \"\"]\n"
      << "forma_receta = [0, 0, 2, 99, -5, 0, 0, 0]\n"
      << "forma_estallido = [\"#FFFFFF\", \"basura\", \"#FFAA00\", \"#FFAA00\", \"#FFAA00\", \"\", \"\", \"\"]\n";
    f.close();
    Check(C::Leer(p), "hostil: Leer");
    const C::Heroe& h = C::Slot(0);
    Check(h.texcol.empty(), "hostil: texcol invalido no limpiado");
    Check(h.capas[0].fid == -1 && h.capas[2].fid == -1, "hostil: capas invalidas no rechazadas");
    Check(h.capas[3].fid == 268 && h.capas[3].quitar == 0, "hostil: quitar negativo no saneado");
    Check(h.forma_receta[2] == -1 && h.forma_receta[3] == -1 && h.forma_receta[4] == -1,
          "hostil: forma_receta a receta vacia/fuera de rango no limpiada");
    Check(h.forma_estallido[2].empty() && h.forma_estallido[3].empty(),
          "hostil: forma_estallido de forma limpiada no borrado");
  }

  std::cout << "FUZZ_CPP_ITERS=" << iters << " SEED=" << seed << " FALLOS=" << g_fallos.size() << "\n";
  for (const auto& s : g_fallos) std::cout << "FAIL: " << s << "\n";
  return g_fallos.empty() ? 0 : 1;
}
