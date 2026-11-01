#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// MSN019 / 0x7E1, EU SLES-55347. Nucleo sin motor, reloj global ni efectos.
// Integracion experimental en iw_mini_botones.cpp, solo doble puerta de pruebas.
// Contrato y evidencia: D:/tmp/astraIW/MSN019_INTEGRACION.md.
// Las salidas son ordenes de una sola llamada, no estados persistentes del motor.
// El llamador conserva el orden Results / MgPly del original: no se impone aqui
// un orden entre sus tareas. No usar duraciones estimadas para fabricar C74/CC8.
namespace dbz3 {
namespace iwmsn019 {

constexpr int kSinPeticion = -2;
constexpr int kFilas = 10;
constexpr std::array<std::uint16_t, 6> kMascaras = {{
    0xF000, 0xF000, 0xF000, 0xFC00, 0xFC00, 0xFF00}};
constexpr std::array<int, 6> kObjetos = {{4, 5, 7, 8, 9, 10}};
constexpr std::array<int, 4> kPuntos = {{10, 5, 3, 0}};

// EU2EBA10. El tipo 8 y el valor por defecto NO se reducen a un solo bit.
constexpr std::uint16_t MascaraTipo(int tipo) {
  return tipo == 0 ? 0x8000 : tipo == 1 ? 0x1000 : tipo == 2 ? 0x2000 :
         tipo == 3 ? 0x4000 : tipo == 4 ? 0x0400 : tipo == 5 ? 0x0800 :
         tipo == 6 ? 0x0100 : tipo == 7 ? 0x0200 : tipo == 8 ? 0x00F0 : 0xFFFF;
}
constexpr int NivelDeFila(int fila) { return fila < 3 ? 0 : fila < 6 ? 2 : 3; }
constexpr int Categoria(int vueltas) {
  return vueltas >= 0 && vueltas <= 2 ? 0 : vueltas > 2 && vueltas <= 4 ? 1 :
         vueltas > 4 && vueltas <= 7 ? 2 : 3;
}

struct Actor {
  int estado = -1;                // +CBC; -1 = aun no inicializado por el adaptador
  int anterior = -1;              // +CBE
  std::int16_t repeticion = 0;    // +CC0, incrementa al REENTRAR en el mismo estado
  int destino = -1;               // +CC4; InitRound lo pone a -1
  std::uint16_t codigo = 0;       // +B60, codigo IW, no alias B3
  std::uint64_t generacion = 0;   // solo host: evita consumir el fin del clip anterior
  float cc8 = 0.0f;              // contador PORTADO: no es el reloj del clip B3
  float cac = 1.0f;              // modelo+66C, inicializado a1; distinto de CB4
};

// EU207180 Animations2: ultimo paso, DESPUES de TickActor. El llamador aporta
// dt logico y CAC efectivo (pausa=0, cambios de velocidad NO se deducen de CB4).
// No se multiplica por la velocidad/AP del clip de AnimationFrames.
inline bool AvanzarContadorActor(Actor& a, float dt, float cac) {
  if (!std::isfinite(dt) || !std::isfinite(cac) || dt < 0.0f || cac < 0.0f) return false;
  const float siguiente = a.cc8 + dt * cac;
  if (!std::isfinite(siguiente)) return false;
  a.cac = cac;
  a.cc8 = siguiente;
  return true;
}

struct OrdenActor {
  bool reproducir = false;       // reiniciar incluso si codigo == codigo anterior
  bool retener_ultimo = false;   // C68 = C6C - 1, solo DESPUES de C74 != 0
  bool reiniciar_cc8 = false;
  bool limpiar_cc6_y_206f = false;
  bool escribir_cb4 = false, escribir_cb8 = false;
  float cb4 = 0, cb8 = 0;       // campos originales, no supuestas duraciones
  int voz_evento = -1;
  int audio_slot = -1;
  int efecto = -1;               // >0: antes emitir Effect(0,...); 0: solo limpiar
  int camara = -1;               // tabla EU4985A0, resuelta en ELF
  bool limpiar_185c = false;
  bool leer_battle_main = false; // w_READ_BattleMain; adaptacion pendiente del host
  int vibracion_a = -1, vibracion_b = 0, vibracion_c = 0;
};

inline std::int16_t IncrementarRepeticion(std::int16_t valor) {
  const unsigned v = (static_cast<unsigned>(static_cast<std::uint16_t>(valor)) + 1u) & 0xFFFFu;
  return static_cast<std::int16_t>(v < 0x8000u ? static_cast<int>(v) : static_cast<int>(v) - 0x10000);
}

// EU308BD0 + entradas de la tabla EU498E70. Solo estados MSN019 0..7.
// personaje_iw: 15 Videl / 14 Satan. No pasar el indice del banco CMN.
inline OrdenActor EntrarActor(Actor& a, int estado, int personaje_iw) {
  OrdenActor o;
  if (estado < 0 || estado > 7) return o;
  const auto codigo_anterior = a.codigo;
  a.anterior = a.estado;
  a.estado = estado;
  a.repeticion = a.anterior == estado ? IncrementarRepeticion(a.repeticion) : 0;
  ++a.generacion;
  a.cc8 = 0.0f; // TODOS los Enter, incluso mismo estado/codigo; CAC se conserva
  o.reproducir = o.reiniciar_cc8 = o.limpiar_cc6_y_206f = true;
  const bool videl = personaje_iw == 15;
  switch (estado) {
    case 0: // EU308890 (entrada ausente del C; comprobada en ELF)
      a.codigo = 0x76C;
      o.escribir_cb4 = true;
      o.cb4 = a.destino == -1 ? 1.0f : 0.0f;
      if (a.destino != -1) { o.escribir_cb8 = true; o.cb8 = 0.02f; }
      o.audio_slot = 0;
      break;
    case 1: // EU308610. No modulo 3: para repeticion >2 conserva B60.
      if (a.repeticion == 0) a.codigo = 0x776;
      else if (a.repeticion == 1) a.codigo = 0x777;
      else if (a.repeticion == 2) a.codigo = 0x778;
      if (a.repeticion >= 0 && a.repeticion <= 2) {
        o.escribir_cb4 = true; o.cb4 = a.repeticion == 0 ? 0.0f : 1.0f;
      }
      if (a.repeticion == 0) {
        o.escribir_cb8 = true; o.cb8 = 0.3f;
        o.efecto = videl ? 9 : 11;
        o.audio_slot = videl ? 3 : 4;
      }
      o.leer_battle_main = true;
      break;
    case 2:
    case 3: { // EU3082D0 / EU307F60
      const int previo_nivel = estado == 2 ? 0 : 2;
      const std::uint16_t entrada = estado == 2 ? 0x76D : 0x76F;
      const std::uint16_t espera = estado == 2 ? 0x76E : 0x770;
      a.codigo = a.repeticion == 0 && a.anterior == previo_nivel ? entrada : espera;
      if (a.repeticion != 0 && codigo_anterior == entrada) o.voz_evento = estado == 2 ? 3 : 4;
      o.escribir_cb4 = true;
      o.cb4 = a.repeticion < 1 ? 0.01f : 1.0f;
      if (a.repeticion < 1) { o.escribir_cb8 = true; o.cb8 = 0.03f; }
      if (a.repeticion == 0 && a.anterior == previo_nivel) o.efecto = 3;
      if (a.repeticion == 1 || a.repeticion == 0)
        o.audio_slot = a.repeticion == 0 && a.anterior == previo_nivel ? 5 : estado - 1;
      break;
    }
    case 4:
    case 5:
    case 7:
      a.codigo = estado == 4 ? 0x7BC : estado == 5 ? 0x7BD : 0x7BE;
      o.escribir_cb4 = o.escribir_cb8 = true; o.cb4 = 1.0f; o.cb8 = 0.05f;
      o.leer_battle_main = true;
      if (estado != 5) { o.limpiar_185c = true; o.efecto = 0; }
      if (estado == 5) o.audio_slot = videl ? 7 : 9;
      if (estado == 7) o.camara = personaje_iw == 14 ? 0x5AF : 0x5AC;
      break;
    case 6:
      a.codigo = 0x780;
      o.escribir_cb4 = o.escribir_cb8 = true; o.cb4 = 0.01f; o.cb8 = 0.3f;
      o.efecto = videl ? 10 : 12;
      o.vibracion_a = 0xAC; o.vibracion_b = 0xB4; o.vibracion_c = 200;
      break;
  }
  return o;
}

// EU2EACD0. El resultado no altera +290: ese campo pertenece a Results.
inline OrdenActor PedirReaccion(Actor& a, int peticion, int fila, int personaje_iw) {
  OrdenActor o;
  int siguiente = -1;
  if (peticion == 0 && (fila == 0 || fila == 3 || fila == 6)) siguiente = NivelDeFila(fila);
  else if (peticion == 1) siguiente = 6;
  else if (peticion == 2) siguiente = 4;
  else if (peticion == 3) siguiente = 5;
  else if (peticion == 4 || peticion == -1) {
    a.destino = peticion == 4 ? 7 : NivelDeFila(fila);
    siguiente = 1;
  }
  if (siguiente != -1) o = EntrarActor(a, siguiente, personaje_iw);
  if (peticion == 0 && siguiente != -1) {
    o.vibracion_a = fila == 6 ? 0x20 : fila == 3 ? 0x10 : 0;
    o.vibracion_b = 0x3C; o.vibracion_c = 100;
  }
  return o;
}

// Tick de PreExec ANTES de AllAnimations. CC8 y dt son observaciones del
// reloj original/equivalente, no el fotograma visible del clip ni segundos.
inline OrdenActor TickActor(Actor& a, float cc8, float dt, int personaje_iw) {
  OrdenActor o;
  if (!(dt > 0.0f)) return o;
  const bool videl = personaje_iw == 15;
  if ((a.estado == 0 || a.estado == 2 || a.estado == 3) && cc8 > 20.0f && cc8 <= 20.0f + dt) {
    if (a.estado == 0) o.efecto = 4;
    else {
      const int previo_nivel = a.estado == 2 ? 0 : 2;
      if (a.repeticion == 0 && a.anterior != previo_nivel) {
        o.efecto = a.estado == 2 ? (videl ? 5 : 7) : (videl ? 6 : 8);
        a.repeticion = 2; // escritura REAL de CC0 en EU308140/EU307DD0
      } else if (a.repeticion == 1) {
        o.efecto = a.estado == 2 ? (videl ? 5 : 7) : (videl ? 6 : 8);
      }
    }
  }
  const float umbral = a.estado == 5 ? (videl ? 0.0f : 75.0f) :
                       a.estado == 6 ? (videl ? 67.0f : 127.0f) :
                       a.estado == 7 ? (videl ? 50.0f : 90.0f) : -1.0f;
  if (umbral >= 0.0f && umbral <= cc8 && cc8 < umbral + dt)
    o.voz_evento = a.estado == 6 ? 5 : 6;
  return o;
}

struct FinAnimacion {
  std::uint64_t generacion = 0;
  std::uint16_t codigo = 0;
  bool terminada = false; // C74 DESPUES de AnimationFramesLink; no latch artificial
  float cc8 = 0;          // snapshot del contador portado en la fase Update
  bool cc8_valido = false; // contador logico valido, nunca j.t ni reloj de camara
};

inline OrdenActor ActualizarActor(Actor& a, const FinAnimacion& f, int personaje_iw) {
  OrdenActor o;
  if (f.generacion != a.generacion || f.codigo != a.codigo || !f.terminada) return o;
  if (a.estado == 1) { // EU3084B0
    if (a.destino == 7) {
      if (a.codigo == 0x777) {
        if (f.cc8_valido && f.cc8 > 179.0f) return EntrarActor(a, 7, personaje_iw);
      } else return EntrarActor(a, 1, personaje_iw);
    } else if (a.codigo == 0x778) {
      return EntrarActor(a, a.destino, personaje_iw);
    } else return EntrarActor(a, 1, personaje_iw);
  } else if (a.estado == 2 || a.estado == 3) {
    return EntrarActor(a, a.estado, personaje_iw);
  } else if (a.estado == 6) {
    o = EntrarActor(a, 5, personaje_iw);
    o.camara = personaje_iw == 14 ? 0x5AE : 0x5AB;
  } else if (a.estado == 5 || a.estado == 7) {
    o.retener_ultimo = true;
  }
  // Estados 0 y 4 solo AnimationFramesLink: NO reentrada automatica.
  return o;
}

// Auxiliar EU2E9790: el estado 1 (desapareciendo) YA cuenta como terminado.
// Recorrer los objetos 0x11..0x11+N INCLUSIVE; ignorar datos inexistentes.
struct Objeto {
  int estado = 3; // 0 disponible, 1 desapareciendo, 2 oculto, 3 excluido, 4 solapado
  int tipo = 0;
  int cuenta = 0; // datos UI +0xC; acierto escribe 10, no duracion de pose
};
struct OrdenObjeto {
  bool pendiente = false;
  bool ocultar = false;
  int animacion_ui = -1;
  float cuadro_ui = 0;
};
inline OrdenObjeto ActualizarObjeto(Objeto& objeto, bool solapa, bool recheck) {
  OrdenObjeto o;
  if (objeto.estado == 4) {
    o.pendiente = true;
    if (!solapa || recheck) {
      objeto.estado = 0; o.animacion_ui = 0xB; o.cuadro_ui = 26.0f;
    }
  } else if (objeto.estado == 1) {
    if (--objeto.cuenta < 0) { objeto.estado = 2; o.ocultar = true; }
  } else if (objeto.estado == 0) {
    o.pendiente = true;
    if (!recheck && solapa) { objeto.estado = 4; o.animacion_ui = 0x1B; }
  }
  return o;
}
inline OrdenObjeto AcertarObjeto(Objeto& objeto) {
  OrdenObjeto o;
  if (objeto.estado != 1) {
    objeto.estado = 1; objeto.cuenta = 10; o.animacion_ui = 0xC;
  }
  return o;
}

enum class Fase { Inactiva, InicioFila, Evaluar, Recheck, Tally, EsperarFinal, CalcularPremios, Entregada };
struct RegistroFila {
  int vueltas = -1;             // contexto Results +74 + fila*10h
  std::uint32_t intentos = 0;   // +78
  std::uint32_t aciertos = 0;   // +7C
  int categoria = 3;           // +80
};
struct Resultados {
  Fase fase = Fase::Inactiva;
  std::array<RegistroFila, kFilas> filas{};
  int fila = 0;                // +68, NO incrementar al entrar Tally
  int reaccion = 0;            // +290
  int cuenta = 0;              // task +30: 100 inicio / 60 tally / 30 error
  std::uint16_t intentos = 0, aciertos = 0; // +6E / +6C
  std::uint32_t vueltas_totales = 0;       // +70
  bool palanca_enclavada = false;          // +60
  std::uint16_t mascara = 0;
};

// InitRound EU2EB460 + StateWaitInput EU2EAA90: llamar cuando camara termine
// o START (bit 8), no por 200/300 cuadros host. Aplicar fuera reaccion 0,
// actor.destino=-1, posicion inicial y Bounce_Activate (contador=1).
inline bool IniciarResultados(Resultados& r, int dificultad) {
  if (dificultad < 0 || dificultad >= 6) return false;
  const bool palanca_enclavada = r.palanca_enclavada; // InitRound no escribe +60
  r = Resultados{};
  r.palanca_enclavada = palanca_enclavada;
  r.mascara = kMascaras[static_cast<std::size_t>(dificultad)];
  r.fase = Fase::InicioFila;
  r.cuenta = 100;
  return true;
}

struct SolapeEntrada {
  bool hay_datos = false;
  bool solapa = false; // consulta original tras Bounce_UpdateMove, no solo icono resaltado
  int tipo = 0;
};
struct EntradaResultados {
  bool objetos_terminados = false; // retorno de Results_UpdateItemStates, ANTES de mover
  int vueltas_antes_mover = 1;     // Bounce_GetField40, no numero de impactos en el borde
  std::uint16_t pulsaciones = 0;   // uRam004ADECE completo
  std::array<std::int16_t, 4> ejes{}; // contexto +50/+52/+54/+56, valores con signo
  const SolapeEntrada* objetos = nullptr; // orden UI creciente; solo N objetos, NO N+1
  std::size_t cantidad = 0;
  int estado_actor = -1;          // snapshot +CBC; -1 = falta observacion, no avanzar
  bool fin_actor = false;         // snapshot +C74; no el fin calculado por duracion
};
struct OrdenResultados {
  int reaccion = kSinPeticion;
  int objeto_acertado = -1;       // indice 0..N-1; llamar AcertarObjeto aunque ya resolvio
  bool error_individual = false;
  bool reabrir_entrada = false;
  bool fila_cerrada = false;
  bool ocultar_pendientes = false; // timeout: objetos N pasan a estado3; anim C para0/4
  bool mostrar_banner = false;    // solo cierre por objetos_terminados
  bool preparar_objetos = false;
  bool primera_preparacion = false;
  bool excluir_un_objeto = false; // RespawnItem elige UNO comun con Memory()%N
  bool activar_bounce = false;    // contador=1, ancla=posicion ACTUAL, no reiniciar cursor
  bool inicio_juego = false;
  bool ocultar_ui = false;
  bool desenlace = false;
  bool bueno = false;
  bool calcular_premios = false;
  int categoria = -1;
  int voz_evento = -1;
};

// Antes de PasoResultados: gauge, cursor-overlap e items en este orden; luego
// mover SOLO si DebeMoverCursor(). En Evaluar se consulta el solape de entrada
// DESPUES del movimiento. No llamar estos auxiliares en InicioFila/EsperarFinal.
inline bool ActualizaInterfaz(Fase fase) {
  return fase == Fase::Evaluar || fase == Fase::Recheck || fase == Fase::Tally;
}
inline bool DebeMoverCursor(Fase fase, bool objetos_terminados, int vueltas) {
  return fase == Fase::Recheck || fase == Fase::Tally ||
         (fase == Fase::Evaluar && !objetos_terminados && static_cast<std::uint32_t>(vueltas) <= 7u);
}

// EU2EB6F0 para su llamada MSN019 (mask,0,1,1). Se llama SOLO si no hubo
// pulsacion en la mascara: conservar el cortocircuito, incluso el enclavamiento.
inline bool ComprobarPalancas(Resultados& r, const std::array<std::int16_t, 4>& ejes) {
  bool supera32 = false, todos_menores32 = true;
  for (auto eje : ejes) {
    const int v = eje < 0 ? -static_cast<int>(eje) : static_cast<int>(eje);
    supera32 = supera32 || v > 32;
    todos_menores32 = todos_menores32 && v < 32;
  }
  if (r.palanca_enclavada) {
    if (todos_menores32) r.palanca_enclavada = false;
    return false;
  }
  if ((r.mascara & 0xF0) == 0xF0 && supera32) {
    r.palanca_enclavada = true;
    return true;
  }
  return false;
}
inline int TotalPuntos(const Resultados& r) {
  int total = 0;
  for (const auto& fila : r.filas) total += kPuntos[static_cast<std::size_t>(fila.categoria)];
  return total;
}

inline OrdenResultados PasoResultados(Resultados& r, const EntradaResultados& e) {
  OrdenResultados o;
  switch (r.fase) {
    case Fase::InicioFila: // EU2EA980: 100 -> -1, no movimiento de Bounce
      if (--r.cuenta < 0) {
        o.preparar_objetos = o.primera_preparacion = o.excluir_un_objeto = o.inicio_juego = true;
        r.fase = Fase::Evaluar;
      }
      break;
    case Fase::Evaluar: { // EU2EA1F0
      if (r.fila < 0 || r.fila >= kFilas) break;
      auto& fila = r.filas[static_cast<std::size_t>(r.fila)];
      // La finalizacion gana al pad. La comparacion timeout original es unsigned.
      if (e.objetos_terminados || static_cast<std::uint32_t>(e.vueltas_antes_mover) > 7u) {
        fila.vueltas = e.vueltas_antes_mover;
        fila.categoria = Categoria(fila.vueltas);
        r.vueltas_totales += static_cast<std::uint32_t>(fila.vueltas);
        o.categoria = fila.categoria;
        o.fila_cerrada = true;
        o.mostrar_banner = e.objetos_terminados;
        if (o.mostrar_banner) o.voz_evento = 1;
        o.ocultar_pendientes = !e.objetos_terminados;
        r.cuenta = 60;
        r.fase = Fase::Tally;
        break; // NO reaccion de actor por completar/fallar una fila
      }
      const bool intento = (e.pulsaciones & r.mascara) != 0 || ComprobarPalancas(r, e.ejes);
      if (!intento) break;
      ++fila.intentos; r.intentos = static_cast<std::uint16_t>(r.intentos + 1u);
      for (std::size_t i = 0; e.objetos && i < e.cantidad; ++i) {
        const auto& objeto = e.objetos[i];
        const auto mascara = MascaraTipo(objeto.tipo);
        if (objeto.hay_datos && objeto.solapa &&
            ((mascara & e.pulsaciones) != 0 || ((mascara & 0xF0) != 0 && r.palanca_enclavada))) {
          ++fila.aciertos; r.aciertos = static_cast<std::uint16_t>(r.aciertos + 1u);
          o.objeto_acertado = static_cast<int>(i);
          return o; // PRIMER solape correcto; no filtra por estado del objeto
        }
      }
      r.cuenta = 30; // EU5D9160; Recheck NO descuenta ni consulta este valor
      r.reaccion = -1;
      r.fase = Fase::Recheck;
      o.error_individual = true; o.reaccion = -1; o.voz_evento = 2;
      break;
    }
    case Fase::Recheck: // EU2EA140; cursor/items con parametro 1, Bounce sigue
      if (e.estado_actor < 0) break; // frontera host: no inventar salida del actor
      if (e.estado_actor != 1) r.reaccion = 0;
      if (r.reaccion != -1) { r.fase = Fase::Evaluar; o.reabrir_entrada = true; }
      break;
    case Fase::Tally: // EU2E9D40; cursor/items con parametro 0, Bounce sigue
      if (--r.cuenta < 0) {
        o.preparar_objetos = o.activar_bounce = true;
        o.excluir_un_objeto = r.fila + 1 < 5; // EU5D917C, antes de incrementar
        if (r.fila + 1 < kFilas) {
          ++r.fila;
          o.reaccion = r.reaccion;
          r.fase = Fase::Evaluar;
        } else {
          // Respawn y Bounce_Activate ocurren INCLUSO antes de ocultar la ultima fila.
          o.ocultar_ui = o.desenlace = true;
          o.bueno = TotalPuntos(r) > 0;
          if (o.bueno) r.reaccion = 1;
          o.reaccion = o.bueno ? r.reaccion : 4;
          r.fase = Fase::EsperarFinal;
        }
      }
      break;
    case Fase::EsperarFinal: // EU2E9920, resuelta en ELF (LAB ausente del C)
      if (e.fin_actor && (TotalPuntos(r) > 0 || e.estado_actor == 7))
        r.fase = Fase::CalcularPremios;
      break;
    case Fase::CalcularPremios: // callback siguiente EU2E9AF0, una sola entrega
      o.calcular_premios = true;
      r.fase = Fase::Entregada;
      break;
    case Fase::Inactiva:
    case Fase::Entregada:
      break;
  }
  return o;
}

} // namespace iwmsn019
} // namespace dbz3
