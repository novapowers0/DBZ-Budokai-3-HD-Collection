// SPDX-License-Identifier: MIT
// Interprete propio del EE R5900 (subconjunto entero + COP1 + VU0 macro), port en C++ de tools/iw_mips/ee.py.
// Ejecuta funciones del ELF de IW que el jugador tiene en su ISO (nada del juego se copia al repo ni a la release).
// Primer uso: IA original de Bubbles (EU s_len_3_2f 0x31A940) tras la cvar dbz3_iw_logica_original (apagada).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbz3::iwmips {

class Ee {
 public:
  static constexpr uint32_t kRam = 0x2000000, kPila = 0x2000000, kSpr = 0x2100000, kTotal = kSpr + 0x4000;
  static constexpr uint32_t kRetorno = 0xFFFFFF00;
  // Imagen del invitado (32 MB + pila propia + scratchpad) con los PT_LOAD del ELF; codigo en [texto0, texto1).
  bool Cargar(const std::vector<uint8_t>& elf, uint32_t texto0, uint32_t texto1);
  bool Preparar(uint32_t texto0, uint32_t texto1);  // imagen vacia (autocomprobacion)
  uint64_t HashTexto() const;                       // FNV-1a del texto cargado (verificacion del ELF)
  static bool Autocomprobacion();                   // LFSR ensamblado a mano + casos FPU, sin datos del juego
  // Llama a `dir` con a0..a3; false si se sale del subconjunto, de la memoria o del presupuesto (ver error()).
  bool Llamar(uint32_t dir, std::initializer_list<uint32_t> args, uint64_t limite = 200000);
  uint32_t R32(uint32_t a);
  uint16_t R16(uint32_t a) { return uint16_t(R32(a & ~3u) >> ((a & 2) * 8)); }
  uint8_t R8(uint32_t a) { return uint8_t(R32(a & ~3u) >> ((a & 3) * 8)); }
  void W8(uint32_t a, uint8_t v) {
    const uint32_t s = (a & 3) * 8, w = R32(a & ~3u);
    W32(a & ~3u, (w & ~(0xFFu << s)) | (uint32_t(v) << s));
  }
  float Rf(uint32_t a);
  void W32(uint32_t a, uint32_t v);
  void W16(uint32_t a, uint16_t v);
  void Wf(uint32_t a, float v);
  void Cero(uint32_t a, uint32_t n);
  uint64_t r[32]{}, up[32]{};
  uint64_t n = 0;  // instrucciones ejecutadas (total)
  uint32_t gp = 0;
  std::unordered_map<uint32_t, std::function<void(Ee&)>> puentes;  // direccion -> puente (lee a0-a3, escribe v0)
  std::unordered_map<uint32_t, std::function<void(Ee&)>> observadores;  // se llaman al entrar y la funcion se ejecuta
  uint32_t F(int i) const { return f_[i]; }  // bits de la FPR i (f12 = primer argumento float)
  const std::string& error() const { return error_; }
  uint64_t io = 0;  // accesos fuera de RAM/pila/scratchpad (se aborta)
  // add.s/sub.s del R5900 como PCSX2 (FPU_ADD_SUB): el operando menor pierde los bits que caen por debajo del mayor
  // (deja uno de guarda). Medido 6/11 en la esquiva de Freezer (149/149 con, 99/149 sin). Por llamada: Bubbles sin.
  bool suma_ps2 = false;

 private:
  struct Ins { uint16_t op = 0; uint8_t rs, rt, rd, sa; int32_t simm; uint32_t w, t; };
  int64_t Fis(uint32_t a);  // direccion fisica en mem_ o -1
  template <class T> T Lee(uint32_t a);
  template <class T> void Escribe(uint32_t a, T v);
  const Ins* Decodificar(uint32_t pc);
  bool Fallo(const char* que, uint32_t pc);
  std::vector<uint8_t> mem_;
  std::vector<Ins> code_;
  uint32_t t0_ = 0, t1_ = 0;
  std::string error_;
  uint32_t f_[32]{}, vf_[32][4]{}, acc_ = 0, vacc_[4]{}, q_ = 0x3F800000;
  uint16_t vi_[16]{};
  bool c_ = false;
  uint32_t fcr_ = 0;  // dia1E6: banderas de FCR31 (solo con la regla de hardware fpu_hw_flags; iw_mips.cpp)
  uint64_t lo_ = 0, hi_ = 0, lo1_ = 0, hi1_ = 0;
};

namespace bubbles {
// Estado del port (Maquina/Entrada) que la IA original lee; se escribe en el espejo del invitado en cada llamada.
struct EntradaIA {
  float bub[3]{}, jug[3]{}, frente_x = 0, frente_z = 1, stick_x = 0, stick_y = 0;
  int estado = 0, anterior = 0, estado_jugador = 1, dificultad = 2;
  bool tarea = false, bloqueo248 = false, objetivo180 = false, icono = false;
  uint64_t tick = 0;
  // Maquina de estados (UPDATE 0x31B260): actor+0x40, clip, reloj y latch; largos = duracion por codigo de clip (0xE3).
  float p[3]{}, cuadro = 0, reloj = 0;
  int cc4 = 0, duracion = 0;
  unsigned codigo = 0xD2, flags18e8 = 0;
  bool fin = false, f218 = false, latch224 = false;
  const float* largos = nullptr;
  // Movimiento: la tarea que mueve MgObj_UpdateTrajectory y la normal del barrido (+0x238) para crear la tarea 5.
  int tm_modo = 0, tm_fase = 0;
  float tm_duracion = 0, tm_tiempo = 0, tm_vel[3]{}, tm_centro[3]{}, normal238[3]{};
  // Tick (tabla 0x4995E0). Decision 6/11 (INFORME_dia1E §7): el invitado NO es persistente; lo que el original lee de
  // cuadros anteriores lo guarda Maquina (campos con nombre + `guarda`, palabras del invitado copiadas tal cual).
  static constexpr int kGuarda = 20;  // work+0xB4, work+0x40/+0x60 (base del stick), work+0x1CC, MGW+0x238..+0x25C
  uint32_t guarda[kGuarda]{};
  bool guarda_ok = false;
  float p_prev[3]{}, anterior90[3]{}, suelo18d0 = 0, altura_b84 = 0, local50[3]{};
  bool aire = false;
  // Puentes al port (wcf::Backend). Devuelven si hay choque y rellenan punto y contacto. Sin ellos la tick da false.
  struct Contacto { float normal[3]{}, d = 0; uint16_t material = 0, tipo = 0; };
  void* host = nullptr;
  bool (*barrido)(void*, const float* desde, const float* hasta, float radio, unsigned mascara, float* punto, Contacto&) = nullptr;
  bool (*rayo)(void*, const float* desde, const float* hasta, unsigned mascara, float* punto, Contacto&) = nullptr;
};
struct TareaIA { int modo = 0, fase = 0; float duracion = 0, vel[3]{}, centro[3]{}; };
// Lo que el original pide al port, en orden: R = consumir raiz (Char_UpdateRootAndBones), E = entrar en el estado n,
// M = clip nuevo n (BonesMovement), B = platano de tipo n con desvio.
struct EventoIA { char tipo = 0; int n = 0, cc4 = 0; float desvio = 0; };
struct SalidaIA {
  bool f200 = false, f210 = false, f214 = false, f218 = false, icono = false;
  int voz = -1, n_tareas = 0;
  TareaIA tareas[4];
  float stick_x = 0;
  // solo UPDATE
  int estado = 0, anterior = 0, cc4 = 0, n_eventos = 0;
  unsigned codigo = 0;
  float cuadro = 0, reloj = 0, frente_x = 0, frente_z = 0;
  bool fin = false, latch224 = false, frente_cambia = false;
  EventoIA eventos[16];
  // solo trayectoria: actor+0x40 y la tarea tras moverla (tm_fin = la tarea acabo y work+0xB8 quedo a 0)
  float p[3]{}, tm_tiempo = 0, tm_vel[3]{}, tm_centro[3]{};
  int tm_fase = 0;
  bool tm_fin = false;
  // solo tick: work+0x90, suelo +0x18D0, factor +0x184C, tipos MGW+0x1F0/+0x1E8 del actor, guarda; tarea5 = el barrido
  // choco con tarea (READ_BattleMain + s_BBR_POST_EXEC(actor,1,5) ejecutados en el invitado).
  float a90[3]{}, suelo18d0 = 0, f184c = 0;
  uint32_t tipo1f0 = 0, tipo1e8 = 0, guarda[EntradaIA::kGuarda]{};
  bool tarea5 = false;
};
// Las pone iw_mips.cpp al cargar: los arneses offline que incluyen iw_bubbles_gp2.h sin enlazar el interprete
// ven nullptr y siguen con el port. Devuelven false (cvar apagada, ELF distinto, fallo) -> el llamador usa el port.
inline bool (*g_ia)(const EntradaIA&, SalidaIA&, uint32_t& semilla) = nullptr;      // s_len_3_2f 0x31A940
inline bool (*g_update)(const EntradaIA&, SalidaIA&, uint32_t& semilla) = nullptr;  // tabla de updates 0x4995A0
inline bool (*g_trayectoria)(const EntradaIA&, SalidaIA&, uint32_t& semilla) = nullptr;  // MgObj_UpdateTrajectory 0x31A570
inline bool (*g_tarea5)(const EntradaIA&, SalidaIA&, uint32_t& semilla) = nullptr;  // s_BBR_POST_EXEC(actor,1,5) 0x3172E0
inline bool (*g_tick)(const EntradaIA&, SalidaIA&, uint32_t& semilla) = nullptr;    // Char_PoseTickDispatch 0x31B240
}  // namespace bubbles

// Esquiva de Freezer (0x7D7, EU Char_PoseInputTransition 0x35DF50). Globales 0x5D9D60..0x5D9D88 en el orden del ELF:
// azimut, elevacion, larga, apunta>0,95, velocidad, direccion, anterior, tiempo, preparada, tirada. Una llamada por
// campo PAL (paso 1,2), como el original; el estado lo guarda el port (iw_mini_disparo.cpp), no el invitado.
namespace freezer {
struct EntradaEsq {
  uint32_t glob[10]{};
  int estado = 1, c74 = 0, krillin = 8, dif = 2, b4c = 70, b50 = 60;
  float b48 = 0.022f, f[3]{};  // f = direccion de la camara en ejes del original
  uint64_t t = 0;
};
struct SalidaEsq { uint32_t glob[10]{}; int estado = 0; uint32_t b60 = 0; };
inline bool (*g_activa)() = nullptr;  // cvar encendida y ELF EU (o USA, dia1E5) cargado
// dia1E5: ELF USA SLUS_218.42 cargado (NTSC): una llamada por cuadro (60 campos/s, paso 1,0) en vez de 5 de cada 6.
// Lo pone iw_mips.cpp al cargar el ELF (la primera g_activa()); el llamador lo mira despues de g_activa().
inline bool g_ntsc = false;
inline bool (*g_esquiva)(const EntradaEsq&, SalidaEsq&, uint32_t& semilla) = nullptr;
}  // namespace freezer

// MSN019 (0x7E1, Videl): reparto de cada fila como el original: Memory(32) % N = objeto oculto (si la fila lo lleva) y
// Results_RespawnItem 0x2EB000 por objeto (hueco con azar, sin solapar con los demas, tipo al azar de la mascara).
// El invitado no guarda nada: los objetos de la fila anterior (con los que se mira el solape) los guarda el port.
namespace msn019 {
struct Objeto { float x = 0, y = 0; uint32_t estado = 3, tipo = 0, hueco = 0; };  // UI +0x20/+0x24, datos +0x44/+0x48/+0x4C
struct EntradaFila {
  int n = 0, dif = 2;
  uint16_t mascara = 0xF000;
  float ancho = 0.75f;                 // fraccion de la pantalla de la barra (tabla 0x5D9164)
  bool excluir = false, primera = false;
  Objeto prev[10];
  uint64_t fila = 0;
};
struct SalidaFila { Objeto obj[10]; int oculto = -1; };
inline bool (*g_fila)(const EntradaFila&, SalidaFila&, uint32_t& semilla) = nullptr;
}  // namespace msn019

// dia1E4: esquiva de Piccolo (0x7D8, EU Viewer_UpdatePoseRotationInput 0x32DC80 con Viewer_PoseScheduler y
// Viewer_PickRandomDirection). Globales 0x5D9940..0x5D9964: azimut, elevacion, golpe, modo, apunta>0,9, giro, rumbo,
// rumbo anterior, contador, armada. Una llamada por campo PAL; el estado lo guarda el port. Activa = freezer::g_activa.
namespace piccolo {
struct EntradaPc {
  uint32_t glob[10]{};
  int estado = 1, c74 = 0, dif = 2;
  float vida = 1000.f, pos[3]{}, ojo[3]{}, obj[3]{};  // ejes del original (= los del port en el 0x7D8)
  uint64_t t = 0;
};
struct SalidaPc { uint32_t glob[10]{}; int estado = 0; uint32_t b60 = 0; };
inline bool (*g_esquiva)(const EntradaPc&, SalidaPc&, uint32_t& semilla) = nullptr;
}  // namespace piccolo

// N2 (noche 6-7/11): las 5 marcas de la carrera de Cell (0x7DE, EU Char_InitTrailMarkerRandom 0x31D8F0, tabla 0x5D9500,
// 0x30 B por marca: +0 tipo = Memory % 10, +0x10/+0x18 x/z, +0x20 paso, +0x24 duracion = (Memory & 0x7F) + 32) x paso de
// campo). Dorado: tools/iw_mips/cell.py dorado (sonda cap_7de.py en PCSX2).
namespace cell {
struct Marca { int tipo = 0; float x = 0, y = 0, z = 0, paso = 0, dura = 0; };
inline bool (*g_marcas)(Marca (&out)[5], uint32_t& semilla, uint64_t t) = nullptr;
}  // namespace cell

// P1 (7/11): Piccolo persigue (0x7DD). Por campo se ejecutan del ELF, como Battle_UpdateAllCharacters con el actor 1
// del tipo 0xE: Minigame_ProcessPlayerInput 0x29A1D0, Char_DispatchPoseTick 0x363E70 (tabla 0x49B2C0: la CPU
// MinigameHideNSeekPiccolo_sCPU -> Piccolo_sCPUScript y los cambios de estado de la tabla 0x49B250),
// Char_DispatchPoseUpdate 0x363E50 (tabla 0x49B330: palanca, gravedad, suelo) y la tarea MgObj_UpdateTrajectory
// 0x31A570. El invitado guarda el estado de Piccolo entre llamadas (solo corre este minijuego). Puentes: rayo y
// barrido contra el #WCF del campo 906 (wcf::Backend, los de Bubbles). Dorado: tools/iw_mips/persecucion.py puentes;
// juego: persecucion.py juego --log.
namespace persecucion {
using Contacto = bubbles::EntradaIA::Contacto;
// Raiz animada de Piccolo (7/11, iw_7dd_raiz): de la copia del jugador. #BSK 925/0 (Piccolo) y 915/3 (comun) dan
// codigo -> [indice][almacen]; #AMM por almacen: 0 = 915/0, 1 = 915/1, 3 = 925/1, 4 = 925/2 (2 vacio).
struct RaizDatos {
  std::vector<uint8_t> bsk[2], amm[5];
};
struct Entrada {
  bool inicio = false;               // primera llamada de la partida: prepara el espejo y entra en el estado 13
  float pic0[3]{};                    // solo con inicio: sitio de salida de Piccolo (+0x40)
  float jug40[3]{}, jug640[3]{};      // raiz y ancla del jugador (coordenadas de IW = las del campo)
  int jcbc = 1, dif = 2;              // estado de pose de IW del jugador (iwestado::DeAnim) y dificultad
  unsigned tipo25a = 1;               // MGW+0x25A: tipo del suelo del ultimo contacto (1 suelo)
  bool w180 = false;                  // work[0]+0x180 (pillado)
  uint64_t t = 0;
  // Puentes al #WCF del campo (wcf::Backend): true si choca y rellenan punto y contacto (= los de Bubbles).
  void* host = nullptr;
  bool (*rayo)(void*, const float* desde, const float* hasta, unsigned mascara, float* punto, Contacto&) = nullptr;
  bool (*barrido)(void*, const float* desde, const float* hasta, float radio, unsigned mascara, float* punto, Contacto&) = nullptr;
  const RaizDatos* raiz = nullptr;    // sin el: el hueso vivo no se evalua (Piccolo se mueve sin la raiz de sus clips)
};
struct Salida {
  int codigo = -1, estado = 0, voz = -1;
  float p[3]{}, frente[3]{};          // Piccolo +0x40 tras la tarea y work+0x50
  bool tarea = false, vuela = false;  // work+0xB8 != 0, +0x2061
  uint32_t clip = 0;                  // +0xB60
};
inline bool (*g_tick)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvars dbz3_iw_7dd_original (de serie apagada: decision del usuario 7/11 21h) y dbz3_iw_logica_original
}  // namespace persecucion

// T2B (7/11): NB012 (0x7DA, un carril y 10 rondas) con su logica ORIGINAL. Por campo PAL se ejecutan del ELF
// Mg_UpdateMain 0x315010 y, de Mg_UpdateFrame, Mg_UpdateScrollingIcons 0x315540 y Mg_UpdateMissPoseSequence 0x315230:
// intro, barajado de filas y camaras, aparicion y avance de iconos, juicio, puntos, fallos, rondas, total y rango. El
// trabajo (0x114 B), la QTE y los objetos UI viven en el invitado entre campos (como el 7DD). Puentes: mando
// (QTE_Update), poses (MgPly_SetPoseState -> port, que devuelve el estado de IW), camaras (UiAnim_Start y su
// temporizador), sonido y READY/resultados (eventos para el port). Dorado: E:/DBZ3HD_trabajo/T2B/qte7da.py comparar.
namespace nb012 {
struct Entrada {
  bool inicio = false;          // primer campo de la partida: espejo nuevo (Mg_ResetState + Mg_InitParamsByLevel)
  int dif = 2, personaje = 7;
  int boton = -1;               // codigo de IW del boton pulsado este campo (bit del mando: 15 cuadrado...), -1 ninguno
  bool cam_fin = false;         // Camera_IsTimerElapsed: el clip de camara actual acabo
  int pose = 3, pose_t = 0;     // actor +0x14BC (estado de pose de IW) y +0x1478 (cuadros en el estado)
  bool pose_act = true;         // actor +0x1474
  uint16_t flanco = 0;          // botones de IW pulsados este campo (0x4ADED2; la intro mira 8 = START para saltarse)
  uint64_t t = 0;
};
// 'P' pose n, 'C' camara (clip n de UiAnim_Start), 'R' s_MSN_CV_ReadyStart(n), 'G' sonido de golpe, 'F' resultados
struct Evento { char tipo = 0; int n = 0; };
struct Salida {
  int fase = 0, sub = 0, paso = 0, ronda = -1, actual = 0, juicio = 0, botones = 0, rango = 0, n_ev = 0;
  float reloj = 0, nota = 0, total = 0, x[10]{}, ronda_pts[10]{}, cat[3]{};
  uint8_t vivo[10]{}, fallado[10]{}, acierto[10]{}, seq[10]{};
  Evento ev[16];
};
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvars dbz3_iw_7da_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace nb012

// T2B (7/11): NB014 (0x7DC, dos carriles) con su logica ORIGINAL. Por campo PAL: QteMg_Update 0x331EC0 (intro, cuenta,
// juego, resultado) y QteMg_UpdateGameplay 0x332CA0 (iconos de los dos carriles, carril activo, combo, juicio y vida).
// Trabajo (0x360 B), QTE, UI y las vidas (+0x1850) de los dos luchadores en el invitado. Puentes: mando, camaras,
// poses (Char_SetPoseState / _P2 -> port), sonido y efectos (mudos). Dorado: E:/DBZ3HD_trabajo/T2B/qte7dc.py.
namespace nb014 {
struct Entrada {
  bool inicio = false;
  int dif = 2;
  int boton = -1;               // codigo de IW del boton pulsado este campo, -1 ninguno
  uint16_t flanco = 0;          // botones de IW pulsados este campo (0x4ADED2: la intro mira START)
  uint16_t mantenido = 0;       // botones de IW apretados (0x4ADECC: el combo de los dos carriles los mira)
  bool cam_fin = false;         // Camera_IsTimerElapsed
  uint64_t t = 0;
};
struct Icono { int k = 0, boton = 0; float x = 0; bool vivo = false, fallado = false; };
// 'J' pose del jugador n, 'R' pose del rival n, 'C' camara (clip | 0x10000 si play 0), 'V' sonido de vida
struct Evento { char tipo = 0; int n = 0; };
struct Salida {
  int fase = 0, sub = 0, paso = 0, ronda = -1, activo = 0, rango = 0, n_ev = 0;
  float nota = 0, vida[2]{};
  int n_ic[2]{}, aciertos[2]{}, llegaron[2]{};   // +0x3C/+0x40 aciertos y +0x2C/+0x30 iconos ya resueltos por carril
  Icono ic[2][15];
  Evento ev[16];
};
inline bool (*g_campo)(const Entrada&, Salida&, uint32_t& semilla) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvars dbz3_iw_7dc_original (de serie apagada) y dbz3_iw_logica_original
}  // namespace nb014
}  // namespace dbz3::iwmips
