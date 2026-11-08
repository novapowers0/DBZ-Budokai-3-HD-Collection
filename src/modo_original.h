// Capa «modo original» (dia 1, O): reloj segun el ELF del jugador y arreglos medidos por cvar.
// Regla y sitios: studies 5/DECISIONES_DIA1.md (1a reloj, 1b interruptores). Prueba: python tools/reloj_equivalencia.py.
// Solo cabecera: el dia 1 CMakeLists es de E. ponytail: Actual()/Salta()/Censo()/PedirCombate() de 1d no estan; se
// anaden cuando tengan llamador (guardas EU de anim_iw ya van con iwmini::Activo()).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>

// Arreglos medidos y aprobados, separados por comas (vacio = ninguno; los pone la copia de pruebas en dbz3_user.toml).
REXCVAR_DECLARE(std::string, dbz3_original_on);   // definido en launcher/settings.cpp

namespace dbz3::pasarela {
std::filesystem::path BuscarIW();                                   // pasarela.h
std::vector<uint8_t> LeerElf(const std::filesystem::path& iw);      // pasarela_iw.h
}  // namespace dbz3::pasarela

namespace dbz3::original {
enum class Fuente : uint8_t { Ninguna, IwEu, IwUsa, B2Pal, B2Ntsc };

// ELF de IW del jugador por tamano: SLES_553.47 = 3786800 B (PAL); otro no vacio = SLUS_218.42 (como AbrirElfIW).
constexpr Fuente FuenteIwElf(size_t bytes) { return bytes == 3786800 ? Fuente::IwEu : bytes ? Fuente::IwUsa : Fuente::Ninguna; }
// ponytail: se lee una vez por proceso; si la copia de IW cambia en caliente, hay que refrescarla.
inline Fuente FuenteIw() {
  static const Fuente f = FuenteIwElf(pasarela::LeerElf(pasarela::BuscarIW()).size());
  return f;
}

// fRam0049c7dc del original: 1,2 con video PAL y 1,0 con NTSC. B2 NTSC esta bloqueado (la nave falla cerrada): 60 Hz.
constexpr int HzCampos(Fuente f) { return f == Fuente::IwEu || f == Fuente::B2Pal ? 50 : 60; }
constexpr float Delta(Fuente f) { return HzCampos(f) == 50 ? 1.2f : 1.0f; }
// n campos del ELF en pasos del port (60/s). Lo que el original ya multiplica por Delta NO pasa por aqui.
constexpr int CamposAPasos(int n, Fuente f) { return n * 60 / HzCampos(f); }
constexpr float CamposAPasos(float n, Fuente f) { return n * Delta(f); }   // = el x*1.2f de antes, bit a bit

static_assert(Delta(Fuente::IwEu) == 1.2f && Delta(Fuente::B2Pal) == 1.2f && Delta(Fuente::IwUsa) == 1.0f);
static_assert(CamposAPasos(105, Fuente::IwEu) == 126 && CamposAPasos(105, Fuente::IwUsa) == 105);
static_assert(CamposAPasos(25, Fuente::IwEu) == 25 * 6 / 5 && CamposAPasos(40.0f, Fuente::IwEu) == 40.0f * 1.2f);

// `token` esta en la lista (separada por comas, punto y coma o espacios).
constexpr bool Contiene(std::string_view lista, std::string_view token) {
  for (size_t i = 0; i <= lista.size();) {
    const size_t j = lista.find_first_of(", ;", i);
    const size_t fin = j == std::string_view::npos ? lista.size() : j;
    if (lista.substr(i, fin - i) == token) return true;
    i = fin + 1;
  }
  return false;
}
static_assert(Contiene("ik_b2, manos_toon", "manos_toon") && Contiene("ik_b2;pose_ya", "ik_b2") &&
              !Contiene("ik_b2x", "ik_b2") && !Contiene("", "ik_b2") && !Contiene("ik_b2", ""));
// true si `token` esta en dbz3_original_on. Tokens: studies 5/DECISIONES_DIA1.md 1b (todos apagados de serie).
inline bool Arreglo(std::string_view token) { return !token.empty() && Contiene(REXCVAR_GET(dbz3_original_on), token); }
}  // namespace dbz3::original
