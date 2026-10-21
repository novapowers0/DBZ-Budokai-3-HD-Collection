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
#include "pasarela.h"

#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

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

namespace dbz3::hub {
// 0 = lo normal en cada campo; time_s > 0 segundos, < 0 sin tiempo; hp > 0 vida inicial y maxima, < 0 solo la
// inicial; form = forma inicial
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
bool OwnBattle();
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

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
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
    {36, "Bu (niño)", "Kid Buu"}, {37, "Dabura", "Dabura"}, {38, "Cooler", "Cooler"}, {42, "Saibaman", "Saibaman"}, {43, "Cell Jr.", "Cell Jr."}};

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
    std::fclose(f);
    std::filesystem::rename(tmp, Carpeta() / "progreso.toml", ec);
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
// Piccolo (forma 2 en B1) no cambia en B3: se queda en 0. Los personajes de mods (Zarbon de B1) usan las suyas.
int FormaB3(int id, int forma_b1) {
  static constexpr int kFormas[][3] = {{0, 4, 2}, {3, 1, 1}, {33, 2, 2}, {27, 1, 3}, {27, 2, 4}};   // ID de B3, B1, B3
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
        f.combo_max = f.combo_dano_max = 0;
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
            // golpe nuevo: la vida baja y llevaba mas de 60 ms quieta (un rayo baja en cada cuadro: es el mismo)
            if (b1_vida[v] >= 0 && hp < b1_vida[v] - 0.5f && !libre) {
              if (v == 1) combo_dano += b1_vida[v] - hp;   // tambien lo que baja un rayo en cada cuadro
              if (ahora - b1_golpe[v] > 60) {
                if (ahora - b1_golpe[v] > 1500) {
                  b1_combo[v] = 0;
                  if (v == 1) combo_dano = b1_vida[v] - hp;
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
  if (nombre && *nombre) S::MenuText(dl, a.At(x0 + w * 0.5f, y0 + w + 42 + bob), 44 * a.s, nombre, on, false);
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
const char* const kDificultad[] = {"Novato", "Experto", "Avanzado"};   // registro
// Los de las imagenes 666 #7-9 de la ISO espanola de B1, en su orden (0 Novice, 1 Adept, 2 Advanced): "Novel",
// "Avanzado", "Experto" (comprobado en la captura D:/DBZ3HD/_pruebas/20261016-113134_camp_test_satan/a2_dificultad.png).
const char* NombreDificultad(int k) {
  return k == 0 ? T::T("Novel", "Novice") : k == 1 ? T::T("Avanzado", "Adept") : T::T("Experto", "Advanced");
}
// Cuadros (60/s) de la voz 2 de la charla del retador r (1..10) en el tablero de B1 (0x350B10, FUN_00232a80), contados
// desde el estado que la espera; la voz 1 entra 80 cuadros antes de ese estado (FUN_002324c0 -> 00232920).
constexpr int kVoz2[10] = {500, 700, 700, 650, 500, 750, 830, 700, 890, 730};
int MsVoz2(int r) { return (80 + kVoz2[std::clamp(r, 1, 10) - 1]) * 1000 / 60; }   // de la voz 1 a la 2

std::mutex g_mu;
std::shared_ptr<const DatosB1> g_d;   // datos del modo en curso
std::vector<Paso> g_pasos;
int g_fase = kNada, g_paso = 0, g_intentos = 0;
int g_vida_ganada = 0;   // vida de Mr. Satan al ganar el ultimo combate (para los que no la recuperan)
int g_ki_ganada = -1;    // ki de Mr. Satan al ganar el ultimo combate (registro 0xFFFF: el que traia; -1 = ninguno)
int g_dificultad = 0;    // 0 Novato, 1 Experto, 2 Avanzado (nivel del COM)
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

void Salir(bool completo) {   // con g_mu
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
std::atomic<bool> g_previa_combate{false};   // S1-F03.1: el combate en curso es el "solo escena" (sin puntos en pantalla)
std::atomic<bool> g_previa_negro{false};     // S1-F03.1: tras la escena previa, negro hasta que B3 suelta el combate

void Entrar(int paso) {   // con g_mu
  g_previa_vista = false;
  g_paso = paso;
  g_intentos = 0;
  Publicar(paso >= int(g_pasos.size()) ? kFin : kPantalla);
}

// Puntos de un combate ganado como en B1 (0x198E18): los de los sucesos (Fin::puntos, ver Anotar) + LIFE (vida del
// ganador - la del otro) + TIME (segundos que sobran x 20); la leyenda suma la mitad (0x198F00: score / 2).
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

// Reglas que B1 tiene en codigo (satanmode.c) segun el retador: Androide 16 (B1 16) "dificil de danar", Trunks (5)
// "manos y piernas heridas: ataca con ki", Piccolo (6) "cuesta reunir ki". Ki inicial del rival: el de la tabla; el del
// jugador tambien (+0x2C): lo pone Lanzar (300 = 3 barras; 0xFFFF, como la vida de Krilin y Gohan, = el que traia).
// (Los multiplicadores de B1 no estan en el ejecutable: no hay lectura de la ronda en el codigo del combate; ver
// docs/14_frentes/pasarela.md "Reglas del original".)
Reglas ReglasDe(const CombateB1& c, int dificultad, int ronda) {
  Reglas r;
  r.nivel_com = NivelComB3(dificultad, ronda);
  if (const char* f = std::getenv("DBZ3_PASARELA_NIVEL")) r.nivel_com = std::clamp(std::atoi(f), 0, 2);   // pruebas
  r.ki_rival = float(c.ki_rival) / 100.0f;
  if (c.rival_b1 == 16) r.armadura_rival = 0.5f;
  if (c.rival_b1 == 5) r.solo_ki = true;
  if (c.rival_b1 == 6) r.ki_lento = 0.5f;
  return r;
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
// Duraciones medidas en B1 (s9b, capturas a camara lenta): 2,6 s la primera y 2,3 s la segunda (no 2,4).
void TomasPresentacion(dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  C::Paso p;
  p.plano = C::kLado1;
  p.segundos = 2.6f;
  p.anim[0] = C::kPoseEntrada1;
  Toma(p, {-2.0f, 1.2f, 27.0f}, {-15.0f, 8.5f, 0.0f}, {-4.0f, 4.0f, 25.0f}, {-15.0f, 9.0f, 0.0f});
  e.pasos.push_back(p);
  p = C::Paso{};
  p.plano = C::kLado2;
  p.segundos = 2.3f;
  Toma(p, {24.0f, 8.0f, 28.0f}, {15.0f, 7.0f, 0.0f}, {23.0f, 7.5f, 29.0f}, {15.0f, 7.0f, 0.0f});
  e.pasos.push_back(p);
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
  TomasPresentacion(e);
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
    // 4. por encima del hombro de Cell, Goku llega delante (corte del subtitulo a los 10,6 s: [H] +-1 s, capturas)
    {dbz3::cinematica::kLado1, 10.62f, -1, "Solo hay un montón de perdedores... ¿Qué será de la Tierra?",
     "Nothing but a bunch of losers out there... What will happen to the Earth?", DBZ3_PREVIA(hombro1)},
    // y Mr. Satan grita desde fuera (la misma toma sigue)
    {dbz3::cinematica::kLado1, 3.80f, kJugador, "¡Espeeera!", "Waaait!", DBZ3_PREVIA(hombro2)},
};
#undef DBZ3_PREVIA
// Campo de vision vertical de B1 en estas tomas: medido proyectando a Goku (suela y = 3,9 en el pixel 385, punta del pelo
// 24,07 en el 95, captura del estado de PCSX2 a los 33,5 s, 640x480) con el ojo y el objetivo leidos: 0,579 rad, que es
// 2 atan(0,3) = 0,583 (la camara de B1 guarda 1,2 = 4 x 0,3 en +0). B3 usa 0,785 (mgr+0x1C4, cine.cpp).
constexpr float kFovB1 = 0.583f;
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
    if (t.cara == kCellB3) p.anim[1] = C::kPoseEntrada2;
    e.pasos.push_back(p);
  }
}
// El combate "solo escena": 1 s de tiempo (el reloj esta parado mientras dura la escena, cinematica.cpp) y, al acabar
// la escena, se da por terminado como un ring out del lado 1 (mgr+35569 = hueco del ganador + 1, como los retos de
// VigilarCombate) para no dejar ese segundo de pelea. Sin puntos ni avance: al soltar el combate se vuelve al mismo
// paso y DrawCombate pide el combate de Yamcha (Lanzar).
bool LanzarPrevia(int escenario) {   // con g_mu
  namespace C = dbz3::cinematica;
  C::Escena e;
  e.iso_voces = g_d->ruta;
  e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  e.distancia = 30;
  EscenaPrevia(e);
  if (!dbz3::hub::RequestBattleEx(kGokuB3, kCellB3, escenario, true, 1, 0, 0, kFormaSsj, kFormaPerfecto)) return false;
  C::Empezar(std::move(e));
  g_previa_vista = true;
  REXLOG_INFO("dbz3 pasarela: escena previa de los Juegos de Cell (paso {}): Goku (forma {}) contra Cell (forma {}), "
              "escenario {}, sin puntos", g_paso + 1, kFormaSsj, kFormaPerfecto, escenario);
  CallarVozB1();
  CallarAdxPs2();
  Publicar(kLuchando);
  const uint64_t cambio = g_cambio;
  g_previa_combate = true;
  // B1 entra en la escena sin presentacion: el vuelo y las entradas de B3 (estados 1 y 2, ~20 s) se saltan con START
  // (como el jugador; hub.cpp: "START la salta") en el mando del lado 1, y lo que B3 pone tras el fin (ring out,
  // victoria, estados 5-8) se tapa con un fundido a negro (Panel) y tambien se acelera con START.
  std::thread([] {
    uint8_t* base = Base();
    const int64_t t0 = NowMs();
    bool vista = false, acabada = false, pulsado = false, sin_hud = false;
    int64_t t_pulso = 0;
    auto soltar = [&] {
      if (pulsado) dbz3::net::InyectarPad(0, -1);
      pulsado = false;
    };
    while (base && NowMs() - t0 < 180000) {
      const bool own = dbz3::hub::OwnBattle();
      const uint32_t mgr = own ? REX_LOAD_U32(kMgrPtr) : 0;
      const uint32_t st = mgr ? REX_LOAD_U32(mgr + 35128) : 0;
      if (!own && (vista || acabada || NowMs() - t0 > 60000)) break;
      const bool saltar = st == 1 || st == 2 || (acabada && st >= 5 && st <= 7);
      if (saltar && NowMs() - t_pulso > (pulsado ? 120 : 400)) {
        t_pulso = NowMs();
        pulsado = !pulsado;
        dbz3::net::InyectarPad(0, pulsado ? 0x0010 : -1);
      } else if (!saltar) {
        soltar();
      }
      if (st >= 1 && st <= 3 && !sin_hud) {   // B1 no tiene marcador antes de la escena (el READY de B3 si sale)
        sin_hud = true;
        dbz3::combate::OcultarHud(true);
      }
      vista = vista || (st == 4 && C::Activa());
      if (st == 4 && vista && !acabada && !C::Activa()) {
        acabada = true;
        g_previa_negro = true;
        dbz3::combate::OcultarHud(true);
        REX_STORE_U8(mgr + 35569, 1);
        REXLOG_INFO("dbz3 pasarela: escena previa acabada: fin del combate solo escena");
      }
      if (st >= 5 && st <= 8 && !acabada) {   // acabo solo (el segundo de tiempo)
        acabada = true;
        g_previa_negro = true;
        dbz3::combate::OcultarHud(true);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    soltar();
    if (acabada || sin_hud) dbz3::combate::OcultarHud(false);
    g_previa_negro = false;
    g_previa_combate = false;
  }).detach();
  Reglas r;
  r.capsulas = false;
  VigilarCombate("pasarela: escena previa", r, [cambio](const Fin& f) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fase != kLuchando || g_cambio != cambio) return;
    REXLOG_INFO("dbz3 pasarela: escena previa terminada (ganador {}); sigue el combate del paso {}", f.ganador + 1,
                g_paso + 1);
    Publicar(kPantalla);   // mismo paso: DrawCombate pide el combate de verdad
  });
  return true;
}

// Escena del retador r (1..10) tras su tablero (B1 pone la charla en el tablero, 0x231F20-0x232A80; aqui va en 3D por
// peticion del usuario, en el ring de B3): charla 669 par = lo que dice Mr. Satan, impar = aviso de la regla; voz 1
// 1269 + 2 (r - 1) y la 2 a los cuadros de B1 (MsVoz2). Yamcha (r = 0) no tiene charla ni tablero en B1: sin escena.
// Planos: los dos, Mr. Satan hablando, el rival, el aviso.
dbz3::cinematica::Escena Escena(int r, int rival) {   // con g_mu
  namespace C = dbz3::cinematica;
  C::Escena e;
  e.iso_voces = g_d->ruta;
  e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  e.distancia = 30;   // la tabla de B1 pone a los dos lados en x = -15 y +15
  // SONDA (DBZ3_CINE_KI=1): busca la carga de ki (accion 21) manteniendo 2,5 s cada combinacion de botones de cara
  // por el mando virtual; la sonda DBZ3_CINE_TRAZA apunta la accion y el codigo de cada una
  if (std::getenv("DBZ3_CINE_KI")) {
    // (2026-10-16: las 11 de cara dan 0/1/13/15, ninguna 21; esta vuelta, con LB 0x0100 / RB 0x0200)
    for (const uint16_t b : {0x0100, 0x0200, 0x0300, 0xA100, 0xA200, 0x2200, 0x8200, 0xA300, 0x2100, 0x8100, 0x1300}) {
      C::Paso q;
      q.plano = C::kLado1;
      q.segundos = 2.5f;
      q.botones[0] = b;
      e.pasos.push_back(q);
    }
    return e;
  }
  const size_t ch = size_t(2 * (r - 1));
  const bool hay = r >= 1 && ch + 1 < g_d->charlas.size();
  C::Paso p;
  p.segundos = 2.5f;
  p.anim[0] = C::kPoseEntrada1;   // Satan con su pose de entrada de B3 (auditoria10/D); el rival en guardia: con la
  e.pasos.push_back(p);           // suya (0x280) se giraba de espaldas a la camara (captura 224641 c1)
  p.anim[0] = -1;
  // Mr. Satan dice su linea con su pose de victoria de B3 y el rival le contesta con la suya
  p.plano = C::kLado1;
  p.cara = kJugador;
  p.anim[0] = C::kPoseVictoria;
  p.segundos = 2.5f;
  if (hay) {
    p.texto = g_d->charlas[ch];
    p.voces = {1269 + 2 * (r - 1), -MsVoz2(r), 1270 + 2 * (r - 1)};
    p.segundos = float(MsVoz2(r)) / 1000.0f + 4.0f;   // ponytail: 4 s para la voz 2 (largo sin medir)
  }
  e.pasos.push_back(p);
  p = C::Paso{};
  p.plano = C::kLado2;
  p.segundos = 3.0f;
  p.anim[1] = C::kPoseVictoria;   // la entrada (0x280) lleva al personaje hacia delante: sale cortado de cerca
  e.pasos.push_back(p);
  if (hay) {
    p = C::Paso{};
    p.segundos = 3.5f;
    p.texto = g_d->charlas[ch + 1];
    e.pasos.push_back(p);
  }
  TomasPresentacion(e);   // s9b: como B1, las dos tomas cortas antes del "READY?"
  return e;
}

bool Lanzar() {   // con g_mu: el combate del paso actual, con las reglas del original
  const Paso& p = g_pasos[size_t(g_paso)];
  const CombateB1& c = g_d->combates[size_t(p.ronda)];
  const int escenario = EscenarioB3(c.escenario_nombre);
  // S1-F03.1: antes del combate 1, la escena previa de los Juegos de Cell (una vez por paso; no con DBZ3_SIN_CINE)
  static const bool sin_cine = std::getenv("DBZ3_SIN_CINE") != nullptr || std::getenv("DBZ3_SIN_PREVIA") != nullptr;
  if (p.ronda == 0 && p.vez == 0 && !g_previa_vista && !sin_cine) return LanzarPrevia(escenario);
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
  if (p.vez == 0 && p.ronda > 0) {
    dbz3::cinematica::Empezar(Escena(p.ronda, c.rival));
  } else if (p.vez == 0) {   // ronda 1 (Yamcha): la escena de los Juegos de Cell (aproximada) y las dos tomas
    dbz3::cinematica::Escena e;
    e.iso_voces = g_d->ruta;
    e.volumen = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
    e.distancia = 30;
    EscenaCell(e);
    dbz3::cinematica::Empezar(e);
  }
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
  if (!dbz3::hub::RequestBattleEx(kJugador, c.rival, escenario, true, tiempo, vida1, vida2, 0, forma2)) return false;
  r.traza = std::getenv("DBZ3_PASARELA_TRAZA") != nullptr;   // pruebas: vida y ki de los dos cada segundo
  REXLOG_INFO("dbz3 pasarela: ronda {} ({}/{}): id {} contra id {} (B1 {}, forma B1 {} -> {}), escenario {} ({}), "
              "tiempo {}, vida {} / {}; reglas: {} (COM {}), ki del rival {:.1f} barras, armadura {:.2f}, solo ki {}, "
              "ki lento {:.2f}", p.ronda + 1, p.vez + 1, c.cantidad, kJugador, c.rival, c.rival_b1, c.forma, forma2,
              escenario, c.escenario_nombre, tiempo, vida1, vida2, kDificultad[g_dificultad], r.nivel_com, r.ki_rival,
              r.armadura_rival, r.solo_ki, r.ki_lento);
  REXLOG_INFO("dbz3 pasarela: ki del jugador del original (paso {}): {:.1f} barras ({})", g_paso + 1, r.ki_jugador, ki_de);
  CallarVozB1();
  CallarAdxPs2();
  Publicar(kLuchando);
  const uint64_t cambio = g_cambio;
  VigilarCombate(("pasarela: paso " + std::to_string(g_paso + 1)).c_str(), r, [cambio](const Fin& f) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fase != kLuchando || g_cambio != cambio) return;
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
        Entrar(g_paso + 1);
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

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const int fase = g_fase_pub.load();
    const bool menu = dbz3::ingame::GameScreen() == kMainScreen;
    const bool show = (fase == kPantalla || fase == kPerdido || fase == kFin || fase == kResultado) && menu &&
                      !dbz3::GameDimmed() && !dbz3::OverlayOpen();
    if (auto* in = Input(); in && show != blocked_) {   // mientras se ve, el juego no recibe los mandos
      if (show) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
      blocked_ = show;
    }
    const float t = float(ImGui::GetTime()), dt = std::min(io.DeltaTime, 0.1f);
    alpha_ = std::clamp(alpha_ + (show ? dt * 4.0f : -dt * 8.0f), 0.0f, 1.0f);
    // en el combate: puntos y avisos de los sucesos (como el anillo de avisos de B1)
    {   // S1-F03.1: fundido a negro tras la escena previa (tapa el ring out y la victoria de B3)
      negro_ = std::clamp(negro_ + (g_previa_negro.load() ? 1.0f : -2.0f) * std::min(io.DeltaTime, 0.1f) * 3.0f, 0.0f, 1.0f);
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
    const bool titulo_b1 = g_fase != kResultado && g_fase != kFin && g_fase != kPerdido && size_t(g_paso) < g_pasos.size() &&
                           g_pasos[size_t(g_paso)].tipo == kPresentacion && elegida_ != g_cambio && Img(kPulsa);
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
  }

  // Voces y jingles del original (ADX_JP.AFS; -ms = a esos ms de la entrada, los cuadros de B1); cualquier otra
  // pantalla los corta. Tablero: voz 1 a los 61 cuadros (FUN_002324c0) y la 2 MsVoz2 despues, si la charla no va en
  // la escena 3D. Los jingles del resultado y del final van en Cuenta (se mezclan y siguen en el tablero).
  void Voz() {   // con g_mu
    const Paso& p = g_pasos[size_t(std::min<int>(g_paso, int(g_pasos.size()) - 1))];
    std::vector<int> e;
    if (g_fase == kPantalla && p.tipo == kCharla && p.ronda >= 1 && !dbz3::cinematica::Conectada())
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
    if (!c) S::MenuText(dl, a.At(x0 + 15.0f * float(s.size()) * k, cy), 40 * k * a.s, s.c_str(), true, false);
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
    for (auto it = suc.rbegin(); it != suc.rend(); ++it) {
      const float edad = float(ahora - it->ms) / 1000.0f;
      if (edad > 3.0f) continue;
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
    S::MenuText(dl, a.At(x1 - 18.0f * float(s.size()) * k, cy), 52 * k * a.s, s.c_str(), true, false);
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
    else S::MenuText(dl, a.At(600, cy), 34 * a.s, texto.c_str(), true, false);
  }

  void Ayuda(ImDrawList* dl, const S::Area& a, const std::vector<std::pair<const char*, const char*>>& items) {
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, items);
  }

  static float Vol() { return dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()); }

  // Prologo "Mr. Satan's Style" como B1 (SATAN DEMO MAIN, escena 0x1A, satanmode.c 0x22E380-0x2307AC), despues de
  // elegir la dificultad (FUN_0022de70 pide la 0x1A): la filosofia (668 #6), "The Art of Combat!" (668 #8, voz 1262)
  // y 5 consejos con la cara de Mr. Satan (668 #7; #10-#14: golpe 1263 y a los 139 cuadros su voz 1264-1268). Musica
  // 79 del demo a los 30 cuadros (FUN_0022e8f0) y voz 1261 a los 90 (FUN_0022e9d0). A pasa, START lo salta. Tiempos
  // (cuadros a 60/s): filosofia 30 + 60 + 1200 (FUN_0022eb90), "The Art of Combat!" 209 (FUN_0022f140), consejos
  // 139 + 349 / 239 / 309 / 229 / 369 (FUN_0022f2b0 a 0x230360). Al acabar, el combate 1 (FUN_00217120(0x1B)), sin
  // tarjeta. ponytail: B1 pasa detras su escenario 3D desenfocado (FUN_002304c0); aqui el fondo del modo oscurecido.
  void DrawPrologo(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (prologo_de_ != g_cambio) {
      prologo_de_ = g_cambio;
      prologo_k_ = 0;
      prologo_at_ = prologo_t0_ = t;
      prologo_voz_ = -1;
      musica_demo_ = false;
      CallarAdxPs2();   // la 79 sustituye a la 1259 del titulo (mismo canal 0 en B1)
    }
    if (!musica_demo_ && t - prologo_t0_ > 0.5f) {
      musica_demo_ = true;
      if (!g_d->ruta.empty()) OirAdxPs2(g_d->ruta, "adx_jp.afs", 79, Vol());
    }
    const int k = prologo_k_;
    if (prologo_voz_ != k) {
      prologo_voz_ = k;
      REXLOG_INFO("dbz3 pasarela: prologo {}/7", k + 1);
      const std::vector<int> e = k == 0   ? std::vector<int>{-1500, 1261}
                                 : k == 1 ? std::vector<int>{1262}
                                          : std::vector<int>{1263, -2317, 1262 + k};
      if (!g_d->ruta.empty()) OirVozB1(g_d->ruta, e, Vol());
    }
    Fondo(dl, a, false);
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 255));   // B1: negro puro (medido en la captura)
    const float tp = t - prologo_at_;
    constexpr int kConsejoCuadros0[5] = {349, 239, 309, 229, 369};
    const float dur0 = k == 0 ? 1290.0f / 60.0f : k == 1 ? 209.0f / 60.0f : float(139 + kConsejoCuadros0[k - 2]) / 60.0f;
    if (k == 0) {
      PrologoFilosofia(dl, a, tp, dur0);
    } else {
      PrologoConsejos(dl, a, k, tp);
    }
    Ayuda(dl, a, {{"A", T::T("Siguiente", "Next")}, {"START", T::T("Saltar", "Skip")}, {"B", T::T("Salir", "Quit")}});
    constexpr int kConsejoCuadros[5] = {349, 239, 309, 229, 369};
    const float dur = k == 0 ? 1290.0f / 60.0f : k == 1 ? 209.0f / 60.0f : float(139 + kConsejoCuadros[k - 2]) / 60.0f;
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
  // Fondo del resultado de B1: negro y, de la fila 300 a la 480, un degradado a rojo oscuro (medido: 0 -> 90 de rojo).
  void FondoNegro(ImDrawList* dl, const S::Area& a, bool degradado) {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 255));
    if (!degradado) return;
    const float y0 = a.At(0, 300 * kB1).y, y1 = std::max(a.At(0, 480 * kB1).y, d.y);
    dl->AddRectFilledMultiColor(ImVec2(0, y0), ImVec2(d.x, y1), IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255),
                                IM_COL32(90, 0, 0, 255), IM_COL32(90, 0, 0, 255));
  }
  // Pantalla de resultado de B1 tras cada combate: la charla y el "NEXT BATTLE" en una sola (medida contra la captura de
  // PCSX2, 640x480, con las mismas texturas de 668/669): tablon 668 #15 en (28, 17); Mr. Satan y "Total" (668 #18) en
  // (439, 79) al 102 %; charla 669 par en (87, 122) y aviso 669 impar en (160, 337); cifras del total (610, 277) al 62 %;
  // abajo la fila de caras 668 #24 (la vencida con el aspa 668 #17 en (56, 408), la que sigue en (100, 393) al 76 % con
  // su "Challenger Nth" 668 #16 en (155, 398) y las siguientes en siluetas desde (303, 408) cada 47).
  void DrawResumenB1(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    const Paso& p = g_pasos[size_t(g_paso)];
    const int r = p.ronda, n = std::min(int(g_d->combates.size()), 11);
    const float e = t - cambio_at_;
    const ImU32 col = IM_COL32(255, 255, 255, int(255 * Ease(e / 0.33f)));   // fundidos de 20 cuadros
    FondoNegro(dl, a, true);
    if (const Tex* tb = Img(kTablon)) Spr(dl, a, *tb, 28, 17, 1.0f, col);
    if (const Tex* st = Img(kTotal)) Spr(dl, a, *st, 439, 79, 1.02f, col);
    Cifras(dl, a, g_puntos, 610, 277, 0.62f, col);
    if (r >= 1) {
      if (const Tex* tx = Img(2 * (r - 1))) Spr(dl, a, *tx, 87, 122, 1.0f, col);
      if (const Tex* av = Img(2 * (r - 1) + 1)) Spr(dl, a, *av, 160, 337, 1.0f, col);
    }
    if (const Tex* ch = Img(kChibis)) {
      auto cara = [&](int celda, float x, float y, float s) {
        const float sx = float((celda % 7) * 72), sy = float((celda / 7) * 72);
        Spr(dl, a, *ch, x - sx * s, y - sy * s * kB1Alto, s, col, sx, sy, sx + 72, sy + 72);
      };
      for (int k = std::max(0, r - 2); k < r; ++k) {   // vencidos (los dos ultimos), con el aspa
        const float x = 56 - 30.0f * float(r - 1 - k);
        cara(k, x, 408, 0.7f);
        if (const Tex* cz = Img(kCruz)) Spr(dl, a, *cz, x - 1, 410, 1.0f, col);
      }
      if (r < n) cara(r, 100, 393, 0.76f);
      for (int j = 0; j < 6 && r + 1 + j < n; ++j) cara(r + 1 + j + 10, 303 + 47.0f * float(j), 408, 0.62f);
    }
    const float late = 0.975f + 0.025f * std::sin(e * 2.0f * 3.14159265f);   // el letrero late (FUN_00232a80)
    if (const Tex* rt = Img(kRetadores); rt && r < 11) {
      const float cx = float((r % 3) * 136), cy = float((r / 3) * 72);
      Spr(dl, a, *rt, 155 - cx, 398 - cy * kB1Alto, 1.0f, col, cx, cy, cx + 136, cy + 72, 223, 436, late);
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
  void PantallaTitulo(ImDrawList* dl, const S::Area& a) {
    const float k = kB1;
    if (const Tex* s = Img(kSatan)) Pic(dl, *s, a.At(65 * k, 8 * k), a.At((65 + s->w) * k, (8 + s->h * kB1Alto) * k));
    if (const Tex* l = Img(kLogo)) {
      const float hw = l->w * 0.975f * 0.5f * k, hh = l->h * 0.975f * kB1Alto * 0.5f * k, cx = 210 * k, cy = 118 * k;
      const float c = std::cos(20.0f * 3.14159265f / 180.0f), sn = std::sin(20.0f * 3.14159265f / 180.0f);
      auto P = [&](float x, float y) { return a.At(cx + x * c + y * sn, cy - x * sn + y * c); };
      dl->AddImageQuad(reinterpret_cast<ImTextureID>(l->tex.get()), P(-hw, -hh), P(hw, -hh), P(hw, hh), P(-hw, hh),
                       ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1));
    }
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float y0 = a.At(0, 315 * k).y, y1 = std::max(a.At(0, 480 * k).y, d.y);
    dl->AddRectFilledMultiColor(ImVec2(0, y0), ImVec2(d.x, y1), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 250), IM_COL32(0, 0, 0, 250));
    if (const Tex* c2 = Img(kCopy))
      Pic(dl, *c2, a.At(75 * k, 416 * k), a.At((75 + c2->w) * k, (416 + c2->h * kB1Alto) * k));
  }

  // Titulo como el de B1 (SATAN MODE MAIN, escena 5): fondo 666 #0, Mr. Satan 666 #1, logo 666 #2, "Pulsa START"
  // 666 #5 y la musica 1259 a los 50 cuadros (FUN_0022db00). START abre el letrero 666 #6 con Novice / Adept /
  // Advanced (666 #7-#9, arriba/abajo); al confirmar se guarda la dificultad y sigue el prologo (FUN_0022de70).
  void DrawPresentacion(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (elegida_ == g_cambio) {
      if (Img(kEstilo) && Img(kConsejo + 5)) return DrawPrologo(dl, a, t, pressed, auto_on);
      CallarAdxPs2();
      return Entrar(g_paso + 1);
    }
    if (musica_de_ != g_cambio && t - cambio_at_ > 50.0f / 60.0f) {
      musica_de_ = g_cambio;
      if (!g_d->ruta.empty()) OirAdxPs2(g_d->ruta, "adx_jp.afs", 1259, Vol());
    }
    const bool titulo = Img(kPulsa) && titulo_ != g_cambio;
    PantallaTitulo(dl, a);
    if (const Tex* ps = Img(kPulsa); ps && titulo) {
      const float in = Ease((t - cambio_at_) / 0.3f);
      const int alfa = int(in * (165.0f + 90.0f * std::sin(t * 4.0f)));
      // "Press START button" de B1: 666 #5 a 1:1, centrado en (321, 324) de la captura de 640x480
      const float k = kB1, w = ps->w * k, h = ps->h * kB1Alto * k;
      Pic(dl, *ps, a.At(321 * k - w * 0.5f, 324 * k - h * 0.5f), a.At(321 * k + w * 0.5f, 324 * k + h * 0.5f), IM_COL32(255, 255, 255, std::clamp(alfa, 0, 255)));
      Ayuda(dl, a, {{"START", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
      if (auto_on && t - cambio_at_ > g_auto_seg) pressed |= kStart;
      if (pressed & kB) return Salir(false);
      if (pressed & (kA | kStart)) {
        titulo_ = g_cambio;
        cambio_at_ = t;
        REXLOG_INFO("dbz3 pasarela: titulo: START (dificultad)");
      }
      return;
    }
    // Letrero de la dificultad de B1: 666 #6 al 95 % centrado en (317, 318) de la captura; filas a 286 + 32,5 por fila
    // (elegida al 95 %, las otras al 80 % y mas oscuras), sin franja de seleccion.
    const float k = kB1, cx = 317 * k;
    if (const Tex* l = Img(kLetrero)) {
      const float w = l->w * 0.95f * k, h = l->h * 0.95f * kB1Alto * k;
      Pic(dl, *l, a.At(cx - w * 0.5f, 318 * k - h * 0.5f), a.At(cx + w * 0.5f, 318 * k + h * 0.5f));
    } else {
      S::Frame(dl, a.At(cx - 180, 318 * k - 140), a.At(cx + 180, 318 * k + 140), a.s);
    }
    for (int r = 0; r < 3; ++r) {
      const float y = (286 + 32.5f * float(r)) * k;
      const bool sel = r == g_dificultad;
      if (const Tex* d = Img(kDif + r)) {
        const float sc = sel ? 0.95f : 0.8f, w = d->w * sc * k, h = d->h * sc * kB1Alto * k;
        Pic(dl, *d, a.At(318 * k - w * 0.5f, y - h * 0.5f), a.At(318 * k + w * 0.5f, y + h * 0.5f),
            sel ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255));
      } else {
        S::MenuText(dl, a.At(cx, y), 44 * a.s, NombreDificultad(r), sel, false);
      }
    }
    if (pressed & kUp) g_dificultad = (g_dificultad + 2) % 3;
    if (pressed & kDown) g_dificultad = (g_dificultad + 1) % 3;
    Ayuda(dl, a, {{"A", T::T("Empezar", "Start")}, {"B", T::T("Salir", "Quit")}});
    if (auto_on && t - cambio_at_ > g_auto_seg) pressed |= kA;
    if (pressed & kB) return Salir(false);
    if (pressed & (kA | kStart)) {
      GuardarProgreso("hercule_dificultad", g_dificultad);
      g_puntos = 0;
      GuardarProgreso("hercule_puntos", 0);
      REXLOG_INFO("dbz3 pasarela: dificultad {} (nivel del COM del 1.er retador {}; sube con la ronda)", kDificultad[g_dificultad],
                  NivelComB3(g_dificultad, 0));
      elegida_ = g_cambio;
      cambio_at_ = t;
    }
  }

  // Tablero antes de cada retador del 2.o al 11.o como el de B1 (escena 0x1C, FUN_00231f20-00232a80): las 11 caras de
  // 668 #24 (vencidos en color con el aspa 668 #17, el resto en silueta), la del siguiente en grande (crece de 0,7 a 1)
  // con "Challenger Nth" (668 #16) y la charla 669 (par: Mr. Satan, impar: el aviso) con sus voces (Voz); START sigue.
  // Con la escena 3D conectada la charla y las voces van en ella (Escena), justo despues. Tras perder se vuelve aqui con
  // Reintentar / Salir. ponytail: B1 no tiene pantalla de derrota localizada en el modo; posiciones de las caras sin
  // medir (la tabla de sprites del modo no esta leida): fila arriba, la grande en medio.
  void DrawTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, bool perdido) {
    if (!perdido && Img(kTablon) && Img(kTotal) && Img(kCifras)) return DrawResumenB1(dl, a, t, pressed, auto_on);
    const Paso& p = g_pasos[size_t(g_paso)];
    const int r = p.ronda, n = std::min(int(g_d->combates.size()), 11);
    const float e = t - cambio_at_;
    const ImU32 col = IM_COL32(255, 255, 255, int(255 * Ease(e / 0.33f)));   // fundidos de 20 cuadros
    if (const Tex* ch = Img(kChibis)) {
      for (int k = 0; k < n; ++k) {
        if (k == r) continue;   // la suya sale en grande (B1 la quita de la fila, FUN_00232660)
        const int i = k < r ? k : k + 10;   // 0-10 color; siluetas del 2.o al 11.o: 11-20
        const float sx = float((i % 7) * 72), sy = float((i / 7) * 72), x = 600 + (float(k) - 5.0f) * 92;
        Trozo(dl, a, *ch, sx, sy, sx + 72, sy + 72, x, 220, 1.1f, col);
        if (const Tex* cz = Img(kCruz); cz && k < r) PicAt(dl, a, *cz, x, 220, 64, col);
      }
      const float sx = float((r % 7) * 72), sy = float((r / 7) * 72);
      const float k = 2.0f * (0.7f + 0.3f * Ease((e - 0.33f) / 0.5f));
      Trozo(dl, a, *ch, sx, sy, sx + 72, sy + 72, 600, 375, k, col);
    }
    const float late = 0.975f + 0.025f * std::sin(e * 2.0f * 3.14159265f);   // el letrero late (FUN_00232a80)
    if (const Tex* rt = Img(kRetadores); rt && r < 11) {
      const float cx = float((r % 3) * 136), cy = float((r / 3) * 72);
      Trozo(dl, a, *rt, cx, cy, cx + 136, cy + 72, 600, 492, 1.4f * late, col);
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
  // la acaba de golpe). Devuelve true al acabar. ponytail: el tic (efecto 0xE cada 10 cuadros) y el del final (0xF)
  // son del banco de efectos de B1, sin localizar; las animaciones de los letreros (0x16, 0x1C) sin medir: 0,6 s.
  bool Cuenta(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed) {
    if (cuenta_de_ != g_cambio) {
      cuenta_de_ = g_cambio;
      cuenta_ya_ = false;
      cuenta_fin_ = -1;
      jingle_ = false;
    }
    const float e = t - cambio_at_, in = Ease(e / 0.35f);
    // jingle 1178 a los 30 cuadros (FUN_002319c0) o, en el final, 1180 a los 10 (FUN_002333d0)
    if (!jingle_ && e > (g_fase == kFin ? 0.167f : 0.5f) && !g_d->ruta.empty()) {
      jingle_ = true;
      OirAdxPs2(g_d->ruta, "adx_jp.afs", g_fase == kFin ? 1180 : 1178, Vol());
    }
    constexpr float kEmpieza = 0.5f + 0.6f + 0.6f + 0.5f;   // 30 cuadros, tablon, letrero, 30 cuadros (0x231b90)
    if (pressed & (kA | kStart)) cuenta_ya_ = true;
    const int ronda = std::max(0, g_ronda_pts);
    const int pasados = cuenta_ya_ ? ronda : std::clamp(int((e - kEmpieza) * 60.0f) * 33, 0, ronda);
    if (g_fase != kFin && Img(kAhora) && Img(kTotal) && Img(kCifras)) {
      // Resultado de B1 (captura de PCSX2, 640x480): negro; "Now!!" (668 #19) en (132, 47) al 96 % con los puntos del
      // combate (cifras al 93 %, derecha en 425, arriba en 95) y Mr. Satan con "Total" (668 #18) en (439, 79) al 102 %
      // con el total (cifras al 62 %, derecha en 610, arriba en 277). Los letreros entran deslizandose (0,35 s).
      FondoNegro(dl, a, false);
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
      if (!det.empty()) S::MenuText(dl, a.At(g_fase == kFin ? 600 : 430, g_fase == kFin ? 600 : 690), 26 * a.s, det.c_str(), true, false);
    }
    const bool hecho = pasados >= ronda && (cuenta_ya_ || e > kEmpieza);
    if (hecho && cuenta_fin_ < 0) cuenta_fin_ = t;
    return hecho;
  }

  // Resultado tras cada retador como B1 (SATAN RESULT MAIN, escena 0x1D, FUN_00231720-00231e60): jingle 1178,
  // la cuenta y, al acabarla, solo al tablero (0x1C) sin pulsar nada.
  // ponytail: el tablon 668 #15 sale vacio por dentro (su animacion 0x16 de B1 sin leer); se pone el "Challenger Nth"
  // (668 #16) del vencido, como hasta ahora.
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
  // tiene). ponytail: la contrasena de B1 no se pone. FUN_00235310 la hace con 2 numeros al azar, la dificultad, si el
  // total cuadra con la suma de los 11 y el total; 12 simbolos de la tabla 0x35F890 tras barajar con una de las 16
  // permutaciones de 0x350B70 (0x35F938...), en un recuadro con 669 #20 (FUN_00234d90). Sus simbolos son sprites del
  // modo sin localizar y la contrasena era para la web de Bandai, que ya no existe.
  void DrawFin(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on) {
    if (fin_de_ != g_cambio) {
      fin_de_ = g_cambio;
      fin_total_ = false;
    }
    if (!fin_total_) {
      const bool antes = cuenta_de_ == g_cambio && cuenta_fin_ >= 0;   // la pulsacion que acaba la cuenta no pasa
      if (!Cuenta(dl, a, t, antes ? 0 : pressed)) return;
      Ayuda(dl, a, {{"A", T::T("Seguir", "Continue")}});
      if (auto_on && t - cuenta_fin_ > 2.0f) pressed |= kA;
      if (antes && (pressed & (kA | kStart))) {
        fin_total_ = true;
        total_at_ = t;
      }
      return;
    }
    const ImU32 col = IM_COL32(255, 255, 255, int(255 * Ease((t - total_at_) / 0.33f)));
    if (const Tex* to = Img(kTotal)) {
      PicAt(dl, a, *to, 600, 400, 360, col);
      Numero(dl, a, g_puntos, 635, 498, 0.8f);   // a la izquierda de "Pts." del dibujo
    } else {
      char tot[64];
      std::snprintf(tot, sizeof(tot), T::T("Total: %d puntos", "Total: %d points"), g_puntos);
      S::MenuText(dl, a.At(600, 400), 44 * a.s, tot, true, false);
    }
    Ayuda(dl, a, {{"A", T::T("Volver al HUB", "Back to the HUB")}});
    if (auto_on && t - total_at_ > g_auto_seg) pressed |= kA;
    if (pressed & (kA | kStart)) Salir(true);
  }

  enum : int { kLogo = 100, kFondo, kSatan, kTablon, kRetadores, kCruz, kTotal, kAhora, kTienes, kCifras, kChibis, kLetrero,
               kPulsa, kDif, kBarra = kDif + 3, kTienesPeq, kCifrasPeq, kClave, kLetras, kEstilo = kLetras + 2,
               kCaraSatan, kConsejo, kCopy = kConsejo + 6 };
  std::map<int, Tex> tex_;
  uint32_t tex_gen_ = ~0u;
  const DatosB1* tex_src_ = nullptr;
  bool blocked_ = false, wait_release_ = true;
  uint64_t titulo_ = ~0ull;   // g_cambio en el que se pulso START en el titulo (despues, la dificultad)
  uint64_t elegida_ = ~0ull;  // g_cambio en el que se eligio la dificultad (despues, el prologo)
  uint64_t prologo_de_ = ~0ull, musica_de_ = ~0ull, cuenta_de_ = ~0ull, fin_de_ = ~0ull;
  int prologo_k_ = -1, prologo_voz_ = -1;
  float prologo_at_ = 0, prologo_t0_ = 0, lanzar_at_ = -10, cuenta_fin_ = -1, total_at_ = 0;
  bool musica_demo_ = false, cuenta_ya_ = false, fin_total_ = false, jingle_ = false;
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
