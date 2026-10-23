// dbz3 - Select wheel extension (US or EU/PAL image, guest_region.h): capacity 39 -> 64 cells and NEW cells
// declared by roster mods (roster_ext, `celda = true`).
//
// Wheel data (allocated by sub_8217E6D8, 1448 B; 2368 B here):
//   +0 sprite resource | +12 s8 cells | +16 u64 slot mask | +36 per-player block |
//   +40 current cell | +44 cells x 28 B (+2 s8 slot, +4 icon sprite, +8 frame
//   sprite, +12 ring position) | +1136 ring positions x 8 B (moved to +1840).
// The functions that touch the ring positions are regenerated with the new
// layout by tools/gen_select_wheel.py (select_wheel_gen.inc).
//
// A new cell is a real wheel cell whose slot byte is its HOST slot: the per-slot
// data kept in the save (last costume, 38 records of 4624 B) stays the host's, so
// the save format does not change. While the select logic runs for the player
// standing on a new cell, the slot tables answer with the new character (slot -> ID
// lists, portrait fids, name and icon images) and the host's "Custom" capsule list
// is swapped with the new character's own (roster_ext: mods/capsulas_custom.txt).

#include "roster_ext.h"

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/dbz3_init.h"
#include "eu_map.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dbz3::ingame {
bool OnSelectPicked(int player, int id);   // ingame/tagteam_panel.cpp
void AvisarEleccion(int player, int id);   // ingame/tagteam_panel.cpp (aviso a otros modos: Fighter's Road, R1)
void OnCapsuleChoice(int player, bool chosen, int option, const uint16_t* custom);
bool TagTeamMode();
}

// trazas de diagnostico (roster_trace.cpp, dbz1_diag_logging)
void dbz3_trace_sub_8217F3F0(PPCContext& ctx, uint8_t* base);
void dbz3_trace_sub_8217F520(PPCContext& ctx, uint8_t* base);
void dbz3_trace_sub_82180AA0(PPCContext& ctx, uint8_t* base);

namespace {

using dbz3::roster::GetVirtualCell;
using dbz3::roster::VirtualCellCount;

// Diagnostico (solo lectura): traza el cambio de ROTULO por alias. Se activa con la
// variable de entorno DBZ3_NAMES_DIAG=1 (mismo patron que DBZ3_TEST_VOCES). Nada de
// comportamiento cambia sin ella. Ver seguimiento_nombres/DIAGNOSTICO_NOMBRES2.md.
bool NamesDiag() {
  static const bool on = std::getenv("DBZ3_NAMES_DIAG") != nullptr;
  return on;
}

constexpr uint32_t kCells = 64;
constexpr uint32_t kCellBase = 44;
constexpr uint32_t kCellSize = 28;
constexpr uint32_t kPosBase = 1840;
constexpr uint32_t kRealSlots = 39;   // 38 personajes + aleatorio
constexpr uint32_t kRandomSlot = 38;
constexpr uint32_t kSlotToIdUs[3] = {0x82020618, 0x82021470, 0x82024760};
constexpr uint32_t kSlotToIdEu[3] = {0x82020620, 0x82021460, 0x82024778};
uint32_t SlotToId(int k) { return dbz3::GuestAddr(kSlotToIdUs[k], kSlotToIdEu[k]); }
constexpr dbz3::GA kPortraits = 0x82372818;

std::mutex g_mu;
// datos de rueda -> casilla nueva de cada celda (-1 = casilla original)
std::unordered_map<uint32_t, std::array<int8_t, kCells>> g_wheels;
// objetos rueda creados por sub_8217E6D8 (+48 = datos de la rueda)
std::unordered_map<uint32_t, uint32_t> g_wheel_objs;
std::unordered_map<uint32_t, uint64_t> g_wheel_base_masks, g_wheel_page_versions;
std::atomic<uint64_t> g_dynamic_page_version{0};
// objeto retrato -> casilla nueva que lo pidio (-1 = ninguna)
std::unordered_map<uint32_t, int> g_portrait_obj;

// Casillas nuevas que aparecen en una rueda: en los modos libres (la rueda tiene la
// casilla aleatoria, o casi todos los personajes: Practica y "1P VS COM" no la tienen
// si la partida no lo ha desbloqueado todo), siempre disponibles (el slot anfitrion solo
// aporta los datos guardados por slot, aunque no este desbloqueado). Ahi el rival sale
// de una lista que vuelve a escribir los IDs por casilla: el inicio del combate pone el
// elegido (SetPickedCell / PickedCellFor). Las ruedas del modo historia llevan pocos.
std::vector<int> NewCellsFor(uint64_t mask) {
  std::vector<int> out;
  uint32_t n = 0;
  for (uint32_t s = 0; s < kRealSlots; ++s) n += (mask >> s) & 1;
  // ponytail: "libre" = aleatoria o >= 20 casillas; si un modo de rueda corta las
  // necesitara, marcarlo por su hook en vez de contar casillas
  if (!((mask >> kRandomSlot) & 1) && n < 20) return out;
  for (int i = 0; i < VirtualCellCount() && n < kCells; ++i, ++n) out.push_back(i);
  return out;
}

bool GuestPtr(uint32_t p) { return p >= 0x40000000u && p < 0xA0000000u && !(p & 3); }

int NewCellOfWheel(uint8_t* base, uint32_t d) {
  std::lock_guard<std::mutex> lk(g_mu);
  auto it = g_wheels.find(d);
  if (it == g_wheels.end()) return -1;
  const uint32_t cur = REX_LOAD_U32(d + 40);
  if (cur < d + kCellBase) return -1;
  const uint32_t k = (cur - d - kCellBase) / kCellSize;
  return k < kCells ? it->second[k] : -1;
}

// per-player select state (+40 wheel object, whose +48 is the wheel data)
int NewCellOfState(uint8_t* base, uint32_t st) {
  if (!GuestPtr(st)) return -1;
  const uint32_t wobj = REX_LOAD_U32(st + 40);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_wheel_objs.count(wobj)) return -1;
  }
  return NewCellOfWheel(base, REX_LOAD_U32(wobj + 48));
}

// ---- imagenes propias (icono de la rueda / rotulo del nombre) ----
// Un descriptor de imagen del recurso de sprites del select (#ACA 2026) acaba en un
// bloque [textura del #AZT 2027, 3, 1.0f, 0, x0, y0, x1, y1] (rect logico) y lleva
// en +0x20 el quad centrado. Se clona una plantilla con otra textura/rect/quad:
// icono = imagen 61 (bloque en +0xB0), rotulo = imagen 105 (+0x60). El sprite del
// rotulo dibuja la textura ENTERA a su tamano logico, asi que cada rotulo nuevo es
// una textura propia anadida al #AZT 2027 por roster_build.py.
struct ImageTemplate {
  uint32_t img;
  uint32_t tex_block;
};
constexpr ImageTemplate kIconTpl{61, 0xB0};
constexpr ImageTemplate kNameTpl{105, 0x60};
constexpr uint32_t kIconImageBase = 61;
constexpr uint32_t kNameImageBase = 105;
constexpr uint32_t kImages = 145;
constexpr dbz3::GA kSelectGlobal = 0x8245F190;  // +16 = recurso de sprites del select
constexpr dbz3::GA kSpriteScale = 0x8201F60C;   // float que pasa sub_8217E090 (EU: 0x8201F604)

struct BuiltImage {
  uint32_t buf = 0;
  uint32_t cap = 0;
  uint32_t tpl = 0;
};
std::unordered_map<int, BuiltImage> g_images;  // clave: casilla * 2 + (0 icono / 1 rotulo)

uint32_t SpriteTable(uint8_t* base, uint32_t res) {
  return GuestPtr(res) ? REX_LOAD_U32(res + 28) : 0;
}

uint32_t BuildImage(uint8_t* base, uint32_t tbl, int vc, bool name) {
  const int32_t* r = name ? GetVirtualCell(vc).name : GetVirtualCell(vc).icon;
  if (r[0] < 0 || !GuestPtr(tbl)) return 0;
  const ImageTemplate t = name ? kNameTpl : kIconTpl;
  const uint32_t src = REX_LOAD_U32(tbl + 4 * t.img);
  if (!GuestPtr(src) || REX_LOAD_U32(src + t.tex_block + 4) != 3 ||
      REX_LOAD_U32(src + t.tex_block + 8) != 0x3F800000u) {
    return 0;
  }
  uint32_t n = 0x200;  // hasta el siguiente descriptor
  for (uint32_t i = 0; i < kImages; ++i) {
    const uint32_t q = REX_LOAD_U32(tbl + 4 * i);
    if (q > src && q - src < n) n = q - src;
  }
  auto& b = g_images[vc * 2 + (name ? 1 : 0)];
  if (b.tpl == src && b.buf) return b.buf;
  if (!b.buf || b.cap < n) {
    b.buf = dbz3::roster::GuestAlloc(0x200);
    b.cap = 0x200;
    if (!b.buf) return 0;
  }
  for (uint32_t o = 0; o + 4 <= n; o += 4) {
    uint32_t v = REX_LOAD_U32(src + o);
    if (v >= src && v < src + n) v = v - src + b.buf;  // punteros internos
    REX_STORE_U32(b.buf + o, v);
  }
  auto f = [](float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return u;
  };
  REX_STORE_U32(b.buf + t.tex_block, uint32_t(r[0]));
  for (int k = 0; k < 4; ++k) REX_STORE_U32(b.buf + t.tex_block + 0x10 + 4 * k, f(float(r[1 + k])));
  const float w = float(r[3] - r[1]) / 2, h = float(r[4] - r[2]) / 2;
  REX_STORE_U32(b.buf + 0x20, f(-w));
  REX_STORE_U32(b.buf + 0x24, f(-h));
  REX_STORE_U32(b.buf + 0x28, f(w));
  REX_STORE_U32(b.buf + 0x2C, f(h));
  b.tpl = src;
  return b.buf;
}

// ---- cambio de contexto: slot anfitrion -> personaje de la casilla nueva ----
thread_local int t_depth = 0;
thread_local int t_active = -1;
thread_local uint16_t t_saved_ids[3];
thread_local uint32_t t_name_slot = 0;   // entrada de la tabla de sprites cambiada
thread_local uint32_t t_name_saved = 0;
// lista "Custom" de capsulas (datos guardados por casilla: +68 + 14 * slot del bloque de
// la partida del jugador, estado +120): la del personaje nuevo en vez de la del anfitrion
thread_local uint32_t t_save_buf = 0;
thread_local uint32_t t_list_at = 0;
thread_local uint16_t t_host_list[7];
thread_local uint16_t t_own_list[7];
constexpr uint32_t kSaveCustom = 68;      // 38 x 7 x u16 por casilla
constexpr uint32_t kSaveInventory = 1094; // 2048 x u8 por capsula

void Restore(uint8_t* base) {
  if (t_active < 0) return;
  const auto& c = GetVirtualCell(t_active);
  if (t_list_at) {           // guarda lo editado (Edit Skills) y devuelve la del anfitrion
    uint16_t now[7];
    for (int k = 0; k < 7; ++k) now[k] = REX_LOAD_U16(t_list_at + 2 * k);
    if (std::memcmp(now, t_own_list, sizeof(now)) != 0) dbz3::roster::SetCustomList(c.id, now);
    for (int k = 0; k < 7; ++k) REX_STORE_U16(t_list_at + 2 * k, t_host_list[k]);
    t_list_at = 0;
  }
  for (int k = 0; k < 3; ++k) REX_STORE_U16(SlotToId(k) + 2 * c.alias, t_saved_ids[k]);
  if (t_name_slot) REX_STORE_U32(t_name_slot, t_name_saved);
  if (t_name_slot && NamesDiag())
    REXLOG_INFO("dbz3 names: RESTAURA casilla {} (alias {} rotulo {}) entrada {} deja el nativo",
                t_active, c.alias, GetVirtualCell(t_active).name[0], kNameImageBase + c.alias);
  t_name_slot = 0;
  t_active = -1;
}

// Diagnostico (solo con DBZ3_NAMES_DIAG): resuelve la textura del rotulo y del icono en el banco
// global como lo hace sub_82096FF0 (k = tex - bank[+24]; off = bank[bank[+20] + 4k]; obj = bank + off + 20).
void DiagBank(uint8_t* base, int vc) {
  const uint32_t bank = REX_LOAD_U32(dbz3::GA(0x8238CE10));   // banco global (-12784 de sub_82092C58)
  if (!GuestPtr(bank)) {
    REXLOG_INFO("dbz3 names: BANCO global nulo (casilla {})", vc);
    return;
  }
  // el indice NO esta en el banco: sub_82096FF0 lo lee del global 0x8238CE14 (-12780)
  const uint32_t n = REX_LOAD_U32(bank + 16), idx = REX_LOAD_U32(dbz3::GA(0x8238CE14)), first = REX_LOAD_U32(bank + 24);
  const auto& c = GetVirtualCell(vc);
  for (int which = 0; which < 2; ++which) {
    const uint32_t tex = uint32_t(which == 0 ? c.name[0] : c.icon[0]);
    const uint32_t k = tex - first;
    const uint32_t off = GuestPtr(idx) && k < n ? REX_LOAD_U32(idx + 4 * k) : 0;
    const uint32_t obj = off ? REX_LOAD_U32(bank + off + 20) : 0;
    REXLOG_INFO("dbz3 names: BANCO casilla {} {} tex {} k {} n {} idx {:08X} off {:08X} obj {:08X} bank {:08X}",
                vc, which ? "icono" : "rotulo", tex, k, n, idx, off, obj, bank);
  }
}

void Apply(uint8_t* base, int vc) {
  if (vc == t_active) return;
  Restore(base);
  if (vc < 0) return;
  const auto& c = GetVirtualCell(vc);
  for (int k = 0; k < 3; ++k) {
    t_saved_ids[k] = REX_LOAD_U16(SlotToId(k) + 2 * c.alias);
    REX_STORE_U16(SlotToId(k) + 2 * c.alias, uint16_t(c.id));
  }
  if (GuestPtr(t_save_buf) && c.alias < kRandomSlot) {
    t_list_at = t_save_buf + kSaveCustom + 14 * c.alias;
    dbz3::roster::GetCustomList(c.id, t_own_list);
    for (int k = 0; k < 7; ++k) {
      t_host_list[k] = REX_LOAD_U16(t_list_at + 2 * k);
      REX_STORE_U16(t_list_at + 2 * k, t_own_list[k]);
    }
    dbz3::roster::OwnNewCapsules(t_save_buf + kSaveInventory);   // sus capsulas, siempre tuyas
  }
  const uint32_t sel = REX_LOAD_U32(kSelectGlobal);
  const uint32_t tbl = GuestPtr(sel) ? SpriteTable(base, REX_LOAD_U32(sel + 16)) : 0;
  if (const uint32_t desc = BuildImage(base, tbl, vc, true)) {
    t_name_slot = tbl + 4 * (kNameImageBase + c.alias);
    t_name_saved = REX_LOAD_U32(t_name_slot);
    REX_STORE_U32(t_name_slot, desc);
  }
  if (NamesDiag())
    REXLOG_INFO("dbz3 names: APLICA casilla {} id {} alias {} rotulo {} -> entrada tbl[{}] {}",
                vc, c.id, c.alias, GetVirtualCell(vc).name[0], kNameImageBase + c.alias,
                t_name_slot ? "cambiada" : "SIN ROTULO (BuildImage=0)");
  if (NamesDiag() && t_name_slot && GuestPtr(t_name_saved))
    REXLOG_INFO("dbz3 names: NATIVO casilla {} alias {} tex nativa {} (bloque {:08X} {:08X})", vc, c.alias,
                REX_LOAD_U32(t_name_saved + kNameTpl.tex_block), REX_LOAD_U32(t_name_saved + kNameTpl.tex_block + 4),
                REX_LOAD_U32(t_name_saved + kNameTpl.tex_block + 8));
  if (NamesDiag()) DiagBank(base, vc);
  t_active = vc;
}

// r3 = task object; +48 = player state, or (nested: sub_82182210 / 82182930) a
// confirm-task state whose +16 is the player state.
// Ultima vez que corrio la logica de un jugador de la rueda (la seleccion de personajes comparte
// el byte de pantalla con el menu de Duelo, 14): lo usan las pantallas propias del port.
std::atomic<int64_t> g_select_seen_ms{0};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Enter(uint8_t* base, uint32_t obj, bool nested) {
  g_select_seen_ms = NowMs();
  if (t_depth++ == 0 && VirtualCellCount() > 0 && GuestPtr(obj)) {
    uint32_t st = REX_LOAD_U32(obj + 48);
    if (NamesDiag() && !nested && GuestPtr(st)) {
      static thread_local int last = -1;
      const int slot = int(int8_t(REX_LOAD_U8(st + 4)));
      if (slot != last) {
        last = slot;
        REXLOG_INFO("dbz3 names: menu slot(state+4)={} (activa {} alias {} rotulo {})", slot, t_active,
                    t_active >= 0 ? GetVirtualCell(t_active).alias : -1,
                    t_active >= 0 ? GetVirtualCell(t_active).name[0] : -1);
      }
    }
    if (nested && GuestPtr(st)) st = REX_LOAD_U32(st + 16);
    t_save_buf = GuestPtr(st) ? REX_LOAD_U32(st + 120) : 0;
    Apply(base, NewCellOfState(base, st));
  }
}

void Leave(uint8_t* base) {
  if (--t_depth == 0) Restore(base);
}

}  // namespace

// Casilla nueva activa para el jugador cuyo estado del select se esta ejecutando en este
// hilo (-1 = casilla original). La usa el cambio de traje (roster_ext, sub_8217A5A0).
int dbz3_select_active_cell() { return t_active; }
bool dbz3_select_live() { return NowMs() - g_select_seen_ms.load() < 300; }

// Por jugador de la rueda: ultima vez que estaba eligiendo (estado sub_8217CC68 sin confirmar) y
// si ya confirmo. Con los dos confirmados lo siguiente es la eleccion de escenario, que comparte
// la pantalla 14 con el menu de Duelo.
namespace {
std::atomic<int64_t> g_browse_ms[2] = {0, 0};
std::atomic<uint8_t> g_confirmed{0};   // bit por jugador
}  // namespace
int64_t dbz3_select_browse_ms(int k) { return k == 0 || k == 1 ? g_browse_ms[k].load() : 0; }
// Menu de capsulas abierto (Normal / Custom / Edit Skills, sub_8217CB10): opcion resaltada del jugador
// k, o -1 si su menu no esta abierto ahora.
namespace {
std::atomic<int> g_cap_opt[2] = {-1, -1};
std::atomic<int64_t> g_cap_ms[2] = {0, 0};
}  // namespace
int dbz3_select_capsule_option(int k) {
  return (k == 0 || k == 1) && NowMs() - g_cap_ms[k].load() < 200 ? g_cap_opt[k].load() : -1;
}
int64_t dbz3_select_now_ms() { return NowMs(); }
bool dbz3_select_to_stage() { return g_confirmed.load() == 3; }
void dbz3_select_reset_flow() { g_confirmed = 0; }

#include "select_wheel_gen.inc"
#if defined(DBZ3_DUAL_REGION)
#include "select_wheel_gen_eu.inc"
#endif
#define DBZ3_WHEEL(a) DBZ3_CALL2(Wheel_##a, WheelEu_##a)

// Guest functions called directly below (US / EU/PAL twin).
DBZ3_EXTERN_EU(dbz3eu_sub_8217D8F8);
DBZ3_EXTERN_EU(dbz3eu_sub_8217D6C8);
DBZ3_EXTERN_EU(dbz3eu_sub_8217DFE8);
DBZ3_EXTERN_EU(dbz3eu_sub_8217E048);
DBZ3_EXTERN_EU(dbz3eu_sub_8217DE38);
DBZ3_EXTERN_EU(dbz3eu_sub_8217D828);
DBZ3_EXTERN_EU(dbz3eu_sub_820ACC80);
DBZ3_EXTERN_EU(dbz3eu_sub_820B0670);

DBZ3_HOOK(sub_8217D710, dbz3eu_sub_8217D6C8) { DBZ3_WHEEL(8217D710)(ctx, base); }
DBZ3_HOOK(sub_8217DB20, dbz3eu_sub_8217DAD8) { DBZ3_WHEEL(8217DB20)(ctx, base); }
DBZ3_HOOK(sub_8217DC38, dbz3eu_sub_8217DBF0) { DBZ3_WHEEL(8217DC38)(ctx, base); }
DBZ3_HOOK(sub_8217E410, dbz3eu_sub_8217E3C8) { DBZ3_WHEEL(8217E410)(ctx, base); }
// Creates a wheel object (returns it in r3; +48 = wheel data, 2368 B here).
DBZ3_HOOK(sub_8217E6D8, dbz3eu_sub_8217E690) {
  DBZ3_WHEEL(8217E6D8)(ctx, base);
  std::lock_guard<std::mutex> lk(g_mu);
  if (ctx.r3.u32) {
    g_wheel_objs[ctx.r3.u32] = REX_LOAD_U32(ctx.r3.u32 + 48);
    g_wheel_base_masks.erase(ctx.r3.u32);
    g_wheel_page_versions.erase(ctx.r3.u32);
  }
}

// Puts the cursor on the FIRST cell of slot r4 (r3 = wheel data); the select
// does it when a player confirms. A cursor on a new cell of that host slot stays
// on it (the host's own cells are hidden from the search).
DBZ3_HOOK(sub_8217DE80, dbz3eu_sub_8217DE38) {
  const uint32_t d = ctx.r3.u32;
  const int vc = NewCellOfWheel(base, d);
  uint32_t hidden[kCells];
  uint32_t nh = 0;
  uint8_t alias = 0;
  if (vc >= 0 && int8_t(ctx.r4.s32) == int8_t(GetVirtualCell(vc).alias)) {
    alias = uint8_t(GetVirtualCell(vc).alias);
    const uint32_t cur = REX_LOAD_U32(d + 40);
    for (uint32_t at = d + kCellBase + 2; at < cur && nh < kCells; at += kCellSize) {
      if (REX_LOAD_U8(at) == alias) {
        REX_STORE_U8(at, 0x7F);
        hidden[nh++] = at;
      }
    }
  }
  DBZ3_WHEEL(8217DE80)(ctx, base);
  for (uint32_t k = 0; k < nh; ++k) REX_STORE_U8(hidden[k], alias);
}

// Rebuilds the wheel for a slot mask (r3 = wheel object, r4 = u64 mask).
DBZ3_HOOK(sub_8217E490, dbz3eu_sub_8217E448) {
  const uint32_t obj = ctx.r3.u32;
  const uint64_t mask = ctx.r4.u64;
  if (!obj) return;
  const uint32_t d = REX_LOAD_U32(obj + 48);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_wheel_base_masks.count(obj))
      g_wheel_base_masks[obj] = mask & ~dbz3::roster::ExtraUnlockMask();
  }
  int8_t cur_slot = 0;
  if (const uint32_t cur = REX_LOAD_U32(d + 40)) cur_slot = int8_t(REX_LOAD_U8(cur + 2));
  const int cur_vc = NewCellOfWheel(base, d);
  REX_STORE_U64(d + 16, mask);
  uint32_t n = 0;
  for (uint32_t s = 0; s < kRealSlots; ++s) n += (mask >> s) & 1;
  n += uint32_t(NewCellsFor(mask).size());
  REX_STORE_U8(d + 12, uint8_t(n));
  ctx.r3.u64 = d;
  DBZ3_CALL2(sub_8217D940, dbz3eu_sub_8217D8F8)(ctx, base);
  REX_STORE_U32(d + 40, 0);
  std::memset(base + d + kCellBase, 0, kCells * kCellSize);
  std::memset(base + d + kPosBase, 0, kCells * 8);
  ctx.r3.u64 = d;
  DBZ3_CALL2(sub_8217D710, dbz3eu_sub_8217D6C8)(ctx, base);
  ctx.r3.u64 = d;
  DBZ3_CALL2(sub_8217E030, dbz3eu_sub_8217DFE8)(ctx, base);
  ctx.r3.u64 = d;
  DBZ3_CALL2(sub_8217E090, dbz3eu_sub_8217E048)(ctx, base);
  if (cur_vc >= 0) {  // seguir en la casilla nueva tras reconstruir
    std::lock_guard<std::mutex> lk(g_mu);
    const auto& map = g_wheels[d];
    for (uint32_t k = 0; k < kCells; ++k) {
      if (map[k] == cur_vc) REX_STORE_U32(d + 40, d + kCellBase + kCellSize * k);
    }
  }
  ctx.r3.u64 = d;
  ctx.r4.s64 = cur_slot;
  DBZ3_CALL2(sub_8217DE80, dbz3eu_sub_8217DE38)(ctx, base);
  ctx.r3.u64 = d;
  DBZ3_CALL2(sub_8217D870, dbz3eu_sub_8217D828)(ctx, base);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_wheel_page_versions[obj] = g_dynamic_page_version.load();
  }
}

void RefreshDynamicWheelForState(PPCContext& ctx, uint8_t* base, bool nested) {
  const uint32_t obj = ctx.r3.u32;
  if (!GuestPtr(obj)) return;
  uint32_t state = REX_LOAD_U32(obj + 48);
  if (!GuestPtr(state)) return;
  if (nested) state = REX_LOAD_U32(state + 16);
  if (!GuestPtr(state)) return;
  const uint32_t wheel = REX_LOAD_U32(state + 40);
  if (!GuestPtr(wheel)) return;
  uint64_t base_mask = 0;
  const uint64_t generation = g_dynamic_page_version.load();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    const auto it = g_wheel_base_masks.find(wheel);
    if (!g_wheel_objs.count(wheel) || it == g_wheel_base_masks.end() ||
        g_wheel_page_versions[wheel] == generation) return;
    base_mask = it->second;
  }
  const PPCContext saved = ctx;
  ctx.r3.u64 = wheel;
  ctx.r4.u64 = base_mask | dbz3::roster::ExtraUnlockMask();
  DBZ3_CALL2(sub_8217E490, dbz3eu_sub_8217E448)(ctx, base);
  ctx = saved;
}

// Assigns a slot to every cell (r3 = wheel data): available slots in order, each
// new cell right after its `after` slot (else at the end), then the random cell.
DBZ3_HOOK(sub_8217E030, dbz3eu_sub_8217DFE8) {
  const uint32_t d = ctx.r3.u32;
  dbz3::roster::ClearPickedCells();     // select nuevo: nada elegido aun
  const uint64_t mask = REX_LOAD_U64(d + 16);
  std::array<int8_t, kCells> map;
  map.fill(-1);
  uint32_t n = 0;
  auto add = [&](uint32_t slot, int vc) {
    if (n >= kCells) return;
    REX_STORE_U8(d + kCellBase + kCellSize * n + 2, uint8_t(slot));
    map[n++] = int8_t(vc);
  };
  const std::vector<int> news = NewCellsFor(mask);
  std::vector<bool> placed(news.size(), false);
  for (uint32_t s = 0; s < kRandomSlot; ++s) {
    if (!((mask >> s) & 1)) continue;
    add(s, -1);
    for (size_t i = 0; i < news.size(); ++i) {  // casillas nuevas que van tras esta
      if (!placed[i] && GetVirtualCell(news[i]).after == s) {
        add(GetVirtualCell(news[i]).alias, news[i]);
        placed[i] = true;
      }
    }
  }
  for (size_t i = 0; i < news.size(); ++i) {
    if (!placed[i]) add(GetVirtualCell(news[i]).alias, news[i]);
  }
  if ((mask >> kRandomSlot) & 1) add(kRandomSlot, -1);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_wheels[d] = map;
  }
  REX_STORE_U32(d + 40, d + kCellBase);
  DBZ3_CALL2(sub_8217D870, dbz3eu_sub_8217D828)(ctx, base);
}

// Frees the icon / frame sprites of every cell (r3 = wheel data).
DBZ3_HOOK(sub_8217D940, dbz3eu_sub_8217D8F8) {
  const uint32_t d = ctx.r3.u32;
  for (uint32_t k = 0; k < kCells; ++k) {
    for (uint32_t off : {4u, 8u}) {
      const uint32_t at = d + kCellBase + kCellSize * k + off;
      if (const uint32_t sprite = REX_LOAD_U32(at)) {
        ctx.r3.u64 = sprite;
        ctx.r4.u64 = 1;
        DBZ3_CALL2(sub_820ACCF0, dbz3eu_sub_820ACC80)(ctx, base);
        REX_STORE_U32(at, 0);
      }
    }
  }
}

// Cursor changed (r3 = wheel data): copies the cell's per-slot data to the player
// block. Inside a player's select logic, re-evaluate the new-cell context.
DBZ3_HOOK(sub_8217D870, dbz3eu_sub_8217D828) {
  const uint32_t d = ctx.r3.u32;
  orig(ctx, base);
  if (t_depth > 0) Apply(base, NewCellOfWheel(base, d));
}

// Select data -> save (r3 = select data: +68 custom capsules per slot, +1094 inventory;
// called through a pointer): while a new character's own list is swapped in, the save
// must get the host slot's list.
DBZ3_HOOK(sub_82179880, dbz3eu_sub_82179838) {
  const bool swapped = t_list_at && ctx.r3.u32 == t_save_buf;
  uint16_t own[7];
  if (swapped) {
    for (int k = 0; k < 7; ++k) {
      own[k] = REX_LOAD_U16(t_list_at + 2 * k);
      REX_STORE_U16(t_list_at + 2 * k, t_host_list[k]);
    }
  }
  orig(ctx, base);
  if (swapped) for (int k = 0; k < 7; ++k) REX_STORE_U16(t_list_at + 2 * k, own[k]);
}

// Per-player select states (r3 = task object, +48 = player state).
void RefreshDynamicWheelForState(PPCContext& ctx, uint8_t* base, bool nested);
#define DBZ3_SELECT_PLAYER_HANDLER(fn, eu, nested) \
  DBZ3_HOOK(fn, eu) {                              \
    if (!(nested)) {                               \
      uint64_t removed = 0, added = 0;              \
      if (dbz3::roster::ProcessDynamicRosterPage(removed, added)) ++g_dynamic_page_version; \
    }                                              \
    RefreshDynamicWheelForState(ctx, base, nested); \
    Enter(base, ctx.r3.u32, nested);               \
    orig(ctx, base);                               \
    Leave(base);                                   \
  }
DBZ3_SELECT_PLAYER_HANDLER(sub_8217CFC8, dbz3eu_sub_8217CF80, false)
DBZ3_SELECT_PLAYER_HANDLER(sub_82180E90, dbz3eu_sub_82180E48, false)
DBZ3_SELECT_PLAYER_HANDLER(sub_82182210, dbz3eu_sub_821821C8, true)
DBZ3_SELECT_PLAYER_HANDLER(sub_82182930, dbz3eu_sub_821828E8, true)
#undef DBZ3_SELECT_PLAYER_HANDLER

// Tras confirmar el personaje: menu de capsulas del jugador (estado +48: +2 resultado, 1 elegido y -1
// atras; +7 jugador; +10 tipo de menu, 2-4 el de opciones; +12 opcion, 0 Normal, 1 Custom, 2 Edit Skills; +4
// casilla; +120 copia de la partida, listas Custom en +68 + 14*casilla; estudio 2026-10-12). En
// TAG TEAM cada miembro lleva lo que se elige aqui (tagteam_panel.cpp). Dentro de Enter: con una
// casilla nueva la lista de la copia ya es la del personaje nuevo.
DBZ3_HOOK(sub_8217CB10, dbz3eu_sub_8217CAC8) {
  Enter(base, ctx.r3.u32, false);
  const uint32_t tobj = ctx.r3.u32;
  const uint32_t tst = GuestPtr(tobj) ? REX_LOAD_U32(tobj + 48) : 0;
  const int8_t before = tst && GuestPtr(tst) ? int8_t(REX_LOAD_U8(tst + 2)) : 0;
  orig(ctx, base);
  if (tst && GuestPtr(tst) && dbz3::ingame::TagTeamMode()) {
    // opcion resaltada del menu de opciones (tipo 2-4: objeto en +52, su interior en +48, opcion en +0)
    const uint8_t pk = REX_LOAD_U8(tst + 7), pkind = REX_LOAD_U8(tst + 10);
    const uint32_t w = REX_LOAD_U32(tst + 52);
    const uint32_t inner = GuestPtr(w) ? REX_LOAD_U32(w + 48) : 0;
    if (pk < 2 && pkind >= 2 && pkind <= 4 && GuestPtr(inner)) {
      if (NowMs() - g_cap_ms[pk].load() > 1000)
        REXLOG_INFO("dbz3 tag: menu de capsulas del jugador {} abierto (estado {:08X}, menu {:08X}, interior {:08X})", pk + 1, tst,
                    w, inner);
      const int opt = REX_LOAD_U8(inner);   // medido 2026-10-12: 0 Normal, 1 Custom, 2 Edit Skills (igual en estado +12)
      if (opt != g_cap_opt[pk].load()) REXLOG_INFO("dbz3 tag: menu de capsulas del jugador {}: opcion {}", pk + 1, opt);
      g_cap_opt[pk] = opt;
      g_cap_ms[pk] = NowMs();
    }
    const int8_t after = int8_t(REX_LOAD_U8(tst + 2));
    // elegido: +2 pasa a 1; atras: la tarea vuelve a la rueda (sub_8217CC68). +2 = -1 no basta: puede
    // valer ya -1 de un atras anterior (2026-10-12, el atras no llegaba)
    const bool back = REX_LOAD_U32(tobj + 28) == dbz3::GA(0x8217CC68u) && after != 1;
    if ((after != before && after == 1) || back) {
      const uint8_t kind = REX_LOAD_U8(tst + 10), option = REX_LOAD_U8(tst + 12);
      const int8_t cell = int8_t(REX_LOAD_U8(tst + 4));
      const uint32_t buf = REX_LOAD_U32(tst + 120);
      uint16_t custom[7];
      std::fill(custom, custom + 7, uint16_t(0xFFFF));
      if (GuestPtr(buf) && cell >= 0 && cell < 38) {
        for (uint32_t k = 0; k < 7; ++k) custom[k] = REX_LOAD_U16(buf + 68 + 14 * uint32_t(cell) + 2 * k);
      }
      REXLOG_INFO("dbz3 tag: menu de capsulas del jugador {}: {} (menu {}, opcion {}, casilla {})", REX_LOAD_U8(tst + 7) + 1,
                  back ? "atras" : "elegido", kind, option, cell);
      dbz3::ingame::OnCapsuleChoice(REX_LOAD_U8(tst + 7), !back, kind >= 2 && kind <= 4 ? option : 0, custom);
    }
  }
  Leave(base);
}

void dbz3_select_refresh_preview(PPCContext& ctx, uint8_t* base);   // roster_ext.cpp

// Confirmar personaje (sub_8217ADE8 escribe el ID del jugador en 0x824BA12F + jugador
// desde las tablas casilla -> ID, cambiadas mientras esta en una casilla nueva): se
// recuerda la eleccion para el inicio del combate (dbz3::roster::PickedCellFor).
DBZ3_HOOK(sub_8217CC68, dbz3eu_sub_8217CC20) {
  dbz3_select_refresh_preview(ctx, base);   // LT/RT: la vista previa en la forma elegida
  constexpr dbz3::GA kPicked = 0x824BA12F;   // EU 0x824BA11F (tools/eu_map.py)
  uint8_t before[2] = {REX_LOAD_U8(kPicked), REX_LOAD_U8(kPicked + 1)};
  Enter(base, ctx.r3.u32, false);
  const int vc = t_active;
  // confirmar = la tarea pasa de este estado (la rueda) al siguiente; jugador en st+7
  const uint32_t tobj = ctx.r3.u32, tst = GuestPtr(tobj) ? REX_LOAD_U32(tobj + 48) : 0;
  const uint32_t handler = GuestPtr(tobj) ? REX_LOAD_U32(tobj + 28) : 0;
  orig(ctx, base);
  if (tst && GuestPtr(tst) && REX_LOAD_U8(tst + 7) < 2) {
    const uint8_t k = REX_LOAD_U8(tst + 7);
    if (REX_LOAD_U32(tobj + 28) == handler) {
      g_browse_ms[k] = NowMs();
      g_confirmed &= uint8_t(~(1u << k));
    } else {
      g_confirmed |= uint8_t(1u << k);
    }
  }
  if (tst && GuestPtr(tst) && REX_LOAD_U32(tobj + 28) != handler && REX_LOAD_U8(tst + 7) < 2) {   // aviso (R1)
    dbz3::ingame::AvisarEleccion(REX_LOAD_U8(tst + 7), REX_LOAD_U8(kPicked + REX_LOAD_U8(tst + 7)));
  }
  if (tst && GuestPtr(tst) && REX_LOAD_U32(tobj + 28) != handler && dbz3::ingame::TagTeamMode()) {
    const uint8_t k = REX_LOAD_U8(tst + 7);
    if (k < 2) {
      const uint8_t id = REX_LOAD_U8(kPicked + k);
      if (dbz3::ingame::OnSelectPicked(k, id)) {   // TAG TEAM: miembro elegido, sigue en la rueda
        Leave(base);
        return;
      }
    }
  }
  for (int k = 0; k < 2; ++k) {
    const uint8_t now = REX_LOAD_U8(kPicked + k);
    if (now == before[k]) continue;
    if (vc >= 0 && now == GetVirtualCell(vc).id) {
      dbz3::roster::SetPickedCell(k, int(now), int(t_saved_ids[0]));
    } else {
      dbz3::roster::SetPickedCell(k, -1, -1);
    }
  }
  Leave(base);
}

DBZ3_HOOK(sub_82180AA0, dbz3eu_sub_82180A58) {
  dbz3_trace_sub_82180AA0(ctx, base);
  Enter(base, ctx.r3.u32, false);
  orig(ctx, base);
  Leave(base);
}

// Portrait request (r3 = portrait object, r4 = slot): remember which new cell
// asked for it; the portrait loads later in its own task (sub_8217F3F0).
DBZ3_HOOK(sub_8217F520, dbz3eu_sub_8217F4D8) {
  dbz3_trace_sub_8217F520(ctx, base);
  if (ctx.r3.u32 && VirtualCellCount() > 0) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_portrait_obj[ctx.r3.u32] = t_active;
  }
  orig(ctx, base);
}

// Portrait load: fid = table[slot][variant] (u32 [39][2]).
DBZ3_HOOK(sub_8217F3F0, dbz3eu_sub_8217F3A8) {
  dbz3_trace_sub_8217F3F0(ctx, base);
  int vc = -1;
  if (VirtualCellCount() > 0) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_portrait_obj.find(ctx.r3.u32);
    if (it != g_portrait_obj.end()) vc = it->second;
  }
  if (vc < 0 || GetVirtualCell(vc).portrait[0] == 0xFFFFFFFFu) {
    orig(ctx, base);
    return;
  }
  const auto& c = GetVirtualCell(vc);
  const uint32_t at = kPortraits + 8 * c.alias;
  const uint32_t old0 = REX_LOAD_U32(at), old1 = REX_LOAD_U32(at + 4);
  REX_STORE_U32(at, c.portrait[0]);
  REX_STORE_U32(at + 4, c.portrait[1]);
  orig(ctx, base);
  REX_STORE_U32(at, old0);
  REX_STORE_U32(at + 4, old1);
}

// ---- Bloque H (studies 8): rotulos de las casillas nuevas ----
// sub_82092C80(r3 = etapa 0/1, r4 = textura) lo llama el dibujo de cada sprite (sub_82097290 / sub_820959B0) y
// resuelve la textura en el banco GLOBAL 0x8238CE10 con sub_82096FF0(*(0x82386260), etapa, textura).
// MEDIDO 2026-10-19 (DBZ3_NAMES_DIAG, gancho de aqui): al DIBUJAR el rotulo el banco es el #AZT del select (data_usi
// 2027 del mod _roster, 0x4770AEA0, n = 88) y la textura 65..87 SI esta (entrada valida, objeto con vtable
// 8201CAA0, pixeles correctos en memoria fisica). Lo que lo borraba es la guarda de sub_82096FF0 en campaigns.cpp
// (cierres del Tag Team / R1): descarta toda entrada con desplazamiento >= 16 MB ("efecto 73 que no esta en el banco
// 4770AEA0 ... sin efecto"), y roster_build.py anade los rotulos AL FINAL del #AZT de 17 MB (entradas en
// +0x01047980..). Aqui, solo para una entrada que es de verdad una textura del banco (indice propio == textura,
// recurso con la vtable de texturas y su metodo [1] en codigo), se llama a sub_82096FF0 ORIGINAL; el resto sigue por
// el camino de siempre (con la guarda).
DBZ3_EXTERN_EU(__imp__dbz3eu_sub_82096F88);
namespace {
constexpr dbz3::GA kTexVtable = 0x8201CAA0;   // clase de las texturas del #AZT (EU 0x8201CA94: la direccion cambia, S5-I1)

// Entrada del banco global para la textura `tex` si es una textura real mas alla de lo que admite la guarda (0 si no).
uint32_t EntradaLejana(uint8_t* base, uint32_t tex) {
  const uint32_t bank = REX_LOAD_U32(dbz3::GA(0x8238CE10));
  if (!GuestPtr(bank) || tex == 0xFFFFFFFFu) return 0;
  const uint32_t n = REX_LOAD_U32(bank + 16), k = tex - REX_LOAD_U32(bank + 24);
  const uint32_t idx = REX_LOAD_U32(dbz3::GA(0x8238CE14));
  if (k >= n || n > 4096 || !GuestPtr(idx)) return 0;
  const uint32_t rel = REX_LOAD_U32(idx + 4 * k);
  if (rel < 0x01000000u || rel >= 0x08000000u) return 0;   // < 16 MB: lo resuelve el camino normal
  const uint32_t e = bank + rel;
  if (NamesDiag() && tex >= 70 && tex < 100)   // S5-I1: por que una textura lejana no se resuelve (Torneo EU)
    REXLOG_INFO("dbz3 names: LEJANA tex {} rel {:08X} e {:08X} e[0] {} res {:08X} vt {:08X}", tex, rel, e,
                GuestPtr(e) ? REX_LOAD_U32(e) : 0, GuestPtr(e) ? REX_LOAD_U32(e + 20) : 0,
                GuestPtr(e) && GuestPtr(REX_LOAD_U32(e + 20)) ? REX_LOAD_U32(REX_LOAD_U32(e + 20)) : 0);
  if (!GuestPtr(e) || REX_LOAD_U32(e) != tex) return 0;    // la entrada lleva su propio indice
  const uint32_t res = REX_LOAD_U32(e + 20);
  if (!GuestPtr(res) || REX_LOAD_U32(res) != uint32_t(kTexVtable)) return 0;
  const uint32_t m1 = REX_LOAD_U32(REX_LOAD_U32(res) + 4);
  if (m1 < 0x82000000u || m1 >= 0x83000000u || (m1 & 3)) return 0;
  return e;
}
}  // namespace

DBZ3_HOOK(sub_82092C80, dbz3eu_sub_82092BC8) {
  const uint32_t tex = ctx.r4.u32;
  const uint32_t e = EntradaLejana(base, tex);
  if (NamesDiag()) {   // una linea por (banco, textura) dibujada: banco, n y si resuelve
    static std::mutex mu;
    static std::unordered_map<uint64_t, int> seen;
    const uint32_t bank = REX_LOAD_U32(dbz3::GA(0x8238CE10));
    bool log = false;
    {
      std::lock_guard<std::mutex> lk(mu);
      log = seen.size() < 600 && seen.emplace((uint64_t(bank) << 32) | tex, 1).second;
    }
    if (log) {
      const bool ok = GuestPtr(bank);
      const uint32_t n = ok ? REX_LOAD_U32(bank + 16) : 0, k = tex - (ok ? REX_LOAD_U32(bank + 24) : 0);
      const uint32_t idx = REX_LOAD_U32(dbz3::GA(0x8238CE14));
      const uint32_t off = ok && GuestPtr(idx) && k < n ? REX_LOAD_U32(idx + 4 * k) : 0;
      REXLOG_INFO("dbz3 names: DIBUJO tex {} etapa {} bank {:08X} n {} idx {:08X} off {:08X} obj {:08X} desde {:08X} {}", tex,
                  ctx.r3.u32, bank, n, idx, off, off ? REX_LOAD_U32(bank + off + 20) : 0, uint32_t(ctx.lr),
                  e ? "LEJANA (sin guarda)" : "normal");
    }
  }
  if (!e) {
    orig(ctx, base);
    return;
  }
  // cuerpo de sub_82092C80 con la llamada a sub_82096FF0 original
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = REX_LOAD_U32(dbz3::GA(0x82386260));
  DBZ3_IMP_EU(sub_82096FF0)(ctx, base);
  ctx.r3.u64 = 0;
}

// Creates the icon / frame sprites of every cell (r3 = wheel data); new cells
// get their own icon image.
DBZ3_HOOK(sub_8217E090, dbz3eu_sub_8217E048) {
  const uint32_t d = ctx.r3.u32;
  orig(ctx, base);
  if (VirtualCellCount() == 0) return;
  std::array<int8_t, kCells> map;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_wheels.find(d);
    if (it == g_wheels.end()) return;
    map = it->second;
  }
  const uint32_t tbl = SpriteTable(base, REX_LOAD_U32(d));
  const uint32_t n = std::min<uint32_t>(REX_LOAD_U8(d + 12), kCells);
  for (uint32_t k = 0; k < n; ++k) {
    if (map[k] < 0) continue;
    const uint32_t desc = BuildImage(base, tbl, map[k], false);
    if (!desc) continue;
    const uint32_t at = d + kCellBase + kCellSize * k + 4;
    if (const uint32_t old = REX_LOAD_U32(at)) {
      ctx.r3.u64 = old;
      ctx.r4.u64 = 1;
      DBZ3_CALL2(sub_820ACCF0, dbz3eu_sub_820ACC80)(ctx, base);
    }
    const uint32_t scale = REX_LOAD_U32(dbz3::GuestAddr(kSpriteScale, 0x8201F604));
    float fs;
    std::memcpy(&fs, &scale, 4);
    ctx.f1.f64 = double(fs);
    ctx.r3.u64 = desc;
    DBZ3_CALL2(sub_820B06E0, dbz3eu_sub_820B0670)(ctx, base);
    REX_STORE_U32(at, ctx.r3.u32);
  }
}
