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
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

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
#include "../launcher/mod_pipeline.h"
#include "../launcher/update_check.h"
#include "matchmaking.h"
#include "../launcher/novas_mods.h"
#include "../launcher/settings.h"
#include "../mods.h"
#include "../net/netplay.h"
#include "../launcher/ui_kit.h"
#include "game_style.h"
#include "native_ui.h"
#include "tagteam_panel.h"
#include "../tagteam.h"
#ifndef DBZ3_EU_VARIANT
#include "../modos.h"   // rango del ranking local (modos.cpp no entra en la build solo EU)
#endif

#include <rex/logging.h>

// La ultima IP a la que se unio el jugador: la pantalla ONLINE la trae escrita la proxima vez.
REXCVAR_DEFINE_STRING(dbz3_net_last_ip, "", "DBZ3/Online", "Last host IP joined from the ONLINE screen");
// Matchmaking sin servidor propio (ntfy.sh por defecto): aceptacion de privacidad, URL del servicio e interruptor.
REXCVAR_DEFINE_BOOL(dbz3_mm_consent, false, "DBZ3/Online", "Player accepted publishing the room code on the public matchmaking service");
REXCVAR_DEFINE_STRING(dbz3_mm_url, "https://ntfy.sh", "DBZ3/Online", "Matchmaking service URL (ntfy-compatible)");
REXCVAR_DEFINE_BOOL(dbz3_mm_enabled, true, "DBZ3/Online", "FIND A MATCH option on the ONLINE screen");
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
bool RowActive();
bool OwnBattle();
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);   // hub_menu.cpp: la fila HUB ocupa el sitio de la fila TAG TEAM (Tag Team sigue con X)
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

#include "matchmaking.inl"

// Pasos de la pantalla ONLINE (caja a la izquierda) y estado de la conexion con color y ping:
// 1 crear/unirse, 2 compartir el codigo, 3 esperando/conectando, 4 combate. El paso actual va iluminado.
void StepsBox(ImDrawList* dl, const dbz3::ingame::style::Area& a, const Session& s, bool host, int mode) {
  namespace S = dbz3::ingame::style;
  const Link link = s.link();
  int step = 1;   // paso en el que estas
  if (link == Link::kWaiting) step = host ? 1 : 2;
  else if (link == Link::kConnected) step = s.mods_match() ? 3 : 2;
  else if (link == Link::kStarting || link == Link::kPlaying) step = 3;
  const char* first = mode == 1 ? T::T("BUSCANDO RIVAL", "LOOKING FOR A RIVAL")
                      : mode == 3 ? T::T("LLAMANDO A LA SALA", "CALLING THE ROOM")
                                  : T::T("PASA EL CODIGO", "SHARE THE CODE");
  const char* names[3] = {first, T::T("CONECTANDO", "CONNECTING"), T::T("LISTO PARA LUCHAR", "READY TO FIGHT")};
  const ImVec2 p0 = a.At(14, 215), p1 = a.At(318, 215 + 46.0f * 3 + 94);
  S::Frame(dl, p0, p1, a.s * 0.7f);
  for (int i = 0; i < 3; ++i) {
    const float y = 215 + 24 + 46.0f * float(i);
    S::MenuText(dl, a.At(166, y), 28 * a.s, names[i], i + 1 == step, i + 1 < step, 270 * a.s);
  }
  const bool bad = link == Link::kError || link == Link::kLost;
  const ImU32 col = bad ? IM_COL32(230, 60, 60, 255)
                    : (link == Link::kConnected || link == Link::kStarting || link == Link::kPlaying) ? IM_COL32(70, 210, 90, 255)
                    : (link == Link::kWaiting || g_mm.Active()) ? IM_COL32(240, 190, 50, 255) : IM_COL32(150, 150, 150, 255);
  const char* txt = bad ? T::T("SIN CONEXION", "NO CONNECTION")
                    : (link == Link::kWaiting || g_mm.Active()) ? T::T("ESPERANDO", "WAITING")
                    : link == Link::kIdle ? T::T("DESCONECTADO", "OFFLINE") : T::T("CONECTADO", "CONNECTED");
  const float y = 215 + 46.0f * 3 + 36;
  dl->AddCircleFilled(a.At(44, y), 11 * a.s, col);
  dl->AddCircle(a.At(44, y), 11 * a.s, IM_COL32(30, 20, 10, 255), 0, 1.5f * a.s);
  S::MenuText(dl, a.At(176, y), 26 * a.s, txt, false, false, 220 * a.s);
  if (s.ping_ms() >= 0 && (link == Link::kConnected || link == Link::kStarting || link == Link::kPlaying)) {
    char pb[24];
    std::snprintf(pb, sizeof(pb), "PING %d ms", s.ping_ms());
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
  if (static const bool test_mm = std::getenv("DBZ3_TEST_MM") != nullptr; test_mm && !g_wanted.load() &&
      s.link() == Link::kIdle && (std::getenv("DBZ3_NET_RUEDA") ? (g_screen.load() == kDuelMenuScreen && DuelMenuRow() == 4) : g_screen.load() == kMainMenuScreen) && NowMs() > 4000)
    g_wanted = true;   // PRUEBAS (solo pruebas.py): la pantalla ONLINE se abre sola y busca
  if (static const char* tmm = std::getenv("DBZ3_TEST_MM"); tmm && std::strcmp(tmm, "pantalla") != 0) {   // PRUEBAS (solo pruebas.py)
    // En OnDraw (corre siempre) y no en OnlineTick: con la sesion activa el gancho de mandos no llama a OnlineTick (el combate pedido no salia nunca).
    static uint32_t last = 0;
    static bool fired = false;
    // DBZ3_TEST_MM_TEAM2 = "id": dos salas seguidas. Al acabar el combate 1 se cierra la sala, se pone ese equipo
    // (DBZ3_NET_TEAM) y se vuelve a buscar partida: la plaza de cada id logico ha de salir como en una sala nueva.
    static bool saw_fight = false, closing = false;
    static uint32_t room = 1;
    if (const char* t2 = std::getenv("DBZ3_TEST_MM_TEAM2"); t2 && room == 1) {
      if (dbz3::hub::OwnBattle()) saw_fight = true;
      if (saw_fight && !dbz3::hub::OwnBattle() && !closing) {   // el otro lado puede cerrar antes: aqui basta con que acabe el combate
        closing = true;
        REXLOG_INFO("dbz3 mm: sala 1 terminada: se cierra y se busca otra con el equipo {}", t2);
        if (s.link() != Link::kIdle) g_close_req = true;
      }
      if (closing && s.link() == Link::kIdle) {
#ifdef _WIN32
        _putenv_s("DBZ3_NET_TEAM", t2);
#else
        setenv("DBZ3_NET_TEAM", t2, 1);
#endif
        room = 2;
        fired = false;
        last = 0;
        test_mm_restart_ = true;
      }
    }
    if (s.active() && !fired && s.tick() > 150 && !std::getenv("DBZ3_NET_RUEDA")) {   // DBZ3_NET_RUEDA: sin combate pedido, la prueba recorre la rueda con teclas
      fired = true;
      const bool ok = dbz3::hub::RequestBattleEx(0, 7, 2, true, 15, 0, 0, 0, 0);
      REXLOG_INFO("dbz3 mm: combate pedido ({})", ok ? "ok" : "rechazado");
    }
    if (s.active() && s.tick() >= last + 300) {
      last = s.tick();
      REXLOG_INFO("dbz3 mm: tick {} sumas comparadas {} distintas {} desync {}", s.tick(), s.sums_compared(), s.sums_bad(), s.desynced());
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
      // DS138: antes iba en ar.At(30,24) con 900 u de ancho: en de/it (97-99 caracteres) ocupaba dos
      // lineas y tapaba el marcador nativo (y 50..160) y el reloj central. Ahora va ABAJO (base en y 846,
      // encima de la linea de reglas de y 868), centrado y con la letra mas pequena: por debajo de los
      // luchadores, como el resto de HUD propio (kHud*). Al perderse el enlace el NetBadge ya no se dibuja.
      const dbz3::ingame::style::Area ar = dbz3::ingame::style::GameArea(io);
      const char* msg = T::T("ONLINE: se corto la partida con el otro PC. Al acabar el combate veras el motivo.",
                             "ONLINE: the match with the other PC was cut. You will see why when the fight ends.");
      ImFont* font = ImGui::GetFont();
      const float fs = 18.0f * ar.s, wrap = 900.0f * ar.s;
      const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, wrap, msg);
      const ImVec2 c = ar.At(600, 846);
      ImGui::GetForegroundDrawList()->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y),
                                              IM_COL32(255, 70, 70, 255), msg, nullptr, wrap);
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
  auto admission_ready = [this, &admission_reason]() {
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
        admission_reason = T::T("Online: no se puede leer un archivo del juego. Prueba Reparar instalacion en el launcher: ", "Online: a game file cannot be read. Try Repair install in the launcher: ") +
                           rex::path_to_utf8(p.filename());
        return false;
      }
      acc += h;
    }
    Session::Get().Close();   // tras un error/perdida la sesion vuelve a reposo antes de fijar identidad
    g_build_digest = rex::hash_bytes(acc);
    if (!Session::Get().ConfigureIdentity(g_build_digest, true, false)) {
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
    // el modo elegido arriba decide el equipo que viaja (1 contra 1 = ninguno: un equipo de un Tag Team anterior no se cuela)
    if (tag_ && team_[0] >= 0 && team_[1] >= 0) dbz3::tag::SetMyOnlineTeam({team_[0], team_[1]});
    else if (!tag_) dbz3::tag::SetMyOnlineTeam({});
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

  // ---- Buscar partida: el matchmaking avanza aqui, un paso por cuadro (mode_ 1 buscar, 2 crear, 3 unirse con codigo)
  const std::function<bool()> admit = [&]() { return admission_ready(); };
  const bool test_mm = std::getenv("DBZ3_TEST_MM") != nullptr;   // PRUEBAS (solo pruebas.py): busca sin pulsar nada
  auto cancel_all = [&]() {
    g_mm.Cancel();
    mode_ = 0;
    auto_start_at_ = 0;
    g_close_req = true;
  };
  auto start_search = [&]() {
    last_err_.clear();
    if (!REXCVAR_GET(dbz3_mm_enabled)) { last_err_ = T::T("La busqueda de partida no esta disponible.", "Matchmaking is not available."); return; }
    if (!admission_ready()) { last_err_ = admission_reason; return; }
    mode_ = 1;
    g_mm.Start();
  };
  auto do_join = [&]() {
    if (!ip_[0]) return;
    if (!admission_ready()) { last_err_ = admission_reason; return; }
    const bool joined = s.Join(ip_, uint16_t(port_), &last_err_);
    if (!joined) {
      REXLOG_WARN("dbz3 online: no se pudo unir a la partida: {}", last_err_);
      err_detail_ = last_err_;
    }
    else mode_ = 3;
    if (joined && REXCVAR_GET(dbz3_net_last_ip) != ip_) {
      rex::cvar::SetFlagByName("dbz3_net_last_ip", ip_);
      dbz3::settings::SaveUserSettings();
    }
    row_ = 0;
  };
  if (join_after_edit_) {
    join_after_edit_ = false;
    do_join();
  }
  static bool port_env_done = false;
  if (test_mm && !port_env_done && std::strcmp(std::getenv("DBZ3_TEST_MM"), "pantalla") == 0 && (port_env_done = true))   // PRUEBAS: otro puerto en la pantalla
    if (const char* pt = std::getenv("DBZ3_TEST_MM_PORT")) port_ = std::atoi(pt);
  if (test_mm_restart_) { test_mm_restart_ = false; test_mm_started_ = false; }
  if (test_mm && std::strcmp(std::getenv("DBZ3_TEST_MM"), "pantalla") != 0 && mode_ == 0 && idle && !test_mm_started_) {
    test_mm_started_ = true;
    if (const char* pt = std::getenv("DBZ3_TEST_MM_PORT")) port_ = std::atoi(pt);   // dos copias en un PC: puertos distintos
    tag_ = std::getenv("DBZ3_NET_TEAM") != nullptr;   // la prueba salta los pasos de elegir modo y equipo: el equipo viene del entorno
    step_ = 2;
    start_search();
  }
  if (mode_ == 1) {
    g_mm.Tick(s, ImGui::GetTime(), port_, delay_, admit);
    if (g_mm.st == Mm::kFail) {
      last_err_ = g_mm.fail_msg;
      g_mm.st = Mm::kOff;
      mode_ = 0;
    }
  }
  const bool searching = mode_ == 1 && g_mm.Active();
  if (mode_ != 0 && link == Link::kIdle && !searching) mode_ = 0;
  if (idle && !searching && link != Link::kError && link != Link::kLost) auto_start_at_ = 0;

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
  enum Row { kSearchRow, kHostRow, kJoinRow, kAdvRow, kPortRow, kDelayRow, kStartRow, kSyncRow, kVanillaRow, kCancelRow, kConsentYes, kConsentNo,
             kModeSolo, kModeTag, kTeamRow, kCopyRow, kPickRow };
  std::vector<Row> rows;
  if (consent_ask_ && idle && !searching) {
    rows = {kConsentYes, kConsentNo};
  } else if (idle && !searching) {
    if (step_ == 0) {
      rows = {kModeSolo, kModeTag};
    } else if (step_ == 1) {
      rows = {kPickRow};
    } else {
      if (tag_) rows.push_back(kTeamRow);
      rows.insert(rows.end(), {kHostRow, kJoinRow, kSearchRow, kAdvRow});
      if (adv_) { rows.push_back(kPortRow); rows.push_back(kDelayRow); }
    }
  } else if (searching && link == Link::kIdle) {
    rows.push_back(kCancelRow);
  } else {
    if (host && mode_ != 1 && link == Link::kWaiting) rows.push_back(kCopyRow);
    if (host && mode_ != 1) rows.push_back(kStartRow);
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
  const S::Area a = S::GameArea(io);
  const float kStep = n >= 6 ? 58.0f : 72.0f;   // con la lista larga (equipo + avanzado) las filas se aprietan
  const bool picker = step_ == 1 && idle && !searching && !consent_ask_;
  const std::vector<int>& fighters = dbz3::ingame::TeamFighterIds();
  constexpr int kCols = 7;
  const int nf = int(fighters.size());
  // casilla i de la rejilla de luchadores (px de pantalla)
  auto cell_rect = [&](int i, ImVec2* c0, ImVec2* c1) {
    *c0 = a.At(341 + 74.0f * float(i % kCols), 232 + 50.0f * float(i / kCols));
    *c1 = a.At(341 + 74.0f * float(i % kCols) + 72, 232 + 50.0f * float(i / kCols) + 48);
  };
  // ratón: pasar por encima elige y un clic acepta (clic derecho = atras); el mando y el teclado siguen igual
  if (!editing_ && !wait_release_) {
    const bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (picker) {
      for (int i = 0; i < nf; ++i) {
        ImVec2 c0, c1;
        cell_rect(i, &c0, &c1);
        if (ImGui::IsMouseHoveringRect(c0, c1, false)) {
          if (moved || click) pick_cur_ = i;
          if (click) pressed |= kA;
        }
      }
    } else {
      const ImVec2 q0 = a.At(330, 175);
      for (int i = 0; i < n; ++i) {
        const ImVec2 r0(q0.x, q0.y + (10 + kStep * float(i)) * a.s), r1(a.At(870, 0).x, r0.y + (kStep - 2) * a.s);
        if (ImGui::IsMouseHoveringRect(r0, r1, false)) {
          if (moved || click) row_ = i;
          if (click) pressed |= kA;
        }
      }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pressed |= kB;
  }
  if (picker) {   // rejilla de luchadores: la cruceta mueve el cursor
    if (pressed & kLeft) pick_cur_ = (pick_cur_ + nf - 1) % nf;
    if (pressed & kRight) pick_cur_ = (pick_cur_ + 1) % nf;
    if (pressed & kUp) pick_cur_ = pick_cur_ >= kCols ? pick_cur_ - kCols : std::min(nf - 1, pick_cur_ + ((nf - 1) / kCols) * kCols);
    if (pressed & kDown) pick_cur_ = pick_cur_ + kCols < nf ? pick_cur_ + kCols : pick_cur_ % kCols;
    pick_cur_ = std::clamp(pick_cur_, 0, nf - 1);
    pressed &= uint16_t(~(kUp | kDown));
  }
  if (pressed & kUp) row_ = (row_ + n - 1) % n;
  if (pressed & kDown) row_ = (row_ + 1) % n;
  const Row cur = rows[size_t(row_)];
  const int step = (pressed & kRight) ? 1 : (pressed & kLeft) ? -1 : 0;
  if (cur == kPortRow && step) port_ = std::clamp(port_ + step, 1024, 65535);
  if (cur == kDelayRow && step) delay_ = std::clamp(delay_ + step, 0, 8);   // 0 = AUTO
  const bool can_start = link == Link::kConnected && s.mods_match();
  if (mode_ == 1 && host && can_start) {   // Buscar partida: sin boton, empieza solo a los 2 s de verse los dos
    if (auto_start_at_ == 0) auto_start_at_ = ImGui::GetTime();
    else if (ImGui::GetTime() - auto_start_at_ > 2.0) s.RequestStart();
  }
  if (pressed & kA) {
    last_err_.clear();   // una accion nueva borra el error anterior
    switch (cur) {
      case kSearchRow:
        if (!REXCVAR_GET(dbz3_mm_consent)) { consent_ask_ = true; row_ = 0; }
        else start_search();
        break;
      case kConsentYes:
        rex::cvar::SetFlagByName("dbz3_mm_consent", "true");
        dbz3::settings::SaveUserSettings();
        consent_ask_ = false;
        start_search();
        break;
      case kConsentNo: consent_ask_ = false; row_ = 0; break;
      case kModeSolo: tag_ = false; step_ = 2; row_ = 0; break;
      case kModeTag: {
        tag_ = true;
        if (team_[0] < 0 || team_[1] < 0) {   // el ultimo equipo del Tag Team de 1P, si lo hay
          const std::vector<int> last = dbz3::tag::MyLastTeam();
          if (last.size() == 2) { team_[0] = last[0]; team_[1] = last[1]; }
        }
        if (team_[0] >= 0 && team_[1] >= 0) { step_ = 2; row_ = 0; }
        else { step_ = 1; pick_n_ = 0; pick_cur_ = 0; row_ = 0; }
        break;
      }
      case kTeamRow: step_ = 1; pick_n_ = 0; team_[0] = team_[1] = -1; row_ = 0; break;
      case kPickRow: {
        const int id = fighters[size_t(pick_cur_)];
        if (pick_n_ == 1 && team_[0] == id) {
          last_err_ = T::T("Ese luchador ya esta en tu equipo. Elige otro.", "That fighter is already on your team. Pick another one.");
          break;
        }
        team_[pick_n_++] = id;
        if (pick_n_ >= 2) { step_ = 2; row_ = 0; }
        break;
      }
      case kCopyRow: {
        const std::string code = s.RoomCode(nullptr);
        if (!code.empty() && !std::getenv("DBZ3_NO_DIALOGS")) ImGui::SetClipboardText(code.c_str());
        copied_at_ = ImGui::GetTime();
        break;
      }
      case kHostRow:
        if (!admission_ready()) { last_err_ = admission_reason; break; }
        if (s.Host(uint16_t(port_), uint8_t(delay_), &last_err_)) {
          mode_ = 2;
          REXLOG_INFO("dbz3 online: partida creada en el puerto {}", port_);
        } else {
          REXLOG_WARN("dbz3 online: no se pudo crear la partida: {}", last_err_);
          err_detail_ = last_err_;
        }
        row_ = 0;
        break;
      case kJoinRow: editing_ = true; break;   // escribe el codigo; Intro se une
      case kAdvRow: adv_ = !adv_; break;
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
      case kCancelRow: cancel_all(); break;
      default: break;
    }
  }
  if (pressed & 0x4000) {   // X: pegar el codigo del portapapeles (sin teclado) o, si eres el anfitrion, copiarlo otra vez
    if (idle && !editing_ && step_ == 2) {
      const char* clip = ImGui::GetClipboardText();
      std::string c;
      for (const char* q = clip ? clip : ""; *q && c.size() < sizeof(ip_) - 1; ++q)
        if (!std::isspace((unsigned char)*q)) c += char(std::toupper((unsigned char)*q));
      if (!c.empty()) {
        std::snprintf(ip_, sizeof(ip_), "%s", c.c_str());
        last_err_.clear();
        do_join();   // pegar el codigo y entrar, en un solo paso
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
    if (consent_ask_) {
      consent_ask_ = false;
    } else if (searching || mode_ == 1) {
      cancel_all();
    } else if (idle && step_ == 1) {
      last_err_.clear();
      if (pick_n_ > 0) { team_[--pick_n_] = -1; } else { step_ = 0; row_ = 1; }
    } else if (idle && step_ == 2) {
      last_err_.clear();
      step_ = 0;
      row_ = tag_ ? 1 : 0;
    } else if (idle) {
      last_err_.clear();
      g_wanted = false;   // cerrar (una sesion perdida vuelve a quedar libre)
      if (link != Link::kIdle) g_close_req = true;
    } else {
      g_close_req = true;   // B con una partida a medias: la cancela
    }
  }

  auto* dl = ImGui::GetBackgroundDrawList();
  const float t = float(ImGui::GetTime());
  // Torneo del Otro Mundo y Paikuhan (pack Dragon HUB, arte del autor); sin el pack, como el Duelo, del que sale:
  // su fondo y el Kaio del Norte donde esta alli. Las tiras doradas, encima
  S::Backdrop(dl, a, S::Img("hub/fondo_online") ? "hub/fondo_online" : "fondo_duelo");
  S::Host(dl, a, S::Img("hub/anfitrion_paikuhan") ? "hub/anfitrion_paikuhan" : "anfitrion_online", 781, 336, false);
  S::Sides(dl, a);
  S::Banner(dl, a, "Online", "titulo_online");
  if (mode_ != 0 || link != Link::kIdle) StepsBox(dl, a, s, host, mode_);   // en el menu inicial aun no hay pasos que ensenar
  {   // etiqueta beta (el online siempre se presenta como beta)
    const ImVec2 b0 = a.At(1030, 128), b1 = a.At(1180, 176);
    S::Frame(dl, b0, b1, a.s * 0.6f);
    S::MenuText(dl, ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), 30 * a.s, "BETA", false, false);
  }
  const ImVec2 f0 = a.At(330, 175), f1 = a.At(870, 175 + kStep * n + 20);
  if (picker) {
    // rejilla de luchadores con su cara; la elegida lleva el numero de su puesto en el equipo
    S::Frame(dl, a.At(325, 170), a.At(875, 590), a.s);
    for (int i = 0; i < nf; ++i) {
      ImVec2 c0, c1;
      cell_rect(i, &c0, &c1);
      const int id = fighters[size_t(i)];
      const bool mine = (pick_n_ > 0 && team_[0] == id);
      if (i == pick_cur_) S::Highlight(dl, ImVec2(c0.x - 1, c0.y - 1), ImVec2(c1.x + 1, c1.y + 1), t);
      if (const ImTextureID face = dbz3::ingame::TeamFighterFace(id)) {
        dl->AddImage(face, c0, c1, ImVec2(0.05f, 0.0f), ImVec2(0.80f, 0.75f), mine ? IM_COL32(110, 110, 120, 255) : IM_COL32_WHITE);
      } else {
        dl->AddRectFilled(c0, c1, IM_COL32(10, 15, 30, 230));
        S::MenuText(dl, ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), 17 * a.s, dbz3::ingame::TeamFighterName(id).c_str(), i == pick_cur_, false, 68 * a.s);
      }
      dl->AddRect(c0, c1, i == pick_cur_ ? IM_COL32(240, 190, 50, 255) : IM_COL32(30, 20, 10, 255), 0, 0, (i == pick_cur_ ? 3.0f : 1.5f) * a.s);
      if (mine) S::MenuText(dl, ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), 34 * a.s, "1", true, false);
    }
    // cabecera: los dos puestos del equipo
    for (int k = 0; k < 2; ++k) {
      char tb[96];
      const std::string nm = team_[k] >= 0 ? dbz3::ingame::TeamFighterName(team_[k]) : std::string("?");
      std::snprintf(tb, sizeof(tb), "%d  %s", k + 1, nm.c_str());
      S::MenuText(dl, a.At(450 + 300.0f * float(k), 200), 30 * a.s, tb, k == pick_n_, false, 270 * a.s);
    }
  } else {
  S::Frame(dl, f0, f1, a.s);
  }
  if (!picker && !idle && !(searching && link == Link::kIdle)) RoomRows(dl, a, 175 + kStep * n + 44, n >= 3 ? 2 : 3, s, t);   // sala: rango y reglas de los dos
  char buf[96];
  for (int i = 0; i < (picker ? 0 : n); ++i) {
    const ImVec2 r0(f0.x, f0.y + (10 + kStep * i) * a.s), r1(f1.x, r0.y + (kStep - 2) * a.s);
    const ImVec2 c((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    const bool sel = i == row_;
    if (sel) S::Highlight(dl, r0, r1, t);
    const char* label = "";
    bool disabled = false;
    switch (rows[size_t(i)]) {
      case kSearchRow: label = T::T("BUSCAR PARTIDA", "FIND A MATCH"); break;
      case kHostRow: label = T::T("CREAR SALA", "CREATE A ROOM"); break;
      case kJoinRow: label = T::T("UNIRME CON CODIGO", "JOIN WITH A CODE"); break;
      case kModeSolo: label = T::T("1 CONTRA 1", "1 VS 1"); break;
      case kModeTag: label = T::T("TAG TEAM  2 CONTRA 2", "TAG TEAM  2 VS 2"); break;
      case kTeamRow: {
        const std::string tn = std::string(T::T("EQUIPO: ", "TEAM: ")) + (team_[0] >= 0 ? dbz3::ingame::TeamFighterName(team_[0]) + " + " + dbz3::ingame::TeamFighterName(team_[1]) : "?");
        std::snprintf(buf, sizeof(buf), "%s", tn.c_str());
        label = buf;
        break;
      }
      case kCopyRow: label = ImGui::GetTime() - copied_at_ < 2.0 ? T::T("COPIADO", "COPIED") : T::T("COPIAR CODIGO", "COPY CODE"); break;
      case kPickRow: break;
      case kAdvRow: label = T::T("AJUSTES AVANZADOS", "ADVANCED"); break;
      case kConsentYes: label = T::T("ACEPTAR Y BUSCAR", "ACCEPT AND SEARCH"); break;
      case kConsentNo: label = T::T("NO, USAR CODIGO", "NO, USE A CODE"); break;
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
    if (rows[size_t(i)] == kJoinRow && editing_) {
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
      if (ImGui::IsItemDeactivated()) {
        editing_ = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) join_after_edit_ = true;
      }
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
  if (consent_ask_ && idle && !searching) {
    say = T::T("Para encontrar rival, tu codigo de sala se publica en un servicio gratuito (ntfy.sh). Ese codigo incluye tu direccion de internet (IP), y cualquiera que lo lea puede verla. Solo se envia eso, la version del juego y un resumen de tus mods. Si prefieres que no, juega con un amigo usando un codigo.",
               "To find an opponent, your room code is posted on a free service (ntfy.sh). That code includes your internet address (IP), and anyone reading it can see it. Only that, the game version and a summary of your mods are sent. If you would rather not, play with a friend using a code.");
  } else if (searching) {
    if (!last_err_.empty()) say = last_err_ + "\n";
    say += (g_mm.st == Mm::kJoin ? T::T("Rival encontrado. Conectando...", "Opponent found. Connecting...")
            : g_mm.st == Mm::kHost ? T::T("Esperando a que otro jugador busque. Puedes cancelar cuando quieras.", "Waiting for another player to search. You can cancel any time.")
                                   : T::T("Buscando rival...", "Looking for an opponent..."));
    static double host_desde = 0;   // sin rival tras un rato: decir que hacer
    if (g_mm.st != Mm::kHost) host_desde = 0;
    else if (host_desde == 0) host_desde = ImGui::GetTime();
    if (host_desde != 0 && ImGui::GetTime() - host_desde > 45.0)
      say += T::T("\nNo hay nadie buscando ahora mismo. Puedes seguir esperando o cancelar y jugar con un amigo usando CREAR SALA.",
                  "\nNobody else is searching right now. You can keep waiting, or cancel and play with a friend using CREATE A ROOM.");
    if (g_mm.st == Mm::kHost && g_mm.poll_fails >= 2)
      say += T::T("\nSin respuesta del servicio: tu sala sigue abierta, pasa el codigo a mano.", "\nNo answer from the service: your room is still open, give the code by hand.");
  } else if (link == Link::kIdle && step_ == 0) {
    say += T::T("Primero, como quereis jugar? Elige una opcion: despues os explico cada paso.",
                "First, how do you want to play? Pick one and I will explain each step.");
    say += "\n";
    say += cur == kModeTag ? T::T("Cada uno lleva un equipo de dos luchadores y se relevan en pleno combate.",
                                  "Each of you brings a team of two fighters and you swap them mid-fight.")
                           : T::T("Un luchador contra otro, como el Duelo de siempre.", "One fighter against another, like the classic Duel.");
  } else if (link == Link::kIdle && step_ == 1) {
    say += pick_n_ == 0 ? T::T("Elige a tu primer luchador", "Pick your first fighter")
                        : T::T("Elige a tu segundo luchador", "Pick your second fighter");
    say += "\n" + dbz3::ingame::TeamFighterName(fighters[size_t(pick_cur_)]) + "  (" + T::T("A: elegir, B: atras", "A: pick, B: back") + ")";
  } else if (idle) {
    // El motivo real (SO, archivos, puerto, timeout) y el informe deben verse sin Avanzado.
    if (link != Link::kIdle) say += s.Status() + "\n";
    if (step_ == 2 && link == Link::kIdle && !adv_) {
      say += tag_ ? T::T("Modo: Tag Team (equipo de dos). ", "Mode: Tag Team (team of two). ")
                  : T::T("Modo: 1 contra 1. ", "Mode: 1 vs 1. ");
    }
    if (link == Link::kError && s.desynced())
      say += std::string(T::T("Los dos juegos dejaron de ver lo mismo. Que hacer: volved a crear la sala; si se repite, enviadnos el informe.",
                              "The two games stopped seeing the same thing. What to do: create the room again; if it repeats, send us the report.")) + "\n";
    else if (link == Link::kLost)
      say += std::string(T::T("Se perdio la conexion. Que hacer: mira que tu internet funciona y volved a crear la sala y unirte.",
                              "The connection was lost. What to do: check your internet, then create the room and join again.")) + "\n";
    if (link != Link::kError && link != Link::kLost) switch (cur) {   // con un error, solo el error y que hacer (cuadro corto)
      case kSearchRow:
        say += T::T("Te empareja con otro jugador que este buscando ahora. Solo pulsa A.", "Matches you with another player searching right now. Just press A.");
        break;
      case kHostRow:
        say += T::T("Crea una sala: te damos un codigo para pasarselo a tu amigo.",
                    "Create a room: we give you a code to send to your friend.");
        break;
      case kJoinRow:
        say += T::T("Escribe el codigo que te paso tu amigo (o copialo y pulsa X para pegarlo). La sala te dira su modo.",
                    "Type the code your friend gave you (or copy it and press X to paste it). The room will show its mode.");
        break;
      case kTeamRow:
        say += T::T("Este es tu equipo. Pulsa A si quieres cambiarlo.", "This is your team. Press A to change it.");
        say += " ";
        say += T::T("Usan tu lista Custom; si un luchador no tiene, la Normal.", "They use your Custom list; if a fighter has none, the Normal one.");
        break;
      case kAdvRow:
        say += T::T("Para problemas de conexion: puerto y retardo.", "For connection problems: port and delay.");
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
      if (mode_ != 1) say += std::string(T::T("\nPasa este codigo a tu amigo: ", "\nGive this code to your friend: ")) + code + T::T("\nYa esta copiado: pegalo en un mensaje.", "\nIt is already copied: paste it in a message.");
      if (link == Link::kWaiting && how != "local" && adv_) {
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
        if (adv_) say += "\n" + std::string(T::T("El router acepto abrir el puerto. Falta comprobar la llegada del rival.", "The router accepted the port mapping. The other player's connection still needs to be verified."));
      } else if (link == Link::kWaiting) {
        say += "\n" + std::string(how == "stun" ?
            T::T("Codigo publico, puerto sin verificar: abre el puerto UDP en el router y permite el juego en el cortafuegos. Si no puedes, que cree tu amigo.",
                 "Public code, unverified port: forward the UDP port on the router and allow the game through the firewall. If you cannot, let your friend host.") :
            T::T("Este codigo solo sirve en la misma red. Para internet hace falta un codigo publico y abrir el puerto UDP; obtener la IP publica no abre el puerto.",
                 "This code only works on the same network. Internet play needs a public code and UDP port forwarding; obtaining the public IP does not open the port."));
      }
    }
    if (link == Link::kConnected || link == Link::kStarting) {
      if (s.mods_match()) {
        {   // el modo de la sala (lo que lleva cada uno) en palabras normales
          std::vector<int> mine, peer;
          if (s.Teams(&mine, &peer)) {
            auto names = [](const std::vector<int>& v) {
              std::string o;
              for (size_t i = 0; i < v.size(); ++i) o += (i ? " + " : "") + dbz3::ingame::TeamFighterName(v[i]);
              return o;
            };
            const bool tm = mine.size() >= 2, tp = peer.size() >= 2;
            if (tm && tp)
              say += std::string(T::T("\nSala Tag Team (2 contra 2). Tu equipo: ", "\nTag Team room (2 vs 2). Your team: ")) + names(mine) +
                     T::T(". Rivales: ", ". Opponents: ") + names(peer) + ".";
            else if (!tm && !tp)
              say += T::T("\nSala de 1 contra 1. Usan tu lista Custom (preparala en Editar Habilidades antes de entrar); si un luchador no tiene, la Normal.", "\n1 vs 1 room. They use your Custom list (set it up in Edit Skills before joining); if a fighter has none, the Normal one.");
            else
              say += T::T("\nOjo: uno de vosotros eligio Tag Team y el otro 1 contra 1, asi que se jugara 2 contra 1. Para igualarlo, cancelad y volved a entrar con el mismo modo.",
                          "\nHeads up: one of you picked Tag Team and the other 1 vs 1, so it will be 2 vs 1. To match, cancel and join again with the same mode.");
          }
        }
        if (s.ping_ms() >= 0) {
          char qb[64];
          std::snprintf(qb, sizeof(qb), s.ping_ms() < 60 ? T::T("\nConexion: buena (%d ms)", "\nConnection: good (%d ms)")
                                        : s.ping_ms() < 130 ? T::T("\nConexion: regular (%d ms)", "\nConnection: fair (%d ms)")
                                                            : T::T("\nConexion: mala (%d ms)", "\nConnection: poor (%d ms)"),
                        s.ping_ms());
          say += qb;
        }
        if (mode_ == 1) say += host ? T::T("\nListo. El combate empieza solo...", "\nReady. The fight starts by itself...")
                                    : T::T("\nListo. Esperando a que empiece el combate...", "\nReady. Waiting for the fight to start...");
        else
        say += host ? T::T("\nTodo listo. Pulsa EMPEZAR cuando querais.", "\nAll set. Press START when you are ready.")
                    : T::T("\nTodo listo. Esperando a que tu amigo pulse EMPEZAR...",
                           "\nAll set. Waiting for your friend to press START...");
        if (tag_) say += T::T("\nEn combate, SELECT releva a tu companero.", "\nIn the fight, SELECT swaps in your partner.");
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
  if (idle && !searching && !consent_ask_ && step_ == 2) help.push_back({"X", T::T("Pegar codigo", "Paste code")});
  else if (host && mode_ != 1) help.push_back({"X", T::T("Copiar codigo", "Copy code")});
  S::HelpLine(dl, a.At(idle || (host && mode_ != 1) ? 690 : 745, 870), 38 * a.s, help);
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
