// Dragon Budokai Survivors, paquete P1 (combate y espectaculo): los ajustes de P1 y su estado de dibujo. Solo cabecera: sin archivo nuevo en CMake.
// Ajustes (apagados de serie, definidos en modo_mundo.cpp): dbz3_survivors_jugo (A01-A03: luchador legible, impacto, cifras),
// dbz3_survivors_fx_b3 (A04), dbz3_survivors_formas (A05), dbz3_survivors_ia (A06), dbz3_survivors_jefes_v2 (A07, A08).
// dbz3_survivors_meta_v2 (A09) lo define P2 en modo_pantallas.cpp (pantallas_p2.h).
// El coordinador llama a AjustesP1(&cfg) en Modo::Empezar justo antes de Juego::Iniciar (junto a cfg.meta_v2 = MetaV2()).
#pragma once

#include <rex/cvar.h>

#include "juego.h"

REXCVAR_DECLARE(bool, dbz3_survivors_jugo);   // definidos en modo_mundo.cpp
REXCVAR_DECLARE(bool, dbz3_survivors_fx_b3);
REXCVAR_DECLARE(bool, dbz3_survivors_formas);
REXCVAR_DECLARE(bool, dbz3_survivors_ia);
REXCVAR_DECLARE(bool, dbz3_survivors_jefes_v2);
REXCVAR_DECLARE(bool, dbz3_survivors_jefes_dos_fases);
REXCVAR_DECLARE(bool, dbz3_survivors_autoapuntado);

namespace dbz3::survivors::interno {

inline bool Jugo() { return REXCVAR_GET(dbz3_survivors_jugo); }
inline bool FxB3() { return REXCVAR_GET(dbz3_survivors_fx_b3); }
inline bool Formas() { return REXCVAR_GET(dbz3_survivors_formas); }
inline bool IaV2() { return REXCVAR_GET(dbz3_survivors_ia); }
inline bool JefesV2() { return REXCVAR_GET(dbz3_survivors_jefes_v2); }
// Opcion "sacudida si/no" (P2, pantalla Opciones): false = sin temblor de camara aunque dbz3_survivors_jugo este encendido.
inline bool& SacudidaActiva() {
  static bool v = true;
  return v;
}
// Pasa a la logica (juego.cpp, que no ve ImGui ni cvars) los ajustes que cambian la simulacion.
inline void AjustesP1(Config* c) {
  c->jugo = Jugo(), c->formas = Formas(), c->ia = IaV2(), c->jefes_v2 = JefesV2();
  c->jefes_dos_fases = REXCVAR_GET(dbz3_survivors_jefes_dos_fases);
}

}  // namespace dbz3::survivors::interno
