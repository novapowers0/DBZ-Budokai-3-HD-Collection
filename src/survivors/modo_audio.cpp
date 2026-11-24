// Dragon Budokai Survivors: sonido del modo (MezclaSe de Tactics, combate nativo de fondo y, con A16, musica de B3). Parte de modo.cpp (P0).
#include "modo_interno.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "audio_p3.h"
#include "../launcher/settings.h"
#include "../pasarela.h"
#include "../region.h"
#include "../xma_propio.h"

// A16 (P3): los dos ajustes de la ronda comercial se definen aqui (el coordinador los declara en modo_interno.h si los necesita).
REXCVAR_DEFINE_BOOL(dbz3_survivors_audio_v2, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: musica de batalla de B3 por escenario, sonidos de recogida y golpes del juego (sustituye a dbz3_survivors_sonido_combate)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_contenido_v2, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: luchadores, tecnicas, evoluciones, pasivas, enemigos y escenarios nuevos (res/survivors/survivors_v2.toml)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// SUR-07 (via C): golpes, explosiones y gritos del juego por el decodificador XMA propio (src/xma_propio.cpp), sin combate oculto.
// SUR-8 (decision del usuario): el combate oculto vuelve a estar ENCENDIDO de serie: es lo que mueve el motor real de efectos de B3 (fx_real.inl);
// con el, el sonido va por ese combate y no por el decodificador propio (SePropio() es falso: no se duplica). Apagado = decodificador propio, sin motor real de efectos.
REXCVAR_DEFINE_BOOL(dbz3_survivors_combate_oculto, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: sonido de combate por un combate oculto de fondo (encendido de serie: motor real de efectos de B3 y sonido del juego; apagado = decodificador propio, sin motor de efectos)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::survivors::interno {

namespace {

// ---- SUR-07: bancos del juego leidos de los AFS del jugador. data_cmn 3983 = banco 2 del combate (el de kSeImpacto); la voz de cada
// luchador es su banco de gritos de lang_usa/lang_jpn (char372 +0x24 & 0xFFFF, la misma entrada que carga el combate).
constexpr int kBancoCombate = 3983;
std::filesystem::path CarpetaRegion() {
  return dbz3::HostDataRoot() / (dbz3::settings::Region() == "eu" ? "eu" : "us");
}
int EntradaVoz(int b3) {
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base || b3 < 0 || b3 >= 64) return -1;
  const uint32_t fid = REX_LOAD_U32(dbz3::GA(0x82329CF0) + 372 * uint32_t(b3) + 0x24);
  return fid ? int(fid & 0xFFFF) : -1;
}
bool VocesJaponesas() {   // Opciones de B3: byte G + 0x2C0C9 (0 = japones), como video_sfd.cpp
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  return base && REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9) == 0;
}

bool SilencioPrueba() {
  static const bool s = std::getenv("DBZ3_TEST_SURVIVORS_SIN_SONIDO") || std::getenv("DBZ3_TEST_TACTICS_SIN_SONIDO");
  return s;
}

// ---- musica: pistas de adx_usa.afs de B3 HD (las 25 de 48 kHz estereo, entradas 122..146, de 77 a 287 s; el juego solo las usa en menus y
// escenas, no en el combate). Misma via que el selector de escenarios (OirMusicaPs2: reproductor propio, bucle de pista completa, en otro hilo).
// ponytail: tabla fija por id de escenario; el reparto es a ojo (no se ha escuchado) y se cambia aqui o con DBZ3_TEST_SURVIVORS_PISTA.
struct PistaEsc { const char* escenario; int normal, jefe; };
constexpr PistaEsc kPistas[] = {{"llanura", 126, 124}, {"desierto", 128, 132}, {"ruinas", 130, 145}, {"glaciar", 133, 146},
                                {"nave", 135, 132},    {"planeta", 127, 145}};
int g_musica = -1;     // entrada de adx_usa.afs que suena (-1 = ninguna)
int g_musica_log = 0;

int PistaDe(const char* esc, bool jefe) {
  if (const char* v = std::getenv("DBZ3_TEST_SURVIVORS_PISTA")) return std::atoi(v);
  for (const PistaEsc& p : kPistas)
    if (esc && std::strcmp(p.escenario, esc) == 0) return jefe ? p.jefe : p.normal;
  return jefe ? 124 : 126;
}

// ---- gema: tic en cadena (la racha de recogidas seguidas). Sin tono variable (el gestor de efectos no lo expone): la cadena cambia de id.
float g_t_tic = -100;   // tiempo real (ImGui) del ultimo tic
int g_cadena = 0;

// ---- sonda de bancos (DBZ3_TEST_SURVIVORS_SONDA=1): lectura de memoria, sin llamar al juego. Que hay en las ranuras del gestor de efectos.
bool g_sonda_hecha = false;
void SondaBancos() {
  static const auto t0 = std::chrono::steady_clock::now();   // el combate de fondo tarda unos segundos en cargar sus bancos: se mira a los 6 s
  if (g_sonda_hecha || !std::getenv("DBZ3_TEST_SURVIVORS_SONDA") || std::chrono::steady_clock::now() - t0 < std::chrono::seconds(6)) return;
  constexpr dbz3::GA kGestor = 0x82386260;
  constexpr uint32_t kSlotSz = 9320;
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!base) return;
  const uint32_t g = REX_LOAD_U32(kGestor);
  const auto ok = [](uint32_t a) { return a >= 0x10000000u && a < 0xC0000000u && (a & 3) == 0; };
  if (!ok(g)) return;
  g_sonda_hecha = true;
  for (uint32_t s = 0; s < 16; ++s) {
    const uint32_t e = g + 808 + s * kSlotSz, m = REX_LOAD_U32(e + 1568), estado = REX_LOAD_U32(e + 1564);
    if (!ok(m)) {
      REXLOG_INFO("dbz3 survivors: sonda: ranura {} vacia (estado {})", s, estado);
      continue;
    }
    const uint32_t n = REX_LOAD_U32(m + 0x0C);   // cabecera de banco de #AMB (b3hd-yell-gateway): +0x0C sonidos; registro de 0x100 B desde +0x100
    char txt[400] = {};
    int off = 0;
    for (uint32_t i = 0; i < std::min<uint32_t>(n, 24) && off < 360; ++i)
      off += std::snprintf(txt + off, sizeof txt - size_t(off), " %u:%u", i, REX_LOAD_U32(m + 0x100 + 0x100 * i + 0x14));
    REXLOG_INFO("dbz3 survivors: sonda: ranura {} estado {} banco {:08X} sonidos {} (id:bytes){}", s, estado, m, n, txt);
  }
}

}  // namespace

void AudioCuadro(bool partida, const char* esc, bool jefe) {
  const bool v2 = REXCVAR_GET(dbz3_survivors_audio_v2);
  const int quiero = v2 && partida ? PistaDe(esc, jefe) : -1;
  if (partida) SondaBancos();
  if (quiero == g_musica) return;
  if (g_musica >= 0 && !SilencioPrueba()) dbz3::pasarela::CallarMusicaPs2();
  if (g_musica >= 0 && g_musica_log++ < 40) REXLOG_INFO("dbz3 survivors: audio: musica cortada (era adx_usa {})", g_musica);
  g_musica = quiero;
  if (quiero < 0) return;
  const bool mudo = SilencioPrueba() || dbz3::settings::AudioMute();
  const float vol = mudo ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.6f * float(g_opciones.volumen) / 100.0f;   // auditoria #6: opcion Volumen propia
  if (g_musica_log++ < 40)
    REXLOG_INFO("dbz3 survivors: audio: suena musica {} escenario {} (adx_usa {}, vol {:.2f}{})", jefe ? "jefe" : "batalla", esc ? esc : "?", quiero, vol,
                SilencioPrueba() ? ", silenciado" : "");
  if (!SilencioPrueba()) dbz3::pasarela::OirMusicaPs2(dbz3::HostDataRoot(), "adx_usa.afs", quiero, vol);
}

void AudioFin() { AudioCuadro(false, nullptr, false); }

void Modo::AudioAlEvento(const Evento& e) {
  if (!REXCVAR_GET(dbz3_survivors_audio_v2)) return;
  if (e.tipo == TipoEvento::Recogida) {   // tic de gema: cadena si se recoge otra antes de 0,6 s (la cadena sube de id 2 a 4: moverse/aceptar...)
    if (t_ - g_t_tic > 0.6f) g_cadena = 0;
    if (t_ - g_t_tic < 0.07f) return;      // como mucho ~14 tics por segundo
    g_t_tic = t_;
    g_cadena = std::min(g_cadena + 1, 3);
    Sonar("gema", g_cadena >= 3 ? 3 : 2, 0.0f);
  } else if (e.tipo == TipoEvento::Objeto && TipoObjeto(e.a) == TipoObjeto::Zeni) {
    Sonar("zeni", 3, 0.05f);
  } else if (e.tipo == TipoEvento::CofreAbre) {
    Sonar("cofre_abre", 3, 0.0f);
  }
}

bool Modo::Silencio() { return SilencioPrueba(); }

void Modo::Sonar(const char* cual, int id, float min_s) {
  const bool apagado = Silencio();
  if (t_ - t_sonido_ < min_s) return;   // no satura: uno por fotograma del menu como mucho (el ultimo gana) y su propio limite
  t_sonido_ = t_;
  if (n_sonidos_++ < 40) REXLOG_INFO("dbz3 survivors: sonido {}{}", cual, apagado ? " (silenciado)" : "");
  if (REXCVAR_GET(dbz3_survivors_audio_v2) && n_sonidos_ <= 60) REXLOG_INFO("dbz3 survivors: audio: suena {} (sistema, id {})", cual, id);
  if (apagado) return;
  if (combate_ && !saliendo_) dbz3::combate::PedirSonido(0, id);   // en la pelea de fondo no hay menu principal que saque la cola de QueueSe
  else dbz3::hub::QueueSe(id);
}

bool Modo::SePropio() { return !REXCVAR_GET(dbz3_survivors_combate_oculto); }

void Modo::SonarCombate(const char* cual, tactics::PrioSe prio, double dura, int b3, int id, float& ultimo, float cada) {
  if (saliendo_ || Silencio() || t_ - ultimo < cada) return;
  if (SePropio()) {
    if (!se_.Pedir(t_, prio, dura)) return;   // misma mezcla de voces (6 a la vez)
    ultimo = t_;
    const int entrada = b3 < 0 ? kBancoCombate : EntradaVoz(b3);
    const std::filesystem::path afs = CarpetaRegion() / (b3 < 0 ? "data_cmn.afs" : VocesJaponesas() ? "lang_jpn.afs" : "lang_usa.afs");
    const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
    const bool ok = entrada >= 0 && dbz3::xma::Oir(afs, entrada, id, vol);
    if (n_se_combate_++ < 60)
      REXLOG_INFO("dbz3 survivors: audio: suena {} (propio: {} {} id {}{})", cual, afs.filename().string(), entrada, id, ok ? "" : ", NO CARGA");
    return;
  }
  if (!combate_) return;
  const int banco = b3 < 0 ? kSeImpacto : Ranura(b3);
  if (banco < 0 || !se_.Pedir(t_, prio, dura)) return;   // limite de voces a la vez (MezclaSe: 6, con sitio reservado a tecnicas y KO)
  ultimo = t_;
  if (n_se_combate_++ < 30) REXLOG_INFO("dbz3 survivors: sonido combate {} banco {} id {}", cual, banco, id);
  if (REXCVAR_GET(dbz3_survivors_audio_v2) && n_se_combate_ <= 60) REXLOG_INFO("dbz3 survivors: audio: suena {} (banco {} id {})", cual, banco, id);
  dbz3::combate::PedirSonido(banco, id);
}

void Modo::PedirFondo(int b3_jugador) {
  if (SePropio()) {   // SUR-07: sin combate oculto; se decodifican de antemano el banco de golpes y la voz del luchador y del jefe
    if (combate_pedido_ || Silencio()) return;
    combate_pedido_ = true;
    dbz3::xma::Precargar(CarpetaRegion() / "data_cmn.afs", kBancoCombate);
    const std::filesystem::path lang = CarpetaRegion() / (VocesJaponesas() ? "lang_jpn.afs" : "lang_usa.afs");
    for (const int b : {b3_jugador, B3Jefe()})
      if (EntradaVoz(b) >= 0) dbz3::xma::Precargar(lang, EntradaVoz(b));
    REXLOG_INFO("dbz3 survivors: sonido de combate propio (XMA del juego, sin combate oculto): golpes data_cmn {}, voces {} {} y {}", kBancoCombate,
                lang.filename().string(), EntradaVoz(b3_jugador), EntradaVoz(B3Jefe()));
    return;
  }
  // A16: con dbz3_survivors_audio_v2 el combate de fondo (golpes, gritos, KO del juego) va incluido; el ajuste antiguo sigue valiendo solo
  if (combate_ || combate_pedido_ || !(REXCVAR_GET(dbz3_survivors_sonido_combate) || REXCVAR_GET(dbz3_survivors_audio_v2)) || Silencio()) return;
  combate_pedido_ = true;
  const int jefe = B3Jefe();
  dbz3::hub::BattleModeNext(5);   // directo al READY, sin presentacion
  if (!dbz3::hub::RequestBattleEx(b3_jugador, jefe, kEscenarioFondo, true, -1, 0, 0, 0, 0)) {
    REXLOG_WARN("dbz3 survivors: el conector no acepto el combate de fondo: sin sonido de combate");
    return;
  }
  combate_ = true, g_fin_combate = false, fondo_b3_[0] = b3_jugador, fondo_b3_[1] = jefe;
  uint64_t m = 0;   // voces que faltan: los otros luchadores elegibles y los enemigos (las ranuras libres son 11)
  const auto pon = [&](int fid) {
    const int b = ModeloDe(fid).b3;
    if (b != fondo_b3_[0] && b != fondo_b3_[1] && b >= 0 && b < 64) m |= 1ull << b;
  };
  for (const DefPersonaje& p : datos_.personajes) pon(p.fid);
  for (const DefEnemigo& e : datos_.enemigos) pon(e.fid);
  dbz3::combate::CargarVoces(m);
  dbz3::combate::SetMuneco(1);
  dbz3::combate::OcultarHud(true);
  dbz3::pasarela::Reglas reglas;
  reglas.capsulas = false;
  dbz3::pasarela::VigilarCombate("survivors", reglas, [](const dbz3::pasarela::Fin&) {
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    g_fin_combate = true;
  });
  REXLOG_INFO("dbz3 survivors: combate de fondo pedido para el sonido (luchador {} contra {}, escenario {}, voces {:x})", b3_jugador, jefe, kEscenarioFondo, m);
}

void Modo::VigilarFondo() {
  if (!g_fin_combate.load() || saliendo_) return;
  combate_ = false;   // acabo solo (no deberia): la partida sigue con los sonidos del menu
  dbz3::combate::CargarVoces(0);
  REXLOG_WARN("dbz3 survivors: el combate de fondo acabo solo: sigue sin sonido de combate");
}

}  // namespace dbz3::survivors::interno

// SUR2/02: test del combate visible de B3, no una recreacion en Survivors.
// Solo observa los argumentos de la VM y del gestor nativo, antes del original.
namespace {
bool TrazaDefinitivas() {
  static const bool activo = [] {
    const char* v = std::getenv("DBZ3_TEST_SURVIVORS_ULT_TRAZA");
    return v && std::strcmp(v, "1") == 0;
  }();
  return activo;
}
std::atomic_uint g_ult_secuencia{0}, g_ult_ace{0}, g_ult_se{0};
const char* RegionDefinitivas() { return dbz3::g_guest_eu ? "eu" : "us"; }
bool ArgsDefinitivas(uint32_t p, uint32_t bytes) {
  // Misma ventana general de punteros que usa el puente FX; el original
  // sigue siendo quien posee y valida el contexto de la VM.
  return p >= 0x10000000u && p <= 0xE0000000u - bytes && (p & 3u) == 0;
}
}  // namespace

// Builtin 0x165 (357 decimal). La tabla de cada XEX da su propia entrada.
DBZ3_HOOK(sub_821289B0, dbz3eu_sub_82128AB8) {
  if (TrazaDefinitivas() && ArgsDefinitivas(ctx.r4.u32, 48)) {
    const uint32_t p = REX_LOAD_U32(ctx.r4.u32 + 44);
    if (ArgsDefinitivas(p, 20) && g_ult_ace.fetch_add(1, std::memory_order_relaxed) < 128)
      REXLOG_INFO("dbz3 sur2 ult: ace seq={} region={} lado={} ranura={} banco={} indice={} tipo={:04X} ancla={}",
                  g_ult_secuencia.fetch_add(1, std::memory_order_relaxed), RegionDefinitivas(),
                  REX_LOAD_U32(p + 12), REX_LOAD_U32(p + 16), REX_LOAD_U32(p + 4), REX_LOAD_U32(p),
                  REX_LOAD_U32(p + 8) & 0xffffu, REX_LOAD_U32(p + 8) == 0x400u ? "grupo_escena" : "otro");
  }
  orig(ctx, base);
}

// Entrada del gestor de SE, aguas abajo de PlaySe generico. Una peticion
// puede ser rechazada por el original: el registro no confirma reproduccion.
DBZ3_HOOK(sub_8209E008, dbz3eu_sub_8209DD40) {
  if (TrazaDefinitivas() && ctx.r4.u32 > 0 && ctx.r4.u32 < 16 &&
      g_ult_se.fetch_add(1, std::memory_order_relaxed) < 384)
    REXLOG_INFO("dbz3 sur2 ult: se_peticion seq={} region={} banco={} id={} lr={:08X}",
                g_ult_secuencia.fetch_add(1, std::memory_order_relaxed), RegionDefinitivas(), ctx.r4.u32, ctx.r5.u32,
                uint32_t(ctx.lr));
  orig(ctx, base);
}
