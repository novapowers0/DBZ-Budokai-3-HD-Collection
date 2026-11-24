// SPDX-License-Identifier: MIT
// F1: rango original y, por ajuste independiente OFF, energia/salida de carga Cell.
// NO es aun la maquina completa de aim/carga/fuego/impacto. Bancos sinteticos:
// disparo_banco.py rangos y disparo_fases.py (EU/USA; sin juego).
// Como iw_mips_vuelo.inl: el port incluye solo las declaraciones; iw_mips.cpp,
// al final y fuera de sus namespaces, incluye tambien la implementacion.
#ifndef IWD_DECL_HECHA
#define IWD_DECL_HECHA
#include <cstdint>
namespace dbz3::iwmips::disparo {
// valor: segundos JUGADOS en 7D7/7D8; proporcion de altura en 7DE.
// Un rango -1 es una salida original valida (fuera de bandas o Freezer <7 aciertos).
// false = ajuste OFF, ELF no disponible o llamada fallida: conservar el port.
inline bool (*g_rango)(unsigned clave, int dificultad, int aciertos, float valor, int& rango) = nullptr;
// R22/PAQ_3: premio (cfg+0x13) de las tablas del ELF EU: 7D7 Viewer_CommitShootResult 0x3496B0, 7D8 Viewer_StoreResultToConfig
// 0x324C50. -1 = sin ELF EU o clave sin tabla: conservar el port.
inline int (*g_premio)(unsigned clave, int dificultad, int rango) = nullptr;
// Solo Cell vivo, tiempo positivo y tarea Charge. El llamador conserva HUD/FX,
// caducidad, derrota y sonido. Un paso del port = 1 campo original (60 Hz).
struct EntradaCarga {
  float energia = 0, altura = 0;
  uint16_t mantenido = 0;
};
struct SalidaCarga { float energia = 0; bool volver_mira = false; };
inline bool (*g_carga_cell)(const EntradaCarga&, SalidaCarga&) = nullptr;
// Estado transportable entre cuadros: no conservar un espejo EE compartido con
// otros minijuegos. reloj es float32 en PreIntro/Ready y u32 de fase en Intro.
enum class FaseIntroCell : uint32_t { preintro, intro, ready, mira };
struct EstadoIntroCell {
  FaseIntroCell fase = FaseIntroCell::preintro;
  uint32_t reloj = 0x40000000u;  // Start original: dos campos
  uint32_t flags = 0, par_a = 0, par_b = 0;
  uint8_t visible_a = 1, visible_b = 1;
};
struct EntradaIntroCell {
  EstadoIntroCell estado;
  float camara = 0, duracion = 0;
  uint16_t pulsado = 0;
};
struct SalidaIntroCell {
  EstadoIntroCell estado;
  int clip = -1, preparaciones = 0;
};
inline bool (*g_intro_cell)(const EntradaIntroCell&, SalidaIntroCell&) = nullptr;
// IW-F05: un campo de MgShoot_St_Aim (EU 0x30ADE0) con MgShoot_MoveAimByPad (0x309AE0) del ELF: mira (x, z de la
// camara +0x80/+0x88) y la decision Aim -> Fire/Charge/LowEnergyWait, con cruceta o palanca (la palanca va siempre a
// 1,5 por campo en su direccion). Cell vivo con tiempo: la derrota/tiempo (GameOver) la decide el llamador antes.
struct EntradaMira {
  float x = 0, z = 200, energia = 0, altura = 0;
  bool usada = false;          // ctx+8: LowEnergyWait ya usado
  uint16_t mantenido = 0;      // 0x4ADECC: cruceta 0x10/0x20/0x40/0x80 y cuadrado 0x8000
  uint16_t nuevo = 0;          // 0x4ADECE: circulo 0x2000 recien pulsado
  int8_t ax = 0, ay = 0;       // 0x4ADEDA/DB: palanca (zona muerta 32 en 0x49C380; fuera de ella manda la palanca)
};
enum class SiguienteMira : uint8_t { mira, fuego, carga, baja };
struct SalidaMira {
  float x = 0, z = 0;
  SiguienteMira siguiente = SiguienteMira::mira;
  bool usada = false;
};
inline bool (*g_mira_cell)(const EntradaMira&, SalidaMira&) = nullptr;
}  // namespace dbz3::iwmips::disparo
#endif

#ifndef IWD_SOLO_DECL
#ifndef IWD_IMPL_HECHA
#define IWD_IMPL_HECHA
#include <cmath>
#include <cstring>
#include <array>
#include <cstdlib>
REXCVAR_DEFINE_BOOL(dbz3_iw_disparo_original, true, "DBZ3/IW",
                    "IW disparo: rango desde el ELF original EU/USA (solo resultados; motor completo pendiente; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_disparo_carga_original, true, "DBZ3/IW",
                    "IW Cell: energia y salida de carga desde el ELF (solo vivo/sin caducidad; HUD/FX del port; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_disparo_intro_original, true, "DBZ3/IW",
                   "IW Cell: PreIntro/Intro/Ready desde el ELF; camaras y actores del port; requiere dbz3_iw_logica_original");
REXCVAR_DEFINE_BOOL(dbz3_iw_disparo_mira_original, true, "DBZ3/IW",
                    "IW Cell: mira (cruceta) y paso Aim->Fire/Charge/LowEnergy desde el ELF (pruebas; requiere dbz3_iw_logica_original)");
namespace dbz3::iwmips::bubbles {
namespace {
// Estos seis init/rank y sus tablas se ejecutan sin puentes en el banco Python.
// No reutilizar las entradas MgShoot como si fueran el motor Freezer/Piccolo.
struct DirRangoDisparo {
  unsigned clave;
  uint32_t init, rango, ctxp, indice;
};
constexpr DirRangoDisparo kRangoDisparoEu[] = {
    {0x7D7, 0x34B800, 0x3481E0, 0x49CE78, 0x14},
    {0x7D8, 0x3279F0, 0x324490, 0x49CDF0, 0x28},
    {0x7DE, 0x30D150, 0x3099E0, 0, 0},
};
constexpr DirRangoDisparo kRangoDisparoUsa[] = {
    {0x7D7, 0x34B300, 0x347CE0, 0x49C65C, 0x14},
    {0x7D8, 0x3276E0, 0x3241A0, 0x49C5D0, 0x28},
    {0x7DE, 0x30CEE0, 0x3097C0, 0, 0},
};
// 0x11B0000..0x11E8000 lo usa el mapa (DMD/SCD). No alojar aqui un
// contexto de disparo aunque Ambito invalide el espejo de la maquina anterior.
constexpr uint32_t kCtxRangoDisparo = 0x1200000;

// R22/PAQ_3: premio como el original (iw_sles55347.c:437680-437692 y 415046-415058): rango -1 -> u16 de fallo; si no,
// u16 base[rango] * float mult[dif], redondeado a la centena (resto >= 50 sube). Las tablas las deja el init del
// minijuego, asi que se leen dentro de RangoDisparo, justo tras el rango; PremioDisparo devuelve esa lectura.
struct PremioLeido { unsigned clave = 0; int dif = -1, rango = -9, premio = -1; };
PremioLeido g_premio_leido;
// USA (SLUS_218.42): las cuatro tablas en EU - 0x800 (.bss; lui+lo de las mismas instrucciones emparejadas,
// E:/DBZ3HD_trabajo/iw_eu_usa_tabla.py).
int LeerPremio(Ee& e, unsigned clave, int dif, int rango) {
  const bool d7 = clave == 0x7D7;
  const uint32_t u = e.HashTexto() == kHashUsa ? 0x800 : 0;
  if (rango < 0) return e.R16((d7 ? 0x5D9B34 : 0x5D986C) - u);
  const uint32_t mb = e.R32((d7 ? 0x5D9B58 + uint32_t(dif) * 0x18 : 0x5D98C0 + uint32_t(dif) * 0x10) - u);
  float mult;
  std::memcpy(&mult, &mb, 4);
  const uint16_t base = e.R16((d7 ? 0x5D9B36 : 0x5D986E) - u + uint32_t(rango) * 2);
  if (!base || !(mult > 0.0f && mult < 10.0f)) {
    REXLOG_WARN("dbz3 iwdisparo: {:X} tablas de premio vacias (base {} mult {}); conserva el port", clave, base, mult);
    return -1;
  }
  const int v = int(float(base) * mult);
  return (v % 100 < 0x32 ? v / 100 : v / 100 + 1) * 100 & 0xFFFF;
}
// R23: RangoDisparo sale en su primera linea sin dbz3_iw_disparo_original (M7D7_gana no la pone) y nadie leia las
// tablas: "sin lectura" con leido 0/-1/-9. Aqui se corre solo el init del minijuego (deja las tablas) y se leen,
// EU o USA. Sin dbz3_iw_logica_original Listo() es nulo y se conserva el port, como antes.
int PremioPorInit(unsigned clave, int dif, int rango) {
  if ((clave != 0x7D7 && clave != 0x7D8) || dif < 0 || dif > 5 || rango < -1 || rango > 4) return -1;
  Estado* const st = Listo();
  if (!st) return -1;
  const auto& dirs = st->ee.HashTexto() == kHashUsa ? kRangoDisparoUsa : kRangoDisparoEu;
  const DirRangoDisparo* d = nullptr;
  for (const auto& item : dirs) if (item.clave == clave) d = &item;
  if (!d) return -1;
  Ee& e = st->ee;
  Ambito amb(*st, kNadie);
  const uint32_t previo = d->ctxp ? e.R32(d->ctxp) : 0;
  Cronometro cr(e);
  const bool ok = e.Llamar(d->init, {}, 100000) && !e.io;
  st->Cuenta(cr.Us());
  const int v = ok ? LeerPremio(e, clave, dif, rango) : -1;
  if (d->ctxp) e.W32(d->ctxp, previo);
  if (!ok) REXLOG_WARN("dbz3 iwdisparo: premio {:X}: init fallo ({}); conserva el port", clave, e.error());
  return v;
}
int PremioDisparo(unsigned clave, int dif, int rango) {
  if (g_premio_leido.clave != clave || g_premio_leido.dif != dif || g_premio_leido.rango != rango ||
      g_premio_leido.premio < 0)
    if (const int v = PremioPorInit(clave, dif, rango); v >= 0) g_premio_leido = {clave, dif, rango, v};
  const PremioLeido& p = g_premio_leido;
  if (p.clave != clave || p.dif != dif || p.rango != rango || p.premio < 0) {
    REXLOG_INFO("dbz3 iwdisparo: premio ELF sin lectura: pedido {:X}/{}/{} leido {:X}/{}/{} premio {}", clave, dif, rango, p.clave, p.dif, p.rango, p.premio);
    return -1;
  }
  REXLOG_INFO("dbz3 iwdisparo: {:X} rango/premio desde ELF ({}) rango={} premio={} dif={}", clave,
              clave == 0x7D7 ? "0x3496B0" : "0x324C50", rango, p.premio, dif);
  return p.premio;
}

bool RangoDisparo(unsigned clave, int dif, int aciertos, float valor, int& salida) {
  if (!REXCVAR_GET(dbz3_iw_disparo_original) || dif < 0 || dif > 5 ||
      aciertos < 0 || aciertos > 255 || !std::isfinite(valor)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  // Elegir DESPUES de Listo(): la primera carga identifica el ELF USA.
  const auto& dirs = st->ee.HashTexto() == kHashUsa ? kRangoDisparoUsa : kRangoDisparoEu;
  const DirRangoDisparo* d = nullptr;
  for (const auto& item : dirs) if (item.clave == clave) d = &item;
  if (!d) return false;
  Ee& e = st->ee;
  // El init escribe tablas globales: invalidar expresamente cualquier espejo
  // persistente anterior, sin atribuirlo a otra maquina ni reutilizar su estado.
  Ambito amb(*st, kNadie);
  const uint32_t previo = d->ctxp ? e.R32(d->ctxp) : 0;
  struct RestaurarContexto {
    Ee& e;
    uint32_t ptr, antes;
    ~RestaurarContexto() { if (ptr) e.W32(ptr, antes); }
  } restaurar{e, d->ctxp, previo};
  Cronometro cr(e);
  uint32_t bits;
  std::memcpy(&bits, &valor, sizeof(bits));
  bool ok = e.Llamar(d->init, {}, 100000) && !e.io;
  if (ok) {
    if (d->ctxp) {
      e.Cero(kCtxRangoDisparo, 0x200);
      e.W32(d->ctxp, kCtxRangoDisparo);
      e.W32(kCtxRangoDisparo + d->indice, uint32_t(dif));
      if (clave == 0x7D7) e.W8(kCtxRangoDisparo + 0xC, uint8_t(aciertos));
    }
    e.WF(12, bits);  // API entregada a F2 junto con el include: bits crudos, sin conversion host.
    ok = e.Llamar(d->rango, {}, 5000);
  }
  st->Cuenta(cr.Us());
  if (!ok || e.io) {
    REXLOG_WARN("dbz3 iwmips disparo rango {:#x}: fallo ({}); conserva el port", clave, e.error());
    return false;
  }
  const uint32_t raw = uint32_t(e.r[2]);
  if (raw != 0xFFFFFFFFu && raw > 4) {
    REXLOG_WARN("dbz3 iwmips disparo rango {:#x}: salida invalida {:#x}; conserva el port", clave, raw);
    return false;
  }
  salida = raw == 0xFFFFFFFFu ? -1 : int(raw);
  g_premio_leido = {};
  if (clave == 0x7D7 || clave == 0x7D8)
    g_premio_leido = {clave, dif, salida, LeerPremio(e, clave, dif, salida)};
  REXLOG_INFO("dbz3 iwmips disparo rango clave={:#x} dif={} aciertos={} valor={} rango={}",
              clave, dif, aciertos, valor, salida);
  return true;
}

struct DirCargaDisparo {
  uint32_t init, carga, mira, ctxp, mgrp, paso, mando, escala;
  // Orden de los JAL del cuerpo Charge; Camera aparece tres veces.
  uint32_t servicios[17];
};
constexpr DirCargaDisparo kCargaDisparoEu = {
    0x30D150, 0x30A5C0, 0x30ADE0, 0x49CDBC, 0x49CA70, 0x49C7DC, 0x4ADECC, 0x5D9434,
    {0x309DF0, 0x30BE70, 0x2ED7D0, 0x31ECB0, 0x2ED7D0, 0x2ED7D0, 0x30C020, 0x2EF4E0,
     0x31E1F0, 0x1FC7C0, 0x2EF9D0, 0x31E050, 0x310340, 0x2F0A30, 0x3102A0, 0x347DF0, 0x13F7A0}};
constexpr DirCargaDisparo kCargaDisparoUsa = {
    0x30CEE0, 0x30A3A0, 0x30AB80, 0x49C59C, 0x49C270, 0x49BFDC, 0x4AD6CC, 0x5D8C34,
    {0x309BD0, 0x30BC00, 0x2ED5C0, 0x31E9C0, 0x2ED5C0, 0x2ED5C0, 0x30BDB0, 0x2EF2D0,
     0x31DF00, 0x1FC7B0, 0x2EF7C0, 0x31DD60, 0x3100D0, 0x2F0820, 0x310030, 0x3478F0, 0x13F7A0}};
constexpr uint32_t kMgrCargaDisparo = 0x1201000, kCamCargaDisparo = 0x1205000,
                   kTareaCargaDisparo = 0x1206000;

bool CargaDisparoCell(const disparo::EntradaCarga& in, disparo::SalidaCarga& salida) {
  if (!REXCVAR_GET(dbz3_iw_disparo_carga_original) || !std::isnormal(in.energia) ||
      in.energia <= 0 || in.energia > 1 || !std::isfinite(in.altura) ||
      in.altura < 0 || in.altura / 400.f >= 1.f) return false;
  Estado* const st = Listo();
  if (!st) return false;
  const auto& d = st->ee.HashTexto() == kHashUsa ? kCargaDisparoUsa : kCargaDisparoEu;
  Ee& e = st->ee;
  Ambito amb(*st, kNadie);  // espejo completo de este paso, sin persistencia ajena
  const std::array<uint32_t, 5> dirs = {d.ctxp, d.mgrp, d.paso, d.mando, d.escala};
  struct RestaurarGlobales {
    Ee& e;
    std::array<uint32_t, 5> dirs, valores;
    ~RestaurarGlobales() { for (size_t i = 0; i < dirs.size(); ++i) e.W32(dirs[i], valores[i]); }
  } restaurar{e, dirs, {}};
  for (size_t i = 0; i < dirs.size(); ++i) restaurar.valores[i] = e.R32(dirs[i]);
  int hud = 0, tiempo = 0, camara = 0, freeze = 0, efecto = 0;
  bool fuera = false;
  // Una salida de derrota/tiempo no forma parte de este adaptador. Cualquier
  // servicio de esa rama invalida la llamada; nunca ejecutarlo con mundo ficticio.
  for (uint32_t dir : d.servicios)
    amb.Puente(dir, [&](Ee& c) { fuera = true; V0(c, 0); });
  amb.Puente(d.servicios[0], [&](Ee& c) { ++hud; V0(c, 0); });  // HUD sigue en el port
  amb.Puente(d.servicios[1], [&](Ee& c) {
    ++tiempo;
    if (uint32_t(c.r[4]) != kTareaCargaDisparo) fuera = true;
    V0(c, 0);  // el llamador ya comprobo tiempo positivo antes de entrar en Charge
  });
  amb.Puente(d.servicios[2], [&](Ee& c) { ++camara; V0(c, kCamCargaDisparo); });
  amb.Puente(d.servicios[14], [&](Ee& c) { ++freeze; if (uint32_t(c.r[4]) != 0) fuera = true; });
  amb.Puente(d.servicios[15], [&](Ee& c) {
    ++efecto;
    if (uint32_t(c.r[4]) != 1 || uint32_t(c.r[5]) != 0) fuera = true;
  });
  Cronometro cr(e);
  bool ok = e.Llamar(d.init, {}, 100000) && !e.io && e.R32(d.escala) == 0x40000000u;
  if (ok) {
    e.Cero(kCtxRangoDisparo, 0x100); e.Cero(kMgrCargaDisparo, 0x3000);
    e.Cero(kCamCargaDisparo, 0x100); e.Cero(kTareaCargaDisparo, 0x100);
    e.W32(d.ctxp, kCtxRangoDisparo); e.W32(d.mgrp, kMgrCargaDisparo);
    e.W32(d.paso, 0x3F800000u);  // port a 60 Hz: 1 campo, no 1.2 PAL
    e.W16(d.mando, in.mantenido);
    e.Wf(kCtxRangoDisparo + 4, in.energia);
    e.W32(kCtxRangoDisparo + 0x20, 0xFFFFFFFFu);  // sonido queda en el port
    e.Wf(kMgrCargaDisparo + 0x2914, in.altura); e.Wf(kCamCargaDisparo + 0x84, 400.f);
    e.W32(kTareaCargaDisparo + 0x1C, d.carga);
    ok = e.Llamar(d.carga, {kTareaCargaDisparo}, 5000) && !e.io;
  }
  st->Cuenta(cr.Us());
  const uint32_t estado = e.R32(kTareaCargaDisparo + 0x1C);
  const bool volver = estado == d.mira;
  const float energia = e.Rf(kCtxRangoDisparo + 4);
  if (!ok || fuera || hud != 1 || tiempo != 1 || camara != 1 ||
      (estado != d.carga && !volver) || freeze != int(volver) || efecto != int(volver) ||
      !std::isfinite(energia) || energia <= 0 || energia > 1) return false;
  // Commit solo tras todas las comprobaciones; false no modifica la salida.
  salida.energia = energia;
  salida.volver_mira = volver;
  if (std::getenv("DBZ3_TEST_IW_CARGA"))
    REXLOG_INFO("dbz3 iwmips disparo carga t60 energia_in={} altura={} mando={} energia_out={} mira={}",
                in.energia, in.altura, in.mantenido, energia, volver);
  return true;
}
// Perfiles comprobados por disparo_intro.py/ready.py en ambos ELF. Los JAL de
// cada cuerpo se asocian por funcion, nunca por un delta regional universal.
enum class ServicioIntro {
  nativo, pose, animacion, fatiga, efecto, campos, preparar, stopaudio,
  voz, sonido, vozmini, playaudio, hud, flag, timer_ui
};
constexpr ServicioIntro kRolesIntro[] = {
    ServicioIntro::nativo, ServicioIntro::nativo, ServicioIntro::pose, ServicioIntro::pose,
    ServicioIntro::animacion, ServicioIntro::fatiga, ServicioIntro::nativo, ServicioIntro::efecto,
    ServicioIntro::campos, ServicioIntro::preparar, ServicioIntro::nativo, ServicioIntro::fatiga,
    ServicioIntro::stopaudio, ServicioIntro::voz, ServicioIntro::sonido, ServicioIntro::nativo,
    ServicioIntro::efecto, ServicioIntro::campos, ServicioIntro::preparar, ServicioIntro::nativo,
    ServicioIntro::fatiga, ServicioIntro::fatiga, ServicioIntro::stopaudio, ServicioIntro::voz,
    ServicioIntro::sonido, ServicioIntro::nativo};
constexpr ServicioIntro kRolesPreIntro[] = {
    ServicioIntro::nativo, ServicioIntro::nativo, ServicioIntro::nativo, ServicioIntro::animacion,
    ServicioIntro::pose, ServicioIntro::pose, ServicioIntro::nativo, ServicioIntro::nativo,
    ServicioIntro::vozmini, ServicioIntro::playaudio};
constexpr ServicioIntro kRolesReady[] = {
    ServicioIntro::hud, ServicioIntro::nativo, ServicioIntro::pose, ServicioIntro::nativo,
    ServicioIntro::voz, ServicioIntro::flag, ServicioIntro::timer_ui};

bool IntroDisparoCell(const disparo::EntradaIntroCell& in, disparo::SalidaIntroCell& salida) {
  using Fase = disparo::FaseIntroCell;
  const auto& s = in.estado;
  if (!REXCVAR_GET(dbz3_iw_disparo_intro_original) || s.fase == Fase::mira ||
      uint32_t(s.fase) > uint32_t(Fase::ready) || !std::isfinite(in.camara) ||
      !std::isfinite(in.duracion) || in.camara < 0 || in.duracion < 0 ||
      s.visible_a > 1 || s.visible_b > 1 || s.par_a > 1 || s.par_b > 1) return false;
  // En Intro el mismo slot contiene una fase entera, NO un float. Fuera de
  // Intro rechazar NaN/inf sin tocar ni la salida ni el espejo compartido.
  if (s.fase != Fase::intro && (s.reloj & 0x7F800000u) == 0x7F800000u) return false;
  Estado* const st = Listo();
  if (!st) return false;
  const bool usa = st->ee.HashTexto() == kHashUsa;
  const auto& d = usa ? kCargaDisparoUsa : kCargaDisparoEu;
  const uint32_t pre = usa ? 0x30B1A0 : 0x30B410, intro = usa ? 0x30AF50 : 0x30B1C0,
                 ready = usa ? 0x30AEA0 : 0x30B110, fin = usa ? 0x30B2B0 : 0x30B520;
  const uint32_t camp = usa ? 0x49C54C : 0x49CD6C, ctrlp = usa ? 0x49C5A4 : 0x49CDC4,
                 par = usa ? 0x5D8CC0 : 0x5D94C0;
  constexpr uint32_t ctrl = 0x1207000;
  Ee& e = st->ee;
  Ambito amb(*st, kNadie);
  // Este adaptador no llama init ni SetupRound sobre el mundo sintetico. Los
  // servicios de presentacion se convierten en solicitudes, aplicadas por el port
  // solo DESPUES de validar el paso entero (tambien conserva Setup doble).
  const std::array<uint32_t, 9> dirs = {d.mgrp, d.paso, d.mando + 2, camp, ctrlp,
                                      par, par + 4, d.ctxp, d.escala};
  struct RestaurarIntro {
    Ee& e; const std::array<uint32_t, 9>& dirs; std::array<uint32_t, 9> valores{};
    ~RestaurarIntro() { for (size_t i = 0; i < dirs.size(); ++i) e.W32(dirs[i], valores[i]); }
  } restaurar{e, dirs};
  for (size_t i = 0; i < dirs.size(); ++i) restaurar.valores[i] = e.R32(dirs[i]);
  disparo::SalidaIntroCell out;
  bool fuera = false;
  auto servicios = [&](uint32_t a, uint32_t b, const auto& roles) {
    size_t i = 0;
    for (; a < b; a += 4) {
      const uint32_t op = e.R32(a);
      if ((op >> 26) != 3) continue;
      if (i >= std::size(roles)) return false;
      const auto rol = roles[i++];
      if (rol == ServicioIntro::nativo) continue;
      amb.Puente((op & 0x3FFFFFFu) << 2, [&, rol](Ee& c) {
        if (rol == ServicioIntro::animacion) {
          const uint32_t clip = uint32_t(c.r[5]);
          if (clip != 1300 && clip != 1303) fuera = true;
          else out.clip = int(clip);
        } else if (rol == ServicioIntro::preparar) {
          ++out.preparaciones;  // SetupRound real queda en el port, no se finge su efecto EE
        }
        V0(c, 0);
      });
    }
    return i == std::size(roles);
  };
  if (!servicios(intro, pre, kRolesIntro) || !servicios(pre, fin, kRolesPreIntro) ||
      !servicios(ready, intro, kRolesReady)) return false;
  e.Cero(kMgrCargaDisparo, 0x4000); e.Cero(kCamCargaDisparo, 0x200);
  e.Cero(kTareaCargaDisparo, 0x100); e.Cero(ctrl, 0x100);
  e.W32(d.mgrp, kMgrCargaDisparo); e.W32(d.paso, 0x3F800000u);
  e.W16(d.mando + 2, in.pulsado);
  e.W32(camp, kCamCargaDisparo); e.W32(ctrlp, ctrl);
  e.Wf(kCamCargaDisparo + 0x168, in.camara); e.Wf(kCamCargaDisparo + 0x16C, in.duracion);
  e.W8(kMgrCargaDisparo + 0x14CD, s.visible_a); e.W8(kMgrCargaDisparo + 0x359D, s.visible_b);
  e.W32(ctrl + 0x24, s.flags); e.W32(par, s.par_a); e.W32(par + 4, s.par_b);
  const uint32_t fn = s.fase == Fase::preintro ? pre : s.fase == Fase::intro ? intro : ready;
  e.W32(kTareaCargaDisparo + 0x1C, fn); e.W32(kTareaCargaDisparo + 0x30, s.reloj);
  Cronometro cr(e);
  const bool ok = e.Llamar(fn, {kTareaCargaDisparo}, 10000) && !e.io;
  st->Cuenta(cr.Us());
  const uint32_t destino = e.R32(kTareaCargaDisparo + 0x1C);
  if (!ok || fuera || out.preparaciones > 2 ||
      (destino != pre && destino != intro && destino != ready && destino != d.mira)) {
    REXLOG_WARN("dbz3 iwmips disparo intro: fallo ({}); conserva el port", e.error());
    return false;
  }
  out.estado.fase = destino == pre ? Fase::preintro : destino == intro ? Fase::intro :
                    destino == ready ? Fase::ready : Fase::mira;
  out.estado.reloj = e.R32(kTareaCargaDisparo + 0x30);
  out.estado.visible_a = e.R8(kMgrCargaDisparo + 0x14CD);
  out.estado.visible_b = e.R8(kMgrCargaDisparo + 0x359D);
  out.estado.flags = e.R32(ctrl + 0x24); out.estado.par_a = e.R32(par); out.estado.par_b = e.R32(par + 4);
  salida = out;
  if (s.fase != out.estado.fase || (s.reloj != out.estado.reloj && s.fase == Fase::intro) || out.preparaciones)
    REXLOG_INFO("dbz3 iwmips disparo intro fase={}->{} reloj={} clip={} setup={} region={}",
                uint32_t(s.fase), uint32_t(out.estado.fase), out.estado.reloj, out.clip, out.preparaciones, usa ? "us" : "eu");
  return true;
}

// IW-F05: los 33 JAL del cuerpo de MgShoot_St_Aim en orden (tools/iw_mips/disparo_aim.py, ROLES; EU y USA iguales).
// nativo = se ejecuta del ELF (MoveAimByPad, Camera_GetControllerPtr, Timer_SetSlot, float->double y comparacion);
// fuera = rama GameOver (la decide el port antes: no se finge); nada = presentacion (UI, voz, efecto) que sigue en el port.
enum class ServicioMira : uint8_t { nativo, hud, tiempo, fuera, nada, camptr };
constexpr ServicioMira kRolesMira[] = {
    ServicioMira::hud, ServicioMira::nativo, ServicioMira::tiempo, ServicioMira::nativo,      // hud mover tiempo camara
    ServicioMira::fuera, ServicioMira::nativo, ServicioMira::nativo, ServicioMira::fuera,     // restante camara camara reset
    ServicioMira::fuera, ServicioMira::fuera, ServicioMira::fuera, ServicioMira::nativo,      // vacio pantalla voz timer
    ServicioMira::nada, ServicioMira::nada, ServicioMira::nada, ServicioMira::nativo,         // par flags objeto extender
    ServicioMira::nativo, ServicioMira::nada, ServicioMira::nada, ServicioMira::nada,         // comparar transform vozmini target
    ServicioMira::camptr, ServicioMira::camptr, ServicioMira::nada, ServicioMira::nada,       // camptr camptr viewdir efecto
    ServicioMira::nativo, ServicioMira::nada, ServicioMira::nada, ServicioMira::nada,         // timer timer_ui par flags
    ServicioMira::nada, ServicioMira::nada, ServicioMira::nada, ServicioMira::nada,           // objetos sprite objeto vozmini
    ServicioMira::nada};                                                                       // flag_global

bool MiraDisparoCell(const disparo::EntradaMira& in, disparo::SalidaMira& salida) {
  if (!REXCVAR_GET(dbz3_iw_disparo_mira_original) || !std::isfinite(in.x) || !std::isfinite(in.z) ||
      in.x < -111.f || in.x > 74.f || in.z < 60.f || in.z > 280.f || !std::isnormal(in.energia) ||
      in.energia <= 0 || in.energia > 1 || !std::isfinite(in.altura) || in.altura < 0 || in.altura / 400.f >= 1.f ||
      (in.mantenido & ~0x80F0u) || (in.nuevo & ~0x2000u)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  const bool usa = st->ee.HashTexto() == kHashUsa;
  const auto& d = usa ? kCargaDisparoUsa : kCargaDisparoEu;
  const uint32_t ready = usa ? 0x30AEA0 : 0x30B110, mover = usa ? 0x3098C0 : 0x309AE0,
                 fuego = usa ? 0x30A5E0 : 0x30A800, baja = usa ? 0x30A310 : 0x30A530,
                 camp = usa ? 0x49C54C : 0x49CD6C, mando2 = usa ? 0x4AD72C : 0x4ADF2C,
                 analog = usa ? 0x4AD6D8 : 0x4ADED8, velocidad = usa ? 0x5D8B74 : 0x5D9374;
  Ee& e = st->ee;
  Ambito amb(*st, kNadie);
  const std::array<uint32_t, 8> dirs = {d.ctxp, d.mgrp, d.paso, d.mando, mando2, analog, camp, d.escala};
  struct RestaurarMira {
    Ee& e; const std::array<uint32_t, 8>& dirs; std::array<uint32_t, 8> valores{};
    ~RestaurarMira() { for (size_t i = 0; i < dirs.size(); ++i) e.W32(dirs[i], valores[i]); }
  } restaurar{e, dirs};
  for (size_t i = 0; i < dirs.size(); ++i) restaurar.valores[i] = e.R32(dirs[i]);
  // Frontera de JAL comprobada como el banco: 33 llamadas, la 1 es MoveAimByPad y la 3 Camera_GetControllerPtr; una
  // misma direccion no puede tener dos papeles distintos.
  std::array<std::pair<uint32_t, ServicioMira>, std::size(kRolesMira)> llamadas{};
  size_t n = 0;
  for (uint32_t a = d.mira; a < ready; a += 4) {
    const uint32_t op = e.R32(a);
    if ((op >> 26) != 3) continue;
    if (n >= std::size(kRolesMira)) return false;
    llamadas[n] = {(op & 0x3FFFFFFu) << 2, kRolesMira[n]};
    ++n;
  }
  if (n != std::size(kRolesMira) || llamadas[1].first != mover || llamadas[3].first != d.servicios[2]) return false;
  for (size_t i = 0; i < n; ++i)
    for (size_t j = 0; j < i; ++j)
      if (llamadas[i].first == llamadas[j].first && llamadas[i].second != llamadas[j].second) return false;
  int hud = 0, tiempo = 0;
  bool fuera = false;
  for (size_t i = 0; i < n; ++i) {
    const uint32_t dir = llamadas[i].first;
    const ServicioMira rol = llamadas[i].second;
    if (rol == ServicioMira::nativo) continue;
    amb.Puente(dir, [&hud, &tiempo, &fuera, rol](Ee& c) {
      if (rol == ServicioMira::hud) ++hud;
      else if (rol == ServicioMira::tiempo) {
        ++tiempo;
        if (uint32_t(c.r[4]) != kTareaCargaDisparo) fuera = true;
      } else if (rol == ServicioMira::fuera) fuera = true;
      V0(c, rol == ServicioMira::camptr ? kCamCargaDisparo + 0x80 : 0);
    });
  }
  Cronometro cr(e);
  bool ok = e.Llamar(d.init, {}, 100000) && !e.io && e.R32(d.escala) == 0x40000000u &&
            e.Rf(velocidad) == 1.5f;
  if (ok) {
    e.Cero(kCtxRangoDisparo, 0x100); e.Cero(kMgrCargaDisparo, 0x3000);
    e.Cero(kCamCargaDisparo, 0x200); e.Cero(kTareaCargaDisparo, 0x100);
    e.W32(d.ctxp, kCtxRangoDisparo); e.W32(d.mgrp, kMgrCargaDisparo); e.W32(camp, kCamCargaDisparo);
    e.W32(d.paso, 0x3F800000u);  // port a 60 Hz
    e.Wf(kCtxRangoDisparo + 4, in.energia); e.W32(kCtxRangoDisparo + 8, in.usada ? 1u : 0u);
    e.Wf(kMgrCargaDisparo + 0x2914, in.altura);
    const float v[8] = {in.x, 400.f, in.z, 1.f, in.x, 0.f, in.z - 200.f, 1.f};   // ojo (y = Ten) y objetivo
    for (uint32_t i = 0; i < 8; ++i) e.Wf(kCamCargaDisparo + 0x80 + 4 * i, v[i]);
    e.W32(d.mando, uint32_t(in.mantenido) | (uint32_t(in.nuevo) << 16));
    e.W32(mando2, 0); e.W32(analog, 0);
    e.W8(analog + 2, uint8_t(in.ax)); e.W8(analog + 3, uint8_t(in.ay));
    e.W32(kTareaCargaDisparo + 0x1C, d.mira);
    ok = e.Llamar(d.mira, {kTareaCargaDisparo}, 10000) && !e.io;
  }
  st->Cuenta(cr.Us());
  const uint32_t destino = e.R32(kTareaCargaDisparo + 0x1C);
  disparo::SalidaMira out;
  out.x = e.Rf(kCamCargaDisparo + 0x80);
  out.z = e.Rf(kCamCargaDisparo + 0x88);
  out.usada = e.R32(kCtxRangoDisparo + 8) != 0;
  if (destino == d.mira) out.siguiente = disparo::SiguienteMira::mira;
  else if (destino == fuego) out.siguiente = disparo::SiguienteMira::fuego;
  else if (destino == d.carga) out.siguiente = disparo::SiguienteMira::carga;
  else if (destino == baja) out.siguiente = disparo::SiguienteMira::baja;
  else ok = false;
  if (!ok || fuera || hud != 1 || tiempo != 1 || !std::isfinite(out.x) || !std::isfinite(out.z)) {
    REXLOG_WARN("dbz3 iwmips disparo mira: fallo ({}) destino {:#x}; conserva el port", e.error(), destino);
    return false;
  }
  salida = out;
  if (out.siguiente != disparo::SiguienteMira::mira || std::getenv("DBZ3_TEST_IW_MIRA"))
    REXLOG_INFO("dbz3 iwmips disparo mira original x={:.1f} z={:.1f} siguiente={} usada={} region={}", out.x, out.z,
                int(out.siguiente), int(out.usada), usa ? "us" : "eu");
  return true;
}

const bool kRangoDisparoRegistrado = [] {
  disparo::g_rango = RangoDisparo;
  disparo::g_premio = PremioDisparo;
  disparo::g_carga_cell = CargaDisparoCell;
  disparo::g_intro_cell = IntroDisparoCell;
  disparo::g_mira_cell = MiraDisparoCell;
  return true;
}();
}  // namespace
}  // namespace dbz3::iwmips::bubbles
#endif
#endif
