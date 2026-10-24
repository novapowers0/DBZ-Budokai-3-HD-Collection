// dbz3 - Panel ONLINE del modo Duelo (ver online_panel.h y src/net/netplay.h).
#include "online_panel.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>

#include "../eu_map.h"
#include "generated/dbz3_init.h"
#include "../launcher/i18n.h"
#include "../launcher/launcher_state.h"
#include "../launcher/novas_mods.h"
#include "../launcher/settings.h"
#include "../mods.h"
#include "../net/netplay.h"
#include "../launcher/ui_kit.h"
#include "game_style.h"
#include "native_ui.h"
#include "tagteam_panel.h"
#ifndef DBZ3_EU_VARIANT
#include "../modos.h"   // rango del ranking local (modos.cpp no entra en la build solo EU)
#endif

#include <rex/logging.h>

// La ultima IP a la que se unio el jugador: la pantalla ONLINE la trae escrita la proxima vez.
REXCVAR_DEFINE_STRING(dbz3_net_last_ip, "", "DBZ3/Online", "Last host IP joined from the ONLINE screen");
// Mods nativos del launcher: se pueden quitar (sin la fila del menu ni su pantalla)
REXCVAR_DEFINE_BOOL(dbz3_tag_team_enabled, true, "DBZ3/Mods", "TAG TEAM entry in the main menu");
REXCVAR_DEFINE_BOOL(dbz3_online_enabled, true, "DBZ3/Mods", "ONLINE entry in the Duel menu");

namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
void SetWindowIconRgba(const uint8_t* rgba, int w, int h);
}

namespace dbz3::tag {
bool InBattle();
void OnScreen(int screen);
}
bool dbz3_select_live();   // select_ext.cpp: la rueda de personajes esta en marcha
bool dbz3_select_to_stage();   // los dos confirmaron: escenario (misma pantalla 14)
void dbz3_select_reset_flow();

namespace dbz3::hub {
bool RowActive();   // hub_menu.cpp: la fila HUB ocupa el sitio de la fila TAG TEAM (Tag Team sigue con X)
}
namespace dbz3::campaigns {
void Trace(uint8_t* base);   // campaigns.cpp
void ForgetStoryBattle();   // campaigns.cpp: menu principal = fuera de Dragon Universe
}

namespace dbz3::ingame {
namespace {

namespace T = dbz3::i18n;
using dbz3::net::Link;
using dbz3::net::Session;

std::atomic<bool> g_wanted{false};
std::atomic<bool> g_open_at_duel{false};   // OnlineOpen(): el panel se abre al llegar al Duelo
std::atomic<bool> g_open{false};
std::atomic<uint16_t> g_pad{0};          // mando 1 leido fuera del pintado (DeferUi)
std::atomic<bool> g_pad_pending{false};
uint16_t g_prev_select = 0;

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

// Rotulo "(X) TEXTO" abajo a la derecha del area 4:3 del juego, con el marco naranja y el azul de
// los menus de B3; solo se dibuja (no captura el raton ni el mando).
void EntryBadge(ImGuiIO& io, const char* text) {
  namespace S = dbz3::ingame::style;
  const S::Area a = S::GameArea(io);
  const ImVec2 p0 = a.At(792, 630), p1 = a.At(1140, 697.5f);
  auto* dl = ImGui::GetForegroundDrawList();
  S::Frame(dl, p0, p1, a.s);
  dl->PushClipRect(p0, p1, true);
  // cabe en la caja (300 de 1200): si el idioma es largo se encoge el rotulo en vez de cortarlo con el recorte
  const float em = ImGui::GetFont()->CalcTextSizeA(1.0f, FLT_MAX, 0, text).x;
  const float h = std::min(36.0f * a.s, 300.0f * a.s / (1.2f + 0.62f * em * 1.15f));
  S::HelpLine(dl, a.At(966, 663.75f), h, {{"X", text}});
  dl->PopClipRect();
}

// Fundido de nuestras filas con el del juego (medido 2026-10-11 con capturas cada 0,5 s): al
// confirmar (A/B) la lista sale en ~0,3 s y la pantalla funde a negro en ~1,5 s; al llegar a un
// menu la pantalla sale de negro en ~1,5 s. El tiempo de llegada y el de salida los anota OnlineTick.
// Fundido a negro del propio juego (US): float en 0x8243A278, 0 = se ve todo, 1 = negro. Encontrado el
// 2026-10-15 comparando la memoria con el brillo de capturas al entrar y salir del Duelo (r = -0,95;
// E:\DBZ3HD_trabajo\bugs\analiza_fundido.py). Lo lee OnlineTick (hilo del juego) en cada cuadro. Con el
// la fila ONLINE ya no sale sobre el negro (el byte de pantalla cambia con la pantalla aun negra y el negro
// dura mas o menos segun lo que cargue). EU (sin medir): la cuenta de tiempo de antes, con una espera.
constexpr dbz3::GA kGameFade = 0x8243A278;   // misma direccion en US y EU (tools/eu_map.py)
std::atomic<float> g_game_fade{0.0f};
std::atomic<int64_t> g_enter_ms{0}, g_leave_ms{0};   // 0 = sin salida pendiente
std::atomic<int> g_enter_delay_ms{0};                 // EU: espera antes de salir de negro (por pantalla)
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
float MenuFade() {
  const int64_t now = NowMs(), leave = g_leave_ms.load();
  float a = 1.0f - g_game_fade.load();
  if (leave) a *= std::clamp(1.0f - float(now - leave) / 300.0f, 0.0f, 1.0f);
  return a;
}
// multiplica el alfa de lo dibujado desde el vertice v0 (colores ABGR de ImGui)
void FadeFrom(ImDrawList* dl, int v0, float alpha) {
  for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
    ImU32& c = dl->VtxBuffer[i].col;
    c = (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * alpha) << 24);
  }
}

// Indicador del online mientras se juega (menus y combate), abajo a la izquierda del area del juego: retardo
// actual (AUTO si se ajusta solo), barras de la conexion y ping. Marco y letra de los menus del juego; al cambiar
// el retardo se ilumina 2 s. Solo dibuja.
void NetBadge(ImGuiIO& io, const Session& s) {
  namespace S = dbz3::ingame::style;
  const S::Area a = S::GameArea(io);
  auto* dl = ImGui::GetForegroundDrawList();
  ImFont* font = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float h = 26 * a.s, gap = 12 * a.s, bar = 7 * a.s, step = 10 * a.s;
  const int ping = s.ping_ms();
  char left[48], right[24];
  std::snprintf(left, sizeof(left), T::T("RETARDO %d%s", "DELAY %d%s"), s.delay(), s.auto_delay() ? " AUTO" : "");
  std::snprintf(right, sizeof(right), ping >= 0 ? "%d ms" : "- ms", ping);
  const float w1 = font->CalcTextSizeA(h, FLT_MAX, 0, left).x, w2 = font->CalcTextSizeA(h, FLT_MAX, 0, right).x;
  const ImVec2 p0 = a.At(40, 806);   // encima de la linea de reglas de combate.cpp (y 868, centrada)
  const ImVec2 p1(p0.x + 2 * gap + w1 + gap + 4 * step + gap + w2, p0.y + 36 * a.s);
  const float cy = (p0.y + p1.y) * 0.5f;
  S::Frame(dl, p0, p1, a.s * 0.5f);
  // retardo recien cambiado: la franja de la fila elegida de los menus del juego, 2 s
  if (NowMs() - int64_t(s.delay_changed_ms()) < 2000) S::Highlight(dl, p0, p1, float(ImGui::GetTime()));
  float x = p0.x + gap;
  S::MenuText(dl, ImVec2(x + w1 * 0.5f, cy), h, left, true, false);
  x += w1 + gap;
  // 4 barras con buen ping, 1 con muy malo (doradas como el marco; las que faltan, oscuras)
  const int bars = ping < 0 ? 0 : ping < 50 ? 4 : ping < 100 ? 3 : ping < 160 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    const ImVec2 b0(x + i * step, cy + 9 * a.s - (i + 1) * 4.5f * a.s), b1(x + i * step + bar, cy + 9 * a.s);
    dl->AddRectFilled(b0, b1, i < bars ? IM_COL32(240, 190, 50, 255) : IM_COL32(40, 45, 70, 210), 1.5f * a.s);
    dl->AddRect(b0, b1, IM_COL32(30, 20, 10, 255), 1.5f * a.s, 0, 1.2f * a.s);
  }
  x += 4 * step + gap;
  S::MenuText(dl, ImVec2(x + w2 * 0.5f, cy), h, right, true, false);
}

// Sala: rango de los dos (ficha 01, "reto que muestra el rango") con la medalla y el Elo, como las filas de la pagina
// de Ranking del HUB (modos.cpp): TU arriba, RIVAL debajo (llega del otro PC al conectar). Si caben, una tercera fila
// con las reglas opcionales del combate (F1 > COMBATE): iguales en los dos = van activas; distintas = apagadas.
using dbz3::net::CombatRules;
// "Esperando..." con los puntos yendo y viniendo con el tiempo (la traduccion se busca con los tres puntos).
std::string WaitDots(const char* text, float t) {
  std::string s = text;
  while (!s.empty() && s.back() == '.') s.pop_back();
  return s + std::string(size_t(int(t * 2.5f) % 4), '.');
}

std::string RulesText(const CombatRules& r) {
  std::string out;
  auto add = [&out](const char* x) { out += (out.empty() ? "" : "  ") + std::string(x); };
  if (r.flags & 1) add("V0");
  if (r.flags & 2) add(T::T("DEF. DIRECTA", "DIRECT ULT."));
  if (r.flags & 4) add(T::T("CANCELACION LB", "CANCEL LB"));
  if (r.flags & 8) add(T::T("CANCELACION RB", "CANCEL RB"));
  if (r.reversion) add(T::T("PIERDE FORMA", "FORM LOSS"));
  return out;
}

void RoomRows(ImDrawList* dl, const dbz3::ingame::style::Area& a, float y0, int filas, const Session& s, float t) {
  namespace S = dbz3::ingame::style;
  static const char* const kMedallas[] = {"retos/medalla_1_bronce", "retos/medalla_2_plata", "retos/medalla_3_oro",
                                          "retos/medalla_4_platino", "retos/medalla_5_diamante"};
  auto nombre = [](int tramo) {   // los tramos de modos.cpp: cada 100 de Elo desde 1000
    switch (tramo) {
      case 0: return T::T("Guerrero Z", "Z Fighter");
      case 1: return "Super Saiyan";
      case 2: return T::T("Kaio", "King Kai");
      case 3: return T::T("Kaio-shin", "Supreme Kai");
      default: return "Shenron";
    }
  };
  int elo[2] = {1000, 1000}, tramo[2] = {-1, -1};
  CombatRules reglas[2];
  s.MyRoom(&elo[0], &tramo[0], &reglas[0]);
  const bool peer = s.PeerRoom(&elo[1], &tramo[1], &reglas[1]);
  constexpr float paso = 44;
  S::Frame(dl, a.At(330, y0), a.At(870, y0 + 16 + paso * float(filas)), a.s);
  const float k = 30 * a.s;
  for (int i = 0; i < 2; ++i) {
    const float y = y0 + 8 + paso * (float(i) + 0.5f);
    if (i == 0) S::Highlight(dl, a.At(330, y - paso * 0.5f + 2), a.At(870, y + paso * 0.5f - 2), t);
    S::MenuText(dl, a.At(395, y), k, i == 0 ? T::T("TÚ", "YOU") : T::T("RIVAL", "RIVAL"), i == 0, false);
    if (i == 1 && !peer) {
      S::MenuText(dl, a.At(640, y), k, WaitDots(T::T("Esperando...", "Waiting..."), t).c_str(), false, true);
      continue;
    }
    if (tramo[i] < 0) {
      S::MenuText(dl, a.At(640, y), k, T::T("Sin combates", "No fights yet"), i == 0, false);
      continue;
    }
    if (const S::Pic md = S::Img(kMedallas[std::clamp(tramo[i], 0, 4)])) {
      const float r = paso * 0.42f;
      S::DrawPic(dl, md, a.At(470 - r, y - r), a.At(470 + r, y + r));
    }
    S::MenuText(dl, a.At(630, y), k, nombre(tramo[i]), i == 0, false, 150 * a.s);   // sin pisar la columna del Elo (x 800)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", elo[i]);
    S::MenuText(dl, a.At(800, y), k, buf, i == 0, false);
  }
  if (filas < 3) return;
  const float y = y0 + 8 + paso * 2.5f;
  S::MenuText(dl, a.At(395, y), k * 0.8f, T::T("REGLAS", "RULES"), false, false);
  std::string val;
  bool on = false, off = false;
  if (!peer) {
    val = WaitDots(T::T("Esperando...", "Waiting..."), t);
    off = true;
  } else if (!(reglas[0] == reglas[1])) {
    val = T::T("Distintas: combate original", "Different: original combat");
    off = true;
  } else if (!reglas[0].any()) {
    val = T::T("Combate original", "Original combat");
  } else {
    val = RulesText(reglas[0]);
    on = true;
  }
  ImFont* font = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  const float w = font->CalcTextSizeA(k, FLT_MAX, 0, val.c_str()).x, max = 380 * a.s;   // que quepa en la fila
  S::MenuText(dl, a.At(660, y), w > max ? k * max / w : k, val.c_str(), on, off);
}

// Pasos de la pantalla ONLINE (caja a la izquierda) y estado de la conexion con color y ping:
// 1 crear/unirse, 2 compartir el codigo, 3 esperando/conectando, 4 combate. El paso actual va iluminado.
void StepsBox(ImDrawList* dl, const dbz3::ingame::style::Area& a, const Session& s, bool host) {
  namespace S = dbz3::ingame::style;
  const Link link = s.link();
  int step = 1;   // paso en el que estas
  if (link == Link::kWaiting) step = host ? 2 : 3;
  else if (link == Link::kConnected) step = 3;
  else if (link == Link::kStarting || link == Link::kPlaying) step = 4;
  const char* names[4] = {T::T("1 CREAR O UNIRSE", "1 HOST OR JOIN"), T::T("2 PASAR EL CODIGO", "2 SHARE THE CODE"),
                          T::T("3 CONECTANDO", "3 CONNECTING"), T::T("4 A COMBATIR", "4 FIGHT")};
  const ImVec2 p0 = a.At(14, 215), p1 = a.At(318, 215 + 46.0f * 4 + 94);
  S::Frame(dl, p0, p1, a.s * 0.7f);
  for (int i = 0; i < 4; ++i) {
    const float y = 215 + 24 + 46.0f * float(i);
    S::MenuText(dl, a.At(166, y), 28 * a.s, names[i], i + 1 == step, i + 1 < step, 270 * a.s);
  }
  // estado: punto de color (gris sin conexion, ambar esperando, verde conectado, rojo error) y ping
  const bool bad = link == Link::kError || link == Link::kLost;
  const ImU32 col = bad ? IM_COL32(230, 60, 60, 255)
                    : (link == Link::kConnected || link == Link::kStarting || link == Link::kPlaying) ? IM_COL32(70, 210, 90, 255)
                    : link == Link::kWaiting ? IM_COL32(240, 190, 50, 255) : IM_COL32(150, 150, 150, 255);
  const char* txt = bad ? T::T("SIN CONEXION", "NO CONNECTION")
                    : link == Link::kWaiting ? T::T("ESPERANDO", "WAITING")
                    : link == Link::kIdle ? T::T("DESCONECTADO", "OFFLINE") : T::T("CONECTADO", "CONNECTED");
  const float y = 215 + 46.0f * 4 + 36;
  dl->AddCircleFilled(a.At(44, y), 11 * a.s, col);
  dl->AddCircle(a.At(44, y), 11 * a.s, IM_COL32(30, 20, 10, 255), 0, 1.5f * a.s);
  S::MenuText(dl, a.At(176, y), 26 * a.s, txt, false, false, 220 * a.s);
  if (s.ping_ms() >= 0 && (link == Link::kConnected || link == Link::kStarting || link == Link::kPlaying)) {
    char pb[24];
    std::snprintf(pb, sizeof(pb), s.ping_ms() >= 0 ? "PING %d ms" : "PING - ms", s.ping_ms());
    S::MenuText(dl, a.At(176, y + 30), 22 * a.s, pb, false, false, 220 * a.s);
  }
}

// Quinta fila "ONLINE" del menu de Duelo. Medidas de una captura a 1600x900 (area 4:3 desde
// x=200): interior del marco x 340..860, filas centradas en y 245/330/410/492, borde de abajo
// en y 538. El marco se alarga una fila (relleno del color del interior y bordes dorados).
void OnlineRow(ImGuiIO& io, bool focus) {
  namespace S = dbz3::ingame::style;
  const S::Area a = S::GameArea(io);
  auto* dl = ImGui::GetForegroundDrawList();
  const float fade = MenuFade();
  if (fade <= 0.001f) return;
  const int v0 = dl->VtxBuffer.Size;
  // un recuadro propio bajo la lista del Duelo, como el de TAG TEAM del menu principal: marco,
  // interior y franja de agua del juego (data_usi 2033)
  const ImVec2 r0 = a.At(430, 562), r1 = a.At(770, 614);
  S::Frame(dl, r0, r1, a.s);
  if (focus) {
    // la franja del juego sigue en COM VS COM: se apaga, la elegida es ONLINE
    dl->AddRectFilled(a.At(340, 452), a.At(860, 533), IM_COL32(10, 25, 45, 150));
    S::Highlight(dl, r0, r1, float(ImGui::GetTime()));
    S::Speech(dl, a.At(195, 650), a.At(950, 860), a.s,
              T::T("Juega contra otra persona por internet.", "Play against another person over the internet."));
  }
  S::MenuText(dl, ImVec2((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f), 50 * a.s, "ONLINE", focus, false);
  FadeFrom(dl, v0, fade);
}

// Octava "fila" del menu principal: TAG TEAM en un recuadro con el marco del juego bajo la lista
// (medido a 1600x900: interior de la lista x 105..575 del area, ultima fila en y 560, marco hasta
// y ~625; el bocadillo del juego va de y 646 a 866 y su primera linea de texto esta en y ~680).
void TagTeamRow(ImGuiIO& io, bool focus) {
  namespace S = dbz3::ingame::style;
  const S::Area a = S::GameArea(io);
  auto* dl = ImGui::GetForegroundDrawList();
  const float fade = MenuFade();
  if (fade <= 0.001f) return;
  const int v0 = dl->VtxBuffer.Size;
  // entre el adorno de abajo del marco y el bocadillo: sin tapar el texto de ayuda del juego (2026-10-12:
  // en y 640..694 se comia la primera linea de la ayuda de las otras filas)
  const ImVec2 r0 = a.At(160, 606), r1 = a.At(520, 650);
  if (focus) {
    dl->AddRectFilled(a.At(105, 523), a.At(575, 597), IM_COL32(10, 25, 45, 150));   // Dragon Arena ya no es la elegida
    // el bocadillo del juego entero (y 646..866) tapado por el nuestro: no asoma su texto
    S::Speech(dl, a.At(30, 646), a.At(790, 868), a.s,
              T::T("Equipos de 2 o 3 luchadores: SELECT para relevar al compañero.",
                   "Teams of 2 or 3 fighters: SELECT to switch partners."), -1.0f);
  }
  S::Frame(dl, r0, r1, a.s);
  if (focus) S::Highlight(dl, r0, r1, float(ImGui::GetTime()));
  S::MenuText(dl, ImVec2((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f), 40 * a.s, "Tag Team", focus, false);
  FadeFrom(dl, v0, fade);
}

}  // namespace

// Pantalla de menu del juego: byte 0x824BA110 (EU 0x824BA100), medido en el juego el 2026-10-10:
// 5 = menu principal, 14 = menu de Duelo (1P VS COM...). BACK abre la ayuda del juego en todos los
// menus, asi que nuestras entradas van con X, que en esos dos menus no hace nada.
constexpr int kMainMenuScreen = 5, kDuelMenuScreen = 14;
std::atomic<int> g_screen{-1};
// ---- VANILLA (V): reconexion automatica de una sesion sin mods (hilo de la interfaz). Tras reiniciar en vanilla viene del
// fichero temporal; si este PC ya estaba en vanilla, espera a que el otro (que se reinicia) desaparezca y vuelve a llamar.
struct VanillaAuto {
  bool active = false, host = false, loaded = false, need_drop = false;
  int port = 7777, delay = 0;
  std::string target;
  double deadline = 0, next_try = 0, agreed_at = 0;
};
VanillaAuto g_vn;
void VanillaLoadPending() {   // una vez por arranque: el fichero lo dejo el PC que se reinicio
  if (g_vn.loaded) return;
  g_vn.loaded = true;
  std::string txt;
  if (!dbz3::TakeVanillaPending(txt)) return;
  std::vector<std::string> v;
  size_t a = 0;
  for (size_t b; (b = txt.find('\n', a)) != std::string::npos; a = b + 1) v.push_back(txt.substr(a, b - a));
  if (v.size() < 3) return;
  g_vn.host = v[0] == "host";
  g_vn.port = std::clamp(std::atoi(v[1].c_str()), 1024, 65535);
  g_vn.delay = std::clamp(std::atoi(v[2].c_str()), 0, 8);
  g_vn.target = v.size() > 3 ? v[3] : "";
  g_vn.active = g_vn.host || !g_vn.target.empty();
  REXLOG_INFO("dbz3 online: sesion vanilla, se reconecta como {}", g_vn.host ? "anfitrion" : "invitado");
}
std::atomic<bool> g_battle{false};
std::atomic<bool> g_online_focus{false};   // la fila ONLINE (quinta del Duelo) tiene el cursor
std::atomic<bool> g_tag_focus{false};      // la fila TAG TEAM (bajo el menu principal) tiene el cursor
std::atomic<bool> g_pad2{false};           // mando 2 conectado (1P VS 2P activo en el Duelo)
uint16_t g_swallow = 0;                     // botones que el juego no ve hasta que se suelten

std::atomic<int> g_main_row{0};             // fila del menu principal (seguida aqui)
// Fila real del menu principal: byte +150 de su objeto (tarea +48), leido por el propio menu en
// sub_8218C5D8 (fila 0 Dragon Universe, 1 Duelo -> pantalla 14, ... 6 Dragon Arena). Solo US; si
// no llega (EU) se usa la cuenta de arriba.
std::atomic<int> g_real_main_row{-1};
std::atomic<int64_t> g_real_main_at{0};
std::atomic<uint32_t> g_main_obj{0};        // objeto del menu principal (+150 = fila)

int GameScreen() { return g_screen.load(); }
uint32_t MainMenuObj() { return NowMs() - g_real_main_at.load() < 500 ? g_main_obj.load() : 0; }
bool MenuRowsTracked() {
  const int sc = g_screen.load();
  return sc == kMainMenuScreen || (sc == kDuelMenuScreen && !dbz3_select_live() && !dbz3_select_to_stage());
}
void NotePad2(bool connected) { g_pad2 = connected; }
bool BattleNow() { return g_battle.load(); }
bool Pad2Connected() { return g_pad2.load(); }
int MainMenuRow() {
  if (NowMs() - g_real_main_at.load() < 500 && g_real_main_row.load() >= 0) return g_real_main_row.load();
  return g_main_row.load();
}
void SetMainMenuRow(int row) { g_main_row = row; }

void OnlineOpen() { g_open_at_duel = true; }
bool OnlinePanelWanted() { return g_wanted.load() || Session::Get().link() != Link::kIdle; }
bool OnlinePanel::IsOpen() { return g_open.load(); }

// Quinta fila del Duelo: con el cursor del juego en COM VS COM, abajo pasa a ONLINE (el juego no
// lo ve); arriba vuelve, abajo da la vuelta a 1P VS COM y A abre la pantalla ONLINE.
// El cursor del juego vive en un objeto que cambia de sitio en cada visita, asi que se sigue aqui:
// el menu nace en la fila 0, abajo/arriba recorren las 4 filas (1P VS 2P gris tambien) y dan la vuelta, y cada
// pulsacion cuenta una vez (mantener pulsado no repite en este menu, para no perder la cuenta).
std::atomic<bool> g_close_req{false};
// el juego recuerda la fila del Duelo entre visitas (el piloto de TAG TEAM pulsaba A en COM VS 2P
// creyendo estar en 1P VS COM, 2026-10-12): la cuenta no se reinicia al entrar
std::atomic<int> g_duel_row{0};
int DuelMenuRow() { return g_duel_row.load(); }
void SetDuelMenuRow(int row) { g_duel_row = row; }

uint16_t DuelMenuFilter(uint8_t* base, uint16_t real) {
  (void)base;
  constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kUD = 0x0003, kA = 0x1000, kB = 0x2000, kY = 0x8000;
  using Clock = std::chrono::steady_clock;
  static uint16_t prev = 0;
  int row = g_duel_row.load();
  static Clock::time_point ud_at;     // cuando empezo la ultima pulsacion de arriba/abajo
  static bool ud_cap = false;         // arriba/abajo se corta tras el primer paso (sin repeticion)
  const uint16_t pressed = real & ~prev;
  prev = real;
  g_swallow &= real;   // soltado: ya no se oculta
  if (!(real & kUD)) ud_cap = false;
  // un paso por pulsacion: el juego ve arriba/abajo un momento (lo justo para moverse) y no mas
  auto one_step = [&]() {
    ud_at = Clock::now();
    ud_cap = true;
  };
  auto out = [&]() -> uint16_t {
    uint16_t mask = g_swallow;
    if (ud_cap && Clock::now() - ud_at > std::chrono::milliseconds(120)) mask |= kUD;
    return uint16_t(real & ~mask);
  };
  // la rueda de personajes tambien es la pantalla 14: se distingue por su logica (select_ext.cpp)
  const int screen = (g_screen.load() == kDuelMenuScreen && (dbz3_select_live() || dbz3_select_to_stage())) ? -2
                                                                                                  : g_screen.load();
  // menu principal (7 filas, recuerda la ultima): se sigue igual para el piloto de TAG TEAM
  // con la ultima fila (Dragon Arena), abajo pasa a TAG TEAM (el juego no lo ve); arriba vuelve,
  // abajo da la vuelta a la primera fila y A abre la pantalla TAG TEAM
  if (screen == kMainMenuScreen && !TagTeamWanted() && REXCVAR_GET(dbz3_tag_team_enabled) && !dbz3::hub::RowActive()) {
    g_main_row = MainMenuRow();   // la fila real, si el menu la ha dado
    if (!g_tag_focus.load()) {
      if ((pressed & kDown) && g_main_row.load() == 6) {
        g_tag_focus = true;
        g_swallow |= kDown;
      } else if (pressed & kUD) {
        g_main_row = (g_main_row.load() + ((pressed & kDown) ? 1 : 6)) % 7;
        one_step();
      }
    } else if (pressed & kUp) {
      g_tag_focus = false;
      g_swallow |= kUp;
    } else if (pressed & kDown) {
      g_tag_focus = false;            // el juego da la vuelta a la primera fila
      g_main_row = 0;
      one_step();
    } else if (pressed & kA) {
      g_tag_focus = false;
      g_swallow |= kA;
      TagTeamOpen();
    } else if (pressed & kB) {
      g_tag_focus = false;
    } else if (pressed) {
      g_swallow |= pressed;
    }
    if (g_tag_focus.load()) return 0;
    return out();
  }
  if (screen != kMainMenuScreen) g_tag_focus = false;
  const bool active = screen == kDuelMenuScreen && !g_wanted.load() && REXCVAR_GET(dbz3_online_enabled) &&
                      Session::Get().link() == Link::kIdle;
  if (!active) {
    g_online_focus = false;
    if (screen == kDuelMenuScreen && (pressed & kUD)) {
      g_duel_row = (row + ((pressed & kDown) ? 1 : 3)) % 4;
      one_step();
      return out();
    }
    return uint16_t(real & ~g_swallow);
  }
  if (!g_online_focus.load()) {
    if ((pressed & kDown) && row == 3) {
      g_online_focus = true;          // el juego no lo ve: se queda en COM VS COM
      g_swallow |= kDown;
    } else if (pressed & kUD) {
      row = (row + ((pressed & kDown) ? 1 : 3)) % 4;   // 1P VS 2P sin mando 2 sale gris pero se pisa
      one_step();
    }
  } else if (pressed & kUp) {
    g_online_focus = false;           // vuelve a COM VS COM (el juego no lo ve)
    g_swallow |= kUp;
  } else if (pressed & kDown) {
    g_online_focus = false;           // el juego da la vuelta a la primera fila
    row = 0;
    one_step();
  } else if (pressed & kA) {
    g_wanted = true;                  // pantalla ONLINE
    g_swallow |= kA;
  } else if (pressed & (kB | kY)) {
    g_online_focus = false;           // atras: lo hace el juego
  } else if (pressed) {
    g_swallow |= pressed;
  }
  g_duel_row = row;
  if (g_online_focus.load()) return 0;
  return out();
}

void OnlineTick(uint8_t* base, uint16_t p1_buttons) {
  dbz3::campaigns::Trace(base);
  auto& s = Session::Get();
  constexpr uint16_t kX = 0x4000;
  const int screen = base[dbz3::GuestAddr(0x824BA110, 0x824BA100)];
  {
    const uint32_t bits = REX_LOAD_U32(kGameFade);
    float f;
    std::memcpy(&f, &bits, 4);
    g_game_fade = std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : 0.0f;
  }
  dbz3::tag::OnScreen(screen);
  if (screen == kMainMenuScreen) dbz3::campaigns::ForgetStoryBattle();
  if (screen != kDuelMenuScreen) dbz3_select_reset_flow();
  const bool battle = dbz3::tag::InBattle();
  if (screen != g_screen.load()) {          // llegada a otro menu: sale de negro
    g_enter_ms = NowMs();
    g_enter_delay_ms = screen == kDuelMenuScreen ? 900 : 0;
    g_leave_ms = 0;
  }
  // HUB > Online: la pantalla ONLINE se abre cuando el Duelo ya se ve (antes salia en cuanto cambiaba el byte,
  // con la pantalla aun en negro)
  if (g_open_at_duel.load() && screen == kDuelMenuScreen && !dbz3_select_live() && !dbz3_select_to_stage() &&
      MenuFade() >= 1.0f) {
    g_open_at_duel = false;
    g_wanted = true;
  }
  // el juego sale del menu: A/B sin nuestra fila elegida y, en el Duelo, tambien Y (atras; antes no contaba y la
  // fila seguia entera hasta el negro). Con ONLINE elegida, B/Y solo la sueltan (medido: el Duelo se queda).
  {
    const uint16_t newly = p1_buttons & ~g_prev_select;
    constexpr uint16_t kA = 0x1000, kB = 0x2000, kY = 0x8000;
    const bool leaves = (screen == kMainMenuScreen && !g_tag_focus.load() && (newly & (kA | kB))) ||
                        (screen == kDuelMenuScreen && !g_online_focus.load() && (newly & (kA | kB | kY)));
    if (leaves && !g_leave_ms.load()) g_leave_ms = NowMs();
  }
  if (g_leave_ms.load() && NowMs() - g_leave_ms.load() > 2500) {   // no salio (opcion bloqueada): vuelve
    g_enter_ms = NowMs();
    g_leave_ms = 0;
  }
  g_screen = screen;
  g_battle = battle;
  if ((p1_buttons & kX) && !(g_prev_select & kX) && !battle && s.link() != Link::kPlaying) {
    if (screen == kMainMenuScreen && REXCVAR_GET(dbz3_tag_team_enabled)) {
      TagTeamOpen();
    }
  }
  g_prev_select = p1_buttons;
  // anfitrion: foto del estado de menu (desbloqueos, ultima eleccion) que viaja al empezar
  if (s.role() == dbz3::net::Role::kHost && (s.link() == Link::kConnected || s.link() == Link::kStarting)) {
    s.SetUiSnapshot(base + dbz3::GuestAddr(Session::kUiSnap, Session::kUiSnapEu), Session::kUiSnapSize);
  }
}

OnlinePanel::OnlinePanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

OnlinePanel::~OnlinePanel() {
  if (blocked_) {
    if (auto* in = Input()) in->RemoveUIInputBlocker();
  }
}

uint16_t OnlinePanel::Pad() {
  if (!g_pad_pending.exchange(true)) {
    dbz3::launcher::DeferUi([]() {
      uint16_t b = 0;
      if (auto* in = Input()) {
        rex::input::X_INPUT_STATE st = {};
        if (in->GetStateForUI(0, &st) == 0) {
          b = static_cast<uint16_t>(st.gamepad.buttons);
          const int lx = int16_t(st.gamepad.thumb_lx), ly = int16_t(st.gamepad.thumb_ly);   // la palanca tambien
          if (ly > 16000) b |= 0x0001;
          if (ly < -16000) b |= 0x0002;
          if (lx < -16000) b |= 0x0004;
          if (lx > 16000) b |= 0x0008;
        }
      }
      g_pad = b;
      g_pad_pending = false;
    });
  }
  const uint16_t b = g_pad.load();
  const uint16_t pressed = b & ~last_buttons_;
  last_buttons_ = b;
  return pressed;
}

void OnlinePanel::OnDraw(ImGuiIO& io) {
  if (dbz3::GameDimmed()) return;   // fuera de la ventana: solo la pantalla oscura del juego
  native_ui::Prefetch();   // piezas del juego para las pantallas propias (una vez, en su hilo)
  if (static bool icon = false; !icon && native_ui::Ready()) {   // icono de la ventana: la bola del juego
    icon = true;
    if (native_ui::Image im; native_ui::Take("icono", im)) dbz3::SetWindowIconRgba(im.rgba.data(), im.w, im.h);
  }
  auto& s = Session::Get();
  VanillaLoadPending();
  // PRUEBAS (DBZ3_VANILLA_TEST=accept|decline, con DBZ3_NET_AUTO): sin pulsar nada en la pantalla ONLINE, "accept" acepta jugar
  // vanilla cuando los mods difieren y, con los dos de acuerdo, reinicia como la pantalla; "decline" no hace nada.
  if (static const char* vt = std::getenv("DBZ3_VANILLA_TEST");
      vt && (std::strcmp(vt, "accept") == 0 || std::strcmp(vt, "accept_ui") == 0)) {
    static double agreed = 0;
    if (s.link() == Link::kConnected && !s.mods_match() && !s.MyVanilla()) s.OfferVanilla();
    if (s.link() == Link::kConnected && s.VanillaAgreed()) {
      if (agreed == 0) agreed = ImGui::GetTime();
      else if (ImGui::GetTime() - agreed > 1.5 && !dbz3::VanillaSession()) {
        if (std::strcmp(vt, "accept_ui") == 0) {   // prueba del camino real: fichero temporal + reconexion de la pantalla (sin RbAuto)
          const char* pt = std::getenv("DBZ3_NET_PORT");
          const bool host = s.role() == dbz3::net::Role::kHost;
          const std::string pend = std::string(host ? "host" : "guest") + "\n" + (pt ? pt : "7777") + "\n0\n" + (host ? "" : "127.0.0.1") + "\n";
#ifdef _WIN32
          SetEnvironmentVariableA("DBZ3_NET_AUTO", nullptr);
#endif
          dbz3::RelaunchVanilla(pend, true);
        } else {
          dbz3::RelaunchVanilla("", true);
        }
      }
    }
  }
  if (g_vn.active && !g_wanted.load() && s.link() == Link::kIdle &&
      (g_screen.load() == kMainMenuScreen || g_screen.load() == kDuelMenuScreen))
    g_wanted = true;   // ---- VANILLA (V): reconexion tras reiniciar sin mods
  // salir pedido desde la pantalla: lo ejecuta este hilo (dueno del socket salvo en partida); en
  // partida lo cierra el hilo del juego (RunTick), que es el que usa el socket
  if (g_close_req.exchange(false)) {
    if (s.link() == Link::kPlaying) {
      s.RequestLeave();
    } else {
      s.Close();
      REXLOG_INFO("dbz3 online: sesion cerrada por el jugador");
    }
  }
  if (s.link() != Link::kIdle && s.link() != Link::kPlaying) s.Poll();
  const bool show = g_wanted.load() && s.link() != Link::kPlaying;
  auto* in = Input();
  if (show != blocked_ && in) {           // mientras se ve, el juego no recibe los mandos
    if (show) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
    blocked_ = show;
  }
  if (show && !g_open.load()) {   // la A que abre la pantalla no cuenta (el mando llega un poco tarde)
    wait_release_ = true;
    opened_at_ = ImGui::GetTime();
    REXLOG_INFO("dbz3 online: se abre la pantalla ONLINE");
  }
  g_open = show;
  if (s.link() == Link::kPlaying) g_wanted = false;
  // conexion perdida o error: la pantalla vuelve sola con el motivo; si pasa en pleno combate, al acabarlo
  // (antes solo se miraba el cuadro del cambio: en combate no se abria y despues tampoco)
  const Link now = s.link();
  const bool broken = now == Link::kLost || now == Link::kError;
  if (int(now) != last_link_ && broken) lost_notice_ = true;
  if (!broken) lost_notice_ = false;
  if (lost_notice_ && !g_battle.load()) {
    s.ClearCutFight();
    g_wanted = true;
    lost_notice_ = false;
  }
  last_link_ = int(now);
  if (dbz3::OverlayOpen()) return;   // launcher/menu rapido encima: la red sigue, sin pintar nada
  if (!show) {
    // aviso en la partida (el juego no tiene pantalla para esto). Antes miraba desynced() con el enlace en
    // kPlaying, que no se da nunca (Fail lo pasa a error y borra la marca): un corte en combate no avisaba
    if (lost_notice_ && g_battle.load()) {
      // en el area del juego y con la letra escalada; el texto largo se envuelve en vez de salirse
      const dbz3::ingame::style::Area ar = dbz3::ingame::style::GameArea(io);
      ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), 22.0f * ar.s, ar.At(30, 24), IM_COL32(255, 70, 70, 255),
                                              T::T("ONLINE: se corto la partida con el otro PC. Al acabar el combate veras el motivo.",
                                                   "ONLINE: the match with the other PC was cut. You will see why when the fight ends."),
                                              nullptr, 900.0f * ar.s);
    }
    editing_ = false;
    if (s.link() == Link::kPlaying) NetBadge(io, s);   // retardo y ping, en menus y en combate
    // entradas propias en los menus del juego: rotulo con el boton X, como los avisos del juego
    const int screen = g_screen.load();
    // jugando online, en el menu de Duelo: X sale del online (lo lee este PC, no el lockstep)
    if (s.link() == Link::kPlaying && !g_battle.load() && g_screen.load() == kDuelMenuScreen &&
        !dbz3_select_live() && !dbz3_select_to_stage()) {
      EntryBadge(io, T::T("SALIR DEL ONLINE", "LEAVE ONLINE"));
      if (Pad() & 0x4000) g_close_req = true;
    }
    if (!g_battle.load() && !TagTeamWanted() && !TagTeamGate() && s.link() == Link::kIdle) {
      if (screen == kMainMenuScreen && REXCVAR_GET(dbz3_tag_team_enabled) && !dbz3::hub::RowActive()) {
        TagTeamRow(io, g_tag_focus.load());   // US y EU: los menus son iguales
      }
      if (screen == kDuelMenuScreen && REXCVAR_GET(dbz3_online_enabled) && !dbz3_select_live() && !dbz3_select_to_stage()) {
        OnlineRow(io, g_online_focus.load());
      }
    }
    return;
  }
  uint16_t pressed = Pad();
  if (wait_release_) {
    if (g_pad.load() == 0 && ImGui::GetTime() - opened_at_ > 0.3) wait_release_ = false;
    pressed = 0;
  }
  // entra fundida en 0,3 s, como las listas del juego (antes aparecia de golpe)
  auto* bg = ImGui::GetBackgroundDrawList();
  auto* fg = ImGui::GetForegroundDrawList();
  const int v0 = bg->VtxBuffer.Size, f0 = fg->VtxBuffer.Size;
  DrawScreen(io, editing_ ? 0 : pressed);   // escribiendo la IP, el teclado no mueve el menu
  const float fade_in = std::clamp(float(ImGui::GetTime() - opened_at_) / 0.3f, 0.0f, 1.0f);
  FadeFrom(bg, v0, fade_in);
  FadeFrom(fg, f0, fade_in);
}

// Pantalla con el aspecto del Duelo: fondo de Kame House, cinta "Online", marco con filas y
// el Kaio del Norte (sacado del juego) que explica cada fila y el estado de la conexion.
void OnlinePanel::DrawScreen(ImGuiIO& io, uint16_t pressed) {
  namespace S = dbz3::ingame::style;
  constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kA = 0x1000, kB = 0x2000,
                     kY = 0x8000;
  auto& s = Session::Get();
  const Link link = s.link();
  const bool idle = link == Link::kIdle || link == Link::kError || link == Link::kLost;
  const bool host = s.role() == dbz3::net::Role::kHost;
  if (idle) synced_ = sync_failed_ = false;
  // Admision: los dos PCs tienen que llevar el mismo programa (exe + runtime + GPU); los mods se
  // comparan aparte en el HELLO. 1 contra 1: Tag Team online sigue sin autorizar (falta el relevo guest).
  std::string admission_reason;
  auto admission_ready = [&admission_reason]() {
    const std::filesystem::path exe = rex::filesystem::GetExecutablePath();
    std::vector<std::filesystem::path> files = {exe};
#ifdef _WIN32
    files.push_back(exe.parent_path() / "rexruntime.dll");
    files.push_back(exe.parent_path() / "rexgpu-xenos.dll");
#endif
    std::string acc;
    for (const auto& p : files) {
      const std::string h = rex::hash_file(p);
      if (h.empty()) {
        admission_reason = T::T("Online: no se pudo verificar el programa: ", "Online: could not verify the program: ") +
                           rex::path_to_utf8(p.filename());
        return false;
      }
      acc += h;
    }
    Session::Get().Close();   // tras un error/perdida la sesion vuelve a reposo antes de fijar identidad
    if (!Session::Get().ConfigureIdentity(rex::hash_bytes(acc), true, false)) {
      admission_reason = T::T("Online: cierra la sesion anterior y vuelve a intentarlo.",
                              "Online: close the previous session and try again.");
      return false;
    }
    // rango propio para la sala (ficha 01): el del ranking local (combates.csv); sin combates, sin rango
    int elo = 1000, tramo = -1;
#ifndef DBZ3_EU_VARIANT
    if (!dbz3::modos::PerfilJugador(&elo, &tramo, nullptr)) elo = 1000, tramo = -1;
#endif
    Session::Get().SetProfile(elo, tramo);
    REXLOG_INFO("dbz3 online: rango propio para la sala: elo {}, tramo {}", elo, tramo);
    return true;
  };

  // ---- VANILLA (V): reconexion automatica tras reiniciar (o esperar al otro, que se reinicia)
  if (g_vn.active) {
    const double now = ImGui::GetTime();
    if (g_vn.deadline == 0) g_vn.deadline = now + 180.0;   // desde que se ve la pantalla: da tiempo a que el otro PC arranque
    const bool up = link == Link::kConnected || link == Link::kStarting;
    if (g_vn.need_drop && !up) g_vn.need_drop = false;   // el otro ya se fue: ahora si se reconecta
    if (up && !g_vn.need_drop) {
      g_vn.active = false;
      REXLOG_INFO("dbz3 online: sesion vanilla reconectada");
    } else if (now > g_vn.deadline) {
      g_vn.active = false;
      last_err_ = T::T("No se pudo reconectar tras reiniciar sin mods. Vuelve a crear/unirte a la partida.",
                       "Could not reconnect after restarting without mods. Host/join the match again.");
      REXLOG_WARN("dbz3 online: sesion vanilla: sin reconexion");
      if (link != Link::kIdle) g_close_req = true;
    } else if (idle && !g_vn.need_drop && now >= g_vn.next_try) {
      g_vn.next_try = now + 2.0;
      std::string err;
      if (admission_ready()) {
        const bool ok = g_vn.host ? s.Host(uint16_t(g_vn.port), uint8_t(g_vn.delay), &err)
                                  : s.Join(g_vn.target, uint16_t(g_vn.port), &err);
        REXLOG_INFO("dbz3 online: sesion vanilla: {} {}", g_vn.host ? "crear" : "unirse", ok ? "en marcha" : err);
      }
    }
  }
  // ---- VANILLA (V): los dos aceptaron "jugar vanilla": tras 1,5 s (el otro recibe mi oferta repetida) cada PC se reinicia
  // sin mods y se reconecta solo. Si este PC ya esta en vanilla, espera a que el otro se reinicie.
  if (link == Link::kConnected && s.VanillaAgreed()) {
    const double now = ImGui::GetTime();
    if (g_vn.agreed_at == 0) {
      g_vn.agreed_at = now;
    } else if (now - g_vn.agreed_at > 1.5) {
      g_vn.agreed_at = 0;
      std::string pend = std::string(host ? "host" : "guest") + "\n" + std::to_string(port_) + "\n" +
                         std::to_string(delay_) + "\n" + (host ? "" : std::string(ip_)) + "\n";
      if (dbz3::VanillaSession()) {
        g_vn = {true, host, true, true, port_, delay_, host ? "" : std::string(ip_), 0, 0, 0};
      } else if (!dbz3::RelaunchVanilla(pend, true)) {
        last_err_ = T::T("No se pudo reiniciar el juego sin mods.", "Could not restart the game without mods.");
        g_close_req = true;
      }
    }
  }

  // Mods distintos: que falta en cada PC y que se puede bajar de Nova's Mods (carpeta = id del catalogo)
  using Delta = dbz3::net::Session::ModDelta;
  std::vector<Delta> diff;
  const bool diff_ready = link == Link::kConnected && !s.mods_match() && s.ModDiff(diff);
  std::vector<dbz3::launcher::NovaMod> installable;
  if (diff_ready) {
    const bool need = std::any_of(diff.begin(), diff.end(), [](const Delta& d) { return d.kind != Delta::kExtra; });
    if (need) dbz3::launcher::StartNovaCatalog();
    for (const auto& m : dbz3::launcher::NovaCatalog()) {
      for (const auto& d : diff) {
        if (d.kind != Delta::kExtra && d.name == m.id) installable.push_back(m);
      }
    }
  }
  const bool installing = std::any_of(installable.begin(), installable.end(),
                                      [](const auto& m) { return dbz3::launcher::NovaBusy(m.id); });
  // terminado (una vez, al acabar las descargas: leer los manifest cada cuadro iba al disco): si alguno no
  // quedo con la version del catalogo (descarga fallida), se puede reintentar
  if (synced_ && !installing && was_installing_ &&
      !std::all_of(installable.begin(), installable.end(), [](const auto& m) {
        const std::string v = dbz3::launcher::NovaInstalledVersion(m.id);   // "?" = instalado sin version en su manifest
        return v == m.version || v == "?";
      })) {
    synced_ = false;
    sync_failed_ = true;
  }
  was_installing_ = installing;

  // filas segun el estado
  enum Row { kHostRow, kJoinRow, kIpRow, kPortRow, kDelayRow, kStartRow, kSyncRow, kVanillaRow, kCancelRow };
  std::vector<Row> rows;
  if (idle) {
    rows = {kHostRow, kJoinRow, kIpRow, kPortRow, kDelayRow};
  } else {
    if (host) rows.push_back(kStartRow);
    if (!installable.empty()) rows.push_back(kSyncRow);
    if (link == Link::kConnected && !s.mods_match()) rows.push_back(kVanillaRow);   // ---- VANILLA (V)
    rows.push_back(kCancelRow);
  }
  if (!ip_[0] && !ip_loaded_) {   // la de la ultima vez
    std::snprintf(ip_, sizeof(ip_), "%s", REXCVAR_GET(dbz3_net_last_ip).c_str());
    ip_loaded_ = true;
  }
  const int n = int(rows.size());
  row_ = std::clamp(row_, 0, n - 1);
  if (pressed & kUp) row_ = (row_ + n - 1) % n;
  if (pressed & kDown) row_ = (row_ + 1) % n;
  const Row cur = rows[size_t(row_)];
  const int step = (pressed & kRight) ? 1 : (pressed & kLeft) ? -1 : 0;
  if (cur == kPortRow && step) port_ = std::clamp(port_ + step, 1024, 65535);
  if (cur == kDelayRow && step) delay_ = std::clamp(delay_ + step, 0, 8);   // 0 = AUTO
  const bool can_start = link == Link::kConnected && s.mods_match();
  if (pressed & kA) {
    last_err_.clear();   // una accion nueva borra el error anterior
    switch (cur) {
      case kHostRow:
        if (!admission_ready()) { last_err_ = admission_reason; break; }
        if (s.Host(uint16_t(port_), uint8_t(delay_), &last_err_))
          REXLOG_INFO("dbz3 online: partida creada en el puerto {}", port_);
        else
          REXLOG_WARN("dbz3 online: no se pudo crear la partida: {}", last_err_);
        row_ = 0;
        break;
      case kJoinRow:
        if (!admission_ready()) { last_err_ = admission_reason; break; }
        if (ip_[0]) {
          const bool joined = s.Join(ip_, uint16_t(port_), &last_err_);
          if (!joined) REXLOG_WARN("dbz3 online: no se pudo unir a la partida: {}", last_err_);
          if (joined && REXCVAR_GET(dbz3_net_last_ip) != ip_) {
            rex::cvar::SetFlagByName("dbz3_net_last_ip", ip_);
            dbz3::settings::SaveUserSettings();
          }
          row_ = 0;
        } else {
          row_ = 2;
          editing_ = true;   // sin IP: a escribirla
        }
        break;
      case kIpRow: editing_ = true; break;
      case kStartRow:
        if (can_start) s.RequestStart();
        break;
      case kSyncRow:
        if (installing || synced_) break;   // fila en gris: no se vuelve a instalar (moveria el mod en uso)
        for (const auto& m : installable) dbz3::launcher::InstallNovaMod(m);   // cada uno en su hilo
        synced_ = true;
        sync_failed_ = false;
        was_installing_ = true;   // aunque la descarga acabe antes del siguiente cuadro, se comprueba
        break;
      case kVanillaRow: if (!s.MyVanilla()) s.OfferVanilla(); break;   // ---- VANILLA (V): yo acepto
      case kCancelRow: g_close_req = true; break;
      default: break;
    }
  }
  if (pressed & 0x4000) {   // X: pegar el codigo del portapapeles (sin teclado) o, si eres el anfitrion, copiarlo otra vez
    if (idle && !editing_) {
      const char* clip = ImGui::GetClipboardText();
      std::string c;
      for (const char* q = clip ? clip : ""; *q && c.size() < sizeof(ip_) - 1; ++q)
        if (!std::isspace((unsigned char)*q)) c += char(std::toupper((unsigned char)*q));
      if (!c.empty()) {
        std::snprintf(ip_, sizeof(ip_), "%s", c.c_str());
        last_err_.clear();
        for (int k = 0; k < n; ++k) if (rows[size_t(k)] == kJoinRow) row_ = k;   // el cursor pasa a UNIRSE
      } else {
        last_err_ = T::T("No hay ningun codigo copiado. Copia el codigo que te paso el otro jugador y pulsa X.",
                         "No code copied. Copy the code the other player gave you, then press X.");
      }
    } else if (!idle && host && !std::getenv("DBZ3_NO_DIALOGS")) {
      const std::string code = s.RoomCode(nullptr);
      if (!code.empty()) ImGui::SetClipboardText(code.c_str());
    }
  }
  if (pressed & (kB | kY)) {
    if (idle) {
      last_err_.clear();
      g_wanted = false;   // cerrar (una sesion perdida vuelve a quedar libre)
      if (link != Link::kIdle) g_close_req = true;
    } else {
      g_close_req = true;   // B con una partida a medias: la cancela
    }
  }

  const S::Area a = S::GameArea(io);
  auto* dl = ImGui::GetBackgroundDrawList();
  const float t = float(ImGui::GetTime());
  // como el Duelo, del que sale: su fondo, el Kaio del Norte donde esta alli y las tiras doradas encima
  S::Backdrop(dl, a, "fondo_duelo");
  S::Host(dl, a, "anfitrion_online", 781, 336, false);
  S::Sides(dl, a);
  S::Banner(dl, a, "Online", "titulo_online");
  StepsBox(dl, a, s, host);
  constexpr float kStep = 72;
  const ImVec2 f0 = a.At(330, 175), f1 = a.At(870, 175 + kStep * n + 20);
  S::Frame(dl, f0, f1, a.s);
  if (!idle) RoomRows(dl, a, 175 + kStep * n + 44, n >= 3 ? 2 : 3, s, t);   // sala: rango y reglas de los dos
  char buf[96];
  for (int i = 0; i < n; ++i) {
    const ImVec2 r0(f0.x, f0.y + (10 + kStep * i) * a.s), r1(f1.x, r0.y + (kStep - 2) * a.s);
    const ImVec2 c((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    const bool sel = i == row_;
    if (sel) S::Highlight(dl, r0, r1, t);
    const char* label = "";
    bool disabled = false;
    switch (rows[size_t(i)]) {
      case kHostRow: label = T::T("CREAR PARTIDA", "HOST A MATCH"); break;
      case kJoinRow:
        label = T::T("UNIRSE", "JOIN");
        disabled = !ip_[0];
        break;
      case kIpRow:
        std::snprintf(buf, sizeof(buf), T::T("CODIGO  %s", "CODE  %s"), ip_[0] ? ip_ : "- - -");
        label = buf;
        break;
      case kPortRow:
        std::snprintf(buf, sizeof(buf), T::T("PUERTO  %d", "PORT  %d"), port_);
        label = buf;
        break;
      case kDelayRow:
        if (delay_ == 0) std::snprintf(buf, sizeof(buf), "%s", T::T("RETARDO  AUTO", "DELAY  AUTO"));
        else std::snprintf(buf, sizeof(buf), T::T("RETARDO  %d", "DELAY  %d"), delay_);
        label = buf;
        break;
      case kStartRow:
        label = T::T("EMPEZAR", "START");
        disabled = !can_start;
        break;
      case kSyncRow:
        label = installing ? T::T("INSTALANDO...", "INSTALLING...")
                : synced_  ? T::T("INSTALADO", "INSTALLED")
                           : T::T("INSTALAR LO QUE FALTA", "INSTALL WHAT IS MISSING");
        disabled = installing || synced_;
        break;
      case kVanillaRow:   // ---- VANILLA (V)
        label = s.VanillaAgreed()  ? T::T("REINICIANDO SIN MODS...", "RESTARTING WITHOUT MODS...")
                : s.MyVanilla()    ? T::T("ESPERANDO AL OTRO...", "WAITING FOR THE OTHER...")
                : s.PeerOffersVanilla() ? T::T("ACEPTAR: JUGAR VANILLA", "ACCEPT: PLAY VANILLA")
                                        : T::T("JUGAR VANILLA (SIN MODS)", "PLAY VANILLA (NO MODS)");
        disabled = s.MyVanilla();
        break;
      case kCancelRow: label = T::T("CANCELAR", "CANCEL"); break;
    }
    if (rows[size_t(i)] == kIpRow && editing_) {
      // la IP se escribe con el teclado en la misma fila (Intro o Esc terminan)
      ImGui::SetNextWindowPos(ImVec2(r0.x + 40 * a.s, c.y - 26 * a.s), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(r1.x - r0.x - 80 * a.s, 0), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.0f);
      ImGui::Begin("##dbz3_online_ip", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
      ImFont* f = dbz3::launcher::ui::GetFonts().menu;
      if (f) ImGui::PushFont(f);
      ImGui::SetWindowFontScale(0.7f * a.s);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.02f, 0.10f, 0.25f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.01f, 0.82f, 0.90f, 1.0f));
      ImGui::SetNextItemWidth(-1);
      if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
      ImGui::InputTextWithHint("##ip", "XXXXX-XXXXX", ip_, sizeof(ip_), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CharsUppercase);
      if (ImGui::IsItemDeactivated()) editing_ = false;
      ImGui::PopStyleColor(2);
      if (f) ImGui::PopFont();
      ImGui::End();
    } else {
      const float ancho_vanilla = rows[size_t(i)] == kVanillaRow ? 500.0f * a.s : 0.0f;
      S::MenuText(dl, c, 50 * a.s, label, sel && !disabled, disabled, ancho_vanilla);
    }
    if (sel && (rows[size_t(i)] == kPortRow || rows[size_t(i)] == kDelayRow)) {
      S::Arrows(dl, ImVec2(r0.x + 30 * a.s, r0.y), ImVec2(r1.x - 30 * a.s, r1.y), 40 * a.s, t);
    }
  }

  // el anfitrion explica la fila elegida; con una partida en marcha, el estado
  std::string say;
  if (!last_err_.empty()) say = last_err_ + "\n";
  if (idle) {
    if (link != Link::kIdle) say += s.Status() + "\n";
    if (link == Link::kError && s.desynced())
      say += std::string(T::T("Los dos juegos dejaron de ver lo mismo. Que hacer: volved a crear la sala; si se repite, enviadnos el informe.",
                              "The two games stopped seeing the same thing. What to do: create the room again; if it repeats, send us the report.")) + "\n";
    else if (link == Link::kLost)
      say += std::string(T::T("Se perdio la conexion. Que hacer: mira que tu internet funciona y volved a crear la sala y unirte.",
                              "The connection was lost. What to do: check your internet, then create the room and join again.")) + "\n";
    else if (link == Link::kError)
      say += std::string(T::T("No se pudo conectar. Que hacer: comprueba que el codigo esta bien escrito y que el otro jugador ya tiene la sala creada.",
                              "Could not connect. What to do: check the code is typed right and the other player has already created the room.")) + "\n";
    if (link != Link::kError && link != Link::kLost) switch (cur) {   // con un error, solo el error y que hacer (cuadro corto)
      case kHostRow:
        say += T::T("Crea la partida: te dara un codigo de sala para pasarselo al otro jugador. Necesitais la misma version y los mismos mods.",
                    "Host the match: you get a room code to give to the other player. You both need the same version and mods.");
        break;
      case kJoinRow:
      case kIpRow:
        say += T::T("Escribe el codigo de sala del anfitrion (pulsa A y usa el teclado; tambien vale su IP) y unete.",
                    "Type the host's room code (press A and use the keyboard; their IP also works), then join.");
        break;
      case kPortRow:
        say += T::T("Puerto UDP. Al crear la partida el juego intenta abrirlo en tu router; si no puede, abrelo a mano.",
                    "UDP port. When you host, the game tries to open it on your router; if it can't, forward it by hand.");
        break;
      default:
        say += delay_ == 0
                   ? T::T("AUTO: el juego ajusta el retardo a la conexion, tambien en pleno combate. Lo decide quien crea la partida.",
                          "AUTO: the game adjusts the delay to the connection, even mid-fight. The host decides it.")
                   : T::T("Retardo fijo. Mas retardo, mas suave con mala conexion: 2-3 en casa, 4-6 por internet.",
                          "Fixed delay. More delay is smoother on a bad connection: 2-3 at home, 4-6 online.");
        break;
    }
  } else {
    say += s.Status();
    if (host) {
      // codigo de sala: se copia solo al portapapeles cada vez que cambia (red local -> publica)
      std::string how;
      const std::string code = s.RoomCode(&how);
      if (!code.empty() && code != copied_code_) {
        // pruebas automaticas (DBZ3_NO_DIALOGS): sin pisar el portapapeles del usuario
        if (!std::getenv("DBZ3_NO_DIALOGS")) ImGui::SetClipboardText(code.c_str());
        copied_code_ = code;
      }
      say += T::T("\nCodigo de sala: ", "\nRoom code: ") + code + T::T("  (copiado)", "  (copied)");
      if (link == Link::kWaiting && how != "local") {
        // el codigo lleva la IP publica: desde la misma casa muchos routers no dejan entrar por ella
        if (lan_.empty() || ImGui::GetTime() - lan_at_ > 5.0) {   // gethostname + DNS: no en cada fotograma
          lan_ = s.LocalAddresses();
          lan_ = lan_.substr(0, lan_.find(' '));
          lan_at_ = ImGui::GetTime();
        }
        if (!lan_.empty())
          say += T::T("\nDesde tu misma casa: ", "\nFrom the same house: ") + lan_;   // Join acepta IP (puerto: el de la fila)
      }
      if (link == Link::kConnected && !s.mods_match()) {
        // sin la nota de red: el bocadillo no da para ella y la lista de mods
      } else if (how == "router") {
        say += T::T("\nPuerto abierto en tu router: vale por internet.", "\nPort opened on your router: works over the internet.");
      } else if (how == "stun") {
        say += T::T("\nPor internet puede necesitar abrir el puerto UDP en el router.",
                    "\nOver the internet the UDP port may need to be forwarded on the router.");
      } else {
        say += T::T("\nCodigo de red local (buscando la IP publica...).", "\nLocal network code (looking for the public IP...).");
      }
    }
    if (link == Link::kConnected || link == Link::kStarting) {
      if (s.mods_match()) {
        say += host ? T::T("\nMods iguales. Pulsa EMPEZAR: los dos ireis a la eleccion de personajes (1P VS 2P).",
                           "\nSame mods. Press START: you both go to character select (1P VS 2P).")
                    : T::T("\nMods iguales. Esperando a que el anfitrion empiece...",
                           "\nSame mods. Waiting for the host to start...");
        int e0, t0, e1, t1;
        CombatRules r0, r1;
        s.MyRoom(&e0, &t0, &r0);
        if (s.PeerRoom(&e1, &t1, &r1) && (r0.any() || r1.any())) {   // reglas opcionales del combate (F1)
          say += r0 == r1 ? T::T("\nReglas de combate iguales en los dos: van activas.",
                                 "\nSame combat rules on both PCs: they are on.")
                          : T::T("\nReglas de combate distintas: se juega el combate original.",
                                 "\nDifferent combat rules: the original combat is played.");
        }
      } else if (!diff_ready) {
        say += T::T("\nMods distintos: comparando las listas...", "\nDifferent mods: comparing the lists...");
      } else {
        int shown = 0;
        for (const auto& d : diff) {
          if (shown++ == 2) { say += " (+" + std::to_string(diff.size() - 2) + ")"; break; }
          say += "\n" + std::string(d.kind == Delta::kMissing ? T::T("Te falta: ", "You are missing: ")
                                    : d.kind == Delta::kDifferent ? T::T("Version distinta: ", "Different version: ")
                                                                  : T::T("Al otro le falta: ", "The other player is missing: ")) + d.name;
        }
        // ---- VANILLA (V): salida cuando los mods no se pueden igualar
        if (s.VanillaAgreed())
          say += T::T("\nAceptado: reiniciando sin mods...",
                      "\nAccepted: restarting without mods...");
        else if (s.MyVanilla())
          say += T::T("\nHas aceptado vanilla. Esperando al otro...", "\nYou accepted vanilla. Waiting for the other...");
        else if (s.PeerOffersVanilla())
          say += T::T("\nEl otro propone vanilla (sin mods). Elige JUGAR VANILLA.",
                      "\nThe other offers vanilla (no mods). Choose PLAY VANILLA.");
        else
          say += T::T("\nO jugad vanilla (sin mods), si aceptais los dos.",
                      "\nOr play vanilla (no mods) if you both accept.");
        const std::string nova = dbz3::launcher::NovaStatus();
        if (synced_ && !installing) {
          say += T::T("\nCierra el juego, pulsa JUGAR y reconecta.",
                      "\nClose the game, press PLAY and reconnect.");
        } else if (!installable.empty()) {
          say += T::T("\nPulsa INSTALAR LO QUE FALTA.", "\nPress INSTALL WHAT IS MISSING.");
        } else if (std::all_of(diff.begin(), diff.end(), [](const Delta& d) { return d.kind == Delta::kExtra; })) {
          say += T::T("\nEl otro puede instalarlo, o desactivalo tu.",
                      "\nThe other can install it, or disable it yourself.");
        } else {
          say += T::T("\nActivad los mismos mods y reconectad.",
                      "\nEnable the same mods and reconnect.");
        }
        if ((installing || sync_failed_) && !nova.empty()) say += "\n" + nova;   // tambien el error de una descarga
      }
    }
  }
  S::Speech(dl, a.At(195, 615), a.At(950, 845), a.s, say.c_str(), 0.78f);
  std::vector<std::pair<const char*, const char*>> help = {{"A", T::T("Aceptar", "OK")},
                                                           {"B", idle ? T::T("Atras", "Back") : T::T("Cancelar", "Cancel")}};
  if (idle) help.push_back({"X", T::T("Pegar codigo", "Paste code")});
  else if (host) help.push_back({"X", T::T("Copiar codigo", "Copy code")});
  S::HelpLine(dl, a.At(idle || host ? 690 : 745, 870), 38 * a.s, help);
}

}  // namespace dbz3::ingame

// EU: sub_8218C5A0 (mismo cuerpo, mismos campos +48/+150)
DBZ3_HOOK(sub_8218C5D8, dbz3eu_sub_8218C5A0) {
  const uint32_t obj = REX_LOAD_U32(ctx.r3.u32 + 48);
  if (obj) {
    const int row = REX_LOAD_U8(obj + 150);
    if (dbz3::ingame::g_real_main_row.exchange(row) < 0) REXLOG_INFO("dbz3 menu: fila real del menu principal {}", row);
    dbz3::ingame::g_main_obj = obj;
    dbz3::ingame::g_real_main_at = dbz3::ingame::NowMs();
  }
  orig(ctx, base);
}
