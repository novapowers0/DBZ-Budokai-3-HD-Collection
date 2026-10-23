// dbz3 - Roster extension: new characters / costumes declared by mods.
//
// A mod may ship mods/<mod>/roster.toml. At launch (guest image loaded, guest
// thread not yet running) the declared characters are written into the guest's
// per-character tables (US or EU/PAL image; verified by signature, guest_region.h). See
// roster_ext.cpp for the table map and the file format.
#pragma once

#include <string>

#include <cstdint>
#include <cstddef>
#include "form_selection.h"

namespace rex::memory {
class Memory;
}

namespace dbz3::roster {

// Before the guest starts: the generated mod mods/_roster (roster_build.py) needs
// a rexruntime that serves APPENDED AFS entries and grows any entry (it publishes
// the cvar dbz3_afs_append). Without it the mod is disabled (.disabled) so the
// game still boots; it is re-enabled once a capable runtime is installed.
void GuardGeneratedMod();

// Reads every enabled mod's roster.toml and patches the guest tables. Safe to
// call once per launch; does nothing (and logs why) on an unknown image.
void ApplyAtLaunch(rex::memory::Memory* memory);
// Solo hilo guest. UI consume forms::Copy, nunca esta API.
bool FormSelectionRange(uint32_t address, size_t size, bool write = false);
forms::Snapshot ReadForms(int id, int suit);
void InitializeForms();
void ResetForms();

// Character IDs the roster mods made playable (bit per ID), OR-ed into the
// select screen's unlock mask.
uint64_t ExtraUnlockMask();
// Suma los trajes anadidos por mods a la tabla de trajes por casilla del select.
void AddExtraCostumes(uint8_t* base);
bool SlotHasExtraCostumes(uint32_t slot);
// Trajes reales de un personaje anadido por roster.toml (0 = no es de un mod).
int CostumeCountOf(uint32_t id);
// Page registry: L1/R1 requests are queued from UI and consumed on the guest thread.
bool DynamicRosterEnabled();
int DynamicRosterPage();
int DynamicRosterPageCount();
void RequestDynamicRosterPage(int delta);
int64_t LogicalForProxy(int guest);   // L: id logico del proxy o -1
bool ProcessDynamicRosterPage(uint64_t& removed_proxy_mask, uint64_t& added_proxy_mask);

// Donor character ID declared for `id` (-1 if none).
int DonorOf(uint32_t id);
// Franja de ojos propia (roster.toml `ojos`): [textura de data_usi 2064, x0, y0, x1, y1]; false si el ID no la tiene.
bool EyeStrip(uint32_t id, int32_t out[5]);
// Tag Team: miembros del equipo con ese ID (equipo = [...] en roster.toml); 0 si no es un equipo.
int TeamSize(uint32_t id);
// ID del miembro k del equipo (-1 si no hay).
int TeamMember(uint32_t id, int k);
// Nombre interno del ID (registro de char96: "GOKOU", "JANEMBA"...); vacio si no hay.
std::string InternalName(uint32_t id);
// Casilla nueva elegida por el jugador k (0 = 1P): el ID nuevo y el de su anfitrion. En
// "1P VS COM" / Practica la lista del rival vuelve a escribir los IDs por casilla (sale
// el anfitrion); el inicio del combate lo corrige. id < 0 = ninguna.
void SetPickedCell(int player, int id, int host);
int PickedCellFor(int player, int host);
void ClearPickedCells();

// New cell of the select wheel (roster.toml `celda = true`). It shares the saved
// per-slot data (last costume) of the host slot `alias` (default: the donor's slot);
// everything else (ID, icon, name, portrait, custom capsules) is its own.
struct VirtualCell {
  uint32_t id = 0;
  uint32_t alias = 0;
  uint32_t after = 0;                    // va en la rueda tras esta casilla (por defecto alias)
  int32_t icon[5] = {-1, 0, 0, 0, 0};   // textura del #AZT 2027 + rect logico (x0,y0,x1,y1)
  int32_t name[5] = {-1, 0, 0, 0, 0};   // rotulo: igual
  int32_t tor_icon[5] = {-1, 0, 0, 0, 0};   // icono en el #AZT del Torneo (2047): cuadro; igual
  int32_t tor_name[5] = {-1, 0, 0, 0, 0};   // rotulo del VS del Torneo; igual
  uint32_t portrait[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};  // fids (partition<<16|fid) P1/P2
};
constexpr int kMaxVirtualCells = 24;
int VirtualCellCount();
const VirtualCell& GetVirtualCell(int i);

// Guest system-heap allocation (zeroed) for runtime-built guest data; 0 if the
// roster was not applied.
uint32_t GuestAlloc(uint32_t size);

// Extended capsule catalogue (new capsules, inherited owners). Idempotent; called at
// launch and again from the select / battle hooks until the catalogue is in memory.
void EnsureCapsules();

// Select framing (7 values: x, y, z, scale, rx, ry, rz) of a roster ID >= 44 for
// player 0/1; false if none.
bool SelectPos(uint32_t id, uint32_t player, uint32_t out[7]);

// Host slot of a roster ID that has a new select cell (-1 if none).
int HostSlotOf(uint32_t id);

// Skill sheets (pause skill list, capsule captions in battle) of the roster characters:
// SkillTable(true/false) adds/removes their ID -> sheet entries around sub_820E6D70;
// own sheets are served through record slot 1 while sub_821B5FC0 loads them.
void SkillTable(bool push);
uint32_t SkillRecordFor(uint32_t k);
void SetSkillSlot1(uint32_t rec);
uint32_t SkillSlot1();

// Capsule inventory / "Edit Skills": records in the (extended) catalogue, and the new
// capsules marked as owned in a 2048-B inventory (u8 count per capsule ID; the save
// keeps 2048 although the retail catalogue has 596).
uint32_t CapsuleCount();
void OwnNewCapsules(uint32_t inventory);
// Capsula de transformacion unica (ficha 14, ID forms::UnifiedId): bit f = la forma f la da (agrupable y
// desbloqueada, con las anteriores tambien); 0 si el personaje no tiene o el mod no esta activo.
uint8_t UnifiedForms(int id);
// Edit Skills: la capsula unica del personaje (su ID) si da 2+ formas desbloqueadas, si no 0. Cuenta como tuya
// sin escribir el inventario: la partida guardada no cambia (WL-14).
uint32_t UnifiedListed(int id);

// "Custom" capsule list of a roster character (7 x u16, 0xFFFF = empty). The save
// keeps one per select SLOT; a new cell shares its host slot, so the select swaps
// this list in while the new character is being processed. Default: its Normal
// list. Changes are kept in mods/capsulas_custom.txt (the save is not touched).
void GetCustomList(uint32_t id, uint16_t out[7]);
void SetCustomList(uint32_t id, const uint16_t in[7]);

}  // namespace dbz3::roster
