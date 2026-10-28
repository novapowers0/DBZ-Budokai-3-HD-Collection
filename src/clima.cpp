// dbz3 - CLIMA (ficha 10): el cielo del escenario se oscurece poco a poco durante el combate. US y EU (src/eu_map.h).
// Apagado por defecto: opcion dbz3_clima, regla de reto (Activar) o DBZ3_CLIMA=1 / =rapido en pruebas. Fuera en
// online (solo se veria en un PC).
//
// Mapa del juego (US, medido 2026-10-15):
//   Oscurecimiento del escenario del propio juego, el de la entrada en modo hiper (cielo y escenario oscuros, los
//   luchadores no): mgr+35180 = cuanto (el juego llega a 0,75 y vuelve a 0) y mgr+35184 = su fundido 0..1,
//   mgr = *0x823D6CF4. Medido con volcados de memoria antes/durante/despues del modo hiper (rival quieto).
//   Niebla: G = 0x823A3D80 (G+0 registro, G+0x20 #ZDD cargado, G+64 variante, G+76 banderas, 0x20 = niebla);
//   registro (0x82323760 + i*92) +0x1C + variante*32: [0] luz (0x82322B30, 80 B), [1] niebla (0x823232A0, 20 B:
//   color, 2 floats, inicio, fin). sub_820C2058 (cada fotograma de escenario, por puntero) borra la pantalla y
//   pone la niebla: sub_82085650 (activa), sub_82085670 (color 0x82389424), sub_82085688 (inicio, fin).
//   La niebla no llega al cielo (solo al fondo lejano); el color de los materiales (#AWO +0x20) no se usa al dibujar
//   el escenario (probado: sin efecto).

#include "clima.h"

#include "cine.h"
#include "escenarios.h"
#include "gigantes.h"
#include "iw_minijuegos.h"
#include "cinematica.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "net/netplay.h"

REXCVAR_DEFINE_BOOL(dbz3_clima, false, "DBZ3/Mods",
                    "Clima vivo: el cielo del escenario se oscurece poco a poco durante el combate");

namespace dbz3::clima {
namespace {

constexpr dbz3::GA kRecs = 0x82323760;
constexpr dbz3::GA kFogOn = 0x8238941C, kFogColor = 0x82389424;
constexpr float kOscuro = 0.55f;   // oscuridad final del cielo (el modo hiper llega a 0,75)

bool g_regla = false;          // regla de reto (Activar)
uint32_t g_frames = 0;         // fotogramas de pelea (estado 4) en este escenario

float F(uint8_t* base, uint32_t a) {
  uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PonF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}

bool Pruebas() {
  static const bool on = std::getenv("DBZ3_CLIMA") != nullptr;
  return on;
}

// SONDA (DBZ3_SONDA_CLIMA=1): niebla y luz de cada registro (variante 0 y 1), para elegir el clima por escenario.
void Sonda(uint8_t* base) {
  for (uint32_t r = 0; r < 17; ++r) {
    std::string t;
    for (uint32_t v = 0; v < 2; ++v) {
      const uint32_t grp = kRecs + r * 92 + 0x1C + v * 32, ni = REX_LOAD_U32(grp + 4);
      const uint32_t n = dbz3::GA(0x823232A0) + (ni < 64 ? ni : 0) * 20;
      char b[160];
      std::snprintf(b, sizeof(b), " | v%u luz %u niebla %u: %08X %.0f..%.0f", v, REX_LOAD_U32(grp), ni, REX_LOAD_U32(n),
                    F(base, n + 12), F(base, n + 16));
      t += b;
    }
    REXLOG_INFO("dbz3 sonda clima: registro {:2} '{}'{}", r, reinterpret_cast<const char*>(base + kRecs + r * 92), t);
  }
}

}  // namespace

bool Activo() {
  return (g_regla || Pruebas() || REXCVAR_GET(dbz3_clima)) &&
         !dbz3::net::Session::Get().active();
}
void Activar(bool on) { g_regla = on; }

void EscenarioPedido() {
  dbz3::cine::NuevoCombate();
  g_frames = 0;
}

}  // namespace dbz3::clima

// Fotograma del escenario (borrado + niebla): aqui empieza el dibujo, con la logica del fotograma ya hecha.
DBZ3_HOOK_EU(sub_820C2058) {
  orig(ctx, base);
  using namespace dbz3::clima;
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  const uint32_t st = mgr ? REX_LOAD_U32(mgr + 35128) : 0;
  if (st == 4) ++g_frames;
  dbz3::cine::Fotograma(base, st, st == 4 ? g_frames : 0);
  dbz3::gigantes::Fotograma(base, st == 4 ? g_frames : 0);
  dbz3::iwmini::Fotograma(base, st);   // minijuegos de IW (frente P4): modo libre con camara detras
  dbz3::cinematica::Fotograma(base, st);   // escenas 3D (frente R3)
  dbz3::escenarios::AplicarCamaraEscena(base);   // ZB: camara de la escena del cambio de zona de B2
  if (!Activo() || !mgr) return;
  static const bool sonda = std::getenv("DBZ3_SONDA_CLIMA") != nullptr;
  if (sonda && g_frames == 1) {
    static bool hecha = false;
    if (!hecha) Sonda(base);
    hecha = true;
  }
  // tras 20 s de pelea el cielo se oscurece del todo en 40 s (DBZ3_CLIMA=rapido: 3 s y 8 s)
  static const bool rapido = [] {
    const char* e = std::getenv("DBZ3_CLIMA");
    return e && std::strcmp(e, "rapido") == 0;
  }();
  const float t0 = rapido ? 3.0f : 20.0f, dur = rapido ? 8.0f : 40.0f;
  const float k = std::clamp((float(g_frames) / 60.0f - t0) / dur, 0.0f, 1.0f);
  const float suave = k * k * (3.0f - 2.0f * k);   // suave al empezar y al acabar
  if (suave <= 0.0f) return;
  // el oscurecimiento del juego: el mayor entre el suyo (modo hiper) y el del clima
  const float osc = kOscuro * suave;
  if (F(base, mgr + 35180) < osc) PonF(base, mgr + 35180, osc);
  if (F(base, mgr + 35184) < suave) PonF(base, mgr + 35184, suave);
  // niebla del juego (recien puesta) tenida hacia un azul de noche y mas cerca: el fondo lejano tambien se apaga
  const uint32_t col = REX_LOAD_U32(kFogColor);
  const bool on = REX_LOAD_U32(kFogOn) != 0;
  const float ini = on ? F(base, dbz3::GA(0x823106B4)) : 0.0f, fin = on ? F(base, dbz3::GA(0x823106B8)) : 4000.0f;
  constexpr uint8_t kNoche[3] = {18, 20, 44};
  uint32_t nuevo = col & 0xFF000000u;
  for (int c = 0; c < 3; ++c) {
    const float v = float((col >> (16 - 8 * c)) & 0xFF);
    nuevo |= uint32_t(v + (float(kNoche[c]) - v) * suave + 0.5f) << (16 - 8 * c);
  }
  PPCContext c = ctx;
  c.r3.u64 = 1;
  DBZ3_CALL_EU(sub_82085650)(c, base);
  c = ctx;
  c.r3.u64 = nuevo;
  DBZ3_CALL_EU(sub_82085670)(c, base);
  c = ctx;
  c.f1.f64 = ini;
  c.f2.f64 = fin + (std::max(ini + 150.0f, 0.15f * fin) - fin) * suave;
  DBZ3_CALL_EU(sub_82085688)(c, base);
  if (sonda && g_frames % 120 == 0)
    REXLOG_INFO("dbz3 sonda clima: {:.0f} s: oscuro {:.2f}, niebla {:08X} -> {:08X}, {:.0f}..{:.0f} -> {:.0f}",
                g_frames / 60.0f, osc, col, nuevo, ini, fin, c.f2.f64);
}
