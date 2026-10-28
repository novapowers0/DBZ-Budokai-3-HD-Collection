// dbz3 - Modos nativos: Torre, Supervivencia y Kumite (combates seguidos con rivales elegidos), torneo local
// con bots (lista propia de participantes en el Torneo Mundial), ranking Elo y records guardados.
//
// Retos: cada combate se pide con dbz3::hub::RequestBattle (escena 25 con la CFG 0x8245EFC0, hub.cpp). La CFG
// se completa justo antes de que el juego la use (espera de la escena 24, sub_821D7400: traje y vida de los
// dos lados) y el resultado se lee al acabar (sub_820FEBA8, antes de que el juego suelte el combate: ganador
// mgr+35136, vida del jugador X+6080). Vida en el registro de 80 B (sub_82109638, leido en el codigo):
// rec+36 = vida con la que empieza, rec+40 = maxima (se usa la mayor de las dos).
// Tras ganar no sale el menu del final (se elige solo "Reselect", sub_820FF750) y se pasa al combate siguiente;
// tras perder sale (Battle Again? = reintentar). Salir desde la pausa guarda el reto: se sigue en ese combate.
// Entre combates el juego vuelve al menu principal: encima, una pantalla propia con el resultado. Entradas
// "torre", "supervivencia", "kumite" y "ranking" del HUB (hub_panel.h, RegisterMode). Docs: docs/14_frentes/modos.md.
// Datos: user_data/dbz3/hub_modos.toml (records por modo y personaje, retos a medias; .bak = la version anterior)
// y retos.csv (historial). Ranking: Elo por personaje desde combates.csv (lo escribe LabTick en tagteam.cpp).
// Torneo: tras el sorteo nativo (sub_821A28A8) los COM pasan a ser los de la lista; un ID sin casilla propia
// en la rueda va en la casilla de su anfitrion y su ID se pone al preparar cada combate (sub_821A5A30).
// US y EU: ganchos, llamadas y direcciones con su pareja EU (src/eu_map.h, tools/eu_map.py).
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "reloj_prueba.h"
#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "ingame/hub_panel.h"
#include "ingame/tutorial.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"
#include "cine.h"
#include "clima.h"
#include "combate.h"
#include "escenarios.h"
#include "gigantes.h"
#include "modos.h"
#include "mods.h"
#include "roster_ext.h"

namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::hub {
bool RequestBattle(int id1, int id2, int stage, bool p2_com);   // hub.cpp
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
bool OwnBattle();
// encadenado (coordinador): pedido durante un combate nuestro, el siguiente sale sin el menu principal; HoldChain
// lo retiene en negro tras el combate (la pantalla entre combates va encima)
bool ChainBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
void NoCapsulesNext();
void HoldChain(bool hold);
void CancelChain();   // tambien en la espera en negro: vuelve al menu principal
}  // namespace dbz3::hub
namespace dbz3::ingame {
int GameScreen();
}
namespace dbz3::modos {

// Texto en el idioma del juego (es, en, fr, de, it; el resto en ingles).
const char* L(const char* es, const char* en, const char* fr, const char* de, const char* it) {
  if (const char* o = dbz3::i18n::Otro(en)) return o;   // pt / ja
  const char* c = dbz3::i18n::LangCode();
  return !std::strcmp(c, "es") ? es : !std::strcmp(c, "fr") ? fr : !std::strcmp(c, "de") ? de
                                      : !std::strcmp(c, "it") ? it : en;
}

namespace {
namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
namespace D = dbz3::ingame::style::mode;   // esquema comun de las pantallas de modo

constexpr dbz3::GA kGame = 0x824BA110, kCfg = 0x8245EFC0, kReqCur = 0x8247A990, kMgrPtr = 0x823D6CF4, kResult = 0x823D6CE0;
constexpr dbz3::GA kHud = 0x82324064;   // marcador del combate (su menu: +4272, +4292)
constexpr dbz3::GA kWt = 0x8245F270, kWtScreen = 0x824681D0, kSlotToId = 0x82020618, kIdToSlot = 0x82020668;
constexpr int kVidaNormal = 3000, kVidaTope = 7000;   // CFG por defecto: 3000; 7000 = rec+48 (tope de barras)
const char* const kNombres[kTipos] = {"torre", "supervivencia", "kumite", "simulacion", "tienda",
                                      "heroe", "torres", "abyss", "jefes", "torneo4", "mision100"};
const char* const kResNombres[] = {"derrota", "victoria", "abandono", "guardado"};

int64_t NowMs() { return dbz3::RelojMs(); }

int TipoDe(const std::string& s) {
  for (int k = 0; k < kTipos; ++k) {
    if (s == kNombres[k]) return k;
  }
  return -1;
}

struct Combate {
  int id = 0, traje = 0, escenario = 0;
  int vida = 0;              // del rival (0 = la normal)
  int tiempo = -1;           // limite en segundos (0 = sin limite; -1 = el del reto)
  int regen = -1;            // vida por segundo que recupera el rival (-1 = la del reto)
  int objetivo = -1;         // aguantar estos segundos = victoria (-1 = el del reto)
  bool clima = false;        // reglas de escenarios.md R1: cielo que se oscurece,
  float gigante = 0;         // rival a escala (0 = normal; 0.5..3)
  bool intro = false;        // e intro de cine de 10 s
  int forma = 0;             // studies 8 (equipo A): forma inicial del rival (RequestBattleEx form2; fusiones nativas)
};
bool Jugable(int id);
// Rival de un reto propio: jugable, o un registro de fusion nativo de B3 (64..104) pedido con su forma.
bool RivalValido(int id, int forma) { return Jugable(id) || (forma > 0 && id >= 64 && id < 105); }
struct Reto {
  std::string nombre, name;  // nombre del reto (catalogo); vacio = el del modo
  int tiempo = 0, regen = 0, objetivo = 0;   // para todos sus combates (cada combate puede cambiarlos)
  int vida = kVidaNormal;    // vida maxima del jugador
  int vida_inicial = 0;      // del primer combate (0 = la maxima)
  bool arrastra = false;     // la vida que le queda pasa al combate siguiente
  int recupera = 0;          // vida que recupera entre combates (si arrastra)
  bool pausa = false;        // entre combates espera a Continue() (pantalla de resultados del HUB)
  int premio = 0;            // Zenie del HUB al completarlo (rpg.cpp, monedero propio)
  bool propio = false;       // de rpg.cpp: su pantalla entre combates, su guardado y sus reglas
  bool reintentar = true;    // tras perder, "Battle Again?" (false: el reto acaba, sin repetir premios)
  std::string palabra;       // del marcador ("Dia", "Piso"...); vacio = la del tipo
  std::vector<Combate> combates;
};

// Escenarios del Duelo (indice de la CFG +340) por los que van rotando los retos por defecto.
constexpr int kEscenarios[] = {0, 1, 2, 3, 4, 5, 6, 7};

Reto PorDefecto(int tipo) {
  Reto r;
  auto add = [&r](std::initializer_list<int> ids, int vida0, int paso) {
    int k = 0;
    for (const int id : ids) {
      r.combates.push_back({id, 0, kEscenarios[size_t(k) % std::size(kEscenarios)], vida0 + paso * k});
      ++k;
    }
  };
  if (tipo == kTorre) {   // 8 pisos: de Saibaman a Kid Buu, cada rival con mas vida
    add({42, 18, 19, 21, 20, 27, 33, 36}, 2000, 300);
  } else if (tipo == kSupervivencia) {   // 10 oleadas: la vida se arrastra, +500 entre oleadas
    r.arrastra = true;
    r.recupera = 500;
    r.premio = 1000;
    add({42, 43, 29, 28, 30, 38, 37, 35, 40, 41}, 1500, 150);
    // modificadores (ficha 09): las oleadas 3, 6 y 9 con 60 s; la 4, 7 y 10 con un rival que se regenera
    for (const int k : {2, 5, 8}) r.combates[size_t(k)].tiempo = 60;
    for (const int k : {3, 6, 9}) r.combates[size_t(k)].regen = 25;
  } else {   // kumite: 5 jefes con mucha vida; la del jugador se arrastra, +1000 entre jefes
    r.arrastra = true;
    r.recupera = 1000;
    add({27, 33, 38, 40, 36}, 5000, 500);
  }
  return r;
}

std::string LeerTexto(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// user_data/dbz3/modos/<tipo>.toml (modos_reto.py nativo); sin el, el reto por defecto.
Reto Cargar(int tipo) {
  Reto r = PorDefecto(tipo);
  const auto path = dbz3::settings::UserDataRoot() / "modos" / (std::string(kNombres[tipo]) + ".toml");
  const std::string text = LeerTexto(path);
  if (text.empty()) return r;
  try {
    const toml::table t = toml::parse(text);
    r.vida = std::clamp(int(t["vida"].value_or(int64_t(r.vida))), 1, kVidaTope);
    r.vida_inicial = int(t["vida_inicial"].value_or(int64_t(0)));
    r.arrastra = t["arrastra_vida"].value_or(r.arrastra);
    r.recupera = int(t["recupera"].value_or(int64_t(r.recupera)));
    r.pausa = t["pausa"].value_or(false);
    r.premio = std::clamp(int(t["premio"].value_or(int64_t(r.premio))), 0, 100000);
    r.nombre = t["nombre"].value_or(std::string());
    r.name = t["name"].value_or(std::string());
    r.tiempo = std::clamp(int(t["tiempo"].value_or(int64_t(0))), 0, 999);
    r.regen = std::clamp(int(t["regen"].value_or(int64_t(0))), 0, 1000);
    r.objetivo = std::clamp(int(t["objetivo_tiempo"].value_or(int64_t(0))), 0, 999);
    std::vector<Combate> cs;
    if (const toml::array* arr = t["combate"].as_array()) {
      for (const toml::node& n : *arr) {
        const toml::table* c = n.as_table();
        if (!c) continue;
        Combate x;
        x.id = int((*c)["rival"].value_or(int64_t(-1)));
        x.traje = int((*c)["traje"].value_or(int64_t(0)));
        x.escenario = int((*c)["escenario"].value_or(int64_t(0)));
        x.vida = std::clamp(int((*c)["vida"].value_or(int64_t(0))), 0, kVidaTope);
        x.tiempo = std::clamp(int((*c)["tiempo"].value_or(int64_t(-1))), -1, 999);
        x.regen = std::clamp(int((*c)["regen"].value_or(int64_t(-1))), -1, 1000);
        x.objetivo = std::clamp(int((*c)["objetivo_tiempo"].value_or(int64_t(-1))), -1, 999);
        x.clima = (*c)["clima"].value_or(t["clima"].value_or(false));
        x.intro = (*c)["intro"].value_or(t["intro"].value_or(false));
        x.gigante = float((*c)["gigante"].value_or(t["gigante"].value_or(0.0)));
        if (x.gigante > 0) x.gigante = std::clamp(x.gigante, 0.5f, 3.0f);
        if (x.id >= 0 && x.id < 64) cs.push_back(x);
      }
    }
    if (!cs.empty()) r.combates = std::move(cs);
    REXLOG_INFO("dbz3 modos: {} desde {} ({} combates)", kNombres[tipo], path.string(), r.combates.size());
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("dbz3 modos: {} no se puede leer ({}): reto por defecto", path.string(), e.description());
  }
  return r;
}

std::mutex g_mu;   // g_reto, g_st, g_traje, g_marcas, g_curso, g_torneo, g_reglas, g_ef
Reto g_reto;
Estado g_st;
int g_traje = 0;
Reglas g_reglas;   // del combate siguiente de un reto propio (se quitan al acabar el reto)
struct Efectos {   // reglas durante la pelea (sub_820FD678)
  uint32_t combate = 0;              // g_lab_combate del combate en curso
  float prev[2] = {-1, -1};          // vida del fotograma anterior
  float ki_prev[2] = {-1, -1};       // ki del fotograma anterior (tecnicas)
  int cuadros = 0;
  bool armadura = false;             // armadura del rival activa (para el marcador)
} g_ef;
std::atomic<uint8_t*> g_base{nullptr};   // memoria del guest (la de los ganchos)
std::atomic<int64_t> g_lanzado_ms{0};    // ultimo combate pedido
std::atomic<bool> g_fin_visible{false};  // pantalla del final del reto (hasta pulsar A)
std::atomic<bool> g_tras_victoria{false};  // el combate pedido sigue a uno ganado (pantalla entre combates)
std::atomic<bool> g_encadenado{false};     // el siguiente combate espera en negro (hub::HoldChain) a Lanzar
std::atomic<bool> g_reintento{false};      // el combate pedido repite uno perdido (pantalla de revancha propia)

uint8_t* Base() {
  if (uint8_t* b = g_base.load()) return b;
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}

// Se puede pelear con el: los de serie (sin los recortados 22-26 y 31) o un ID con mod de roster.
bool Jugable(int id) {
  if (id < 0 || id >= 64) return false;
  if (dbz3::roster::DonorOf(uint32_t(id)) >= 0) return true;
  return id < 44 && (id < 22 || id > 26) && id != 31;
}

const Combate& Actual() { return g_reto.combates[size_t(g_st.ganados)]; }   // con g_mu
int Objetivo() { return Actual().objetivo >= 0 ? Actual().objetivo : g_reto.objetivo; }
int Tiempo() { return Objetivo() > 0 ? Objetivo() : Actual().tiempo >= 0 ? Actual().tiempo : g_reto.tiempo; }
int Regen() { return Actual().regen >= 0 ? Actual().regen : g_reto.regen; }
// Gana el jugador: el lado 1, o con objetivo de tiempo, acabar por tiempo (motivo 3) sin caer.
bool Gana(int winner, int reason) { return winner == 0 || (Objetivo() > 0 && reason == 3); }
// "Tiempo 60 s - Regenera 25/s" del combate actual (con g_mu); vacio sin modificadores.
std::string Modificadores() {
  std::string s;
  if (Objetivo() <= 0 && Tiempo() > 0) {
    s += std::string(L("Tiempo ", "Time ", "Temps ", "Zeit ", "Tempo ")) + std::to_string(Tiempo()) + " s";
  }
  if (Regen() > 0) {
    s += std::string(s.empty() ? "" : "  -  ") + L("Regenera ", "Regenerates ", "Régénère ", "Regeneriert ", "Rigenera ") +
         std::to_string(Regen()) + "/s";
  }
  if (Actual().gigante > 0) {
    s += std::string(s.empty() ? "" : "  -  ") + L("Rival gigante", "Giant rival", "Adversaire géant", "Riesiger Gegner",
                                                   "Avversario gigante");
  }
  if (Actual().clima) {
    s += std::string(s.empty() ? "" : "  -  ") + L("Tormenta", "Storm", "Tempête", "Sturm", "Tempesta");
  }
  return s;
}

float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}

// ---- Datos guardados: user_data/dbz3/hub_modos.toml ----
// El juego solo tiene una partida (ranura 00000001 del perfil): el fichero va junto a ella en UserDataRoot,
// como historias_extra.bin. Escritura atomica (.tmp + rename) y la version anterior queda en .bak.
struct Marca {
  int tipo = 0, id = 0;
  std::string reto;                   // nombre del reto puesto (vacio = el de serie): otro reto, marca nueva
  int mejor = 0, total = 0;           // mas combates ganados seguidos (de total)
  int intentos = 0, completados = 0;
};
struct EnCurso {   // reto a medias (salida desde la pausa o juego cerrado): se sigue desde aqui
  int tipo = 0, id = 0, traje = 0, ganados = 0, vida = 0, total = 0;
};
// ---- Laboratorio: cada combate deja su resumen (y, si se pide, su captura por fotograma) ----
struct Lado {
  int id = -1;
  float dano = 0;                   // dano hecho al otro
  int golpes = 0;                   // veces que le bajo la vida (bajadas separadas por un fotograma sin bajar)
  int combo = 0, combo_mejor = 0;   // golpes seguidos (menos de 1 s entre uno y otro)
  float combo_dano = 0, combo_mejor_dano = 0;
  int ultimo = -1000;               // fotograma del ultimo golpe
  float vida_prev = -1;
  bool bajando = false;
};
// Frame data de cada golpe (como `frame_data.py --medido` con la sonda de combate.cpp): arranque = cuadro de la
// animacion del atacante al contacto (X+3160), dano aplicado, guardia = el defensor en la accion 1 justo antes, y
// ventaja = cuadro en que el defensor vuelve a una accion neutra (0/1/2, X+3244) menos el del atacante.
struct Golpe {
  int lado = 0, id = 0, codigo = 0, arranque = 0, cuadro = 0;
  float dano = 0;
  bool guardia = false;
  int listo_a = -1, listo_v = -1;   // cuadro en que vuelve cada uno a neutra
};
struct Lab {
  uint32_t mgr = 0;
  int cuadros = 0;
  std::array<Lado, 2> lado;
  std::array<int, 2> accion{};       // accion del cuadro anterior
  std::vector<Golpe> golpes;         // los ultimos (como mucho kGolpesLab)
} g_lab;                            // con g_mu
constexpr size_t kGolpesLab = 6;
std::vector<Marca> g_marcas;
std::vector<EnCurso> g_curso;
bool g_datos_leidos = false;
bool g_lab_captura = false;   // laboratorio: captura por fotograma (user_data/dbz3/laboratorio.csv)
// Practica contra el muneco de combate.cpp (dbz3_combate_prueba_muneco = quieto|guardia, el ordenador no decide):
// se pone solo para ese combate y se devuelve el valor de antes al acabar (o al abrir el juego si se cerro a mitad).
int g_muneco = 0;                 // 0 quieto, 1 en guardia
bool g_practica_guardada = false; // en el fichero: practica a medias
std::string g_muneco_antes;
std::atomic<bool> g_practica{false};
constexpr const char* kCvarMuneco = "dbz3_combate_prueba_muneco";

std::filesystem::path DatosPath() { return dbz3::settings::UserDataRoot() / "hub_modos.toml"; }

bool LeerDatos(const std::filesystem::path& p) {
  const std::string text = LeerTexto(p);
  if (text.empty()) return false;
  try {
    const toml::table t = toml::parse(text);
    if (t["version"].value_or(int64_t(0)) != 1) return false;
    std::vector<Marca> m;
    std::vector<EnCurso> c;
    if (const toml::array* a = t["marca"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        Marca r;
        r.tipo = TipoDe((*x)["tipo"].value_or(std::string()));
        r.id = int((*x)["id"].value_or(int64_t(-1)));
        r.mejor = int((*x)["mejor"].value_or(int64_t(0)));
        r.total = int((*x)["total"].value_or(int64_t(0)));
        r.intentos = int((*x)["intentos"].value_or(int64_t(0)));
        r.completados = int((*x)["completados"].value_or(int64_t(0)));
        r.reto = (*x)["reto"].value_or(std::string());
        if (r.tipo >= 0 && r.id >= 0 && r.id < 64) m.push_back(r);
      }
    }
    if (const toml::array* a = t["en_curso"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        EnCurso r;
        r.tipo = TipoDe((*x)["tipo"].value_or(std::string()));
        r.id = int((*x)["id"].value_or(int64_t(-1)));
        r.traje = int((*x)["traje"].value_or(int64_t(0)));
        r.ganados = int((*x)["ganados"].value_or(int64_t(0)));
        r.vida = int((*x)["vida"].value_or(int64_t(0)));
        r.total = int((*x)["total"].value_or(int64_t(0)));
        if (r.tipo >= 0 && r.id >= 0 && r.id < 64) c.push_back(r);
      }
    }
    g_lab_captura = t["laboratorio"].value_or(false);
    g_muneco = std::clamp(int(t["muneco"].value_or(int64_t(0))), 0, 1);
    g_practica_guardada = t["practica"].value_or(false);
    g_muneco_antes = t["muneco_antes"].value_or(std::string());
    if (const toml::table* u = t["ultimo_combate"].as_table()) {   // resumen del ultimo combate (laboratorio)
      g_lab = {};
      g_lab.cuadros = int((*u)["cuadros"].value_or(int64_t(0)));
      for (size_t k = 0; k < 2; ++k) {
        Lado& l = g_lab.lado[k];
        auto num = [&](const char* clave) { return (*u)[clave][k].value_or(0.0); };
        l.id = int(num("id"));
        l.dano = float(num("dano"));
        l.golpes = int(num("golpes"));
        l.combo_mejor = int(num("combo"));
      }
    }
    g_marcas = std::move(m);
    g_curso = std::move(c);
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

void GuardarDatos();
void CargarDatos() {   // con g_mu, una vez
  if (g_datos_leidos) return;
  g_datos_leidos = true;
  const auto p = DatosPath();
  if (LeerDatos(p)) {
    REXLOG_INFO("dbz3 modos: datos: {} records y {} retos a medias ({})", g_marcas.size(), g_curso.size(), p.string());
  } else if (LeerDatos(p.string() + ".bak")) {
    REXLOG_WARN("dbz3 modos: datos: {} sin leer: se usa la copia de seguridad", p.string());
  }
  if (g_practica_guardada && !g_practica.load()) {
    rex::cvar::SetFlagByName(kCvarMuneco, g_muneco_antes);
    g_practica_guardada = false;
    REXLOG_WARN("dbz3 modos: laboratorio: la practica anterior no acabo: muneco devuelto a '{}'", g_muneco_antes);
    GuardarDatos();
  }
}

void GuardarDatos() {   // con g_mu
  std::ostringstream o;
  o << "# Records y retos a medias de los modos del HUB (src/modos.cpp). No editar con el juego abierto.\n"
       "version = 1\n";
  o << "laboratorio = " << (g_lab_captura ? "true" : "false") << "\nmuneco = " << g_muneco << "\n";
  if (g_practica.load()) o << "practica = true\nmuneco_antes = " << toml::value<std::string>(g_muneco_antes) << "\n";
  if (g_lab.cuadros > 0) {
    const auto& l = g_lab.lado;
    o << "\n[ultimo_combate]\ncuadros = " << g_lab.cuadros << "\nid = [" << l[0].id << ", " << l[1].id
      << "]\ndano = [" << int(l[0].dano) << ", " << int(l[1].dano) << "]\ngolpes = [" << l[0].golpes << ", "
      << l[1].golpes << "]\ncombo = [" << l[0].combo_mejor << ", " << l[1].combo_mejor << "]\n";
  }
  for (const Marca& m : g_marcas) {
    o << "\n[[marca]]\ntipo = \"" << kNombres[m.tipo] << "\"\nid = " << m.id << "\nmejor = " << m.mejor
      << "\ntotal = " << m.total << "\nintentos = " << m.intentos << "\ncompletados = " << m.completados
      << "\nreto = " << toml::value<std::string>(m.reto) << "\n";
  }
  for (const EnCurso& c : g_curso) {
    o << "\n[[en_curso]]\ntipo = \"" << kNombres[c.tipo] << "\"\nid = " << c.id << "\ntraje = " << c.traje
      << "\nganados = " << c.ganados << "\nvida = " << c.vida << "\ntotal = " << c.total << "\n";
  }
  std::error_code ec;
  const auto p = DatosPath();
  std::filesystem::create_directories(p.parent_path(), ec);
  const std::filesystem::path tmp = p.string() + ".tmp", bak = p.string() + ".bak";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << o.str();
    if (!f.flush()) {
      REXLOG_WARN("dbz3 modos: datos: no se puede escribir {}", tmp.string());
      return;
    }
  }
  if (std::filesystem::exists(p, ec)) std::filesystem::copy_file(p, bak, std::filesystem::copy_options::overwrite_existing, ec);
  std::filesystem::rename(tmp, p, ec);
  if (ec) REXLOG_WARN("dbz3 modos: datos: no se puede guardar {} ({})", p.string(), ec.message());
}

// Otro reto puesto (otro nombre u otra lista de combates): la marca de ese modo y personaje empieza de nuevo.
void NuevaMarca(Marca& m) {   // con g_mu
  const std::string reto = g_reto.nombre.empty() ? g_reto.name : g_reto.nombre;
  if (m.total == g_st.total && m.reto == reto) return;
  m.mejor = m.completados = 0;
  m.total = g_st.total;
  m.reto = reto;
}

Marca& MarcaDe(int tipo, int id) {   // con g_mu
  for (Marca& m : g_marcas) {
    if (m.tipo == tipo && m.id == id) return m;
  }
  g_marcas.push_back({tipo, id});
  return g_marcas.back();
}
EnCurso* CursoDe(int tipo) {   // con g_mu
  for (EnCurso& c : g_curso) {
    if (c.tipo == tipo) return &c;
  }
  return nullptr;
}
void QuitarCurso(int tipo) {
  std::erase_if(g_curso, [tipo](const EnCurso& c) { return c.tipo == tipo; });
}
void AnotarCurso() {   // con g_mu: el reto en curso, tal como empieza su combate siguiente
  EnCurso* c = CursoDe(g_st.tipo);
  if (!c) {
    g_curso.push_back({g_st.tipo});
    c = &g_curso.back();
  }
  *c = {g_st.tipo, g_st.jugador, g_traje, g_st.ganados, g_st.vida, g_st.total};
}
void AnotarMarca(bool completado) {   // con g_mu
  Marca& m = MarcaDe(g_st.tipo, g_st.jugador);
  NuevaMarca(m);
  m.mejor = std::max(m.mejor, g_st.ganados);
  if (completado) ++m.completados;
}

void AnotarFin() {   // una linea por reto en user_data/dbz3/retos.csv
  std::error_code ec;
  const auto dir = dbz3::settings::UserDataRoot();
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "retos.csv";
  const bool fresh = !std::filesystem::exists(path, ec);
  if (std::FILE* f = std::fopen(path.string().c_str(), "a")) {
    if (fresh) std::fputs("fecha,tipo,jugador,ganados,total,resultado\n", f);
    const std::time_t now = std::time(nullptr);
    char when[32] = {};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(f, "%s,%s,%d,%d,%d,%s\n", when, kNombres[g_st.tipo], g_st.jugador, g_st.ganados, g_st.total,
                 kResNombres[g_st.resultado]);
    std::fclose(f);
  }
}

// Fin del reto (con g_mu). kGuardado: se sigue en el combate g_st.ganados la proxima vez.
void Terminar(int res) {
  if (g_encadenado.exchange(false)) dbz3::hub::CancelChain();   // esperaba en negro: al menu principal
  g_st.fase = kFin;
  g_st.resultado = res;
  g_reglas = {};   // fuera los efectos del reto
  dbz3::clima::Activar(false);
  dbz3::gigantes::Activar(1, 0);
  AnotarMarca(res == kVictoria);
  if (g_reto.propio) {
  } else if (res == kGuardado) {
    AnotarCurso();
  } else {
    QuitarCurso(g_st.tipo);
  }
  if (res == kVictoria && g_reto.premio > 0) dbz3::rpg::Premio(g_reto.premio);
  GuardarDatos();
  AnotarFin();
  g_fin_visible = true;
  REXLOG_INFO("dbz3 modos: {} terminado: {} ({} de {} combates ganados)", kNombres[g_st.tipo], kResNombres[res],
              g_st.ganados, g_st.total);
}

bool Lanzar() {   // con g_mu: pide el combate g_st.ganados
  const Combate& c = g_reto.combates[size_t(g_st.ganados)];
  if (g_encadenado.exchange(false)) {   // ya pedido al acabar el anterior: sale de la espera en negro
    dbz3::hub::HoldChain(false);
  } else if (!dbz3::hub::RequestBattleEx(g_st.jugador, c.id, c.escenario, true, 0, 0, 0, 0, c.forma)) {
    return false;
  }
  g_st.fase = kCombate;
  g_reintento = false;
  g_st.rival = c.id;
  g_st.escenario = c.escenario;
  g_lanzado_ms = NowMs();
  if (!g_reto.propio) AnotarCurso();   // si el juego se cierra a mitad, se sigue en este combate (rpg: el suyo)
  GuardarDatos();
  REXLOG_INFO("dbz3 modos: {} {}/{}: id {} contra id {} (escenario {}, vida {}/{})", kNombres[g_st.tipo],
              g_st.ganados + 1, g_st.total, g_st.jugador, c.id, c.escenario, g_st.vida, g_st.vida_max);
  return true;
}

// Siguiente combate cuando el menu principal lleva ~2,5 s en pantalla (la de resultados encima; el conector
// pide la pantalla 5 activa).
void SiguienteAlMenu() {
  std::thread([] {
    uint8_t* base = Base();
    for (int t = 0, quieto = 0; base && t < 3000 && quieto < 150; ++t) {   // como mucho ~50 s
      quieto = REX_LOAD_U8(kGame) == 5 ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_st.fase == kEntre && !g_reto.pausa && !Lanzar()) Terminar(kGuardado);
  }).detach();
}

// Siguiente combate encadenado (hilo del juego, combate nuestro acabando): se pide ya y espera en negro con la
// pantalla entre combates encima. Clasicos: a los ~2,5 s sale (Lanzar). Retos de rpg.cpp: sale con su Continue (A en
// su pantalla) o vuelve al menu principal con Abort (B). Sin el conector, por el menu principal como antes.
void Encadenar(bool solo) {   // con g_mu
  const Combate& n = g_reto.combates[size_t(g_st.ganados)];
  if (!dbz3::hub::ChainBattleEx(g_st.jugador, n.id, n.escenario, true, 0, 0, 0, 0, n.forma)) {
    if (solo) SiguienteAlMenu();
    return;
  }
  dbz3::hub::HoldChain(true);
  g_encadenado = true;
  if (!solo) return;
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_st.fase == kEntre && g_encadenado.load() && !Lanzar()) Terminar(kGuardado);
  }).detach();
}

// Fin de un combate nuestro (hilo del juego, ya de vuelta hacia el menu principal).
void FinCombate(int raw, int winner, int reason, int hp, int rival_hp) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase != kCombate) return;
  REXLOG_INFO("dbz3 modos: fin del combate {}/{}: gana el lado {} (motivo {}, resultado del juego {}), vida del "
              "jugador {}, del rival {}", g_st.ganados + 1, g_st.total, winner + 1, reason, raw, hp, rival_hp);
  g_st.recibido = int(g_lab.lado[1].dano);   // dano del rival al jugador (resumen del laboratorio)
  if (hp <= 0 || !Gana(winner, reason)) {
    if (winner < 0 && raw == 2) return Terminar(kGuardado);   // salir desde la pausa: guardado
    // revancha con pantalla propia (el menu del final del juego ya no sale): el mismo combate espera en negro
    // a A (Continue) o B (Abort). Los retos de rpg.cpp acaban al perder, como antes sin su revancha.
    if (!g_reto.reintentar || g_reto.propio) return Terminar(kDerrota);
    g_st.fase = kEntre;
    g_st.resultado = kDerrota;
    g_reintento = true;
    g_tras_victoria = false;
    Encadenar(false);
    return;
  }
  ++g_st.ganados;
  g_st.resultado = kVictoria;
  if (g_reto.arrastra) g_st.vida = std::clamp(hp + g_reto.recupera, 1, g_st.vida_max);
  if (g_st.ganados >= g_st.total) return Terminar(kVictoria);
  if (raw == 2) return Terminar(kGuardado);     // "Return to Main Menu" (si llego a salir el menu): guardado
  g_st.fase = kEntre;
  g_tras_victoria = true;
  g_st.rival = g_reto.combates[size_t(g_st.ganados)].id;
  g_st.escenario = g_reto.combates[size_t(g_st.ganados)].escenario;
  g_reglas = {};   // las reglas son de un combate: rpg.cpp pone las del siguiente
  AnotarMarca(false);
  if (!g_reto.propio) AnotarCurso();
  GuardarDatos();
  if (g_reto.propio) Encadenar(false);
  else if (!g_reto.pausa) Encadenar(true);
}

std::FILE* g_lab_csv = nullptr;     // solo el hilo del juego
uint32_t g_lab_combate = 1;         // contador de combates (el gestor del combate se reutiliza)
uint32_t g_lab_visto = 0;

void LabCuadro(uint8_t* base, uint32_t mgr) {   // hilo del juego, cada fotograma de pelea (estado 4)
  const uint32_t x[2] = {mgr + 2096, mgr + 2096 + 8240};
  std::lock_guard<std::mutex> lk(g_mu);
  const int id0 = int(REX_LOAD_U32(x[0] + 7420)), id1 = int(REX_LOAD_U32(x[1] + 7420));
  if (g_lab_visto != g_lab_combate) {   // combate nuevo (un relevo del Tag Team sigue en el mismo)
    g_lab_visto = g_lab_combate;
    CargarDatos();   // antes de empezar el resumen: el fichero trae el del combate anterior
    g_lab = {};
    g_lab.mgr = mgr;
    if (g_lab_csv) std::fclose(g_lab_csv);
    g_lab_csv = nullptr;
    if (g_lab_captura) {
      g_lab_csv = std::fopen((dbz3::settings::UserDataRoot() / "laboratorio.csv").string().c_str(), "w");
      if (g_lab_csv) std::fputs("cuadro,id1,accion1,vida1,x1,y1,z1,id2,accion2,vida2,x2,y2,z2\n", g_lab_csv);
    }
  }
  g_lab.lado[0].id = id0;
  g_lab.lado[1].id = id1;
  const int f = g_lab.cuadros++;
  const int act[2] = {REX_LOAD_U16(x[0] + 3244), REX_LOAD_U16(x[1] + 3244)};
  for (Golpe& g : g_lab.golpes) {   // ventaja: primer cuadro tras el contacto en que cada uno esta en una accion neutra
    if (f <= g.cuadro) continue;
    if (g.listo_a < 0 && act[g.lado] <= 2) g.listo_a = f;
    if (g.listo_v < 0 && act[1 - g.lado] <= 2) g.listo_v = f;
  }
  for (int v = 0; v < 2; ++v) {   // v = el que recibe; a = el que pega
    Lado& victima = g_lab.lado[size_t(v)];
    Lado& a = g_lab.lado[size_t(1 - v)];
    const float hp = LoadF(base, x[v] + 6080);
    const bool baja = victima.vida_prev >= 0 && hp < victima.vida_prev;
    if (baja) {
      const float d = victima.vida_prev - hp;
      a.dano += d;
      if (victima.bajando && !g_lab.golpes.empty() && g_lab.golpes.back().lado == 1 - v) g_lab.golpes.back().dano += d;
      if (!victima.bajando) {   // golpe nuevo: su frame data
        Golpe g;
        g.lado = 1 - v;
        g.id = v == 0 ? id1 : id0;
        g.codigo = int(REX_LOAD_U32(x[1 - v] + 2928) & 0xFFF);
        // cuadro de la animacion al contacto: aqui se ve la vida bajar un cuadro despues que en el golpe
        // (sub_8210ED48, combate.cpp); medido con la patada de Goku 0x210: 12 aqui, 11 alli (y 11 en el #CSK)
        g.arranque = int(LoadF(base, x[1 - v] + 3160)) - 1;
        g.cuadro = f;
        g.dano = d;
        // guardia: en la accion 1 justo antes, o aun bloqueando el golpe anterior de la misma serie (sin volver a neutra)
        const Golpe* prev = g_lab.golpes.empty() ? nullptr : &g_lab.golpes.back();
        g.guardia = g_lab.accion[size_t(v)] == 1 || (prev && prev->lado == g.lado && prev->guardia && prev->listo_v < 0);
        g_lab.golpes.push_back(g);
        if (g_lab.golpes.size() > kGolpesLab) g_lab.golpes.erase(g_lab.golpes.begin());
        ++a.golpes;
        if (f - a.ultimo > 60) {
          a.combo = 0;
          a.combo_dano = 0;
        }
        ++a.combo;
      }
      a.combo_dano += d;
      a.ultimo = f;
      a.combo_mejor = std::max(a.combo_mejor, a.combo);
      a.combo_mejor_dano = std::max(a.combo_mejor_dano, a.combo_dano);
    }
    victima.bajando = baja;
    victima.vida_prev = hp;
  }
  g_lab.accion = {act[0], act[1]};
  if (g_lab_csv) {
    std::fprintf(g_lab_csv, "%d,%d,%u,%.0f,%.2f,%.2f,%.2f,%d,%u,%.0f,%.2f,%.2f,%.2f\n", f, id0, REX_LOAD_U16(x[0] + 3244),
                 LoadF(base, x[0] + 6080), LoadF(base, x[0] + 64), LoadF(base, x[0] + 68), LoadF(base, x[0] + 72), id1,
                 REX_LOAD_U16(x[1] + 3244), LoadF(base, x[1] + 6080), LoadF(base, x[1] + 64), LoadF(base, x[1] + 68),
                 LoadF(base, x[1] + 72));
    if (f % 600 == 0) std::fflush(g_lab_csv);
  }
}
// Reglas del reto propio durante la pelea (hilo del juego, cada fotograma del estado 4, con g_mu): cada bajada de
// vida se escala (dano hecho/recibido, armadura del jefe) y el ki del jugador baja (o sube). Antes de la
// comprobacion del KO del juego: un golpe escalado que no deja a 0 no tumba; uno que tumba (vida 0) no se toca.
void AplicarReglas(uint8_t* base, uint32_t mgr) {
  const Reglas& r = g_reglas;
  const uint32_t x[2] = {mgr + 2096, mgr + 2096 + 8240};
  if (g_ef.combate != g_lab_combate) {
    g_ef = {};
    g_ef.combate = g_lab_combate;
  }
  const int f = g_ef.cuadros++;
  if (f == 0) {   // capsulas con las que pelea el jugador de verdad (registro de seleccion *(X+7412) +52/+54)
    if (const uint32_t sel = REX_LOAD_U32(x[0] + 7412)) {
      const uint32_t n = std::min<uint32_t>(REX_LOAD_U16(sel + 52), 7);
      std::string ids;
      for (uint32_t i = 0; i < n; ++i) ids += (i ? " " : "") + std::to_string(REX_LOAD_U16(sel + 54 + 2 * i));
      REXLOG_INFO("dbz3 modos: en combate: lado 1 con {} capsulas ({}), forma {}", n, ids, REX_LOAD_U32(x[0] + 2980));
    }
  }
  // armadura: siempre o, con ventana, todo el ciclo menos sus 3 ultimos segundos (el hueco castigable)
  g_ef.armadura = r.armadura > 0 && (r.ventana <= 0 || f % (r.ventana * 60) < (r.ventana - 3) * 60);
  for (int v = 0; v < 2; ++v) {
    const float hp = LoadF(base, x[v] + 6080), prev = g_ef.prev[v];
    float out = hp;
    if (prev > 0 && hp > 0 && hp < prev) {
      const int pct = v == 0 ? r.dano_recibido : r.dano_hecho * (100 - (g_ef.armadura ? r.armadura : 0)) / 100;
      if (pct != 100) out = std::max(1.0f, prev - (prev - hp) * float(pct) / 100.0f);
      if (out != hp) {
        StoreF(base, x[v] + 6080, out);
        REXLOG_INFO("dbz3 modos: reglas: golpe {} {:.0f} -> {:.0f} ({}%{})", v == 0 ? "al jugador" : "al rival",
                    prev - hp, prev - out, pct, v == 1 && g_ef.armadura ? ", con armadura" : "");
      } else if (v == 1 && r.armadura > 0) {
        REXLOG_INFO("dbz3 modos: reglas: golpe al rival {:.0f} sin armadura (hueco)", prev - hp);
      }
    }
    g_ef.prev[v] = out;
  }
  // tecnica = gasto de ki de golpe (>= media barra): al log, con el codigo del ataque (pruebas del Modo Heroe)
  for (int k = 0; k < 2; ++k) {
    const float ki = LoadF(base, x[k] + 6100);
    if (g_ef.ki_prev[k] >= 0 && g_ef.ki_prev[k] - ki >= 500.0f) {
      REXLOG_INFO("dbz3 modos: tecnica: lado {} codigo {:03X} ki {:.0f} -> {:.0f} (forma {} -> {})", k + 1,
                  REX_LOAD_U32(x[k] + 2928) & 0xFFF, g_ef.ki_prev[k], ki, REX_LOAD_U32(x[k] + 2980),
                  REX_LOAD_U32(x[k] + 2984));
    }
    g_ef.ki_prev[k] = ki;
  }
  // PRUEBA (DBZ3_TEST_KI_LLENO=1): el lado 1 con el ki lleno (con DBZ3_COMBATE_LADOS=2 lo lleva el ordenador)
  if (static const bool lleno = std::getenv("DBZ3_TEST_KI_LLENO") != nullptr; lleno) {
    const float mx = LoadF(base, x[0] + 6104);
    if (mx > 0 && LoadF(base, x[0] + 6100) < mx) StoreF(base, x[0] + 6100, mx);
    // ... y el rival no baja de la mitad de su vida: el combate dura hasta que el ordenador use una tecnica
    const float hp = LoadF(base, x[1] + 6080), hmx = LoadF(base, x[1] + 6084);
    if (hp > 0 && hp < hmx * 0.5f) StoreF(base, x[1] + 6080, hmx);
  }
  if (r.ki_drena != 0) {
    const float ki = LoadF(base, x[0] + 6100), mx = LoadF(base, x[0] + 6104);
    if (mx > 0) StoreF(base, x[0] + 6100, std::clamp(ki - mx * float(r.ki_drena) / 6000.0f, 0.0f, mx));   // % del tope/s
    if (f % 300 == 0) REXLOG_INFO("dbz3 modos: reglas: ki del jugador {:.2f} / {:.2f} ({}%/s)", ki, mx, -r.ki_drena);
  }
}

// Practica del laboratorio: el luchador del Duelo contra el muneco (quieto o en guardia), sin tiempo, en el prado.
// Al acabar (salir desde la pausa o tras el menu final) se vuelve a la pantalla del laboratorio.
bool EmpezarPractica(int jugador, int rival) {   // con g_mu
  dbz3::hub::NoCapsulesNext();   // sin capsulas: las medidas del laboratorio son las del golpe sin modificadores
  if (!dbz3::hub::RequestBattleEx(jugador, rival, 5, true, -1, 0, 0, 0, 0)) return false;
  g_muneco_antes = rex::cvar::GetFlagByName(kCvarMuneco);
  rex::cvar::SetFlagByName(kCvarMuneco, g_muneco ? "guardia" : "quieto");
  g_practica = true;
  GuardarDatos();
  REXLOG_INFO("dbz3 modos: laboratorio: practica id {} contra el muneco id {} ({})", jugador, rival,
              g_muneco ? "guardia" : "quieto");
  return true;
}
void FinPractica() {   // hilo del juego, fin de un combate nuestro
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_practica.exchange(false)) return;
  rex::cvar::SetFlagByName(kCvarMuneco, g_muneco_antes);
  GuardarDatos();
  REXLOG_INFO("dbz3 modos: laboratorio: fin de la practica ({} golpes medidos)", g_lab.golpes.size());
}

void LabFinCombate() {   // hilo del juego: el combate acabo (el siguiente empieza de cero)
  std::lock_guard<std::mutex> lk(g_mu);
  ++g_lab_combate;
  if (g_lab_csv) std::fclose(g_lab_csv);
  g_lab_csv = nullptr;
  if (g_lab.cuadros > 0) GuardarDatos();   // el laboratorio lo ensena tambien tras reabrir el juego
}

// ---- Torneo con bots ----
std::vector<int> g_torneo;                        // IDs de los COM (vacio = sorteo nativo); con g_mu
std::array<int, 32> g_t_cell{};                   // por plaza: casilla puesta
std::array<int, 32> g_t_id = [] {                 // e ID que juega (-1 = el de la casilla)
  std::array<int, 32> a;
  a.fill(-1);
  return a;
}();

// Casilla de la rueda de ese ID: la suya, la nueva de un mod de roster (la de su anfitrion) o -1 (sin casilla:
// el cuadro ensena la del sorteo y el combate se juega con su ID).
int SlotOf(uint8_t* base, int id) {
  if (const int host = dbz3::roster::HostSlotOf(uint32_t(id)); host >= 0) return host;
  if (id >= 44) return -1;
  const int slot = int16_t(REX_LOAD_U16(kIdToSlot + 2 * uint32_t(id)));
  return slot >= 0 && slot < 40 && REX_LOAD_U16(kSlotToId + 2 * uint32_t(slot)) == uint32_t(id) ? slot : -1;
}

}  // namespace

bool Start(int tipo, int jugador, int traje) {
  if (tipo < kTorre || tipo > kKumite) return false;
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase == kCombate) return false;
  CargarDatos();
  g_reto = Cargar(tipo);
  std::erase_if(g_reto.combates, [](const Combate& c) {
    if (Jugable(c.id)) return false;
    REXLOG_WARN("dbz3 modos: rival {} sin personaje (falta su mod de roster): fuera del reto", c.id);
    return true;
  });
  const EnCurso* sigue = CursoDe(tipo);   // reto a medias: con su personaje, su combate y su vida
  if (sigue && (!Jugable(sigue->id) || sigue->ganados >= int(g_reto.combates.size()))) {
    QuitarCurso(tipo);
    sigue = nullptr;
  }
  if (sigue) {
    jugador = sigue->id;
    traje = sigue->traje;
  }
  if (g_reto.combates.empty() || !Jugable(jugador)) return false;
  g_st = Estado{};
  g_st.tipo = tipo;
  g_st.jugador = jugador;
  g_st.total = int(g_reto.combates.size());
  g_st.vida_max = g_reto.vida;
  g_st.vida = g_reto.vida_inicial > 0 ? std::min(g_reto.vida_inicial, g_reto.vida) : g_reto.vida;
  if (sigue) {
    g_st.ganados = sigue->ganados;
    if (sigue->vida > 0) g_st.vida = std::clamp(sigue->vida, 1, g_st.vida_max);
    REXLOG_INFO("dbz3 modos: {}: se sigue el reto guardado (combate {}, vida {})", kNombres[tipo], g_st.ganados + 1,
                g_st.vida);
  } else {
    Marca& m = MarcaDe(tipo, jugador);
    NuevaMarca(m);
    ++m.intentos;
  }
  g_traje = traje;
  g_fin_visible = false;
  g_tras_victoria = false;
  if (Lanzar()) return true;
  g_st.fase = kNada;
  return false;
}

// Reto de rpg.cpp (tipo 3-9): sus combates desde el `ganados` (lo guarda rpg.cpp), siempre con pausa entre ellos
// (rpg.cpp dibuja su pantalla y llama a Continue o Abort). Hilo del juego o de la interfaz, menu principal.
bool StartPropio(int tipo, const std::string& nombre, const std::string& palabra, int jugador, int traje, int vida,
                 int vida_max, const std::vector<Pelea>& peleas, int ganados, bool reintentar, int premio, bool pausado) {
  if (tipo <= kKumite || tipo >= kTipos || !Jugable(jugador)) return false;
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase == kCombate || g_st.fase == kEntre) return false;
  CargarDatos();
  Reto r;
  r.nombre = r.name = nombre;
  r.palabra = palabra;
  r.propio = r.pausa = r.arrastra = true;
  r.reintentar = reintentar;
  r.premio = premio;
  r.vida = std::clamp(vida_max, 1, kVidaTope);
  for (const Pelea& p : peleas) {
    if (!RivalValido(p.rival, p.forma)) continue;
    r.combates.push_back({p.rival, p.traje, p.escenario, std::clamp(p.vida, 0, kVidaTope), p.tiempo, p.regen,
                          p.objetivo, p.clima, p.gigante > 0 ? std::clamp(p.gigante, 0.5f, 3.0f) : 0.0f, p.intro,
                          std::clamp(p.forma, 0, 7)});
  }
  if (r.combates.empty() || ganados < 0 || ganados >= int(r.combates.size())) return false;
  g_reto = std::move(r);
  g_st = Estado{};
  g_st.tipo = tipo;
  g_st.jugador = jugador;
  g_st.total = int(g_reto.combates.size());
  g_st.ganados = ganados;
  g_st.vida_max = g_reto.vida;
  g_st.vida = std::clamp(vida, 1, g_reto.vida);
  g_st.propio = 1;
  if (ganados == 0) {
    Marca& m = MarcaDe(tipo, jugador);
    NuevaMarca(m);
    ++m.intentos;
  }
  g_traje = traje;
  g_fin_visible = false;
  g_tras_victoria = false;
  if (pausado) {   // partida reanudada: entre combates (Continue la sigue, Abort la deja guardada)
    g_st.fase = kEntre;
    g_st.rival = g_reto.combates[size_t(ganados)].id;
    g_st.escenario = g_reto.combates[size_t(ganados)].escenario;
    REXLOG_INFO("dbz3 modos: {}: partida reanudada entre combates ({} de {}, vida {}/{})", kNombres[tipo], ganados,
                g_st.total, g_st.vida, g_st.vida_max);
    return true;
  }
  if (Lanzar()) return true;
  g_st.fase = kNada;
  return false;
}

// Entre combates de un reto propio: vida del siguiente (tienda, descanso, cartas) y sus reglas.
void SetVida(int vida, int vida_max) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_st.vida_max = std::clamp(vida_max, 1, kVidaTope);
  g_st.vida = std::clamp(vida, 1, g_st.vida_max);
}
void SetReglas(const Reglas& r) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_reglas = r;
}
void SetJugador(int id) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase != kEntre || id == g_st.jugador || !Jugable(id)) return;
  // el siguiente ya iba pedido (Encadenar) con el luchador de antes: fuera; Continue lo pide de nuevo (RequestBattle)
  if (g_encadenado.exchange(false)) dbz3::hub::CancelChain();
  REXLOG_INFO("dbz3 modos: {}: el combate siguiente con el id {} (antes {})", kNombres[g_st.tipo], id, g_st.jugador);
  g_st.jugador = id;
}
// La pantalla del final del reto ya se vio (rpg.cpp): el HUB vuelve.
void CerrarFin() { g_fin_visible = false; }
// Rival del combate siguiente de un reto propio (entre combates).
void SetSiguiente(const Pelea& p) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase != kEntre || !g_reto.propio || !RivalValido(p.rival, p.forma)) return;
  g_reto.combates[size_t(g_st.ganados)] = {p.rival, p.traje, p.escenario, std::clamp(p.vida, 0, kVidaTope), p.tiempo,
                                           p.regen, p.objetivo, p.clima, p.gigante, p.intro, std::clamp(p.forma, 0, 7)};
  g_st.rival = p.rival;
}
bool ArmaduraActiva() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_ef.armadura;
}

// Siguiente combate (con `pausa = true`, tras la pantalla de resultados del HUB).
bool Continue() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_st.fase == kEntre && Lanzar();
}

// Deja el reto (sin guardarlo para seguir); un combate en curso acaba normal.
void Abort() {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_st.fase == kCombate || g_st.fase == kEntre) Terminar(kAbandono);
}

Estado Get() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_st;
}

// Participantes COM del proximo Torneo Mundial (IDs 0-63; n = 0 vuelve al sorteo nativo).
void SetTorneo(const int* ids, int n) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_torneo.assign(ids, ids + std::max(n, 0));
}

// Ranking Elo por personaje (K = 32, todos empiezan en 1000, en el orden del archivo; espejos fuera).
// Mismo calculo que tools/ranking.py. Devuelve cuantos puestos escribe (de mas a menos Elo).
int Ranking(Puesto* out, int max) {
  std::istringstream in(LeerTexto(dbz3::settings::UserDataRoot() / "combates.csv"));
  std::map<int, double> elo;
  std::map<int, Puesto> p;
  std::string line;
  while (std::getline(in, line)) {
    int a = -1, b = -1, w = -1, mot = 0, com = 0;   // S1-F10 (#42): 6a columna com (lineas viejas: 0 = los dos cuentan)
    if (std::sscanf(line.c_str(), "%*[^,],%d,%d,%d,%d,%d", &a, &b, &w, &mot, &com) < 3 || a == b || a < 0 || b < 0)
      continue;
    const bool cuenta_a = !(com & 1), cuenta_b = !(com & 2);   // el personaje que llevaba el COM no entra
    if (!cuenta_a && !cuenta_b) continue;
    const auto it_a = elo.find(a), it_b = elo.find(b);
    const double ra = it_a != elo.end() ? it_a->second : 1000.0, rb = it_b != elo.end() ? it_b->second : 1000.0;
    const double sa = w == 1 ? 1.0 : w == 2 ? 0.0 : 0.5;
    const double ea = 1.0 / (1.0 + std::pow(10.0, (rb - ra) / 400.0));
    if (cuenta_a) {
      elo[a] = ra + 32.0 * (sa - ea);
      Puesto& pa = p[a];
      pa.id = a;
      (w == 1 ? pa.ganados : w == 2 ? pa.perdidos : pa.empates)++;
    }
    if (cuenta_b) {
      elo[b] = rb - 32.0 * (sa - ea);
      Puesto& pb = p[b];
      pb.id = b;
      (w == 2 ? pb.ganados : w == 1 ? pb.perdidos : pb.empates)++;
    }
  }
  std::vector<Puesto> v;
  for (auto& [id, q] : p) {
    q.elo = int(std::lround(elo[id]));
    v.push_back(q);
  }
  std::stable_sort(v.begin(), v.end(), [](const Puesto& x, const Puesto& y) { return x.elo > y.elo; });
  const int n = std::min(int(v.size()), std::max(max, 0));
  std::copy_n(v.begin(), n, out);
  return n;
}

// Perfil del jugador (ficha 01, ranking local): su Elo (el del lado 1 de cada combate de combates.csv contra el Elo
// del personaje rival, K = 32), el pico, victorias, derrotas, su luchador mas usado y su tramo (0-4: Guerrero Z,
// Super Saiyan, Kaio, Kaio-shin, Shenron; cada 100 de Elo desde 1000). Escribe user_data/dbz3/online_profile.toml
// (escritura segura) para que el panel Online ensene el rango en el reto. Devuelve false sin combates.
struct Perfil {
  int elo = 1000, pico = 1000, ganados = 0, perdidos = 0, empates = 0, favorito = -1, tramo = 0;
};
int TramoDe(int elo) { return std::clamp((elo - 900) / 100, 0, 4); }
const char* NombreTramo(int tramo) {
  static const char* const k[5][5] = {
      {"Guerrero Z", "Z Fighter", "Guerrier Z", "Z-Kämpfer", "Guerriero Z"},
      {"Super Saiyan", "Super Saiyan", "Super Saiyan", "Super-Saiyajin", "Super Saiyan"},
      {"Kaio", "King Kai", "Maître Kaïo", "Meister Kaio", "Re Kaioh"},
      {"Kaio-shin", "Supreme Kai", "Kaïo Shin", "Kaioshin", "Kaioshin"},
      {"Shenron", "Shenron", "Shenron", "Shenlong", "Shenron"}};
  const auto& t = k[std::clamp(tramo, 0, 4)];
  return L(t[0], t[1], t[2], t[3], t[4]);
}
bool CalcularPerfil(Perfil* out) {
  std::istringstream in(LeerTexto(dbz3::settings::UserDataRoot() / "combates.csv"));
  std::map<int, double> elo;   // de cada personaje (como Ranking)
  std::map<int, int> usos;
  double yo = 1000.0;
  Perfil p;
  std::string line;
  while (std::getline(in, line)) {
    int a = -1, b = -1, w = -1, mot = 0, com = 0;   // S1-F10 (#42): COM contra COM (com & 1) no es un combate tuyo
    if (std::sscanf(line.c_str(), "%*[^,],%d,%d,%d,%d,%d", &a, &b, &w, &mot, &com) < 3 || a < 0 || b < 0 || (com & 1))
      continue;
    const double rb = elo.try_emplace(b, 1000.0).first->second;
    const double s = w == 1 ? 1.0 : w == 2 ? 0.0 : 0.5;
    yo += 32.0 * (s - 1.0 / (1.0 + std::pow(10.0, (rb - yo) / 400.0)));
    if (a != b) {
      const double ra = elo.try_emplace(a, 1000.0).first->second;
      const double ea = 1.0 / (1.0 + std::pow(10.0, (rb - ra) / 400.0));
      elo[a] = ra + 32.0 * (s - ea);
      elo[b] = rb - 32.0 * (s - ea);
    }
    (w == 1 ? p.ganados : w == 2 ? p.perdidos : p.empates)++;
    ++usos[a];
    p.pico = std::max(p.pico, int(std::lround(yo)));
  }
  if (usos.empty()) return false;
  p.elo = int(std::lround(yo));
  p.tramo = TramoDe(p.elo);
  p.favorito = std::max_element(usos.begin(), usos.end(), [](auto& x, auto& y) { return x.second < y.second; })->first;
  *out = p;
  std::ostringstream o;   // user_data/dbz3/online_profile.toml (lo puede leer el panel Online)
  o << "# Perfil local (src/modos.cpp, ficha 01). Se rehace desde combates.csv: no editar.\nversion = 1\nrating = "
    << p.elo << "\npico = " << p.pico << "\nwins = " << p.ganados << "\nlosses = " << p.perdidos
    << "\ndraws = " << p.empates << "\ntramo = " << p.tramo << "\nfavorito = " << p.favorito << "\n";
  std::error_code ec;
  const auto path = dbz3::settings::UserDataRoot() / "online_profile.toml";
  const std::filesystem::path tmp = path.string() + ".tmp", bak = path.string() + ".bak";
  if (LeerTexto(path) != o.str()) {
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      f << o.str();
      if (!f.flush()) return true;
    }
    if (std::filesystem::exists(path, ec)) std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::rename(tmp, path, ec);
  }
  return true;
}
// Para el panel Online ("reto que muestra el rango"): Elo y tramo del jugador (false sin combates).
bool PerfilJugador(int* elo, int* tramo, const char** nombre_tramo) {
  Perfil p;
  if (!CalcularPerfil(&p)) return false;
  *elo = p.elo;
  *tramo = p.tramo;
  if (nombre_tramo) *nombre_tramo = NombreTramo(p.tramo);
  return true;
}

// Record de un modo: el mejor de todos los personajes (false si no hay). Para el HUB.
bool Record(int tipo, int* id, int* mejor, int* total, int* completados) {
  std::lock_guard<std::mutex> lk(g_mu);
  CargarDatos();
  const Marca* best = nullptr;
  for (const Marca& m : g_marcas) {
    if (m.tipo == tipo && (!best || m.completados > best->completados ||
                           (m.completados == best->completados && m.mejor > best->mejor))) {
      best = &m;
    }
  }
  if (!best) return false;
  *id = best->id;
  *mejor = best->mejor;
  *total = best->total;
  *completados = best->completados;
  return true;
}

// Reto a medias de un modo (false si no hay): se sigue en el combate ganados + 1. Para el HUB.
bool Guardado(int tipo, int* id, int* ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  CargarDatos();
  const EnCurso* c = CursoDe(tipo);
  if (!c) return false;
  *id = c->id;
  *ganados = c->ganados;
  return true;
}

namespace {
std::string NombrePj(int id);
}
// Nombre del luchador en el idioma del juego (mods incluidos). Para el HUB.
std::string Nombre(int id) { return NombrePj(id); }

namespace {

// ---- Pantallas propias (resultado entre combates, final del reto, ranking) ----
// Como las del juego: fondo del Duelo, cinta del titulo, marco con letra turquesa, bocadillo y botones
// (piezas de native_ui, sacadas del AFS del jugador). Mientras se ven, el juego no recibe el mando.
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kA = 0x1000, kB = 0x2000;

const char* const kPjEn[44] = {"Goku", "Kid Goku", "Kid Gohan", "Teen Gohan", "Gohan", "Great Saiyaman", "Goten",
                               "Vegeta", "Trunks", "Kid Trunks", "Krillin", "Piccolo", "Tien", "Yamcha", "Hercule",
                               "Videl", "Supreme Kai", "Uub", "Raditz", "Nappa", "Ginyu", "Recoome", nullptr,
                               nullptr, nullptr, nullptr, nullptr, "Frieza", "Android 16", "Android 17", "Android 18",
                               nullptr, "Dr. Gero", "Cell", "Majin Buu", "Super Buu", "Kid Buu", "Dabura", "Cooler",
                               "Bardock", "Broly", "Omega Shenron", "Saibaman", "Cell Jr."};
const char* const kPjEs[44] = {"Goku", "Goku niño", "Gohan niño", "Gohan joven", "Gohan", "Gran Saiyaman", "Goten",
                               "Vegeta", "Trunks", "Trunks niño", "Krilin", "Piccolo", "Ten Shin Han", "Yamcha",
                               "Mr. Satán", "Videl", "Kaio-shin", "Oob", "Raditz", "Nappa", "Ginyu", "Reacum", nullptr,
                               nullptr, nullptr, nullptr, nullptr, "Freezer", "C-16", "C-17", "C-18", nullptr,
                               "Dr. Gero", "Cell", "Majin Buu", "Super Buu", "Buu niño", "Dabra", "Cooler", "Bardock",
                               "Broly", "Omega Shenron", "Saibaman", "Cell Jr."};

// Nombres de los personajes de mods (mods/_roster/roster.toml, `nombre` de cada [[personaje]]): los IDs recortados
// 22-26 y 31 conservan en el juego el nombre del recortado (GULDO...), el del mod es el bueno.
const std::map<int, std::string>& NombresMod() {
  static const std::map<int, std::string> m = [] {
    std::map<int, std::string> r;
    try {
      const std::string text = LeerTexto(dbz3::ModsRoot() / "_roster" / "roster.toml");
      if (text.empty()) return r;
      const toml::table t = toml::parse(text);
      if (const toml::array* a = t["personaje"].as_array()) {
        for (const toml::node& n : *a) {
          if (const toml::table* x = n.as_table()) {
            const int id = int((*x)["id"].value_or(int64_t(-1)));
            const std::string nombre = (*x)["nombre"].value_or(std::string());
            if (id >= 0 && id < 64 && !nombre.empty()) r[id] = nombre;
          }
        }
      }
    } catch (const toml::parse_error&) {
    }
    return r;
  }();
  return m;
}

std::string NombrePj(int id) {
  const bool es = std::strcmp(T::LangCode(), "es") == 0;
  if (dbz3::roster::DonorOf(uint32_t(id)) >= 0) {   // personaje de un mod
    if (const auto it = NombresMod().find(id); it != NombresMod().end()) return it->second;
    if (std::string n = dbz3::roster::InternalName(uint32_t(id)); !n.empty()) return n;
  }
  if (id >= 0 && id < 44 && kPjEn[id]) return es ? kPjEs[id] : kPjEn[id];
  if (std::string n = dbz3::roster::InternalName(uint32_t(id)); !n.empty()) return n;
  return "#" + std::to_string(id);
}
const char* NombreTipo(int tipo) {
  switch (tipo) {
    case kTorre: return L("Torre", "Tower", "Tour", "Turm", "Torre");
    case kSupervivencia: return L("Supervivencia", "Survival", "Survie", "Überleben", "Sopravvivenza");
    case kSimulacion: return L("Simulación", "Simulation", "Simulation", "Simulation", "Simulazione");
    case kTienda: return L("Supervivencia con tienda", "Survival with shop", "Survie avec boutique",
                           "Überleben mit Laden", "Sopravvivenza con negozio");
    case kHeroe: return L("Modo Héroe", "Hero Mode", "Mode Héros", "Heldenmodus", "Modalità Eroe");
    case kTorres: return L("Torres del Tiempo", "Time Towers", "Tours du Temps", "Zeittürme", "Torri del Tempo");
    case kAbyss: return "Abyss";
    case kJefes: return L("Kumite de jefes", "Boss Kumite", "Kumite des boss", "Boss-Kumite", "Kumite dei boss");
    case kTorneo4: return L("Torneo de 4", "4-Fighter Tournament", "Tournoi à 4", "Turnier zu viert", "Torneo a 4");
    case kMision: return L("Misión 100", "Mission 100", "Mission 100", "Mission 100", "Missione 100");
    default: return "Kumite";
  }
}
}  // namespace
const char* NombreModo(int tipo) { return NombreTipo(tipo); }
namespace {
// "Piso 3 de 8" / "Oleada 3 de 10" / "Jefe 3 de 5"
std::string Paso(int tipo, int n, int total) {
  const char* w = tipo == kTorre ? T::T("Piso", "Floor") : tipo == kSupervivencia ? T::T("Oleada", "Wave")
                                                                                 : T::T("Jefe", "Boss");
  return std::string(w) + " " + std::to_string(n) + " " + T::T("de", "of") + " " + std::to_string(total);
}

int Pantalla() {
  uint8_t* base = Base();
  return base ? int(REX_LOAD_U8(kGame)) : -1;
}
bool MenuPrincipal() { return Pantalla() == 5; }
// Nombre del reto puesto (claves nombre/name de su TOML) o el del modo.
std::string NombreReto(int tipo) {
  std::lock_guard<std::mutex> lk(g_mu);
  const bool es = std::strcmp(T::LangCode(), "es") == 0;
  const std::string& n = es || g_reto.name.empty() ? g_reto.nombre : g_reto.name;
  return n.empty() ? NombreTipo(tipo) : n;
}

std::atomic<bool> g_rank_abierto{false};
std::atomic<bool> g_lab_abierto{false};
std::atomic<uint16_t> g_pad{0};
std::atomic<bool> g_pad_pendiente{false};

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

class ModosPanel : public rex::ui::ImGuiDialog {
 public:
  explicit ModosPanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  ~ModosPanel() override { Bloquear(false); }

 protected:
  void OnDraw(ImGuiIO& io) override {
    enum { kNinguna, kEntreP, kFinP, kRankP, kLabP, kRpgP } que = kNinguna;
    Estado st = Get();
    const bool menu = MenuPrincipal();
    // prueba de dibujo (frente R6, tools/pruebas.py r6_pantallas): DBZ3_TEST_R6_PANTALLA=entre|derrota|fin pinta en el
    // menu principal esa pantalla con un reto de ejemplo, sin leer el mando (no cambia nada del reto ni de la partida)
    static const char* const kPrueba = std::getenv("DBZ3_TEST_R6_PANTALLA");
    const bool fingida = kPrueba && menu && st.fase == kNada && !g_rank_abierto.load() && !g_lab_abierto.load();
    if (fingida) {
      const bool fin = std::strcmp(kPrueba, "fin") == 0;
      st.tipo = kTorre, st.ganados = fin ? 8 : 2, st.total = 8, st.rival = 7, st.escenario = 3, st.jugador = 0;
      st.vida = 3200, st.vida_max = 4000, st.fase = fin ? kFin : kEntre, st.resultado = fin ? kVictoria : kNinguno;
    }
    if (g_rank_abierto.load()) {
      que = kRankP;
    } else if (g_lab_abierto.load() && !g_practica.load()) {   // (durante la practica, el combate)
      que = kLabP;
    } else if ((menu || g_encadenado.load() ||
                (st.propio && st.fase == kCombate && NowMs() - g_lanzado_ms.load() < 1500 && Pantalla() != 25)) &&
               dbz3::rpg::Activa()) {   // selector y pantallas de los retos propios (rpg.cpp), tambien en la espera en negro
      que = kRpgP;
    } else if (st.propio) {
    } else if ((g_fin_visible.load() || fingida) && menu && st.fase == kFin) {
      que = kFinP;
    } else if ((menu || g_encadenado.load()) && st.fase == kEntre) {
      que = kEntreP;
    } else if (st.fase == kCombate && g_tras_victoria.load() && NowMs() - g_lanzado_ms.load() < 1500 &&
               Pantalla() != 25) {
      que = kEntreP;
    }
    const bool tapado = dbz3::GameDimmed() || dbz3::OverlayOpen();
    Bloquear(que != kNinguna && !tapado);
    // el HUD del modo sale y se va con el marcador del juego (no en la presentacion ni en las definitivas)
    if (que == kNinguna && !tapado && st.fase == kCombate && Pantalla() == 25 && dbz3::combate::HudNativoVisible()) {
      const float info = Contador(io, st);
      if (st.propio) dbz3::rpg::Hud(ImGui::GetForegroundDrawList(), S::GameArea(io), info);
    }
    if (que != ultima_) {
      ultima_ = que;
      abierta_ = float(dbz3::RelojT(ImGui::GetTime()));
      esperar_suelta_ = true;
      if (que == kRankP) Refrescar();
    }
    if (que == kNinguna || tapado) return;
    uint16_t pulsado = Pad();
    if (esperar_suelta_ || fingida) {   // el boton que abrio la pantalla no cuenta hasta soltarlo todo
      if (g_pad.load() == 0 && float(dbz3::RelojT(ImGui::GetTime())) - abierta_ > 0.25f) esperar_suelta_ = false;
      pulsado = 0;
    }
    if ((que == kRpgP && dbz3::ingame::tutorial::Mostrar(dbz3::rpg::Heroe() ? "heroe" : "retos")) ||
        dbz3::ingame::tutorial::Abierto())
      pulsado = 0;   // F3: tutorial encima
    // sonidos del menu del juego (hub_menu.cpp, QueueSe: suenan sobre el menu principal): mover 2, aceptar 3, atras 5
    if (const int se = pulsado & kB ? 5 : pulsado & kA ? 3 : pulsado & (kUp | kDown | kLeft | kRight | kLb | kRb) ? 2 : -1;
        se >= 0) {
      dbz3::hub::QueueSe(se);
      static const bool log_se = std::getenv("DBZ3_TEST_MODOS_SE") != nullptr;
      if (log_se) REXLOG_INFO("dbz3 modos: prueba: sonido {} (pantalla {})", se, int(que));
    }
    const S::Area a = S::GameArea(io);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    const float in = std::clamp((t - abierta_) / 0.2f, 0.0f, 1.0f);
    const int v0 = dl->VtxBuffer.Size;
    // fondos propios 16:9 (assets_propios/png/add): Laboratorio, Modo Heroe y, en el resto, el del HUB (sin ellos, el
    // del Duelo). Esquema comun de colocacion: S::mode (game_style.h); el laboratorio va sin anfitrion (tabla ancha).
    // Pack Dragon HUB del autor (assets_propios/png/add/dragon_hub): fondo y anfitrion de cada pantalla; si falta
    // la pieza, el fondo del HUB y el Kaio del Duelo como antes
    const bool heroe = que == kRpgP && dbz3::rpg::Heroe(), m100 = que == kRpgP && dbz3::rpg::Mision100();
    const char* fondo = que == kLabP ? "hub/fondo_lab" : heroe ? "hub/fondo_heroe" : m100 ? "hub/fondo_mision"
                        : que == kRankP ? "hub/fondo_ranking" : que == kEntreP || que == kFinP ? "hub/fondo_retos"
                                                                                               : "hub/fondo";
    if (!S::Img(fondo)) fondo = "hub/fondo";
    S::Backdrop(dl, a, S::Img(fondo) ? fondo : "fondo_duelo");
    // de pie, pies abajo, ~437 px de alto (S::Host): Heroe su anfitrion, Mision 100 Vegeta, Ranking Whis, entre
    // combates y final Goku, el selector de retos Trunks
    const char* host = heroe ? "hub/anfitrion_heroe" : m100 ? "hub/anfitrion_vegeta"
                       : que == kRankP ? "hub/anfitrion_whis" : que == kEntreP || que == kFinP ? "hub/anfitrion_goku"
                                                                                              : "hub/anfitrion_trunks";
    if (!S::Img(host)) host = "anfitrion_tagteam_a";
    if (que != kLabP) S::Host(dl, a, host, D::kHostX, D::kHostY, false);
    S::Sides(dl, a);
    if (que == kEntreP) DibujarEntre(dl, a, st, pulsado);
    else if (que == kRpgP) dbz3::rpg::Dibujar(dl, a, t, pulsado);
    else if (que == kFinP) DibujarFin(dl, a, st, pulsado);
    else if (que == kLabP) DibujarLab(dl, a, pulsado);
    else DibujarRanking(dl, a, t, pulsado);
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {   // entra con un fundido corto, como los menus del juego
      ImU32& c = dl->VtxBuffer[i].col;
      c = (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * in) << 24);
    }
  }

 private:
  void Bloquear(bool si) {
    if (si == bloqueado_) return;
    if (auto* inp = Input()) {
      if (si) inp->AddUIInputBlocker();
      else inp->RemoveUIInputBlocker();
      bloqueado_ = si;
    }
  }

  // Botones recien pulsados del mando 1 (la palanca izquierda cuenta como cruceta).
  uint16_t Pad() {
    if (!g_pad_pendiente.exchange(true)) {
      dbz3::launcher::DeferUi([]() {
        uint16_t b = 0;
        if (auto* inp = Input()) {
          rex::input::X_INPUT_STATE s = {};
          if (inp->GetStateForUI(0, &s) == 0) {
            b = static_cast<uint16_t>(s.gamepad.buttons);
            const int lx = int16_t(s.gamepad.thumb_lx), ly = int16_t(s.gamepad.thumb_ly);
            if (ly > 16000) b |= kUp;
            if (ly < -16000) b |= kDown;
            if (lx < -16000) b |= kLeft;
            if (lx > 16000) b |= kRight;
          }
        }
        g_pad = b;
        g_pad_pendiente = false;
      });
    }
    const uint16_t b = g_pad.load(), pulsado = uint16_t(b & ~ultimo_);
    ultimo_ = b;
    return pulsado;
  }

  // Lineas centradas en el marco del juego, en la columna del esquema comun (la primera, cabecera resaltada).
  static void Marco(ImDrawList* dl, const S::Area& a, float y0, const std::vector<std::string>& lineas, float t) {
    const float paso = 52, y1 = y0 + 2 * D::kPad + paso * float(lineas.size());
    S::Frame(dl, a.At(D::kX0, y0), a.At(D::kX1, y1), a.s);
    D::Stripe(dl, a, D::kX0, y0 + D::kPad + 2, D::kX1, y0 + D::kPad + paso - 2, t);
    for (size_t i = 0; i < lineas.size(); ++i) {
      S::MenuText(dl, a.At(D::kCx, y0 + D::kPad + paso * (float(i) + 0.5f)), (i == 0 ? D::kHead : D::kRow) * a.s,
                  lineas[i].c_str(), i == 0, false, (D::kX1 - D::kX0 - 2 * D::kText) * a.s);
    }
  }

  // Durante el combate, bajo el reloj: "Piso 2 de 8" (letra del menu, sin marco: no tapa el marcador).
  // Marcador del HUB (res/ingame_ui/retos: marcador_<modo> 560x128 con la insignia a la izquierda y la caja del
  // texto en (128,25)-(538,106); cifras doradas num_*): palabra con la letra del menu y "N/M" con las cifras.
  // Bloque del HUD de los modos (S::mode::kHud*): marcador y debajo sus lineas; devuelve la y de la siguiente linea
  // libre (para las reglas del reto propio, rpg::Hud).
  static float Contador(ImGuiIO& io, const Estado& st) {
    const S::Area a = S::GameArea(io);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    bool aguanta = false;
    std::string mods, propia;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      aguanta = g_st.fase == kCombate && Objetivo() > 0;
      if (g_st.fase == kCombate) mods = Modificadores();
      propia = g_reto.palabra;
    }
    // retos de rpg.cpp: su palabra y el marcador del modo parecido (torre: pisos; supervivencia: rondas; kumite)
    static constexpr int kMarcador[kTipos] = {kTorre, kSupervivencia, kKumite, kKumite, kSupervivencia,
                                              kKumite, kTorre,         kTorre,  kKumite, kKumite, kKumite};
    const int base_tipo = kMarcador[std::clamp(st.tipo, 0, kTipos - 1)];
    const char* palabra = st.propio && !propia.empty() ? propia.c_str()
                          : base_tipo == kTorre        ? T::T("Piso", "Floor")
                          : base_tipo == kSupervivencia ? T::T("Oleada", "Wave")
                                                        : T::T("Jefe", "Boss");
    float info = D::kHudInfoY;   // lineas bajo el marcador: "¡Aguanta!" o los modificadores, luego las reglas
    const std::string linea = aguanta ? std::string(T::T("¡Aguanta!", "Survive!")) : mods;
    if (!linea.empty()) {
      S::MenuText(dl, a.At(600, info), 24 * a.s, linea.c_str(), true, false, 600 * a.s);
      info += D::kHudLine;
    }
    const S::Pic m = S::Img((std::string("retos/marcador_") + kNombres[base_tipo]).c_str());
    const std::string cifras = std::to_string(st.ganados + 1) + "/" + std::to_string(st.total);
    // cifras: las del marcador del juego (native_ui "cifras_hud", data_usi 0: 0-9 y "/" blancas con perfil, en naranja
    // como el "2/2" del contador de rivales); sin ellas, las doradas propias retos/num_*
    struct Cifra {
      S::Pic p;
      ImVec4 src;
      float k;   // alto respecto a una cifra (la barra es mas baja)
    };
    std::vector<Cifra> nums;
    const S::Pic hud = S::Img("cifras_hud");
    if (hud) {
      static constexpr float kX[10][2] = {{3, 30},   {46, 62},  {76, 104}, {112, 139}, {148, 175},
                                          {2, 29},   {40, 67},  {77, 102}, {112, 140}, {148, 175}};
      for (const char c : cifras) {
        const int d = c - '0';
        const float y = d < 5 ? 4 : 40;
        nums.push_back(c == '/' ? Cifra{hud, ImVec4(124, 76, 139, 94), 0.6f}
                                : Cifra{hud, ImVec4(kX[d][0], y, kX[d][1], y + 30), 1.0f});
      }
    } else {
      for (const char c : cifras) {
        const S::Pic n = S::Img(c == '/' ? "retos/num_barra" : (std::string("retos/num_") + c).c_str());
        if (!n) {
          nums.clear();
          break;
        }
        nums.push_back({n, ImVec4(0, 0, n.w, n.h), 1.0f});
      }
    }
    if (!m || nums.empty()) {   // sin las piezas: solo la letra del menu
      const std::string txt = std::string(palabra) + " " + std::to_string(st.ganados + 1) + "/" + std::to_string(st.total);
      S::MenuText(dl, a.At(600, D::kHudY + D::kHudH * 0.5f), 30 * a.s, txt.c_str(), true, false, 360 * a.s);
      return info;
    }
    const float h = D::kHudH, k = h / 128.0f, w = 560 * k, x0 = 600 - w * 0.5f, y0 = D::kHudY;
    S::DrawPic(dl, m, a.At(x0, y0), a.At(x0 + w, y0 + h));
    const float bx0 = x0 + 128 * k, bx1 = x0 + 538 * k, cy = y0 + 65.5f * k, dh = 81 * k * 0.62f;
    const float paso = hud ? 1.0f : -2.0f;   // las del juego van sueltas; las propias, montadas
    const ImU32 tinte = hud ? IM_COL32(255, 160, 40, 255) : IM_COL32_WHITE;
    auto ancho = [&](const Cifra& n) { return (n.src.z - n.src.x) * dh * n.k / (n.src.w - n.src.y); };
    float dw = 0;
    for (const Cifra& n : nums) dw += ancho(n) + paso;
    float x = bx1 - 8 - dw;
    for (const Cifra& n : nums) {   // alineadas por abajo
      const float nw = ancho(n), gh = dh * n.k;
      S::DrawPic(dl, n.p, a.At(x, cy + dh * 0.5f - gh), a.At(x + nw, cy + dh * 0.5f), n.src, tinte);
      x += nw + paso;
    }
    S::MenuText(dl, a.At((bx0 + bx1 - 8 - dw) * 0.5f, cy), 32 * h / 70 * a.s, palabra, true, false,
                (bx1 - 16 - dw - bx0) * a.s);
    return info;
  }

  void DibujarEntre(ImDrawList* dl, const S::Area& a, const Estado& st, uint16_t pulsado) {
    S::Banner(dl, a, NombreReto(st.tipo).c_str());
    static const bool kPruebaDerrota = [] {   // prueba de dibujo (DBZ3_TEST_R6_PANTALLA=derrota)
      const char* e = std::getenv("DBZ3_TEST_R6_PANTALLA");
      return e && std::strcmp(e, "derrota") == 0;
    }();
    if ((g_reintento.load() || kPruebaDerrota) && st.fase == kEntre) {   // combate perdido: revancha o dejar el reto
      if (pulsado & kA) Continue();
      else if (pulsado & kB) Abort();
      Marco(dl, a, D::kTop, {T::T("Derrota", "Defeat"),
                         Paso(st.tipo, st.ganados + 1, st.total) + "  -  " + NombrePj(st.rival) + "  (" +
                             dbz3::escenarios::Nombre(st.escenario) + ")"},
            float(dbz3::RelojT(ImGui::GetTime())));
      D::Bubble(dl, a, T::T("¡No te rindas! Puedes repetir este combate o dejar el reto.",
                            "Don't give up! You can retry this fight or leave the challenge."));
      D::Help(dl, a, {{"A", T::T("Reintentar", "Retry")}, {"B", T::T("Dejar el reto", "Quit")}});
      return;
    }
    bool arrastra = false;
    std::string mods;   // modificadores del combate siguiente (ficha 09)
    {
      std::lock_guard<std::mutex> lk(g_mu);
      arrastra = g_reto.arrastra;
      if (g_st.fase == kEntre) mods = Modificadores();
    }
    std::vector<std::string> l = {Paso(st.tipo, st.ganados, st.total) + T::T(": ¡superado!", ": cleared!"),
                                  std::string(T::T("Siguiente: ", "Next: ")) + NombrePj(st.rival) + "  (" +
                                      dbz3::escenarios::Nombre(st.escenario) + ")"};
    if (arrastra) l.push_back(std::string(T::T("Vida ", "Health ")) + std::to_string(st.vida) + " / " +
                                     std::to_string(st.vida_max));
    if (!mods.empty()) l.push_back(mods);
    Marco(dl, a, D::kTop, l, float(dbz3::RelojT(ImGui::GetTime())));
    const std::string bocadillo = Paso(st.tipo, st.ganados + 1, st.total) + ". " +
                                  T::T("¡Prepárate para el siguiente combate!", "Get ready for the next fight!");
    D::Bubble(dl, a, bocadillo.c_str());
  }

  void DibujarFin(ImDrawList* dl, const S::Area& a, const Estado& st, uint16_t pulsado) {
    if (pulsado & (kA | kB)) g_fin_visible = false;   // vuelve el HUB
    S::Banner(dl, a, NombreReto(st.tipo).c_str());
    std::string titulo, dice;
    if (st.resultado == kVictoria) {
      titulo = st.tipo == kTorre ? T::T("¡Torre completada!", "Tower cleared!")
               : st.tipo == kSupervivencia ? T::T("¡Has sobrevivido!", "You survived!")
                                           : T::T("¡Kumite superado!", "Kumite cleared!");
      dice = T::T("¡Increíble! Nadie ha llegado tan lejos.", "Amazing! Nobody has made it this far.");
    } else if (st.resultado == kGuardado) {
      titulo = T::T("Reto guardado", "Challenge saved");
      dice = Paso(st.tipo, st.ganados + 1, st.total) + ": " +
             T::T("la próxima vez seguirás desde aquí.", "next time you will continue from here.");
    } else {
      titulo = st.resultado == kAbandono ? T::T("Reto abandonado", "Challenge abandoned")
                                         : T::T("Derrota", "Defeat");
      dice = T::T("¡Vuelve a intentarlo! Cada combate cuenta para tu récord.",
                  "Try again! Every fight counts toward your record.");
    }
    int id = 0, mejor = 0, total = 0, comp = 0;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      const Marca& m = MarcaDe(st.tipo, st.jugador);
      mejor = m.mejor;
      total = m.total;
      comp = m.completados;
      id = st.jugador;
    }
    int premio = 0;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      premio = st.resultado == kVictoria ? g_reto.premio : 0;
    }
    std::vector<std::string> l = {titulo, NombrePj(id) + ": " + std::to_string(st.ganados) + " / " +
                                              std::to_string(st.total),
                                  std::string(T::T("Récord: ", "Record: ")) + std::to_string(mejor) + " / " +
                                      std::to_string(total) + (comp > 0 ? "  x" + std::to_string(comp) : "")};
    if (premio > 0) {
      l.push_back(std::string(L("Premio: ", "Prize: ", "Prix : ", "Preis: ", "Premio: ")) + std::to_string(premio) +
                  " Zenie");
    }
    Marco(dl, a, D::kTop, l, float(dbz3::RelojT(ImGui::GetTime())));
    D::Bubble(dl, a, dice.c_str());
    D::Help(dl, a, {{"A", T::T("Aceptar", "OK")}});
  }

  // ---- Ranking: pagina 0 = perfil del jugador (ficha 01); 1 = Elo por personaje; despues, records de cada modo ----
  struct Fila {
    std::string pos, nombre, valor, extra;
    int medalla = 0;   // 1 bronce .. 5 diamante (retos/medalla_*), 0 = sin medalla
  };
  struct Pagina {
    std::string titulo, dice;
    std::vector<Fila> filas;
  };
  void Refrescar() {
    paginas_.clear();
    Perfil p;
    Pagina perfil{L("Tu perfil", "Your profile", "Ton profil", "Dein Profil", "Il tuo profilo"),
                  L("Tu rango sube al ganar y baja al perder (Elo de todos tus combates contra el luchador rival). "
                    "Tramos cada 100: Guerrero Z, Super Saiyan, Kaio, Kaio-shin y Shenron.",
                    "Your rank rises when you win and drops when you lose (Elo of all your fights against the rival "
                    "fighter). A tier every 100: Z Fighter, Super Saiyan, King Kai, Supreme Kai and Shenron.",
                    "Ton rang monte quand tu gagnes et baisse quand tu perds (Elo de tous tes combats contre le "
                    "combattant adverse). Un palier tous les 100 : Guerrier Z, Super Saiyan, Maître Kaïo, Kaïo Shin "
                    "et Shenron.",
                    "Dein Rang steigt bei Siegen und sinkt bei Niederlagen (Elo aller deiner Kämpfe gegen den "
                    "gegnerischen Kämpfer). Alle 100 eine Stufe: Z-Kämpfer, Super-Saiyajin, Meister Kaio, Kaioshin "
                    "und Shenlong.",
                    "Il tuo grado sale quando vinci e scende quando perdi (Elo di tutti i tuoi incontri contro il "
                    "lottatore avversario). Un livello ogni 100: Guerriero Z, Super Saiyan, Re Kaioh, Kaioshin e "
                    "Shenron."),
                  {}};
    if (CalcularPerfil(&p)) {
      perfil.filas = {
          {"", L("Rango", "Rank", "Rang", "Rang", "Grado"), NombreTramo(p.tramo), "", p.tramo + 1},
          {"", "Elo", std::to_string(p.elo),
           std::string(L("pico ", "peak ", "max ", "Spitze ", "picco ")) + std::to_string(p.pico)},
          {"", L("Combates", "Fights", "Combats", "Kämpfe", "Incontri"),
           std::to_string(p.ganados) + "-" + std::to_string(p.perdidos), ""},
          {"", L("Favorito", "Favorite", "Favori", "Liebling", "Preferito"), NombrePj(p.favorito), ""}};
    }
    paginas_.push_back(std::move(perfil));
    Pagina elo{T::T("Ranking de luchadores", "Fighter ranking"),
               T::T("Elo de cada luchador en tus combates: sube al ganar y baja al perder. "
                    "A la derecha, victorias y derrotas.",
                    "Each fighter's Elo in your fights: it rises when you win and drops when you lose. "
                    "On the right, wins and losses."),
               {}};
    std::array<Puesto, 10> top{};
    const int n = Ranking(top.data(), int(top.size()));
    for (int i = 0; i < n; ++i) {
      // medalla por Elo: menos de 1000 bronce, 1000 plata, 1100 oro, 1200 platino, 1300 diamante
      elo.filas.push_back({std::to_string(i + 1), NombrePj(top[size_t(i)].id), std::to_string(top[size_t(i)].elo),
                           std::to_string(top[size_t(i)].ganados) + "-" + std::to_string(top[size_t(i)].perdidos),
                           TramoDe(top[size_t(i)].elo) + 1});
    }
    paginas_.push_back(std::move(elo));
    std::lock_guard<std::mutex> lk(g_mu);
    CargarDatos();
    for (int tipo = 0; tipo < kTipos; ++tipo) {
      Pagina pg{NombreTipo(tipo),
                T::T("Mejor marca con cada luchador (combates ganados seguidos). x = veces completado.",
                     "Best run with each fighter (fights won in a row). x = times cleared."),
                {}};
      std::vector<Marca> v;
      for (const Marca& m : g_marcas) {
        if (m.tipo == tipo) v.push_back(m);
      }
      if (v.empty() && tipo > kKumite) continue;   // los retos nuevos, solo con alguna marca
      std::stable_sort(v.begin(), v.end(), [](const Marca& x, const Marca& y) {
        return x.completados != y.completados ? x.completados > y.completados : x.mejor > y.mejor;
      });
      for (size_t i = 0; i < v.size() && i < 10; ++i) {
        pg.filas.push_back(
            {std::to_string(i + 1), NombrePj(v[i].id), std::to_string(v[i].mejor) + " / " + std::to_string(v[i].total),
             v[i].completados > 0 ? "x" + std::to_string(v[i].completados) : "-",
             std::min(v[i].completados, 5)});   // medalla por veces completado
      }
      if (const EnCurso* c = tipo <= kKumite ? CursoDe(tipo) : nullptr) {
        pg.dice += std::string("\n") + T::T("Guardado: ", "Saved: ") + Paso(tipo, c->ganados + 1, c->total) + " (" +
                   NombrePj(c->id) + ")";
      }
      paginas_.push_back(std::move(pg));
    }
    pagina_ = std::clamp(pagina_, 0, int(paginas_.size()) - 1);
  }

  void DibujarRanking(ImDrawList* dl, const S::Area& a, float t, uint16_t pulsado) {
    const int np = std::max(int(paginas_.size()), 1);
    if (pulsado & (kLeft | kLb)) pagina_ = (pagina_ + np - 1) % np;
    if (pulsado & (kRight | kRb | kA)) pagina_ = (pagina_ + 1) % np;
    if (pulsado & kB) g_rank_abierto = false;   // vuelve el HUB
    if (paginas_.empty()) return;
    const Pagina& pg = paginas_[size_t(pagina_)];
    S::Banner(dl, a, "Ranking");
    // pagina: el selector de formato del juego (marco, letra y flechas)
    // (arriba a la derecha, junto a la cinta: el hueco de la esquina del esquema comun)
    const ImVec2 g0 = a.At(D::kCornerX0 + 40, D::kCornerY0), g1 = a.At(D::kX1 - 40, D::kCornerY1);
    S::Frame(dl, g0, g1, a.s);
    S::MenuText(dl, ImVec2((g0.x + g1.x) * 0.5f, (g0.y + g1.y) * 0.5f), D::kHead * a.s, pg.titulo.c_str(), true, false,
                g1.x - g0.x - 2 * D::kText * a.s);
    S::Arrows(dl, ImVec2(g0.x - 18 * a.s, g0.y), ImVec2(g1.x + 18 * a.s, g1.y), 40 * a.s, t);
    // tabla: hasta 8 filas con la letra del menu (el marco, al alto de las que hay); la primera (el lider) con la
    // franja del juego. (paso 47: las 8 filas y el relleno kPad caben sin llegar al pico del bocadillo)
    const auto& filas = pg.filas;
    const float y0 = D::kTop, paso = 47, x0 = D::kX0, x1 = D::kX1;
    const ImVec2 f0 = a.At(x0, y0), f1 = a.At(x1, y0 + 2 * D::kPad + paso * float(std::clamp(int(filas.size()), 1, 8)));
    S::Frame(dl, f0, f1, a.s);
    if (filas.empty()) {
      S::MenuText(dl, a.At(D::kCx, y0 + D::kPad + paso * 0.5f), D::kRow * a.s, T::T("Aún no hay combates", "No fights yet"),
                  false, true);
    }
    for (size_t i = 0; i < filas.size() && i < 8; ++i) {
      const float y = y0 + D::kPad + paso * (float(i) + 0.5f);
      if (i == 0) D::Stripe(dl, a, x0, y - paso * 0.5f + 2, x1, y + paso * 0.5f - 2, t);
      const float k = D::kSmall * a.s;
      S::MenuText(dl, a.At(x0 + 48, y), k, filas[i].pos.c_str(), i == 0, false);
      static const char* const kMedallas[] = {"retos/medalla_1_bronce", "retos/medalla_2_plata", "retos/medalla_3_oro",
                                              "retos/medalla_4_platino", "retos/medalla_5_diamante"};
      if (filas[i].medalla > 0) {
        if (const S::Pic md = S::Img(kMedallas[filas[i].medalla - 1])) {
          const float r = paso * 0.42f;
          S::DrawPic(dl, md, a.At(x0 + 96 - r, y - r), a.At(x0 + 96 + r, y + r));
        }
      }
      S::MenuText(dl, a.At(x0 + 265, y), k, filas[i].nombre.c_str(), i == 0, false, 240 * a.s);
      S::MenuText(dl, a.At(x0 + 480, y), k, filas[i].valor.c_str(), i == 0, false, 160 * a.s);
      S::MenuText(dl, a.At(x1 - D::kText - 50, y), k, filas[i].extra.c_str(), i == 0, false, 100 * a.s);
    }
    D::Bubble(dl, a, pg.dice.c_str());
    D::Help(dl, a, {{"LB", T::T("Anterior", "Previous")}, {"RB", T::T("Siguiente", "Next")}, {"B", T::T("Atrás", "Back")}});
  }

  // Laboratorio: el ultimo combate (de cualquier modo), la frame data de sus ultimos golpes y la practica contra
  // el muneco de combate.cpp (quieto o en guardia). Arriba/abajo eligen; A o izquierda/derecha cambian.
  void DibujarLab(ImDrawList* dl, const S::Area& a, uint16_t pulsado) {
    Lab lab;
    bool captura = false;
    int muneco = 0;
    // filas: 0 luchador, 1 rival (izquierda/derecha: cualquier jugable), 2 practicar, 3 muneco, 4 captura
    if (pulsado & kUp) lab_fila_ = (lab_fila_ + 4) % 5;
    if (pulsado & kDown) lab_fila_ = (lab_fila_ + 1) % 5;
    if (lab_jug_ < 0) {   // al abrir: los del ultimo Duelo
      int traje = 0;
      UltimoDuelo(&lab_jug_, &traje);
      uint8_t* base = Base();
      lab_riv_ = base ? int8_t(REX_LOAD_U8(kGame + 32)) : -1;
      if (!Jugable(lab_riv_) || lab_riv_ == lab_jug_) lab_riv_ = lab_jug_ == 7 ? 0 : 7;
    }
    const int lado = pulsado & kRight ? 1 : pulsado & kLeft ? -1 : 0;
    auto otro = [](int id, int dir) {
      for (int k = 0; k < 64; ++k)
        if (Jugable(id = (id + dir + 64) % 64)) break;
      return id;
    };
    if (lado && lab_fila_ == 0) lab_jug_ = otro(lab_jug_, lado);
    if (lado && lab_fila_ == 1) lab_riv_ = otro(lab_riv_, lado);
    {
      std::lock_guard<std::mutex> lk(g_mu);
      CargarDatos();
      const bool cambia = (pulsado & (kA | kLeft | kRight)) != 0;
      if (lab_fila_ == 3 && cambia) {
        g_muneco = 1 - g_muneco;
        GuardarDatos();
      } else if (lab_fila_ == 4 && cambia) {
        g_lab_captura = !g_lab_captura;
        GuardarDatos();
      } else if (lab_fila_ <= 2 && (pulsado & kA)) {   // practica: el luchador elegido contra el rival elegido
        if (!EmpezarPractica(lab_jug_, lab_riv_)) REXLOG_WARN("dbz3 modos: laboratorio: no se pudo pedir la practica");
      }
      lab = g_lab;
      captura = g_lab_captura;
      muneco = g_muneco;
    }
    if (pulsado & kB) g_lab_abierto = false;   // vuelve el HUB
    S::Banner(dl, a, T::T("Laboratorio", "Laboratory"));
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    // resumen del ultimo combate (marco compacto)
    std::vector<std::string> l;
    char buf[160];
    if (lab.cuadros == 0) {
      l = {L("Último combate", "Last fight", "Dernier combat", "Letzter Kampf", "Ultimo incontro"),
           L("Aún no hay combates", "No fights yet", "Pas encore de combats", "Noch keine Kämpfe",
             "Ancora nessun incontro")};
    } else {
      std::snprintf(buf, sizeof(buf), "%s  %d:%02d", L("Último combate", "Last fight", "Dernier combat", "Letzter Kampf",
                                                        "Ultimo incontro"), lab.cuadros / 3600, lab.cuadros / 60 % 60);
      l.emplace_back(buf);
      for (const Lado& s : lab.lado) {
        std::snprintf(buf, sizeof(buf), "%s: %.0f %s, %d %s, combo %d", NombrePj(s.id).c_str(), double(s.dano),
                      L("de daño", "damage", "de dégâts", "Schaden", "di danni"), s.golpes,
                      L("golpes", "hits", "coups", "Treffer", "colpi"), s.combo_mejor);
        l.emplace_back(buf);
      }
    }
    // esquema comun sin anfitrion (la tabla necesita el ancho): resumen en la esquina de arriba a la derecha, tabla y
    // opciones centradas, separadas D::kGap, y el bocadillo abajo sin pico
    // (texto desde el filo de la linea del marco, pad; resumen 28.., tabla 200..432, opciones 472..686, bocadillo 728..)
    const float pad = D::kPad - 4, sy0 = 28;
    S::Frame(dl, a.At(D::kCornerX0, sy0), a.At(D::kX1, sy0 + 2 * pad + 32 * float(l.size())), a.s);
    for (size_t i = 0; i < l.size(); ++i) {
      S::MenuText(dl, a.At((D::kCornerX0 + D::kX1) * 0.5f, sy0 + pad + 32 * (float(i) + 0.5f)), D::kSmall * a.s,
                  l[i].c_str(), i == 0, false, (D::kX1 - D::kCornerX0 - 2 * D::kText) * a.s);
    }
    // frame data de los ultimos golpes (los mas nuevos arriba)
    const float x0 = 150, x1 = 1050, y0 = 200 + pad - 8, paso = 28;   // y0 + 8 = primera fila, como antes
    S::Frame(dl, a.At(x0, 200), a.At(x1, 200 + 2 * pad + paso * 7), a.s);
    const float cx[5] = {300, 560, 680, 800, 940};
    const char* cab[5] = {L("Golpe", "Hit", "Coup", "Treffer", "Colpo"),
                          L("Arranque", "Startup", "Démarrage", "Anlauf", "Avvio"),
                          L("Daño", "Damage", "Dégâts", "Schaden", "Danni"),
                          L("Ventaja", "Advantage", "Avantage", "Vorteil", "Vantaggio"),
                          L("Resultado", "Result", "Résultat", "Ergebnis", "Risultato")};
    for (int c = 0; c < 5; ++c) S::MenuText(dl, a.At(cx[c], y0 + 8 + paso * 0.5f), D::kSmall * a.s, cab[c], true, false);
    if (lab.golpes.empty()) {
      S::MenuText(dl, a.At(600, y0 + 8 + paso * 3.5f), D::kSmall * a.s,
                  L("Sin golpes medidos: practica contra el muñeco", "No hits measured: practice against the dummy",
                    "Aucun coup mesuré : entraîne-toi contre le mannequin",
                    "Keine Treffer gemessen: übe gegen die Puppe", "Nessun colpo misurato: allenati contro il manichino"),
                  false, true);
    }
    for (size_t i = 0; i < lab.golpes.size(); ++i) {
      const Golpe& g = lab.golpes[lab.golpes.size() - 1 - i];
      const float y = y0 + 8 + paso * (float(i) + 1.5f);
      std::snprintf(buf, sizeof(buf), "%s %03X", NombrePj(g.id).c_str(), g.codigo);
      S::MenuText(dl, a.At(cx[0], y), D::kSmall * a.s, buf, false, false, 250 * a.s);
      S::MenuText(dl, a.At(cx[1], y), D::kSmall * a.s, std::to_string(g.arranque).c_str(), false, false);
      S::MenuText(dl, a.At(cx[2], y), D::kSmall * a.s, std::to_string(int(g.dano)).c_str(), false, false);
      if (g.listo_a >= 0 && g.listo_v >= 0) {
        std::snprintf(buf, sizeof(buf), "%+d", g.listo_v - g.listo_a);
      } else {
        std::snprintf(buf, sizeof(buf), "-");
      }
      S::MenuText(dl, a.At(cx[3], y), D::kSmall * a.s, buf, false, false);
      S::MenuText(dl, a.At(cx[4], y), D::kSmall * a.s,
                  g.guardia ? L("Guardia", "Guard", "Garde", "Abwehr", "Guardia") : L("Golpe", "Hit", "Touché", "Treffer",
                                                                                      "Colpito"),
                  false, false);
    }
    // opciones
    const std::string fila_muneco =
        std::string(L("Muñeco: ", "Dummy: ", "Mannequin : ", "Puppe: ", "Manichino: ")) +
        (muneco ? L("en guardia", "guarding", "en garde", "in Abwehr", "in guardia")
                : L("quieto", "standing", "immobile", "steht still", "fermo"));
    const std::string fila_captura =
        std::string(L("Captura por fotograma: ", "Frame capture: ", "Capture image par image : ", "Bild-für-Bild-Aufnahme: ",
                      "Cattura per fotogramma: ")) +
        (captura ? L("SÍ", "ON", "OUI", "AN", "SÌ") : L("NO", "OFF", "NON", "AUS", "NO"));
    const std::string filas[5] = {
        std::string(L("Luchador: ", "Fighter: ", "Combattant : ", "Kämpfer: ", "Lottatore: ")) + NombrePj(lab_jug_),
        std::string(L("Rival: ", "Rival: ", "Adversaire : ", "Gegner: ", "Avversario: ")) + NombrePj(lab_riv_),
        L("Practicar contra el muñeco", "Practice against the dummy", "S'entraîner contre le mannequin",
          "Gegen die Puppe üben", "Allenarsi col manichino"),
        fila_muneco, fila_captura};
    const float oy = 200 + 2 * pad + paso * 7 + 40, op = 34;
    S::Frame(dl, a.At(300, oy), a.At(900, oy + 2 * D::kPad + op * 5), a.s);
    for (int i = 0; i < 5; ++i) {
      const float y = oy + D::kPad + op * (float(i) + 0.5f);
      if (i == lab_fila_) D::Stripe(dl, a, 300, y - op * 0.5f + 1, 900, y + op * 0.5f - 1, t);
      // luchador y rival: las flechas del juego dentro del marco (antes "<  >" con letras)
      if (i == lab_fila_ && i <= 1) S::Arrows(dl, a.At(352, y - op * 0.5f), a.At(848, y + op * 0.5f), 28 * a.s, t);
      S::MenuText(dl, a.At(600, y), D::kRow * a.s, filas[i].c_str(), i == lab_fila_, false, (600 - 2 * D::kText) * a.s);
    }
    const char* dice =
        lab_fila_ <= 1
            ? L("Elige con izquierda y derecha quién practica y quién hace de muñeco. A: empezar.",
                "Use left and right to pick who practices and who is the dummy. A: start.",
                "Choisis avec gauche et droite qui s'entraîne et qui sert de mannequin. A : commencer.",
                "Wähle mit links und rechts, wer übt und wer die Puppe ist. A: starten.",
                "Scegli con sinistra e destra chi si allena e chi fa da manichino. A: inizia.")
        : lab_fila_ == 2
            ? L("Combate sin tiempo contra un muñeco que no ataca. Al salir (pausa) vuelves aquí con la tabla: arranque "
                "en cuadros, daño y ventaja (+ = te recuperas antes).",
                "Untimed fight against a dummy that doesn't attack. When you leave (pause) you come back here with the "
                "table: startup in frames, damage and advantage (+ = you recover first).",
                "Combat sans limite contre un mannequin qui n'attaque pas. En quittant (pause), tu reviens ici avec le "
                "tableau : démarrage en images, dégâts et avantage (+ = tu récupères avant).",
                "Kampf ohne Zeitlimit gegen eine Puppe, die nicht angreift. Beim Verlassen (Pause) kommst du hierher "
                "zurück: Anlauf in Bildern, Schaden und Vorteil (+ = du erholst dich zuerst).",
                "Incontro senza tempo contro un manichino che non attacca. Uscendo (pausa) torni qui con la tabella: "
                "avvio in fotogrammi, danni e vantaggio (+ = ti riprendi prima).")
        : lab_fila_ == 3
            ? L("Quieto: recibe cada golpe. En guardia: lo bloquea (ves la ventaja en guardia).",
                "Standing: takes every hit. Guarding: blocks it (you see the advantage on guard).",
                "Immobile : il prend chaque coup. En garde : il le bloque (tu vois l'avantage en garde).",
                "Steht still: nimmt jeden Treffer. In Abwehr: blockt ihn (du siehst den Vorteil bei Abwehr).",
                "Fermo: subisce ogni colpo. In guardia: lo para (vedi il vantaggio in guardia).")
            : L("Cada fotograma (acción, vida y posición de los dos) queda en user_data/dbz3/laboratorio.csv.",
                "Every frame (action, health and position of both) goes to user_data/dbz3/laboratorio.csv.",
                "Chaque image (action, vie et position des deux) va dans user_data/dbz3/laboratorio.csv.",
                "Jedes Bild (Aktion, Leben und Position beider) landet in user_data/dbz3/laboratorio.csv.",
                "Ogni fotogramma (azione, vita e posizione di entrambi) va in user_data/dbz3/laboratorio.csv.");
    S::Speech(dl, a.At(200, 728), a.At(1000, 852), a.s, dice, -1.0f);
    D::Help(dl, a,
                {{"A", lab_fila_ <= 2 ? L("Practicar", "Practice", "S'entraîner", "Üben", "Allenati")
                                      : L("Cambiar", "Change", "Changer", "Ändern", "Cambia")},
                 {"B", L("Atrás", "Back", "Retour", "Zurück", "Indietro")}});
  }

  int ultima_ = -1, pagina_ = 0, lab_fila_ = 0, lab_jug_ = -1, lab_riv_ = -1;
  float abierta_ = 0;
  bool esperar_suelta_ = true, bloqueado_ = false;
  uint16_t ultimo_ = 0;
  std::vector<Pagina> paginas_;
};

std::atomic<bool> g_panel_pedido{false};
void CrearPanel() {
  static ModosPanel* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new ModosPanel(rt->imgui_drawer());
}
void PedirPanel() {
  if (!g_panel_pedido.exchange(true)) dbz3::launcher::DeferUi(&CrearPanel);
}
// prueba de dibujo (frente R6, DBZ3_TEST_R6_PANTALLA): el panel se crea al llegar al menu principal
const bool g_prueba_r6 = std::getenv("DBZ3_TEST_R6_PANTALLA") && (std::thread([] {
                           while (dbz3::ingame::GameScreen() != 5) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                           PedirPanel();
                         }).detach(),
                                                                  true);

// ---- HUB (hub_menu.cpp): A en la entrada (hilo del juego, menu principal activo) ----
// Retos: con el ultimo personaje del Duelo (partida+31/+33) o, si hay uno guardado, con el suyo. Mientras dure
// (y hasta cerrar la pantalla del final) el HUB no se dibuja (busy); al acabar vuelve a abrirse.
}  // namespace
// Ultimo luchador del Duelo (partida+31/+33): el de partida del selector de retos.
void UltimoDuelo(int* id, int* traje) {
  int jugador = 0, t = 0;
  if (uint8_t* base = Base()) {
    jugador = int8_t(REX_LOAD_U8(kGame + 31));
    t = int8_t(REX_LOAD_U8(kGame + 33));
  }
  if (!Jugable(jugador)) jugador = t = 0;
  if (t < 0 || t > 3) t = 0;
  *id = jugador;
  *traje = t;
}
bool EsJugable(int id) { return Jugable(id); }
namespace {
bool RetoEnCurso() {
  const int f = Get().fase;
  // (g_prueba_r6: la pantalla fingida de la prueba de dibujo tapa el HUB como la de verdad)
  return f == kCombate || f == kEntre || (f == kFin && g_fin_visible.load()) || dbz3::rpg::Busy() || g_prueba_r6;
}
// Torre / Supervivencia / Kumite del HUB: el selector de retos de rpg.cpp (el clasico y los nuevos de cada entrada).
void AbrirSelector(int entrada) {
  PedirPanel();
  dbz3::rpg::AbrirSelector(entrada);
}
void AbrirRanking() {
  PedirPanel();
  g_rank_abierto = true;
}
bool RankingAbierto() { return g_rank_abierto.load(); }
void AbrirLab() {
  PedirPanel();
  g_lab_abierto = true;
}
bool LabAbierto() { return g_lab_abierto.load() || g_practica.load(); }
const bool g_hub = dbz3::hub::RegisterMode("torre", [] { AbrirSelector(kTorre); }, &RetoEnCurso) &&
                   dbz3::hub::RegisterMode("supervivencia", [] { AbrirSelector(kSupervivencia); }, &RetoEnCurso) &&
                   dbz3::hub::RegisterMode("kumite", [] { AbrirSelector(kKumite); }, &RetoEnCurso) &&
                   dbz3::hub::RegisterMode("ranking", &AbrirRanking, &RankingAbierto) &&
                   dbz3::hub::RegisterMode("laboratorio", &AbrirLab, &LabAbierto) &&
                   dbz3::hub::RegisterEntry("mision100", {"Misión 100", "Mission 100", "Mission 100", "Mission 100", "Missione 100"},
                                            {"Cien misiones por sagas: combates, contrarreloj, aguantar, reglas especiales, "
                                             "Tag Team y supervivencia. ¡Consigue el rango S en todas!",
                                             "A hundred missions by saga: battles, time attacks, survival, special rules "
                                             "and Tag Team. Get the S rank in all of them!",
                                             "Cent missions par saga : combats, contre la montre, survie, règles spéciales "
                                             "et Tag Team. Obtiens le rang S partout !",
                                             "Hundert Missionen nach Sagas: Kämpfe, Zeitangriffe, Durchhalten, Sonderregeln "
                                             "und Tag Team. Hol dir überall Rang S!",
                                             "Cento missioni per saga: scontri, contro il tempo, resistenza, regole speciali "
                                             "e Tag Team. Ottieni il grado S in tutte!"},
                                            [] { AbrirSelector(3); }, &RetoEnCurso) &&
                   dbz3::hub::RegisterEntry("heroe", {"Modo Héroe", "Hero Mode", "Mode Héros", "Heldenmodus", "Modalità Eroe"},
                                            {"Tu heroe con su historia propia: crea su cuerpo y su estilo, entrena con "
                                             "maestros y sube de nivel. Entrada propia, fuera del Kumite.",
                                             "Your own hero and story: create his body and style, train with masters and "
                                             "level up. Its own entry, outside the Kumite.",
                                             "Ton heros et son histoire : cree son corps et son style, entraine-toi avec "
                                             "des maitres et monte de niveau. Entree propre, hors du Kumite.",
                                             "Dein Held mit eigener Geschichte: erstelle Koerper und Stil, trainiere bei "
                                             "Meistern und steige auf. Eigener Eintrag, ausserhalb des Kumite.",
                                             "Il tuo eroe con la sua storia: crea corpo e stile, allenati con i maestri "
                                             "e sali di livello. Voce propria, fuori dal Kumite."},
                                            [] { PedirPanel(); dbz3::rpg::AbrirHeroe(); }, &RetoEnCurso);

// PRUEBAS. DBZ3_TEST_MODO="tipo,jugador[,traje]" (tipo: torre, supervivencia, kumite): empieza el reto cuando
// el menu principal lleva 3 s en pantalla y, al acabar, escribe el ranking en el log.
// DBZ3_TEST_TORNEO="id,id,...": participantes COM del Torneo Mundial.
const bool g_test = [] {
  if (const char* t = std::getenv("DBZ3_TEST_TORNEO")) {
    std::vector<int> ids;
    for (std::istringstream ss(t); ss.good();) {
      int id = -1;
      char sep = 0;
      if (ss >> id) ids.push_back(id);
      ss >> sep;
    }
    SetTorneo(ids.data(), int(ids.size()));
  }
  const char* t = std::getenv("DBZ3_TEST_MODO");
  if (!t) return false;
  std::thread([s = std::string(t)] {
    char name[32] = {};
    int jugador = 0, traje = 0;
    std::sscanf(s.c_str(), "%31[^,],%d,%d", name, &jugador, &traje);
    const int tipo = std::max(TipoDe(name), 0);
    for (int quieto = 0; quieto < 120;) {   // 3 s en el menu principal
      quieto = dbz3::ingame::GameScreen() == 5 ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    PedirPanel();
    if (tipo == kHeroe) {   // DBZ3_TEST_MODO=heroe,jugador[,traje]: abre la creacion del Modo Heroe (entrada propia)
      dbz3::rpg::AbrirHeroe();
      REXLOG_INFO("dbz3 modos: prueba: Modo Heroe (entrada propia del HUB)");
      return;
    }
    if (tipo == kMision) {   // DBZ3_TEST_MODO=mision100,jugador,n: la mision n (1-100) de la Mision 100
      REXLOG_INFO("dbz3 modos: prueba: mision 100 n {} con id {}: {}", traje, jugador,
                  dbz3::rpg::ProbarMision(traje, jugador) ? "empieza" : "no se pudo empezar");
      return;
    }
    REXLOG_INFO("dbz3 modos: prueba: {} con id {} (traje {}): {}", kNombres[tipo], jugador, traje,
                Start(tipo, jugador, traje) ? "empieza" : "no se pudo empezar");
    while (Get().fase != kFin) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::array<Puesto, 5> top{};
    const int n = Ranking(top.data(), int(top.size()));
    for (int i = 0; i < n; ++i)
      REXLOG_INFO("dbz3 modos: ranking {}: id {} elo {} ({}-{}-{})", i + 1, top[i].id, top[i].elo, top[i].ganados,
                  top[i].perdidos, top[i].empates);
  }).detach();
  return true;
}();
}  // namespace

// S5-I1: ID que de verdad juega la plaza `plaza` del cuadro del Torneo cuando es un personaje de mod sin casilla propia
// (torneo.toml); -1 = el de su casilla.
int TorneoIdReal(int plaza) {
  std::lock_guard<std::mutex> lk(g_mu);
  return plaza >= 0 && plaza < 32 ? g_t_id[size_t(plaza)] : -1;
}

}  // namespace dbz3::modos

// Espera de la escena 24: pedida la 25, el juego pasa la CFG a sub_820FE668. Con un combate del reto se
// completa antes: trajes y vida de los dos lados.
DBZ3_HOOK_EU(sub_821D7400) {
  using namespace dbz3::modos;
  if (dbz3::hub::OwnBattle() && int32_t(REX_LOAD_U32(kReqCur)) == 25) {
    g_base = base;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_st.fase == kCombate) {
      const Combate& c = g_reto.combates[size_t(g_st.ganados)];
      constexpr dbz3::GA p1 = kCfg + 16, p2 = kCfg + 96;
      REX_STORE_U16(p1 + 18, uint16_t(g_traje));
      REX_STORE_U32(p1 + 36, uint32_t(g_st.vida));
      REX_STORE_U32(p1 + 40, uint32_t(g_st.vida_max));
      REX_STORE_U16(p2 + 16, uint16_t(c.id));       // el del reto (el encadenado lo pidio al acabar el anterior)
      REX_STORE_U16(kCfg + 340, uint16_t(c.escenario));
      REX_STORE_U16(p2 + 18, uint16_t(c.traje));
      if (c.vida > 0) {
        REX_STORE_U32(p2 + 36, uint32_t(c.vida));
        REX_STORE_U32(p2 + 40, uint32_t(c.vida));
      }
      // tiempo (+348) en segundos: sub_820FE668 lo pasa a cuadros (x 60) y sub_820FD678 acaba el combate al llegar
      // (motivo 3). Medido 2026-10-15: segundos x 60 (como deja sub_82179408 las opciones) duraba 60 veces mas.
      if (const int sec = Tiempo(); sec > 0) REX_STORE_U32(kCfg + 348, uint32_t(sec));
      // reglas de escenarios.md R1 para este combate (se quitan al acabar el reto)
      dbz3::clima::Activar(c.clima);
      dbz3::gigantes::Activar(1, c.gigante);
      if (c.intro) dbz3::cine::Pedir();
      if (c.clima || c.gigante > 0 || c.intro)
        REXLOG_INFO("dbz3 modos: CFG: clima {} gigante {:.1f} intro {}", c.clima, c.gigante, c.intro);
      // capsulas del jugador para este combate (rec+52 cuantas, rec+54 IDs, como DBZ3_TEST_CAPS de hub.cpp): las del
      // reto propio (Modo Heroe, cartas, exploracion), sin tocar las de la partida
      const int antes = std::min<int>(REX_LOAD_U16(p1 + 52), 7);
      if (g_reglas.ncaps >= 0) {   // solo las del reto (heroe) o sumadas a las que ya trae (hasta 7)
        int n = g_reglas.reemplazar ? 0 : antes;
        for (int i = 0; i < g_reglas.ncaps && n < 7; ++i, ++n) {   // (la macro evalua la direccion dos veces)
          REX_STORE_U16(p1 + 54 + 2 * uint32_t(n), uint16_t(g_reglas.caps[i]));
        }
        for (int i = n; i < 7; ++i) REX_STORE_U16(p1 + 54 + 2 * uint32_t(i), 0);
        REX_STORE_U16(p1 + 52, uint16_t(n));
      }
      REXLOG_INFO("dbz3 modos: CFG: capsulas del jugador {} -> {} ({} {} {} {} {} {} {})", antes, REX_LOAD_U16(p1 + 52),
                  REX_LOAD_U16(p1 + 54), REX_LOAD_U16(p1 + 56), REX_LOAD_U16(p1 + 58), REX_LOAD_U16(p1 + 60),
                  REX_LOAD_U16(p1 + 62), REX_LOAD_U16(p1 + 64), REX_LOAD_U16(p1 + 66));
      REXLOG_INFO("dbz3 modos: CFG: id {} traje {} vida {}/{} contra id {} traje {} vida {} (tiempo {} s, regen {}/s, "
                  "objetivo {} s)",
                  REX_LOAD_U16(p1 + 16), g_traje, g_st.vida, g_st.vida_max, REX_LOAD_U16(p2 + 16), c.traje,
                  int32_t(REX_LOAD_U32(p2 + 36)), Tiempo(), Regen(), Objetivo());
    }
  }
  orig(ctx, base);
}

// Fin de combate (antes de soltarlo): ganador y vida del jugador para el reto.
DBZ3_HOOK_EU(sub_820FEBA8) {
  using namespace dbz3::modos;
  bool mine = false;
  int raw = 0, winner = -1, reason = 0, hp = 0, rival_hp = 0;
  if (dbz3::hub::OwnBattle()) {
    std::lock_guard<std::mutex> lk(g_mu);
    mine = g_st.fase == kCombate;
  }
  if (mine) {
    g_base = base;
    raw = int32_t(REX_LOAD_U32(kResult));
    if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
      winner = int32_t(REX_LOAD_U32(mgr + 35136));
      reason = int32_t(REX_LOAD_U32(mgr + 35132));
      hp = int(LoadF(base, mgr + 2096 + 6080));
      rival_hp = int(LoadF(base, mgr + 2096 + 8240 + 6080));
    }
  }
  // antes de soltarlo: dentro va el fin de la escena (sub_821D3628, hub.cpp), que ya debe saber si hay encadenado
  if (mine) FinCombate(raw, winner, reason, hp, rival_hp);
  if (dbz3::hub::OwnBattle() && g_practica.load()) FinPractica();
  orig(ctx, base);
  LabFinCombate();
}

// Torneo Mundial: tras el sorteo nativo, los COM son los de la lista (en orden, hasta acabarla).
DBZ3_HOOK_EU(sub_821A28A8) {
  using namespace dbz3::modos;
  orig(ctx, base);
  std::lock_guard<std::mutex> lk(g_mu);
  g_t_id.fill(-1);
  std::vector<int> lista = g_torneo;
  if (lista.empty()) {   // user_data/dbz3/modos/torneo.toml: participantes = [id, ...]
    const std::string text = LeerTexto(dbz3::settings::UserDataRoot() / "modos" / "torneo.toml");
    try {
      if (!text.empty()) {
        const toml::table t = toml::parse(text);
        if (const toml::array* a = t["participantes"].as_array()) {
          for (const toml::node& n : *a) lista.push_back(int(n.value_or(int64_t(-1))));
        }
      }
    } catch (const toml::parse_error& e) {
      REXLOG_WARN("dbz3 modos: torneo.toml no se puede leer ({})", e.description());
    }
  }
  if (lista.empty()) return;
  const uint32_t obj = REX_LOAD_U32(kWtScreen);
  const int rounds = int8_t(REX_LOAD_U8(kWt + 36690));
  if (!obj || rounds < 1 || rounds > 5) return;
  size_t k = 0;
  for (int i = 0; i < (1 << rounds) && k < lista.size(); ++i) {
    const uint32_t e = obj + 748 + 20 * uint32_t(i);
    if (int16_t(REX_LOAD_U16(e + 10)) >= 0) continue;   // humano
    const int id = lista[k++];
    if (!Jugable(id)) continue;
    int slot = SlotOf(base, id);
    if (slot >= 0) REX_STORE_U16(e + 8, uint16_t(slot));
    else slot = int16_t(REX_LOAD_U16(e + 8));
    if (slot < 0) continue;
    g_t_cell[size_t(i)] = slot;
    if (REX_LOAD_U16(kSlotToId + 2 * uint32_t(slot)) != uint32_t(id)) g_t_id[size_t(i)] = id;
    REXLOG_INFO("dbz3 modos: torneo: plaza {} = id {} (casilla {})", i, id, slot);
  }
}

// Torneo Mundial: preparacion del combate (cada tick del cuadro). Un participante sin casilla propia juega con
// su ID (la casilla es la de su anfitrion).
DBZ3_HOOK_EU(sub_821A5A30) {
  using namespace dbz3::modos;
  orig(ctx, base);
  std::lock_guard<std::mutex> lk(g_mu);
  const uint32_t obj = REX_LOAD_U32(kWtScreen);
  if (!obj) return;
  for (uint32_t p = 0; p < 2; ++p) {
    const int i = int8_t(REX_LOAD_U8(kWt + 36688 + p));
    if (i < 0 || i >= 32 || g_t_id[size_t(i)] < 0) continue;
    // El byte de partida (+31/+32) es un ID de casilla del juego (0-63); un ID logico fuera de ahi no se escribe.
    if (g_t_id[size_t(i)] > 63) continue;
    // DS193 (issue #55): este gancho corre en el tick del cuadro. Con el torneo ya terminado (obj+104 = 1, lo pone
    // sub_821A3260 tras el ultimo combate) el objeto puede seguir vivo mientras la tarea del cuadro se apaga: no se
    // toca `partida+31/32` ahi. Escribir un ID de otra ronda despues del final dejaba al juego cargando el modelo.
    // (El objeto del cuadro es 1388 B y +104 es el aviso de "terminado", docs/12_conectores/12_torneo_como_motor.md s2.2.)
    // Se mira solo con un participante de mod en juego: sin lista propia este gancho no toca nada, como antes.
    if (REX_LOAD_U32(obj + 104)) return;
    if (REX_LOAD_U16(obj + 748 + 20 * uint32_t(i) + 8) != uint32_t(g_t_cell[size_t(i)])) continue;
    if (REX_LOAD_U8(kGame + 31 + p) != uint32_t(g_t_id[size_t(i)])) {
      REX_STORE_U8(kGame + 31 + p, uint8_t(g_t_id[size_t(i)]));
      REXLOG_INFO("dbz3 modos: torneo: lado {} juega con id {}", p + 1, g_t_id[size_t(i)]);
    }
  }
}

// Menu del final del combate (estado 7 del combate, sub_820FF750 cada fotograma): sub_820DF568 lee la fila elegida
// del menu del marcador (B = *0x82324064: +4272 bit 0 = aun eligiendo, +4292 = fila). Fila 2 ("Reselect
// Characters") = R 4 y salir (sub_820FF820). En los combates del HUB (retos, pasarela, laboratorio) no sale nunca:
// tras el WINNER! se sale y cada modo ensena su pantalla (siguiente combate, revancha o resultado).
DBZ3_HOOK_EU(sub_820FF750) {
  if (dbz3::hub::OwnBattle()) {
    const uint32_t hud = REX_LOAD_U32(dbz3::modos::kHud);
    if (hud && (REX_LOAD_U32(hud + 4272) & 1)) {
      REX_STORE_U32(hud + 4272, REX_LOAD_U32(hud + 4272) & ~1u);
      REX_STORE_U16(hud + 4292, 2);
      REXLOG_INFO("dbz3 modos: sin menu final (pantalla propia del modo)");
    }
  }
  orig(ctx, base);
}

// Comprobacion de cada fotograma de la pelea (sub_820FD678: KO, fuera del ring y tiempo; medido 2026-10-15 en el
// modo 5: una llamada por fotograma del estado 4). Con `regen`, el rival del reto recupera vida mientras no ha caido.
namespace dbz3::escenarios {
void ComprobarZonaB(PPCContext& ctx, uint8_t* base);   // escenarios.cpp: ring out -> zona B (dbz3_escenarios_zonas_b)
}
DBZ3_HOOK_EU(sub_820FD678) {
  using namespace dbz3::modos;
  // X1esc: el aviso de ring out (mgr+35569) lo consume esta funcion y pasa el combate al estado 5; el gancho de
  // sub_82114910 llegaba ya con el estado 5 y nunca cambiaba de zona. Aqui el estado aun es 4.
  dbz3::escenarios::ComprobarZonaB(ctx, base);
  if (dbz3::hub::OwnBattle()) {   // reglas del reto propio (antes del laboratorio: ve la vida final)
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    std::lock_guard<std::mutex> lk(g_mu);
    if (mgr && g_st.fase == kCombate && g_reto.propio && REX_LOAD_U32(mgr + 35128) == 4) AplicarReglas(base, mgr);
  }
  {   // laboratorio: resumen de cualquier combate
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (mgr && REX_LOAD_U32(mgr + 35128) == 4) LabCuadro(base, mgr);
  }
  if (dbz3::hub::OwnBattle()) {
    int regen = 0;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_st.fase == kCombate) regen = Regen();
    }
    const uint32_t mgr = regen > 0 ? REX_LOAD_U32(kMgrPtr) : 0;
    if (mgr && REX_LOAD_U32(mgr + 35128) == 4) {
      const uint32_t x = mgr + 2096 + 8240;   // luchador del lado 2 (el rival)
      const float hp = LoadF(base, x + 6080), mx = LoadF(base, x + 6084);
      if (hp > 0.0f && hp < mx) StoreF(base, x + 6080, std::min(mx, hp + float(regen) / 60.0f));
      static int64_t log_ms = 0;
      if (NowMs() - log_ms > 5000) {
        log_ms = NowMs();
        REXLOG_INFO("dbz3 modos: regen {}/s: rival {:.0f}/{:.0f}", regen, LoadF(base, x + 6080), mx);
      }
    }
  }
  orig(ctx, base);
}

