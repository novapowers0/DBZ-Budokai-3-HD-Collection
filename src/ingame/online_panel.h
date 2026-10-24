// dbz3 - Panel ONLINE dentro del modo Duelo (SELECT en el menu de Duelo): crear partida / unirse
// por IP, comprobar mods y partida, y empezar el 1P vs 2P en los dos PCs a la vez.
#pragma once

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include <string>

namespace dbz3::ingame {

class OnlinePanel : public rex::ui::ImGuiDialog {
 public:
  explicit OnlinePanel(rex::ui::ImGuiDrawer* drawer);
  ~OnlinePanel() override;

  static bool IsOpen();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  uint16_t Pad();   // botones recien pulsados del mando local (la palanca izquierda cuenta como cruceta)
  void DrawScreen(ImGuiIO& io, uint16_t pressed);
  bool blocked_ = false;
  uint16_t last_buttons_ = 0;
  int last_link_ = 0;   // Link de la ultima vez (para abrir al perder la conexion)
  bool lost_notice_ = false;   // se perdio en pleno combate: la pantalla sale al acabarlo
  int row_ = 0;
  bool editing_ = false;   // escribiendo la IP con el teclado
  bool wait_release_ = false;   // recien abierta: hasta soltar los botones no se lee el mando
  double opened_at_ = 0;
  char ip_[64] = {};
  bool ip_loaded_ = false;   // ya se puso la IP de la ultima vez
  std::string last_err_;     // error de CREAR/UNIRSE: se ve hasta la siguiente accion (no un fotograma)
  std::string copied_code_;   // ultimo codigo de sala copiado al portapapeles
  bool synced_ = false;       // ya se pidio instalar lo que faltaba (Nova's Mods)
  bool sync_failed_ = false;  // esa instalacion no quedo completa (se ensena el error y se puede reintentar)
  bool was_installing_ = false;  // para comprobar las versiones instaladas una vez al acabar (no cada cuadro)
  std::string lan_;           // IP local del anfitrion (cacheada: LocalAddresses hace DNS)
  double lan_at_ = 0;
  int port_ = 7777;
  int delay_ = 0;   // 0 = AUTO (se ajusta solo durante la partida), 1-8 = fijo
};

// Hilo del guest, una vez por iteracion (sondeo de mandos): pantalla del juego y SELECT en el menu
// de Duelo -> abre el panel (lo dibuja el hilo de la interfaz).
void OnlineTick(uint8_t* base, uint16_t p1_buttons);
bool OnlinePanelWanted();
// Abre el panel ONLINE en cuanto se llegue al menu del Duelo (entrada Online del HUB). Cualquier hilo.
void OnlineOpen();
// Menu principal o de Duelo (no la rueda): sus filas se cuentan aqui (DuelMenuFilter).
bool MenuRowsTracked();
// Mando 1 en el menu de Duelo: la quinta fila ONLINE (devuelve los botones que ve el juego).
uint16_t DuelMenuFilter(uint8_t* base, uint16_t real);
// Pantalla de menu del juego (byte 0x824BA110): 5 menu principal, 14 Duelo, -1 sin leer.
int GameScreen();
// Mando 2 conectado o no (para saber si el Duelo salta la fila 1P VS 2P).
void NotePad2(bool connected);
bool BattleNow();   // hay un combate en curso
bool Pad2Connected();
// Fila del menu principal (0 Dragon Universe, 1 Duelo...), seguida pulsacion a pulsacion.
int MainMenuRow();
void SetMainMenuRow(int row);
// Fila del cursor del Duelo (el juego la recuerda entre visitas).
int DuelMenuRow();
void SetDuelMenuRow(int row);
// Objeto del menu principal en pantalla (guest; +150 = fila del cursor), 0 si no esta.
uint32_t MainMenuObj();

}  // namespace dbz3::ingame
