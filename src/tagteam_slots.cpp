// dbz3 - Tag Team: miembros elegidos en la rueda del juego por cada lado (modo TAG TEAM,
// ingame/tagteam_panel.cpp). Son los personajes del juego tal cual: al empezar el combate el
// lado entra con su primer miembro (su ID en la seleccion) y el relevo (tagteam.cpp) cambia el
// luchador por el siguiente miembro de verdad.
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "modos.h"
#include "roster_ext.h"
#include "form_selection_state.h"
#include <cstring>
#include "tagteam.h"
#include "ingame/tagteam_panel.h"
#include "net/netplay.h"

namespace dbz3::campaigns {
int PlayerStartForm();
int RivalSuit();
}

namespace dbz3::tag {
namespace {
std::mutex g_mu;
std::array<std::vector<int>, 2> g_dyn;   // miembros por lado (vacio = sin equipo)
std::vector<int> g_mine;                 // ultimo equipo de 1P (el que se lleva al online)
std::array<std::map<int, std::array<uint16_t, 8>>, 2> g_caps;   // por lado: ID -> capsulas Custom
}  // namespace

void SetMemberCapsules(int side, int id, const uint16_t* caps) {
  if (side < 0 || side > 1) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!caps) {
    g_caps[size_t(side)].erase(id);
    return;
  }
  auto& c = g_caps[size_t(side)][id];
  std::copy(caps, caps + 8, c.begin());
}

// TAG_F3_RECIPE_V1
// Copia coherente del mismo proveedor de PutCapsules; ninguna escritura guest.
bool ReadMemberCapsules(uint8_t* base, int side, int id, std::array<uint16_t, 8>& c) {
  if (id < 0 || id >= dbz3::forms::kIds || side < 0 || side > 1) return false;
  bool custom = false;
  if (dbz3::net::Session::Get().link() == dbz3::net::Link::kIdle) {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_caps[size_t(side)].find(id);
    if (it != g_caps[size_t(side)].end()) { c = it->second; custom = true; }
  } else {   // online: la lista Custom que cada lado acordo al conectar (igual en los dos PC)
    custom = dbz3::net::Session::Get().AgreedCaps(side, id, c);
    REXLOG_INFO("dbz3 online: capsulas (relevo) lado {} id {}: {} {} {} {} {}", side + 1, id, custom ? "Custom" : "Normal", c[0], c[1], c[2], c[3]);
  }
  if (!custom) {
    const uint32_t address = dbz3::GA(0x8234ABB8u) + 96u * uint32_t(id) + 80u;
    if (!dbz3::roster::FormSelectionRange(address, 16, false)) return false;
    for (uint32_t i = 0; i < 8; ++i) c[i] = REX_LOAD_U16(address + 2*i);
  }
  if (c[0] > 7) return false;
  const uint32_t count = dbz3::roster::CapsuleCount();
  if (!count || count > 2048) return false;
  for (uint32_t i = 1; i <= c[0]; ++i) if (c[i] >= count) return false;
  return true;
}

void PutCapsules(uint8_t* base, uint32_t rec, int side, int id) {
  std::array<uint16_t, 8> c{};
  bool custom = false;
  if (dbz3::net::Session::Get().link() == dbz3::net::Link::kIdle && side >= 0 && side < 2) {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_caps[size_t(side)].find(id);
    if (it != g_caps[size_t(side)].end()) {
      c = it->second;
      custom = true;
    }
  } else if (side >= 0 && side < 2) {   // online: la lista Custom acordada de ese lado y miembro
    custom = dbz3::net::Session::Get().AgreedCaps(side, id, c);
    REXLOG_INFO("dbz3 online: capsulas (inicio) lado {} id {}: {} lista [{} {} {} {}]", side + 1, id, custom ? "Custom" : "Normal", c[0], c[1], c[2], c[3]);
  }
  if (!custom) {   // las suyas por defecto (char96 +80: n y 7 IDs)
    const uint32_t c96 = dbz3::GA(0x8234ABB8) + 96 * uint32_t(id);
    for (uint32_t k = 0; k < 8; ++k) c[k] = REX_LOAD_U16(c96 + 80 + 2 * k);
  }
  for (uint32_t k = 0; k < 8; ++k) REX_STORE_U16(rec + 52 + 2 * k, c[k]);
}

void SetDynamicTeam(int side, const std::vector<int>& members, bool remember) {
  if (side < 0 || side > 1) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_dyn[size_t(side)] = members;
  if (remember && side == 0 && members.size() >= 2) g_mine = members;
}

std::vector<int> MyLastTeam() {
  std::lock_guard<std::mutex> lock(g_mu);
  // pruebas del online (DBZ3_NET_TEAM="0,11"): el equipo que este PC lleva a la partida online si no
  // eligio uno; solo lo lee el arranque online, no la demo ni los combates normales
  if (g_mine.empty()) {
    if (const char* env = std::getenv("DBZ3_NET_TEAM")) {
      std::vector<int> team;
      for (const char* p = env; *p;) {
        team.push_back(std::atoi(p));
        while (*p && *p != ',') ++p;
        if (*p) ++p;
      }
      return team;
    }
  }
  return g_mine;
}

std::array<uint16_t, 8> OwnCustomCapsules(int id) {
  std::array<uint16_t, 8> out{};
  if (id < 0) return out;
  uint16_t ids[7];
  std::fill(ids, ids + 7, uint16_t(0xFFFF));
  if (id < 38) {   // lista Custom guardada en la partida (casilla del select = ID), como hub.cpp
    if (!dbz3::roster::SaveCustomList(uint32_t(id), ids)) return out;
  } else {   // celda nueva: mods/capsulas_custom.txt (sin lista propia devuelve la Normal)
    dbz3::roster::GetCustomList(uint32_t(id), ids);
  }
  if (const char* te = std::getenv("DBZ3_TEST_CAPS_ONLINE")) {   // PRUEBAS (solo pruebas.py): "id:c1,c2;id:c1,c2" en vez de la partida
    for (const char* q = te; *q;) {
      const int tid = std::atoi(q);
      const char* col = std::strchr(q, ':');
      const char* semi = std::strchr(q, ';');
      if (col && (!semi || col < semi) && tid == id) {
        std::fill(ids, ids + 7, uint16_t(0xFFFF));
        int k = 0;
        for (const char* d = col + 1; *d && *d != ';' && k < 7;) {
          ids[k++] = uint16_t(std::atoi(d));
          while (*d && *d != ',' && *d != ';') ++d;
          if (*d == ',') ++d;
        }
        break;
      }
      if (!semi) break;
      q = semi + 1;
    }
  }
  const uint32_t count = dbz3::roster::CapsuleCount();
  uint16_t n = 0;
  for (uint32_t i = 0; i < 7; ++i) {
    if (ids[i] == 0xFFFF || ids[i] == 0) continue;
    if (ids[i] >= count) return {};   // valor invalido: la Normal
    out[1 + n++] = ids[i];
  }
  out[0] = n;
  return out;
}

void SetMyOnlineTeam(const std::vector<int>& members) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_mine = members.size() >= 2 ? members : std::vector<int>{};
}

void ClearDynamicTeams() {
  dbz3::forms::ClearMembers();
  std::lock_guard<std::mutex> lock(g_mu);
  for (auto& d : g_dyn) d.clear();
  for (auto& c : g_caps) c.clear();
}

std::vector<int> DynamicTeam(int side) {
  std::lock_guard<std::mutex> lock(g_mu);
  return side == 0 || side == 1 ? g_dyn[size_t(side)] : std::vector<int>{};
}

// Inicio del combate (sub_820FE668, r3 = seleccion; ID del jugador k en +32 + 80k): cada lado
// con equipo empieza con su primer miembro.
void BeforeBattleSetup(uint8_t* base, uint32_t sel) {
  if (!sel || uint64_t(sel) + 176 > 0x100000000ull ||
      !dbz3::roster::FormSelectionRange(sel, 176, true)) return;
  // historias de mod: tu personaje empieza transformado (forma minima y maxima de la seleccion,
  // rec+20/+22: el juego solo carga esa forma y empieza en ella)
  if (const int f = dbz3::campaigns::PlayerStartForm(); f > 0) {
    REX_STORE_U16(sel + 16 + 20, uint16_t(f));
    REX_STORE_U16(sel + 16 + 22, uint16_t(f));
    REXLOG_INFO("dbz3 campanas: tu personaje empieza en la forma {}", f);
  }
  if (const int suit = dbz3::campaigns::RivalSuit(); suit >= 0) {   // rec+18 = traje (0 el normal)
    REX_STORE_U16(sel + 16 + 80 + 18, uint16_t(suit));
    REXLOG_INFO("dbz3 campanas: el rival lleva el traje {}", suit);
  }
  // equipos solo en el modo Tag Team o en una partida online: si quedo alguno de antes (online
  // cerrado a medias, salida por otro camino) no se cuela en Practica ni en un combate normal
  // (la Mision 100 de rpg.cpp pone los suyos para sus misiones de equipo: EquiposMision)
  if (!dbz3::ingame::TagTeamMode() && dbz3::net::Session::Get().link() == dbz3::net::Link::kIdle &&
      !dbz3::rpg::EquiposMision()) {
    ClearDynamicTeams();
    // combate normal (Duelo, HUB, retos, demo): el ultimo equipo de TAG TEAM deja de ser "el mio" para el online
    // (antes no se olvidaba nunca: bugs.md P5). De TAG TEAM directo al online se sigue llevando.
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_mine.empty()) REXLOG_INFO("dbz3 tag: combate sin TAG TEAM: se olvida el ultimo equipo ({} miembros)", g_mine.size());
    g_mine.clear();
  }
  for (int k = 0; k < 2; ++k) {
    std::vector<int> team = DynamicTeam(k);
    // pruebas: DBZ3_TAG_TEST="0,11" = equipo del jugador 1 en cualquier combate (tambien en el modo Tag
    // Team, por encima de lo elegido en la rueda: la rueda recuerda la ultima casilla y las pruebas
    // automaticas acababan con otros equipos); DBZ3_TAG_TEST2 el del jugador 2
    if (const char* env = std::getenv(k ? "DBZ3_TAG_TEST2" : "DBZ3_TAG_TEST"); env && *env) {
      team.clear();
      for (const char* p = env; *p;) {
        team.push_back(std::atoi(p));
        while (*p && *p != ',') ++p;
        if (*p) ++p;
      }
      SetDynamicTeam(k, team);
    }
    if (team.size() < 2) {
      // Online 1 contra 1: la lista de la rueda sale de la partida guardada de CADA PC (kUiSnap solo copia 0x100 B; las listas estan
      // en +0x1340). Se escribe la lista Custom del dueño de cada lado (la mandaron al conectar, kTeam) o la Normal: igual en los dos PC.
      if (dbz3::net::Session::Get().link() != dbz3::net::Link::kIdle) {
        const uint32_t rec1 = sel + 16 + 80 * uint32_t(k);
        const uint32_t id1 = REX_LOAD_U16(rec1 + 16);
        if (const char* tc = std::getenv("DBZ3_TEST_REC_CAPS"); tc && k == 0) {   // PRUEBA: simula una lista Custom distinta en cada PC
          int c[7] = {0, 0, 0, 0, 0, 0, 0}, n = 0;
          for (const char* q = tc; *q && n < 7; ++n) { c[n] = std::atoi(q); while (*q && *q != ',') ++q; if (*q) ++q; }
          REX_STORE_U16(rec1 + 52, uint16_t(n));
          for (int i = 0; i < n; ++i) REX_STORE_U16(rec1 + 54 + 2 * i, uint16_t(c[i]));
        }
        if (id1 < 105 && !std::getenv("DBZ3_TEST_SIN_NORMAL")) {
          PutCapsules(base, rec1, int(k), int(id1));
          REXLOG_INFO("dbz3 online: 1 contra 1, lado {} id {}: capsulas acordadas [{} {} {} {}]", k + 1, id1, REX_LOAD_U16(rec1 + 52),
                      REX_LOAD_U16(rec1 + 54), REX_LOAD_U16(rec1 + 56), REX_LOAD_U16(rec1 + 58));
        } else {
          REXLOG_INFO("dbz3 online: 1 contra 1, lado {} id {}: capsulas SIN forzar [{} {} {} {}]", k + 1, id1, REX_LOAD_U16(rec1 + 52),
                      REX_LOAD_U16(rec1 + 54), REX_LOAD_U16(rec1 + 56), REX_LOAD_U16(rec1 + 58));
        }
      }
      continue;
    }
    // su ID y sus capsulas, las elegidas para el (Normal / Custom) en el menu del juego (la seleccion
    // trae las del ultimo confirmado en la rueda: sin esto el primero peleaba con las de otro)
    const uint32_t rec = sel + 16 + 80 * uint32_t(k);
    REX_STORE_U16(rec + 16, uint16_t(team[0]));
    PutCapsules(base, rec, k, team[0]);
    // la casilla nueva recordada es la del ultimo miembro elegido (roster_ext la devolvia a la
    // seleccion y el lado empezaba con el ultimo en vez de con el primero, 2026-10-12)
    dbz3::roster::SetPickedCell(int(k), -1, -1);
    REXLOG_INFO("dbz3 tag: lado {} empieza con el id {} ({} miembros)", k + 1, team[0], team.size());
  }
  // Resolver común: forma lógica de char372; modelo independiente en char96.
  // Online y forma impuesta por campaña conservan el flujo original.
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t rec = sel + 16 + 80*k;
    if (!dbz3::roster::FormSelectionRange(rec, 80, true)) continue;
    const int id = int(REX_LOAD_U16(rec + 16));
    const int suit = dbz3::ingame::TagTeamMode()
        ? dbz3::forms::MemberSuit(int(k), id, int(REX_LOAD_U16(rec + 18)))
        : int(REX_LOAD_U16(rec + 18));
    auto snapshot = dbz3::roster::ReadForms(id, suit);
    const uint32_t n = REX_LOAD_U16(rec + 52);
    if (n > 7) continue;
    snapshot.capsules.clear();
    for (uint32_t c = 0; c < n; ++c) snapshot.capsules.push_back(REX_LOAD_U16(rec + 54 + 2*c));
    const bool online = dbz3::net::Session::Get().link() != dbz3::net::Link::kIdle;
    int want = dbz3::forms::MemberRequest(int(k), id);
    if (const char* env = std::getenv("DBZ3_START_FORM")) {
      const char* comma = std::strchr(env, ',');
      want = std::atoi(k == 0 || !comma ? env : comma + 1);
    }
    const auto result = dbz3::forms::Resolve(snapshot, want,
        dbz3::ingame::FormSelectorEnabled(),
        dbz3::forms::ImposedStart(int(REX_LOAD_U16(rec + 20)),
            k == 0 ? dbz3::campaigns::PlayerStartForm() : -1), online);
    dbz3::forms::Notice(int(k), snapshot, result);
    if (!result.valid || result.reason == dbz3::forms::Reason::kDisabled) continue;
    REX_STORE_U16(rec + 18, uint16_t(suit));
    REX_STORE_U16(rec + 20, uint16_t(result.applied));
    const uint16_t maximum = uint16_t(snapshot.count - 1);
    REX_STORE_U16(rec + 22, uint16_t(std::clamp<int>(REX_LOAD_U16(rec + 22), result.applied, maximum)));
    if (result.reason != dbz3::forms::Reason::kOk)
      REXLOG_WARN("dbz3 forma: lado {} id {} pedida {} aplicada {} motivo {} capsula {}",
          k + 1, id, want, result.applied, int(result.reason), result.capsule);
  }
}

}  // namespace dbz3::tag
