// dbz3 - Pasarela de modos: modos de otros juegos de la saga dentro del HUB.
// Primer modo: "La leyenda de Mr. Satan" de Budokai 1 (PS2): 11 retadores seguidos con las charlas del original.
//
// Datos: la ISO de Budokai 1 del jugador (ps2_games junto a dbz3.exe), leida en tiempo de ejecucion por
// pasarela_b1.cpp: combates (tabla del ELF), charlas y avisos (imagenes de texto en el idioma del juego), logo,
// fondo, dibujo de Mr. Satan, dificultades y piezas del resultado. Nada del juego de origen va en la version. Entrada
// del HUB: "satan" (Leyenda de Mr. Satan); sin esa ISO sale en gris y el bocadillo explica que falta (RegisterGate).
// Pasos: presentacion con la dificultad (Novato/Experto/Avanzado, como el titulo de B1) -> por cada retador: charla de
// Mr. Satan (desde el 2.o) -> tarjeta del combate con el aviso del original -> combate (conector nativo
// dbz3::hub::RequestBattleEx, src/hub.cpp; Cell Jr: 5 seguidos) -> resultado con los puntos -> ... -> final.
// Reglas del original: las de la tabla por el conector (vida que no se recupera, ki inicial del jugador del registro
// (300 = 3 barras; en Krilin y Gohan, el que traia, 0xFFFF), 60 s, sin tiempo, 5 Cell Jr. de
// 1000, formas iniciales) y las que B1 tiene en codigo por memoria del luchador mientras se pelea (VigilarCombate):
// ki inicial del rival, armadura del Androide 16, Mr. Satan herido contra Trunks (solo el ki hace dano), ki lento
// contra Piccolo (valores propios: los de B1 no estan en su codigo, docs/14_frentes/pasarela.md), y el nivel de la IA
// del COM (mgr+35573: dificultad y retador con la tabla de B1, NivelComB3). Puntos como en B1 (0x1AF6F0 / 0x198E18 del
// ELF europeo): sucesos del combate (golpes, danos, agarres, tecnicas, combos) + LIFE (vida del ganador - la del otro,
// puede ser negativa) + TIME (segundos que sobran x 20), con tope de 0 tras cada suma; la leyenda se lleva la mitad.
// El resultado se lee sin ganchos: estado del combate mgr+35128 (4 pelea, 5 acabado, 6 pose, 7 menu final) y hueco
// del ganador mgr+35136 (0 = el jugador), mgr = *0x823D6CF4 (los de tagteam.cpp LabTick). Perder: reintentar o salir;
// el avance se guarda en user_data/dbz3/pasarela/progreso.toml.
// Sin ganchos REX_HOOK_RAW propios. US y EU (direcciones con su pareja EU: src/eu_map.h).
#include "reloj_prueba.h"
#include "pasarela.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>   // DS31: CaptureGuestOutput (fotograma congelado del final)
#include <rex/ui/window.h>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "generated/dbz3_init.h"
#include "cinematica.h"
#include "escenarios.h"   // S1-F03.2: Ring de Cell de B1 (alta de campos_iw.py)
#include "eu_map.h"
#include "ingame/game_style.h"
#include "ingame/tutorial.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"
#include "mods.h"
#include "ps2_modelo.h"

namespace dbz3::hub {
// 0 = lo normal en cada campo; time_s > 0 segundos, < 0 sin tiempo; hp > 0 vida inicial y maxima, < 0 solo la
// inicial; form = forma inicial
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
bool OwnBattle();
void BattleModeNext(int mode);   // DS170: modo de la CFG del siguiente combate (0 = el de siempre)
bool RegisterMode(const char* key, void (*open)(), bool (*busy)());   // hub_menu.cpp
bool RegisterGate(const char* key, bool (*ready)(), const char* (*why)());
}  // namespace dbz3::hub
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::ingame {
int GameScreen();   // online_panel.cpp
}
namespace dbz3::combate {
int OndasEvento(int lado, int tipo);   // combate.cpp: ondas del lado dado; 0 devueltas con G, 1 con un golpe, 2 desviadas
void OcultarHud(bool ocultar);         // S1-F03.1: combate.h (marcador del juego)
}
namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // net/netplay_hooks.cpp (< 0: el mando de verdad)
}
namespace dbz3::iwmini {
void PadPrueba(int dir);   // iw_minijuegos.cpp: palanca a la izquierda / derecha de la pantalla (0 suelta)
}

namespace dbz3::pasarela {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kStages = 0x82323760, kCfg = 0x8245EFC0;   // escenarios: 17 x 92 B, +0x10 mapa (stage_ext.h)
constexpr int kMainScreen = 5, kJugador = 14;                    // Mr. Satan en B3
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kStart = 0x0010, kA = 0x1000, kB = 0x2000;
constexpr const char* kClave = "hercule";
bool g_real = false;       // prueba (",real"): el prologo dura lo que en B1 aunque la prueba avance sola
float g_auto_seg = 4.0f;   // prueba (DBZ3_TEST_PASARELA=auto, ",seg=N"): segundos por pantalla
// Luchador X = mgr + 2096 + 8240 * lado: vida X+6080 (f32), maxima X+6084, ki X+6100, tope X+6104 (docs/15_tecnicas);
// mgr+35573 = nivel de la IA del COM (sub_820FC9B0 lo pone de las opciones o del nivel del Torneo; lo leen las tablas
// por personaje de sub_8213E6C8); mgr+2028 tiempo limite en cuadros, mgr+35144 cuadros pasados (f32, sub_820FD678).
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVida = 6080, kVidaMax = 6084, kKi = 6100, kKiMax = 6104;
constexpr uint32_t kNivelCom = 35573, kLimite = 2028, kPasado = 35144;
constexpr float kKiBarra = 1000.0f;   // ki de B3 por barra (medido: ver docs/16_pasarela_modos/01 s5)

int64_t NowMs() { return dbz3::RelojMs(); }
uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
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

// ---- Datos leidos de la ISO (hilo propio; se cambian enteros) ----
std::mutex g_datos_mu;
std::shared_ptr<const DatosB1> g_datos;   // nullptr = sin ISO o sin leer
std::string g_error;                      // por que no hay datos
std::atomic<bool> g_leyendo{false};
std::atomic<int64_t> g_ultimo_intento{-100000};
std::atomic<uint32_t> g_generacion{0};    // sube con cada lectura (el panel rehace sus texturas)

std::shared_ptr<const DatosB1> Datos() {
  std::lock_guard<std::mutex> lk(g_datos_mu);
  return g_datos;
}

// Busca la ISO y la lee en un hilo (como mucho una lectura a la vez; reintenta cada 10 s si no hay).
void Leer(bool forzar) {
  if (!forzar && NowMs() - g_ultimo_intento.load() < 10000) return;
  if (g_leyendo.exchange(true)) return;
  g_ultimo_intento = NowMs();
  std::thread([lang = std::string(T::LangCode())] {
    auto d = std::make_shared<DatosB1>();
    std::string error;
    const std::filesystem::path iso = BuscarIsoB1();
    bool ok = !iso.empty();
    if (!ok) error = "sin ISO de Budokai 1 en ps2_games";
    const int64_t t0 = NowMs();
    if (ok) ok = LeerLeyendaSatan(iso, lang, *d, error);
    {
      std::lock_guard<std::mutex> lk(g_datos_mu);
      if (ok) g_datos = d;
      g_error = error;
    }
    if (ok) {
      ++g_generacion;
      REXLOG_INFO("dbz3 pasarela: {} leida en {} ms: ELF {} tabla 0x{:X}, {} combates, {} de la historia, charlas {} "
                  "({}, {} imagenes), resultado {}", d->iso, NowMs() - t0, d->elf, d->tabla, d->combates.size(),
                  d->historia.size(), d->afs.empty() ? "-" : d->afs, d->idioma, d->charlas.size(),
                  d->res.retadores ? "si" : "no");
    } else {
      REXLOG_INFO("dbz3 pasarela: leyenda de Mr. Satan no disponible ({})", error);
    }
    g_leyendo = false;
  }).detach();
}

// ---- Avance guardado: user_data/dbz3/pasarela/progreso.toml (clave = valor; una por modo) ----
std::mutex g_prog_mu;
std::filesystem::path Carpeta() { return dbz3::settings::UserDataRoot() / "pasarela"; }
std::string LeerFichero(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
std::map<std::string, int64_t> LeerTodo() {   // con g_prog_mu
  std::map<std::string, int64_t> out;
  try {
    const toml::table t = toml::parse(LeerFichero(Carpeta() / "progreso.toml"));
    for (const auto& [k, v] : t) {
      if (const auto n = v.value<int64_t>()) out[std::string(k.str())] = *n;
    }
  } catch (const toml::parse_error&) {
  }
  return out;
}

// Nombres de B3 (los de tagteam_panel.cpp, en espanol e ingles como el resto de textos propios).
struct Nombre {
  int id;
  const char *es, *en;
};
constexpr Nombre kNombres[] = {
    {0, "Goku", "Goku"}, {2, "Gohan (niño)", "Kid Gohan"}, {3, "Gohan (adolescente)", "Teen Gohan"}, {4, "Gohan", "Gohan"},
    {5, "Gran Saiyaman", "Great Saiyaman"}, {6, "Goten", "Goten"}, {7, "Vegeta", "Vegeta"}, {8, "Trunks", "Trunks"},
    {9, "Trunks (niño)", "Kid Trunks"}, {10, "Krilin", "Krillin"}, {11, "Piccolo", "Piccolo"},
    {12, "Ten Shin Han", "Tien"}, {13, "Yamcha", "Yamcha"}, {14, "Mr. Satán", "Hercule"}, {15, "Videl", "Videl"}, {16, "Kaio-shin", "Supreme Kai"}, {18, "Raditz", "Raditz"},
    {19, "Nappa", "Nappa"}, {20, "Ginyu", "Ginyu"}, {21, "Recoome", "Recoome"}, {27, "Freezer", "Frieza"},
    {28, "Androide 16", "Android 16"}, {29, "Androide 17", "Android 17"}, {30, "Androide 18", "Android 18"},
    {32, "Dr. Gero", "Dr. Gero"}, {33, "Cell", "Cell"}, {34, "Majin Bu", "Majin Buu"}, {35, "Súper Bu", "Super Buu"},
    {36, "Bu (niño)", "Kid Buu"}, {37, "Dabura", "Dabura"}, {38, "Cooler", "Cooler"}, {41, "1-Star Dragon", "1-Star Dragon"}, {42, "Saibaman", "Saibaman"}, {43, "Cell Jr.", "Cell Jr."}};

// Personajes de mods: mods/_roster/roster.toml ("# <Nombre> (mod <carpeta>)" + "id = N", como tagteam_panel.cpp) y el
// origen de cada uno en mods/<carpeta>/personaje.toml (origen_fuente / origen_clave, los escribe importar.py).
struct Port {
  std::string nombre, carpeta, fuente, clave;
};
// Los trajes extra (traje.toml) salen como "# <NOMBRE>: traje extra (mod <carpeta>)" + "id = N".
struct Roster {
  std::map<int, Port> ports;
  std::map<int, std::vector<std::string>> trajes;   // ID -> carpetas, en el orden en que se anaden
};
std::mutex g_ports_mu;
Roster LeerRoster() {
  static Roster r;
  static std::filesystem::file_time_type sello{};
  static int64_t visto = -100000;
  std::lock_guard<std::mutex> lk(g_ports_mu);
  if (NowMs() - visto < 2000) return r;
  visto = NowMs();
  std::error_code ec;
  const auto fichero = dbz3::ModsRoot() / "_roster" / "roster.toml";
  const auto t = std::filesystem::last_write_time(fichero, ec);
  if (ec) {
    r = {};
    return r;
  }
  if (t == sello) return r;
  sello = t;
  r = {};
  std::ifstream in(fichero);
  Port pendiente;
  bool traje = false;
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.rfind("# ", 0) == 0) {
      const size_t at = l.rfind(" (mod ");
      if (at != std::string::npos && l.size() > at + 7 && l.back() == ')') {
        pendiente = {l.substr(2, at - 2), l.substr(at + 6, l.size() - at - 7), {}, {}};
        traje = pendiente.nombre.find(": traje extra") != std::string::npos;
      }
    } else if (l.rfind("id = ", 0) == 0 && !pendiente.carpeta.empty()) {
      const int id = std::atoi(l.c_str() + 5);
      if (traje) {
        r.trajes[id].push_back(pendiente.carpeta);
      } else {
        try {
          const toml::table p = toml::parse(LeerFichero(dbz3::ModsRoot() / pendiente.carpeta / "personaje.toml"));
          pendiente.fuente = p["personaje"]["origen_fuente"].value_or(std::string());
          pendiente.clave = p["personaje"]["origen_clave"].value_or(std::string());
        } catch (const toml::parse_error&) {
        }
        r.ports[id] = pendiente;
      }
      pendiente = {};
    }
  }
  return r;
}
std::map<int, Port> Ports() { return LeerRoster().ports; }

}  // namespace

std::shared_ptr<const DatosB1> DatosLeidos() {
  auto d = Datos();
  if (!d) Leer(false);
  return d;
}

int LeerProgreso(const std::string& clave, int defecto) {
  std::lock_guard<std::mutex> lk(g_prog_mu);
  const auto t = LeerTodo();
  auto it = t.find(clave);
  return it == t.end() ? defecto : int(it->second);
}

void GuardarProgreso(const std::string& clave, int valor) {
  std::lock_guard<std::mutex> lk(g_prog_mu);
  auto t = LeerTodo();
  t[clave] = valor;
  std::error_code ec;
  std::filesystem::create_directories(Carpeta(), ec);
  const auto tmp = Carpeta() / "progreso.toml.tmp";
  if (std::FILE* f = std::fopen(tmp.string().c_str(), "w")) {
    std::fprintf(f, "# Pasarela de modos: avance de cada modo (lo escribe el juego)\n");
    for (const auto& [k, v] : t) std::fprintf(f, "%s = %lld\n", k.c_str(), static_cast<long long>(v));
    // H9 24/10: solo se publica si la escritura fue bien (disco lleno -> el progreso anterior queda intacto)
    const bool ok = std::ferror(f) == 0;
    if (std::fclose(f) == 0 && ok) {
      std::filesystem::rename(tmp, Carpeta() / "progreso.toml", ec);
    }
  }
}

namespace {
constexpr int kUmbralesPE[11] = {0, 100, 250, 450, 700, 1000, 1400, 1900, 2500, 3200, 4000};
}
int RangoPE() { return LeerProgreso("rango_pe", 0); }
int RangoNivel(int pe) {
  int n = 0;
  while (n + 1 < 11 && pe >= kUmbralesPE[n + 1]) ++n;
  return n;
}
const char* RangoNombre(int nivel) {
  static const char* kKyu[5] = {"Kyu 5", "Kyu 4", "Kyu 3", "Kyu 2", "Kyu 1"};
  static const char* kDan[5] = {"Dan 1", "Dan 2", "Dan 3", "Dan 4", "Dan 5"};
  nivel = std::clamp(nivel, 0, 10);
  if (nivel < 5) return kKyu[nivel];
  if (nivel < 10) return kDan[nivel - 5];
  return T::T("Maestro", "Master");
}
// ponytail: sin la prueba de ascenso de los Dan (Tekken: partida de ascenso); se sube al llegar a los PE
int RangoMejora(const std::string& clave, int valor, int pe_por_paso, int pe_primera) {
  const int antes = LeerProgreso(clave, 0);
  if (valor <= antes) return 0;
  const int pe = (valor - antes) * pe_por_paso + (antes == 0 ? pe_primera : 0);
  GuardarProgreso(clave, valor);
  const int total = RangoPE() + pe, n0 = RangoNivel(total - pe), n1 = RangoNivel(total);
  GuardarProgreso("rango_pe", total);
  if (n1 > n0) GuardarProgreso("rango_ascenso", n1 + 1);   // el HUB lo anuncia (hub_panel.cpp) y lo pone a 0
  REXLOG_INFO("dbz3 rango: +{} PE por {} ({} -> {}): {} PE, {}", pe, clave, antes, valor, total, RangoNombre(n1));
  return pe;
}

int TrajeExtra(int id, const std::string& carpeta) {
  const auto r = LeerRoster();
  auto it = r.trajes.find(id);
  if (it == r.trajes.end() || id < 0 || id >= 105) return -1;
  const auto& v = it->second;
  const auto k = std::find(v.begin(), v.end(), carpeta);
  uint8_t* base = Base();
  if (k == v.end() || !base) return -1;
  // roster_ext.cpp AppendCostume: cada traje extra suma uno a los trajes del personaje (char96 +4)
  const int ahora = REX_LOAD_U8(dbz3::GA(0x8234ABB8u) + 96u * uint32_t(id) + 4);
  return ahora - int(v.size()) + int(k - v.begin());
}

int IdPort(const std::string& fuente, const std::string& clave) {
  for (const auto& [id, p] : Ports()) {
    if (p.fuente == fuente && p.clave == clave) return id;
  }
  return -1;
}

std::string NombreDe(int id) {
  for (const auto& n : kNombres) {
    if (n.id == id) return T::T(n.es, n.en);
  }
  const auto ports = Ports();
  if (auto it = ports.find(id); it != ports.end()) return it->second.nombre;
  return "#" + std::to_string(id);
}

int EscenarioPorMapa(int mapa) {
  uint8_t* base = Base();
  for (uint32_t i = 0; base && i < 17; ++i) {
    if (REX_LOAD_U32(kStages + 92 * i + 0x10) == uint32_t(mapa)) return int(i);
  }
  return 0;
}

// Escenario de B3 para un escenario de B1: el de los Juegos de Cell es el mapa 50 (CELL_RING_MAP).
int EscenarioB3(const std::string& nombre_b1) {
  int mapa = 50;
  std::string n = nombre_b1;
  if (n.rfind("CHG_", 0) == 0) n = n.substr(4);
  static const std::pair<const char*, int> kMapas[] = {{"CELL RING", 50}, {"NAMEC", 60},   {"RETTOU", 56},
                                                       {"SEISHIN", 58},   {"TENKAITI", 46}, {"BOKUSOUTI", 62},
                                                       {"IWAYAMA", 64},   {"SANKAN", 64},   {"TIKYUU", 68}};
  for (const auto& [k, m] : kMapas) {
    if (n.rfind(k, 0) == 0) mapa = m;
  }
  // S1-F03.2: el ring de Cell de B1, entero (hierba, losas y cuatro columnas), si el launcher lo creo de la ISO del
  // jugador (campos_iw.py auto, carpeta "Ring de Cell B1"); si no, el de B3 (destruido)
  if (mapa == 50)
    if (const int b1 = dbz3::escenarios::PorMod("Ring de Cell B1"); b1 >= 0) return b1;
  return EscenarioPorMapa(mapa);
}

// Forma de B1 (+0x52) -> forma inicial de B3 (rec+20), medida en el juego 2026-10-15: Goku 2 = SSJ (la 1 es el
// Kaioken), Gohan adolescente 1 = SSJ, Cell 2 = perfecto; Freezer: B1 1 = forma final (B3 3), 2 = al 100 % (B3 4).
// DS143: Piccolo B1 forma 2 = la 2.a transformacion de B3 (Piccolo tiene forms=3 y caps 57/58 en roster_db; todas usan
// el mismo modelo, asi que B3 no cambia la figura pero SI la ficha/estado, como la "fusion" de B1). El aviso 669 #11 de
// B1 lo llama "Piccolo the Evil King" (texto de broma): la forma jugable equivalente es la 2 de B3.
int FormaB3(int id, int forma_b1) {
  static constexpr int kFormas[][3] = {{0, 4, 2}, {3, 1, 1}, {33, 2, 2}, {11, 2, 2}, {27, 1, 3}, {27, 2, 4}};   // ID de B3, B1, B3
  for (const auto& f : kFormas) {
    if (f[0] == id && f[1] == forma_b1) return f[2];
  }
  if (Ports().count(id)) return forma_b1;
  return 0;
}

namespace {
std::atomic<int> g_estado_combate{0};

// Puntos por suceso de la Leyenda (B1 europeo, sub 0x1AF6F0(luchador, puntos, etiqueta): solo en el modo de Mr.
// Satan; suma en luchador+0x4F4C sin bajar de 0 y apunta la etiqueta en un anillo de 8 avisos, +0x63C0). Valores de
// sus 18 llamadas: HIT 30 x min(n, 10) al que pega mientras el combo lleva menos de 21 (n = golpe n-esimo del combo,
// 0x1AB700), DAMAGE... -20 x n al que recibe, COMBO FIN 60 x n y SPECIAL FIN 2000 al KO final (0x1AD670), THROW 300,
// SPECIAL 450, ANTI-THROW 400, REFLECT 300, BURST! 400, CUT-OFF 150, RECOVERY 100, AROUSE 500, ESCAPE... -300 x
// (n - 1), DULL... -200 x k; al acabar LIFE (vida del ganador - la del otro) y TIME (s que sobran x 20), y la leyenda
// suma la mitad del total del ganador (0x198F00). Aqui se miden sin ganchos los que se ven en memoria: golpes y
// combos (bajadas de vida, combo = menos de 1 s entre golpes, como el laboratorio), tecnicas (gastar 900 de ki de
// golpe), lanzamientos (codigo del movimiento del agarre comun 0x480-0x48F, X+2928) y el KO.
struct Suceso {
  const char* etiqueta;
  int puntos;
  int64_t ms;
};
std::mutex g_suc_mu;
std::vector<Suceso> g_sucesos;        // los ultimos 8, como B1
std::map<std::string, int> g_suc_suma;   // puntos por etiqueta del combate en curso (el desglose del resultado)
std::atomic<int> g_puntos_vivo{0};    // puntos de sucesos del combate en curso
// EU 0x1AF6F0 SatanMode_AddScore: suma en luchador+0x4F4C sin bajar de 0 y apunta {puntos, etiqueta} en el anillo de 8
// registros de 0x18 B de +0x63C0 (indices +0x63C4/+0x63C6). Aqui `total` hace de +0x4F4C y g_sucesos el anillo de 8.
void Anotar(int& total, int pts, const char* etiqueta) {
  if (!pts) return;
  total = std::max(0, total + pts);
  g_puntos_vivo = total;
  REXLOG_INFO("dbz3 pasarela: puntos: {} {:+d} (combate {})", etiqueta, pts, total);
  std::lock_guard<std::mutex> lk(g_suc_mu);
  g_suc_suma[etiqueta] += pts;
  g_sucesos.push_back({etiqueta, pts, NowMs()});
  if (g_sucesos.size() > 8) g_sucesos.erase(g_sucesos.begin());
}

// DULL...: el historial de ataques del luchador de B1 (anillo de 64 entradas en luchador+0x6532, indices +0x65B2 / +0x65B3):
// FUN_001af8e0 apunta cada ataque que empieza (su animacion) y -2 / -3 cuando acaba o se corta; FUN_001af960 cuenta, para
// cada entrada anterior, cuantos ataques seguidos coinciden con los ultimos (tope 8 por longitud) y devuelve la suma / 2.
// DULL = -200 x eso: repetir siempre lo mismo. Aqui los ataques se identifican por el codigo del movimiento de B3.
// EU 0x1AF800 SatanMode_AttackHistoryAdd: apunta cada ataque que empieza en el anillo de 32 (+0x64B0, indices
// +0x6530/+0x6531); aqui, Poner() del anillo de 64 por codigo de movimiento de B3.
// EU 0x1AF8D0 SatanMode_AttackHistoryClose: cuenta los ataques seguidos que coinciden para DULL...; aqui, Repite().
struct Historial {
  int16_t h[64] = {};
  int ini = 0, fin = 0;
  static int Ant(int i) { return (i + 63) & 63; }
  void Poner(int v) {
    const int sig = (fin + 1) & 63;
    if (ini == sig) ini = (ini + 1) & 63;
    h[fin] = int16_t(v);
    fin = sig;
  }
  int Repite() const {
    if (ini == fin) return 0;
    const int ult = Ant(fin);
    if (ult == ini) return 0;
    int cuenta[8] = {};
    for (int cand = Ant(ult); cand != ini; cand = Ant(cand)) {
      int n = 0, a = cand, b = ult;
      while (h[a] == h[b]) {
        if (h[b] >= 0) ++n;
        a = Ant(a);
        b = Ant(b);
        if (n > 8 || a == ini || h[b] < 0) break;
      }
      if (n > 0) {
        int& c = cuenta[std::min(n, 8) - 1];
        c = std::min(c + 1, 8);
      }
    }
    int suma = 0;
    for (const int c : cuenta) suma += c;
    return suma >> 1;
  }
};

// PRUEBA (DBZ3_PASARELA_PILOTO=1 | 2 | 3): el jugador juega solo por el mando virtual para medir los sucesos de B1 en el juego
// (sin esto haria falta pulsar a tiempo con las dos manos). Los dos luchadores no mueren y llevan el ki lleno.
//   1 (con DBZ3_COMBATE_MUNECO=ki: el rival quieto tira una bola de ki cada 0,75 s): guardia mantenida con Deflect-Back
//     (X+8141: la onda se devuelve, 3-15 s), con Deflect (X+8140: se desvia, 15-27 s) y toques de G cada 0,33 s sin
//     habilidades (27-39 s).
//   2 (contra la IA): a cada ataque nuevo del rival (accion 15 o 50), G tras un retraso rotando, con la palanca hacia el
//     rival una vez si y otra no (esquiva 38 / contraataque 39).
//   4 (contra la IA): andar hacia el rival y probar una combinacion de botones cada 3 s (busca el agarre propio y el choque
//     con el del rival).
//   3 (con DBZ3_COMBATE_MUNECO=quieto): 3 retrocesos (doble toque hacia atras cada 1,4 s) y 3 avances, a repetir (3-20 s);
//     X cada 0,6 s (20-30 s) y X / Y alternos (30-40 s).
// ponytail: solo para medir; no se usa en el juego.
struct PilotoB1 {
  int64_t pulsa = 0, suelta = 0;
  int n = 0;
  uint32_t riv = ~0u;
  bool uso = false;
  int64_t t0 = 0;
  void Paso(uint8_t* base, uint32_t x0, uint32_t x1, int modo) {
    const int64_t ahora = NowMs();
    uso = true;
    if (!t0) t0 = ahora;
    const int64_t te = ahora - t0;
    for (const uint32_t x : {x0, x1}) {
      const float vm = LoadF(base, x + kVidaMax), v = LoadF(base, x + kVida);
      if (vm > 0 && v < vm * 0.5f) StoreF(base, x + kVida, vm);
      StoreF(base, x + kKi, LoadF(base, x + kKiMax));
    }
    const float jx = LoadF(base, x0 + 4960 + 48), rx = LoadF(base, x0 + 4960 + 56);
    const int hacia = rx >= jx ? 1 : -1;   // lado de la pantalla donde esta el rival
    int d = 0;
    int32_t m = 0;
    if (te < 3000) {
    } else if (modo == 1) {
      if (te < 27000) {   // C / D: guardia mantenida con la habilidad
        const bool devuelve = te < 15000;
        REX_STORE_U8(x0 + 8141, devuelve ? 1 : 0);
        REX_STORE_U8(x0 + 8140, devuelve ? 0 : 1);
        m = 0x1000;
      } else {   // E: toques de G sin habilidades
        REX_STORE_U8(x0 + 8141, 0);
        REX_STORE_U8(x0 + 8140, 0);
        if ((te - 27000) % 330 < 100) m = 0x1000;
      }
    } else if (modo == 5) {   // choque de rayos: andar hacia el rival disparando (X+Y) sin parar y machacar si hay choque
      d = hacia;
      if (te % 500 < 100) m = 0xC000;
      if (REX_LOAD_U16(x0 + 3244) == 27) {
        d = 0;
        m = te % 120 < 60 ? 0x4000 : 0x8000;
      }
    } else if (modo == 4) {   // buscar el agarre: andar hacia el rival y probar una combinacion de botones cada 3 s
      static const int32_t kComb[5] = {0xC000, 0x6000, 0xA000, 0x5000, 0x9000};   // X+Y X+B Y+B A+X A+Y
      const int64_t k = (te - 3000) / 3000;
      d = hacia;
      if ((te - 3000) % 3000 < 60 && (te - 3000) / 3000 != n) {
        n = int(k);
        REXLOG_INFO("dbz3 pasarela: piloto: agarre, combinacion {:04X}", kComb[k % 5]);
      }
      if ((te - 3000) % 400 < 100) m = kComb[k % 5];
      if (REX_LOAD_U16(x0 + 3244) == 27) {   // choque de rayos (accion 27 en los dos): machacar X / Y para ganarlo
        d = 0;
        m = te % 120 < 60 ? 0x4000 : 0x8000;
      }
    } else if (modo == 3) {
      if (te < 20000) {   // A: 3 retrocesos y 3 avances (doble toque)
        const int64_t c = (te - 3000) % 8400, cc = c % 1400;
        if (cc < 70 || (cc >= 140 && cc < 210)) d = c / 1400 < 3 ? -hacia : hacia;
      } else if (te < 30000) {
        if ((te - 20000) % 600 < 100) m = 0x4000;
      } else if (te < 40000) {
        if ((te - 30000) % 600 < 100) m = ((te - 30000) / 600) % 2 ? 0x8000 : 0x4000;
      }
    } else {   // 2: a cada ataque nuevo del rival, G tras un retraso (con la palanca hacia el rival en los pares)
      static const int kRetraso[7] = {120, 160, 80, 200, 40, 240, 0};
      const uint32_t ra = REX_LOAD_U16(x1 + 3244), rc = REX_LOAD_U32(x1 + 2928) & 0xFFF;
      if ((ra << 16 | rc) != riv) {
        riv = ra << 16 | rc;
        if ((ra == 15 || ra == 50) && ahora > suelta + 700) {
          pulsa = ahora + kRetraso[n % 7];
          suelta = pulsa + 140;
          REXLOG_INFO("dbz3 pasarela: piloto: intento {}: rival {}/{:03X}, G tras {} ms{}", n, ra, rc, kRetraso[n % 7],
                      n % 2 == 0 ? " con la palanca hacia el rival" : "");
          ++n;
        }
      }
      const bool dir = n % 2 == 1;   // n ya subio: el intento en curso es n - 1 (par = con palanca)
      if (ahora >= pulsa - 70 && ahora < suelta && dir) d = hacia;
      if (ahora >= pulsa && ahora < suelta) m = 0x1000;
    }
    iwmini::PadPrueba(d);
    net::InyectarPad(0, m);
  }
  ~PilotoB1() {
    if (!uso) return;
    iwmini::PadPrueba(0);
    net::InyectarPad(0, -1);
  }
};
}  // namespace
int EstadoCombate() { return g_estado_combate.load(); }

void VigilarCombate(const char* quien, const Reglas& r, std::function<void(const Fin&)> fin) {
  std::thread([quien = std::string(quien), r, fin = std::move(fin)] {
    uint8_t* base = Base();
    bool visto = false;
    Fin f;
    uint32_t ultimo = 0;
    float vida_rival = -1, ki_jug = -1, anulado = 0, ki_perdido = 0;
    int64_t gasto_ki = -100000, gasto_ki_rival = -100000;
    float ki_riv = -1;
    // B2 (frente R2): vida que baja (ultimo vistazo) y sin bloqueo (configuracion del mando del jugador, 8 botones:
    // bits 1 P, 2 K, 4 G, 8 E como el muneco de combate.cpp; el boton que solo da G se apaga y vuelve al acabar)
    int64_t vida_t = 0;
    float vida_jug = -1;   // B2: vida del jugador del vistazo anterior (armadura)
    uint32_t g_dir = 0, g_mgr = 0;
    uint8_t g_antes[8] = {};
    auto devolver_g = [&] {
      for (uint32_t j = 0; g_dir && j < 8; ++j) REX_STORE_U8(g_dir + j, g_antes[j]);
      g_dir = 0;
    };
    const int64_t t0 = NowMs();
    // traje del jugador: el conector deja el 0 en la CFG (rec+18) y el juego la copia en la espera de la escena 24
    // (el fotograma siguiente): se escribe en cuanto el combate es nuestro. ponytail: carrera de un fotograma con el
    // hilo del juego; lo limpio es un parametro de traje en RequestBattleEx (propuesta en docs/14_frentes/campanas.md)
    // Capsulas: el conector deja las listas vacias (rec+52 = 0), asi nadie lanza tecnicas; como en el Duelo, cada
    // lado lleva la lista Normal de su personaje (tabla 0x8234ABB8 + 96 * ID: +80 cuantas, +82 7 IDs; la que el
    // Torneo pone al COM, docs/12_conectores/12_torneo_como_motor.md s4.1). Una lista ya puesta (DBZ3_TEST_CAPS) se
    // respeta.
    if (base) {
      while (!dbz3::hub::OwnBattle() && NowMs() - t0 < 3000) std::this_thread::yield();
      if (dbz3::hub::OwnBattle()) {
        if (r.traje_jugador > 0) REX_STORE_U16(kCfg + 16 + 18, uint16_t(r.traje_jugador));
        if (r.traje_rival > 0) REX_STORE_U16(kCfg + 96 + 18, uint16_t(r.traje_rival));
        std::string caps;
        for (uint32_t k = 0; r.capsulas && k < 2; ++k) {
          const uint32_t rec = kCfg + 16 + 80 * k, id = REX_LOAD_U16(rec + 16);
          // la lista Normal ya la pone el conector (hub.cpp); aqui solo la del rival a medida (capsulas_rival)
          const uint32_t tab = dbz3::GA(0x8234ABB8u) + 96u * id, propia = k == 1 ? uint32_t(r.capsulas_rival[0]) : 0;
          if ((REX_LOAD_U16(rec + 52) && !propia) || id >= 105) continue;
          const uint32_t n = std::min<uint32_t>(propia ? propia : REX_LOAD_U16(tab + 80), 7);
          REX_STORE_U16(rec + 52, uint16_t(n));
          for (uint32_t c = 0; c < 7; ++c) {
            const uint32_t cap = c >= n ? 0 : propia ? uint32_t(r.capsulas_rival[1 + c]) : REX_LOAD_U16(tab + 82 + 2 * c);
            REX_STORE_U16(rec + 54 + 2 * c, uint16_t(cap));
          }
          caps += " lado " + std::to_string(k + 1) + ": " + std::to_string(n);
        }
        REXLOG_INFO("dbz3 {}: traje del jugador {}, del rival {}; capsulas de serie{}", quien, r.traje_jugador,
                    r.traje_rival, caps.empty() ? " (ninguna)" : caps);
      }
    }
    float ki_lado[2] = {-1, -1};
    int forma_lado[2] = {-1, -1};
    int tecnicas = 0;
    // puntos de B1 (r.puntos_b1): por lado, vida del vistazo anterior, golpes del combo que recibe y su ultimo golpe
    float b1_vida[2] = {-1, -1};
    int b1_combo[2] = {0, 0};
    float combo_dano = 0;    // vida quitada al rival en el combo en curso del jugador (Fin::combo_dano_max)
    // H8 (IW): contadores del rango del RIVAL. +0x24 = mayor racha de carga de ki (+0x8c44) y +0x28 = mayor dano de
    // un combo (+0x8c48); ambos del rival (LoadWaitBattleMainExitEnd EU 0x208EA0 los lee del lado 1-cRam005c14ac).
    int rival_ki_racha = 0;
    float ki_riv_prev = -1;
    int64_t rival_ki_t0 = 0, rival_ki_ult = 0;   // inicio y ultimo cuadro con el ki del rival subiendo
    float combo_dano_rival = 0;   // vida quitada al jugador en el combo en curso del rival
    int64_t b1_golpe[2] = {-100000, -100000}, b1_tecnica = -100000;
    bool b1_agarre = false, b1_especial = false;
    bool empezado = false;   // reglas de inicio ya aplicadas en esta pelea
    int n_reglas = 0, n_reglas_ki = 0;   // registro de las reglas (pruebas)
    int64_t traza_ms = 0;
    // sucesos de B1 sin ganchos propios (docs/14_frentes/pasarela.md s3): accion y codigo de cada lado para la traza
    // (DBZ3_PASARELA_TRAZA), contadores de ondas de combate.cpp al empezar y el piloto de pruebas
    uint32_t tr_acc[2] = {~0u, ~0u}, tr_cod[2] = {~0u, ~0u};
    int ond_base[3] = {0, 0, 0};
    uint32_t ev_acc = ~0u, ev_cod = ~0u;   // accion / codigo del jugador del vistazo anterior (ESCAPE, RECOVERY, DULL)
    bool ev_ataque = false;                // el jugador esta en un ataque (accion 15, codigo 0x200-0x3FF)
    int escape_n = 0;                      // retrocesos seguidos (ESCAPE...)
    Historial hist;                        // DULL...
    const int con_piloto = std::getenv("DBZ3_PASARELA_PILOTO") ? std::atoi(std::getenv("DBZ3_PASARELA_PILOTO")) : 0;
    PilotoB1 piloto;
    while (base) {
      const bool own = dbz3::hub::OwnBattle();
      visto = visto || own;
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      const uint32_t st = mgr ? REX_LOAD_U32(mgr + 35128) : 0;
      const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
      if (mgr && st >= 1 && st <= 4) {   // nivel del COM: en cuanto existe el combate; la IA de cada luchador lo lee al
        f.empezo = true;                 // empezar (sub_8213E5C0). Escribirlo antes en la CFG (+344) no sirve: se pisa
        if (r.nivel_com >= 0 && REX_LOAD_U8(mgr + kNivelCom) != uint8_t(r.nivel_com)) {
          REXLOG_INFO("dbz3 {}: nivel del COM {} -> {}", quien, REX_LOAD_U8(mgr + kNivelCom), r.nivel_com);
          REX_STORE_U8(mgr + kNivelCom, uint8_t(r.nivel_com));
        }
      }
      // al llegar al READY (o a la pelea sin el), con los luchadores ya puestos: ki inicial del rival; tambien en la
      // revancha
      if (mgr && (st == 3 || st == 4) && !empezado && LoadF(base, x1 + kVidaMax) > 0) {
        empezado = true;
        b1_vida[0] = b1_vida[1] = -1;   // combo mayor (todos) y sucesos de B1: de cero en cada pelea
        b1_combo[0] = b1_combo[1] = 0;
        combo_dano = 0;
        combo_dano_rival = 0;
        rival_ki_racha = 0;
        ki_riv_prev = -1;
        rival_ki_t0 = rival_ki_ult = 0;
        f.combo_max = f.combo_dano_max = 0;
        f.rival_ki_max = f.rival_combo_dano_max = 0;
        if (r.puntos_b1) {   // pelea nueva (o revancha): puntos de sucesos a 0
          f.puntos = 0;
          g_puntos_vivo = 0;
          b1_agarre = false;
          for (int t = 0; t < 3; ++t) ond_base[t] = dbz3::combate::OndasEvento(0, t);
          ev_acc = ev_cod = ~0u;
          ev_ataque = false;
          escape_n = 0;
          hist = Historial();
          std::lock_guard<std::mutex> lk(g_suc_mu);
          g_sucesos.clear();
          g_suc_suma.clear();
        }
        const float k0 = LoadF(base, x0 + kKi), k1 = LoadF(base, x1 + kKi), m1 = LoadF(base, x1 + kKiMax);
        if (r.ki_rival > 0) StoreF(base, x1 + kKi, std::min(m1 > 0 ? m1 : 1e9f, r.ki_rival * kKiBarra));
        if (r.ki_jugador > 0) {   // registro de B1 (+0x2C): el ki con el que empieza el jugador (300 = 3 barras)
          const float m0 = LoadF(base, x0 + kKiMax);
          StoreF(base, x0 + kKi, std::min(m0 > 0 ? m0 : 1e9f, r.ki_jugador * kKiBarra));
          REXLOG_INFO("dbz3 {}: ki del jugador al empezar {:.0f} ({:.1f} barras de B1)", quien, LoadF(base, x0 + kKi),
                      r.ki_jugador);
        }
        if (r.ki_cero) StoreF(base, x0 + kKi, 0);
        if (const uint32_t pad = REX_LOAD_U16(x0 + 5028); r.sin_bloqueo && !g_dir && pad < 4) {   // X+5028 = mando
          g_mgr = mgr;
          g_dir = mgr + (pad + 4382) * 8;
          int con_g = 0;
          for (uint32_t j = 0; j < 8; ++j) {
            g_antes[j] = REX_LOAD_U8(g_dir + j);
            con_g += g_antes[j] == 4;
            if (g_antes[j] == 4) REX_STORE_U8(g_dir + j, 0);   // solo G; los atajos con G (P+G...) siguen
          }
          REXLOG_INFO("dbz3 {}: sin bloqueo (mando {}: {} botones de guardia apagados)", quien, pad, con_g);
        }
        REXLOG_INFO("dbz3 {}: ki al empezar {:.0f}/{:.0f} y {:.0f}/{:.0f} -> rival {:.0f}; vida {:.0f}/{:.0f} y "
                    "{:.0f}/{:.0f}; nivel del COM {}", quien, k0, LoadF(base, x0 + kKiMax), k1, m1,
                    LoadF(base, x1 + kKi), LoadF(base, x0 + kVida), LoadF(base, x0 + kVidaMax),
                    LoadF(base, x1 + kVida), LoadF(base, x1 + kVidaMax), REX_LOAD_U8(mgr + kNivelCom));
        if (r.traza) {   // pruebas (US): nivel de IA que cada luchador saco de su tabla (sub_8213E5C0: struct 0x823EE440 + 192 k, +22)
          REXLOG_INFO("dbz3 {}: nivel interno de la IA: lado 1 {}, lado 2 {} (mgr+35573 = {})", quien,
                      REX_LOAD_U8(0x823EE456u), REX_LOAD_U8(0x823EE456u + 192),
                      REX_LOAD_U8(mgr + kNivelCom));
        }
        vida_rival = LoadF(base, x1 + kVida);
        ki_jug = LoadF(base, x0 + kKi);
        {   // capsulas de cada luchador (registro de seleccion *(X+7412): +52 cuantas, +54 IDs)
          for (int k = 0; k < 2; ++k) {
            const uint32_t sel = REX_LOAD_U32(x0 + kXPaso * uint32_t(k) + 7412);
            std::string caps;
            const uint32_t n = sel ? REX_LOAD_U16(sel + 52) : 0;
            for (uint32_t c = 0; c < n && c < 7; ++c) caps += " " + std::to_string(REX_LOAD_U16(sel + 54 + 2 * c));
            REXLOG_INFO("dbz3 {}: capsulas del lado {}: {}{} (modo {})", quien, k + 1, n, caps, REX_LOAD_U16(mgr + 2018));
          }
        }
      }
      if (mgr && st == 4) {
        f.ganador = -1;   // pelea (tambien la revancha de "Battle Again?")
        if (r.traza && NowMs() - traza_ms >= 1000) {
          traza_ms = NowMs();
          REXLOG_INFO("dbz3 {}: traza: vida {:.0f} / {:.0f}, ki {:.0f} / {:.0f}", quien, LoadF(base, x0 + kVida),
                      LoadF(base, x1 + kVida), LoadF(base, x0 + kKi), LoadF(base, x1 + kKi));
        }
        for (int k = 0; k < 2; ++k) {   // formas (X+2980 indice de forma): transformaciones y absorciones
          const int fo = int(REX_LOAD_U32(x0 + kXPaso * uint32_t(k) + 2980));
          if (forma_lado[k] >= 0 && fo != forma_lado[k]) REXLOG_INFO("dbz3 {}: el lado {} pasa a la forma {}", quien, k + 1, fo);
          forma_lado[k] = fo;
        }
        // tecnicas (para las pruebas): de un fotograma a otro se gasta una barra de ki o mas (las rafagas gastan poco)
        for (int k = 0; k < 2; ++k) {
          const float ki = LoadF(base, x0 + kXPaso * uint32_t(k) + kKi);
          if (ki_lado[k] >= 0 && ki_lado[k] - ki >= 900.0f) {
            if (k == 0) {
              b1_tecnica = NowMs();
              b1_especial = false;
            }
            if (tecnicas < 12) {
              ++tecnicas;
              REXLOG_INFO("dbz3 {}: el lado {} gasta {:.0f} de ki de golpe (tecnica)", quien, k + 1, ki_lado[k] - ki);
            }
          }
          ki_lado[k] = ki;
        }
        // ki del jugador: lo que gasta (ki o tecnicas: ventana de 1,5 s para "solo ki") y lo que gana (ki lento)
        const float ki = LoadF(base, x0 + kKi);
        // gastar ki = bajar 30 o mas de un vistazo a otro (8 ms): rafagas y tecnicas; el modo hiper gasta poco a poco
        if (ki_jug >= 0 && ki < ki_jug - 30.0f) gasto_ki = NowMs();
        if (const float kr = LoadF(base, x1 + kKi); ki_riv >= 0 && kr < ki_riv - 30.0f) gasto_ki_rival = NowMs();
        ki_riv = LoadF(base, x1 + kKi);
        // H8 (IW +0x24 / +0x8c44, Battle_KiChargeTick EU 0x2334F0): racha de cuadros cargando ki. El original suma uno
        // por tick (60 Hz) mientras carga y HitCountFilter la reinicia al recibir un golpe. El vigilante muestrea a
        // 125 Hz, asi que la racha se mide por tiempo continuo de subida y se pasa a cuadros de 60 Hz.
        if (ki_riv_prev >= 0 && ki_riv > ki_riv_prev + 1.0f) {
          const int64_t ahora_ki = NowMs();
          if (!rival_ki_t0 || ahora_ki - rival_ki_ult > 50) rival_ki_t0 = ahora_ki;
          rival_ki_ult = ahora_ki;
          rival_ki_racha = 1 + int((ahora_ki - rival_ki_t0) * 60 / 1000);
          f.rival_ki_max = std::max(f.rival_ki_max, rival_ki_racha);
        } else if (rival_ki_t0 && NowMs() - rival_ki_ult > 100) {
          rival_ki_t0 = rival_ki_ult = 0;
          rival_ki_racha = 0;
        }
        ki_riv_prev = ki_riv;
        if (r.vida_baja > 0) {   // la pausa no cuenta
          const int64_t t = NowMs();
          const float vj = LoadF(base, x0 + kVida);
          if (vida_t > 0 && vj > 1 && !dbz3::GameDimmed()) {
            const float nueva = std::max(1.0f, vj - LoadF(base, x0 + kVidaMax) * r.vida_baja * float(t - vida_t) / 1000.0f);
            StoreF(base, x0 + kVida, nueva);
            if (vida_jug > 0) vida_jug -= vj - nueva;   // la armadura no anula lo que se gasta solo
          }
          vida_t = t;
        }
        if (r.ki_lento > 0 && ki_jug >= 0 && ki > ki_jug) {
          const float quita = (ki - ki_jug) * r.ki_lento;
          StoreF(base, x0 + kKi, ki - quita);
          ki_perdido += quita;
          if (++n_reglas_ki <= 3) {
            REXLOG_INFO("dbz3 {}: regla: ki lento, el jugador gana {:.1f} de ki y pierde {:.1f}", quien, ki - ki_jug,
                        quita);
          }
        }
        ki_jug = LoadF(base, x0 + kKi);
        // dano al rival: armadura (se anula una parte) y, herido, el que no viene del ki (se anula el 75 %)
        const float v = LoadF(base, x1 + kVida);
        // retos de IW: golpes que quitan vida al rival (cada bajada cuenta uno). Al cumplir (o al fallar el de no
        // atacar) el combate acaba como un ring out: mgr+35569 = hueco del ganador + 1 (el aviso que pone el juego al
        // caer del ring; tagteam.cpp DBZ3_TEST_RINGOUT)
        // golpe basico del jugador: el rival pierde vida sin que el jugador acabe de gastar ki (rafaga, tecnica) ni el
        // rival (su rafaga devuelta al cubrirse)
        const int64_t ahora = NowMs();
        const bool basico = ahora - gasto_ki > 400 && ahora - gasto_ki_rival > 2000;
        if (vida_rival > 0 && v < vida_rival - 0.5f && basico) {
          ++f.golpes;
          if (r.contador) r.contador->store(f.golpes);
          if (r.sin_atacar && !f.ataco) {
            f.ataco = true;
            REX_STORE_U8(mgr + 35569, 2);
            REXLOG_INFO("dbz3 {}: reto: el jugador ha atacado (fallo): el rival pierde {:.0f} (ki gastado hace {} ms el "
                        "jugador, {} ms el rival; accion del jugador {}, del rival {})", quien, vida_rival - v,
                        ahora - gasto_ki, ahora - gasto_ki_rival, REX_LOAD_U16(x0 + 3244), REX_LOAD_U16(x1 + 3244));
          }
          if (r.golpes > 0 && f.golpes == r.golpes) {
            REX_STORE_U8(mgr + 35569, 1);
            REXLOG_INFO("dbz3 {}: reto: {} golpes, cumplido", quien, f.golpes);
          } else if (r.golpes > 0 && (f.golpes % 5) == 0) {
            REXLOG_INFO("dbz3 {}: reto: {} de {} golpes", quien, f.golpes, r.golpes);
          }
        }
        if (r.ataque > 0 && vida_rival > 0 && v < vida_rival && v > 0) {   // daga de B2: el golpe quita mas
          StoreF(base, x1 + kVida, std::max(1.0f, v - (vida_rival - v) * r.ataque));
        }
        if (const float vj = LoadF(base, x0 + kVida); r.defensa > 0 && vida_jug > 0 && vj < vida_jug && vj > 0) {
          StoreF(base, x0 + kVida, vj + (vida_jug - vj) * r.defensa);   // armadura de B2: se anula una parte
        }
        vida_jug = LoadF(base, x0 + kVida);
        if (vida_rival > 0 && v < vida_rival && v > 0) {
          float parte = r.armadura_rival;
          if (r.solo_ki && NowMs() - gasto_ki > 1500) parte = std::max(parte, 0.75f);
          if (parte > 0) {
            const float vuelve = (vida_rival - v) * parte;
            StoreF(base, x1 + kVida, v + vuelve);
            anulado += vuelve;
            if (++n_reglas <= 6) {   // pruebas: los primeros golpes con la regla aplicada
              REXLOG_INFO("dbz3 {}: regla: golpe de {:.0f} al rival, se anula el {:.0f} % ({:.0f}); {}", quien,
                          vida_rival - v, parte * 100.0f, vuelve,
                          r.solo_ki && ahora - gasto_ki > 1500 ? "sin gastar ki (herido)" : "armadura");
            }
          }
        }
        vida_rival = LoadF(base, x1 + kVida);
        {   // combos (v = el que recibe): el mayor del jugador en Fin::combo_max; con r.puntos_b1, los sucesos de B1
          for (int v = 0; v < 2; ++v) {
            const float hp = LoadF(base, x0 + kXPaso * uint32_t(v) + kVida);
            // el combo acaba cuando el que recibe se recupera (accion X+3244 <= 2: neutra o guardia, como el
            // laboratorio; B1 pone a 0 su cuenta igual) y los golpes que para no cuentan (el "N HITS" de B3 tampoco)
            const bool libre = REX_LOAD_U16(x0 + kXPaso * uint32_t(v) + 3244) <= 2;
            if (libre && ahora - b1_golpe[v] > 100) b1_combo[v] = 0;
            if (v == 1 && b1_combo[1] == 0) combo_dano = 0;
            if (v == 0 && b1_combo[0] == 0) combo_dano_rival = 0;
            // golpe nuevo: la vida baja y llevaba mas de 60 ms quieta (un rayo baja en cada cuadro: es el mismo)
            if (b1_vida[v] >= 0 && hp < b1_vida[v] - 0.5f && !libre) {
              if (v == 0) rival_ki_racha = 0, rival_ki_t0 = rival_ki_ult = 0;   // H8: HitCountFilter (EU 0x233320) reinicia la carga al golpear
              if (v == 1) combo_dano += b1_vida[v] - hp;   // tambien lo que baja un rayo en cada cuadro
              if (v == 0) combo_dano_rival += b1_vida[v] - hp;
              if (ahora - b1_golpe[v] > 60) {
                if (ahora - b1_golpe[v] > 1500) {
                  b1_combo[v] = 0;
                  if (v == 1) combo_dano = b1_vida[v] - hp;
                  if (v == 0) combo_dano_rival = b1_vida[v] - hp;
                }
                const int n = ++b1_combo[v];
                if (!r.puntos_b1) {
                  // solo el combo mayor (abajo)
                } else if (v == 1) {
                  if (n < 21) Anotar(f.puntos, 30 * std::min(n, 10), "HIT");
                  if (ahora - b1_tecnica < 2500 && !b1_especial) {
                    b1_especial = true;
                    Anotar(f.puntos, 450, "SPECIAL");
                  }
                } else {
                  Anotar(f.puntos, -20 * n, "DAMAGE...");
                }
              }
              b1_golpe[v] = ahora;
            }
            b1_vida[v] = hp;
            if (v == 1 && b1_combo[1] > 0 && b1_combo[1] >= f.combo_max) {   // combo mayor del jugador y su dano
              if (b1_combo[1] > f.combo_max) {
                f.combo_dano_max = 0;
                REXLOG_INFO("dbz3 {}: combo mayor del jugador {} golpes", quien, b1_combo[1]);
              }
              f.combo_max = b1_combo[1];
              f.combo_dano_max = std::max(f.combo_dano_max, int(combo_dano + 0.5f));
            }
            // H8 (IW +0x28 / +0x8c48, HitCountFilter EU 0x233320): mayor dano de un combo del RIVAL sobre el jugador.
            if (v == 0 && combo_dano_rival > 0)
              f.rival_combo_dano_max = std::max(f.rival_combo_dano_max, int(combo_dano_rival + 0.5f));
          }
          // agarre comun de B3: BASE 0x480 (ranura 20). El que agarra lleva 0x480 y la victima 0x481 (medido 2026-10-17:
          // el rival agarra con 15/480 y el jugador pasa a 15/481): solo cuenta si el jugador es el primero o el menor
          const uint32_t cod = REX_LOAD_U32(x0 + 2928) & 0xFFF, cod_riv = REX_LOAD_U32(x1 + 2928) & 0xFFF;
          const bool agarre = cod >= 0x480 && cod < 0x490 && !(cod_riv >= 0x480 && cod_riv < cod);
          if (agarre && !b1_agarre && r.puntos_b1) Anotar(f.puntos, 300, "THROW");
          b1_agarre = agarre;
        }
        if (con_piloto) piloto.Paso(base, x0, x1, con_piloto);
        if (r.traza) {   // pruebas: cada cambio de accion (X+3244) o codigo (X+2928 & 0xFFF) de los dos lados
          for (int k = 0; k < 2; ++k) {
            const uint32_t x = x0 + kXPaso * uint32_t(k), a = REX_LOAD_U16(x + 3244), c = REX_LOAD_U32(x + 2928) & 0xFFF;
            if (a == tr_acc[k] && c == tr_cod[k]) continue;
            REXLOG_INFO("dbz3 {}: traza accion: lado {} {}/{:03X} (antes {}/{:03X}) vida {:.0f} ki {:.0f} guardia {:.1f}",
                        quien, k + 1, a, c, tr_acc[k] == ~0u ? 0 : tr_acc[k], tr_cod[k] == ~0u ? 0 : tr_cod[k],
                        LoadF(base, x + kVida), LoadF(base, x + kKi), LoadF(base, x + 6008));
            tr_acc[k] = a;
            tr_cod[k] = c;
          }
        }
        if (r.puntos_b1) {   // acciones del jugador (medidas en el juego, docs/14_frentes/pasarela.md s3)
          const uint32_t a = REX_LOAD_U16(x0 + 3244), c = REX_LOAD_U32(x0 + 2928) & 0xFFF;
          if (a != ev_acc || c != ev_cod) {
            const bool ataque = a == 15 && c >= 0x200 && c < 0x400;   // un golpe o tecnica propios (no un agarre ni una escena)
            // ESCAPE...: retroceso rapido (doble toque hacia atras: accion 6, codigo 006; B1 accion 6). Cada uno seguido
            // sin avanzar (acciones 5 / 9) ni atacar resta 300 x (n - 1)
            if (a == 6 && ev_acc != 6) {
              if (++escape_n > 1) Anotar(f.puntos, -300 * (escape_n - 1), "ESCAPE...");
            } else if (a == 5 || a == 9 || ataque) {
              escape_n = 0;
            }
            // BURST!: gana el choque de rayos (accion 27 en los dos; el que gana pasa a la accion 15, codigo 0x0EA, como la
            // animacion 0xEA de B1; el que pierde, a la 28)
            if (a == 15 && c == 0x0EA) Anotar(f.puntos, 400, "BURST!");
            // RECOVERY: el contraataque de G en el golpe (Z-Counter, accion 39: el jugador se teletransporta sin recibirlo)
            if (a == 39 && ev_acc != 39) Anotar(f.puntos, 100, "RECOVERY");
            // DULL...: cada golpe que empieza va al historial; al acabar el ataque (vuelve a una accion neutra o de paso,
            // no si lo cortan) se apunta el final y se resta 200 x las repeticiones (Historial::Repite)
            if (ataque) {
              hist.Poner(int(c));
            } else if (ev_ataque && (a <= 10 || a == 38 || a == 39)) {
              hist.Poner(-2);
              if (const int n = hist.Repite(); n > 0) Anotar(f.puntos, -200 * n, "DULL...");
            }
            ev_ataque = ataque;
            ev_acc = a;
            ev_cod = c;
          }
        }
        if (r.puntos_b1) {   // ondas de ki del jugador: REFLECT (devuelta con G o a golpe, B1 accion 0x1B) y CUT-OFF (desviada, 0x1A)
          const int dev = dbz3::combate::OndasEvento(0, 0) + dbz3::combate::OndasEvento(0, 1);
          const int ant = ond_base[0] + ond_base[1], desv = dbz3::combate::OndasEvento(0, 2);
          for (int i = ant; i < dev; ++i) Anotar(f.puntos, 300, "REFLECT");
          for (int i = ond_base[2]; i < desv; ++i) Anotar(f.puntos, 150, "CUT-OFF");
          ond_base[0] = dbz3::combate::OndasEvento(0, 0);
          ond_base[1] = dbz3::combate::OndasEvento(0, 1);
          ond_base[2] = desv;
        }
      }
      if (mgr && st >= 5 && st <= 7) {
        if (r.puntos_b1 && empezado && int32_t(REX_LOAD_U32(mgr + 35136)) == 0 && LoadF(base, x1 + kVida) <= 0) {
          const int64_t ahora = NowMs();   // KO del jugador: el combo y la tecnica que lo acaban
          if (b1_combo[1] > 0 && ahora - b1_golpe[1] < 1000) Anotar(f.puntos, 60 * b1_combo[1], "COMBO FIN");
          if (ahora - b1_tecnica < 3000) Anotar(f.puntos, 2000, "SPECIAL FIN");
        }
        if (empezado && (anulado > 0 || ki_perdido > 0)) {   // al acabar (tambien si sigue una revancha)
          REXLOG_INFO("dbz3 {}: reglas: {:.0f} de dano anulado (armadura {:.2f}, solo ki {}), {:.0f} de ki perdido "
                      "(ki lento {:.2f})", quien, anulado, r.armadura_rival, r.solo_ki, ki_perdido, r.ki_lento);
          anulado = ki_perdido = 0;
        }
        empezado = false;
        vida_t = 0;
        vida_jug = -1;
        devolver_g();
        f.ganador = int32_t(REX_LOAD_U32(mgr + 35136));
        for (int k = 0; k < 2; ++k) {
          f.vida[k] = int(LoadF(base, x0 + kXPaso * uint32_t(k) + kVida));
          f.vida_max[k] = int(LoadF(base, x0 + kXPaso * uint32_t(k) + kVidaMax));
          f.ki[k] = int(LoadF(base, x0 + kXPaso * uint32_t(k) + kKi));
        }
        const int32_t limite = int32_t(REX_LOAD_U32(mgr + kLimite));
        f.segundos = limite > 0 ? std::max(0, int((float(limite) - LoadF(base, mgr + kPasado)) / 60.0f)) : -1;
      }
      g_estado_combate = own ? int(st) : 0;
      if (own && st != ultimo) REXLOG_INFO("dbz3 {} estado del combate {} (ganador {})", quien, st, f.ganador);
      ultimo = st;
      if (visto && !own) break;
      if (!visto && NowMs() - t0 > 60000) {
        REXLOG_WARN("dbz3 {}: el combate no empezo en 60 s", quien);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    if (g_dir && REX_LOAD_U32(kMgrPtr) == g_mgr) devolver_g();   // salio sin final (pausa): G vuelve
    fin(f);
  }).detach();
}

namespace {
S::Pic CaraPic(int id) {   // hilo de dibujo
  if (const S::Pic f = S::Img(("caras/" + std::to_string(id)).c_str())) return f;
  static std::map<int, std::pair<std::string, std::unique_ptr<rex::ui::ImmediateTexture>>> cache;
  static std::map<int, S::Pic> pics;
  const auto ports = Ports();
  const auto it = ports.find(id);
  if (it == ports.end()) return {};
  auto& c = cache[id];
  if (c.first == it->second.carpeta) return pics[id];
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return {};
  c.first = it->second.carpeta;
  c.second.reset();
  pics[id] = {};
  const auto dir = dbz3::ModsRoot() / it->second.carpeta / "ui" / "_vista";
  for (const char* file : {"hud.png", "icono.png"}) {
    std::ifstream in(dir / file, std::ios::binary);
    if (!in) continue;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0;
    const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
    if (px.empty()) continue;
    c.second = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                     px.data());
    if (!c.second) continue;
    S::Pic p;
    p.tex = reinterpret_cast<ImTextureID>(c.second.get());
    p.w = float(w);
    p.h = float(h);
    pics[id] = p;
    break;
  }
  return pics[id];
}
}  // namespace

ImTextureID CaraDe(int id) { return CaraPic(id).tex; }

void DibujarCara(ImDrawList* dl, const S::Area& a, int id, float x0, float y0, float w, bool on, const char* nombre,
                 float t, bool espejo) {
  const float bob = on ? 4.0f * std::sin(t * 3.0f) : 0.0f;
  const ImVec2 p0 = a.At(x0, y0 + bob), p1 = a.At(x0 + w, y0 + w + bob);
  S::Frame(dl, p0, p1, a.s);
  if (const S::Pic f = CaraPic(id)) {
    // la cara llena el hueco del marco (alto), recortada por su linea: antes iba encajada a lo ancho y quedaba
    // pequena con medio marco vacio (revision de capturas 2026-10-16)
    const float in = S::mode::kLine * a.s;
    const ImVec2 q0(p0.x + in, p0.y + in), q1(p1.x - in, p1.y - in);
    const float k = std::max((q1.x - q0.x) / f.w, (q1.y - q0.y) / f.h);
    const ImVec2 c((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), h(f.w * k * 0.5f, f.h * k * 0.5f);
    dl->PushClipRect(q0, q1, true);
    S::DrawPic(dl, f, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y), ImVec4(0, 0, 0, 0),
               on ? IM_COL32_WHITE : IM_COL32(120, 120, 130, 255), espejo);
    dl->PopClipRect();
  }
  if (nombre && *nombre) S::MenuText(dl, a.At(x0 + w * 0.5f, y0 + w + 42 + bob), 44 * a.s, nombre, on, false, w * a.s);
}

namespace {

// ---- Pasos del modo ----
enum Tipo : int { kPresentacion, kCharla, kCombate };
struct Paso {
  int tipo = kPresentacion;
  int ronda = 0;      // retador (0..10)
  int vez = 0;        // combate: cual de los `cantidad` seguidos
};

std::vector<Paso> Pasos(const DatosB1& d) {
  std::vector<Paso> p;
  p.push_back({kPresentacion, 0, 0});
  for (int r = 0; r < int(d.combates.size()); ++r) {
    if (r > 0) p.push_back({kCharla, r, 0});
    for (int v = 0; v < std::max(1, d.combates[size_t(r)].cantidad); ++v) p.push_back({kCombate, r, v});
  }
  return p;
}

// ---- Estado (g_mu) ----
enum Fase : int { kNada = 0, kPantalla, kLuchando, kPerdido, kFin, kResultado };
const char* const kFaseNombre[] = {"nada", "pantalla", "combate", "perdido", "fin", "resultado"};
// 666 #7-9 del AFS del jugador: 0 Novel/Novice, 1 Avanzado/Adept, 2 Experto/Advanced (0x5E9C10 guarda el indice).
const char* const kDificultad[] = {"Novato", "Avanzado", "Experto"};
// Los de las imagenes 666 #7-9 de la ISO espanola de B1, en su orden (0 Novice, 1 Adept, 2 Advanced): "Novel",
// "Avanzado", "Experto" (comprobado en la captura D:/DBZ3HD/_pruebas/20261016-113134_camp_test_satan/a2_dificultad.png).
const char* NombreDificultad(int k) {
  return k == 0 ? T::T("Novel", "Novice") : k == 1 ? T::T("Avanzado", "Adept") : T::T("Experto", "Advanced");
}
// Cuadros (60/s) de la voz 2 de la charla del retador r (1..10) en el tablero de B1 (0x350B10, FUN_00232a80), contados
// desde el estado que la espera; la voz 1 entra 80 cuadros antes de ese estado (FUN_002324c0 -> 00232920).
// EU 0x232A80 SatanBoard_WaitCall: cuadro de entrada de la voz del retador = tabla 0x350B10[ronda-1]
// (500,700,700,650,500,750,830,700,890,730 cuadros); aqui, en ms para OirVozB1.
constexpr int kVoz2[10] = {500, 700, 700, 650, 500, 750, 830, 700, 890, 730};
int MsVoz2(int r) { return (80 + kVoz2[std::clamp(r, 1, 10) - 1]) * 1000 / 60; }   // de la voz 1 a la 2
// Duracion real de la voz 2 (ADX_JP.AFS entrada 1270 + 2*(r-1)) leida de su cabecera ADX: +0x08 frecuencia (24000 Hz
// en las 10) y +0x0C muestras totales. Antes se sumaban 4 s fijos y la voz se cortaba en las rondas largas (r=5: 15,4 s).
constexpr float kVoz2Dur[10] = {9.98f, 11.76f, 7.72f, 9.78f, 15.43f, 11.39f, 10.11f, 12.39f, 13.05f, 4.74f};

std::mutex g_mu;
std::shared_ptr<const DatosB1> g_d;   // datos del modo en curso
std::vector<Paso> g_pasos;
int g_fase = kNada, g_paso = 0, g_intentos = 0;
int g_vida_ganada = 0;   // vida de Mr. Satan al ganar el ultimo combate (para los que no la recuperan)
int g_ki_ganada = -1;    // ki de Mr. Satan al ganar el ultimo combate (registro 0xFFFF: el que traia; -1 = ninguno)
int g_dificultad = 0;    // indice de 0x5E9C10: 0 Novato, 1 Avanzado, 2 Experto (fila de kNivelB1)
int g_puntos = 0;        // de esta partida
int g_ronda_pts = 0;     // del ultimo retador (la pantalla del resultado)
int g_ronda_ev = 0, g_ronda_vida = 0, g_ronda_tiempo = 0;   // su desglose antes de la mitad: sucesos, LIFE, TIME
std::map<std::string, int> g_ronda_det;   // puntos de los sucesos nuevos (REFLECT, CUT-OFF, ESCAPE...) por etiqueta
uint64_t g_cambio = 0;   // sube con cada pantalla nueva (el panel reinicia el texto)
std::atomic<int> g_fase_pub{kNada};
std::atomic<bool> g_auto{false};
std::atomic<bool> g_panel{false};

void Publicar(int fase) {   // con g_mu
  if (fase == kFin) {
    REXLOG_INFO("dbz3 pasarela: final de la leyenda ({} pasos, {} puntos)", g_pasos.size(), g_puntos);
  } else if (fase != g_fase || fase == kPantalla) {
    const Paso& p = g_pasos[size_t(std::min<int>(g_paso, int(g_pasos.size()) - 1))];
    REXLOG_INFO("dbz3 pasarela: paso {}/{} ({} ronda {}{}): {}", g_paso + 1, g_pasos.size(),
                p.tipo == kPresentacion ? "presentacion" : p.tipo == kCharla ? "charla" : "combate", p.ronda + 1,
                p.tipo == kCombate && p.vez ? " vez " + std::to_string(p.vez + 1) : std::string(), kFaseNombre[fase]);
  }
  g_fase = fase;
  g_fase_pub = fase;
  ++g_cambio;
}

// S5-A3: musica de cada combate como B1. El byte +0x72 del registro es el indice de la tabla de pistas del ELF
// (VA 0x360540, u16: entrada de ADX_JP.AFS; puntero en 0x350CE8; la rutina 0x19AE60 la toca por el canal 0 al cargar el
// combate). MEDIDO en PCSX2 (gancho en 0x1369D0, 10 rondas, 22/10): indice de la tabla -> entrada que suena = 74, 74, 73,
// 71, 72, 82, 75, 81, 81, 79 para las rondas 2-11 (= tabla[byte 4, 4, 3, 1, 2, 15, 5, 14, 14, 12]); la ronda 1 tiene
// byte 0 y la tabla del ELF (0x360540) da tabla[0] = 70 (no se midio en PCSX2, pero el dato es del propio juego).
// La pista arranca 0,15 s despues de empezar la carga, o sea con la primera toma del "READY?".
constexpr int kBgmB1[17] = {70, 71, 72, 73, 74, 75, 76, 77, 78, 62, 63, 64, 79, 80, 81, 82, 83};
std::atomic<uint32_t> g_mus_ficha{0};
std::atomic<int> g_mus_pista{-1};
void DetenerMusica() {
  ++g_mus_ficha;
  g_mus_pista = -1;
  CallarMusicaPs2();
}

// S1 (A3 O3 #1): la mezcla de B3 durante las escenas de la leyenda. Medido en A3 ola 4 (volcado de audio del SDK +
// traza de streams): en combate B3 NO abre ninguna pista de musica; lo que queda es el ambiente del escenario (RMS
// 0,05, grave y tonal, centroide 614 Hz; parar todos los streams con sub_82081A48(-1) no lo cambia). Mientras dura la
// escena y suena la musica de B1, se calla esa mezcla con la ganancia del driver SDL (`audio_gain`, la misma que usa
// video_sfd.cpp) y se devuelve al valor que tuviera al acabar. En el combate NO se toca (callar la mezcla apagaria
// tambien los golpes; el ambiente medido no es musica). DBZ3_SIN_CALLAR_B3=1 lo apaga.
void CallarMezclaB3(bool si) {
  static const bool sin = std::getenv("DBZ3_SIN_CALLAR_B3") != nullptr;
  static double previa = -1;
  if (sin) return;
  if (si) {
    if (previa < 0) {
      const std::string v = rex::cvar::GetFlagByName("audio_gain");
      previa = v.empty() ? double(dbz3::settings::MasterVolume()) : std::atof(v.c_str());
      rex::cvar::SetFlagByName("audio_gain", "0");
      REXLOG_INFO("dbz3 pasarela: mezcla de B3 callada mientras dura la escena (audio_gain {:.2f} -> 0)", previa);
    }
  } else if (previa >= 0) {
    rex::cvar::SetFlagByName("audio_gain", std::to_string(previa));
    REXLOG_INFO("dbz3 pasarela: mezcla de B3 devuelta (audio_gain {:.2f})", previa);
    previa = -1;
  }
}
void CallarB3EnEscena(dbz3::cinematica::Escena& e) {   // S1: callar la mezcla de B3 durante la escena y devolverla al acabar
  e.al_empezar_escena = [] { CallarMezclaB3(true); };
  e.al_terminar_escena = [] { CallarMezclaB3(false); };
}

void Salir(bool completo) {   // con g_mu
  DetenerMusica();
  CallarVozB1();
  CallarAdxPs2();
  if (completo) {
    GuardarProgreso(kClave, 0);
    GuardarProgreso("hercule_puntos", 0);
  } else {   // se vuelve al tablero del combate pendiente
    int vuelta = std::min<int>(g_paso, int(g_pasos.size()) - 1);
    while (vuelta > 0 && g_pasos[size_t(vuelta)].tipo == kCombate && g_pasos[size_t(vuelta)].vez > 0) --vuelta;
    if (vuelta > 0 && g_pasos[size_t(vuelta)].tipo == kCombate && g_pasos[size_t(vuelta - 1)].tipo == kCharla) --vuelta;
    GuardarProgreso(kClave, vuelta);
  }
  REXLOG_INFO("dbz3 pasarela: {}", completo ? "leyenda completada" : "fuera del modo (se guarda el avance)");
  g_paso = 0;
  Publicar(kNada);
}

bool g_previa_vista = false;   // S1-F03.1: con g_mu; la escena previa de los Juegos de Cell ya salio en este paso
bool g_pres_vista = false;    // DS30: con g_mu; la cinematica de presentacion de la ronda ya salio en este paso (antes del READY)
bool g_final_vista = false;       // S5-A3: con g_mu; la escena final 3D ya salio
bool g_final_pendiente = false;   // con g_mu; ganada la ultima ronda: Lanzar() abre la escena final 3D (mismo paso)
std::atomic<bool> g_previa_combate{false};   // S1-F03.1: el combate en curso es el "solo escena" (sin puntos en pantalla)
std::atomic<bool> g_previa_negro{false};     // S1-F03.1: tras la escena previa, negro hasta que B3 suelta el combate

void Entrar(int paso) {   // con g_mu
  g_previa_vista = false;
  g_pres_vista = false;
  g_final_pendiente = false;
  if (paso == 0) g_final_vista = false;
  g_paso = paso;
  g_intentos = 0;
  Publicar(paso >= int(g_pasos.size()) ? kFin : kPantalla);
}

// Puntos de un combate ganado como en B1 (0x198E18): los de los sucesos (Fin::puntos, ver Anotar) + LIFE (vida del
// ganador - la del otro) + TIME (segundos que sobran x 20); la leyenda suma la mitad (0x198F00: score / 2).
// EU 0x198E18 SatanMode_EndOfMatchScore (bloque de 0x198C90): LIFE = vida del ganador (+0x5180) - la del otro (+0x4F50)
// y TIME = (limite - reloj)/60 * 20, las dos por 0x1AF6F0 (etiquetas 0x35E6C8 y 0x35E6D0).
// EU 0x198F00 SatanMode_HalfToLegend: con el modo 3, la mitad del marcador del ganador (+0x517C >> 1) va al total del
// modo (0x5ECC5C[0]) y a la casilla de la ronda (0x5ECC5C[ronda+2]).
int PuntosB1(const Fin& f) {
  const int life = f.vida[0] - f.vida[1];   // LIFE puede ser negativo (ring out con menos vida que el rival)
  const int time = f.segundos > 0 ? f.segundos * 20 : 0;
  // 0x1AF6F0 suma cada suceso y deja el marcador en 0 si baja de 0: LIFE primero, luego TIME
  return std::max(0, std::max(0, f.puntos + life) + time) / 2;
}

// Nivel de la IA de B1 por dificultad y retador: la rutina 0x26C400 (caso 3, modo Mr. Satan) pone al luchador el byte
// `fichero_CPU+0x48[dificultad][retador]`; los tres bloques de 11 bytes estan en la entrada 0x176 ("#CPU") de
// DATA_xx.AFS (+0x1E0 Novato, +0x1EB Experto, +0x1F6 Avanzado) y apuntan a las 35 fichas de IA del mismo fichero (0x308 +
// 0x110 n). B1 va de 1 a 12 y sube con el retador. B3 solo deja elegir tres (mgr+35573 = 0..2: su tabla 13+n de la misma
// familia de ficheros, que da el nivel interno de IA 1, 4 y 7, medido en el juego con DBZ3_PASARELA_TRAZA), asi que se
// toma el mas cercano: 1-2 -> 0 (nivel 1), 3-5 -> 1 (nivel 4), 6-12 -> 2 (nivel 7).
constexpr uint8_t kNivelB1[3][11] = {{2, 1, 2, 3, 4, 5, 6, 2, 4, 5, 6},
                                     {3, 2, 3, 4, 6, 7, 9, 3, 6, 7, 10},
                                     {4, 2, 4, 6, 8, 10, 12, 4, 8, 10, 12}};
int NivelComB3(int dificultad, int ronda) {
  const int n = kNivelB1[std::clamp(dificultad, 0, 2)][std::clamp(ronda, 0, 10)];
  return n <= 2 ? 0 : n <= 5 ? 1 : 2;
}

// DS143: las "reglas" de A16 / Trunks / Piccolo son SOLO los avisos de 669 #5/#7/#11 (texto de Mr. Satan); B1 no las
// aplica en el combate: en b1.c no hay ninguna comparacion con los IDs 5 (Trunks), 6 (Piccolo) ni 16 (A16) -- el unico
// caso especial por personaje es Cell Jr. 0x17/forma 3 (0x1a.../0x1ad0d0) y el dano pasa por la formula general
// 0x1AD0D0 sin multiplicadores del modo. Antes el puerto inventaba armadura 0,5 / solo-ki 25 % / ki lento 0,5: se
// quitan para que la pelea sea la de B1. Ki inicial del rival: el de la tabla; el del jugador tambien (+0x2C): lo pone
// Lanzar (300 = 3 barras; 0xFFFF, como la vida de Krilin y Gohan, = el que traia).
// DS13: traje del rival en la Leyenda, por ronda (indice del juego rec+18; 0 = el de serie). B1 y B3 no visten igual a
// todos: Trunks en B1 lleva la ARMADURA saiyan (B3 traje 2), Ten Shin Han va sin camiseta (B3 traje 1) y Vegeta lleva la
// armadura con peto amarillo (B3 traje 1; el 0 de B3 es el mono sin armadura). El resto coincide con su traje de serie
// (comprobado con las fotos de las tomas de B1 de DS11 y el modelo de B3 de cada rival). Indice = r de la tabla de B1.
constexpr int kTrajeRival[11] = {0, 1, 0, 0, 2, 1, 0, 0, 0, 0, 0};   // 0 Yamcha, 1 Tien, 2 Krilin, 3 A16, 4 Trunks, 5 Vegeta...
Reglas ReglasDe(const CombateB1& c, int dificultad, int ronda) {
  Reglas r;
  r.nivel_com = NivelComB3(dificultad, ronda);
  if (const char* f = std::getenv("DBZ3_PASARELA_NIVEL")) r.nivel_com = std::clamp(std::atoi(f), 0, 2);   // pruebas
  r.ki_rival = float(c.ki_rival) / 100.0f;
  r.traje_rival = kTrajeRival[std::clamp(ronda, 0, 10)];
  if (const char* t = std::getenv("DBZ3_DS13_TRAJE_RIVAL")) r.traje_rival = std::atoi(t);   // sonda de pruebas
  return r;
}

// Contrasena de B1 (FUN_00235310). Entradas: el total de puntos, la dificultad (0..2) y el aviso de "el total no cuadra
// con la suma de las 11 rondas" (aqui siempre cuadra: el puerto suma las mitades de cada ronda). Pasos del original:
//  1. dos numeros del LFSR de B1 (FUN_002189a0(0x20): estado = estado<<1 | (estado>>31) ^ ((estado>>4)&1), devuelve
//     estado-1): r2 = &0x1f (elige una de las 16 permutaciones y 4 bits) y r1 = byte;
//  2. campo de 8 B: 4 bits de r2, 8 de r1, 2 de dificultad, 1 del aviso, 19 del total y 19 del complemento
//     ~(r1+total) (FUN_002356a0);
//  3. 2 bits de paridad (FUN_00235a80), permutacion de los 14 nibbles con una de las 16 cadenas de 0x350B70
//     (FUN_00235c50) y 12 simbolos de 5 bits de la tabla 0x35F890.
// Los simbolos del original son sprites 0x10..0x1B del banco 669; aqui se devuelven los 12 caracteres ASCII de la tabla
// (0x1F se sustituye por '?') y se pintan con la fuente del juego. La web de Bandai para la que servia ya no existe.
// EU 0x235310 SatanMode_MakePassword: 12 simbolos de 5 bits de 0x35F890 a partir de total, dificultad y aviso.
std::string ClaveB1(int total, int dificultad) {
  static const char* const kPerm[16] = {"0D862931CB754A", "1405A7C6298BD3", "201856934D7ACB", "3A165C9BD80427",
                                        "45B972D38A6C10", "59DC4781B3A206", "64AB2C13975D80", "71C064B3A5D928",
                                        "85A70D2C4631B9", "9B758463CD01A2", "A41896D075CB23", "BD658A437C2019",
                                        "C1746D2A538B09", "D16B2093A57C84", "08A26D374C51B9", "17D2C936A5840B"};
  static const char kSim[32] = {'[', '_', 'X', '?', 'V', 'm', ' ', 'o', 'Z', '!', 'e', 'g', '&', 'h', 'Y', '\\',
                                'j', 'W', 'l', ']', '`', 'c', '"', 'i', 'a', 'b', '$', 'f', '%', '#', 'k', 'n'};
  uint32_t s = uint32_t(NowMs()) ^ (uint32_t(total) * 2654435761u) ^ (uint32_t(dificultad) << 16) ^ 0x9e3779b9u;
  if (!s) s = 0x12345678u;
  // EU 0x2189A0 Lfsr32Next: estado = estado<<1 | (estado>>31) ^ ((estado>>4)&1); devuelve estado-1.
  auto rnd = [&](int n) {
    for (int i = 0; i < n; ++i) s = (s << 1) | ((s >> 31) ^ ((s >> 4) & 1u));
    return int(s - 1u);
  };
  const uint32_t r2 = uint32_t(rnd(0x20)) & 0x1fu;
  const uint32_t r1 = uint32_t(rnd(0x20)) & 0xffu;
  const uint64_t p3 = uint64_t(uint32_t(std::max(0, total))), p2 = uint64_t(r1), u2 = ~(p2 + p3);
  uint8_t b[8] = {};
  auto or8 = [&](int i, uint64_t v) { b[i] = uint8_t(b[i] | uint8_t(v)); };
  // EU 0x235610 SatanPass_SetLowPerm: *b |= (r2 & 0xf) << 4.
  or8(0, uint64_t(r2 & 0xf) << 4);
  // EU 0x235630 SatanPass_SetByte: nibble alto de r1 en b[0] (bits 0-3) y nibble bajo en b[7] (bits 4-7).
  or8(0, (r1 >> 4) & 0xf);
  or8(7, uint64_t(r1 & 0xf) << 4);
  // EU 0x235660 SatanPass_SetDifficulty: 2 bits de la dificultad en b[1] (bits 6-7).
  or8(1, uint64_t(dificultad & 3) << 6);
  // EU 0x235680 SatanPass_SetMismatch: 1 bit de "el total no cuadra" en b[6] (bit 5).
  or8(6, uint64_t(0) << 5);   // el puerto siempre cuadra el total (0x233FF0 compara la suma de las 11 rondas)
  // EU 0x2356A0 SatanPass_PutTotal: 19 bits del total y 19 del complemento ~(r1+total), bit a bit.
  // (La escritura del bit del aviso, 0x235680, va justo arriba con el 0 fijo.)

  or8(1, (p3 & 0x80000) >> 14); or8(1, (p3 & 0x40000) >> 15); or8(1, (p3 >> 16) & 2);
  or8(2, (p3 & 0x10000) >> 10); or8(2, (p3 & 0x8000) >> 11); or8(2, (p3 & 0x4000) >> 12); or8(2, (p3 & 0x2000) >> 13);
  or8(3, (p3 & 0x1000) >> 5); or8(3, (p3 & 0x800) >> 6); or8(3, (p3 & 0x400) >> 7); or8(3, (p3 >> 8) & 2);
  or8(4, (p3 & 0x100) >> 2); or8(4, (p3 & 0x80) >> 3); or8(4, (p3 & 0x40) >> 4); or8(4, (p3 & 0x20) >> 5);
  {
    const uint32_t u1 = uint32_t((p3 << 44) >> 32);
    or8(5, uint64_t(((u1 >> 12) & 0x10) << 3)); or8(5, uint64_t(((u1 >> 12) & 8) << 2));
    or8(5, uint64_t(((u1 >> 12) & 4) << 1)); or8(5, p3 & 2); or8(6, uint64_t(((u1 >> 12) & 1) << 6));
  }
  or8(1, (u2 & 0x80000) >> 15); or8(1, (u2 >> 16) & 4); or8(1, (u2 & 0x20000) >> 17);
  or8(2, (u2 & 0x10000) >> 9); or8(2, (u2 & 0x8000) >> 10); or8(2, (u2 & 0x4000) >> 11); or8(2, (u2 & 0x2000) >> 12);
  or8(3, (u2 & 0x1000) >> 6); or8(3, (u2 & 0x800) >> 7); or8(3, (u2 >> 8) & 4); or8(3, (u2 & 0x200) >> 9);
  or8(4, (u2 & 0x100) >> 1); or8(4, (u2 & 0x80) >> 2); or8(4, (u2 & 0x40) >> 3); or8(4, (u2 & 0x20) >> 4);
  {
    const uint32_t u1 = uint32_t((u2 << 44) >> 32);
    or8(5, uint64_t(((u1 >> 12) & 0x10) << 2)); or8(5, uint64_t(((u1 >> 12) & 8) << 1));
    or8(5, u2 & 4); or8(5, (u2 & 2) >> 1); or8(6, uint64_t(((u1 >> 12) & 1) << 7));
  }
  // EU 0x235A80 SatanPass_AddParity: cuenta los bits de los 8 B y completa 1 o 2 bits de b[6] (mod 3 del LFSR).
  int bits = 0;
  for (uint8_t v : b) for (int k = 0; k < 8; ++k) bits += (v >> k) & 1;
  const int par = bits & 3;
  if (par == 3) {
    const int r = rnd(0x20) % 3;
    or8(6, r == 2 ? 1 : r == 1 ? 2 : 4);
  } else if (par == 2) {
    const int r = rnd(0x20) % 3;
    or8(6, r == 2 ? 3 : r == 1 ? 5 : 6);
  } else if (par == 1) {
    or8(6, 7);
  }
  // EU 0x235C50 SatanPass_PermuteNibbles: permutacion de 14 nibbles con una de las 16 cadenas de 0x350B70.
  uint8_t src[8];
  std::memcpy(src, b, 8);
  std::memset(b, 0, 8);
  const char* perm = kPerm[r2 & 0xf];
  for (int i = 0; i < 14; ++i) {
    const int c = perm[i] < 'A' ? perm[i] - '0' : perm[i] - 'A' + 10;
    const int idx = c + 1;
    const int nib = (idx & 1) == 0 ? (src[idx >> 1] >> 4) & 0xf : src[idx >> 1] & 0xf;
    if (((i + 1) & 1) == 0) b[(i + 1) >> 1] = uint8_t(b[(i + 1) >> 1] | (nib << 4));
    else b[(i + 1) >> 1] = uint8_t(b[(i + 1) >> 1] | nib);
  }
  // EU 0x235C50 SatanPass_PermuteNibbles: ultima linea del original, `*pbVar4 = *pbVar4 | abStack_8[0] & 0xf0`
  // (pbVar4 sigue apuntando a b[0] al salir del bucle): el nibble alto de r2 que 0x235610 dejo en b[0] vuelve al
  // nibble alto de b[0], que los 14 nibbles no tocan. Faltaba: los simbolos 0 y 1 (que leen b[0]) salian mal.
  b[0] = uint8_t(b[0] | (src[0] & 0xf0));
  // 12 simbolos de 5 bits (FUN_00235310, ultimo bloque).
  char out[13];
  out[0] = kSim[b[0] >> 3];
  out[1] = kSim[((b[0] & 7) << 2) | (b[1] >> 6)];
  out[2] = kSim[(b[1] >> 1) & 0x1f];
  out[3] = kSim[((b[1] & 1) << 4) | (b[2] >> 4)];
  out[4] = kSim[((b[2] & 0xf) << 1) | (b[3] >> 7)];
  out[5] = kSim[(b[3] >> 2) & 0x1f];
  out[6] = kSim[((b[3] & 3) << 3) | (b[4] >> 5)];
  out[7] = kSim[b[4] & 0x1f];
  out[8] = kSim[b[5] >> 3];
  out[9] = kSim[((b[5] & 7) << 2) | (b[6] >> 6)];
  out[10] = kSim[(b[6] >> 1) & 0x1f];
  out[11] = kSim[((b[6] & 1) << 4) | (b[7] >> 4)];
  out[12] = 0;
  return std::string(out);
}

// s9b: tomas del original de B1 (escena 0x1C, medidas con capturas de PCSX2 a camara lenta, D:\tmp\s9b_satan\capturas).
// Camara en el marco de los luchadores (Paso::rel): origen = punto medio, x hacia el lado 2, y = altura sobre el suelo, z
// = a su izquierda; Mr. Satan esta en x = -15 y el retador en x = +15. cam_ojo/cam_obj = {cuadro, x, y, z}; 60 cuadros/s.
void Toma(dbz3::cinematica::Paso& p, std::array<float, 3> o0, std::array<float, 3> b0, std::array<float, 3> o1,
          std::array<float, 3> b1) {
  const float f = p.segundos * 60.0f;
  p.rel = true;
  p.cam_ojo = {{0.0f, o0[0], o0[1], o0[2]}, {f, o1[0], o1[1], o1[2]}};
  p.cam_obj = {{0.0f, b0[0], b0[1], b0[2]}, {f, b1[0], b1[1], b1[2]}};
}
// Las dos tomas cortas de cada ronda antes del "READY?": Mr. Satan desde abajo con los brazos abiertos (B1: camara a ras
// de suelo, el cuerpo entero con las rodillas y la cabeza casi en los bordes, sube un poco) y el retador de cerca.
// FUN_001920b0 (B1) es la camara de las dos tomas (define la rampa con el contador +0x1f0 hasta 186; el corte medido en
// las 10 rondas de DS11 cae a los 180, 3,00 s, asi que los 6 ultimos cuadros no se ven). Ojo alrededor del luchador:
//   theta = 1,8849558 * t / 206
//   ojo   = pies + (lado * 20 * sin(theta), 4 + 10 * t / 186, 20 * cos(theta))
//   fov   = (35 + 35 * t / 186) grados = 0,611 -> 1,202 rad vertical (DS11 midio la misma rampa en 0x5751D0+0x44)
//   objetivo = lerp(cabeza, cintura, t/186), suavizado 0,015 por cuadro (FUN_001553d0); las tablas kReadyObj_* son
//              esa recta cabeza->cintura medida por rival.
// lado = +1 en la toma 1 (Satan, x = -15) y -1 en la 2 (el retador, x = +15). Las claves medidas de DS11 coinciden
// con esta formula dentro de 0,56 u (peor caso, toma 2 en el cuadro 24). La camara es del modo, no del retador: la tabla
// 0x35DFC0 del ELF pone a los dos en x = -15/+15 y el escenario 14 (CHG_CELL RING) en los 11 registros (leido: 11/11),
// asi que las 11 rondas usan la misma toma. El escenario de B1 no se envia: los combates y las tomas van en el ring de
// Cell de B3 (mapa 50, registro 8).
constexpr float kFovVerticalB1 = 0.611f;   // A3b: el 0,611 rad del #AMC es el fov HORIZONTAL (4:3): vertical = 2 atan(tan(0,3055) * 3/4) = 0,45 (confirmado a ojo con las capturas pareadas: tamano de la cabeza de Satan)
constexpr float kFovB1 = 0.583f;   // 2 atan(0,3), medido en S1-F03
// DS186: cabeza de Cell de B1 sobre su cintura: #AMO de DATA_EN 331 (THL_HEAD 6,5458 - THL_WAIST -0,9718 = 7,518).
// La toma 2 de la previa (Cell de cerca, G 600..950 del #AMC 2372/20) apunta al cuerpo de Cell con el `y` del #AMC
// (22,817 en el mundo de B1) y el B3 lo dibuja con la pose de la escena; sin el ajuste al cuerpo dibujado solo asomaba
// la coronilla por el borde inferior (c024 de la captura de DS185). Lo aplica cinematica.cpp (objetivo_cuerpo_b3).
constexpr float kB1CellCabeza = 7.518f;
// DS11: las dos tomas del "READY?" usan la camara de COMBATE de B1 (0x5751D0), cuyo fov vertical (0x5751D0+0x44) abre
// de 0,611 a ~1,20 durante cada toma (medido en el PCSX2 de la copia propia; no es el 0,583 constante de la escena previa).
// EU 0x1920B0 SatanMode_ReadyShotCamera: ojo de la toma `cuadro` (0..180, 60 Hz) para el lado 1 (Satan) o el 2
// (retador): th = 1,8849558*t/206; ojo = pies + (lado*20*sin(th), 4 + 10*t/186, 20*cos(th)); fov = (35+35*t/186) grados.
std::array<float, 3> OjoTomaB1(int cuadro, bool lado2) {
  const float t = float(cuadro), th = 1.8849558f * t / 206.0f;
  return {float(lado2 ? 15 : -15) + (lado2 ? -20.0f : 20.0f) * std::sin(th), 4.0f + 10.0f * t / 186.0f,
          20.0f * std::cos(th)};
}
std::vector<std::array<float, 4>> OjosTomaB1(bool lado2) {
  std::vector<std::array<float, 4>> v;
  for (int c = 0; c <= 180; c += 6) {   // 31 claves; el juego interpola entre ellas
    const auto o = OjoTomaB1(c, lado2);
    v.push_back({float(c), o[0], o[1], o[2]});
  }
  return v;
}
// Objetivo por retador (r = indice de la tabla de B1, 1..10): el ojo es el mismo en todas (error <= 0,5 u entre las 8 rondas
// medidas, 22/10, S5-A3) pero el objetivo sigue la cabeza del luchador. r = 0 (Yamcha) y r = 2 (Krilin) medidas en la ola 3 (ojo igual, error <= 0,5 u).
constexpr std::array<float, 4> kReadyObj_1_1[] = {{0.0f, -17.1f, 15.0f, 1.6f}, {103.2f, -17.3f, 12.9f, 1.4f}, {178.8f, -15.5f, 8.8f, 1.0f}};
constexpr std::array<float, 4> kReadyObj_1_2[] = {{0.0f, 13.4f, 12.6f, -0.5f}, {25.2f, 13.1f, 12.4f, -0.5f}, {69.6f, 11.6f, 11.5f, 0.1f}, {122.4f, 13.1f, 11.0f, -0.0f}, {177.6f, 12.7f, 8.5f, 0.4f}};
constexpr std::array<float, 4> kReadyObj_3_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, -0.0f}};
constexpr std::array<float, 4> kReadyObj_3_2[] = {{0.0f, 16.9f, 17.6f, -1.4f}, {69.6f, 16.9f, 16.6f, -1.3f}, {177.6f, 16.9f, 12.9f, -0.9f}};
constexpr std::array<float, 4> kReadyObj_4_1[] = {{0.0f, -17.1f, 15.0f, 1.6f}, {103.2f, -17.3f, 12.9f, 1.4f}, {178.8f, -15.5f, 8.8f, 1.0f}};
constexpr std::array<float, 4> kReadyObj_4_2[] = {{0.0f, 14.0f, 10.3f, -0.6f}, {26.4f, 13.7f, 10.1f, -0.6f}, {70.8f, 12.1f, 9.3f, -0.1f}, {122.4f, 13.4f, 8.8f, -0.2f}, {177.6f, 12.9f, 6.6f, 0.2f}};
constexpr std::array<float, 4> kReadyObj_5_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, -0.0f}};
constexpr std::array<float, 4> kReadyObj_5_2[] = {{0.0f, 17.2f, 12.3f, 0.4f}, {49.2f, 16.7f, 11.8f, 0.2f}, {98.4f, 15.4f, 9.8f, -0.1f}, {177.6f, 14.9f, 6.8f, -0.2f}};
constexpr std::array<float, 4> kReadyObj_6_1[] = {{0.0f, -17.1f, 15.0f, 1.6f}, {105.6f, -17.2f, 12.8f, 1.4f}, {178.8f, -15.5f, 8.8f, 1.0f}};
constexpr std::array<float, 4> kReadyObj_6_2[] = {{0.0f, 16.6f, 15.9f, 0.7f}, {74.4f, 16.6f, 14.7f, 0.7f}, {177.6f, 16.2f, 11.5f, 0.1f}};
constexpr std::array<float, 4> kReadyObj_7_1[] = {{0.0f, -17.0f, 15.0f, 1.6f}, {105.6f, -17.2f, 12.8f, 1.4f}, {178.8f, -15.5f, 8.8f, 1.0f}};
constexpr std::array<float, 4> kReadyObj_7_2[] = {{0.0f, 16.2f, 8.0f, 0.1f}, {177.6f, 16.1f, 5.3f, -0.2f}};
constexpr std::array<float, 4> kReadyObj_8_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, -0.0f}};
constexpr std::array<float, 4> kReadyObj_8_2[] = {{0.0f, 17.1f, 13.5f, -1.6f}, {103.2f, 17.3f, 11.8f, -1.4f}, {177.6f, 15.6f, 8.2f, -0.9f}};
constexpr std::array<float, 4> kReadyObj_9_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, -0.0f}};
constexpr std::array<float, 4> kReadyObj_9_2[] = {{0.0f, 16.8f, 8.5f, -1.1f}, {104.4f, 17.0f, 7.2f, -1.0f}, {177.6f, 15.9f, 4.7f, -0.8f}};
constexpr std::array<float, 4> kReadyObj_10_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, -0.0f}};
constexpr std::array<float, 4> kReadyObj_10_2[] = {{0.0f, 17.2f, 16.6f, -1.6f}, {105.6f, 17.5f, 14.5f, -1.3f}, {177.6f, 15.8f, 10.4f, -0.9f}};
constexpr std::array<float, 4> kReadyObj_0_1[] = {{0.0f, -15.3f, 14.6f, 0.4f}, {70.8f, -15.3f, 13.5f, 0.1f}, {178.8f, -14.6f, 10.0f, 0.0f}};
constexpr std::array<float, 4> kReadyObj_0_2[] = {{0.0f, 13.7f, 11.1f, -0.7f}, {25.2f, 13.5f, 11.0f, -0.7f}, {70.8f, 11.9f, 10.1f, -0.1f}, {122.4f, 13.3f, 9.7f, -0.3f}, {177.6f, 12.9f, 7.4f, 0.2f}};
constexpr std::array<float, 4> kReadyObj_2_1[] = {{0.0f, -17.0f, 15.0f, 1.6f}, {104.4f, -17.2f, 12.8f, 1.4f}, {178.8f, -15.4f, 8.7f, 1.0f}};
constexpr std::array<float, 4> kReadyObj_2_2[] = {{0.0f, 16.7f, 9.3f, -1.8f}, {103.2f, 16.9f, 8.0f, -1.6f}, {176.4f, 15.4f, 5.3f, -1.1f}};
struct ObjReady { const std::array<float, 4>* a; size_t n; };
#define DBZ3_OBJ(r, t) ObjReady{kReadyObj_##r##_##t, std::size(kReadyObj_##r##_##t)}
const ObjReady kObjReady[11][2] = {
    {DBZ3_OBJ(0, 1), DBZ3_OBJ(0, 2)}, {DBZ3_OBJ(1, 1), DBZ3_OBJ(1, 2)}, {DBZ3_OBJ(2, 1), DBZ3_OBJ(2, 2)}, {DBZ3_OBJ(3, 1), DBZ3_OBJ(3, 2)},
    {DBZ3_OBJ(4, 1), DBZ3_OBJ(4, 2)}, {DBZ3_OBJ(5, 1), DBZ3_OBJ(5, 2)}, {DBZ3_OBJ(6, 1), DBZ3_OBJ(6, 2)}, {DBZ3_OBJ(7, 1), DBZ3_OBJ(7, 2)},
    {DBZ3_OBJ(8, 1), DBZ3_OBJ(8, 2)}, {DBZ3_OBJ(9, 1), DBZ3_OBJ(9, 2)}, {DBZ3_OBJ(10, 1), DBZ3_OBJ(10, 2)}};
#undef DBZ3_OBJ
// Voces de las dos tomas (ADX_JP.AFS; MEDIDAS con el gancho de 0x1369D0, canal 1 = Mr. Satan, canal 2 = el retador, 22/10):
// {entrada de Mr. Satan, ms desde el principio de la toma 1, entrada del retador, ms desde el principio de la toma 2}.
// Las 10 rondas 2-11 estan medidas (DS11). La fila 0 (Yamcha) es la unica sin volver a medir: B1 no le da charla ni
// tablero (la tabla 0x3507F0 del ELF tiene 16 entradas, 1269-1284, para r=1..10) y sus dos tomas van al final de la
// escena larga de los Juegos de Cell. Se conservan 1303/1516 de la primera sonda [VERIFICAR en pantalla: que la voz de
// la toma 2 sea la de Yamcha].
constexpr int kVozReady[11][4] = {{1303, 300, 1516, 30}, {1302, 1370, 1477, 1260}, {1307, 1580, 662, 1440}, {1299, 1670, 15, 1770},
                                  {1302, 1330, 1456, 1130}, {1303, 320, 1493, 330}, {1307, 1600, 1160, 830},
                                  {1302, 1330, 103, 750}, {1299, 1680, 614, 1420}, {1303, 320, 569, 1510}, {1299, 1730, 93, 2040}};
// DS13: RE de la pose de B1 en las tomas del READY. La animacion NO es la entrada de B3: es el codigo 0xC0 de B1 (lado 1,
// Mr. Satan) y el 0xC1 (lado 2, el retador). Medido (PCSX2 propio, ronda 2, 10/2026, PINE): el clip de Satan es el anim 198
// (180 cuadros, 60 Hz; encaja con la pose medida a 0,21 u de error) y el de Tien el 156 (1,79 u). B3 no trae ese clip: se han
// comparado sus 107 clips propios y los 359 del almacen global (Pose::guard) y el mejor queda a 1,37-2,9 u. Se elige por
// retador el codigo de B3 con la pose mas cercana a la de B1 en los primeros 1,2 s de la toma (medido con las posiciones
// de los huesos de los dos esqueletos, alineadas en guinada). -1 = su guardia (es lo mas cerca que queda en Trunks).
// Banco de animacion de B1 que usa cada toma (registro de personaje del ELF, fichero 0x259390 + 564*id: +0x140 banco de
// animacion, +0x150 modelo, +0x154 #BSK; entradas de DATA_EN.AFS): Satan 2358, Yamcha 2528, Tien 2445, Krilin 1297,
// A16 0 (sin banco propio), Trunks 2415, Vegeta 2486, Piccolo 1761, Cell Jr. 358, Goku 1213, Gohan 1187, Cell 326.
// La toma 1 es el clip 198 de 2358 (codigo 0xC0) y la 2 el clip que B1 da al 0xC1 en el banco del rival (Tien: 156 de
// 2445); el puerto no puede arrancar esos bancos, asi que usa la tabla de abajo.
// Indice = r de la tabla de B1 (0 Yamcha ... 10 Cell). Valores: 0x280/0x281 = entrada del lado 2/lado 1, 0x2A0 = victoria.
constexpr int kPoseRival[11] = {0x280, 0x280, 0x2A0, 0x2A0, -1, 0x280, 0x281, 0x280, 0x280, 0x280, 0x2A0};
void TomasPresentacion(dbz3::cinematica::Escena& e, int r = 1) {
  namespace C = dbz3::cinematica;
  C::Paso p;
  p.plano = C::kLado1;
  p.segundos = 3.0f;
  static const int pose1 = [] {   // DS13: sonda para comparar poses sin compilar (DBZ3_DS13_POSE1=<code>, -1 = guardia)
    const char* v = std::getenv("DBZ3_DS13_POSE1");
    return v ? int(std::strtol(v, nullptr, 0)) : int(C::kPoseEntrada1);
  }();
  static const int pose2_forzada = [] {   // DS13: idem para el retador (DBZ3_DS13_POSE2); sin variable, la tabla de abajo
    const char* v = std::getenv("DBZ3_DS13_POSE2");
    return v ? int(std::strtol(v, nullptr, 0)) : 0x7FFFFFFF;
  }();
  // EU 0x19BCB0 SatanBattle_PoseBoth: aplica la pose a los dos luchadores (0x19C150 reinicia cada uno y 0x1AA700
  // mezcla la pose con peso 1.0). Aqui la hace el paso de la escena: p.anim[] elige la pose B3 equivalente del
  // personaje y p.brazos_b1 compone los brazos medidos de B1; se aplica a los dos lados.
  // EU 0x19C150 SatanBattle_ResetFighter: deja el luchador en su estado inicial (posicion, vida, ki y pose) antes
  // de la toma; aqui, el C::Paso nuevo con su anim/posicion hace ese reinicio.
  // EU 0x1AA700 SatanBattle_BlendPose: aplica la pose con el peso `param_1` (1.0 = entera); aqui, p.anim sin mezcla
  // (peso 1.0) para el lado que habla.
  p.anim[0] = pose1;   // DS13: el gesto de B1 (brazos abiertos, manos arriba y guardia) es el mas cercano de B3 a 0x281
  p.brazos_b1 = true;   // DS16: los brazos de la toma 1 son los de B1 (direcciones medidas), compuestos sobre el clip de B3
  p.rel = true;
  p.estilo_b1 = true;   // DS11: B1 ensena estas dos tomas a imagen entera (sin bandas de cine)
  p.sin_ayuda = true;   // DS13: B1 no muestra el rotulo "A Siguiente / START Saltar" en las tomas
  static const std::vector<std::array<float, 4>> ojo1 = OjosTomaB1(false);   // FUN_001920b0, lado 1 (Satan)
  p.cam_ojo = ojo1;
  const ObjReady& o1 = kObjReady[std::clamp(r, 0, 10)][0];
  p.cam_obj.assign(o1.a, o1.a + o1.n);
  // Sin el ajuste provisional de LUNA20 (ojo Y-1,2/Z+14 y objetivo X+4): FUN_001920b0 da la camara exacta y las
  // claves medidas de DS11 la confirman. El encuadre de la toma 1 y el de la 2 son el mismo tipo de orbita.
  p.cam_fov = {{0.0f, 0.610865f}, {180.0f, 1.2025f}};   // FUN_001920b0: fov = (35 + 35*t/186) grados; a 180 cuadros 1,2025 rad
  if (!e.iso_voces.empty()) p.voces = {-kVozReady[std::clamp(r, 0, 10)][1], kVozReady[std::clamp(r, 0, 10)][0]};
  e.pasos.push_back(p);
  p = C::Paso{};
  p.plano = C::kLado2;
  p.segundos = 3.0f;
  p.rel = true;
  p.estilo_b1 = true;   // DS11: como la toma 1
  p.sin_ayuda = true;   // DS13: B1 no muestra el rotulo en las tomas
  // EU 0x19BCB0 SatanBattle_PoseBoth / EU 0x19C150 / EU 0x1AA700: misma aplicacion en el lado 2 (0x19c150(iVar1+0x6830)
  // y 0x1aa700(1.0, iVar1+0x6830)); aqui es la pose B3 del retador con peso 1.0.
  p.anim[1] = pose2_forzada != 0x7FFFFFFF ? pose2_forzada : kPoseRival[std::clamp(r, 0, 10)];   // DS13: la pose de B1 de este retador (0xC1 de B1) frente a las de B3
  static const std::vector<std::array<float, 4>> ojo2 = OjosTomaB1(true);   // FUN_001920b0, lado 2 (el retador)
  p.cam_ojo = ojo2;
  const ObjReady& o2 = kObjReady[std::clamp(r, 0, 10)][1];
  p.cam_obj.assign(o2.a, o2.a + o2.n);
  p.cam_fov = {{0.0f, 0.610865f}, {180.0f, 1.2025f}};   // FUN_001920b0, igual que la toma 1
  if (!e.iso_voces.empty()) p.voces = {-kVozReady[std::clamp(r, 0, 10)][3], kVozReady[std::clamp(r, 0, 10)][2]};
  e.pasos.push_back(p);
  // DS13: tramo FIJO de B1 entre la toma 2 y el "READY?" (2,64 s, medido: la toma 2 acaba a 6,01 s y el READY llega a
  // 8,65 s). La camara de B1 (0x5751D0) se queda quieta en la ultima clave de la toma 2 (ojo -4,68/17,55/-0,90; objetivo
  // 12,69/12,39/0,45) y se ve un plano ancho de los dos en guardia porque B1 los ha llevado al centro del ring. El puerto
  // los deja a +-15: con esa camara solo sale el retador de cerca, asi que se usa el plano general de la escena, de lado
  // (los dos de perfil), con los dos vueltos a la guardia.
  p = C::Paso{};
  p.plano = C::kGeneral;
  p.segundos = 2.65f;
  p.rel = true;
  p.estilo_b1 = true;
  p.sin_ayuda = true;
  // DS16: B1 los lleva al centro del ring en este tramo (el READY los pilla juntos, no a +-15). DS30: ese paseo salia
  // erratico en el puerto (la separacion se recolocaba cada cuadro desde la posicion viva de los dos y el motor tambien
  // los movia) y el usuario pide dejarlos quietos en su pose hasta que empiece la pelea, como B1: no se mueve a nadie;
  // siguen a la distancia de la escena (30 u) hasta el READY.
  p.anim[0] = p.anim[1] = 0;   // DS13: los dos vueltos a la guardia (0x2A0 puede quedarse en bucle y B1 aqui esta en guardia)
  // plano de lado como el de B1 (los dos de perfil, Satan a la izquierda): +z = al lado de los luchadores, +x = hacia el rival
  p.cam_ojo = {{0.0f, 0.0f, 10.0f, 50.0f}};
  p.cam_obj = {{0.0f, 0.0f, 5.0f, 0.0f}};
  e.pasos.push_back(p);
}
// DS143: camara CRUDA de un #AMC de B1 (mismo formato que el del final, sin el cambio de marco): {cuadro, x, y, z}
// tal cual la guarda el juego, y corte de cada clip. La usa la escena larga de la ronda 1 (DATA_EN 2372 hijo 20):
// 11 clips de 600/350/230/850/400/890/160/340/660/620/222 cuadros (5322 = 88,70 s). Se lee en marcha de la ISO.
struct CamaraCruda {
  std::vector<std::array<float, 4>> ojo, obj;   // {cuadro global, x, y, z} en el mundo de B1
  std::vector<std::array<float, 2>> fov;        // {cuadro global, rad}
  std::vector<int> corte;                       // cuadro global en que EMPIEZA cada clip (corte.size() = clips + 1)
  int cuadros = 0;
};
bool LeerCamaraCruda(const std::vector<uint8_t>& paq, int hijo, CamaraCruda& c) {
  const std::vector<uint8_t> amc = HijoAmb(paq, uint32_t(hijo));
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amc.size()) std::memcpy(&v, &amc[o], 4); return v; };
  auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= amc.size()) std::memcpy(&v, &amc[o], 4); return v; };
  if (amc.size() < 0x40 || std::memcmp(amc.data(), "#AMC", 4) != 0) return false;
  const uint32_t ncl = u32(0x10), tbl = u32(0x14);
  int g0 = 0;
  c.corte.push_back(0);
  for (uint32_t k = 0; k < ncl && k < 32; ++k) {
    const uint32_t nf = u32(tbl + 16 * k + 8), off = u32(tbl + 16 * k + 12);
    if (off && nf > 2) {
      auto pista = [&](uint32_t idx, int dim, auto&& poner) {
        const uint32_t p = u32(off + 4 * idx);
        if (!p) return;
        const uint32_t n = std::min<uint32_t>(u32(p + 8), 8192);
        for (uint32_t i = 0; i < n; ++i) {
          const size_t o = p + 12 + size_t(4 * (1 + dim)) * i;
          poner(g0 + int(u32(o)), o + 4);
        }
      };
      // clave = {cuadro, x, y, z} (verificado con el #AMC del final: el 2.o float es x, el 3.o y, el 4.o z)
      pista(0, 3, [&](int f, size_t o) { c.ojo.push_back({float(f), f32(o), f32(o + 4), f32(o + 8)}); });
      pista(1, 3, [&](int f, size_t o) { c.obj.push_back({float(f), f32(o), f32(o + 4), f32(o + 8)}); });
      pista(3, 1, [&](int f, size_t o) { c.fov.push_back({float(f), f32(o)}); });
    }
    g0 += int(nf);
    c.corte.push_back(g0);
  }
  c.cuadros = g0;
  return !c.ojo.empty() && !c.obj.empty();
}
// DS143: trayectoria de un actor en un #AMM de B1: la pista de POSICION del hueso `hueso` (WAIST, indice 1) lleva su
// sitio en el mundo. Se devuelve {cuadro, x, y, z} con y ya en el suelo del luchador para escribirla como posicion
// absoluta del luchador de B3.
// DS145: el suelo se obtiene restando la altura de la cadera del MODELO DE B3 de ese personaje (bind, medida en el
// propio juego: Goku bin 264 = 10,45; Cell bin 147 = 15,49; Satan bin 377 = 11,44) y no 10,0 para todos. Con 10,0 Cell
// quedaba 6,6 u por encima del suelo de B3 (su cadera de B1 mide 16,63 porque es alto, no porque este sobre una
// plataforma): la camara de sus tomas salia 6,6 u alta. Con la altura real, la cadera de Cell cae a 1,14 del suelo de
// B3, Goku a -0,92 y Satan a -0,99 (los tres ~1 u, el margen de la pose), que es el encuadre de B1.
struct TrayectoriaB1 {
  // clips[c] = claves {cuadro LOCAL al clip, x, y, z} de la pista de posicion del hueso
  std::vector<std::vector<std::array<float, 4>>> clips;
  float suelo = 10.45f;   // altura de la cadera del modelo de B3 que se ha restado al `y` (para volver a la cintura de B1)
};
bool LeerTrayectoriaB1(const std::vector<uint8_t>& amm, const char* hueso, TrayectoriaB1& t) {
  if (amm.size() < 0x20 || std::memcmp(amm.data(), "#AMM", 4) != 0) return false;
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amm.size()) std::memcpy(&v, &amm[o], 4); return v; };
  const uint32_t nb = u32(0x18), nom = u32(0x1C), nc = u32(0x10), tbl = u32(0x14);
  int wi = -1;
  for (uint32_t j = 0; j < nb && size_t(nom) + 32 * (j + 1) <= amm.size(); ++j)
    if (std::strncmp(reinterpret_cast<const char*>(&amm[nom + 32 * j]), hueso, 32) == 0) { wi = int(j); break; }
  if (wi < 0) return false;
  t.clips.resize(nc);
  for (uint32_t k = 0; k < nc; ++k) {
    const size_t e = tbl + 16 * size_t(k);
    const uint32_t fl = u32(e), off = u32(e + 12), per = fl & 0x10 ? 3 : 2;
    const uint32_t p = u32(off + 4 * (per * uint32_t(wi) + 1));
    if (p) {
      const uint32_t n = std::min<uint32_t>(u32(p + 8), 4096);
      for (uint32_t i = 0; i < n; ++i) {
        const size_t o = p + 12 + 16 * size_t(i);
        // clave = {cuadro, x, y, z} (mismo orden que el #AMC); y = cadera - 10,0 (suelo del luchador)
        float x, y, z;
        std::memcpy(&x, &amm[o + 4], 4);
        std::memcpy(&y, &amm[o + 8], 4);
        std::memcpy(&z, &amm[o + 12], 4);
        // DS145: altura de la cadera del modelo de B3 (bin) sobre la suela, medida de sus vertices de bind.
        float suelo = 10.45f;   // GOK_WAIST (B3 bin 264: y minimo -10,45)
        if (std::strncmp(hueso, "THL_WAIST", 32) == 0) suelo = 15.49f;      // Cell (bin 147: y minimo -15,49)
        else if (std::strncmp(hueso, "STN_WAIST", 32) == 0) suelo = 11.44f;   // Satan (bin 377: y minimo -11,44)
        t.suelo = suelo;   // DS178: se guarda para poder volver a la cintura de B1 (y + suelo) en las tomas nuevas
        t.clips[k].push_back({float(u32(o)), x, y - suelo, z});
      }
    }
  }
  return !t.clips.empty();
}

// Juegos de Cell (ronda 1): en B1 la escena 0x1C dura ~60 s con Cell, Goku, el reportero, el coche y Mr. Satan. El motor
// de B3 solo tiene los dos luchadores del combate (Mr. Satan y Yamcha): aqui van las tomas de Mr. Satan y del ring (sin
// Cell, Goku, reportero ni coche) con los subtitulos de B1 resumidos.
void EscenaCell(dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  C::Paso p;   // S1-F03.1: el ring de lejos va ahora en la escena previa (EscenaPrevia)
  // Mr. Satan de espaldas, con la mochila, de cara al ring
  p.plano = C::kLado1;
  p.segundos = 6.66f;   // L04: corte 39,822-46,482 de la traza B1
  p.linea = T::T("¡Ha llegado el salvador de la Tierra, Mr. Satán!", "The Earth's savior, Mr. Satan, has arrived!");
  Toma(p, {-47.0f, 9.5f, 2.5f}, {15.0f, 8.5f, 0.0f}, {-43.0f, 9.0f, 2.0f}, {15.0f, 8.5f, 0.0f});
  e.pasos.push_back(p);
  p = C::Paso{};   // plano que sube de las botas a la cara
  p.plano = C::kLado1;
  p.segundos = 14.84f;   // L04: corte 46,482-61,322 de la traza B1
  p.linea = T::T("¡Por fin, por fin ha aparecido!", "At last, at last he's shown up!");
  Toma(p, {-6.0f, 1.2f, 8.0f}, {-15.0f, 3.0f, 0.0f}, {-6.0f, 1.2f, 8.0f}, {-15.0f, 13.0f, 0.0f});
  e.pasos.push_back(p);
  p = C::Paso{};   // L04: plano ancho desde detras de Mr. Satan hacia el retador (61,322-63,982, 2,66 s)
  p.plano = C::kLado1;
  p.segundos = 2.66f;
  Toma(p, {-31.4f, 12.5f, -36.7f}, {15.0f, 12.0f, 0.0f}, {-21.4f, 12.2f, -33.3f}, {15.0f, 12.0f, 0.0f});
  e.pasos.push_back(p);
  p = C::Paso{};   // primer plano, primera frase
  p.plano = C::kLado1;
  p.segundos = 5.66f;   // L04: corte 63,982-69,642 de la traza B1
  p.cara = kJugador;
  p.linea = T::T("¡Je, je, je! Qué ganas de un rival de verdad...", "Heh heh! I can't wait for a real opponent...");
  Toma(p, {-8.0f, 12.8f, 8.0f}, {-15.0f, 12.6f, 0.0f}, {-10.0f, 12.8f, 7.0f}, {-15.0f, 12.6f, 0.0f});
  e.pasos.push_back(p);
  p = C::Paso{};   // y la segunda, con la mano
  p.plano = C::kLado1;
  p.segundos = 5.34f;   // L04: corte 69,642-74,982 de la traza B1
  p.cara = kJugador;
  p.linea = T::T("Os acabo a todos a la vez. ¡Venid a por mí!", "I'll take you all on at once. Come and get it!");
  Toma(p, {-9.0f, 12.6f, 8.0f}, {-15.0f, 12.4f, 0.0f}, {-11.0f, 12.6f, 7.0f}, {-15.0f, 12.4f, 0.0f});
  e.pasos.push_back(p);
  TomasPresentacion(e, 0);   // r = 0: Yamcha (camara y voces medidas en la ronda 1)
}

// S1-F03.1: escena previa de los Juegos de Cell (B1, escena 0x1C de la ronda 1, antes de Mr. Satan): Goku (SSJ) contra
// Cell en un combate "solo escena" que no puntua, con las 4 tomas del original (capturas de PCSX2,
// D:\tmp\s9b_satan\capturas\originales_b1\cell1_g1..g4): el ring de lejos, Cell de cerca, Goku desde abajo y, por
// encima del hombro de Cell, Goku llegando. Goku = lado 1 (x = -15), Cell = lado 2 (x = +15), marco de Paso::rel.
constexpr int kGokuB3 = 0, kCellB3 = 33, kFormaSsj = 2, kFormaPerfecto = 2;   // FormaB3: Goku 2 = SSJ, Cell 2 = perfecto
// Camara MEDIDA en PCSX2 (B1 europeo): la de B1 vive en 0x555B00 (+0x10 ojo, +0x20 objetivo; leida ~7800 veces por
// segundo con PINE desde un estado al final del prologo) y los luchadores en sus nodos de cuerpo (Goku 0x728400, Cell
// 0x723FC0: matriz 4x4 cuyo eje z es hacia donde miran). Cada toma pasa al marco de Paso::rel anclada a su luchador
// (Cell de cerca, Goku desde abajo), al sitio de Cell con el eje de la pelea (el ojo por encima de su hombro) o al
// centro del ring (la general y el objetivo del hombro); suelo = el del luchador (nodo - 10,2 Goku, - 12,7 Cell; la
// suela de Goku en el ring, y = 3,9). Claves {cuadro de 60 Hz, x, y, z} simplificadas (error <= 1 unidad; 4 en la
// general, que esta a 300-700). Duraciones = cortes de la camara (9,98 / 5,80 / 3,80 / 14,42 s). Mismas unidades que
// B3 (los escenarios de B1 entran a escala 1, escenarios_convertir.py). Herramientas en el INFORME de F03.
constexpr std::array<float, 4> kPrevia_ring_ojo[] = {{0, 188.2f, 212.2f, 678.3f}, {99, 187.6f, 210.1f, 674.4f}, {183, 184.9f, 203.7f, 659.5f}, {237, 174.0f, 198.8f, 639.5f}, {291, 150.0f, 195.2f, 609.4f}, {357, 106.3f, 192.4f, 559.6f}, {429, 45.7f, 190.6f, 489.5f}, {507, -28.7f, 189.6f, 396.2f}, {594, -114.3f, 188.8f, 271.0f}, {598, -115.5f, 188.7f, 269.1f}};
constexpr std::array<float, 4> kPrevia_ring_obj[] = {{0, 126.5f, 2.7f, 219.2f}, {123, 122.4f, 3.2f, 212.7f}, {177, 116.3f, 3.8f, 201.7f}, {204, 110.1f, 4.6f, 190.5f}, {249, 89.5f, 7.2f, 153.5f}, {355, 24.5f, 15.6f, 39.1f}, {385, 10.6f, 17.3f, 15.5f}, {409, 3.3f, 18.2f, 3.7f}, {447, -0.0f, 17.8f, 0.0f}, {535, 3.4f, 8.8f, 7.4f}, {598, 3.6f, 8.5f, 7.7f}};
// (Cell de cerca, Goku desde abajo y el ojo del hombro: lo que en B1 es "delante" del luchador va hacia donde mira su
// pecho en la guardia de B3, de lado: +z el lado 1 y -z el lado 2, como los planos de cinematica.cpp Encuadre)
constexpr std::array<float, 4> kPrevia_cell_ojo[] = {{0, 11.1f, 18.7f, -12.6f}, {347, 11.1f, 18.7f, -12.6f}};
constexpr std::array<float, 4> kPrevia_cell_obj[] = {{0, 15.1f, 18.9f, 0.3f}, {347, 15.1f, 18.9f, 0.3f}};
constexpr std::array<float, 4> kPrevia_goku_ojo[] = {{0, -11.3f, 5.5f, 20.4f}, {18, -10.9f, 5.8f, 19.3f}, {66, -11.8f, 6.1f, 19.4f}, {81, -9.6f, 6.4f, 18.9f}, {89, -10.1f, 5.1f, 18.7f}, {97, -9.5f, 4.2f, 15.9f}, {108, -6.4f, 5.8f, 12.8f}, {119, -6.5f, 7.9f, 12.5f}, {139, -7.7f, 9.0f, 12.7f}, {227, -7.7f, 9.4f, 12.6f}};
constexpr std::array<float, 4> kPrevia_goku_obj[] = {{0, -20.0f, 14.4f, -5.3f}, {30, -20.0f, 14.7f, -6.6f}, {82, -20.7f, 15.2f, -5.9f}, {99, -20.0f, 12.9f, -8.8f}, {133, -21.3f, 16.8f, -7.9f}, {227, -21.6f, 17.4f, -7.9f}};
constexpr std::array<float, 4> kPrevia_hombro1_ojo[] = {{0, 14.9f, 25.4f, 37.7f}, {240, 29.9f, 22.4f, 18.5f}, {316, 33.4f, 22.2f, 16.1f}, {623, 49.0f, 22.5f, 12.5f}, {636, 51.0f, 22.7f, 12.3f}};
constexpr std::array<float, 4> kPrevia_hombro1_obj[] = {{0, -5.2f, 8.4f, 9.3f}, {86, -3.1f, 10.2f, 8.3f}, {229, 0.4f, 12.9f, 3.1f}, {301, 1.4f, 13.3f, 1.7f}, {636, 5.0f, 12.1f, 0.6f}};
constexpr std::array<float, 4> kPrevia_hombro2_ojo[] = {{0, 51.5f, 22.7f, 12.2f}, {96, 56.6f, 22.8f, 11.2f}, {123, 56.3f, 22.8f, 13.0f}, {227, 61.9f, 22.9f, 12.0f}};
constexpr std::array<float, 4> kPrevia_hombro2_obj[] = {{0, 5.1f, 12.1f, 0.6f}, {227, 7.4f, 11.3f, -0.0f}};
struct TomaPrevia {
  int plano;
  float segundos;
  int cara;              // retrato del subtitulo (-1 ninguno)
  const char *es, *en;   // subtitulo (texto propio; el de B1 son imagenes sin localizar, F03.4)
  const std::array<float, 4>* ojo;
  size_t n_ojo;
  const std::array<float, 4>* obj;
  size_t n_obj;
};
#define DBZ3_PREVIA(n) kPrevia_##n##_ojo, std::size(kPrevia_##n##_ojo), kPrevia_##n##_obj, std::size(kPrevia_##n##_obj)
const TomaPrevia kTomasPrevia[] = {
    // 1. el reportero y el ring de lejos, Cell en el centro (B1: el reportero anuncia los Juegos)
    {dbz3::cinematica::kGeneral, 9.98f, -1, "¡Empiezan los Juegos de Cell!", "The Cell Games are about to begin!",
     DBZ3_PREVIA(ring)},
    // 2. Cell de cerca, quieto
    {dbz3::cinematica::kLado2, 5.80f, kCellB3, "Ya es la hora. Me da igual quién, ¡pero que salga uno!",
     "It's time. I don't care who it is, just get out here!", DBZ3_PREVIA(cell)},
    // 3. Goku desde abajo, subiendo al ring
    {dbz3::cinematica::kLado1, 3.80f, kGokuB3, "Bueno, empiezo yo.", "Well, I'll go first.", DBZ3_PREVIA(goku)},
    // 4. por encima del hombro de Cell, Goku llega delante (corte de camara de B1 en 10,00 s: #AMC de DATA_EN 2372,
    //    hijo 20, clips de 600/350/230/850/400/890/160/340/660/620/222 cuadros = cortes 0/10,00/15,83/19,67/33,83/
    //    40,50/55,33/58,00/63,67/74,67/85,00/88,70 s. El subtitulo medido cambia a los 10,6 s; la toma del puerto es
    //    un resumen de 10,62 s de esa escena de 88,7 s).
    {dbz3::cinematica::kLado1, 10.62f, -1, "Solo hay un montón de perdedores... ¿Qué será de la Tierra?",
     "Nothing but a bunch of losers out there... What will happen to the Earth?", DBZ3_PREVIA(hombro1)},
    // y Mr. Satan grita desde fuera (la misma toma sigue)
    {dbz3::cinematica::kLado1, 3.80f, kJugador, "¡Espeeera!", "Waaait!", DBZ3_PREVIA(hombro2)},
};
#undef DBZ3_PREVIA
// Campo de vision vertical de B1 en estas tomas: medido proyectando a Goku (suela y = 3,9 en el pixel 385, punta del pelo
// 24,07 en el 95, captura del estado de PCSX2 a los 33,5 s, 640x480) con el ojo y el objetivo leidos: 0,579 rad, que es
// 2 atan(0,3) = 0,583 (la camara de B1 guarda 1,2 = 4 x 0,3 en +0). B3 usa 0,785 (mgr+0x1C4, cine.cpp).
void EscenaPrevia(dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  for (const auto& t : kTomasPrevia) {
    C::Paso p;
    p.plano = t.plano;
    p.segundos = t.segundos;
    p.cara = t.cara;
    if (t.es) p.linea = T::T(t.es, t.en);
    p.rel = true;
    p.cam_ojo.assign(t.ojo, t.ojo + t.n_ojo);
    p.cam_obj.assign(t.obj, t.obj + t.n_obj);
    p.cam_fov = {{0.0f, kFovB1}};
    // B1: Cell espera de pie (brazos cruzados); en guardia B3 se agacha y gira: su pose de entrada (lado 2)
    // EU 0x19C150 SatanBattle_ResetFighter / EU 0x1AA700 SatanBattle_BlendPose: reinicio y aplicacion de la pose
    // (peso 1.0) al luchador de la toma; aqui, la pose B3 equivalente del lado 2.
    if (t.cara == kCellB3) {
      p.anim[1] = C::kPoseEntrada2;
      // DS186: mismo ajuste que la escena larga: la tabla medida apunta a la cara de Cell con el `y` de B1; el B3 lo
      // dibuja con su pose y solo asomaba la coronilla (c024). Cintura de Cell de B1 sobre el suelo del ring:
      // 16,6016 (THL_WAIST del #AMM en el clip 1) - 3,9 (suela de B1). El suelo del marco `rel` es la suela.
      p.objetivo_cuerpo_b3 = true;
      p.cuerpo_cintura_b1 = 12.702f;
      p.cuerpo_cabeza_b1 = kB1CellCabeza;
    }
    e.pasos.push_back(p);
  }
}
// DS143: ESCENA LARGA DE LA RONDA 1 (Juegos de Cell) con sus clips y tiempos REALES. El paquete es DATA_EN.AFS #2372
// (#AMB) de la ISO del jugador: hijo 20 = #AMC con los 11 clips de camara (600/350/230/850/400/890/160/340/660/620/222
// cuadros = 5322 = 88,70 s), hijo 1 = #AMM de Goku (GOK_WAIST), hijo 0 = #AMM de Cell (THL_WAIST) e hijo 3 = #AMM de
// Mr. Satan (STN_WAIST): sus pistas de posicion llevan el sitio de cada actor en el mundo de B1. Los clips encajan
// cuadro a cuadro con la camara: 0-600 ancho del ring, 600-950 Cell, 950-1180 Goku entra, 1180-2030 Goku y Cell se
// enfrentan (los dos 850), 2030-2430 el coche, 2430-3320 Satan baja, 3320-3480 botas-cara, 3480-3820 plano ancho,
// 3820-4480 y 4480-5100 primeros planos, 5100-5322 el retrato. Se usa el escenario de B3 de los Juegos de Cell (mapa
// 50, ring en el origen como el de B1, escala 1) y los modelos de B3; la camara y las trayectorias son las de B1 tal
// cual (sin cambio de marco: las dos escenas comparten el origen del ring).
struct PaqueteRonda1 {
  bool leido = false, ok = false;
  CamaraCruda cam;
  TrayectoriaB1 gok, thl, stn;
};
PaqueteRonda1& PaqueteRonda1De(const std::filesystem::path& ruta) {
  static PaqueteRonda1 p;
  if (p.leido) return p;
  p.leido = true;
  if (ruta.empty()) return p;
  AfsPs2 afs;
  bool abierto = false;
  for (const char* n : {"DATA_EN.AFS", "DATA_US.AFS", "DATA_USA.AFS"}) abierto = abierto || afs.Abrir(ruta, n);
  if (!abierto) {
    REXLOG_WARN("dbz3 pasarela: ronda 1: no se pudo abrir DATA_EN.AFS de la ISO de B1 (escena larga: la resumida)");
    return p;
  }
  const std::vector<uint8_t> paq = afs.Entrada(2372);   // el paquete de la escena de los Juegos de Cell
  if (paq.empty()) {
    REXLOG_WARN("dbz3 pasarela: ronda 1: DATA_EN #2372 vacio (escena larga: la resumida)");
    return p;
  }
  p.ok = LeerCamaraCruda(paq, 20, p.cam) && p.cam.cuadros >= 5322;
  LeerTrayectoriaB1(HijoAmb(paq, 1), "GOK_WAIST", p.gok);   // Goku
  LeerTrayectoriaB1(HijoAmb(paq, 0), "THL_WAIST", p.thl);   // Cell
  LeerTrayectoriaB1(HijoAmb(paq, 3), "STN_WAIST", p.stn);   // Mr. Satan
  REXLOG_INFO("dbz3 pasarela: ronda 1: escena larga de B1 (#AMC 2372/20): {} ({} cuadros, {} claves); trayectorias: "
              "Goku {} clips, Cell {} clips, Satan {} clips", p.ok ? "leida" : "no disponible", p.cam.cuadros,
              p.cam.ojo.size(), p.gok.clips.size(), p.thl.clips.size(), p.stn.clips.size());
  return p;
}
// Copia las claves de camara del tramo [g0,g1) de la escena, con el cuadro relativo al paso.
// DS170: ademas de las claves de dentro del tramo se copian la ULTIMA de antes de g0 y la PRIMERA de despues de g1
// (con su cuadro relativo, aunque sea negativo o pase del final). Los tramos que parten un clip de B1 por el cambio de
// subtitulo perdian las claves del clip y cada mitad se quedaba congelada en una: el clip 5 de la llegada (2430..3320)
// tiene SOLO dos claves y esta partido en 2790 -> el ojo pasaba de golpe de y 7,07 a 17,60 y el objetivo de 6,24 a
// 16,77, cuando en B1 es un travelling continuo de 14,83 s entre esos dos valores (por eso Satan salia cortado por
// abajo en las tomas del "¡Por fin...!"). Con las claves de borde, MuestraCam interpola igual que B1. En un corte de
// camara de verdad (cada clip empieza con su clave) la clave extra queda fuera del tramo y no cambia nada.
void AnadirCamara(cinematica::Paso& p, const CamaraCruda& c, int g0, int g1) {
  auto copiar = [&](const std::vector<std::array<float, 4>>& k, std::vector<std::array<float, 4>>& out) {
    out.clear();
    for (const auto& a : k)
      if (a[0] >= float(g0) - 0.5f && a[0] <= float(g1)) out.push_back({a[0] - float(g0), a[1], a[2], a[3]});
    const std::array<float, 4>* antes = nullptr;
    const std::array<float, 4>* despues = nullptr;
    for (const auto& a : k) {
      if (a[0] < float(g0) - 0.5f) antes = &a;
      else if (a[0] > float(g1) && !despues) despues = &a;
    }
    if (antes) out.insert(out.begin(), {(*antes)[0] - float(g0), (*antes)[1], (*antes)[2], (*antes)[3]});
    if (despues) out.push_back({(*despues)[0] - float(g0), (*despues)[1], (*despues)[2], (*despues)[3]});
    if (out.empty() && !k.empty()) {   // tramo sin claves (no deberia): la mas cercana, fija
      const std::array<float, 4>* r = &k.front();
      for (const auto& a : k)
        if (std::fabs(a[0] - float(g0)) < std::fabs((*r)[0] - float(g0))) r = &a;
      out.push_back({0.0f, (*r)[1], (*r)[2], (*r)[3]});
    }
  };
  copiar(c.ojo, p.cam_ojo);
  copiar(c.obj, p.cam_obj);
  p.cam_fov.clear();
  for (const auto& f : c.fov)
    if (f[0] >= float(g0) - 0.5f && f[0] <= float(g1)) p.cam_fov.push_back({f[0] - float(g0), f[1]});
  if (!c.fov.empty()) {   // DS170: mismas claves de borde para el fov
    const std::array<float, 2>* antes = nullptr;
    const std::array<float, 2>* despues = nullptr;
    for (const auto& f : c.fov) {
      if (f[0] < float(g0) - 0.5f) antes = &f;
      else if (f[0] > float(g1) && !despues) despues = &f;
    }
    if (antes) p.cam_fov.insert(p.cam_fov.begin(), {(*antes)[0] - float(g0), (*antes)[1]});
    if (despues) p.cam_fov.push_back({(*despues)[0] - float(g0), (*despues)[1]});
  }
  if (p.cam_fov.empty() && !c.fov.empty()) {
    float v = c.fov.front()[1], mejor = std::fabs(c.fov.front()[0] - float(g0));
    for (const auto& f : c.fov)
      if (std::fabs(f[0] - float(g0)) < mejor) mejor = std::fabs(f[0] - float(g0)), v = f[1];
    p.cam_fov.push_back({0.0f, v});
  }
}
// Trayectoria de un actor en un clip #AMM, en el tramo global [g0,g1) (claves {cuadro GLOBAL, x, y, z}).
void AnadirTrayectoria(const TrayectoriaB1& t, size_t clip, int clip_g0, int g0, int g1,
                       std::vector<std::array<float, 4>>& out) {
  if (clip >= t.clips.size() || t.clips[clip].empty()) return;
  const auto& k = t.clips[clip];
  auto muestra = [&](float f, float o[3]) {
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1][0] <= f) ++i;
    const size_t j = std::min(i + 1, k.size() - 1);
    const float d = k[j][0] - k[i][0], u = d > 0 ? std::clamp((f - k[i][0]) / d, 0.0f, 1.0f) : 0.0f;
    for (int c = 1; c < 4; ++c) o[c - 1] = k[i][size_t(c)] + (k[j][size_t(c)] - k[i][size_t(c)]) * u;
  };
  for (int f = g0; f < g1; ++f) {
    float o[3];
    muestra(float(f - clip_g0), o);
    out.push_back({float(f), o[0], o[1], o[2]});
  }
}
// Pasa a un paso la trayectoria del actor al que se ancla la camara en el tramo [g0,g1) (cuadro relativo al paso).
void AnadirAncla(cinematica::Paso& p, int lado, const std::vector<std::array<float, 4>>& global, int g0, int g1) {
  if (lado < 0 || lado > 1 || global.empty()) return;
  for (const auto& a : global)
    if (a[0] >= float(g0) - 0.5f && a[0] < float(g1) - 0.5f)
      p.cam_ancla.push_back({a[0] - float(g0), a[1], a[2], a[3]});
  if (p.cam_ancla.empty()) {   // fuera del tramo: la clave mas cercana, fija
    const std::array<float, 4>* r = &global.front();
    for (const auto& a : global)
      if (std::fabs(a[0] - float(g0)) < std::fabs((*r)[0] - float(g0))) r = &a;
    p.cam_ancla.push_back({0.0f, (*r)[1], (*r)[2], (*r)[3]});
  }
  p.cam_ancla_lado = lado;
}
// Llena la camara/tramo de un paso de la escena larga. `ancla`/`ancla_lado`: el actor de B1 de la toma (la camara se
// desplaza por cuadro para encuadrar al luchador de B3 en su sitio); nullptr = camara de B1 tal cual (planos del ring).
void PasoEscenaLarga(cinematica::Paso& p, const CamaraCruda& cam, const std::vector<std::array<float, 4>>* ancla,
                     int ancla_lado, int g0, int g1) {
  p.segundos = float(g1 - g0) / 60.0f;
  p.cuadros = g1 - g0;
  p.estilo_b1 = true;   // B1: imagen entera, sin bandas de cine
  p.sin_ayuda = true;
  p.fov_acerca = true;   // B3 proyecta con su 0,785: el fov del #AMC se consigue por distancia (como en el final)
  AnadirCamara(p, cam, g0, g1);
  if (ancla) AnadirAncla(p, ancla_lado, *ancla, g0, g1);
}
// Escena larga de los Juegos de Cell: camara y actores de B1 en coordenadas del mundo (vacio = no se pudo leer).
bool EscenaPreviaLarga(cinematica::Escena& e) {
  PaqueteRonda1& q = PaqueteRonda1De(g_d->ruta);
  if (!q.ok || q.gok.clips.empty() || q.thl.clips.empty()) return false;
  // Trayectorias globales de los dos actores con los clips que encajan con la camara (ver arriba).
  std::vector<std::array<float, 4>> gok, thl;
  AnadirTrayectoria(q.gok, 2, 0, 0, 600, gok);       // de pie en el ring (clip 2 de 2 cuadros)
  AnadirTrayectoria(q.thl, 0, 0, 0, 600, thl);       // Cell espera
  AnadirTrayectoria(q.thl, 1, 600, 600, 950, thl);   // 350: Cell se acerca (1,0 s)
  AnadirTrayectoria(q.gok, 0, 950, 950, 1180, gok);  // 230: Goku entra al ring
  AnadirTrayectoria(q.gok, 1, 1180, 1180, 2030, gok);   // 850: Goku y Cell cara a cara
  AnadirTrayectoria(q.thl, 2, 1180, 1180, 2030, thl);   // 850: Cell
  // Un paso por toma de B1: los cortes de la camara (0/600/950/1180/1817/2030) y sus 5 lineas.
  // Tom 1 = Cell (lado 2), toma 2 = Goku (lado 1); las tomas del ring (0 y el cara a cara) van con la camara tal cual.
  struct Tramo { int g0, g1, linea; const std::vector<std::array<float, 4>>* ancla; int lado; };
  static const Tramo kTramos[] = {{0, 600, 0, nullptr, -1}, {600, 950, 1, &thl, 1}, {950, 1180, 2, &gok, 0},
                                  {1180, 1817, 3, nullptr, -1}, {1817, 2030, 4, nullptr, -1}};
  static constexpr int kLineas[5] = {0, 1, 2, 3, 4};   // kTomasPrevia[0..4]
  // DS145: B1 pone a los actores de esta escena mirando a lo largo de Z (Cell en z=20,3 y Goku en z=-12 en el cara a
  // cara) y B3 enfrenta los dos lados a lo largo de X; sin corregir, la camara de B1 (que va en coordenadas del mundo)
  // veia a Cell casi de perfil en su primer plano (offset B1 (-3,9,6,0,-12,6) contra una cara que en B3 mira a -X:
  // 73 grados). +pi/2 lleva el offset de la camara al mismo lado del cuerpo (17 grados, el de B1). Solo en los pasos
  // anclados: los planos del ring (0 y cara a cara) se quedan con la camara de B1 tal cual.
  constexpr float kGiroPrevia = 1.5707964f;
  // DS148: posicion de Cell (pista THL_WAIST del #AMM) en un cuadro global, para la traza por toma.
  auto puntoCell = [](const std::vector<std::array<float, 4>>& k, int f) {
    std::array<float, 3> o{};
    if (k.empty()) return o;
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1][0] <= float(f)) ++i;
    const size_t j = std::min(i + 1, k.size() - 1);
    const float d = k[j][0] - k[i][0], u = d > 0 ? std::clamp((float(f) - k[i][0]) / d, 0.0f, 1.0f) : 0.0f;
    for (int c = 0; c < 3; ++c) o[size_t(c)] = k[i][size_t(c) + 1] + (k[j][size_t(c) + 1] - k[i][size_t(c) + 1]) * u;
    return o;
  };
  int toma = 0;
  for (const Tramo& t : kTramos) {
    cinematica::Paso p;
    p.plano = cinematica::kGeneral;
    PasoEscenaLarga(p, q.cam, t.ancla, t.lado, t.g0, t.g1);
    if (t.ancla) p.cam_ancla_giro = kGiroPrevia;
    if (t.linea == 1) {   // DS186: primer plano de Cell (c024): objetivo al cuerpo DIBUJADO (cintura/cabeza de Cell de B1)
      p.objetivo_cuerpo_b3 = true;
      p.cuerpo_suelo_b3 = q.thl.suelo;   // 15,49: altura de la cadera de B3 que LeerTrayectoriaB1 resto a la pista de B1
      p.cuerpo_cabeza_b1 = kB1CellCabeza;
    }
    const auto& l = kTomasPrevia[kLineas[t.linea]];
    if (l.es) p.linea = T::T(l.es, l.en);
    if (t.linea == 1 || t.linea >= 3) p.anim[1] = cinematica::kPoseEntrada2;   // Cell de pie, no en guardia
    e.pasos.push_back(p);
    const bool es_goku = t.linea == 2;
    const auto pos = es_goku ? puntoCell(gok, t.g0) : puntoCell(thl, t.g0);
    const char* const objeto = t.linea == 1 ? "Cell perfecto + ala XCEL_LWING/RWING"
                                : t.linea == 2 ? "Goku"
                                : t.linea == 4 ? "Cell de espaldas (ala XCEL_LWING/RWING) + Goku"
                                               : "Cell y Goku (ring; ala XCEL_LWING/RWING)";
    REXLOG_INFO("dbz3 pasarela: previa toma {}/5 (G {}..{}): objeto '{}' pos ({:.1f},{:.1f},{:.1f}) escala 1.00; "
                "coche/humo no (solo en la llegada)", ++toma, t.g0, t.g1, objeto, pos[0], pos[1], pos[2]);
  }
  REXLOG_INFO("dbz3 pasarela: ronda 1: escena previa LARGA de B1: {} tomas, {:.2f} s (clips 0-3 del #AMC)",
              e.pasos.size(), q.cam.corte[4] / 60.0);
  // DS148: el objeto negro en V que se ve en las tomas del ring NO es un prop de la llegada. Aqui no se pide el coche
  // (ni su carga): el coche y el humo solo existen en EscenaCellLarga (clips 4-10 del #AMC) y el V es el ala de Cell
  // perfecto del propio modelo de B3 (bin 148, huesos XCEL_LWING/XCEL_RWING; en el bind van rigidas al hueso 0). B1
  // dibuja la misma ala en su toma 1 (captura PCSX2 originales_b1/cell1_g1 frames 001/002).
  REXLOG_INFO("dbz3 pasarela: previa: sin props de la llegada (coche/humo solo en la toma del clip 4, G 2030..2430); "
              "el objeto negro en V es el ala de Cell perfecto del modelo de B3 (XCEL_LWING/XCEL_RWING)");
  return true;
}
// Llegada de Mr. Satan (clips 4-10, 54,87 s): camara de B1 anclada a la trayectoria de Satan; el retador no esta en la
// escena de B1 (se oculta). Los luchadores se quedan en su sitio del escenario de B3 (la camara se desplaza por cuadro
// con la trayectoria de B1) para no sacarlos del ring; despues van las dos tomas de presentacion.
bool EscenaCellLarga(cinematica::Escena& e) {
  PaqueteRonda1& q = PaqueteRonda1De(g_d->ruta);
  if (!q.ok || q.stn.clips.empty()) return false;
  std::vector<std::array<float, 4>> sat;
  AnadirTrayectoria(q.stn, 0, 2030, 2030, 2430, sat);   // 2: espera junto al coche
  AnadirTrayectoria(q.stn, 1, 2430, 2430, 3320, sat);   // 890: baja del coche
  AnadirTrayectoria(q.stn, 2, 3320, 3320, 3480, sat);   // 160: primer plano (botas a cara)
  AnadirTrayectoria(q.stn, 3, 3480, 3480, 3820, sat);   // 340: plano ancho
  AnadirTrayectoria(q.stn, 4, 3820, 3820, 4480, sat);   // 660: primer plano
  AnadirTrayectoria(q.stn, 5, 4480, 4480, 5100, sat);   // 620: primer plano
  AnadirTrayectoria(q.stn, 6, 5100, 5100, 5322, sat);   // 222: retrato
  // Lineas medidas de la traza de B1 (cortes en s -> cuadro global): 39,822 / 46,482 / 61,322 / 63,982 / 69,642 / 74,982
  // DS168: la toma del coche (2030..2389) tambien va anclada a Satan. Antes iba sin ancla, con la camara en
  // coordenadas del mundo de B1: el coche (mundo de B1) quedaba junto a la camara pero Satan de B3 estaba en el ring
  // (otro sitio) y, al llegar el corte 2389, la camara saltaba de golpe por el ancla (~330 u). El sujeto de la toma es
  // Satan junto al coche, asi que se usa el mismo marco que el resto de la llegada.
  // DS168: 2030..2430 es la toma del coche (clip 4). Se parte en 2389 solo por la linea de B1 (39,822 s), pero el
  // corte de camara real es 2430: los dos primeros tramos conservan el ojo de B1 (sin fov_acerca) para no dar un salto
  // de 81 u en medio de la toma; en 2430 empieza la toma 5 y vuelve el fov_acerca.
  struct Tramo { int g0, g1, linea; bool ancla; };
  static constexpr Tramo kTramos[] = {{2030, 2389, -1, true}, {2389, 2430, 0, true}, {2430, 2790, 0, true},
                                      {2790, 3680, 1, true}, {3680, 3840, -1, true}, {3840, 4180, 2, true},
                                      {4180, 4500, 3, true}, {4500, 5322, -1, true}};
  static const char* const kLineas[][2] = {
      {"¡Ha llegado el salvador de la Tierra, Mr. Satán!", "The Earth's savior, Mr. Satan, has arrived!"},
      {"¡Por fin, por fin ha aparecido!", "At last, at last he's shown up!"},
      {"¡Je, je, je! Qué ganas de un rival de verdad...", "Heh heh! I can't wait for a real opponent..."},
      {"Os acabo a todos a la vez. ¡Venid a por mí!", "I'll take you all on at once. Come and get it!"}};
  // DS170: altura del hueso de la cabeza de Mr. Satan de B1 sobre su cintura: #AMO de DATA_EN 2363, STN_HEAD 6,942 -
  // STN_WAIST -0,972 = 7,914 (medido en DS170). DS185: el objetivo de B1 se lleva a la MISMA fraccion de la recta
  // cintura->cabeza del cuerpo DIBUJADO de B3 (esqueleto animado de la escena; cinematica.cpp, objetivo_cuerpo_b3).
  constexpr float kB1CabezaSobreCadera = 7.914f;
  for (const Tramo& t : kTramos) {
    cinematica::Paso p;
    p.plano = cinematica::kLado1;
    PasoEscenaLarga(p, q.cam, t.ancla ? &sat : nullptr, 0, t.g0, t.g1);
    // DS185: desde el clip 5 (Satan ya fuera del coche) el objetivo de B1 apunta al CUERPO de Satan: se lleva a la
    // fraccion equivalente del cuerpo que se dibuja. La toma del coche (g0 < 2430) se queda como estaba (su ojo de B1
    // y el resultado ya validado en DS168). `q.stn.suelo` (11,438) es la altura de la cadera de B3 que `LeerTrayectoriaB1`
    // resto a la pista de B1; con ella se recupera la cintura de B1 de cada cuadro en cinematica.cpp.
    if (t.ancla && t.g0 >= 2430) {
      p.objetivo_cuerpo_b3 = true;
      p.cuerpo_suelo_b3 = q.stn.suelo;
      p.cuerpo_cabeza_b1 = kB1CabezaSobreCadera;
    }
    p.ocultar_lado2 = true;   // B1 no tiene retador en la escena de la llegada (el cuerpo del lado 2, oculto)
    // DS168: la toma del coche (clip 4) conserva el ojo de B1. El truco de fov_acerca (acercar el ojo 39,5 % al
    // objetivo) movia la camara ~85 u por delante y dejaba el coche detras (Camara::P: zv > -1, "vis false"), y como
    // el objetivo del clip 4 acaba en y = -3,55, el ojo bajaba de 13,1 a 8,3 y se metia bajo la hierba del escenario
    // de B3 (fotos c041-c044). Sin acercar, la camara se queda en el ojo y la altura de B1 (12,8..16,0) y el coche
    // vuelve a entrar por la derecha como en el original (frames 104-122 de cell1_g7).
    if (t.g0 < 2430) p.fov_acerca_sin_mover = true;   // toda la toma del coche
    if (t.linea >= 0) p.linea = T::T(kLineas[t.linea][0], kLineas[t.linea][1]);
    e.pasos.push_back(p);
  }
  TomasPresentacion(e, 0);   // despues, las dos tomas de B1 (Satan y Yamcha) y el READY
  REXLOG_INFO("dbz3 pasarela: ronda 1: llegada LARGA de B1: {} tomas, {:.2f} s (clips 4-10 del #AMC, camara anclada a "
              "la trayectoria de Satan)", e.pasos.size(), (q.cam.cuadros - q.cam.corte[4]) / 60.0f);
  {   // DS170: la toma del clip 5 (2430..3320) en B1 es un travelling de 14,83 s (ojo y 7,07 -> 17,60, objetivo
      // y 6,24 -> 16,77). Se deja en el log la primera y la ultima clave del fichero para comprobar en la prueba que
      // no se vuelve a congelar (el tramo 2790..3680 solo tenia la clave final: Satan salia cortado por abajo).
    auto ext = [&](const std::vector<std::array<float, 4>>& k, float* a, float* b) {
      bool hay = false;
      for (const auto& v : k)
        if (v[0] >= 2430.0f && v[0] <= 3320.0f) {
          if (!hay) *a = v[2];
          *b = v[2];
          hay = true;
        }
      return hay;
    };
    float o0 = 0, o1 = 0, b0 = 0, b1 = 0;
    const bool ok_o = ext(q.cam.ojo, &o0, &o1), ok_b = ext(q.cam.obj, &b0, &b1);
    char so[64] = "sin claves", sb[64] = "sin claves";
    if (ok_o) std::snprintf(so, sizeof so, "%.2f -> %.2f", o0, o1);
    if (ok_b) std::snprintf(sb, sizeof sb, "%.2f -> %.2f", b0, b1);
    REXLOG_INFO("dbz3 pasarela: llegada: clip 5 de B1 (G 2430..3320, 890 cuadros): ojo y {}; objetivo y {} (travelling "
                "de 14,83 s; el tramo del subtitulo en 2790 lo interpola, no lo congela)", so, sb);
  }
  return true;
}
// El combate "solo escena": 1 s de tiempo (el reloj esta parado mientras dura la escena, cinematica.cpp) y, al acabar
// la escena, se da por terminado como un ring out del lado 1 (mgr+35569 = hueco del ganador + 1, como los retos de
// VigilarCombate) para no dejar ese segundo de pelea. Sin puntos ni avance: al soltar el combate se vuelve al mismo
// paso y DrawCombate pide el combate de Yamcha (Lanzar).
// ---- S5-A3b: reportera (con su micro), tarjeta "secreto" y Cell tumbado de la escena final, portados de B1 ----
// Todo sale en marcha de la ISO de B1 del jugador (nada va en la version): el paquete de la escena es DATA_EN.AFS #2374
// (#AMB): hijo 1 = #AMM de Cell (THL_, 1 clip de 650 cuadros: su pose de KO), hijo 2 = #AMM de la reportera (ANS00_, 2 clips
// de 400 y 380 cuadros: entra andando y se queda con el micro), hijos 3/4/5 = #AMO EFT_BOOK + #AMM (2 clips de 320) + #AMT
// de la tarjeta "secreto" (256 x 256 con el sello); los modelos son DATA_EN 335/336 (Cell perfecto) y 57/58 (reportera).
// Los clips llevan la POSICION en el mundo de B1 en la pista del hueso WAIST (Satan: (-36,13.7,-43) = su ancla medida), asi
// que la colocacion es dato, no ajuste: se pasa al marco de la camara medida (conv de A3/herramientas/gen_final.py:
// x' = z + 22,2, y' = y - 3,9, z' = -(x + 35)) y de ahi al mundo con los dos luchadores. Orden de los clips de Satan
// del paquete (650, 350, 400, 380, 320, 320, 310, 440 cuadros a 60 Hz = 52,8 s) = las tomas medidas (cortes de camara a
// 17, 24, 35 y 46 s): reportera = clips 2 y 3 de Satan, tarjeta = 4 y 5, Cell = el 0. Se pintan con ps2_modelo (toon de
// B3, camara del combate, z-buffer del juego).
struct ClipFinal {
  ps2::Anim an;
  int g0 = 0;   // cuadro global (60 Hz desde el principio de la escena) en que empieza
};
struct PiezaFinal {
  ps2::Modelo m;
  std::vector<ps2::Tex> tx;
  std::vector<ClipFinal> clips;
  float dx = 0, dy = 0, dz = 0, cuadros = 0;   // ajuste de prueba (marco de la escena) y desfase de cuadros
  float alto = 0;   // altura del modelo (para el borde del toon)
  float escala = 1.0f;   // escala del actor en B1 (la reportera: 0,81, medida en la RAM: sus huesos miden 0,81 de los del modelo)
  int waist = -1;        // hueso WAIST (la pista de posicion lleva su sitio en el mundo; el pivote de la escala)
  // DS31: tramo de la escena (cuadro global G) en el que B1 dibuja esta pieza. Cell solo esta en el primer plano
  // (su clip dura 650 cuadros) y la reportera no sale en los planos del folleto ni en el medio/primer plano final
  // (en B1 no hay micro delante de Satan): se deja de dibujar en esos pasos, como en el original.
  int g0 = 0, g1 = 1 << 30;
  bool ok = false, subida = false;
};
struct PropsFinal {
  PiezaFinal rep, cell, tarjeta;
  std::atomic<int> estado{0};   // 0 sin pedir, 1 cargando, 2 cargado (el hilo de dibujo sube las texturas)
  std::atomic<bool> activo{false};
  std::atomic<int64_t> t0{0};   // ms: principio de la escena (primer paso)
  std::atomic<int64_t> t_req{0};   // DS31: ms de la peticion del combate del final (negro desde ahi: tapa el
                                   // destello verde y el READY con los dos en guardia, que B1 no tiene)
};
PropsFinal g_pf;
// DS31: fotograma congelado del final. B1 superpone la cuenta de puntos al ultimo primer plano de Satan; el puerto
// ponia una pantalla azul. Se captura la salida del guest (Presenter::CaptureGuestOutput) en el ultimo plano y se pinta
// de fondo de la pantalla de puntos (DrawFin), con los carteles encima como en el original.
std::mutex g_foto_mu;
rex::ui::RawImage g_foto;
std::atomic<bool> g_foto_ok{false};
std::atomic<bool> g_foto_pedida{false};
std::atomic<int> g_foto_gen{0};
void CapturarFotogramaFinal() {
  if (g_foto_ok.load() || g_foto_pedida.exchange(true)) return;
  bool ok = false;
  if (rex::Runtime* rt = rex::Runtime::instance()) {
    if (rex::system::IGraphicsSystem* g = rt->graphics_system()) {   // Window::presenter() es protegido; el del sistema grafico es publico
      if (rex::ui::Presenter* p = g->presenter()) {
        rex::ui::RawImage img;
        if (p->CaptureGuestOutput(img) && img.width > 32 && img.height > 32 && !img.data.empty()) {
          {   // el fondo se pinta con una textura RGBA8 ajustada (sin huecos de fila)
            std::lock_guard<std::mutex> lk(g_foto_mu);
            const size_t fila = size_t(img.width) * 4;
            g_foto.width = img.width;
            g_foto.height = img.height;
            g_foto.stride = fila;
            g_foto.data.resize(fila * img.height);
            for (uint32_t y = 0; y < img.height; ++y)
              std::memcpy(&g_foto.data[size_t(y) * fila], &img.data[size_t(y) * img.stride], fila);
          }
          g_foto_ok = true;
          ok = true;
        }
      }
    }
  }
  REXLOG_INFO("dbz3 pasarela: final: fotograma congelado para la cuenta de puntos: {}", ok ? "capturado" : "no disponible");
}
// DS31: el intro de B3 (estados 1 y 2) se salta con START para llegar a la escena; esa entrada termina con la carga de
// ki del luchador y, al cortarla, el aura sigue encendida (la verde que se ve alrededor de Cell tumbado). El aura es el
// bloque de 420 B por jugador en *0x82322AEC; sus 48 efectos van en +212 y los libera sub_82089C10 (es lo que hace
// tagteam.cpp, SwapAura, antes de rehacerla). Se sueltan solo los del rival (lado 2 = indice 1).
void ApagarAuraFinal() {
  if (std::getenv("DBZ3_SIN_APAGAR_AURA_FINAL")) return;
  uint8_t* base = Base();
  const uint32_t g = base ? REX_LOAD_U32(0x82322AEC) : 0;
  if (!g) return;
  const uint32_t a = g + 420;   // jugador 1 (lado 2 de la pantalla)
  PPCContext ctx{};
  int n = 0;
  for (uint32_t i = 0; i < 48; ++i) {
    const uint32_t p = REX_LOAD_U32(a + 212 + 4 * i);
    if (!p) continue;
    ctx.r3.u64 = p;
    DBZ3_CALL_EU(sub_82089C10)(ctx, base);
    REX_STORE_U32(a + 212 + 4 * i, 0);
    ++n;
  }
  REXLOG_INFO("dbz3 pasarela: final: aura del rival apagada ({} efectos del bloque de aura liberados)", n);
}
// DS31: textura del fotograma congelado (se crea una vez por final, en el hilo de la interfaz).
rex::ui::ImmediateTexture* FotoFinalTex() {
  static std::unique_ptr<rex::ui::ImmediateTexture> tex;
  static int generacion = -1;
  if (generacion != g_foto_gen.load()) {
    tex.reset();
    generacion = g_foto_gen.load();
  }
  if (tex) return tex.get();
  std::lock_guard<std::mutex> lk(g_foto_mu);
  if (!g_foto_ok.load() || g_foto.data.empty() || !dbz3::launcher::PreviewDrawer()) return nullptr;
  tex = dbz3::launcher::PreviewDrawer()->CreateTexture(g_foto.width, g_foto.height, rex::ui::ImmediateTextureFilter::kLinear,
                                                      false, g_foto.data.data());
  return tex.get();
}
bool DibujarFotoFinal(ImDrawList* dl) {
  rex::ui::ImmediateTexture* tex = FotoFinalTex();
  if (!tex) return false;
  const S::Imagen g = S::GuestImage(ImGui::GetIO());
  dl->AddImage(reinterpret_cast<ImTextureID>(tex), g.p0, g.p1);
  return true;
}
constexpr float kFinalOffsetS = 0.0f;   // el cuadro 0 de los clips es el 0 de la escena: los cortes de camara medidos (10,74 / 23,29 / 29,67 / 34,87 / 40,32 s) caen en 650 / 1400 / 1780 / 2100 / 2420 cuadros a 60 Hz
int HuesoDe(const std::vector<uint8_t>& amo, const char* nombre) {   // indice del hueso `nombre` del #AMO de PS2 (tabla de nombres de 32 B)
  uint32_t nb = 0, nom = 0;
  if (amo.size() < 0x30) return -1;
  std::memcpy(&nb, &amo[0x10], 4);
  std::memcpy(&nom, &amo[0x24], 4);
  for (uint32_t i = 0; i < nb && size_t(nom) + 32 * (i + 1) <= amo.size(); ++i)
    if (std::strncmp(reinterpret_cast<const char*>(&amo[nom + 32 * i]), nombre, 32) == 0) return int(i);
  return -1;
}
// Animacion `clip` de un #AMM de B1 para un #AMO cuyos huesos no son los mismos que los del #AMM (el #AMM no trae los de
// relleno, p. ej. X<P>_BODY o los NL): cada pista va al hueso del #AMO de su mismo nombre. ps2::LeerAnim asume el mismo orden.
ps2::Anim AnimB1(const std::vector<uint8_t>& amo, const std::vector<uint8_t>& amm, uint32_t clip, size_t nb_amo) {
  uint32_t nb = 0, nom = 0, bn = 0, bnom = 0;
  if (amo.size() < 0x30 || amm.size() < 0x30) return {};
  std::memcpy(&nb, &amo[0x10], 4);
  std::memcpy(&nom, &amo[0x24], 4);
  std::memcpy(&bn, &amm[0x18], 4);
  std::memcpy(&bnom, &amm[0x1C], 4);
  ps2::Anim a = ps2::LeerAnim(amm, clip, bn);
  if (!a || nb != nb_amo) return {};
  ps2::Anim r;
  r.cuadros = a.cuadros;
  r.ang.resize(nb);
  r.pos.resize(nb);
  for (uint32_t j = 0; j < bn && size_t(bnom) + 32 * (j + 1) <= amm.size(); ++j) {
    const char* n = reinterpret_cast<const char*>(&amm[bnom + 32 * j]);
    for (uint32_t i = 0; i < nb && size_t(nom) + 32 * (i + 1) <= amo.size(); ++i)
      if (std::strncmp(n, reinterpret_cast<const char*>(&amo[nom + 32 * i]), 32) == 0) {
        r.ang[i] = a.ang[j];
        r.pos[i] = a.pos[j];
        break;
      }
  }
  return r;
}
// Listas de pesos de B1 con casi nada de peso (p. ej. 0,004 en un solo hueso: la reportera): ps2::Posar da el resto al padre
// del hueso en su reposo (regla de IW/B2), y con la pista del WAIST en coordenadas del mundo ese vertice se queda en el origen
// (pincho de 100 u); con peso 0 el vertice queda en (0, 0, 0). En B1 esas listas valen 1 (normalizadas o rigidas a su hueso).
int NormalizarPesosB1(ps2::Esqueleto& e) {
  int n = 0;
  for (auto& lista : e.pesos) {
    float sw = 0;
    for (const auto& q : lista) sw += q.w;
    if (sw > 1e-6f && sw < 0.05f) {
      for (auto& q : lista) q.w /= sw;
      ++n;
    } else if (sw <= 1e-6f && !lista.empty()) {   // peso 0 (el vertice quedaba en el origen): rigido a su hueso
      lista[0].w = 1.0f;
      ++n;
    }
  }
  return n;
}
// El mod "Final Leyenda Satan (B1)" (mod center hd/final_b1.py, creado de la ISO de B1 del jugador) mete en el moveset NATIVO de
// Cell y de Mr. Satan un clip con las poses de B1 (Cell tumbado; Satan con su animacion de la escena) en el codigo 0x2F8: con el,
// la escena usa los luchadores de B3 y no hace falta pintar a Cell de B1.
constexpr int kCodigoFinalB1 = 0x2F8;
bool ModFinalB1() {
  static const bool si = [] {
    if (std::getenv("DBZ3_SIN_MOD_FINAL")) return false;
    for (const auto& m : dbz3::ListMods(false))
      if (m.name == "Final Leyenda Satan (B1)" && m.enabled) return true;
    return false;
  }();
  return si;
}
// A3b: contorno sin puntas. El borde del toon se extruye por la normal de cada vertice y la malla de B1 repite los vertices en las costuras con
// normales distintas (puntas en los punos de la reportera): se promedian las normales de los vertices coincidentes (a menos de 1e-3 u) cuando
// difieren menos de 75 grados (como hizo E3 en su conversion), en la malla en reposo y en las listas de pesos (en el espacio de cada hueso).
int SuavizarNormales(ps2::Esqueleto& e) {
  struct Ins { size_t t, c; ps2::V3 n; };
  std::map<std::array<int, 3>, std::vector<Ins>> grupos;
  for (size_t t = 0; t < e.crudo.tris.size(); ++t)
    for (size_t c = 0; c < 3; ++c) {
      const ps2::V3& p = e.crudo.tris[t].p[c];
      grupos[{int(std::lround(p.x * 1000.0f)), int(std::lround(p.y * 1000.0f)), int(std::lround(p.z * 1000.0f))}].push_back({t, c, e.crudo.tris[t].n[c]});
    }
  int cambiados = 0;
  const float cos75 = 0.2588f;
  for (auto& [k, g] : grupos) {
    if (g.size() < 2) continue;
    for (const Ins& a : g) {
      ps2::V3 s{0, 0, 0};
      for (const Ins& b : g)
        if (a.n.x * b.n.x + a.n.y * b.n.y + a.n.z * b.n.z > cos75) s = {s.x + b.n.x, s.y + b.n.y, s.z + b.n.z};
      const float l = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
      if (l < 1e-6f) continue;
      const ps2::V3 n{s.x / l, s.y / l, s.z / l};
      e.crudo.tris[a.t].n[a.c] = n;
      ++cambiados;
      if (a.t * 3 + a.c < e.pesos_de.size() && e.pesos_de[a.t * 3 + a.c] >= 0)
        for (ps2::Esqueleto::Peso& q : e.pesos[size_t(e.pesos_de[a.t * 3 + a.c])])
          if (q.hueso < e.inv_mundo.size()) q.normal = e.inv_mundo[q.hueso].gira(n);
    }
  }
  return cambiados;
}
// A3b: diferencias cabeza/manos de Satan entre su pose de B3 y la de B1 por cuadro global (final_b1.py deltas(): 9 floats por cuadro: cabeza B3-B1,
// manos B3-B1, cabeza de B1; marco de la escena). La camara y la tarjeta de B1 apuntan a los huesos de B1; el cuerpo de B3 es ~0,8 u mas bajo.
const float* DeltaFinal(int g) {
  static std::vector<float> v = [] {
    std::vector<float> d;
    std::ifstream f(dbz3::ModsRoot() / "Final Leyenda Satan (B1)" / "delta_satan.bin", std::ios::binary);
    if (!f) return d;
    f.seekg(0, std::ios::end);
    const auto n = size_t(f.tellg());
    f.seekg(0);
    d.resize(n / 4);
    f.read(reinterpret_cast<char*>(d.data()), std::streamsize(d.size() * 4));
    return d;
  }();
  if (v.size() < 9 || std::getenv("DBZ3_SIN_DELTA_FINAL")) return nullptr;
  const size_t i = size_t(std::clamp(g, 0, int(v.size() / 9) - 1));
  return &v[i * 9];
}
void CargarPropsFinal(std::filesystem::path iso) {
  AfsPs2 afs;
  bool abierto = false;
  for (const char* n : {"DATA_EN.AFS", "DATA_US.AFS", "DATA_USA.AFS"}) abierto = abierto || afs.Abrir(iso, n);
  PropsFinal& pf = g_pf;
  if (abierto) {
    const std::vector<uint8_t> paq = afs.Entrada(2374);   // el paquete de la escena
    // reportera con micro: clips 0 (entra andando) y 1 (se queda), tras los de Satan 2 y 3
    const auto amo = afs.Entrada(57), amt = afs.Entrada(58);
    if (HuesoDe(amo, "ANS00_WAIST") >= 0 && !paq.empty()) {
      pf.rep.m = ps2::CargarSuelto(amo, amt, true, false, false);
      if (pf.rep.m.esq) {
        // los vertices rigidos al hueso 0 (X<P>_BODY, que no se mueve: la pista del WAIST lleva la posicion del mundo) se quedaban
        // en el origen al posar (pinchos hasta (0, 0, 0)): se pegan al WAIST, que los lleva con el cuerpo (medido: 0 si ya van pesados)
        auto& e = *pf.rep.m.esq;
        int pegados = 0;
        for (size_t t = 0; t < e.crudo.tris.size(); ++t)
          for (size_t c = 0; c < 3; ++c)
            if (e.crudo.tris[t].hueso[c] == 0 && (t * 3 + c >= e.pesos_de.size() || e.pesos_de[t * 3 + c] < 0)) e.crudo.tris[t].hueso[c] = 1, ++pegados;
        REXLOG_INFO("dbz3 pasarela: reportera: {} vertices rigidos al hueso 0 pegados al WAIST, {} listas de pesos normalizadas", pegados, NormalizarPesosB1(e));
        if (!std::getenv("DBZ3_SIN_SUAVIZAR")) REXLOG_INFO("dbz3 pasarela: reportera: {} normales suavizadas", SuavizarNormales(e));
        const auto amm = HijoAmb(paq, 2);
        const size_t nb = pf.rep.m.esq->padre.size();
        pf.rep.clips = {{AnimB1(amo, amm, 0, nb), 1000}, {AnimB1(amo, amm, 1, nb), 1400}};
        pf.rep.ok = bool(pf.rep.m.malla) && bool(pf.rep.clips[0].an) && bool(pf.rep.clips[1].an);
        pf.rep.escala = 0.81f;
        pf.rep.waist = HuesoDe(amo, "ANS00_WAIST");
        // DS31: B1 no lo dibuja en los planos del folleto ni en el medio/primer plano final (no hay micro delante
        // de Satan): entra a los 16,7 s (G 1000) y se deja de pintar al empezar la linea del folleto (G 1950).
        pf.rep.g0 = 1000;
        pf.rep.g1 = 1949;
      }
    }
    // Cell perfecto tumbado: el clip 0 (650 cuadros) y se queda en el ultimo (solo si el mod de poses no esta: si esta, Cell es el de B3)
    const auto cao = afs.Entrada(335), cat = afs.Entrada(336);
    if (!ModFinalB1() && HuesoDe(cao, "THL_WAIST") >= 0 && !paq.empty()) {
      pf.cell.m = ps2::CargarSuelto(cao, cat, true, false, false);
      if (pf.cell.m.esq) {
        NormalizarPesosB1(*pf.cell.m.esq);
        pf.cell.clips = {{AnimB1(cao, HijoAmb(paq, 1), 0, pf.cell.m.esq->padre.size()), 0}};
        pf.cell.ok = bool(pf.cell.m.malla) && bool(pf.cell.clips[0].an);
        // DS31: su clip de B1 dura 650 cuadros; despues B1 no lo dibuja en ningun encuadre (el del reportero incluido).
        pf.cell.g0 = 0;
        pf.cell.g1 = 649;
      }
    }
    // tarjeta "secreto": EFT_BOOK (hijo 3) con su textura (5) y sus 2 clips de 320 cuadros (4), tras los de Satan 4 y 5
    if (const auto lib = HijoAmb(paq, 3); HuesoDe(lib, "EFT_BOOK") >= 0) {
      pf.tarjeta.m = ps2::CargarSuelto(lib, HijoAmb(paq, 5), true, false, false);
      if (pf.tarjeta.m.esq) {
        const auto amm = HijoAmb(paq, 4);
        const size_t nb = pf.tarjeta.m.esq->padre.size();
        pf.tarjeta.clips = {{AnimB1(lib, amm, 0, nb), 1780}, {AnimB1(lib, amm, 1, nb), 2100}};
        pf.tarjeta.ok = bool(pf.tarjeta.m.malla) && bool(pf.tarjeta.clips[0].an) && bool(pf.tarjeta.clips[1].an);
        pf.tarjeta.g0 = 1956;   // B1: sale a los 41,8 s del video (G 1956) y se va con el 2.o clip (2420)
        pf.tarjeta.g1 = 2430;
      }
    }
  }
  REXLOG_INFO("dbz3 pasarela: props del final 3D de B1: reportera {} ({} tri), Cell tumbado {} ({} tri), tarjeta {} ({} tri)", pf.rep.ok,
              pf.rep.m.malla.tris.size(), pf.cell.ok, pf.cell.m.malla.tris.size(), pf.tarjeta.ok, pf.tarjeta.m.malla.tris.size());
  pf.estado = 2;
}
// ---- DS145: props de la LLEGADA de la ronda 1 (el coche y su humo), del paquete #2372 de la ISO de B1 ----
// En B1 esa escena es un #AMB (DATA_EN 2372) con los actores y los props juntos: hijos 10 (#AMO STC_, 19 huesos),
// 11 (#AMM de 400 cuadros: la pista del hueso raiz STC_BASE lleva la trayectoria del coche de (0,0,0) a (-8,30,19,85),
// el coche frena en los ultimos cuadros) y 12 (#AMT); 13 (#AMO KEMURI_, 19 huesos), 14 (#AMT), 15 (#AMM de 400) y
// 16 (#AMP: 15 registros de 0x1C B, +0x10/+0x14 = cuadros 119..399, el tramo en que salen las 16 nubes). El #AMP no
// lleva transformadas: el humo usa la pista de sus huesos BASE1/BASE2 (posiciones del mundo de B1). Las posiciones del
// #AMM son ABSOLUTAS (igual que las de los actores y las del final): si se sumaran a la matriz de reposo el coche
// saldria detras de la camara del clip 4 (comprobado con el #AMC: solo la posicion absoluta cae en pantalla).
struct PropsLlegada {
  PiezaFinal car, humo;
  std::atomic<int> estado{0};        // 0 sin pedir, 1 cargando, 2 cargado
  std::atomic<bool> activo{false};
  std::atomic<int64_t> t0{0};        // ms del primer paso de EscenaCellLarga (cuadro global 2030 del #AMC)
  int humo_g0 = 119, humo_g1 = 399;  // #AMP hijo 16 (cuadros locales del clip 4)
};
PropsLlegada g_pl;
// DS145: en estos #AMM de props (flags 0x19) las pistas de los huesos que no se mueven son todo ceros: si se aplican
// como absolutas, las ruedas/espejos del coche y las nubes se van al origen del hueso raiz (el coche se desmonta). Se
// quitan esas pistas vacias: `ps2::Posar` deja entonces el eje de reposo del #AMO (la posicion/giro con la que se modelo
// cada pieza). Las pistas con algun valor si se aplican tal cual (la del hueso raiz STC_BASE lleva la trayectoria, y las
// de KEMURI_BASE1/2 y las nubes su animacion).
void LimpiarPistasVacias(ps2::Anim& a) {
  for (auto& k : a.pos) {
    bool cero = true;
    for (const auto& q : k)
      if (q[1] != 0.0f || q[2] != 0.0f || q[3] != 0.0f) { cero = false; break; }
    if (cero) k.clear();
  }
  for (auto& k : a.ang) {
    bool cero = true;
    for (const auto& q : k)
      if (q[1] != 0 || q[2] != 0 || q[3] != 0) { cero = false; break; }
    if (cero) k.clear();
  }
}
// DS148: en los #AMM de los props la pista de posicion es un DESPLAZAMIENTO sobre el eje de reposo (el coche esta
// modelado en la carretera: STC_BASE p = (-368,2; 11,5; 155,1), y su #AMM empieza en (0,0,0)). ps2::Posar la aplica
// como absoluta y lo mandaba al origen del ring; sumar la posicion de reposo del hueso lo devuelve a la carretera.
// Los giros siguen absolutos (el STC_BASE ya trae 60 grados en el reposo y en la pista; el humo, su base).
void SumarReposo(ps2::Anim& a, const ps2::Esqueleto& esq) {
  for (size_t i = 0; i < a.pos.size() && i < esq.local.size(); ++i)
    if (!a.pos[i].empty())
      for (auto& k : a.pos[i]) {
        k[1] += esq.local[i].m[3];
        k[2] += esq.local[i].m[7];
        k[3] += esq.local[i].m[11];
      }
}
void CargarPropsLlegada(std::filesystem::path iso) {
  AfsPs2 afs;
  bool abierto = false;
  for (const char* n : {"DATA_EN.AFS", "DATA_US.AFS", "DATA_USA.AFS"}) abierto = abierto || afs.Abrir(iso, n);
  PropsLlegada& pl = g_pl;
  if (abierto) {
    const std::vector<uint8_t> paq = afs.Entrada(2372);   // el paquete de la escena de los Juegos de Cell
    if (!paq.empty()) {
      {   // coche: #AMO 10 + #AMT 12 + #AMM 11 (la pista de STC_BASE es la trayectoria)
        const auto amo = HijoAmb(paq, 10), amt = HijoAmb(paq, 12);
        if (HuesoDe(amo, "STC_BASE") >= 0) {
          pl.car.m = ps2::CargarSuelto(amo, amt, true, false, false);
          if (pl.car.m.esq) {
            NormalizarPesosB1(*pl.car.m.esq);
            pl.car.clips = {{AnimB1(amo, HijoAmb(paq, 11), 0, pl.car.m.esq->padre.size()), 0}};
            LimpiarPistasVacias(pl.car.clips[0].an);
            SumarReposo(pl.car.clips[0].an, *pl.car.m.esq);
            pl.car.ok = bool(pl.car.m.malla) && bool(pl.car.clips[0].an);
            pl.car.g0 = 0;
            pl.car.g1 = 1 << 30;
          }
        }
      }
      {   // humo: #AMO 13 + #AMT 14 + #AMM 15 + #AMP 16 (tramo de los cuadros)
        const auto amo = HijoAmb(paq, 13), amt = HijoAmb(paq, 14);
        if (HuesoDe(amo, "KEMURI_NULL") >= 0) {
          pl.humo.m = ps2::CargarSuelto(amo, amt, true, false, false);
          if (pl.humo.m.esq) {
            NormalizarPesosB1(*pl.humo.m.esq);
            pl.humo.clips = {{AnimB1(amo, HijoAmb(paq, 15), 0, pl.humo.m.esq->padre.size()), 0}};
            LimpiarPistasVacias(pl.humo.clips[0].an);
            SumarReposo(pl.humo.clips[0].an, *pl.humo.m.esq);
            pl.humo.ok = bool(pl.humo.m.malla) && bool(pl.humo.clips[0].an);
            pl.humo.g0 = 0;
            pl.humo.g1 = 1 << 30;
          }
        }
        const auto amp = HijoAmb(paq, 16);
        auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amp.size()) std::memcpy(&v, &amp[o], 4); return v; };
        int n = 0;
        for (size_t i = 0; i < 16; ++i) {   // tabla de registros en +0x38 (16 palabras); el registro empieza 4 B despues
          const uint32_t off = u32(0x38 + 4 * i);
          if (!off || off + 0x1C > amp.size()) continue;
          const uint32_t a = u32(off + 4 + 0x10), b = u32(off + 4 + 0x14);
          if (a < b && b <= 4096) pl.humo_g0 = int(a), pl.humo_g1 = int(b), ++n;
        }
        REXLOG_INFO("dbz3 pasarela: humo de la llegada: #AMP 2372/16 leido ({} registros; cuadros {}..{})", n, pl.humo_g0, pl.humo_g1);
      }
    }
  }
  REXLOG_INFO("dbz3 pasarela: props de la llegada de B1: coche {} ({} tri), humo {} ({} tri)", pl.car.ok,
              pl.car.m.malla.tris.size(), pl.humo.ok, pl.humo.m.malla.tris.size());
  // DS148: tramos del #AMC/#AMM/#AMP (60 Hz). Coche y humo son la toma 4 (G 2030..2430): los dos #AMM duran 400
  // cuadros y el #AMP da 119..399; en el cuadro 400 B1 corta a la toma de Satan y ya no los dibuja.
  REXLOG_INFO("dbz3 pasarela: props de la llegada: coche #AMM 11 (400 cuadros; toma 4 del #AMC, G 2030..2430), humo "
              "#AMP 16 cuadros {}..{} (solo la toma 4; despues no se dibuja)",
              2030 + pl.humo_g0, 2030 + pl.humo_g1);
  pl.estado = 2;
}
void LeerAjustePropsLlegada() {
  static const char* f = std::getenv("DBZ3_TEST_PROPS_LLEGADA_FILE");
  static int64_t ultimo = 0;
  if (!f || NowMs() - ultimo < 1000) return;
  ultimo = NowMs();
  std::ifstream in(f);
  std::string l;
  while (std::getline(in, l)) {
    char n[16] = {};
    float v[3] = {};
    if (std::sscanf(l.c_str(), "%15s %f %f %f", n, &v[0], &v[1], &v[2]) < 4) continue;
    PiezaFinal* p = !std::strcmp(n, "car") ? &g_pl.car : !std::strcmp(n, "humo") ? &g_pl.humo : nullptr;
    if (p) p->dx = v[0], p->dy = v[1], p->dz = v[2];
  }
}
// Ajuste de prueba: DBZ3_TEST_PROPS_FILE=<fichero> con lineas "rep|cell|tarjeta dx dy dz desfase_cuadros" (marco de la escena;
// se relee cada segundo). Sin el fichero, la colocacion de los datos tal cual.
void LeerAjusteProps() {
  static const char* f = std::getenv("DBZ3_TEST_PROPS_FILE");
  static int64_t ultimo = 0;
  if (!f || NowMs() - ultimo < 1000) return;
  ultimo = NowMs();
  std::ifstream in(f);
  std::string l;
  while (std::getline(in, l)) {
    char n[16] = {};
    float v[4] = {};
    if (std::sscanf(l.c_str(), "%15s %f %f %f %f", n, &v[0], &v[1], &v[2], &v[3]) < 5) continue;
    PiezaFinal* p = !std::strcmp(n, "rep") ? &g_pf.rep : !std::strcmp(n, "cell") ? &g_pf.cell : !std::strcmp(n, "tarjeta") ? &g_pf.tarjeta : nullptr;
    if (p) p->dx = v[0], p->dy = v[1], p->dz = v[2], p->cuadros = v[3];
  }
}
// DS61: niebla de B1. Las tres funciones de estado del GS que las auditorias dejaron en NO se aplican aqui con los
// valores medidos del propio ELF en el prologo (0x22E670): near 100,0 (0x42C80000), far 350,0 (0x43AF0000) y color
// (255-0, 255-255) = negro. En B3 la niebla de la escena la pone su motor; este tinte conserva el estado de B1 sobre
// el fondo del prologo (que ya es negro, asi que no altera la imagen) y documenta el dato.
// EU 0x13EBE0 SatanFog_SetColor: fija el color de niebla (uGpffff87bc).
// EU 0x13EC20 SatanFog_SetColor2: fija los dos componentes 255-a y 255-b (fGpffff87b4/fGpffff87b8).
// EU 0x149840 SatanFog_LoadState: carga desde 0x335970 el estado de niebla/proyeccion (0x350? rect y color).
struct NieblaB1 {
  float cerca = 100.0f, lejos = 350.0f;
  float r = 0.0f, g = 0.0f, b = 0.0f;
};
constexpr NieblaB1 kNieblaPrologo{};
void NieblaB1Pintar(ImDrawList* dl, const S::Area& a, float alfa) {
  if (alfa <= 0.0f) return;
  const ImVec2 d = ImGui::GetIO().DisplaySize;
  dl->AddRectFilledMultiColor(ImVec2(0, std::max(0.0f, a.At(0, 328.0f).y)), ImVec2(d.x, d.y), IM_COL32(0, 0, 0, 0),
                              IM_COL32(0, 0, 0, 0),
                              IM_COL32(int(kNieblaPrologo.r), int(kNieblaPrologo.g), int(kNieblaPrologo.b), int(110 * alfa)),
                              IM_COL32(int(kNieblaPrologo.r), int(kNieblaPrologo.g), int(kNieblaPrologo.b), int(110 * alfa)));
}

// DS30: la camara EXACTA de la escena en curso para los props del final, con el mismo formato que ps2::CamaraCombate.
// mgr+0x60 alterna entre la camara nativa del guest y la de la escena (A3b lo midio: dos valores por cuadro) y la
// reportera temblaba. cinematica::CamaraEscena da la que escribe AplicarCamaraDirecta; el fov vertical de B3 es 0,785
// (A3b: escribir mgr+0x1C4 no cambia su proyeccion).
void CamaraDeEscena(uint8_t* base, ImVec2 pantalla, ps2::Camara& c) {
  float ojo[3], obj[3], giro = 0;
  if (!dbz3::cinematica::CamaraEscena(ojo, obj, &giro)) return;
  const float f[3] = {obj[0] - ojo[0], obj[1] - ojo[1], obj[2] - ojo[2]};
  const float lf = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (lf < 1e-6f) return;
  const float fr[3] = {f[0] / lf, f[1] / lf, f[2] / lf};
  const float d0[3] = {-fr[2], 0.0f, fr[0]};   // Cruz(f, {0,1,0})
  const float ld = std::sqrt(d0[0] * d0[0] + d0[2] * d0[2]);
  if (ld < 1e-6f) return;
  const float dr[3] = {d0[0] / ld, 0.0f, d0[2] / ld};
  const float u0[3] = {dr[1] * fr[2] - dr[2] * fr[1], dr[2] * fr[0] - dr[0] * fr[2], dr[0] * fr[1] - dr[1] * fr[0]};
  const float cg = std::cos(giro), sg = std::sin(giro);
  const float d[3] = {dr[0] * cg - u0[0] * sg, dr[1] * cg - u0[1] * sg, dr[2] * cg - u0[2] * sg};
  const float u[3] = {u0[0] * cg + dr[0] * sg, u0[1] * cg + dr[1] * sg, u0[2] * cg + dr[2] * sg};
  const float a[3] = {-fr[0], -fr[1], -fr[2]};
  // EU 0x13DE80 SatanView_SetMatrix: compone la matriz de vista 4x4 con las columnas derecha/arriba/atras y la
  // traslacion del ojo (lo mismo que deja en _DAT_00352010..40 y aplica 0x145330 para proyectar el objeto del
  // prologo). Aqui se reutiliza para la camara de los props del final: misma base y misma traslacion.
  const float m[16] = {d[0],      u[0],      a[0],      0.0f, d[1],      u[1],      a[1],      0.0f,
                       d[2],      u[2],      a[2],      0.0f, -d[0] * ojo[0] - d[1] * ojo[1] - d[2] * ojo[2],
                       -u[0] * ojo[0] - u[1] * ojo[1] - u[2] * ojo[2], -a[0] * ojo[0] - a[1] * ojo[1] - a[2] * ojo[2], 1.0f};
  for (int i = 0; i < 16; ++i) c.m[i] = m[i];
  c.fov = 0.785f;
  (void)base;
  (void)pantalla;
}
void DibujarPropsFinal(ImGuiIO& io) {   // hilo de la interfaz, cada cuadro
  PropsFinal& pf = g_pf;
  const int64_t t0 = pf.t0.load();
  const bool act = dbz3::cinematica::Activa();
  // DS31: negro desde que se pide el combate del final hasta que empieza la escena. B1 pasa del cartel de victoria
  // a Cell tumbado sin ensenar nada: se tapan asi el destello verde de la carga del combate de B3 (00:01-02) y el
  // READY con los dos en guardia (00:03-04). Al empezar la escena entra desde negro en 0,6 s (DS30: corte limpio).
  if (pf.activo.load()) {
    float al = 1.0f;
    if (act && t0) al = std::clamp(1.0f - float(NowMs() - t0) / 600.0f, 0.0f, 1.0f);
    if (al > 0.0f) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * al)));
  }
  if (!pf.activo.load() || pf.estado.load() != 2 || !act || t0 == 0) return;
  uint8_t* base = Base();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  if (!mgr) return;
  for (PiezaFinal* p : {&pf.rep, &pf.cell, &pf.tarjeta})
    if (p->ok && !p->subida) p->tx = ps2::Texturas(p->m), p->subida = true;
  LeerAjusteProps();
  ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
  if (!cam.ok) return;
  CamaraDeEscena(base, io.DisplaySize, cam);   // DS30: la exacta de la escena, si la hay (final 3D)
  const uint32_t x1 = mgr + kX0, x2 = mgr + kX0 + kXPaso;
  const ps2::V3 p1{LoadF(base, x1 + 48), LoadF(base, x1 + 52), LoadF(base, x1 + 56)}, p2{LoadF(base, x2 + 48), LoadF(base, x2 + 52), LoadF(base, x2 + 56)};
  const float dx = p2.x - p1.x, dz = p2.z - p1.z, L = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
  const ps2::V3 eje{dx / L, 0, dz / L}, lado{-eje.z, 0, eje.x}, c{(p1.x + p2.x) * 0.5f, p1.y, (p1.z + p2.z) * 0.5f};
  const float ts = float(NowMs() - pf.t0.load()) / 1000.0f;
  // DS31: ultimo primer plano (G 2920-3170 = 48,7-52,8 s): se guarda el fotograma para la cuenta de puntos de B1.
  if (ts > 52.2f) CapturarFotogramaFinal();
  {   // sonda (DBZ3_TEST_PROPS_LOG=<ms>): el reloj de la escena, para capturar en el mismo instante que los savestates del original
    static const int ms = [] { const char* v = std::getenv("DBZ3_TEST_PROPS_LOG"); const int n = v ? std::atoi(v) : 0; return n <= 0 ? 0 : n == 1 ? 1000 : std::max(n, 50); }();
    static int64_t ult = 0;
    if (ms && NowMs() - ult > ms) {
      ult = NowMs();
      REXLOG_INFO("dbz3 pasarela: prop escena t {:.1f} s (suelo de los luchadores y = {:.2f} / {:.2f})", ts, p1.y, p2.y);
    }
  }
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  static ps2::Malla pose;
  for (PiezaFinal* p : {&pf.cell, &pf.rep, &pf.tarjeta}) {
    if (!p->ok) continue;
    const float g = (ts - kFinalOffsetS) * 60.0f + p->cuadros;   // cuadro global de la escena
    // DS31: cada pieza solo en el tramo en que B1 la dibuja (Cell: primer plano; reportera: hasta el folleto)
    if (int(g) < p->g0 || int(g) > p->g1) continue;
    // clip en curso: el ultimo que ya empezo (el 1.o se espera: la reportera y la tarjeta no estan antes)
    const ClipFinal* cl = nullptr;
    for (const ClipFinal& k : p->clips)
      if (g >= float(k.g0)) cl = &k;
    if (!cl) continue;
    const float f = std::min(g - float(cl->g0), float(cl->an.cuadros) - 1.0f);
    ps2::Posar(*p->m.esq, cl->an, f / 60.0f, pose);
    const ps2::V3 cen = p->m.esq->centro;
    if (p == &pf.tarjeta) {   // sin posicion (EFT_BOOK en el origen) no se pinta; el tramo va en g0/g1

      float mx = 0;
      for (const auto& t : pose.tris) mx = std::max({mx, std::fabs(t.p[0].x + cen.x), std::fabs(t.p[0].z + cen.z)});
      if (mx < 5.0f) continue;
    }
    // pivote de la escala del actor: el WAIST en el suelo (x, z de la pista del WAIST, y = suelo de B1)
    float pvx = 0, pvz = 0;
    if (p->escala != 1.0f && p->waist >= 0 && size_t(p->waist) < cl->an.pos.size() && !cl->an.pos[size_t(p->waist)].empty()) {
      const auto& ks = cl->an.pos[size_t(p->waist)];
      size_t i = 0;
      while (i + 1 < ks.size() && ks[i + 1][0] <= f) ++i;
      const size_t j = std::min(i + 1, ks.size() - 1);
      const float u = ks[j][0] > ks[i][0] ? std::clamp((f - ks[i][0]) / (ks[j][0] - ks[i][0]), 0.0f, 1.0f) : 0.0f;
      pvx = ks[i][1] + u * (ks[j][1] - ks[i][1]);
      pvz = ks[i][3] + u * (ks[j][3] - ks[i][3]);
    }
    ps2::Malla w;
    w.tris = pose.tris;
    // mundo de B1 -> marco de la camara medida (x' = z + 22,2, y' = y - 3,9, z' = -(x + 35)) -> mundo con los luchadores
    auto mundo = [&](const ps2::V3& v, bool punto) {
      float bx = v.x + (punto ? cen.x : 0.0f), bz = v.z + (punto ? cen.z : 0.0f), by = v.y;
      if (punto && p->escala != 1.0f) bx = pvx + p->escala * (bx - pvx), bz = pvz + p->escala * (bz - pvz), by = 3.9f + p->escala * (by - 3.9f);
      float rx = bz + (punto ? 22.2f + p->dx : 0.0f), ry = by - (punto ? 3.9f - p->dy : 0.0f), rz = -bx - (punto ? 35.0f - p->dz : 0.0f);
      if (punto && p == &pf.tarjeta)   // la tarjeta va en la mano de B3 (que esta mas baja que la de B1)
        if (const float* d = DeltaFinal(int(g))) rx += d[3], ry += d[4], rz += d[5];
      return ps2::V3{c.x + eje.x * rx + lado.x * rz + (punto ? 0.0f : 0.0f), c.y + ry, c.z + eje.z * rx + lado.z * rz};
    };
    if (p == &pf.tarjeta && !w.tris.empty()) {   // la tarjeta de B1 es mas grande que la mano de B3: se reduce sobre su centro (DBZ3_TARJETA_ESC)
      static const float esc = [] { const char* v = std::getenv("DBZ3_TARJETA_ESC"); return v ? float(std::atof(v)) : 0.7f; }();
      ps2::V3 cc{0, 0, 0};
      for (const auto& t : w.tris) cc = {cc.x + t.p[0].x, cc.y + t.p[0].y, cc.z + t.p[0].z};
      const float nt = float(w.tris.size());
      cc = {cc.x / nt, cc.y / nt, cc.z / nt};
      for (auto& t : w.tris)
        for (int k = 0; k < 3; ++k) t.p[k] = {cc.x + esc * (t.p[k].x - cc.x), cc.y + esc * (t.p[k].y - cc.y), cc.z + esc * (t.p[k].z - cc.z)};
    }
    for (auto& t : w.tris)
      for (int k = 0; k < 3; ++k) {
        t.p[k] = mundo(t.p[k], true);
        const ps2::V3 n = t.n[k];
        const ps2::V3 r{n.z, n.y, -n.x};   // rotacion de B1 al marco de la escena (sin traslacion)
        t.n[k] = {eje.x * r.x + lado.x * r.z, r.y, eje.z * r.x + lado.z * r.z};
      }
    if (p->alto <= 0)
      for (const auto& tr : p->m.malla.tris)
        for (const auto& v : tr.p) p->alto = std::max(p->alto, v.y);
    // DS30: contorno en PIXELES (3 px, lo medido en A3b: 3 px en la mano de Satan nativo y 4 en el traje de la
    // reportera), no un 0,9 % de la altura en unidades del mundo: en los primeros planos del final (camara de B1) ese
    // borde fijo se veia gigante. Se convierte a unidades con los px/unidad de la camara en el centro del modelo.
    float borde = std::max(p->alto, 1.0f) * 0.009f;
    {
      ps2::V3 cc{0, 0, 0};
      for (const auto& t : w.tris) cc = {cc.x + t.p[0].x, cc.y + t.p[0].y, cc.z + t.p[0].z};
      const float nt = float(std::max<size_t>(w.tris.size(), 1));
      cc = {cc.x / nt, cc.y / nt, cc.z / nt};
      ImVec2 sp;
      float kk = 0;
      if (cam.P(cc, sp, kk) && kk > 1e-3f) borde = 3.0f / kk;
    }
    toon.borde = std::clamp(borde, 0.002f, 0.2f);
    esc.Poner(cam, w, p->tx, ps2::V3{0, 0, 0}, 0.0f, 1.0f, toon);
    static const int traza_ms = [] { const char* v = std::getenv("DBZ3_TEST_PROPS_LOG"); const int n = v ? std::atoi(v) : 0; return n <= 0 ? 0 : n == 1 ? 1000 : std::max(n, 50); }();   // sonda: centro de cada pieza en pantalla, cada N ms (1 = 1000)
    static int64_t ult[3] = {};
    const int k = p == &pf.cell ? 0 : p == &pf.rep ? 1 : 2;
    if (traza_ms && NowMs() - ult[k] > traza_ms) {
      ult[k] = NowMs();
      ps2::V3 m{};
      for (const auto& t : w.tris) m.x += t.p[0].x, m.y += t.p[0].y, m.z += t.p[0].z;
      const float nt = float(std::max<size_t>(w.tris.size(), 1));
      m = {m.x / nt, m.y / nt, m.z / nt};
      float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
      for (const auto& t : w.tris)
        for (const auto& v : t.p) {
          const float q[3] = {v.x, v.y, v.z};
          for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], q[a]), hi[a] = std::max(hi[a], q[a]);
        }
      ImVec2 sp;
      float kk = 0;
      const bool vis = cam.P(m, sp, kk);
      REXLOG_INFO("dbz3 pasarela: prop {} t {:.1f} s cuadro {:.0f} clip g0 {} centro ({:.1f},{:.1f},{:.1f}) pantalla ({:.0f},{:.0f}) vis {} caja x {:.1f}..{:.1f} y {:.1f}..{:.1f} z {:.1f}..{:.1f}", k == 0 ? "cell" : k == 1 ? "reportera" : "tarjeta", ts, g, cl->g0, m.x, m.y, m.z, sp.x, sp.y, vis, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    }
  }
  esc.Tapar(cam, base, x1);
  esc.Pintar(ImGui::GetBackgroundDrawList());
}
// DS148: toma del #AMC de la llegada (clips 4..10) a la que pertenece un cuadro local (0 = cuadro global 2030).
// Cortes del #AMC 2372/20 medidos en DS143 (600/350/230/850/400/890/160/340/660/620/222 cuadros): globales 2030,
// 2430, 3320, 3480, 3820, 4480, 5100 y 5322. La llegada son 3292 cuadros = 54,87 s.
struct TomaLlegada {
  int clip = 4, g0 = 0, g1 = 400;
  const char* nombre = "llegada: el coche llega y frena";
};
TomaLlegada TomaDeLlegada(float g) {
  static constexpr int kCorte[8] = {0, 400, 1290, 1450, 1790, 2450, 3070, 3292};   // relativos al 2030
  static constexpr const char* kNombre[7] = {
      "llegada: el coche llega y frena", "llegada: Satan baja del coche", "llegada: botas a cara",
      "llegada: plano ancho", "llegada: primer plano 1", "llegada: primer plano 2", "llegada: retrato"};
  const int gg = int(std::max(0.0f, g));
  for (int i = 0; i < 7; ++i)
    if (gg < kCorte[i + 1]) return {4 + i, kCorte[i], kCorte[i + 1], kNombre[i]};
  return {10, kCorte[7], kCorte[7], kNombre[6]};
}
// DS145/DS148: coche y humo de la llegada (EscenaCellLarga), en el mundo de B1 con la camara exacta de la escena. Se
// pintan con ps2_modelo (texturas del #AMT del paquete y z-buffer del juego) y solo durante la escena de la llegada
// (54,87 s). DS148 deja una linea por toma y objeto (posicion, tamano y si se dibuja) y corta el humo en el ultimo
// cuadro del #AMP (399): B1 no lo dibuja despues de la toma del coche.
void DibujarPropsLlegada(ImGuiIO& io) {
  PropsLlegada& pl = g_pl;
  if (!pl.activo.load() || pl.estado.load() != 2 || !dbz3::cinematica::Activa()) return;
  const int64_t t0 = pl.t0.load();
  if (!t0) return;
  const float ts = float(NowMs() - t0) / 1000.0f;
  if (ts < 0.0f || ts > 55.0f) return;   // 5322-2030 = 3292 cuadros = 54,87 s; despues van las tomas del READY
  uint8_t* base = Base();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  if (!mgr) return;
  for (PiezaFinal* p : {&pl.car, &pl.humo})
    if (p->ok && !p->subida) p->tx = ps2::Texturas(p->m), p->subida = true;
  LeerAjustePropsLlegada();
  ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
  if (!cam.ok) return;
  CamaraDeEscena(base, io.DisplaySize, cam);
  ps2::Escena esc;
  esc.Empezar();
  // DS145: las tomas de la llegada anclan la camara a Satan; el coche/humo van en el mundo de B1, asi que se desplazan
  // lo mismo que la camara (si no, el ancla los sacaria del encuadre).
  float ancla[3] = {};
  const bool con_ancla = dbz3::cinematica::AnclaDelta(ancla);
  const float g = ts * 60.0f;   // cuadro local del clip 4 (el primero es el 2030 del #AMC)
  const TomaLlegada toma = TomaDeLlegada(g);
  // DS148: el coche y el humo son la toma 4 del #AMC (G 2030..2430): el #AMM 11 y el #AMM 15 duran 400 cuadros y el
  // #AMP 16 da el tramo 119..399. En el cuadro 400 B1 corta a la toma de Satan (2430) y ya no los dibuja: se cortan
  // los dos ahi (no se quedan en las tomas siguientes, que es lo que hacia DS145 con el humo).
  auto pintar = [&](PiezaFinal& p, int g0, int g1, bool translucido, int k) -> bool {
    if (!p.ok) return false;
    if (g < float(g0) || g > float(g1)) return false;
    const float f = std::clamp(g - float(p.clips[0].g0), 0.0f, float(p.clips[0].an.cuadros) - 1.0f);
    ps2::Malla pose;
    ps2::Posar(*p.m.esq, p.clips[0].an, f / 60.0f, pose);
    // Posar quita el centro x/z del modelo de reposo (los actores lo tienen en 0; el coche y el humo no): se devuelve
    const ps2::V3 ce = p.m.esq->centro;
    for (auto& t : pose.tris)
      for (int c = 0; c < 3; ++c)
        t.p[c] = {t.p[c].x + ce.x + p.dx + (con_ancla ? ancla[0] : 0.0f), t.p[c].y + p.dy + (con_ancla ? ancla[1] : 0.0f),
                  t.p[c].z + ce.z + p.dz + (con_ancla ? ancla[2] : 0.0f)};
    if (translucido)
      for (auto& t : pose.tris) t.trans = true;   // el humo del #AMT lleva alfa (Escena::Poner lo mezcla)
    // DS148: caja y centro reales de lo que se va a pintar (posicion y escala en unidades del mundo de B3).
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const auto& t : pose.tris)
      for (const auto& v : t.p) {
        const float q[3] = {v.x, v.y, v.z};
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], q[a]), hi[a] = std::max(hi[a], q[a]);
      }
    const ps2::V3 cen{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    ImVec2 sp{};
    float kk = 0;
    const bool vis = cam.P(cen, sp, kk);
    static int ult[2] = {-1, -1};
    if (ult[k] != toma.clip) {
      ult[k] = toma.clip;
      REXLOG_INFO("dbz3 pasarela: objeto {} toma {} '{}' (G {}..{}) local {:.0f}: centro ({:.1f},{:.1f},{:.1f}) "
                  "tamano ({:.1f},{:.1f},{:.1f}) pantalla ({:.0f},{:.0f}) vis {} clip {} cuadro {:.0f}",
                  k == 0 ? "coche" : "humo", toma.clip, toma.nombre, 2030 + toma.g0, 2030 + toma.g1, g, cen.x, cen.y,
                  cen.z, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], sp.x, sp.y, vis, p.clips[0].g0, f);
    }
    // DS168: primera vez que el objeto entra en cuadro en esta toma (el coche empieza detras de la camara, como en
    // B1: se le ve al pasar la camara por su lado, ~cuadro 104 del clip 4). Sirve para comprobar la toma en el log.
    static int visto[2] = {-1, -1};
    if (vis && visto[k] != toma.clip) {
      visto[k] = toma.clip;
      REXLOG_INFO("dbz3 pasarela: objeto {} entra en cuadro en la toma {} local {:.0f} (pantalla {:.0f},{:.0f})",
                  k == 0 ? "coche" : "humo", toma.clip, g, sp.x, sp.y);
    }
    if (!vis && visto[k] == toma.clip) visto[k] = -1;   // si vuelve a salir, la proxima entrada se registra
    esc.Poner(cam, pose, p.tx, ps2::V3{0, 0, 0}, 0.0f, 1.0f);
    return true;
  };
  const bool con_coche = pintar(pl.car, 0, 399, false, 0);
  const bool con_humo = pintar(pl.humo, pl.humo_g0, pl.humo_g1, true, 1);
  {   // DS148: si un objeto no se dibuja en esta toma, tambien queda una linea (para separarlo de los del guest).
    static int ult_fuera[2] = {-1, -1};
    const bool fuera[2] = {!con_coche, !con_humo};
    for (int k = 0; k < 2; ++k)
      if (fuera[k] && ult_fuera[k] != toma.clip && (toma.clip != 4 || (k == 0 ? !pl.car.ok : !pl.humo.ok))) {
        ult_fuera[k] = toma.clip;
        const bool existe = k == 0 ? pl.car.ok : pl.humo.ok;
        REXLOG_INFO("dbz3 pasarela: objeto {} toma {} '{}' (G {}..{}) no dibujado: {}", k == 0 ? "coche" : "humo",
                    toma.clip, toma.nombre, 2030 + toma.g0, 2030 + toma.g1,
                    !existe ? "no cargado" : k == 1 ? "fuera del tramo del #AMP" : "fuera de su toma");
      }
  }
  esc.Tapar(cam, base, mgr + kX0);
  esc.Pintar(ImGui::GetBackgroundDrawList());
  {   // sonda: centro de cada prop en pantalla (DBZ3_TEST_PROPS_LLEGADA_LOG=<ms>)
    static const int ms = [] { const char* v = std::getenv("DBZ3_TEST_PROPS_LLEGADA_LOG"); const int n = v ? std::atoi(v) : 0; return n <= 0 ? 0 : n == 1 ? 1000 : std::max(n, 50); }();
    static int64_t ult = 0;
    if (ms && NowMs() - ult > ms) {
      ult = NowMs();
      float ojo[3] = {}, obj[3] = {}, giro = 0.0f;   // DS168: ojo real aplicado (para ver que no baja bajo el suelo)
      const bool cam_ok = dbz3::cinematica::CamaraEscena(ojo, obj, &giro);
      REXLOG_INFO("dbz3 pasarela: props llegada t {:.1f} s (cuadro local {:.0f}, toma {}); camara escena activa {} ojo "
                  "({:.1f},{:.1f},{:.1f}) objetivo ({:.1f},{:.1f},{:.1f})", ts, g, toma.clip, cam_ok, ojo[0], ojo[1],
                  ojo[2], obj[0], obj[1], obj[2]);
    }
  }
}

// DS30: escenas "sueltas" (un combate que solo sirve para reproducir la cinematica y se cierra al acabar).
// Antes del READY van las de presentacion: en B1 el orden es cinematica -> READY? -> FIGHT; B3 pinta su READY en el
// estado 3, asi que la cinematica NO puede ir dentro del combate de verdad (saldria detras, como se veia en el puerto).
enum TipoSuelta : int { kSueltaPrevia = 0, kSueltaPresentacion = 1, kSueltaFinal = 2 };

void ArrancarMusica(const std::filesystem::path& ruta, int indice, float retraso_s, bool continuar);   // mas abajo

bool LanzarSolo(int escenario, int id1, int id2, int f1, int f2, dbz3::cinematica::Escena e, int tipo = kSueltaPrevia,
                Reglas reglas = {}) {   // con g_mu
  namespace C = dbz3::cinematica;
  const bool final = tipo == kSueltaFinal;
  // DS170: estos combates son "solo escena": B3 no tiene presentacion que ensenar (el negro tapa los estados 1-3 y la
  // camara de B1 manda desde el estado 4). Con el modo 5 del HUB se salta el vuelo de entrada del Duelo (2,65 s
  // medidos en la captura de DS168b: estados 1->2); el combate de verdad de despues sigue saliendo con el 1 y su
  // READY. El final no se toca (su entrada ya esta tapada por g_pf.t_req y lleva los props). DBZ3_SIN_MODO5_ESCENA lo
  // deja como estaba.
  if (tipo != kSueltaFinal && !std::getenv("DBZ3_SIN_MODO5_ESCENA")) dbz3::hub::BattleModeNext(5);
  if (!dbz3::hub::RequestBattleEx(id1, id2, escenario, true, 1, 0, 0, f1, f2)) {
    dbz3::hub::BattleModeNext(0);   // DS170: no dejar el modo pendiente para otro combate
    return false;
  }
  C::Empezar(std::move(e));
  if (final) {
    g_final_vista = true;
    g_pf.t_req = NowMs();   // DS31: negro desde aqui hasta que empieza la escena (tapa destello y READY)
    {   // DS31: cada final guarda su propio fotograma congelado para la cuenta de puntos
      std::lock_guard<std::mutex> lk(g_foto_mu);
      g_foto = {};
      g_foto_ok = false;
      g_foto_pedida = false;
      ++g_foto_gen;
    }
    if (g_pf.estado.load() == 0 && !std::getenv("DBZ3_SIN_PROPS_FINAL")) {   // S5-A3b: reportera, tarjeta y Cell tumbado de B1
      g_pf.estado = 1;
      std::thread(CargarPropsFinal, g_d->ruta).detach();
    }
    g_pf.activo = true;
  } else if (tipo == kSueltaPresentacion) {
    g_pres_vista = true;
  } else {
    g_previa_vista = true;
  }
  REXLOG_INFO("dbz3 pasarela: escena {} (paso {}): id {} (forma {}) contra id {} (forma {}), escenario {}, sin puntos",
              final ? "final 3D de la leyenda" : tipo == kSueltaPresentacion ? "de presentacion (antes del READY)"
                                                                          : "previa de los Juegos de Cell",
              g_paso + 1, id1, f1, id2, f2, escenario);
  CallarVozB1();
  CallarAdxPs2();
  Publicar(kLuchando);
  const uint64_t cambio = g_cambio;
  g_previa_combate = true;
  // DS148: B1 no tiene la entrada de B3 (estados 1-3: los dos en guardia, la camara por detras de Cell y sus alas en
  // primer plano). Se tapa a negro hasta que manda la camara de la escena (estado 4), igual que el fundido del final.
  if (!final) g_previa_negro = true;
  // B1 entra en la escena sin presentacion: el vuelo y las entradas de B3 (estados 1 y 2, ~20 s) se saltan con START
  // (como el jugador; hub.cpp: "START la salta") en el mando del lado 1, y lo que B3 pone tras el fin (ring out,
  // victoria, estados 5-8) se tapa con un fundido a negro (Panel) y tambien se acelera con START.
  std::thread([tipo] {
    uint8_t* base = Base();
    const int64_t t0 = NowMs();
    bool vista = false, arrancado = false, acabada = false, pulsado = false, sin_hud = false;
    int64_t t_pulso = 0;
    auto soltar = [&] {
      if (pulsado) dbz3::net::InyectarPad(0, -1);
      pulsado = false;
    };
    while (base && NowMs() - t0 < 180000) {
      const bool own = dbz3::hub::OwnBattle();
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      const uint32_t st = mgr ? REX_LOAD_U32(mgr + 35128) : 0;
      // DS168: mientras dura la escena, el combate "solo escena" no puede acabar por KO del rival ni del jugador. La
      // escena es un guion (nadie pelea): se devuelve la vida al maximo cada vuelta. El ring out de prueba lo corta
      // tagteam.cpp (DBZ3_TEST_RINGOUT, ver alli: no dispara con una cinematica activa).
      if (mgr && st == 4 && C::Activa()) {
        for (uint32_t s = 0; s < 2; ++s) {
          const uint32_t x = mgr + kX0 + kXPaso * s;
          const float vm = LoadF(base, x + kVidaMax);
          if (vm > 0 && LoadF(base, x + kVida) < vm) StoreF(base, x + kVida, vm);
        }
      }
      if (!own && (vista || acabada || NowMs() - t0 > 60000)) break;
      const bool saltar = st == 1 || st == 2 || (acabada && st >= 5 && st <= 7);
      if (saltar && NowMs() - t_pulso > (pulsado ? 120 : 400)) {
        t_pulso = NowMs();
        pulsado = !pulsado;
        dbz3::net::InyectarPad(0, pulsado ? 0x0010 : -1);
      } else if (!saltar) {
        soltar();
      }
      if (st >= 1 && st <= 3) arrancado = true;   // DS148: el estado 4 de un combate anterior no cuenta
      if (st >= 1 && st <= 3 && !sin_hud) {   // B1 no tiene marcador antes de la escena (el READY de B3 si sale)
        sin_hud = true;
        dbz3::combate::OcultarHud(true);
      }
      const bool escena_vista = st == 4 && C::Activa() && (arrancado || NowMs() - t0 > 2000);
      if (escena_vista && !vista && tipo != kSueltaFinal) {
        g_previa_negro = false;   // DS148: ya manda la camara de B1 (la entrada de B3 no es de B1)
        REXLOG_INFO("dbz3 pasarela: entrada de B3 tapada (estados 1-3); camara de B1 activa en el estado 4");
      }
      vista = vista || escena_vista;
      if (st == 4 && vista && !acabada && !C::Activa()) {
        acabada = true;
        g_previa_negro = true;
        dbz3::combate::OcultarHud(true);
        REX_STORE_U8(mgr + 35569, 1);
        // DS178: el combate "solo escena" acaba aqui; la victoria/resultado de B3 (estados 5-7, 3,2 s medidos) no es de
        // B1 y va tapada. Se salta en el gancho de la camara en cuanto el combate llega al estado 5.
        if (tipo != kSueltaFinal) C::PedirResultadoRapido();
        REXLOG_INFO("dbz3 pasarela: escena sola acabada: fin del combate de la escena");
      }
      if (st >= 5 && st <= 8 && !acabada) {   // acabo solo (el segundo de tiempo)
        acabada = true;
        g_previa_negro = true;
        dbz3::combate::OcultarHud(true);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    soltar();
    C::QuitarResultadoRapido();   // DS178: si el combate acabo sin pasar por el estado 5, no dejarlo armado
    if (acabada || sin_hud) dbz3::combate::OcultarHud(false);
    g_previa_negro = false;
    g_previa_combate = false;
  }).detach();
  Reglas r = reglas;
  r.capsulas = false;
  VigilarCombate("pasarela: escena sola", r, [cambio, tipo](const Fin& f) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fase != kLuchando || g_cambio != cambio) return;
    if (tipo == kSueltaFinal) {
      REXLOG_INFO("dbz3 pasarela: escena final 3D terminada (ganador {}); sigue el final de la leyenda", f.ganador + 1);
      g_pf.activo = false;
      DetenerMusica();
      Entrar(g_paso + 1);
      return;
    }
    REXLOG_INFO("dbz3 pasarela: {} terminada (ganador {}); sigue el combate del paso {}",
                tipo == kSueltaPresentacion ? "presentacion (antes del READY)" : "escena previa", f.ganador + 1, g_paso + 1);
    Publicar(kPantalla);   // mismo paso: DrawCombate pide el combate de verdad
  });
  return true;
}
bool LanzarPrevia(int escenario) {   // con g_mu
  dbz3::cinematica::Escena e;
  e.iso_voces = g_d->ruta;
  e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  e.distancia = 30;
  if (!EscenaPreviaLarga(e)) EscenaPrevia(e);   // DS143: los 4 clips reales del #AMC de B1; si no, el resumen medido
  return LanzarSolo(escenario, kGokuB3, kCellB3, kFormaSsj, kFormaPerfecto, std::move(e), kSueltaPrevia);
}
// DS30: la cinematica de presentacion de la ronda (en B1 solo las dos tomas de FUN_001920b0; DS143 quito la charla 3D)
// va en un combate "solo escena" ANTES del combate de verdad. Asi el READY? nativo de B3 (estado 3) sale despues de la
// cinematica, como en B1 (tomas -> tramo fijo -> READY -> FIGHT), y no antes. El combate de verdad arranca luego sin escena.
bool LanzarPresentacion(int escenario, const CombateB1& c, int ronda) {   // con g_mu
  dbz3::cinematica::Escena e;
  e.iso_voces = g_d->ruta;
  e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  e.distancia = 30;   // la tabla de B1 (0x35DFC0) pone a los dos en x = -15 / +15
  if (ronda == 0) {
    // DS143: ronda 1 = los 11 clips reales del #AMC de B1 (previa ya jugada antes) + las dos tomas de Yamcha
    if (!EscenaCellLarga(e)) {
      EscenaCell(e);   // si la ISO no trae el paquete, el resumen medido
    } else if (!std::getenv("DBZ3_SIN_PROPS_LLEGADA")) {
      // DS145: coche y humo de la llegada (paquete #2372 de la ISO, en un hilo). El primer paso fija el reloj: el
      // cuadro global 2030 del #AMC es el 0 de la llegada.
      e.pasos[0].al_empezar = [] { g_pl.t0 = NowMs(); g_pl.activo = true; };
      if (g_pl.estado.load() == 0) {
        g_pl.estado = 1;
        std::thread(CargarPropsLlegada, g_d->ruta).detach();
      }
    }
  } else {
    // DS143: B1 no tiene escena 3D de la charla (la pone en el tablero, ya reproducida): antes del READY solo van las
    // dos tomas de presentacion de FUN_001920b0. Se quita la charla 3D anadida (contenido inventado).
    TomasPresentacion(e, ronda);
  }
  static const bool sin_musica = std::getenv("DBZ3_SIN_MUSICA_B1") != nullptr;
  if (!sin_musica && e.pasos.size() >= 3)
    e.pasos[e.pasos.size() - 3].al_empezar = [ruta = g_d->ruta, m = c.musica] { ArrancarMusica(ruta, m, 0.0f, false); };
  Reglas r = ReglasDe(c, g_dificultad, ronda);
  r.capsulas = false;   // la escena no pide capsulas (las del combate las pone VigilarCombate en el combate de verdad)
  const int forma = FormaB3(c.rival, c.forma);
  return LanzarSolo(escenario, kJugador, c.rival, 0, forma, std::move(e), kSueltaPresentacion, r);
}
// S5-A3: escena final 3D de B1 (tras Cell), camara MEDIDA en PCSX2 (0x555B00, fov 1,2) pasada al marco de la escena sola:
// Mr. Satan en x = -22, Cell en +22 (distancia 44; en B1 estan a ~44 u en el eje z del mundo), suelo 3,9, ancla de Satan en el mundo de B1 (-35; -44,2) [H +-1 u].
struct PasoFinal { float seg; const std::array<float, 4>* ojo; size_t n_ojo; const std::array<float, 4>* obj; size_t n_obj; int texto; };
// Voces de toda la escena (ms desde su principio, entrada de ADX_JP.AFS; medidas con el gancho de 0x1369D0, canal 1 y 2): un solo reproductor.
constexpr int kVocesFinal[] = {-1, 1233, -590, 1234, -10740, 1235, -18000, 1236, -22900, 1238, -28910, 1239, -32780, 1240, -34300, 1241, -40510, 1243};
constexpr std::array<float, 4> kFinal0_ojo[] = {{0.0f, 21.7f, 2.0f, 9.5f}, {47.0f, 21.1f, 2.2f, 9.5f}, {144.0f, 16.4f, 3.7f, 9.0f}, {297.1f, 2.9f, 8.0f, 7.5f}};
constexpr std::array<float, 4> kFinal0_obj[] = {{0.0f, 27.6f, 1.1f, 8.7f}, {47.0f, 27.0f, 1.3f, 8.6f}, {144.0f, 22.7f, 2.6f, 8.1f}, {297.1f, 10.2f, 6.6f, 6.6f}};
constexpr std::array<float, 4> kFinal1_ojo[] = {{8.9f, 1.7f, 8.4f, 7.4f}, {202.9f, -19.2f, 15.2f, 5.4f}, {309.4f, -28.7f, 18.4f, 4.7f}, {332.7f, -28.7f, 18.4f, 4.7f}, {344.4f, -22.5f, 15.3f, 17.1f}, {668.7f, -22.4f, 15.2f, 13.8f}};
constexpr std::array<float, 4> kFinal1_obj[] = {{8.9f, 9.1f, 7.0f, 6.5f}, {273.7f, -16.9f, 15.7f, 3.6f}, {332.7f, -19.7f, 16.5f, 3.2f}, {344.4f, -21.8f, 14.9f, 3.2f}, {668.7f, -21.8f, 14.9f, 3.2f}};
constexpr std::array<float, 4> kFinal2_ojo[] = {{56.8f, -33.6f, 17.1f, 49.3f}, {115.6f, -33.5f, 17.8f, 47.7f}, {365.6f, -32.3f, 21.0f, 33.5f}, {377.3f, -13.2f, 25.4f, 20.0f}, {412.4f, -12.9f, 25.2f, 18.2f}};
constexpr std::array<float, 4> kFinal2_obj[] = {{56.8f, -17.6f, 8.2f, 4.0f}, {365.6f, -20.4f, 10.2f, 2.3f}, {377.3f, -21.3f, 12.2f, 1.4f}, {412.4f, -21.4f, 12.5f, 1.3f}};
constexpr std::array<float, 4> kFinal3_ojo[] = {{4.2f, -12.8f, 25.0f, 17.5f}, {143.3f, -11.8f, 20.8f, 8.1f}, {259.0f, -11.2f, 17.3f, 2.0f}, {305.8f, -11.0f, 16.5f, 0.8f}, {328.5f, -11.0f, 16.4f, 0.8f}, {339.9f, -15.2f, 16.4f, 7.5f}, {561.1f, -16.4f, 16.6f, 7.5f}, {583.7f, -16.4f, 16.6f, 8.8f}, {651.9f, -15.7f, 12.2f, 7.5f}};
constexpr std::array<float, 4> kFinal3_obj[] = {{4.2f, -21.4f, 12.6f, 1.2f}, {305.8f, -22.1f, 16.1f, 0.6f}, {583.7f, -22.4f, 17.2f, 1.0f}, {651.9f, -22.3f, 15.3f, 1.0f}};
constexpr std::array<float, 4> kFinal4_ojo[] = {{181.9f, -13.9f, 11.5f, 7.2f}, {342.2f, -10.2f, 11.8f, 1.2f}, {472.2f, -10.5f, 13.4f, 1.3f}, {483.5f, -13.4f, 14.8f, 1.3f}, {540.8f, -14.6f, 16.1f, 0.7f}, {552.3f, -17.2f, 16.1f, 0.5f}, {586.7f, -17.2f, 16.1f, 0.4f}};
constexpr std::array<float, 4> kFinal4_obj[] = {{181.9f, -21.9f, 15.3f, 0.5f}, {586.7f, -21.8f, 16.7f, -0.4f}};
constexpr std::array<float, 4> kFinal5_ojo[] = {{102.1f, -17.2f, 16.1f, 0.4f}};
constexpr std::array<float, 4> kFinal5_obj[] = {{102.1f, -21.8f, 16.7f, -0.4f}};
const PasoFinal kPasosFinal[] = {
    {5.00f, kFinal0_ojo, std::size(kFinal0_ojo), kFinal0_obj, std::size(kFinal0_obj), -1},
    {12.00f, kFinal1_ojo, std::size(kFinal1_ojo), kFinal1_obj, std::size(kFinal1_obj), 0},
    {7.00f, kFinal2_ojo, std::size(kFinal2_ojo), kFinal2_obj, std::size(kFinal2_obj), 1},
    {11.00f, kFinal3_ojo, std::size(kFinal3_ojo), kFinal3_obj, std::size(kFinal3_obj), 3},
    {11.00f, kFinal4_ojo, std::size(kFinal4_ojo), kFinal4_obj, std::size(kFinal4_obj), 6},
    {1.70f, kFinal5_ojo, std::size(kFinal5_ojo), kFinal5_obj, std::size(kFinal5_obj), -1},
};
// Subtitulos propios (en B1 son imagenes sin localizar). Los tramos ya no son estimaciones de capturas: siguen las voces
// medidas (kVocesFinalB1, gancho 0x1369D0) y los cortes del #AMC de DATA_EN 2374, hijo 14 (650/350/400/380/320/320/310/
// 440/2 cuadros = 10,83/5,83/6,67/6,33/5,33/5,33/5,17/7,33/0,03 s; total 52,87 s).
const char* const kTextoFinal[][2] = {
    {"¡Lo ha conseguido! ¡La Tierra está salvada!", "He-he did it! The earth is saved!"},
    {"¡Vaya! ¡Nos está saludando! ¿Nos llama?", "Wow! He's waving at us. Is he calling us over?"},
    {"¡Y ahora unas palabras del héroe en persona!", "So, let's have a few words from the hero in person!"},
    {"El Camino de Mr. Satán... Es la marca de los verdaderamente duros...", "The Way of Mr. Satan... It's the mark of the truly tough..."},
    {"Y eso es todo...", "And it's all..."},
    {"¡Aquí mismo!", "Right here!"},
    {"Ya a la venta en 100 copias de edición limitada firmada.", "Now in 100 copies of a signed limited edition."},
    {"¡Date prisa, hasta fin de existencias!", "Hurry, while stocks last!"}};   // A3b: las 8 lineas del video de B1 (voces 1233 -> L1, 1235, 1236, 1238, 1239, 1240, 1241, 1243)
// Lineas del final (cuadros globales a 60 Hz): la voz de cada una y su subtitulo medidos en la grabacion de PCSX2 (A3b\datos\eventos_b1.json:
// 1233 a 0, 1235 a 642, 1236 a 1034, 1238 a 1372, 1239 a 1743, 1240 a 1933, 1241 a 2058, 1243 a 2744; el subtitulo sigue a la voz ~0,1 s)
struct SegFinal { int g0, g1, texto; };
constexpr SegFinal kSegsFinal[] = {{0, 320, 0},    {320, 642, -1},   {642, 935, 1},    {935, 1034, -1},  {1034, 1390, 2},  {1390, 1735, 3}, {1735, 1760, -1},
                                   {1760, 1950, 4}, {1950, 2010, 5}, {2010, 2058, -1}, {2058, 2700, 6},  {2700, 2744, -1},  {2744, 2920, 7}, {2920, 3170, -1}};
// Voces (ADX_JP.AFS) y su cuadro de entrada (ms desde el principio: cuadro / 60)
constexpr int kVocesFinalB1[] = {1233, -670, 1234, -10700, 1235, -17240, 1236, -22870, 1238, -29030, 1239, -32220, 1240, -34300, 1241, -45730, 1243};

// Camara de la escena final: el #AMC del paquete (DATA_EN #2374, hijo 14: 9 clips = los de Satan, una clave por cuadro a 60 Hz; el
// corte de camara es un salto entre dos cuadros), en el marco de la camara medida de A3 (x' = z + 22,2, y' = y - 3,9, z' = -(x + 35)).
struct CamaraB1 {
  std::vector<std::array<float, 4>> ojo, obj;   // {cuadro global, x, y, z}
  std::vector<std::array<float, 2>> fov;        // {cuadro global, rad} (el del #AMC tal cual)
  int cuadros = 0;
};
bool LeerCamaraB1(const std::vector<uint8_t>& paq, CamaraB1& c) {
  const std::vector<uint8_t> amc = HijoAmb(paq, 14);
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amc.size()) std::memcpy(&v, &amc[o], 4); return v; };
  auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= amc.size()) std::memcpy(&v, &amc[o], 4); return v; };
  if (amc.size() < 0x40 || std::memcmp(amc.data(), "#AMC", 4) != 0) return false;
  const uint32_t ncl = u32(0x10), tbl = u32(0x14);
  int g0 = 0;
  for (uint32_t k = 0; k < ncl && k < 32; ++k) {
    const uint32_t nf = u32(tbl + 16 * k + 8), off = u32(tbl + 16 * k + 12);
    if (off && nf > 2) {
      auto pista = [&](uint32_t idx, int dim, auto&& poner) {
        const uint32_t p = u32(off + 4 * idx);
        if (!p) return;
        const uint32_t n = std::min<uint32_t>(u32(p + 8), 8192);
        for (uint32_t i = 0; i < n; ++i) {
          const size_t o = p + 12 + size_t(4 * (1 + dim)) * i;
          poner(g0 + int(u32(o)), o + 4);
        }
      };
      pista(0, 3, [&](int f, size_t o) { c.ojo.push_back({float(f), f32(o + 8) + 22.2f, f32(o + 4) - 3.9f, -(f32(o) + 35.0f)}); });
      pista(1, 3, [&](int f, size_t o) { c.obj.push_back({float(f), f32(o + 8) + 22.2f, f32(o + 4) - 3.9f, -(f32(o) + 35.0f)}); });
      pista(3, 1, [&](int f, size_t o) { c.fov.push_back({float(f), f32(o)}); });
    }
    g0 += int(nf);
  }
  c.cuadros = g0;
  return !c.ojo.empty() && !c.obj.empty();   // las pistas del ojo y del objetivo pueden tener claves en cuadros distintos
}

void ArrancarMusicaPista(const std::filesystem::path& ruta, int pista);   // abajo
dbz3::cinematica::Escena EscenaFinal() {   // con g_mu
  namespace C = dbz3::cinematica;
  C::Escena e;
  e.iso_voces = g_d->ruta;
  e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  e.distancia = 44;
  if (const char* cara = std::getenv("DBZ3_TEST_CAMARA")) {   // PRUEBA: camara fija ("ojo x y z objetivo x y z" en el marco de la escena), p. ej. primer plano de la cara de la reportera o del contacto de Cell con el suelo
    static std::array<float, 4> ojo{}, obj{};
    float v[6] = {-12, 14.5f, 3, -20, 14, 10.7f};
    std::sscanf(cara, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    ojo = {0.0f, v[0], v[1], v[2]};
    obj = {0.0f, v[3], v[4], v[5]};
    C::Paso p;
    p.plano = C::kLado1;
    p.segundos = 54.0f;
    if (ModFinalB1()) p.anim[0] = p.anim[1] = kCodigoFinalB1;   // la camara fija ve los dos moveset de B1 a lo largo de toda la escena
    p.rel = true;
    p.cam_ojo.assign(&ojo, &ojo + 1);
    p.cam_obj.assign(&obj, &obj + 1);
    p.cam_fov = {{0.0f, 0.785f}};   // el de B3: k = 1, solo para que actue el lazo cerrado de la camara (cinematica.cpp, fov_acerca)
    p.fov_acerca = true;
    p.al_empezar = [] { g_pf.t0 = NowMs(); ApagarAuraFinal(); };
    e.pasos.push_back(p);
    CallarB3EnEscena(e);
    return e;
  }
  // A3b: camara EXACTA del #AMC del paquete (todos los cuadros, cortes incluidos) y un paso por linea de subtitulo; si no se puede leer, los pasos medidos de A3
  static CamaraB1 camb;
  static int camb_estado = 0;   // 0 sin leer, 1 ok, 2 no
  if (camb_estado == 0) {
    camb_estado = 2;
    AfsPs2 afs;
    bool abierto = false;
    for (const char* n : {"DATA_EN.AFS", "DATA_US.AFS", "DATA_USA.AFS"}) abierto = abierto || afs.Abrir(g_d->ruta, n);
    if (abierto && LeerCamaraB1(afs.Entrada(2374), camb) && camb.cuadros >= 3000) camb_estado = 1;
    REXLOG_INFO("dbz3 pasarela: camara del final de B1 (#AMC de DATA_EN 2374): {} ({} claves, {} cuadros)", camb_estado == 1 ? "leida" : "no disponible",
                camb.ojo.size(), camb.cuadros);
  }
  static const float fov_final = [] {
    const char* v = std::getenv("DBZ3_TEST_FOV");   // prueba: fov vertical de B1 (la banda visible de 480 de 720 filas tiene este angulo)
    const float fb1 = v ? float(std::atof(v)) : kFovVerticalB1;
    return std::getenv("DBZ3_FINAL_BANDAS") ? 2.0f * std::atan(std::tan(fb1 * 0.5f) * 1.5f) : fb1;   // sin bandas fb1 es el fov vertical de la imagen entera (el 0,611 del #AMC)   // con bandas de cine (las 2/3 visibles valen fb1) o sin ellas (toda la imagen vale fb1)
  }();
  if (camb_estado == 1) {
    bool primero2 = true;
    for (const SegFinal& s : kSegsFinal) {
      C::Paso p;
      p.plano = C::kLado1;
      p.segundos = float(s.g1 - s.g0) / 60.0f;
      p.rel = true;
      // si la toma mira a la cabeza de Satan de B1 (a menos de 2,5 u), se mueve ojo y objetivo lo que la cabeza de B3 difiere
      auto desv = [&](const std::array<float, 4>& k, const std::array<float, 4>* obj_k) {
        const float* d = DeltaFinal(int(k[0]));
        if (!d || !obj_k) return std::array<float, 3>{0, 0, 0};
        const float dd = std::hypot(std::hypot((*obj_k)[1] - d[6], (*obj_k)[2] - d[7]), (*obj_k)[3] - d[8]);
        return dd < 2.5f ? std::array<float, 3>{d[0], d[1], d[2]} : std::array<float, 3>{0, 0, 0};
      };
      auto cerca = [&](float g) {   // clave del objetivo mas cercana al cuadro g
        const std::array<float, 4>* r = nullptr;
        for (const auto& o : camb.obj)
          if (!r || std::fabs(o[0] - g) < std::fabs((*r)[0] - g)) r = &o;
        return r;
      };
      float ratio_fov = 1.0f;   // distancia real / distancia puesta en los primerisimos planos: el fov se estrecha igual
      for (const auto& k : camb.ojo)
        if (k[0] >= s.g0 && k[0] <= s.g1) {
          const auto dv = desv(k, cerca(k[0]));
          // primerisimo final (G >= 2900): la cara de B3 queda mas abajo respecto a su hueso de la cabeza que la de B1 (con el hueso centrado se ve el pelo): se baja la toma
          static const float dy_final = [] { const char* v = std::getenv("DBZ3_L8_DY"); return v ? float(std::atof(v)) : 0.0f; }();
          std::array<float, 4> e = {k[0] - s.g0, k[1] + dv[0], k[2] + dv[1] + (k[0] >= 2900 ? dy_final : 0.0f), k[3] + dv[2]};
          // la cabeza (y el pelo) de Satan de B3 es mas grande que la de B1: en los primerisimos planos (< 6 u del objetivo) el ojo quedaba DENTRO del pelo (negro, L8);
          // se aleja hasta 6 u (DBZ3_FINAL_DIST_MIN) y el fov (por distancia) se compensa en cinematica con el mismo factor
          if (const auto* ob = cerca(k[0])) {
            static const float dmin = [] { const char* v = std::getenv("DBZ3_FINAL_DIST_MIN"); return v ? float(std::atof(v)) : 0.0f; }();
            const float dx = e[1] - (*ob)[1], dy = e[2] - (*ob)[2], dz = e[3] - (*ob)[3], d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 0.01f && d < dmin) {
              e[1] = (*ob)[1] + dx * dmin / d, e[2] = (*ob)[2] + dy * dmin / d, e[3] = (*ob)[3] + dz * dmin / d;
              ratio_fov = std::min(ratio_fov, d / dmin);
            }
          }
          p.cam_ojo.push_back(e);
        }
      for (const auto& k : camb.obj)
        if (k[0] >= s.g0 && k[0] <= s.g1) {
          const auto dv = desv(k, &k);
          static const float dy_final2 = [] { const char* v = std::getenv("DBZ3_L8_DY"); return v ? float(std::atof(v)) : 0.0f; }();
          p.cam_obj.push_back({k[0] - s.g0, k[1] + dv[0], k[2] + dv[1] + (k[0] >= 2900 ? dy_final2 : 0.0f), k[3] + dv[2]});
        }
      bool primerisimo = false;   // algun ojo a menos de 6 u del objetivo: B1 pone la camara a ~2,6 u de la cara; con el fov de B1 imitado por distancia el ojo de B3 entraria en la cabeza (pelo negro): se queda el fov de B3 (k = 1, toma algo mas abierta)
      for (const auto& k : camb.ojo)
        if (k[0] >= s.g0 && k[0] <= s.g1)
          if (const auto* ob = [&]() { const std::array<float, 4>* r = nullptr; for (const auto& o : camb.obj) if (!r || std::fabs(o[0] - k[0]) < std::fabs((*r)[0] - k[0])) r = &o; return r; }())
            if (std::hypot(std::hypot(k[1] - (*ob)[1], k[2] - (*ob)[2]), k[3] - (*ob)[3]) < 6.0f) primerisimo = true;
      p.cam_fov = {{0.0f, primerisimo && !std::getenv("DBZ3_FINAL_FOV_B1_CERCA") ? 0.785f : 2.0f * std::atan(std::tan(fov_final * 0.5f) * ratio_fov)}};
      p.fov_acerca = true;
      p.ocultar_lado2 = s.g0 >= 642;   // DS31: el clip de Cell de B1 acaba en 650; despues B1 no lo dibuja
      p.estilo_b1 = !std::getenv("DBZ3_FINAL_BANDAS");   // sin bandas: la imagen entera (fov vertical = el de B1, ver fov_final)
      if (s.texto >= 0) p.linea = T::T(kTextoFinal[s.texto][0], kTextoFinal[s.texto][1]);
      if (primero2) {
        if (ModFinalB1()) p.anim[0] = p.anim[1] = kCodigoFinalB1;
        p.voces.assign(std::begin(kVocesFinalB1), std::end(kVocesFinalB1));
        p.al_empezar = [ruta = g_d->ruta] {
          ArrancarMusicaPista(ruta, 1244);   // B1: musica 1244 de ADX_JP.AFS
          g_pf.t0 = NowMs();
          ApagarAuraFinal();   // DS31: el aura verde del rival (carga de ki del intro que se salta)
          if (uint8_t* base = Base(); base && g_pf.cell.ok) {   // Cell tumbado de B1 como prop (sin el mod de poses): el luchador de B3 no se dibuja
            const uint32_t x2 = REX_LOAD_U32(kMgrPtr) + kX0 + kXPaso;
            REX_STORE_U8(x2 + 3261, 1);
            REX_STORE_U8(x2 + 4925, 1);
          }
        };
      }
      primero2 = false;
      e.pasos.push_back(p);
    }
    // SONDA S1 (DBZ3_TEST_CAMARA_CONGELA=<segundos de escena>): la camara del #AMC en ese instante, congelada en
    // todos los pasos (un primer plano fijo para capturar la cara mientras se barre el campo de la expresion con
    // DBZ3_TEST_CARA). Se toma la clave mas cercana de cada pista; el reloj de la escena sigue corriendo.
    if (const char* cg = std::getenv("DBZ3_TEST_CAMARA_CONGELA")) {
      const float g = float(std::atof(cg)) * 60.0f;
      auto cerca = [&](const std::vector<std::array<float, 4>>& k) -> std::array<float, 4> {
        std::array<float, 4> r = {0, -12, 14.5f, 3};
        for (const auto& o : k)
          if (std::fabs(o[0] - g) < std::fabs(r[0] - g)) r = o;
        return r;
      };
      const auto ojo = cerca(camb.ojo), obj = cerca(camb.obj);
      float fov = fov_final, mejor = 1e30f;
      for (const auto& o : camb.fov)
        if (std::fabs(o[0] - g) < mejor) mejor = std::fabs(o[0] - g), fov = o[1];
      for (C::Paso& p : e.pasos) {
        p.cam_ojo = {{0.0f, ojo[1], ojo[2], ojo[3]}};
        p.cam_obj = {{0.0f, obj[1], obj[2], obj[3]}};
        p.cam_fov = {{0.0f, fov}};
        p.fov_acerca = true;
        p.estilo_b1 = true;
      }
      REXLOG_INFO("dbz3 pasarela: SONDA camara congelada en {:.2f} s: ojo ({:.2f} {:.2f} {:.2f}) objetivo ({:.2f} {:.2f} {:.2f}) fov {:.3f}",
                  double(g) / 60.0, ojo[1], ojo[2], ojo[3], obj[1], obj[2], obj[3], fov);
    }
    CallarB3EnEscena(e);
    return e;
  }
  bool primero = true;
  float g0_final = 0;
  for (const auto& f : kPasosFinal) {
    C::Paso p;
    p.plano = C::kLado1;
    p.segundos = f.seg;
    p.rel = true;
    p.ocultar_lado2 = g0_final >= 642;   // DS31: Cell solo en el primer plano (clip de 650 cuadros de B1)
    g0_final += f.seg * 60.0f;
    p.cam_ojo.assign(f.ojo, f.ojo + f.n_ojo);
    p.cam_obj.assign(f.obj, f.obj + f.n_obj);
    p.cam_fov = {{0.0f, kFovB1}};
    if (primero && ModFinalB1()) p.anim[0] = p.anim[1] = kCodigoFinalB1;   // las dos poses de B1 arrancan a la vez y duran toda la escena (3170 cuadros)
    if (f.texto >= 0) p.linea = T::T(kTextoFinal[f.texto][0], kTextoFinal[f.texto][1]);

    if (primero)
      p.al_empezar = [ruta = g_d->ruta] {
        ArrancarMusicaPista(ruta, 1244);   // B1: musica 1244 de ADX_JP.AFS
        g_pf.t0 = NowMs();
        ApagarAuraFinal();   // DS31: el aura verde del rival (carga de ki del intro que se salta)
        if (uint8_t* base = Base(); base && g_pf.cell.ok) {   // Cell tumbado de B1 como prop (sin el mod de poses): el luchador de B3 no se dibuja
          const uint32_t x2 = REX_LOAD_U32(kMgrPtr) + kX0 + kXPaso;
          REX_STORE_U8(x2 + 3261, 1);
          REX_STORE_U8(x2 + 4925, 1);
        }
      };
    primero = false;
    e.pasos.push_back(p);
  }
  CallarB3EnEscena(e);
  return e;
}


// Medido (S5-A3, volcado de audio del juego): en combate B3 no abre ningun stream de musica (0 aperturas en `voces: stream` entre el
// estado 1 y el 4) y lo que queda de audio (RMS 0,05) es grave y tonal (centroide 614 Hz, 52 % por debajo de 200 Hz) = ambiente del
// escenario; parar todos los streams (sub_82081A48(-1)) no lo cambia. No hay musica de B3 que silenciar.
void ArrancarPista(const std::filesystem::path& ruta, int pista, float retraso_s, bool continuar) {
  if (pista < 0 || ruta.empty()) return;
  if (continuar && g_mus_pista.load() == pista) return;   // los Cell Jr. seguidos: la misma pista sigue
  DetenerMusica();
  g_mus_pista = pista;
  const uint32_t ficha = g_mus_ficha.load();
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.6f;
  REXLOG_INFO("dbz3 pasarela: musica del combate: ADX_JP {} dentro de {:.1f} s", pista, retraso_s);
  std::thread([ruta, pista, vol, retraso_s, ficha] {
    std::this_thread::sleep_for(std::chrono::milliseconds(int(retraso_s * 1000)));
    if (ficha == g_mus_ficha.load()) OirMusicaPs2(ruta, "adx_jp.afs", pista, vol);
  }).detach();
}
void ArrancarMusica(const std::filesystem::path& ruta, int indice, float retraso_s, bool continuar) {
  if (indice >= 0 && indice < int(std::size(kBgmB1))) ArrancarPista(ruta, kBgmB1[indice], retraso_s, continuar);
}
void ArrancarMusicaPista(const std::filesystem::path& ruta, int pista) { ArrancarPista(ruta, pista, 0.0f, false); }

// Efectos de la interfaz de B1: DATA_<idioma>.AFS 676 = cuerpo PS-ADPCM y 677 = indice IECS (17 sonidos, 0..0x10).
// B1 los pide por FUN_00152690(0,0,id) al sistema de audio del IOP; el puerto decodifica ese mismo banco con
// OirSonidoPs2 (src/pasarela_b1.cpp). 3 = confirmar, 0xE = tic de la cuenta, 0xF = fin de la cuenta.
std::string AfsUiB1() {
  // El AFS que el lector abrio de verdad (DatosB1::afs = "/USR/DATA_XX.AFS #banco"); si no, por idioma.
  const size_t bar = g_d->afs.find_last_of("/\\");
  const size_t fin = g_d->afs.find(' ', bar == std::string::npos ? 0 : bar);
  if (bar != std::string::npos && fin != std::string::npos && fin > bar + 1) return g_d->afs.substr(bar + 1, fin - bar - 1);
  const std::string& l = g_d->idioma;
  if (l == "es") return "data_sp.afs";
  if (l == "fr") return "data_fr.afs";
  if (l == "de") return "data_ge.afs";
  if (l == "it") return "data_it.afs";
  return "data_en.afs";
}
void SeB1(int id, int tipo = 5) {
  if (!g_d || g_d->ruta.empty()) return;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  OirSonidoPs2(g_d->ruta, AfsUiB1(), 676, id, tipo, vol);
}

bool Lanzar() {   // con g_mu: el combate del paso actual, con las reglas del original
  const Paso& p = g_pasos[size_t(g_paso)];
  const CombateB1& c = g_d->combates[size_t(p.ronda)];
  const int escenario = EscenarioB3(c.escenario_nombre);
  // S1-F03.1: antes del combate 1, la escena previa de los Juegos de Cell (una vez por paso; no con DBZ3_SIN_CINE)
  static const bool sin_cine = std::getenv("DBZ3_SIN_CINE") != nullptr || std::getenv("DBZ3_SIN_PREVIA") != nullptr;
  if (p.ronda == 0 && p.vez == 0 && !g_previa_vista && !sin_cine) return LanzarPrevia(escenario);
  // Decision del usuario: TODOS los combates de la leyenda en el escenario nativo de los Juegos de Cell de B3 HD (mapa 50); el ring convertido de B1 queda solo para la escena previa y la final 3D
  const int escenario_cmb = EscenarioPorMapa(50);
  // PRUEBA (DBZ3_TEST_FINAL3D=1): la ultima ronda abre directamente la escena final 3D (sin pelear contra Cell)
  static const bool prueba_final = std::getenv("DBZ3_TEST_FINAL3D") != nullptr;
  if ((prueba_final || g_final_pendiente) && g_paso + 1 >= int(g_pasos.size()) && !g_final_vista) {
    g_final_pendiente = false;
    return LanzarSolo(escenario, kJugador, kCellB3, 0, kFormaPerfecto, EscenaFinal(), kSueltaFinal);
  }
  // DS30/DS143: la cinematica de la ronda (las dos tomas de B1; sin la charla 3D inventada) va en un combate solo
  // escena ANTES de este. B3
  // pinta el READY? en su estado 3 y la escena solo puede correr en el 4: si se pide aqui, el READY saldria antes de
  // la cinematica (fallo visto por el usuario). Con la escena aparte, el combate de verdad llega al READY (estado 3)
  // despues de la cinematica, como B1.
  if (p.vez == 0 && !g_pres_vista && !sin_cine) return LanzarPresentacion(escenario_cmb, c, p.ronda);
  // vida que no se recupera (Krilin, Gohan) y los Cell Jr. seguidos: la que le quedo al ganar el anterior
  const bool arrastra = c.arrastra_vida || p.vez > 0;
  const int vida1 = arrastra && g_vida_ganada > 0 ? -g_vida_ganada : 0;   // < 0: solo la inicial (maxima de serie, como B1)
  const int vida2 = c.vida_rival > 0 && c.vida_rival != 3000 ? c.vida_rival : 0;   // Cell Jr.: 1000
  int tiempo = c.tiempo > 0 ? (c.tiempo + 30) / 60 : -1;   // cuadros -> s; -1 sin tiempo
  if (c.tiempo == -2) {   // -2 = el de las opciones (Cell Jr.): G+0x2C08C (s; <= 0 sin tiempo; doc 12/10)
    uint8_t* base = Base();
    const int op = base ? int(int16_t(REX_LOAD_U16(dbz3::GA(0x824BA110) + 0x2C08C))) : 0;
    tiempo = op > 0 ? op : -1;
    REXLOG_INFO("dbz3 pasarela: tiempo -2 de B1 = el de las opciones: {} s ({})", op, op > 0 ? "con limite" : "sin limite");
  }
  const int forma2 = FormaB3(c.rival, c.forma);
  static const bool sin_musica = std::getenv("DBZ3_SIN_MUSICA_B1") != nullptr;
  // DS30: la musica arranca en la cinematica de presentacion (o aqui si se salto); con `continuar`, si ya suena la
  // misma pista no se reinicia, y en los Cell Jr. seguidos sigue de un combate al siguiente.
  if (!sin_musica) ArrancarMusica(g_d->ruta, c.musica, 0.0f, true);
  Reglas r = ReglasDe(c, g_dificultad, p.ronda);
  // Ki del jugador del registro de B1 (+0x2C): 300 = 3 barras, igual que el rival; 0xFFFF (Krilin y Gohan, las mismas
  // fichas que la vida que no se recupera) = el que traia. El hilo de los 5 Cell Jr. es un solo combate seguido en
  // B1 (cantidad de la tabla): tambien sigue de uno al siguiente.
  const bool ki_sigue = c.ki_jugador == 0xFFFF || p.vez > 0;
  std::string ki_de;
  if (ki_sigue && g_ki_ganada >= 0) {
    r.ki_jugador = float(g_ki_ganada) / kKiBarra;
    ki_de = c.ki_jugador == 0xFFFF ? "el que traia, 0xFFFF del registro" : "el que traia (Cell Jr. seguido)";
  } else if (!ki_sigue && c.ki_jugador > 0) {
    r.ki_jugador = float(c.ki_jugador) / 100.0f;
    ki_de = "registro " + std::to_string(c.ki_jugador);
  } else {
    ki_de = "sin anterior (no se toca)";
  }
  r.puntos_b1 = true;
  // EU 0x18C9D0 SatanBattle_StartScene: lanza la escena de combate de B1 (0x18CA00 + 0x18C810 + 0x1CE150); en el
  // puerto el combate lo ejecuta el motor de B3 con los dos luchadores, las reglas y el escenario de la ronda.
  // EU 0x18CA00 SatanBattle_ResetScene: suelta las tareas/efectos de la escena anterior; aqui lo hacen Salir() y
  // cinematica::Terminar al cambiar de pantalla.
  if (!dbz3::hub::RequestBattleEx(kJugador, c.rival, escenario_cmb, true, tiempo, vida1, vida2, 0, forma2)) return false;
  // DS30: antes del READY, B3 reproduce su presentacion nativa (vuelo por el escenario, estado 1, y entradas, 2, ~20 s)
  // en la que los dos luchadores se acercan solos. En B1 no existe: se salta con START (el atajo del Duelo, hub.cpp)
  // pulsando el mando del lado 1 solo en los estados 1 y 2, y se suelta al llegar al READY (estado 3), que sale entero
  // y detras de la cinematica. Asi los luchadores quedan quietos en su pose hasta que empieza la pelea.
  std::thread([] {
    uint8_t* base = Base();
    const int64_t t0 = NowMs();
    bool pulsado = false, nuevo = false;
    uint32_t st_prev = ~0u;
    int64_t t_estado = 0, t_pulso = 0;
    int pulsos = 0;
    auto soltar = [&] {
      if (pulsado) dbz3::net::InyectarPad(0, -1);
      pulsado = false;
    };
    while (base && NowMs() - t0 < 40000) {
      const bool own = dbz3::hub::OwnBattle();
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      const uint32_t st = mgr ? REX_LOAD_U32(mgr + 35128) : 0;
      if (!nuevo) {   // al pedir el combate aun puede estar el anterior (estados 5-8): se espera al nuevo
        if (!own) {
          std::this_thread::sleep_for(std::chrono::milliseconds(8));
          if (NowMs() - t0 > 20000) break;
          continue;
        }
        if (st >= 3 && st <= 4) break;   // el nuevo ya esta en READY/pelea: no hay intro que saltar
        nuevo = st <= 2;
        if (!nuevo) {
          std::this_thread::sleep_for(std::chrono::milliseconds(8));
          continue;
        }
      }
      if (st >= 3 && st <= 8) break;   // READY/pelea: el READY? se deja entero
      if (st != st_prev) {             // DS30: dos toques cortos al entrar en cada estado (1 vuelo, 2 entradas), no
        st_prev = st;                  // pulsaciones hasta el borde del READY (se lo podrian saltar)
        t_estado = NowMs();
        pulsos = 0;
        soltar();
      }
      if ((st == 1 || st == 2) && !pulsado && pulsos < 2 && NowMs() - t_estado >= (pulsos == 0 ? 60 : 500)) {
        pulsado = true;
        t_pulso = NowMs();
        ++pulsos;
        dbz3::net::InyectarPad(0, 0x0010);
      } else if (pulsado && NowMs() - t_pulso > 120) {
        soltar();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    soltar();
  }).detach();
  r.traza = std::getenv("DBZ3_PASARELA_TRAZA") != nullptr;   // pruebas: vida y ki de los dos cada segundo
  REXLOG_INFO("dbz3 pasarela: ronda {} ({}/{}): id {} contra id {} (B1 {}, forma B1 {} -> {}), escenario {} ({}), "
              "tiempo {}, vida {} / {}; IA {} (nivel COM {}), ki del rival {:.1f} barras (sin reglas inventadas: B1 solo "
              "cambia la tabla)", p.ronda + 1, p.vez + 1, c.cantidad, kJugador, c.rival, c.rival_b1, c.forma, forma2,
              escenario_cmb, c.escenario_nombre, tiempo, vida1, vida2, kDificultad[g_dificultad], r.nivel_com, r.ki_rival);
  REXLOG_INFO("dbz3 pasarela: ki del jugador del original (paso {}): {:.1f} barras ({})", g_paso + 1, r.ki_jugador, ki_de);
  dbz3::combate::OcultarHud(false);   // DS30: el combate de verdad ensena su READY (la escena de presentacion lo dejo oculto)
  g_previa_negro = false;             // y el fundido de la escena suelta no tapa el READY
  CallarVozB1();
  CallarAdxPs2();
  Publicar(kLuchando);
  const uint64_t cambio = g_cambio;
  // EU 0x1D0840 SatanBattle_UpdateData: refresca los datos del combate desde el motor (0x26C790); aqui lo hace
  // VigilarCombate cada 8 ms (vida, ki, ganador y segundos) y entrega el `Fin` a este callback.
  VigilarCombate(("pasarela: paso " + std::to_string(g_paso + 1)).c_str(), r, [cambio](const Fin& f) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fase != kLuchando || g_cambio != cambio) return;
    // EU 0x18C9D0 SatanBattle_EndScene: cierra la escena de combate (0x18CA00 + 0x18C810 + 0x1CE150); aqui se
    // cancela cualquier cinematica pendiente y se sueltan sus tareas al pasar a resultado/final.
    if (dbz3::cinematica::Activa()) dbz3::cinematica::Cancelar();
    {   // la musica sigue solo entre los Cell Jr. seguidos
      const Paso& pm = g_pasos[size_t(g_paso)];
      if (!(f.ganador == 0 && pm.vez + 1 < std::max(1, g_d->combates[size_t(pm.ronda)].cantidad))) DetenerMusica();
    }
    REXLOG_INFO("dbz3 pasarela: fin del combate (paso {}): gana el lado {}, vida de Mr. Satan {}, combo mayor {} ({} de "
                "dano)", g_paso + 1, f.ganador + 1, f.vida[0], f.combo_max, f.combo_dano_max);
    if (f.ganador == 0) {
      const int pts = PuntosB1(f);
      g_vida_ganada = f.vida[0];
      g_ki_ganada = f.ki[0];
      g_puntos += pts;
      const Paso& p = g_pasos[size_t(g_paso)];
      const bool ultima_vez = p.vez + 1 >= std::max(1, g_d->combates[size_t(p.ronda)].cantidad);
      g_ronda_pts = (p.vez == 0 ? 0 : g_ronda_pts) + pts;
      if (p.vez == 0) g_ronda_ev = g_ronda_vida = g_ronda_tiempo = 0, g_ronda_det.clear();
      g_ronda_ev += f.puntos;
      {
        std::lock_guard<std::mutex> lk2(g_suc_mu);
        for (const auto& [etiqueta, pts] : g_suc_suma) g_ronda_det[etiqueta] += pts;
      }
      g_ronda_vida += std::max(0, f.puntos + f.vida[0] - f.vida[1]) - f.puntos;   // LIFE tras el tope de 0 (PuntosB1)
      g_ronda_tiempo += f.segundos > 0 ? f.segundos * 20 : 0;
      REXLOG_INFO("dbz3 pasarela: puntos del combate {} (sucesos {}, vida {} - {}, {} s; la mitad), ronda {}, total {}",
                  pts, f.puntos, f.vida[0], f.vida[1], f.segundos, g_ronda_pts, g_puntos);
      GuardarProgreso(kClave, g_paso + 1);
      GuardarProgreso("hercule_puntos", g_puntos);
      RangoMejora("rango_satan_" + std::to_string(g_paso), 1, 25);   // escalafon: cada combate ganado, una vez
      if (ultima_vez && g_paso + 1 < int(g_pasos.size())) {
        Publicar(kResultado);   // SATAN RESULT MAIN (0x1D); tras Cell va directo al final (0x1E), que cuenta igual
      } else {
        static const bool sin_final = std::getenv("DBZ3_SIN_CINE") != nullptr || std::getenv("DBZ3_SIN_FINAL3D") != nullptr;
        if (g_paso + 1 >= int(g_pasos.size()) && !g_final_vista && !sin_final) {
          g_final_pendiente = true;   // como la escena previa: mismo paso en pantalla y Lanzar() pide la escena
          Publicar(kPantalla);
        } else {
          Entrar(g_paso + 1);
        }
      }
    } else {
      ++g_intentos;
      Publicar(kPerdido);
    }
  });
  return true;
}

void CreatePanel();

}  // namespace

// ---- API (HUB y pruebas) ----

bool Disponible() {
  if (!Datos()) Leer(false);
  return Datos() != nullptr;
}

const char* Motivo() {
  if (Disponible()) {
    return T::T("La leyenda de Mr. Satán de Budokai 1: once retadores seguidos con las charlas de tu copia del juego.",
                "Hercule's legend from Budokai 1: eleven challengers in a row, with the talks from your copy of the game.");
  }
  // pulsar A en la fila abre el dialogo para elegir la ISO (hub_menu.cpp Launch); el aviso sigue el proceso
  static thread_local std::string s;
  s = T::T("Necesita tu copia de Dragon Ball Z Budokai (PS2). Pulsa A y elige su ISO: no se copia nada.",
           "Needs your copy of Dragon Ball Z Budokai (PS2). Press A and pick its ISO: nothing is copied.");
  if (const std::string av = AvisoEleccionPs2(); !av.empty()) s += "\n" + av;
  return s.c_str();
}

// Empieza el modo (hilo del juego con el menu principal activo, o el hilo de prueba). false si no hay datos.
bool Start() {
  const auto d = Datos();
  if (!d || d->combates.empty()) {
    REXLOG_WARN("dbz3 pasarela: no se puede empezar (sin datos de Budokai 1)");
    Leer(true);
    return false;
  }
  if (d->idioma != T::LangCode()) Leer(true);   // otro idioma: se releen las imagenes (mientras, las de antes)
  if (!g_panel.exchange(true)) dbz3::launcher::DeferUi(&CreatePanel);
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_fase != kNada) return false;
  g_d = d;
  g_pasos = Pasos(*d);
  int desde = LeerProgreso(kClave);
  if (desde < 0 || desde >= int(g_pasos.size())) desde = 0;
  g_dificultad = std::clamp(LeerProgreso("hercule_dificultad", 0), 0, 2);
  g_puntos = desde > 0 ? LeerProgreso("hercule_puntos", 0) : 0;
  REXLOG_INFO("dbz3 pasarela: la leyenda de Mr. Satan ({} combates, {} pasos), desde el paso {}, {} ({} puntos)",
              d->combates.size(), g_pasos.size(), desde + 1, kDificultad[g_dificultad], g_puntos);
  Entrar(desde);
  return true;
}

bool Busy() { return g_fase_pub.load() != kNada; }

namespace {

// ---- Pantalla (hilo de la interfaz) ----
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

// DS97: claves del menu de dificultad de B1. Salen de 665 #AMA (DATA_<idioma>.AFS, banco 665, registros de 0x40 B;
// claves de 0x20 B = u32 cuadro + sx,sy,rot,px,py,pz,alfa). Registros 9/16/23 = apertura (x 0,0,0,0'03,0'7 en
// 0/16/30/31/45; y 0'7); 10/17/24 = entrada de la opcion elegida (0'7 -> 1'2 -> 0'9 en 0/4/8 y z 300->200);
// 11/18/25 = reposo de la elegida (1'0 -> 1'1 -> 1'0 en 0/10/20, bucle); 12/19/26 = salida al cambiar de opcion
// (1'15 -> 0'7 en 0/5); 15/22/29 = confirmar (1'0,1'5,1'4,1'4,0'7,1'2,0'9,1'0 en 0/3/5/6/9/13/17/21). Las tres
// opciones usan la misma forma; solo cambia la fila (y = 264, 294, 324 en px de 640x448) y el registro (7 por
// opcion). 0x22DDA0 crea la elegida con la animacion 10+7*i y la enlaza a 11+7*i (FUN_001f9a80: +0x48).
struct DifAnim {
  int clip = -1;   // registro 665 #AMA que se reproduce (-1 = abierto ya terminado)
  int next = -1;   // registro que sigue al acabar (FUN_001f9a80, campo +0x48 del sprite)
  float at = 0;    // instante de arranque
};

float Clave2(const float k[][2], int n, float f) {
  if (f <= k[0][0]) return k[0][1];
  for (int i = 1; i < n; ++i) {
    if (f <= k[i][0]) {
      const float u = (f - k[i - 1][0]) / std::max(1e-6f, k[i][0] - k[i - 1][0]);
      return k[i - 1][1] + u * (k[i][1] - k[i - 1][1]);
    }
  }
  return k[n - 1][1];
}

float EscalaDificultad(int opt, int clip, float f) {
  static constexpr float kAbre[5][2] = {{0, 0.0f}, {16, 0.0f}, {30, 0.0f}, {31, 0.03f}, {45, 0.7f}};
  static constexpr float kEntra[3][2] = {{0, 0.7f}, {4, 1.2f}, {8, 0.9f}};
  static constexpr float kReposo[3][2] = {{0, 1.0f}, {10, 1.1f}, {20, 1.0f}};
  static constexpr float kSale[2][2] = {{0, 1.15f}, {5, 0.7f}};
  static constexpr float kConfirma[8][2] = {{0, 1.0f}, {3, 1.5f}, {5, 1.4f}, {6, 1.4f},
                                            {9, 0.7f}, {13, 1.2f}, {17, 0.9f}, {21, 1.0f}};
  if (clip < 0 || clip == 9 + 7 * opt || clip == 16 + 7 * opt || clip == 23 + 7 * opt)
    return Clave2(kAbre, 5, f);
  if (clip == 10 + 7 * opt) return Clave2(kEntra, 3, f);
  if (clip == 11 + 7 * opt) return Clave2(kReposo, 3, std::fmod(std::max(0.0f, f), 20.0f));
  if (clip == 12 + 7 * opt) return Clave2(kSale, 2, f);
  if (clip == 15 + 7 * opt) return Clave2(kConfirma, 8, f);
  if (clip == 13 + 7 * opt) return 1.0f;   // 0x22DE70: la elegida al cancelar (0x1000)
  if (clip == 14 + 7 * opt) return 0.7f;   // las demas al cancelar
  return 0.7f;
}



class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    DibujarPropsFinal(io);   // S5-A3b
    DibujarPropsLlegada(io); // DS145: coche y humo de la llegada de la ronda 1
    const int fase = g_fase_pub.load();
    const bool menu = dbz3::ingame::GameScreen() == kMainScreen;
    const bool show = (fase == kPantalla || fase == kPerdido || fase == kFin || fase == kResultado) && menu &&
                      !dbz3::GameDimmed() && !dbz3::OverlayOpen();
    if (auto* in = Input(); in && show != blocked_) {   // mientras se ve, el juego no recibe los mandos
      if (show) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
      blocked_ = show;
    }
    const float t = float(dbz3::RelojT(ImGui::GetTime())), dt = dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    alpha_ = std::clamp(alpha_ + (show ? dt * 4.0f : -dt * 8.0f), 0.0f, 1.0f);
    // en el combate: puntos y avisos de los sucesos (como el anillo de avisos de B1)
    {   // S1-F03.1: fundido a negro tras la escena previa (tapa el ring out y la victoria de B3)
      negro_ = std::clamp(negro_ + (g_previa_negro.load() ? 1.0f : -2.0f) * dbz3::RelojDt(std::min(io.DeltaTime, 0.1f)) * 3.0f, 0.0f, 1.0f);
      if (negro_ > 0.0f)
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * negro_)));
    }
    if (const int st = EstadoCombate(); fase == kLuchando && st >= 3 && st <= 6 && !dbz3::GameDimmed() && !dbz3::cinematica::Activa() &&
                                        !g_previa_combate.load() && !dbz3::OverlayOpen()) {
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_d) {
        Texturas();
        DrawPuntos(ImGui::GetForegroundDrawList(), S::GameArea(io));
      }
    }
    if (!show) {
      wait_release_ = true;   // la A que abrio el modo (o la del combate) no cuenta
      if (alpha_ <= 0.0f) return;
    }
    uint16_t pressed = Pad();
    if (wait_release_) {
      if (g_pad.load() == 0 && alpha_ >= 1.0f) wait_release_ = false;
      pressed = 0;
    }
    if (!show || g_auto.load()) pressed = 0;   // prueba automatica: solo avanza el modo (teclas de mas no cuentan)
    if ((show && dbz3::ingame::tutorial::Mostrar("satan")) || dbz3::ingame::tutorial::Abierto()) pressed = 0;   // F3
    std::unique_lock<std::mutex> lk(g_mu);
    if (!g_d || g_pasos.empty()) return;
    if (g_cambio != visto_) {
      visto_ = g_cambio;
      cambio_at_ = t;
      Voz();
    }
    Texturas();
    const auto a = S::GameArea(io);
    auto* dl = ImGui::GetForegroundDrawList();
    const int v0 = dl->VtxBuffer.Size;
    // titulo y dificultad llevan su logo grande y girado (PantallaTitulo); el tablon del resultado ya lleva el suyo
    // DS77: el logo pequeno de Fondo() no se pinta durante TODO el titulo (montaje y dificultad); antes
    // reaparecia en la pantalla de dificultad al pasar titulo_ a != g_cambio (el grande ya lo pone PantallaTitulo).
    const bool titulo_b1 = g_fase != kResultado && g_fase != kFin && g_fase != kPerdido && size_t(g_paso) < g_pasos.size() &&
                           g_pasos[size_t(g_paso)].tipo == kPresentacion && elegida_ != g_cambio;
    Fondo(dl, a, g_fase != kResultado && g_fase != kFin && !titulo_b1);
    const bool auto_on = g_auto.load();
    if (g_fase == kFin) {
      DrawFin(dl, a, t, pressed, auto_on);
    } else if (g_fase == kResultado) {
      DrawResultado(dl, a, t, pressed, auto_on);
    } else if (g_fase == kPerdido) {
      DrawTablero(dl, a, t, pressed, auto_on, true);
    } else {
      const Paso& p = g_pasos[size_t(g_paso)];
      if (p.tipo == kPresentacion) DrawPresentacion(dl, a, t, pressed, auto_on);
      else if (p.tipo == kCharla) DrawTablero(dl, a, t, pressed, auto_on, false);
      else DrawCombate(t);
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
  struct Tex {
    std::unique_ptr<rex::ui::ImmediateTexture> tex;
    float w = 0, h = 0;
  };

  uint16_t Pad() {
    if (!g_pad_pending.exchange(true)) {
      dbz3::launcher::DeferUi([]() {
        uint16_t b = 0;
        if (auto* in = Input()) {
          rex::input::X_INPUT_STATE st = {};
          if (in->GetStateForUI(0, &st) == 0) {
            b = static_cast<uint16_t>(st.gamepad.buttons);
            const int ly = int16_t(st.gamepad.thumb_ly);
            if (ly > 16000) b |= kUp;
            if (ly < -16000) b |= kDown;
          }
        }
        g_pad = b;
        g_pad_pending = false;
      });
    }
    const uint16_t b = g_pad.load();
    const uint16_t pressed = b & ~last_;
    last_ = b;
    return pressed;
  }

  // Texturas de las imagenes del juego de origen (se rehacen si se releyo la ISO, p. ej. otro idioma).
  void Texturas() {   // con g_mu
    const auto d = Datos();
    if (d && d != g_d && d->combates.size() == g_d->combates.size()) g_d = d;   // idioma nuevo: mismas rondas
    if (tex_gen_ == g_generacion.load() && tex_src_ == g_d.get()) return;
    auto* drawer = dbz3::launcher::PreviewDrawer();
    if (!drawer) return;
    tex_gen_ = g_generacion.load();
    tex_src_ = g_d.get();
    tex_.clear();
    auto subir = [&](int key, const Imagen& img) {
      if (!img) return;
      Tex t;
      t.tex = drawer->CreateTexture(uint32_t(img.w), uint32_t(img.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                    img.rgba.data());
      t.w = float(img.w);
      t.h = float(img.h);
      if (t.tex) tex_[key] = std::move(t);
    };
    for (int i = 0; i < int(g_d->charlas.size()); ++i) subir(i, g_d->charlas[size_t(i)]);
    subir(kLogo, g_d->logo);
    subir(kFondo, g_d->fondo);
    subir(kSatan, g_d->satan);
    const ResultadoB1& r = g_d->res;
    subir(kTablon, r.tablon);
    subir(kRetadores, r.retadores);
    subir(kCruz, r.cruz);
    subir(kTotal, r.total);
    subir(kAhora, r.ahora);
    subir(kTienes, r.tienes);
    subir(kCifras, r.cifras);
    subir(kChibis, r.chibis);
    subir(kLetrero, r.letrero);
    subir(kPulsa, r.pulsa);
    for (int k = 0; k < 3; ++k) subir(kDif + k, r.dificultad[k]);
    subir(kBarra, r.barra);
    subir(kCopy, r.copyright);
    subir(kTienesPeq, r.tienes_peq);
    subir(kCifrasPeq, r.cifras_peq);
    subir(kClave, r.clave);
    for (int k = 0; k < 2; ++k) subir(kLetras + k, r.letras[k]);
    subir(kEstilo, r.estilo);
    subir(kCaraSatan, r.cara_satan);
    for (int k = 0; k < 6; ++k) subir(kConsejo + k, r.consejos[k]);
    // DS71: el degradado rojo del fondo del resultado es la textura 668 #5 (8x184, RGB rojo con alfa 0..0x84), que
    // no estaba en las piezas de la ficha. Se lee con los lectores que ya hay (AfsPs2/TexturaAmt) del mismo AFS/banco
    // que el resto de imagenes del resultado; si no se puede, el fondo usa el degradado de antes.
    if (!g_d->afs.empty() && !g_d->ruta.empty()) {
      const size_t hash = g_d->afs.rfind(" #");
      const int banco = hash == std::string::npos ? -1 : std::atoi(g_d->afs.c_str() + hash + 2);
      if (banco > 0) {
        const std::string afs = std::filesystem::path(g_d->afs.substr(0, hash)).filename().string();
        AfsPs2 a2;
        if (a2.Abrir(g_d->ruta, afs)) subir(kGradRes, TexturaAmt(a2.Entrada(size_t(banco) - 1), 5));
      }
    }
  }

  // Voces y jingles del original (ADX_JP.AFS; -ms = a esos ms de la entrada, los cuadros de B1); cualquier otra
  // pantalla los corta. Tablero: voz 1 a los 61 cuadros (FUN_002324c0) y la 2 MsVoz2 despues, si la charla no va en
  // la escena 3D. Los jingles del resultado y del final van en Cuenta (se mezclan y siguen en el tablero).
  void Voz() {   // con g_mu
    const Paso& p = g_pasos[size_t(std::min<int>(g_paso, int(g_pasos.size()) - 1))];
    std::vector<int> e;
    // EU 0x2324C0 SatanBoard_Voice: voz 1 del retador (tabla 0x3507F0) y voz 2 en el cuadro 0x350B10; el tablero ya
    // no comparte pantalla con la charla 3D (DS143), asi que suena aqui como en B1.
    if (g_fase == kPantalla && p.tipo == kCharla && p.ronda >= 1)
      e = {-1017, 1269 + 2 * (p.ronda - 1), -(1017 + MsVoz2(p.ronda)), 1270 + 2 * (p.ronda - 1)};
    if (e.empty() || g_d->ruta.empty()) return CallarVozB1();
    OirVozB1(g_d->ruta, e, dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()));
  }

  // Texto con las letras amarillas del resultado de B1 (668 #4 mayusculas, #3 '-'); x0 a la izquierda, centro cy.
  // Devuelve la x del final.
  float Letras(ImDrawList* dl, const S::Area& a, const char* s, float x0, float cy, float k, ImU32 tint) {
    const Tex *may = Img(kLetras + 1), *minus = Img(kLetras);
    float x = x0;
    for (; *s; ++s) {
      const char ch = *s;
      const Tex* t = ch >= 'A' && ch <= 'Z' ? may : ch == '-' ? minus : nullptr;
      const int i = ch == '-' ? 26 : ch - 'A';
      if (t) {
        const float sx = float((i % 7) * 48), sy = float((i / 7) * 64);
        Trozo(dl, a, *t, sx, sy, sx + 48, sy + 64, x + 18 * k, cy, k, tint);
        x += 34 * k;
      } else if (ch == '.') {
        dl->AddRectFilled(a.At(x + 2 * k, cy + 14 * k), a.At(x + 10 * k, cy + 22 * k), (tint & 0xFF000000) | 0x28C8E6);
        x += 12 * k;
      } else {
        x += 20 * k;
      }
    }
    return x;
  }
  // Cifras pequenas del resultado de B1 (668 #23, 4 x 3 de 40 x 40) desde x0; con signo si `signo`.
  float CifrasPeq(ImDrawList* dl, const S::Area& a, int n, float x0, float cy, float k, ImU32 tint, bool signo) {
    float x = x0;
    const ImU32 am = (tint & 0xFF000000) | 0x28C8E6;   // amarillo de las cifras (ABGR)
    if (signo) {   // + / - en el amarillo de las cifras
      dl->AddRectFilled(a.At(x, cy - 3 * k), a.At(x + 22 * k, cy + 3 * k), am);
      if (n >= 0) dl->AddRectFilled(a.At(x + 8 * k, cy - 11 * k), a.At(x + 14 * k, cy + 11 * k), am);
      x += 28 * k;
    }
    const std::string s = std::to_string(std::abs(n));
    const Tex* c = Img(kCifrasPeq);
    for (const char ch : s) {
      const int d = ch - '0';
      if (c) {
        const float sx = float((d % 4) * 40), sy = float((d / 4) * 40);
        Trozo(dl, a, *c, sx, sy, sx + 40, sy + 40, x + 15 * k, cy, k, tint);
      }
      x += 30 * k;
    }
    // sin la textura: centrada en el hueco de sus cifras (desde la primera, tras el signo), sin salirse de el
    if (!c) S::MenuText(dl, a.At(x - 15.0f * float(s.size()) * k, cy), 40 * k * a.s, s.c_str(), true, false,
                        30.0f * float(s.size()) * k * a.s);
    return x;
  }

  // Durante el combate de la leyenda: los puntos del combate (668 #20 "You have ... Pts.", abajo a la izquierda) y
  // encima los ultimos avisos (etiqueta de B1 y puntos) sobre la franja 666 #3, que se desvanecen a los 3 s.
  void DrawPuntos(ImDrawList* dl, const S::Area& a) {   // con g_mu
    std::vector<Suceso> suc;
    {
      std::lock_guard<std::mutex> lk(g_suc_mu);
      suc = g_sucesos;
    }
    const int64_t ahora = NowMs();
    const float bx = 30, by = 735;
    if (const Tex* tp = Img(kTienesPeq)) {
      Pic(dl, *tp, a.At(bx, by), a.At(bx + tp->w, by + tp->h));
      const std::string s = std::to_string(g_puntos_vivo.load());
      CifrasPeq(dl, a, g_puntos_vivo.load(), bx + 112 - 30 * 0.7f * float(s.size()), by + 80, 0.7f, IM_COL32_WHITE, false);
    } else {
      char b[48];
      std::snprintf(b, sizeof(b), T::T("Puntos: %d", "Points: %d"), g_puntos_vivo.load());
      S::MenuText(dl, a.At(bx + 100, by + 80), 36 * a.s, b, true, false);
    }
    float y = by - 22;
    // DS138: el anillo de B1 guarda 8 avisos y cada uno sube 44 u: con 8 la pila llegaba a y~405 y
    // tapaba a los luchadores (x 30..360). Se ven como mucho los 4 mas recientes (y >= 559).
    int vistos = 0;
    for (auto it = suc.rbegin(); it != suc.rend(); ++it) {
      const float edad = float(ahora - it->ms) / 1000.0f;
      if (edad > 3.0f) continue;
      if (vistos >= 4) break;
      ++vistos;
      const int alfa = int(255 * std::clamp((3.0f - edad) / 0.5f, 0.0f, 1.0f));
      const ImU32 tint = IM_COL32(255, 255, 255, alfa);
      if (const Tex* b = Img(kBarra)) {   // degradado de la 666 #3 de lado, como franja
        Pic(dl, *b, a.At(bx, y - 20), a.At(bx + 330, y + 20), IM_COL32(255, 255, 255, alfa * 3 / 4), false,
            ImVec2(0, 1), ImVec2(1, 0.2f));
      }
      const float x = Letras(dl, a, it->etiqueta, bx + 10, y, 0.55f, tint);
      CifrasPeq(dl, a, it->puntos, x + 10, y, 0.6f, tint, true);
      y -= 44;
    }
  }
  const Tex* Img(int key) const {
    auto it = tex_.find(key);
    return it != tex_.end() && it->second.tex ? &it->second : nullptr;
  }
  // EU 0x141320 FUN_00141320: dibuja un objeto texturizado con su mascara de flags y sus UV (el quad/pares de
  // 0x3509A0/0x3509E0 del prologo y los sprites del modo); aqui, Pic con su textura, recorte UV y tinte.
  void Pic(ImDrawList* dl, const Tex& t, ImVec2 p0, ImVec2 p1, ImU32 tint = IM_COL32_WHITE, bool mirror = false,
           ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) {
    if (mirror) std::swap(uv0.x, uv1.x);
    dl->AddImage(reinterpret_cast<ImTextureID>(t.tex.get()), p0, p1, uv0, uv1, tint);
  }
  // Imagen centrada en c con alto h (px de 1200x900).
  void PicAt(ImDrawList* dl, const S::Area& a, const Tex& t, float cx, float cy, float h, ImU32 tint = IM_COL32_WHITE) {
    const float w = t.w * h / t.h;
    Pic(dl, t, a.At(cx - w * 0.5f, cy - h * 0.5f), a.At(cx + w * 0.5f, cy + h * 0.5f), tint);
  }
  // Recorte (px de la textura) centrado en (cx, cy) con escala k.
  void Trozo(ImDrawList* dl, const S::Area& a, const Tex& t, float x0, float y0, float x1, float y1, float cx, float cy,
             float k, ImU32 tint = IM_COL32_WHITE) {
    const float w = (x1 - x0) * k, h = (y1 - y0) * k;
    Pic(dl, t, a.At(cx - w * 0.5f, cy - h * 0.5f), a.At(cx + w * 0.5f, cy + h * 0.5f), tint, false,
        ImVec2(x0 / t.w, y0 / t.h), ImVec2(x1 / t.w, y1 / t.h));
  }
  // Numero con las cifras del resultado de B1 (5 x 2 de 48 x 64), alineado a la derecha en (x1, cy).
  // EU 0x234570 SatanDraw_RoundScore: ronda/marcador parcial, base sprite 1 y digitos 0x1D+d.
  // EU 0x234790 SatanDraw_Total: total, base sprite 8 y digitos 0x27+d (6 posiciones). Aqui, una sola rutina de
  // cifras alineadas a la derecha.
  void Numero(ImDrawList* dl, const S::Area& a, int n, float x1, float cy, float k) {
    const std::string s = std::to_string(std::max(0, n));
    if (const Tex* c = Img(kCifras)) {
      float x = x1;
      for (auto it = s.rbegin(); it != s.rend(); ++it) {
        const int d = *it - '0', cx = (d % 5) * 48, cy0 = (d / 5) * 64;
        Trozo(dl, a, *c, float(cx), float(cy0), float(cx + 48), float(cy0 + 64), x - 20 * k, cy, k);
        x -= 40 * k;
      }
      return;
    }
    // sin la textura: alineada a la derecha en x1, dentro del hueco de sus cifras (40 px por cifra)
    S::MenuText(dl, a.At(x1 - 20.0f * float(s.size()) * k, cy), 52 * k * a.s, s.c_str(), true, false,
                40.0f * float(s.size()) * k * a.s);
  }

  // Fondo azul y logo de la pantalla del modo de Budokai 1; sin ellos, los del Duelo de B3.
  void Fondo(ImDrawList* dl, const S::Area& a, bool logo) {
    if (const Tex* f = Img(kFondo)) {   // como S::Backdrop: sin bordes dorados, a toda la ventana (recortando)
      const ImVec2 d = ImGui::GetIO().DisplaySize;
      const float k = std::max(d.x / f->w, d.y / f->h), x0 = (d.x - f->w * k) * 0.5f, y0 = (d.y - f->h * k) * 0.5f;
      if (S::Borders()) Pic(dl, *f, a.p0, a.p1);
      else Pic(dl, *f, ImVec2(x0, y0), ImVec2(x0 + f->w * k, y0 + f->h * k));
    } else {
      S::Backdrop(dl, a, "fondo_duelo");
    }
    S::Sides(dl, a);
    if (!logo) return;
    if (const Tex* l = Img(kLogo)) {
      const float h = 150, w = l->w * h / l->h;
      Pic(dl, *l, a.At(60, 18), a.At(60 + w, 18 + h));
    } else {
      S::Banner(dl, a, T::T("La leyenda de Mr. Satán", "Hercule's legend"));
    }
  }

  // Mr. Satan (su dibujo del juego de origen o su cara del marcador de B3) a la izquierda.
  // El dibujo acaba en un corte recto: su borde de abajo va bajo el bocadillo (y0 + h >= 650) o en el borde de abajo.
  void Satan(ImDrawList* dl, const S::Area& a, float t, bool habla, float y0 = 205, float h = 450) {
    const float bob = habla ? 4.0f * std::sin(t * 3.0f) : 0.0f;
    if (const Tex* s = Img(kSatan)) {
      const float w = s->w * h / s->h;
      Pic(dl, *s, a.At(70, y0 + bob), a.At(70 + w, y0 + h + bob + 6), habla ? IM_COL32_WHITE : IM_COL32(150, 150, 160, 255));
      return;
    }
    Cara(dl, a, kJugador, 0, habla, t);
  }

  // Cara del marcador de B3 en un marco del juego.
  void Cara(ImDrawList* dl, const S::Area& a, int id, int lado, bool on, float t) {
    DibujarCara(dl, a, id, lado == 0 ? 120.0f : 800.0f, 190.0f, 280.0f, on, NombreDe(id).c_str(), t, lado == 1);
  }

  // Bocadillo del juego abajo; dentro, la imagen de texto del original (oscurecida: su gris es para fondo oscuro)
  // o un texto nuestro.
  void Bocadillo(ImDrawList* dl, const S::Area& a, float t, int imagen, const char* texto, float cola) {
    const float in = Ease((t - cambio_at_) / 0.3f);
    const Tex* img = imagen >= 0 ? Img(imagen) : nullptr;
    S::Speech(dl, a.At(60, 640 + 30 * (1 - in)), a.At(1140, 850 + 30 * (1 - in)), a.s, img ? "" : texto, cola);
    if (img) PicAt(dl, a, *img, 600, 745 + 30 * (1 - in), std::min(185.0f, img->h * 1.45f), IM_COL32(150, 150, 150, 255));
  }

  // Franja oscura con el aviso del combate (imagen del original, letra clara) o un texto nuestro.
  void Aviso(ImDrawList* dl, const S::Area& a, int imagen, const std::string& texto, float cy = 582) {
    const Tex* img = imagen >= 0 ? Img(imagen) : nullptr;
    if (!img && texto.empty()) return;
    dl->AddRectFilled(a.At(150, cy - 42), a.At(1050, cy + 43), IM_COL32(8, 30, 48, 215), 10 * a.s);
    if (img) PicAt(dl, a, *img, 600, cy, std::min(78.0f, img->h * 1.25f));
    else S::MenuText(dl, a.At(600, cy), 34 * a.s, texto.c_str(), true, false, 880 * a.s);
  }

  void Ayuda(ImDrawList* dl, const S::Area& a, const std::vector<std::pair<const char*, const char*>>& items) {
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, items);
  }

  static float Vol() { return dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()); }

  // Prologo "Mr. Satan's Style" como B1 (SATAN DEMO MAIN, escena 0x1A, satanmode.c 0x22E380-0x2307AC), despues de
  // elegir la dificultad (FUN_0022de70 pide la 0x1A): la filosofia (668 #6), "The Art of Combat!" (668 #8, voz 1262)
  // y 5 consejos con la cara de Mr. Satan (668 #7; #10-#14: golpe 1263 y a los 139 cuadros su voz 1264-1268). Musica
  // 79 del demo a los 30 cuadros (FUN_0022e8f0) y voz 1261 a los 90 (FUN_0022e9d0). A pasa, START lo salta. Tiempos
  // (cuadros a 60/s): filosofia 30 + 60 + 1200 (FUN_0022eb90) + 30 de espera antes del primer cartel
  // (FUN_0022ed20 crea 0x22ef40 y FUN_0022efd0 cuenta 0x1e), "The Art of Combat!" 209 (FUN_0022f140), consejos
  // 139 + 349 / 239 / 309 / 229 / 369 (FUN_0022f2b0 a 0x230360). Al acabar, el combate 1 (FUN_00217120(0x1B)), sin
  // tarjeta. EU 0x2304C0 SatanDemo_Compose: el efecto es el objeto 6 (textura 668 #6, 512x512) sobre el quad de
  // 0x3509A0 y el degradado de 0x350A20; 0x9A4 es la mascara de flags de FUN_00141320(1,0x9a4,...,6,0x44,0), no un
  // objeto. El puerto lee 668 #6 como kEstilo y pinta el rodillo medido (PrologoFilosofia) sobre negro.
  // EU 0x22E670 SatanDemo_Main: estado principal del prologo (fondo, camara inicial y llamada a 0x22E8F0).
  // EU 0x22E2E0 SatanDemo_Load: carga los paquetes 667/668 (el puerto los lee al entrar, g_d->consejos/estilo).
  // EU 0x22E380 SatanDemo_Start: crea la tarea del prologo y el sprite del fondo (aqui, este mismo estado).
  // EU 0x22E440 SatanDemo_Free: libera paquetes, tareas y sprites (aqui, Salir() y el cambio de pantalla).
  // EU 0x230470 SatanDemo_End: fin del prologo y combate 1 sin tarjeta (aqui, Entrar(g_paso + 1)).
  void DrawPrologo(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (prologo_de_ != g_cambio) {
      prologo_de_ = g_cambio;
      prologo_k_ = 0;
      prologo_at_ = prologo_t0_ = t;
      prologo_voz_ = -1;
      musica_demo_ = false;
    }
    // EU 0x22E8F0 SatanDemo_Intro: a los 30 cuadros (0x1E) para la musica del titulo (FUN_00136fc0), pide la escena
    // 0x1B (FUN_00217120(0x1b)), arranca la animacion (FUN_001ea980(-1)), suena el jingle 79 (0x4F) y pasa a 0x22E9D0.
    if (!musica_demo_ && t - prologo_t0_ > 0.5f) {
      musica_demo_ = true;
      CallarAdxPs2();   // la 79 sustituye a la 1259 del titulo al cuadro 30, no al entrar al prologo
      if (!g_d->ruta.empty()) OirAdxPs2(g_d->ruta, "adx_jp.afs", 79, Vol());
    }
    const int k = prologo_k_;
    if (prologo_voz_ != k) {
      prologo_voz_ = k;
      REXLOG_INFO("dbz3 pasarela: prologo {}/7", k + 1);
      // EU 0x22E9D0 SatanDemo_PhilosophyStart: a los 60 cuadros de este estado (90 desde el principio de la escena)
      // suena la voz 1261 (0x4ED). La camara de este estado (0x3508E0 -> 0x3508F0 sobre 1300 cuadros) la reproduce
      // PrologoFilosofia; el objeto es la 668 #6 (kEstilo) y 0x9A4 es la mascara de flags (0x2304C0).
      const std::vector<int> e = k == 0   ? std::vector<int>{-1500, 1261}
                                 : k == 1 ? std::vector<int>{1262}
                                          : std::vector<int>{1263, -2317, 1262 + k};
      if (!g_d->ruta.empty()) OirVozB1(g_d->ruta, e, Vol());
    }
    Fondo(dl, a, false);
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 255));   // B1: negro puro (medido en la captura)
    // EU 0x149840 SatanFog_LoadState / EU 0x13EBE0 / EU 0x13EC20: estado de niebla del prologo (near 100, far 350,
    // color negro de 0x22E670); el tinte va sobre el fondo negro, donde el original ya no deja ver nada.
    NieblaB1Pintar(dl, a, 1.0f);
    const float tp = t - prologo_at_;
    // EU 0x22EB90 SatanDemo_Philosophy: 1200 cuadros (0x4AF) de filosofia; el puerto usa 1320 = 90 (intro de 0x22E8F0
    // + 0x22E9D0) + 1200 + 30 de la espera de 0x22EF40/0x22EFD0 antes del primer cartel. La camara de 0x22EB90
    // (0x350900 -> 0x350910) la reproduce PrologoFilosofia con el mismo contador.
    // EU 0x22EFD0 SatanDemo_FirstTipWait: descuenta 0x1e (30) cuadros y, al llegar a 0, pasa a 0x22F050 (primer cartel);
    // aqui son los ultimos 30 cuadros de la filosofia (el puerto mantiene el ultimo estado de la imagen).
    // EU 0x22F140 SatanDemo_ArtOfCombat: 209 cuadros (0xD1) de "The Art of Combat!".
    // Los consejos agrupan los tramos de 0x22F2B0 (139), 0x22F420 (349), 0x22F5D0 (139), 0x22F7F0 (239),
    // 0x22F9A0 (139), 0x22FBC0 (309), 0x22FD70 (139), 0x22FF90 (229), 0x230140 (139) y 0x230360 (369).
    constexpr int kConsejoCuadros0[5] = {349, 239, 309, 229, 369};
    const float dur0 = k == 0 ? 1320.0f / 60.0f : k == 1 ? 209.0f / 60.0f : float(139 + kConsejoCuadros0[k - 2]) / 60.0f;
    if (k == 0) {
      PrologoFilosofia(dl, a, tp, dur0);
    } else {
      PrologoConsejos(dl, a, k, tp);
    }
    // EU 0x2304C0 SatanDemo_Compose: tras el quad del rotulo, el original dibuja un segundo quad de pantalla
    // (tablas 0x350A20..0x350A50 = (0,336),(640,336),(0,448),(640,448) en 640x448) con el color de arriba
    // uRam005ecd80 y el de abajo _DAT_00350960 = (0,0,0,1) (0x350960): el degradado negro del borde inferior.
    // DS97: es el unico trozo del efecto que el puerto no pintaba. El quad del rotulo es el de 0x3509A0
    // (-100,-100,-260)..(100,100,-260), UV 0x3509E0 = (0,1),(0,0),(1,1),(1,0), color 0x350990 = (1,1,1,1),
    // textura 668 #6 (512x512, #AMT); el recorrido 0x3508E0->0x3508F0 = (0,23,120)->(0,336,-190) sobre 1300
    // cuadros y la proyeccion 0x3519A0 (near 0.1, far 65535, f 512) los reproduce PrologoFilosofia.
    {
      const ImVec2 g0 = a.At(0, 336 * kB1 * kB1Alto), g1 = a.At(640 * kB1, 448 * kB1 * kB1Alto);
      const ImU32 abajo = IM_COL32(0, 0, 0, 255);
      dl->AddRectFilledMultiColor(g0, g1, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), abajo, abajo);
    }
    Ayuda(dl, a, {{"A", T::T("Siguiente", "Next")}, {"START", T::T("Saltar", "Skip")}, {"B", T::T("Salir", "Quit")}});
    constexpr int kConsejoCuadros[5] = {349, 239, 309, 229, 369};
    const float dur = k == 0 ? 1320.0f / 60.0f : k == 1 ? 209.0f / 60.0f : float(139 + kConsejoCuadros[k - 2]) / 60.0f;
    if (t - prologo_at_ > (auto_on && !g_real ? 2.0f : dur)) pressed |= kA;
    if (pressed & kB) return Salir(false);
    if ((pressed & kStart) || ((pressed & kA) && k == 6)) {
      CallarVozB1();
      CallarAdxPs2();
      REXLOG_INFO("dbz3 pasarela: prologo {}", k == 6 ? "visto" : "saltado");
      return Entrar(g_paso + 1);
    }
    if (pressed & kA) {
      prologo_k_ = k + 1;
      prologo_at_ = t;
    }
  }

  // Texto con el "fuego" de los consejos de B1: copias naranjas desplazadas hacia arriba debajo de la imagen. (c, cy) en
  // px de la captura de 640x480 de B1, k = escala de la imagen.
  // EU 0x230C10 SatanDemo_TipFire: particulas de "fuego" de cada consejo (0x6C = 15 cuadros; velocidades
  // +0x30..+0x5C). Aqui, las copias naranjas desplazadas hacia arriba que forman las llamas.
  void TextoFuego(ImDrawList* dl, const S::Area& a, const Tex& im, float cx, float cy, float k, float alfa, float x0 = 0,
                  float y0 = 0, float x1 = -1, float y1 = -1) {
    if (x1 < 0) x1 = im.w, y1 = im.h;
    const float w = (x1 - x0) * k * kB1, h = (y1 - y0) * k * kB1Alto * kB1;
    const ImVec2 u0(x0 / im.w, y0 / im.h), u1(x1 / im.w, y1 / im.h);
    const ImVec2 p0 = a.At(cx * kB1 - w * 0.5f, cy * kB1 - h * 0.5f);
    for (int j = 6; j >= 1; --j) {   // llamas: copias naranja-amarillo cada vez mas altas y flojas
      const ImU32 c = IM_COL32(255, 60 + 30 * (6 - j), 0, int(95.0f * alfa));
      Pic(dl, im, ImVec2(p0.x, p0.y - 2.6f * float(j) * a.s), ImVec2(p0.x + w * a.s, p0.y + (h - 2.6f * float(j)) * a.s), c, false, u0, u1);
    }
    Pic(dl, im, p0, ImVec2(p0.x + w * a.s, p0.y + h * a.s), IM_COL32(255, 255, 255, int(255.0f * alfa)), false, u0, u1);
  }

  // "Mr. Satan's Style" de B1: las 8 lineas de 668 #6 entran por abajo una tras otra (cada 1,2 s), suben con
  // perspectiva (la curva y(tau), la escala y el brillo salen de 54 capturas de la ISO cada 1,2 s; las lineas
  // siguen todas la misma curva) y se pierden arriba, como el rodillo de "La guerra de las galaxias".
  // Geometria del original (ELF SLES-512.33): el objeto es el quad de 0x3509A0 = (-100,-100,-260)..(100,100,-260)
  // con las UV de 0x3509E0 = (0,1),(0,0),(1,1),(1,0), color (1,1,1,1) de 0x350990 y la textura 668 #6 (512x512);
  // el recorrido 0x3508E0->0x3508F0 = (0,23,120)->(0,336,-190) se interpola sobre 1300 cuadros (el contador es
  // el mismo de los cuatro estados 0x22E9D0/0x22EB90/0x22ED20/0x22EE30). La mascara 0x9A4 son flags de dibujo,
  // NO el objeto (DS46).
  // EU 0x13D5D0 SatanDemo_Projection: fija la proyeccion del prologo (640x448, 0x3519A0); aqui la escala y la
  // posicion vertical de cada linea salen de la curva proyectada y(tau) y sc(y) (medida en capturas de la ISO).
  // EU 0x13E1C0 SatanDemo_SetMatrices: selecciona la matriz de camara/objeto del estado; aqui, el recorrido
  // (0,23,120)->(0,336,-190) sobre 1300 cuadros que comparten los cuatro estados.
  // EU 0x143BB0 SatanDemo_DrawObject: dibuja el objeto texturizado (textura 6) con la mascara de flags 0x9A4;
  // aqui, el Trozo/Pic de la 668 #6 con su escala y alfa.
  // EU 0x145330 SatanDemo_ViewTransform: pasa la posicion del objeto a pantalla; aqui, la transformacion vertical
  // y(tau)+13,9*sc medida en 54 capturas. [H] 0x13D5D0/0x13E1C0/0x143BB0/0x145330 (0x2304C0): el microcodigo
  // VU exacto no esta en el pseudocodigo; no se sustituye la curva medida por una suposicion.
  // EU 0x2304C0 SatanDemo_Compose: compone el fondo del prologo (proyeccion 0x13D5D0 + camara 0x145330 + objeto
  // 0x143BB0); aqui, la composicion y el dibujo de esta funcion.
  // EU 0x22ED20 SatanDemo_TipIntro: primer consejo; el mismo recorrido de camara (0x350920->0x350930) sigue en
  // la curva de abajo (el estado no incrementa el contador).
  // EU 0x22EE30 SatanDemo_TipCamera: acerca la camara (0x350940->0x350950) hasta el cuadro 1300; la curva de
  // abajo cubre ese tramo con el mismo contador.
  void PrologoFilosofia(ImDrawList* dl, const S::Area& a, float tp, float dur) {
    const Tex* es = Img(kEstilo);
    if (!es) return;
    static constexpr float kCentro[8] = {61, 148, 198, 248, 298, 348, 398, 448};   // alto del centro de cada linea en la 668 #6
    static constexpr float kEntra[8] = {1.5f, 3.9f, 5.1f, 6.3f, 7.5f, 8.7f, 9.9f, 11.1f};
    static constexpr float kTau[12] = {0.0f, 1.2f, 2.3f, 3.5f, 4.7f, 5.9f, 7.1f, 8.3f, 9.5f, 10.7f, 11.9f, 14.0f};
    static constexpr float kY[12] = {425, 361, 313, 272, 238, 205, 179, 153, 131, 112, 95, 82};   // arriba de la letra
    const float fin = std::clamp((dur - tp) / 1.5f, 0.0f, 1.0f);
    for (int i = 0; i < 8; ++i) {
      const float tau = tp - kEntra[i];
      if (tau < 0) continue;
      float y = kY[11];
      for (int j = 0; j < 11; ++j) {
        if (tau <= kTau[j + 1]) {
          y = kY[j] + (kY[j + 1] - kY[j]) * std::max(0.0f, (tau - kTau[j]) / (kTau[j + 1] - kTau[j]));
          break;
        }
      }
      const float sc = y >= 361 ? 1.18f + (std::min(y, 419.0f) - 361) * (0.11f / 58) : 0.593f + (std::max(y, 109.0f) - 109) * (0.587f / 252);
      const float al = std::clamp((y - 55) / 230.0f, 0.0f, 1.0f) * std::clamp(tau / 0.5f, 0.0f, 1.0f) * fin;
      if (al <= 0.01f) continue;
      const float c = kCentro[i], cy = y + 13.9f * sc;
      const float w = es->w * sc * kB1, h = 50 * sc * kB1Alto * kB1;
      Pic(dl, *es, a.At(320 * kB1 - w * 0.5f, cy * kB1 - h * 0.5f), a.At(320 * kB1 + w * 0.5f, cy * kB1 + h * 0.5f),
          IM_COL32(255, 255, 255, int(255 * al)), false, ImVec2(0, (c - 25) / es->h), ImVec2(1, (c + 25) / es->h));
    }
  }

  // "The Art of Combat!" arriba (amplia desde un 230 % a su tamano en 1,5 s) y los consejos que se van quedando, con la
  // cara de Mr. Satan oscura abajo a la izquierda. Posiciones medidas en la captura (640x480, centros y de cada imagen).
  // EU 0x22EF40 SatanDemo_FirstTipStart: corta la musica del titulo y prepara el primer consejo tras la filosofia.
  // EU 0x22F050 SatanDemo_FirstTip: pinta el primer cartel y suena la voz 1262 (0x4EE, canal 1).
  // EU 0x22F2B0 SatanDemo_Tip1: paso 1 (668 #10), 139 cuadros, voz 1264 (0x4F0, canal 1).
  // EU 0x22F420 SatanDemo_Tip2: paso 2, 349 cuadros, voz 1263 (0x4EF, canal 0).
  // EU 0x22F5D0 SatanDemo_Tip3: paso 3, 139 cuadros, voz 1265 (0x4F1, canal 1).
  // EU 0x22F7F0 SatanDemo_Tip4: paso 4, 239 cuadros, voz 1263 (0x4EF, canal 0).
  // EU 0x22F9A0 SatanDemo_Tip5: paso 5, 139 cuadros, voz 1266 (0x4F2, canal 1).
  // EU 0x22FBC0 SatanDemo_Tip6: paso 6, 309 cuadros, voz 1263 (0x4EF, canal 0).
  // EU 0x22FD70 SatanDemo_Tip7: paso 7, 139 cuadros, voz 1267 (0x4F3, canal 1).
  // EU 0x22FF90 SatanDemo_Tip8: paso 8, 229 cuadros, voz 1263 (0x4EF, canal 0).
  // EU 0x230140 SatanDemo_Tip9: paso 9, 139 cuadros, voz 1268 (0x4F4, canal 1).
  // EU 0x230360 SatanDemo_LastCard: ultimo cartel, 369 cuadros (668 #10-#14 con sus cuadros y voces).
  void PrologoConsejos(ImDrawList* dl, const S::Area& a, int k, float tp) {
    static constexpr float kY[5] = {150.5f, 226, 287.5f, 347, 417.5f};
    if (const Tex* f = Img(kCaraSatan)) {
      const float w = f->w * 1.1f * kB1, h = f->h * 1.1f * kB1Alto * kB1;   // medida: pelo en (55, 185), mentón en y 430
      Pic(dl, *f, a.At(50 * kB1, 170 * kB1), a.At(50 * kB1 + w, 170 * kB1 + h), IM_COL32(255, 255, 255, 38));
    }
    if (const Tex* ti = Img(kConsejo)) {
      const float z = k == 1 ? 1.0f + 1.3f * std::max(0.0f, 1.0f - tp / 1.5f) : 1.0f;
      const float al = k == 1 ? std::clamp(tp / 0.6f, 0.0f, 1.0f) : 1.0f;
      TextoFuego(dl, a, *ti, 323.5f, 64, z, al);
    }
    for (int i = 0; i < 5; ++i) {
      if (k < 2 + i) break;
      const Tex* ts = Img(kConsejo + 1 + i);
      if (!ts) continue;
      const float al = k == 2 + i ? std::clamp(tp / 0.6f, 0.0f, 1.0f) : 1.0f;
      if (al > 0) TextoFuego(dl, a, *ts, 320, kY[i], 1.0f, al);
    }
  }

  // Sprite de B1 en coordenadas de la captura (640x480, y estirada 480/448): el punto (0, 0) de la textura va a (ox, oy)
  // con escala s; (x0, y0)-(x1, y1) = recorte de la textura (px; x1 < 0 = entera); k > 0 = escala alrededor de (cx, cy).
  void Spr(ImDrawList* dl, const S::Area& a, const Tex& t, float ox, float oy, float s, ImU32 tint = IM_COL32_WHITE,
           float x0 = 0, float y0 = 0, float x1 = -1, float y1 = -1, float cx = -1, float cy = -1, float k = 1) {
    if (x1 < 0) x1 = t.w, y1 = t.h;
    float ax = ox + x0 * s, ay = oy + y0 * s * kB1Alto, bx = ox + x1 * s, by = oy + y1 * s * kB1Alto;
    if (cx >= 0) {
      ax = cx + (ax - cx) * k, bx = cx + (bx - cx) * k, ay = cy + (ay - cy) * k, by = cy + (by - cy) * k;
    }
    Pic(dl, t, a.At(ax * kB1, ay * kB1), a.At(bx * kB1, by * kB1), tint, false, ImVec2(x0 / t.w, y0 / t.h), ImVec2(x1 / t.w, y1 / t.h));
  }
  // Cifras amarillas de 668 #22 (5 x 2 de 48 x 64), alineadas a la derecha en xr (captura), con lo alto de la celda en y.
  void Cifras(ImDrawList* dl, const S::Area& a, int n, float xr, float y, float s, ImU32 tint = IM_COL32_WHITE) {
    const Tex* c = Img(kCifras);
    if (!c) return;
    const std::string v = std::to_string(std::max(0, n));
    int i = 0;
    for (auto it = v.rbegin(); it != v.rend(); ++it, ++i) {
      const int d = *it - '0', cx = (d % 5) * 48, cy = (d / 5) * 64;
      Spr(dl, a, *c, xr - 48 * s - 40 * s * float(i) - float(cx) * s, y - float(cy) * s * kB1Alto, s, tint, float(cx), float(cy), float(cx + 48), float(cy + 64));
    }
  }
  // Fondo del resultado de B1: negro y, de la fila 300 a la 480, el degradado a rojo oscuro del original.
  // DS71: el degradado es la textura 668 #5 del AFS del jugador (8x184, rojo 0xFD0000 con alfa 0..0x84), dibujada
  // desde y=300 hasta abajo (posicion medida en la captura). El alfa se modula a 175 para que el borde de abajo de
  // los 90 de rojo medidos en el original (0x84 * 175/255 * 253/255 = 90); sin la textura, el degradado lineal de antes.
  void FondoNegro(ImDrawList* dl, const S::Area& a, bool degradado) {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 255));
    if (!degradado) return;
    const float y0 = a.At(0, 300 * kB1).y, y1 = std::max(a.At(0, 480 * kB1).y, d.y);
    if (const Tex* g = Img(kGradRes)) {
      Pic(dl, *g, ImVec2(0, y0), ImVec2(d.x, y1), IM_COL32(255, 255, 255, 175));
      return;
    }
    dl->AddRectFilledMultiColor(ImVec2(0, y0), ImVec2(d.x, y1), IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255),
                                IM_COL32(90, 0, 0, 255), IM_COL32(90, 0, 0, 255));
  }
  // Pantalla de resultado de B1 tras cada combate: la charla y el "NEXT BATTLE" en una sola (medida contra la captura de
  // PCSX2, 640x480, con las mismas texturas de 668/669): tablon 668 #15 en (28, 17); Mr. Satan y "Total" (668 #18) en
  // (439, 79) al 102 %; charla 669 par en (87, 122) y aviso 669 impar en (160, 337); cifras del total (610, 277) al 62 %.
  // Fila de 11 caras: 667 #AMA 60+k (color) / 70+k (silueta) en x=80+48k, y=408 (UV de 668 #24, celdas 72x72); el aspa
  // es 667 #AMA 81 (48x56, copiada a la cara vencida por 0x232140) y el letrero 667 #AMA 49+r (136x72) va a su derecha.
  // EU 0x231AC0 SatanResult_ShowScore: pinta la cifra del marcador de la ronda con FUN_00234570 y el letrero.
  // EU 0x231E60 SatanResult_NextBoard: letreros 0x17/0x14 y vuelve al tablero del siguiente retador.
  void DrawResumenB1(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    const Paso& p = g_pasos[size_t(g_paso)];
    const int r = p.ronda, n = std::min(int(g_d->combates.size()), 11);
    const float e = t - cambio_at_;
    // EU 0x232060 SatanBoard_FadeIn / EU 0x232140 SatanBoard_Faces: fundido lineal de 0,05 por cuadro (20
    // cuadros = 0,33 s), no una curva Ease.
    const ImU32 col = IM_COL32(255, 255, 255, int(255 * std::clamp(e / (20.0f / 60.0f), 0.0f, 1.0f)));
    FondoNegro(dl, a, true);
    if (const Tex* tb = Img(kTablon)) Spr(dl, a, *tb, 28, 17, 1.0f, col);
    if (const Tex* st = Img(kTotal)) Spr(dl, a, *st, 439, 79, 1.02f, col);
    Cifras(dl, a, g_puntos, 610, 277, 0.62f, col);
    if (r >= 1) {
      if (const Tex* tx = Img(2 * (r - 1))) Spr(dl, a, *tx, 87, 122, 1.0f, col);
      if (const Tex* av = Img(2 * (r - 1) + 1)) Spr(dl, a, *av, 160, 337, 1.0f, col);
    }
    if (const Tex* ch = Img(kChibis)) {
      // DS97: fila de 11 caras en x = 80+48k, y = 408 (667 #AMA 60+k / 70+k, 72x72; centro de la animacion).
      // Las vencidas llevan el aspa 667 #AMA 81 (48x56) en la misma posicion. La del retador actual (r) sale de
      // la fila (0x232660) y se dibuja grande con su letrero.
      auto At448 = [&](float x, float y) { return a.At(x * kB1, y * kB1 * kB1Alto); };
      auto caraCentro = [&](int celda, float cx, float cy, float s, ImU32 tint, const Tex& tex, float tw, float th) {
        const float sx = float((celda % 7) * 72), sy = float((celda / 7) * 72);
        if (tw > 72.0f) {   // 668 #16 tiene celdas de 136x72 (3 columnas x 4 filas)
          const float u = float((celda % 3) * 136), v = float((celda / 3) * 72);
          Pic(dl, tex, At448(cx - tw * s * 0.5f, cy - th * s * 0.5f), At448(cx + tw * s * 0.5f, cy + th * s * 0.5f),
              tint, false, ImVec2(u / tex.w, v / tex.h), ImVec2((u + tw) / tex.w, (v + th) / tex.h));
          return;
        }
        Pic(dl, tex, At448(cx - 36.0f * s, cy - 36.0f * s), At448(cx + 36.0f * s, cy + 36.0f * s), tint, false,
            ImVec2(sx / tex.w, sy / tex.h), ImVec2((sx + 72) / tex.w, (sy + 72) / tex.h));
      };
      for (int k = 0; k < n; ++k) {
        if (k == r) continue;   // EU 0x232660 SatanBoard_RemoveCurrentFace: la suya va en grande
        const bool vencida = k < r;
        caraCentro(vencida ? k : k + 10, 80.0f + 48.0f * float(k), 408.0f, 1.0f, col, *ch, 72, 72);
        if (vencida)
          if (const Tex* cz = Img(kCruz); cz)
            Pic(dl, *cz, At448(80.0f + 48.0f * float(k) - 24, 408.0f - 28), At448(80.0f + 48.0f * float(k) + 24, 408.0f + 28));
      }
      // EU 0x2327E0 SatanBoard_GrowPair: el par grande crece 0,7 -> 1 (0,01/cuadro, 30 cuadros) tras el fundido.
      const float kGrow = 0.7f + 0.3f * std::clamp((e - 20.0f / 60.0f) / 0.5f, 0.0f, 1.0f);
      if (r < n) caraCentro(r, 80.0f + 48.0f * float(r), 408.0f, kGrow, col, *ch, 72, 72);
    }
    // EU 0x232920 SatanBoard_Pulse (y 0x232A80): late = 0,975 + 0,025*sin((cuadro % 30)*2pi/30) con el
    // contador ENTERO de cuadros del callback (no el tiempo continuo: la fase es la del cuadro).
    const int cuadro = int(std::max(0.0f, e) * 60.0f);
    const float late = 0.975f + 0.025f * std::sin(float(cuadro % 30) * (2.0f * 3.14159265f / 30.0f));
    if (const Tex* rt = Img(kRetadores); rt && r < 11) {
      // EU 0x232140 SatanBoard_Faces: el letrero (sprite 0x1C) se crea con el par. 667 #AMA 49+r (136x72) tiene
      // sus claves atadas a la cara de la ronda (x=80+48r, y=432, caja +24..+160 / -43..+28) y es el que late
      // (0x232920/0x232A80). En la ronda 1 el centro medido es 223,436: el mismo desfase se conserva al avanzar
      // de ronda (paso 48). La celda r de 668 #16 es el mismo arte.
      const float lx = float((r % 3) * 136), ly = float((r / 3) * 72);
      const float cx = 223.0f + 48.0f * float(std::max(0, r - 1)), cy = 436.0f, hw = 68.0f * late, hh = 36.0f * late;
      Pic(dl, *rt, a.At((cx - hw) * kB1, (cy - hh) * kB1 * kB1Alto), a.At((cx + hw) * kB1, (cy + hh) * kB1 * kB1Alto),
          col, false, ImVec2(lx / rt->w, ly / rt->h), ImVec2((lx + 136) / rt->w, (ly + 72) / rt->h));
    }
    Ayuda(dl, a, {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on && e > g_auto_seg) pressed |= kA;
    if (pressed & kB) return Salir(false);
    if (pressed & (kA | kStart)) Entrar(g_paso + 1);
  }

  // Pantalla del titulo y de la dificultad de B1, medida contra la captura del original (PCSX2, 640x480; la PS2 saca
  // 640x448 y lo estira a 4:3 en vertical: kB1Alto) con las texturas 666 #0-#6 (ajuste por distancia a los bordes
  // oscuros del logo, 2026-10-19): Mr. Satan #1 a 1:1 en (65, 8); logo #2 al 97,5 % girado 20 grados (antihorario) con
  // su centro en (210, 118); copyright #4 a 1:1 en (75, 416); degradado negro de abajo de la 315 a la 480.
  static constexpr float kB1 = 1.875f;     // 640 de la captura -> 1200 del area
  static constexpr float kB1Alto = 480.0f / 448.0f;
  // EU 0x22D210 SatanTitle_Compose: coloca los 4 trozos del titulo y los 4 del rotulo con las tablas 0x3507B0/
  // 0x3507C0/0x3507D0; aqui se usa la composicion medida de la captura (666 #1/#2/#4/#5) y el mismo orden.
  // DS71: el degradado negro de abajo es la textura 666 #3 (16x88) del AFS del jugador, en la franja medida de la
  // captura (315..480) y en el mismo orden que el original (sobre el borde inferior del dibujo, bajo el copyright).
  // No es un elemento nuevo del puerto: es el degradado nativo. Sin la textura, el degradado lineal de antes.
  // DS77: las animaciones 1-5 del montaje del titulo (EU 0x22DA10, FUN_001f9850(1..5, 0..4)) estan en 665 #AMA
  // (DATA_<idioma>.AFS, banco 665; entrada de 0x40 B y claves de 0x20 B: cuadro + escala xy/rot + posicion xyz +
  // alfa). Registro 1 (Mr. Satan, 666 #1): (0,-260,230) (30,280,230) (80,340,230) (150,340,230). Registro 2
  // (logo, 666 #2): (0,930,110) (30,930,110) (50,250,110) (80,210,110) (150,210,110), en px de 640x448. Hasta
  // el cuadro 100 (30 de 0x22D990 + 70 de 0x22DA10) las dos piezas siguen en su primera clave (fuera de pantalla).
  void PantallaTitulo(ImDrawList* dl, const S::Area& a, float t) {
    const float k = kB1;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    static constexpr std::array<std::array<float, 3>, 4> kSat = {{{0, -260, 230}, {30, 280, 230}, {80, 340, 230}, {150, 340, 230}}};
    static constexpr std::array<std::array<float, 3>, 5> kLogoKf = {{{0, 930, 110}, {30, 930, 110}, {50, 250, 110}, {80, 210, 110}, {150, 210, 110}}};
    auto kf = [](const auto& keys, float f) {
      if (f <= keys.front()[0]) return keys.front();
      for (size_t i = 1; i < keys.size(); ++i)
        if (f <= keys[i][0]) {
          const float u = (f - keys[i - 1][0]) / (keys[i][0] - keys[i - 1][0]);
          return std::array<float, 3>{0.0f, keys[i - 1][1] + u * (keys[i][1] - keys[i - 1][1]),
                                      keys[i - 1][2] + u * (keys[i][2] - keys[i - 1][2])};
        }
      return keys.back();
    };
    const float fa = t * 60.0f - 100.0f;   // EU 0x22DA10: las animaciones 1-5 empiezan en el cuadro 100
    if (fa >= 0.0f) {
      if (const Tex* s = Img(kSatan)) {
        const auto q = kf(kSat, fa);   // el ancla del puerto es la medida (65,8); 340/230 son la clave final
        const float w = s->w * k, h = s->h * kB1Alto * k;
        const float cx = (65 + s->w * 0.5f + q[1] - 340.0f) * k;
        const float cy = (8 + s->h * kB1Alto * 0.5f + (q[2] - 230.0f) * kB1Alto) * k;
        Pic(dl, *s, a.At(cx - w * 0.5f, cy - h * 0.5f), a.At(cx + w * 0.5f, cy + h * 0.5f));
      }
      if (const Tex* l = Img(kLogo)) {
        const auto q = kf(kLogoKf, fa);
        const float hw = l->w * 0.975f * 0.5f * k, hh = l->h * 0.975f * kB1Alto * 0.5f * k;
        const float cx = q[1] * k, cy = q[2] * kB1Alto * k;
        if (cx + hw > 0.0f && cx - hw < d.x) {
          const float c = std::cos(20.0f * 3.14159265f / 180.0f), sn = std::sin(20.0f * 3.14159265f / 180.0f);
          auto P = [&](float x, float y) { return a.At(cx + x * c + y * sn, cy - x * sn + y * c); };
          dl->AddImageQuad(reinterpret_cast<ImTextureID>(l->tex.get()), P(-hw, -hh), P(hw, -hh), P(hw, hh), P(-hw, hh),
                           ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1));
        }
      }
    }
    const float y0 = a.At(0, 315 * k).y, y1 = std::max(a.At(0, 480 * k).y, d.y);
    if (const Tex* b = Img(kBarra)) {
      Pic(dl, *b, ImVec2(0, y0), ImVec2(d.x, y1));
    } else {
      dl->AddRectFilledMultiColor(ImVec2(0, y0), ImVec2(d.x, y1), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 250), IM_COL32(0, 0, 0, 250));
    }
    if (const Tex* c2 = Img(kCopy))
      Pic(dl, *c2, a.At(75 * k, 416 * k), a.At((75 + c2->w) * k, (416 + c2->h * kB1Alto) * k));
  }

  // Titulo como el de B1 (SATAN MODE MAIN, escena 5): fondo 666 #0, Mr. Satan 666 #1, logo 666 #2, "Pulsa START"
  // 666 #5 y la musica 1259 a los 50 cuadros (FUN_0022db00). START abre el letrero 666 #6 con Novice / Adept /
  // Advanced (666 #7-#9, arriba/abajo); al confirmar se guarda la dificultad y sigue el prologo (FUN_0022de70).
  // EU 0x22D7E0 SatanTitle_Start: espera el paquete 666 (aqui ya esta cargado), monta el fondo y el sprite y pasa al
  // estado de 30 cuadros.
  void DrawPresentacion(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (elegida_ == g_cambio) {
      if (Img(kEstilo) && Img(kConsejo + 5)) return DrawPrologo(dl, a, t, pressed, auto_on);
      CallarAdxPs2();
      return Entrar(g_paso + 1);
    }
    // EU 0x22D990 SatanTitle_Wait30: 30 cuadros (0x1e) de espera tras montar la composicion.
    // EU 0x22DA10 SatanTitle_AnimIntro: 70 cuadros (0x45) y, al acabar (cuadro 100), arranca las animaciones 1-5
    // de los trozos (FUN_001f9850(1..5, 0..4)); PantallaTitulo las reproduce con las claves de 665 #AMA.
    // EU 0x22DB00 SatanTitle_Wait: a los 50 cuadros de ESTE estado (cuadro 150 del titulo) para la musica, suena la
    // 1259 en el canal 0 y, con la animacion 4 terminada, crea el sprite 6 = "Pulsa START" (665 #AMA registro 5):
    // su clave es fija (alfa 1), asi que no parpadea.
    const float fr = (t - cambio_at_) * 60.0f;
    if (musica_de_ != g_cambio && fr >= 150.0f) {
      musica_de_ = g_cambio;
      if (!g_d->ruta.empty()) OirAdxPs2(g_d->ruta, "adx_jp.afs", 1259, Vol());
    }
    const bool titulo = Img(kPulsa) && titulo_ != g_cambio;
    PantallaTitulo(dl, a, t - cambio_at_);
    if (titulo && fr < 150.0f) return;   // montaje + animaciones 1-5 antes de "Pulsa START"
    if (const Tex* ps = Img(kPulsa); ps && titulo) {
      // "Press START button" de B1: 666 #5 a 1:1, centrado en (321, 324) de la captura de 640x480, alfa fijo.
      const float k = kB1, w = ps->w * k, h = ps->h * kB1Alto * k;
      Pic(dl, *ps, a.At(321 * k - w * 0.5f, 324 * k - h * 0.5f), a.At(321 * k + w * 0.5f, 324 * k + h * 0.5f));
      Ayuda(dl, a, {{"START", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
      if (auto_on && fr > g_auto_seg * 60.0f) pressed |= kStart;
      if (pressed & kB) return Salir(false);
      // EU 0x22DBF0 SatanTitle_Confirm: START confirma la entrada, suena el 3 y pasa al letrero de dificultad.
      if (pressed & (kA | kStart)) {
        titulo_ = g_cambio;
        letrero_at_ = t;   // no se toca cambio_at_: el fondo del titulo sigue su reloj
        // DS97: las tres opciones arrancan su apertura en este cuadro (665 #AMA 9/16/23 desde 0). La elegida
        // recibira la entrada 10+7*i en 0x22DDA0, cuando acabe la animacion 7 (cuadro 45).
        dif_iniciada_ = ~0ull;
        confirmando_ = ~0ull;
        for (int i = 0; i < 3; ++i) dif_[i] = {9 + 7 * i, -1, letrero_at_};
        SeB1(3);
        REXLOG_INFO("dbz3 pasarela: titulo: START (dificultad)");
      }
      return;
    }
    // Letrero y dificultades de B1. Las claves de 665 #AMA que arranca 0x22DBF0 (FUN_001f9850(6,6), (7,7),
    // (8,9), (9,0x10), (10,0x17)) son: registro 6 (Pulsa) escala 1,1'3,1'2,0'5 en los cuadros 0/5/10/15;
    // registro 7 (letrero) escala x 0,0,-1,-0'03,0'03,1 en 0/16/22/30/31/45 (volteo). 0x22DDA0 espera el fin de la
    // animacion 7: 45 cuadros (0,75 s), no 11. Los registros 9/16/23 abren las tres opciones (x 0..0'7); a partir
    // del 45 la elegida reproduce 10/17/24 y queda en 11/18/25; al movernos 12/19/26 (la que sale) y 10/17/24 (la
    // que entra); al confirmar 15/22/29. Datos de 665 #AMA (ver EscalaDificultad).
    const float k = kB1, cx = 320.0f * k;
    const float pf = std::max(0.0f, (t - letrero_at_) * 60.0f);
    auto clave1 = [](const auto& keys, float f) {
      if (f <= keys.front()[0]) return keys.front()[1];
      for (size_t i = 1; i < keys.size(); ++i)
        if (f <= keys[i][0]) {
          const float u = (f - keys[i - 1][0]) / (keys[i][0] - keys[i - 1][0]);
          return keys[i - 1][1] + u * (keys[i][1] - keys[i - 1][1]);
        }
      return keys.back()[1];
    };
    static constexpr std::array<std::array<float, 2>, 4> kPulsaZoom = {{{0, 1.0f}, {5, 1.3f}, {10, 1.2f}, {15, 0.5f}}};
    static constexpr std::array<std::array<float, 2>, 6> kLetreroAb = {{{0, 0.0f}, {16, 0.0f}, {22, -1.0f}, {30, -0.03f}, {31, 0.03f}, {45, 1.0f}}};
    if (pf < 15.0f)
      if (const Tex* ps = Img(kPulsa)) {   // EU 0x22DBF0: la salida de "Pulsa START" (animacion 6, 665 #AMA)
        // centro de la clave 6: (320, 300) en px de 640x448
        const float z = clave1(kPulsaZoom, pf), w = ps->w * z * k, h = ps->h * z * kB1Alto * k;
        Pic(dl, *ps, a.At(cx - w * 0.5f, 300 * k * kB1Alto - h * 0.5f),
            a.At(cx + w * 0.5f, 300 * k * kB1Alto + h * 0.5f));
      }
    const float abre = pf < 45.0f ? clave1(kLetreroAb, pf) : 1.0f;
    if (const Tex* l = Img(kLetrero)) {
      // EU 0x22DBF0: registro 7 de 665 #AMA = letrero 666 #6 (200x136) en (320,294) de 640x448, y=1.
      const float w = l->w * std::fabs(abre) * k, h = l->h * kB1Alto * k;
      if (w > 0.5f)
        Pic(dl, *l, a.At(cx - w * 0.5f, 294 * k * kB1Alto - h * 0.5f),
            a.At(cx + w * 0.5f, 294 * k * kB1Alto + h * 0.5f), IM_COL32_WHITE, abre < 0.0f);
    } else if (pf >= 45.0f) {
      S::Frame(dl, a.At(cx - 180, 294 * k * kB1Alto - 140), a.At(cx + 180, 294 * k * kB1Alto + 140), a.s);
    }
    // EU 0x22DDA0 SatanTitle_Options: al acabar la animacion 7 (45 cuadros del letrero) se crean las opciones.
    // La elegida (0x5E9C10) recibe 10+7*i y su siguiente 11+7*i (FUN_001f9a80: +0x48); las otras conservan 9+7*i.
    if (pf >= 45.0f && dif_iniciada_ != g_cambio) {
      dif_iniciada_ = g_cambio;
      for (int i = 0; i < 3; ++i) dif_[i] = {9 + 7 * i, -1, letrero_at_};
      dif_[g_dificultad] = {10 + 7 * g_dificultad, 11 + 7 * g_dificultad, t};
    }
    const bool confirmando = confirmando_ == g_cambio;
    for (int pasada = 0; pasada < 2; ++pasada) {
      for (int r = 0; r < 3; ++r) {
        if ((r == g_dificultad) != (pasada == 1)) continue;   // la elegida se pinta al final (z=200 < 300)
        const bool sel = r == g_dificultad;
        int clip = dif_[r].clip;
        float f = std::max(0.0f, (t - dif_[r].at) * 60.0f);
        if (confirmando && sel) clip = 15 + 7 * r;
        if (clip == 10 + 7 * r && f > 8.0f) {         // entrada -> reposo (FUN_001f9a80(+0x48) = 11+7*i)
          clip = 11 + 7 * r;
          f -= 9.0f;
        }
        const float sc = EscalaDificultad(r, clip, f);
        const float sy = (clip == 9 + 7 * r && clip >= 0) ? 0.7f : sc;   // la apertura solo escala x
        const float y = (264.0f + 30.0f * float(r)) * k * kB1Alto;
        if (const Tex* d = Img(kDif + r)) {
          const float w = d->w * sc * k, h = d->h * sy * kB1Alto * k;
          if (w > 0.5f)
            Pic(dl, *d, a.At(cx - w * 0.5f, y - h * 0.5f), a.At(cx + w * 0.5f, y + h * 0.5f));
        } else if (pf >= 45.0f) {
          S::MenuText(dl, a.At(cx, y), 44 * a.s, NombreDificultad(r), sel, false);
        }
      }
    }
    if (pf < 45.0f) {   // EU 0x22DDA0: hasta que acaba la animacion 7 no se crean las opciones (no hay entrada)
      Ayuda(dl, a, {{"A", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
      if (pressed & kB) return Salir(false);
      return;
    }
    if (confirmando) {   // 0x22DE70: la elegida reproduce 15/22/29; 0x22DE70 pide la escena 0x1A (fundido 10)
      if ((t - confirm_at_) * 60.0f > 22.0f) {
        GuardarProgreso("hercule_dificultad", g_dificultad);
        g_puntos = 0;
        GuardarProgreso("hercule_puntos", 0);
        REXLOG_INFO("dbz3 pasarela: dificultad {} (nivel del COM del 1.er retador {}; sube con la ronda)",
                    kDificultad[g_dificultad], NivelComB3(g_dificultad, 0));
        elegida_ = g_cambio;
        cambio_at_ = t;
      }
      Ayuda(dl, a, {{"A", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
      return;
    }
    // EU 0x22DE70 SatanTitle_DifficultySelect: 3 opciones en ciclo; arriba 0x10 / abajo 0x40 y confirmar 0x4008.
    // Tabla 0x350870 del ELF (3 filas x 6 B): {13,14,15,10,11,12} {20,21,22,17,18,19} {27,28,29,24,25,26}; de ahi
    // salen entrada 10/17/24, reposo 11/18/25, salida 12/19/26, confirmar 15/22/29 y cancelar 13/20/27 (elegida)
    // + 14/21/28 (las demas). Suena el 2 al moverse y el 3 al confirmar.
    if (pressed & kUp) {
      const int vieja = g_dificultad;
      g_dificultad = (g_dificultad + 2) % 3;
      dif_[vieja] = {12 + 7 * vieja, -1, t};
      dif_[g_dificultad] = {10 + 7 * g_dificultad, 11 + 7 * g_dificultad, t};
      SeB1(2);   // EU 0x22DE70: arriba/abajo, FUN_00152690(0,0,2)
    }
    if (pressed & kDown) {
      const int vieja = g_dificultad;
      g_dificultad = (g_dificultad + 1) % 3;
      dif_[vieja] = {12 + 7 * vieja, -1, t};
      dif_[g_dificultad] = {10 + 7 * g_dificultad, 11 + 7 * g_dificultad, t};
      SeB1(2);
    }
    Ayuda(dl, a, {{"A", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on && pf > g_auto_seg * 60.0f) pressed |= kA;
    if (pressed & kB) return Salir(false);
    if (pressed & (kA | kStart)) {
      confirmando_ = g_cambio;
      confirm_at_ = t;
      confirm_opt_ = g_dificultad;
      dif_[g_dificultad] = {15 + 7 * g_dificultad, -1, t};
      SeB1(3);   // EU 0x22DE70 SatanTitle_DifficultySelect: suena el 3 al confirmar (FUN_00152690(0,0,3))
    }
  }

  // DS61/DS97: par de sprites del tablero. La tabla 0x350AB0 del ELF (22 u32, leida: -1,-1,0,1,2,3...18,19) da los
  // dos indices de 667 #AMA de cada ronda (r=0 sin par; r=1 -> 0/1 ... r=10 -> 18/19).
  // DS77: el par de verdad son DOS piezas distintas, no dos caras. 0x232140 crea las 11 caras (sprite 0x10+k con
  // animacion 0x3C+k = 667 #AMA 60+k: UV (0..503,0/72..144) de 668 #24, 72x72) y 0x231F20/0x232DC0 el par grande:
  // sprite 0x1B con animacion 0x3C+r (667 #AMA 60+r, la cara 668 #24 celda r) y sprite 0x1C con animacion 0x31+r
  // (667 #AMA 49+r, el letrero 668 #16 celda r de 136x72), en (320,180) y (320,336). El letrero es el que late
  // (0x232920/0x232A80, sprite 0x1C); la cara no. Antes el puerto pintaba la cara dos veces.
  // DS97: claves de 667 #AMA (DATA_<idioma>.AFS, banco 667) leidas del AFS del jugador: registro de 0x40 B con dos
  // canales de claves de 0x20 B (u32 cuadro + sx,sy,rot,px,py,pz,alfa). Cara k: registro 60+k (defeated) / 70+k
  // (silueta) en x = 80+48k, y = 408; aspa: registro 81 (48x56) en la misma posicion. Letrero de la ronda r:
  // registro 49+r (136x72) en x = 80+48r; cara grande: 60+r. El original dibuja el par en (320,180)/(320,336)
  // (0x232DC0) y la fila en 80+48k (0x232140); aqui se usan esas posiciones para la fila y la composicion medida
  // de la pantalla de resultado para el par (la captura de referencia orig_resultado_tablero.png).
  // EU 0x231F20 SatanBoard_LoadPair: lee el par de la tabla 0x350AB0[2*r] y lo deja en el estado del tablero.
  // EU 0x232140 SatanBoard_Faces: crea las 11 caras (0x3C+k) y las siluetas (0x46+k) en x = 80+48k y el par.
  // EU 0x232660 SatanBoard_RemoveCurrentFace: quita de la fila la cara del retador actual (0x1F9910(ronda+0x10)).
  // EU 0x232DC0 SatanBoard_DrawPair: pinta el par en (320,180) y (320,336) con la escala 0x5ECCDC (0..1 de fundido).
  void Par667(ImDrawList* dl, const S::Area& a, int r, float fundido, ImU32 tint, float escala = 1.0f, float late = 1.0f) {
    if (r < 1 || r > 10 || fundido <= 0.0f) return;
    const float alfa = std::clamp(fundido, 0.0f, 1.0f);
    const ImU32 col = (tint & 0x00FFFFFF) | (ImU32(float(tint >> 24) * alfa) << 24);
    if (const Tex* ch = Img(kChibis)) {
      const int celda = std::clamp(r, 0, 10);
      const float sx = float((celda % 7) * 72), sy = float((celda / 7) * 72);
      Trozo(dl, a, *ch, sx, sy, sx + 72, sy + 72, 320.0f * kB1, 180.0f * kB1 * kB1Alto, escala, col);
    }
    if (const Tex* rt = Img(kRetadores)) {
      const float lx = float((r % 3) * 136), ly = float((r / 3) * 72);
      Trozo(dl, a, *rt, lx, ly, lx + 136, ly + 72, 320.0f * kB1, 336.0f * kB1 * kB1Alto, escala * late, col);
    }
  }

  // Tablero antes de cada retador del 2.o al 11.o como el de B1 (escena 0x1C, FUN_00231f20-00232a80): las 11 caras de
  // 668 #24 (vencidos en color con el aspa 668 #17, el resto en silueta), la del siguiente en grande (crece de 0,7 a 1)
  // con "Challenger Nth" (668 #16) y la charla 669 (par: Mr. Satan, impar: el aviso) con sus voces (Voz); START sigue.
  // Con la escena 3D conectada la charla y las voces van en ella (Escena), justo despues. Tras perder se vuelve aqui con
  // Reintentar / Salir. B1 no tiene pantalla de derrota en el modo (no hay estado de "perdido" en satanmode.c): es una
  // pantalla propia; las caras van en fila arriba y la grande en medio.
  // EU 0x232060 SatanBoard_FadeIn: fundido de entrada de 0,05 por cuadro (20 cuadros) y dibuja el par.
  // EU 0x232140 SatanBoard_Faces: coloca las 11 caras (0x10-0x1A, las vencidas con el aspa 0x51) y el par grande.
  // EU 0x232660 SatanBoard_RemoveCurrentFace: quita de la fila la cara del retador actual.
  // EU 0x2327E0 SatanBoard_GrowPair: crece el par grande de 0,7 a 1 (0,01 por cuadro, 30 cuadros) y letrero 0x27.
  // EU 0x232920 SatanBoard_Pulse: late = 0,975 + 0,025*sin((cuadro % 30)*2pi/30).
  // EU 0x232A80 SatanBoard_WaitCall: voz del retador en el cuadro de 0x350B10 y parpadeo cada 30 cuadros.
  // EU 0x232CD0 SatanBoard_Exit: parpadeo mientras se sale al combate.
  void DrawTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, bool perdido) {
    if (!perdido && Img(kTablon) && Img(kTotal) && Img(kCifras)) return DrawResumenB1(dl, a, t, pressed, auto_on);
    const Paso& p = g_pasos[size_t(g_paso)];
    const int r = p.ronda, n = std::min(int(g_d->combates.size()), 11);
    const float e = t - cambio_at_;
    // EU 0x232060 SatanBoard_FadeIn / EU 0x232140 SatanBoard_Faces: fundido lineal de 0,05 por cuadro (20
    // cuadros = 0,33 s).
    const ImU32 col = IM_COL32(255, 255, 255, int(255 * std::clamp(e / (20.0f / 60.0f), 0.0f, 1.0f)));
    // EU 0x232920 SatanBoard_Pulse (y 0x232A80): late = 0,975 + 0,025*sin((cuadro % 30)*2pi/30) con el
    // contador ENTERO de cuadros del callback. Late el letrero del par (sprite 0x1C), no la cara.
    const int cuadro = int(std::max(0.0f, e) * 60.0f);
    const float late = 0.975f + 0.025f * std::sin(float(cuadro % 30) * (2.0f * 3.14159265f / 30.0f));
    if (const Tex* ch = Img(kChibis)) {
      for (int k = 0; k < n; ++k) {
        if (k == r) continue;   // EU 0x232660 SatanBoard_RemoveCurrentFace: la suya sale en grande (0x1F9910)
        const int i = k < r ? k : k + 10;   // 0-10 color; siluetas del 2.o al 11.o: 11-20
        const float sx = float((i % 7) * 72), sy = float((i / 7) * 72), x = 600 + (float(k) - 5.0f) * 92;
        Trozo(dl, a, *ch, sx, sy, sx + 72, sy + 72, x, 220, 1.1f, col);
        if (const Tex* cz = Img(kCruz); cz && k < r) PicAt(dl, a, *cz, x, 220, 64, col);
      }
      // EU 0x2327E0 SatanBoard_GrowPair: escala 0,7 + 0,01 por cuadro durante 30 cuadros (lineal, no Ease).
      const float k = 0.7f + 0.3f * std::clamp((e - 0.33f) / 0.5f, 0.0f, 1.0f);
      // EU 0x231F20 SatanBoard_LoadPair + EU 0x232DC0 SatanBoard_DrawPair: cara (668 #24) + letrero (668 #16)
      // en (320,180)/(320,336); el crecimiento 0,7->1 y el fundido 0..1 son los del original.
      Par667(dl, a, r, std::clamp(e / (20.0f / 60.0f), 0.0f, 1.0f), col, k, late);
    }
    if (!perdido && r >= 1 && !dbz3::cinematica::Conectada()) {
      Aviso(dl, a, 2 * (r - 1) + 1, {}, 585);
      Bocadillo(dl, a, t, 2 * (r - 1), "", 0.22f);
    }
    if (perdido) Ayuda(dl, a, {{"A", T::T("Reintentar", "Retry")}, {"B", T::T("Salir", "Quit")}});
    else Ayuda(dl, a, {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on && e > g_auto_seg) pressed |= perdido && g_intentos >= 2 ? kB : kA;
    if (pressed & kB) return Salir(false);
    if (perdido && (pressed & kA)) return Publicar(kPantalla);
    if (!perdido && (pressed & (kA | kStart))) Entrar(g_paso + 1);
  }

  // Combate: B1 pasa de la pantalla anterior al combate (0x1B) sin tarjeta; se pide en cuanto se ve el modo.
  void DrawCombate(float t) {
    if (dbz3::ingame::tutorial::Abierto() || t - lanzar_at_ < 1.0f) return;
    lanzar_at_ = t;
    if (!Lanzar()) REXLOG_WARN("dbz3 pasarela: el conector no acepto el combate (otro pendiente)");
  }

  // La cuenta del resultado y del final de B1 (FUN_00231c40 / FUN_00233680): el tablon 668 #15, "Now!!" (668 #19) con
  // los puntos del retador y "You have" (668 #21) con el total; luego 33 puntos por cuadro pasan del uno al otro (START
  // la acaba de golpe). Devuelve true al acabar. Efectos y tiempos de B1 (FUN_00233680 / FUN_00231c40): 33 puntos por
  // cuadro; efecto 0xE cada 10 cuadros mientras queda cuenta y 0xF al llegar a 0; 30 cuadros de espera antes de contar
  // (FUN_00231b90) y 30 despues (FUN_002338c0); jingle 1178 a los 30 cuadros del resultado (FUN_002319c0) y 1180 a los
  // 10 del final (FUN_002333d0); los letreros entran con las animaciones 0x15/0x16 (resultado) y 0x1B/0x1C (tablero),
  // 20 cuadros de fundido (FUN_00232140/002324c0) y el letrero late con 0,975 + 0,025*sin (FUN_00232920).
  bool Cuenta(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed) {
    if (cuenta_de_ != g_cambio) {
      cuenta_de_ = g_cambio;
      cuenta_ya_ = false;
      cuenta_fin_ = -1;
      cuenta_tic_ = -1;
      cuenta_se_fin_ = false;
      jingle_ = false;
    }
    const float e = t - cambio_at_, in = Ease(e / 0.35f);
    // EU 0x2319C0 SatanResult_Jingle: jingle 1178 a los 30 cuadros del resultado.
    // EU 0x2333D0 SatanClear_Jingle: en el final, jingle 1180 a los 10 cuadros.
    if (!jingle_ && e > (g_fase == kFin ? 0.167f : 0.5f) && !g_d->ruta.empty()) {
      jingle_ = true;
      OirAdxPs2(g_d->ruta, "adx_jp.afs", g_fase == kFin ? 1180 : 1178, Vol());
    }
    // EU 0x231B90 SatanResult_BeforeCount: 30 cuadros de espera (el tablon y la entrada de los letreros) antes de que
    // empiece a correr la cuenta.
    constexpr float kEmpieza = 0.5f + 0.6f + 0.6f + 0.5f;   // 30 cuadros, tablon, letrero, 30 cuadros (0x231b90)
    if (pressed & (kA | kStart)) cuenta_ya_ = true;
    const int ronda = std::max(0, g_ronda_pts);
    // EU 0x231C40 SatanResult_Count: 33 puntos por cuadro de la ronda al total, tope 999999; START pasa el resto de
    // golpe. En el final es la misma cuenta (0x233680).
    // EU 0x233680 SatanClear_Count: igual que 0x231C40, con el total del final.
    const int pasados = cuenta_ya_ ? ronda : std::clamp(int((e - kEmpieza) * 60.0f) * 33, 0, ronda);
    // EU 0x231C40 SatanResult_Count / EU 0x233680 SatanClear_Count: efecto 0xE cada 10 cuadros mientras corre la
    // cuenta y 0xF al llegar a 0 (FUN_00152690(0,0,id); el mismo par en el resultado y en el final).
    const int cuadro_cuenta = int(std::max(0.0f, e - kEmpieza) * 60.0f);
    if (!cuenta_ya_ && pasados > 0 && pasados < ronda && cuadro_cuenta / 10 != cuenta_tic_) {
      cuenta_tic_ = cuadro_cuenta / 10;
      SeB1(0xE, 6);
    }
    // DS31: tambien en el final (antes caia al tablon de resultado sobre el fondo azul). En el final el fondo ya lo
    // pinta DrawFin: negro + el primer plano congelado de Satan (como B1, FUN_002333d0).
    if (Img(kAhora) && Img(kTotal) && Img(kCifras)) {
      // Resultado de B1 (captura de PCSX2, 640x480): negro; "Now!!" (668 #19) en (132, 47) al 96 % con los puntos del
      // combate (cifras al 93 %, derecha en 425, arriba en 95) y Mr. Satan con "Total" (668 #18) en (439, 79) al 102 %
      // con el total (cifras al 62 %, derecha en 610, arriba en 277). Los letreros entran deslizandose (0,35 s).
      if (g_fase != kFin) FondoNegro(dl, a, false);
      const float des = (1.0f - in) * 220.0f;
      Spr(dl, a, *Img(kAhora), 132 - des, 47, 0.96f);
      Cifras(dl, a, ronda - pasados, 425 - des, 95, 0.93f);
      Spr(dl, a, *Img(kTotal), 439 + des, 79, 1.02f);
      Cifras(dl, a, std::min(999999, g_puntos - ronda + pasados), 610 + des, 277, 0.62f);
    } else {
    if (const Tex* tb = Img(kTablon)) Trozo(dl, a, *tb, 0, 0, tb->w, 262, 600, 255 + 20 * (1 - in), 1.25f);
    else S::Frame(dl, a.At(240, 110), a.At(960, 400), a.s);
    if (e > 0.5f || cuenta_ya_) {
      if (const Tex* ah = Img(kAhora)) Trozo(dl, a, *ah, 0, 0, ah->w, ah->h, 400, 455, 1.0f);
      else S::MenuText(dl, a.At(320, 455), 40 * a.s, T::T("¡Ahora!", "Now!!"), true, false);
      Numero(dl, a, ronda - pasados, 520, 460, 0.9f);
    }
    if (e > 1.1f || cuenta_ya_) {
      if (const Tex* ti = Img(kTienes)) Trozo(dl, a, *ti, 0, 0, ti->w, ti->h, 800, 455, 1.0f);
      else S::MenuText(dl, a.At(720, 455), 40 * a.s, T::T("Tienes", "You have"), true, false);
      Numero(dl, a, std::min(999999, g_puntos - ronda + pasados), 920, 460, 0.9f);
    }
    }
    if (e > 1.1f || cuenta_ya_) {   // de donde salen los puntos (B1 los apunta como avisos LIFE y TIME; aqui, la suma)
      // s9: sin la linea de la suma (B1 no la ensena; cotejado con orig_resultado_cuenta)
      // y, debajo, los sucesos de B1 que no son golpes (con las etiquetas del original): "REFLECT +600  ESCAPE... -300"
      std::string det;
      for (const char* k : {"ANTI-THROW", "REFLECT", "CUT-OFF", "RECOVERY", "BURST!", "AROUSE ;-)", "ESCAPE...", "DULL..."}) {
        const auto it = g_ronda_det.find(k);
        if (it == g_ronda_det.end() || it->second == 0) continue;
        char t[48];
        std::snprintf(t, sizeof(t), "%s%s %+d", det.empty() ? "" : "   ", k, it->second);
        det += t;
      }
      if (!det.empty()) S::MenuText(dl, a.At(g_fase == kFin ? 600 : 430, g_fase == kFin ? 600 : 690), 26 * a.s, det.c_str(), true, false, 800 * a.s);
    }
    const bool hecho = pasados >= ronda && (cuenta_ya_ || e > kEmpieza);
    if (hecho && cuenta_fin_ < 0) {
      cuenta_fin_ = t;
      if (!cuenta_se_fin_) {
        cuenta_se_fin_ = true;
        SeB1(0xF, 6);   // 0x231C40/0x233680: 0xF al acabar la cuenta
      }
    }
    return hecho;
  }

  // Resultado tras cada retador como B1 (SATAN RESULT MAIN, escena 0x1D, FUN_00231720-00231e60): jingle 1178,
  // la cuenta y, al acabarla, solo al tablero (0x1C) sin pulsar nada.
  // B1 dibuja en el tablon DOS sprites por ronda: FUN_00231f20 carga el par 2*r de la tabla 0x350AB0 (0..19) y
  // FUN_00232dc0 los pinta en (320,180) y (320,336). No es la animacion 0x16 (esa es de los letreros del resultado,
  // FUN_002319c0). El puerto no extrae esos sprites y pone el "Challenger Nth" (668 #16) del vencido.
  // EU 0x231720 SatanResult_Main: estado principal de la escena 0x1D (tablon, cuenta y paso al tablero).
  void DrawResultado(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool) {
    const bool hecho = Cuenta(dl, a, t, pressed);
    const int r = g_pasos[size_t(g_paso)].ronda;
    if (const Tex* rt = Img(kRetadores); rt && r < 11 && !(Img(kAhora) && Img(kTotal) && Img(kCifras))) {   // el de B1 no lo lleva
      const float cx = float((r % 3) * 136), cy = float((r / 3) * 72);
      Trozo(dl, a, *rt, cx, cy, cx + 136, cy + 72, 600, 300, 1.5f);
    }
    if (hecho && t - cuenta_fin_ > 1.0f) Entrar(g_paso + 1);
  }

  // Final como B1 (SATAN CLEAR MAIN, escena 0x1E, FUN_00233200-00234490), tras Cell sin resultado aparte: jingle 1180,
  // la misma cuenta y, con START, el total (668 #18) con su cifra; START vuelve al HUB. Sin record (B1 no lo
  // tiene). La contrasena de B1 (FUN_00235310) se calcula con ClaveB1: 2 numeros del LFSR, la dificultad, el aviso de
  // total descuadrado, 19 bits del total y su complemento, paridad, una de las 16 permutaciones de 0x350B70 y 12
  // simbolos de 0x35F890 (recuadro 669 #20 en B1, FUN_00234d90).
  // EU 0x2334F0 SatanClear_ShowScore: pinta el marcador con FUN_00234570 y espera 30 cuadros.
  // EU 0x2335E0 SatanClear_ShowTotal: espera la animacion del letrero y pinta el total con FUN_00234790.
  // EU 0x233AB0 SatanClear_WaitKey: espera la animacion y pinta la clave con FUN_00234990; START salta.
  // EU 0x233BB0 SatanClear_MakeKey: muestra el recuadro 0x29 y calcula la contrasena (FUN_00234B90/00235310).
  // EU 0x233CC0 SatanClear_KeyIn: funde el tablero y el recuadro de la clave (0,05 por cuadro = 20 cuadros), coloca el
  // recuadro con la tabla de 0x234D90 y pasa a 0x233FF0.
  // EU 0x233FF0 SatanClear_KeyShow: otros 20 cuadros con el recuadro (alfa = cuadro/40) y, con la animacion lista,
  // suma las 11 rondas (0x5ECC5C[2..12]) para el aviso de 0x235310 y pinta los 12 simbolos (FUN_00235100).
  // EU 0x234180 SatanClear_WaitStart: recuadro puesto y espera START (0x4008).
  // EU 0x234280 SatanClear_Hold100: 100 cuadros con el recuadro; al acabar oculta la clave (FUN_00235290) y pasa a
  // 0x234380.
  // EU 0x234380 SatanClear_Handoff: 5 cuadros, devuelve camara/tareas (0x1755C0) y cambia la musica (0x1EA810(10,0,0)).
  // EU 0x1EA810 SatanClear_Music: FUN_001EA810(pista,...) cambia la musica del juego (10 = tabla 0x360540[10] -> ADX 63);
  // aqui ArrancarMusica(ruta, 10) usa la misma tabla de B1 (kBgmB1).
  // EU 0x234410 SatanClear_ShowTotal100: 100 cuadros con el fondo del total (FUN_00234f60).
  // EU 0x234490 SatanClear_Exit: START vuelve al menu (FUN_00217120(0x19,0)).
  // EU 0x234550 SatanClear_DrawTotal: sigue pintando el fondo del total (FUN_00234f60) hasta que el motor cambia de
  // escena; aqui, mientras se espera START en el total.
  // EU 0x234D90 SatanClear_KeyBox: recuadro de la clave (sprite 0x14, 608x160) centrado en (640-w)/2, 370-h/2 con
  // el fundido param/20; aqui se usa la misma caja del 668 #25 (608x160) y, encima, la clave de ClaveB1 como texto
  // (los 12 simbolos del original son sprites 0x10-0x1B que el puerto no extrae).
  // EU 0x235290 SatanClear_HideKey: oculta los 12 simbolos (0x10-0x1B) y las dos flechas (0x1D/0x1E).
  void DrawFin(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (fin_de_ != g_cambio) {
      fin_de_ = g_cambio;
      fin_fase_ = 0;
      fin_fase_at_ = t;
      fin_musica_ = false;
      clave_fin_.clear();
    }
    {   // DS31: fondo del final como B1: el primer plano congelado de Satan (capturado al acabar la escena) en vez
        // de la pantalla azul del modo; si la captura no esta, negro (no el fondo azul).
      const ImVec2 d = ImGui::GetIO().DisplaySize;
      dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 255));
      DibujarFotoFinal(dl);
    }
    const float fe = t - fin_fase_at_;
    // EU 0x234990 SatanClear_DrawKey: base sprite 0x28 y digitos 2..7 sobre el letrero del final; aqui, la clave
    // de ClaveB1 centrada sobre el panel (la pieza 668 #25 hace de letrero).
    // EU 0x234B90 SatanClear_DrawKeyBox: base sprite 0x29 y digitos 9..14 dentro del recuadro de la clave.
    // EU 0x234D90 SatanClear_KeyBox: (640-w)/2, 370-h/2 (en px de la captura de 640x480) y los 12 simbolos dentro.
    auto Clave = [&](ImU32 tint, bool texto = true) {
      if (clave_fin_.empty()) clave_fin_ = ClaveB1(g_puntos, g_dificultad);
      if (const Tex* cl = Img(kClave))   // 668 #25 = la caja 0x14 (608x160) de 0x234d90
        Spr(dl, a, *cl, (640.0f - cl->w) * 0.5f, 370.0f - cl->h * 0.5f, 1.0f, tint);
      // DS143: cartel pequeno "You have ... Pts." de B1 en la esquina (668 #20 = sprite 0x28 de FUN_00234990) con el
      // total en las cifras pequenas (668 #23); captura b064: caja a la derecha arriba, cifras verdes dentro.
      if (const Tex* tp = Img(kTienesPeq)) {
        // captura b064 (640x480): el contenido de la 668 #20 va de (433,17) a (633,133): escala 1,12 y origen
        // (426,-9) (la imagen trae 23 px transparentes arriba); cifras verdes dentro, derecha en 585, centro y 77.
        Spr(dl, a, *tp, 426.0f, -9.0f, 1.12f, tint);
        const int nd = int(std::to_string(std::min(999999, g_puntos)).size());
        CifrasPeq(dl, a, std::min(999999, g_puntos), 585.0f - 35.0f * 0.8f - float(nd - 1) * 30.0f * 0.8f, 77.0f,
                  0.8f, tint, false);
      }
      // Los 12 simbolos (0x10-0x1B) se anaden al acabar 0x233ff0 (cuadro 40), no al crear la caja (cuadro 20).
      if (texto && !clave_fin_.empty()) {
        // B1 los pinta en 3 grupos de 4 separados por guiones (b064: HLF5-YPNF-QFDN); los simbolos sin glifo ASCII
        // (0x1F) siguen saliendo como '?'.
        std::string v = clave_fin_;
        for (size_t i = 4; i < v.size(); i += 5) v.insert(i, 1, '-');
        S::MenuText(dl, a.At(320 * kB1, 370 * kB1), 30 * a.s, v.c_str(), true, false);
      }
    };
    switch (fin_fase_) {
      case 0: {   // la cuenta del final (0x233680) y, al acabar, la clave (0x233AB0/0x233BB0)
        const bool antes = cuenta_de_ == g_cambio && cuenta_fin_ >= 0;   // la pulsacion que acaba la cuenta no pasa
        if (!Cuenta(dl, a, t, antes ? 0 : pressed)) return;
        fin_fase_ = 7;
        fin_fase_at_ = t;
        return;
      }
      case 7: {   // EU 0x2338C0 SatanClear_KeyIntro: 30 cuadros de espera (el contador entra en 0x1e) y 20 de
                  // fundido (0,05 por cuadro) de las cifras/letreros 1, 8 y 9-14; el fondo congelado sigue
                  // detras (antes el puerto fundia a negro y tapaba la escena). Los digitos de ronda 2-7 ya estan
                  // ocultos al acabar la cuenta (0x233680 -> 0x234570(0,...)).
        const float espera = (pressed & kStart) ? 0.0f : 30.0f / 60.0f;   // START salta la espera (0x2338c0)
        const float fundido = 20.0f / 60.0f;
        const float alfa = fe < espera ? 1.0f : std::clamp(1.0f - (fe - espera) / fundido, 0.0f, 1.0f);
        const ImU32 col = IM_COL32(255, 255, 255, int(255.0f * alfa));
        if (const Tex* ah = Img(kAhora)) Spr(dl, a, *ah, 132, 47, 0.96f, col);
        if (const Tex* to = Img(kTotal)) Spr(dl, a, *to, 439, 79, 1.02f, col);
        if (Img(kCifras)) Cifras(dl, a, std::min(999999, g_puntos), 610, 277, 0.62f, col);
        if (fe >= espera + fundido) {
          fin_fase_ = 1;
          fin_fase_at_ = t;
        }
        return;
      }
      case 1: {   // EU 0x233CC0 SatanClear_KeyIn + EU 0x233FF0 SatanClear_KeyShow: primero 20 cuadros de entrada
                  // de la caja (0x233cc0; aqui no hay el letrero 0x28/0x29 del 669) y despues 20 con la caja
                  // 0x14, cuyo grupo 0x27 va de alfa 0 a 0,5 (cuadro/40); los 12 simbolos aparecen al cuadro 40.
        const float t20 = 20.0f / 60.0f;
        if (fe >= t20) {
          const float f2 = std::clamp((fe - t20) / t20, 0.0f, 1.0f);
          Clave(IM_COL32(255, 255, 255, int(127.5f * f2 * f2)), false);   // caja: 0,5 * (cuadro/20)^2
        }
        if (fe >= 40.0f / 60.0f) {
          fin_fase_ = 2;
          fin_fase_at_ = t;
        }
        return;
      }
      case 2:   // EU 0x234180 SatanClear_WaitStart: START (bits 0x4008) con el recuadro puesto; el original solo
                // avanza con START, no con A. Al final de 0x233ff0 el grupo 0x27 queda en alfa 0,5.
        Clave(IM_COL32(255, 255, 255, 128));
        Ayuda(dl, a, {{"START", T::T("Seguir", "Continue")}});
        if (auto_on && fe > g_auto_seg) pressed |= kStart;
        if (pressed & kStart) {
          fin_fase_ = 3;
          fin_fase_at_ = t;
        }
        return;
      case 3:   // EU 0x234280 SatanClear_Hold100: 100 cuadros con el recuadro (alfa 0,5) y, al final, se oculta
                // la clave (0x235290)
        Clave(IM_COL32(255, 255, 255, 128));
        if (fe >= 100.0f / 60.0f) {
          fin_fase_ = 4;   // EU 0x235290 SatanClear_HideKey
          fin_fase_at_ = t;
        }
        return;
      case 4:   // EU 0x234380 SatanClear_Handoff: cuenta 5 cuadros y, en ese mismo cuadro, 0x1755c0 y
                // 0x1EA810(10,0,0); el original no condiciona el cambio de musica a que exista la ruta.
        if (fe >= 5.0f / 60.0f) {
          if (!fin_musica_) {
            fin_musica_ = true;
            // EU 0x1EA810 SatanClear_Music: pista 10 -> ADX 63 (la tabla de B1, kBgmB1).
            ArrancarMusica(g_d->ruta, 10, 0.0f, false);
          }
          fin_fase_ = 5;
          fin_fase_at_ = t;
        }
        return;
      default: {   // EU 0x234410 SatanClear_ShowTotal100 + EU 0x234490/0x234550: el total se pinta entero desde
                   // el primer cuadro (no hay entrada de 0,33 s en el original) y espera START
        const ImU32 col = IM_COL32_WHITE;
        // DS143: el cartel del final de B1 es el sprite 0x15 (668 #21, "You have ... Pts."), centrado por
        // FUN_00234f60 en (640-w)/2, (448-h)/2. El de "Total" con Mr. Satan (668 #18) es el del RESULTADO de ronda.
        if (g_fase == kFin) {
          if (const Tex* to = Img(kTienes)) {
            const float x = (640.0f - to->w) * 0.5f, y = (448.0f - to->h) * 0.5f;
            Spr(dl, a, *to, x, y, 1.0f, col);
            Cifras(dl, a, std::min(999999, g_puntos), x + 292.0f, y + 66.0f, 0.8f, col);   // a la izquierda de "Pts."
          } else {
            S::MenuText(dl, a.At(320 * kB1, 224 * kB1), 44 * a.s, T::T("You have", "You have"), true, false);
            Numero(dl, a, g_puntos, 320, 250, 0.9f);
          }
        } else if (const Tex* to = Img(kTotal)) {
          PicAt(dl, a, *to, 600, 400, 360, col);
          Numero(dl, a, g_puntos, 635, 498, 0.8f);   // a la izquierda de "Pts." del dibujo
        } else {
          char tot[64];
          std::snprintf(tot, sizeof(tot), T::T("Total: %d puntos", "Total: %d points"), g_puntos);
          S::MenuText(dl, a.At(600, 400), 44 * a.s, tot, true, false);
        }
        if (fin_fase_ == 5) {   // 0x234410: 100 cuadros de total antes de aceptar START
          if (fe >= 100.0f / 60.0f) fin_fase_ = 6;   // el tiempo sigue corriendo: el total no se vuelve a fundir
          return;
        }
        Ayuda(dl, a, {{"START", T::T("Volver al HUB", "Back to the HUB")}});
        if (auto_on && fe > g_auto_seg) pressed |= kStart;
        if (pressed & kStart) Salir(true);   // EU 0x234490 SatanClear_Exit: 0x4008 -> FUN_00217120(0x19,0)
      }
    }
  }

  enum : int { kLogo = 100, kFondo, kSatan, kTablon, kRetadores, kCruz, kTotal, kAhora, kTienes, kCifras, kChibis, kLetrero,
               kPulsa, kDif, kBarra = kDif + 3, kTienesPeq, kCifrasPeq, kClave, kLetras, kEstilo = kLetras + 2,
               kCaraSatan, kConsejo, kCopy = kConsejo + 6, kGradRes };
  std::map<int, Tex> tex_;
  uint32_t tex_gen_ = ~0u;
  const DatosB1* tex_src_ = nullptr;
  bool blocked_ = false, wait_release_ = true;
  uint64_t titulo_ = ~0ull;   // g_cambio en el que se pulso START en el titulo (despues, la dificultad)
  uint64_t elegida_ = ~0ull;  // g_cambio en el que se eligio la dificultad (despues, el prologo)
  // DS97: animacion de las tres dificultades (665 #AMA 9..29; ver EscalaDificultad).
  std::array<DifAnim, 3> dif_{};
  uint64_t dif_iniciada_ = ~0ull;   // g_cambio en el que se abrieron las opciones (cuadro 45 de la animacion 7)
  uint64_t confirmando_ = ~0ull;    // g_cambio en el que se confirmo (animacion 15/22/29 antes del prologo)
  float confirm_at_ = 0;
  int confirm_opt_ = 0;
  uint64_t prologo_de_ = ~0ull, musica_de_ = ~0ull, cuenta_de_ = ~0ull, fin_de_ = ~0ull;
  int prologo_k_ = -1, prologo_voz_ = -1;
  float prologo_at_ = 0, prologo_t0_ = 0, lanzar_at_ = -10, cuenta_fin_ = -1, fin_fase_at_ = 0;
  float letrero_at_ = -10;   // DS61: cuadro en que START abrio el letrero (0x22DDA0: 11 cuadros de animacion 7)
  int cuenta_tic_ = -1, fin_fase_ = 0;   // 0 cuenta, 7 fundido 0x2338C0, 1-2 clave, 3 espera 100, 4 5 cuadros, 5-6 total
  bool fin_musica_ = false;              // 0x234380: la musica del total (0x1EA810(10)) ya se pidio
  bool musica_demo_ = false, cuenta_ya_ = false, jingle_ = false, cuenta_se_fin_ = false;
  std::string clave_fin_;   // contrasena de B1 del total (FUN_00235310)
  uint16_t last_ = 0;
  uint64_t visto_ = ~0ull;
  float cambio_at_ = 0, alpha_ = 0;
  float negro_ = 0;   // S1-F03.1
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

// HUB: entrada "satan" ("Leyenda de Mr. Satan", la reserva hub_panel.h): siempre a la vista; sin la ISO de
// Budokai 1, en gris y el bocadillo dice Motivo() (RegisterGate).
void Abrir() {
  if (!Start()) REXLOG_WARN("dbz3 pasarela: no se pudo empezar");
}
const bool g_hub = dbz3::hub::RegisterMode("satan", &Abrir, &Busy) &&
                   dbz3::hub::RegisterGate("satan", &Disponible, &Motivo);

// PRUEBA: DBZ3_TEST_PASARELA=1 (o "auto": avanza solo; "auto,desde=N": empieza en el paso N; ",dificultad=D")
// empieza el modo cuando el menu principal lleva 3 s en pantalla y escribe el avance en el log ("dbz3 pasarela: ...").
const bool g_test = [] {
  const char* t = std::getenv("DBZ3_TEST_PASARELA");
  if (!t) return false;
  g_auto = std::strstr(t, "auto") != nullptr;
  int desde = -1, dif = -1;
  if (const char* d = std::strstr(t, "desde=")) desde = std::atoi(d + 6);
  if (const char* d = std::strstr(t, "dificultad=")) dif = std::atoi(d + 11);
  g_real = std::strstr(t, "real") != nullptr;
  if (const char* d = std::strstr(t, "seg=")) g_auto_seg = std::max(1.0f, float(std::atof(d + 4)));
  std::thread([desde, dif] {
    for (int quieto = 0; quieto < 120;) {
      quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    Leer(true);
    for (int i = 0; i < 400 && g_leyendo.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    if (desde >= 0) GuardarProgreso(kClave, desde);
    if (dif >= 0) GuardarProgreso("hercule_dificultad", dif);
    REXLOG_INFO("dbz3 pasarela: prueba: disponible {}; {}", Disponible(), Start() ? "empieza" : "no empieza");
  }).detach();
  return true;
}();

}  // namespace
}  // namespace dbz3::pasarela
