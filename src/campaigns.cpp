// dbz3 - Campanas de Dragon Universe hechas con mods.
//
// El juego traduce cada "codigo de historia" a un contenedor #LST de data_cmn con una tabla
// propia (sub_82156F30, EU sub_82156EF0; ver docs/11_campanas/CAMPANAS.md): 1-99, 100-106,
// 2000-2399, 5000-6199 (un bloque de 38 escenas por historia), 8192+, 16384+. Un mod con
// "campana.toml" puede llevar cualquier codigo a otro contenedor (uno suyo, anadido a data_cmn,
// o uno original), sin tocar los de los demas:
//
//   [codigos]
//   5500 = 3990      # escena 0 de la historia 5500 -> contenedor 3990
//   5501 = 3991
//
// Los textos de las escenas son imagenes por idioma (#AZT en data_usi/data_eng/...), que el
// mod sustituye o anade como cualquier otra entrada.
//
// Y puede sumar personajes a la rueda de Dragon Universe (la del juego solo deja 11, fijos en
// sub_8217D2D0); cada uno juega la historia que el juego guarde (o la del mod) para su casilla:
//
//   [rueda]
//   personajes = 27      # Freezer (IDs separados por comas)
#include <atomic>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include <rex/runtime.h>
#include "launcher/settings.h"
#include "mods.h"

namespace dbz3::heroe { int Saga(); }   // heroe.cpp: saga siguiente del heroe activo
namespace dbz3::campaigns {
namespace {

std::once_flag g_once;
std::unordered_map<int32_t, uint32_t> g_codes;   // codigo -> contenedor
// [codigos_historia] de cada mod, con los IDs de su [rueda]: solo mientras se juega la casilla de ese mod, asi
// varias campanas (cada una en su casilla) pueden copiar las mismas escenas compartidas (102, 1, 2100...)
std::vector<std::pair<std::vector<int>, std::unordered_map<int32_t, uint32_t>>> g_story_codes;
std::vector<int> g_wheel_ids;                    // personajes de mas en la rueda de DU
// C3 (Heroe en DU): casillas de la campana que NO salen en la rueda (el Heroe es exclusivo de su modo: se entra con
// hub::RequestDu, que salta la rueda): [rueda] casillas_virtuales = [36]; [casilla_luchador] 36 = 63 (casilla -> ID del
// luchador en el mapa, los menus y el combate de DU); [intro_casilla] 36 = 102 (la intro con la que empieza)
std::vector<int> g_virtual_slots;
std::unordered_map<uint32_t, uint32_t> g_slot_fighter, g_slot_intro;
std::atomic<int> g_du_sesion{-1};         // C4: casilla del Heroe mientras su sesion de DU sigue (para reanudar tras guardar)
std::atomic<bool> g_du_reanudar{false};
std::atomic<int> g_saga_ganada{-1};      // C4: ultima saga (indice 0..) con un combate ganado en esta sesion: la que se cierra   // el cierre de saga acaba de guardar (escena 16384.5): el juego vuelve al titulo de DU
std::atomic<bool> g_du_cover{false};   // pantalla en negro mientras la entrada directa pasa titulo, menu y rueda de DU
std::atomic<int> g_du_auto{-1};   // casilla que la rueda de DU confirma sola (hub::RequestDu); -1 = nada
std::unordered_map<uint32_t, size_t> g_word_owner;   // palabra de [modelo_mapa]/[traje_mapa]/[animacion_mapa] -> campana (indice de g_story_codes)
std::unordered_map<uint32_t, int> g_start_form;  // [forma_jugador]: palabra del combate -> forma inicial
std::unordered_map<uint32_t, int> g_rival_suit;  // [traje_rival]: palabra del combate -> traje del rival
std::unordered_map<uint32_t, int> g_map_model;   // [modelo_mapa]: palabra del combate -> modelo en el mapa
std::unordered_map<uint32_t, int> g_map_suit;    // [traje_mapa]: palabra del combate -> traje en el mapa
std::unordered_map<uint32_t, int> g_map_anim;    // [animacion_mapa]: palabra -> casilla cuyo vuelo del mapa se usa
std::atomic<uint32_t> g_last_battle{0xFFFFFFFFu}; // ultimo combate lanzado (palabra)

// DEPURACION (DBZ3_TEST_DU_ESTADOS=1): registra los cambios del byte de pantalla (0x824BA110) y del estado de la tarea de escenas
// (tarea 0x8247A9A0, +28): para saber que estado del juego es el titulo de DU, su menu y la rueda (entrada directa del Heroe)
void SondaEstados() {
  if (!std::getenv("DBZ3_TEST_DU_ESTADOS")) return;
  std::thread([] {
    uint32_t last_s = 0xFFFFFFFF, last_t = 0xFFFFFFFF, last_r = 0xFFFFFFFF;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      rex::Runtime* rt = rex::Runtime::instance();
      uint8_t* b = rt ? rt->virtual_membase() : nullptr;
      if (!b) continue;
      auto be = [&](uint32_t a) { return __builtin_bswap32(*reinterpret_cast<uint32_t*>(b + a)); };
      const uint32_t scr = b[0x824BA110u], task = be(0x8247A9A0u), st = task ? be(task + 28) : 0, req = be(0x8247A990u);
      if (scr != last_s || st != last_t || req != last_r) {
        last_s = scr, last_t = st, last_r = req;
        REXLOG_INFO("dbz3 du-estados: pantalla {} tarea {:08X} estado {:08X} peticion {}", scr, task, st, int32_t(req));
      }
    }
  }).detach();
}

void Load() {
  static std::once_flag sonda;
  std::call_once(sonda, SondaEstados);
  for (const auto& mod : dbz3::ListMods(false)) {
    if (!mod.enabled) continue;
    std::ifstream f(dbz3::ModsRoot() / mod.name / "campana.toml");
    if (!f) continue;
    std::string line;
    bool in_codes = false, in_story = false, in_wheel = false, in_fighter = false, in_intro = false, in_forms = false, in_suits = false, in_models = false, in_map_suits = false, in_map_anims = false;
    int n = 0;
    g_story_codes.emplace_back();
    while (std::getline(f, line)) {
      const auto hash = line.find('#');
      if (hash != std::string::npos) line.resize(hash);
      // cabecera de seccion solo si la linea EMPIEZA por '[': `personajes = [27]` (lista TOML) no es una
      // (2026-10-15: con el '[' en cualquier sitio la [rueda] quedaba vacia y al cargar salia Goku)
      if (const auto first = line.find_first_not_of(" \t"); first != std::string::npos && line[first] == '[') {
        in_codes = line.find("[codigos]") != std::string::npos;
        in_story = line.find("[codigos_historia]") != std::string::npos;
        in_wheel = line.find("[rueda]") != std::string::npos;
        in_fighter = line.find("[casilla_luchador]") != std::string::npos;
        in_intro = line.find("[intro_casilla]") != std::string::npos;
        in_forms = line.find("[forma_jugador]") != std::string::npos;
        in_suits = line.find("[traje_rival]") != std::string::npos;
        in_models = line.find("[modelo_mapa]") != std::string::npos;
        in_map_suits = line.find("[traje_mapa]") != std::string::npos;
        in_map_anims = line.find("[animacion_mapa]") != std::string::npos;
        continue;
      }
      const auto eq = line.find('=');
      if ((in_fighter || in_intro) && eq != std::string::npos) {   // clave = valor, los dos enteros
        const long k = std::strtol(line.c_str(), nullptr, 10), v = std::strtol(line.c_str() + eq + 1, nullptr, 10);
        if (k >= 0 && k < 64 && v >= 0 && v < 200) (in_intro ? g_slot_intro : g_slot_fighter)[uint32_t(k)] = uint32_t(v);
        continue;
      }
      if (in_wheel && eq != std::string::npos && line.find("casillas_virtuales") < eq) {
        for (const char* p = line.c_str() + eq + 1; *p;) {
          char* end = nullptr;
          const long v = std::strtol(p, &end, 10);
          if (end == p) {
            ++p;
            continue;
          }
          if (v >= 0 && v < 38) {
            g_virtual_slots.push_back(int(v));
            g_story_codes.back().first.push_back(-1 - int(v));   // negativo = casilla directa (WheelSlot)
          }
          p = end;
        }
        continue;
      }
      if (in_wheel && eq != std::string::npos && line.find("personajes") < eq) {
        for (const char* p = line.c_str() + eq + 1; *p;) {
          char* end = nullptr;
          const long id = std::strtol(p, &end, 10);
          if (end == p) {
            ++p;
            continue;
          }
          if (id >= 0 && id < 64) {
            g_wheel_ids.push_back(int(id));
            g_story_codes.back().first.push_back(int(id));
          }
          p = end;
        }
        continue;
      }
      if ((in_forms || in_suits || in_models || in_map_suits || in_map_anims) && eq != std::string::npos) {   // palabra = forma / traje / modelo
        const long v = std::strtol(line.c_str() + eq + 1, nullptr, 10);
        if (v < -1 || v > 255) {   // van a un byte del juego: fuera de rango seria otro personaje o basura
          REXLOG_WARN("dbz3 campanas: '{}': valor fuera de rango ignorado: {}", mod.name, line);
          continue;
        }
        const uint32_t word = uint32_t(std::strtoul(line.c_str(), nullptr, 10));
        (in_forms ? g_start_form : in_suits ? g_rival_suit : in_models ? g_map_model : in_map_suits ? g_map_suit : g_map_anim)[word] = int(v);
        if (in_models || in_map_suits || in_map_anims) g_word_owner[word] = g_story_codes.size() - 1;
        continue;
      }
      if ((!in_codes && !in_story) || eq == std::string::npos) continue;
      char* end = nullptr;
      const long code = std::strtol(line.c_str(), &end, 10);
      const long cont = std::strtol(line.c_str() + eq + 1, &end, 10);
      if (code <= 0 || cont <= 0) continue;
      (in_story ? g_story_codes.back().second : g_codes)[int32_t(code)] = uint32_t(cont);
      ++n;
    }
    REXLOG_INFO("dbz3 campanas: '{}' cambia {} escenas de Dragon Universe", mod.name, n);
  }
}

}  // namespace
}  // namespace dbz3::campaigns

namespace dbz3::campaigns {
std::chrono::steady_clock::time_point g_map_since;
// Forma con la que empieza tu personaje el combate de historia que se acaba de lanzar
// ([forma_jugador] del campana.toml del mod), o -1.
int PlayerStartForm() {
  const auto it = g_start_form.find(g_last_battle.load());
  return it == g_start_form.end() ? -1 : it->second;
}
// Traje del rival en ese combate ([traje_rival]: 0 el normal), o -1.
int RivalSuit() {
  const auto it = g_rival_suit.find(g_last_battle.load());
  return it == g_rival_suit.end() ? -1 : it->second;
}
// En el menu principal (fuera de Dragon Universe) se olvida el ultimo combate de historia. Antes no se
// borraba nunca: tras un combate de una historia de mod con [forma_jugador] / [traje_rival], el Duelo,
// el Tag Team o el online siguientes empezaban con esa forma y ese traje del rival (2026-10-15).
void ForgetStoryBattle() {
  // PRUEBA (tools/pruebas.py historia_y_duelo): DBZ3_TEST_STORY_WORD=palabra hace como si se volviera de
  // Dragon Universe tras ese combate (la primera vez que se llega al menu principal); DBZ3_TEST_KEEP_STORY=1
  // deja el comportamiento de antes (no se olvida) para ver el fallo
  if (static const char* w = std::getenv("DBZ3_TEST_STORY_WORD"); w) {
    static bool once = false;
    if (!once) {
      once = true;
      g_last_battle = uint32_t(std::strtoul(w, nullptr, 0));
      REXLOG_INFO("dbz3 campanas: PRUEBA: ultimo combate de historia {:08X}", g_last_battle.load());
    }
  }
  if (static const bool keep = std::getenv("DBZ3_TEST_KEEP_STORY") != nullptr; keep) return;
  if (g_last_battle.exchange(0xFFFFFFFFu) != 0xFFFFFFFFu)
    REXLOG_INFO("dbz3 campanas: menu principal: se olvida el ultimo combate de historia");
}
// casilla de DU de una entrada de [rueda]: >= 0 = ID de personaje (tabla ID -> casilla), < 0 = casilla directa (-1 - casilla)
uint32_t WheelSlot(uint8_t* base, int e) {
  return e < 0 ? uint32_t(-1 - e) : uint32_t(REX_LOAD_U16(dbz3::GA(0x82020668) + uint32_t(e) * 2));
}
// Luchador del jugador en el combate de historia que arranca (el Heroe: [casilla_luchador]); -1 = el del juego.
// El juego lo saca de la tabla casilla -> ID del select (casilla 36 = un Saibaman, medido 2026-10-22).
// C4 (sagas 4-8 del Heroe): los combates de una campana son 5400-5799 (bloques del juego) y, para las sagas virtuales,
// 12500-12999 (campana.py: 12500 + 100*(saga - 4) + casilla); (code - 5000) % 100 == casilla vale en los dos rangos.
bool CodigoCombate(uint32_t code) { return (code >= 5400 && code < 5800) || (code >= 12500 && code < 13000); }
// H4: el bloque 5400 (+casilla) es la saga 4 REAL (indice 3: campana.py usa el bloque 0 tras 5100-5300); 5500-5700 = sagas 1-3; virtuales (saga 5+) = 4 + ...
int SagaDeCombate(uint32_t code) { return code < 5800 ? (code < 5500 ? 3 : int((code - 5400) / 100) - 1) : 4 + int((code - 12500) / 100); }
// Intro de la saga sig (1-based): 102/104/106 del juego; la 4.a es la 100 (bloque 0); mas alla, el mapa nativo (sin jugar aun).
uint32_t IntroDeSaga(int sig) { return sig <= 3 ? uint32_t(100 + 2 * std::max(1, sig)) : sig == 4 ? 100u : 5336u; }
int DuFighter(uint8_t* base) {
  if (g_slot_fighter.empty() || dbz3::g_guest_eu) return -1;
  const uint32_t w = g_last_battle.load(), code = w >> 16, slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  if (w == 0xFFFFFFFFu || !CodigoCombate(code) || (code - 5000) % 100 != slot) return -1;
  const auto it = g_slot_fighter.find(slot);
  return it == g_slot_fighter.end() ? -1 : int(it->second);
}
bool IsModBattle(uint8_t* base, uint32_t word) {
  const uint32_t code = word >> 16;
  if (word == 0xFFFFFFFF || !CodigoCombate(code)) return false;
  for (int id : g_wheel_ids) {
    if (WheelSlot(base, id) == (code - 5000) % 100) return true;
  }
  for (int v : g_virtual_slots) {
    if (uint32_t(v) == (code - 5000) % 100) return true;
  }
  return false;
}
}  // namespace dbz3::campaigns

DBZ3_HOOK(sub_82156F30, dbz3eu_sub_82156EF0) {
  std::call_once(dbz3::campaigns::g_once, dbz3::campaigns::Load);
  const int32_t code = ctx.r3.s32;
  const uint32_t lr = uint32_t(ctx.lr);
  const auto it = dbz3::campaigns::g_codes.find(code);
  uint32_t cont = it == dbz3::campaigns::g_codes.end() ? 0 : it->second;
  // escenas compartidas del juego (eventos del mapa) con la copia del mod solo mientras se juega
  // la historia de uno de sus personajes de [rueda] (la casilla de DU en 0x824A58A0)
  if (!cont) {
    const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    for (const auto& [ids, codes] : dbz3::campaigns::g_story_codes) {
      const auto st = codes.find(code);
      if (st == codes.end()) continue;
      for (int id : ids) {
        if (dbz3::campaigns::WheelSlot(base, id) == slot) cont = st->second;
      }
    }
  }
  if (!cont) {
    orig(ctx, base);
  } else {
    ctx.r3.u64 = cont;
  }
  // una linea por escena: quien hace campañas ve que codigo pide cada momento de la historia
  REXLOG_INFO("dbz3 campanas: codigo {} -> contenedor {}{} (desde {:08X}, casilla {})", code, ctx.r3.s32,
              cont ? " (del mod)" : "", lr, uint32_t(REX_LOAD_U8(dbz3::GA(0x824A58A0))));
}

// Casilla -> ID del personaje (sub_82156E88, solo US): el mapa y los menus de DU crean al personaje con el. Una
// casilla de [casilla_luchador] (o de DBZ3_TEST_CASILLA_ID="22=63") da su luchador (el Heroe, ID propio).
REX_HOOK_RAW(sub_82156E88) {
  static const bool test = [] {
    if (const char* e = std::getenv("DBZ3_TEST_CASILLA_ID")) {
      std::call_once(dbz3::campaigns::g_once, dbz3::campaigns::Load);
      dbz3::campaigns::g_slot_fighter[uint32_t(std::strtoul(e, nullptr, 10))] = uint32_t(std::strtoul(std::strchr(e, '=') ? std::strchr(e, '=') + 1 : "0", nullptr, 10));
    }
    return true;
  }();
  (void)test;
  const uint32_t slot = ctx.r3.u32;
  __imp__sub_82156E88(ctx, base);
  if (dbz3::g_guest_eu) return;
  std::call_once(dbz3::campaigns::g_once, dbz3::campaigns::Load);
  if (const auto it = dbz3::campaigns::g_slot_fighter.find(slot); it != dbz3::campaigns::g_slot_fighter.end()) ctx.r3.u64 = it->second;
}


// Intro de la casilla (sub_82156DC8: casilla -> codigo << 16 | casilla): una campana pide otra ([intro_casilla]
// 36 = 102: la de Namek) para empezar su historia en esa saga.
REX_HOOK_RAW(sub_82156DC8) {
  const uint32_t slot = ctx.r3.u32;
  __imp__sub_82156DC8(ctx, base);
  if (dbz3::g_guest_eu) return;
  std::call_once(dbz3::campaigns::g_once, dbz3::campaigns::Load);
  if (const auto it = dbz3::campaigns::g_slot_intro.find(slot); it != dbz3::campaigns::g_slot_intro.end()) {
    ctx.r3.u64 = (it->second << 16) | slot;
    // C4: "Continuar" tras cerrar una saga: la introduccion es la de la saga SIGUIENTE segun el progreso del heroe (ficha):
    // sig = saga + 1 (1-based); las 3 primeras usan 102/104/106 del juego y las demas las virtuales 14001+ (campana.py)
    if (slot < 38) {
      // PRUEBA (DBZ3_TEST_DU_SAGA=4): fija la saga 1-based de la intro sin jugar las anteriores (sondas de C8/DS9).
      const char* ts = std::getenv("DBZ3_TEST_DU_SAGA");
      const int sig = ts ? std::atoi(ts) : dbz3::heroe::Saga() + 1;
      const uint32_t intro = dbz3::campaigns::IntroDeSaga(sig);
      if (sig <= 8) ctx.r3.u64 = (uint64_t(intro) << 16) | slot;
    }
  }
}

// Entrada directa del Heroe (hub::RequestDu): la rueda de DU se crea sin verse elegir a nadie y su estado de espera
// (sub_821CA020, un tick por cuadro) recibe la casilla y la confirmacion: "continuar" (si no hay historia, nueva).
namespace dbz3::campaigns {
void DuAuto(int slot) { g_du_auto = slot, g_du_cover = true, g_du_sesion = slot; }
void DuSesionFin() { g_du_sesion = -1, g_du_reanudar = false; }
bool DuCover() { return g_du_cover.load(); }
bool DuAutoPending() { return g_du_auto.load() >= 0; }
}  // namespace dbz3::campaigns
// Escena 6 (titulo y menu de DU) en marcha: con una entrada directa del Heroe "pulsa" START y A sobre el mando del juego
// (kPad + 184 por jugador; +14/+18/+114 = botones; A = 0x4000, START = 0x0008) cada ~1,5 s hasta que el juego pide la
// rueda: pasa el titulo y confirma la 1.a fila del menu ("Nueva historia") sin que se vea elegir a nadie.
REX_HOOK_RAW(sub_821D78F8) {
  static uint32_t frames = 0;
  if (REX_LOAD_U8(0x824BA110u) == 6 && dbz3::campaigns::g_du_auto.load() < 0 && dbz3::campaigns::g_du_sesion.load() >= 0 &&
      dbz3::campaigns::g_du_reanudar.exchange(false)) {   // C4: tras guardar al cerrar una saga el juego vuelve al titulo: se reanuda sola
    REXLOG_INFO("dbz3 campanas: fin de saga guardado: el Heroe reanuda su historia (casilla {})", dbz3::campaigns::g_du_sesion.load());
    dbz3::campaigns::DuAuto(dbz3::campaigns::g_du_sesion.load());
  }
  if (dbz3::campaigns::g_du_auto.load() >= 0 && !dbz3::g_guest_eu && REX_LOAD_U8(0x824BA110u) == 6) {
    if (++frames % 20 == 10) {
      const uint32_t pad = 0x8238B780u + REX_LOAD_U32(0x8238B670u) * 184;
      for (uint32_t off : {14u, 18u, 114u}) REX_STORE_U16(pad + off, REX_LOAD_U16(pad + off) | 0x4008);
      REXLOG_INFO("dbz3 campanas: entrada directa del Heroe: START/A sobre el titulo y el menu de DU (cuadro {})", frames);
    }
  } else {
    frames = 0;
  }
  __imp__sub_821D78F8(ctx, base);
}
REX_HOOK_RAW(sub_821CA020) {
  if (const int slot = dbz3::campaigns::g_du_auto.load(); slot >= 0 && !dbz3::g_guest_eu) {
    REX_STORE_U16(0x8245F140u, uint16_t(slot));
    // 0 = Continuar (sin historia empezada, la inicia el propio juego). C4: al REANUDAR tras cerrar una saga, "Continuar" no
    // lanza la introduccion siguiente (se queda en negro): se entra como historia nueva, que si lanza la intro de la casilla
    // (el progreso del heroe vive en su ficha; la introduccion la elige el gancho de sub_82156DC8 segun heroe::Saga())
    static const bool reanudar_nueva = std::getenv("DBZ3_DU_REANUDAR_CONTINUAR") == nullptr;
    REX_STORE_U8(REX_LOAD_U32(ctx.r3.u32 + 48) + 85, (reanudar_nueva && dbz3::heroe::Saga() > 0) ? 1 : 0);
  }
  __imp__sub_821CA020(ctx, base);
}
// El estado de la rueda pregunta por la confirmacion con sub_8217ABA0 (1 = A): la damos una vez (hub::RequestDu)
REX_HOOK_RAW(sub_8217ABA0) {
  __imp__sub_8217ABA0(ctx, base);
  if (ctx.lr == 0x821CA140 && !dbz3::g_guest_eu && dbz3::campaigns::g_du_auto.load() >= 0) {
    ctx.r3.u64 = 1;
    REXLOG_INFO("dbz3 campanas: la rueda de DU confirma sola la casilla {} (Heroe)", dbz3::campaigns::g_du_auto.load());
    dbz3::campaigns::g_du_auto = -1;
  }
}

// Rueda de Dragon Universe: r3 = objeto de la rueda (+32 = casillas que permite el modo, r4 = las
// desbloqueadas). La de DU es siempre 0x4_00023C9D; los personajes del mod se suman a las dos.
DBZ3_HOOK(sub_8217AD68, dbz3eu_sub_8217AD20) {
  std::call_once(dbz3::campaigns::g_once, dbz3::campaigns::Load);
  constexpr uint64_t kDuMask = 0x400023C9Dull;
  const uint32_t obj = ctx.r3.u32;
  if (obj && !dbz3::campaigns::g_wheel_ids.empty() && REX_LOAD_U64(obj + 32) == kDuMask) {
    uint64_t extra = 0;
    for (int id : dbz3::campaigns::g_wheel_ids) {
      const uint32_t slot = REX_LOAD_U16(dbz3::GuestAddr(0x82020668, 0x82020670) + uint32_t(id) * 2);
      if (slot < 38) extra |= 1ull << slot;
    }
    REX_STORE_U64(obj + 32, kDuMask | extra);
    ctx.r4.u64 |= extra;
    REXLOG_INFO("dbz3 campanas: rueda de Dragon Universe con casillas de mas {:016X}", extra);
  }
  orig(ctx, base);
}

// Escena que arranca: r3 = codigo << 16 | entrada del contenedor (la entrada no llega al registro).
namespace dbz3::rpg {
void DuVictoria(int saga, int k);   // rpg.cpp
}
std::chrono::steady_clock::time_point g_ultima_escena{};   // C7: cuando se lanzo la ultima escena de la historia (el toque de prueba espera a que acabe su charla)
DBZ3_HOOK(sub_8215E918, dbz3eu_sub_8215E8D8) {
  g_ultima_escena = std::chrono::steady_clock::now();
  REXLOG_INFO("dbz3 campanas: escena {} entrada {}", ctx.r3.u32 >> 16, ctx.r3.u32 & 0xFFFF);
  dbz3::campaigns::g_du_cover = false;   // la primera escena de la historia: ya se ve
  // C3: la escena de victoria (entrada impar) de un combate de la casilla del Heroe: su progreso
  if (const uint32_t code = ctx.r3.u32 >> 16, entry = ctx.r3.u32 & 0xFFFF; !dbz3::g_guest_eu && dbz3::campaigns::CodigoCombate(code) && (entry & 1) &&
      dbz3::campaigns::g_slot_fighter.count(REX_LOAD_U8(dbz3::GA(0x824A58A0))) && (code - 5000) % 100 == REX_LOAD_U8(dbz3::GA(0x824A58A0))) {
    static uint32_t last = 0;
    dbz3::campaigns::g_saga_ganada = dbz3::campaigns::SagaDeCombate(code);
    if (last != ctx.r3.u32) dbz3::rpg::DuVictoria(dbz3::campaigns::SagaDeCombate(code), int(entry / 2));   // bloque 5500 = saga 0 del Heroe (SAGA0 = 1)
    last = ctx.r3.u32;
  }
  orig(ctx, base);
}

// Quien pide la escena siguiente (para seguir una historia en el log): un evento con sus
// parametros (r4+44 -> {entrada, codigo - 8192}) o la vuelta al mapa (codigo 3000, r3 = entrada).
DBZ3_HOOK(sub_8215EC68, dbz3eu_sub_8215EC28) {
  if (const uint32_t p = ctx.r4.u32 ? REX_LOAD_U32(ctx.r4.u32 + 44) : 0) {
    REXLOG_INFO("dbz3 campanas: un evento pide la escena {} entrada {} (desde {:08X})",
                int32_t(REX_LOAD_U32(p + 4)) + 8192, REX_LOAD_U32(p), uint32_t(ctx.lr));
  }
  orig(ctx, base);
}
DBZ3_HOOK(sub_82156B38, dbz3eu_sub_82156AF8) {
  REXLOG_INFO("dbz3 campanas: vuelta al mapa, entrada {} (desde {:08X})", ctx.r3.s32, uint32_t(ctx.lr));
  orig(ctx, base);
}

// Arranque de una escena desde el mapa o la historia (r3 = codigo << 16 | entrada).
DBZ3_HOOK(sub_8215E9C8, dbz3eu_sub_8215E988) {
  dbz3::campaigns::g_last_battle = ctx.r3.u32;

  if ((ctx.r3.u32 >> 16) == 16384 && (ctx.r3.u32 & 0xFFFF) == 5 && dbz3::campaigns::g_du_sesion.load() >= 0) dbz3::campaigns::g_du_reanudar = true;
  if ((ctx.r3.u32 >> 16) == 16384 && (ctx.r3.u32 & 0xFFFF) == 4 && dbz3::campaigns::g_du_sesion.load() >= 0 && dbz3::campaigns::g_saga_ganada.load() >= 0) {
    // C4: el cierre de saga va a guardar: la escena guardada de la casilla (bloque + 4564, la que usa "Continuar") pasa a la
    // introduccion de la saga SIGUIENTE (campana.py: 100 + 2*s del juego para las 3 primeras; 14001 + 2*(s-4) las virtuales)
    const int cerrada_abs = dbz3::campaigns::g_saga_ganada.load() + 1, sig = cerrada_abs + 1;   // SAGA0 = 1: indice 0 = saga 1
    const uint32_t intro = dbz3::campaigns::IntroDeSaga(sig);
    const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    const uint32_t blk = dbz3::GA(0x8247A9B0) + 128 + 4624 * (cas < 38 ? cas : 0);
    REXLOG_INFO("dbz3 campanas: cierre de saga {}: escena guardada {:08X} -> {:08X} (intro de la saga {})", cerrada_abs,
                REX_LOAD_U32(blk + 4564), (intro << 16) | cas, sig);
    REXLOG_INFO("dbz3 campanas: bloque de la casilla {}: +4560 {:08X} +4564 {:08X} +4568 {:08X} +4572 {:08X} pendiente {:08X}", cas, REX_LOAD_U32(blk + 4560),
                REX_LOAD_U32(blk + 4564), REX_LOAD_U32(blk + 4568), REX_LOAD_U32(blk + 4572), REX_LOAD_U32(0x8234DD1Cu));
    REX_STORE_U32(blk + 4564, (intro << 16) | cas);
    REX_STORE_U32(blk + 4572, 0);   // estado de las introducciones (0 = la de +4564 por ver): sin esto "Continuar" no la lanza
  }
  REXLOG_INFO("dbz3 campanas: se lanza la escena {} entrada {} (desde {:08X})", ctx.r3.u32 >> 16, ctx.r3.u32 & 0xFFFF,
              uint32_t(ctx.lr));
  orig(ctx, base);
}

namespace dbz3::campaigns {
// Depuracion (DBZ3_DU_TRACE=1): cambios de las palabras de "escena pendiente" y de la siguiente
// escena guardada del personaje de DU, una vez por cuadro.
void Trace(uint8_t* base) {
  static const bool on = std::getenv("DBZ3_DU_TRACE") != nullptr;
  if (!on) return;
  static uint32_t last[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
  const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  const uint32_t blk = dbz3::GA(0x8247A9B0) + 128 + 4624 * (slot < 38 ? slot : 0);
  // mapa (0x82479A60): +484 objeto cercano, +3568 su marca (lo que X lanza)
  const uint32_t now[9] = {REX_LOAD_U32(dbz3::GA(0x8234DD1C)), REX_LOAD_U32(dbz3::GA(0x8234DD24)), REX_LOAD_U32(dbz3::GA(0x8234DD28)),
                           REX_LOAD_U32(blk + 4564), REX_LOAD_U32(blk + 4568), REX_LOAD_U32(dbz3::GA(0x82479A60) + 484),
                           REX_LOAD_U32(dbz3::GA(0x82479A60) + 3568), REX_LOAD_U32(dbz3::GA(0x82479A60) + 216),
                           REX_LOAD_U32(dbz3::GA(0x82479CC8) + 1388)};
  if (std::equal(now, now + 9, last)) return;
  std::copy(now, now + 9, last);
  auto w = [](uint32_t v) { return v == 0xFFFFFFFF ? std::string("-") : std::to_string(v >> 16) + "." + std::to_string(v & 0xFFFF); };
  REXLOG_INFO("dbz3 campanas: traza casilla {} pend {} / {} / {} guardada {} / {} cerca {:08X} marca {:08X} mapa {} combate {}",
              slot, w(now[0]), w(now[1]), w(now[2]), w(now[3]), w(now[4]), now[5], now[6], now[7], int32_t(now[8]));
}
}  // namespace dbz3::campaigns

// Evento del mapa que pide la siguiente escena (r4+44 -> {codigo << 16 | entrada, ...}).
DBZ3_HOOK(sub_82159C18, dbz3eu_sub_82159BD8) {
  if (const uint32_t p = ctx.r4.u32 ? REX_LOAD_U32(ctx.r4.u32 + 44) : 0) {
    const uint32_t v = REX_LOAD_U32(p);
    REXLOG_INFO("dbz3 campanas: el mapa pide la escena {} entrada {} (objeto {:08X}, {:08X} {:08X})", v >> 16, v & 0xFFFF,
                ctx.r4.u32, REX_LOAD_U32(p + 4), REX_LOAD_U32(p + 8));
  }
  orig(ctx, base);
}


DBZ3_EXTERN_EU(dbz3eu_sub_8215E988);

// Combates de las historias de mod: el juego solo los arranca desde el mapa (lanzados nada mas
// acabar una narracion, el combate cuelga en sub_8209F568). IsModBattle: la palabra es un combate
// (bloques 5400-5799) de un personaje de [rueda].

// Seguro de las historias de mod: sub_820AAD30(lista, recurso) libera un recurso de una escena y
// lee recurso+128 sin comprobarlo. Con las escenas de un mod llego a recibir 0x20 (un recurso ya
// liberado) y el juego se cerraba al cargar el dialogo del combate siguiente: un puntero imposible
// se ignora (devuelve 1, "no estaba cargando", como cuando el estado no es 1).
DBZ3_HOOK_EU(sub_820AAD30) {
  if (ctx.r4.u32 < 0x10000) {
    static int warned = 0;
    if (warned++ < 5)
      REXLOG_WARN("dbz3 campanas: recurso imposible {:08X} al liberar (desde {:08X}): se ignora", ctx.r4.u32,
                  uint32_t(ctx.lr));
    ctx.r3.u64 = 1;
    return;
  }
  orig(ctx, base);
}

// Estado "volar por el mapa" (sub_821C4DA0, r3 = tarea del mapa; en +28 su siguiente estado). Las
// historias de mod dejan su combate como marca del mapa (op 24); tras 5 s de mapa (lo que tarda el mapa en quedar libre tras un combate) se toca sola,
// haciendo lo mismo que el juego al tocar una marca de combate (0x821C4B90 / 0x821C5040): marca
// tocada en +232, escena lanzada, mapa al estado 7 (entrar en un evento) y la tarea a su manejador.
// US y EU (direcciones medidas en la US; la pareja EU, de src/eu_map_gen.h).
DBZ3_HOOK_EU(sub_821C4DA0) {
  using namespace dbz3::campaigns;
  // Desde 2026-10-11 las marcas de las historias de mod son como las del juego (icono 1, letrero y
  // punto en el minimapa) y se tocan volando hasta ellas; el toque solo queda para pruebas.
  // DBZ3_AUTO_TOUCH=N: las N primeras marcas (sin numero, todas)
  static int auto_touch = std::getenv("DBZ3_AUTO_TOUCH") ? (std::atoi(std::getenv("DBZ3_AUTO_TOUCH")) > 0 ? std::atoi(std::getenv("DBZ3_AUTO_TOUCH")) : 1 << 30) : 0;
  static const int auto_touch_s = std::getenv("DBZ3_AUTO_TOUCH_S") ? std::max(1, std::atoi(std::getenv("DBZ3_AUTO_TOUCH_S"))) : 0;   // C7: segundos con la marca antes de tocarla
  if (auto_touch > 0 && (!g_wheel_ids.empty() || !g_virtual_slots.empty()) && REX_LOAD_U32(dbz3::GA(0x8234DD1C)) == 0xFFFFFFFF) {
    const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    const uint32_t marks = dbz3::GA(0x8247A9B0) + 128 + 4624 * (slot < 38 ? slot : 0) + 128;
    uint32_t mark = 0;
    for (uint32_t i = 0; i < 64 && !mark; ++i) {
      if (IsModBattle(base, REX_LOAD_U32(marks + 64 * i))) mark = marks + 64 * i;
    }
    if (!mark) {
      g_map_since = {};
    } else if (g_map_since == std::chrono::steady_clock::time_point{}) {
      g_map_since = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - g_map_since > std::chrono::seconds(5) &&
               std::chrono::steady_clock::now() - g_ultima_escena > std::chrono::seconds(auto_touch_s)) {
      g_map_since = {};
      --auto_touch;
      const uint32_t task = ctx.r3.u32, word = REX_LOAD_U32(mark);
      REXLOG_INFO("dbz3 campanas: se toca la marca de combate {} entrada {}", word >> 16, word & 0xFFFF);
      constexpr dbz3::GA kMap = 0x82479A60, kScr = 0x824BA110;
      auto f32 = [&](uint32_t a) { const uint32_t v = REX_LOAD_U32(a); float f; std::memcpy(&f, &v, 4); return f; };
      auto put = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); REX_STORE_U32(a, v); };
      const float f31 = f32(dbz3::GA(0x8201F60C));
      const PPCContext saved = ctx;
      REX_STORE_U32(kMap + 232, mark);
      ctx.r3.u64 = mark + 16;                       // camara hacia la marca
      DBZ3_CALL_EU(sub_821C3218)(ctx, base);
      put(kMap + 200, f31);
      REX_STORE_U32(kMap + 192, 0x10007);
      put(kMap + 204, f32(dbz3::GA(0x82000D74)));
      const uint32_t cam = REX_LOAD_U32(REX_LOAD_U32(REX_LOAD_U32(kMap + 188) + 1132) + 20);
      put(kMap + 196, float(int32_t(REX_LOAD_U32(cam + 120))));
      ctx.r3.u64 = 1;                               // fundido
      ctx.r4.u64 = 0;
      ctx.f1.f64 = f32(kScr + 0x2C0C0);
      DBZ3_CALL_EU(sub_82092570)(ctx, base);
      if (f32(kScr + 0x2C0C0) > f31) {
        ctx.r3.u64 = 1; ctx.r4.u64 = 0; ctx.r5.u64 = 9; ctx.r6.u64 = 0;
        DBZ3_CALL_EU(sub_820922A0)(ctx, base);
      }
      ctx.r3.u64 = word;
      DBZ3_CALL_EU(sub_8215E9C8)(ctx, base);
      ctx.r3.u64 = 10; ctx.r4.u64 = dbz3::GA(0x821C7058); ctx.r5.u64 = 0;   // sonido de entrar
      DBZ3_CALL_EU(sub_82165190)(ctx, base);
      ctx = saved;
      REX_STORE_U32(kMap + 216, 7);
      REX_STORE_U32(task + 28, dbz3::GA(0x821C3960));
      return;
    }
  }
  orig(ctx, base);
}


namespace dbz3::campaigns {
namespace {
// Aspecto del personaje del mapa ([modelo_mapa] / [traje_mapa] / [animacion_mapa] del combate que
// toca; -1 = el suyo). El combate que toca: la escena guardada del personaje, el ultimo combate
// lanzado y las marcas del mapa; si ninguna palabra esta tal cual, vale otra del mismo codigo (saga).
struct MapLook {
  int model = -1, suit = -1, anim = -1;
  bool operator==(const MapLook& o) const { return model == o.model && suit == o.suit && anim == o.anim; }
};
bool OwnerCovers(uint8_t* base, uint32_t word, uint32_t slot) {
  const auto o = g_word_owner.find(word);
  if (o == g_word_owner.end() || o->second >= g_story_codes.size()) return true;
  for (int e : g_story_codes[o->second].first) {
    if (WheelSlot(base, e) == slot) return true;
  }
  return false;
}
MapLook PickLook(uint8_t* base, const char* how, bool quiet = false) {
  MapLook l;
  // EU: la casilla de DU (0x824A58A0) no esta localizada en la europea: sin cambio de aspecto
  if ((g_map_model.empty() && g_map_suit.empty() && g_map_anim.empty())) return l;
  const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  const uint32_t blk = dbz3::GA(0x8247A9B0) + 128 + 4624 * (slot < 38 ? slot : 0);
  std::vector<uint32_t> words = {REX_LOAD_U32(blk + 4564), REX_LOAD_U32(blk + 4568), g_last_battle.load()};
  for (uint32_t i = 0; i < 64; ++i) words.push_back(REX_LOAD_U32(blk + 128 + 64 * i));
  auto pick = [&](const std::unordered_map<uint32_t, int>& m) {
    for (uint32_t w : words) {
      if (const auto it = m.find(w); it != m.end()) return it->second;
    }
    for (uint32_t w : words) {
      for (const auto& [k, v] : m) {
        // otra palabra de la misma saga, pero solo de la campana de ESTA casilla (Freezer no cambia al Heroe)
        if (w != 0xFFFFFFFFu && (k >> 16) == (w >> 16) && OwnerCovers(base, k, slot)) return v;
      }
    }
    return -1;
  };
  l = {pick(g_map_model), pick(g_map_suit), pick(g_map_anim)};
  if (!quiet) REXLOG_INFO("dbz3 campanas: personaje del mapa {} (casilla {}, guardada {:08X}, ultimo {:08X}, +4560 {:08X} +4568 {:08X} +4572 {:08X}, pendiente {:08X}): modelo {} traje {} vuelo {}",
              how, slot, words[0], words[2], REX_LOAD_U32(blk + 4560), words[1], REX_LOAD_U32(blk + 4572), REX_LOAD_U32(0x8234DD1Cu), l.model, l.suit, l.anim);
  return l;
}

// Crea al personaje (la llamada del juego `create`) con ese aspecto y lo deja todo como estaba, asi la
// partida guardada no cambia. Modelo y traje en el registro (+4578 / +4579). [animacion_mapa]: las
// entradas de la casilla del personaje en las tablas del mapa, cambiadas solo durante la llamada por
// las de la casilla elegida: vuelo (0x823742C8 + (casilla*5 + k)*4, k = traje si el modelo < 3, si no
// 4) y animaciones (0x823745C0[casilla]). La casilla (+4577) no se cambia: con ella cambiaban tambien
// los datos por casilla y modelo (sub_821C0BE0) y R1 cerraba el juego (2026-10-12).
template <class F>
void WithLook(uint8_t* base, uint32_t rec, const MapLook& l, F&& create) {
  constexpr dbz3::GA kFlight = 0x823742C8;
  if (!rec) {
    create();
    return;
  }
  const uint8_t old_model = REX_LOAD_U8(rec + 4578), old_suit = REX_LOAD_U8(rec + 4579);
  if (l.model >= 0) REX_STORE_U8(rec + 4578, uint8_t(l.model));
  if (l.suit >= 0) REX_STORE_U8(rec + 4579, uint8_t(l.suit));
  // y el fichero de animaciones del mapa de la casilla (0x823745C0[casilla], map+24): con solo el vuelo
  // Mecha seguia sentado como en la nave (visto por el usuario 2026-10-12)
  constexpr dbz3::GA kAnims = 0x823745C0;
  uint32_t entry = 0, old_entry = 0, anims = 0, old_anims = 0;
  if (l.anim >= 0 && l.anim < 38) {
    const uint32_t slot = REX_LOAD_U8(rec + 4577);
    const uint32_t k = REX_LOAD_U8(rec + 4578) < 3 ? REX_LOAD_U8(rec + 4579) : 4;
    if (slot < 38 && k < 5) {
      entry = kFlight + (slot * 5 + k) * 4;
      old_entry = REX_LOAD_U32(entry);
      REX_STORE_U32(entry, REX_LOAD_U32(kFlight + (uint32_t(l.anim) * 5 + k) * 4));
      anims = kAnims + slot * 4;
      old_anims = REX_LOAD_U32(anims);
      REX_STORE_U32(anims, REX_LOAD_U32(kAnims + uint32_t(l.anim) * 4));
    }
  }
  create();
  if (entry) REX_STORE_U32(entry, old_entry);
  if (anims) REX_STORE_U32(anims, old_anims);
  REX_STORE_U8(rec + 4578, old_model);
  REX_STORE_U8(rec + 4579, old_suit);
}
}  // namespace
}  // namespace dbz3::campaigns

// Partida guardada: el juego solo guarda las historias de sus 11 personajes de DU (copias fijas de
// 4624 B en sub_821D2BD8 = guardar, sub_821D2F80 = cargar; casillas de 0x4_00023C9D). Las casillas de
// [rueda] de los mods (Freezer = 22) volvian vacias al cargar y salia Goku. Se guardan aparte, en
// historias_extra.bin junto a las preferencias, en el mismo momento que la partida del juego. US y EU.
namespace dbz3::campaigns {
namespace {
// las dos copias de los datos de DU (bloque k: +336 + 4624k): la del juego en curso y la de la partida
// (la que leen y escriben las conversiones; medido 2026-10-14: sub_821D2F80 carga en 0x824BA110)
bool IsDuStruct(uint32_t a) { return a == dbz3::GA(0x8247A8E0) || a == dbz3::GA(0x824BA110); }
constexpr uint32_t kDuBlock = 4624;
constexpr uint64_t kDuNative = 0x400023C9Dull;        // casillas que el juego si guarda
std::vector<uint32_t> ExtraSlots(uint8_t* base) {
  std::call_once(g_once, Load);
  std::vector<uint32_t> out;
  for (int id : g_wheel_ids) {
    const uint32_t slot = REX_LOAD_U16(dbz3::GA(0x82020668) + uint32_t(id) * 2);
    if (slot < 38 && !((kDuNative >> slot) & 1)) out.push_back(slot);
  }
  for (int v : g_virtual_slots) {
    if (std::find(out.begin(), out.end(), uint32_t(v)) == out.end()) out.push_back(uint32_t(v));
  }
  return out;
}
std::filesystem::path ExtraFile() { return dbz3::settings::UserDataRoot() / "historias_extra.bin"; }
}  // namespace
}  // namespace dbz3::campaigns

DBZ3_HOOK_EU(sub_821D2BD8) {   // r3 = partida a guardar, r4 = datos de DU
  using namespace dbz3::campaigns;
  if (const uint32_t du = ctx.r4.u32; IsDuStruct(du)) {
    const auto slots = ExtraSlots(base);
    if (!slots.empty()) {
      std::error_code ec;
      const auto path = ExtraFile();
      std::filesystem::create_directories(path.parent_path(), ec);
      std::ofstream out(path.string() + ".tmp", std::ios::binary | std::ios::trunc);
      out.write("DBZ3DU1", 8);
      for (uint32_t slot : slots) {
        const uint32_t be = __builtin_bswap32(slot);
        out.write(reinterpret_cast<const char*>(&be), 4);
        out.write(reinterpret_cast<const char*>(base + du + 336 + kDuBlock * slot), kDuBlock);
      }
      out.close();
      if (out) std::filesystem::rename(path.string() + ".tmp", path, ec);
      REXLOG_INFO("dbz3 campanas: partida: {} historias de mods guardadas aparte{}", slots.size(), ec ? " (ERROR)" : "");
    }
  }
  orig(ctx, base);
}

DBZ3_HOOK_EU(sub_821D2F80) {   // r3 = datos de DU, r4 = partida cargada
  const uint32_t live = ctx.r3.u32;
  orig(ctx, base);
  using namespace dbz3::campaigns;
  if (!IsDuStruct(live)) return;
  const auto slots = ExtraSlots(base);
  std::ifstream in(ExtraFile(), std::ios::binary);
  char magic[8] = {};
  if (slots.empty() || !in.read(magic, 8) || std::memcmp(magic, "DBZ3DU1", 8) != 0) {
    if (!slots.empty()) REXLOG_INFO("dbz3 campanas: partida: sin historias de mods guardadas aparte");
    return;
  }
  uint32_t be = 0;
  int n = 0;
  std::vector<char> blk(kDuBlock);
  while (in.read(reinterpret_cast<char*>(&be), 4) && in.read(blk.data(), kDuBlock)) {
    const uint32_t slot = __builtin_bswap32(be);
    if (std::find(slots.begin(), slots.end(), slot) == slots.end()) continue;   // mod quitado: se ignora
    std::memcpy(base + live + 336 + kDuBlock * slot, blk.data(), kDuBlock);
    ++n;
  }
  REXLOG_INFO("dbz3 campanas: partida: {} historias de mods recuperadas", n);
}

// Personaje del mapa al volver de un combate o al empezar la historia (sub_821C24B8): lo crea
// sub_82133B88(ID, modelo +4578, traje +4579) con el registro del jugador de DU (0x82479A60 +180).
// Freezer: modelo 5 = Mecha; traje 2 con modelo 0 = el modelo 193, Freezer en su nave. US y EU.
DBZ3_HOOK_EU(sub_821C24B8) {
  using namespace dbz3::campaigns;
  const uint32_t rec = REX_LOAD_U32(dbz3::GA(0x82479A60) + 180);
  const MapLook l = rec ? PickLook(base, "al crear") : MapLook{};
  WithLook(base, rec, l, [&] { orig(ctx, base); });
}

// Al llegar a otro planeta el juego no rehace al personaje del mapa: en la Tierra sigue la nave de
// Namek hasta volver del primer combate. Rehacerlo a mano al llegar (sub_821C24B8 desde el estado de
// volar) cerro el juego (2026-10-12, lectura de 0x14): no se hace.


// sub_820A5820(objeto, recurso, hueco): suelta el recurso (sub_820AAD30) y luego lee recurso+32.
// Tras un relevo del Tag Team, un Kamehameha del que entra le paso 0x20 (2026-10-12: el seguro de
// arriba lo ignoraba pero la lectura siguiente cerraba el juego). Un recurso imposible se trata como
// "sin recurso" (r4 = 0), un caso que la funcion ya contempla.
DBZ3_HOOK_EU(sub_820A5820) {
  if (ctx.r4.u32 && ctx.r4.u32 < 0x10000) {
    static int warned = 0;
    if (warned++ < 5)
      REXLOG_WARN("dbz3: recurso imposible {:08X} en sub_820A5820 (desde {:08X}): sin recurso", ctx.r4.u32,
                  uint32_t(ctx.lr));
    ctx.r4.u64 = 0;
  }
  orig(ctx, base);
}

// sub_82096FF0(?, ?, id): efecto/sonido `id` del banco global ([0x8238CE10], tabla de desplazamientos
// en [0x8238CE14] desde el id [banco+24]); usa el recurso de la entrada (+20). Tras un relevo del Tag
// Team el Kamehameha del que entra pidio una entrada con recurso 0x20 (el banco es del que salio) y el
// juego se cerraba (2026-10-12). Una entrada imposible no se usa: sin ese efecto, sin cierre.
// 2026-10-12 (prueba de estres del Tag Team): un efecto que seguia en pantalla tras el relevo pidio un id
// que no esta en el banco y el juego llamo a una funcion basura del recurso (vtable[1]): parada del
// juego (0x80000003). Se comprueba lo mismo que el juego va a leer (entrada, recurso, su vtable[1]) y,
// si no es posible, se hace lo que la funcion hace sin banco: 32 bytes a 0 en objeto+224.
// C4 (Shenron): sub_8215C1B0 comprueba la mascara de Esferas del Dragon de la casilla (u8 en bloque+4580; bloque = 0x8247AA30 +
// 4624 * casilla) y con las 7 (0x7F) lanza la escena de Shenron. DBZ3_TEST_ESFERAS=7 las pone antes de comprobar (prueba: ver
// con DBZ3_SHENRON_DIAG quien dibuja el banco data_usi 1958). Solo US (sin entrada EU).
REX_HOOK_RAW(sub_8215C1B0) {
  static const int esf = std::getenv("DBZ3_TEST_ESFERAS") ? std::atoi(std::getenv("DBZ3_TEST_ESFERAS")) : 0;
  const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  const uint32_t cas2 = REX_LOAD_U8(0x824A5960);
  const uint32_t dir = 0x8247AA30u + 4624u * cas + 4580u;
  const uint32_t antes = REX_LOAD_U8(dir);
  if (esf >= 7) REX_STORE_U8(dir, 0x7F);
  REXLOG_INFO("dbz3 shenron: sub_8215C1B0 (comprueba las Esferas) casilla {} / {}, mascara antes {:02X}{} (desde {:08X})", cas, cas2,
              antes, esf >= 7 ? " -> 7F (prueba)" : "", uint32_t(ctx.lr));
  __imp__sub_8215C1B0(ctx, base);
}

DBZ3_HOOK_EU(sub_82096FF0) {
  const uint32_t bank = REX_LOAD_U32(dbz3::GA(0x8238CE10));
  const int32_t id = ctx.r5.s32;
  // C4: rotulo de nombre de la pantalla de subida de nivel de DU (data_usi 1974, textura 27+casilla; 36 = 'Saibamen'): con la
  // historia del Heroe en las casillas 35-37 se pide la textura propia (casilla + 30) que anade mods/heroe_nombres
  // (cac/nombre_levelup.py): el banco pasa de 65 a 68 texturas.
  if (bank && id >= 62 && id <= 64 && REX_LOAD_U32(bank + 16) >= 68) {
    const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    if (int32_t(cas) == id - 27 && dbz3::campaigns::g_slot_fighter.count(cas)) ctx.r5.s64 = id + 3;
  }
  {   // C4: DIAGNOSTICO de la escena de Shenron (DBZ3_SHENRON_DIAG=1): el banco de texto data_usi 1958 tiene 4 texturas
    static const bool shdiag = std::getenv("DBZ3_SHENRON_DIAG") != nullptr;
    if (shdiag && bank && REX_LOAD_U32(bank + 16) == 4 && id >= 0 && id < 4) {
      static std::atomic<int> nsh{0};
      if (nsh.fetch_add(1) < 40)
        REXLOG_INFO("dbz3 shenron-diag: sub_82096FF0 id {} del banco de 4 texturas (data_usi 1958?) desde {:08X}", id, uint32_t(ctx.lr));
    }
  }
  {   // C4: DIAGNOSTICO del rotulo de la pantalla de subida de nivel (DBZ3_LEVELUP_DIAG=1): que ids se piden y desde donde
    static const bool diag = std::getenv("DBZ3_LEVELUP_DIAG") != nullptr;
    static std::mutex dmu;
    static std::set<uint64_t> vistos;
    if (diag && bank && REX_LOAD_U32(bank + 16) > 60) {
      std::lock_guard<std::mutex> lk(dmu);
      if (vistos.insert((uint64_t(uint32_t(id)) << 32) | uint32_t(ctx.lr)).second && vistos.size() < 600)
        REXLOG_INFO("dbz3 levelup-diag: sub_82096FF0 id {} (banco n {}) desde {:08X} casilla {}", id, REX_LOAD_U32(bank + 16),
                    uint32_t(ctx.lr), REX_LOAD_U8(dbz3::GA(0x824A58A0)));
    }
  }
  if (bank && id != -1) {
    // id fuera de la tabla del banco: el juego lee una entrada basura. A veces le sale un recurso 0 y no
    // pasa nada, pero R1 en el mapa de la Tierra con Freezer pide uno que cae en memoria sin mapear y el
    // juego se cerraba (2026-10-12, lectura de 0x06A459B2): fuera de la tabla, sin efecto
    auto code = [](uint32_t a) { return a >= 0x82000000u && a < 0x83000000u && !(a & 3); };
    const uint32_t offs = REX_LOAD_U32(dbz3::GA(0x8238CE14));
    const int32_t idx = id - int32_t(REX_LOAD_U32(bank + 24));
    const uint32_t rel = offs >= 0x10000 && idx >= 0 && idx < 4096 ? REX_LOAD_U32(offs + 4 * uint32_t(idx)) : 0xFFFFFFFFu;
    const uint32_t entry = rel < 0x01000000u ? bank + rel : 0;
    const uint32_t res = entry ? REX_LOAD_U32(entry + 20) : 0;
    // el recurso tiene que ser de la clase de los efectos (vtable 8201CAA0, la unica vista en 14 relevos
    // con tecnicas, DBZ3_FX_VTDIAG 2026-10-12): uno liberado por un relevo y reutilizado por otra clase
    // pasaba el control anterior y el juego acababa en sub_822F5460 -> sub_822F8988 (cierre)
    constexpr dbz3::GA kFxVtable = 0x8201CAA0;
    // y en memoria que existe (montones del juego, imagen o fisica): una direccion basura (no mapeada) cerraba el
    // juego al leer su vtable (frente de escenarios, 2026-10-15)
    auto heap = [](uint32_t a) {
      return !(a & 3) && ((a >= 0x40000000u && a < 0x80000000u) || (a >= 0x82000000u && a < 0x83000000u) || a >= 0xA0000000u);
    };
    const bool ok = entry && (!res || (heap(res) && REX_LOAD_U32(res) == kFxVtable &&
                                       code(REX_LOAD_U32(REX_LOAD_U32(res) + 4))));
    static const bool vtdiag = std::getenv("DBZ3_FX_VTDIAG") != nullptr;
    if (ok && res && vtdiag) {   // DIAGNOSTICO: clases de recurso que usa el juego
      static std::mutex mu;
      static std::set<uint64_t> seen;
      const uint32_t vt = REX_LOAD_U32(res);
      std::lock_guard<std::mutex> lock(mu);
      if (seen.size() < 32 && seen.insert((uint64_t(vt) << 32) | REX_LOAD_U32(vt + 4)).second)
        REXLOG_INFO("dbz3 fxdiag: recurso {:08X} vtable {:08X} [1]={:08X} id {} desde {:08X}", res, vt,
                    REX_LOAD_U32(vt + 4), id, uint32_t(ctx.lr));
    }
    if (!ok) {
      static int warned = 0;
      if (warned++ < 8)
        REXLOG_WARN("dbz3: efecto {} que no esta en el banco {:08X} (entrada {:08X}, recurso {:08X}, desde {:08X}): sin efecto", id,
                    bank, entry, res, uint32_t(ctx.lr));
      std::memset(base + ctx.r3.u32 + 224, 0, 32);
      return;
    }
  }
  orig(ctx, base);
}

// C8: traza de la maquina de escenas (cuelgue del 1.er combate de la saga 2). DBZ3_TRAZA_ESC=1: cada cambio de estado y
// cada peticion de escena (sub_821D2778) al log, para comparar saga 1 (sana) y saga 2 (colgada). Ver docs 12_conectores/10.
namespace {
bool TrazaEsc() { static const bool on = std::getenv("DBZ3_TRAZA_ESC") != nullptr; return on; }
void TrazaCeldas(uint8_t* base, const char* quien, uint32_t lr) {
  const uint32_t t = REX_LOAD_U32(0x8247A9A0u);
  REXLOG_INFO("dbz3 traza: {} (desde {:08X}) arg {:08X} cola {:08X} pedida {:08X} fin {} lista {} T {:08X} T+28 {:08X} pant {}", quien, lr,
              REX_LOAD_U32(0x8247A988u), REX_LOAD_U32(0x8247A98Cu), REX_LOAD_U32(0x8247A990u), REX_LOAD_U32(0x8247A994u),
              REX_LOAD_U32(0x8247A998u), t, t ? REX_LOAD_U32(t + 28) : 0u, REX_LOAD_U8(0x824BA110u));
}
}  // namespace
REX_HOOK_RAW(sub_821D2778) {
  if (TrazaEsc()) { REXLOG_INFO("dbz3 traza: pide escena {} arg {}", ctx.r3.s32, ctx.r4.s32); TrazaCeldas(base, "antes de pedir", uint32_t(ctx.lr)); }
  __imp__sub_821D2778(ctx, base);
  if (TrazaEsc()) TrazaCeldas(base, "tras pedir", 0);
}
REX_HOOK_RAW(sub_821D27A8) { if (TrazaEsc()) TrazaCeldas(base, "salida terminada", uint32_t(ctx.lr)); __imp__sub_821D27A8(ctx, base); }
REX_HOOK_RAW(sub_821D27F8) { if (TrazaEsc()) TrazaCeldas(base, "escena lista", uint32_t(ctx.lr)); __imp__sub_821D27F8(ctx, base); }
uint32_t g_traza_epoca = 0;   // sube con cada combate (primer nodo de la carga)
void TrazaVuelcaNodo(uint8_t* base) {
  static uint32_t epoca_vista = 0, n = 0;
  const uint32_t mgr = REX_LOAD_U32(0x823D6CF4u);
  if (!mgr) return;
  const uint32_t h = REX_LOAD_U32(mgr + 35248);
  if (h < 0x40000000u || h >= 0xA0000000u) return;
  static uint32_t ult_hash = 0;
  if (epoca_vista != g_traza_epoca) { epoca_vista = g_traza_epoca; n = 0; ult_hash = 0; }
  std::string s;
  uint32_t hs = 17;
  for (uint32_t i = 0; i < 64; i += 4) { const uint32_t w = REX_LOAD_U32(h + i); hs = hs * 31 + w; s += fmt::format(" {:08X}", w); }
  static uint32_t cuadros = 0, cuadros_ep = 0;
  if (cuadros_ep != g_traza_epoca) { cuadros_ep = g_traza_epoca; cuadros = 0; }
  const bool forzar = ++cuadros == 240;   // 4 s despues: otra vez la cola
  if ((hs == ult_hash || n >= 6) && !forzar) return;
  ult_hash = hs;
  {
    std::string q;
    uint32_t nd = REX_LOAD_U32(0x823863C0u);
    for (int k = 0; k < 16 && nd >= 0x40000000u && nd < 0xA0000000u; ++k, nd = REX_LOAD_U32(nd + 216))
      q += fmt::format("\n   cola[{}] {:08X}: w0 {:08X} st {:04X} id {:08X} +208 {:08X} +212 {:08X}", k, nd, REX_LOAD_U32(nd), REX_LOAD_U16(nd + 4), REX_LOAD_U32(nd + 196), REX_LOAD_U32(nd + 208), REX_LOAD_U32(nd + 212));
    {
      std::string g, hn;
      for (uint32_t i = 25400; i < 25700; i += 4) g += fmt::format("{}{:X}", (i % 32 == 25400 % 32) ? " |" : " ", REX_LOAD_U32(0x82380000u + i));
      const uint32_t h0 = REX_LOAD_U32(0x823863C0u);
      if (h0 >= 0x40000000u && h0 < 0xA0000000u) for (uint32_t i = 0; i < 224; i += 4) hn += fmt::format(" {:X}", REX_LOAD_U32(h0 + i));
      REXLOG_INFO("dbz3 traza: cargador globals[+25400..]:{} | cabeza:{}", g, hn);
    }
    REXLOG_INFO("dbz3 traza: cola de cargas (cuadro {}) pool idx {} lim {}:{}", cuadros, REX_LOAD_U32(0x823863BCu), REX_LOAD_U32(0x823863B0u), q);
  }
  if (n == 0) {
    const uint32_t desc = REX_LOAD_U32(0x82380000u + 25568);
    for (uint32_t cont = 0; cont < 4 && desc >= 0x40000000u; ++cont) {
      std::string d;
      for (uint32_t i = 0; i < 380; i += 4) d += fmt::format(" {:X}", REX_LOAD_U32(desc + cont * 380 + i));
      REXLOG_INFO("dbz3 traza: descriptor contenedor {} @{:08X}:{}", cont, desc + cont * 380, d);
    }
  }
  if (n++ == 0) {
    std::string t, m;
    for (uint32_t i = 0; i < 8; ++i) t += fmt::format(" {:08X}", REX_LOAD_U32(0x8202ED14u + 4 * i));
    for (uint32_t i = 0; i < 24; ++i) m += fmt::format(" {:08X}", REX_LOAD_U32(mgr + 1680 + 4 * i));
    REXLOG_INFO("dbz3 traza: tabla de cargadores 8202ED14:{} | mgr+1680:{}", t, m);
  }
  REXLOG_INFO("dbz3 traza: nodo carga mgr+35248 = {:08X} (n {}):{}", h, n, s);
}
// C8: la carga del combate espera un fichero (mgr+35248) que va detras de otro en la cola del cargador (0x823863C0); en el
// 1.er combate tras el cierre de saga la cabeza de la cola se queda en el estado 1 para siempre (la E/S nunca se completa).
// Si dura 2 s la pantalla del combate (9) se reencola la peticion atascada (estado 0 = empezar de nuevo).
void ArreglaColaAtascada(uint8_t* base) {
  static uint32_t atasco = 0, ult_cabeza = 0, reintentos = 0;
  static const bool apagado = std::getenv("DBZ3_ARREGLO_COLA") == nullptr;   // prueba: no arregla nada (C8)
  const uint32_t mgr = REX_LOAD_U32(0x823D6CF4u);
  const uint32_t h = mgr ? REX_LOAD_U32(mgr + 35248) : 0, cab = REX_LOAD_U32(0x823863C0u);
  const bool espera = !apagado && REX_LOAD_U8(0x824BA110u) == 9 && h >= 0x40000000u && h < 0xA0000000u && int16_t(REX_LOAD_U16(h + 4)) == 0 &&
                      cab >= 0x40000000u && cab < 0xA0000000u && cab != h && int16_t(REX_LOAD_U16(cab + 4)) == 1;
  if (TrazaEsc() && REX_LOAD_U8(0x824BA110u) == 9 && h >= 0x40000000u && h < 0xA0000000u && int16_t(REX_LOAD_U16(h + 4)) == 0) {
    static uint32_t c = 0;
    if (++c % 60 == 1) REXLOG_INFO("dbz3 traza: cola atascada? cab {:08X} (h {:08X}) estado cab {} id {:X} atasco {}", cab, h, cab ? int16_t(REX_LOAD_U16(cab + 4)) : -9, cab ? REX_LOAD_U32(cab + 196) : 0u, atasco);
  }
  if (!espera) { atasco = 0; if (REX_LOAD_U8(0x824BA110u) != 9) reintentos = 0; return; }
  if (cab != ult_cabeza) { ult_cabeza = cab; atasco = 0; }
  if (++atasco < (reintentos ? 20u : 120u)) return;
  atasco = 0;
  if (++reintentos > 40) return;
  REXLOG_INFO("dbz3 campanas: carga del combate atascada (cabeza {:08X} id {:X} estado 1): se reencola ({} de 40)", cab, REX_LOAD_U32(cab + 196), reintentos);
  REX_STORE_U16(cab + 4, 0);
}
void ColaPeriodica(uint8_t* base) {
  static uint32_t c = 0, ult_cab = 0, desde = 0;
  ++c;
  const uint32_t cab = REX_LOAD_U32(0x823863C0u);
  if (cab < 0x40000000u || cab >= 0xA0000000u) { ult_cab = 0; return; }
  if (cab != ult_cab || int16_t(REX_LOAD_U16(cab + 4)) == 0) { ult_cab = cab; desde = c; return; }
  if ((c - desde) % 90 != 89) return;   // la misma cabeza lleva >1,5 s
  std::string q;
  uint32_t nd = cab;
  int n = 0;
  for (; n < 16 && nd >= 0x40000000u && nd < 0xA0000000u; ++n, nd = REX_LOAD_U32(nd + 216))
    q += fmt::format(" {:X}/{}", REX_LOAD_U32(nd + 196), int16_t(REX_LOAD_U16(nd + 4)));
  REXLOG_INFO("dbz3 traza: cola de cargas atascada {} cuadros: pant {} escena {:08X} nodos {}:{}", c - desde, REX_LOAD_U8(0x824BA110u), dbz3::campaigns::g_last_battle.load(), n, q);
}
REX_HOOK_RAW(sub_821D3888) {   // cabecera de cada manejador de escena (cada cuadro): solo los cambios
  __imp__sub_821D3888(ctx, base);
  if (TrazaEsc()) ColaPeriodica(base);
  ArreglaColaAtascada(base);
  if (TrazaEsc()) TrazaVuelcaNodo(base);
  if (TrazaEsc()) {
    static uint32_t ult[11] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    const uint32_t t = REX_LOAD_U32(0x8247A9A0u);
    const uint32_t P = REX_LOAD_U32(0x823D6CF4u), O = P ? REX_LOAD_U32(P) : 0u;
    const uint32_t cur[11] = {uint32_t(ctx.lr), uint32_t(REX_LOAD_U8(0x824BA110u)), REX_LOAD_U32(0x8247A990u), t ? REX_LOAD_U32(t + 28) : 0u,
                             REX_LOAD_U32(0x8247A998u), REX_LOAD_U32(0x8247A994u), ctx.r3.u32, REX_LOAD_U32(0x823CE1E0u + 35128), O, O ? uint32_t(REX_LOAD_U16(O + 4)) : 0xFFFFu, REX_LOAD_U32(0x823D75DCu)};
    if (std::memcmp(cur, ult, sizeof cur) != 0) {
      std::memcpy(ult, cur, sizeof cur);
      REXLOG_INFO("dbz3 traza: manejador desde {:08X} pant {} pedida {:08X} T+28 {:08X} lista {} fin {} err {} estado combate {} obj {:08X} obj+4 {:X} P {:08X} pend_carga {:08X}", cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], cur[6], cur[7], cur[8], cur[9], P, cur[10]);
    }
  }
}
REX_HOOK_RAW(sub_820FEA98) { if (TrazaEsc()) TrazaCeldas(base, "sub_820FEA98 inicio combate", uint32_t(ctx.lr)); __imp__sub_820FEA98(ctx, base); }
REX_HOOK_RAW(sub_82110810) { if (TrazaEsc()) TrazaCeldas(base, "sub_82110810", uint32_t(ctx.lr)); __imp__sub_82110810(ctx, base); }
REX_HOOK_RAW(sub_82092908) {   // peticion de carga de un recurso (id en r5)
  if (ctx.r5.u32 == 16384 && uint32_t(ctx.lr) == 0x821006E0) ++g_traza_epoca;
  if (TrazaEsc()) REXLOG_INFO("dbz3 traza: carga recurso id {} r3 {:08X} r6 {} r7 {:08X} r8 {:08X} (desde {:08X}) pant {}", ctx.r5.u32, ctx.r3.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, uint32_t(ctx.lr), REX_LOAD_U8(0x824BA110u));
  __imp__sub_82092908(ctx, base);
}
// C8: tareas de la carga del combate (nodos del planificador sub_820927E0): cada una registra solo cuando cambia su estado
namespace {
void TrazaCambio(const char* quien, uint32_t k, std::initializer_list<uint32_t> v) {
  static std::unordered_map<std::string, std::vector<uint32_t>> ult;
  static uint32_t vista = 0;
  if (vista != g_traza_epoca) { vista = g_traza_epoca; ult.clear(); }
  std::vector<uint32_t> cur(v);
  auto& u = ult[quien];
  if (u == cur) return;
  u = cur;
  std::string s;
  for (uint32_t x : cur) s += fmt::format(" {:08X}", x);
  REXLOG_INFO("dbz3 traza: tarea {} nodo {:08X}:{}", quien, k, s);
}
}  // namespace
#define TRAZA_TAREA(fn, ...) REX_HOOK_RAW(fn) { if (TrazaEsc()) TrazaCambio(#fn, ctx.r3.u32, {__VA_ARGS__}); __imp__##fn(ctx, base); }
TRAZA_TAREA(sub_821005F8, REX_LOAD_U32(ctx.r3.u32 + 48), uint32_t(REX_LOAD_U8(0x824BA110u)))
TRAZA_TAREA(sub_821005B0, REX_LOAD_U32(0x823D6CF4u), REX_LOAD_U32(REX_LOAD_U32(0x823D6CF4u) + 35248), REX_LOAD_U32(REX_LOAD_U32(0x823D6CF4u) + 35248) ? uint32_t(REX_LOAD_U16(REX_LOAD_U32(REX_LOAD_U32(0x823D6CF4u) + 35248) + 4)) : 0xFFFFu)
TRAZA_TAREA(sub_82100568, 1)
TRAZA_TAREA(sub_820FFF80, REX_LOAD_U32(0x823D75DCu))
TRAZA_TAREA(sub_820FEDB8, REX_LOAD_U32(ctx.r3.u32 + 48))
