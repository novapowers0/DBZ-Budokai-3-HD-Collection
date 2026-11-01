// dbz3 - Tag Team en combate (ver tagteam.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dbz3::tag {

bool InBattle();

// Lo que dibuja el marcador de los compañeros (tagteam_panel.cpp), copiado en cada tick.
struct HudSide {
  int n = 0;              // miembros (0 = este jugador no lleva un equipo)
  int active = 0;         // miembro en combate
  int ids[5] = {};        // ID de personaje de cada miembro
  float hp[5] = {};       // vida 0..1
  bool down[5] = {};      // caido
  bool switching = false;
  bool refused = false;   // SELECT rechazado hace poco (el HUD parpadea)
  bool shown = true;      // marcador del juego a la vista (no en la presentacion ni el READY)
};
HudSide Hud(int player);

// Tag Team al vuelo (tagteam_slots.cpp): miembros elegidos en la rueda por lado (0 = 1P).
// Con 2 o mas, el combate siguiente usa la ranura de ese lado con esos miembros.
// remember: el equipo del lado 1 pasa a ser "mi ultimo equipo" (el que se lleva al online); el online no
void SetDynamicTeam(int side, const std::vector<int>& members, bool remember = true);
void ClearDynamicTeams();
std::vector<int> DynamicTeam(int side);
// Capsulas "Custom" de un miembro, elegidas en el menu del juego al confirmarlo (n + 7 IDs, como
// char96 +80); nullptr = las suyas por defecto ("Normal").
void SetMemberCapsules(int side, int id, const uint16_t* caps);
// Capsulas del miembro `id` en el registro de seleccion de 80 B (+52 n, +54 7 IDs): las elegidas o las
// suyas por defecto. Online siempre las de por defecto (los dos PCs tienen que ver lo mismo).
void PutCapsules(uint8_t* base, uint32_t rec, int side, int id);
// Ultimo equipo elegido por 1P en TAG TEAM: online, cada PC manda el suyo (netplay kTeam).
std::vector<int> MyLastTeam();
// Pantalla ONLINE: equipo que se lleva a la sala (2+ ids = Tag Team; vacio = 1 contra 1). No toca los equipos de combate.
void SetMyOnlineTeam(const std::vector<int>& members);
// Online: lista Custom propia del luchador (id de serie o de plaza): {n, hasta 7 IDs de capsula}; n = 0 = la Normal.
std::array<uint16_t, 8> OwnCustomCapsules(int id);
// Lado que lleva el ordenador (relevo propio cuando le queda poca vida).
void SetComSide(int side, bool com);
// Inicio del combate (roster_ext.cpp, hook de sub_820FE668): rellena las ranuras y cambia el ID.
void BeforeBattleSetup(uint8_t* base, uint32_t sel);
// Online: relevos ya cargados en este PC (bits 0x0400/0x0800 que viajan en el mando) y los de los
// mandos aplicados; el relevo acaba cuando los dos PCs lo tienen (mismo tick en los dos).
uint16_t NetReadyBits();
void NetApplied(uint32_t user, uint16_t buttons);
// TAG TEAM por la via nativa (solo US, docs/12_conectores/13_tagteam_nativo.md): el menu principal sale
// como con su A y abre la rueda directamente (pantalla 14, argumento 1) en la fila del Duelo `duel_row`
// (0 1P VS COM, 1 1P VS 2P, 3 COM VS COM); "atras" en la rueda vuelve a la pantalla TAG TEAM.
// false = EU o DBZ3_TAG_PILOT (sigue el piloto de menus).
bool NativeEnter(int duel_row);
void NativeEnd();
// Pantalla abierta sobre la rueda del lado (0/1) tras confirmar: estado +10 de la rueda (2-4 menu de capsulas,
// 5-7 las de Edit Skills/Ability); 0 = ninguna o la rueda no esta viva (solo US).
int WheelMenu(int side);
// La bandeja "Custom Tray" de Edit Skills de ese lado esta abierta (no basta con tener resaltada la opcion; solo US).
bool WheelTray(int side);

}  // namespace dbz3::tag
