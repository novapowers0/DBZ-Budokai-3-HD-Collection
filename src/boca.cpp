// dbz3 - Boca con la voz (R23). Ver boca.h y studies 5/ENTREGAS/R23/BOCAS.md.
#include "iw_anim_intro7db.h"
#include "boca.h"
#include "anim_iw.h"      // anim::efecto::CopiarBoca (EU 0x25FA90 = B3 sub_82136FD0)
#include "iw_ap_boca.h"   // BancoAMK: valida la pista antes de pasarla al juego
#include "roster_ext.h"   // GuestAlloc
#include "generated/dbz3_init.h"   // REX_LOAD/STORE
#include "guest_region.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

REXCVAR_DEFINE_BOOL(dbz3_boca, false, "DBZ3/Mods",
                    "Boca con la voz (R23): IW y demas modos arrancan la boca nativa de B3 con la pista #AMK del "
                    "original; las poses son las LIPS del personaje (pruebas)");

namespace dbz3::boca {
namespace {
constexpr uint32_t kAnimObj = 0x650;                 // A = X+0x650
constexpr uint32_t kCuerpo = 0x530, kHablante = 0x540;   // A+0x530 / A+0x540 (sub_820FA750: r29 / r30)
constexpr uint32_t kId = 0x640, kTiempo = 0x644, kIgnorar = 0x648, kCongelado = 0x66C;
constexpr uint32_t kRanura = 15;                     // B3 solo usa el banco 0 (sub_82117460, sub_820FB0D0)
constexpr uint32_t kBancos = 0x2C;                   // hablante+0x2C+4*banco (sub_820F94E0)

bool Valido(uint32_t a) { return a >= 0x10000000u && a < 0xF0000000u; }
uint32_t Le32(const std::vector<uint8_t>& b, size_t o) {
  return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}

// #AMK PS2 (LE, offsets relativos) -> memoria de B3 (BE, punteros absolutos como tras la reubicacion del juego).
// Se cachea por contenido: cada pista se sube una vez. ponytail: no se libera nunca (pocos KB por actor).
uint32_t Subir(uint8_t* base, const std::vector<uint8_t>& amk) {
  static std::mutex mu;
  static std::map<std::vector<uint8_t>, uint32_t> subidas;
  std::lock_guard<std::mutex> lk(mu);
  if (auto it = subidas.find(amk); it != subidas.end()) return it->second;
  anim::apev::boca::BancoAMK prueba;
  if (!prueba.Cargar(amk)) return 0;   // magia, tabla y claves comprobadas
  const uint32_t n = Le32(amk, 20), tabla = Le32(amk, 24);
  size_t claves = 0;
  for (uint32_t i = 0; i < n; ++i) claves += Le32(amk, tabla + 16 * i + 4);
  const uint32_t g = roster::GuestAlloc(uint32_t(32 + 16 * n + 4 * claves));
  if (!g) return 0;
  REX_STORE_U32(g, 0x23414D4Bu);   // "#AMK"
  for (uint32_t k = 4; k < 32; k += 4) REX_STORE_U32(g + k, Le32(amk, k));
  REX_STORE_U32(g + 24, g + 32);
  uint32_t datos = g + 32 + 16 * n;
  for (uint32_t i = 0; i < n; ++i) {
    const size_t p = tabla + 16 * i;
    const uint32_t cuantas = Le32(amk, p + 4), q = Le32(amk, p + 12), e = g + 32 + 16 * i;
    REX_STORE_U32(e, Le32(amk, p));
    REX_STORE_U32(e + 4, cuantas);
    REX_STORE_U32(e + 8, Le32(amk, p + 8));
    REX_STORE_U32(e + 12, cuantas ? datos : 0);
    for (uint32_t k = 0; k < cuantas; ++k, datos += 4) {
      const size_t c = q + 4 * k;
      REX_STORE_U16(datos, uint16_t(amk[c] | amk[c + 1] << 8));
      REX_STORE_U8(datos + 2, amk[c + 2]);
      REX_STORE_U8(datos + 3, amk[c + 3]);
    }
  }
  subidas.emplace(amk, g);
  return g;
}
}  // namespace

bool Activo() {
  static const bool v = REXCVAR_GET(dbz3_boca);
  return v;
}

bool Hablar(uint8_t* base, uint32_t x, const std::vector<uint8_t>& amk, int clip, bool ignorar_pausa, bool intro7db) {
  if ((!Activo() && !(intro7db && anim7db::escena.load())) || clip < 0 || clip > 0xFFFF || !Valido(x)) return false;
  const uint32_t a = x + kAnimObj;
  if (REX_LOAD_U8(a + kCongelado)) return false;   // como sub_820FA750: congelado no arranca
  const uint32_t hablante = REX_LOAD_U32(a + kHablante), cuerpo = REX_LOAD_U32(a + kCuerpo);
  if (!Valido(hablante) || !Valido(cuerpo)) return false;
  const uint32_t banco = Subir(base, amk);
  if (!banco || uint32_t(clip) >= REX_LOAD_U32(banco + 20) ||
      int32_t(REX_LOAD_U32(REX_LOAD_U32(banco + 24) + 16 * uint32_t(clip) + 4)) <= 0)
    return false;   // sub_820F94E0: clip vacio o fuera del banco
  REX_STORE_U32(hablante + kBancos + 4 * kRanura, banco);
  REX_STORE_U32(a + kIgnorar, ignorar_pausa ? 1u : 0u);
  REX_STORE_U32(a + kId, ((kRanura + 1) << 16) | uint32_t(clip));
  REX_STORE_U32(a + kTiempo, 0u);   // 0.0f
  anim::efecto::CopiarBoca(base, hablante, cuerpo, 1);   // cuerpo -> hablante: la mezcla parte de la pose actual
  return true;
}

int VisemaActual(uint8_t* base, uint32_t x, int* variante) {
  if (!Valido(x)) return -1;
  const uint32_t a = x + kAnimObj, hablante = REX_LOAD_U32(a + kHablante);
  if (!Valido(hablante) || int32_t(REX_LOAD_U32(a + kId)) < 0) return -1;
  if (variante) *variante = int8_t(REX_LOAD_U8(hablante + 0x20 + 0x4D));
  return int8_t(REX_LOAD_U8(hablante + 0x20 + 0x4F));
}

}  // namespace dbz3::boca

namespace dbz3::boca {
namespace {
// PM-33: observa los actores nativos, tambien fuera del gestor de dos luchadores.
// El original conserva el clip, banco 0, azar, reloj y copia de huesos.
bool RegistroNativo() {
  static const bool activo = [] {
    const char* v = std::getenv("DBZ3_TEST_BOCA_NATIVA");
    return v && std::strcmp(v, "1") == 0;
  }();
  return activo;
}
struct VozNativa {
  uint64_t serie = 0;
  uint32_t a = 0, hablante = 0, cuerpo = 0, id = 0;
  float cuadro = 0;
  int pose = -1;
};
thread_local std::array<VozNativa, 16> voces{};
thread_local size_t siguiente = 0;
std::atomic<uint64_t> serie{0};

VozNativa* BuscarVoz(uint32_t a) {
  // serie == 0 identifica los huecos vacios, tambien tras reiniciar una voz.
  for (auto& v : voces) if (v.serie != 0 && v.a == a) return &v;
  return nullptr;
}
}  // namespace
}  // namespace dbz3::boca

// Retornos del bl de sub_82117460 (US) / sub_82117560 (EU), no el de #CSK ni el de scripts.
DBZ3_HOOK(sub_820FA750, dbz3eu_sub_820FA850) {
  using namespace dbz3::boca;
  if (!RegistroNativo()) return orig(ctx, base);
  const uint32_t a = ctx.r3.u32, clip = ctx.r4.u32;
  const bool voz = ctx.lr == dbz3::GuestAddr(0x821175F0u, 0x821176F0u);
  const bool activo = Valido(a) && !REX_LOAD_U8(a + kCongelado);
  orig(ctx, base);
  if (!activo) return;
  auto* anterior = BuscarVoz(a);
  if (anterior) *anterior = {};
  if (!voz || clip > 0xFFFF) return;
  const uint32_t id = REX_LOAD_U32(a + kId);
  const uint32_t h = REX_LOAD_U32(a + kHablante), c = REX_LOAD_U32(a + kCuerpo);
  if (id != (0x10000u | clip) || !Valido(h) || !Valido(c)) {
    REXLOG_INFO("dbz3 boca04: rechazada actor={:08X} clip={}", a, clip);
    return;
  }
  auto& v = anterior ? *anterior : voces[siguiente++ % voces.size()];
  v = {++serie, a, h, c, id};
  REXLOG_INFO("dbz3 boca04: inicio serie={} actor={:08X} clip={}", v.serie, a, clip);
}

// Animacion del cuerpo: tras evaluar la clave y copiar hablante -> cuerpo.
DBZ3_HOOK(sub_820FAF40, dbz3eu_sub_820FB040) {
  using namespace dbz3::boca;
  if (!RegistroNativo()) return orig(ctx, base);
  const uint32_t a = ctx.r3.u32;
  orig(ctx, base);
  if (!Valido(a)) return;   // repaso dsREV: r3 = 0 leia direcciones bajas
  auto* v = BuscarVoz(a);
  if (!v) return;
  const float cuadro = std::bit_cast<float>(uint32_t(REX_LOAD_U32(a + kTiempo)));
  if (REX_LOAD_U32(a + kId) != v->id || REX_LOAD_U32(a + kHablante) != v->hablante ||
      REX_LOAD_U32(a + kCuerpo) != v->cuerpo || !(cuadro >= v->cuadro)) {
    *v = {};
    return;
  }
  // A+0x624 == -2: sub_820FAF40 no evalua ni copia; congelado: tampoco.
  if (REX_LOAD_U32(a + 0x624) == 0xFFFFFFFEu || REX_LOAD_U8(a + kCongelado)) return;
  const uint32_t selector = v->hablante + 0x20;
  if (REX_LOAD_U32(selector + 88) != v->id) return;
  int variante = -1;
  const int visema = VisemaActual(base, a - kAnimObj, &variante);
  if (visema < 0 || visema >= 6 || variante < 0) return;
  const int pose = 6 * variante + visema;
  v->cuadro = cuadro;
  if (v->pose == pose) return;
  v->pose = pose;
  REXLOG_INFO("dbz3 boca04: pose serie={} actor={:08X} clip={} cuadro={:.3f} visema={} variante={} pose={}",
              v->serie, a, v->id & 0xFFFFu, cuadro, visema, variante, pose);
}
