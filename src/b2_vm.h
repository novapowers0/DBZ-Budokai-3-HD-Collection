// VM común B2: misma ISA SPX que IW, con tabla/nativas y reloj de B2.
// Los handlers de iw_spx.h proceden asimismo de pasarela_b2.cpp::spx.
// No se registran las nativas IW ni se usan direcciones RAM de otro motor.
#pragma once
#include "iw_spx.h"

namespace dbz3::b2vm {
using Maquina = dbz3::iw::spx::Instancia;
using Tarea = dbz3::iw::spx::Hilo;
using Configuracion = dbz3::iw::spx::Configuracion;
using Host = dbz3::iw::spx::Host;
using Traza = dbz3::iw::spx::Traza;
using dbz3::iw::spx::Float;
using dbz3::iw::spx::Bits;
using dbz3::iw::spx::Signo;
inline constexpr uint32_t kTabla = 0x375190;

inline Configuracion Configurar() {
  Configuracion c;
  c.prioridad = 0x3500; // FUN_001DA1B0 crea la instancia con esta prioridad.
  // Estado original11469, instancia69FA40:32 ranuras, compartida/local128B,
  // temporal128B por tarea y pila256B. No heredar tamaños de los QTE de IW.
  c.ranuras = 32;
  c.bytes_compartida = c.bytes_locales = c.bytes_temporal = 0x80;
  c.bytes_pila = 0x100;
  return c;
}

// FUN_001E39B0 pone142FC a0; se incrementa SOLO en el paso de simulación.
// El dueño mantiene vivo este objeto mientras existan tareas de la instancia.
struct Reloj {
  float cuadro = 0, delta = 1;
  void Instalar(Maquina& m) {
    m.Registrar(kTabla, 4, [this](Maquina& vm, Tarea& h) {
      const int32_t objetivo = Signo(vm.Arg(h, 0));
      h.sondeo = [this, objetivo](Maquina&, Tarea& t) {
        if (cuadro + 0.5f < float(objetivo)) return 3u;
        t.sondeo = {};
        return 1u;
      };
      return 3u;
    });
    m.Registrar(kTabla, 9, [this](Maquina&, Tarea& h) { h.f[0] = Bits(delta); return 1u; });
  }
};
} // namespace dbz3::b2vm
