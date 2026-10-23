// dbz3 - Fila HUB del menu principal y pantalla HUB: la logica, en el hilo del juego (ver
// src/ingame/hub_panel.h y docs/12_conectores/11_menu_principal_nativo.md s9). US y EU (src/eu_map.h).
//
// El menu principal (objeto de 172 B en tarea+48) sigue creyendo que tiene sus 6 o 7 filas: la fila HUB
// es virtual y no se escribe nunca en el objeto ni en la partida (0x824BA135). Su estado activo
// (sub_8218C5D8) dibuja (sub_8218BBC0: fondo sub_8218B2F0 ... filas sub_8218B550) y despues lee el mando
// del juego (0x8238B780): al principio de su dibujo (gancho de B2F0, nada pintado aun) se decide con ese
// mismo mando y, si lo usa el HUB, se borra antes de que el menu lo lea. Con el cursor en el HUB, el del
// juego queda en la fila 0 (asi el contador de filas de online_panel nunca ve la ultima) y se dibuja sin
// fila elegida ni franja. En la pantalla HUB el juego sigue dibujando su fondo, su marco, su cinta y su
// bocadillo; se le quitan las etiquetas (+151 a 0) y el texto de la cinta, y hub_panel.cpp pinta lo
// nuestro en su sitio.
// Ida y vuelta entre menu y HUB con las animaciones del propio menu (las de su A y su B: sub_8218C588 /
// sub_8218C520 sin pedir pantalla) y un fundido a negro como el de sus pantallas; en negro se rehacen sus
// piezas de entrada (las de sub_8218C4D0, soltando antes las que habia) y entra otra vez como al nacer.
// Ganchos: sub_8218B2F0, sub_8218B550, sub_8218B3F0, sub_8218B998, sub_8217E800, sub_821D6A08 (uno por funcion en todo
// el exe).
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <random>
#include <string>

#include "reloj_prueba.h"
#include "escenarios.h"
#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/hub_panel.h"
#include "ingame/menu_pilot.h"
#include "ingame/online_panel.h"
#include "ingame/tagteam_panel.h"
#include "ingame/tutorial.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"

REXCVAR_DEFINE_BOOL(dbz3_hub_enabled, true, "DBZ3/Mods", "HUB row in the main menu (Tag Team, Online, quick battle...)");
REXCVAR_DECLARE(bool, dbz3_tag_team_enabled);   // online_panel.cpp
REXCVAR_DECLARE(bool, dbz3_online_enabled);

namespace dbz3::ingame {
bool Online();   // tagteam_panel.cpp: sesion online abierta
}

namespace dbz3::hub {
bool RequestBattle(int id1, int id2, int stage, bool p2_com);   // hub.cpp

namespace {
constexpr dbz3::GA kPad = 0x8238B780, kPadIdx = 0x8238B670;     // mando del juego: 184 B por jugador
constexpr dbz3::GA kGame = 0x824BA110, kHelp = 0x82020D54;       // bocadillo de cada fila (u16)
constexpr dbz3::GA kSeFloor = 0x8201F60C;
constexpr dbz3::GA kSeVolume = kGame + 0x2C0C0;
constexpr dbz3::GA kArenaFlag = kGame + 169;                      // Dragon Arena (lo lee sub_821D3B78)
// tarea del menu principal: *(*(0x8245F230) + 16) (sub_8218CB88); sus estados
constexpr dbz3::GA kMenuMgr = 0x8245F230, kStActive = 0x8218C5D8, kStLeaving = 0x8218C4C8;
// botones como los lee el menu (+14 pulsado, +18|+114 con repeticion). Disposicion de PS2: L2 0x100,
// R2 0x200, L1 0x400, R1 0x800; cruceta arriba 0x10, derecha 0x20, abajo 0x40, izquierda 0x80.
constexpr uint16_t kBackBtn = 0x0001, kUp = 0x0010, kRight = 0x0020, kDown = 0x0040, kLeft = 0x0080, kL2 = 0x0100,
                   kR2 = 0x0200, kL1 = 0x0400, kR1 = 0x0800, kA = 0x4000, kB = 0x1000, kCircle = 0x2000;
constexpr uint16_t kPrevPage = kL1 | kL2 | kLeft, kNextPage = kR1 | kR2 | kRight;
constexpr int kSeMove = 2, kSeOk = 3, kSeBack = 5;               // los del propio menu (sub_820922A0)
// texto de la cinta "Menu principal" (sprite +80, lo dibuja sub_8218B3F0 con la cinta +76; medido
// ocultando cada sprite del menu, 2026-10-15): fuera en la pantalla HUB, la cinta se queda
constexpr uint32_t kTitleSlot = 80;
// IDs jugables de serie (los de tagteam_panel.cpp) para el combate rapido
constexpr int kFighters[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
                             19, 20, 21, 27, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43};

const char* const kKeys[kEntries] = {"tagteam", "online",      "rapido",   "torre",      "supervivencia", "kumite",
                                     "ranking", "laboratorio", "escenarios", "pasarela", "minijuegos",    "satan"};
// nombre (sin su pieza etq_*) y lo que dice el bocadillo de las entradas fijas, en los idiomas del juego
const Texts kNames[kEntries] = {
    {"Tag Team", "Tag Team", "Tag Team", "Tag Team", "Tag Team"},
    {"Online", "Online", "En ligne", "Online", "Online"},
    {"Combate rápido", "Quick Battle", "Combat rapide", "Schnellkampf", "Scontro rapido"},
    {"Torre", "Tower", "Tour", "Turm", "Torre"},
    {"Supervivencia", "Survival", "Survie", "Überleben", "Sopravvivenza"},
    {"Kumite", "Kumite", "Kumite", "Kumite", "Kumite"},
    {"Ranking", "Ranking", "Classement", "Rangliste", "Classifica"},
    {"Laboratorio", "Laboratory", "Laboratoire", "Labor", "Laboratorio"},
    {"Escenarios", "Stages", "Arènes", "Arenen", "Arene"},
    {"Pasarela", "Gateway", "Passerelle", "Portal", "Passerella"},
    {"Minijuegos", "Minigames", "Mini-jeux", "Minispiele", "Minigiochi"},
    {"Leyenda de Mr. Satán", "Legend of Hercule", "Légende d'Hercule", "Mr. Satans Legende", "Leggenda di Mr. Satan"}};
const Texts kHelps[kEntries] = {
    {"Equipos de 2 o 3 luchadores. Pulsa SELECT en el combate para relevar al compañero.",
     "Teams of 2 or 3 fighters. Press SELECT in battle to switch partners.",
     "Équipes de 2 ou 3 combattants. Appuie sur SELECT en combat pour changer de partenaire.",
     "Teams aus 2 oder 3 Kämpfern. Drücke im Kampf SELECT, um den Partner zu wechseln.",
     "Squadre di 2 o 3 lottatori. Premi SELECT in combattimento per cambiare compagno."},
    {"Juega contra otra persona por internet desde el menú de Duelo.",
     "Play against another person over the internet from the Duel menu.",
     "Affronte une autre personne sur Internet depuis le menu Duel.",
     "Spiele aus dem Duell-Menü über das Internet gegen eine andere Person.",
     "Gioca contro un'altra persona via Internet dal menu Duello."},
    {"Un combate al momento contra el ordenador, con luchadores y escenario al azar.",
     "A fight right away against the computer, with random fighters and stage.",
     "Un combat immédiat contre l'ordinateur, avec combattants et arène au hasard.",
     "Sofort ein Kampf gegen den Computer, mit zufälligen Kämpfern und zufälliger Arena.",
     "Uno scontro immediato contro il computer, con lottatori e arena a caso."},
    {"Sube piso a piso contra rivales cada vez más fuertes con tu último luchador del Duelo.",
     "Climb floor by floor against ever stronger rivals with your last Duel fighter.",
     "Monte étage par étage face à des rivaux toujours plus forts avec ton dernier combattant du Duel.",
     "Steige Etage für Etage gegen immer stärkere Gegner auf, mit deinem letzten Duell-Kämpfer.",
     "Sali piano dopo piano contro rivali sempre più forti con il tuo ultimo lottatore del Duello."},
    {"Oleadas seguidas: la vida que te queda pasa a la siguiente y recuperas un poco entre medias.",
     "Back-to-back waves: your remaining health carries over and you recover a little in between.",
     "Vagues à la suite : la vie qui te reste passe à la suivante et tu en récupères un peu entre deux.",
     "Wellen am Stück: Deine restliche Energie geht in die nächste über, dazwischen erholst du dich etwas.",
     "Ondate di fila: la vita che ti resta passa alla successiva e ne recuperi un po' tra l'una e l'altra."},
    {"Jefes con mucha vida, uno tras otro. Tu vida también se arrastra.",
     "Bosses with lots of health, one after another. Your health carries over too.",
     "Des boss très résistants, l'un après l'autre. Ta vie est conservée, elle aussi.",
     "Bosse mit viel Energie, einer nach dem anderen. Auch deine Energie bleibt erhalten.",
     "Boss con tanta vita, uno dopo l'altro. Anche la tua vita si conserva."},
    {"El rango de cada luchador (con medalla) y tus mejores marcas en los retos.",
     "Each fighter's rank (with medal) and your best challenge records.",
     "Le rang de chaque combattant (avec médaille) et tes meilleurs records dans les défis.",
     "Der Rang jedes Kämpfers (mit Medaille) und deine besten Rekorde in den Herausforderungen.",
     "Il grado di ogni lottatore (con medaglia) e i tuoi migliori record nelle sfide."},
    {"Practica contra un muñeco (quieto o en guardia) y mira los datos de tu último golpe.",
     "Practice against a dummy (standing or guarding) and check your last hit's data.",
     "Entraîne-toi contre un mannequin (immobile ou en garde) et regarde les données de ton dernier coup.",
     "Übe gegen eine Puppe (stehend oder blockend) und sieh dir die Daten deines letzten Treffers an.",
     "Allenati contro un manichino (fermo o in guardia) e guarda i dati del tuo ultimo colpo."},
    {"Explora los escenarios y elige dónde luchar.", "Browse the stages and pick where to fight.",
     "Parcours les arènes et choisis où combattre.", "Sieh dir die Arenen an und wähle, wo du kämpfst.",
     "Esplora le arene e scegli dove combattere."},
    {"Historias y misiones de Budokai 2 e Infinite World. Algunas necesitan tu copia del juego: elige su ISO en la campaña (botón A) o en el lanzador (pestaña Juego).",
     "Stories and missions from Budokai 2 and Infinite World. Some need your copy of the game: pick its ISO in the campaign (A button) or in the launcher (Game tab).",
     "Histoires et missions de Budokai 2 et Infinite World. Certaines demandent ta copie du jeu : choisis son ISO dans la campagne (bouton A) ou dans le lanceur (onglet Jeu).",
     "Geschichten und Missionen aus Budokai 2 und Infinite World. Manche brauchen deine Spielkopie: wähle ihre ISO in der Kampagne (A-Taste) oder im Launcher (Reiter Spiel).",
     "Storie e missioni di Budokai 2 e Infinite World. Alcune richiedono la tua copia del gioco: scegli la sua ISO nella campagna (tasto A) o nel launcher (scheda Gioco)."},
    {"Pruebas cortas para jugar entre combate y combate.", "Short challenges to play between fights.",
     "Des épreuves courtes à jouer entre deux combats.", "Kurze Aufgaben für zwischen den Kämpfen.",
     "Prove brevi da giocare tra uno scontro e l'altro."},
    {"La historia de Mr. Satán en los Juegos de Cell: 11 retadores. Necesita tu copia de Budokai 1.",
     "Mr. Satan's story at the Cell Games: 11 challengers. Needs your Budokai 1 copy.",
     "L'histoire d'Hercule aux Jeux de Cell : 11 adversaires. Nécessite ta copie de Budokai 1.",
     "Mr. Satans Geschichte bei den Cell-Spielen: 11 Herausforderer. Braucht deine Kopie von Budokai 1.",
     "La storia di Mr. Satan ai Giochi di Cell: 11 sfidanti. Serve la tua copia di Budokai 1."}};

struct Mode {
  std::atomic<void (*)()> open{nullptr};
  std::atomic<bool (*)()> busy{nullptr};
  std::atomic<bool (*)()> ready{nullptr};          // RegisterGate: false = en gris
  std::atomic<const char* (*)()> why{nullptr};     // ... y lo que dice el bocadillo
};
Mode g_modes[kMaxEntries];   // inicializacion constante: valen los registros desde inicializadores estaticos

// Entradas nuevas de RegisterEntry (kEntries..): clave y textos copiados. Se crean al primer uso (otro .cpp
// puede registrar antes de que este fichero inicialice sus globales) y solo crecen.
struct Extra {
  std::string key, name[5], help[5];
};
struct Extras {
  std::mutex mu;
  Extra e[kMaxEntries - kEntries];
  std::atomic<int> n{0};
};
Extras& Ext() {
  static Extras x;
  return x;
}
// clave -> entrada (fija o nueva); -1 si no existe
int Find(const char* key) {
  for (int e = 0; e < kEntries; ++e) {
    if (std::strcmp(key, kKeys[e]) == 0) return e;
  }
  Extras& x = Ext();
  for (int i = 0, n = x.n.load(); i < n; ++i) {
    if (x.e[i].key == key) return kEntries + i;
  }
  return -1;
}
// indice del idioma del juego en Texts (es, en, fr, de, it)
int Lang() {
  const char* c = dbz3::i18n::LangCode();
  return c[0] == 'e' && c[1] == 's' ? 0 : c[0] == 'f' ? 2 : c[0] == 'd' ? 3 : c[0] == 'i' ? 4 : 1;
}

std::mutex g_mu;
View g_view;
std::atomic<int64_t> g_seen_ms{0};
std::atomic<bool> g_session{false};   // InSession()
std::atomic<int> g_se{-1};            // QueueSe: sonido pendiente para el siguiente fotograma del menu
std::atomic<int64_t> g_se_at{0};      // ... y cuando se pidio (uno de hace mas de 0,3 s ya no suena)
std::atomic<int> g_quick[3] = {-1, -1, -1};   // ultimo combate rapido: luchador 1, luchador 2, escenario

int64_t NowMs() { return dbz3::RelojMs(); }

// Estado del HUB: solo el hilo del juego.
struct State {
  bool focus = false;    // cursor en la fila HUB
  bool open = false;     // pantalla HUB
  bool leaving = false;  // el menu se va hacia un modo elegido en el HUB (sin sus etiquetas)
  int leave_frames = 0;
  bool back = false;     // al volver al menu principal, abrir el HUB
  bool armed = false;    // ... y ya se salio de el (o la pantalla del modo se abrio encima)
  int cursor = 0;
  int last_state = -1;
  uint32_t obj = 0;      // menu que se esta dibujando
  uint32_t bubble = 0;   // tarea del bocadillo del menu (+144)
  bool panel = false;    // dialogo pedido
  Fade fade = Fade::kNone;
  bool to_hub = false;
  int64_t t0 = 0;
  uint32_t task = 0;     // tarea del menu durante el fundido
  bool test = false;     // DBZ3_TEST_HUB
} g;

bool Enabled() { return REXCVAR_GET(dbz3_hub_enabled); }
// el bocadillo del menu dice lo del HUB (sin el texto del juego)
bool OwnsBubble() { return g.focus || g.open || g.leaving || (g.back && g.armed) || g.fade != Fade::kNone; }

bool OtherScreen() {
  using namespace dbz3::ingame;
  if (TagTeamWanted() || TagTeamGate() || TagTeamMode() || pilot::Busy() || OnlinePanel::IsOpen() || Online()) {
    return true;
  }
  for (const Mode& m : g_modes) {
    if (bool (*busy)() = m.busy.load(); busy && busy()) return true;
  }
  return false;
}

float LoadF32(uint8_t* base, uint32_t addr) {
  const uint32_t u = REX_LOAD_U32(addr);
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}
void StoreF32(uint8_t* base, uint32_t addr, float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  REX_STORE_U32(addr, u);
}

// Llamada a una funcion del juego con r3 (y r4) sin ensuciar los registros del llamador.
template <typename F>
void Call(PPCContext& ctx, uint8_t* base, F fn, uint32_t r3, uint32_t r4 = 0) {
  PPCContext c = ctx;
  c.r3.u64 = r3;
  c.r4.u64 = r4;
  fn(c, base);
}

// Sonido del menu, como lo hace sub_8218C5D8 (volumen de efectos y, si suena, el efecto).
void PlaySe(PPCContext& ctx, uint8_t* base, int id) {
  const float volume = LoadF32(base, kSeVolume);
  PPCContext c = ctx;
  c.f1.f64 = volume;
  c.r3.u64 = 0;
  c.r4.u64 = 0;
  DBZ3_CALL_EU(sub_82092570)(c, base);
  if (volume > LoadF32(base, kSeFloor)) {
    c = ctx;
    c.r3.u64 = 0;
    c.r4.u64 = 0;
    c.r5.u64 = uint64_t(id);
    c.r6.u64 = 0;
    DBZ3_CALL_EU(sub_820922A0)(c, base);
  }
}

// Texto del bocadillo del menu (-1 = sin texto: el juego lo funde y queda el bocadillo vacio).
void Bubble(uint8_t* base, int id) {
  if (g.bubble && int32_t(REX_LOAD_U32(g.bubble + 60)) != id) REX_STORE_U32(g.bubble + 60, uint32_t(id));
}

// Cursor del juego a una fila nativa, como al moverse en el menu: sonido, ayuda y voz de la fila.
void ToNativeRow(PPCContext& ctx, uint8_t* base, uint32_t obj, int row) {
  REX_STORE_U8(obj + 150, uint8_t(row));
  PlaySe(ctx, base, kSeMove);
  Bubble(base, int16_t(REX_LOAD_U16(kHelp + 2 * row)));
  Call(ctx, base, DBZ3_CALL_EU(sub_8218B8E0), obj);
}

// ---- Fundido entre el menu y la pantalla HUB ----
// Sale el menu con su propia animacion (la de su A al ir al HUB, la de su B al volver) y queda en su
// estado "saliendo" (sub_8218C4C8: solo dibuja), sin pedir pantalla. false = no se puede (se abre en seco).
bool StartFade(PPCContext& ctx, uint8_t* base, uint32_t obj, bool to_hub) {
  const uint32_t mgr = REX_LOAD_U32(kMenuMgr);
  const uint32_t task = mgr ? REX_LOAD_U32(mgr + 16) : 0;
  if (!task || REX_LOAD_U32(task + 48) != obj || REX_LOAD_U32(task + 28) != kStActive) {
    REXLOG_WARN("dbz3 hub: sin fundido (tarea del menu {:08X})", task);
    return false;
  }
  if (to_hub) {
    Call(ctx, base, DBZ3_CALL_EU(sub_8218C588), obj);   // como su A: piezas de salida, +148 = 5, +156 = 0
  } else {                                // como su B (sub_8218C520) sin pedir el titulo
    Call(ctx, base, DBZ3_CALL_EU(sub_8218C3A8), obj);
    Call(ctx, base, DBZ3_CALL_EU(sub_8218C0C8), obj);
    Call(ctx, base, DBZ3_CALL_EU(sub_8218BF38), obj);
    REX_STORE_U16(obj + 148, 4);
    StoreF32(base, obj + 156, 0.0f);
  }
  Call(ctx, base, DBZ3_CALL_EU(sub_82081A48), REX_LOAD_U32(obj + 164));   // calla la voz de la fila
  REX_STORE_U32(task + 28, kStLeaving);
  g.task = task;
  g.fade = Fade::kOut;
  g.to_hub = to_hub;
  g.t0 = NowMs();
  return true;
}

// En negro: las piezas que entran (cinta, titulo, marco, etiquetas, franja) otra vez desde el principio,
// como sub_8218C4D0 al nacer el menu (el cielo y el reparto no se mueven). Antes se sueltan las que hay
// (sub_8218C190 no las suelta; sub_8218BE70 y sub_8218C000 si). Al principio del dibujo: nada pintado.
void ReplayEntry(PPCContext& ctx, uint8_t* base, uint32_t obj) {
  auto drop = [&](uint32_t off) {
    if (const uint32_t inst = REX_LOAD_U32(obj + off)) {
      Call(ctx, base, DBZ3_CALL_EU(sub_820ACCF0), inst, 1);
      REX_STORE_U32(obj + off, 0);
    }
  };
  drop(84);
  drop(88);
  for (uint32_t i = 0; i < 7; ++i) drop(92 + 4 * i);
  drop(120);
  drop(124);
  drop(136);
  Call(ctx, base, DBZ3_CALL_EU(sub_8218BE70), obj);
  Call(ctx, base, DBZ3_CALL_EU(sub_8218C000), obj);
  Call(ctx, base, DBZ3_CALL_EU(sub_8218C190), obj);
  REX_STORE_U16(obj + 148, 2);
  StoreF32(base, obj + 156, 0.0f);
}

void FadeStep(PPCContext& ctx, uint8_t* base, uint32_t obj) {
  const int64_t now = NowMs();
  const bool same = g.task && REX_LOAD_U32(g.task + 48) == obj;
  if (!same) {   // el menu ya no es el mismo: se deja estar
    g.fade = Fade::kNone;
    return;
  }
  if (g.fade == Fade::kOut && now - g.t0 >= kFadeOutMs + kFadeHoldMs) {
    ReplayEntry(ctx, base, obj);
    g.open = g.to_hub;
    g.focus = !g.to_hub;
    g.fade = Fade::kIn;
    g.t0 = now;
    REXLOG_INFO("dbz3 hub: {} (fundido)", g.to_hub ? "entra la pantalla HUB" : "vuelve el menu principal");
  } else if (g.fade == Fade::kIn && now - g.t0 >= kEntryMs) {
    REX_STORE_U16(obj + 148, 3);
    REX_STORE_U8(obj + 168, 0);
    REX_STORE_U32(g.task + 28, kStActive);
    g.fade = Fade::kNone;
  }
}

// escenarios normales (los registros 11-16 son variantes; stage_ext.h) y las altas de los mods (17+,
// escenarios.cpp: RequestBattle las carga igual que una de serie)
int Stage(std::mt19937& rng) {
  using dbz3::escenarios::kSerie;
  const int altas = dbz3::escenarios::Visibles();   // sin los campos de IW (no son escenarios de Duelo)
  const int k = std::uniform_int_distribution<int>(0, 10 + altas)(rng);
  return k <= 10 ? k : kSerie + (k - 11);
}

// A en una entrada del HUB. true = deja una pulsacion para el menu del juego (no borrar el mando).
bool Launch(PPCContext& ctx, uint8_t* base, uint32_t obj, uint32_t pad) {
  const int e = g.cursor;
  if (!EntryReady(e)) {   // por llegar (otro frente) o falta algo del jugador: el bocadillo ya lo dice
    // sin la ISO de PS2 que pide (Leyenda: Budokai 1; minijuegos: Infinite World): A abre el dialogo para elegirla
    const std::string k = EntryKey(e);
    const int juego = k == "satan" ? 0 : k == "minijuegos" ? 2 : -1;
    if (juego >= 0 && dbz3::pasarela::IsoPs2(juego).empty()) dbz3::pasarela::ElegirIsoPs2EnSegundoPlano(juego);
    return false;
  }
  auto away = [](bool same_menu) {   // el HUB vuelve cuando el modo acabe
    g.open = false;
    g.back = true;
    g.armed = same_menu;
    g.leaving = !same_menu;
    g.leave_frames = 0;
  };
  switch (e) {
    case kTagTeam:   // su pantalla, encima del menu principal (B en ella vuelve aqui)
      PlaySe(ctx, base, kSeOk);
      away(true);
      dbz3::ingame::TagTeamOpen();
      break;
    case kOnline:    // el Duelo del juego, que lleva la fila ONLINE: se confirma su fila como haria el jugador
      dbz3::ingame::OnlineOpen();   // y al llegar al Duelo se abre ya el panel ONLINE
      away(false);
      REX_STORE_U8(obj + 150, 1);
      REX_STORE_U16(pad + 14, kA);
      REX_STORE_U16(pad + 18, 0);
      REX_STORE_U16(pad + 114, 0);
      return true;
    case kQuick: {   // 1P VS COM al azar por el conector nativo (hub.cpp)
      static std::mt19937 rng{uint32_t(NowMs())};
      std::uniform_int_distribution<size_t> pick(0, std::size(kFighters) - 1);
      const int p1 = kFighters[pick(rng)];
      int p2 = kFighters[pick(rng)];
      while (p2 == p1) p2 = kFighters[pick(rng)];
      const int stage = Stage(rng);
      if (RequestBattle(p1, p2, stage, true)) {
        g_quick[0] = p1;
        g_quick[1] = p2;
        g_quick[2] = stage;
        PlaySe(ctx, base, kSeOk);
        away(false);
        REXLOG_INFO("dbz3 hub: combate rapido {} contra {} (escenario {}, {})", p1, p2, stage,
                    dbz3::escenarios::Nombre(stage, 1));
      }
      break;
    }
    default: {
      const Mode& m = g_modes[e];
      PlaySe(ctx, base, kSeOk);
      away(m.busy.load() != nullptr);
      m.open.load()();
      REXLOG_INFO("dbz3 hub: modo {}", EntryKey(e));
      break;
    }
  }
  return false;
}

// Mueve el cursor del HUB: paso (+1/-1, con vuelta al final) o pagina (+1/-1, misma fila de la otra).
void Move(int step, int page_step) {
  int vis[kMaxEntries];
  const int n = VisibleEntries(vis);
  if (n <= 0) return;
  int pos = 0;
  while (pos < n && vis[pos] != g.cursor) ++pos;
  if (pos == n) pos = 0;
  if (step) pos = (pos + step + n) % n;
  if (page_step) {
    const int pages = (n + kPerPage - 1) / kPerPage;
    const int page = (pos / kPerPage + page_step + pages) % pages;
    pos = std::min(page * kPerPage + pos % kPerPage, n - 1);
  }
  g.cursor = vis[pos];
}

// Mando del menu con la fila o la pantalla HUB: true = el HUB se lo queda (el menu no lo ve).
bool Input(PPCContext& ctx, uint8_t* base, uint32_t obj, uint32_t pad) {
  if (g.open && dbz3::ingame::tutorial::Abierto()) return true;   // F3: tutorial encima del HUB (el lo lee)
  const uint16_t pressed = REX_LOAD_U16(pad + 14), rep = REX_LOAD_U16(pad + 18) | REX_LOAD_U16(pad + 114);
  const int rows = REX_LOAD_U8(obj + 151), row = REX_LOAD_U8(obj + 150);
  if (g.test && (pressed || rep)) REXLOG_INFO("dbz3 hub: prueba: botones {:04X} (repeticion {:04X})", pressed, rep);
  if (g.open) {
    if (pressed & (kB | kCircle)) {   // atras: el del menu (0x1000) y B del mando (0x2000)
      PlaySe(ctx, base, kSeBack);
      g.back = false;
      if (!StartFade(ctx, base, obj, false)) {
        g.open = false;
        g.focus = true;
      }
    } else if (pressed & kA) {
      return !Launch(ctx, base, obj, pad);   // Online: la A queda para el menu del juego
    } else if (rep & (kUp | kDown)) {        // con la repeticion del juego (mantener)
      Move((rep & kDown) ? 1 : -1, 0);
      PlaySe(ctx, base, kSeMove);
    } else if (pressed & (kPrevPage | kNextPage)) {
      int vis[kMaxEntries];
      if (VisibleEntries(vis) > kPerPage) {
        Move(0, (pressed & kNextPage) ? 1 : -1);
        PlaySe(ctx, base, kSeMove);
      }
    }
    return true;
  }
  if (g.focus) {
    if (pressed & kB) {   // como en el resto del menu: el juego vuelve al titulo
      g.focus = false;
      return false;
    }
    if (pressed & kA) {
      PlaySe(ctx, base, kSeOk);
      if (!StartFade(ctx, base, obj, true)) g.open = true;
    } else if (rep & kUp) {
      g.focus = false;
      ToNativeRow(ctx, base, obj, rows - 1);
    } else if (rep & kDown) {
      g.focus = false;
      ToNativeRow(ctx, base, obj, 0);
    }
    return true;   // tambien BACK (la ayuda del juego no habla del HUB)
  }
  // la fila HUB va despues de la ultima nativa: abajo desde la ultima o arriba desde la primera
  if (((rep & kDown) && row == rows - 1) || ((rep & kUp) && row == 0)) {
    g.focus = true;
    REX_STORE_U8(obj + 150, 0);
    PlaySe(ctx, base, kSeMove);
    return true;
  }
  return false;
}

// Pruebas (pruebas.py): DBZ3_TEST_HUB=1 registra entradas de mentira (y una nueva con RegisterEntry) para ver
// las paginas y escribe los botones en el log; DBZ3_TEST_HUB_ARENA=1 activa Dragon Arena (7 filas) para el
// siguiente menu principal (la copia de pruebas guarda la partida asi: no usarlo con la partida real).
void TestHooks(uint8_t* base) {
  static bool done = false;
  if (done) return;
  done = true;
  if (std::getenv("DBZ3_TEST_HUB")) {
    g.test = true;
    for (const char* k : {"escenarios", "pasarela", "minijuegos", "satan"}) {
      RegisterMode(k, [] { REXLOG_INFO("dbz3 hub: prueba: entrada de mentira"); }, [] { return false; });
    }
    RegisterEntry("prueba", {"Entrada nueva", "New entry", "Nouvelle entrée", "Neuer Eintrag", "Nuova voce"},
                  {"Entrada de otro frente con sus textos (RegisterEntry).", "Another front's entry with its own texts "
                   "(RegisterEntry).", "Entrée d'un autre front avec ses textes.", "Eintrag eines anderen Bereichs.",
                   "Voce di un altro fronte con i suoi testi."},
                  [] { REXLOG_INFO("dbz3 hub: prueba: entrada nueva"); }, [] { return false; });
    REXLOG_INFO("dbz3 hub: prueba: 4 entradas de mentira (escenarios, pasarela, minijuegos, satan) y una nueva");
  }
  if (std::getenv("DBZ3_TEST_HUB_ARENA") && REX_LOAD_U8(kArenaFlag) == 0) {
    REX_STORE_U8(kArenaFlag, 1);
    REXLOG_INFO("dbz3 hub: prueba: Dragon Arena activado (vale desde el siguiente menu principal)");
  }
}

}  // namespace

const char* EntryKey(int e) {
  if (e >= 0 && e < kEntries) return kKeys[e];
  Extras& x = Ext();
  return e >= kEntries && e < kEntries + x.n.load() ? x.e[e - kEntries].key.c_str() : "";
}

int EntryCount() { return kEntries + Ext().n.load(); }

const char* Tr(const Texts& t) {
  const char* const v[5] = {t.es, t.en, t.fr, t.de, t.it};
  if (t.en)
    if (const char* o = dbz3::i18n::Otro(t.en)) return o;   // pt / ja
  const char* s = v[Lang()];
  return s && *s ? s : t.en ? t.en : "";
}

const char* EntryName(int e) {
  if (e >= 0 && e < kEntries) return Tr(kNames[e]);
  Extras& x = Ext();
  if (e < kEntries || e >= kEntries + x.n.load()) return "";
  const Extra& r = x.e[e - kEntries];
  return r.name[Lang()].empty() ? r.name[1].c_str() : r.name[Lang()].c_str();
}

const char* EntryHelp(int e) {
  if (e >= 0 && e < kEntries) return Tr(kHelps[e]);
  Extras& x = Ext();
  if (e < kEntries || e >= kEntries + x.n.load()) return "";
  const Extra& r = x.e[e - kEntries];
  return r.help[Lang()].empty() ? r.help[1].c_str() : r.help[Lang()].c_str();
}

bool RegisterEntry(const char* key, const Texts& name, const Texts& help, void (*open)(), bool (*busy)()) {
  if (!key || !*key || !open) return false;
  Extras& x = Ext();
  {
    std::lock_guard<std::mutex> lock(x.mu);
    if (Find(key) < 0) {
      const int n = x.n.load();
      if (n >= kMaxEntries - kEntries) return false;
      Extra& r = x.e[n];
      r.key = key;
      const char* const nm[5] = {name.es, name.en, name.fr, name.de, name.it};
      const char* const hp[5] = {help.es, help.en, help.fr, help.de, help.it};
      for (int l = 0; l < 5; ++l) {
        r.name[l] = nm[l] ? nm[l] : "";
        r.help[l] = hp[l] ? hp[l] : "";
      }
      if (r.name[1].empty()) r.name[1] = key;
      x.n.store(n + 1);   // despues de rellenarla: quien lea n ya la ve entera
    }
  }
  return RegisterMode(key, open, busy);
}

bool RegisterMode(const char* key, void (*open)(), bool (*busy)()) {
  const int e = key ? Find(key) : -1;
  if (e < kTower || !open) return false;   // Tag Team, Online y Combate rapido son del HUB
  g_modes[e].busy = busy;
  g_modes[e].open = open;
  return true;
}

bool RegisterGate(const char* key, bool (*ready)(), const char* (*why)()) {
  const int e = key ? Find(key) : -1;
  if (e < kTower) return false;
  g_modes[e].why = why;
  g_modes[e].ready = ready;
  return true;
}

const char* EntryWhy(int e) {
  if (e <= kQuick || e >= kMaxEntries) return nullptr;
  const char* (*why)() = g_modes[e].why.load();
  return why ? why() : nullptr;
}

bool EntryReady(int e) {
  switch (e) {
    case kTagTeam: return REXCVAR_GET(dbz3_tag_team_enabled);
    case kOnline: return REXCVAR_GET(dbz3_online_enabled);
    case kQuick: return true;
    default: {
      if (e <= kQuick || e >= kMaxEntries || g_modes[e].open.load() == nullptr) return false;
      bool (*ready)() = g_modes[e].ready.load();
      return !ready || ready();
    }
  }
}

bool EntryVisible(int e) {
  return e >= 0 && e < EntryCount() && (e < kAlwaysShown || g_modes[e].open.load() != nullptr);
}

int VisibleEntries(int* out) {
  int n = 0;
  for (int e = 0, c = EntryCount(); e < c; ++e) {
    if (EntryVisible(e)) out[n++] = e;
  }
  return n;
}

bool RowActive() { return Enabled(); }

bool InSession() { return g_session.load(); }

void QueueSe(int id) {
  g_se_at = NowMs();
  g_se = id;
}

bool SePending() { return g_se.load() >= 0 && NowMs() - g_se_at.load() <= 300; }

void PlayQueuedSe(PPCContext& ctx, uint8_t* base) {
  const int se = g_se.exchange(-1);
  // ponytail: 0,3 s; un sonido pedido justo al irse el menu (la A que empieza un reto) sonaba 20 s despues, al acabar
  // el combate
  if (se < 0 || NowMs() - g_se_at.load() > 300) return;
  PlaySe(ctx, base, se);
  static const bool log_se = std::getenv("DBZ3_TEST_HUB_SE") != nullptr;
  if (log_se) {   // SONDA: ranuras de voz del gestor de sonido (sm = *(0x82386260) + 808; 64 de 24 B desde sm)
    const uint32_t sm = REX_LOAD_U32(dbz3::GA(0x82386260)) + 808;
    int voces = 0, mias = 0;
    uint32_t h = 0;
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t v = sm + 8 + 24 * i;
      if (!REX_LOAD_U8(v + 8)) continue;
      ++voces;
      if (REX_LOAD_U32(v - 8) == 0 && REX_LOAD_U32(v - 4) == uint32_t(se)) {
        ++mias;
        h = REX_LOAD_U32(v);
      }
    }
    REXLOG_INFO("dbz3 hub: prueba: sonido {} de la cola (voces {}, suyas {}, asa {:08X}, estado {})", se, voces, mias, h,
                REX_LOAD_U32(sm + 1564));
  }
}

bool LastQuick(int* p1, int* p2, int* stage) {
  *p1 = g_quick[0];
  *p2 = g_quick[1];
  *stage = g_quick[2];
  return *stage >= 0;
}

int64_t ClockMs() { return NowMs(); }

View Snapshot() {
  std::lock_guard<std::mutex> lock(g_mu);
  View v = g_view;
  v.menu = NowMs() - g_seen_ms.load() < 200;
  return v;
}

}  // namespace dbz3::hub

namespace dbz3::hub {
namespace {
// Un fotograma del menu, al principio de su dibujo (nada pintado aun) y antes de que su estado lea el
// mando (sub_8218BBC0 dibuja y despues el estado activo lee el mando).
void Frame(PPCContext& ctx, uint8_t* base, uint32_t obj) {
  if (!g.panel) {
    g.panel = true;
    dbz3::launcher::DeferUi(&CreatePanel);
  }
  TestHooks(base);
  if (obj != g.obj) {   // otro menu (nacio de nuevo): ningun fundido a medias
    g.obj = obj;
    g.fade = Fade::kNone;
  }
  g.bubble = REX_LOAD_U32(obj + 144);
  const int state = REX_LOAD_U16(obj + 148);
  if (state != g.last_state) {
    REXLOG_INFO("dbz3 hub: menu principal estado {} (filas {}, cursor {})", state, REX_LOAD_U8(obj + 151),
                REX_LOAD_U8(obj + 150));
    if (state == 2 && g.fade == Fade::kNone) g.focus = g.open = g.leaving = false;   // menu nuevo: nada elegido
    g.last_state = state;
  }
  if (g.fade != Fade::kNone) FadeStep(ctx, base, obj);
  PlayQueuedSe(ctx, base);   // el de una pantalla propia (QueueSe)
  const bool other = OtherScreen();
  if (g.fade == Fade::kNone) {
    if (g.back && !g.armed && state != 3) g.armed = true;   // ya se fue del menu: al volver, el HUB
    if (state == 3 && !other && g.back && g.armed) {
      g.back = g.armed = g.leaving = false;
      g.open = g.focus = true;
    }
    if (g.leaving && state == 3 && ++g.leave_frames > 180) {   // el modo no arranco: se ve el menu
      g.leaving = false;
      g.focus = true;
    }
    const uint32_t pad = kPad + REX_LOAD_U32(kPadIdx) * 184;
    if (state == 3 && !other && Input(ctx, base, obj, pad)) {
      REX_STORE_U16(pad + 14, 0);
      REX_STORE_U16(pad + 18, 0);
      REX_STORE_U16(pad + 114, 0);
    }
  }
  if (OwnsBubble()) Bubble(base, -1);
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_view = View{true,  state, other,    g.focus, g.open, g.leaving || (g.back && g.armed), g.cursor, g.fade,
                  g.to_hub, g.t0, REX_LOAD_U8(obj + 151)};
  }
  g_seen_ms = NowMs();
  g_session = g.open || g.back || g.leaving || g.fade != Fade::kNone;
}

bool Screen() { return g.open || g.leaving || (g.back && g.armed); }

}  // namespace
}  // namespace dbz3::hub

// Fondo del menu principal: lo primero que dibuja sub_8218BBC0. Aqui decide el HUB (mando, fundido).
DBZ3_HOOK_EU(sub_8218B2F0) {
  using namespace dbz3::hub;
  if (Enabled() && ctx.r3.u32) Frame(ctx, base, ctx.r3.u32);
  orig(ctx, base);
}

// Filas del menu principal: el juego dibuja sin sus etiquetas (+151 a 0) en la pantalla HUB y sin franja
// (+120) ni fila elegida con el cursor en el HUB.
DBZ3_HOOK_EU(sub_8218B550) {
  using namespace dbz3::hub;
  const uint32_t obj = ctx.r3.u32;
  if (!Enabled() || !obj) {
    orig(ctx, base);
    return;
  }
  const uint8_t rows = REX_LOAD_U8(obj + 151);
  const uint32_t stripe = REX_LOAD_U32(obj + 120);
  const uint8_t row = REX_LOAD_U8(obj + 150);
  if (Screen() || g.focus) {   // ninguna fila nativa elegida: ni franja ni etiqueta a pleno brillo
    REX_STORE_U32(obj + 120, 0);
    REX_STORE_U8(obj + 150, 0xFF);
  }
  if (Screen()) REX_STORE_U8(obj + 151, 0);
  orig(ctx, base);
  REX_STORE_U32(obj + 120, stripe);
  REX_STORE_U8(obj + 151, rows);
  REX_STORE_U8(obj + 150, row);
}
// Cinta del titulo: en la pantalla HUB sin su texto "Menu principal" (hub_panel.cpp pone el logo encima).
DBZ3_HOOK_EU(sub_8218B3F0) {
  using namespace dbz3::hub;
  const uint32_t obj = ctx.r3.u32;
  if (!Enabled() || !obj || !Screen()) {
    orig(ctx, base);
    return;
  }
  const uint32_t text = REX_LOAD_U32(obj + kTitleSlot);
  REX_STORE_U32(obj + kTitleSlot, 0);
  orig(ctx, base);
  REX_STORE_U32(obj + kTitleSlot, text);
}

// Espera del menu sin mando: al callar la voz de bienvenida pone la ayuda y la voz de su fila. Con la
// fila o la pantalla HUB, ni una ni otra.
DBZ3_HOOK_EU(sub_8218B998) {
  using namespace dbz3::hub;
  if (Enabled() && (g.focus || g.open)) return;
  orig(ctx, base);
}

// Entrada del menu principal (tarea; +48 el menu): al acabar de entrar pone el saludo de Goku (voz 0x20000|4329 en
// +164) y pasa al estado activo (3). Si se vuelve de un modo lanzado desde el HUB (que se reabre ahora), se calla
// esa voz en el mismo cuadro: el saludo queda solo para el arranque y la vuelta normal (feedback 1.4.6, punto 3).
DBZ3_HOOK_EU(sub_8218C8E0) {
  using namespace dbz3::hub;
  const uint32_t obj = ctx.r3.u32 ? REX_LOAD_U32(ctx.r3.u32 + 48) : 0;
  const bool antes = obj && REX_LOAD_U16(obj + 148) == 3;
  PPCContext c = ctx;
  orig(ctx, base);
  if (Enabled() && obj && !antes && REX_LOAD_U16(obj + 148) == 3 && g.back && g.armed && REX_LOAD_U32(obj + 164)) {
    Call(c, base, DBZ3_CALL_EU(sub_82081A48), REX_LOAD_U32(obj + 164));
    REXLOG_INFO("dbz3 hub: vuelta al HUB sin el saludo del menu principal");
  }
}

// Texto del bocadillo de un menu: el del menu principal es del HUB mientras lo usa (el juego pone el
// saludo al terminar de entrar y la ayuda de su fila al moverse).
DBZ3_HOOK_EU(sub_8217E800) {
  using namespace dbz3::hub;
  if (Enabled() && ctx.r3.u32 == g.bubble && ctx.r4.s16 != -1 && OwnsBubble()) return;
  orig(ctx, base);
}

// Estado de la tarea de escenas tras un combate (ya en negro). Con HoldChain (hub.cpp) se repite en cada tick mientras
// una pantalla propia espera entre combates encadenados (tienda, cartas, cuadro...): aqui suenan sus sonidos (QueueSe),
// que el menu principal no puede tocar porque no esta. Nadie mas engancha esta funcion (comprobado 2026-10-15).
// Medido (2026-10-15): en esta espera el gestor de sonido acepta el sonido (voz nueva, banco del reproductor 0 listo,
// estado 3) pero la salida es 0: al acabar el combate el juego baja el volumen general con su fundido de sonido
// (sub_821D27C8 -> sub_821652D0) y lo vuelve a subir al crear la escena siguiente (el menu principal: sub_82165218 en
// sub_8218C9F0). En nuestra espera no hay escena siguiente: la primera vez que una pantalla propia pide un sonido se
// hace la misma subida que el juego (sub_82165390, fundido de entrada del sonido), una vez por espera.
DBZ3_HOOK_EU(sub_821D6A08) {
  orig(ctx, base);
  static int64_t last = 0;
  static bool subido = false;
  const int64_t now = dbz3::hub::ClockMs();
  if (now - last > 1000) subido = false;   // una espera nueva (o el primer tick tras otro combate)
  last = now;
  if (!subido && dbz3::hub::SePending()) {
    subido = true;
    PPCContext c = ctx;
    c.r3.s64 = -1;   // la musica que hubiera (ninguna)
    c.r4.u64 = 0;
    DBZ3_CALL_EU(sub_82165390)(c, base);
    REXLOG_INFO("dbz3 hub: espera tras el combate: vuelve el volumen del sonido (para las pantallas propias)");
  }
  dbz3::hub::PlayQueuedSe(ctx, base);
}
