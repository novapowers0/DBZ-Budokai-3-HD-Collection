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
//     el port lo modela con `DurTarea(27)` (DS127; antes `DurDe(24)` [H]). Si no hay dos mandos (0x3B43A0 / 0x3B4458 == 3) deja 1P: PORTADO (2026-10-26: 2P con dos mandos; el rival es el mando 2).
//   0x264DE0 FUN_00264DE0: FUN_00262830 y 0x264D00 (espera a los 8 objetos 0x699D70..0x699D8C y a 0x699D60;
//     clips 0x0F/0x12/0x24/0x27/0x2A/0x3D/0x40/0x2D+4p/0x49; manda max(+0x20,+0x24), 61 cuadros). PORTADO por DS127 con `DurTarea`.
//   0x264670 FUN_00264670 / 0x264710 FUN_00264710: esperan a los objetos y ceden a 0x2644E0 (seleccion) / 0x265410.
//     PORTADO por DS127: clips 0x11/0x14/0x3F/0x42/0x2F+4p y 0x699D60 0x4B (max(+0x20,+0x24), 21 cuadros); B no espera 0x11/0x14.
// Mas funciones de pantalla, leidas: EU 0x261880 FUN_00261880 (cuando el objeto 1P/2P llega a su bucle, arranca el fundido uGpffff8c2c y la voz de
//   Babidi 1149 con FUN_0012BA00(1, DAT_00378370): el port la suena al abrir la pantalla `Voz(1149)` [H: unos cuadros antes que el original]); EU 0x263990 FUN_00263990 (carga de la pantalla de resultado: espera a los 3 objetos y a 0x699DC0, y construye 0x2624F0, 0x2621F0
//   0x262640 -> 0x263900 = kResultado); EU 0x263700 FUN_00263700 (cuenta del resultado: +1 cada 3 cuadros de 60 Hz, `dt` PAL ya aplicado
//   dentro: 1,2 / 1,0; el port usa 3 cuadros de 60 Hz) y cede a 0x263670 (no leida).
// Pegamento de la rueda (b2.c; FUN_002B4430 = estado del motor de la rueda 3D: 3 abierta, 5 y 6 elegido, 7 lista, 8 cerrada hacia atras,
// 9 elegido, 10 atras, 11 cerrada), cada funcion comparada con el port: EU 0x25FE30 FUN_0025FE30: estado 0 espera a que el objeto 0x699D18 pare; 1 con la rueda en 7 lanza su anim 0x28; 2 con 9 (elegido)
//     guarda el luchador (FUN_002B4270 -> 0x693718), anim 0x2A y cambia a la escena 0x12; con 10 (atras) vuelve a 0x25FFF0; 3 con 11
//     cierra. COINCIDE en lo esencial con kJugador/kRival (elegir -> siguiente, B -> atras) [H: tiempos de las anims 0x28/0x2A]. EU 0x263D20 FUN_00263D20 (1.a rueda) y EU 0x2640E0 FUN_002640E0 (rueda del jugador/COM): estado 0 con la rueda en 3 lanza la
//     anim 0x76; 1 con 5 (elegido) copia de cada jugador (FUN_002B4290: personaje, bandera, "Custom") su lista de capsulas a
//     0x699540 + 0x10 n: la por defecto del personaje (registro +0x2B8, tabla 0x3897F8 + 0x2C8 c) si no es "Custom", la propia si lo es
//     (= `g_lista_capsulas` 0 Normal / 1 Custom del port); con 6 (atras) anim 0x78 y estado 2; 2 con 8 cierra hacia 0x264DE0 (prueba).
//     En 0x263D20 con 2 jugadores (cGpffff8c38 == 1) y la 3.a prueba (cGpffff8c3c == 2) salta a la escena 0x19 con modo 3: solo 2P.
//     COINCIDE; [H]: el port no copia la lista a 0x699540 (la lista Normal/Custom se usa al pedir el combate de B3).
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
#include "pasarela.h"

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
#include <mutex>
#include <vector>

#include <memory>

#include <rex/ui/immediate_drawer.h>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include "mods.h"
#include "generated/dbz3_init.h"
#include "escenarios.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "ps2_modelo.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

namespace dbz3 {   // main.cpp: sin foco la ventana se oscurece y el F1 esta encima (la pausa del modo no cuenta)
bool GameDimmed();
bool OverlayOpen();
}
namespace dbz3::hub { bool RequestEditSkills(); }   // hub.cpp: abre el Edit Skills nativo (pantalla 22)
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
int g_vida = 0, g_vencidos = 0, g_cuenta = 0, g_frase_sel = 9, g_frase_sitio = 11;
float g_auto_t = 0;
std::atomic<bool> g_enc_armado{false};   // prueba I: pasarela_misiones.cpp ya encadeno el siguiente combate (sub_820FE9B0)
// Entrada por mando (nave::Mandos, la llama pasarela_misiones.cpp antes de Dibujar): pulsaciones de este cuadro y si
// el mando 2 esta conectado. El original lee los cuatro registros de mando 0x3B43B2/0x4412/0x446A/0x44CA y exige
// 0x3B43A0 == 0x3B4458 == 3 para ofrecer 2P (EU 0x264F60 / 0x260630).
std::atomic<uint16_t> g_ent1{0}, g_ent2{0};
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
  REXLOG_INFO("dbz3 nave: prueba {}: {} contra {} en {} (vida {}, tiempo {}, vencidos {}, {})", g_prueba + 1, NombreDe(j),
              NombreDe(r), kEscenas2[g_sitio].es, out[3], out[4], g_vencidos, g_2p.load() ? "2P humano" : "1P");
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
std::vector<Tex> g_cielo_tx;                     // cielo: DATA_CMN 912 (#AMT: 2 texturas de nube 256x256, blancas con alfa)
std::vector<Anim> g_anim, g_animv;               // 905 (nave) y 933 (ventana de premio)
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

// ---- A2nave: #AMA completo (varias piezas, z) para la rueda y la burbuja (ver el bloque grande mas abajo)
struct Pz2 {
  float t = 0, alpha = 1;
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
Ama2 g_rueda, g_burb;
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

// EU 0x23D520 FUN_0023d520 (carga los dos recursos del domo)
// EU 0x23DAE0 FUN_0023dae0 (pide los ids de la tabla gp-0x7E48 con FUN_0012D100, una vez)
// EU 0x23D960 FUN_0023d960 (crea la tarea del domo con la variante: la nave pide la 2)
bool CargarCielo(AfsPs2& cmn, rex::ui::ImmediateDrawer* dr) {
  g_cielo_tri.clear();
  g_cielo_tex.clear();
  g_cielo_tx.clear();
  ps2::Malla m;
  if (!ps2::LeerAmo(cmn.Entrada(911), 2, m, nullptr, true) || !m) {
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
  REXLOG_INFO("dbz3 nave cielo: DATA_CMN 911 #AMG 2 + 912: {} triangulos de {}, {} texturas", g_cielo_tri.size(), m.tris.size(),
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
    texs(loc.Entrada(1127), g_rueda.tex);
    texs(loc.Entrada(1145), g_ayuda);
    g_burb.a = LeerAma2(cmn.Entrada(897));
    texs(cmn.Entrada(898), g_burb.tex);
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
void DibCielo(ImDrawList* dl, const Lienzo& L, float t) {
  // EU 0x23CD00 FUN_0023cd00
  static const float kDy = [] {
    const uint32_t b = 0x3E13E814;   // FUN_0023CD00: componente y del punto al que mira
    float f;
    std::memcpy(&f, &b, 4);
    return f;
  }();
  const float kCs = 1.0f / std::sqrt(1.0f + kDy * kDy), kSn = kDy * kCs;
  // EU 0x131450 FUN_00131450
  // EU 0x1317A0 FUN_001317a0
  constexpr float kFx = 512.0f, kFy = 512.0f * 448.0f / 480.0f, kCerca = 1.0f;
  // EU 0x23D300 FUN_0023d300
  const float sc = std::fmod(0.03f * t, 1.0f);   // FUN_0023D300: +0,0005 * dt por cuadro (0,03 por segundo) en u y v
  const ImVec2 blanco = ImGui::GetFontTexUvWhitePixel();
  struct Q {
    float x, y, z, u, v, c[4];
  };
  int tex_ant = -1;
  for (size_t i = 0; i < g_cielo_tri.size(); ++i) {
    const int tx = g_cielo_tex[i];
    Q q[4];
    int n = 0;
    {   // vertices en el espacio de la camara (x a la derecha, y hacia abajo, z hacia delante) y recorte contra z >= kCerca
      Q a[3];
      for (int k = 0; k < 3; ++k) {
        const CieloV& p = g_cielo_tri[i][size_t(k)];
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
      const float sx = 320.0f + kFx * q[k].x / q[k].z, sy = 224.0f + kFy * q[k].y / q[k].z;
      s[k] = L.P(sx, sy);
      x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
    }
    if (x1 < 0 || x0 > 640 || y1 < 0 || y0 > 448) continue;
    if (tx != tex_ant) {
      if (tex_ant >= 0) dl->PopTexture();
      if (tx >= 0) dl->PushTexture(ImTextureRef(reinterpret_cast<ImTextureID>(g_cielo_tx[size_t(tx)].t.get())));
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

void Babidi(ImDrawList* dl, const Lienzo& L, int frase, bool arriba) {
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
  const float cx = q.x + (b.x0 + b.x1) * 0.5f, cy = q.y + (b.y0 + b.y1) * 0.5f;
  dl->AddImage(reinterpret_cast<ImTextureID>(f.t.get()), L.P(cx - f.w * 0.5f, cy - f.h * 0.5f),
               L.P(cx + f.w * 0.5f, cy + f.h * 0.5f));
}

// ===================================================================================================================
// A2nave (studies 5): rueda de personajes ("Select Characters"), menu Normal/Custom/Edit Skills y burbuja de escenarios
// ("Stage Selection") NATIVAS de B2, leidas de la ISO del jugador en marcha (nunca se reparten):
//   rueda  = #AMA DATA_CMN 901 + #AMT DATA_<idioma> 1127 (77 texturas: palabras del menu, 30 caras en 6 tiras de 5, 30
//            nombres, circulos 1P/2P, rotulo 1P/2P/COM, flechas);  burbuja = #AMA DATA_CMN 897 + #AMT DATA_CMN 898.
// Las dos son un anillo 3D (FUN_002A9950 / FUN_002AF4A0) visto de lado: los puntos de un circulo de radio R, girado
// `cab` (cabeceo) y `roll` (giro del anillo), trasladado a (x0, y0, z0). Medido por PINE en PCSX2 (anillo vivo, 30 + 10
// puntos) y con la formula del ELF; cuadra al decimal:
//     v  = (R sin a, -R cos a sin cab + R0 sin th0, R cos a cos cab - R0 cos th0)     a = 2 pi (arco i / n + roll) / 65536
//     x = x0 + vx - cizalla vz,  y = y0 - vy,  z = z0 + zs vz          (zs = -10; la rueda tiene cizalla -1, la burbuja 0)
//   Rueda  : x0 161, y0 346, z0 1199, R 180, cab 0x3000 (67,5 grados), arco 0x10000, cizalla -1, giro de 10 cuadros
//            (5 con L1/R1), entrada de 60 cuadros: R 600 -> 180, cab -0x4000 -> 0x3000, roll -8 f^2.
//   Burbuja: x0 160, y0 360, z0 1199, R 180, cab 0x1800 (33,75 grados), arco 0xA000 (la burbuja NO da la vuelta),
//            giro de 10 cuadros, entrada de 45 cuadros (mismas formulas).
//   Escala de cada cara (FUN_002A97C0): 1 - (z - z0) 0,0002 y 1,2 la elegida; de cada casilla (FUN_002AF360):
//            1 - (z - z0) 0,0001. Se dibuja de lejos (z grande) a cerca.
// ===================================================================================================================
// Una capa dibujable (los objetos se ordenan por z: grande = lejos = primero)
struct Capa {
  float z;
  ImTextureID tex;
  ImVec2 p[4], uv[4];
  ImU32 col;
  void (*fn)(ImDrawList*) = nullptr;   // el modelo 3D grande (se pinta entre las caras lejanas y las cercanas)
  bool circ = false;   // cara de B3 recortada en el aro de B2 (centro p[0], radio p[1].x, grosor del aro p[1].y)
};
std::vector<Capa> g_capas;
void VaciarCapas(ImDrawList* dl) {
  std::stable_sort(g_capas.begin(), g_capas.end(), [](const Capa& a, const Capa& b) { return a.z > b.z; });
  for (const Capa& c : g_capas) {
    if (c.fn) {
      c.fn(dl);
      continue;
    }
    if (!c.circ) {
      dl->AddImageQuad(c.tex, c.p[0], c.p[1], c.p[2], c.p[3], c.uv[0], c.uv[1], c.uv[2], c.uv[3], c.col);
      continue;
    }
    const ImVec2 ce = c.p[0];
    const float r = c.p[1].x, g = c.p[1].y;
    dl->AddCircleFilled(ce, r, IM_COL32(20, 40, 30, c.col >> 24), 40);
    dl->AddImageRounded(c.tex, ImVec2(ce.x - r, ce.y - r), ImVec2(ce.x + r, ce.y + r), c.uv[0], c.uv[2], c.col, r);
    dl->AddCircle(ce, r + g * 0.5f, IM_COL32(195, 85, 48, c.col >> 24), 40, g);        // aro naranja medido en las caras de B2
    dl->AddCircle(ce, r + g * 1.1f, IM_COL32(40, 18, 10, c.col >> 24), 40, g * 0.5f);   // y su filo oscuro
  }
  g_capas.clear();
}
struct Pose2 {
  float x = 0, y = 0, z = 0, sx = 1, sy = 1, rot = 0, a = 1;
  float m[3] = {1, 1, 1};
};
// Pose de la animacion en el cuadro c: claves (lineal) y alfa de la linea de piezas; bucle = vuelve al principio.
Pose2 PoseAma2(const Anim2& A, float c, bool bucle) {
  Pose2 q;
  if (A.k.empty()) return q;
  const float kd = A.k.back().t, pd = A.p.empty() ? 0.0f : A.p.back().t;
  float ck = c, cp = c;
  if (bucle) {
    ck = kd > 0 ? std::fmod(std::max(0.0f, c), kd) : 0.0f;
    cp = pd > 0 ? std::fmod(std::max(0.0f, c), pd) : 0.0f;
  } else {
    ck = std::min(c, kd);
    cp = std::min(c, pd);
  }
  const Clave2* k0 = &A.k.front();
  const Clave2* k1 = k0;
  for (size_t i = 1; i < A.k.size(); ++i) {
    k1 = &A.k[i];
    if (ck < k1->t) break;
    k0 = k1;
  }
  const float f = k1->t > k0->t ? std::clamp((ck - k0->t) / (k1->t - k0->t), 0.0f, 1.0f) : 1.0f;
  auto m = [&](float a, float b) { return a + (b - a) * f; };
  q.x = m(k0->x, k1->x);
  q.y = m(k0->y, k1->y);
  q.z = m(k0->z, k1->z);
  q.sx = m(k0->sx, k1->sx);
  q.sy = m(k0->sy, k1->sy);
  q.rot = m(k0->rot, k1->rot);
  q.a = m(k0->a, k1->a);
  float pa = 1.0f;
  if (!A.p.empty()) {
    pa = A.p.front().alpha;
    for (int j = 0; j < 3; ++j) q.m[j] = A.p.front().m[j];
    for (size_t i = 0; i < A.p.size(); ++i) {
      if (cp < A.p[i].t) break;
      pa = A.p[i].alpha;
      for (int j = 0; j < 3; ++j) q.m[j] = A.p[i].m[j];
      if (i + 1 < A.p.size() && A.p[i + 1].t > A.p[i].t) {
        const float g = std::clamp((cp - A.p[i].t) / (A.p[i + 1].t - A.p[i].t), 0.0f, 1.0f);
        pa += (A.p[i + 1].alpha - A.p[i].alpha) * g;
        for (int j = 0; j < 3; ++j) q.m[j] += (A.p[i + 1].m[j] - A.p[i].m[j]) * g;
      }
    }
  }
  q.a *= pa;
  return q;
}
// Pone la animacion k de `set` en el cuadro c. pos/esc/z/alfa opcionales: pos = (x, y) final en lugar del de las claves
// (si hay), `d` = desplazamiento, esc = escala extra, z < 0: el de las claves.
void Spr2(const Lienzo& L, const Ama2& set, int k, float c, bool bucle, ImVec2 d = ImVec2(0, 0), const ImVec2* pos = nullptr,
          float esc = 1.0f, float alfa = 1.0f, float z = -1e9f, ImU32 tinte = IM_COL32_WHITE) {
  if (k < 0 || k >= int(set.a.size())) return;
  const Anim2& A = set.a[size_t(k)];
  if (A.p.empty()) return;
  const Pose2 q = PoseAma2(A, c, bucle);
  const Pz2& p = A.p.front();
  if (p.tex < 0 || p.tex >= int(set.tex.size()) || !set.tex[size_t(p.tex)].t) return;
  const Tex& t = set.tex[size_t(p.tex)];
  const float al = q.a * alfa;
  if (al <= 0.001f || q.sx == 0 || q.sy == 0) return;
  const float sx = q.sx * esc, sy = q.sy * esc;
  const float x = (pos ? pos->x : q.x) + d.x, y = (pos ? pos->y : q.y) + d.y;
  Capa cp;
  cp.z = z > -1e8f ? z : q.z;
  cp.tex = reinterpret_cast<ImTextureID>(t.t.get());
  cp.p[0] = L.P(x + A.x0 * sx, y + A.y0 * sy);
  cp.p[1] = L.P(x + (A.x1 + 1) * sx, y + A.y0 * sy);
  cp.p[2] = L.P(x + (A.x1 + 1) * sx, y + (A.y1 + 1) * sy);
  cp.p[3] = L.P(x + A.x0 * sx, y + (A.y1 + 1) * sy);
  cp.uv[0] = ImVec2(p.u0 / t.w, p.v0 / t.h);
  cp.uv[1] = ImVec2(p.u1 / t.w, p.v0 / t.h);
  cp.uv[2] = ImVec2(p.u1 / t.w, p.v1 / t.h);
  cp.uv[3] = ImVec2(p.u0 / t.w, p.v1 / t.h);
  const ImU32 a8 = ImU32(std::clamp(al, 0.0f, 1.0f) * float(tinte >> 24));
  auto cm = [&](ImU32 ch, float m) { return ImU32(std::clamp(float(ch) * m, 0.0f, 255.0f)); };
  cp.col = cm(tinte & 0xFF, q.m[0]) | (cm((tinte >> 8) & 0xFF, q.m[1]) << 8) | (cm((tinte >> 16) & 0xFF, q.m[2]) << 16) | (a8 << 24);
  g_capas.push_back(cp);
}
// Duracion de una animacion para la tarea (entrada -> reposo)
float DurAma2(const Ama2& set, int k) { return k >= 0 && k < int(set.a.size()) ? set.a[size_t(k)].dur : 0.0f; }
// Objeto con entrada `e` (una vez) y reposo `r` (en bucle) desde el cuadro `desde`.
void Obj2(const Lienzo& L, const Ama2& set, int e, int r, float c, ImVec2 d = ImVec2(0, 0), const ImVec2* pos = nullptr,
          float esc = 1.0f, float alfa = 1.0f, float z = -1e9f) {
  if (c < 0) return;
  const float de = e >= 0 ? DurAma2(set, e) : 0.0f;
  if (e >= 0 && c < de) return Spr2(L, set, e, c, false, d, pos, esc, alfa, z);
  Spr2(L, set, r, c - de, true, d, pos, esc, alfa, z);
}

// ---- Anillo 3D
struct Anillo {
  float x0, y0, z0, zs, cizalla, R0, th0, arco;   // th0 en 1/65536 de vuelta
};
constexpr float kPi2 = 6.28318530718f;
void AnilloPos(const Anillo& r, float R, float cab /*1/65536*/, float ang /*1/65536*/, float& x, float& y, float& z) {
  const float a = ang * (kPi2 / 65536.0f), cb = cab * (kPi2 / 65536.0f), t0 = r.th0 * (kPi2 / 65536.0f);
  const float vx = R * std::sin(a);
  const float vy = -R * std::cos(a) * std::sin(cb) + r.R0 * std::sin(t0);
  const float vz = R * std::cos(a) * std::cos(cb) - r.R0 * std::cos(t0);
  x = r.x0 + vx - r.cizalla * vz;
  y = r.y0 - vy;
  z = r.z0 + vz * r.zs;
}
// Parametros de la entrada en el cuadro e (0..T): f = T - e cuadros por recorrer
struct AnilloEntrada {
  float R, cab, roll;
};
AnilloEntrada EntradaAnillo(float e, float T, float cab_fin) {
  const float f = std::max(0.0f, T - e);
  return {180.0f + f * (600.0f - 180.0f) / T, cab_fin + f * (-16384.0f - cab_fin) / T, -(8.0f * f * f)};
}
constexpr Anillo kAnilloRueda = {161, 346, 1199, -10, -1, 180, 0x3000, 65536};
constexpr Anillo kAnilloBurb = {160, 360, 1199, -10, 0, 180, 0x1800, 0xA000};



// ---- Estado de la rueda y de la burbuja (el giro avanza por cuadros de 60 Hz del dibujo)
struct Giro2 {
  float pos = 0;          // posicion mostrada (en casillas)
  int obj = 0;            // casilla elegida
  float c = -1;           // cuadros desde que empezo el giro (< 0: parado)
  float dur = 10, ini = 0, fin = 0;
  float t_ant = -1;       // reloj de la ultima actualizacion (s)
  bool Girando() const { return c >= 0; }
  void Mover(int a, int n, bool vuelta, float cuadros = 10) {   // a = +-1
    if (Girando()) return;
    int nuevo = obj + a;
    if (vuelta) nuevo = (nuevo % n + n) % n;
    else if (nuevo < 0 || nuevo >= n) return;
    ini = pos;
    fin = pos + float(a);
    obj = nuevo;
    c = 0;
    dur = cuadros;
  }
  void Saltar(int destino, int n, float cuadros = 10) {   // salto directo (por el camino corto en la rueda; recto en la burbuja)
    if (Girando() || destino == obj) return;
    ini = pos;
    fin = float(destino);
    obj = destino;
    c = 0;
    dur = cuadros;
    (void)n;
  }
  void Paso(float dt_cuadros) {
    if (!Girando()) return;
    c += dt_cuadros;
    if (c >= dur) {
      pos = fin;
      c = -1;
    } else {
      pos = ini + (fin - ini) * (c / dur);
    }
  }
  void Fijar(int i) {
    pos = ini = fin = float(i);
    obj = i;
    c = -1;
  }
};
Giro2 g_giro_rueda, g_giro_burb;
float g_t_rueda = 0, g_t_esfera = 0, g_t_menu = 0;   // reloj (s) de la entrada de cada pantalla
int g_sitio_vis = 0;                                  // casilla de la burbuja (0..9; 9 = "?")
int g_menu_sel = 0;           // Normal / Custom / Edit Skills
int g_lista_capsulas = 0;     // FUN_00260CF0: 0 Normal, 1 Custom; Edit Skills abre su pantalla aparte.
float g_c_rueda = 0;          // cuadros desde que entro la rueda (el menu la deja puesta)
bool g_rueda_com = false;     // false: elige el jugador (1P), true: el rival (COM)


// ---- El modelo 3D grande del luchador elegido (A2nave). B2 (FUN_002AE010/002AE9B0, b2.c): cuerpo = #AMB[#AMO,#AMT] de la tabla
// de personajes del ELF (0x389540 + id * 0x2C8: +0x74 cuerpos por traje, +0x114 cabeza/pelo, +0x30/+0x32 cuantos por traje);
// la pose sale del banco fijo DATA_CMN 931 (#AMM, nodo 0x3A3): cuadro 16 * DAT_0037B8B0[id] de su animacion 0 [H: las
// pistas se buscan por nombre de hueso en B2 y aqui por indice]. Luces y camara: tabla 0x37ABD0. Se pinta entre las caras
// lejanas (z > 1500) y las cercanas, como B2 (FUN_002AA460 / FUN_002AA3D0).
struct Fig {
  ps2::Modelo m, cab;
  std::vector<ps2::Tex> tx;
  ps2::Anim an;
  std::string prefijo;   // B3M: prefijo de los huesos (GOK, VGT...)
  std::vector<std::string> suf;   // B3M: sufijo del nombre de cada hueso del cuerpo (GOK_RARM1 -> RARM1), para pasar la animacion a un modelo de B3
  float alto = 0, cuadro = 0;
  int hueso = -1;
  bool ok = false;
};
std::map<int, Fig> g_figs;
std::vector<uint8_t> g_amm931;
int g_fig_id = -1;
ImVec2 g_fig_pie(0, 0);
float g_fig_px = 0, g_fig_alto_ref = 0;
const uint8_t kPose37B8B0[40] = {1, 1, 0, 3, 3, 0, 2, 1, 0, 3, 2, 2, 3, 1, 1, 5, 2, 3, 4, 1, 1, 2, 1, 1, 1, 3, 0, 3, 1, 1, 1, 1, 1, 3, 1, 1, 6, 1, 1, 0};
// prueba DBZ3_SEL_REL: 0 absoluto (por defecto), 1 giro relativo al reposo, 2 giro y posicion
int modo_rel() { static const int m = std::getenv("DBZ3_SEL_REL") ? std::atoi(std::getenv("DBZ3_SEL_REL")) : 0; return m; }
std::vector<ps2::M34> EjesMundo(const ps2::Esqueleto& esq, const ps2::Anim& an, float t) {
  const size_t nb = esq.padre.size();
  static const char* fija = std::getenv("DBZ3_SEL_POSEFRAC");   // prueba: fotograma fijo (fraccion de la animacion)
  const float fc = fija ? std::atof(fija) * float(std::max(an.cuadros - 1, 1)) : std::fmod(t * 60.0f, float(std::max(an.cuadros, 1)));
  std::vector<ps2::M34> w(nb);
  for (size_t i = 0; i < nb; ++i) {
    ps2::M34 l = esq.local[i];
    if (i < an.ang.size() && !an.ang[i].empty()) {
      const auto& ks = an.ang[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && float(ks[ia + 1][0]) <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float fa = float(ks[ia][0]), fb = float(ks[ib][0]);
      const float u = fb > fa ? std::clamp((fc - fa) / (fb - fa), 0.0f, 1.0f) : 0.0f;
      auto ang = [&](int e) {
        const int d = int(int16_t(uint16_t(ks[ib][size_t(e)] - ks[ia][size_t(e)])));
        return (float(ks[ia][size_t(e)]) + u * float(d)) * 9.5873799e-5f;
      };
      const float x = ang(1), y = ang(2), z = ang(3);
      const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
      const float r[9] = {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, -sy, cy * sx, cy * cx};
      if (an.relativa && modo_rel() >= 1) {   // B2: giro de reposo por el de la clave (como Posar)
        const ps2::M34& rep = esq.local[i];
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = rep.m[f * 4] * r[k] + rep.m[f * 4 + 1] * r[3 + k] + rep.m[f * 4 + 2] * r[6 + k];
      } else {
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = r[f * 3 + k];
      }
    }
    if (i < an.pos.size() && !an.pos[i].empty()) {
      const auto& ks = an.pos[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && ks[ia + 1][0] <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float u = ks[ib][0] > ks[ia][0] ? std::clamp((fc - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0.0f, 1.0f) : 0.0f;
      float c[4];
      for (int e = 1; e < 4; ++e) c[e] = ks[ia][size_t(e)] + u * (ks[ib][size_t(e)] - ks[ia][size_t(e)]);
      if (an.relativa && modo_rel() >= 2) l.m[3] = esq.local[i].m[3] + c[1], l.m[7] = esq.local[i].m[7] + c[2], l.m[11] = esq.local[i].m[11] + c[3];
      else l.m[3] = c[1], l.m[7] = c[2], l.m[11] = c[3];
    }
    w[i] = esq.padre[i] >= 0 ? w[size_t(esq.padre[i])] * l : l;
  }
  return w;
}
// pbank/panim: banco #AMM de pose propio (panel comic: CMN 844 Saibaman anim 0, CMN 827 Goku anim 4; medido en PCSX2 en p2, modelo+0x45c / +0x41c);
// 0 = el banco de la rueda (CMN 931, anim por personaje).
const Fig& FigDe(int id_b2, int traje = 0, int pbank = 0, int panim = 0) {   // traje = `forma` de FUN_002AE9B0 (el Saibaman: su color)
  const int clave = id_b2 + 100 * traje + 10000 * pbank + 100000000 * panim;
  auto it = g_figs.find(clave);
  if (it != g_figs.end()) return it->second;
  Fig& f = g_figs[clave];
  const std::vector<uint8_t> elf = ElfB2();
  AfsPs2 cmn;
  if (elf.size() < 0x200000 || id_b2 < 0 || id_b2 > 40 || !cmn.Abrir(g_iso, "DATA_CMN.AFS")) return f;
  const size_t rec = 0x389540 - 0x100000 + 0x80 + size_t(id_b2) * 0x2C8;   // VA 0x100000 = offset 0x80 del ELF
  if (rec + 0x2C8 > elf.size()) return f;
  const int nc = int(elf[rec + 0x30]) | int(elf[rec + 0x31]) << 8, nh = int(elf[rec + 0x32]) | int(elf[rec + 0x33]) << 8;
  if (traje < 0 || (traje > 0 && (nc <= 0 || rec + 0x114 + 4 * size_t(traje * std::max(nh, 1)) + 4 > elf.size()))) return f;
  const uint32_t cuerpo = U32(elf, rec + 0x74 + 4 * size_t(traje * nc)), cabeza = U32(elf, rec + 0x114 + 4 * size_t(traje * nh));
  const bool con_cab = int8_t(elf[rec + 0x25B]) >= 0 && nh > 0 && cabeza > 0;
  if (cuerpo == 0) return f;
  const std::vector<uint8_t> amb = cmn.Entrada(cuerpo);
  f.m = ps2::CargarSuelto(HijoAmb(amb, 0), HijoAmb(amb, 1), true, true, true);
  if (!f.m.malla || !f.m.esq) return f;
  if (g_amm931.empty()) g_amm931 = cmn.Entrada(931);
  const std::vector<uint8_t> amm_pb = pbank > 0 ? cmn.Entrada(uint32_t(pbank)) : std::vector<uint8_t>();
  const std::vector<uint8_t>& amm = pbank > 0 ? amm_pb : g_amm931;
  {   // pose por NOMBRE de hueso, como B2 (FUN_0013F370: se compara el nombre desde el final hasta el "_"): la animacion
    // DAT_0037B8B0[id] de DATA_CMN 931 (7 animaciones sobre los 47 huesos FRZ_*) se aplica a los huesos del modelo con el mismo sufijo
    auto nombres = [](const std::vector<uint8_t>& b, size_t ini, size_t n) {
      std::vector<std::string> v;
      for (size_t i = 0; i < n && ini + 32 * (i + 1) <= b.size(); ++i) v.emplace_back(reinterpret_cast<const char*>(&b[ini + 32 * i]), strnlen(reinterpret_cast<const char*>(&b[ini + 32 * i]), 32));
      return v;
    };
    auto sufijo = [](const std::string& n) { const size_t k = n.rfind('_'); return k == std::string::npos ? n : n.substr(k + 1); };
    const std::vector<uint8_t> amo = HijoAmb(amb, 0);
    size_t ini = 0;
    for (size_t k = 0; k + 6 < amo.size(); ++k)
      if (std::memcmp(&amo[k], "_BODY", 5) == 0 && (amo[k + 5] == 0 || (amo[k + 5] == '_' && amo[k + 6] == '1' && amo[k + 7] == 0))) {   // B3M: Gohan = XGHL_BODY_1
        ini = k;
        while (ini > 0 && (std::isupper(amo[ini - 1]) || std::isdigit(amo[ini - 1]) || amo[ini - 1] == '_')) --ini;
        break;
      }
    const size_t nb = f.m.esq->padre.size();
    const uint32_t nt = amm.size() > 0x20 ? U32(amm, 0x18) : 0, off_n = amm.size() > 0x20 ? U32(amm, 0x1C) : 0;
    static const char* forzar = std::getenv("DBZ3_SEL_POSEIDX");   // prueba: misma animacion para todos
    const uint32_t pose = pbank > 0 ? uint32_t(panim) : forzar ? uint32_t(std::atoi(forzar)) : kPose37B8B0[std::clamp(id_b2, 0, 39)];
    const ps2::Anim base = ps2::LeerAnim(amm, pose, nt);
    REXLOG_INFO("dbz3 nave: pose id B2 {} anim {} cuadros {}", id_b2, pose, base.cuadros);
    if (ini > 0 && nt > 0 && base) {
      const std::vector<std::string> mn = nombres(amo, ini, nb), an = nombres(amm, off_n, nt);
      f.an.cuadros = base.cuadros;
      f.an.relativa = true;   // piel de B2 (ps2_modelo.h, frente R2)
      f.an.absoluta = true;   // MEDIDO en PCSX2: los giros de CMN 931 son absolutos (error 0.000 en los 28 huesos con pista), no sobre el reposo
      f.an.ang.resize(nb);
      f.an.pos.resize(nb);
      if (mn.size() > 1) { const size_t u = mn[1].find('_'); f.prefijo = u == std::string::npos ? mn[1] : mn[1].substr(0, u); }
      for (size_t j = 0; j < nb && j < mn.size(); ++j) {   // B3M: el sufijo de vista_b2.py (tras el primer '_' sin la X: GHL_BODY_1 -> BODY_1)
        const size_t u = mn[j].find('_');
        f.suf.push_back(u == std::string::npos ? mn[j] : mn[j].substr(u + 1));
      }
      int hechos = 0;
      for (size_t j = 0; j < nb && j < mn.size(); ++j)
        for (size_t t = 0; t < an.size(); ++t)
          if (!mn[j].empty() && sufijo(mn[j]) == sufijo(an[t])) {
            f.an.ang[j] = base.ang[t];
            f.an.pos[j] = base.pos[t];
            ++hechos;
            break;
          }
      REXLOG_INFO("dbz3 nave: pose de la rueda id B2 {}: animacion {} de CMN 931, {} de {} huesos con pista por nombre", id_b2, pose, hechos, nb);
    }
    f.cuadro = 0;
  }
  for (const auto& tr : f.m.malla.tris)
    for (const auto& v : tr.p) f.alto = std::max(f.alto, v.y);
  f.tx = ps2::Texturas(f.m);
  if (con_cab) {
    const std::vector<uint8_t> ac = cmn.Entrada(cabeza);
    f.cab = ps2::CargarSuelto(HijoAmb(ac, 0), HijoAmb(ac, 1), true, true, true);
    if (f.cab.esq)
      for (auto& x : ps2::Texturas(f.cab)) f.tx.push_back(std::move(x));
    const auto& esq = *f.m.esq;
    std::vector<ps2::M34> wb(esq.padre.size());
    float ymax = -1e9f;
    for (size_t b = 0; b < esq.padre.size(); ++b) {
      wb[b] = esq.padre[b] >= 0 ? wb[size_t(esq.padre[b])] * esq.local[b] : esq.local[b];
      if (wb[b].m[7] > ymax) ymax = wb[b].m[7], f.hueso = int(b);
    }
    if (f.hueso >= 0 && esq.padre[size_t(f.hueso)] >= 0) f.hueso = esq.padre[size_t(f.hueso)];
  }
  f.ok = true;
  REXLOG_INFO("dbz3 nave: modelo de la rueda: id B2 {} cuerpo {} ({} tri, {} huesos, alto {:.1f}) cabeza {} pose cuadro {}", id_b2, cuerpo,
              f.m.malla.tris.size(), f.m.esq->padre.size(), f.alto, con_cab ? int(cabeza) : -1, int(f.cuadro));
  return f;
}
// factor de tamano medido contra B2 (barrido de las 29 casillas de la pagina 1); el resto 0,93 [H]
float FacDe(int id) {
  switch (id) {
    case 0: return 0.78f;
    case 2: return 0.77f;
    case 28: return 0.99f;
    case 24: return 0.94f;
    case 33: return 0.93f;
    case 6: return 0.90f;
    case 4: return 1.00f;
    case 8: return 1.05f;
    case 7: return 0.89f;
    case 21: return 0.89f;
    case 35: return 0.91f;
    case 32: return 0.92f;
    case 29: return 0.95f;
    case 30: return 0.90f;
    case 31: return 0.94f;
    case 25: return 0.85f;
    case 20: return 0.86f;
    case 18: return 0.93f;
    case 17: return 0.91f;
    case 16: return 0.98f;
    case 15: return 0.70f;
    case 11: return 0.90f;
    case 12: return 1.01f;
    case 9: return 1.00f;
    case 10: return 0.97f;
    case 3: return 0.92f;
    case 5: return 1.02f;
    case 27: return 0.87f;
    case 26: return 0.77f;
  }
  return 0.93f;
}

// ---- Modelo 3D de B3 HD de los luchadores sin registro de B2 (extras, puertos, mods): malla HMV1 de vista_nave.py (la hace en
// segundo plano con el data_cmn.afs del jugador; nada se reparte), pintada con ps2::Escena como la vista previa del Heroe.
// borde toon de una figura (fraccion de su alto): el de B2/IW (0,9 %) o, con el modelo de B3, el fino de B3 HD
float BordeFig(int estado_b3) {
  static const float b3 = std::getenv("DBZ3_FIG_BORDE_B3") ? float(std::atof(std::getenv("DBZ3_FIG_BORDE_B3"))) : 0.005f;
  return estado_b3 == 2 ? b3 : 0.009f;
}
struct ModB3 {
  ps2::Malla malla;
  std::vector<ps2::Tex> tx;
  float alto = 0;
  std::vector<std::vector<std::pair<float, int>>> rampa;   // "RMP1" tras los triangulos: tramos de la rampa de B3 por textura
};
std::map<int, ModB3> g_modb3;
std::set<int> g_modb3_pidiendo, g_modb3_fallo;
std::mutex g_modb3_mu;
int g_fig_b3 = -1;
std::filesystem::path RutaVistaNave(int id) { return dbz3::ModsRoot().parent_path() / "ingame_ui" / "nave_vista" / (std::to_string(id) + ".malla"); }
// fid (entrada de data_cmn) del traje 0 de un luchador: tabla char96 0x8234ABB8 + 96 * ID (+18 modelo de la forma 0, +8 lista de fids)
int FidB3(int id, int traje = 0, int forma = 0) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base || id < 0 || id >= 105) return -1;
  const dbz3::GA tab = dbz3::GA(0x8234ABB8) + 96u * uint32_t(id);
  const uint32_t ptr = REX_LOAD_U32(tab + 8);
  const uint32_t ntr = REX_LOAD_U8(tab + 4), nmod = std::max<uint32_t>(REX_LOAD_U8(tab + 5), 1);
  uint32_t md = REX_LOAD_U8(tab + 18);
  if (forma > 0 && forma < 8) {   // B3M: la forma k (8 B desde +16: u16 ID, u8 modelo dentro del traje)
    const uint32_t m = REX_LOAD_U8(tab + 16 + 8 * uint32_t(forma) + 2);
    if (m < nmod) md = m;
  }
  if (ptr < 0x80000000u || ptr > 0xA0000000u) return -1;
  const uint32_t t = (traje > 0 && uint32_t(traje) < ntr) ? uint32_t(traje) : 0;   // B3M: el traje de B2 en el de B3 con el mismo numero (si no, el 0)
  const uint32_t fid = REX_LOAD_U32(ptr + 12 * (t * nmod + md));   // la lista es de 12 B por modelo: fid, puntero, 0 (medido en la imagen del guest)
  return fid < 100000 ? int(fid) : -1;
}
// ID de B3 de la forma (u16 en +16 + 8 * forma): su pose del select
int IdFormaB3(int id, int forma) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base || id < 0 || id >= 105) return id;
  const dbz3::GA tab = dbz3::GA(0x8234ABB8) + 96u * uint32_t(id);
  const uint32_t v = forma > 0 && forma < 8 ? uint32_t(REX_LOAD_U16(tab + 16 + 8 * uint32_t(forma))) : uint32_t(id);
  return v < 105 ? int(v) : id;
}
// forma de B3 que enseña B2 en sus figuras grandes (como el tablero: Cell perfecto y Freezer en su forma final)
int FormaFigB3(int b3) { return b3 == 33 ? 2 : b3 == 27 ? 3 : 0; }
bool LeerMalla3(const std::filesystem::path& f, ModB3& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 4;
  auto u32 = [&](uint32_t& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  auto f32 = [&](float& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  if (d.size() < 16 || std::memcmp(d.data(), "HMV1", 4) != 0) return false;
  uint32_t ntex = 0, ntri = 0;
  float alto = 0;
  if (!u32(ntex) || !u32(ntri) || !f32(alto) || ntex > 256 || ntri > 200000) return false;
  std::vector<pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  ps2::Malla malla;
  malla.tris.resize(ntri);
  for (auto& t : malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      for (float& x : v) if (!f32(x)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.malla = std::move(malla);
  m.rampa.clear();   // 25/10: "RMP1" (vista_heroe.exportar_malla): tramos de la rampa de B3 por textura
  uint32_t n_orig = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "RMP1", 4) == 0 && (o += 4, u32(n_orig)) && n_orig <= ntex) {
    m.rampa.resize(n_orig);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t n = 0;
      if (!u32(n) || n > 8) { bien = false; break; }
      for (uint32_t k = 0; k < n && bien; ++k) {
        float l0 = 0;
        uint32_t ti = 0;
        if (!f32(l0) || !u32(ti) || ti >= ntex) bien = false;
        else fila.emplace_back(l0, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(ps2::Crear(im, false));
  m.alto = alto;
  return alto > 0 && !m.malla.tris.empty();
}
#if defined(_WIN32)
bool LanzarOculto(const std::wstring& cmd, DWORD* code) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
  *code = 1;
  if (WaitForSingleObject(pi.hProcess, 180000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
#endif
// Lanza un script de mod center hd/cac con el Python del juego (oculto, tope 180 s). true si salio con 0.
bool CorrerVista(const char* script_nombre, const std::wstring& args) {
#if defined(_WIN32)
  namespace fs = std::filesystem;
  const fs::path juego = dbz3::ModsRoot().parent_path(), script = juego / "mod center hd" / "cac" / script_nombre;
  std::error_code ec;
  if (!fs::exists(script, ec)) return false;
  std::vector<std::wstring> pys;
  if (const char* e = std::getenv("DBZ3_PYTHON"); e && *e) pys.push_back(L"\"" + fs::path(e).wstring() + L"\"");
  if (fs::exists(juego / "python" / "python.exe", ec)) pys.push_back(L"\"" + (juego / "python" / "python.exe").wstring() + L"\"");
  pys.push_back(L"py -3");
  pys.push_back(L"python");
  for (const std::wstring& py : pys) {
    const std::wstring cmd = py + L" -I -X utf8 \"" + script.wstring() + L"\" --juego \"" + juego.wstring() + L"\" " + args;
    DWORD code = 1;
    if (!LanzarOculto(cmd, &code) || code == 9009) continue;
    return code == 0;
  }
#else
  (void)script_nombre; (void)args;
#endif
  return false;
}
void GenerarModB3(int id, int fid) {
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_modb3_pidiendo.count(id) || g_modb3_fallo.count(id)) return;
    g_modb3_pidiendo.insert(id);
  }
  std::thread([id, fid] {
    std::error_code ec;
    const bool ok = CorrerVista("vista_nave.py", L"--id " + std::to_wstring(id) + L" --fid " + std::to_wstring(fid)) && std::filesystem::exists(RutaVistaNave(id), ec);
    REXLOG_INFO("dbz3 nave: modelo 3D de B3 id {} (fid {}) {}", id, fid, ok ? "generado" : "no se pudo generar (se queda la cara grande)");
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_pidiendo.erase(id);
    if (!ok) g_modb3_fallo.insert(id);
  }).detach();
}
// hilo de dibujo: el modelo si ya esta; si no, lo pide (una vez) y devuelve null
const ModB3* ModeloB3(int id) {
  if (auto it = g_modb3.find(id); it != g_modb3.end()) return it->second.alto > 0 ? &it->second : nullptr;
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_modb3_pidiendo.count(id) || g_modb3_fallo.count(id)) return nullptr;
  }
  std::error_code ec;
  if (std::filesystem::exists(RutaVistaNave(id), ec)) {
    ModB3& m = g_modb3[id];
    if (LeerMalla3(RutaVistaNave(id), m)) {
      REXLOG_INFO("dbz3 nave: modelo 3D de B3 id {} cargado ({} triangulos, alto {:.2f})", id, m.malla.tris.size(), m.alto);
      return &m;
    }
    m = ModB3{};
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_fallo.insert(id);
    return nullptr;
  }
  const int fid = FidB3(id);
  if (fid < 0) {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_modb3_fallo.insert(id);
    return nullptr;
  }
  GenerarModB3(id, fid);
  return nullptr;
}
// ---- B3M (25/10): el modelo de B3 CON HUESOS para las figuras grandes de B2 (vineta del tablero y rueda de la nave).
// vista_b2.py lo exporta de data_cmn.afs del jugador (HMS1: malla en el espacio de su hueso, piel rigida como model_render + el
// esqueleto de reposo). La animacion es la de B2 (banco #AMM del original) pasada hueso a hueso por el SUFIJO del nombre
// (GOK_RARM1 -> RARM1; medido: el reposo de B2 y B3 coincide en 26 de 29 luchadores, ver B3M/INFORME.md); los huesos de B3 sin
// hueso de B2 (pelo, cara) heredan del padre. La posicion de la cadera (pista) se escala por el largo de pierna B3/B2.
struct SkB3 {
  ps2::Malla malla;   // p / n en el espacio del hueso (Tri::hueso)
  std::vector<ps2::Tex> tx;
  std::vector<std::string> suf;
  ps2::Esqueleto esq;   // padre + local (reposo; con el giro del select nativo de B3 en los huesos que lo traen)
  std::vector<ps2::M34> reposo;   // local sin el giro del select (para el largo de pierna)
  std::vector<pasarela::Imagen> imgs;   // las texturas en CPU (para recolorear)
  float alto = 0;
  std::vector<std::vector<std::pair<float, int>>> rampa;   // HMS3: tramos de la rampa de B3 por textura (Escena::Toon::rampa)
};
const std::vector<std::vector<std::pair<float, int>>>* g_rampa_fig = nullptr;   // la de la ultima FiguraB3 lista
std::map<int, SkB3> g_sk;   // por fid
std::set<int> g_sk_pidiendo, g_sk_fallo;
std::filesystem::path RutaSk(int fid) { return dbz3::ModsRoot().parent_path() / "ingame_ui" / "nave_vista" / ("sk_" + std::to_string(fid) + ".sk"); }
bool LeerSk(const std::filesystem::path& f, SkB3& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 4;
  auto u32 = [&](uint32_t& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  auto f32 = [&](float& v) { if (o + 4 > d.size()) return false; std::memcpy(&v, d.data() + o, 4); o += 4; return true; };
  const bool hms3 = d.size() >= 4 && std::memcmp(d.data(), "HMS3", 4) == 0;
  if (d.size() < 20 || (!hms3 && std::memcmp(d.data(), "HMS2", 4) != 0)) return false;
  uint32_t ntex = 0, ntri = 0, nb = 0;
  float alto = 0;
  if (!u32(ntex) || !u32(ntri) || !u32(nb) || !f32(alto) || ntex > 256 || ntri > 200000 || nb == 0 || nb > 256) return false;
  std::vector<pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  m.esq = ps2::Esqueleto{};
  m.suf.clear();
  m.reposo.clear();
  for (uint32_t i = 0; i < nb; ++i) {
    uint32_t pa = 0;
    if (!u32(pa) || o + 24 + 48 > d.size()) return false;
    m.esq.padre.push_back(int(int32_t(pa)));
    m.suf.emplace_back(reinterpret_cast<const char*>(d.data() + o), strnlen(reinterpret_cast<const char*>(d.data() + o), 24));
    o += 24;
    ps2::M34 l;
    for (float& x : l.m) if (!f32(x)) return false;
    m.reposo.push_back(l);
    uint32_t con = 0;
    float g[9];
    if (!u32(con)) return false;
    for (float& x : g) if (!f32(x)) return false;
    if (con) {   // B3M: el giro local del select nativo de B3 (cola, capa, pelo: lo que B2 no anima)
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) l.m[r * 4 + c] = g[r * 3 + c];
    }
    m.esq.local.push_back(l);
  }
  m.malla = ps2::Malla{};
  m.malla.tris.resize(ntri);
  for (auto& t : m.malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      uint32_t h = 0;
      for (float& x : v) if (!f32(x)) return false;
      if (!u32(h)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
      t.hueso[c] = uint8_t(std::min<uint32_t>(h, nb - 1));
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.rampa.clear();
  uint32_t n_orig = 0;
  if (hms3 && u32(n_orig) && n_orig <= ntex) {
    m.rampa.resize(n_orig);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t n = 0;
      if (!u32(n) || n > 8) { bien = false; break; }
      for (uint32_t k = 0; k < n && bien; ++k) {
        float l0 = 0;
        uint32_t ti = 0;
        if (!f32(l0) || !u32(ti) || ti >= ntex) bien = false;
        else fila.emplace_back(l0, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(ps2::Crear(im, false));
  m.imgs = std::move(imgs);
  m.alto = alto;
  return alto > 0 && !m.malla.tris.empty();
}
// hilo de dibujo. 0 = no hay (sin B3 / fallo), 1 = generandose, 2 = listo (`*sk`)
int SkEstado(int fid, const SkB3** sk, int cid = -1) {
  if (fid < 0 || std::getenv("DBZ3_SEL_SIN_SK")) { static bool d = false; if (!d) { d = true; REXLOG_INFO("dbz3 nave: B3 con huesos: sin fid ({})", fid); } return 0; }
  if (auto it = g_sk.find(fid); it != g_sk.end()) {
    if (it->second.alto > 0) { *sk = &it->second; return 2; }
    return 0;
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    if (g_sk_fallo.count(fid)) return 0;
    if (g_sk_pidiendo.count(fid)) return 1;
  }
  std::error_code ec;
  if (std::filesystem::exists(RutaSk(fid), ec)) {
    SkB3& m = g_sk[fid];
    if (LeerSk(RutaSk(fid), m)) {
      REXLOG_INFO("dbz3 nave: modelo B3 con huesos fid {} cargado ({} triangulos, {} huesos, alto {:.2f})", fid, m.malla.tris.size(), m.suf.size(), m.alto);
      *sk = &m;
      return 2;
    }
    REXLOG_INFO("dbz3 nave: B3 con huesos fid {}: el fichero no se pudo leer", fid);
    m = SkB3{};
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_fallo.insert(fid);
    return 0;
  }
  {
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_pidiendo.insert(fid);
  }
  std::thread([fid, cid] {
    std::error_code e2;
    const bool ok = CorrerVista("vista_b2.py", L"--fid " + std::to_wstring(fid) + (cid >= 0 ? L" --id " + std::to_wstring(cid) : L"")) && std::filesystem::exists(RutaSk(fid), e2);
    REXLOG_INFO("dbz3 nave: modelo B3 con huesos fid {} {}", fid, ok ? "generado" : "no se pudo generar");
    std::lock_guard<std::mutex> lk(g_modb3_mu);
    g_sk_pidiendo.erase(fid);
    if (!ok) g_sk_fallo.insert(fid);
  }).detach();
  return 1;
}
// largo de pierna en reposo (cadera -> punta del pie) de un esqueleto, por sufijos; 0 si falta algun hueso
float LargoPierna(const std::vector<int>& padre, const std::vector<ps2::M34>& local, const std::vector<std::string>& suf) {
  std::vector<ps2::M34> w(padre.size());
  int cad = -1, pie = -1;
  for (size_t b = 0; b < padre.size(); ++b) {
    w[b] = padre[b] >= 0 ? w[size_t(padre[b])] * local[b] : local[b];
    if (b < suf.size()) {
      if (suf[b] == "WAIST") cad = int(b);
      else if (suf[b] == "NLF" || (pie < 0 && suf[b] == "LFOOT1")) pie = int(b);
    }
  }
  return cad >= 0 && pie >= 0 ? w[size_t(cad)].m[7] - w[size_t(pie)].m[7] : 0.0f;
}
// la animacion de B2 de `f` en los huesos de `sk` (cache por pareja)
const ps2::Anim& AnimSk(const SkB3& sk, const Fig& f) {
  static std::map<std::pair<const SkB3*, const Fig*>, ps2::Anim> cache;
  const auto key = std::make_pair(&sk, &f);
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  ps2::Anim& a = cache[key];
  if (f.suf.empty() || !f.an || f.suf.size() != f.an.ang.size()) return a;
  const float l2 = LargoPierna(f.m.esq->padre, f.m.esq->local, f.suf), l3 = LargoPierna(sk.esq.padre, sk.reposo, sk.suf);
  const float k = (l2 > 0.5f && l3 > 0.5f) ? std::clamp(l3 / l2, 0.5f, 2.0f) : 1.0f;
  a.cuadros = f.an.cuadros;
  a.relativa = f.an.relativa;
  a.absoluta = f.an.absoluta;
  a.ang.resize(sk.suf.size());
  a.pos.resize(sk.suf.size());
  int hechos = 0;
  for (size_t j = 0; j < sk.suf.size(); ++j)
    for (size_t i = 0; i < f.suf.size(); ++i)
      if (sk.suf[j] == f.suf[i]) {
        a.ang[j] = f.an.ang[i];
        a.pos[j] = f.an.pos[i];
        for (auto& c : a.pos[j]) c[1] *= k, c[2] *= k, c[3] *= k;
        ++hechos;
        break;
      }
  REXLOG_INFO("dbz3 nave: B3 con huesos: {} de {} huesos de B3 con pista de B2 ({} huesos), escala de cadera {:.3f}", hechos, sk.suf.size(), f.suf.size(), k);
  return a;
}
// Malla de B3 posada con la animacion de B2 de `f` en el segundo `t`; mismo marco que la de B2 (Posar: x/z menos el centro de
// la malla de B2). false si no se puede (sin pistas): se queda el modelo de B2.
bool PosarSk(const SkB3& sk, const Fig& f, float t, ps2::Malla& out) {
  const ps2::Anim& a = AnimSk(sk, f);
  if (!a) return false;
  const std::vector<ps2::M34> w = EjesMundo(sk.esq, a, t);
  static const bool sin_centro = std::getenv("DBZ3_SEL_SK_SINCENTRO") != nullptr;   // prueba: B3 en coordenadas crudas del modelo (como el juego)
  const ps2::V3 c = (f.m.esq && !sin_centro) ? f.m.esq->centro : ps2::V3{};
  { static bool dicho = false; if (!dicho && f.m.esq) { dicho = true; REXLOG_INFO("dbz3 nave: B3 con huesos: centro de la malla de B2 ({:.3f}, {:.3f})", f.m.esq->centro.x, f.m.esq->centro.z); } }
  out.tris = sk.malla.tris;
  for (auto& tr : out.tris)
    for (int k = 0; k < 3; ++k) {
      const ps2::M34& W = w[tr.hueso[k]];
      ps2::V3 p = W.por(tr.p[k]);
      tr.p[k] = {p.x - c.x, p.y, p.z - c.z};
      tr.n[k] = W.gira(tr.n[k]);
    }
  return true;
}
// ---- B3M: colores del Saibaman. B2 trae 5 (modo = traje: 0 verde, 1 salmon, 2 azul, 3 amarillo, 4 rosa) y B3 solo 2 (los modos 0 y 1,
// por traje); los otros 3 se recolorean desde el verde de B3 con el color medio de la piel (texturas 0, 2, 5 del cuerpo de B2) y de la
// armadura (8, 9) de ese traje de B2. ponytail: indices de textura medidos (B3M/INFORME.md) solo para el Saibaman (id 22).
struct Hsv { float h, s, v; };
Hsv HsvDe(float r, float g, float b) {   // r, g, b en 0..1; h en grados
  const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  float h = 0;
  if (d > 1e-6f) h = mx == r ? 60.0f * std::fmod((g - b) / d + 6.0f, 6.0f) : mx == g ? 60.0f * ((b - r) / d + 2.0f) : 60.0f * ((r - g) / d + 4.0f);
  return {h, mx > 0 ? d / mx : 0.0f, mx};
}
void RgbDe(Hsv c, float& r, float& g, float& b) {
  const float hh = std::fmod(c.h + 720.0f, 360.0f) / 60.0f, ch = c.v * c.s, x = ch * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f)), m = c.v - ch;
  float rr = 0, gg = 0, bb = 0;
  const int k = int(hh);
  if (k == 0) rr = ch, gg = x; else if (k == 1) rr = x, gg = ch; else if (k == 2) gg = ch, bb = x; else if (k == 3) gg = x, bb = ch; else if (k == 4) rr = x, bb = ch; else rr = ch, bb = x;
  r = rr + m; g = gg + m; b = bb + m;
}
Hsv MediaHsv(const std::vector<pasarela::Imagen>& tex, std::initializer_list<size_t> idx) {
  double r = 0, g = 0, b = 0, n = 0;
  for (size_t i : idx) {
    if (i >= tex.size()) continue;
    const auto& im = tex[i];
    for (size_t k = 0; k + 3 < im.rgba.size(); k += 4)
      if (im.rgba[k + 3] > 128) r += im.rgba[k], g += im.rgba[k + 1], b += im.rgba[k + 2], n += 1;
  }
  return n > 0 ? HsvDe(float(r / n / 255.0), float(g / n / 255.0), float(b / n / 255.0)) : Hsv{0, 0, 1};
}
// copia de `base` recoloreada al traje `modo` de B2 (f = el Saibaman de B2 de ese traje); cache por (modo)
const SkB3* SkTinte(const SkB3& base, const Fig& f, int modo) {
  static std::map<std::pair<const SkB3*, int>, SkB3> cache;
  const auto key = std::make_pair(&base, modo);
  if (auto it = cache.find(key); it != cache.end()) return &it->second;
  SkB3& o = cache[key];
  o.malla = base.malla;   // las texturas de GPU no se copian (unique_ptr): se rehacen abajo
  o.suf = base.suf;
  o.esq = base.esq;
  o.reposo = base.reposo;
  o.alto = base.alto;
  const Hsv piel2 = MediaHsv(f.m.tex, {0, 2, 5}), arma2 = MediaHsv(f.m.tex, {8, 9});
  const Hsv piel3{65.0f, 0.63f, 0.81f}, arma3{80.0f, 0.87f, 0.45f};   // medidos en el verde de B3 (fid 371)
  o.imgs = base.imgs;
  for (auto& im : o.imgs) {
    for (size_t k = 0; k + 3 < im.rgba.size(); k += 4) {
      if (im.rgba[k + 3] < 8) continue;
      Hsv c = HsvDe(im.rgba[k] / 255.0f, im.rgba[k + 1] / 255.0f, im.rgba[k + 2] / 255.0f);
      if (c.s < 0.12f) continue;
      const bool piel = c.h >= 45.0f && c.h < 72.0f, arma = c.h >= 72.0f && c.h < 110.0f;
      if (!piel && !arma) continue;
      const Hsv& t = piel ? piel2 : arma2;
      const Hsv& b = piel ? piel3 : arma3;
      c.h += t.h - b.h;
      c.s = std::clamp(c.s * (t.s / std::max(b.s, 0.05f)), 0.0f, 1.0f);
      c.v = std::clamp(c.v * (t.v / std::max(b.v, 0.05f)), 0.0f, 1.0f);
      float r, g, bl;
      RgbDe(c, r, g, bl);
      im.rgba[k] = uint8_t(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
      im.rgba[k + 1] = uint8_t(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
      im.rgba[k + 2] = uint8_t(std::clamp(bl * 255.0f + 0.5f, 0.0f, 255.0f));
    }
    o.tx.push_back(ps2::Crear(im, false));
  }
  REXLOG_INFO("dbz3 nave: Saibaman B3 recoloreado al modo {} de B2 (piel h {:.0f} s {:.2f} v {:.2f}, armadura h {:.0f} s {:.2f} v {:.2f})", modo, piel2.h, piel2.s, piel2.v, arma2.h, arma2.s, arma2.v);
  return &o;
}
// 0 = no aplica (se queda el modelo de B2), 1 = el de B3 se esta generando (no se dibuja nada), 2 = `m` / `tx` / `alto` son los de B3
int FiguraB3(int id_b2, int traje, const Fig& f, float t, ps2::Malla& m, const std::vector<ps2::Tex>*& tx, float& alto) {
  const int b3 = B3DeB2(id_b2);
  if (b3 < 0 || !f.ok || std::getenv("DBZ3_SEL_FORZAR_B2")) return 0;
  const SkB3* sk = nullptr;
  const int e = SkEstado(FidB3(b3, traje, FormaFigB3(b3)), &sk, IdFormaB3(b3, FormaFigB3(b3)));
  if (e != 2) return e;
  if (id_b2 == 22 && traje >= 2 && traje <= 4) sk = SkTinte(*sk, f, traje);   // B3M: modos azul / amarillo / rosa del Saibaman de B2
  if (!PosarSk(*sk, f, t, m)) return 0;
  tx = &sk->tx;
  alto = sk->alto;
  g_rampa_fig = sk->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? nullptr : &sk->rampa;
  return 2;
}
// alto (ymax - ymin) de los modelos de B2 de la rueda, para dar a un extra el tamano del de altura parecida
struct AltoB2 { int id; float h; };
constexpr AltoB2 kAltosB2[] = {{0, 14.05f}, {2, 10.23f}, {28, 16.85f}, {24, 16.47f}, {33, 13.85f}, {6, 17.12f}, {4, 12.14f}, {8, 16.22f}, {7, 15.37f}, {21, 17.30f}, {35, 13.80f},
                               {32, 18.36f}, {29, 15.67f}, {30, 19.05f}, {31, 13.86f}, {25, 16.61f}, {20, 20.67f}, {18, 15.52f}, {17, 15.91f}, {16, 20.05f}, {15, 13.75f},
                               {11, 16.61f}, {12, 21.57f}, {9, 18.30f}, {10, 19.11f}, {3, 12.74f}, {5, 15.27f}, {27, 9.19f}, {26, 8.83f}};
// alto de la malla de B3 (brazos abajo) / alto del modelo de B2: Goku 19,6 / 14,0, Piccolo 20,4 / 17,1, Krillin 13,3 / 12,1, Frieza 16,6 / 13,7
constexpr float kEscalaB3 = 1.2f;
void PintaExtra(ImDrawList* dl) {
  const ModB3* m = ModeloB3(g_fig_b3);
  if (!m) {   // sin modelo (generandose o sin Python / data_cmn): la cara grande de B3 HD
    {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      if (!g_modb3_fallo.count(g_fig_b3)) return;
    }
    if (const ImTextureID tex = CaraDe(g_fig_b3)) {
      const float t = g_fig_px * 0.55f;
      const ImVec2 c(g_fig_pie.x, g_fig_pie.y - g_fig_px * 0.62f);
      dl->AddImage(tex, ImVec2(c.x - t * 0.5f, c.y - t * 0.5f), ImVec2(c.x + t * 0.5f, c.y + t * 0.5f));
    }
    return;
  }
  const float hb2 = m->alto / kEscalaB3;
  // Los extras no tienen equivalente individual en B2: el factor de respaldo medido es 0,93.
  // No inferirlo de los tres modelos de altura mas cercana, porque sesga la escala por el roster.
  std::array<const AltoB2*, 3> cer{};
  for (auto& c : cer) {
    const AltoB2* mejor = nullptr;
    for (const AltoB2& r : kAltosB2) {
      if (std::find(cer.begin(), cer.end(), &r) != cer.end()) continue;
      if (!mejor || std::fabs(r.h - hb2) < std::fabs(mejor->h - hb2)) mejor = &r;
    }
    c = mejor;
  }
  constexpr float fac = 0.93f;
  float href = 0;
  for (const AltoB2* c : cer) href += c->h / 3;
  const AltoB2* ref = cer[0];
  // 1,38: lo que mide en pantalla un modelo de B2 por encima de su altura nominal (cabeza medida contra PCSX2: Goku, Teen Gohan...)
  // tope: la cabeza siempre dentro de la pantalla (los de B2 mas altos, Cell y Frieza, ya salen casi recortados)
  const float alto_px = std::min(1.38f * g_fig_px * fac * hb2 / (href + 1.1f), 0.95f * g_fig_px);
  ps2::Camara cam;
  cam.fov = 0.30f;   // perspectiva suave: las colas y capas de B3 (Cooler) hacia la camara no deben agrandar el cuerpo
  cam.c = g_fig_pie;
  cam.h = ImVec2(450, 450);
  const float fl = 450.0f / std::tan(cam.fov * 0.5f), d = m->alto * fl / std::max(alto_px, 1.0f);
  { static std::set<int> v; if (v.insert(g_fig_b3).second) REXLOG_INFO("dbz3 nave: extra B3 {}: alto {:.2f} hb2 {:.2f} ref B2 {} fac {:.2f} alto_px {:.1f} d {:.2f} g_fig_px {:.1f}", g_fig_b3, m->alto, hb2, ref->id, fac, alto_px, d, g_fig_px); }
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = m->alto * (m->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? 0.009f : BordeFig(2));
  toon.rampa = m->rampa.empty() || std::getenv("DBZ3_FIG_TOON_B2") ? nullptr : &m->rampa;   // 25/10: la rampa de B3
  static const float giro = std::getenv("DBZ3_SEL_GIRO") ? float(std::atof(std::getenv("DBZ3_SEL_GIRO"))) : 0.35f;
  esc.Poner(cam, m->malla, m->tx, {0, 0, 0}, giro, 1.0f, toon);
  esc.Pintar(dl);
}
// ---- A2n: camara real de la rueda de B2. FUN_002AE570: registro 0x37ABD0 + tipo*0x66C + 0x10 + id*0x2C
// (esta rueda usa el bloque base, tipo 0):
// [0]/[1] x (1P/COM), [2] y, [3] desplazamiento vertical, [4] distancia (el original la escala por 8/7);
// la escala final de perspectiva usa [4] en FUN_002AE570. El giro procede de la cabecera de camara [1]/[2].
struct CamRueda { float cx = 0, cy = 0, dy = 0, dz = 0, yaw = 0, F = 0, esc = 1; bool ok = false; };
Lienzo g_fig_L{1, 1, ImVec2(0, 0)};
CamRueda CamDeRueda(int id, bool com) {
  CamRueda r;
  static float F0 = 5863.36f, dcx = 0, dcy = 0, gx = 0, ayx = 1;
  if (static const char* fa = std::getenv("DBZ3_SEL_CAMFILE"); fa) {   // prueba: "F dcx dcy giro_extra" releido cada vez
    if (FILE* fp = std::fopen(fa, "r")) {
      float v[4] = {F0, 0, 0, 0};
      if (std::fscanf(fp, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]) >= 1) { F0 = v[0]; dcx = v[1]; dcy = v[2]; gx = v[3]; }
      std::fclose(fp);
    }
  }
  const std::vector<uint8_t>& elf = ElfB2();
  const size_t base = size_t(0x37ABD0) - 0x100000 + 0x80;
  const size_t row = base + 0x10 + size_t(id) * 0x2C;
  if (id < 0 || id > 0x3E || row + 0x2C > elf.size()) return r;
  auto fl = [&](size_t o) { float v; std::memcpy(&v, elf.data() + o, 4); return v; };
  const uint32_t w = U32(elf, base + 4 + 4 * (com ? 1 : 0));
  r.yaw = float(int16_t(uint16_t(w & 0xFFFF))) * (6.2831853f / 65536.0f) + gx;
  r.cx = fl(row + (com ? 4 : 0)) + dcx;
  r.cy = fl(row + 8) + dcy;
  r.dy = fl(row + 12);
  r.dz = fl(row + 16) * (448.0f / 392.0f);
  r.F = F0;
  {   // escala del personaje: tabla de floats DAT_003892A0[id] (FUN_002AE010 la aplica a la raiz del modelo: FUN_00149E40)
    const size_t o = size_t(0x3892A0) - 0x100000 + 0x80 + 4 * size_t(id);
    if (o + 4 <= elf.size()) { const float e = fl(o); if (e > 0.2f && e < 3.0f) r.esc = e; }
  }
  r.ok = std::isfinite(r.cx) && std::isfinite(r.cy) && r.dz < -1.0f;
  return r;
}
void PintaFig(ImDrawList* dl) {
  if (static const bool sin = std::getenv("DBZ3_SEL_SINMODELO") != nullptr; sin) return;
  if (g_fig_id < 0) {
    if (g_fig_b3 >= 0) PintaExtra(dl);
    return;
  }
  const Fig& f = FigDe(g_fig_id);
  if (!f.ok || f.alto <= 0) return;
  static ps2::Malla pose;
  const bool anima = bool(f.an);
  if (anima) ps2::Posar(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())), pose);
  ps2::Malla m = anima ? pose : f.m.malla;
  const std::vector<ps2::Tex>* txp = &f.tx;
  float alto_f = f.alto;
  const int estado_b3 = anima ? FiguraB3(g_fig_id, 0, f, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())), m, txp, alto_f) : 0;   // B3M
  if (estado_b3 == 1) return;   // el de B3 se esta generando (primer uso)
  float ymin = 1e9f, ymax = -1e9f;
  for (const auto& tr : m.tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y), ymax = std::max(ymax, v.y);
  if (f.cab.esq && f.hueso >= 0 && anima && estado_b3 != 2) {
    const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())));
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
      if (g_fig_id == 0 && tr0.tex == 2) {   // B2G 24/10: la cara de Goku (textura 2) sin el tinte de color: salia naranja y sin blancos de ojos
        tr.con_color = false; for (auto& cc : tr.col) cc = IM_COL32_WHITE; }
      m.tris.push_back(tr);
    }
  }
  { static uint64_t vistos = 0; if (g_fig_id < 64 && !(vistos >> g_fig_id & 1)) { vistos |= 1ull << g_fig_id; REXLOG_INFO("dbz3 nave: figura id B2 {} alto de modelo {:.3f} ymin {:.3f} ymax {:.3f}", g_fig_id, (ymax - ymin + 1.1f), ymin, ymax);
      if (f.m.esq->padre.size() > 1) {   // A2n: largo de pierna en reposo (cadera -> suela) para el reescalado de la pista de la cadera
        std::vector<ps2::M34> rw(f.m.esq->padre.size());
        float mn = 1e9f, mx = -1e9f;
        for (size_t b = 0; b < rw.size(); ++b) { rw[b] = f.m.esq->padre[b] >= 0 ? rw[size_t(f.m.esq->padre[b])] * f.m.esq->local[b] : f.m.esq->local[b]; mn = std::min(mn, rw[b].m[7]); mx = std::max(mx, rw[b].m[7]); }
        REXLOG_INFO("dbz3 nave: figura id B2 {} reposo-mundo y min {:.3f} max {:.3f}", g_fig_id, mn, mx);
      }
      if (anima && f.m.esq->padre.size() > 1) { const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime()))); REXLOG_INFO("dbz3 nave: figura id B2 {} cadera mundo ({:.3f}, {:.3f}, {:.3f}) huesos {} reposo ({:.3f}, {:.3f}, {:.3f}) padre1 {}", g_fig_id, w[1].m[3], w[1].m[7], w[1].m[11], w.size(), f.m.esq->local[1].m[3], f.m.esq->local[1].m[7], f.m.esq->local[1].m[11], f.m.esq->padre[1]); } } }
  if (!std::getenv("DBZ3_SEL_VIEJO")) {   // A2n: la camara REAL de la rueda de B2 (FUN_002AE570): registro 0x37ABD0 + id*0x2C
    const CamRueda cr = CamDeRueda(g_fig_id, g_rueda_com);
    if (cr.ok) {
      const float ay = 448.0f / 392.0f;   // el diseno de B2 es 640 x 392 y la pantalla (GS) 448 filas: SOLO el centro de pantalla (196 + cy) va por ay
      // B3M (25/10): la malla NO se estira en vertical. Medido sobre el original de A2n (Goku, Piccolo, Vegeta; B3M/herramientas/rueda_hip.py):
      // con el estiramiento Goku salia ~25 % mas alto/grande (el "Goku mas grande" de A2n); sin el, la cabeza cae donde el original.
      // DBZ3_SEL_AYMALLA=1 vuelve al estiramiento anterior.
      static const float ay_m = std::getenv("DBZ3_SEL_AYMALLA") ? ay : 1.0f;
      for (auto& tr : m.tris)
        for (auto& v : tr.p) v.y *= ay_m;
      ps2::Camara cam;
      const float fd = cr.F * g_fig_L.s;
      cam.h = ImVec2(450, 450);
      cam.fov = 2.0f * std::atan(450.0f / fd);
      cam.c = g_fig_L.P(320.0f + cr.cx, (196.0f + cr.cy) * ay);
      cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
      cam.m[13] = cr.dy * ay_m;
      cam.m[14] = cr.dz;
      cam.ok = true;
      ps2::Escena esc;
      esc.Empezar();
      ps2::Escena::Toon toon;
      toon.borde = alto_f * BordeFig(estado_b3);
      toon.rampa = estado_b3 == 2 ? g_rampa_fig : nullptr;
      ps2::V3 desp{0, 0, 0};   // la tabla de escala solo multiplica la POSICION de la raiz (medido: cadera x 0,164 -> 0,131 en Vegeta, malla sin escalar)
      if (anima && f.m.esq->padre.size() > 1) {
        const auto w = EjesMundo(*f.m.esq, f.an, f.cuadro / 60.0f + float(dbz3::RelojT(ImGui::GetTime())));
        const float k = cr.esc - 1.0f, hx = w[1].m[3] * k, hy = w[1].m[7] * k, hz = w[1].m[11] * k, cg = std::cos(cr.yaw), sg = std::sin(cr.yaw);
        desp = {hx * cg + hz * sg, hy * ay_m, -hx * sg + hz * cg};
      }
      esc.Poner(cam, m, *txp, desp, cr.yaw, 1.0f, toon);
      esc.Pintar(dl);
      return;
    }
  }
  // px por unidad del modelo igual para todos (el de Goku: alto de referencia), anclado por los pies
  const float alto_modelo = ymax - ymin + 1.1f;
  // B2 encuadra a todos a una altura de pantalla parecida (Cell, Piccolo y Goku salen del mismo tamano): altura normalizada
  // factor de tamano medido contra B2 (PCSX2, rueda de cada uno): Goku 0,85, Vegeta 0,93, Piccolo 0,95, Cell 1,0, Frieza 0,69; el
  // resto 0,93 [H] (B2 usa para cada uno los campos [2..4] de su registro de camara 0x37ABD0, sin descifrar del todo)
  const float fac = FacDe(g_fig_id);
  const float alto_px = g_fig_px * fac;
  ps2::Camara cam;
  cam.fov = 0.785f;
  cam.c = g_fig_pie;
  cam.h = ImVec2(450, 450);
  const float fl = 450.0f / std::tan(cam.fov * 0.5f), d = alto_modelo * fl / std::max(alto_px, 1.0f);
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = alto_f * BordeFig(estado_b3);
  toon.rampa = estado_b3 == 2 ? g_rampa_fig : nullptr;
  static const float giro = std::getenv("DBZ3_SEL_GIRO") ? float(std::atof(std::getenv("DBZ3_SEL_GIRO"))) : 0.35f;
  esc.Poner(cam, m, *txp, {0, -ymin, 0}, giro, 1.0f, toon);
  esc.Pintar(dl);
}

// ---- La rueda: anillo de 30 caras + circulo azul + nombre + flechas y rotulo 1P/2P/COM + (modelo 3D: aparte)
// `rotulo`: 0 = 1P, 1 = COM, 2 = 2P (nave 2P). Imagenes del #AMA de la rueda: 118 (1P), 35 (COM) y 119 (2P).
void DibRueda(ImDrawList* dl, const Lienzo& L, float c, int rotulo, bool menu) {
  if (!g_rueda.ok()) return;
  const std::vector<Entrada>& pg = Pag();
  const int n = int(pg.size()) + 1;   // + el "?"
  const Giro2& g = g_giro_rueda;
  float R = 180.0f, cab = float(kAnilloRueda.th0), roll = 0;
  if (c < 60.0f) {
    const AnilloEntrada e = EntradaAnillo(c, 60.0f, float(kAnilloRueda.th0));
    R = e.R;
    cab = e.cab;
    roll = e.roll;
  }
  for (int i = 0; i < n; ++i) {
    float d = float(i) - g.pos;
    d -= float(n) * std::round(d / float(n));
    float x, y, z;
    AnilloPos(kAnilloRueda, R, cab, kAnilloRueda.arco * d / float(n) + roll, x, y, z);
    float s = 1.0f - (z - kAnilloRueda.z0) * 0.0002f;
    if (i == g.obj && c >= 60.0f && s >= 0.97f) s = 1.2f;   // la elegida (FUN_002A97C0)
    const ImVec2 p(x, y);
    const int cara = i == n - 1 ? 29 : pg[size_t(i)].cara;
    if (cara >= 0) {
      Spr2(L, g_rueda, 36 + cara, 0, false, ImVec2(0, 0), &p, s, 1.0f, z);
    } else if (const ImTextureID tex = CaraDe(pg[size_t(i)].b3)) {   // sin cara de B2: la de B3 HD en el mismo aro
      Capa cp;
      cp.z = z;
      cp.tex = tex;
      cp.circ = true;
      cp.p[0] = L.P(x - 1.0f * s, y + 8.0f * s);
      cp.p[1] = ImVec2(21.0f * s * L.s, 5.0f * s * L.s);
      cp.uv[0] = ImVec2(0.0f, 0.0f);
      cp.uv[2] = ImVec2(1.0f, 1.0f);
      cp.col = IM_COL32_WHITE;
      g_capas.push_back(cp);
    }
  }
  {   // el modelo grande del elegido, en z = 1500 (entre las caras lejanas y las cercanas)
    const int sel = g.obj;
    const int id_b2 = sel < n - 1 ? pg[size_t(sel)].b2 : -1;
    // Decision del usuario (23/10): el modelo NATIVO de B3 con su pose de B3 para todo luchador que existe en B3 (ahorra IDs y evita
    // la pose de B2); el de B2 solo si el de B3 no se pudo generar (sin Python / data_cmn) o el luchador no esta en B3.
    const int b3_sel = sel < n - 1 ? pg[size_t(sel)].b3 : -1;
    bool b3_ok = b3_sel >= 0;
    if (b3_ok) {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      b3_ok = !g_modb3_fallo.count(b3_sel);
    }
    // B3M: con el modelo de B3 CON HUESOS (vista_b2.py) la figura va por la camara real de la rueda (PintaFig) con la animacion de B2
    int sk_est = 0;
    if (id_b2 >= 0 && b3_ok && c >= 25.0f && !std::getenv("DBZ3_SEL_FORZAR_B2")) {
      const SkB3* sk_ = nullptr;
      sk_est = SkEstado(FidB3(b3_sel, 0, FormaFigB3(b3_sel)), &sk_, IdFormaB3(b3_sel, FormaFigB3(b3_sel)));
    }
    g_fig_id = (id_b2 >= 0 && (!b3_ok || sk_est >= 1 || std::getenv("DBZ3_SEL_FORZAR_B2")) && c >= 25.0f) ? id_b2 : -1;
    g_fig_b3 = (b3_ok && g_fig_id < 0 && c >= 25.0f) ? b3_sel : -1;
    static const float ref = [] { const Fig& r = FigDe(0); return r.ok ? r.alto : 8.0f; }();
    g_fig_alto_ref = ref;
    const float alto_goku = std::getenv("DBZ3_SEL_ALTO") ? float(std::atof(std::getenv("DBZ3_SEL_ALTO"))) : 450.0f;
    g_fig_px = alto_goku * L.s * L.k;
    g_fig_L = L;
    g_fig_pie = L.P(std::getenv("DBZ3_SEL_X") ? float(std::atof(std::getenv("DBZ3_SEL_X"))) : 125.0f,
                    std::getenv("DBZ3_SEL_Y") ? float(std::atof(std::getenv("DBZ3_SEL_Y"))) : 500.0f);
    if (g_fig_id >= 0 || g_fig_b3 >= 0) {
      Capa cp;
      cp.z = 1500.0f;
      cp.fn = PintaFig;
      g_capas.push_back(cp);
    }
  }
  const ImVec2 o(161, 0);
  const float cc = std::max(0.0f, c - 20.0f);   // los rotulos entran tras media entrada del anillo (FUN_002AA280: 60 cuadros)
  Obj2(L, g_rueda, 17, 18, cc, o);               // circulo azul bajo la cara (FUN_002AB* tabla 0x37AB50)
  Obj2(L, g_rueda, 21, 22, cc, o);               // la banda oscura del nombre
  const int i = g.obj;
  const int cara_sel = i == n - 1 ? 29 : pg[size_t(i)].cara;
  if (cara_sel >= 0) Obj2(L, g_rueda, -1, cara_sel == 0 ? 67 : 68 + cara_sel, cc, o);   // nombre nativo (anim 0x43 la primera, 0x44 + i las demas)
  Obj2(L, g_rueda, 13, 14, cc, o);                      // flechas
  const ImVec2 po(161, 296);
  Spr2(L, g_rueda, rotulo == 1 ? 35 : rotulo == 2 ? 119 : 118, 0, false, ImVec2(0, 0), &po, 1.0f, std::min(1.0f, cc / 15.0f), 1100.0f);   // 1P / 2P / COM
  (void)menu;
}

// Spr2 con la imagen (rect, textura, z) de `img` y el movimiento / escala / alfa de `mot`; `base_y` = la y de reposo de
// `mot` (la imagen se desplaza lo que `mot` se aparta de ella).
void Spr2b(const Lienzo& L, const Ama2& set, int img, int mot, float c, bool bucle, ImVec2 d, float base_y, float tinte_m) {
  if (img < 0 || mot < 0 || img >= int(set.a.size()) || mot >= int(set.a.size())) return;
  const Anim2 &I = set.a[size_t(img)], &M = set.a[size_t(mot)];
  if (I.p.empty() || I.k.empty() || M.k.empty()) return;
  const Pose2 q = PoseAma2(M, c, bucle);
  const Pz2& p = I.p.front();
  if (p.tex < 0 || p.tex >= int(set.tex.size()) || !set.tex[size_t(p.tex)].t) return;
  const Tex& t = set.tex[size_t(p.tex)];
  const float al = q.a;
  if (al <= 0.001f) return;
  const float x = I.k[0].x + d.x, y = I.k[0].y + (q.y - base_y) + d.y;
  Capa cp;
  cp.z = q.z;
  cp.tex = reinterpret_cast<ImTextureID>(t.t.get());
  cp.p[0] = L.P(x + I.x0 * q.sx, y + I.y0 * q.sy);
  cp.p[1] = L.P(x + (I.x1 + 1) * q.sx, y + I.y0 * q.sy);
  cp.p[2] = L.P(x + (I.x1 + 1) * q.sx, y + (I.y1 + 1) * q.sy);
  cp.p[3] = L.P(x + I.x0 * q.sx, y + (I.y1 + 1) * q.sy);
  cp.uv[0] = ImVec2(p.u0 / t.w, p.v0 / t.h);
  cp.uv[1] = ImVec2(p.u1 / t.w, p.v0 / t.h);
  cp.uv[2] = ImVec2(p.u1 / t.w, p.v1 / t.h);
  cp.uv[3] = ImVec2(p.u0 / t.w, p.v1 / t.h);
  const ImU32 m = ImU32(std::clamp(q.m[0] * tinte_m, 0.0f, 1.0f) * 255.0f);
  cp.col = m | (m << 8) | (m << 16) | (ImU32(std::clamp(al, 0.0f, 1.0f) * 255.0f) << 24);
  g_capas.push_back(cp);
}
// ---- El menu Normal / Custom / Edit Skills (tabla 0x37ABA0): panel (10 -> 11) y tres palabras: cada una con su imagen
// (anims 0 / 8 / 9) y el movimiento de la 0 (entrada) y luego el de la 6 (la elegida: late 1,15 -> 1,05) o el de la 2 (apagada, a media luz)
void DibMenuRueda(const Lienzo& L, float c, int sel) {
  if (!g_rueda.ok()) return;
  const ImVec2 o(161, 0);
  Obj2(L, g_rueda, 10, 11, c, o);
  const int img[3] = {0, 8, 9};
  const float de = DurAma2(g_rueda, 0);
  for (int k = 0; k < 3; ++k) {
    if (c < de) Spr2b(L, g_rueda, img[k], 0, c, false, o, 160.0f, 1.0f);
    else Spr2b(L, g_rueda, img[k], k == sel ? 6 : 2, c - de, k == sel, o, 160.0f, 1.0f);
  }
}

// ---- La burbuja de escenarios
// casillas desbloqueadas (todas: el desbloqueo por avance del original no esta portado [H]); `k` = la elegida
void DibBurbuja(const Lienzo& L, float c, float t_ant_dummy) {
  (void)t_ant_dummy;
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
    const float s = 1.0f - (z - kAnilloBurb.z0) * 0.0001f;
    const ImVec2 p(x, y);
    Spr2(L, g_burb, 29 + g_cel[size_t(i)], 0, false, ImVec2(0, 0), &p, s, 1.0f, z);
  }
  Obj2(L, g_burb, 6, 7, c, off, nullptr, 1.0f, 0.6f);          // la bola (tex 2): entra fundiendo 50 cuadros
  const float c2 = c - 45.0f;
  if (c < 45.0f) {
    // slot 4 (anim 12, 3 piezas con alfa 0 -> 1 -> 0): destello de la imagen mientras gira el anillo
    const Pose2 q = PoseAma2(g_burb.a[12], c, false);
    Spr2(L, g_burb, 19 + g_cel[size_t(std::clamp(g.obj, 0, n - 1))], 80, false, off, nullptr, 1.0f, 0.45f * q.a, 1200.0f);
  } else {
    Spr2(L, g_burb, 19 + g_cel[size_t(std::clamp(g.obj, 0, n - 1))], c2, false, off, nullptr, 1.0f, 0.45f);   // la imagen del escenario (anim 0x13 + k, slot 8)
    Obj2(L, g_burb, 3, 4, c2, off);                              // el marco naranja de la casilla elegida
    Obj2(L, g_burb, 9, 10, c2, off);                             // el cristal (tex 3): 0,7
    if (g.obj > 0) Obj2(L, g_burb, 0, 1, c2, off);               // flecha izquierda (se esconde en el primero)
    if (g.obj < n - 1) Obj2(L, g_burb, 39, 40, c2, off);         // flecha derecha (se esconde en el ultimo)
  }
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
    // EU 0x263670 FUN_00263670
    case 1:   // 0x263670: 40 cuadros; sin Kili se acaba
      if ((r.espera += cuadros) > 40) {
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
    if (c < g_cartel) {
      char li[48];
      std::snprintf(li, sizeof(li), "%d Kili%s", p.kili, g_res.nuevo ? T::T("  ¡Nuevo!", "  New!") : "");
      S::MenuText(dl, L.P(320, 300), 15 * L.s, li, false, false);
    }
  }
}

}  // namespace

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
bool DibujarFiguraB2(ImDrawList* dl, int id_b2, ImVec2 pie, float alto_px, float giro, float t, int traje, int pbank, int panim, const float* cb) {
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
  if (anima) ps2::Posar(*f.m.esq, *an, t, pose);
  ps2::Malla m = anima ? pose : f.m.malla;
  const std::vector<ps2::Tex>* txp = &f.tx;
  float alto_f = f.alto;
  const int estado_b3 = anima ? FiguraB3(id_b2, traje, f, t, m, txp, alto_f) : 0;   // B3M: el modelo de B3 con la animacion de B2
  if (estado_b3 == 1) return true;   // generandose (primer uso): nada, no el de B2
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
      else m.tris.push_back(tr);
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
    ps2::Escena es;
    es.Empezar();
    ps2::Escena::Toon tn;
    tn.borde = alto_f * BordeFig(estado_b3);
    tn.rampa = estado_b3 == 2 ? g_rampa_fig : nullptr;
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
    es.Pintar(dl);
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
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = alto_f * BordeFig(estado_b3);
  toon.rampa = estado_b3 == 2 ? g_rampa_fig : nullptr;
  esc.Poner(cam, m, *txp, {0, -ymin, 0}, giro, 1.0f, toon);
  esc.Pintar(dl);
  return true;
}

// S1-F01.2: el ELF de B2 del jugador para el tablero (el iris de la Bola de Dragon lee de el sus tablas de estrellas).
const std::vector<uint8_t>& ElfB2() {
  static const std::vector<uint8_t> e = LeerElf(BuscarJuego("budokai 2"));
  return e;
}
bool Disponible() { return b2::Disponible(); }
// Entrada por mando de este cuadro y conexion del mando 2 (pasarela_misiones.cpp la llama antes de Dibujar, como
// b2::Mantenidos). Es la entrada que leen las pantallas 1P/2P del original: 0x264F60/0x260630 miran los cuatro
// registros de mando (0x3B43B2/0x4412/0x446A/0x44CA) y solo dejan 2P con los dos mandos conectados (0x3B43A0/0x3B4458 == 3).
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
  g_luch = g_riv = g_sitio = g_sitio_vis = g_menu_sel = g_lista_capsulas = g_pag = g_j3 = 0;
  g_r3 = 3;
  // La planta en curso: B2 la guarda con el resto de su estado (cRam003a492c, cursor de 0x264790) y al volver a la
  // nave sigue donde se quedo. Aqui se persiste en progreso.toml para no perderla al salir (N3, DS151).
  g_prueba = std::clamp(LeerProgreso("nave_planta", 0), 0, 3);
  ConstruirPaginas();   // A2nave: las ruedas arrancan en el primero (Goku), como B2
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

// 0 nada, 1 salir, 2 combate pedido en out = {jugador, rival, mapa, vida inicial, tiempo, prueba, P2 humano}.
int Dibujar(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, int out[7]) {
  Cargar();
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
    antes = g_estado;
    desde = t;
    // EU 0x2644E0 FUN_002644e0 (de la pantalla de pruebas a la rueda: ruedas del jugador y del COM)
    if (g_estado == kJugador && prev != kMenu) {   // la rueda de 1P entra (60 cuadros); desde el menu ya esta puesta
      ConstruirPaginas();                          // el roster cambia con los mods: se lee al entrar
      g_pag = 0;
      g_giro_rueda.Fijar(0);
      g_t_rueda = t;
      g_rueda_com = false;
    }
    if (g_estado == kRival) {                       // la del COM (misma pantalla, rotulo COM); en 2P la del mando 2
      g_pag = 0;
      g_giro_rueda.Fijar(0);
      g_t_rueda = t;
      g_rueda_com = !g_2p.load();                   // 2P: la camara de la rueda es la del jugador (1P)
    }
    if (g_estado == kSitio) {                       // la burbuja entra (45 cuadros)
      ConstruirCasillas();
      g_sitio_vis = 0;
      g_giro_burb.Fijar(0);
      g_t_esfera = t;
    }
    if (g_estado == kMenu) g_t_menu = t;
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
        if (ok) g_res.premio_t = g_cuadro - g_cartel;   // salta el cartel
        ok = false;
        FichaReset();
      } else {
        if (pressed & (kX | kLeft | kRight)) FichaGirar(c - g_cartel);
        else if (pressed & (kA | kStart | kY)) CerrarFicha(c);
        ok = PremioCerrado(c);
      }
    }
    // EU 0x263700 FUN_00263700
    const bool fin = PasoResultado(dt * 60.0f, ok);
    DibujarResultado(dl, L);
    M::Help(dl, a, {{"A", T::T("Seguir", "Continue")}});
    // EU 0x262AF0 FUN_00262af0 (vuelta del combate: de la pantalla de resultado a la de pruebas, 0x264DE0)
    if (fin) {
      g_estado = kPrueba;
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
      DibC(dl, L, dos_j ? 28 : 35, 0, false, 0, 0, 0, IM_COL32(150, 150, 150, 255));
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
      DibC(dl, L, 30, cs, false);
      if (cs >= DurTarea(30)) DibC(dl, L, 34, cs - DurTarea(30), false);
      if (cs >= std::max({DurTarea(26), DurTarea(30) + DurTarea(34), DurTarea(31)}) || auto_on) {
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
    if (pressed & kB) Efecto(0, 3);                       // b2.c:211730
    else if (ok) Efecto(0, 5);                            // b2.c:211712
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
    M::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Volver", "Back")}});
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
  if (g_estado == kSitio) Obj(dl, L, 121, 122, g_cuadro - g_c_tit);
  else Obj(dl, L, 118, 119, g_cuadro - g_c_tit);
  dl->PushClipRect(L.P(0, 0), L.P(640, 448), true);
  if (g_estado == kSitio) {
    Giro2& g = g_giro_burb;
    g.Paso(dt_c);
    const float cs = (t - g_t_esfera) * 60.0f;
    DibBurbuja(L, cs, 0);
    VaciarCapas(dl);
    dl->PopClipRect();
    Babidi(dl, L, g_frase_sitio, true);
    M::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Volver", "Back")}});
    if (pressed & kB) {
      Efecto(0, 3);
      g_estado = kRival;
      return 0;
    }
    if (cs >= 45.0f && !g.Girando()) {
      if (pressed & kLeft) {
        g.Mover(-1, int(g_cel.size()), false);
        if (g.Girando()) Efecto(0, 2);
      } else if (pressed & kRight) {
        g.Mover(1, int(g_cel.size()), false);
        if (g.Girando()) Efecto(0, 2);
      } else if (pressed & kY) {
        g.Saltar(int(g_cel.size()) - 1, int(g_cel.size()));   // el cuadrado salta al "?" (FUN_002AFEB0)
        if (g.Girando()) Efecto(0, 2);
      }
    }
    g_sitio_vis = g.obj;
    if (ok && cs >= 45.0f) {
      Efecto(0, 5);
      const bool azar = g.obj == int(g_cel.size()) - 1;
      g_sitio = azar ? g_cel[size_t(std::rand() % std::max(1, int(g_cel.size()) - 1))] : g_cel[size_t(g.obj)];   // el "?" elige uno al azar
      g_vida = 0;
      g_vencidos = 0;
      REXLOG_INFO("dbz3 nave: eleccion jugador {} rival {} escenario {} (id B2 {}){} planta {}", g_j3, g_r3, g_sitio,
                  kEscenas2[g_sitio].id_b2, azar ? " al azar" : "", g_prueba + 1);
      Pedir(out);
      g_estado = kPrueba;
      return salir(2);
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
  Babidi(dl, L, g_frase_sel, true);
  AyudaB2(dl, L, t);
  if (g_estado == kMenu) {
    if (pressed & kB) {
      Efecto(0, 3);
      g_estado = kJugador;
      return 0;
    }
    // EU 0x25F7D0 FUN_0025F7D0 (lista vertical de cGpffff8be0 filas, 0x699D30): abajo en la ultima fila vuelve a la primera y arriba
    // en la primera salta a la ultima (si hay mas de una fila); sonido 2 en cada cambio; cruz (0x4000) elige, triangulo (0x1000)
    // sale. [H]: que esta sea la lista Normal / Custom / Edit Skills (se supone: 3 filas desde la tabla 0x6936F0 + 0xB6E).
    if (pressed & kUp) {
      g_menu_sel = (g_menu_sel + 2) % 3;
      Efecto(0, 2);
    }
    if (pressed & kDown) {
      g_menu_sel = (g_menu_sel + 1) % 3;
      Efecto(0, 2);
    }
    if (ok && g_menu_sel == 2) {   // Edit Skills NATIVO (pantalla 22); al salir el juego vuelve al menu principal y la nave sigue aqui
      Efecto(0, 5);
      if (dbz3::hub::RequestEditSkills()) REXLOG_INFO("dbz3 nave: Edit Skills nativo pedido (personaje B3 {} / pagina {})", g_j3, g_pag + 1);
      return 0;
    }
    if (ok) {
      Efecto(0, 5);
      // EU 0x263D20 FUN_00263d20
      // EU 0x2640E0 FUN_002640e0
      // FUN_00260CF0 (b2.c): seleccion 0/1 elige Normal/Custom; la opcion 2 es Edit Skills y no altera esta eleccion.
      g_lista_capsulas = g_menu_sel == 1 ? 1 : 0;
      REXLOG_INFO("dbz3 nave: menu {} (lista de capsulas {})", g_menu_sel == 0 ? "Normal" : "Custom",
                  g_lista_capsulas ? "Custom" : "Normal");
      g_estado = kRival;
    }
    return 0;
  }
  // EU 0x25FE30 FUN_0025fe30
  if (pressed & kB) {
    Efecto(0, 3);
    g_estado = g_estado == kJugador ? kPrueba : (g_2p.load() ? kJugador : kMenu);   // 2P: la rueda del mando 2 vuelve a la del 1P
    return 0;
  }
  const int n = int(Pag().size()) + 1;
  if (cr >= 60.0f && !g.Girando()) {
    if (g_pags.size() > 1 && (pressed & (kLB | kRB))) {   // L1 / R1: otra pagina del roster
      const int np = int(g_pags.size());
      g_pag = ((g_pag + ((pressed & kRB) ? 1 : -1)) % np + np) % np;
      g.Fijar(0);
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
    if (pressed & kLeft) {
      g.Mover(paso(-1), n, true);
      if (g.Girando()) Efecto(0, 2);
    } else if (pressed & kRight) {
      g.Mover(paso(1), n, true);
      if (g.Girando()) Efecto(0, 2);
    }
  }
  // EU 0x263A80 FUN_00263a80 (rueda lista -> elegido: guarda el luchador, sonido y paso a la pantalla siguiente)
  if (ok && cr >= 60.0f) {
    const bool azar = g.obj == n - 1;
    int casilla = azar ? std::rand() % (n - 1) : g.obj;   // el "?" elige uno al azar de la pagina
    Efecto(0, 5);
    const Entrada e = Pag()[size_t(casilla)];
    if (g_estado == kJugador) {
      g_luch = casilla;
      g_j3 = e.b3;
      REXLOG_INFO("dbz3 nave: eleccion 1P {} (casilla {}, id B2 {}, id B3 {}, pagina {}){}", NombreDe(e.b3), casilla, e.b2, e.b3, g_pag + 1, azar ? " al azar" : "");
      g_estado = kMenu;
    } else {
      if (e.b3 == g_j3) casilla = (casilla + 1) % (n - 1);   // el rival no es el mismo luchador
      const Entrada e2 = Pag()[size_t(casilla)];
      g_riv = casilla;
      g_r3 = e2.b3;
      REXLOG_INFO("dbz3 nave: eleccion {} {} (casilla {}, id B2 {}, id B3 {}, pagina {}){}", g_2p.load() ? "2P" : "COM", NombreDe(e2.b3), casilla, e2.b2, e2.b3, g_pag + 1, azar ? " al azar" : "");
      g_estado = kSitio;
    }
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
      g_cuenta = g_vencidos;
    }
  } else if (g_prueba == 1) {
    g_cuenta = (std::min(ms, kTopeII * 1000) * 60 / 1000 + 60) / 60;   // (cuadros + 60) / 60
  } else if (g_prueba == 2) {
    g_cuenta = golpes;
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
  if (ev >= 0x1CD && ev <= 0x1E9) {
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
int ListaCapsulas() { return g_lista_capsulas; }

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
