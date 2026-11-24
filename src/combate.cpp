// dbz3 - Combate (frente combate, ronda 2): reglas opcionales del combate y sonda de medida.
//
// Todo lo que cambia la jugabilidad esta APAGADO por defecto, lo enciende el jugador (F1 > COMBATE) y se
// apaga solo en el online (las dos partidas tienen que simular lo mismo) y en la version EU (direcciones US).
//   05 reglas competitivas v0 : dos golpes de Goku con otro dano (bloque HR cambiado solo durante el golpe)
//   06 aviso de golpe         : marca en pantalla antes del impacto + sonda de la ventana del contraataque
//   12 laboratorio (datos)    : sonda por cuadro (logs/combate_sonda.csv) y golpes (logs/combate_golpes.csv)
//   15 definitiva directa     : la definitiva sin entrar en modo hiper (paga el ki de su capsula, una vez)
//   16 boton de cancelacion   : LB o RB = adelante + ki en una pulsacion (la entrada que el juego ya acepta)
//   18 perder la transformacion: un golpe de N o mas de dano devuelve a la forma normal (rutina del juego)
// Medidas y RE: docs/14_frentes/combate.md. Frame data: studies/tools/frame_data.py.
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "src/tactics/entrada.h"   // TACTICS-01: puerta unica de las variables de prueba de Tactics
#include "roster_ext.h"   // SLOTS_PJ-06: CatalogueLimit() del catalogo de capsulas
#include "reloj_prueba.h"
#include "combate.h"
#include "comp_estado.h"
#include "comp_hud.h"
#include "comp_aura.h"
#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "escenarios_intro_sonda.h"
#include "ingame/game_style.h"
#include "ingame/online_panel.h"   // BattleNow: diagnostico del acabado OBJ
#include "iw_minijuegos.h"
#include "cinematica.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "net/netplay.h"
#include "tagteam.h"
#include "t3_crusher_ball.h"
#include "survivors/fx_real.inl"

REXCVAR_DEFINE_BOOL(dbz3_combate_reglas_v0, false, "DBZ3/Combate",
                    "Reglas competitivas v0: dos golpes de Goku con otro dano (no online)");
REXCVAR_DEFINE_BOOL(dbz3_combate_aviso, false, "DBZ3/Combate", "Aviso en pantalla antes de cada golpe (no cambia nada)");
REXCVAR_DEFINE_BOOL(dbz3_combate_sonda, false, "DBZ3/Combate", "Sonda del laboratorio: logs/combate_*.csv");
REXCVAR_DEFINE_BOOL(dbz3_combate_definitiva_directa, false, "DBZ3/Combate",
                    "Definitiva sin modo hiper: P+K+G+E con el ki de su capsula (no online)");
REXCVAR_DEFINE_STRING(dbz3_combate_boton_cancel, "no", "DBZ3/Combate",
                      "Boton de cancelacion: no, lb o rb = adelante + ki en una pulsacion (no online)");
REXCVAR_DEFINE_INT32(dbz3_combate_reversion, 0, "DBZ3/Combate",
                     "Dano de un golpe que devuelve a la forma normal (0 = nunca; no online)");
// DR01 (#63): el Rush teletransporta al atacante al borde al salir de su cinematica (medido: 109,7 u; ver
// studies 5/ENTREGAS/INFORME_tPEND.md). Apagado = identico al original; encendido devuelve al atacante a su
// sitio de antes del Rush (<= 20 u) durante 12 fotogramas tras el remate.
REXCVAR_DEFINE_BOOL(dbz3_rush_sin_salto, false, "DBZ3/Combate",
                    "Dragon Rush sin salto: devuelve al atacante a su sitio al acabar el Rush (mitigacion; "
                    "apagado = original) | Dragon Rush: send the attacker back to its spot after the Rush "
                    "(mitigation; off = original)");
// COMP: Modo Competitivo = el Aura Burst de Infinite World, con estado independiente del hiper de B3 (estudio, parejas de
// funciones y decisiones: studies 5/ENTREGAS/MODO_COMPETITIVO_ESTUDIO.md). Apagado = juego original.
REXCVAR_DEFINE_BOOL(dbz3_modo_competitivo, false, "DBZ3/Combate",
                    "Modo Competitivo: L2 = Aura Burst (1 barra para entrar, 10 de ki por cuadro, no cansa al acabar), "
                    "definitiva con Arriba + Circulo, sin Dragon Rush (online: si la sala lo acuerda) | Competitive Mode: L2 = Aura Burst, "
                    "ultimate with Up + Circle, no Dragon Rush (online: if the room agrees)");
// Solo pruebas (no salen en el menu): muneco del laboratorio y ki del jugador del teclado.
REXCVAR_DEFINE_STRING(dbz3_combate_prueba_muneco, "", "DBZ3/Combate",
                      "Pruebas: el ordenador no decide (quieto) o se queda en guardia (guardia)");
REXCVAR_DEFINE_INT32(dbz3_combate_prueba_ki, -1, "DBZ3/Combate", "Pruebas: ki del jugador del teclado (una vez)");
REXCVAR_DEFINE_INT32(dbz3_combate_prueba_ki_muneco, -1, "DBZ3/Combate", "Pruebas: ki fijo del muneco (-1 = no)");
REXCVAR_DEFINE_INT32(dbz3_combate_prueba_vida, -1, "DBZ3/Combate", "Pruebas: vida del jugador del teclado (una vez)");
REXCVAR_DEFINE_INT32(dbz3_combate_prueba_forma, -1, "DBZ3/Combate",
                     "Pruebas: forma del jugador del teclado (una vez, con la rutina del juego; Tag Team no deja P+K+G)");

namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3

namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // misma entrada XInput que las cinematicas y el Tag Team
}

namespace dbz3::combate {
namespace {

constexpr dbz3::GA kMgrPtr = 0x823D6CF4;   // cfg/gestor del combate (US y EU)
// Registro de cada mando (184 B, sub_8208FD58): +12 botones pulsados (bits PS2: 0x8000 cuadrado ... 0x0100 L2),
// +14 recien pulsados, +26 palanca x (s8), +72 + 4j cuadros pulsado (>0) o desde que se solto (<0) del boton
// j = bit - 8, +108/+110 direcciones (0x20 derecha, 0x80 izquierda). Medido 2026-10-15 (sonda).
constexpr dbz3::GA kPads = 0x8238B780, kPadOfSlot = 0x8238B670;
// Campos del luchador X (= cfg + 2096 + 8240 k), medidos o leidos del codigo recompilado (docs/15_tecnicas):
constexpr uint32_t kAct = 3244, kMask = 3250, kHp = 6080, kKi = 6100, kKiMax = 6104;
constexpr uint32_t kForm = 2980, kNextForm = 2984, kId = 7420, kPadIdx = 5028;
constexpr uint32_t kHitFrom = 6132, kHitHr = 6136, kHitPending = 6140, kHitProps = 6144, kCsk = 2756;
constexpr uint32_t kHyperState = 8184, kHyper = 8208;
constexpr uint32_t kCode = 2928, kHitFrame = 2922, kAnimFrame = 3160;   // codigo de ataque, cuadro de impacto, cuadro
constexpr uint32_t kTimers = 4960 + 72;   // cuadros de P, K, G, E (s32; I = X+4960, sub_820F6770)
constexpr uint32_t kScreenX = 4960 + 48, kRivalScreenX = 4960 + 56;   // x en pantalla propia / del rival

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

uint32_t Mgr(uint8_t* base) { return REX_LOAD_U32(kMgrPtr); }
uint32_t Fighter(uint8_t* base, int k) {
  const uint32_t m = Mgr(base);
  return m ? m + 2096 + 8240 * uint32_t(k) : 0;
}
int SideOf(uint8_t* base, uint32_t x) {
  const uint32_t m = Mgr(base);
  if (!m || x < m + 2096) return -1;
  const uint32_t d = x - (m + 2096);
  return d % 8240 == 0 && d / 8240 < 2 ? int(d / 8240) : -1;
}
uint32_t BattleState(uint8_t* base) {
  const uint32_t m = Mgr(base);
  return m ? REX_LOAD_U32(m + 35128) : 0;
}
// Registro de mando (0..3) que maneja al luchador, o -1 (ordenador).
int PadRecord(uint8_t* base, uint32_t x) {
  const int16_t pad = int16_t(REX_LOAD_U16(x + kPadIdx));
  if (pad < 0 || pad > 3) return -1;
  const uint32_t rec = REX_LOAD_U32(kPadOfSlot + 4 * uint32_t(pad));
  return rec < 4 ? int(rec) : -1;
}

// Las reglas que cambian la simulacion: en la US; en el online solo si la sala vio las mismas en los dos PCs
// (frente online, netplay.h CombatRulesAgreed; durante la partida quedan fijas).
bool Rules() {
  const auto& s = dbz3::net::Session::Get();
  return (!s.active() || s.CombatRulesAgreed());
}
int CancelButton() {   // bit PS2 del boton de cancelacion (0 = no)
  const std::string& v = REXCVAR_GET(dbz3_combate_boton_cancel);
  return v == "lb" ? 0x0400 : v == "rb" ? 0x0800 : 0;
}

std::atomic<uint32_t> g_frame{0};   // cuadros de pelea (hilo del juego)
std::atomic<int64_t> g_tick_ms{0};  // ultimo paso de pelea (para el panel)
std::atomic<int64_t> g_inicio_ms{0};   // empiezo de la pelea (estado 4)
std::atomic<bool> g_obj_cel{false};
std::atomic<int> g_aviso[2] = {};   // cuadros que faltan para el golpe que va a recibir cada lado (0 = nada)
std::atomic<bool> g_humano[2] = {};
std::atomic<int64_t> g_aviso_ms[2] = {};   // ultimo aviso (para DBZ3_COMBATE_AVISO_PRUEBA: verlo en una captura)

int64_t NowMs() { return dbz3::RelojMs(); }

// ---------------------------------------------------------------- sonda (06, 12)
std::FILE* g_sonda = nullptr;
std::FILE* g_golpes = nullptr;
std::FILE* g_volcado = nullptr;
uint32_t g_volcado_max = 0;

std::FILE* Abrir(const char* name, const char* header) {
  std::error_code ec;
  std::filesystem::create_directories("logs", ec);
  std::FILE* f = std::fopen((std::string("logs/") + name).c_str(), "w");
  if (f && header) std::fputs(header, f);
  return f;
}

bool SondaOn() {
  static const char* env = std::getenv("DBZ3_COMBATE_SONDA");
  return REXCVAR_GET(dbz3_combate_sonda) || env;
}

void SondaLado(uint8_t* base, std::FILE* f, uint32_t x) {
  const int rec = PadRecord(base, x);
  const uint32_t r = rec >= 0 ? uint32_t(kPads + 184 * uint32_t(rec)) : 0;
  std::fprintf(f, ",%u,%u,%u,%04X,%.0f,%.0f,%u,%u,%u,%u,%03X,%u,%.0f,%d,%d,%d,%d,%d,%04X,%04X,%.2f,%.2f,%.2f,%.2f",
               REX_LOAD_U32(x + kId), REX_LOAD_U16(x + kAct), REX_LOAD_U16(x + 3246), REX_LOAD_U16(x + kMask),
               LoadF(base, x + kHp), LoadF(base, x + kKi), REX_LOAD_U32(x + kForm), REX_LOAD_U32(x + kNextForm),
               REX_LOAD_U8(x + kHyperState), REX_LOAD_U8(x + kHyper), REX_LOAD_U32(x + kCode),
               REX_LOAD_U16(x + kHitFrame), LoadF(base, x + kAnimFrame), rec, int32_t(REX_LOAD_U32(x + kTimers)),
               int32_t(REX_LOAD_U32(x + kTimers + 4)), int32_t(REX_LOAD_U32(x + kTimers + 8)),
               int32_t(REX_LOAD_U32(x + kTimers + 12)), r ? REX_LOAD_U16(r + 12) : 0, r ? REX_LOAD_U16(r + 108) : 0,
               LoadF(base, x + kScreenX), LoadF(base, x + kRivalScreenX), LoadF(base, x + 64), LoadF(base, x + 72));
}

void Sonda(uint8_t* base, uint32_t st) {
  if (!SondaOn()) {
    if (g_sonda) std::fclose(g_sonda), g_sonda = nullptr;
    if (g_golpes) std::fclose(g_golpes), g_golpes = nullptr;
    return;
  }
  if (!g_sonda) {
    const char* lado = "id,accion,accion_antes,estado,vida,ki,forma,forma_sig,hiper_estado,hiper,codigo,cuadro_golpe,"
                       "cuadro_anim,mando,t_p,t_k,t_g,t_e,botones,direccion,px,px_rival,x,z";
    std::string h = "cuadro,combate";
    for (const char* p : {"1_", "2_"}) {
      std::string l = lado;
      for (size_t i = 0, s = 0; i <= l.size(); ++i)
        if (i == l.size() || l[i] == ',') h += std::string(",") + p + l.substr(s, i - s), s = i + 1;
    }
    if (dbz3::intro_sonda::Activa()) h += dbz3::intro_sonda::Cabecera();
    g_sonda = Abrir("combate_sonda.csv", (h + "\n").c_str());
    g_golpes = Abrir("combate_golpes.csv",
                     "cuadro,tipo,victima,atacante,id_atacante,codigo,cuadro_anim,props,hr,dano_hr,vida_antes,"
                     "vida_despues,accion_antes,accion_despues,ki_antes,ki_despues,regla,devuelve\n");
  }
  const uint32_t x0 = Fighter(base, 0), x1 = Fighter(base, 1);
  if (!g_sonda || !x0) return;
  std::fprintf(g_sonda, "%u,%u", g_frame.load(), st);
  SondaLado(base, g_sonda, x0);
  SondaLado(base, g_sonda, x1);
  if (dbz3::intro_sonda::Activa()) dbz3::intro_sonda::Fila(base, g_sonda, Mgr(base));
  std::fputc('\n', g_sonda);
  if (g_frame.load() % 60 == 0) std::fflush(g_sonda), std::fflush(g_golpes);
}

// Volcado crudo de los dos luchadores por cuadro (DBZ3_COMBATE_VOLCADO=N cuadros): para buscar campos.
void Volcado(uint8_t* base) {
  static const char* env = std::getenv("DBZ3_COMBATE_VOLCADO");
  if (!env) return;
  if (!g_volcado_max) {
    g_volcado_max = uint32_t(std::max(1, std::atoi(env)));
    std::error_code ec;
    std::filesystem::create_directories("logs", ec);
    g_volcado = std::fopen("logs/combate_volcado.bin", "wb");
  }
  static uint32_t n = 0;
  if (!g_volcado || n >= g_volcado_max) return;
  ++n;
  const uint32_t m = Mgr(base);
  uint8_t hdr[16] = {'F', 'R', 'M', '1'};
  const uint32_t fr = g_frame.load(), st = BattleState(base);
  std::memcpy(hdr + 4, &fr, 4);
  std::memcpy(hdr + 8, &st, 4);
  std::memcpy(hdr + 12, &m, 4);
  std::fwrite(hdr, 1, 16, g_volcado);
  for (uint32_t k = 0; k < 2; ++k) std::fwrite(base + m + 2096 + 8240 * k, 1, 8240, g_volcado);
  std::fwrite(base + kPads, 1, 184 * 2, g_volcado);
  if (n == g_volcado_max) std::fclose(g_volcado), g_volcado = nullptr;
}

// ---------------------------------------------------------------- 06 aviso de golpe
// El juego guarda en el atacante el cuadro de impacto de su golpe (X+2922, la primera linea AP1 del #CSK) y el
// cuadro de la animacion (X+3160): cuando faltan 10 cuadros o menos (lo que deja contraatacar el juego: el tiempo
// de la guardia entre -5 y 10, sub_82102DF8) se avisa al que lo va a recibir, si es un jugador. No cambia nada.
constexpr int kAvisoCuadros = 10;
bool EsMuneco(uint8_t* base, uint32_t x);   // pruebas (abajo): al muneco no se le avisa
void Avisos(uint8_t* base) {
  for (int k = 0; k < 2; ++k) {
    const uint32_t a = Fighter(base, 1 - k), d = Fighter(base, k);
    int falta = 0;
    const uint16_t hf = REX_LOAD_U16(a + kHitFrame);
    if (REX_LOAD_U16(a + kAct) == 15 && hf != 0xFFFF) {
      const float cf = LoadF(base, a + kAnimFrame);
      const int f = int(std::lround(float(hf) - cf));
      if (f > 0 && f <= kAvisoCuadros) falta = f;
    }
    const bool humano = PadRecord(base, d) >= 0 && !EsMuneco(base, d);
    if (falta && !g_aviso[k].load() && humano && REXCVAR_GET(dbz3_combate_aviso))
      REXLOG_INFO("dbz3 combate: 06 aviso al lado {}: golpe {:X} en {} cuadros (cuadro {})", k + 1,
                  REX_LOAD_U32(a + kCode), falta, g_frame.load());
    g_aviso[k] = falta;
    g_humano[k] = humano;
    if (falta) g_aviso_ms[k] = NowMs();
  }
}

// ---------------------------------------------------------------- 05 reglas competitivas v0
// Estatico: (personaje, bloque HR del #CSK, dano clasico, dano v0). Bloques HR de un solo golpe
// (frame_data.py --usos): Goku normal, 0x210 patada (HR 16, impacto en el cuadro 11) y 0x21B adelante + punetazo
// (HR 30, cuadro 9).
struct Regla {
  uint32_t id;
  uint32_t hr;
  int16_t clasico, v0;
};
constexpr Regla kReglasV0[] = {{0, 16, 50, 70}, {0, 30, 35, 50}};

// ---------------------------------------------------------------- 18 perder la transformacion
// La rutina del juego que ajusta la forma al ki tras un golpe (sub_8211A510 -> sub_82119258 baja de forma hasta
// la normal salvo fusiones/absorciones/formas fijas; sub_820FB5D0 cambia el modelo). Se le hace creer que no queda
// ki para mantener la forma (ki a 0 solo durante la llamada).
void Revertir(PPCContext& ctx, uint8_t* base, uint32_t x, float dano) {
  const uint32_t antes = REX_LOAD_U32(x + kForm);
  const float ki = LoadF(base, x + kKi);
  StoreF(base, x + kKi, 0.0f);
  PPCContext c = ctx;
  c.r3.u64 = x;
  DBZ3_CALL_EU(sub_8211A510)(c, base);
  StoreF(base, x + kKi, ki);
  REXLOG_INFO("dbz3 combate: 18 golpe de {:.0f} (umbral {}): forma {} -> {} (siguiente {})", dano,
              REXCVAR_GET(dbz3_combate_reversion), antes, REX_LOAD_U32(x + kForm), REX_LOAD_U32(x + kNextForm));
}

// Dano que no pasa por sub_8210ED48 (ki blasts y otros proyectiles, el golpe de la escena de una definitiva): se
// ve en el paso de pelea como una bajada de vida que el gancho del golpe no apunto. Un evento por bajada.
std::array<float, 2> g_dano_gancho{};   // daño visto por el gancho del golpe desde el ultimo paso
void OtroDano(PPCContext& ctx, uint8_t* base, uint32_t st) {
  static std::array<float, 2> vida{};
  static std::array<uint32_t, 2> id{};
  static std::array<bool, 2> listo{};
  for (int k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, k);
    const float hp = LoadF(base, x + kHp);
    const float bajada = listo[size_t(k)] && id[size_t(k)] == REX_LOAD_U32(x + kId)
                             ? vida[size_t(k)] - hp - g_dano_gancho[size_t(k)] : 0.0f;
    listo[size_t(k)] = st == 4;
    vida[size_t(k)] = hp;
    id[size_t(k)] = REX_LOAD_U32(x + kId);
    g_dano_gancho[size_t(k)] = 0.0f;
    const int umbral = REXCVAR_GET(dbz3_combate_reversion);
    if (umbral <= 0 || !Rules() || st != 4 || bajada < float(umbral) || REX_LOAD_U32(x + kForm) == 0) continue;
    if (hp <= 0.0f || dbz3::tag::Hud(k).switching) {
      REXLOG_INFO("dbz3 combate: 18 dano de {:.0f} sin golpe directo al lado {} (umbral {}): no revierte: {}", bajada,
                  k + 1, umbral, hp <= 0.0f ? "KO (manda el KO)" : "relevo del Tag Team en curso");
      continue;
    }
    REXLOG_INFO("dbz3 combate: 18 dano de {:.0f} sin golpe directo (proyectil o escena) al lado {}", bajada, k + 1);
    Revertir(ctx, base, x, bajada);
  }
}

// ---------------------------------------------------------------- pruebas (laboratorio)
// Muneco: dbz3_combate_prueba_muneco = quieto|guardia (o DBZ3_COMBATE_MUNECO, o SetMuneco desde el Laboratorio de
// modos.cpp). El rival del teclado pasa a ser un JUGADOR con un mando 2 virtual que no se mueve (y, en guardia,
// mantiene G pulsada): el juego hace el resto como con una persona (guardia nativa en cuanto puede, tambien al
// levantarse; nada de IA ni de contraataques). Antes se le quitaba la IA (sub_820F8470) y se le ponia la accion 1:
// otra rutina del ordenador lo seguia moviendo (acciones 2, 5, 6) y la guardia se caia a los 4-7 cuadros, asi que
// el mismo golpe unas veces se bloqueaba y otras no (runA_p1 y rpg lab_guardia, medido 2026-10-15).
// dbz3_combate_prueba_ki = N (o DBZ3_COMBATE_KI al empezar): ki del lado del teclado. DBZ3_COMBATE_LADOS=2: el
// teclado maneja el lado 2 y el muneco es el lado 1 (medir los dos lados).
constexpr uint32_t kMunecoPad = 1;   // mando (y usuario de XInput) del muneco
const bool g_lados2 = std::getenv("DBZ3_COMBATE_LADOS") && std::atoi(std::getenv("DBZ3_COMBATE_LADOS")) == 2;
std::atomic<int> g_muneco_api{-1};   // SetMuneco (Laboratorio de modos.cpp): -1 = lo que diga el ajuste
std::atomic<int> g_muneco_lado{-1};  // lado al que se le dio el mando virtual (-1 = ninguno)
int Muneco() {   // lado del muneco (bit 0), en guardia (bit 1), dando patadas (bit 2, solo pruebas), o -1
  static const char* env = std::getenv("DBZ3_COMBATE_MUNECO");
  if (const int api = g_muneco_api.load(); api >= 0)
    return api == 0 ? -1 : (g_lados2 ? 0 : 1) | (api == 2 ? 2 : 0) | (api == 3 ? 4 : 0);
  const std::string& v = REXCVAR_GET(dbz3_combate_prueba_muneco);
  const std::string m = v.empty() && env ? env : v;
  if (m != "quieto" && m != "guardia" && m != "ataca" && m != "pk" && m != "ki") return -1;
  return (g_lados2 ? 0 : 1) | (m == "guardia" ? 2 : 0) | (m == "ataca" ? 4 : 0) | (m == "pk" ? 8 : 0) | (m == "ki" ? 16 : 0);
}
bool EsMuneco(uint8_t* base, uint32_t x) { return g_muneco_lado.load() >= 0 && SideOf(base, x) == g_muneco_lado.load(); }
bool MunecoMandoActivo(uint8_t* base) {   // fuera de la pelea el mando 2 vuelve a ser el de verdad
  if (g_muneco_lado.load() < 0) return false;
  const uint32_t st = BattleState(base);
  if (st == 3 || st == 4) return true;
  g_muneco_lado = -1;
  return false;
}
// Cada cuadro: el lado del muneco lleva el mando virtual mientras haya muneco; al quitarlo vuelve al ordenador.
void MunecoCuadro(uint8_t* base, uint32_t st) {
  const int m = (st == 3 || st == 4) && Rules() ? Muneco() : -1;
  const int lado = g_muneco_lado.load();
  if (m < 0 || (lado >= 0 && lado != (m & 1))) {
    if (lado >= 0 && (st == 3 || st == 4)) REX_STORE_U16(Fighter(base, lado) + kPadIdx, 0xFFFF);
    if (lado >= 0) REXLOG_INFO("dbz3 combate: muneco fuera (lado {})", lado + 1);
    g_muneco_lado = -1;
    if (m < 0) return;
  }
  const uint32_t x = Fighter(base, m & 1);
  if (REX_LOAD_U16(x + kPadIdx) != kMunecoPad) REX_STORE_U16(x + kPadIdx, uint16_t(kMunecoPad));
  if (g_muneco_lado.exchange(m & 1) != (m & 1))
    REXLOG_INFO("dbz3 combate: muneco en el lado {} ({})", (m & 1) + 1, m & 2 ? "guardia" : m & 4 ? "ataca" : m & 8 ? "pk" : "quieto");
}
// Registro del mando virtual (en vez de leer XInput): conectado, sin palancas, G mantenida si va en guardia.
void MunecoMando(uint8_t* base) {
  const uint32_t r = kPads + 184 * kMunecoPad, m = Mgr(base);
  uint16_t ahora = 0;
  static uint32_t n = 0;
  const int modo = Muneco();
  // G mantenida; K cada 12 cuadros (cadena de patadas); o P y K a la vez cada 40 (P+K, ventana 0x04 del #CSK)
  ++n;
  // y "ki" (pruebas de la Leyenda de Mr. Satan): E cada 45 cuadros = una bola de ki al jugador
  const uint8_t quiero = modo & 2 ? 4 : (modo & 4) && n % 12 == 0 ? 2 : (modo & 8) && n % 40 == 0 ? 3 : (modo & 16) && n % 45 == 0 ? 8 : 0;
  for (uint32_t j = 0; quiero && j < 8; ++j) {
    const uint8_t b = m ? REX_LOAD_U8(m + (kMunecoPad + 4382) * 8 + j) : 0;
    if (b == quiero || (quiero == 3 && (b == 1 || b == 2))) ahora = uint16_t(ahora | (0x100u << j));
  }
  const uint16_t antes = REX_LOAD_U16(r + 12);
  REX_STORE_U32(r + 0, 3);   // conectado (sub_8208FC40)
  REX_STORE_U16(r + 12, ahora);
  REX_STORE_U16(r + 14, uint16_t(ahora & ~antes));
  REX_STORE_U16(r + 16, uint16_t(antes & ~ahora));
  for (uint32_t j = 0; j < 8; ++j) {
    const int32_t t = int32_t(REX_LOAD_U32(r + 72 + 4 * j));
    const bool p = ahora & (0x100u << j);
    REX_STORE_U32(r + 72 + 4 * j, uint32_t(p ? (t > 0 ? std::min(t + 1, 100000) : 1) : (t > 0 ? -1 : std::max(t - 1, -100000))));
  }
  REX_STORE_U32(r + 24, 0);   // palancas
  for (uint32_t o = 108; o <= 114; o += 2) REX_STORE_U16(r + o, 0);   // direcciones
}
void PruebaInicio(uint8_t* base) {
  static const char* ki = std::getenv("DBZ3_COMBATE_KI");
  const uint32_t x0 = Fighter(base, 0), x1 = Fighter(base, 1);
  if (g_lados2) {   // el teclado (mando 0) pasa al lado 2; el 1 es el ordenador (o el muneco, con su mando 2)
    if (!EsMuneco(base, x0)) REX_STORE_U16(x0 + kPadIdx, 0xFFFF);
    REX_STORE_U16(x1 + kPadIdx, 0);
    REXLOG_INFO("dbz3 combate: prueba: el teclado lleva el lado 2");
  }
  if (ki) REXCVAR_SET(dbz3_combate_prueba_ki, std::atoi(ki));
}
void PruebaCuadro(PPCContext& ctx, uint8_t* base) {   // el ki se mantiene mientras el del teclado esta en una accion neutra
  if (const int km = REXCVAR_GET(dbz3_combate_prueba_ki_muneco); km >= 0 && g_muneco_lado.load() >= 0)
    StoreF(base, Fighter(base, g_muneco_lado.load()) + kKi, float(km));   // el del muneco, siempre
  // forma del teclado (una vez): destino X+2984 y sub_82115330, lo mismo que hace P+K+G al cargar el modelo
  if (const int f = REXCVAR_GET(dbz3_combate_prueba_forma); f >= 0) {
    const uint32_t x = Fighter(base, g_lados2 ? 1 : 0);
    REXCVAR_SET(dbz3_combate_prueba_forma, -1);
    if (f > 0 && f < 8 && REX_LOAD_U32(x + 7608 + 4 * uint32_t(f)) && REX_LOAD_U32(x + kForm) == 0) {
      REX_STORE_U32(x + kNextForm, uint32_t(f));
      PPCContext c = ctx;
      c.r3.u64 = x;
      c.r4.u64 = 0;
      DBZ3_CALL_EU(sub_82115330)(c, base);
    }
    REXLOG_INFO("dbz3 combate: prueba: forma {} pedida en el lado {}: forma {} (siguiente {})", f, g_lados2 ? 2 : 1,
                REX_LOAD_U32(x + kForm), REX_LOAD_U32(x + kNextForm));
  }
  if (const int v = REXCVAR_GET(dbz3_combate_prueba_vida); v >= 0) {   // vida del teclado, una vez (0 = KO)
    StoreF(base, Fighter(base, g_lados2 ? 1 : 0) + kHp, float(v));
    REXCVAR_SET(dbz3_combate_prueba_vida, -1);
    REXLOG_INFO("dbz3 combate: prueba: vida {} en el lado {} (cuadro {})", v, g_lados2 ? 2 : 1, g_frame.load());
  }
  const int ki = REXCVAR_GET(dbz3_combate_prueba_ki);
  if (ki < 0) return;
  const uint32_t x = Fighter(base, g_lados2 ? 1 : 0);
  if (REX_LOAD_U16(x + kAct) > 2) {
    REXCVAR_SET(dbz3_combate_prueba_ki, -1);
    REXLOG_INFO("dbz3 combate: prueba: ki {} hasta el cuadro {} (accion {}, ki ahora {:.0f})", ki, g_frame.load(),
                REX_LOAD_U16(x + kAct), LoadF(base, x + kKi));
    return;
  }
  StoreF(base, x + kKi, float(ki));
}

// ---------------------------------------------------------------- 15 definitiva directa
struct Ult {
  uint16_t cap = 0, code = 0, aire = 0;   // capsula y codigos de suelo y aire (w8, w12, w13) de la definitiva
};
std::array<Ult, 2> g_ult{};
std::array<std::array<Ult, 8>, 2> g_ults{};   // todas las vistas en este combate (g_ult = la ultima)
bool Equipada(uint8_t* base, uint32_t x, uint16_t cap) {   // lista de capsulas del luchador: *(X+7412) +52 n, +54
  const uint32_t sel = REX_LOAD_U32(x + 7412);
  if (!sel) return false;
  const uint32_t n = std::min<uint32_t>(REX_LOAD_U16(sel + 52), 7);
  for (uint32_t i = 0; i < n; ++i)
    if (REX_LOAD_U16(sel + 54 + 2 * i) == cap) return true;
  return false;
}
float Coste(uint8_t* base, uint16_t cap) {   // #SKC: *(0x8237560C) + 40 ID, +15 = decimas de barra
  const uint32_t skc = REX_LOAD_U32(dbz3::GA(0x8237560C));
  // el catalogo ampliado (roster_ext) llega a kInventorySize=2048: con el tope viejo de 1024 una
  // definitiva de una capsula >= 1024 costaba 1e9 (la regla 15 no la daba nunca por pagable)
  return skc && cap < dbz3::roster::CatalogueLimit() ? float(REX_LOAD_U8(skc + 40 * uint32_t(cap) + 15)) * 100.0f : 1e9f;
}
bool Directa(uint8_t* base, uint32_t x, int lado) {
  const Ult& u = g_ult[size_t(lado)];
  return u.cap && Equipada(base, x, u.cap) && LoadF(base, x + kKi) >= Coste(base, u.cap);
}
// ---------------------------------------------------------------- COMP modo competitivo
// Solo US (los ganchos nuevos no tienen pareja EU verificada). En el online, solo si la sala lo acordo en los dos PCs
// (bit 0x20 de CombatRules, netplay.cpp): todo lo que usa vive en la memoria del juego o sale del mando de ese cuadro,
// que el online sincroniza por debajo de sub_8208FD58 (tambien al repetir cuadros del rollback).
bool Competitivo() { return REXCVAR_GET(dbz3_modo_competitivo) && !dbz3::g_guest_eu && Rules(); }
// R16: estado propio dentro del bloque del luchador que Snapshot guarda/restaura entero (net/rollback.h).
// +6116 era EXCLUSIVAMENTE el ki guardado del Hiper: los dos lectores de sub_82110818 solo entran con +8208 != 0
// (generated/19.cpp:7762-7801). En COMP se interceptan AMBAS escrituras on/off y +8208 permanece cero.
// Se reutiliza ese campo inerte, no un hueco supuesto ni una asignacion fuera del rollback. El inicializador
// sub_821094E8 ya lo borra al crear luchador/asaltar (generated/25.cpp:8037). La marca no parece un puntero del heap.
constexpr uint32_t kCompBurst = 6116, kCompBurstOn = 0x434D5001, kCompAtaqueGratis = 2;
std::atomic<bool> g_comp_burst[2]{};
std::atomic<float> g_comp_ki[2]{};
std::array<bool, 2> g_l2{};   // se rehace desde el mando sincronizado, nunca decide desde el reloj host
thread_local uint32_t g_comp_chase = 0;   // contexto de llamada, nunca estado persistente entre cuadros
thread_local uint32_t g_comp_nodo = 0, g_comp_actor = 0;   // comprobacion del #CCM en curso
thread_local uint32_t g_comp_capsula = 0;   // cobro de la capsula del ataque, no su posesion/forma
bool Burst(uint8_t* base, uint32_t x) {
  return x && SideOf(base, x) >= 0 && Competitivo() &&
         (REX_LOAD_U32(x + kCompBurst) & ~kCompAtaqueGratis) == kCompBurstOn;
}
bool AtaqueGratis(uint8_t* base, uint32_t x) {
  return (REX_LOAD_U32(x + kCompBurst) & ~1u) == ((kCompBurstOn & ~1u) | kCompAtaqueGratis) &&
         REX_LOAD_U16(x + kAct) == 15;
}
void PublicarComp(uint8_t* base) {
  const bool en = Competitivo() && BattleState(base) == 4;
  for (int k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, k);
    // Solo US usa esta marca. No tocar el ki guardado del Hiper nativo.
    if (x && !dbz3::g_guest_eu && !Competitivo() && !REX_LOAD_U8(x + kHyper)) {
      const uint32_t v = REX_LOAD_U32(x + kCompBurst) & ~1u;
      if (v == (kCompBurstOn & ~1u) || v == ((kCompBurstOn & ~1u) | kCompAtaqueGratis))
        REX_STORE_U32(x + kCompBurst, 0);
    }
    g_comp_burst[k].store(en && Burst(base, x), std::memory_order_relaxed);
    const float ki = en && x ? LoadF(base, x + kKi) / 1000.0f : 0.0f;
    g_comp_ki[k].store(std::isfinite(ki) ? std::clamp(ki, 0.0f, 6.0f) : 0.0f, std::memory_order_relaxed);
  }
}
void CompApagar(uint8_t* base, uint32_t x) {
  const uint32_t s = REX_LOAD_U32(x + kCompBurst);
  // IW fija la gratuidad del ataque al iniciarlo (0x21C530), aunque el Burst termine antes del impacto.
  if ((s & ~kCompAtaqueGratis) == kCompBurstOn)
    REX_STORE_U32(x + kCompBurst, s & kCompAtaqueGratis ? s & ~1u : 0);
}
// Definitiva pedida este cuadro con Arriba + E. La escribe el gancho del mando en cada lectura (sub_8208FD58) y la
// consume sub_820F7758 en el mismo cuadro: asi L2 a secas es siempre Aura Burst.
// Vive fuera de la memoria del juego, pero se rehace del mando sincronizado en cada lectura: igual en los dos PCs.
std::array<bool, 2> g_def_pedida{};
std::array<bool, 2> g_l1{};   // COMP/7: L1 recien pulsado este cuadro (persecucion), igual que la marca de arriba
// IW: la definitiva es la entrada Arriba + E del #BCM (cond 0x8009), sin exigir estado. En B3 es P+K+G+E con el bit de
// hiper: Arriba + el boton de E pasa a traer P+K+G+E ese cuadro (como el boton de cancelacion) y deja la marca. Solo
// si hay una definitiva equipada y pagable: sin ella, Arriba + E sigue siendo la rafaga de ki (los #CCM nativos no
// tienen ninguna entrada con Arriba: censo en MODO_COMPETITIVO_ESTUDIO.md).
// PRUEBA (DBZ3_TEST_COMP_RUSH, solo via tools/comp_pruebas.py): el lado 1 hace la entrada del Dragon Rush al cuadro, que con
// teclas inyectadas no cabe en su ventana (~0,4 s): en reposo, P+K+G+E (hiper; =3 y =4 sin hiper), el rival a 15 delante y
// P+K (lanzador); 8 cuadros despues E + R1 (la entrada del Rush) y, con =2 o =4, 20 cuadros despues L1 (persecucion).
// Repite cada 4 s. Pulsar = bit del registro (+12), recien pulsado (+14) y cuadros pulsado = 1 (+72 + 4j).
void PruebaRush(uint8_t* base, uint32_t user) {
  static const int modo = std::getenv("DBZ3_TEST_COMP_RUSH") ? std::atoi(std::getenv("DBZ3_TEST_COMP_RUSH")) : 0;
  const uint32_t x = Fighter(base, 0);
  if (!modo || user != 0 || !x || PadRecord(base, x) != 0) return;
  static int fase = 0;
  static uint32_t t = 0;
  ++t;
  const uint32_t r = kPads, cfg = Mgr(base) + (REX_LOAD_U16(x + kPadIdx) + 4382u) * 8;
  auto pulsa = [&](std::initializer_list<uint8_t> quiero, uint16_t extra) {
    uint16_t bits = extra;
    for (const uint8_t w : quiero)
      for (uint32_t j = 0; j < 8; ++j)
        if (REX_LOAD_U8(cfg + j) == w) { bits = uint16_t(bits | (0x100u << j)); break; }
    for (uint32_t j = 0; j < 8; ++j)
      if (bits & (0x100u << j)) REX_STORE_U32(r + 72 + 4 * j, 1);
    REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
    REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
    REXLOG_INFO("dbz3 combate: prueba rush: fase {} pulsa {:04X} (accion {}, hiper {}, ki {:.0f}, rival {})", fase, bits,
                REX_LOAD_U16(x + kAct), REX_LOAD_U8(x + kHyper), LoadF(base, x + kKi), REX_LOAD_U16(Fighter(base, 1) + kAct));
  };
  const bool hiper = modo <= 2, l1 = modo == 2 || modo == 4;
  const uint16_t act = REX_LOAD_U16(x + kAct);
  if (fase == 0 && act <= 2 && t > 240) {
    if (hiper && !(Competitivo() ? Burst(base, x) : REX_LOAD_U8(x + kHyper))) {
      if (Competitivo()) pulsa({}, 0x0100);   // L2 fisico; el control conserva el combo B3
      else pulsa({1, 2, 4, 8}, 0);
    }
    fase = 1, t = 0;
  } else if (fase == 1 && t > 90 && act <= 2 && (!hiper || (Competitivo() ? Burst(base, x) : REX_LOAD_U8(x + kHyper)))) {
    const uint32_t a = x + 64, b = Fighter(base, 1) + 64;   // el rival a 15 delante (solo en el suelo)
    const float dx = LoadF(base, b) - LoadF(base, a), dz = LoadF(base, b + 8) - LoadF(base, a + 8);
    const float d = std::max(0.01f, std::sqrt(dx * dx + dz * dz));
    StoreF(base, b, LoadF(base, a) + dx / d * 15.0f);
    StoreF(base, b + 8, LoadF(base, a + 8) + dz / d * 15.0f);
    pulsa({1, 2}, 0);
    fase = 2, t = 0;
  } else if (fase == 2 && t == 8) {
    pulsa({8}, 0x0800);
  } else if (fase == 2 && t == 20 && l1) {
    pulsa({}, 0x0400);
  } else if (fase == 2 && t > 30) {
    fase = 0, t = 0;
  } else if (fase == 1 && t > 600) {
    fase = 0, t = 0;
  }
}

void CompMando(uint8_t* base, uint32_t user) {
  uint32_t x = 0;
  for (int k = 0; k < 2; ++k)
    if (const uint32_t f = Fighter(base, k); f && PadRecord(base, f) == int(user)) x = f;
  if (!x) return;
  const int lado = SideOf(base, x);
  g_def_pedida[size_t(lado)] = false;
  g_l1[size_t(lado)] = false;
  g_l2[size_t(lado)] = false;
  if (!Competitivo()) return;
  const uint32_t r = kPads + 184 * user, cfg = Mgr(base) + (REX_LOAD_U16(x + kPadIdx) + 4382u) * 8;
  g_l2[size_t(lado)] = (REX_LOAD_U16(r + 14) & 0x0100) != 0;
  // COMP/7 (decision 6): L1 es la persecucion y deja de ser su atajo (P+G): el juego lo ve suelto, como el boton de
  // cancelacion; la pulsacion nueva queda en g_l1 para sub_821033E0.
  if (REX_LOAD_U16(r + 12) & 0x0400) {
    g_l1[size_t(lado)] = (REX_LOAD_U16(r + 14) & 0x0400) != 0;
    REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) & ~0x0400));
    REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) & ~0x0400));
    REX_STORE_U32(r + 72 + 4 * 2, uint32_t(-1));
  }
  uint16_t eb = 0, bits = 0;
  for (uint32_t j = 0; j < 8; ++j)
    if (REX_LOAD_U8(cfg + j) == 8) eb = uint16_t(eb | (0x100u << j));
  if (g_l2[size_t(lado)]) {
    // Adaptador del #CCM de B3 al boton L2 del #BCM IW: solo durante ESTA lectura se presenta su entrada
    // de animacion. La condicion de abajo exige g_l2; P+K+G+E real por si solo nunca la acepta.
    for (const uint8_t want : {1, 2, 4, 8})
      for (uint32_t j = 0; j < 8; ++j)
        if (REX_LOAD_U8(cfg + j) == want) {
          bits = uint16_t(bits | (0x100u << j));
          REX_STORE_U32(r + 72 + 4 * j, 1);
          break;
        }
    REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
    REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
    return;   // L2 no es la definitiva, aunque se mantenga Arriba
  }
  if (!(REX_LOAD_U16(r + 108) & 0x10) || !(REX_LOAD_U16(r + 14) & eb)) return;   // Arriba mantenido + E recien pulsado
  bool hay = false;
  for (const Ult& u : g_ults[size_t(lado)])
    hay = hay || (u.code && Equipada(base, x, u.cap) && LoadF(base, x + kKi) >= Coste(base, u.cap));
  if (!hay) return;
  for (const uint8_t want : {1, 2, 4})
    for (uint32_t j = 0; j < 8; ++j)
      if (REX_LOAD_U8(cfg + j) == want) {
        bits = uint16_t(bits | (0x100u << j));
        REX_STORE_U32(r + 72 + 4 * j, 1);
        break;
      }
  REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | bits));
  REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | bits));
  g_def_pedida[size_t(lado)] = true;
  REXLOG_INFO("dbz3 combate: COMP Arriba + E del lado {}: definitiva pedida (ki {:.0f}, hiper {}, cuadro {})", lado + 1,
              LoadF(base, x + kKi), REX_LOAD_U8(x + kHyper), g_frame.load());
}

// Seguimiento de la definitiva (log): empieza (codigo de la entrada), cobro (el juego quita el ki de una vez cuando
// la definitiva se confirma: el ataque conecta y arranca la escena) y como acaba (de pie, golpeado, KO, fin).
std::array<uint32_t, 2> g_ultima_code{};   // ultimo codigo de ataque de cada lado (para los proyectiles)
void DefinitivaLog(uint8_t* base) {
  static float ki[2] = {};
  static uint32_t code[2] = {};
  static bool en[2] = {};
  static float cobrado[2] = {};
  static float vida_rival[2] = {};
  static uint32_t cuadro0[2] = {};
  static uint32_t espera[2] = {};
  static std::vector<uint32_t> cadena[2];   // codigos por los que pasa (para saber si salio entera)
  if (BattleState(base) < 4) g_ults = {};
  for (int k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, k), c = REX_LOAD_U32(x + kCode), act = REX_LOAD_U16(x + kAct);
    const float now = LoadF(base, x + kKi);
    // astraDIF: traza por cuadro para comparar una forma con el personaje separado.
    // Solo la activa el escenario de pruebas; no modifica el combate.
    if (static const bool trace = std::getenv("DBZ3_TEST_DIF") != nullptr; trace && k == 0 && (act == 15 || en[k])) {
      REXLOG_INFO("dbz3 dif: frame {} form {} code {:X} act {} ki {:.0f} hp2 {:.0f} cam {:08X} ccm {:08X} "
                  "anim {:08X} {:08X} {:08X} {:08X} flags {:08X} {:08X} af {:.2f} hf {} csk {:08X} hit {:08X} {}",
                  g_frame.load(), REX_LOAD_U32(x + kForm), c, act, now, LoadF(base, Fighter(base, 1) + kHp),
                  REX_LOAD_U32(x + 7896), REX_LOAD_U32(x + 5444),
                  REX_LOAD_U32(x + 2932), REX_LOAD_U32(x + 2936), REX_LOAD_U32(x + 2940), REX_LOAD_U32(x + 2944),
                  REX_LOAD_U32(x + 3248), REX_LOAD_U32(x + 3252), LoadF(base, x + kAnimFrame),
                  REX_LOAD_U16(x + kHitFrame), REX_LOAD_U32(x + kCsk),
                  REX_LOAD_U32(Fighter(base, 1) + kHitHr), REX_LOAD_U16(Fighter(base, 1) + kHitPending));
    }
    if (act == 15 && c) g_ultima_code[size_t(k)] = c;
    const Ult* ini = nullptr;   // un moveset puede traer varias entradas de definitiva (por forma, suelo y aire)
    if (code[k] != c)
      for (const Ult& u : g_ults[size_t(k)])
        if (u.code && (c == u.code || c == u.aire)) { ini = &u; break; }
    auto fin = [&](int kk, uint32_t xx, uint32_t aa) {
      std::string lista;
      for (uint32_t v : cadena[kk]) { char t[12]; std::snprintf(t, sizeof(t), "%X ", v); lista += t; }
      const uint32_t rival = Fighter(base, 1 - kk);
      REXLOG_INFO("dbz3 combate: 15 fin de la definitiva del lado {}: accion {}, vida {:.0f}, estado {}, cobrado {:.0f}, "
                  "cuadros {}, dano al rival {:.0f} (accion {}), codigos {}",
                  kk + 1, aa, LoadF(base, xx + kHp), BattleState(base), cobrado[kk], g_frame.load() - cuadro0[kk],
                  vida_rival[kk] - LoadF(base, rival + kHp), REX_LOAD_U16(rival + kAct), lista);
      en[kk] = false;
      espera[kk] = 0;
    };
    if (ini) {
      if (en[k]) fin(k, x, act);   // la anterior sigue esperando su ultimo golpe: se cierra antes de empezar otra
      REXLOG_INFO("dbz3 combate: 15 definitiva {:X} del lado {} (capsula {}, coste {:.0f}): empieza con ki {:.0f}, "
                  "modo hiper {}, distancia {:.1f}", c, k + 1, ini->cap, Coste(base, ini->cap), now, REX_LOAD_U8(x + kHyper),
                  std::hypot(LoadF(base, x + 64) - LoadF(base, Fighter(base, 1 - k) + 64),
                             LoadF(base, x + 72) - LoadF(base, Fighter(base, 1 - k) + 72)));
      en[k] = true;
      espera[k] = 0;
      cobrado[k] = 0;
      vida_rival[k] = LoadF(base, Fighter(base, 1 - k) + kHp);
      cuadro0[k] = g_frame.load();
      cadena[k].assign(1, c);
    } else if (en[k]) {
      if (c != code[k] && cadena[k].size() < 48) cadena[k].push_back(c);
      if (ki[k] - now > 500.0f) {
        cobrado[k] += ki[k] - now;
        REXLOG_INFO("dbz3 combate: 15 cobro de {:.0f} de ki al lado {} (cuadro {}, codigo {:X}): {:.0f} -> {:.0f}",
                    ki[k] - now, k + 1, g_frame.load(), c, ki[k], now);
      }
      // Solo registro: el golpe final puede caer despues de salir de la accion 15 (Baby: 61 y 1020 mas tarde), asi que el
      // fin se anota 180 cuadros despues (o al acabar el combate, o al empezar otra definitiva) para no cortar antes.
      if (act != 15 || BattleState(base) != 4) {
        if (!espera[k]) espera[k] = std::max<uint32_t>(g_frame.load(), 1u);
        if (g_frame.load() - espera[k] >= 180 || BattleState(base) != 4) fin(k, x, act);
      } else espera[k] = 0;   // una pausa entre fases no cuenta como final: GT vuelve a la escena antes del impacto
    }
    code[k] = c;
    ki[k] = now;
  }
}

// ---------------------------------------------------------------- modo ondas (prueba IV de la Nave de Babidi)
// B2, modo de combate 9: el ordenador no elige ningun movimiento (FUN_001d1c40 da -2: nodo de reposo), tras cada reaccion
// vuelve a la guardia (FUN_001d1db0 da 1 = accion 1) y a toda onda que le llega la devuelve (FUN_001d1980 da 2 -> FUN_001f7fc0
// da 5). El que tiene que devolver es el jugador; el contador (OndasDevueltas) cuenta sus devoluciones. Aqui: el rival es el
// muneco en guardia (mando virtual 2: G mantenida, sin IA) y lleva la habilidad Deflect-Back (X+8125+16, la que lee
// sub_82116FA0 con la guardia pulsada), que se repone cada cuadro (sub_82112260 la recalcula de las capsulas).
std::atomic<bool> g_ondas_modo{false};
std::atomic<int> g_ondas_muneco_antes{-1};   // el muneco que habia antes de entrar al modo
void OndasRival(uint8_t* base, uint32_t st) {
  const int lado = g_muneco_lado.load();
  if (!g_ondas_modo.load() || lado < 0 || (st != 3 && st != 4)) return;
  REX_STORE_U8(Fighter(base, lado) + 8125 + 16, 1);
}

}  // namespace

// ---------------------------------------------------------------- panel (hilo de dibujo)
namespace S = dbz3::ingame::style;
class CombatePanel : public rex::ui::ImGuiDialog {
 public:
  explicit CombatePanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // La mascara cel pertenece a las normales del OBJ. Nunca apagar la constante
    // global: tambien iluminaria al rival nativo y a otros modelos de la pelea.
    const bool cel = g_obj_cel.load() && dbz3::ingame::BattleNow();
    static bool cel_anterior = false;
    if (cel && !cel_anterior)
      REXLOG_INFO("dbz3 OBJ cel: mascara propia del modelo; brillo HD nativo intacto (escala {})",
                  rex::cvar::Query<double>("dbz3_rim_light_scale"));
    cel_anterior = cel;
    if (NowMs() - g_tick_ms.load() > 250 || dbz3::GameDimmed() || dbz3::OverlayOpen()) return;
    dbz3::t3cb::Draw(io);
    const S::Area a = S::GameArea(io);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (REXCVAR_GET(dbz3_combate_aviso)) {
      for (int k = 0; k < 2; ++k) {
        static const int64_t retener = std::getenv("DBZ3_COMBATE_AVISO_PRUEBA")
                                           ? std::atoll(std::getenv("DBZ3_COMBATE_AVISO_PRUEBA")) : 0;
        int f = g_aviso[k].load();
        if (!f && retener && NowMs() - g_aviso_ms[k].load() < retener) f = 1;   // pruebas: se queda en la captura
        if (!f || !g_humano[k].load()) continue;
        // bajo la barra de vida de ese lado: el boton de la guardia del juego y un aro que se cierra hasta el golpe
        const ImVec2 c = a.At(k == 0 ? 165.0f : 1035.0f, 190.0f);
        const float t = float(f) / float(kAvisoCuadros);
        dl->AddCircle(c, (22.0f + 40.0f * t) * a.s, IM_COL32(255, 206, 60, int(255 * (1.0f - 0.6f * t))), 32,
                      4.0f * a.s);
        S::Glyph(dl, "A", ImVec2(c.x - 17.0f * a.s, c.y), 34.0f * a.s);
      }
    }
    // reglas activas: una linea pequena abajo (el jugador sabe que no es el combate original)
    std::string r;
    if (Rules()) {
      if (Competitivo()) r += dbz3::i18n::T("MODO COMPETITIVO  ", "COMPETITIVE MODE  ");
      if (REXCVAR_GET(dbz3_combate_reglas_v0)) r += dbz3::i18n::T("REGLAS V0  ", "RULES V0  ");
      if (REXCVAR_GET(dbz3_combate_definitiva_directa)) r += dbz3::i18n::T("DEFINITIVA DIRECTA  ", "DIRECT ULTIMATE  ");
      if (CancelButton()) r += dbz3::i18n::T("CANCELACION ", "CANCEL ") + std::string(CancelButton() == 0x400 ? "LB  " : "RB  ");
      if (REXCVAR_GET(dbz3_combate_reversion) > 0) r += dbz3::i18n::T("PIERDE FORMA  ", "FORM LOSS  ");
    }
    // se ve 4 s al empezar cada pelea y al cambiar una regla (no tapa las definitivas ni las escenas)
    static std::string ultima;
    static int64_t hasta = 0;
    const int64_t ahora = NowMs();
    if (r != ultima) ultima = r, hasta = ahora + 4000;
    hasta = std::max(hasta, g_inicio_ms.load() + 4000);
    // DS138: tope de ancho (1080 u de 1200) para que las reglas no se salgan del area con idiomas largos
    if (!r.empty() && ahora < hasta)
      S::MenuText(dl, a.At(600.0f, 868.0f), 22.0f * a.s, r.c_str(), false, false, 1080.0f * a.s);
  }
};

void CrearPanel() {
  static CombatePanel* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new CombatePanel(rt->imgui_drawer());
}

}  // namespace dbz3::combate

// IWFIN/HUECOS-08: palabras de control por jugador de IW (EU 0x22F960: palabra[n] += 1, 32 por jugador en
// *(0x49CA70) + jugador * 0x40 + 0x8C1C). El rango de las misiones (Char_ComputeStatGrade EU 0x2AC0D0) lee cuatro que el
// port no media: 1 (+0x30) al entrar en la accion 0x26 (esquiva, EU 0x2165F0), 2 (+0x2C) en la 0x27 (teletransporte, EU
// 0x215E30), 3 (+0x34) en la 0x1E (EU 0x218580) y 16 (+0x20) en la 0x28 (ataque con teletransporte, EU 0x2157B0). B3 HD es
// el mismo motor: las mismas cuatro funciones escriben la misma accion en X+3244 (US sub_82107DB0, sub_8210D620,
// sub_82107380 y sub_82108098), pero no llevan la cuenta; aqui se lleva, una por llamada como el original.
namespace dbz3::combate {
namespace {
std::atomic<int> g_palabra_iw[2][4] = {};
void SumarPalabraIW(uint8_t* base, uint32_t x, int k) {
  const int lado = SideOf(base, x);
  if (lado < 0) return;
  const int n = ++g_palabra_iw[lado][k];
  static const bool traza = std::getenv("DBZ3_TEST_PALABRAS_IW") != nullptr;   // solo pruebas
  if (traza && n <= 12) REXLOG_INFO("dbz3 combate: palabra IW {} del lado {} = {} (accion {:#x})", k == 3 ? 16 : k + 1, lado + 1, n, REX_LOAD_U16(x + kAct));
}
}  // namespace
int PalabraIW(int lado, int k) { return lado >= 0 && lado < 2 && k >= 0 && k < 4 ? g_palabra_iw[lado][k].load() : 0; }
void PalabrasIWCero() {
  for (auto& l : g_palabra_iw)
    for (auto& v : l) v = 0;
}
}  // namespace dbz3::combate
DBZ3_HOOK(sub_82107DB0, dbz3eu_sub_82107EB0) {   // accion 0x26 -> palabra 1
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  const bool valido = SideOf(base, x) >= 0;
  const bool comp = valido && Competitivo(), burst = comp && Burst(base, x);
  static const bool prueba = std::getenv("DBZ3_TEST_COMP_ESQUIVA") != nullptr;
  const float antes = valido && prueba ? LoadF(base, x + 6120) : 0.0f;
  orig(ctx, base);
  SumarPalabraIW(base, x, 0);
  // IW PAL 0x21697C..0x216998 (cola de 0x2165F0): 50 de fatiga por esquiva, solo fuera del Burst.
  // La pareja B3 termina sin esa llamada. Banderas 0: suma/limita, no pide aturdimiento inmediato.
  if (comp && !burst) {
    PPCContext llamada = ctx;
    llamada.r3.u64 = x, llamada.r5.u64 = 0, llamada.f1.f64 = 50.0;
    sub_821194E0(llamada, base);
  }
  if (valido && prueba)
    REXLOG_INFO("dbz3 combate: COMP prueba esquiva: lado {}, modo {}, Burst {}, fatiga {:.2f} -> {:.2f}",
                SideOf(base, x) + 1, comp, burst, antes, LoadF(base, x + 6120));
}
DBZ3_HOOK(sub_8210D620, dbz3eu_sub_8210D720) {   // accion 0x27 -> palabra 2
  const uint32_t x = ctx.r3.u32;
  orig(ctx, base);
  dbz3::combate::SumarPalabraIW(base, x, 1);
}
DBZ3_HOOK(sub_82107380, dbz3eu_sub_82107478) {   // accion 0x1E -> palabra 3
  const uint32_t x = ctx.r3.u32;
  orig(ctx, base);
  dbz3::combate::SumarPalabraIW(base, x, 2);
}
DBZ3_HOOK(sub_82108098, dbz3eu_sub_82108198) {   // accion 0x28 -> palabra 16
  const uint32_t x = ctx.r3.u32;
  orig(ctx, base);
  dbz3::combate::SumarPalabraIW(base, x, 3);
}

// SONDA (DBZ3_TEST_BANCOS=1): cada carga de banco de efectos (sub_8209E650: r4 = descriptor en memoria del juego)
DBZ3_HOOK_EU(sub_8209E650) {
  static const bool bancos = std::getenv("DBZ3_TEST_BANCOS") != nullptr;
  if (bancos) {
    const uint32_t d = ctx.r4.u32;
    std::string s;
    for (uint32_t o = 0; o < 28; o += 4) { char t[16]; std::snprintf(t, sizeof t, " %08X", REX_LOAD_U32(d + o)); s += t; }
    const uint32_t bp = REX_LOAD_U32(d + 4), sp = REX_LOAD_U32(d + 12);
    REXLOG_INFO("dbz3 combate: carga banco lr={:08X} slot {} desc{} bank@{:08X}:{:08X} sq@{:08X}:{:08X}", uint32_t(ctx.lr), REX_LOAD_U32(d), s,
                bp, bp ? REX_LOAD_U32(bp) : 0u, sp, sp ? REX_LOAD_U32(sp) : 0u);
  }
  orig(ctx, base);
}

// SONDA (DBZ3_TEST_BANCOS=1): estado de las 16 ranuras de bancos de efectos (9320 B cada una, +1564 = 3 cargada, +1560 = handle) cada 300
// llamadas al vigilante del gestor (sub_8209DA08, vivo tambien fuera del combate: sirve para ver que no queda nada despues)
DBZ3_HOOK_EU(sub_8209DA08) {
  static const bool bancos = std::getenv("DBZ3_TEST_BANCOS") != nullptr;
  static uint32_t cuadros = 0;
  static std::string ultimo;
  if (bancos) {
    ++cuadros;
    if (const uint32_t g = REX_LOAD_U32(dbz3::GA(0x82386260))) {
      std::string s;
      for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t e = g + 808 + i * 9320;
        char t[48];
        std::snprintf(t, sizeof t, " %u:%u/%X/%X", i, REX_LOAD_U32(e + 1564), REX_LOAD_U32(e + 1560), REX_LOAD_U32(e + 1568) != 0);
        s += t;
      }
      if (s != ultimo || cuadros % 600 == 0) REXLOG_INFO("dbz3 combate: bancos de efectos{}", s);   // al cambiar y cada 10 s
      ultimo = s;
    }
  }
  orig(ctx, base);
}

// SONDA (DBZ3_TEST_BANCOS=1): liberacion de todas las ranuras del gestor al acabar el combate (sub_8209D988, solo si +1540 != 0)
DBZ3_HOOK_EU(sub_8209D988) {
  static const bool bancos = std::getenv("DBZ3_TEST_BANCOS") != nullptr;
  auto estado = [&]() {
    std::string s;
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t e = ctx.r3.u32 + i * 9320;
      char t[32];
      std::snprintf(t, sizeof t, " %u:%d", i, REX_LOAD_U32(e + 1568) != 0 ? 1 : 0);
      s += t;
    }
    return s;
  };
  const uint32_t mgr = ctx.r3.u32;
  const bool activa = bancos && REX_LOAD_U8(mgr + 1540) != 0;
  if (activa) REXLOG_INFO("dbz3 combate: liberacion de bancos: antes (copia en memoria){}", estado());
  orig(ctx, base);
  if (activa) REXLOG_INFO("dbz3 combate: liberacion de bancos: despues{}", estado());
}

// Sonidos pedidos desde otros hilos (Dragon Tactics): una cola de 16 (un productor, el hilo de la interfaz; un consumidor, este gancho) que
// suena por el gestor de efectos del propio juego (sub_820922A0: banco e id, como los de un combate normal). Como mucho 4 por cuadro;
// si la cola se llena, el sonido nuevo se descarta (la mezcla de Tactics ya limita lo que pide).
namespace {
constexpr uint32_t kColaSe = 16, kSeCuadro = 4;
std::atomic<uint32_t> g_cola_se[kColaSe], g_se_in{0}, g_se_out{0};

// VOCES PROPIAS (Dragon Tactics, H7): el combate carga en las ranuras 3 y 4 del gestor de efectos el banco de gritos de sus dos
// luchadores (sub_821221F8: fid lang = char372 +0x24 | 0x30000, nodo AFS en el hilo de E/S; sub_821226A8 lo parte en los hijos del #AMB y
// sub_8209E938 arma el descriptor de sub_8209E650). Aqui se repite lo mismo para otros personajes en las ranuras libres 5..15: pedir el
// fichero (sub_82083968), esperar su nodo, descriptor {ranura, hijo0, hijo1, 0, tam0, tam1, -1} y sub_8209E650. De una en una: el gestor
// solo vigila (sub_8209DA08) la ultima ranura cargada (+1544). Lo que ya no se pide se suelta (como sub_8209D988, pero por ranura).
constexpr uint32_t kRanuraIni = 5, kRanuras = 11, kSlotSz = 9320;
constexpr dbz3::GA kGestorPtr = 0x82386260, kChar372Voz = 0x82329CF0;
std::atomic<uint64_t> g_voces_mask{0};
std::atomic<int8_t> g_voz_slot[64];
struct RanuraVoz { int ch = -1; int estado = 0; uint32_t nodo = 0; uint32_t desde = 0; };   // estado: 0 libre, 1 leyendo, 2 cargando, 3 lista
RanuraVoz g_rv[kRanuras];
uint32_t g_rv_cuadro = 0;
const bool g_voz_init = [] { for (auto& s : g_voz_slot) s.store(-1); return true; }();

uint32_t Gcall(PPCContext& ctx, uint8_t* base, Dbz3GuestFn f, uint32_t a, uint32_t b = 0, uint32_t c = 0) {
  const PPCContext guardado = ctx;
  ctx.r3.u64 = a, ctx.r4.u64 = b, ctx.r5.u64 = c;
  f(ctx, base);
  const uint32_t r = ctx.r3.u32;
  ctx = guardado;
  return r;
}

void SoltarRanuraVoz(PPCContext& ctx, uint8_t* base, uint32_t k) {
  RanuraVoz& r = g_rv[k];
  const uint32_t g = REX_LOAD_U32(kGestorPtr);
  if (r.estado == 1 && r.nodo) Gcall(ctx, base, DBZ3_CALL_EU(sub_82083D08), r.nodo, 0);   // pedido que no llego a usarse
  if (r.estado >= 2 && g) {
    const uint32_t e = g + 808 + (kRanuraIni + k) * kSlotSz;
    if (const uint32_t m = REX_LOAD_U32(e + 1568)) {   // (si el fin del combate ya la soltio, +1568 vale 0: nada que hacer)
      if (const uint32_t h = REX_LOAD_U32(e + 1560)) Gcall(ctx, base, DBZ3_CALL_EU(sub_822EB318), h);
      Gcall(ctx, base, DBZ3_CALL_EU(sub_8221CED8), m);
    }
    if (const uint32_t q = REX_LOAD_U32(e + 1576)) {
      Gcall(ctx, base, DBZ3_CALL_EU(sub_820A1B50), q);
      Gcall(ctx, base, DBZ3_CALL_EU(sub_8221CED8), q);
    }
    REX_STORE_U32(e + 1560, 0), REX_STORE_U32(e + 1564, 0), REX_STORE_U32(e + 1568, 0), REX_STORE_U32(e + 1576, 0);
  }
  REXLOG_INFO("dbz3 combate: voz: personaje {} suelta (ranura {}, estado {})", r.ch, kRanuraIni + k, r.estado);
  if (r.ch >= 0 && r.ch < 64) g_voz_slot[r.ch].store(-1);
  r = RanuraVoz{};
}

void VocesCuadro(PPCContext& ctx, uint8_t* base) {
  ++g_rv_cuadro;
  const uint32_t g = REX_LOAD_U32(kGestorPtr);
  const uint64_t pedidas = g_voces_mask.load();
  if (!g) {   // el gestor ya no existe (fin del combate): todo lo suyo se fue con el
    for (RanuraVoz& r : g_rv) if (r.estado) { if (r.ch >= 0) g_voz_slot[r.ch].store(-1); r = RanuraVoz{}; }
    return;
  }
  // 1) lo que ya no se pide se suelta; lo que el fin del combate soltio (handle a 0) se olvida
  for (uint32_t k = 0; k < kRanuras; ++k) {
    RanuraVoz& r = g_rv[k];
    if (!r.estado) continue;
    const uint32_t e = g + 808 + (kRanuraIni + k) * kSlotSz;
    if (r.estado >= 2 && !REX_LOAD_U32(e + 1568)) {
      REXLOG_INFO("dbz3 combate: voz: personaje {} perdida con el combate (ranura {})", r.ch, kRanuraIni + k);
      g_voz_slot[r.ch].store(-1);
      r = RanuraVoz{};
      continue;
    }
    if (!(pedidas >> r.ch & 1)) SoltarRanuraVoz(ctx, base, k);
  }
  // 2) la que se esta cargando: esperar el nodo, cargar, esperar el estado 3 del gestor
  bool ocupado = int32_t(REX_LOAD_U32(g + 808 + 1544)) >= 0;   // sub_8209DA08 aun vigila una carga
  for (uint32_t k = 0; k < kRanuras; ++k) {
    RanuraVoz& r = g_rv[k];
    const uint32_t e = g + 808 + (kRanuraIni + k) * kSlotSz;
    if (r.estado == 2) {
      if (REX_LOAD_U32(e + 1564) == 3) {
        r.estado = 3;
        g_voz_slot[r.ch].store(int8_t(kRanuraIni + k));
        REXLOG_INFO("dbz3 combate: voz: personaje {} lista en la ranura {} ({} cuadros)", r.ch, kRanuraIni + k, g_rv_cuadro - r.desde);
      } else if (g_rv_cuadro - r.desde > 600) {
        REXLOG_WARN("dbz3 combate: voz: personaje {} no llego a estado 3 (ranura {})", r.ch, kRanuraIni + k);
        SoltarRanuraVoz(ctx, base, k);
      }
    } else if (r.estado == 1 && !ocupado) {
      const uint32_t nodo = r.nodo, data = nodo ? REX_LOAD_U32(nodo + 192) : 0;
      if (!nodo || REX_LOAD_U8(nodo + 1) || !data || data == 0xFFFFFFFFu) {
        if (g_rv_cuadro - r.desde > 1200) {
          REXLOG_WARN("dbz3 combate: voz: personaje {}: el fichero no llega", r.ch);
          SoltarRanuraVoz(ctx, base, k);
        }
        continue;
      }
      const uint32_t raiz = REX_LOAD_U32(data + 208);
      // Ronda 10/11: con un mod, el banco de voces puede no traer raiz de escena (data+208 = 0) y
      // sub_820800A8(NULL) lee la direccion 1 (access violation al empezar el combate). Sin raiz no hay
      // escena que instanciar: se salta esta voz (las demas del personaje siguen) en vez de cerrar el juego.
      if (!raiz) {
        static std::atomic<int> avisos{0};
        if (avisos.fetch_add(1) < 4)
          REXLOG_WARN("dbz3 combate: voz: personaje {} sin raiz de escena en el banco {:08X}: se salta la voz", r.ch, data);
        SoltarRanuraVoz(ctx, base, k);
        continue;
      }
      Gcall(ctx, base, DBZ3_CALL_EU(sub_820800A8), raiz);
      const uint32_t c0 = Gcall(ctx, base, DBZ3_CALL_EU(sub_820B0CC8), raiz, 0);
      const uint32_t c1 = Gcall(ctx, base, DBZ3_CALL_EU(sub_820B0CC8), raiz, 1);
      const uint32_t sp = ctx.r1.u32;
      ctx.r1.u32 = sp - 256;
      const uint32_t d = ctx.r1.u32 + 32;
      REX_STORE_U32(d + 0, kRanuraIni + k), REX_STORE_U32(d + 4, REX_LOAD_U32(c0)), REX_STORE_U32(d + 8, REX_LOAD_U32(c1)), REX_STORE_U32(d + 12, 0);
      REX_STORE_U32(d + 16, REX_LOAD_U32(c0 + 4)), REX_STORE_U32(d + 20, REX_LOAD_U32(c1 + 4)), REX_STORE_U32(d + 24, 0xFFFFFFFFu);
      REXLOG_INFO("dbz3 combate: voz: personaje {} -> ranura {}: banco {:08X} ({:X} B) nombres {:08X} ({:X} B)", r.ch, kRanuraIni + k,
                  REX_LOAD_U32(d + 4), REX_LOAD_U32(d + 16), REX_LOAD_U32(d + 8), REX_LOAD_U32(d + 20));
      Gcall(ctx, base, DBZ3_CALL_EU(sub_8209E650), g + 808, d);
      ctx.r1.u32 = sp;
      Gcall(ctx, base, DBZ3_CALL_EU(sub_82083D08), nodo, 0);   // el banco ya esta copiado: el fichero se va con su nodo
      r.nodo = 0, r.estado = 2, r.desde = g_rv_cuadro;
      ocupado = true;
    }
  }
  // 3) una peticion nueva por cuadro, si no hay otra a medias
  for (const RanuraVoz& r : g_rv) if (r.estado == 1 || r.estado == 2) return;
  for (int ch = 0; ch < 64; ++ch) {
    if (!(pedidas >> ch & 1) || g_voz_slot[ch].load() >= 0) continue;
    uint32_t libre = kRanuras;
    for (uint32_t k = 0; k < kRanuras; ++k) if (!g_rv[k].estado) { libre = k; break; }
    if (libre == kRanuras) return;   // sin ranuras: los demas se quedan con la voz del banco por defecto
    const uint32_t fid = REX_LOAD_U32(kChar372Voz + 372 * uint32_t(ch) + 0x24);
    if (fid == 0xFFFFFFFFu) continue;
    const uint32_t nodo = Gcall(ctx, base, DBZ3_CALL_EU(sub_82083968), fid | 0x30000u, 0, 1);
    if (nodo == 0xFFFFFFFFu || !nodo) return;
    g_rv[libre] = RanuraVoz{ch, 1, nodo, g_rv_cuadro};
    REXLOG_INFO("dbz3 combate: voz: personaje {} pide lang {} (nodo {:08X}) para la ranura {}", ch, fid, nodo, kRanuraIni + libre);
    return;
  }
}

void SonarPedidos(PPCContext& ctx, uint8_t* base) {
  VocesCuadro(ctx, base);
  for (uint32_t n = 0; n < kSeCuadro && g_se_out.load() != g_se_in.load(std::memory_order_acquire); ++n) {
    const uint32_t v = g_cola_se[g_se_out.load() % kColaSe].load();
    g_se_out.fetch_add(1);
    const PPCContext guardado = ctx;
    if ((v >> 16) == 0) {   // subTAC: banco del sistema = el del menu; su volumen se fija antes (sub_82092570, como PlaySe de hub_menu.cpp) o el efecto abre voz pero no suena
      constexpr dbz3::GA kJuego = 0x824BA110, kSePiso = 0x8201F60C;
      constexpr dbz3::GA kSeVol = kJuego + 0x2C0C0;   // volumen de efectos y su minimo (los de hub_menu.cpp)
      auto cf = [&](uint32_t a) { const uint32_t u = REX_LOAD_U32(a); float f; std::memcpy(&f, &u, 4); return f; };
      const float vol = cf(kSeVol);
      ctx.r3.u64 = 0, ctx.r4.u64 = 0, ctx.f1.f64 = vol;
      DBZ3_CALL_EU(sub_82092570)(ctx, base);
      ctx = guardado;
      if (!(vol > cf(kSePiso))) continue;
    }
    ctx.r3.u64 = v >> 16;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = v & 0xFFFF;
    ctx.r6.u64 = 0;
    DBZ3_CALL_EU(sub_820922A0)(ctx, base);
    if (static const bool sonda = dbz3::tactics::EnvPrueba("PRUEBA") != nullptr; sonda && (v >> 16) == 0) {   // subTAC: sonda de las voces del gestor tras pedir un efecto del banco 0 (misma lectura que hub_menu.cpp)
      const uint32_t sm = REX_LOAD_U32(dbz3::GA(0x82386260)) + 808;
      int voces = 0, mias = 0;
      for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t w = sm + 8 + 24 * i;
        if (!REX_LOAD_U8(w + 8)) continue;
        ++voces;
        mias += REX_LOAD_U32(w - 8) == 0 && REX_LOAD_U32(w - 4) == (v & 0xFFFF);
      }
      REXLOG_INFO("dbz3 combate: prueba: efecto de sistema {} pedido (voces {}, suyas {})", v & 0xFFFF, voces, mias);
    }
    ctx = guardado;
  }
}

// DR01 (#63): el Rush devuelve al atacante al borde al salir de la cinematica (109,7 u medidos con el personaje
// nativo; docs/ENTREGAS/INFORME_tPEND.md). La camara del juego solo encuadra a los dos y por eso se ve el fondo
// lejos. Se guarda la posicion de suelo (X+48/+52/+56) al entrar en el Rush (codigos 0x400..0x47F) y, al salir,
// si el atacante (0x400..0x43F) aparece a mas de 20 u se devuelve durante 12 fotogramas (el tiron del motor es de
// un fotograma; la victima no se toca). Apagado = original; nunca actua en el online.
struct RushSitio { bool dentro = false, atacante = false, guardado = false; float x = 0, z = 0; int vuelta = 0; };
void RushSinSalto(uint8_t* base) {
  using namespace dbz3::combate;
  if (!REXCVAR_GET(dbz3_rush_sin_salto) || dbz3::net::Session::Get().active()) return;   // nunca en online
  static RushSitio sitio[2];
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, int(k));
    if (!x) continue;
    const uint32_t code = REX_LOAD_U32(x + kCode);
    const bool dentro = code >= 0x400u && code < 0x480u;
    if (dentro) {
      if (!sitio[k].guardado) {   // una sola vez por Rush: el sitio de antes de entrar
        sitio[k].guardado = true;
        sitio[k].x = LoadF(base, x + 48);
        sitio[k].z = LoadF(base, x + 56);
      }
      sitio[k].dentro = true;
      sitio[k].atacante = code < 0x440u;   // el remate (0x440..0x47F es la victima: el juego la devuelve bien)
    } else if (sitio[k].dentro) {
      sitio[k].dentro = false;
      sitio[k].vuelta = sitio[k].atacante ? 12 : 0;
      if (!sitio[k].vuelta) sitio[k].guardado = false;
    }
    if (sitio[k].vuelta <= 0) continue;
    if ((code >= 0x400u && code < 0x500u) || code == 0x23Eu) continue;   // otra cinemática: no tocar
    if (--sitio[k].vuelta <= 0) sitio[k].guardado = false;
    const float px = LoadF(base, x + 48), pz = LoadF(base, x + 56);
    const float dx = px - sitio[k].x, dz = pz - sitio[k].z;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d <= 20.0f) continue;
    StoreF(base, x + 48, sitio[k].x);
    StoreF(base, x + 56, sitio[k].z);
    static std::atomic<int> avisos{0};
    if (avisos.fetch_add(1) < 4)
      REXLOG_INFO("dbz3 combate: DR01: el lado {} vuelve a su sitio tras el Rush (estaba a {:.1f} u)", k + 1, d);
  }
}
}  // namespace
namespace dbz3::combate {
void CargarVoces(uint64_t mascara) { g_voces_mask.store(mascara); }
int SlotVoz(int personaje) { return personaje >= 0 && personaje < 64 ? int(g_voz_slot[personaje].load()) : -1; }
void PedirSonido(int banco, int id) {
  const uint32_t in = g_se_in.load();
  if (in - g_se_out.load() >= kColaSe) return;
  g_cola_se[in % kColaSe].store((uint32_t(banco) << 16) | uint32_t(id));
  g_se_in.store(in + 1, std::memory_order_release);
}
}  // namespace dbz3::combate

// Paso de pelea de los dos luchadores (sub_821100B0: entradas, golpes, acciones; sin otro gancho en src/).
DBZ3_HOOK_EU(sub_821100B0) {
  using namespace dbz3::combate;
  {
    const uint32_t st = BattleState(base);
    // Prueba visual por mando NATIVO, sin imponer acciones, animaciones ni ki.
    // Formato: cuadro de inicio,duracion,botones XInput. Solo entorno de prueba,
    // nunca online; el resultado se acredita con la sonda de acciones y ki.
    if (static const char* pad_test = std::getenv("DBZ3_TEST_OBJ_PAD"); pad_test && !dbz3::net::Session::Get().active()) {
      static uint32_t cuadro = 0;
      static int usuario_activo = -1;
      unsigned inicio = 600, duracion = 600, botones = 0xA000, doble = 0;
      const bool valido = std::sscanf(pad_test, "%u,%u,%u,%u", &inicio, &duracion, &botones, &doble) >= 3 && botones <= 0xFFFF;
      if (st == 4 && valido) ++cuadro;
      else cuadro = 0;
      const uint32_t x = Fighter(base, 0);
      const int slot = x ? int16_t(REX_LOAD_U16(x + kPadIdx)) : -1;
      const bool pulsar = valido && st == 4 && cuadro >= inicio && cuadro - inicio < duracion && slot >= 0 && slot < 4;
      if (pulsar) {
        const uint32_t delta = cuadro - inicio;
        unsigned actuales = doble == 1 && delta >= 3 && delta < 6 ? 0 : botones;
        if (doble == 2) {   // inventario finito de combinaciones; no son acciones impuestas
          const unsigned grupo = delta / 240, paso = delta % 240;
          const unsigned prueba = grupo < 15 ? (grupo + 1) << 12 : grupo == 15 ? 0x1004 : 0x1008;
          actuales = paso >= 180 ? 0 : prueba;
          if (grupo == 0 && paso >= 3 && paso < 6) actuales = 0;
          if (grupo >= 15 && (paso < 3 || (paso >= 6 && paso < 9))) actuales = 0x1000;
          if (!paso) REXLOG_INFO("dbz3 prueba OBJ: combinacion {} botones {:04X}", grupo, prueba);
        }
        dbz3::net::InyectarPad(uint32_t(slot), int32_t(actuales));
        if (usuario_activo < 0)
          REXLOG_INFO("dbz3 prueba OBJ: mando nativo inicio usuario {} botones {:04X}", slot, botones);
        usuario_activo = slot;
      } else if (usuario_activo >= 0) {
        dbz3::net::InyectarPad(uint32_t(usuario_activo), -1);
        usuario_activo = -1;
        REXLOG_INFO("dbz3 prueba OBJ: mando nativo fin");
      }
    }
    bool obj_cel = false;
    if (st >= 3 && st <= 7) {
      for (int k = 0; k < 2; ++k) {
        const uint32_t x = Fighter(base, k);
        if (x && dbz3::roster::ObjCel(REX_LOAD_U32(x + kId))) obj_cel = true;
      }
    }
    g_obj_cel.store(obj_cel);
    static uint32_t last = 0;
    static uint32_t pelea = 0;
    if (st != last && st == 3) g_frame = 0, ++pelea;   // READY: combate nuevo (la revancha tambien)
    if (st != last && SondaOn()) REXLOG_INFO("dbz3 combate: pelea {}, estado {}", pelea, st);
    if (st != last && st == 4 && last == 3) PruebaInicio(base), g_inicio_ms = NowMs();
    // PRUEBA (DBZ3_COMBATE_CAPS="c1,c2,...", hasta 7): capsulas del lado del teclado al empezar cada pelea (el modo 1,
    // el del Duelo con presentacion, no usa DBZ3_TEST_CAPS del HUB)
    if (static const char* caps = std::getenv("DBZ3_COMBATE_CAPS"); caps && st != last && st == 3) {
      int c[7] = {};
      const int n = std::max(0, std::sscanf(caps, "%d,%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]));
      if (const uint32_t sel = REX_LOAD_U32(Fighter(base, g_lados2 ? 1 : 0) + 7412)) {
        REX_STORE_U16(sel + 52, uint16_t(n));
        for (int i = 0; i < 7; ++i) REX_STORE_U16(sel + 54 + 2 * uint32_t(i), uint16_t(i < n ? c[i] : 0));
        REXLOG_INFO("dbz3 combate: prueba: {} capsulas para el lado {} ({})", n, g_lados2 ? 2 : 1, caps);
      }
    }
    last = st;
    g_frame.fetch_add(1);
    g_tick_ms = NowMs();
    SonarPedidos(ctx, base);
    dbz3::survivors::fxreal::Paso(ctx, base);
    static bool panel = false;
    if (!panel) panel = true, dbz3::launcher::DeferUi(&CrearPanel);
    MunecoCuadro(base, st);
    OndasRival(base, st);
    if (st == 4) Avisos(base), PruebaCuadro(ctx, base);
    if (st >= 4 && st <= 7) DefinitivaLog(base);
    OtroDano(ctx, base, st);
    Sonda(base, st);
    Volcado(base);
  }
  PublicarComp(base);   // Retirar marcas antes de que el juego procese Hiper al apagar COMP.
  if(!dbz3::t3cb::Frozen()) orig(ctx, base);
  dbz3::t3cb::Tick(ctx, base);
  PublicarComp(base);   // R16: instantanea atomica para el aura/HUD, tambien tras resimular un cuadro
  dbz3::comp::aura::Actualizar(base);
  if (const uint32_t st = BattleState(base); st >= 4 && st <= 7) RushSinSalto(base);   // DR01 (#63)
}

// Golpe recibido (sub_8210ED48, r3 = victima): 05 dano del bloque HR, 18 reversion, sonda de golpes.
DBZ3_HOOK_EU(sub_8210ED48) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  const int lado = SideOf(base, x);
  if (lado < 0) {
    orig(ctx, base);
    return;
  }
  const uint32_t from = REX_LOAD_U32(x + kHitFrom);
  const int de = from ? SideOf(base, from) : -1;   // -1: proyectil u otro objeto (sin reglas 05)
  const uint32_t hr = REX_LOAD_U32(x + kHitHr);
  const int16_t pend = int16_t(REX_LOAD_U16(x + kHitPending));
  const bool muneco = pend && EsMuneco(base, x);
  const float hp0 = LoadF(base, x + kHp), ki0 = LoadF(base, x + kKi);
  const uint32_t act0 = REX_LOAD_U16(x + kAct), aid = de >= 0 ? REX_LOAD_U32(from + kId) : 0xFFFF;
  int hr_code = -1;
  if (hr && de >= 0) {
    const uint32_t csk = REX_LOAD_U32(from + kCsk);
    const uint32_t hr0 = csk ? REX_LOAD_U32(csk + 28) : 0;
    if (hr0 && hr >= hr0 && (hr - hr0) % 128 == 0) hr_code = int((hr - hr0) / 128);
  }
  // 05: el bloque HR solo cambia durante esta llamada; despues queda byte a byte como estaba
  std::array<uint8_t, 128> guardado{};
  bool parcheado = false;
  if (pend && hr_code >= 0 && REXCVAR_GET(dbz3_combate_reglas_v0) && Rules()) {
    for (const Regla& r : kReglasV0) {
      if (r.id != aid || int(r.hr) != hr_code || int16_t(REX_LOAD_U16(hr)) != r.clasico) continue;
      std::memcpy(guardado.data(), base + hr, 128);
      for (uint32_t l = 0; l < 8; ++l)
        if (int16_t(REX_LOAD_U16(hr + 16 * l)) == r.clasico) REX_STORE_U16(hr + 16 * l, uint16_t(r.v0));
      parcheado = true;
    }
  }
  const int16_t dano_hr = hr ? int16_t(REX_LOAD_U16(hr)) : 0;
  const uint32_t code = de >= 0 ? REX_LOAD_U32(from + kCode) : 0;
  const float cf = de >= 0 ? LoadF(base, from + kAnimFrame) : 0.0f;
  orig(ctx, base);
  const uint32_t ret = ctx.r3.u32;
  if (parcheado) {
    std::memcpy(base + hr, guardado.data(), 128);
    if (std::memcmp(base + hr, guardado.data(), 128) != 0) REXLOG_WARN("dbz3 combate: 05 bloque HR no restaurado");
  }
  const float hp1 = LoadF(base, x + kHp);
  const float dano = hp0 - hp1;
  g_dano_gancho[size_t(lado)] += dano;
  // 18: un golpe fuerte devuelve a la forma normal (no con KO; el relevo del Tag Team manda)
  const int umbral = REXCVAR_GET(dbz3_combate_reversion);
  if (umbral > 0 && Rules() && pend && dano >= float(umbral)) {
    const uint32_t act1 = REX_LOAD_U16(x + kAct);
    const char* no = hp1 <= 0.0f                      ? "KO (manda el KO)"
                     : REX_LOAD_U32(x + kForm) == 0   ? "ya en la forma normal"
                     : act0 == 15 && act1 == 15       ? "armadura (el golpe no lo para)"
                     : dbz3::tag::Hud(lado).switching ? "relevo del Tag Team en curso"
                                                      : nullptr;
    if (no)
      REXLOG_INFO("dbz3 combate: 18 golpe de {:.0f} al lado {} (umbral {}): no revierte: {}", dano, lado + 1, umbral, no);
    else
      Revertir(ctx, base, x, dano);
  }
  if (muneco) {
    const uint32_t act1 = REX_LOAD_U16(x + kAct);
    const char* que = act1 == 39 ? "teletransporte" : act0 == 1 || (dano_hr > 0 && dano < 0.75f * dano_hr) ? "guardia" : "golpe";
    REXLOG_INFO("dbz3 combate: muneco (lado {}, {}): golpe {:X} -> {} ({:.0f} de dano de {}, accion {} -> {})", lado + 1,
                Muneco() & 2 ? "guardia" : "quieto", de >= 0 ? REX_LOAD_U32(from + kCode) : 0, que, dano, dano_hr, act0,
                act1);
  }
  if (g_golpes && pend) {
    std::fprintf(g_golpes, "%u,%s,%d,%d,%u,%X,%.0f,%04X,%d,%d,%.0f,%.0f,%u,%u,%.0f,%.0f,%d,%u\n", g_frame.load(),
                 de >= 0 ? "golpe" : "proyectil", lado + 1, de >= 0 ? de + 1 : 2 - lado, aid,
                 de >= 0 ? code : g_ultima_code[size_t(1 - lado)], cf, REX_LOAD_U16(x + kHitProps), hr_code, int(dano_hr), hp0, hp1, act0,
                 REX_LOAD_U16(x + kAct), ki0, LoadF(base, x + kKi), parcheado ? 1 : 0, ret);
  }
}

// Contraataque por teletransporte del jugador (sub_82102DF8: r3 = victima humana). El juego lo concede si el
// tiempo de la guardia (cuadros desde que se pulso G, I+80) esta entre -5 y 10 sin P/K/E. Solo se mide.
DBZ3_HOOK_EU(sub_82102DF8) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32, i = x + 4960;
  const int32_t tp = int32_t(REX_LOAD_U32(i + 72)), tk = int32_t(REX_LOAD_U32(i + 76));
  const int32_t tg = int32_t(REX_LOAD_U32(i + 80)), te = int32_t(REX_LOAD_U32(i + 84));
  const uint32_t ent = i + 100 + 4 * (REX_LOAD_U32(i + 88) & 0x3F);   // ultima entrada del bufer (tipo, valor)
  orig(ctx, base);
  if (g_golpes) {
    const uint32_t a = REX_LOAD_U32(x + 4948);
    std::fprintf(g_golpes, "%u,ventana,%d,%d,P%d K%d E%d ent %u/%d,%X,%.0f,,,,,,,,,,%d,%u\n", g_frame.load(),
                 SideOf(base, x) + 1, SideOf(base, a) + 1, tp, tk, te, REX_LOAD_U16(ent), int16_t(REX_LOAD_U16(ent + 2)),
                 a ? REX_LOAD_U32(a + kCode) : 0, a ? LoadF(base, a + kAnimFrame) : 0.0f, tg, ctx.r3.u32);
  }
}

// Estado del luchador frente al de una entrada del #CCM (sub_820F7758: r3 = nodo, r4 = X; se llama cada cuadro con
// las entradas posibles). Las definitivas piden el bit 0x8000 (modo hiper, X+8208) en cond2; con la regla 15
// cuentan como si lo tuviera, y la entrada del modo hiper (la misma P+K+G+E, antes en la lista) cede si la
// definitiva se puede pagar. El ki lo cobra despues el juego (sub_820F7588 -> capsula; fuera del hiper se paga).
DBZ3_HOOK_EU(sub_820F7758) {
  using namespace dbz3::combate;
  const uint32_t node = ctx.r3.u32, x = ctx.r4.u32;
  const int lado = !node ? -1 : SideOf(base, x);
  if (lado < 0) {
    orig(ctx, base);
    return;
  }
  const uint16_t c5 = REX_LOAD_U16(node + 10), c6 = REX_LOAD_U16(node + 12);
  const bool ult = (c5 & 0x0008) && (c6 & 0x8000);
  // PRUEBA (DBZ3_TEST_ULT): en modo hiper solo cuentan las entradas P+K+G+E: el pulsar a la vez de la prueba elegia otras (P+K...),
  // tambien con el personaje nativo; hace falta para medir la definitiva heredada
  if (static const bool prueba_ult = std::getenv("DBZ3_TEST_ULT") != nullptr; prueba_ult && lado == 0 && REX_LOAD_U8(x + kHyper) && REX_LOAD_U16(node + 2) != 15) {
    ctx.r3.u64 = 0;
    return;
  }
  if (ult) {
    const Ult u{REX_LOAD_U16(node + 16), REX_LOAD_U16(node + 24), REX_LOAD_U16(node + 26)};
    g_ult[size_t(lado)] = u;
    for (Ult& h : g_ults[size_t(lado)]) {
      if (h.code == u.code && h.aire == u.aire && h.cap == u.cap) break;
      if (!h.code) { h = u; break; }
    }
  }
  if (Competitivo() && BattleState(base) == 4) {   // COMP
    const bool pedida = g_def_pedida[size_t(lado)];
    if (c5 & 0x0400) {
      // IW 0x22AAE0 Battle_IsSpecialStateActive (cond 0x4000 de la entrada L2): fuera del Burst y con una barra
      // (ki >= 1000.0) o con el reloj de ki ilimitado. El "fuera del Burst" ya lo mira B3 (sub_820F7588, X+8208).
      const bool barra = LoadF(base, x + 6720) > 0.0f || LoadF(base, x + kKi) >= 1000.0f;
      bool recien = true;
      for (uint32_t b = 0; b < 4; ++b) recien = recien && int32_t(REX_LOAD_U32(x + kTimers + 4 * b)) == 1;
      if (recien && g_l2[size_t(lado)] && !pedida && !Burst(base, x))
        REXLOG_INFO("dbz3 combate: COMP L2 del lado {} con ki {:.0f}: {}", lado + 1, LoadF(base, x + kKi),
                    barra ? "Aura Burst" : "sin una barra, no entra");
      if (pedida || !g_l2[size_t(lado)] || Burst(base, x) || !barra) {
        ctx.r3.u64 = 0;
        return;
      }
    } else if (ult && PadRecord(base, x) >= 0) {
      // IW 0x200E50: la definitiva (bit 0x0008) no exige estado; aqui solo sale con la marca de Arriba + E (L2 nunca
      // la dispara, tampoco dentro del Burst). El ordenador sigue con su criterio de B3 (hiper y despues definitiva).
      if (!pedida) {
        ctx.r3.u64 = 0;
        return;
      }
      const uint16_t mask = REX_LOAD_U16(x + kMask);
      REX_STORE_U16(x + kMask, uint16_t(mask | 0x8000));
      orig(ctx, base);
      REX_STORE_U16(x + kMask, mask);
      return;
    }
    orig(ctx, base);
    return;
  }
  const bool on = REXCVAR_GET(dbz3_combate_definitiva_directa) && Rules() && REX_LOAD_U8(x + kHyper) == 0 &&
                  BattleState(base) == 4;   // solo en plena pelea (no tras un KO ni en el fin de tiempo)
  if (on && (c5 & 0x0400)) {
    const bool directa = Directa(base, x, lado);
    bool recien = true;   // P, K, G y E recien pulsados (la entrada de los dos nodos): una linea por pulsacion
    for (uint32_t b = 0; b < 4; ++b) recien = recien && int32_t(REX_LOAD_U32(x + kTimers + 4 * b)) == 1;
    if (recien)
      REXLOG_INFO("dbz3 combate: 15 P+K+G+E del lado {} con ki {:.0f} (capsula {} {}equipada, coste {:.0f}): {}",
                  lado + 1, LoadF(base, x + kKi), g_ult[size_t(lado)].cap,
                  Equipada(base, x, g_ult[size_t(lado)].cap) ? "" : "no ", Coste(base, g_ult[size_t(lado)].cap),
                  directa ? "definitiva directa" : "modo hiper (como siempre)");
    if (directa) {
      ctx.r3.u64 = 0;   // el modo hiper no sale: la definitiva va primero
      return;
    }
  }
  if (!on || !ult) {
    orig(ctx, base);
    if (static const bool esp = std::getenv("DBZ3_TEST_ESP") != nullptr; esp && lado == 0 && !ult) {   // PRUEBA: que entradas ve y por que fallan (una linea por cambio)
      static std::map<uint32_t, uint32_t> visto;
      const uint32_t clave = REX_LOAD_U16(node + 16) << 16 | REX_LOAD_U16(node + 26), val = ctx.r3.u32 << 24 | (REX_LOAD_U16(x + kMask) & 0xFFFF);
      if (auto it = visto.find(clave); it == visto.end() || it->second != val) {
        visto[clave] = val;
        REXLOG_INFO("dbz3 combate: prueba esp: entrada tipo {} cod {:X} cap {} c5 {:04X} c6 {:04X} w {} -> {} (estado {:04X}, ki {:.0f}, equipada {}) [{:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X} {:04X}]", REX_LOAD_U16(node + 2),
                    REX_LOAD_U16(node + 16), REX_LOAD_U16(node + 26), c5, c6, REX_LOAD_U16(node + 24), ctx.r3.u32, REX_LOAD_U16(x + kMask), LoadF(base, x + kKi), Equipada(base, x, REX_LOAD_U16(node + 26)),
                    REX_LOAD_U16(node), REX_LOAD_U16(node + 2), REX_LOAD_U16(node + 4), REX_LOAD_U16(node + 6), REX_LOAD_U16(node + 8), REX_LOAD_U16(node + 10), REX_LOAD_U16(node + 12), REX_LOAD_U16(node + 14),
                    REX_LOAD_U16(node + 16), REX_LOAD_U16(node + 18), REX_LOAD_U16(node + 20), REX_LOAD_U16(node + 22), REX_LOAD_U16(node + 24), REX_LOAD_U16(node + 26), REX_LOAD_U16(node + 28), REX_LOAD_U16(node + 30));
      }
    }
    if (static const bool traza = std::getenv("DBZ3_TEST_ULT") != nullptr; traza && ult && lado == 0 && REX_LOAD_U8(x + kHyper)) {
      static uint32_t n = 0;   // PRUEBA: la entrada de la definitiva, si el juego la da por buena
      if (n++ % 120 == 0)
        REXLOG_INFO("dbz3 combate: prueba: entrada de definitiva (capsula {}): resultado {}, condiciones {:04X} {:04X}, estado {:04X}, ki {:.0f}, coste {:.0f}, equipada {}", REX_LOAD_U16(node + 16),
                    ctx.r3.u32, c5, c6, REX_LOAD_U16(x + kMask), LoadF(base, x + kKi), Coste(base, REX_LOAD_U16(node + 16)), Equipada(base, x, REX_LOAD_U16(node + 16)));
    }
    return;
  }
  const uint16_t mask = REX_LOAD_U16(x + kMask);
  REX_STORE_U16(x + kMask, uint16_t(mask | 0x8000));
  orig(ctx, base);
  REX_STORE_U16(x + kMask, mask);
}

// Mando de cada usuario (sub_8208FD58: r3 = usuario 0..3, lee XInput y rellena su registro). 16: el boton de
// cancelacion (LB/RB) deja de hacer su atajo y, al pulsarlo, el registro trae adelante + el boton que da E en ese
// cuadro (como pulsarlos a la vez a mano; mantenerlo no repite).
namespace dbz3::fusion { void PruebaMando(uint8_t* base, uint32_t user); }   // fusion.cpp (prueba)
namespace dbz3::formas { void PruebaMando(uint8_t* base, uint32_t user); }   // transformaciones.cpp (prueba)
#include "diagnostico_var.inl"
DBZ3_HOOK_EU(sub_8208FD58) {
  using namespace dbz3::combate;
  const uint32_t user = ctx.r3.u32;
  if (user == kMunecoPad && MunecoMandoActivo(base)) {
    MunecoMando(base);   // el mando del muneco no se lee: es el virtual
    return;
  }
  dbz3::iwmini::Mando(base, user, false);   // minijuegos de IW (frente P4): palanca relativa a la camara
  dbz3::diagnostico_var::Mando medida(user);
  orig(ctx, base);
  medida.Fin();
  dbz3::iwmini::Mando(base, user, true);
  dbz3::cinematica::Mando(base, user);   // escenas 3D (frente R3)
  if (user > 3 || BattleState(base) != 4) return;
  dbz3::formas::PruebaMando(base, user);   // frente transformaciones: P+K+G de prueba (DBZ3_TEST_PKG)
  dbz3::fusion::PruebaMando(base, user);   // Nova's Mods: definitiva a voluntad (DBZ3_TEST_ULT)
  PruebaRush(base, user);   // PRUEBA (DBZ3_TEST_COMP_RUSH)
  CompMando(base, user);   // COMP: Arriba + E = definitiva
  const int bit = CancelButton();
  if (!bit || !Rules()) return;
  uint32_t x = 0;
  for (int k = 0; k < 2; ++k)
    if (const uint32_t f = Fighter(base, k); f && PadRecord(base, f) == int(user)) x = f;
  if (!x) return;
  const uint32_t r = kPads + 184 * user, m = Mgr(base), pad = REX_LOAD_U16(x + kPadIdx);
  const uint32_t jc = bit == 0x0400 ? 2 : 3;   // L1 / R1
  static bool antes[4] = {};
  const bool ahora = REX_LOAD_U16(r + 12) & bit;
  const bool nuevo = ahora && !antes[user];
  antes[user] = ahora;
  if (!ahora) return;
  // el atajo del boton (P+G / P+K de serie) no sale mientras se usa como cancelacion: el juego lo ve suelto
  REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) & ~bit));
  REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) & ~bit));
  REX_STORE_U32(r + 72 + 4 * jc, uint32_t(-1));
  if (!nuevo) return;
  int je = -1;
  for (uint32_t j = 0; j < 8; ++j)
    if (REX_LOAD_U8(m + (pad + 4382) * 8 + j) == 8) je = int(j);   // boton que solo da E en la configuracion
  if (je < 0) return;
  const bool derecha = LoadF(base, x + kRivalScreenX) >= LoadF(base, x + kScreenX);
  const uint16_t dir = derecha ? 0x0020 : 0x0080;
  const uint16_t eb = uint16_t(0x100u << je);
  REX_STORE_U16(r + 12, uint16_t(REX_LOAD_U16(r + 12) | eb));
  REX_STORE_U16(r + 14, uint16_t(REX_LOAD_U16(r + 14) | eb));
  REX_STORE_U32(r + 72 + 4 * uint32_t(je), 1);
  REX_STORE_U16(r + 108, uint16_t(REX_LOAD_U16(r + 108) | dir));
  REX_STORE_U16(r + 110, uint16_t(REX_LOAD_U16(r + 110) | dir));
  REX_STORE_U8(r + 26, uint8_t(derecha ? 0x7F : 0x81));
  REXLOG_INFO("dbz3 combate: 16 boton de cancelacion: {} + E (lado {}, cuadro {})", derecha ? "derecha" : "izquierda",
              SideOf(base, x) + 1, g_frame.load());
}

// R16: la entrada de animacion comparte el punto de llamada B3, pero NO su estado, filtro, armadura ni aura.
// IW 0x210E40 / 0x22AAE0: entrar con una barra o reloj de ki ilimitado; no volver a entrar mientras esta activo.
// Armadura IW: 0x240220/0x2400E0 abren/consumen X+0xABA desde el flag 4 de la pista de animacion.
// B3 conserva ese contador en (X+2336)+426 = X+2762 (sub_82121698 y sub_8211B738): no se fuerza por estar en Burst.
// La colision respeta la espalda y el flag 0x20 del golpe; X+8208=0 evita la excepcion Hiper del atacante.
// Las pistas IW nuevas 0x200/0x400 no se importan: su mascara 0x70F difiere del 0x10F nativo B3.
REX_HOOK_RAW(sub_82119360) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  if (SideOf(base, x) < 0 || !Competitivo()) { __imp__sub_82119360(ctx, base); return; }
  if (Burst(base, x) || (LoadF(base, x + 6720) <= 0.0f && LoadF(base, x + kKi) < 1000.0f)) return;
  REX_STORE_U32(x + kCompBurst, kCompBurstOn);
  REXLOG_INFO("dbz3 combate: COMP estado propio del lado {} encendido (accion {}, llamada {:08X}, Hiper {})",
              SideOf(base, x) + 1, REX_LOAD_U16(x + kAct), ctx.lr, REX_LOAD_U8(x + kHyper));
  PublicarComp(base);
}
// Los finales de caida, guion y reinicio pasan por aqui. En COMP no se crea ni se destruye el recurso Hiper.
REX_HOOK_RAW(sub_82111A98) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  if (SideOf(base, x) < 0 || !Competitivo()) { __imp__sub_82111A98(ctx, base); return; }
  if (Burst(base, x))
    REXLOG_INFO("dbz3 combate: COMP estado propio del lado {} apagado (accion {}, llamada {:08X})",
                SideOf(base, x) + 1, REX_LOAD_U16(x + kAct), ctx.lr);
  CompApagar(base, x);
  PublicarComp(base);
}
// IW 0x200E50: omite el ki de ranuras especiales (0x30000); el flag 2 comprueba posesion, no ki.
// El contexto no modifica el nodo compartido; conserva los demas filtros, capsulas y la definitiva.
REX_HOOK_RAW(sub_820F7588) {
  using namespace dbz3::combate;
  const uint32_t nodo = ctx.r3.u32, x = ctx.r4.u32;
  const uint32_t previo_nodo = g_comp_nodo, previo_actor = g_comp_actor;
  if (Burst(base, x)) g_comp_nodo = nodo, g_comp_actor = x;
  __imp__sub_820F7588(ctx, base);
  g_comp_nodo = previo_nodo, g_comp_actor = previo_actor;
}
// IW 0x21C530: recuerda si el ataque empezo en Burst (IW +0x2092); no usa el byte 8210 de B3,
// que pertenece a otro mecanismo. El bit 2 comparte nuestra palabra +6116 y viaja en el rollback.
REX_HOOK_RAW(sub_82104C00) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  if (SideOf(base, x) >= 0 && Competitivo())
    REX_STORE_U32(x + kCompBurst, Burst(base, x) ? kCompBurstOn | kCompAtaqueGratis : 0);
  __imp__sub_82104C00(ctx, base);
}
// B3 reparte el precio entre el nodo y el #SKC. Se conserva la comprobacion de capsula/forma;
// solo se deja gratis el pago anidado al iniciar un ataque en Burst (IW 0x224670).
REX_HOOK_RAW(sub_82102650) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32, previo = g_comp_capsula;
  if (SideOf(base, x) >= 0 && Competitivo() && AtaqueGratis(base, x))
    g_comp_capsula = x;
  __imp__sub_82102650(ctx, base);
  g_comp_capsula = previo;
}
// IW reparte las gratuidades por accion, no en HitDamageCount (0x235320). Se limitan por llamada y banderas;
// nunca se sube el ki temporalmente ni se activa Hiper. La definitiva y el drenaje siguen pagando.
REX_HOOK_RAW(sub_82115518) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32, flags = ctx.r5.u32;
  const bool l1 = x == g_comp_chase &&
                  ((ctx.lr == 0x8210B2B0 && flags == 10) || (ctx.lr == 0x8210B42C && flags == 0));
  const bool burst = l1 && Burst(base, x);
  static const bool prueba_cobros = std::getenv("DBZ3_TEST_COMP_COBROS") != nullptr;
  const float antes = l1 || prueba_cobros ? LoadF(base, x + kKi) : 0.0f;
  const double coste = ctx.f1.f64;
  const uint64_t llamada = ctx.lr;
  bool gratis = burst;
  if (SideOf(base, x) >= 0 && Competitivo()) {
    const bool activo = Burst(base, x);
    // IW 0x22E150 / 0x22E290: solo el coste, despues de todos los bloqueos defensivos originales.
    gratis |= activo && flags == 10 && (ctx.lr == 0x82117CB8 || ctx.lr == 0x82117BE0);
    // IW 0x221A60 y rama equivalente de impacto: teleport/esquiva, sin coste ni fatiga durante Burst.
    gratis |= activo && flags == 8 &&
              (ctx.lr == 0x8210EB64 || ctx.lr == 0x8210EE7C || ctx.lr == 0x8210F2D8);
    gratis |= activo && x == g_comp_actor && g_comp_nodo && flags == 2 && ctx.lr == 0x820F7744 &&
               (REX_LOAD_U32(g_comp_nodo + 8) & 0x30000);
    gratis |= activo && x == g_comp_actor && g_comp_nodo && flags == 2 && ctx.lr == 0x821159F0 &&
               !(REX_LOAD_U32(g_comp_nodo + 8) & 8);   // B3 junta posesion y ki; la definitiva (8) sigue pagando
    // IW 0x224670: el golpe del ataque iniciado en Burst no cobra, incluso si Burst ya termino.
    gratis |= flags == 0 && ctx.lr == 0x821026B0 && AtaqueGratis(base, x);
    gratis |= flags == 0 && ctx.lr == 0x821159F0 && x == g_comp_capsula;
    // Las mismas ramas IW tampoco regalan 200 de ki al rival cuando la defensa se hizo en Burst.
    if (coste < 0.0 && flags == 0 && (ctx.lr == 0x8210EEA4 || ctx.lr == 0x8210F2EC) &&
        Burst(base, ctx.r31.u32)) { ctx.r3.u64 = 1; return; }
  }
  if (gratis && coste > 0.0) {
    ctx.r3.u64 = 1;
  } else __imp__sub_82115518(ctx, base);
  if (prueba_cobros && coste > 1.0 && (flags == 0 || (flags == 2 && g_comp_actor == x)))
    REXLOG_INFO("dbz3 combate: COMP prueba cobro: lado {}, retorno {:08X}, banderas {}, coste {:.0f}, gratis {}, ki {:.0f} -> {:.0f}",
                SideOf(base, x) + 1, llamada, flags, coste, gratis, antes, LoadF(base, x + kKi));
  if (static const bool prueba = std::getenv("DBZ3_TEST_COMP_RUSH") != nullptr; prueba && l1)
    REXLOG_INFO("dbz3 combate: COMP prueba L1: lado {}, Burst {}, banderas {}, coste {:.0f}, ki {:.0f} -> {:.0f}",
                SideOf(base, x) + 1, burst, flags, coste, antes, LoadF(base, x + kKi));
}

// COMP: ki y fatiga por cuadro (IW 0x22BCD0). Estado independiente: la rutina Hiper no corre durante el Burst.
REX_HOOK_RAW(sub_82119EA0) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  const int lado = SideOf(base, x);
  static bool antes[2] = {};
  static uint32_t desde[2] = {};
  const bool burst = lado >= 0 && Burst(base, x);
  if (lado >= 0 && burst != antes[lado]) {   // solo registro
    if (burst) desde[lado] = g_frame.load();
    REXLOG_INFO("dbz3 combate: COMP Aura Burst del lado {} {} (ki {:.0f}, fatiga {:.1f}, agotado {}, accion {}, {} cuadros)",
                lado + 1, burst ? "encendido" : "apagado", LoadF(base, x + kKi), LoadF(base, x + 6120),
                REX_LOAD_U8(x + 8211), REX_LOAD_U16(x + kAct), burst ? 0u : g_frame.load() - desde[lado]);
    antes[lado] = burst;
  }
  // el estado con byte 21 == 112 del #SKC (ki siempre al tope) va antes que el hiper tambien en B3: sin cambios
  const int16_t est = int16_t(REX_LOAD_U16(x + 8176));
  const uint32_t skc = REX_LOAD_U32(dbz3::GA(0x8237560C));
  if (!burst || (est && skc && REX_LOAD_U8(skc + 40 * uint32_t(est) + 21) == 112)) {
    __imp__sub_82119EA0(ctx, base);
    return;
  }
  // IW 0x22BCD0: no corre en 0x32, 0x1E, 0x35 ni agotado.
  const uint16_t accion = REX_LOAD_U16(x + kAct);
  if (accion == 0x32 || accion == 0x1E || accion == 0x35 || REX_LOAD_U8(x + 8211)) return;
  const PPCContext guardado = ctx;
  // IW LAB_0022bd98: HitDamageCount(X+0x20A8 * 10.0, X, 4) -> 10 de ki por cuadro, sin multiplicador de coste
  // (X+0x20A8 = 1.0: las capsulas de duracion del hiper se ocultan en el modo, decision 5). Pareja: sub_82115518.
  ctx.r3.u64 = x, ctx.r5.u64 = 4, ctx.f1.f64 = 10.0;
  sub_82115518(ctx, base);
  // IW: ki (o el tope, con el reloj de ki ilimitado) < 20.0 -> se apaga; NO llama al agotamiento (0x22DFD0)
  if ((LoadF(base, x + 6720) > 0.0f ? LoadF(base, x + kKiMax) : LoadF(base, x + kKi)) < 20.0f) {
    ctx = guardado;
    ctx.r3.u64 = x, ctx.r4.u64 = 1;   // 1 = suelta tambien el filtro oscuro (X+6836), como IW al apagarse
    sub_82111A98(ctx, base);
  }
  // IW LAB_0022bfd8: FatigueBarScript(-(2.0 * 0.01), X, 0): la fatiga baja el doble en el Burst. Pareja: sub_821194E0.
  ctx = guardado;
  ctx.r3.u64 = x, ctx.r5.u64 = 0, ctx.f1.f64 = -0.02;
  sub_821194E0(ctx, base);
  ctx = guardado;
}

// COMP/7: persecucion (sub_8210B1C0 = IW 0x220EC0 P_K_G_E_AnimationPicker, rama de la persecucion). Las condiciones
// son las mismas en los dos (atacante con X+8215 = IW 0x207C, tope de 3 = X+8218 / 0x207F, no agotado, rival saliendo
// volando, 1000 de ki comprobados y cobrados). Lo que IW cambio: en el Aura Burst la persecucion es GRATIS y es la
// accion 0x28; B3, en hiper, en vez de la persecucion lanza el Dragon Rush (sub_82120C90(5, ...): evento 5,
// sub_82121390). En COMP +8208 siempre queda a cero. El contexto de cobro solo cubre esta llamada; el motor mantiene
// la distancia y el seguimiento nativos, el contador de tres, la victima en vuelo y los bloqueos de accion.
REX_HOOK_RAW(sub_8210B1C0) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  if (SideOf(base, x) < 0 || !Competitivo()) {
    __imp__sub_8210B1C0(ctx, base);
    return;
  }
  const float ki = LoadF(base, x + kKi);
  const uint16_t act = REX_LOAD_U16(x + kAct);
  const uint32_t previo = g_comp_chase;
  g_comp_chase = x;
  __imp__sub_8210B1C0(ctx, base);
  g_comp_chase = previo;
  if (act != 40 && REX_LOAD_U16(x + kAct) == 40 && Burst(base, x))
    REXLOG_INFO("dbz3 combate: COMP persecucion en el Aura Burst del lado {} (gratis, ki {:.0f}, seguidas {})",
                SideOf(base, x) + 1, ki, REX_LOAD_U8(x + 8218));
}
// COMP/7: tipo de persecucion del humano (sub_821033E0(I = X+4960), solo la llama sub_8210B1C0). B3 la pide con E
// pulsado ese cuadro (I+84 == 1) y la direccion del historial de la palanca (neutro 1, -1 -> 2, +1 -> 3); en el modo
// la pide L1 (decision 6): se le ensena E pulsado solo si L1 lo esta, y si no, no hay persecucion (0).
REX_HOOK_RAW(sub_821033E0) {
  using namespace dbz3::combate;
  const uint32_t i = ctx.r3.u32;
  const int lado = SideOf(base, i - 4960);
  if (lado < 0 || !Competitivo()) {
    __imp__sub_821033E0(ctx, base);
    return;
  }
  if (!g_l1[size_t(lado)]) {
    ctx.r3.u64 = 0;
    return;
  }
  const uint32_t e = REX_LOAD_U32(i + 84);
  REX_STORE_U32(i + 84, 1);
  __imp__sub_821033E0(ctx, base);
  REX_STORE_U32(i + 84, e);
  REXLOG_INFO("dbz3 combate: COMP L1 del lado {}: persecucion pedida (tipo {})", lado + 1, ctx.r3.u32);
}

// COMP paso 6: ataque y defensa (sub_82115A20 = IW 0x233CD0 GeneralKiLoss; las dos las llama el paso de pelea cada
// cuadro para los dos luchadores). IW, con X+0x2091: suma 5.0 al ataque y a la defensa de la forma antes de acumularlos;
// como los dos se suman tal cual al total, aqui se suman 5 a los totales (X+6668 / X+6672), se rehacen sus factores
// (x 0.01, X+6680 / X+6684) y las dos cifras del marcador, con la misma cuenta que el final de la funcion.
REX_HOOK_RAW(sub_82115A20) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  __imp__sub_82115A20(ctx, base);
  if (SideOf(base, x) < 0 || !Burst(base, x)) return;
  const int32_t ata = int32_t(REX_LOAD_U32(x + 6668)) + 5, def = int32_t(REX_LOAD_U32(x + 6672)) + 5;
  REX_STORE_U32(x + 6668, uint32_t(ata));
  REX_STORE_U32(x + 6672, uint32_t(def));
  const float fa = float(ata) * 0.01f, fd = float(def) * 0.01f;
  StoreF(base, x + 6680, fa);
  StoreF(base, x + 6684, fd);
  const PPCContext guardado = ctx;
  ctx.r3.u64 = REX_LOAD_U32(x + 4944);
  sub_820DB540(ctx, base);
  const uint32_t hud = ctx.r3.u32;
  ctx = guardado;
  auto cifra = [](float f) { return int16_t(100 - int32_t((f + (f >= 0.0f ? 0.005f : -0.005f)) * -100.0f)); };
  if (hud) REX_STORE_U16(hud + 60, uint16_t(cifra(fa))), REX_STORE_U16(hud + 58, uint16_t(cifra(fd)));
}

// COMP paso 6: fatiga (sub_821194E0 = IW 0x235180 FatigueBarScript). B3 solo aturde (sub_82117CD0(X, 1)) fuera del hiper
// y con fatiga > 800 y ki < 1000; IW, tambien en el Burst y solo con la barra llena: banderas & 1, no agotado y
// fatiga >= 1000.0. Se deja a B3 sumar y limitar (sub_82112028) sin su aturdimiento y se aplica el de IW.
REX_HOOK_RAW(sub_821194E0) {
  using namespace dbz3::combate;
  const uint32_t x = ctx.r3.u32;
  // IW 0x221A60 / rama de impacto: los 100 de fatiga del teleport no se suman en Burst.
  // No se filtra la fatiga de golpes ni capsulas ni la recuperacion por cuadro.
  if (Burst(base, x) && ctx.r5.u32 == 0 && ctx.f1.f64 == 100.0 &&
      (ctx.lr == 0x8210EB78 || ctx.lr == 0x8210EE90)) { ctx.r3.u64 = 0; return; }
  if (SideOf(base, x) < 0 || !Competitivo() || ctx.f1.f64 < 0.0 || !(ctx.r5.u32 & 1)) {
    __imp__sub_821194E0(ctx, base);
    return;
  }
  const PPCContext guardado = ctx;
  ctx.r5.u64 = ctx.r5.u32 & ~1u;
  __imp__sub_821194E0(ctx, base);
  const uint32_t ret = ctx.r3.u32;
  ctx = guardado;
  if (!REX_LOAD_U8(x + 8211) && LoadF(base, x + 6120) >= 1000.0f) {
    REXLOG_INFO("dbz3 combate: COMP fatiga llena del lado {}: aturdido (ki {:.0f}, Burst {})", SideOf(base, x) + 1,
                LoadF(base, x + kKi), Burst(base, x));
    ctx.r3.u64 = x, ctx.r4.u64 = 1;
    sub_82117CD0(ctx, base);
    ctx = guardado;
  }
  ctx.r3.u64 = ret;
}

namespace dbz3::combate {
void SetMuneco(int modo) {   // 0 nada, 1 quieto, 2 guardia, 3 ataca, -1 el ajuste dbz3_combate_prueba_muneco
  if (g_muneco_api.exchange(modo) == modo) return;
  REXLOG_INFO("dbz3 combate: muneco {}", modo == 1 ? "quieto" : modo == 2 ? "en guardia" : modo == 3 ? "ataca" : modo == 0 ? "no" : "del ajuste");
}
}  // namespace dbz3::combate

namespace dbz3::comp {
bool EnBurst(int lado) {
  return lado >= 0 && lado < 2 && dbz3::combate::g_comp_burst[lado].load(std::memory_order_relaxed);
}
float KiBloques(int lado) {
  return lado >= 0 && lado < 2 ? dbz3::combate::g_comp_ki[lado].load(std::memory_order_relaxed) : 0.0f;
}
}  // namespace dbz3::comp

// Marcador del combate (2026-10-16, frente P4). El objeto del marcador (*0x82324064) registra 4 tareas al crearse
// (sub_820DCBC0): "FIX MAIN" (sub_820DCA10), "FIX PREDRAW" (sub_820DC080), "FIX DRAW" (sub_820DC1A8, o sub_820DC230
// en el modo 6) y "FIX POSTDRAW" (sub_820DBB00). FIX DRAW pinta las barras, caras, "V", reloj (sub_820DBCF0) y los
// avisos (sub_820EB9F0) solo si mgr+35253 != 1 y 0x823CD870 tiene el bit alto: asi lo apaga el juego. OcultarHud
// salta esa tarea entera.
std::atomic<bool> g_ocultar_hud{false};
std::atomic<int64_t> g_fix_draw_ms{0};   // (frente R6) ultima vez que el juego llamo a FIX DRAW: HudNativoVisible

// POST/SDK-01-ultra (`dbz3_ultrapanoramico`): el presentador estira x g_ultra_k la imagen 1280x720 del juego. Mientras
// se pinta el marcador, cada vertice 2D se encoge 1/k hacia el ancla de su pieza: izquierda, derecha o
// centro segun el centro de la pieza (o del vertice, si no viene de una pieza). Asi sale sin estirar y a la
// misma distancia de su borde que en 16:9. Fuera del combate todo el 2D va a 16:9 en el centro, con bandas.
static int g_hud_ancla = 0;   // > 0 dentro de una tarea de dibujo del marcador
struct HudAncla {
  HudAncla() { g_hud_ancla += g_ultra_k > 1.01f; }
  ~HudAncla() { g_hud_ancla -= g_ultra_k > 1.01f; }
};
static float HudF(uint8_t* base, uint32_t a) { const uint32_t u = REX_LOAD_U32(a); float f; std::memcpy(&f, &u, 4); return f; }
static void HudS(uint8_t* base, uint32_t a, float f) { uint32_t u; std::memcpy(&u, &f, 4); REX_STORE_U32(a, u); }
static float g_ancla_pieza = -1.0f;
static bool UltraMenu();   // ancla de la pieza que se esta pintando (-1: se decide por vertice)
// El 2D del marcador mide 800 de ancho sobre la imagen 1280 (medido 12/11 con SONDA_ULTRA: el reloj esta en x = 400;
// con el render ancho de 1704 de antes medía 1066,7 = 800 x 1704 / 1280)
constexpr float kAncho2D = 800.0f;
static float AnclaDe(float cx) {
  return cx < kAncho2D * 0.5f - 20 ? 0.0f : cx > kAncho2D * 0.5f + 20 ? kAncho2D : kAncho2D * 0.5f;
}
namespace dbz3::campaigns { bool NarracionAncha(); }   // campaigns.cpp
static void AnclarSprite(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig) {
  const uint32_t d = ctx.r3.u32, dev = REX_LOAD_U32(dbz3::GA(0x82386260));
  if (d && dbz3::campaigns::NarracionAncha() && REX_LOAD_U8(dbz3::GA(0x824BA110)) == 8) {   // historia del Heroe con dibujo grande (campaigns.cpp)
    const float x1 = HudF(base, d + 56), y1 = HudF(base, d + 60), x0 = HudF(base, d + 48), y0 = HudF(base, d + 52);
    float px = HudF(base, d + 32), py = HudF(base, d + 36), nx = px, ny = py;
    if (x1 == 240.0f && y1 == 160.0f) ny = py + 2000.0f;                                      // marco nativo del panel: fuera de pantalla
    else if (x0 == 0.0f && x1 == 600.0f && y1 == 0.0f && y0 < 0.0f && py > 300.0f) ny = py + 2000.0f;   // tiras de nubes del borde inferior
    else if (x0 == -16.0f && x1 == 16.0f && y0 == 0.0f && y1 == 32.0f && py > 330.0f) nx = 645.0f, ny = 372.0f;   // triangulo de continuar: abajo a la derecha de la franja
    if (nx != px || ny != py) {
      HudS(base, d + 32, nx), HudS(base, d + 36, ny);
      orig(ctx, base);
      HudS(base, d + 32, px), HudS(base, d + 36, py);
      return;
    }
  }
  if (g_hud_ancla <= 0 || !d || !dev) return orig(ctx, base);
  // centro de la pieza en pantalla: escala/desplazamiento 2D del dibujo (dev+312/+304, ver sub_82098000) sobre la
  // posicion (+32), el desplazamiento del grupo (+96, si el bit 0x40) y las esquinas x (+48/+56)
  const float gx = (REX_LOAD_U32(d + 4) & 0x40) ? HudF(base, d + 96) : 0.0f;
  const float cx = HudF(base, dev + 312) * (HudF(base, d + 32) + gx + (HudF(base, d + 48) + HudF(base, d + 56)) * 0.5f) +
                   HudF(base, dev + 304);
  g_ancla_pieza = AnclaDe(cx);
  orig(ctx, base);
  g_ancla_pieza = -1.0f;
}
// pareja EU a mano: misma distancia (-0x70) que sus vecinas verificadas sub_820ACCF0 / sub_820B04C8 (eu_map_gen.h)
namespace dbz3::heroe_start { bool DibujarSprite(PPCContext&, uint8_t*, Dbz3GuestFn); }
DBZ3_HOOK(sub_820AD6E0, dbz3eu_sub_820AD670) { if (!dbz3::heroe_start::DibujarSprite(ctx, base, orig)) AnclarSprite(ctx, base, orig); }
DBZ3_HOOK(sub_820ADD18, dbz3eu_sub_820ADCA8) { if (!dbz3::heroe_start::DibujarSprite(ctx, base, orig)) AnclarSprite(ctx, base, orig); }
// Vertice 2D (r3 = dibujo, r4 = x,y,z): x pantalla = dev[312] * x + dev[304]. Dentro del marcador se cambian esos dos
// para que el vertice caiga en ancla + (X - ancla) / k. EU: -0x68 como sub_82096FF0 (eu_map_gen.h).
// Combate = el juego llamo a FIX DRAW hace < 250 ms (corre tambien con el marcador oculto en la vuelta de camara).
static bool UltraMenu() { return g_ultra_k > 1.01f && dbz3::combate::NowMs() - g_fix_draw_ms.load() > 250; }
static void AnclarVertice(PPCContext& ctx, uint8_t* base, Dbz3GuestFn orig) {
  const uint32_t dev = ctx.r3.u32, v = ctx.r4.u32;
  if (g_hud_ancla <= 0 || !dev || !v) return orig(ctx, base);
  const float k = g_ultra_k, s = HudF(base, dev + 312), o = HudF(base, dev + 304);
  const float ancla = g_ancla_pieza >= 0 ? g_ancla_pieza : AnclaDe(s * HudF(base, v) + o);
  HudS(base, dev + 312, s / k);
  HudS(base, dev + 304, ancla + (o - ancla) / k);
  orig(ctx, base);
  HudS(base, dev + 312, s);
  HudS(base, dev + 304, o);
}
// los tres emisores de vertices 2D (via sub_82085A58 / sub_82085AA0 / sub_82085A70); EU: -0x68 como sub_82096FF0
DBZ3_HOOK(sub_82098000, dbz3eu_sub_82097F98) { AnclarVertice(ctx, base, orig); }
DBZ3_HOOK(sub_82097F20, dbz3eu_sub_82097EB8) { AnclarVertice(ctx, base, orig); }
DBZ3_HOOK(sub_82098100, dbz3eu_sub_82098098) { AnclarVertice(ctx, base, orig); }
// POST2/ULTRA: el render del guest sigue en 1280x720 (asi menus y escenas salen identicos al 16:9 normal). En combate
// la proyeccion del 3D pasa al aspecto de la ventana (video+3408/+3412, la lee la camara en cada cuadro) y el
// presentador estira la imagen a la ventana sin bandas: 3D sin deformar y con mas campo, HUD anclado (AnclarVertice).
// Fuera del combate, aspecto original y las bandas que tenga el usuario. Lo decide el hilo de ImGui cada cuadro con el
// tamano real de la ventana y el ajuste: cambiar cualquiera de los dos en marcha surte efecto al momento.
namespace {
class BandasUltra : public rex::ui::ImGuiDialog {
 public:
  explicit BandasUltra(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const int ww = int(io.DisplaySize.x), wh = int(io.DisplaySize.y);
    const bool on = g_ultra_video && REXCVAR_GET(dbz3_ultrapanoramico) && wh > 0 && ww * 9 > wh * 16 + wh / 8;
    g_ultra_k = on ? (float(ww) / float(wh)) / (16.0f / 9.0f) : 0.0f;   // estirado del presentador (HUD)
    const int modo = !on ? 0 : UltraMenu() ? 1 : 2;
    if (modo == modo_ && (modo != 2 || (ww == ww_ && wh == wh_))) return;
    uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
    if (!base) return;
    if (modo == 2 && modo_ != 2) letterbox_ = rex::cvar::GetFlagByName("present_letterbox");   // el del usuario
    unsigned aw = g_ultra_aw0, ah = g_ultra_ah0;
    if (modo == 2) {
      unsigned a = unsigned(ww), b = unsigned(wh);
      while (b) { const unsigned t = a % b; a = b; b = t; }
      aw = unsigned(ww) / a, ah = unsigned(wh) / a;
    }
    REX_STORE_U32(g_ultra_video + 3408, aw);
    REX_STORE_U32(g_ultra_video + 3412, ah);
    if (modo == 2) rex::cvar::SetFlagByName("present_letterbox", "false");
    else if (modo_ == 2 && !letterbox_.empty()) rex::cvar::SetFlagByName("present_letterbox", letterbox_);
    REXLOG_INFO("dbz3: ultrapanoramico {} (ventana {}x{}, aspecto 3D {}:{})",
                modo == 2 ? "combate (ancho)" : modo == 1 ? "menu (16:9)" : "apagado", ww, wh, aw, ah);
    modo_ = modo, ww_ = ww, wh_ = wh;
  }

 private:
  int modo_ = 0, ww_ = 0, wh_ = 0;
  std::string letterbox_;
};
}  // namespace
void CrearBandasUltra() {
  dbz3::comp::hud::Crear();
  dbz3::comp::aura::Crear();
  static BandasUltra* p = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!p && rt && rt->imgui_drawer()) p = new BandasUltra(rt->imgui_drawer());
}
// FIX PREDRAW del marcador (sub_820DCBC0); EU a mano: -0x40 como FIX DRAW (sub_820DC1A8 -> sub_820DC168)
DBZ3_HOOK(sub_820DC080, dbz3eu_sub_820DC040) { HudAncla a; orig(ctx, base); }
DBZ3_HOOK_EU(sub_820DC1A8) {
  g_fix_draw_ms = dbz3::combate::NowMs();
  if (g_ocultar_hud.load()) return;
  HudAncla a;
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820DC230) {
  g_fix_draw_ms = dbz3::combate::NowMs();
  if (g_ocultar_hud.load()) return;
  HudAncla a;
  orig(ctx, base);
}
// (frente R14) Contadores de golpes y dano ("2 HITS!!", "80 DMG."): son tareas aparte por jugador, registradas en
// sub_820DF248 ("[FIX]DMG %dP" + "DMGDRW %dP" = sub_820DDCA0) y sub_820DF330 ("[FIX]CMB %dP" + "CMBDRW %dP" =
// sub_820DDFE8). OcultarHud salta tambien sus dibujos.
DBZ3_HOOK_EU(sub_820DDCA0) {
  if (g_ocultar_hud.load()) return;
  HudAncla a;
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820DDFE8) {
  if (g_ocultar_hud.load()) return;
  HudAncla a;
  orig(ctx, base);
}
// (B2-P02) NO se saltan sub_820DDC20 (DMG) ni sub_820DDF68 (CMB): son el DESTRUCTOR de la tarea [FIX] (segundo
// argumento de sub_82092908 en sub_820DF248 / sub_820DF330), no un dibujo: ponen a 0 su hueco en 0x823CD904/0x823CD914,
// matan la tarea DMGDRW/CMBDRW (sub_82092A20) y sueltan los sprites (sub_820ACCA0). Saltarlos con el marcador oculto
// (toda la Nave) dejaba viva la tarea de dibujo apuntando a la [FIX] ya liberada; al reusarse esa memoria,
// sub_820DDD88 llamaba a sub_820AE730 con un 1.0f como puntero (lectura de 0x3F800004, planta III 10/11).
// y los avisos del combate ("FIRST ATTACK!!", contraataque, rotura de guardia: tareas "[MSG]FATK/CNTR/GBREAK %dP"),
// que pinta la tarea "[MSGDRW]ID=%d" (sub_820DE490, registrada en sub_820DEAC8)
DBZ3_HOOK_EU(sub_820DE490) {
  if (g_ocultar_hud.load()) return;
  HudAncla a;
  orig(ctx, base);
}
namespace dbz3::combate {
void OcultarHud(bool ocultar) {
  if (g_ocultar_hud.exchange(ocultar) != ocultar) REXLOG_INFO("dbz3 combate: marcador {}", ocultar ? "oculto" : "visible");
}
bool HudNativoVisible() {   // (frente R6) la misma condicion con la que FIX DRAW pinta, solo lectura
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  if (!base || g_ocultar_hud.load()) return false;
  // y que el juego haya llamado a FIX DRAW hace nada (< 100 ms): en la vuelta de camara del escenario el objeto del
  // marcador y las dos marcas ya dan "visible" pero el juego no lo pinta (captura
  // 20261016-033323_hub_test_r6_hud\a0_presentacion.png)
  if (NowMs() - g_fix_draw_ms.load() > 100) return false;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr), hud = REX_LOAD_U32(dbz3::GA(0x82324064));
  // ...y la puerta de sub_820DBCF0 (barras, caras, reloj): ([0x823CD870] & [0x823CD874]) con los bits 31 y 1 y el
  // puntero [0x823CC6CC] puesto. FIX DRAW corre tambien en la vuelta de camara (captura
  // 20261016-033826_hub_test_r6_hud\a0_presentacion.png) y es esta puerta la que no pinta.
  // ponytail: 0x823CD874 y 0x823CC6CC por distancia a 0x823CD870 (en EU es la misma direccion); si EU falla, su pareja
  // con tools/eu_map.py.
  const dbz3::GA kBanderas = dbz3::GA(0x823CD870);
  const uint32_t v = REX_LOAD_U32(kBanderas) & REX_LOAD_U32(kBanderas + 4);
  return mgr != 0 && hud != 0 && REX_LOAD_U8(mgr + 35253) != 1 && (v & 0x80000002u) == 0x80000002u &&
         REX_LOAD_U32(kBanderas - 0x11A4) != 0;
}
}  // namespace dbz3::combate

// Mando conectado o no (sub_8208FC40: r3 = usuario; lee XInput y marca el registro). El del muneco siempre lo esta
// (sin esto el juego pausaba con "el mando del jugador 2 ha sido desconectado").
DBZ3_HOOK_EU(sub_8208FC40) {
  using namespace dbz3::combate;
  if (ctx.r3.u32 == kMunecoPad && MunecoMandoActivo(base)) {
    REX_STORE_U32(kPads + 184 * kMunecoPad, 3);
    ctx.r3.u64 = 1;
    return;
  }
  orig(ctx, base);
}

// ---------------------------------------------------------------- ondas devueltas (Nave de Babidi, prueba IV)
// B2 (descompilado b2.c): FUN_001e2610 (onda contra luchador) pregunta a FUN_001f7fc0 y, con 5 = "devuelta", llama a
// FUN_001ebf40(luchador, 3), que en el modo 9 (prueba IV) suma 1 al contador del luchador (+0x9bb8); al acabar,
// FUN_001d41e0 lo deja como resultado (FUN_001d6580) y FUN_00265ab0 paga 3 Kili por onda. B3 trae la misma regla:
// sub_82116FA0 = FUN_001f7fc0 (mismos 5.0 / 12.0 cuadros desde que se pulsa la guardia, X+6008, y las habilidades
// Deflect / Deflect-Back, X+8140 / X+8141). sub_82119B38 (la onda toca al luchador) recibe 5 (o 6: su golpe la
// devuelve) y, para devolverla, llama a sub_82122488(1, 26, ...) desde 0x82119D00 (EU 0x82119E00), la unica llamada
// asi. Ese es el suceso: se cuenta por lado (el defensor es r30 de sub_82119B38, que sigue viva al entrar aqui).
// Solo el 5 (guardia): el 6 (golpe en curso, accion 15) no tiene equivalente en B2.
namespace dbz3::combate {
namespace {
std::atomic<int> g_ondas[2] = {};
// Leyenda de Mr. Satan (pasarela.cpp, REFLECT / CUT-OFF de B1): las otras dos salidas de sub_82119B38 con el resultado de
// sub_82116FA0 (3/4 = onda desviada, sub_82122488(1, 25) desde 0x82119D90 US / 0x82119E90 EU; 5/6 = devuelta, la de
// arriba) y la devuelta con un golpe (accion 15)
std::atomic<int> g_ondas_golpe[2] = {};
std::atomic<int> g_ondas_desv[2] = {};
}
int OndasDevueltas(int lado) { return lado >= 0 && lado < 2 ? g_ondas[lado].load() : 0; }
// tipo 0 devuelta con la guardia, 1 devuelta con un golpe, 2 desviada (CUT-OFF de B1)
int OndasEvento(int lado, int tipo) {
  if (lado < 0 || lado > 1) return 0;
  return tipo == 0 ? g_ondas[lado].load() : tipo == 1 ? g_ondas_golpe[lado].load() : g_ondas_desv[lado].load();
}
void OndasCero() { g_ondas[0] = g_ondas[1] = 0; }
// Modo ondas (prueba IV): el rival pasa a ser el muneco en guardia con Deflect-Back mientras dure; false lo deja como estaba.
void OndasModo(bool on) {
  if (g_ondas_modo.exchange(on) == on) return;
  if (on)
    g_ondas_muneco_antes = g_muneco_api.exchange(2);
  else
    g_muneco_api = g_ondas_muneco_antes.load();
  REXLOG_INFO("dbz3 combate: modo ondas {} (muneco {})", on ? "si" : "no", g_muneco_api.load());
}
}  // namespace dbz3::combate

DBZ3_HOOK_EU(sub_82122488) {
  using namespace dbz3::combate;
  if (ctx.r3.u32 == 1 && ctx.r4.u32 == 26 && ctx.lr == (dbz3::g_guest_eu ? 0x82119E00u : 0x82119D00u)) {
    const uint32_t x = ctx.r30.u32;   // el defensor
    const int lado = SideOf(base, x);
    if (lado >= 0 && REX_LOAD_U16(x + kAct) != 15) {
      const int n = ++g_ondas[lado];
      REXLOG_INFO("dbz3 combate: onda devuelta por el lado {} (accion {}, guardia {} cuadros, tiempo {:.1f}): {}", lado + 1,
                  REX_LOAD_U16(x + kAct), int32_t(REX_LOAD_U32(x + 5040)), LoadF(base, x + 6008), n);
    } else if (lado >= 0) {
      const int n = ++g_ondas_golpe[lado];
      REXLOG_INFO("dbz3 combate: onda devuelta a golpe por el lado {} (accion 15, codigo {:03X}): {}", lado + 1,
                  REX_LOAD_U32(x + 2928) & 0xFFF, n);
    }
  } else if (ctx.r3.u32 == 1 && ctx.r4.u32 == 25 && ctx.lr == (dbz3::g_guest_eu ? 0x82119E90u : 0x82119D90u)) {
    const uint32_t x = ctx.r30.u32;   // el defensor; r25 = resultado de sub_82116FA0 (3 o 4)
    const int lado = SideOf(base, x);
    if (lado >= 0) {
      const int n = ++g_ondas_desv[lado];
      REXLOG_INFO("dbz3 combate: onda desviada por el lado {} (resultado {}, accion {}, guardia {} cuadros, tiempo {:.1f}): {}",
                  lado + 1, ctx.r25.u32, REX_LOAD_U16(x + kAct), int32_t(REX_LOAD_U32(x + 5040)), LoadF(base, x + 6008), n);
    }
  }
  orig(ctx, base);
}
