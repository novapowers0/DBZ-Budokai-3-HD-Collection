// dbz3 - Pantalla TAG TEAM dentro del juego (ver tagteam_panel.cpp).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

namespace dbz3::ingame {

class TagTeamPanel : public rex::ui::ImGuiDialog {
 public:
  explicit TagTeamPanel(rex::ui::ImGuiDrawer* drawer);
  ~TagTeamPanel() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  uint16_t Pad();
  void DrawMenu(ImGuiIO& io, uint16_t pressed);
  void DrawPicking(ImGuiIO& io);
  bool blocked_ = false;
  bool shown_ = false;
  bool wait_release_ = false;   // la A que abre la pantalla no cuenta: se espera a soltarlo todo
  uint16_t last_buttons_ = 0;
  int row_ = 0;
};

void TagTeamOpen();
bool TagTeamWanted();
// Fundido a negro de la pasarela TAG TEAM <-> rueda (el piloto recorre los menus por debajo).
bool TagTeamGate();
// Modo Tag Team activo (de la pantalla TAG TEAM al menu principal).
bool TagTeamMode();
// L: pin - ids ya elegidos por los dos lados (el roster dinamico los ancla al cambiar de pagina).
std::vector<int> TeamPickedIds();
bool FormSelectorEnabled();
// Mando `user` (0/1) en la rueda con el modo Tag Team: Y quita el ultimo miembro.
uint16_t TeamPickFilter(uint16_t real, int user);
// Personaje confirmado en la rueda (select_ext.cpp): true si era un miembro del equipo.
bool OnSelectPicked(int player, int id);
// Via nativa: el lado aun no tiene el equipo completo (el miembro elegido vuelve a la rueda, tagteam.cpp).
bool TeamWantsMore(int side);
// Menu de capsulas del juego tras confirmar (select_ext.cpp): elegido o atras, opcion (0 Normal,
// 1 Custom) y la lista Custom de la casilla (7 IDs, 0xFFFF = hueco).
void OnCapsuleChoice(int player, bool chosen, int option, const uint16_t* custom);
// Forma inicial en la rueda (fuera del Tag Team y del online): LT/RT del mando `user` baja/sube la
// forma con la que empieza el lado que esta eligiendo. StartForm(side) = 0 normal.
void FormPickPad(int user, uint8_t lt, uint8_t rt);
int StartForm(int side);
// La elegida ahora (sin gastarla) y si cambio desde la ultima vez (la vista previa se rehace).
int ChosenForm(int side);
bool TakeFormDirty(int side);
// Para otros modos (frente R1): piloto del menu principal al Duelo (fila 0 = 1P VS COM) y su rueda; aviso del personaje
// confirmado en la rueda por el lado `jugador` (select_ext.cpp lo da tambien fuera del Tag Team; nullptr lo quita).
void PilotarAlDuelo(int fila);
void RegistrarEleccion(void (*aviso)(int jugador, int id));
void AvisarEleccion(int jugador, int id);

}  // namespace dbz3::ingame
