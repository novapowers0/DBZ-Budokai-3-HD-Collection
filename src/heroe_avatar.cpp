// R16 HEROE-AVATAR: el mismo personaje montado para combate, en mapa y nivel.
// Solo US: las campanas virtuales del Heroe todavia no tienen pareja EU.
// No se cambia la casilla de historia ni se guardan modelos/trajes en la partida.
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/dbz3_init.h"
#include "guest_region.h"
#include "heroe_api.h"
#include "roster_ext.h"

namespace dbz3::rpg { int EsferasDeCasilla(int casilla); }

namespace {
constexpr uint32_t kMap = 0x82479A60;
constexpr uint32_t kAnims = 0x823745C0;
thread_local int level_hero = -1;

// La casilla, la ficha activa y el roster tienen que coincidir. Un CaC ajeno o
// una exportacion pendiente de reiniciar no sustituyen al personaje nativo.
int Hero(uint8_t* base, uint32_t slot) {
  if (dbz3::g_guest_eu || slot < 35 || slot > 37 || REX_LOAD_U8(0x824A58A0) != slot ||
      dbz3::rpg::EsferasDeCasilla(int(slot)) < 0 || !dbz3::heroe::HistoriaMontada()) return -1;
  const int id = dbz3::heroe::IdPropio();
  static const bool diagnostic = std::getenv("DBZ3_TEST_AVATAR_DIAG") != nullptr;
  if (diagnostic) REXLOG_INFO("dbz3 heroe avatar: ficha esperada ID {}: {}", id, dbz3::heroe::ArgsPropio());
  return id >= 0 && id < 64 && dbz3::roster::DonorOf(uint32_t(id)) >= 0 ? id : -1;
}

struct MapAnimation {
  uint32_t address = 0, original = 0;
  void Restore(uint8_t* base) {
    if (address) REX_STORE_U32(address, original);
    address = 0;
  }
  void Apply(uint8_t* base, uint32_t slot, int id) {
    Restore(base);
    const int donor = dbz3::roster::DonorOf(uint32_t(id));
    uint32_t animation = donor >= 0 && donor < 44 ? REX_LOAD_U16(0x82020668u + uint32_t(donor) * 2) : 0;
    // La casilla 22 es Freezer sentado en su nave. Goku es el vuelo de respaldo.
    if (animation >= 35 || animation == 22 || !REX_LOAD_U32(kAnims + animation * 4)) animation = 0;
    address = kAnims + slot * 4;
    original = REX_LOAD_U32(address);
    REX_STORE_U32(address, REX_LOAD_U32(kAnims + animation * 4));
  }
};
thread_local MapAnimation map_animation;
}  // namespace

// Constructor comun. Los dos LR del mapa se verifican en recomp.27/.33;
// 82135B34 es el cargador de figura, SOLO bajo el alcance de la pantalla nivel.
REX_HOOK_RAW(sub_82133B88) {
  const uint32_t caller = uint32_t(ctx.lr);
  int id = -1;
  const bool map = caller == 0x821C2530 || caller == 0x821C6DD0;
  if (map && !dbz3::g_guest_eu) {
    const uint32_t rec = REX_LOAD_U32(kMap + 180);
    if (rec) {
      const uint32_t slot = REX_LOAD_U8(rec + 4577);
      id = Hero(base, slot);
      if (id >= 0) map_animation.Apply(base, slot, id);
    }
  } else if (caller == 0x82135B34) {
    id = level_hero;
  }
  if (id >= 0) {
    REXLOG_INFO("dbz3 heroe avatar: {} ID {} modelo 0 traje 0 (antes {} / {} / {})", map ? "mapa" : "nivel",
                id, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
    ctx.r3.u64 = uint32_t(id);
    ctx.r4.u64 = 0;
    ctx.r5.u64 = 0;
  }
  __imp__sub_82133B88(ctx, base);
}

// Los dos creadores cargan kAnims[casilla] en map+24 ANTES de esta llamada.
// La tabla se devuelve inmediatamente a su valor previo (incluido WithLook).
REX_HOOK_RAW(sub_821C0BE0) {
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller == 0x821C2574 || caller == 0x821C6E18) map_animation.Restore(base);
  __imp__sub_821C0BE0(ctx, base);
}

// Nivel (pantalla 10): r3 apunta a la ficha temporal; +1 es casilla DU.
// Se conservan el reparto, la camara y la animacion originales; solo se elige
// modelo 0/traje 0 del ID propio, igual que en combate.
REX_HOOK_RAW(sub_821CD098) {
  const uint32_t record = ctx.r3.u32;
  const int id = !dbz3::g_guest_eu && record ? Hero(base, REX_LOAD_U8(record + 1)) : -1;
  struct Scope {
    int previous;
    ~Scope() { level_hero = previous; }
  } scope{std::exchange(level_hero, id)};
  __imp__sub_821CD098(ctx, base);
}
