// solHAB: cuarta fila virtual del START de DU. Solo US, igual que la Habitacion del mapa.
// Los tres indices nativos y sus recursos siguen perteneciendo al juego.
#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include "launcher/launcher_state.h"
#include "launcher/i18n.h"
#include "ingame/game_style.h"

namespace dbz3::heroe_start {
constexpr dbz3::GA kMapa = 0x82479A60;
bool DibujarSprite(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig);
std::atomic<long long> visto{-100000};
std::atomic<bool> foco{false};

bool Activa(uint8_t* base) {
  return dbz3::heroe_ui::HabitacionActiva() &&
      dbz3::campaigns::g_slot_fighter.count(REX_LOAD_U8(dbz3::GA(0x824A58A0)));
}

class Fila final : public rex::ui::ImGuiDialog {
 public:
  explicit Fila(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
 protected:
  void OnDraw(ImGuiIO& io) override {
    if (dbz3::campaigns::AhoraMs() - visto.load() > 120) return;
    namespace S = dbz3::ingame::style;
    const auto a = S::GameArea(io);
    auto* dl = ImGui::GetForegroundDrawList();
    S::MenuText(dl, a.At(600, 775), 59 * a.s, dbz3::i18n::T("Habitación", "Room"), foco.load(), false, 470 * a.s);
  }
};

void Asegurar() {
  static bool creado = false;
  if (creado) return;
  creado = true;
  dbz3::launcher::DeferUi([] {
    auto* rt = rex::Runtime::instance();
    if (rt && rt->imgui_drawer()) new Fila(rt->imgui_drawer());
  });
}

// Un descriptor de la animacion nativa desmarca la ultima fila, sin inventar un indice 3 para el juego.
void Desmarcar(PPCContext c, uint8_t* base, unsigned fila) {
  c.r1.u32 -= 128;
  const uint32_t p = c.r1.u32 + 80;
  REX_STORE_U8(p, uint8_t(4 + fila));
  REX_STORE_U8(p + 1, 0);
  REX_STORE_U16(p + 2, uint16_t(1 + fila));
  REX_STORE_U16(p + 4, 34);
  REX_STORE_U16(p + 6, 0xFFFF);
  REX_STORE_U8(p + 8, 0xFF);
  c.r3.u32 = kMapa + 80; c.r4.u32 = kMapa + 124;
  c.r5.u32 = kMapa + 136; c.r6.u32 = kMapa + 148;
  c.r7.u32 = p; c.r8.u32 = 37376; c.r9.u32 = 0x20000; c.r10.u32 = 0x300000;
  DBZ3_CALL_EU(sub_821BBF58)(c, base);
  if (fila) {
    c.r3.u32 = REX_LOAD_U32(kMapa + 96 + 4 * fila);
    c.r4.u32 = REX_LOAD_U32(kMapa + 124); c.r5.u32 = 36 + fila; c.r6.u32 = 0;
    DBZ3_CALL_EU(sub_821BB8F0)(c, base);
    c.r3.u32 = REX_LOAD_U32(kMapa + 96 + 4 * fila);
    c.r4.u32 = 0; c.r5.u32 = REX_LOAD_U32(kMapa + 124); c.r6.u32 = 36 + fila;
    DBZ3_CALL_EU(sub_821BB930)(c, base);
  }
}
}  // namespace dbz3::heroe_start

DBZ3_HOOK_EU(sub_821C1F38) {
  orig(ctx, base);
  if (!dbz3::heroe_start::Activa(base)) return;
  dbz3::heroe_start::foco = false;
  dbz3::heroe_start::Asegurar();
  REXLOG_INFO("dbz3 solHAB: START abierto, tres filas nativas + Habitacion");
}

DBZ3_HOOK_EU(sub_821C3FD0) {
  using namespace dbz3::heroe_start;
  if (!Activa(base)) return orig(ctx, base);
  const uint32_t tarea = ctx.r3.u32;
  const uint32_t pad = dbz3::GA(0x8238B780) + REX_LOAD_U32(dbz3::GA(0x8238B670)) * 184;
  const uint16_t pr = REX_LOAD_U16(pad + 14);
  const uint16_t rep = uint16_t(REX_LOAD_U16(pad + 18) | REX_LOAD_U16(pad + 114));
  const unsigned fila = REX_LOAD_U32(kMapa + 228);
  const bool antes = foco.load();
  bool consumir = false, abrir = false;
  if (antes) {
    if (pr & 0x4000) abrir = consumir = true;
    else if (pr & 0x1008) foco = false;  // cierre nativo con B / START
    else if (rep & 0x50) {
      const unsigned nueva = (rep & 0x10) ? 2 : 0;
      PPCContext c = ctx;
      c.r3.u32 = fila; c.r4.u32 = nueva;
      DBZ3_CALL_EU(sub_821C2090)(c, base);
      REX_STORE_U32(kMapa + 228, nueva);
      foco = false; consumir = true;
    }
  } else if (!(pr & 0x5009) && (((rep & 0x40) && fila == 2) || ((rep & 0x10) && fila == 0))) {
    Desmarcar(ctx, base, fila);
    foco = true; consumir = true;
    REXLOG_INFO("dbz3 solHAB: cursor en Habitacion");
  }
  if (consumir) {
    // La confirmacion de la fila nueva cierra el START por su propio camino de salida.
    REX_STORE_U16(pad + 14, abrir ? 8 : 0);
    REX_STORE_U16(pad + 18, 0); REX_STORE_U16(pad + 114, 0);
  }
  orig(ctx, base);
  if (REX_LOAD_U32(tarea + 28) == uint32_t(dbz3::GA(0x821C3FD0))) visto = dbz3::campaigns::AhoraMs();
  else visto = -100000, foco = false;
  if (abrir) {
    visto = -100000; foco = false;
    dbz3::rpg::AbrirHabitacionMapa();
    REXLOG_INFO("dbz3 solHAB: Habitacion abierta desde START");
  }
}

// El render es diferido: BBB78 encola nodos, AD6E0 los dibuja despues.
// El marco crece una separacion sin cambiar sus imagenes ni estirar las etiquetas nativas.
bool dbz3::heroe_start::DibujarSprite(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig) {
  using namespace dbz3::heroe_start;
  if (!Activa(base) || dbz3::campaigns::AhoraMs() - visto.load() > 120)
    return false;
  const uint32_t n = ctx.r3.u32;
  auto anim = [&](unsigned i) -> uint32_t {
    const uint32_t t = REX_LOAD_U32(kMapa + 80 + i * 4);
    if (!t) return 0;
    const uint32_t p = REX_LOAD_U32(t + 60);
    const uint32_t a = p ? REX_LOAD_U32(p) : 0;
    return a;
  };
  auto pertenece = [&](uint32_t a) {
    for (unsigned i = 0; a && i < 8; ++i, a = REX_LOAD_U32(a + 44))
      if (REX_LOAD_U32(a + 48) == n) return true;
    return false;
  };
  const uint32_t a = anim(3);
  const bool marco = a && n == REX_LOAD_U32(a + 48);
  // Hermano del marco: relleno azul de 260 unidades, no la capa que oscurece todo el mapa.
  const bool fondo = pertenece(a) && REX_LOAD_U32(n + 48) == 0xC3020000 && REX_LOAD_U32(n + 56) == 0x43020000;
  const bool franja = foco.load() && pertenece(anim(7));
  if (!marco && !fondo && !franja) return false;
  const uint32_t off = (marco || fondo) ? 60 : 36;
  const uint32_t anterior = REX_LOAD_U32(n + off);
  float v; std::memcpy(&v, &anterior, 4);
  v += (marco || fondo) ? 39.0f : (3 - REX_LOAD_U32(kMapa + 228)) * 39.0f;
  uint32_t cambiado; std::memcpy(&cambiado, &v, 4);
  REX_STORE_U32(n + off, cambiado);
  orig(ctx, base);
  REX_STORE_U32(n + off, anterior);
  return true;
}
