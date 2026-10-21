// dbz3 - Nave de Babidi de Budokai 2 (frente R2 ronda 4; 1:1 en R7): las 4 pruebas de un jugador que dan Kili y los
// premios que se consiguen al juntar Kili. pasarela_misiones.cpp solo llama a Entrar/Dibujar/Resultado (con su g_mu) y
// pide los combates nativos (RequestBattleEx). Necesita la ISO de B2 del jugador (como el tablero).
// RE (ELF europeo SLES_518.39, Ghidra E:/DBZ3HD_trabajo/ghidra/salida/b2.c; informe E:/DBZ3HD_trabajo/R7/nave):
//   Pantallas: 1P/2P (0x2629B0, 0x264F60; 2P solo con dos mandos) -> prueba (0x262830, 0x264790: arriba/abajo SIN dar la
//   vuelta, flechas solo si hay mas) -> seleccion de personajes y escenario nativa de B2 (0x2644E0 con la cabecera
//   "Selecc. Person." / "Seleccion Fase" y el bocadillo de Babidi) -> combate (escena 0x19, tipo de combate 6 + prueba)
//   -> resultado (escena 0x1A: 0x262640, 0x263700, 0x262DD0) -> de vuelta en la prueba.
//   Reglas del combate (0x1D2620, tipo 6..9): COM al nivel 4;
//     I   el rival con una barra (1000) y vidas sin fin (+0x16 = 0xFF): cada vez que cae se levanta (estado 0x19) y
//         cuenta uno (0x9FA2); el jugador NO recupera vida; acaba al caer el jugador. 25 Kili por enemigo.
//     II  los dos con una barra (1000), tiempo 36000 cuadros (600 s); cuenta los cuadros aguantados: (cuadros + 60) / 60
//         segundos, 2 Kili por segundo.
//     III 99 s (0x1734 cuadros); cuenta los golpes (0x9DD0); 4 Kili por golpe.
//     IV  99 s; cuenta las ondas devueltas (0x9DC8; FUN_001e2610 -> FUN_001ebf40, onda "devuelta" con la guardia < 5
//         cuadros antes); el rival no ataca y solo devuelve. 3 Kili por onda. Aqui: combate.cpp OndasDevueltas/OndasModo.
//   Pago (0x265AB0): cuenta x {25, 2, 4, 3}. Resultado: el Kili ganado sube de 1 en 1 cada 3 cuadros mientras baja la
//   cuenta (0x263700); 40 cuadros; luego pasa a la barra de la nave (0x262DD0, 1 cada 3 cuadros, la aguja gira
//   barra * 360 / 10000, 0x261B20). Al llegar al Kili de un premio (tabla 0x3782A0: 43 x {ID, Kili}) sale la ventana
//   del premio (0x2A44C0) y se gana. Un premio solo esta si aun no tienes el maximo (0x6942AD + 0x20 * ID menos lo que
//   tienes); la barra solo da la vuelta al pasar de 10000 si quedan premios por ganar (si no, se queda en 10000).
//   Al acabar suma los premios al inventario y guarda. Confirmar salta la cuenta / hasta el siguiente premio.
//   Musica: pista 0x12 de la tabla 0x377E90 = ADX_US 1148 (0x263900, 0x2654A0). Voces de Babidi (DAT_00378370..84,
//   ADX_US): 1149 con la frase 0 (1P/2P), 1150/1151 con la 9/10 (personajes), 1152-1154 con la 11-13 (escenario).
// Frente 08 (1:1, studies 3): la disposicion es la de siempre (#AMA de DATA_CMN 905); DATA_<idioma> 1133 NO la lleva
// (va con 1134/1135 en la tabla 0x378360; son IECS = sonido; detalle en la entrega studies 3/ENTREGAS/08). Ahora ademas:
//   - la ventana de premio es la nativa ("Get skill!"): DATA_CMN 933 (14 animaciones) + DATA_<idioma> 1148 (4 texturas)
//     y el nombre del premio de su arte (DATA_<idioma> 542 + ID, textura 0). Original: FUN_002a44c0 + FUN_002a4070.
//   - el maximo de cada premio sale de la tabla #SKA de B2 (DATA_CMN 851: 520 registros de 0x20; +0x0D = maximo; en
//     RAM 0x6942A0, cargada con FUN_0012d100(0x353, 0x694280, 0); el original consulta byte[0x6942AD + 0x20*ID]).
//   - la prueba I encadena los combates sin pantalla VS (hub::ChainBattleEx desde pasarela_misiones.cpp: sub_820FE9B0,
//     que corre antes de que el conector cierre el combate; Siguiente/Seguido/Encadenado/Seguir lo conectan).
//   - la prueba II corta a los 600 s (el tiempo del combate del propio juego) y su reloj no cuenta la pausa.
//   - los escenarios son los de B2 que instala el launcher (escenarios::PorMod), con mapa de B3 de respaldo.
#include "pasarela.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>

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
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include <rex/ui/immediate_drawer.h>

#include "generated/dbz3_init.h"
#include "escenarios.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

namespace dbz3 {   // main.cpp: sin foco la ventana se oscurece y el F1 esta encima (la pausa del modo no cuenta)
bool GameDimmed();
bool OverlayOpen();
}
namespace dbz3::pasarela::b2 {
bool Disponible();
ImTextureID CabezaDeB3(int id_b3);   // pasarela_b2.cpp: cabeza de B2 (CMN 1196)
}
namespace dbz3::combate {   // combate.cpp: ondas que el luchador del lado dado ha devuelto (prueba IV)
int OndasDevueltas(int lado);
void OndasCero();
void OndasModo(bool on);   // el rival pasa a ser el muneco en guardia que devuelve toda onda (prueba IV, como en B2)
}

namespace dbz3::pasarela::nave {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
namespace M = dbz3::ingame::style::mode;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kA = 0x1000,
                   kB = 0x2000, kX = 0x4000, kY = 0x8000;

struct Prueba {
  int pago, tiempo;   // Kili por cada uno (0x265AB0); tiempo del combate en s (-1 sin limite)
};
constexpr int kTopeII = 600;   // prueba II: 36000 cuadros = 600 s (B2 0x1D2620 case 7: +0x143a4 = 36000)
const Prueba kPruebas[4] = {{25, -1}, {2, kTopeII}, {4, 99}, {3, 99}};   // la II la corta el juego a los 600 s

// Nombres de los premios por su Kili (FAQ F3, version inglesa). ponytail: la ventana de premio de B2 (0x2A44C0) saca
// el nombre de la tabla de capsulas del juego; aqui va con la letra de B3.
struct Nombre {
  int kili;
  const char* nombre;
};
const Nombre kNombres[] = {
    {100, "Dabura"}, {320, "Hell Blitz"}, {430, "Evil Tornado"}, {540, "Demonic Will"}, {800, "Yakon"},
    {965, "Demon Realm Guard"}, {1200, "Majin Buu"}, {1420, "Innocence Cannon"}, {1530, "Hell Blow Shoot"},
    {1695, "Babidi's Scope"}, {1805, "Merry Swing"}, {2025, "Mage Guard"}, {2190, "Enjoy Browning"},
    {2400, "Super Buu"}, {2620, "Ill Flash"}, {2730, "Hate Ray Cannon"}, {2840, "Innocence Dance"},
    {2950, "Badness Throw"}, {3000, "Super Saiyan (Goku)"}, {3060, "Hate Dancing"}, {3170, "Vice Browning"},
    {3390, "Vice Break"}, {3600, "Kid Buu"}, {4040, "Hell Blade Rush"}, {4260, "Evil Blast"},
    {4480, "Majin Buu's Restoration"}, {4700, "Vanishing Ball"}, {4975, "Loyalty to Babidi"}, {5085, "Vanishing Drop"},
    {5360, "Babidi's Guard"}, {5580, "Assault Rain"}, {5745, "Innocence Strike"}, {6185, "Angry Explosion"},
    {6405, "Kamehameha"}, {6790, "Dabura Cookie"}, {6955, "Crazy Smash"}, {7175, "Human Candy"},
    {7505, "Toxic Chocolate"}, {7670, "Mad Kill Spike"}, {8000, "Babidi's Guard"}, {8110, "Savage Rush"},
    {8660, "Warp Kamehameha"}, {10000, "Absorption"}};
struct Premio {
  int id = 0, kili = 0;
  std::string nombre;
};

// ELF de B2 (SLES_* de la raiz de la ISO o de la carpeta extraida). ponytail: copia de LeerElf de pasarela_iw.cpp
// (frente R1); pasarla a pasarela.h cuando ese archivo quede libre.
uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}
std::vector<uint8_t> Leer(std::ifstream& f, uint64_t off, uint32_t n) {
  std::vector<uint8_t> b(n);
  f.clear();
  f.seekg(std::streamoff(off));
  f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
  b.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
  return b;
}
std::vector<uint8_t> LeerElf(const std::filesystem::path& juego) {
  auto es_elf = [](std::string n) {
    for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    return n.size() >= 11 && (n.rfind("SLUS_", 0) == 0 || n.rfind("SLES_", 0) == 0) && n[8] == '.';
  };
  std::error_code ec;
  if (std::filesystem::is_directory(juego, ec)) {
    for (const auto& e : std::filesystem::directory_iterator(juego, ec)) {
      if (!es_elf(e.path().filename().string())) continue;
      std::ifstream f(e.path(), std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    }
    return {};
  }
  std::ifstream f(juego, std::ios::binary);
  const std::vector<uint8_t> pvd = Leer(f, 16 * 2048, 2048);
  if (pvd.size() < 2048 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return {};
  const std::vector<uint8_t> raiz = Leer(f, uint64_t(U32(pvd, 156 + 2)) * 2048, std::min<uint32_t>(U32(pvd, 156 + 10), 1 << 20));
  for (size_t o = 0; o + 33 < raiz.size();) {
    const uint8_t len = raiz[o];
    if (len == 0) {
      o = (o / 2048 + 1) * 2048;
      continue;
    }
    const uint8_t nl = raiz[o + 32];
    if (o + 33 + nl <= raiz.size() && es_elf(std::string(reinterpret_cast<const char*>(&raiz[o + 33]), nl)))
      return Leer(f, uint64_t(U32(raiz, o + 2)) * 2048, std::min<uint32_t>(U32(raiz, o + 10), 64 << 20));
    o += len;
  }
  return {};
}
// Tabla de premios del ELF: se busca por sus dos primeras filas (486, 100), (180, 320).
std::vector<Premio> LeerPremios() {
  std::vector<Premio> out;
  const std::vector<uint8_t> elf = LeerElf(BuscarJuego("budokai 2"));
  const uint8_t firma[8] = {0xE6, 0x01, 100, 0, 0xB4, 0, 0x40, 0x01};
  const auto it = std::search(elf.begin(), elf.end(), std::begin(firma), std::end(firma));
  for (size_t o = size_t(it - elf.begin()); it != elf.end() && o + 4 <= elf.size() && out.size() < 64; o += 4) {
    Premio p;
    p.id = elf[o] | elf[o + 1] << 8;
    p.kili = elf[o + 2] | elf[o + 3] << 8;
    if (!p.kili) break;
    char n[48];
    std::snprintf(n, sizeof(n), "#%d", p.id);
    p.nombre = n;
    for (const Nombre& k : kNombres)
      if (k.kili == p.kili) p.nombre = k.nombre;
    out.push_back(p);
  }
  REXLOG_INFO("dbz3 nave: {} premios de Kili en el ELF ({} B){}", out.size(), elf.size(),
              out.empty() ? "" : (", el ultimo " + std::to_string(out.back().kili)).c_str());
  return out;
}
const std::vector<Premio>& Premios() {
  static const std::vector<Premio> p = LeerPremios();   // ponytail: se lee una vez por sesion
  return p;
}
// Luchadores de B2 que tiene B3 (IDs de B3, los de pasarela_b2.cpp B2aB3).
const int kLuchadores[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 27, 28, 29, 30, 32,
                           33, 34, 35, 36, 37};
constexpr int kN = int(sizeof(kLuchadores) / sizeof(kLuchadores[0]));

// kModo: 1P/2P; kPrueba; kJugador/kRival/kSitio: la seleccion (en B2 la nativa; aqui la nuestra con la cabecera, el
// bocadillo y la voz del original); kSigue: prueba I, el mismo rival se levanta; kResultado.
enum Estado { kModo, kPrueba, kJugador, kRival, kSitio, kSigue, kResultado };
int g_estado = kModo, g_prueba = 0, g_luch = 0, g_riv = 1, g_sitio = 0, g_kili = 0;
int g_vida = 0, g_vencidos = 0, g_cuenta = 0, g_frase_sel = 9, g_frase_sitio = 11;
float g_auto_t = 0;
std::atomic<bool> g_enc_armado{false};   // prueba I: pasarela_misiones.cpp ya encadeno el siguiente combate (sub_820FE9B0)

// Escenarios: los de B2 que instala el launcher desde la ISO del jugador (mod center hd/campos_iw.py auto, B12) y que
// busca escenarios::PorMod; el mapa de B3 de respaldo es el mas parecido si el alta no esta. En B2 la nave usa la
// seleccion nativa de escenario ("Seleccion Fase"; el motor 6/7, FUN_002644e0 -> FUN_002b45d0(6, 0x5100, ...)): las
// mismas casillas de B2 que el tablero (pasarela_b2.cpp kEscenarioB2). No hay escenario fijo por prueba.
struct Sitio {
  const char* alta;   // stage.toml que instala el launcher (0 = sin alta: solo el mapa de B3)
  int mapa;           // mapa de B3 de respaldo (entrada de data_cmn)
  const char *es, *en;
};
const Sitio kSitios[] = {{"Llanura B2", 62, "Llanura (B2)", "Plains (B2)"},
                         {"Calle de la ciudad B2", 68, "Zona urbana (B2)", "Urban Area (B2)"},
                         {"Base Red Ribbon B2", 44, "Base de la Red Ribbon (B2)", "Red Ribbon Base (B2)"},
                         {"Montanas nevadas", 64, "Muscle Tower (B2)", "Muscle Tower (B2)"},
                         {"Templo de Kami", 52, "Templo (B2)", "Temple (B2)"},
                         {"Namek B2", 54, "Mundo de Kaiyoshin (B2)", "Supreme Kai's World (B2)"},
                         {"Islas B2", 60, "Planeta Namek (B2)", "Planet Namek (B2)"}};
constexpr int kNSitios = int(sizeof(kSitios) / sizeof(kSitios[0]));
// Registro del selector del alta de B2 de cada sitio (-1 si el launcher no la creo), leido una vez.
const std::vector<int>& Registros() {
  static const std::vector<int> r = [] {
    std::vector<int> v;
    for (const Sitio& s : kSitios) v.push_back(s.alta ? dbz3::escenarios::PorMod(s.alta) : -1);
    return v;
  }();
  return r;
}
// El combate del sitio: registro del alta (negativo, como el tablero: -(reg + 1)) o su mapa de B3.
int EscenarioDelSitio(int sitio) {
  const int reg = Registros()[size_t(sitio)];
  return reg >= 0 ? -(reg + 1) : kSitios[sitio].mapa;
}

// Prueba II: segundos de pelea (estado 4 del combate vigilado), sin las entradas ni el final. La pausa no cuenta:
// el reloj del juego esta oculto (mgr+35253 = 1, lo mismo que apaga las barras y el reloj del HUD) o la ventana no
// tiene el foco (GameDimmed) o el F1 esta encima (OverlayOpen). El corte a los 600 s lo hace el propio combate
// (kPruebas[1].tiempo): B2 0x1D2620 case 7 con 36000 cuadros.
std::atomic<int> g_ms_lucha{0};
std::atomic<unsigned> g_reloj{0};
void Reloj() {
  const unsigned gen = ++g_reloj;
  g_ms_lucha = 0;
  std::thread([gen] {
    using C = std::chrono::steady_clock;
    const auto fin = C::now() + std::chrono::minutes(32);
    bool empezo = false;
    auto antes = C::now();
    while (g_reloj.load() == gen && C::now() < fin) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      const auto ahora = C::now();
      const int e = EstadoCombate();
      uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
      const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(0x823D6CF4)) : 0;
      const bool pausa = base && mgr != 0 && REX_LOAD_U8(mgr + 35253) == 1;   // el juego esconde su reloj al pausar
      if (e == 4 && !pausa && !dbz3::GameDimmed() && !dbz3::OverlayOpen()) {
        empezo = true;
        g_ms_lucha += int(std::chrono::duration_cast<std::chrono::milliseconds>(ahora - antes).count());
      } else if (empezo && (e == 0 || e >= 5)) {
        break;
      }
      antes = ahora;
    }
  }).detach();
}

void Pedir(int out[6]) {
  combate::OndasCero();
  combate::OndasModo(g_prueba == 3);
  const int j = kLuchadores[g_luch], r = kLuchadores[g_riv];
  out[0] = j;
  out[1] = r;
  out[2] = EscenarioDelSitio(g_sitio);   // el alta de B2 (negativa) o su mapa de B3
  out[3] = g_prueba == 1 ? 1000 : g_prueba == 0 ? g_vida : 0;   // II: una barra; I: la que quedo (no se recupera)
  out[4] = kPruebas[g_prueba].tiempo;
  if (const char* t = std::getenv("DBZ3_TEST_NAVE_TIEMPO")) out[4] = std::atoi(t);   // pruebas: acorta el reloj
  out[5] = g_prueba;
  if (g_prueba == 1) Reloj();
  REXLOG_INFO("dbz3 nave: prueba {}: {} contra {} en {} (vida {}, tiempo {}, vencidos {})", g_prueba + 1, NombreDe(j),
              NombreDe(r), kSitios[g_sitio].es, out[3], out[4], g_vencidos);
}

// Capsula de B3 equivalente a cada premio de B2 (por nombre: modding resources discord/research/SkillList.dson y
// docs/07_ports/.../b3_caps.json; E:/DBZ3HD_trabajo/R2/capsulas/tabla.md). 0 = sin equivalente en B3: 186 Evil Tornado,
// 183 Hell Blow Shoot, 192 Merry Swing, 190 Enjoy Browning, 184 Hate Ray Cannon, 191 Innocence Dance, 198 Badness
// Throw, 185 Hate Dancing, 197 Vice Browning, 195 Vice Break, 219 Vanishing Drop, 196 Assault Rain, 189 Innocence
// Strike, 216 Crazy Smash, 217 Mad Kill Spike, 218 Savage Rush; y los personajes 486-489 (Dabura y los Bu, que B3 ya
// tiene). Probables (mismo juego de IDs): 398->395, 214->127, 383->343, 215->128.
int CapsulaB3(int id_b2) {
  static const int k[][2] = {{180, 130}, {179, 129}, {443, 421}, {380, 340}, {187, 120}, {440, 413}, {381, 341},
                             {194, 124}, {2, 2},     {182, 132}, {181, 131}, {398, 395}, {213, 126}, {458, 436},
                             {382, 342}, {188, 122}, {214, 127}, {416, 393}, {445, 423}, {426, 377}, {383, 343},
                             {215, 128}, {193, 123}};
  for (const auto& e : k)
    if (e[0] == id_b2) return e[1];
  return 0;
}
// Inventario: con equivalente, el de capsulas de la partida de B3 (u8 por ID en 0x824BA110 + 0x2AFE9, docs/03_formatos/
// CAPSULAS_B3.md; maximo 9); sin el, uno propio en progreso.toml ("nave_b2_<ID>", maximo 1).
constexpr dbz3::GA kPartida = 0x824BA110;   // US; la EU la traduce GA (eu_map)
int Tienes(int id_b2) {
  const int c = CapsulaB3(id_b2);
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (c && base) return REX_LOAD_U8(kPartida + 0x2AFE9 + c);
  return LeerProgreso("nave_b2_" + std::to_string(id_b2), 0);
}
// Maximo real de cada capsula: la tabla #SKA de B2 (DATA_CMN 851; 520 registros de 0x20; +0x0D = maximo). El original
// la carga en RAM (FUN_0012d100(0x353, 0x694280, 0), b2.c) y cada premio la consulta como
// byte[0x6942AD + 0x20 * ID] - byte[0x694074 + ID] (b2.c 0x2654C4): maximos reales = 9 en las capsulas y 1 en los
// personajes (486-489) y en Absorption (193). Sin la ISO: 9 si la capsula existe en B3, si no 1 (lo de antes).
const std::vector<uint8_t>& TablaCapsulas() {
  static const std::vector<uint8_t> d = [] {
    std::vector<uint8_t> v;
    AfsPs2 cmn;
    if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) v = cmn.Entrada(851);
    REXLOG_INFO("dbz3 nave: tabla de capsulas de B2 (#SKA 851): {} B{}", v.size(),
                v.size() >= 0x20 && std::memcmp(v.data(), "#SKA", 4) == 0 ? "" : " (no es #SKA: maximos de reserva)");
    return v;
  }();
  return d;
}
int Maximo(int id_b2) {
  const std::vector<uint8_t>& d = TablaCapsulas();
  const size_t o = 0x20 + 0x20 * size_t(id_b2) + 0x0D;
  if (id_b2 >= 0 && o < d.size() && std::memcmp(d.data(), "#SKA", 4) == 0) return d[o];
  return CapsulaB3(id_b2) ? 9 : 1;
}
void Dar(const Premio& p, int n) {
  const int c = CapsulaB3(p.id);
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (c && base) {
    const auto a = kPartida + 0x2AFE9 + c;
    const int antes = REX_LOAD_U8(a), ahora = std::min(Maximo(p.id), antes + n);
    REX_STORE_U8(a, uint8_t(ahora));
    REXLOG_INFO("dbz3 nave: premio {} -> capsula {} de B3 ({} -> {}, maximo {})", p.nombre, c, antes, ahora, Maximo(p.id));
  } else {
    const std::string k = "nave_b2_" + std::to_string(p.id);
    GuardarProgreso(k, std::min(Maximo(p.id), LeerProgreso(k, 0) + n));
    REXLOG_INFO("dbz3 nave: premio {} (ID {} de B2, sin capsula en B3) apuntado", p.nombre, p.id);
  }
}

// ---- Graficos del original. El modo carga 3 archivos (tabla del ELF 0x378350): DATA_<idioma> 1131 (35 piezas),
// DATA_CMN 905 (#AMA: la disposicion, 125 animaciones) y DATA_<idioma> 1132 (las 14 frases de Babidi). Cada animacion
// del #AMA (registro de 0x40 en +0x10: rectangulo local +0x00/+0x10, lista A de fotogramas de 0x20 B {tiempo, escala
// x, escala y, giro, x, y, z, alfa}, lista B de cuadros de 0x30 B {+4 pieza, +0x20 u0 v0 u1 v1 inclusivos}) en la
// pantalla de 640x448 del original. Cada objeto del modo hace su animacion de entrada y luego la de reposo, que se
// repite (FUN_00245AD0 modo 1 -> 2).
struct Tex {
  std::unique_ptr<rex::ui::ImmediateTexture> t;
  float w = 0, h = 0;
};
struct Anim {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int pieza = -1;
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  std::vector<std::array<float, 7>> claves;   // lista A: {cuadro, escala x, escala y, x, y, alfa, giro}
  float Dur() const { return claves.empty() ? 0.0f : claves.back()[0]; }
  float dur_tarea = 0;   // S1-F01.4: lo que dura para la tarea del original: max(+0x20, +0x24) del registro (FUN_00245AD0)
};
std::vector<Tex> g_pz, g_frases;                 // 1131 (piezas), 1132 (frases de Babidi)
std::vector<Tex> g_pzv;                          // ventana de premio: DATA_<idioma> 1148 (#AMT: 0 "Get skill!",
                                                 // 1 estrella, 2 emblema, 3 "BOOM!!")
std::vector<Tex> g_nubes;                        // nubes del fondo: DATA_CMN 910 (#AMT: 512x512 + banco 512x128)
std::vector<Anim> g_anim, g_animv, g_animn;      // 905 (nave), 933 (ventana de premio) y 906 (nubes)
std::map<int, Tex> g_nombres;                    // nombre de cada premio: DATA_<idioma> (542 + ID), textura 0
std::vector<Tex> g_ficha;                        // "Skill Z Sheet": DATA_<idioma> 541 (fondos, rotulos, iconos, "New")
std::map<int, Tex> g_textos_capsula;             // textos de cada capsula: DATA_<idioma> (542 + ID), texturas 1-4 (clave ID * 8 + j)
int g_ficha_cara = 0;                            // 0 frente, 1 reverso
// S1-F01.4: el cartel dura lo que tardan en acabar sus dos tareas de entrada (FUN_002A3E90 estado 0: anims 0 y 3 del
// #AMA 933 en reposo -> FUN_00292660 abre la ficha); con la ISO europea max(+0x20, +0x24) = 101 en las dos.
float g_cartel = 101.0f;
// S1-F01.4: la ficha (FUN_002924A0 / FUN_00292420 / FUN_002922D0, b2.c; callbacks 0x292170 y 0x292090 en el ELF) no
// funde: CRECE desde (320, 216) con escala x = y de 0 a 1 en 20 cuadros (+0,05 por cuadro), se encoge igual al cerrarla
// (cruz / triangulo) y al darle la vuelta (cuadrado / izquierda / derecha) la x baja a 0 en 5 cuadros (-0,2), cambia de
// cara y vuelve a 1 en otros 5.
float g_ficha_giro_c = -100;                     // cuadro (de la ficha) en que se pidio la vuelta
float g_ficha_sale_c = -1;                       // cuadro en que se pidio cerrarla (< 0 = abierta)
std::string g_afs_idioma;                        // el DATA_<idioma>.AFS que se abrio ("DATA_UK.AFS"...)
bool g_cargado = false;
std::filesystem::path g_iso;

std::vector<Anim> LeerAma(const std::vector<uint8_t>& d) {
  std::vector<Anim> out;
  auto u32 = [&](size_t o) -> uint32_t {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
  };
  auto f32 = [&](size_t o) {
    const uint32_t u = u32(o);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  };
  if (d.size() < 16 || std::memcmp(d.data(), "#AMA", 4) != 0) return out;
  const uint32_t n = u32(8), base = u32(12);
  for (uint32_t i = 0; i < n && i < 1024 && base + 0x40 * (size_t(i) + 1) <= d.size(); ++i) {
    const size_t o = base + 0x40 * size_t(i);
    Anim a;
    a.x0 = f32(o);
    a.y0 = f32(o + 4);
    a.x1 = f32(o + 0x10);
    a.y1 = f32(o + 0x14);
    a.dur_tarea = float(std::max(int32_t(u32(o + 0x20)), int32_t(u32(o + 0x24))));
    const uint32_t na = u32(o + 0x30), oa = u32(o + 0x34), nb = u32(o + 0x38), ob = u32(o + 0x3C);
    for (uint32_t c = 0; c < na && c < 64 && oa + 0x20 * (size_t(c) + 1) <= d.size(); ++c) {
      const size_t q = oa + 0x20 * size_t(c);
      a.claves.push_back({float(u32(q)), f32(q + 4), f32(q + 8), f32(q + 0x10), f32(q + 0x14), f32(q + 0x1C), f32(q + 0xC)});
    }
    if (nb > 0 && ob + 0x30 <= d.size()) {
      a.pieza = int(u32(ob + 4));
      a.u0 = f32(ob + 0x20);
      a.v0 = f32(ob + 0x24);
      a.u1 = f32(ob + 0x28) + 1;
      a.v1 = f32(ob + 0x2C) + 1;
    }
    out.push_back(a);
  }
  return out;
}

// Fondo de nubes del original: #AMA 906 (5 capas visibles; la 5.a es vacia) sobre el #AMT 910 de DATA_CMN
// (0: nube 512x512, 4: banco 512x128). Los recortes del #AMA van en pixeles de la pantalla de 640x448, asi que se
// crean con repeticion para que las UV que pasan de la textura (por ejemplo 639/512) sigan tileando como el original.
bool CargarNubes(AfsPs2& cmn, rex::ui::ImmediateDrawer* dr) {
  g_animn = LeerAma(cmn.Entrada(906));
  const std::vector<uint8_t> amt = cmn.Entrada(910);
  g_nubes.clear();
  const int n = std::min(CuantasAmt(amt), 64);
  for (int j = 0; j < n; ++j) {
    const Imagen im = TexturaAmt(amt, j);
    Tex x;
    if (im) {
      x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, true,
                              im.rgba.data());
      x.w = float(im.w);
      x.h = float(im.h);
    }
    g_nubes.push_back(std::move(x));
  }
  if (g_animn.empty() || g_nubes.empty()) {
    g_nubes.clear();
    g_animn.clear();
    REXLOG_WARN("dbz3 nave nubes: ama=906 tex=910 no disponible (fondo sin nubes)");
    return false;
  }
  for (size_t k = 0; k < g_animn.size(); ++k) {
    const int p = g_animn[k].pieza;
    if (p < 0 || p >= int(g_nubes.size())) continue;
    REXLOG_INFO("dbz3 nave nubes: ama=906 tex=910 capa={} w/h={}/{}", k, int(g_nubes[size_t(p)].w),
                int(g_nubes[size_t(p)].h));
  }
  return true;
}

void Cargar() {
  if (g_cargado) return;
  g_cargado = true;
  auto* dr = dbz3::launcher::PreviewDrawer();
  g_iso = BuscarJuego("budokai 2");
  AfsPs2 loc, cmn;
  const std::string lc = T::LangCode();
  g_afs_idioma = lc == "es" ? "DATA_SP.AFS" : lc == "fr" ? "DATA_FR.AFS" : lc == "it" ? "DATA_IT.AFS"
                 : lc == "de" ? "DATA_DU.AFS" : "DATA_UK.AFS";
  if (!dr || g_iso.empty() || !cmn.Abrir(g_iso, "DATA_CMN.AFS") ||
      (!loc.Abrir(g_iso, g_afs_idioma) && !loc.Abrir(g_iso, g_afs_idioma = "DATA_UK.AFS"))) {
    REXLOG_WARN("dbz3 nave: sin graficos de la nave (ISO {})", g_iso.string());
    return;
  }
  auto leer = [&](size_t k, std::vector<Tex>& out) {
    const std::vector<uint8_t> e = loc.Entrada(k);
    for (int j = 0; j < std::min(CuantasAmt(e), 64); ++j) {
      const Imagen im = TexturaAmt(e, j);
      Tex x;
      if (im) {
        x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                im.rgba.data());
        x.w = float(im.w);
        x.h = float(im.h);
      }
      out.push_back(std::move(x));
    }
  };
  leer(1131, g_pz);
  leer(1132, g_frases);
  leer(1148, g_pzv);   // ventana de premio del original (FUN_002a44c0, b2.c)
  leer(541, g_ficha);  // la ficha de la capsula (FUN_00292850 pide 0x1021D = DATA_<idioma> 541)
  g_anim = LeerAma(cmn.Entrada(905));
  g_animv = LeerAma(cmn.Entrada(933));
  CargarNubes(cmn, dr);
  if (g_animv.size() > 3 && g_animv[0].dur_tarea > 0) g_cartel = std::max(g_animv[0].dur_tarea, g_animv[3].dur_tarea);   // S1-F01.4
  REXLOG_INFO("dbz3 nave: graficos de la ISO: {} piezas, {} frases, {} animaciones; ventana de premio {} texturas y "
              "{} animaciones", g_pz.size(), g_frases.size(), g_anim.size(), g_pzv.size(), g_animv.size());
}

// Nombre del premio con el arte del original: DATA_<idioma> (542 + ID) es un #AMT de 5 texturas (0 el nombre).
// Lo carga la ventana (FUN_00292850 desde FUN_002a44c0, b2.c 0x2A44C0); aqui se cachea por ID.
const Tex* NombreTex(int id_b2) {
  if (id_b2 < 0 || id_b2 > 700 || g_iso.empty() || g_afs_idioma.empty()) return nullptr;
  auto it = g_nombres.find(id_b2);
  if (it == g_nombres.end()) {
    it = g_nombres.emplace(id_b2, Tex{}).first;
    AfsPs2 loc;
    if (loc.Abrir(g_iso, g_afs_idioma)) {
      const Imagen im = TexturaAmt(loc.Entrada(size_t(542 + id_b2)), 0);
      if (im) {
        if (auto* dr = dbz3::launcher::PreviewDrawer())
          it->second.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear,
                                           false, im.rgba.data());
        it->second.w = float(im.w);
        it->second.h = float(im.h);
      }
    }
  }
  return it->second.t ? &it->second : nullptr;
}

// Textura j (1 efectos, 2 requisitos de uso, 3 comentarios, 4 "solo puede usarlo...") de la capsula `id_b2`
// (DATA_<idioma> 542 + ID, un #AMT de 5 texturas; 0 es el nombre).
const Tex* TextoCapsula(int id_b2, int j) {
  if (id_b2 < 0 || id_b2 > 700 || j < 0 || j > 4 || g_iso.empty() || g_afs_idioma.empty()) return nullptr;
  if (j == 0) return NombreTex(id_b2);
  auto it = g_textos_capsula.find(id_b2 * 8 + j);
  if (it == g_textos_capsula.end()) {
    it = g_textos_capsula.emplace(id_b2 * 8 + j, Tex{}).first;
    AfsPs2 loc;
    if (loc.Abrir(g_iso, g_afs_idioma)) {
      const Imagen im = TexturaAmt(loc.Entrada(size_t(542 + id_b2)), j);
      if (im) {
        if (auto* dr = dbz3::launcher::PreviewDrawer())
          it->second.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear,
                                           false, im.rgba.data());
        it->second.w = float(im.w);
        it->second.h = float(im.h);
      }
    }
  }
  return it->second.t ? &it->second : nullptr;
}

// Sonido: musica del modo (ADX_US 1148) y voces de Babidi (ADX_US 1149-1154).
float Volumen() {
  if (std::getenv("DBZ3_TEST_MUSICA")) return 0.001f;   // pruebas: se decodifica y se registra, sin oirse
  return dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
}
bool g_sonando = false;
void Musica(bool on) {
  if (on == g_sonando || g_iso.empty()) return;
  g_sonando = on;
  if (on) {
    REXLOG_INFO("dbz3 nave: musica ADX_US 1148");
    OirMusicaPs2(g_iso, "ADX_US.AFS", 1148, Volumen());
  } else {
    CallarMusicaPs2();
  }
}
void Voz(int entrada) {
  if (g_iso.empty()) return;
  REXLOG_INFO("dbz3 nave: voz de Babidi ADX_US {}", entrada);
  CallarAdxPs2();
  OirAdxPs2(g_iso, "ADX_US.AFS", entrada, Volumen());
}

// Efectos IECS de la nave (studies 8, E; N13). El original los pide con FUN_00147260(canal, 0, id, 0):
//   canal 2 = banco propio de la nave: FUN_00265F20 -> FUN_002A68F0(0x699DC0, 2, 0x1046D, 0x1046E, 0x1046F) (b2.c:212401;
//     tabla 0x378360) = DATA_<idioma> 1133 (cuerpo PS-ADPCM), 1134 (indice IECS, 2 sonidos: 0,79 s y 0,18 s) y 1135.
//     Ids usados: 1 (cada paso de la cuenta y de la barra: 0x263700, 0x262DD0) y 2 (fin de la cuenta, premio, saltar).
//     [H] van de 1 en 1 (id 1 -> registro 0, id 2 -> registro 1): son los unicos dos y es el unico reparto que cabe.
//   canal 0 = menus: cursor 2, aceptar 5, volver 3 (0x264790 prueba, 0x264F60 1P/2P), 0x15 al abrir la pantalla 1P/2P
//     (0x2629B0) y 0x16 tras aceptar 1P. [H] banco: el general DATA_<idioma> 142/143 (26 sonidos), el mismo que el port
//     usa para el canal 3 del tablero; no se ha visto quien lo carga.
void Efecto(int canal, int id) {
  if (g_iso.empty() || g_afs_idioma.empty()) return;
  const bool prueba = std::getenv("DBZ3_TEST_MUSICA") != nullptr;
  const int cuerpo = canal == 2 ? 1133 : 142, i = canal == 2 ? id - 1 : id;
  static int pasos = 0;   // el paso de la cuenta suena cada 3 cuadros: al registro, 1 de cada 50
  if (!(canal == 2 && id == 1) || pasos++ % 50 == 0)
    REXLOG_INFO("dbz3 nave: efecto canal {} id {} -> {} cuerpo {} registro {}", canal, id, g_afs_idioma, cuerpo, i);
  OirSonidoPs2(g_iso, g_afs_idioma, cuerpo, i, canal == 2 ? 6 : 7, prueba ? 1.0f : Volumen(), 0, prueba);
}

// Pantalla del original (640x448) dentro del area: escala y origen.
struct Lienzo {
  float s;
  ImVec2 o;
  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
};
Lienzo LienzoDe(const S::Area& a) {
  const float s = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
  return {s, ImVec2(a.p0.x + (1200.0f * a.s - 640.0f * s) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * s) * 0.5f)};
}
float g_cuadro = 1e9f;   // cuadros (60 por segundo) desde que cambio la pantalla
// Los objetos que siguen de una pantalla a otra no vuelven a entrar (B2 solo relanza la entrada si el objeto no esta ya
// en ella: 0x262830, 0x263A80): Babidi, el bocadillo (sale y entra al cambiar de abajo a arriba) y la cabecera (al
// cambiar de titulo). Cuadros desde su entrada.
float g_c_bab = 0, g_c_burb = 0, g_c_tit = 0;

// Pose de la animacion k en el cuadro c (lineal entre claves; `vuelta` = se repite, como las de reposo).
struct Pose {
  float x, y, sx, sy, a, giro;
};
Pose PoseDe(const Anim& a, float c, bool vuelta) {
  if (a.claves.empty()) return {0, 0, 1, 1, 1, 0};
  const float d = a.Dur();
  if (vuelta && d > 0) c = std::fmod(std::max(0.0f, c), d);
  const auto* k0 = &a.claves.front();
  const auto* k1 = k0;
  for (size_t i = 1; i < a.claves.size(); ++i) {
    k1 = &a.claves[i];
    if (c < (*k1)[0]) break;
    k0 = k1;
  }
  const float f = (*k1)[0] > (*k0)[0] ? std::clamp((c - (*k0)[0]) / ((*k1)[0] - (*k0)[0]), 0.0f, 1.0f) : 1.0f;
  auto m = [&](int j) { return (*k0)[j] + ((*k1)[j] - (*k0)[j]) * f; };
  return {m(3), m(4), m(1), m(2), m(5), m(6)};
}
// Dibuja la animacion k en el cuadro c; (dx, dy) desplaza; giro extra en grados (sentido horario) alrededor de su sitio.
// Por defecto la disposicion de la nave (g_anim/g_pz); `set`/`pzs` dibujan otra (la ventana de premio).
void DibC(ImDrawList* dl, const Lienzo& L, int k, float c, bool vuelta, float giro = 0, float dx = 0, float dy = 0,
          ImU32 col = IM_COL32_WHITE, const std::vector<Anim>* set = nullptr, const std::vector<Tex>* pzs = nullptr) {
  const std::vector<Anim>& anims = set ? *set : g_anim;
  const std::vector<Tex>& piezas = pzs ? *pzs : g_pz;
  if (k < 0 || k >= int(anims.size())) return;
  const Anim& a = anims[size_t(k)];
  if (a.pieza < 0 || a.pieza >= int(piezas.size()) || !piezas[size_t(a.pieza)].t) return;
  const Tex& p = piezas[size_t(a.pieza)];
  const Pose q = PoseDe(a, c, vuelta);
  if (q.sx == 0 || q.sy == 0 || q.a <= 0) return;
  const float g = (q.giro + giro) * 3.14159265f / 180.0f, cs = std::cos(g), sn = std::sin(g);
  auto v = [&](float lx, float ly) {
    lx *= q.sx;
    ly *= q.sy;
    return L.P(q.x + dx + lx * cs - ly * sn, q.y + dy + lx * sn + ly * cs);
  };
  const ImU32 cc = (col & 0x00FFFFFF) | (ImU32(float(col >> 24) * std::min(1.0f, q.a)) << 24);
  dl->AddImageQuad(reinterpret_cast<ImTextureID>(p.t.get()), v(a.x0, a.y0), v(a.x1 + 1, a.y0), v(a.x1 + 1, a.y1 + 1),
                   v(a.x0, a.y1 + 1), ImVec2(a.u0 / p.w, a.v0 / p.h), ImVec2(a.u1 / p.w, a.v0 / p.h),
                   ImVec2(a.u1 / p.w, a.v1 / p.h), ImVec2(a.u0 / p.w, a.v1 / p.h), cc);
}
float DurDe(int k, const std::vector<Anim>* set = nullptr) {
  const std::vector<Anim>& anims = set ? *set : g_anim;
  return k >= 0 && k < int(anims.size()) ? anims[size_t(k)].Dur() : 0.0f;
}
// Objeto con entrada `e` (una vez, desde el cuadro `desde` de la pantalla) y reposo `r` (en bucle); e < 0 = sin entrada.
void Obj(ImDrawList* dl, const Lienzo& L, int e, int r, float desde = 0, float dx = 0, float dy = 0,
         const std::vector<Anim>* set = nullptr, const std::vector<Tex>* pzs = nullptr) {
  const float c = g_cuadro - desde;
  if (c < 0) return;
  if (e >= 0 && c < DurDe(e, set)) return DibC(dl, L, e, c, false, 0, dx, dy, IM_COL32_WHITE, set, pzs);
  DibC(dl, L, r, e >= 0 ? c - DurDe(e, set) : c, true, 0, dx, dy, IM_COL32_WHITE, set, pzs);
}
// Numero con las cifras del #AMA: `primera` = animacion del 0 (las 10 seguidas); la ultima cifra en el sitio de la del
// 0 y las demas a su izquierda, cada una a su ancho (0x261920).
void Numero(ImDrawList* dl, const Lienzo& L, int primera, int n) {
  if (primera < 0 || primera + 9 >= int(g_anim.size())) return;
  const Anim& c0 = g_anim[size_t(primera)];
  const float paso = (c0.x1 + 1 - c0.x0) * (c0.claves.empty() ? 1.0f : c0.claves.back()[1]);
  const std::string s = std::to_string(std::max(0, n));
  for (size_t i = 0; i < s.size(); ++i) {
    const Anim& d = g_anim[size_t(primera + (s[i] - '0'))];
    const Pose q0 = PoseDe(c0, 0, false), qd = PoseDe(d, 0, false);
    DibC(dl, L, primera + (s[i] - '0'), 0, false, 0, q0.x - qd.x - paso * float(s.size() - 1 - i), q0.y - qd.y);
  }
}

// Fondo: los lados de la ventana con la madera del marco del original (pieza 0) un poco oscurecida, como hacen las
// remasterizaciones con las pantallas 4:3; dentro, el morado, la nave (71), el marco (4-7) y la cinta (13).
void Fondo(ImDrawList* dl, const Lienzo& L, bool cielo) {
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 255));
  if (!g_pz.empty() && g_pz[0].t && L.s > 0) {
    const float w = 40 * L.s, h = 312 * L.s, x0 = L.o.x, x1 = L.o.x + 640 * L.s;
    const ImTextureID id = reinterpret_cast<ImTextureID>(g_pz[0].t.get());
    for (float y = 0; y < ds.y; y += h) {
      for (float x = x0 - w; x > -w; x -= w) dl->AddImage(id, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(120, 120, 120, 255));
      for (float x = x1; x < ds.x; x += w) dl->AddImage(id, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(120, 120, 120, 255));
    }
  }
  dl->AddRectFilled(L.P(0, 0), L.P(640, 448), IM_COL32(34, 22, 52, 255));
  if (!g_animn.empty()) {   // nubes del fondo (DATA_CMN 906 + 910), detras de la nave y del marco
    dl->PushClipRect(L.P(0, 0), L.P(640, 448), true);
    // El original modula las nubes con un tinte morado oscuro (media medida 58,42,58), no las pinta blancas:
    // con blanco se comen el fondo y producen el "fondo erratico" que reporta el jugador.
    const ImU32 tinte = IM_COL32(58, 42, 58, 255);
    for (int k = 0; k < int(g_animn.size()); ++k) DibC(dl, L, k, g_cuadro, true, 0, 0, 0, tinte, &g_animn, &g_nubes);
    dl->PopClipRect();
  }
  if (cielo) DibC(dl, L, 124, 0, false);   // el cielo va detras de la nave y del marco (0x2644E0)
  DibC(dl, L, 71, g_cuadro, true);
  for (int k = 4; k <= 7; ++k) DibC(dl, L, k, 0, false);
  DibC(dl, L, 13, 0, false);
}
// Babidi (36 -> 37) y su bocadillo con la frase k de 1132 a su tamano, centrada: el de abajo (18 -> 19) o, en la
// seleccion, el de arriba a la derecha (21 -> 22).
void Babidi(ImDrawList* dl, const Lienzo& L, int frase, bool arriba) {
  Obj(dl, L, 36, 37, g_cuadro - g_c_bab);
  const int e = arriba ? 21 : 18, r = arriba ? 22 : 19;
  Obj(dl, L, e, r, g_cuadro - g_c_burb);
  if (g_c_burb < DurDe(e) || frase < 0 || frase >= int(g_frases.size()) || !g_frases[size_t(frase)].t ||
      r >= int(g_anim.size()))
    return;
  const Anim& b = g_anim[size_t(r)];
  const Pose q = PoseDe(b, 0, false);
  const Tex& f = g_frases[size_t(frase)];
  const float cx = q.x + (b.x0 + b.x1) * 0.5f, cy = q.y + (b.y0 + b.y1) * 0.5f;
  dl->AddImage(reinterpret_cast<ImTextureID>(f.t.get()), L.P(cx - f.w * 0.5f, cy - f.h * 0.5f),
               L.P(cx + f.w * 0.5f, cy + f.h * 0.5f));
}

// ---- Resultado (escena 0x1A): cuenta -> 40 cuadros -> barra de la nave con los premios -> 40 cuadros -> prueba.
struct Res {
  int fase = 0;            // 0 cuenta, 1 espera, 2 barra, 3 premio, 4 espera final
  float acc = 0, espera = 0;
  int total = 0;           // Kili ganado (cuenta x pago)
  int ganado = 0;          // el que se ve en la linea del medio (sube y luego pasa a la barra)
  int cuenta = 0;          // la que se ve arriba (baja)
  int barra = 0;           // la barra de la nave (0..10000)
  int sig = 0;             // siguiente premio (43 = ninguno)
  int quedan = 0;          // premios que aun se pueden ganar (B2 iGpffff8c00)
  int premio = -1;         // el que se esta ensenando
  float premio_t = 0;      // cuadro en el que salio (ventana nativa: DATA_CMN 933)
  bool nuevo = false;
  std::vector<int> max, ganados;
} g_res;

int Siguiente() {
  const auto& P = Premios();
  int i = 0;
  while (i < int(P.size()) && !(g_res.barra < P[size_t(i)].kili && g_res.ganados[size_t(i)] != g_res.max[size_t(i)])) ++i;
  return i;
}
void Vuelta() {   // al pasar de 10000: vuelta si quedan premios, si no se queda llena
  if (g_res.quedan < 1) g_res.barra = 10000;
  else g_res.barra -= 10000;
  g_res.sig = Siguiente();
}
void EmpezarResultado() {
  const auto& P = Premios();
  const int pago = kPruebas[g_prueba].pago;
  g_res = Res();
  g_res.total = g_cuenta * pago;
  g_res.cuenta = g_cuenta;
  g_res.barra = g_kili;
  g_res.max.resize(P.size());
  g_res.ganados.assign(P.size(), 0);
  for (size_t i = 0; i < P.size(); ++i) {
    g_res.max[i] = std::max(0, Maximo(P[i].id) - Tienes(P[i].id));
    g_res.quedan += g_res.max[i];
  }
  g_res.sig = Siguiente();
  REXLOG_INFO("dbz3 nave: resultado de la prueba {}: {} x {} = {} Kili (barra {}, premios por ganar {})", g_prueba + 1,
              g_cuenta, pago, g_res.total, g_kili, g_res.quedan);
}
void EnsenarPremio() {
  const auto& P = Premios();
  g_res.premio = g_res.sig;
  const Premio& p = P[size_t(g_res.premio)];
  g_res.nuevo = Tienes(p.id) == 0 && g_res.ganados[size_t(g_res.premio)] < 1;
  g_res.premio_t = g_cuadro;
  g_res.fase = 3;
  REXLOG_INFO("dbz3 nave: premio: {} (ID {}, {} Kili, maximo {}){}", p.nombre, p.id, p.kili, Maximo(p.id),
              g_res.nuevo ? " nuevo" : "");
}
void AcabarBarra() {
  const auto& P = Premios();
  for (size_t i = 0; i < P.size(); ++i)
    if (g_res.ganados[i] > 0) Dar(P[i], g_res.ganados[i]);
  g_kili = g_res.barra;
  GuardarProgreso("nave_kili", g_kili);
  g_res.fase = 4;
  g_res.espera = 0;
  REXLOG_INFO("dbz3 nave: barra de la nave: {} Kili", g_kili);
}
// Un cuadro de la pantalla de resultado; true = vuelve a la prueba.
bool PasoResultado(float cuadros, bool ok) {
  auto& r = g_res;
  const int pago = kPruebas[g_prueba].pago;
  switch (r.fase) {
    case 0:   // 0x263700: 1 Kili cada 3 cuadros; la cuenta baja uno cada `pago`
      if (ok || r.ganado >= r.total) {
        Efecto(2, 2);   // b2.c:211143 (fin) / 211171 (saltar)
        r.cuenta = 0;
        r.ganado = r.total;
        r.fase = 1;
        r.espera = 0;
        break;
      }
      for (r.acc += cuadros; r.acc >= 3 && r.ganado < r.total; r.acc -= 3) {
        if (r.ganado % pago == 0) --r.cuenta;
        ++r.ganado;
        Efecto(2, 1);   // b2.c:211165: cada paso
      }
      break;
    case 1:   // 0x263670: 40 cuadros; sin Kili se acaba
      if ((r.espera += cuadros) > 40) {
        if (r.total < 1) return true;
        if (r.barra > 9999) Vuelta();
        r.sig = Siguiente();
        r.fase = 2;
        r.acc = 0;
      }
      break;
    case 2: {   // 0x262DD0
      const auto& P = Premios();
      if (ok) {
        Efecto(2, 2);   // b2.c:210934: saltar
        if (r.barra > 9999) Vuelta();
        if (r.sig >= int(P.size()) && r.barra + r.ganado > 9999) {
          r.ganado -= 10000 - r.barra;
          r.barra = 10000;
          if (r.barra > 9999) Vuelta();
        }
        const int falta = r.sig < int(P.size()) ? P[size_t(r.sig)].kili - r.barra : 1 << 30;
        if (falta <= r.ganado) {
          r.barra += falta;
          r.ganado -= falta;
          EnsenarPremio();
        } else {
          r.barra += r.ganado;
          r.ganado = 0;
          if (r.barra > 10000) Vuelta();
          AcabarBarra();
        }
        break;
      }
      if (r.sig < int(P.size()) && r.barra == P[size_t(r.sig)].kili) {
        Efecto(2, 2);   // b2.c:210859: llega a un premio
        EnsenarPremio();
      } else if (r.ganado < 1) {
        Efecto(2, 2);   // b2.c:210875: se acaba
        AcabarBarra();
      } else {
        for (r.acc += cuadros; r.acc >= 3 && r.ganado > 0; r.acc -= 3) {
          --r.ganado;
          ++r.barra;
          Efecto(2, 1);   // b2.c:210928: cada paso
          if (r.barra > 10000) Vuelta();
          if (r.sig < int(P.size()) && r.barra == P[size_t(r.sig)].kili) break;
        }
      }
      break;
    }
    case 3:   // ventana del premio (0x262D50): al cerrarla se gana y se busca el siguiente (0x263540)
      if (ok) {
        ++r.ganados[size_t(r.premio)];
        --r.quedan;
        r.premio = -1;
        if (r.ganado > 0 && r.barra > 9999) Vuelta();
        r.sig = Siguiente();
        r.fase = 2;
        r.acc = 0;
      }
      break;
    default:   // 0x262CC0 / 0x262C50: guardado y 40 cuadros
      if ((r.espera += cuadros) > 40) return true;
  }
  return false;
}
// La ficha de la capsula tras el cartel ("Skill Z Sheet", FUN_00292550 con las texturas de DATA_<idioma> 541 y 542 + ID y la
// disposicion de DATA_CMN 852). Medida en PCSX2 (fase I, premio del Saibaman): panel de 408x392 centrado en (320, 216);
// rotulos "Skill Name" (148, 89), "Effects" (148, 161), "Usage requirements" (148, 252) y "Comments" en el reverso
// (223, 55); nombre centrado en y 129 y la linea de "solo puede usarlo..." en y 152; efectos y requisitos desde x 157 (y 192 y
// 282); comentarios desde (157, 90); abajo el numero de copias (azul, centro 297, 381) y el giro (verde, 390, 381);
// "New" (88x64) centrado en (132, 101). Ayuda: cuadrado / cruceta = reverso, X / triangulo = volver.
// S1-F01.4 (FUN_00291C40, b2.c; lista de elementos del ELF 0x379E00; #SKA 851 = DATA_CMN cargado en 0x694280, registro
// de 0x20 por ID desde +0x20): la variante v = (byte +0x0A >> 4) & 3 elige las anims 2v / 2v+1 del #AMA 852 = texturas
// de 541 {0, 2, 3, 4} de frente y {1, 1, 1, 5} de reverso (v = 3 es la DORADA: 193 Absorption); los rotulos toman el
// cuadro v de su lista B (tinte 0 / 0,3 / 0,6 en v 0-2 y 0,6 / 0,3 / 0 en la dorada). Abajo: la cifra azul es
// byte +0x08 & 7 (anim 12 + n, oculta si es 0), la "P" (anim 12) sale con byte +0x0B & 8, la "Z" (anim 20) sin el
// bit 4 y el giro es el verde (anim 21) con el bit 1 o el tachado (anim 22) sin el.
int ByteCapsula(int id_b2, int k) {
  const std::vector<uint8_t>& d = TablaCapsulas();
  const size_t o = 0x20 + 0x20 * size_t(id_b2) + size_t(k);
  return id_b2 >= 0 && o < d.size() && std::memcmp(d.data(), "#SKA", 4) == 0 ? d[o] : -1;
}
void DibFicha(ImDrawList* dl, const Lienzo& L, int id_b2, float c, int cara, bool nuevo) {
  if (g_ficha.size() < 22) return;
  // escala de la ficha: crece en 20 cuadros, se encoge en 20 al cerrarla; la x baja y sube en 5 + 5 al darle la vuelta
  float sy = std::clamp(c / 20.0f, 0.0f, 1.0f), sx = sy;
  if (g_ficha_sale_c >= 0) sy = sx = std::clamp(1.0f - (c - g_ficha_sale_c) / 20.0f, 0.0f, 1.0f);
  const float dg = c - g_ficha_giro_c;
  if (dg >= 0 && dg < 10) {
    sx *= dg < 5 ? 1.0f - dg / 5.0f : (dg - 5) / 5.0f;
    if (dg < 5) cara ^= 1;   // todavia se ve la cara de antes
  }
  if (sx <= 0 || sy <= 0) return;
  auto P = [&](float x, float y) { return L.P(320 + (x - 320) * sx, 216 + (y - 216) * sy); };
  auto pone = [&](const Tex* t, float x, float y, bool centro = false, ImU32 tinte = IM_COL32_WHITE) {
    if (!t || !t->t) return;
    if (centro) {
      x -= t->w * 0.5f;
      y -= t->h * 0.5f;
    }
    dl->AddImage(reinterpret_cast<ImTextureID>(t->t.get()), P(x, y), P(x + t->w, y + t->h), ImVec2(0, 0), ImVec2(1, 1), tinte);
  };
  const int b10 = ByteCapsula(id_b2, 0x0A), b8 = ByteCapsula(id_b2, 0x08), b11 = ByteCapsula(id_b2, 0x0B);
  const int v = b10 < 0 ? 1 : (b10 >> 4) & 3;   // sin la tabla, la plateada (la de antes)
  // los rotulos son grises: el juego los tine (medido en v = 1: 58 104 152 sobre su gris de 230); la dorada, al reves
  const ImU32 tinte = v == 3 ? IM_COL32(168, 112, 62, 255) : IM_COL32(62, 112, 168, 255);
  static const int kFrente[4] = {0, 2, 3, 4}, kReverso[4] = {1, 1, 1, 5};
  pone(&g_ficha[size_t(cara ? kReverso[v] : kFrente[v])], 320, 216, true);
  if (cara == 0) {
    pone(&g_ficha[6], 148, 89, false, tinte);
    pone(&g_ficha[7], 148, 161, false, tinte);
    pone(&g_ficha[8], 148, 252, false, tinte);
    pone(TextoCapsula(id_b2, 0), 320, 129, true);
    pone(TextoCapsula(id_b2, 4), 320, 152, true);
    pone(TextoCapsula(id_b2, 1), 157, 191.75f);
    pone(TextoCapsula(id_b2, 2), 157, 282.25f);
    const int n = b8 < 0 ? 1 : b8 & 7;
    if (b11 >= 0 && (b11 & 8)) pone(&g_ficha[10], 250.5f, 381.5f, true);       // "P" (anim 12)
    if (n > 0) pone(&g_ficha[size_t(10 + n)], 297, 381.5f, true);               // cifra (anim 12 + n)
    if (b11 >= 0 && !(b11 & 4)) pone(&g_ficha[18], 343.5f, 381.5f, true);      // "Z" (anim 20)
    if (b11 < 0 || (b11 & 1)) pone(&g_ficha[19], 390, 381.5f, true);           // giro (anim 21)
    else pone(&g_ficha[20], 389.5f, 381.5f, true);                              // giro tachado (anim 22)
  } else {
    pone(&g_ficha[9], 223, 55, false, tinte);
    pone(TextoCapsula(id_b2, 3), 157, 90);
  }
  if (nuevo) {   // anim 23 del #AMA: crece de 0 a 1,2 y se asienta en 1 en 10 cuadros (va en las dos caras)
    const float e = c < 6 ? c / 6.0f * 1.2f : c < 10 ? 1.2f - 0.2f * (c - 6) / 4.0f : 1.0f;
    const Tex& t = g_ficha[21];
    const ImVec2 mid = P(132, 101);
    const float w = t.w * 0.5f * e * sx * L.s, h = t.h * 0.5f * e * sy * L.s;
    if (t.t) dl->AddImage(reinterpret_cast<ImTextureID>(t.t.get()), ImVec2(mid.x - w, mid.y - h), ImVec2(mid.x + w, mid.y + h));
  }
  S::HelpLine(dl, L.P(320, 428), 20 * L.s, {{"X", T::T("Ver reverso", "View reverse side")}, {"A", T::T("Volver", "Back")}});
}
// S1-F01.4: dar la vuelta (si no esta ya girando) y cerrar con su animacion; c = cuadro de la ficha.
void FichaGirar(float c) {
  if (c - g_ficha_giro_c < 10 || g_ficha_sale_c >= 0) return;
  g_ficha_giro_c = c;
  g_ficha_cara ^= 1;
}
void FichaReset() {
  g_ficha_cara = 0;
  g_ficha_giro_c = -100;
  g_ficha_sale_c = -1;
}

// Ventana "Get skill!" del original (FUN_002a44c0 + FUN_002a4070, b2.c) en el cuadro c desde que se abrio: lanza a la
// vez la tarea del cartel (anim 0 -> reposo 1), la de la estrella (3 -> 4) y las del estallido con el emblema (6..12) y
// la placa "BOOM!!" (13); texturas DATA_<idioma> 1148, disposicion DATA_CMN 933. En el original (medido) el cartel va con
// una nube de humo y despues sale la ficha de la capsula (DibFicha) con el nombre y los textos del premio; el nombre grande
// que antes se dibujaba aqui quedaba tapado por el humo. La usan la nave y la orden 70 del tablero de B2 (FUN_00293740).
// S1-F01.4 (FUN_002A3E90): mientras la ficha esta abierta el cartel y la estrella siguen detras en reposo (anims 1 y 4);
// al cerrarla salen con sus anims 2 y 5 a la vez que la ficha se encoge, y la ventana acaba cuando acaban las dos.
float FinPremio() {
  const float a = g_animv.size() > 5 ? std::max(g_animv[2].dur_tarea, g_animv[5].dur_tarea) : 0.0f;
  return std::max(20.0f, a);
}
bool PremioCerrado(float c) { return g_ficha_sale_c >= 0 && c - g_cartel - g_ficha_sale_c >= FinPremio(); }
void CerrarFicha(float c) {
  if (g_ficha_sale_c < 0) g_ficha_sale_c = std::max(0.0f, c - g_cartel);
}
void DibVentanaPremio(ImDrawList* dl, const Lienzo& L, int id_b2, const char* nombre, float c, bool nuevo = true) {
  const bool ficha = c >= g_cartel && g_ficha.size() >= 22;
  auto obj = [&](int e, int r) {
    if (c < DurDe(e, &g_animv)) DibC(dl, L, e, c, false, 0, 0, 0, IM_COL32_WHITE, &g_animv, &g_pzv);
    else DibC(dl, L, r, c - DurDe(e, &g_animv), true, 0, 0, 0, IM_COL32_WHITE, &g_animv, &g_pzv);
  };
  if (ficha && g_ficha_sale_c >= 0) {   // S1-F01.4: salida del cartel y de la estrella
    const float cs = c - g_cartel - g_ficha_sale_c;
    DibC(dl, L, 5, cs, false, 0, 0, 0, IM_COL32_WHITE, &g_animv, &g_pzv);
    DibC(dl, L, 2, cs, false, 0, 0, 0, IM_COL32_WHITE, &g_animv, &g_pzv);
  } else {
    obj(3, 4);   // estrella
    obj(0, 1);   // cartel "Get skill!"
  }
  for (int k = 6; k <= 13; ++k)   // tareas de una vez (FUN_00245210): se apagan al llegar a max(+0x20, +0x24)
    if (k < int(g_animv.size()) && c < std::max(g_animv[size_t(k)].dur_tarea, DurDe(k, &g_animv))) DibC(dl, L, k, c, false, 0, 0, 0, IM_COL32_WHITE, &g_animv, &g_pzv);
  if (ficha) return DibFicha(dl, L, id_b2, c - g_cartel, g_ficha_cara, nuevo);
  if (g_ficha.size() < 22) {   // sin la ficha (otra ISO): el nombre sobre el cartel, como antes
    if (const Tex* n = NombreTex(id_b2))
      dl->AddImage(reinterpret_cast<ImTextureID>(n->t.get()), L.P(320 - n->w * 0.5f, 206 - n->h * 0.5f),
                   L.P(320 + n->w * 0.5f, 206 + n->h * 0.5f));
    else
      S::MenuText(dl, L.P(320, 206), 22 * L.s, nombre, true, false, 320 * L.s);
  }
}
void DibujarResultado(ImDrawList* dl, const Lienzo& L) {
  // 0x262640: titulo (16), panel (77), franja (78), esferas (79, 80), aguja (81), cuenta (82) con su palabra (92-95),
  // Kili ganado (96, 106) y barra de la nave (107, 117)
  Obj(dl, L, 15, 16);
  for (int k = 77; k <= 80; ++k) DibC(dl, L, k, g_cuadro, true);
  DibC(dl, L, 81, 0, false, float(g_res.barra) * 360.0f / 10000.0f);
  Numero(dl, L, 82, g_res.cuenta);
  DibC(dl, L, 92 + g_prueba, 0, false);
  Numero(dl, L, 96, g_res.ganado);
  DibC(dl, L, 106, 0, false);
  Numero(dl, L, 107, g_res.barra);
  DibC(dl, L, 117, 0, false);
  if (g_res.fase == 3 && g_res.premio >= 0) {
    // Ventana del premio: la de B2 ("Get skill!"). FUN_002a4070 (b2.c) lanza a la vez la tarea del cartel (anim 0
    // -> reposo 1), la de la estrella (3 -> 4) y las del estallido con el emblema (6..12) y la placa "BOOM!!" (13);
    // las texturas son DATA_<idioma> 1148 (la disposicion, DATA_CMN 933). El nombre del premio es su arte nativo
    // (DATA_<idioma> 542 + ID, textura 0) y el Kili, nuestra linea (el original no lo repite).
    const Premio& p = Premios()[size_t(g_res.premio)];
    const float c = std::max(0.0f, g_cuadro - g_res.premio_t);
    DibVentanaPremio(dl, L, p.id, p.nombre.c_str(), c, g_res.nuevo);
    if (c < g_cartel) {
      char li[48];
      std::snprintf(li, sizeof(li), "%d Kili%s", p.kili, g_res.nuevo ? T::T("  ¡Nuevo!", "  New!") : "");
      S::MenuText(dl, L.P(320, 300), 15 * L.s, li, false, false);
    }
  }
}

}  // namespace

// S1-F01.2: el ELF de B2 del jugador para el tablero (el iris de la Bola de Dragon lee de el sus tablas de estrellas).
const std::vector<uint8_t>& ElfB2() {
  static const std::vector<uint8_t> e = LeerElf(BuscarJuego("budokai 2"));
  return e;
}
bool Disponible() { return b2::Disponible(); }
// Se abre con el deseo "Portal de la nave de Babidi" de Shenron en el Dragon World (pasarela_b2.cpp, como en B2).
bool Abierta() { return LeerProgreso("tablero_b2_nave", 0) != 0; }

void Entrar() {
  combate::OndasModo(false);
  g_kili = LeerProgreso("nave_kili", 0);
  g_enc_armado = false;
  Premios();   // los graficos los carga Dibujar en el hilo que dibuja (crear texturas desde otro hilo rompia: R7)
  g_estado = kModo;   // la primera vez, la pantalla de 1P/2P (0x2654A0 con 0x8C44 = 0)
  REXLOG_INFO("dbz3 nave: entrar ({} Kili)", g_kili);
}

// --- Cadena de la prueba I (B2: el rival cae y se levanta, sin pantalla VS). La pide pasarela_misiones.cpp en el hilo
// del juego (gancho sub_820FE9B0, antes de que el conector cierre el combate) y la gasta CombateNave sin volver a
// pedir nada: el hub::ChainBattleEx pedido arranca el combate siguiente. Sin el armado, todo sigue igual que antes
// (pantalla VS). Ver studies 3/ENTREGAS/08_nave_babidi_1a1. ---
bool Siguiente(int out[6], int vida) {   // no toca el estado: la cadena puede fallar
  if (g_prueba != 0 || g_enc_armado.load()) return false;
  const int j = kLuchadores[g_luch], r = kLuchadores[g_riv];
  out[0] = j;
  out[1] = r;
  out[2] = EscenarioDelSitio(g_sitio);
  out[3] = std::max(1, vida);   // el jugador sigue con la vida que le quedo (B2: no se recupera)
  out[4] = kPruebas[0].tiempo;
  out[5] = 0;
  return true;
}
void Seguido(int vida) {   // el combate encadenado quedo pedido: cuenta el vencido y guarda la vida
  if (g_enc_armado.exchange(true)) return;
  ++g_vencidos;
  g_vida = std::max(1, vida);
  REXLOG_INFO("dbz3 nave: prueba I: el rival se levanta; combate {} encadenado (vida {})", g_vencidos + 1, g_vida);
}
bool Encadenado() { return g_enc_armado.load(); }
bool Seguir() { return g_enc_armado.exchange(false); }   // CombateNave: lo consume y arranca la cadena

// 0 nada, 1 salir, 2 combate pedido en out = {jugador, rival, mapa, vida inicial, tiempo, prueba}.
int Dibujar(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, int out[6]) {
  Cargar();
  static int antes = -1;
  static float desde = 0, t_antes = 0, d_bab = 0, d_burb = 0, d_tit = 0;
  const float dt = std::clamp(t - t_antes, 0.0f, 0.1f);
  t_antes = t;
  if (g_estado != antes) {   // pantalla nueva: las piezas entran con sus claves; Babidi habla
    auto sel = [](int e) { return e == kJugador || e == kRival || e == kSitio; };
    auto cab = [&](int e) { return e == kSitio ? 2 : sel(e) ? 1 : 0; };
    if (antes < 0 || antes == kResultado || antes == kSigue || g_estado == kResultado) d_bab = d_burb = d_tit = t;
    else {
      if (sel(antes) != sel(g_estado)) d_burb = t;   // bocadillo de abajo <-> de arriba
      if (cab(antes) != cab(g_estado)) d_tit = t;    // titulo / "Selecc. Person." / "Seleccion Fase"
    }
    antes = g_estado;
    desde = t;
    if (g_estado == kModo) {
      Voz(1149);
      Efecto(0, 0x15);   // 0x2629B0 (b2.c:210713) al abrir la pantalla 1P/2P
    }
    if (g_estado == kJugador) {
      g_frase_sel = 9 + std::rand() % 2;   // 0x2617C0
      Voz(1150 + g_frase_sel - 9);
    }
    if (g_estado == kSitio) {
      g_frase_sitio = 11 + std::rand() % 3;   // 0x261700
      Voz(1152 + g_frase_sitio - 11);
    }
  }
  g_cuadro = (t - desde) * 60.0f;
  g_c_bab = (t - d_bab) * 60.0f;
  g_c_burb = (t - d_burb) * 60.0f;
  g_c_tit = (t - d_tit) * 60.0f;
  const Lienzo L = LienzoDe(a);
  auto salir = [&](int r) {
    Musica(false);
    CallarAdxPs2();
    antes = -1;
    return r;
  };
  if (g_estado == kSigue) {   // prueba I: el mismo rival se levanta (vidas sin fin) y sigue con la vida que quedo
    Pedir(out);
    g_estado = kPrueba;
    return salir(2);
  }
  Musica(true);
  Fondo(dl, L, g_estado == kJugador || g_estado == kRival || g_estado == kSitio);
  if (auto_on && t - g_auto_t > 2.5f) {
    g_auto_t = t;
    pressed |= kA;
  }
  bool ok = (pressed & (kA | kStart)) != 0;
  if (g_estado == kResultado) {
    if (g_res.fase == 3) {   // el cartel pasa a la ficha con A; en la ficha, X / cruceta da la vuelta y A / Y vuelve
      // S1-F01.4: en la ficha, X / izquierda / derecha le dan la vuelta (cuadrado / 0x80 / 0x20 del original) y A / Y
      // la cierran con su animacion; la ventana se da por cerrada cuando acaba (PremioCerrado).
      const float c = g_cuadro - g_res.premio_t;
      if (c < g_cartel) {
        if (ok) g_res.premio_t = g_cuadro - g_cartel;   // salta el cartel
        ok = false;
        FichaReset();
      } else {
        if (pressed & (kX | kLeft | kRight)) FichaGirar(c - g_cartel);
        else if (pressed & (kA | kStart | kY)) CerrarFicha(c);
        ok = PremioCerrado(c);
      }
    }
    const bool fin = PasoResultado(dt * 60.0f, ok);
    DibujarResultado(dl, L);
    M::Help(dl, a, {{"A", T::T("Seguir", "Continue")}});
    if (fin) {
      g_estado = kPrueba;
      if (auto_on) return salir(1);   // prueba automatica: un combate y fuera
    }
    return 0;
  }
  if (g_estado == kModo) {   // 0x2629B0: titulo, Babidi con la frase 0, el recuadro (24 -> 25) y 1P (29) / 2P (35)
    Obj(dl, L, 15, 16, g_cuadro - g_c_tit);
    Babidi(dl, L, 0, false);
    Obj(dl, L, 24, 25);
    if (g_cuadro >= DurDe(24)) {   // 1P elegido (28 da el vaiven); 2P solo con dos mandos (0x264F60): aqui uno
      DibC(dl, L, 28, g_cuadro, true);
      DibC(dl, L, 35, 0, false, 0, 0, 0, IM_COL32(150, 150, 150, 255));
    }
    M::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Salir", "Quit")}});
    if (pressed & kB) {
      Efecto(0, 3);   // b2.c:211911
      return salir(1);
    }
    if (ok) {
      Efecto(0, 5);      // b2.c:211890
      Efecto(0, 0x16);   // b2.c:211895 (corta al anterior, como en el original: mismo canal)
      g_estado = kPrueba;
    }
    return 0;
  }
  if (g_estado == kPrueba) {   // 0x262830 / 0x264790
    const int antes_p = g_prueba;
    if ((pressed & kUp) && g_prueba > 0) --g_prueba;   // sin dar la vuelta
    if ((pressed & kDown) && g_prueba < 3) ++g_prueba;
    if (auto_on) g_prueba = 2;
    if (const char* f = std::getenv("DBZ3_TEST_NAVE_PRUEBA"))   // pruebas: la prueba automatica (I/II)
      g_prueba = std::clamp(std::atoi(f), 0, 3);
    static float desde_p = 0;
    if (g_prueba != antes_p) desde_p = g_cuadro;   // la "Fase N" nueva entra con su animacion
    if (g_prueba != antes_p && !auto_on) Efecto(0, 2);   // cursor (b2.c:211685/211705)
    if (pressed & kB) Efecto(0, 3);                       // b2.c:211730
    else if (ok) Efecto(0, 5);                            // b2.c:211712
    Obj(dl, L, 15, 16, g_cuadro - g_c_tit);
    Obj(dl, L, 61, 62);                                // la columna de las 4 casillas
    DibC(dl, L, 66 + g_prueba, 0, false);              // la casilla encendida
    Obj(dl, L, 45 + 4 * g_prueba, 46 + 4 * g_prueba, desde_p);
    if (g_prueba != 0) Obj(dl, L, 39, 40);             // flechas solo si hay mas arriba / abajo
    if (g_prueba != 3) Obj(dl, L, 42, 43);
    Babidi(dl, L, 1 + g_prueba, false);                // frase de la prueba (1P: 1-4)
    M::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Volver", "Back")}});
    if (pressed & kB) g_estado = kModo;
    else if (ok) g_estado = kJugador;
    return 0;
  }
  // Seleccion: en B2 la nativa con "Selecc. Person." (118 -> 119) y, para el escenario, "Seleccion Fase" (121 -> 122),
  // con Babidi y el bocadillo de arriba. Aqui el recuadro (25) con la cara de B3, a la izquierda para no pisarlos.
  int* cur = g_estado == kJugador ? &g_luch : g_estado == kRival ? &g_riv : &g_sitio;
  const int n = g_estado == kSitio ? kNSitios : kN;
  if (pressed & (kLeft | kUp)) *cur = (*cur + n - 1) % n;
  if (pressed & (kRight | kDown)) *cur = (*cur + 1) % n;
  if (g_estado == kSitio) Obj(dl, L, 121, 122, g_cuadro - g_c_tit);
  else Obj(dl, L, 118, 119, g_cuadro - g_c_tit);
  constexpr float bx = 170 - 320, by = 210 - 172;   // el recuadro (320, 172) movido a (170, 210)
  Obj(dl, L, 24, 25, 0, bx, by);
  if (g_estado == kSitio) {
    S::MenuText(dl, L.P(170, 210), 22 * L.s, T::T(kSitios[g_sitio].es, kSitios[g_sitio].en), true, false, 150 * L.s);
  } else {
    const int id = kLuchadores[*cur];
    // R7: la cabeza de B2 (CMN 1196, la del original); sin ella, la cara de B3
    ImTextureID tex = dbz3::pasarela::b2::CabezaDeB3(id);
    if (!tex) tex = CaraDe(id);
    if (tex) dl->AddImage(tex, L.P(118, 168), L.P(222, 254));
    DibC(dl, L, g_estado == kJugador ? 29 : 35, 0, false, 0, -150, g_estado == kJugador ? -12 : -64);   // 1P / 2P
  }
  S::MenuText(dl, L.P(170, 300), 15 * L.s,
              ("<  " + (g_estado == kSitio ? std::string(T::T(kSitios[g_sitio].es, kSitios[g_sitio].en))
                                           : NombreDe(kLuchadores[*cur])) + "  >").c_str(),
              true, false, 230 * L.s);
  Babidi(dl, L, g_estado == kSitio ? g_frase_sitio : g_frase_sel, true);
  M::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Volver", "Back")}});
  if (pressed & kB) g_estado = g_estado == kJugador ? kPrueba : g_estado - 1;
  if (ok) {
    if (g_estado == kRival && g_riv == g_luch && kN > 1) g_riv = (g_riv + 1) % kN;
    if (g_estado != kSitio) {
      ++g_estado;
      return 0;
    }
    g_vida = 0;
    g_vencidos = 0;
    Pedir(out);
    g_estado = kPrueba;
    return salir(2);
  }
  return 0;
}

// Fin del combate de la prueba (lado 0 = jugador): segundos que sobraban (-1 sin limite) y golpes que quitaron vida.
void Resultado(int ganador, int vida, int segundos, int golpes) {
  combate::OndasModo(false);
  const int ms = g_ms_lucha.load();
  ++g_reloj;   // para el reloj de la prueba II
  REXLOG_INFO("dbz3 nave: fin: gana el lado {}, vida {}, {} s de sobra, {} golpes, {} ondas devueltas, {} ms de pelea",
              ganador + 1, vida, segundos, golpes, combate::OndasDevueltas(0), ms);
  if (ganador < 0) {   // salio desde la pausa: sin Kili
    g_estado = kPrueba;
    return;
  }
  if (g_prueba == 0) {
    if (ganador == 0) {
      if (g_enc_armado) {   // el combate siguiente ya se pidio (pasarela_misiones.cpp sub_820FE9B0): Seguido ya conto
        g_estado = kSigue;
        return;
      }
      ++g_vencidos;   // el rival cae y se levanta: uno mas, y sigue con la vida que le queda
      g_vida = std::max(1, vida);
      g_estado = kSigue;
      return;
    }
    g_cuenta = g_vencidos;
  } else if (g_prueba == 1) {
    g_cuenta = (std::min(ms, kTopeII * 1000) * 60 / 1000 + 60) / 60;   // (cuadros + 60) / 60
  } else if (g_prueba == 2) {
    g_cuenta = golpes;
  } else {
    g_cuenta = combate::OndasDevueltas(0);   // IV: ondas de ki devueltas (combate.cpp, sub_82122488)
  }
  if (const char* c = std::getenv("DBZ3_TEST_NAVE_CUENTA")) g_cuenta = std::atoi(c);   // pruebas: la barra y premios
  if (g_cuenta > 0) RangoMejora("rango_nave_" + std::to_string(g_prueba), 1, 25);   // escalafon: cada prueba, una vez
  EmpezarResultado();
  g_estado = kResultado;
}

// Para el tablero de B2 (cápsula al azar de sus casillas): la de B3 equivalente a una de B2, 0 si no hay.
int CapsulaDeB2(int id_b2) { return CapsulaB3(id_b2); }

// ---- Orden 70 del tablero de B2 (studies 8, E): FUN_00293740 -> FUN_002835F0 + la ventana de premio de la nave ----
// FUN_002835F0(grupo, mascara, inventario) (b2.c:229323): recorre los 0x208 registros del #SKA (en RAM 0x6942A0 = DATA_CMN
// 851 + 0x20); vale el que tiene +0x14 == grupo y su mascara de personajes (+0, u64) es 0 o comparte bit con la pedida.
// Lista A: los que valen y aun no estan al maximo (+0x0D); lista B: los que valen con mascara != 0. Elige al azar en A
// (FUN_0012E4E0) y, si A esta vacia, en B; sin ninguno, 0. La mascara pedida es la de los personajes que tienes
// (FUN_00245DD0: tabla 0x377E40 de 36 capsulas de personaje) con un AND con el bit del personaje de la pieza.
// [H] "tienes al personaje": aqui siempre (las piezas del tablero son las del jugador; B3 no tiene esas capsulas).
int CapsulaAlAzar(int grupo, int personaje_b2) {
  const std::vector<uint8_t>& d = TablaCapsulas();
  if (d.size() < 0x40 || std::memcmp(d.data(), "#SKA", 4) != 0) return 0;
  // personaje < 0: la orden 8 (FUN_00295C10), que pide la mascara de TODOS los personajes que tienes (aqui, todos [H])
  const uint64_t tengo = personaje_b2 < 0 ? ~uint64_t(0) : personaje_b2 < 64 ? (uint64_t(1) << personaje_b2) : 0;
  std::vector<int> con_hueco, con_mascara;
  for (int i = 0; i < 0x208; ++i) {
    const size_t o = 0x20 + 0x20 * size_t(i);
    if (o + 0x20 > d.size()) break;
    if (d[o + 0x14] != uint8_t(grupo)) continue;
    uint64_t m = 0;
    for (int b = 0; b < 8; ++b) m |= uint64_t(d[o + size_t(b)]) << (8 * b);
    if (m != 0 && (m & tengo) == 0) continue;
    if (m != 0) con_mascara.push_back(i);
    if (Tienes(i) < int(d[o + 0x0D])) con_hueco.push_back(i);
  }
  const std::vector<int>& l = !con_hueco.empty() ? con_hueco : con_mascara;
  if (l.empty()) return 0;
  return l[size_t(std::rand()) % l.size()];
}
bool CapsulaNueva(int id_b2) { return Tienes(id_b2) == 0; }
// Al cerrarse la ventana: +1 si no esta al maximo (b2.c:238091-238095).
void GanarCapsula(int id_b2) {
  if (id_b2 <= 0 || Tienes(id_b2) >= Maximo(id_b2)) return;
  Premio p;
  p.id = id_b2;
  p.nombre = "#" + std::to_string(id_b2);
  Dar(p, 1);
}
// La ventana "Get skill!" (la misma de la nave) en la pantalla de 640x448 del original dentro del area.
// Devuelve true cuando el jugador (o la prueba automatica) la cierra. `cuadros` = tiempo desde que se abrio.
bool VentanaPremio(ImDrawList* dl, const S::Area& a, int id_b2, float cuadros, uint16_t pressed, bool auto_on) {
  Cargar();   // hilo de dibujo (las texturas se crean aqui)
  static float salto = 0;   // cuadros que se adelanta la ficha al saltar el cartel con A
  if (cuadros < 1) {
    salto = 0;
    FichaReset();
  }
  const float c = cuadros + salto;
  const std::string n = "#" + std::to_string(id_b2);
  DibVentanaPremio(dl, LienzoDe(a), id_b2, n.c_str(), std::max(0.0f, c), Tienes(id_b2) == 0);
  if (c < g_cartel) {
    if ((pressed & kA) && c >= 30) salto += g_cartel - c;   // A salta el cartel (minimo 30 cuadros: sonido del cartel)
  } else if (pressed & (kX | kLeft | kRight)) {   // S1-F01.4: cuadrado / izquierda / derecha (0x80A0 del original)
    FichaGirar(c - g_cartel);
  } else if (pressed & (kA | kY)) {               // cruz / triangulo (0x5000): se encoge y sale el cartel
    CerrarFicha(c);
  }
  if (auto_on && c - g_cartel > 70.0f) CerrarFicha(c);   // prueba automatica: se ve la ficha un momento
  if (std::getenv("DBZ3_TEST_FICHA_GIRO") && c - g_cartel > 40.0f && c - g_cartel < 45.0f) FichaGirar(c - g_cartel);   // prueba
  return PremioCerrado(c);
}

}  // namespace dbz3::pasarela::nave
