// dbz3 - GIGANTES (ficha 19): el rival como jefe a escala gigante contra un luchador normal. US y EU (src/eu_map.h).
//
// Apagado por defecto. Se enciende con la opcion dbz3_jefe_gigante (escala del rival COM, p. ej. 1.8; 0 = no),
// con la regla de reto Activar(lado, escala) o con DBZ3_GIGANTE=<escala> (pruebas, jugador 2).
//
// Medido 2026-10-15 (sonda de cine.cpp): el bloque del luchador X = mgr+2096+8240*lado empieza con su matriz
// de mundo 4x4 (filas 0..2 = giro, fila 3 = posicion, la de X+48/X+64). Escalar las filas 0..2 al empezar el
// dibujo agranda el modelo entero (huesos, piel, sombra) desde los pies, que siguen en el suelo.
// La camara del juego encuadra a tamano normal: con un gigante se abre su campo de vision.
// Golpes (WL-19, medido 2026-10-15): cada cuerpo T (X+1616, y X+3280 en las transformaciones) lleva 22 puntos de
// mundo de 16 B (T+0: cintura, pecho, cuello, cabeza... manos y pies), sacados de los huesos de la logica con la
// tabla T+1340 por sub_820F9548(T, mascara). Se escalan con el gigante; el alcance de los golpes no sale de ahi
// (medido: tiene un tope de distancia que no se ha encontrado; ver escenarios.md, WL-19).
// Fuera en online (solo se veria en un PC).

#include "gigantes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "cine.h"
#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "net/netplay.h"

REXCVAR_DEFINE_DOUBLE(dbz3_jefe_gigante, 0.0, "DBZ3/Mods",
                      "Jefe gigante: escala del rival controlado por la consola (0 = apagado; p. ej. 1.8; tope 2.0)");
// RESTO-09: con un atacante gigante la victima del agarre (codigos 0x480..0x48F) se coloca en huesos de la logica a
// tamano normal y se ve desplazada. Encendido la lleva sobre la mano del gigante (desvio de partida x escala).
// Apagado = identico al original. Solo actua cuando ya hay un gigante (dbz3_jefe_gigante > 0 o DBZ3_GIGANTE).
REXCVAR_DEFINE_BOOL(dbz3_gigante_agarre, false, "DBZ3/Mods",
                    "Jefe gigante: coloca a la victima del agarre en la mano del gigante (apagado = original)");

namespace dbz3::gigantes {
namespace {

std::atomic<float> g_regla[2] = {0.0f, 0.0f};   // regla de reto por lado (0 = sin regla; la pone/lee el hilo del juego)

float LeeF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PonF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}

// Escala de un lado en este combate (1 = normal).
float Escala(uint8_t* base, uint32_t mgr, int lado) {
  if (const float r = g_regla[lado].load(); r > 0.0f) return r;
  static const float prueba = std::getenv("DBZ3_GIGANTE") ? float(std::atof(std::getenv("DBZ3_GIGANTE"))) : 0.0f;
  static const int lado_prueba = std::getenv("DBZ3_GIGANTE_LADO") ? std::atoi(std::getenv("DBZ3_GIGANTE_LADO")) - 1 : 1;
  if ((lado == lado_prueba || lado_prueba == 2) && prueba > 0.0f) return prueba;   // DBZ3_GIGANTE_LADO=3: los dos
  const float op = float(REXCVAR_GET(dbz3_jefe_gigante));
  // X+5028 es de 16 bits (-1 = COM; X+5030 va aparte: leido como 32 bits el COM daba -65535 y la opcion no actuaba)
  // (durante la intro de cine.cpp el COM lleva otro mando: cine::Mando da el de antes)
  const bool com = mgr && dbz3::cine::Mando(base, lado) == -1;
  return lado == 1 && op > 0.0f && com ? op : 1.0f;
}

// RESTO-09: agarre. Con un atacante gigante la victima (codigos 0x480..0x48F) queda en huesos de la logica a tamano
// normal. Se guarda al entrar el desvio (x,z) respecto a los pies del gigante y cada cuadro se vuelve a poner x escala.
// Se mueven las dos copias de la posicion conocidas: la del paso de combate (+48/+56) y la del dibujo (+64/+72).
// Apagado = no toca nada. Sonda: "gigante: agarre ..." con la distancia (medida de la ficha: error <= 1 u).
struct AgarreLado { bool dentro = false; float x = 0, y = 0, z = 0, cx = 0, cy = 0, cz = 0; };
// POST2: cuello de la victima respecto a sus pies (mundo), del esqueleto de la logica, como cinematica.cpp PuntoCuerpo:
// anclaje 2 (NECK) en x+2956, hueso [x+2952]+1032 + 64*i con su punto en +48, girado con la matriz del luchador (x+0..x+32).
bool CuelloRel(uint8_t* base, uint32_t x, float g[3]) {
  const uint32_t mi = REX_LOAD_U32(x + 2952), anc = REX_LOAD_U32(x + 2956);
  if (!mi || !anc) return false;
  const uint32_t tb = REX_LOAD_U32(mi + 1032);
  const int hb = int16_t(REX_LOAD_U16(anc + 4u));
  if (tb < 0x40000000u || tb >= 0xA0000000u || hb < 0 || hb > 255) return false;
  const uint32_t h = tb + 64u * uint32_t(hb);
  const float v[3] = {LeeF(base, h + 48), LeeF(base, h + 52), LeeF(base, h + 56)};
  for (uint32_t f = 0; f < 3; ++f)
    g[f] = LeeF(base, x + 16 * f) * v[0] + LeeF(base, x + 16 * f + 4) * v[1] + LeeF(base, x + 16 * f + 8) * v[2];
  return std::isfinite(g[1]) && g[1] > 2.0f && g[1] < 30.0f;
}
void AjusteAgarre(uint8_t* base, uint32_t mgr) {
  static AgarreLado ag[2];
  if (!REXCVAR_GET(dbz3_gigante_agarre) || dbz3::net::Session::Get().active()) {
    ag[0].dentro = ag[1].dentro = false;
    return;
  }
  float s[2];
  int grande = -1;
  for (int k = 0; k < 2; ++k) {
    s[k] = std::clamp(Escala(base, mgr, k), 0.5f, 2.0f);
    if (s[k] > 1.01f) grande = k;
  }
  if (grande < 0) {   // sin gigante no hay nada que colocar (y no se deja estado pegado)
    ag[0].dentro = ag[1].dentro = false;
    return;
  }
  const int victima = 1 - grande;
  const uint32_t xv = mgr + 2096 + 8240 * uint32_t(victima), xg = mgr + 2096 + 8240 * uint32_t(grande);
  const uint32_t code = REX_LOAD_U32(xv + 2928) & 0xFFFFu;   // el bit 0x10000 es una marca: 0x10481 = agarre de la victima
  const bool dentro = code >= 0x480u && code < 0x490u;
  if (!dentro) {
    if (ag[victima].dentro) REXLOG_INFO("dbz3 gigante: agarre lado {} termina (codigo {:03X})", victima + 1, code);
    ag[victima].dentro = false;
    return;
  }
  const float s2 = s[grande];
  if (!ag[victima].dentro) {   // primer cuadro: desvio de partida de la victima respecto a los pies del gigante
    ag[victima].dentro = true;
    ag[victima].x = LeeF(base, xv + 64) - LeeF(base, xg + 64);
    ag[victima].z = LeeF(base, xv + 72) - LeeF(base, xg + 72);
    ag[victima].y = LeeF(base, xv + 68) - LeeF(base, xg + 68);
    float g[3] = {0, 14.0f, 0};   // sin esqueleto: un cuello a 14 u (luchador normal)
    const bool hueso = CuelloRel(base, xv, g);
    ag[victima].cx = g[0], ag[victima].cy = g[1], ag[victima].cz = g[2];
    REXLOG_INFO("dbz3 gigante: agarre cuello de la victima a ({:.1f},{:.1f},{:.1f}){}", g[0], g[1], g[2], hueso ? "" : " (supuesto)");
    REXLOG_INFO("dbz3 gigante: agarre lado {} empieza (escala {:.2f}) a {:.1f} u del gigante", victima + 1, s2,
                std::sqrt(ag[victima].x * ag[victima].x + ag[victima].z * ag[victima].z));
  }
  // POST2: a tamano normal la mano del gigante esta en el cuello de la victima (pies + desvio + cuello); el gigante se
  // dibuja escalado desde sus pies, asi que esa mano queda en s x (desvio + cuello): los pies de la victima van a
  // s x desvio + (s-1) x cuello (antes: (s-1) x la cintura de la tabla X+1616, vieja fuera de los estados 3/4/5/8: rodilla)
  const float px = LeeF(base, xg + 64) + ag[victima].x * s2 + (s2 - 1.0f) * ag[victima].cx;
  const float pz = LeeF(base, xg + 72) + ag[victima].z * s2 + (s2 - 1.0f) * ag[victima].cz;
  const float py = LeeF(base, xg + 68) + ag[victima].y * s2 + (s2 - 1.0f) * ag[victima].cy;
  PonF(base, xv + 52, py);
  PonF(base, xv + 68, py);
  PonF(base, xv + 48, px);
  PonF(base, xv + 56, pz);
  PonF(base, xv + 64, px);
  PonF(base, xv + 72, pz);
  static const bool sonda = std::getenv("DBZ3_SONDA_GIGANTE") != nullptr;
  static uint32_t cuadro = 0;
  if (sonda && ++cuadro % 20 == 0) {
    const float dx = LeeF(base, xv + 64) - LeeF(base, xg + 64), dz = LeeF(base, xv + 72) - LeeF(base, xg + 72);
    REXLOG_INFO("dbz3 gigante: agarre a {:.1f} u (desvio base {:.1f}, escala {:.2f})", std::sqrt(dx * dx + dz * dz),
                std::sqrt(ag[victima].x * ag[victima].x + ag[victima].z * ag[victima].z), s2);
  }
}

}  // namespace

float EscalaDe(uint8_t* base, int lado) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr || lado < 0 || lado > 1 || dbz3::net::Session::Get().active()) return 1.0f;
  return std::clamp(Escala(base, mgr, lado), 0.5f, 2.0f);
}

bool ReglaActiva() { return g_regla[0].load() > 0.0f || g_regla[1].load() > 0.0f; }

void Activar(int lado, float escala) {
  if (lado < 0 || lado > 1) return;
  const float v = escala > 0.0f ? std::clamp(escala, 0.5f, 2.0f) : 0.0f;
  const float old = g_regla[lado].load();
  if (old == v) return;   // sin cambio: no se repite el aviso en cada combate del reto
  g_regla[lado] = v;
  REXLOG_INFO("dbz3 gigante: regla de reto lado {} {}", lado + 1, v > 0.0f ? std::to_string(v) + "x" : std::string("quitada"));
}

void Fotograma(uint8_t* base, uint32_t frames_pelea) {
  if (dbz3::net::Session::Get().active()) return;
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr) return;
  static const bool sonda = std::getenv("DBZ3_SONDA_GIGANTE") != nullptr;
  float ojo[3] = {0, 0, 0}, obj[3] = {0, 0, 0};
  // La camara solo la usan las sondas: si falla, el modo sigue.
  const bool cam = dbz3::cine::LeerCamara(base, ojo, obj);
  if (sonda && cam && frames_pelea && frames_pelea % 120 == 0)   // vida (X+6080) y distancia de la camara
    REXLOG_INFO("dbz3 sonda gigante: {} s: vida {:.0f} / {:.0f}, camara del juego a {:.1f} (altura {:.1f})",
                frames_pelea / 60, LeeF(base, mgr + 2096 + 6080), LeeF(base, mgr + 2096 + 8240 + 6080),
                std::sqrt((ojo[0] - obj[0]) * (ojo[0] - obj[0]) + (ojo[2] - obj[2]) * (ojo[2] - obj[2])), ojo[1]);
  float mayor = 1.0f;
  for (int lado = 0; lado < 2; ++lado) mayor = std::max(mayor, std::clamp(Escala(base, mgr, lado), 0.5f, 2.0f));
  if (frames_pelea == 1 && mayor > 1.01f) REXLOG_INFO("dbz3 gigante: escala {:.2f}", mayor);
  if (sonda && frames_pelea) {   // cada golpe que quita vida: a que distancia estaban (alcance)
    static float vida[2] = {0, 0};
    for (uint32_t k = 0; k < 2; ++k) {
      const float v = LeeF(base, mgr + 2096 + 8240 * k + 6080);
      if (frames_pelea > 1 && v < vida[k]) {
        const uint32_t a = mgr + 2096, b = a + 8240;
        const float dx = LeeF(base, a + 64) - LeeF(base, b + 64), dz = LeeF(base, a + 72) - LeeF(base, b + 72);
        REXLOG_INFO("dbz3 sonda gigante: golpe al lado {} ({:.0f} de dano) a distancia {:.1f}", k + 1, vida[k] - v,
                    std::sqrt(dx * dx + dz * dz));
      }
      vida[k] = v;
    }
  }
  if (sonda && frames_pelea == 120)   // alturas de los puntos de golpe (cintura, cuello, cabeza) sobre los pies
    for (uint32_t k = 0; k < 2; ++k) {
      const uint32_t x = mgr + 2096 + 8240 * k;
      const float y0 = LeeF(base, x + 68);
      REXLOG_INFO("dbz3 sonda gigante: lado {} puntos de golpe: cintura {:.1f} cuello {:.1f} cabeza {:.1f}", k + 1,
                  LeeF(base, x + 1616 + 4) - y0, LeeF(base, x + 1616 + 32 + 4) - y0, LeeF(base, x + 1616 + 48 + 4) - y0);
      // huesos de la logica: [X+2952]+1032, 64 B cada uno; el de la cabeza por la tabla de anclajes [X+2956][3]
      const uint32_t mi = REX_LOAD_U32(x + 2952), anc = REX_LOAD_U32(x + 2956);
      if (mi && anc) {
        const uint32_t h = REX_LOAD_U32(mi + 1032) + 64 * uint32_t(int16_t(REX_LOAD_U16(anc + 6)));
        REXLOG_INFO("dbz3 sonda gigante: lado {} hueso de la cabeza (al empezar el dibujo) a {:.1f}", k + 1,
                    LeeF(base, h + 52) - y0);
      }
    }
  // PRUEBA de alcance (DBZ3_GIGANTE_DISTANCIA=d): al empezar la pelea el jugador 2 se pone a d del 1, en el eje x
  static const float distancia =
      std::getenv("DBZ3_GIGANTE_DISTANCIA") ? float(std::atof(std::getenv("DBZ3_GIGANTE_DISTANCIA"))) : 0.0f;
  // (cada fotograma: los dos se quedan en su sitio, asi solo cuenta el alcance del golpe, no el avance)
  static float sitio[2] = {0, 0}, sitio_z = 0;
  if (distancia > 0.0f && frames_pelea >= 2) {
    const uint32_t x1 = mgr + 2096, x2 = x1 + 8240;
    if (frames_pelea == 2) {
      sitio[0] = LeeF(base, x1 + 64);
      sitio[1] = sitio[0] + distancia;
      sitio_z = LeeF(base, x1 + 72);
      REXLOG_INFO("dbz3 gigante: prueba de alcance a {:.1f}", distancia);
    }
    for (uint32_t k = 0; k < 2; ++k)
      for (uint32_t o : {48u, 64u, 80u}) {
        PonF(base, (k ? x2 : x1) + o, sitio[k]);
        PonF(base, (k ? x2 : x1) + o + 8, sitio_z);
      }
  }
  // encuadre: se abre el campo de vision (mgr+0x1C4, 0,785 de serie) en proporcion a la escala. El ojo y el
  // objetivo no se tocan: la camara del juego parte de los del fotograma anterior y se iria lejos (medido: a los
  // 2 s de alejarlos estaba a 1e16). La intro de cine.cpp lleva su propia camara.
  // El campo de vision se abre lo justo para que el gigante ocupe lo mismo que un luchador normal a esa distancia:
  // tan(fov'/2) = escala * tan(fov/2) (triangulos semejantes) y, si la camara se acerca, lo que pida la cabeza
  // (punto 3 de los 22) para no salirse por arriba. Antes se abria la mitad de lo necesario (+50 % de la escala) y
  // en el cuerpo a cuerpo la cabeza salia cortada (medido 2026-11-09). Tope 1,55 rad (89 grados verticales): a partir
  // de ahi el ojo de pez estropea mas de lo que arregla. 2,0 encuadra; 2,5 puede seguir cortando (la fila F1 avisa).
  static float fov_juego = 0.0f, fov_puesto = 0.0f;
  static bool tocado = false;
  const float fov = LeeF(base, mgr + 0x1C4);
  // Se abre desde la pelea (estado 4) y se queda en el KO/victoria (5+), donde la camara tambien es de cerca: si se
  // cerrara al acabar la pelea, el gigante salia cortado en la caida (medido 2026-11-09). En la presentacion (1-3)
  // no se toca: el gigante se dibuja a tamano normal y su camara es la del guion.
  // RESTO-09: el agarre va antes de los retornos de la camara (un campo de vision fuera de rango o una toma de cine
  // durante el agarre lo saltaban). Sin gigante o con la cvar apagada no hace nada.
  if (REX_LOAD_U32(mgr + 35128) >= 4) AjusteAgarre(base, mgr);
  if (mayor <= 1.01f || dbz3::cine::EnCurso() || REX_LOAD_U32(mgr + 35128) < 4) {
    if (tocado) PonF(base, mgr + 0x1C4, fov_juego);   // fuera del combate o sin gigante: el del juego
    tocado = false;
    return;
  }
  if (!tocado || fov != fov_puesto) fov_juego = fov;   // el juego lo rehace (o es el primero): ese es el suyo
  if (!(fov_juego > 0.3f && fov_juego < 1.2f)) return;
  float fov_necesario = 2.0f * std::atan(std::tan(0.5f * fov_juego) * mayor);
  // Si la camara esta muy cerca, la cabeza del gigante queda por encima del eje aunque el cuerpo quepa: se mira el
  // punto de cabeza (punto 3 de los 22, ya escalado) y se abre lo justo para que entre (con 7 grados de margen).
  if (cam) {
    const float vx0 = obj[0] - ojo[0], vy0 = obj[1] - ojo[1], vz0 = obj[2] - ojo[2];
    const float vl = std::sqrt(vx0 * vx0 + vy0 * vy0 + vz0 * vz0);
    if (vl > 1e-3f) {
      const float vx = vx0 / vl, vy = vy0 / vl, vz = vz0 / vl;
      float ux = -vx * vy, uy = 1.0f - vy * vy, uz = -vz * vy;   // arriba del mundo, perpendicular a la vista
      const float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
      if (ul > 1e-4f) {
        ux /= ul, uy /= ul, uz /= ul;
        for (uint32_t lado = 0; lado < 2; ++lado) {
          if (std::clamp(Escala(base, mgr, int(lado)), 0.5f, 2.0f) < 1.02f) continue;
          const uint32_t x = mgr + 2096 + 8240 * lado;
          const bool oculta_a = REX_LOAD_U8(x + 3260) || REX_LOAD_U8(x + 3261);
          const bool oculta_b = REX_LOAD_U8(x + 4924) || REX_LOAD_U8(x + 4925);
          if (oculta_a && oculta_b) continue;
          const uint32_t cabeza = (oculta_a ? x + 3280 : x + 1616) + 48;   // punto 3 (16 B por punto)
          float hx = LeeF(base, cabeza) - ojo[0], hy = LeeF(base, cabeza + 4) - ojo[1], hz = LeeF(base, cabeza + 8) - ojo[2];
          const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
          if (hl < 1e-3f) continue;
          hx /= hl, hy /= hl, hz /= hl;
          const float delante = hx * vx + hy * vy + hz * vz;
          if (delante < 0.2f) continue;   // detras o al lado de la camara
          const float arriba = std::atan2(hx * ux + hy * uy + hz * uz, delante);
          if (arriba > 0.0f) fov_necesario = std::max(fov_necesario, 2.0f * arriba + 0.12f);
        }
      }
    }
  }
  fov_puesto = std::min(1.55f, std::max(fov_juego, fov_necesario));
  PonF(base, mgr + 0x1C4, fov_puesto);
  if (!tocado) REXLOG_INFO("dbz3 gigante: campo de vision {:.3f} -> {:.3f}", fov_juego, fov_puesto);
  tocado = true;
}

}  // namespace dbz3::gigantes

// Dibujo del luchador X (r3): su matriz de mundo (X+0, filas 0..2) a la escala del lado solo mientras se dibuja;
// despues vuelve la del juego (la logica del combate no la ve cambiada).
// Solo desde la pelea (mgr+35128 >= 4: pelea, KO y victoria). La presentacion del juego (estados 1-3) usa una camara
// de guion que no se deja mover (escenarios.md R2/R6b) y con el jefe a 1,8x solo salian las piernas (plano de
// presentacion) o la cabeza cortada (READY), medido 2026-11-09. Ahi se dibuja a tamano normal y crece al empezar el
// combate (el flash de FIGHT tapa el cambio); la intro de cine ya corre en estado 4 y su encuadre usa EscalaDe.
DBZ3_HOOK_EU(sub_82109D90) {
  using namespace dbz3::gigantes;
  const uint32_t x = ctx.r3.u32, mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr || REX_LOAD_U32(mgr + 35128) < 4) {
    orig(ctx, base);
    return;
  }
  const int lado = x == mgr + 2096 ? 0 : x == mgr + 2096 + 8240 ? 1 : -1;
  const float s = lado >= 0 && !dbz3::net::Session::Get().active() ? std::clamp(Escala(base, mgr, lado), 0.5f, 2.0f)
                                                                  : 1.0f;
  if (s > 0.99f && s < 1.01f) {
    orig(ctx, base);
    return;
  }
  uint32_t mat[12];   // la matriz del juego (orig es el original de la funcion)
  for (uint32_t i = 0; i < 12; ++i) mat[i] = REX_LOAD_U32(x + 4 * i);
  for (uint32_t i = 0; i < 12; ++i)
    if (i % 4 != 3) PonF(base, x + 4 * i, LeeF(base, x + 4 * i) * s);
  orig(ctx, base);
  for (uint32_t i = 0; i < 12; ++i) REX_STORE_U32(x + 4 * i, mat[i]);
  static const bool sonda = std::getenv("DBZ3_SONDA_GIGANTE") != nullptr;
  static int n = 0;
  if (sonda && n < 3 && REX_LOAD_U32(x + 2952) && REX_LOAD_U32(x + 2956)) {
    const uint32_t mi = REX_LOAD_U32(x + 2952), anc = REX_LOAD_U32(x + 2956);
    const uint32_t h = REX_LOAD_U32(mi + 1032) + 64 * uint32_t(int16_t(REX_LOAD_U16(anc + 6)));
    if (REX_LOAD_U32(mi + 1032) && ++n)
      REXLOG_INFO("dbz3 sonda gigante: lado {} hueso de la cabeza (tras dibujar) a {:.1f}", lado + 1,
                  LeeF(base, h + 52) - LeeF(base, x + 68));
  }
}

// Puntos de golpe del cuerpo T (r3) que pide la mascara r4 (bit i = punto i): tras calcularlos el juego (de los
// huesos de la logica, [T+1336]+1032 por la tabla de anclajes [T+1340]), los de un gigante se escalan desde los pies
// de su luchador (T+1384): el cuerpo del gigante (cabeza, manos...) esta donde se ve.
// Medido (WL-19, escenarios.md): esto no alarga el alcance de sus golpes. Escalar tambien los huesos de la logica
// tampoco (con los dos clavados, ni gigante contra gigante entra a 30 o 40) y ademas sus golpes pasaban por encima
// del rival pequeno (a 21 no le daba; en 12 s de combate no le quito vida): se probo y se quito.
DBZ3_HOOK_EU(sub_820F9548) {
  using namespace dbz3::gigantes;
  const uint32_t t = ctx.r3.u32, mascara = ctx.r4.u32;
  orig(ctx, base);
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr || REX_LOAD_U32(mgr + 35128) < 4) return;   // presentacion: como el dibujo, sin escala
  const uint32_t x = t ? REX_LOAD_U32(t + 1384) : 0;
  const int lado = x == mgr + 2096 ? 0 : x == mgr + 2096 + 8240 ? 1 : -1;
  if (lado < 0 || dbz3::net::Session::Get().active()) return;
  const float s = std::clamp(Escala(base, mgr, lado), 0.5f, 2.0f);
  if (s > 0.99f && s < 1.01f) return;
  const float pie[3] = {LeeF(base, x + 64), LeeF(base, x + 68), LeeF(base, x + 72)};
  uint32_t m = mascara;
  for (uint32_t i = 0; i < 22 && m; ++i, m >>= 1) {
    if (!(m & 1)) continue;
    for (uint32_t c = 0; c < 3; ++c) {
      const uint32_t a = t + 16 * i + 4 * c;
      PonF(base, a, pie[c] + (LeeF(base, a) - pie[c]) * s);
    }
  }
}

// ---- Golpes del gigante (WL-19, RE 2026-10-15) -------------------------------------------------------------
// sub_8211B738(V) (cuerpo a cuerpo) y sub_82112588(V) (efectos pegados) miran si el atacante A = [V+4948] le da a V:
// cada nodo de golpe de A (lista [A+2752], siguiente +44; +0 posicion, +16 la anterior, [+32] la linea de golpe:
// +12 radio u8) contra cada punto de cuerpo de V (V+1616 + 16 i, i < 22) con su radio [[V+2996] + 24 + 4 i]:
// distancia^2 <= (radio_i + radio_golpe * [[A+2996]+0])^2. Los nodos salen de los huesos de la logica sin escalar
// (sub_82112AB0), asi que el tope (~22) era el de un luchador normal. Durante esa comprobacion, y solo para un
// gigante: sus nodos (posicion de mundo) se alejan de sus pies en horizontal x s y en vertical x la escala del
// golpeado, su radio de golpe x s (tabla +0) y, si el golpeado es el gigante, sus radios de cuerpo x s (sus puntos
// ya estan escalados). Despues todo vuelve a como estaba.
namespace {
using GolpeFn = void (*)(PPCContext&, uint8_t*);
void ConGolpeGigante(PPCContext& ctx, uint8_t* base, GolpeFn orig) {
  using namespace dbz3::gigantes;
  const uint32_t v = ctx.r3.u32, mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  const uint32_t a = v ? REX_LOAD_U32(v + 4948) : 0;
  auto lado = [&](uint32_t x) { return mgr && x == mgr + 2096 ? 0 : mgr && x == mgr + 2096 + 8240 ? 1 : -1; };
  const int la = lado(a), lv = lado(v);
  if (la < 0 || lv < 0 || dbz3::net::Session::Get().active()) {
    orig(ctx, base);
    return;
  }
  const float sa = std::clamp(Escala(base, mgr, la), 0.5f, 2.0f), sv = std::clamp(Escala(base, mgr, lv), 0.5f, 2.0f);
  const bool ga = sa < 0.99f || sa > 1.01f, gv = sv < 0.99f || sv > 1.01f;
  if (!ga && !gv) {
    orig(ctx, base);
    return;
  }
  uint32_t nodos[64], guardados[64][8], n = 0;
  uint32_t tabla_a = 0, tam_a = 0, tabla_v = 0, radios_v[22] = {};
  if (ga) {
    const float pie[3] = {LeeF(base, a + 64), LeeF(base, a + 68), LeeF(base, a + 72)};
    for (uint32_t nd = REX_LOAD_U32(a + 2752); nd >= 0x40000000u && nd < 0xA0000000u && n < 64; nd = REX_LOAD_U32(nd + 44)) {
      nodos[n] = nd;
      for (uint32_t k = 0; k < 4; ++k) guardados[n][k] = REX_LOAD_U32(nd + 4 * k);
      for (uint32_t k = 0; k < 4; ++k) guardados[n][4 + k] = REX_LOAD_U32(nd + 16 + 4 * k);
      // en horizontal x s (su alcance); en vertical x la escala del golpeado: el puno de un gigante (a 23 de altura)
      // pasaria por encima de un rival normal (medido: a 21 no le daba)
      for (uint32_t o : {0u, 16u})
        for (uint32_t c = 0; c < 3; ++c)
          PonF(base, nd + o + 4 * c, pie[c] + (LeeF(base, nd + o + 4 * c) - pie[c]) * (c == 1 ? sv : sa));
      ++n;
    }
    tabla_a = REX_LOAD_U32(a + 2996);
    if (tabla_a >= 0x40000000u && tabla_a < 0xA0000000u) {
      tam_a = REX_LOAD_U32(tabla_a);
      PonF(base, tabla_a, LeeF(base, tabla_a) * sa);
    } else {
      tabla_a = 0;
    }
  }
  if (gv) {
    tabla_v = REX_LOAD_U32(v + 2996);
    if (tabla_v >= 0x40000000u && tabla_v < 0xA0000000u) {
      for (uint32_t i = 0; i < 22; ++i) {
        radios_v[i] = REX_LOAD_U32(tabla_v + 24 + 4 * i);
        PonF(base, tabla_v + 24 + 4 * i, LeeF(base, tabla_v + 24 + 4 * i) * sv);
      }
    } else {
      tabla_v = 0;
    }
  }
  static const bool sonda = std::getenv("DBZ3_SONDA_GIGANTE") != nullptr;
  if (sonda && ga) {   // nodos de golpe activos (+8 bit 0) del gigante: lo mas cerca que pasan de un punto del golpeado
    static int veces = 0;
    for (uint32_t i = 0; i < n && veces < 40; ++i) {
      const uint32_t d = REX_LOAD_U32(nodos[i] + 32);
      if (d < 0x40000000u || d >= 0xA0000000u || !(REX_LOAD_U16(d + 8) & 1)) continue;
      const uint32_t tv = REX_LOAD_U32(v + 2996);
      const float rg = float(REX_LOAD_U8(d + 12)) * (tabla_a ? LeeF(base, tabla_a) : 1.0f);
      float mejor = 1e30f, falta = 0;
      uint32_t im = 0;
      for (uint32_t k = 0; k < 22; ++k) {
        float d2 = 0;
        for (uint32_t c = 0; c < 3; ++c) {
          const float e = LeeF(base, nodos[i] + 4 * c) - LeeF(base, v + 1616 + 16 * k + 4 * c);
          d2 += e * e;
        }
        if (d2 < mejor) mejor = d2, im = k, falta = tv ? LeeF(base, tv + 24 + 4 * k) : 0;
      }
      ++veces;
      REXLOG_INFO("dbz3 sonda gigante: nodo {:08X} activo (radio u8 {} x {:.2f}): altura {:.1f}, punto {} a {:.1f} (radio {:.1f}); "
                  "sin escalar {:.1f} {:.1f} {:.1f}", nodos[i], REX_LOAD_U8(d + 12), tabla_a ? LeeF(base, tabla_a) : 0.0f,
                  LeeF(base, nodos[i] + 4) - LeeF(base, a + 68), im, std::sqrt(mejor), falta,
                  [&] { uint32_t u = guardados[i][0]; float f; std::memcpy(&f, &u, 4); return f; }(),
                  [&] { uint32_t u = guardados[i][1]; float f; std::memcpy(&f, &u, 4); return f; }(),
                  [&] { uint32_t u = guardados[i][2]; float f; std::memcpy(&f, &u, 4); return f; }());
      (void)rg;
    }
  }
  orig(ctx, base);
  if (tabla_v)
    for (uint32_t i = 0; i < 22; ++i) REX_STORE_U32(tabla_v + 24 + 4 * i, radios_v[i]);
  if (tabla_a) REX_STORE_U32(tabla_a, tam_a);
  for (uint32_t i = 0; i < n; ++i)
    for (uint32_t k = 0; k < 4; ++k) {
      REX_STORE_U32(nodos[i] + 4 * k, guardados[i][k]);
      REX_STORE_U32(nodos[i] + 16 + 4 * k, guardados[i][4 + k]);
    }
}
}  // namespace

DBZ3_HOOK_EU(sub_8211B738) { ConGolpeGigante(ctx, base, orig); }
DBZ3_HOOK_EU(sub_82112588) { ConGolpeGigante(ctx, base, orig); }
