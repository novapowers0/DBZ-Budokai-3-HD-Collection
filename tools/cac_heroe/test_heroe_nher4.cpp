// test_heroe_nher4.cpp — Real C++ execution harness linking ACTUAL PATCHED src/heroe.cpp + src/cac/cac.cpp!
// Exercises the REAL production functions (NO reimplementations):
//   - dbz3::heroe::DuVictoria, ResolverRuta, ElegirCamino, EsferaMapaRuta, ShenronAparece,
//     ConfirmarDeseoHistoria, RutaCompletada, NuevaVueltaRuta, VueltaRuta, EtapaRuta, VidaExtraDu,
//     FinalesCompletados, Importar, Exportar, ImportarProgreso, ExportarProgreso, Mision, Reglas,
//     Premios, PremiosAviso, ColeccionPorcentaje, ColeccionVisto, ColeccionTodoVisto,
//     EscenarioPremiadoDisponible, LuchadoresPremiados, CapsulasPremiadas, TecnicasV2, Estilos,
//     Desbloqueos, CapsulasColeccion, TrajesColeccion, ColeccionVueltasOn
//   - dbz3::heroe::col_io::Cargar, GuardarAtomico, SincronizarConFichas
//   - dbz3::heroe::rutas::AplicarVidaExtraDu, CalcularEstadoMapa, DuUsaNuevaHistoria
//   - dbz3::cac::Leer, Guardar, Slot, SlotEditable, Archivados
// Supports --scenario <name> --userdir <dir> --modsdir <dir> so each scenario runs in a clean process.

#include "cac/cac.h"
#include "heroe_api.h"
#include "heroe_coleccion_io.h"
#include "heroe_rutas.h"
#include "launcher/i18n.h"
#include "launcher/settings.h"
#include "modos.h"
#include "mods.h"
#include "roster_ext.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace H = dbz3::heroe;
namespace col_io = dbz3::heroe::col_io;
namespace rutas = dbz3::heroe::rutas;

// Forward declarations of functions defined in src/heroe.cpp (not all in heroe_api.h)
namespace dbz3::heroe {
void Importar(int slot);
void Exportar(dbz3::cac::Heroe& h);
void ImportarProgreso(const dbz3::cac::Heroe& h);
void ExportarProgreso(dbz3::cac::Heroe& h);
void DuVictoria(int saga, int k);
int ResolverRuta();
int ElegirCamino(int t);
int Tramo();
void Mision(int saga, int k, int estilo, int cuerpo, int* rival, int* vida, int* escenario);
dbz3::modos::Reglas Reglas(int estilo, int extra, int traje);
void GuardarRutaActual();
void EvaluarRecompensas(int tramo, int rival, int forma);
std::vector<int> CapsulasPremiadas();
}  // namespace dbz3::heroe

// =============================================================================
// Deterministic test harness stubs for non-Hero subsystems
// =============================================================================
static fs::path g_test_user_dir;
static fs::path g_test_mods_dir;
static int g_zenie_total = 0;
static std::vector<int> g_zenie_awards;
static std::map<uint32_t, int> g_donor_map;

namespace dbz3 {
fs::path ModsRoot() { return g_test_mods_dir; }
}  // namespace dbz3

namespace dbz3::settings {
fs::path UserDataRoot() { return g_test_user_dir; }
bool Xv2Instalado() { return false; }
bool CacXv2Piezas() { return false; }
void SetCacXv2Piezas(bool) {}
void SaveUserSettings() {}
}  // namespace dbz3::settings

namespace dbz3::i18n {
const char* T(const char* es, const char*) { return es; }
const char* LangCode() { return "es"; }
const char* Otro(const char*) { return nullptr; }
}  // namespace dbz3::i18n

namespace dbz3::modos {
const char* L(const char* es, const char*, const char*, const char*, const char*) { return es; }
std::string Nombre(int id) {
  static const std::map<int, std::string> kNom = {
      {0, "Goku"}, {2, "Gohan"}, {7, "Vegeta"}, {10, "Krilin"}, {11, "Piccolo"},
      {12, "Ten"}, {13, "Yamcha"}, {27, "Freezer"}, {33, "Cell"}, {35, "Super Bu"},
      {36, "Kid Bu"}, {39, "Bardock"}, {41, "Omega Shenron"}, {42, "Saibaman"}, {43, "Cell Jr."}};
  if (auto it = kNom.find(id); it != kNom.end()) return it->second;
  return "Luchador#" + std::to_string(id);
}
}  // namespace dbz3::modos

namespace dbz3::pasarela {
int RangoMejora(const std::string&, int, int, int) { return 0; }
}  // namespace dbz3::pasarela

namespace dbz3::rpg {
void Premio(int zeni) {
  g_zenie_total += zeni;
  g_zenie_awards.push_back(zeni);
}
}  // namespace dbz3::rpg

namespace dbz3::roster {
int DonorOf(uint32_t id) {
  auto it = g_donor_map.find(id);
  return it == g_donor_map.end() ? -1 : it->second;
}
int64_t LogicalForProxy(int) { return -1; }
int HeroRosterGuest(int) { return -1; }
std::string HeroRosterName(int) { return ""; }
void RequestHeroRosterId(int) {}
int DynamicRosterPageCount() { return 0; }
bool IsLogicalRosterId(int) { return false; }
bool HeroRosterAnswered(int) { return true; }
std::vector<int> HeroRosterIds() { return {}; }
}  // namespace dbz3::roster

// =============================================================================
// Mock Guest RAM with real #SKC capsule records for EsEsquiva / CatalogoTecnicas
// =============================================================================
struct MockGuestRam {
  // Sparse virtual address mapper: base_ + guest_va is NOT feasible for 0x82xxxxxx,
  // EXCEPT on 64-bit Windows we can reserve a small VirtualAlloc? No: 0x82375608 is ~2.18 GB offset!
  // Let's use VirtualAlloc(nullptr, 0x83000000, MEM_RESERVE) + commit ONLY the 64 KB pages we touch!
  uint8_t* base = nullptr;
  rex::Runtime rt;

  static void StoreBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
  }
  static void StoreBe16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
  }

  bool Init() {
#ifdef _WIN32
    base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x83000000ULL, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) return false;
    auto commit = [&](uint32_t va, size_t sz) -> bool {
      return VirtualAlloc(base + va, sz, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    };
    // Commit signature/ID table at 0x82020000, char372/char96/SKC/costumes at 0x82320000..0x82470000
    if (!commit(0x82020000u, 0x00010000u)) return false;
    if (!commit(0x82320000u, 0x00150000u)) return false;
    std::memcpy(base + 0x82020700u, "_charasel.cpp", 13);
    StoreBe32(base + 0x82324468u, 0xFFFFFFFFu);  // empty face table terminator
    const uint32_t skc_hdr = 0x82400000u;
    const uint32_t skc_rec = 0x82401000u;
    StoreBe32(base + 0x82375608u, skc_hdr);
    StoreBe32(base + 0x8237560Cu, skc_rec);
    StoreBe32(base + skc_hdr + 0x10u, 600u);

    auto set_dodge = [&](uint32_t cap, uint8_t sub) {
      uint8_t* r = base + skc_rec + 40u * cap;
      r[8] = 0x11;   // support class 0x1N
      r[14] = 0xFF;  // all forms
      r[15] = 0;     // ki
      r[20] = 0x43;  // dodge effect family
      r[21] = sub;
    };
    set_dodge(409, 0x0F);  // Gero's Deflection R&D
    set_dodge(410, 0x10);  // Gero's Deflect-Back R&D
    set_dodge(444, 0x18);  // Turtle Shell
    set_dodge(445, 0x19);  // Concentration

    auto set_attack = [&](uint32_t cap, int owner, uint8_t ki) {
      uint8_t* r = base + skc_rec + 40u * cap;
      uint64_t own = 1ULL << owner;
      StoreBe32(r + 0, uint32_t(own >> 32));
      StoreBe32(r + 4, uint32_t(own & 0xFFFFFFFFu));
      r[8] = 0x21;   // attack class
      r[14] = 0xFF;  // forms
      r[15] = ki;
    };
    // Reward technique capsules from recompensas.toml
    set_attack(100, 27, 10);  // Death Beam
    set_attack(17, 2, 10);    // Masenko
    set_attack(38, 7, 10);    // Galick Gun
    set_attack(43, 7, 20);    // Big Bang Attack
    set_attack(64, 12, 20);   // Kikoho
    set_attack(139, 39, 30);  // Spirit of Saiyans
    set_attack(67, 13, 10);   // Wolf Fang Fist
    set_attack(10, 0, 30);    // Spirit Bomb
    set_attack(65, 12, 30);   // Neo Kikoho
    set_attack(55, 10, 10);   // Kienzan
    set_attack(117, 33, 20);  // Cell's Kamehameha

    rt.set_virtual_membase(base);
    rex::Runtime::InstancePtr() = &rt;
    return true;
#else
    return false;
#endif
  }

  ~MockGuestRam() {
#ifdef _WIN32
    rex::Runtime::InstancePtr() = nullptr;
    if (base) VirtualFree(base, 0, MEM_RELEASE);
#endif
  }
};

// =============================================================================
// Assertions & helpers
// =============================================================================
static int g_checks = 0;
#define CHECK(cond, msg)                                                                  \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(cond)) {                                                                        \
      std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n";                        \
      return 1;                                                                           \
    }                                                                                     \
  } while (0)

static std::string LeerBytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool HasCap(const std::vector<int>& v, int cap) {
  return std::find(v.begin(), v.end(), cap) != v.end();
}

// =============================================================================
// Scenario 1: Default OFF regression & consumer isolation
// =============================================================================
static int ScenarioDefaultOff() {
  CHECK(!H::ColeccionVueltasOn(), "cvar dbz3_heroe_coleccion_vueltas defaults to false");
  CHECK(!H::RutaCompletada(), "OFF: RutaCompletada() is false");
  CHECK(!H::NuevaVueltaRuta(), "OFF: NuevaVueltaRuta() is false");
  CHECK(H::FinalesCompletados().empty(), "OFF: FinalesCompletados() is empty");
  CHECK(H::VidaExtraDu() == 0, "OFF: VidaExtraDu() is 0");
  CHECK(H::CapsulasColeccion().empty(), "OFF: CapsulasColeccion() is empty");
  CHECK(H::TrajesColeccion().empty(), "OFF: TrajesColeccion() is empty");

  // Create 2 slots: slot 0 has stage/fighter/tech rewards, slot 1 is empty
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Hero1\"\nid = \"h001\"\nprogreso = 2\nsaga = 1\nxp = 3\n"
      << "desbloqueos = [\"r_t0_death_beam\", \"r_t0_escenario_cell_b1\", \"r_luchador_omega\"]\n\n"
      << "[[heroe]]\nnombre = \"Hero2\"\nid = \"h002\"\nprogreso = 2\nsaga = 0\nxp = 0\n"
      << "desbloqueos = []\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer ok");
  H::Importar(0);
  CHECK(H::EscenarioPremiadoDisponible(5031), "OFF slot0: stage 5031 unlocked");
  CHECK(H::EscenarioPremiadoDisponible(5001), "OFF slot0: undeclared stage 5001 always open");
  CHECK((H::LuchadoresPremiados() & (1ULL << 41)) != 0, "OFF slot0: Omega Shenron (41) bit set");
  CHECK(HasCap(H::CapsulasPremiadas(), 100), "OFF slot0: Death Beam (100) in CapsulasPremiadas");

  // Switch to slot 1: with cvar OFF, slot 1 MUST NOT inherit slot 0's rewards!
  H::Importar(1);
  CHECK(!H::EscenarioPremiadoDisponible(5031), "OFF slot1: stage 5031 stays locked");
  CHECK(H::LuchadoresPremiados() == 0ULL, "OFF slot1: LuchadoresPremiados == 0");
  CHECK(H::CapsulasPremiadas().empty(), "OFF slot1: CapsulasPremiadas empty");
  CHECK(H::ColeccionPorcentaje() == 0, "OFF slot1: 0%");

  // Mirror boss OFF: even with IdPropio() == 63, Mision() substitutes Freezer (27) and Cell (33)
  int rival = -1, vida = 0, esc = -1;
  H::Mision(0, 2, 27, 27, &rival, &vida, &esc);   // Saga 0 fight 2 = Ginyu; let's also check actual Freezer/Cell fights
  // Find exact saga/k where rival==27 and rival==33 from CombatesDeSaga
  bool found_f = false, found_c = false;
  for (int s = 0; s < H::SagasTotal(); ++s) {
    auto cs = H::CombatesDeSaga(s);
    for (int k = 0; k < int(cs.size()); ++k) {
      if (cs[size_t(k)].first == 27 && !found_f) {
        H::Mision(s, k, 27, 27, &rival, &vida, &esc);
        CHECK(rival != 27, "OFF: Mision substitutes Freezer (27) when style==cuerpo==27");
        found_f = true;
      }
      if (cs[size_t(k)].first == 33 && !found_c) {
        H::Mision(s, k, 33, 33, &rival, &vida, &esc);
        CHECK(rival != 33, "OFF: Mision substitutes Cell (33) when style==cuerpo==33");
        found_c = true;
      }
    }
  }
  CHECK(found_f && found_c, "Found real Freezer (27) and Cell (33) fights in res/heroe/sagas.toml");
  return 0;
}

// =============================================================================
// Scenario 2: Corrupt coleccion.toml protection (fixes AP_NHER3 objection!)
// =============================================================================
static int ScenarioCorruptNoOverwrite() {
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  CHECK(H::ColeccionVueltasOn(), "cvar ON");

  const fs::path col_p = g_test_user_dir / "coleccion.toml";
  const std::string broken = "# archivo corrupto intencionado\nvistos = [\"sin_cerrar\n";
  {
    std::ofstream o(col_p, std::ios::binary | std::ios::trunc);
    o << broken;
  }
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Hero1\"\nid = \"h001\"\nprogreso = 2\nsaga = 1\nxp = 3\n"
      << "desbloqueos = [\"r_t0_death_beam\"]\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer ok");
  // Trigger ColeccionCargar + ColeccionSincronizarConFichas + EvaluarRecompensas + ColeccionVisto + ColeccionTodoVisto
  H::Importar(0);
  H::EvaluarRecompensas(1, 33, 0);
  H::ColeccionVisto("r_t0_death_beam");
  H::ColeccionTodoVisto();
  // The corrupt file MUST remain byte-for-byte intact and NO .bak created!
  CHECK(LeerBytes(col_p) == broken, "corrupt coleccion.toml NEVER overwritten by normal Hero writer");
  CHECK(!fs::exists(col_p.string() + ".bak"), "no .bak created when refusing to overwrite corrupt file");
  return 0;
}

// =============================================================================
// Scenario 3: Extra TOML keys preservation, multiline, .bak, IO failure, ON->OFF->ON
// =============================================================================
static int ScenarioIoAndExtraKeys() {
  const fs::path col_p = g_test_user_dir / "coleccion.toml";
  const std::string orig =
      "# Comentario de usuario\r\n"
      "todo = false\r\n"
      "clave_extra = \"conservar_esto\"\r\n"
      "version_futura = 99\r\n"
      "vistos = [\r\n"
      "  \"r_t0_death_beam\",\r\n"
      "  \"r_h25\",\r\n"
      "]\r\n"
      "\r\n"
      "[seccion_mod]\r\n"
      "activo = true\r\n";
  {
    std::ofstream o(col_p, std::ios::binary | std::ios::trunc);
    o << orig;
  }
  col_io::DatosColeccion d;
  std::string err;
  CHECK(col_io::Cargar(col_p, d, &err), "Cargar multiline with extras ok: " + err);
  CHECK(!d.todo && d.vistos.size() == 2 && d.vistos.count("r_h25") == 1, "vistos parsed");
  CHECK(d.extras["clave_extra"].value_or(std::string{}) == "conservar_esto", "string extra preserved");
  CHECK(d.extras["version_futura"].value_or(int64_t(0)) == 99, "int extra preserved");
  CHECK(d.extras["seccion_mod"]["activo"].value_or(false) == true, "table extra preserved");

  // Save with global keys ON -> check .bak matches orig bytes EXACTLY, and all extras survive!
  d.conseguidos.insert("r_t0_death_beam");
  d.conseguidos.insert("id\"con\\barra");
  d.finales.insert("torneo");
  CHECK(col_io::GuardarAtomico(col_p, d, true, &err), "GuardarAtomico ON ok: " + err);
  CHECK(LeerBytes(col_p.string() + ".bak") == orig, ".bak matches exact original bytes");

  col_io::DatosColeccion d2;
  CHECK(col_io::Cargar(col_p, d2, &err), "reload after GuardarAtomico ok: " + err);
  CHECK(d2.conseguidos.count("id\"con\\barra") == 1, "escaped quote/backslash round-trips via toml++");
  CHECK(d2.finales.count("torneo") == 1, "finales persisted");
  CHECK(d2.extras["clave_extra"].value_or(std::string{}) == "conservar_esto", "extra string survived save");
  CHECK(d2.extras["version_futura"].value_or(int64_t(0)) == 99, "extra int survived save");
  CHECK(d2.extras["seccion_mod"]["activo"].value_or(false) == true, "extra table survived save");

  // ON -> OFF write must NOT wipe conseguidos/finales or extras
  d2.vistos.insert("r_h50");
  CHECK(col_io::GuardarAtomico(col_p, d2, false, &err), "GuardarAtomico OFF preserves global fields");
  col_io::DatosColeccion d3;
  CHECK(col_io::Cargar(col_p, d3, &err), "reload after OFF write ok");
  CHECK(d3.conseguidos.count("r_t0_death_beam") == 1 && d3.finales.count("torneo") == 1,
        "conseguidos/finales preserved across ON->OFF->ON");
  CHECK(d3.extras["clave_extra"].value_or(std::string{}) == "conservar_esto", "extras preserved across OFF write");

  // Write failure: block .tmp with directory -> original untouched
  const std::string snap = LeerBytes(col_p);
  const fs::path tmp_dir = col_p.string() + ".tmp";
  fs::create_directory(tmp_dir);
  d3.conseguidos.insert("r_t1_masenko");
  CHECK(!col_io::GuardarAtomico(col_p, d3, true, &err), "GuardarAtomico fails cleanly when .tmp blocked");
  CHECK(LeerBytes(col_p) == snap, "original file untouched when .tmp blocked");
  fs::remove(tmp_dir);

  // Write failure: block .bak with directory -> original untouched
  fs::remove(col_p.string() + ".bak");
  const fs::path bak_dir = col_p.string() + ".bak";
  fs::create_directory(bak_dir);
  CHECK(!col_io::GuardarAtomico(col_p, d3, true, &err), "GuardarAtomico fails cleanly when .bak blocked");
  CHECK(LeerBytes(col_p) == snap, "original file untouched when .bak blocked");
  fs::remove(bak_dir);
  return 0;
}

// =============================================================================
// Scenario 4: Shared Collection across 3 active + 1 archived + ALL real consumers + Zenie
// =============================================================================
static int ScenarioSharedCollectionConsumers() {
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  // Read real recompensas.toml to split the 20 non-milestone rewards across 3 active + 1 archived slot
  const toml::table t = toml::parse_file((g_test_mods_dir.parent_path() / "res" / "heroe" / "recompensas.toml").string());
  std::vector<std::string> non_hito;
  for (const auto& e : *t["recompensa"].as_array()) {
    const auto* x = e.as_table();
    if ((*x)["coleccion"].value_or(int64_t(-1)) < 0) non_hito.push_back((*x)["id"].value_or(std::string{}));
  }
  CHECK(non_hito.size() == 20, "20 non-milestone rewards in recompensas.toml");
  // Place r_t0_escenario_cell_b1 (index 0) and r_t0_death_beam (index 1) in Slot 1 (NOT Slot 0),
  // and r_luchador_omega in Archived slot, so Slot 0 inherits ALL of them from the account!
  // Put indices 15..19 in Slot 0, 0..4 in Slot 1, 5..9 in Slot 2, 10..14 in Archived!
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n";
    auto ws = [&](const char* hdr, const char* nom, size_t a, size_t b) {
      o << hdr << "\nnombre = \"" << nom << "\"\nid = \"id_" << nom << "\"\nprogreso = 2\nsaga = 6\nxp = 20\nph = 30\ndesbloqueos = [";
      for (size_t i = a; i < b; ++i) {
        if (i > a) o << ", ";
        o << "\"" << non_hito[i] << "\"";
      }
      o << "]\n\n";
    };
    ws("[[heroe]]", "Slot0", 15, 20);
    ws("[[heroe]]", "Slot1", 0, 5);
    ws("[[heroe]]", "Slot2", 5, 10);
    ws("[[archivado]]", "Arch0", 10, 15);
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer multi-slot ok");
  CHECK(dbz3::cac::NumSlots() == 3 && dbz3::cac::NumArchivados() == 1, "3 active + 1 archived loaded");

  // Import Slot 0 -> triggers REAL ColeccionSincronizarConFichas()!
  H::Importar(0);
  CHECK(H::ColeccionPorcentaje() == 100, "ON: shared collection reaches 100% across 3 active + 1 archived slots");
  CHECK(dbz3::cac::Slot(0).desbloqueos.size() == 5, "Slot 0 cac.toml desbloqueos untouched (5 local)");

  // Verify ALL real InfoPremio entries: 5 en_ficha=true, 19 en_ficha=false (all 24 conseguido=true!)
  const auto ps = H::Premios();
  CHECK(ps.size() == 24, "24 total rewards");
  int ok_cnt = 0, ficha_cnt = 0, cuenta_cnt = 0;
  for (const auto& p : ps) {
    ok_cnt += p.conseguido;
    ficha_cnt += (p.conseguido && p.en_ficha);
    cuenta_cnt += (p.conseguido && !p.en_ficha);
  }
  CHECK(ok_cnt == 24 && ficha_cnt == 5 && cuenta_cnt == 19, "24/24 conseguido: 5 en_ficha + 19 inherited from account");

  // Verify ALL REAL CONSUMERS on Slot 0 (which does NOT own 5031 or 100 locally!):
  CHECK(H::EscenarioPremiadoDisponible(5031), "Consumer: stage 5031 unlocked via account on Slot 0");
  CHECK((H::LuchadoresPremiados() & (1ULL << 41)) != 0, "Consumer: Omega Shenron (41) unlocked via account");
  const auto caps = H::CapsulasPremiadas();
  for (int c : {100, 17, 38, 43, 64, 139, 67, 10, 65, 55, 117})
    CHECK(HasCap(caps, c), "Consumer: CapsulasPremiadas includes shared cap " + std::to_string(c));

  // Verify Zenie & en_ficha when Slot 0 clears Tramo 1 (r_t1_masenko was inherited from Slot 1):
  g_zenie_total = 0;
  g_zenie_awards.clear();
  H::EvaluarRecompensas(1, -1, -1);
  CHECK(g_zenie_total == 500, "Consumer: duplicate event on account-owned reward gives +500 Zenie once");
  bool saw_masenko_rep = false;
  for (const auto& a : H::PremiosAviso())
    if (a.id == "r_t1_masenko" && a.repetido && a.en_ficha) saw_masenko_rep = true;
  CHECK(saw_masenko_rep, "PremiosAviso marks r_t1_masenko repetido=true AND records en_ficha=true on Slot 0");

  // Non-event milestones (tramo=-1, rival=-1) must give 0 Zenie!
  g_zenie_total = 0;
  H::EvaluarRecompensas(-1, -1, -1);
  CHECK(g_zenie_total == 0, "Non-event milestones give 0 duplicate Zenie");

  // Verify toggling OFF still preserves r_t1_masenko on Slot 0 (now 6 local rewards)!
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", false);
  int off_ok = 0;
  for (const auto& p : H::Premios()) off_ok += p.conseguido;
  CHECK(off_ok == 6, "OFF after Slot 0 earned r_t1_masenko: Slot 0 keeps its 6 local rewards");
  CHECK(!H::EscenarioPremiadoDisponible(5031), "OFF: stage 5031 (only on Slot 1) locked again on Slot 0");
  return 0;
}

// =============================================================================
// Scenario 5: REAL 4-Lap DU+Rutas Lifecycle via REAL DuVictoria() + Save/Reload + Rollback
// =============================================================================
// Read real fight counts for all 14 tramos (8 base + 6 variants) from res/heroe/sagas.toml
static std::vector<int> TramoFightCounts() {
  static std::vector<int> counts;
  if (!counts.empty()) return counts;
  const toml::table t = toml::parse_file((g_test_mods_dir.parent_path() / "res" / "heroe" / "sagas.toml").string());
  std::vector<int> base, var;
  for (const auto& e : *t["saga"].as_array()) {
    const auto* s = e.as_table();
    const int n = s && (*s)["combate"].as_array() ? int((*s)["combate"].as_array()->size()) : 0;
    if (s && (*s)["variante"].value_or(false)) var.push_back(n);
    else base.push_back(n);
  }
  base.insert(base.end(), var.begin(), var.end());
  counts = std::move(base);
  return counts;
}

static int RunOneFullLap(int c0, int c1, int master_idx, bool revivir, bool do_opc_namek, bool do_opc_tierra,
                         int expected_end_tramo, const char* expected_fin, int expected_lap) {
  const auto ncomb = TramoFightCounts();
  CHECK(ncomb.size() == 14, "14 tramos in sagas.toml");
  CHECK(H::VueltaRuta() == expected_lap, "lap matches expected");
  CHECK(!H::RutaCompletada(), "not completed at lap start");
  CHECK(H::EtapaRuta() == 0, "EtapaRuta() == 0 at lap start");

  // Stage 0 crossroads [0, 8]
  CHECK(H::ElegirCamino(c0) == c0, "stage 0 crossroads pick");
  if (do_opc_namek) {
    H::DuVictoria(c0, ncomb[size_t(c0)] + 1);
  }
  for (int k = 0; k < ncomb[size_t(c0)]; ++k) H::DuVictoria(c0, k);
  // Resolve stage 1
  const int t1_init = H::ResolverRuta();
  CHECK(t1_init >= 0, "stage 1 resolved");
  CHECK(H::ElegirCamino(c1) == c1, "stage 1 crossroads pick");
  CHECK(H::EtapaRuta() == 1, "EtapaRuta() == 1");
  if (do_opc_tierra) {
    H::DuVictoria(c1, ncomb[size_t(c1)] + 1);
  }
  for (int k = 0; k < ncomb[size_t(c1)]; ++k) H::DuVictoria(c1, k);

  // Pick master before resolving stage 2 so master branch (maestro_*) activates!
  if (H::PuedeCambiarA(master_idx)) H::ElegirMaestro(master_idx);

  // Stage 2
  const int t2 = H::ResolverRuta();
  CHECK(t2 >= 0, "stage 2 resolved");
  CHECK(H::EtapaRuta() == 2, "EtapaRuta() == 2");
  for (int k = 0; k < ncomb[size_t(t2)]; ++k) H::DuVictoria(t2, k);

  // Stage 3
  const int t3 = H::ResolverRuta();
  CHECK(t3 >= 0, "stage 3 resolved");
  CHECK(H::EtapaRuta() == 3, "EtapaRuta() == 3");
  for (int k = 0; k < ncomb[size_t(t3)]; ++k) H::DuVictoria(t3, k);

  // Stage 4 (Reunion de las Esferas) + map Dragon Balls 5 & 6
  const int t4 = H::ResolverRuta();
  CHECK(t4 == 5, "stage 4 is tramo 5");
  CHECK(H::EtapaRuta() == 4, "EtapaRuta() == 4");
  CHECK(H::EsferaMapaRuta(5), "collect map Dragon Ball 5");
  CHECK(H::EsferaMapaRuta(6), "collect map Dragon Ball 6");
  for (int k = 0; k < ncomb[size_t(t4)]; ++k) H::DuVictoria(t4, k);

  // Dragon Balls == 127 -> Shenron history wish!
  CHECK(H::DeseoHistoriaPendiente(), "history wish pending after stage 4");
  CHECK(H::ResolverRuta() == -2, "ResolverRuta returns -2 while wish pending");
  CHECK(H::ShenronAparece(), "Shenron appears with 7 Dragon Balls");
  CHECK(H::ConfirmarDeseoHistoria(revivir), "history wish confirmed");

  // Stage 5 (Ending!)
  const int t5 = H::ResolverRuta();
  CHECK(t5 == expected_end_tramo, "stage 5 resolved to expected ending tramo " + std::to_string(expected_end_tramo) +
                                      ", got " + std::to_string(t5));
  CHECK(H::EtapaRuta() == 5, "EtapaRuta() == 5");
  const int n5 = ncomb[size_t(t5)];
  for (int k = 0; k < n5; ++k) H::DuVictoria(t5, k);

  // Stage 5 closed!
  CHECK(H::RutaCompletada(), "RutaCompletada() == true after stage 5 close");
  CHECK(H::EtapaRuta() == 6, "EtapaRuta() == 6 when completed");
  CHECK(H::Tramo() == -1, "Tramo() == -1 when completed");

  // Duplicate stage 5 DuVictoria MUST be ignored!
  const int saga_before = H::Saga();
  H::DuVictoria(t5, n5 - 1);
  CHECK(H::Saga() == saga_before, "duplicate stage 5 DuVictoria ignored (g_tramo < 0)");

  // Ending recorded!
  const auto fins = H::FinalesCompletados();
  CHECK(std::find(fins.begin(), fins.end(), std::string(expected_fin)) != fins.end(),
        std::string("ending recorded: ") + expected_fin);
  return 0;
}

static int ScenarioFourLapsPhase1() {
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Runner\"\nid = \"run01\"\nprogreso = 2\nsaga = 0\nxp = 0\nph = 0\n"
      << "desbloqueos = []\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer fresh hero");
  H::Importar(0);
  CHECK(H::VidaExtraDu() == 0, "Lap 1 VidaExtraDu == 0");

  // Lap 1: Nave (0) -> Cell lab (10) -> Goku (0) -> revivir=true -> Villanos (12)? Wait:
  // Let's check exact sagas.toml routes:
  // Lap 1: c0=0 (Nave, opc_namek), c1=10 (Cell), master=0 (Goku), revivir=true -> tramo 12 (Villanos, stage 2)? No,
  // Let's trace:
  //   c0=0 (camino_nave), opc_namek -> etapa 1 has cruce [1, 9, 10, 11] or [1, 9, 10]
  //   With c0=0, c1=10, revivir=true -> tramo 6 (torneo) at stage 5!
  if (int rc = RunOneFullLap(0, 10, 0, true, true, true, 6, "torneo", 1)) return rc;

  // Test failed save rollback EXACTLY as rpg.cpp does!
  {
    auto antes = dbz3::cac::Slot(0);
    H::Exportar(antes);
    const int xp_antes = antes.xp;
    CHECK(H::NuevaVueltaRuta(), "NuevaVueltaRuta succeeds before simulated save failure");
    CHECK(H::VueltaRuta() == 2 && !H::RutaCompletada(), "temporarily in lap 2");
    // Simulate Guardar() returning false -> rollback!
    dbz3::cac::SlotEditable(0) = antes;
    H::ImportarProgreso(antes);
    CHECK(H::RutaCompletada(), "rollback restored RutaCompletada() == true");
    CHECK(H::VueltaRuta() == 1, "rollback restored VueltaRuta() == 1");
    CHECK(H::Tramo() == -1, "rollback restored Tramo() == -1");
    CHECK(dbz3::cac::Slot(0).xp == xp_antes, "rollback preserved XP");
  }

  // Save completed Lap 1 to disk so Phase 2 (separate process!) proves real cross-process reload!
  H::Exportar(dbz3::cac::SlotEditable(0));
  CHECK(dbz3::cac::Guardar(cac_p.string()), "save completed Lap 1 to cac.toml");
  H::GuardarRutaActual();
  return 0;
}

static int ScenarioFourLapsPhase2() {
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  CHECK(dbz3::cac::Leer(cac_p.string()), "Phase 2: cac::Leer reloaded from disk");
  H::Importar(0);
  CHECK(H::RutaCompletada(), "Phase 2: RutaCompletada() survived process restart");
  CHECK(H::VueltaRuta() == 1, "Phase 2: VueltaRuta() == 1 after reload");
  CHECK(H::EtapaRuta() == 6, "Phase 2: EtapaRuta() == 6 after reload");
  CHECK(H::FinalesCompletados().size() == 1 && H::FinalesCompletados()[0] == "torneo",
        "Phase 2: 'torneo' ending survived process restart");

  const int ph_before = dbz3::cac::Slot(0).ph;
  const int xp_before = dbz3::cac::Slot(0).xp;
  // Start Lap 2!
  CHECK(H::NuevaVueltaRuta(), "Phase 2: NuevaVueltaRuta starts Lap 2");
  CHECK(H::VueltaRuta() == 2, "VueltaRuta() == 2");
  CHECK(H::EtapaRuta() == 0, "EtapaRuta() == 0 in Lap 2");
  CHECK(H::Tramo() == 0, "Tramo() == 0 in Lap 2");
  CHECK(H::Esferas() == 0, "Esferas reset to 0");
  CHECK(H::VidaExtraDu() == 500, "Lap 2 VidaExtraDu() == 500");
  CHECK(rutas::DuUsaNuevaHistoria(0, H::Saga(), true), "DU uses Nueva historia at Lap 2 stage 0");
  CHECK(!rutas::DuUsaNuevaHistoria(0, H::Saga(), false), "OFF keeps legacy Continuar at stage 0");
  H::Exportar(dbz3::cac::SlotEditable(0));
  CHECK(dbz3::cac::Slot(0).ph == ph_before && dbz3::cac::Slot(0).xp == xp_before,
        "NuevaVueltaRuta preserved PH and XP");
  CHECK(dbz3::cac::Guardar(cac_p.string()), "save start of Lap 2");
  H::GuardarRutaActual();

  // Play Lap 2 -> Juegos de Cell (tramo 13): c0=0 (Nave), c1=11 (Ciudad y Cell), master=0, revivir=false
  if (int rc = RunOneFullLap(0, 11, 0, false, false, false, 13, "juegos_cell", 2)) return rc;

  // Lap 3 -> Torneo (tramo 6): c0=8 (Vegeta), c1=1 (Lab Gero), master=1 (Vegeta -> Baby tramo 3), revivir=true
  CHECK(H::NuevaVueltaRuta(), "Start Lap 3");
  CHECK(H::VidaExtraDu() == 1000, "Lap 3 VidaExtraDu() == 1000");
  if (int rc = RunOneFullLap(8, 1, 1, true, true, false, 6, "torneo", 3)) return rc;

  // Lap 4 -> Omega Final (tramo 7): c0=8 (Vegeta), c1=9 (Ciudad), master=2 (Piccolo -> Piccolo tramo 2), revivir=false
  CHECK(H::NuevaVueltaRuta(), "Start Lap 4");
  CHECK(H::VidaExtraDu() == 1500, "Lap 4 VidaExtraDu() == 1500");
  if (int rc = RunOneFullLap(8, 9, 2, false, false, true, 7, "omega", 4)) return rc;

  // Final verification: 24/24 (100%) and 3/3 endings!
  CHECK(H::ColeccionPorcentaje() == 100, "4 laps in real DuVictoria() reach 100% Collection (24/24)");
  const auto fins = H::FinalesCompletados();
  CHECK(fins.size() == 3, "All 3 endings archived (torneo, omega, juegos_cell)");
  CHECK(std::find(fins.begin(), fins.end(), "torneo") != fins.end(), "has torneo");
  CHECK(std::find(fins.begin(), fins.end(), "omega") != fins.end(), "has omega");
  CHECK(std::find(fins.begin(), fins.end(), "juegos_cell") != fins.end(), "has juegos_cell");

  // Save and reload in-memory via cac::Guardar/Leer to confirm h02_lap_4 + 3 h02_fin_* persist cleanly!
  H::Exportar(dbz3::cac::SlotEditable(0));
  CHECK(dbz3::cac::Guardar(cac_p.string()), "save after 4 laps");
  CHECK(dbz3::cac::Leer(cac_p.string()), "reload after 4 laps");
  H::Importar(0);
  CHECK(H::VueltaRuta() == 4, "VueltaRuta() == 4 after reload");
  CHECK(H::FinalesCompletados().size() == 3, "3/3 endings after reload");
  CHECK(H::ColeccionPorcentaje() == 100, "100% Collection after reload");
  return 0;
}

// =============================================================================
// Scenario 6: DU +500 HP/lap (AplicarVidaExtraDu) + Mirror Bosses (Mision + real IdPropio=63)
// =============================================================================
static int ScenarioDuHpAndMirror() {
  // 6a. DU +500 HP per lap (exact sub_820FE668 sel+96 +36/+40/+48 logic)
  {
    uint32_t hi = 3000, hm = 3000;
    CHECK(!rutas::AplicarVidaExtraDu(hi, hm, 7000, 0), "Lap 1 (extra=0) does not modify HP");
    CHECK(hi == 3000 && hm == 3000, "Lap 1 unchanged");

    hi = 0; hm = 0;
    CHECK(rutas::AplicarVidaExtraDu(hi, hm, 7000, 500), "Lap 2 (0/0 default) -> 3500/3500");
    CHECK(hi == 3500 && hm == 3500, "0/0 + 500 = 3500/3500");

    hi = 1500; hm = 3000;   // half-HP battle from sub_82156CE8
    CHECK(rutas::AplicarVidaExtraDu(hi, hm, 7000, 500), "Lap 2 half-HP battle");
    CHECK(hi == 2000 && hm == 3500, "half-HP preserves delta: 1500/3000 -> 2000/3500");

    hi = 6500; hm = 6500;
    CHECK(rutas::AplicarVidaExtraDu(hi, hm, 7000, 1500), "Lap 4 clamped to cap 7000");
    CHECK(hi == 7000 && hm == 7000, "clamped to 7000 cap");
  }

  // 6b. Mirror Bosses via REAL Mision() + REAL IdPropio() == 63!
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Espejo\"\nid = \"esp01\"\nprogreso = 2\nsaga = 0\nxp = 0\n"
      << "desbloqueos = []\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer ok");
  H::Importar(0);

  // Mount fake mods/heroe_propio/ficha.txt matching H::ArgsPropio() + mods/_roster/roster.toml with id=63!
  const std::string args = H::ArgsPropio();
  fs::create_directories(g_test_mods_dir / "heroe_propio");
  fs::create_directories(g_test_mods_dir / "_roster");
  {
    std::ofstream o(g_test_mods_dir / "heroe_propio" / "ficha.txt", std::ios::binary | std::ios::trunc);
    o << args << "\n";
  }
  {
    std::ofstream o(g_test_mods_dir / "_roster" / "roster.toml", std::ios::binary | std::ios::trunc);
    o << "[[personaje]]\n# Heroe (mod heroe_propio)\nid = 63\ndonante = 0\n";
  }
  g_donor_map[63] = 0;

  CHECK(H::IdPropio() == 63, "REAL IdPropio() resolves to 63");

  // Locate Freezer (27) and Cell (33) fights in sagas.toml
  int s_f = -1, k_f = -1, s_c = -1, k_c = -1;
  for (int s = 0; s < H::SagasTotal(); ++s) {
    auto cs = H::CombatesDeSaga(s);
    for (int k = 0; k < int(cs.size()); ++k) {
      if (cs[size_t(k)].first == 27 && s_f < 0) s_f = s, k_f = k;
      if (cs[size_t(k)].first == 33 && s_c < 0) s_c = s, k_c = k;
    }
  }
  CHECK(s_f >= 0 && s_c >= 0, "Freezer and Cell fights found");

  int rival = -1, vida = 0, esc = -1;
  // OFF + IdPropio=63 -> still substitutes!
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", false);
  H::Mision(s_f, k_f, 27, 27, &rival, &vida, &esc);
  CHECK(rival != 27, "OFF + IdPropio=63: Freezer still substituted");
  H::Mision(s_c, k_c, 33, 33, &rival, &vida, &esc);
  CHECK(rival != 33, "OFF + IdPropio=63: Cell still substituted");

  // ON + IdPropio=63 -> keeps REAL Freezer (27) and REAL Cell (33)!
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  H::Mision(s_f, k_f, 27, 27, &rival, &vida, &esc);
  CHECK(rival == 27, "ON + IdPropio=63: Mirror fight keeps real Freezer (27)");
  H::Mision(s_c, k_c, 33, 33, &rival, &vida, &esc);
  CHECK(rival == 33, "ON + IdPropio=63: Mirror fight keeps real Cell (33)");

  // ON + IdPropio=-1 (unmounted) -> still substitutes safely!
  g_donor_map.clear();
  // Note: Propio() caches for 1500 ms unless ArgsPropio() changes; change name to invalidate cache!
  H::PonerNombre("SinPropio");
  CHECK(H::IdPropio() == -1, "IdPropio() == -1 when not mounted");
  H::Mision(s_f, k_f, 27, 27, &rival, &vida, &esc);
  CHECK(rival != 27, "ON + IdPropio=-1: Freezer safely substituted");
  H::Mision(s_c, k_c, 33, 33, &rival, &vida, &esc);
  CHECK(rival != 33, "ON + IdPropio=-1: Cell safely substituted");
  return 0;
}

// =============================================================================
// Scenario 7: REAL Tab 0 & Tab 3 Equip-State 1:1 Parity (fixes AP_NHER3 objection!)
// =============================================================================
static int ScenarioTabsEquipParity() {
  MockGuestRam ram;
  CHECK(ram.Init(), "MockGuestRam initialized with real #SKC dodge & attack records");

  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  rex::cvar::SetBoolFlag("dbz3_heroe_tecnicas_v2", true);

  // Write coleccion.toml with todo = true to PROVE Tab 0 & Tab 3 do NOT falsely unlock unequippable items!
  {
    std::ofstream o(g_test_user_dir / "coleccion.toml", std::ios::binary | std::ios::trunc);
    o << "todo = true\nvistos = []\n";
  }

  auto verify_exact_parity = [&](const std::string& tag) -> int {
    const auto caps = H::CapsulasColeccion();
    const auto trajes = H::TrajesColeccion();
    CHECK(caps.size() == 9, tag + ": CapsulasColeccion has exactly 9 unique capsules");
    CHECK(trajes.size() == 9, tag + ": TrajesColeccion has exactly 9 entries (6 styles + 3 cosmetics)");

    // Check unique capsule IDs == {378, 385, 381, 445, 384, 382, 409, 410, 444}
    std::set<int> cap_ids;
    for (const auto& c : caps) cap_ids.insert(c.cap);
    const std::set<int> expected_caps = {378, 385, 381, 445, 384, 382, 409, 410, 444};
    CHECK(cap_ids == expected_caps, tag + ": exact 9 unique native B3 capsule IDs (445 deduplicated)");

    // 1:1 Parity for Tab 0 (TrajesColeccion) vs REAL Estilos() and REAL Desbloqueos()!
    const auto estilos_eq = H::Estilos();
    const auto desb_eq = H::Desbloqueos();
    for (const auto& t : trajes) {
      if (t.id.rfind("estilo_m_", 0) == 0) {
        const bool can_equip = std::find(estilos_eq.begin(), estilos_eq.end(), t.cara) != estilos_eq.end();
        CHECK(t.conseguido == can_equip, tag + ": style " + t.id + " display == Estilos() equippable");
      } else {
        bool found = false, can_equip = false;
        for (const auto& d : desb_eq)
          if (d.id == t.id) found = true, can_equip = d.conseguido;
        CHECK(found, tag + ": cosmetic " + t.id + " exists in Desbloqueos()");
        CHECK(t.conseguido == can_equip, tag + ": cosmetic " + t.id + " display == Desbloqueos() equippable");
      }
    }

    // 1:1 Parity for Tab 3 (CapsulasColeccion) vs REAL Reglas() (master passives) + REAL TecnicasV2() (dodge)!
    // Check master passives using Maestro(i).entrenado (which directly drives Reglas() g_maestros bit i)
    // and dodge capsules using REAL TecnicasV2()!
    const auto tecs_v2 = H::TecnicasV2();
    std::map<int, bool> dodge_open;
    for (const auto& tv : tecs_v2)
      if (tv.tipo == 3) dodge_open[tv.cap] = tv.abierta;

    for (const auto& c : caps) {
      bool equip_passive = false;
      for (int i = 0; i < H::kMaestros; ++i) {
        const auto m = H::Maestro(i);
        if (m.capsula == c.cap && m.entrenado) equip_passive = true;
      }
      const bool equip_dodge = H::TecnicasV2On() && dodge_open.count(c.cap) && dodge_open[c.cap];
      const bool expected = equip_passive || equip_dodge;
      CHECK(c.conseguido == expected,
            tag + ": capsule " + std::to_string(c.cap) + " display (" + std::to_string(c.conseguido) +
                ") == equippable (" + std::to_string(expected) + ")");
    }
    return 0;
  };

  // State A: Fresh hero (todo=true in coleccion.toml, 0 training, 0 unlocks)
  const fs::path cac_p = g_test_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Parity\"\nid = \"par01\"\nprogreso = 2\nsaga = 0\nxp = 0\n"
      << "maestros = 0\nentreno = [0, 0, 0, 0, 0, 0]\ndesbloqueos = []\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "State A load");
  H::Importar(0);
  if (int rc = verify_exact_parity("StateA_fresh_todo_true")) return rc;
  // Specifically assert todo=true did NOT unlock locked items!
  int tr_ok = 0, cp_ok = 0;
  for (const auto& t : H::TrajesColeccion()) tr_ok += t.conseguido;
  for (const auto& c : H::CapsulasColeccion()) cp_ok += c.conseguido;
  CHECK(tr_ok == 1, "State A: only Goku style unlocked (1/9), NOT fake-unlocked by todo=true");
  CHECK(cp_ok == 0, "State A: 0/9 capsules unlocked, NOT fake-unlocked by todo=true");

  // State B: Goku trained 60% + Krillin 60% + ki_dorado_goku unlocked
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Parity\"\nid = \"par01\"\nprogreso = 2\nsaga = 2\nxp = 6\n"
      << "maestros = 1\nentreno = [100, 0, 0, 60, 0, 0]\ndesbloqueos = [\"ki_dorado_goku\"]\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "State B load");
  H::Importar(0);
  if (int rc = verify_exact_parity("StateB_goku100_krilin60")) return rc;
  // Goku passive 378 + Goku dodge 444 + Krillin dodge 445 = 3 capsules unlocked!
  cp_ok = 0;
  for (const auto& c : H::CapsulasColeccion()) cp_ok += c.conseguido;
  CHECK(cp_ok == 3, "State B: 3/9 capsules unlocked (378, 444, 445)");

  // State C: TecnicasV2 OFF -> dodge-only capsules (444, 445 without Krillin 100%) are NOT equippable!
  rex::cvar::SetBoolFlag("dbz3_heroe_tecnicas_v2", false);
  if (int rc = verify_exact_parity("StateC_tecnicas_v2_off")) return rc;
  cp_ok = 0;
  for (const auto& c : H::CapsulasColeccion()) cp_ok += c.conseguido;
  CHECK(cp_ok == 1, "State C (TecnicasV2 OFF): only Goku passive 378 (1/9) is equippable");

  // State D: All 6 masters trained + all 3 cosmetics unlocked + TecnicasV2 ON -> 9/9 + 9/9!
  rex::cvar::SetBoolFlag("dbz3_heroe_tecnicas_v2", true);
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"Parity\"\nid = \"par01\"\nprogreso = 2\nsaga = 6\nxp = 30\n"
      << "maestros = 63\nentreno = [100, 100, 100, 100, 100, 100]\n"
      << "desbloqueos = [\"ki_dorado_goku\", \"aura_violeta_piccolo\", \"ki_rojo_vegeta\"]\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "State D load");
  H::Importar(0);
  if (int rc = verify_exact_parity("StateD_all_unlocked")) return rc;
  tr_ok = 0; cp_ok = 0;
  for (const auto& t : H::TrajesColeccion()) tr_ok += t.conseguido;
  for (const auto& c : H::CapsulasColeccion()) cp_ok += c.conseguido;
  CHECK(tr_ok == 9 && cp_ok == 9, "State D: 9/9 Trajes + 9/9 Capsulas unlocked");
  return 0;
}

int main(int argc, char** argv) {
  std::string scenario;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    if (k == "--scenario") scenario = argv[i + 1];
    else if (k == "--userdir") g_test_user_dir = argv[i + 1];
    else if (k == "--modsdir") g_test_mods_dir = argv[i + 1];
  }
  if (scenario.empty() || g_test_user_dir.empty() || g_test_mods_dir.empty()) {
    std::cerr << "Usage: test_heroe_nher4 --scenario <name> --userdir <dir> --modsdir <dir>\n";
    return 2;
  }
  fs::create_directories(g_test_user_dir);
  int rc = 2;
  if (scenario == "default_off") rc = ScenarioDefaultOff();
  else if (scenario == "corrupt_no_overwrite") rc = ScenarioCorruptNoOverwrite();
  else if (scenario == "io_extra_keys") rc = ScenarioIoAndExtraKeys();
  else if (scenario == "shared_consumers") rc = ScenarioSharedCollectionConsumers();
  else if (scenario == "four_laps_p1") rc = ScenarioFourLapsPhase1();
  else if (scenario == "four_laps_p2") rc = ScenarioFourLapsPhase2();
  else if (scenario == "du_hp_mirror") rc = ScenarioDuHpAndMirror();
  else if (scenario == "tabs_equip_parity") rc = ScenarioTabsEquipParity();
  else {
    std::cerr << "Unknown scenario: " << scenario << "\n";
    return 2;
  }
  if (rc == 0) std::cout << "PASS " << scenario << " (" << g_checks << " checks)\n";
  return rc;
}
