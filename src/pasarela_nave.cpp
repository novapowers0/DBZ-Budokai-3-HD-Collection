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
// Flujo de pantallas de la nave (b2.c), cada tarea cede a la siguiente cambiando su +0x1C. Comprobado contra el port (COINCIDE /
// DIFIERE / [H] = sin cerrar): EU 0x265E90 FUN_00265E90: la 1.a vez crea la tarea 0x265560 (carga) y al volver de un combate (uGpffff8c14 != 0) 0x262AF0.
//     COINCIDE: el port empieza en kModo (0x8C44 = 0) y tras el resultado vuelve a kPrueba. EU 0x265560 FUN_00265560 / EU 0x2654A0: tras las 3 cargas ponen los objetos del marco (anims 0, 0xC, 0x46) y pasan a 0x265410
//     (0x8C44 = 0: 1P/2P) o 0x264DE0 (prueba). COINCIDE (kModo / kPrueba). EU 0x262AF0 FUN_00262AF0 (vuelta del combate): mismas anims de marco y a 0x264DE0. COINCIDE.
//   0x2621F0 FUN_002621F0 / 0x262360 FUN_00262360: crean los objetos del #AMA 905 (3 + 8 + 2 + 12) parados en la anim 0.
//     Motor: el port dibuja las anims por indice, sin objetos [equivalente, sin estados +0x30 propios]. EU 0x2624F0 FUN_002624F0: crea el objeto de fundido uGpffff8c2c (anim 0x13) con las frases 1132 = `Babidi()`. COINCIDE. EU 0x265410 FUN_00265410 -> FUN_002629B0 y EU 0x265310 FUN_00265310: la pantalla 1P/2P no lee mandos hasta que sus objetos de
//     entrada (0x8C30/0x8C34, clip 0x1B = 27, espera max(+0x20,+0x24) = 31 cuadros) dejan de estar en el estado 1 (+0x30);
//     el port lo modela con `DurTarea(27)` (DS127; antes `DurDe(24)`, ya corregido). Si no hay dos mandos (0x3B43A0 / 0x3B4458 == 3) deja 1P: PORTADO (2026-10-26: 2P con dos mandos; el rival es el mando 2).
//   0x264DE0 FUN_00264DE0: FUN_00262830 y 0x264D00 (espera a los 8 objetos 0x699D70..0x699D8C y a 0x699D60;
//     clips 0x0F/0x12/0x24/0x27/0x2A/0x3D/0x40/0x2D+4p/0x49; manda max(+0x20,+0x24), 61 cuadros). PORTADO por DS127 con `DurTarea`.
//   0x264670 FUN_00264670 / 0x264710 FUN_00264710: esperan a los objetos y ceden a 0x2644E0 (seleccion) / 0x265410.
//     PORTADO por DS127: clips 0x11/0x14/0x3F/0x42/0x2F+4p y 0x699D60 0x4B (max(+0x20,+0x24), 21 cuadros); B no espera 0x11/0x14.
// Mas funciones de pantalla, leidas: EU 0x261880 FUN_00261880 (cuando el objeto 1P/2P llega a su bucle, arranca el fundido uGpffff8c2c y la voz de
//   Babidi 1149 con FUN_0012BA00(1, DAT_00378370): el port espera g_c_burb >= DurDe(18), al pasar al bucle (g_voz_modo); EU 0x263990 FUN_00263990 (carga de la pantalla de resultado: espera a los 3 objetos y a 0x699DC0, y construye 0x2624F0, 0x2621F0
//   0x262640 -> 0x263900 = kResultado); EU 0x263700 FUN_00263700 (cuenta del resultado: +1 cada 3 cuadros de 60 Hz, `dt` PAL ya aplicado
//   dentro: 1,2 / 1,0; el port usa 3 cuadros de 60 Hz) y cede a 0x263670 (no leida).
// Pegamento de la rueda (b2.c; FUN_002B4430 = estado del motor de la rueda 3D: 3 abierta, 5 y 6 elegido, 7 lista, 8 cerrada hacia atras,
// 9 elegido, 10 atras, 11 cerrada), cada funcion comparada con el port: EU 0x25FE30 FUN_0025FE30: estado 0 espera a que el objeto 0x699D18 pare; 1 con la rueda en 7 lanza su anim 0x28; 2 con 9 (elegido)
//     guarda el luchador (FUN_002B4270 -> 0x693718), anim 0x2A y cambia a la escena 0x12; con 10 (atras) vuelve a 0x25FFF0; 3 con 11
//     cierra. COINCIDE en lo esencial con kJugador/kRival (elegir -> siguiente, B -> atras); anims 0x28/0x2A de #AMA 905: 21 cuadros cada (F8). EU 0x263D20 FUN_00263D20 (1.a rueda) y EU 0x2640E0 FUN_002640E0 (rueda del jugador/COM): estado 0 con la rueda en 3 lanza la
//     anim 0x76; 1 con 5 (elegido) copia de cada jugador (FUN_002B4290: personaje, bandera, "Custom") su lista de capsulas a
//     0x699540 + 0x10 n: la por defecto del personaje (registro +0x2B8, tabla 0x3897F8 + 0x2C8 c) si no es "Custom", la propia si lo es
//     (= `g_lista_capsulas` 0 Normal / 1 Custom del port); con 6 (atras) anim 0x78 y estado 2; 2 con 8 cierra hacia 0x264DE0 (prueba).
//     En 0x263D20 con 2 jugadores (cGpffff8c38 == 1) y la 3.a prueba (cGpffff8c3c == 2) salta a la escena 0x19 con modo 3: solo 2P.
//     COINCIDE; equivalente port: `g_lista_capsulas` -> `hub::CustomCapsulesNext` + `nave::ListaCapsulas()` (pasarela_misiones.cpp).
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
#include "reloj_prueba.h"
#include "survivors/medidas.h"
#include "pasarela.h"
#include "iw_nave_hud_b2.h"
#include "combate.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <imgui.h>

#include <rex/cvar.h>
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
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <mutex>
#include <vector>

#include <memory>
#include <future>
#include <functional>

#include <rex/ui/immediate_drawer.h>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include "mods.h"
#include "region.h"
#include "generated/dbz3_init.h"
#include "escenarios.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "ps2_modelo.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

// B2-P01 (11/11): la leia EsperaResultadoB2() por nombre pero nadie la registraba (la fila del F1 no podia guardarla).
REXCVAR_DEFINE_DOUBLE(dbz3_nave_resultado_seg, 0.0, "DBZ3/Dev",
                      "Nave B2: espera extra de la pantalla de resultado en segundos (0 = como el original)");
REXCVAR_DECLARE(bool, dbz3_b2_fx_nativo);   // pasarela_b2.cpp (PUL-B2: lista B de los #AMA 905/933 de la Nave)
REXCVAR_DEFINE_BOOL(dbz3_b2_nave_frase_pose, true,  // 12/11 encendido de serie (HOJA_DECISIONES_0800 A)
    "DBZ3/Dev",
                    "Nave B2: la frase de Babidi en la pose del bocadillo como FUN_00245510 (RESCATE B2-NAVE, en prueba)");

namespace dbz3 {   // main.cpp: sin foco la ventana se oscurece y el F1 esta encima (la pausa del modo no cuenta)
bool GameDimmed();
bool OverlayOpen();
}
namespace dbz3::hub { bool RequestEditSkills(); }   // hub.cpp: abre el Edit Skills nativo (pantalla 22)
namespace dbz3::pasarela::b2 {
bool Disponible();
ImTextureID CabezaDeB3(int id_b3);   // pasarela_b2.cpp: cabeza de B2 (CMN 1196)
uint32_t B2Rng(uint32_t bits);       // pasarela_b2.cpp: LFSR del tablero (b2.c:29165-29176); mismos consumidores en la nave
}
namespace dbz3::combate {   // combate.cpp: ondas que el luchador del lado dado ha devuelto (prueba IV)
int OndasDevueltas(int lado);
void OndasCero();
void OndasModo(bool on);   // el rival pasa a ser el muneco en guardia que devuelve toda onda (prueba IV, como en B2)
}

namespace dbz3::pasarela::nave {
const std::vector<uint8_t>& ElfB2();   // mas abajo
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
namespace M = dbz3::ingame::style::mode;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kLB = 0x0100, kRB = 0x0200,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

struct Prueba {
  int pago, tiempo;   // Kili por cada uno (0x265AB0); tiempo del combate en s (-1 sin limite)
};
constexpr int kTopeII = 600;   // prueba II: 36000 cuadros = 600 s (B2 0x1D2620 case 7: +0x143a4 = 36000)
const Prueba kPruebas[4] = {{25, -1}, {2, kTopeII}, {4, 99}, {3, 99}};   // la II la corta el juego a los 600 s

// Nombres orientativos por Kili. El identificador y el umbral reales se leen de la tabla del ELF en DAT_003782A0
// (FUN_00262DD0/FUN_00265AB0); el arte de premio se obtiene de DATA_<idioma> 542 + ID en FUN_002A44C0/FUN_002A4070.
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
std::atomic<int> g_hz_b2{50};   // resultado: SLES PAL; SLUS conserva sus 60 campos/s
std::vector<uint8_t> LeerElf(const std::filesystem::path& juego) {
  auto es_elf = [](std::string n) {
    for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    const bool ok = n.size() >= 11 && (n.rfind("SLUS_", 0) == 0 || n.rfind("SLES_", 0) == 0) && n[8] == '.';
    if (ok) g_hz_b2 = n.rfind("SLES_", 0) == 0 ? 50 : 60;
    return ok;
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
// ---- Los 29 luchadores de la rueda (orden del anillo, ELF 0x3A3910: ids de B2) y su luchador de B3 (B2aB3 de
// pasarela_b2.cpp, copia: el tablero lo tiene en un ambito anonimo). 63 = "?" (al azar).
constexpr int kAnilloB2[30] = {0, 2, 28, 24, 33, 6, 4, 8, 7, 21, 35, 32, 29, 30, 31, 25, 20, 18, 17, 16, 15, 11, 12, 9, 10, 3, 5, 27, 26, 63};
int B3DeB2(int c) {
  switch (c) {
    case 25: return 32;
    case 26: return 6;
    case 27: return 9;
    case 28: return 4;
    case 29: return 34;
    case 30: return 35;
    case 31: return 36;
    case 32: return 37;
    case 33: return 15;
    case 35: return 16;
  }
  return c >= 0 && c < 25 ? B1aB3(c) : -1;
}
int B3Anillo(int i) { return i >= 0 && i < 29 ? B3DeB2(kAnilloB2[i]) : -1; }

// Escenarios de la burbuja (ELF 0x3A39F0: 10 casillas; imagen = tex 5 + k, casilla = tex 14 + k; la ultima es "?"): id de
// escenario de B2 (0 llanura, 1 ciudad, 2 CAMARA DEL TIEMPO (grupo 5: la casilla del reloj de arena, medido en PCSX2 23/10),
// 3 torneo (grupo 1), 4 base, 5 montanas nevadas, 6 templo del cielo (grupo 2), 7 mar con torre, 8 Namek; tabla 0x372AB0) y el escenario con el que se pide (alta de B2 si el launcher la instalo, si no el
// mapa de B3 mas parecido).
struct Escena2 {
  int id_b2;
  const char* alta;
  int mapa;
  const char *es, *en;
};
const Escena2 kEscenas2[9] = {
    {0, "Llanura B2", 62, "Llanura (B2)", "Plains (B2)"},
    {3, "Torneo B2", 46, "Torneo (B2)", "Tournament (B2)"},
    {5, "Montanas nevadas", 64, "Montanas nevadas (B2)", "Snowy Mountains (B2)"},
    {7, "Islas B2", 60, "Mar con torre (B2)", "Sea Tower (B2)"},
    {1, "Calle de la ciudad B2", 68, "Zona urbana (B2)", "Urban Area (B2)"},
    {4, "Base Red Ribbon B2", 44, "Base de la Red Ribbon (B2)", "Red Ribbon Base (B2)"},
    {6, "Templo de Kami", 52, "Templo (B2)", "Temple (B2)"},                     // escenario 6 -> grupo 2 (tabla 0x372AB0 del ELF; la casilla es la isla del cielo)
    {2, nullptr, 58, "Camara del Tiempo (B2)", "Time Chamber (B2)"},
    {8, "Namek B2", 54, "Namek (B2)", "Namek (B2)"}};
constexpr int kNEsc2 = 9;

// kModo: 1P/2P; kPrueba; kJugador / kMenu / kRival / kSitio: la rueda de personajes del jugador (1P), su menu Normal /
// Custom / Edit Skills, la rueda del rival (COM o el mando 2 en 2P) y la burbuja de escenarios, todas nativas de B2
// (A2nave); kSigue: prueba I, el mismo rival se levanta; kResultado.
enum Estado { kModo, kPrueba, kJugador, kRival, kSitio, kSigue, kResultado, kMenu };
int g_estado = kModo, g_prueba = 0, g_luch = 0, g_riv = 0, g_sitio = 0, g_kili = 0;   // g_luch / g_riv: casilla del anillo (0..29; 29 = "?")
int g_vida = 0, g_cuenta = 0, g_frase_sel = 9, g_frase_sitio = 11;
// g_vencidos lo escribe el hilo del juego (tagteam.cpp -> Levantado, un golpe letal) y lo lee el UI: atomico
// para que el predicado de la prueba I no dependa del mutex de misiones (no se bloquea el hilo de combate).
std::atomic<int> g_vencidos{0};
float g_auto_t = 0;
std::atomic<bool> g_enc_armado{false};   // prueba I: pasarela_misiones.cpp ya encadeno el siguiente combate (sub_820FE9B0)
// Entrada por mando (nave::Mandos, la llama pasarela_misiones.cpp antes de Dibujar): pulsaciones de este cuadro y si
// el mando 2 esta conectado. El original lee los cuatro registros de mando 0x3B43B2/0x4412/0x446A/0x44CA y exige
// 0x3B43A0 == 0x3B4458 == 3 para ofrecer 2P (EU 0x264F60 / 0x260630).
std::atomic<uint16_t> g_ent1{0}, g_ent2{0};
std::atomic<uint16_t> g_mant{0};   // botones mantenidos del mando 1 (L1 / R1 + cruceta)
uint16_t g_sol_ant = 0;            // L1 / R1 mantenidos el cuadro anterior
bool g_sol_uso = false;            // la cruceta se uso con L1 / R1 mantenido (no es un cambio de pagina)
std::atomic<bool> g_dos_mandos{false};
std::atomic<bool> g_2p{false};   // cGpffff8c38 del original: 0 = 1P, 1 = 2P (humano contra humano)

// Escenarios: los de B2 que instala el launcher desde la ISO del jugador (mod center hd/campos_iw.py auto, B12) y que
// busca escenarios::PorMod; el mapa de B3 de respaldo es el mas parecido si el alta no esta. En B2 la nave usa la
// seleccion nativa de escenario ("Stage Selection": la burbuja; motor 6/7, FUN_002644e0 -> FUN_002b45d0(6, 0x5100, ...)).
// Registro del selector del alta de B2 de cada escenario (-1 si el launcher no la creo), leido una vez.
const std::vector<int>& Registros() {
  static const std::vector<int> r = [] {
    std::vector<int> v;
    for (const Escena2& s : kEscenas2) v.push_back(s.alta ? dbz3::escenarios::PorMod(s.alta) : -1);
    return v;
  }();
  return r;
}
// El combate del sitio: registro del alta (negativo, como el tablero: -(reg + 1)) o su mapa de B3.
int EscenarioDelSitio(int sitio) {
  sitio = std::clamp(sitio, 0, kNEsc2 - 1);
  const int reg = Registros()[size_t(sitio)];
  return reg >= 0 ? -(reg + 1) : kEscenas2[sitio].mapa;
}
// ---- Roster de la rueda (A2nave): TODOS los luchadores que tiene el jugador en B3 HD, en paginas de 29 + el "?".
// Pagina 0 = el reparto de B2 (con sus caras y nombres nativos, orden de la rueda de B2); las siguientes = el resto del
// roster actual (base de B3 sin lo ya puesto, personajes de mods / importados / puertos y los IDs extra 44+), por ID, con su
// cara de B3 HD recortada en el aro naranja de B2 y su nombre. El heroe (exclusivo del modo Heroe: ID 63 o "Heroe") no
// sale. Se reconstruye al entrar (el roster cambia con los mods). L1 / R1 cambian de pagina, como el "Quick select" de B2.
struct Entrada {
  int b2 = -1, b3 = 0, cara = -1;   // cara >= 0: tira de caras de B2 (indice del anillo)
};
// Desbloqueo NATIVO de B2: partida nueva = luchadores 0x1C0002D1 (Goku, Krilin, Piccolo, Ten Shin Han,
// Raditz, Goten, Trunks nino, Gohan: un bit por ID) y escenarios 0x05 (bits 0 y 2 = llanura y templo; bit -> casilla de la
// burbuja: ELF 0x3A39E0 = 0,4,7,1,5,2,6,3,8). Los eventos de desbloqueo de la partida (b2.c 0x1CD..0x1E9 personajes ->
// ID por DAT_00379603, 0x1FE..0x206 escenarios -> bit por DAT_003795F2) los dispara el tablero: `DesbloquearEvento`.
// Guardado: progreso.toml nave_pers_lo / nave_pers_hi / nave_esc. DBZ3_TEST_NAVE_TODO=1 (pruebas) lo abre todo.
constexpr uint32_t kPersLo = 0x1C0002D1u;
constexpr int kBitCasilla[9] = {0, 4, 7, 1, 5, 2, 6, 3, 8};
constexpr int kEvPers[29] = {0, 2, 28, 26, 3, 27, 5, 6, 4, 7, 8, 24, 21, 33, 35, 9, 10, 12, 11, 15, 16, 17, 18, 25, 20, 32, 29, 30, 31};
constexpr int kEvEsc[9] = {3, 2, 0, 5, 7, 6, 1, 8, 4};
bool TodoAbierto() { return std::getenv("DBZ3_TEST_NAVE_TODO") != nullptr; }
bool PersAbierto(int id_b2) {
  if (TodoAbierto() || id_b2 < 0) return true;
  const uint32_t lo = uint32_t(LeerProgreso("nave_pers_lo", int(kPersLo))), hi = uint32_t(LeerProgreso("nave_pers_hi", 0));
  return id_b2 < 32 ? ((lo >> id_b2) & 1) != 0 : ((hi >> (id_b2 - 32)) & 1) != 0;
}
std::vector<int> g_cel;   // casillas de la burbuja abiertas (0..8) + 9 = "?"
void ConstruirCasillas() {
  g_cel.clear();
  const uint32_t esc = TodoAbierto() ? 0x1FFu : uint32_t(LeerProgreso("nave_esc", 0x05));
  for (int c = 0; c < 9; ++c)
    for (int b = 0; b < 9; ++b)
      if (kBitCasilla[b] == c && ((esc >> b) & 1)) g_cel.push_back(c);
  std::sort(g_cel.begin(), g_cel.end());
  g_cel.push_back(9);
}
std::vector<std::vector<Entrada>> g_pags(1);
int g_pag = 0, g_j3 = 0, g_r3 = 0;   // pagina de la rueda y luchadores de B3 elegidos
// solP3: B2 conserva por lado la eleccion CONFIRMADA en 0x69D3E4/+0x430
// (FUN_002B3F60/002AD490); cancelar la rueda no guarda. No es parte de la tarjeta.
struct EleccionNave { int id = 0, traje = 0, lista = 0; };
EleccionNave g_recordada[2] = {{0, 0, 0}, {0, 0, 0}}, g_elegida[2];   // eB2e: el COM arranca en 0 como el original (memoria a cero, dorado PAL)
std::atomic<uint64_t> g_trajes_pendientes{0};
bool EsHeroe(int id, const std::string& n) {
  std::string l = n;
  for (auto& c : l) c = char(std::tolower(static_cast<unsigned char>(c)));
  return id == 63 || l == "heroe" || l == "hero";
}
void ConstruirPaginas() {
  g_pags.clear();
  std::vector<Entrada> p0;
  std::set<int> usados;
  for (int i = 0; i < 29; ++i) {
    usados.insert(B3Anillo(i));   // aunque este cerrado: no sale en las paginas de extras
    if (PersAbierto(kAnilloB2[i])) p0.push_back({kAnilloB2[i], B3Anillo(i), i});
  }
  g_pags.push_back(p0);
  std::vector<Entrada> extra;
  for (int id = 0; id < 128; ++id) {
    if (usados.count(id)) continue;
    const std::string n = NombreDe(id);
    if (n.empty() || n[0] == '#' || EsHeroe(id, n)) continue;
    extra.push_back({-1, id, -1});
  }
  for (size_t k = 0; k < extra.size(); k += 29)
    g_pags.emplace_back(extra.begin() + long(k), extra.begin() + long(std::min(extra.size(), k + 29)));
  g_pag = std::clamp(g_pag, 0, int(g_pags.size()) - 1);
  std::string ids;
  for (const Entrada& e : extra) ids += std::to_string(e.b3) + " ";
  REXLOG_INFO("dbz3 nave: roster de la rueda: {} paginas, {} luchadores extra: {}", g_pags.size(), extra.size(), ids);
}
const std::vector<Entrada>& Pag() { return g_pags[size_t(std::clamp(g_pag, 0, int(g_pags.size()) - 1))]; }

// Prueba II: cuadros del RELOJ DEL COMBATE (mgr+35144, como VigilarCombate), no tiempo de pared.
// Las pausas, tecnicas y ralentizaciones pueden dejar el combate en estado 4 sin avanzar su reloj.
// B2 paga (cuadros+60)/60; contar milisegundos host daba Kili de mas (37 s frente a 21 del juego).
std::atomic<int> g_cuadros_lucha{0};
std::atomic<unsigned> g_reloj{0};
void Reloj() {
  const unsigned gen = ++g_reloj;
  g_cuadros_lucha = 0;
  std::thread([gen] {
    using C = std::chrono::steady_clock;
    const auto fin = C::now() + std::chrono::minutes(32);
    bool empezo = false;
    while (g_reloj.load() == gen && C::now() < fin) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      const int e = EstadoCombate();
      uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
      const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(0x823D6CF4)) : 0;
      if (base && mgr && e >= 4 && e <= 7) {
        const uint32_t bits = REX_LOAD_U32(mgr + 35144);
        float cuadros = 0;
        std::memcpy(&cuadros, &bits, sizeof(cuadros));
        if (std::isfinite(cuadros) && cuadros >= 0 && cuadros <= kTopeII * 60)
          g_cuadros_lucha = std::max(g_cuadros_lucha.load(), int(cuadros));
        empezo = true;
      }
      if (empezo && (e == 0 || e >= 5)) {
        break;
      }
    }
  }).detach();
}

void Pedir(int out[7]) {
  combate::OndasCero();
  combate::OndasModo(!g_2p.load() && g_prueba == 3);   // en 2P el rival es humano: no lo convertimos en muneco
  const int j = g_j3, r = g_r3;
  out[0] = j;
  out[1] = r;
  out[2] = EscenarioDelSitio(g_sitio);   // el alta de B2 (negativa) o su mapa de B3
  out[3] = g_prueba == 1 ? 1000 : g_prueba == 0 ? g_vida : 0;   // II: una barra; I: la que quedo (no se recupera)
  out[4] = kPruebas[g_prueba].tiempo;
  if (const char* t = std::getenv("DBZ3_TEST_NAVE_TIEMPO")) out[4] = std::atoi(t);   // pruebas: acorta el reloj
  out[5] = g_prueba;
  out[6] = g_2p.load() ? 1 : 0;   // P2 humano (camino nativo del vs de B3; 0 = como antes)
  if (g_prueba == 1) Reloj();
  REXLOG_INFO("dbz3 nave: prueba {}: {} contra {} en {} (vida {}, tiempo {}, vencidos {}, {}, regla {} s pago {})", g_prueba + 1,
              NombreDe(j), NombreDe(r), kEscenas2[g_sitio].es, out[3], out[4], g_vencidos.load(), g_2p.load() ? "2P humano" : "1P",
              kPruebas[g_prueba].tiempo, kPruebas[g_prueba].pago);   // B2_FASE2: comparar_modo b2 nave (regla sin el atajo de pruebas)
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
  std::vector<std::array<float, 5>> listab;   // PUL-B2: lista B {cuadro, r, g, b, a} (+0x00, +0x10..+0x1C de cada pieza)
};
std::vector<Tex> g_pz, g_frases;                 // 1131 (piezas), 1132 (frases de Babidi)
std::vector<Tex> g_pzv;                          // ventana de premio: DATA_<idioma> 1148 (#AMT: 0 "Get skill!",
                                                 // 1 estrella, 2 emblema, 3 "BOOM!!")
std::vector<Tex> g_cielo_tx;                     // cielo: DATA_CMN 912 (#AMT: 2 texturas de nube 256x256, blancas con alfa)
std::vector<Anim> g_anim, g_animv;               // 905 (nave) y 933 (ventana de premio)
std::vector<Tex> g_resultado_tx;                // Kili: textura * RGB del #AMA y despues FOGCOL (GS)
std::map<int, int> g_resultado_tex;             // clip 96..117 -> variante compartida, solo para el resultado
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
    for (uint32_t c = 0; c < nb && c < 16 && ob + 0x30 * (size_t(c) + 1) <= d.size(); ++c) {
      const size_t q = ob + 0x30 * size_t(c);
      a.listab.push_back({float(u32(q)), f32(q + 0x10), f32(q + 0x14), f32(q + 0x18), f32(q + 0x1C)});
    }
    out.push_back(a);
  }
  return out;
}

// ---- A2nave: #AMA completo (varias piezas, z) para la rueda y la burbuja (ver el bloque grande mas abajo)
struct Pz2 {
  float t = 0, alpha = 1;
  uint32_t niebla = 0x01000000;   // +0x0C: FOGCOL RGB + cantidad (FUN_001A7020 -> FUN_00138710)
  int tex = 0;
  float m[3] = {1, 1, 1};   // multiplicador de color (+0x10..+0x18: 0,5 = oscurecida)
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};
struct Clave2 {
  float t, sx, sy, rot, x, y, z, a;
};
struct Anim2 {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  std::vector<Clave2> k;   // claves (movimiento, escala, z)
  std::vector<Pz2> p;      // linea de piezas (alfa)
  float dur = 0;           // lo que dura para la tarea del original: max(+0x20, +0x24)
};
struct Ama2 {
  std::vector<Anim2> a;
  std::vector<Tex> tex;
  bool ok() const { return !a.empty() && !tex.empty(); }
};
Ama2 g_rueda, g_burb, g_cab_seleccion;
std::vector<Imagen> g_burb_cpu;
std::vector<Tex> g_burb_niebla;
std::vector<uint32_t> g_burb_color;
std::vector<Tex> g_ayuda;   // DATA_<idioma> 1145: las tiras ya compuestas de la barra de ayuda (11 Quick select, 27 Confirm, 32 Back, 3 Select Character)

std::vector<Anim2> LeerAma2(const std::vector<uint8_t>& d) {
  std::vector<Anim2> out;
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
  for (uint32_t i = 0; i < n && i < 512 && base + 0x40 * (size_t(i) + 1) <= d.size(); ++i) {
    const size_t o = base + 0x40 * size_t(i);
    Anim2 a;
    a.x0 = f32(o);
    a.y0 = f32(o + 4);
    a.x1 = f32(o + 0x10);
    a.y1 = f32(o + 0x14);
    a.dur = float(std::max(int32_t(u32(o + 0x20)), int32_t(u32(o + 0x24))));
    const uint32_t na = u32(o + 0x30), oa = u32(o + 0x34), nb = u32(o + 0x38), ob = u32(o + 0x3C);
    for (uint32_t c = 0; c < na && c < 64 && oa + 0x20 * (size_t(c) + 1) <= d.size(); ++c) {
      const size_t q = oa + 0x20 * size_t(c);
      a.k.push_back({float(u32(q)), f32(q + 4), f32(q + 8), f32(q + 0xC), f32(q + 0x10), f32(q + 0x14), f32(q + 0x18), f32(q + 0x1C)});
    }
    for (uint32_t c = 0; c < nb && c < 16 && ob + 0x30 * (size_t(c) + 1) <= d.size(); ++c) {
      const size_t q = ob + 0x30 * size_t(c);
      Pz2 p;
      p.t = float(u32(q));
      p.tex = int(u32(q + 4));
      p.niebla = u32(q + 0xC);
      p.alpha = f32(q + 0x1C);
      p.m[0] = f32(q + 0x10);
      p.m[1] = f32(q + 0x14);
      p.m[2] = f32(q + 0x18);
      p.u0 = f32(q + 0x20);
      p.v0 = f32(q + 0x24);
      p.u1 = f32(q + 0x28) + 1;
      p.v1 = f32(q + 0x2C) + 1;
      a.p.push_back(p);
    }
    out.push_back(std::move(a));
  }
  return out;
}


// Cielo del original. NO son capas de sprites (#AMA 906 / #AMT 910 era una suposicion): es un domo 3D que pinta la tarea de
// fondo de B2 (b2.c: FUN_0023DAE0 carga la pareja de la tabla 0x3A3EA8 = DATA_CMN 911 #AMO + 912 #AMT; FUN_0023D960(0x1501, 2)
// crea la tarea FUN_0023D520 -> FUN_0023D300 que cada cuadro hace FUN_0023CD00 (camara) y FUN_00140970 (el #AMO); la nave
// pide la variante 2 = el #AMG 2 del #AMO: 0x2644E0-0x265560/0x265AB0). Los colores (cupula azul noche a marron en el horizonte,
// 3 anillos de nube de color de vertice marron) vienen en los vertices del #AMO; las texturas del #AMT son blancas con alfa.
// FUN_0023D300 (variante != 0): desplaza las UV de las partes con textura +0,0005 * dt por cuadro en u y en v (dt = fGpffff866c =
// 1,2 por vsync a 50 Hz, 1 a 60 cuadros/s): 0,03 por segundo, envuelto a (-1, 1).
// Camara (FUN_0023CD00 con la variante != 0): en el origen mirando a (0, 0x3E13E814, -1) = (0, 0,14444, -1) (cabeceo 8,2 grados
// hacia arriba); proyeccion (FUN_00131450 + FUN_001317a0, valores en el GS: 409,6 / 546,133 px por tangente con 2048 de
// centro = 512 / 477,87 sobre la pantalla de 640x448, foco 512 a 4:3).
struct CieloV {
  float x, y, z, u, v;
  float c[4];
};
std::vector<std::array<CieloV, 3>> g_cielo_tri;   // triangulos del domo ya subdivididos (cada uno de 4x4 para la perspectiva)
std::vector<int> g_cielo_tex;                     // textura (-1 = solo color de vertice) de cada uno
std::vector<std::array<CieloV, 3>> g_principal_cielo_tri;
std::vector<int> g_principal_cielo_tex;
std::vector<Tex> g_principal_cielo_tx;
Ama2 g_principal;
std::vector<Tex> g_principal_ayuda;
std::vector<Tex> g_principal_botones;
ps2::Modelo g_principal_avion;
std::vector<ps2::Tex> g_principal_avion_tx;
std::array<ps2::Anim, 9> g_principal_avion_an;

// EU 0x23D520 FUN_0023d520 (carga los dos recursos del domo)
// EU 0x23DAE0 FUN_0023dae0 (pide los ids de la tabla gp-0x7E48 con FUN_0012D100, una vez)
// EU 0x23D960 FUN_0023d960 (crea la tarea del domo con la variante: la nave pide la 2)
bool CargarCielo(AfsPs2& cmn, rex::ui::ImmediateDrawer* dr, int variante = 2,
                 std::vector<std::array<CieloV, 3>>& g_cielo_tri = ::dbz3::pasarela::nave::g_cielo_tri,
                 std::vector<int>& g_cielo_tex = ::dbz3::pasarela::nave::g_cielo_tex,
                 std::vector<Tex>& g_cielo_tx = ::dbz3::pasarela::nave::g_cielo_tx) {
  g_cielo_tri.clear();
  g_cielo_tex.clear();
  g_cielo_tx.clear();
  ps2::Malla m;
  if (!ps2::LeerAmo(cmn.Entrada(911), variante, m, nullptr, true) || !m) {
    REXLOG_WARN("dbz3 nave cielo: DATA_CMN 911 (#AMO) no disponible (fondo liso)");
    return false;
  }
  const std::vector<uint8_t> amt = cmn.Entrada(912);
  for (int j = 0, n = std::min(CuantasAmt(amt), 8); j < n; ++j) {
    const Imagen im = TexturaAmt(amt, j);
    Tex x;
    if (im) {
      x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, true, im.rgba.data());
      x.w = float(im.w);
      x.h = float(im.h);
    }
    g_cielo_tx.push_back(std::move(x));
  }
  constexpr int N = 4;
  for (const auto& tr : m.tris) {
    auto v = [&](int i) {
      CieloV q{tr.p[i].x, tr.p[i].y, tr.p[i].z, tr.uv[i].x, tr.uv[i].y, {}};
      for (int k = 0; k < 4; ++k) q.c[k] = float((tr.col[i] >> (8 * k)) & 0xFF) / 255.0f;
      return q;
    };
    const CieloV a = v(0), b = v(1), c = v(2);
    // punto (i, j) de la malla de N x N sobre el triangulo: a + i/N (b - a) + j/N (c - a)
    auto pt = [&](int i, int j) {
      const float s = float(i) / N, t = float(j) / N;
      CieloV q;
      q.x = a.x + s * (b.x - a.x) + t * (c.x - a.x);
      q.y = a.y + s * (b.y - a.y) + t * (c.y - a.y);
      q.z = a.z + s * (b.z - a.z) + t * (c.z - a.z);
      q.u = a.u + s * (b.u - a.u) + t * (c.u - a.u);
      q.v = a.v + s * (b.v - a.v) + t * (c.v - a.v);
      for (int k = 0; k < 4; ++k) q.c[k] = a.c[k] + s * (b.c[k] - a.c[k]) + t * (c.c[k] - a.c[k]);
      return q;
    };
    for (int i = 0; i < N; ++i)
      for (int j = 0; j < N - i; ++j) {
        g_cielo_tri.push_back({pt(i, j), pt(i + 1, j), pt(i, j + 1)});
        g_cielo_tex.push_back(tr.tex >= 0 && tr.tex < int(g_cielo_tx.size()) && g_cielo_tx[size_t(tr.tex)].t ? tr.tex : -1);
        if (i + j < N - 1) {
          g_cielo_tri.push_back({pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)});
          g_cielo_tex.push_back(g_cielo_tex.back());
        }
      }
  }
  REXLOG_INFO("dbz3 nave cielo: DATA_CMN 911 #AMG {} + 912: {} triangulos de {}, {} texturas", variante, g_cielo_tri.size(), m.tris.size(),
              g_cielo_tx.size());
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
  CargarCielo(cmn, dr);
  {   // A2nave: rueda (CMN 901 + lang 1127) y burbuja (CMN 897 + CMN 898)
    auto texs = [&](const std::vector<uint8_t>& e, std::vector<Tex>& out) {
      for (int j = 0; j < std::min(CuantasAmt(e), 96); ++j) {
        const Imagen im = TexturaAmt(e, j);
        Tex x;
        if (im) {
          x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          x.w = float(im.w);
          x.h = float(im.h);
        }
        out.push_back(std::move(x));
      }
    };
    g_rueda.a = LeerAma2(cmn.Entrada(901));
    g_cab_seleccion.a = LeerAma2(cmn.Entrada(905));   // salida 120/123: el alfa tambien esta en la lista B
    // El lector simple de #AMA no consumia RGB/FOGCOL de la lista B: los Kili salian blancos.
    // Conservar el alfa y cocer una sola vez cada combinacion real del original (sin superponer capas).
    const auto resultado_amt = loc.Entrada(1131);
    std::map<std::tuple<int, uint32_t, float, float, float>, int> tonos;
    for (int k = 96; k <= 117 && k < int(g_cab_seleccion.a.size()); ++k) {
      const auto& piezas = g_cab_seleccion.a[size_t(k)].p;
      if (piezas.empty()) continue;
      const Pz2& p = piezas.front();
      const auto clave = std::make_tuple(p.tex, p.niebla, p.m[0], p.m[1], p.m[2]);
      if (auto it = tonos.find(clave); it != tonos.end()) { g_resultado_tex[k] = it->second; continue; }
      Imagen im = TexturaAmt(resultado_amt, p.tex);
      if (!im) continue;
      const float f = float(p.niebla >> 24) / 255.0f;
      for (size_t b = 0; b + 3 < im.rgba.size(); b += 4) for (int ch = 0; ch < 3; ++ch) {
        const float rgb = float(im.rgba[b + ch]) * p.m[ch];
        im.rgba[b + ch] = uint8_t(std::clamp(rgb * (1 - f) + float((p.niebla >> (8 * ch)) & 255) * f, 0.0f, 255.0f));
      }
      Tex tx;
      tx.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
      tx.w = float(im.w); tx.h = float(im.h);
      if (!tx.t) continue;
      const int id = int(g_resultado_tx.size());
      tonos[clave] = id; g_resultado_tex[k] = id;
      g_resultado_tx.push_back(std::move(tx));
    }
    texs(loc.Entrada(1127), g_rueda.tex);
    texs(loc.Entrada(1145), g_ayuda);
    g_burb.a = LeerAma2(cmn.Entrada(897));
    const auto burb_amt = cmn.Entrada(898);
    texs(burb_amt, g_burb.tex);
    for (int j = 0; j < int(g_burb.tex.size()); ++j) {
      g_burb_cpu.push_back(TexturaAmt(burb_amt, j));
    }
    g_burb_niebla.resize(g_burb.tex.size());
    g_burb_color.resize(g_burb.tex.size());
    REXLOG_INFO("dbz3 nave: rueda {} animaciones / {} texturas, burbuja {} / {}", g_rueda.a.size(), g_rueda.tex.size(), g_burb.a.size(),
                g_burb.tex.size());
  }
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
// EU 0x263900 FUN_00263900 (crea/reutiliza el objeto de musica y le da la pista 0x12 de la tabla 0x377E90 = ADX_US 1148 con FUN_002460D0)
void Musica(bool on) {
  if (on == g_sonando || g_iso.empty()) return;
  g_sonando = on;
  // 24/10 (como el tablero B2, CM): mientras suena la de B2 se calla la mezcla de B3 (si no, su musica de fondo suena debajo)
  rex::cvar::SetFlagByName("audio_gain", std::to_string(on ? 0.0 : std::clamp(dbz3::settings::MasterVolume(), 0.0, 1.0)));
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
//     FUN_00265F20 carga el banco con DAT_00378350..358 y FUN_002A68F0 recibe DAT_00378360/364/368; los dos IDs usados
//     por el modo son 1 y 2 y OirSonidoPs2 usa sus registros 0 y 1 del indice IECS DATA_<idioma> 1134.
//   canal 0 = menus: cursor 2, aceptar 5, volver 3 (0x264790 prueba, 0x264F60 1P/2P), 0x15 al abrir la pantalla 1P/2P
//     (0x2629B0) y 0x16 tras aceptar 1P. Banco de interfaz DATA_<idioma> 142/143, registro 21/22 (26 registros):
//     0x15 -> 21 en FUN_002629B0 y 0x16 -> 22 en FUN_00264F60.
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
// A2nave (studies 5): lienzo 4:3 como la TV del original (la PS2 emite 640x448 y la pantalla lo estira a 4:3): la x va a
// 1200/640 y la y a 900/448, es decir k = 1,0714 mas alto por pixel. Con eso el recuadro de la nave llena el area entera
// (antes iba con pixeles cuadrados y barras arriba y abajo) y las medidas del original (PCSX2 800x600) cuadran 1:1.
struct Lienzo {
  float s;
  float k = 1.0f;   // escala vertical / horizontal
  ImVec2 o;
  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s * k); }
};
Lienzo LienzoDe(const S::Area& a) {
  const float s = (1200.0f / 640.0f) * a.s;
  return {s, (900.0f / 448.0f) / (1200.0f / 640.0f), a.p0};
}
float g_cuadro = 1e9f;   // cuadros (60 por segundo) desde que cambio la pantalla
// Los objetos que siguen de una pantalla a otra no vuelven a entrar (B2 solo relanza la entrada si el objeto no esta ya
// en ella: 0x262830, 0x263A80): Babidi, el bocadillo (sale y entra al cambiar de abajo a arriba) y la cabecera (al
// cambiar de titulo). Cuadros desde su entrada.
float g_c_bab = 0, g_c_burb = 0, g_c_tit = 0;
bool g_voz_modo = false;   // la voz 1149 de Babidi espera a que la burbuja (anim 18) llegue a su bucle (EU 0x261880)

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
// PUL-B2 (dbz3_b2_fx_nativo): copia local de TintB (pasarela_b2.cpp) sobre la lista B de un Anim; c < 0 = la ultima clave.
// sin_rgb: solo el alfa (el rgb ya va horneado en la textura).
ImU32 TintBNave(ImU32 col, const std::vector<std::array<float, 5>>& r, float c, bool sin_rgb = false) {
  if (r.empty()) return col;
  std::array<float, 5> k = r.back();
  if (c >= 0 && c <= r.front()[0]) k = r.front();
  else if (c >= 0)
    for (size_t i = 1; i < r.size(); ++i)
      if (c < r[i][0]) {
        const float f = (c - r[i - 1][0]) / std::max(1e-3f, r[i][0] - r[i - 1][0]);
        for (int j = 1; j < 5; ++j) k[size_t(j)] = r[i - 1][size_t(j)] + (r[i][size_t(j)] - r[i - 1][size_t(j)]) * f;
        break;
      }
  if (sin_rgb) k[1] = k[2] = k[3] = 1.0f;
  auto m = [&](int sh, float f) { return ImU32(std::clamp(float((col >> sh) & 0xFF) * std::clamp(f, 0.0f, 1.0f) + 0.5f, 0.0f, 255.0f)) << sh; };
  return m(0, k[1]) | m(8, k[2]) | m(16, k[3]) | m(24, k[4]);
}
// RESCATE B2-NAVE (solo pruebas, DBZ3_TEST_NAVE_QUADS=carpeta): una vez por pantalla, a los 240 cuadros, los quads de DibC
// en coordenadas logicas 640x448 (b2-quads-v1, misma identidad que tools/b2_quads_original.py: imagen = primera anim con el
// mismo rect, instancia img<imagen>#k por (y0, x0)) en <carpeta>/port_nave_<estado>.json. Sin la variable no hace nada.
struct QNave {
  int ama, anim, img;
  float cuadro;
  ImVec2 p[4];
};
std::vector<QNave> g_nq;
bool g_nq_grabando = false;
void VolcarQuadsNave(const char* carpeta, int estado) {
  auto& q = g_nq;
  std::stable_sort(q.begin(), q.end(), [](const QNave& x, const QNave& y) {
    if (x.ama != y.ama) return x.ama < y.ama;
    if (x.anim != y.anim) return x.anim < y.anim;
    if (x.cuadro != y.cuadro) return x.cuadro < y.cuadro;
    if (x.img != y.img) return x.img < y.img;
    return x.p[0].y != y.p[0].y ? x.p[0].y < y.p[0].y : x.p[0].x < y.p[0].x;
  });
  std::string js;
  char b[360];
  for (size_t i = 0, k = 0; i < q.size(); ++i) {
    k = i && q[i].ama == q[i - 1].ama && q[i].anim == q[i - 1].anim && q[i].cuadro == q[i - 1].cuadro && q[i].img == q[i - 1].img ? k + 1 : 0;
    std::snprintf(b, sizeof(b),
                  "%s{\"ama\":%d,\"anim\":%d,\"cuadro\":%.1f,\"instancia\":\"img%d#%zu\",\"xy\":[[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f]]}",
                  i ? "," : "", q[i].ama, q[i].anim, double(q[i].cuadro), q[i].img, k, double(q[i].p[0].x), double(q[i].p[0].y),
                  double(q[i].p[1].x), double(q[i].p[1].y), double(q[i].p[2].x), double(q[i].p[2].y), double(q[i].p[3].x), double(q[i].p[3].y));
    js += b;
  }
  const std::string ruta = std::string(carpeta) + "/port_nave_" + std::to_string(estado) + ".json";
  if (std::FILE* f = std::fopen(ruta.c_str(), "wb")) {
    std::fprintf(f, "{\"formato\":\"b2-quads-v1\",\"escena\":\"nave_%d\",\"region\":\"PAL\",\"campo\":-1,\"plano\":[640,448],"
                    "\"viewport\":[0,0,640,448],\"completo\":true,\"origen\":\"port\",\"quads\":[%s]}\n", estado, js.c_str());
    std::fclose(f);
    REXLOG_INFO("dbz3 nave: RESCATE-B2-NAVE: {} quads de la pantalla {} en {}", q.size(), estado, ruta);
  }
  q.clear();
}
// PUL-B2: una linea por elemento portado (la primera vez que se pinta con el ajuste)
void PulB2LogNave(int id, const char* txt) {
  static uint32_t vistos = 0;
  if (vistos >> id & 1u) return;
  vistos |= 1u << id;
  REXLOG_INFO("dbz3 nave: PUL-B2: {}", txt);
}
// PUL-B2 (dbz3_b2_fx_nativo): en B2 la cruz (0x4000) suena FUN_00147260(0,0,3,0) y el triangulo (0x1000) (0,0,5,0):
// menu FUN_0025F7D0 (b2.c:209208 / 209238), plantas FUN_00264790 (211712 / 211730; 0x4000 -> 211730), burbuja FUN_002AFEB0
// (253199 / 253156) y rueda FUN_002AB8C0 (cruz 250756). El port los tenia al reves; apagado = como antes.
int SonidoB2(int antes, int nativo) {
  if (!REXCVAR_GET(dbz3_b2_fx_nativo)) return antes;
  PulB2LogNave(3, "Nave sonidos de confirmar (3) y volver (5) como FUN_0025F7D0/FUN_00264790/FUN_002AFEB0/FUN_002AB8C0");
  return nativo;
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
  auto lg = [&](float lx, float ly) {   // coordenadas logicas 640x448
    lx *= q.sx;
    ly *= q.sy;
    return ImVec2(q.x + dx + lx * cs - ly * sn, q.y + dy + lx * sn + ly * cs);
  };
  auto v = [&](float lx, float ly) {
    const ImVec2 p = lg(lx, ly);
    return L.P(p.x, p.y);
  };
  ImU32 cc = (col & 0x00FFFFFF) | (ImU32(float(col >> 24) * std::min(1.0f, q.a)) << 24);
  ImTextureID tex = reinterpret_cast<ImTextureID>(p.t.get());
  bool horneada = false;   // Kili del resultado: el rgb de la lista B ya va dentro de la textura (g_resultado_tx)
  if (!set && !pzs) if (auto it = g_resultado_tex.find(k); it != g_resultado_tex.end()) {
    tex = reinterpret_cast<ImTextureID>(g_resultado_tx[size_t(it->second)].t.get());
    horneada = true;
  }
  if (REXCVAR_GET(dbz3_b2_fx_nativo) && !a.listab.empty()) {
    // PUL-B2: el dibujante de #AMA del original (FUN_001a8d10) multiplica SIEMPRE por la lista B (rgb y alfa, lineal
    // entre claves; en reposo en bucle con su propio periodo). Antes la Nave (905/933) solo leia la lista A.
    const float pb = a.listab.back()[0];
    const float cb = vuelta && pb > 0 ? std::fmod(std::max(0.0f, c), pb) : c;
    cc = TintBNave(cc, a.listab, cb, horneada);
    if (cc >> 24 == 0) return;
    PulB2LogNave(set == &g_animv ? 1 : 0, set == &g_animv ?
        "Nave ventana de premio #AMA 933 con el rgb y alfa de su lista B (FUN_001a8d10)" :
        "Nave torre/menus/resultado #AMA 905 con el rgb y alfa de su lista B (FUN_001a8d10)");
  }
  if (g_nq_grabando) {   // RESCATE B2-NAVE (pruebas): rect tal cual, como pose + rect del registro del original
    int img = k;
    for (int i = 0; i < k; ++i) {
      const Anim& o = anims[size_t(i)];
      if (o.x0 == a.x0 && o.y0 == a.y0 && o.x1 == a.x1 && o.y1 == a.y1) {
        img = i;
        break;
      }
    }
    g_nq.push_back({set == &g_animv ? 933 : set ? 0 : 905, k, img, std::round(std::max(0.0f, c) * 10.0f) / 10.0f,
                    {lg(a.x0, a.y0), lg(a.x1, a.y0), lg(a.x1, a.y1), lg(a.x0, a.y1)}});
  }
  dl->AddImageQuad(tex, v(a.x0, a.y0), v(a.x1 + 1, a.y0), v(a.x1 + 1, a.y1 + 1),
                   v(a.x0, a.y1 + 1), ImVec2(a.u0 / p.w, a.v0 / p.h), ImVec2(a.u1 / p.w, a.v0 / p.h),
                   ImVec2(a.u1 / p.w, a.v1 / p.h), ImVec2(a.u0 / p.w, a.v1 / p.h), cc);
}
float DurDe(int k, const std::vector<Anim>* set = nullptr) {
  const std::vector<Anim>& anims = set ? *set : g_anim;
  return k >= 0 && k < int(anims.size()) ? anims[size_t(k)].Dur() : 0.0f;
}
// Espera real de la tarea del original: FUN_00245AD0 copia a +0x38 max(+0x20, +0x24) del clip y el
// estado cambia cuando el contador (+0x34) llega a esa espera (FUN_002459C0/0x261B20/0x261C50/0x261FD0).
// No es la ultima clave del dibujo (Dur()), que puede ser menor. `dur_tarea` ya se lee en LeerAma.
float DurTarea(int k, const std::vector<Anim>* set = nullptr) {
  const std::vector<Anim>& anims = set ? *set : g_anim;
  if (k < 0 || k >= int(anims.size())) return 0.0f;
  const Anim& a = anims[size_t(k)];
  return a.dur_tarea > 0 ? a.dur_tarea : a.Dur();
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

// Pinta el domo del cielo (CargarCielo) en la pantalla de 640x448 de `L`; `t` en segundos. Camara y proyeccion: ver CargarCielo.
// EU 0x140970 FUN_00140970 (dibuja el #AMO del domo)
void DibCielo(ImDrawList* dl, const Lienzo& L, float t, bool principal = false) {
  const auto& triangulos = principal ? g_principal_cielo_tri : g_cielo_tri;
  const auto& texturas = principal ? g_principal_cielo_tex : g_cielo_tex;
  const auto& imagenes = principal ? g_principal_cielo_tx : g_cielo_tx;
  // EU 0x23CD00 FUN_0023cd00
  static const float kDy = [] {
    const uint32_t b = 0x3E13E814;   // FUN_0023CD00: componente y del punto al que mira
    float f;
    std::memcpy(&f, &b, 4);
    return f;
  }();
  const float dy = principal ? 0.0f : kDy;
  const float kCs = 1.0f / std::sqrt(1.0f + dy * dy), kSn = dy * kCs;
  // EU 0x131450 FUN_00131450
  // EU 0x1317A0 FUN_001317a0
  // FUN_0023CD00 cambia el limite de recorte a 1977, NO el foco (param[15], 512).
  // El desplazamiento de 70 esta en coordenadas GS: factores .8 y 16/15 del original.
  const float kFx = 512.0f, kFy = kFx * 448.0f / 480.0f;
  constexpr float kCerca = 1.0f;
  // EU 0x23D300 FUN_0023d300
  const float sc = std::fmod(0.03f * t, 1.0f);   // FUN_0023D300: +0,0005 * dt por cuadro (0,03 por segundo) en u y v
  const ImVec2 blanco = ImGui::GetFontTexUvWhitePixel();
  struct Q {
    float x, y, z, u, v, c[4];
  };
  int tex_ant = -1;
  for (size_t i = 0; i < triangulos.size(); ++i) {
    const int tx = texturas[i];
    Q q[4];
    int n = 0;
    {   // vertices en el espacio de la camara (x a la derecha, y hacia abajo, z hacia delante) y recorte contra z >= kCerca
      Q a[3];
      for (int k = 0; k < 3; ++k) {
        const CieloV& p = triangulos[i][size_t(k)];
        a[k] = {p.x, -(kCs * p.y + kSn * p.z), kSn * p.y - kCs * p.z, p.u + (tx >= 0 ? sc : 0.0f), p.v + (tx >= 0 ? sc : 0.0f),
                {p.c[0], p.c[1], p.c[2], p.c[3]}};
      }
      for (int k = 0; k < 3; ++k) {
        const Q &p = a[k], &r = a[(k + 1) % 3];
        const bool dentro = p.z >= kCerca, dentro_r = r.z >= kCerca;
        if (dentro) q[n++] = p;
        if (dentro != dentro_r) {
          const float f = (kCerca - p.z) / (r.z - p.z);
          Q m;
          m.x = p.x + f * (r.x - p.x);
          m.y = p.y + f * (r.y - p.y);
          m.z = kCerca;
          m.u = p.u + f * (r.u - p.u);
          m.v = p.v + f * (r.v - p.v);
          for (int j = 0; j < 4; ++j) m.c[j] = p.c[j] + f * (r.c[j] - p.c[j]);
          q[n++] = m;
        }
      }
    }
    if (n < 3) continue;
    ImVec2 s[4];
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int k = 0; k < n; ++k) {
      const float sx = 320.0f + (principal ? 70.0f / 0.8f : 0.0f) + kFx * q[k].x / q[k].z,
                  sy = 224.0f + (principal ? 70.0f / (16.0f / 15.0f) : 0.0f) + kFy * q[k].y / q[k].z;
      s[k] = L.P(sx, sy);
      x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
    }
    if (x1 < 0 || x0 > 640 || y1 < 0 || y0 > 448) continue;
    if (tx != tex_ant) {
      if (tex_ant >= 0) dl->PopTexture();
      if (tx >= 0) dl->PushTexture(ImTextureRef(reinterpret_cast<ImTextureID>(imagenes[size_t(tx)].t.get())));
      tex_ant = tx;
    }
    auto col = [](const Q& v) {
      auto c8 = [](float f) { return int(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
      return IM_COL32(c8(v.c[0]), c8(v.c[1]), c8(v.c[2]), c8(v.c[3]));
    };
    for (int k = 1; k + 1 < n; ++k) {
      dl->PrimReserve(3, 3);
      for (const int j : {0, k, k + 1}) dl->PrimVtx(s[j], tx >= 0 ? ImVec2(q[j].u, q[j].v) : blanco, col(q[j]));
    }
  }
  if (tex_ant >= 0) dl->PopTexture();
}

// Fondo: los lados de la ventana con la madera del marco del original (pieza 0) un poco oscurecida, como hacen las
// remasterizaciones con las pantallas 4:3; dentro, el cielo 3D, la nave (71), el marco (4-7) y la cinta (13).
void Fondo(ImDrawList* dl, const Lienzo& L, bool cielo, float t) {
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 255));
  if (!g_pz.empty() && g_pz[0].t && L.s > 0) {
    const float w = 40 * L.s, h = 312 * L.s * L.k, x0 = L.o.x, x1 = L.o.x + 640 * L.s;
    const ImTextureID id = reinterpret_cast<ImTextureID>(g_pz[0].t.get());
    for (float y = 0; y < ds.y; y += h) {
      for (float x = x0 - w; x > -w; x -= w) dl->AddImage(id, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(120, 120, 120, 255));
      for (float x = x1; x < ds.x; x += w) dl->AddImage(id, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(120, 120, 120, 255));
    }
  }
  if (g_cielo_tri.empty()) {
    dl->AddRectFilled(L.P(0, 0), L.P(640, 448), IM_COL32(34, 22, 52, 255));   // sin DATA_CMN 911/912: fondo liso
  } else {
    dl->PushClipRect(L.P(0, 0), L.P(640, 448), true);
    DibCielo(dl, L, t);
    dl->PopClipRect();
  }
  if (cielo) DibC(dl, L, 124, 0, false);   // el cielo va detras de la nave y del marco (0x2644E0)
  DibC(dl, L, 71, g_cuadro, true);
  for (int k = 4; k <= 7; ++k) DibC(dl, L, k, 0, false);
  DibC(dl, L, 13, 0, false);
}
// Babidi (36 -> 37) y su bocadillo con la frase k de 1132 a su tamano, centrada: el de abajo (18 -> 19) o, en la
// seleccion, el de arriba a la derecha (21 -> 22).
// Barra de ayuda inferior de la rueda de B2: rotula "L1 button/ R1 button + [cruz]: Quick select  [X]: Confirm  [triangulo]: Back
// [cruz]: Select Character" y se desplaza hacia la izquierda en bucle (velocidad kAyudaVel medida contra B2).
constexpr float kAyudaVel = 75.0f;   // px de lienzo (640x448) por segundo: medido en PCSX2 (280 px de 800 en 3 s, 5 pares)
void AyudaB2(ImDrawList* dl, const Lienzo& L, float t) {
  {   // las tiras de B2 (DATA_<idioma> 1145), en el mismo orden que la barra original
    static const int kTiras[4] = {11, 27, 32, 3};
    bool hay = g_ayuda.size() > 32;
    for (int k : kTiras) hay = hay && g_ayuda[size_t(k)].t;
    if (hay) {
      static int ult = -1;
      if (ImGui::GetFrameCount() == ult) return;
      ult = ImGui::GetFrameCount();
      const float hueco = 28 * L.s;
      float ancho = 0;
      for (int k : kTiras) ancho += g_ayuda[size_t(k)].w * L.s + hueco;
      const ImVec2 q0 = L.P(0, 394), q1 = L.P(640, 438);
      const float cy = (q0.y + q1.y) * 0.5f, off = std::fmod(t * kAyudaVel * L.s, ancho);
      dl->PushClipRect(q0, q1, true);   // solo se recorta en los bordes del marco 4:3
      for (float x0 = q0.x - off; x0 < q1.x; x0 += ancho) {
        float x = x0;
        for (int k : kTiras) {
          const Tex& tx = g_ayuda[size_t(k)];
          dl->AddImage(reinterpret_cast<ImTextureID>(tx.t.get()), ImVec2(x, cy - tx.h * 0.5f * L.s), ImVec2(x + tx.w * L.s, cy + tx.h * 0.5f * L.s));
          x += tx.w * L.s + hueco;
        }
      }
      dl->PopClipRect();
      return;
    }
  }
  static int ultimo = -1;   // una sola vez por fotograma de la interfaz (dos pasadas con otro `t` se solapaban)
  if (ImGui::GetFrameCount() == ultimo) return;
  ultimo = ImGui::GetFrameCount();
  enum { kTxt, kCruz, kEquis, kTri, kCaja };
  struct Tok { int k; const char* s; };
  static const Tok kT[] = {{kCaja, "L1"}, {kTxt, "button/"}, {kCaja, "R1"}, {kTxt, "button +"}, {kCruz, ""}, {kTxt, ":Quick select"}, {kEquis, ""}, {kTxt, ":Confirm"},
                           {kTri, ""}, {kTxt, ":Back"}, {kCruz, ""}, {kTxt, ":Select Character"}};
  ImFont* fnt = ImGui::GetFont();
  const float fs = 19 * L.s, ic = 17 * L.s, esp = 7 * L.s, hueco = 90 * L.s;
  auto pasada = [&](float x0, float cy, bool pinta) {
    float x = x0;
    for (const Tok& k : kT) {
      if (k.k == kTxt) {
        const ImVec2 ts = fnt->CalcTextSizeA(fs, FLT_MAX, 0, k.s);
        if (pinta) {
          const ImVec2 p(x, cy - ts.y * 0.5f);
          for (int a = 0; a < 8; ++a)
            dl->AddText(fnt, fs, ImVec2(p.x + 1.5f * L.s * std::cos(a * 0.7853982f), p.y + 1.5f * L.s * std::sin(a * 0.7853982f)), IM_COL32(20, 20, 30, 255), k.s);
          dl->AddText(fnt, fs, p, IM_COL32(245, 245, 235, 255), k.s);
        }
        x += ts.x + esp;
      } else if (k.k == kCaja) {   // boton L1 / R1 de la PS2: caja gris con su rotulo
        const ImVec2 ts = fnt->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0, k.s);
        const float w = ts.x + 8 * L.s;
        if (pinta) {
          dl->AddRectFilled(ImVec2(x, cy - ic * 0.5f), ImVec2(x + w, cy + ic * 0.5f), IM_COL32(95, 95, 105, 255), 4 * L.s);
          dl->AddRect(ImVec2(x, cy - ic * 0.5f), ImVec2(x + w, cy + ic * 0.5f), IM_COL32(20, 20, 30, 255), 4 * L.s, 0, 1.5f * L.s);
          dl->AddText(fnt, fs * 0.8f, ImVec2(x + 4 * L.s, cy - ts.y * 0.5f), IM_COL32_WHITE, k.s);
        }
        x += w + esp;
      } else {
        if (pinta) {
          const ImVec2 c(x + ic * 0.5f, cy);
          const float r = ic * 0.5f;
          if (k.k == kCruz) {
            const ImU32 col = IM_COL32(235, 235, 235, 255);
            dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.33f), ImVec2(c.x + r, c.y + r * 0.33f), col);
            dl->AddRectFilled(ImVec2(c.x - r * 0.33f, c.y - r), ImVec2(c.x + r * 0.33f, c.y + r), col);
          } else if (k.k == kEquis) {
            const ImU32 col = IM_COL32(90, 140, 255, 255);
            dl->AddLine(ImVec2(c.x - r * 0.7f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), col, 3.5f * L.s);
            dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), col, 3.5f * L.s);
          } else {
            dl->AddTriangle(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + r * 0.9f, c.y + r * 0.7f), ImVec2(c.x - r * 0.9f, c.y + r * 0.7f), IM_COL32(60, 210, 120, 255), 3.0f * L.s);
          }
        }
        x += ic + esp;
      }
    }
    return x - x0;
  };
  const ImVec2 p0 = L.P(0, 394), p1 = L.P(640, 438);
  const float cy = (p0.y + p1.y) * 0.5f, ancho = pasada(0, cy, false) + hueco;
  const float off = std::fmod(t * kAyudaVel * L.s, ancho);
  dl->PushClipRect(p0, p1, true);
  for (float x = p0.x - off; x < p1.x; x += ancho) pasada(x, cy, true);
  dl->PopClipRect();
}

void Babidi(ImDrawList* dl, const Lienzo& L, int frase, bool arriba, float frase_alfa = 1.0f) {
  // 36 es la entrada y 37 el gesto hablado de reposo. Mantener el reloj propio
  // del personaje (independiente de los cambios de pantalla) evita congelarlo
  // cuando la frase del bocadillo no cambia.
  const float cb = std::max(0.0f, g_cuadro - g_c_bab);
  Obj(dl, L, 36, 37, cb);
  const int e = arriba ? 21 : 18, r = arriba ? 22 : 19;
  Obj(dl, L, e, r, g_cuadro - g_c_burb);
  if (g_c_burb < DurDe(e) || frase < 0 || frase >= int(g_frases.size()) || !g_frases[size_t(frase)].t ||
      r >= int(g_anim.size()))
    return;
  const Anim& b = g_anim[size_t(r)];
  const Pose q = PoseDe(b, 0, false);
  const Tex& f = g_frases[size_t(frase)];
  // RESCATE B2-NAVE (dbz3_b2_nave_frase_pose): FUN_00245510 (b2.c:196975-196991) pone la frase con el rect
  // (-w/2, -h/2, w/2-1, h/2-1) en la POSE de la anim del bocadillo (+0x3C), no en el centro de su rect: con el rect de la
  // 19 (-216,-80,231,79) el centro iba 7,5 px a la derecha y 0,5 arriba; con la 22 (-160,-80,159,87), 0,5 y 3,5.
  const bool en_pose = REXCVAR_GET(dbz3_b2_nave_frase_pose);
  const float cx = q.x + (en_pose ? 0.0f : (b.x0 + b.x1) * 0.5f), cy = q.y + (en_pose ? 0.0f : (b.y0 + b.y1) * 0.5f);
  if (g_nq_grabando)   // RESCATE B2-NAVE (pruebas): la frase, como el registro del original (borde final inclusivo)
    g_nq.push_back({905, r, -1, 0.0f, {ImVec2(cx - f.w * 0.5f, cy - f.h * 0.5f), ImVec2(cx + f.w * 0.5f - 1, cy - f.h * 0.5f),
                                       ImVec2(cx + f.w * 0.5f - 1, cy + f.h * 0.5f - 1), ImVec2(cx - f.w * 0.5f, cy + f.h * 0.5f - 1)}});
  dl->AddImage(reinterpret_cast<ImTextureID>(f.t.get()), L.P(cx - f.w * 0.5f, cy - f.h * 0.5f),
               L.P(cx + f.w * 0.5f, cy + f.h * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
               IM_COL32(255, 255, 255, int(std::clamp(frase_alfa, 0.0f, 1.0f) * 255)));
}

#include "pasarela_nave_rueda.inl"

// ---- La burbuja de escenarios
// Casillas de g_cel, filtradas por ConstruirCasillas() con nave_esc (eventos 0x1FE..0x206); solo las pruebas abren todas.
void DibBurbuja(const Lienzo& L, float c, float t_ant_dummy) {
  if (!g_burb.ok()) return;
  const Giro2& g = g_giro_burb;
  const int n = int(g_cel.size());
  float R = 180.0f, cab = float(kAnilloBurb.th0), roll = 0;
  if (c < 45.0f) {
    const AnilloEntrada e = EntradaAnillo(c, 45.0f, float(kAnilloBurb.th0));
    R = e.R;
    cab = e.cab;
    roll = e.roll;
  }
  const ImVec2 off(-160, 0);   // la burbuja va a la izquierda: el desplazamiento del panel (S + 0x60, gp - 0x7C94)
  for (int i = 0; i < n; ++i) {
    float x, y, z;
    AnilloPos(kAnilloBurb, R, cab, kAnilloBurb.arco * (float(i) - g.pos) / float(n) + roll, x, y, z);
    if (SaliendoBurbuja() && i < int(g_sel.puntos.size())) {
      const auto& p = g_sel.puntos[size_t(i)];
      x = p.x + p.vx * g_sel.c; y = p.y + p.vy * g_sel.c; z = p.z;
    }
    const float s = 1.0f - (z - kAnilloBurb.z0) * 0.0001f;
    const ImVec2 p(x, y);
    Spr2(L, g_burb, 29 + g_cel[size_t(i)], 0, false, ImVec2(0, 0), &p, s, 1.0f, z);
  }
  const int imagen = 19 + g_cel[size_t(std::clamp(g.obj, 0, n - 1))];
  if (SaliendoBurbuja()) {
    // Tabla ELF 0x37B940: bola 8, cristal 11, flash 14, marco 5, imagen 18, flechas 2/41.
    for (int anim : {8, 11, 14, 5}) Spr2(L, g_burb, anim, g_sel.c, false, off);
    Spr2b(L, g_burb, imagen, 18, g_sel.c, false, off, 216, 1);
    if (g.obj > 0) Spr2(L, g_burb, 2, g_sel.c, false, off);
    if (g.obj < n - 1) Spr2(L, g_burb, 41, g_sel.c, false, off);
    return;
  }
  Obj2(L, g_burb, 6, 7, c, off);          // la bola (tex 2): alfa y destello del AMA, sin multiplicador inventado
  const float c2 = c - 45.0f;
  if (c < 45.0f) {
    // slot 4 (anim 12, 3 piezas con alfa 0 -> 1 -> 0): destello de la imagen mientras gira el anillo
    Spr2b(L, g_burb, imagen, 12, c, false, off, 216, 1);
  } else {
    const float ci = std::max(0.0f, t_ant_dummy - g_burb_imagen_t) * 60.0f;
    const bool confirma = g_sel.fase == SelFase::burbujaConfirma;
    const float de = DurAma2(g_burb, 16);
    Spr2b(L, g_burb, imagen, confirma ? 19 : ci < de ? 16 : 17,
          confirma ? g_sel.c : ci < de ? ci : ci - de, !confirma && ci >= de, off, 216, 1);
    Obj2(L, g_burb, 3, 4, c2, off);                              // el marco naranja de la casilla elegida
    Obj2(L, g_burb, 9, 10, c2, off);                             // el cristal (tex 3): 0,7
    if (g.obj > 0) Obj2(L, g_burb, 0, 1, c2, off);               // flecha izquierda (se esconde en el primero)
    if (g.obj < n - 1) Obj2(L, g_burb, 39, 40, c2, off);         // flecha derecha (se esconde en el ultimo)
  }
}

// ---- Resultado (escena 0x1A): cuenta -> 41 campos -> barra con premios -> 41 campos -> prueba.
struct Res {
  int fase = 0;            // 0 cuenta, 1 espera, 2 barra, 3 premio, 4 espera final
  float acc = 0, espera = 0;
  double campos = 0;
  bool ok_pendiente = false;
  int campo = 0;
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
// B2-10: la pantalla de resultado del original dura 7-10 s con Kili (cuenta + barra) y ~1,6 s con 0 (dorados del
// 10/11: III "17 Hits" y IV "0 times"). El reparto del port mete ~11 s en el cierre de B3, asi que alargar el resultado
// sin tocar ese cierre se sale de la ventana fin->torre: la espera extra va APAGADA (0) y solo se enciende en pruebas
// (dbz3_nave_resultado_seg). Medida del reparto: tools/s4_nave_hud.py resultado.
float g_res_espera = 0, g_res_extra = 0;
float EsperaResultadoB2() {
  const std::string v = rex::cvar::GetFlagByName("dbz3_nave_resultado_seg");
  return std::clamp(float(std::atof(v.c_str())), 0.0f, 30.0f);
}

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
// EU 0x265AB0 FUN_00265ab0 (pago por prueba, cuenta, premios que quedan = max - tienes)
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
  g_res_espera = 0;
  g_res_extra = EsperaResultadoB2();
  if (g_res_extra > 0)
    REXLOG_INFO("dbz3 nave: resultado: espera extra de {:.1f} s (dbz3_nave_resultado_seg)", g_res_extra);
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
// Un campo PAL del original: el acumulador de Kili suma 1,2, pero los callbacks
// 0x263670/0x262C50 incrementan un BYTE en 1, no dt (b2.c:211112/210783).
bool PasoResultadoCampo(bool ok) {
  auto& r = g_res;
  const float cuadros = 60.0f / float(g_hz_b2.load());
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
    // EU 0x263670 FUN_00263670
    case 1:   // 0x263670: 41 campos; sin Kili se acaba
      if (++r.espera > 40) {
        if (r.total < 1) return true;
        if (r.barra > 9999) Vuelta();
        r.sig = Siguiente();
        r.fase = 2;
        r.acc = 0;
      }
      break;
    // EU 0x262DD0 FUN_00262dd0
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
    // EU 0x262D50 FUN_00262d50
    // EU 0x263540 FUN_00263540 (busca el siguiente premio)
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
    // EU 0x262CC0 FUN_00262cc0
    // EU 0x262C50 FUN_00262c50
    default:   // 0x262CC0 / 0x262C50: guardado y 41 campos
      if (++r.espera > 40) return true;
  }
  return false;
}
// Separar los campos PAL de los Hz del monitor. Asi no se adelanta una pausa ni
// se emiten varios pasos de sonido en el mismo campo cuando baja el framerate.
bool PasoResultado(float cuadros, bool ok) {
  auto& r = g_res;
  r.ok_pendiente |= ok;
  r.campos += double(cuadros) * (double(g_hz_b2.load()) / 60.0);
  while (r.campos >= 1.0 - 1e-5) {
    r.campos = std::max(0.0, r.campos - 1.0);
    ++r.campo;
    const bool fin = PasoResultadoCampo(r.ok_pendiente);
    r.ok_pendiente = false;
    if (std::getenv("DBZ3_TEST_NAVE_CUADRO"))
      REXLOG_INFO("dbz3 b2t: resultado campo {} fase {} cuenta {} ganado {} barra {} espera {} fin {}",
                  r.campo, r.fase, r.cuenta, r.ganado, r.barra, int(r.espera), fin);
    if (fin) return true;
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
    const float w = t.w * 0.5f * e * sx * L.s, h = t.h * 0.5f * e * sy * L.s * L.k;
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
// EU 0x2A44C0 FUN_002a44c0 (crea la ventana "Get skill!" con DATA_CMN 933 + DATA_<idioma> 1148/542 y el nombre del premio)
// EU 0x291C40 FUN_00291c40 / EU 0x2924A0 FUN_002924a0 (carga y entrada de la ficha "Skill Z Sheet", DL 541/542)
// EU 0x291C40 FUN_00291c40
// EU 0x2924A0 FUN_002924a0
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
    // El original no superpone otra linea de Kili al cartel/humo.
  }
}

}  // namespace

// solP4: salida real de la Nave -> menu principal de B2, no al Gateway.
// Recursos de FUN_0023FA30: tabla ELF 0x377DC0 = loc 1136, CMN 913, loc 1137.
// Etiquetas/posiciones/color de FUN_0023E8E0: patrones 23/28..33; elegida 22,
// cinta 13, titulo 16 y bocadillo 19. Se leen de la ISO, no se distribuyen.
int DibujarPrincipalB2(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, int& cursor) {
  static bool cargado = false;
  static bool sonando = false;
  static int destino = 0, cursor_anterior = -1;
  static float salida = -1, fila_desde = 0;
  auto salir = [&](int r) {
    if (sonando) {
      CallarMusicaPs2();
      rex::cvar::SetFlagByName("audio_gain", std::to_string(std::clamp(dbz3::settings::MasterVolume(), 0.0, 1.0)));
      sonando = false;
    }
    return r;
  };
  if (!cargado) {
    auto* dr = dbz3::launcher::PreviewDrawer();
    AfsPs2 cmn, loc;
    const auto iso = BuscarJuego("budokai 2");
    const std::string lc = T::LangCode();
    const std::string afs = lc == "es" ? "DATA_SP.AFS" : lc == "fr" ? "DATA_FR.AFS" : lc == "it" ? "DATA_IT.AFS"
                            : lc == "de" ? "DATA_DU.AFS" : "DATA_UK.AFS";
    if (!dr || !cmn.Abrir(iso, "DATA_CMN.AFS") || (!loc.Abrir(iso, afs) && !loc.Abrir(iso, "DATA_UK.AFS"))) return salir(-1);
    auto texturas = [&](int entrada, std::vector<Tex>& dst) {
      const auto amt = loc.Entrada(size_t(entrada));
      for (int k = 0; k < std::min(64, CuantasAmt(amt)); ++k) {
        const Imagen im = TexturaAmt(amt, k);
        Tex tx;
        if (im) {
          tx.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          tx.w = float(im.w); tx.h = float(im.h);
        }
        dst.push_back(std::move(tx));
      }
    };
    g_principal.a = LeerAma2(cmn.Entrada(913));
    texturas(1136, g_principal.tex);
    texturas(1137, g_principal_ayuda);
    texturas(1145, g_principal_botones);
    CargarCielo(cmn, dr, 0, g_principal_cielo_tri, g_principal_cielo_tex, g_principal_cielo_tx);
    // Avion del menu: tabla 0x377D90 (908 #AMO, 909 #AMT, 907 #AMM),
    // FUN_0023CE20: traslacion (-80,-20,-145), giro -2000/65536 de vuelta.
    g_principal_avion = ps2::CargarSuelto(cmn.Entrada(908), cmn.Entrada(909), true, true);
    g_principal_avion_tx = ps2::Texturas(g_principal_avion);
    if (g_principal_avion.esq) {
      const auto amm = cmn.Entrada(907);
      for (size_t k = 0; k < g_principal_avion_an.size(); ++k) {
        auto& an = g_principal_avion_an[k];
        an = ps2::LeerAnim(amm, uint32_t(k), g_principal_avion.esq->padre.size());
        an.relativa = true;
      }
    }
    cargado = true;
    REXLOG_INFO("dbz3 nave: menu principal B2: CMN 913 + loc 1136/1137 cargados");
  }
  const bool nave = LeerProgreso("tablero_b2_nave", 0) != 0;
  const int n = nave ? 7 : 6;
  cursor = std::clamp(cursor, 0, n - 1);
  if (!sonando) {
    destino = 0; salida = -1; fila_desde = DurAma2(g_principal, 0); cursor_anterior = cursor;
    // FUN_0023F4B0: indice 13 de la tabla 0x377E90 = ADX_US 1155.
    sonando = true;
    rex::cvar::SetFlagByName("audio_gain", "0");
    OirMusicaPs2(BuscarJuego("budokai 2"), "ADX_US.AFS", 1155, Volumen());
  }
  // FUN_0023CE20: el #AMM avanza UN cuadro por campo (PAL = 50 Hz).
  // FUN_0023F430 espera al clip 0 (30 campos) antes de montar el #AMA.
  float campo = std::floor(std::max(0.0f, t) * 50.0f + 0.001f);
  // Arnés PINE: mismo índice entero de campo, sin depender del tiempo de una captura.
  if (const char* ruta = std::getenv("DBZ3_TEST_B2_MENU_CAMPO")) if (FILE* fp = std::fopen(ruta, "r")) {
    int fijo = -1;
    if (std::fscanf(fp, "%d", &fijo) == 1 && fijo >= 0) campo = float(fijo);
    std::fclose(fp);
  }
  // PINE: clip 0 en campo 61 -> interfaz en 90 (29 campos), mando en 133.
  const float entrada_avion = float(std::max(1, g_principal_avion_an[0].cuadros - 1));
  const float c = std::max(0.0f, (campo - entrada_avion) * 1.2f);
  if (!destino && campo >= entrada_avion && c >= DurAma2(g_principal, 0)) {
    if (pressed & kB) return salir(-1);   // adaptacion PC: desde este menu se vuelve al HUB
    const int anterior = cursor;
    if (pressed & kUp) cursor = (cursor + n - 1) % n;
    if (pressed & kDown) cursor = (cursor + 1) % n;
    if (cursor != anterior) { Efecto(0, 2); cursor_anterior = anterior; fila_desde = c; }
    if (pressed & (kA | kStart)) {
      Efecto(0, 3);   // FUN_0023EDC0: aceptar usa 3, no el sonido 5 de Start/demo
      destino = cursor + 1;
      salida = campo;
      REXLOG_INFO("dbz3 nave: menu B2 salida fila {} campo {:.0f}", cursor, campo);
    }
  }
  const float cs = salida >= 0 ? (campo - salida) * 1.2f : -1;
  const float fin_ui = DurAma2(g_principal, 8);
  const float campo_salida = cs >= fin_ui ? campo - salida - std::ceil(fin_ui / 1.2f) : -1;
  const int clip = campo_salida >= 0 ? destino + 1 : campo < entrada_avion ? 0 : 1;
  if (destino && campo_salida >= float(std::max(1, g_principal_avion_an[size_t(clip)].cuadros) - 1))
    return salir(destino);
  Lienzo L = LienzoDe(a);
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32_BLACK);
  dl->PushClipRect(L.P(0, 0), L.P(640, 448), true);
  DibCielo(dl, L, t, true);
  if (g_principal_avion.malla) {
    ps2::Malla pose;
    const auto esq = g_principal_avion.esq;
    const auto& an = g_principal_avion_an[size_t(clip)];
    // Posar recibe segundos de 60 Hz; el reloj del avión es el contador de campos.
    const float ca = campo_salida >= 0 ? campo_salida : clip == 0 ? campo : std::fmod(campo - entrada_avion, float(std::max(1, an.cuadros - 1)));
    if (esq && an) ps2::Posar(*esq, an, std::min(ca, float(std::max(1, an.cuadros) - 1)) / 60.0f, pose);
    ps2::Camara cam;
    cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
    // Misma proyección que el cielo: sesgo GS +70 en ambos ejes, dividido
    // por 0,8 y 16/15 (RAM 0x3A43B8/BC); foco 512, no un encuadre a ojo.
    cam.c = L.P(320 + 70.0f / 0.8f, 224 + 70.0f / (16.0f / 15.0f));
    cam.h = ImVec2(224 * L.s, 224 * L.s);
    cam.fov = 2.0f * std::atan(224.0f / 512.0f); cam.ok = true;
    ps2::Escena escena;
    escena.Empezar();
    const ps2::V3 centro = esq ? esq->centro : ps2::V3{};
    // FUN_0023CE20: segunda pasada negra, expansión 0x3ECCCCCD = 0,4.
    // El contorno usa el camino de caras traseras ya disponible en Escena.
    ps2::Escena::Toon contorno;
    contorno.borde = 0.4f;
    contorno.tonos[0] = contorno.tonos[1] = contorno.tonos[2] = 1.0f;
    escena.Poner(cam, pose ? pose : g_principal_avion.malla, g_principal_avion_tx,
                 {centro.x - 80, -20, centro.z - 145}, -2000.0f * 6.28318531f / 65536.0f, 1, contorno);
    const int v0 = dl->VtxBuffer.Size;
    escena.Pintar(dl);
    for (int v = v0; v < dl->VtxBuffer.Size; ++v)
      dl->VtxBuffer[v].pos.y = cam.c.y + (dl->VtxBuffer[v].pos.y - cam.c.y) * L.k * (448.0f / 480.0f);
  }
  const bool ui = campo >= entrada_avion && (cs < 0 || cs < fin_ui);
  if (ui) {
    // FUN_0023E740: salida 8..11, 14, 17, 20 y 24/25, sin saltar de pantalla.
    for (int k = 0; k < 4; ++k) {
      if (destino) Spr2(L, g_principal, 8 + k, cs, false);
      else Obj2(L, g_principal, k, 4 + k, c);
    }
    if (destino) {
      Spr2(L, g_principal, 14, cs, false);
      Spr2(L, g_principal, 17, cs, false);
      Spr2(L, g_principal, 20, cs, false);
    } else {
      Obj2(L, g_principal, 12, 13, c);
      Obj2(L, g_principal, 15, 16, c);
      Obj2(L, g_principal, 18, 19, c);
    }
  }
  if (ui && g_principal.a.size() > 36) for (int k = 0; k < n; ++k) {
    const int base = k == 0 ? 23 : 27 + k;
    const bool entrando = c < DurAma2(g_principal, 21);
    const float cf = c - fila_desde;
    const int patron = destino ? (k == cursor ? 24 : 25) : entrando ? 21 : c < DurAma2(g_principal, 0) ? 23 :
                       k == cursor ? (cf < DurAma2(g_principal, 27) ? 27 : 22) :
                       k == cursor_anterior && cf < DurAma2(g_principal, 26) ? 26 : 23;
    if (!g_principal.a[size_t(patron)].p.empty() && !g_principal.a[size_t(base)].k.empty()) {
      auto& sp = g_principal.a[size_t(patron)];
      const Anim2 copia = sp;
      const auto& fila = g_principal.a[size_t(base)];
      const float dy = fila.k[0].y - g_principal.a[23].k[0].y;
      sp.x0 = fila.x0; sp.y0 = fila.y0; sp.x1 = fila.x1; sp.y1 = fila.y1;
      sp.p[0].tex = 4 + k;
      sp.p[0].u0 = fila.p[0].u0; sp.p[0].v0 = fila.p[0].v0;
      sp.p[0].u1 = fila.p[0].u1; sp.p[0].v1 = fila.p[0].v1;
      Spr2(L, g_principal, patron, destino ? cs : entrando ? c : cf, patron == 22, ImVec2(0, dy));
      sp = copia;
    }
  }
  // FUN_0023DFE0: la Bola sigue la fila con su oscilacion de 100/65536 vueltas por cuadro.
  if (ui && c >= DurAma2(g_principal, 0) && g_principal.a.size() > 36) {
    const int base = cursor == 0 ? 23 : 27 + cursor;
    if (!g_principal.a[size_t(base)].k.empty()) {
      const float ang = (campo - entrada_avion) * 100.0f * 6.28318531f / 65536.0f;
      const ImVec2 pos(64.0f + (1.0f - std::cos(ang)) * 46.0f,
                      g_principal.a[size_t(base)].k[0].y + std::sin(2.0f * ang) * 5.0f);
      const float cb = c - DurAma2(g_principal, 0);
      Spr2(L, g_principal, destino ? 36 : cb < 51 ? 34 : 35, destino ? cs : cb, !destino && cb >= 51, ImVec2(0, 0), &pos);
    }
  }
  VaciarCapas(dl);
  if (ui && !destino && c >= DurAma2(g_principal, 18) && cursor < int(g_principal_ayuda.size())) {
    const Tex& tx = g_principal_ayuda[size_t(cursor)];
    if (tx.t) dl->AddImage(reinterpret_cast<ImTextureID>(tx.t.get()), L.P(492 - tx.w / 2, 230 - tx.h / 2),
                            L.P(492 + tx.w / 2, 230 + tx.h / 2));
  }
  // FUN_0023F290, máscara 0x108000001: Select / Confirm / Back. La ayuda
  // original se mantiene quieta porque sus 432 px caben en el rectángulo 512.
  if (ui && !destino && c >= DurAma2(g_principal, 0) && g_principal_botones.size() > 32) {
    const int ids[] = {0, 27, 32};
    float ancho = 0;
    for (int id : ids) ancho += g_principal_botones[size_t(id)].w + 32;
    float x = 64 + std::max(0.0f, 512 - (ancho - 32));
    for (int id : ids) {
      const Tex& tx = g_principal_botones[size_t(id)];
      if (tx.t) dl->AddImage(reinterpret_cast<ImTextureID>(tx.t.get()), L.P(x, 392), L.P(x + tx.w, 424));
      x += tx.w + 32;
    }
  }
  dl->PopClipRect();
  return 0;
}

// A2c (tablero B2): el luchador `id_b2` en 3D con su modelo y su pose de B2 (el mismo cargador FigDe de la rueda), con los pies en
// `pie` y `alto_px` de alto en pantalla, girado `giro` rad; `t` = segundos dentro de la animacion. Devuelve false si no hay modelo.
// B3M: banco de animaciones de HISTORIA (DATA_CMN 801..848, un #AMM por personaje con sus huesos <PREFIJO>_*) del luchador: la anim 0 es su
// guardia de pie, la pose de la tarjeta "Battle!" del original (medido con Goku, banco 827). 0 = sin banco (se queda el de la rueda, 931).
int BancoHistoria(const Fig& f) {
  static std::map<std::string, int> cache;
  if (f.prefijo.empty()) return 0;
  if (auto it = cache.find(f.prefijo); it != cache.end()) return it->second;
  int hallado = 0;
  AfsPs2 cmn;
  if (cmn.Abrir(g_iso, "DATA_CMN.AFS"))
    for (uint32_t e = 801; e <= 849 && !hallado; ++e) {
      const std::vector<uint8_t> a = cmn.Entrada(e);
      if (a.size() < 0x80 || a[1] != 'A' || a[2] != 'M' || a[3] != 'M') continue;
      const uint32_t nb = U32(a, 0x18), off = U32(a, 0x1C);
      if (nb < 2 || off + 64 > a.size()) continue;
      const std::string n1(reinterpret_cast<const char*>(&a[off + 32]), strnlen(reinterpret_cast<const char*>(&a[off + 32]), 32));
      const size_t u = n1.find('_');
      if (u != std::string::npos && n1.substr(0, u) == f.prefijo) hallado = int(e);
    }
  cache[f.prefijo] = hallado;
  REXLOG_INFO("dbz3 nave: banco de historia de {}: {}", f.prefijo, hallado);
  return hallado;
}
// `comp` (Dragon Tactics, prueba de figuras): una Escena de fuera donde se anade la figura SIN Empezar ni Pintar (el que la da lo hace), para
// pintar varias figuras en un solo orden de pintor; nullptr = una Escena propia, como siempre.
bool DibujarFiguraB2En(ps2::Escena* comp, ImDrawList* dl, int id_b2, ImVec2 pie, float alto_px, float giro, float t, int traje, int pbank, int panim, const float* cb) {
  if (g_iso.empty()) g_iso = BuscarJuego("budokai 2");   // el tablero lo usa sin haber pasado por la nave
  if (pbank == 0 && !cb && !std::getenv("DBZ3_SEL_TARJ_931")) {   // B3M: la tarjeta usa la guardia (anim 0) del banco de historia del personaje
    const int b = BancoHistoria(FigDe(id_b2, traje));
    if (b > 0) pbank = b, panim = 0;
    else if (id_b2 == 33) pbank = 827, panim = 0;   // Videl: guardia de Goku, retargeteada por sufijo
  }
  const Fig& f = FigDe(id_b2, traje, pbank, panim);
  if (!f.ok || f.alto <= 0 || alto_px <= 1) return false;
  {   // sonda A2c: id, pie y alto en pantalla (una linea cada vez que cambia)
    static int u_id = -99; static float u_x = 0, u_y = 0, u_h = 0;
    if (id_b2 != u_id || std::fabs(pie.x - u_x) > 1 || std::fabs(pie.y - u_y) > 1 || std::fabs(alto_px - u_h) > 1) {
      u_id = id_b2; u_x = pie.x; u_y = pie.y; u_h = alto_px;
      REXLOG_INFO("dbz3 figb2: id {} pie ({:.0f}, {:.0f}) alto {:.0f} px giro {:.2f}", id_b2, pie.x, pie.y, alto_px, giro);
    }
  }
  static ps2::Malla pose;
  const bool anima = bool(f.an);
  float cbl[14];
  // prueba: "t giro dx dy escala" desde un fichero (barrido de poses/encuadre); ARCHIVO2 = el rival (pbank 844), ARCHIVO = el resto
  if (const char* fa = std::getenv(pbank == 844 ? "DBZ3_TEST_FIG_ARCHIVO2" : "DBZ3_TEST_FIG_ARCHIVO")) {
    float v[6] = {-1, 1e9f, 0, 0, 1, 0};
    if (FILE* fp = std::fopen(fa, "r")) {
      if (std::fscanf(fp, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) < 1) v[0] = -1;
      std::fclose(fp);
    }
    if (v[0] >= 0) {
      t = v[0];
      if (v[1] < 1e8f) giro = v[1];
      if (cb) { std::memcpy(cbl, cb, sizeof(cbl)); cb = cbl; cbl[12] += v[2]; cbl[11] += v[3]; cbl[10] *= v[4]; cbl[13] += v[5]; }   // con camara: giro extra, dy mundo, x escala
      else { pie.x += v[2] * alto_px; pie.y += v[3] * alto_px; alto_px *= v[4]; }   // dx, dy en alturas de la figura
    }
  }
  t += f.cuadro / 60.0f + (std::getenv("DBZ3_TEST_FIG_RELOJ") ? float(dbz3::RelojT(ImGui::GetTime())) : 0.0f);   // prueba: reloj de la rueda
  ps2::Anim an_abs;   // prueba DBZ3_TEST_FIG_ABS=1: giros ABSOLUTOS (los de 931 son del esqueleto FRZ_*, no relativos al reposo de Goku)
  const ps2::Anim* an = &f.an;
  if (anima && std::getenv("DBZ3_TEST_FIG_ABS")) { an_abs = f.an; an_abs.relativa = false; an = &an_abs; }
  const std::vector<ps2::Tex>* txp = &f.tx;
  float alto_f = f.alto;
  const std::vector<std::vector<std::pair<float, int>>>* rampa = nullptr;   // la rampa toon de ESTA figura (por instancia)
  const ps2::Malla* mb3 = nullptr;
  const int estado_b3 = anima ? FiguraB3(id_b2, traje, f, t, mb3, txp, alto_f, rampa) : 0;   // B3M: el modelo de B3 con la animacion de B2
  if (estado_b3 == 1) return true;   // generandose (primer uso): nada, no el de B2
  // Nunca el modelo de B2 de un luchador que tiene B3 (como en la rueda): sin modelo B3, su cara grande de B3 HD
  if (const int b3 = B3DeB2(id_b2); estado_b3 == 0 && b3 >= 0 && !std::getenv("DBZ3_SEL_FORZAR_B2")) {
    static std::set<int> avisados;
    if (avisados.insert(id_b2).second) REXLOG_WARN("dbz3 nave: id B2 {} sin modelo B3 en el tablero: cara grande de B3 HD", id_b2);
    if (const ImTextureID tex = CaraDe(b3)) {
      const float tc = alto_px * 0.55f;
      const ImVec2 c(pie.x, pie.y - alto_px * 0.62f);
      dl->AddImage(tex, ImVec2(c.x - tc * 0.5f, c.y - tc * 0.5f), ImVec2(c.x + tc * 0.5f, c.y + tc * 0.5f));
    }
    return true;
  }
  ps2::Malla m_b2;   // el modelo de B2 solo se posa si no se usa el de B3
  if (estado_b3 != 2) {
    if (anima) ps2::Posar(*f.m.esq, *an, t, pose);
    m_b2 = anima ? pose : f.m.malla;
  }
  const ps2::Malla& m = estado_b3 == 2 ? *mb3 : m_b2;
  float ymin = 1e9f, ymax = -1e9f;
  for (const auto& tr : m.tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y), ymax = std::max(ymax, v.y);
  ps2::Malla cabeza_b2;
  // 25/10 (usuario: "en el tablero se siguen usando modelos de B2; la regla es B3 siempre, con las animaciones de B2"): la
  // cabeza de B2 que se ponia encima del Goku de B3 en la vineta ya no; solo con DBZ3_TEST_CARA_B2 (comparar)
  const bool cara_b2_vineta = cb && id_b2 == 0 && estado_b3 == 2 && std::getenv("DBZ3_TEST_CARA_B2");
  if (f.cab.esq && f.hueso >= 0 && anima && (estado_b3 != 2 || cara_b2_vineta)) {
    const auto w = EjesMundo(*f.m.esq, f.an, t);
    const ps2::M34 W = w[size_t(f.hueso)];
    const int base_tex = int(f.tx.size()) - int(f.cab.tex.size());
    for (const auto& tr0 : f.cab.esq->crudo.tris) {
      ps2::Malla::Tri tr = tr0;
      for (int c = 0; c < 3; ++c) {
        tr.p[c] = W.por(tr0.p[c]);
        tr.p[c].x -= f.m.esq->centro.x;
        tr.p[c].z -= f.m.esq->centro.z;
        tr.n[c] = W.gira(tr0.n[c]);
      }
      tr.tex = tr0.tex + base_tex;
      if (id_b2 == 0 && tr0.tex == 2) {   // B2G 24/10: la cara de Goku (textura 2) sin el tinte de color: salia naranja y sin blancos de ojos
        tr.con_color = false; for (auto& cc : tr.col) cc = IM_COL32_WHITE; }
      if (cara_b2_vineta) cabeza_b2.tris.push_back(tr);
      else m_b2.tris.push_back(tr);
    }
  }
  if (cb) {   // A1c: camara de B2 tal cual (ojo, objetivo, fov del #AMC del banco; centro y media pantalla en px; escala S y desplazamiento y)
    // cb = {ojo xyz, objetivo xyz, fov, cx, cy, hy, S, dy, giro_extra, dx}; vista como la de combate.cpp: filas (d, u, -f), delante z < 0
    float fw[3] = {cb[3] - cb[0], cb[4] - cb[1], cb[5] - cb[2]};
    const float lf = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
    for (float& v : fw) v /= lf;
    float r[3] = {fw[1] * 0 - fw[2] * 1, fw[2] * 0 - fw[0] * 0, fw[0] * 1 - fw[1] * 0};   // f x (0,1,0)
    const float lr = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& v : r) v /= lr;
    const float u[3] = {r[1] * fw[2] - r[2] * fw[1], r[2] * fw[0] - r[0] * fw[2], r[0] * fw[1] - r[1] * fw[0]};   // r x f
    ps2::Camara cm;
    cm.m[0] = r[0]; cm.m[4] = r[1]; cm.m[8] = r[2];
    cm.m[1] = u[0]; cm.m[5] = u[1]; cm.m[9] = u[2];
    cm.m[2] = -fw[0]; cm.m[6] = -fw[1]; cm.m[10] = -fw[2];
    cm.m[12] = -(r[0] * cb[0] + r[1] * cb[1] + r[2] * cb[2]);
    cm.m[13] = -(u[0] * cb[0] + u[1] * cb[1] + u[2] * cb[2]);
    cm.m[14] = fw[0] * cb[0] + fw[1] * cb[1] + fw[2] * cb[2];
    cm.m[15] = 1;
    cm.fov = cb[6];
    cm.c = ImVec2(cb[7], cb[8]);
    cm.h = ImVec2(cb[9], cb[9]);
    cm.ok = true;
    ps2::Escena es_propia;
    ps2::Escena& es = comp ? *comp : es_propia;
    if (!comp) es.Empezar();
    ps2::Escena::Toon tn;
    tn.borde = alto_f * BordeFig(estado_b3);
    tn.rampa = estado_b3 == 2 ? rampa : nullptr;
    {   // sonda A1c: caja del modelo en unidades de malla
      static float u_a = -1e9f, u_b = -1e9f;
      if (std::fabs(ymax - u_a) > 1e-3f || std::fabs(ymin - u_b) > 1e-3f) {
        u_a = ymax; u_b = ymin;
        REXLOG_INFO("dbz3 figb2cam: id {} banco {} ymin {:.3f} ymax {:.3f} S {:.3f}", id_b2, pbank, ymin, ymax, cb[10]);
      }
    }
    es.Poner(cm, m, *txp, {cb[13], cb[11], 0}, giro + cb[12], cb[10], tn);
    if (!cabeza_b2.tris.empty())
      es.Poner(cm, cabeza_b2, f.tx, {cb[13], cb[11], 0}, giro + cb[12], cb[10], tn);
    if (!comp) es.Pintar(dl);
    return true;
  }
  const float alto_modelo = ymax - ymin + 1.1f;
  // la camara de la rueda (fov 0,785 sobre 900 px) a ~2,9 alturas del modelo; aqui, con cualquier tamano de ventana, la misma
  // distancia relativa (misma perspectiva) y el campo que da `alto_px`
  ps2::Camara cam;
  const float d = alto_modelo * 2.9f, fl = alto_px * 2.9f;   // la rueda: ~2,9 alturas (mas cerca el plano cercano rompe la malla)
  cam.fov = 2.0f * std::atan(450.0f / fl);
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc_propia;
  ps2::Escena& esc = comp ? *comp : esc_propia;
  if (!comp) esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = alto_f * BordeFig(estado_b3);
  toon.rampa = estado_b3 == 2 ? rampa : nullptr;
  if (toon.rampa) for (float& tono : toon.tonos) tono = 1.0f;
  esc.Poner(cam, m, *txp, {0, -ymin, 0}, giro, 1.0f, toon);
  if (!comp) esc.Pintar(dl);
  return true;
}
// R23 BOCA-03: pose de boca (variante*6 + visema) del luchador id_b2 en la vineta; pose < 0 = deja de hablar (sin boca).
void BocaB2(int id_b2, int pose) {
  if (id_b2 < 0) g_boca_b2.clear();   // fin de la escena: nadie habla
  else if (pose < 0) g_boca_b2.erase(id_b2);
  else g_boca_b2[id_b2] = pose;
}
bool DibujarFiguraB2(ImDrawList* dl, int id_b2, ImVec2 pie, float alto_px, float giro, float t, int traje, int pbank, int panim, const float* cb) {
  return DibujarFiguraB2En(nullptr, dl, id_b2, pie, alto_px, giro, t, traje, pbank, panim, cb);
}
// Alto del modelo de B2 del luchador (unidades de B2; Goku 14,05) para escalar una figura con camara fija como el original; 0 si no hay
float AltoModeloB2(int id_b2) {
  if (g_iso.empty()) g_iso = BuscarJuego("budokai 2");
  const Fig& f = FigDe(id_b2);
  return f.ok ? f.alto : 0.0f;
}
// ---- Dragon Tactics: figuras de B3 HD SOLAS (sin ISO ni registro de B2). El modelo es el sk_<fid>.sk de vista_b2.py y las animaciones
// las hornea vista_tactics_iw.py (andar IW si esta disponible, respaldo B3; tk_<fid>.tk); aqui solo se reproducen.
namespace {
struct Tk {
  uint32_t nb = 0;
  // espera, andar, ataque, tecnica, dano (golpe recibido), derrota (KO), levantarse, victoria
  struct Clip { uint32_t cuadros = 0; bool bucle = false; std::vector<ps2::M34> m; } clip[8];
};
std::map<int, Tk> g_tk;   // nodos estables: un puntero publicado sigue valido; buscar/insertar solo con g_tk_mu
std::mutex g_tk_mu;       // 14/11: los hornos de Survivors (std::async) consultan g_tk a la vez que el dibujo inserta
std::set<int> g_tk_pidiendo, g_tk_fallo;
bool LeerTk(const std::filesystem::path& f, Tk& t) {
  dbz3::survivors::medidas::Tramo tramo{dbz3::survivors::medidas::Clips};
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 4;
  auto u32 = [&](uint32_t& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  uint32_t ncl = 0;
  if (d.size() < 12 || std::memcmp(d.data(), "TKB2", 4) != 0 || !u32(t.nb) || !u32(ncl) || ncl < 4 || ncl > 8 || t.nb == 0 || t.nb > 256) return false;
  for (uint32_t k = 0; k < ncl; ++k) {
    auto& c = t.clip[k];
    uint32_t n = 0, bucle = 0;
    if (!u32(n) || !u32(bucle) || n > 1000 || o + size_t(n) * t.nb * 48 > d.size()) return false;
    c.cuadros = n, c.bucle = bucle != 0;
    c.m.resize(size_t(n) * t.nb);
    if (!c.m.empty()) std::memcpy(c.m.data(), d.data() + o, c.m.size() * 48);
    o += c.m.size() * 48;
  }
  return true;
}
// Mezcla de dos poses locales (3x4 rigidas): traslacion lineal y giro con quaternion (slerp). Las transiciones entre clips
// duran ~0,18 s; sin esto el cambio de clip cortaba a media pose (corte seco).
struct Quat { float x, y, z, w; };
Quat QuatDe(const ps2::M34& a) {
  const float tr = a.m[0] + a.m[5] + a.m[10];
  Quat q;
  if (tr > 0.0f) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    q.w = 0.25f * s, q.x = (a.m[9] - a.m[6]) / s, q.y = (a.m[2] - a.m[8]) / s, q.z = (a.m[4] - a.m[1]) / s;
  } else if (a.m[0] > a.m[5] && a.m[0] > a.m[10]) {
    const float s = std::sqrt(1.0f + a.m[0] - a.m[5] - a.m[10]) * 2.0f;
    q.w = (a.m[9] - a.m[6]) / s, q.x = 0.25f * s, q.y = (a.m[1] + a.m[4]) / s, q.z = (a.m[2] + a.m[8]) / s;
  } else if (a.m[5] > a.m[10]) {
    const float s = std::sqrt(1.0f + a.m[5] - a.m[0] - a.m[10]) * 2.0f;
    q.w = (a.m[2] - a.m[8]) / s, q.x = (a.m[1] + a.m[4]) / s, q.y = 0.25f * s, q.z = (a.m[6] + a.m[9]) / s;
  } else {
    const float s = std::sqrt(1.0f + a.m[10] - a.m[0] - a.m[5]) * 2.0f;
    q.w = (a.m[4] - a.m[1]) / s, q.x = (a.m[2] + a.m[8]) / s, q.y = (a.m[6] + a.m[9]) / s, q.z = 0.25f * s;
  }
  return q;
}
ps2::M34 MezclaM34(const ps2::M34& a, const ps2::M34& b, float t) {
  Quat qa = QuatDe(a), qb = QuatDe(b);
  float d = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
  if (d < 0.0f) qb.x = -qb.x, qb.y = -qb.y, qb.z = -qb.z, qb.w = -qb.w, d = -d;
  float wa = 1.0f - t, wb = t;
  if (d < 0.9995f) {   // slerp; casi iguales -> lineal (evita division por 0)
    const float th = std::acos(std::clamp(d, -1.0f, 1.0f)), s = std::sin(th);
    wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
  }
  const Quat q{qa.x * wa + qb.x * wb, qa.y * wa + qb.y * wb, qa.z * wa + qb.z * wb, qa.w * wa + qb.w * wb};
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  const float x = q.x / n, y = q.y / n, z = q.z / n, w = q.w / n;
  ps2::M34 r;
  r.m[0] = 1 - 2 * (y * y + z * z), r.m[1] = 2 * (x * y - z * w), r.m[2] = 2 * (x * z + y * w);
  r.m[4] = 2 * (x * y + z * w), r.m[5] = 1 - 2 * (x * x + z * z), r.m[6] = 2 * (y * z - x * w);
  r.m[8] = 2 * (x * z - y * w), r.m[9] = 2 * (y * z + x * w), r.m[10] = 1 - 2 * (x * x + y * y);
  r.m[3] = a.m[3] + (b.m[3] - a.m[3]) * t, r.m[7] = a.m[7] + (b.m[7] - a.m[7]) * t, r.m[11] = a.m[11] + (b.m[11] - a.m[11]) * t;
  return r;
}
#include "survivors/modelo_cpu.inl"
// Una fuente por arranque: la misma copia elegida en el launcher se pasa al horno. La firma no depende de que
// su fecha sea mas reciente: tambien detecta instalar IW despues de haber horneado B3, cambiarlo o retirarlo.
std::pair<std::filesystem::path, std::string> FuenteTk() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path fuente = BuscarJuego("infinite world"), archivo;
  if (!fuente.empty() && fs::is_directory(fuente, ec)) {
    for (const auto& dir : {fuente, fuente / "USR", fuente / "usr"}) {
      for (const char* nombre : {"DATA_CMN.AFS", "data_cmn.afs"}) {
        if (fs::is_regular_file(dir / nombre, ec)) { archivo = dir / nombre; break; }
      }
      if (!archivo.empty()) { fuente = dir; break; }
    }
  } else if (!fuente.empty() && fs::is_regular_file(fuente, ec)) {
    archivo = fuente;
  }
  if (archivo.empty()) return {{}, "andar-iw-v1\nB3"};
  const auto real = fs::weakly_canonical(archivo, ec);
  if (!ec) archivo = real;
  std::error_code e1, e2;
  const auto tam = fs::file_size(archivo, e1);
  const auto fecha = fs::last_write_time(archivo, e2);
  return {fuente, "andar-iw-v1\nIW\n" + archivo.string() + "\n" +
      (e1 ? "?" : std::to_string(tam)) + "\n" + (e2 ? "?" : std::to_string(fecha.time_since_epoch().count()))};
}
// hilo de dibujo: 0 = no hay (falta algo de B3 HD), 1 = preparandose, 2 = listo (`sk`, `tk`)
// `fid_pedido` >= 0: el modelo exacto que dice el catalogo de Tactics; la tabla del juego no siempre lo da (Vegeta forma 4 trae el modelo 0 -> 416)
// Survivors: traje alternativo -> fid del traje 0 (mismo esqueleto; sus clips son los del base). Lo rellena FidTrajeB3.
std::map<int, int> g_tk_clips_de;
int TkEstado(int b3, int forma, const SkB3** sk, const Tk** tk, int fid_pedido = -1) {
  const int fid = fid_pedido >= 0 ? fid_pedido : FidB3(b3, 0, forma);
  if (fid < 0) return 0;
  int fc = fid;   // fid de los clips: el propio, o el del traje 0 si `fid` es un traje alternativo
  { std::lock_guard<std::mutex> lk(g_tk_mu); if (auto it = g_tk_clips_de.find(fid); it != g_tk_clips_de.end()) fc = it->second; }
  static std::map<int, std::future<Tk>> lecturas;
  // Publicar tambien los pedidos de una pantalla que ya se cerro; no retener las dos plazas para siempre.
  for (auto it = lecturas.begin(); it != lecturas.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
    const int listo = it->first;
    Tk t = it->second.get();
    it = lecturas.erase(it);
    if (t.nb > 0) {
      std::lock_guard<std::mutex> lk(g_tk_mu);
      g_tk.emplace(listo, std::move(t));
      REXLOG_INFO("dbz3 tactics: clips de B3 fid {} cargados", listo);
    } else {
      std::error_code ec;
      const auto cache = RutaSk(listo).parent_path() / ("tk_" + std::to_string(listo) + ".tk");
      { std::lock_guard<std::mutex> lk(g_modb3_mu); g_tk_fallo.insert(listo); } // una sola copia invalida por ejecucion
      auto respaldo = cache;
      respaldo += ".invalid." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      std::filesystem::rename(cache, respaldo, ec); // conservar la cache invalida antes de regenerarla
      if (ec) {
        std::lock_guard<std::mutex> lk(g_modb3_mu);
        g_tk_fallo.insert(listo); // no repetir indefinidamente una lectura que no se pudo apartar
        REXLOG_ERROR("dbz3 tactics: clips de B3 fid {} invalidos; no se pudo conservar la cache: {}", listo, ec.message());
      } else {
        REXLOG_WARN("dbz3 tactics: clips de B3 fid {} invalidos; cache conservada en {}", listo, respaldo.string());
      }
    }
  }
  int e = 0;
  { dbz3::survivors::medidas::Tramo tramo{dbz3::survivors::medidas::Modelo}; e = SkEstadoCpu(fid, sk, IdFormaB3(b3, forma)); }
  if (e != 2) return e;
  {
    std::lock_guard<std::mutex> lk(g_tk_mu);
    if (auto it = g_tk.find(fc); it != g_tk.end()) {
      *tk = &it->second;
      return it->second.nb == (*sk)->suf.size() ? 2 : 0;
    }
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_tk_fallo.count(fc)) return 0;
    if (g_tk_pidiendo.count(fc)) return 1;
  }
  const std::filesystem::path ruta = RutaSk(fc).parent_path() / ("tk_" + std::to_string(fc) + ".tk");
  const std::filesystem::path sello = ruta.string() + ".iw-source";
  static const auto fuente = FuenteTk();
  std::error_code ec;
  // H14 + R16: el manifiesto y los hornos invalidan por fecha; la fuente por identidad (incluso si IW es mas antiguo).
  if (std::filesystem::exists(ruta, ec)) {
    std::ifstream in(sello, std::ios::binary);
    const std::string anterior((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    bool rehacer = anterior != fuente.second;
    const auto base = dbz3::ModsRoot().parent_path() / "mod center hd";
    const auto tt = std::filesystem::last_write_time(ruta, ec);
    if (!ec) for (const char* dep : {"tactics_manifest.toml", "tactics_andar_iw.py", "cac/vista_tactics_iw.py", "cac/vista_tactics.py"}) {
      std::error_code e1;
      const auto tm = std::filesystem::last_write_time(base / dep, e1);
      if (!e1 && tt < tm) rehacer = true;
    }
    if (rehacer) {
      const auto respaldo = ruta.string() + ".anterior." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      std::filesystem::rename(ruta, respaldo, ec);
      if (ec) {
        REXLOG_ERROR("dbz3 tactics: fid {}: no se pudo conservar la cache anterior: {}", fc, ec.message());
        std::lock_guard<std::mutex> lk(g_modb3_mu);
        g_tk_fallo.insert(fc);
        return 0;
      }
      std::filesystem::rename(sello, respaldo + ".iw-source", ec);
      REXLOG_INFO("dbz3 tactics: clips de B3 fid {}: cache anterior al manifiesto/horno o fuente IW distinta, se rehace", fc);
    }
  }
  if (std::filesystem::exists(ruta, ec)) {
    if (lecturas.find(fc) == lecturas.end()) {
      if (lecturas.size() >= 5) return 1;   // SUR-08: antes 2
      lecturas[fc] = std::async(std::launch::async, [ruta] { Tk t; if (!LeerTk(ruta, t)) return Tk{}; return t; });
    }
    return 1;
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    // Evitar lanzar decenas de procesos de horneado a la vez en una carga fria.
    if (g_tk_pidiendo.size() >= 4) return 1;
    g_tk_pidiendo.insert(fc);
  }
  std::thread([fc, b3, forma, sello] {
    bool ok = CorrerVista("vista_tactics_iw.py", L"--fid " + std::to_wstring(fc) + L" --b3 " + std::to_wstring(b3) + L" --forma " + std::to_wstring(forma) + L" --id " + std::to_wstring(IdFormaB3(b3, forma)) +
        (fuente.first.empty() ? L"" : L" --iw \"" + fuente.first.wstring() + L"\""));
    if (ok) {
      std::ofstream out(sello, std::ios::binary);
      out << fuente.second;
      out.close();
      ok = bool(out);
    }
    REXLOG_INFO("dbz3 tactics: clips de B3 fid {} {} (andar {})", fc, ok ? "generados" : "no se pudieron generar", fuente.first.empty() ? "B3 de respaldo" : "IW");
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_tk_pidiendo.erase(fc);
    if (!ok) g_tk_fallo.insert(fc);
  }).detach();
  return 1;
}
}  // namespace
#include "iw_nave_hud_b2.inl"
// HMS3 en la cache PAL, independiente de la cache y el roster de Tactics.
bool LeerFigura1910(const std::filesystem::path& ruta, ps2::Modelo& modelo,
                    std::vector<std::vector<std::pair<float,int>>>& rampa, std::vector<std::string>& nombres) {
  std::ifstream f(ruta,std::ios::binary);
  char magic[4];
  if (!f.read(magic,4) || std::memcmp(magic,"HMS3",4)) return false;
  auto sk = LeerSkCpu(ruta);
  if (!sk.malla || !std::isfinite(sk.alto) || sk.esq.padre.empty()) return false;
  for (const auto& m : sk.esq.local)
    for (float x : m.m) if (!std::isfinite(x)) return false;
  modelo.esq = std::make_shared<ps2::Esqueleto>(std::move(sk.esq));
  modelo.malla = std::move(sk.malla);
  modelo.tex = std::move(sk.imgs);
  rampa = std::move(sk.rampa);
  nombres = std::move(sk.suf);
  return true;
}

int EstadoFiguraB3(int b3, int forma, int fid) {
  const SkB3* sk = nullptr;
  const Tk* tk = nullptr;
  return TkEstado(b3, forma, &sk, &tk, fid);
}
// Survivors: fid del traje `traje` del luchador (sus colores alternativos oficiales); -1 o el del traje 0 si no lo tiene
int FidTrajeB3(int b3, int traje, int forma) {
  const int f = FidB3(b3, traje, forma), base = FidB3(b3, 0, forma);
  if (traje > 0 && f >= 0 && base >= 0 && f != base) { std::lock_guard<std::mutex> lk(g_tk_mu); g_tk_clips_de[f] = base; }
  return f;
}
// Dragon Tactics: la altura de cada modelo respecto a la de un luchador normal (Goku, sk->alto ~19,6), medida del propio modelo y acotada para que
// ni Krilin ni Freezer salgan ilegibles. 0 = el modelo aun no esta listo.
float AltoRelFiguraB3(int b3, int forma, int fid) {
  const SkB3* sk = nullptr;
  const Tk* tk = nullptr;
  if (TkEstado(b3, forma, &sk, &tk, fid) != 2) return 0.0f;
  return std::clamp(sk->alto / 19.6f, 0.62f, 1.3f);
}
// La figura de pie en `suelo` (mundo) con `alto` de alto, girada `giro` (0 = de cara a +z), vista con la camara `cam` del tablero y puesta en la
// `esc` comun (todas las figuras se ordenan juntas); clip 0 espera, 1 andar, 2 ataque, 3 tecnica, 4 dano, 5 derrota, 6 levantarse, 7 victoria;
// `t` = segundos dentro del clip (60 cuadros por segundo; los que se repiten dan la vuelta, el resto se queda en el ultimo cuadro).
// `ciclo` >= 0 (andar): el clip recorre una vuelta entera con el avance entre casillas (los pies no se deslizan).
// `clip_prev` + `t_prev` + `mezcla` (0..1): mezcla con la pose del clip anterior (transiciones suaves). false si no esta lista.
bool DibujarFiguraB3En(ps2::Escena* esc, const ps2::Camara& cam, int b3, int forma, ps2::V3 suelo, float alto, float giro, int clip, float t, int fid, float dura,
                       float ciclo, int clip_prev, float t_prev, float mezcla) {
  const SkB3* sk = nullptr;
  const Tk* tk = nullptr;
  if (!esc || alto <= 0 || TkEstado(b3, forma, &sk, &tk, fid) != 2) return false;
  const Tk::Clip* c = &tk->clip[std::clamp(clip, 0, 7)];
  if (c->cuadros == 0) c = &tk->clip[0];
  if (c->cuadros == 0) return false;
  const auto marco = [&](const Tk::Clip* cl, float tt, float dur) -> const ps2::M34* {
    if (!cl || cl->cuadros == 0) return nullptr;
    if (!cl->bucle && dur > 0.0f && float(cl->cuadros) > dur * 60.0f) tt *= float(cl->cuadros) / (dur * 60.0f);   // el clip acaba con la accion
    const uint32_t f = cl->bucle ? uint32_t(std::max(tt, 0.0f) * 60.0f) % cl->cuadros : std::min(uint32_t(std::max(tt, 0.0f) * 60.0f), cl->cuadros - 1);
    return cl->m.data() + size_t(f) * tk->nb;
  };
  float t_uso = t;
  if (ciclo >= 0.0f && c->cuadros > 0) t_uso = std::max(ciclo, 0.0f) * float(c->cuadros) / 60.0f;   // andar: las vueltas del clip van con el avance entre casillas
  const ps2::M34* loc = marco(c, t_uso, dura);
  const ps2::M34* loc_prev = nullptr;
  if (clip_prev >= 0 && mezcla > 0.0f && mezcla < 1.0f && clip_prev != clip) {
    const Tk::Clip* cp = &tk->clip[std::clamp(clip_prev, 0, 7)];
    if (cp->cuadros) loc_prev = marco(cp, t_prev, 0.0f);
  }
  static std::vector<ps2::M34> w;   // hilo de dibujo: sin reservar memoria en cada figura
  w.resize(tk->nb);
  for (size_t i = 0; i < w.size(); ++i) {
    const ps2::M34 L = loc_prev ? MezclaM34(loc_prev[i], loc[i], mezcla) : loc[i];
    w[i] = sk->esq.padre[i] >= 0 ? w[size_t(sk->esq.padre[i])] * L : L;
  }
  if (sk->posada.tris.size() != sk->malla.tris.size()) sk->posada.tris = sk->malla.tris;
  float ymin = 1e9f;
  for (size_t i = 0; i < sk->malla.tris.size(); ++i) {
    const ps2::Malla::Tri& src = sk->malla.tris[i];
    ps2::Malla::Tri& tr = sk->posada.tris[i];
    for (int k = 0; k < 3; ++k) {
      const ps2::M34& W = w[src.hueso[k]];
      tr.p[k] = W.por(src.p[k]);
      tr.n[k] = W.gira(src.n[k]);
      ymin = std::min(ymin, tr.p[k].y);
    }
  }
  sk->posada_de = nullptr;   // la cache de la nave (por animacion de B2) ya no vale
  const float e = alto * std::clamp(sk->alto / 19.6f, 0.62f, 1.3f) / std::max(sk->alto, 1e-3f);   // escala fija por modelo (no la altura de cada pose); `alto` = la de un luchador normal, cada modelo guarda su proporcion real
  ps2::Escena::Toon toon;
  toon.borde = sk->alto * BordeFig(2);
  toon.rampa = sk->rampa.empty() ? nullptr : &sk->rampa;
  toon.agrupar = true;
  esc->Poner(cam, sk->posada, sk->tx, {suelo.x, suelo.y - ymin * e, suelo.z}, giro, e, toon);
  return true;
}
// POST/B2-P13-HD (dbz3_b2_shenron_hd): Shenron de B3 HD (POST3: el de la pantalla de deseos, data_cmn #AWO 2209 de 112 huesos
// SNR_* + #AZT 2210, posado con el cuadro 0 de la anim 0 del #ACM 2208; el 534 de antes es OTRO modelo, dorado; sk_2209.sk de
// vista_b2.py) con la animacion de B2 de la escena del deseo (`an`, pistas en el orden de los huesos de `b2`), pasada
// hueso a hueso por el sufijo como las figuras del tablero (AnimSk/PosarSk). `amo` = el #AMO de B2 (CMN 327 hijo 0) para
// los nombres. Escala: la altura de reposo del de B2. false mientras se genera o si falta algo (se queda el de B2).
bool PonerShenronB3(ps2::Escena* esc, const ps2::Camara& cam, const ps2::Modelo& b2, const std::vector<uint8_t>& amo, const ps2::Anim& an,
                    float t, ps2::V3 pos) {
  constexpr int kFidShenron = 2209;   // POST3: el de la pantalla de deseos de B3 HD (#AWO 2209 + #AZT 2210 + #ACM 2208; vista_b2.py SUELTOS). El 534 es otro, dorado
  const SkB3* sk = nullptr;
  (void)amo;
  if (!esc || !b2.esq || !an || SkEstado(kFidShenron, &sk) != 2) return false;
  const ps2::Esqueleto& e2 = *b2.esq;
  const size_t n2 = e2.padre.size(), n3 = sk->esq.padre.size();
  if (e2.inv_mundo.size() != n2 || e2.crudo.tris.empty()) return false;
  // El sk viene ya enroscado (vista_b2.py POSE_FIJA) y mide como el de B2 (volcadas las dos mallas: la misma figura). Se lleva
  // a la caja de la malla de B2 (escala por el alto) y cada hueso sigue rigido al de B2 cuyo tramo de reposo le queda mas cerca.
  // ponytail: el enroscado es fijo (sin la anim de B3 corriendo) (vista_b2.py lo exporta posado).
  struct VB { ps2::V3 p, n; int h; };
  static std::vector<VB> vb;
  static float s = 1;
  if (vb.size() != sk->malla.tris.size() * 3) {
    std::vector<ps2::M34> w3(n3);
    for (size_t j = 0; j < n3; ++j) w3[j] = sk->esq.padre[j] >= 0 ? w3[size_t(sk->esq.padre[j])] * sk->esq.local[j] : sk->esq.local[j];
    std::vector<ps2::V3> j2(n2);
    for (size_t i = 0; i < n2; ++i) { const ps2::M34 W = e2.inv_mundo[i].Inversa(); j2[i] = {W.m[3], W.m[7], W.m[11]}; }
    float a2[6] = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f}, a3[6] = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f};
    auto caja = [](float* a, const ps2::V3& v) {
      a[0] = std::min(a[0], v.x), a[1] = std::min(a[1], v.y), a[2] = std::min(a[2], v.z);
      a[3] = std::max(a[3], v.x), a[4] = std::max(a[4], v.y), a[5] = std::max(a[5], v.z);
    };
    for (const auto& tr : e2.crudo.tris) for (const auto& v : tr.p) caja(a2, v);
    for (const auto& tr : sk->malla.tris) for (int k = 0; k < 3; ++k) caja(a3, w3[tr.hueso[k]].por(tr.p[k]));
    s = (a3[4] > a3[1]) ? (a2[4] - a2[1]) / (a3[4] - a3[1]) : 1.0f;
    const ps2::V3 off{(a2[0] + a2[3]) * 0.5f - s * (a3[0] + a3[3]) * 0.5f, a2[1] - s * a3[1], (a2[2] + a2[5]) * 0.5f - s * (a3[2] + a3[5]) * 0.5f};
    std::vector<int> de(n3, 0);
    std::string tabla;
    for (size_t j = 0; j < n3; ++j) {
      const ps2::V3 q{w3[j].m[3] * s + off.x, w3[j].m[7] * s + off.y, w3[j].m[11] * s + off.z};
      float mejor = 1e30f;
      for (size_t i = 0; i < n2; ++i) {
        ps2::V3 a = j2[i], b = a;
        for (size_t k = 0; k < n2; ++k) if (e2.padre[k] == int(i)) { b = j2[k]; break; }
        const ps2::V3 ab{b.x - a.x, b.y - a.y, b.z - a.z}, aq{q.x - a.x, q.y - a.y, q.z - a.z};
        const float l2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
        const float u = l2 > 1e-6f ? std::clamp((aq.x * ab.x + aq.y * ab.y + aq.z * ab.z) / l2, 0.0f, 1.0f) : 0.0f;
        const float dx = aq.x - u * ab.x, dy = aq.y - u * ab.y, dz = aq.z - u * ab.z, d = dx * dx + dy * dy + dz * dz;
        if (d < mejor) mejor = d, de[j] = int(i);
      }
      // POST2: el cuerpo de B2 va pesado al hueso 0 (quieto); sus huesos 1-7 son solo cuello y cabeza y se mueven hacia
      // la camara. Con todo el cuerpo HD en 1-3 (lo mas cercano) el enroscado entero se acercaba a la camara (medido:
      // caja posada igual de alta que la de B2 pero delante). Solo la cabeza HD sigue a la cabeza de B2.
      const std::string suf = j < sk->suf.size() ? sk->suf[j] : std::string();
      if (suf != "NECK" && suf != "JAW" && suf.find("MUSTACHE") == std::string::npos) de[j] = 0;   // SNR_NECK = la cabeza (padre de mandibula y bigotes)
      tabla += fmt::format("{}->{}({:.1f}) ", j < sk->suf.size() ? sk->suf[j] : std::string("?"), de[j], std::sqrt(mejor));
    }
    REXLOG_INFO("dbz3 b2 shenron hd: enroscado, escala {:.3f}, caja B2 x {:.1f}..{:.1f} y {:.1f}..{:.1f} z {:.1f}..{:.1f}, tabla {}", s, a2[0], a2[3],
                a2[1], a2[4], a2[2], a2[5], tabla);
    vb.resize(sk->malla.tris.size() * 3);
    for (size_t t = 0; t < sk->malla.tris.size(); ++t)
      for (int c = 0; c < 3; ++c) {
        const auto& src = sk->malla.tris[t];
        const ps2::V3 r = w3[src.hueso[c]].por(src.p[c]);
        vb[t * 3 + size_t(c)] = {{r.x * s + off.x, r.y * s + off.y, r.z * s + off.z}, w3[src.hueso[c]].gira(src.n[c]), de[src.hueso[c]]};
      }
  }
  const std::vector<ps2::M34> loc = ps2::Locales(e2, an, t);
  std::vector<ps2::M34> w2(n2), piel(n2);
  for (size_t i = 0; i < n2; ++i) {
    w2[i] = e2.padre[i] >= 0 ? w2[size_t(e2.padre[i])] * loc[i] : loc[i];
    piel[i] = w2[i] * e2.inv_mundo[i];
  }
  static ps2::Malla m;
  if (m.tris.size() != sk->malla.tris.size()) m.tris = sk->malla.tris;
  for (size_t i = 0; i < m.tris.size(); ++i)
    for (int k = 0; k < 3; ++k) {
      const VB& v = vb[i * 3 + size_t(k)];
      // POST3: la malla cruda de B2 lleva su centro x/z y la posada no (Posar se lo quita y el que llama lo devuelve en
      // `pos`): sin restarlo el HD iba dos veces desplazado (medido volcando las dos mallas: 4,3 hacia la camara)
      m.tris[i].p[k] = piel[size_t(v.h)].por(v.p);
      m.tris[i].p[k].x -= e2.centro.x, m.tris[i].p[k].z -= e2.centro.z;
      m.tris[i].n[k] = piel[size_t(v.h)].gira(v.n);
    }
  ps2::Escena::Toon toon;
  toon.borde = sk->alto * s * BordeFig(2) * 0.5f;   // POST3: a la mitad; con el de las figuras los bigotes salian el doble de gruesos que en la pantalla 11 nativa
  toon.rampa = sk->rampa.empty() ? nullptr : &sk->rampa;
  toon.agrupar = true;
  toon.recortar = toon.ligero = true;
  esc->Poner(cam, m, sk->tx, pos, 0.0f, 1.0f, toon);
  return true;
}
#include "survivors/horno.inl"   // Dragon Budokai Survivors: impostores horneados de una figura de B3 (HornearFiguraB3)

// S1-F01.2: el ELF de B2 del jugador para el tablero (el iris de la Bola de Dragon lee de el sus tablas de estrellas).
const std::vector<uint8_t>& ElfB2() {
  static const std::vector<uint8_t> e = LeerElf(BuscarJuego("budokai 2"));
  return e;
}
bool Disponible() { return b2::Disponible(); }
// Entrada por mando de este cuadro y conexion del mando 2 (pasarela_misiones.cpp la llama antes de Dibujar, como
// b2::Mantenidos). Es la entrada que leen las pantallas 1P/2P del original: 0x264F60/0x260630 miran los cuatro
// registros de mando (0x3B43B2/0x4412/0x446A/0x44CA) y solo dejan 2P con los dos mandos conectados (0x3B43A0/0x3B4458 == 3).
void Mantenidos(uint16_t m) { g_mant = m; }
void Mandos(uint16_t p1, uint16_t p2, bool dos_mandos) {
  g_ent1 = p1;
  g_ent2 = p2;
  g_dos_mandos = dos_mandos;
}
bool DosJugadores() { return g_2p.load(); }
// Se abre con el deseo "Portal de la nave de Babidi" de Shenron en el Dragon World (pasarela_b2.cpp, como en B2).
bool Abierta() { return LeerProgreso("tablero_b2_nave", 0) != 0; }

void Entrar() {
  combate::OndasModo(false);
  g_kili = LeerProgreso("nave_kili", 0);
  g_enc_armado = false;
  Premios();   // los graficos los carga Dibujar en el hilo que dibuja (crear texturas desde otro hilo rompia: R7)
// EU 0x265E90 FUN_00265E90
// EU 0x265560 FUN_00265560
// EU 0x2654A0 FUN_002654A0
  g_estado = kModo;   // la primera vez, la pantalla de 1P/2P (0x2654A0 con 0x8C44 = 0)
  g_2p = false;       // 0x264F60: sin los dos mandos el original fuerza 1P (y al abrir se empieza en 1P)
  g_luch = g_riv = g_sitio = g_sitio_vis = g_menu_sel = g_pag = 0;
  g_elegida[0] = g_recordada[0];
  g_elegida[1] = g_recordada[1];
  g_j3 = g_elegida[0].id;
  g_r3 = g_elegida[1].id;
  g_lista_capsulas = g_elegida[0].lista;
  g_trajes_pendientes = 0;
  // La planta en curso: B2 la guarda con el resto de su estado (cRam003a492c, cursor de 0x264790) y al volver a la
  // nave sigue donde se quedo. Aqui se persiste en progreso.toml para no perderla al salir (N3, DS151).
  g_prueba = std::clamp(LeerProgreso("nave_planta", 0), 0, 3);
  ConstruirPaginas();   // se restaura por ID: el roster puede cambiar con los mods
  REXLOG_INFO("dbz3 nave: entrar ({} Kili)", g_kili);
}

// --- Cadena de la prueba I (B2: el rival cae y se levanta, sin pantalla VS). La pide pasarela_misiones.cpp en el hilo
// del juego (gancho sub_820FE9B0, antes de que el conector cierre el combate) y la gasta CombateNave sin volver a
// pedir nada: el hub::ChainBattleEx pedido arranca el combate siguiente. Sin el armado, todo sigue igual que antes
// (pantalla VS). Ver studies 3/ENTREGAS/08_nave_babidi_1a1. ---
bool Siguiente(int out[7], int vida) {   // no toca el estado: la cadena puede fallar
  if (g_2p.load() || g_prueba != 0 || g_enc_armado.load()) return false;   // 2P: es un versus, no la prueba I encadenada
  const int j = g_j3, r = g_r3;
  out[0] = j;
  out[1] = r;
  out[2] = EscenarioDelSitio(g_sitio);
  out[3] = std::max(1, vida);   // el jugador sigue con la vida que le quedo (B2: no se recupera)
  out[4] = kPruebas[0].tiempo;
  out[5] = 0;
  out[6] = 0;
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
void Levantado() {   // tagteam.cpp: el rival de la prueba I se levanto en el mismo combate (contador x0 -> x1)
  ++g_vencidos;
  REXLOG_INFO("dbz3 nave: prueba I: el rival se levanta en el mismo combate (x{})", g_vencidos.load());
}

// 0 nada, 1 salir, 2 combate pedido en out = {jugador, rival, mapa, vida inicial, tiempo, prueba, P2 humano}.
int Dibujar(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, int out[7]) {
  Cargar();
  const char* reloj_prueba = std::getenv("DBZ3_TEST_NAVE_CUADRO");
  if (reloj_prueba) {   // arnes: reloj explicito de 60 Hz; ausente no hay E/S ni cambio de reloj
    if (FILE* f = std::fopen(reloj_prueba, "r")) {
      float cuadro = 0;
      if (std::fscanf(f, "%f", &cuadro) == 1 && std::isfinite(cuadro) && cuadro >= 0) t = cuadro / 60.0f;
      std::fclose(f);
    }
  }
  const uint16_t e1 = g_ent1.load(), e2 = g_ent2.load();
  const bool dos = g_dos_mandos.load();
  static int antes = -1;
  static float desde = 0, t_antes = 0, d_bab = 0, d_burb = 0, d_tit = 0;
  const float dt = std::clamp(t - t_antes, 0.0f, 0.1f);
  t_antes = t;
  if (g_estado != antes) {   // pantalla nueva: las piezas entran con sus claves; Babidi habla
    auto sel = [](int e) { return e == kJugador || e == kRival || e == kSitio || e == kMenu; };
    auto cab = [&](int e) { return e == kSitio ? 2 : sel(e) ? 1 : 0; };
    if (antes < 0 || antes == kResultado || antes == kSigue || g_estado == kResultado) d_bab = d_burb = d_tit = t;
    else {
      if (sel(antes) != sel(g_estado)) d_burb = t;   // bocadillo de abajo <-> de arriba
      if (cab(antes) != cab(g_estado)) d_tit = t;    // titulo / "Selecc. Person." / "Seleccion Fase"
    }
    const int prev = antes;
    if (prev < 0) g_sel = SelTransicion();
    antes = g_estado;
    desde = t;
    // EU 0x2644E0 FUN_002644e0 (de la pantalla de pruebas a la rueda: ruedas del jugador y del COM)
    if (g_estado == kJugador && prev != kMenu) {   // la rueda de 1P entra (60 cuadros); desde el menu ya esta puesta
      ConstruirPaginas();                          // el roster cambia con los mods: se lee al entrar
      g_elegida[0] = g_recordada[0];
      g_elegida[1] = g_recordada[1];
      RestaurarRueda(0);
      g_t_rueda = t;
      g_rueda_com = false;
    }
    if (g_estado == kRival) {                       // la del COM (misma pantalla, rotulo COM); en 2P la del mando 2
      RestaurarRueda(1);
      g_t_rueda = g_rueda_continua ? t - 1.0f : t;
      g_rueda_continua = false;
      g_rueda_com = !g_2p.load();                   // 2P: la camara de la rueda es la del jugador (1P)
    }
    if (g_estado == kSitio) {                       // la burbuja entra (45 cuadros)
      ConstruirCasillas();
      g_sitio_vis = 0;
      g_giro_burb.Fijar(0);
      g_t_esfera = t;
      g_burb_imagen_t = t + 45.0f / 60.0f;
    }
    if (g_estado == kJugador && prev == kMenu) RestaurarRueda(0);
    if (g_estado == kMenu) {
      g_t_menu = t;
      if (prev == kRival) RestaurarRueda(0);
    }
    if (g_estado != prev)
      REXLOG_INFO("dbz3 nave: pantalla {}{}", g_estado == kModo ? "modo" : g_estado == kPrueba ? "planta" : g_estado == kJugador ? "rueda 1P"
                  : g_estado == kMenu ? "menu" : g_estado == kRival ? (g_2p.load() ? "rueda 2P" : "rueda COM") : g_estado == kSitio ? "burbuja" : g_estado == kResultado ? "resultado" : "otra",
                  g_estado == kPrueba ? " " + std::to_string(g_prueba + 1) : std::string());
    g_voz_modo = false;
    if (g_estado == kModo) {
      g_voz_modo = true;   // 0x261880 FUN_00261880: suena al pasar el objeto 0x699D74 (burbuja, anim 18) al bucle
      Efecto(0, 0x15);   // 0x2629B0 (b2.c:210713) al abrir la pantalla 1P/2P
    }
    if (g_estado == kJugador) {
      // FUN_002617C0 (b2.c:210156): una llamada al LFSR del tablero, (v & 1) + 9.
      g_frase_sel = 9 + int(b2::B2Rng(0x20) & 1u);
      if (const char* fr = std::getenv("DBZ3_TEST_NAVE_FRASE")) g_frase_sel = std::clamp(std::atoi(fr), 9, 10);   // pruebas: el dado de Babidi fijo para comparar con el original
      Voz(1150 + g_frase_sel - 9);
    }
    if (g_estado == kSitio) {
      // FUN_00261700 (b2.c:210133): una llamada al LFSR del tablero, v % 3 + 11.
      g_frase_sitio = 11 + int(b2::B2Rng(0x20) % 3u);
      Voz(1152 + g_frase_sitio - 11);
    }
  }
  g_cuadro = (t - desde) * 60.0f;
  if (static const char* nq = std::getenv("DBZ3_TEST_NAVE_QUADS"); nq) {   // RESCATE B2-NAVE (pruebas): ver VolcarQuadsNave
    static uint32_t hechos = 0;
    static int grab = -1;
    if (grab >= 0) {   // lo grabado en la pasada anterior (un cuadro entero)
      VolcarQuadsNave(nq, grab);
      hechos |= 1u << grab;
      grab = -1;
      g_nq_grabando = false;
    }
    if (g_estado >= 0 && g_estado < 32 && !(hechos >> g_estado & 1u) && g_cuadro >= 240.0f) {
      grab = g_estado;
      g_nq.clear();
      g_nq_grabando = true;
    }
  }
  g_c_bab = (t - d_bab) * 60.0f;
  g_c_burb = (t - d_burb) * 60.0f;
  g_c_tit = (t - d_tit) * 60.0f;
  const Lienzo L = LienzoDe(a);
  auto salir = [&](int r) {
    Musica(false);
    CallarAdxPs2();
    antes = -1;
    if (r == 1) g_trajes_pendientes = 0;
    return r;
  };
  if (g_estado == kSigue) {   // prueba I: el mismo rival se levanta (vidas sin fin) y sigue con la vida que quedo
    Pedir(out);
    g_estado = kPrueba;
    return salir(2);
  }
  if (g_sel.fase != SelFase::quieta) {
    g_sel.c = std::max(0.0f, (t - g_sel.t0) * 60.0f);
    const float dur = g_sel.fase == SelFase::burbujaConfirma ? DurAma2(g_burb, 19) : 60.0f;
    if (g_sel.fase == SelFase::menuSale && g_sel.c + 0.001f >= DurAma2(g_rueda, 12)) {
      g_sel = SelTransicion();
      g_estado = kJugador;
      return 0;
    }
    if (g_sel.fase == SelFase::menuConfirma && g_sel.c + 0.001f >= dur) {
      g_sel = SelTransicion();
      g_rueda_continua = true;
      g_estado = kRival;
      return 0;
    }
    if ((g_sel.fase == SelFase::ruedaConfirma || g_sel.fase == SelFase::burbujaConfirma) && g_sel.c + 0.001f >= dur) {
      const bool burbuja = g_sel.fase == SelFase::burbujaConfirma;
      SalidaSeleccion(burbuja, burbuja ? kPrueba : kSitio, burbuja, g_sel.t0 + dur / 60.0f);
      g_sel.c = std::max(0.0f, (t - g_sel.t0) * 60.0f);
    }
    if ((SaliendoRueda() || SaliendoBurbuja()) && g_sel.c + 0.001f >= 30.0f) {
      const bool combate = g_sel.combate;
      const int destino = g_sel.destino;
      REXLOG_INFO("dbz3 nave: transicion salida acabada a {:.3f} cuadros", g_sel.c);
      g_sel = SelTransicion();
      g_estado = destino;
      if (combate) { Pedir(out); return salir(2); }
      return 0;
    }
  }
  g_fig_flash = (g_sel.fase == SelFase::menuConfirma || g_sel.fase == SelFase::ruedaConfirma)
                    ? std::clamp(1.0f - g_sel.c / 60.0f, 0.0f, 1.0f) : 0;
  g_fig_salida_x = SaliendoRueda() ? -14.0f * g_sel.c : 0;
  if (reloj_prueba) {
    static float ult = -1;
    static int estado_ult = -1, fase_ult = -1, res_ult = -1;
    if (ult != t || estado_ult != int(g_estado) || fase_ult != int(g_sel.fase) || res_ult != g_res.fase) {
      ult = t;
      estado_ult = int(g_estado); fase_ult = int(g_sel.fase);
      res_ult = g_res.fase;
      REXLOG_INFO("dbz3 nave: solP cuadro {:.3f} estado {} fase {} c {:.3f} res {} premio_c {:.3f} salida {:.3f}",
                  t * 60.0f, int(g_estado), int(g_sel.fase), g_sel.c, g_res.fase,
                  g_estado == kResultado && g_res.fase == 3 ? g_cuadro - g_res.premio_t : -1.0f, g_ficha_sale_c);
    }
  }
  Musica(true);
  Fondo(dl, L, g_estado == kJugador || g_estado == kRival || g_estado == kSitio || g_estado == kMenu, t);
  if (auto_on && t - g_auto_t > 2.5f) {
    g_auto_t = t;
    pressed |= kA;
  }
  // Nave 2P: cada mando lleva su pantalla (la rueda del 1P, su menu y el escenario con el mando 1; la del 2P con el
  // mando 2). En 1P o con el 2P apagado, `pressed` es el de siempre (solo el mando 1).
  if (g_2p.load()) {
    if (g_estado == kRival) pressed = e2;
    else if (g_estado == kJugador || g_estado == kMenu || g_estado == kSitio) pressed = e1;
    else pressed |= e2;   // pruebas y resultado: cualquiera de los dos (0x264790 en 2P lee los cuatro registros)
  }
  bool ok = (pressed & (kA | kStart)) != 0;
  if (g_estado == kResultado) {
    if (g_res.fase == 3) {   // el cartel pasa a la ficha con A; en la ficha, X / cruceta da la vuelta y A / Y vuelve
      // S1-F01.4: en la ficha, X / izquierda / derecha le dan la vuelta (cuadrado / 0x80 / 0x20 del original) y A / Y
      // la cierran con su animacion; la ventana se da por cerrada cuando acaba (PremioCerrado).
      const float c = g_cuadro - g_res.premio_t;
      if (c < g_cartel) {
        // FUN_002A3E90 espera los objetos: A no adelanta el cartel.
        ok = false;
        FichaReset();
      } else {
        const bool lista = c - g_cartel >= 20.0f;
        if (lista && (pressed & (kX | kLeft | kRight))) FichaGirar(c - g_cartel);
        else if (lista && (pressed & (kA | kStart | kY))) CerrarFicha(c);
        ok = PremioCerrado(c);
      }
    }
    // EU 0x263700 FUN_00263700
    const bool fin = PasoResultado(dt * 60.0f, ok);
    DibujarResultado(dl, L);
    // EU 0x262AF0 FUN_00262af0 (vuelta del combate: de la pantalla de resultado a la de pruebas, 0x264DE0)
    if (fin) {
      // B2-10: espera opcional (apagada) para igualar el reparto del original sin tocar el cierre de B3.
      g_res_espera += dt;
      if (g_res_extra > 0 && g_res_espera < g_res_extra) return 0;
      g_estado = kPrueba;
      g_res_espera = 0;
      if (auto_on) return salir(1);   // prueba automatica: un combate y fuera
    }
    return 0;
  }
  // EU 0x261880 FUN_00261880
  if (g_voz_modo && g_estado == kModo && g_c_burb >= DurDe(18)) {
    g_voz_modo = false;
    Voz(1149);
  }
// EU 0x265410 FUN_00265410
// EU 0x2629B0 FUN_002629B0
  if (g_estado == kModo) {   // 0x2629B0: titulo, Babidi con la frase 0, el recuadro (24 -> 25) y 1P (29) / 2P (35)
    static bool salm = false;
    static float salm_c0 = 0;
    static bool salb = false;   // salida con B: cierre + fundido (N4, DS151)
    static float salb_c0 = 0;
    static bool salb_dos = false;   // lado elegido cuando se pulso B (0 = 1P, 1 = 2P)
    if (!dos) g_2p = false;   // EU 0x264F60: sin los dos mandos conectados (0x3B43A0/0x3B4458 == 3) fuerza 1P
    if (dos) {                // esta pantalla lee los dos mandos (0x260630/0x264F60); en 1P no cambia nada
      pressed |= e2;
      ok = ok || (e2 & (kA | kStart)) != 0;
    }
    // N4 (DS151). Salida con B desde 1P/2P. El original NO sale en ese cuadro: FUN_00264F60 (rama 0x1000 =
    // triangulo/B) monta el cierre de los objetos y cede a FUN_00264EA0, que espera a que los OCHO objetos vuelvan
    // al estado 0 (el mayor es 0x11/0x26 = 21 cuadros). Solo entonces FUN_00249F60(0,0x245c40) funde a negro de
    // golpe (fGpffff8a6c = 0) y FUN_00232E00 pide el cambio de escena (uGpffff8870 = 1).
    // EU 0x264F60 FUN_00264f60 (rama 0x1000; b2.c:211884-211905)
    if (salb) {
      const float cs = g_cuadro - salb_c0;
      // EU 0x264EA0 FUN_00264ea0 (b2.c:211833): espera a los 8 objetos; el mayor 0x11/0x26 = 21 cuadros
      const float fin = std::max({DurTarea(0x11), DurTarea(0x26), DurTarea(0x1A), DurTarea(0x14)});
      if (cs < fin) {
        // Clips que monta la rama: 0x699D78 0x26 (Babidi, 21), 0x699D74 0x14 (bocadillo, 16),
        // 0x699D7C 0x1A (recuadro, 16), 0x699D70 0x11 (titulo, 21), 0x8C30/0x8C34 0x1F (11) y el circulo
        // elegido 0x1E (11). El marco (0x699D58/5C/60 -> 8/0xE/0x48, 21/21/16) lo pinta Fondo() en el port.
        DibC(dl, L, 0x11, cs, false);                                       // D70: titulo
        DibC(dl, L, 0x26, cs, false);                                       // D78: Babidi
        DibC(dl, L, 0x14, cs, false);                                       // D74: bocadillo
        DibC(dl, L, 0x1A, cs, false);                                       // D7C: recuadro
        // 8C30 (1P) esta en y=144 y 8C34 (2P) en y=196 (clip 0x23): el elegido lleva 0x1E y el otro 0x1F.
        DibC(dl, L, 0x1E, cs, false, 0, 0, salb_dos ? 52.0f : 0.0f);
        DibC(dl, L, 0x1F, cs, false, 0, 0, salb_dos ? 0.0f : 52.0f);
      } else {
        // EU 0x249F60 FUN_00249f60 (fundido; b2.c:199262): el nivel de fundido ya vale 0 (negro)
        dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 255));
        if (cs >= fin + 1.0f) {
          // EU 0x232E00 FUN_00232e00 (uGpffff8870 = 1; b2.c:187351): un cuadro en negro y cambia de escena
          salb = false;
          return salir(1);
        }
      }
      return 0;
    }
    Obj(dl, L, 15, 16, g_cuadro - g_c_tit);
    // (25/10, usuario: el titulo nativo ya estaba bien; el rotulo superpuesto de LUNA27 tapaba el original: quitado)
    Babidi(dl, L, 0, false);
    if (!salm) Obj(dl, L, 24, 25);
    if (!salm && g_cuadro >= DurDe(24)) {   // 1P (28) / 2P (35): el elegido hace el vaiven; el otro, en gris
      const bool dos_j = g_2p.load();
      DibC(dl, L, dos_j ? 35 : 28, g_cuadro, true);
      // PUL-B2 (dbz3_b2_fx_nativo): el gris del no elegido es el de la lista B del reposo 29 (rgb 0,5 = 128), no 150 a mano.
      const bool fxg = REXCVAR_GET(dbz3_b2_fx_nativo) && g_anim.size() > 29;
      DibC(dl, L, dos_j ? 28 : 35, 0, false, 0, 0, 0,
           fxg ? TintBNave(IM_COL32_WHITE, g_anim[29].listab, -1.0f) : IM_COL32(150, 150, 150, 255));
      if (fxg) PulB2LogNave(2, "Nave 1P/2P: fila no elegida con el gris de la lista B del #AMA 905 #29 (rgb 0,5; FUN_001a8d10)");
    }
    // Barra de controles dentro del margen inferior del marco, sin invadir el bocadillo. (25/10: como la del original,
    // cruceta = seleccionar, confirmar y atras; antes decia "Elegir" dos veces con la letra por defecto, punteada)
    S::HelpLine(dl, L.P(320, 425), 22 * L.s * L.k,
                {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Atrás", "Back")}});
    // EU 0x2629B0 FUN_002629b0 (montaje de la pantalla: 0x699D7C clip 0x18; 0x8C30 y 0x8C34 clip 0x1B)
    // EU 0x265310 FUN_00265310 (sin mandos hasta que los dos objetos 0x8C30/0x8C34 salen del estado 1,
    // es decir hasta que termina el clip 0x1B = 27; despues pone el elegido en estado 7 con el clip 0x21 = 33).
    if (g_cuadro < DurTarea(27)) {
      pressed = 0;
      ok = false;
    }
    // EU 0x260630 FUN_00260630 / EU 0x264F60 FUN_00264f60 (1P/2P): arriba/abajo (0x10/0x40 de los cuatro registros de
    // mando) alternan 1P y 2P con el sonido 2, solo si AMBOS mandos estan conectados; la cruz confirma (la del mando 2
    // elige 2P) y el triangulo vuelve. En 1P el comportamiento es el de antes.
    if (dos && (pressed & (kUp | kDown))) {
      g_2p = !g_2p.load();
      Efecto(0, 2);
      REXLOG_INFO("dbz3 nave: 1P/2P -> {}", g_2p.load() ? "2P (dos mandos)" : "1P");
    }
    if (dos && (e2 & kA) && !g_2p.load()) {   // el mando 2 confirma: elige 2P directamente
      g_2p = true;
      Efecto(0, 2);
      REXLOG_INFO("dbz3 nave: 1P/2P -> 2P (confirma el mando 2)");
    }
    if (!salm && (pressed & kB)) {
      // EU 0x264F60 FUN_00264f60 (rama 0x1000 = triangulo/B): sonidos 5 y 0x16 y el cierre de la pantalla;
      // el fundido y el cambio de escena los hace 0x264EA0 -> 0x249F60 -> 0x232E00 (ver el bloque de `salb`).
      Efecto(0, 5);      // b2.c:211890
      Efecto(0, 0x16);   // b2.c:211895 (mismo canal: corta al anterior, como en el original)
      salb = true;
      salb_dos = g_2p.load();
      salb_c0 = g_cuadro;
      return 0;
    }
    // EU 0x264F60 FUN_00264f60 (cruz, 0x4000): sonidos 5 y 0x16; 0x699D7C estado 3 clip 0x1A = 26;
    // 0x8C30 y 0x8C34 estado 5 clip 0x1F = 31; el circulo elegido estado 0 clip 0x1E = 30 y luego
    // estado 8 clip 0x22 = 34. La tarea 0x264E20 espera a que 0x8C30/0x8C34 y 0x699D7C vuelvan a estado 0.
    // EU 0x264E20 FUN_00264e20 (espera los dos 0x8C30/0x8C34 y 0x699D7C; despues pasa a 0x264DE0, kPrueba).
    if (salm) {
      const float cs = g_cuadro - salm_c0;
      DibC(dl, L, 26, cs, false);
      // FUN_00264F60: 30 en estado 0 y enseguida 34 en estado 8; NO encadena 11+21.
      // PINE II/III/IV: aceptar -> 0x264DE0 en 18 campos PAL, mas el despacho de entrada.
      DibC(dl, L, 34, cs, false);
      if (cs + 0.001f >= std::max({DurTarea(26), DurTarea(34), DurTarea(31)}) || auto_on) {
        salm = false;
        g_estado = kPrueba;
      }
      return 0;
    }
    if (ok) {
      Efecto(0, 5);      // b2.c:211890
      Efecto(0, 0x16);   // b2.c:211895 (corta al anterior, como en el original: mismo canal)
      salm = true;
      salm_c0 = g_cuadro;
    }
    return 0;
  }
  // EU 0x264790 FUN_00264790 (cursor arriba/abajo sin dar la vuelta, X elige, triangulo vuelve; 2P usaria los dos mandos)
  // EU 0x262830 FUN_00262830 (pone la pantalla de pruebas en sus animaciones)
  if (g_estado == kPrueba) {   // 0x262830 / 0x264790
    // EU 0x264670 FUN_00264670
    // EU 0x264710 FUN_00264710
    // FUN_00264670 espera estado 0 en 0x699D70/74/88/8C/7C y estado 2 en
    // 0x699D60; FUN_00264710 espera estado 0 en 0x699D88/8C/7C y estado 2 en
    // 0x699D60. FUN_00245AD0 modo 3 pone el clip en cola en +0x3C y su espera
    // (+0x38) es max(+0x20,+0x24) del #AMA. Reproducimos esos estados/esperas,
    // no un plazo visual común: el port carece de los objetos originales.
    static bool sal = false;
    static float sal_c0 = 0;
    static int sal_dest = kJugador;
    if (sal) {
      pressed = 0;
      ok = false;
    }
    // EU 0x262830 FUN_00262830 (monta la pantalla de pruebas: pone en estado 1 y clip de entrada a cada objeto)
    // EU 0x264DE0 FUN_00264de0 (FUN_00262830 y despues 0x264D00)
    // EU 0x264D00 FUN_00264d00 (espera a que los 8 objetos 0x699D70..0x699D8C y 0x699D60 SALGAN del estado 1)
    // Objetos y clips de entrada de FUN_00262830: 0x699D70 -> 0x0F, 0x699D74 -> 0x12, 0x699D78 -> 0x24,
    // 0x699D88 -> 0x3D, 0x699D80 -> 0x27 (si cGpffff8c3c != 0), 0x699D84 -> 0x2A (si != 3),
    // 0x699D8C -> 0x40, 0x699D7C -> 0x2D + 4*prueba y 0x699D60 -> 0x49. La espera es max(+0x20,+0x24) de
    // cada clip (DurTarea), no la ultima clave; con la ISO europea el mayor es 0x12 = 61 cuadros.
    if (!sal && g_cuadro < std::max({DurTarea(0x0F), DurTarea(0x12), DurTarea(0x24), DurTarea(0x27),
                                     DurTarea(0x2A), DurTarea(0x3D), DurTarea(0x40),
                                     DurTarea(0x2D + 4 * g_prueba), DurTarea(0x49)})) {
      pressed = 0;
      ok = false;
    }
    const int antes_p = g_prueba;
    if (!sal && (pressed & kUp) && g_prueba > 0) --g_prueba;   // sin dar la vuelta
    if (!sal && (pressed & kDown) && g_prueba < 3) ++g_prueba;
    if (auto_on && !sal) g_prueba = 2;
    if (const char* f = std::getenv("DBZ3_TEST_NAVE_PRUEBA"))   // pruebas: la prueba automatica (I/II)
      g_prueba = std::clamp(std::atoi(f), 0, 3);
    static float desde_p = 0;
    if (g_prueba != antes_p) {   // la "Fase N" nueva entra con su animacion
      desde_p = g_cuadro;
      if (!auto_on) GuardarProgreso("nave_planta", g_prueba);   // N3: la planta en curso no se pierde al salir de la nave
    }
    if (g_prueba != antes_p && !auto_on) Efecto(0, 2);   // cursor (b2.c:211685/211705)
    if (pressed & kB) Efecto(0, SonidoB2(3, 5));          // b2.c:211712 (0x1000 = triangulo -> 5; PUL-B2)
    else if (ok) Efecto(0, SonidoB2(5, 3));               // b2.c:211730 (0x4000 = cruz -> 3; PUL-B2)
    const float cs = g_cuadro - sal_c0;
    const int fa = 47 + 4 * g_prueba;                  // salida de la fase (48 + 4p la remata al elegir)
    if (!sal) {
      Obj(dl, L, 15, 16, g_cuadro - g_c_tit);
      Obj(dl, L, 61, 62);                                // la columna de las 4 casillas
      DibC(dl, L, 66 + g_prueba, 0, false);              // la casilla encendida
      Obj(dl, L, 45 + 4 * g_prueba, 46 + 4 * g_prueba, desde_p);
      if (g_prueba != 0) Obj(dl, L, 39, 40);             // flechas solo si hay mas arriba / abajo
      if (g_prueba != 3) Obj(dl, L, 42, 43);
    } else {
      DibC(dl, L, 17, cs, false);                        // titulo
      DibC(dl, L, 63, cs, false);                        // columna
      DibC(dl, L, 66 + g_prueba, 0, false);
      DibC(dl, L, fa, cs, false);
      if (sal_dest == kJugador && cs >= DurDe(fa)) DibC(dl, L, fa + 1, cs - DurDe(fa), false);
      if (g_prueba != 0) DibC(dl, L, 41, cs, false);
      if (g_prueba != 3) DibC(dl, L, 44, cs, false);
    }
    Babidi(dl, L, 1 + g_prueba, false);                // frase de la prueba (1P: 1-4)
    S::HelpLine(dl, L.P(320, 425), 22 * L.s * L.k,
                {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Atrás", "Back")}});
    if (!sal && (pressed & kB)) {
      sal = true;
      sal_c0 = g_cuadro;
      sal_dest = kModo;
    } else if (!sal && ok) {
      sal = true;
      sal_c0 = g_cuadro;
      sal_dest = kJugador;
    }
    // EU 0x264790 FUN_00264790 (A/cruz: 0x699D70 clip 0x11, 0x699D74 clip 0x14, 0x699D88 clip 0x3F,
    // 0x699D8C clip 0x42, 0x699D7C clip 0x2F+4*prueba y 0x699D60 clip 0x4B en estado 1 -> 2;
    // B/triangulo: los mismos salvo 0x699D70/0x699D74)
    // EU 0x264670 FUN_00264670 (A: espera estado 0 en 0x699D70, 0x699D74, 0x699D88, 0x699D8C, 0x699D7C
    // y estado 2 en 0x699D60, y despues pasa a 0x2644E0 = rueda del jugador)
    // EU 0x264710 FUN_00264710 (B: espera estado 0 solo en 0x699D88, 0x699D8C, 0x699D7C y estado 2 en
    // 0x699D60, y despues pasa a 0x265410 = pantalla 1P/2P). Los clips 0x29/0x2C de 0x699D80/0x699D84
    // no los mira ninguna de las dos esperas.
    if (sal) {
      float fin = std::max({DurTarea(0x3F), DurTarea(0x42), DurTarea(fa), DurTarea(0x4B)});
      if (sal_dest == kJugador) fin = std::max({fin, DurTarea(0x11), DurTarea(0x14)});
      if (g_cuadro - sal_c0 >= fin) {
        sal = false;
        g_estado = sal_dest;
      }
    }
    return 0;
  }
  // Seleccion nativa de B2 (A2nave): rueda de personajes (1P y COM), menu del jugador y burbuja de escenarios. La cabecera
  // ("Select Characters" 118 -> 119, "Stage Selection" 121 -> 122) y Babidi con su bocadillo de arriba son los de siempre.
  const float dt_c = dt * 60.0f;
  if (SaliendoBurbuja() && !g_sel.combate) Spr2(L, g_cab_seleccion, 123, g_sel.c, false);
  else if (SaliendoRueda() && g_sel.destino != kMenu) Spr2(L, g_cab_seleccion, 120, g_sel.c, false);
  else if (g_estado == kSitio) Obj(dl, L, 121, 122, g_cuadro - g_c_tit);
  else Obj(dl, L, 118, 119, g_cuadro - g_c_tit);
  dl->PushClipRect(L.P(0, 0), L.P(640, 448), true);
  if (g_estado == kSitio) {
    Giro2& g = g_giro_burb;
    g.Paso(dt_c);
    const float cs = (t - g_t_esfera) * 60.0f;
    DibBurbuja(L, cs, t);
    VaciarCapas(dl);
    dl->PopClipRect();
    const float frase_alfa = SaliendoBurbuja() && !g_sel.combate && g_cab_seleccion.a.size() > 23
                                ? PoseAma2(g_cab_seleccion.a[23], g_sel.c, false).a : 1;
    Babidi(dl, L, g_frase_sitio, true, frase_alfa);
    // La burbuja original lleva cruceta / confirmar / atras, igual que la pantalla 1P/2P.
    S::HelpLine(dl, L.P(320, 425), 22 * L.s * L.k,
                {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Atrás", "Back")}});
    if (g_sel.fase != SelFase::quieta) return 0;
    if ((pressed & kB) && cs >= 45.0f && !g.Girando()) {
      Efecto(0, SonidoB2(3, 5));   // FUN_002AFEB0 0x1000 (b2.c:253199)
      SalidaSeleccion(true, kRival, false, t);
      return 0;
    }
    if (cs >= 45.0f && !g.Girando()) {
      if (pressed & kLeft) {
        g.Mover(-1, int(g_cel.size()), false);
        if (g.Girando()) Efecto(0, SonidoB2(2, 0x1A));   // FUN_002AFEB0 b2.c:253147 (+0x5c = 0 en FUN_002b2fe0/37a0)
      } else if (pressed & kRight) {
        g.Mover(1, int(g_cel.size()), false);
        if (g.Girando()) Efecto(0, SonidoB2(2, 0x1A));
      } else if (pressed & kY) {
        g.Saltar(int(g_cel.size()) - 1, int(g_cel.size()));   // el cuadrado salta al "?" (FUN_002AFEB0)
        if (g.Girando()) Efecto(0, SonidoB2(2, 0x1A));
      }
    }
    if (g_sitio_vis != g.obj) g_burb_imagen_t = t;
    g_sitio_vis = g.obj;
    if (ok && cs >= 45.0f && !g.Girando()) {
      Efecto(0, SonidoB2(5, 3));   // FUN_002AFEB0 0x6000 (b2.c:253156)
      const bool azar = g.obj == int(g_cel.size()) - 1;
      g_sitio = azar ? g_cel[size_t(std::rand() % std::max(1, int(g_cel.size()) - 1))] : g_cel[size_t(g.obj)];   // el "?" elige uno al azar
      g_vida = 0;
      g_vencidos = 0;
      ReiniciarGolpesB2();
      REXLOG_INFO("dbz3 nave: eleccion jugador {} rival {} escenario {} (id B2 {}){} planta {}", g_j3, g_r3, g_sitio,
                  kEscenas2[g_sitio].id_b2, azar ? " al azar" : "", g_prueba + 1);
      g_sel.fase = SelFase::burbujaConfirma; g_sel.t0 = t; g_sel.c = 0;
      REXLOG_INFO("dbz3 nave: transicion burbuja confirmacion {} cuadros", DurAma2(g_burb, 19));
      return 0;
    }
    return 0;
  }
  // rueda (1P, menu o COM)
  Giro2& g = g_giro_rueda;
  g.Paso(dt_c);
  const float cr = (t - g_t_rueda) * 60.0f;
  DibRueda(dl, L, cr, g_estado == kRival ? (g_2p.load() ? 2 : 1) : 0, g_estado == kMenu);
  if (g_estado == kMenu) DibMenuRueda(L, (t - g_t_menu) * 60.0f, g_menu_sel);
  VaciarCapas(dl);
  dl->PopClipRect();
  {   // nombre de los luchadores sin rotulo de B2 (el de B3 HD, en el sitio del rotulo nativo)
    const std::vector<Entrada>& pg = Pag();
    if (g.obj < int(pg.size()) && pg[size_t(g.obj)].cara < 0 && cr >= 40.0f)
    {   // blanco con borde oscuro, como los rotulos nativos de B2
      const std::string nm = NombreDe(pg[size_t(g.obj)].b3);
      ImFont* fnt = ImGui::GetFont();
      const float tam = 34 * L.s;
      ImVec2 sz = fnt->CalcTextSizeA(tam, 1e9f, 0.0f, nm.c_str());
      if (sz.x > 250 * L.s) { const float k = 250 * L.s / sz.x; sz.x *= k; sz.y *= k; }
      const float t2 = tam * (sz.y / std::max(1.0f, fnt->CalcTextSizeA(tam, 1e9f, 0.0f, nm.c_str()).y));
      const ImVec2 c = L.P(161, 256), p0(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f);
      const float b = std::max(2.0f, 3.0f * L.s);
      for (int a = 0; a < 8; ++a)
        dl->AddText(fnt, t2, ImVec2(p0.x + b * std::cos(a * 0.7853982f), p0.y + b * std::sin(a * 0.7853982f)), IM_COL32(30, 22, 30, 255), nm.c_str());
      dl->AddText(fnt, t2, p0, IM_COL32_WHITE, nm.c_str());
    }
    if (g_pags.size() > 1)
      S::MenuText(dl, L.P(520, 30), 14 * L.s, (std::string("L1 / R1  ") + std::to_string(g_pag + 1) + "/" + std::to_string(g_pags.size())).c_str(), false, false);
  }
  const float frase_alfa = SaliendoRueda() && g_sel.destino != kMenu && g_cab_seleccion.a.size() > 23
                              ? PoseAma2(g_cab_seleccion.a[23], g_sel.c, false).a : 1;
  Babidi(dl, L, g_frase_sel, true, frase_alfa);
  AyudaB2(dl, L, t);
  if (g_sel.fase != SelFase::quieta) return 0;
  if (g_estado == kMenu) {
    if ((t - g_t_menu) * 60.0f < std::max(DurAma2(g_rueda, 0), DurAma2(g_rueda, 10))) return 0;
    if (pressed & kB) {
      Efecto(0, SonidoB2(3, 5));   // FUN_0025F7D0 0x1000 (b2.c:209208)
      g_sel.fase = SelFase::menuSale; g_sel.t0 = t; g_sel.c = 0;
      return 0;
    }
    // EU 0x25F7D0 FUN_0025F7D0 (lista vertical de cGpffff8be0 filas, 0x699D30): abajo en la ultima fila vuelve a la primera y arriba
    // en la primera salta a la ultima (si hay mas de una fila); sonido 2 en cada cambio; cruz (0x4000) elige, triangulo (0x1000)
    // sale. FUN_0025EE10 (b2.c:208938-208957) crea cGpffff8be0 filas, una por byte no nulo de 0x6936F0 + 0xB6E (hasta 6;
    // etiquetas i+0x21), y FUN_0025F7D0 hace la vuelta arriba/abajo. El port dibuja las tres filas del arte (Normal / Custom /
    // Edit Skills; la 3.a no aplica lista y abre la pantalla 22): diferencia documentada (F8, B2-12), las filas del original
    // dependen de la partida guardada.
    if (pressed & kUp) {
      g_menu_sel = (g_menu_sel + 2) % 3;
      Efecto(0, 2);
    }
    if (pressed & kDown) {
      g_menu_sel = (g_menu_sel + 1) % 3;
      Efecto(0, 2);
    }
    if (ok && g_menu_sel == 2) {   // Edit Skills NATIVO (pantalla 22); al salir el juego vuelve al menu principal y la nave sigue aqui
      Efecto(0, SonidoB2(5, 3));   // FUN_0025F7D0 0x4000 (b2.c:209238)
      if (dbz3::hub::RequestEditSkills()) REXLOG_INFO("dbz3 nave: Edit Skills nativo pedido (personaje B3 {} / pagina {})", g_j3, g_pag + 1);
      return 0;
    }
    if (ok) {
      Efecto(0, SonidoB2(5, 3));   // FUN_0025F7D0 0x4000 (b2.c:209238)
      // EU 0x263D20 FUN_00263d20
      // EU 0x2640E0 FUN_002640e0
      // FUN_00260CF0 (b2.c): seleccion 0/1 elige Normal/Custom; la opcion 2 es Edit Skills y no altera esta eleccion.
      g_lista_capsulas = g_menu_sel == 1 ? 1 : 0;
      g_elegida[0].lista = g_lista_capsulas;
      g_recordada[0] = g_elegida[0];
      REXLOG_INFO("dbz3 nave: menu {} (lista de capsulas {})", g_menu_sel == 0 ? "Normal" : "Custom",
                  g_lista_capsulas ? "Custom" : "Normal");
      g_sel.fase = SelFase::menuConfirma; g_sel.t0 = t; g_sel.c = 0;
      REXLOG_INFO("dbz3 nave: transicion menu confirmacion 60 cuadros");
    }
    return 0;
  }
  // EU 0x25FE30 FUN_0025fe30
  if ((pressed & kB) && cr >= 60.0f && !g.Girando()) {
    Efecto(0, SonidoB2(3, 5));   // rueda en estado 0x11 -> FUN_002b2fe0/37a0 (b2.c:254510/254758): sonido 5
    // 1P: el triangulo del COM termina toda la seleccion (motor 6 -> 8),
    // FUN_002640E0 -> 0x264DE0 -> 0x264790. No vuelve al menu ni a otra rueda.
    SalidaSeleccion(false, g_estado == kRival && g_2p.load() ? kJugador : kPrueba, false, t);
    return 0;
  }
  const int n = int(Pag().size()) + 1;
  if (cr >= 60.0f && !g.Girando()) {
    // L1 / R1 + cruceta = "Quick select" de B2 (giro de 5 cuadros en vez de 10); solo L1 / R1, al soltarlo sin cruceta: otra pagina del roster
    const uint16_t sol = g_mant & (kLB | kRB), suelto = g_sol_ant & uint16_t(~sol);
    g_sol_ant = sol;
    if (sol && (pressed & (kLeft | kRight))) g_sol_uso = true;
    if (!sol && !suelto) g_sol_uso = false;
    if (g_pags.size() > 1 && suelto && !g_sol_uso) {
      g_sol_uso = false;
      const int np = int(g_pags.size());
      g_pag = ((g_pag + ((suelto & kRB) ? 1 : -1)) % np + np) % np;
      g.Fijar(0);
      g_traje_rueda = 0;
      g_t_rueda = t;
      Efecto(0, 2);
      REXLOG_INFO("dbz3 nave: pagina {}/{} de la rueda", g_pag + 1, np);
      return 0;
    }
    // prueba IV (medido en PCSX2, 4 plantas): en la rueda del COM de la ultima planta el cursor se salta al luchador del 1P
    auto paso = [&](int a) {
      const int sig = (g.obj + a + n) % n;
      if (g_estado == kRival && g_prueba == 3 && sig < n - 1 && Pag()[size_t(sig)].b3 == g_j3) a *= 2;
      return a;
    };
    const float giro = sol ? 5.0f : 10.0f;
    if (pressed & kLeft) {
      g.Mover(paso(-1), n, true, giro);
      if (g.Girando()) Efecto(0, 2);
    } else if (pressed & kRight) {
      g.Mover(paso(1), n, true, giro);
      if (g.Girando()) Efecto(0, 2);
    }
    if (g.Girando()) {
      g_traje_rueda = 0;   // FUN_002AB8C0: otro luchador vuelve al primer traje
      if (g_estado == kRival && g.obj < n - 1 && Pag()[size_t(g.obj)].b3 == g_j3 && g_elegida[0].traje == 0)
        g_traje_rueda = TrajesDe(Pag()[size_t(g.obj)]) > 1 ? 1 : 0;
    } else if (g.obj < n - 1 && (pressed & (kUp | kDown))) {
      const int nt = TrajesDe(Pag()[size_t(g.obj)]), dir = (pressed & kUp) ? 1 : -1;
      int tr = g_traje_rueda;
      for (int i = 0; i < nt; ++i) {
        tr = (tr + nt + dir) % nt;
        if (g_estado != kRival || Pag()[size_t(g.obj)].b3 != g_j3 || tr != g_elegida[0].traje) break;
      }
      if (tr != g_traje_rueda) {
        g_traje_rueda = tr;
        Efecto(0, 2);
        REXLOG_INFO("dbz3 nave: traje {} id {} -> {} de {}", g_estado == kRival ? "COM/2P" : "1P",
                    Pag()[size_t(g.obj)].b3, tr, nt);
      }
    }
  }
  // EU 0x263A80 FUN_00263a80 (rueda lista -> elegido: guarda el luchador, sonido y paso a la pantalla siguiente)
  if (ok && cr >= 60.0f && !g.Girando()) {
    const bool azar = g.obj == n - 1;
    int casilla = azar ? std::rand() % (n - 1) : g.obj;   // el "?" elige uno al azar de la pagina
    Efecto(0, SonidoB2(5, 3));   // FUN_002AB8C0 0x4000 (b2.c:250756)
    const Entrada e = Pag()[size_t(casilla)];
    if (g_estado == kJugador) {
      g_luch = casilla;
      g_j3 = e.b3;
      g_elegida[0] = {e.b3, g_traje_rueda, g_lista_capsulas};
      REXLOG_INFO("dbz3 nave: eleccion 1P {} (casilla {}, id B2 {}, id B3 {}, pagina {}){}", NombreDe(e.b3), casilla, e.b2, e.b3, g_pag + 1, azar ? " al azar" : "");
      g_estado = kMenu;
    } else {
      if (e.b3 == g_j3 && g_prueba == 3) casilla = (casilla + 1) % (n - 1);
      const Entrada e2 = Pag()[size_t(casilla)];
      g_riv = casilla;
      g_r3 = e2.b3;
      const int nt = TrajesDe(e2);
      int tr = azar ? std::rand() % nt : g_traje_rueda;
      if (g_r3 == g_j3 && tr == g_elegida[0].traje) tr = (tr + 1) % nt;
      g_elegida[1] = {e2.b3, tr, 0};
      g_recordada[1] = g_elegida[1];
      REXLOG_INFO("dbz3 nave: eleccion {} {} (casilla {}, id B2 {}, id B3 {}, pagina {}){}", g_2p.load() ? "2P" : "COM", NombreDe(e2.b3), casilla, e2.b2, e2.b3, g_pag + 1, azar ? " al azar" : "");
      g_sel.fase = SelFase::ruedaConfirma; g_sel.t0 = t; g_sel.c = 0;
      REXLOG_INFO("dbz3 nave: transicion rueda confirmacion 60 cuadros");
    }
  }
  return 0;
}

// Fin del combate de la prueba (lado 0 = jugador): segundos que sobraban (-1 sin limite) y golpes que quitaron vida.
void Resultado(int ganador, int vida, int segundos, int golpes) {
  g_trajes_pendientes = 0;
  combate::OndasModo(false);
  const int cuadros = g_cuadros_lucha.load();
  ++g_reloj;   // para el reloj de la prueba II
  REXLOG_INFO("dbz3 nave: fin: gana el lado {}, vida {}, {} s de sobra, {} golpes, {} ondas devueltas, {} cuadros de pelea",
              ganador + 1, vida, segundos, golpes, combate::OndasDevueltas(0), cuadros);
  // B3 devuelve -1 tambien en un empate al agotarse el reloj. En II-IV es un resultado valido de la prueba;
  // salir desde la pausa conserva segundos=-1 (Fin no capturo el final) y no da Kili.
  const bool fin_por_tiempo = g_prueba >= 1 && segundos == 0;
  if (ganador < 0 && !fin_por_tiempo) {
    g_estado = kPrueba;
    return;
  }
  if (g_prueba == 0) {
    if (g_2p.load()) {   // 2P: es un versus (no la prueba I de vidas sin fin); sin Kili ni cadena
      g_cuenta = 0;
    } else if (ganador == 0) {
      if (g_enc_armado) {   // el combate siguiente ya se pidio (pasarela_misiones.cpp sub_820FE9B0): Seguido ya conto
        g_estado = kSigue;
        return;
      }
      ++g_vencidos;   // el rival cae y se levanta: uno mas, y sigue con la vida que le queda
      g_vida = std::max(1, vida);
      g_estado = kSigue;
      return;
    } else {
      g_cuenta = g_vencidos.load();
    }
  } else if (g_prueba == 1) {
    g_cuenta = (std::min(cuadros, kTopeII * 60) + 60) / 60;
  } else if (g_prueba == 2) {
    // B2-08: el contador de la planta III del original suma golpes (0x9DD0). Fin::golpes cuenta los golpes que
    // quitaron vida; el contador vivo del HUD (bajadas de vida del rival, medido con los dorados 2/10/17) manda si
    // es mayor: es la cifra que el jugador ha visto. Con valores iguales no cambia nada.
    g_cuenta = golpes;
    if (kContadorVivoB2) {
      const int vivos = GolpesHudB2();
      if (vivos > g_cuenta) {
        REXLOG_INFO("dbz3 nave: prueba III: contador vivo {} frente a Fin::golpes {} (se usa el vivo)", vivos, golpes);
        g_cuenta = vivos;
      }
    }
  } else {
    g_cuenta = combate::OndasDevueltas(0);   // IV: ondas de ki devueltas (combate.cpp, sub_82122488)
  }
  if (const char* c = std::getenv("DBZ3_TEST_NAVE_CUENTA")) g_cuenta = std::atoi(c);   // pruebas: la barra y premios
  if (g_cuenta > 0) RangoMejora("rango_nave_" + std::to_string(g_prueba), 1, 25);   // escalafon: cada prueba, una vez
  // EU 0x263990 FUN_00263990
  EmpezarResultado();
  g_estado = kResultado;
}

// Desbloqueo de la partida (lo llama el tablero cuando dispara el suceso `ev` de b2.c: 0x1CD..0x1E9 luchador, 0x1FE..0x206 escenario).
void DesbloquearEvento(int ev) {
  if (ev >= 500 && ev <= 502) {
    // EU FUN_002831f0 b2.c:229179-229193: 500 -> +0xB0 = 0, 501 -> 1, 502 -> 2 (siempre como minimo).
    // NO CONFIRMADO que efecto tiene +0xB0: se guarda el valor, sin inventar ningun desbloqueo.
    const int actual = LeerProgreso("nave_progreso", 0);
    const int querido = ev - 500;
    if (querido > actual) GuardarProgreso("nave_progreso", querido);
    REXLOG_INFO("dbz3 nave: progreso +0xB0 a {} (suceso {}) [NO CONFIRMADO el efecto]", querido, ev);
  } else if (ev >= 0x1CD && ev <= 0x1E9) {
    const int id = kEvPers[ev - 0x1CD];
    uint32_t lo = uint32_t(LeerProgreso("nave_pers_lo", int(kPersLo))), hi = uint32_t(LeerProgreso("nave_pers_hi", 0));
    if (id < 32) lo |= 1u << id; else hi |= 1u << (id - 32);
    GuardarProgreso("nave_pers_lo", int(lo));
    GuardarProgreso("nave_pers_hi", int(hi));
    REXLOG_INFO("dbz3 nave: desbloqueado el luchador {} de B2 (suceso {:X})", id, ev);
  } else if (ev >= 0x1FE && ev <= 0x206) {
    GuardarProgreso("nave_esc", LeerProgreso("nave_esc", 0x05) | (1 << kEvEsc[ev - 0x1FE]));
    REXLOG_INFO("dbz3 nave: desbloqueado el escenario bit {} (suceso {:X})", kEvEsc[ev - 0x1FE], ev);
  }
}
// FUN_00260CF0 (b2.c): Custom es una lista de combate distinta; Edit Skills es la tercera opcion separada.
int ListaCapsulas() {
  // Justo antes de RequestBattleEx/ChainBattleEx; el despacho aplica los trajes
  // en el hilo guest DESPUES de que hub rellene la CFG (sin carrera entre hilos).
  g_trajes_pendientes = (uint64_t(1) << 63) | uint64_t(g_j3 & 0xFF) | (uint64_t(g_r3 & 0xFF) << 8)
                       | (uint64_t(g_elegida[0].traje & 0xFF) << 16) | (uint64_t(g_elegida[1].traje & 0xFF) << 24);
  return g_lista_capsulas;
}
void AplicarTrajesNave(PPCContext& ctx, uint8_t* base) {
  if (ctx.r3.u32 != 10 || !g_trajes_pendientes.load()) return;
  const uint64_t p = g_trajes_pendientes.exchange(0);
  constexpr dbz3::GA cfg = 0x8245EFC0;
  if (REX_LOAD_U16(cfg + 32) != (p & 0xFF) || REX_LOAD_U16(cfg + 112) != ((p >> 8) & 0xFF)) return;
  int tr[2] = {};
  for (uint32_t lado = 0; lado < 2; ++lado) {
    const uint32_t id = uint32_t((p >> (8 * lado)) & 0xFF);
    const uint32_t nt = REX_LOAD_U8(dbz3::GA(0x8234ABB8) + 96u * id + 4);
    const uint32_t elegido = uint32_t((p >> (16 + 8 * lado)) & 0xFF);
    tr[lado] = elegido < nt ? int(elegido) : 0;
    REX_STORE_U16(cfg + 34 + 80u * lado, uint16_t(tr[lado]));
  }
  REXLOG_INFO("dbz3 nave: trajes en CFG {} / {} (seleccion {} / {})", tr[0], tr[1], (p >> 16) & 0xFF, (p >> 24) & 0xFF);
}

// Para el tablero de B2 (cápsula al azar de sus casillas): la de B3 equivalente a una de B2, 0 si no hay.
int CapsulaDeB2(int id_b2) { return CapsulaB3(id_b2); }

// ---- Orden 70 del tablero de B2 (studies 8, E): FUN_00293740 -> FUN_002835F0 + la ventana de premio de la nave ----
// FUN_002835F0(grupo, mascara, inventario) (b2.c:229323): recorre los 0x208 registros del #SKA (en RAM 0x6942A0 = DATA_CMN
// 851 + 0x20); vale el que tiene +0x14 == grupo y su mascara de personajes (+0, u64) es 0 o comparte bit con la pedida.
// Lista A: los que valen y aun no estan al maximo (+0x0D); lista B: los que valen con mascara != 0. Elige al azar en A
// (FUN_0012E4E0) y, si A esta vacia, en B; sin ninguno, 0. La mascara pedida es la de los personajes que tienes
// (FUN_00245DD0: tabla 0x377E40 de 36 capsulas de personaje) con un AND con el bit del personaje de la pieza.
// Mascara de personaje de FUN_00245DD0: incluye solo personajes desbloqueados en la progresion B2.
int CapsulaAlAzar(int grupo, int personaje_b2) {
  const std::vector<uint8_t>& d = TablaCapsulas();
  if (d.size() < 0x40 || std::memcmp(d.data(), "#SKA", 4) != 0) return 0;
  // personaje < 0: la orden 8 pide la mascara de todos los personajes que tienes, no todos los IDs posibles.
  uint64_t tengo = 0;
  if (personaje_b2 < 0) {
    for (int id = 0; id < 64; ++id)
      if (PersAbierto(id)) tengo |= uint64_t(1) << id;
  } else if (personaje_b2 < 64 && PersAbierto(personaje_b2)) {
    tengo = uint64_t(1) << personaje_b2;
  }
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
  // FUN_002835F0 (b2.c:229372/:229377): una llamada a FUN_0012E4E0(0x20), el resto entre los candidatos que quedan.
  return l[size_t(b2::B2Rng(0x20) % uint32_t(l.size()))];
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
// EU 0x2A4410 FUN_002a4410 (crea la tarea del cartel "Get skill!") / EU 0x2A42B0 FUN_002a42b0 (estado: -1 = libre)
// EU 0x292550 FUN_00292550 (ficha "Skill Z Sheet") / EU 0x292850 FUN_00292850 (carga sus recursos)
// EU 0x2A4410 FUN_002a4410
// EU 0x2A42B0 FUN_002a42b0
// EU 0x292550 FUN_00292550
// EU 0x292850 FUN_00292850
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

// hub.cpp despacha sub_821D27C8(10) tras completar ambos registros de combate.
// El original se llama siempre; sin una peticion de la Nave no hay cambios.
DBZ3_HOOK_EU(sub_821D27C8) {
  dbz3::pasarela::nave::AplicarTrajesNave(ctx, base);
  orig(ctx, base);
}
