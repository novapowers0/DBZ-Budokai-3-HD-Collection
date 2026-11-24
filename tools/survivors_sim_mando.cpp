// NSUR (Gemini Neon; adaptado en AP_NSUR): simulador con perfiles sinteticos de mando (XInput / D-pad)
// y comparativa directa contra el bot C10 (tests/survivors_juego_test.cpp).
// Enlaza directamente src/survivors/{juego,sim,tienda}.cpp sin duplicar logica del juego.
// Uso:
//   sim_mando.exe --toml <ruta> [--perfil <nombre|todos>] [--dif 0|1|2|todos] [--reps N]
//                 [--mejoras 0..5] [--vida F] [--dano F] [--zoom F] [--semilla S]
//                 [--solo-3pj] [--csv <ruta>] [--json <ruta>] [--selftest]
#define main main_c10_oculto
#include "survivors_juego_test.cpp"
#undef main

#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace nsur {

using namespace dbz3::survivors;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPicado = 0.95993f;  // src/survivors/impostores.h:35 (55 grados)

struct ParamsPerfil {
  const char* nombre;
  bool es_bot_c10;
  int c10_perfil;          // kTorpe / kMedio / kExperto si es_bot_c10
  bool autoapuntar;
  bool dpad;               // true = cruceta 8 direcciones (modo.cpp:178-187), false = stick analogico (modo.cpp:173-177)
  int reaccion_cuadros;    // cada cuantos cuadros (1/60 s) reevalua direccion
  int reaccion_aviso;      // retraso extra (cuadros) antes de esquivar un aviso recien aparecido
  float vel_stick;         // velocidad de desplazamiento del pulgar en espacio stick (unidades/s, 1.0 = centro a borde)
  float ruido_ang;         // desviacion angular del pulgar (rad)
  float umbral_giro_disp;  // margen minimo a enemigo (u) para girar a apuntar (stutter-step); >99 = nunca gira
  float ventana_recarga;   // segundos antes del disparo direccional en que intenta encarar (stutter-step)
  float habilidad_cartas;  // 0.0 = azar puro, 0.65 = jugador medio, 0.95 = veterano
  bool busca_objetos;      // cofres/imanes/capsulas visibles en pantalla
  bool esquiva_avisos;     // franjas rojas, rayos y sombras visibles
  bool resortea;           // usa reroll con criterio
};

// Perfiles sinteticos: solo la curva analogica se toma de Modo::Mando(). Latencia, ruido,
// decisiones y campo de vision son hipotesis; las cifras no son una calibracion con humanos.
static const ParamsPerfil kPerfiles[] = {
    // Lineas base C10 (para reproducir y contrastar SUR1/SUR3)
    {"bot_c10_medio_auto",   true,  kMedio,   true,  false, 1,  0, 999.0f, 0.00f, 999.0f, 0.00f, 0.00f, false, false, false},
    {"bot_c10_medio_noauto", true,  kMedio,   false, false, 1,  0, 999.0f, 0.00f, 999.0f, 0.00f, 0.00f, false, false, false},
    {"bot_c10_exp_auto",     true,  kExperto, true,  false, 1,  0, 999.0f, 0.00f, 999.0f, 0.00f, 1.00f, true,  true,  true},
    {"bot_c10_exp_noauto",   true,  kExperto, false, false, 1,  0, 999.0f, 0.00f, 999.0f, 0.00f, 1.00f, true,  true,  true},
    // Perfiles de mando real (autoapuntar OFF = valor de serie en src/survivors/modo_mundo.cpp:16)
    {"mando_novato",         false, 0,        false, false, 14, 16, 4.5f,  0.22f, 5.5f,   0.14f, 0.35f, false, true,  false},
    {"mando_medio",          false, 0,        false, false, 10, 12, 7.0f,  0.12f, 3.2f,   0.22f, 0.70f, true,  true,  false},
    {"mando_veterano",       false, 0,        false, false, 6,  8,  11.0f, 0.06f, 2.3f,   0.28f, 0.95f, true,  true,  true},
    {"dpad_medio",           false, 0,        false, true,  10, 12, 8.0f,  0.00f, 3.2f,   0.22f, 0.70f, true,  true,  false},
    // Espejos de mando real con autoapuntar ON (aisla el efecto exacto de dbz3_survivors_autoapuntado=true)
    {"mando_novato_auto",    false, 0,        true,  false, 14, 16, 4.5f,  0.22f, 999.0f, 0.00f, 0.35f, false, true,  false},
    {"mando_medio_auto",     false, 0,        true,  false, 10, 12, 7.0f,  0.12f, 999.0f, 0.00f, 0.70f, true,  true,  false},
    {"mando_veterano_auto",  false, 0,        true,  false, 6,  8,  11.0f, 0.06f, 999.0f, 0.00f, 0.95f, true,  true,  true},
};

// Aplica exactamente la misma transformacion fisica de Modo::Mando() (src/survivors/modo.cpp:171-189)
static Entrada ProcesarMandoReal(float raw_x, float raw_y, bool dpad) {
  Entrada e{};
  if (dpad) {
    const float m = std::hypot(raw_x, raw_y);
    if (m <= 0.25f) return e;
    const float ang = std::atan2(raw_y, raw_x);
    const int sec = int(std::round(ang / (kPi * 0.25f)));
    const float q = float(sec) * (kPi * 0.25f);
    e.x = std::abs(std::cos(q)) > 0.35f ? (std::cos(q) > 0 ? 1.0f : -1.0f) : 0.0f;
    e.y = std::abs(std::sin(q)) > 0.35f ? (std::sin(q) > 0 ? 1.0f : -1.0f) : 0.0f;
    const float l = std::hypot(e.x, e.y);
    if (l > 1.0f) e.x /= l, e.y /= l;
    return e;
  }
  const int16_t ix = int16_t(std::clamp(raw_x, -1.0f, 1.0f) * 32767.0f);
  const int16_t iy = int16_t(std::clamp(raw_y, -1.0f, 1.0f) * 32767.0f);
  const float lx = float(ix) / 32767.0f;
  const float ly = float(iy) / 32767.0f;
  const float m = std::sqrt(lx * lx + ly * ly);
  if (m > 0.25f) {
    const float r = std::min(1.0f, (m - 0.25f) / 0.75f) / m;
    e.x = lx * r;
    e.y = ly * r;
  }
  return e;
}

static bool EsDireccional(Arquetipo a) {
  return a == Arquetipo::Rayo || a == Arquetipo::Cono || a == Arquetipo::RayoFino ||
         a == Arquetipo::Rafaga || a == Arquetipo::Teledirigida || a == Arquetipo::Bumeran;
}

struct PilotoMando {
  uint64_t s;
  ParamsPerfil pf;
  float zoom = 1.8f;
  float stick_x = 0.0f, stick_y = 0.0f;   // posicion fisica actual del pulgar en [-1, 1]
  float deseo_x = 0.0f, deseo_y = 1.0f;   // objetivo de stick tras reaccion
  int paso_ultimo_plan = -999;
  int cuadros_giro_disp = 0;              // breve toque de encaramiento para disparar (stutter-step)
  float dir_disp_x = 0.0f, dir_disp_y = 1.0f;
  float ciclo_orbita = 1.0f;              // +1 horario, -1 antihorario

  uint32_t Azar() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return uint32_t(s >> 33);
  }
  float Azar01() { return float(Azar() & 0xFFFFFF) / 16777216.0f; }
  float AzarSim() { return Azar01() * 2.0f - 1.0f; }

  // Geometria de pantalla real en 16:9 (modo_mundo.cpp:391)
  bool Visible(float px, float py, float x, float y) const {
    const float sp = std::sin(kPicado);
    const float half_w = (16.0f / 18.0f) * 22.0f / zoom;  // ~10.86 u a zoom 1.8
    const float up_h = (22.0f * 0.55f) / (zoom * sp);     // ~8.21 u arriba
    const float dn_h = (22.0f * 0.45f) / (zoom * sp);     // ~6.71 u abajo
    const float dx = x - px, dy = y - py;
    return std::abs(dx) <= half_w && dy <= up_h && dy >= -dn_h;
  }

  // Distancia a segmento + normal perpendicular para salir de una franja roja (AvisoLinea)
  static float DistYNormalSeg(float px, float py, float x1, float y1, float x2, float y2, float* nx, float* ny) {
    const float sx = x2 - x1, sy = y2 - y1;
    const float l2 = sx * sx + sy * sy + 1e-6f;
    float t = ((px - x1) * sx + (py - y1) * sy) / l2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = x1 + sx * t, cy = y1 + sy * t;
    const float dx = px - cx, dy = py - cy;
    const float d = std::hypot(dx, dy);
    if (d > 1e-4f) {
      *nx = dx / d;
      *ny = dy / d;
    } else {
      const float sl = std::sqrt(l2);
      *nx = -sy / sl;
      *ny = sx / sl;
    }
    return d;
  }

  bool TieneDireccionalPronto(const Juego& j) const {
    if (pf.autoapuntar || pf.umbral_giro_disp > 50.0f) return false;
    const Datos& d = *j.datos;
    for (int t = 0; t < j.nTecnicas; ++t) {
      const DefTecnica& dt = d.tecnicas[size_t(j.tecnicas[t].def)];
      if (!EsDireccional(dt.arq)) continue;
      // En Juego::Paso no se expone recarga_ privada; estimamos ventana periodica con fase real del reloj
      const float rec = std::max(0.25f, dt.recarga[std::clamp(j.tecnicas[t].nivel - 1, 0, kNivelMax - 1)]);
      const float resto = rec - std::fmod(j.tiempo, rec);
      if (resto <= pf.ventana_recarga) return true;
    }
    return false;
  }

  void Planificar(const Juego& j) {
    const float px = j.jugador.x, py = j.jugador.y;
    if (Azar() % 60 == 0) ciclo_orbita = -ciclo_orbita;

    float fx = 0.0f, fy = 0.0f;
    float min_enem_d2 = 1e9f;
    float cerca_ex = 0.0f, cerca_ey = 0.0f;
    float centro_x = 0.0f, centro_y = 0.0f;
    float peso_c = 0.0f;

    // 1) Amenaza de enemigos visibles (el humano percibe bien los cercanos; los muy lejanos menos)
    const float radio_huye = 6.2f;
    const float radio_huye2 = radio_huye * radio_huye;
    for (int i = 0, n = j.NumEnemigos(); i < n; ++i) {
      const VistaEnemigo e = j.Enemigo(i);
      if (!Visible(px, py, e.x, e.y)) continue;
      const float vx = px - e.x, vy = py - e.y;
      const float d2 = vx * vx + vy * vy;
      if (d2 < min_enem_d2) min_enem_d2 = d2, cerca_ex = e.x, cerca_ey = e.y;
      if (d2 < 81.0f) {
        const float w = 1.0f / (d2 + 1.5f);
        centro_x += e.x * w;
        centro_y += e.y * w;
        peso_c += w;
      }
      if (d2 < radio_huye2 && d2 > 1e-6f) {
        const float d = std::sqrt(d2);
        const float urg = (e.aviso > 0.5f ? 2.4f : 1.0f) * (e.mini ? 2.0f : 1.0f);
        const float inv = urg / (d2 + 0.35f);
        fx += vx * inv;
        fy += vy * inv;
        // Componente tangencial (orbitar la horda en vez de chocar en linea recta)
        const float tang = 0.22f * ciclo_orbita / (d + 0.8f);
        fx += -vy / d * tang;
        fy +=  vx / d * tang;
      }
    }

    // 2) Jefe visible
    const bool jefe_activo = j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto;
    if (jefe_activo && Visible(px, py, j.jefe.x, j.jefe.y)) {
      const float vx = px - j.jefe.x, vy = py - j.jefe.y;
      const float d2 = vx * vx + vy * vy + 1e-3f;
      if (d2 < 85.0f) {
        const float peso = j.jefe.estado == EstadoJefe::Carga ? 10.0f : 4.5f;
        fx += peso * vx / d2;
        fy += peso * vy / d2;
      }
    }

    // 3) Proyectiles y avisos hostiles visibles (respetando tiempo de reaccion al aviso)
    for (int k = 0; k < j.nEfectos; ++k) {
      const Efecto2D& e = j.efectos[k];
      if (!e.hostil) continue;
      const float edad = e.vida0 - e.vida;
      const float retraso_s = float(pf.reaccion_aviso) / 60.0f;
      if (e.forma == Forma::Bola || e.forma == Forma::Disco) {
        if (!Visible(px, py, e.x, e.y)) continue;
        const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy + 1e-3f;
        if (d2 < 14.0f) fx += 2.2f * vx / d2, fy += 2.2f * vy / d2;
      } else if (pf.esquiva_avisos && edad >= retraso_s) {
        if (e.forma == Forma::AvisoRayo || e.forma == Forma::Sombra || e.forma == Forma::AvisoAnillo) {
          if (!Visible(px, py, e.x, e.y)) continue;
          const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy + 1e-3f;
          const float r = e.radio + 1.8f;
          if (d2 < r * r) fx += 6.0f * vx / d2, fy += 6.0f * vy / d2;
        } else if (e.forma == Forma::AvisoLinea) {
          float nx = 0, ny = 0;
          const float dist = DistYNormalSeg(px, py, e.x, e.y, e.x2, e.y2, &nx, &ny);
          const float margen = e.ancho * 0.5f + 1.6f;
          if (dist < margen) fx += 8.0f * nx, fy += 8.0f * ny;
        }
      }
    }

    // 4) Gemas visibles en pantalla
    int g = -1;
    float mg = 1e9f;
    for (int k = 0; k < j.nGemas; ++k) {
      if (!Visible(px, py, j.gx[k], j.gy[k])) continue;
      const float d2 = (j.gx[k] - px) * (j.gx[k] - px) + (j.gy[k] - py) * (j.gy[k] - py);
      if (d2 < mg) mg = d2, g = k;
    }
    if (g >= 0) {
      const float d = std::sqrt(mg) + 1e-3f;
      fx += 0.45f * (j.gx[g] - px) / d;
      fy += 0.45f * (j.gy[g] - py) / d;
    }

    // 5) Objetos utiles visibles (cofres, imanes, capsulas, relojes, bombas)
    if (pf.busca_objetos) {
      for (int k = 0; k < j.nObjetos; ++k) {
        const Objeto& o = j.objetos[k];
        if (!Visible(px, py, o.x, o.y)) continue;
        const bool quiere = o.tipo == TipoObjeto::Cofre ||
                            (o.tipo == TipoObjeto::Capsula && j.jugador.vida < j.jugador.vida_max * 0.65f) ||
                            o.tipo == TipoObjeto::Iman || o.tipo == TipoObjeto::Reloj ||
                            (o.tipo == TipoObjeto::Bomba && j.NumEnemigos() > 55) ||
                            o.tipo == TipoObjeto::Zeni;
        if (!quiere) continue;
        const float vx = o.x - px, vy = o.y - py, d2 = vx * vx + vy * vy;
        if (d2 < 144.0f) {
          const float w = o.tipo == TipoObjeto::Cofre ? 0.65f : 0.35f;
          const float d = std::sqrt(d2) + 1e-3f;
          fx += w * vx / d;
          fy += w * vy / d;
        }
      }
    }

    // 6) Stutter-step / giro de apunte manual con mando cuando autoapuntar=false
    const float min_d = std::sqrt(min_enem_d2);
    if (!pf.autoapuntar && min_d > pf.umbral_giro_disp && TieneDireccionalPronto(j)) {
      float tx = 0.0f, ty = 0.0f;
      bool hay_obj = false;
      if (jefe_activo && Visible(px, py, j.jefe.x, j.jefe.y)) {
        tx = j.jefe.x - px, ty = j.jefe.y - py, hay_obj = true;
      } else if (peso_c > 0.0f) {
        tx = centro_x / peso_c - px, ty = centro_y / peso_c - py, hay_obj = true;
      } else if (min_enem_d2 < 1e8f) {
        tx = cerca_ex - px, ty = cerca_ey - py, hay_obj = true;
      }
      const float tl = std::hypot(tx, ty);
      if (hay_obj && tl > 0.2f) {
        dir_disp_x = tx / tl;
        dir_disp_y = ty / tl;
        cuadros_giro_disp = 5;  // breve giro de ~83 ms hacia la horda/jefe
      }
    }

    const float fl = std::hypot(fx, fy);
    if (fl > 1e-3f) {
      const float ang = std::atan2(fy, fx) + AzarSim() * pf.ruido_ang;
      const float mag = 0.90f + 0.10f * Azar01();
      deseo_x = std::cos(ang) * mag;
      deseo_y = std::sin(ang) * mag;
    } else {
      // Sin estimulos cercanos: patrulla suave
      const float ang = j.tiempo * 0.45f * ciclo_orbita;
      deseo_x = std::cos(ang) * 0.85f;
      deseo_y = std::sin(ang) * 0.85f;
    }
  }

  Entrada Mira(const Juego& j) {
    if (int(j.pasos) - paso_ultimo_plan >= pf.reaccion_cuadros) {
      Planificar(j);
      paso_ultimo_plan = int(j.pasos);
    }
    const bool giro = cuadros_giro_disp > 0;
    const float tx = giro ? dir_disp_x : deseo_x, ty = giro ? dir_disp_y : deseo_y;
    if (giro) --cuadros_giro_disp;
    if (pf.dpad) return ProcesarMandoReal(tx, ty, true);  // asigna botones, no mueve un stick virtual
    // Dinamica fisica del pulgar moviendo el stick hacia deseo (pasa realmente por deadzone al invertir 180 deg)
    const float max_paso = pf.vel_stick * (1.0f / 60.0f);
    const float dx = tx - stick_x, dy = ty - stick_y;
    const float dist = std::hypot(dx, dy);
    if (dist <= max_paso || dist < 1e-6f) {
      stick_x = tx;
      stick_y = ty;
    } else {
      stick_x += dx / dist * max_paso;
      stick_y += dy / dist * max_paso;
    }
    return ProcesarMandoReal(stick_x, stick_y, false);
  }

  void Elige(Juego& j, const Bot2& evaluador) {
    if (j.CofreAbierto()) return j.CerrarCofre();
    if (j.nOpciones <= 1) return j.Elegir(0);
    // Jugador humano: con probabilidad habilidad_cartas elige la mejor segun criterio, si no elige una carta suboptima
    int mejor = 0;
    float pm = -1.0f;
    for (int i = 0; i < j.nOpciones; ++i) {
      float p = evaluador.Puntos(j, j.opciones[i]);
      // Un jugador sin autoapuntar valora mas las tecnicas omnidireccionales (orbita, aura, onda, estallido, explosion, barrera)
      if (!pf.autoapuntar && j.opciones[i].tipo == TipoOpcion::Tecnica) {
        const Arquetipo arq = j.datos->tecnicas[size_t(j.opciones[i].def)].arq;
        if (!EsDireccional(arq)) p += 10.0f * pf.habilidad_cartas;
      }
      if (p > pm) pm = p, mejor = i;
    }
    if (pf.resortea && pm < 38.0f && j.config.meta_v2 && j.PuedeResortear() &&
        j.resorteos < 3 && j.zeni >= j.CosteResortear() + 20 && j.Resortear()) {
      return Elige(j, evaluador);
    }
    if (Azar01() <= pf.habilidad_cartas) {
      j.Elegir(mejor);
    } else {
      j.Elegir(int(Azar() % uint32_t(j.nOpciones)));
    }
  }
};

struct ResMando {
  Estado estado = Estado::Derrota;
  int pj = 0, esc = 0, dif = 0;
  uint64_t semilla = 0;
  float minuto = 0.0f, primera_subida = -1.0f, nivel_min10 = -1.0f;
  int nivel = 0, derrotados = 0, evoluciones = 0, golpes_recibidos = 0, zeni = 0;
  bool llego_jefe = false;
  std::vector<int> tecs;
  std::vector<double> dano;
};

static ResMando CorrerPartida(const Datos& d, int pj, int esc, int dif, uint64_t semilla,
                              const ParamsPerfil& pf, int nivel_mejoras, float zoom, bool blanco_propio = false) {
  auto j = std::make_unique<Juego>();
  Config c;
  c.escenario = esc;
  c.dificultad = dif;
  // Configuracion REAL de serie del juego (modo_mundo.cpp + modo_pantallas.cpp)
  c.meta_v2 = true;
  c.jugo = true;
  c.formas = true;
  c.ia = true;
  c.jefes_v2 = true;
  c.jefes_dos_fases = true;
  c.autoapuntar = pf.autoapuntar;
#ifdef NSUR_CANDIDATO
  c.blanco_propio = blanco_propio;  // solo al compilar contra la copia con el parche experimental
#endif
  if (nivel_mejoras > 0) {
    for (size_t m = 0; m < d.mejoras.size() && m < size_t(kMejorasPerm); ++m) {
      c.mejoras[m] = std::min(nivel_mejoras, kNivelMax);
    }
  }
  j->Iniciar(d, pj, semilla, 0.0f, c);

  Bot2 bot_c10{semilla * 7 + 1 + uint64_t(pf.c10_perfil) * 99991, pf.c10_perfil};
  Bot2 eval_exp{semilla * 11 + 3, kExperto};
  PilotoMando pm{semilla * 17 + 5, pf, zoom};

  ResMando r;
  r.pj = pj;
  r.esc = esc;
  r.dif = dif;
  r.semilla = semilla;

  for (int p = 0; p < 200000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
    if (j->estado == Estado::Eligiendo) {
      if (pf.es_bot_c10) bot_c10.Elige(*j);
      else pm.Elige(*j, eval_exp);
      continue;   // el ultimo tick de simulacion no se cuenta otra vez al elegir carta
    } else {
      j->Paso(pf.es_bot_c10 ? bot_c10.Mira(*j) : pm.Mira(*j));
    }
    for (int e = 0; e < j->nEventos; ++e) {
      const auto tipo = j->eventos[e].tipo;
      if (tipo == TipoEvento::Subida && r.primera_subida < 0) r.primera_subida = j->tiempo;
      if (tipo == TipoEvento::Evolucion) ++r.evoluciones;
      if (tipo == TipoEvento::DanoJugador) ++r.golpes_recibidos;
      if (tipo == TipoEvento::JefeAparece) r.llego_jefe = true;
    }
    if (r.nivel_min10 < 0 && j->tiempo >= j->duracion * 10.0f / 15.0f) r.nivel_min10 = float(j->jugador.nivel);
  }

  r.estado = j->estado;
  r.minuto = j->Minuto();
  r.nivel = j->jugador.nivel;
  r.derrotados = j->derrotados;
  r.zeni = j->zeni;
  if (j->jefe.estado != EstadoJefe::Ausente) r.llego_jefe = true;
  for (int t = 0; t < j->nTecnicas; ++t) r.tecs.push_back(j->tecnicas[t].def);
  r.dano.assign(d.tecnicas.size(), 0.0);
  for (size_t t = 0; t < d.tecnicas.size(); ++t) r.dano[t] = j->dano_tecnica[t];
  return r;
}

struct Resumen {
  int n = 0, vict = 0, jefe = 0, evol = 0;
  int m_temprana = 0, m_mini1 = 0, m_media = 0, m_jefe = 0, m_furia = 0;
  double min_sum = 0, niv_sum = 0, s1_sum = 0, n10_sum = 0, derr_sum = 0, golpes_sum = 0, zeni_sum = 0;
  int ns1 = 0, nn10 = 0;
  std::vector<float> mins;

  void Sumar(const ResMando& r) {
    ++n;
    if (r.estado == Estado::Victoria) ++vict;
    else {
      if (r.minuto < 5.0f) ++m_temprana;
      else if (r.minuto < 8.0f) ++m_mini1;
      else if (r.minuto < 12.0f) ++m_media;
      else if (r.minuto < 15.0f) ++m_jefe;
      else ++m_furia;
    }
    if (r.llego_jefe) ++jefe;
    if (r.evoluciones > 0) ++evol;
    min_sum += r.minuto;
    niv_sum += r.nivel;
    derr_sum += r.derrotados;
    golpes_sum += r.golpes_recibidos;
    zeni_sum += r.zeni;
    if (r.primera_subida >= 0) s1_sum += r.primera_subida, ++ns1;
    if (r.nivel_min10 >= 0) n10_sum += r.nivel_min10, ++nn10;
    mins.push_back(r.minuto);
  }

  float MedianaMin() const {
    if (mins.empty()) return 0.0f;
    auto v = mins;
    std::sort(v.begin(), v.end());
    return (v[v.size() / 2] + v[(v.size() - 1) / 2]) * 0.5f;
  }
};

static int SelfTest() {
  // 1) Verifica deadzone exacta de Modo::Mando() (modo.cpp:173-177)
  const Entrada e0 = ProcesarMandoReal(0.20f, 0.0f, false);
  const Entrada e1 = ProcesarMandoReal(0.249f, 0.0f, false);
  const Entrada e2 = ProcesarMandoReal(0.625f, 0.0f, false);  // (0.625-0.25)/0.75 = 0.5
  const Entrada e3 = ProcesarMandoReal(1.0f, 0.0f, false);
  const Entrada ed = ProcesarMandoReal(0.9f, 0.9f, true);
  if (e0.x != 0 || e0.y != 0 || e1.x != 0 || e1.y != 0) {
    std::fprintf(stderr, "SELFTEST FAIL: deadzone 0.25 no filtra entrada baja\n");
    return 1;
  }
  if (std::abs(e2.x - 0.5f) > 0.01f || std::abs(e3.x - 1.0f) > 0.01f) {
    std::fprintf(stderr, "SELFTEST FAIL: reescalado analogico incorrecto (%.4f, %.4f)\n", e2.x, e3.x);
    return 1;
  }
  if (std::abs(ed.x - 0.7071f) > 0.01f || std::abs(ed.y - 0.7071f) > 0.01f) {
    std::fprintf(stderr, "SELFTEST FAIL: normalizacion D-pad diagonal incorrecta\n");
    return 1;
  }
  // 2) Verifica inercia fisica al invertir 180 grados: debe cruzar la deadzone en el giro
  PilotoMando pm{12345, kPerfiles[5], 1.8f};
  pm.stick_x = 1.0f;
  pm.stick_y = 0.0f;
  pm.deseo_x = -1.0f;
  pm.deseo_y = 0.0f;
  bool paso_por_deadzone = false;
  for (int i = 0; i < 30; ++i) {
    const float max_p = pm.pf.vel_stick / 60.0f;
    const float dx = pm.deseo_x - pm.stick_x;
    pm.stick_x += std::abs(dx) <= max_p ? dx : (dx > 0 ? max_p : -max_p);
    const Entrada e = ProcesarMandoReal(pm.stick_x, pm.stick_y, false);
    if (e.x == 0.0f && e.y == 0.0f) paso_por_deadzone = true;
  }
  if (!paso_por_deadzone) {
    std::fprintf(stderr, "SELFTEST FAIL: inversion de stick no cruzo la zona muerta\n");
    return 1;
  }
  // 3) Verifica determinismo de CorrerPartida
  Datos d;
  std::string err;
  if (!CargarDatos("res/survivors/survivors_v2.toml", &d, &err)) {
    std::fprintf(stderr, "SELFTEST FAIL: no carga survivors_v2.toml: %s\n", err.c_str());
    return 1;
  }
  auto j = std::make_unique<Juego>();
  if (!j->Iniciar(d, 0, 5000)) return 1;
  PilotoMando latencia{5000, kPerfiles[5]};
  for (int i = 0; i <= latencia.pf.reaccion_cuadros; ++i) {
    j->pasos = i;
    latencia.Mira(*j);
    const int esperado = i == latencia.pf.reaccion_cuadros ? i : 0;
    if (latencia.paso_ultimo_plan != esperado) {
      std::fprintf(stderr, "SELFTEST FAIL: latencia de planificacion no respetada\n");
      return 1;
    }
  }
  const ResMando a = CorrerPartida(d, 0, 0, 0, 5000, kPerfiles[5], 0, 1.8f);
  const ResMando b = CorrerPartida(d, 0, 0, 0, 5000, kPerfiles[5], 0, 1.8f);
  if (a.estado != b.estado || a.derrotados != b.derrotados || a.nivel != b.nivel || std::abs(a.minuto - b.minuto) > 1e-5f) {
    std::fprintf(stderr, "SELFTEST FAIL: no determinista\n");
    return 1;
  }
  std::printf("SELFTEST OK: deadzone, reescalado, diagonal D-pad, giro 180 deg, latencia y repetibilidad (min=%.2f niv=%d derr=%d)\n",
              a.minuto, a.nivel, a.derrotados);
  return 0;
}

static std::string JsonEsc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o.push_back('\\');
    o.push_back(c);
  }
  return o;
}

}  // namespace nsur

int main(int argc, char** argv) {
  using namespace nsur;
  std::string ruta_toml = "res/survivors/survivors_v2.toml";
  std::string filtro_perfil = "todos";
  int filtro_dif = -1;  // -1 = todas (0,1,2)
  int reps = 4;         // repeticiones por mapa (4 * 6 mapas = 24 partidas en v2; 6 * 4 = 24 en clasico)
  int nivel_mejoras = 0;
  float vida_ovr = -1.0f, dano_ovr = -1.0f;
  int ovr_dif = 0;
  float zoom = 1.8f;
  uint64_t semilla_base = 5000;
  bool solo_3pj = false;
  bool blanco_propio = false;
  std::string ruta_csv, ruta_json;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto sig = [&](const char* nombre) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Falta valor para %s\n", nombre);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--selftest") return SelfTest();
    else if (a == "--help" || a == "-h") {
      std::printf("Uso: sim_mando --toml <ruta> [--perfil <nombre|todos>] [--dif 0|1|2|todos] [--reps N]\n"
                  "               [--mejoras 0..5] [--vida F] [--dano F] [--ovr-dif 0|1|2] [--zoom F]\n"
                  "               [--semilla S] [--solo-3pj] [--csv <ruta>] [--json <ruta>] [--selftest]\n"
                  "Perfiles:");
      for (const auto& p : kPerfiles) std::printf(" %s", p.nombre);
      std::printf("\n");
      return 0;
    } else if (a == "--toml") ruta_toml = sig("--toml");
    else if (a == "--perfil") filtro_perfil = sig("--perfil");
    else if (a == "--dif") {
      const std::string v = sig("--dif");
      filtro_dif = (v == "todos") ? -1 : std::atoi(v.c_str());
    } else if (a == "--reps") reps = std::max(1, std::atoi(sig("--reps").c_str()));
    else if (a == "--mejoras") nivel_mejoras = std::clamp(std::atoi(sig("--mejoras").c_str()), 0, 5);
    else if (a == "--vida") vida_ovr = float(std::atof(sig("--vida").c_str()));
    else if (a == "--dano") dano_ovr = float(std::atof(sig("--dano").c_str()));
    else if (a == "--ovr-dif") ovr_dif = std::clamp(std::atoi(sig("--ovr-dif").c_str()), 0, 2);
    else if (a == "--zoom") zoom = std::clamp(float(std::atof(sig("--zoom").c_str())), 1.0f, 2.2f);
    else if (a == "--semilla") semilla_base = std::strtoull(sig("--semilla").c_str(), nullptr, 10);
    else if (a == "--solo-3pj") solo_3pj = true;
#ifdef NSUR_CANDIDATO
    else if (a == "--blanco-propio") blanco_propio = true;
#endif
    else if (a == "--csv") ruta_csv = sig("--csv");
    else if (a == "--json") ruta_json = sig("--json");
    else {
      std::fprintf(stderr, "Argumento desconocido: %s\n", a.c_str());
      return 2;
    }
  }

  Datos d;
  std::string err;
  if (!CargarDatos(ruta_toml, &d, &err)) {
    std::fprintf(stderr, "Error cargando %s:\n%s\n", ruta_toml.c_str(), err.c_str());
    return 2;
  }
  if (vida_ovr > 0.0f) d.dificultad[ovr_dif].vida = vida_ovr;
  if (dano_ovr > 0.0f) d.dificultad[ovr_dif].dano = dano_ovr;

  std::vector<int> perfiles_idx;
  for (int i = 0; i < int(sizeof(kPerfiles) / sizeof(kPerfiles[0])); ++i) {
    if (filtro_perfil == "todos" || filtro_perfil == kPerfiles[i].nombre) perfiles_idx.push_back(i);
  }
  if (perfiles_idx.empty()) {
    std::fprintf(stderr, "Perfil desconocido: %s\n", filtro_perfil.c_str());
    return 2;
  }

  std::vector<int> difs;
  if (filtro_dif >= 0) difs.push_back(std::clamp(filtro_dif, 0, 2));
  else difs = {0, 1, 2};

  const int ne = int(d.escenarios.size());
  const int np = solo_3pj ? std::min(3, int(d.personajes.size())) : int(d.personajes.size());

  struct Tarea { int p_idx, dif, esc, rep, pj; uint64_t seed; };
  std::vector<Tarea> tareas;
  for (int pi : perfiles_idx) {
    for (int dif : difs) {
      for (int e = 0; e < ne; ++e) {
        for (int m = 0; m < reps; ++m) {
          // Misma receta emparejada de SUR1/SUR3: seed = base + 100*mapa + 13*rep, pj = (mapa + rep) % np
          const uint64_t seed = semilla_base + uint64_t(100 * e + 13 * m);
          const int pj = (e + m) % np;
          tareas.push_back({pi, dif, e, m, pj, seed});
        }
      }
    }
  }

  std::vector<ResMando> resultados(tareas.size());
  std::atomic<size_t> sig{0};
  const unsigned nh = std::max(2u, std::min(12u, std::thread::hardware_concurrency()));
  std::vector<std::future<void>> fs;
  for (unsigned h = 0; h < nh; ++h) {
    fs.push_back(std::async(std::launch::async, [&] {
      for (size_t k; (k = sig.fetch_add(1)) < tareas.size();) {
        const Tarea& t = tareas[k];
        resultados[k] = CorrerPartida(d, t.pj, t.esc, t.dif, t.seed, kPerfiles[t.p_idx], nivel_mejoras, zoom, blanco_propio);
      }
    }));
  }
  for (auto& f : fs) f.get();
  for (const auto& r : resultados) {
    if (r.estado != Estado::Victoria && r.estado != Estado::Derrota) {
      std::fprintf(stderr, "Simulacion incompleta (mapa=%d, luchador=%d, semilla=%llu)\n", r.esc, r.pj,
                   (unsigned long long)r.semilla);
      return 1;
    }
  }

  // Agregacion por (perfil, dif) y desglose por personaje / escenario
  struct Clave {
    int pi, dif;
    bool operator<(const Clave& o) const { return pi != o.pi ? pi < o.pi : dif < o.dif; }
  };
  std::map<Clave, Resumen> por_celda;
  std::map<std::pair<int, int>, Resumen> por_pj;   // (pi, pj) en dif 0
  std::map<std::pair<int, int>, Resumen> por_esc;  // (pi, esc) en dif 0

  for (size_t k = 0; k < tareas.size(); ++k) {
    const Tarea& t = tareas[k];
    por_celda[{t.p_idx, t.dif}].Sumar(resultados[k]);
    if (t.dif == difs[0]) {
      por_pj[{t.p_idx, t.pj}].Sumar(resultados[k]);
      por_esc[{t.p_idx, t.esc}].Sumar(resultados[k]);
    }
  }

  const char* nom_dif[3] = {"normal(mas_facil)", "dificil", "extremo"};
  std::printf("# NSUR Perfiles sinteticos | toml=%s | reps/mapa=%d (%d/celda) | mejoras=%d | zoom=%.1f | blanco_propio=%d\n",
              ruta_toml.c_str(), reps, reps * ne, nivel_mejoras, zoom, int(blanco_propio));
  std::printf("# Dificultades: N(vida=%.2f,dano=%.2f) D(vida=%.2f,dano=%.2f) E(vida=%.2f,dano=%.2f)\n",
              d.dificultad[0].vida, d.dificultad[0].dano, d.dificultad[1].vida, d.dificultad[1].dano,
              d.dificultad[2].vida, d.dificultad[2].dano);
  std::printf("%-22s %-17s %6s %6s %6s %6s %6s %6s %6s %7s | muertes(<5/5-8/8-12/12-15/15+)\n",
              "perfil", "dificultad", "vict", "vict%", "min", "med", "niv", "jefe%", "evol%", "golpes");
  for (const auto& [cl, r] : por_celda) {
    const double k = std::max(1, r.n);
    std::printf("%-22s %-17s %2d/%-3d %5.1f%% %6.2f %6.2f %6.1f %5.0f%% %5.0f%% %7.1f | %d/%d/%d/%d/%d\n",
                kPerfiles[cl.pi].nombre, nom_dif[cl.dif], r.vict, r.n, 100.0 * r.vict / k, r.min_sum / k,
                r.MedianaMin(), r.niv_sum / k, 100.0 * r.jefe / k, 100.0 * r.evol / k, r.golpes_sum / k,
                r.m_temprana, r.m_mini1, r.m_media, r.m_jefe, r.m_furia);
  }

  if (!ruta_csv.empty()) {
    std::ofstream out(ruta_csv, std::ios::binary | std::ios::trunc);
    out << "perfil,autoapuntar,dpad,dif,escenario,luchador,arma_dir,semilla,estado,minuto,nivel,derrotados,evoluciones,golpes,llego_jefe,zeni\n";
    for (size_t k = 0; k < tareas.size(); ++k) {
      const Tarea& t = tareas[k];
      const ResMando& r = resultados[k];
      const auto& pf = kPerfiles[t.p_idx];
      const auto& pj = d.personajes[size_t(t.pj)];
      const bool dir = EsDireccional(d.tecnicas[size_t(pj.tecnica)].arq);
      out << pf.nombre << "," << int(pf.autoapuntar) << "," << int(pf.dpad) << "," << t.dif << ","
          << d.escenarios[size_t(t.esc)].id << "," << pj.id << "," << int(dir) << "," << t.seed << ","
          << (r.estado == Estado::Victoria ? "VICTORIA" : "DERROTA") << ","
          << std::fixed << std::setprecision(2) << r.minuto << "," << r.nivel << "," << r.derrotados << ","
          << r.evoluciones << "," << r.golpes_recibidos << "," << int(r.llego_jefe) << "," << r.zeni << "\n";
    }
    out.close();
    if (!out) { std::fprintf(stderr, "No se pudo escribir CSV\n"); return 2; }
  }

  if (!ruta_json.empty()) {
    std::ofstream out(ruta_json, std::ios::binary | std::ios::trunc);
    out << "{\n";
    out << "  \"toml\": \"" << JsonEsc(ruta_toml) << "\",\n";
    out << "  \"reps_por_mapa\": " << reps << ",\n";
    out << "  \"escenarios\": " << ne << ",\n";
    out << "  \"personajes\": " << np << ",\n";
    out << "  \"mejoras\": " << nivel_mejoras << ",\n";
    out << "  \"semilla_base\": " << semilla_base << ",\n";
    out << "  \"blanco_propio\": " << (blanco_propio ? "true" : "false") << ",\n";
    out << "  \"zoom\": " << std::fixed << std::setprecision(2) << zoom << ",\n";
    out << "  \"dificultades\": [\n";
    for (int i = 0; i < 3; ++i) {
      out << "    {\"idx\": " << i << ", \"vida\": " << d.dificultad[i].vida
          << ", \"dano\": " << d.dificultad[i].dano << ", \"zeni\": " << d.dificultad[i].zeni << "}"
          << (i < 2 ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"celdas\": [\n";
    size_t idx = 0;
    for (const auto& [cl, r] : por_celda) {
      const double k = std::max(1, r.n);
      out << "    {\"perfil\": \"" << kPerfiles[cl.pi].nombre << "\", \"autoapuntar\": " << (kPerfiles[cl.pi].autoapuntar ? "true" : "false")
          << ", \"dpad\": " << (kPerfiles[cl.pi].dpad ? "true" : "false")
          << ", \"dif\": " << cl.dif << ", \"n\": " << r.n << ", \"vict\": " << r.vict
          << ", \"vict_pct\": " << std::setprecision(2) << (100.0 * r.vict / k)
          << ", \"min_medio\": " << (r.min_sum / k) << ", \"min_mediana\": " << r.MedianaMin()
          << ", \"nivel_medio\": " << (r.niv_sum / k)
          << ", \"jefe_pct\": " << (100.0 * r.jefe / k)
          << ", \"evol_pct\": " << (100.0 * r.evol / k)
          << ", \"golpes_medio\": " << (r.golpes_sum / k)
          << ", \"derrotados_medio\": " << (r.derr_sum / k)
          << ", \"zeni_medio\": " << (r.zeni_sum / k)
          << ", \"muertes\": {\"m0_5\": " << r.m_temprana << ", \"m5_8\": " << r.m_mini1
          << ", \"m8_12\": " << r.m_media << ", \"m12_15\": " << r.m_jefe << ", \"m15p\": " << r.m_furia << "}}"
          << (++idx < por_celda.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"dif_desglose\": " << difs[0] << ",\n";
    out << "  \"por_luchador\": [\n";
    idx = 0;
    for (const auto& [cl, r] : por_pj) {
      const double k = std::max(1, r.n);
      const auto& pj = d.personajes[size_t(cl.second)];
      const bool dir = EsDireccional(d.tecnicas[size_t(pj.tecnica)].arq);
      out << "    {\"perfil\": \"" << kPerfiles[cl.first].nombre << "\", \"luchador\": \"" << pj.id
          << "\", \"tecnica\": \"" << d.tecnicas[size_t(pj.tecnica)].id << "\", \"direccional\": " << (dir ? "true" : "false")
          << ", \"n\": " << r.n << ", \"vict\": " << r.vict
          << ", \"vict_pct\": " << (100.0 * r.vict / k) << ", \"min_medio\": " << (r.min_sum / k) << "}"
          << (++idx < por_pj.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"por_escenario\": [\n";
    idx = 0;
    for (const auto& [cl, r] : por_esc) {
      const double k = std::max(1, r.n);
      out << "    {\"perfil\": \"" << kPerfiles[cl.first].nombre << "\", \"escenario\": \"" << d.escenarios[size_t(cl.second)].id
          << "\", \"n\": " << r.n << ", \"vict\": " << r.vict
          << ", \"vict_pct\": " << (100.0 * r.vict / k) << ", \"min_medio\": " << (r.min_sum / k) << "}"
          << (++idx < por_esc.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    out.close();
    if (!out) { std::fprintf(stderr, "No se pudo escribir JSON\n"); return 2; }
  }

  return 0;
}
