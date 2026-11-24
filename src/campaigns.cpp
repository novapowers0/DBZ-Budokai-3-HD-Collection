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
#include <rex/cvar.h>
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
#include "heroe_api.h"
#include "heroe_rutas.h"

REXCVAR_DEFINE_BOOL(dbz3_heroe_fondos_historia, true, "DBZ3/Mods",
                    "Hero Mode story screens with a big picture band and a text strip underneath (BETA; off = the original narration panel)");
namespace dbz3::heroe { int Saga(); }   // heroe.cpp: saga siguiente del heroe activo
// heroe.cpp, rutas (ajuste dbz3_heroe_rutas, apagado de serie): tramo en curso, intro siguiente, cruce y opcional del mapa
namespace dbz3::heroe { bool Rutas(); int Tramo(); int ResolverRuta(); int ElegirCamino(int t); int OpcionalRuta(int t); int CaminoDelCruce(int i); bool NaveEnMapa(); int PlanetaTramo(int t); }
namespace dbz3::rpg { int ResolverRutaHeroe(); int ElegirCaminoHeroe(int tramo); }
// Cruce del mapa (heroe_v2_cruce.inl lo dibuja): el mapa de DU del Heroe esta libre (se vuela) con un cruce abierto, ahora.
namespace dbz3::campaigns {
std::atomic<long long> g_cruce_ms{-100000};
}
namespace dbz3::modos { void AsegurarPanel(); }   // modos.cpp
namespace dbz3::rpg { void AbrirHabitacionMapa(); }   // rpg.cpp (modos.h)
namespace dbz3::heroe_ui { bool HabitacionActiva(); }   // heroe_ui.cpp
namespace dbz3::campaigns {
long long AhoraMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Ultimo instante en que corrio el estado "volar por el mapa" (sub_821C4DA0). En Dragon Universe el
// byte de pantalla puede seguir a 5 (menu principal): online_panel.cpp lo usa para no aplicar alli los
// filtros del menu principal (fila TAG TEAM, X abre Tag Team).
std::atomic<long long> g_vuelo_mapa_ms{-100000};
bool VolandoMapa() { return AhoraMs() - g_vuelo_mapa_ms.load(std::memory_order_relaxed) < 1000; }
std::atomic<bool> g_cruce_oculto{false};   // B en el cruce: el panel se esconde hasta la proxima escena (ver el mapa)
std::atomic<bool> g_avatar_nave{false};    // se crea el personaje del mapa del Heroe como la nave de Freezer (rutas en Namek)
bool CruceEnMapa() { return AhoraMs() - g_cruce_ms.load() < 250 && !g_cruce_oculto.load(); }
}  // namespace dbz3::campaigns
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
std::atomic<int> g_battle_saga{0};   // DS20: saga (1-based) del ultimo combate lanzado; su victoria usa SU contenedor
std::atomic<int> g_saga_ganada{-1};      // C4: ultima saga (indice 0..) con un combate ganado en esta sesion: la que se cierra   // el cierre de saga acaba de guardar (escena 16384.5): el juego vuelve al titulo de DU
std::atomic<bool> g_du_cover{false};   // pantalla en negro mientras la entrada directa pasa titulo, menu y rueda de DU
std::atomic<int> g_du_auto{-1};   // casilla que la rueda de DU confirma sola (hub::RequestDu); -1 = nada
std::unordered_map<uint32_t, size_t> g_word_owner;   // palabra de [modelo_mapa]/[traje_mapa]/[animacion_mapa] -> campana (indice de g_story_codes)
std::unordered_map<uint32_t, int> g_start_form;  // [forma_jugador]: palabra del combate -> forma inicial
std::unordered_map<uint32_t, int> g_rival_suit;  // [traje_rival]: palabra del combate -> traje del rival
std::unordered_map<uint32_t, int> g_map_model;   // [modelo_mapa]: palabra del combate -> modelo en el mapa
std::unordered_map<uint32_t, int> g_map_suit;    // [traje_mapa]: palabra del combate -> traje en el mapa
std::unordered_map<uint32_t, uint32_t> g_retrato_busqueda;   // [retrato_busqueda]: casilla -> entrada de data_cmn con su busto (R1 en el mapa)
std::unordered_map<uint32_t, int> g_map_anim;    // [animacion_mapa]: palabra -> casilla cuyo vuelo del mapa se usa
std::atomic<uint32_t> g_last_battle{0xFFFFFFFFu}; // ultimo combate lanzado (palabra)
// [codigos_saga] (sagas 5+ del Heroe): el motor DU solo reproduce codigos nativos, asi que las sagas
// que no caben en los 4 bloques de la casilla reutilizan el bloque 0 y cada una lleva sus contenedores.
// casilla:saga -> {mapa, combate, dialogo, intro}; se elige por heroe::Saga() mientras se juega esa casilla.
struct SagaCodes {
  uint32_t casilla = 0, saga = 0, mapa = 0, combate = 0, dialogo = 0, intro = 0;
};
std::vector<SagaCodes> g_saga_codes;

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
    bool in_codes = false, in_story = false, in_wheel = false, in_fighter = false, in_intro = false, in_forms = false, in_suits = false, in_bust = false, in_models = false, in_map_suits = false, in_map_anims = false, in_saga_codes = false;
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
        in_bust = line.find("[retrato_busqueda]") != std::string::npos;
        in_saga_codes = line.find("[codigos_saga]") != std::string::npos;
        continue;
      }
      if (in_saga_codes) {   // casilla:saga = mapa,combate,dialogo,intro (los contenedores de esa saga)
        char* end = nullptr;
        const long casilla = std::strtol(line.c_str(), &end, 10);
        if (end == line.c_str() || *end != ':') continue;
        const long saga = std::strtol(end + 1, &end, 10);
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != '=') continue;
        SagaCodes row;
        row.casilla = uint32_t(casilla), row.saga = uint32_t(saga);
        uint32_t* dst[4] = {&row.mapa, &row.combate, &row.dialogo, &row.intro};
        for (uint32_t* d : dst) {
          const long v = std::strtol(end + 1, &end, 10);
          if (end == line.c_str()) break;
          *d = uint32_t(v);
          while (*end == ' ' || *end == '\t') ++end;   // el siguiente strtol empieza tras la coma
        }
        if (row.casilla < 64 && row.saga >= 1 && row.saga <= 64) g_saga_codes.push_back(row);
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
      if (in_bust && eq != std::string::npos) {
        g_retrato_busqueda[uint32_t(std::strtoul(line.c_str(), nullptr, 10))] = uint32_t(std::strtoul(line.c_str() + eq + 1, nullptr, 10));
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
// Intro de la saga sig (1-based): 102/104/106 del juego; la 4.a y las siguientes (5-8) reutilizan la 100
// (bloque 0 de la casilla) y [codigos_saga] elige el contenedor de cada una. Antes, 5+ caian en el mapa 5336.
uint32_t IntroDeSaga(int sig) { return sig <= 3 ? uint32_t(100 + 2 * std::max(1, sig)) : 100u; }
int DuFighter(uint8_t* base) {
  if (g_slot_fighter.empty() || dbz3::g_guest_eu) return -1;
  const uint32_t w = g_last_battle.load(), code = w >> 16, slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  // tambien en los combates opcionales nativos del mapa (2400-2404, puntos del Heroe): en la casilla manda su luchador
  const bool opcional = code >= 2400 && code < 2405;
  if (w == 0xFFFFFFFFu || (!opcional && (!CodigoCombate(code) || (code - 5000) % 100 != slot))) return -1;
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
// Saga 0-based en curso. DBZ3_TEST_HEROE_SAGA permite entrar directo a una saga sin pasar por el menu
// del Heroe (que es quien llama a heroe::CargarProgreso); sin la variable manda la ficha.
int SagaActual() {
  if (dbz3::heroe::Rutas()) return dbz3::heroe::Tramo();   // rutas: el TRAMO en curso (0-7 las de siempre, 8+ variantes)
  // La env solo FIJA la saga de entrada; al ganar el ultimo combate heroe::Saga() avanza y manda el
  // (si no, una partida de prueba saltada a la saga 5 volveria a la intro de la 5 en bucle).
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_SAGA")) return std::max(std::atoi(e), dbz3::heroe::Saga());
  return dbz3::heroe::Saga();
}
// Sagas 5+ del Heroe: el bloque 0 de la casilla se reutiliza (codigos nativos) y cada saga tiene sus
// propios contenedores ([codigos_saga]). Se elige por la saga en curso (heroe::Saga(), 0-based).
bool HeroSagaContainer(uint8_t* base, uint32_t code, uint32_t* out) {
  if (g_saga_codes.empty() || dbz3::g_guest_eu) return false;
  const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  const bool es_heroe = g_slot_fighter.count(slot) ||
                        std::find(g_virtual_slots.begin(), g_virtual_slots.end(), int(slot)) != g_virtual_slots.end();
  if (!es_heroe) return false;
  const int sg = SagaActual();
  int row_saga = sg + 1;   // mapa/dialogo/intro/batalla: la saga en curso (o la siguiente al cerrar)
  if (code >= 5400 && code < 5800 && code % 100 == slot) {
    // la escena de victoria (entrada impar) de un combate se pide DESPUES de DuVictoria: si era el ultimo,
    // g_saga ya ha avanzado y la siguiente no es la suya. Se usa la saga del combate que se lanzo.
    const uint32_t word = g_last_battle.load();
    if ((word >> 16) == code && (word & 1) && g_battle_saga.load() > 0) row_saga = g_battle_saga.load();
  }
  // El cierre atomico deja el tramo en -1 ANTES de cargar el dialogo de la ultima victoria.
  // Ese dialogo conserva el contenedor del combate ganado, incluso con la ruta ya terminada.
  if (row_saga < 1 || row_saga > 64) return false;
  const auto it = std::find_if(g_saga_codes.begin(), g_saga_codes.end(),
                               [&](const SagaCodes& r) { return r.casilla == slot && r.saga == uint32_t(row_saga); });
  if (it == g_saga_codes.end()) return false;
  uint32_t cont = 0;
  if (code >= 100 && code <= 106) cont = it->intro;                              // 100/102/104/106: intro de la saga
  else if (code >= 5000 && code < 5400 && code % 100 == slot) cont = it->mapa;   // 5x00+casilla: mapa
  else if (code >= 5400 && code < 5800 && code % 100 == slot) cont = it->combate; // 5x00+casilla: combate
  else if (code >= 5800 && code < 6200 && code % 100 == slot) cont = it->dialogo; // 6x00+casilla: dialogo
  if (!cont) return false;
  *out = cont;
  return true;
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
  // C4/DS20: saga 5+ del Heroe: el mismo codigo nativo (bloque 0) lleva al contenedor de la saga en curso
  if (uint32_t saga_cont = 0; dbz3::campaigns::HeroSagaContainer(base, uint32_t(code), &saga_cont)) {
    cont = saga_cont;
    REXLOG_INFO("dbz3 campanas: saga {}: codigo {} -> contenedor {} (del mod, por saga)", dbz3::campaigns::SagaActual() + 1, code, cont);
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
  if (const auto it = dbz3::campaigns::g_slot_fighter.find(slot); it != dbz3::campaigns::g_slot_fighter.end()) {
    uint32_t id = it->second;
    // S8 (ronda 6): la campana del Heroe pide el ID 63, pero el heroe propio puede estar montado con otro ID (o no
    // estar: copia sin heroe_propio). Con 63 sin modelo el guest cerraba al crear el personaje del mapa (de/pt).
    // Se usa el ID real de la sesion y, si no hay heroe, 0 (Goku): el mapa y la historia del Heroe siguen.
    if (dbz3::campaigns::g_avatar_nave.load()) {   // rutas en Namek: el avatar del mapa es Freezer (traje 2 = su nave)
      ctx.r3.u64 = 27;
      return;
    }
    if (id == 63) {
      const int propio = dbz3::heroe::IdPropio();
      id = propio >= 0 ? uint32_t(propio) : 0;
      if (propio < 0) REXLOG_WARN("dbz3 campanas: Heroe sin ID propio en la sesion: casilla {} usa 0 (Goku) para no cerrar", slot);
    }
    ctx.r3.u64 = id;
  }
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
      const bool rutas = dbz3::heroe::Rutas() && !ts;   // rutas: el tramo elegido (si el anterior acabo, se elige ahora)
      int tramo = rutas ? dbz3::rpg::ResolverRutaHeroe() : -1;
      if (tramo == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha no se pudo guardar al resolver la intro; reintento");
        tramo = dbz3::rpg::ResolverRutaHeroe();
        if (tramo == -3) {
          REXLOG_WARN("dbz3 campanas: ruta: la ficha sigue sin guardarse; no se lanza la intro");
          return;
        }
      }
      if (tramo == -4) {
        REXLOG_WARN("dbz3 campanas: ruta: etapa 5 pendiente con Esferas sin reunir; se conserva el mapa");
        return;
      }
      const int sig = ts ? std::atoi(ts) : (rutas ? tramo : dbz3::campaigns::SagaActual()) + 1;
      const uint32_t intro = dbz3::campaigns::IntroDeSaga(sig);
      if (rutas ? sig >= 1 : sig <= 8) ctx.r3.u64 = (uint64_t(intro) << 16) | slot;
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
    const bool nueva_hist = dbz3::heroe::rutas::DuUsaNuevaHistoria(
        dbz3::campaigns::SagaActual(), dbz3::heroe::Saga(), dbz3::heroe::ColeccionVueltasOn());
    REX_STORE_U8(REX_LOAD_U32(ctx.r3.u32 + 48) + 85, (reanudar_nueva && nueva_hist) ? 1 : 0);
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
void ShenronDeseo();                // rpg.cpp: reserva las siete hasta confirmar una fila
bool DeseoRaza();                   // false si esta eleccion ya se confirmo
bool DeseoCapsula();                // confirma una fila nativa, sin conceder cambio de raza
bool DeseoAplazado();               // POST3 "B Pensarlo": sin deseo, las Esferas vuelven
bool DeseoHistoria(bool revivir);
void EsferaDelMapa(int evento);     // H02: identidad 5/6 de la marca, -1 para historias antiguas
int EsferasDeCasilla(int casilla);  // rpg.cpp: Esferas del Heroe si la casilla es la suya; si no, -1
}
static int g_esfera_ruta = -1;
static bool g_historia_shenron = false, g_historia_salida = false, g_historia_aplazada = false;
static bool g_historia_callback = false;   // la limpieza del menu puede llegar despues del cambio de pantalla
std::chrono::steady_clock::time_point g_ultima_escena{};   // C7: cuando se lanzo la ultima escena de la historia (el toque de prueba espera a que acabe su charla)
DBZ3_HOOK(sub_8215E918, dbz3eu_sub_8215E8D8) {
  g_ultima_escena = std::chrono::steady_clock::now();
  REXLOG_INFO("dbz3 campanas: escena {} entrada {}", ctx.r3.u32 >> 16, ctx.r3.u32 & 0xFFFF);
  dbz3::campaigns::g_du_cover = false;   // la primera escena de la historia: ya se ve
  // C3: la escena de victoria (entrada impar) de un combate de la casilla del Heroe: su progreso
  if (const uint32_t code = ctx.r3.u32 >> 16, entry = ctx.r3.u32 & 0xFFFF; !dbz3::g_guest_eu && dbz3::campaigns::CodigoCombate(code) && (entry & 1) &&
      dbz3::campaigns::g_slot_fighter.count(REX_LOAD_U8(dbz3::GA(0x824A58A0))) && (code - 5000) % 100 == REX_LOAD_U8(dbz3::GA(0x824A58A0))) {
    // DS20: las sagas 5-8 reutilizan el codigo de combate del bloque 0 (5436); la saga en curso manda
    // (heroe::Saga() es la 0-based que se esta jugando y solo avanza al ganar el ultimo combate).
    const int heroe_saga = dbz3::campaigns::SagaActual();
    const int saga = dbz3::heroe::Rutas() || (heroe_saga >= 4 && heroe_saga < 8) ? heroe_saga : dbz3::campaigns::SagaDeCombate(code);   // rutas: el tramo en curso
    static uint32_t last = 0;
    static int last_saga = -1;
    dbz3::campaigns::g_saga_ganada = saga;
    if (last != ctx.r3.u32 || last_saga != saga)
      dbz3::rpg::DuVictoria(saga, int(entry / 2));   // bloque 5500 = saga 0 del Heroe (SAGA0 = 1)
    last = ctx.r3.u32, last_saga = saga;
    REXLOG_INFO("dbz3 campanas: victoria de la saga {} (codigo {} entrada {}): heroe::Saga() {}", saga + 1, code, entry, heroe_saga);
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
  // Rutas del Heroe (dbz3_heroe_rutas, apagado de serie; heroe_sagas_a_du.py las genera asi):
  //  - intro de saga (100/102/104/106 . casilla): la del tramo que elige la ruta ahora; sin tramos, el final (1.39)
  //  - marca de camino (5400+casilla . 1000+2*tramo) y "?" del opcional (. 1200): el combate de verdad de ese tramo
  //  - primer combate de un tramo (entrada 0) con un cruce abierto: se eligio el camino de la intro
  if (const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
      !dbz3::g_guest_eu && dbz3::heroe::Rutas() && dbz3::campaigns::g_slot_fighter.count(slot)) {
    const uint32_t code = ctx.r3.u32 >> 16, entry = ctx.r3.u32 & 0xFFFF, antes = ctx.r3.u32;
    auto bcode = [&](int t) { return t < 3 ? 5500u + 100u * uint32_t(t) + slot : 5400u + slot; };   // campana.py bcode
    if ((code == 100 || code == 102 || code == 104 || code == 106) && entry == slot) {
      int t = dbz3::rpg::ResolverRutaHeroe();
      if (t == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha no se pudo guardar; reintento al lanzar la escena");
        t = dbz3::rpg::ResolverRutaHeroe();
      }
      if (t == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha sigue sin guardarse; se conserva el mapa y el tramo actual");
        return;
      }
      if (t == -4) {
        REXLOG_WARN("dbz3 campanas: ruta: etapa 5 pendiente con Esferas sin reunir; se conserva el mapa");
        return;
      }
      if (t == -2) g_historia_shenron = true, g_historia_salida = false;
      // R23 HER-13: el cierre del tramo ya no cambia de planeta (heroe_sagas_a_du.py); la vuelta al mapa tras esta intro
      // carga el del tramo nuevo (+4576, lo que hacia la op 47)
      if (const int pl = dbz3::heroe::PlanetaTramo(t); pl >= 0 && pl < 2) {
        const uint32_t blk = 0x8247A9B0u + 128u + 4624u * slot;
        if (REX_LOAD_U8(blk + 4576) != uint32_t(pl)) REXLOG_INFO("dbz3 campanas: ruta: tramo {}: planeta del mapa {} -> {}", t, REX_LOAD_U8(blk + 4576), pl);
        REX_STORE_U8(blk + 4576, uint8_t(pl));
      }
      ctx.r3.u64 = t < 0 ? ((1u << 16) | 39u) : ((dbz3::campaigns::IntroDeSaga(t + 1) << 16) | slot);
    } else if (code == 5400 + slot && (entry == 1300 || entry == 1302)) {
      const int evento = entry == 1300 ? 5 : 6;
      if (!dbz3::heroe::EsferaRutaPendiente(evento)) return;
      g_esfera_ruta = evento;
      ctx.r3.u64 = (2000u << 16) | 6u;   // animacion y evento 38 originales, identidad propia
    } else if (code == 5400 + slot && entry == 1304 && dbz3::heroe::DeseoHistoriaPendiente()) {
      g_historia_shenron = true, g_historia_salida = false;
      ctx.r3.u64 = (1u << 16) | 39u;
    } else if (code == 5400 + slot && entry >= 1000 && entry < 1300 && !(entry & 1)) {
      int t = entry >= 1200 ? dbz3::heroe::Tramo() : dbz3::rpg::ElegirCaminoHeroe(int(entry - 1000) / 2);
      if (t == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha no se pudo guardar al elegir el camino; reintento");
        t = dbz3::rpg::ElegirCaminoHeroe(int(entry - 1000) / 2);
      }
      if (t == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha sigue sin guardarse; no se lanza el combate del camino");
        return;
      }
      const int k = entry >= 1200 ? dbz3::heroe::OpcionalRuta(t) : 0;
      if (k >= 0) ctx.r3.u64 = (bcode(t) << 16) | uint32_t(2 * k);
    } else if (dbz3::campaigns::CodigoCombate(code) && (code - 5000) % 100 == slot && entry == 0) {
      if (dbz3::rpg::ElegirCaminoHeroe(-1) == -3 && dbz3::rpg::ElegirCaminoHeroe(-1) == -3) {
        REXLOG_WARN("dbz3 campanas: ruta: la ficha no se pudo guardar; se conserva el mapa");
        return;
      }
    }
    if (ctx.r3.u32 != antes)
      REXLOG_INFO("dbz3 campanas: ruta: escena {}.{} -> {}.{} (tramo {})", antes >> 16, antes & 0xFFFF, ctx.r3.u32 >> 16, ctx.r3.u32 & 0xFFFF,
                  dbz3::heroe::Tramo());
  }
  dbz3::campaigns::g_last_battle = ctx.r3.u32;
  dbz3::campaigns::g_cruce_oculto = false;   // rutas: cada escena nueva vuelve a mostrar el panel del cruce
  // Heroe: sus Esferas (una por saga, heroe.cpp) se copian a la mascara nativa de la casilla, la que comprueba sub_8215C1B0 y pinta el DU
  if (const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0)); !dbz3::g_guest_eu && dbz3::campaigns::g_slot_fighter.count(cas)) {
    if (const int esf = dbz3::rpg::EsferasDeCasilla(int(cas)); esf >= 0) REX_STORE_U8(0x8247AA30u + 4624u * cas + 4580u, uint8_t(esf));
  }
  // PRUEBA (DBZ3_TEST_HEROE_FINAL=1): la llegada al mapa del Heroe salta a su final (1.39), para medir el camino a Shenron sin 27 combates
  if (static const bool salto = std::getenv("DBZ3_TEST_HEROE_FINAL") != nullptr; salto) {
    const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    if (dbz3::campaigns::g_slot_fighter.count(cas) && ctx.r3.u32 == (((5100u + cas) << 16) | 0u)) ctx.r3.u64 = (1u << 16) | 39u;
  }
  // DS20: recuerda a que saga pertenece el combate que se lanza (su victoria puede llegar con g_saga ya avanzado)
  if (const uint32_t code = ctx.r3.u32 >> 16, entry = ctx.r3.u32 & 0xFFFF, slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
      !dbz3::g_guest_eu && dbz3::campaigns::CodigoCombate(code) && !(entry & 1) &&
      dbz3::campaigns::g_slot_fighter.count(slot) && (code - 5000) % 100 == slot)
    dbz3::campaigns::g_battle_saga = dbz3::campaigns::SagaActual() + 1;

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
  // PRUEBA (DBZ3_TEST_HEROE_FINAL=1, HEROE-MAQ): aqui si pasa la escena que se lanza; el combate de la casilla del Heroe salta a su final (1.39)
  if (static const bool salto2 = std::getenv("DBZ3_TEST_HEROE_FINAL") != nullptr; salto2 && (ctx.r3.u32 & 0xFFFFu) == 0) {
    const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0)), code = ctx.r3.u32 >> 16;
    if (code >= 5000u && code < 6000u && (code - 5000u) % 100u == cas && dbz3::rpg::EsferasDeCasilla(int(cas)) >= 0) {
      REXLOG_INFO("dbz3 campanas: FINAL prueba: combate {} -> final 1.39", code);
      ctx.r3.u64 = (1u << 16) | 39u;
    }
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

// H02: productores del nucleo. Tambien cubren historias ya montadas con el generador anterior.
// Crear y retirar con las rutinas nativas mantiene los objetos del minimapa y sus recursos.
static void MarcasEsferasHistoria(PPCContext& ctx, uint8_t* base, uint32_t slot) {
  const uint32_t marks = 0x8247A9B0u + 128u + 4624u * slot + 128u;
  const uint32_t codigo = (5400u + slot) << 16;
  const bool deseo = dbz3::heroe::DeseoHistoriaPendiente();
  const bool reunion_cerrada = deseo || (dbz3::heroe::Saga() >= 5 &&
      (dbz3::heroe::EsferaRutaPendiente(5) || dbz3::heroe::EsferaRutaPendiente(6)));
  const PPCContext saved = ctx;
  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t m = marks + 64 * i, w = REX_LOAD_U32(m);
    if (w == 0xFFFFFFFFu) continue;
    const bool propia = w == (codigo | 1300u) || w == (codigo | 1302u) || w == (codigo | 1304u);
    const bool vigente = w == (codigo | 1300u) ? dbz3::heroe::EsferaRutaPendiente(5) :
                         w == (codigo | 1302u) ? dbz3::heroe::EsferaRutaPendiente(6) :
                         w == (codigo | 1304u) && deseo;
    // Solo se retiran las esferas viejas de ESTA historia (contenedor 5400+casilla):
    // una marca 1089 nativa de otra casilla/historia no es nuestra y no se toca.
    if ((propia && !vigente) || (!propia && (w >> 16) == 5400u + slot && REX_LOAD_U32(m + 4) == 1089u) ||
        (reunion_cerrada && !propia && dbz3::campaigns::IsModBattle(base, w))) {
      // 2828 libera los recursos y marca el hueco como vacio; 2958 solo cambia +32/+36/+40.
      ctx = saved;
      ctx.r3.u64 = w;
      sub_821C2828(ctx, base);
    }
  }
  for (int evento = 5; evento <= 7; ++evento) {
    if (!(evento == 7 ? deseo : dbz3::heroe::EsferaRutaPendiente(evento))) continue;
    const uint32_t word = codigo | uint32_t(1300 + 2 * (evento - 5));
    bool existe = false, libre = false;
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t w = REX_LOAD_U32(marks + 64 * i);
      existe |= w == word;
      libre |= w == 0xFFFFFFFFu;
    }
    if (existe || !libre) continue;   // la creadora nativa no admite la tabla llena
    ctx = saved;
    ctx.r1.u32 -= 256;
    REX_STORE_U32(ctx.r1.u32, saved.r1.u32);
    const uint32_t pos = ctx.r1.u32 + 96;
    const float xyz[4] = {evento == 5 ? 9200.0f : evento == 6 ? 460.0f : 3800.0f,
                          122.0f, evento == 5 ? -8700.0f : evento == 6 ? -9065.0f : -4200.0f, 1.0f};
    for (int j = 0; j < 4; ++j) { uint32_t v; std::memcpy(&v, &xyz[j], 4); REX_STORE_U32(pos + 4 * j, v); }
    ctx.r3.u64 = word, ctx.r4.u64 = 1089, ctx.r5.u64 = 29, ctx.r6.u64 = pos, ctx.f1.f64 = 1000;
    sub_821C2778(ctx, base);
    REXLOG_INFO("dbz3 heroe: H02 marca normal {}.{} creada ({})", word >> 16, word & 0xFFFF,
                evento == 7 ? "Shenron pendiente" : evento == 5 ? "Esfera 6" : "Esfera 7");
  }
  ctx = saved;
}

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

#include "heroe_start.inl"  // START de DU: fila virtual de la Habitacion.

// Estado "volar por el mapa" (sub_821C4DA0, r3 = tarea del mapa; en +28 su siguiente estado). Las
// historias de mod dejan su combate como marca del mapa (op 24); tras 5 s de mapa (lo que tarda el mapa en quedar libre tras un combate) se toca sola,
// haciendo lo mismo que el juego al tocar una marca de combate (0x821C4B90 / 0x821C5040): marca
// tocada en +232, escena lanzada, mapa al estado 7 (entrar en un evento) y la tarea a su manejador.
// US y EU (direcciones medidas en la US; la pareja EU, de src/eu_map_gen.h).
DBZ3_HOOK_EU(sub_821C4DA0) {
  using namespace dbz3::campaigns;
  g_vuelo_mapa_ms.store(AhoraMs(), std::memory_order_relaxed);
  if (const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0)); !dbz3::g_guest_eu && dbz3::heroe::Rutas() &&
      g_slot_fighter.count(slot) && REX_LOAD_U32(dbz3::GA(0x8234DD1C)) == 0xFFFFFFFF)
    MarcasEsferasHistoria(ctx, base, slot);
  // Desde 2026-10-11 las marcas de las historias de mod son como las del juego (icono 1, letrero y
  // punto en el minimapa) y se tocan volando hasta ellas; el toque solo queda para pruebas.
  // DBZ3_AUTO_TOUCH=N: las N primeras marcas (sin numero, todas)
  static int auto_touch = std::getenv("DBZ3_AUTO_TOUCH") ? (std::atoi(std::getenv("DBZ3_AUTO_TOUCH")) > 0 ? std::atoi(std::getenv("DBZ3_AUTO_TOUCH")) : 1 << 30) : 0;
  static const int auto_touch_s = std::getenv("DBZ3_AUTO_TOUCH_S") ? std::max(1, std::atoi(std::getenv("DBZ3_AUTO_TOUCH_S"))) : 0;   // C7: segundos con la marca antes de tocarla
  // Rutas del Heroe (dbz3_heroe_rutas): con un cruce abierto y el mapa libre se avisa al dibujo (heroe_v2_cruce.inl) y
  // X / Y eligen el camino A / B tocando SU marca, como si se llegara a ella (mando: +14 = recien pulsados; cuadrado
  // 0x8000 = X, triangulo 0x1000 = Y).
  // Habitacion (dbz3_heroe_habitacion, apagado de serie): en el mapa de DU del Heroe libre, SELECT (0x0001 de la palabra de recien
  // pulsados del juego) abre la Habitacion encima del mapa (rpg.cpp la dibuja; el panel bloquea el mando del juego mientras esta abierta).
  if (const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0)); dbz3::heroe_ui::HabitacionActiva() &&
                                                                 g_slot_fighter.count(slot) && REX_LOAD_U32(dbz3::GA(0x8234DD1C)) == 0xFFFFFFFF) {
    if (static bool panel = false; !panel) panel = true, dbz3::modos::AsegurarPanel();
    const uint32_t pal = dbz3::GA(0x8238B780) + REX_LOAD_U32(dbz3::GA(0x8238B670)) * 184 + 14;
    if (const uint16_t w = REX_LOAD_U16(pal); w & 0x0001) {
      REX_STORE_U16(pal, uint16_t(w & ~0x0001));   // el juego no ve este SELECT (abriria su ayuda de controles)
      dbz3::rpg::AbrirHabitacionMapa();
    }
  }
  uint32_t mark_xy = 0;
  if (const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0)); !dbz3::g_guest_eu && dbz3::heroe::Rutas() && g_slot_fighter.count(slot) &&
      dbz3::heroe::CaminoDelCruce(1) >= 0 && REX_LOAD_U32(dbz3::GA(0x8234DD1C)) == 0xFFFFFFFF) {
    if (static bool panel = false; !panel) panel = true, dbz3::modos::AsegurarPanel();   // el dibujo del cruce va en el panel de modos
    g_cruce_ms = AhoraMs();
    const uint32_t marks = dbz3::GA(0x8247A9B0) + 128 + 4624 * (slot < 38 ? slot : 0) + 128;
    // PRUEBA R23 (DBZ3_TEST_CRUCE_CAM=1): sonda para anclar el cruce al mapa: la camara del mapa y las marcas de camino, 1 vez/s
    if (static const bool sonda = std::getenv("DBZ3_TEST_CRUCE_CAM") != nullptr; sonda) {
      static long long ult = 0;
      if (AhoraMs() - ult > 1000) {
        ult = AhoraMs();
        auto f = [&](uint32_t a) { const uint32_t v = REX_LOAD_U32(a); float x; std::memcpy(&x, &v, 4); return x; };
        constexpr uint32_t kMapa = 0x82479A60;
        const uint32_t o = REX_LOAD_U32(REX_LOAD_U32(kMapa + 188) + 1132), cam = REX_LOAD_U32(o + 20);
        std::string t = "kMap:";
        for (uint32_t k = 180; k < 260; k += 4) t += fmt::format(" {}={:.4g}", k, f(kMapa + k));
        REXLOG_INFO("dbz3 sonda cruce: {}", t);
        for (const uint32_t base_c : {kMapa}) {   // +336 posicion, +416..+476 matriz mundo -> camara (R23, medido)
          t.clear();
          for (uint32_t k = 0; k < 4096; k += 4) t += fmt::format(" {}={:.4g}", k, f(base_c + k));
          REXLOG_INFO("dbz3 sonda cruce: {:08X}:{}", base_c, t);
        }
        for (uint32_t i = 0; i < 64; ++i) {
          const uint32_t w = REX_LOAD_U32(marks + 64 * i);
          if (w != 0xFFFFFFFFu && w != 0) REXLOG_INFO("dbz3 sonda cruce: marca {} {}.{} pos {:.1f} {:.1f} {:.1f}", i, w >> 16, w & 0xFFFF,
                                                     f(marks + 64 * i + 16), f(marks + 64 * i + 20), f(marks + 64 * i + 24));
        }
      }
    }
    const uint16_t pulsados = REX_LOAD_U16(0x8238B780u + REX_LOAD_U32(0x8238B670u) * 184 + 14);
    if (pulsados & 0x2000) {   // B (circulo): "Atras" de la maqueta: esconde el panel del cruce (o lo vuelve a sacar)
      g_cruce_oculto = !g_cruce_oculto.load();
      REXLOG_INFO("dbz3 campanas: ruta: B {} el panel del cruce", g_cruce_oculto.load() ? "esconde" : "vuelve a mostrar");
    }
    // R23 (usuario): con el panel del cruce a la vista el mapa se queda quieto (palancas y direcciones a cero, como combate.cpp):
    // las burbujas son un menu, no lugares del mapa, y no deben "flotar" sobre un mapa que se mueve. B lo esconde y el mapa
    // vuelve a moverse (para mirar o volar hasta una marca).
    if (!g_cruce_oculto.load()) {
      const uint32_t r = 0x8238B780u + REX_LOAD_U32(0x8238B670u) * 184;
      REX_STORE_U32(r + 24, 0);
      for (uint32_t o = 108; o <= 114; o += 2) REX_STORE_U16(r + o, 0);
    }
    const int i = (pulsados & 0x8000) ? 0 : (pulsados & 0x1000) ? 1 : -1;
    if (const int t = i >= 0 ? dbz3::heroe::CaminoDelCruce(i) : -1; t >= 0) {
      const uint32_t palabra = i == 0 ? ((t < 3 ? 5500u + 100u * uint32_t(t) + slot : 5400u + slot) << 16)
                                      : (((5400u + slot) << 16) | uint32_t(1000 + 2 * t));
      for (uint32_t k = 0; k < 64 && !mark_xy; ++k)
        if (REX_LOAD_U32(marks + 64 * k) == palabra) mark_xy = marks + 64 * k;
      REXLOG_INFO("dbz3 campanas: ruta: {} elige el camino {} (tramo {}){}", i == 0 ? "X" : "Y", i == 0 ? "A" : "B", t, mark_xy ? "" : ": su marca no esta");
    }
  }
  if ((auto_touch > 0 || mark_xy) && (!g_wheel_ids.empty() || !g_virtual_slots.empty()) && REX_LOAD_U32(dbz3::GA(0x8234DD1C)) == 0xFFFFFFFF) {
    const uint32_t slot = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    const uint32_t marks = dbz3::GA(0x8247A9B0) + 128 + 4624 * (slot < 38 ? slot : 0) + 128;
    uint32_t mark = 0;
    static const bool del_final = std::getenv("DBZ3_AUTO_TOUCH_ULTIMA") != nullptr;   // PRUEBA: toca del ultimo hueco al primero y cualquier tipo de marca
    for (uint32_t i = 0; i < 64 && (!mark || del_final); ++i) {
      const uint32_t w = REX_LOAD_U32(marks + 64 * i);
      if (del_final ? (w != 0xFFFFFFFFu && w != 0u) : IsModBattle(base, w)) mark = marks + 64 * i;
    }
    // PRUEBA rutas (DBZ3_TEST_RUTA_TOCAR="1200,1016,0"): entradas de marca que se tocan antes, en orden y una vez cada una
    // (1000+2*tramo = camino de ese tramo, 1200 = el opcional "?", 0 = el primer combate de un tramo, el camino de la intro)
    static std::vector<uint32_t> preferidas = [] {
      std::vector<uint32_t> v;
      if (const char* e = std::getenv("DBZ3_TEST_RUTA_TOCAR"))
        for (const char* p = e; *p;) {
          char* fin = nullptr;
          v.push_back(uint32_t(std::strtoul(p, &fin, 10)));
          p = (*fin == ',') ? fin + 1 : fin;
          if (fin == p && *p) break;
        }
      return v;
    }();
    if (!del_final && !preferidas.empty()) {
      for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t w = REX_LOAD_U32(marks + 64 * i);
        if (IsModBattle(base, w) && (w & 0xFFFF) == preferidas.front()) {
          mark = marks + 64 * i;
          break;
        }
      }
    }
    if (mark && !preferidas.empty() && (REX_LOAD_U32(mark) & 0xFFFF) == preferidas.front() && g_map_since != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() - g_map_since > std::chrono::seconds(5) && std::chrono::steady_clock::now() - g_ultima_escena > std::chrono::seconds(auto_touch_s))
      preferidas.erase(preferidas.begin());   // se va a tocar ahora (abajo): la siguiente preferida, en el proximo mapa
    if (mark_xy) mark = mark_xy;   // X / Y del cruce: esa marca, ya
    if (!mark) {
      g_map_since = {};
    } else if (!mark_xy && g_map_since == std::chrono::steady_clock::time_point{}) {
      g_map_since = std::chrono::steady_clock::now();
    } else if (mark_xy || (std::chrono::steady_clock::now() - g_map_since > std::chrono::seconds(5) &&
                           std::chrono::steady_clock::now() - g_ultima_escena > std::chrono::seconds(auto_touch_s))) {
      g_map_since = {};
      if (!mark_xy) --auto_touch;
      const uint32_t task = ctx.r3.u32, word = REX_LOAD_U32(mark);
      REXLOG_INFO("dbz3 campanas: se toca la marca de combate {} entrada {} (hueco {}, icono {})", word >> 16, word & 0xFFFF, (mark - marks) / 64, REX_LOAD_U32(mark + 4));
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
  // S8-heroe (10/11): casilla del Heroe sin su modelo montado (IdPropio -1: copia sin heroe_propio/_roster 63) dejaba
  // modelo -1 y el guest cerraba al crear el personaje del mapa (access violation en de/pt, 0xC0000005). Se usa Goku (0)
  // como respaldo SOLO en las casillas virtuales del Heroe: el mapa se ve y no cierra; el aviso queda en el registro.
  if (l.model < 0 && std::find(g_virtual_slots.begin(), g_virtual_slots.end(), int(slot)) != g_virtual_slots.end()) {
    REXLOG_WARN("dbz3 campanas: casilla {} del Heroe sin modelo (heroe propio no montado): se usa el modelo 0 (Goku) para no cerrar", slot);
    l.model = 0;
    if (l.suit < 0) l.suit = 0;
    if (l.anim < 0) l.anim = 0;
  }
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
  // Esta tabla (casilla * 5 + traje) es el fichero del busto de R1 (3852 = Goku), no el vuelo. Con busto propio queda fijo en la casilla.
  for (const auto& [cas, ent] : g_retrato_busqueda) {
    if (cas < 38) for (uint32_t k = 0; k < 5; ++k) REX_STORE_U32(kFlight + (cas * 5 + k) * 4, ent);
  }
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
      if (!g_retrato_busqueda.count(slot)) {
        entry = kFlight + (slot * 5 + k) * 4;
        old_entry = REX_LOAD_U32(entry);
        REX_STORE_U32(entry, REX_LOAD_U32(kFlight + (uint32_t(l.anim) * 5 + k) * 4));
      }
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
  MapLook l = rec ? PickLook(base, "al crear") : MapLook{};
  // Rutas del Heroe en Namek (maquetas 1 y 2 aprobadas): el avatar del mapa es la nave de Freezer, como en su historia de DU:
  // ID 27, modelo 0, traje 2 (el modelo 193) y el vuelo sentado de la casilla 22 (historia_freezer.toml)
  const uint32_t slot = rec ? REX_LOAD_U8(rec + 4577) : 0xFFu;
  const bool nave = rec && !dbz3::g_guest_eu && g_slot_fighter.count(slot) && dbz3::heroe::NaveEnMapa();
  if (nave) {
    l.model = 0, l.suit = 2, l.anim = 22;
    REXLOG_INFO("dbz3 campanas: ruta: avatar del mapa = nave de Freezer (casilla {})", slot);
  }
  g_avatar_nave = nave;
  WithLook(base, rec, l, [&] { orig(ctx, base); });
  g_avatar_nave = false;
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
// Shenron: sub_8215C1B0 = nativa 0x115 (grupo 17, indice 5) de la VM de guiones #SPX (sub_8209F568, op 0x80). La llaman al acabar los guiones
// de los finales (codigos 1 y 2; scripts data_cmn 2800-2865). Con la mascara de Esferas de la casilla (u8 en 0x8247AA30 + 4624 * casilla + 4580)
// a 0x7F pide la pantalla 11 (Shenron y deseos, luego 12 creditos); si no, la 12. Las gana el evento 38 del #LST (sub_8215A258, al azar).
// DBZ3_TEST_ESFERAS=7 las pone antes de comprobar. Solo US (sin entrada EU).
// Evento 38 del #LST: Esfera al azar en la mascara nativa. El Heroe cuenta las suyas por saga (la mascara se rehace en la escena siguiente).
REX_HOOK_RAW(sub_8215A258) {
  const int cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  // R23 HER-13: el evento 38 nativo elige al azar entre las Esferas que FALTAN en la mascara de la casilla y divide por cuantas
  // faltan (twllei r29,0): con las 7 ya puestas es una division por cero y el juego se cerraba (Esfera del mapa tocada en una
  // casilla de Heroe que no es la de la ficha activa: nadie rehace la mascara y se llena). Con las 7 no hay nada que ganar.
  if (const uint8_t m = REX_LOAD_U8(0x8247AA30u + 4624u * uint32_t(cas) + 4580u); (m & 0x7F) == 0x7F) {
    REXLOG_WARN("dbz3 campanas: evento 38 (Esfera) en la casilla {} con las 7 ya en la mascara: sin Esfera, sin cierre", cas);
  } else {
    __imp__sub_8215A258(ctx, base);
  }
  if (dbz3::rpg::EsferasDeCasilla(cas) >= 0) {
    dbz3::rpg::EsferaDelMapa(g_esfera_ruta);
    g_esfera_ruta = -1;
    REX_STORE_U8(0x8247AA30u + 4624u * uint32_t(cas) + 4580u, uint8_t(dbz3::rpg::EsferasDeCasilla(cas)));
  }
}

// POST2: aspecto nuevo del menu de deseos (dbz3_heroe_ui_nueva) en la historia del Heroe.
static bool ShenronV2(uint8_t* base, uint32_t obj) {
  return obj && dbz3::rpg::EsferasDeCasilla(REX_LOAD_U8(dbz3::GA(0x824A58A0))) >= 0 &&
         (g_historia_shenron || rex::cvar::GetFlagByName("dbz3_heroe_ui_nueva") == "true");
}
static float F32(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
static void F32(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
// Menu de deseos (pantalla 11): objeto en 0x8243A260, +20 filas, +24.. capsula de cada fila (la ultima, sin limite de veces), +36 cursor.
// En la historia del Heroe la ultima fila es "cambiar raza y cuerpo": no da su capsula y se pinta con su textura propia.
namespace dbz3::heroe_ui { void AvisarMenuDeseo(int fila, int filas); }
// sub_82162420 = estado del menu (cursor y A). Al elegir, el estado pasa a 0x82162350 (BOOM y carta de la capsula): la fila de raza no da
// capsula, asi que hace lo mismo que 0x821622A8 al cerrar la carta y sigue a la despedida (0x82162260).
static bool g_pensarlo = false;   // POST3: el menu se cerro con B (sin deseo)
static uint32_t g_pensarlo_pad = 0;   // palabra de pulsados donde la tarea de dibujo tiene que ver tambien la A
// sub_82161FA8 = tarea que PINTA el menu (marco sub_82161AF0 + filas sub_82161B78). Lee ella misma la palabra de pulsados y se
// mata (sub_82092A20) al ver la A. Con la A inyectada solo en sub_82162420 seguia viva y, tras la despedida, pintaba con el
// objeto 0x8243A260 ya liberado (lectura de 0+224, cierre del juego: POST2 y primera prueba de POST3). Aqui ve la misma A.
REX_HOOK_RAW(sub_82161FA8) {
  if (!g_pensarlo_pad) {
    __imp__sub_82161FA8(ctx, base);
    return;
  }
  const uint32_t pad = g_pensarlo_pad;
  g_pensarlo_pad = 0;
  const uint16_t antes = REX_LOAD_U16(pad);
  REX_STORE_U16(pad, uint16_t(0x4000));
  __imp__sub_82161FA8(ctx, base);
  REX_STORE_U16(pad, antes);
}
REX_HOOK_RAW(sub_82162420) {
  const uint32_t tarea = ctx.r3.u32, obj = REX_LOAD_U32(0x8243A260u);
  const bool heroe = obj && dbz3::rpg::EsferasDeCasilla(REX_LOAD_U8(dbz3::GA(0x824A58A0))) >= 0;
  if (heroe && g_historia_shenron) {
    REX_STORE_U32(obj + 20, 2);   // dos decisiones de historia; ninguna fila entrega capsula o raza
    if (REX_LOAD_U32(obj + 36) >= 2) REX_STORE_U32(obj + 36, 0);
  }
  if (heroe) dbz3::heroe_ui::AvisarMenuDeseo(int(REX_LOAD_U32(obj + 36)), int(REX_LOAD_U32(obj + 20)));
  // POST3 "B Pensarlo" (solo con dbz3_heroe_ui_nueva): la nativa no lee B (solo Select 0x1, arriba 0x10, abajo 0x40 y A 0x4000 de
  // la palabra de pulsados, mando*184 + 14 de 0x8238B780). Se hace por el camino YA PROBADO de la fila de raza: cursor a la ultima
  // fila + una A inyectada en esa palabra; g_pensarlo hace que sub_821618D8 no conceda nada y devuelva las Esferas.
  uint32_t pad = 0;
  uint16_t pad_antes = 0;
  if (heroe && ShenronV2(base, obj)) {
    const int8_t idx = int8_t(REX_LOAD_U8(0x824BA110u + 38u));
    pad = 0x8238B780u + REX_LOAD_U32(0x8238B670u + 4u * uint32_t(int32_t(idx))) * 184u + 14u;
    pad_antes = REX_LOAD_U16(pad);
    if ((pad_antes & 0x2000) && !(pad_antes & 0x4051)) {
      g_pensarlo = true;
      g_pensarlo_pad = pad;
      REX_STORE_U32(obj + 36, REX_LOAD_U32(obj + 20) - 1u);
      REX_STORE_U16(pad, uint16_t(0x4000));
      REXLOG_INFO("dbz3 shenron: B Pensarlo: se cierra el menu sin deseo");
    } else pad = 0;
  }
  __imp__sub_82162420(ctx, base);
  if (pad) REX_STORE_U16(pad, pad_antes);
  if (heroe && g_historia_shenron && REX_LOAD_U32(tarea + 28) == 0x82162350u) {
    g_historia_callback = true;
    g_historia_aplazada = g_pensarlo;
    const bool ok = g_pensarlo ? dbz3::rpg::DeseoAplazado() :
                               dbz3::rpg::DeseoHistoria(REX_LOAD_U32(obj + 36) == 0);
    if (!ok) {
      g_historia_aplazada = true;
      REXLOG_WARN("dbz3 heroe: H02 decision no confirmada; se conserva la reunion pendiente");
    }
  }
  if (heroe && REX_LOAD_U32(tarea + 28) == 0x82162350u &&
      (g_historia_shenron || REX_LOAD_U32(obj + 36) + 1 == REX_LOAD_U32(obj + 20))) {
    const PPCContext saved = ctx;
    ctx.r3.u64 = 1;
    sub_82081A48(ctx, base);
    ctx.r3.u64 = 15;
    sub_820924C0(ctx, base);
    sub_821BA820(ctx, base);
    ctx.r3.u64 = 9, ctx.r4.u64 = 0;
    sub_821652D0(ctx, base);
    ctx.r3.u64 = REX_LOAD_U32(obj + 4);
    sub_8209ECE0(ctx, base);
    ctx = saved;
    REX_STORE_U32(tarea + 28, 0x82162260u);
    REXLOG_INFO("dbz3 shenron: fila de raza: sin carta de capsula");
    return;
  }
}
// sub_82161B78 pinta cada fila con sub_82161548(pos, color, banco = obj+244 = data_usi 2684, textura = capsula, r7 = 0x700000 + 1280 * fila).
// Ultima fila del Heroe: la textura 1983 que transformaciones.cpp anade al banco, con el texto en cada idioma.
REX_HOOK_RAW(sub_82161548) {
  const uint32_t obj = REX_LOAD_U32(0x8243A260u);
  if (g_historia_shenron && obj && ctx.r5.u32 == REX_LOAD_U32(obj + 244)) return;   // etiquetas explicitas en heroe_ui
  if (obj && ctx.r5.u32 == REX_LOAD_U32(obj + 244) && dbz3::rpg::EsferasDeCasilla(REX_LOAD_U8(dbz3::GA(0x824A58A0))) >= 0 &&
      ctx.r7.u32 == 0x700000u + 1280u * (REX_LOAD_U32(obj + 20) - 1u)) {
    constexpr uint32_t kFila = 1983;   // forms::kUnifiedBase - 1
    const uint32_t banco = ctx.r5.u32;
    if (REX_LOAD_U32(banco + 16) > kFila && REX_LOAD_U32(banco + REX_LOAD_U32(banco + 20) + 4 * kFila)) ctx.r6.u64 = kFila;
    else {
      static int avisos = 0;
      if (!avisos++) REXLOG_WARN("dbz3 shenron: banco de nombres sin la textura de la fila de raza (n {})", REX_LOAD_U32(banco + 16));
    }
  }
  if (!ShenronV2(base, obj) || ctx.r5.u32 != REX_LOAD_U32(obj + 244)) {
    __imp__sub_82161548(ctx, base);
    return;
  }
  // POST2 (maqueta 08, solo con dbz3_heroe_ui_nueva): fila alineada a la izquierda tras su esfera (x 278 de 1280) y en los
  // colores de Edit Skills (elegida cian claro, las demas turquesa apagado). sub_82161548 centra la textura en pos.x
  // (x0 = x - w/2); las unidades 2D del menu x 1,6 = px de 1280x720 (registro: fila x 395 -> 632, marco w 408 -> 653).
  const uint32_t pos = ctx.r3.u32, col = ctx.r4.u32, banco = ctx.r5.u32;
  const uint32_t ent = banco + REX_LOAD_U32(banco + REX_LOAD_U32(banco + 20) + 4u * ctx.r6.u32);
  const float w = float(int16_t(REX_LOAD_U16(ent + 16))), x = F32(base, pos);
  const bool sel = ctx.r7.u32 == 0x700000u + 1280u * REX_LOAD_U32(obj + 36);
  if (static int dicho = 0; dicho++ < 3)
    REXLOG_INFO("dbz3 shenron v2: fila x {} y {} w {} h {} color {} sel {}", x, F32(base, pos + 4), w, int16_t(REX_LOAD_U16(ent + 18)),
                F32(base, col), sel);
  F32(base, pos, 278.0f / 1.6f + w * 0.5f);
  F32(base, col, sel ? 0.78f : 0.42f), F32(base, col + 4, sel ? 1.0f : 0.78f), F32(base, col + 8, sel ? 1.0f : 0.72f);
  __imp__sub_82161548(ctx, base);
  F32(base, pos, x);   // la misma pos sirve para la fila siguiente (sub_82161B78 solo le suma el paso en y)
}
// POST2: marco nativo del menu (sub_82161AF0 -> sub_821613A8(pos, banco obj+224, textura 0)). Con el aspecto nuevo se
// estira (ancho/alto de su entrada, solo durante la llamada) al panel de la maqueta 08 (x 174..1106, y 421..706 de 1280):
// su interior queda detras de TODO el panel de ImGui y su borde, debajo del marco de ImGui.
static bool g_marco_deseos = false;
REX_HOOK_RAW(sub_82161AF0) {
  g_marco_deseos = true;
  __imp__sub_82161AF0(ctx, base);
  g_marco_deseos = false;
}
REX_HOOK_RAW(sub_821613A8) {
  const uint32_t obj = REX_LOAD_U32(0x8243A260u);
  if (!g_marco_deseos || !ShenronV2(base, obj)) {
    __imp__sub_821613A8(ctx, base);
    return;
  }
  const uint32_t pos = ctx.r3.u32, banco = ctx.r4.u32;
  const uint32_t ent = banco + REX_LOAD_U32(banco + REX_LOAD_U32(banco + 20) + 4u * ctx.r5.u32);
  const uint16_t w = REX_LOAD_U16(ent + 16), h = REX_LOAD_U16(ent + 18);
  const float x = F32(base, pos), y = F32(base, pos + 4);
  if (static bool dicho = false; !dicho) {
    dicho = true;
    REXLOG_INFO("dbz3 shenron v2: marco x {} y {} w {} h {}", x, y, int16_t(w), int16_t(h));
  }
  // nativo: centro (395, 360) w 408 h 160 (x 1,6 = px de 1280) -> panel 174..1106 x 421..706 de 1280
  REX_STORE_U16(ent + 16, uint16_t(596)), REX_STORE_U16(ent + 18, uint16_t(182));   // 21/11 (HER-01): 608x196 dejaba las esquinas nativas ~20 px fuera del marco de ImGui (doble marco)   // ronda 2: su borde (3,3 % / 8,4 % hacia dentro) cae bajo el de ImGui
  F32(base, pos, 400.0f), F32(base, pos + 4, 352.2f);
  // panel opaco: el interior es translucido; se pinta 4 veces (el alfa se acumula) y asi el texto nativo de las filas, que se
  // pinta DESPUES, queda encima sin que ImGui tenga que taparlo
  const PPCContext guardado = ctx;
  for (int i = 0; i < 4; ++i) {
    ctx = guardado;
    __imp__sub_821613A8(ctx, base);
  }
  REX_STORE_U16(ent + 16, w), REX_STORE_U16(ent + 18, h);
  F32(base, pos, x), F32(base, pos + 4, y);
}
REX_HOOK_RAW(sub_821618D8) {
  const uint32_t obj = REX_LOAD_U32(0x8243A260u);
  if (obj && dbz3::rpg::EsferasDeCasilla(REX_LOAD_U8(dbz3::GA(0x824A58A0))) >= 0) {
    const uint32_t fila = REX_LOAD_U32(obj + 36), filas = REX_LOAD_U32(obj + 20), cap = REX_LOAD_U32(obj + 24 + 4 * fila);
    REXLOG_INFO("dbz3 shenron: deseo elegido: fila {} de {} (capsula {})", fila + 1, filas, cap);
    const bool historia = g_historia_shenron || g_historia_callback;
    g_historia_callback = false;
    const bool raza = fila + 1 == filas;
    const bool confirmado = historia ? true : g_pensarlo ? dbz3::rpg::DeseoAplazado() :
                             raza ? dbz3::rpg::DeseoRaza() : dbz3::rpg::DeseoCapsula();
    g_pensarlo = false;
    if (historia || raza || !confirmado) {
      // Conserva la limpieza nativa; ni raza/B ni un callback repetido entregan una capsula.
      const uint32_t cuenta = 0x8247A9B0u + 0x30000u - 20711u + cap, antes = REX_LOAD_U8(cuenta);
      __imp__sub_821618D8(ctx, base);
      REX_STORE_U8(cuenta, antes);
      return;
    }
  }
  __imp__sub_821618D8(ctx, base);
}

// La despedida conserva la limpieza de Shenron (5D20 -> 57E0). Solo despues
// se carga de nuevo DU (peticion 8); la 9 es para salir de un mapa ya existente.
REX_HOOK_RAW(sub_821C7EC0) {
  // La intro reinicia el indicador del mapa. Tras Shenron el mapa no existe;
  // se creara al procesar la intro, por lo que no hay indicador que reiniciar.
  if (g_historia_shenron && !REX_LOAD_U32(0x8247A8A0u)) return;
  __imp__sub_821C7EC0(ctx, base);
}

REX_HOOK_RAW(sub_821D5D20) {
  if (g_historia_shenron) {
    // La variante nativa crea CC308 (Ending00.sfd) antes de 57E0.
    // En la consulta intermedia esperamos sus mismos cerrojos, pero seguimos
    // a la limpieza del menu sin crear una pelicula que bloquearia el mapa.
    const PPCContext saved = ctx;
    const uint32_t task = ctx.r3.u32;
    ctx.r3.u64 = 4;
    sub_821D3888(ctx, base);
    if (ctx.r3.u32) { ctx = saved; return; }
    sub_821D7FE8(ctx, base);
    if (ctx.r3.u32 || !REX_LOAD_U32(0x8247A994u)) { ctx = saved; return; }
    g_historia_salida = true;
    REX_STORE_U32(task + 28, 0x821D57E0u);
    REX_STORE_U32(0x8247A990u, 8);
    sub_821D27F8(ctx, base);   // listo, como CC308, sin crear la tarea de pelicula
    ctx = saved;
    REXLOG_INFO("dbz3 heroe: H02 despedida intermedia -> historia {}, sin creditos", g_historia_aplazada ? "aplazada" : "con decision");
    return;
  }
  __imp__sub_821D5D20(ctx, base);
}

REX_HOOK_RAW(sub_821D57E0) {
  const uint32_t task = ctx.r3.u32;
  const bool historia = g_historia_shenron && g_historia_salida;
  __imp__sub_821D57E0(ctx, base);
  if (historia && REX_LOAD_U32(task + 28) == 0x821D5368u) {
    // La carga de DU reinicia el interprete en 821609B8 -> 8215A7A8.
    // No lanzar E9C8 aqui: su tarea quedaria viva con el recurso recien borrado.
    // C7108 lee la escena guardada y la lanza cuando el mapa ya esta cargado.
    const uint32_t slot = REX_LOAD_U8(0x824A58A0u);
    const uint32_t escena = g_historia_aplazada ? ((5000u + slot) << 16) :
                                                ((dbz3::campaigns::IntroDeSaga(6) << 16) | slot);
    const uint32_t blk = 0x8247A9B0u + 128u + 4624u * slot;
    REX_STORE_U32(blk + 4564, escena);
    REX_STORE_U32(blk + 4572, 0);
    // R23 HER-08: el deseo se pide en el mapa de la Reunion (Namek) y la etapa final es en la Tierra. Sin cierre de saga
    // no pasa la op 47 (planeta, bloque +4576) y el mapa seguia en Namek: el final 100 -> 1.39 no se lanzaba nunca.
    if (const int pl = dbz3::heroe::PlanetaEtapaPendiente(); !g_historia_aplazada && pl >= 0 && pl < 2) {
      REX_STORE_U8(blk + 4576, uint8_t(pl));
      REXLOG_INFO("dbz3 heroe: H02 tras el deseo, planeta del mapa {} (etapa pendiente)", pl);
    }
    REX_STORE_U32(0x8247A990u, 8);
    REX_STORE_U32(task + 28, 0x821D65B8u);
    // 57E0 borra el aviso de salida (A994). Su sucesor nativo era creditos;
    // 65B8 -> 6110, en cambio, espera ese aviso antes de cargar C7690.
    // Shenron ya esta limpio: completar su salida, no la carga del nuevo mapa.
    const PPCContext saved = ctx;
    sub_821D27A8(ctx, base);
    ctx = saved;
    g_historia_shenron = false;
    // Una decision confirmada que no paso por sub_821618D8 no debe saltarse
    // la entrega normal de la siguiente pantalla de capsulas/raza.
    g_historia_callback = false;
    REXLOG_INFO("dbz3 heroe: H02 Shenron limpiado; escena guardada {}.{} para despues de cargar el mapa", escena >> 16, escena & 0xFFFF);
  }
}

// Diagnostico del mapa: distinguir la carga inicial del regreso de Shenron.
REX_HOOK_RAW(sub_821C7108) {
  static const bool diag = std::getenv("DBZ3_TEST_H02_TRANSICION") != nullptr;
  static uint32_t espera = 0, ultimo = ~0u;
  const uint32_t task = ctx.r3.u32;
  if (diag) {
    const uint32_t carga = REX_LOAD_U32(0x8238CD48u);
    if (carga != ultimo || ++espera % 300 == 1) {
      ultimo = carga;
      const uint32_t p = REX_LOAD_U32(0x82479B14u);
      REXLOG_INFO("dbz3 heroe: H02 mapa C7108 carga {} tarea {:08X} mapa {:08X} progreso {:08X}",
                  carga, task, REX_LOAD_U32(0x8247A8A0u), p);
    }
  }
  __imp__sub_821C7108(ctx, base);
  if (diag && REX_LOAD_U32(task + 28) != 0x821C7108u)
    REXLOG_INFO("dbz3 heroe: H02 mapa C7108 termino -> {:08X}", REX_LOAD_U32(task + 28));
  if (diag && g_historia_salida && REX_LOAD_U32(task + 28) != 0x821C7108u && REX_LOAD_U32(0x8247A8A0u)) {
    g_historia_salida = false;
    REXLOG_INFO("dbz3 heroe: H02 regreso de Shenron: mapa cargado");
  }
}

// CC308 tambien interviene al despedir a Shenron: su traza "creditos" no
// acredita el final. 0BE8 es el creador al que llega 5368 al entrar en creditos.
REX_HOOK_RAW(sub_821D0BE8) {
  // La rutina tambien se prepara al arrancar el juego. Solo acreditar el final
  // despues del ultimo cierre de la ruta (etapa 5 ganada -> saga 6).
  if (std::getenv("DBZ3_DU_TRACE") && dbz3::heroe::Rutas() && dbz3::heroe::Saga() >= 6 && !g_historia_shenron)
    REXLOG_INFO("dbz3 heroe: H02 entrada creditos finales (821D0BE8)");
  __imp__sub_821D0BE8(ctx, base);
}

REX_HOOK_RAW(sub_8215C1B0) {
  static const int esf = std::getenv("DBZ3_TEST_ESFERAS") ? std::atoi(std::getenv("DBZ3_TEST_ESFERAS")) : 0;
  const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
  const uint32_t cas2 = REX_LOAD_U8(0x824A5960);
  const uint32_t dir = 0x8247AA30u + 4624u * cas + 4580u;
  const uint32_t antes = REX_LOAD_U8(dir);
  if (esf >= 7) REX_STORE_U8(dir, 0x7F);
  const bool shenron = REX_LOAD_U8(dir) == 0x7F;   // clrlwi 25 (7 bits), igual que la nativa
  REXLOG_INFO("dbz3 shenron: sub_8215C1B0 (comprueba las Esferas) casilla {} / {}, mascara antes {:02X}{} (desde {:08X})", cas, cas2,
              antes, esf >= 7 ? " -> 7F (prueba)" : "", uint32_t(ctx.lr));
  if (shenron && dbz3::rpg::EsferasDeCasilla(int(cas)) >= 0) dbz3::rpg::ShenronDeseo();
  __imp__sub_8215C1B0(ctx, base);
}

DBZ3_HOOK_EU(sub_82096FF0) {
  const uint32_t bank = REX_LOAD_U32(dbz3::GA(0x8238CE10));
  const int32_t id = ctx.r5.s32;
  // C4: rotulo de nombre de la pantalla de subida de nivel de DU (data_usi 1974, textura 27+casilla; 36 = 'Saibamen'): con la
  // historia del Heroe en las casillas 35-37 se pide la textura propia (casilla + 30) que anade mods/heroe_nombres
  // (cac/nombre_levelup.py): el banco pasa de 65 a 68 texturas.
  // Sin mods/heroe_nombres cargado (banco de 65: ficha montada antes de existir el rotulo, o nombre recien cambiado) el id 62 es el
  // nativo 'Super Yi-Shin-Long' de la casilla 35: mejor sin rotulo (un id fuera de la tabla no dibuja nada) que el nombre de otro.
  if (bank && id >= 62 && id <= 64) {
    const uint32_t cas = REX_LOAD_U8(dbz3::GA(0x824A58A0));
    const uint32_t n = REX_LOAD_U32(bank + 16);   // 65 = data_usi 1974 nativo; otros bancos tienen sus propios ids 62-64
    if (int32_t(cas) == id - 27 && dbz3::campaigns::g_slot_fighter.count(cas) && (n >= 68 || n == 65)) ctx.r5.s64 = n >= 68 ? id + 3 : 0x7FFF;
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
    const int32_t idx = ctx.r5.s32 - int32_t(REX_LOAD_U32(bank + 24));   // el id ya cambiado por el rotulo del Heroe
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
      if (warned++ < 8) {
        // pila de llamadas (4 niveles) y datos del banco: de que modelo/efecto es la textura que falta
        uint32_t fr = ctx.r1.u32, up[5] = {0, 0, 0, 0, 0};
        for (int k = 0; k < 5 && fr >= 0x70000000u && fr < 0x80000000u; ++k) {
          fr = REX_LOAD_U32(fr);
          if (fr < 0x70000000u || fr >= 0x80000000u) break;
          up[k] = REX_LOAD_U32(fr - 8);
        }
        REXLOG_WARN("dbz3: efecto {} que no esta en el banco {:08X} (entrada {:08X}, recurso {:08X}, desde {:08X}): sin efecto"
                    " [n {} primera {} pila {:08X} {:08X} {:08X} {:08X} {:08X}]", id,
                    bank, entry, res, uint32_t(ctx.lr), REX_LOAD_U32(bank + 16), REX_LOAD_U32(bank + 24), up[0], up[1], up[2], up[3], up[4]);
      }
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
// Historia del Heroe con dibujo grande (ajuste dbz3_heroe_fondos_historia, encendido de serie). La narracion nativa pinta su imagen
// y su texto con sub_8213A768 (r3 = objeto: +16/+20 tamano, +48/+52 origen, +64/+68 posicion, +80/+84 escala; 2D de 600x450 con +100 en x
// para el 16:9 = 800x450). Con una imagen ancha (la compone campana.py: banda 1024x362 + franja oscura abajo, 1024x512) se estira a todo
// el ancho visible entre los pilares (el 2D se recorta a x 0..600: rectangulo 0..600 x 50..450) y el texto ancho (<= 2 lineas) se pone en la franja, nunca encima de la banda.
// Solo US (la pareja EU de sub_8213A768 esta sin medir: en EU el ajuste no hace nada).
namespace {
std::atomic<long long> g_narr_ancha_ms{-100000};
long long MsNarr() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
float FN(uint8_t* base, uint32_t a) { const uint32_t u = REX_LOAD_U32(a); float f; std::memcpy(&f, &u, 4); return f; }
void SetFN(uint8_t* base, uint32_t a, float f) { uint32_t u; std::memcpy(&u, &f, 4); REX_STORE_U32(a, u); }
}  // namespace
namespace dbz3::campaigns {
bool NarracionAncha() {
  return !dbz3::g_guest_eu && REXCVAR_GET(dbz3_heroe_fondos_historia) &&
         g_du_sesion.load() >= 0 && MsNarr() - g_narr_ancha_ms.load() < 250;
}
bool FondosHistoriaHeroe(uint8_t* base) {
  if (dbz3::g_guest_eu || !REXCVAR_GET(dbz3_heroe_fondos_historia)) return false;
  // La pantalla 8 tambien pertenece al DU normal: solo la sesion y casilla del Heroe pueden ensancharla.
  const int sesion = g_du_sesion.load();
  const uint32_t slot = REX_LOAD_U8(0x824A58A0u);
  return sesion >= 0 && uint32_t(sesion) == slot && g_slot_fighter.count(slot) &&
         REX_LOAD_U8(0x824BA110u) == 8;
}
}
REX_HOOK_RAW(sub_8213A768) {
  if (!dbz3::campaigns::FondosHistoriaHeroe(base)) {
    g_narr_ancha_ms = -100000;
    return __imp__sub_8213A768(ctx, base);
  }
  const uint32_t d = ctx.r3.u32;
  const float w = FN(base, d + 16), h = FN(base, d + 20), ox = FN(base, d + 48), oy = FN(base, d + 52);
  const bool banda = w == 683.0f && h == 341.0f && ox == 0.0f && oy == 0.0f;
  const bool imagen = banda || (w == 440.0f && h == 280.0f && ox == 0.0f && oy == 0.0f && dbz3::campaigns::NarracionAncha());
  const bool texto = (ox > 0.0f || oy > 0.0f) && w >= 500.0f;
  if (banda || texto) {
    if (!dbz3::campaigns::NarracionAncha())
      REXLOG_INFO("dbz3 fondos: narracion ancha del Heroe (casilla {})", dbz3::campaigns::g_du_sesion.load());
    g_narr_ancha_ms = MsNarr();
  }
  else if (ox > 0.0f && w < 500.0f) g_narr_ancha_ms = -100000;   // texto nativo: narracion sin dibujo grande
  if (!imagen && !texto) return __imp__sub_8213A768(ctx, base);
  const uint32_t campos[] = {64, 68, 80, 84, 48};
  float guardado[5];
  for (int i = 0; i < 5; ++i) guardado[i] = FN(base, d + campos[i]);
  if (imagen) {
    // medido con una rejilla: el juego ensancha este rectangulo x1,275 respecto al centro (x 300); para cubrir -100..700 se pide 300 +- 400/1,275
    constexpr float kAn = 1.0f, mitad = 395.0f / kAn;   // un poco menos que 400: justo en el borde el juego recorta mal
    SetFN(base, d + 64, 300.0f - mitad), SetFN(base, d + 68, 0.0f);
    SetFN(base, d + 80, 2.0f * mitad / w), SetFN(base, d + 84, 450.0f / h);
  } else {
    constexpr float k = 0.48f;   // letra de 38 px -> ~44 px a 1080p (2,4 px por unidad)
    SetFN(base, d + 80, k), SetFN(base, d + 84, k);
    SetFN(base, d + 64, 20.8f + ox * k), SetFN(base, d + 68, 378.5f);   // izquierda en x 290 px y centro de la franja (y 816-1000 px)
  }
  __imp__sub_8213A768(ctx, base);
  for (int i = 0; i < 5; ++i) SetFN(base, d + campos[i], guardado[i]);
}
// Barras laterales del 2D 4:3 (relleno naranja y pilares, x 0..100 y 700..800): con la banda ancha de la narracion no se pintan.
REX_HOOK_RAW(sub_82095680) {
  if (dbz3::campaigns::FondosHistoriaHeroe(base) && dbz3::campaigns::NarracionAncha()) return;
  __imp__sub_82095680(ctx, base);
}
