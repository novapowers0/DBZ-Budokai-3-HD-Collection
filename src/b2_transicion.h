// Ejecutor común de transiciones B2. Guion/tareas/reloj reales, sin secuencias
// prefijadas. La adaptación de las órdenes al motor B3 es obligatoria y explícita.
#pragma once
#include "b2_vm.h"
#include "escenarios_b2_recursos.h"
#include <functional>
#include <memory>

namespace dbz3::escenarios::b2 {
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

  bool Iniciar(const Recurso& r, uint32_t e, Puente p) {
    if (r.version < 2) { error_ = "B2TZv2 requerido: MAD/MAS y banco de efectos correcto"; return false; }
    recurso_ = &r;
    entrada = e;
    puente = std::move(p);
    error_.clear();
    reloj.cuadro = 0;
    const Vista spx = r.Bloque(2);
    if (!vm.Cargar(std::vector<uint8_t>(spx.p, spx.p + spx.n), dbz3::b2vm::kTabla)) return false;
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
    for (auto& a : actores) {
      if (a.hueso < 0) continue;
      if (!recurso_ || a.clip < 0 || size_t(a.clip) >= recurso_->trayectoria.size()) { error_ = "trayectoria B2 inválida"; return false; }
      a.tiempo = std::min(a.tiempo + delta, float(recurso_->trayectoria[size_t(a.clip)].cuadros - 1));
    }
    if (!vm.Tick()) return false;
    if (camara >= 0 && recurso_ && size_t(camara) < recurso_->camara.size()) {
      std::array<std::array<float, 3>, 4> v;
      tiempo_camara = reloj.cuadro - inicio_camara;
      if (!Muestrear(recurso_->camara[size_t(camara)], tiempo_camara, v)) { error_ = "cámara B2 inválida"; return false; }
      ojo = v[0]; objetivo = v[1]; giro_camara = v[2][0]; fov = v[3][0];
    }
    reloj.cuadro += delta;
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
      camara = int(o.argumentos[4]); inicio_camara = reloj.cuadro;
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
