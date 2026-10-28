// dbz3 - ESCENARIOS NUEVOS (altas) y sonda de colision. Ver escenarios.h. US y EU (src/eu_map.h).
//
// Mapa del juego (US):
//   0x82323760  17 registros x 92 B (+0x00 nombre, +0x10 id_mapa, +0x14 id_SE, +0x18 variante,
//               +0x1C/+0x3C dos grupos de objetos de 8 palabras); 0x82323D80 temas (17 + 0).
//   sub_820C26E0(obj): carga el escenario obj+340 (s16): id_mapa -> AFS, G+0 (0x823A3D80) = indice.
//   sub_820C19B0(i):   id_SE del registro i (lo pide sub_821221F8 con el mismo obj+340).
//   Todo lo demas lee el registro con G+0. Por eso basta con traducir en esos dos ganchos el alta
//   (17+) al anfitrion y tener el registro del anfitrion con los datos del alta mientras este cargada.

#include "escenarios.h"
#include "ingame/game_style.h"
#include "launcher/launcher_state.h"

#include "clima.h"
#include "cine.h"      // ZB: camara de la escena del cambio de zona
#include "combate.h"   // ZB: marcador oculto durante la escena
#include "launcher/settings.h"
#include "mods.h"
#include "pasarela.h"   // seguimiento 12c: reproductor ADX de los modos (OirMusicaPs2/CallarMusicaPs2); DS63: AFS de IW
#include "ps2_modelo.h" // DS63: ps2::Escena / ps2::CamaraCombate / ps2::Tex (clima y suelo en mosaico de IW)
#include "region.h"     // seguimiento 12c: HostDataRoot() (carpeta con los .afs del jugador)
#include "suelo_iw.h"   // DS63: altura del terreno para las salpicaduras del clima

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <rex/hook.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "net/netplay.h"   // frente 12: azar compartido (sin desincronia online)

// Modo opcional "zonas B de B2" (frente 07): al caer por el borde se pasa a la zona B del escenario
// (el mod de la zona B lo declara en su stage.toml con `zona_b = <entrada del escenario A>`).
REXCVAR_DEFINE_BOOL(dbz3_escenarios_base_zona_b, true, "DBZ3/Dev",
                    "Cambio de zona en la Base Red Ribbon B2 (necesita el bin con todas las raices, campos_iw VERSION 18)");

REXCVAR_DEFINE_BOOL(dbz3_escenarios_zonas_b, true, "DBZ3/Mods",
                    "Al caer por el borde se pasa a la zona B del escenario (si su mod la trae; "
                    "stage.toml `zona_b`; desactivado = el ring out acaba el combate como siempre)");

// ZB 25/10: el cambio de zona de B2 como en el original: marcador fuera, camara de escena (los clips #AMC del suceso de B2) y
// el cambio dentro de la escena. Apagada = corte directo como antes (X1esc).
REXCVAR_DEFINE_BOOL(dbz3_escenarios_zona_escena, true, "DBZ3/Mods",
                    "Al pasar de zona en los escenarios de B2, escena de camara del original (stage.toml `zona_escena`); "
                    "apagado = corte directo");

// X1esc 23/10: IMITACION 2D de la nieve de B2 en Montanas nevadas (el port aun no lanza los emisores #ACE de B2: B3 no tiene el
// builtin 0x96). Apagada: la norma es portar, no imitar.
REXCVAR_DEFINE_BOOL(dbz3_nieve_propia, false, "DBZ3/Dev",
                    "Copos 2D sobre Montanas nevadas (B2) mientras no haya emisores 3D (experimental)");
// DS63: clima/polvo/destellos y suelo en mosaico de IW, con los datos del stage.toml (campos_iw.py). Los campos de
// IW que se convierten no traen clima en el original (sus #AMB no llevan #AME de RAIN/Sand Dust y su montaje de
// minijuego no pasa por Stage_SetupObjectsWithClimate), asi que estas lineas no aparecen en sus stage.toml; quedan
// para las altas que si lo declaren (escenarios de combate de IW y mods).
REXCVAR_DEFINE_BOOL(dbz3_clima_iw, true, "DBZ3/Mods",
                    "Clima de IW de los escenarios que lo declaren en stage.toml (`clima`/`polvo`: lluvia, nieve, polvo)");
REXCVAR_DEFINE_BOOL(dbz3_destello_iw, true, "DBZ3/Mods",
                    "Destellos (lens flare) de IW de los escenarios que declaren `destello` en stage.toml");
REXCVAR_DEFINE_BOOL(dbz3_mosaico_iw, true, "DBZ3/Mods",
                    "Suelo en mosaico de IW (47 copias, rejilla 450) cuando el alta declara `mosaico`");

// Frente 12: escenario nuevo en cada revancha cuando el combate salio de la casilla "?" (aleatorio).
REXCVAR_DEFINE_BOOL(dbz3_escenarios_revancha_aleatoria, false, "DBZ3/Mods",
                    "Con el escenario \"aleatorio\" del Duelo (casilla ?), cada revancha carga un escenario "
                    "distinto de los visibles (0..10 y las altas); online el azar sale de la semilla compartida");

// Seguimiento 12c (studies 4): vista previa de la musica de combate en el selector del Duelo (la del
// escenario que se esta mirando). Reutiliza el reproductor ADX de los modos de PS2 (pasarela_b1.cpp).
REXCVAR_DEFINE_BOOL(dbz3_selector_musica, false, "DBZ3/Mods",
                    "Al mover el cursor en el selector del Duelo suena la musica de combate del escenario "
                    "(ADX de la copia del jugador); se corta al salir del selector");

namespace dbz3::net { void InyectarPad(uint32_t usuario, int32_t botones); }   // net/netplay_hooks.cpp (-1 = el mando de verdad)

namespace dbz3::escenarios {
namespace {

// EU 0x1DB560 Task_FogEndIfFlagged y 0x1DB5D0 Task_FogBeginIfFlagged: el cambio
// de estado de niebla pertenece al motor gráfico; los escenarios de B3 usan el
// estado de niebla común (src/clima.cpp), no tareas de escenario PS2.
// EU 0x1DE830 Stage_RemoveObjectRender, 0x1DF010 Stage_RegisterObjectRender y
// 0x1DF100 Stage_RegisterExtraObjectRenders: administración del render de objetos
// del motor PS2. Las altas de B3 entregan los modelos al cargador/renderizador B3;
// no existe aquí una lista PS2 equivalente que deba replicarse.
// EU 0x1DF260 s_Static, 0x1DF4A0 Stage_ProcessStaticObjects y
// 0x1DF7C0 Stage_CallStageSpecificFunc: colas/callbacks de objetos propios del
// motor PS2; las altas usan el flujo de carga estándar de B3, sin callbacks IW.
// EU 0x1E03C0 Stage_LinkAllTextureRefs, 0x1E0560 Stage_CreateAnimData,
// 0x1E0640 Stage_FreeResources y 0x1E09A0 Stage_ParseLoadedBundle: enlazado,
// animación, liberación y parseo de recursos son responsabilidades del cargador
// del motor B3. No se duplica el ciclo de vida de recursos PS2 en las altas.
// EU 0x1E0DA0 Stage_DrawClearAndSetFog: limpieza y estado de niebla son parte del
// pase gráfico común de B3; la personalización de niebla de altas ya se aplica en
// Activar() y en el estado de escenario, sin reproducir el pase GS de PS2.
// DS40 25/10: las animaciones del escenario (UV, material y objetos dinámicos) SÍ se
// reproducen aquí, cuadro a cuadro, sobre los datos que el conversor deja en stage.toml
// (capas/capa/arrastre del #MCF, material/matclave y dinamico del #MIF): ver PasoCapas.
// Lista de trabajo y qué bloque del original lee cada cosa: docs/ENTREGAS/DS40/INFORME.md.
// DS58 26/10 (auditoria LUNA_ESCEN0): lo anterior es sustitucion por el motor/cargador de B3 (y por la niebla
// comun de clima.cpp), no una copia de las listas PS2. La unica parte visible que se corrige aqui es la niebla
// del campo: el #MCF guarda color +0x1C y CUATRO floats +0x20/+0x24/+0x28/+0x2C (Stage_ParseResourceBundle
// 0x307060 los copia a cdb8+0x63c..0x64c y el dibujo llama a FUN_0012c920/960/930); la entrada de niebla de B3
// es [color, +4, +8, ini+12, fin+16] y sub_820C2058 lee los cuatro. Ahora se copian tambien +0x20/+0x24 a
// +4/+8, el color va como 0x00RRGGBB (las entradas nativas de B3 lo son; antes se escribia RRGGBBAA y los canales
// se corrian) y un campo sin #MCF (880) usa los valores por defecto del original (World_CharactersMiscContent
// 0x307410 l.394839-844: blanco, 0/255, 1000..2000) en vez de leer el #AMT hijo 0 (niebla negra 0..0).


constexpr dbz3::GA kRecs = 0x82323760, kTemas = 0x82323D80;
constexpr uint32_t kRec = 92;
constexpr int kAnfitrion = 16;   // R&R BASE: grupos de objetos vacios y tema generico
// Estado del escenario cargado (clima.cpp): G+0 = registro, G+0x20 = #ZDD cargado, G+0x40 = objeto/zona
// activa (el motor la llama "variante"), G+0x4C = banderas. El cambio de zona del motor la cambia.
constexpr dbz3::GA kG = 0x823A3D80;

// Ring out (medido, tagteam.cpp DBZ3_TEST_RINGOUT): el aviso mgr+35569 = hueco del ganador + 1 lo pone
// el juego al caer alguien del ring y sub_820FD678 (cada fotograma de la pelea) lo da como fin (motivo 4).
constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
constexpr uint32_t kAvisoRing = 35569;
constexpr uint32_t kEstadoCombate = 35128;   // mgr+35128: 4 = pelea, 5 = fin (modos.cpp, tagteam.cpp)

// X1esc 23/10: cielo y nubes de los campos de IW derivan (UV): `nubes = "firma hex (3 ventanas de 44 B),n ventanas,du,dv,tam #ZDD"`
// en stage.toml (mod center hd/campos_iw.py). Se halla la firma en el #ZDD cargado del guest y se suma du, dv por cuadro (a 60 Hz)
// a los UV (+36/+40 de cada ventana BE): la GPU lee los vertices de la memoria del guest en cada dibujo.
// DS40 25/10: la deriva sale del #MCF del campo (8 capas), no de una pareja suelta: ver Capa / PasoCapas.
struct Capa {
  float u = 0, v = 0, px = 0, py = 0;   // #MCF: base u/v por cuadro y arrastre por unidad de camara
  float acc_u = 0, acc_v = 0;           // estado en [0,1) (el original lo guarda en iRam0049cdb8+0x5f0)
};
struct Nube {
  std::vector<uint8_t> firma;
  uint32_t n = 0, tam = 0, guest = 0;
  int capa = -1;                          // capa del #MCF que la mueve (-1 = mod viejo: usa du/dv del propio nubes)
  float du = 0, dv = 0, px = 0, py = 0;
  float acc_u = 0, acc_v = 0;
  // EU 0x1E2880 Stage_DrawCameraFollowModel: el cielo se dibuja trasladado a (camara.x, 0, camara.z). `sigue` = 1 en
  // la pieza de cielo (campos_iw.py CIELO); `off_*` = traslacion ya escrita en sus ventanas (delta por cuadro).
  // DIFIERE LUNA_ESCEN2_0 (DS64): el port traduce la traslacion con la camara; la evaluacion UV y la niebla que el
  // original delimita alrededor de este dibujo son del pase GS de PS2 y las lleva B3 (src/clima.cpp sub_820C2058).
  int sigue = 0;
  float off_x = 0, off_z = 0;
};
// EU 0x1DD590 Anim_StepMaterialAnim: pista de color de material con bucle; claves en cuadros (RGBA8).
struct MatClave { uint32_t f = 0, rgba = 0; };
struct Material {
  std::vector<uint8_t> firma;   // primeros 16 B del registro de material del #AWG (incluye su color)
  uint32_t tam = 0, guest = 0, frames = 1;
  std::vector<MatClave> claves;
  float t = 0;
};
// EU 0x1DEF90 Stage_ProcessDynamicObjects / EU 0x1DED90 s_Dynamic: objetos que el original crea por nombre (#MIF).
struct Dinamico {
  int modelo = 0, rejilla = 0;
  std::string nombre;
  float x = 0, y = 0, z = 0;
};
// DS73 26/10: OBJETO ANIMADO UV DEL ESCENARIO (#MTA) — las auditorias LUNA_SESC0/1/2 lo devolvieron a falta
// (0x1E2ED0 Stage_CreateAnimUVObject, 0x1E2C60 Stage_AnimObjectUpdateUVScroll, 0x1E2DF0 Stage_AnimObjectDraw,
// 0x1DD2D0 Task_UvAnimCopyStateToGlobals, 0x1DD310 Task_UvAnimEvalChannels, 0x1DD3B0 Anim_SetUvAnimFlag y
// 0x1DDD80 Task_RunUpdateCallbacksFlag2800).
// Formato del #MTA (decomp 0x1BB120 UvAnim_RelocateData y 0x1BAB60 UvAnim_EvalChannel): +4 n.o de canales, +8 offset
// de la tabla; canal = 0x80 B: 6 descriptores de 16 B {+0 n.o de claves, +4 cuadros del bucle, +8 offset de claves,
// +0xC valor por defecto}; las claves van a 16 B {+0 cuadro i32, +4 valor f32}. Los descriptores 4 y 5 son la deriva
// u/v por cuadro; 0..3 la matriz UV. El estado vivo es un vector de 8 floats por canal: [0..5] valores evaluados,
// [6..7] acumuladores u/v (EvalChannel suma [4]/[5] y quita la parte entera, l.155090-155114).
// `uvobjeto`/`uvcanal` (campos_iw.py y stage.toml) entregan lo que el paquete trae; si el modelo no viaja en el
// alta, el objeto queda en estado y no toca vertices (se avisa una vez), que es lo mismo que ver el original.
struct UvClave { int32_t f = 0; float v = 0; };
struct UvCanal {
  int32_t nk[6] = {};
  int32_t bucle[6] = {};
  float defecto[6] = {};
  std::vector<UvClave> claves[6];
};
struct ObjetoUv {
  int entrada = 1;                    // entrada (0,1) del escenario (Stage_CreateAnimUVObject l.185062)
  uint32_t mascara = 0;               // canales activos en el buffer UV (uRam0049ca34)
  uint32_t flags = 0x100;             // Stage_CreateAnimUVObject: MapLoader con 0x100 (l.185081-185086)
  std::vector<UvCanal> canales;
  float estado[0x16][8] = {};         // 8 floats por canal, como iRam0049ca38*0x200+0x4c8410
  float aplicado_u[0x16] = {};        // ultimo u/v escrito en las ventanas (se aplica el delta, no se acumula)
  float aplicado_v[0x16] = {};
  float tiempo = 0;                   // fRam0049c7dc acumulado en cuadros (Stage_UpdateObjAnims l.394132)
  uint32_t zdd = 0, guest = 0, n = 0; // ventanas del #ZDD localizadas por `firma` (opcional, para aplicar UV)
  std::vector<uint8_t> firma;
  bool avisado = false;
};
// DS63: clima de IW. Los emisores del original no son #AME del paquete: ClimateScript (EU 0x1E2150) usa las
// definiciones de ELF 0x44B4F0/0x44B540 ("SNOW": caja 30x30, base -200; +0x44B570 color 0.7/0.6/0.5, tam 1.0,
// 0.3..3.5, 102 particulas) y Rain/SnowToTheGroundEffect (EU 0x1E1FC0, efecto "Sand Dust" 0x493E08 que son las
// salpicaduras del suelo). El port los reproduce con los mismos numeros; el atlas (si el stage.toml lo trae) sale
// del AFS de IW del jugador (pasarela::AfsPs2 + ps2::TexturaAmt).
struct ClimaP {
  std::string tipo;        // snow | rain | dust
  uint32_t n = 0;          // particulas vivas
  float tam = 1.0f;        // medio tamano en unidades de IW
  float vel = 60.0f;       // caida por segundo
  float area = 900.0f;     // lado del volumen alrededor de la camara
  float alto = 400.0f;     // altura de emision sobre el suelo
  uint32_t color = 0xFFFFFF;
  float viento = 0.0f;     // deriva horizontal por segundo
  uint32_t afs = 0, hijo_amt = 0, tex = 0;   // atlas opcional (entrada del DATA_CMN de IW, hijo #AMT, textura)
};
// EU 0x1DE690 s_LensFlare / EU 0x1E24E0 Stage_SetupObjectsLensFlareB: el destello es un objeto "LensFlare"
// (0x493D58) que se dibuja en pantalla; la tabla de escalas/colores por escenario esta en ELF 0x44A5E0 (16 B por
// entrada: tamano, distancia, intensidad, alfa). El port lo pinta con la camara del combate.
struct DestelloP {
  float x = 0, y = 200, z = 0;   // posicion del sol/luz en el mundo
  float tam = 60.0f;
  uint32_t color = 0xFFFFFF;
  uint32_t afs = 0, hijo_amt = 0, tex = 0;
};
// EU 0x1E2590 Stage_DrawTiledGround: el suelo se dibuja 47 veces en una rejilla de 450 alrededor de la camara
// (tabla ELF 0x44B5B0, 47 vectores xyz). El port solo lo activa si el stage.toml trae el modelo original; asi no
// duplica el terreno ya convertido de los campos.
struct MosaicoP {
  bool on = false;
  float paso = 450.0f;
  uint32_t afs = 0, amb_hijo = 0, amo = 0, amt = 0;
  int variante = 0;
};
struct Alta {
  std::string mod, nombre, titulo;   // nombre: el del registro (<= 15); titulo: el del rotulo
  uint32_t entrada = 0, id_se = 45, menu = 0;   // menu: #AZT con la bola, la vista y el rotulo (selector)
  int tema = 16;
  int variante = -1;   // 12b: registro alternativo al mantener Circle en el selector (-1 = el mismo que `entrada`)
  int musica = -1;     // 12c: entrada ADX (adx_usa.afs) de la musica de combate para la vista previa (-1 = no)
  bool niebla = false;   // R8: niebla propia (campos de IW: la del #MCF), "RRGGBB,inicio,fin[,fogA,fogB]" en stage.toml
  uint32_t niebla_color = 0;
  float niebla_ini = 0, niebla_fin = 0;
  // DS58: los dos floats de la niebla del #MCF (+0x20/+0x24). Entran en la entrada de niebla de B3 en +4/+8
  // (sub_820C2058 -> sub_820856B8); sin ellos se quedaban los del registro anfitrion.
  float niebla_a = 0, niebla_b = 0;
  bool niebla_extra = false;
  uint32_t escena = 0;   // ZB: clips de camara de la escena del cambio de zona (ultimo #ACC del objeto 0); 0 = corte directo
  uint32_t zona_b = 0;   // frente 07: esta alta es la zona B del alta cuya `entrada` es este numero (0 = no)
  std::vector<Nube> nubes;   // X1esc: deriva UV del cielo y las nubes (campos de IW)
  // DS40: animacion del escenario (campos de IW). `capas` = 8 capas UV del #MCF; `arrastre` = el original le pasa el
  // movimiento de la camara a Node_ScrollBackgroundUV (solo los minijuegos de vuelo; los de campo pasan 0).
  bool arrastre = false;
  uint32_t capa_flags = 0;
  float capa_ex = 0, capa_ey = 0;
  std::vector<Capa> capas;
  std::vector<Material> mats;
  std::vector<Dinamico> dinamicos;
  std::vector<ClimaP> climas;   // DS63: clima/polvo/particulas del escenario (stage.toml `clima`/`polvo`)
  std::vector<DestelloP> destellos;   // DS63: lens flare (stage.toml `destello`)
  MosaicoP mosaico;   // DS63: suelo en mosaico (stage.toml `mosaico`)
  std::string setup;   // DS63: funcion de montaje del original ("Stage_SetupObjectsWithClimate", ...)
  int variante_escena = 0;   // DS73: iRam004db2c0 de la tabla 0x44ABC4+4 (0/1); decide clima, polvo y objeto global
  bool usa_clima = false;    // DS73: Stage_SetupObjectsWithClimate/SandDust, variante 1
  bool usa_polvo = false;    // DS73: Stage_SetupObjectsWithSandDust, variante 0
  bool usa_global = false;   // DS73: w_s_Global(0) (Stage_SetupObjects variante 0 / GlobalVar1 variante 1)
  bool usa_destello = false; // DS78: el setup llama a s_LensFlare (0x1DE690) segun la tabla 0x44B1D0
  std::vector<ObjetoUv> uvobjetos;   // DS73: objetos animados UV del escenario (#MTA)
  std::string uvanim;   // #MTA del campo (animacion UV de un modelo): resumen informativo
  bool oculta = false;   // fuera del selector del Duelo (campos de IW: solo para sus minijuegos); `selector = false`
};
rex::memory::Memory* g_mem = nullptr;
std::vector<Alta> g_altas;
uint32_t g_visibles = 0;   // altas con casilla en el selector: las primeras de g_altas (las ocultas van al final)
int g_activa = -1;               // alta puesta en el registro anfitrion (-1 = ninguna)
bool g_zona_motor = false;       // X1esc: PasarZonaB llama al cambio de zona del motor (el gancho de sub_820CC6C0 lo deja pasar)
uint8_t g_orig[kRec];            // registro original del anfitrion
uint32_t g_orig_tema = 0;
constexpr dbz3::GA kNieblas = 0x823232A0;   // entradas de niebla de 20 B (color, 2 floats, inicio, fin): clima.cpp
uint32_t g_niebla = 0;                       // entrada de niebla prestada mientras el alta esta puesta (0 = ninguna)
uint8_t g_orig_niebla[20];
uint32_t g_obj_combate = 0;                  // obj del escenario del combate (sub_820C26E0): para recargarlo
int g_fondo = -1;                            // bit 0 de G+0x4C guardado mientras el fondo esta apagado (-1 = sin guardar)

std::string Limpia(std::string s) {
  const auto h = s.find('#');
  if (h != std::string::npos && s.find('"') == std::string::npos) s.resize(h);
  while (!s.empty() && std::strchr(" \t\r", s.front())) s.erase(0, 1);
  while (!s.empty() && std::strchr(" \t\r", s.back())) s.pop_back();
  return s;
}

std::string Valor(const std::string& v) {
  std::string s = Limpia(v);
  if (!s.empty() && s.front() == '"') {
    const auto z = s.find('"', 1);
    return s.substr(1, z == std::string::npos ? std::string::npos : z - 1);
  }
  const auto h = s.find('#');
  return Limpia(h == std::string::npos ? s : s.substr(0, h));
}

void Restaurar(uint8_t* base) {
  if (g_activa < 0) return;
  std::memcpy(base + kRecs + kAnfitrion * kRec, g_orig, kRec);
  REX_STORE_U32(kTemas + 4 * kAnfitrion, g_orig_tema);
  if (g_niebla) std::memcpy(base + g_niebla, g_orig_niebla, sizeof(g_orig_niebla));
  g_niebla = 0;
  g_activa = -1;
}

void MarcarUvAnim(ObjetoUv& o);   // definida con las funciones UV (mas abajo)

// EU 0x1E1B40 Stage_SetupObjects: orden del original s_Ground -> estaticos -> extras -> dinamicos -> Stub (vacio) ->
// w_s_Global(0) solo en la variante 0. El conversor ya monta suelo, estaticos y extras en el #ZDD del alta; aqui se
// decide que piezas de este fichero se activan (clima, polvo, destellos y objeto global) con la variante real de la
// tabla 0x44ABC4+4, que stage.toml guarda en `variante_escena`.
// EU 0x1E2470 Stage_SetupObjectsGlobalVar1: mismo orden, pero w_s_Global(0) solo en la variante 1.
// EU 0x1E1E90 Stage_SetupObjectsWithClimate: variante 1 ClimateScript; variante 0 w_s_Global(0).
// EU 0x1E23C0 Stage_SetupObjectsWithSandDust: variante 1 ClimateScript; variante 0 w_s_Global(0) + s_Sand_Dust.
// EU 0x1E1BA0 Stage_SetupObjectsWithLensFlare: s_LensFlare SIEMPRE; w_s_Global(0) solo en la variante 0.
// EU 0x1E24E0 Stage_SetupObjectsLensFlareB: s_LensFlare SIEMPRE; variante 0 w_s_Global(0), variante 1 w_s_Global(2).
// EU 0x1E30C0 Stage_SetupObjectsAnimUV: s_LensFlare SIEMPRE; variante 1 objeto UV + w_s_Global(2), variante 0 oculta.
// EU 0x1E3530 Stage_UpdateObjectsAndClimate: variante 1 ClimateScript; variante 0 w_s_Global(2) (sin global(0)).
// EU 0x1E3760 Stage_UpdateObjectsClimateLensFlare: variante 1 ClimateScript; variante 0 s_LensFlare + s_Sand_Dust.
// EU 0x1E4160 Stage_UpdateObjectsLensFlareFileIndex: s_LensFlare SIEMPRE; variante 0 w_s_Global(0) + objeto UV (3,1).
// DS78: el clima NO se monta por el simple hecho de declararlo; solo los setups que llaman a ClimateScript/
// s_Sand_Dust lo encienden (antes un `clima`/`polvo` en un Stage_SetupObjects o LensFlareB se dibujaba de mas).
void MontarObjetos(Alta& a) {
  const std::string& s = a.setup;
  a.usa_clima = a.usa_polvo = a.usa_global = a.usa_destello = false;
  if (s == "Stage_SetupObjectsGlobalVar1") {
    a.usa_global = a.variante_escena == 1;
  } else if (s == "Stage_SetupObjects") {   // 0x1E1B40: sin clima ni polvo
    a.usa_global = a.variante_escena == 0;
  } else if (s == "Stage_SetupObjectsWithClimate") {
    a.usa_clima = a.variante_escena == 1;
    a.usa_global = a.variante_escena == 0;
  } else if (s == "Stage_SetupObjectsWithSandDust") {
    a.usa_clima = a.variante_escena == 1;
    a.usa_polvo = a.variante_escena == 0;
    a.usa_global = a.variante_escena == 0;
  } else if (s == "Stage_SetupObjectsWithLensFlare" || s == "Stage_SetupObjectsLensFlareB") {
    a.usa_destello = true;                  // s_LensFlare se llama en las dos variantes
    a.usa_global = a.variante_escena == 0;  // la variante 1 de LensFlareB usa w_s_Global(2), no modelado [H]
  } else if (s == "Stage_SetupObjectsAnimUV") {
    a.usa_destello = true;                  // s_LensFlare se llama en las dos variantes
  } else if (s == "Stage_UpdateObjectsAndClimate") {
    a.usa_clima = a.variante_escena == 1;   // variante 0: w_s_Global(2), no global(0)
  } else if (s == "Stage_UpdateObjectsClimateLensFlare") {
    a.usa_clima = a.variante_escena == 1;
    a.usa_polvo = a.variante_escena == 0;
    a.usa_destello = a.variante_escena == 0;   // s_LensFlare solo en la variante 0
  } else if (s == "Stage_UpdateObjectsLensFlareFileIndex") {
    a.usa_destello = true;                  // s_LensFlare en las dos variantes
    a.usa_global = a.variante_escena == 0;
  } else if (s == "s_Ground" || s == "s_Trs_Ground") {
    // Solo suelo (y cielo en s_Trs_Ground): sin global ni clima; el resto lo pone el #ZDD convertido.
  } else {   // setup desconocido: como Stage_SetupObjects (0x1E1B40), sin clima
    a.usa_global = a.variante_escena == 0;
  }
  for (ObjetoUv& o : a.uvobjetos) MarcarUvAnim(o);   // Stage_CreateAnimUVObject: Anim_SetUvAnimFlag
  REXLOG_INFO("dbz3 escenarios: montaje '{}' variante {} -> suelo/estaticos/extras/dinamicos, clima {}, polvo {}, "
              "destello {}, global {}, objetos UV {}",
              s.empty() ? "Stage_SetupObjects" : s, a.variante_escena, a.usa_clima, a.usa_polvo, a.usa_destello,
              a.usa_global, a.uvobjetos.size());
}

void Activar(uint8_t* base, int v) {
  if (g_activa == v) return;
  Restaurar(base);
  Alta& a = g_altas[size_t(v - kSerie)];
  const uint32_t rec = kRecs + kAnfitrion * kRec;
  std::memcpy(g_orig, base + rec, kRec);
  g_orig_tema = REX_LOAD_U32(kTemas + 4 * kAnfitrion);
  const uint32_t tema = REX_LOAD_U32(kTemas + 4 * uint32_t(a.tema));
  const uint32_t ni = REX_LOAD_U32(rec + 0x1C + 4);   // entrada de niebla del anfitrion (grupo 0)
  std::memset(base + rec, 0, kRec);                 // sin grupos de objetos (como R&R BASE)
  std::strncpy(reinterpret_cast<char*>(base + rec), a.nombre.c_str(), 15);
  REX_STORE_U32(rec + 0x10, a.entrada);
  REX_STORE_U32(rec + 0x14, a.id_se);
  REX_STORE_U32(kTemas + 4 * kAnfitrion, tema);
  if (a.niebla && ni < 64) {   // R8: la del alta en la entrada del anfitrion (se devuelve en Restaurar)
    g_niebla = kNieblas + ni * 20;
    std::memcpy(g_orig_niebla, base + g_niebla, sizeof(g_orig_niebla));
    uint32_t ini, fin;
    std::memcpy(&ini, &a.niebla_ini, 4);
    std::memcpy(&fin, &a.niebla_fin, 4);
    REX_STORE_U32(g_niebla, a.niebla_color);
    REX_STORE_U32(g_niebla + 12, ini);
    REX_STORE_U32(g_niebla + 16, fin);
    // DS88: +4/+8 no se tocan (se quedan como los del anfitrion, 0/255). IW guarda sus +0x20/+0x24 ya
    // complementados (FUN_0012c960: 255 - valor) y B3 (sub_820856B8) usa esos dos floats como color de fondo;
    // copiar el valor crudo del #MCF ponia el fondo/niebla lejana en azul (900: 10/255 -> 0,63,187), que es lo
    // que se veia como mesetas azules planas en el horizonte. Los mods v22..v24 traen el par en stage.toml: se
    // ignora aqui para que tambien se arreglen sin regenerar.
    (void)a.niebla_extra;
    REX_STORE_U32(rec + 0x1C + 4, ni);   // variantes 0 y 1
    REX_STORE_U32(rec + 0x3C + 4, ni);
  }
  g_activa = v;
  MontarObjetos(a);
  REXLOG_INFO("dbz3 escenarios: alta {} '{}' (mod {}) en el registro {}: entrada {}, id_se {}, tema {}", v,
              a.nombre, a.mod, kAnfitrion, a.entrada, a.id_se, a.tema);
}

// Indice que debe ver el juego: el anfitrion para un alta; de serie tal cual (y el anfitrion vuelve a
// ser el original si estaba prestado).
int Traducir(uint8_t* base, int i) {
  if (i >= kSerie && i < Total()) {
    Activar(base, i);
    return kAnfitrion;
  }
  if (i >= 0 && i < kSerie) Restaurar(base);
  return i;
}

// Frente 07: obj del escenario del combate (lo da sub_820C26E0), para poder recargar el escenario al
// pasar de zona (como hace el juego original de B2 al caer por el borde: recarga el escenario sin tocar
// el combate, FUN_001b95b0 en b2.c).
void RecordarObj(uint32_t obj) { g_obj_combate = obj; }

// Registro del alta que es la zona B del alta v (su stage.toml lleva `zona_b = entrada de v`), o -1.
int ZonaBDe(int v) {
  if (v < kSerie || v >= Total()) return -1;
  const uint32_t entrada = g_altas[size_t(v - kSerie)].entrada;
  for (size_t k = 0; k < g_altas.size(); ++k)
    if (g_altas[k].zona_b == entrada) return kSerie + int(k);
  return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// ZB 25/10: ESCENA del cambio de zona. El #SPX original de B2 hace: camara clip 0 (el suceso) -> al acabar su espera, cambio de
// zona (builtin 0x40) -> camara clip 1 (-> clip 2 en Llanura); la escena dura lo que suman los clips (a 60 cuadros/s; nieve 420 + 270).
// El conversor deja esos clips como ultimo #ACC de la lista de camaras del objeto 0 (stage.toml `zona_escena` = cuantos); aqui se
// leen del #ZDD cargado, se oculta el marcador y la camara del juego se pisa al empezar el dibujo (AplicarCamaraEscena).
// ---------------------------------------------------------------------------------------------------------------------
// Luchadores quietos mientras dura (como cinematica.cpp): el humano sigue en su mando, que deja de leerse (se inyecta 0); el COM pasa al
// del otro lado (su IA para). X = mgr + 2096 + 8240 * lado, X+5028 = mando (16 bits, -1 = COM).
int16_t g_esc_mando[2] = {-1, -1};
int g_esc_usuario[2] = {-1, -1};
bool g_esc_cambiado[2] = {};

struct Clave { uint32_t f; float v[3]; };
struct Clip {
  uint32_t n = 0;                      // cuadros del clip
  std::vector<Clave> pista[4];         // ojo, objetivo (3 valores), giro, fov (1 valor)
};
struct Escena {
  bool activa = false, cambiada = false;
  uint32_t frame = 0, total = 0;
  std::vector<Clip> clips;
  std::vector<uint32_t> inicio;        // cuadro de la escena en que empieza cada clip (el cambio de zona va en inicio[1])
};
Escena g_esc;
std::atomic<bool> g_esc_cam{false};    // hay camara de escena para este cuadro
std::atomic<uint32_t> g_esc_dibujos{0};   // cuadros dibujados desde que empezo (el dibujo es 1 por cuadro; la comprobacion de la pelea es 2 por cuadro, 1 por luchador)
float g_esc_ojo[3], g_esc_obj[3], g_esc_giro = 0, g_esc_fov = 0;

uint8_t* BaseGuest() {
  auto* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}

float LeeFl(uint32_t a) {
  uint8_t* base = BaseGuest();
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// Puntero del #ZDD cargado: el reubicador del juego deja direcciones absolutas; por si quedaran desplazamientos, los < base se suman.
uint32_t Abs(uint32_t base, uint32_t v) { return v < base ? base + v : v; }

bool LeerClips(uint32_t cuantos, std::vector<Clip>& out) {
  uint8_t* base = BaseGuest();
  if (!base) return false;
  const uint32_t z = REX_LOAD_U32(kG + 32);
  if (!z) return false;
  const uint32_t ol = Abs(z, REX_LOAD_U32(z + 0x20));
  const uint32_t ob = ol ? Abs(z, REX_LOAD_U32(ol)) : 0;
  const uint32_t n_cam = ob ? REX_LOAD_U32(ob + 0xD4) : 0;
  if (n_cam == 0 || n_cam > 64) return false;
  const uint32_t lst = Abs(z, REX_LOAD_U32(ob + 0xD8));
  const uint32_t acc = Abs(z, REX_LOAD_U32(lst + 4 * (n_cam - 1)));   // el ultimo bloque: el del suceso
  if (std::getenv("DBZ3_TEST_ESCENA_VOLCADO")) {   // diagnostico: estructura del #ZDD cargado
    REXLOG_INFO("dbz3 escenarios: volcado zdd=0x{:08X} lista objetos=0x{:08X} obj0=0x{:08X} n_cam={} lista=0x{:08X}", z, ol, ob, n_cam, lst);
    for (uint32_t k = 0; k < n_cam; ++k) {
      const uint32_t e = REX_LOAD_U32(lst + 4 * k);
      REXLOG_INFO("  cam {} ptr=0x{:08X} -> {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", k, e, REX_LOAD_U32(Abs(z, e)),
                  REX_LOAD_U32(Abs(z, e) + 4), REX_LOAD_U32(Abs(z, e) + 8), REX_LOAD_U32(Abs(z, e) + 12),
                  REX_LOAD_U32(Abs(z, e) + 16), REX_LOAD_U32(Abs(z, e) + 20));
    }
    for (uint32_t o = 0xC0; o < 0xE8; o += 4) REXLOG_INFO("  obj0+0x{:X} = 0x{:08X}", o, REX_LOAD_U32(ob + o));
  }
  if ((REX_LOAD_U32(acc) & 0xFFFFFFu) != 0x414343u) {   // "#ACC" (el juego lo deja como "!ACC" al reubicarlo)
    REXLOG_WARN("dbz3 escenarios: escena de zona: el ultimo bloque de camaras (0x{:08X}) no es un #ACC", acc);
    return false;
  }
  const uint32_t n_clips = REX_LOAD_U32(acc + 0x10), tab = Abs(acc, REX_LOAD_U32(acc + 0x14));
  if (n_clips < cuantos || n_clips > 64) return false;
  for (uint32_t c = 0; c < cuantos; ++c) {
    Clip clip;
    clip.n = REX_LOAD_U32(tab + 16 * c + 8);
    const uint32_t cl = Abs(acc, REX_LOAD_U32(tab + 16 * c + 12));
    for (uint32_t j = 0; j < 4; ++j) {
      const uint32_t p = Abs(acc, REX_LOAD_U32(cl + 4 * j));
      if (!p) return false;
      const uint32_t nk = REX_LOAD_U32(p + 8), paso = j < 2 ? 16 : 8;
      if (nk == 0 || nk > 20000) return false;
      for (uint32_t k = 0; k < nk; ++k) {
        const uint32_t q = p + 12 + paso * k;
        Clave cv{REX_LOAD_U32(q), {LeeFl(q + 4), j < 2 ? LeeFl(q + 8) : 0.0f, j < 2 ? LeeFl(q + 12) : 0.0f}};
        clip.pista[j].push_back(cv);
      }
    }
    if (clip.n == 0 || clip.n > 5000) return false;
    out.push_back(std::move(clip));
  }
  return true;
}

// Valor de una pista en el cuadro f (lineal; fuera de rango = la ultima clave).
void Muestra(const std::vector<Clave>& k, float f, float v[3], int comp) {
  size_t i = 0;
  while (i + 1 < k.size() && float(k[i + 1].f) <= f) ++i;
  const Clave& a = k[i];
  if (i + 1 >= k.size() || f <= float(a.f)) {
    for (int c = 0; c < comp; ++c) v[c] = a.v[c];
    return;
  }
  const Clave& b = k[i + 1];
  const float t = (f - float(a.f)) / float(std::max<uint32_t>(1, b.f - a.f));
  for (int c = 0; c < comp; ++c) v[c] = a.v[c] + (b.v[c] - a.v[c]) * t;
}

// ---------------------------------------------------------------------------------------------------------------------
// DS73: animacion UV por objeto del escenario (#MTA). Traduccion de UvAnim_EvalChannel (EU 0x1BAB60),
// UvAnim_EvalAllChannels (0x1BAD80), UvAnim_CopyStateToGlobals (0x1BAA90) y de los callbacks de escenario
// 0x1DD2D0/0x1DD310/0x1DD3B0/0x1DDD80/0x1E2C60/0x1E2DF0/0x1E2ED0. Ningun numero es inventado: los canales y sus
// claves salen del #MTA (campos_iw.py) y el avance es fRam0049c7dc = 1 por cuadro a 60 Hz.
// ---------------------------------------------------------------------------------------------------------------------

// EU 0x1BAB60 UvAnim_EvalChannel: valor de un descriptor en el cuadro t y avance de los acumuladores u/v.
// El original: si no hay claves o t<0 usa el valor por defecto; si t supera el bucle lo envuelve y suma la parte
// entera; busca la clave y interpola; la ultima clave usa fin de bucle como extremo (l.155043-155114).
float EvalUvCanal(const UvCanal& c, int d, float t) {
  if (d < 0 || d > 5 || c.nk[d] <= 0 || t < 0.0f) return c.defecto[d];
  const int loop = c.bucle[d];
  int ti = int(t);
  float frac = 0.0f;
  if (loop > 0 && loop < ti) {
    ti %= loop;
    frac = t - float(int(t));
  }
  float f = float(ti) + frac;
  const auto& k = c.claves[d];
  const int nk = std::min<int>(c.nk[d], int(k.size()));
  if (nk <= 0) return c.defecto[d];
  int i = 1;
  while (i < nk && !(f < float(k[size_t(i)].f))) ++i;
  const int ia = std::max(0, i - 1);
  const UvClave& a = k[size_t(ia)];
  if (ia == nk - 1) {
    // Original (l.155090-155107): la ultima clave interpola hacia la PRIMERA (piVar6 sigue apuntando al inicio) y
    // usa el fin del bucle como cuadro final.
    const float fin = loop > a.f ? float(loop) : float(a.f + 1);
    const float u = fin > float(a.f) ? (f - float(a.f)) / (fin - float(a.f)) : 0.0f;
    return a.v * (1.0f - u) + k[0].v * u;
  }
  const UvClave& b = k[size_t(ia + 1)];
  const float u = b.f > a.f ? (f - float(a.f)) / float(b.f - a.f) : 0.0f;
  return a.v * (1.0f - u) + b.v * u;
}

// EU 0x1DD310 Task_UvAnimEvalChannels: evalua todos los canales activos del objeto (UvAnim_EvalAllChannels).
// EU 0x1BAD80 UvAnim_EvalAllChannels
void EvaluarCanalesUv(ObjetoUv& o) {
  const uint32_t mascara = o.mascara ? o.mascara : (o.canales.size() >= 16 ? 0xFFFFu : ((1u << o.canales.size()) - 1u));
  for (size_t ch = 0; ch < o.canales.size() && ch < 16; ++ch) {
    if (!(mascara & (1u << ch))) continue;
    const UvCanal& c = o.canales[ch];
    float* s = o.estado[ch];
    for (int d = 0; d < 6; ++d) s[d] = EvalUvCanal(c, d, o.tiempo);
    s[6] += s[4];
    s[7] += s[5];
    s[6] -= float(int(s[6]));
    s[7] -= float(int(s[7]));
  }
}

// EU 0x1DD3B0 Anim_SetUvAnimFlag: marca el objeto como UV animado (escribe 1 en +0xC de la entrada).
void MarcarUvAnim(ObjetoUv& o) { o.flags |= 0x100; }

// EU 0x1DD2D0 Task_UvAnimCopyStateToGlobals: copia la matriz UV (estado 0..3) y los acumuladores u/v (6/7) a los
// registros compartidos del motor (0x4c4f20 y 0x4c4e20/0x4c4e60). En B3 esos registros son del GS de PS2; se
// conservan aqui para que la evaluacion sea completa y para el que pinte el modelo (ver AplicarObjetoUv).
void CopiarEstadoUv(const ObjetoUv& o, float matriz[][4], float u[], float v[]) {
  for (size_t ch = 0; ch < o.canales.size() && ch < 16; ++ch) {
    for (int d = 0; d < 4; ++d) matriz[ch][d] = o.estado[ch][d];
    u[ch] = o.estado[ch][6];
    v[ch] = o.estado[ch][7];
  }
}

// EU 0x1E2DF0 Stage_AnimObjectDraw: deja preparado el dibujo del objeto (copia estado a globales y aplica la
// niebla por flags). El modelo lo pinta el renderizador de B3 porque viaja en el #ZDD del alta; aqui se aplica el
// estado UV a sus ventanas, que es lo unico que cambia lo que se ve.
void AplicarObjetoUv(ObjetoUv& o) {
  if (!o.guest || !o.n) return;
  uint8_t* base = BaseGuest();
  if (!base) return;
  const uint32_t mascara = o.mascara ? o.mascara : (o.canales.size() >= 16 ? 0xFFFFu : ((1u << o.canales.size()) - 1u));
  for (size_t ch = 0; ch < o.canales.size() && ch < 16; ++ch) {
    if (!(mascara & (1u << ch))) continue;
    // El estado UV es absoluto (se recalcula del modelo base al dibujar); las ventanas del guest ya llevan el delta
    // anterior, asi que se escribe la diferencia.
    const float du = o.estado[ch][6] - o.aplicado_u[ch], dv = o.estado[ch][7] - o.aplicado_v[ch];
    o.aplicado_u[ch] = o.estado[ch][6];
    o.aplicado_v[ch] = o.estado[ch][7];
    if (du == 0.0f && dv == 0.0f) continue;
    for (uint32_t i = 0; i < o.n; ++i) {
      const uint32_t q = o.guest + 44 * i + 36;
      float u, v;
      uint32_t b0 = REX_LOAD_U32(q), b1 = REX_LOAD_U32(q + 4);
      std::memcpy(&u, &b0, 4);
      std::memcpy(&v, &b1, 4);
      u += du;
      v += dv;
      std::memcpy(&b0, &u, 4);
      std::memcpy(&b1, &v, 4);
      REX_STORE_U32(q, b0);
      REX_STORE_U32(q + 4, b1);
    }
  }
}

// EU 0x1E2C60 Stage_AnimObjectUpdateUVScroll: avanza el tiempo del objeto, evalua sus canales y aplica la deriva a
// las ventanas del modelo. Despues el original llama a Anim_StepMaterialAnim / Anim_StepModelAnim / Anim_StepUvAnim
// (las tres funciones del nodo): las dos primeras las lleva PasoCapas con los datos del stage.toml y la tercera es
// este mismo estado.
void ActualizarObjetoUv(ObjetoUv& o, float dt) {
  o.tiempo += 60.0f * dt;   // Stage_UpdateObjAnims l.394132: + fRam0049c7dc (1 a 60 Hz, como PasoCapas)
  EvaluarCanalesUv(o);
  AplicarObjetoUv(o);
}

// EU 0x1E2ED0 Stage_CreateAnimUVObject: crea el objeto animado de la entrada (0,1) del escenario con sus canales
// #MTA. Los canales salen de la lista de la variante (0x44ABE0): los IDs >=100 son objetos UV (objeto = ID-100).
// Aqui los trae stage.toml (`uvobjeto`/`uvcanal`, campos_iw.py) porque esa tabla vive en el ELF.
void CrearObjetoUv(Alta& a, const std::string& v) {
  ObjetoUv o;
  int entrada = 1;
  unsigned mascara = 0;
  unsigned n = 0;
  char hex[600] = {};
  const int campos = std::sscanf(v.c_str(), "%d,%u,%u,%599[0-9A-Fa-f]", &entrada, &mascara, &n, hex);
  if (campos < 2) return;
  o.entrada = entrada;
  o.mascara = mascara & 0xFFFFu;
  o.n = n;
  if (campos >= 4 && hex[0]) {
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
      const char b2[3] = {hex[i], hex[i + 1], 0};
      o.firma.push_back(uint8_t(std::strtoul(b2, nullptr, 16)));
    }
  }
  MarcarUvAnim(o);
  a.uvobjetos.push_back(std::move(o));
}

// `uvcanal = "<canal>;<n>,<bucle>,<defecto>,<cuadro=valor|...>;... (6 descriptores)"`. Se anade al ultimo
// `uvobjeto`; los descriptores que falten quedan sin claves (valor por defecto 0).
void AnadirCanalUv(Alta& a, const std::string& v) {
  if (a.uvobjetos.empty()) return;
  ObjetoUv& o = a.uvobjetos.back();
  const size_t sc = v.find(';');
  const int ch = std::atoi(v.substr(0, sc).c_str());
  if (ch < 0 || ch >= 16) return;
  if (size_t(ch) >= o.canales.size()) o.canales.resize(size_t(ch) + 1);
  UvCanal& c = o.canales[size_t(ch)];
  size_t ini = sc == std::string::npos ? v.size() : sc + 1;
  int d = 0;
  while (ini < v.size() && d < 6) {
    size_t fin = v.find(';', ini);
    if (fin == std::string::npos) fin = v.size();
    const std::string desc = v.substr(ini, fin - ini);
    unsigned nk = 0, bucle = 0;
    float defecto = 0.0f;
    int usado = 0;
    const int leidos = std::sscanf(desc.c_str(), "%u,%u,%f%n", &nk, &bucle, &defecto, &usado);
    if (leidos >= 3) {
      c.nk[d] = int(nk);
      c.bucle[d] = int(bucle);
      c.defecto[d] = defecto;
      if (usado > 0) {
        size_t p = size_t(usado);
        while (p < desc.size() && desc[p] == ',') ++p;
        while (p < desc.size() && c.claves[d].size() < nk) {
          size_t pe = desc.find('|', p);
          if (pe == std::string::npos) pe = desc.size();
          const std::string kv = desc.substr(p, pe - p);
          const size_t eq = kv.find('=');
          if (eq != std::string::npos)
            c.claves[d].push_back(UvClave{int32_t(std::strtol(kv.substr(0, eq).c_str(), nullptr, 10)),
                                          std::strtof(kv.substr(eq + 1).c_str(), nullptr)});
          p = pe + 1;
        }
      }
    }
    ini = fin + 1;
    ++d;
  }
}

// EU 0x1DDD80 Task_RunUpdateCallbacksFlag2800: recorre los objetos del alta y llama a su update solo si llevan los
// bits 0x2800 (el original omite los demas en este pase). El objeto UV de Stage_CreateAnimUVObject lleva 0x100 y por
// tanto no entra; los que si lo declaren en stage.toml (`uvobjeto` con flags 0x2800) se actualizan aqui.
void PasarCallbacks2800(Alta& a, float dt) {
  for (ObjetoUv& o : a.uvobjetos)
    if ((o.flags & 0x2800) == 0x2800) ActualizarObjetoUv(o, dt);
}

// El cambio de zona del propio motor (sub_820CC6C0: suceso + G+0x40 ^= 1 + luz y niebla); pasa por el gancho que lo bloquea en las altas.
void CambiarZonaMotor(PPCContext& ctx, uint8_t* base) {
  PPCContext c = ctx;
  g_zona_motor = true;
  DBZ3_CALL_EU(sub_820CC6C0)(c, base);
  g_zona_motor = false;
  dbz3::clima::EscenarioPedido();
}

void SoltarLuchadores() {
  uint8_t* base = BaseGuest();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + 2096 + 8240 * k;
    if (mgr && g_esc_cambiado[k] && int16_t(REX_LOAD_U16(x + 5028)) == g_esc_usuario[k])
      REX_STORE_U16(x + 5028, uint16_t(g_esc_mando[k]));
    if (g_esc_usuario[k] >= 0) dbz3::net::InyectarPad(uint32_t(g_esc_usuario[k]), -1);
    g_esc_cambiado[k] = false;
    g_esc_usuario[k] = -1;
  }
}

void QuietarLuchadores() {
  uint8_t* base = BaseGuest();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  if (!mgr) return;
  for (uint32_t k = 0; k < 2; ++k) g_esc_mando[k] = int16_t(REX_LOAD_U16(mgr + 2096 + 8240 * k + 5028));
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + 2096 + 8240 * k;
    const int16_t otro = g_esc_mando[1 - k];
    g_esc_cambiado[k] = g_esc_mando[k] < 0 || g_esc_mando[k] > 3;
    const int u = g_esc_cambiado[k] ? (otro >= 0 && otro <= 3 ? otro : 0) : g_esc_mando[k];
    g_esc_usuario[k] = u;
    dbz3::net::InyectarPad(uint32_t(u), 0);
    if (g_esc_cambiado[k]) REX_STORE_U16(x + 5028, uint16_t(u));
  }
}

void TerminarEscena(const char* por) {
  if (!g_esc.activa) return;
  g_esc.activa = false;
  g_esc_cam = false;
  SoltarLuchadores();
  dbz3::combate::OcultarHud(false);
  REXLOG_INFO("dbz3 escenarios: escena de zona {} en el cuadro {} de {} (zona cambiada: {})", por, g_esc.frame, g_esc.total,
              g_esc.cambiada);
  g_esc.clips.clear();
}

bool EmpezarEscena(uint32_t cuantos) {
  Escena e;
  if (!LeerClips(cuantos, e.clips)) return false;
  uint32_t t = 0;
  for (const Clip& c : e.clips) {
    e.inicio.push_back(t);
    t += c.n;
  }
  e.total = t;
  e.activa = true;
  g_esc = std::move(e);
  g_esc_dibujos = 0;
  QuietarLuchadores();
  dbz3::combate::OcultarHud(true);
  std::string d;
  for (const Clip& c : g_esc.clips) d += " " + std::to_string(c.n);
  REXLOG_INFO("dbz3 escenarios: escena de zona: empieza ({} clips de camara:{} cuadros; {:.1f} s)", g_esc.clips.size(), d,
              double(g_esc.total) / 60.0);
  return true;
}

// Cada comprobacion de la pelea (ComprobarZonaB, 2 por cuadro medido: una por luchador): el cuadro de la escena es el del dibujo;
// el cambio de zona va al empezar el clip 1.
void TickEscena(PPCContext& ctx, uint8_t* base) {
  if (!g_esc.activa) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || REX_LOAD_U32(mgr + kEstadoCombate) != 4) {   // la pelea acabo o se salio
    TerminarEscena("cortada");
    return;
  }
  g_esc.frame = g_esc_dibujos.load();
  if (g_esc.frame >= g_esc.total) {
    TerminarEscena("terminada");
    return;
  }
  if (!g_esc.cambiada && g_esc.frame >= g_esc.inicio[1]) {
    g_esc.cambiada = true;
    CambiarZonaMotor(ctx, base);
    REXLOG_INFO("dbz3 escenarios: escena de zona: cambio de zona del motor en el cuadro {} (G+64={})", g_esc.frame,
                REX_LOAD_U32(kG + 64));
  }
  size_t i = g_esc.clips.size() - 1;
  while (i > 0 && g_esc.frame < g_esc.inicio[i]) --i;
  const Clip& c = g_esc.clips[i];
  const float f = float(g_esc.frame - g_esc.inicio[i]);
  float ojo[3], obj[3], giro[3] = {}, fov[3] = {};
  Muestra(c.pista[0], f, ojo, 3);
  Muestra(c.pista[1], f, obj, 3);
  Muestra(c.pista[2], f, giro, 1);
  Muestra(c.pista[3], f, fov, 1);
  std::memcpy(g_esc_ojo, ojo, sizeof(ojo));
  std::memcpy(g_esc_obj, obj, sizeof(obj));
  g_esc_giro = giro[0];
  g_esc_fov = fov[0];
  g_esc_cam = true;
  static int nlog = -1;
  static const bool cam_log = std::getenv("DBZ3_TEST_ESCENA_VOLCADO") != nullptr;   // DS189: traza solo en pruebas
  if (cam_log && g_esc.frame % 60 == 0 && nlog != int(g_esc.frame))
    REXLOG_INFO("dbz3 escenarios: escena de zona cuadro {} clip {}/{} ojo ({:.1f} {:.1f} {:.1f}) obj ({:.1f} {:.1f} {:.1f}) fov {:.2f}",
                g_esc.frame, i, g_esc.clips.size(), ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2], fov[0]);
  nlog = int(g_esc.frame);
}

// Ring out con zona B (dbz3_escenarios_zonas_b): el alta cargada tiene zona B -> se recarga el escenario
// del combate con el bin de la zona B (su objeto 0 es la otra zona; las camaras de su objeto 0 son las de
// la transicion de B2). Devuelve true si ha hecho el cambio (el llamador consume el aviso de ring out).
bool PasarZonaB(PPCContext& ctx, uint8_t* base) {
  // X1esc 23/10: el cambio de zona es el del propio motor (sub_820CC6C0: suceso + G+0x40 ^= 1 + regenera luz y niebla).
  // Los bins convertidos de B2 de los grupos 0, 4, 7 y 8 ya traen la zona B como objeto 1 del #ZDD (n_obj = 2). La
  // recarga entera del escenario (sub_820C26E0) que se probaba antes dejaba el objeto +16 a cero y cerraba el juego
  // (lr 0x820BD300, sub_820BE2E8).
  if (g_activa < 0) return false;
  // Base Red Ribbon B2 (entrada 5000): el cambio cierra el juego (lectura de guest 0x294 en sub_82090820: un objeto nulo
  // + 0x290, un cuaternio); medido 23/10 aun con 2 raices. Sin zona B hasta saber que objeto le falta. Montanas nevadas
  // (5007), Ciudad (5008) y Llanura (5004) probados: cambian bien.
  if (g_altas[size_t(g_activa - kSerie)].entrada == 5000 && !REXCVAR_GET(dbz3_escenarios_base_zona_b)) return false;
  const uint32_t zdd = REX_LOAD_U32(kG + 32);
  if (!zdd || REX_LOAD_U32(zdd + 4) < 2 || REX_LOAD_U32(kG + 64) != 0) {   // sin zona B o ya en ella
    static std::atomic<int> nn{0};
    if (nn.fetch_add(1) < 3)
      REXLOG_INFO("dbz3 escenarios: sin cambio de zona: zdd=0x{:08X} n_obj={} G+64={}", zdd, zdd ? REX_LOAD_U32(zdd + 4) : 0, REX_LOAD_U32(kG + 64));
    return false;
  }
  if (g_esc.activa) return true;   // la escena ya esta en marcha: el aviso se consume sin hacer nada
  const uint32_t n_escena = g_altas[size_t(g_activa - kSerie)].escena;
  // Online no: el cambio de zona iria al cuadro de DIBUJO de cada PC y los dos se desincronizarian (corte directo, como antes).
  if (n_escena >= 2 && REXCVAR_GET(dbz3_escenarios_zona_escena) && !dbz3::net::Session::Get().active() && EmpezarEscena(n_escena)) {
    REXLOG_INFO("dbz3 escenarios: ring out (aviso {}): escena de cambio de zona (alta {})",
                REX_LOAD_U8(REX_LOAD_U32(kMgrPtr) + kAvisoRing), g_activa);
    return true;
  }
  CambiarZonaMotor(ctx, base);
  REXLOG_INFO("dbz3 escenarios: ring out (aviso {}): cambio de zona del motor (alta {}, zona B = objeto 1)",
              REX_LOAD_U8(REX_LOAD_U32(kMgrPtr) + kAvisoRing), g_activa);
  return true;
}

}  // namespace

int Total() { return kSerie + int(g_altas.size()); }
int Visibles() { return int(g_visibles); }

bool AltaActiva() { return g_activa >= 0; }

int PorMod(const std::string& mod) {
  for (size_t i = 0; i < g_altas.size(); ++i)
    if (g_altas[i].mod == mod) return kSerie + int(i);
  return -1;
}

int CampoIW(int campo) { return PorMod("Campo IW " + std::to_string(campo)); }   // altas de mod center hd/campos_iw.py

void OcultarFondo(bool ocultar) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base) return;
  constexpr uint32_t kFondo = 0x4C;   // G+0x4C bit0: sub_820C1E98 lo lee; sub_820C1E68/1E80 lo ponen/quitan
  const uint32_t v = REX_LOAD_U32(kG + kFondo);
  if (ocultar) {
    if (g_fondo < 0) g_fondo = int(v & 1u);
    if (v & 1u) {
      REX_STORE_U32(kG + kFondo, v & ~1u);
      REXLOG_INFO("dbz3 escenarios: fondo del escenario apagado (G+0x4C=0x{:08X})", v & ~1u);
    }
  } else if (g_fondo >= 0) {
    const uint32_t w = (v & ~1u) | uint32_t(g_fondo);
    REX_STORE_U32(kG + kFondo, w);
    REXLOG_INFO("dbz3 escenarios: fondo del escenario restaurado (G+0x4C=0x{:08X})", w);
    g_fondo = -1;
  }
}

// Nombres oficiales: los rotulos del selector (data_<idioma>.afs 2043, textura 0, una fila por casilla),
// copiados a texto. Fila de cada registro: las variantes (ISLAND B, TOWN B, NAMEC B, KAIOU B y EARTH, que
// usa el mapa de ROCK) llevan el nombre de su mapa.
std::string Nombre(int registro, int idioma) {
  static const char* const kFilas[12][5] = {   // ingles, aleman, frances, espanol, italiano
      {"Red Ribbon Base", "Red Ribbon Basis", "Base Ruban rouge", "Base de la Red Ribbon", "Base Red Ribbon"},
      {"Grandpa Gohan's House", "Großvaters Haus", "Chez Grand-père", "Casa del abuelo", "Casa del nonno"},
      {"Inside Buu", "In Buu", "Entrailles de Buu", "Dentro de Bu", "Dentro Bu"},
      {"Archipelago", "Archipel", "Archipel", "Archipiélago", "Arcipelago"},
      {"Supreme Kai's World", "Kaioshins Welt", "Monde de Kaïo Shin", "Mundo de Kaiyoshin", "Mondo di Kaiohshin"},
      {"Hyperbolic Time Chamber", "Raum v. Geist/Zeit", "Salle Esprit / Temps", "Cámara de Tiempo Hiperbólico",
       "Stanza Tempo e Spirito"},
      {"Planet Namek", "Planet Namek", "Planète Namek", "Planeta Namek", "Pianeta Namek"},
      {"Mountains", "Felsenberg", "Montagnes rocheuses", "Montañas rocosas", "Montagna rocciosa"},
      {"World Tournament Stage", "Stufe Welt-Kampf", "Niveau Champ. du Monde", "Escenario de Torneo Mundial",
       "Livello Torneo mondiale"},
      {"Urban Area", "Stadt", "Zone urbaine", "Zona urbana", "Città"},
      {"Plains", "Ebenen", "Plaines", "Llanuras", "Pianure"},
      {"Cell Ring", "Cell-Ring", "Anneau de Cell", "Ring de Cell", "Ring di Cell"},
  };
  // BUDO, MIND&TIME, ISLAND, TOWN, ROCK, PLANE, GOKU, NAMEC, CELL, KAIOU, BUU BODY, ISLAND B, TOWN B, EARTH,
  // NAMEC B, KAIOU B, R&R BASE
  static const int kFila[kSerie] = {8, 5, 3, 9, 7, 10, 1, 6, 11, 4, 2, 3, 9, 7, 6, 4, 0};
  if (registro >= kSerie && registro < Total()) {
    const Alta& a = g_altas[size_t(registro - kSerie)];
    return a.titulo.empty() ? a.nombre : a.titulo;
  }
  if (registro < 0 || registro >= kSerie) return {};
  const int col = idioma == 3 ? 1 : idioma == 4 ? 2 : idioma == 5 ? 3 : idioma == 6 ? 4 : 0;   // 1, 2: ingles
  return kFilas[kFila[registro]][col];
}

std::string Nombre(int registro) { return Nombre(registro, dbz3::settings::Language()); }

void CargarDeMods(rex::memory::Memory* memory) {
  g_altas.clear();
  g_mem = memory;
  if (!memory) return;
  std::error_code ec;
  for (const auto& info : ListMods(false)) {
    if (!info.enabled) continue;
    const auto file = ModsRoot() / info.name / "stage.toml";
    if (!std::filesystem::is_regular_file(file, ec)) continue;
    std::ifstream in(file);
    std::string line;
    Alta* a = nullptr;
    while (std::getline(in, line)) {
      line = Limpia(line);
      if (line.empty() || line.front() == '#') continue;
      if (line.front() == '[') {
        a = nullptr;
        if (line == "[[escenario]]" || line == "[escenario]") a = &g_altas.emplace_back(Alta{info.name});
        continue;
      }
      const auto eq = line.find('=');
      if (!a || eq == std::string::npos) continue;
      const std::string k = Limpia(line.substr(0, eq)), v = Valor(line.substr(eq + 1));
      if (k == "nombre") a->nombre = v.substr(0, 15);
      else if (k == "titulo") a->titulo = v;
      else if (k == "entrada") a->entrada = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
      else if (k == "id_se") a->id_se = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
      else if (k == "tema") a->tema = std::atoi(v.c_str());
      else if (k == "menu") a->menu = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
      else if (k == "variante") a->variante = std::atoi(v.c_str());   // 12b: registro del Circle
      else if (k == "musica") a->musica = int(std::strtoul(v.c_str(), nullptr, 10));   // 12c: entrada ADX
      else if (k == "zona_escena") a->escena = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
      else if (k == "zona_b") a->zona_b = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
      else if (k == "selector") a->oculta = v == "false" || v == "no" || v == "0";
      else if (k == "nubes") {
        Nube nb;
        char hex[600] = {};
        const int campos = std::sscanf(v.c_str(), "%599[0-9A-Fa-f],%u,%f,%f,%u,%f,%f,%d,%d", hex, &nb.n, &nb.du, &nb.dv,
                                       &nb.tam, &nb.px, &nb.py, &nb.capa, &nb.sigue);
        if (campos < 5)
          std::sscanf(v.c_str(), "%599[0-9A-Fa-f],%u,%f,%f,%u", hex, &nb.n, &nb.du, &nb.dv, &nb.tam);
        if (nb.n > 0 && nb.tam > 0) {
          for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
            const char b2[3] = {hex[i], hex[i + 1], 0};
            nb.firma.push_back(uint8_t(std::strtoul(b2, nullptr, 16)));
          }
          if (nb.firma.size() >= 16) a->nubes.push_back(std::move(nb));
        }
      }
      else if (k == "arrastre") a->arrastre = !(v == "false" || v == "no" || v == "0");
      else if (k == "capas") {
        a->capa_flags = 0;
        std::sscanf(v.c_str(), "%u,%f,%f", &a->capa_flags, &a->capa_ex, &a->capa_ey);
      }
      else if (k == "capa") {
        int L = -1;
        Capa c;
        if (std::sscanf(v.c_str(), "%d,%f,%f,%f,%f", &L, &c.u, &c.v, &c.px, &c.py) == 5 && L >= 0 && L < 8) {
          if (a->capas.empty()) a->capas.resize(8);
          c.acc_u = c.acc_v = 0;
          a->capas[size_t(L)] = c;
        }
      }
      else if (k == "dinamico") {
        Dinamico d;
        char nom[64] = {};
        if (std::sscanf(v.c_str(), "%d,%d,%f,%f,%f,%63[^\n]", &d.modelo, &d.rejilla, &d.x, &d.y, &d.z, nom) == 6) {
          d.nombre = nom;
          a->dinamicos.push_back(std::move(d));
        }
      }
      else if (k == "uvanim") a->uvanim = v;
      else if (k == "uvobjeto") CrearObjetoUv(*a, v);   // DS73: objeto UV animado del escenario (#MTA)
      else if (k == "uvcanal") AnadirCanalUv(*a, v);
      else if (k == "variante_escena") a->variante_escena = std::atoi(v.c_str());   // DS73: iRam004db2c0
      else if (k == "setup") a->setup = v;   // DS63: funcion de montaje del original (informativa)
      else if (k == "clima" || k == "polvo" || k == "particulas") {
        // DS63: "tipo,n,tam,vel,area,alto,RRGGBB,viento[,afs,hijo_amt,tex]"
        ClimaP c;
        char tipo[32] = {};
        if (std::sscanf(v.c_str(), "%31[^,],%u,%f,%f,%f,%f,%x,%f,%u,%u,%u", tipo, &c.n, &c.tam, &c.vel, &c.area,
                        &c.alto, &c.color, &c.viento, &c.afs, &c.hijo_amt, &c.tex) >= 7) {
          c.tipo = tipo;
          if (k == "polvo" || c.tipo.empty()) c.tipo = "dust";
          c.color &= 0xFFFFFFu;
          if (c.n > 0 && c.n <= 4000) a->climas.push_back(std::move(c));
        }
      }
      else if (k == "destello") {
        // DS63: "x,y,z,tam,RRGGBB[,afs,hijo_amt,tex]" (s_LensFlare 0x1DE690)
        DestelloP d;
        if (std::sscanf(v.c_str(), "%f,%f,%f,%f,%x,%u,%u,%u", &d.x, &d.y, &d.z, &d.tam, &d.color, &d.afs,
                        &d.hijo_amt, &d.tex) >= 5) {
          d.color &= 0xFFFFFFu;
          a->destellos.push_back(std::move(d));
        }
      }
      else if (k == "mosaico") {
        // DS63: "paso,afs,amb_hijo,amo,amt,variante" (Stage_DrawTiledGround 0x1E2590). amb_hijo = 0xFFFFFFFF
        // cuando amo/amt son entradas sueltas del AFS.
        MosaicoP m;
        char amb[32] = {};
        if (std::sscanf(v.c_str(), "%f,%u,%31[^,],%u,%u,%d", &m.paso, &m.afs, amb, &m.amo, &m.amt, &m.variante) == 6) {
          m.amb_hijo = (amb[0] == '-' || std::strcmp(amb, "0xFFFFFFFF") == 0) ? 0xFFFFFFFFu
                                                                              : uint32_t(std::strtoul(amb, nullptr, 10));
          m.on = m.paso > 1.0f && m.afs != 0 && m.amt != 0;
          if (m.on) a->mosaico = m;
        }
      }
      else if (k == "material") {   // "firma hex,cuadros del bucle" (EU 0x1DD590)
        Material m;
        char hex[600] = {};
        if (std::sscanf(v.c_str(), "%599[0-9A-Fa-f],%u", hex, &m.frames) == 2 && m.frames > 0) {
          for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
            const char b2[3] = {hex[i], hex[i + 1], 0};
            m.firma.push_back(uint8_t(std::strtoul(b2, nullptr, 16)));
          }
          if (m.firma.size() >= 12) a->mats.push_back(std::move(m));
        }
      }
      else if (k == "matclave") {   // "firma hex,cuadro,RRGGBBAA"
        char hex[600] = {};
        unsigned f = 0, rgba = 0;
        if (std::sscanf(v.c_str(), "%599[0-9A-Fa-f],%u,%x", hex, &f, &rgba) == 3) {
          std::vector<uint8_t> firma;
          for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
            const char b2[3] = {hex[i], hex[i + 1], 0};
            firma.push_back(uint8_t(std::strtoul(b2, nullptr, 16)));
          }
          for (Material& m : a->mats)
            if (m.firma == firma) m.claves.push_back(MatClave{f, rgba});
        }
      }
      else if (k == "niebla") {   // "RRGGBB,ini,fin[,fogA,fogB]" (24 bits de color, como las entradas nativas de B3)
        // Compatibilidad con los stage.toml viejos ("RRGGBBAA"): el color bueno son los 6 primeros digitos.
        const size_t coma = v.find(',');
        const std::string tok = coma == std::string::npos ? v : v.substr(0, coma);
        unsigned col = 0;
        if (tok.size() >= 8) col = unsigned(std::strtoul(tok.substr(0, 6).c_str(), nullptr, 16));
        else col = unsigned(std::strtoul(tok.c_str(), nullptr, 16));
        a->niebla_color = col & 0xFFFFFFu;
        a->niebla = coma != std::string::npos;
        if (coma != std::string::npos) {
          const int campos = std::sscanf(v.c_str() + coma + 1, "%f,%f,%f,%f", &a->niebla_ini, &a->niebla_fin,
                                         &a->niebla_a, &a->niebla_b);
          a->niebla = campos >= 2;
          a->niebla_extra = campos == 4;
        }
      }
    }
  }
  std::erase_if(g_altas, [](const Alta& a) {
    const bool mal = a.entrada < 5000 || a.tema < 0 || a.tema >= kSerie;
    if (mal) REXLOG_WARN("dbz3 escenarios [{}]: alta ignorada (entrada {} < 5000 o tema {} fuera de 0..16)", a.mod,
                         a.entrada, a.tema);
    return mal;
  });
  if (g_altas.size() > 23) g_altas.resize(23);
  // Contrato de zona B (frente 07): `zona_b = <entrada del alta A>`. Si no existe esa alta, o apunta a si
  // misma, se ignora el cambio: ComprobarZonaB nunca debe recargar un registro que no esta cargado.
  for (auto& a : g_altas) {
    if (!a.zona_b) continue;
    const bool existe = std::any_of(g_altas.begin(), g_altas.end(),
                                    [&](const Alta& b) { return b.entrada == a.zona_b; });
    if (!existe || a.zona_b == a.entrada) {
      REXLOG_WARN("dbz3 escenarios [{}]: zona_b={} ignorada (no hay alta con esa entrada o es la misma)", a.mod,
                  a.zona_b);
      a.zona_b = 0;
    }
  }
  // Campos de IW (campos_iw.py): llanuras para los minijuegos, no escenarios de Duelo. Las ocultas van al final
  // y las visibles caben en el carrusel de 24 (escenarios_sel_gen.inc kSelMax) = 12 del juego + 11 + el "?";
  // antes los 8 campos llenaban el carrusel y dejaban fuera las altas siguientes (Montanas nevadas de B2).
  for (auto& a : g_altas)
    if (a.mod.rfind("Campo IW", 0) == 0) a.oculta = true;
  std::stable_partition(g_altas.begin(), g_altas.end(), [](const Alta& a) { return !a.oculta; });
  g_visibles = uint32_t(std::count_if(g_altas.begin(), g_altas.end(), [](const Alta& a) { return !a.oculta; }));
  if (g_visibles > 11) {
    REXLOG_WARN("dbz3 escenarios: {} escenarios nuevos y el selector solo tiene sitio para 11", g_visibles);
    g_visibles = 11;
  }
  for (auto& a : g_altas)
    if (!a.menu) a.menu = a.entrada + 100;
  // las tablas viven en .data: escribibles para prestar el registro anfitrion
  if (auto* heap = memory->LookupHeap(kRecs))
    heap->Protect(kRecs & ~0xFFFu, 0x2000, rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite);
  for (size_t k = 0; k < g_altas.size(); ++k)
    REXLOG_INFO("dbz3 escenarios: alta {} = '{}' / '{}' (mod {}, entrada {})", kSerie + int(k), g_altas[k].nombre,
                Nombre(kSerie + int(k), 5), g_altas[k].mod, g_altas[k].entrada);
  std::string lista;   // para pantallas propias y pruebas: los nombres en el idioma del jugador
  for (int r = 0; r < Total(); ++r) lista += (r ? ", " : "") + std::to_string(r) + " " + Nombre(r);
  REXLOG_INFO("dbz3 escenarios: nombres (idioma {}): {}", dbz3::settings::Language(), lista);
}

}  // namespace dbz3::escenarios

// ---------------------------------------------------------------------------------------------------
// FRENTE 12 - AZAR EN LA REVANCHA (mod opcional dbz3_escenarios_revancha_aleatoria). La casilla "?" del
// selector (mn_StageSel) elige un escenario al azar AL CONFIRMAR: la casilla confirmada se guarda en el
// byte 0x824BA129 (global 0x824BA110, datos del selector; la escribe mn_StageSel en su byte +25; valor 12
// = "?"): generated/dbz3_recomp.40.cpp:16028-16031. El combate guarda ya el registro elegido en obj+340,
// asi que la revancha nativa ("Luchar otra vez") repetia el mismo. Aqui, con la cvar activa y el combate
// venido del "?", se elige un registro nuevo en cada recarga posterior (la primera respeta lo elegido).
// Solo casillas visibles (0..10 y las altas; sin los campos de IW de los minijuegos). Online el valor sale
// de la semilla compartida (netplay.cpp Session::AzarCompartido) para que los dos PCs elijan lo mismo.
// ---------------------------------------------------------------------------------------------------
namespace dbz3::escenarios {
namespace {
constexpr dbz3::GA kSelDatos = 0x824BA110;   // datos del selector del Duelo (mn_StageSel; netplay kUiSnap)
constexpr uint32_t kSelCasilla = 12;         // la casilla "?" (aleatorio) de la lista del selector
bool g_aleatorio = false;                    // el combate en curso vino de la casilla "?"

// Indice de registro al azar de las casillas visibles (mismo reparto que hub_menu.cpp Stage(): 0..10 de
// serie + las altas). Nunca 11..16 (variantes) ni los campos de IW.
int RegistroAzar(uint32_t azar) {
  const int n = 11 + Visibles();
  if (n <= 0) return 0;
  const int k = int(azar % uint32_t(n));
  return k <= 10 ? k : kSerie + (k - 11);
}
}  // namespace

// Registro de la revancha aleatoria, o -1 si no cambia (-1 = usar el que trae obj+340).
int RevanchaAzar(uint8_t* base, int i) {
  if (!REXCVAR_GET(dbz3_escenarios_revancha_aleatoria)) {
    g_aleatorio = false;
    return -1;
  }
  if (REX_LOAD_U8(kSelDatos + 25) != uint8_t(kSelCasilla)) {   // el combate no vino del "?"
    g_aleatorio = false;
    return -1;
  }
  if (!g_aleatorio) {   // primera carga del combate aleatorio: se respeta lo que eligio el juego
    g_aleatorio = true;
    return -1;
  }
  const uint32_t azar = dbz3::net::Session::Get().AzarCompartido(uint32_t(i) * 0x9E3779B9u + 0x1234u);
  int nuevo = RegistroAzar(azar);
  if (nuevo == i && 11 + Visibles() > 1) nuevo = RegistroAzar(azar + 1u);   // que cambie de verdad
  REXLOG_INFO("dbz3 escenarios: revancha aleatoria: registro {} -> {} ('{}')", i, nuevo, Nombre(nuevo, 1));
  return nuevo;
}
}  // namespace dbz3::escenarios

// Carga del escenario del combate (obj+340 = indice de registro).
DBZ3_HOOK_EU(sub_820C26E0) {
  const uint32_t obj = ctx.r3.u32;
  int i = int16_t(REX_LOAD_U16(obj + 340));
  dbz3::escenarios::RecordarObj(obj);   // frente 07: para recargar el escenario al pasar de zona
  if (const int rev = dbz3::escenarios::RevanchaAzar(base, i); rev >= 0 && rev != i) {   // frente 12: revancha aleatoria
    i = rev;
    REX_STORE_U16(obj + 340, uint16_t(i));   // se queda: la proxima revancha vuelve a barajar
  }
  const int j = dbz3::escenarios::Traducir(base, i);
  dbz3::clima::EscenarioPedido();
  if (j != i) REX_STORE_U16(obj + 340, uint16_t(j));
  orig(ctx, base);
  if (j != i) REX_STORE_U16(obj + 340, uint16_t(i));   // "otra vez" vuelve a pedir el alta
}

// id_SE del registro r3 (sub_821221F8, con el mismo indice del combate).
DBZ3_HOOK_EU(sub_820C19B0) {
  ctx.r3.u64 = uint32_t(dbz3::escenarios::Traducir(base, int(ctx.r3.s32)));
  orig(ctx, base);
}

// ---------------------------------------------------------------------------------------------------
// SIN CAMBIO DE ZONA EN LAS ALTAS (frente 05, 2026-10-18). El "cambio de zona" del motor es cambiar el
// objeto activo del escenario cargado: G = 0x823A3D80 lleva el indice en G+0x40 (clima.cpp lo llama
// variante; su array de 36 B por objeto esta en G+0x34/0x38) y de el salen tambien la luz y la niebla
// del registro (record + 0x1C + variante*32, sub_820C2058). El UNICO escritor de G+0x40 de todo el
// juego es sub_820CC6C0 (generated/dbz3_recomp.30.cpp:4111; gemelo EU dbz3eu_sub_820CC650):
//   - lee n_obj = *(*(G+0x20)+4), el numero de objetos del #ZDD del escenario (sub_820C26E0 guarda
//     el #ZDD en G+0x20; n_obj va en +4 y la lista de objetos en +0x20: sub_820C30A0 lo arma en G+0x34);
//   - si n_obj <= 1 no hace nada y devuelve 1; si hay mas de uno, reproduce su suceso (sub_820BD028,
//     sub_820BDEE0, sub_8214F938(0x2000000,1)) e invierte G+0x40 (xori 1) y regenera el escenario
//     (sub_820C13A8: funcion de tema del registro o sub_820C11B0/1228/12A0/1320/BFC28).
// En las altas de mod, el registro anfitrion (16, R&R BASE) tiene los GRUPOS a cero (Activar, arriba):
// tras el cambio, luz y niebla salen del grupo 0 y el escenario pasa a su segundo objeto, que en los
// B2 convertidos de los grupos con zona (0, 4, 7 y 8) es la zona B de B2 sin preparar (sus raices se
// quitaron y sus camaras de suceso tambien). Eso es el "cambia de zona y se ve mal" del usuario:
//   - los bins convertidos de esos grupos conservan n_obj = 2 (medido: Base B2, B2 grupo 4, Montanas
//     nevadas y Ciudad B2 con #ZDD+4 = 2; B2 grupo 2 y el donante 44, con 1): es el mismo criterio con
//     el que el guion de B2 pide su cambio de zona, y el motivo de que el suceso pueda saltar;
//   - en R&R BASE nativo (n_obj = 1) y en las altas de un solo objeto el motor ya se niega solo.
// Con un alta activa no se deja cambiar: es EXACTAMENTE el camino del original con n_obj < 2 (no toca
// nada y devuelve 1). Los escenarios de serie no cambian: sin alta el gancho llama al original.
// ---------------------------------------------------------------------------------------------------
DBZ3_HOOK(sub_820CC6C0, dbz3eu_sub_820CC650) {
  if (dbz3::escenarios::g_zona_motor) return orig(ctx, base);
  if (dbz3::escenarios::AltaActiva()) {
    const uint32_t zdd = REX_LOAD_U32(dbz3::escenarios::kG + 32);
    const uint32_t n_obj = zdd ? REX_LOAD_U32(zdd + 4) : 0;
    {
      static std::atomic<int> k{0};
      if (k.fetch_add(1) < 10) REXLOG_INFO("dbz3 escenarios: el guion pide cambio de zona en un alta (lr=0x{:08X}, n_obj={})", uint32_t(ctx.lr), n_obj);
    }
    if (n_obj > 1) {
      static std::atomic<int> avisos{0};
      if (avisos.fetch_add(1) < 8)
        REXLOG_INFO("dbz3 escenarios: cambio de zona bloqueado en un alta (registro {}): el #ZDD tiene {} "
                    "objetos; se queda en la zona 0", dbz3::escenarios::kAnfitrion, n_obj);
    }
    ctx.r3.u64 = 1;
    return;
  }
  orig(ctx, base);
}

// ---------------------------------------------------------------------------------------------------
// SONDA (DBZ3_SONDA_ESCENARIO=1): tras reubicar el #ZDD (sub_820CBC90) comprueba que los registros de
// colision rec20/rec30 de cada objeto (obj+0xAC/+0xB0/+0xB4/+0xB8) caen dentro de su modelo #AWO:
// campo 7 = el modelo, 4 = cabecera del bloque de colision [FFFFFFFF ...], 3 = [4, nodos, ...].
// Es la medida que separo el arreglo del cuelgue de B2 (punteros PS2 sin traducir, sub_820CB0A0).
// ---------------------------------------------------------------------------------------------------
REXCVAR_DEFINE_BOOL(dbz3_escenarios_guardas, true, "DBZ3/Dev",
                    "Redes de seguridad de los escenarios convertidos (camara sin clip, nombre NULL, objeto sin casillas); apagar solo para probar la raiz");
// X1esc 23/10: red de seguridad del cierre en combates largos de los B2 convertidos (lectura de guest 0x14, lr 0x820F4680):
// la actualizacion de camara lee *(*(cam+52)+20) y el clip (+52) es nulo cuando el escenario no tiene el recurso pedido.
// Sin clip se salta esa actualizacion (la raiz se arregla en el conversor). EU: gemelo por tools/eu_map.py.
DBZ3_HOOK_EU(sub_820F4678) {
  // 24/10 CAM (issue #52, logs 1.5.5.0/1.5.5.1): el original solo lee el clip (+52) con cam+32 == 1 y cam+36 == 7 (camara
  // por clip). En el resto de modos +52 nulo es normal (la camara de combate de serie lo tiene asi): saltar la actualizacion
  // dejaba la camara fija tras la presentacion y loca en los agarres. Ahora en los escenarios de serie no se toca nada (camara
  // 100 % original); en los convertidos solo se salta el caso en que el original leeria 0+20 y cerraria el juego.
  const uint32_t cam = ctx.r3.u32;
  if (static const bool traza = std::getenv("DBZ3_TEST_CAM_TRAZA") != nullptr; traza && cam) {   // CAM: medir los modos
    static std::atomic<int> t{0};
    if (t.fetch_add(1) < 200)
      REXLOG_INFO("dbz3 cam: sub_820F4678 lr=0x{:08X} +32={} +36={} +48=0x{:X} +52=0x{:08X} +56={}", uint32_t(ctx.lr),
                  REX_LOAD_U32(cam + 32), REX_LOAD_U32(cam + 36), REX_LOAD_U32(cam + 48), REX_LOAD_U32(cam + 52), REX_LOAD_U32(cam + 56));
  }
  if (dbz3::escenarios::AltaActiva() && cam && !REX_LOAD_U32(cam + 52) && REX_LOAD_U32(cam + 32) == 1 && REX_LOAD_U32(cam + 36) == 7) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 5) REXLOG_INFO("dbz3 escenarios: camara de clip sin clip (+52 nulo, modo 7) en un escenario convertido");
    if (REXCVAR_GET(dbz3_escenarios_guardas)) return;
  }
  orig(ctx, base);
}

// X1esc 23/10: cierre 'read of guest 0x1, lr 0x8208DF10' (ya en el exe anterior a esta noche): sub_8208DEE0 compara 3 letras
// del nombre r3+1 con una cadena y le llega r3 = NULL. Se registra quien la llama (lr) y, sin nombre, se responde 'no coincide'.
DBZ3_HOOK_EU(sub_8208DEE0) {
  if (!ctx.r3.u32 && REXCVAR_GET(dbz3_escenarios_guardas)) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 20)
      REXLOG_INFO("dbz3 escenarios: sub_8208DEE0 con nombre NULL (llamador lr=0x{:08X} r4=0x{:08X} alta={})", uint32_t(ctx.lr),
                  ctx.r4.u32, dbz3::escenarios::AltaActiva() ? 1 : 0);
    const uint32_t tab = REX_LOAD_U32(dbz3::GA(0x823A3D80) + 44);
    if (tab && n.load() < 6) {
      const uint32_t cnt = REX_LOAD_U32(tab + 16), arr = REX_LOAD_U32(tab + 20);
      REXLOG_INFO("dbz3 escenarios:   tabla G+44=0x{:08X} n={} arr=0x{:08X} [124]={:08X} {:08X} [125]={:08X} {:08X}", tab, cnt, arr,
                  arr ? REX_LOAD_U32(arr + 124 * 16) : 0, arr ? REX_LOAD_U32(arr + 124 * 16 + 4) : 0,
                  arr ? REX_LOAD_U32(arr + 125 * 16) : 0, arr ? REX_LOAD_U32(arr + 125 * 16 + 4) : 0);
    }
    ctx.r3.s64 = 0;
    return;
  }
  orig(ctx, base);
}

// X1esc 23/10: builtin de script sub_820CCAF8 (arg0 = tipo) crea el objeto de sub_820BFA20 con las casillas 124, 125 y 110+tipo del
// registro de 641 casillas. El registro del donante 44 las trae vacias (los nativos 50 y 60 si las tienen): sin ellas el juego cierra
// (sub_8208DEE0 con nombre NULL, lr 0x8208DF10). Se registra la llamada y, si faltan las casillas, no se crea el objeto.
DBZ3_HOOK_EU(sub_820CCAF8) {
  const uint32_t args = REX_LOAD_U32(ctx.r4.u32 + 44);
  const uint32_t tipo = args ? REX_LOAD_U32(args) : 0;
  const uint32_t tab = REX_LOAD_U32(dbz3::GA(0x823A3D80) + 44), arr = tab ? REX_LOAD_U32(tab + 20) : 0;
  auto slot = [&](uint32_t i) { return arr ? REX_LOAD_U32(arr + i * 16) : 0u; };
  const bool falta = !slot(124) || !slot(125) || !slot(110 + tipo);
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 30)
    REXLOG_INFO("dbz3 escenarios: builtin crear-objeto(820CCAF8) tipo={} casillas 124={:08X} 125={:08X} 110+t={:08X} falta={}", tipo,
                slot(124), slot(125), slot(110 + tipo), falta ? 1 : 0);   // sin filtro de alta: tambien los nativos
  if (falta && dbz3::escenarios::AltaActiva() && REXCVAR_GET(dbz3_escenarios_guardas)) {
    ctx.r3.s64 = 1;
    return;
  }
  orig(ctx, base);
}

// X1esc 23/10: Base B2, cambio de zona con un bin viejo (raices de la zona B quitadas): sub_82090820 recibe r3 = 0x290 (campo +0x290
// de un indice de hueso fuera de la tabla). Red de seguridad: se registra el llamador y no se dibuja ese nodo.
DBZ3_HOOK_EU(sub_82090820) {
  if (ctx.r3.u32 < 0x10000u) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 6)
      REXLOG_INFO("dbz3 escenarios: sub_82090820 con r3=0x{:X} (objeto nulo) llamador lr=0x{:08X} r1=0x{:08X}", ctx.r3.u32, uint32_t(ctx.lr), ctx.r1.u32);
    if (REXCVAR_GET(dbz3_escenarios_guardas)) { ctx.r3.s64 = 0; return; }
  }
  orig(ctx, base);
}

// ESC 24/10: cierre al empezar el combate en B2 GRUPO 1 ("read of guest 0x0081F8E8", lr 0x8208E5AC). El dibujo de un modelo AWO
// (sub_8208F3A0 -> sub_8208E518) lleva una lista por hueso en obj+8 (pares de 8 B; word0 = linea de ejes a dibujar, -1 = hueso oculto).
// Medido: en el AWO de 9 huesos de la geometria 5001 (array_am = 1, solo la linea 0) el juego pone 0x13 en los huesos 1..8 a los pocos
// segundos; sub_8208E518 indexa axes[0x13] (fuera del array, cae en el #ACM vecino), toma como "hueso" datos de camara y lee un puntero
// sin traducir (+52 = 0x0081F8E4). Solo en escenarios convertidos se limita la linea a la ultima que tiene el modelo (como lo haria el
// original con un modelo de una sola linea); los modelos de serie no se tocan.
DBZ3_HOOK_EU(sub_8208F3A0) {
  const uint32_t obj = ctx.r3.u32;
  if (dbz3::escenarios::AltaActiva() && obj && REXCVAR_GET(dbz3_escenarios_guardas)) {
    const uint32_t awo = REX_LOAD_U32(obj);
    const uint32_t bones = awo ? REX_LOAD_U32(awo + 16) : 0, lineas = awo ? REX_LOAD_U32(awo + 32) : 0;
    if (lineas && lineas < 16 && bones && bones < 512) {
      for (uint32_t i = 0; i < bones; ++i) {
        const uint32_t a = obj + 8 + 8 * i, v = REX_LOAD_U32(a);
        if (v != 0xFFFFFFFFu && v >= lineas) {
          static std::atomic<int> n{0};
          if (n.fetch_add(1) < 6)
            REXLOG_INFO("dbz3 escenarios: linea {} fuera del modelo (hueso {} de {}, {} linea/s): se usa la {}", v, i, bones, lineas, lineas - 1);
          REX_STORE_U32(a, lineas - 1);
        }
      }
    }
  }
  orig(ctx, base);
}

// ESC 24/10: transicion de escenario (builtin sub_820CC870 -> sub_820C1908 crea la tarea sub_820C1418). El guion del donante la llama
// en pareja (tipo 1 = a negro, tipo 2 = de vuelta, ~3 s despues) tras un agarre contra el muro; la tarea pone la camara en modo 7 con el
// clip de mgr+564 (pista = tipo) y la deja ~5 s. Medido (alta 20, B2 grupo 4, 4 de 7 pasadas con agarres): el clip es el del donante y
// en la geometria convertida solo enseña terreno ("la camara mira el suelo"). En los escenarios convertidos el cambio de zona lo hace
// PasarZonaB (no el guion), asi que la transicion no tiene nada que tapar: se devuelve 1 (hecho) sin crear la tarea. Los de serie no se tocan.
DBZ3_HOOK_EU(sub_820CC870) {
  if (dbz3::escenarios::AltaActiva()) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 20) {
      const uint32_t a = REX_LOAD_U32(ctx.r4.u32 + 44);
      REXLOG_INFO("dbz3 escenarios: builtin transicion(820CC870) lr=0x{:08X} args@0x{:08X}: {} {} {:08X}", uint32_t(ctx.lr), a,
                  a ? int32_t(REX_LOAD_U32(a)) : 0, a ? int32_t(REX_LOAD_U32(a + 4)) : 0, a ? REX_LOAD_U32(a + 8) : 0);
    }
    if (std::getenv("DBZ3_TEST_ESC_SIN_GUARDA_TRANSICION") == nullptr && REXCVAR_GET(dbz3_escenarios_guardas)) {
      ctx.r3.s64 = 1;   // builtin hecho (el original devuelve 1): sin tarea, sin camara de clip
      return;
    }
  }
  orig(ctx, base);
}
// X1esc 23/10: busqueda de recurso de camara (tipo/id) sub_820CF580; sonda de resultados nulos en altas.
DBZ3_HOOK_EU(sub_820CF580) {
  const uint32_t r3 = ctx.r3.u32, tipo = ctx.r4.u32, id = ctx.r5.u32;
  orig(ctx, base);
  if (dbz3::escenarios::AltaActiva()) {
    static std::atomic<int> n{0};
    if (ctx.r3.u32 == 0 && n.fetch_add(1) < 40)
      REXLOG_INFO("dbz3 escenarios: RECURSO_CAMARA nulo tipo={} id={} base=0x{:08X}", tipo, int32_t(id), r3);
  }
}

DBZ3_HOOK_EU(sub_820CBC90) {
  const uint32_t zdd = ctx.r3.u32;
  orig(ctx, base);
  static const bool sonda = std::getenv("DBZ3_SONDA_ESCENARIO") != nullptr;
  if (!sonda) return;
  auto guest = [](uint32_t p) { return p >= 0x40000000u && p < 0x50000000u; };
  const uint32_t n = REX_LOAD_U32(zdd + 4), lista = REX_LOAD_U32(zdd + 32);
  int ok = 0, mal = 0;
  for (uint32_t k = 0; k < n && guest(lista); ++k) {
    const uint32_t ob = REX_LOAD_U32(lista + 4 * k);
    if (!guest(ob)) continue;
    const uint32_t arr[4][3] = {{0xAC, 0x94, 5}, {0xB0, 0x98, 8}, {0xB4, 0x9C, 5}, {0xB8, 0xA0, 8}};
    for (const auto& a : arr) {
      const uint32_t p = REX_LOAD_U32(ob + a[0]), c = REX_LOAD_U32(ob + a[1]);
      const uint32_t tam = a[2] == 5 ? 0x20 : 0x30, campos = a[2];
      for (uint32_t r = 0; r < c && guest(p); ++r) {
        const uint32_t e = p + r * tam;
        uint32_t f[8];
        for (uint32_t j = 0; j < campos; ++j) f[j] = REX_LOAD_U32(e + 4 * j);
        const uint32_t mdl = f[campos - 1], cab = f[campos - 4], f3 = f[campos - 5];
        bool bien = guest(mdl) && REX_LOAD_U8(mdl + 1) == 'A' && REX_LOAD_U8(mdl + 2) == 'W' &&
                    REX_LOAD_U8(mdl + 3) == 'O' && guest(cab) && guest(f3);
        for (uint32_t j = 0; bien && j < campos; ++j) bien = f[j] >= mdl && f[j] - mdl < 0x400000u;
        bien = bien && REX_LOAD_U32(cab) == 0xFFFFFFFFu && REX_LOAD_U32(f3) == 4;
        if (bien) {
          ++ok;
        } else if (mal++ < 4) {
          REXLOG_WARN("dbz3 sonda escenario: obj {} +0x{:X}[{}] MAL: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                      k, a[0], r, f[0], f[1], f[2], f[3], f[4], campos > 5 ? f[5] : 0, campos > 6 ? f[6] : 0,
                      campos > 7 ? f[7] : 0);
        }
      }
    }
  }
  REXLOG_INFO("dbz3 sonda escenario: #ZDD 0x{:08X}: {} registros de colision dentro de su modelo, {} fuera", zdd, ok,
              mal);
}

// ---------------------------------------------------------------------------------------------------
// SELECTOR DE ESCENARIOS DEL DUELO (mn_StageSel): una casilla nueva por alta (13 + k -> registro 17 + k).
// Datos ampliados y tablas por casilla propias: escenarios_sel_gen.inc (tools/gen_escenarios_sel.py).
// Imagenes de la casilla nueva: el #AZT `menu` del mod (0 circulo, 1 vista, 2 rotulo, 3 bola) se carga con
// los del selector y el juego lo registra al crearle las texturas. Lo de la casilla nueva se dibuja con ese
// #AZT puesto como banco de texturas (el del selector, D+4, queda igual): circulo y bola (sub_8219D520 /
// sub_8219D6E8), y la vista y el rotulo (sprites copiados de los nativos, tabla #ACA ampliada en D+0 +28;
// se crean en sub_8219CC48 y se dibujan en sub_8219E828). Sin altas todo es el codigo original.
// ---------------------------------------------------------------------------------------------------
// valores de partida (US); Tablas() los cambia por las tablas propias antes de que corra ninguna copia Sel_/SelEu_
uint32_t dbz3_esc_f94 = 0x82020F94, dbz3_esc_fb0 = 0x82020FB0, dbz3_esc_trans = 0x82373384, dbz3_esc_limite = 11;
void dbz3_esc_instalar(PPCContext& ctx, uint8_t* base, uint32_t d);
void dbz3_esc_banco(PPCContext& ctx, uint8_t* base, uint32_t d, bool nuevo);

namespace {
#include "escenarios_sel_gen.inc"
#if defined(DBZ3_DUAL_REGION)
#include "escenarios_sel_gen_eu.inc"   // las mismas copias del codigo EU (tools/gen_escenarios_sel.py)
#define SEL(fn) DBZ3_CALL2(Sel_##fn, SelEu_##fn)
#else
#define SEL(fn) Sel_##fn
#endif

using dbz3::escenarios::g_altas;
using dbz3::escenarios::g_visibles;
constexpr uint32_t kCasillasSerie = 13;   // 0..11 escenarios, 12 = "?"
constexpr dbz3::GA kGlobalSel = 0x8245F25C;   // estructura del selector: +20 tarea (+48 = datos)

bool Ext() { return !g_altas.empty() && dbz3::escenarios::g_mem; }

uint32_t Alloc(uint32_t n) {
  const uint32_t a = dbz3::escenarios::g_mem->SystemHeapAlloc(n, 0x20);
  if (a) std::memset(dbz3::escenarios::g_mem->TranslateVirtual<uint8_t*>(a), 0, n);
  return a;
}

uint32_t Llamar(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*), uint32_t r3, uint32_t r4 = 0,
                uint32_t r5 = 0) {
  PPCContext c = ctx;
  c.r3.u64 = r3;
  c.r4.u64 = r4;
  c.r5.u64 = r5;
  fn(c, base);
  return c.r3.u32;
}

std::vector<uint32_t> g_cargas;       // nodo de lectura AFS del #AZT de cada alta (sub_82083968)
std::vector<uint32_t> g_azt;          // #AZT de cada alta ya en memoria y registrado (0 = no esta)
uint32_t g_datos = 0, g_tabla_nat = 0;

// #AZT de la casilla c (0 si es del juego o su alta no tiene imagenes).
uint32_t AztDe(uint32_t c) {
  return c >= kCasillasSerie && c - kCasillasSerie < g_azt.size() ? g_azt[c - kCasillasSerie] : 0;
}

// 12b: #AZT que debe verse en la casilla `c`. mn_StageSel escribe cada fotograma el registro elegido
// (con el "flag" de variante de Circle) en el byte +30 de sus datos (global 0x824BA110). Si ese registro
// es un alta con imagenes, se ponen las suyas: asi el icono cambia al mantener Circle cuando el alta
// declara `variante` en su stage.toml. Los registros de serie 11..16 NO traen icono propio (el atlas
// data_<idioma>.afs 2043 solo tiene 13 vistas: 12 casillas + el "?"), asi que devuelven el de la casilla
// (el normal). Ver VERIFICACION.md del seguimiento.
uint32_t AztSel(uint8_t* base, uint32_t c) {
  if (c < kCasillasSerie) return AztDe(c);
  const uint32_t reg = REX_LOAD_U8(dbz3::GA(0x824BA110) + 30);
  return reg >= uint32_t(dbz3::escenarios::kSerie) && reg - dbz3::escenarios::kSerie < g_azt.size() &&
                 g_azt[reg - dbz3::escenarios::kSerie]
             ? g_azt[reg - dbz3::escenarios::kSerie]
             : AztDe(c);
}

// ---------------------------------------------------------------------------------------------------
// 12c (seguimiento): musica de combate con vista previa en el selector. La pista la arranca EL JUEGO al
// empezar la pelea por sub_820F3980(handle, playlist, track): lee el numero de pistas de la playlist
// (*(0x82328298 + playlist*4)) y el indice ADX del item (*(*(0x823280B0 + playlist*4) + track*4)) y lo
// reproduce con sub_82081FE8(item | 0x20000, -3) (generated/dbz3_recomp.30.cpp:6253-6344). El
// (playlist, track) lo pone un comando de guion del escenario (sub_8211EF60, generated/dbz3_recomp.10.cpp:
// 8659-8750); NO es un campo del registro: el `id_SE` del registro (+0x14) es el ambiente, no la musica.
// La pista por registro de abajo es [H] (pendiente de medir en el juego; ver VERIFICACION.md §12c); un
// alta la declara en su stage.toml (`musica = <entrada de adx_usa.afs>`). Solo US.
// ---------------------------------------------------------------------------------------------------
constexpr int kMusicaRegistro[dbz3::escenarios::kSerie] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,   // 0..16: medir en el juego
};

// Indice ADX (adx_usa.afs) de la musica de combate del registro r, o -1 si no hay.
int MusicaDeRegistro(int r) {
  if (r >= 0 && r < dbz3::escenarios::kSerie) return kMusicaRegistro[r];
  if (r >= dbz3::escenarios::kSerie && r - dbz3::escenarios::kSerie < int(g_altas.size()))
    return g_altas[size_t(r - dbz3::escenarios::kSerie)].musica;
  return -1;
}

int g_mus_sonando = -1;   // pista de la vista previa que esta sonando (-1 = ninguna)

// Corta la vista previa (al salir del selector o al apagarla con la cvar).
void CortarVistaPreviaMusica() {
  if (g_mus_sonando != -1) {
    dbz3::pasarela::CallarMusicaPs2();
    g_mus_sonando = -1;
  }
}

// Suena la pista del registro elegido en vivo (0x824BA110 + 30, que mn_StageSel reescribe cada fotograma
// con el flag de variante de Circle); al cambiar de casilla se corta y entra la nueva. [H] mientras suena,
// la musica del propio menu del Duelo (la del guest) sigue por debajo: el port no puede cortarla sin RE
// del reproductor del juego (ver VERIFICACION.md §12c); el volumen de la previa es bajo a proposito.
void VistaPreviaMusica(uint8_t* base, uint32_t d) {
  if (!REXCVAR_GET(dbz3_selector_musica) || !d) {
    CortarVistaPreviaMusica();
    return;
  }
  const int reg = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 30));
  const int pista = MusicaDeRegistro(reg);
  if (pista == g_mus_sonando) return;
  CortarVistaPreviaMusica();
  if (pista < 0) return;
  g_mus_sonando = pista;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.5f;
  REXLOG_INFO("dbz3 escenarios: vista previa musical del escenario {} ('{}'): adx_usa {}", reg,
              dbz3::escenarios::Nombre(reg, 1), pista);
  dbz3::pasarela::OirMusicaPs2(dbz3::HostDataRoot(), "adx_usa.afs", pista, vol);
}

// SONDA del selector (frente 15; DBZ3_SONDA_ESCENARIO=1, la misma que usa el frente 07): imprime la casilla
// del cursor (+25) y el registro elegido (+30) cada vez que cambian. Sin ella, la navegacion de la prueba
// 12a hay que adivinarla; con ella, una sola ejecucion da la cuenta exacta de teclas hasta la casilla "?".
void SondaSel(uint8_t* base) {
  const int casilla = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 25));
  const int reg = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 30));
  static int ult_c = -2, ult_r = -2;
  if (casilla == ult_c && reg == ult_r) return;
  ult_c = casilla;
  ult_r = reg;
  REXLOG_INFO("dbz3 escenarios: sel casilla {} (registro {})", casilla, reg);
}

void Tablas(uint8_t* base) {
  static bool hechas = false;
  if (hechas) return;
  hechas = true;
  const uint32_t f94 = Alloc(2 * kSelMax), fb0 = Alloc(2 * kSelMax), tr = Alloc(4 * kSelMax);
  for (uint32_t c = 0; c < kSelMax; ++c) {
    const uint32_t src = c < kCasillasSerie ? c : 12;   // de relleno: las del "?"
    REX_STORE_U8(f94 + 2 * c, REX_LOAD_U8(dbz3::GA(0x82020F94) + 2 * src));
    REX_STORE_U8(f94 + 2 * c + 1, REX_LOAD_U8(dbz3::GA(0x82020F94) + 2 * src + 1));
    REX_STORE_U8(fb0 + 2 * c, REX_LOAD_U8(dbz3::GA(0x82020FB0) + 2 * src));
    REX_STORE_U8(fb0 + 2 * c + 1, REX_LOAD_U8(dbz3::GA(0x82020FB0) + 2 * src + 1));
  }
  for (uint32_t c = 0; c < 12; ++c) REX_STORE_U32(tr + 4 * c, REX_LOAD_U32(dbz3::GA(0x82373384) + 4 * c));
  for (uint32_t k = 0; k < g_visibles; ++k) {
    const uint16_t r = uint16_t(dbz3::escenarios::kSerie + k);
    REX_STORE_U16(tr + 4 * (kCasillasSerie + k), r);
    // 12b: segundo valor del par = el registro alternativo (Circle). Sin `variante` en el stage.toml, el
    // mismo registro (comportamiento de antes).
    const int v = g_altas[k].variante;
    REX_STORE_U16(tr + 4 * (kCasillasSerie + k) + 2, uint16_t(v >= 0 ? v : r));
  }
  dbz3_esc_f94 = f94;
  dbz3_esc_fb0 = fb0;
  dbz3_esc_trans = tr;
  dbz3_esc_limite = 12 + g_visibles;
}

// Copia de un sprite del #ACA (hasta el siguiente de la tabla) con sus punteros internos rebasados y su
// bloque de textura [tex, 3, 1.0, 0, x0, y0, x1, y1] apuntando a `tex` con el rectangulo logico dado.
uint32_t CopiaSprite(uint8_t* base, uint32_t tabla, uint32_t n, uint32_t i, uint32_t tex, float w, float h,
                     uint32_t indice) {
  const uint32_t src = REX_LOAD_U32(tabla + 4 * i);
  uint32_t tam = 0x200;
  for (uint32_t j = 0; j < n; ++j) {
    const uint32_t q = REX_LOAD_U32(tabla + 4 * j);
    if (q > src && q - src < tam) tam = q - src;
  }
  const uint32_t dst = Alloc(tam);
  if (!dst) return 0;
  uint32_t bloque = 0;
  for (uint32_t o = 0; o + 4 <= tam; o += 4) {
    uint32_t v = REX_LOAD_U32(src + o);
    if (v >= src && v < src + tam) v = v - src + dst;
    REX_STORE_U32(dst + o, v);
    if (!bloque && o >= 16 && o + 32 <= tam && REX_LOAD_U32(src + o + 4) == 3 &&
        REX_LOAD_U32(src + o + 8) == 0x3F800000u && REX_LOAD_U32(src + o) < 0x100)
      bloque = o;
  }
  if (!bloque) return 0;
  auto f = [](float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return u;
  };
  REX_STORE_U32(dst + bloque, tex);
  REX_STORE_U32(dst + bloque + 16, 0);
  REX_STORE_U32(dst + bloque + 20, 0);
  REX_STORE_U32(dst + bloque + 24, f(w));
  REX_STORE_U32(dst + bloque + 28, f(h));
  REX_STORE_U32(dst + 0x14, indice);
  return dst;
}

// Pone el banco de texturas `banco` como lo hace el juego (sub_8219CBB8: globales del menu + sub_82092C58).
void PonerBanco(PPCContext& ctx, uint8_t* base, uint32_t banco) {
  REX_STORE_U32(dbz3::GA(0x8239592C), 0);
  REX_STORE_U32(dbz3::GA(0x82395928), 28672);
  REX_STORE_U32(dbz3::GA(0x82395930), banco);
  Llamar(ctx, base, DBZ3_CALL_EU(sub_82092C58), banco, 28672);
  REX_STORE_U32(dbz3::GA(0x82389430), 0);
}

}  // namespace

// sub_8219F350 (regenerado), con los recursos ya en D: texturas de las altas y sprites nuevos.
void dbz3_esc_instalar(PPCContext& ctx, uint8_t* base, uint32_t d) {
  if (!Ext() || !d) return;
  Tablas(base);
  const size_t na = g_visibles;
  g_azt.assign(na, 0);
  for (size_t k = 0; k < na && k < g_cargas.size(); ++k) {
    const uint32_t h = g_cargas[k];
    if (!h || REX_LOAD_U16(h + 4) != 3) continue;
    const uint32_t datos = REX_LOAD_U32(h + 212);
    REX_STORE_U32(h + 212, 0);
    Llamar(ctx, base, DBZ3_CALL_EU(sub_82083D08), h, 0);
    g_cargas[k] = 0;
    if (!datos || REX_LOAD_U8(datos + 1) != 'A' || REX_LOAD_U8(datos + 2) != 'Z' || REX_LOAD_U32(datos + 16) < 4) {
      REXLOG_WARN("dbz3 escenarios: el #AZT del selector de '{}' (entrada {}) no vale", g_altas[k].nombre,
                  g_altas[k].menu);
      continue;
    }
    Llamar(ctx, base, DBZ3_CALL_EU(sub_820800A8), datos);   // crea y registra las texturas
    g_azt[k] = datos;
  }
  const uint32_t aca = REX_LOAD_U32(d + 0);
  if (!aca) return;
  // sprites: tabla nativa + 2 por alta (vista y rotulo, copias de los de la casilla 0, texturas 1 y 2)
  const uint32_t ns = REX_LOAD_U32(aca + 24), tn = REX_LOAD_U32(aca + 28);
  const uint32_t t = Alloc(4 * (ns + 2 * uint32_t(na)));
  if (!t || ns + 2 * na > 255) return;
  for (uint32_t i = 0; i < ns; ++i) REX_STORE_U32(t + 4 * i, REX_LOAD_U32(tn + 4 * i));
  const uint32_t vista0 = REX_LOAD_U8(dbz3::GA(0x82020FB0)), rotulo0 = REX_LOAD_U8(dbz3::GA(0x82020FB1));
  for (size_t k = 0; k < na; ++k) {
    const uint32_t c = kCasillasSerie + uint32_t(k), sv_i = ns + 2 * uint32_t(k);
    const uint32_t sv = g_azt[k] ? CopiaSprite(base, tn, ns, vista0, 1, 440.0f, 320.0f, sv_i) : 0;
    const uint32_t sr = g_azt[k] ? CopiaSprite(base, tn, ns, rotulo0, 2, 192.0f, 32.0f, sv_i + 1) : 0;   // fila 0 del atlas
    REX_STORE_U32(t + 4 * sv_i, sv ? sv : REX_LOAD_U32(tn + 4 * vista0));
    REX_STORE_U32(t + 4 * (sv_i + 1), sr ? sr : REX_LOAD_U32(tn + 4 * rotulo0));
    if (g_azt[k]) {   // circulo y bola: texturas 0 y 3 de su #AZT; sin el, las del "?" (no se cuelga)
      REX_STORE_U8(dbz3_esc_f94 + 2 * c, 0);
      REX_STORE_U8(dbz3_esc_f94 + 2 * c + 1, 3);
    }
    REX_STORE_U8(dbz3_esc_fb0 + 2 * c, uint8_t(sv_i));
    REX_STORE_U8(dbz3_esc_fb0 + 2 * c + 1, uint8_t(sv_i + 1));
  }
  g_datos = d;
  g_tabla_nat = tn;
  REX_STORE_U32(aca + 28, t);
  REXLOG_INFO("dbz3 escenarios: selector con {} casillas nuevas (sprites {}+)", na, ns);
}

// sub_8219E828 (regenerado): banco de la casilla nueva alrededor de la vista grande y el rotulo.
void dbz3_esc_banco(PPCContext& ctx, uint8_t* base, uint32_t d, bool nuevo) {
  if (!Ext() || !d) return;
  const uint32_t z = AztSel(base, REX_LOAD_U16(d + 604));
  if (z) PonerBanco(ctx, base, nuevo ? z : REX_LOAD_U32(d + 4));
}

// Cargador del selector: los #AZT de las altas se piden ANTES que los del juego (la cola de lectura va en
// orden, asi estan cuando sub_8219F350 ve el ultimo del juego).
DBZ3_HOOK_EU(sub_8219C7B0) {
  if (Ext()) {
    g_cargas.clear();
    for (uint32_t k = 0; k < g_visibles; ++k)
      g_cargas.push_back(Llamar(ctx, base, DBZ3_CALL_EU(sub_82083968), g_altas[k].menu, 0, 0));
  }
  orig(ctx, base);
}

// Fin del selector: el juego libera lo suyo (con su tabla de sprites); luego los #AZT de las altas.
DBZ3_HOOK_EU(sub_8219E108) {
  CortarVistaPreviaMusica();   // 12c: se sale del selector; se corta la vista previa musical
  if (Ext() && g_datos) {
    const uint32_t g = REX_LOAD_U32(kGlobalSel), tarea = g ? REX_LOAD_U32(g + 20) : 0;
    const uint32_t d = tarea ? REX_LOAD_U32(tarea + 48) : 0;
    if (d == g_datos)
      if (const uint32_t aca = REX_LOAD_U32(d + 0)) REX_STORE_U32(aca + 28, g_tabla_nat);
    g_datos = 0;
  }
  orig(ctx, base);
  if (Ext()) {
    for (uint32_t z : g_azt)
      if (z) Llamar(ctx, base, DBZ3_CALL_EU(sub_82089C10), z);
    g_azt.clear();
    for (uint32_t h : g_cargas)   // pedidos que no llegaron a usarse
      if (h) Llamar(ctx, base, DBZ3_CALL_EU(sub_82083D08), h, 0);
    g_cargas.clear();
  }
}

// Lista de casillas: las del juego (bits 0..11 de la mascara), las altas y el "?" (bit 12) al final, que
// es lo que espera la eleccion al azar (toma una de las n-1 primeras).
DBZ3_HOOK_EU(sub_8219E268) {
  if (!Ext()) {
    orig(ctx, base);
    return;
  }
  Tablas(base);
  const uint32_t d = ctx.r3.u32, mascara = ctx.r4.u32 & 0xFFFF;
  REX_STORE_U16(d + 576, uint16_t(mascara));
  REX_STORE_U16(d + 574, uint16_t(mascara));
  for (uint32_t i = 0; i < kSelMax; ++i) REX_STORE_U16(d + kSelLista + 2 * i, 0xFFFF);
  uint32_t n = 0;
  auto poner = [&](uint32_t c) {   // (REX_STORE_U16 es una macro: nada de n++ dentro)
    if (n >= kSelMax) return;
    REX_STORE_U16(d + kSelLista + 2 * n, uint16_t(c));
    ++n;
  };
  for (uint32_t b = 0; b < 12; ++b)
    if (mascara >> b & 1) poner(b);
  for (uint32_t k = 0; k < g_visibles; ++k) poner(kCasillasSerie + k);
  if (mascara >> 12 & 1) poner(12);
  REX_STORE_U8(d + 609, uint8_t(n));
  REX_STORE_U32(d + 632, n ? 65536u / (n * 12) : 0);
  if (REX_LOAD_U8(d + 610) == 2) Llamar(ctx, base, DBZ3_CALL_EU(sub_8219CAE8), d);
}

// Circulo (sub_8219D6E8) y bola (sub_8219D520) de la casilla r4 y sprites de la casilla actual
// (sub_8219CC48): con el #AZT de la casilla nueva en D+4 durante la llamada.
void ConBanco(Dbz3GuestFn F, PPCContext& ctx, uint8_t* base, uint32_t c) {
  // 12b: la casilla actual (la que mira el jugador) puede mostrar el icono de su variante (Circle); las
  // demas del carrusel van siempre con el suyo.
  const bool actual = c == REX_LOAD_U16(ctx.r3.u32 + 604);
  const uint32_t d = ctx.r3.u32, z = actual ? AztSel(base, c) : AztDe(c), nat = REX_LOAD_U32(d + 4);
  if (z) REX_STORE_U32(d + 4, z);
  F(ctx, base);
  if (z) {
    REX_STORE_U32(d + 4, nat);
    // F deja de banco global (0x8238CE10) el ultimo que puso, aqui el del alta: el del juego, como lo dejaria
    // el juego. Los sprites de otras tareas piden sus texturas al banco global (sub_82092C80 -> sub_82096FF0:
    // la 37 al pasar a la casilla, medido 2026-10-15); en el #AZT del alta solo hay 4 (y su indice va relleno
    // hasta 64 por si acaso: tools/escenarios_convertir.py azt_menu)
    if (REX_LOAD_U32(dbz3::GA(0x8238CE10)) == z) PonerBanco(ctx, base, nat);
  }
}

DBZ3_HOOK_EU(sub_8219D520) {
  if (Ext())
    ConBanco(SEL(8219D520), ctx, base, ctx.r4.u32 & 0xFFFF);
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219D6E8) {
  if (Ext())
    ConBanco(SEL(8219D6E8), ctx, base, ctx.r4.u32 & 0xFFFF);
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219E540) {   // entrada r4 del carrusel
  if (Ext())
    ConBanco(SEL(8219E540), ctx, base, REX_LOAD_U16(ctx.r3.u32 + kSelCarrusel + 8 + 12 * (ctx.r4.u32 & 0xFFFF)));
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219CC48) {
  if (Ext())
    ConBanco(SEL(8219CC48), ctx, base, REX_LOAD_U16(ctx.r3.u32 + 604));
  else
    orig(ctx, base);
}

#define DBZ3_SEL(fn)              \
  DBZ3_HOOK_EU(sub_##fn) {        \
    if (Ext())                    \
      SEL(fn)(ctx, base);         \
    else                          \
      orig(ctx, base);            \
  }
DBZ3_SEL(8219F4F8)
DBZ3_SEL(8219C870)
DBZ3_SEL(8219CAE8)
DBZ3_SEL(8219E6C0)
DBZ3_SEL(8219F2A8)
DBZ3_SEL(8219F350)
DBZ3_SEL(8219E828)
#undef DBZ3_SEL

// 12c: al final del fotograma del selector, vista previa de la musica del escenario que se mira (funciona
// tambien sin altas: la musica no depende de ellas).
DBZ3_HOOK_EU(sub_8219ECC8) {
  // La tarea del selector SOLO esta en r3 al ENTRAR (sub_8219ECC8 empieza con `lwz r31,48(r3)`); al volver,
  // la funcion deja en r3 el resultado de su ultima llamada (sub_8219E4A0/sub_8219C8D0) y `r3+48` es basura:
  // leerlo ahi hacia saltar el juego con 0xC0000005 al activar dbz3_selector_musica (studies 4, frente 15).
  const uint32_t tarea = ctx.r3.u32;
  if (std::getenv("DBZ3_SONDA_ESCENARIO")) SondaSel(base);
  if (Ext())
    SEL(8219ECC8)(ctx, base);
  else
    orig(ctx, base);
  if (REXCVAR_GET(dbz3_selector_musica))
    VistaPreviaMusica(base, tarea ? REX_LOAD_U32(tarea + 48) : 0);
}

// ---------------------------------------------------------------------------------------------------
// RING OUT CON ZONA B (frente 07, mod opcional dbz3_escenarios_zonas_b). En el original de B2, al caer un
// luchador por el borde el guion del escenario llama a FUN_001b95b0 (opcode 0x80 'call_ext', indice 0x40):
// recarga el escenario con la otra variante (la zona B) sin tocar el combate. En B3 ese aviso lo pone el
// juego en mgr+35569 (hueco del ganador + 1) y lo consumen las funciones de fin del combate (motivo 4).
// El gancho va en sub_82114910: comprueba mgr+35569 y los estados de combate (3/4/5/8) al entrar; con la
// cvar activada y una zona B en el alta cargada, el aviso se consume (el combate sigue) y se recarga el
// escenario del combate con el bin de la zona B (cuyo objeto 0 es la otra zona y cuyas camaras del objeto
// 0 son las de la transicion de B2). Con la cvar en false (por defecto) el ring out acaba el combate como
// siempre. [H] que sub_82114910 corra en el fotograma del ring out: lee el aviso y los estados de pelea
// (generated/dbz3_recomp.42.cpp:8277-8318) y la llama el flujo del combate (sub_8210E700).
// ---------------------------------------------------------------------------------------------------
// El gancho de sub_82114910 es uno solo (iw_minijuegos.cpp, tope de distancia de B3): lo llama al entrar.
namespace dbz3::escenarios {
void ComprobarZonaB(PPCContext& ctx, uint8_t* base);
}
namespace dbz3::escenarios {
// NIEVE PROPIA (X1esc 23/10): B3 no tiene el efecto 0x96 de B2 (stub sub_820A3A08) ni un builtin de clima. En Montanas
// nevadas (entrada 5007) el port dibuja copos 2D sobre la imagen del juego. Densidad, tamano y velocidad: medidos a ojo en
// la captura de PCSX2 (b2_699391_05) [H: sin leer los #ACE de B2]. Solo mientras el combate llama a TickNubes.
static std::atomic<int64_t> g_nieve_ms{0};
static int64_t AhoraMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
namespace S = dbz3::ingame::style;
class NievePanel : public rex::ui::ImGuiDialog {
 public:
  explicit NievePanel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  struct Copo { float x, y, vy, fase, r, a; };
  std::vector<Copo> copos_;
  uint32_t rng_ = 12345;
  float Al() { rng_ = rng_ * 1664525u + 1013904223u; return float(rng_ >> 8) / 16777216.f; }
  void OnDraw(ImGuiIO& io) override {
    if (AhoraMs() - g_nieve_ms.load() > 250) { copos_.clear(); return; }
    const S::Imagen g = S::GuestImage(io);
    const ImVec2 sz = g.Size();
    if (sz.x < 50 || sz.y < 50) return;
    if (copos_.empty()) {
      for (int i = 0; i < 220; ++i) copos_.push_back({Al(), Al(), 0.10f + 0.20f * Al(), Al() * 6.28f, 1.0f + 2.2f * Al(), 0.55f + 0.4f * Al()});
    }
    const float dt = std::min(0.1f, io.DeltaTime);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (Copo& c : copos_) {
      c.y += c.vy * dt;                                  // alturas de imagen por segundo
      c.fase += dt * 1.3f;
      c.x += std::sin(c.fase) * 0.012f * dt * 6.f + 0.012f * dt;   // vaiven y viento suave hacia la derecha
      if (c.y > 1.f) { c.y -= 1.f; c.x = Al(); }
      if (c.x > 1.f) c.x -= 1.f;
      if (c.x < 0.f) c.x += 1.f;
      const ImVec2 p(g.p0.x + c.x * sz.x, g.p0.y + c.y * sz.y);
      dl->AddCircleFilled(p, c.r * sz.y / 540.f + 0.6f, IM_COL32(255, 255, 255, int(c.a * 255)), 8);
    }
  }
};
static void CrearNievePanel() {
  static NievePanel* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new NievePanel(rt->imgui_drawer());
}
static void TickNieve() {
  if (!REXCVAR_GET(dbz3_nieve_propia) || g_activa < 0 || g_altas[size_t(g_activa - kSerie)].entrada != 5007) return;
  static bool pedido = false;
  if (!pedido) { pedido = true; dbz3::launcher::DeferUi(&CrearNievePanel); }
  g_nieve_ms = AhoraMs();
}

// =====================================================================================================================
// DS63 - CLIMA, DESTELLOS, PARTICULAS Y SUELO EN MOSAICO DE INFINITE WORLD
//
// Traduccion de las piezas visibles que quedaron en DS59 (38 faltas de profundidad 0-1). Datos:
//  - ClimateScript (EU 0x1E2150): el efecto de nieve/lluvia de ELF 0x44B4F0/0x44B540 ("SNOW") y 0x44B520/0x44B570
//    (caja 30x30, base -200, color 0.7/0.6/0.5, tamano 1.0, velocidad 0.3..3.5, 102 particulas).
//  - Rain/SnowToTheGroundEffect (EU 0x1E1FC0): MapLoader del efecto "Sand Dust" (0x493E08) = salpicaduras al suelo.
//  - s_Sand_Dust (EU 0x1E22E0 y su duplicado 0x1E3680): MapLoader del efecto "Sand Dust" (0x493E20/0x493E50).
//  - s_LensFlare (EU 0x1DE690) / Stage_SetupObjectsLensFlareB (EU 0x1E24E0): efecto "LensFlare" (0x493D58) y la
//    tabla de escalas de ELF 0x44A5E0 (16 B: tamano, distancia, intensidad, alfa).
//  - Stage_DrawTiledGround (EU 0x1E2590): 47 copias en rejilla de 450 alrededor de la camara (tabla 0x44B5B0).
// Los campos de IW que convierte campos_iw.py NO traen clima en el original: su paquete (#AMB de 881..909) no tiene
// #AME de RAIN/Sand Dust y su minijuego monta la escena con MiniGame_LoadStageModel, no con
// Stage_SetupObjectsWithClimate. Por eso el conversor no escribe `clima`/`polvo`/`destello` en sus stage.toml; el
// sistema queda listo para las altas que si los declaren (escenarios de combate de IW y mods) y no inventa clima.
// Dibujo: particulas y destellos con la camara del combate (ps2::CamaraCombate); suelo en mosaico con ps2::Escena y
// el modelo original del AFS de IW del jugador.
// =====================================================================================================================
struct CopaIw {
  float x = 0, y = 0, z = 0;
  float fase = 0, tam = 1;
  int grupo = 0;   // indice en Alta::climas (un alta puede declarar clima y polvo a la vez)
};
struct Salpicadura {
  float x = 0, y = 0, z = 0, edad = 0, vida = 0.25f;
};
struct ClimaIwEstado {
  std::vector<CopaIw> copos;
  std::vector<Salpicadura> salpicaduras;
  uint32_t zdd = 0;
  uint32_t sem = 0x13579BDFu;
  float t = 0;
};
static ClimaIwEstado g_clima_estado;
static std::atomic<int64_t> g_clima_ms{0};
static std::atomic<int64_t> g_destello_ms{0};
static std::atomic<int64_t> g_mosaico_ms{0};
static std::mutex g_mosaico_mutex;
static ps2::Modelo g_mosaico_modelo;
static bool g_mosaico_modelo_listo = false;
static uint32_t g_mosaico_zdd = 0;

static float RndClima(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return float(s >> 8) / 16777216.0f;
}

// EU 0x1E2150 ClimateScript: monta el clima del escenario para la variante activa. El original crea el efecto
// "RAIN" (0x493E18) en la variante 1 y, en la 0, el polvo de arena (s_Sand_Dust). Aqui cada alta declara sus
// emisores en stage.toml (`clima`/`polvo`); `setup` conserva el nombre de la funcion original.
// EU 0x1E1E90 Stage_SetupObjectsWithClimate
// EU 0x1E23C0 Stage_SetupObjectsWithSandDust
// EU 0x1E2150 ClimateScript
static void MontarClima(const Alta& a) {
  if (!REXCVAR_GET(dbz3_clima_iw)) return;
  g_clima_estado.copos.clear();
  g_clima_estado.salpicaduras.clear();
  g_clima_estado.t = 0;
  for (size_t gi = 0; gi < a.climas.size(); ++gi) {
    const ClimaP& c = a.climas[gi];
    if ((c.tipo == "dust" && !a.usa_polvo) || (c.tipo != "dust" && !a.usa_clima)) continue;
    // El emisor de nieve/lluvia de IW (ClimateScript 0x1E2150 -> Rain_SnowFallingEffect 0x44B4F0) crea 102
    // particulas; si el stage.toml no trae `n` se usa ese numero, no un 220 inventado.
    const uint32_t n = std::min<uint32_t>(c.n ? c.n : 102u, 4000u);
    for (uint32_t i = 0; i < n; ++i) {
      CopaIw p;
      p.grupo = int(gi);
      p.x = RndClima(g_clima_estado.sem) * 2 - 1;
      p.y = RndClima(g_clima_estado.sem);
      p.z = RndClima(g_clima_estado.sem) * 2 - 1;
      p.fase = RndClima(g_clima_estado.sem) * 6.2831853f;
      p.tam = 0.5f + RndClima(g_clima_estado.sem);
      g_clima_estado.copos.push_back(p);
    }
  }
}

// EU 0x1E1FC0 Rain/SnowToTheGroundEffect: al llegar una particula al suelo (Altura de suelo_iw) nace la
// salpicadura del efecto "Sand Dust" (0x493E08). La vida la fija el emisor del original (8 cuadros para la linea
// de impacto, tabla 0x44B520: 30/30/-200 y 8/-4/8/-4).
// EU 0x1E1FC0 Rain/SnowToTheGroundEffect
// EU 0x1E22E0 s_Sand_Dust
// EU 0x1E3680 s_Sand_Dust (duplicado de profundidad 1)
static void TickClima(const Alta& a, float dt) {
  // DS78: el destello (s_LensFlare 0x1DE690) es independiente del clima. Antes su reloj solo se refrescaba al final de
  // esta funcion, que sale antes si el alta no declara `clima`/`polvo`: un setup LensFlare sin clima no lo dibujaba.
  if (a.usa_destello && !a.destellos.empty()) g_destello_ms = AhoraMs();
  if (!REXCVAR_GET(dbz3_clima_iw) || a.climas.empty() || (!a.usa_clima && !a.usa_polvo)) return;
  g_clima_ms = AhoraMs();
  g_clima_estado.t += dt;
  uint8_t* base = BaseGuest();
  const uint32_t zdd = REX_LOAD_U32(kG + 32);
  if (zdd != g_clima_estado.zdd) {
    g_clima_estado.zdd = zdd;
    MontarClima(a);
  }
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  float cx = 0, cz = 0;
  bool hay = false;
  if (base && mgr) {
    float ojo[3], mira[3];
    if (dbz3::cine::LeerCamara(base, ojo, mira) && (ojo[0] != 0 || ojo[2] != 0)) cx = ojo[0], cz = ojo[2], hay = true;
    else cx = LeeFl(mgr + 2096 + 64), cz = LeeFl(mgr + 2096 + 72), hay = true;
  }
  if (!hay) return;
  for (size_t gi = 0; gi < a.climas.size(); ++gi) {
    const ClimaP& c = a.climas[gi];
    if ((c.tipo == "dust" && !a.usa_polvo) || (c.tipo != "dust" && !a.usa_clima)) continue;
    const float hx = c.area * 0.5f, hz = c.area * 0.5f;
    const float caida = c.vel > 0 ? c.vel : 60.0f;
    const float viento = c.viento;
    for (CopaIw& p : g_clima_estado.copos) {
      if (p.grupo != int(gi)) continue;
      p.fase += dt * (c.tipo == "dust" ? 2.4f : 1.1f);
      const float vaiv = (c.tipo == "rain" ? 0.0f : 0.35f) * std::sin(p.fase);
      p.x += (vaiv * caida * 0.15f + viento) * dt;
      p.z += std::cos(p.fase * 0.7f) * (c.tipo == "dust" ? 12.0f : 1.5f) * dt;
      p.y -= caida * dt;
      // posicion en mundo: centro de la camara + caja normalizada
      const float wx = cx + p.x * hx, wz = cz + p.z * hz;
      const float wy = p.y * c.alto;
      const float suelo = dbz3::suelo_iw::Altura(wx, wz, 0.0f);
      if (wy <= suelo || p.y <= 0.0f) {
        // EU 0x1E1FC0: salpicadura en el punto de impacto (solo lluvia/nieve, no polvo)
        if (c.tipo != "dust" && g_clima_estado.salpicaduras.size() < 400)
          g_clima_estado.salpicaduras.push_back({wx, suelo + 0.5f, wz, 0.0f, 0.25f});
        p.y += 1.0f;
        p.x = RndClima(g_clima_estado.sem) * 2 - 1;
        p.z = RndClima(g_clima_estado.sem) * 2 - 1;
      }
      if (p.x < -1) p.x += 2; else if (p.x > 1) p.x -= 2;
      if (p.z < -1) p.z += 2; else if (p.z > 1) p.z -= 2;
    }
  }
  for (Salpicadura& s : g_clima_estado.salpicaduras) s.edad += dt;
  std::erase_if(g_clima_estado.salpicaduras, [](const Salpicadura& s) { return s.edad >= s.vida; });
}

// Tabla de ELF 0x44B5B0 (EU): 47 vectores xyz de la rejilla del suelo (paso 450; el original copia 0x5E palabras =
// 47 x 4 floats). Stage_DrawTiledGround (EU 0x1E2590) dibuja el modelo del suelo en cada uno de ellos.
static const float kMosaicoOff[47][3] = {
    {1350, 0, -900}, {1350, 0, -450}, {1350, 0, 0},    {1350, 0, 450},   {1350, 0, 900},
    {900, 0, -1350}, {900, 0, -900},  {900, 0, -450},  {900, 0, 0},     {900, 0, 450},
    {900, 0, 900},   {900, 0, 1350},  {450, 0, -1350}, {450, 0, -900},  {450, 0, -450},
    {450, 0, 0},     {450, 0, 450},   {450, 0, 900},   {450, 0, 1350},  {0, 0, -1800},
    {0, 0, -1350},   {0, 0, -900},    {0, 0, -450},    {0, 0, 0},       {0, 0, 450},
    {0, 0, 900},     {0, 0, 1350},    {0, 0, 1800},    {-450, 0, -1350}, {-450, 0, -900},
    {-450, 0, -450}, {-450, 0, 0},    {-450, 0, 450},  {-450, 0, 900},  {-450, 0, 1350},
    {-900, 0, -1350}, {-900, 0, -900}, {-900, 0, -450}, {-900, 0, 0},   {-900, 0, 450},
    {-900, 0, 900},  {-900, 0, 1350}, {-1350, 0, -900}, {-1350, 0, -450}, {-1350, 0, 0},
    {-1350, 0, 450}, {-1350, 0, 900},
};

// EU 0x1E2590 Stage_DrawTiledGround: carga una vez el modelo original del suelo (si el alta lo declara en
// stage.toml) y lo deja listo para las 47 copias. `mosaico = "paso,afs,amb_hijo,amo,amt,variante"`.
// EU 0x1E2590 Stage_DrawTiledGround
static void CargarMosaico(const MosaicoP& m) {
  if (!m.on || !REXCVAR_GET(dbz3_mosaico_iw)) return;
  uint8_t* base = BaseGuest();
  if (!base) return;
  const uint32_t zdd = REX_LOAD_U32(kG + 32);
  if (zdd == g_mosaico_zdd) return;
  g_mosaico_zdd = zdd;
  {
    std::lock_guard<std::mutex> lg(g_mosaico_mutex);
    g_mosaico_modelo_listo = false;
    g_mosaico_modelo = ps2::Modelo{};
  }
  pasarela::AfsPs2 afs;
  if (!afs.Abrir(pasarela::BuscarIW(), "DATA_CMN.AFS")) return;
  const std::vector<uint8_t> e = afs.Entrada(m.afs);
  if (e.empty()) return;
  ps2::Modelo mod;
  if (m.amb_hijo == 0xFFFFFFFFu) {
    mod = ps2::CargarDeAfs(afs, m.amo, m.amt, false);
  } else {
    const std::vector<uint8_t> amb = pasarela::HijoAmb(e, m.amb_hijo);
    if (amb.empty()) return;
    mod = ps2::CargarModelo(amb, m.amo, m.amt, m.variante);
  }
  if (!mod.malla) return;
  size_t tris = mod.malla.tris.size();
  {
    std::lock_guard<std::mutex> lg(g_mosaico_mutex);
    g_mosaico_modelo = std::move(mod);
    g_mosaico_modelo_listo = true;
  }
  g_mosaico_ms = AhoraMs();
  REXLOG_INFO("dbz3 escenarios: mosaico: modelo del suelo cargado ({} triangulos; tabla 0x44B5B0, 47 copias)", tris);
}

// DS63: atlas opcional del AFS de IW del jugador. `afs` = entrada de DATA_CMN (paquete de escenario o #AMT),
// `hijo` = hijo #AMT dentro de su #AMB (0xFFFFFFFF = la entrada entera), `tex` = indice de TexturaAmt.
static bool CargarTexturaIw(uint32_t afs_e, uint32_t hijo, uint32_t tex, pasarela::Imagen& out) {
  pasarela::AfsPs2 afs;
  if (!afs.Abrir(pasarela::BuscarIW(), "DATA_CMN.AFS")) return false;
  std::vector<uint8_t> e = afs.Entrada(afs_e);
  if (e.empty()) return false;
  std::vector<uint8_t> amt;
  if (hijo != 0xFFFFFFFFu) {
    amt = pasarela::HijoAmb(e, hijo);
    if (amt.empty()) {   // paquete de escenario: el banco de efectos va en su #AMB (hijo 4)
      const std::vector<uint8_t> amb = pasarela::HijoAmb(e, 4);
      if (!amb.empty()) amt = pasarela::HijoAmb(amb, hijo);
    }
  } else {
    amt = std::move(e);
  }
  if (amt.empty()) return false;
  out = pasarela::TexturaAmt(amt, int(tex));
  return bool(out);
}

class ClimaIwPanel : public rex::ui::ImGuiDialog {
 public:
  explicit ClimaIwPanel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  const ps2::Tex* TexDe(uint32_t afs, uint32_t hijo, uint32_t tex) {
    if (!afs) return nullptr;
    const uint64_t k = (uint64_t(afs) << 40) ^ (uint64_t(hijo & 0xFFFFFFu) << 16) ^ uint64_t(tex & 0xFFFFu);
    auto it = tex_cache_.find(k);
    if (it != tex_cache_.end()) return it->second ? &it->second : nullptr;
    pasarela::Imagen im;
    if (!CargarTexturaIw(afs, hijo, tex, im)) {
      tex_cache_.emplace(k, ps2::Tex{});
      return nullptr;
    }
    auto [p, ok] = tex_cache_.emplace(k, ps2::Crear(im, false));
    return ok && p->second ? &p->second : nullptr;
  }

  void OnDraw(ImGuiIO& io) override {
    const bool clima = AhoraMs() - g_clima_ms.load() < 250;
    const bool destello = AhoraMs() - g_destello_ms.load() < 250;
    const bool mosaico = AhoraMs() - g_mosaico_ms.load() < 250;
    if (!clima && !destello && !mosaico) return;
    uint8_t* base = BaseGuest();
    if (!base) return;
    const ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    if (!cam.ok) return;
    if (g_activa < 0) return;
    const Alta& a = g_altas[size_t(g_activa - kSerie)];
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // EU 0x1E2590 Stage_DrawTiledGround: 47 copias alrededor de la camara cuantizada a 450 (el original:
    // floor(cam/450)*450; cada copia suma su vector de 0x44B5B0).
    if (mosaico && REXCVAR_GET(dbz3_mosaico_iw) && a.mosaico.on) {
      std::lock_guard<std::mutex> lg(g_mosaico_mutex);
      if (g_mosaico_modelo_listo && g_mosaico_modelo.malla) {
        if (!tex_intentado_) {
          tex_intentado_ = true;
          tex_mosaico_ = ps2::Texturas(g_mosaico_modelo);
        }
        float ojo[3], mira[3];
        float ox = 0, oz = 0;
        if (dbz3::cine::LeerCamara(base, ojo, mira)) ox = ojo[0], oz = ojo[2];
        const float qx = std::floor(ox / a.mosaico.paso) * a.mosaico.paso;
        const float qz = std::floor(oz / a.mosaico.paso) * a.mosaico.paso;
        ps2::Escena esc;
        esc.Empezar();
        for (const auto& off : kMosaicoOff)
          esc.Poner(cam, g_mosaico_modelo.malla, tex_mosaico_, {qx + off[0], 0, qz + off[2]}, 0.0f, 1.0f);
        esc.Pintar(bg);
      }
    }
    // EU 0x1E2150 ClimateScript / EU 0x1E1FC0 / EU 0x1E22E0: particulas y salpicaduras.
    if (clima && REXCVAR_GET(dbz3_clima_iw) && !a.climas.empty()) {
      const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
      float cx = 0, cz = 0;
      bool hay = false;
      if (mgr) {
        float ojo[3], mira[3];
        if (dbz3::cine::LeerCamara(base, ojo, mira) && (ojo[0] != 0 || ojo[2] != 0)) cx = ojo[0], cz = ojo[2], hay = true;
        else cx = LeeFl(mgr + 2096 + 64), cz = LeeFl(mgr + 2096 + 72), hay = true;
      }
      if (hay) {
        for (size_t gi = 0; gi < a.climas.size(); ++gi) {
          const ClimaP& c = a.climas[gi];
          const uint32_t col = c.color;
          const int cr = int((col >> 16) & 0xFF), cg = int((col >> 8) & 0xFF), cb = int(col & 0xFF);
          const float hx = c.area * 0.5f, hz = c.area * 0.5f;
          for (const CopaIw& p : g_clima_estado.copos) {
            if (p.grupo != int(gi)) continue;
            const float wx = cx + p.x * hx, wy = p.y * c.alto, wz = cz + p.z * hz;
            ImVec2 s;
            float k = 0;
            if (!cam.P({wx, wy, wz}, s, k)) continue;
            const float tam = std::clamp(p.tam * c.tam * k, 0.6f, 24.0f);
            const ps2::Tex* at = TexDe(c.afs, c.hijo_amt, c.tex);
            if (at && *at) {
              const int alfa = c.tipo == "dust" ? 90 : 210;
              fg->AddImage(at->id(), ImVec2(s.x - tam, s.y - tam), ImVec2(s.x + tam, s.y + tam), ImVec2(0, 0),
                           ImVec2(1, 1), IM_COL32(cr, cg, cb, alfa));
              continue;
            }
            if (c.tipo == "rain") {
              ImVec2 s2;
              float k2 = 0;
              if (!cam.P({wx, wy + c.tam * 6.0f, wz}, s2, k2)) continue;
              fg->AddLine(s, s2, IM_COL32(cr, cg, cb, 170), std::max(1.0f, tam * 0.25f));
            } else {
              const int alfa = c.tipo == "dust" ? 70 : 190;
              fg->AddCircleFilled(s, tam, IM_COL32(cr, cg, cb, alfa), 8);
            }
          }
        }
        // EU 0x1E1FC0 Rain/SnowToTheGroundEffect: salpicadura (anillo que crece y se apaga).
        for (const Salpicadura& sp : g_clima_estado.salpicaduras) {
          ImVec2 s;
          float k = 0;
          if (!cam.P({sp.x, sp.y, sp.z}, s, k)) continue;
          const float t = std::clamp(sp.edad / sp.vida, 0.0f, 1.0f);
          fg->AddCircle(s, (2.0f + 10.0f * t) * k, IM_COL32(230, 240, 255, int(150 * (1 - t))), 12,
                        std::max(1.0f, 2.0f * k));
        }
      }
    }
    // EU 0x1DE690 s_LensFlare
    // EU 0x1E24E0 Stage_SetupObjectsLensFlareB
    // Destello de pantalla en la direccion
    // del sol. La tabla 0x44A5E0 da el tamano y la intensidad; aqui el color y el tamano vienen del stage.toml.
    if (destello && a.usa_destello && REXCVAR_GET(dbz3_destello_iw)) {
      for (const DestelloP& d : a.destellos) {
        ImVec2 s;
        float k = 0;
        if (!cam.P({d.x, d.y, d.z}, s, k)) continue;
        if (s.x < -io.DisplaySize.x || s.y < -io.DisplaySize.y || s.x > 2 * io.DisplaySize.x ||
            s.y > 2 * io.DisplaySize.y)
          continue;
        const int cr = int((d.color >> 16) & 0xFF), cg = int((d.color >> 8) & 0xFF), cb = int(d.color & 0xFF);
        const float tam = std::clamp(d.tam * k, 4.0f, 400.0f);
        const ps2::Tex* at = TexDe(d.afs, d.hijo_amt, d.tex);
        if (at && *at) {
          fg->AddImage(at->id(), ImVec2(s.x - tam, s.y - tam), ImVec2(s.x + tam, s.y + tam), ImVec2(0, 0), ImVec2(1, 1),
                       IM_COL32(255, 255, 255, 200));
          continue;
        }
        fg->AddCircleFilled(s, tam, IM_COL32(cr, cg, cb, 40), 48);
        fg->AddCircleFilled(s, tam * 0.55f, IM_COL32(cr, cg, cb, 70), 48);
        fg->AddCircleFilled(s, tam * 0.22f, IM_COL32(255, 255, 255, 140), 32);
        for (int j = 1; j <= 3; ++j)   // fantasmas del flare a lo largo de la linea al centro
          fg->AddCircleFilled(ImVec2(s.x + (io.DisplaySize.x * 0.5f - s.x) * (0.18f * j),
                                     s.y + (io.DisplaySize.y * 0.5f - s.y) * (0.18f * j)),
                              tam * (0.18f - 0.03f * j), IM_COL32(cr, cg, cb, 26), 24);
      }
    }
  }

 private:
  std::vector<ps2::Tex> tex_mosaico_;
  bool tex_intentado_ = false;
  std::map<uint64_t, ps2::Tex> tex_cache_;
};
static void CrearClimaPanel() {
  static ClimaIwPanel* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new ClimaIwPanel(rt->imgui_drawer());
}
// Se llama cada cuadro desde PasoCapas. Deja el panel pedido una sola vez.
static void TickClimaIw(const Alta& a) {
  if (a.climas.empty() && a.destellos.empty() && !a.mosaico.on) return;
  static bool pedido = false;
  if (!pedido) { pedido = true; dbz3::launcher::DeferUi(&CrearClimaPanel); }
  if (a.mosaico.on) g_mosaico_ms = AhoraMs();
  CargarMosaico(a.mosaico);
}

// EU 0x306570 Node_ScrollBackgroundUV: las 8 capas UV del fondo del campo (iRam0049cdb8+0x5f0). El original suma a cada
// capa, CUADRO A CUADRO, su base (capa[0], capa[1]) y, en los minijuegos de vuelo, el arrastre por el movimiento de la
// camara (capa[2]*dx, capa[3]*dz) recortado a la extension del mundo cuando el flag del #MCF lo pide (bits 1 y 2), y la
// envuelve en [0,1). Aqui la capa mueve las ventanas de las partes de cielo/nubes del #ZDD cargado (misma firma de 3
// ventanas, 132 B). Se llama cada fotograma desde los ganchos de sub_82114910 / sub_820FD678 (ComprobarZonaB).
// EU 0x1E0EE0 Stage_UpdateTick: por cuadro, si no hay pausa, copia el delta de tiempo (fRam004db2c4 = *(ca70+0x8B80))
// y llama a Task_RunUpdateCallbacks (l.183569-183590). Aqui el delta es el reloj real del cuadro (dt, topado a 0,25 s)
// y la lista de updates es la del alta: este es el unico punto por cuadro de las animaciones del escenario.
// DIFIERE LUNA_ESCEN2_0 (DS64), no aplicado: las guardas +0x8b7c/+0x8d26/+0x8bb0/+0x8bd8 y el delta +0x8b80 son
// offsets del gestor de IW (iRam0049ca70), NO del mgr de B3 (0x823D6CF4); leerlos en el mgr de B3 seria inventar.
// El port ya solo corre desde sub_82114910 (estados de pelea 3/4/5/8) y el delta real equivale a fRam004db2c4=1 a
// 60 Hz. Queda [H] si algun dia se localiza el campo de pausa/delta del mgr de B3.
// EU 0x1DDB50 Task_RunUpdateCallbacks: recorre la lista de nodos de MapLoader y llama a la funcion de update de cada
// uno (UV del suelo y del cielo 0x1E2750/0x1E29A0, UV de objeto 0x1E2C60, material 0x1DD590, modelo 0x1DD660 y UV de
// modelo 0x1DD4F0). Aqui se recorren las nubes/capas, los materiales y los dinamicos del alta.
// DIFIERE LUNA_ESCEN2_0 (DS64), estructura que no cambia lo que se ve: 0x1DDB50 omite los callbacks con bits 0x2800
// de la lista PS2; el port no tiene esos nodos (sus listas son las del stage.toml) y recorre entera la suya. 0x1E2880
// dibuja el cielo con pases GS y niebla alrededor, que en B3 son del renderizador (src/clima.cpp sub_820C2058). Y
// 0x1E2750/0x1E29A0 recorren los registros de canal de 16 B del objeto con mascara 0x3C00 y llaman Anim_StepUvAnim;
// el port aplica la capa del #MCF a las ventanas (una vez por parte) y el #MTA lo llevan los minijuegos
// (src/iw_minijuegos.cpp AplicarDerivaUv, src/iw_mini_vuelo.cpp AplicarDerivaUvCasa). No se replica la lista PS2.
static void PasoCapas(uint8_t* base) {
  TickNieve();
  if (g_activa < 0) return;
  Alta& a = g_altas[size_t(g_activa - kSerie)];
  TickClimaIw(a);   // DS63: deja pedido el panel de clima/destellos/mosaico si el alta los declara
  if (a.nubes.empty() && a.mats.empty() && a.dinamicos.empty() && a.climas.empty() && a.destellos.empty() &&
      a.uvobjetos.empty() && !a.mosaico.on)
    return;
  static uint32_t zdd_vista = 0;
  static std::chrono::steady_clock::time_point t_ant;
  static int ticks = 0;
  static float pos_x = 0, pos_z = 0;   // posicion de la camara en el cuadro anterior (arrastre de las capas)
  static bool pos_hay = false;
  static float cam_base_x = 0, cam_base_z = 0;   // sitio de la camara al cargar el #ZDD (0x1E2880: cielo con la camara)
  static bool cam_base_hay = false;
  const uint32_t zdd = REX_LOAD_U32(kG + 32);
  if (!zdd || zdd < 0x40000000u) return;
  if (zdd != zdd_vista) {   // escenario (re)cargado: se vuelven a buscar las firmas en el #ZDD
    zdd_vista = zdd;
    ticks = 0;
    pos_hay = false;
    cam_base_hay = false;
    size_t hallados = 0;
    for (Nube& nb : a.nubes) {
      nb.guest = 0; nb.acc_u = nb.acc_v = 0; nb.off_x = nb.off_z = 0;
      const uint8_t* ini = base + zdd;
      const uint8_t* fin = ini + nb.tam;
      const uint8_t* p = std::search(ini, fin, nb.firma.begin(), nb.firma.end());
      if (p != fin) { nb.guest = uint32_t(p - base); ++hallados; }
    }
    size_t mat_hallados = 0;
    uint32_t tam_zdd = 0;   // el #ZDD del alta: el mayor de los tramos que dan las firmas de las nubes
    for (const Nube& nb : a.nubes) tam_zdd = std::max(tam_zdd, nb.tam);
    for (Material& m : a.mats) {
      m.guest = 0; m.t = 0;
      if (!tam_zdd) continue;   // sin tamaño del #ZDD no se busca (no se lee fuera de la memoria del guest)
      const uint8_t* ini = base + zdd;
      const uint8_t* fin = ini + tam_zdd;
      const uint8_t* p = std::search(ini, fin, m.firma.begin(), m.firma.end());
      if (p != fin) { m.guest = uint32_t(p - base); ++mat_hallados; }
    }
    // EU 0x1E2ED0 Stage_CreateAnimUVObject: el objeto UV es la entrada (0,1); si stage.toml trae la firma de sus
    // ventanas (16 B) se localizan en el #ZDD cargado para aplicarles la deriva. Sin firma no se toca ningun vertice
    // (el modelo no viaja en el alta o lo pinta su minijuego) y se avisa una vez.
    for (ObjetoUv& o : a.uvobjetos) {
      o.guest = 0;
      o.tiempo = 0;
      std::memset(o.estado, 0, sizeof(o.estado));
      std::memset(o.aplicado_u, 0, sizeof(o.aplicado_u));
      std::memset(o.aplicado_v, 0, sizeof(o.aplicado_v));
      if (!o.firma.empty() && tam_zdd) {
        const uint8_t* ini = base + zdd;
        const uint8_t* fin = ini + tam_zdd;
        const uint8_t* p = std::search(ini, fin, o.firma.begin(), o.firma.end());
        if (p != fin) o.guest = uint32_t(p - base);
      }
      o.zdd = zdd;
      if (!o.guest && !o.avisado) {
        o.avisado = true;
        REXLOG_INFO("dbz3 escenarios: objeto UV (entrada {}, {} canales) sin ventanas en el #ZDD: solo estado "
                    "(el modelo no viaja en el alta; lo pinta su minijuego si existe)",
                    o.entrada, o.canales.size());
      }
    }
    // EU 0x1DEF90 Stage_ProcessDynamicObjects: el original recorre el tramo de dinamicos [+0x88,+0x90) de la
    // parte cargada y crea cada uno con s_Dynamic (0x1DED90: MapLoader + su lista UV). En el alta del port esos
    // objetos NO viajan: se quedan fuera a proposito y los pinta su minijuego (900/33 -> iw_minijuegos.cpp;
    // 881/883 -> iw_mini_vuelo.cpp). Aqui solo se registran/avisan (sustitucion, no se crea un nodo PS2).
    for (const Dinamico& d : a.dinamicos)
      REXLOG_INFO("dbz3 escenarios: objeto dinamico '{}' (modelo {}, rejilla {}) en ({:.1f}, {:.1f}, {:.1f})", d.nombre,
                  d.modelo, d.rejilla, d.x, d.y, d.z);
    REXLOG_INFO("dbz3 escenarios: capas: {} de {} partes de cielo/nubes, {} de {} materiales, {} dinamicos; animacion UV "
                "del #MTA: '{}' (si el modelo no viaja en el alta la pinta su minijuego)",
                hallados, a.nubes.size(), mat_hallados, a.mats.size(), a.dinamicos.size(), a.uvanim);
    t_ant = std::chrono::steady_clock::now();
    return;
  }
  const auto ahora = std::chrono::steady_clock::now();
  const float dt = std::min(0.25f, std::chrono::duration<float>(ahora - t_ant).count());
  t_ant = ahora;
  if (dt <= 0.f) return;
  TickClima(a, dt);   // DS63: ClimateScript + Rain/SnowToTheGroundEffect + s_Sand_Dust
  // EU 0x1DDD80 Task_RunUpdateCallbacksFlag2800: pase de updates de los nodos con bits 0x2800. El objeto UV normal
  // (0x100) no entra en ese pase: lo actualiza el callback propio del nodo (Stage_AnimObjectUpdateUVScroll).
  PasarCallbacks2800(a, dt);
  // EU 0x1E2C60 Stage_AnimObjectUpdateUVScroll (callback de update del objeto 0x84000000): avanza tiempo, evalua los
  // canales #MTA y aplica la deriva UV a las ventanas del objeto. Antes no se hacia para los escenarios de combate;
  // las capas del #MCF de mas abajo son otro sistema (Node_ScrollBackgroundUV 0x306570).
  for (ObjetoUv& o : a.uvobjetos)
    if ((o.flags & 0x2800) != 0x2800) ActualizarObjetoUv(o, dt);
  // Desplazamiento de la camara desde el ultimo cuadro (el original pasa a Node_ScrollBackgroundUV el delta de la
  // camara, MgFly_UpdateStageByCameraDelta 0x366B10). La camara del juego esta en mgr+0x190 y la lee
  // dbz3::cine::LeerCamara (medida en cine.cpp): es la que usa el original en Stage_DrawCameraFollowModel
  // (0x1E2880: traslacion = (camara.x, 0, camara.z), leida de iRam0049ca70+0x244). Si no hay camara, se usa la X/Z
  // del luchador 0 como respaldo (la camara del port le sigue). El segundo pase del cuadro ve la misma posicion y
  // su delta es 0.
  const uint32_t mgr_cam = REX_LOAD_U32(kMgrPtr);
  bool cam_hay = mgr_cam != 0;
  float cam_x = 0.0f, cam_z = 0.0f;
  float ojo_cam[3], mira_cam[3];
  if (cam_hay && dbz3::cine::LeerCamara(base, ojo_cam, mira_cam) &&
      (ojo_cam[0] != 0.0f || ojo_cam[1] != 0.0f || ojo_cam[2] != 0.0f)) {   // ojo sin inicializar: al respaldo
    cam_x = ojo_cam[0], cam_z = ojo_cam[2];
  } else if (cam_hay) {
    cam_x = LeeFl(mgr_cam + 2096 + 64);
    cam_z = LeeFl(mgr_cam + 2096 + 72);
  }
  float dx = 0, dz = 0;
  if (a.arrastre && cam_hay) {
    if (pos_hay) { dx = cam_x - pos_x; dz = cam_z - pos_z; }
    pos_x = cam_x; pos_z = cam_z; pos_hay = true;
  }
  auto envolver = [](float& d, float ext) {   // for (; ext*0.5 <= d; d -= ext); for (; d < -ext*0.5; d += ext);
    if (!(ext > 1e-6f) || !std::isfinite(d)) { d = 0; return; }
    int guarda = 0;
    while (d >= ext * 0.5f && guarda++ < 256) d -= ext;
    while (d < -ext * 0.5f && guarda++ < 256) d += ext;
  };
  if (a.capa_flags & 1) envolver(dx, a.capa_ex);
  if (a.capa_flags & 2) envolver(dz, a.capa_ey);
  static const float mult = std::getenv("DBZ3_NUBES_MULT") ? float(std::atof(std::getenv("DBZ3_NUBES_MULT"))) : 1.f;   // PRUEBAS: acelera la deriva para verla en capturas
  // EU 0x306570 Node_ScrollBackgroundUV: avanza el estado UV de las capas del #MCF (base por cuadro + arrastre por
  // unidad de camara), envolviendo en [0,1) y recortando el arrastre a la extension si el flag del #MCF lo pide. El
  // original lleva el estado en iRam0049cdb8+0x5f0 (8 capas) y lo aplica al dibujar el fondo; el port lo aplica a
  // las ventanas (UV +36/+40) de las partes de cielo/nubes del #ZDD, que es lo mismo que ve la GPU.
  // La animacion UV POR OBJETO con #MTA (EU 0x1DD4F0 Anim_StepUvAnim: UvAnim_EvalAllChannels avanza el tiempo y
  // los canales 4/5 dan du/dv) NO se hace aqui: el modelo del geiser (900) y la casa del vuelo (881/883) no viajan
  // en el alta y los pinta su minijuego (iw_minijuegos.cpp LeerUvMta / iw_mini_vuelo.cpp LeerDerivasMta). El original
  // suma la base una vez por actualizacion con el delta de cuadro fRam004db2c4; aqui se usa el mismo delta del
  // gestor (mgr+0x8b80, 1.0 a 60 Hz), asi el avance es por cuadro del juego y no por segundos del port.
  for (Nube& nb : a.nubes) {
    if (!nb.guest) continue;
    float bu = nb.du, bv = nb.dv, px = nb.px, py = nb.py;
    if (nb.capa >= 0 && nb.capa < int(a.capas.size())) {   // capa del #MCF (converter v20)
      const Capa& c = a.capas[size_t(nb.capa)];
      bu = c.u; bv = c.v; px = c.px; py = c.py;
    }
    float du = (bu + px * dx) * 60.f * dt * mult, dv = (bv + py * dz) * 60.f * dt * mult;
    nb.acc_u += du; nb.acc_v += dv;                        // estado en [0,1), como los while del original
    while (nb.acc_u >= 1.f) { du -= 1.f; nb.acc_u -= 1.f; }   // (la textura se repite con periodo 1)
    while (nb.acc_v >= 1.f) { dv -= 1.f; nb.acc_v -= 1.f; }
    while (nb.acc_u < 0.f) { du += 1.f; nb.acc_u += 1.f; }
    while (nb.acc_v < 0.f) { dv += 1.f; nb.acc_v += 1.f; }
    // EU 0x1E2750 Stage_UpdateUVScroll
    // EU 0x1E29A0 Stage_UpdateUVScroll2
    // Por cada registro de canal de 16 B del objeto ([1] canal 6..13,
    // [2] du entero, [3] dv entero, l.184806-184847) suma al estado del canal `(float)du * fRam004db2c4` y
    // `(float)dv * fRam004db2c4`, con envoltura 1 -> 0 y < 0 -> 1 (`if (1.0 < u) u = 0; else if (u < 0.0) u = 1.0`),
    // y luego Anim_StepUvAnim si el objeto tiene animacion.
    // EU 0x1E29A0 Stage_UpdateUVScroll2 (cielo): identico al de suelo (l.184849-184890) para el canal del objeto 1.
    // Aqui el incremento sale de la capa del #MCF (Node_ScrollBackgroundUV 0x306570 lo deja en su estado) y se aplica
    // a las ventanas de la parte con 60*dt (el mismo avance que fRam004db2c4=1 a 60 Hz); la envoltura es la misma
    // (los `while` repiten el `if` del original hasta dejarlo en [0,1)). DIFIERE LUNA_ESCEN2_0 (estructura, ver
    // informe DS64): no se recorre la lista de registros de canal ni su mascara 0x3C00 (el stage.toml ya asigna una
    // capa por parte), Anim_StepUvAnim lo llevan los minijuegos que pintan esos modelos y el delta +0x8b80 es del
    // gestor de IW, no del de B3. [H] que la lista de registros de 16 B del original sea exactamente el #MCF: no
    // esta demostrado (ver ESCENARIOS.md §15).
    for (uint32_t i = 0; i < nb.n; ++i) {
      const uint32_t o = nb.guest + 44 * i + 36;
      float u, v;
      uint32_t b0 = REX_LOAD_U32(o), b1 = REX_LOAD_U32(o + 4);
      std::memcpy(&u, &b0, 4); std::memcpy(&v, &b1, 4);
      u += du; v += dv;
      std::memcpy(&b0, &u, 4); std::memcpy(&b1, &v, 4);
      REX_STORE_U32(o, b0); REX_STORE_U32(o + 4, b1);
    }
    // EU 0x1E2880 Stage_DrawCameraFollowModel: el cielo se dibuja trasladado a (camara.x, 0, camara.z); si
    // Battle_GetStateOrInvalid (EU 0x20E290) devuelve 0, la traslacion es nula. Se usa la camara del juego
    // (dbz3::cine::LeerCamara, mgr+0x190); el original lee el bloque de camara iRam0049ca70+0x244. Las ventanas del
    // cielo estan en coordenadas del campo (pegadas a su hueso raiz), de modo
    // que lo que se suma es el movimiento desde el sitio de carga del #ZDD: el cielo queda siempre a la misma
    // distancia del ojo (en el original su modelo esta centrado en el origen y se dibuja con esa traslacion).
    if (nb.sigue && cam_hay) {
      if (!cam_base_hay) { cam_base_x = cam_x; cam_base_z = cam_z; cam_base_hay = true; }
      const float des_x = cam_x - cam_base_x, des_z = cam_z - cam_base_z;
      const float ddx = des_x - nb.off_x, ddz = des_z - nb.off_z;
      if (ddx != 0.0f || ddz != 0.0f) {
        for (uint32_t i = 0; i < nb.n; ++i) {
          const uint32_t o = nb.guest + 44 * i;   // ventana del #AWO: +0 x, +4 y, +8 z
          float x, z;
          uint32_t b0 = REX_LOAD_U32(o), b2 = REX_LOAD_U32(o + 8);
          std::memcpy(&x, &b0, 4); std::memcpy(&z, &b2, 4);
          x += ddx; z += ddz;
          std::memcpy(&b0, &x, 4); std::memcpy(&b2, &z, 4);
          REX_STORE_U32(o, b0); REX_STORE_U32(o + 8, b2);
        }
        nb.off_x = des_x, nb.off_z = des_z;
      }
    }
  }
  // EU 0x1DD590 Anim_StepMaterialAnim: avanza la pista de material con bucle. El original aplica el cuadro con
  // FUN_0013bdf0, suma `velocidad * fRam004db2c4` (piVar1[5]) y vuelve a 0 al llegar al ultimo cuadro del bloque;
  // EU 0x13BDF0 FUN_0013bdf0 NO es este avance: recorre las entradas de un bloque de animacion y mapea cada indice
  // de hueso mediante FUN_001370A0 al indice almacenado en +0x0E. El port de escenario solo interpola claves RGBA;
  // el mapeo de huesos pertenece al motor de animacion y no se reproduce aqui.
  // el stage.toml del port solo lleva el bucle (`material`/`matclave`), asi que avanza 60*dt claves/s (una por
  // cuadro a 60 Hz) e interpola.
  // Los 9 campos de IW NO traen pista de material (su bloque de animacion esta en la tabla del escenario,
  // 0x4DB280+0x1C, no en el paquete del campo): con estos campos el bucle no hace nada (ver informe DS58).
  for (Material& m : a.mats) {
    if (!m.guest || m.claves.empty()) continue;
    m.t += 60.f * dt;
    while (m.frames && m.t >= float(m.frames)) m.t -= float(m.frames);
    const MatClave* ka = &m.claves.front();
    const MatClave* kb = ka;
    for (const MatClave& k : m.claves) {
      if (float(k.f) <= m.t) ka = &k;
      if (float(k.f) >= m.t) { kb = &k; break; }
      kb = &k;
    }
    const float tt = kb->f > ka->f ? (m.t - float(ka->f)) / float(kb->f - ka->f) : 1.f;
    uint32_t rgba = 0;
    for (int c = 0; c < 4; ++c) {
      const float a0 = float((ka->rgba >> (8 * c)) & 0xFF), a1 = float((kb->rgba >> (8 * c)) & 0xFF);
      const uint32_t v = uint32_t(std::clamp(a0 + (a1 - a0) * tt, 0.0f, 255.0f) + 0.5f) & 0xFF;
      rgba |= v << (8 * c);
    }
    REX_STORE_U32(m.guest, rgba);   // color del material HD (primer campo del registro de 0x50 B, amo2awo.py)
  }
  if (++ticks == 1 || ticks == 61 || ticks == 301) {   // medida: UV de la 1.a ventana de la 1.a parte en cuadros conocidos
    for (const Nube& nb : a.nubes) if (nb.guest) {
      float u, v;
      uint32_t b0 = REX_LOAD_U32(nb.guest + 36), b1 = REX_LOAD_U32(nb.guest + 40);
      std::memcpy(&u, &b0, 4); std::memcpy(&v, &b1, 4);
      REXLOG_INFO("dbz3 escenarios: nubes: tick {} uv = ({:.5f}, {:.5f}) acc = ({:.5f}, {:.5f})", ticks, u, v, nb.acc_u, nb.acc_v);
      break;
    }
  }
}
// ---------------------------------------------------------------------------------------------------------------------
// DS73 26/10: FISICA SECUNDARIA (cadenas de huesos y tela) de IW. Las auditorias LUNA_SESC2/3/4 devolvieron a falta
// 0x261DB0 PhysGroup_Update, 0x260020 Cloth_UpdateObject, 0x261770 BoneChain_Update, 0x261BF0 BoneChain_WarmUpGroup,
// 0x265700 Cloth_SimulateStep, 0x261480 BoneChain_UpdateRoot, 0x2615D0 BoneChain_UpdateAllSegments,
// 0x261870 BoneChain_ResetPose y 0x262F30 Skel_ApplyChainForBone.
// Se traduce el ALGORITMO COMPLETO con los datos que el original usa (grupo, cadenas, telas, esferas y huesos): la
// estructura de control, el orden de llamadas, las constantes y las formulas salen del decomp. Los parametros de
// cada cadena/tela salen del modelo de IW, que no viaja en las altas de B3 (no hay descriptor de fisica en el #ZDD):
// por eso el bloque queda como motor listo para que el dueno de ps2_modelo/anim_iw lo alimente con esos datos.
// [H] El llamante: en IW lo hace el update del modelo (0x280800 PhysGroup_Update(*(modelo+0x58))); el port usa los
// modelos de B3, que no traen grupos de fisica. Sin ese dato no se puede mover un hueso real sin inventarlo.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

inline ps2::V3 V3Suma(const ps2::V3& a, const ps2::V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline ps2::V3 V3Resta(const ps2::V3& a, const ps2::V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline ps2::V3 V3Escala(const ps2::V3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float V3Punto(const ps2::V3& a, const ps2::V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline ps2::V3 V3Cruza(const ps2::V3& a, const ps2::V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float V3Largo(const ps2::V3& a) { return std::sqrt(V3Punto(a, a)); }
inline ps2::V3 V3Normal(const ps2::V3& a) {
  const float l = V3Largo(a);
  return l > 1e-6f ? V3Escala(a, 1.0f / l) : ps2::V3{0, 1, 0};
}
inline ps2::V3 Traslacion(const ps2::M34& m) { return {m.m[3], m.m[7], m.m[11]}; }
inline ps2::M34 Identidad() { return ps2::M34{}; }

// Matriz de un segmento: origen en `cabeza`, eje Z hacia la punta (el original construye la matriz con dos vectores,
// FUN_0013c690/FUN_0013c0c0, y deja la punta en la columna de traslacion).
ps2::M34 MatrizSegmento(const ps2::V3& cabeza, const ps2::V3& cola, const ps2::V3& arriba) {
  const ps2::V3 f = V3Normal(V3Resta(cola, cabeza));
  ps2::V3 r = V3Cruza(arriba, f);
  if (V3Largo(r) < 1e-5f) r = V3Cruza(ps2::V3{1, 0, 0}, f);
  r = V3Normal(r);
  const ps2::V3 u = V3Cruza(f, r);
  ps2::M34 m;
  m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z; m.m[3] = cabeza.x;
  m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z; m.m[7] = cabeza.y;
  m.m[8] = f.x; m.m[9] = f.y; m.m[10] = f.z; m.m[11] = cabeza.z;
  return m;
}

// Esqueleto minimo para resolver el mundo de un hueso por su cadena de padres (Skel_ApplyChainForBone).
struct HuesoRig {
  int padre = -1;
  ps2::M34 local;          // hueso local (reposo/pose)
};
struct EsqueletoRig {
  std::vector<HuesoRig> huesos;
  std::vector<ps2::M34> mundo;
  bool calculado = false;
  void Calcular() {
    mundo.assign(huesos.size(), Identidad());
    for (size_t i = 0; i < huesos.size(); ++i) {
      const int p = huesos[i].padre;
      mundo[i] = (p >= 0 && size_t(p) < i) ? mundo[size_t(p)] * huesos[i].local : huesos[i].local;
    }
    calculado = true;
  }
  ps2::M34 Mundo(int h) {
    if (!calculado || mundo.size() != huesos.size()) Calcular();
    return (h >= 0 && size_t(h) < mundo.size()) ? mundo[size_t(h)] : Identidad();
  }
};

struct EsferaFisica {
  ps2::V3 centro;
  float radio = 0.0f;
};
struct SegmentoCadena {
  int hueso = -1;
  ps2::M34 actual, anterior;   // matrices del segmento (0x00 y 0x40 del original)
  ps2::V3 cabeza, cola, velocidad, ancla;
  float largo = 1.0f, largo_min = 0.0f, largo_max = 1.0f, flex = 0.5f;
};
struct ParametrosCadena {
  float gravedad = 0.0f;       // +0x5C: el original aplica gravedad * -0.027222225
  float limite = 0.0f;         // +0x50: magnitud del desplazamiento permitido
  ps2::V3 minimo{0, 0, 0};     // +0x30 (limite inferior por eje)
  ps2::V3 maximo{0, 0, 0};     // +0x40 (limite superior por eje)
  uint32_t flags = 0;          // +0x18 (0x10 = usar la raiz, 0x20 = sin limites, 1/2/4 = ejes fijos)
};
struct CadenaFisica {
  int esqueleto = 0;                       // indice del esqueleto del modelo
  std::vector<SegmentoCadena> segmentos;   // +0x1E numero de segmentos
  ParametrosCadena params;
  std::vector<EsferaFisica> esferas;       // +0x7C / +0x78 (esferas de colision de la cadena)
  bool usa_raiz = false;                   // bit 0x10 de params.flags
};

struct ParticulaTela {
  ps2::V3 p, v, n;
  float u = 0, vv = 0;
  bool fija = false;
  float radio = 0.0f;
};
struct MuelleTela { int a = 0, b = 0; float largo_max = 0.0f; };
struct TelaFisica {
  int ancho = 0, alto = 0;
  std::vector<ParticulaTela> p;
  std::vector<MuelleTela> muelles;
  std::vector<EsferaFisica> esferas;
  ps2::V3 gravedad{0, 0, 0};   // +0x70 (direccion * fuerza)
  float suelo = -1e30f;        // +0x74
  int pasos = 1;               // DAT_70000068 (iteraciones del paso)
  int esqueleto = -1;          // modelo al que va cosida (opcional)
  std::vector<int> huesos;     // hueso de cada ancla
  std::vector<ps2::V3> anclas; // +0x0C de cada punto (matriz del hueso al que va cosido)
};
struct GrupoFisico {
  std::vector<CadenaFisica> cadenas;   // +0x00, numero en +0x2C
  std::vector<TelaFisica> telas;       // +0x28, numero en +0x2E
  std::vector<EsqueletoRig> esqueletos;
  bool calentar = false;               // byte +0x41 (BoneChain_SetFlag41)
};

void Cloth_CollideWithSpheres(TelaFisica& t);
void Cloth_IntegrateParticles(TelaFisica& t);
void Cloth_ConstrainSpringLengths(TelaFisica& t);
void Cloth_ComputeVertexNormals(TelaFisica& t);
void Cloth_SimulateStep(TelaFisica& t);

// EU 0x262F30 Skel_ApplyChainForBone: devuelve la matriz mundo del hueso aplicando su cadena de padres.
ps2::M34 Skel_ApplyChainForBone(EsqueletoRig& esq, int hueso) { return esq.Mundo(hueso); }

// Escritura de la matriz del hueso (lo que el original hace al final de BoneChain_BuildSegmentMatrix: deja la
// matriz del segmento en el hueso del modelo).
void EscribirHueso(EsqueletoRig& esq, int hueso, const ps2::M34& m) {
  if (hueso >= 0 && size_t(hueso) < esq.huesos.size()) {
    if (esq.mundo.size() != esq.huesos.size()) esq.Calcular();
    esq.mundo[size_t(hueso)] = m;
  }
}

// EU 0x261870 BoneChain_ResetPose: cero velocidades y cada segmento a la pose actual del hueso (matriz + matriz de
// reposo). El original copia la matriz del hueso (l.278120-278190) y limpia los acumuladores.
void BoneChain_ResetPose(CadenaFisica& c, EsqueletoRig& esq) {
  for (SegmentoCadena& s : c.segmentos) {
    const ps2::M34 m = Skel_ApplyChainForBone(esq, s.hueso);
    s.actual = s.anterior = m;
    s.cabeza = Traslacion(m);
    s.cola = V3Suma(s.cabeza, V3Escala(ps2::V3{m.m[8], m.m[9], m.m[10]}, s.largo));
    s.velocidad = {0, 0, 0};
    s.ancla = s.cabeza;
  }
}

// EU 0x2608D0 BoneChain_CollideWithSpheres: empuja la punta fuera de las esferas y reconstruye la matriz.
void BoneChain_CollideWithSpheres(CadenaFisica& c, SegmentoCadena& s) {
  for (const EsferaFisica& e : c.esferas) {
    const ps2::V3 d = V3Resta(s.cola, e.centro);
    const float dist = V3Largo(d);
    if (dist < e.radio && dist > 1e-5f) s.cola = V3Suma(e.centro, V3Escala(d, e.radio / dist));
  }
}

// EU 0x260AF0 BoneChain_ApplyGravityOffset: gravedad e inercia del segmento; el original usa la direccion del hueso
// padre y la escala de dibujo (params+0x50).
void BoneChain_ApplyGravityOffset(const CadenaFisica& c, const ps2::V3& padre_dir, ps2::V3& v) {
  if (c.params.limite <= 0.0f) return;
  v = V3Suma(v, V3Escala(padre_dir, c.params.gravedad * -0.027222225f));
}

// EU 0x260F60 BoneChain_UpdateSegment: simula un segmento. `copiar` = primer pase del original (copia la pose del
// hueso si la matriz cambio). Respeta los limites por eje (params+0x30/+0x40) y el factor de flex (params+0x54).
void BoneChain_UpdateSegment(GrupoFisico& g, CadenaFisica& c, SegmentoCadena& s, bool copiar,
                             const ps2::V3* cabeza = nullptr) {
  if (copiar) {
    s.anterior = s.actual;
    s.anterior.m[3] = s.cabeza.x; s.anterior.m[7] = s.cabeza.y; s.anterior.m[11] = s.cabeza.z;
  }
  EsqueletoRig& esq = g.esqueletos[size_t(c.esqueleto)];
  const ps2::M34 hueso = Skel_ApplyChainForBone(esq, s.hueso);
  s.cabeza = cabeza ? *cabeza : Traslacion(hueso);
  ps2::V3 arriba{hueso.m[4], hueso.m[5], hueso.m[6]};
  ps2::V3 dir = V3Resta(s.cola, s.cabeza);
  if (V3Largo(dir) < 1e-5f) dir = ps2::V3{hueso.m[8], hueso.m[9], hueso.m[10]};
  ps2::V3 destino = V3Suma(s.cola, V3Escala(dir, s.flex));
  BoneChain_ApplyGravityOffset(c, V3Normal(dir), destino);
  // Limites del hueso (el original los aplica eje a eje solo si el flag correspondiente esta encendido).
  if (!(c.params.flags & 0x20)) {
    if (c.params.flags & 1) destino.x = std::clamp(destino.x, s.cabeza.x + c.params.minimo.x, s.cabeza.x + c.params.maximo.x);
    if (c.params.flags & 2) destino.y = std::clamp(destino.y, s.cabeza.y + c.params.minimo.y, s.cabeza.y + c.params.maximo.y);
    if (c.params.flags & 4) destino.z = std::clamp(destino.z, s.cabeza.z + c.params.minimo.z, s.cabeza.z + c.params.maximo.z);
  }
  float l = V3Largo(V3Resta(destino, s.cabeza));
  const float maxl = s.largo_max > 0.0f ? s.largo_max : s.largo;
  const float minl = s.largo_min > 0.0f ? s.largo_min : 0.0f;
  l = std::clamp(l, minl, maxl);
  s.cola = V3Suma(s.cabeza, V3Escala(V3Normal(V3Resta(destino, s.cabeza)), l));
  BoneChain_CollideWithSpheres(c, s);
  s.actual = MatrizSegmento(s.cabeza, s.cola, arriba);
  EscribirHueso(esq, s.hueso, s.actual);
  s.velocidad = V3Resta(s.cola, s.ancla);
  s.ancla = s.cola;
}

// EU 0x261480 BoneChain_UpdateRoot: ancla cada segmento al hueso y actualiza desde la raiz (solo el primer
// segmento recibe el empuje; los demas siguen la cadena). El original guarda los globales de la cadena y llama a
// BoneChain_UpdateSegment (l.277810-277897).
void BoneChain_UpdateRoot(GrupoFisico& g, CadenaFisica& c) {
  if (c.esqueleto < 0 || size_t(c.esqueleto) >= g.esqueletos.size()) return;
  EsqueletoRig& esq = g.esqueletos[size_t(c.esqueleto)];
  for (SegmentoCadena& s : c.segmentos) s.ancla = Traslacion(Skel_ApplyChainForBone(esq, s.hueso));
  if (c.segmentos.empty()) return;
  BoneChain_UpdateSegment(g, c, c.segmentos.front(), false);
  for (size_t i = 1; i < c.segmentos.size(); ++i) {
    const ps2::V3 cabeza = c.segmentos[i - 1].cola;
    BoneChain_UpdateSegment(g, c, c.segmentos[i], false, &cabeza);
  }
}

// EU 0x2615D0 BoneChain_UpdateAllSegments: recalcula el ancla de todos los segmentos desde sus huesos y despues
// recorre la cadena (cada segmento cuelga del anterior). l.277903-278012.
void BoneChain_UpdateAllSegments(GrupoFisico& g, CadenaFisica& c) {
  if (c.esqueleto < 0 || size_t(c.esqueleto) >= g.esqueletos.size()) return;
  EsqueletoRig& esq = g.esqueletos[size_t(c.esqueleto)];
  for (SegmentoCadena& s : c.segmentos) {
    s.cabeza = Traslacion(Skel_ApplyChainForBone(esq, s.hueso));
    s.ancla = s.cabeza;
  }
  if (c.segmentos.empty()) return;
  BoneChain_UpdateSegment(g, c, c.segmentos.front(), false);
  for (size_t i = 1; i < c.segmentos.size(); ++i) {
    const ps2::V3 cabeza = c.segmentos[i - 1].cola;
    BoneChain_UpdateSegment(g, c, c.segmentos[i], false, &cabeza);
  }
}

// EU 0x261770 BoneChain_Update: si la escala es 0 o el juego esta pausado (o `copiar`), copia la pose actual al
// modelo; si el flag 0x10 esta puesto actualiza solo la raiz; si no, toda la cadena. l.278014-278080.
void BoneChain_Update(GrupoFisico& g, CadenaFisica& c, bool copiar) {
  if (c.segmentos.empty()) return;
  if (copiar) {
    if (c.esqueleto >= 0 && size_t(c.esqueleto) < g.esqueletos.size())
      for (SegmentoCadena& s : c.segmentos) EscribirHueso(g.esqueletos[size_t(c.esqueleto)], s.hueso, s.actual);
    return;
  }
  if (c.params.flags & 0x10) BoneChain_UpdateRoot(g, c);
  else BoneChain_UpdateAllSegments(g, c);
  for (SegmentoCadena& s : c.segmentos) s.anterior = s.actual;
}

// EU 0x260020 Cloth_UpdateObject: sincroniza las anclas de la tela con sus huesos, copia luz/escala y da el paso de
// simulacion; despues escribe las dos matrices de ancla (l.276641-276752).
void Cloth_UpdateObject(GrupoFisico& g, TelaFisica& t) {
  if (t.p.empty()) return;
  if (!t.anclas.empty() && t.esqueleto >= 0 && size_t(t.esqueleto) < g.esqueletos.size()) {
    EsqueletoRig& esq = g.esqueletos[size_t(t.esqueleto)];
    for (size_t i = 0; i < t.anclas.size() && i < t.p.size(); ++i) {
      const int hueso = i < t.huesos.size() ? t.huesos[i] : -1;
      t.p[i].p = V3Suma(Traslacion(esq.Mundo(hueso)), t.anclas[i]);
      t.p[i].fija = true;
      t.p[i].v = {0, 0, 0};
    }
  }
  Cloth_SimulateStep(t);
}

// EU 0x265700 Cloth_SimulateStep: `pasos` veces (DAT_70000068) colision -> integracion -> muelles; despues normales.
// La scratchpad del original (0x70000000) solo es el area de trabajo del VU: aqui son vectores del host.
void Cloth_SimulateStep(TelaFisica& t) {
  const int pasos = std::max(1, t.pasos);
  for (int k = 0; k < pasos; ++k) {
    Cloth_CollideWithSpheres(t);
    Cloth_IntegrateParticles(t);
    Cloth_ConstrainSpringLengths(t);
  }
  Cloth_ComputeVertexNormals(t);
}

// EU 0x264F50 Cloth_ComputeVertexNormals: normal por vertice como suma de los productos vectoriales de las celdas.
void Cloth_ComputeVertexNormals(TelaFisica& t) {
  for (ParticulaTela& p : t.p) p.n = {0, 0, 0};
  for (int y = 0; y + 1 < t.alto; ++y)
    for (int x = 0; x + 1 < t.ancho; ++x) {
      const int i0 = y * t.ancho + x, i1 = i0 + 1, i2 = i0 + t.ancho, i3 = i2 + 1;
      const ps2::V3 a = V3Resta(t.p[size_t(i1)].p, t.p[size_t(i0)].p);
      const ps2::V3 b = V3Resta(t.p[size_t(i2)].p, t.p[size_t(i0)].p);
      const ps2::V3 n = V3Cruza(a, b);
      for (int i : {i0, i1, i2, i3}) t.p[size_t(i)].n = V3Suma(t.p[size_t(i)].n, n);
    }
  for (ParticulaTela& p : t.p) p.n = V3Normal(p.n);
}

// EU 0x265130 Cloth_ConstrainSpringLengths: si dos puntos se separan mas que el largo del muelle, el segundo se
// acerca (el primero queda fijo, como el original: solo mueve pauVar6).
void Cloth_ConstrainSpringLengths(TelaFisica& t) {
  for (const MuelleTela& m : t.muelles) {
    if (m.a < 0 || m.b < 0 || size_t(m.a) >= t.p.size() || size_t(m.b) >= t.p.size()) continue;
    ParticulaTela& a = t.p[size_t(m.a)];
    ParticulaTela& b = t.p[size_t(m.b)];
    const ps2::V3 d = V3Resta(b.p, a.p);
    const float dist = V3Largo(d);
    if (dist > m.largo_max && dist > 1e-5f) b.p = V3Suma(a.p, V3Escala(d, m.largo_max / dist));
  }
}

// EU 0x265230 Cloth_IntegrateParticles: v += gravedad; p += v; el original limpia la velocidad tras integrar y
// sujeta al suelo si el flag de dibujo 4 no esta puesto.
void Cloth_IntegrateParticles(TelaFisica& t) {
  const float dt = 1.0f / 60.0f;
  for (ParticulaTela& p : t.p) {
    if (p.fija) continue;
    p.v = V3Suma(p.v, V3Escala(t.gravedad, dt));
    p.p = V3Suma(p.p, V3Escala(p.v, dt));
    p.v = {0, 0, 0};   // el original limpia la velocidad tras integrar (l.281390)
    if (p.p.y < t.suelo) p.p.y = t.suelo;
  }
}

// EU 0x265320 Cloth_CollideWithSpheres: si la particula entra en una esfera la empuja a la superficie y anula su
// velocidad.
void Cloth_CollideWithSpheres(TelaFisica& t) {
  for (const EsferaFisica& e : t.esferas) {
    for (ParticulaTela& p : t.p) {
      if (p.fija) continue;
      const ps2::V3 d = V3Resta(p.p, e.centro);
      const float dist = V3Largo(d);
      if (dist < e.radio && dist > 1e-5f) {
        p.p = V3Suma(e.centro, V3Escala(d, e.radio / dist));
        p.v = {0, 0, 0};
      }
    }
  }
}

// EU 0x261FA0 PhysGroup_ResetPose: resetea la pose de todo el grupo (l.278544-278571). Recorre las cadenas
// (numero en el byte +0x2C del grupo, tabla en +0x00) llamando a BoneChain_ResetPose con el esqueleto de cada
// una, y despues las telas (numero en el byte +0x2E, tabla en +0x28) con Stub_Empty_260380, que en el original
// es un `return` sin cuerpo (0x260380): la tela no guarda pose que resetear. La llama el guion de escena
// (Cutscene_GotoLine 0x3424F0) al empezar la linea y el calentamiento del grupo (0x261BF0, que en el original
// lleva estos mismos dos bucles escritos dentro). Aqui la usan los dos: BoneChain_WarmUpGroup la llama en vez de
// repetir el codigo. El llamante del guion sigue [H]: el port no reproduce las lineas de cutscene de IW.
void PhysGroup_ResetPose(GrupoFisico& g) {
  for (CadenaFisica& c : g.cadenas)
    if (c.esqueleto >= 0 && size_t(c.esqueleto) < g.esqueletos.size())
      BoneChain_ResetPose(c, g.esqueletos[size_t(c.esqueleto)]);
  // Segundo bucle del original (+0x2E telas, l.278557-278568): llama a Stub_Empty_260380 (0x260380), que es un
  // `return` sin cuerpo. No hay nada que resetear por tela, asi que no se recorre.
}

// EU 0x261BF0 BoneChain_WarmUpGroup: escala de dibujo x3, reset de todas las cadenas, 16 pasos de update + tela y
// vuelta a la escala original. l.278327-278414.
void BoneChain_WarmUpGroup(GrupoFisico& g) {
  PhysGroup_ResetPose(g);   // 0x261FA0: el original escribe aqui los mismos dos bucles de reset
  for (CadenaFisica& c : g.cadenas) BoneChain_Update(g, c, false);
  for (TelaFisica& t : g.telas) Cloth_UpdateObject(g, t);
  for (int i = 0; i < 16; ++i) {
    for (CadenaFisica& c : g.cadenas) BoneChain_Update(g, c, true);
    for (TelaFisica& t : g.telas) Cloth_UpdateObject(g, t);
  }
}

// EU 0x261DB0 PhysGroup_Update: si el flag de recien enlazado (byte +0x41) esta a 0, actualiza cadenas y telas; si
// esta a 1, lo limpia y hace el calentamiento del grupo. l.278416-278460.
void PhysGroup_Update(GrupoFisico& g) {
  if (g.calentar) {
    g.calentar = false;
    BoneChain_WarmUpGroup(g);
    return;
  }
  for (CadenaFisica& c : g.cadenas) BoneChain_Update(g, c, false);
  for (TelaFisica& t : g.telas) Cloth_UpdateObject(g, t);
}

}  // namespace

}  // namespace dbz3::escenarios

// Lo llama el gancho de dibujo del escenario (clima.cpp, sub_820C2058): pisa la camara con la del cuadro de la escena.
void dbz3::escenarios::AplicarCamaraEscena(uint8_t* base) {
  if (g_esc.activa) {   // red de seguridad: si la pelea acabo sin que la comprobacion lo viera, el marcador y los mandos vuelven
    const uint32_t m = REX_LOAD_U32(kMgrPtr);
    if (!m || REX_LOAD_U32(m + kEstadoCombate) != 4) {
      TerminarEscena("cortada (dibujo)");
      return;
    }
  }
  if (!g_esc_cam.load()) return;
  g_esc_dibujos.fetch_add(1);   // al acabar este dibujo
  dbz3::cine::Camara(base, g_esc_ojo, g_esc_obj, g_esc_giro);
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (mgr && g_esc_fov > 0.05f) {
    uint32_t u;
    std::memcpy(&u, &g_esc_fov, 4);
    REX_STORE_U32(mgr + 0x1C4, u);
  }
}

bool dbz3::escenarios::EscenaDeZona() { return g_esc.activa; }

void dbz3::escenarios::ComprobarZonaB(PPCContext& ctx, uint8_t* base) {
  PasoCapas(base);
  TickEscena(ctx, base);
  if (!REXCVAR_GET(dbz3_escenarios_zonas_b)) return;
  const uint32_t mgr = REX_LOAD_U32(dbz3::escenarios::kMgrPtr);
  if (!mgr || !REX_LOAD_U8(mgr + dbz3::escenarios::kAvisoRing)) return;
  // DS131: el piloto automatico de las pruebas gana el combate con el aviso de ring out (DBZ3_TEST_RINGOUT,
  // tagteam.cpp). En las arenas de B2 con zona B ese aviso es el mismo que dispara el cambio de zona: si se consume
  // (PasarZonaB) el combate sigue y el lado del jugador, que no tiene mando, acaba KO. Con la prueba activa se deja
  // el aviso al motor para que cierre el combate por ring out (como antes de existir el cambio de zona). Solo cambia
  // el comportamiento con la variable de pruebas puesta; el juego normal hace el cambio de zona de B2.
  if (std::getenv("DBZ3_TEST_RINGOUT")) return;
  // Contrato: solo se cambia con el alta de la zona A cargada (`g_activa`), su `zona_b` existente y el
  // combate en estado 4 (pelea). Sin esto el aviso de ring out sigue su curso normal y acaba el combate.
  if (REX_LOAD_U32(mgr + dbz3::escenarios::kEstadoCombate) == 4 && dbz3::escenarios::PasarZonaB(ctx, base)) {
    REX_STORE_U8(mgr + dbz3::escenarios::kAvisoRing, 0);   // el aviso se consume: el combate sigue
    return;
  }
  static std::atomic<int> avisos{0};
  if (avisos.fetch_add(1) < 4)
    REXLOG_INFO("dbz3 escenarios: zona B no aplicada (alta {}, estado {}, zona_b {}; el ring out sigue normal)",
                dbz3::escenarios::g_activa, REX_LOAD_U32(mgr + dbz3::escenarios::kEstadoCombate),
                dbz3::escenarios::ZonaBDe(dbz3::escenarios::g_activa));
}
