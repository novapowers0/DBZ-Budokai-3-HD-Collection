// SPDX-License-Identifier: MIT
// Tanda3 (8/11): minijuegos de campo MSNMW de IW (0x7D0 Raditz, 0x7D2, 0x7D9, 0x7DF: mismo codigo) con su logica ORIGINAL:
// s_SPACE 0x31F280 (EU) por campo PAL: recogida de aros/monedas/bebidas/objetos D, bola oculta (+0x16A4), contadores
// (+0x165A total, +0x165C cogidos), meta (+0x16BC) y el reloj del campo, sobre G = *(0x49CDE8). El init original
// LoadWaitDragonMissionGroundMinigameItems 0x323840 llena G desde el #ITM del campo (A/B/C/D por dificultad).
// Un solo #include al final de iw_mips.cpp (dentro: cvar, declaraciones y puente); iw_minijuegos.cpp lo incluye con
// IWC_SOLO_DECL para ver solo las declaraciones. Base aprobada ON (dbz3_iw_7d0_original), con DirC para EU y USA.
// Dorado: tools/iw_mips/cap_campo.py (PCSX2 PINE, parejas antes/despues de s_SPACE con el jugador puesto sobre los
// puntos del #ITM) + tools/iw_mips/campo.py / campo_espejo.py (reejecucion en ee.py: 74/74 parejas iguales en G+0x1620..0x1700
// y los 0x9000 B del gestor, con recogidas hasta 40 y la bola oculta 0->1->0->2; el espejo desde cero tambien 74/74).
#ifndef IWC_DECL_HECHA
#define IWC_DECL_HECHA
#include <cstdint>
namespace dbz3::iwmips {
namespace campo {
// Offsets dentro de Salida::g (= G+0x1620..0x1700): G+0x165A total (s16), +0x165C cogidos (s16), +0x165E.. aros A (1 B/objeto:
// 0 cerca, 1 lejos de la camara, 2 cogido), +0x1670.. B, +0x1692.. C, +0x16A8.. D, +0x16A4 bola oculta (2 = cogida), +0x16A6 slot.
constexpr int kBase = 0x1620;
struct Entrada {
  bool inicio = false;
  int indice = 0, dif = 2;               // indice del MSNMW (ctx+4: 0 Raditz, 2 Serpiente, 10 nave, 14 Piccolo, 16 Radar) y dificultad
  const uint8_t* itm = nullptr;          // #ITM crudo del campo (cabecera "#ITM" + puntos de 16 B desde +0x20)
  uint32_t itm_n = 0;
  uint32_t mascara = 0;                  // bolas ya conseguidas (global 0x5C1D78); el port no la tiene: 0
  int slot = 0;                          // sitio de la bola oculta (0..7): lo elige el script del original; aqui el port
  float anc[3]{};                        // ancla del jugador (+0x640 del actor: raiz + cadera), ejes de IW
  float jug[3]{};                        // raiz del jugador (+0x40)
  float ex = 0, ez = 0;                  // periodos de la rejilla del campo (0 = sin rejilla)
  uint64_t t = 0;
  // IW-F03: tareas del ELF (TCB 0x140020, Task_Create 0x140300, Task_Kill 0x140460, planificador 0x140200): el init crea
  // la tarea de s_SPACE y el cuadro recorre la lista; la tarea del objetivo 0x299000 (0x299900) corre y muere como en PS2.
  bool planificador = false;
};
struct Salida { uint8_t g[0xE0]{}; };
inline int S16(const Salida& s, int off) { return int(int16_t(uint16_t(s.g[off - kBase] | (s.g[off - kBase + 1] << 8)))); }
inline int Total(const Salida& s) { return S16(s, 0x165A); }
inline int Cogidos(const Salida& s) { return S16(s, 0x165C); }
inline int Flag(const Salida& s, int off) { return s.g[off - kBase]; }
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;     // cvar dbz3_iw_7d0_original y dbz3_iw_logica_original, ELF EU/USA
}  // namespace campo
// IW-F08 (12/11): 0x7D1 Capturar a Bubbles, el flujo del minijuego con la logica ORIGINAL (ajuste dbz3_iw_7d1_original,
// apagado). Un Paso = un campo PAL, en el orden de prioridades de las tareas del ELF (ver Paso7D1; banco
// E:/DBZ3HD_trabajo/iw-f08/banco_7d1.py, 0 DIF con el dorado y 119/119 reglas): GroundMinigameTimer 0x2F3BE0 -> personajes
// (Minigame_ProcessPlayerInput 0x29A1D0 y el agarre Minigame_State1C/1D_Update 0x29EF40/0x29EC00) -> tareas (START 0x299000,
// cut-in) -> Battle_TriggerProgressCutins 0x2A63B0. Solo EU (SLES_553.47).
namespace campo7d1 {
struct Entrada {
  bool inicio = false;          // primer campo: tareas (TCB) y LoadWaitMinigameLink 0x298760 (limite mgr+0x7CC)
  bool start = false;           // fin de la entrada (Battle_CharState2dUpdate 317775): 0x299900(jugador, 0) = cartel START
  bool pausa = false;           // menu de pausa (ctx+0x8C bit 0 -> mgr+0x8B7C)
  int dif = 2;
  int estado = 1;               // pose de IW del jugador (+0x14BC); 0x1C/0x1D = agarre en curso
  float vida = 100, energia = 100;   // jugador +0x1850 (Minigame_TriggerObjective) y +0x185C (Char_GetNextActionCode)
  uint16_t pad_mantenido = 0, pad_pulsado = 0;   // mando en disposicion PS2 (+0x4ADECC / +0x4ADECE): triangulo 0x1000
  float y = 0;                  // raiz del jugador +0x44 (el agarre exige <= 2)
  bool aire = false;            // +0x2061
  float cuadro = 0;             // fotograma del clip del agarre (+0xC68: 200 en 0x1C, 0xC9 en 0x1D)
  bool fin = false;             // clip acabado (+0xC74)
  float mano[2][3]{};           // +0x720/+0x730: manos del jugador (B3: puntos de cuerpo 10 LHAND y 11 RHAND)
  float bub[3]{};               // ancla de Bubbles (+0x28D0+0x640)
  uint64_t t = 0;
};
struct Salida {
  float restante = 0;           // mgr+0x7CC - mgr+0x8B9C (1/60 s)
  bool start_vivo = false;      // tarea objetivo del cartel START viva (objs+0x180, +0x6C = 0)
  bool fallo = false;           // Minigame_TriggerObjective 0x2A6260: tiempo o vida (pose 0x30, objetivo 2)
  bool exito = false;           // agarre: Battle_CharState2fInit 0x29B120 (pose 0x2F)
  int accion = 0;               // 0x1C/0x1D pedido por Minigame_ReadPlayerButtons (objs+0x21C/+0x220)
  int pose = -1;                // pose pedida por 0x2AAF60 (9 tras el fallo)
  int cartel = -1, voz = -1;    // aviso: UI_SprPlaceByIndex 0x3389C0 y la voz de s_PLY_CUTIN (PlayHit 0x22FD20)
  bool bub_pose_d = false;      // Char_EnterPoseStateByIndex(Bubbles, 0xD) al atraparlo
};
inline bool (*g_paso)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;     // cvar dbz3_iw_7d1_original + dbz3_iw_logica_original + ELF EU
}  // namespace campo7d1
}  // namespace dbz3::iwmips
#endif

#ifndef IWC_SOLO_DECL
#ifndef IWC_IMPL_HECHA
#define IWC_IMPL_HECHA

REXCVAR_DEFINE_BOOL(dbz3_iw_7d0_original, true, "DBZ3/IW",
                    "IW MSNMW (0x7D0/7D2/7D9/7DF): recogida, bola oculta, contadores y meta con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_campo_aislado, false, "DBZ3/IW",
                    "Prueba: imagen EE propia para s_SPACE, sin pisar la IA de Bubbles o Piccolo; no certifica el minijuego completo");
REXCVAR_DEFINE_BOOL(dbz3_iw_7d1_original, false, "DBZ3/IW",
                    "IW 0x7D1 Capturar a Bubbles: reloj, cartel START, avisos, agarre y fallo con la logica original del ELF EU (en pruebas)");

namespace dbz3::iwmips::bubbles {
namespace {
constexpr Dueno kD7D1 = Dueno(10);   // el enum (0..9) solo admite hasta 15: 11 mapa, 12 campo/vuelo, 13-15 botones
constexpr Dueno kDCampo = Dueno(12);   // 13-15 los usa iw_mips_botones.inl
// Espejo propio: G, gestor (jugador en +0x800), objetos, camara, rejilla, contexto del minijuego y #ITM.
constexpr uint32_t kGC = 0x1180000, kMgrC = 0x1182000, kObjsC = 0x118C000, kCamC = 0x118D000, kGridC = 0x118E000,
                   kCtxPC = 0x118F000, kItmC = 0x1190000;
// Punteros globales (EU; USA en DirC) que s_SPACE y sus ayudantes leen (mgr, G, contexto, camara, objetos, rejilla, contexto del minijuego).
constexpr uint32_t kPMgrC = 0x49CA70, kPGC = 0x49CDE8, kPCtxC = 0x49CBA4, kPCamC = 0x49CBBC, kPObjsC = 0x49CBC0, kPGridC = 0x49CDB8,
                   kPCtxPC = 0x49CDC0, kSpaceC = 0x31F280, kInitC = 0x323840;
// Funciones sin efecto en la logica: efectos, UvAnim, sonido, UI, scripts, carga y SO (por nombre del decompilado; 458 direcciones).
constexpr uint32_t kMudosC[] = {
    0x105E70, 0x106070, 0x11DD40, 0x11DEF8, 0x12CC10, 0x1352D0, 0x135320, 0x135370, 0x1353D0, 0x135420,
    0x13F7A0, 0x140300, 0x141390, 0x142440, 0x1BAA90, 0x1BAB60, 0x1BAD80, 0x1BAE30, 0x1BB110, 0x1BB120,
    0x1D2F70, 0x1D3010, 0x1D3370, 0x1D5580, 0x1D5610, 0x1D5680, 0x1D56D0, 0x1D57E0, 0x1D58F0, 0x1D5930,
    0x1D5A10, 0x1D5C00, 0x1D60A0, 0x1D81D0, 0x1D85F0, 0x1DBF60, 0x1DC0A0, 0x1DC3C0, 0x1DCD00, 0x1E1280,
    0x1E13E0, 0x1E9910, 0x1E9AC0, 0x1E9D50, 0x1EA1A0, 0x1EA300, 0x1EA490, 0x1EA5F0, 0x1EAD20, 0x1EB1C0,
    0x1EBC90, 0x1EBEC0, 0x1EBF00, 0x1EBF20, 0x1EBFF0, 0x1EC2C0, 0x1EC320, 0x1EC330, 0x1EC5C0, 0x1EC670,
    0x1EC740, 0x1EC7C0, 0x1EC8B0, 0x1ECAC0, 0x1ECBA0, 0x1ECC70, 0x1ECD60, 0x1ECDF0, 0x1ECE80, 0x1ECF00,
    0x1ED050, 0x1ED130, 0x1ED1B0, 0x1ED230, 0x1ED350, 0x1ED3C0, 0x1ED4E0, 0x1ED510, 0x1ED5A0, 0x1ED5F0,
    0x1ED640, 0x1ED690, 0x1ED6E0, 0x1ED730, 0x1ED760, 0x1ED790, 0x1ED7C0, 0x1ED7F0, 0x1ED850, 0x1ED8C0,
    0x1ED900, 0x1ED930, 0x1ED960, 0x1ED990, 0x1ED9C0, 0x1ED9F0, 0x1EDA20, 0x1EDAF0, 0x1EDBE0, 0x1EDC20,
    0x1EDC50, 0x1EDDC0, 0x1EDEB0, 0x1EE700, 0x1EF100, 0x1EFA10, 0x1EFB00, 0x1EFBB0, 0x1EFD50, 0x1EFED0,
    0x1F0030, 0x1F0100, 0x1F02D0, 0x1F0490, 0x1F0540, 0x1F06A0, 0x1F0790, 0x1F08B0, 0x1F0A60, 0x1F0BD0,
    0x1F0C20, 0x1F0C60, 0x1F0D00, 0x1F0FB0, 0x1F1200, 0x1F15A0, 0x1F1910, 0x1F1CA0, 0x1F22F0, 0x1F2830,
    0x1F2BE0, 0x1F2E90, 0x1F30F0, 0x1F3370, 0x1F35C0, 0x1F3740, 0x1F3790, 0x1F3800, 0x1F38E0, 0x1F39F0,
    0x1F3AB0, 0x1F3AE0, 0x1F3C80, 0x1F4000, 0x1F4090, 0x1F41A0, 0x1F41E0, 0x1F4220, 0x1F4490, 0x1F4540,
    0x1F4570, 0x1F60D0, 0x1F61B0, 0x1F65B0, 0x1F6750, 0x1F6870, 0x1F6B60, 0x1F7490, 0x1F7530, 0x1F7650,
    0x1F7B20, 0x1F7E40, 0x1F8000, 0x1F80A0, 0x1F81D0, 0x1F8440, 0x1F8820, 0x1F9010, 0x1F90B0, 0x1F9290,
    0x1F9840, 0x1F9860, 0x1F98F0, 0x1F9A70, 0x1F9AD0, 0x1F9B40, 0x1F9BA0, 0x1F9D40, 0x1F9DE0, 0x1FA110,
    0x1FA200, 0x1FA330, 0x1FA480, 0x1FA570, 0x1FA6D0, 0x1FA7D0, 0x1FA9A0, 0x1FAB50, 0x1FACE0, 0x1FAF60,
    0x1FAFF0, 0x1FB040, 0x1FB320, 0x1FB370, 0x1FB490, 0x1FB5B0, 0x1FB720, 0x1FB770, 0x1FB800, 0x1FB870,
    0x1FB8C0, 0x1FB940, 0x1FB9D0, 0x1FBA20, 0x1FBA80, 0x1FBC00, 0x1FBE00, 0x1FC010, 0x1FC120, 0x1FC1E0,
    0x1FC290, 0x1FC2D0, 0x1FC2F0, 0x1FC4B0, 0x1FC630, 0x1FC660, 0x1FC7C0, 0x1FD030, 0x1FD2A0, 0x1FD410,
    0x1FD7D0, 0x1FDA10, 0x22EBB0, 0x22FC50, 0x2366D0, 0x239320, 0x26F340, 0x26F9C0, 0x26FB90, 0x26FF60,
    0x270400, 0x270780, 0x2709B0, 0x271060, 0x271210, 0x2712F0, 0x271440, 0x271510, 0x2715D0, 0x2716E0,
    0x2719A0, 0x271C90, 0x272070, 0x2725F0, 0x272A30, 0x272FA0, 0x273530, 0x2739D0, 0x273C10, 0x274200,
    0x274670, 0x274990, 0x274C70, 0x275080, 0x275350, 0x275920, 0x275A00, 0x275F60, 0x276050, 0x2764C0,
    0x276570, 0x276A70, 0x276B30, 0x276BB0, 0x276C30, 0x276D30, 0x277150, 0x277270, 0x277430, 0x277620,
    0x277630, 0x277710, 0x277B40, 0x277C50, 0x277D10, 0x277DC0, 0x277E90, 0x278240, 0x278340, 0x278480,
    0x278540, 0x278620, 0x278670, 0x2786E0, 0x278730, 0x278800, 0x2788E0, 0x278920, 0x278970, 0x278A70,
    0x278A80, 0x278AD0, 0x278B30, 0x279840, 0x2798A0, 0x279910, 0x279980, 0x279A60, 0x279B60, 0x279B80,
    0x279BC0, 0x279C30, 0x279E50, 0x279FD0, 0x282710, 0x282CB0, 0x283000, 0x2830B0, 0x283130, 0x283160,
    0x2832F0, 0x283670, 0x2836C0, 0x2838A0, 0x2839D0, 0x283DA0, 0x296340, 0x296350, 0x299850, 0x2ACF20,
    0x2D9190, 0x2D92E0, 0x2E7CF0, 0x2E9170, 0x2E9280, 0x2EBC00, 0x2F06A0, 0x2F0940, 0x2F0960, 0x2F0A30,
    0x2F0AC0, 0x2F0B50, 0x2F0E10, 0x2F0E30, 0x2F0F60, 0x2F0F90, 0x2F1000, 0x2F1050, 0x2F10A0, 0x2F10F0,
    0x2F1140, 0x2F66A0, 0x2F67E0, 0x2F6920, 0x2F6A60, 0x2F6BA0, 0x2F6CF0, 0x2F6E90, 0x2F78A0, 0x2F7900,
    0x2F7950, 0x2F79A0, 0x2F7E00, 0x301330, 0x301440, 0x301580, 0x3016C0, 0x301810, 0x3019A0, 0x301AD0,
    0x301B30, 0x301B90, 0x301BF0, 0x301C50, 0x301CB0, 0x301D10, 0x301D60, 0x301DC0, 0x301E20, 0x301E80,
    0x301ED0, 0x301F20, 0x301F70, 0x302030, 0x302150, 0x302210, 0x3022D0, 0x302390, 0x302440, 0x302490,
    0x302570, 0x3025C0, 0x302690, 0x3027F0, 0x302800, 0x302810, 0x3028C0, 0x3029B0, 0x302BB0, 0x302CB0,
    0x303030, 0x30C680, 0x30C6C0, 0x30C810, 0x30CF20, 0x31B310, 0x31B3C0, 0x31F030, 0x31F0C0, 0x31F150,
    0x31F1E0, 0x320520, 0x320600, 0x3206C0, 0x320800, 0x320990, 0x320B20, 0x320C80, 0x3234C0, 0x323830,
    0x323FD0, 0x324220, 0x338970, 0x3389D0, 0x338B40, 0x342090, 0x343A20, 0x347340, 0x347470, 0x347560,
    0x3476B0, 0x3477E0, 0x347960, 0x347D60, 0x347DB0, 0x347DF0, 0x347EC0, 0x348020, 0x348030, 0x348040,
    0x348110, 0x348190, 0x3481E0, 0x35EA50, 0x35EAA0, 0x35EB60, 0x363F10, 0x3640E0, 0x364240, 0x3643B0,
    0x364510, 0x3645D0, 0x364630, 0x364640, 0x364660, 0x364680, 0x364830, 0x364870, 0x364880, 0x3648D0,
    0x364930, 0x3649D0, 0x364A40, 0x372570, 0x3725E0, 0x372620, 0x372660, 0x372700,
};

// USA (SLUS_218.42): las mismas funciones emparejadas instruccion a instruccion (tools/iw_mips/campo_espejo.py USA=1: 74/74).
constexpr uint32_t kMudosU[] = {
    0x105E70, 0x106070, 0x11DD40, 0x11DEF8, 0x12CC10, 0x1352D0, 0x135320, 0x135370, 0x1353D0, 0x135420,
    0x13F7A0, 0x140300, 0x141390, 0x142440, 0x1BAA90, 0x1BAB60, 0x1BAD80, 0x1BAE30, 0x1BB110, 0x1BB120,
    0x1D2F70, 0x1D3010, 0x1D3370, 0x1D5580, 0x1D5610, 0x1D5680, 0x1D56D0, 0x1D57E0, 0x1D58F0, 0x1D5930,
    0x1D5A10, 0x1D5C00, 0x1D60A0, 0x1D81D0, 0x1D85F0, 0x1DBF60, 0x1DC0A0, 0x1DC3C0, 0x1DCD00, 0x1E1280,
    0x1E13E0, 0x1E9910, 0x1E9AC0, 0x1E9D50, 0x1EA1A0, 0x1EA300, 0x1EA490, 0x1EA5F0, 0x1EAD20, 0x1EB1C0,
    0x1EBC90, 0x1EBEC0, 0x1EBF00, 0x1EBF20, 0x1EBFF0, 0x1EC2C0, 0x1EC320, 0x1EC330, 0x1EC5C0, 0x1EC670,
    0x1EC740, 0x1EC7C0, 0x1EC8B0, 0x1ECAC0, 0x1ECBA0, 0x1ECC70, 0x1ECD60, 0x1ECDF0, 0x1ECE80, 0x1ECF00,
    0x1ED050, 0x1ED130, 0x1ED1B0, 0x1ED230, 0x1ED350, 0x1ED3C0, 0x1ED4E0, 0x1ED510, 0x1ED5A0, 0x1ED5F0,
    0x1ED640, 0x1ED690, 0x1ED6E0, 0x1ED730, 0x1ED760, 0x1ED790, 0x1ED7C0, 0x1ED7F0, 0x1ED850, 0x1ED8C0,
    0x1ED900, 0x1ED930, 0x1ED960, 0x1ED990, 0x1ED9C0, 0x1ED9F0, 0x1EDA20, 0x1EDAF0, 0x1EDBE0, 0x1EDC20,
    0x1EDC50, 0x1EDDC0, 0x1EDEB0, 0x1EE700, 0x1EF100, 0x1EFA10, 0x1EFB00, 0x1EFBB0, 0x1EFD50, 0x1EFED0,
    0x1F0030, 0x1F0100, 0x1F02D0, 0x1F0490, 0x1F0540, 0x1F06A0, 0x1F0790, 0x1F08B0, 0x1F0A60, 0x1F0BD0,
    0x1F0C20, 0x1F0C60, 0x1F0D00, 0x1F0FB0, 0x1F1200, 0x1F15A0, 0x1F1910, 0x1F1CA0, 0x1F22F0, 0x1F2830,
    0x1F2BE0, 0x1F2E90, 0x1F30F0, 0x1F3370, 0x1F35C0, 0x1F3740, 0x1F3790, 0x1F3800, 0x1F38E0, 0x1F39F0,
    0x1F3AB0, 0x1F3AE0, 0x1F3C80, 0x1F4000, 0x1F4090, 0x1F41A0, 0x1F41E0, 0x1F4220, 0x1F4490, 0x1F4540,
    0x1F4570, 0x1F60D0, 0x1F61B0, 0x1F65B0, 0x1F6750, 0x1F6870, 0x1F6B60, 0x1F7490, 0x1F7530, 0x1F7650,
    0x1F7B20, 0x1F7E40, 0x1F8000, 0x1F80A0, 0x1F81D0, 0x1F8440, 0x1F8820, 0x1F9010, 0x1F90B0, 0x1F9290,
    0x1F9840, 0x1F9860, 0x1F98F0, 0x1F9A70, 0x1F9AD0, 0x1F9B40, 0x1F9BA0, 0x1F9D40, 0x1F9DE0, 0x1FA110,
    0x1FA200, 0x1FA330, 0x1FA480, 0x1FA570, 0x1FA6D0, 0x1FA7D0, 0x1FA9A0, 0x1FAB50, 0x1FACE0, 0x1FAF60,
    0x1FAFF0, 0x1FB040, 0x1FB320, 0x1FB370, 0x1FB490, 0x1FB5B0, 0x1FB720, 0x1FB770, 0x1FB800, 0x1FB870,
    0x1FB8C0, 0x1FB940, 0x1FB9D0, 0x1FBA20, 0x1FBA80, 0x1FBC00, 0x1FBE00, 0x1FC010, 0x1FC120, 0x1FC1E0,
    0x1FC290, 0x1FC2D0, 0x1FC2F0, 0x1FC4A0, 0x1FC620, 0x1FC650, 0x1FC7B0, 0x1FD020, 0x1FD290, 0x1FD400,
    0x1FD7C0, 0x1FDA00, 0x22EAF0, 0x22FAE0, 0x236540, 0x239190, 0x26F130, 0x26F7B0, 0x26F980, 0x26FD50,
    0x2701F0, 0x270570, 0x2707A0, 0x270E50, 0x271000, 0x2710E0, 0x271230, 0x271300, 0x2713C0, 0x2714D0,
    0x271790, 0x271A80, 0x271E60, 0x2723E0, 0x272820, 0x272D90, 0x273320, 0x2737C0, 0x273A00, 0x273FF0,
    0x274460, 0x274780, 0x274A60, 0x274E70, 0x275140, 0x275710, 0x2757F0, 0x275D50, 0x275E40, 0x2762B0,
    0x276360, 0x276860, 0x276920, 0x2769A0, 0x276A20, 0x276B20, 0x276F40, 0x277060, 0x277220, 0x277410,
    0x277420, 0x277500, 0x277930, 0x277A40, 0x277B00, 0x277BB0, 0x277C80, 0x278030, 0x278130, 0x278270,
    0x278330, 0x278410, 0x278460, 0x2784D0, 0x278520, 0x2785F0, 0x2786D0, 0x278710, 0x278760, 0x278860,
    0x278870, 0x2788C0, 0x278920, 0x279630, 0x279690, 0x279700, 0x279770, 0x279850, 0x279950, 0x279970,
    0x2799B0, 0x279A20, 0x279C40, 0x279DC0, 0x282500, 0x282AA0, 0x282DF0, 0x282EA0, 0x282F20, 0x282F50,
    0x2830E0, 0x283460, 0x2834B0, 0x283690, 0x2837C0, 0x283B90, 0x2960B0, 0x2960C0, 0x2995C0, 0x2ACA30,
    0x2D9030, 0x2D9180, 0x2E7AE0, 0x2E8F60, 0x2E9070, 0x2EB9F0, 0x2F0490, 0x2F0730, 0x2F0750, 0x2F0820,
    0x2F08B0, 0x2F0940, 0x2F0C00, 0x2F0C20, 0x2F0D50, 0x2F0D80, 0x2F0DF0, 0x2F0E40, 0x2F0E90, 0x2F0EE0,
    0x2F0F30, 0x2F6490, 0x2F65D0, 0x2F6710, 0x2F6850, 0x2F6990, 0x2F6AE0, 0x2F6C80, 0x2F7690, 0x2F76F0,
    0x2F7740, 0x2F7790, 0x2F7BF0, 0x301200, 0x301340, 0x301480, 0x3015D0, 0x301760, 0x301890, 0x3018F0,
    0x301950, 0x3019B0, 0x301A10, 0x301A70, 0x301AD0, 0x301B20, 0x301B80, 0x301BE0, 0x301C40, 0x301C90,
    0x301CE0, 0x301D30, 0x301DF0, 0x301F10, 0x301FD0, 0x302090, 0x302150, 0x302200, 0x302250, 0x302330,
    0x302380, 0x302450, 0x3025B0, 0x3025C0, 0x3025D0, 0x302680, 0x302770, 0x302990, 0x302A90, 0x302E10,
    0x30C410, 0x30C450, 0x30C5A0, 0x30CCB0, 0x31B020, 0x31B0D0, 0x31ED40, 0x31EDD0, 0x31EE60, 0x31EEF0,
    0x320230, 0x320310, 0x3203D0, 0x320510, 0x3206A0, 0x320830, 0x320990, 0x3231D0, 0x323540, 0x323CE0,
    0x323F30, 0x338490, 0x3384F0, 0x338660, 0x341B90, 0x343520, 0x346E40, 0x346F70, 0x347060, 0x3471B0,
    0x3472E0, 0x347460, 0x347860, 0x3478B0, 0x3478F0, 0x3479C0, 0x347B20, 0x347B30, 0x347B40, 0x347C10,
    0x347C90, 0x347CE0, 0x35EB90, 0x35EBE0, 0x35ECA0, 0x364050, 0x364220, 0x364380, 0x3644F0, 0x364650,
    0x364710, 0x364770, 0x364780, 0x3647A0, 0x3647C0, 0x364970, 0x3649B0, 0x3649C0, 0x364A10, 0x364A70,
    0x364B10, 0x364B80, 0x3726A0, 0x372710, 0x372750, 0x372790, 0x372830,
};
struct DirC {
  uint32_t space, init, pmgr, pg, pctx, pcam, pobjs, pgrid, pctxp, ctx, mascara, paso2;
  const uint32_t* mudos; size_t n_mudos;
  uint32_t lista, dibujo;   // IW-F03: cabeza de la lista de tareas (centinela = +0x18) y tarea de dibujo del init (0x323560)
};
constexpr DirC kEuC = {0x31F280, 0x323840, 0x49CA70, 0x49CDE8, 0x49CBA4, 0x49CBBC, 0x49CBC0, 0x49CDB8, 0x49CDC0, 0x5C0DC0, 0x5C1D78, 0x49C7E0,
                       kMudosC, sizeof kMudosC / 4, 0x4AD558, 0x31F270};
constexpr DirC kUsaC = {0x31EF90, 0x323550, 0x49C270, 0x49C5C8, 0x49C394, 0x49C3AC, 0x49C3B0, 0x49C598, 0x49C5A0, 0x5C05C0, 0x5C1578, 0x49BFE0,
                        kMudosU, sizeof kMudosU / 4, 0x4ACD58, 0x31EF80};
// IW-F03: sistema de tareas del ELF (mismas direcciones en EU y USA; solo cambian sus globales). Banco sin juego:
// E:/DBZ3HD_trabajo/iw-f03/serpiente_banco.py (900/900 con 0x140200 del ELF y con este mismo recorrido).
constexpr uint32_t kTcbC = 0x140020, kCrearC = 0x140300, kMatarC = 0x140460, kLiberarC = 0x140B50, kAllocC = 0x11CF40,
                   kMemsetC = 0x11DEF8, kNombreC = 0x121030, kArenaC = 0x1192000, kArenaFinC = 0x119C000;
uint32_t g_arena_c = kArenaC;
// FUN_00140200 (EU 48684-48713): llama +0x1C(tarea) de cada tarea viva en orden de prioridad y despues libera las
// muertas (+0x1C = -1) con 0x140B50. Sin sus estadisticas de REG_RCNT0 (fuera de la RAM del interprete) y sin la tarea
// de dibujo (la pinta el port).
bool PlanificarCampo(Ee& e, const DirC& d) {
  const uint32_t fin = d.lista + 0x18;
  int vueltas = 0;
  for (uint32_t t = e.R32(d.lista); t != fin; t = e.R32(t + 0x28)) {
    if (++vueltas > 0x100) return false;
    const uint32_t cb = e.R32(t + 0x1C);
    if (int32_t(cb) > 0 && cb != d.dibujo && !e.Llamar(cb, {t})) return false;
  }
  for (uint32_t t = e.R32(d.lista); t != fin; t = e.R32(t + 0x28)) {
    if (++vueltas > 0x200) return false;
    if (e.R32(t + 0x1C) == 0xFFFFFFFFu && !e.Llamar(kLiberarC, {t})) return false;
  }
  return true;
}
bool EsUsaC() { return kGp == 0x4A35F0; }

bool ActivaCampo() { return REXCVAR_GET(dbz3_iw_7d0_original) && Listo() != nullptr; }

Estado* EspejoCampo() {
  // No copiar Ee: sus puentes capturan el Estado que los creo. Una imagen verificada nueva conserva sus propios
  // globales y su dueno entre llamadas; el campo y la IA del NPC se ejecutan en la MISMA partida, no solo al cambiarla.
  static Estado* const st = Iniciar();
  return st;
}

bool CampoMsn(const campo::Entrada& in, campo::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7d0_original) || in.itm == nullptr || in.itm_n < 0x650) return false;
  Estado* const compartido = Listo();
  if (!compartido) return false;
  Estado* const st = REXCVAR_GET(dbz3_iw_campo_aislado) ? EspejoCampo() : compartido;
  if (!st) return false;
  const DirC& d = EsUsaC() ? kUsaC : kEuC;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = campo::Salida{};
  const Dueno antes = st->dueno;
  Ambito amb(*st, kDCampo);
  if (!in.inicio && !amb.continua) {   // G vive en el invitado entre campos
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips campo: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  // Iniciar() deja puentes permanentes de Bubbles sobre estos accesores (su ctx/objetos/trabajo): aqui van los del campo.
  amb.Puente(kCtxF, [&d](Ee& c) { V0(c, d.ctx); });
  amb.Puente(kObjsA, [](Ee& c) { V0(c, kObjsC); });
  amb.Puente(kObjsB, [](Ee& c) { V0(c, kObjsC); });
  amb.Puente(kGetWork, [](Ee& c) { V0(c, kCamC); });
  for (size_t q = 0; q < d.n_mudos; ++q) amb.Puente(d.mudos[q], [](Ee& c) { V0(c, 0); });
  const bool plan = in.planificador;
  if (plan) {
    // Tareas de verdad: fuera los puentes de Task_Create/Kill (mudo del campo y los de Bubbles). Sustituidos solo el
    // reservador (bloque propio), la copia del nombre (MMI) y memset fuera del bloque (sigue mudo como en el 74/74).
    amb.Quitar(kCrearC); amb.Quitar(kMatarC); amb.Quitar(kLiberarC); amb.Quitar(kTcbC);
    amb.Puente(kAllocC, [](Ee& c) {
      const uint32_t al = std::max<uint32_t>(uint32_t(c.r[4]), 1), n = uint32_t(c.r[5]);
      const uint32_t p = (g_arena_c + al - 1) & ~(al - 1);
      g_arena_c = p + n;
      V0(c, g_arena_c <= kArenaFinC ? p : 0);
    });
    amb.Puente(kMemsetC, [](Ee& c) {
      const uint32_t a = uint32_t(c.r[4]), n = uint32_t(c.r[6]);
      const uint8_t v = uint8_t(c.r[5]);
      if (a < kArenaC || a >= kArenaFinC || n > kArenaFinC - a) { V0(c, 0); return; }
      for (uint32_t i = 0; i < n; ++i) c.W8(a + i, v);
      V0(c, a);
    });
    amb.Puente(kNombreC, [](Ee& c) {
      const uint32_t dst = uint32_t(c.r[4]), src = uint32_t(c.r[5]), n = uint32_t(c.r[6]);
      for (uint32_t i = 0; i < n; ++i) {
        const uint8_t b = c.R8(src + i);
        c.W8(dst + i, b);
        if (!b) break;
      }
      V0(c, dst);
    });
  }
  e.W32(kSemilla, semilla);
  const Cronometro cr(e);
  bool ok = true;
  if (in.inicio) {
    // PM-18: otro minijuego del mismo espejo (0x7D1: Bubbles y su campo) deja listas de sprites que s_SPACE libera
    // despues (Sprite_DestroyTree EU 0x1B8BD0 salia de la RAM al coger el 4.o aro de 0x7D9 en la serie). Cada campo
    // empieza de los datos limpios del ELF; si el espejo lo dejo 0x7D1 original en esta misma partida, se respeta.
    if (antes != kD7D1 && !st->limpia.empty()) e.Bytes(kTexto1, st->limpia.data(), st->limpia.size());
    e.Cero(kGC, 0x2000); e.Cero(kMgrC, 0xA000); e.Cero(kObjsC, 0x1000); e.Cero(kCamC, 0x1000); e.Cero(kGridC, 0x1000);
    e.Cero(kCtxPC, 0x1000); e.Cero(kItmC, 0x2000);
    const uint32_t n = std::min<uint32_t>(in.itm_n, 0x2000);
    for (uint32_t i = 0; i < n; i += 4) {   // #ITM crudo al invitado (palabras; el resto del ultimo bloque queda a cero)
      uint32_t w = 0;
      std::memcpy(&w, in.itm + i, std::min<uint32_t>(4, n - i));
      e.W32(kItmC + i, w);
    }
    e.W32(d.pmgr, kMgrC); e.W32(d.pg, kGC); e.W32(d.pctx, d.ctx); e.W32(d.pcam, kCamC); e.W32(d.pobjs, kObjsC);
    e.W32(d.pgrid, kGridC); e.W32(d.pctxp, kCtxPC);
    e.W32(d.ctx + 4, uint32_t(in.indice)); e.W16(d.ctx + 0xC, uint16_t(in.dif));
    e.W8(d.mascara, uint8_t(in.mascara));
    e.W32(kGC + 0x148, kItmC);
    e.W8(kGC + 0x16A6, uint8_t(in.slot & 7));   // lo pone el script MMI2 del original (no se ejecuta): el slot lo elige el port
    e.Wf(kMgrC + 0x8B80, kPasoCampo);
    if (plan) {   // main EU 53996: TCB_InternalThreadStructure(0x100, 0x40); el init crea despues las tareas del campo
      e.Cero(kArenaC, kArenaFinC - kArenaC);
      g_arena_c = kArenaC;
      ok = e.Llamar(kTcbC, {0x100, 0x40}, 1000000);
    }
    if (ok) ok = e.Llamar(d.init, {});
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips campo: {} al preparar; se usa el port", e.error());
      st->dueno = kNadie;
      return false;
    }
  }
  // Rejilla del campo (Node_WrapPositionInGrid): periodo del suelo de IW (sin rejilla: periodo enorme = sin giro). Medida en
  // el savestate del 0x7D0: 3000 x 3000 en 10 x 10 desde -1350.
  const float px = in.ex > 0 ? in.ex : 1e9f, pz = in.ez > 0 ? in.ez : 1e9f;
  e.W32(kGridC + 0x630, 7); e.Wf(kGridC + 0xAD4, px); e.Wf(kGridC + 0xAD8, pz);
  e.W32(kGridC + 0xADC, 10); e.W32(kGridC + 0xAE0, 10); e.Wf(kGridC + 0xAF0, -0.45f * px); e.Wf(kGridC + 0xAF8, -0.45f * pz);
  const uint32_t jug = kMgrC + 0x800;
  for (uint32_t i = 0; i < 3; ++i) {
    e.Wf(jug + 0x640 + 4 * i, in.anc[i]); e.Wf(jug + 0x40 + 4 * i, in.jug[i]);
    for (uint32_t o : {0x80u, 0x90u, 0xC0u}) e.Wf(kCamC + o + 4 * i, in.anc[i]);   // camara = el jugador (la recogida no mira la camara)
  }
  e.Wf(jug + 0x64C, 1.f); e.Wf(jug + 0x4C, 1.f);
  for (uint32_t o : {0x8Cu, 0x9Cu, 0xCCu}) e.Wf(kCamC + o, 1.f);
  e.W16(jug + 0xCBC, 1);                       // estado de pose del jugador (+0x14BC): 1 = en pie
  e.Wf(kMgrC + 0x8B80, kPasoCampo);
  e.W32(kMgrC + 0x8B7C, 0);
  e.Wf(kPasoT, kPasoCampo); e.Wf(d.paso2, kPasoCampo);
  ok = plan ? PlanificarCampo(e, d) : e.Llamar(d.space, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips campo: {} (t={}); el campo sigue con el port", e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  // la semilla del port no se toca: los efectos mudos del original gastan numeros al azar que aqui no se hacen
  for (uint32_t i = 0; i < sizeof out.g; ++i) out.g[i] = e.R8(kGC + campo::kBase + i);
  if (plan && (log || in.inicio))   // PAQ_1: planificador del 0x7D2 medido contra el oraculo
    REXLOG_INFO("dbz3 iwmips campo plan: arena={:X}..{:X} tareas={} idx={}", kArenaC, g_arena_c, e.R32(d.ctx + 0x8), in.indice);
  st->Cuenta(us);
  if (log && in.inicio && st != compartido) {
    // Constata el ajuste y la llamada real a s_SPACE; NO es un oraculo de minijuego completo.
    REXLOG_INFO("dbz3 iwmips campo aislamiento=propio idx={} dif={} completo=0", in.indice, in.dif);
  }
  if (log) {
    REXLOG_INFO("dbz3 iwmips campo t={} ini={} idx={} tot={} cog={} oculto={} slot={} instr={} us={:.1f}", in.t, int(in.inicio),
                in.indice, campo::Total(out), campo::Cogidos(out), campo::Flag(out, 0x16A4), campo::Flag(out, 0x16A6), e.n - cr.n0, us);
  }
  return true;
}

// ---------------- IW-F08: 0x7D1 Capturar a Bubbles (EU) ----------------
// Decompilado EU: LoadWaitMinigameLink 315586 (indice 1: limite = 0x49C7E4 * 60 * 4 = 12000 con 50 cuadros/s),
// GroundMinigameTimer 380599 (+0x8B9C += +0x8B80 con +0x8D21 = 0, topado al limite), 0x299900 316288 / 0x299000 315946
// (START: 140,0, al morir +0x8D21 = 0; fallo: 150,0 y pose 9 al primer paso), Battle_TriggerProgressCutins 325438 (avisos
// del 1: carteles 0x16/0x17/0x18, voces 0/1/2) -> Minigame_TriggerObjective 325386 (restante <= 0 o vida <= 0 -> 0x30 y
// objetivo 2), Minigame_ReadPlayerButtons 316201 (triangulo en el suelo: pose < 3 -> +0x220 = 0x1D, pose 3 -> +0x21C = 0x1C),
// Minigame_State1C/1D_Update 320207/320064 (cuadros 11..24 / 11..20: mano a < 6 del ancla de Bubbles -> +0x1A8 = 0x2F) y
// Battle_CharState2fInit 317414 (Bubbles a la pose 0xD, cartel 7). Banco: E:/DBZ3HD_trabajo/iw-f08/banco_7d1.py.
constexpr uint32_t kMgr71 = 0x1220000, kObjs71 = 0x122A000, kMgw71 = 0x122B000, kCam71 = 0x122C000, kCtxP71 = 0x122D000,
                   kArena71 = 0x1230000, kArenaFin71 = 0x1240000;
// r23: TCB(0x100, 0x40) pide 0x100 bloques de 0x90 (0x9000) + anillo 0x400 = 0x9400. Con 0x8000 el reservador daba 0, la
// reserva y el anillo quedaban en la direccion 0 pisandose (anillo[11] = 0x0D020A0C) y el Task_Create 12 (el primer aviso,
// s_PLY_CUTIN) escribia fuera de la RAM: 0x14039C, 7D1 volvia al port y su captura por distancia daba "superado".
constexpr uint32_t kJug71 = kMgr71 + 0x800, kBub71 = kMgr71 + 0x28D0;
// Valen las del EU; MapaUsa71() las pasa a SLUS_218.42 (r23: emparejadas por ventana enmascarada unica y cuerpos sin
// diferencias de campos, E:/DBZ3HD_trabajo/iw_eu_usa_tabla.py; sin probar en juego con el ELF USA).
uint32_t kLink71 = 0x298760, kObjetivo71 = 0x299900, kReloj71 = 0x2F3BE0, kAvisos71 = 0x2A63B0,
         kAgarre1C = 0x29EF40, kAgarre1D = 0x29EC00, kPose71 = 0x2AAF60, kCartel71 = 0x3389C0, kEntraPose71 = 0x31B280,
         kPad71 = 0x4ADECC, kPadIdx71 = 0x5C14B4, kFps71 = 0x49C7E4, kEntrada71 = 0x29A1D0, kDistancia71 = 0x142440,
         kStick71 = 0x299250, kCamStick71 = 0x2977F0;
// LoadWaitMinigameLink: sus subsistemas (escena, camara, HUD, auras, sonido, objetos del campo, mensajes) no deciden el
// reloj; mudos solo durante esa llamada (jal medidos en el ELF EU: 0x298760..0x2989D0 salvo los dos accesores).
uint32_t kLinkSub71[] = {0x296B80, 0x29A450, 0x2982E0, 0x2E9210, 0x2F79D0, 0x302620, 0x30FC40, 0x1FD9D0, 0x323840, 0x338AA0};
// Sin efecto en la logica de 7D1: camara, animacion (la lleva el port), sonido, raiz/huesos, liberar recursos, cadena de
// huesos, voz del START. Se anotan aparte el cartel, la voz del aviso, la pose pedida y la de Bubbles.
uint32_t kMudos71[] = {0x2972D0, 0x297300, 0x238640, 0x240740, 0x2AAA60, 0x261BD0, 0x2AAB70};
void MapaUsa71() {
  static bool hecho = false;
  if (hecho) return;
  hecho = true;
  kLink71 = 0x2984D0, kObjetivo71 = 0x299670, kReloj71 = 0x2F39D0, kAvisos71 = 0x2A5F30;
  kAgarre1C = 0x29EC40, kAgarre1D = 0x29E900, kPose71 = 0x2AAA70, kCartel71 = 0x3384E0, kEntraPose71 = 0x31AF90;
  kPad71 = 0x4AD6CC, kPadIdx71 = 0x5C0CB4, kFps71 = 0x49BFE4, kEntrada71 = 0x299F40;   // kDistancia71 igual
  kStick71 = 0x298FC0, kCamStick71 = 0x297560;
  const uint32_t sub[] = {0x2968F0, 0x29A1C0, 0x298050, 0x2E9000, 0x2F77C0, 0x3023E0, 0x30F9D0, 0x1FD9C0, 0x323550, 0x3385C0};
  const uint32_t mud[] = {0x297040, 0x297070, 0x2384B0, 0x2405B0, 0x2AA590, 0x2619C0, 0x2AA6A0};
  std::copy(std::begin(sub), std::end(sub), kLinkSub71);
  std::copy(std::begin(mud), std::end(mud), kMudos71);
}
uint32_t g_arena_71 = kArena71;
bool g_listo_71 = false, g_exito_71 = false, g_fallo_71 = false;

// Una linea de registro cada vez que cambia la respuesta (la primera, al entrar en 7D1): la prueba dice sola por que no entra.
bool Activa7D1() {
  const char* motivo = !REXCVAR_GET(dbz3_iw_7d1_original) ? "ajuste dbz3_iw_7d1_original apagado"
                       : !REXCVAR_GET(dbz3_iw_logica_original) ? "ajuste dbz3_iw_logica_original apagado"
                       : Listo() == nullptr ? "interprete o ELF de IW no cargado (ver 'logica original de Bubbles')"
                                            : nullptr;
  static const char* ultimo = "";
  if (motivo != ultimo) {
    ultimo = motivo;
    if (motivo) REXLOG_INFO("dbz3 iwmips 7d1 original: no activo porque {}; 7D1 sigue con el port", motivo);
    else REXLOG_INFO("dbz3 iwmips 7d1 original: activo (ELF {})", EsUsaC() ? "USA" : "EU");
  }
  return motivo == nullptr;
}

bool Paso7D1(const campo7d1::Entrada& in, campo7d1::Salida& out, uint32_t& semilla) {
  out = campo7d1::Salida{};
  if (!REXCVAR_GET(dbz3_iw_7d1_original)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  if (EsUsaC()) MapaUsa71();
  if (!in.inicio && !g_listo_71) return false;
  const DirC& d = EsUsaC() ? kUsaC : kEuC;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  Ambito amb(*st, kD7D1);
  // Puentes de Bubbles que miran st->in/out (nulos aqui) y accesores del minijuego -> espejo propio.
  amb.Puente(kCtxF, [&d](Ee& c) { V0(c, d.ctx); });
  amb.Puente(kObjsA, [](Ee& c) { V0(c, kObjs71); });
  amb.Puente(kObjsB, [](Ee& c) { V0(c, kObjs71); });
  amb.Puente(kGetWork, [](Ee& c) { V0(c, kMgw71); });
  for (size_t q = 0; q < d.n_mudos; ++q) amb.Puente(d.mudos[q], [](Ee& c) { V0(c, 0); });
  for (uint32_t a : kMudos71) amb.Puente(a, [](Ee& c) { V0(c, 0); });
  // CharacterOutlines 0x142440 (distancia mano-ancla del agarre, VU0): muda en kMudosC deja f0 viejo y el agarre nunca
  // atrapa (banco 77/89 -> 119/119). Aqui corre de verdad. Stick y camara de la entrada no deciden el agarre: mudos.
  amb.Quitar(kDistancia71);
  for (uint32_t a : {kStick71, kCamStick71}) amb.Puente(a, [](Ee& c) { V0(c, 0); });
  amb.Puente(kBones, [](Ee& c) { V0(c, 0); });
  amb.Puente(kRaiz, [](Ee& c) { V0(c, 0); });
  amb.Puente(kVoz, [&out](Ee& c) { out.voz = int(c.r[5] & 0xFFFF); V0(c, 0); });
  amb.Puente(kCartel71, [&out](Ee& c) { out.cartel = int(c.r[4] & 0xFFFF); V0(c, 0); });
  amb.Puente(kPose71, [&out](Ee& c) { out.pose = int(c.r[5] & 0xFFFF); V0(c, 0); });
  amb.Puente(kEntraPose71, [&out](Ee& c) {
    if (uint32_t(c.r[4]) == kBub71 && (c.r[5] & 0xFFFF) == 0xD) out.bub_pose_d = true;
    V0(c, 0);
  });
  // Tareas de verdad (como IW-F03): Task_Create/Kill/liberar/TCB del ELF; reservador en un bloque propio.
  amb.Quitar(kCrearC); amb.Quitar(kMatarC); amb.Quitar(kLiberarC); amb.Quitar(kTcbC);
  amb.Puente(kAllocC, [](Ee& c) {
    const uint32_t al = std::max<uint32_t>(uint32_t(c.r[4]), 1), n = uint32_t(c.r[5]);
    const uint32_t p = (g_arena_71 + al - 1) & ~(al - 1);
    g_arena_71 = p + n;
    V0(c, g_arena_71 <= kArenaFin71 ? p : 0);
  });
  amb.Puente(kMemsetC, [](Ee& c) {
    const uint32_t a = uint32_t(c.r[4]), n = uint32_t(c.r[6]);
    if (a < kArena71 || a >= kArenaFin71 || n > kArenaFin71 - a) { V0(c, 0); return; }
    for (uint32_t i = 0; i < n; ++i) c.W8(a + i, uint8_t(c.r[5]));
    V0(c, a);
  });
  amb.Puente(kNombreC, [](Ee& c) {
    const uint32_t dst = uint32_t(c.r[4]), src = uint32_t(c.r[5]), n = uint32_t(c.r[6]);
    for (uint32_t i = 0; i < n; ++i) {
      const uint8_t b = c.R8(src + i);
      c.W8(dst + i, b);
      if (!b) break;
    }
    V0(c, dst);
  });
  // Punteros globales: los demas minijuegos del espejo los cambian entre campos.
  e.W32(d.pmgr, kMgr71); e.W32(d.pobjs, kObjs71); e.W32(d.pctx, d.ctx); e.W32(d.pcam, kCam71); e.W32(d.pctxp, kCtxP71);
  e.W32(d.ctx + 4, 1); e.W16(d.ctx + 0xC, uint16_t(in.dif)); e.W32(d.ctx + 0x8C, in.pausa ? 1u : 0u);
  e.Wf(kPasoT, kPasoCampo); e.Wf(d.paso2, kPasoCampo); e.W32(kFps71, EsUsaC() ? 60 : 50);   // .bss del ELF: 50 cuadros/s en PAL (savestate 7DD); NTSC 60
  e.W32(kSemilla, semilla);
  const Cronometro cr(e);
  bool ok = true;
  if (in.inicio) {
    e.Cero(kMgr71, 0xA000); e.Cero(kObjs71, 0x1000); e.Cero(kMgw71, 0x1000); e.Cero(kCam71, 0x1000); e.Cero(kCtxP71, 0x1000);
    e.Cero(kArena71, kArenaFin71 - kArena71);
    g_arena_71 = kArena71;
    g_listo_71 = g_exito_71 = g_fallo_71 = false;
    e.W32(kJug71 + 0x1380, 0); e.W32(kJug71 + 0x1384, kBub71); e.W32(kBub71 + 0x1380, 1); e.W32(kBub71 + 0x1384, kJug71);
    e.Wf(kMgr71 + 0x8B80, kPasoCampo);
    ok = e.Llamar(kTcbC, {0x100, 0x40}, 1000000);
    if (ok) {
      for (uint32_t a : kLinkSub71) amb.Puente(a, [](Ee& c) { V0(c, 0); });
      ok = e.Llamar(kLink71, {});   // los subsistemas quedan mudos hasta el final de esta llamada
    }
    e.W8(kMgr71 + 0x8D21, 1);   // Minigame_StartPlayerWork 316927: la entrada corre con el reloj parado
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips 7d1: {} al preparar; 7D1 sigue con el port", e.error());
      st->dueno = kNadie;
      return false;
    }
    g_listo_71 = true;
  }
  // Jugador: pose, vida/energia, altura y aire (lo que leen los avisos, el fallo y los botones).
  e.W16(kJug71 + 0xCBC, uint16_t(g_exito_71 ? 0x2F : in.estado));
  e.Wf(kJug71 + 0x1850, in.vida); e.Wf(kJug71 + 0x185C, in.energia);
  e.Wf(kJug71 + 0x44, in.y); e.W8(kJug71 + 0x2061, in.aire ? 1 : 0);
  // Orden por campo LEIDO del ELF: el planificador 0x140200 corre la lista de tareas por prioridad ascendente (Task_Create
  // 0x140300 inserta ordenado por a2). MinigamePlayerFunc 0x2F6380: GroundMinigameTimer prio 0x4100; MinigameStageIntros
  // 0x2AB000: Battle_UpdateAllCharacters 0x2AA7A0 prio 0x5100 (por luchador: Minigame_ProcessPlayerInput 0x29A1D0 y luego
  // el Update del estado, 1C/1D) y ExplorationMinigameBodyAnimationsMerge 0x2AA0A0 prio 0x5300 (acaba en los avisos
  // 0x2A63B0); la tarea del START (0x299900 -> 0x299000, 0x29997C) y el cut-in (0x2A5470) van a 0x5101.
  // => reloj, personajes (START nace aqui, en Battle_CharState2dUpdate), tareas, avisos.
  if (ok) ok = e.Llamar(kReloj71, {});
  if (ok && in.start) ok = e.Llamar(kObjetivo71, {kJug71, 0});
  if (ok && !g_exito_71) {
    e.W8(kPadIdx71, 0);
    e.W16(kPad71, in.pad_mantenido); e.W16(kPad71 + 2, in.pad_pulsado);
    for (uint32_t o : {0x21Cu, 0x220u}) e.W32(kObjs71 + o, 0);
    ok = e.Llamar(kEntrada71, {kJug71});   // sin botones con START (+0x180), fin (+0x248), pose 0x2C..0x31 o pausa
    out.accion = e.R32(kObjs71 + 0x21C) ? 0x1C : e.R32(kObjs71 + 0x220) ? 0x1D : 0;
    // las demas acciones (salto, scatto, carga...) las lleva el port: aqui solo se lee el agarre
    for (uint32_t o : {0x1B8u, 0x1BCu, 0x1FCu, 0x200u, 0x208u, 0x21Cu, 0x220u, 0x22Cu, 0x230u, 0x234u, 0x24Cu}) e.W32(kObjs71 + o, 0);
    if (ok && (in.estado == 0x1C || in.estado == 0x1D) && !in.fin) {
      e.Wf(kJug71 + 0xC68, in.cuadro); e.W32(kJug71 + 0xC74, 0);
      for (int i = 0; i < 3; ++i) {
        e.Wf(kJug71 + 0x720 + 4 * i, in.mano[0][i]); e.Wf(kJug71 + 0x730 + 4 * i, in.mano[1][i]);
        e.Wf(kBub71 + 0x640 + 4 * i, in.bub[i]);
      }
      e.Wf(kJug71 + 0x72C, 1.f); e.Wf(kJug71 + 0x73C, 1.f); e.Wf(kBub71 + 0x64C, 1.f);
      ok = e.Llamar(in.estado == 0x1C ? kAgarre1C : kAgarre1D, {kJug71});
      if (ok && e.R16(kJug71 + 0xCBC) == 0x2F) g_exito_71 = true;
    }
  }
  if (ok) ok = PlanificarCampo(e, d);
  if (ok) ok = e.Llamar(kAvisos71, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips 7d1: {} (t={}); 7D1 sigue con el port", e.error(), in.t);
    g_listo_71 = false;
    st->dueno = kNadie;
    return false;
  }
  semilla = e.R32(kSemilla);
  const uint32_t tarea = e.R32(kObjs71 + 0x180);
  if (e.R32(kObjs71 + 0x248) && e.R32(kObjs71 + 0x1A8) == 0x30) g_fallo_71 = true;
  out.restante = float(int32_t(e.R32(kMgr71 + 0x7CC))) - e.Rf(kMgr71 + 0x8B9C);
  out.start_vivo = tarea != 0 && e.R16(tarea + 0x6C) == 0;
  out.fallo = g_fallo_71;
  out.exito = g_exito_71;
  st->Cuenta(us);
  if (log && (in.inicio || in.start || out.accion || out.pose >= 0 || out.cartel >= 0 || out.voz >= 0 || out.bub_pose_d ||
              (out.exito && in.estado != 0x2F)))
    REXLOG_INFO("dbz3 iwmips 7d1 t={} ini={} start={} restante={:.1f} start_vivo={} fallo={} exito={} accion={:X} pose={} cartel={} "
                "voz={} bubD={} estado={:X} cuadro={:.1f} instr={} us={:.1f}", in.t, int(in.inicio), int(in.start), out.restante,
                int(out.start_vivo), int(out.fallo), int(out.exito), out.accion, out.pose, out.cartel, out.voz, int(out.bub_pose_d),
                in.estado, in.cuadro, e.n - cr.n0, us);
  return true;
}

const bool kRegistradoCampo = (campo::g_campo = &CampoMsn, campo::g_activa = &ActivaCampo, campo7d1::g_paso = &Paso7D1,
                               campo7d1::g_activa = &Activa7D1, true);
}  // namespace
}  // namespace dbz3::iwmips::bubbles
#endif
#endif
