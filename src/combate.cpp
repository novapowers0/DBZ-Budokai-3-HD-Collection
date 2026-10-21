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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "combate.h"
#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "iw_minijuegos.h"
#include "cinematica.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "net/netplay.h"
#include "tagteam.h"

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
std::atomic<int> g_aviso[2] = {};   // cuadros que faltan para el golpe que va a recibir cada lado (0 = nada)
std::atomic<bool> g_humano[2] = {};
std::atomic<int64_t> g_aviso_ms[2] = {};   // ultimo aviso (para DBZ3_COMBATE_AVISO_PRUEBA: verlo en una captura)

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

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
  return skc && cap < 1024 ? float(REX_LOAD_U8(skc + 40 * uint32_t(cap) + 15)) * 100.0f : 1e9f;
}
bool Directa(uint8_t* base, uint32_t x, int lado) {
  const Ult& u = g_ult[size_t(lado)];
  return u.cap && Equipada(base, x, u.cap) && LoadF(base, x + kKi) >= Coste(base, u.cap);
}
// Seguimiento de la definitiva (log): empieza (codigo de la entrada), cobro (el juego quita el ki de una vez cuando
// la definitiva se confirma: el ataque conecta y arranca la escena) y como acaba (de pie, golpeado, KO, fin).
std::array<uint32_t, 2> g_ultima_code{};   // ultimo codigo de ataque de cada lado (para los proyectiles)
void DefinitivaLog(uint8_t* base) {
  static float ki[2] = {};
  static uint32_t code[2] = {};
  static bool en[2] = {};
  static float cobrado[2] = {};
  for (int k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, k), c = REX_LOAD_U32(x + kCode), act = REX_LOAD_U16(x + kAct);
    const float now = LoadF(base, x + kKi);
    if (act == 15 && c) g_ultima_code[size_t(k)] = c;
    if (g_ult[size_t(k)].code && (c == g_ult[size_t(k)].code || c == g_ult[size_t(k)].aire) && code[k] != c) {
      REXLOG_INFO("dbz3 combate: 15 definitiva {:X} del lado {} (capsula {}, coste {:.0f}): empieza con ki {:.0f}, "
                  "modo hiper {}", c, k + 1, g_ult[size_t(k)].cap, Coste(base, g_ult[size_t(k)].cap), now,
                  REX_LOAD_U8(x + kHyper));
      en[k] = true;
      cobrado[k] = 0;
    } else if (en[k]) {
      if (ki[k] - now > 500.0f) {
        cobrado[k] += ki[k] - now;
        REXLOG_INFO("dbz3 combate: 15 cobro de {:.0f} de ki al lado {} (cuadro {}, codigo {:X}): {:.0f} -> {:.0f}",
                    ki[k] - now, k + 1, g_frame.load(), c, ki[k], now);
      }
      if (act != 15 || BattleState(base) != 4) {
        REXLOG_INFO("dbz3 combate: 15 fin de la definitiva del lado {}: accion {}, vida {:.0f}, estado {}, cobrado {:.0f}",
                    k + 1, act, LoadF(base, x + kHp), BattleState(base), cobrado[k]);
        en[k] = false;
      }
    }
    code[k] = c;
    ki[k] = now;
  }
}

// ---------------------------------------------------------------- Capsula 1970: Transmision Instantanea
// Elimina el consumo de Ki durante la accion de teletransporte (Z-Counter / colocarse a la espalda, accion 39)
constexpr uint16_t kCapTeleportInstant = 1970;
void TransmisionInstantaneaTick(uint8_t* base) {
  static float ki_prev[2] = {0.0f, 0.0f};
  static uint16_t act_prev[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    const uint32_t x = Fighter(base, k);
    if (!x) continue;
    const uint16_t act = REX_LOAD_U16(x + kAct);
    const float cur_ki = LoadF(base, x + kKi);

    if (Equipada(base, x, kCapTeleportInstant)) {
      // Acciones 39/40: Teletransporte a la espalda / contraataque
      if (act == 39 || act == 40) {
        if (cur_ki < ki_prev[k]) {
          // Restaurar el Ki previo al consumo del teletransporte
          StoreF(base, x + kKi, ki_prev[k]);
          REXLOG_INFO("dbz3 combate: Transmision Instantanea lado {}: Ki protegido ({:.0f} -> {:.0f})",
                      k + 1, cur_ki, ki_prev[k]);
        }
      } else {
        // En cualquier otra accion guardamos el Ki actual como referencia
        ki_prev[k] = cur_ki;
      }
    } else {
      ki_prev[k] = cur_ki;
    }
    act_prev[k] = act;
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
    if (NowMs() - g_tick_ms.load() > 250 || dbz3::GameDimmed() || dbz3::OverlayOpen()) return;
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
    if (!r.empty() && ahora < hasta) S::MenuText(dl, a.At(600.0f, 868.0f), 22.0f * a.s, r.c_str(), false, false);
  }
};

void CrearPanel() {
  static CombatePanel* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new CombatePanel(rt->imgui_drawer());
}

}  // namespace dbz3::combate

// Paso de pelea de los dos luchadores (sub_821100B0: entradas, golpes, acciones; sin otro gancho en src/).
DBZ3_HOOK_EU(sub_821100B0) {
  using namespace dbz3::combate;
  {
    const uint32_t st = BattleState(base);
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
    static bool panel = false;
    if (!panel) panel = true, dbz3::launcher::DeferUi(&CrearPanel);
    MunecoCuadro(base, st);
    OndasRival(base, st);
    if (st == 4) Avisos(base), PruebaCuadro(ctx, base), TransmisionInstantaneaTick(base);
    if (st >= 4 && st <= 7) DefinitivaLog(base);
    OtroDano(ctx, base, st);
    Sonda(base, st);
    Volcado(base);
  }
  orig(ctx, base);
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
  const bool has_teleport = Equipada(base, x, kCapTeleportInstant);
  const float ki_pre_hit = has_teleport ? LoadF(base, x + kKi) : 0.0f;
  if (has_teleport && ki_pre_hit < 3000.0f) {
    StoreF(base, x + kKi, 3000.0f);
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
  if (has_teleport) {
    const uint16_t act_now = REX_LOAD_U16(x + kAct);
    if (act_now == 39 || act_now == 40) {
      StoreF(base, x + kKi, ki_pre_hit);
    } else {
      StoreF(base, x + kKi, std::max(ki_pre_hit, LoadF(base, x + kKi)));
    }
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
  if (Equipada(base, x, kCapTeleportInstant)) {
    if (tg >= -15 && tg <= 20) {
      ctx.r3.u64 = 1;
    }
  }
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
  if (ult) g_ult[size_t(lado)] = {REX_LOAD_U16(node + 16), REX_LOAD_U16(node + 24), REX_LOAD_U16(node + 26)};
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
namespace dbz3::formas { void PruebaMando(uint8_t* base, uint32_t user); }   // transformaciones.cpp (prueba)
DBZ3_HOOK_EU(sub_8208FD58) {
  using namespace dbz3::combate;
  const uint32_t user = ctx.r3.u32;
  if (user == kMunecoPad && MunecoMandoActivo(base)) {
    MunecoMando(base);   // el mando del muneco no se lee: es el virtual
    return;
  }
  dbz3::iwmini::Mando(base, user, false);   // minijuegos de IW (frente P4): palanca relativa a la camara
  orig(ctx, base);
  dbz3::iwmini::Mando(base, user, true);
  dbz3::cinematica::Mando(base, user);   // escenas 3D (frente R3)
  if (user > 3 || BattleState(base) != 4) return;
  dbz3::formas::PruebaMando(base, user);   // frente transformaciones: P+K+G de prueba (DBZ3_TEST_PKG)
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

namespace dbz3::combate {
void SetMuneco(int modo) {   // 0 nada, 1 quieto, 2 guardia, 3 ataca, -1 el ajuste dbz3_combate_prueba_muneco
  if (g_muneco_api.exchange(modo) == modo) return;
  REXLOG_INFO("dbz3 combate: muneco {}", modo == 1 ? "quieto" : modo == 2 ? "en guardia" : modo == 3 ? "ataca" : modo == 0 ? "no" : "del ajuste");
}
}  // namespace dbz3::combate

// Marcador del combate (2026-10-16, frente P4). El objeto del marcador (*0x82324064) registra 4 tareas al crearse
// (sub_820DCBC0): "FIX MAIN" (sub_820DCA10), "FIX PREDRAW" (sub_820DC080), "FIX DRAW" (sub_820DC1A8, o sub_820DC230
// en el modo 6) y "FIX POSTDRAW" (sub_820DBB00). FIX DRAW pinta las barras, caras, "V", reloj (sub_820DBCF0) y los
// avisos (sub_820EB9F0) solo si mgr+35253 != 1 y 0x823CD870 tiene el bit alto: asi lo apaga el juego. OcultarHud
// salta esa tarea entera.
std::atomic<bool> g_ocultar_hud{false};
std::atomic<int64_t> g_fix_draw_ms{0};   // (frente R6) ultima vez que el juego llamo a FIX DRAW: HudNativoVisible
DBZ3_HOOK_EU(sub_820DC1A8) {
  g_fix_draw_ms = dbz3::combate::NowMs();
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820DC230) {
  g_fix_draw_ms = dbz3::combate::NowMs();
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
// (frente R14) Contadores de golpes y dano ("2 HITS!!", "80 DMG."): son tareas aparte por jugador, registradas en
// sub_820DF248 ("[FIX]DMG %dP" + "DMGDRW %dP" = sub_820DDCA0) y sub_820DF330 ("[FIX]CMB %dP" + "CMBDRW %dP" =
// sub_820DDFE8). OcultarHud salta tambien sus dibujos.
DBZ3_HOOK_EU(sub_820DDCA0) {
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820DDFE8) {
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
// y el segundo callback de cada tarea [FIX] (sub_820DDC20 de DMG y sub_820DDF68 de CMB: dos llamadas a sub_820ACCA0),
// que es el que pinta el contador mientras se desvanece (seguia viendose "48 DMG." del lado 2: R14 segunda vuelta)
DBZ3_HOOK_EU(sub_820DDC20) {
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
DBZ3_HOOK_EU(sub_820DDF68) {
  if (g_ocultar_hud.load()) return;
  orig(ctx, base);
}
// y los avisos del combate ("FIRST ATTACK!!", contraataque, rotura de guardia: tareas "[MSG]FATK/CNTR/GBREAK %dP"),
// que pinta la tarea "[MSGDRW]ID=%d" (sub_820DE490, registrada en sub_820DEAC8)
DBZ3_HOOK_EU(sub_820DE490) {
  if (g_ocultar_hud.load()) return;
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
