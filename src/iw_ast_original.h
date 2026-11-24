// dbz3 - PUL-IW-FX-06 / AST07: eventos AP4 (tecnica pedida por la animacion) como los consume el original.
// Cadena EU: Anim_ProcessEventTrack 0x23F520 (tipo 4, cola +0x1FC) -> Animations2 0x207180 -> Super_Ult_Ki_AttackEffects
// 0x1BF610 (paquete {actor,0}) -> Battle_DispatchEffectEvent 0x1BEC60:
//   id < 4 o id > 349 -> Battle_SpawnProjectilesForId 0x1D06F0: tabla #AST del jugador (filas 0xF0; +0x12 id, +0x10 tipo
//                        != 0xB, estado +0x16 exacto o rango +0xDC..+0xDE contra actor+0xB94) -> s_SPSHOT_DRAW.
//   id < 300           -> Battle_TriggerEffectsForEvent 0x1BC3A0: tabla #ASE del jugador (filas 0xD0; +0x6A id, estado +0xA2
//                        o rango +0xA8..+0xAA) -> generador Battle_SpawnEffectTableEntry 0x1BC0E0 (Effect_DispatchSpawnByType
//                        0x1F3740, 13 tipos por +0x70; ancla Effect_InitPositionAndOrientation 0x1F35C0 por el mismo +0x70)
//                        y reloj Effect_SpawnTimerObject 0x1F0C60 si +0x7C > 0 (duracion +0x84, 0 -> 100.0f).
//   resto              -> Battle_HandleEffectCommand 0x1BE300.
// Las tablas las carga s_SP_MAIN 0x1BF9C0 (Battle_LoadEffectTablesAll 0x1BC740 / LoadProjectileTablesBothPlayers 0x1D08A0)
// buscando la magia "#ASE" (ELF 0x493890) / "#AST" (ELF 0x493AC0) entre los archivos del paquete del jugador.
// Medido sin juego (DATA_CMN EU): solo 815-870 (luchadores) traen #ASE/#AST; los 20 actores de minijuego (913-932) no
// traen ninguna -> en los minijuegos el original no tiene entrada que lanzar y AP4 no dibuja nada.
// Solo diagnostico/decision; tras dbz3_iw_efectos_original (apagado de serie no se llama).
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dbz3::iwmini { bool EfectosOriginalIW(); }

namespace dbz3::iwast {

inline uint32_t L32(const std::vector<uint8_t>& b, size_t p) {
  if (p + 4 > b.size()) return 0;
  uint32_t v; std::memcpy(&v, b.data() + p, 4); return v;
}
inline int16_t L16(const std::vector<uint8_t>& b, size_t p) {
  if (p + 2 > b.size()) return 0;
  int16_t v; std::memcpy(&v, b.data() + p, 2); return v;
}
inline float LF(const std::vector<uint8_t>& b, size_t p) { const uint32_t u = L32(b, p); float f; std::memcpy(&f, &u, 4); return f; }

struct Tabla { std::vector<uint8_t> datos; size_t filas = 0, base = 0, paso = 0; };
// AFSfileCompactor(archivo, magia, 4): cabecera magia, +8 filas, +0xC desplazamiento de la primera.
// ponytail: busca la magia dentro de la entrada CMN entera (no recorre los hijos del paquete); "#ASE"/"#AST" no salen sueltos.
inline Tabla LeerTabla(const std::vector<uint8_t>& entrada, const char* magia, size_t paso) {
  Tabla t;
  for (size_t p = 0; p + 16 <= entrada.size(); p += 4) {
    if (std::memcmp(entrada.data() + p, magia, 4)) continue;
    const size_t n = L32(entrada, p + 8), o = L32(entrada, p + 12);
    if (n > 4096 || p + o + n * paso > entrada.size()) continue;
    t.datos.assign(entrada.begin() + long(p), entrada.end());
    t.filas = n; t.base = o; t.paso = paso;
    return t;
  }
  return t;
}

// Bandera de grupo del original (0x1BC3A0 / 0x1BC0E0): 0..99 -> 2, 100..199 -> 0x400, resto -> 0x10.
inline uint16_t Grupo(int id) { return id >= 0 && id < 100 ? 2 : id >= 100 && id < 200 ? 0x400 : 0x10; }

struct Accion {
  enum Que { Generar, MatarConHandle, Matar, Proyectil } que = Generar;
  int fila = 0; uint16_t grupo = 0;
  int16_t tipo70 = 0, modo76 = 0;   // generador/ancla (+0x70) y subcaso de orientacion (+0x76)
  bool genera = false, proyectil_afs = false, atr = false, sonido38 = false;
  int reloj7c = 0; float duracion = 0;
};

// Estado del actor: +0xB94 (estado de animacion de IW); contador 0x49BE88+jugador*0x640+0x3A (solo ids 0x10/0x30).
inline bool EstadoCasa(int16_t exacto, int16_t lo, int16_t hi, int estado) {
  return exacto == 0 ? (lo <= estado && estado <= hi) : exacto == estado;
}
// EU 0x1BC3A0 Battle_TriggerEffectsForEvent (+0x1BC0E0 para el "genera").
inline std::vector<Accion> Disparar(const Tabla& t, int id, int estado, int contador3a) {
  std::vector<Accion> out;
  for (size_t j = 0; j < t.filas; ++j) {
    const size_t e = t.base + j * t.paso;
    if (!EstadoCasa(L16(t.datos, e + 0xA2), L16(t.datos, e + 0xA8), L16(t.datos, e + 0xAA), estado)) continue;
    if (id == L16(t.datos, e + 0x6A)) {
      if ((id == 0x10 || id == 0x30) && contador3a < 1) continue;   // goto LAB_001bc680: tambien salta las bajas
      Accion a; a.fila = int(j); a.grupo = Grupo(id); a.sonido38 = id == 0x10 || id == 0x30;
      a.tipo70 = L16(t.datos, e + 0x70); a.modo76 = L16(t.datos, e + 0x76);
      const int16_t f5e = L16(t.datos, e + 0x5E), f60 = L16(t.datos, e + 0x60);
      a.genera = unsigned(a.tipo70 - 5) < 2 ? (f5e == 0 && f60 != 0) : f5e != 0;
      a.proyectil_afs = L16(t.datos, e + 0x64) != 0; a.atr = L16(t.datos, e + 0xAE) != 0;
      a.reloj7c = int32_t(L32(t.datos, e + 0x7C));
      a.duracion = LF(t.datos, e + 0x84) == 0.0f ? 100.0f : LF(t.datos, e + 0x84);
      out.push_back(a);
    }
    if (id == L16(t.datos, e + 0x6C)) { Accion a; a.que = Accion::MatarConHandle; a.fila = int(j); a.grupo = Grupo(id); out.push_back(a); }
    if (id == L16(t.datos, e + 0x6E)) { Accion a; a.que = Accion::Matar; a.fila = int(j); a.grupo = Grupo(id); out.push_back(a); }
  }
  return out;
}
// EU 0x1D06F0 Battle_SpawnProjectilesForId.
inline std::vector<Accion> Proyectiles(const Tabla& t, int id, int estado) {
  std::vector<Accion> out;
  for (size_t j = 0; j < t.filas; ++j) {
    const size_t e = t.base + j * t.paso;
    if (!EstadoCasa(L16(t.datos, e + 0x16), L16(t.datos, e + 0xDC), L16(t.datos, e + 0xDE), estado)) continue;
    if (id == L16(t.datos, e + 0x12) && L16(t.datos, e + 0x10) != 0xB) {
      Accion a; a.que = Accion::Proyectil; a.fila = int(j); a.tipo70 = L16(t.datos, e + 0x10); out.push_back(a);
    }
  }
  return out;
}

// Fila de GroundMinigameCharacterProperties (USA 0x473880, 40 B: 8 CMN + SE + boca) que contiene `cmn`; para un
// luchador (815-870) el paquete es la propia entrada. Mismo localizador que apev::BancoActor.
inline std::vector<uint32_t> PaqueteCmn(const std::vector<uint8_t>& elf, uint32_t cmn) {
  for (size_t p = 32; p + 17 * 40 <= elf.size(); p += 4) {
    if (L32(elf, p - 32) != 915 || L32(elf, p) != 922 || L32(elf, p + 40) != 921 || L32(elf, p + 80) != 920) continue;
    for (size_t i = 0; i < 17; ++i)
      for (size_t a = 0; a < 8; ++a)
        if (L32(elf, p + i * 40 + 4 * a) == cmn) {
          std::vector<uint32_t> v;
          for (size_t k = 0; k < 8; ++k) if (const uint32_t c = L32(elf, p + i * 40 + 4 * k); c && c < 2000) v.push_back(c);
          return v;
        }
    break;
  }
  return cmn ? std::vector<uint32_t>{cmn} : std::vector<uint32_t>{};
}

struct Resultado { bool silencio_original = false; const char* texto = ""; };
// AP4 de la animacion: decide como el original. Texto estable (cache) para el registro "pulfx06: AST07 ...".
// `leer(cmn)` devuelve la entrada de DATA_CMN. El estado +0xB94 del actor IW no existe en el cuerpo B3: se pasa -1 y,
// si la tabla tiene filas, se informa (no se inventa el estado).
inline Resultado Ap4(const std::vector<uint8_t>& elf, uint32_t actor_cmn, uint32_t valor,
                     const std::function<std::vector<uint8_t>(uint32_t)>& leer) {
  static std::map<uint64_t, std::pair<Tabla, Tabla>> cache;
  static std::map<std::string, std::string> textos;
  const int id = int16_t(uint16_t(valor));
  auto [it, nuevo] = cache.try_emplace(actor_cmn);
  if (nuevo)
    for (uint32_t c : PaqueteCmn(elf, actor_cmn)) {
      const auto e = leer(c);
      if (!it->second.first.filas) it->second.first = LeerTabla(e, "#ASE", 0xD0);
      if (!it->second.second.filas) it->second.second = LeerTabla(e, "#AST", 0xF0);
    }
  const auto& [ase, ast] = it->second;
  const char* ruta = id < 4 || id > 349 ? "proyectil(#AST)" : id < 300 ? "tabla(#ASE)" : "comando(1BE300)";
  const size_t filas = id < 4 || id > 349 ? ast.filas : id < 300 ? ase.filas : 0;
  std::string s = "pulfx06: AST07 CMN" + std::to_string(actor_cmn) + " id " + std::to_string(id) + " ruta " + ruta +
                  " filas " + std::to_string(filas);
  Resultado r;
  if (id >= 300 && id <= 349) s += ": comando 1BE300 sin portar";
  else if (!filas) { s += ": sin tabla en el paquete -> el original no genera nada"; r.silencio_original = true; }
  else s += ": tabla presente; estado +0xB94 de IW no disponible en el cuerpo B3, generador 1F3740 sin portar";
  r.texto = textos.try_emplace(s, s).first->second.c_str();
  return r;
}

// ---- AST02 (boca AP8), puente de indices: MouthAnimations 0x25FA90 copia 9 huesos cuyos indices salen de
// Char_SetAnimWithDefaultTable 0x25FA70 -> Model_ReadBodyPartIndices 0x262EB0 (9 nombres, tabla EU 0x458DE0 -> 0x4960F8).
// En el cuerpo B3 el nombre lleva prefijo (XKLL_M_JAW): misma regla de sufijo "_<nombre>" que AnclaImpactoAp, ambiguo -> -1.
// B3 trae 7 de los 9 (sin LCHEEK/RCHEEK): esos quedan -1 y el original salta las parejas con s16 < 0.
inline constexpr std::array<const char*, 9> kBocaIW = {"JAW", "UTEETH", "DTEETH", "LMOUTH1", "RMOUTH1",
                                                       "LMOUTH2", "RMOUTH2", "LCHEEK", "RCHEEK"};
inline std::array<int16_t, 9> IndicesBocaB3(const std::vector<std::string>& nombres) {
  std::array<int16_t, 9> out; out.fill(-1);
  for (size_t i = 0; i < 9; ++i) {
    const std::string b = std::string("_") + kBocaIW[i];
    int hallado = -1;
    for (size_t j = 0; j < nombres.size() && j < 32767; ++j)
      if (nombres[j].size() > b.size() && nombres[j].ends_with(b)) { if (hallado >= 0) { hallado = -2; break; } hallado = int(j); }
    out[i] = int16_t(hallado >= 0 ? hallado : -1);
  }
  return out;
}

// Registro de AP8 en juego: nombres del cuerpo B3 (cabecera #AWO +0x20/+0x24 o #AWG/#AMG +0x10/+0x1C, 32 B cada uno,
// como VolcarHuesos) -> indices. Texto estable por modelo. Lo que sigue sin portar se dice en el texto.
inline const char* Ap8Boca(uint32_t modelo, const std::function<uint32_t(uint32_t)>& u32,
                           const std::function<uint8_t(uint32_t)>& u8) {
  static std::map<uint32_t, std::string> textos;
  auto ok = [](uint32_t a, size_t n) { return a >= 0x10000000u && uint64_t(a) + n < 0xF0000000ull; };
  if (auto it = textos.find(modelo); it != textos.end()) return it->second.c_str();
  std::vector<std::string> nombres;
  const uint32_t h = ok(modelo, 0x46C) ? u32(modelo + 4) : 0;
  if (ok(h, 0x28)) {
    const uint32_t m = u32(h) & 0x00FFFFFFu; const bool awg = m == 0x00415747u || m == 0x00414D47u;
    const uint32_t n = u32(h + (awg ? 0x10 : 0x20)), rel = u32(h + (awg ? 0x1C : 0x24));
    const uint32_t tabla = rel && rel < 0x01000000u ? h + rel : rel;   // anim_iw.cpp Abs()
    if (n && n <= 512 && ok(tabla, size_t(n) * 32))
      for (uint32_t i = 0; i < n; ++i) {
        std::string s; for (uint32_t k = 0; k < 32; ++k) { const char c = char(u8(tabla + 32 * i + k)); if (!c) break; s.push_back(c); }
        nombres.push_back(s);
      }
  }
  const auto ix = IndicesBocaB3(nombres);
  std::string t = "pulfx06: AST02 boca B3 huesos " + std::to_string(nombres.size()) + " indices";
  int n = 0; for (size_t i = 0; i < 9; ++i) { t += std::string(" ") + kBocaIW[i] + "=" + std::to_string(ix[i]); n += ix[i] >= 0; }
  t += " (" + std::to_string(n) + "/9); pendiente: paleta #AMM de boca y paso local PS2(+0x45C,128 B)->mundo B3(+0x408,64 B)";
  return textos.emplace(modelo, t).first->second.c_str();
}

}  // namespace dbz3::iwast
