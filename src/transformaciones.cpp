// dbz3 - Transformaciones: capsula de transformacion unica en combate (ficha 14, estilo Infinite World).
//
// Cada forma de B3 pide su propia capsula (char372 +212 + 20f) y el combate lo apunta por luchador en
// X+8156 + f al preparar sus capsulas (sub_82112260, tambien en cada relevo del Tag Team). La subida nativa
// (P+K+G -> sub_82119108) sube mientras esos bytes, la mascara de formas y el ki de cada capsula lo
// permitan; la bajada (sub_82119258) tambien los mira. La capsula unica (1984 + ID, roster_ext.cpp) solo
// enciende esos bytes para las formas que agrupa: el ki que pide cada forma y el resto del juego no cambian.
// Activo en offline y online (UnifiedForms es determinista en ambos jugadores).
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "form_selection.h"
#include "net/netplay.h"
#include "roster_ext.h"

namespace {

constexpr dbz3::GA kMgr = 0x823D6CF4;   // combate: luchador k en +2096 + 8240k, estado en +35128 (4 = pelea)
std::atomic<bool> g_pkg{false};         // prueba: pulsar P+K+G en el siguiente registro del mando 1

// X: +7412 registro de seleccion (+52 n capsulas, +54 IDs), +7416 char372, +7420 ID, +8156 + f forma permitida
void GrantForms(uint8_t* base, uint32_t x) {
  const uint32_t rec = REX_LOAD_U32(x + 7412);
  const int id = int32_t(REX_LOAD_U32(x + 7420));
  const uint16_t uid = dbz3::forms::UnifiedId(id);
  if (!rec || !uid) return;
  const int n = std::clamp<int>(int16_t(REX_LOAD_U16(rec + 52)), 0, 7);
  bool equipped = false;
  for (int k = 0; k < n && !equipped; ++k) equipped = REX_LOAD_U16(rec + 54 + 2 * uint32_t(k)) == uid;
  if (!equipped) return;
  const uint8_t mask = dbz3::roster::UnifiedForms(id);
  std::string caps;   // capsula de cada forma y cuantas tienes (inventario de la partida)
  const uint32_t c372 = REX_LOAD_U32(x + 7416), inv = dbz3::GuestAddr(0x824BA110, 0x824BA100) + 0x2AFE9;
  for (uint32_t f = 1; f < 8; ++f) {
    if ((mask >> f) & 1) REX_STORE_U8(x + 8156 + f, 1);
    const uint32_t cap = c372 ? REX_LOAD_U16(c372 + 212 + 20 * f) : 0;
    if (cap) caps += " " + std::to_string(cap) + ":" + std::to_string(cap < 2048 ? REX_LOAD_U8(inv + cap) : 0);
  }
  REXLOG_INFO("dbz3 formas: luchador {:08X} id {}: capsula unica {} -> formas {:02X} (capsula:tienes{})", x, id,
              uid, mask, caps);
}

// PRUEBA (DBZ3_TEST_FORMAS=1 o =ID; docs/14_frentes/transformaciones_pruebas/formas.py): al jugador
// 1 se le da justo el ki que pide su siguiente forma; con DBZ3_TEST_FORMAS_AUTO=1 la prueba hace lo que sigue a
// P+K+G (abajo). En el tope se queda sin ki y el primer golpe del rival lo baja (bajada nativa). Deja
// "dbz3 formas: prueba: ..." en el log en cada cambio.
void TestTick(PPCContext& ctx, uint8_t* base) {
  static int phase = 0, last = -1, ready = -1, wait = 0, pending = -1;
  static const int only = std::atoi(std::getenv("DBZ3_TEST_FORMAS"));   // > 1: solo con ese ID (Tag Team)
  const uint32_t mgr = REX_LOAD_U32(kMgr);
  if (!mgr) return;
  const uint32_t state = REX_LOAD_U32(mgr + 35128);
  if (state >= 5) phase = 0, last = -1, ready = -1, pending = -1;   // combate acabado: el siguiente, de cero
  const uint32_t x = mgr + 2096;
  if (state != 4 || (only > 1 && int32_t(REX_LOAD_U32(x + 7420)) != only)) return;
  const uint32_t c372 = REX_LOAD_U32(x + 7416);
  const int cur = int32_t(REX_LOAD_U32(x + 2980));
  if (!c372) return;
  if (cur != last) {
    if (last >= 0) REXLOG_INFO("dbz3 formas: prueba: {} a la forma {}", cur > last ? "sube" : "baja", cur);
    last = cur;
  }
  static uint16_t held = 0;   // botones convertidos del usuario 0 (0x8238B780 +12) que ve el juego
  if (const uint16_t h = REX_LOAD_U16(dbz3::GA(0x8238B780) + 12); h != held) {
    held = h;
    if (h) REXLOG_INFO("dbz3 formas: prueba: botones {:04X} (accion {})", h, REX_LOAD_U16(x + 3244));
  }
  auto set_ki = [&](float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    REX_STORE_U32(x + 6100, bits);
  };
  // el rival (COM) no ataca mientras sube: lee el mando 1 (X+5028 = 0; el 2 desconectado pausaba el juego) y
  // solo repite el P+K+G de la prueba. En el tope vuelve a ser COM (-1) y ataca. Con el muneco de combate.cpp
  // (DBZ3_COMBATE_MUNECO=quieto) no hace falta.
  static const bool muneco = std::getenv("DBZ3_COMBATE_MUNECO") != nullptr;
  if (!muneco) REX_STORE_U16(x + 8240 + 5028, uint16_t(phase == 1 ? 0xFFFF : 0));
  if (phase == 1) {               // sin ki hasta que un golpe le baje de forma
    set_ki(0.0f);
    return;
  }
  if (cur != int32_t(REX_LOAD_U32(x + 2984))) return;   // transformandose
  const int count = std::min<int>(int32_t(REX_LOAD_U32(c372 + 0xD0)), 8);
  static const bool pkg = [] { const char* e = std::getenv("DBZ3_TEST_PKG"); return e && *e == '1'; }();
  if (cur + 1 >= count || !REX_LOAD_U8(x + 8156 + uint32_t(cur + 1))) {
    if (ready != -2) {
      ready = -2;
      wait = 0;
      REXLOG_INFO("dbz3 formas: prueba: tope en la forma {} de {}", cur, count);
    }
    // con P+K+G de verdad, en el tope se sigue pulsando con todo el ki: no debe pasar de ahi (fase bloqueada)
    if (pkg && ++wait % 90 == 0 && wait < 300) {
      REX_STORE_U32(x + 6100, REX_LOAD_U32(x + 6104));
      g_pkg.store(true);
      REXLOG_INFO("dbz3 formas: prueba: P+K+G en el tope (forma {}, ki lleno)", cur);
    }
    if (!pkg) ++wait;
    if (wait >= 300) phase = 1;   // 5 s quieto en el tope (captura) y luego el rival ataca
    return;
  }
  const uint32_t cap = REX_LOAD_U16(c372 + 212 + 20 * uint32_t(cur + 1));
  const uint32_t recs = REX_LOAD_U32(dbz3::GA(0x8237560C));
  const float need = cap && recs ? float(REX_LOAD_U8(recs + 40 * cap + 15)) * 100.0f : 1000.0f;
  uint32_t max_bits = REX_LOAD_U32(x + 6104);
  float max_ki;
  std::memcpy(&max_ki, &max_bits, 4);
  // DBZ3_TEST_KI_FALTA=1: los 2 primeros P+K+G con 0,6 barras menos de las que pide la forma (no debe subir)
  static int falta = std::getenv("DBZ3_TEST_KI_FALTA") ? 2 : 0, hold = 0;   // hold: cuadros tras la ultima
  if (hold > 0) --hold;
  set_ki(std::min(need + (falta > 0 || hold > 0 ? -600.0f : 10.0f), max_ki));
  auto call = [&](auto fn, uint32_t arg) {
    PPCContext c = ctx;
    c.r3.u64 = x;
    c.r4.u64 = arg;
    fn(c, base);
    return c.r3.s32;
  };
  if (ready != cur + 1) {
    ready = cur + 1;
    wait = 0;
    REXLOG_INFO("dbz3 formas: prueba: lista para la forma {} (capsula {}, ki {:.0f}; P+K+G daria la {})", ready, cap,
                need, call(DBZ3_CALL_EU(sub_82119108), 0));   // sub_82119108 solo calcula
  }
  // DBZ3_TEST_PKG=1: P+K+G de verdad, en el registro del mando 1 (PruebaMando), cada 1,5 s mientras esta libre
  if (pkg) {
    if (REX_LOAD_U16(x + 3244) <= 2 && ++wait % 90 == 0) {
      if (falta > 0)
        REXLOG_INFO("dbz3 formas: prueba: P+K+G con ki insuficiente ({:.0f} de {:.0f})", need - 600.0f, need);
      g_pkg.store(true);
      if (falta > 0 && --falta == 0) hold = 45;   // que el ultimo se lea aun sin ki; el siguiente, ya con su ki
    }
    return;
  }
  // DBZ3_TEST_FORMAS_AUTO=1: la prueba hace lo que sigue a P+K+G (el "P+K+G" de 3 teclas a la vez de pruebas.py
  // llega al juego, botones E000, pero su lector de comandos no lo toma como transformarse): forma de destino con
  // la decision nativa, carga de su modelo (sub_82100A10/B78, como sub_8210E100) y cambio nativo (sub_82115330,
  // el del relevo del Tag Team) cuando el modelo esta listo
  static const bool autopress = [] { const char* e = std::getenv("DBZ3_TEST_FORMAS_AUTO"); return e && *e == '1'; }();
  if (!autopress) return;
  if (pending < 0) {
    if (++wait < 120) return;
    wait = 0;
    const int target = call(DBZ3_CALL_EU(sub_82119108), 0);
    if (target <= cur) return;
    call(DBZ3_CALL_EU(sub_82100A10), uint32_t(target));
    call(DBZ3_CALL_EU(sub_82100B78), uint32_t(target));
    pending = target;
    REXLOG_INFO("dbz3 formas: prueba: carga la forma {} (modelo {:08X})", target,
                REX_LOAD_U32(x + 7608 + 4 * uint32_t(target)));
    return;
  }
  // listo como espera el relevo (tagteam.cpp Loaded): modelo con +36 a 0 y nodo del moveset leido (+1 a 0)
  const uint32_t model = REX_LOAD_U32(x + 7608 + 4 * uint32_t(pending));
  const uint32_t anm = REX_LOAD_U32(x + 7672 + 28 * uint32_t(pending) + 20);
  if (++wait < 90 || !model || REX_LOAD_U32(model + 36) || (anm && REX_LOAD_U8(anm + 1)) ||
      REX_LOAD_U16(x + 3244) > 2) {   // X+6048 es nulo en reposo y estas rutinas no lo usan
    if (wait % 300 == 0)
      REXLOG_INFO("dbz3 formas: prueba: espera la forma {}: modelo {:08X} (+36 {:X}) contexto {:08X} accion {}", pending,
                  model, model ? REX_LOAD_U32(model + 36) : 0, REX_LOAD_U32(x + 6048), REX_LOAD_U16(x + 3244));
    return;
  }
  REX_STORE_U32(x + 2984, uint32_t(pending));
  call(DBZ3_CALL_EU(sub_82115330), 0);
  pending = -1;
}

}  // namespace

// PRUEBA: lo llama el gancho del mando de combate.cpp (sub_8208FD58, registro de 184 B por usuario ya relleno):
// con una pulsacion pedida, P, K y G recien pulsados en el mismo cuadro (+12 pulsados, +14 recien pulsados,
// +72 + 4j cuadros pulsado), con los botones que la configuracion del jugador da a cada uno (mgr + (pad + 4382)
// * 8 + j = 1 P, 2 K, 4 G), como si se pulsaran los tres a la vez a mano.
namespace dbz3::formas {
void PruebaMando(uint8_t* base, uint32_t user) {
  if (user != 0 || !g_pkg.exchange(false)) return;
  const uint32_t mgr = REX_LOAD_U32(kMgr);
  if (!mgr) return;
  const uint32_t pad = REX_LOAD_U16(mgr + 2096 + 5028), r = dbz3::GA(0x8238B780) + 184 * user;
  uint16_t bits = 0;
  for (const uint8_t want : {1, 2, 4}) {
    for (uint32_t j = 0; j < 8; ++j) {
      if (REX_LOAD_U8(mgr + (pad + 4382) * 8 + j) != want) continue;
      bits |= uint16_t(0x100u << j);
      REX_STORE_U32(r + 72 + 4 * j, 1);
      break;
    }
  }
  REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
  REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
  REXLOG_INFO("dbz3 formas: prueba: P+K+G en el mando 1 (botones {:04X})", bits);
}
}  // namespace dbz3::formas

// Preparacion de las capsulas del luchador (r3 = X): despues, las formas de su capsula unica. Antes, fuera de su
// lista de combate (la copia del combate, no la partida) las capsulas que ya no estan en el catalogo: una lista
// guardada con la capsula unica y luego jugada sin ella (mod apagado, ISO) haria leer al juego un registro fuera
// del catalogo (sub_82112260: efecto +21 -> X+8125 + efecto). Asi se vuelve a Clasico sin tocar la partida (WL-14).
DBZ3_HOOK(sub_82112260, dbz3eu_sub_82112360) {
  const uint32_t x = ctx.r3.u32;
  if (const uint32_t rec = REX_LOAD_U32(x + 7412)) {
    const int n = std::clamp<int>(int16_t(REX_LOAD_U16(rec + 52)), 0, 7);
    const uint32_t count = dbz3::roster::CapsuleCount();
    int kept = 0;
    for (int k = 0; k < n; ++k) {
      const uint16_t cap = REX_LOAD_U16(rec + 54 + 2 * uint32_t(k));
      if (cap >= count) {
        REXLOG_INFO("dbz3 formas: capsula {} fuera del catalogo ({}): no entra en el combate", cap, count);
        continue;
      }
      // fuera de la macro: REX_STORE_U16 evalua la direccion dos veces (kept++ doblaba la lista y la cuenta)
      REX_STORE_U16(rec + 54 + 2 * uint32_t(kept), cap);
      ++kept;
    }
    if (kept != n) {
      for (int k = kept; k < 7; ++k) REX_STORE_U16(rec + 54 + 2 * uint32_t(k), 0);
      REX_STORE_U16(rec + 52, uint16_t(kept));
    }
  }
  orig(ctx, base);
  GrantForms(base, x);
}

// Primer paso de cada luchador en el fotograma de combate (r3 = X; lo llama sub_821100B0, que es de
// combate.cpp): solo para la prueba, con el jugador 1.
DBZ3_HOOK_EU(sub_82110B08) {
  static const bool test = std::getenv("DBZ3_TEST_FORMAS") != nullptr;
  if (test) {
    const uint32_t mgr = REX_LOAD_U32(kMgr);
    if (mgr && ctx.r3.u32 == mgr + 2096) TestTick(ctx, base);
  }
  orig(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// Nombres y panel de la capsula unica, hechos por el juego al arrancar desde los AFS del jugador (lo mismo que
// mod center hd/forma_unica.py, que queda de respaldo): banco de nombres de cada personaje con formas y los
// generales 2663/2684, ampliados con "Todas las transformaciones", y el panel de descripcion de Edit Skills, en
// los 6 data_*.afs. Van como entradas NUEVAS (n de entradas + 1024: no pisan nada ni chocan con el _roster) en
// mods/_forma_unica: el lector de AFS del runtime (rexruntime) solo sirve ficheros de mods/. Sin comprimir: el
// cargador del juego (sub_82084210) solo descomprime si ve la cabecera LZX (sub_820A4700); si no, copia tal cual.
// Se rehace solo si cambian los AFS, el _roster, los bancos o esta version (stamp.txt).
// ---------------------------------------------------------------------------------------------------------------
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include <rex/system/lzx.h>

#include "launcher/settings.h"
#include "mods.h"
#include "region.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace dbz3::formas {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
constexpr const char* kVersion = "forma_unica 2";
constexpr uint32_t kNamesShort = 2663, kNamesLong = 2684;

struct Texts {
  const char* name;
  const char* who;
  const char* what;
  const char* cmd;
  const char* note;
};
// los mismos que forma_unica.py (UTF-8)
const std::map<std::string, Texts> kTexts = {
    {"en", {"All Transformations", "Characters who transform", "Can use every transformation\nyou have unlocked",
            "P+K+G\nWith the Ki each form needs", "Takes half the slots of\nits separate capsules"}},
    {"es", {"Todas las transformaciones", "Personajes que se transforman",
            "Puede usar todas las\ntransformaciones desbloqueadas", "P+K+G\nCon el Ki que pide cada forma",
            "Ocupa la mitad de huecos\nque sus c\xC3\xA1psulas sueltas"}},
    {"fr", {"Toutes les transformations", "Personnages qui se transforment",
            "Peut utiliser toutes les\ntransformations d\xC3\xA9" "bloqu\xC3\xA9" "es", "P+K+G\nAvec le Ki de chaque forme",
            "Prend la moiti\xC3\xA9 des places\nde ses capsules s\xC3\xA9par\xC3\xA9" "es"}},
    {"de", {"Alle Verwandlungen", "Charaktere mit Verwandlungen", "Kann alle freigeschalteten\nVerwandlungen nutzen",
            "P+K+G\nMit dem Ki jeder Form", "Braucht die H\xC3\xA4lfte der Pl\xC3\xA4tze\nseiner einzelnen Kapseln"}},
    {"it", {"Tutte le trasformazioni", "Personaggi che si trasformano", "Pu\xC3\xB2 usare tutte le\ntrasformazioni sbloccate",
            "P+K+G\nCon il Ki di ogni forma", "Occupa met\xC3\xA0 degli spazi\ndelle sue capsule separate"}},
};

const std::map<std::string, Texts> kTextsTeleport = {
    {"en", {"Instant Transmission", "All characters",
            "Infinite teleport\nwithout Ki cost", "Guard towards opponent\nwhen attacked",
            "Consumes no Ki when\ncountering or vanishing"}},
    {"es", {"Transmisión Instantánea", "Todos los personajes",
            "Teletransporte infinito\nsin uso de KI", "Guardia hacia el rival\nal recibir un ataque",
            "Elimina el consumo de Ki\ndurante el teletransporte"}},
    {"fr", {"Transmission Instantanée", "Tous les personnages",
            "Téléportation infinie\nsans coût de Ki", "Garde vers l'adversaire\nlors d'une attaque",
            "Ne consomme pas de Ki\nlors de la téléportation"}},
    {"de", {"Momentane Teleportation", "Alle Charaktere",
            "Unendliche Teleportation\nohne Ki-Verbrauch", "Blocken zum Gegner\nbei einem Angriff",
            "Verbraucht kein Ki\nbeim Teleportieren"}},
    {"it", {"Trasmissione Istantanea", "Tutti i personaggi",
            "Teletrasporto infinito\nsenza consumo di Ki", "Parata verso l'avversario\nquando attaccato",
            "Non consuma Ki\ndurante il teletrasporto"}},
};

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
void PutBe32(Bytes& b, size_t at, uint32_t v) {
  for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (24 - 8 * k));
}
void AddBe32(Bytes& b, uint32_t v) {
  b.resize(b.size() + 4);
  PutBe32(b, b.size() - 4, v);
}
void AddBe16(Bytes& b, uint16_t v) {
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v));
}
void AddLe32(Bytes& b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
}
void PadTo(Bytes& b, size_t align) { b.resize((b.size() + align - 1) / align * align); }
int Pow2(int v, int min = 16) {
  int p = min;
  while (p < v) p *= 2;
  return p;
}
int Log2(int v) {
  int k = 0;
  while ((1 << k) < v) ++k;
  return k;
}

// Entrada de AFS: sin la cabecera LZX tal cual; con ella, sus tramas descomprimidas (como native_ui.cpp ReadEntry,
// pero bloque a bloque: cada bloque de +0x28 B sin comprimir (2 MB) es un LZX aparte; xbcompress parte asi los
// grandes, p. ej. el 2663 del _roster, que de una sola tira no sale)
Bytes Unpack(Bytes raw) {
  if (raw.size() < 0x30 || Be32(raw.data()) != 0x0FF512EE) return raw;
  const uint32_t window = Be32(&raw[0x10]), part = Be32(&raw[0x28]);
  const uint64_t total = uint64_t(Be32(&raw[0x18])) << 32 | Be32(&raw[0x1C]);
  if (total == 0 || total > (64ull << 20)) return {};
  Bytes out(total), stream, whole;
  bool per_block = part > 0;
  uint64_t done = 0;
  for (size_t p = 0x30; p + 4 <= raw.size();) {
    const size_t end = std::min(raw.size(), p + 4 + Be32(&raw[p]));
    if (end == p + 4) break;
    stream.clear();
    for (size_t q = p + 4; q + 2 <= end;) {
      size_t c;
      if (raw[q] == 0xFF) {
        if (q + 5 > end) break;
        c = Be16(&raw[q + 3]);
        q += 5;
      } else {
        c = Be16(&raw[q]);
        q += 2;
      }
      if (c == 0 || q + c > end) break;
      stream.insert(stream.end(), raw.begin() + std::ptrdiff_t(q), raw.begin() + std::ptrdiff_t(q + c));
      q += c;
    }
    whole.insert(whole.end(), stream.begin(), stream.end());
    const uint64_t n = std::min<uint64_t>(part, total - done);
    if (per_block && (n == 0 || lzx_decompress(stream.data(), stream.size(), out.data() + done, n, window, nullptr, 0) != 0))
      per_block = false;
    done += n;
    p = end;
  }
  if (per_block && done == total) return out;
  if (lzx_decompress(whole.data(), whole.size(), out.data(), out.size(), window, nullptr, 0) != 0) return {};
  return out;
}

uint32_t AfsCount(const fs::path& afs) {
  std::ifstream f(afs, std::ios::binary);
  uint8_t head[8] = {};
  return f.read(reinterpret_cast<char*>(head), 8) && std::memcmp(head, "AFS", 3) == 0 ? Le32(head + 4) : 0;
}

Bytes ReadFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

Bytes ReadEntry(const fs::path& afs, uint32_t n) {
  const uint32_t count = AfsCount(afs);
  std::ifstream f(afs, std::ios::binary | std::ios::ate);
  const uint64_t size = uint64_t(f.tellg());
  if (n >= count || size < 8 + 8ull * (n + 1)) return {};
  uint8_t ent[8];
  f.seekg(std::streamoff(8 + 8ull * n));
  if (!f.read(reinterpret_cast<char*>(ent), 8)) return {};
  const uint64_t off = Le32(ent), len = Le32(ent + 4);
  if (!len || len > (64ull << 20) || off + len > size) return {};
  Bytes raw(len);
  f.seekg(std::streamoff(off));
  if (!f.read(reinterpret_cast<char*>(raw.data()), std::streamsize(len))) return {};
  return Unpack(std::move(raw));
}

// La entrada n de ese data_*.afs tal como la sirve el mod (primer fichero de su carpeta, como el runtime), o vacio
Bytes ModEntry(const fs::path& mod, const std::string& afs, uint32_t n) {
  std::error_code ec;
  fs::path first;
  for (const auto& e : fs::directory_iterator(mod / "us" / afs / std::to_string(n), ec)) {
    const std::string name = e.path().filename().string();
    if (e.is_regular_file(ec) && !name.empty() && name[0] != '.' && name.find(".txt") == std::string::npos &&
        (first.empty() || e.path() < first))
      first = e.path();
  }
  return first.empty() ? Bytes{} : Unpack(ReadFile(first));
}

// ---- texto -> RGBA (stb_truetype, el de ImGui en una copia privada) ----
struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
};

struct Font {
  Bytes data;
  stbtt_fontinfo info{};
  float scale = 0;
  int ascent = 0, descent = 0;
  bool Load(std::initializer_list<const char*> names, float px) {
    std::error_code ec;
    for (const char* n : names) {
      for (const fs::path dir : {fs::path("C:/Windows/Fonts"), fs::path("/usr/share/fonts/truetype/dejavu"),
                                 fs::path("/usr/share/fonts/TTF")}) {
        if (!fs::exists(dir / n, ec)) continue;
        data = ReadFile(dir / n);
        if (data.empty() || !stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) continue;
        scale = stbtt_ScaleForMappingEmToPixels(&info, px);   // como el tamano de PIL (em en pixeles)
        stbtt_GetFontVMetrics(&info, &ascent, &descent, nullptr);
        return true;
      }
    }
    return false;
  }
};

std::vector<int> Utf8(const std::string& s) {
  std::vector<int> out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = uint8_t(s[i]);
    int cp = c, n = 0;
    if (c >= 0xF0) {
      cp = c & 7, n = 3;
    } else if (c >= 0xE0) {
      cp = c & 15, n = 2;
    } else if (c >= 0xC0) {
      cp = c & 31, n = 1;
    }
    for (++i; n > 0 && i < s.size(); --n, ++i) cp = cp << 6 | (uint8_t(s[i]) & 63);
    out.push_back(cp);
  }
  return out;
}

float Width(const Font& f, const std::string& s) {
  float w = 0;
  for (int cp : Utf8(s)) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
    w += float(adv) * f.scale;
  }
  return w;
}

// Cobertura (0..255) del texto con la linea base en (x, base) sobre una mascara w x h
void Draw(const Font& f, const std::string& s, float x, float base, int w, int h, std::vector<uint8_t>& mask) {
  for (int cp : Utf8(s)) {
    int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
    const float sub = x - std::floor(x);
    stbtt_GetCodepointBitmapBoxSubpixel(&f.info, cp, f.scale, f.scale, sub, 0, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      std::vector<uint8_t> g(size_t(gw) * size_t(gh));
      stbtt_MakeCodepointBitmapSubpixel(&f.info, g.data(), gw, gh, gw, f.scale, f.scale, sub, 0, cp);
      const int ox = int(std::floor(x)) + x0, oy = int(std::lround(base)) + y0;
      for (int yy = 0; yy < gh; ++yy) {
        for (int xx = 0; xx < gw; ++xx) {
          const int px = ox + xx, py = oy + yy;
          if (px < 0 || py < 0 || px >= w || py >= h) continue;
          uint8_t& m = mask[size_t(py) * size_t(w) + size_t(px)];
          m = std::max(m, g[size_t(yy) * size_t(gw) + size_t(xx)]);
        }
      }
    }
    x += float(adv) * f.scale;
  }
}

// Nombre como los nativos: blanco con borde oscuro de 2 px, alto 28, centrado en vertical (capsulas.render_name)
Image RenderName(const Font& f, const std::string& s, int h = 28) {
  Image img;
  img.w = std::min(int(Width(f, s)) + 10, 508);
  img.h = h;
  std::vector<uint8_t> text(size_t(img.w) * size_t(h));
  const float base = float(h) / 2.0f + float(f.ascent + f.descent) * f.scale / 2.0f;   // ancla "lm" de PIL
  Draw(f, s, 3, base, img.w, h, text);
  img.rgba.assign(size_t(img.w) * size_t(h) * 4, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < img.w; ++x) {
      int stroke = 0;   // borde: la maxima cobertura en un radio de 2
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          if (dx * dx + dy * dy <= 5 && y + dy >= 0 && y + dy < h && x + dx >= 0 && x + dx < img.w)
            stroke = std::max<int>(stroke, text[size_t(y + dy) * size_t(img.w) + size_t(x + dx)]);
        }
      }
      const int t = text[size_t(y) * size_t(img.w) + size_t(x)];
      uint8_t* p = &img.rgba[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
      p[0] = uint8_t((255 * t + 30 * (255 - t)) / 255);
      p[1] = uint8_t((255 * t + 20 * (255 - t)) / 255);
      p[2] = uint8_t((255 * t + 20 * (255 - t)) / 255);
      p[3] = uint8_t(std::max(stroke, t));
    }
  }
  return img;
}

// Lineas del panel (capsulas.render_lines): texto oscuro de 16 px, 208 de ancho, 19 por linea, 6 arriba
Image RenderLines(const Font& f, const std::string& text) {
  Image img;
  if (text.empty()) {
    img.w = 16, img.h = 24;
    img.rgba.assign(16 * 24 * 4, 0);
    return img;
  }
  std::vector<std::string> lines;
  std::stringstream paras(text);
  for (std::string para; std::getline(paras, para, '\n');) {
    std::stringstream words(para);
    std::string line;
    for (std::string w; words >> w;) {
      const std::string cand = line.empty() ? w : line + " " + w;
      if (!line.empty() && Width(f, cand) > 208) {
        lines.push_back(line);
        line = w;
      } else {
        line = cand;
      }
    }
    lines.push_back(line);
  }
  float widest = 0;
  for (const auto& l : lines) widest = std::max(widest, Width(f, l));
  img.w = std::max(16, std::min(208 + 8, int(widest) + 6));
  img.h = 6 + 19 * int(lines.size()) + 5;
  std::vector<uint8_t> mask(size_t(img.w) * size_t(img.h));
  for (size_t k = 0; k < lines.size(); ++k)
    Draw(f, lines[k], 1, 6 + 19.0f * float(k) + float(f.ascent) * f.scale, img.w, img.h, mask);
  img.rgba.assign(size_t(img.w) * size_t(img.h) * 4, 0);
  for (size_t i = 0; i < mask.size(); ++i) {
    img.rgba[4 * i] = 18, img.rgba[4 * i + 1] = 10, img.rgba[4 * i + 2] = 0, img.rgba[4 * i + 3] = mask[i];
  }
  return img;
}

// ---- DXT3 + #AZT (los mismos formatos que capsulas.py / texture_b3.py) ----
uint16_t To565(int r, int g, int b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
std::array<int, 3> From565(uint16_t c) { return {(c >> 11 & 31) * 255 / 31, (c >> 5 & 63) * 255 / 63, (c & 31) * 255 / 31}; }

// DDS DXT3 de la imagen en un lienzo W x H (W potencia de 2 >= 16; H 32 los nombres, >= 64 los del panel)
Bytes TexBlob(const Image& img, bool tall, int& W, int& H) {
  W = Pow2(img.w);
  H = tall ? std::max(64, Pow2(img.h)) : 32;
  Bytes out = {'D', 'D', 'S', ' ', 0x7c, 0, 0, 0, 0x07, 0x10, 0x08, 0};
  for (uint32_t v : {uint32_t(H), uint32_t(W), uint32_t(H * W), 0u, 0u}) AddLe32(out, v);
  out.resize(out.size() + 44);
  for (uint8_t v : {0x20, 0, 0, 0, 0x04, 0, 0, 0}) out.push_back(v);
  for (char c : {'D', 'X', 'T', '3'}) out.push_back(uint8_t(c));
  out.resize(out.size() + 20);
  AddLe32(out, 0x1000);
  out.resize(out.size() + 16);
  auto px = [&](int x, int y, int c) -> int {
    return x < img.w && y < img.h ? img.rgba[(size_t(y) * size_t(img.w) + size_t(x)) * 4 + size_t(c)] : 0;
  };
  for (int by = 0; by < H; by += 4) {
    for (int bx = 0; bx < W; bx += 4) {
      auto a = [&](int k) { return std::min(15, int(px(bx + k % 4, by + k / 4, 3) / 16.0 + 0.5)); };
      for (int i = 0; i < 16; i += 2) out.push_back(uint8_t(a(i + 1) << 4 | a(i)));
      int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
      for (int k = 0; k < 16; ++k) {
        for (int c = 0; c < 3; ++c) {
          lo[c] = std::min(lo[c], px(bx + k % 4, by + k / 4, c));
          hi[c] = std::max(hi[c], px(bx + k % 4, by + k / 4, c));
        }
      }
      uint16_t c0 = To565(hi[0], hi[1], hi[2]), c1 = To565(lo[0], lo[1], lo[2]);
      if (c0 < c1) std::swap(c0, c1);
      const auto p0 = From565(c0), p1 = From565(c1);
      std::array<std::array<int, 3>, 4> pal = {p0, p1, std::array<int, 3>{}, std::array<int, 3>{}};
      for (int c = 0; c < 3; ++c) pal[2][c] = (2 * p0[c] + p1[c]) / 3, pal[3][c] = (p0[c] + 2 * p1[c]) / 3;
      uint32_t idx = 0;
      for (int k = 0; k < 16 && c0 != c1; ++k) {
        int best = 0, bd = 1 << 30;
        for (int i = 0; i < 4; ++i) {
          int d = 0;
          for (int c = 0; c < 3; ++c) {
            const int e = px(bx + k % 4, by + k / 4, c) - pal[size_t(i)][size_t(c)];
            d += e * e;
          }
          if (d < bd) bd = d, best = i;
        }
        idx |= uint32_t(best) << (2 * k);
      }
      for (uint16_t c : {c0, c1}) out.push_back(uint8_t(c)), out.push_back(uint8_t(c >> 8));
      AddLe32(out, idx);
    }
  }
  return out;
}

// Registro de 0x30 B de una textura del #AZT (id, 0x21, tamanos, datos, vram, 0x400)
void AddEntry(Bytes& b, uint32_t id, const Image& img, int W, int log2h, uint32_t data, uint32_t len, uint32_t vram) {
  AddBe32(b, id);
  AddBe32(b, 0x21);
  AddBe16(b, uint16_t(img.w));
  AddBe16(b, uint16_t(img.h));
  AddBe16(b, uint16_t(Log2(W)));
  AddBe16(b, uint16_t(log2h));
  AddBe16(b, uint16_t(img.w));
  AddBe16(b, uint16_t(img.h));
  AddBe32(b, data);
  AddBe32(b, len);
  for (uint32_t v : {1u, 0u, 0u, vram, 0x400u}) AddBe32(b, v);
}

// capsulas.extend_bank: anade texturas (ID -> imagen) a un #AZT indexado por ID, alargando su indice
Bytes ExtendBank(const Bytes& bank, const std::map<uint32_t, Image>& items) {
  if (bank.size() < 0x20 || std::memcmp(bank.data(), "#AZT", 4) != 0 || items.empty()) return {};
  const uint32_t n = Be32(&bank[0x10]), idx = Be32(&bank[0x14]), first = Be32(&bank[0x18]);
  if (idx + 4ull * n > bank.size() || items.begin()->first < first) return {};
  std::vector<uint32_t> offs(n);
  for (uint32_t k = 0; k < n; ++k) offs[k] = Be32(&bank[idx + 4 * k]);
  const uint32_t n_new = std::max<uint32_t>(n, items.rbegin()->first - first + 1);
  offs.resize(n_new, 0);
  Bytes out = bank;
  PadTo(out, 0x80);
  const uint32_t last = *std::max_element(offs.begin(), offs.end());
  uint32_t vram = last + 0x28 <= bank.size() ? Be32(&bank[last + 0x24]) + Be32(&bank[last + 0x18]) / 0x20 : 0;
  std::vector<std::pair<uint32_t, Bytes>> ents;
  for (const auto& [id, img] : items) {
    int W = 0, H = 0;
    const Bytes blob = TexBlob(img, false, W, H);
    const uint32_t at = uint32_t(out.size());
    out.insert(out.end(), blob.begin(), blob.end());
    PadTo(out, 0x80);
    Bytes e;
    AddEntry(e, id, img, W, 6, at, uint32_t(blob.size()), vram);
    vram += uint32_t(blob.size()) / 0x20;
    ents.emplace_back(id, std::move(e));
  }
  PadTo(out, 0x10);
  for (const auto& [id, e] : ents) {
    offs[id - first] = uint32_t(out.size());
    out.insert(out.end(), e.begin(), e.end());
  }
  const uint32_t new_idx = uint32_t(out.size());
  for (uint32_t o : offs) AddBe32(out, o);
  PadTo(out, 0x10);
  PutBe32(out, 0x10, n_new);
  PutBe32(out, 0x14, new_idx);
  return out;
}

uint32_t Crc32(const Bytes& b, size_t from) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = from; i < b.size(); ++i) {
    c ^= b[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

// capsulas.build_desc: #AZT del panel de Edit Skills (5 texturas en columna, IDs 0..4)
Bytes BuildDesc(const std::array<Image, 5>& imgs) {
  constexpr uint32_t n = 5, idx = 0x20, first = (idx + 4 * n + 0xF) / 0x10 * 0x10;
  const uint32_t data_base = (first + 0x30 * n + 0x7F) / 0x80 * 0x80;
  Bytes ent, body;
  std::vector<uint32_t> index;
  uint32_t vram = 0;
  for (uint32_t k = 0; k < n; ++k) {
    int W = 0, H = 0;
    const Bytes blob = TexBlob(imgs[k], true, W, H);
    index.push_back(first + uint32_t(ent.size()));
    AddEntry(ent, k, imgs[k], W, Log2(H), data_base + uint32_t(body.size()), uint32_t(blob.size()), vram);
    vram += uint32_t(blob.size()) / 0x20;
    body.insert(body.end(), blob.begin(), blob.end());
    PadTo(body, 0x80);
  }
  Bytes out = {'#', 'A', 'Z', 'T'};
  for (uint32_t v : {0u, 0u, 0u, n, idx, 0u, 0u}) AddBe32(out, v);
  for (uint32_t o : index) AddBe32(out, o);
  out.resize(first);
  out.insert(out.end(), ent.begin(), ent.end());
  out.resize(data_base);
  out.insert(out.end(), body.begin(), body.end());
  PutBe32(out, 0x1C, Crc32(out, data_base));
  return out;
}

bool WriteFile(const fs::path& p, const Bytes& b) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  return f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())).good();
}

std::string LangOf(const std::string& afs) {
  for (const auto& [key, lang] : {std::pair{"spn", "es"}, std::pair{"fra", "fr"}, std::pair{"ger", "de"},
                                  std::pair{"ita", "it"}})
    if (afs.find(key) != std::string::npos) return lang;
  return "en";
}

}  // namespace

bool PrepareNamesMod(const std::vector<std::pair<int, uint32_t>>& banks, const std::string& roster_stamp) {
  const fs::path root = dbz3::HostDataRoot();
  if (root.empty() || banks.empty()) return false;
  std::error_code ec;
  fs::path dir = root / (dbz3::settings::Region() == "eu" ? "eu" : "us");
  if (!fs::exists(dir / "data_usi.afs", ec) && !fs::exists(dir / "data_eng.afs", ec)) dir = root / "us";
  // solo el data_*.afs del idioma del juego (los 6 ocupaban ~110 MB sin comprimir); otro idioma = se rehace
  const int32_t lang = dbz3::settings::Language();
  const char* active = lang == 3 ? "data_ger.afs" : lang == 4 ? "data_fra.afs" : lang == 5 ? "data_spn.afs"
                     : lang == 6 ? "data_ita.afs" : "data_usi.afs";
  std::vector<std::pair<std::string, fs::path>> afss;
  for (const char* n : {active, "data_eng.afs"}) {   // EU no trae data_usi: su ingles es data_eng
    if (afss.empty() && fs::exists(dir / n, ec)) afss.emplace_back(n, dir / n);
  }
  if (afss.empty()) {
    REXLOG_INFO("dbz3 formas: sin data_*.afs en {} (imagen de disco?): la capsula unica no tendra nombre", dir.string());
    return false;
  }
  const fs::path mods = dbz3::ModsRoot(), out = mods / "_forma_unica", roster = mods / "_roster";
  if (fs::exists(out / ".disabled", ec) || fs::exists(mods / "_forma_unica.disabled", ec)) return false;   // apagada a mano
  std::string stamp = std::string(kVersion) + "|" + roster_stamp;
  uint32_t count = 0;
  for (const auto& [n, p] : afss) {
    stamp += "|" + n + ":" + std::to_string(fs::file_size(p, ec)) + ":" +
             std::to_string(static_cast<long long>(fs::last_write_time(p, ec).time_since_epoch().count()));   // Linux: __int128
    count = std::max(count, AfsCount(p));
  }
  for (const auto& [id, fid] : banks) stamp += "|" + std::to_string(id) + ":" + std::to_string(fid);
  {
    std::ifstream f(out / "stamp.txt");
    std::string old;
    if (std::getline(f, old) && old == stamp && fs::exists(out / "roster.toml", ec)) return false;
    // hecha a mano con forma_unica.py (sin stamp.txt): vale mientras sea del mismo _roster
    std::ifstream t(out / "roster.toml");
    const std::string toml((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    if (!fs::exists(out / "stamp.txt", ec) && toml.find("roster_hash = \"" + roster_stamp + "\"") != std::string::npos)
      return false;
  }
  Font name_font, desc_font;
  if (!name_font.Load({"ARLRDBD.TTF", "arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"}, 18) ||
      !desc_font.Load({"arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"}, 16)) {
    REXLOG_WARN("dbz3 formas: sin fuente de texto: la capsula unica no tendra nombre");
    return false;
  }
  const uint32_t desc = count + 1024;
  const uint32_t desc_teleport = desc + 1;
  struct Plan {
    uint32_t from, to;
    std::vector<int> ids;   // personajes que usan ese banco (vacio: los generales 2663/2684, con todas)
    bool from_roster;
  };
  std::vector<Plan> plan;
  std::map<uint32_t, std::vector<int>> owner;   // banco -> personajes (un port sin banco propio usa el del donante)
  for (const auto& [id, fid] : banks) owner[fid].push_back(id);
  for (const auto& [fid, ids] : owner) plan.push_back({fid, uint32_t(desc + 2 + plan.size()), ids, false});
  for (const uint32_t general : {kNamesShort, kNamesLong})
    plan.push_back({general, uint32_t(desc + 2 + plan.size()), {}, false});
  const fs::path tmp = mods / ".forma_unica_nueva";   // con punto: los mods y el runtime la ignoran
  fs::remove_all(tmp, ec);
  size_t written = 0;
  for (const auto& [afs_name, path] : afss) {
    const Texts& t = kTexts.at(LangOf(afs_name));
    const Image name = RenderName(name_font, t.name);
    const std::array<Image, 5> panel = {name, RenderLines(desc_font, t.who), RenderLines(desc_font, t.what),
                                        RenderLines(desc_font, t.cmd), RenderLines(desc_font, t.note)};
    written += WriteFile(tmp / "us" / afs_name / std::to_string(desc) / "forma_unica.bin", BuildDesc(panel));

    const Texts& tt = kTextsTeleport.at(LangOf(afs_name));
    const Image name_teleport = RenderName(name_font, tt.name);
    const std::array<Image, 5> panel_teleport = {name_teleport, RenderLines(desc_font, tt.who), RenderLines(desc_font, tt.what),
                                                 RenderLines(desc_font, tt.cmd), RenderLines(desc_font, tt.note)};
    written += WriteFile(tmp / "us" / afs_name / std::to_string(desc_teleport) / "forma_unica.bin", BuildDesc(panel_teleport));

    for (auto& p : plan) {
      std::map<uint32_t, Image> items;
      for (const auto& [id, fid] : banks)
        if (p.ids.empty() || std::find(p.ids.begin(), p.ids.end(), id) != p.ids.end()) items[forms::UnifiedId(id)] = name;
      // FIXME(refactor): Extraer el motor de rasterizado de texto #AZT (Font, RenderName, ExtendBank, BuildDesc)
      // a un modulo independiente (ej. src/capsule_ui.h y src/capsule_ui.cpp) para desacoplar transformaciones.cpp
      // de capsulas genericas o pasivas como Transmision Instantanea (ID 1970).
      items[1970] = name_teleport;
      Bytes bank = ModEntry(roster, afs_name, p.from);   // el del _roster gana, como en el juego
      p.from_roster = p.from_roster || !bank.empty();
      if (bank.empty()) bank = ReadEntry(path, p.from);
      const Bytes ext = ExtendBank(bank, items);
      if (!ext.empty()) written += WriteFile(tmp / "us" / afs_name / std::to_string(p.to) / "forma_unica.bin", ext);
    }
  }
  std::string pairs[2];
  for (const auto& p : plan) {
    std::string& s = pairs[p.from_roster ? 1 : 0];
    s += (s.empty() ? "" : ", ") + std::to_string(p.from) + ", " + std::to_string(p.to);
  }
  std::ofstream(tmp / "roster.toml") << "# GENERADO por el juego (src/transformaciones.cpp) desde tus AFS: se rehace solo.\n"
                                        "[forma_unica]\ndescripcion = " << desc << "\ndescripcion_teleport = " << desc_teleport
                                     << "\nbancos = [" << pairs[0] << "]\nbancos_roster = [" << pairs[1]
                                     << "]\nroster_hash = \"" << roster_stamp << "\"\ntodas = true\n";
  std::ofstream(tmp / "LEEME.txt") << "Nombre y panel de la capsula 'Todas las transformaciones', hechos por el juego\n"
                                      "desde tus archivos del juego. No van con la descarga; si apartas esta carpeta,\n"
                                      "el juego la vuelve a crear al arrancar.\n";
  std::ofstream(tmp / "stamp.txt") << stamp << "\n";
  if (fs::exists(out / "stamp.txt", ec)) {
    fs::remove_all(out, ec);   // la cache anterior, hecha por el juego (se rehace igual)
  } else if (fs::exists(out, ec)) {   // hecha por otro (forma_unica.py o a mano): no se borra, a mods_respaldo
    const fs::path bak = mods.parent_path() / "mods_respaldo";
    fs::create_directories(bak, ec);
    fs::rename(out, bak / ("_forma_unica_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())), ec);
  }
  ec.clear();
  fs::rename(tmp, out, ec);
  REXLOG_INFO("dbz3 formas: _forma_unica hecha por el juego ({} ficheros, {} bancos, panel {}){}", written, plan.size(),
              desc, ec ? " ERROR al publicar: " + ec.message() : std::string());
  return !ec;
}

}  // namespace dbz3::formas
