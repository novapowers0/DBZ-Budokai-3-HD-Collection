// Dragon Tactics H1/T6-T8 + H3: entrada del HUB + arena + partida jugable. Precarga de las 8 figuras con barra de progreso (y error legible); despues se pide un
// combate NATIVO de B3 en el escenario del Torneo (hub::RequestBattleEx, como los minijuegos de IW): los dos luchadores ocultos y quietos,
// sin marcador ni tiempo, y la camara del combate fijada (cinematica, una escena de un paso) sobre el centro del ring. Encima se pinta el
// tablero HEXAGONAL de 6x6 casillas, puesto sobre el suelo del ring y visto con ESA camara (todas las figuras en la misma Escena,
// ordenadas juntas), con un 3 contra 3 o 6 contra 6. El combate lo juega la simulacion de sim.cpp: pasos fijos de 1/60 s con un acumulador
// (nunca depende de los FPS ni del dibujo; pausa y descarte del tiempo si se pierde el foco o con P) y la presentacion solo lee sus eventos
// y una instantanea del estado por cuadro: posicion interpolada entre casillas, clip segun el evento, barras reales y numeros de dano.
// H3: sobre ese tablero se juega la partida de partida.cpp contra un bot: preparacion (sin limite de tiempo: comprar, vender, colocar,
// Comprar EXP, Actualizar) > combate > resultado > preparacion, hasta que un entrenador llegue a 0 de vida. La interfaz sigue la maqueta
// (ronda/fase/tiempo arriba, sinergias a la izquierda, jugadores a la derecha, nivel/oro/tienda/banquillo abajo) con el mando (cursor por
// zonas: tablero, banquillo, tienda y botones; A confirma, B cancela, X vende, Y velocidad, Start pausa) y el raton como extra.
// H5: capsulas (panel a la izquierda: A coge, A sobre una unidad equipa, Q / LB / clic central quita), tutorial de 4 pantallas (la primera vez; Select / T / boton)
// y sonidos del combate (audio.h: maximo de voces con prioridad a tecnicas y KO; suenan por combate.cpp, gestor de efectos del juego).
// B / Esc sin nada en la mano: pide salir; una segunda pulsacion en 3 s vuelve al HUB (acaba el combate de fondo).
// Pruebas (tools/pruebas.py): DBZ3_TEST_TACTICS_ENTRADA=N abre y cierra la arena N veces seguidas desde el menu principal;
// DBZ3_TEST_TACTICS_AUTO=N juega N rondas con compras y colocacion automaticas (el piloto del bot) y SEMILLA=N fija la semilla;
// DBZ3_TEST_TACTICS_VEL=1,4 (o LADO=3|6) en vez de la partida juega la ronda de demostracion 3v3/6v6 de la simulacion a esas velocidades.
#include "entrada.h"

#include <imgui.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <array>
#include <set>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../cinematica.h"
#include "../combate.h"
#include "../eu_map.h"
#include "generated/dbz3_init.h"
#include "../ingame/game_style.h"
#include "../ingame/hub_panel.h"
#include "../ingame/online_panel.h"
#include "../launcher/i18n.h"
#include "../launcher/launcher_state.h"
#include "../launcher/settings.h"
#include "../launcher/ui_kit.h"
#include "../mods.h"
#include "../pasarela.h"
#include "../ps2_modelo.h"
#include "audio.h"
#include "catalogo.h"
#include "partida.h"
#include "sim.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);   // hub.cpp
void BattleModeNext(int mode);
bool OwnBattle();
}  // namespace dbz3::hub

namespace dbz3::pasarela::nave {
float AltoRelFiguraB3(int b3, int forma, int fid);   // pasarela_nave.cpp: altura relativa del modelo (0 = no listo)
int EstadoFiguraB3(int b3, int forma, int fid = -1);   // pasarela_nave.cpp: 0 falta algo de B3 HD, 1 preparandose, 2 lista
bool DibujarFiguraB3En(dbz3::ps2::Escena* esc, const dbz3::ps2::Camara& cam, int b3, int forma, dbz3::ps2::V3 suelo, float alto, float giro, int clip, float t, int fid = -1, float dura = 0.0f,
                       float ciclo = -1.0f, int clip_prev = -1, float t_prev = 0.0f, float mezcla = 0.0f);
}  // namespace dbz3::pasarela::nave

namespace dbz3::tactics {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
using Reloj = std::chrono::steady_clock;

constexpr uint16_t kB = 0x2000, kX = 0x4000, kA = 0x1000, kY = 0x8000, kStart = 0x0010, kBack = 0x0020, kLB = 0x0100, kRB = 0x0200;
constexpr int kFilas = 6, kCols = 6;   // tablero hexagonal de 6x6 casillas (filas impares desplazadas media casilla)
constexpr float kAncho = 1.1f;         // ancho de una casilla (mundo); el alto de la fila es 0,866 de eso
constexpr float kAltoFigura = 1.25f;    // altura de una figura (mundo)
constexpr double kTopeCarga = 180.0;   // s: el mismo tope que la generacion de modelos de la nave
// El ring del Torneo (registro 0 de escenarios.cpp), en unidades de B3: la plaza mide ~300 de ancho y un luchador de serie ~12; las figuras
// van a kAltoFigura x kEscala (33) para que se lean con la camara elevada de la maqueta (medido con capturas: plaza, puerta y cabezas de dragon).
constexpr int kEscenarioTorneo = 0;
constexpr float kEscala = 22.0f;
constexpr float kCamAlto = 139.0f, kCamAtras = 146.0f, kCamObjZ = 29.0f;   // H12: se mira un poco delante del centro para subir el tablero en pantalla   // camara elevada: ojo a esta altura y distancia del centro del ring (marco de los luchadores)
constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kPos = 48, kEstado = 35128, kAvisoFuera = 35569;   // combate: luchador 1 y paso al 2; posicion; estado; aviso de ring out

constexpr int kFondoA = 0, kFondoB = 7;   // los luchadores del combate de fondo (RequestBattleEx): el juego ya carga su voz en las ranuras 3 y 4
// Mascara de personajes B3 (bit = id) de unos modelos cuya voz hay que cargar (los dos de fondo ya la tienen)
struct Modelo;
struct Modelo {
  const char* id;   // id del catalogo (tactics_units.toml) y del manifiesto
  int b3, forma;    // luchador de B3 y su forma (Freezer final, Cell perfecto como en la rueda)
  const char *es, *en;
};
uint64_t MascaraVoces(const Modelo* const* ms, size_t n) {
  uint64_t m = 0;
  for (size_t i = 0; i < n; ++i)
    if (ms[i] && ms[i]->b3 != kFondoA && ms[i]->b3 != kFondoB && ms[i]->b3 < 64) m |= 1ull << ms[i]->b3;
  return m;
}
constexpr int kModelos = 34;   // distintos: lo que se precarga (los ids estan en tactics_manifest.toml)
constexpr Modelo kModelo[kModelos] = {{"goku", 0, 0, "Goku", "Goku"},          {"vegeta", 7, 0, "Vegeta", "Vegeta"}, {"piccolo", 11, 0, "Piccolo", "Piccolo"},
                                      {"krilin", 10, 0, "Krilin", "Krillin"}, {"a16", 28, 0, "A-16", "Android 16"}, {"freezer", 27, 3, "Freezer", "Frieza"},
                                      {"cell", 33, 2, "Cell", "Cell"},        {"a18", 30, 0, "C-18", "Android 18"}, {"gohan", 3, 0, "Gohan", "Gohan"},
                                      {"trunks", 8, 0, "Trunks", "Trunks"},   {"goten", 6, 0, "Goten", "Goten"},    {"superbuu", 35, 0, "Super Buu", "Super Buu"},
                                      {"broly", 40, 0, "Broly", "Broly"},     {"bardock", 39, 0, "Bardock", "Bardock"}, {"cooler", 38, 0, "Cooler", "Cooler"},
                                      {"recoome", 21, 0, "Recoome", "Recoome"},
                                      // H11: 14 mas, ids 0-43 del juego base (default.xex), cada uno con su modelo en data_cmn
                                      {"saibaman", 42, 0, "Saibaman", "Saibaman"}, {"tenshinhan", 12, 0, "Ten Shin Han", "Tien"}, {"nappa", 19, 0, "Nappa", "Nappa"},
                                      {"a17", 29, 0, "A-17", "Android 17"}, {"dabura", 37, 0, "Dabura", "Dabura"}, {"gero", 32, 0, "Dr. Gero", "Dr. Gero"},
                                      {"celljr", 43, 0, "Cell Jr.", "Cell Jr."}, {"satan", 14, 0, "Mr. Satán", "Mr. Satan"}, {"kaioshin", 16, 0, "Kaio-Shin", "Supreme Kai"},
                                      {"videl", 15, 0, "Videl", "Videl"}, {"buuninio", 36, 0, "Buu niño", "Kid Buu"}, {"gohankid", 2, 0, "Gohan niño", "Kid Gohan"},
                                      {"trunkskid", 9, 0, "Trunks niño", "Kid Trunks"}, {"gokukid", 1, 0, "Goku niño", "Kid Goku"},
                                      // H12: Yamcha, Raditz, Uub y Ginyu (ids 13, 18, 17, 20 del juego base)
                                      {"yamcha", 13, 0, "Yamcha", "Yamcha"}, {"raditz", 18, 0, "Raditz", "Raditz"}, {"uub", 17, 0, "Uub", "Uub"}, {"ginyu", 20, 0, "Ginyu", "Captain Ginyu"}};
// Los 8 entrenadores del torneo: el jugador (0) y 7 bots, cada uno con el retrato de una unidad (nombre = el de esa unidad)
constexpr const char* kEntrenador[kMaxJug] = {"goku", "vegeta", "freezer", "cell", "piccolo", "broly", "superbuu", "a18"};
// Alineacion de salida: las 6 primeras son el 3 contra 3 y todas el 6 contra 6 (las mismas del test de la simulacion, para comparar huellas).
struct Puesto { const char* id; int equipo, col, fila; };
constexpr Puesto kPuesto[12] = {{"goku", 0, 2, 3}, {"vegeta", 0, 3, 4}, {"piccolo", 0, 1, 4}, {"freezer", 1, 2, 2}, {"cell", 1, 3, 1}, {"a18", 1, 1, 0},
                                {"krilin", 0, 4, 5}, {"a16", 0, 0, 3}, {"a18", 0, 2, 5}, {"a16", 1, 4, 2}, {"krilin", 1, 0, 1}, {"goku", 1, 2, 0}};
// Solo pruebas (DBZ3_TEST_TACTICS_DISTINTOS=1): 12 personajes distintos en una ronda 6 contra 6 (voz propia de cada uno)
constexpr const char* kDistintos[12] = {"goku", "vegeta", "piccolo", "freezer", "cell", "a18", "krilin", "a16", "gohan", "trunks", "broly", "bardock"};
constexpr int kVidaNumero = 34;   // ticks que vive un numero de dano flotante
constexpr int kTicksHundir = 30;  // ticks que tarda una unidad derrotada en hundirse

std::atomic<bool> g_abierta{false}, g_cerrar{false}, g_lista{false}, g_panel_pedido{false}, g_fin{false};
std::atomic<int> g_ciclo{0};   // sube en cada apertura: el dialogo reinicia su estado
std::atomic<uint16_t> g_pad{0};
std::atomic<bool> g_pad_pendiente{false};

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

enum class Zona { Tablero, Banco, Tienda, Botones, Caps, Ayuda, Sin };   // Caps: panel de capsulas (0..n-1 y n = comprar); Ayuda: boton Tutorial (solo raton)
struct Hit { Zona z; int i; ImVec2 a, b; };
constexpr uint16_t kArriba = 0x0001, kAbajo = 0x0002, kIzq = 0x0004, kDer = 0x0008;
constexpr int kColsCaps = 3;   // H12: columnas del panel de capsulas (se abre sobre la tira)
constexpr int kBotones = 5;   // Comprar EXP, Actualizar, Listo, Volver al HUB, Bloquear tienda (H10)

const Modelo* ModeloDe(const std::string& id) {
  for (const Modelo& m : kModelo) if (id == m.id) return &m;
  return nullptr;
}
// H9: estrellas = transformaciones. El catalogo (tactics_units.toml `formas`) dice el {luchador B3, forma, fid} del modelo de cada estrella.
const Catalogo* g_cat = nullptr;
std::array<int, 3> FormaDe(const Modelo* m, int est) {
  if (g_cat)
    for (size_t i = 0; i < g_cat->unidades.size() && i < g_cat->formas.size(); ++i)
      if (g_cat->unidades[i].id == m->id && !g_cat->formas[i].empty()) return g_cat->Forma(i, est);
  return {m->b3, m->forma, -1};
}
// La figura de `m` con la forma de sus `est` estrellas; si esa forma aun se esta horneando (o fallo), la de la estrella anterior.
bool DibujarEstrellas(dbz3::ps2::Escena* esc, const dbz3::ps2::Camara& cam, const Modelo* m, int est, dbz3::ps2::V3 suelo, float alto, float giro, int clip, float t, float dura = 0.0f,
                      float ciclo = -1.0f, int clip_prev = -1, float t_prev = 0.0f, float mezcla = 0.0f) {
  static std::set<std::string> dicho;
  for (int e = std::clamp(est, 1, 4); e >= 1; --e) {
    const std::array<int, 3> f = FormaDe(m, e);
    if (!pasarela::nave::DibujarFiguraB3En(esc, cam, f[0], f[1], suelo, alto, giro, clip, t, f[2], dura, ciclo, clip_prev, t_prev, mezcla)) continue;
    if (dicho.insert(std::string(m->id) + "#" + std::to_string(est) + "#" + std::to_string(e)).second)
      REXLOG_INFO("dbz3 tactics: forma: {} estrella {} dibuja B3 {} forma {} fid {}{}", m->id, est, f[0], f[1], f[2], e == est ? "" : " (la de la estrella anterior: la pedida aun no esta lista)");
    return true;
  }
  return false;
}

// Altura del modelo de `m` con `est` estrellas respecto a un luchador normal (1 si aun no esta listo): la usan barras y estrellas para ir sobre la cabeza.
float AltoRel(const Modelo* m, int est) {
  for (int e = std::clamp(est, 1, 4); e >= 1; --e) {
    const std::array<int, 3> f = FormaDe(m, e);
    if (const float r = pasarela::nave::AltoRelFiguraB3(f[0], f[1], f[2]); r > 0.0f) return r;
  }
  return 1.0f;
}

// Lienzo de 1600x900 sobre la imagen del juego (16:9): la maqueta pone los paneles en los bordes de la imagen, no en el area 4:3 de los menus.
struct Lienzo {
  ImVec2 p0;
  float s = 1.0f;
  ImVec2 At(float x, float y) const { return ImVec2(p0.x + x * s, p0.y + y * s); }
};
Lienzo LienzoDe(const ImGuiIO& io) {
  const S::Imagen g = S::GuestImage(io);
  const ImVec2 sz = g.Size();
  Lienzo l;
  l.s = std::min(sz.x / 1600.0f, sz.y / 900.0f);
  l.p0 = ImVec2(g.p0.x + (sz.x - 1600.0f * l.s) * 0.5f, g.p0.y + (sz.y - 900.0f * l.s) * 0.5f);
  return l;
}
// Texto con la letra de B3 alineado a la izquierda (0), al centro (1) o a la derecha (2) de x; maxw = ancho maximo (se encoge).
constexpr float kMinTxt = 18.0f;   // H12: ningun texto por debajo de 18 (14,4 px a 720p); lo que no cabe se acorta con ".." en vez de encogerse mas
void Txt(ImDrawList* dl, const Lienzo& q, float x, float y, float size, const std::string& t0, int al, bool sel = false, bool dis = false, float maxw = 0) {
  size = std::max(size, kMinTxt);
  std::string t = t0;
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();   // la misma letra con la que se pinta (MenuTextB3)
  float w = f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x;
  if (maxw > 0 && w > maxw * q.s) {   // primero se encoge hasta el minimo; si aun no cabe, se recorta
    const float k = std::max(kMinTxt / size, maxw * q.s / w);
    size = std::max(kMinTxt, size * k);
    while (t.size() > 2 && f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x > maxw * q.s) {
      size_t n = t.size() - 1;
      while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;   // no partir un caracter UTF-8
      t.resize(n);
      if (f->CalcTextSizeA(size * q.s, 1e9f, 0, (t + "..").c_str()).x <= maxw * q.s) { t += ".."; break; }
    }
    w = f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x;
  }
  const ImVec2 p = q.At(x, y);
  S::MenuTextB3(dl, ImVec2(p.x + (al == 0 ? w * 0.5f : al == 1 ? 0.0f : -w * 0.5f), p.y), size * q.s, t.c_str(), sel, dis, 0);
}
void Caja(ImDrawList* dl, const Lienzo& q, float x0, float y0, float x1, float y1, ImU32 borde, ImU32 relleno = IM_COL32(10, 14, 30, 215), float grosor = 2.0f) {
  dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), relleno, 6 * q.s);
  dl->AddRect(q.At(x0, y0), q.At(x1, y1), borde, 6 * q.s, 0, grosor * q.s);
}
// H10: borde negro suave (dos capas translucidas) para que la estrella se vea sobre cualquier fondo, claro u oscuro
void Estrella(ImDrawList* dl, ImVec2 c, float r) {
  for (int capa = 0; capa < 3; ++capa) {
    const float k = capa == 0 ? 1.38f : capa == 1 ? 1.2f : 1.0f;
    const ImU32 col = capa == 0 ? IM_COL32(0, 0, 0, 80) : capa == 1 ? IM_COL32(0, 0, 0, 190) : IM_COL32(255, 205, 40, 255);
    ImVec2 p[10];
    for (int i = 0; i < 10; ++i) {
      const float a = -1.5708f + 0.62832f * float(i), rr = (i % 2 ? r * 0.45f : r) * k;
      p[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    for (int i = 0; i < 10; ++i) dl->AddTriangleFilled(c, p[i], p[(i + 1) % 10], col);
  }
}
// H10: texto plano (el que no usa la letra de B3) con el mismo borde negro suave; wrap > 0 = ancho de ajuste de linea
void TextoSuave(ImDrawList* dl, float fs, ImVec2 p, ImU32 col, const char* t, float wrap = 0, float alfa = 1.0f) {
  const float g = std::max(1.0f, fs * 0.07f);
  const ImU32 borde = IM_COL32(0, 0, 0, int(170.0f * alfa));
  const auto& fuentes = dbz3::launcher::ui::GetFonts();
  ImFont* f = std::strcmp(T::LangCode(), "ja") == 0 && fuentes.menu ? fuentes.menu : ImGui::GetFont();   // la letra del juego trae los kana y kanji; la de ImGui no
  static const float d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& o : d) dl->AddText(f, fs, ImVec2(p.x + o[0] * g, p.y + o[1] * g), borde, t, nullptr, wrap);
  dl->AddText(f, fs, p, col, t, nullptr, wrap);
}
void Estrellas(ImDrawList* dl, ImVec2 centro, float r, int n) {   // n estrellas en fila centrada en `centro`
  // H11: cada estrella es la bola de dragon de 1 estrella del propio juego (native_ui "icono"); sin la pieza, la estrella dibujada
  if (const S::Pic bola = S::Img("bola_estrella")) {
    const float d = r * 2.5f, paso = d * 0.92f;
    for (int i = 0; i < n; ++i) {
      const ImVec2 c(centro.x + (float(i) - float(n - 1) * 0.5f) * paso, centro.y);
      S::DrawPic(dl, bola, ImVec2(c.x - d * 0.5f, c.y - d * 0.5f), ImVec2(c.x + d * 0.5f, c.y + d * 0.5f));
    }
    return;
  }
  for (int i = 0; i < n; ++i) Estrella(dl, ImVec2(centro.x + (float(i) - float(n - 1) * 0.5f) * r * 2.1f, centro.y), r);
}
// H11: el oro es la pila de monedas de la caja de ZENIE de la Skill Shop (native_ui "caja_zenie", recorte 36,14-108,80); sin la pieza, el circulo
void Moneda(ImDrawList* dl, ImVec2 c, float r) {
  if (const S::Pic caja = S::Img("caja_zenie")) {
    const float h = r * 2.9f, w = h * 72.0f / 66.0f;
    S::DrawPic(dl, caja, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec4(36, 14, 108, 80));
    return;
  }
  dl->AddCircleFilled(c, r, IM_COL32(255, 200, 40, 255));
  dl->AddCircle(c, r, IM_COL32(150, 90, 10, 255), 0, r * 0.18f);
  dl->AddCircle(c, r * 0.6f, IM_COL32(200, 130, 20, 255), 0, r * 0.14f);
}

// Textos nuevos de H5 (tutorial y capsulas): mod center hd/tactics_textos.toml, siete idiomas; sin el fichero o sin el idioma sale el ingles.
std::string Texto(const char* clave) {
  static const std::map<std::string, std::map<std::string, std::string>> tabla = [] {
    std::map<std::string, std::map<std::string, std::string>> t;
    try {
      const toml::table doc = toml::parse_file(((dbz3::ModsRoot().parent_path() / "mod center hd") / "tactics_textos.toml").string());
      for (const auto& [k, v] : doc)
        if (const toml::table* e = v.as_table())
          for (const auto& [l, x] : *e)
            if (auto txt = x.value<std::string>()) t[std::string(k.str())][std::string(l.str())] = *txt;
    } catch (...) {
    }
    return t;
  }();
  const auto it = tabla.find(clave);
  if (it == tabla.end()) return clave;
  const auto l = it->second.find(T::LangCode());
  if (l != it->second.end()) return l->second;
  const auto en = it->second.find("en");
  return en != it->second.end() ? en->second : std::string(clave);
}
// "+25 % vida, +10 % ataque": generada de los mismos numeros de tactics_capsulas.toml, asi que nunca los contradice
std::string Describir(const CapsulaDef& c) {
  std::string r;
  const auto sumar = [&](int v, const char* clave, bool casillas) {
    if (!v) return;
    if (!r.empty()) r += ", ";
    r += (v > 0 ? "+" : "") + std::to_string(v) + (casillas ? " " : " % ") + Texto(clave);
  };
  sumar(c.vida, "ef_vida", false), sumar(c.ataque, "ef_ataque", false), sumar(c.defensa, "ef_defensa", false), sumar(c.velocidad, "ef_velocidad", false);
  sumar(c.ki_inicial, "ef_ki_inicial", false), sumar(c.ki_max, "ef_ki_max", false), sumar(c.alcance, "ef_alcance", true), sumar(c.tecnica, "ef_tecnica", false);
  return r;
}
ImU32 ColorCaps(int c) {
  static const ImU32 col[kMaxCaps] = {IM_COL32(230, 80, 80, 255),  IM_COL32(255, 150, 40, 255), IM_COL32(90, 170, 255, 255), IM_COL32(110, 230, 140, 255),
                                      IM_COL32(190, 120, 255, 255), IM_COL32(255, 230, 80, 255), IM_COL32(80, 230, 230, 255),  IM_COL32(255, 120, 190, 255),
                                      IM_COL32(220, 220, 220, 255), IM_COL32(160, 200, 80, 255), IM_COL32(200, 150, 100, 255), IM_COL32(120, 120, 255, 255)};
  return col[c % kMaxCaps];
}
// H11: botella de capsula de la Skill Shop del propio juego (native_ui "capsulas": roja, verde, amarilla y gris, 31x83 cada 36 px), centrada en `c`.
// Cada tipo de capsula usa el color que le toca; sin la pieza, el circulo de color de siempre.
void Botella(ImDrawList* dl, ImVec2 c, float alto, int tipo, bool apagada = false) {
  static constexpr int kBot[kMaxCaps] = {1, 0, 3, 2, 2, 0, 2, 1, 3, 0, 1, 2};   // vigor verde, fuerza roja, guardia gris, viento y ki amarillos...
  if (const S::Pic cap = S::Img("capsulas")) {
    const float bx = 3 + 36.0f * float(kBot[tipo % kMaxCaps]), w = alto * 31 / 83;
    S::DrawPic(dl, cap, ImVec2(c.x - w * 0.5f, c.y - alto * 0.5f), ImVec2(c.x + w * 0.5f, c.y + alto * 0.5f), ImVec4(bx, 7, bx + 31, 90),
               apagada ? IM_COL32(255, 255, 255, 80) : IM_COL32_WHITE);
    return;
  }
  dl->AddCircleFilled(c, alto * 0.25f, apagada ? IM_COL32(60, 64, 84, 255) : ColorCaps(tipo));
  dl->AddCircle(c, alto * 0.25f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
}
// H11: color del marco segun el rango (coste 1 a 4): gris azulado, verde, azul y dorado, como los rangos de cualquier autobattler
ImU32 ColorRango(int coste) {
  static const ImU32 col[4] = {IM_COL32(150, 165, 200, 255), IM_COL32(90, 205, 110, 255), IM_COL32(70, 170, 255, 255), IM_COL32(255, 190, 40, 255)};
  return col[std::clamp(coste, 1, 4) - 1];
}
// Sonidos del juego (gestor de efectos, banco/id medidos en un combate normal; los de voz son los del luchador de fondo)
constexpr int kSeImpacto[2] = {2, 2};   // los gritos de cada unidad salen de su `voz` en tactics_units.toml

// H14: arrastrar unidades con el raton (mover / soltar sobre la tienda = vender). DESACTIVADO: el piloto de pruebas no consigue una pulsacion mantenida
// y sin verlo en juego no se entrega; el codigo queda hecho para activarlo cuando se pueda probar.
constexpr bool kArrastrar = true;
constexpr bool kPrevia = true;   // vista previa de sinergias al mover una unidad cogida: probada en juego (tactics_arrastre, 1/11)

class Arena : public rex::ui::ImGuiDialog {
 public:
  explicit Arena(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}
  ~Arena() override { Bloquear(false); }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_abierta.load()) {
      Bloquear(false);
      visto_ = -1;
      return;
    }
    if (visto_ != g_ciclo.load()) Reiniciar();
    Bloquear(true);
    const double ahora = ImGui::GetTime();
    ImDrawList* fondo = ImGui::GetBackgroundDrawList();
    ImDrawList* arriba = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    const uint16_t pad = Pad();
    if (g_fin.load()) return Cerrar();   // el combate de fondo acabo (el conector ya lo solto)
    bool salir = g_cerrar.load();
    if (soltado_) {
      if ((pad & kB) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (tut_ > 0) CerrarTutorial();
        else if (menu_ && menu_pag_ == 1) menu_pag_ = 0, mc_ = 3 - (guardada_ ? 0 : 1);
        else if (menu_) salir = true;
        else if (!combate_ || !error_.empty() || demo_ || !lista_) salir = true;
        else if (mano_ >= 0 || capmano_ >= 0) mano_ = capmano_ = -1;
        else if (ahora - salir_t_ < 3.0) salir = true;
        else salir_t_ = ahora;   // primera pulsacion: avisa y espera la segunda
      }
    } else if (g_pad.load() == 0 && ahora - abierta_ > 0.25) {
      soltado_ = true;
    }
    if (salir) {
      if (!combate_ || !error_.empty()) return Cerrar();
      Salir();
    }
    if (!combate_ || !error_.empty()) {
      fondo->AddRectFilledMultiColor(ImVec2(0, 0), io.DisplaySize, IM_COL32(26, 30, 52, 255), IM_COL32(26, 30, 52, 255), IM_COL32(8, 8, 18, 255),
                                     IM_COL32(8, 8, 18, 255));
      if (S::Img("fondo_duelo")) {   // subTAC: fondo del propio juego (el cielo del Duelo, extraido de la copia del jugador) oscurecido; sin el, el degradado de arriba
        S::Backdrop(fondo, a, "fondo_duelo");
        fondo->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(6, 8, 24, 150));
      }
      S::TitleText(arriba, a, 600, 60, 640, "Dragon Tactics (experimental)");
    }
    if (!error_.empty()) return Mensaje(arriba, a, error_.c_str());
    if (!lista_) return Cargar(arriba, a);
    if (!Escenario(fondo, io)) return;   // sin la camara del ring: negro (carga, presentacion o salida)
    if (soltado_ && !saliendo_) Entrada(pad, ahora);
    if (menu_) return Menu(arriba, io);
    Tablero(fondo, arriba, ahora, a);
    if (demo_) {
      const std::string ayuda = std::string(T::T("B / Círculo: HUB  ·  X / Tab: 3v3 o 6v6  ·  V / Y: velocidad (x", "B / Circle: HUB  ·  X / Tab: 3v3 or 6v6  ·  V / Y: speed (x")) +
                                std::to_string(vel_) + T::T(")  ·  R / A: repetir  ·  P: pausa", ")  ·  R / A: repeat  ·  P: pause");
      S::MenuText(arriba, a.At(600, 860), 26 * a.s, ayuda.c_str(), false, false);
    } else {
      Interfaz(arriba, io, ahora);
      LogHits(io);
      if (++ui_cuadros_ % 300 == 0)
        REXLOG_INFO("dbz3 tactics: interfaz: {} vertices en la lista de primer plano; banquillo 3D: {} miniaturas, {} triangulos, CPU {:.2f} ms/cuadro", arriba->VtxBuffer.Size, minis_, esc_b_.Cuantos(), banco_ms_);
    }
  }

 private:
  void Reiniciar() {
    visto_ = g_ciclo.load();
    abierta_ = ImGui::GetTime();
    t0_ = Reloj::now();
    soltado_ = false, lista_ = false;
    error_.clear();
    siguiente_ = 0;
    hechas_ = 0;
    combate_ = saliendo_ = false;
    g_fin = false;
    for (bool& h : hecha_) h = false;
    ultimo_ = 0;
    pausa_ = false;
    vel_ = 1;
    sim_ = nullptr;
    demo_sim_.reset();
    partida_.reset();
    guardada_.reset();
    menu_ = fin_partida_ = false;
    mano_ = capmano_ = -1;
    tut_ = 0;
    tut_visto_ = false;
    se_ = MezclaSe();
    salir_t_ = -10;
    const auto num = [](const char* n, long def) { const char* v = std::getenv(n); return v ? std::atol(v) : def; };
    demo_ = std::getenv("DBZ3_TEST_TACTICS_VEL") || std::getenv("DBZ3_TEST_TACTICS_LADO");
    auto_rondas_ = int(num("DBZ3_TEST_TACTICS_AUTO", 0));
    semilla_ = uint64_t(num("DBZ3_TEST_TACTICS_SEMILLA", demo_ || auto_rondas_ ? 2024 : long(std::chrono::system_clock::now().time_since_epoch().count() & 0x7fffffff)));
    vels_.clear();
    if (const char* v = std::getenv("DBZ3_TEST_TACTICS_VEL"))
      for (; *v; ++v)
        if (*v == '1' || *v == '2' || *v == '4') vels_.push_back(*v - '0');
    vel_i_ = 0;
    const std::string datos = (dbz3::ModsRoot().parent_path() / "mod center hd").string();
    std::string err;
    if (!CargarCatalogo(datos + "/tactics_units.toml", datos + "/tactics_manifest.toml", &cat_, &err) ||
        !CargarEconomia(datos + "/tactics_economia.toml", cat_, &eco_, &err)) {
      error_ = std::string(T::T("No se pudieron leer los datos de Dragon Tactics: ", "Could not read the Dragon Tactics data: ")) + err;
      REXLOG_ERROR("dbz3 tactics: {}", error_);
      return;
    }
    g_cat = &cat_;
    calidad_base_ = eco_.calidad_bots;
    CargarOpc();
    vel_ = opc_.vel;
    if (demo_) Colocar(num("DBZ3_TEST_TACTICS_LADO", 3) == 6 ? 6 : 3, semilla_);
    else IniciarOMenu();
  }
  // Pruebas (tools/pruebas.py): DBZ3_TEST_TACTICS_PRUEBA=1 guarda en hub_tactics_prueba.toml (nunca toca la partida del jugador) y no ofrece
  // "Continuar" salvo con DBZ3_TEST_TACTICS_MENU=1.
  static bool EnPrueba() { return std::getenv("DBZ3_TEST_TACTICS_PRUEBA") || std::getenv("DBZ3_TEST_TACTICS_AUTO") || std::getenv("DBZ3_TEST_TACTICS_SEMILLA"); }
  static std::filesystem::path RutaGuardado() { return dbz3::settings::UserDataRoot() / (EnPrueba() ? "hub_tactics_prueba.toml" : "hub_tactics.toml"); }
  static std::filesystem::path RutaOpc() { return dbz3::settings::UserDataRoot() / (EnPrueba() ? "hub_tactics_opciones_prueba.toml" : "hub_tactics_opciones.toml"); }
  void CargarOpc() {
    opc_ = Opc{};
    try {
      const toml::table doc = toml::parse_file(RutaOpc().string());
      opc_.reloj = std::clamp(int(doc["reloj"].value_or<int64_t>(0)), 0, 300);
      opc_.vel = doc["velocidad"].value_or<int64_t>(1) == 4 ? 4 : doc["velocidad"].value_or<int64_t>(1) == 2 ? 2 : 1;
      opc_.dif = std::clamp(int(doc["dificultad"].value_or<int64_t>(1)), 0, 2);
      opc_.sonido = doc["sonido"].value_or(true);
    } catch (...) {
    }
    if (const char* r = std::getenv("DBZ3_TEST_TACTICS_RELOJ")) opc_.reloj = std::clamp(std::atoi(r), 0, 300);   // solo pruebas
  }
  void GuardarOpc() const {
    std::error_code ec;
    std::filesystem::create_directories(RutaOpc().parent_path(), ec);
    std::ofstream f(RutaOpc());
    f << "# Dragon Tactics: opciones del menu del modo (reloj en segundos, 0 = apagado; velocidad 1, 2 o 4; dificultad 0 facil, 1 normal, 2 dificil)\n";
    f << "reloj = " << opc_.reloj << "\nvelocidad = " << opc_.vel << "\ndificultad = " << opc_.dif << "\nsonido = " << (opc_.sonido ? "true" : "false") << "\n";
  }
  // dificultad: la calidad de decision de los bots 1..7 se mueve -25 / 0 / +15 puntos (nunca pasa de 0-100)
  void AplicarDificultad() {
    eco_.calidad_bots = calidad_base_;
    for (int& q : eco_.calidad_bots) q = std::clamp(q + (opc_.dif == 0 ? -25 : opc_.dif == 2 ? 15 : 0), 0, 100);
  }
  void CambiarOpc(int i, int d) {
    static const int relojes[] = {0, 30, 60, 90, 120};
    const auto ciclo = [&](int v, const int* tabla, int n) {
      int k = 0;
      for (int j = 0; j < n; ++j) if (tabla[j] == v) k = j;
      return tabla[(k + d + n) % n];
    };
    static const int vels[] = {1, 2, 4}, difs[] = {0, 1, 2};
    if (i == 0) opc_.reloj = ciclo(opc_.reloj, relojes, 5);
    else if (i == 1) opc_.vel = ciclo(opc_.vel, vels, 3), vel_ = opc_.vel;
    else if (i == 2) opc_.sonido = !opc_.sonido;
    else if (i == 3) opc_.dif = ciclo(opc_.dif, difs, 3);
    else return;
    GuardarOpc();
    REXLOG_INFO("dbz3 tactics: opcion {} -> reloj {} s, velocidad x{}, sonido {}, dificultad {}", i, opc_.reloj, opc_.vel, opc_.sonido, opc_.dif);
  }
  // Menu propio: Continuar (si hay partida guardada), Jugar, Como se juega, Opciones y Volver. Las pruebas lo saltan salvo con DBZ3_TEST_TACTICS_MENU=1.
  void IniciarOMenu() {
    menu_ = false;
    menu_pag_ = 0;
    guardada_.reset();
    mc_ = 0;
    if (EnPrueba() && !std::getenv("DBZ3_TEST_TACTICS_MENU")) return NuevaPartida(semilla_);
    menu_ = true;
    std::error_code ec;
    if (std::filesystem::exists(RutaGuardado(), ec) || std::filesystem::exists(RutaGuardado().string() + ".bak", ec)) {
      std::string e;
      if (CargarArchivo(RutaGuardado().string(), cat_, eco_, &guardada_, &e)) {
        REXLOG_INFO("dbz3 tactics: menu de inicio: hay una partida guardada (ronda {}, {} vivos)", guardada_->Ronda(), guardada_->Vivos());
        return;
      }
      REXLOG_WARN("dbz3 tactics: la partida guardada no se puede usar ({}): partida nueva", e);
      aviso_ = T::T("Tu partida guardada es de una versión anterior y no se puede cargar. Empieza una nueva.", "Your saved game is from an older version and can't be loaded. Starting a new one.");
    }
    REXLOG_INFO("dbz3 tactics: menu de inicio: sin partida guardada");
  }
  // ids de las filas del menu principal: 0 Continuar, 1 Jugar, 2 Como se juega, 3 Opciones, 4 Volver
  std::vector<int> FilasMenu() const {
    std::vector<int> r;
    if (guardada_) r.push_back(0);
    for (int i = 1; i <= 4; ++i) r.push_back(i);
    return r;
  }
  void ElegirMenu(int id) {
    if (id == 0 && guardada_) {
      partida_.emplace(std::move(*guardada_));
      guardada_.reset();
      menu_ = false;
      fin_partida_ = false;
      REXLOG_INFO("dbz3 tactics: partida continuada (ronda {}, huella {:016x})", partida_->Ronda(), partida_->Huella());
      EntrarPreparacion(false);
    } else if (id == 1) {
      menu_ = false;
      guardada_.reset();
      AplicarDificultad();
      if (!aviso_.empty()) aviso_t_ = ImGui::GetTime(), aviso_dura_ = 9.0;
      NuevaPartida(semilla_);
    } else if (id == 2) {
      AbrirTutorial(true);
    } else if (id == 3) {
      menu_pag_ = 1, mc_ = 0;
      REXLOG_INFO("dbz3 tactics: menu: opciones");
    } else if (id == 4) {
      g_cerrar = true;
    }
  }
  void GuardarPartida() {
    std::string e;
    const std::string t = partida_->Guardar();
    if (t.empty() || !GuardarArchivo(RutaGuardado().string(), t, &e)) REXLOG_WARN("dbz3 tactics: no se pudo guardar la partida: {}", e);
    else REXLOG_INFO("dbz3 tactics: partida guardada (ronda {}, huella {:016x})", partida_->Ronda(), partida_->Huella());
  }
  // Presentacion de las unidades de la simulacion en curso (sim_): modelo, estrellas y casilla de salida de cada una
  void IniciarPres(const std::vector<const Modelo*>& ms, const std::vector<int>& est) {
    if (combate_) dbz3::combate::CargarVoces(MascaraVoces(ms.data(), ms.size()));
    cuadros_ = 0;
    media_ = 0;
    cc_ = lentos_ = 0;
    dt_suma_ = dt_peor_ = 0;
    acc_ = 0;
    leidos_ = 0;
    fin_t_ = -1, caido_ = nullptr;
    p_.clear();
    flota_.clear(), fx_.clear();
    for (size_t i = 0; i < ms.size(); ++i) {
      const sim::Unidad& u = sim_->Unidades()[i];
      Pres p;
      p.m = ms[i];
      p.est = est[i];
      Pos(u.casilla / sim::kAncho, u.casilla % sim::kAncho, p.x, p.z);
      p.giro = u.equipo ? 0.0f : 3.14159f;
      p_.push_back(p);
    }
  }
  // Ronda de demostracion (solo pruebas): `lado` unidades por bando en las casillas de salida y la simulacion desde cero con esa semilla
  void Colocar(int lado, uint64_t semilla) {
    lado_ = lado;
    semilla_ = semilla;
    if (!vels_.empty()) vel_ = vels_[size_t(vel_i_)];
    demo_sim_.emplace(semilla);
    sim_ = &*demo_sim_;
    std::vector<const Modelo*> ms;
    for (int i = 0; i < 2 * lado; ++i) {
      const Puesto& pu = kPuesto[i];
      static const bool distintos = std::getenv("DBZ3_TEST_TACTICS_DISTINTOS") != nullptr;
      // H13 (solo pruebas): DISTINTOS puede ser una lista de ids separados por comas (los 12 de la ronda 6 contra 6) para fotografiar unas unidades concretas
      static const std::vector<std::string> lista = [] {
        std::vector<std::string> v;
        std::string t = std::getenv("DBZ3_TEST_TACTICS_DISTINTOS") ? std::getenv("DBZ3_TEST_TACTICS_DISTINTOS") : "";
        for (size_t p0 = 0; !t.empty() && p0 <= t.size();) { const size_t c = std::min(t.find(',', p0), t.size()); if (c > p0) v.push_back(t.substr(p0, c - p0)); p0 = c + 1; }
        return v.size() >= 12 ? v : std::vector<std::string>();
      }();
      const char* id = !lista.empty() && lado == 6 ? lista[size_t(i)].c_str() : distintos && lado == 6 ? kDistintos[i] : pu.id;
      const sim::DefUnidad* d = cat_.Buscar(id);
      static const bool ki_lleno = std::getenv("DBZ3_TEST_TACTICS_KI") != nullptr;   // solo pruebas: todos empiezan con el ki lleno (la tecnica sale pronto)
      sim::DefUnidad copia = d ? *d : sim::DefUnidad{};
      if (ki_lleno) copia.ki_inicial = 100;
      if (!d || sim_->Anadir(copia, pu.equipo, {pu.col, pu.fila}) < 0) {
        error_ = std::string("Dragon Tactics: ") + id;
        REXLOG_ERROR("dbz3 tactics: no se pudo colocar {}", id);
        return;
      }
      ms.push_back(ModeloDe(id));
    }
    IniciarPres(ms, std::vector<int>(ms.size(), 1));
    REXLOG_INFO("dbz3 tactics: ronda {}v{} semilla {} a x{}", lado, lado, semilla, vel_);
  }
  void NuevaPartida(uint64_t semilla) {
    semilla_ = semilla;
    partida_.emplace(cat_, eco_, semilla);
    sim_ = nullptr;
    p_.clear();
    flota_.clear(), fx_.clear();
    fin_t_ = -1, caido_ = nullptr;
    fin_partida_ = false;
    REXLOG_INFO("dbz3 tactics: partida nueva, semilla {}", semilla);
    EntrarPreparacion();
  }
  void EntrarPreparacion(bool guardar = true) {
    sim_ = nullptr;
    p_.clear();
    flota_.clear(), fx_.clear();
    fin_t_ = -1, caido_ = nullptr;
    mano_ = -1;
    zc_ = Zona::Tienda, ic_ = 0;
    ficha_ver_ = false;
    prep_t_ = -1;
    auto_jugado_ = false;
    hits_dichos_ = false;
    capmano_ = -1;
    est_ok_ = false;
    destellos_.clear();
    prep_resto_ = float(opc_.reloj);
    espia_ = -1;
    if (const char* ids = std::getenv("DBZ3_TEST_TACTICS_PUESTO"); ids && partida_->Ronda() == 1) {   // solo pruebas: unidades de 1 estrella ya puestas en el tablero (ids separados por comas)
      std::string resto = ids;
      while (!resto.empty()) {
        const size_t c = resto.find(',');
        const std::string id = resto.substr(0, c);
        resto = c == std::string::npos ? std::string() : resto.substr(c + 1);
        for (size_t u = 0; u < eco_.unidades.size(); ++u) if (eco_.unidades[u].id == id) partida_->PonerPrueba(0, int(u), 1);
      }
    }
    if (guardar) GuardarPartida();
    AbrirTutorial(false);
    const Jugador& j = partida_->J(0);
    const Duelo& d = partida_->Duelos()[size_t(partida_->DueloHumano())];
    REXLOG_INFO("dbz3 tactics: preparacion ronda {}: oro {}, nivel {}, vida {}, {} vivos, rival {}{} (vida {}), tienda {} {} {} {} {}", partida_->Ronda(), j.oro, j.nivel, j.vida,
                partida_->Vivos(), d.copia ? "copia de " : "", d.b, partida_->J(d.b).vida, j.tienda[0], j.tienda[1], j.tienda[2], j.tienda[3], j.tienda[4]);
  }
  // H9: (1) hornea en segundo plano, una a una, las formas de las estrellas 2..4; (2) destello al subir una pieza de estrella;
  // (3) pruebas: DBZ3_TEST_TACTICS_ESTRELLAS=N pone una unidad de 1 estrella (DBZ3_TEST_TACTICS_ESTRELLAS_UNIDAD, por defecto goku), espera a
  // que este lista la forma de N estrellas y la sube a N (antes y despues se capturan desde pruebas.py; el log trae el fid de cada forma).
  void ActualizarFormas(double ahora, bool prep) {
    if (lista_ && g_cat)
      for (int pasos = 0; pasos < 8 && forma_sig_ < size_t(kModelos) * 3; ++pasos) {
        const std::array<int, 3> f = FormaDe(&kModelo[forma_sig_ / 3], int(forma_sig_ % 3) + 2);
        if (pasarela::nave::EstadoFiguraB3(f[0], f[1], f[2]) == 1) break;
        if (++forma_sig_ == size_t(kModelos) * 3) REXLOG_INFO("dbz3 tactics: formas de las estrellas 2 a 4 preparadas ({:.1f} s desde que se abrio)", Seg());
      }
    if (!partida_ || !prep) return;
    static const int prueba = std::getenv("DBZ3_TEST_TACTICS_ESTRELLAS") ? std::clamp(std::atoi(std::getenv("DBZ3_TEST_TACTICS_ESTRELLAS")), 0, 4) : 0;
    if (prueba >= 2 && partida_->Ronda() == 1) {
      const char* id = std::getenv("DBZ3_TEST_TACTICS_ESTRELLAS_UNIDAD") ? std::getenv("DBZ3_TEST_TACTICS_ESTRELLAS_UNIDAD") : "goku";
      int u = -1;
      for (size_t i = 0; i < eco_.unidades.size(); ++i) if (eco_.unidades[i].id == id) u = int(i);
      const Modelo* m = ModeloDe(id);
      if (u >= 0 && m && prueba_est_ == 0) {
        prueba_slot_ = partida_->PonerPrueba(0, u, 1);
        prueba_est_ = 1, prueba_t_ = ahora;
        REXLOG_INFO("dbz3 tactics: prueba estrellas: {} 1 estrella (casilla {}), forma B3 {} fid {}", id, prueba_slot_, FormaDe(m, 1)[1], FormaDe(m, 1)[2]);
      } else if (u >= 0 && m && prueba_est_ == 1 && prueba_slot_ >= 0 && ahora - prueba_t_ > 8.0) {
        const std::array<int, 3> f = FormaDe(m, prueba);
        const int estado = pasarela::nave::EstadoFiguraB3(f[0], f[1], f[2]);   // 1 = horneandose (esta llamada la pide si falta)
        if (estado != 1 || ahora - prueba_t_ > 240.0) {
          prueba_est_ = 2;
          partida_->EstrellasPrueba(0, prueba_slot_, prueba);
          REXLOG_INFO("dbz3 tactics: prueba estrellas: {} {} estrellas, forma B3 {} fid {} (estado {})", id, prueba, f[1], f[2], estado);
        }
      }
    }
    const Jugador& j = partida_->J(0);
    for (int s = 0; s < kSlots; ++s) {
      const int e = j.pz[s].hay() ? j.pz[s].estrellas : 0, u = j.pz[s].hay() ? j.pz[s].unidad : -1;
      if (est_ok_ && s < kBanco && est_vista_[s] == 0 && e > 0 && !sin_destello_compra_) destellos_.push_back({s, ahora});   // H14: pieza nueva en el banquillo (compra): mismo destello
      if (est_ok_ && est_vista_[s] > 0 && u == uni_vista_[s] && e > est_vista_[s]) {
        destellos_.push_back({s, ahora});
        REXLOG_INFO("dbz3 tactics: destello casilla {}: {} sube a {} estrellas", s, u >= 0 ? eco_.unidades[size_t(u)].id : "?", e);
      }
      est_vista_[s] = e, uni_vista_[s] = u;
    }
    est_ok_ = true;
  }
  // Destello de las piezas que acaban de subir (1 s): anillo que crece sobre la casilla del tablero o velo dorado sobre la celda del banquillo
  void PintarDestellos(ImDrawList* arriba, ImDrawList* dl, const Lienzo& q, double ahora) {
    for (size_t i = 0; i < destellos_.size();) {
      const float k = float((ahora - destellos_[i].t0) / 1.0);
      if (k >= 1.0f) { destellos_.erase(destellos_.begin() + long(i)); continue; }
      const int s = destellos_[i].slot;
      const int a = int(255 * (1.0f - k));
      const ImU32 col = destellos_[i].tipo ? IM_COL32(255, 110, 100, a) : IM_COL32(255, 235, 90, a);
      const ImU32 relleno = destellos_[i].tipo ? IM_COL32(255, 140, 120, a / 3) : IM_COL32(255, 245, 160, a / 3);
      if (s >= kBanco) {
        float x, z, esc;
        ImVec2 o;
        Pos(3 + (s - kBanco) / 6, (s - kBanco) % 6, x, z);
        if (Proy(x, kAltoFigura * 0.5f, z, o, esc)) {
          arriba->AddCircle(o, (0.25f + 0.9f * k) * esc, col, 0, 4.0f);
          arriba->AddCircleFilled(o, (0.9f * (1.0f - k)) * esc * 0.5f, relleno);
        }
      } else {
        const float x0 = 400 + 100.0f * float(s), y0 = 714;
        dl->AddRectFilled(q.At(x0, y0), q.At(x0 + 94, y0 + 62), relleno, 6 * q.s);
        dl->AddRect(q.At(x0, y0), q.At(x0 + 94, y0 + 62), col, 6 * q.s, 0, 2.5f * q.s);
      }
      ++i;
    }
  }
  // Tutorial de 4 pantallas: la primera vez que se juega (marca en hub_tactics_tutorial.visto), o a peticion (boton, Select o T). En las
  // pruebas no sale (bloquearia al mando y al raton) salvo con DBZ3_TEST_TACTICS_TUTORIAL=1.
  static std::filesystem::path RutaTutorial() { return dbz3::settings::UserDataRoot() / "hub_tactics_tutorial.visto"; }
  void AbrirTutorial(bool pedido) {
    if (!pedido) {
      if (tut_visto_) return;
      tut_visto_ = true;
      std::error_code ec;
      if (!std::getenv("DBZ3_TEST_TACTICS_TUTORIAL") && (EnPrueba() || std::filesystem::exists(RutaTutorial(), ec))) return;
    }
    tut_ = 1;
    REXLOG_INFO("dbz3 tactics: tutorial abierto{}", pedido ? " (a peticion)" : "");
  }
  void CerrarTutorial() {
    tut_ = 0;
    if (!EnPrueba()) std::ofstream(RutaTutorial(), std::ios::binary) << "1";
    REXLOG_INFO("dbz3 tactics: tutorial cerrado");
  }
  void IniciarCombate() {
    GuardarPartida();   // antes del combate: si se cierra el juego aqui, "Continuar" vuelve a esta preparacion
    partida_->Listo();
    sim_ = &partida_->Combate();
    std::vector<const Modelo*> ms;
    std::vector<int> est;
    for (const Combatiente& c : partida_->Combatientes()) {
      ms.push_back(ModeloDe(eco_.unidades[size_t(c.unidad)].id));
      est.push_back(c.estrellas);
    }
    IniciarPres(ms, est);
    mano_ = capmano_ = -1;
    aplicado_ = false;
    transicion_t0_ = ImGui::GetTime();   // banda de "COMBATE" al pasar de preparacion a combate
    Sonar(ImGui::GetTime(), PrioSe::Tecnica, 0.3, 0, 3);   // el "aceptar" del sistema, como al empezar un combate del juego
    const auto rival = std::count_if(partida_->Combatientes().begin(), partida_->Combatientes().end(), [](const Combatiente& c) { return c.jugador == 1; });
    REXLOG_INFO("dbz3 tactics: combate ronda {}: {} contra {} unidades a x{}", partida_->Ronda(), ms.size() - size_t(rival), rival, vel_);
  }
  void Cerrar() {
    dbz3::combate::CargarVoces(0);   // sueltas las ranuras de voz que quedaran
    Bloquear(false);
    g_lista = false;
    g_cerrar = false;
    g_abierta = false;   // el HUB vuelve a dibujarse (busy)
    REXLOG_INFO("dbz3 tactics: arena cerrada");
  }
  void Bloquear(bool si) {
    if (si == bloqueado_) return;
    if (auto* inp = Input()) {
      if (si) inp->AddUIInputBlocker();
      else inp->RemoveUIInputBlocker();
      bloqueado_ = si;
    }
  }
  // botones recien pulsados del mando 1 (lectura en el hilo de la interfaz, como modos.cpp); la palanca cuenta como cruceta
  uint16_t Pad() {
    static int pendiente_n = 0;
    if (g_pad_pendiente.load()) { if (EnPrueba() && ++pendiente_n % 300 == 0) REXLOG_INFO("dbz3 tactics: pad: la lectura del mando lleva {} cuadros pendiente", pendiente_n); }
    else pendiente_n = 0;
    if (!g_pad_pendiente.exchange(true)) {
      dbz3::launcher::DeferUi([] {
        uint16_t b = 0;
        if (auto* inp = Input()) {
          rex::input::X_INPUT_STATE s = {};
          if (inp->GetStateForUI(0, &s) == 0) {
            b = static_cast<uint16_t>(s.gamepad.buttons);
            const int lx = int16_t(s.gamepad.thumb_lx), ly = int16_t(s.gamepad.thumb_ly);
            if (ly > 16000) b |= kArriba;
            if (ly < -16000) b |= kAbajo;
            if (lx > 16000) b |= kDer;
            if (lx < -16000) b |= kIzq;
          }
        }
        g_pad = b;
        g_pad_pendiente = false;
      });
    }
    const uint16_t b = g_pad.load(), nuevo = uint16_t(b & ~ultimo_);
    ultimo_ = b;
    return nuevo;
  }

  // Precarga: un modelo nuevo por cuadro; los ya pedidos se miran en cada cuadro
  // (el modelo y los clips de B3 se generan en otro proceso). 0 = fallo -> mensaje; 2 = lista.
  void Cargar(ImDrawList* dl, const S::Area& a) {
    if (siguiente_ < kModelos) ++siguiente_;
    for (int i = 0; i < siguiente_ && error_.empty(); ++i) {
      if (hecha_[i]) continue;
      const std::array<int, 3> f1 = FormaDe(&kModelo[i], 1);
      const int e = pasarela::nave::EstadoFiguraB3(f1[0], f1[1], f1[2]);
      if (e == 2) {
        hecha_[i] = true;
        ++hechas_;
        REXLOG_INFO("dbz3 tactics: figura {} lista ({:.1f} s desde que se abrio)", i, Seg());
      } else if (e == 0) {
        error_ = std::string(T::T("No se pudo preparar el modelo de ", "Could not prepare the model of ")) + T::T(kModelo[i].es, kModelo[i].en) + ". " +
                 T::T("Faltan datos de Budokai 3 HD: comprueba que data_cmn.afs está en la carpeta del juego y que el lanzador encuentra Python.",
                      "Budokai 3 HD data is missing: check that data_cmn.afs is in the game folder and that the launcher can find Python.");
        REXLOG_ERROR("dbz3 tactics: {}", error_);
      }
    }
    if (error_.empty() && Seg() > kTopeCarga) {
      error_ = T::T("La preparación de las figuras tarda demasiado. Vuelve a intentarlo más tarde.", "Preparing the figures is taking too long. Try again later.");
      REXLOG_ERROR("dbz3 tactics: {}", error_);
    }
    if (!error_.empty()) return Mensaje(dl, a, error_.c_str());
    if (hechas_ == kModelos) {
      lista_ = true;
      REXLOG_INFO("dbz3 tactics: precarga de {} figuras en {:.1f} s", kModelos, Seg());
      Pedir();
      return;
    }
    const float x0 = 300, x1 = 900, y = 470, p = float(hechas_) / float(kModelos);
    S::MenuText(dl, a.At(600, 400), 34 * a.s,
                (std::string(T::T("Preparando figuras", "Preparing figures")) + " " + std::to_string(hechas_) + "/" + std::to_string(kModelos)).c_str(), false, false);
    dl->AddRectFilled(a.At(x0, y), a.At(x1, y + 22), IM_COL32(0, 0, 0, 170), 6 * a.s);
    dl->AddRectFilled(a.At(x0, y), a.At(x0 + (x1 - x0) * p, y + 22), IM_COL32(255, 160, 30, 255), 6 * a.s);
    dl->AddRect(a.At(x0, y), a.At(x1, y + 22), IM_COL32(255, 255, 255, 200), 6 * a.s, 0, 2 * a.s);
    S::MenuText(dl, a.At(600, 860), 26 * a.s, T::T("B / Círculo: cancelar", "B / Circle: cancel"), false, false);
  }
  void Mensaje(ImDrawList* dl, const S::Area& a, const char* texto) {
    const ImVec2 p0 = a.At(150, 330), p1 = a.At(1050, 560);
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 200), 12 * a.s);
    dl->AddRect(p0, p1, IM_COL32(255, 160, 30, 255), 12 * a.s, 0, 3 * a.s);
    const float fs = 28 * a.s;
    TextoSuave(dl, fs, ImVec2(p0.x + 24 * a.s, p0.y + 24 * a.s), IM_COL32_WHITE, texto, p1.x - p0.x - 48 * a.s);
    S::MenuText(dl, a.At(600, 610), 26 * a.s, T::T("B / Círculo: volver al HUB", "B / Circle: back to the HUB"), false, false);
  }

  // Combate nativo de fondo en el Torneo: Goku y Vegeta (ocultos), sin tiempo ni marcador, quietos (muneco), modo 5 (directo al READY).
  // La camara la fija una escena de cinematica de un solo paso interminable, en el marco de los luchadores (origen = su punto medio,
  // x hacia el lado 2, z perpendicular): el tablero vive en ese mismo marco, asi que cae sobre el ring en cualquier posicion de salida.
  void Pedir() {
    dbz3::hub::BattleModeNext(5);
    if (!dbz3::hub::RequestBattleEx(0, 7, kEscenarioTorneo, true, -1, 0, 0, 0, 0)) {
      error_ = T::T("No se pudo preparar el escenario del Torneo.", "Could not set up the Tournament stage.");
      REXLOG_ERROR("dbz3 tactics: el conector no acepto el combate");
      return;
    }
    combate_ = true;
    {   // voces: las de todos los modelos (las ranuras libres son 11; cada ronda deja pedidas solo las suyas)
      const Modelo* todos[kModelos];
      for (int i = 0; i < kModelos; ++i) todos[i] = &kModelo[i];
      dbz3::combate::CargarVoces(MascaraVoces(todos, kModelos));
    }
    dbz3::combate::SetMuneco(1);
    dbz3::combate::OcultarHud(true);
    PonerCamara();
    dbz3::pasarela::Reglas reglas;
    reglas.capsulas = false;
    dbz3::pasarela::VigilarCombate("tactics", reglas, [](const dbz3::pasarela::Fin&) {
      dbz3::combate::SetMuneco(-1);
      dbz3::combate::OcultarHud(false);
      dbz3::cinematica::Cancelar();
      g_fin = true;
    });
    REXLOG_INFO("dbz3 tactics: combate de fondo pedido (escenario {})", kEscenarioTorneo);
  }
  static void PonerCamara() {
    dbz3::cinematica::Paso p;
    p.segundos = 1.0e6f;
    p.rel = p.fov_acerca = p.estilo_b1 = p.sin_ayuda = true;
    p.cam_ojo = {{0, 0, kCamAlto, kCamAtras}};
    p.cam_obj = {{0, 0, 0, kCamObjZ}};
    p.cam_fov = {{0, 0.785f}};   // el fov fijo de B3: la camara de la escena y la del dibujo coinciden
    dbz3::cinematica::Escena e;
    e.pasos.push_back(std::move(p));
    dbz3::cinematica::Empezar(std::move(e));
  }
  // B: el rival queda fuera del ring y se salta la pose de victoria: el combate acaba, el conector vuelve al menu principal y el HUB reaparece
  void Salir() {
    if (saliendo_) return;
    saliendo_ = true;
    dbz3::combate::CargarVoces(0);   // el combate de fondo aun corre unos segundos: ahi se sueltan las ranuras de voz (combate.cpp)
    uint8_t* base = rex::Runtime::instance()->virtual_membase();
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (base && mgr && dbz3::hub::OwnBattle()) REX_STORE_U8(mgr + kAvisoFuera, 1);   // el aviso de fuera del ring del rival (retos de pasarela.cpp): el combate acaba
    dbz3::cinematica::PedirResultadoRapido();
    REXLOG_INFO("dbz3 tactics: saliendo (el combate de fondo acaba)");
  }
  // Dibuja el fondo y fija `cam_` y el marco del tablero si el combate esta en la pelea con la camara del ring puesta; false = negro.
  bool Escenario(ImDrawList* fondo, const ImGuiIO& io) {
    auto negro = [&] {
      fondo->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32_BLACK);
      return false;
    };
    uint8_t* base = rex::Runtime::instance()->virtual_membase();
    const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
    if (!mgr || !dbz3::hub::OwnBattle()) return negro();
    const uint32_t estado = REX_LOAD_U32(mgr + kEstado);
    for (uint32_t k = 0; k < 2 && estado >= 2; ++k) {   // luchadores ocultos (los flags del cuerpo A y B, como el relevo del Tag Team)
      REX_STORE_U8(mgr + kX0 + kXPaso * k + 3261, 1);
      REX_STORE_U8(mgr + kX0 + kXPaso * k + 4925, 1);
    }
    if (estado != 4 || saliendo_) return negro();
    if (!dbz3::cinematica::Activa()) PonerCamara();   // START la salto: se vuelve a poner
    float ojo[3], obj[3], giro;
    if (!dbz3::cinematica::CamaraEscena(ojo, obj, &giro)) return negro();
    float p[2][3];
    for (uint32_t k = 0; k < 2; ++k)
      for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t u = REX_LOAD_U32(mgr + kX0 + kXPaso * k + kPos + 4 * i);
        std::memcpy(&p[k][i], &u, 4);
      }
    suelo_ = (p[0][1] + p[1][1]) * 0.5f;
    centro_[0] = (p[0][0] + p[1][0]) * 0.5f, centro_[1] = (p[0][2] + p[1][2]) * 0.5f;
    const float dx = p[1][0] - p[0][0], dz = p[1][2] - p[0][2], l = std::sqrt(dx * dx + dz * dz);
    eje_[0] = l > 1e-3f ? dx / l : 1.0f, eje_[1] = l > 1e-3f ? dz / l : 0.0f;
    cam_ = dbz3::ps2::CamaraCombate(base, io.DisplaySize);   // centro y fov; la vista se calcula de ojo y objetivo (exactos)
    // vista: ejes derecha / arriba / atras (Camara::P: x = m0 X + m4 Y + m8 Z + m12)
    float at[3] = {ojo[0] - obj[0], ojo[1] - obj[1], ojo[2] - obj[2]};
    const float la = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    for (float& v : at) v /= la;
    const float lr = std::sqrt(at[2] * at[2] + at[0] * at[0]);
    const float re[3] = {at[2] / lr, 0, -at[0] / lr}, ar[3] = {at[1] * re[2] - at[2] * re[1], at[2] * re[0] - at[0] * re[2], at[0] * re[1] - at[1] * re[0]};
    const float* eje3[3] = {re, ar, at};
    for (int c = 0; c < 3; ++c) {
      cam_.m[c] = eje3[c][0], cam_.m[4 + c] = eje3[c][1], cam_.m[8 + c] = eje3[c][2];
      cam_.m[12 + c] = -(eje3[c][0] * ojo[0] + eje3[c][1] * ojo[1] + eje3[c][2] * ojo[2]);
    }
    cam_.m[3] = cam_.m[7] = cam_.m[11] = 0, cam_.m[15] = 1;
    cam_.za = cam_.zb = 0;   // sin prueba de profundidad: el ring queda debajo de las figuras
    if (static bool dicho = false; !dicho) {
      dicho = true;
      REXLOG_INFO("dbz3 tactics: tablero sobre el escenario (centro {:.1f} {:.1f} {:.1f}, ojo {:.1f} {:.1f} {:.1f})", centro_[0], suelo_, centro_[1], ojo[0], ojo[1], ojo[2]);
    }
    g_lista = true;
    return true;
  }
  // punto del tablero (x, y, z en unidades del tablero) -> mundo de B3 (marco de los luchadores)
  dbz3::ps2::V3 Mundo(float x, float y, float z) const {
    x *= kEscala, y *= kEscala, z *= kEscala;
    return {centro_[0] + eje_[0] * x - eje_[1] * z, suelo_ + y, centro_[1] + eje_[1] * x + eje_[0] * z};
  }
  // proyeccion de un punto del tablero: pixeles y px por unidad del tablero a esa distancia
  bool Proy(float x, float y, float z, ImVec2& o, float& k) const {
    if (!cam_.P(Mundo(x, y, z), o, k)) return false;
    k *= kEscala;
    return true;
  }
  // centro de la casilla (fila, columna) en el mundo
  static void Pos(int r, int c, float& x, float& z) {
    x = (float(c) + 0.5f * float(r & 1) + 0.25f - float(kCols) * 0.5f) * kAncho;
    z = (float(r) + 0.5f - float(kFilas) * 0.5f) * kAncho * 0.866f;
  }
  // ---- presentacion: lo que se sabe de cada unidad por los eventos de la simulacion (el estado de combate es solo de sim_)
  struct Pres {
    const Modelo* m = nullptr;
    float x = 0, z = 0, x1 = 0, z1 = 0, giro = 0;   // (x, z): centro de su casilla; (x1, z1): destino si anda
    bool anda = false, muerta = false;
    int clip = 0, mira = -1, est = 1;                // clip 0 espera, 1 andar, 2 ataque, 3 tecnica, 4 dano, 5 derrota, 6 levantarse, 7 victoria; est = estrellas
    uint32_t t0 = 0, t1 = 0, t_muerte = 0;           // ticks: inicio del clip, fin del paso, caida
    float t_golpe = -100;                            // tick del ultimo golpe recibido
    int clip_prev = -1;                              // clip anterior (mezcla al cambiar: sin corte seco)
    float t_prev = 0;                                // segundos dentro del clip anterior cuando se cambio
    double mezcla_t0 = -100;                         // cuando se cambio (segundos de ImGui)
    uint32_t t_reaccion = 0;                         // tick del golpe recibido si seguia andando (t0 es el del paso)
    uint32_t clip_fin = 0;                           // tick en que el clip de golpe vuelve a la espera
    uint32_t derribo_t = 0;                          // tick del derribo (dano > derrota > levantarse); 0 = ninguno
  };
  struct Flota { float x, z, dx; uint32_t tick; int valor, tipo; float dy; };   // numero de dano flotante; tipo 0 basico, 1 tecnica, 2 desgaste
  // Instantanea inmutable de un cuadro: todo lo que hace falta para pintarlo, sin tocar la simulacion ni el estado de presentacion.
  struct Foto {
    struct U { const Modelo* m; int equipo; float x, z, giro, esc, vida, ki, t; int clip; bool viva, oculta; int est; int c0 = -1, c1 = -1; float golpe = 0, dura = 0;
               float ciclo = -1; int clip_prev = -1; float t_prev = 0, mezcla = 0; };
    std::vector<U> u;
    float tp = 0;   // tick de simulacion con fraccion (interpolacion)
  };
  static uint32_t ClipT0(const Pres& p) { return p.clip == 4 && p.anda ? p.t_reaccion : p.t0; }
  // Cambia el clip de una unidad mezclando desde el anterior (0,18 s). `tick` = tick de simulacion del cambio.
  void CambiarClip(Pres& p, int nuevo, uint32_t tick) {
    if (nuevo == p.clip) return;
    p.clip_prev = p.clip;
    p.t_prev = std::max(0.0f, float(int64_t(tick) - int64_t(ClipT0(p))) / 60.0f);
    p.mezcla_t0 = ImGui::GetTime();
    p.clip = nuevo;
  }

  static void PosEn(const Pres& p, float tp, float& x, float& z) {
    float k = 0;
    if (p.anda && !p.muerta) k = std::clamp((tp - float(p.t0)) / float(std::max<uint32_t>(1, p.t1 - p.t0)), 0.0f, 1.0f);
    x = p.x + (p.x1 - p.x) * k, z = p.z + (p.z1 - p.z) * k;
  }
  // Lee los eventos nuevos de la simulacion y actualiza la presentacion (clips, destinos, numeros de dano, fin de ronda).
  // Un efecto del juego por la mezcla (maximo de voces, prioridad a tecnicas y KO); las pruebas pueden apagarlo con DBZ3_TEST_TACTICS_SIN_SONIDO
  void Sonar(double ahora, PrioSe prio, double dura, int banco, int id, int unidad = -1) {
    static const bool apagado = std::getenv("DBZ3_TEST_TACTICS_SIN_SONIDO") != nullptr;
    if (apagado || !opc_.sonido || !se_.Pedir(ahora, prio, dura)) return;
    if (unidad >= 0) {   // su propia voz: ranura 3/4 (los dos luchadores de fondo, Goku y Vegeta) o la que cargo combate.cpp; si aun no esta, la del toml
      if (const Modelo* m = ModeloDe(sim_->Unidades()[size_t(unidad)].def.id)) {
        const int s = m->b3 == kFondoA ? 3 : m->b3 == kFondoB ? 4 : dbz3::combate::SlotVoz(m->b3);
        if (s >= 0) banco = s;
      }
      REXLOG_INFO("dbz3 tactics: voz: {} banco {} id {}", sim_->Unidades()[size_t(unidad)].def.id, banco, id);
    }
    dbz3::combate::PedirSonido(banco, id);
  }
  void Leer(double ahora) {
    const auto& ev = sim_->Eventos();
    for (; leidos_ < ev.size(); ++leidos_) {
      const sim::Evento& e = ev[leidos_];
      const float tk = float(e.tick);
      Pres* p = e.unidad >= 0 ? &p_[size_t(e.unidad)] : nullptr;
      switch (e.tipo) {
        case sim::Tipo::Inicio: {
          const int nuevo = e.accion == sim::Accion::Mover ? 1 : e.accion == sim::Accion::Tecnica ? 3 : 2;
          CambiarClip(*p, nuevo, e.tick);
          p->t0 = e.tick;
          p->clip_fin = 0;
          p->derribo_t = 0;   // una accion nueva corta el derribo
          if (e.accion == sim::Accion::Mover) {
            p->anda = true;
            Pos(e.objetivo / sim::kAncho, e.objetivo % sim::kAncho, p->x1, p->z1);
            p->t1 = e.tick + uint32_t(std::max(1, 600 / std::max(1, sim_->Unidades()[size_t(e.unidad)].def.velocidad)));
          } else {
            p->mira = e.objetivo;
            if (e.accion == sim::Accion::Tecnica) {   // H11: efecto de la tecnica con texturas de efectos del propio juego (ver PintarFx)
              const auto& d = sim_->Unidades()[size_t(e.unidad)].def;
              fx_.push_back({e.tick + uint32_t(d.tecnica_ticks * 18 / 100), e.tick + uint32_t(d.tecnica_ticks / 2), e.unidad, e.objetivo, 1 + int(d.tecnica.forma), d.tecnica.alcance, d.tecnica.area});
            }
            if (e.accion == sim::Accion::Tecnica) { const auto& d = sim_->Unidades()[size_t(e.unidad)].def; Sonar(ahora, PrioSe::Tecnica, 0.9, d.voz_banco, d.voz_ataque[(e.tick / 7 + uint32_t(e.unidad)) % 3], e.unidad); }
          }
          break;
        }
        case sim::Tipo::Recuperacion:
          if (e.accion == sim::Accion::Mover) Pos(e.valor / sim::kAncho, e.valor % sim::kAncho, p->x, p->z), p->anda = false;
          CambiarClip(*p, 0, e.tick);
          p->t0 = e.tick;
          break;
        case sim::Tipo::Impacto: {
          Pres& v = p_[size_t(e.objetivo)];
          Flota f{0, 0, float(sim_->Cosmetico().Hasta(21)) * 0.01f - 0.1f, e.tick, e.valor, e.unidad < 0 ? 2 : e.accion == sim::Accion::Tecnica ? 1 : 0, 0};
          PosEn(v, tk, f.x, f.z);
          {   // H12: varios golpes sobre quien aun lleva un numero en pantalla: se escalonan (lado a lado y en altura) para que no se pisen
            int n = 0;
            for (const Flota& g : flota_) n += std::abs(g.x - f.x) < 0.4f && std::abs(g.z - f.z) < 0.4f && e.tick < g.tick + 20;
            static const float lado[3] = {0.0f, -0.55f, 0.55f};
            f.dx += lado[n % 3], f.dy = 0.4f * float(std::min(n, 4));
          }
          flota_.push_back(f);
          v.t_golpe = tk;
          const auto& ud = sim_->Unidades()[size_t(e.objetivo)];
          if (ud.viva()) {   // golpe recibido: clip nativo de reaccion del propio personaje (banco comun) y, si el golpe es fuerte
                             // y la unidad estaba quieta, derribo corto (dano > derrota > levantarse)
            CambiarClip(v, 4, e.tick);
            v.t_reaccion = e.tick;
            if (!v.anda) v.t0 = e.tick;   // t0 tambien es el inicio del paso: andando no se toca, o volveria a su casilla
            v.clip_fin = e.tick + 16;
            if (v.clip_prev == 0 && !v.derribo_t && (e.accion == sim::Accion::Tecnica || int64_t(e.valor) * 100 >= int64_t(ud.def.vida) * 15)) v.derribo_t = e.tick;
          }
          if (e.unidad >= 0 && e.accion != sim::Accion::Tecnica) fx_.push_back({e.tick, e.tick + 10, e.unidad, e.objetivo, 0, 0, 0});   // chispas del golpe basico
          if (e.unidad >= 0) Sonar(ahora, e.accion == sim::Accion::Tecnica ? PrioSe::Tecnica : PrioSe::Golpe, 0.35, kSeImpacto[0], kSeImpacto[1]);
          break;
        }
        case sim::Tipo::Muerte:
          PosEn(*p, tk, p->x, p->z);
          p->anda = false, p->muerta = true;
          CambiarClip(*p, 5, e.tick);   // KO: cae y queda tumbado (clip nativo) antes de hundirse
          p->t0 = e.tick;
          p->t_muerte = e.tick + 45;   // primero se ve el KO (clip de 60 cuadros) y luego se hunde
          p->clip_fin = 0, p->derribo_t = 0;
          if (!demo_ && !caido_ && sim_->Unidades()[size_t(e.unidad)].equipo == 0) caido_ = ModeloDe(sim_->Unidades()[size_t(e.unidad)].def.id);
          { const auto& d = sim_->Unidades()[size_t(e.unidad)].def; Sonar(ahora, PrioSe::Muerte, 0.9, d.voz_banco, d.voz_ko[(e.tick / 5 + uint32_t(e.unidad)) % 3], e.unidad); }
          break;
        case sim::Tipo::FinRonda: {
          fin_t_ = ahora;
          const sim::Resultado r = sim_->Resumen();
          victoria_t0_ = ahora;   // pose de victoria de los que quedan en pie del equipo ganador (clip nativo)
          if (r.ganador >= 0)
            for (size_t i = 0; i < p_.size(); ++i)
              if (sim_->Unidades()[i].viva() && sim_->Unidades()[i].equipo == r.ganador) {
                CambiarClip(p_[i], 7, sim_->Tick());
                p_[i].t0 = sim_->Tick();
              }
          REXLOG_INFO("dbz3 tactics: sonidos: golpes {} (descartados {}), tecnicas {} ({}), KO {} ({}), como mucho {} voces a la vez", se_.Sonados(PrioSe::Golpe), se_.Descartados(PrioSe::Golpe),
                      se_.Sonados(PrioSe::Tecnica), se_.Descartados(PrioSe::Tecnica), se_.Sonados(PrioSe::Muerte), se_.Descartados(PrioSe::Muerte), int(MezclaSe::kVoces));
          REXLOG_INFO("dbz3 tactics: ritmo del combate: {} cuadros, {:.0f} FPS de media, peor cuadro {:.1f} ms, {} cuadros de mas de 25 ms ({} unidades)", cc_, cc_ ? double(cc_) / dt_suma_ : 0.0,
                      dt_peor_ * 1000.0, lentos_, sim_->Unidades().size());
          if (demo_) {
            REXLOG_INFO("dbz3 tactics: ronda {}v{} semilla {} fin: ganador {} en {} ticks, {} eventos, huella {:08x} (velocidad x{})", lado_, lado_, semilla_, r.ganador,
                        r.ticks, ev.size(), sim_->Huella(), vel_);
          } else if (!aplicado_) {
            aplicado_ = true;
            partida_->TerminarCombate();
            const Duelo& d = partida_->Duelos()[size_t(partida_->DueloHumano())];
            REXLOG_INFO("dbz3 tactics: partida ronda {} fin: ganador {} en {} ticks, {} eventos, huella {:08x} (velocidad x{}), vida {} / {}, dano {} / {}", partida_->Ronda(),
                        r.ganador, r.ticks, ev.size(), sim_->Huella(), vel_, partida_->J(0).vida, d.copia ? 0 : partida_->J(d.b).vida, partida_->DanoRonda(0), partida_->DanoRival());
          }
          break;
        }
        default: break;   // Fallo: el golpe no cae sobre nadie, no hay nada que mostrar
      }
    }
    const uint32_t tick = sim_->Tick();
    flota_.erase(std::remove_if(flota_.begin(), flota_.end(), [&](const Flota& f) { return tick > f.tick + kVidaNumero; }), flota_.end());
  }
  // Un cuadro de tiempo: pasos fijos de 1/60 s con acumulador. La pausa (P, foco perdido) y los huecos (carga, salto del reloj) descartan el tiempo.
  void Avanzar(double ahora, double dt) {
    alpha_ = 0;
    if (!sim_) return;   // preparacion: no hay combate
    if (fin_t_ >= 0) {
      if (demo_) {
        if (ahora - fin_t_ > 4.0) {   // letrero visto: vuelve a colocar (en las pruebas con varias velocidades, repite la ronda)
          if (!vels_.empty() && vel_i_ + 1 < int(vels_.size())) ++vel_i_, Colocar(lado_, semilla_);
          else Colocar(lado_, semilla_ + 1);
        }
      } else {
        Resultado(ahora);
      }
      return;
    }
    static const bool sin_foco = std::getenv("DBZ3_TEST_TACTICS_SIN_FOCO") != nullptr;
    if (pausa_ || (!sin_foco && !TengoFoco()) || dt <= 0 || dt > 0.25) {
      acc_ = 0;
      return;
    }
    if (!sim_->Terminada()) {
      constexpr double kPaso = 1.0 / sim::kHz;
      acc_ += dt * vel_;
      int n = 0;
      while (acc_ >= kPaso && n < 32 && !sim_->Terminada()) sim_->Paso(), acc_ -= kPaso, ++n;
      if (n == 32) acc_ = 0;
      alpha_ = sim_->Terminada() ? 0.0f : float(acc_ / kPaso);
    }
    Leer(ahora);
  }
  // Tras el letrero (5 s o A): siguiente ronda, o fin de la partida (A: otra nueva)
  void Resultado(double ahora) {
    if (ahora - fin_t_ < 5.0 && !ok_) return;
    partida_->SiguienteRonda();
    if (partida_->Acabada()) {
      fin_partida_ = true;
      sim_ = nullptr;   // la simulacion de la ultima ronda ya no existe
      std::error_code ec;   // la partida ya no se puede continuar: el fichero se aparta (no se borra)
      std::filesystem::rename(RutaGuardado(), RutaGuardado().string() + ".terminada", ec);
      REXLOG_INFO("dbz3 tactics: partida acabada en la ronda {}: campeon {}, tu puesto {} de {}", partida_->Ronda(), partida_->Campeon(), partida_->J(0).puesto, partida_->N());
    } else {
      EntrarPreparacion();
    }
  }
  static bool TengoFoco() {
#if defined(_WIN32)
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
#else
    return true;
#endif
  }
  // Las unidades se vuelven hacia su rival (o hacia donde andan)
  void Girar(float dt) {
    if (!sim_) return;
    const float tk = float(sim_->Tick());
    for (Pres& p : p_) {
      if (p.muerta) continue;
      float x, z, tx = p.x1, tz = p.z1;
      PosEn(p, tk, x, z);
      if (!p.anda) {
        if (p.mira < 0 || !sim_->Unidades()[size_t(p.mira)].viva()) continue;
        PosEn(p_[size_t(p.mira)], tk, tx, tz);
      }
      const float da = std::remainder(std::atan2(tx - x, tz - z) - p.giro, 6.2831853f);
      p.giro += std::clamp(da, -9.0f * dt, 9.0f * dt);
    }
  }
  Foto Instantanea() {
    Foto f;
    const uint32_t tick = sim_->Tick();
    f.tp = float(tick) + alpha_;
    for (size_t i = 0; i < p_.size(); ++i) {
      Pres& p = p_[i];
      const sim::Unidad& u = sim_->Unidades()[i];
      // fin del clip de golpe: vuelve a andar (si seguia andando) o a la espera (con mezcla)
      if (p.clip == 4 && p.clip_fin && tick >= p.clip_fin && !p.derribo_t) {
        CambiarClip(p, p.anda ? 1 : 0, p.clip_fin);
        if (!p.anda) p.t0 = p.clip_fin;
        p.clip_fin = 0;
      }
      // derribo: golpe recibido > tumbado (KO) > levantarse, y vuelve a la espera
      if (p.derribo_t) {
        const uint32_t dt = tick - p.derribo_t;
        const int fase = dt < 10 ? 4 : dt < 34 ? 5 : dt < 94 ? 6 : 0;
        if (fase != p.clip) {
          CambiarClip(p, fase, tick);
          p.t0 = p.derribo_t + (fase == 4 ? 0 : fase == 5 ? 10 : fase == 6 ? 34 : 94);
        }
        if (fase == 0) p.derribo_t = 0;
      }
      Foto::U o{p.m, u.equipo, 0, 0, p.giro, 1.0f, 0, 0, 0, p.clip, u.viva(), false, p.est};
      PosEn(p, f.tp, o.x, o.z);
      o.vida = std::clamp(float(u.vida) / float(u.def.vida), 0.0f, 1.0f);
      o.ki = std::clamp(float(u.ki) / float(u.def.ki_max), 0.0f, 1.0f);
      o.dura = p.clip == 2 ? u.def.ataque_ticks / 60.0f : p.clip == 3 ? u.def.tecnica_ticks / 60.0f : p.clip == 4 ? 0.27f : p.clip == 6 ? 1.0f : 0.0f;
      if (p.clip == 7) o.t = float(ImGui::GetTime() - victoria_t0_);   // victoria: reloj de pared (la simulacion ya acabo)
      else o.t = p.clip <= 1 ? f.tp / 60.0f + 0.37f * float(i) : std::max(0.0f, f.tp - float(ClipT0(p))) / 60.0f;
      if (p.clip == 1 && p.anda && !p.muerta) {   // andar sin deslizar: el clip va con el avance (una vuelta cada ~0,4 s)
        const float prog = std::clamp((f.tp - float(p.t0)) / float(std::max<uint32_t>(1, p.t1 - p.t0)), 0.0f, 1.0f);
        o.ciclo = prog * std::clamp(float(p.t1 - p.t0) / 24.0f, 1.0f, 8.0f);
      }
      o.clip_prev = p.clip_prev;
      o.t_prev = p.t_prev;
      o.mezcla = std::clamp(float((ImGui::GetTime() - p.mezcla_t0) / 0.18), 0.0f, 1.0f);
      if (p.muerta) o.esc = std::clamp(1.0f - (f.tp - float(p.t_muerte)) / float(kTicksHundir), 0.0f, 1.0f);   // tras el KO, se hunde
      else o.golpe = std::clamp(1.0f - (f.tp - p.t_golpe) / 9.0f, 0.0f, 1.0f);   // H11: destello suave al recibir un golpe (antes la figura se ocultaba cada 2 ticks)
      f.u.push_back(o);
    }
    // H13: la simulacion ya reserva una casilla por unidad, asi que dos figuras VIVAS nunca se tocan (la distancia minima medida es 0,87 casillas, cuando una pasa
    // junto a otra). El empuje de H12 (minimo 0,95) saltaba en cada cruce y movia a las que estaban quietas hasta media casilla: ahora solo corrige un solape de verdad
    // (<0,7) y no cuenta a las que se hunden.
    for (int it = 0; it < 3; ++it)
      for (size_t a = 0; a < f.u.size(); ++a)
        for (size_t b = a + 1; b < f.u.size(); ++b) {
          Foto::U &ua = f.u[a], &ub = f.u[b];
          if (!ua.viva || !ub.viva) continue;
          float dx = ub.x - ua.x, dz = ub.z - ua.z;
          const float d = std::sqrt(dx * dx + dz * dz), minimo = 0.7f * kAncho;
          if (d >= minimo) continue;
          if (d < 1e-3f) dx = a % 2 ? 0.96f : -0.96f, dz = 0.29f;   // justo encima: se abren en un sentido fijo
          else dx /= d, dz /= d;
          const float emp = (minimo - d) * 0.5f;
          ua.x -= dx * emp, ua.z -= dz * emp, ub.x += dx * emp, ub.z += dz * emp;
        }
    return f;
  }
  // Preparacion: los dos tableros quietos, tal como estan en la partida
  Foto FotoPreparacion(double ahora) const {
    Foto f;
    if (partida_->DueloHumano() < 0) return f;
    const Duelo& d = partida_->Duelos()[size_t(partida_->DueloHumano())];
    for (int p = 0; p < 2; ++p)
      for (int s = kBanco; s < kSlots; ++s) {
        const Pieza& z = partida_->J(p == 0 ? 0 : d.b).pz[s];
        if (!z.hay()) continue;
        const int idx = s - kBanco, fila = p == 0 ? 3 + idx / 6 : 2 - idx / 6;
        Foto::U o{ModeloDe(eco_.unidades[size_t(z.unidad)].id), p, 0, 0, p == 0 ? 3.14159f : 0.0f, 1.0f, 1.0f, 0, float(ahora) + 0.37f * float(f.u.size()), 0, true, false, z.estrellas};
        o.c0 = z.caps[0], o.c1 = z.caps[1];
        Pos(fila, p == 0 ? idx % 6 : 5 - idx % 6, o.x, o.z);   // el tablero del rival se ve girado 180 grados: la simulacion lo coloca asi (rejilla hexagonal simetrica)
        f.u.push_back(o);
      }
    return f;
  }

  // ---- entrada: mando (cruceta/palanca, A, B, X, Y, Start) y raton; mismas acciones
  void Entrada(uint16_t pad, double ahora) {
    bool ok = pad & kA, vender = (pad & kX) || ImGui::IsKeyPressed(ImGuiKey_Delete, false);
    if (pad && EnPrueba()) REXLOG_INFO("dbz3 tactics: pad {:04x} con el cursor en zona {} casilla {}", pad, int(zc_), ic_);
    const ImGuiIO& io = ImGui::GetIO();
    const bool mueve = io.MouseDelta.x != 0 || io.MouseDelta.y != 0, clic = ImGui::IsMouseClicked(0), clic2 = ImGui::IsMouseClicked(1), clic3 = ImGui::IsMouseClicked(2);
    if (pad || mueve || clic || clic2) ficha_ver_ = true;
    if (EnPrueba()) { static bool abajo_ant = false; if (io.MouseDown[0] != abajo_ant) { abajo_ant = io.MouseDown[0]; REXLOG_INFO("dbz3 tactics: raton izq {} en {:.0f},{:.0f} arr {} mano {}", abajo_ant ? "ABAJO" : "ARRIBA", io.MousePos.x, io.MousePos.y, arr_, mano_); } }
    if (tut_ > 0) {   // el tutorial lo tapa todo: A, clic o Intro pasan de pantalla (la ultima lo cierra)
      if (ok || clic || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (++tut_ > 4) CerrarTutorial();
      }
      return;
    }
    bool quitar = (pad & kLB) || ImGui::IsKeyPressed(ImGuiKey_Q, false) || clic3;
    bool sobre = false;
    if (mueve || clic || clic2 || clic3) {
      for (const Hit& h : hits_)
        if (io.MousePos.x >= h.a.x && io.MousePos.x <= h.b.x && io.MousePos.y >= h.a.y && io.MousePos.y <= h.b.y) {
          sobre = true;
          if (clic && h.z != Zona::Caps && !(h.z == Zona::Ayuda && h.i == 1)) caps_abiertas_ = false;   // H12: un clic fuera del panel lo cierra
          if (h.z != Zona::Ayuda || clic) zc_ = h.z, ic_ = h.i;
          if (menu_) mc_ = h.i;
          ok = ok || clic, vender = vender || clic2;
        }
      if (mueve && !sobre && zc_ == Zona::Sin) zc_ = Zona::Tablero, ic_ = 0;   // el raton salio de la ficha de sinergia
    }
    if ((clic || clic2) && EnPrueba()) REXLOG_INFO("dbz3 tactics: clic en {:.0f},{:.0f}: zona {} casilla {}, sobre una zona: {}, tutorial {}, hits {}", io.MousePos.x, io.MousePos.y, int(zc_), ic_, sobre, tut_, hits_.size());
    if ((pad & kY) || ImGui::IsKeyPressed(ImGuiKey_V, false)) vel_ = vel_ == 1 ? 2 : vel_ == 2 ? 4 : 1;
    if ((pad & kY) && !sim_ && partida_ && partida_->FaseActual() == Fase::Preparacion) espia_ = espia_ + 1 < partida_->N() ? espia_ + 1 : -1;   // H10: Y recorre los tableros de la lista y luego se apaga
    if ((pad & kStart) || ImGui::IsKeyPressed(ImGuiKey_P, false)) pausa_ = !pausa_;
    if (menu_) {   // menu propio: cruceta / palanca / raton; A elige, izquierda y derecha cambian una opcion, B vuelve
      const int n = menu_pag_ == 0 ? int(FilasMenu().size()) : 5;
      if (pad & kArriba) mc_ = (mc_ + n - 1) % n;
      if (pad & kAbajo) mc_ = (mc_ + 1) % n;
      if (menu_pag_ == 1) {
        const int d = ((pad & kDer) ? 1 : 0) - ((pad & kIzq) ? 1 : 0);
        if (d) CambiarOpc(mc_, d);
        else if (ok && mc_ < 4) CambiarOpc(mc_, 1);
        else if (vender && mc_ < 4) CambiarOpc(mc_, -1);
        else if (ok) menu_pag_ = 0, mc_ = 3 - (guardada_ ? 0 : 1);
      } else if (ok) {
        ElegirMenu(FilasMenu()[size_t(mc_)]);
      }
      return;
    }
    if (demo_) {
      if ((pad & kX) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) Colocar(lado_ == 3 ? 6 : 3, semilla_);
      if (ok || ImGui::IsKeyPressed(ImGuiKey_R, false)) Colocar(lado_, semilla_);   // la misma ronda otra vez
      return;
    }
    ok_ = ok;
    if (fin_partida_) {   // clasificacion final: A empieza otra partida (B dos veces: HUB)
      if (ok) NuevaPartida(semilla_ + 1);
      return;
    }
    if (partida_->FaseActual() != Fase::Preparacion) return;
    if ((pad & kBack) || ImGui::IsKeyPressed(ImGuiKey_T, false)) return AbrirTutorial(true);
    if (pad & kRB) Registra("bloquear", 0, 0, partida_->Bloquear(0));
    if (mano_ >= 0 && !partida_->J(0).pz[mano_].hay()) mano_ = -1;
    const int dx = ((pad & kDer) ? 1 : 0) - ((pad & kIzq) ? 1 : 0), dy = ((pad & kAbajo) ? 1 : 0) - ((pad & kArriba) ? 1 : 0);
    if (dx || dy) Navegar(dx, dy);
    if (kArrastrar && arr_ >= 0 && ImGui::IsMouseReleased(0)) {   // H14: soltar una unidad arrastrada: sobre la tienda la vende; sobre otra casilla, la mueve
      const bool lejos = std::abs(io.MousePos.x - arr_p_.x) + std::abs(io.MousePos.y - arr_p_.y) > 14.0f;
      const int origen = arr_;
      arr_ = -1;
      if (lejos && partida_->J(0).pz[origen].hay())
        for (const Hit& h : hits_)
          if (io.MousePos.x >= h.a.x && io.MousePos.x <= h.b.x && io.MousePos.y >= h.a.y && io.MousePos.y <= h.b.y) {
            if (h.z == Zona::Tienda) {
              if (Registra("vender", origen, 0, partida_->Vender(0, origen)) == Res::Ok) mano_ = -1;
            } else if (h.z == Zona::Banco || h.z == Zona::Tablero) {
              const int d = h.z == Zona::Banco ? h.i : kBanco + h.i;
              if (d != origen) Aviso(Registra("mover", origen, d, partida_->Mover(0, origen, d)), ahora);
              mano_ = -1;
            }
            break;
          }
    }
    if (ok) {
      const int antes = mano_;
      Confirmar(ahora);
      arr_ = kArrastrar && clic && antes < 0 && mano_ >= 0 && (zc_ == Zona::Banco || zc_ == Zona::Tablero) ? mano_ : -1;
      arr_p_ = io.MousePos;
    } else if (vender && (zc_ == Zona::Banco || zc_ == Zona::Tablero)) {
      const int s = zc_ == Zona::Banco ? ic_ : kBanco + ic_;
      if (Registra("vender", s, 0, partida_->Vender(0, s)) == Res::Ok && mano_ == s) mano_ = -1;
    } else if (quitar && (zc_ == Zona::Banco || zc_ == Zona::Tablero)) {   // la ultima capsula de la unidad bajo el cursor vuelve al inventario
      const int s = zc_ == Zona::Banco ? ic_ : kBanco + ic_;
      const Pieza& z = partida_->J(0).pz[s];
      if (z.hay() && (z.caps[0] >= 0 || z.caps[1] >= 0)) Registra("quitar", s, z.caps[1] >= 0 ? 1 : 0, partida_->Quitar(0, s, z.caps[1] >= 0 ? 1 : 0));
    }
  }
  // Cursor por zonas: tablero (3 filas) > banquillo > tienda > botones
  void Navegar(int dx, int dy) {
    const auto lim = [](int v, int n) { return std::clamp(v, 0, n - 1); };
    switch (zc_) {
      case Zona::Sin:   // H10: filas del panel de sinergias (cada una muestra su ficha al enfocarla)
        if (nsin_ == 0 || dx > 0) zc_ = Zona::Tablero, ic_ = 0;
        else ic_ = lim(ic_ + dy, nsin_);
        break;
      case Zona::Tablero: {
        if (dx < 0 && ic_ % 6 == 0 && nsin_ > 0) { zc_ = Zona::Sin, ic_ = 0; break; }
        int r = ic_ / 6, c = lim(ic_ % 6 + dx, 6);
        if (dy > 0 && r == 2) zc_ = Zona::Banco, ic_ = c;
        else ic_ = lim(r + dy, 3) * 6 + c;
        break;
      }
      case Zona::Banco:
        if (dy < 0) zc_ = Zona::Tablero, ic_ = 12 + lim(ic_, 6);
        else if (dy > 0) zc_ = Zona::Tienda, ic_ = lim(ic_ * 5 / kBanco, 5);
        else if (dx < 0 && ic_ == 0) zc_ = Zona::Caps, ic_ = 3;
        else ic_ = lim(ic_ + dx, kBanco);
        break;
      case Zona::Caps: {   // rejilla de 4 columnas con el boton de comprar al final; a la derecha del todo se pasa al banquillo
        const int n = int(eco_.capsulas.size());
        if (ic_ >= n) {
          if (dy < 0) ic_ = n - 1;
          else if (dx > 0) zc_ = Zona::Banco, ic_ = 0;
        } else if (dx != 0) {
          if (dx > 0 && (ic_ % kColsCaps == kColsCaps - 1 || ic_ == n - 1)) zc_ = Zona::Banco, ic_ = 0;
          else ic_ = std::clamp(ic_ + dx, 0, n - 1);
        } else if (dy > 0) {
          ic_ = ic_ + kColsCaps < n ? ic_ + kColsCaps : n;
        } else if (dy < 0) {
          ic_ = std::max(0, ic_ - kColsCaps);
        }
        break;
      }
      case Zona::Ayuda: zc_ = Zona::Botones, ic_ = 3; break;
      case Zona::Tienda:
        if (dy < 0) zc_ = Zona::Banco, ic_ = lim(ic_ * kBanco / 5, kBanco);
        else if (dy > 0) zc_ = Zona::Botones, ic_ = lim(ic_, kBotones);
        else ic_ = lim(ic_ + dx, 5);
        break;
      case Zona::Botones:
        if (dy < 0) zc_ = Zona::Tienda, ic_ = lim(ic_, 5);
        else ic_ = lim(ic_ + dx, kBotones);
        break;
    }
  }
  // Pruebas de mando y raton (tools/pruebas.py): una vez por preparacion se escribe en el log el centro en pixeles de cada zona clicable
  void LogHits(const ImGuiIO& io) {
    if (hits_dichos_ || !EnPrueba() || !partida_ || partida_->FaseActual() != Fase::Preparacion || hits_.size() < 30) return;
    hits_dichos_ = true;
    static const char letra[] = {'T', 'B', 'S', 'X', 'C', 'A', 'N'};
    std::string t;
    for (const Hit& h : hits_) t += std::string(" ") + letra[int(h.z)] + std::to_string(h.i) + "=" + std::to_string(int((h.a.x + h.b.x) * 0.5f)) + "," + std::to_string(int((h.a.y + h.b.y) * 0.5f));
    REXLOG_INFO("dbz3 tactics: zonas {}x{}:{}", int(io.DisplaySize.x), int(io.DisplaySize.y), t);
  }
  // Cada accion del humano queda en el log (las pruebas de mando y raton comprueban lo que de verdad llego a la partida)
  Res Registra(const char* accion, int a, int b, Res r) {
    static const char* const nombre[] = {"ok", "fase", "invalido", "sin oro", "sin sitio", "tope"};
    REXLOG_INFO("dbz3 tactics: accion {} {} {} -> {} (oro {}, tablero {})", accion, a, b, nombre[int(r)], partida_->J(0).oro, partida_->EnTablero(0));
    // subTAC: sonido de la interfaz del juego (banco 0 = sistema, el del menu: 3 = aceptar, 2 = mover) al comprar / vender; respeta Opciones > Sonidos
    if (r == Res::Ok && partida_->FaseActual() == Fase::Preparacion && (!std::strcmp(accion, "comprar") || !std::strcmp(accion, "vender"))) {
      int sb = 0, si = accion[0] == 'c' ? 3 : 2;
      if (const char* t = std::getenv("DBZ3_TEST_TACTICS_SE_COMPRA")) std::sscanf(t, "%d,%d", &sb, &si);   // pruebas.py: otro (banco,id) para medir cual suena
      Sonar(ImGui::GetTime(), PrioSe::Tecnica, 0.3, sb, si);
      if (accion[0] == 'v') destellos_.push_back({a, ImGui::GetTime(), 1});   // venta: aro rojo en el hueco que se vacia
    }
    return r;
  }
  void Aviso(Res r, double ahora) {
    const char* t = r == Res::SinOro     ? T::T("No tienes oro suficiente.", "Not enough gold.")
                    : r == Res::SinSitio ? T::T("El banquillo está lleno.", "The bench is full.")
                    : r == Res::Tope     ? T::T("El tablero está completo: sube de nivel con Comprar EXP.", "The board is full: level up with Buy EXP.")
                                         : nullptr;
    if (t) aviso_ = t, aviso_t_ = ahora, aviso_dura_ = 2.5;
  }
  void Confirmar(double ahora) {
    Partida& pa = *partida_;
    switch (zc_) {
      case Zona::Tienda: Aviso(Registra("comprar", ic_, 0, pa.Comprar(0, ic_)), ahora); break;
      case Zona::Ayuda: {   // 0: boton Tutorial; 1: la tira de capsulas (abre o cierra el panel)
        const int i = ic_;
        zc_ = Zona::Botones, ic_ = 3;
        if (i == 1) caps_abiertas_ = !caps_abiertas_;
        else AbrirTutorial(true);
        break;
      }
      case Zona::Sin: break;
      case Zona::Caps:
        if (ic_ >= int(eco_.capsulas.size())) {
          const Res r = Registra("capsula", 0, 0, pa.ComprarCapsula(0));
          Aviso(r, ahora);
          for (size_t c = 0; r == Res::Ok && c < eco_.capsulas.size(); ++c)
            if (pa.J(0).inv[c] > 0) { REXLOG_INFO("dbz3 tactics: capsula en el inventario: {}", c); break; }
        } else if (pa.J(0).inv[ic_] > 0) capmano_ = capmano_ == ic_ ? -1 : ic_, mano_ = -1;
        else capmano_ = -1;
        break;
      case Zona::Botones:
        if (ic_ == 0) Aviso(Registra("exp", 0, 0, pa.ComprarExp(0)), ahora);
        else if (ic_ == 1) Aviso(Registra("actualizar", 0, 0, pa.Actualizar(0)), ahora);
        else if (ic_ == 4) Registra("bloquear", 0, 0, pa.Bloquear(0));
        else if (ic_ == 2) {
          if (pa.EnTablero(0) == 0) aviso_ = T::T("Coloca al menos una unidad en el tablero.", "Place at least one unit on the board."), aviso_t_ = ahora, aviso_dura_ = 2.5;
          else {
            REXLOG_INFO("dbz3 tactics: accion listo");
            IniciarCombate();
          }
        } else if (ahora - salir_t_ < 3.0) {
          g_cerrar = true;
        } else {
          salir_t_ = ahora;
        }
        break;
      default: {
        const int s = zc_ == Zona::Banco ? ic_ : kBanco + ic_;
        if (capmano_ >= 0) {   // capsula en la mano: al primer hueco libre de la unidad (si lleva dos, sustituye a la segunda)
          if (pa.J(0).pz[s].hay()) {
            const int h = pa.J(0).pz[s].caps[0] < 0 ? 0 : 1;
            if (Registra("equipar", s, capmano_, pa.Equipar(0, s, h, capmano_)) == Res::Ok && pa.J(0).inv[capmano_] == 0) capmano_ = -1;
          }
        } else if (mano_ < 0) {
          if (pa.J(0).pz[s].hay()) mano_ = s;
        } else {
          if (mano_ != s) Aviso(Registra("mover", mano_, s, pa.Mover(0, mano_, s)), ahora);
          mano_ = -1;
        }
      }
    }
  }

  // ---- dibujo
  bool Elegido(Zona z, int i) const { return zc_ == z && ic_ == i; }
  void Marcar(Zona z, int i, ImVec2 a, ImVec2 b) { hits_.push_back({z, i, a, b}); }

  void Tablero(ImDrawList* dl, ImDrawList* arriba, double ahora, const S::Area& a) {
    const auto t0 = Reloj::now();
    const double dt = ahora - ultimo_t_;
    ultimo_t_ = ahora;
    if (sim_ && fin_t_ < 0 && !pausa_ && dt > 0 && dt < 1.0) ++cc_, dt_suma_ += dt, dt_peor_ = std::max(dt_peor_, dt), lentos_ += dt > 0.025;   // ritmo del dibujo durante el combate
    Avanzar(ahora, dt);
    Girar(std::clamp(float(dt), 0.0f, 0.1f));
    hits_.clear();
    const bool prep = !sim_;
    if (static const char* rep = std::getenv("DBZ3_TEST_TACTICS_SE_REPITE"); rep && prep) {   // pruebas.py (tactics_compra): "banco,id" cada 0,5 s entre los segundos 6 y 12 de la preparacion, para medir en el volcado de audio si suena
      static double t0 = ahora, ult = 0;
      int sb = 0, si = 3;
      std::sscanf(rep, "%d,%d", &sb, &si);
      if (ahora - t0 > 6.0 && ahora - t0 < 12.0 && ahora - ult >= 0.5) ult = ahora, Sonar(ahora, PrioSe::Tecnica, 0.3, sb, si);
    }
    // piloto automatico de las pruebas: compra y coloca como el bot y juega N rondas seguidas
    if (prep && auto_rondas_ > 0 && !fin_partida_ && partida_->FaseActual() == Fase::Preparacion) {
      if (prep_t_ < 0) prep_t_ = ahora;
      static const bool pasivo = std::getenv("DBZ3_TEST_TACTICS_PASIVO") != nullptr;   // prueba de la clasificacion: el humano no hace nada y cae
      if (ahora - prep_t_ > 3.0 && !auto_jugado_) auto_jugado_ = true, pasivo ? void() : partida_->Jugar(0);   // a los 3 s compra y coloca (antes se ve la tienda sin tocar)
      if (ahora - prep_t_ > 6.0) --auto_rondas_, IniciarCombate();
    }
    // casillas hexagonales (de las de mas lejos a las mas cercanas)
    const float R = kAncho / 1.7320508f;
    for (int f = 0; f < kFilas; ++f)
      for (int c = 0; c < kCols; ++c) {
        float cx, cz;
        Pos(f, c, cx, cz);
        ImVec2 q[6], centro;
        bool ok = true;
        for (int i = 0; i < 6; ++i) {
          const float ang = 1.0472f * float(i) - 0.5236f;
          float k;
          ok = Proy(cx + R * std::cos(ang), 0, cz + R * std::sin(ang), q[i], k) && ok;
        }
        if (!ok) continue;
        const bool mia = !demo_ && f >= 3;   // la mitad del jugador: las casillas donde coloca
        ImU32 relleno = (f + c) & 1 ? IM_COL32(58, 70, 108, 170) : IM_COL32(44, 54, 88, 170);
        if (!demo_) relleno = mia ? ((f + c) & 1 ? IM_COL32(52, 96, 150, 175) : IM_COL32(40, 82, 134, 175)) : ((f + c) & 1 ? IM_COL32(120, 58, 66, 170) : IM_COL32(100, 46, 54, 170));
        dl->AddConvexPolyFilled(q, 6, relleno);   // translucidas: se ve el ring debajo
        dl->AddPolyline(q, 6, IM_COL32(170, 190, 240, 140), ImDrawFlags_Closed, 1.0f);
        if (mia && prep) {
          float k;
          const int idx = (f - 3) * 6 + c;
          if (Proy(cx, 0, cz, centro, k)) Marcar(Zona::Tablero, idx, ImVec2(centro.x - 0.45f * kAncho * k, centro.y - 0.4f * kAncho * k), ImVec2(centro.x + 0.45f * kAncho * k, centro.y + 0.4f * kAncho * k));
          if (Elegido(Zona::Tablero, idx)) dl->AddPolyline(q, 6, IM_COL32(255, 220, 60, 255), ImDrawFlags_Closed, 4.0f);
          else if (mano_ == kBanco + idx) dl->AddPolyline(q, 6, IM_COL32(90, 230, 255, 255), ImDrawFlags_Closed, 4.0f);
        }
      }
    static const int clip = std::getenv("DBZ3_TEST_TACTICS_CLIP") ? std::atoi(std::getenv("DBZ3_TEST_TACTICS_CLIP")) : -1;   // prueba: fija el clip de todos
    ActualizarFormas(ahora, prep);
    const Foto foto = fin_partida_ ? Foto{} : prep ? FotoPreparacion(ahora) : Instantanea();
    const float giro_mundo = std::atan2(eje_[0], eje_[1]) - 1.5707963f;   // giro del tablero (0 = hacia +z del tablero) llevado al mundo
    for (const Foto::U& u : foto.u) {   // H11: halo blanco que se apaga solo detras de quien recibe un golpe
      ImVec2 o;
      float k;
      if (u.golpe > 0.0f && Proy(u.x, kAltoFigura * 0.5f, u.z, o, k)) dl->AddCircleFilled(o, 0.42f * k * (1.0f + 0.25f * (1.0f - u.golpe)), IM_COL32(255, 245, 220, int(95.0f * u.golpe * u.golpe)), 24);
    }
    PintarFx(dl, foto.tp, 0);   // H12: antes de las figuras, para no taparlas
    esc_.Empezar();
    for (const Foto::U& u : foto.u)
      if (!u.oculta && u.esc > 0.0f)
        DibujarEstrellas(&esc_, cam_, u.m, u.est, Mundo(u.x, 0, u.z), kAltoFigura * kEscala * u.esc, u.giro + giro_mundo, clip >= 0 ? clip : u.clip, u.t, u.dura,
                         u.ciclo, u.clip_prev, u.t_prev, u.mezcla);
    esc_.Pintar(dl);
    PintarFx(dl, foto.tp, 1);
    // estrellas, y vida y ki reales sobre cada unidad en pie, por encima de todo
    const float y_techo = LienzoDe(ImGui::GetIO()).At(0, 100).y;   // en 4:3 las barras de los mas altos rozaban el rotulo Rival (y=80)
    for (const Foto::U& u : foto.u) {
      ImVec2 o;
      float k;
      if (!u.viva || !Proy(u.x, kAltoFigura * AltoRel(u.m, u.est) + 0.3f, u.z, o, k)) continue;
      o.y = std::max(o.y, y_techo);
      const float w = 0.8f * k, h = 0.08f * k;
      Estrellas(arriba, ImVec2(o.x, o.y - 0.17f * k), 0.075f * k, u.est);
      for (int h = 0; h < 2; ++h)
        if (const int c = h ? u.c1 : u.c0; c >= 0) {
          Botella(arriba, ImVec2(o.x + (0.40f + 0.13f * float(h)) * k, o.y - 0.17f * k), 0.2f * k, c);

        }
      const ImU32 bando = u.equipo ? IM_COL32(230, 70, 60, 255) : IM_COL32(70, 150, 255, 255);
      arriba->AddRectFilled(ImVec2(o.x - w / 2 - 1, o.y - 1), ImVec2(o.x + w / 2 + 1, o.y + 2.0f * h + 1), IM_COL32(0, 0, 0, 200), 2);
      arriba->AddRectFilled(ImVec2(o.x - w / 2, o.y), ImVec2(o.x - w / 2 + w * u.vida, o.y + h), IM_COL32(80, 220, 90, 255));
      arriba->AddRectFilled(ImVec2(o.x - w / 2, o.y + h), ImVec2(o.x - w / 2 + w * u.ki, o.y + 2.0f * h), IM_COL32(70, 170, 255, 255));
      arriba->AddRectFilled(ImVec2(o.x - w / 2, o.y - 0.09f * k), ImVec2(o.x - w / 2 + 0.07f * k, o.y - 0.02f * k), bando);
    }
    // numeros de dano: suben y se apagan
    for (const Flota& f : flota_) {
      const float edad = std::clamp((foto.tp - float(f.tick)) / float(kVidaNumero), 0.0f, 1.0f);
      ImVec2 o;
      float k;
      if (!Proy(f.x + f.dx, kAltoFigura + 0.5f + f.dy + 0.7f * edad, f.z, o, k)) continue;
      const std::string t = std::to_string(f.valor);
      const float fs = (f.tipo == 1 ? 0.30f : 0.24f) * k;
      const int al = int(255.0f * (1.0f - edad * edad));
      const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, t.c_str());
      const ImVec2 pos(o.x - sz.x / 2, o.y - sz.y / 2);
      TextoSuave(arriba, fs, pos, f.tipo == 1 ? IM_COL32(255, 220, 60, al) : f.tipo == 2 ? IM_COL32(255, 140, 40, al) : IM_COL32(255, 255, 255, al), t.c_str(), 0, float(al) / 255.0f);
    }
    if (fin_t_ >= 0 && !fin_partida_) {   // letrero de fin de ronda
      const int g = demo_ ? sim_->Resumen().ganador : partida_->GanadorRonda();
      const char* txt = g < 0 ? T::T("EMPATE", "DRAW") : g == 0 ? T::T("¡VICTORIA!", "VICTORY!") : T::T("DERROTA", "DEFEAT");
      const ImU32 col = g < 0 ? IM_COL32(230, 230, 230, 255) : g == 0 ? IM_COL32(255, 205, 40, 255) : IM_COL32(230, 70, 60, 255);
      const float fs = 110 * a.s;
      const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, txt), c = a.At(600, 380);
      arriba->AddRectFilled(ImVec2(0, c.y - sz.y * 0.8f), ImVec2(ImGui::GetIO().DisplaySize.x, c.y + sz.y * 0.8f), IM_COL32(0, 0, 0, 150));
      arriba->AddText(ImGui::GetFont(), fs, ImVec2(c.x - sz.x / 2 + 4 * a.s, c.y - sz.y / 2 + 4 * a.s), IM_COL32(0, 0, 0, 255), txt);
      TextoSuave(arriba, fs, ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), col, txt);
      if (!demo_ && hist_ronda_ != partida_->Ronda() && partida_->DueloHumano() >= 0) {   // H14: historial por rival
        hist_ronda_ = partida_->Ronda();
        const Duelo& dh = partida_->Duelos()[size_t(partida_->DueloHumano())];
        if (dh.b >= 0 && dh.b < kMaxJug && !dh.copia) hist_[dh.b] = g < 0 ? 3 : g == 0 ? 1 : 2;
      }
      if (g > 0 && !demo_) {   // H13: por que perdiste: la sinergia que estuvo a una unidad y quien cayo primero
        std::string l1, l2;
        const SinergiaDef* mejor = nullptr;
        int mc = 0, mn = 0;
        for (const SinActiva& sa : partida_->Sinergias(0)) {
          const SinergiaDef& d = eco_.sinergias[size_t(sa.sinergia)];
          for (const Umbral& um : d.umbrales) if (um.n == sa.cuenta + 1 && sa.cuenta > mc) mejor = &d, mc = sa.cuenta, mn = um.n;
        }
        if (mejor) l1 = Texto("por_que_sin") + " " + T::T(mejor->es.c_str(), mejor->en.c_str()) + " (" + std::to_string(mc) + "/" + std::to_string(mn) + ")";
        if (caido_) l2 = Texto("por_que_caido") + " " + T::T(caido_->es, caido_->en);
        const float fl = 30 * a.s;
        float y = c.y - sz.y * 0.8f - 10 * a.s - (fl + 12 * a.s) * float(!l1.empty() + !l2.empty());   // H14: encima del letrero (debajo chocaba con "Pierdes N de vida")
        for (const std::string* l : {&l1, &l2}) {
          if (l->empty()) continue;
          const ImVec2 ls = ImGui::GetFont()->CalcTextSizeA(fl, 1e9f, 0, l->c_str());
          arriba->AddRectFilled(ImVec2(c.x - ls.x / 2 - 12 * a.s, y - 2 * a.s), ImVec2(c.x + ls.x / 2 + 12 * a.s, y + ls.y + 2 * a.s), IM_COL32(0, 0, 0, 170));
          TextoSuave(arriba, fl, ImVec2(c.x - ls.x / 2, y), IM_COL32(255, 235, 200, 255), l->c_str());
          y += ls.y + 8 * a.s;
        }
      }
    } else if (pausa_) {
      S::MenuText(arriba, a.At(600, 380), 80 * a.s, T::T("PAUSA", "PAUSED"), false, false);
    }
    media_ += (std::chrono::duration<double, std::milli>(Reloj::now() - t0).count() - media_) * 0.05;
    if (++cuadros_ == 120)
      REXLOG_INFO("dbz3 tactics: arena {}v{}: {} triangulos, {} vertices, tandas {}, descartados {}, CPU {:.2f} ms/cuadro, {:.0f} FPS; banquillo 3D: {} miniaturas, {} triangulos, CPU {:.2f} ms/cuadro",
                  lado_, lado_, esc_.Cuantos(), esc_.Vertices(), esc_.Tandas(), esc_.Descartados(), media_, double(ImGui::GetIO().Framerate), minis_, esc_b_.Cuantos(), banco_ms_);
  }

  // H11: efectos de las tecnicas y los golpes con texturas de efectos del propio combate de B3 (native_ui "fx_*", sacadas de data_cmn del jugador).
  // Bola de ki que viaja del atacante al objetivo, rayo que cruza la linea, onda en el suelo y estallido al impactar; el color es el de cada personaje.
  // Textura y color de la tecnica de cada unidad LEIDOS del data_cmn del jugador (mod center hd/tactics_fx.py -> fx_nativo.inc; las imagenes las saca native_ui
  // como tkfx/<id>/bola y tkfx/<id>/rayo). Sin entrada (Videl, Satan, Uub, A16, Super Buu...) o con color 0,0,0 vale la tabla a mano de ColorKi.
  struct KiNativo { const char* id; float c[3]; bool bola, rayo; };
  static const KiNativo* KiDe(const char* id) {
#define TKFX(nid, entrada, hbola, hrayo, r, g, b) {nid, {r, g, b}, (hbola) != 0, (hrayo) != 0},
    static const KiNativo t[] = {
#include "fx_nativo.inc"
        {"", {0, 0, 0}, false, false}};
#undef TKFX
    for (const KiNativo& k : t)
      if (!std::strcmp(k.id, id)) return &k;
    return nullptr;
  }
  static void ColorKi(const char* id, float c[3]) {
    if (const KiNativo* n = KiDe(id); n && (n->c[0] + n->c[1] + n->c[2]) > 0.0f) return void(std::copy(n->c, n->c + 3, c));
    struct K { const char* id; float c[3]; };
    static const K t[] = {{"goku", {0.35f, 0.65f, 1.0f}}, {"gokukid", {0.35f, 0.65f, 1.0f}}, {"gohan", {0.55f, 0.75f, 1.0f}}, {"gohankid", {0.9f, 0.7f, 1.0f}},
                          {"vegeta", {0.85f, 0.45f, 1.0f}}, {"trunks", {0.6f, 0.5f, 1.0f}}, {"trunkskid", {0.6f, 0.5f, 1.0f}}, {"goten", {1.0f, 0.75f, 0.3f}},
                          {"piccolo", {1.0f, 0.95f, 0.45f}}, {"krilin", {1.0f, 0.8f, 0.3f}}, {"tenshinhan", {1.0f, 0.9f, 0.4f}}, {"freezer", {1.0f, 0.35f, 0.75f}},
                          {"cooler", {1.0f, 0.4f, 0.3f}}, {"cell", {0.5f, 1.0f, 0.5f}}, {"celljr", {0.5f, 1.0f, 0.5f}}, {"a16", {0.5f, 1.0f, 0.8f}},
                          {"a17", {1.0f, 0.6f, 0.3f}}, {"a18", {1.0f, 0.9f, 0.4f}}, {"gero", {0.8f, 1.0f, 0.5f}}, {"superbuu", {1.0f, 0.55f, 0.8f}},
                          {"buuninio", {1.0f, 0.5f, 0.75f}}, {"dabura", {1.0f, 0.45f, 0.35f}}, {"kaioshin", {0.9f, 0.6f, 1.0f}}, {"broly", {0.5f, 1.0f, 0.55f}},
                          {"bardock", {1.0f, 0.6f, 0.35f}}, {"recoome", {1.0f, 0.55f, 0.3f}}, {"nappa", {1.0f, 0.7f, 0.4f}}, {"saibaman", {0.6f, 1.0f, 0.4f}},
                          {"yamcha", {1.0f, 0.7f, 0.35f}}, {"raditz", {0.9f, 0.5f, 1.0f}}, {"uub", {0.4f, 0.9f, 1.0f}}, {"ginyu", {1.0f, 0.4f, 0.9f}}};
    for (const K& k : t)
      if (!std::strcmp(k.id, id)) return void(std::copy(k.c, k.c + 3, c));
    c[0] = 1.0f, c[1] = 0.85f, c[2] = 0.5f;
  }
  // H13: tecnicas de golpes (sin ki): se pintan como embestida, no como bola de energia
  static bool Fisico(const char* id) {
    static const char* const t[] = {"videl", "satan", "yamcha", "ginyu"};
    for (const char* x : t) if (!std::strcmp(x, id)) return true;
    return false;
  }
  // pasada 0 (antes de las figuras: nada tapa a nadie): rayo, bola de ki, onda en el suelo y estallido; pasada 1 (despues): solo las chispas del golpe, pequenas.
  // H12: todo mas grande y con mas contraste (borde oscuro por fuera, color del personaje, nucleo blanco).
  void PintarFx(ImDrawList* dl, float tp, int pasada) {
    if (fx_.empty() || !sim_ || fin_t_ >= 0) return;   // acabada la ronda el reloj de la simulacion se para: nada de efectos congelados
    const S::Pic impacto = S::Img("fx_impacto"), anillo = S::Img("fx_anillo"), nucleo = S::Img("fx_nucleo"), halo = S::Img("fx_halo"), chispas = S::Img("fx_chispas");
    const S::Pic brillo = S::Img("fx_brillo");   // H14: chorro de energia y bola de brillo del propio combate de B3 (antes se extraian y no se pedian)
    if (!impacto || !nucleo || !halo) return;   // sin las piezas del juego no se dibuja nada propio
    // bola y rayo propios de la tecnica de cada unidad (los del juego nativo, ya coloreados: se dibujan sin tinte); sin ellos, el dibujo de siempre
    const auto tex_de = [&](int u, const char* que) -> S::Pic {
      if (u < 0 || !sim_ || size_t(u) >= sim_->Unidades().size()) return {};
      const std::string& id = sim_->Unidades()[size_t(u)].def.id;
      const KiNativo* n = KiDe(id.c_str());
      if (!n || !(que[0] == 'b' ? n->bola : n->rayo)) return {};
      return S::Img(("tkfx/" + id + "/" + que).c_str());
    };
    const auto tinte = [](const float c[3], float a) { return IM_COL32(int(c[0] * 255), int(c[1] * 255), int(c[2] * 255), int(std::clamp(a, 0.0f, 1.0f) * 255)); };
    const auto sprite = [&](const S::Pic& pic, ImVec2 c, float w, float h, ImU32 col) { S::DrawPic(dl, pic, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec4(0, 0, 0, 0), col); };
    for (size_t i = 0; i < fx_.size();) {
      const Fx& f = fx_[i];
      if (tp > float(f.t1) + 60.0f) { fx_.erase(fx_.begin() + long(i)); continue; }
      ++i;
      if (f.a < 0 || size_t(f.a) >= p_.size() || f.de < 0 || size_t(f.de) >= p_.size() || tp < float(f.t0)) continue;
      float c[3];
      ColorKi(sim_->Unidades()[size_t(f.de)].def.id.c_str(), c);
      float ax, az, bx, bz;
      PosEn(p_[size_t(f.de)], tp, ax, az);
      PosEn(p_[size_t(f.a)], tp, bx, bz);
      ImVec2 o;
      float k;
      if (f.tipo == 0) {   // chispas del golpe (10 ticks)
        const float e = (tp - float(f.t0)) / 10.0f;
        if (pasada == 1 && chispas && e < 1.0f && Proy(bx, kAltoFigura * 0.55f, bz, o, k)) sprite(chispas, o, 0.5f * k * (0.7f + 0.5f * e), 0.5f * k * (0.7f + 0.5f * e), IM_COL32(255, 255, 255, int(220.0f * (1.0f - e))));
        continue;
      }
      const float dur = float(std::max<uint32_t>(1, f.t1 - f.t0)), v = std::clamp((tp - float(f.t0)) / dur, 0.0f, 1.0f), post = std::max(0.0f, tp - float(f.t1)) / 40.0f;
      const bool vuelo = tp < float(f.t1);
      if (pasada == 1) {   // destello breve y pequeno al llegar (no tapa la figura)
        if (!vuelo && post < 0.25f && Proy(bx, kAltoFigura * 0.55f, bz, o, k)) sprite(nucleo, o, 0.5f * k, 0.5f * k, IM_COL32(255, 255, 255, int(200.0f * (1.0f - post * 4.0f))));
        continue;
      }
      if (f.tipo == 2) {   // H13: rayo en linea fino, que se ensancha hacia la punta, ondula y trae chispas (antes: una losa uniforme de media casilla de ancho)
        const float dx = bx - ax, dz = bz - az, n = std::max(0.001f, std::sqrt(dx * dx + dz * dz)), largo = std::max(1.0f, float(f.alcance)) * kAncho;
        const float crece = std::min(1.0f, (tp - float(f.t0)) / (dur * 0.7f)), a = vuelo ? std::min(1.0f, (tp - float(f.t0)) / 6.0f) : std::max(0.0f, 1.0f - post * 1.4f);
        constexpr int kSeg = 14;
        ImVec2 P[kSeg + 1];
        float K[kSeg + 1], W[kSeg + 1];
        bool todo = true;
        for (int i = 0; i <= kSeg && todo; ++i) {
          const float s = float(i) / float(kSeg), d = largo * crece * s;
          todo = Proy(ax + dx / n * d, kAltoFigura * (0.6f - 0.1f * s), az + dz / n * d, P[i], K[i]);
          const float onda = 1.0f + 0.16f * std::sin(14.0f * s - 0.55f * (tp - float(f.t0)));
          W[i] = onda * (0.5f + 0.5f * s) * (s > 0.92f ? (1.0f - s) / 0.08f * 0.6f + 0.4f : 1.0f);   // se ensancha hacia la punta y se redondea al final
        }
        if (!todo) continue;
        const float grueso = std::clamp(0.8f + float(sim_->Unidades()[size_t(f.de)].def.tecnica.dano) / 700.0f, 0.9f, 1.25f);   // las tecnicas fuertes, algo mas gruesas
        const S::Pic tira = tex_de(f.de, "rayo");
        if (tira) {   // el rayo del juego: su tira (bandas a lo largo, centro blanco) estirada de la mano a la punta, con el resplandor del color de la tecnica debajo
          for (int i = 0; i < kSeg; ++i) {
            const float nx0 = -(P[i + 1].y - P[i].y), ny0 = P[i + 1].x - P[i].x, l0 = std::max(0.001f, std::sqrt(nx0 * nx0 + ny0 * ny0));
            const float g0 = 0.30f * grueso * K[i] * W[i], g1 = 0.30f * grueso * K[i + 1] * W[i + 1], ux = nx0 / l0, uy = ny0 / l0;
            dl->AddImageQuad(halo.tex, ImVec2(P[i].x + ux * g0, P[i].y + uy * g0), ImVec2(P[i + 1].x + ux * g1, P[i + 1].y + uy * g1), ImVec2(P[i + 1].x - ux * g1, P[i + 1].y - uy * g1),
                             ImVec2(P[i].x - ux * g0, P[i].y - uy * g0), ImVec2(0.5f, 0), ImVec2(0.5f, 0), ImVec2(0.5f, 1), ImVec2(0.5f, 1), tinte(c, 0.30f * a));
            const float t0 = float(i) / float(kSeg), t1 = float(i + 1) / float(kSeg), h0 = 0.32f * grueso * K[i] * W[i], h1 = 0.32f * grueso * K[i + 1] * W[i + 1];
            dl->AddImageQuad(tira.tex, ImVec2(P[i].x + ux * h0, P[i].y + uy * h0), ImVec2(P[i + 1].x + ux * h1, P[i + 1].y + uy * h1), ImVec2(P[i + 1].x - ux * h1, P[i + 1].y - uy * h1),
                             ImVec2(P[i].x - ux * h0, P[i].y - uy * h0), ImVec2(t0, 0), ImVec2(t1, 0), ImVec2(t1, 1), ImVec2(t0, 1), IM_COL32(255, 255, 255, int(255 * a)));
          }
        }
        for (int capa = tira ? 4 : 0; capa < 4; ++capa) {   // resplandor suave, color, color vivo, nucleo blanco fino (solo sin la tira del juego)
          const float gr = (capa == 0 ? 0.30f : capa == 1 ? 0.20f : capa == 2 ? 0.13f : 0.055f) * grueso;
          const ImU32 col = capa == 0 ? tinte(c, 0.38f * a) : capa == 1 ? tinte(c, 0.85f * a) : capa == 2 ? IM_COL32(int(c[0] * 120 + 135), int(c[1] * 120 + 135), int(c[2] * 120 + 135), int(255 * a)) : IM_COL32(255, 255, 255, int(255 * a));
          for (int i = 0; i < kSeg; ++i) {
            const float nx0 = -(P[i + 1].y - P[i].y), ny0 = P[i + 1].x - P[i].x, l0 = std::max(0.001f, std::sqrt(nx0 * nx0 + ny0 * ny0));
            const float g0 = gr * K[i] * W[i], g1 = gr * K[i + 1] * W[i + 1], ux = nx0 / l0, uy = ny0 / l0;
            dl->AddImageQuad(halo.tex, ImVec2(P[i].x + ux * g0, P[i].y + uy * g0), ImVec2(P[i + 1].x + ux * g1, P[i + 1].y + uy * g1), ImVec2(P[i + 1].x - ux * g1, P[i + 1].y - uy * g1),
                             ImVec2(P[i].x - ux * g0, P[i].y - uy * g0), ImVec2(0.5f, 0), ImVec2(0.5f, 0), ImVec2(0.5f, 1), ImVec2(0.5f, 1), col);
          }
        }
        for (int i = 0; i < 4; ++i) {   // chispas que corren por el rayo
          const float s = std::fmod(0.23f * float(i) + 0.035f * (tp - float(f.t0)), 1.0f);
          const int j = std::min(kSeg, int(s * float(kSeg)));
          sprite(nucleo, P[j], 0.16f * K[j] * grueso, 0.16f * K[j] * grueso, IM_COL32(255, 255, 255, int(220 * a)));
        }
        sprite(halo, P[0], 0.6f * K[0] * grueso, 0.6f * K[0] * grueso, tinte(c, 0.7f * a));   // carga en las manos
        sprite(nucleo, P[0], 0.28f * K[0] * grueso, 0.28f * K[0] * grueso, IM_COL32(255, 255, 255, int(255 * a)));
        sprite(halo, P[kSeg], 0.75f * K[kSeg] * grueso, 0.75f * K[kSeg] * grueso, tinte(c, 0.8f * a));   // cabeza del rayo
        sprite(nucleo, P[kSeg], 0.34f * K[kSeg] * grueso, 0.34f * K[kSeg] * grueso, IM_COL32(255, 255, 255, int(255 * a)));
        continue;
      }
      const bool fisico = Fisico(sim_->Unidades()[size_t(f.de)].def.id.c_str());
      if (vuelo && fisico) {   // H13: tecnica de golpes sin ki (Videl, Satan, Yamcha, Ginyu): embestida con estela de lineas de velocidad, sin bola
        ImVec2 q0, q1;
        float kq0, kq1;
        const float vv = std::min(1.0f, v * 1.6f);
        if (Proy(ax + (bx - ax) * std::max(0.0f, vv - 0.35f), kAltoFigura * 0.5f, az + (bz - az) * std::max(0.0f, vv - 0.35f), q0, kq0) && Proy(ax + (bx - ax) * vv, kAltoFigura * 0.5f, az + (bz - az) * vv, q1, kq1)) {
          const float ang = std::atan2(q1.y - q0.y, q1.x - q0.x), nx = -std::sin(ang), ny = std::cos(ang);
          for (int l = -2; l <= 2; ++l) {
            const float of = float(l) * 0.11f * kq0, largo = (1.0f - 0.2f * std::abs(float(l)));
            dl->AddLine(ImVec2(q0.x + nx * of, q0.y + ny * of), ImVec2(q0.x + (q1.x - q0.x) * largo + nx * of, q0.y + (q1.y - q0.y) * largo + ny * of), l == 0 ? IM_COL32(255, 255, 255, 230) : tinte(c, 0.7f), (l == 0 ? 3.0f : 1.6f));
          }
          sprite(nucleo, q1, 0.5f * kq1, 0.5f * kq1, tinte(c, 0.9f));
        }
      } else if (vuelo) {   // bola de ki (tipo 1) o carga del circulo (tipo 3): viaja y deja estela
        for (int t = 5; t >= 0; --t) {
          const float vv = std::max(0.0f, v - 0.045f * float(t));
          if (Proy(ax + (bx - ax) * vv, kAltoFigura * (0.62f - 0.1f * vv), az + (bz - az) * vv, o, k)) {
            const float r = (0.95f - 0.09f * float(t)) * k, a = 1.0f - 0.13f * float(t);
            if (t == 0) sprite(halo, o, r * 2.6f, r * 2.6f, IM_COL32(10, 10, 30, 150));   // borde oscuro: se ve sobre cualquier suelo
            const S::Pic bolap = t == 0 ? tex_de(f.de, "bola") : S::Pic{};
            if (bolap) {   // la bola de energia del juego (ya coloreada, con su nucleo blanco)
              sprite(bolap, o, r * 3.4f, r * 3.4f, IM_COL32_WHITE);
              continue;
            }
            if (t == 0 && brillo) sprite(brillo, o, r * 3.2f, r * 3.2f, tinte(c, 0.85f));   // H14: brillo del propio juego alrededor de la bola
            sprite(halo, o, r * 1.9f, r * 1.9f, tinte(c, a));
            if (t == 0) sprite(nucleo, o, r * 1.15f, r * 1.15f, IM_COL32(255, 255, 255, 255));
          }
        }
      } else if (post < 1.0f && Proy(bx, kAltoFigura * 0.5f, bz, o, k)) {   // estallido y onda al impactar
        const float a = 1.0f - post;
        const float tam = fisico ? 0.55f : (f.tipo == 3 ? 0.65f : 0.75f);   // H13: el estallido de un circulo o de un golpe fisico es mas pequeno (antes tapaba a los vecinos)
        sprite(impacto, o, (1.2f + 1.3f * post) * tam * k, (1.2f + 1.3f * post) * tam * k, tinte(c, a * 0.9f));
        if (anillo && Proy(bx, 0.0f, bz, o, k)) {   // onda en el suelo (elipse): radio del area, dos frentes
          for (int fr = 0; fr < 2; ++fr) {
            const float rad = ((f.tipo == 3 ? 0.7f + float(f.area) * kAncho * 1.0f : 1.1f) * std::max(0.0f, post - 0.18f * float(fr)) + 0.3f) * (fr ? 0.8f : 1.0f);
            sprite(anillo, o, 2.0f * rad * k * 1.12f, 1.1f * rad * k * 1.12f, IM_COL32(10, 10, 30, int(170 * a)));
            sprite(anillo, o, 2.0f * rad * k, 1.1f * rad * k, tinte(c, a));
            sprite(anillo, o, 1.8f * rad * k, 0.98f * rad * k, IM_COL32(255, 255, 255, int(200 * a)));
          }
        }
      }
    }
  }

  // Interfaz de la maqueta (lienzo de 1600x900 sobre la imagen del juego)
  void Interfaz(ImDrawList* dl, const ImGuiIO& io, double ahora) {
    const Lienzo q = LienzoDe(io);
    const Partida& pa = *partida_;
    const Economia& eco = eco_;
    const Jugador& yo = pa.J(0);
    const bool prep = pa.FaseActual() == Fase::Preparacion;
    if (prep && opc_.reloj > 0 && !fin_partida_ && !tut_) {   // H10: reloj de preparacion opcional: al llegar a 0 empieza el combate con lo que haya
      prep_resto_ -= std::min(io.DeltaTime, 0.25f);
      if (prep_resto_ <= 0) {
        REXLOG_INFO("dbz3 tactics: se acabo el tiempo de preparacion");
        IniciarCombate();
        return;
      }
    }
    const ImU32 naranja = IM_COL32(255, 160, 30, 255), oscuro = IM_COL32(10, 14, 30, 215), amarillo = IM_COL32(255, 220, 60, 255);
    const auto es_en = [](const SinergiaDef& s) { return std::string(T::T(s.es.c_str(), s.en.c_str())); };
    const auto nombre = [&](int unidad) { const Modelo* m = ModeloDe(eco.unidades[size_t(unidad)].id); return std::string(T::T(m->es, m->en)); };
    const auto uidx = [&](const char* id) { for (size_t u = 0; u < eco.unidades.size(); ++u) if (eco.unidades[u].id == id) return int(u); return 0; };
    const auto entrenador = [&](int p) { return p == 0 ? std::string(T::T("Tú", "You")) : nombre(uidx(kEntrenador[p])); };
    const auto cara = [&](int unidad, ImVec2 a, ImVec2 b, ImU32 tinte = IM_COL32_WHITE) {
      const Modelo* m = ModeloDe(eco.unidades[size_t(unidad)].id);
      if (const S::Pic pic = S::Img(("caras/" + std::to_string(m->b3)).c_str())) S::DrawPic(dl, pic, a, b, ImVec4(0, 0, 0, 0), tinte);
      else dl->AddRectFilled(a, b, IM_COL32(40, 48, 80, 255));
    };

    // H11: icono de sinergia = la cara de HUD del propio juego de un personaje que la representa, en un disco con aro dorado (activa) o apagado
    const auto iconoSin = [&](ImVec2 c, float r, const std::string& sid, bool activa) {
      static const std::map<std::string, const char*> emblema = {{"saiyan", "goku"}, {"terricolas", "krilin"}, {"freezer", "freezer"}, {"androides", "a18"},
                                                                  {"misticos", "superbuu"}, {"guerreros_z", "gohan"}, {"fuerza_bruta", "broly"},
                                                                  {"guardianes", "a16"}, {"artilleros", "vegeta"}, {"veloces", "trunks"}, {"regeneradores", "cell"}};
      const auto it = emblema.find(sid);
      const Modelo* m = it != emblema.end() ? ModeloDe(it->second) : nullptr;
      const S::Pic pic = m ? S::Img(("caras/" + std::to_string(m->b3)).c_str()) : S::Pic{};
      dl->AddCircleFilled(c, r + 2.5f, activa ? IM_COL32(255, 200, 50, 255) : IM_COL32(70, 76, 100, 255), 28);
      if (pic) {   // recorte cuadrado del centro de la cara (las caras son apaisadas)
        const float ar = pic.w / pic.h, u0 = 0.5f - 0.5f / ar, u1 = 0.5f + 0.5f / ar;
        dl->AddImageRounded(pic.tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(u0, 0), ImVec2(u1, 1), activa ? IM_COL32_WHITE : IM_COL32(120, 120, 130, 255), r);
      } else dl->AddCircleFilled(c, r, IM_COL32(30, 36, 60, 255), 28);
      dl->AddCircle(c, r + 2.5f, IM_COL32(0, 0, 0, 210), 28, 1.5f);
    };

    // arriba: ronda, fase, tiempo y unidades en el tablero
    Caja(dl, q, 500, 8, 1100, 56, naranja);
    const char* fase = pa.FaseActual() == Fase::Preparacion ? T::T("PREPARACIÓN", "PREPARATION") : pa.FaseActual() == Fase::Combate ? T::T("COMBATE", "COMBAT")
                       : pa.FaseActual() == Fase::Resultado ? T::T("RESULTADO", "RESULT") : T::T("FIN", "END");
    char tiempo[32];
    if (prep && opc_.reloj > 0) std::snprintf(tiempo, sizeof tiempo, "%02d:%02d", int(prep_resto_ + 0.99f) / 60, int(prep_resto_ + 0.99f) % 60);
    else if (prep) std::snprintf(tiempo, sizeof tiempo, "%s", T::T("SIN LÍMITE", "NO LIMIT"));
    else { const int s = sim_ ? int(sim_->Tick() / sim::kHz) : 0; std::snprintf(tiempo, sizeof tiempo, "%02d:%02d", s / 60, s % 60); }
    Txt(dl, q, 575, 32, 26, std::string(T::T("RONDA ", "ROUND ")) + std::to_string(pa.Ronda()), 1);
    Txt(dl, q, 745, 32, 30, fase, 1, true);
    Txt(dl, q, 925, 32, 26, tiempo, 1, prep && opc_.reloj > 0 && prep_resto_ < 10.0f);
    Txt(dl, q, 1040, 32, 26, std::to_string(pa.EnTablero(0)) + "/" + std::to_string(pa.TopeTablero(0)), 1);
    // dsTAC: paso de preparacion a combate: banda con "COMBATE" que se apaga en 1,2 s (transicion, no corte)
    if (transicion_t0_ > 0 && ahora - transicion_t0_ < 1.2) {
      const float k = float(ahora - transicion_t0_);
      const int al = int(235 * std::clamp(1.0f - (k - 0.55f) / 0.65f, 0.0f, 1.0f));
      dl->AddRectFilled(ImVec2(0, q.At(0, 372).y), ImVec2(io.DisplaySize.x, q.At(0, 478).y), IM_COL32(0, 0, 0, al / 2));
      const char* txt = T::T("¡COMBATE!", "COMBAT!");
      const float fs = 70 * q.s;
      const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, txt), c = q.At(800, 425);
      TextoSuave(dl, fs, ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), IM_COL32(255, 220, 60, al), txt, 0, float(al) / 255.0f);
    }
    // arriba a la izquierda: titulo y salida (H12: mas pequenos, pegados al borde)
    Txt(dl, q, 16, 26, 34, "Dragon Tactics", 0, true);
    Txt(dl, q, 300, 36, 18, "BETA", 0, false, true);
    if (prep) {
      Caja(dl, q, 16, 52, 180, 84, Elegido(Zona::Botones, 3) ? amarillo : naranja);
      Txt(dl, q, 98, 68, 20, T::T("Volver al HUB", "Back to the HUB"), 1, Elegido(Zona::Botones, 3), false, 150);
      Marcar(Zona::Botones, 3, q.At(16, 52), q.At(180, 84));
      Caja(dl, q, 188, 52, 296, 84, naranja);   // Tutorial (solo raton; con el mando, Select; con el teclado, T)
      Txt(dl, q, 242, 68, 20, Texto("tut_boton"), 1, false, false, 100);
      Marcar(Zona::Ayuda, 0, q.At(188, 52), q.At(296, 84));
    }
    if (ahora - salir_t_ < 3.0) Txt(dl, q, 800, 80, 24, T::T("Pulsa B otra vez para salir al HUB", "Press B again to go back to the HUB"), 1, true);
    else if (pa.DueloHumano() >= 0 && !fin_partida_) {
      const Duelo& d = pa.Duelos()[size_t(pa.DueloHumano())];
      if (d.b == kNeutral) Txt(dl, q, 800, 80, 22, T::T("Ronda neutral: gana para conseguir oro y una cápsula", "Neutral round: win to earn gold and a capsule"), 1, true, false, 560);
      else Txt(dl, q, 800, 80, 22, std::string(d.copia ? T::T("Rival: copia del equipo de ", "Rival: copy of ") : T::T("Rival: ", "Rival: ")) + entrenador(d.b), 1, true, false, 480);
    }

    // izquierda: tira de sinergias (solo las que tienen algun miembro en el tablero): icono, nombre y cuantos hay / el siguiente tramo; el detalle, en la ficha
    {
      const std::vector<SinActiva> sin = pa.Sinergias(0);
      nsin_ = int(sin.size());
      if (zc_ == Zona::Sin && ic_ >= nsin_) zc_ = Zona::Tablero, ic_ = 0;
      int idx_sin = -1;
      float y = 100;
      for (const SinActiva& s : sin) {
        ++idx_sin;
        const SinergiaDef& d = eco.sinergias[size_t(s.sinergia)];
        int sig = d.umbrales.back().n;
        for (const Umbral& u : d.umbrales) if (u.n > s.cuenta) { sig = u.n; break; }
        const bool activa = s.umbral >= 0, el = Elegido(Zona::Sin, idx_sin);
        Caja(dl, q, 8, y, 240, y + 42, el ? amarillo : activa ? IM_COL32(255, 200, 50, 255) : IM_COL32(90, 96, 120, 255), oscuro, el ? 3.0f : 1.5f);
        Marcar(Zona::Sin, idx_sin, q.At(8, y), q.At(240, y + 42));
        iconoSin(q.At(32, y + 21), 15 * q.s, d.id, activa);
        Txt(dl, q, 54, y + 21, 20, es_en(d), 0, activa, !activa, 136);
        Txt(dl, q, 234, y + 21, 20, std::to_string(s.cuenta) + "/" + std::to_string(sig), 2, activa, !activa);
        y += 46;
      }
    }
    // H10: ficha de la sinergia enfocada (raton encima o mando): que hace en cada tramo, cuales estan activos y que unidades la dan
    if (zc_ == Zona::Sin && ic_ >= 0 && ic_ < nsin_) {
      const SinActiva sa = pa.Sinergias(0)[size_t(ic_)];
      const SinergiaDef& d = eco.sinergias[size_t(sa.sinergia)];
      std::string quien;
      for (const UnidadEco& u : eco.unidades) if (u.sin[0] == sa.sinergia || u.sin[1] == sa.sinergia) quien += (quien.empty() ? "" : ", ") + nombre(int(&u - eco.unidades.data()));
      const float h = 92 + 30.0f * float(d.umbrales.size()) + 44;
      S::Frame(dl, q.At(250, 100), q.At(696, 100 + h), q.s);
      Txt(dl, q, 473, 130, 26, es_en(d) + "  (" + std::to_string(sa.cuenta) + ")", 1, true, false, 400);
      for (size_t k = 0; k < d.umbrales.size(); ++k) {
        const Umbral& u = d.umbrales[k];
        std::string t = std::to_string(u.n) + T::T(" miembros: ", " members: ");
        const auto suma = [&](int v, const char* es, const char* en) { if (v) t += std::string(t.back() == ' ' ? "" : "  ") + T::T(es, en) + std::to_string(v) + "%"; };
        suma(u.vida, "Vida +", "HP +"), suma(u.ataque, "ataque +", "attack +"), suma(u.defensa, "defensa +", "defense +");
        const bool act = int(k) <= sa.umbral;
        Txt(dl, q, 272, 172 + 30.0f * float(k), 20, t, 0, act, !act, 410);
      }
      TextoSuave(dl, 18 * q.s, q.At(272, 172 + 30.0f * float(d.umbrales.size()) + 8), IM_COL32(220, 230, 255, 255), (std::string(T::T("Unidades: ", "Units: ")) + quien).c_str(), 404 * q.s);
    }
    // derecha: los entrenadores del torneo por vida (los eliminados al final, por puesto) con el rival de esta ronda marcado en rojo (H12: tira estrecha; el nombre completo, al pasar por encima)
    {
      std::vector<int> orden(static_cast<size_t>(pa.N()));
      for (int p = 0; p < pa.N(); ++p) orden[size_t(p)] = p;
      std::stable_sort(orden.begin(), orden.end(), [&](int x, int y) {
        const Jugador &jx = pa.J(x), &jy = pa.J(y);
        return jx.vivo != jy.vivo ? jx.vivo : jx.vivo ? jx.vida > jy.vida : jx.puesto < jy.puesto;
      });
      const Duelo* dh = pa.DueloHumano() >= 0 ? &pa.Duelos()[size_t(pa.DueloHumano())] : nullptr;
      for (int k = 0; k < pa.N(); ++k) {
        const int p = orden[size_t(k)];
        const Jugador& jp = pa.J(p);
        const float y = 70 + 44.0f * float(k);
        const bool rival = dh && p == dh->b;
        Caja(dl, q, 1396, y, 1592, y + 40, p == 0 ? amarillo : rival ? (dh->copia ? naranja : IM_COL32(230, 70, 60, 255)) : IM_COL32(90, 96, 120, 255), IM_COL32(10, 14, 30, 190),
             p == 0 ? 3.0f : rival ? 2.5f : 1.5f);
        dl->AddRectFilled(q.At(1400, y + 3), q.At(1434, y + 37), IM_COL32(24, 30, 54, 255));
        cara(uidx(kEntrenador[p]), q.At(1400, y + 3), q.At(1434, y + 37), jp.vivo ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
        Txt(dl, q, 1440, y + 12, 18, entrenador(p), 0, p == 0, !jp.vivo, 100);
        Txt(dl, q, 1587, y + 12, 20, jp.vivo ? std::to_string(jp.vida) : "#" + std::to_string(jp.puesto), 2, false, !jp.vivo);
        if (jp.vivo) {
          const float f = std::clamp(float(jp.vida) / float(eco.vida_entrenador), 0.0f, 1.0f);
          dl->AddRectFilled(q.At(1440, y + 28), q.At(1587, y + 35), IM_COL32(0, 0, 0, 200), 3 * q.s);
          dl->AddRectFilled(q.At(1440, y + 28), q.At(1440 + 147 * f, y + 35), f > 0.5f ? IM_COL32(80, 220, 90, 255) : f > 0.25f ? IM_COL32(240, 200, 50, 255) : IM_COL32(230, 70, 60, 255), 3 * q.s);
          dl->AddRect(q.At(1440, y + 28), q.At(1587, y + 35), IM_COL32(0, 0, 0, 230), 3 * q.s, 0, 1.5f * q.s);
        }
      }
    }
    // capsulas (H12): en preparacion, una tira que se abre al pedirla (clic, o el cursor del mando sobre ella); el panel con el inventario por tipo sale encima
    const int ncap = int(eco.capsulas.size());
    if (prep) {
      int total = 0;
      for (int c = 0; c < ncap; ++c) total += yo.inv[c];
      const bool abierta = caps_abiertas_ || zc_ == Zona::Caps;
      Caja(dl, q, 8, 724, 264, 766, abierta ? amarillo : capmano_ >= 0 ? IM_COL32(90, 230, 255, 255) : naranja, IM_COL32(20, 34, 70, 230), abierta || capmano_ >= 0 ? 3.0f : 2.0f);
      Botella(dl, q.At(30, 745), 30 * q.s, capmano_ >= 0 ? capmano_ : 0);
      Txt(dl, q, 50, 745, 20, Texto("caps_titulo"), 0, abierta, false, 150);
      Txt(dl, q, 256, 745, 20, "x" + std::to_string(total), 2, total > 0, total == 0);
      Marcar(Zona::Ayuda, 1, q.At(8, 724), q.At(264, 766));
      if (abierta) {
        const int filas = (ncap + kColsCaps - 1) / kColsCaps;
        const float y1 = 716, y0 = y1 - (46 + 50.0f * float(filas) + 50);
        S::Frame(dl, q.At(8, y0), q.At(424, y1), q.s);
        Txt(dl, q, 216, y0 + 26, 22, Texto("caps_titulo"), 1, true);
        for (int c = 0; c < ncap; ++c) {
          const float x0 = 16 + 133.0f * float(c % kColsCaps), cy0 = y0 + 46 + 50.0f * float(c / kColsCaps);
          const bool el = Elegido(Zona::Caps, c), tengo = yo.inv[c] > 0;
          Caja(dl, q, x0, cy0, x0 + 129, cy0 + 46, el ? amarillo : capmano_ == c ? IM_COL32(90, 230, 255, 255) : tengo ? ColorCaps(c) : IM_COL32(70, 76, 100, 255), IM_COL32(18, 28, 62, 235),
               el || capmano_ == c ? 4.0f : 1.5f);
          Botella(dl, q.At(x0 + 15, cy0 + 23), 36 * q.s, c, !tengo);
          Txt(dl, q, x0 + 32, cy0 + 14, 18, std::string(T::T(eco.capsulas[size_t(c)].es.c_str(), eco.capsulas[size_t(c)].en.c_str())), 0, el, !tengo, 92);
          Txt(dl, q, x0 + 32, cy0 + 33, 18, "x" + std::to_string(yo.inv[c]), 0, tengo, !tengo);
          Marcar(Zona::Caps, c, q.At(x0, cy0), q.At(x0 + 129, cy0 + 46));
        }
        const float by = y0 + 46 + 50.0f * float(filas);
        const bool el = Elegido(Zona::Caps, ncap), puede = yo.oro >= eco.precio_capsula;
        Caja(dl, q, 16, by, 416, by + 36, el ? amarillo : naranja, IM_COL32(20, 34, 70, 230), el ? 4.0f : 2.0f);
        Txt(dl, q, 28, by + 18, 20, Texto("caps_comprar"), 0, el, !puede, 270);
        Moneda(dl, q.At(396, by + 18), 9 * q.s);
        Txt(dl, q, 384, by + 18, 20, std::to_string(eco.precio_capsula), 2, false, !puede);
        Marcar(Zona::Caps, ncap, q.At(16, by), q.At(416, by + 36));
      }
    }
    // ficha de la unidad bajo el cursor (o la que llevas en la mano), o la descripcion de la capsula bajo el cursor (ambas solo al pasar por encima)
    if (prep && zc_ == Zona::Caps) {
      S::Frame(dl, q.At(8, 340), q.At(424, 462), q.s);
      if (ic_ < ncap) {
        const CapsulaDef& c = eco.capsulas[size_t(ic_)];
        Botella(dl, q.At(34, 372), 44 * q.s, ic_);
        Txt(dl, q, 62, 372, 24, std::string(T::T(c.es.c_str(), c.en.c_str())), 0, true, false, 250);
        Txt(dl, q, 414, 372, 20, Texto("caps_tienes") + " " + std::to_string(yo.inv[ic_]), 2, true);
        TextoSuave(dl, 20 * q.s, q.At(24, 400), IM_COL32(220, 230, 255, 255), Describir(c).c_str(), 384 * q.s);
      } else {
        TextoSuave(dl, 20 * q.s, q.At(24, 360), IM_COL32(220, 230, 255, 255), (Texto("caps_sin_oro") + " " + std::to_string(eco.precio_capsula)).c_str(), 384 * q.s);
      }
    } else if (prep) {
      int u = -1, est = 1, c0 = -1, c1 = -1;
      const auto de = [&](const Pieza& z) { u = z.unidad, est = z.estrellas, c0 = z.caps[0], c1 = z.caps[1]; };
      if (zc_ == Zona::Tienda && yo.tienda[ic_] >= 0) u = yo.tienda[ic_];
      else if (zc_ == Zona::Banco && yo.pz[ic_].hay()) de(yo.pz[ic_]);
      else if (zc_ == Zona::Tablero && yo.pz[kBanco + ic_].hay()) de(yo.pz[kBanco + ic_]);
      else if (mano_ >= 0 && yo.pz[mano_].hay()) de(yo.pz[mano_]);
      if (u >= 0 && ficha_ver_) Ficha(dl, q, u, est, c0, c1);   // H13: el cursor parte en la tienda y la ficha no sale hasta que el jugador mueve el raton o el mando
    }
    // banquillo de 8 huecos (solo en preparacion): miniaturas 3D de B3 (la misma Escena de figuras, puestas bajo cada hueco); si no se puede, el retrato.
    // Los fondos y las figuras van en una ventana de ImGui propia (su lista de dibujo aparte: las listas de 16 bits no admiten tantos vertices y
    // pintar figuras en la lista de la interfaz la corrompia); los bordes, estrellas y retratos van encima, en la lista de la interfaz.
    bool mini[kBanco] = {};
    int minis = 0;
    if (prep) {
      esc_b_.Empezar();
      const auto t_banco = Reloj::now();
      int en_banco = 0;
      for (const Pieza& z : yo.pz) en_banco += z.hay() && &z < yo.pz + kBanco;
      // medido: cada miniatura cuesta ~0,9 ms de CPU por cuadro (unos 6000 triangulos): hasta 4 en el banquillo (3,7 ms) y con mas, retratos
      for (int i = 0; i < kBanco && en_banco <= 4; ++i) {
        const Pieza& z = yo.pz[i];
        float x, zz, k;
        ImVec2 o;
        if (!z.hay() || !Suelo(q.At(400 + 100.0f * float(i) + 47, 770), x, zz) || !Proy(x, 0, zz, o, k)) continue;
        const Modelo* m = ModeloDe(eco.unidades[size_t(z.unidad)].id);
        mini[i] = DibujarEstrellas(&esc_b_, cam_, m, z.estrellas, Mundo(x, 0, zz), 50.0f * q.s / k * kEscala, std::atan2(eje_[0], eje_[1]) - 1.5707963f,
                                   0, float(ahora) + 0.37f * float(i));
        minis += mini[i];
      }
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("##dt_banco", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImDrawList* capa = ImGui::GetWindowDrawList();
      for (int i = 0; i < kBanco; ++i) capa->AddRectFilled(q.At(400 + 100.0f * float(i), 714), q.At(494 + 100.0f * float(i), 776), IM_COL32(8, 10, 24, 200), 6 * q.s);
      esc_b_.Pintar(capa);
      ImGui::End();
      banco_ms_ += (std::chrono::duration<double, std::milli>(Reloj::now() - t_banco).count() - banco_ms_) * 0.05;
    }
    minis_ = minis;
    PintarDestellos(ImGui::GetForegroundDrawList(), dl, q, ahora);
    for (int i = 0; i < kBanco && prep; ++i) {
      const float x0 = 400 + 100.0f * float(i), y0 = 714, x1 = x0 + 94, y1 = y0 + 62;
      const Pieza& z = yo.pz[i];
      const bool el = Elegido(Zona::Banco, i);
      Caja(dl, q, x0, y0, x1, y1, el ? amarillo : mano_ == i ? IM_COL32(90, 230, 255, 255) : z.hay() ? ColorRango(eco.unidades[size_t(z.unidad)].coste) : IM_COL32(120, 130, 170, 255), IM_COL32(0, 0, 0, 0), el || mano_ == i ? 4.0f : 1.5f);
      if (el) dl->AddRectFilled(q.At(x0 + 3, y0 + 3), q.At(x1 - 3, y1 - 3), IM_COL32(255, 255, 255, 22), 5 * q.s);   // respuesta al pasar por encima
      if (z.hay() && !mini[i]) cara(z.unidad, q.At(x0 + 3, y0 + 3), q.At(x1 - 3, y1 - 3));
      if (z.hay()) Estrellas(dl, q.At((x0 + x1) * 0.5f, y0 + 9), 6 * q.s, z.estrellas);
      for (int h = 0; h < 2; ++h)
        if (z.hay() && z.caps[h] >= 0) {
          Botella(dl, q.At(x0 + 12 + 14.0f * float(h), y1 - 10), 24 * q.s, z.caps[h]);
          dl->AddCircle(q.At(x0 + 12 + 14.0f * float(h), y1 - 10), 6 * q.s, IM_COL32(0, 0, 0, 255), 0, 1.5f);
        }
      Marcar(Zona::Banco, i, q.At(x0, y0), q.At(x1, y1));
    }
    // abajo a la izquierda: nivel, EXP, oro y los dos botones (en el combate se recoge a una pastilla con el nivel y el oro)
    if (!prep) {
      Caja(dl, q, 8, 852, 270, 892, naranja);
      Txt(dl, q, 20, 872, 20, std::string(T::T("NIVEL ", "LEVEL ")) + std::to_string(yo.nivel), 0, true);
      Moneda(dl, q.At(254, 872), 11 * q.s);
      Txt(dl, q, 226, 872, 22, std::to_string(yo.oro), 2, true);
    } else {
      Caja(dl, q, 8, 800, 388, 892, naranja);
      const bool max = yo.nivel >= eco.NivelMax();
      Txt(dl, q, 20, 816, 24, std::string(T::T("NIVEL ", "LEVEL ")) + std::to_string(yo.nivel), 0, true);
      const int need = max ? 1 : eco.exp_subir[size_t(yo.nivel - 1)];
      dl->AddRectFilled(q.At(140, 808), q.At(240, 822), IM_COL32(0, 0, 0, 200), 3 * q.s);
      dl->AddRectFilled(q.At(140, 808), q.At(140 + 100 * (max ? 1.0f : float(yo.exp) / float(need)), 822), IM_COL32(70, 170, 255, 255), 3 * q.s);
      dl->AddRect(q.At(140, 808), q.At(240, 822), IM_COL32(0, 0, 0, 230), 3 * q.s, 0, 1.5f * q.s);
      Txt(dl, q, 248, 816, 18, max ? "MAX" : std::to_string(yo.exp) + "/" + std::to_string(need), 0);
      if (yo.nivel <= eco.NivelMax()) {   // H14: probabilidades de coste 1/2/3/4 siempre visibles junto al nivel (como la barra de nivel de TFT)
        const auto& w = eco.peso_coste[size_t(yo.nivel - 1)];
        int tot = 0;
        for (int v : w) tot += v;
        std::string t;
        for (int v : w) t += (t.empty() ? "" : "/") + std::to_string(tot ? 100 * v / tot : 0);
        Txt(dl, q, 298, 816, 15, t + "%", 0, false, true, 88);
      }
      {   // H10: lo que ganaras al acabar la ronda: interes y racha (victorias o derrotas seguidas)
        std::string t = std::string(T::T("Interés +", "Interest +")) + std::to_string(std::min(eco.interes_tope, yo.oro / eco.interes_cada));
        if (std::abs(yo.racha) >= 2) t += std::string(" · ") + (yo.racha > 0 ? T::T("Racha ", "Win streak ") : T::T("Derrotas ", "Loss streak ")) + std::to_string(std::abs(yo.racha)) + (pa.OroRacha(yo.racha) ? " (+" + std::to_string(pa.OroRacha(yo.racha)) + ")" : "");
        Txt(dl, q, 20, 842, 18, t, 0, false, false, 360);
      }
      Moneda(dl, q.At(364, 816), 12 * q.s);
      Txt(dl, q, 350, 816, 28, std::to_string(yo.oro), 2, true);
      const struct { const char* es; const char* en; int precio; } btn[2] = {{"Comprar EXP", "Buy EXP", eco.precio_exp}, {"Actualizar", "Refresh", eco.precio_actualizar}};
      for (int b = 0; b < 2; ++b) {
        const float x0 = 16 + 186.0f * float(b), y0 = 856;
        const bool el = Elegido(Zona::Botones, b), puede = yo.oro >= btn[b].precio && (b == 1 || !max);
        Caja(dl, q, x0, y0, x0 + 178, y0 + 32, el ? amarillo : naranja, IM_COL32(20, 34, 70, 230), el ? 4.0f : 2.0f);
        Txt(dl, q, x0 + 8, y0 + 16, 18, T::T(btn[b].es, btn[b].en), 0, el, !puede, 124);
        Moneda(dl, q.At(x0 + 160, y0 + 16), 8 * q.s);
        Txt(dl, q, x0 + 148, y0 + 16, 20, std::to_string(btn[b].precio), 2, false, !puede);
        Marcar(Zona::Botones, b, q.At(x0, y0), q.At(x0 + 178, y0 + 32));
      }
    }
    // la tienda (solo en preparacion): 5 cartas con retrato, sus dos sinergias (iconos) y precio
    for (int c = 0; c < eco.tienda && prep; ++c) {
      const float x0 = 396 + 216.0f * float(c), y0 = 800, x1 = x0 + 210, y1 = 892;
      const int u = yo.tienda[c];
      const bool el = Elegido(Zona::Tienda, c);
      if (u < 0) {
        Caja(dl, q, x0, y0, x1, y1, IM_COL32(70, 76, 100, 255), IM_COL32(8, 10, 24, 160), 1.5f);
        Marcar(Zona::Tienda, c, q.At(x0, y0), q.At(x1, y1));
        continue;
      }
      const UnidadEco& ue = eco.unidades[size_t(u)];
      const bool puede = yo.oro >= ue.coste;
      Caja(dl, q, x0, y0, x1, y1, el ? amarillo : yo.bloq ? IM_COL32(90, 230, 255, 255) : ColorRango(ue.coste), IM_COL32(18, 28, 62, 235), el ? 4.0f : yo.bloq ? 3.0f : 2.5f + 0.5f * float(ue.coste));
      if (el) dl->AddRectFilled(q.At(x0 + 3, y0 + 3), q.At(x1 - 3, y1 - 3), IM_COL32(255, 255, 255, 22), 5 * q.s);   // respuesta al pasar por encima
      cara(u, q.At(x0 + 6, y0 + 6), q.At(x0 + 6 + 88, y0 + 6 + 55), puede ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255));
      Txt(dl, q, x0 + 102, y0 + 20, 20, nombre(u), 0, el, !puede, 102);
      bool en_tablero = false;
      for (int sl = kBanco; sl < kSlots; ++sl) en_tablero |= yo.pz[sl].unidad == u;
      static std::vector<SinActiva> sin_yo;   // H14: cada icono lleva el numero de miembros que tendras al comprar (verde = sube, borde amarillo = ya la llevas)
      if (c == 0) sin_yo = pa.Sinergias(0);
      for (int h = 0; h < 2; ++h) {
        int cuenta = 0;
        for (const SinActiva& sa : sin_yo) if (sa.sinergia == ue.sin[h]) cuenta = sa.cuenta;
        const ImVec2 ic = q.At(x0 + 114 + 30.0f * float(h), y0 + 48);
        iconoSin(ic, 11 * q.s, eco.sinergias[size_t(ue.sin[h])].id, true);
        if (cuenta > 0) dl->AddCircle(ic, 13 * q.s, IM_COL32(255, 220, 60, 255), 0, 2.0f * q.s);
        Txt(dl, q, x0 + 114 + 30.0f * float(h) + 12, y0 + 62, 15, std::to_string(cuenta + (en_tablero ? 0 : 1)), 0, !en_tablero, en_tablero, 28);
      }
      Moneda(dl, q.At(x1 - 20, y1 - 16), 11 * q.s);
      Txt(dl, q, x1 - 34, y1 - 16, 24, std::to_string(ue.coste), 2, false, !puede);
      if (!puede) TextoSuave(dl, 16 * q.s, q.At(x0 + 44, y1 - 25), IM_COL32(255, 120, 110, 255), (Texto("faltan_oro") + " " + std::to_string(ue.coste - yo.oro)).c_str(), 100 * q.s);   // subTAC: por que no se puede comprar, de un vistazo
      int tengo = 0;
      for (const Pieza& z : yo.pz) tengo += z.unidad == u ? pa.Peso(z.estrellas) : 0;
      if (tengo) Txt(dl, q, x0 + 10, y1 - 16, 20, "x" + std::to_string(tengo), 0, true, false, 80);
      // H14: si esta compra completa 3 copias, la tarjeta lo avisa (borde verde y "Sube a N estrellas"), como el resaltado de los autobattlers
      const int sube = tengo + 1 == 27 ? 4 : tengo + 1 == 9 ? 3 : tengo + 1 == 3 ? 2 : 0;
      if (sube && puede) {
        const int pulso = 180 + int(75.0f * std::sin(float(ahora) * 6.0f));   // blanco que late: el verde ya es el rango de coste 2
        Caja(dl, q, x0 - 3, y0 - 3, x1 + 3, y1 + 3, IM_COL32(255, 255, 255, pulso), IM_COL32(0, 0, 0, 0), 3.5f);
        Txt(dl, q, x0 + 44, y1 - 16, 16, Texto("sube_est") + " " + std::to_string(sube) + "*", 0, true, false, 120);
      }
      Marcar(Zona::Tienda, c, q.At(x0, y0), q.At(x1, y1));
    }
    // H14: arrastrando una unidad con el raton: su cara sigue al cursor y la tienda se ofrece como "soltar para vender"
    const bool arrastrando = kArrastrar && prep && arr_ >= 0 && ImGui::IsMouseDown(0) && yo.pz[arr_].hay() && std::abs(io.MousePos.x - arr_p_.x) + std::abs(io.MousePos.y - arr_p_.y) > 14.0f;
    if (arrastrando) {
      const float r = 34 * q.s;
      cara(yo.pz[arr_].unidad, ImVec2(io.MousePos.x - r, io.MousePos.y - r * 0.8f), ImVec2(io.MousePos.x + r, io.MousePos.y + r * 0.8f));
      dl->AddRect(ImVec2(io.MousePos.x - r, io.MousePos.y - r * 0.8f), ImVec2(io.MousePos.x + r, io.MousePos.y + r * 0.8f), IM_COL32(90, 230, 255, 255), 4 * q.s, 0, 3.0f);
      dl->AddRectFilled(q.At(396, 800), q.At(1476, 892), IM_COL32(120, 20, 20, 150), 6 * q.s);
      Txt(dl, q, 936, 846, 26, Texto("soltar_vender"), 1, true, false, 900);
    }
    // H14: vista previa de sinergias al mover una unidad cogida: cuantos miembros tendra cada una antes > despues (* = llega a un tramo)
    std::string previa;
    if (kPrevia && prep && mano_ >= 0 && yo.pz[mano_].hay() && (zc_ == Zona::Tablero || zc_ == Zona::Banco) && !arrastrando) {
      std::array<int, kSlots> antes{}, desp{};
      for (int sl = 0; sl < kSlots; ++sl) antes[size_t(sl)] = desp[size_t(sl)] = yo.pz[sl].hay() ? yo.pz[sl].unidad : -1;
      std::swap(desp[size_t(mano_)], desp[size_t(zc_ == Zona::Banco ? ic_ : kBanco + ic_)]);
      const auto contar = [&](const std::array<int, kSlots>& v, int k) {
        std::set<int> m;
        for (int sl = kBanco; sl < kSlots; ++sl)
          if (v[size_t(sl)] >= 0 && (eco.unidades[size_t(v[size_t(sl)])].sin[0] == k || eco.unidades[size_t(v[size_t(sl)])].sin[1] == k)) m.insert(v[size_t(sl)]);
        return int(m.size());
      };
      for (size_t k = 0; k < eco.sinergias.size(); ++k) {
        const int a0 = contar(antes, int(k)), b0 = contar(desp, int(k));
        if (a0 == b0) continue;
        bool tramo = false;
        for (const Umbral& um : eco.sinergias[k].umbrales) tramo |= um.n == b0;
        previa += (previa.empty() ? "" : "   ") + es_en(eco.sinergias[k]) + " " + std::to_string(a0) + " > " + std::to_string(b0) + (tramo ? "*" : "");
      }
    }
    if (EnPrueba()) { static std::string previa_ant; if (previa != previa_ant) { previa_ant = previa; REXLOG_INFO("dbz3 tactics: previa [{}] arrastrando {}", previa, arrastrando); } }
    // avisos y ayuda: en preparacion sobre el banquillo, bajo el tablero; en combate, abajo del todo
    if (!fin_partida_ && !(fin_t_ >= 0 && !prep)) {
      const float hy = prep ? 699.0f : 872.0f;
      Caja(dl, q, 440, hy - 13, 1160, hy + 13, naranja, IM_COL32(10, 14, 30, 190), 1.5f);
      if (ahora - aviso_t_ < aviso_dura_ && prep) Txt(dl, q, 800, hy, 20, aviso_, 1, true, false, 700);
      else if (!previa.empty()) Txt(dl, q, 800, hy, 18, std::string(Texto("previa_sin")) + " " + previa, 1, true, false, 700);
      else if (prep && (capmano_ >= 0 || zc_ == Zona::Caps)) Txt(dl, q, 800, hy, 18, Texto("caps_ayuda"), 1, false, false, 700);
      else if (prep && zc_ == Zona::Botones && ic_ == 0 && yo.nivel <= eco.NivelMax()) {   // H10: probabilidades de la tienda a este nivel
        const auto& w = eco.peso_coste[size_t(yo.nivel - 1)];
        int suma = 0;
        for (int v : w) suma += v;
        std::string t = std::string(T::T("Probabilidades de la tienda (coste 1/2/3/4): ", "Shop odds (cost 1/2/3/4): "));
        for (size_t k = 0; k < w.size(); ++k) t += (k ? " / " : "") + std::to_string(suma ? w[k] * 100 / suma : 0);
        Txt(dl, q, 800, hy, 18, t + " %", 1, false, false, 700);
      } else if (prep && zc_ == Zona::Botones && ic_ == 4)
        Txt(dl, q, 800, hy, 18, T::T("Bloquear: la tienda se conserva en la ronda siguiente hasta que la actualices (RB)", "Lock: keeps the shop for the next round until you refresh it (RB)"), 1, false, false, 700);
      else if (prep) Txt(dl, q, 800, hy, 18, T::T("A: elegir / comprar  ·  X: vender  ·  B: cancelar", "A: select / buy  ·  X: sell  ·  B: cancel"), 1, false, false, 700);
      else Txt(dl, q, 800, hy, 18, std::string(T::T("Y: velocidad (x", "Y: speed (x")) + std::to_string(vel_) + T::T(")  ·  Start: pausa  ·  B dos veces: HUB", ")  ·  Start: pause  ·  B twice: HUB"), 1, false, false, 700);
    }
    // boton Listo y bloquear la tienda: abajo a la derecha (solo en preparacion)
    if (prep) {
      const float x0 = 1484, y0 = 800, x1 = 1592, y1 = 846;
      const bool el = Elegido(Zona::Botones, 2);
      Caja(dl, q, x0, y0, x1, y1, el ? amarillo : naranja, IM_COL32(40, 90, 40, 235), el ? 4.0f : 2.0f);
      Txt(dl, q, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 24, T::T("¡LISTO!", "READY!"), 1, el, false, 98);
      Marcar(Zona::Botones, 2, q.At(x0, y0), q.At(x1, y1));
      // H10: bloquear la tienda (se conserva de una ronda a otra; Actualizar la desbloquea). Mando RB (en las pruebas, tecla 3).
      const bool elb = Elegido(Zona::Botones, 4);
      Caja(dl, q, x0, 852, x1, 892, elb ? amarillo : yo.bloq ? IM_COL32(90, 230, 255, 255) : naranja, yo.bloq ? IM_COL32(20, 70, 90, 235) : IM_COL32(20, 34, 70, 230), elb ? 4.0f : 2.0f);
      Txt(dl, q, (x0 + x1) * 0.5f, 872, 18, yo.bloq ? T::T("Bloqueada", "Locked") : T::T("Bloquear", "Lock"), 1, elb || yo.bloq, false, 98);
      Marcar(Zona::Botones, 4, q.At(x0, 852), q.At(x1, 892));
    }
    // fin de ronda y de partida
    if (fin_t_ >= 0 && !prep) {
      const int g = pa.GanadorRonda();
      const bool copia = pa.DueloHumano() >= 0 && pa.Duelos()[size_t(pa.DueloHumano())].copia;
      std::string t;
      const bool neutral = pa.DueloHumano() >= 0 && pa.Duelos()[size_t(pa.DueloHumano())].b == kNeutral;
      if (neutral) t = g == 0 ? std::string(T::T("Ronda neutral superada: +", "Neutral round cleared: +")) + std::to_string(eco.neutral_oro) + T::T(" de oro y una cápsula", " gold and a capsule") : std::string(T::T("Ronda neutral perdida: nadie pierde vida", "Neutral round lost: nobody loses health"));
      else if (g == 0) t = copia ? std::string(T::T("Has vencido a la copia: nadie pierde vida", "You beat the copy: nobody loses health")) : std::string(T::T("El rival pierde ", "The rival loses ")) + std::to_string(pa.DanoRival()) + T::T(" de vida", " HP");
      else if (g == 1) t = std::string(T::T("Pierdes ", "You lose ")) + std::to_string(pa.DanoRonda(0)) + T::T(" de vida", " HP");
      else t = std::string(T::T("Los dos pierden ", "Both lose ")) + std::to_string(pa.DanoRonda(0)) + T::T(" de vida", " HP");
      if (!pa.J(0).vivo) t = std::string(T::T("Has sido eliminado: puesto ", "You are out: place ")) + std::to_string(pa.J(0).puesto) + " / " + std::to_string(pa.N());
      Txt(dl, q, 800, 470, 30, t, 1, true, false, 900);
      if (fin_partida_) Clasificacion(dl, q);
      else Txt(dl, q, 800, 510, 20, T::T("A: continuar", "A: continue"), 1, false, true);
    }
    // H10: panel de dano tras la ronda: lo que hizo cada unidad de los dos equipos (de los eventos de la simulacion; el desgaste final no cuenta)
    if (fin_t_ >= 0 && !prep && sim_ && !fin_partida_) {
      std::vector<int> hecho(pa.Combatientes().size(), 0);
      for (const sim::Evento& e : sim_->Eventos())
        if (e.tipo == sim::Tipo::Impacto && e.unidad >= 0 && size_t(e.unidad) < hecho.size()) hecho[size_t(e.unidad)] += e.valor;
      int tope = 1;
      for (int v : hecho) tope = std::max(tope, v);
      for (int lado = 0; lado < 2; ++lado) {
        const float x0 = 440.0f + 380.0f * float(lado);
        std::vector<size_t> ord;
        for (size_t i = 0; i < hecho.size(); ++i) if (pa.Combatientes()[i].jugador == lado) ord.push_back(i);
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return hecho[a] > hecho[b]; });
        int total = 0;
        for (size_t i : ord) total += hecho[i];
        Caja(dl, q, x0, 712, x0 + 340, 886, lado ? IM_COL32(230, 70, 60, 255) : IM_COL32(70, 150, 255, 255), IM_COL32(10, 14, 30, 215), 2.0f);
        Txt(dl, q, x0 + 10, 728, 20, std::string(T::T(lado ? "Daño del rival: " : "Tu daño: ", lado ? "Rival damage: " : "Your damage: ")) + std::to_string(total), 0, true, false, 320);
        for (size_t k = 0; k < ord.size() && k < 6; ++k) {
          const Combatiente& c = pa.Combatientes()[ord[k]];
          const float y = 746 + 23.0f * float(k);
          const bool viva = sim_->Unidades()[ord[k]].viva();
          cara(c.unidad, q.At(x0 + 8, y), q.At(x0 + 32, y + 20), viva ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255));
          Txt(dl, q, x0 + 38, y + 10, 18, nombre(c.unidad), 0, false, !viva, 84);
          dl->AddRectFilled(q.At(x0 + 130, y + 3), q.At(x0 + 130 + 150.0f * float(hecho[ord[k]]) / float(tope), y + 17), lado ? IM_COL32(230, 90, 70, 255) : IM_COL32(80, 160, 255, 255), 2 * q.s);
          dl->AddRect(q.At(x0 + 130, y + 3), q.At(x0 + 280, y + 17), IM_COL32(0, 0, 0, 200), 2 * q.s, 0, 1.2f * q.s);
          Txt(dl, q, x0 + 334, y + 10, 18, std::to_string(hecho[ord[k]]), 2);
        }
      }
    }
    // H10: espiar a un rival (raton): su tablero con las estrellas de cada unidad, al pasar por su fila de la clasificacion
    if (prep && !tut_) {
      for (int k = 0; k < pa.N(); ++k) {
        const float y = 70 + 44.0f * float(k);
        const ImVec2 m((io.MousePos.x - q.p0.x) / q.s, (io.MousePos.y - q.p0.y) / q.s);
        if (espia_ != k && (m.x < 1396 || m.x > 1592 || m.y < y || m.y > y + 40)) continue;
        if (espia_ >= 0 && espia_ != k) continue;
        // la fila k de la clasificacion es el jugador `orden[k]`; se recalcula aqui el mismo orden
        std::vector<int> ord2(static_cast<size_t>(pa.N()));
        for (int p = 0; p < pa.N(); ++p) ord2[size_t(p)] = p;
        std::stable_sort(ord2.begin(), ord2.end(), [&](int x, int yy) {
          const Jugador &jx = pa.J(x), &jy = pa.J(yy);
          return jx.vivo != jy.vivo ? jx.vivo : jx.vivo ? jx.vida > jy.vida : jx.puesto < jy.puesto;
        });
        const int p = ord2[size_t(k)];
        const Jugador& jp = pa.J(p);
        if (EnPrueba() && espiado_ != p) {
          espiado_ = p;
          REXLOG_INFO("dbz3 tactics: espiando al jugador {}", p);
        }
        Caja(dl, q, 1136, y, 1388, y + 200, IM_COL32(255, 220, 60, 255), IM_COL32(10, 14, 30, 238), 2.0f);
        Txt(dl, q, 1146, y + 16, 20, entrenador(p) + "  " + T::T("Nivel ", "Lvl ") + std::to_string(jp.nivel) + "  " + std::to_string(jp.oro) + "g", 0, true, false, 232);
        int n = 0;
        for (int sl = kBanco; sl < kSlots; ++sl) {
          if (!jp.pz[sl].hay()) continue;
          const float cx = 1146 + 40.0f * float(n % 6), cy = y + 34 + 50.0f * float(n / 6);
          cara(jp.pz[sl].unidad, q.At(cx, cy), q.At(cx + 36, cy + 30));
          Estrellas(dl, q.At(cx + 18, cy + 40), 4.5f * q.s, jp.pz[sl].estrellas);
          ++n;
        }
        if (hist_[p]) Txt(dl, q, 1146, y + 186, 16, std::string(T::T("Último duelo: ", "Last duel: ")) + (hist_[p] == 1 ? T::T("ganaste", "you won") : hist_[p] == 2 ? T::T("perdiste", "you lost") : T::T("empate", "draw")), 0, false, true, 232);
        if (espia_ == k) Txt(dl, q, 1262, y + 170, 18, T::T("Y: siguiente jugador", "Y: next player"), 1, false, true, 240);
        if (!n) Txt(dl, q, 1262, y + 100, 18, T::T("Sin unidades en el tablero", "No units on the board"), 1, false, true, 230);
        const std::vector<SinActiva> sn = pa.Sinergias(p);
        std::string st;
        for (const SinActiva& sa : sn) if (sa.umbral >= 0) st += (st.empty() ? "" : ", ") + es_en(eco.sinergias[size_t(sa.sinergia)]) + " " + std::to_string(sa.cuenta);
        if (!st.empty() && espia_ != k) Txt(dl, q, 1146, y + 184, 18, st, 0, false, false, 236);
        break;
      }
    }
    if (tut_ > 0) Tutorial(dl, q, io);
  }
  // Tutorial: 4 pantallas sobre el tablero atenuado (textos en tactics_textos.toml)
  void Tutorial(ImDrawList* dl, const Lienzo& q, const ImGuiIO& io) {
    const ImU32 naranja = IM_COL32(255, 160, 30, 255);
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 175));
    Caja(dl, q, 300, 210, 1300, 650, naranja, IM_COL32(10, 14, 30, 245), 3.0f);
    Txt(dl, q, 800, 262, 40, Texto(("tut_titulo_" + std::to_string(tut_)).c_str()), 1, true, false, 880);
    TextoSuave(dl, 27 * q.s, q.At(350, 318), IM_COL32_WHITE, Texto(("tut_texto_" + std::to_string(tut_)).c_str()).c_str(), 900 * q.s);
    for (int i = 1; i <= 4; ++i) dl->AddCircleFilled(q.At(770 + 20.0f * float(i), 590), 6 * q.s, i == tut_ ? naranja : IM_COL32(90, 96, 120, 255));
    Txt(dl, q, 800, 624, 22, Texto(tut_ == 4 ? "tut_fin" : "tut_sig"), 1, true, false, 560);
    Txt(dl, q, 1240, 624, 18, Texto("tut_saltar"), 2, false, true, 220);
  }
  // Clasificacion final del torneo (todos los entrenadores por puesto)
  void Clasificacion(ImDrawList* dl, const Lienzo& q) {
    const Partida& pa = *partida_;
    const ImU32 naranja = IM_COL32(255, 160, 30, 255), amarillo = IM_COL32(255, 220, 60, 255);
    Caja(dl, q, 420, 110, 1180, 130 + 48.0f * float(pa.N()) + 110, naranja, IM_COL32(10, 14, 30, 235), 3.0f);
    Txt(dl, q, 800, 140, 36, T::T("CLASIFICACIÓN FINAL", "FINAL STANDINGS"), 1, true);
    const std::vector<int> cl = pa.Clasificacion();
    for (int i = 0; i < pa.N(); ++i) {
      const int p = cl[size_t(i)];
      const Jugador& j = pa.J(p);
      const float y = 175 + 48.0f * float(i);
      Caja(dl, q, 440, y, 1160, y + 42, p == 0 ? amarillo : IM_COL32(90, 96, 120, 255), p == 0 ? IM_COL32(60, 52, 10, 200) : IM_COL32(10, 14, 30, 180), p == 0 ? 3.0f : 1.5f);
      Txt(dl, q, 478, y + 21, 28, "#" + std::to_string(j.puesto), 1, i == 0);
      int u = 0;
      for (size_t k = 0; k < eco_.unidades.size(); ++k) if (eco_.unidades[k].id == kEntrenador[p]) u = int(k);
      const Modelo* m = ModeloDe(eco_.unidades[size_t(u)].id);
      dl->AddRectFilled(q.At(520, y + 4), q.At(576, y + 38), IM_COL32(24, 30, 54, 255));
      if (const S::Pic pic = S::Img(("caras/" + std::to_string(m->b3)).c_str())) S::DrawPic(dl, pic, q.At(520, y + 4), q.At(576, y + 38), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
      Txt(dl, q, 590, y + 21, 26, p == 0 ? std::string(T::T("Tú", "You")) : std::string(T::T(m->es, m->en)), 0, p == 0, false, 260);
      Txt(dl, q, 1150, y + 21, 22, j.vivo ? std::string(T::T("Campeón", "Champion")) : std::string(T::T("Eliminado en la ronda ", "Out in round ")) + std::to_string(j.ronda_baja), 2, i == 0, !j.vivo);
    }
    Txt(dl, q, 800, 175 + 48.0f * float(pa.N()) + 32, 22, T::T("A: nueva partida  ·  B dos veces: volver al HUB", "A: new game  ·  B twice: back to the HUB"), 1, false, false, 700);
  }
  // Ficha de una unidad: atributos (con las estrellas), tecnica, pasiva y sinergias
  void Ficha(ImDrawList* dl, const Lienzo& q, int u, int est, int c0, int c1) {
    const UnidadEco& ue = eco_.unidades[size_t(u)];
    const sim::DefUnidad& d = cat_.unidades[size_t(ue.cat)];
    const Modelo* m = ModeloDe(ue.id);
    const int mult = eco_.mult_estrella[size_t(est - 1)];
    const auto es_en = [](const SinergiaDef& s) { return std::string(T::T(s.es.c_str(), s.en.c_str())); };
    S::Frame(dl, q.At(1296, 432), q.At(1594, 716), q.s);
    Txt(dl, q, 1312, 456, 24, T::T(m->es, m->en), 0, true, false, 150);
    Estrellas(dl, q.At(1504, 454), 6 * q.s, est);   // hasta 4: entre el nombre y la moneda
    Moneda(dl, q.At(1566, 456), 9 * q.s);
    Txt(dl, q, 1552, 456, 20, std::to_string(ue.coste), 2);
    Txt(dl, q, 1312, 488, 20, std::string(T::T("Vida ", "HP ")) + std::to_string(d.vida * mult / 100) + T::T("  Ataque ", "  Atk ") + std::to_string(d.ataque * mult / 100), 0, false, false, 268);
    Txt(dl, q, 1312, 514, 20, std::string(T::T("Defensa ", "Def ")) + std::to_string(d.defensa) + T::T("  Alcance ", "  Range ") + std::to_string(d.alcance) + T::T("  Vel. ", "  Spd ") + std::to_string(d.velocidad), 0, false, false, 268);
    const char* forma = d.tecnica.forma == sim::Forma::Objetivo ? T::T("un objetivo", "single target") : d.tecnica.forma == sim::Forma::Linea ? T::T("en línea", "line") : T::T("en área", "area");
    TextoSuave(dl, 19 * q.s, q.At(1312, 538), IM_COL32(255, 230, 90, 255), (std::string(T::T("Técnica: ", "Skill: ")) + std::to_string(d.tecnica.dano) + T::T("% del ataque, ", "% of attack, ") + forma + ", " + T::T("alc. ", "rng ") + std::to_string(d.tecnica.alcance)).c_str(), 268 * q.s);
    static const struct { const char *es, *en; } pasiva[] = {
        {"Sin pasiva", "No passive"},
        {"Ki inicial: empieza con %d%% de ki", "Starting ki: begins with %d%% ki"},
        {"Furia: +%d%% de ataque bajo la mitad de vida", "Fury: +%d%% attack below half health"},
        {"Perfora: sus golpes ignoran el %d%% de la defensa", "Pierce: hits ignore %d%% of defense"},
        {"Cazagigantes: su técnica hace +%d%% a quien tiene más vida", "Giant slayer: skill deals +%d%% to bigger targets"},
        {"Coraza: recibe un %d%% menos de daño", "Armor: takes %d%% less damage"},
        {"Ki constante: +%d de ki por segundo", "Steady ki: +%d ki per second"},
        {"Remate: +%d%% de daño a quien tiene menos del 30%% de vida", "Finisher: +%d%% damage to targets under 30%% health"},
        {"Regenera: %d%% de vida por segundo (hasta el 25%% por ronda)", "Regen: %d%% health per second (up to 25%% per round)"}};
    char buf[160];
    std::snprintf(buf, sizeof buf, T::T(pasiva[int(d.pasiva)].es, pasiva[int(d.pasiva)].en), d.pasiva_valor);
    TextoSuave(dl, 18 * q.s, q.At(1312, 596), IM_COL32(220, 230, 255, 255), buf, 268 * q.s);
    for (int h = 0; h < 2; ++h) Txt(dl, q, 1312 + 140.0f * float(h), 666, 18, es_en(eco_.sinergias[size_t(ue.sin[h])]), 0, true, false, 128);
    for (int h = 0; h < 2; ++h)   // capsulas equipadas
      if (const int c = h ? c1 : c0; c >= 0) {
        const CapsulaDef& cp = eco_.capsulas[size_t(c)];
        Botella(dl, q.At(1322 + 140.0f * float(h), 696), 30 * q.s, c);
        Txt(dl, q, 1338 + 140.0f * float(h), 696, 18, std::string(T::T(cp.es.c_str(), cp.en.c_str())), 0, false, false, 100);
      }
  }
  // Menu propio del modo (estilo de los paneles del juego: marco, filas naranjas y la letra de B3, con el borde negro suave)
  void Menu(ImDrawList* dl, const ImGuiIO& io) {
    const Lienzo q = LienzoDe(io);
    hits_.clear();
    const ImU32 naranja = IM_COL32(255, 160, 30, 255), amarillo = IM_COL32(255, 220, 60, 255);
    S::Frame(dl, q.At(500, 150), q.At(1100, 700), q.s);
    Txt(dl, q, 800, 200, 44, "Dragon Tactics", 1, true);
    Txt(dl, q, 800, 238, 18, "BETA", 1, false, true);
    std::vector<std::string> filas;
    std::vector<std::string> valores;
    if (menu_pag_ == 0) {
      for (int id : FilasMenu()) {
        if (id == 0) filas.push_back(std::string(T::T("Continuar (ronda ", "Continue (round ")) + std::to_string(guardada_->Ronda()) + T::T(", quedan ", ", ") + std::to_string(guardada_->Vivos()) + T::T(" entrenadores)", " trainers left)"));
        else filas.push_back(id == 1 ? (guardada_ ? T::T("Nueva partida", "New game") : T::T("Jugar", "Play")) : id == 2 ? T::T("Cómo se juega", "How to play") : id == 3 ? T::T("Opciones", "Options") : T::T("Volver al HUB", "Back to the HUB"));
      }
    } else {
      filas = {T::T("Reloj de preparación", "Prep timer"), T::T("Velocidad inicial", "Starting speed"), T::T("Sonidos de combate", "Combat sounds"), T::T("Dificultad de los bots", "Bot difficulty"), T::T("Atrás", "Back")};
      valores = {opc_.reloj ? std::to_string(opc_.reloj) + " s" : std::string(T::T("Apagado", "Off")), "x" + std::to_string(opc_.vel), opc_.sonido ? T::T("Sí", "On") : T::T("No", "Off"),
                 opc_.dif == 0 ? T::T("Fácil", "Easy") : opc_.dif == 1 ? T::T("Normal", "Normal") : T::T("Difícil", "Hard"), ""};
    }
    for (size_t i = 0; i < filas.size(); ++i) {
      const float y = 282 + 64.0f * float(i);
      const bool el = mc_ == int(i);
      Caja(dl, q, 540, y, 1060, y + 52, el ? amarillo : naranja, IM_COL32(20, 34, 70, 230), el ? 4.0f : 2.0f);
      if (menu_pag_ == 0) {
        Txt(dl, q, 800, y + 26, 26, filas[i], 1, el, false, 480);
      } else {
        if (!valores[i].empty()) {
          Txt(dl, q, 560, y + 26, 24, filas[i], 0, el, false, 270);
          Txt(dl, q, 1040, y + 26, 24, std::string("<  ") + valores[i] + "  >", 2, el, false, 220);
        } else {
          Txt(dl, q, 800, y + 26, 24, filas[i], 1, el, false, 270);
        }
      }
      Marcar(Zona::Botones, int(i), q.At(540, y), q.At(1060, y + 52));
    }
    if (menu_pag_ == 1) Txt(dl, q, 800, 650, 17, T::T("Izquierda / derecha o clic: cambiar  ·  B: volver", "Left / right or click: change  ·  B: back"), 1, false, true, 540);
    else if (!aviso_.empty() && !guardada_) TextoSuave(dl, 20 * q.s, q.At(560, 628), IM_COL32(255, 220, 120, 255), aviso_.c_str(), 500 * q.s);
    else Txt(dl, q, 800, 650, 17, T::T("Cruceta / palanca y A  ·  se guarda al empezar cada preparación y antes del combate", "D-pad / stick and A  ·  saved at the start of each preparation and before the fight"), 1, false, true, 540);
    if (tut_ > 0) Tutorial(dl, q, io);
  }
  // Punto del suelo del tablero (x, z) que se proyecta en el pixel `t` (Newton con diferencias finitas); false si no converge
  bool Suelo(ImVec2 t, float& x, float& z) const {
    x = 0, z = 5.0f;
    ImVec2 o;
    float k;
    for (int it = 0; it < 14; ++it) {
      if (!Proy(x, 0, z, o, k)) return false;
      const float fx = o.x - t.x, fy = o.y - t.y;
      if (fx * fx + fy * fy < 0.25f) return true;
      constexpr float h = 0.05f;
      ImVec2 ox, oz;
      float k2;
      if (!Proy(x + h, 0, z, ox, k2) || !Proy(x, 0, z + h, oz, k2)) return false;
      const float a = (ox.x - o.x) / h, b = (oz.x - o.x) / h, c = (ox.y - o.y) / h, d = (oz.y - o.y) / h, det = a * d - b * c;
      if (std::fabs(det) < 1e-6f) return false;
      x -= (d * fx - b * fy) / det, z -= (-c * fx + a * fy) / det;
    }
    return false;
  }
  double Seg() const { return std::chrono::duration<double>(Reloj::now() - t0_).count(); }

  dbz3::ps2::Escena esc_, esc_b_;   // persistente: sus vectores conservan la memoria de un cuadro al siguiente (T4)
  int visto_ = -1, siguiente_ = 0, hechas_ = 0, cuadros_ = 0, lado_ = 3, vel_ = 1, vel_i_ = 0, auto_rondas_ = 0;
  int espia_ = -1;     // H10: fila de la lista que se ve con el mando (Y), -1 ninguna
  int espiado_ = -1;   // H10: jugador de la lista bajo el cursor (solo para el log de pruebas)
  bool hecha_[kModelos] = {}, soltado_ = false, lista_ = false, bloqueado_ = false, combate_ = false, saliendo_ = false, pausa_ = false;
  bool hits_dichos_ = false;
  int ui_cuadros_ = 0;
  bool demo_ = false, aplicado_ = false, fin_partida_ = false, ok_ = false, auto_jugado_ = false, menu_ = false;
  int mc_ = 0, minis_ = 0;           // menu de inicio: opcion elegida; miniaturas 3D del banquillo en este cuadro
  int capmano_ = -1, tut_ = 0;       // capsula cogida del inventario (tipo) y pantalla del tutorial (0 = cerrado, 1..4)
  bool tut_visto_ = false;           // el tutorial ya se comprobo en esta apertura
  long cc_ = 0, lentos_ = 0;         // cuadros del combate en curso y los de mas de 25 ms
  double dt_suma_ = 0, dt_peor_ = 0;
  int hist_[kMaxJug] = {}, hist_ronda_ = -1;   // H14: resultado del ultimo duelo contra cada rival (1 gano, 2 perdio, 3 empate)
  MezclaSe se_;                      // mezcla de sonido del combate en curso
  double banco_ms_ = 0;              // CPU media del banquillo 3D (ms/cuadro)
  std::optional<Partida> guardada_;  // la partida guardada leida para el menu de inicio
  dbz3::ps2::Camara cam_;   // la del combate de fondo (cada cuadro)
  float centro_[2] = {}, suelo_ = 0, eje_[2] = {1, 0}, alpha_ = 0;   // marco del tablero en el mundo: centro (x, z), suelo (y) y eje x del tablero (x, z)
  const Modelo* caido_ = nullptr;   // H13: primera unidad propia caida en el combate (para la pantalla de derrota)
  double abierta_ = 0, media_ = 0, ultimo_t_ = 0, acc_ = 0, fin_t_ = -1, prep_t_ = -1, salir_t_ = -10, aviso_t_ = -10;
  double victoria_t0_ = -100;   // cuando empezo la pose de victoria (reloj de pared)
  double transicion_t0_ = -100; // cuando empezo el paso preparacion -> combate
  uint64_t semilla_ = 2024;
  size_t leidos_ = 0;   // eventos de la simulacion ya leidos
  Catalogo cat_;
  bool est_ok_ = false, ficha_ver_ = false;
  int uni_vista_[kSlots] = {};
  size_t forma_sig_ = 0;                      // H9: siguiente forma de estrella 2..4 por hornear en segundo plano
  int prueba_est_ = 0, prueba_slot_ = -1;     // H9 pruebas: fase de DBZ3_TEST_TACTICS_ESTRELLAS
  double prueba_t_ = 0;
  struct Destello { int slot; double t0; int tipo = 0; };   // H9: una pieza acaba de subir de estrella (tipo 0) o se ha vendido (1)
  std::vector<Destello> destellos_;
  bool sin_destello_compra_ = false;   // (reservado: apagar el destello de compra)
  int arr_ = -1;                       // H14: slot de la unidad cogida con el raton (para arrastrarla) y donde se pulso
  ImVec2 arr_p_;
  int est_vista_[kSlots] = {};                // estrellas del jugador por casilla en el cuadro anterior (0 = vacia)
  Economia eco_;
  std::optional<Partida> partida_;
  std::optional<sim::Sim> demo_sim_;   // ronda de demostracion (pruebas)
  sim::Sim* sim_ = nullptr;            // la del combate en curso (de la partida o la de demostracion); nullptr en preparacion
  std::vector<Pres> p_;
  std::vector<Flota> flota_;
  // H11: efectos de combate. tipo 0 chispas de un golpe, 1 bola de ki, 2 rayo en linea, 3 onda en circulo; t0..t1 = del lanzamiento al impacto (ticks)
  struct Fx { uint32_t t0, t1; int de, a, tipo, alcance, area; };
  std::vector<Fx> fx_;
  std::vector<int> vels_;   // pruebas: velocidades de las rondas repetidas
  std::vector<Hit> hits_;   // zonas del raton de este cuadro
  Zona zc_ = Zona::Tienda;  // cursor: zona e indice (tablero 0..17, banquillo 0..7, tienda 0..4, botones 0..3)
  int ic_ = 0, mano_ = -1;  // mano_: slot de la unidad cogida para moverla
  std::string aviso_;
  // H10: opciones del modo (menu propio): reloj de preparacion (0 = apagado), velocidad inicial, sonidos de combate y dificultad de los bots
  struct Opc { int reloj = 0, vel = 1, dif = 1; bool sonido = true; } opc_;
  int menu_pag_ = 0, nsin_ = 0;
  bool caps_abiertas_ = false;   // H12: panel de capsulas abierto con un clic en su tira (con el mando se abre solo al llegar a el)
  float prep_resto_ = 0;
  std::vector<int> calidad_base_;
  double aviso_dura_ = 2.5;
  uint16_t ultimo_ = 0;
  std::string error_;
  Reloj::time_point t0_ = Reloj::now();
};

void CrearPanel() {
  static Arena* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Arena(rt->imgui_drawer());
}

}  // namespace

void AbrirArena() {
  if (g_abierta.exchange(true)) return;
  g_cerrar = false;
  g_lista = false;
  ++g_ciclo;
  REXLOG_INFO("dbz3 tactics: arena abierta");
  if (!g_panel_pedido.exchange(true)) dbz3::launcher::DeferUi(&CrearPanel);
}
bool ArenaAbierta() { return g_abierta.load(); }

namespace {
const bool g_hub = dbz3::hub::RegisterEntry(
    "tactics", {"Dragon Tactics", "Dragon Tactics", "Dragon Tactics", "Dragon Tactics", "Dragon Tactics"},
    {"Auto chess con los luchadores de Budokai 3: compra, coloca y mejora a tu equipo y míralo combatir contra un rival de la CPU. Versión de prueba (beta).",
     "Auto chess with the Budokai 3 fighters: buy, place and upgrade your team and watch it fight a CPU rival. Test version (beta).",
     "Auto chess avec les combattants de Budokai 3 : achète, place et améliore ton équipe et regarde-la affronter un rival CPU. Version d'essai (bêta).",
     "Auto Chess mit den Kämpfern von Budokai 3: Kaufe, platziere und verbessere dein Team und sieh ihm gegen einen CPU-Rivalen zu. Testversion (Beta).",
     "Auto chess con i combattenti di Budokai 3: compra, schiera e potenzia la squadra e guardala combattere contro un rivale CPU. Versione di prova (beta)."},
    &AbrirArena, &ArenaAbierta);

// prueba: abre y cierra la arena N veces seguidas desde el menu principal (pantalla 5)
const bool g_prueba = [] {
  const char* n = std::getenv("DBZ3_TEST_TACTICS_ENTRADA");
  if (!n) return false;
  std::thread([ciclos = std::max(1, std::atoi(n))] {
    using namespace std::chrono_literals;
    for (int k = 1; k <= ciclos; ++k) {
      while (dbz3::ingame::GameScreen() != 5 || g_abierta.load()) std::this_thread::sleep_for(100ms);
      AbrirArena();
      const auto t0 = Reloj::now();
      while (!g_lista.load() && g_abierta.load() && Reloj::now() - t0 < 200s) std::this_thread::sleep_for(100ms);
      const bool lista = g_lista.load();
      std::this_thread::sleep_for(1500ms);   // unos cuadros con el tablero en pantalla
      g_cerrar = true;
      while (g_abierta.load()) std::this_thread::sleep_for(50ms);
      REXLOG_INFO("dbz3 tactics prueba: ciclo {} de {} {}", k, ciclos, lista ? "completo" : "SIN LLEGAR A LA ARENA");
    }
    REXLOG_INFO("dbz3 tactics prueba: {} ciclos hechos", ciclos);
  }).detach();
  return true;
}();
}  // namespace

}  // namespace dbz3::tactics
