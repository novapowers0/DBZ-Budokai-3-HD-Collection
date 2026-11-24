// Ejecutor común de transiciones B2. Guion/tareas/reloj reales, sin secuencias
// prefijadas. La adaptación de las órdenes al motor B3 es obligatoria y explícita.
#pragma once
#include "b2_vm.h"
#include "escenarios_b2_recursos.h"
#include <cmath>
#include <functional>
#include <memory>

namespace dbz3::escenarios::b2 {
// CONTRATO P1/F4 (2026-11-10), revision 1; consumidores F5/S5/F7/S4/F8.
// Se conserva la firma Actor/Orden/Puente/Transicion y B2TZv2. F5 entrega
// recursos y adapta ordenes a este puente: no crea otra VM ni otro reloj.
// Extension optativa F4: Puente::validar_inicio=false conserva el recorrido
// anterior; true comprueba contenedor/caches/SPX y reinicia el estado de escena.
//
// VIDA: Recurso se obtiene con b2::Leer/Cargar y permanece inmutable y vivo
// hasta terminar la Transicion. Iniciar exige version 2, SPX de combate y
// CAMR/TRAY/MDD0/FX00/MAD0/MAS0 validos; el banco FX00 pertenece al escenario,
// nunca al personaje donante. FXHD/SND0/ANM*/BSK0/ENV0 son extensiones: cada
// consumidor exige las que realmente use y devuelve false si faltan.
//
// ROLES: actores[0] = atacante, actores[1] = victima del contacto REAL.
// No equivalen a P1/P2: B2MotorHD::Iniciar resuelve el actor fisico. S5 consume
// matrices finales en B2MotorHD::Paso, despues de trayectoria/animacion y
// antes de aplicar camara; mirada primero, IK despues. No usar el gancho
// intermedio sub_82110AB8 ni ajustar desfase para mejorar la comparacion.
//
// ORDEN: id = builtin de la tabla PAL 0x375190; pc = offset en SPX;
// argumentos[0..cuenta) = palabras de 32 bits en orden de firma, no direcciones
// HD. Los floats conservan sus bits (b2vm::Float/Bits); los enteros con signo
// usan b2vm::Signo. El adaptador no altera la orden observada.
// ejecutar=true SOLO tras consumir la orden. Las consultas escriben h.r[0]
// (entero/puntero virtual B2) o h.f[0] (bits float); el bool no es ese retorno.
// Las esperas del reloj instalan h.sondeo; el sondeo devuelve 3 mientras
// espera y 1 al continuar. ejecutar no codifica retornos del planificador.
// Sin consumidor/argumento/recurso: false, VM
// bloqueada con Error(), sin aplicar cambios internos de esa orden.
// observar recibe la orden antes del consumidor y no acredita su ejecucion.
//
// RELOJ: Paso(delta) recibe cuadros de simulacion B2, nunca milisegundos ni
// ticks de dibujo. El motor llama una vez por guest frame (delta=1). Se avanza
// el reloj ANTES de la VM (PAL1D4630); builtin4 espera con tolerancia 0,5;
// builtin9 devuelve delta en f0. Camara tipo2 y trayectoria C1 empiezan en
// delta; no sumar un segundo avance en el mismo paso. Pausa no llama Paso.
// Viva=false incluye final normal y bloqueo: consultar Error para distinguir.
// Politica de activacion/online/rollback y restauracion de vida/ki/HUD pertenece
// al motor anfitrion; publicar este contrato no habilita escenas nuevas.
inline constexpr uint32_t kContratoTransicion = 1;

struct Actor {
  std::array<float, 3> posicion{};
  uint32_t giro = 0, codigo = 0;
  int hueso = -1, clip = 0;
  float tiempo = 0;
};
struct Orden {
  uint32_t id = 0, pc = 0;
  float cuadro = 0;
  int tarea = -1;
  std::array<uint32_t, 16> argumentos{};
  size_t cuenta = 0;
};
struct Puente {
  // Una orden sin consumidor bloquea la instancia: nunca se simula éxito.
  // Para consultas, el consumidor escribe r0/f0 del hilo, como el builtin B2.
  std::function<bool(const Orden&, dbz3::b2vm::Maquina&, dbz3::b2vm::Tarea&)> ejecutar;
  std::function<void(const Orden&)> observar;
  bool validar_inicio = false;
};
class Transicion {
 public:
  dbz3::b2vm::Maquina vm{dbz3::b2vm::Configurar()};
  dbz3::b2vm::Reloj reloj;
  std::array<Actor, 2> actores{};
  Puente puente;
  uint32_t zona = 0, entrada = 0;
  int camara = -1;
  float tiempo_camara = 0, inicio_camara = 0;
  std::array<float, 3> ojo{}, objetivo{};
  float giro_camara = 0, fov = 0;

  // Preflight sin mutar VM/HUD/actores: el anfitrion puede llamarlo ANTES
  // de instalar animaciones. No acredita consumidores ni fidelidad visual.
  static bool ValidarInicio(const Recurso& r, uint32_t e, std::string& error) {
    auto fallo = [&](const char* causa) { error = causa; return false; };
    if (r.version != 2) return fallo("inicio B2 requiere B2TZv2");
    Recurso verificado;
    if (!Leer(r.datos, verificado, error)) return false;
    if (r.version != verificado.version || r.grupo != verificado.grupo ||
        r.off != verificado.off || r.tam != verificado.tam)
      return fallo("metadatos B2 difieren del contenedor");
    auto mismos_clips = [](const std::vector<Clip>& a, const std::vector<Clip>& b) {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].cuadros != b[i].cuadros || a[i].tipo != b[i].tipo) return false;
        for (size_t j = 0; j < a[i].pistas.size(); ++j) {
          if (a[i].pistas[j].size() != b[i].pistas[j].size()) return false;
          for (size_t k = 0; k < a[i].pistas[j].size(); ++k)
            if (a[i].pistas[j][k].cuadro != b[i].pistas[j][k].cuadro ||
                a[i].pistas[j][k].valor != b[i].pistas[j][k].valor) return false;
        }
      }
      return true;
    };
    if (!mismos_clips(r.camara, verificado.camara) ||
        !mismos_clips(r.trayectoria, verificado.trayectoria))
      return fallo("cache ACC B2 difiere del contenedor");
    const Vista spx = verificado.Bloque(2);
    if (!spx.Rango(0, 0x24)) return fallo("cabecera SPX B2 truncada");
    const uint32_t codigo = spx.U(0x14), entradas = spx.U(0x18);
    if (codigo < 0x20 || codigo >= spx.n || entradas > (codigo - 0x20) / 4 || e >= entradas)
      return fallo("entrada/base SPX B2 invalida");
    const uint32_t destino = spx.U(0x20 + size_t(e) * 4) & dbz3::iw::spx::kMascaraDestino;
    if (destino >= spx.n - codigo) return fallo("destino SPX B2 fuera del guion");
    error.clear();
    return true;
  }

  bool Iniciar(const Recurso& r, uint32_t e, Puente p) {
    const bool validar = p.validar_inicio;
    // Un segundo inicio no debe cambiar el recurso/puente/reloj de tareas
    // todavia vivas ni bloquearlas por intentar vm.Cargar sobre ellas.
    if (validar && vm.Viva()) return false;
    if (validar && !ValidarInicio(r, e, error_)) return false;
    if (r.version < 2) { error_ = "B2TZv2 requerido: MAD/MAS y banco de efectos correcto"; return false; }
    recurso_ = &r;
    entrada = e;
    puente = std::move(p);
    error_.clear();
    reloj.cuadro = 0;
    const Vista spx = r.Bloque(2);
    if (!vm.Cargar(std::vector<uint8_t>(spx.p, spx.p + spx.n), dbz3::b2vm::kTabla)) return false;
    if (validar) {
      zona = 0; camara = -1; tiempo_camara = inicio_camara = 0;
      actores = {}; ojo = {}; objetivo = {}; giro_camara = fov = 0;
      reloj.delta = 1;
    }
    reloj.Instalar(vm);
    // Solo registrar funciones presentes en la tabla de combate B2, con su
    // firma en palabras. No usar la tabla de la pasarela/dialogos.
    for (uint32_t id : {0u,1u,0x10u,0x11u,0x13u,0x14u,0x15u,0x16u,0x18u,0x19u,0x1Cu,0x1Du,
          0x24u,0x25u,0x26u,0x27u,0x28u,0x2Au,0x2Du,0x31u,0x35u,0x40u,0x41u,0x45u,0x46u,0x47u,0x49u,0x4Cu,
          0x52u,0x61u,0x62u,0x64u,0x66u,0x78u,0x79u,0x7Eu,0x7Fu,0x84u,0x86u,0x87u,0x90u,
          0x94u,0x95u,0x96u,0x97u,0x98u,0xA6u,0xB3u,0xB8u,0xBDu,0xC1u,0xD0u,0xD1u,0xD2u,
          0x10Cu,0x124u,0x160u,0x163u})
      vm.Registrar(dbz3::b2vm::kTabla, id, [this,id](auto& m, auto& h) { return Nativa(m,h,id); });
    return vm.Iniciar(e) >= 0;
  }
  bool Paso(float delta = 1) {
    if (!error_.empty() || !vm.Viva()) return false;
    reloj.delta = delta;
    // PAL1D4630 avanza142FC antes de ejecutar el guion. Hacerlo después
    // aplazaba los cortes con espera (Nieve30/450), aunque la cámara inicial
    // de RR pareciera correcta. La espera4 conserva su tolerancia de0,5.
    reloj.cuadro += delta;
    if (camara >= 0) tiempo_camara += delta;
    for (auto& a : actores) {
      if (a.hueso < 0) continue;
      if (!recurso_ || a.clip < 0 || size_t(a.clip) >= recurso_->trayectoria.size()) { error_ = "trayectoria B2 inválida"; return false; }
      a.tiempo = std::min(a.tiempo + delta, float(recurso_->trayectoria[size_t(a.clip)].cuadros - 1));
    }
    if (!vm.Tick()) return false;
    if (camara >= 0 && recurso_ && size_t(camara) < recurso_->camara.size()) {
      std::array<std::array<float, 3>, 4> v;
      if (!Muestrear(recurso_->camara[size_t(camara)], tiempo_camara, v)) { error_ = "cámara B2 inválida"; return false; }
      ojo = v[0]; objetivo = v[1]; giro_camara = v[2][0]; fov = v[3][0];
    }
    return true;
  }
  bool Viva() const { return error_.empty() && vm.Viva() && !vm.Bloqueada(); }
  std::string Error() const { return error_.empty() ? vm.Error() : error_; }
  bool Trayectoria(size_t lado, std::array<float, 3>& posicion) const {
    if (!recurso_ || lado >= actores.size()) return false;
    const Actor& a = actores[lado];
    if (a.hueso < 0 || a.clip < 0 || size_t(a.clip) >= recurso_->trayectoria.size()) return false;
    std::array<std::array<float, 3>, 4> v;
    if (!Muestrear(recurso_->trayectoria[size_t(a.clip)], a.tiempo, v)) return false;
    // F5 2026-11-10: el original marca "sin destino de hueso" con la clave
    // (0,-100000,0) (Nieve, TRAY clip0 cuadro 41..419; medido en la ISO PAL).
    // No es una posicion: a partir de ahi la victima sigue por fisica nativa.
    // Aplicarla tal cual teletransporta al luchador -100000; se declara ausencia
    // y el motor conserva el camino nativo, como el original.
    if (!(std::fabs(v[0][1]) < 99999.0f)) return false;
    posicion = v[0];
    return true;
  }
 private:
  const Recurso* recurso_ = nullptr;
  std::string error_;
  static size_t Argumentos(uint32_t id) {
    switch (id) {
      case 0: case 1: case 0x40: case 0x52: case 0x66: case 0x7F:
      case 0x78: case 0x79: case 0x90: case 0x98: case 0xA6: case 0xB3: case 0x10C: return 0;
      case 0x18: case 0x28: case 0x2A: case 0x2D: case 0x4C:
      case 0x95: case 0x86: case 0x87: case 0xBD: case 0xD2: case 0x124: case 0x41: case 0x49: return 1;
      case 0x16: case 0x1C: case 0x1D: case 0x24: case 0x25: case 0x26: case 0x27:
      case 0x61: case 0x62: case 0x7E: case 0x84: case 0x97: case 0xB8: case 0xD0: case 0xD1: case 0x163: return 2;
      case 0x11: case 0x14: case 0x15: case 0x19: return 3;
      case 0x13: case 0x64: case 0x94: case 0xC1: return 4;
      case 0x10: case 0x96: return 5;
      case 0x35: return 6;
      case 0x31: return 8;
      case 0x160: return 14;
      default: return 0;
    }
  }
  uint32_t Nativa(dbz3::b2vm::Maquina& m, dbz3::b2vm::Tarea& h, uint32_t id) {
    Orden o;
    o.id = id; o.pc = h.r[9] - m.Base(); o.cuadro = reloj.cuadro; o.tarea = h.ranura; o.cuenta = Argumentos(id);
    for (size_t i = 0; i < o.cuenta; ++i) o.argumentos[i] = m.Arg(h, uint32_t(i));
    if (puente.observar) puente.observar(o);
    if (!id || id == 1) return 1; // ELF1FC390/1FC3A0 son solo return1.
    // Cambios de la VM que no dependen del renderer: mantener el consumidor
    // real obligatorio antes de considerar ejecutada una orden visual/física.
    if (!puente.ejecutar || !puente.ejecutar(o,m,h))
      m.Fallar("B2 nativa sin consumidor: " + std::to_string(id) + " PC " + std::to_string(o.pc));
    if (id == 0x35 && o.argumentos[5] == 2) {
      // PAL1CDED0: el tipo6 empieza en delta; 1CDC80 lo muestrea y después
      // avanza su reloj propio. No derivarlo del reloj global ni sumar dos
      // veces al seleccionar otra cámara dentro de este mismo paso.
      camara = int(o.argumentos[4]); tiempo_camara = reloj.delta;
      inicio_camara = reloj.cuadro - tiempo_camara;
    }
    if (id == 0x40) zona ^= 1;
    if (id == 0xC1 && o.argumentos[3] < actores.size()) {
      Actor& a = actores[o.argumentos[3]];
      a.hueso = int(int8_t(uint8_t(o.argumentos[2]))); a.clip = int(o.argumentos[1]);
      a.tiempo = reloj.delta; // ASM1F98E8..F4: inicializa al delta, no al último argumento.
    }
    return 1;
  }
};
} // namespace dbz3::escenarios::b2
