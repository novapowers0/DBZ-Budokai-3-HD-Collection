// dbz3 - Tecnicas sin tope (31/10, se incluye al final de ports_guard.cpp; sin entrada propia en CMake).
//
// 1) Mas de 64 recursos en un BSP. La ranura guarda 64 punteros de recurso (+32) y 64 de instancia (+288); con mas, el
//    constructor nativo (sub_82126490) se sale y pisa instancias y hasta la ranura siguiente (Vegeta GT de IW, 65). Los
//    recursos >= 64 van a una tabla del lado host; el asa va en ranura+28 (campo sin usar: medido en todas las funciones
//    con zancada 544/1152), asi se mueve sola con los memcpy de la ranura de Tag Team. Lectores del juego: los 4 de abajo
//    (los unicos que suman indice a la ranura, ademas de sub_82125808/8212AEC0, que leen el primero de [1]/[0]).
//    Un BSP de <= 64 sigue el camino nativo sin ningun cambio.
//
// 2) Tecnicas y guion por forma (roster.toml: tecnicas_forma = [-1, fid], cam_forma = [-1, fid]). Cada forma con fichero
//    propio tiene su contenido de ranura (544 B) y su bloque de camara (X+7896, 24 B) guardados aqui; al cambiar X+2980 se
//    intercambian y se reenlazan los punteros en uso, igual que el relevo de Tag Team (mismas llamadas). Asi una forma no
//    comparte indices ni codigos con las demas y Gogeta SSJ (25) + SS4 (44) = 69 no se funden: cada uno en su sitio.
//    Lo que este en vuelo sigue valiendo: nada se suelta hasta el final del combate.

// Pareja verificada por firma y llamadas: 46 instrucciones, solo cambian save/restore.
DBZ3_EXTERN_EU(dbz3eu_sub_8208D290);

namespace {

struct Spill {
  std::vector<uint32_t> res, inst;
};
std::vector<Spill> g_spill;     // asa = indice + 1 (ranura+28)
std::atomic<bool> g_spill_on{false};
constexpr uint32_t kSlotStride = 544, kSideStride = 1152, kSpillField = 28;

using GuestFn = void (*)(PPCContext&, uint8_t*);
uint32_t GCall(PPCContext& ctx, uint8_t* base, GuestFn fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0) {
  PPCContext c = ctx;
  c.r3.u64 = r3;
  c.r4.u64 = r4;
  c.r5.u64 = r5;
  fn(c, base);
  return c.r3.u32;
}
#define TEC_CALL(fn, ...) GCall(ctx, base, DBZ3_CALL_EU(fn), __VA_ARGS__)

void BindFormAnimations(PPCContext& ctx, uint8_t* base, uint32_t x, uint32_t model, uint32_t form) {
  if (!model || form >= 8) return;
  for (const auto& pair : {std::pair{3u, 8u}, std::pair{4u, 12u}, std::pair{7u, 16u}}) {
    const uint32_t acm = REX_LOAD_U32(x + 7672 + 28 * form + pair.second);
    if (acm && REX_LOAD_U32(model + 1132 + 4 * pair.first) != acm)
      GCall(ctx, base, DBZ3_CALL2(sub_8208D2C8, dbz3eu_sub_8208D290), model, pair.first, acm);
  }
}

uint32_t SlotAddr(uint8_t* base, uint32_t k, uint32_t j) {
  const uint32_t b = REX_LOAD_U32(kSlotsBase);
  return b ? b + kSideStride * k + kSlotStride * j + 80 : 0;
}

Spill* SpillOf(uint8_t* base, uint32_t slot) {
  const uint32_t h = slot ? REX_LOAD_U32(slot + kSpillField) : 0;
  return h && h <= g_spill.size() ? &g_spill[h - 1] : nullptr;
}

// Lectura de la ranura del lado k (j < 0: la que toca, como sub_82126078).
uint32_t SlotOf(PPCContext& ctx, uint8_t* base, uint32_t side, uint32_t j) {
  return TEC_CALL(sub_82126078, side, j);
}

void AvisoLectura(uint32_t i, uint32_t res) {
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 4) REXLOG_INFO("dbz3 ports: recurso {} leido de la tabla ampliada ({:08X})", i, res);
}

// Mismo trabajo que sub_82126490 para un recurso: hijo `idx` de `parent`, en la posicion i.
void PutResource(PPCContext& ctx, uint8_t* base, uint32_t slot, Spill& sp, uint32_t parent, uint32_t idx, uint32_t i) {
  const uint32_t hdr = TEC_CALL(sub_820B0CC8, parent, idx);
  const uint32_t res = TEC_CALL(sub_820B0CF8, parent, idx);
  TEC_CALL(sub_82126430, hdr, res);
  const bool awo = int32_t(REX_LOAD_U32(hdr + 8)) == 1;
  const uint32_t inst = awo ? TEC_CALL(sub_8208DEE0, res, 0) : 0;
  if (i < kCap) {
    REX_STORE_U32(slot + 32 + 4 * i, res);
    if (awo) REX_STORE_U32(slot + 288 + 4 * i, inst);
  } else {
    sp.res.push_back(res);
    sp.inst.push_back(inst);
  }
}

}  // namespace

// Recargar un modelo vuelve a enlazar los ACM base. Conservar los propios del
// guion de forma tambien en ese camino; r5 != 0 es la victima de una cinematica
// y debe conservar exactamente los bancos prestados por el atacante.
// US/EU: 156 instrucciones equivalentes, llamadas 3/4/7 verificadas en ambas.
DBZ3_HOOK(sub_821093C8, dbz3eu_sub_821094C8) {
  const uint32_t x = ctx.r3.u32, other = ctx.r5.u32;
  const int form = ctx.r4.s32;
  orig(ctx, base);
  if (other) return;
  const auto* files = dbz3::roster::FormFilesOf(int32_t(REX_LOAD_U32(x + 7420)));
  if (!files) return;
  for (int f = 1; f < 8; ++f) {
    if ((form >= 0 && form != f) || files->cam[size_t(f)] < 0 || !REX_LOAD_U32(x + 7672 + 28 * f + 24)) continue;
    const uint32_t instance = REX_LOAD_U32(x + 7608 + 4 * f);
    const uint32_t root = instance ? REX_LOAD_U32(instance) : 0;
    if (root) BindFormAnimations(ctx, base, x, REX_LOAD_U32(root + 76), uint32_t(f));
  }
}

// Prueba de los recursos desbordados (DBZ3_TEST_TECNICAS_EXTRA=N): anade N hijos de la raiz que apuntan al ultimo
// recurso real, para medir el camino con cualquier numero sin fabricar un BSP en disco.
DBZ3_HOOK_EU(sub_82126490) {
  if (SinGuardas()) return orig(ctx, base);
  const uint32_t k = ctx.r3.u32, j = ctx.r4.u32;
  const uint32_t slot = k < 2 && j < 2 ? SlotAddr(base, k, j) : 0;
  const uint32_t node = slot ? REX_LOAD_U32(slot) : 0;
  const uint32_t data = node ? REX_LOAD_U32(node + 192) : 0;
  if (!data) return orig(ctx, base);
  const uint32_t bin = REX_LOAD_U32(data + 208);
  // sub_820800A8 es idempotente (marca el fichero con '!'): se puede contar antes
  TEC_CALL(sub_820800A8, bin);
  const uint32_t kids0 = TEC_CALL(sub_820B0CF8, bin, 0), kids1 = TEC_CALL(sub_820B0CF8, bin, 1);
  if (!kids0 || !kids1) return orig(ctx, base);
  TEC_CALL(sub_820800A8, kids0);
  TEC_CALL(sub_820800A8, kids1);
  const uint32_t n0 = REX_LOAD_U32(kids0 + 16), n1 = REX_LOAD_U32(kids1 + 16), ntop = REX_LOAD_U32(bin + 16);
  const uint32_t total = n0 + n1 + (ntop > 2 ? ntop - 2 : 0);
  static const int extra_prueba = [] {
    const char* e = std::getenv("DBZ3_TEST_TECNICAS_EXTRA");
    return e ? std::atoi(e) : 0;
  }();
  if (total <= kCap && !extra_prueba) return orig(ctx, base);

  REX_STORE_U32(slot, data);
  REX_STORE_U32(slot + 4, bin);
  REX_STORE_U32(node + 192, 0);
  TEC_CALL(sub_82083D08, node, 0);
  Spill sp;
  uint32_t i = 0;
  for (uint32_t q = 0; q < n0; ++q) PutResource(ctx, base, slot, sp, kids0, q, i++);
  REX_STORE_U32(slot + 12, i);
  for (uint32_t q = 0; q < n1; ++q) PutResource(ctx, base, slot, sp, kids1, q, i++);
  REX_STORE_U32(slot + 16, i);
  for (uint32_t q = 2; q < ntop; ++q) PutResource(ctx, base, slot, sp, bin, q, i++);
  for (int e = 0; e < extra_prueba; ++e) {   // relleno de prueba: mismo fichero, sin instancia
    const uint32_t res = i ? REX_LOAD_U32(slot + 32) : 0;
    if (i < kCap) REX_STORE_U32(slot + 32 + 4 * i, res);
    else { sp.res.push_back(res); sp.inst.push_back(0); }
    ++i;
  }
  REX_STORE_U32(slot + 20, 1);
  if (!sp.res.empty()) {
    size_t h = 0;
    while (h < g_spill.size() && !g_spill[h].res.empty()) ++h;
    if (h == g_spill.size()) g_spill.emplace_back();
    g_spill[h] = std::move(sp);
    REX_STORE_U32(slot + kSpillField, uint32_t(h + 1));
    g_spill_on.store(true);
  }
  REXLOG_INFO("dbz3 ports: tecnicas del lado {} ranura {} con {} recursos ({} de {} en la tabla ampliada)", k, j, i,
              i > kCap ? i - kCap : 0, i);
  if (extra_prueba) {   // lectura por el camino del juego: el ultimo recurso debe ser el relleno (el primero del fichero)
    const uint32_t ultimo = TEC_CALL(sub_820CD4A0, ntop + uint32_t(extra_prueba) - 1, k, j);
    REXLOG_INFO("dbz3 ports: [prueba] recurso {} leido {:08X} esperado {:08X}", i - 1, ultimo, REX_LOAD_U32(slot + 32));
  }
}

// Bancos del juego (sub_820CF580: 0 = desde [1], 1 = hijos de la raiz, 2 = desde [0]); sub_820CF680 igual con instancias.
namespace dbz3::ports {
bool RecursoAmpliado(PPCContext& ctx, uint8_t* base) {
  if (!g_spill_on.load(std::memory_order_relaxed) || (ctx.r4.u32 != 0 && ctx.r4.u32 != 2)) return false;
  const uint32_t slot = SlotOf(ctx, base, ctx.r3.u32, 0xFFFFFFFFu);
  const uint32_t i = REX_LOAD_U32(slot + (ctx.r4.u32 == 0 ? 12 : 8)) + ctx.r5.u32;
  if (i < kCap) return false;
  const Spill* sp = SpillOf(base, slot);
  ctx.r3.u64 = sp && i - kCap < sp->res.size() ? sp->res[i - kCap] : 0;
  AvisoLectura(i, ctx.r3.u32);
  return true;
}
}  // namespace dbz3::ports
DBZ3_HOOK_EU(sub_820CF680) {
  if (g_spill_on.load(std::memory_order_relaxed) && (ctx.r4.u32 == 0 || ctx.r4.u32 == 2)) {
    const uint32_t side = ctx.r3.u32, bank = ctx.r4.u32, idx = ctx.r5.u32;
    const uint32_t slot = SlotOf(ctx, base, side, 0xFFFFFFFFu);
    const uint32_t i = REX_LOAD_U32(slot + (bank == 0 ? 12 : 8)) + idx;
    if (i >= kCap) {
      const Spill* sp = SpillOf(base, slot);
      ctx.r3.u64 = sp && i - kCap < sp->inst.size() ? sp->inst[i - kCap] : 0;
      return;
    }
    ctx.r3.u64 = side;
  }
  orig(ctx, base);
}
// sub_820CD4A0(indice r3, lado r4, ranura r5): hijos de la raiz (indice >= 2 va despues de [0] y [1]); sub_820CD508 instancias.
DBZ3_HOOK_EU(sub_820CD4A0) {
  if (g_spill_on.load(std::memory_order_relaxed) && ctx.r4.s32 != -1) {
    const uint32_t idx = ctx.r3.u32, side = ctx.r4.u32, j = ctx.r5.u32;
    const uint32_t slot = SlotOf(ctx, base, side, j);
    const uint32_t i = REX_LOAD_U32(slot + 16) + idx - 2;
    if (int32_t(i) >= int32_t(kCap)) {
      const Spill* sp = SpillOf(base, slot);
      ctx.r3.u64 = sp && i - kCap < sp->res.size() ? sp->res[i - kCap] : 0;
      AvisoLectura(i, ctx.r3.u32);
      return;
    }
  }
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820CD508) {
  if (g_spill_on.load(std::memory_order_relaxed) && ctx.r4.s32 != -1) {
    const uint32_t idx = ctx.r3.u32, side = ctx.r4.u32, j = ctx.r5.u32;
    const uint32_t slot = SlotOf(ctx, base, side, j);
    const uint32_t i = REX_LOAD_U32(slot + 16) + idx - 2;
    if (int32_t(i) >= int32_t(kCap)) {
      const Spill* sp = SpillOf(base, slot);
      ctx.r3.u64 = sp && i - kCap < sp->inst.size() ? sp->inst[i - kCap] : 0;
      return;
    }
  }
  orig(ctx, base);
}
// sub_82125808 lee el primer recurso de [1], ranura+32+4*n0: con n0 >= 64 esta en la tabla ampliada (se le presenta en la
// posicion 0 mientras dura la llamada).
DBZ3_HOOK_EU(sub_82125808) {
  if (g_spill_on.load(std::memory_order_relaxed)) {
    const uint32_t side = ctx.r3.u32, j = ctx.r4.u32;
    const uint32_t slot = SlotOf(ctx, base, side, j);
    const uint32_t n0 = REX_LOAD_U32(slot + 12);
    const Spill* sp = n0 >= kCap ? SpillOf(base, slot) : nullptr;
    if (sp && n0 - kCap < sp->res.size()) {
      const uint32_t e0 = REX_LOAD_U32(slot + 32), res = sp->res[n0 - kCap];
      REX_STORE_U32(slot + 12, 0);
      REX_STORE_U32(slot + 32, res);
      ctx.r3.u64 = side;
      ctx.r4.u64 = j;
      orig(ctx, base);
      REX_STORE_U32(slot + 32, e0);
      REX_STORE_U32(slot + 12, n0);
      return;
    }
    ctx.r3.u64 = side;
    ctx.r4.u64 = j;
  }
  orig(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tecnicas y guion por forma
namespace {

constexpr uint32_t kXForm = 2980, kXId = 7420, kXCamBlock = 7896;
constexpr int kMaxForm = 8;

struct FormBank {
  int id = -1;
  std::array<uint32_t, kMaxForm> tech_node{}, cam_node{};
  std::array<std::array<uint8_t, 544>, kMaxForm> slot{};      // contenido de la ranura de la forma que no esta puesta
  std::array<std::array<uint8_t, 24>, kMaxForm> cam{};        // X+7896.. de la forma que no esta puesta
  std::array<bool, kMaxForm> slot_ok{}, cam_ok{};
  int tech_cur = 0, cam_cur = 0;
};
std::array<std::vector<FormBank>, 2> g_banks;   // por lado; un Tag Team puede traer varios IDs con formas
std::array<int, 2> g_last_id{-1, -1};

// A que hueco de banco corresponde la forma: las formas sin fichero propio usan el de la forma 0.
int BankIndex(const std::array<int32_t, 8>& fids, uint32_t form) {
  return form < 8 && fids[form] >= 0 ? int(form) : 0;
}

bool NodeReady(uint8_t* base, uint32_t n) {
  return n && !REX_LOAD_U8(n + 1) && REX_LOAD_U32(n + 192);
}

// Lo mismo que hace sub_8210FDE0 con la camara del luchador, sobre un bloque propio.
void ProcessCam(PPCContext& ctx, uint8_t* base, uint32_t node, std::array<uint8_t, 24>& out) {
  const uint32_t data = REX_LOAD_U32(node + 192), root = REX_LOAD_U32(data + 208);
  TEC_CALL(sub_820800A8, root);
  REX_STORE_U32(node + 192, 0);
  TEC_CALL(sub_82083D08, node, 0);
  const uint32_t c0 = TEC_CALL(sub_820B0CF8, root, 0);
  TEC_CALL(sub_820800A8, c0);
  const uint32_t c2 = TEC_CALL(sub_820B0CF8, root, 2);
  TEC_CALL(sub_820800A8, c2);
  const uint32_t c3 = TEC_CALL(sub_820B0CF8, root, 3);
  const uint32_t h3 = TEC_CALL(sub_820B0CC8, root, 3);
  REX_STORE_U32(c3 + 28, REX_LOAD_U32(h3 + 4));
  const uint32_t w[6] = {root, c0, 0, c2, c3, data};
  for (int i = 0; i < 6; ++i)
    for (int b = 0; b < 4; ++b) out[size_t(4 * i + b)] = uint8_t(w[i] >> (24 - 8 * b));
}

void BankTick(PPCContext& ctx, uint8_t* base, uint32_t k, uint32_t x) {
  const int id = int32_t(REX_LOAD_U32(x + kXId));
  auto& banks = g_banks[k];
  // relevo de Tag Team: sub_821092C8 solto la camara del que salio (la que estaba puesta); la de base guardada, si la
  // puesta era otra, ya no volvera: se suelta, y al volver entra su camara de base recien cargada
  if (g_last_id[k] != id) {
    for (auto& b : banks) {
      if (b.id != g_last_id[k] || !b.cam_cur) continue;
      b.cam_ok[size_t(b.cam_cur)] = false;
      b.cam_node[size_t(b.cam_cur)] = 0;
      if (b.cam_ok[0]) {
        TEC_CALL(sub_82083A68, (uint32_t(b.cam[0][20]) << 24) | (uint32_t(b.cam[0][21]) << 16) |
                                   (uint32_t(b.cam[0][22]) << 8) | b.cam[0][23]);
        b.cam_ok[0] = false;
      }
      b.cam_cur = 0;
    }
    g_last_id[k] = id;
  }
  const dbz3::roster::FormFiles* files = dbz3::roster::FormFilesOf(id);
  if (!files) return;
  FormBank* bank = nullptr;
  for (auto& b : banks)
    if (b.id == id) bank = &b;
  if (!bank) {
    banks.emplace_back();
    bank = &banks.back();
    bank->id = id;
    REXLOG_INFO("dbz3 ports: lado {} id {}: tecnicas y guion por forma", k, id);
  }
  // Super Bu (35): con forma, el juego lee la ranura B (sub_82126078, medido en su codigo): ahi van las tecnicas del absorbido
  const uint32_t ranura = id == 35 ? 1 : 0;
  const uint32_t slot = SlotAddr(base, k, ranura);
  if (!slot || !REX_LOAD_U32(SlotAddr(base, k, 0) + 20)) return;   // ranura aun sin procesar (arranque, relevo)
  for (int f = 1; f < kMaxForm; ++f) {
    const size_t s = size_t(f);
    if (files->tech[s] >= 0 && !bank->tech_node[s] && !bank->slot_ok[s] && bank->tech_cur != f) {
      const uint32_t h = TEC_CALL(sub_82083968, uint32_t(files->tech[s]), 0, 1);
      bank->tech_node[s] = h != 0xFFFFFFFFu ? h : 0;
    }
    if (files->cam[s] >= 0 && !bank->cam_node[s] && !bank->cam_ok[s] && bank->cam_cur != f) {
      const uint32_t h = TEC_CALL(sub_82083968, uint32_t(files->cam[s]), 0, 1);
      bank->cam_node[s] = h != 0xFFFFFFFFu ? h : 0;
    }
    // proceso en cuanto llegan (crear instancias aqui, no en el fotograma de transformarse)
    if (NodeReady(base, bank->tech_node[s])) {
      std::array<uint8_t, 544> keep;
      std::memcpy(keep.data(), base + slot, 544);
      std::memset(base + slot, 0, 544);
      REX_STORE_U32(slot, bank->tech_node[s]);
      TEC_CALL(sub_82126490, k, ranura);
      std::memcpy(bank->slot[s].data(), base + slot, 544);
      std::memcpy(base + slot, keep.data(), 544);
      bank->slot_ok[s] = true;
      bank->tech_node[s] = 0;
      REXLOG_INFO("dbz3 ports: lado {} id {} forma {}: tecnicas listas (estado {})", k, id, f, REX_LOAD_U32(REX_LOAD_U32(dbz3::GA(0x823D6CF4)) + 35128));
    }
    if (NodeReady(base, bank->cam_node[s])) {
      ProcessCam(ctx, base, bank->cam_node[s], bank->cam[s]);
      bank->cam_ok[s] = true;
      bank->cam_node[s] = 0;
    }
  }
  const uint32_t form = REX_LOAD_U32(x + kXForm);
  const int want_tech = BankIndex(files->tech, form), want_cam = BankIndex(files->cam, form);
  if (want_tech != bank->tech_cur && bank->slot_ok[size_t(want_tech)]) {
    std::memcpy(bank->slot[size_t(bank->tech_cur)].data(), base + slot, 544);
    bank->slot_ok[size_t(bank->tech_cur)] = REX_LOAD_U32(slot + 20) != 0;   // la B de Bu puede estar vacia: nada que soltar
    std::memcpy(base + slot, bank->slot[size_t(want_tech)].data(), 544);
    bank->slot_ok[size_t(want_tech)] = false;
    bank->tech_cur = want_tech;
    TEC_CALL(sub_82125808, k, 0xFFFFFFFFu);
    TEC_CALL(sub_8212AEC0, k, 0xFFFFFFFFu);
    REXLOG_INFO("dbz3 ports: lado {} id {} forma {}: tecnicas {} (ranura {:08X})", k, id, form, want_tech, slot);
  }
  if (want_cam != bank->cam_cur && bank->cam_ok[size_t(want_cam)]) {
    std::memcpy(bank->cam[size_t(bank->cam_cur)].data(), base + x + kXCamBlock, 24);
    bank->cam_ok[size_t(bank->cam_cur)] = true;
    std::memcpy(base + x + kXCamBlock, bank->cam[size_t(want_cam)].data(), 24);
    bank->cam_ok[size_t(want_cam)] = false;
    bank->cam_cur = want_cam;
    // arbol de golpes (#CCM): sub_82109638 lo copia en X+5444 al reiniciar y sub_820F6560 deja el lector en la raiz
    REX_STORE_U32(x + 5444, REX_LOAD_U32(x + kXCamBlock + 12));
    TEC_CALL(sub_820F6560, x + 4960);
    // Un guion/moveset prestado usa sus ACM como un personaje independiente.
    // sub_821093C8 deja los bancos 3/4/7 ligados a la forma BASE incluso tras
    // transformarse: Gogeta SS4 ejecutaba 25A con el clip 0x5E de SSJ y no conectaba.
    // Reenlazar con la rutina nativa reconstruye tambien los mapas de huesos.
    {
      const uint32_t model = REX_LOAD_U32(x + 2952);
      BindFormAnimations(ctx, base, x, model, uint32_t(want_cam));
      REXLOG_INFO("dbz3 ports: lado {} id {}: animaciones propias de la forma {}", k, id, want_cam);
    }
    REXLOG_INFO("dbz3 ports: lado {} id {} forma {}: guion {}", k, id, form, want_cam);
  }
}

// Fin del combate (lado k): todo lo guardado se suelta como lo haria el juego. La ranura y la camara puestas las suelta el.
void BanksFree(PPCContext& ctx, uint8_t* base, uint32_t k, uint32_t slot, GuestFn free_slot) {
  auto& banks = g_banks[k];
  if (banks.empty()) return;
  std::array<uint8_t, 544> keep;
  std::memcpy(keep.data(), base + slot, 544);
  int freed = 0;
  auto be32 = [](const std::array<uint8_t, 24>& c, int o) {
    return (uint32_t(c[size_t(o)]) << 24) | (uint32_t(c[size_t(o + 1)]) << 16) | (uint32_t(c[size_t(o + 2)]) << 8) | c[size_t(o + 3)];
  };
  for (auto& b : banks) {
    for (size_t f = 0; f < kMaxForm; ++f) {
      if (b.tech_node[f]) TEC_CALL(sub_82083D08, b.tech_node[f], 0);
      if (b.cam_node[f]) TEC_CALL(sub_82083D08, b.cam_node[f], 0);
      if (b.slot_ok[f]) {
        std::memcpy(base + slot, b.slot[f].data(), 544);
        GCall(ctx, base, free_slot, k, 0);
        ++freed;
      }
      if (b.cam_ok[f]) {
        TEC_CALL(sub_82083A68, be32(b.cam[f], 20));
        ++freed;
      }
    }
  }
  std::memcpy(base + slot, keep.data(), 544);
  banks.clear();
  g_last_id[k] = -1;
  REXLOG_INFO("dbz3 ports: lado {}: {} ficheros por forma sueltos al acabar el combate", k, freed);
}

}  // namespace

namespace dbz3::ports {
void TecnicasTick(PPCContext& ctx, uint8_t* base) {
  if (!dbz3::roster::AnyFormFiles()) return;
  const uint32_t cfg = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!cfg) return;
  for (uint32_t k = 0; k < 2; ++k) BankTick(ctx, base, k, cfg + 2096 + 8240 * k);
}
}  // namespace dbz3::ports

// Soltar la ranura: primero lo guardado por forma (si j == 0, fin de combate), luego la tabla ampliada de lo que hay puesto.
DBZ3_HOOK_EU(sub_821266A8) {
  const uint32_t k = ctx.r3.u32, j = ctx.r4.u32;
  const uint32_t slot = k < 2 && j < 2 ? SlotAddr(base, k, j) : 0;
  if (slot && j == 0) {
    static thread_local bool dentro = false;   // BanksFree vuelve aqui por cada ranura guardada
    if (!dentro) {
      dentro = true;
      BanksFree(ctx, base, k, slot, DBZ3_CALL_EU(sub_821266A8));
      dentro = false;
    }
  }
  if (Spill* sp = SpillOf(base, slot)) {
    for (const uint32_t inst : sp->inst)
      if (inst) TEC_CALL(sub_82089C10, inst);
    REXLOG_INFO("dbz3 ports: tecnicas del lado {}: {} recursos de la tabla ampliada sueltos", k, sp->res.size());
    sp->res.clear();
    sp->inst.clear();
    REX_STORE_U32(slot + kSpillField, 0);
    bool any = false;
    for (const Spill& s : g_spill) any |= !s.res.empty();
    g_spill_on.store(any);
  }
  ctx.r3.u64 = k;
  ctx.r4.u64 = j;
  orig(ctx, base);
}
