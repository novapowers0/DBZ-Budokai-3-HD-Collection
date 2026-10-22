// dbz3 - Roster extension: new characters / costumes declared by mods (US image).
//
// Guest tables (US default.xex, RE 2026-10-03, see the Claude handback):
//   0x8234ABB8  char96[44]   96 B per character ID:
//                 +0 name rec ptr | +4 u8 costumes | +5 u8 models per costume |
//                 +6 u8 ? | +7 u8 forms | +8 model list ptr (12 B: fid, physics
//                 chains ptr, cache) | +12 LIPS list ptr (8 B: fid, cache) |
//                 +16 forms[8] (8 B: u16 id, u8 model in costume (0xFF none), u8 lips,
//                 u16 aura (3 form aura, 4 + SSJ2 sparks; sub_82136C58)) | +80 u16 n + list
//   0x82329CF0  char372[44]  battle record: +0..+0x1C ANM fid per form, +0x20 CAM,
//                 +0x24/+0x28 lang_usa fids, then stats / per-form tables
//   0x82322950  aura fid per ID       0x82333208  technique BSP fid per ID
//   0x82373D68  HUD face (u8 pair)    0x82372950  select preview pos (56 B/ID)
//   0x823280B0  battle voices: ptr per ID to s32[n] ADX indices (adx_usa/jpn.afs),
//               0x82328298 n per ID (50). IDs 44-63 have n = 0 and the cut IDs a
//               block of -1: new characters were silent.
//   0x82020618  slot -> ID (u16)      0x82020668  ID -> slot (u16)
// IDs 22-26 (Guldo, Jeice, Burter, Zarbon, Dodoria) and 31 (Android 19) exist in
// char96 (names, costume counts) but have no models and an empty char372: they
// were cut from the retail game. A roster mod fills them.
// IDs 44-63 ("plazas extra", RE 2026-10-04): char96/char372/aura/BSP/HUD have 105
// entries (64-104 = fusions and battle-only forms) and 44-63 are empty in all of
// them; the select's unlock mask is a u64. Only the select preview position table
// (45 entries; entry 44 = "no character") and ID->slot (44) are short: positions of
// IDs >= 44 live here (sub_8217FF20 hook) and ID->slot answers the host slot.
//
// Capsules (skills): catalogue #SKC (data_usi 4, 596 x 40 B) resident in the image
// and used through the pointer pair 0x82375608 (header) / 0x8237560C (records).
// New capsules (IDs >= 596) get a bigger copy of the catalogue with the pointers
// moved to it. char96 +80 = default list (u16 n + 7 u16), char372 +212 + 20*form
// (u16) = capsule each form needs, 0x82373D68 (u16 per ID) = data_usi entry with the
// names of the character's capsules (shown in battle).
//
// mods/<mod>/roster.toml:
//   [[personaje]]
//   id = 22                 # 0..63 (22-26 y 31 = recortados de fabrica, 44-63 extra)
//   donante = 21            # ID del que se copia lo que no se declare (combate, aura,
//                           # tecnicas, HUD, encuadre del select, moveset, formas)
//   modelos = [3990, 3991]  # fid data_cmn por traje (x modelos_por_traje)
//   bocas = [3992, 3992]    # LIPS por modelo (por defecto, los del donante)
//   modelos_por_traje = 1   # opcional
//   anm = [3993]            # moveset por forma        cam = 3994
//   idioma = [37, 84]       # lang_usa (+0x24, +0x28)   tecnicas = 3995   aura = 33
//   aura_formas = [0, 3, 4, 0]  # opcional: tipo de aura de cada forma (char96 formas[f] +4, 0..5);
//                           # sin la clave, la del donante. Lo lee el juego en combate (sub_82136588)
//   hud = [10, 113]         # select = [14 valores: x, y, z, escala, rx, ry, rz (P1) y P2]
//   casilla = 10            # (pruebas) reasigna esta casilla EXISTENTE al ID
//   nombre = "JANEMBA"      # nombre interno (IDs sin registro de nombre, 44-63)
//   formas = 1              # numero de formas (por defecto, las del donante)
//   formas_donante = [0, 2, 4, 5]  # o que formas de combate del donante, en que orden
//   modelo_forma = [0, 1, 2, 3]  # modelo dentro del traje de cada forma (char96 formas +2)
//   ki_base = [3, 4, 4, 5]  # barras de ki a las que tiende cada forma (char372 +212+20f +9)
//   fisica = 4              # cadenas de fisica (pelo/cinturon) de los modelos de ese ID
//   capsulas = [596, 597]   # lista de capsulas por defecto ("Original", max 7)
//   capsulas_forma = [0, 599]  # capsula que exige cada forma (0 = ninguna)
//   hereda = [140, 141]     # capsulas (del donante) que tambien puede llevar
//   habilidades_indice = 34  # ficha de habilidades del juego (la del donante)
//   habilidades = 2710      # o una propia: data_usi SCM + capsulas de ataque/transformacion
//   habilidades_ataques = [596, 597]   habilidades_transformaciones = []
//   hud_cara = 482          # data_cmn con la cara de la barra de vida (por defecto ninguna)
//   voces = [642, -1, ...]  # voces de combate: indice ADX por situacion (max 64; -1 = sin voz)
//   voces_de = 4            # o las de otro ID (por defecto, las del donante)
//   [[capsula]]             # capsula nueva del catalogo (ID >= 596)
//   id = 596
//   registro = "<40 bytes en hex>"
//   descripcion = 2709      # data_usi con el panel de descripcion (#AZT, 5 lineas)
//   [[parche]]              # parche crudo de memoria del guest (RE / expertos)
//   dir = 0x8234AF7C
//   u32 = [1, 2]            # o u16 = [...] / u8 = [...]

#include "roster_ext.h"
#include "form_selection_state.h"
#include "launcher/settings.h"
#include "tagteam.h"
#include "ingame/tagteam_panel.h"

#include "mods.h"

#include <cstdlib>
#include <rex/cvar.h>
#include <rex/filesystem/afs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <toml++/toml.hpp>

#include "generated/dbz3_init.h"
#include "guest_region.h"

REXCVAR_DECLARE(std::string, dbz3_iso_path);

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace dbz3::formas {
// transformaciones.cpp: genera mods/_forma_unica (nombres y panel de la capsula unica) si falta o cambio
bool PrepareNamesMod(const std::vector<std::pair<int, uint32_t>>& banks, const std::string& roster_stamp);
}

namespace dbz3::roster {
namespace {

constexpr uint32_t kChar96 = 0x8234ABB8;
constexpr uint32_t kChar372 = 0x82329CF0;
constexpr uint32_t kAura = 0x82322950;
constexpr uint32_t kBsp = 0x82333208;
constexpr uint32_t kHudFace = 0x82373D68;
constexpr uint32_t kVoicePtr = 0x823280B0;   // u32 por ID -> s32[n] indices ADX
constexpr uint32_t kVoiceCount = 0x82328298; // s32 n por ID
constexpr uint32_t kSelectPos = 0x82372950;
// .rdata: en la imagen EU/PAL estas cuatro se mueven (SelectImage, guest_region.h)
uint32_t kSlotToId = 0x82020618;
uint32_t kSlotToIdB = 0x82021470;  // copias usadas por otros modos/confirmacion
uint32_t kSlotToIdC = 0x82024760;
uint32_t kIdToSlot = 0x82020668;
constexpr uint32_t kPortraits = 0x82372818;  // u32 [39][2] retrato del select por slot
constexpr uint32_t kSignature = 0x82020700;  // "_charasel.cpp"
constexpr uint32_t kSignatureEu = 0x82020708;
constexpr int kIds = 64;
constexpr int kSelectPosIds = 44;           // la entrada 44 es "sin personaje"
constexpr uint32_t kSkcHdrPtr = 0x82375608;  // -> cabecera del #SKC
constexpr uint32_t kSkcRecPtr = 0x8237560C;  // -> registros (cabecera + 0x20)
constexpr uint32_t kSkcRec = 40;
constexpr uint64_t kSkcEveryone = 0xFFFFFFFFFFFull;  // objetos comunes: IDs 0..43
constexpr int64_t kDataCmnEntries = 3990;    // entradas de data_cmn.afs (US)

std::atomic<uint64_t> g_unlock{0};
std::vector<VirtualCell> g_cells;
rex::memory::Memory* g_memory = nullptr;
// donante de cada ID de la plantilla extendida (-1 = ninguno)
std::atomic<int> g_donor[kIds];
std::atomic<int> g_team_size[kIds];   // Tag Team: miembros (equipo = [...])
std::atomic<int> g_team_member[kIds][5];
// trajes anadidos por mods a personajes del juego (traje.toml)
std::atomic<int> g_extra_costumes[kIds];
// trajes reales de cada personaje anadido por roster.toml (0 = no anadido)
std::atomic<int> g_costume_count[kIds];
// encuadre del select de los IDs >= 44 (14 valores en el formato de la tabla)
std::map<uint32_t, std::array<uint32_t, 14>> g_selpos;
// casilla anfitriona de cada ID nuevo (para ID -> slot)
std::map<uint32_t, uint32_t> g_host_slot;
// capsulas: registros nuevos y bits de dueno anadidos a las existentes
std::map<uint32_t, std::array<uint8_t, 40>> g_new_caps;
// panel de descripcion de las capsulas nuevas: ID -> entrada de data_usi (#AZT de 5 lineas)
std::map<uint32_t, uint32_t> g_cap_desc;
// nombres de capsulas de los IDs >= 44 (la tabla 0x82373D68 tiene 44 entradas: justo
// detras van las posiciones de la bandeja de "Edit Skills", 0x82373DC4..0x82373DE8)
std::map<uint32_t, int32_t> g_hud_fid;
std::map<uint32_t, uint64_t> g_cap_owner_add;
std::atomic<bool> g_caps_done{false};
// Capsula de transformacion unica (ficha 14; mod generado por forma_unica.py con [forma_unica] en su
// roster.toml): registros 1984 + ID, panel de descripcion y bancos de nombres ampliados (data_usi:
// carga del banco original -> su copia con el nombre de la capsula unica).
std::atomic<bool> g_unified_on{false};
bool g_unified_all = true;              // todas = true: tambien las formas sin desbloquear
uint32_t g_unified_desc = 0;
std::vector<uint16_t> g_unified_ids;     // capsulas unicas creadas
std::map<uint32_t, uint32_t> g_bank_redirect;   // 0x10000 | fid -> 0x10000 | fid
// fichas de habilidades (lista de la pausa y rotulos de capsula en combate)
constexpr uint32_t kCostumeCounts = 0x8245F150;   // select: trajes elegibles por casilla
constexpr uint32_t kSkillTable = 0x82324468;   // 16 B: ID, trajes, HUD (data_cmn), indice
constexpr uint32_t kSkillRecs = 0x82373A00;    // punteros a registros: [indice + 1]
constexpr int kVirtualSkill = 1000;            // indices propios: 1000 + n
struct SkillEntry {
  uint32_t id;
  int32_t hud;
  int32_t index;
};
std::vector<SkillEntry> g_skill_entries;
std::vector<uint32_t> g_skill_recs;            // registros propios (guest)
// listas "Custom" propias de los personajes nuevos (la partida guarda una por casilla)
constexpr uint32_t kInventorySize = 2048;
std::mutex g_custom_mu;
std::map<uint32_t, std::array<uint16_t, 7>> g_custom;

std::filesystem::path CustomFile() { return ModsRoot() / "capsulas_custom.txt"; }

void LoadCustomLists() {
  std::lock_guard<std::mutex> lk(g_custom_mu);
  g_custom.clear();
  std::ifstream in(CustomFile());
  std::string ln;
  while (std::getline(in, ln)) {      // "44: 596 597 598 65535 65535 65535 65535"
    const auto colon = ln.find(':');
    if (ln.empty() || ln[0] == '#' || colon == std::string::npos) continue;
    std::array<uint16_t, 7> a;
    a.fill(0xFFFF);
    std::istringstream ss(ln.substr(colon + 1));
    for (auto& v : a) {
      long x = 0;
      if (!(ss >> x)) break;
      v = uint16_t(x);
    }
    try {
      g_custom[uint32_t(std::stoul(ln.substr(0, colon)))] = a;
    } catch (...) {
    }
  }
}

void SaveCustomLists() {
  std::ofstream out(CustomFile(), std::ios::trunc);
  out << "# Listas \"Custom\" de capsulas de los personajes nuevos (ID: 7 capsulas, 65535 = vacia).\n"
         "# Las escribe el juego al usar Edit Skills con un personaje nuevo; borrar una linea = volver\n"
         "# a su lista Normal.\n";
  for (const auto& [id, a] : g_custom) {
    out << id << ':';
    for (auto v : a) out << ' ' << v;
    out << '\n';
  }
}

class Guest {
 public:
  explicit Guest(rex::memory::Memory* m) : m_(m) {}
  uint8_t* P(uint32_t a) const { return m_->TranslateVirtual<uint8_t*>(a); }
  uint32_t U32(uint32_t a) const {
    const uint8_t* p = P(a);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint8_t U8(uint32_t a) const { return *P(a); }
  uint16_t U16(uint32_t a) const { return uint16_t((U8(a) << 8) | U8(a + 1)); }
  // Writes through the page protection (the per-character tables live in the
  // read-only data of the image).
  void Write(uint32_t a, const void* src, uint32_t n) {
    auto* heap = m_->LookupHeap(a);
    const uint32_t page = 0x1000;
    const uint32_t lo = a & ~(page - 1);
    const uint32_t hi = (a + n + page - 1) & ~(page - 1);
    uint32_t old = 0;
    bool changed = false;
    if (heap) {
      changed = heap->Protect(lo, hi - lo,
                              rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                              &old);
    }
    std::memcpy(P(a), src, n);
    if (heap && changed && old) {
      heap->Protect(lo, hi - lo, old);
    }
  }
  void W32(uint32_t a, uint32_t v) {
    const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    Write(a, b, 4);
  }
  void W16(uint32_t a, uint16_t v) {
    const uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v)};
    Write(a, b, 2);
  }
  void W8(uint32_t a, uint8_t v) { Write(a, &v, 1); }
  void MakeWritable(uint32_t a, uint32_t n) {
    if (auto* heap = m_->LookupHeap(a)) {
      const uint32_t lo = a & ~0xFFFu, hi = (a + n + 0xFFF) & ~0xFFFu;
      heap->Protect(lo, hi - lo, rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite);
    }
  }
  void Copy(uint32_t dst, uint32_t src, uint32_t n) {
    std::vector<uint8_t> tmp(P(src), P(src) + n);
    Write(dst, tmp.data(), n);
  }
  uint32_t Alloc(uint32_t n) {
    const uint32_t a = m_->SystemHeapAlloc(n, 0x20);
    if (a) std::memset(P(a), 0, n);
    return a;
  }

 private:
  rex::memory::Memory* m_;
};

std::vector<int64_t> Ints(const toml::node_view<const toml::node>& v) {
  std::vector<int64_t> out;
  if (auto* arr = v.as_array()) {
    for (const auto& e : *arr) {
      if (auto i = e.value<int64_t>()) out.push_back(*i);
    }
  }
  return out;
}

std::vector<double> Floats(const toml::node_view<const toml::node>& v) {
  std::vector<double> out;
  if (auto* arr = v.as_array()) {
    for (const auto& e : *arr) {
      if (auto d = e.value<double>()) out.push_back(*d);
      else if (auto i = e.value<int64_t>()) out.push_back(double(*i));
    }
  }
  return out;
}

int64_t Int(const toml::node_view<const toml::node>& v, int64_t def) {
  if (auto i = v.value<int64_t>()) return *i;
  if (auto s = v.value<std::string>()) {
    try {
      return std::stoll(*s, nullptr, 0);
    } catch (...) {
    }
  }
  return def;
}

uint32_t FloatBits(double d) {
  float f = float(d);
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

bool ValidId(int64_t id) { return id >= 0 && id < kIds; }


// Traje extra de un personaje del juego (traje.toml): un modelo por forma, en el orden
// de las formas del registro de combate (char372 +212 + 20*f: +2 u16 ID cuyo modelo se
// usa, +4 u16 modelo dentro del traje). Las formas de otro ID (p.ej. Goku SSJ4 = ID 91)
// reciben el modelo en ESE ID, en el mismo numero de traje (rellenando con su traje 1).
void AppendCostume(Guest& g, uint32_t id, uint32_t costume, uint32_t model_idx, uint32_t fid) {
  const uint32_t c96 = kChar96 + id * 96;
  const uint32_t ncos = g.U8(c96 + 4), per = std::max<uint32_t>(1, g.U8(c96 + 5));
  const uint32_t lpc = std::max<uint32_t>(1, g.U8(c96 + 6));
  const uint32_t old_list = g.U32(c96 + 8), old_lips = g.U32(c96 + 12);
  if (!ncos || !old_list) return;
  const uint32_t want = std::max(ncos, costume + 1);
  // R7 slots: la lista nueva conserva el relleno de ApplyCharacter (kPadCostumes = 8 trajes, repitiendo el 1.o):
  // una casilla nueva lee el traje guardado de su anfitrion, que puede pasar de los reales (fallo de la 1.4.1).
  const uint32_t cap = std::max<uint32_t>(want, 8);
  uint32_t list = old_list, lips = old_lips;
  if (want > ncos) {
    list = g.Alloc(cap * per * 12);
    lips = g.Alloc(cap * lpc * 8);
    if (!list || !lips) return;
    g.Copy(list, old_list, ncos * per * 12);
    if (old_lips) g.Copy(lips, old_lips, ncos * lpc * 8);
    for (uint32_t c = ncos; c < cap; ++c) {
      for (uint32_t k = 0; k < per; ++k) {   // traje 1 del personaje, sin cache
        g.Copy(list + 12 * (c * per + k), old_list + 12 * k, 8);
      }
      for (uint32_t k = 0; k < lpc && old_lips; ++k) g.W32(lips + 8 * (c * lpc + k), g.U32(old_lips + 8 * k));
    }
    g.W32(c96 + 8, list);
    g.W32(c96 + 12, lips);
    g.W8(c96 + 4, uint8_t(want));
  }
  const uint32_t e = list + 12 * (costume * per + std::min(model_idx, per - 1));
  g.W32(e, fid);
  g.W32(e + 4, 0);   // cadenas de fisica del modelo original: no valen para otro modelo
  g.W32(e + 8, 0);
}

void ApplyExtraCostume(Guest& g, const toml::table& t, const std::string& mod, uint32_t id) {
  const auto fids = Ints(t["traje_formas"]);
  const uint32_t c96 = kChar96 + id * 96;
  const uint32_t c372 = kChar372 + id * 372;
  const uint32_t costume = g.U8(c96 + 4);
  if (fids.empty() || !costume) return;
  uint32_t nforms = g.U32(c372 + 0xD0);
  if (nforms == 0 || nforms > 8) nforms = std::max<uint32_t>(1, g.U8(c96 + 7));
  std::vector<uint32_t> done;   // (id << 8 | modelo) ya escritos en este traje
  for (uint32_t f = 0; f < nforms; ++f) {
    const uint32_t e = c372 + 212 + 20 * f;
    uint32_t fid_id = (g.U8(e + 2) << 8) | g.U8(e + 3);
    // +4 es el indice en char96.formas[] (no el modelo): el modelo dentro del traje es el
    // byte +2 de esa entrada (Gohan adulto: formas [0,1,2,3] -> modelos [0,1,1,2]; el SSJ2
    // usa el modelo del SSJ y solo cambia el aura).
    const uint32_t fi = std::min<uint32_t>((g.U8(e + 4) << 8) | g.U8(e + 5), 7);
    if (fid_id >= 105 || !g.U32(kChar96 + fid_id * 96 + 8)) fid_id = id;
    const uint32_t mi = g.U8(kChar96 + fid_id * 96 + 16 + 8 * fi + 2);
    const uint32_t fid = uint32_t(fids[std::min<size_t>(f, fids.size() - 1)]);
    // Forma con el modelo de OTRO personaje (Super Bu: 4-7 son Piccolo, Gotenks, Gohan y Bu fantasma): si ese ya
    // tiene un traje con este numero, es suyo y no se pisa (antes se sobrescribia con el modelo de este traje y
    // dependia del orden de las carpetas); si no lo tiene, se le anade como siempre.
    if (fid_id != id && costume < g.U8(kChar96 + fid_id * 96 + 4)) {
      REXLOG_INFO("dbz3 roster [{}]: traje {} de id {}: forma {} usa el modelo de id {}, que ya tiene su traje {} "
                  "(no se toca)", mod, costume + 1, id, f, fid_id, costume + 1);
      continue;
    }
    if (mi == 0xFF || std::find(done.begin(), done.end(), (fid_id << 8) | mi) != done.end()) {
      REXLOG_INFO("dbz3 roster [{}]: traje {} de id {}: forma {} comparte el modelo {} (se ignora {})",
                  mod, costume + 1, id, f, mi, fid);
      continue;
    }
    done.push_back((fid_id << 8) | mi);
    AppendCostume(g, fid_id, costume, mi, fid);
    REXLOG_INFO("dbz3 roster [{}]: traje {} de id {}: forma {} -> id {} modelo {} = {}", mod,
                costume + 1, id, f, fid_id, mi, fid);
  }
  g_extra_costumes[id].fetch_add(1);
  // aplicar_estudios (entrega 15): AddExtraCostumes solo cuenta id < 44 (tabla nativa por casilla); la rueda de una
  // casilla nueva (44-63) usa CostumeCountOf, que fijaba ApplyCharacter sin este traje: se le suma aqui.
  if (id >= 44 && id < 64 && g_costume_count[id].load() > 0) {
    g_costume_count[id].store(int(g.U8(c96 + 4)));
    REXLOG_INFO("dbz3 roster [{}]: casilla nueva id {}: la rueda cuenta {} trajes", mod, id, g.U8(c96 + 4));
  }
}

void ApplyCharacter(Guest& g, const toml::table& t, const std::string& mod) {
  const int64_t id = Int(t["id"], -1);
  const int64_t donor = Int(t["donante"], -1);
  if (!ValidId(id)) {
    REXLOG_WARN("dbz3 roster [{}]: id invalido ({})", mod, id);
    return;
  }
  if (donor >= 0 && (!ValidId(donor) || donor == id || g.U32(kChar372 + uint32_t(donor) * 372) == 0)) {
    REXLOG_WARN("dbz3 roster [{}]: donante {} no valido para id {}", mod, donor, id);
    return;
  }
  if (id >= kSelectPosIds && donor < 0 && Floats(t["select"]).size() != 14 &&
      !t["traje_formas"].as_array()) {
    REXLOG_WARN("dbz3 roster [{}]: id {} sin donante ni encuadre: omitido", mod, id);
    return;
  }
  // Entradas de data_cmn ANADIDAS (indice >= 3990): solo las sirve el runtime con el
  // parche AfsVirtualSize (publica la cvar dbz3_afs_append). Con otro runtime el
  // guest pediria entradas inexistentes: mejor no aplicar el personaje.
  if (!rex::cvar::Query<bool>("dbz3_afs_append")) {
    for (const char* key : {"modelos", "bocas", "retrato", "anm", "hud_cara", "traje_formas", "cam"}) {
      auto vals = Ints(t[key]);
      if (auto one = t[key].value<int64_t>()) vals.push_back(*one);
      for (auto v : vals) {
        if (v >= kDataCmnEntries) {
          REXLOG_WARN("dbz3 roster [{}]: id {} usa entradas nuevas de data_cmn ({}) y este "
                      "rexruntime no las admite (falta dbz3_afs_append): personaje omitido",
                      mod, id, v);
          return;
        }
      }
    }
  }
  if (t["traje_formas"].as_array()) {
    ApplyExtraCostume(g, t, mod, uint32_t(id));
    return;
  }
  const uint32_t c96 = kChar96 + uint32_t(id) * 96;
  const uint32_t c372 = kChar372 + uint32_t(id) * 372;
  const auto models = Ints(t["modelos"]);
  auto lips = Ints(t["bocas"]);
  const auto anm = Ints(t["anm"]);
  const auto lang = Ints(t["idioma"]);

  // 1) registro de combate: copia del donante + moveset/camara/idioma propios
  if (donor >= 0) {
    g.Copy(c372, kChar372 + uint32_t(donor) * 372, 372);
    // +212 + 20*forma: entradas por forma; +214 = ID cuyo modelo se carga en combate
    for (uint32_t f = 0; f < 8; ++f) {
      const uint32_t e = c372 + 214 + 20 * f;
      if (((g.U8(e) << 8) | g.U8(e + 1)) == uint32_t(donor)) g.W16(e, uint16_t(id));
    }
  }
  if (!anm.empty()) {
    for (uint32_t f = 0; f < 8; ++f) {
      g.W32(c372 + 4 * f, f < anm.size() ? uint32_t(anm[f]) : 0xFFFFFFFFu);
    }
  }
  if (auto cam = Int(t["cam"], -1); cam >= 0) g.W32(c372 + 0x20, uint32_t(cam));
  if (lang.size() >= 1) g.W32(c372 + 0x24, uint32_t(lang[0]));
  if (lang.size() >= 2) g.W32(c372 + 0x28, uint32_t(lang[1]));

  // 2) tablas por ID (donante salvo que se declare)
  // Un -1 del donante significa "sin recurso" (IDs recortados: Guldo, Zarbon...), no un fid:
  // no se copia (antes se copiaba 0xFFFFFFFF y podia pisar el valor de otro mod del mismo ID).
  auto per_id32 = [&](uint32_t table, const char* key) {
    int64_t v = Int(t[key], -1);
    if (v < 0 && donor >= 0) {
      const uint32_t dv = g.U32(table + uint32_t(donor) * 4);
      if (dv == 0xFFFFFFFFu) {
        REXLOG_WARN("dbz3 roster [{}]: id {}: {} del donante ({}) vacio: sin recurso", mod, id, key, donor);
      } else {
        v = int64_t(dv);
      }
    }
    if (v >= 0) g.W32(table + uint32_t(id) * 4, uint32_t(v));
  };
  per_id32(kAura, "aura");
  per_id32(kBsp, "tecnicas");
  // Tipo de aura por (ID, forma): 8 filas de 5 B en 0x82321A38 + 40*ID (medido 2026-10-18: Goku 02/03/04/05/06/07,
  // Gohan adulto 02/04/05/06; el juego solo dibuja aura con tipo 2..7, pista del mod Goku UI, studies 4/01). Los IDs
  // nuevos (44-63, 31) la tienen a cero: sin aura aunque el fid sea bueno. Se copia la del donante si la del ID esta vacia.
  // `aura_tipos = ID` (roster_build, de `aura_de = ID`): los tipos de ese personaje en vez de los del donante.
  if (const int64_t de = Int(t["aura_tipos"], donor); de >= 0 && de < 64) {
    constexpr uint32_t kAuraTipo = 0x82321A38;
    const uint32_t dst = kAuraTipo + 40 * uint32_t(id), src = kAuraTipo + 40 * uint32_t(de);
    bool vacia = true;
    for (uint32_t k = 0; k < 40 && vacia; ++k) vacia = g.U8(dst + k) == 0 || (k % 5 == 4 && g.U8(dst + k) == 0xFF);
    if (vacia && g.U8(src) >= 2) {
      for (uint32_t k = 0; k < 40; ++k) g.W8(dst + k, g.U8(src + k));
      REXLOG_INFO("dbz3 roster [{}]: id {}: tipos de aura por forma del ID {} ({:02X} {:02X} {:02X} {:02X})", mod, id,
                  de, g.U8(src), g.U8(src + 5), g.U8(src + 10), g.U8(src + 15));
    }
  }
  // aura de cada transformacion elegida en el Kit (aura_tipos_forma): fila de la forma k de Goku, -1 = sin aura,
  // -3 = nombre desconocido (no se toca). Va despues de la copia del donante: manda sobre ella.
  {
    constexpr uint32_t kAuraTipo = 0x82321A38;
    const auto tf = Ints(t["aura_tipos_forma"]);
    for (uint32_t f = 0; f < tf.size() && f < 8; ++f) {
      const uint32_t dst = kAuraTipo + 40 * uint32_t(id) + 5 * f;
      if (tf[f] == -1) {
        for (uint32_t k = 0; k < 5; ++k) g.W8(dst + k, 0);
      } else if (tf[f] >= 0 && tf[f] < 6) {
        for (uint32_t k = 0; k < 5; ++k) g.W8(dst + k, g.U8(kAuraTipo + 5 * uint32_t(tf[f]) + k));
      }
    }
    if (!tf.empty()) REXLOG_INFO("dbz3 roster [{}]: id {}: aura de cada forma elegida ({} formas)", mod, id, tf.size());
  }
  // Diagnostico (v1.5.x): fid de aura y de tecnicas con el que queda el ID (RUNBOOK 11/12).
  REXLOG_INFO("dbz3 roster [{}]: id {}: aura fid {} tecnicas fid {}", mod, id,
              g.U32(kAura + uint32_t(id) * 4), g.U32(kBsp + uint32_t(id) * 4));
  // voces de combate: propias, las de otro ID o las del donante (sin esto, mudo)
  {
    const auto own = Ints(t["voces"]);
    const int64_t from = Int(t["voces_de"], own.empty() ? donor : -1);
    if (!own.empty()) {
      const uint32_t n = uint32_t(std::min<size_t>(own.size(), 64));
      if (const uint32_t blk = g.Alloc(4 * n)) {
        for (uint32_t k = 0; k < n; ++k) g.W32(blk + 4 * k, uint32_t(int32_t(own[k])));
        g.W32(kVoicePtr + uint32_t(id) * 4, blk);
        g.W32(kVoiceCount + uint32_t(id) * 4, n);
      }
    } else if (from >= 0 && from < 105 && g.U32(kVoicePtr + uint32_t(from) * 4)) {
      g.W32(kVoicePtr + uint32_t(id) * 4, g.U32(kVoicePtr + uint32_t(from) * 4));
      g.W32(kVoiceCount + uint32_t(id) * 4, g.U32(kVoiceCount + uint32_t(from) * 4));
    }
  }
  const auto hud = Ints(t["hud"]);
  if (id >= 44) {
    if (hud.size() == 2) {
      g_hud_fid[uint32_t(id)] = int32_t((hud[0] & 0xFF) << 8 | (hud[1] & 0xFF));
    } else if (donor >= 0 && donor < 44) {
      g_hud_fid[uint32_t(id)] = int16_t((g.U8(kHudFace + uint32_t(donor) * 2) << 8) | g.U8(kHudFace + uint32_t(donor) * 2 + 1));
    }
  } else if (hud.size() == 2) {
    g.W8(kHudFace + uint32_t(id) * 2, uint8_t(hud[0]));
    g.W8(kHudFace + uint32_t(id) * 2 + 1, uint8_t(hud[1]));
  } else if (donor >= 0) {
    g.Copy(kHudFace + uint32_t(id) * 2, kHudFace + uint32_t(donor) * 2, 2);
  }
  const auto sel = Floats(t["select"]);
  std::array<uint32_t, 14> pos{};
  bool have_pos = false;
  if (sel.size() == 14) {
    for (uint32_t k = 0; k < 14; ++k) {
      const bool rot = (k % 7) >= 4;
      pos[k] = rot ? uint32_t(int64_t(sel[k])) : FloatBits(sel[k]);
    }
    have_pos = true;
  } else if (donor >= 0) {
    for (uint32_t k = 0; k < 14; ++k) pos[k] = g.U32(kSelectPos + uint32_t(donor) * 56 + 4 * k);
    have_pos = true;
  }
  if (have_pos) {
    if (id < kSelectPosIds) {
      for (uint32_t k = 0; k < 14; ++k) g.W32(kSelectPos + uint32_t(id) * 56 + 4 * k, pos[k]);
    } else {
      g_selpos[uint32_t(id)] = pos;   // fuera de la tabla: lo pone el hook de sub_8217FF20
    }
  }
  // nombre interno: los IDs 44-63 no tienen registro (char96 +0 = 0)
  if (!g.U32(c96)) {
    if (const uint32_t rec = g.Alloc(32)) {
      std::string nm = t["nombre"].value_or(std::string("NEW ") + std::to_string(id));
      for (auto& ch : nm) ch = char(std::toupper(uint8_t(ch)));
      const uint32_t drec = donor >= 0 ? g.U32(kChar96 + uint32_t(donor) * 96) : 0;
      if (drec) g.Copy(rec, drec, 32);
      std::memset(g.P(rec), 0, 29);
      std::memcpy(g.P(rec), nm.data(), std::min<size_t>(nm.size(), 28));
      g.W8(rec + 29, uint8_t(id));
      g.W32(c96, rec);
    }
  }

  // 3) modelos / bocas / formas (registro de 96 B)
  if (!models.empty()) {
    const uint32_t per = uint32_t(std::max<int64_t>(1, Int(t["modelos_por_traje"],
                                                            g.U8(c96 + 5) ? g.U8(c96 + 5) : 1)));
    const uint32_t n = uint32_t(models.size());
    if (lips.empty() && donor >= 0) {
      const uint32_t dl = g.U32(kChar96 + uint32_t(donor) * 96 + 12);
      lips.assign(std::max<uint32_t>(1, n / per), int64_t(g.U32(dl)));
    }
    // Listas nuevas siempre (las de la imagen estan pegadas unas a otras: la de
    // bocas tiene trajes x bocas-por-traje entradas, no una por modelo, y escribir
    // en sitio pisaba la lista del ID siguiente).
    const uint32_t nl = std::max<uint32_t>(1, uint32_t(lips.size()));
    // v1.4.1: las listas se rellenan hasta kPadCostumes trajes repitiendo el primero. Una
    // casilla nueva usa los datos guardados de su anfitrion (ultimo traje elegido) y el
    // numero de trajes del anfitrion al cambiar de traje: Janemba (1 traje, anfitrion
    // Krillin) pedia el traje 2/3 y el juego leia fuera de la lista -> puntero nulo y
    // cierre en el select (sub_8208DDF0 desde 0x82134A98, log de un usuario 2026-10-05).
    constexpr uint32_t kPadCostumes = 8;
    const uint32_t costumes = std::max<uint32_t>(1, n / per);
    const uint32_t padded_models = std::max(n, kPadCostumes * per);
    const uint32_t padded_lips = std::max(nl, kPadCostumes);
    const uint32_t list = g.Alloc(padded_models * 12);
    const uint32_t lips_list = g.Alloc(padded_lips * 8);
    if (!list || !lips_list) {
      REXLOG_ERROR("dbz3 roster [{}]: sin memoria para {} modelos", mod, n);
      return;
    }
    // fisica = ID: cadenas de fisica (pelo, colas del cinturon) de los modelos de ese
    // personaje, mismo traje/modelo (o el ultimo que tenga). Se buscan por nombre de hueso
    // raiz (sufijo: "LOBI1" vale para GHF_LOBI1) y el juego salta las que no encuentra.
    const int64_t phys_id = Int(t["fisica"], -1);
    const uint32_t p96 = kChar96 + uint32_t(std::max<int64_t>(0, phys_id)) * 96;
    const uint32_t plist = (phys_id >= 0 && phys_id < 105 && phys_id != id) ? g.U32(p96 + 8) : 0;
    const uint32_t pcos = plist ? g.U8(p96 + 4) : 0, pper = std::max<uint32_t>(1, g.U8(p96 + 5));
    g.W32(c96 + 8, list);
    g.W32(c96 + 12, lips_list);
    for (uint32_t k = 0; k < padded_models; ++k) {
      // trajes inexistentes = el traje 0 (misma forma)
      const uint32_t src = k < n ? k : (k % per) % n;
      g.W32(list + 12 * k, uint32_t(models[src]));  // +4 cadenas de fisica, +8 cache: 0
      if (pcos) {
        const uint32_t pc = std::min(k < n ? k / per : 0u, pcos - 1);   // relleno = traje 0
        const uint32_t pm = std::min(k % per, pper - 1);
        g.W32(list + 12 * k + 4, g.U32(plist + 12 * (pc * pper + pm) + 4));
      }
    }
    for (uint32_t k = 0; k < padded_lips; ++k) {
      const uint32_t src = k < nl ? k : 0;
      g.W32(lips_list + 8 * k, src < lips.size() ? uint32_t(lips[src]) : 0xFFFFFFFFu);
    }
    g_costume_count[uint32_t(id) & 63].store(int(costumes));
    g.W8(c96 + 4, uint8_t(std::max<uint32_t>(1, n / per)));
    g.W8(c96 + 5, uint8_t(per));
    if (donor >= 0) {
      // formas y lista +80 del donante (el moveset es suyo), con el ID propio
      const uint32_t d96 = kChar96 + uint32_t(donor) * 96;
      g.W8(c96 + 6, g.U8(d96 + 6));
      g.W8(c96 + 7, g.U8(d96 + 7));
      g.Copy(c96 + 16, d96 + 16, 80);
      for (uint32_t f = 0; f < 8; ++f) {
        if (g.U32(c96 + 16 + 8 * f) || g.U32(c96 + 20 + 8 * f)) {
          g.W16(c96 + 16 + 8 * f, uint16_t(id));
        }
      }
      // formas_donante = [0, 2, 4, 5]: formas de combate del donante que se quedan, en ese
      // orden (Goku GT de Infinite World: normal, SSJ, SSJ3, SSJ4; sin Kaioken ni SSJ2).
      // Cada una pasa a ser del ID propio (la del SSJ4 de Goku era del ID 91: cargaba su
      // modelo) con su entrada de char96.formas (aura...) en el mismo indice.
      // formas_de = [ID, forma, ID, forma...]: cada forma del registro de combate de OTRO
      // personaje (Tag Team: forma k = miembro k en su forma normal). Igual que formas_donante.
      std::vector<int64_t> fsrc;
      const auto fde = Ints(t["formas_de"]);
      const auto fdon = Ints(t["formas_donante"]);
      if (fde.size() >= 4) {
        fsrc = fde;
      } else {
        for (auto f : fdon) { fsrc.push_back(donor); fsrc.push_back(f); }
      }
      if (!fsrc.empty()) {
        const uint32_t nf = uint32_t(std::min<size_t>(fsrc.size() / 2, 8));
        std::array<std::array<uint8_t, 20>, 8> recs{};
        std::array<std::array<uint8_t, 8>, 8> fms{};
        for (uint32_t f = 0; f < nf; ++f) {
          const int64_t src_id = fsrc[2 * f] >= 0 && fsrc[2 * f] < 105 ? fsrc[2 * f] : donor;
          const uint32_t d372 = kChar372 + uint32_t(src_id) * 372;
          const uint32_t dn = std::clamp<uint32_t>(g.U32(d372 + 0xD0), 1, 8);
          const uint32_t s = uint32_t(std::clamp<int64_t>(fsrc[2 * f + 1], 0, dn - 1));
          for (uint32_t b = 0; b < 20; ++b) recs[f][b] = g.U8(d372 + 212 + 20 * s + b);
          uint32_t sid = (recs[f][2] << 8) | recs[f][3];
          const uint32_t si = std::min<uint32_t>((recs[f][4] << 8) | recs[f][5], 7);
          if (sid >= 105 || !g.U32(kChar96 + sid * 96 + 8)) sid = uint32_t(src_id);
          for (uint32_t b = 0; b < 8; ++b) fms[f][b] = g.U8(kChar96 + sid * 96 + 16 + 8 * si + b);
        }
        for (uint32_t f = 0; f < nf; ++f) {
          recs[f][2] = uint8_t(id >> 8), recs[f][3] = uint8_t(id);
          recs[f][4] = 0, recs[f][5] = uint8_t(f);
          fms[f][0] = uint8_t(id >> 8), fms[f][1] = uint8_t(id);
          for (uint32_t b = 0; b < 20; ++b) g.W8(c372 + 212 + 20 * f + b, recs[f][b]);
          for (uint32_t b = 0; b < 8; ++b) g.W8(c96 + 16 + 8 * f + b, fms[f][b]);
        }
        g.W32(c372 + 0xD0, nf);
        g.W8(c96 + 7, uint8_t(nf));
        REXLOG_INFO("dbz3 roster [{}]: id {}: {} formas ({})", mod, id, nf,
                    fde.size() >= 4 ? "de varios personajes: equipo" : "del donante");
      }
    }
    // modelo de cada forma dentro del traje (char96 formas[f] +2; el aura va en +4/+5 y
    // sigue siendo la del donante: el SSJ2 conserva los rayos con su propio modelo).
    // modelo_forma = [0, 1, 2, 3]; sin la clave, el reparto del donante limitado a los
    // modelos del traje (con menos modelos que el donante leia los del traje siguiente).
    const auto mf = Ints(t["modelo_forma"]);
    for (uint32_t f = 0; f < 8; ++f) {
      const uint32_t e = c96 + 16 + 8 * f + 2;
      const uint32_t m = f < mf.size() ? uint32_t(std::max<int64_t>(0, mf[f])) : g.U8(e);
      if (m != 0xFF) g.W8(e, uint8_t(std::min(m, per - 1)));
    }
    // aura de cada forma: char96 formas[f] +4/+5 (s16 que lee sub_82136588 en combate y
    // sub_82136C58 en la vista previa: 0 = sin aura de forma, 3 = aura de transformacion,
    // 4 = la de SSJ2 con rayos). Sin la clave queda la del donante (copiada arriba).
    // Encargo 11: [H] pendiente de medir en el juego (RUNBOOK, tests 11-1/11-2).
    const auto af = Ints(t["aura_formas"]);
    for (uint32_t f = 0; f < af.size() && f < 8; ++f) {
      const int64_t v = std::clamp<int64_t>(af[f], 0, 5);
      g.W16(c96 + 16 + 8 * f + 4, uint16_t(v));
      REXLOG_INFO("dbz3 roster [{}]: id {}: forma {} aura tipo {}", mod, id, f, v);
    }
  }

  // 3a) formas: menos que el donante (p.ej. un port sin transformaciones)
  if (auto nf = Int(t["formas"], -1); nf >= 1 && nf <= 8) {
    g.W8(c96 + 7, uint8_t(nf));
    if (g.U32(c372 + 0xD0) > uint32_t(nf)) g.W32(c372 + 0xD0, uint32_t(nf));
  }
  // 3b) capsulas: lista por defecto, la de cada forma y las heredadas del donante
  if (auto* arr = t["capsulas"].as_array()) {
    const auto caps = Ints(t["capsulas"]);
    const uint32_t n = uint32_t(std::min<size_t>(caps.size(), 7));
    g.W16(c96 + 80, uint16_t(n));
    for (uint32_t k = 0; k < 7; ++k) g.W16(c96 + 82 + 2 * k, k < n ? uint16_t(caps[k]) : 0);
    (void)arr;
  }
  {
    const auto fc = Ints(t["capsulas_forma"]);
    for (uint32_t f = 0; f < fc.size() && f < 8; ++f) g.W16(c372 + 212 + 20 * f, uint16_t(fc[f]));
    // nivel de ki (barras) al que tiende cada forma: +9 de su entrada (sub_82100760)
    const auto kb = Ints(t["ki_base"]);
    for (uint32_t f = 0; f < kb.size() && f < 8; ++f) {
      g.W8(c372 + 212 + 20 * f + 9, uint8_t(std::clamp<int64_t>(kb[f], 0, 7)));
    }
  }
  for (auto cap : Ints(t["hereda"])) {
    if (cap > 0 && cap < 0x10000) g_cap_owner_add[uint32_t(cap)] |= uint64_t(1) << id;
  }
  // ficha de habilidades: la del donante (indice) o una propia (data_usi + capsulas)
  {
    SkillEntry se{uint32_t(id), int32_t(Int(t["hud_cara"], -1)), -1};
    if (auto fid = Int(t["habilidades"], -1); fid >= 0) {
      const auto at = Ints(t["habilidades_ataques"]);
      const auto tr = Ints(t["habilidades_transformaciones"]);
      const uint32_t rec = g.Alloc(16 + 2 * uint32_t(at.size() + tr.size()) + 4);
      if (rec) {
        const uint32_t pa = rec + 16, pt = pa + 2 * uint32_t(at.size());
        for (size_t k = 0; k < at.size(); ++k) g.W16(pa + 2 * uint32_t(k), uint16_t(at[k]));
        for (size_t k = 0; k < tr.size(); ++k) g.W16(pt + 2 * uint32_t(k), uint16_t(tr[k]));
        g.W32(rec, uint32_t(fid));
        g.W32(rec + 4, at.empty() ? 0 : pa);
        g.W32(rec + 8, tr.empty() ? 0 : pt);
        g.W8(rec + 12, uint8_t(at.size()));
        g.W8(rec + 13, uint8_t(tr.size()));
        se.index = kVirtualSkill + int32_t(g_skill_recs.size());
        g_skill_recs.push_back(rec);
      }
    } else {
      se.index = int32_t(Int(t["habilidades_indice"], -1));
    }
    if (se.index >= 0) g_skill_entries.push_back(se);
  }

  // 4) pruebas: reasignar una casilla existente
  if (auto slot = Int(t["casilla"], -1); slot >= 0 && slot < 38) {
    const uint32_t old_id = (g.U8(kSlotToId + uint32_t(slot) * 2) << 8) |
                            g.U8(kSlotToId + uint32_t(slot) * 2 + 1);
    for (uint32_t list : {kSlotToId, kSlotToIdB, kSlotToIdC}) {
      g.W16(list + uint32_t(slot) * 2, uint16_t(id));
    }
    // ValidId admite 64 IDs host; la tabla retail solo tiene 44.
    if (old_id < 44) g.W16(kIdToSlot + old_id * 2, 0xFFFF);
    if (id < 44) {
      g.W16(kIdToSlot + uint32_t(id) * 2, uint16_t(slot));
    } else {
      g_host_slot[uint32_t(id)] = uint32_t(slot);
    }
  }
  // 5) casilla NUEVA en la rueda del select (no sustituye a nadie)
  if (auto c = t["celda"].value<bool>(); c && *c) {
    if (int(g_cells.size()) >= kMaxVirtualCells) {
      REXLOG_WARN("dbz3 roster [{}]: maximo de {} casillas nuevas", mod, kMaxVirtualCells);
    } else {
      VirtualCell vc;
      vc.id = uint32_t(id);
      int64_t alias = Int(t["anfitrion"], -1);
      if (alias < 0 && donor >= 0) {
        alias = (g.U8(kIdToSlot + uint32_t(donor) * 2) << 8) | g.U8(kIdToSlot + uint32_t(donor) * 2 + 1);
      }
      vc.alias = (alias >= 0 && alias < 38) ? uint32_t(alias) : 0;
      const int64_t after = Int(t["tras_casilla"], -1);
      vc.after = (after >= 0 && after < 38) ? uint32_t(after) : vc.alias;
      g_host_slot[uint32_t(id)] = vc.alias;
      const auto icon = Ints(t["icono"]);
      if (icon.size() == 5) for (int k = 0; k < 5; ++k) vc.icon[k] = int32_t(icon[k]);
      const auto name = Ints(t["rotulo"]);
      if (name.size() == 5) for (int k = 0; k < 5; ++k) vc.name[k] = int32_t(name[k]);
      const auto por = Ints(t["retrato"]);
      for (size_t k = 0; k < 2 && k < por.size(); ++k) vc.portrait[k] = uint32_t(por[k]);
      if (por.size() == 1) vc.portrait[1] = vc.portrait[0];
      g_cells.push_back(vc);
    }
  }
  g_unlock.fetch_or(uint64_t(1) << id);
  g_donor[id].store(int(donor));
  const auto team = Ints(t["equipo"]);
  g_team_size[id].store(int(team.size()));
  for (size_t k = 0; k < 5; ++k) g_team_member[id][k].store(k < team.size() ? int(team[k]) : -1);
  REXLOG_INFO("dbz3 roster [{}]: personaje id={} donante={} modelos={} anm={}", mod, id, donor,
              models.size(), anm.size());
}

void ReadCapsule(const toml::table& t, const std::string& mod) {
  const int64_t id = Int(t["id"], -1);
  const std::string hex = t["registro"].value_or(std::string());
  if (id < 596 || id > 4095 || hex.size() != 80) {
    REXLOG_WARN("dbz3 roster [{}]: capsula invalida (id {})", mod, id);
    return;
  }
  std::array<uint8_t, 40> rec{};
  for (size_t k = 0; k < 40; ++k) rec[k] = uint8_t(std::stoul(hex.substr(2 * k, 2), nullptr, 16));
  g_new_caps[uint32_t(id)] = rec;
  if (const int64_t d = Int(t["descripcion"], -1); d > 0 && d < 0x10000) g_cap_desc[uint32_t(id)] = uint32_t(d);
}

// Formas 1..k seguidas que agrupa la capsula unica de `id` (owned: solo hasta la primera sin desbloquear;
// las capsulas de mods, IDs >= 596, son siempre tuyas). Mismo corte que la subida nativa (sub_82119108).
int GroupableChain(Guest& g, uint32_t recs, uint32_t count, int id, bool owned) {
  // PRUEBA (DBZ3_TEST_INV="1,2"): esas capsulas cuentan como desbloqueadas (sin tocar la partida)
  static const std::string test_inv = [] { const char* e = std::getenv("DBZ3_TEST_INV"); return e ? "," + std::string(e) + "," : ""; }();
  if (id < 0 || id >= kIds || !recs) return 0;
  const uint32_t c372 = kChar372 + 372u * uint32_t(id);
  const uint32_t n = std::min<uint32_t>(g.U32(c372 + 0xD0), forms::kForms);
  const uint32_t inv = dbz3::GuestAddr(0x824BA110, 0x824BA100) + 0x2AFE9;   // inventario de la partida
  int k = 0;
  for (uint32_t f = 1; f < n; ++f) {
    const uint32_t cap = g.U16(c372 + 212 + 20 * f);
    if (cap == 0 || cap >= count) break;
    if (!forms::Groupable(g.U8(c372 + 218 + 20 * f), int(cap), g.U8(recs + kSkcRec * cap + 8))) break;
    if (owned && !g_unified_all && cap < 596 && g.U8(inv + cap) == 0 &&
        test_inv.find("," + std::to_string(cap) + ",") == std::string::npos) break;
    ++k;
  }
  return k;
}

std::string RosterStamp() {
  std::string stamp;
  std::ifstream file(ModsRoot() / "_roster" / "build.hash", std::ios::binary);
  for (char c; stamp.size() < 256 && file.get(c);) {
    if (c != '\r' && c != '\n') stamp += c;
  }
  return stamp;
}

// [forma_unica] descripcion = fid, bancos = [de, a, ...] (nativos), bancos_roster = [de, a, ...] y
// roster_hash: los bancos del _roster (y 2663/2684) solo valen con el mismo _roster con que se hicieron.
void ReadUnified(const toml::table& t, const std::string& mod) {
  g_unified_on.store(true);
  g_unified_all = t["todas"].value_or(true);
  g_unified_desc = uint32_t(std::clamp<int64_t>(Int(t["descripcion"], 0), 0, 0xFFFF));
  if (const int64_t dt = Int(t["descripcion_teleport"], -1); dt > 0 && dt < 0x10000)
    g_cap_desc[1970] = uint32_t(dt);
  auto pairs = [](const std::vector<int64_t>& v) {
    for (size_t k = 0; k + 1 < v.size(); k += 2) {
      if (v[k] > 0 && v[k] < 0x10000 && v[k + 1] > 0 && v[k + 1] < 0x10000)
        g_bank_redirect[0x10000u | uint32_t(v[k])] = 0x10000u | uint32_t(v[k + 1]);
    }
  };
  pairs(Ints(t["bancos"]));
  const std::string want = t["roster_hash"].value_or(std::string());
  if (want == RosterStamp()) {
    pairs(Ints(t["bancos_roster"]));
  } else {
    REXLOG_WARN("dbz3 roster [{}]: capsula unica: el _roster cambio desde que se hizo (vuelve a ejecutar "
                "forma_unica.py): sin nombres en los menus de los personajes nuevos", mod);
  }
  REXLOG_INFO("dbz3 roster [{}]: capsula unica activa ({} bancos de nombres, descripcion {})", mod,
              g_bank_redirect.size(), g_unified_desc);
}

void ApplyPatch(Guest& g, const toml::table& t, const std::string& mod) {
  const int64_t addr = Int(t["dir"], -1);
  if (addr < 0x82000000LL || addr >= 0x82700000LL) {
    REXLOG_WARN("dbz3 roster [{}]: parche fuera de la imagen ({:#x})", mod, addr);
    return;
  }
  uint32_t a = uint32_t(addr);
  for (auto v : Ints(t["u32"])) { g.W32(a, uint32_t(v)); a += 4; }
  for (auto v : Ints(t["u16"])) { g.W16(a, uint16_t(v)); a += 2; }
  for (auto v : Ints(t["u8"])) { g.W8(a, uint8_t(v)); a += 1; }
  REXLOG_INFO("dbz3 roster [{}]: parche {:#x} ({} B)", mod, uint32_t(addr), a - uint32_t(addr));
}

}  // namespace

uint64_t ExtraUnlockMask() { return g_unlock.load(); }

std::atomic<uint64_t> g_extra_slots{0};

int CostumeCountOf(uint32_t id) { return id < 64 ? g_costume_count[id].load() : 0; }

void AddExtraCostumes(uint8_t* base) {
  uint64_t slots = 0;
  for (uint32_t id = 0; id < 44; ++id) {
    const int extra = g_extra_costumes[id].load();
    if (extra <= 0) continue;
    const uint32_t slot = REX_LOAD_U16(kIdToSlot + id * 2);
    if (slot >= 38) continue;
    const uint32_t at = kCostumeCounts + slot;
    REX_STORE_U8(at, uint8_t(std::min(255, REX_LOAD_U8(at) + extra)));
    slots |= uint64_t(1) << slot;
  }
  g_extra_slots.store(slots);
}

bool SlotHasExtraCostumes(uint32_t slot) { return slot < 64 && ((g_extra_slots.load() >> slot) & 1); }

int DonorOf(uint32_t id) { return id < uint32_t(kIds) ? g_donor[id].load() : -1; }
int TeamSize(uint32_t id) { return id < uint32_t(kIds) ? g_team_size[id].load() : 0; }
int TeamMember(uint32_t id, int k) {
  return id < uint32_t(kIds) && k >= 0 && k < 5 ? g_team_member[id][k].load() : -1;
}

std::string InternalName(uint32_t id) {
  if (!g_memory || id >= 105) return {};
  Guest g(g_memory);
  const uint32_t rec = g.U32(kChar96 + id * 96);
  if (!rec) return {};
  std::string out;
  for (uint32_t k = 0; k < 28 && g.U8(rec + k); ++k) out += char(g.U8(rec + k));
  return out;
}


namespace {
std::atomic<int> g_pick_id[2] = {-1, -1}, g_pick_host[2] = {-1, -1};
}  // namespace
void SetPickedCell(int player, int id, int host) {
  if (player < 0 || player > 1) return;
  g_pick_id[player].store(id);
  g_pick_host[player].store(host);
}
int PickedCellFor(int player, int host) {
  if (player < 0 || player > 1 || g_pick_id[player].load() < 0) return -1;
  return g_pick_host[player].load() == host ? g_pick_id[player].load() : -1;
}
void ClearPickedCells() {
  for (int k = 0; k < 2; ++k) SetPickedCell(k, -1, -1);
}

int VirtualCellCount() { return int(g_cells.size()); }

const VirtualCell& GetVirtualCell(int i) { return g_cells[size_t(i)]; }

uint32_t GuestAlloc(uint32_t size) {
  if (!g_memory) return 0;
  const uint32_t a = g_memory->SystemHeapAlloc(size, 0x20);
  if (a) std::memset(g_memory->TranslateVirtual<uint8_t*>(a), 0, size);
  return a;
}

void GuardGeneratedMod() {
  std::error_code ec;
  const auto dir = ModsRoot() / "_roster";
  if (!std::filesystem::is_directory(dir, ec)) return;
  const auto off = dir / ".disabled";
  const auto ours = dir / ".disabled_por_runtime";
  if (rex::cvar::Query<bool>("dbz3_afs_append")) {
    if (std::filesystem::exists(ours, ec)) {
      std::filesystem::remove(off, ec);
      std::filesystem::remove(ours, ec);
      rex::filesystem::AfsResetModCache();  // el runtime ya pudo escanear los mods
      REXLOG_INFO("dbz3 roster: runtime con entradas AFS anadidas: _roster reactivado");
    }
    return;
  }
  if (!std::filesystem::exists(off, ec)) {
    std::ofstream(off).put('\n');
    std::ofstream(ours) << "Desactivado: este rexruntime.dll no admite entradas AFS anadidas "
                           "(dbz3_afs_append).\n";
    rex::filesystem::AfsResetModCache();
    REXLOG_WARN("dbz3 roster: este rexruntime no admite entradas AFS anadidas: "
                "personajes nuevos (_roster) desactivados");
  }
}

// Catalogo de capsulas ampliado: copia del #SKC con las capsulas nuevas, los bits de
// dueno de las heredadas y de los objetos comunes para los IDs 44-63; los punteros
// 0x82375608/0x8237560C pasan a la copia. El #SKC ya esta en memoria al arrancar
// (si no, se reintenta desde los hooks del select y del combate).
void EnsureCapsules() {
  if (g_caps_done.load() || !g_memory) return;
  if (g_new_caps.empty() && g_cap_owner_add.empty() && (g_unlock.load() >> 44) == 0 && !g_unified_on.load()) {
    g_caps_done.store(true);
    return;
  }
  Guest g(g_memory);
  const uint32_t hdr = g.U32(kSkcHdrPtr);
  if (hdr < 0x82000000u || std::memcmp(g.P(hdr), "#SKC", 4) != 0) return;   // aun no cargado
  const uint32_t n = g.U32(hdr + 0x10);
  const uint32_t start = g.U32(hdr + 0x14);
  if (n == 0 || n > 4096 || g.U32(kSkcRecPtr) != hdr + start) return;
  uint32_t total = n;
  if (!g_new_caps.empty()) total = std::max(total, g_new_caps.rbegin()->first + 1);
  if (g_unified_on.load()) total = std::max(total, forms::kUnifiedBase + 64);
  const uint32_t buf = g.Alloc(start + kSkcRec * total);
  if (!buf) {
    REXLOG_ERROR("dbz3 roster: sin memoria para el catalogo de capsulas ampliado");
    g_caps_done.store(true);
    return;
  }
  std::memcpy(g.P(buf), g.P(hdr), start + kSkcRec * n);
  for (const auto& [cid, rec] : g_new_caps) std::memcpy(g.P(buf + start + kSkcRec * cid), rec.data(), 40);
  const uint64_t extra_ids = g_unlock.load() & ~kSkcEveryone;   // IDs 44-63 de personajes nuevos
  auto or_owner = [&](uint32_t cid, uint64_t bits) {
    const uint32_t a = buf + start + kSkcRec * cid;
    uint64_t m = (uint64_t(g.U32(a)) << 32) | g.U32(a + 4);
    m |= bits;
    g.W32(a, uint32_t(m >> 32));
    g.W32(a + 4, uint32_t(m));
  };
  for (uint32_t cid = 0; cid < n; ++cid) {
    const uint32_t a = buf + start + kSkcRec * cid;
    const uint64_t m = (uint64_t(g.U32(a)) << 32) | g.U32(a + 4);
    if (extra_ids && (m & kSkcEveryone) == kSkcEveryone) or_owner(cid, extra_ids);
  }
  for (const auto& [cid, bits] : g_cap_owner_add) {
    if (cid < total) or_owner(cid, bits);
  }
  // capsula unica de cada personaje con 2+ transformaciones agrupables: registro de su primera
  // transformacion, solo suya, sin requisito ni efecto, con la mitad de huecos (forms::UnifiedCost)
  for (int id = 0; g_unified_on.load() && id < kIds; ++id) {
    const int chain = GroupableChain(g, buf + start, total, id, false);
    if (chain < 2) continue;
    const uint16_t uid = forms::UnifiedId(id);
    const uint32_t first = g.U16(kChar372 + 372u * uint32_t(id) + 232);   // capsula de la forma 1
    std::array<uint8_t, 40> rec{};
    std::memcpy(rec.data(), g.P(buf + start + kSkcRec * first), 40);
    const uint64_t own = uint64_t(1) << id;
    for (int b = 0; b < 8; ++b) rec[size_t(b)] = uint8_t(own >> (56 - 8 * b));
    rec[8] = uint8_t((rec[8] & ~7) | forms::UnifiedCost(chain));
    rec[14] = 0x01;                                   // se equipa en la forma normal
    std::fill(rec.begin() + 16, rec.begin() + 36, 0);  // sin requisito, efecto ni "se convierte en"
    g.Write(buf + start + kSkcRec * uid, rec.data(), 40);
    if (g_unified_desc) g_cap_desc[uid] = g_unified_desc;
    g_unified_ids.push_back(uid);
  }
  // Capsula 1970: Transmision Instantanea (universal para todos los personajes, coste 1 slot)
  constexpr uint16_t kCapTeleport = 1970;
  if (kCapTeleport < total) {
    std::array<uint8_t, 40> rec{};
    const uint64_t own = ~0ull; // Todos los personajes (bits 0..63)
    for (int b = 0; b < 8; ++b) rec[size_t(b)] = uint8_t(own >> (56 - 8 * b));
    rec[8] = 0x21; // Clase objeto / habilidad
    rec[10] = 0x30; // Rareza oro/alta
    rec[14] = 0x01; // Equipada en forma normal
    rec[15] = 0x01; // Coste: 1 ranura
    g.Write(buf + start + kSkcRec * kCapTeleport, rec.data(), 40);
    if (!g_cap_desc.count(kCapTeleport) && g_unified_desc) g_cap_desc[kCapTeleport] = g_unified_desc + 1;
    REXLOG_INFO("dbz3 roster: capsula {} registrada: Transmision Instantanea (universal)", kCapTeleport);
  }
  if (!g_unified_ids.empty())
    REXLOG_INFO("dbz3 roster: {} capsulas unicas de transformacion ({}..{})", g_unified_ids.size(),
                g_unified_ids.front(), g_unified_ids.back());
  g.W32(buf + 0x10, total);
  g.W32(kSkcHdrPtr, buf);
  g.W32(kSkcRecPtr, buf + start);
  g_caps_done.store(true);
  REXLOG_INFO("dbz3 roster: catalogo de capsulas ampliado {} -> {} ({} nuevas, {} heredadas)", n, total,
              g_new_caps.size(), g_cap_owner_add.size());
}

void ApplyAtLaunch(rex::memory::Memory* memory) {
  ResetForms();
  for (auto& d : g_donor) d.store(-1);
  for (auto& c : g_extra_costumes) c.store(0);
  for (auto& c : g_costume_count) c.store(0);
  g_cells.clear();
  g_selpos.clear();
  g_host_slot.clear();
  g_new_caps.clear();
  g_cap_desc.clear();
  g_hud_fid.clear();
  g_cap_owner_add.clear();
  g_caps_done.store(false);
  g_unified_on.store(false);
  g_unified_all = true;
  g_unified_desc = 0;
  g_unified_ids.clear();
  g_bank_redirect.clear();
  g_unlock.store(0);
  g_skill_entries.clear();
  g_skill_recs.clear();
  LoadCustomLists();
  if (!memory) return;
  Guest g(memory);
  g_memory = memory;
  // Imagen US/NA o EU/PAL (nucleo dual): mismas tablas de .data; las de .rdata movidas.
  const bool eu = std::memcmp(g.P(kSignatureEu), "_charasel.cpp", 13) == 0;
  if ((!eu && std::memcmp(g.P(kSignature), "_charasel.cpp", 13) != 0) ||
      std::memcmp(g.P(g.U32(kChar96 + 22 * 96)), "GULDO", 5) != 0) {
    REXLOG_INFO("dbz3 roster: imagen desconocida: sin extension de plantilla");
    return;
  }
  dbz3::g_guest_eu = eu;
  kSlotToId = dbz3::GuestAddr(0x82020618, 0x82020620);
  kSlotToIdB = dbz3::GuestAddr(0x82021470, 0x82021460);
  kSlotToIdC = dbz3::GuestAddr(0x82024760, 0x82024778);
  kIdToSlot = dbz3::GuestAddr(0x82020668, 0x82020670);
  REXLOG_INFO("dbz3 roster: imagen {}", eu ? "EU/PAL" : "US/NA");
  InitializeForms();
  std::error_code ec;
  // ISO sin la copia iso_datos (fallo de copia / poco espacio): los overrides de data_usi/data_cmn no se sirven, y
  // las casillas nuevas saldrian en blanco y el juego caeria al pisarlas. El launcher ya avisa de "sin mods".
  const std::string iso = REXCVAR_GET(dbz3_iso_path);
  const bool sin_overrides = !iso.empty() && std::filesystem::is_regular_file(iso, ec) && !dbz3::settings::IsoDataCacheValid();
  if (sin_overrides) REXLOG_WARN("dbz3 roster: ISO sin copia de datos para mods: no se anaden casillas nuevas");
  for (const auto& info : ListMods(false)) {
    if (sin_overrides) break;
    if (!info.enabled) continue;
    const auto file = ModsRoot() / info.name / "roster.toml";
    if (!std::filesystem::is_regular_file(file, ec)) continue;
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    toml::table doc;
    try {
      doc = toml::parse(ss.str());
    } catch (const toml::parse_error& e) {
      REXLOG_ERROR("dbz3 roster [{}]: roster.toml invalido: {}", info.name, e.description());
      continue;
    }
    if (auto* arr = doc["personaje"].as_array()) {
      for (auto& e : *arr) {
        if (auto* t = e.as_table()) ApplyCharacter(g, *t, info.name);
      }
    }
    if (auto* arr = doc["capsula"].as_array()) {
      for (auto& e : *arr) {
        if (auto* t = e.as_table()) ReadCapsule(*t, info.name);
      }
    }
    if (auto* arr = doc["parche"].as_array()) {
      for (auto& e : *arr) {
        if (auto* t = e.as_table()) ApplyPatch(g, *t, info.name);
      }
    }
    if (auto* t = doc["forma_unica"].as_table()) ReadUnified(*t, info.name);
  }
  // capsula unica: su nombre y su panel los hace el juego desde tus AFS (mods/_forma_unica, transformaciones.cpp)
  // para cada personaje con 2+ formas con capsula, con el banco de nombres que usa (los IDs >= 44, el de g_hud_fid)
  {
    std::vector<std::pair<int, uint32_t>> banks;
    for (int id = 0; id < kIds; ++id) {
      const uint32_t c372 = kChar372 + 372u * uint32_t(id);
      const uint32_t n = std::min<uint32_t>(g.U32(c372 + 0xD0), forms::kForms);
      int caps = 0;
      for (uint32_t f = 1; f < n; ++f) caps += g.U16(c372 + 212 + 20 * f) != 0;
      uint32_t fid = 0xFFFF;
      if (id < 44) {
        fid = g.U16(kHudFace + 2u * uint32_t(id));
      } else if (const auto it = g_hud_fid.find(uint32_t(id)); it != g_hud_fid.end()) {
        fid = uint32_t(it->second) & 0xFFFF;
      }
      if (caps >= 2 && fid && fid != 0xFFFF) banks.emplace_back(id, fid);
    }
    if (dbz3::formas::PrepareNamesMod(banks, RosterStamp())) {
      rex::filesystem::AfsResetModCache();   // el guest aun no ha leido nada
      g_bank_redirect.clear();
      std::ifstream in(ModsRoot() / "_forma_unica" / "roster.toml", std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      try {
        const toml::table doc = toml::parse(ss.str());
        if (auto* t = doc["forma_unica"].as_table()) ReadUnified(*t, "_forma_unica");
      } catch (const toml::parse_error& e) {
        REXLOG_ERROR("dbz3 roster [_forma_unica]: roster.toml invalido: {}", e.description());
      }
    }
  }
  if (!g_cells.empty()) {
    // las casillas nuevas cambian temporalmente slot->ID y los retratos del slot
    // anfitrion mientras se procesa al jugador que esta en ellas (select_ext.cpp)
    for (uint32_t a : {kSlotToId, kSlotToIdB, kSlotToIdC, kPortraits}) g.MakeWritable(a, 0x200);
    REXLOG_INFO("dbz3 roster: {} casillas nuevas en la rueda del select", g_cells.size());
  }
  EnsureCapsules();
}

// La tabla ID -> ficha (0x82324468) acaba en ID -1 y la lee sub_820E6D70 al empezar el
// combate: durante esa llamada se anaden las entradas de los personajes nuevos tras ella.
void SkillTable(bool push) {
  static std::vector<uint8_t> saved;
  static uint32_t at = 0;
  if (!g_memory || g_skill_entries.empty()) return;
  Guest g(g_memory);
  if (push) {
    uint32_t t = 0;
    while (t < 256 && g.U32(kSkillTable + 16 * t) != 0xFFFFFFFFu) ++t;
    if (t >= 256) return;
    at = kSkillTable + 16 * t;
    const uint32_t n = uint32_t(g_skill_entries.size()) + 1;
    saved.assign(g.P(at), g.P(at) + 16 * n);
    std::vector<uint8_t> blk(16 * n);
    auto put = [&](size_t o, uint32_t v) {
      blk[o] = uint8_t(v >> 24); blk[o + 1] = uint8_t(v >> 16); blk[o + 2] = uint8_t(v >> 8); blk[o + 3] = uint8_t(v);
    };
    for (size_t k = 0; k < g_skill_entries.size(); ++k) {
      put(16 * k, g_skill_entries[k].id);
      put(16 * k + 4, 0xFF);
      put(16 * k + 8, uint32_t(g_skill_entries[k].hud));
      put(16 * k + 12, uint32_t(g_skill_entries[k].index));
    }
    std::memcpy(blk.data() + 16 * (n - 1), saved.data(), 16);   // el terminador original
    g.Write(at, blk.data(), uint32_t(blk.size()));
  } else if (at && !saved.empty()) {
    g.Write(at, saved.data(), uint32_t(saved.size()));
    saved.clear();
  }
}

// Registro de ficha k (= indice + 1): los propios (>= 1001) se sirven por el hueco 1.
uint32_t SkillRecordFor(uint32_t k) {
  if (k < uint32_t(kVirtualSkill) + 1) return 0;
  const uint32_t i = k - uint32_t(kVirtualSkill) - 1;
  return i < g_skill_recs.size() ? g_skill_recs[i] : 0;
}

void SetSkillSlot1(uint32_t rec) {
  if (!g_memory) return;
  Guest g(g_memory);
  g.W32(kSkillRecs + 4, rec);
}

uint32_t SkillSlot1() {
  if (!g_memory) return 0;
  Guest g(g_memory);
  return g.U32(kSkillRecs + 4);
}

uint32_t CapsuleCount() {
  if (!g_memory) return 596;
  Guest g(g_memory);
  const uint32_t hdr = g.U32(kSkcHdrPtr);     // la copia ampliada vive en el heap del sistema
  if (hdr < 0x10000u || std::memcmp(g.P(hdr), "#SKC", 4) != 0) return 596;
  return std::min<uint32_t>(g.U32(hdr + 0x10), kInventorySize);
}

void OwnNewCapsules(uint32_t inventory) {
  if (!g_memory || !inventory || g_new_caps.empty()) return;
  Guest g(g_memory);
  for (const auto& [cid, rec] : g_new_caps) {
    if (cid < kInventorySize && g.U8(inventory + cid) == 0) g.W8(inventory + cid, 1);
  }
}

uint8_t UnifiedForms(int id) {
  if (!g_unified_on.load() || !g_memory || id < 0 || id >= kIds) return 0;
  EnsureCapsules();   // combate lanzado sin pasar por el select (HUB): la forma inicial se decide antes
  Guest g(g_memory);
  const uint32_t count = CapsuleCount();
  const uint32_t uid = forms::UnifiedId(id);
  const uint32_t recs = g.U32(kSkcRecPtr);
  if (uid >= count || recs < 0x10000u || !((g.U32(recs + kSkcRec * uid + (id < 32 ? 4 : 0)) >> (id & 31)) & 1))
    return 0;
  const int k = GroupableChain(g, recs, count, id, !g_unified_all);
  return uint8_t(((1u << (k + 1)) - 1) & ~1u);   // bits 1..k
}

uint32_t UnifiedListed(int id) {
  return std::bitset<8>(UnifiedForms(id)).count() >= 2 ? forms::UnifiedId(id) : 0;
}

void GetCustomList(uint32_t id, uint16_t out[7]) {
  {
    std::lock_guard<std::mutex> lk(g_custom_mu);
    auto it = g_custom.find(id);
    if (it != g_custom.end()) {
      std::copy(it->second.begin(), it->second.end(), out);
      return;
    }
  }
  std::fill(out, out + 7, uint16_t(0xFFFF));
  if (!g_memory || id >= uint32_t(kIds)) return;
  Guest g(g_memory);                  // su lista Normal (char96 +80)
  const uint32_t c96 = kChar96 + id * 96;
  const uint32_t n = std::min<uint32_t>((g.U8(c96 + 80) << 8) | g.U8(c96 + 81), 7);
  for (uint32_t k = 0; k < n; ++k) out[k] = uint16_t((g.U8(c96 + 82 + 2 * k) << 8) | g.U8(c96 + 83 + 2 * k));
}

void SetCustomList(uint32_t id, const uint16_t in[7]) {
  std::array<uint16_t, 7> a;
  std::copy(in, in + 7, a.begin());
  std::lock_guard<std::mutex> lk(g_custom_mu);
  auto it = g_custom.find(id);
  if (it != g_custom.end() && it->second == a) return;
  g_custom[id] = a;
  SaveCustomLists();
  REXLOG_INFO("dbz3 roster: lista Custom de capsulas del id {} guardada", id);
}

bool SelectPos(uint32_t id, uint32_t player, uint32_t out[7]) {
  auto it = g_selpos.find(id);
  if (it == g_selpos.end() || player > 1) return false;
  for (uint32_t k = 0; k < 7; ++k) out[k] = it->second[7 * player + k];
  return true;
}

int HostSlotOf(uint32_t id) {
  auto it = g_host_slot.find(id);
  return it == g_host_slot.end() ? -1 : int(it->second);
}

}  // namespace dbz3::roster

// ---------------------------------------------------------------------------
// Hooks (US codegen + its EU/PAL twin, guest_region.h)
// ---------------------------------------------------------------------------

// Load-system init: r10 -> u32[r9] maximum file count per AFS partition
// (data_cmn 4000, data_usi 2720, adx 4560, lang 112, yah). The ADXF partition
// info buffer is sized from these; entries appended by mods (virtual AFS) need
// more room.
DBZ3_HOOK(sub_82083618, dbz3eu_sub_82083618) {
  const uint32_t n = ctx.r9.u32;
  const uint32_t arr = ctx.r10.u32;
  if (n > 0 && n < 16 && arr) {
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t v = REX_LOAD_U32(arr + 4 * i);
      if (v > 0 && v < 8192) REX_STORE_U32(arr + 4 * i, 8192);
    }
  }
  orig(ctx, base);
}

// Select: builds the per-mode availability state (r3); +56 = u64 mask of
// unlocked character IDs. Roster characters are always available.
DBZ3_HOOK(sub_8217A1C0, dbz3eu_sub_8217A178) {
  dbz3::roster::EnsureCapsules();
  const uint32_t st = ctx.r3.u32;
  orig(ctx, base);
  const uint64_t extra = dbz3::roster::ExtraUnlockMask();
  if (extra && st) {
    REX_STORE_U64(st + 56, REX_LOAD_U64(st + 56) | extra);
  }
}

// Battle setup: r3 = selection (players at +16, 80 B each, u16 char ID at +16)
// copied into the battle config (*0x823D6CF4 + 1680). A hardcoded list of IDs
// (22-26, 31 = the cut characters, 66/67/74/75, 80-104) is replaced there by
// Goku (ID 0) with Goku's char96 +80/+82 data, before the per-player init
// (sub_821007E8). Roster characters keep their ID.
namespace {
constexpr uint32_t kBattleCfgPtr = 0x823D6CF4;
thread_local int t_sel_ids[4] = {-1, -1, -1, -1};
thread_local bool t_in_setup = false;
}  // namespace

DBZ3_HOOK(sub_820FE668, dbz3eu_sub_820FE768) {
  const uint32_t sel = ctx.r3.u32;
  // Fase 2: BeforeBattleSetup se ejecuta tras recuperar PickedCell.
  for (uint32_t k = 0; sel && k < 2; ++k) {     // casilla nueva que la lista del rival deshizo
    const int now = int(REX_LOAD_U16(sel + 32 + 80 * k));
    if (const int id = dbz3::roster::PickedCellFor(int(k), now); id >= 0 && id != now) {
      REX_STORE_U16(sel + 32 + 80 * k, uint16_t(id));
      REXLOG_INFO("dbz3 roster: jugador {}: casilla nueva -> id {} (la seleccion decia {})", k + 1, id, now);
    }
  }
  dbz3::tag::BeforeBattleSetup(base, sel);   // despues de restaurar la casilla, antes de copiar a combate
  for (uint32_t k = 0; k < 4; ++k) {
    const int id = sel ? int(REX_LOAD_U16(sel + 32 + 80 * k)) : -1;
    t_sel_ids[k] = (id >= 0 && id < 64 && dbz3::roster::DonorOf(uint32_t(id)) >= 0) ? id : -1;
  }
  dbz3::roster::EnsureCapsules();
  t_in_setup = true;
  orig(ctx, base);
  t_in_setup = false;
}

DBZ3_HOOK(sub_821007E8, dbz3eu_sub_821008E8) {
  const uint32_t rec = ctx.r3.u32;
  const uint32_t cfg = REX_LOAD_U32(kBattleCfgPtr);
  if (t_in_setup && cfg && rec >= cfg + 1696 && (rec - cfg - 1696) % 80 == 0) {
    const uint32_t k = (rec - cfg - 1696) / 80;
    if (k < 4 && t_sel_ids[k] >= 0 && REX_LOAD_U16(rec + 16) != uint32_t(t_sel_ids[k])) {
      const uint32_t id = uint32_t(t_sel_ids[k]);
      const uint32_t c96 = 0x8234ABB8 + id * 96;
      REX_STORE_U16(rec + 16, uint16_t(id));
      REX_STORE_U16(rec + 52, REX_LOAD_U16(c96 + 80));
      for (uint32_t b = 0; b < 14; ++b) REX_STORE_U8(rec + 54 + b, REX_LOAD_U8(c96 + 82 + b));
    }
  }
  orig(ctx, base);
}

// Animation player: r3 = model instance, r4 = motion index. In the select screen
// the index is the CHARACTER ID inside the common select pose banks (data_cmn
// 3881-3883), which have no pose for the cut IDs: use the donor's pose.
DBZ3_HOOK(sub_82136328, dbz3eu_sub_82136430) {
  const uint32_t inst = ctx.r3.u32;
  // Sonda (DBZ3_ANIM_PROBE=1): que movimiento pone el juego a cada modelo (para la caida del relevo)
  static const bool probe = std::getenv("DBZ3_ANIM_PROBE") != nullptr;
  if (probe) REXLOG_INFO("dbz3 animprobe: inst={:08X} motion={} lr={:08X}", inst, ctx.r4.u32, uint32_t(ctx.lr));
  if (inst && ctx.r4.u32 < 64) {
    const uint32_t mt = REX_LOAD_U32(inst + 656);
    if (mt) {
      const uint32_t tbl = REX_LOAD_U32(mt + 20);
      if (tbl && !REX_LOAD_U32(tbl + 4 * ctx.r4.u32)) {
        const int donor = dbz3::roster::DonorOf(ctx.r4.u32);
        if (donor >= 0 && REX_LOAD_U32(tbl + 4 * uint32_t(donor))) {
          ctx.r4.u64 = uint64_t(donor);
        } else {
          return;  // sin pose: no animar (antes: lectura de 0 -> crash)
        }
      }
    }
  }
  orig(ctx, base);
}

// Insertado por fases12_recipes en roster_ext.cpp; solo lo llama el hilo guest.
namespace dbz3::roster {
namespace {
std::atomic<bool> g_forms_verified{false};
std::string g_forms_scope;
}
bool FormSelectionRange(uint32_t address, size_t size, bool write) {
  if (!g_forms_verified.load() || !g_memory || !forms::GuestRange(address, size)) return false;
  uint64_t cursor = address, end = cursor + size;
  while (cursor < end) {
    auto* heap = g_memory->LookupHeap(uint32_t(cursor));
    rex::memory::HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(uint32_t(cursor), &info) ||
        !(info.state & rex::memory::kMemoryAllocationCommit) ||
        !(info.protect & rex::memory::kMemoryProtectRead) ||
        (write && !(info.protect & rex::memory::kMemoryProtectWrite))) return false;
    const uint64_t next = uint64_t(info.base_address) + info.region_size;
    if (info.base_address > cursor || next <= cursor) return false;
    cursor = std::min(next, end);
  }
  return true;
}
bool ReadFormBytes(uint32_t address, void* out, size_t size) {
  if (!out || !FormSelectionRange(address, size, false)) return false;
  std::memcpy(out, g_memory->TranslateVirtual<uint8_t*>(address), size);
  return true;
}
forms::Snapshot ReadForms(int id, int suit) {
  auto snapshot = forms::Decode(ReadFormBytes, id, suit, g_forms_scope);
  if (!forms::CountValid(snapshot)) return snapshot;
  std::array<uint8_t, 4> pointer{};
  const uint32_t count = CapsuleCount();
  const bool catalogue = count >= 1 && count <= 2048 &&
      ReadFormBytes(0x8237560Cu, pointer.data(), pointer.size());
  const uint32_t records = catalogue ? forms::Be32(pointer.data()) : 0;
  for (int f = 1; f < snapshot.count; ++f) {
    auto& form = snapshot.forms[size_t(f)];
    if (!form.capsule) continue;
    form.capsule_record_valid = false;
    const uint64_t address = uint64_t(records) + 40ull * uint32_t(form.capsule);
    std::array<uint8_t, 40> record{};
    if (!records || id < 0 || id >= 64 || uint32_t(form.capsule) >= count ||
        address + record.size() > 0x100000000ull ||
        !ReadFormBytes(uint32_t(address), record.data(), record.size())) continue;
    const uint64_t owners = (uint64_t(forms::Be32(record.data())) << 32) | forms::Be32(record.data() + 4);
    if (!(owners & (uint64_t(1) << id)) || record[8] != 0x11 || record[15] > 70) continue;
    form.prerequisites = {int(forms::Be16(record.data() + 16)), int(forms::Be16(record.data() + 18))};
    if (uint32_t(form.prerequisites[0]) >= count || uint32_t(form.prerequisites[1]) >= count) continue;
    form.required_ki_tenths = record[15];
    form.capsule_record_valid = true;
  }
  if (const uint8_t grouped = UnifiedForms(id)) {
    snapshot.unified = forms::UnifiedId(id);
    for (int f = 1; f < snapshot.count; ++f) snapshot.forms[size_t(f)].grouped = (grouped >> f) & 1;
  }
  return snapshot;
}
void ResetForms() { g_forms_verified.store(false); }
void InitializeForms() {
  g_forms_scope = std::string(dbz3::g_guest_eu ? "eu:" : "us:") +
      dbz3::settings::ModProfile() + ":" + RosterStamp();
  forms::Initialize(dbz3::settings::UserDataRoot() / "forma_inicial.txt");
  g_forms_verified.store(true);
}
} // namespace dbz3::roster

// Select preview model (r3 = task, +48 = model object: +178 player, +180 character
// ID, +196..+220 position/scale/rotation read from 0x82372950[ID][player]). That
// table ends at ID 44: IDs >= 44 get their framing from roster.toml.
// Forma inicial elegida con LT/RT en la rueda: sub_8217FF20 rehace el modelo de la vista previa con
// sub_82135B00(ID, forma 0, traje, jugador + 1, poses 3881-3883); se le pasa la forma elegida (la
// valida mas alta hasta ella: del mismo ID y con modelo en el traje). Solo cambia el modelo 3D.
namespace {
thread_local dbz3::forms::Result t_preview_result;
thread_local int t_preview_id = -1;
std::atomic<uint32_t> g_preview_task[2] = {0, 0};

// Fase 2: PreviewForm se sustituye por forms::Resolve/ReadForms.
}  // namespace

// Forma mas alta que puede enseñar la vista previa del lado (0 = el personaje no se transforma; -1 =
// aun no hay vista previa): LT/RT no pasa de ahi y sin formas no se ofrece.
int dbz3_select_preview_max_form(int side) {
  const auto s = dbz3::forms::Copy(side).snapshot;
  return dbz3::forms::CountValid(s) ? s.count - 1 : -1;
}

DBZ3_HOOK(sub_82135B00, dbz3eu_sub_82135C08) {
  if (t_preview_result.valid && t_preview_result.applied > 0 && ctx.r4.u32 == 0 && ctx.r3.u32 == uint32_t(t_preview_id)) {
    ctx.r3.u64 = uint64_t(t_preview_result.model_id);
    ctx.r4.u64 = uint64_t(t_preview_result.model_index);
  }
  orig(ctx, base);
}

DBZ3_HOOK(sub_8217FF20, dbz3eu_sub_8217FED8) {
  const uint32_t obj = ctx.r3.u32 ? REX_LOAD_U32(ctx.r3.u32 + 48) : 0;
  const int side = obj ? int(REX_LOAD_U16(obj + 178)) : -1;
  if (side == 0 || side == 1) {
    g_preview_task[size_t(side)] = ctx.r3.u32;
    if (dbz3::roster::FormSelectionRange(obj + 178, 6)) {
      const int id = int16_t(REX_LOAD_U16(obj + 180));
      const int suit = int16_t(REX_LOAD_U16(obj + 182));
      auto snapshot = dbz3::roster::ReadForms(id, suit);
      const auto previous = dbz3::forms::Copy(side);
      if (previous.snapshot.identity == snapshot.identity) snapshot.capsules = previous.snapshot.capsules;
      dbz3::forms::Publish(side, snapshot, !dbz3::ingame::FormSelectorEnabled());
      t_preview_id = id;
      t_preview_result = dbz3::forms::Resolve(snapshot, dbz3::ingame::ChosenForm(side), dbz3::ingame::FormSelectorEnabled());
    }
    else {  // sin lectura valida: no dejar las formas del personaje anterior en la vista previa
      dbz3::forms::Publish(side, dbz3::forms::Snapshot{}, !dbz3::ingame::FormSelectorEnabled());
    }
  }
  orig(ctx, base);
  t_preview_result = {};
  t_preview_id = -1;
  if (!obj) return;
  const int16_t id = int16_t(REX_LOAD_U16(obj + 180));
  uint32_t v[7];
  if (id >= 44 && dbz3::roster::SelectPos(uint32_t(id), REX_LOAD_U16(obj + 178), v)) {
    for (uint32_t k = 0; k < 4; ++k) REX_STORE_U32(obj + 196 + 4 * k, v[k]);
    for (uint32_t k = 0; k < 3; ++k) REX_STORE_U32(obj + 212 + 4 * k, v[4 + k] & 0xFFFF);
  }
}

// LT/RT cambio la forma de un lado: su vista previa se rehace (desde la tarea de la rueda, cada tick).
void dbz3_select_refresh_preview(PPCContext& ctx, uint8_t* base) {
  for (int side = 0; side < 2; ++side) {
    if (!dbz3::ingame::TakeFormDirty(side)) continue;
    const uint32_t task = g_preview_task[side].load();
    const uint32_t obj = task ? REX_LOAD_U32(task + 48) : 0;
    if (!obj || REX_LOAD_U16(obj + 178) != uint32_t(side)) continue;
    PPCContext c = ctx;
    c.r3.u64 = task;
    DBZ3_CALL2(sub_8217FF20, dbz3eu_sub_8217FED8)(c, base);
  }
}


// ID -> slot (r3 = ID; the table at 0x82020668 has 44 entries but IDs < 64 are read
// from it): roster IDs >= 44 answer their host slot.
DBZ3_HOOK(sub_82159A88, dbz3eu_sub_82159A48) {
  const uint32_t id = ctx.r3.u32;
  if (id >= 44 && id < 64) {
    const int slot = dbz3::roster::HostSlotOf(id);
    if (slot >= 0) {
      ctx.r3.u64 = uint64_t(slot);
      return;
    }
  }
  orig(ctx, base);
}

// Fichas de habilidades del combate (r3 = ?, recorre los jugadores): entradas de la
// tabla ID -> ficha para los personajes nuevos (ver SkillTable).
DBZ3_HOOK(sub_820E6D70, dbz3eu_sub_820E6E60) {
  dbz3::roster::SkillTable(true);
  orig(ctx, base);
  dbz3::roster::SkillTable(false);
}

// Carga la ficha k (r3): las propias de los personajes nuevos (k >= 1001) se cargan por
// el hueco 1 de la lista de registros, cambiado solo durante la llamada.
DBZ3_HOOK(sub_821B5FC0, dbz3eu_sub_821B4DA8) {
  const uint32_t rec = dbz3::roster::SkillRecordFor(ctx.r3.u32);
  if (!rec) {
    orig(ctx, base);
    return;
  }
  const uint32_t old = dbz3::roster::SkillSlot1();
  dbz3::roster::SetSkillSlot1(rec);
  ctx.r3.u64 = 1;
  orig(ctx, base);
  dbz3::roster::SetSkillSlot1(old);
}

// ---------------------------------------------------------------------------
// "Edit Skills" (seleccion de personaje): el juego recorre las capsulas 1..594 con
// una constante; con el catalogo ampliado (IDs >= 596) se recorre entero.
// Estado del editor por jugador: +28 ID del personaje, +36 pestana, +44 inventario
// (u8 por capsula), +76 + 28*pestana: bloque (+0 desplazamiento, +4 cursor, +8 9 x u16
// visibles, +26 total), +188 lista equipada (u32 n + 7 u32), +32 casillas usadas.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kSkcRecs = 0x8237560C;

bool ListedCapsule(uint8_t* base, uint32_t rec, uint64_t owner_bit, uint32_t tab, uint32_t inv_count) {
  return (REX_LOAD_U64(rec) & owner_bit) != 0 && (REX_LOAD_U8(rec + 10) & 3) == tab &&
         (REX_LOAD_U8(rec + 8) & 7) != 0 && inv_count != 0;
}

bool IsVirtualEquipped(uint8_t* base, uint32_t st, uint32_t cap_id) {
  for (uint32_t k = 0; k < 7; ++k) {
    const uint32_t raw1 = REX_LOAD_U32(st + 192 + 4 * k);
    if (raw1 != 0 && raw1 != 0xFFFFFFFFu) {
      const uint32_t c1 = (raw1 & 0xFFFFF000u) ? (raw1 & 0xFFF) : (raw1 & 0xFFFF);
      if (raw1 == cap_id || c1 == cap_id) return true;
    }
  }
  return false;
}
}  // namespace

// Lista visible de la pestana (9 capsulas desde el desplazamiento). Misma logica que el
// original (sub_821B6ED8) con el catalogo entero.
// s9 (reportes de cierres al editar habilidades): punteros del editor sin comprobar -> se deja al juego (orig)
// Nota: el catalogo ampliado vive en SystemHeap (0x30000000+), por lo que se admite desde 0x10000.
inline bool PtrEditor(uint32_t p) { return p >= 0x00010000u && p < 0xA0000000u; }
DBZ3_HOOK(sub_821B6ED8, dbz3eu_sub_821B5CC0) {
  const uint32_t st = ctx.r3.u32;
  if (!PtrEditor(st) || !PtrEditor(REX_LOAD_U32(st + 44)) || !PtrEditor(REX_LOAD_U32(kSkcRecs))) {
    orig(ctx, base);
    return;
  }
  const uint32_t tab = REX_LOAD_U32(st + 36);
  const uint64_t bit = uint64_t(1) << (int32_t(REX_LOAD_U32(st + 28)) & 63);
  const uint32_t inv = REX_LOAD_U32(st + 44);
  const uint32_t uid = dbz3::roster::UnifiedListed(int32_t(REX_LOAD_U32(st + 28)));   // tuya sin tocar el inventario
  // PRUEBA (DBZ3_TEST_OWN="1,2"): esas capsulas aparecen como tuyas en el editor (copia de pruebas)
  if (static const char* own = std::getenv("DBZ3_TEST_OWN"); own && inv) {
    for (const char* p = own; *p;) {
      const uint32_t c = uint32_t(std::atoi(p));
      if (c > 0 && c < 2048 && !REX_LOAD_U8(inv + c)) REX_STORE_U8(inv + c, 1);
      while (*p && *p != ',') ++p;
      if (*p) ++p;
    }
  }
  const uint32_t recs = REX_LOAD_U32(kSkcRecs);
  const uint32_t blk = st + 76 + 28 * tab;
  const uint32_t out = blk + 8;
  const uint32_t skip = REX_LOAD_U32(blk);
  const uint32_t n = dbz3::roster::CapsuleCount();
  uint32_t skipped = 0, shown = 0;
  for (uint32_t id = 1; id < n; ++id) {
    const bool is_virt = (id == uid || id == 1970);
    const uint8_t owned_count = is_virt ? (IsVirtualEquipped(base, st, id) ? 0u : 1u) : REX_LOAD_U8(inv + id);
    if (!ListedCapsule(base, recs + 40 * id, bit, tab, owned_count)) continue;
    if (int32_t(skipped) < int32_t(skip)) {
      ++skipped;
      continue;
    }
    if (shown < 9) REX_STORE_U16(out + 2 * shown, uint16_t(id));
    ++shown;
  }
  REX_STORE_U16(blk + 26, uint16_t(skip + shown));
  for (uint32_t k = shown; k < 9; ++k) REX_STORE_U16(out + 2 * k, 0xFFFF);
  ctx.r3.u64 = skipped;
}

// Total de la pestana y cursor sobre la capsula r4 (sub_821B6FE8, catalogo entero).
DBZ3_HOOK(sub_821B6FE8, dbz3eu_sub_821B5DD0) {
  const uint32_t st = ctx.r3.u32;
  const int32_t want = ctx.r4.s32;
  const uint32_t recs = REX_LOAD_U32(kSkcRecs);
  if (!PtrEditor(st) || !PtrEditor(recs) || !PtrEditor(REX_LOAD_U32(st + 44)) || want >= int32_t(dbz3::roster::kInventorySize)) {
    orig(ctx, base);   // s9: cursor fuera del catalogo o editor sin montar
    return;
  }
  if (want > 0) REX_STORE_U32(st + 36, REX_LOAD_U8(recs + 40 * uint32_t(want) + 10) & 3);
  const uint32_t tab = REX_LOAD_U32(st + 36);
  const uint64_t bit = uint64_t(1) << (int32_t(REX_LOAD_U32(st + 28)) & 63);
  const uint32_t inv = REX_LOAD_U32(st + 44);
  const uint32_t uid = dbz3::roster::UnifiedListed(int32_t(REX_LOAD_U32(st + 28)));
  const uint32_t blk = st + 28 * tab;
  const uint32_t n = dbz3::roster::CapsuleCount();
  int32_t pos = 0, count = 0;
  for (uint32_t id = 1; id < n; ++id) {
    if (int32_t(id) == want) pos = count;
    const bool is_virt = (id == uid || id == 1970);
    const uint8_t owned_count = is_virt ? (IsVirtualEquipped(base, st, id) ? 0u : 1u) : REX_LOAD_U8(inv + id);
    if (ListedCapsule(base, recs + 40 * id, bit, tab, owned_count)) ++count;
  }
  REX_STORE_U16(blk + 102, uint16_t(count));
  if (count < 9) {
    REX_STORE_U32(blk + 76, 0);
    REX_STORE_U32(blk + 80, uint32_t(pos));
  } else {
    const int32_t rel = pos - int32_t(REX_LOAD_U32(blk + 76));
    if (rel < 9) {
      REX_STORE_U32(blk + 80, uint32_t(rel));
    } else if (pos + 9 < count) {
      REX_STORE_U32(blk + 76, uint32_t(pos));
      REX_STORE_U32(blk + 80, 0);
    } else {
      REX_STORE_U32(blk + 76, uint32_t(count - 9));
      REX_STORE_U32(blk + 80, uint32_t((count - 9) - pos));   // como el original
    }
  }
  ctx.r3.u64 = 0;
}

// Carga de una lista en el editor (r3 = jugador*2, r4 = u32 n + 7 u32): el original
// descarta las capsulas >= 595; se rehace la lista equipada con el catalogo entero.
DBZ3_HOOK(sub_821B7290, dbz3eu_sub_821B6078) {
  const uint32_t player = ctx.r3.u32;
  const uint32_t src = ctx.r4.u32;
  uint32_t list[8] = {};
  bool extra = false;
  const uint32_t n = dbz3::roster::CapsuleCount();
  if (src && n > 595) {
    for (uint32_t k = 0; k < 8; ++k) list[k] = REX_LOAD_U32(src + 4 * k);
    for (uint32_t k = 1; k < 8 && int32_t(k) <= int32_t(list[0]); ++k) {
      const uint32_t v = (list[k] & 0xFFFFF000u) ? (list[k] & 0xFFF) : list[k];
      if (v >= 595 && v < n) extra = true;
    }
  }
  orig(ctx, base);
  if (!extra || !PtrEditor(REX_LOAD_U32(dbz3::GuestAddr(0x8247971C, 0x8247970C)))) return;   // s9: sin estado del editor
  const uint32_t st = REX_LOAD_U32(dbz3::GuestAddr(0x8247971C, 0x8247970C)) + 460 * (player >> 1) + 68;
  const uint32_t recs = REX_LOAD_U32(kSkcRecs);
  uint32_t vals[7], kept = 0, slots = 0;
  for (uint32_t k = 0; k < 7; ++k) {
    const uint32_t v = (list[k + 1] & 0xFFFFF000u) ? (list[k + 1] & 0xFFF) : list[k + 1];
    if (int32_t(v) > 0 && v < n) vals[kept++] = v;
  }
  REX_STORE_U32(st + 188, kept);
  for (uint32_t k = 0; k < 7; ++k) REX_STORE_U32(st + 192 + 4 * k, k < kept ? vals[k] : 0xFFFFFFFFu);
  for (uint32_t k = 0; k < kept; ++k) slots += REX_LOAD_U8(recs + 40 * vals[k] + 8) & 7;
  REX_STORE_U32(st + 32, slots < 7 ? slots + 1 : slots);
}

// Editor de "Edit Skills" por frame (r3 = estado del editor): dibuja la cara del
// personaje con un indice de sprite de la tabla u8 0x82373D38[ID] (44 entradas; los IDs
// recortados tienen 0xFF y los 44-63 leen fuera) -> puntero nulo y cierre. Durante la
// llamada los personajes nuevos usan la cara de su donante (+28 solo se lee ahi).
DBZ3_HOOK(sub_821B8FC8, dbz3eu_sub_821B7DB0) {
  const uint32_t st = ctx.r3.u32;
  const uint32_t id = st ? REX_LOAD_U32(st + 28) : 0xFFFFFFFFu;
  const int donor = id < 64 ? dbz3::roster::DonorOf(id) : -1;
  const bool swap = donor >= 0 && (id >= 44 || REX_LOAD_U8(0x82373D38 + id) == 0xFF);
  if (swap) REX_STORE_U32(st + 28, uint32_t(donor));
  orig(ctx, base);
  if (swap) REX_STORE_U32(st + 28, id);
}

// Bandeja de "Edit Skills" (r3 = estado del editor, +188 lista equipada): sub_821B7C50 no
// dibuja las capsulas >= 595. Durante la llamada cada capsula nueva de la lista se presenta
// con un ID "prestado" < 595 (su registro intercambiado con el del prestado) y el nombre
// se pide con el ID real (sub_82146430: r3 = sprite, +24 textura; r5 = ID en el banco).
namespace {
constexpr int kMaxProxies = 16;
thread_local int t_proxy_n = 0;
thread_local uint32_t t_proxy_from[kMaxProxies];   // ID prestado (< 595)
thread_local uint32_t t_proxy_to[kMaxProxies];     // ID real (>= 595)

void SwapRecords(uint8_t* base, uint32_t a, uint32_t b) {
  const uint32_t recs = REX_LOAD_U32(kSkcRecs);
  for (uint32_t i = 0; i < 40; ++i) {
    const uint8_t t = REX_LOAD_U8(recs + 40 * a + i);
    REX_STORE_U8(recs + 40 * a + i, REX_LOAD_U8(recs + 40 * b + i));
    REX_STORE_U8(recs + 40 * b + i, t);
  }
}

// Presta un ID < 595 (que no este en `used`) a cada capsula nueva de `ids`, con los
// registros del catalogo intercambiados hasta ProxyEnd. Devuelve el ID a usar.
uint32_t ProxyFor(uint8_t* base, uint32_t real, const std::vector<uint32_t>& used) {
  for (int i = 0; i < t_proxy_n; ++i) {
    if (t_proxy_to[i] == real) return t_proxy_from[i];
  }
  if (t_proxy_n >= kMaxProxies) return real;
  uint32_t cand = 594;
  for (;;) {
    bool busy = std::find(used.begin(), used.end(), cand) != used.end();
    for (int i = 0; i < t_proxy_n && !busy; ++i) busy = t_proxy_from[i] == cand;
    if (!busy) break;
    if (--cand == 0) return real;
  }
  t_proxy_from[t_proxy_n] = cand;
  t_proxy_to[t_proxy_n] = real;
  ++t_proxy_n;
  SwapRecords(base, cand, real);
  return cand;
}

uint32_t RealOf(uint32_t v) {
  for (int i = 0; i < t_proxy_n; ++i) {
    if (t_proxy_from[i] == v) return t_proxy_to[i];
  }
  return v;
}

void ProxyEnd(uint8_t* base) {
  for (int i = t_proxy_n - 1; i >= 0; --i) SwapRecords(base, t_proxy_from[i], t_proxy_to[i]);
  t_proxy_n = 0;
}
}  // namespace

DBZ3_HOOK(sub_821B7C50, dbz3eu_sub_821B6A38) {
  const uint32_t st = ctx.r3.u32;
  const uint32_t n = dbz3::roster::CapsuleCount();
  const uint32_t cnt = st && n > 595 && t_proxy_n == 0 ? std::min<uint32_t>(REX_LOAD_U32(st + 188), 7) : 0;
  std::vector<uint32_t> list(cnt);
  for (uint32_t k = 0; k < cnt; ++k) list[k] = REX_LOAD_U32(st + 192 + 4 * k);
  for (uint32_t k = 0; k < cnt; ++k) {
    if (list[k] >= 595 && list[k] < n) REX_STORE_U32(st + 192 + 4 * k, ProxyFor(base, list[k], list));
  }
  if (t_proxy_n == 0) {
    orig(ctx, base);
    return;
  }
  orig(ctx, base);
  for (uint32_t k = 0; k < cnt; ++k) REX_STORE_U32(st + 192 + 4 * k, RealOf(REX_LOAD_U32(st + 192 + 4 * k)));
  ProxyEnd(base);
}

// Antes del combate en el modo 5 (cfg +2018) sub_820FE3D8 limpia las listas de
// capsulas de la seleccion (r3: jugadores de 80 B, +68 n, +70 7 x s16) y borra las >= 595:
// mismas capsulas prestadas durante la llamada.
DBZ3_HOOK(sub_820FE3D8, dbz3eu_sub_820FE4D8) {
  const uint32_t sel = ctx.r3.u32;
  const uint32_t n = dbz3::roster::CapsuleCount();
  int players = sel ? int16_t(REX_LOAD_U16(sel + 336)) : 0;
  if (players <= 0) players = 2;
  players = std::min(players, 4);
  if (!sel || n <= 595 || t_proxy_n != 0) {
    orig(ctx, base);
    return;
  }
  std::vector<uint32_t> used;
  for (int p = 0; p < players; ++p) {
    for (uint32_t k = 0; k < 7; ++k) used.push_back(REX_LOAD_U16(sel + 70 + 80 * p + 2 * k));
  }
  for (int p = 0; p < players; ++p) {
    for (uint32_t k = 0; k < 7; ++k) {
      const uint32_t a = sel + 70 + 80 * p + 2 * k;
      const uint32_t v = REX_LOAD_U16(a);
      if (v >= 595 && v < n) REX_STORE_U16(a, uint16_t(ProxyFor(base, v, used)));
    }
  }
  orig(ctx, base);
  if (t_proxy_n == 0) return;
  for (int p = 0; p < players; ++p) {
    for (uint32_t k = 0; k < 7; ++k) {
      const uint32_t a = sel + 70 + 80 * p + 2 * k;
      REX_STORE_U16(a, uint16_t(RealOf(REX_LOAD_U16(a))));
    }
  }
  ProxyEnd(base);
}

DBZ3_HOOK(sub_82146430, dbz3eu_sub_821A1F00) {
  for (int i = 0; i < t_proxy_n; ++i) {
    if (ctx.r5.u32 == t_proxy_from[i]) {
      ctx.r5.u64 = t_proxy_to[i];
      REX_STORE_U16(ctx.r3.u32 + 24, uint16_t(t_proxy_to[i]));
      break;
    }
  }
  // ID sin entrada en el banco (#AZT: +0x10 n, +0x14 indice, +0x18 primer ID): el juego leeria fuera del
  // indice. Pasa con una capsula nueva cuyo nombre no esta en ese banco (capsula unica sin forma_unica.py).
  if (const uint32_t bank = ctx.r4.u32) {
    const uint32_t k = ctx.r5.u32 - REX_LOAD_U32(bank + 24);
    if (k >= REX_LOAD_U32(bank + 16) || !REX_LOAD_U32(bank + REX_LOAD_U32(bank + 20) + 4 * k)) {
      static int logged = 0;
      if (logged++ < 10) REXLOG_WARN("dbz3 roster: capsula {} sin nombre en el banco {:08X}", ctx.r5.u32, bank);
      return;
    }
  }
  orig(ctx, base);
}

// Panel de descripcion de "Edit Skills" (sub_821BB680: r4 = capsula; carga data_usi
// 2079 + ID con sub_82083968). La lista de capacidades lo pide solo para ID < 595
// (sub_821B95D0, tras sub_821B7218 = capsula bajo el cursor): para una capsula nueva esa
// llamada devuelve 594 y el panel recibe el ID real, con su descripcion propia
// (roster.toml `descripcion`; sin ella el panel no sale, como antes).
namespace {
constexpr uint32_t kDescStand = 594;
thread_local uint32_t t_desc_real = 0;
thread_local uint32_t t_desc_fid = 0;
thread_local uint32_t t_desc_from = 0;
}  // namespace

DBZ3_HOOK(sub_821B7218, dbz3eu_sub_821B6000) {
  const uint32_t ret = uint32_t(ctx.lr);
  orig(ctx, base);
  t_desc_real = 0;
  if (ret == dbz3::GuestAddr(0x821B98E8, 0x821B86D0) && ctx.r3.s32 >= 595 && dbz3::roster::g_cap_desc.count(ctx.r3.u32)) {
    t_desc_real = ctx.r3.u32;
    ctx.r3.u64 = kDescStand;
  }
}

DBZ3_HOOK(sub_821BB680, dbz3eu_sub_821BA468) {
  uint32_t fid = 0;
  if (t_desc_real && ctx.r4.u32 == kDescStand) {
    ctx.r4.u64 = t_desc_real;
    fid = dbz3::roster::g_cap_desc[t_desc_real];
  } else if (auto it = dbz3::roster::g_cap_desc.find(ctx.r4.u32); it != dbz3::roster::g_cap_desc.end()) {
    fid = it->second;      // otras pantallas que ya pasan el ID real
  }
  t_desc_real = 0;
  t_desc_from = fid ? 0x10000u | (2079u + ctx.r4.u32) : 0;
  t_desc_fid = fid ? 0x10000u | fid : 0;
  orig(ctx, base);
  t_desc_from = t_desc_fid = 0;
}

// Nombres de las capsulas del personaje (data_usi 0x82373D68[ID], u16 x 44): para los
// IDs >= 44 el juego lee fuera de la tabla; durante la llamada, el fichero que pediria se
// cambia por el suyo (g_hud_fid). sub_821B6B98: r3 = ID (combate); sub_821B9F90: r5 =
// casilla del select (ID con la tabla casilla -> ID).
namespace {
thread_local uint32_t t_hud_from = 0;
thread_local uint32_t t_hud_to = 0;

bool HudRemap(uint8_t* base, int32_t id) {
  auto it = id >= 44 ? dbz3::roster::g_hud_fid.find(uint32_t(id)) : dbz3::roster::g_hud_fid.end();
  if (it == dbz3::roster::g_hud_fid.end()) return false;
  // mismo valor que calcula el juego: lhax + oris 1
  t_hud_from = uint32_t(int32_t(int16_t(REX_LOAD_U16(dbz3::roster::kHudFace + 2 * uint32_t(id))))) | 0x10000u;
  t_hud_to = 0x10000u | (uint32_t(it->second) & 0xFFFF);
  return true;
}
}  // namespace

DBZ3_HOOK(sub_821B6B98, dbz3eu_sub_821B5980) {
  const bool remap = HudRemap(base, ctx.r3.s32);
  orig(ctx, base);
  if (remap) t_hud_from = t_hud_to = 0;
}

DBZ3_HOOK(sub_821B9F90, dbz3eu_sub_821B8D78) {
  const uint32_t slot = ctx.r5.u32;
  const bool remap = slot < 64 && HudRemap(base, int16_t(REX_LOAD_U16(dbz3::roster::kSlotToId + 2 * slot)));
  orig(ctx, base);
  if (remap) t_hud_from = t_hud_to = 0;
}

// Carga de un fichero (r3 = 0x10000 | indice de data_usi): durante el panel de una
// capsula nueva, 2079 + ID se cambia por su descripcion.
DBZ3_HOOK(sub_82083968, dbz3eu_sub_82083968) {
  if (t_desc_from && ctx.r3.u32 == t_desc_from) ctx.r3.u64 = t_desc_fid;
  if (t_hud_from && ctx.r3.u32 == t_hud_from) ctx.r3.u64 = t_hud_to;
  // banco de nombres -> su copia con la capsula unica (forma_unica.py)
  if (const auto it = dbz3::roster::g_bank_redirect.find(ctx.r3.u32); it != dbz3::roster::g_bank_redirect.end())
    ctx.r3.u64 = it->second;
  orig(ctx, base);
}

// Puntos de anclaje del luchador (sub_82112AB0: golpes/efectos pegados a un hueso). El juego
// traduce cada "hueso estandar" (31: cintura, estomago, cuello, cabeza, manos, nodos NW/NH,
// pelo, boca...) a un hueso del modelo con la tabla s16 de player +2956, buscando por nombre.
// Un modelo portado (Shin Budokai, IW...) no tiene todos esos huesos y la tabla queda a -1:
// el juego multiplicaba -1 x 64 y escribia fuera de la matriz (cierre en combate, RE
// 2026-10-04 con Gohan del Futuro). Los huecos se rellenan con la cabeza (pelo, boca, cara,
// NH) o la raiz.
namespace {
void FixAnchorTable(uint8_t* base, uint32_t pl) {
  const uint32_t tbl = REX_LOAD_U32(pl + 2956);
  if (tbl < 0x40000000u || tbl >= 0xA0000000u) return;
  const int16_t marker = int16_t(REX_LOAD_U16(tbl + 2 * 12));
  static uint32_t dumped[8] = {};
  static uint32_t ndumped = 0;
  if (ndumped < 8 && std::find(dumped, dumped + ndumped, tbl) == dumped + ndumped) {
    dumped[ndumped++] = tbl;
    std::string t;
    for (uint32_t i = 0; i < 31; ++i) t += std::to_string(int16_t(REX_LOAD_U16(tbl + 2 * i))) + " ";
    REXLOG_INFO("dbz3: anchor tabla {:08X}: {}", tbl, t);
  }
  for (uint32_t i = 0; i < 31; ++i) {
    if (int16_t(REX_LOAD_U16(tbl + 2 * i)) >= 0) continue;
    // Missing face/hair/jaw bones (PSP models): point them at the left-foot marker
    // (slot 12), an unskinned leaf, so lip-sync/hair rotations move nothing
    // visible. Pointing them at the head bent the head into the chest.
    REX_STORE_U16(tbl + 2 * i, uint16_t(marker >= 0 ? marker : 0));
  }
  // Effects attached to a standard slot (desc +10): a slot outside the 31-entry
  // table reads past it and yields a wild bone index (SB ports crashed here).
  static int logged = 0;
  for (uint32_t n = REX_LOAD_U32(pl + 2752), guard = 0; n >= 0x40000000u && n < 0xA0000000u && guard < 256;
       n = REX_LOAD_U32(n + 44), ++guard) {
    const uint32_t d = REX_LOAD_U32(n + 32);
    if (d < 0x40000000u || d >= 0xA0000000u) continue;
    const uint16_t slot = REX_LOAD_U16(d + 10);
    const int16_t bone = slot < 31 ? int16_t(REX_LOAD_U16(tbl + 2 * slot)) : -1;
    if (slot < 31 && bone >= 0 && bone < 128) continue;
    if (logged++ < 20) REXLOG_INFO("dbz3: anchor fix: efecto {:08X} slot {} hueso {} -> 0", d, slot, bone);
    REX_STORE_U16(d + 10, 0);
  }
}
}  // namespace

DBZ3_HOOK(sub_82112AB0, dbz3eu_sub_82112BB0) {
  FixAnchorTable(base, ctx.r3.u32);
  orig(ctx, base);
}

// Select: siguiente traje (r3 = casilla, r4 = trajes, r5 = actual, r6 = paso). El juego
// no da la vuelta en la casilla de Goku (su traje 3 es de evento: salta 2 -> 4 y el 4 es
// el ultimo). Con trajes anadidos: recorrido circular de todos, sin el 3 en Goku.
int dbz3_select_active_cell();  // select_ext.cpp

DBZ3_HOOK(sub_8217A5A0, dbz3eu_sub_8217A558) {
  const int slot = int16_t(ctx.r3.u32 & 0xFFFF);
  // v1.4.1: en una casilla nueva se recorren SOLO los trajes del personaje nuevo (antes los
  // del anfitrion: Janemba sobre Krillin ofrecia trajes que no tiene).
  if (const int vc = dbz3_select_active_cell(); vc >= 0 && vc < dbz3::roster::VirtualCellCount()) {
    const int n = dbz3::roster::CostumeCountOf(dbz3::roster::GetVirtualCell(vc).id);
    if (n > 0) {
      const int cur = int16_t(ctx.r5.u32 & 0xFFFF);
      const int step = int16_t(ctx.r6.u32 & 0xFFFF);
      ctx.r3.u64 = uint64_t(int64_t(((cur + step) % n + n) % n));
      return;
    }
  }
  if (slot < 0 || !dbz3::roster::SlotHasExtraCostumes(uint32_t(slot))) {
    orig(ctx, base);
    return;
  }
  const int count = int16_t(ctx.r4.u32 & 0xFFFF);
  const int cur = int16_t(ctx.r5.u32 & 0xFFFF);
  const int step = int16_t(ctx.r6.u32 & 0xFFFF);
  // Goku: la tabla cuenta los trajes elegibles (0, 1, 2, 4 y los anadidos 5, 6...)
  const bool skip3 = slot == 0;
  const int n = count;
  int pos = (skip3 && cur > 3) ? cur - 1 : cur;
  pos = n > 0 ? ((pos + step) % n + n) % n : 0;
  ctx.r3.u64 = uint64_t(int64_t((skip3 && pos >= 3) ? pos + 1 : pos));
}
