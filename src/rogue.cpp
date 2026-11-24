// dbz3 - Dragon Rogue (ROG-01): datos de los caminos y guardado propio. Diseno: studies 5/ENTREGAS/ROGUE_DISENO.md.
//
// Rogue es un reto de rol mas (modos::kRogue): la partida la lleva rpg.cpp con la maquinaria de siempre (selector,
// StartPropio, pantalla entre combates y final). Aqui solo vive lo que es suyo:
//   - el ajuste dbz3_rogue (ENCENDIDO de serie desde el 21/11, decision del usuario; con el apagado no hay entrada en el HUB
//     ni se escribe nada);
//   - los combates de un camino (ROG-01: solo El Abismo, rivales por la semilla);
//   - user_data/dbz3/rogue.toml (version 1; .tmp + rename, la anterior en .bak): la partida a medias y el record por
//     camino y luchador. hub_modos.toml y rpg.toml NO se tocan: la primera vez se copian a *.pre_rogue.toml.
#include <rex/cvar.h>
#include <rex/logging.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "launcher/settings.h"
#include "launcher/ui_kit.h"
#include "modos.h"
#include "rogue.h"
#include "rogue_fx.h"
#include "survivors/recursos.h"

REXCVAR_DEFINE_BOOL(dbz3_rogue, true, "DBZ3/Mods",
                    "Dragon Rogue (beta): entrada del HUB con 7 caminos, cartas, jefes, Kami y reto diario");

namespace dbz3::rogue {
namespace {
namespace M = dbz3::modos;

constexpr int kZeniPiso = 50;               // ROGUE_DISENO 1.6: Zenie por combate ganado

std::mutex g_mu;
bool g_leido = false, g_solo_lectura = false;
bool g_hay = false;
Partida g_partida;
// ROG-05: camino de la partida a medias (o el elegido en la pantalla 01 para la siguiente); g_cam = sus datos, leidos al
// cambiar de camino (una partida en curso no cambia si se edita rogue_caminos.toml con F5).
std::string g_camino = "abismo";
int g_dif = 0;   // PM-24: dificultad 0..2 de la partida (a medias o la siguiente)
Camino g_cam;
const Camino& Cam() {   // con g_mu
  if (g_cam.id != g_camino) g_cam = CaminoDe(g_camino);
  return g_cam;
}
std::map<std::pair<std::string, int>, int> g_record, g_intentos;   // por camino y luchador
std::string g_escrito;                     // lo ultimo leido o escrito: no se reescribe sin cambios
// ROG-02: cartas de la partida de semilla g_sem_cartas. g_elec = una entrada por piso superado (id de carta, "saltar" o
// "" si esa pausa se jugo antes de ROG-02); g_cambios = pisos en los que se gasto un cambio de las 3 cartas.
std::vector<std::string> g_elec;
std::vector<int> g_cambios;
uint32_t g_sem_cartas = 0;

void Sincronizar(uint32_t semilla) {   // con g_mu: otra semilla = otra partida, sin cartas
  if (semilla == g_sem_cartas) return;
  g_sem_cartas = semilla;
  g_elec.clear();
  g_cambios.clear();
}

uint32_t Hash(uint32_t a, uint32_t b) {   // la misma mezcla que rpg.cpp: lo mismo da lo mismo
  uint32_t h = a * 0x9E3779B1u ^ (b * 0x85EBCA6Bu + 0x7F4A7C15u);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}

std::filesystem::path Ruta() { return dbz3::settings::UserDataRoot() / "rogue.toml"; }

std::string Texto() {   // con g_mu
  std::ostringstream o;
  o << "# Dragon Rogue (src/rogue.cpp): partida a medias y records. No editar con el juego abierto.\nversion = 1\n";
  if (g_hay) {
    o << "\n[partida]\ncamino = \"" << g_camino << "\"\n" << (g_dif > 0 ? "dificultad = " + std::to_string(g_dif) + "\n" : std::string()) << "jugador = " << g_partida.jugador << "\ntraje = "
      << g_partida.traje << "\nsemilla = " << g_partida.semilla << "\nganados = " << g_partida.ganados << "\nvida = "
      << g_partida.vida << "\nvida_max = " << g_partida.vida_max << "\n";
    if (!g_elec.empty()) {   // ROG-02 (sin cartas el archivo es el de ROG-01)
      o << "cartas = [";
      for (size_t i = 0; i < g_elec.size(); ++i) o << (i ? ", " : "") << '"' << g_elec[i] << '"';
      o << "]\n";
    }
    if (!g_cambios.empty()) {
      o << "cambios = [";
      for (size_t i = 0; i < g_cambios.size(); ++i) o << (i ? ", " : "") << g_cambios[i];
      o << "]\n";
    }
  }
  for (const auto& [clave, mejor] : g_record) {
    const auto it = g_intentos.find(clave);
    o << "\n[[record]]\ncamino = \"" << clave.first << "\"\njugador = " << clave.second << "\nmejor = " << mejor
      << "\nintentos = " << (it == g_intentos.end() ? 0 : it->second) << "\n";
  }
  o << TextoExtra();   // ROG-06/07: [kami], [diario], [[dia]] (rogue_resultado.cpp)
  return o.str();
}

void Cargar() {   // con g_mu, una vez
  if (g_leido) return;
  g_leido = true;
  REXLOG_INFO("dbz3 rogue: {} caminos en rogue_caminos.toml", Caminos().size());   // ROG-05
  const auto p = Ruta();
  std::ifstream f(p, std::ios::binary);
  if (!f) return;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    const toml::table t = toml::parse(ss.str());
    if (t["version"].value_or(int64_t(0)) > 1) {   // de una version mas nueva: no se usa ni se pisa
      g_solo_lectura = true;
      REXLOG_WARN("dbz3 rogue: {} es de una version mas nueva: no se toca", p.string());
      return;
    }
    CargarExtra(ss.str());   // ROG-06/07
    if (const toml::table* x = t["partida"].as_table()) {
      auto i = [x](const char* k, int64_t d) { return (*x)[k].value_or(d); };
      if (const std::string cam = (*x)["camino"].value_or(std::string("abismo")); CaminoDe(cam).id == cam)
        g_camino = cam;   // ROG-05 (una partida de ROG-01/02/03 es de El Abismo)
      g_dif = std::clamp(int(i("dificultad", 0)), 0, 2);
      g_partida.jugador = int(i("jugador", 0));
      g_partida.traje = int(i("traje", 0));
      g_partida.semilla = uint32_t(i("semilla", 0));
      g_partida.ganados = std::clamp(int(i("ganados", 0)), 0, kPisos - 1);
      g_partida.vida_max = std::clamp(int(i("vida_max", 3000)), 1, 9999);
      g_partida.vida = std::clamp(int(i("vida", 3000)), 1, g_partida.vida_max);
      g_hay = true;
      g_sem_cartas = g_partida.semilla;   // ROG-02 (una partida de ROG-01 no trae estos campos: sin cartas)
      if (const toml::array* a = (*x)["cartas"].as_array()) {
        for (const toml::node& n : *a) {
          std::string id = n.value_or(std::string());
          const bool limpio = std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
          g_elec.push_back(limpio ? id : std::string());
        }
        if (int(g_elec.size()) > g_partida.ganados) g_elec.resize(size_t(g_partida.ganados));
      }
      if (const toml::array* a = (*x)["cambios"].as_array()) {
        for (const toml::node& n : *a) {
          if (const int piso = int(n.value_or(int64_t(0))); piso >= 1 && piso <= g_partida.ganados) g_cambios.push_back(piso);
        }
      }
    }
    if (const toml::array* a = t["record"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        const std::string cam = x ? (*x)["camino"].value_or(std::string()) : std::string();
        if (cam.empty() || !std::all_of(cam.begin(), cam.end(), [](char c) { return c >= 'a' && c <= 'z'; })) continue;
        const int id = int((*x)["jugador"].value_or(int64_t(-1)));
        if (id < 0) continue;
        g_record[{cam, id}] = std::clamp(int((*x)["mejor"].value_or(int64_t(0))), 0, kPisos);
        g_intentos[{cam, id}] = std::max(0, int((*x)["intentos"].value_or(int64_t(0))));
      }
    }
    g_escrito = Texto();
    REXLOG_INFO("dbz3 rogue: {}: {} records, partida {}", p.string(), g_record.size(), g_hay ? "a medias" : "ninguna");
  } catch (const toml::parse_error& e) {   // roto: se aparta y se empieza de cero
    std::error_code ec;
    std::filesystem::rename(p, p.string() + ".roto", ec);
    REXLOG_WARN("dbz3 rogue: {} no se puede leer ({}): se aparta a .roto", p.string(), e.description());
  }
}

void Guardar() {   // con g_mu
  if (g_solo_lectura) return;
  const std::string s = Texto();
  if (s == g_escrito) return;
  std::error_code ec;
  const auto p = Ruta();
  if (!std::filesystem::exists(p, ec)) {
    if (!g_hay && g_record.empty() && !HayExtra()) return;   // nada que guardar: el archivo no se crea (ROG-06: ni compras)
    // primera vez: respaldo de los guardados de los modos clasicos (Rogue no los modifica; por si acaso)
    for (const char* n : {"hub_modos", "rpg"}) {
      const auto o = dbz3::settings::UserDataRoot() / (std::string(n) + ".toml");
      const auto d = dbz3::settings::UserDataRoot() / (std::string(n) + ".pre_rogue.toml");
      if (std::filesystem::exists(o, ec) && !std::filesystem::exists(d, ec)) std::filesystem::copy_file(o, d, ec);
    }
  }
  std::filesystem::create_directories(p.parent_path(), ec);
  const std::filesystem::path tmp = p.string() + ".tmp", bak = p.string() + ".bak";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << s;
    if (!f.flush()) {
      REXLOG_WARN("dbz3 rogue: no se puede escribir {}", tmp.string());
      return;
    }
  }
  if (std::filesystem::exists(p, ec)) std::filesystem::copy_file(p, bak, std::filesystem::copy_options::overwrite_existing, ec);
  std::filesystem::rename(tmp, p, ec);
  if (ec) {
    REXLOG_WARN("dbz3 rogue: no se puede guardar {} ({})", p.string(), ec.message());
    return;
  }
  g_escrito = s;
}
}  // namespace

// PRUEBAS: DBZ3_TEST_ROGUE=N enciende el modo sin tocar los ajustes y deja el camino en N pisos (asi una partida de
// prueba llega a su final y se ve el record y el premio).
int PisosDePrueba() {
  static const int n = [] {
    const char* t = std::getenv("DBZ3_TEST_ROGUE");
    return t ? std::clamp(std::atoi(t), 1, kPisos) : 0;
  }();
  return n;
}

bool Activo() { return REXCVAR_GET(dbz3_rogue) || PisosDePrueba() > 0; }

// ---- ROG-03: jefe cada 5 combates (ROGUE_DISENO 1.5); ROG-05: cada "cada_jefe" del camino ----
namespace {
// Los tres jefes del Kumite de jefes (kJefes de rpg.cpp, mismas reglas ya probadas): armadura % con un hueco de 3 s
// cada `ventana` s, % de dano que recibe el jugador, aguantar `objetivo` s y escala de gigante (con tormenta).
struct Jefe {
  int id, vida, armadura, ventana, recibido, objetivo;
  float gigante;
};
constexpr Jefe kJefes[] = {{33, 5000, 50, 10, 150, 0, 0}, {40, 6000, 60, 8, 175, 0, 1.8f}, {36, 7000, 70, 12, 150, 45, 0}};

// Combates entre jefes del camino (0 = sin jefes). PRUEBAS: DBZ3_TEST_ROGUE_JEFE=N lo fuerza. Con g_mu.
int CadaJefe() {
  static const int n = [] {
    const char* t = std::getenv("DBZ3_TEST_ROGUE_JEFE");
    return t ? std::clamp(std::atoi(t), 1, kPisos) : 0;
  }();
  return n > 0 ? n : Cam().cada_jefe;
}
bool TrasJefe(int ganados) { return CadaJefe() > 0 && ganados > 0 && ganados % CadaJefe() == 0; }
int Total() {   // combates del camino (sin fin = kPisos; en pruebas, como mucho DBZ3_TEST_ROGUE). Con g_mu.
  const int n = Cam().combates > 0 ? Cam().combates : kPisos;
  return PisosDePrueba() > 0 ? std::min(n, PisosDePrueba()) : n;
}
// r23: combates hasta el proximo jefe (1 = el siguiente combate es jefe; 0 = ninguno antes del final). Con g_mu.
int ProximoJefeSL(int ganados) {
  const int c = CadaJefe();
  if (c <= 0) return 0;
  const int n = c - ganados % c;
  return ganados + n <= Total() ? n : 0;   // el jefe es el combate numero ganados + n
}
// Jefe del combate k (desde 0): false si es un combate normal. Rotan las reglas de los tres, empezando por la que diga la
// semilla (en pruebas, por la primera). El rival es el del jefe, o en el camino "jefes" el de su escalera; nunca el
// propio luchador. Con g_mu.
bool JefeDe(uint32_t semilla, int k, int jugador, Jefe* out) {
  if (CadaJefe() <= 0 || (k + 1) % CadaJefe() != 0) return false;
  const Camino& c = Cam();
  size_t i = (size_t(k / CadaJefe()) + (PisosDePrueba() > 0 || c.en_orden ? 0 : Hash(semilla, 400))) % std::size(kJefes);
  Jefe j = kJefes[i];
  if (c.rivales == "jefes" && !c.escalera.empty()) {
    for (size_t d = 0; d < c.escalera.size(); ++d) {
      j.id = c.escalera[(size_t(k) + d) % c.escalera.size()];
      if (j.id != jugador && M::EsJugable(j.id)) break;
    }
    j.vida = c.vida0 + c.vida_paso * k;
  } else {
    if (j.id == jugador) j = kJefes[(i + 1) % std::size(kJefes)];
    j.vida = std::min(j.vida + 500 * (k / CadaJefe()), 9000);
  }
  if (!M::EsJugable(j.id) || j.id == jugador) return false;
  if (out) *out = j;
  return true;
}
// r23 (ROG-05): regla de Torre de cada tramo de 5 combates (la de la maldicion que toque en el camino; el tramo 0 no lleva)
Maldicion ReglaTramo(int combate) {   // combate desde 0; con g_mu
  const Camino& c = Cam();
  const int t = combate / 5;
  if (t < 1 || c.reglas_tramo.empty() || c.maldicion == "semana") return {};
  return MaldicionDe(c.reglas_tramo[size_t(t - 1) % c.reglas_tramo.size()]);
}
std::vector<int> Escalera() {   // con g_mu: la del camino, o la de la torre de la semana
  return Cam().maldicion == "semana" ? MaldicionSemana().rivales : Cam().escalera;
}
}  // namespace

// Los combates del camino (ROG-05). Rivales: "semilla" = cualquier luchador jugable que no sea el jugador ni los de
// relleno 42/43; "escalera"/"jefes" = la lista del camino en orden (o la de la maldicion); "elo" = tres rivales por la
// semilla, el mas flojo en la semifinal y el mas fuerte (Elo del Ranking) en la final. La vida del rival sube por combate.
std::vector<M::Pelea> Peleas(uint32_t semilla, int jugador) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  const Camino& c = Cam();
  std::vector<int> pool;
  for (int id = 0; id < 42; ++id) {
    if (id != jugador && M::EsJugable(id)) pool.push_back(id);
  }
  std::vector<M::Pelea> v;
  if (pool.empty()) return v;
  const std::vector<int> esc = Escalera();
  const Maldicion mal = c.maldicion == "semana" ? MaldicionSemana() : Maldicion{};
  std::vector<int> cuadro;   // "elo"
  if (c.rivales == "elo") {
    std::vector<M::Puesto> top(64);
    const int nr = M::Ranking(top.data(), 64);
    auto elo = [&](int id) {
      for (int i = 0; i < nr; ++i)
        if (top[size_t(i)].id == id) return top[size_t(i)].elo;
      return 1000;
    };
    std::vector<int> p = pool;
    for (int k = 0; k < 3 && !p.empty(); ++k) {
      const size_t i = Hash(semilla, 600 + uint32_t(k)) % p.size();
      cuadro.push_back(p[i]);
      p.erase(p.begin() + std::ptrdiff_t(i));
    }
    std::stable_sort(cuadro.begin(), cuadro.end(), [&](int x, int y) { return elo(x) < elo(y); });
  }
  const int total = Total();
  for (int k = 0; k < total; ++k) {
    M::Pelea p;
    p.rival = pool[Hash(semilla, 200 + uint32_t(k)) % pool.size()];
    p.escenario = int(Hash(semilla, 300 + uint32_t(k)) % 8);
    if ((c.rivales == "escalera" || c.rivales == "jefes") && !esc.empty()) {
      for (size_t d = 0; d < esc.size(); ++d) {
        const int id = esc[(size_t(k) + d) % esc.size()];
        if (id != jugador && M::EsJugable(id)) {
          p.rival = id;
          break;
        }
      }
      p.escenario = k % 8;
    } else if (c.rivales == "elo" && !cuadro.empty()) {
      p.rival = k + 1 >= total ? cuadro.back() : cuadro[std::min(size_t(k), cuadro.size() - 1)];
    }
    p.vida = std::min(c.vida0 + c.vida_paso * k, 9000);
    if (c.maldicion == "semana") {
      p.tiempo = mal.tiempo;
      p.regen = mal.regen;
    } else {   // las oleadas de Supervivencia: una de cada 3 a tiempo, otra con rival que se regenera
      if (c.tiempo > 0 && k % 3 == 2) p.tiempo = c.tiempo;
      if (c.regen > 0 && k % 3 == 0 && k > 0) p.regen = c.regen;
    }
    if (const Maldicion m = ReglaTramo(k); m.regen > 0 && p.regen == 0) p.regen = m.regen;   // r23: regla de Torre del tramo
    if (Jefe j; JefeDe(semilla, k, jugador, &j)) {   // ROG-03
      p.rival = j.id;
      p.vida = j.vida;
      p.objetivo = j.objetivo;
      p.gigante = j.gigante;
      p.clima = j.gigante > 0;
      p.tiempo = p.regen = 0;
    }
    p.vida = std::min(p.vida + 500 * g_dif, 9999);   // PM-24: +500 por nivel de dificultad
    v.push_back(p);
  }
  return v;
}

bool LeerPartida(Partida* p) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  if (g_hay && p) *p = g_partida;
  return g_hay;
}

void GuardarPartida(const Partida* p) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  g_hay = p != nullptr;
  if (p) g_partida = *p;
  // ROG-02: las cartas son de esta partida; nunca hay mas elecciones que pisos superados
  Sincronizar(p ? p->semilla : 0);
  if (!p) {
    g_elec.clear();
    g_cambios.clear();
  } else {
    if (int(g_elec.size()) > p->ganados) g_elec.resize(size_t(std::max(p->ganados, 0)));
    std::erase_if(g_cambios, [p](int piso) { return piso > p->ganados; });
  }
  Guardar();
}

int Record(int jugador) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  const auto it = g_record.find({g_camino, jugador});
  return it == g_record.end() ? 0 : it->second;
}

// ROG-05: camino de la partida siguiente (pantalla 01). Con una partida a medias no cambia: es la que se sigue.
void SetCamino(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  if (g_hay || CaminoDe(id).id != id) return;
  g_camino = id;
  REXLOG_INFO("dbz3 rogue: camino {} ({} combates, jefe cada {}, rivales {}, pausa {}{})", id, Cam().combates, Cam().cada_jefe,
              Cam().rivales, Cam().pausa, Cam().maldicion == "semana" ? ", maldicion " + MaldicionSemana().id : std::string());
}
void SetDificultad(int d) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  if (g_hay) return;
  g_dif = std::clamp(d, 0, 2);
  REXLOG_INFO("dbz3 rogue: dificultad {}", g_dif);
}
int Dificultad() {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  return g_dif;
}
std::string CaminoActual() {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  return g_camino;
}
int RecordCamino(const std::string& camino, int jugador) {   // camino "" = cualquiera; jugador -1 = cualquiera
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  int m = 0;
  for (const auto& [clave, mejor] : g_record)
    if ((camino.empty() || clave.first == camino) && (jugador < 0 || clave.second == jugador)) m = std::max(m, mejor);
  return m;
}

DatosFin DatosDeFin(int jugador, int ganados);   // ROG-06 (abajo, junto a las cartas)
int Fin(int jugador, int ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  DatosFin d = DatosDeFin(jugador, ganados);   // ROG-06: jefes de verdad y sellos, antes de vaciar las cartas
  int& mejor = g_record[{g_camino, jugador}];   // ROG-05: record por camino
  d.record_antes = mejor;
  const bool nuevo = ganados > mejor;
  mejor = std::max(mejor, ganados);
  ++g_intentos[{g_camino, jugador}];
  g_hay = false;
  g_elec.clear();   // ROG-02
  g_cambios.clear();
  // ROG-06: Zenie = ganados x 50 + jefes x 300 (rogue_resultado.cpp); ROG-05: + premio del camino si se completa
  const int zeni = (ZeniFin(ganados, d.jefes, false) + (d.completo ? Cam().premio : 0)) * (100 + 50 * g_dif) / 100;   // PM-24: +50 % por nivel
  d.zeni = zeni;
  AlTerminar(d);   // pantalla 05, hitos y reto diario (rogue_resultado.cpp)
  Guardar();
  REXLOG_INFO("dbz3 rogue: fin de {}: id {}, {} pisos (record {}{}), {} Zenie", g_camino, jugador, ganados, mejor,
              nuevo ? ", nuevo" : "", zeni);
  return zeni;
}

// ---- ROG-02: la pausa "elige 1 de 3" (ROGUE_DISENO 1.1-1.3; maqueta ROGUE_VISUAL/02_elegir_mejora.png) ----
namespace {
namespace S = dbz3::ingame::style;
namespace R = dbz3::survivors;   // cargador de res/mejoras (biblioteca comun; hilo de dibujo)

constexpr uint16_t kLeft = 0x0004, kRight = 0x0008, kA = 0x1000, kX = 0x4000, kY = 0x8000;   // como rpg.cpp
constexpr int kTopeHecho = 300, kTopeRecibido = 40, kTopeCaps = 7, kTopeVida = 9000;
constexpr const char* kCats[] = {"ki", "cuerpo", "apoyo", "evolucion"};                    // categoria = sello
constexpr const char* kIconoCat[] = {"ki_15", "fisica_01", "utilidad_03", "evolucion_01"};   // icono del sello
constexpr const char* kEfectos[] = {"dano_hecho", "dano_recibido", "ki", "vida_max", "cura", "recupera", "cambio", "capsula",
                                    "forma", "mitad", "fusion", "entrenar", "explorar", "descansar"};   // ROG-04/05
// ROG-05: las 3 de la pausa de Simulacion (no son mejoras: ni sello ni "Llevas") y ROG-04: las de evolucion (solo tras un jefe)
bool EsDia(const std::string& e) { return e == "entrenar" || e == "explorar" || e == "descansar"; }
bool EsEvolucion(const std::string& e) { return e == "forma" || e == "mitad" || e == "fusion"; }

struct Carta {
  std::string id, efecto, nombre[5], texto[5];   // es, en, fr, de, it
  int cat = 0, estrellas = 1, max = 1, valor = 0;
};
std::vector<Carta> g_baraja;
bool g_baraja_leida = false;

const char* Idioma(const std::string* s) { return M::L(s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str(), s[4].c_str()); }
const char* NombreCat(int c) {
  switch (c) {
    case 0: return "Ki";
    case 1: return M::L("Cuerpo", "Body", "Corps", "Körper", "Corpo");
    case 2: return M::L("Apoyo", "Support", "Soutien", "Hilfe", "Supporto");
    default: return M::L("Evolución", "Evolution", "Évolution", "Evolution", "Evoluzione");
  }
}

std::string Leer(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// res/mejoras/mejoras.toml (id, categoria, nombre) + rogue_cartas.toml (efectos). Con g_mu, una vez. Sin los archivos
// la baraja queda vacia y no hay eleccion (la pantalla entre combates es la de ROG-01).
void CargarBaraja() {
  if (g_baraja_leida) return;
  g_baraja_leida = true;
  const std::filesystem::path dir = R::RutaMejoras();
  try {
    std::map<std::string, std::pair<int, std::string>> lib;   // id -> categoria, nombre es
    const toml::table tl = toml::parse(Leer(dir / "mejoras.toml"));
    if (const toml::array* a = tl["mejora"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        const std::string cat = (*x)["categoria"].value_or(std::string());
        const auto it = std::find(std::begin(kCats), std::end(kCats), cat);
        if (it != std::end(kCats)) lib[(*x)["id"].value_or(std::string())] = {int(it - std::begin(kCats)), (*x)["es"].value_or(std::string())};
      }
    }
    const toml::table tc = toml::parse(Leer(dir / "rogue_cartas.toml"));
    if (const toml::array* a = tc["carta"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        Carta c;
        c.id = (*x)["id"].value_or(std::string());
        c.efecto = (*x)["efecto"].value_or(std::string());
        const auto it = lib.find(c.id);
        const bool repe = std::any_of(g_baraja.begin(), g_baraja.end(), [&c](const Carta& o) { return o.id == c.id; });
        if (it == lib.end() || repe || std::find(std::begin(kEfectos), std::end(kEfectos), c.efecto) == std::end(kEfectos)) {
          REXLOG_WARN("dbz3 rogue: carta '{}' ignorada (id repetido o que no esta en mejoras.toml, o efecto '{}' desconocido)", c.id, c.efecto);
          continue;
        }
        c.cat = it->second.first;
        c.estrellas = std::clamp(int((*x)["estrellas"].value_or(int64_t(1))), 1, 3);
        c.max = std::clamp(int((*x)["max"].value_or(int64_t(1))), 1, 99);
        c.valor = int((*x)["valor"].value_or(int64_t(0)));
        static constexpr const char* kLang[] = {"es", "en", "fr", "de", "it"};
        for (int i = 0; i < 5; ++i) {
          c.nombre[i] = (*x)[kLang[i]].value_or(std::string());
          c.texto[i] = (*x)[std::string("t_") + kLang[i]].value_or(std::string());
        }
        if (c.nombre[0].empty()) c.nombre[0] = it->second.second;
        for (int i = 1; i < 5; ++i) {   // idioma sin texto: el ingles y, sin el, el espanol
          if (c.nombre[i].empty()) c.nombre[i] = i > 1 && !c.nombre[1].empty() ? c.nombre[1] : c.nombre[0];
          if (c.texto[i].empty()) c.texto[i] = i > 1 && !c.texto[1].empty() ? c.texto[1] : c.texto[0];
        }
        g_baraja.push_back(std::move(c));
      }
    }
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("dbz3 rogue: baraja de {} ilegible ({})", dir.string(), e.description());
  }
  if (g_baraja.empty()) REXLOG_WARN("dbz3 rogue: sin baraja en {}: no hay eleccion de mejoras", dir.string());
  else REXLOG_INFO("dbz3 rogue: baraja de {} cartas", g_baraja.size());
  if (std::getenv("DBZ3_TEST_ROGUE_RAREZA") && !g_baraja.empty()) PruebaRareza();   // ROG-06 (abajo)
}

int Indice(const std::string& id) {
  for (size_t k = 0; k < g_baraja.size(); ++k) {
    if (g_baraja[k].id == id) return int(k);
  }
  return -1;
}

struct Cuenta {
  std::string ini;   // PM-24: carta inicial de la ventaja de Kami ("" = ninguna)
  std::vector<int> n;   // veces elegida cada carta de la baraja
  int cat[4] = {};      // cartas por categoria (sellos)
  int caps = 0;
  int paso = 0;               // ROG-04: transformacion de las cartas "forma" (la mas alta: 1 activa)
  bool mitad = false, fusion = false;
  int fuerza = 0, fatiga = 0;   // ROG-05 Simulacion: +15 % por entrenar; fatiga = +10 % recibido (descansar la quita)
  std::vector<int> caps_dia;    // ... capsulas encontradas al explorar
};
// ROG-05 Simulacion: lo que da explorar el dia `piso` (la tirada de rpg.cpp SimDe: 0 Potential 366, 1 +500 de vida
// maxima, 2 emboscada -500, 3 Spirit!! 382).
int Tirada(int piso) { return int(Hash(g_sem_cartas, 500 + uint32_t(piso)) % 4); }
Cuenta Contar() {   // con g_mu
  Cuenta c;
  c.n.assign(g_baraja.size(), 0);
  if (CartaInicialActiva(g_sem_cartas)) {   // PM-24: ventaja de Kami: 1 carta comun (1 estrella, de dano/ki) elegida por la semilla
    std::vector<size_t> cand;
    for (size_t k = 0; k < g_baraja.size(); ++k) {
      const Carta& ca = g_baraja[k];
      if (ca.estrellas == 1 && (ca.efecto == "dano_hecho" || ca.efecto == "dano_recibido" || ca.efecto == "ki") && !CartaBloqueada(ca.id)) cand.push_back(k);
    }
    if (!cand.empty()) {
      const size_t k = cand[Hash(g_sem_cartas, 900) % cand.size()];
      ++c.n[k];
      ++c.cat[g_baraja[k].cat];
      c.ini = g_baraja[k].id;
    }
  }
  for (const std::string& id : g_elec) {
    const int k = Indice(id);
    if (k < 0) continue;   // "saltar", "" o una carta que ya no esta en la baraja
    ++c.n[size_t(k)];
    const std::string& e = g_baraja[size_t(k)].efecto;
    if (!EsDia(e)) ++c.cat[g_baraja[size_t(k)].cat];
    if (e == "capsula") ++c.caps;
    if (e == "forma") c.paso = std::max(c.paso, g_baraja[size_t(k)].valor);
    c.mitad |= e == "mitad";
    c.fusion |= e == "fusion";
    if (e == "entrenar") c.fuerza += 15, ++c.fatiga;
    if (e == "descansar") c.fatiga = 0;
    const int piso = int(&id - g_elec.data()) + 1;
    if (e == "explorar" && (Tirada(piso) == 0 || Tirada(piso) == 3)) c.caps_dia.push_back(Tirada(piso) == 0 ? 366 : 382);
  }
  return c;
}
int Suma(const Cuenta& c, const char* efecto) {
  int s = 0;
  for (size_t k = 0; k < g_baraja.size(); ++k) {
    if (g_baraja[k].efecto == efecto) s += g_baraja[k].valor * c.n[k];
  }
  return s;
}

// Sinergias (1.3), solo con lo que dan las Reglas: Ki x2 = el ki sube un 1 %/s, Ki x4 = +30 % de dano (de todo el dano:
// las Reglas no separan el de energia); Cuerpo x2 = +10 % de dano (idem, no solo cuerpo a cuerpo), Cuerpo x4 = el hueco sin
// armadura de los jefes pasa de 3 a 5 s (ROG-02, Reglas::hueco); Apoyo x2 = +1 cambio, Apoyo x4 = +300 de vida al ganar.
M::Reglas ReglasDe(const Cuenta& c) {
  M::Reglas g;
  int nc = 0;
  for (size_t k = 0; k < g_baraja.size() && nc < kTopeCaps; ++k) {
    if (c.n[k] > 0 && g_baraja[k].efecto == "capsula") g.caps[nc++] = g_baraja[k].valor;
  }
  for (size_t i = 0; i < c.caps_dia.size() && nc < kTopeCaps; ++i) g.caps[nc++] = c.caps_dia[i];   // ROG-05 Simulacion
  if (nc > 0) g.ncaps = nc;
  const int hecho = 100 + Suma(c, "dano_hecho") + c.fuerza + (c.cat[0] >= 4 ? 30 : 0) + (c.cat[1] >= 2 ? 10 : 0);
  g.dano_hecho = std::clamp(hecho, 10, kTopeHecho);
  g.dano_recibido = std::clamp(100 + Suma(c, "dano_recibido") + 10 * c.fatiga, kTopeRecibido, 300);
  g.ki_drena = -(Suma(c, "ki") + (c.cat[0] >= 2 ? 1 : 0));
  return g;
}
int Cambios(const Cuenta& c) { return std::max(0, 1 + CambiosExtra() + Suma(c, "cambio") + (c.cat[2] >= 2 ? 1 : 0) - int(g_cambios.size())); }

// Hasta 3 cartas que aun no estan al maximo, por la semilla y el piso (lo mismo da lo mismo: no cambia al reabrir).
// Cada cambio gastado en ese piso pasa a las 3 siguientes de la misma mezcla. En pruebas (DBZ3_TEST_ROGUE) la semilla
// de la oferta es fija, para que el escenario sepa que cartas salen.
int g_rareza = 0;   // ROG-06 PruebaRareza: 1 = semilla real con peso, 2 = semilla real sin peso (0 = lo de siempre)
std::vector<int> Oferta(uint32_t semilla, int piso, const Cuenta& c) {
  std::vector<int> v;
  if (Cam().pausa == "simulacion") {   // ROG-05: entrenar, explorar, descansar (en el orden de la baraja)
    for (size_t k = 0; k < g_baraja.size(); ++k)
      if (EsDia(g_baraja[k].efecto)) v.push_back(int(k));
    return v;
  }
  // ROG-04: evoluciones solo tras un jefe y solo las que el luchador tiene en el juego; 1 activa (la fusion las cierra)
  const int jug = g_partida.jugador;
  const bool fusionable = !c.fusion && g_partida.traje == 0 && FichaFusion(jug, nullptr) >= 0;
  auto evolucion_vale = [&](const Carta& ca) {
    if (!TrasJefe(piso)) return false;
    if (ca.efecto == "forma") return !c.fusion && ca.valor > c.paso && ca.valor <= Transformaciones(jug);
    if (ca.efecto == "mitad") return fusionable && !c.mitad;
    return fusionable && c.mitad;   // "fusion"
  };
  for (size_t k = 0; k < g_baraja.size(); ++k) {
    const Carta& ca = g_baraja[k];
    if (EsDia(ca.efecto) || (EsEvolucion(ca.efecto) && !evolucion_vale(ca))) continue;
    if (c.n[k] < ca.max && (ca.efecto != "capsula" || c.caps < kTopeCaps) &&
        !CartaBloqueada(ca.id))   // ROG-06: cartas de Kami (precio o hito) aun sin abrir
      v.push_back(int(k));
  }
  if (TrasJefe(piso)) {   // ROG-03: tras un jefe, solo raras y legendarias (si quedan 3)
    std::vector<int> raras;
    std::copy_if(v.begin(), v.end(), std::back_inserter(raras), [](int k) { return g_baraja[size_t(k)].estrellas >= 2; });
    if (raras.size() >= 3) v = raras;
  }
  const uint32_t s = (PisosDePrueba() > 0 && g_rareza == 0 ? 1u : semilla) + uint32_t(piso);
  if ((PisosDePrueba() == 0 && g_rareza == 0) || g_rareza == 1) {   // POST/ROG 12/11: la rareza pesa (1 estrella x3, 2 x2, 3+ x1); en pruebas no, para no mover las ofertas
    std::vector<int> w;
    for (int k : v)
      for (int r = std::max(1, 4 - g_baraja[size_t(k)].estrellas); r > 0; --r) w.push_back(k);
    v.swap(w);
  }
  for (int i = int(v.size()) - 1; i > 0; --i) std::swap(v[size_t(i)], v[Hash(s, uint32_t(i)) % uint32_t(i + 1)]);
  {   // quitar repetidas dejando la primera aparicion
    std::vector<int> u;
    for (int k : v)
      if (std::find(u.begin(), u.end(), k) == u.end()) u.push_back(k);
    v.swap(u);
  }
  const int n = int(v.size()), var = int(std::count(g_cambios.begin(), g_cambios.end(), piso));
  std::vector<int> o;
  for (int k = 0; k < std::min(3, n); ++k) o.push_back(v[size_t((3 * var + k) % n)]);
  return o;
}

std::string Num(int n) { return std::to_string(n); }
std::string Bono(int cat, int n) {   // lo que enciende la carta numero n de esa categoria
  if (cat == 0 && n == 2) return M::L("el ki sube solo un 1 %/s", "ki rises 1%/s by itself", "le ki monte seul de 1 %/s", "Ki steigt von selbst um 1 %/s", "il ki sale da solo dell'1%/s");
  if (cat == 0 && n == 4) return M::L("+30 % de daño", "+30% damage", "+30 % de dégâts", "+30 % Schaden", "+30% di danni");
  if (cat == 1 && n == 2) return M::L("+10 % de daño", "+10% damage", "+10 % de dégâts", "+10 % Schaden", "+10% di danni");
  if (cat == 1 && n == 4) return M::L("los jefes abren su armadura 5 s", "bosses open their armor for 5 s", "les boss ouvrent leur armure 5 s", "Bosse öffnen ihre Rüstung 5 s", "i boss aprono l'armatura per 5 s");
  if (cat == 2 && n == 2) return M::L("+1 cambio de cartas", "+1 card reroll", "+1 changement de cartes", "+1 Kartentausch", "+1 cambio di carte");
  if (cat == 2 && n == 4) return M::L("+300 de vida al ganar", "+300 health per win", "+300 de vie par victoire", "+300 Leben pro Sieg", "+300 di vita a vittoria");
  return {};
}
std::string Avance(const Carta& ca, const Cuenta& c) {   // "Con Rage: Apoyo 2/2, +1 cambio de cartas"
  if (ca.cat > 2 || EsDia(ca.efecto)) return {};
  const int n = c.cat[ca.cat] + 1, tope = n <= 2 ? 2 : 4;
  std::string s = std::string(M::L("Con ", "With ", "Avec ", "Mit ", "Con ")) + Idioma(ca.nombre) + ": " + NombreCat(ca.cat) + " " +
                  Num(std::min(n, tope)) + "/" + Num(tope);
  if (const std::string b = Bono(ca.cat, n); !b.empty()) s += ", " + b;
  return s;
}

// k = carta de la baraja o -1 = saltar (+10 % de vida). Con g_mu.
void Elegir(int k, int piso, int* vida, int* vida_max) {
  g_elec.resize(size_t(piso - 1));   // pausas jugadas antes de ROG-02: quedan vacias
  if (k < 0) {
    g_elec.push_back("saltar");
    *vida = std::min(*vida_max, *vida + *vida_max * (10 + SaltoExtra()) / 100);
    REXLOG_INFO("dbz3 rogue: piso {}: saltar (vida {}/{})", piso, *vida, *vida_max);
    return;
  }
  const Carta& ca = g_baraja[size_t(k)];
  g_elec.push_back(ca.id);
  if (ca.efecto == "vida_max") *vida_max = std::min(kTopeVida, *vida_max + ca.valor);
  if (ca.efecto == "vida_max" || ca.efecto == "cura") *vida = std::min(*vida_max, *vida + ca.valor);
  // ROG-05 Simulacion (cifras de rpg.cpp SimDe): entrenar -300 de vida; descansar +1500; explorar segun la tirada
  if (ca.efecto == "entrenar") *vida = std::max(1, *vida - 300);
  if (ca.efecto == "descansar") *vida = std::min(*vida_max, *vida + 1500);
  if (ca.efecto == "explorar") {
    const int t = Tirada(piso);
    if (t == 1) *vida_max = std::min(kTopeVida, *vida_max + 500), *vida = std::min(*vida_max, *vida + 500);
    if (t == 2) *vida = std::max(1, *vida - 500);
    REXLOG_INFO("dbz3 rogue: piso {}: explorar: {}", piso, t == 0 ? "capsula 366" : t == 1 ? "+500 de vida maxima" : t == 2 ? "emboscada -500" : "capsula 382");
  }
  const Cuenta c = Contar();
  const M::Reglas g = ReglasDe(c);
  REXLOG_INFO("dbz3 rogue: piso {}: carta {} ({} de {}); reglas: dano {}%, recibido {}%, ki {}/s, {} capsulas; sellos ki {} "
              "cuerpo {} apoyo {}; vida {}/{}",
              piso, ca.id, c.n[size_t(k)], ca.max, g.dano_hecho, g.dano_recibido, g.ki_drena, std::max(g.ncaps, 0), c.cat[0],
              c.cat[1], c.cat[2], *vida, *vida_max);
  if (EsEvolucion(ca.efecto)) {   // ROG-04
    const Evolucion e = EvolucionDe(g_partida.jugador, c.paso, c.fusion);
    REXLOG_INFO("dbz3 rogue: piso {}: evolucion: luchador {} forma {} ({} capsulas: {})", piso, e.id >= 0 ? e.id : g_partida.jugador,
                e.forma, e.caps.size(), e.caps.empty() ? 0 : e.caps.back());
  }
}

// ---- dibujo (px de la maqueta 1280x720 -> area 4:3 de 1200x900 centrada en la imagen 16:9) ----
ImFont* Fuente() {   // la de las filas de los menus (S::MenuTextB3)
  const auto& f = dbz3::launcher::ui::GetFonts();
  return f.menu ? f.menu : ImGui::GetFont();
}
float Ancho(float size, const char* s) { return Fuente()->CalcTextSizeA(size, FLT_MAX, 0, s).x; }
// Texto blanco perfilado (oro = amarillo) desde el borde izquierdo `p` (centro vertical).
void Izq(ImDrawList* dl, ImVec2 p, float size, const char* s, bool oro, float max_w = 0) {
  float w = Ancho(size, s);
  if (max_w > 0 && w > max_w) {
    size *= max_w / w;
    w = max_w;
  }
  S::MenuTextB3(dl, ImVec2(p.x + w * 0.5f, p.y), size, s, oro, false);
}
std::vector<std::string> Partir(const std::string& s, float size, float max_w) {   // lineas que caben en max_w
  std::vector<std::string> v;
  std::istringstream in(s);
  std::string w, l;
  while (in >> w) {
    const std::string c = l.empty() ? w : l + " " + w;
    if (!l.empty() && Ancho(size, c.c_str()) > max_w) {
      v.push_back(l);
      l = w;
    } else {
      l = c;
    }
  }
  if (!l.empty()) v.push_back(l);
  return v;
}

void Pintar(ImDrawList* dl, const S::Area& a, const std::vector<int>& of, int cursor, const Cuenta& c, int cambios, int piso,
            int vida, int vida_max) {
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;   // px de pantalla por px de la maqueta
  // velo: tapa el fondo y el anfitrion de la pantalla entre combates (los pone modos.cpp antes de llegar aqui)
  const S::Imagen im = S::GuestImage(ImGui::GetIO());
  S::Backdrop(dl, a, S::Img("hub/fondo_retos") ? "hub/fondo_retos" : "hub/fondo");   // r23: tapa al anfitrion; el velo deja ver el fondo
  dl->AddRectFilled(im.p0, im.p1, IM_COL32(6, 20, 30, 200));
  const bool dia = Cam().pausa == "simulacion";   // ROG-05
  S::TitleText(dl, a, 600, 52, 760, dia ? M::L("¿Qué haces hoy?", "What will you do today?", "Que fais-tu aujourd'hui ?", "Was machst du heute?", "Cosa fai oggi?")
                                        : M::L("Elige una mejora", "Choose an upgrade", "Choisis une amélioration", "Wähle eine Verbesserung", "Scegli un potenziamento"), 0.88f);
  std::string sub = std::string(M::L("Piso ", "Floor ", "Étage ", "Ebene ", "Piano ")) + Num(piso) +
                    M::L(" superado", " cleared", " terminé", " geschafft", " superato") + "   -   " +
                    M::L("Vida ", "Health ", "Vie ", "Leben ", "Vita ") + Num(vida) + " / " + Num(vida_max);
  if (const int pj = ProximoJefeSL(piso); pj == 1)   // r23: "Proximo jefe en N combates" de la maqueta 02
    sub += std::string("   -   ") + M::L("El próximo combate es un jefe", "The next fight is a boss", "Le prochain combat est un boss", "Der nächste Kampf ist ein Boss", "Il prossimo incontro è un boss");
  else if (pj > 1)
    sub += std::string("   -   ") + M::L("Próximo jefe en ", "Next boss in ", "Prochain boss dans ", "Nächster Boss in ", "Prossimo boss tra ") + Num(pj) +
           M::L(" combates", " fights", " combats", " Kämpfen", " incontri");
  S::MenuTextB3(dl, P(640, 98), 26 * u, sub.c_str(), false, false, 900 * u);

  const int n = int(of.size());
  for (int i = 0; i < n; ++i) {
    const Carta& ca = g_baraja[size_t(of[size_t(i)])];
    const bool sel = i == cursor;
    // tarjeta de 400x560 px a k px de maqueta por px del PNG; la elegida, algo mas grande. Zonas: metadatos_ui.json
    // (icono 108,54 184x184; texto 36,286 328x238)
    const float k = sel ? 0.775f : 0.725f, x0 = 640 + (float(i) - float(n - 1) * 0.5f) * 360 - 200 * k, y0 = 336 - 280 * k;
    auto Q = [&](float px, float py) { return P(x0 + px * k, y0 + py * k); };
    const float z = k * u;   // px de pantalla por px del PNG
    if (const ps2::Tex* tj = R::Ui(sel ? "ui_tarjeta_resaltada" : "ui_tarjeta_normal")) {
      R::DibujarImagen(dl, tj, Q(0, 0), Q(400, 560));
    } else {   // sin el arte: marco nativo
      S::Frame(dl, Q(8, 8), Q(392, 552), a.s);
      if (sel) dl->AddRect(Q(4, 4), Q(396, 556), IM_COL32(255, 200, 24, 255), 0, 0, 3 * a.s);
    }
    R::DibujarAjustada(dl, R::Icono(ca.id.c_str()), Q(108, 54), Q(292, 238));
    S::MenuTextB3(dl, Q(200, 284), 40 * z, Idioma(ca.nombre), true, false, 320 * z);
    char est[20];
    std::snprintf(est, sizeof est, "ui_estrella_%d", ca.estrellas);
    R::DibujarAjustada(dl, R::Ui(est), Q(172, 304), Q(228, 360));
    std::string txt = Idioma(ca.texto);
    if (ca.max > 1 && ca.max < 9) txt += " (" + Num(c.n[size_t(of[size_t(i)])] + 1) + M::L(" de ", " of ", " sur ", " von ", " di ") + Num(ca.max) + ")";
    float escala = 1.0f;
    auto lineas = Partir(txt, 29 * z, 312 * z);
    while (lineas.size() * 37.0f * escala > 100.0f && escala > 0.40f) {
      escala -= 0.05f;
      lineas = Partir(txt, 29 * z * escala, 312 * z);
    }
    float y = 388;
    for (const std::string& l : lineas) {
      S::MenuTextB3(dl, Q(200, y), 29 * z * escala, l.c_str(), false, false, 320 * z);
      y += 37 * escala;
    }
    if (EsDia(ca.efecto)) continue;   // ROG-05: los dias de Simulacion no tienen sello
    R::DibujarAjustada(dl, R::Icono(kIconoCat[ca.cat]), Q(42, 492), Q(76, 526));
    Izq(dl, Q(92, 509), 27 * z, NombreCat(ca.cat), false);
  }

  // abajo: lo que llevas (las ultimas 12, en abanico si no caben), capsulas y sellos con su contador
  dl->AddRectFilled(P(130, 566), P(1150, 658), IM_COL32(8, 24, 36, 220), 10 * u);
  Izq(dl, P(155, 588), 21 * u, M::L("Llevas", "You have", "Tu as", "Du hast", "Hai"), true);
  std::vector<const std::string*> llevas;
  for (const std::string& id : g_elec) {
    if (Indice(id) >= 0 && !EsDia(g_baraja[size_t(Indice(id))].efecto)) llevas.push_back(&id);
  }
  if (llevas.size() > 12) llevas.erase(llevas.begin(), llevas.end() - 12);
  const float paso = llevas.size() > 1 ? std::min(46.0f, 110.0f / float(llevas.size() - 1)) : 0;
  for (size_t i = 0; i < llevas.size(); ++i) {
    const float x = 148 + paso * float(i);
    R::DibujarAjustada(dl, R::Icono(llevas[i]->c_str()), P(x, 604), P(x + 40, 644));
  }
  Izq(dl, P(310, 629), 19 * u, (Num(c.caps) + " / " + Num(kTopeCaps) + " " + M::L("cápsulas", "capsules", "capsules", "Kapseln", "capsule")).c_str(), false, 140 * u);
  Izq(dl, P(476, 588), 21 * u, M::L("Sellos", "Seals", "Sceaux", "Siegel", "Sigilli"), true);
  for (int k = 0; k < 3; ++k) {
    const float sx = 470 + 170.0f * float(k);
    const int m = c.cat[k];
    R::DibujarAjustada(dl, R::Icono(kIconoCat[k]), P(sx, 608), P(sx + 28, 636));
    Izq(dl, P(sx + 40, 622), 20 * u, (std::string(NombreCat(k)) + "  " + Num(std::min(m, 4)) + "/" + Num(m < 2 ? 2 : 4)).c_str(), m >= 2, 122 * u);
  }
  if (const std::string av = Avance(g_baraja[size_t(of[size_t(cursor)])], c); !av.empty()) {   // lo que daria la carta resaltada
    float size = 18.5f * u, w = Ancho(size, av.c_str());
    if (w > 540 * u) {
      size *= 540 * u / w;
      w = 540 * u;
    }
    const ImVec2 p = P(1138, 579), o(p.x - w, p.y);
    dl->AddText(Fuente(), size, ImVec2(o.x + 1.5f * a.s, o.y + 1.5f * a.s), IM_COL32(0, 0, 0, 220), av.c_str());
    dl->AddText(Fuente(), size, o, IM_COL32(124, 232, 140, 255), av.c_str());
  }
  const std::string cam = std::string(M::L("Cambiar las 3", "Change all 3", "Changer les 3", "Alle 3 tauschen", "Cambia le 3")) + " (" + Num(cambios) + ")";
  if (dia) {   // ROG-05: Simulacion sin cambiar ni saltar
    S::mode::Help(dl, a, {{"A", M::L("Elegir", "Choose", "Choisir", "Wählen", "Scegli")},
                          {"B", M::L("Guardar y salir", "Save and exit", "Sauver et quitter", "Speichern und raus", "Salva ed esci")}});
    return;
  }
  S::mode::Help(dl, a, {{"A", M::L("Elegir", "Choose", "Choisir", "Wählen", "Scegli")},
                        {"X", cam.c_str()},
                        {"Y", SaltoExtra() > 0 ? M::L("Saltar: +20 % vida", "Skip: +20% health", "Passer : +20 % de vie", "Auslassen: +20 % Leben", "Salta: +20% vita")
                                  : M::L("Saltar: +10 % vida", "Skip: +10% health", "Passer : +10 % de vie", "Auslassen: +10 % Leben", "Salta: +10% vita")},
                        {"B", M::L("Guardar y salir", "Save and exit", "Sauver et quitter", "Speichern und raus", "Salva ed esci")}});
}
}  // namespace

// ---- ROG-06/07: datos para rogue_resultado.cpp ----
DatosFin DatosDeFin(int jugador, int ganados) {   // con g_mu, antes de vaciar la partida
  CargarBaraja();
  DatosFin d;
  d.jugador = jugador;
  d.ganados = ganados;
  d.semilla = g_hay ? g_partida.semilla : 0;
  d.camino = g_camino;
  d.dif = g_dif;
  d.completo = ganados >= Total();   // ROG-05: los combates del camino
  d.vida = d.completo && g_hay ? g_partida.vida : 0;   // al caer, 0; al completar, la que quedaba antes del ultimo
  for (int k = 0; k < ganados; ++k) d.jefes += JefeDe(d.semilla, k, jugador, nullptr) ? 1 : 0;
  if (g_hay && g_sem_cartas == d.semilla) {
    const Cuenta c = Contar();
    for (int k = 0; k < 4; ++k) d.sellos[k] = c.cat[k];
  }
  return d;
}

std::vector<CartaVista> VerCartas(const uint32_t* semilla) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  CargarBaraja();
  if (semilla && *semilla != g_sem_cartas) return {};   // otra partida: no se toca la lista
  const Cuenta c = Contar();
  std::vector<CartaVista> v;
  for (size_t k = 0; k < g_baraja.size(); ++k) {
    if (semilla && c.n[k] == 0) continue;
    const Carta& ca = g_baraja[k];
    v.push_back({ca.id, Idioma(ca.nombre), Idioma(ca.texto), ca.cat, ca.estrellas, ca.max, semilla ? c.n[k] : 0});
  }
  return v;
}

void GuardarYa() {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  Guardar();
}

// DBZ3_TEST_ROGUE_RAREZA (con g_mu, desde CargarBaraja): 3000 ofertas (semillas 1-1000, pisos 1-4 sin jefe) con el peso
// de la rareza y sin el; la carta comun debe salir mas y la legendaria menos. Dice PASS o FALLO en el registro.
void PruebaRareza() {
  Cuenta c;
  c.n.assign(g_baraja.size(), 0);
  int n[3][4] = {};   // [modo][estrellas]
  for (int modo = 1; modo <= 2; ++modo) {
    g_rareza = modo;
    for (uint32_t s = 1; s <= 1000; ++s) {
      for (int piso = 1, hechas = 0; hechas < 3; ++piso) {
        if (TrasJefe(piso)) continue;
        for (int k : Oferta(s * 7919u, piso, c)) ++n[modo][g_baraja[size_t(k)].estrellas];
        ++hechas;
      }
    }
  }
  g_rareza = 0;
  auto pct = [&n](int modo, int e) { return 100.0 * n[modo][e] / std::max(1, n[modo][1] + n[modo][2] + n[modo][3]); };
  const bool ok = pct(1, 1) > pct(2, 1) && pct(1, 3) < pct(2, 3);
  REXLOG_INFO("dbz3 rogue: prueba rareza: con peso 1* {:.1f} % 2* {:.1f} % 3* {:.1f} %; sin peso 1* {:.1f} % 2* {:.1f} % 3* {:.1f} %: {}",
              pct(1, 1), pct(1, 2), pct(1, 3), pct(2, 1), pct(2, 2), pct(2, 3), ok ? "PASS" : "FALLO");
}

// % de dano que recibe el jugador en el combate k: las cartas, +10 % por tramo superado (hasta +50) y el del jefe.
int Recibido(int cartas, int k, const Jefe* j) {
  const int r = cartas + std::min(10 * (k / 5), 50) + 20 * g_dif;   // PM-24: +20 % por nivel de dificultad   // ROG-05: tramos de 5 combates en todos los caminos
  return j ? r * j->recibido / 100 : r;
}

M::Reglas ReglasPartida(uint32_t semilla, int jugador, int ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  CargarBaraja();
  Sincronizar(semilla);
  const Cuenta c = Contar();
  static uint32_t ultima_ini = 0;   // se pide en cada cuadro: avisa una vez por partida
  if (ganados == 0 && !c.ini.empty() && ultima_ini != semilla) {
    ultima_ini = semilla;
    REXLOG_INFO("dbz3 rogue: ventaja de Kami: carta inicial {}", c.ini);
  }
  M::Reglas g = ReglasDe(c);
  // ROG-04 (rogue_evoluciones.cpp): forma inicial y las capsulas que exige, delante de las de las cartas (tope 7)
  const Evolucion ev = EvolucionDe(jugador, c.paso, c.fusion);
  g.forma1 = ev.forma;
  if (!ev.caps.empty()) {
    std::vector<int> caps(ev.caps.begin(), ev.caps.end());
    for (int i = 0; i < std::max(g.ncaps, 0); ++i) caps.push_back(g.caps[i]);
    g.ncaps = std::min(int(caps.size()), kTopeCaps);
    for (int i = 0; i < g.ncaps; ++i) g.caps[i] = caps[size_t(i)];
  }
  if (Cam().maldicion == "semana") {   // ROG-05: la maldicion de la semana en todos los combates
    const Maldicion m = MaldicionSemana();
    g.ki_drena += m.ki;
    g.dano_recibido = g.dano_recibido * m.recibido / 100;
    g.armadura = std::max(g.armadura, m.armadura);
  }
  if (const Maldicion m = ReglaTramo(ganados); !m.id.empty()) {   // r23 (ROG-05): una regla de Torre por tramo
    g.ki_drena += m.ki;
    g.dano_recibido = g.dano_recibido * m.recibido / 100;
    g.armadura = std::max(g.armadura, m.armadura);
  }
  Jefe jefe;
  const Jefe* j = JefeDe(semilla, ganados, jugador, &jefe) ? &jefe : nullptr;   // ROG-03
  g.dano_recibido = Recibido(g.dano_recibido, ganados, j);
  if (j) {
    g.armadura = j->armadura;
    g.ventana = j->ventana;
    g.hueco = c.cat[1] >= 4 ? 5 : 3;   // ROG-02: sinergia Cuerpo x4 = el jefe abre su armadura 2 s mas
  }
  return g;
}

int ProximoJefe(int ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  return ProximoJefeSL(ganados);
}

std::string TextoHud(int ganados) {   // "Piso 4 - jefe en 1" (HUD de rpg.cpp)
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  const int n = ProximoJefeSL(ganados);
  std::string s = std::string(M::L("Piso ", "Floor ", "Étage ", "Ebene ", "Piano ")) + Num(ganados + 1);
  if (n == 1) s += std::string("  -  ") + M::L("jefe ahora", "boss now", "boss maintenant", "Boss jetzt", "boss ora");
  else if (n > 1) s += std::string("  -  ") + M::L("jefe en ", "boss in ", "boss dans ", "Boss in ", "boss tra ") + Num(n);
  if (const Maldicion m = ReglaTramo(ganados); !m.id.empty()) {   // lo de despues de "Torre X: "
    std::string r = M::L(m.nombre[0].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str());
    if (const size_t i = r.find(": "); i != std::string::npos) r = r.substr(i + 2);
    s += "  -  " + r;
  }
  return s;
}

int VidaAlGanar(uint32_t semilla, int ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  CargarBaraja();
  Sincronizar(semilla);
  // ROG-05: lo que diga el camino. rpg.cpp ya suma 300 al ganar: aqui va la diferencia (vida llena = toda; arrastra =
  // su cura; tras un jefe, su cura_jefe, de serie toda: ROG-03)
  if (Cam().vida == "llena") return kTopeVida;
  if (TrasJefe(ganados)) return Cam().cura_jefe - 300;
  const Cuenta c = Contar();
  return Cam().cura - 300 + Suma(c, "recupera") + (c.cat[2] >= 4 ? 300 : 0);
}

int Eleccion(ImDrawList* dl, const S::Area& a, uint16_t pulsado, uint32_t semilla, int ganados, int* vida, int* vida_max) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  CargarBaraja();
  Sincronizar(semilla);
  if (const Cuenta e = Contar(); e.fusion) {   // ROG-04: con la carta Fusion, el luchador del combate siguiente es el fusionado
    if (const int id = FichaFusion(g_partida.jugador, nullptr); id >= 0) M::SetJugador(id);
  }
  if (ganados <= 0 || int(g_elec.size()) >= ganados) return 0;   // este piso ya tiene su eleccion
  Cuenta c = Contar();
  std::vector<int> of = Oferta(semilla, ganados, c);
  if (of.empty()) return 0;   // sin baraja (o toda al maximo)
  static int cursor = 1, piso_cursor = -1;   // hilo de dibujo; cada piso empieza en la carta del centro
  if (piso_cursor != ganados) {
    piso_cursor = ganados;
    cursor = 1;
  }
  const int n = int(of.size());
  cursor = std::clamp(cursor, 0, n - 1);
  if (pulsado & kLeft) cursor = (cursor + n - 1) % n;
  if (pulsado & kRight) cursor = (cursor + 1) % n;
  if (Cam().pausa == "simulacion") pulsado &= uint16_t(~(kX | kY));   // ROG-05: sin cambiar ni saltar
  if (pulsado & (kA | kY)) {
    Elegir((pulsado & kA) ? of[size_t(cursor)] : -1, ganados, vida, vida_max);
    return 2;
  }
  int r = 1;
  if ((pulsado & kX) && Cambios(c) > 0) {   // otras 3 (queda guardado: al reabrir sale la misma oferta nueva)
    g_cambios.push_back(ganados);
    of = Oferta(semilla, ganados, c);
    cursor = std::clamp(cursor, 0, int(of.size()) - 1);
    REXLOG_INFO("dbz3 rogue: piso {}: cambio de cartas (quedan {})", ganados, Cambios(c));
    r = 2;
  }
  Pintar(dl, a, of, cursor, c, Cambios(c), ganados, *vida, *vida_max);
  fx::Pintar(dl, a, g_baraja[size_t(of[size_t(cursor)])].cat == 3 ? fx::Tipo::Evolucion : fx::Tipo::Premio);
  return r;
}

// ROG-03: aviso antes de un jefe (maqueta ROGUE_VISUAL/04_aviso_jefe.png). Solo dibuja; A y B los atiende rpg.cpp.
bool AvisoJefe(ImDrawList* dl, const S::Area& a, uint32_t semilla, int jugador, int ganados) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  CargarBaraja();
  Sincronizar(semilla);
  Jefe jefe;
  if (!JefeDe(semilla, ganados, jugador, &jefe)) return false;
  const Jefe* j = &jefe;
  const int rec = Recibido(ReglasDe(Contar()).dano_recibido, ganados, j);
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };   // px de la maqueta 1280x720
  const float u = 1.25f * a.s;
  const S::Imagen im = S::GuestImage(ImGui::GetIO());
  dl->AddRectFilled(im.p0, im.p1, IM_COL32(10, 8, 26, 252));   // velo casi opaco: no deja ver al anfitrion (POST/ROG 12/11)
  S::TitleText(dl, a, 600, 90, 520, M::L("¡JEFE!", "BOSS!", "BOSS !", "BOSS!", "BOSS!"), 1.6f);   // POST/ROG: x1,2
  const std::string sub = std::string(Idioma(Cam().nombre)) + "   -   " +   // ROG-05: el camino
                          M::L("Piso ", "Floor ", "Étage ", "Ebene ", "Piano ") + Num(ganados + 1);
  S::MenuTextB3(dl, P(640, 147), 26 * u, sub.c_str(), false, false, 700 * u);

  // retrato redondo: la cara del luchador del juego (native_ui caras/<id>) dentro del aro del arte
  const ImVec2 c0 = P(262 - 104, 320 - 104), c1 = P(262 + 104, 320 + 104);
  dl->AddCircleFilled(P(262, 320), 104 * u, IM_COL32(20, 44, 66, 255), 48);
  if (const S::Pic cara = S::Img(("caras/" + Num(j->id)).c_str())) dl->AddImageRounded(cara.tex, c0, c1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 104 * u);
  R::DibujarImagen(dl, R::Ui("ui_retrato_redondo"), P(262 - 112, 320 - 112), P(262 + 112, 320 + 112));
  S::MenuTextB3(dl, P(262, 470), 52 * u, M::Nombre(j->id).c_str(), true, false, 300 * u);
  if (j->gigante > 0) {
    char g[48];
    std::snprintf(g, sizeof g, "%s  x%.1f", M::L("Gigante", "Giant", "Géant", "Riese", "Gigante"), double(j->gigante));
    S::MenuTextB3(dl, P(262, 511), 22 * u, g, false, false, 300 * u);
  }

  // sus reglas
  if (!R::DibujarNueve(dl, "ui_panel_resultados", P(476, 190), P(1166, 516), 0.42f * u)) S::Frame(dl, P(480, 195), P(1162, 512), a.s);
  Izq(dl, P(506, 228), 30 * u, M::L("Sus reglas", "Its rules", "Ses règles", "Seine Regeln", "Le sue regole"), true);
  dl->AddLine(P(496, 252), P(1146, 252), IM_COL32(232, 140, 32, 255), 1.5f * u);
  const std::string armadura = std::string(M::L("Absorbe el ", "Absorbs ", "Absorbe ", "Schluckt ", "Assorbe il ")) + Num(j->armadura) +
                               M::L(" % de tu daño", "% of your damage", " % de tes dégâts", " % deines Schadens", "% dei tuoi danni");
  const bool x4 = Contar().cat[1] >= 4;   // PM-25: con la sinergia Cuerpo x4 el hueco es de 5 s (ReglasPartida)
  const std::string hueco = std::string(x4 ? M::L("Se abre 5 s cada ", "Opens 5 s every ", "S'ouvre 5 s toutes les ", "Öffnet sich 5 s alle ", "Si apre 5 s ogni ")
                                           : M::L("Se abre 3 s cada ", "Opens 3 s every ", "S'ouvre 3 s toutes les ", "Öffnet sich 3 s alle ", "Si apre 3 s ogni ")) +
                            Num(j->ventana) + " s";
  const std::string golpes = rec >= 100 ? std::string(M::L("Recibes un ", "You take ", "Tu subis ", "Du erleidest ", "Subisci il ")) + Num(rec - 100) +
                                              M::L(" % más de daño", "% more damage", " % de dégâts en plus", " % mehr Schaden", "% di danni in più")
                                        : std::string(M::L("Recibes el ", "You take ", "Tu subis ", "Du erleidest ", "Subisci il ")) + Num(rec) +
                                              M::L(" % del daño", "% of the damage", " % des dégâts", " % des Schadens", "% dei danni");
  const std::string ultima = j->objetivo ? std::string(M::L("Aguanta ", "Survive ", "Tiens ", "Überlebe ", "Resisti ")) + Num(j->objetivo) + " s" : Num(j->vida);
  const std::pair<const char*, const char*> filas[] = {
      {M::L("Armadura", "Armor", "Armure", "Panzerung", "Armatura"), armadura.c_str()},
      {M::L("Punto débil", "Weak point", "Point faible", "Schwachpunkt", "Punto debole"), hueco.c_str()},
      {M::L("Golpes", "Hits", "Coups", "Schläge", "Colpi"), golpes.c_str()},
      {j->objetivo ? M::L("Objetivo", "Goal", "Objectif", "Ziel", "Obiettivo") : M::L("Vida", "Health", "Vie", "Leben", "Vita"), ultima.c_str()}};
  float y = 266;
  for (const auto& [nombre, valor] : filas) {
    dl->AddRectFilled(P(500, y), P(1140, y + 48), IM_COL32(34, 98, 138, 235), 9 * u);
    dl->AddRect(P(500, y), P(1140, y + 48), IM_COL32(90, 160, 200, 200), 9 * u, 0, 1.2f * u);
    Izq(dl, P(530, y + 24), 26 * u, nombre, false);
    float size = 26 * u, w = Ancho(size, valor);
    if (w > 420 * u) {
      size *= 420 * u / w;
      w = 420 * u;
    }
    S::MenuTextB3(dl, ImVec2(P(1108, 0).x - w * 0.5f, P(0, y + 24).y), size, valor, false, false);
    y += 56;
  }

  // si ganas
  dl->AddRectFilled(P(480, 545), P(1170, 628), IM_COL32(8, 24, 36, 200), 8 * u);
  Izq(dl, P(497, 563), 20 * u, M::L("Si ganas", "If you win", "Si tu gagnes", "Wenn du gewinnst", "Se vinci"), true);
  R::DibujarAjustada(dl, R::Icono("evolucion_01"), P(492, 576), P(534, 618));
  Izq(dl, P(552, 600), 19 * u,
      (std::string(M::L("Mejora rara, legendaria o evolución  -  ", "Rare, legendary or evolution upgrade  -  ",
                        "Amélioration rare, légendaire ou évolution  -  ", "Seltene, legendäre oder Evolutions-Verbesserung  -  ",
                        "Potenziamento raro, leggendario o evoluzione  -  ")) +   // ROG-04/05: la cura de jefe del camino
       (Cam().vida == "llena" || Cam().cura_jefe >= kTopeVida
            ? std::string(M::L("vida al completo", "full health", "vie au maximum", "volles Leben", "vita al massimo"))
            : "+" + Num(Cam().cura_jefe) + M::L(" de vida", " health", " de vie", " Leben", " di vita")) +
       "  -  +" + Num(ZeniFin(0, 1, false)) + " Zenie")   // r23 ROG-05: Zenie extra por jefe (ZeniFin: 300)
          .c_str(),
      false, 600 * u);
  S::mode::Help(dl, a, {{"A", M::L("Luchar", "Fight", "Combattre", "Kämpfen", "Combatti")},
                        {"Y", M::L("Ver mis mejoras", "See my upgrades", "Voir mes améliorations", "Meine Verbesserungen", "Vedi potenziamenti")},   // ROG-06 (rpg.cpp)
                        {"B", M::L("Guardar y salir", "Save and exit", "Sauver et quitter", "Speichern und raus", "Salva ed esci")}});
  fx::Pintar(dl, a, fx::Tipo::Jefe);
  return true;
}

}  // namespace dbz3::rogue
