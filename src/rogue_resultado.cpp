// dbz3 - Dragon Rogue ROG-06/ROG-07 (ROGUE_DISENO 1.6 y 1.7; maquetas ROGUE_VISUAL/05, 06 y 07).
//   - Zenie del final (ZeniFin) y pantalla 05 "Fin del camino" (Resultado), con desglose, record y lo nuevo.
//   - Pantalla 06 "Entrenamiento de Kami": caminos, cartas y ventajas que se compran con los Zenie del monedero.
//   - Pantalla 07 "Reto diario": semilla y luchador del dia, un intento puntuable al dia, 30 dias, racha y codigo.
//   - "Ver mis mejoras": las cartas que lleva la partida (desde el aviso de jefe y la pantalla entre combates).
// Lo nuevo se guarda en rogue.toml ([kami], [diario], [[dia]]) por los enganches de rogue.cpp (TextoExtra /
// CargarExtra). Candados: rogue.cpp llama aqui con el suyo puesto (aqui solo g_mx); desde aqui nunca se llama a
// rogue.cpp con g_mx puesto.
#include <rex/logging.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "launcher/i18n.h"
#include "launcher/ui_kit.h"
#include "rogue.h"
#include "rogue_fx.h"
#include "ranking_online.h"   // RANK-ONLINE
#include "survivors/recursos.h"

namespace dbz3::rogue {

// 1.6: Zenie = combates ganados x 50 + jefes x 300 + premio del camino (El Abismo, 800) si se completa.
int ZeniFin(int ganados, int jefes, bool completo) {
  return ganados * 50 + jefes * 300 + (completo ? 800 : 0);
}

// 1.7: dia UTC (dias desde 1970). PRUEBAS: DBZ3_TEST_DIA=N fuerza el dia.
int DiaUtc() {
  if (const char* t = std::getenv("DBZ3_TEST_DIA")) return std::atoi(t);
  return int(std::time(nullptr) / 86400);
}

// Semilla del reto diario: la misma para todos ese dia (camino, luchador, rivales y ofertas salen de ella).
uint32_t SemillaDiaria(int dia) {
  uint32_t x = uint32_t(dia) * 2654435761u ^ 0x524F4755u;   // "ROGU"
  x ^= x >> 16;
  x *= 0x45D9F3Bu;
  return x ^ (x >> 16);
}

// 1.7: puntuacion = ganados x 100 + jefes x 500 + vida final / 10 - segundos totales (nunca negativa).
int PuntosDiario(int ganados, int jefes, int vida, int segundos) {
  const int p = ganados * 100 + jefes * 500 + vida / 10 - segundos;
  return p > 0 ? p : 0;
}

// Luchador del dia: el mismo para todos (de los jugables 0-41, como los rivales de El Abismo).
int JugadorDiario(int dia) {
  std::vector<int> pool;
  for (int id = 0; id < 42; ++id) {
    if (dbz3::modos::EsJugable(id)) pool.push_back(id);
  }
  return pool.empty() ? 0 : pool[SemillaDiaria(dia) % pool.size()];
}

namespace {
namespace M = dbz3::modos;
namespace S = dbz3::ingame::style;
namespace D = dbz3::ingame::style::mode;
namespace R = dbz3::survivors;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;   // como rpg.cpp
constexpr int kPrecioCamino = 2000;
// Ventajas de salida (1.6). Las que el motor de hoy sabe aplicar: +1 cambio y vida inicial (no en el reto diario).
struct Ventaja {
  const char* id;
  int precio, niveles;
  const char* icono;
};
constexpr Ventaja kVentajas[] = {{"cambio", 1500, 1, "kami_baston"}, {"vida", 1200, 3, "kami_semilla"}, {"salto", 1000, 1, "kami_agua"},
                                 {"carta", 2000, 1, "fisica_09"}};

struct Dia {
  int dia = 0, jugador = 0, puntos = 0, ganados = 0, jefes = 0, vida = 0, segundos = 0;
};
struct Meta {   // de rogue_cartas.toml: lo que hace falta para desbloquear una carta (0 = abierta de serie)
  int precio = 0, hito_jefes = 0;
};

std::mutex g_mx;
std::vector<std::string> g_compras;   // "cambio", "vida" (una por nivel), "carta:<id>", "camino:<id>"
int g_jefes_total = 0;                // jefes vencidos en todas las partidas (hitos)
struct {
  int intento = 0;       // dia del ultimo intento puntuable empezado
  uint32_t semilla = 0;  // partida del dia en curso (0 = ninguna)
  int64_t inicio = 0;    // segundos UTC al empezarla
  bool puntuable = false;
} g_dia;
std::vector<Dia> g_dias;   // ultimos 30 dias jugados (puntuables)
std::map<std::string, Meta> g_meta;
bool g_meta_leida = false;
bool g_pend_diario = false;   // el siguiente PartidaNueva es la del dia

// Ultima partida terminada (pantalla 05)
struct Ultimo {
  bool hay = false;
  DatosFin d;
  std::vector<std::string> nuevas;   // cartas que abrio un hito en esta partida
  int puntos = -1;                   // reto diario puntuable: su puntuacion
} g_ult;

int64_t Ahora() { return int64_t(std::time(nullptr)); }
int Veces(const std::string& c) { return int(std::count(g_compras.begin(), g_compras.end(), c)); }   // con g_mx

void LeerMeta() {   // con g_mx, una vez
  if (g_meta_leida) return;
  g_meta_leida = true;
  try {
    std::ifstream f(R::RutaMejoras() / "rogue_cartas.toml", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const toml::table t = toml::parse(ss.str());
    if (const toml::array* a = t["carta"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        Meta m{int((*x)["precio"].value_or(int64_t(0))), int((*x)["hito_jefes"].value_or(int64_t(0)))};
        if (m.precio > 0 || m.hito_jefes > 0) g_meta[(*x)["id"].value_or(std::string())] = m;
      }
    }
  } catch (const toml::parse_error&) {   // rogue.cpp ya avisa de una baraja ilegible
  }
}
bool Abierta(const std::string& id) {   // con g_mx (y LeerMeta)
  const auto it = g_meta.find(id);
  if (it == g_meta.end()) return true;
  return Veces("carta:" + id) > 0 || (it->second.hito_jefes > 0 && g_jefes_total >= it->second.hito_jefes);
}
// PRUEBAS: con DBZ3_TEST_ROGUE las cartas que se desbloquean nunca entran en la oferta (las ofertas de los guiones son
// fijas), salvo con DBZ3_TEST_ROGUE_KAMI=1 (los guiones de Kami).
bool KamiEnPruebas() {
  static const bool k = std::getenv("DBZ3_TEST_ROGUE_KAMI") != nullptr;
  return k;
}
}  // namespace

// ---- enganches de rogue.cpp (con su candado puesto) ----
bool CartaBloqueada(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_mx);
  LeerMeta();
  if (!g_meta.count(id)) return false;
  if (PisosDePrueba() > 0 && !KamiEnPruebas()) return true;
  return !Abierta(id);
}

int SaltoExtra() {   // ventaja de Kami: saltar la carta cura +10 % mas
  std::lock_guard<std::mutex> lk(g_mx);
  return Veces("salto") > 0 ? 10 : 0;
}

bool CartaInicialActiva(uint32_t semilla) {   // PM-24: no vale en el reto diario (misma semilla que la partida del dia)
  std::lock_guard<std::mutex> lk(g_mx);
  if (semilla == 0 || (g_dia.semilla != 0 && g_dia.semilla == semilla)) return false;
  return Veces("carta") > 0;
}

bool DificultadAbierta(const std::string& camino, int d) {   // PM-24: d = 1 (II) o 2 (III)
  if (d <= 0) return true;
  if (PisosDePrueba() > 0) return true;
  std::lock_guard<std::mutex> lk(g_mx);
  return Veces("dif:" + camino + ":" + std::to_string(d)) > 0;
}

int CambiosExtra() {
  std::lock_guard<std::mutex> lk(g_mx);
  return std::min(Veces("cambio"), 1);
}

bool HayExtra() {
  std::lock_guard<std::mutex> lk(g_mx);
  return !g_compras.empty() || g_jefes_total > 0 || g_dia.intento > 0 || !g_dias.empty();
}

std::string TextoExtra() {
  std::lock_guard<std::mutex> lk(g_mx);
  std::ostringstream o;
  if (!g_compras.empty() || g_jefes_total > 0) {
    o << "\n[kami]\ncompras = [";
    for (size_t i = 0; i < g_compras.size(); ++i) o << (i ? ", " : "") << '"' << g_compras[i] << '"';
    o << "]\njefes = " << g_jefes_total << "\n";
  }
  if (g_dia.intento > 0) {
    o << "\n[diario]\nintento = " << g_dia.intento << "\nsemilla = " << g_dia.semilla << "\ninicio = " << g_dia.inicio
      << "\npuntuable = " << (g_dia.puntuable ? "true" : "false") << "\n";
  }
  for (const Dia& d : g_dias) {
    o << "\n[[dia]]\ndia = " << d.dia << "\njugador = " << d.jugador << "\ncamino = \"abismo\"\npuntos = " << d.puntos
      << "\nganados = " << d.ganados << "\njefes = " << d.jefes << "\nvida = " << d.vida << "\nsegundos = " << d.segundos
      << "\n";
  }
  return o.str();
}

void CargarExtra(const std::string& texto) {
  std::lock_guard<std::mutex> lk(g_mx);
  try {
    const toml::table t = toml::parse(texto);
    auto limpio = [](const std::string& s) {
      return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':'; });
    };
    if (const toml::table* k = t["kami"].as_table()) {
      if (const toml::array* a = (*k)["compras"].as_array()) {
        for (const toml::node& n : *a) {
          if (std::string s = n.value_or(std::string()); limpio(s)) g_compras.push_back(s);
        }
      }
      g_jefes_total = std::max(0, int((*k)["jefes"].value_or(int64_t(0))));
    }
    if (const toml::table* d = t["diario"].as_table()) {
      g_dia.intento = int((*d)["intento"].value_or(int64_t(0)));
      g_dia.semilla = uint32_t((*d)["semilla"].value_or(int64_t(0)));
      g_dia.inicio = (*d)["inicio"].value_or(int64_t(0));
      g_dia.puntuable = (*d)["puntuable"].value_or(false);
    }
    if (const toml::array* a = t["dia"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        auto i = [x](const char* c) { return std::max(0, int((*x)[c].value_or(int64_t(0)))); };
        g_dias.push_back({i("dia"), i("jugador"), i("puntos"), i("ganados"), i("jefes"), i("vida"), i("segundos")});
      }
    }
  } catch (const toml::parse_error&) {   // rogue.cpp ya lo trata (lo aparta a .roto)
  }
}

void AlTerminar(const DatosFin& d) {
  std::lock_guard<std::mutex> lk(g_mx);
  LeerMeta();
  g_ult = {};
  g_ult.hay = true;
  g_ult.d = d;
  // hitos: cartas que se abren al llegar a N jefes vencidos en total
  const int antes = g_jefes_total;
  g_jefes_total += d.jefes;
  for (const auto& [id, m] : g_meta) {
    if (m.hito_jefes > 0 && antes < m.hito_jefes && g_jefes_total >= m.hito_jefes && Veces("carta:" + id) == 0) g_ult.nuevas.push_back(id);
  }
  // PM-24: vencer el piso 10 (o completar el camino) abre el nivel siguiente de dificultad en ese camino
  if (d.dif < 2 && (d.ganados >= 10 || d.completo)) {
    const std::string k = "dif:" + d.camino + ":" + std::to_string(d.dif + 1);
    if (Veces(k) == 0) g_compras.push_back(k);
  }
  // reto diario: solo la partida del dia, y solo el primer intento
  if (g_dia.semilla != 0 && d.semilla == g_dia.semilla) {
    if (g_dia.puntuable) {
      const int seg = int(std::clamp<int64_t>(Ahora() - g_dia.inicio, 0, 99999));
      Dia x{g_dia.intento, d.jugador, PuntosDiario(d.ganados, d.jefes, d.vida, seg), d.ganados, d.jefes, d.vida, seg};
      std::erase_if(g_dias, [&x](const Dia& o) { return o.dia == x.dia; });
      g_dias.push_back(x);
      std::sort(g_dias.begin(), g_dias.end(), [](const Dia& a, const Dia& b) { return a.dia > b.dia; });
      if (g_dias.size() > 30) g_dias.resize(30);
      g_ult.puntos = x.puntos;
      REXLOG_INFO("dbz3 rogue: reto diario {}: {} puntos ({} pisos, {} jefes, vida {}, {} s)", x.dia, x.puntos, x.ganados,
                  x.jefes, x.vida, x.segundos);
    } else {
      REXLOG_INFO("dbz3 rogue: reto diario {}: practica (no puntua)", g_dia.intento);
    }
    g_dia.semilla = 0;
    g_dia.puntuable = false;
  }
  std::string nuevas;
  for (const std::string& s : g_ult.nuevas) nuevas += " " + s;
  REXLOG_INFO("dbz3 rogue: resultado: id {}, {} pisos, {} jefes, record antes {}, {} Zenie, sellos ki {} cuerpo {} apoyo {}; "
              "jefes en total {}; nuevo:{}",
              d.jugador, d.ganados, d.jefes, d.record_antes, d.zeni, d.sellos[0], d.sellos[1], d.sellos[2], g_jefes_total,
              nuevas.empty() ? " nada" : nuevas);
}

// ---- rpg.cpp ----
void PartidaNueva(uint32_t* semilla, int* jugador, int* vida, int* vida_max) {
  const int hoy = DiaUtc();
  const int jd = JugadorDiario(hoy);
  if (g_pend_diario) SetDificultad(0), SetCamino("abismo");   // el del dia, para todos (fuera de g_mx: SetCamino toma el candado de rogue.cpp)
  std::lock_guard<std::mutex> lk(g_mx);
  if (g_pend_diario) {   // la del dia: misma semilla y luchador para todos, sin ventajas
    g_pend_diario = false;
    *semilla = SemillaDiaria(hoy);
    *jugador = jd;
    g_dia.puntuable = g_dia.intento != hoy;
    if (g_dia.puntuable) {
      g_dia.intento = hoy;
      g_dia.inicio = Ahora();
    }
    g_dia.semilla = *semilla;
    REXLOG_INFO("dbz3 rogue: reto diario {}: id {}, semilla {} ({})", hoy, jd, *semilla, g_dia.puntuable ? "puntua" : "practica");
    return;
  }
  const int extra = 500 * std::min(Veces("vida"), 3);
  if (extra > 0) {
    *vida_max = std::min(*vida_max + extra, 9000);
    *vida = std::min(*vida + extra, *vida_max);
    REXLOG_INFO("dbz3 rogue: ventaja de Kami: vida inicial {}/{}", *vida, *vida_max);
  }
}

bool CaminoComprado(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_mx);
  return Veces("camino:" + id) > 0;
}

// ---- dibujo (px de la maqueta 1280x720 -> area 4:3 de 1200x900 centrada en la imagen 16:9, como rogue.cpp) ----
namespace {
enum Vista { kNinguna, kKami, kDiario, kMejoras };
Vista g_vista = kNinguna;   // hilo de dibujo
int g_tab = 2, g_fila = 0;
std::string g_aviso;
struct {
  uint32_t semilla = 0;
  int jugador = 0, ganados = 0;
} g_mej;

std::string Num(int n) { return std::to_string(n); }
ImFont* Fuente() {
  const auto& f = dbz3::launcher::ui::GetFonts();
  return f.menu ? f.menu : ImGui::GetFont();
}
float Ancho(float size, const char* s) { return Fuente()->CalcTextSizeA(size, FLT_MAX, 0, s).x; }
float Ajustar(float size, const char* s, float max_w) {
  const float w = Ancho(size, s);
  return max_w > 0 && w > max_w ? size * max_w / w : size;
}
// Texto blanco perfilado (oro = amarillo): izquierda en p.x (Izq) o derecha en p.x (Der); p.y = centro vertical.
void Izq(ImDrawList* dl, ImVec2 p, float size, const std::string& s, bool oro, float max_w = 0) {
  size = Ajustar(size, s.c_str(), max_w);
  S::MenuTextB3(dl, ImVec2(p.x + Ancho(size, s.c_str()) * 0.5f, p.y), size, s.c_str(), oro, false);
}
void Der(ImDrawList* dl, ImVec2 p, float size, const std::string& s, bool oro, float max_w = 0) {
  size = Ajustar(size, s.c_str(), max_w);
  S::MenuTextB3(dl, ImVec2(p.x - Ancho(size, s.c_str()) * 0.5f, p.y), size, s.c_str(), oro, false);
}
void Color(ImDrawList* dl, ImVec2 p, float size, const std::string& s, ImU32 col, bool derecha, float u) {   // verde, etc.
  const float w = Ancho(size, s.c_str());
  const ImVec2 o(derecha ? p.x - w : p.x, p.y - size * 0.5f);
  dl->AddText(Fuente(), size, ImVec2(o.x + 1.5f * u, o.y + 1.5f * u), IM_COL32(0, 0, 0, 220), s.c_str());
  dl->AddText(Fuente(), size, o, col, s.c_str());
}
constexpr ImU32 kVerde = IM_COL32(124, 232, 140, 255);

// Nombre del camino en el idioma del juego (ROG-05: rogue_caminos.toml). Sin id, el de la partida (CaminoActual).
std::string NombreCamino(const std::string& id = {}) {
  const Camino c = CaminoDe(id.empty() ? CaminoActual() : id);
  return M::L(c.nombre[0].c_str(), c.nombre[1].c_str(), c.nombre[2].c_str(), c.nombre[3].c_str(), c.nombre[4].c_str());
}
const char* NombreCat(int c) {
  switch (c) {
    case 0: return "Ki";
    case 1: return M::L("Cuerpo", "Body", "Corps", "Körper", "Corpo");
    case 2: return M::L("Apoyo", "Support", "Soutien", "Hilfe", "Supporto");
    default: return M::L("Evolución", "Evolution", "Évolution", "Evolution", "Evoluzione");
  }
}
constexpr const char* kIconoCat[] = {"ki_15", "fisica_01", "utilidad_03", "evolucion_01"};

// Fila azul de la maqueta (como las del aviso de jefe): nombre a la izquierda, valor a la derecha.
void Fila(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, const std::string& nombre, const std::string& valor) {
  dl->AddRectFilled(p0, p1, IM_COL32(34, 98, 138, 235), 9 * u);
  dl->AddRect(p0, p1, IM_COL32(90, 160, 200, 200), 9 * u, 0, 1.2f * u);
  const float cy = (p0.y + p1.y) * 0.5f, w = p1.x - p0.x;
  Izq(dl, ImVec2(p0.x + 28 * u, cy), 23 * u, nombre, false, w * 0.55f);
  Der(dl, ImVec2(p1.x - 30 * u, cy), 23 * u, valor, false, w * 0.4f);
}
void Panel(ImDrawList* dl, const S::Area& a, ImVec2 p0, ImVec2 p1, float u) {
  if (!R::DibujarNueve(dl, "ui_panel_resultados", p0, p1, 0.42f * u)) S::Frame(dl, p0, p1, a.s);
}
void Cara(ImDrawList* dl, int id, ImVec2 c, float r) {   // retrato redondo con la cara del juego
  dl->AddCircleFilled(c, r, IM_COL32(20, 44, 66, 255), 40);
  if (const S::Pic cara = S::Img(("caras/" + Num(id)).c_str()))
    dl->AddImageRounded(cara.tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
  dl->AddCircle(c, r, IM_COL32(232, 140, 32, 255), 40, 2.0f);
}
void Velo(ImDrawList* dl, int alfa) {   // r23: el fondo de los retos tapa al anfitrion y el velo lo oscurece (antes, el anfitrion se veia detras)
  const S::Imagen im = S::GuestImage(ImGui::GetIO());
  S::Backdrop(dl, S::GameArea(ImGui::GetIO()), S::Img("hub/fondo_retos") ? "hub/fondo_retos" : "hub/fondo");
  dl->AddRectFilled(im.p0, im.p1, IM_COL32(6, 20, 30, std::min(alfa, 205)));
}
void Fondo(ImDrawList* dl, const S::Area& a, const char* propio) {   // tapa el fondo y el anfitrion de modos.cpp
  S::Backdrop(dl, a, S::Img(propio) ? propio : "hub/fondo");
  S::Sides(dl, a);
}

// dia UTC -> dia, mes (1-12), anio (H. Hinnant, civil_from_days)
void Fecha(int dias, int* d, int* m, int* y) {
  int z = dias + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = int(doy - (153 * mp + 2) / 5 + 1);
  *m = int(mp < 10 ? mp + 3 : mp - 9);
  *y = int(yoe) + era * 400 + (*m <= 2 ? 1 : 0);
}
std::string FechaCorta(int dia) {
  static const char* const kMes[5][12] = {
      {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"janv", "févr", "mars", "avr", "mai", "juin", "juil", "août", "sept", "oct", "nov", "déc"},
      {"Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"},
      {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"}};
  int d = 0, m = 1, y = 0;
  Fecha(dia, &d, &m, &y);
  if (!std::strcmp(dbz3::i18n::LangCode(), "ja")) return Num(m) + "月" + Num(d) + "日";   // r23 ROG-07: 11月20日
  if (!std::strcmp(dbz3::i18n::LangCode(), "pt")) {
    static const char* const kPt[12] = {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"};
    return Num(d) + " " + kPt[m - 1];
  }
  const char* l = M::L("0", "1", "2", "3", "4");
  return Num(d) + " " + kMes[std::clamp(l[0] - '0', 0, 4)][m - 1];
}
// Codigo para compartir: ROGUE-DDMM-ABC-PUNTOS (ABC = 3 primeras letras del luchador)
std::string Codigo(int dia, int jugador, int puntos) {
  int d = 0, m = 1, y = 0;
  Fecha(dia, &d, &m, &y);
  std::string abc;
  for (const char c : M::Nombre(jugador)) {
    if (abc.size() < 3 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) abc += char(c & ~0x20);
  }
  char b[64];
  std::snprintf(b, sizeof b, "ROGUE-%02d%02d-%s-%d", d, m, abc.c_str(), puntos);
  return b;
}
int Racha(const std::vector<Dia>& v, int hoy) {   // dias seguidos jugados que acaban hoy o ayer (v: mas nuevo primero)
  int n = 0, esperado = !v.empty() && v[0].dia == hoy ? hoy : hoy - 1;
  for (const Dia& d : v) {
    if (d.dia != esperado) break;
    ++n;
    --esperado;
  }
  return n;
}

// ---- pantalla 06: Entrenamiento de Kami ----
struct Articulo {
  std::string clave, nombre, texto, icono;   // clave de g_compras
  int precio = 0, tiene = 0, niveles = 1, estrellas = 0;
  bool camino = false, abierto_serie = false;
  std::string hito;   // texto del hito (cartas que no se compran)
};
std::vector<Articulo> Articulos(int tab) {   // sin g_mx (llama a rogue.cpp); las compras se leen despues
  std::vector<Articulo> v;
  if (tab == 0) {
    for (const auto& c : Caminos()) {
      Articulo x;
      x.clave = "camino:" + c.id;
      x.nombre = M::L(c.nombre[0].c_str(), c.nombre[1].c_str(), c.nombre[2].c_str(), c.nombre[3].c_str(), c.nombre[4].c_str());
      x.texto = std::string(M::L(c.texto[0].c_str(), c.texto[1].c_str(), c.texto[2].c_str(), c.texto[3].c_str(), c.texto[4].c_str())) + " " +
                M::L("Premio: ", "Prize: ", "Prix : ", "Preis: ", "Premio: ") + Num(c.premio) + " Zenie.";
      x.icono = "rogue/camino_" + c.id + ".png";
      x.precio = c.abierto ? 0 : kPrecioCamino;
      x.camino = true;
      x.abierto_serie = c.abierto;
      v.push_back(x);
    }
  } else if (tab == 1) {
    std::map<std::string, Meta> meta;
    {
      std::lock_guard<std::mutex> lk(g_mx);
      LeerMeta();
      meta = g_meta;
    }
    for (const CartaVista& c : VerCartas(nullptr)) {
      const auto it = meta.find(c.id);
      if (it == meta.end()) continue;
      Articulo x;
      x.clave = "carta:" + c.id;
      x.nombre = c.nombre;
      x.texto = c.texto;
      x.icono = c.id;
      x.estrellas = c.estrellas;
      x.precio = it->second.precio;
      if (it->second.hito_jefes > 0)
        x.hito = std::string(M::L("Vence a ", "Beat ", "Bats ", "Besiege ", "Sconfiggi ")) + Num(it->second.hito_jefes) +
                 M::L(" jefes", " bosses", " boss", " Bosse", " boss");
      v.push_back(x);
    }
  } else {
    for (const Ventaja& w : kVentajas) {
      Articulo x;
      x.clave = w.id;
      x.icono = w.icono;
      x.precio = w.precio;
      x.niveles = w.niveles;
      if (std::string(w.id) == "cambio") {
        x.nombre = M::L("Segundo cambio de cartas", "Second card reroll", "Deuxième changement de cartes", "Zweiter Kartentausch", "Secondo cambio di carte");
        x.texto = M::L("En cada partida puedes cambiar las 3 cartas una vez más.", "In every run you can change the 3 cards one more time.",
                       "À chaque partie, tu peux changer les 3 cartes une fois de plus.", "In jedem Lauf kannst du die 3 Karten einmal mehr tauschen.",
                       "In ogni partita puoi cambiare le 3 carte una volta in più.");
      } else if (std::string(w.id) == "carta") {
        x.nombre = M::L("Empieza con 1 carta común", "Start with 1 common card", "Commence avec 1 carte commune", "Starte mit 1 gewöhnlichen Karte", "Inizia con 1 carta comune");
        x.texto = M::L("Cada partida empieza con una carta común ya elegida (no en el reto diario).", "Every run starts with a common card already chosen (not in the daily challenge).",
                       "Chaque partie commence avec une carte commune déjà choisie (pas dans le défi du jour).", "Jeder Lauf beginnt mit einer bereits gewählten gewöhnlichen Karte (nicht in der Tagesherausforderung).",
                       "Ogni partita inizia con una carta comune già scelta (non nella sfida del giorno).");
      } else if (std::string(w.id) == "salto") {
        x.nombre = M::L("Descanso de Kami", "Kami's rest", "Repos de Kami", "Kamis Rast", "Riposo di Kami");
        x.texto = M::L("Saltar la carta cura un 10 % más de vida (20 % en total).", "Skipping the card heals 10% more health (20% in total).",
                       "Passer la carte soigne 10 % de vie en plus (20 % au total).", "Das Auslassen der Karte heilt 10 % mehr Leben (20 % insgesamt).",
                       "Saltare la carta cura il 10% di vita in più (20% in totale).");
      } else {
        x.nombre = M::L("Vida inicial +500", "Starting health +500", "Vie de départ +500", "Startleben +500", "Vita iniziale +500");
        x.texto = M::L("Empiezas todos los caminos con 500 más de vida (no en el reto diario).",
                       "You start every path with 500 more health (not in the daily challenge).",
                       "Tu commences chaque chemin avec 500 de vie en plus (pas dans le défi du jour).",
                       "Du beginnst jeden Weg mit 500 mehr Leben (nicht in der Tagesherausforderung).",
                       "Inizi ogni cammino con 500 di vita in più (non nella sfida del giorno).");
      }
      v.push_back(x);
    }
  }
  std::lock_guard<std::mutex> lk(g_mx);
  for (Articulo& x : v) x.tiene = Veces(x.clave);
  if (tab == 1) {
    for (Articulo& x : v) {
      if (Abierta(x.clave.substr(6))) x.tiene = std::max(x.tiene, 1);
    }
  }
  return v;
}

Accion Kami(ImDrawList* dl, const S::Area& a, float t, uint16_t p, int* zeni) {
  if (p & kB) {
    g_vista = kNinguna;
    return Accion::kDibujada;
  }
  if (p & (kLb | kLeft)) g_tab = (g_tab + 2) % 3, g_fila = 0, g_aviso.clear();
  if (p & (kRb | kRight)) g_tab = (g_tab + 1) % 3, g_fila = 0, g_aviso.clear();
  std::vector<Articulo> v = Articulos(g_tab);
  const int n = int(v.size());
  if (n > 0) {
    if (p & kUp) g_fila = (g_fila + n - 1) % n, g_aviso.clear();
    if (p & kDown) g_fila = (g_fila + 1) % n, g_aviso.clear();
    g_fila = std::clamp(g_fila, 0, n - 1);
  }
  Accion r = Accion::kDibujada;
  if ((p & kA) && n > 0) {
    Articulo& x = v[size_t(g_fila)];
    const bool lleno = x.abierto_serie || x.tiene >= x.niveles;
    if (lleno) {
      g_aviso = M::L("Ya lo tienes.", "You already have it.", "Tu l'as déjà.", "Hast du schon.", "Ce l'hai già.");
    } else if (x.precio <= 0) {
      g_aviso = x.hito.empty() ? std::string() : x.hito + ".";
    } else if (*zeni < x.precio) {
      g_aviso = M::L("No tienes Zenie suficientes.", "Not enough Zenie.", "Pas assez de Zenie.", "Nicht genug Zenie.", "Zenie insufficienti.");
    } else {
      *zeni -= x.precio;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        g_compras.push_back(x.clave);
      }
      GuardarYa();
      ++x.tiene;
      g_aviso = std::string(M::L("Aprendido: ", "Learned: ", "Appris : ", "Gelernt: ", "Imparato: ")) + x.nombre;
      REXLOG_INFO("dbz3 rogue: kami: compra {} por {} Zenie (quedan {})", x.clave, x.precio, *zeni);
      r = Accion::kZenie;
    }
  }

  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  Fondo(dl, a, "hub/fondo_kami");
  S::BannerBeta(dl, a, M::L("Entrenamiento de Kami", "Kami's training", "Entraînement de Kami", "Kamis Training", "Allenamento di Kami"));
  // Zenie arriba a la derecha
  dl->AddRectFilled(P(950, 20), P(1250, 70), IM_COL32(8, 24, 36, 225), 10 * u);
  Izq(dl, P(975, 45), 24 * u, "Zenie", false);
  R::DibujarCifras(dl, true, std::max(*zeni, 0), P(1105, 62), 34 * u);
  // lista con pestanas
  S::Frame(dl, P(40, 128), P(782, 668), a.s);
  const char* tabs[3] = {M::L("Caminos", "Paths", "Chemins", "Wege", "Cammini"), M::L("Cartas", "Cards", "Cartes", "Karten", "Carte"),
                         M::L("Ventajas", "Perks", "Avantages", "Vorteile", "Vantaggi")};
  for (int k = 0; k < 3; ++k) S::MenuText(dl, P(141.0f + 190.0f * float(k), 158), 30 * u, tabs[k], k == g_tab, k != g_tab);
  dl->AddLine(P(55, 186), P(767, 186), IM_COL32(232, 140, 32, 255), 1.5f * u);
  const int primera = std::clamp(g_fila - 6, 0, std::max(n - 7, 0));
  for (int i = primera; i < n && i < primera + 7; ++i) {
    const Articulo& x = v[size_t(i)];
    const float y = 230 + 62.0f * float(i - primera);
    if (i == g_fila) D::Stripe(dl, a, 55 * 1.25f - 200, (y - 26) * 1.25f, 767 * 1.25f - 200, (y + 26) * 1.25f, t);   // px del area
    const float tam_nombre = Ajustar(26 * u, x.nombre.c_str(), 440 * u);
    S::MenuText(dl, ImVec2(P(76, 0).x + Ancho(tam_nombre, x.nombre.c_str()) * 0.5f, P(0, y).y), tam_nombre, x.nombre.c_str(), i == g_fila,
                false, 440 * u);
    if (x.niveles > 1) Der(dl, P(590, y), 20 * u, Num(x.tiene) + " / " + Num(x.niveles), false);
    if (x.abierto_serie || x.tiene >= x.niveles) {
      Color(dl, P(737, y), 21 * u, x.abierto_serie || x.precio <= 0 ? M::L("Abierto", "Open", "Ouvert", "Offen", "Aperto") : M::L("Comprado", "Bought", "Acheté", "Gekauft", "Comprato"),
            kVerde, true, u);
    } else if (x.precio > 0) {
      const float w = R::DibujarCifras(dl, true, x.precio, ImVec2(-10000, 0), 30 * u, IM_COL32(0, 0, 0, 0));   // medir
      R::DibujarCifras(dl, true, x.precio, ImVec2(P(737, 0).x - w * 0.5f, P(0, y + 14).y), 30 * u);
    } else {
      Der(dl, P(737, y), 19 * u, x.hito, false, 260 * u);
    }
  }
  if (n == 0) Izq(dl, P(76, 230), 24 * u, M::L("Nada por ahora", "Nothing for now", "Rien pour l'instant", "Vorerst nichts", "Niente per ora"), false);
  // detalle de la fila elegida
  S::Frame(dl, P(828, 128), P(1242, 440), a.s);
  if (n > 0) {
    const Articulo& x = v[size_t(g_fila)];
    const ps2::Tex* ic = x.camino ? R::ArteMejoras(x.icono.c_str()) : R::Icono(x.icono.c_str());
    R::DibujarAjustada(dl, ic, P(985, 145), P(1085, 245));
    S::MenuTextB3(dl, P(1035, 270), 28 * u, x.nombre.c_str(), true, false, 380 * u);
    if (x.estrellas > 0) {
      char est[20];
      std::snprintf(est, sizeof est, "ui_estrella_%d", x.estrellas);
      R::DibujarAjustada(dl, R::Ui(est), P(1015, 285), P(1055, 315));
    }
    std::istringstream in(x.texto);
    std::string w, l;
    float y = 335;
    auto linea = [&](const std::string& s) {
      S::MenuTextB3(dl, P(1035, y), 21 * u, s.c_str(), false, false, 390 * u);
      y += 30;
    };
    while (in >> w) {   // partir en lineas de ~380 px
      const std::string c = l.empty() ? w : l + " " + w;
      if (!l.empty() && Ancho(21 * u, c.c_str()) > 370 * u) {
        linea(l);
        l = w;
      } else {
        l = c;
      }
    }
    if (!l.empty()) linea(l);
    if (x.niveles > 1) linea(std::string(M::L("Nivel ", "Level ", "Niveau ", "Stufe ", "Livello ")) + Num(x.tiene) + M::L(" de ", " of ", " sur ", " von ", " di ") + Num(x.niveles));
  }
  // Kami: retrato propio (encargo 4, hub/kami_retrato.png, circulo con alfa); sin el, la cara de Piccolo
  // 21/11: con el busto de dialogo (encargo 4, 416x408, mira a la derecha) Kami habla junto a su bocadillo; el circulo
  // queda de respaldo.
  float bx = 936;   // borde izquierdo del bocadillo
  if (const S::Pic k = S::Img("hub/kami_busto_dialogo")) {
    S::DrawPic(dl, k, P(828, 452), P(828 + 172.0f * 416.0f / 408.0f, 624), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    bx = 1010;
  } else if (const S::Pic k = S::Img("hub/kami_retrato")) {
    const ImVec2 c = P(880, 532);
    const float r = 42 * u * 256.0f / 248.0f;   // el circulo del dibujo mide 248 de 256
    S::DrawPic(dl, k, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
  } else {
    Cara(dl, 11, P(880, 532), 42 * u);
  }
  dl->AddRectFilled(P(bx, 487), P(1250, 577), IM_COL32(8, 24, 36, 225), 10 * u);
  const std::string dice = !g_aviso.empty() ? g_aviso
                                            : M::L("Lo que aprendes aquí no se pierde al caer.", "What you learn here is not lost when you fall.",
                                                   "Ce que tu apprends ici ne se perd pas quand tu tombes.",
                                                   "Was du hier lernst, verlierst du nicht, wenn du fällst.",
                                                   "Ciò che impari qui non si perde quando cadi.");
  const float cx = (bx + 1250) * 0.5f, ancho = (1250 - bx - 14) * u;
  const size_t medio = dice.size() / 2, sa = dice.find_last_of(' ', medio), sd = dice.find(' ', medio);
  const size_t sp = sa == std::string::npos ? sd : sd == std::string::npos ? sa : (medio - sa <= sd - medio ? sa : sd);   // el espacio mas centrado
  if (Ancho(21 * u, dice.c_str()) > ancho && sp != std::string::npos) {   // con el busto el bocadillo es mas estrecho: dos lineas
    S::MenuTextB3(dl, P(cx, 517), 21 * u, dice.substr(0, sp).c_str(), false, false, ancho);
    S::MenuTextB3(dl, P(cx, 547), 21 * u, dice.substr(sp + 1).c_str(), false, false, ancho);
  } else {
    S::MenuTextB3(dl, P(cx, 532), 21 * u, dice.c_str(), false, false, ancho);
  }
  D::Help(dl, a, {{"A", M::L("Comprar", "Buy", "Acheter", "Kaufen", "Compra")},
                  {"RB", M::L("Pestaña", "Tab", "Onglet", "Reiter", "Scheda")},
                  {"B", M::L("Volver", "Back", "Retour", "Zurück", "Indietro")}});
  return r;
}

// ---- pantalla 07: Reto diario ----
Accion Diario(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  if (dbz3::rank::Encima(dl, a, t, p)) return Accion::kDibujada;   // RANK-ONLINE: ficha de una partida del mundial
  if (p & kB) {
    g_vista = kNinguna;
    return Accion::kDibujada;
  }
  const int hoy = DiaUtc(), jd = JugadorDiario(hoy);
  Partida medias;
  const bool a_medias = LeerPartida(&medias);
  std::vector<Dia> dias;
  bool practica = false;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    dias = g_dias;
    practica = g_dia.intento == hoy;
  }
  if (p & kA) {
    if (a_medias) {
      g_aviso = M::L("Antes termina o abandona tu partida a medias.", "First finish or quit your run in progress.",
                     "Termine ou abandonne d'abord ta partie en cours.", "Beende oder verlasse zuerst deinen laufenden Lauf.",
                     "Prima finisci o abbandona la partita in corso.");
    } else {
      std::lock_guard<std::mutex> lk(g_mx);
      g_pend_diario = true;
      g_vista = kNinguna;
      return Accion::kJugarDiario;
    }
  }
  const Dia* d = nullptr;
  for (const Dia& x : dias) {
    if (x.dia == hoy) d = &x;
  }

  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  Fondo(dl, a, "hub/fondo_diario");
  S::BannerBeta(dl, a, M::L("Reto diario", "Daily challenge", "Défi du jour", "Tagesherausforderung", "Sfida del giorno"));
  // hoy
  S::Frame(dl, P(40, 128), P(625, 668), a.s);
  Izq(dl, P(66, 158), 26 * u, std::string(M::L("Hoy  -  ", "Today  -  ", "Aujourd'hui  -  ", "Heute  -  ", "Oggi  -  ")) + FechaCorta(hoy), true);
  dl->AddLine(P(55, 186), P(610, 186), IM_COL32(232, 140, 32, 255), 1.5f * u);
  Cara(dl, jd, P(100, 236), 33 * u);
  Izq(dl, P(160, 220), 25 * u, M::Nombre(jd) + M::L("  en  ", "  in  ", "  dans  ", "  in  ", "  in  ") + NombreCamino("abismo"), false, 440 * u);
  Izq(dl, P(160, 255), 17 * u, M::L("Mismos rivales y mismas cartas para todos", "Same rivals and same cards for everyone",
                                     "Mêmes rivaux et mêmes cartes pour tous", "Gleiche Gegner und gleiche Karten für alle",
                                     "Stessi rivali e stesse carte per tutti"), false, 440 * u);
  const std::string guion = "-";
  const std::pair<std::string, std::string> filas[] = {
      {std::string(M::L("Pisos  ", "Floors  ", "Étages  ", "Etagen  ", "Piani  ")) + (d ? Num(d->ganados) + " x 100" : ""), d ? Num(d->ganados * 100) : guion},
      {std::string(M::L("Jefes  ", "Bosses  ", "Boss  ", "Bosse  ", "Boss  ")) + (d ? Num(d->jefes) + " x 500" : ""), d ? Num(d->jefes * 500) : guion},
      {std::string(M::L("Vida final  ", "Final health  ", "Vie finale  ", "Restleben  ", "Vita finale  ")) + (d ? Num(d->vida) + " / 10" : ""), d ? Num(d->vida / 10) : guion},
      {std::string(M::L("Tiempo  ", "Time  ", "Temps  ", "Zeit  ", "Tempo  ")) + (d ? Num(d->segundos) + " s" : ""), d ? "-" + Num(d->segundos) : guion}};
  float y = 296;
  for (const auto& [nombre, valor] : filas) {
    Fila(dl, P(62, y), P(598, y + 44), u, nombre, valor);
    y += 50;
  }
  dl->AddLine(P(55, 507), P(610, 507), IM_COL32(232, 140, 32, 255), 1.5f * u);
  Izq(dl, P(66, 540), 26 * u, M::L("Tu puntuación", "Your score", "Ton score", "Deine Punkte", "Il tuo punteggio"), true);
  if (d) R::DibujarCifras(dl, true, d->puntos, P(462, 558), 42 * u);
  else Der(dl, P(598, 540), 21 * u, M::L("Aún no has jugado hoy", "Not played today yet", "Pas encore joué aujourd'hui", "Heute noch nicht gespielt", "Non hai ancora giocato oggi"), false);
  dl->AddRectFilled(P(62, 588), P(598, 655), IM_COL32(8, 24, 36, 225), 10 * u);
  if (!g_aviso.empty()) {
    Izq(dl, P(82, 621), 19 * u, g_aviso, true, 500 * u);
  } else if (d && dbz3::rank::Activo()) {   // RANK-ONLINE: estado del envio y puesto de hoy
    dbz3::rank::CajaDiario(dl, P(62, 588), P(598, 655), u, hoy, Codigo(hoy, d->jugador, d->puntos));
  } else {
    Izq(dl, P(82, 604), 16 * u, M::L("Código para compartir", "Code to share", "Code à partager", "Code zum Teilen", "Codice da condividere"), false);
    Izq(dl, P(82, 634), 25 * u, d ? Codigo(hoy, d->jugador, d->puntos) : M::L("Juega para obtenerlo", "Play to get it", "Joue pour l'obtenir", "Spiele, um ihn zu bekommen", "Gioca per ottenerlo"),
        false, 500 * u);
  }
  // ultimos dias (RANK-ONLINE: con el ranking online, el mundial de hoy; Y cambia a estos)
  const bool mundial = dbz3::rank::PanelDiario(dl, P(668, 128), P(1245, 668), u, p, hoy, jd, NombreCamino("abismo"));
  if (!mundial) {   // RANK-ONLINE
  S::Frame(dl, P(668, 128), P(1245, 668), a.s);
  Izq(dl, P(696, 158), 26 * u, M::L("Tus últimos días", "Your last days", "Tes derniers jours", "Deine letzten Tage", "I tuoi ultimi giorni"), true);
  Color(dl, P(1212, 161), 19 * u, std::string(M::L("Racha: ", "Streak: ", "Série : ", "Serie: ", "Serie: ")) + Num(Racha(dias, hoy)) + (!std::strcmp(dbz3::i18n::LangCode(), "ja") ? std::string("日") : !std::strcmp(dbz3::i18n::LangCode(), "pt") ? std::string(Racha(dias, hoy) == 1 ? " dia" : " dias") : std::string(Racha(dias, hoy) == 1 ? M::L(" día", " day", " jour", " Tag", " giorno") : M::L(" días", " days", " jours", " Tage", " giorni"))),
        kVerde, true, u);
  dl->AddLine(P(683, 186), P(1230, 186), IM_COL32(232, 140, 32, 255), 1.5f * u);
  int mejor = 0;
  for (const Dia& x : dias) mejor = std::max(mejor, x.puntos);
  for (size_t i = 0; i < dias.size() && i < 6; ++i) {
    const Dia& x = dias[i];
    const float yy = 229 + 62.0f * float(i);
    if (x.dia == hoy) dl->AddRectFilled(P(684, yy - 26), P(1228, yy + 26), IM_COL32(60, 140, 190, 120), 6 * u);
    Izq(dl, P(700, yy), 19 * u, FechaCorta(x.dia), false);
    Cara(dl, x.jugador, P(813, yy), 22 * u);
    const std::string cam = NombreCamino("abismo");
    S::MenuText(dl, ImVec2(P(858, 0).x + Ancho(25 * u, cam.c_str()) * 0.5f, P(0, yy).y), 25 * u, cam.c_str(), x.dia == hoy, false);
    Der(dl, P(1195, yy), 25 * u, Num(x.puntos), x.puntos == mejor, 140 * u);
  }
  if (dias.empty()) Izq(dl, P(700, 229), 21 * u, M::L("Aún no hay días jugados", "No days played yet", "Aucun jour joué", "Noch keine Tage gespielt", "Nessun giorno giocato"), false);
  if (mejor > 0) {
    R::DibujarAjustada(dl, R::Ui("ui_medalla_oro"), P(700, 590), P(752, 650));
    Izq(dl, P(765, 620), 19 * u, std::string(M::L("Mejor: ", "Best: ", "Meilleur : ", "Bester: ", "Migliore: ")) + Num(mejor) +
                                     M::L("  -  solo en este PC", "  -  this PC only", "  -  sur ce PC seulement", "  -  nur auf diesem PC", "  -  solo su questo PC"),
        false, 440 * u);
  }
  }  // RANK-ONLINE
  std::vector<std::pair<const char*, const char*>> ayuda = {
      {"A", practica ? M::L("Practicar (ya puntuaste)", "Practice (already scored)", "S'entraîner (déjà noté)", "Üben (schon gewertet)", "Allenati (già valutato)")
                     : M::L("Jugar", "Play", "Jouer", "Spielen", "Gioca")}};
  if (dbz3::rank::Activo()) {   // RANK-ONLINE
    if (mundial) ayuda.push_back({"LB", M::L("Hoy / Ayer", "Today / Yesterday", "Aujourd'hui / Hier", "Heute / Gestern", "Oggi / Ieri")});
    if (mundial) ayuda.push_back({"X", M::L("Ver partida", "View run", "Voir la partie", "Run ansehen", "Vedi partita")});
    ayuda.push_back({"Y", mundial ? M::L("Mis días", "My days", "Mes jours", "Meine Tage", "I miei giorni") : M::L("Mundial", "World", "Monde", "Welt", "Mondo")});
  }
  ayuda.push_back({"B", M::L("Volver", "Back", "Retour", "Zurück", "Indietro")});
  D::Help(dl, a, ayuda);
  return Accion::kDibujada;
}

// ---- "ver mis mejoras" ----
Accion Mejoras(ImDrawList* dl, const S::Area& a, uint16_t p) {
  if (p & (kB | kY | kA)) {
    g_vista = kNinguna;
    return Accion::kDibujada;
  }
  const std::vector<CartaVista> v = VerCartas(&g_mej.semilla);
  const M::Reglas g = ReglasPartida(g_mej.semilla, g_mej.jugador, g_mej.ganados);
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  Velo(dl, 245);
  S::TitleText(dl, a, 600, 70, 700, M::L("Tus mejoras", "Your upgrades", "Tes améliorations", "Deine Verbesserungen", "I tuoi potenziamenti"), 1.0f);
  const std::string sub = std::string(NombreCamino()) + "   -   " + M::L("Piso ", "Floor ", "Étage ", "Ebene ", "Piano ") + Num(g_mej.ganados + 1);
  S::MenuTextB3(dl, P(640, 100), 22 * u, sub.c_str(), false, false, 900 * u);
  int sellos[4] = {}, caps = 0, n = 0;
  for (const CartaVista& c : v) sellos[std::clamp(c.cat, 0, 3)] += c.veces;
  for (const CartaVista& c : v) {
    if (n >= 20) break;   // ponytail: 20 casillas (la baraja tiene menos cartas distintas)
    const float x = 150 + 200.0f * float(n % 5), y = 130 + 106.0f * float(n / 5);
    ++n;
    if (!R::DibujarNueve(dl, "ui_casilla_mejora", P(x, y), P(x + 84, y + 84), 0.3f * u)) R::DibujarImagen(dl, R::Ui("ui_casilla_mejora"), P(x, y), P(x + 84, y + 84));
    R::DibujarAjustada(dl, R::Icono(c.id.c_str()), P(x + 8, y + 8), P(x + 76, y + 76));
    Izq(dl, P(x + 92, y + 22), 17 * u, c.nombre, true, 100 * u);
    char est[20];
    std::snprintf(est, sizeof est, "ui_estrella_%d", c.estrellas);
    R::DibujarAjustada(dl, R::Ui(est), P(x + 92, y + 38), P(x + 124, y + 64));
    if (c.max > 1) Izq(dl, P(x + 130, y + 52), 18 * u, "x" + Num(c.veces), false);
    Izq(dl, P(x + 92, y + 80), 14 * u, NombreCat(c.cat), false, 100 * u);
  }
  if (v.empty()) S::MenuTextB3(dl, P(640, 300), 26 * u, M::L("Aún no llevas mejoras", "No upgrades yet", "Pas encore d'améliorations", "Noch keine Verbesserungen", "Ancora nessun potenziamento"), false, false);
  caps = std::max(g.ncaps, 0);
  // abajo: reglas que dan las cartas y sellos
  dl->AddRectFilled(P(130, 566), P(1150, 658), IM_COL32(8, 24, 36, 220), 10 * u);
  const std::string reglas = std::string(M::L("Daño ", "Damage ", "Dégâts ", "Schaden ", "Danni ")) + Num(g.dano_hecho) + " %   -   " +
                             M::L("Recibes ", "You take ", "Tu subis ", "Du erleidest ", "Subisci ") + Num(g.dano_recibido) + " %   -   Ki " +
                             Num(-g.ki_drena) + " %/s   -   " + Num(caps) + " / 7 " + M::L("cápsulas", "capsules", "capsules", "Kapseln", "capsule");
  Izq(dl, P(155, 588), 20 * u, reglas, false, 980 * u);
  for (int k = 0; k < 3; ++k) {
    const float sx = 160 + 250.0f * float(k);
    R::DibujarAjustada(dl, R::Icono(kIconoCat[k]), P(sx, 610), P(sx + 30, 640));
    Izq(dl, P(sx + 40, 625), 20 * u, std::string(NombreCat(k)) + "  " + Num(std::min(sellos[k], 4)) + "/" + Num(sellos[k] < 2 ? 2 : 4), sellos[k] >= 2, 190 * u);
  }
  D::Help(dl, a, {{"B", M::L("Volver", "Back", "Retour", "Zurück", "Indietro")}});
  return Accion::kDibujada;
}
}  // namespace

void AbrirKami() {
  g_vista = kKami;
  g_aviso.clear();
  g_pend_diario = false;
}
void AbrirDiario() {
  g_vista = kDiario;
  g_aviso.clear();
  g_pend_diario = false;
  REXLOG_INFO("dbz3 rogue: reto diario {}: pantalla (luchador {})", DiaUtc(), JugadorDiario(DiaUtc()));
}
void AbrirMejoras(uint32_t semilla, int jugador, int ganados) {
  g_vista = kMejoras;
  g_mej = {semilla, jugador, ganados};
  REXLOG_INFO("dbz3 rogue: ver mis mejoras ({} cartas distintas)", VerCartas(&semilla).size());
}

Accion Pantalla(ImDrawList* dl, const S::Area& a, float t, uint16_t pulsado, int* zeni) {
  switch (g_vista) {
    case kKami: return Kami(dl, a, t, pulsado, zeni);
    case kDiario: return Diario(dl, a, t, pulsado);
    case kMejoras: return Mejoras(dl, a, pulsado);
    default: return Accion::kNada;
  }
}

// ---- pantalla 05: Fin del camino ----
Accion Resultado(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  (void)t;
  Ultimo ul;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    ul = g_ult;
  }
  if (!ul.hay) return Accion::kNada;
  // RANK-ONLINE: el intento puntuable del reto diario puede ir al ranking mundial (bloque en lugar de "Nuevo")
  dbz3::rank::Partida rk;
  if (dbz3::rank::Activo() && ul.puntos >= 0) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (const Dia& x : g_dias) {
      if (x.puntos == ul.puntos && x.jugador == ul.d.jugador && x.dia >= DiaUtc() - 1) {
        rk = {'r', x.jugador, x.segundos, x.puntos, x.dia, x.ganados, x.jefes, x.vida, ul.d.semilla};
        break;
      }
    }
  }
  if (rk.modo) {
    rk.lugar = NombreCamino();
    if (dbz3::rank::Encima(dl, a, t, p)) return Accion::kDibujada;
    p = dbz3::rank::Entrada(p, rk, dbz3::rank::kBotonY);
  }
  if (p & (kA | kB)) return Accion::kCerrar;
  if (p & kX) return Accion::kOtraVez;
  if (p & kY) return Accion::kKami;
  const DatosFin& d = ul.d;
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  Velo(dl, 235);
  Panel(dl, a, P(300, 30), P(985, 660), u);
  S::TitleText(dl, a, 600, 110, 520, M::L("Fin del camino", "End of the path", "Fin du chemin", "Ende des Weges", "Fine del cammino"), 1.2f);
  const std::string sub = std::string(NombreCamino()) + "  -  " + M::Nombre(d.jugador) + "  -  " +
                          (d.completo ? M::L("camino completado", "path cleared", "chemin terminé", "Weg geschafft", "cammino completato")
                                      : std::string(M::L("caíste en el piso ", "fell on floor ", "tombé à l'étage ", "gefallen auf Ebene ", "caduto al piano ")) + Num(d.ganados + 1));
  S::MenuTextB3(dl, P(640, 157), 22 * u, sub.c_str(), false, false, 620 * u);
  std::string sin;
  for (int k = 0; k < 3; ++k) {
    if (d.sellos[k] >= 4) sin += (sin.empty() ? "" : " - ") + std::string(NombreCat(k));
  }
  const bool nuevo = d.ganados > d.record_antes;
  const std::string record = Num(std::max(d.ganados, d.record_antes)) + "   " +
                             (nuevo ? M::L("(¡nuevo!)", "(new!)", "(nouveau !)", "(neu!)", "(nuovo!)") : M::L("(no superado)", "(not beaten)", "(non battu)", "(nicht geschlagen)", "(non battuto)"));
  const std::pair<std::string, std::string> filas[] = {
      {M::L("Pisos superados", "Floors cleared", "Étages franchis", "Geschaffte Etagen", "Piani superati"), Num(d.ganados)},
      {M::L("Jefes vencidos", "Bosses beaten", "Boss vaincus", "Besiegte Bosse", "Boss sconfitti"), Num(d.jefes)},
      {M::L("Récord", "Record", "Record", "Rekord", "Record"), record},
      {M::L("Sinergias completas", "Full synergies", "Synergies complètes", "Volle Synergien", "Sinergie complete"), sin.empty() ? "-" : sin}};
  float y = 176;
  for (const auto& [nombre, valor] : filas) {
    Fila(dl, P(340, y), P(940, y + 44), u, nombre, valor);
    y += 50;
  }
  dl->AddLine(P(320, 389), P(960, 389), IM_COL32(232, 140, 32, 255), 1.5f * u);
  Izq(dl, P(346, 424), 25 * u, M::L("Premio", "Prize", "Prix", "Preis", "Premio"), true);
  R::DibujarAjustada(dl, R::Ui("ui_moneda"), P(476, 402), P(536, 448));
  R::DibujarCifras(dl, true, d.zeni, P(610, 444), 40 * u);
  std::string desglose = Num(d.ganados) + " x 50";
  if (d.jefes > 0) desglose += "  +  " + Num(d.jefes) + " x 300";
  if (d.completo) desglose += "  +  800";
  if (ul.puntos >= 0) desglose += "   (" + Num(ul.puntos) + M::L(" puntos)", " points)", " points)", " Punkte)", " punti)");
  Der(dl, P(937, 428), 18 * u, desglose, false, 300 * u);
  dl->AddLine(P(320, 465), P(960, 465), IM_COL32(232, 140, 32, 255), 1.5f * u);
  // lo nuevo: carta abierta por un hito; si no, el record o un consejo
  if (rk.modo) dbz3::rank::Bloque(dl, P(344, 503), 592, u, rk);   // RANK-ONLINE
  else {   // RANK-ONLINE
  Izq(dl, P(346, 496), 25 * u, M::L("¡Nuevo!", "New!", "Nouveau !", "Neu!", "Nuovo!"), true);
  if (!ul.nuevas.empty()) {
    const std::vector<CartaVista> todas = VerCartas(nullptr);
    const auto it = std::find_if(todas.begin(), todas.end(), [&ul](const CartaVista& c) { return c.id == ul.nuevas[0]; });
    R::DibujarAjustada(dl, R::Icono(ul.nuevas[0].c_str()), P(348, 521), P(414, 588));
    Color(dl, P(436, 543), 24 * u, std::string(M::L("Carta nueva: ", "New card: ", "Nouvelle carte : ", "Neue Karte: ", "Nuova carta: ")) + (it != todas.end() ? it->nombre : ul.nuevas[0]),
          kVerde, false, u);
    Izq(dl, P(436, 577), 18 * u, M::L("Por los jefes que llevas vencidos. Ya puede salir en la oferta.", "For the bosses you have beaten. It can now be offered.",
                                      "Pour les boss que tu as vaincus. Elle peut maintenant sortir.", "Für die besiegten Bosse. Sie kann jetzt angeboten werden.",
                                      "Per i boss che hai sconfitto. Ora può uscire nell'offerta."), false, 520 * u);
  } else if (nuevo) {
    Color(dl, P(436, 543), 24 * u, std::string(M::L("Récord con ", "Record with ", "Record avec ", "Rekord mit ", "Record con ")) + M::Nombre(d.jugador) + ": " + Num(d.ganados), kVerde, false, u);
  } else {
    Izq(dl, P(436, 543), 20 * u, M::L("Gasta tus Zenie en el Entrenamiento de Kami (Y).", "Spend your Zenie in Kami's training (Y).",
                                      "Dépense tes Zenie à l'entraînement de Kami (Y).", "Gib deine Zenie in Kamis Training aus (Y).",
                                      "Spendi i tuoi Zenie nell'allenamento di Kami (Y)."), false, 520 * u);
  }
  }  // RANK-ONLINE
  if (rk.modo && dbz3::rank::Consultando()) {   // RANK-ONLINE: aviso de privacidad (Si / No)
    D::Help(dl, a, {{"A", M::L("Sí, enviar", "Yes, send", "Oui, envoyer", "Ja, senden", "Sì, invia")},
                    {"B", M::L("No", "No", "Non", "Nein", "No")}});
  } else if (rk.modo && dbz3::rank::EligiendoAlias()) {   // RANK-ONLINE
    D::Help(dl, a, {{"A", M::L("Usar este nombre", "Use this name", "Utiliser ce nom", "Diesen Namen nehmen", "Usa questo nome")},
                    {"B", M::L("Ahora no (no se envía)", "Not now (not sent)", "Pas maintenant (non envoyée)", "Jetzt nicht (nicht gesendet)", "Non ora (non si invia)")}});
  } else if (rk.modo) {
    std::vector<std::pair<const char*, const char*>> ayuda = {{"A", M::L("Continuar", "Continue", "Continuer", "Weiter", "Continua")},
                                                              {"X", M::L("Otra vez", "Again", "Encore", "Nochmal", "Ancora")}};
    if (const char* y = dbz3::rank::EtiquetaBoton()) ayuda.push_back({"Y", y});
    D::Help(dl, a, ayuda);
  } else
  D::Help(dl, a, {{"A", M::L("Continuar", "Continue", "Continuer", "Weiter", "Continua")},
                  {"X", M::L("Otra vez", "Again", "Encore", "Nochmal", "Ancora")},
                   {"Y", M::L("Entrenamiento de Kami", "Kami's training", "Entraînement de Kami", "Kamis Training", "Allenamento di Kami")}});
  fx::Pintar(dl, a, fx::Tipo::Resultado);
  return Accion::kDibujada;
}

// ---- pestana "Dragon Rogue" del Ranking (modos.cpp) ----
std::vector<FilaRanking> RankingRogue(std::string* dice) {
  (void)RecordCamino("", -1);   // r23: fuerza la lectura de rogue.toml (si la pestana se arma antes, no habria dias)
  std::vector<Dia> dias;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    dias = g_dias;
  }
  const int hoy = DiaUtc();
  int mejor = 0;
  for (const Dia& d : dias) mejor = std::max(mejor, d.puntos);
  std::vector<FilaRanking> v;
  for (const Dia& d : dias) {   // ponytail: la tabla del Ranking ensena 8 filas; los 30 dias quedan en rogue.toml
    v.push_back({FechaCorta(d.dia), M::Nombre(d.jugador), Num(d.puntos), Num(d.ganados) + M::L(" p", " fl", " ét", " E", " p"), d.puntos == mejor ? 3 : 0});
  }
  if (dice) {
    *dice = std::string(M::L("Reto diario: un intento puntuable al día. Racha: ", "Daily challenge: one scored try per day. Streak: ",
                             "Défi du jour : un essai noté par jour. Série : ", "Tagesherausforderung: ein gewerteter Versuch pro Tag. Serie: ",
                             "Sfida del giorno: un tentativo valutato al giorno. Serie: ")) +
            Num(Racha(dias, hoy)) + ". " + M::L("Mejor: ", "Best: ", "Meilleur : ", "Bester: ", "Migliore: ") + Num(mejor) + ".";
    if (!dias.empty() && dias[0].dia == hoy) *dice += "\n" + Codigo(hoy, dias[0].jugador, dias[0].puntos);
  }
  return v;
}

}  // namespace dbz3::rogue
