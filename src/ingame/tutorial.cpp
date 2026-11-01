// dbz3 - Tutoriales optativos de los modos nuevos (ver tutorial.h).

#include "tutorial.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "../launcher/i18n.h"
#include "../launcher/launcher_state.h"
#include "../launcher/settings.h"
#include "game_style.h"
#include "quick_settings.h"

namespace dbz3 {
bool GameDimmed();    // ventana sin foco (oscurecida)
bool OverlayOpen();   // menu F1 o launcher encima
}  // namespace dbz3

namespace dbz3::ingame::tutorial {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
namespace D = dbz3::ingame::style::mode;

constexpr uint16_t kA = 0x1000, kB = 0x2000, kY = 0x8000, kStart = 0x0010;

std::mutex g_mu;                       // todo lo de abajo
std::set<std::string> g_vistos;        // ayudas.toml + cerrados en esta sesion
int g_gen = -1;                        // AyudasGeneracion() con la que se leyo
std::string g_id;                      // abierto (vacio = ninguno)
Paso g_titulo{};
std::vector<Paso> g_pasos;
int g_paso = 0;
std::atomic<bool> g_abierto{false};
std::atomic<bool> g_panel{false};
std::set<std::string> g_log_visto;     // "ya visto" se escribe una vez por id y sesion (pruebas)

std::filesystem::path Ruta() { return dbz3::settings::UserDataRoot() / "ayudas.toml"; }

std::vector<std::string> Claves(const std::string& id) {
  std::vector<std::string> out;
  for (size_t i = 0; i <= id.size();) {
    const size_t c = std::min(id.find(',', i), id.size());
    if (c > i) out.push_back(id.substr(i, c - i));
    i = c + 1;
  }
  return out;
}

void Leer() {   // con g_mu
  // PRUEBA: DBZ3_TEST_TUTORIAL_RESET=1 empieza con ayudas.toml movido a respaldo (todo sin ver).
  static const bool reset = std::getenv("DBZ3_TEST_TUTORIAL_RESET") && (ReiniciarAyudas(), true);
  (void)reset;
  if (g_gen == AyudasGeneracion()) return;
  g_gen = AyudasGeneracion();
  g_vistos.clear();
  try {
    const toml::table t = toml::parse_file(Ruta().string());
    for (const auto& [k, v] : t)
      if (v.value_or(false)) g_vistos.insert(std::string(k.str()));
  } catch (...) {   // sin archivo o roto: todo sin ver
    std::error_code ec;
    if (std::filesystem::exists(Ruta(), ec))
      REXLOG_WARN("dbz3 tutorial: {} no se puede leer: los tutoriales apareceran como no vistos", Ruta().string());
  }
}

void Guardar(const std::string& id) {   // con g_mu: anade las claves de `id` y reescribe (atomico: .tmp y rename)
  std::set<std::string> todo;
  try {
    const toml::table t = toml::parse_file(Ruta().string());
    for (const auto& [k, v] : t)
      if (v.value_or(false)) todo.insert(std::string(k.str()));
  } catch (...) {
  }
  for (const std::string& k : Claves(id)) todo.insert(k);
  std::error_code ec;
  std::filesystem::create_directories(Ruta().parent_path(), ec);
  const auto tmp = Ruta().string() + ".tmp";
  if (std::FILE* f = std::fopen(tmp.c_str(), "w")) {
    std::fprintf(f, "# Tutoriales vistos (lo escribe el juego). Quita una linea para volver a verlo.\n");
    for (const std::string& k : todo) std::fprintf(f, "%s = true\n", k.c_str());
    std::fclose(f);
    std::filesystem::rename(tmp, Ruta(), ec);
    if (ec) REXLOG_WARN("dbz3 tutorial: no se pudo guardar {}: {}", Ruta().string(), ec.message());
  } else {
    REXLOG_WARN("dbz3 tutorial: no se pudo crear {}: los tutoriales pueden volver a aparecer", tmp);
  }
}

void Cerrar(bool para_siempre) {   // con g_mu
  for (const std::string& k : Claves(g_id)) g_vistos.insert(k);
  if (para_siempre) Guardar(g_id);
  REXLOG_INFO("dbz3 tutorial: {} cerrado en el paso {}/{} ({})", g_id, g_paso + 1, g_pasos.size(),
              para_siempre ? "visto, guardado" : "solo esta sesion");
  g_id.clear();
  g_abierto = false;
}

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool open = g_abierto.load();
    if (auto* in = Input(); in && open != blocked_) {
      if (open) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
      blocked_ = open;
    }
    if (!open) {
      shown_ = false;
      return;
    }
    if (dbz3::GameDimmed() || dbz3::OverlayOpen()) return;   // el F1 o el launcher encima: espera
    const uint16_t b = PadAhora(), pressed = uint16_t(b & ~last_);
    last_ = b;
    const float t = float(ImGui::GetTime());
    if (!shown_) {   // recien abierto: encima de todo (los paneles de los modos se crearon antes) y sin la tecla que entro
      shown_ = true;
      opened_ = t;
      wait_ = true;
      imgui_drawer()->RemoveDialog(this);
      imgui_drawer()->AddDialog(this);
      return;
    }
    if (wait_ && b == 0 && t - opened_ > 0.3f) wait_ = false;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_id.empty()) return;
    const int n = int(g_pasos.size());
    if (!wait_) {
      if (pressed & kY) return Cerrar(true);
      if (pressed & kB) return Cerrar(false);
      if (pressed & (kA | kStart)) {
        if (++g_paso >= n) return Cerrar(true);   // acabado entero: visto
        REXLOG_INFO("dbz3 tutorial: {} paso {}/{}", g_id, g_paso + 1, n);
      }
    }
    Dibujar(io, std::min(g_paso, n - 1), n, std::clamp((t - opened_) / 0.2f, 0.0f, 1.0f));
  }

 private:
  static void Dibujar(const ImGuiIO& io, int k, int n, float in) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int v0 = dl->VtxBuffer.Size;
    const S::Area a = S::GameArea(io);
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 175));
    // marco del juego en la columna de los modos (S::mode), con el titulo arriba y el paso en el bocadillo
    // centrado en pantalla (S::mode deja sitio al anfitrion a la izquierda; aqui no hay)
    constexpr float kX0 = 600 - (D::kX1 - D::kX0) * 0.5f, kX1 = 600 + (D::kX1 - D::kX0) * 0.5f;
    const ImVec2 p0 = a.At(kX0, D::kTop + 40), p1 = a.At(kX1, D::kBottom + 40);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 40, 56, 235));
    S::Frame(dl, p0, p1, a.s);
    const float w = (kX1 - kX0 - 2 * D::kText) * a.s;
    S::MenuText(dl, a.At(600.0f, D::kTop + 40 + D::kPad + D::kHead * 0.5f + 8), D::kHead * 1.2f * a.s,
                T::T(g_titulo.es, g_titulo.en), true, false, w);
    char cuenta[24];
    std::snprintf(cuenta, sizeof(cuenta), "%d / %d", k + 1, n);
    S::MenuText(dl, a.At(600.0f, D::kTop + 40 + D::kPad + D::kHead + 30), D::kSmall * a.s, cuenta, false, false);
    S::Speech(dl, a.At(kX0 + D::kText, D::kTop + 160), a.At(kX1 - D::kText, D::kBottom + 40 - D::kPad - 16), a.s,
              T::T(g_pasos[size_t(k)].es, g_pasos[size_t(k)].en), -1.0f);
    // barra de botones abajo, sobre una banda opaca (tapa la del modo de debajo)
    dl->AddRectFilled(ImVec2(0, a.At(0, 846).y), ImVec2(io.DisplaySize.x, a.At(0, 900).y), IM_COL32(0, 0, 0, 235));
    D::Help(dl, a, {{"A", k + 1 < n ? T::T("Siguiente", "Next") : T::T("Terminar", "Finish")},
                    {"B", T::T("Cerrar", "Close")},
                    {"Y", T::T("No volver a mostrar", "Don't show again")}});
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {   // entra con un fundido corto, como los menus del juego
      ImU32& c = dl->VtxBuffer[i].col;
      c = (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * in) << 24);
    }
  }

  // como pasarela_misiones.cpp: el mando se lee en el hilo de la interfaz (DeferUi) y aqui se usa el ultimo leido
  static uint16_t PadAhora() {
    static std::atomic<uint16_t> pad{0};
    static std::atomic<bool> pendiente{false};
    if (!pendiente.exchange(true)) {
      dbz3::launcher::DeferUi([] {
        uint16_t b = 0;
        if (auto* in = Input()) {
          rex::input::X_INPUT_STATE st = {};
          if (in->GetStateForUI(0, &st) == 0) b = static_cast<uint16_t>(st.gamepad.buttons);
        }
        pad = b;
        pendiente = false;
      });
    }
    return pad.load();
  }

  bool blocked_ = false, shown_ = false, wait_ = false;
  uint16_t last_ = 0;
  float opened_ = 0;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

}  // namespace

bool Abierto() { return g_abierto.load(); }

bool Visto(const char* id) {
  std::lock_guard<std::mutex> lk(g_mu);
  Leer();
  return g_vistos.count(Claves(id).empty() ? std::string() : Claves(id).front()) > 0;
}

bool Abrir(const char* id, Paso titulo, const std::vector<Paso>& pasos) {
  if (!id || !*id || pasos.empty()) return false;
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_id.empty()) return g_id == id;
  // las pruebas (tools/pruebas.py pone DBZ3_NO_DIALOGS) no los ven, salvo la suya (DBZ3_TEST_TUTORIAL)
  static const bool pruebas = std::getenv("DBZ3_NO_DIALOGS") && !std::getenv("DBZ3_TEST_TUTORIAL");
  if (pruebas || rex::cvar::GetFlagByName("dbz3_tutoriales") == "false") return false;
  Leer();
  const std::string primera = Claves(id).front();
  if (g_vistos.count(primera)) {
    if (g_log_visto.insert(primera).second) REXLOG_INFO("dbz3 tutorial: {} ya visto (no se muestra)", primera);
    return false;
  }
  g_id = id;
  g_titulo = titulo;
  g_pasos = pasos;
  g_paso = 0;
  g_abierto = true;
  if (!g_panel.exchange(true)) dbz3::launcher::DeferUi(&CreatePanel);
  REXLOG_INFO("dbz3 tutorial: abre {} ({} pasos)", g_id, g_pasos.size());
  return true;
}

// ---- Catalogo: un tutorial por modo (textos de E:\DBZ3HD_trabajo\integracion\ayudas_tutoriales.md, seccion 15,
// comprobados en el codigo de cada modo) ----
bool Mostrar(const char* id) {
  using V = std::vector<Paso>;
  const std::string k = id ? id : "";
  if (k == "hub")
    return Abrir("hub", {"HUB", "HUB"},
                 V{{"Aquí están los modos nuevos: Tag Team, Online, retos y más.",
                    "Here are the new modes: Tag Team, Online, challenges and more."},
                   {"Una fila en gris significa que te falta algo. El bocadillo dice qué.",
                    "A greyed-out row means something is missing. The bubble says what."},
                   {"Pulsa A para entrar. B vuelve al menú.", "Press A to enter. B goes back to the menu."},
                   {"Tu progreso se guarda solo.", "Your progress is saved automatically."}});
  if (k == "retos")
    return Abrir("retos", {"Retos", "Challenges"},
                 V{{"Elige un reto de la lista. Cada grupo tiene su estilo.",
                    "Pick a challenge from the list. Each group has its own style."},
                   {"Lee la regla especial o el tiempo antes de empezar.",
                    "Read the special rule or time limit before starting."},
                   {"Si pierdes, puedes reintentar o dejar el reto (la partida queda guardada).",
                    "If you lose, you can retry or leave (the run stays saved)."},
                   {"Pulsa \"Guardar y salir\" para seguir otro día.", "Press \"Save and quit\" to continue another day."}});
  if (k == "heroe")
    return Abrir("heroe", {"Modo Héroe", "Hero Mode"},
                 V{{"Crea tu héroe: raza, cuerpo, apariencia y estilo.", "Create your hero: race, body, look and style."},
                   {"El cuerpo es el modelo; el estilo, los golpes (de un maestro u otro luchador).",
                    "The body is the model; the style is the moves (from a master or another fighter)."},
                   {"LB/RB cambian el estilo, también entre combates. La estrella marca a un maestro (da bonus).",
                    "LB/RB change the style, also between fights. The star marks a master (gives a bonus)."},
                   {"Una misión son 3 combates seguidos. Cada victoria da experiencia.",
                    "A mission is 3 fights in a row. Each win gives experience."},
                   {"Sube de nivel para más vida y daño.", "Level up for more health and damage."}});
  if (k == "satan")
    return Abrir("satan", {"Leyenda de Mr. Satán", "Legend of Hercule"},
                 V{{"Mr. Satán se enfrenta a 11 retadores en los Juegos de Cell.",
                    "Mr. Satan faces 11 challengers at the Cell Games."},
                   {"Antes de empezar, elige el nivel del COM (Novato, Experto, Avanzado) con arriba y abajo.",
                    "Before starting, pick the computer level (Novice, Expert, Advanced) with up and down."},
                   {"Cada ronda es un combate. Si pierdes, reintenta.", "Each round is a fight. If you lose, retry."},
                   {"Ganas puntos con los golpes, combos y llaves, como en Budokai 1.",
                    "You score points with hits, combos and throws, as in Budokai 1."}});
  if (k == "dragon_world")
    return Abrir("dragon_world", {"Dragon World", "Dragon World"},
                 V{{"Mueve a tu personaje casilla a casilla (A). RB cambia el personaje que mueves.",
                    "Move your character space by space (A). RB switches the character you move."},
                   {"Busca en una casilla (X). Puede salir un objeto o una Bola de Dragón.",
                    "Search a space (X). You may find an item or a Dragon Ball."},
                   {"Tocar a un rival empieza un combate 1 contra 1. Algunas casillas ponen condiciones al combate.",
                    "Touching a rival starts a 1-on-1 fight. Some spaces add conditions to the fight."},
                   {"Dende te cura. La alfombra de Mr. Popo te lleva a otra casilla.",
                    "Dende heals you. Mr. Popo's carpet carries you to another space."},
                   {"Tu vida se guarda entre combates: cuidado.", "Your health carries over between fights: be careful."},
                   {"Reúne las 7 Bolas para que Shenron te conceda un deseo.",
                    "Collect all 7 Balls for Shenron to grant a wish."}});
  if (k == "nave_babidi")
    return Abrir("nave_babidi", {"Nave de Babidi", "Babidi's Ship"},
                 V{{"Las 4 pruebas dan Kili.", "The 4 trials give Kili."},
                   {"Kili = puntos. Al llegar a cada cantidad, un premio de Babidi.",
                    "Kili = points. At each amount, a Babidi prize."},
                   {"Lee cada prueba antes de empezar: cada una tiene su regla.",
                    "Read each trial before starting: each has its own rule."}});
  if (k == "dragon_mission")
    return Abrir("dragon_mission", {"Dragon Mission", "Dragon Mission"},
                 V{{"Elige una misión en el mapa (A). Las bloqueadas dicen qué falta.",
                    "Pick a mission on the map (A). Locked ones say what is missing."},
                   {"En el campo, acércate a los objetos para cogerlos.", "In the field, walk close to items to pick them up."},
                   {"El rango (Z a C) depende de cuántos objetos cojas.",
                    "The rank (Z to C) depends on how many items you pick up."},
                   {"Los ? del mapa son minijuegos. BACK abre la guía del juego original.",
                    "The ? on the map are minigames. BACK opens the original game's guide."},
                   {"Y: Fighter's Road, combates seguidos con tu luchador.", "Y: Fighter's Road, fights in a row with your fighter."}});
  if (k == "fighters_road")
    return Abrir("fighters_road", {"Fighter's Road", "Fighter's Road"},
                 V{{"Combates seguidos con el luchador que elijas en la rueda.",
                    "Fights in a row with the fighter you pick on the wheel."},
                   {"Deja K.O. a cada rival para abrir el siguiente.", "Knock out each rival to open the next one."},
                   {"B o Y vuelve a Dragon Mission.", "B or Y goes back to Dragon Mission."}});
  // minijuegos de IW: el general (controles) va delante del de su tipo la primera vez
  if (k != "iw_mini_carrera" && k != "iw_mini_vuelo" && k != "iw_mini_disparo" && k != "iw_mini_botones") return false;
  V p;
  if (!Visto("iw_minijuegos")) {
    p = {{"Lee la frase del inicio: te dice el objetivo.", "Read the opening line: it tells you the goal."},
         {"Palanca izquierda: andar. B: correr más rápido mientras quede barra amarilla.",
          "Left stick: walk. B: sprint while the yellow bar lasts."},
         {"Palanca derecha: girar la cámara.", "Right stick: turn the camera."}};
  }
  if (k == "iw_mini_carrera")
    p.push_back({"Carrera: llega a la meta o atrapa a tu objetivo. Cuidado: los plátanos te hacen resbalar.",
                 "Race: reach the goal or catch your target. Watch out: bananas make you slip."});
  else if (k == "iw_mini_vuelo")
    p.push_back({"Vuelo: la palanca arriba y abajo cambia la velocidad. Los objetos también la cambian.",
                 "Flight: the stick up and down changes your speed. Items change it too."});
  else if (k == "iw_mini_disparo")
    p.push_back({"Disparo: apunta con la palanca derecha y dispara con B.", "Shooting: aim with the right stick and fire with B."});
  else
    p.push_back({"Botones: pulsa el botón del icono cuando llegue a la línea (también los gatillos LT RT LB RB).",
                 "Buttons: press the icon's button when it reaches the line (triggers LT RT LB RB too)."});
  return Abrir((k + ",iw_minijuegos").c_str(), {"Minijuego", "Minigame"}, p);
}

// Minijuego de la casilla `clave` del mapa de IW (0x7D0..0x7E1; registros de iw_mini_disparo/vuelo/botones.cpp).
bool MostrarMinijuego(int clave) {
  switch (clave) {
    case 0x7D3: case 0x7D7: case 0x7D8: case 0x7DE: return Mostrar("iw_mini_disparo");
    case 0x7D6: case 0x7E0: return Mostrar("iw_mini_vuelo");
    case 0x7D4: case 0x7D5: case 0x7DA: case 0x7DB: case 0x7DC: case 0x7E1: return Mostrar("iw_mini_botones");
    default: return Mostrar("iw_mini_carrera");   // los de P4 (iw_minijuegos.cpp): a pie hasta la meta
  }
}

}  // namespace dbz3::ingame::tutorial
