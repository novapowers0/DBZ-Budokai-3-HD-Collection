// dbz3 - Pasarela de modos (2): entrada "Pasarela" del HUB con contenido de Budokai 1 y 2 en el motor de B3.
//   - Saga de Vegeta en Namek (Budokai 1, ficha 04): los combates de la historia de B1 leidos de la ISO del jugador
//     (tabla del ELF, pasarela_b1.cpp), con Zarbon importado de B1 por el Mod Kit (personaje de su mod).
//   - Dragon World de Budokai 2 (ficha 17): port del tablero con sus graficos y reglas leidos de la ISO del jugador
//     (pasarela_b2.cpp); aqui solo se piden sus combates.
//   - ¡Fusion fallida! (ficha 20): mision original con Gotenks gordo (port de B2 instalado con el Mod Kit).
//   - Las absorciones de Super Bu (ficha 21): Bu absorbe a quien vence y antes de cada combate se elige con que
//     absorcion luchar (trajes de Bu absorbido); al salir, Bu vuelve a ser el de siempre en la seleccion.
//   - Misiones del Dragon de Infinite World (ficha 02 C): las 96 de la tabla del ELF de la copia del jugador
//     (pasarela_iw.cpp: rival, escenario, tiempo, condicion) con su titulo y texto (DATA_USA.AFS).
// Todo con piezas del juego: combate nativo (dbz3::hub::RequestBattleEx), caras del marcador, marco, bocadillo y
// letra de los menus (game_style). Textos propios (es/en). Nada de B1/B2 va en la version: la saga lee la ISO del
// jugador y los personajes importados son mods suyos; si falta algo, la entrada lo dice y no empieza.
// Frente 08 (nave de Babidi 1:1, studies 3): los combates de la nave llevan el escenario de B2 (registro del selector,
// negativo, como el tablero) y la prueba I encadena (sub_820FE9B0 + hub::ChainBattleEx): ver CombateNave y el gancho.
// Avance en user_data/dbz3/pasarela/progreso.toml (claves saga_b1, tablero_*, gotenks, buu*). US y EU.
// Sin ganchos propios. Docs: docs/16_pasarela_modos/04_misiones_y_tablero.md.
#include "pasarela.h"
#include "pasarela_iw.h"
#include "iw_minijuegos.h"
#include "iw_mini_vuelo.h"
#include "cinematica.h"
#include "video_sfd.h"
#include "mods_opcionales.h"
#include "ingame/tutorial.h"

#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // net/netplay_hooks.cpp (-1 = el mando de verdad)
}
namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
bool ChainBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
void HoldChain(bool hold);
bool OwnBattle();
bool RegisterMode(const char* key, void (*open)(), bool (*busy)());   // hub_menu.cpp
bool RegisterGate(const char* key, bool (*ready)(), const char* (*why)());
}  // namespace dbz3::hub
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::modos {
void UltimoDuelo(int* id, int* traje);   // modos.h: luchador del ultimo Duelo
bool EsJugable(int id);
}  // namespace dbz3::modos
namespace dbz3::rpg {
void Premio(int zeni);   // Zenie al monedero del HUB (modos.h); IW: lo ganado en cada mision
}
namespace dbz3::ingame {
int GameScreen();   // online_panel.cpp
void PilotarAlDuelo(int fila);                                // tagteam_panel.h (piloto a la rueda del Duelo)
void RegistrarEleccion(void (*aviso)(int jugador, int id));  // tagteam_panel.h (personaje confirmado en la rueda)
}
#include "ingame/menu_pilot.h"
bool dbz3_select_live();   // select_ext.cpp: la rueda del select en pantalla
namespace dbz3::pasarela::b2 {   // Dragon World de B2 (pasarela_b2.cpp; todo con g_mu)
bool Disponible();
void Entrar();
int Dibujar(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pressed, bool auto_on, int out[14]);
void Resultado(int ganador, int vida);
int FormaTableroB3(int id_b3, int forma_b2);
}  // namespace dbz3::pasarela::b2
namespace dbz3::pasarela::nave {   // Nave de Babidi de B2 (pasarela_nave.cpp; todo con g_mu)
bool Disponible();
bool Abierta();
void Entrar();
int Dibujar(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pressed, bool auto_on, int out[6]);
void Resultado(int ganador, int vida, int segundos, int golpes);
// Prueba I encadenada (frente 08): el combate siguiente se pide en el fin del actual (gancho de abajo) y CombateNave
// lo arranca sin volver a pedirlo.
bool Siguiente(int out[6], int vida);
void Seguido(int vida);
bool Encadenado();
bool Seguir();
}  // namespace dbz3::pasarela::nave

namespace dbz3::pasarela::misiones {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

constexpr int kMainScreen = 5;
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kLB = 0x0100,
                   kRB = 0x0200, kA = 0x1000, kB = 0x2000;
constexpr float kAutoSeg = 3.0f;   // prueba (DBZ3_TEST_MISIONES=...,auto): segundos por pantalla
// Mapas de B3 (entrada de data_cmn del escenario; EscenarioPorMapa da el registro). Medidos 2026-10-15
// (DBZ3_TEST_MISIONES): 46 BUDO, 58 MIND & TIME, 56 ISLAND, 68 TOWN, 64 ROCK, 62 PLANE, 52 GOKU, 60 NAMEC, 50 CELL,
// 54 KAIOU, 48 BUU BODY, 66 TOWN B, 44 R&R BASE.
constexpr int kMapaCamara = 58, kMapaPradera = 62, kMapaRocas = 64, kMapaCiudad = 68, kMapaKaio = 54;
// IDs de B3
constexpr int kGoku = 0, kGohanJoven = 3, kGohan = 4, kGoten = 6, kKrilin = 10, kVegeta = 7, kPiccolo = 11, kRaditz = 18, kNappa = 19, kMajinBu = 34,
              kSuperBu = 35, kSaibaman = 42;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// ---- Contenido ----
enum Modo : int { kSagaB1, kTablero, kGotenks, kBuu, kIW, kNave, kModos };
const char* const kClaves[kModos] = {"saga_b1", "tablero", "gotenks", "buu", "iw", "nave"};

struct Texto {
  const char *es = "", *en = "";
  const char* operator()() const { return T::T(es, en); }
};
enum TipoPaso : int { kCharla, kCombate, kEleccion, kFinal, kInforme };
struct Paso {
  int tipo = kCharla;
  int quien = -1;          // charla/final: quien habla (su cara)
  bool derecha = false;    // charla: cara a la derecha (el rival)
  Texto texto;             // charla/final: lo que dice; combate: aviso de la tarjeta
  std::string texto_propio;   // en lugar de `texto` (charlas armadas con nombres)
  int jugador = 0, forma1 = 0, rival = 0, forma2 = 0, mapa = kMapaPradera, tiempo = 0, vida2 = 0;
  Reglas reglas;
  int desbloquea = -1;     // Bu: absorcion que gana al vencer (bit)
  int informe = -1;        // IW: mision (indice de g_iw) cuyo titulo y texto se ensenan
  int vida_minima = 0;     // IW, reto sin atacar: vida con la que hay que acabar
  int iw = 0;              // IW: 1 "Fight" (vale ganar o aguantar el tiempo), 2 dejar ganar al rival
  int escenario = -1;      // registro de escenario directo (altas de mods); -1 = el de `mapa`
};

// --- Saga de B1: los combates de la historia de Budokai 1 de la ISO del jugador ---
// Hilo de la saga: los registros seguidos del mismo protagonista desde el primero contra Zarbon (B1 13): en B1 es la
// historia de Vegeta en Namek (Zarbon transformado, Recoome, Freezer y Freezer en su forma final).
constexpr int kZarbonB1 = 13, kDodoriaB1 = 14;
int LuchadorB1(int id_b1) {
  if (id_b1 == kZarbonB1 || id_b1 == kDodoriaB1) return IdPort("b1", std::to_string(id_b1));
  return B1aB3(id_b1);
}
std::vector<CombateB1> HiloSaga(const DatosB1& d) {
  std::vector<CombateB1> out;
  for (size_t i = 0; i < d.historia.size(); ++i) {
    if (d.historia[i].rival_b1 != kZarbonB1) continue;
    out.push_back(d.historia[i]);
    for (size_t j = i + 1; j < d.historia.size(); ++j) {
      const CombateB1& c = d.historia[j];
      if (c.jugador_b1 != out.back().jugador_b1 || c.capitulo != out.back().capitulo + 1) break;
      out.push_back(c);
    }
    break;
  }
  return out;
}
// Lo que dicen antes de cada combate (textos propios; las escenas de B1 no se copian).
struct Frases {
  int rival_b1, forma_b1;
  Texto jugador, rival;
};
const Frases kFrasesSaga[] = {
    {kZarbonB1, -1, {"Zarbon... Esta vez no me vas a pillar por sorpresa. ¡Enséñame esa cara de monstruo!",
                     "Zarbon... This time you won't catch me off guard. Show me that monster face!"},
     {"¿Has vuelto a por más, mono? Te mandaré al fondo del lago otra vez.",
      "Back for more, monkey? I'll send you to the bottom of the lake again."}},
    {12, -1, {"Las Fuerzas Especiales... ¡Apártate, payaso!", "The Special Squadron... Out of my way, clown!"},
     {"¡Recoome no ha perdido nunca! ¡Recoome... ultra fighting bomber!", "Recoome never loses! Recoome... ultra fighting bomber!"}},
    {15, 0, {"¡Freezer! Ahora soy mucho más fuerte. ¡Ha llegado tu hora!", "Frieza! I'm much stronger now. Your time has come!"},
     {"Qué pesado eres, Vegeta. Te recordaré cuál es tu sitio.", "You're so tiresome, Vegeta. Let me remind you of your place."}},
    {15, 1, {"¡Soy el príncipe de los Saiyans! ¡Ni tu forma final me detendrá!",
             "I'm the prince of all Saiyans! Not even your final form will stop me!"},
     {"Esta es mi forma final. Ya no tienes nada que hacer.", "This is my final form. You don't stand a chance."}}};

// --- ¡Fusion fallida! (Gotenks gordo, port de B2: origen b2 / XGXL) ---
int IdGotenksGordo() { return IdPort("b2", "XGXL"); }

// --- Absorciones de Super Bu ---
// Las absorciones de B3 son formas especiales de Super Bu (tipo 7, capsula 123 "Absorcion": modelos 84, 78, 79) que
// solo nacen en combate: pedidas como forma inicial el juego se cae al cargar (medido 2026-10-15). Por eso cada
// absorcion es un traje extra de Super Bu con el modelo del juego del Bu absorbido (traje.toml con "juego:N", mods de
// mod center hd/campanas/trajes_bu/): se elige en la seleccion como cualquier traje y el 1.o es el Bu de siempre.
struct Absorcion {
  const char* carpeta;   // mod del traje ("" = el traje normal)
  Texto nombre;
};
const Absorcion kAbsorciones[] = {{"", {"Súper Bu", "Super Buu"}},
                                  {"bu_absorbido_piccolo", {"Bu (Piccolo absorbido)", "Buu (Piccolo absorbed)"}},
                                  {"bu_absorbido_gotenks", {"Bu (Gotenks absorbido)", "Buu (Gotenks absorbed)"}},
                                  {"bu_absorbido_gohan", {"Bu (Gohan absorbido)", "Buu (Gohan absorbed)"}}};
constexpr int kNAbsorciones = int(std::size(kAbsorciones));
int TrajeBu(int k) { return k <= 0 ? 0 : TrajeExtra(kSuperBu, kAbsorciones[k].carpeta); }
constexpr int kFormaGotenSsj = 1, kFormaGohanDefinitivo = 3, kFormaGokuSsj3 = 4;   // tabla de formas (medida)

// --- Misiones del Dragon de Infinite World (ficha 02 C, ronda 3) ---
// Las 96 misiones de la tabla del ELF de la copia del jugador (pasarela_iw.cpp: jugador, rival, escenario, tiempo y
// condicion) con su titulo y texto de DATA_USA (entrada 131 + numero). "Endure": la vida minima de IW esta sin
// descifrar (ponytail: 1000 de 3000 con el COM en Novato).
std::mutex g_iw_mu;
std::shared_ptr<const DatosIW> g_iw;
std::vector<MisionTablaIW> g_iw_tabla;   // con g_iw_mu; se rellena junto con g_iw
std::atomic<bool> g_iw_leyendo{false};
std::atomic<uint32_t> g_iw_gen{0};
std::shared_ptr<const DragonMissionIW> g_dm;   // con g_iw_mu: mapa, iconos y piezas de Dragon Mission (P1)
std::shared_ptr<const DatosIW> IW() {
  std::lock_guard<std::mutex> lk(g_iw_mu);
  return g_iw;
}
std::shared_ptr<const DragonMissionIW> DM() {
  std::lock_guard<std::mutex> lk(g_iw_mu);
  return g_dm;
}
void LeerIW() {   // en un hilo; una vez (o otra vez si fallo)
  if (IW() || g_iw_leyendo.exchange(true)) return;
  std::thread([] {
    auto d = std::make_shared<DatosIW>();
    std::string error;
    const auto ruta = BuscarIW();
    std::vector<MisionTablaIW> tabla = ruta.empty() ? std::vector<MisionTablaIW>() : LeerTablaIW(ruta, error);
    std::vector<int> numeros;
    for (const auto& m : tabla) {
      numeros.push_back(m.numero - 1);
      REXLOG_INFO("dbz3 misiones: IW mision {}: condicion {}, jugador {} (forma {}) contra {} (forma {}), escenario {} "
                  "(mapa {}), tiempo {}", m.numero, m.condicion, m.jugador, m.forma1, m.rival, m.forma2, m.escenario,
                  MapaIW(m.escenario), m.tiempo);
    }
    const bool ok = !tabla.empty() && LeerMisionesIW(ruta, numeros, *d, error);
    auto dm = std::make_shared<DragonMissionIW>();
    std::string error_dm;
    const bool ok_dm = ok && LeerDragonMission(ruta, *dm, error_dm);
    REXLOG_INFO("dbz3 misiones: Dragon Mission: {}", ok_dm ? std::to_string(dm->casillas.size()) + " casillas, " +
                                                                std::to_string(dm->iconos.size()) + " iconos"
                                                          : error_dm);
    if (ok) {
      std::lock_guard<std::mutex> lk(g_iw_mu);
      g_iw = d;
      g_iw_tabla = std::move(tabla);
      if (ok_dm) g_dm = dm;
      ++g_iw_gen;
    }
    int imagenes = 0;
    for (const auto& mi : d->misiones) imagenes += (mi.titulo ? 1 : 0) + (mi.texto ? 1 : 0);
    REXLOG_INFO("dbz3 misiones: Infinite World {}: {}", ruta.empty() ? std::string("no esta") : ruta.filename().string(),
                ok ? std::to_string(d->misiones.size()) + " misiones, " + std::to_string(imagenes) + " imagenes" : error);
    g_iw_leyendo = false;
  }).detach();
}
// Por que una mision de IW no se juega en B3 (nullptr = jugable).
constexpr int kMisionesDM = 96;   // 97-201: Fighter's Road
int g_iw_luchador = 0;           // Fighter's Road: el luchador elegido (B3)
// El aviso dice a quien falta y como conseguirlo: la receta del Kit lo crea desde la copia de IW del jugador (Nova's Mods
// solo reparte recetas y retoques, nunca modelos de IW). Cadenas guardadas: el puntero vive lo que el juego.
const char* FaltaIW(int id_iw) {
  static const std::pair<int, const char*> kNombre[] = {{44, "Goku GT"}, {45, "Baby"}, {46, "Super 17"}, {47, "Janemba"},
                                                         {48, "Pikkon"}, {54, "Vegeta GT"}, {55, "Gran Saiyaman 2"},
                                                         {56, "Pan"}, {70, "Gogeta SS4"}};
  static std::mutex mu;
  static std::map<int, std::string> hechas;
  std::lock_guard<std::mutex> lk(mu);
  auto it = hechas.find(id_iw);
  if (it == hechas.end()) {
    const char* n = "?";
    for (const auto& [id, nombre] : kNombre)
      if (id == id_iw) n = nombre;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  T::T("Necesita a %s: instálalo en el lanzador > Nova's Mods > Pack Infinite World (se crea con tu "
                       "copia de IW)",
                       "Needs %s: install it in the launcher > Nova's Mods > Infinite World pack (built from your IW "
                       "copy)"),
                  n);
    it = hechas.emplace(id_iw, buf).first;
  }
  return it->second.c_str();
}
const char* MotivoIW(const MisionTablaIW& t) {
  if (t.numero > kMisionesDM)   // Fighter's Road: el jugador es el elegido; solo cuenta el rival
    return IWaB3(t.rival) < 0 ? FaltaIW(t.rival) : nullptr;
  // Training No.2 (guardia, esquiva y teletransporte): jugable (VigilarEntreno2, verificado entero: iw_fix 2026-10-17)
  // R7: No.4 jugable (agarres, verificado en iw_fix); No.8 (hiper + Dragon Rush) tiene vigilante pero sin probar entero
  // (el piloto no consigue que el golpe fuerte en hiper entre): sigue en gris salvo con la sonda
  if (t.condicion == -1 && t.numero != 1 && t.numero != 2 && t.numero != 4 && t.numero != 6 && t.numero != 9 &&
      !std::getenv("DBZ3_R1_TRAZA"))
    return T::T("entrenamiento de mandos de IW: guardia, esquiva, teletransporte, persecución; sin portar",
                "IW controls training: guard, dodge, teleport, pursuit; not ported");
  if (t.condicion < -1 || t.condicion > 3) return T::T("regla de IW sin descifrar", "IW rule not decoded");
  // R9 (cuarta y quinta vuelta): las definitivas de IW van sobre el guion del donante (roster_build iw_guion_donante,
  // IW_TECNICAS_DONANTE para Baby); Goku GT, Baby, Vegeta GT y Pan pasan combates con definitivas.
  if (IWaB3(t.jugador) < 0) return FaltaIW(t.jugador);
  if (IWaB3(t.rival) < 0) return FaltaIW(t.rival);
  return nullptr;
}
// Combate de una mision jugable de IW con las reglas de su condicion; `regla` = la condicion en el idioma del juego.
Paso PasoIW(const MisionTablaIW& t, std::string& regla) {
  char txt[256];
  if (t.numero > kMisionesDM) {   // Fighter's Road: combates seguidos con el luchador elegido (K.O.)
    std::snprintf(txt, sizeof(txt), T::T("Fighter's Road, combate %d de 105: deja K.O. al rival.",
                                         "Fighter's Road, battle %d of 105: knock out your opponent."),
                  t.numero - kMisionesDM);
  } else if (t.condicion == -1 && t.numero == 2) {   // "Training No.2" (DATA_USA 133): guardia, esquiva, teletransporte
    std::snprintf(txt, sizeof(txt), "%s", T::T("Para 5 golpes (G), esquiva 5 (G justo al golpe) y teletranspórtate 5 (G + izquierda/derecha justo al golpe) en 180 s.",
                                               "Guard 5 attacks (G), dodge 5 (G right at the hit) and teleport 5 times (G + left/right at the hit) in 180 s."));
  } else if (t.condicion == -1 && t.numero == 4) {   // "Training No.4" (DATA_USA 135): agarres (VigilarEntrenoIW)
    std::snprintf(txt, sizeof(txt), "%s", T::T("Haz 3 agarres (P + G junto al rival) en 180 s.",
                                               "Perform 3 throws (P + G next to your opponent) in 180 s."));
  } else if (t.condicion == -1 && t.numero == 8) {   // "Training No.8" (DATA_USA 139): Aura Burning = modo hiper de B3
    std::snprintf(txt, sizeof(txt), "%s", T::T("Entra en modo híper (P + K + G + E) y haz 3 Dragon Rush en 180 s.",
                                               "Enter Hyper Mode (P + K + G + E) and perform 3 Dragon Rushes in 180 s."));
  } else if (t.condicion == -1 && t.numero == 6) {   // "Training No.6" (DATA_USA 137): solo ki, super y definitiva
    std::snprintf(txt, sizeof(txt), "%s", T::T("Solo valen las bolas de ki, los súper ataques y la definitiva: deja K.O. al rival en 180 s.",
                                               "Only Ki blasts, Super and Ultimate attacks count: knock out your opponent in 180 s."));
  } else if (t.condicion == -1 && t.numero == 9) {   // "Training No.9" (DATA_USA 140): transformarse y ganar
    std::snprintf(txt, sizeof(txt), "%s", T::T("Transfórmate (Kaioken) con 2 barras de ki o más y deja K.O. al rival en 180 s.",
                                               "Transform (Kaioken) with 2 or more Ki bars and knock out your opponent in 180 s."));
  } else if (t.condicion == -1) {   // "Training No.1": 30 golpes basicos en 180 s
    std::snprintf(txt, sizeof(txt), "%s", T::T("Da 30 golpes básicos (P y K) antes de que pasen 180 segundos.",
                                               "Land 30 basic attacks (P and K) before 180 seconds are up."));
  } else if (t.condicion == 0) {
    std::snprintf(txt, sizeof(txt), "%s", T::T("Deja K.O. al rival. No hay límite de tiempo.",
                                               "Knock out your opponent. There is no time limit."));
  } else if (t.condicion == 1) {
    std::snprintf(txt, sizeof(txt), T::T("Baja la vida del rival o aguanta sin caer hasta que acabe el tiempo (%d s).",
                                         "Wear your opponent down or hold out until time runs out (%d s)."),
                  t.tiempo);
  } else if (t.condicion == 2) {
    std::snprintf(txt, sizeof(txt), "%s", T::T("Deja que el rival te gane: que te saque del ring.",
                                               "Let your opponent win: get knocked out of the ring."));
  } else {
    std::snprintf(txt, sizeof(txt), T::T("No ataques y acaba los %d segundos con 1000 de vida o más.",
                                         "Don't attack and finish the %d seconds with 1000 health or more."),
                  t.tiempo);
  }
  regla = t.numero > kMisionesDM ? std::string(txt)
                                 : std::string(T::T("Misión ", "Mission ")) + std::to_string(t.numero) + ": " + txt;
  Paso x;
  x.tipo = kCombate;
  x.jugador = t.numero > kMisionesDM ? g_iw_luchador : IWaB3(t.jugador);
  x.rival = IWaB3(t.rival);
  if (t.numero <= kMisionesDM) x.forma1 = FormaFusionIW(t.jugador);   // s7 06: fusion nativa de B3 (0 si no lo es)
  x.mapa = MapaIW(t.escenario);
  x.texto = {"Misión de Infinite World.", "Infinite World mission."};
  x.informe = t.numero - 1;
  x.tiempo = t.tiempo;   // -1 = sin limite
  if (t.condicion == -1 && t.numero == 2) {   // Training No.2: VigilarEntreno2 cuenta y, al cumplir, K.O. (Success)
    x.tiempo = std::getenv("DBZ3_R1_ENTRENO2_TIEMPO") ? std::atoi(std::getenv("DBZ3_R1_ENTRENO2_TIEMPO")) : 180;   // prueba
    x.reglas.nivel_com = 1;   // que ataque
    x.iw = 4;
  } else if (t.condicion == -1 && (t.numero == 4 || t.numero == 8)) {   // Training No.4 / No.8: VigilarEntrenoIW
    x.tiempo = 180;
    x.reglas.nivel_com = 0;
    x.iw = t.numero == 4 ? 5 : 6;
  } else if (t.condicion == -1 && t.numero == 6) {   // Training No.6. ponytail: IW no deja dar golpes; aqui los golpes hacen el
    x.tiempo = 180;                           // 25 % (Reglas::solo_ki, la de Mr. Satan herido de la Leyenda)
    x.reglas.nivel_com = 0;
    x.reglas.solo_ki = true;
  } else if (t.condicion == -1 && t.numero == 9) {   // Training No.9: gana si se transformo (VigilarFormaIW)
    x.tiempo = 180;
    x.reglas.nivel_com = 0;
    x.iw = 3;
  } else if (t.condicion == -1 && t.numero <= kMisionesDM) {
    x.tiempo = 180;
    x.reglas.golpes = 30;
    x.reglas.nivel_com = 0;
  } else if (t.condicion == 1) {   // "Fight": KO o aguantar el tiempo con la vida del nivel (+0x04 = barras x1000)
    x.iw = 1;
    x.vida_minima = t.nivel > 0 ? t.nivel * 1000 : 1000;
  } else if (t.condicion == 2) {
    x.iw = 2;
  } else if (t.condicion == 3) {
    x.reglas.sin_atacar = true;
    x.reglas.nivel_com = 0;
    x.vida_minima = t.nivel > 0 ? t.nivel * 1000 : 1000;   // +0x04 barras (las 3 de IW: 1 = 1000 de 3000)
  }
  return x;
}

// --- Tablero: Dragon World de B2 (pasarela_b2.cpp lee la ISO, lo dibuja y aplica sus reglas) ---

// ---- Estado (g_mu) ----
enum Fase : int { kNada = 0, kMenu, kPantalla, kLuchando, kPerdido, kTableroF, kMapaIW, kNaveF };
const char* const kFaseNombre[] = {"nada", "menu", "pantalla", "combate", "perdido", "tablero", "mapa de IW", "nave"};

std::mutex g_mu;
int g_fase = kNada, g_modo = 0, g_cursor = 0, g_intentos = 0;
std::vector<Paso> g_pasos;
int g_paso = 0;
uint64_t g_cambio = 0;
std::atomic<int> g_fase_pub{kNada};
std::atomic<bool> g_auto{false};
// marcador del reto en el combate (IW): 0 nada, 1 golpes (g_hud_golpes de g_hud_meta), 2 aguantar sin atacar
std::atomic<int> g_hud_tipo{0}, g_hud_meta{0}, g_hud_golpes{0};
std::atomic<bool> g_panel{false};
// tablero: vida que le queda al del jugador en el combate pedido (0 = llena)
int g_vida = 0;
int g_nave_prueba = -1;   // prueba de la nave en curso (CombateNave c[5])
bool g_nave_armada = false;      // el fin del combate de la prueba I ya encadeno el siguiente (gancho sub_820FE9B0)
bool g_nave_encadenada = false;  // el combate de la nave lo pidio la cadena: Lanzar no vuelve a pedirlo
// Bu
int g_absorbidos = 1;    // bit por absorcion (la 0, Super Bu, siempre)
int g_forma_bu = 0;

void Publicar(int fase) {   // con g_mu
  if (fase != g_fase || fase == kPantalla) {
    std::string que;
    if (fase == kPantalla && g_paso < int(g_pasos.size())) {
      const Paso& p = g_pasos[size_t(g_paso)];
      que = p.tipo == kCharla ? " (charla)" : p.tipo == kCombate ? " (combate)" : p.tipo == kEleccion ? " (eleccion)"
            : p.tipo == kInforme ? " (informe)" : " (final)";
    }
    REXLOG_INFO("dbz3 misiones: {} paso {}/{}{}: {}", g_fase == kNada && fase == kMenu ? "menu" : kClaves[g_modo],
                g_paso + 1, g_pasos.size(), que, kFaseNombre[fase]);
  }
  g_fase = fase;
  g_fase_pub = fase;
  ++g_cambio;
}

// Disponible y por que no (para el menu).
bool Listo(int m, std::string* motivo) {
  auto no = [&](const char* es, const char* en) {
    if (motivo) *motivo = T::T(es, en);
    return false;
  };
  if (m == kSagaB1) {
    const auto d = DatosLeidos();
    if (!d) return no("Necesita tu copia de Dragon Ball Z Budokai (PS2).",
                      "Needs your copy of Dragon Ball Z Budokai (PS2).");
    if (HiloSaga(*d).empty()) return no("Tu ISO de Budokai 1 no trae la historia de Namek.", "Your Budokai 1 ISO has no Namek story.");
    if (IdPort("b1", std::to_string(kZarbonB1)) < 0)
      return no("Importa a Zarbon de Budokai 1 con el Mod Kit (Importar personaje > Budokai 1).",
                "Import Zarbon from Budokai 1 with the Mod Kit (Import character > Budokai 1).");
  }
  if (m == kNave && !nave::Disponible())
    return no("Necesita tu copia de Dragon Ball Z Budokai 2 (PS2).",
              "Needs your copy of Dragon Ball Z Budokai 2 (PS2).");
  if (m == kNave && !nave::Abierta())   // como en B2: se abre con un deseo a Shenron
    return no("Junta las 7 Bolas en el Dragon World y pídele a Shenron el portal de la nave de Babidi.",
              "Gather the 7 Dragon Balls in Dragon World and wish for the portal to Babidi's ship.");
  if (m == kTablero && !b2::Disponible())
    return no("Necesita tu copia de Dragon Ball Z Budokai 2 (PS2).",
              "Needs your copy of Dragon Ball Z Budokai 2 (PS2).");
  if (m == kBuu) {
    for (int k = 1; k < kNAbsorciones; ++k) {
      if (TrajeBu(k) < 0)
        return no("Instala los trajes de Bu absorbido (Mod Kit: mods de mod center hd/campanas/trajes_bu).",
                  "Install the absorbed Buu costumes (Mod Kit: mods in mod center hd/campanas/trajes_bu).");
    }
  }
  if (m == kIW && !IW()) {
    LeerIW();
    return no("Necesita tu copia de Dragon Ball Z Infinite World (PS2).",
              "Needs your copy of Dragon Ball Z Infinite World (PS2).");
  }
  if (m == kGotenks && IdGotenksGordo() < 0)
    return no("Importa a Gotenks gordo de Budokai 2 con el Mod Kit (Importar personaje > Budokai 2).",
              "Import Fat Gotenks from Budokai 2 with the Mod Kit (Import character > Budokai 2).");
  if (motivo) motivo->clear();
  return true;
}

const Texto kTitulos[kModos] = {{"Saga de Vegeta en Namek (Budokai 1)", "Vegeta's Namek saga (Budokai 1)"},
                                {"Dragon World (Budokai 2) - BETA", "Dragon World (Budokai 2) - BETA"},
                                {"¡Fusión fallida! (Gotenks gordo)", "Failed fusion! (Fat Gotenks)"},
                                {"Las absorciones de Súper Bu", "Super Buu's absorptions"},
                                {"Misiones del Dragón (Infinite World)", "Dragon Mission (Infinite World)"},
                                {"Nave de Babidi (Budokai 2) - BETA", "Babidi's Spaceship (Budokai 2) - BETA"}};
const Texto kAyudas[kModos] = {
    {"Los combates de la historia de Budokai 1 de tu copia del juego, con Zarbon importado de Budokai 1.",
     "The story battles from your copy of Budokai 1, with Zarbon imported from Budokai 1."},
    {"En construcción. El tablero de Budokai 2 de tu copia: mueve a Goku y a su compañero casilla a casilla, busca objetos y lucha al tocar a un rival.",
     "Work in progress. The Budokai 2 board from your copy: move Goku and his partner space by space, search for items and fight when you touch a rival."},
    {"Una misión nueva: la fusión sale mal, pero la Tierra no puede esperar.",
     "A new mission: the fusion goes wrong, but the Earth can't wait."},
    {"Bu absorbe a quien vence. Antes de cada combate, elige con qué absorción luchar.",
     "Buu absorbs whoever he beats. Before each fight, choose which absorption to fight with."},
    {"El mapa de Dragon Mission de tu copia de Infinite World: elige una misión en el mapa y cúmplela.",
     "The Dragon Mission map from your copy of Infinite World: pick a mission on the map and clear it."},
    {"Las 4 pruebas de la nave de Budokai 2: gana Kili y consigue los premios de Babidi.",
     "The 4 trials of the Budokai 2 ship: earn Kili and get Babidi's prizes."}};

// Pasos lineales de cada modo (el tablero arma los suyos al llegar a una casilla).
std::vector<Paso> PasosDe(int m) {
  std::vector<Paso> p;
  auto charla = [&](int quien, bool der, Texto t) {
    Paso x;
    x.tipo = kCharla;
    x.quien = quien;
    x.derecha = der;
    x.texto = t;
    p.push_back(x);
  };
  auto combate = [&](int j, int f1, int r, int f2, int mapa, Texto aviso) {
    Paso x;
    x.tipo = kCombate;
    x.jugador = j;
    x.forma1 = f1;
    x.rival = r;
    x.forma2 = f2;
    x.mapa = mapa;
    x.texto = aviso;
    p.push_back(x);
    return &p.back();
  };
  auto final = [&](int quien, Texto t) {
    Paso x;
    x.tipo = kFinal;
    x.quien = quien;
    x.texto = t;
    p.push_back(x);
  };
  if (m == kSagaB1) {
    const auto d = DatosLeidos();
    if (!d) return p;
    for (const CombateB1& c : HiloSaga(*d)) {
      const int j = LuchadorB1(c.jugador_b1), r = LuchadorB1(c.rival_b1);
      for (const auto& f : kFrasesSaga) {
        if (f.rival_b1 != c.rival_b1 || (f.forma_b1 >= 0 && f.forma_b1 != c.forma)) continue;
        charla(j, false, f.jugador);
        charla(r, true, f.rival);
      }
      Paso* x = combate(j, FormaB3(j, c.forma_jugador), r, FormaB3(r, c.forma), 0, {});
      x->mapa = -1;   // el escenario de B1 (EscenarioB3 con su nombre)
      x->texto_propio = c.escenario_nombre;
      x->tiempo = c.tiempo > 0 ? (c.tiempo + 30) / 60 : c.tiempo == -1 ? -1 : 0;
      x->vida2 = c.vida_rival > 0 && c.vida_rival != 3000 ? c.vida_rival : 0;
      x->reglas.ki_rival = float(c.ki_rival) / 100.0f;
    }
    final(kVegeta, {"¡Kakarotto! Esto no ha terminado. El único que derrotará a Freezer seré yo...",
                    "Kakarot! This isn't over. I'm the only one who will defeat Frieza..."});
  } else if (m == kGotenks) {
    const int fat = IdGotenksGordo();
    charla(kPiccolo, true, {"¡Os habéis equivocado de postura! ¡Los dedos no se tocaban!",
                            "You got the pose wrong! Your fingers weren't touching!"});
    charla(fat, false, {"¡Uf, uf...! ¡Da igual! ¡Con este cuerpo también vamos a ganar a Bu!",
                        "Huff, huff...! Who cares! We'll beat Buu even with this body!"});
    combate(fat, 0, kMajinBu, 0, kMapaCiudad, {"¡Gotenks gordo contra Majin Bu en la ciudad!",
                                               "Fat Gotenks versus Majin Buu in the city!"});
    charla(kPiccolo, true, {"No me lo puedo creer... ¡Está aguantando! Pero Bu ha cambiado...",
                            "I can't believe it... He's holding on! But Buu has changed..."});
    charla(kSuperBu, true, {"Ji, ji. Bu quiere jugar con el gordito en la sala del tiempo.",
                            "Hee hee. Buu wants to play with the chubby one in the time chamber."});
    combate(fat, 0, kSuperBu, 0, kMapaCamara, {"¡Aguanta en la sala del espíritu y del tiempo!",
                                               "Hold on in the Hyperbolic Time Chamber!"});
    final(fat, {"¡Ja! ¡La fusión perfecta es para los flojos! ...Ahora la repetimos bien, ¿vale, Piccolo?",
                "Ha! Perfect fusions are for wimps! ...We'll do it properly now, okay, Piccolo?"});
  } else if (m == kIW) {
    // Las 96 Misiones del Dragon en su orden: cada una, su ficha (titulo y texto de IW + reglas) y su combate; las
    // que no se pueden jugar en B3, una ficha "no disponible" por cada tramo seguido con el mismo motivo.
    std::vector<MisionTablaIW> tabla;
    {
      std::lock_guard<std::mutex> lk(g_iw_mu);
      tabla = g_iw_tabla;
    }
    // (sin el mapa de Dragon Mission: con el, Entrar va a DrawMapaIW y cada mision arma su combate con PasoIW)
    const char* por = nullptr;
    int desde = 0, hasta = 0;
    auto avisar = [&] {
      if (!por) return;
      Paso x;
      x.tipo = kInforme;
      x.informe = desde - 1;   // titulo y texto de IW de la primera del tramo
      x.texto_propio = (desde == hasta ? T::T("No disponible: misión ", "Not available: mission ") + std::to_string(desde)
                                       : T::T("No disponibles: misiones ", "Not available: missions ") +
                                             std::to_string(desde) + "-" + std::to_string(hasta)) +
                       " (" + por + ").";
      p.push_back(x);
      por = nullptr;
    };
    for (size_t i = 0; i < tabla.size() && tabla[i].numero <= kMisionesDM; ++i) {
      const MisionTablaIW& t = tabla[i];
      if (const char* no = MotivoIW(t)) {
        if (por && std::strcmp(por, no) == 0 && t.numero == hasta + 1) {
          hasta = t.numero;
          continue;
        }
        avisar();
        por = no;
        desde = hasta = t.numero;
        continue;
      }
      avisar();
      Paso in;
      in.tipo = kInforme;
      in.informe = int(i);
      const Paso x = PasoIW(t, in.texto_propio);
      p.push_back(in);
      p.push_back(x);
    }
    avisar();
    final(kGoku, {"¡Misiones del Dragón completadas! Así se lucha en Infinite World.",
                  "Dragon Missions complete! That's how you fight in Infinite World."});
  } else if (m == kBuu) {
    Paso e;
    e.tipo = kEleccion;
    charla(kSuperBu, false, {"Bu tiene hambre... ¡Bu se va a comer a los más fuertes de la Tierra!",
                             "Buu is hungry... Buu is going to eat the strongest on Earth!"});
    charla(kPiccolo, true, {"No te dejaré pasar, monstruo. ¡Aquí se acaba todo!", "I won't let you through, monster. This ends here!"});
    combate(kSuperBu, 0, kPiccolo, 0, kMapaCamara, {"Vence a Piccolo y Bu lo absorberá.", "Beat Piccolo and Buu will absorb him."})
        ->desbloquea = 1;
    p.push_back(e);
    charla(kGoten, true, {"¡Trunks, la fusión! ...¡Demasiado tarde, ya está aquí!", "Trunks, the fusion! ...Too late, he's here!"});
    combate(kSuperBu, 0, kGoten, kFormaGotenSsj, kMapaPradera, {"Vence a Goten y Bu absorberá a Gotenks.",
                                                                "Beat Goten and Buu will absorb Gotenks."})
        ->desbloquea = 2;
    p.push_back(e);
    charla(kGohan, true, {"¡Bu! ¡Devuélveme a mis amigos ahora mismo!", "Buu! Give my friends back right now!"});
    combate(kSuperBu, 0, kGohan, kFormaGohanDefinitivo, kMapaRocas, {"Vence a Gohan y Bu lo absorberá.",
                                                                     "Beat Gohan and Buu will absorb him."})
        ->desbloquea = 3;
    p.push_back(e);
    charla(kGoku, true, {"Así que has absorbido a todos... Pues yo no me voy a dejar.",
                         "So you've absorbed everyone... Well, I won't let you have me."});
    combate(kSuperBu, 0, kGoku, kFormaGokuSsj3, kMapaKaio, {"El último: Goku en Súper Saiyan 3.", "The last one: Super Saiyan 3 Goku."});
    final(kSuperBu, {"¡Ja, ja! ¡Bu es el más fuerte! ...Bu tiene hambre otra vez.",
                     "Ha ha! Buu is the strongest! ...Buu is hungry again."});
  }
  return p;
}

// ---- Tablero ----
void VolverAlTablero() {   // con g_mu
  g_pasos.clear();
  g_paso = 0;
  Publicar(kTableroF);
}
// Combate que pide el tablero: c = {jugador, rival (IDs de B3), mapa de B3, vida que le queda al jugador, vida del
// rival, condicion de la casilla}. Condiciones con las Reglas que existen: 2 (empiezan con 0 de ki) = ki del rival a 0;
// 4 (el ki se va gastando) = el jugador no gana ki. ponytail: 2 no vacia el ki del jugador, y 3 (la vida se va gastando)
// y 5 (sin bloqueo) solo se avisan: hacen falta campos nuevos en Reglas (pasarela.h/.cpp, frente P3).
bool Lanzar();   // mas abajo
void CombateTablero(const int c[14]) {   // con g_mu
  g_pasos.clear();
  Paso x;
  x.tipo = kCombate;
  x.jugador = c[0];
  x.rival = c[1];
  // Forma inicial: el T2 de B2 no la trae (0 en las 9 fases). Cell y Freezer del tablero salen siempre
  // perfecto/final; una orden 52 explicita se traduce con FormaB3.
  x.forma1 = b2::FormaTableroB3(x.jugador, c[8]);
  x.forma2 = b2::FormaTableroB3(x.rival, c[10]);
  if (c[2] < 0) x.escenario = -c[2] - 1;   // pasarela_b2: el alta del escenario de B2 de la casilla
  else x.mapa = c[2];
  x.vida2 = c[4];
  // sin condicion no hay aviso: la franja repetia la cinta del titulo (revision de R6)
  if (c[5] == 1) x.texto = {"Empezáis con media vida.", "Both start with half health."};
  if (c[5] == 2) {
    x.reglas.ki_rival = 0.01f;
    x.reglas.ki_cero = true;
    x.texto = {"Empezáis con 0 de ki.", "Both start with 0 ki."};
  }
  if (c[5] == 3) {
    x.reglas.vida_baja = 0.01f;   // ponytail: ritmo de B2 sin medir (1 % de la vida por segundo, nunca por debajo de 1)
    x.texto = {"La vida se va gastando.", "Health keeps draining."};
  }
  if (c[5] == 5) x.reglas.sin_bloqueo = true;
  // Saibaman de colores: cada combate contra uno saca un traje al azar entre los que tenga (los 2 de serie y los del
  // mod de colores, mod center hd/campanas/trajes_saibaman). Con un solo traje, el de siempre.
  if (c[1] == 42) {
    auto* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    const int n = base ? REX_LOAD_U8(dbz3::GA(0x8234ABB8u) + 96u * 42u + 4) : 0;
    if (n > 1) x.reglas.traje_rival = int(std::chrono::steady_clock::now().time_since_epoch().count() % n);
  }
  x.reglas.ataque = float(c[6]) / 100.0f;   // daga y armadura del tablero
  x.reglas.defensa = std::min(0.9f, float(c[7]) / 100.0f);
  if (c[5] == 4) {
    x.reglas.ki_lento = 1.0f;
    x.texto = {"El ki se va gastando.", "Ki keeps draining."};
  }
  if (c[5] == 5) x.texto = {"Sin bloqueo.", "No blocking."};
  g_pasos.push_back(x);
  g_vida = c[3];
  g_paso = 0;
  g_intentos = 0;
  // B2 no tiene tarjeta VS: del contacto ("!!" y pose, en pasarela_b2) va derecho al combate. La tarjeta solo si
  // el conector esta ocupado (la tarjeta lo reintenta).
  if (!Lanzar()) Publicar(kPantalla);
}
// ---- Nave de Babidi (pasarela_nave.cpp): c = {jugador, rival, mapa, vida inicial, tiempo, prueba} ----
void VolverANave() {   // con g_mu
  g_pasos.clear();
  g_paso = 0;
  Publicar(kNaveF);
}
void CombateNave(const int c[6]) {   // con g_mu
  g_pasos.clear();
  Paso x;
  x.tipo = kCombate;
  x.jugador = c[0];
  x.rival = c[1];
  if (c[2] < 0) x.escenario = -c[2] - 1;   // alta de B2 (registro del selector), como el tablero
  else x.mapa = c[2];
  x.tiempo = c[4];
  if (c[5] == 0 || c[5] == 1) x.vida2 = 1000;   // B2 0x1D2620: el rival con una barra en I y II
  x.reglas.nivel_com = 2;                         // B2: COM al nivel mas alto en las 4 pruebas
  if (c[5] == 3) x.reglas.ki_rival = 5;   // IV: el rival con ki para lanzar ondas. ponytail: valor sin leer de B2
  static const Texto kAviso[4] = {{"Prueba I: hasta caer.", "Trial I: until you fall."},
                                  {"Prueba II: una sola barra de vida.", "Trial II: a single health bar."},
                                  {"Prueba III: golpea durante 99 s.", "Trial III: hit for 99 s."},
                                  {"Prueba IV: devuelve ondas de ki.", "Trial IV: deflect ki waves."}};
  x.texto = kAviso[std::clamp(c[5], 0, 3)];
  g_pasos.push_back(x);
  g_vida = c[3];
  g_nave_prueba = c[5];
  g_paso = 0;
  g_intentos = 0;
  // Prueba I de B2: el rival se levanta y sigue el combate, sin pantalla VS. El combate siguiente ya lo pidio el
  // gancho sub_820FE9B0 (hub::ChainBattleEx + HoldChain): aqui solo se suelta y se pone su vigilante.
  if (nave::Encadenado()) {
    nave::Seguir();
    g_nave_encadenada = true;
    if (Lanzar()) return;
    g_nave_encadenada = false;
  }
  Publicar(kPantalla);
}

// --- Dragon Mission de IW (frente P1): mapa -> ficha -> combate -> resultado -> mapa (pasarela_iw.cpp lo dibuja) ---
int g_iw_vista = 0;          // en kMapaIW: 0 mapa, 1 ficha, 2 resultado
std::atomic<bool> g_iw_vuelta_combate{false};   // H03: se vuelve de un combate/derrota; el mapa no repite la intro
int g_iw_mision = 0;         // mision donde poner el cursor (-1 = la abierta); 0 = donde estaba
int64_t g_iw_t0 = 0;         // cuando se pidio el combate (TIME de las misiones sin tiempo)
ResultadoDM g_iw_res;
bool g_iw_final = false;    // superada la 96: creditos al cerrar el resultado
std::atomic<bool> g_iw_eligiendo{false};   // Fighter's Road: eligiendo en la rueda del juego (la pasarela se oculta)
std::atomic<int> g_iw_elegido{-1};
bool g_iw_road = false;     // en Fighter's Road (mapa del #DMD 1092)
int g_iw_dificultad = 2;    // la de la ficha (0 Very Easy .. 4 Very Hard) del combate en curso
Paso g_iw_tras_video;       // combate que se lanza al acabar el video de antes (EscenaIW)
bool g_iw_esperando_video = false;
int g_iw_despues = -1;      // video de la historia que va tras el resultado (EscenaIW); -1 ninguno
// Training No.9: si el jugador cambio de forma (X+2980, la forma de B3: Goku 1 = Kaioken) durante el combate.
std::atomic<bool> g_iw_transformo{false};
void VigilarFormaIW() {
  g_iw_transformo = false;
  std::thread([] {
    constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
    constexpr uint32_t kX0 = 2096, kForma = 2980;
    rex::Runtime* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    bool en = false;
    for (int64_t fin = NowMs() + 400000; base && NowMs() < fin; std::this_thread::sleep_for(std::chrono::milliseconds(16))) {
      const bool own = dbz3::hub::OwnBattle();
      if (en && !own) break;
      en = en || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      if (mgr && REX_LOAD_U32(mgr + 35128) == 4 && REX_LOAD_U32(mgr + kX0 + kForma) != 0 && !g_iw_transformo.exchange(true))
        REXLOG_INFO("dbz3 misiones: iw: entrenamiento: transformado (forma {})", REX_LOAD_U32(mgr + kX0 + kForma));
    }
  }).detach();
}
// Training No.2 ("Guard 5 attacks, dodge 5 and teleport 5 times", DATA_USA 133): cuenta, por cambio de la ACCION del jugador
// (X+3244), lo que mide la sonda DBZ3_R1_ENTRENO2_SONDA (iw_fix, 2026-10-17: barrido del retraso y la direccion de G
// contra los ataques del rival): guardia = 26 (golpe parado), esquiva = 38 (la "Dodge_26" de IW: salio con G sola y con
// G + direccion segun el momento; sus animaciones 0B8-0C8 son variantes) y teletransporte = 39 (la "Teleportation_27" de
// IW, 0EC: G + derecha poco antes del golpe). Las medidas anteriores (38/0C5 esquiva, 38/0BE teletransporte) tomaban una
// animacion de la esquiva por el teletransporte: con 5 de cada la cuenta no llegaba nunca. Con 5 de cada la mision se
// cumple y el rival cae (vida a 0: el "Success" de IW acaba el combate). El ki del jugador se mantiene (el teletransporte
// lo gasta y B3 lo recupera despacio: sin esto no se pueden encadenar 5).
std::atomic<bool> g_iw_entreno2{false};
// Piloto de PRUEBA (DBZ3_R1_ENTRENO2_AUTO=1; con DBZ3_R1_ENTRENO2_SONDA=1 solo registra): juega el entrenamiento por el
// mando virtual de XInput (net::InyectarPad): guardia mantenida (A) hasta 5 golpes parados; luego, en cada ataque del rival
// (accion 15 o 50), toca A tras un retraso que va barriendo (esquiva sin direccion, teletransporte con la palanca hacia
// el rival, iwmini::PadPrueba) hasta tener 5 de cada. Registra lo que le pasa al jugador desde que empezo el ataque.
struct PilotoE2 {
  int64_t t_ataque = 0, t_pulsa = 0, t_suelta = 0, t_dir = 0;
  int ciclo = 0, retraso = 0, dir = 0;
  uint32_t riv_antes = ~0u, jug_antes = ~0u;
  bool activo = false, juega = false;
  void Paso(uint32_t x0, uint32_t x1, const int cuenta[3], uint8_t* base) {
    const int64_t ahora = NowMs();
    const uint32_t ra = REX_LOAD_U16(x1 + 3244), rc = REX_LOAD_U32(x1 + 2928) & 0xFFF;
    const uint32_t ja = REX_LOAD_U16(x0 + 3244), jc = REX_LOAD_U32(x0 + 2928) & 0xFFF;
    const int fase = cuenta[0] < 5 ? 0 : cuenta[1] < 5 ? 1 : cuenta[2] < 5 ? 2 : 3;
    if ((ra << 16 | rc) != riv_antes) {
      riv_antes = ra << 16 | rc;
      if (fase >= 1 && fase <= 2 && (ra == 15 || ra == 50) && ahora - t_ataque > 700 && !activo) {   // ataque nuevo del rival
        static const int kEsquiva[5] = {0, 60, 100, 140, 180}, kTele[7] = {120, 160, 80, 200, 40, 240, 0};
        t_ataque = ahora;
        retraso = fase == 1 ? kEsquiva[ciclo % 5] : kTele[ciclo % 7];
        {   // teletransporte: G con la palanca HACIA el rival (medido: 5 de 20 intentos; alejandose, 0 de 20), segun el lado
          // en pantalla (X+4960+48 / +56)
          uint32_t a = REX_LOAD_U32(x0 + 4960 + 48), b = REX_LOAD_U32(x0 + 4960 + 56);
          float jx, rx;
          std::memcpy(&jx, &a, 4);
          std::memcpy(&rx, &b, 4);
          dir = fase == 2 ? (rx >= jx ? 1 : -1) : 0;
        }
        ++ciclo;
        t_pulsa = ahora + retraso;
        t_dir = t_pulsa - 70;   // la direccion va ANTES que G (el teletransporte sale con la direccion ya pulsada, medido)
        t_suelta = t_pulsa + 140;
        activo = true;
        REXLOG_INFO("dbz3 e2: ataque del rival {}/{:03X}; fase {}: pulso G dir {} tras {} ms", ra, rc, fase, dir, retraso);
      }
    }
    if ((ja << 16 | jc) != jug_antes) {
      jug_antes = ja << 16 | jc;
      const uint32_t u = REX_LOAD_U32(x0 + 6100);
      float ki;
      std::memcpy(&ki, &u, 4);
      REXLOG_INFO("dbz3 e2: jugador {}/{:03X} a +{} ms (retraso {} dir {}) ki {:.0f}", ja, jc, ahora - t_ataque, retraso, dir,
                  ki);
    }
    if (!juega) return;
    if (fase == 0) {
      dbz3::net::InyectarPad(0, 0x1000);
    } else if (fase == 3) {
      dbz3::net::InyectarPad(0, 0);
    } else if (activo && ahora >= t_pulsa && ahora < t_suelta) {
      dbz3::iwmini::PadPrueba(dir);   // la palanca ya inclinada cuando se pulsa G (la cruceta no vale)
      dbz3::net::InyectarPad(0, 0x1000);
    } else if (activo && dir != 0 && ahora >= t_dir && ahora < t_pulsa) {
      dbz3::iwmini::PadPrueba(dir);
      dbz3::net::InyectarPad(0, 0);
    } else {
      dbz3::iwmini::PadPrueba(0);
      dbz3::net::InyectarPad(0, 0);
      if (activo && ahora > t_suelta + 450) activo = false;
    }
  }
};

void VigilarEntreno2() {
  g_iw_entreno2 = false;
  std::thread([] {
    constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
    constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVida = 6080, kKi = 6100, kKiMax = 6104;
    rex::Runtime* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    int cuenta[3] = {0, 0, 0};
    uint32_t antes = ~0u;
    bool en = false;
    static const bool sonda = std::getenv("DBZ3_R1_ENTRENO2_SONDA") != nullptr;
    static const bool auto_ = std::getenv("DBZ3_R1_ENTRENO2_AUTO") != nullptr;
    PilotoE2 piloto;
    piloto.juega = auto_ && !sonda;
    for (int64_t fin = NowMs() + 400000; base && NowMs() < fin; std::this_thread::sleep_for(std::chrono::milliseconds(8))) {
      const bool own = dbz3::hub::OwnBattle();
      if (en && !own) break;
      en = en || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      if (!mgr || REX_LOAD_U32(mgr + 35128) != 4) continue;
      const uint32_t x = mgr + kX0;
      if (sonda || auto_) piloto.Paso(x, x + kXPaso, cuenta, base);
      {   // vida: el entrenamiento no se pierde por K.O. (el rival pega fuerte y hay que fallar varias veces para aprender)
        uint32_t u = REX_LOAD_U32(x + kVida + 4), v = REX_LOAD_U32(x + kVida);
        float mx, vida;
        std::memcpy(&mx, &u, 4);
        std::memcpy(&vida, &v, 4);
        if (mx > 0 && vida < mx * 0.4f) REX_STORE_U32(x + kVida, u);
      }
      {   // ki: se mantiene lleno (el teletransporte lo gasta)
        uint32_t u = REX_LOAD_U32(x + kKiMax), v = REX_LOAD_U32(x + kKi);
        float mx, ki;
        std::memcpy(&mx, &u, 4);
        std::memcpy(&ki, &v, 4);
        if (mx > 0 && ki < mx * 0.5f) REX_STORE_U32(x + kKi, u);
      }
      const uint32_t ac = REX_LOAD_U16(x + 3244);
      if (ac == antes) continue;
      antes = ac;
      const int k = ac == 26 ? 0 : ac == 38 ? 1 : ac == 39 ? 2 : -1;
      if (k < 0) continue;
      ++cuenta[k];
      REXLOG_INFO("dbz3 misiones: iw: entrenamiento 2: guardia {} esquiva {} teletransporte {}", cuenta[0], cuenta[1], cuenta[2]);
      if (cuenta[0] >= 5 && cuenta[1] >= 5 && cuenta[2] >= 5 && !g_iw_entreno2.exchange(true)) {
        REXLOG_INFO("dbz3 misiones: iw: entrenamiento 2 cumplido: el rival cae");
        // Success: el combate acaba como un ring out ganado por el jugador (mgr+35569 = hueco del ganador + 1, igual que el
        // reto de los 30 golpes de pasarela.cpp). Poner solo la vida del rival a 0 no bastaba: B3 la mira al darle un
        // golpe y, tras el quinto teletransporte, el combate seguia (iw_fix, 2026-10-17)
        REX_STORE_U8(mgr + 35569, 1);
        REX_STORE_U32(mgr + kX0 + kXPaso + kVida, 0u);
      }
    }
    if (sonda || auto_) {
      dbz3::iwmini::PadPrueba(0);
      dbz3::net::InyectarPad(0, -1);
    }
  }).detach();
}
// Training No.4 (x.iw 5) y No.8 (x.iw 6), R7: cuenta por el codigo de animacion del jugador (X+2928 & 0xFFF, medido con
// PilotoSecuencia en iw_fix 2026-10-17, E:/DBZ3HD_trabajo/R7/iw/sonda*.toml):
// - No.4 "Regular Throw, Attack Throw y Charge Attack x3": el agarre de B3 que entra (base 0x480: 0x357 intento ->
//   0x488 agarrado; P+G o LB). ponytail: B3 no tiene agarre de ataque (E+G solo guarda) ni ataque cargado (P+K mantenido
//   no sale): se piden 3 agarres.
// - No.8 "Aura Burning + 3 Pursuit Attacks seguidos x3": el modo hiper de B3 (P+K+G+E, X+8208) es el Aura Burning y su
//   Dragon Rush (el atacante pasa por 0x400-0x43F; la victima 0x440-0x47F) son las persecuciones seguidas: 3 Dragon Rush.
// Al cumplir, como el No.2: ring out ganado (mgr+35569) y vida del rival a 0. Vida y ki como en el No.2 (no se pierde).
std::atomic<bool> g_iw_entreno{false};
void VigilarEntrenoIW(int iw) {
  g_iw_entreno = false;
  std::thread([iw] {
    constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
    constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVida = 6080, kKi = 6100, kKiMax = 6104;
    rex::Runtime* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    int cuenta = 0;
    bool en = false, dentro = false;
    for (int64_t fin = NowMs() + 400000; base && NowMs() < fin; std::this_thread::sleep_for(std::chrono::milliseconds(8))) {
      const bool own = dbz3::hub::OwnBattle();
      if (en && !own) break;
      en = en || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      if (!mgr || REX_LOAD_U32(mgr + 35128) != 4) continue;
      const uint32_t x = mgr + kX0;
      {   // vida: no se pierde por K.O.
        uint32_t u = REX_LOAD_U32(x + kVida + 4), v = REX_LOAD_U32(x + kVida);
        float mx, vida;
        std::memcpy(&mx, &u, 4);
        std::memcpy(&vida, &v, 4);
        if (mx > 0 && vida < mx * 0.4f) REX_STORE_U32(x + kVida, u);
      }
      if (iw == 6) {   // ki para el modo hiper (en IW el Aura Burning tambien pide ki)
        uint32_t u = REX_LOAD_U32(x + kKiMax), v = REX_LOAD_U32(x + kKi);
        float mx, ki;
        std::memcpy(&mx, &u, 4);
        std::memcpy(&ki, &v, 4);
        if (mx > 0 && ki < mx * 0.5f) REX_STORE_U32(x + kKi, u);
      }
      const uint32_t c = REX_LOAD_U32(x + 2928) & 0xFFF;
      const bool ahora = REX_LOAD_U16(x + 3244) == 15 &&
                         (iw == 5 ? (c & 0xFF0) == 0x480 : (c & 0xFC0) == 0x400);
      static int64_t t_ultimo = 0;   // un Dragon Rush pasa por varios tramos de codigos: uno cada 3 s como mucho
      if (ahora && !dentro && (iw == 5 || NowMs() - t_ultimo > 3000)) {
        t_ultimo = NowMs();
        ++cuenta;
        REXLOG_INFO("dbz3 misiones: iw: entrenamiento {}: {} {} de 3 (codigo {:03X})", iw == 5 ? 4 : 8,
                    iw == 5 ? "agarre" : "Dragon Rush", cuenta, c);
        if (cuenta >= 3 && !g_iw_entreno.exchange(true)) {
          REXLOG_INFO("dbz3 misiones: iw: entrenamiento {} cumplido: el rival cae", iw == 5 ? 4 : 8);
          REX_STORE_U8(mgr + 35569, 1);
          REX_STORE_U32(mgr + kX0 + kXPaso + kVida, 0u);
        }
      }
      dentro = ahora;
    }
  }).detach();
}
// SONDA de los entrenamientos 2-10 (DBZ3_R1_TRAZA=1): mientras dura el combate pedido, cada cambio de la accion
// (X+3244) y del codigo de animacion (X+2928 & 0xFFF) de los dos luchadores, con el estado del combate, al registro
// ("iw traza"): para saber que valores son guardia, esquiva, teletransporte, persecucion... y contarlos despues.
// PRUEBA R7 (con la sonda): DBZ3_R7_SEC = "PASO,PASO,..." en bucle mientras dura la pelea; PASO = [r|a]HEX@MS: botones
// XInput (A 1000 G, X 4000, Y 8000, B 2000, LB 100, RB 200) mantenidos MS ms, con la palanca hacia el rival (r) o
// alejandose (a). Para medir que accion (X+3244) da cada mando de los entrenamientos.
void PilotoSecuencia(const char* sec) {
  struct P { int dir; int32_t bot; int ms; };
  std::vector<P> pasos;
  for (const char* s = sec; *s;) {
    P p{0, 0, 100};
    if (*s == 'r' || *s == 'a') p.dir = *s++ == 'r' ? 1 : -1;
    char* e = nullptr;
    p.bot = int32_t(std::strtol(s, &e, 16));
    if (e && *e == '@') p.ms = std::atoi(e + 1);
    pasos.push_back(p);
    s = std::strchr(s, ',');
    if (!s) break;
    ++s;
  }
  std::thread([pasos] {
    constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
    rex::Runtime* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    bool en = false;
    size_t i = 0;
    for (int64_t fin = NowMs() + 300000; base && !pasos.empty() && NowMs() < fin;) {
      const bool own = dbz3::hub::OwnBattle();
      if (en && !own) break;
      en = en || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      if (!mgr || REX_LOAD_U32(mgr + 35128) != 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }
      const P& p = pasos[i++ % pasos.size()];
      for (int64_t t = NowMs() + 2500; p.bot && NowMs() < t;) {   // botones solo con el jugador libre (quieto o andando)
        const uint32_t ac = REX_LOAD_U16(mgr + 2096 + 3244);
        if (ac == 0 || ac == 2 || ac == 3 || REX_LOAD_U32(mgr + 35128) != 4) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
      }
      int dir = 0;
      if (p.dir) {   // hacia el rival segun el lado en pantalla (X+4960+48 / +56), como PilotoE2
        const uint32_t x0 = mgr + 2096;
        uint32_t a = REX_LOAD_U32(x0 + 4960 + 48), b = REX_LOAD_U32(x0 + 4960 + 56);
        float jx, rx;
        std::memcpy(&jx, &a, 4);
        std::memcpy(&rx, &b, 4);
        dir = (rx >= jx ? 1 : -1) * p.dir;
      }
      if (std::getenv("DBZ3_R7_KI")) REX_STORE_U32(mgr + 2096 + 6100, REX_LOAD_U32(mgr + 2096 + 6104));   // ki lleno
      REXLOG_INFO("dbz3 misiones: r7 sec: paso {} botones {:04X} dir {} {} ms (hiper {})", (i - 1) % pasos.size(), p.bot,
                  dir, p.ms, REX_LOAD_U8(mgr + 2096 + 8208));
      dbz3::iwmini::PadPrueba(dir);
      dbz3::net::InyectarPad(0, p.bot);
      std::this_thread::sleep_for(std::chrono::milliseconds(p.ms));
    }
    dbz3::iwmini::PadPrueba(0);
    dbz3::net::InyectarPad(0, -1);
  }).detach();
}
void SondaIW() {
  if (!std::getenv("DBZ3_R1_TRAZA")) return;
  if (const char* sec = std::getenv("DBZ3_R7_SEC")) PilotoSecuencia(sec);
  std::thread([] {
    constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
    constexpr uint32_t kX0 = 2096, kXPaso = 8240, kAccion = 3244, kCodigo = 2928, kEstado = 35128;
    rex::Runtime* rt = rex::Runtime::instance();
    uint8_t* base = rt ? rt->virtual_membase() : nullptr;
    uint32_t visto[2] = {~0u, ~0u};
    bool en = false;
    for (int64_t fin = NowMs() + 300000; base && NowMs() < fin; std::this_thread::sleep_for(std::chrono::milliseconds(8))) {
      const bool own = dbz3::hub::OwnBattle();
      if (en && !own) break;   // el combate acabo
      en = en || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      if (!mgr) continue;
      const uint32_t st = REX_LOAD_U32(mgr + kEstado);
      for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t x = mgr + kX0 + kXPaso * k;
        const uint32_t ac = REX_LOAD_U16(x + kAccion), an = REX_LOAD_U32(x + kCodigo) & 0xFFF;
        if ((ac << 16 | an) == visto[k]) continue;
        visto[k] = ac << 16 | an;
        REXLOG_INFO("dbz3 misiones: iw traza: estado {} lado {} accion {} codigo {:03X}", st, k + 1, ac, an);
      }
    }
  }).detach();
}
// Escena de MOTOR de IW volcada por studies 3/ENTREGAS/04_iw_escenas_motor/generar.py: forma compacta .cine (una
// linea por token; '#' comenta). La camara por cuadro y la duracion real llenan los campos que consume cinematica.cpp
// (Paso::cam_*, Paso::cuadros). El vinculo actor->modelo y las voces aun NO van (se marcan en el volcado).
bool LeerCineIW(const std::filesystem::path& ruta, dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  std::ifstream f(ruta);
  if (!f) return false;
  C::Paso p;
  bool abierto = false;
  std::string tipo;
  while (f >> tipo) {
    if (tipo[0] == '#' || tipo == "mision") { std::string resto; std::getline(f, resto); continue; }
    if (tipo == "linea") { if (abierto) e.pasos.push_back(p); p = C::Paso{}; abierto = true; continue; }
    if (tipo == "cuadros") { f >> p.cuadros; continue; }
    if (tipo == "plano") { f >> p.plano; continue; }
    if (tipo == "anim0") { f >> p.anim[0]; continue; }
    if (tipo == "anim1") { f >> p.anim[1]; continue; }
    if (tipo == "cam") { int n; f >> n; continue; }
    if (tipo == "ojo" || tipo == "obj") {
      std::array<float, 4> v{};
      f >> v[0] >> v[1] >> v[2] >> v[3];
      (tipo == "ojo" ? p.cam_ojo : p.cam_obj).push_back(v);
      continue;
    }
    if (tipo == "fov" || tipo == "roll") {
      std::array<float, 2> v{};
      f >> v[0] >> v[1];
      (tipo == "fov" ? p.cam_fov : p.cam_roll).push_back(v);
      continue;
    }
    std::string resto;
    std::getline(f, resto);   // token desconocido: se ignora
  }
  if (abierto) e.pasos.push_back(p);
  return !e.pasos.empty();
}
// Escena de antes de una mision en el motor de B3 (cinematica.h, frente R3): los dos luchadores de la mision en su
// escenario y, abajo, cada linea del subtitulo del video de IW (tablas del ELF: VideoIW). ponytail: en IW es un video
// (SFD) con voz; quien habla y cuanto dura cada linea van en el video (no se leen): plano general y 3 s por linea.
// Respaldo del video original (video_sfd.h): solo si el video no se puede reproducir (ISO, falta el archivo).
// DBZ3_IW_ESCENA=<fichero .cine>: escena de MOTOR decodificada (generar.py) en vez del respaldo de B3.
dbz3::cinematica::Escena EscenaHistoriaIW(const DragonMissionIW& dm, int video) {
  namespace C = dbz3::cinematica;
  if (const char* r = std::getenv("DBZ3_IW_ESCENA")) {
    C::Escena c;
    if (LeerCineIW(r, c)) {
      REXLOG_INFO("dbz3 misiones: iw: escena de motor de IW leida de {} ({} pasos)", r, c.pasos.size());
      return c;
    }
    REXLOG_WARN("dbz3 misiones: iw: DBZ3_IW_ESCENA={} no se pudo leer; se usa el respaldo de B3", r);
  }
  C::Escena e;
  C::Paso p;
  p.segundos = 2.0f;
  e.pasos.push_back(p);   // los dos en el escenario
  for (const Imagen& l : dm.subtitulos[size_t(video)]) {
    p = C::Paso{};
    p.segundos = 3.0f;
    // letra blanca con borde oscuro, como sobre el video: cinematica.cpp elige el fondo por el brillo de los pixeles
    // de alfa > 64 y con el borde la tomaria por letra oscura (bocadillo claro); el borde baja a 64 -> franja oscura
    p.texto = l;
    for (size_t i = 0; i + 3 < p.texto.rgba.size(); i += 4) {
      const uint8_t* c = &p.texto.rgba[i];
      if (c[3] > 64 && c[0] + c[1] + c[2] < 3 * 128) p.texto.rgba[i + 3] = 64;
    }
    e.pasos.push_back(p);
  }
  return e;
}
// Musica de IW (R7): la pista del mapa (PantallaDM::Pista, ADX_CMN de la copia del jugador) mientras se ve el mapa; -1
// la corta. Solo corta la suya (la musica de PS2 es una a la vez y la usan tambien el tablero y la nave de B2).
// Volumen: el del lanzador (como pasarela_b2.cpp Dibujar).
void MusicaIW(int pista) {
  static int sonando = -1;
  if (pista == sonando) return;
  if (pista < 0) {
    CallarMusicaPs2();
  } else {
    const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
    REXLOG_INFO("dbz3 misiones: iw: musica ADX_CMN {} (volumen {:.2f})", pista, vol);
    // PRUEBA: DBZ3_TEST_MUSICA=1 la decodifica aunque el juego este en silencio (registro con su duracion)
    OirMusicaPs2(BuscarIW(), "ADX_CMN.AFS", pista, std::getenv("DBZ3_TEST_MUSICA") ? 0.001f : vol);
  }
  sonando = pista;
}
// M6 (mod opcional iw.musica_menus): las pistas de menu de IW (ADX_CMN, banco 2) con la lista verificada en
// iw_sles55347.c: 74 (LoadWaitStartGameSelection_DataLoad, :346407), 75 (pantalla VS, :365724), 77 (opciones,
// :356861/:357673), 80 (LoadWaitDragonMissionFunc, :456353), 81 (rueda de personajes, :362329/:362648), 82 (menu
// principal s_MAIN_MENU_UPDATE, :348255) y 83 (aparicion de la rueda, :340244). El toml trae esas claves en
// [pistas] (la 80 incluida); cual suena en cada pantalla del port es [H] (las pantallas del original no son las del
// port): ficha de mision (g_iw_vista 1) -> 74; resultado (2) -> 82; rueda de respaldo de Fighter's Road (3) -> 81.
// -1 = sin cambio (la pista del mapa): sin el mod, o si el toml no trae esa pista.
int PistaMenuIW(int vista) {
  if (!dbz3::opcional::Activo("iw.musica_menus")) return -1;
  std::vector<int> pistas;   // claves de [pistas]: las pistas de menu verificadas (74, 75, 77, 80, 81, 82, 83)
  for (const std::string& k : dbz3::opcional::Claves("iw.musica_menus", "pistas")) pistas.push_back(std::atoi(k.c_str()));
  auto usa = [&](std::initializer_list<int> quiero) {
    for (int q : quiero)
      if (std::find(pistas.begin(), pistas.end(), q) != pistas.end()) return q;
    return -1;
  };
  if (vista == 1) return usa({74, 82});
  if (vista == 2) return usa({82, 83});
  if (vista == 3) return usa({81, 83});
  return -1;
}
void VolverAlMapa() {   // con g_mu
  g_pasos.clear();
  g_paso = 0;
  Publicar(kMapaIW);
}
// Rango de IW 0 C .. 4 Z (Battle_ComputeRankAndRecord, EU 0x28A5E0): suma de las tres notas de Char_ComputeStatGrade
// (0x2AC0D0) segun el tipo de notas de la mision y cortes 19/16/12/7. Notas (LoadWaitBattleMainExitEnd 0x208EA0):
// Defeat = % de vida propia (25..85 -> 1..7) + segundos (<60 -> 7 ... >=180 -> 1); Fight = % de vida propia + % del
// dano pedido al rival hecho (40..100); Endure = % del dano permitido que no se recibio (15..90 -> 2..7) + % de vida del
// rival (40..100). Nota de los golpes (Defeat y Fight; en Fighter's Road es la segunda): mayor combo >= 5 golpes (+2)
// y su dano >= 500 (+2; +1 en Fight) con Fin::combo_max / combo_dano_max (R3). ponytail: los otros dos contadores
// de esa nota (mgr+0x8c56 y +0x8c3c de IW, sin identificar; Fight anade uno mas) cuentan su mitad, 2; la tercera
// nota de Endure (cinco contadores sin identificar) cuenta su mitad, 3 de 7.
int RangoIW(const MisionTablaIW& t, const Fin& f, int segundos) {
  auto escala = [](int v, std::initializer_list<int> cortes) {
    int n = 0;
    for (const int c : cortes) n += v >= c;
    return n;
  };
  auto pct = [](int a, int b) { return b > 0 ? a * 100 / b : 0; };
  const int objetivo = t.nivel * 1000;
  const int vida = pct(std::max(0, f.vida[0]), f.vida_max[0]);
  const bool combo5 = f.combo_max >= 5, dano500 = f.combo_dano_max >= 500;
  int g0 = 0, g1 = 0, g2 = 3;
  if (t.notas == 1) {
    g0 = escala(vida, {25, 35, 45, 55, 65, 75, 85});
    g1 = 7 - escala(segundos, {60, 75, 90, 120, 150, 180});
    g2 = 2 + 2 * combo5 + 2 * dano500;
  } else if (t.notas == 2) {
    g0 = escala(vida, {25, 35, 45, 55, 65, 75, 85});
    g1 = escala(pct(f.vida_max[1] - std::max(0, f.vida[1]), f.vida_max[1] - objetivo), {40, 50, 60, 70, 80, 90, 100});
    g2 = 2 + 2 * combo5 + 1 * dano500;
  } else if (t.notas == 3) {
    g0 = 1 + escala(100 - pct(f.vida_max[0] - std::max(0, f.vida[0]), f.vida_max[0] - objetivo), {15, 30, 45, 60, 75, 90});
    g1 = escala(pct(std::max(0, f.vida[1]), f.vida_max[1]), {40, 70, 75, 85, 90, 95, 100});
  } else if (t.notas == 4) {   // Fighter's Road: vida + golpes/combos; rango de dos notas
    const int suma = escala(vida, {25, 35, 45, 55, 65, 75, 85}) + 2 + 2 * combo5 + 2 * dano500;
    return suma >= 13 ? 4 : suma >= 11 ? 3 : suma >= 8 ? 2 : suma >= 5 ? 1 : 0;
  } else {
    return -1;   // entrenamientos y "dejar ganar": el rango de los puntos
  }
  const int suma = g0 + g1 + g2;
  return suma >= 19 ? 4 : suma >= 16 ? 3 : suma >= 12 ? 2 : suma >= 7 ? 1 : 0;
}
// Mision ganada: puntos, record (iw_mejor_N), Zenie al monedero del HUB y avance (iw_hecho = la mas alta superada).
void GanarIW(const Paso& p, const Fin& f) {   // con g_mu
  const int n = p.informe + 1;
  ResultadoDM r;
  r.mision = n;
  r.segundos = p.tiempo > 0 && f.segundos >= 0 ? p.tiempo - f.segundos : int((NowMs() - g_iw_t0) / 1000);
  // ponytail: puntos = 80 % de la vida que queda + 8 por segundo por debajo de 180 (ganar con la vida llena en 30 s =
  // 3600, el umbral mas alto del #MSS); la formula de IW no esta descifrada
  r.puntos = std::clamp(std::max(0, f.vida[0]) * 4 / 5 + std::max(0, 180 - r.segundos) * 8, 0, 99999);
  {
    std::lock_guard<std::mutex> lk(g_iw_mu);
    // Zenie de IW (descompilacion Ghidra, EU): Char_ComputeScaledStat 0x2ABB80 = +0x0E del registro de la mision x
    // 50/80/100/130/160 segun la dificultad (x200 la sexta; la 77, Gogeta contra Omega, x100 siempre; sin capsula
    // de Zenie: con ella x1,1..x2) y Char_RoundAndScaleStat 0x2ABD90 = + 20/40/60/100 % por el rango 1-4 (no en los entrenamientos
    // 1-10 ni en la 77) y redondeo a la centena. ponytail: el rango es el nuestro (PantallaDM::Rango); el de IW suma
    // tres notas de la vida, golpes y tiempo (Char_ComputeStatGrade 0x2AC0D0, sin portar).
    constexpr int kZeni[5] = {50, 80, 100, 130, 160}, kExtra[5] = {0, 20, 40, 60, 100};
    if (n >= 1 && n - 1 < int(g_iw_tabla.size())) r.rango = RangoIW(g_iw_tabla[size_t(n - 1)], f, r.segundos);
    const int rango = r.rango >= 0 ? r.rango : PantallaDM::Rango(r.puntos);
    if (n >= 1 && n - 1 < int(g_iw_tabla.size())) {
      const int base = g_iw_tabla[size_t(n - 1)].premio * (n == 77 ? 100 : kZeni[std::clamp(g_iw_dificultad, 0, 4)]);
      const int extra = n <= 10 || n == 77 ? 0 : base / 100 * kExtra[std::clamp(rango, 0, 4)];
      r.zeni = (base + extra + 50) / 100 * 100;
    }
  }
  // Como IW (Battle_ComputeRankAndRecord, EU 0x28A5E0): los combates guardan el mejor RANGO de cada mision (sin
  // puntos; los puntos son de los minijuegos). iw_rango_N = rango + 1 (0 = sin superar).
  {
    const std::string clave = "iw_rango_" + std::to_string(n);
    const int antes = LeerProgreso(clave, 0), ahora = (r.rango >= 0 ? r.rango : PantallaDM::Rango(r.puntos)) + 1;
    r.nuevo_record = ahora > antes;
    if (r.nuevo_record) GuardarProgreso(clave, ahora);
    r.mejor = std::max(antes, ahora);
    RangoMejora("rango_iw_" + std::to_string(n), ahora, 30, 20);   // escalafon: 30 por rango mejorado, 20 la 1.a vez
  }
  dbz3::rpg::Premio(r.zeni);
  if (const auto dm = DM()) g_iw_despues = VideoIW(*dm, 1, n - 1, true);   // FD02 tras la 11...
  if (n == kMisionesDM) g_iw_final = true;   // la ultima de Dragon Mission: los creditos (ENDING.SFD, R7)
  const char* clave_hecho = n > kMisionesDM ? "iw_road_hecho" : "iw_hecho";   // Fighter's Road: su avance
  const int hecho = std::max(LeerProgreso(clave_hecho, n > kMisionesDM ? kMisionesDM : 0), n);
  GuardarProgreso(clave_hecho, hecho);
  REXLOG_INFO("dbz3 misiones: iw: mision {} superada: {} s, {} puntos (record {}), rango {}, {} Zenie, hechas hasta la {}",
              n, r.segundos, r.puntos, r.mejor, r.rango >= 0 ? r.rango : PantallaDM::Rango(r.puntos), r.zeni, hecho);
  g_iw_res = r;
  g_iw_vista = 2;
}

// ---- Avance ----
void Salir() {   // con g_mu: al menu de la pasarela (se guarda donde iba)
  if (g_modo == kIW && DM() && g_fase != kMapaIW && g_fase != kMenu) {   // IW: de la ficha o la derrota, al mapa
    g_iw_vuelta_combate = true;   // H03: la derrota vuelve al mapa sin repetir la intro de 100 cuadros
    g_iw_vista = 0;
    return VolverAlMapa();
  }
  if (g_modo != kTablero && g_fase != kMenu) GuardarProgreso(kClaves[g_modo], g_paso);
  g_pasos.clear();
  g_paso = 0;
  Publicar(kMenu);
}
void Terminar() {   // con g_mu: modo completado
  REXLOG_INFO("dbz3 misiones: {} completado", kClaves[g_modo]);
  if (g_modo != kTablero) GuardarProgreso(kClaves[g_modo], 0);
  if (g_modo == kBuu) {
    g_absorbidos = 1;
    GuardarProgreso("buu_absorbidos", 1);
  }
  g_pasos.clear();
  g_paso = 0;
  Publicar(kMenu);
}
void Siguiente() {   // con g_mu
  ++g_paso;
  g_intentos = 0;
  if (g_paso >= int(g_pasos.size())) {
    if (g_modo == kTablero) return VolverAlTablero();
    if (g_modo == kIW && DM()) return VolverAlMapa();
    return Terminar();
  }
  if (g_modo != kTablero) GuardarProgreso(kClaves[g_modo], g_paso);
  Publicar(kPantalla);
}

// G01-iw-iso B-04 (studies 2): la cache de video (user_data/dbz3/cache_ps2/iw) no llevaba sello de origen, asi que
// al cambiar de ISO se servia el SFD viejo. El sello guarda ruta, tamano, fecha y una huella FNV-1a de los extremos
// de la ISO; si no coincide, la cache se aparta a iw_viejo_<ticks> (no se borra) y se vuelve a extraer.
std::string HuellaFuente(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  constexpr std::streamoff kMuestra = 64 * 1024;
  f.seekg(0, std::ios::end);
  const std::streamoff tam = f.tellg();
  if (tam < 0) return {};
  uint64_t h = 1469598103934665603ull;
  auto leer = [&](std::streamoff off, std::streamsize n) {
    std::vector<char> b(static_cast<size_t>(n));
    f.clear();
    f.seekg(off);
    f.read(b.data(), n);
    for (std::streamsize i = 0; i < f.gcount(); ++i) {
      h ^= uint8_t(b[size_t(i)]);
      h *= 1099511628211ull;
    }
  };
  leer(0, std::streamsize(std::min<std::streamoff>(kMuestra, tam)));
  if (tam > kMuestra) leer(tam - kMuestra, std::streamsize(kMuestra));
  char hex[17] = {};
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
  return std::string(hex);
}

std::string SelloFuenteIw(const std::filesystem::path& j) {
  std::error_code ec;
  if (j.empty() || std::filesystem::is_directory(j, ec)) return {};   // carpeta extraida: no hay cache
  const auto tam = std::filesystem::file_size(j, ec);
  if (ec) return {};
  const auto fecha = std::filesystem::last_write_time(j, ec);
  if (ec) return {};
  return j.string() + "|" + std::to_string(tam) + "|" + std::to_string(fecha.time_since_epoch().count()) + "|" +
         HuellaFuente(j);
}

void PrepararCacheIw(const std::filesystem::path& j) {
  const std::string sello = SelloFuenteIw(j);
  if (sello.empty()) return;
  std::error_code ec;
  const std::filesystem::path cache = dbz3::settings::UserDataRoot() / "cache_ps2" / "iw";
  std::ifstream f(cache / ".fuente", std::ios::binary);
  const std::string previo((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (previo == sello) return;
  if (std::filesystem::exists(cache, ec)) {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path viejo = cache.parent_path() / ("iw_viejo_" + std::to_string(ticks));
    std::filesystem::rename(cache, viejo, ec);
    if (ec)
      REXLOG_WARN("dbz3 misiones: iw: no se pudo apartar la cache vieja de video ({}); se reextrae igualmente",
                  ec.message());
    else
      REXLOG_INFO("dbz3 misiones: iw: la ISO de origen cambio; cache de video anterior en {}", viejo.string());
  }
  std::filesystem::create_directories(cache, ec);
}

void SellarCacheIw(const std::filesystem::path& j) {
  const std::string sello = SelloFuenteIw(j);
  if (sello.empty()) return;
  std::error_code ec;
  const std::filesystem::path cache = dbz3::settings::UserDataRoot() / "cache_ps2" / "iw";
  std::filesystem::create_directories(cache, ec);
  std::ofstream f(cache / ".fuente", std::ios::binary | std::ios::trunc);
  f << sello;
}

// M2 (mod opcional iw.video_apertura): la apertura de la copia de IW al entrar al modo, una vez por sesion. El
// original lo tiene junto a las etiquetas "# VS MODE #" y "# OPENING #" (R7/mapa_recursos/musica_video.md A5) y su
// pantalla exacta sigue sin resolver [H] (opcional.toml: "modo de arranque exacto sin confirmar"); aqui se usa la
// entrada al Dragon Mission, que es cuando el port abre IW. La ISO puede ser carpeta o .iso (cache con sello).
void VideoAperturaIW() {
  if (!dbz3::opcional::Activo("iw.video_apertura")) return;
  static bool hecho = false;
  if (hecho) return;
  // G01-iw-iso B-01: IW USA trae USR/OPENING2.SFD; la ISO EU solo trae USR/OPENING.SFD. Se prueban en orden y solo
  // se da por fallado si no esta ninguno (log claro, sin romper el modo).
  std::vector<std::string> rutas;
  const std::string config = dbz3::opcional::Texto("iw.video_apertura", "ruta_iso", "USR/OPENING2.SFD");
  if (!config.empty()) rutas.push_back(config);
  for (const char* r : {"USR/OPENING2.SFD", "USR/OPENING.SFD"})
    if (std::find(rutas.begin(), rutas.end(), r) == rutas.end()) rutas.emplace_back(r);
  const std::filesystem::path fuente = BuscarJuego("infinite world");
  if (fuente.empty()) {
    REXLOG_WARN("dbz3 misiones: iw: video de apertura: no hay copia de Infinite World en ps2_games");
    return;
  }
  PrepararCacheIw(fuente);
  std::filesystem::path p;
  std::string usado;
  for (const std::string& r : rutas) {
    p = dbz3::opcional::ArchivoDeJuegoPs2("iw", r);
    if (!p.empty()) {
      usado = r;
      break;
    }
    REXLOG_WARN("dbz3 misiones: iw: video de apertura: {} no esta en {}", r, fuente.string());
  }
  if (p.empty()) {   // sin copia de IW (o sin ninguno de los dos nombres): se reintenta al reentrar
    REXLOG_WARN("dbz3 misiones: iw: video de apertura: no se reproduce (faltan USR/OPENING2.SFD y USR/OPENING.SFD)");
    return;
  }
  SellarCacheIw(fuente);
  hecho = true;
  const bool ok = dbz3::video::Reproducir(p);
  REXLOG_INFO("dbz3 misiones: iw: video de apertura (mod iw.video_apertura): {} de {} ({})", usado, fuente.string(),
              ok ? "reproduciendo" : "no se pudo abrir");
}

void Entrar(int m) {   // con g_mu: desde el menu
  g_modo = m;
  g_intentos = 0;
  if (m == kTablero) {
    b2::Entrar();
    return VolverAlTablero();
  }
  if (m == kNave) {
    nave::Entrar();
    return VolverANave();
  }
  if (m == kIW && DM()) {   // Dragon Mission: el mapa del mundo de IW, con el cursor en la mision abierta
    g_iw_vista = 0;
    g_iw_mision = -1;
    REXLOG_INFO("dbz3 misiones: iw: mapa de Dragon Mission (hechas hasta la {})", LeerProgreso("iw_hecho", 0));
    VideoAperturaIW();   // M2 (mod opcional iw.video_apertura)
    return VolverAlMapa();
  }
  g_pasos = PasosDe(m);
  g_paso = std::clamp(LeerProgreso(kClaves[m], 0), 0, std::max(0, int(g_pasos.size()) - 1));
  // se sigue desde la charla de antes del combate pendiente
  while (g_paso > 0 && g_pasos[size_t(g_paso)].tipo != kCharla && g_pasos[size_t(g_paso - 1)].tipo == kCharla) --g_paso;
  if (m == kBuu) {
    g_absorbidos = LeerProgreso("buu_absorbidos", 1) | 1;
    g_forma_bu = 0;
  }
  REXLOG_INFO("dbz3 misiones: {} ({} pasos) desde el paso {}", kClaves[m], g_pasos.size(), g_paso + 1);
  Publicar(kPantalla);
}

bool Lanzar() {   // con g_mu
  const Paso& p = g_pasos[size_t(g_paso)];
  int escenario = 0;
  if (p.escenario >= 0) {
    escenario = p.escenario;
  } else if (p.mapa < 0) {
    escenario = EscenarioB3(p.texto_propio);
  } else {
    escenario = EscenarioPorMapa(p.mapa);
  }
  const int forma1 = p.forma1;
  Reglas reglas = p.reglas;
  g_hud_golpes = 0;
  g_hud_tipo = p.reglas.golpes > 0 ? 1 : p.reglas.sin_atacar ? 2 : 0;
  g_hud_meta = p.reglas.golpes > 0 ? p.reglas.golpes : p.vida_minima;
  reglas.contador = &g_hud_golpes;
  if (g_modo == kBuu && p.jugador == kSuperBu) reglas.traje_jugador = std::max(0, TrajeBu(g_forma_bu));
  g_iw_t0 = NowMs();
  int vida1 = (g_modo == kTablero || g_modo == kNave) && g_vida > 0 ? -g_vida : 0;   // la vida que le quedo (tablero)
  if (g_modo == kNave && g_nave_prueba == 1) vida1 = 1000;   // Prueba II de B2: el jugador tambien con una sola barra
  g_nave_armada = false;   // combate nuevo: la cadena de la prueba I se vuelve a armar en su fin (sub_820FE9B0)
  if (g_nave_encadenada) {   // nave: el combate ya lo encadeno el gancho (sub_820FE9B0); sale de la espera en negro
    g_nave_encadenada = false;
    dbz3::hub::HoldChain(false);
  } else if (!dbz3::hub::RequestBattleEx(p.jugador, p.rival, escenario, true, p.tiempo, vida1, p.vida2, forma1, p.forma2)) {
    return false;
  }
  REXLOG_INFO("dbz3 misiones: {} combate: id {} (forma {}, traje {}) contra id {} (forma {}), escenario {}, tiempo {}, "
              "vida {} / {}, ki del rival {:.1f}", kClaves[g_modo], p.jugador, forma1, reglas.traje_jugador, p.rival,
              p.forma2, escenario, p.tiempo, vida1, p.vida2, reglas.ki_rival);
  Publicar(kLuchando);
  const uint64_t cambio = g_cambio;
  // en el registro, cada combate con su nombre (el tablero repite pasos: tambien el turno)
  const std::string quien = "misiones: " + std::string(kClaves[g_modo]) +
                            std::string() + " paso " +
                            std::to_string(g_paso + 1);
  VigilarCombate(quien.c_str(), reglas,
                 [cambio](const Fin& f) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fase != kLuchando || g_cambio != cambio) return;
    REXLOG_INFO("dbz3 misiones: fin del combate ({} paso {}): gana el lado {}, vida {}", kClaves[g_modo], g_paso + 1,
                f.ganador + 1, f.vida[0]);
    if (g_modo == kTablero) {   // Dragon World: el resultado lo aplica pasarela_b2.cpp y se vuelve al tablero
      b2::Resultado(f.ganador, f.vida[0]);
      return VolverAlTablero();
    }
    if (g_modo == kNave) {   // Nave de Babidi: el Kili lo cuenta pasarela_nave.cpp
      nave::Resultado(f.ganador, f.vida[0], f.segundos, f.golpes);
      return VolverANave();
    }
    // H03: al volver al mapa (resultado o derrota) no se repite Story_MapIntroDelayTick; las esperas del resultado
    // (Story_MapResultDelayTick y Story_MapReloadAfterDelayTick) las lleva PantallaDM::VolverDelCombate.
    if (g_modo == kIW && DM()) g_iw_vuelta_combate = true;
    const Paso& reto = g_pasos[size_t(g_paso)];
    bool gana = f.ganador == 0;
    if (reto.reglas.golpes > 0) gana = f.golpes >= reto.reglas.golpes;
    if (reto.reglas.sin_atacar) gana = !f.ataco && f.segundos == 0 && f.vida[0] >= reto.vida_minima;
    // IW "Fight" (Battle_DetermineResultD_, EU 0x20A060 L216525-216531): al agotarse el tiempo gana el jugador si
    // le queda al menos nivel x1000 de vida (nivel = +0x04; 1 = 1000 de 3000); si no, gana el rival.
    if (reto.iw == 1) gana = gana || (f.segundos == 0 && f.vida[0] >= reto.vida_minima);
    if (reto.iw == 2) gana = f.ganador == 1;                                // IW: dejar ganar al rival
    if (reto.iw == 3) gana = gana && g_iw_transformo.load();               // IW Training No.9: transformado
    if (reto.iw == 4) gana = g_iw_entreno2.load();                         // IW Training No.2: 5 + 5 + 5
    if (reto.iw == 5 || reto.iw == 6) gana = g_iw_entreno.load();          // IW Training No.4 / No.8 (R7)
    if (reto.reglas.golpes > 0 || reto.reglas.sin_atacar) {
      REXLOG_INFO("dbz3 misiones: reto {}: {} golpes, ataco {}, {} s de sobra, vida {} (minimo {})",
                  gana ? "cumplido" : "fallado", f.golpes, f.ataco, f.segundos, f.vida[0], reto.vida_minima);
    }
    if (!gana) {
      ++g_intentos;
      return Publicar(kPerdido);
    }
    const Paso& p = g_pasos[size_t(g_paso)];
    if (g_modo == kBuu && p.desbloquea > 0) {
      g_absorbidos |= 1 << p.desbloquea;
      GuardarProgreso("buu_absorbidos", g_absorbidos);
      REXLOG_INFO("dbz3 misiones: Bu absorbe: {} (absorciones {:#x})", kAbsorciones[p.desbloquea].nombre.es, g_absorbidos);
    }
    if (g_modo == kIW && DM()) GanarIW(p, f);
    Siguiente();
  });
  return true;
}

void CreatePanel();

}  // namespace

// ---- API (HUB y pruebas) ----
bool Start() {
  DatosLeidos();   // la saga lee la ISO de B1 (si no estaba leida)
  LeerIW();
  if (!g_panel.exchange(true)) dbz3::launcher::DeferUi(&CreatePanel);
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_fase != kNada) return false;
  g_cursor = 0;
  Publicar(kMenu);
  return true;
}
bool Busy() { return g_fase_pub.load() != kNada; }

namespace {

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}
std::atomic<uint16_t> g_pad{0};
std::atomic<bool> g_pad_pending{false};

float Ease(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const int fase = g_fase_pub.load();
    if (fase == kLuchando && g_hud_tipo.load() && dbz3::ingame::GameScreen() == 25 &&
        (EstadoCombate() == 3 || EstadoCombate() == 4)) {
      Marcador(io);   // solo con la pelea en marcha (no sobre el menu del final)
    }
    const bool menu = dbz3::ingame::GameScreen() == kMainScreen;
    const bool show = fase != kNada && fase != kLuchando && menu && !dbz3::GameDimmed() && !dbz3::OverlayOpen() &&
                      !g_iw_eligiendo.load();   // Fighter's Road: la rueda del juego por debajo
    if (auto* in = Input(); in && show != blocked_) {
      if (show) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
      blocked_ = show;
    }
    // M6 (mod opcional iw.musica_menus): la rueda del juego (Fighter's Road) suena con la pista de rueda de IW
    // (81, Char_WheelOpenUpdate); la pasarela esta oculta mientras se elige y el mapa no manda su pista.
    const int rueda_iw = g_iw_eligiendo.load() ? PistaMenuIW(3) : -1;
    if (rueda_iw >= 0) MusicaIW(rueda_iw);
    else if (fase != kMapaIW) MusicaIW(-1);
    const float t = float(ImGui::GetTime()), dt = std::min(io.DeltaTime, 0.1f);
    alpha_ =std::clamp(alpha_ + (show ? dt * 4.0f : -dt * 8.0f), 0.0f, 1.0f);
    if (!show) {
      wait_release_ = true;
      if (alpha_ <= 0.0f) return;
    }
    uint16_t pressed = Pad();
    if (wait_release_) {
      if (g_pad.load() == 0 && alpha_ >= 1.0f) wait_release_ = false;
      pressed = 0;
    }
    if (!show || g_auto.load()) pressed = 0;   // prueba automatica: solo avanza el modo (teclas de mas no cuentan)
    if (dbz3::ingame::tutorial::Abierto()) pressed = 0;   // F3: tutorial encima
    std::unique_lock<std::mutex> lk(g_mu);
    if (g_cambio != visto_) {
      visto_ = g_cambio;
      cambio_at_ = t;
    }
    const auto a = S::GameArea(io);
    auto* dl = ImGui::GetForegroundDrawList();
    const int v0 = dl->VtxBuffer.Size;
    S::Backdrop(dl, a, S::Img("hub/fondo") ? "hub/fondo" : "fondo_duelo");   // el del HUB, como sus modos
    S::Sides(dl, a);
    const bool auto_on = g_auto.load() && t - cambio_at_ > kAutoSeg;
    if (g_fase == kMenu) {
      DrawMenu(dl, a, t, pressed, auto_on);
    } else if (g_fase == kTableroF) {
      DrawTablero(dl, a, t, pressed, auto_on);
    } else if (g_fase == kNaveF) {
      DrawNave(dl, a, t, pressed);
    } else if (g_fase == kMapaIW) {
      DrawMapaIW(dl, io, t, pressed, auto_on);
    } else if (g_fase == kPerdido) {
      DrawCombate(dl, a, t, pressed, auto_on, true);
    } else if (g_paso < int(g_pasos.size())) {
      const Paso& p = g_pasos[size_t(g_paso)];
      if (p.tipo == kCharla) DrawCharla(dl, a, t, pressed, auto_on);
      else if (p.tipo == kCombate) DrawCombate(dl, a, t, pressed, auto_on, false);
      else if (p.tipo == kEleccion) DrawEleccion(dl, a, t, pressed, auto_on);
      else if (p.tipo == kInforme) DrawInforme(dl, a, t, pressed, auto_on);
      else DrawFinal(dl, a, t, pressed, auto_on);
    }
    lk.unlock();
    if (alpha_ < 1.0f) {
      for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImU32& c = dl->VtxBuffer[i].col;
        c = (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * alpha_) << 24);
      }
    }
  }

 private:
  uint16_t Pad() {
    if (!g_pad_pending.exchange(true)) {
      dbz3::launcher::DeferUi([]() {
        uint16_t b = 0;
        if (auto* in = Input()) {
          rex::input::X_INPUT_STATE st = {};
          if (in->GetStateForUI(0, &st) == 0) {
            b = static_cast<uint16_t>(st.gamepad.buttons);
            const int lx = int16_t(st.gamepad.thumb_lx), ly = int16_t(st.gamepad.thumb_ly);
            if (ly > 16000) b |= kUp;
            if (ly < -16000) b |= kDown;
            if (lx < -16000) b |= kLeft;
            if (lx > 16000) b |= kRight;
          }
        }
        g_pad = b;
        g_pad_pending = false;
      });
    }
    const uint16_t b = g_pad.load(), pressed = uint16_t(b & ~last_);
    last_ = b;
    return pressed;
  }

  // Durante el combate de un reto de IW, bajo el reloj: "Golpes 12/30" o "Sin atacar - vida 1500 o más".
  static void Marcador(ImGuiIO& io) {
    const S::Area a = S::GameArea(io);
    char txt[96];
    if (g_hud_tipo.load() == 1) {
      std::snprintf(txt, sizeof(txt), T::T("Golpes %d/%d", "Hits %d/%d"), g_hud_golpes.load(), g_hud_meta.load());
    } else {
      std::snprintf(txt, sizeof(txt), T::T("Sin atacar - vida %d o más", "No attacking - health %d or more"),
                    g_hud_meta.load());
    }
    S::MenuText(ImGui::GetForegroundDrawList(), a.At(600, 172), 34 * a.s, txt, true, false);
  }

  void Ayuda(ImDrawList* dl, const S::Area& a, const std::vector<std::pair<const char*, const char*>>& items) {
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, items);
  }
  void Bocadillo(ImDrawList* dl, const S::Area& a, float t, const char* texto, float cola) {
    const float in = Ease((t - cambio_at_) / 0.3f);
    S::Speech(dl, a.At(60, 640 + 30 * (1 - in)), a.At(1140, 850 + 30 * (1 - in)), a.s, texto, cola);
  }

  // Menu de la pasarela: los cuatro modos en el marco del juego; los que no estan listos, en gris con el motivo.
  void DrawMenu(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    S::Banner(dl, a, T::T("Pasarela", "Gateway"));
    const float y0 = 140, paso = 470.0f / float(kModos);   // el marco acaba antes del bocadillo (640)
    S::Frame(dl, a.At(150, y0), a.At(1050, y0 + 30 + paso * float(kModos)), a.s);
    std::string motivo[kModos];
    bool listo[kModos];
    for (int m = 0; m < kModos; ++m) listo[m] = Listo(m, &motivo[m]);
    if (pressed & kUp) g_cursor = (g_cursor + kModos - 1) % kModos;
    if (pressed & kDown) g_cursor = (g_cursor + 1) % kModos;
    for (int m = 0; m < kModos; ++m) {
      const float y = y0 + 15 + paso * (float(m) + 0.5f);
      if (m == g_cursor) S::Highlight(dl, a.At(165, y - paso * 0.5f + 6), a.At(1035, y + paso * 0.5f - 6), t);
      S::MenuText(dl, a.At(600, y), 40 * a.s, kTitulos[m](), m == g_cursor, !listo[m]);
    }
    // sin la ISO de PS2 que pide la campana: A abre el dialogo para elegirla (en un hilo; el juego sigue)
    const int falta = listo[g_cursor] ? -1
                      : g_cursor == kSagaB1 ? (IsoPs2(0).empty() ? 0 : -1)
                      : g_cursor == kNave || g_cursor == kTablero ? (IsoPs2(1).empty() ? 1 : -1)
                      : g_cursor == kIW ? (IsoPs2(2).empty() ? 2 : -1) : -1;
    std::string texto = listo[g_cursor] ? std::string(kAyudas[g_cursor]()) : motivo[g_cursor];
    if (falta >= 0) {
      texto += "\n";
      const std::string av = AvisoEleccionPs2();
      texto += av.empty() ? T::T("Pulsa A para elegir su ISO (no se copia nada).", "Press A to pick its ISO (nothing is copied).")
                          : av.c_str();
    }
    S::Speech(dl, a.At(60, 640), a.At(1140, 840), a.s, texto.c_str(), 0.12f);
    Ayuda(dl, a, {{"A", falta >= 0 ? T::T("Elegir ISO", "Pick ISO") : T::T("Elegir", "Select")},
                  {"B", T::T("Volver", "Back")}});
    if ((pressed & (kA | kStart)) && falta >= 0) ElegirIsoPs2EnSegundoPlano(falta);
    if (pressed & kB) {
      REXLOG_INFO("dbz3 misiones: fuera de la pasarela");
      return Publicar(kNada);
    }
    if ((pressed & (kA | kStart)) && listo[g_cursor]) Entrar(g_cursor);
    (void)auto_on;
  }

  void Cara(ImDrawList* dl, const S::Area& a, int id, bool derecha, bool on, float t) {
    if (id < 0) return;
    // marco 170..450, nombre debajo (hasta ~500) y la franja del aviso desde 525: no se pisan
    DibujarCara(dl, a, id, derecha ? 800.0f : 120.0f, 170.0f, 280.0f, on, nullptr, t, derecha);
    S::MenuText(dl, a.At((derecha ? 800.0f : 120.0f) + 140.0f, 170.0f + 280.0f + 30.0f), 44 * a.s,
                NombreDe(id).c_str(), on, false);
  }

  void DrawCharla(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    const Paso& p = g_pasos[size_t(g_paso)];
    S::Banner(dl, a, kTitulos[g_modo]());
    Cara(dl, a, p.quien, p.derecha, true, t);
    Bocadillo(dl, a, t, p.texto_propio.empty() ? p.texto() : p.texto_propio.c_str(), p.derecha ? 0.78f : 0.22f);
    Ayuda(dl, a, {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on) pressed |= kA;
    if (pressed & kB) return Salir();
    if (pressed & (kA | kStart)) Siguiente();
  }

  // Tarjeta del combate (y la de reintentar tras perder): las dos caras, "VS" y el aviso.
  void DrawCombate(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, bool perdido) {
    const Paso& p = g_pasos[size_t(g_paso)];
    S::Banner(dl, a, kTitulos[g_modo]());
    int j = p.jugador;
    Cara(dl, a, j, false, !perdido, t);
    Cara(dl, a, p.rival, true, true, t);
    const float in = Ease((t - cambio_at_) / 0.35f);
    S::MenuText(dl, a.At(600, 330), (110 + 40 * (1 - in)) * a.s, "VS", true, false);
    std::string aviso = p.mapa < 0 ? std::string() : std::string(p.texto());
    if (g_modo == kBuu && p.jugador == kSuperBu) aviso = std::string(kAbsorciones[g_forma_bu].nombre()) + ". " + aviso;
    if (g_modo == kTablero && g_vida > 0) {
      char v[64];
      std::snprintf(v, sizeof(v), T::T(" Vida: %d.", " Health: %d."), g_vida);
      aviso += v;
    }
    if (perdido) {
      const bool tablero = g_modo == kTablero;
      dl->AddRectFilled(a.At(150, 525), a.At(1050, 600), IM_COL32(8, 30, 48, 215), 10 * a.s);
      S::MenuText(dl, a.At(600, 562), 38 * a.s, tablero ? T::T("¡Fracaso!", "Failure!") : T::T("¡Derrota!", "Defeat!"),
                  true, false);
      S::Speech(dl, a.At(60, 640), a.At(1140, 790), a.s,
                tablero ? T::T("Has caído en el tablero: vuelves a Kame House y las casillas se reinician.",
                               "You fell on the board: back to Kame House, and the spaces reset.")
                        : T::T("¿Lo intentas otra vez?", "Want to try again?"),
                -1);
      Ayuda(dl, a, {{"A", tablero ? T::T("Al tablero", "To the board") : T::T("Reintentar", "Retry")},
                    {"B", T::T("Salir", "Quit")}});
      if (auto_on) pressed |= kA;
      if (pressed & kB) return Salir();
      if (pressed & kA) {
        if (tablero) return VolverAlTablero();
        Publicar(kPantalla);
      }
      return;
    }
    if (!aviso.empty()) {
      dl->AddRectFilled(a.At(150, 525), a.At(1050, 600), IM_COL32(8, 30, 48, 215), 10 * a.s);
      S::MenuText(dl, a.At(600, 562), 32 * a.s, aviso.c_str(), true, false);
    }
    std::string texto = NombreDe(j) + T::T(" contra ", " versus ") + NombreDe(p.rival) + ".";
    if (g_intentos > 0) {
      char k[48];
      std::snprintf(k, sizeof(k), T::T(" Intento %d.", " Try %d."), g_intentos + 1);
      texto += k;
    }
    S::Speech(dl, a.At(60, 640), a.At(1140, 790), a.s, texto.c_str(), -1);
    Ayuda(dl, a, {{"A", T::T("¡A luchar!", "Fight!")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on) pressed |= kA;
    if (pressed & kB) return Salir();
    if ((pressed & kA) && !Lanzar()) REXLOG_WARN("dbz3 misiones: el conector no acepto el combate (otro pendiente)");
  }

  // Dragon Mission de IW (frente P1): mapa del mundo, ficha y resultado con los graficos de la copia del jugador
  // (pasarela_iw.cpp). Lo que no cambia en cada cuadro (tabla, motivos, records, avance) se rehace al cambiar de pantalla.
  void DrawMapaIW(ImDrawList* dl, ImGuiIO& io, float t, uint16_t pressed, bool auto_on) {
    const auto dm = DM();
    PantallaDM& pantalla_ = g_iw_road ? pantalla_road_ : pantalla_dm_;   // Fighter's Road: su propio cursor
    const auto textos = IW();
    if (!dm) return Salir();
    if (g_iw_vuelta_combate.exchange(false)) pantalla_.VolverDelCombate();   // H03: vuelta de combate, sin intro
    if (iw_cambio_ != g_cambio || iw_gen_ != g_iw_gen.load()) {
      iw_cambio_ = g_cambio;
      iw_gen_ = g_iw_gen.load();
      {
        std::lock_guard<std::mutex> lk(g_iw_mu);
        tabla_iw_ = g_iw_tabla;
      }
      info_.motivo.assign(size_t(tabla_iw_.size()) + 1, std::string());
      info_.mejor.assign(size_t(tabla_iw_.size()) + 1, 0);
      info_.road = g_iw_road;
      info_.hecho = g_iw_road ? LeerProgreso("iw_road_hecho", kMisionesDM) : LeerProgreso("iw_hecho", 0);
      info_.abiertas = int(tabla_iw_.size());
      for (const MisionTablaIW& m : tabla_iw_) {
        if (m.numero < 1 || m.numero > int(tabla_iw_.size())) continue;
        if (const char* no = MotivoIW(m)) info_.motivo[size_t(m.numero)] = no;
        info_.mejor[size_t(m.numero)] = LeerProgreso("iw_rango_" + std::to_string(m.numero), 0);   // mejor rango + 1
      }
      // abiertas: hasta la primera sin hacer que sea jugable o que solo pida un personaje que el jugador puede crear
      // (Pack Infinite World): esa frena el avance, como en IW, y dice a quien falta. Las que aun no estan portadas
      // (entrenamientos de mandos) se saltan para no dejar la partida atascada. Antes se saltaba todo lo bloqueado y se
      // abrian misiones de mas adelante (informe R6: tras la 73 salia la 76).
      auto falta_pj = [](const MisionTablaIW& m) {
        return m.numero > kMisionesDM ? IWaB3(m.rival) < 0 : (IWaB3(m.jugador) < 0 || IWaB3(m.rival) < 0);
      };
      for (const MisionTablaIW& m : tabla_iw_) {
        if ((m.numero > kMisionesDM) != g_iw_road) continue;   // Dragon Mission o Fighter's Road
        if (m.numero > info_.hecho && (info_.motivo[size_t(m.numero)].empty() || falta_pj(m))) {
          info_.abiertas = m.numero;
          break;
        }
      }
    }
    info_.dm = dm.get();
    info_.textos = textos.get();
    info_.tabla = &tabla_iw_;
    if (g_iw_mision != 0) {
      pantalla_.Elegir(info_, g_iw_mision < 0 ? info_.abiertas : g_iw_mision);
      g_iw_mision = 0;
    }
    if (auto_on) pressed |= kA;
    if (dbz3::iwmini::Activo()) pressed = 0;   // minijuego en curso (frente P4): el mapa espera
    // video de la historia encima: lee A/START (y 0,4 s despues, que la A que lo salta no elija en el mapa)
    const bool en_video = dbz3::video::Activo();
    if (iw_en_video_ && !en_video) iw_mudo_hasta_ = NowMs() + 400;
    iw_en_video_ = en_video;
    if (en_video || NowMs() < iw_mudo_hasta_) pressed = 0;
    const int menu_iw = g_iw_vista != 0 ? PistaMenuIW(g_iw_vista) : -1;   // M6 (mod opcional iw.musica_menus)
    MusicaIW(dbz3::iwmini::Activo() ? PistaMinijuegoIW()
             : en_video || pantalla_.Contando() || g_iw_esperando_video ? -1
             : menu_iw >= 0 ? menu_iw : pantalla_.Pista(info_));
    if (!en_video && g_iw_esperando_video) {   // acabo el video de antes: su combate
      g_iw_esperando_video = false;
      SondaIW();   // (tambien tras el video de antes)
      if (g_iw_tras_video.iw == 3) VigilarFormaIW();
      if (g_iw_tras_video.iw == 5 || g_iw_tras_video.iw == 6) VigilarEntrenoIW(g_iw_tras_video.iw);
      g_pasos = {g_iw_tras_video};
      g_paso = 0;
      g_intentos = 0;
      if (!Lanzar()) Publicar(kPantalla);
      return;
    }
    if (dbz3::iwmini::Resultado mr; dbz3::iwmini::TomarResultado(mr)) {   // vuelta de un minijuego: su resultado
      ResultadoDM r;
      r.segundos = mr.centesimas / 100;
      r.puntos = mr.puntos;
      r.rango = mr.rango;   // el del minijuego (su formula de IW), no el recalculado por puntos (auditoria8/B)
      if (mr.superado)   // escalafon: 10 por rango mejorado del minijuego
        RangoMejora("rango_iw_mini_" + std::to_string(mr.clave),
                    (mr.rango >= 0 ? mr.rango : PantallaDM::Rango(mr.puntos)) + 1, 10);
      const std::string clave = "iw_mini_" + std::to_string(mr.clave);
      const int antes = LeerProgreso(clave, 0);
      r.nuevo_record = r.puntos > antes;
      if (r.nuevo_record) GuardarProgreso(clave, r.puntos);
      r.mejor = std::max(antes, r.puntos);
      // F5: Zenie = el premio de Results_ComputeTotalsAndRewards (tabla del rango x factor de la dificultad, a la
      // centena; sin rango, el de fallo), que es lo que cada minijuego ya da como puntos. Battle_ApplyResultRewards lo
      // suma con o sin rango (premio en +0x5A = bloque de resultados +0x13). ponytail: el tope de IW (999900) no se
      // aplica: el monedero es el comun del proyecto (rpg.cpp, tope propio).
      r.zeni = r.puntos;
      if (mr.clave == 0x7D3) r.ladrillos = mr.monedas;   // fila BRICKS DESTROYED (334/1 t17, fila 2)
      r.clave_mini = mr.clave;                            // s1 F04.3: titulo del minijuego arriba
      dbz3::rpg::Premio(r.zeni);
      for (const CasillaDM& c : dm->casillas) {   // su escena de despues (FD08B tras el 10, la nave de Namek)
        if (c.tipo == 0 && c.clave == mr.clave && mr.superado) g_iw_despues = VideoIW(*dm, 0, c.indice, true);
      }
      g_iw_res = r;
      g_iw_vuelta_combate = true;   // H03: vuelta de minijuego: el mapa no repite la intro de 100 cuadros
      g_iw_vista = 2;
      ++g_cambio;
      REXLOG_INFO("dbz3 misiones: iw: minijuego {:#x} acabado: {} ({} s, {} puntos)", mr.clave,
                  mr.superado ? "superado" : "no superado", r.segundos, r.puntos);
    }
    // PRUEBA (F5): DBZ3_TEST_IW_RESMINI=1 ensena una vez, al abrir el mapa, un resultado de ladrillos (0x7D3) de muestra
    // (5 filas con su conteo); no suma Zenie ni guarda nada.
    if (static bool hecho = false; !hecho && g_iw_vista == 0 && std::getenv("DBZ3_TEST_IW_RESMINI")) {
      hecho = true;
      ResultadoDM r;
      r.segundos = 95, r.puntos = r.zeni = 4200, r.rango = 3, r.ladrillos = 37, r.nuevo_record = true;
      r.clave_mini = 0x7D3;   // s1 F04.3: con su titulo ("Destroy the bricks!")
      g_iw_res = r;
      g_iw_vista = 2;
      ++g_cambio;
      REXLOG_INFO("dbz3 misiones: iw: prueba: resultado de minijuego de muestra");
    }
    if (g_iw_vista == 3) return DrawElegirRoad(dl, io, t, pressed);
    if (g_iw_vista == 0) {
      dbz3::iwmini::Dificultad(pantalla_.Dificultad());   // F5: los 5 niveles de la ficha, tal cual, al minijuego
      // R12: el personaje que se lleva en el mapa (IW: el de la ultima mision, DM+0x374; el vuelo lo compara con 2 / 9)
      dbz3::iwvuelo::PersonajeDelMapa(info_.hecho >= 1 && info_.hecho <= int(tabla_iw_.size()) ? tabla_iw_[size_t(info_.hecho - 1)].jugador : -1);
      if (dbz3::ingame::tutorial::Mostrar(g_iw_road ? "fighters_road" : "dragon_mission")) pressed = 0;   // F3
      const auto ac = pantalla_.Mapa(dl, io, t, pressed, info_);
      if (ac == PantallaDM::kSalir && g_iw_road) {   // B o Y en Fighter's Road: de vuelta a Dragon Mission
        g_iw_road = false;
        ++g_cambio;
        return;
      }
      if (ac == PantallaDM::kSalir) return Salir();
      if (ac == PantallaDM::kRoad) {   // Y: Fighter's Road, primero el luchador en la rueda del juego
        int traje = 0;
        dbz3::modos::UltimoDuelo(&g_iw_luchador, &traje);
        g_iw_luchador = LeerProgreso("iw_road_luchador", g_iw_luchador);
        REXLOG_INFO("dbz3 misiones: iw: Fighter's Road: eleccion del luchador ({}): rueda del juego", g_iw_luchador);
        EleccionRoadNativa();
        return;
      }
      if (ac == PantallaDM::kMinijuego) REXLOG_INFO("dbz3 misiones: iw: minijuego del mapa (frente P4)");
      if (ac == PantallaDM::kElegir) {
        g_iw_vista = 1;
        ++g_cambio;
        REXLOG_INFO("dbz3 misiones: iw: ficha de la mision {}", pantalla_.Mision(info_));
      }
    } else if (g_iw_vista == 1) {
      const int n = pantalla_.Mision(info_);
      Paso x;
      info_.regla.clear();
      if (n >= 1 && n <= int(tabla_iw_.size())) x = PasoIW(tabla_iw_[size_t(n - 1)], info_.regla);
      const auto ac = pantalla_.Ficha(dl, io, t, pressed, info_);
      if (ac == PantallaDM::kVolver || x.tipo != kCombate) {
        g_iw_vista = 0;
        ++g_cambio;
      } else if (ac == PantallaDM::kEmpezar) {
        if (x.reglas.nivel_com < 0) x.reglas.nivel_com = pantalla_.NivelCom();
        g_iw_dificultad = pantalla_.Dificultad();
        g_pasos = {x};
        g_paso = 0;
        g_intentos = 0;
        g_iw_vista = 0;
        REXLOG_INFO("dbz3 misiones: iw: MISSION START mision {} (COM {})", n, x.reglas.nivel_com);
        if (const int v = VideoIW(*dm, 1, n - 1, false); v >= 0) {   // FD01B antes de la 11...
          REXLOG_INFO("dbz3 misiones: iw: escena de la historia antes de la mision {}: video {} ({} lineas)", n, v,
                      dm->subtitulos[size_t(v)].size());
          const auto ruta = RutaVideoIW(*dm, v);
          // DBZ3_IW_ESCENA: escena de MOTOR decodificada (generar.py) en vez del video/respaldo, para probarla.
          const bool forzar = std::getenv("DBZ3_IW_ESCENA") != nullptr;
          if (!forzar && !ruta.empty() && dbz3::video::Reproducir(ruta, dm->subtitulos[size_t(v)])) {   // el original
            REXLOG_INFO("dbz3 misiones: iw: video de la historia {} ({}): reproduciendo", v, ruta.filename().string());
            g_iw_tras_video = x;
            g_iw_esperando_video = true;
            ++g_cambio;
            return;
          }
          dbz3::cinematica::Empezar(EscenaHistoriaIW(*dm, v));   // respaldo: con los modelos de B3 (o la de motor)
        }
        SondaIW();
        if (x.iw == 3) VigilarFormaIW();
        if (x.iw == 4) VigilarEntreno2();
        if (x.iw == 5 || x.iw == 6) VigilarEntrenoIW(x.iw);
        if (!Lanzar()) Publicar(kPantalla);   // otro combate pendiente: la tarjeta lo reintenta
      }
    } else if (pantalla_.Resultado(dl, io, t, pressed, info_, g_iw_res)) {
      g_iw_vista = 0;
      ++g_cambio;
      if (g_iw_despues >= 0) {   // la escena de la historia de despues, sobre el mapa
        REXLOG_INFO("dbz3 misiones: iw: escena de la historia despues: video {} ({} lineas)", g_iw_despues,
                    dm->subtitulos[size_t(g_iw_despues)].size());
        pantalla_.Contar(g_iw_despues);
        g_iw_despues = -1;
      } else if (g_iw_final) {   // creditos del final de IW tras la mision 96 (el mando lo bloquea el video)
        g_iw_final = false;
        REXLOG_INFO("dbz3 misiones: iw: final de Dragon Mission: creditos (ENDING.SFD): {}",
                    dbz3::video::ReproducirIW(dbz3::video::kFinalIW) ? "reproduciendo" : "no esta");
      }
    }
  }

  // Fighter's Road como en IW (LoadWaitFightersRoadSelectionFunc: la rueda normal del select): el piloto del Tag Team
  // lleva al Duelo > 1P VS COM y a su rueda (la pasarela se oculta), el jugador elige, el aviso de select_ext.cpp da el
  // id, y el piloto vuelve atras (Y: menu de capsulas -> rueda -> Duelo -> menu principal) y abre el mapa del Road. Si
  // no se llega a la rueda o el jugador sale sin elegir, la eleccion de respaldo de abajo (o el mapa de Dragon Mission).
  static void EleccionRoadNativa() {   // con g_mu
    if (g_iw_eligiendo.exchange(true)) return;
    g_iw_elegido = -1;
    dbz3::ingame::RegistrarEleccion([](int jugador, int id) {
      if (jugador == 0 && g_iw_elegido.load() < 0) g_iw_elegido = id;
    });
    std::thread([] {
      namespace P = dbz3::ingame::pilot;
      constexpr uint16_t kYp = 0x8000;
      constexpr int kDuelo = 14;
      auto esperar = [](auto cond, int64_t ms) {
        for (const int64_t fin = NowMs() + ms; NowMs() < fin; std::this_thread::sleep_for(std::chrono::milliseconds(50)))
          if (cond()) return true;
        return false;
      };
      esperar([] { return !P::Busy() && dbz3::ingame::GameScreen() == kMainScreen; }, 3000);
      dbz3::ingame::PilotarAlDuelo(0);   // Duelo > 1P VS COM
      const bool rueda = esperar([] { return !P::Busy() && dbz3_select_live(); }, 15000);
      REXLOG_INFO("dbz3 misiones: iw: Fighter's Road: rueda del juego {} (pantalla {})", rueda ? "abierta" : "sin abrir",
                  dbz3::ingame::GameScreen());
      // elige, o sale (de vuelta al menu principal) sin elegir
      esperar([] { return g_iw_elegido.load() >= 0 || (!P::Busy() && dbz3::ingame::GameScreen() == kMainScreen); },
              rueda ? 180000 : 0);
      dbz3::ingame::RegistrarEleccion(nullptr);
      const int id = g_iw_elegido.load();
      if (id >= 0) {   // de vuelta: menu de capsulas -> rueda -> Duelo -> menu principal (Y; nunca en el menu
        esperar([] { return !P::Busy(); }, 3000);   // principal, que Y ahi va al titulo)
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        // un Y cada vez, mirando la pantalla despues de cada uno: se para en el menu principal (la rueda es parte de la
        // pantalla del Duelo, 14; PressUntilBrowse/PressToLeave seguian pulsando y llegaban al titulo)
        for (int i = 0; i < 10 && dbz3::ingame::GameScreen() != kMainScreen; ++i) {
          REXLOG_INFO("dbz3 misiones: iw: Fighter's Road: atras (Y) desde la pantalla {} (rueda {})",
                      dbz3::ingame::GameScreen(), dbz3_select_live());
          P::Run({P::Press(kYp)});
          esperar([] { return !P::Busy(); }, 2000);
          esperar([] { return dbz3::ingame::GameScreen() == kMainScreen; }, 900);
        }
        (void)kDuelo;
      }
      std::lock_guard<std::mutex> lk(g_mu);
      if (id >= 0) {
        g_iw_luchador = id;
        GuardarProgreso("iw_road_luchador", id);
        g_iw_road = true;
        g_iw_vista = 0;
        g_iw_mision = -1;
        REXLOG_INFO("dbz3 misiones: iw: Fighter's Road con el id {} (rueda del juego; hechos hasta el {})", id,
                    LeerProgreso("iw_road_hecho", kMisionesDM));
      } else {
        g_iw_vista = rueda ? 0 : 3;   // sin rueda: la eleccion de respaldo
        REXLOG_INFO("dbz3 misiones: iw: Fighter's Road: sin eleccion en la rueda ({})", rueda ? "salio" : "respaldo");
      }
      ++g_cambio;
      g_iw_eligiendo = false;
    }).detach();
  }

  // Fighter's Road: el luchador para los 105 combates (izquierda/derecha, A empieza, B vuelve). ponytail: en IW es la
  // rueda del select del juego (EU 0x2B1150, LoadWaitCharacterWheelFunc(2)); aqui, la cara del marcador y su nombre
  // como en los retos del HUB, con el del ultimo Duelo de partida (la rueda nativa: con el piloto del Tag Team).
  void DrawElegirRoad(ImDrawList* dl, ImGuiIO& io, float t, uint16_t pressed) {
    const auto a = S::GameArea(io);
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 235));
    for (int dir : {-1, 1}) {
      if (!(pressed & (dir < 0 ? kLeft : kRight))) continue;
      for (int k = 0, id = g_iw_luchador; k < 64; ++k) {
        id = (id + dir + 64) % 64;
        if (dbz3::modos::EsJugable(id)) {
          g_iw_luchador = id;
          break;
        }
      }
    }
    S::Banner(dl, a, "Fighter's Road");
    const ImVec2 g0 = a.At(S::mode::kX0, S::mode::kTop), g1 = a.At(S::mode::kX1, S::mode::kTop + 300);
    S::Frame(dl, g0, g1, a.s);
    if (ImTextureID c = CaraDe(g_iw_luchador))
      dl->AddImage(c, a.At(S::mode::kCx - 90, S::mode::kTop + 40), a.At(S::mode::kCx + 90, S::mode::kTop + 220));
    S::MenuText(dl, a.At(S::mode::kCx, S::mode::kTop + 260), S::mode::kHead * a.s, NombreDe(g_iw_luchador).c_str(),
                true, false, (g1.x - g0.x) * 0.9f);
    S::Arrows(dl, ImVec2(g0.x - 18 * a.s, g0.y), ImVec2(g1.x + 18 * a.s, g1.y), 40 * a.s, t);
    S::mode::Help(dl, a, {{"A", T::T("Empezar", "Start")}, {"B", T::T("Volver", "Back")}});
    if (pressed & kB) {
      g_iw_vista = 0;
      ++g_cambio;
    } else if (pressed & (kA | kStart)) {
      GuardarProgreso("iw_road_luchador", g_iw_luchador);
      g_iw_road = true;
      g_iw_vista = 0;
      g_iw_mision = -1;
      ++g_cambio;
      REXLOG_INFO("dbz3 misiones: iw: Fighter's Road con el id {} (hechos hasta el {})", g_iw_luchador,
                  LeerProgreso("iw_road_hecho", kMisionesDM));
    }
  }

  // Imagenes de las misiones de IW (titulo y texto de la copia del jugador) como texturas.
  void TexturasIW() {
    const auto d = IW();
    if (!d || tex_iw_gen_ == g_iw_gen.load()) return;
    auto* drawer = dbz3::launcher::PreviewDrawer();
    if (!drawer) return;
    tex_iw_gen_ = g_iw_gen.load();
    tex_iw_.clear();
    for (size_t i = 0; i < d->misiones.size(); ++i) {
      for (int k = 0; k < 2; ++k) {
        const Imagen& img = k == 0 ? d->misiones[i].titulo : d->misiones[i].texto;
        if (!img) continue;
        Tex t;
        t.tex = drawer->CreateTexture(uint32_t(img.w), uint32_t(img.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                      img.rgba.data());
        t.w = float(img.w);
        t.h = float(img.h);
        if (t.tex) tex_iw_[int(i) * 2 + k] = std::move(t);
      }
    }
  }

  // Mision de IW: su titulo y su texto (imagenes del juego de origen, en ingles) en una franja oscura, y en el
  // bocadillo las reglas en el idioma del juego.
  void DrawInforme(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    const Paso& p = g_pasos[size_t(g_paso)];
    S::Banner(dl, a, kTitulos[g_modo]());
    TexturasIW();
    const float in = Ease((t - cambio_at_) / 0.35f);
    S::Frame(dl, a.At(230, 150 + 20 * (1 - in)), a.At(970, 600 + 20 * (1 - in)), a.s);
    dl->AddRectFilled(a.At(250, 170), a.At(950, 580), IM_COL32(16, 22, 48, 235), 8 * a.s);
    auto img = [&](int k, float cx, float cy, float alto) {
      auto it = tex_iw_.find(p.informe * 2 + k);
      if (it == tex_iw_.end()) return false;
      const Tex& x = it->second;
      const float w = x.w * alto / x.h;
      dl->AddImage(reinterpret_cast<ImTextureID>(x.tex.get()), a.At(cx - w * 0.5f, cy - alto * 0.5f),
                   a.At(cx + w * 0.5f, cy + alto * 0.5f));
      return true;
    };
    if (!img(0, 600, 225, 66)) S::MenuText(dl, a.At(600, 225), 44 * a.s, T::T("Misión", "Mission"), true, false);
    img(1, 600, 410, 280);
    Bocadillo(dl, a, t, p.texto_propio.empty() ? p.texto() : p.texto_propio.c_str(), 0.5f);
    Ayuda(dl, a, {{"A", T::T("Aceptar", "OK")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on) pressed |= kA;
    if (pressed & kB) return Salir();
    if (pressed & (kA | kStart)) Siguiente();
  }

  // Bu: con que absorcion lucha el siguiente combate (las que tiene; las demas, en gris).
  void DrawEleccion(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    S::Banner(dl, a, kTitulos[g_modo]());
    Cara(dl, a, kSuperBu, false, true, t);
    const int n = kNAbsorciones;
    const float y0 = 190, paso = 90;
    S::Frame(dl, a.At(470, y0), a.At(1110, y0 + 30 + paso * float(n)), a.s);
    if (pressed & kUp) {
      for (int k = 1; k <= n; ++k) {
        const int c = (g_forma_bu + n - k) % n;
        if (g_absorbidos & (1 << c)) {
          g_forma_bu = c;
          break;
        }
      }
    }
    if (pressed & kDown) {
      for (int k = 1; k <= n; ++k) {
        const int c = (g_forma_bu + k) % n;
        if (g_absorbidos & (1 << c)) {
          g_forma_bu = c;
          break;
        }
      }
    }
    for (int k = 0; k < n; ++k) {
      const float y = y0 + 15 + paso * (float(k) + 0.5f);
      const bool tiene = (g_absorbidos & (1 << k)) != 0;
      if (k == g_forma_bu) S::Highlight(dl, a.At(485, y - paso * 0.5f + 6), a.At(1095, y + paso * 0.5f - 6), t);
      S::MenuText(dl, a.At(790, y), 38 * a.s, tiene ? kAbsorciones[k].nombre() : "???", k == g_forma_bu, !tiene);
    }
    Bocadillo(dl, a, t, T::T("¿Con qué absorción lucha Bu en el siguiente combate?",
                             "Which absorption does Buu fight with in the next battle?"), 0.22f);
    Ayuda(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on) {   // prueba: la absorcion mas nueva
      for (int k = n - 1; k >= 0; --k) {
        if (g_absorbidos & (1 << k)) {
          g_forma_bu = k;
          break;
        }
      }
      pressed |= kA;
    }
    if (pressed & kB) return Salir();
    if (pressed & (kA | kStart)) {
      REXLOG_INFO("dbz3 misiones: Bu elige {} (traje {})", kAbsorciones[g_forma_bu].nombre.es, TrajeBu(g_forma_bu));
      Siguiente();
    }
  }

  void DrawFinal(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    const Paso& p = g_pasos[size_t(g_paso)];
    S::Banner(dl, a, kTitulos[g_modo]());
    Cara(dl, a, p.quien, false, true, t);
    S::MenuText(dl, a.At(850, 330), 56 * a.s, T::T("¡Completado!", "Complete!"), true, false);
    Bocadillo(dl, a, t, p.texto(), 0.22f);
    Ayuda(dl, a, {{"A", T::T("Volver", "Back")}});
    if (auto_on) pressed |= kA;
    if (pressed & (kA | kStart | kB)) Terminar();
  }

  // Tablero: Dragon World de B2 (pasarela_b2.cpp); aqui solo se piden sus combates.
  void DrawTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    (void)auto_on;   // el tablero lleva su propio ritmo en la prueba automatica
    int c[14] = {};
    if (dbz3::ingame::tutorial::Mostrar("dragon_world")) pressed = 0;   // F3
    const int r = b2::Dibujar(dl, a, t, pressed, g_auto.load(), c);
    if (r == 1) return Salir();
    if (r == 2) CombateTablero(c);
  }
  // Nave de Babidi de B2 (pasarela_nave.cpp); aqui solo se piden sus combates.
  void DrawNave(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed) {
    int c[6] = {};
    if (dbz3::ingame::tutorial::Mostrar("nave_babidi")) pressed = 0;   // F3
    const int r = nave::Dibujar(dl, a, t, pressed, g_auto.load(), c);
    if (r == 1) return Salir();
    if (r == 2) CombateNave(c);
  }

  struct Tex {
    std::unique_ptr<rex::ui::ImmediateTexture> tex;
    float w = 0, h = 0;
  };
  std::map<int, Tex> tex_iw_;
  uint32_t tex_iw_gen_ = ~0u;
  PantallaDM pantalla_dm_;               // Dragon Mission (P1)
  PantallaDM pantalla_road_;             // Fighter's Road (R1)
  InfoDM info_;
  std::vector<MisionTablaIW> tabla_iw_;
  bool iw_en_video_ = false;
  int64_t iw_mudo_hasta_ = 0;
  uint64_t iw_cambio_ = ~0ull;
  uint32_t iw_gen_ = ~0u;
  bool blocked_ = false, wait_release_ = true;
  uint16_t last_ = 0;
  uint64_t visto_ = ~0ull;
  float cambio_at_ = 0, alpha_ = 0;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

void Abrir() {
  if (!Start()) REXLOG_WARN("dbz3 misiones: no se pudo abrir la pasarela");
}
bool Lista() { return true; }   // US y EU
const char* Por() {
  return T::T("Modos de Budokai 1 y 2: la saga de Namek, el tablero, Gotenks gordo y las absorciones de Bu.",
              "Budokai 1 and 2 modes: the Namek saga, the board, Fat Gotenks and Buu's absorptions.");
}
const bool g_hub = dbz3::hub::RegisterMode("pasarela", &Abrir, &Busy) && dbz3::hub::RegisterGate("pasarela", &Lista, &Por);

// ---- Nave de Babidi: prueba I encadenada (frente 08). En B2 el rival cae y se levanta y el combate sigue, sin pantalla
// VS; aqui el combate siguiente se pide en el fin del actual. sub_820FE9B0 (b2.c: dentro del fin de combate de B3,
// corre antes que sub_821D3628) es lo unico que corre en el hilo del juego con el combate todavia nuestro
// (hub::OwnBattle, g_phase = 3), que es cuando hub::ChainBattleEx lo acepta. La nave arranca el encadenado en
// CombateNave (nave::Seguir) sin volver a pedirlo.
DBZ3_HOOK(sub_820FE9B0, dbz3eu_sub_820FEAB0) {
  if (dbz3::hub::OwnBattle() && g_modo == kNave && g_nave_prueba == 0 && g_fase == kLuchando && !g_nave_armada) {
    std::lock_guard<std::mutex> lk(g_mu);   // el estado de la nave tambien lo toca el hilo que dibuja
    const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
    const int ganador = mgr ? int32_t(REX_LOAD_U32(mgr + 35136)) : -1;   // mgr+35136 = hueco del ganador (0 = jugador)
    float vida_f = 0;
    if (mgr) {
      const uint32_t u = REX_LOAD_U32(mgr + 2096 + 6080);   // vida del jugador (X0 + 6080), como modos.cpp
      std::memcpy(&vida_f, &u, 4);
    }
    int out[6] = {};
    if (ganador == 0 && mgr && nave::Siguiente(out, int(vida_f))) {
      const int escenario = out[2] < 0 ? -out[2] - 1 : EscenarioPorMapa(out[2]);
      const int vida1 = out[3] > 0 ? -out[3] : 0;   // solo la inicial: la barra no se recupera
      if (dbz3::hub::ChainBattleEx(out[0], out[1], escenario, true, out[4], vida1, 1000, 0, 0)) {
        nave::Seguido(int(vida_f));
        dbz3::hub::HoldChain(true);   // espera en negro hasta que la nave lo suelta (CombateNave -> Lanzar)
        g_nave_armada = true;
        REXLOG_INFO("dbz3 misiones: nave: prueba I: encadenado id {} contra id {} (escenario {}, vida {}, rival 1000)",
                    out[0], out[1], escenario, vida1);
      }
    }
  }
  orig(ctx, base);
}

// Prueba: formas de cada personaje (tabla de combate 0x82329CF0 + 372 * ID: +208 cuantas, +212 + 20 * f: capsula,
// ID del modelo, indice, tipo) y escenarios (0x82323760 + 92 * i: nombre, +0x10 mapa) en el registro.
void LogDatos() {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base) return;
  for (int id : {0, 4, 6, 7, 9, 11, 19, 21, 27, 34, 35, 42, 22, 23}) {
    const uint32_t c = dbz3::GA(0x82329CF0u) + 372u * uint32_t(id);
    const uint32_t n = REX_LOAD_U32(c + 208);
    std::string s;
    for (uint32_t f = 0; f < n && f < 8; ++f) {
      const uint32_t p = c + 212 + 20 * f;
      s += " [" + std::to_string(f) + ": capsula " + std::to_string(REX_LOAD_U16(p)) + ", modelo " +
           std::to_string(REX_LOAD_U16(p + 2)) + ":" + std::to_string(REX_LOAD_U16(p + 4)) + ", tipo " +
           std::to_string(REX_LOAD_U8(p + 6)) + "]";
    }
    REXLOG_INFO("dbz3 misiones: prueba: formas del id {} ({}): {}", id, n, s);
  }
  // modelos (char96 0x8234ABB8 + 96 * ID: +4 trajes, +5 modelos por traje, +7 formas, +8 tabla de 12 B: fid)
  for (int id : {35, 84, 78, 79, 11, 64, 4, 90}) {
    const uint32_t c = dbz3::GA(0x8234ABB8u) + 96u * uint32_t(id);
    const uint32_t trajes = REX_LOAD_U8(c + 4), per = REX_LOAD_U8(c + 5), tabla = REX_LOAD_U32(c + 8);
    std::string s;
    for (uint32_t k = 0; tabla && k < trajes * per && k < 24; ++k) s += " " + std::to_string(REX_LOAD_U32(tabla + 12 * k));
    std::string idx;
    for (uint32_t f = 0; f < 8; ++f) idx += " " + std::to_string(REX_LOAD_U8(c + 16 + 8 * f + 2));
    REXLOG_INFO("dbz3 misiones: prueba: modelo {}: {} trajes x {} modelos, formas {} [{}], fids{}", id, trajes, per,
                REX_LOAD_U8(c + 7), idx, s);
  }
  for (uint32_t i = 0; i < 17; ++i) {
    std::string nombre;
    for (uint32_t k = 0; k < 16 && REX_LOAD_U8(dbz3::GA(0x82323760u) + 92 * i + k); ++k) nombre += char(REX_LOAD_U8(dbz3::GA(0x82323760u) + 92 * i + k));
    REXLOG_INFO("dbz3 misiones: prueba: escenario {} '{}' mapa {}", i, nombre, REX_LOAD_U32(dbz3::GA(0x82323760u) + 92 * i + 0x10));
  }
}

// PRUEBA: DBZ3_TEST_MISIONES="<modo>[,auto][,desde=N]" (modo 0 saga B1, 1 tablero, 2 Gotenks gordo, 3 Bu; "menu" =
// solo el menu) abre la pasarela cuando el menu principal lleva 3 s y entra en ese modo. "reset" pone su avance a 0.
const bool g_test = [] {
  const char* t = std::getenv("DBZ3_TEST_MISIONES");
  if (!t) return false;
  g_auto = std::strstr(t, "auto") != nullptr;
  if (std::strncmp(t, "tecnica", 7) == 0) {
    // "tecnica,K1,K2...": combates seguidos de Goku (quieto, 7000 de vida para que dure) contra Super Bu del COM con
    // la absorcion K (traje; 0 = el Bu de siempre) y sus capsulas de serie, IA en Avanzado, 60 s: el registro dice
    // cuando alguien gasta ki de golpe (tecnica) y la vida/ki de los dos cada segundo.
    std::vector<int> ks;
    for (const char* c = std::strchr(t, ','); c; c = std::strchr(c + 1, ',')) ks.push_back(std::atoi(c + 1));
    std::thread([ks] {
      for (int quieto = 0; quieto < 120;) {
        quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      for (size_t i = 0; i < ks.size(); ++i) {
        Reglas r;
        r.nivel_com = 2;
        r.traje_rival = std::max(0, TrajeBu(ks[i]));
        r.traza = true;
        const int caps[] = {3, 123, 124, 125};   // Absorcion (definitiva), Ill Flash, Ill Ball Attack
        std::copy(std::begin(caps), std::end(caps), r.capsulas_rival);
        const bool ok = dbz3::hub::RequestBattleEx(kGoku, kSuperBu, EscenarioPorMapa(kMapaCamara), true, 60, 7000, 0, 0, 0);
        REXLOG_INFO("dbz3 misiones: prueba: tecnica {}: {} (traje {}) {}", i + 1,
                    kAbsorciones[std::clamp(ks[i], 0, kNAbsorciones - 1)].nombre.es, r.traje_rival,
                    ok ? "pedido" : "no aceptado");
        std::atomic<bool> listo{false};
        VigilarCombate(("misiones: tecnica " + std::to_string(i + 1)).c_str(), r, [&listo](const Fin&) { listo = true; });
        while (!listo.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (int quieto = 0; quieto < 120;) {   // de vuelta en el menu principal
          quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
      }
      REXLOG_INFO("dbz3 misiones: prueba: tecnicas acabadas");
    }).detach();
    return true;
  }
  const int modo = std::strncmp(t, "menu", 4) == 0 ? -1 : std::atoi(t);
  int desde = -1;
  if (const char* d = std::strstr(t, "desde=")) desde = std::atoi(d + 6);
  const bool reset = std::strstr(t, "reset") != nullptr;
  std::thread([modo, desde, reset] {
    for (int quieto = 0; quieto < 120;) {
      quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    LeerIW();
    for (int i = 0; i < 200 && (!DatosLeidos() || g_iw_leyendo.load()); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    LogDatos();
    if (modo >= 0 && modo < kModos) {
      if (reset) {
        GuardarProgreso(kClaves[modo], 0);
        if (modo == kTablero) {
          for (const char* k : {"tablero_b2_fase", "tablero_b2_bolas"}) GuardarProgreso(k, 0);
        }
        if (modo == kBuu) GuardarProgreso("buu_absorbidos", 1);
        if (modo == kNave) GuardarProgreso("nave_kili", 0);
        if (modo == kNave) GuardarProgreso("tablero_b2_nave", 1);   // la prueba entra como si ya se hubiera pedido el deseo
      }
      if (desde >= 0) GuardarProgreso(modo == kTablero ? "tablero_b2_fase" : kClaves[modo], desde);
      if (modo == kIW && (reset || desde >= 0)) GuardarProgreso("iw_hecho", std::max(desde, 0));   // superadas hasta N
    }
    const bool ok = Start();
    std::string motivo;
    if (modo >= 0 && modo < kModos) {
      std::lock_guard<std::mutex> lk(g_mu);
      const bool listo = Listo(modo, &motivo);
      REXLOG_INFO("dbz3 misiones: prueba: modo {} listo {} ({})", kClaves[modo], listo, motivo);
      g_cursor = modo;
      if (ok && listo) Entrar(modo);
    } else {   // solo el menu: lo que esta disponible y por que no (WL-20: falta un mod)
      std::lock_guard<std::mutex> lk(g_mu);
      for (int m = 0; m < kModos; ++m) {
        const bool listo = Listo(m, &motivo);
        REXLOG_INFO("dbz3 misiones: prueba: modo {} listo {} ({})", kClaves[m], listo, motivo);
      }
    }
    REXLOG_INFO("dbz3 misiones: prueba: {}", ok ? "abierta" : "no abre");
  }).detach();
  return true;
}();

}  // namespace
}  // namespace dbz3::pasarela::misiones
