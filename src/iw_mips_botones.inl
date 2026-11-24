// SPDX-License-Identifier: MIT
// T2B tanda3 (8/11): minijuegos de botones de IW con su logica ORIGINAL (familia de la plantilla CampoDA/CampoDC).
// Un solo #include al final de iw_mips.cpp (dentro: cvars, declaraciones y puentes); iw_mini_botones.cpp lo incluye con
// IWB_SOLO_DECL para ver solo las declaraciones. Cada minijuego, detras de su ajuste dbz3_iw_7dX_original (apagado de serie).
//   7DB (NB013, Vegeta-Trunks): Mg_UpdatePlayField 0x3568B0 + Mg_Update 0x3550A0 (EU). Dorado: E:/DBZ3HD_trabajo/T2B/qte7db.py.
#ifndef IWB_DECL_HECHA
#define IWB_DECL_HECHA
#include <cstdint>
namespace dbz3::iwmips {
// 7DB (NB013): dos carriles de iconos (10 huecos de UI por carril), marca de choque (objetos 0x2A/0x2B/0x2C) y choque de
// machacar boton. Por campo PAL: Mg_UpdatePlayField (tarea de cuadro) y Mg_Update (fases), en ese orden (medido en bench).
namespace nb013 {
struct Entrada {
  bool inicio = false;
  int dif = 2;
  int boton = -1;               // codigo de IW del boton pulsado este campo, -1 ninguno
  uint16_t flanco = 0;          // botones de IW pulsados este campo (0x4ADED2)
  uint16_t mantenido = 0;       // botones apretados (0x4ADECC)
  uint16_t palanca = 0;         // PUL-IW-FX-03: palanca DERECHA en digital (0x1000 arriba, 0x2000 der., 0x4000 abajo, 0x8000 izq.), pad +0x6C
  bool presenta_7db = false;    // distinguir la ranura de camara 1, clip 0
  bool cam_fin = false;         // Camera_IsTimerElapsed
  bool fin_pose[2] = {false, false};   // la animacion de la pose del jugador / rival acabo (la maquina de poses es del port)
  uint64_t t = 0;
};
struct Icono { int k = 0, boton = 0; float x = 0; bool vivo = false, fallado = false; };
// 'J' pose del jugador n, 'R' pose del rival n, 'C' camara (clip | 0x10000 si play 0), 'Y' s_MSN_CV_ReadyStart(n)
struct Evento { char tipo = 0; int n = 0; };
struct Salida {
  int fase = 0, sub = 0, paso = 0, activo = 0, rango = 0, choques = 0, n_ev = 0;
  float nota = 0, vida[2]{};
  int n_ic[2]{}, llegaron[2]{};              // llegaron = iconos ya resueltos por carril (+0x2C/+0x30)
  int golpes[2]{}, intentos[2]{};            // +0x3C/+0x40 y +0x44/+0x48: OFFENCE/DEFENCE = golpes / intentos (EU 0x371C..)
  Icono ic[2][10];
  float marca_x[3]{}, marca_y = 0;           // objetos UI 0x2A (borde izquierdo), 0x2B (derecho), 0x2C (medio)
  int marca_lado = 0, marca_indice = 0;      // ce94+0x80 / +0x7C: carril e indice de la celda que lleva la marca
  int choque_fase = 0, choque_lado = 0, pulsos = 0, objetivo = 0;
  int qte_boton = -1;   // Piloto: celda especial NB013; objeto UI 0x2F (+0x44) NB006
  Evento ev[24];   // + 'F' del CHRFADE del choque (antes 16)
};
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvar dbz3_iw_7db_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace nb013
// 7D4 (NB006): misma familia y mismo contexto (+0x40 en los punteros): tipos de nb013. Tareas: Mg_UpdateTick + Mg_UpdateFrame.
namespace nb006 {
using Entrada = nb013::Entrada;
using Salida = nb013::Salida;
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvar dbz3_iw_7d4_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace nb006

// 7D5 (NB007, Goku): "teclear" por rondas. Por campo PAL: MiniGame_UpdateInputCue 0x295140 (tarea de cuadro) y
// MiniGame_UpdateDispatch 0x295030 (fases): intro, fila quieta, tecleo en orden con limite de tiempo, aciertos/fallos, ronda, total y rango.
namespace nb007 {
struct Entrada {
  bool inicio = false;
  int dif = 2;
  int boton = -1;
  uint16_t flanco = 0, mantenido = 0;
  bool cam_fin = false;
  // Salidas de la maquina de poses del actor (MgPlyB, la lleva el port) que lee la logica: estado (+0xCBC), vueltas del
  // clip en bucle (+0xC78) y aviso de fin de animacion (+0xC74). pose < 0 = no se escribe.
  int pose = -1, vueltas = 0;
  bool anim_fin = false;
  uint64_t t = 0;
};
// 'P' estado de pose n (MgPlyB_ChangeState), 'C' camara (clip | 0x10000 si play 0), 'Y' s_MSN_CV_ReadyStart(n)
struct Evento { char tipo = 0; int n = 0; };
struct Salida {
  int fase = 0, sub = 0, paso = 0, ronda = -1, n = 0, cuenta = 0, aciertos = 0, rango = 0, n_ev = 0;
  float reloj = 0, tiempo = 0, total = 0;   // +8 reloj de la fase, +0x7C tiempo de la ronda, +0x44 total
  uint8_t seq[10]{};
  uint8_t w80 = 0;   // IW-F02: +0x80 (fallo en curso): MiniGame_UpdateSlotIcons 0x2930E0 ensena la cruz 0x15 con el y +4 == 1
  Evento ev[16];
};
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvar dbz3_iw_7d5_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace nb007
}  // namespace dbz3::iwmips
#endif

#ifndef IWB_SOLO_DECL
#ifndef IWB_IMPL_HECHA
#define IWB_IMPL_HECHA

REXCVAR_DEFINE_BOOL(dbz3_iw_7d5_original, false, "DBZ3/IW",
                    "IW 0x7D5 (NB007): rondas, tecleo, tiempo y rango con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_7d4_original, true, "DBZ3/IW",
                    "IW 0x7D4 (NB006): carriles, marca, remate, juicio, vida y rango con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_botones_ui_anim_d4, false, "DBZ3/IW",
                    "IW 0x7D4 (NB006): extender a 7D4 la correccion de animaciones de UI probada en 7DB (en pruebas)");
// IWFIN/IW-F14: UI_UpdateFrame EU 0x2F1190 iba mudo: sin UI_AdvanceAnimations (0x2F2F40) el icono acertado no soltaba sus
// bits 2/4 al acabar la animacion 0x0C (retorno Ui_ClearObjectFlags 0x351020) y la marca +0x1F8 se ponia al salir de la
// caja, no a los 11 cuadros: igual por casualidad en dificultad 2 (dorado 0 DIF), distinto con iconos lentos (dificultad 0:
// 132 campos de 4499; con esto 2). Banco: E:/DBZ3HD_trabajo/iwfin/cmp7db.py --ui.
REXCVAR_DEFINE_BOOL(dbz3_iw_botones_ui_anim, false, "DBZ3/IW",
                    "IW 0x7DB: relojes y fin de animacion de los objetos de la UI como el original (UI_UpdateFrame); corrige las dificultades distintas de la 2");
REXCVAR_DEFINE_BOOL(dbz3_iw_7db_original, true, "DBZ3/IW",
                    "IW 0x7DB (NB013): carriles, marca, choque, juicio, vida y rango con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
REXCVAR_DECLARE(bool, dbz3_iw_r23_eventos);   // IW-06: definido en iw_mini_disparo.cpp

namespace dbz3::iwmips::bubbles {
namespace {
constexpr Dueno kD7DB = Dueno(15), kD7D4 = Dueno(14), kD7D5 = Dueno(13);   // el enum Dueno (iw_mips.cpp) llega a 8: 15, 14 y 13 quedan para 7DB, 7D4 y 7D5

// ---------------- 7DB (NB013) ----------------
// Direcciones: EU y USA (USA validado cuerpo a cuerpo con E:/DBZ3HD_trabajo/T2B/verifica_usa.py; datos: usa_datos, -0x81C).
struct DirDB {
  uint32_t update, campo, reset, setup[6], neg, pose1, pose2, cam_ctrl, cam_mat, dispositivo[24];   // neg: a0 = nivel (NB006)
  uint32_t pctx, pb84, pb88, pact1, pact2, pb94;
};
constexpr DirDB kEuDB = {0x3550A0, 0x3568B0, 0x356EB0, {0x350F70, 0x350EC0, 0x350B60, 0x350C40}, 0, 0x350970, 0x35C810, 0x2ED7D0, 0x2ED810,
                         // IW-X03: 0x11A6B0 (__truncdfsf2) NO va mudo (el reloj de la ronda lo necesita, medido 9/11).
                         {0, 0x2EE040, 0x2ED690, 0x1353D0, 0x135370, 0x13C7A0, 0x372870},
                         0x49CE7C, 0x49CE84, 0x49CE88, 0x49CE8C, 0x49CE90, 0x49CE94};
constexpr DirDB kUsaDB = {0x3552D0, 0x356A90, 0x357000, {0x3512E0, 0x351230, 0x350660, 0x350FB0}, 0, 0x350470, 0x35C950, 0x2ED5C0, 0x2ED600,
                          {0, 0x2EDE30, 0x2ED480, 0x1353D0, 0x135370, 0x13C7A0, 0x3729A0},
                          0x49C660, 0x49C668, 0x49C66C, 0x49C670, 0x49C674, 0x49C678};
// NB006 (7D4): Mg_UpdateFrame 0x37A560 (fases) + Mg_UpdateTick 0x37B850 (cuadro); Mg_ResetState; SetLevelScaleValue, SetLevelTimerValue,
// InitParamTable, SetupRoundParams, CountPositiveCells y CountNegativeCells(nivel). Punteros +0x40 respecto a NB013.
constexpr DirDB kEuD4 = {0x37A560, 0x37B850, 0x37BE00, {0x376CE0, 0x376C30, 0x376790, 0x3768F0, 0x377390}, 0x3764F0, 0x3763E0, 0x380720,
                         0x2ED7D0, 0x2ED810,
                         {0, 0x2EE040, 0x2ED690, 0x1353D0, 0x135370, 0x13C7A0, 0x372870, 0x27D970, 0x377650, 0x377F10, 0x377A00, 0x22FD20, 0x207080},
                         0x49CEBC, 0x49CEC4, 0x49CEC8, 0x49CECC, 0x49CED0, 0x49CED4};
constexpr DirDB kUsaD4 = {0x37A5D0, 0x37B840, 0x37BDF0, {0x376E10, 0x376D60, 0x3768C0, 0x376A20, 0x3774C0}, 0x376620, 0x376510, 0x3806F0,
                          0x2ED5C0, 0x2ED600,
                          {0, 0x2EDE30, 0x2ED480, 0x1353D0, 0x135370, 0x13C7A0, 0x3729A0, 0x27D760, 0x377780, 0x378030, 0x377B30, 0x22FBB0, 0x207060},
                          0x49C6A0, 0x49C6A8, 0x49C6AC, 0x49C6B0, 0x49C6B4, 0x49C6B8};
constexpr uint32_t kWDB = 0x1160000, kB84DB = 0x1161000, kB88DB = 0x1161100, kB94DB = 0x1161200, kCamDB = 0x1162000, kMDB = 0x1170000;

bool ActivaDB() { return REXCVAR_GET(dbz3_iw_7db_original) && Listo() != nullptr; }

bool CampoBot(const DirDB& d, const char* nombre, const nb013::Entrada& in, nb013::Salida& out, uint32_t& semilla) {
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = nb013::Salida{};
  Ambito amb(*st, d.update == kEuDB.update || d.update == kUsaDB.update ? kD7DB : kD7D4);
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips {}: el espejo lo uso otro minijuego (t={}); este campo usa el port", nombre, in.t);
    return false;
  }
  auto ev = [&](char t, int n) { if (out.n_ev < 24) out.ev[out.n_ev++] = {t, n}; };
  // Char_EnterPoseStateByIndex(jugador, n) / Char_EnterPoseByTable_49ad90(rival, n): el estado de pose (+0xCBC, anterior +0xCBE)
  // lo lee la logica (Mg_CheckClashOutcome espera 3 contra 2); la animacion es del port
  auto pose = [&](char t, uint32_t a) {
    return [&ev, t, a](Ee& c) { const int n = int(c.r[5] & 0xFF); c.W16(a + 0xCBE, c.R16(a + 0xCBC)); c.W16(a + 0xCBC, uint16_t(n)); ev(t, n); };
  };
  amb.Puente(d.pose1, pose('J', kMDB + 0x800));
  amb.Puente(d.pose2, pose('R', kMDB + 0x28D0));
  bool cam_fin = in.cam_fin;   // un clip que empieza en este campo tiene su reloj a 0
  amb.Puente(kUiAnim, [&](Ee& c) {
    // EU 0x354EC0: a0=1 (ranura DATA_CMN 876), a1=0, a2=1. No es el clip 0 de 915.
    const char tipo = in.presenta_7db && c.r[4] == 1 && c.r[5] == 0 ? 'I' : 'C';
    ev(tipo, int(c.r[5] & 0xFFFF) | ((c.r[6] & 0xFF) ? 0 : 0x10000));
    cam_fin = false;
  });
  amb.Puente(kCamTimer, [&](Ee& c) { V0(c, cam_fin ? 1u : 0u); });
  amb.Puente(kCam170, [&](Ee&) {});
  amb.Puente(d.cam_ctrl, [&](Ee& c) { V0(c, kCamDB); });
  amb.Puente(d.cam_mat, [&](Ee& c) { V0(c, kCamDB + 0x100); });
  amb.Puente(kCamVista, [&](Ee&) {});
  amb.Puente(kEngine, [&](Ee& c) { V0(c, kEngDA); });
  amb.Puente(kCda4, [&](Ee& c) { V0(c, kCda4DC); });
  amb.Puente(kReady, [&](Ee& c) { ev('Y', int(c.r[4] & 0xFF)); });
  // s_UTL_CHRFADE(&actor) (EU 0x2EF640): Mg_SetClashStepPoses marca con su color a quien recibe el golpe del choque.
  // El color lo pinta el port (PasoFade); del invitado solo interesa QUIEN, como evento 'F' (0 jugador, 1 rival).
  amb.Puente(kChrFade, [&](Ee& c) {
    const uint32_t actor = c.R32(uint32_t(c.r[4]));
    ev('F', actor == kMDB + 0x28D0 ? 1 : 0);
  });
  amb.Mudo({kJadeo, kGolpeSnd, kVibra, kFadeH, kFadeU, kEfecto, kAudioStop, kAudioPlay, kVidaSnd, kCamUpd});   // kMmi2 = memset: corre (R23 IW-4)
  // IW-06 (dbz3_iw_r23_eventos): s_UTL_ACTUATE(*ce84, *(ce84+4), 100) de Mg_ResolvePlayerInput EU 0x353A50 y
  // Mg_UpdateHeldInputResolution 0x353490 (NB013: ce84 = {10, 6} de Mg_AllocContexts EU 0x358390) sale como evento 'V' (a0 | a1 << 8 | a2 << 16).
  if ((d.update == kEuDB.update || d.update == kUsaDB.update) && REXCVAR_GET(dbz3_iw_r23_eventos))
    amb.Puente(kVibra, [&](Ee& c) { ev('V', int((c.r[4] & 0xFF) | ((c.r[5] & 0xFF) << 8) | ((c.r[6] & 0xFFFF) << 16))); });
  // IW-FX-04 (resto de IW-F02): UI_UpdateFrame va dentro de la tarea de cuadro (Mg_UpdatePlayField / Mg_UpdateTick): sigue
  // mudo, pero marca la fase 2 de los objetos UI que se pongan despues (efectos de acierto/fallo desde el original).
  int fase_ui = 1;
  const bool es_d4 = d.update == kEuD4.update || d.update == kUsaD4.update;
  const bool ui_anim = es_d4 ? (ui::g_dur_ok && REXCVAR_GET(dbz3_iw_botones_ui_anim_d4))
                             : ((d.update == kEuDB.update || d.update == kUsaDB.update) && ui::g_dur_ok &&
                                REXCVAR_GET(dbz3_iw_botones_ui_anim));
  amb.Puente(kUiUpd, [&fase_ui, ui_anim, es_d4](Ee& c) {
    fase_ui = 2;
    if (!ui_anim) return;
    // IWFIN/IW-F14: UI_AdvanceAnimations (reloj +0x1C >= duracion +0x168: siguiente animacion +0x16C o, si no hay, el
    // retorno +0x184) y despues el paso al reloj de los objetos con el bit 4, en ese orden.
    // ponytail: el unico retorno que pone NB013 es Ui_ClearObjectFlags (EU 0x351020: quita los bits 2 y 4 y devuelve 1); se
    // aplica aqui en vez de llamar al invitado desde dentro de un puente.
    const float dt = c.Rf(kPasoT);
    for (uint32_t k = 0; k < 0x60; ++k) {
      const uint32_t b = kUiDA + k * 400, an = c.R32(b + 0x158);
      if (!(c.R32(b + 0x150) & 1) || an >= 256 || ui::g_dur[an] < 0 || c.Rf(b + 0x1C) < ui::g_dur[an]) continue;
      // R23: NB006 no pone retorno (+0x184 solo lo escribe NB013 con 0x351020), asi que en 7D4 solo encadena +0x16C.
      static int n_d4 = 0;
      if (es_d4 && n_d4 < 8) {
        ++n_d4;
        REXLOG_INFO("dbz3 r23 ui_anim 7d4: objeto {} anim {:X} fin a {:.1f} (sig {:X} ret {:X})", k, an, c.Rf(b + 0x1C),
                    c.R32(b + 0x16C), c.R32(b + 0x184));
      }
      if (const uint32_t sig = c.R32(b + 0x16C); sig != 0xFFFFFFFFu) {
        c.W32(b + 0x158, sig); c.Wf(b + 0x1C, 0.f); c.W32(b + 0x150, c.R32(b + 0x150) | 6); c.W32(b + 0x16C, 0xFFFFFFFFu);
      } else if (c.R32(b + 0x184)) {
        c.W32(b + 0x150, c.R32(b + 0x150) & ~6u); c.W32(b + 0x184, 0);
      }
    }
    for (uint32_t k = 0; k < 0x60; ++k) {
      const uint32_t b = kUiDA + k * 400;
      if ((c.R32(b + 0x150) & 5) == 5) c.Wf(b + 0x1C, c.Rf(b + 0x1C) + dt);
    }
  });
  ObservarUi(e, fase_ui);
  for (uint32_t a : d.dispositivo) if (a) amb.Puente(a, [](Ee&) {});
  // NB006: Mg_IntroScreenSetup (resultado global) es lo que pasa a la fase 2 (+4) y reinicia el reloj; lo demas es dibujo y poses del port
  if (d.neg) amb.Puente(d.dispositivo[9], [pb94 = d.pb94](Ee& c) { c.W32(kWDB + 4, 2); c.W32(kWDB + 0xC, 0); c.W8(kWDB + 0x5E, 1); c.W32(pb94 + 8, 0); });
  const uint32_t pctx = d.pctx, pb94 = d.pb94;
  if (in.inicio) {
    e.Cero(kWDB, 0x400); e.Cero(kB84DB, 0x10); e.Cero(kB88DB, 0x10); e.Cero(kB94DB, 0x100); e.Cero(kQDA, 0x120);
    e.Cero(kEngDA, 0x200); e.Cero(kMDB, 0x5000); e.Cero(kCamDB, 0x400); e.Cero(kCda4DC, 0x100);
    e.W32(pctx, kWDB); e.W32(d.pb84, kB84DB); e.W32(d.pb88, kB88DB); e.W32(pb94, kB94DB);
    // IW-06: Mg_AllocContexts EU 0x358390 deja ce84 = {10, 6} (solo lo lee s_UTL_ACTUATE)
    if ((d.update == kEuDB.update || d.update == kUsaDB.update) && REXCVAR_GET(dbz3_iw_r23_eventos)) {
      e.W8(kB84DB, 10); e.W32(kB84DB + 4, 6);
    }
    e.W32(d.pact1, kMDB + 0x800); e.W32(d.pact2, kMDB + 0x28D0); e.W32(kPActor, kMDB); e.W32(kPQte, kQDA);
    e.W32(kPUia, kUiDA); e.W32(kPNUia, 0x60);
    e.Wf(kPAnchoUi, 600.f);
    e.W32(pb94 + 4, 0); e.W32(pb94 + 8, 0);   // ce98 / ce9c: temporizador global del minijuego
    e.Cero(kUiDA, 0x60 * 400);
    for (uint32_t k = 0; k < 0x60; ++k) {
      const uint32_t b = kUiDA + k * 400;
      e.W32(b + 0x150, 1);
      e.Wf(b + 0x30, -16.f); e.Wf(b + 0x34, -16.f); e.Wf(b + 0x38, 16.f); e.Wf(b + 0x3C, 16.f); e.Wf(b + 0x2C, 1.f);
      e.W32(b + 0x16C, 0xFFFFFFFFu);   // IWFIN/IW-F14: sin animacion siguiente (UI_ResetObjectStruct)
    }
    // objetivos de los carriles (Mg_SetupUiObjects): objetos 1 y 2 en (ancho/2 + 150, 366 / 409), caja (-28, -28, 7, 28)
    for (uint32_t l = 0; l < 2; ++l) {
      const uint32_t b = kUiDA + (1 + l) * 400;
      e.Wf(b + 0x20, 450.f); e.Wf(b + 0x24, 366.f + 43.f * float(l));
      e.Wf(b + 0x30, -28.f); e.Wf(b + 0x34, -28.f); e.Wf(b + 0x38, 7.f); e.Wf(b + 0x3C, 28.f);
    }
    e.W8(kB84DB, 10); e.W32(kB84DB + 4, 6);   // Mg_AllocContexts
    // s_MSN_NB013_Updata (sin sus tareas): Mg_ResetContext, umbrales del rango, velocidad, tiempos y objetivos por dificultad
    bool ok = e.Llamar(d.reset, {});
    constexpr uint32_t kUmbr[11] = {0x5F, 100, 0x51, 0x5E, 0x45, 0x50, 0x38, 0x44, 1, 0x37, 0};
    for (uint32_t i = 0; i < 11; ++i) e.W32(kWDB + 0x310 + 4 * i, kUmbr[i]);
    e.W32(kWDB + 0x18, uint32_t(in.dif));
    for (uint32_t f : d.setup) if (f) ok = ok && e.Llamar(f, {});
    if (d.neg) ok = ok && e.Llamar(d.neg, {uint32_t(in.dif)});
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips {}: {} al preparar; se usa el port", nombre, e.error());
      st->dueno = kNadie;
      return false;
    }
    e.W32(kQDA + 0x110, 1);
  }
  e.Wf(kPasoT, kPasoCampo);
  e.W32(kSemilla, semilla);
  {
    const uint16_t nuevo = in.boton < 0 ? 0 : uint16_t(1u << in.boton);
    e.W16(kFlancoDA, uint16_t(in.flanco | nuevo));
    e.W16(kFlancoDA - 6, uint16_t(in.mantenido | nuevo));   // 0x4ADECC
    e.W16(kFlancoDA - 4, nuevo);                            // 0x4ADECE
    // PUL-IW-FX-03: el machaque del Masenko (NB006 Mg_PollPlayerButton EU 0x378F80) lee QTE_GetPressedButton(1,1,0) = Q+8,
    // que DragonMissionQTEInputs 0x2EE790 saca de pad +0x6E (0x4ADF2E): las direcciones NUEVAS de la palanca DERECHA
    // (FUN_0014baa0: +0x6C apretadas, +0x6E nuevas). Sin esto Q+8 era siempre -1: el especial no se cargaba nunca.
    const uint16_t pal_antes = e.R16(kFlancoDA + 0x5A);      // 0x4ADF2C
    e.W16(kFlancoDA + 0x5C, uint16_t(in.palanca & ~pal_antes));   // 0x4ADF2E
    e.W16(kFlancoDA + 0x5A, in.palanca);
  }
  // Fin de las poses del choque: al acabar la animacion el estado vuelve a la espera (3 jugador / 2 rival), que es lo que
  // Mg_CheckClashOutcome espera para cerrar el choque (en el original lo hace la maquina de poses, que es del port). Medido en
  // PCSX2 (dorado 7DB): la fase 2 del choque dura 67 campos; tambien vale el aviso de fin de animacion del port.
  if (d.update == kEuDB.update || d.update == kUsaDB.update) {   // solo NB013
    // Reacciones fuera del choque (estados 3..0x28): al acabar la animacion el estado vuelve a la espera, como la maquina de
    // poses del original (el port avisa con fin_pose; sin esto la pose se quedaba clavada hasta el siguiente golpe).
    for (int r = 0; r < 2; ++r) {
      const uint32_t a = kMDB + (r ? 0x28D0u : 0x800u);
      const uint16_t espera = r ? 2 : 3, st = e.R16(a + 0xCBC);
      if (in.fin_pose[r] && st != espera && st >= (r ? 3 : 4) && st < 0x29 && e.R32(kWDB + 8) != 3) {
        e.W16(a + 0xCBE, st); e.W16(a + 0xCBC, espera);
        ev(r ? 'R' : 'J', espera);   // la maquina de poses tambien arranca la espera en bucle
      }
    }
    const uint32_t cnt = kB94DB + 0xB0;
    if (e.R32(kWDB + 8) == 3 && e.R32(kB94DB) == 2) e.W32(cnt, e.R32(cnt) + 1); else e.W32(cnt, 0);
    if (e.R32(kWDB + 8) == 3 && e.R32(kB94DB) >= 1)
      for (int r = 0; r < 2; ++r) {
        const uint32_t a = kMDB + (r ? 0x28D0u : 0x800u);
        const uint16_t espera = r ? 2 : 3;
        if ((in.fin_pose[r] || e.R32(cnt) >= 67) && e.R16(a + 0xCBC) != espera) {
          e.W16(a + 0xCBE, e.R16(a + 0xCBC)); e.W16(a + 0xCBC, espera);
          ev(r ? 'R' : 'J', espera);
        }
      }
  }
  else {   // NB006: Mg_EnterResultPhase (fase 3) espera el aviso de fin de animacion del jugador (+0xC74), que da la maquina de poses del port
    const uint32_t a1 = kMDB + 0x800, cnt = kB94DB + 0xB0;
    if (e.R32(kWDB + 8) == 3 && e.R32(kB94DB) == 3) e.W32(cnt, e.R32(cnt) + 1); else e.W32(cnt, 0);
    // ponytail: tope de 120 campos por si la pose del port no avisa; el original espera el fin de la 0x29E (180 cuadros)
    e.W32(a1 + 0xC74, (in.fin_pose[0] || e.R32(cnt) >= 120) ? 1 : 0);
    // Reacciones y fin de punto (Mg_UpdateRoundStart / derrota de Mg_UpdateFrame) esperan a los dos en la pose de espera
    // (3 jugador / 2 rival): la maquina de poses del port avisa con fin_pose; sin aviso, a los 90 campos
    for (int r = 0; r < 2; ++r) {
      const uint32_t a = kMDB + (r ? 0x28D0u : 0x800u), cn = kB94DB + 0xB4 + 4 * uint32_t(r);
      const uint16_t espera = r ? 2 : 3, st = e.R16(a + 0xCBC);
      const bool reaccion = st != espera && st >= (r ? 3 : 4) && st < 0x29;
      e.W32(cn, reaccion ? e.R32(cn) + 1 : 0);
      if (reaccion && (in.fin_pose[r] || e.R32(cn) >= 90)) { e.W16(a + 0xCBE, st); e.W16(a + 0xCBC, espera); }
    }
  }
  const Cronometro cr(e);
  // orden medido en el bench contra PCSX2: la tarea de cuadro (Mg_UpdatePlayField) va antes que Mg_Update
  const bool ok = e.Llamar(d.campo, {}) && e.Llamar(d.update, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips {}: {} (t={}); la partida sigue con el port", nombre, e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  out.fase = int(e.R32(kWDB + 4)); out.sub = int(e.R32(kWDB + 8)); out.paso = int(e.R32(kWDB + 0x10));
  out.activo = int(e.R32(kWDB + 0x2E0)); out.rango = int(e.R32(kWDB + 0x1C)); out.nota = e.Rf(kWDB + 0x20);
  out.choques = int(e.R32(kWDB + 0x48));
  const uint32_t a1 = kMDB + 0x800, a2 = kMDB + 0x28D0;
  out.vida[0] = e.Rf(a1 + 0x1850); out.vida[1] = e.Rf(a2 + 0x1850);
  for (int l = 0; l < 2; ++l) {   // Mg_SpawnNextMarker: el icono k va en el objeto 0x16 + k % 10 + 10 l
    out.llegaron[l] = int(e.R32(kWDB + 0x2C + 4 * uint32_t(l)));
    out.golpes[l] = int(e.R32(kWDB + 0x3C + 4 * uint32_t(l)));
    out.intentos[l] = int(e.R32(kWDB + 0x44 + 4 * uint32_t(l)));
    const int cnt = std::min(50, int(e.R32(kWDB + 0x2C8 + 4 * uint32_t(l))));
    for (int k = std::max(0, cnt - 10); k < cnt; ++k) {
      const uint32_t f = kWDB + uint32_t(l) * 100 + uint32_t(k);
      nb013::Icono& ic = out.ic[l][out.n_ic[l]++];
      ic.k = k;
      ic.boton = e.R8(kQDA + 0x28 + uint32_t(l) * 100 + uint32_t(k));
      ic.x = e.Rf(kUiDA + uint32_t(k % 10 + l * 10 + 0x16) * 400 + 0x20);
      ic.vivo = e.R8(f + 0x130) != 0;
      ic.fallado = e.R8(f + 0x1F8) != 0;
    }
  }
  out.marca_x[0] = e.Rf(kUiDA + 0x2A * 400 + 0x20); out.marca_y = e.Rf(kUiDA + 0x2A * 400 + 0x24);
  out.marca_x[1] = e.Rf(kUiDA + 0x2B * 400 + 0x20); out.marca_x[2] = e.Rf(kUiDA + 0x2C * 400 + 0x20);
  out.marca_indice = int(e.R32(kB94DB + 0x7C)); out.marca_lado = int(e.R32(kB94DB + 0x80));
  LeerUi(e, kUiDA, 0x60);   // IW-FX-04
  if (es_d4) {
    out.qte_boton = int(e.R32(kUiDA + 0x2F * 400 + 0x44));
  } else if (out.marca_indice >= 0 && out.marca_indice < 50 && (out.marca_lado == 0 || out.marca_lado == 1)) {
    // NB013 EU 0x353090: QTE_CheckPressedMatch(0,1,0, indice, carril), no el objeto del contador.
    out.qte_boton = int(e.R8(kQDA + 0x28 + uint32_t(out.marca_lado * 100 + out.marca_indice)));
  }
  out.choque_fase = int(e.R32(kB94DB)); out.pulsos = int(e.R32(kB94DB + 8)); out.choque_lado = int(e.R32(kB94DB + 0x80));
  {
    const int lado = int(e.R32(kB94DB + 0x80)), nivel = int(e.R32(kWDB + 0x18));
    out.objetivo = int(e.R32(kB94DB + 4 * uint32_t(lado * 6 + nivel + 3)));
  }
  st->Cuenta(us);
  // Medir FPS sin construir/volcar cientos de palabras de RAM en cada cuadro.
  // Optativo: los bancos de replay conservan su traza completa de serie.
  if (log && !std::getenv("DBZ3_TEST_IW_SIN_TRAZA_CUADRO")) {  // qte7db.py juego reejecuta esta traza en ee.py
    std::string w, c94;
    for (uint32_t i = 0; i < 0x364 / 4; ++i) Hx(w, e.R32(kWDB + 4 * i));
    for (uint32_t i = 0; i < 0x28; ++i) Hx(c94, e.R32(kB94DB + 4 * i));
    std::string evs;
    for (int i = 0; i < out.n_ev; ++i) { char t[16]; std::snprintf(t, sizeof t, "%c%d,", out.ev[i].tipo, out.ev[i].n); evs += t; }
    REXLOG_INFO("dbz3 iwmips {} t={} ini={} dif={} seed_in={:08X} seed_out={:08X} in={},{},{:X},{:X} vida={:X},{:X} w={} c94={} ev={} "
                "pose={:X},{:X} instr={} us={:.1f} fp={}{}",
                nombre, in.t, int(in.inicio), in.dif, semilla_in, semilla, in.boton, int(in.cam_fin), in.flanco, in.mantenido,
                e.R32(a1 + 0x1850), e.R32(a2 + 0x1850), w, c94, evs.empty() ? "-" : evs, e.R16(a1 + 0xCBC), e.R16(a2 + 0xCBC), e.n - cr.n0, us,
                int(in.fin_pose[0]), int(in.fin_pose[1]));   // IWFIN: fp = fin de pose (entrada del port) para reejecutar el registro
  }
  return true;
}

bool CampoDB(const nb013::Entrada& in, nb013::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7db_original)) return false;
  return CampoBot(kGp == 0x4A35F0 ? kUsaDB : kEuDB, "nb013", in, out, semilla);
}
bool ActivaD4() { return REXCVAR_GET(dbz3_iw_7d4_original) && Listo() != nullptr; }
bool CampoD4(const nb006::Entrada& in, nb006::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7d4_original)) return false;
  return CampoBot(kGp == 0x4A35F0 ? kUsaD4 : kEuD4, "nb006", in, out, semilla);
}

// ---------------- 7D5 (NB007) ----------------
struct DirD5 {
  uint32_t dispatch, cue, reset, params, pose, muldf, cam_ctrl, cam_mat, mudos[16];
  uint32_t pctx, pbuf;
};
// 9/11: 0x11A6B0 (__truncdfsf2) NO va mudo: QTE_Update suma al reloj de la ronda (float)(paso * 1/60 en doble); mudo, el
// reloj sumaba lo que quedara en f0 (dorado de PCSX2: 830/1853 campos iguales; sin el mudo y con la pose, 1843/1853).
// IW-F01 (11/11): Minigame_CheckTransformSound (EU 0x371E30, USA 0x371F60) ya NO va mudo: llama a Memory (32 pasos del
// LFSR por ronda); mudo, el exe gastaba 32 y el original 64 (qte7d5.py juego: 9 campos con otra semilla). Su sonido es kGolpeSnd.
constexpr DirD5 kEuD5 = {0x295030, 0x295140, 0x295300, 0x2928C0, 0x309940, 0x11A040, 0x2ED7D0, 0x2ED810,
                         {0x293BD0, 0x294560, 0x2F26B0, 0x27D970, 0x2EE040, 0x2ED690, 0x135370, 0x13C7A0, 0, 0x2ED8D0},
                         0x49CBA8, 0x49CBB0};
constexpr DirD5 kUsaD5 = {0x294DA0, 0x294EB0, 0x295070, 0x292690, 0x309720, 0x11A040, 0x2ED5C0, 0x2ED600,
                          {0x293960, 0x2942D0, 0x2F24A0, 0x27D760, 0x2EDE30, 0x2ED480, 0x135370, 0x13C7A0, 0, 0x2ED6C0},
                          0x49C398, 0x49C3A0};
constexpr uint32_t kWD5 = 0x1180000, kBD5 = 0x1181000, kMD5 = 0x1190000;

bool ActivaD5() { return REXCVAR_GET(dbz3_iw_7d5_original) && Listo() != nullptr; }

bool CampoD5(const nb007::Entrada& in, nb007::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7d5_original)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  const DirD5& d = kGp == 0x4A35F0 ? kUsaD5 : kEuD5;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = nb007::Salida{};
  Ambito amb(*st, kD7D5);
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips nb007: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  auto ev = [&](char t, int n) { if (out.n_ev < 16) out.ev[out.n_ev++] = {t, n}; };
  amb.Puente(d.pose, [&](Ee& c) {   // MgPlyB_ChangeState(actor, n): el estado (+0xCBC / +0xCBE) lo lee la logica; la animacion es del port
    const uint32_t a = uint32_t(c.r[4]);
    const int n = int(c.r[5] & 0xFF);
    c.W16(a + 0xCBE, c.R16(a + 0xCBC)); c.W16(a + 0xCBC, uint16_t(n));
    ev('P', n);
  });
  amb.Puente(d.muldf, [](Ee& c) {   // __muldf3 (soft-float): el interprete no tiene pmfhl.lw; producto de doubles del anfitrion
    double x, y;
    uint64_t a = c.r[4], b = c.r[5];
    std::memcpy(&x, &a, 8); std::memcpy(&y, &b, 8);
    const double z = x * y;
    uint64_t r;
    std::memcpy(&r, &z, 8);
    c.r[2] = r;
  });
  bool cam_fin = in.cam_fin;
  amb.Puente(kUiAnim, [&](Ee& c) { ev('C', int(c.r[5] & 0xFFFF) | ((c.r[6] & 0xFF) ? 0 : 0x10000)); cam_fin = false; });
  amb.Puente(kCamTimer, [&](Ee& c) { V0(c, cam_fin ? 1u : 0u); });
  amb.Puente(d.cam_ctrl, [&](Ee& c) { V0(c, kCamDB); });
  amb.Puente(d.cam_mat, [&](Ee& c) { V0(c, kCamDB + 0x100); });
  amb.Puente(kEngine, [&](Ee& c) { V0(c, kEngDA); });
  amb.Puente(kReady, [&](Ee& c) { ev('Y', int(c.r[4] & 0xFF)); });
  amb.Mudo({kJadeo, kGolpeSnd, kChrFade, kVibra, kFadeH, kFadeU, kEfecto, kAudioStop, kAudioPlay, kVidaSnd, kCamUpd, kUiUpd,
            kCam170, kCamVista, kCamF164, kVoz, kBoca});
  for (uint32_t a : d.mudos) if (a) amb.Puente(a, [](Ee&) {});
  int fase_ui = 1;   // IW-F02: MiniGame_UpdateFrame = MiniGame_UpdateInputCue y UI_UpdateFrame; despues el despacho
  ObservarUi(e, fase_ui);
  if (in.inicio) {
    e.Cero(kWD5, 0x200); e.Cero(kBD5, 0x10); e.Cero(kQDA, 0x120); e.Cero(kEngDA, 0x200); e.Cero(kMD5, 0x6000); e.Cero(kCamDB, 0x400);
    e.W32(d.pctx, kWD5); e.W32(d.pbuf, kBD5); e.W32(kPActor, kMD5); e.W32(kPQte, kQDA);
    e.W32(kPUia, kUiDA); e.W32(kPNUia, 0x60);
    e.Wf(kPAnchoUi, 600.f);
    e.Cero(kUiDA, 0x60 * 400);
    for (uint32_t k = 0; k < 0x60; ++k) {
      const uint32_t b = kUiDA + k * 400;
      e.W32(b + 0x150, 1);
      e.Wf(b + 0x30, -16.f); e.Wf(b + 0x34, -16.f); e.Wf(b + 0x38, 16.f); e.Wf(b + 0x3C, 16.f); e.Wf(b + 0x2C, 1.f);
      e.W32(b + 0x16C, 0xFFFFFFFFu);   // IWFIN/IW-F14: sin animacion siguiente (UI_ResetObjectStruct)
    }
    bool ok = e.Llamar(d.reset, {});
    e.W8(kWD5 + 0x14, uint8_t(in.dif));   // NB007: el "tipo" del trabajo es la dificultad
    // s_MSN_NB007_Update (sin sus tareas): umbrales de tiempo del rango y limite de la QTE
    constexpr uint32_t kUm[11][2] = {{0xCC, 0x3c23d70a}, {0xD0, 0x41c80000}, {0xD4, 0x41c8147b}, {0xD8, 0x420c0000}, {0xDC, 0x420c0a3d},
                                     {0xE0, 0x42200000}, {0xE4, 0x42200a3d}, {0xE8, 0x42340000}, {0xEC, 0x42340a3d}, {0xF0, 0x425c0000},
                                     {0xF4, 0x425c0a3d}};
    for (const auto& u : kUm) e.W32(kWD5 + u[0], u[1]);
    ok = ok && e.Llamar(d.params, {});
    e.W32(kWD5 + 0xF8, 0x40e00000); e.W32(kWD5 + 0xC4, 2); e.W32(kWD5 + 0xC8, 0x40000000); e.W8(kWD5 + 0xFC, 2);
    e.W8(kBD5, 10); e.W32(kBD5 + 4, 6); e.W32(kBD5 + 8, 0);
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips nb007: {} al preparar; se usa el port", e.error());
      st->dueno = kNadie;
      return false;
    }
    e.W32(kQDA + 0x110, 1);
  }
  e.Wf(kPasoT, kPasoCampo);
  e.W32(kSemilla, semilla);
  int boton = in.boton;
  uint16_t flanco = in.flanco, mantenido = in.mantenido;
  {  // IW-F01, solo pruebas (DBZ3_TEST_IWMINI con "fallos"): ronda 1 (2.a), la 1.a tecla va mal; ronda 2 (3.a) sin
     // pulsar nada, caduca. El piloto de PasoOriginalD5 no falla; asi se ven la cadena del fallo y la caducidad.
    static bool mal_hecho = false;
    if (in.inicio) mal_hecho = false;
    const char* prueba = std::getenv("DBZ3_TEST_IWMINI");
    const int ronda = int(int32_t(e.R32(kWD5 + 0x54)));
    if (prueba && std::strstr(prueba, "fallos")) {
      if (ronda == 1 && boton >= 0 && !mal_hecho) {
        mal_hecho = true;
        boton = boton == 15 ? 14 : 15;
        flanco = mantenido = uint16_t(1u << boton);
      } else if (ronda == 2) {
        boton = -1;
        flanco = mantenido = 0;
      }
    }
  }
  {
    const uint16_t nuevo = boton < 0 ? 0 : uint16_t(1u << boton);
    e.W16(kFlancoDA, uint16_t(flanco | nuevo));
    e.W16(kFlancoDA - 6, uint16_t(mantenido | nuevo));
    e.W16(kFlancoDA - 4, nuevo);
  }
  if (in.pose >= 0) {   // la cadena del fallo (+0x82: 6 -> 7 -> 3) y el cierre de ronda esperan a la pose del actor
    const uint32_t a = kMD5 + 0x800;
    if (e.R16(a + 0xCBC) != uint16_t(in.pose)) { e.W16(a + 0xCBE, e.R16(a + 0xCBC)); e.W16(a + 0xCBC, uint16_t(in.pose)); }
    e.W32(a + 0xC78, uint32_t(in.vueltas)); e.W32(a + 0xC74, in.anim_fin ? 1u : 0u);
  }
  const Cronometro cr(e);
  bool ok = e.Llamar(d.cue, {});
  fase_ui = 2;
  ok = ok && e.Llamar(d.dispatch, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips nb007: {} (t={}); la partida sigue con el port", e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  out.fase = int(e.R32(kWD5)); out.sub = int(e.R32(kWD5 + 4)); out.reloj = e.Rf(kWD5 + 8); out.paso = int(e.R32(kWD5 + 0xC));
  out.ronda = int(int32_t(e.R32(kWD5 + 0x54))); out.n = int(e.R32(kWD5 + 0xB8));
  out.cuenta = int(e.R32(kWD5 + 0x5C)); out.aciertos = int(e.R32(kWD5 + 0x60)); out.tiempo = e.Rf(kWD5 + 0x7C);
  out.total = e.Rf(kWD5 + 0x44); out.rango = e.R8(kWD5 + 0x88);
  for (int i = 0; i < 10; ++i) out.seq[i] = e.R8(kQDA + 0x28 + uint32_t(i));
  LeerUi(e, kUiDA, 0x60);
  out.w80 = e.R8(kWD5 + 0x80);
  st->Cuenta(us);
  if (log) {  // solo pruebas: E:/DBZ3HD_trabajo/T2B/qte7d5.py juego <dbz3.log> la reejecuta en ee.py
    std::string w;
    for (uint32_t i = 0; i < 0x120 / 4; ++i) Hx(w, e.R32(kWD5 + 4 * i));
    std::string evs;
    for (int i = 0; i < out.n_ev; ++i) { char t[16]; std::snprintf(t, sizeof t, "%c%d,", out.ev[i].tipo, out.ev[i].n); evs += t; }
    // IW-F01: pose= (entrada de la maquina de poses del port) para que qte7d5.py juego reejecute el mismo campo
    REXLOG_INFO("dbz3 iwmips nb007 t={} ini={} dif={} seed_in={:08X} seed_out={:08X} in={},{},{:X},{:X} w={} ev={} instr={} us={:.1f} pose={},{},{}",
                in.t, int(in.inicio), in.dif, semilla_in, semilla, boton, int(in.cam_fin), flanco, mantenido, w,
                evs.empty() ? "-" : evs, e.n - cr.n0, us, in.pose, in.vueltas, int(in.anim_fin));
  }
  return true;
}

const bool kRegistradoBotones = (nb013::g_campo = &CampoDB, nb013::g_activa = &ActivaDB, nb006::g_campo = &CampoD4,
                                 nb006::g_activa = &ActivaD4, nb007::g_campo = &CampoD5, nb007::g_activa = &ActivaD5, true);   // tanda3: + 7DB, 7D4, 7D5
}  // namespace
}  // namespace dbz3::iwmips::bubbles
#endif
#endif
