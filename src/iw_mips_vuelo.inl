// dbz3 - T3 (8/11): motor MgFly de IW (EU 0x366410-0x369370) con su logica ORIGINAL. Incluido por src/iw_mips.cpp dentro de
// dbz3::iwmips::bubbles (anonimo), despues de CampoDC. Interfaz con el port: src/iw_mips_vuelo.h.
//
// 0x7D3 (ladrillos). Por campo PAL se ejecutan del ELF, en este orden (el del port, medido contra PCSX2 por astraIW):
//   1. la tarea de estados: el estado actual (MgFly_StateFlyTick 0x367C10, ...SegmentProgress 0x367990, ...LoadNodeStart,
//      ...SegmentDoneSchedule) con la tarea del contexto (ctx+0x40) como argumento;
//   2. StartFunc 0x366F60 (la mira: palanca/cruceta, fijado y iman, disparo, acierto).
// El contexto (0x700 B en 0x49CEAC), el #MSS y la tarea viven en el invitado entre cuadros. Se entra directamente en el
// estado de vuelo del primer ladrillo (el port pinta la presentacion). Puentes mudos: carga de efectos, sonido, dialogos y
// contadores de pantalla; eventos hacia el port: 'S' disparo, 'K' aviso de Kaio (Timelapse...), 'F' fin.
//
// Pendiente (ver studies 5/ENTREGAS/TANDA3_IW_VUELO.md): USA (direcciones sin emparejar) y 0x7D6/0x7E0.

#ifndef IWV_DECL_HECHA
#define IWV_DECL_HECHA
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>
namespace dbz3::iwmips::mgfly {
struct Evento { char tipo = 0; int n = 0; };   // 'K' aviso de Kaio n, 'S' disparo, 'F' fin (n = rango)
struct Entrada {
  bool inicio = false;               // primer cuadro: monta el espejo y prepara el primer ladrillo
  const uint8_t* mss = nullptr;      // #MSS 890/4 crudo
  size_t mss_n = 0;
  int dif = 2;                       // indice de bloque del #MSS (ya acotado a los que trae el fichero)
  uint16_t mantenido = 0, nuevo = 0; // botones de IW apretados / recien pulsados
  int8_t palanca_x = 0, palanca_y = 0;
  bool aviso_fin = false;            // el aviso de Kaio que pidio un evento 'K' ya termino
  uint64_t t = 0;
};
struct Salida {
  float mira[2]{}, blanco[2]{}, prof = 0, progreso = 0, duracion = 0, iman = 0;
  float pos3[3]{}, pos3b[3]{};       // +0x520 (posicion actual) y +0x530/+0x540 (inicio/fin) del ladrillo
  int fijado = 0, activo = 0, tiros = 0, aciertos = 0, quedan = 0, rango = 0;
  bool fin = false;                  // el original llego a su estado final (EndCutsceneLine1/2)
  uint32_t estado = 0;
  int n_ev = 0;
  Evento ev[16];
};
inline bool (*g_ladrillos)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa_lad)() = nullptr;   // cvar dbz3_iw_7d3_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace dbz3::iwmips::mgfly

// T4B (8/11): vuelo de IW (0x7D6 Vegeta / 0x7E0 Goten, modulo Viewer_StateFly*) con su logica ORIGINAL.
namespace dbz3::iwmips::vuelo7 {
struct Entrada {
  bool inicio = false;               // primer cuadro: monta el contexto del vuelo con el #MSF
  const uint8_t* msf = nullptr;      // #MSF crudo (DATA_CMN 891 hijo 23 + v)
  size_t msf_n = 0;
  int v = 0, dif = 2, pj = 0;        // variante (0 Vegeta, 1 Goten), dificultad, personaje
  uint16_t botones = 0;              // bits del mando de PS2 (R1 = 0x800)
  int8_t lx = 0, ly = 0, rx = 0;     // palanca izquierda X/Y y derecha X (bytes de PS2)
  uint32_t anim_cod = 0;             // animacion que el port reproduce ahora en el cuerpo del jugador
  float anim_cuadro = 0;             // su cuadro actual
  int anim_cuadros = 0;              // y su numero de cuadros
  bool congelar = false;             // el efecto del objeto 8 llego al rival: ctx+0x520 = #MSF bloque +0x24
  uint64_t t = 0;
};
struct Salida {
  float x = 0, z = 0, vel = 0, t_ms = 0, medidor = 0, congelado = 0, t568 = 0, t56c = 0;
  uint32_t yaw = 0, anim = 0;
  int estado = 0, monedas = 0, quedan = 0, n_obj = 0;
  bool meta = false;                 // el original llamo a Viewer_BeginFinalPhase (cruzar la meta)
  uint16_t flags[256]{};              // flags de los objetos: [0] = meta, luego L1 y L2; bit 0x8000 = oculto/cogido
};
inline bool (*g_vuelo)(const Entrada&, Salida&) = nullptr;
inline bool (*g_activa_vuelo)() = nullptr;
}  // namespace dbz3::iwmips::vuelo7
#endif

#ifndef IWV_SOLO_DECL
#ifndef IWV_IMPL_HECHA
#define IWV_IMPL_HECHA
REXCVAR_DEFINE_BOOL(dbz3_iw_7d3_original, true, "DBZ3/IW",
                    "IW 0x7D3 (ladrillos): mira, fijado, disparo y tramos con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
namespace dbz3::iwmips::bubbles {
namespace {
// ---------------- T3 (8/11) MgFly 0x7D3 ----------------
constexpr uint32_t kCtxL = 0x1180000, kMssL = 0x1181000, kFlagsL = 0x1186000, kTareaL = 0x1187000;
constexpr Dueno kDMgFly = Dueno(12);   // el enum llega a 8; 15, 14 y 13 son de iw-botones
// direcciones EU y USA (USA: pc_eu_a_usa + cuerpo comparado sin inmediatos con tools de T2B/verifica_usa.py, 28/28 OK; datos
// usa_datos: ctx 0x49C690 por el lw -0x6F60(gp) de StartFunc USA)
struct DirMg {
  uint32_t ctxp, est_fly, fin1, fin2, start, setup, aura, timelapse, table, ancho, alto, a834, a838, a830, mat, foc;
  uint32_t mudos[17];
};
constexpr DirMg kMgEU = {0x49CEAC, 0x367C10, 0x3676D0, 0x367850, 0x366F60, 0x366700, 0x33A950, 0x346E00, 0x31B3A0,
                         0x49C84C, 0x49C850, 0x49C834, 0x49C838, 0x49C830, 0x4AB4A0, 0x4AB64C,
                         {0x346580, 0x346600, 0x3466B0, 0x3466C0, 0x346E80, 0x346F40, 0x13F2B0, 0x13F0F0, 0x2EFA10, 0x1265F0,
                          0x126CA0, 0x126B70, 0x342090, 0x11DEF8, 0x303010, 0x33A870, 0x33A950}};
constexpr DirMg kMgUSA = {0x49C690, 0x367D50, 0x367810, 0x367990, 0x3670A0, 0x366840, 0x33A450, 0x346900, 0x31B0B0,
                          0x49C04C, 0x49C050, 0x49C034, 0x49C038, 0x49C030, 0x4AACA0, 0x4AAE4C,
                          {0x346080, 0x346100, 0x3461B0, 0x3461C0, 0x346980, 0x346A40, 0x13F2B0, 0x13F0F0, 0x2EF800, 0x1265F0,
                           0x126CA0, 0x126B70, 0x341B90, 0x11DEF8, 0x302DF0, 0x33A370, 0x33A450}};

bool ActivaLad() { return REXCVAR_GET(dbz3_iw_7d3_original) && Listo() != nullptr; }

bool CampoLadrillos(const mgfly::Entrada& in, mgfly::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7d3_original)) return false;
  const DirMg& D = freezer::g_ntsc ? kMgUSA : kMgEU;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  out = mgfly::Salida{};
  Ambito amb(*st, kDMgFly);
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips mgfly: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  auto ev = [&](char t, int n) { if (out.n_ev < 16) out.ev[out.n_ev++] = {t, n}; };
  // solo pruebas: DBZ3_TEST_MGFLY_REPLAY=<txt de mgfly.py repetir>: semilla y mando de cada cuadro del dorado de PCSX2
  mgfly::Entrada en = in;
  static std::vector<std::array<long long, 5>> rep;
  static size_t rep_i = 0;
  static std::vector<uint32_t> rep_ini;   // semilla, ctx 0x4B0..0x55F (44), tarea +0x1C..+0x3C (8)
  static std::vector<uint8_t> rep_flags;
  if (const char* rp = std::getenv("DBZ3_TEST_MGFLY_REPLAY")) {
    if (rep.empty()) {
      std::ifstream f(rp);
      long long v[5];
      for (int i = 0; i < 1 + 44 + 8; ++i) { std::string h; f >> h; rep_ini.push_back(uint32_t(std::stoul(h, nullptr, 16))); }
      for (int i = 0; i < 64; ++i) { std::string h; f >> h; rep_flags.push_back(uint8_t(std::stoul(h, nullptr, 16))); }
      while (f >> v[0] >> v[1] >> v[2] >> v[3] >> v[4]) rep.push_back({v[0], v[1], v[2], v[3], v[4]});
    }
    if (in.inicio) rep_i = 0;
    if (!in.inicio && rep_i < rep.size()) {
      const auto& r = rep[rep_i++];
      { en.mantenido = uint16_t(r[1]); en.nuevo = uint16_t(r[2]); en.palanca_x = int8_t(r[3]); en.palanca_y = int8_t(r[4]); semilla = uint32_t(r[0]); }
    }
  }
  for (uint32_t a : D.mudos) amb.Mudo({a});
  amb.Puente(D.aura, [&](Ee& c) { if ((c.r[4] & 0xFF) == 7) ev('S', 0); });          // Aura_Dash(7, ...): el disparo
  amb.Puente(D.timelapse, [&](Ee& c) {   // Timelapse/Vegeta-GotenMeter: aviso de Kaio n; el estado espera ctx+0x6F8 == -1
    e.W32(kCtxL + 0x6F8, 0);
    ev('K', int(c.r[4] & 0xFF));
  });
  amb.Puente(D.table, [&](Ee& c) { V0(c, 0); });   // Table_Lookup480450
  if (in.inicio) {
    if (!in.mss || in.mss_n < 0x70 || in.mss_n > 0x4000) return false;
    e.Cero(kCtxL, 0x800); e.Cero(kMssL, 0x4000); e.Cero(kFlagsL, 0x100); e.Cero(kTareaL, 0x100);
    for (size_t i = 0; i < in.mss_n; ++i) e.W8(kMssL + uint32_t(i), in.mss[i]);
    // el cargador del juego convierte los desplazamientos de bloque (+0x2C...) en punteros
    for (uint32_t k = 0; k < 6; ++k) {
      const uint32_t v = e.R32(kMssL + 0x2C + 4 * k);
      if (v && v < in.mss_n) e.W32(kMssL + 0x2C + 4 * k, kMssL + v);
    }
    const uint32_t blq = e.R32(kMssL + 0x2C + 4 * uint32_t(in.dif));
    if (blq < kMssL || blq + 0x64 > kMssL + in.mss_n) return false;
    // proyeccion de la escena (medida en PCSX2: focal 512 sobre el lienzo 600 x 450 de PS2; ver mgfly.py)
    e.Wf(D.ancho, 600.f); e.Wf(D.alto, 450.f); e.W32(D.a834, 512); e.W32(D.a838, 448); e.Wf(D.a830, 448.f);
    e.Wf(D.foc, 512.f);
    // matriz de proyeccion de la escena, tal como la deja MgFly_StateIntroSetup (volcada de PCSX2: 436,9 = 512*512/600; 509,72 = 512*448/450)
    constexpr uint32_t mw[16] = {0x43DA740E, 0, 0, 0, 0, 0x43FEDCBB, 0, 0, 0x45000000, 0x45000000, 0xC1C4B6EF, 0x3F800000, 0, 0, 0x49CCB653, 0};
    for (uint32_t i = 0; i < 16; ++i) e.W32(D.mat + 4 * i, mw[i]);
    // MgFly_LoadWaitPlayerFlyingMinigame (0x368230): estado inicial del contexto
    e.W32(D.ctxp, kCtxL);
    e.W32(kCtxL + 0x40, kTareaL);
    e.W32(kCtxL + 0x4BC, uint32_t(in.dif));
    e.W32(kCtxL + 0x4C0, kMssL); e.W32(kCtxL + 0x4C4, blq);
    e.W32(kCtxL + 0x4C8, 0); e.W32(kCtxL + 0x4D0, 7); e.W32(kCtxL + 0x4CC, e.R32(blq));
    e.W32(kCtxL + 0x4DC, 0); e.W32(kCtxL + 0x4E4, 0);
    e.Wf(kCtxL + 0x4EC, 300.f); e.Wf(kCtxL + 0x4F0, 225.f);
    e.Wf(kCtxL + 0x520, 0.f); e.Wf(kCtxL + 0x524, -1000.f); e.Wf(kCtxL + 0x528, 0.f); e.Wf(kCtxL + 0x52C, 1.f);
    e.Wf(kCtxL + 0x4F4, -1000.f); e.Wf(kCtxL + 0x4F8, -1000.f);
    e.Wf(kCtxL + 0x500, 1.f); e.Wf(kCtxL + 0x508, 0.f); e.Wf(kCtxL + 0x504, 0.f); e.Wf(kCtxL + 0x50C, 1200.f);
    e.Wf(kCtxL + 0x514, 0.f); e.W32(kCtxL + 0x4FC, 0xFFFFFFFFu);
    e.W32(kCtxL + 0x4D4, e.R32(blq + 0x60)); e.W32(kCtxL + 0x4D8, kFlagsL);
    e.W32(kCtxL + 0x4E8, 2); e.Wf(kCtxL + 0x554, 90.f);
    e.W32(kCtxL + 0x6F8, 0xFFFFFFFFu);
    e.Wf(kPasoT, std::getenv("DBZ3_TEST_MGFLY_REPLAY") ? 1.2f : kPasoCampo);   // el dorado de PCSX2 es PAL
    e.W32(kSemilla, semilla);
    // MgFly_StateNodeAnimWait, rama "animacion acabada": cuenta atras, siguiente nodo y a volar
    bool ok = e.Llamar(D.setup, {});
    e.W32(kCtxL + 0x4CC, e.R32(kCtxL + 0x4CC) - 1);
    e.W32(kCtxL + 0x4E8, 0);
    e.W32(kTareaL + 0x1C, D.est_fly);
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips mgfly: {} al preparar; se usa el port", e.error());
      st->dueno = kNadie;
      return false;
    }
    if (!rep_ini.empty()) {   // pruebas: estado del dorado en su primer cuadro (la captura empezo con el ladrillo ya en vuelo)
      for (uint32_t j = 0; j < 44; ++j) {
        const uint32_t off = 0x4B0 + 4 * j;
        if (off == 0x4B0 || off == 0x4C0 || off == 0x4C4 || off == 0x4D8) continue;
        e.W32(kCtxL + off, rep_ini[1 + j]);
      }
      for (uint32_t j = 0; j < 8; ++j) e.W32(kTareaL + 0x1C + 4 * j, rep_ini[45 + j]);
      if (&D == &kMgUSA && e.R32(kTareaL + 0x1C) == kMgEU.est_fly) e.W32(kTareaL + 0x1C, D.est_fly);   // el dorado es EU
      for (uint32_t j = 0; j < rep_flags.size(); ++j) e.W8(kFlagsL + j, rep_flags[j]);
      e.W32(kSemilla, rep_ini[0]);
    }
    semilla = e.R32(kSemilla);
  } else {
    e.Wf(kPasoT, std::getenv("DBZ3_TEST_MGFLY_REPLAY") ? 1.2f : kPasoCampo);   // el dorado de PCSX2 es PAL
    e.W32(kSemilla, semilla);
    if (in.aviso_fin) e.W32(kCtxL + 0x6F8, 0xFFFFFFFFu);
    // mando: apretados 0x4ADECC, nuevos 0x4ADECE, palancas 0x4ADEDA/DB (StartFunc las lee de la ranura del mando 0)
    e.W16(kFlancoDA - 6, en.mantenido); e.W16(kFlancoDA - 4, en.nuevo);
    e.W8(kFlancoDA + 8, uint8_t(en.palanca_x)); e.W8(kFlancoDA + 9, uint8_t(en.palanca_y));
    const Cronometro cr(e);
    const uint32_t fn = e.R32(kTareaL + 0x1C);
    bool ok = true;
    if (fn != D.fin1 && fn != D.fin2 && fn) ok = e.Llamar(fn, {kTareaL});
    const uint32_t fn2 = e.R32(kTareaL + 0x1C);
    if (ok && fn2 != D.fin1 && fn2 != D.fin2) ok = e.Llamar(D.start, {});
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips mgfly: {} (t={}); la partida sigue con el port", e.error(), in.t);
      st->dueno = kNadie;
      return false;
    }
    semilla = e.R32(kSemilla);
    st->Cuenta(cr.Us());
  }
  const uint32_t fin = e.R32(kTareaL + 0x1C);
  out.estado = fin;
  out.fin = fin == D.fin1 || fin == D.fin2;
  out.mira[0] = e.Rf(kCtxL + 0x4EC); out.mira[1] = e.Rf(kCtxL + 0x4F0);
  out.blanco[0] = e.Rf(kCtxL + 0x4F4); out.blanco[1] = e.Rf(kCtxL + 0x4F8);
  out.prof = e.Rf(kCtxL + 0x528); out.progreso = e.Rf(kCtxL + 0x508); out.duracion = e.Rf(kCtxL + 0x50C);
  out.iman = e.Rf(kCtxL + 0x550);
  for (int i = 0; i < 3; ++i) {
    out.pos3[i] = e.Rf(kCtxL + 0x520 + 4 * uint32_t(i));
    out.pos3b[i] = e.Rf(kCtxL + 0x540 + 4 * uint32_t(i));
  }
  out.fijado = int(e.R32(kCtxL + 0x4E4)); out.activo = int(e.R32(kCtxL + 0x4FC)) != -1;
  out.tiros = int(e.R32(kCtxL + 0x4D0)); out.aciertos = int(e.R32(kCtxL + 0x4DC));
  out.quedan = int(e.R32(kCtxL + 0x4CC)); out.rango = int(e.R32(kCtxL + 0x4E0));
  if (out.fin) ev('F', out.rango);
  if (std::getenv("DBZ3_TEST_IWMINI")) {   // solo pruebas: mgfly.py juego <dbz3.log> la reejecuta en ee.py
    std::string w;
    for (uint32_t i = 0x4B0; i < 0x560; i += 4) Hx(w, e.R32(kCtxL + i));
    REXLOG_INFO("dbz3 iwmips mgfly t={} ini={} in={:X},{:X},{},{} seed={:08X} est={:X} ctx={}", in.t, int(in.inicio), in.mantenido,
                in.nuevo, int(in.palanca_x), int(in.palanca_y), semilla, fin, w);
  }
  return true;
}

const bool kRegistradoMgFly = (mgfly::g_ladrillos = &CampoLadrillos, mgfly::g_activa_lad = &ActivaLad, true);
}  // namespace
}  // namespace dbz3::iwmips::bubbles

// ---------------- T4B (8/11) vuelo 0x7D6 / 0x7E0 ----------------
REXCVAR_DEFINE_BOOL(dbz3_iw_vuelo_original, true, "DBZ3/IW",
                    "IW 0x7D6/0x7E0 (vuelo): giro, estados, medidor y objetos con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
namespace dbz3::iwmips::bubbles {
namespace {
constexpr uint32_t kCtxV = 0x1180000, kMsfV = 0x1181000, kObjV = 0x11A0000, kTareaV = 0x1187000, kTarea2V = 0x1188000,
                   kModV = 0x1190000, kGridV = 0x1195000;
struct DirV {
  uint32_t ctxp, paso, cnt, grid, pad, padidx;
  uint32_t est[8];          // Update0, 2, 3, 4, 5, 6, 7 y s_WAIST
  uint32_t finpose, reset;  // Viewer_StateEnterFinalPose, Viewer_ResetToIdlePose
  uint32_t upd_obj, scripts, setanim, avanza, final_fase;
  uint32_t mudos[42];
};
// EU 0x33xxxx..; USA por pc_eu_a_usa + cuerpo comparado sin inmediatos (T2B/verifica_usa.py, 8/11: todas OK)
constexpr DirV kVEU = {0x49CE3C, 0x49C7DC, 0x49C7E4, 0x49CDB8, 0x4ADECC, 0x5C14B4,
                       {0x340D70, 0x340AA0, 0x3403A0, 0x3407D0, 0x33FEF0, 0x33FA10, 0x340CB0, 0x33F810}, 0x33EB30, 0x341420,
                       0x33B7D0, 0x33C660, 0x2666C0, 0x266310, 0x33B2C0,
                       {0x11DEF8, 0x135370, 0x1353D0, 0x135D30, 0x135F50, 0x13F6E0, 0x13F3E0, 0x277EA0, 0x304D90, 0x33A910,
                        0x33A950, 0x339DD0, 0x126CA0, 0x126B70, 0x13F2B0, 0x13F0F0, 0x2EFA10, 0x1265F0, 0x303010, 0x33A870,
                        0x140750, 0x1056B0, 0x277E90, 0x2EF9E0, 0x33A8C0, 0x136E40, 0x127CA0, 0x346E00, 0x346580, 0x346600,
                        0x3466B0, 0x3466C0, 0x346E80, 0x346F40, 0x346AA0, 0x346760, 0x342C60, 0x1B5720, 0x1B57B0, 0x31B3C0,
                        0x298380, 0x135420}};
constexpr DirV kVUSA = {0x49C620, 0x49BFDC, 0x49BFE4, 0x49C598, 0x4AD6CC, 0x5C0CB4,
                        {0x340870, 0x3405A0, 0x33FEA0, 0x3402D0, 0x33F9F0, 0x33F510, 0x3407B0, 0x33F310}, 0x33E630, 0x340F20,
                        0x33B2D0, 0x33C160, 0x2664B0, 0x266100, 0x33ADC0,
                        {0x11DEF8, 0x135370, 0x1353D0, 0x135D30, 0x135F50, 0x13F6E0, 0x13F3E0, 0x277C90, 0x304B70, 0x33A410,
                         0x33A450, 0x3398D0, 0x126CA0, 0x126B70, 0x13F2B0, 0x13F0F0, 0x2EF800, 0x1265F0, 0x302DF0, 0x33A370,
                         0x140750, 0x1056B0, 0x277C80, 0x2EF7D0, 0x33A3C0, 0x136E40, 0x127CA0, 0x346900, 0x346080, 0x346100,
                         0x3461B0, 0x3461C0, 0x346980, 0x346A40, 0x3465A0, 0x346260, 0x342760, 0x1B5720, 0x1B57B0, 0x31B0D0,
                         0x2980F0, 0x135420}};

int g_sin_pose = 0;
float g_reloj_v = 0;
bool ActivaVuelo() { return REXCVAR_GET(dbz3_iw_vuelo_original) && Listo() != nullptr; }

bool CampoVuelo(const vuelo7::Entrada& in, vuelo7::Salida& out) {
  if (!REXCVAR_GET(dbz3_iw_vuelo_original)) return false;
  const DirV& D = freezer::g_ntsc ? kVUSA : kVEU;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  out = vuelo7::Salida{};
  Ambito amb(*st, kDMgFly);
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips vuelo: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  for (uint32_t a : D.mudos) amb.Mudo({a});
  amb.Puente(D.setanim, [&](Ee& c) {   // CharModel_SetAnimBlend(a, b, modelo, codigo, bucle): el port pone la pose
    const uint32_t cod = uint32_t(c.r[7]);
    e.W32(kModV + 0x27C, cod);
    g_sin_pose = 0; g_reloj_v = 0;
    e.Wf(kModV + 0x270, 0.f); e.W32(kModV + 0x278, 1000000);   // hasta que el port informe de la suya
    c.r[2] = 0;
  });
  amb.Mudo({D.avanza});                // FuncToDefine: avanza el port
  amb.Puente(D.final_fase, [&](Ee&) { out.meta = true; e.W32(kCtxV + 0x530, 0); });   // Viewer_BeginFinalPhase
  const uint32_t G = kCtxV;
  if (in.inicio) {
    if (!in.msf || in.msf_n < 0xC0 || in.msf_n > 0x4000) { REXLOG_WARN("dbz3 iwmips vuelo: #MSF de {} B", in.msf_n); return false; }
    e.Cero(G, 0x800); e.Cero(kMsfV, 0x4000); e.Cero(kObjV, 0x2000); e.Cero(kTareaV, 0x100); e.Cero(kTarea2V, 0x100);
    e.Cero(kModV, 0x400); e.Cero(kGridV, 0xC00);
    for (size_t i = 0; i < in.msf_n; ++i) e.W8(kMsfV + uint32_t(i), in.msf[i]);
    auto rel = [&](uint32_t a) { const uint32_t x = e.R32(a); if (x) e.W32(a, kMsfV + x); };
    for (uint32_t k = 0; k < 6; ++k) rel(kMsfV + 0xA8 + 4 * k);
    for (uint32_t k = 0; k < 6; ++k) {
      const uint32_t blk = e.R32(kMsfV + 0xA8 + 4 * k);
      if (blk < kMsfV || blk + 0x18 > kMsfV + in.msf_n) { REXLOG_WARN("dbz3 iwmips vuelo: bloque {} fuera ({:X})", k, blk); return false; }
      for (uint32_t j = 0; j < 6; ++j) rel(blk + 4 * j);
    }
    e.W32(D.ctxp, G);
    e.W32(G + 0x4F0, uint32_t(in.v)); e.W32(G + 0x4F4, uint32_t(in.pj)); e.W32(G + 0x4F8, uint32_t(in.dif));
    e.W32(G + 0x4FC, kMsfV);
    const uint32_t blk = e.R32(kMsfV + 0xA8 + 4 * uint32_t(in.dif));
    e.W32(G + 0x500, blk);
    e.W32(G + 0x504, e.R8(kMsfV + 0x10));
    e.W32(G + 0x548, kModV); e.W32(G + 0x58, kTareaV);
    // rejilla del mundo (iRam0049cdb8): Node_WrapPositionInGrid; 1 celda de lado = periodo del port (v0 16000, v1 24000)
    const float per = in.v == 0 ? 16000.f : 24000.f;
    e.W32(D.grid, kGridV);
    e.Wf(kGridV + 0xAD4, per); e.Wf(kGridV + 0xAD8, per); e.W32(kGridV + 0xADC, 1); e.W32(kGridV + 0xAE0, 1);
    e.Wf(kGridV + 0xAF0, 0.f); e.Wf(kGridV + 0xAF8, 0.f); e.W32(kGridV + 0x630, 3);
    e.W32(D.padidx, 0);
    // tabla de objetos (FlyingMinigameFuncs_ 0x33CE40): meta, L1 y L2; 24 B cada uno
    const uint32_t salida = e.R32(blk), meta = e.R32(blk + 4), l1 = e.R32(blk + 8), l2 = e.R32(blk + 12);
    const uint32_t n1 = e.R32(l1), n2 = e.R32(l2);
    if (n1 > 250 || n2 > 250 || n1 + n2 + 1 > 256) { REXLOG_WARN("dbz3 iwmips vuelo: listas {} {}", n1, n2); return false; }
    e.W32(G + 0x510, n1 + 1 + n2); e.W32(G + 0x514, kObjV);
    const uint32_t vis = e.R32(G + 0x504);
    auto reg = [&](uint32_t i, uint32_t ent, int forzar) {
      const uint32_t w0 = e.R32(ent), tipo = w0 >> 16;
      uint32_t fl = w0 & 0xFFFF;
      if (forzar >= 0) fl = uint32_t(forzar);
      const uint32_t a = kObjV + 24 * i;
      e.W32(a, fl | (tipo << 16)); e.W32(a + 4, e.R32(ent + 4)); e.W32(a + 8, e.R32(ent + 8));
      e.W32(a + 0xC, e.R32(kMsfV + (tipo & 0xFF) * 4 + 0x60)); e.W32(a + 0x10, 0); e.W32(a + 0x14, 0xDEAD0000u + i);
    };
    reg(0, meta, -1);
    for (uint32_t i = 0; i < n1; ++i) {
      const uint32_t ent = l1 + 4 + 12 * i;
      uint32_t fl = e.R16(ent);
      if (vis == 1) fl = (fl & 0x7FFF) | 1;
      else if (vis == 0) fl = i == 0 ? ((fl & 0x7FFF) | 1) : ((fl & 0xFFFE) | 0x8000);
      reg(1 + i, ent, int(fl));
    }
    for (uint32_t i = 0; i < n2; ++i) reg(1 + n1 + i, l2 + 4 + 12 * i, -1);
    e.W32(G + 0x50C, n1); e.W32(G + 0x508, n1);
    // posicion de salida (FlyingMinigameCharacterHeightPosition)
    const uint32_t est = e.R32(salida), giro = e.R32(salida + 4);
    e.W32(G + 0x5C0, e.R32(salida + 8)); e.Wf(G + 0x5C4, 400.f); e.W32(G + 0x5C8, e.R32(salida + 12)); e.Wf(G + 0x5CC, 1.f);
    for (uint32_t o : {0x5D0u, 0x5E0u, 0x5F0u})
      for (uint32_t k = 0; k < 4; ++k) e.W32(G + o + 4 * k, e.R32(G + 0x5C0 + 4 * k));
    e.W32(G + 0x55C, giro); e.W32(G + 0x558, 0); e.W32(G + 0x560, 0);
    e.W32(G + 0x51C, e.R32(blk + 0x14)); e.W32(G + 0x518, 0);
    e.W32(G + 0x554, 0x320); e.W32(G + 0x570, est);
    e.W32(kTareaV + 0x1C, est == 3 ? D.est[2] : D.est[0]);
    e.W32(G + 0x550, 0);
    e.W32(kTarea2V + 0x1C, D.scripts);
    e.Wf(kModV + 0x270, 0.f); e.W32(kModV + 0x278, 1000000);
  }
  const bool ntsc = freezer::g_ntsc;
  e.Wf(D.paso, ntsc ? 1.0f : 1.2f);
  e.W32(D.cnt, ntsc ? 60u : 50u);
  // mando: R1 en los bits de apretados; palancas en los bytes 0x4ADED8 (X izq), 0x4ADEDA (X der), 0x4ADEDB (Y izq)
  e.W16(D.pad, in.botones); e.W16(D.pad + 2, in.botones);
  e.W8(D.pad + 0xC, uint8_t(in.lx)); e.W8(D.pad + 0xE, uint8_t(in.rx)); e.W8(D.pad + 0xF, uint8_t(in.ly));
  // animacion del cuerpo: el original pregunta por el cuadro (+0x270) y el numero de cuadros (+0x278) del modelo
  if (!in.inicio) {
    const uint32_t pedida = e.R32(kModV + 0x27C);
    // g_sin_pose: cuadros que el port lleva sin reproducir la pedida (pose que no tiene): g_reloj_v propio de 45 cuadros
    if (in.anim_cod == pedida && in.anim_cuadros > 0) {
      g_sin_pose = 0; g_reloj_v = 0;
      e.Wf(kModV + 0x270, in.anim_cuadro); e.W32(kModV + 0x278, uint32_t(in.anim_cuadros));
    } else if (++g_sin_pose > 3) {
      g_reloj_v += 1.0f;
      e.Wf(kModV + 0x270, g_reloj_v); e.W32(kModV + 0x278, 45);
    }
  }
  if (in.congelar) e.W32(G + 0x520, e.R32(e.R32(G + 0x500) + 0x24));
  const Cronometro cr(e);
  bool ok = true;
  if (!in.inicio) {   // el primer cuadro solo monta; cada cuadro: medidor (tarea 2), jugador y objetos
    ok = e.Llamar(D.scripts, {kTarea2V}, 2000000);
    const uint32_t fn = e.R32(kTareaV + 0x1C);
    bool conocido = fn == D.finpose || fn == D.reset;
    for (uint32_t f : D.est) conocido |= fn == f;
    if (ok && conocido) ok = e.Llamar(fn, {kTareaV}, 2000000);
    if (ok) ok = e.Llamar(D.upd_obj, {}, 2000000);
  }
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips vuelo: {} (t={}); sigue el port", e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  st->Cuenta(cr.Us());
  out.x = e.Rf(G + 0x5C0); out.z = e.Rf(G + 0x5C8); out.vel = e.Rf(G + 0x560);
  out.yaw = e.R32(G + 0x55C) & 0xFFFF; out.anim = e.R32(G + 0x554); out.estado = int(e.R32(G + 0x570));
  out.t_ms = e.Rf(G + 0x518); out.medidor = e.Rf(G + 0x51C); out.congelado = e.Rf(G + 0x520);
  out.t568 = e.Rf(G + 0x568); out.t56c = e.Rf(G + 0x56C);
  out.monedas = int(e.R32(G + 0x524)); out.quedan = int(e.R32(G + 0x50C));
  out.n_obj = int(std::min<uint32_t>(e.R32(G + 0x510), 256));
  for (int i = 0; i < out.n_obj; ++i) out.flags[i] = e.R16(kObjV + 24 * uint32_t(i));
  if (std::getenv("DBZ3_TEST_IWMINI"))
    REXLOG_INFO("dbz3 iwmips vuelo t={} pos={:.3f},{:.3f} yaw={} est={} vel={:.3f} med={:.5f} quedan={} anim={:X}", in.t, out.x, out.z,
                out.yaw, out.estado, out.vel, out.medidor, out.quedan, out.anim);
  return true;
}

const bool kRegistradoVuelo = (vuelo7::g_vuelo = &CampoVuelo, vuelo7::g_activa_vuelo = &ActivaVuelo, true);
}  // namespace
}  // namespace dbz3::iwmips::bubbles
#endif
#endif
