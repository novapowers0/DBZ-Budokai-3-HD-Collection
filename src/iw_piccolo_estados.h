#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// MSN010 / 0x7D8, EU SLES-55347. Candidato SIN integrar, sin SDK ni efectos.
// Evidencia, contrato, limites y pruebas: D:/tmp/astraIW/PICCOLO_INTEGRACION.md.
// Las funciones devuelven copias: un error conserva integramente el estado de
// entrada y no consume azar. El llamador aplica las ordenes SOLO si error==Ninguno.
namespace dbz3 {
namespace iwpiccolo {

constexpr float kPi = 3.1415927f;
constexpr float kLimiteInclinacion = 0.7853982f;
constexpr float kLimiteSorteo = 0.5235987901687622f;
constexpr float kUmbral98A0 = 0.25f;
constexpr float kUmbral9878 = 0.4f;

struct ReaccionHueso {
  std::uint8_t hueso, grupo;
  std::uint16_t campo02;  // dato literal; NO es el dano de EU32E730
  std::uint32_t codigo;
  float radio_mundo, radio_pantalla;
};

// ELF EU499A60, 22 registros de 16 bytes (<BBHIff). Indices IW, NO huesos B3.
constexpr std::array<ReaccionHueso, 22> kHuesos = {{
    { 0,1, 5,0x407,2.75f,20.0f}, { 1,1, 5,0x407,1.65f,15.0f},
    { 2,0,30,0x406,1.10f,10.0f}, { 3,0,30,0x406,1.10f,10.0f},
    { 4,4,15,0x40A,2.75f,20.0f}, { 5,5,15,0x40B,2.75f,20.0f},
    { 6,4,15,0x40A,2.20f,20.0f}, { 7,5,15,0x40B,2.20f,20.0f},
    { 8,2,20,0x408,1.65f,14.0f}, { 9,3,20,0x409,1.65f,14.0f},
    {10,2,20,0x408,1.65f,14.0f}, {11,3,20,0x409,1.65f,14.0f},
    {12,4,15,0x40A,2.20f,20.0f}, {13,5,15,0x40B,2.20f,20.0f},
    {14,2,20,0x408,1.65f,14.0f}, {15,3,20,0x409,1.65f,14.0f},
    {16,2,20,0x408,1.76f,15.0f}, {17,3,20,0x409,1.76f,15.0f},
    {18,1, 5,0x407,2.20f,18.0f}, {19,0,30,0x406,2.53f,19.5f},
    {20,1, 5,0x407,2.20f,20.0f}, {21,0,30,0x406,2.53f,19.5f}
}};

struct Giro { float horizontal, vertical; };
// ELF EU499BC0: incrementos POR LLAMADA, sin multiplicar por dt/velocidad.
constexpr std::array<Giro, 6> kGiros = {{
    {0.0f,-0.01f}, {-0.001f,0.001f}, {-0.005f,-0.005f},
    {0.005f,-0.005f}, {-0.005f,0.005f}, {0.005f,0.005f}
}};
// EU480B10 -> inicializador EU3279F0 -> RAM 5D987C, NO campo02.
constexpr std::array<int, 6> kDanos = {{40,3,12,12,10,10}};

enum class Error {
  Ninguno, EstadoInvalido, EntradaInvalida, PasoNoSoportado,
  ObservacionDeOtroClip, AzarInsuficiente, HuesoNoResuelto
};

struct Estado {
  int pose_estado = 1;              // CBC: solo 1/2/3 en este nucleo
  int pose_anterior = 0;            // CBE
  std::uint16_t repeticion_cc0 = 0; // conserva bits de la suma s16 original
  int variante_cc4 = 0;
  float contador_cc8 = 0;           // observado del actor; no inventa duraciones
  std::uint32_t codigo = 0x3E8;     // B60 IW, nunca alias B3
  std::uint64_t generacion = 0;     // host: rechaza fin de un clip sustituido
  int scheduler_modo = 0;           // 5D994C, actualizado por salud en EU32DC80
  bool punteria_9950 = false;       // tambien submodo del scheduler: dot > .9
  float velocidad9954 = 0;
  int direccion = 0;               // 9958: 0 +azimut, 1 -azimut, 2 +incl., 3 -incl.
  int direccion_anterior = 0;       // 995C
  float tiempo9960 = 0;
  std::uint32_t contador9964 = 0;   // NO es una cuenta atras; no se decrementa
  int ultimo_hueso = -1;            // sustituye puntero 9948 por indice validado
  bool pulso9968 = false;
  float expansion996C = 0;
  float fase9970 = 0;
};

struct Orden {
  bool reproducir = false;
  std::uint32_t codigo = 0;
  std::uint64_t generacion = 0;
  bool reiniciar_cc8 = false;
  bool limpiar_cc6_y_206f = false;
  bool escribir_cc4 = false;
  int cc4 = 0;
  float cb4 = 0, cb8 = 0; // valores originales; traduccion a API de animacion aparte
  bool sonido_esquiva = false;
  int efecto = -1;        // EU347DF0: 12/13/14 por grupo, no ID de efecto B3
  int hueso_efecto = -1;
};

struct Resultado {
  Estado estado;
  Error error = Error::Ninguno;
  Orden orden;
  std::size_t azar_consumido = 0;
  float delta_azimut = 0, delta_inclinacion = 0;
  float inclinacion = 0; // salida absoluta, incluye clamp previo en pose2
  bool rotacion_suspendida = false;
  bool impacto = false;
  int dano = 0;
  float radio_usado = 60; // radio ANTES de avanzar el pulso (EU32D820)
};

// Cinta de resultados de Memory(32), incluyendo sorteos que luego se descartan.
// No es una semilla. Sin callbacks que puedan modificar objetos externos.
struct Azar {
  const std::uint32_t* valores = nullptr;
  std::size_t cantidad = 0;
};

struct ObservacionClip {
  std::uint64_t generacion = 0;
  bool terminado_c74 = false;
  float contador_cc8 = 0;
};

struct Entrada {
  float dt = 1; // un paso logico de 60 Hz; otros pasos fallan explicitamente
  float dot_punteria = 0;
  float salud = 1, salud_maxima = 1; // contexto+4 / tabla98B8[nivel]
  float paramBC = 0;               // tabla98BC[nivel], radianes/paso; inyectado
  float inclinacion = 0;           // RAM9944 en radianes, no polar B3
  ObservacionClip clip;
  Azar azar;
};

inline bool EstadoValido(const Estado& e) {
  return e.pose_estado >= 1 && e.pose_estado <= 3 &&
         e.scheduler_modo >= 0 && e.scheduler_modo <= 1 &&
         e.direccion >= 0 && e.direccion <= 3 &&
         e.direccion_anterior >= 0 && e.direccion_anterior <= 3 &&
         e.ultimo_hueso >= -1 && e.ultimo_hueso < 22 &&
         std::isfinite(e.velocidad9954) && std::isfinite(e.tiempo9960) &&
         std::isfinite(e.expansion996C) && std::isfinite(e.fase9970) &&
         std::isfinite(e.contador_cc8);
}

inline Resultado Fallar(const Estado& e, Error error) {
  Resultado r;
  r.estado = e;
  r.error = error;
  return r;
}

namespace detalle {
inline void Entrar(Resultado& r, int pose) {
  Estado& e = r.estado;
  e.pose_anterior = e.pose_estado;
  e.pose_estado = pose;
  e.repeticion_cc0 = e.pose_anterior == pose
      ? static_cast<std::uint16_t>(e.repeticion_cc0 + 1u) : 0;
  e.contador_cc8 = 0;
  ++e.generacion;
  Orden& o = r.orden;
  o.reproducir = o.reiniciar_cc8 = o.limpiar_cc6_y_206f = true;
  o.generacion = e.generacion;
  o.cb8 = 0.25f;
  if (pose == 1) {
    e.codigo = 0x3E8;
    o.cb4 = 0;
  } else if (pose == 2) {
    const bool larga = e.tiempo9960 > 35.0f;
    e.variante_cc4 = larga ? 1 : 0;
    e.codigo = 0x3F2u + static_cast<unsigned>(e.direccion) + (larga ? 4u : 0u);
    o.escribir_cc4 = true;
    o.cc4 = e.variante_cc4;
    o.cb4 = larga ? 0.0f : 1.0f;
    o.cb8 = larga ? 0.05f : 0.25f;
    o.sonido_esquiva = true;
  } else {
    const ReaccionHueso& h = kHuesos[static_cast<std::size_t>(e.ultimo_hueso)];
    e.codigo = h.codigo;
    o.cb4 = 1;
    o.efecto = h.grupo == 0 ? 12 : h.grupo == 1 ? 13 : 14;
    o.hueso_efecto = h.hueso;
    if (!e.pulso9968) {
      e.pulso9968 = true;
      e.expansion996C = e.fase9970 = 0;
    } else if (e.fase9970 >= kPi) {
      e.fase9970 = kPi - e.fase9970;
    }
  }
  o.codigo = e.codigo;
}

inline bool Sacar(const Azar& a, std::size_t& cursor, std::uint32_t& valor) {
  if (!a.valores || cursor >= a.cantidad) return false;
  valor = a.valores[cursor++];
  return true;
}

// EU32D6B0 y EU32D540. La segunda criba NO vuelve a evitar la direccion previa
// ni repite la prueba de inclinacion; cambiar eso modifica el algoritmo original.
inline bool Programar(Resultado& r, const Entrada& in) {
  Estado& e = r.estado;
  std::uint32_t v = 0;
  const float base = in.dt * in.paramBC;
  if (e.scheduler_modo == 0) {
    e.velocidad9954 = base;
    e.tiempo9960 = 0;
    if (!Sacar(in.azar, r.azar_consumido, v)) return false;
    e.contador9964 = v % 10u; // divu en ELF, no modulo firmado del C
  } else {
    // EU11A290 -> EU11A2E8 es DIVISION double (resta exponentes y divide
    // mantisas). El nombre opaco del decomp NO demuestra multiplicacion.
    e.velocidad9954 = static_cast<float>(static_cast<double>(base) / 1.5);
    if (!Sacar(in.azar, r.azar_consumido, v)) return false; // %120 sobrescrito
    e.tiempo9960 = 120;
    if (!Sacar(in.azar, r.azar_consumido, v)) return false;
    e.contador9964 = v % 5u;
  }
  e.direccion_anterior = e.direccion;
  if (!Sacar(in.azar, r.azar_consumido, v)) return false; // Memory(32) inicial descartado
  do {
    if (!Sacar(in.azar, r.azar_consumido, v)) return false;
    e.direccion = static_cast<int>(v & 3u);
  } while (e.direccion == e.direccion_anterior);
  const int prohibida = e.direccion == 3 && in.inclinacion < -kLimiteSorteo ? 3 :
                        e.direccion == 2 && in.inclinacion > kLimiteSorteo ? 2 : -1;
  if (prohibida >= 0) {
    do {
      if (!Sacar(in.azar, r.azar_consumido, v)) return false;
      e.direccion = static_cast<int>(v & 3u);
    } while (e.direccion == prohibida);
  }
  if (e.punteria_9950) e.contador9964 = 1;
  return true;
}
} // namespace detalle

// Pedir entrada1 al iniciar la fase jugable; no llamar cada cuadro.
// El llamador proporciona el estado inicial del scheduler si importa reproducir
// una captura existente; no se afirma que todos sus campos se reseteen en D420.
inline Resultado EntrarEspera(const Estado& anterior) {
  if (!EstadoValido(anterior)) return Fallar(anterior, Error::EstadoInvalido);
  Resultado r;
  r.estado = anterior;
  detalle::Entrar(r, 1);
  return r;
}

// EU32DC80 exclusivamente. No avanza animacion, no despacha fin de pose3,
// no toca radio ni coordenadas B3. Consume el fin de clip que observa ESTA tarea.
inline Resultado ActualizarRotacion(const Estado& anterior, const Entrada& in) {
  if (!EstadoValido(anterior)) return Fallar(anterior, Error::EstadoInvalido);
  if (in.dt != 1.0f) return Fallar(anterior, Error::PasoNoSoportado);
  if (!std::isfinite(in.dot_punteria) || !std::isfinite(in.salud) ||
      !std::isfinite(in.salud_maxima) || in.salud_maxima <= 0 ||
      !std::isfinite(in.paramBC) || !std::isfinite(in.inclinacion) ||
      !std::isfinite(in.clip.contador_cc8))
    return Fallar(anterior, Error::EntradaInvalida);
  if (in.clip.generacion != anterior.generacion)
    return Fallar(anterior, Error::ObservacionDeOtroClip);
  Resultado r;
  r.estado = anterior;
  r.inclinacion = in.inclinacion;
  Estado& e = r.estado;
  e.contador_cc8 = in.clip.contador_cc8;
  e.punteria_9950 = in.dot_punteria > 0.9f;
  const float salud = in.salud / in.salud_maxima;
  if (salud < kUmbral98A0) {
    r.rotacion_suspendida = true; // no actualiza 994C ni tiempo ni velocidad
    return r;
  }
  e.scheduler_modo = salud < kUmbral9878 ? 1 : 0;
  if (e.pose_estado == 1) {
    if (e.contador9964 == 0) {
      if (in.clip.terminado_c74 || in.dot_punteria > 0.95f) {
        if (!detalle::Programar(r, in)) return Fallar(anterior, Error::AzarInsuficiente);
        if (!in.clip.terminado_c74) e.contador9964 = 1;
      }
    } else if (in.dot_punteria >= 0.5f) {
      if (!detalle::Programar(r, in)) return Fallar(anterior, Error::AzarInsuficiente);
      detalle::Entrar(r, 2); // no mueve en este mismo despacho
    }
  } else if (e.pose_estado == 2) {
    e.tiempo9960 -= in.dt;
    if (in.inclinacion < -kLimiteInclinacion || in.inclinacion > kLimiteInclinacion) {
      r.inclinacion = in.inclinacion < 0 ? -kLimiteInclinacion : kLimiteInclinacion;
      detalle::Entrar(r, 1);
    } else if (in.dot_punteria < 0.5f ||
               (e.scheduler_modo == 0 && in.clip.terminado_c74) ||
               (e.scheduler_modo == 1 && e.tiempo9960 < 0)) {
      detalle::Entrar(r, 1);
    } else {
      const float v = e.velocidad9954;
      if (e.direccion == 0) r.delta_azimut = v;
      if (e.direccion == 1) r.delta_azimut = -v;
      if (e.direccion == 2) r.delta_inclinacion = v;
      if (e.direccion == 3) r.delta_inclinacion = -v;
      if (e.scheduler_modo == 0) e.velocidad9954 -= (in.dt * in.paramBC) / 70.0f;
      // Sin clamp de velocidad ni clamp posterior de inclinacion en el original.
    }
  } else if (e.ultimo_hueso >= 0) {
    const Giro g = kGiros[kHuesos[static_cast<std::size_t>(e.ultimo_hueso)].grupo];
    r.delta_azimut = g.horizontal;
    r.delta_inclinacion = g.vertical;
    const float siguiente = in.inclinacion + g.vertical;
    if (siguiente < -kLimiteInclinacion || siguiente > kLimiteInclinacion) {
      r.delta_inclinacion = 0;
      r.delta_azimut *= e.scheduler_modo == 1 ? 4.0f : 2.0f;
    }
  }
  r.inclinacion += r.delta_inclinacion;
  return r;
}

// EU32DBF0 -> tabla499C20[3] = EU32D100 (pese a su nombre TickState4).
// clip debe observarse DESPUES del AnimationFramesLink de esa tarea. El tick
// tabla499C50[3] es EU32D0F0, un retorno vacio: no confundir ambos despachos.
inline Resultado DespacharFinReaccion(const Estado& anterior, const ObservacionClip& clip) {
  if (!EstadoValido(anterior)) return Fallar(anterior, Error::EstadoInvalido);
  if (clip.generacion != anterior.generacion)
    return Fallar(anterior, Error::ObservacionDeOtroClip);
  if (!std::isfinite(clip.contador_cc8)) return Fallar(anterior, Error::EntradaInvalida);
  Resultado r;
  r.estado = anterior;
  if (anterior.pose_estado == 3) {
    r.estado.contador_cc8 = clip.contador_cc8;
    if (clip.terminado_c74) detalle::Entrar(r, 1);
  }
  return r;
}

struct EntradaGolpe {
  int hueso_iw = -1; // -1 = fallo de colision; otros invalidos FALLAN
  float salud_antes = 1, salud_maxima = 1;
  bool retorno_ui_timer_no_cero = false; // valor REAL EU31ECC0, no nombre inferido
  std::array<int, 6> danos = kDanos;      // permite inyectar tabla RAM si difiere
};

// EU32E730: primero fija 9948, comprueba agotamiento ANTES de restar dano,
// entra3 si procede, y devuelve dano. La salud y el KO se resuelven fuera.
inline Resultado AplicarGolpe(const Estado& anterior, const EntradaGolpe& in) {
  if (!EstadoValido(anterior)) return Fallar(anterior, Error::EstadoInvalido);
  if (in.hueso_iw < -1 || in.hueso_iw >= 22)
    return Fallar(anterior, Error::HuesoNoResuelto);
  if (!std::isfinite(in.salud_antes) || !std::isfinite(in.salud_maxima) || in.salud_maxima <= 0)
    return Fallar(anterior, Error::EntradaInvalida);
  Resultado r;
  r.estado = anterior;
  if (in.hueso_iw == -1) return r;
  r.impacto = true;
  r.estado.ultimo_hueso = in.hueso_iw;
  r.dano = in.danos[kHuesos[static_cast<std::size_t>(in.hueso_iw)].grupo];
  const bool agotado = !in.retorno_ui_timer_no_cero && in.salud_antes / in.salud_maxima <= 0;
  if (!agotado) detalle::Entrar(r, 3);
  return r;
}

// Subpaso radial de EU32D820: el consumidor usa radio_usado para la posicion
// actual; expansion y fase devueltas se usaran en la SIGUIENTE posicion.
// Llamar solo cuando el flag original 9924 habilita este bloque, incluso si
// ActualizarRotacion quedo suspendida por salud<25%.
inline Resultado ActualizarRadio(const Estado& anterior, float dt = 1.0f) {
  if (!EstadoValido(anterior)) return Fallar(anterior, Error::EstadoInvalido);
  if (dt != 1.0f) return Fallar(anterior, Error::PasoNoSoportado);
  Resultado r;
  r.estado = anterior;
  r.radio_usado = 60.0f + anterior.expansion996C;
  Estado& e = r.estado;
  if (e.fase9970 > kPi) {
    e.fase9970 = e.expansion996C = 0;
    e.pulso9968 = false;
  }
  if (e.pulso9968) {
    e.expansion996C = std::sin(e.fase9970) * 10.0f;
    e.fase9970 = (dt * 0.1f + e.fase9970) + 0.0f;
  }
  return r;
}

} // namespace iwpiccolo
} // namespace dbz3
