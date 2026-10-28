// dbz3 - Misiones del Dragon de Infinite World (ronda 3, frentes F y P1): la tabla de misiones del ELF de la copia del
// jugador (rival, escenario, tiempo y condicion de cada mision) y la pantalla de Dragon Mission con los graficos del
// juego (mapa del mundo, insignias, ficha y resultado), leidos en marcha; nada del juego va en la version. Docs:
// docs/16_pasarela_modos/03_infinite_world.md §2.
#include "pasarela_iw.h"
#include "anim_iw.h"
#include "guest_region.h"
#include "ingame/tutorial.h"
#include "iw_minijuegos.h"
#include "ps2_modelo.h"
#include "video_sfd.h"

#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>

#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

namespace dbz3::pasarela {
namespace ama = dbz3::anim::ama;
namespace {

constexpr int kMisiones = 96;    // Misiones del Dragon (despues, 105 de Fighter's Road)
constexpr int kRegistros = 201;
constexpr int kRegistro = 0x60;
// DS62: #AMA 334/2 (mapa, nubes y globo) y 334/4 (insignias de la historia) del DATA_USA del jugador. Se copian
// al cargar el mapa y los consume el motor ama::Pool; no se guardan en DragonMissionIW para no tocar el header.
std::vector<uint8_t> g_ama_mapa_datos;
std::vector<uint8_t> g_ama_historia_datos;

uint16_t U16(const std::vector<uint8_t>& b, size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); }
uint32_t U32(const std::vector<uint8_t>& b, size_t o) { return U16(b, o) | (uint32_t(U16(b, o + 2)) << 16); }

// EU 0x00120478 DragonMissionMinigameFlightVisibleItems: el decompilado le puso un nombre ajeno; su cuerpo es el
// formateador de texto del juego (buffer, limite, formato en .rodata y hasta 8 valores; llama a
// ProbablyRelatedToStagesInfinity EU 0x122A90, un printf sobre las VU, y escribe el 0 final). Devuelve la longitud
// como el original. En el port hace lo mismo con vsnprintf; lo usan los mismos textos que el original escribe por
// esta via (contador del mapa y TIME del resultado).
int FormatearTextoIW(char* dst, size_t n, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int largo = std::vsnprintf(dst, n, fmt, ap);
  va_end(ap);
  return largo;
}

// EU 0x2ACA10 GeneralCapsuleProperties: Capsules_SCDindexes, la tabla de 0x319 fichas de 16 B del bloque de datos del
// ELF (no esta en el AFS). Sin direccion fija: las 127 primeras filas llevan en +0x02 el ID de activacion 1..127 (USA
// SLUS_218.42 0x373BC0; PAL SLES_553.47 0x3743C0). DS135: +0x00 = ID de CAPSULA (rango del catalogo, 1..1141) y
// +0x02 = ID de ACTIVACION / indice SCD; la prueba es DragonMissionCapsulesCapsuleActivation/SkillMenuCapsuleSelection
// (EU 0x2AC9C0), que busca `param_1 == +0x02` y devuelve `+0x00`: con el campo 512 del registro de la mision devuelve
// 1 = Kamehameha (Goku), que es lo que equipa la mision 11. El byte +0x05 = nivel (1..3) que lee StoryModeLockedCapsules
// (EU 0x2ABEC0). Se usa para comprobar que las capsulas de las 96 misiones existen en el catalogo.
struct CapsulaSCD {
  int activacion = 0;   // +0x02: indice SCD/activacion (el que guarda el registro de mision)
  int id = 0;           // +0x00: ID de capsula del catalogo de IW
  int nivel = 0;        // byte +0x05
};
std::vector<CapsulaSCD> g_caps_iw;

std::vector<CapsulaSCD> LeerTablaCapsulasIW(const std::vector<uint8_t>& elf) {
  for (size_t p = 0; p + 16 * 127 <= elf.size(); p += 2) {
    if (U16(elf, p + 2) != 1 || U16(elf, p + 18) != 2 || U16(elf, p + 16 * 126 + 2) != 127) continue;
    bool ok = true;
    for (int i = 0; i < 127 && ok; ++i) ok = U16(elf, p + 16 * size_t(i) + 2) == uint16_t(i + 1);
    if (!ok) continue;
    std::vector<CapsulaSCD> out;
    out.reserve(0x319);
    for (int i = 0; i < 0x319 && p + 16 * size_t(i) + 8 <= elf.size(); ++i) {
      CapsulaSCD c;
      c.id = int(U16(elf, p + 16 * size_t(i)));
      c.activacion = int(U16(elf, p + 16 * size_t(i) + 2));
      c.nivel = int(elf[p + 16 * size_t(i) + 5]);
      out.push_back(c);
    }
    return out;
  }
  return {};
}

bool CapsulaEnTablaIW(int id) {
  if (id <= 0) return false;
  for (const CapsulaSCD& c : g_caps_iw)
    if (c.id == id) return true;
  return false;
}
// Textura (u16 de la primera clave) del objeto `objeto` de un #AMA: nodo +0x10 -> animacion, +8 -> pista 1 (la que
// usa w_Appear_FadeFlag_), +0x14 -> claves de 0x20 B, u16 +0 = textura. Es la misma lectura que ama2.py de R7. -1 si no.
int TexturaObjetoAma(const std::vector<uint8_t>& ama, int objeto) {
  if (ama.size() < 0x20 || std::memcmp(ama.data(), "#AMA", 4) != 0 || objeto < 0) return -1;
  const uint32_t n = U32(ama, 0x10), t = U32(ama, 0x14);
  if (uint32_t(objeto) >= n || size_t(t) + 4 * size_t(objeto) + 4 > ama.size()) return -1;
  const uint32_t nodo = U32(ama, t + 4 * size_t(objeto));
  if (size_t(nodo) + 0x14 > ama.size()) return -1;
  const uint32_t anim = U32(ama, nodo + 0x10);
  if (size_t(anim) + 0x10 > ama.size()) return -1;
  const uint32_t pista = U32(ama, anim + 8);
  if (size_t(pista) + 0x18 > ama.size()) return -1;
  const uint32_t claves = U32(ama, pista + 0x14);
  if (!U16(ama, pista + 4) || size_t(claves) + 2 > ama.size()) return -1;
  return U16(ama, claves);
}

// Texturas directas de 32 bits del #AMT (psm=0, sin CLUT; en 334/5 son t71 y t94, arte de las casillas de evento
// tipo 4 0x8DA/0x8DC). El lector comun de #AMT solo acepta PSMT4/PSMT8 y las dejaba vacias; mismo layout RGBA de
// PSMCT32 y alfa 0..0x80 como en el resto del formato.
Imagen Textura32Amt(const std::vector<uint8_t>& amt, int j) {
  Imagen img;
  if (amt.size() < 0x18 || std::memcmp(amt.data(), "#AMT", 4) != 0) return img;
  const uint32_t n = U32(amt, 0x10), idx = U32(amt, 0x14);
  if (j < 0 || uint32_t(j) >= n || size_t(idx) + 4 * uint32_t(j) + 4 > amt.size()) return img;
  const uint32_t e = U32(amt, idx + 4 * uint32_t(j));
  if (!e || size_t(e) + 0x2C > amt.size()) return img;
  const uint32_t psm = U32(amt, e + 0x08), w = U16(amt, e + 0x10), h = U16(amt, e + 0x12);
  const uint32_t data = U32(amt, e + 0x14) + 0x20, dsize = U32(amt, e + 0x18);
  if (psm != 0 || !w || !h || w > 2048 || h > 2048 || dsize < w * h * 4 || size_t(data) + size_t(w) * h * 4 > amt.size())
    return img;
  img.w = int(w);
  img.h = int(h);
  img.rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const uint8_t* c = &amt[data + 4 * i];
    img.rgba[4 * i + 0] = c[0];
    img.rgba[4 * i + 1] = c[1];
    img.rgba[4 * i + 2] = c[2];
    img.rgba[4 * i + 3] = uint8_t(std::min<int>(255, int(c[3]) * 2));
  }
  return img;
}

std::vector<uint8_t> Leer(std::ifstream& f, uint64_t off, uint32_t n) {
  std::vector<uint8_t> b(n);
  f.clear();
  f.seekg(std::streamoff(off));
  f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
  b.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
  return b;
}

}  // namespace

// ELF del juego (SLUS_*/SLES_* en la raiz): de la carpeta extraida o de la ISO (ISO 9660, directorio raiz).
std::vector<uint8_t> LeerElf(const std::filesystem::path& iw) {
  auto es_elf = [](std::string n) {
    for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    return n.size() >= 11 && (n.rfind("SLUS_", 0) == 0 || n.rfind("SLES_", 0) == 0) && n[8] == '.';
  };
  std::error_code ec;
  if (std::filesystem::is_directory(iw, ec)) {
    for (const auto& e : std::filesystem::directory_iterator(iw, ec)) {
      if (!es_elf(e.path().filename().string())) continue;
      std::ifstream f(e.path(), std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    }
    return {};
  }
  std::ifstream f(iw, std::ios::binary);
  const std::vector<uint8_t> pvd = Leer(f, 16 * 2048, 2048);
  if (pvd.size() < 2048 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return {};
  const std::vector<uint8_t> raiz = Leer(f, uint64_t(U32(pvd, 156 + 2)) * 2048, std::min<uint32_t>(U32(pvd, 156 + 10), 1 << 20));
  for (size_t o = 0; o + 33 < raiz.size();) {
    const uint8_t len = raiz[o];
    if (len == 0) {   // el resto del sector va vacio
      o = (o / 2048 + 1) * 2048;
      continue;
    }
    const uint8_t nl = raiz[o + 32];
    if (o + 33 + nl <= raiz.size() && es_elf(std::string(reinterpret_cast<const char*>(&raiz[o + 33]), nl)))
      return Leer(f, uint64_t(U32(raiz, o + 2)) * 2048, std::min<uint32_t>(U32(raiz, o + 10), 64 << 20));
    o += len;
  }
  return {};
}

namespace {
// Indice SCD -> capsula (EU 0x2AC9C0 DragonMissionCapsulesCapsuleActivation/SkillMenuCapsuleSelection, tabla
// Capsules_SCDindexes en EU 0x474340 / USA 0x473B40: 0x319 registros de 16 B, +0 = ID de capsula, +2 = indice SCD).
// Los 8 huecos de tecnica de cada luchador del registro de mision (+0x1E/+0x20/+0x22 y +0x2C..+0x34, y en el rival
// +0x44/+0x46/+0x48 y +0x52..+0x5A) guardan INDICES SCD, no IDs de capsula: la funcion del menu de Skills los pasa
// por esta tabla para obtener la capsula equipada (DragonMissionSkillEditScreen, EU 0x2FAD10). La firma (700, 1) con
// +4 = 256 y +6 = 1 es unica en los dos ELF (EU fichero 0x3743C0, USA 0x373BC0).
std::unordered_map<int, int> TablaSCDCapsula(const std::vector<uint8_t>& elf) {
  std::unordered_map<int, int> m;
  size_t base = 0;
  for (size_t p = 0; !base && p + 0x319 * 16 <= elf.size(); p += 2) {
    if (U16(elf, p) == 700 && U16(elf, p + 2) == 1 && U16(elf, p + 4) == 256 && U16(elf, p + 6) == 1 &&
        U16(elf, p + 16) == 701 && U16(elf, p + 18) == 2 && U16(elf, p + 20) == 512 && U16(elf, p + 22) == 1)
      base = p;
  }
  for (size_t i = 0; base && i < 0x319; ++i) {
    const int scd = U16(elf, base + i * 16 + 2), caps = U16(elf, base + i * 16);
    if (!m.count(scd)) m.emplace(scd, caps);
  }
  return m;
}
// Los 8 huecos por luchador (en orden de registro) de la mision; los rellena TablaMisionesIW. Con g_tec_mu.
std::mutex g_tec_mu;
std::vector<std::array<int, 8>> g_tecnicas_iw;
// La tabla: registros de 0x60 B numerados 1, 2, 3... (USA: fichero 0x376D50).
std::vector<MisionTablaIW> TablaMisionesIW(const std::vector<uint8_t>& elf, std::string& error) {
  std::vector<MisionTablaIW> out;
  size_t base = 0;
  for (size_t p = 0; !base && p + size_t(kRegistro) * kMisiones <= elf.size(); p += 2) {
    int n = 1;
    while (n <= kMisiones && U16(elf, p + size_t(kRegistro) * (n - 1)) == n) ++n;
    if (n > kMisiones) base = p;
  }
  if (!base) {
    error = "tabla de misiones no encontrada en el ELF";
    return out;
  }
  const std::unordered_map<int, int> scd2cap = TablaSCDCapsula(elf);
  std::vector<std::array<int, 8>> tecnicas;
  for (int n = 1; n <= kRegistros && base + size_t(kRegistro) * size_t(n) <= elf.size() &&
                  U16(elf, base + size_t(kRegistro) * (n - 1)) == n; ++n) {   // 1-96 y Fighter's Road 97-201
    const size_t r = base + size_t(kRegistro) * (n - 1);
    MisionTablaIW m;
    m.numero = n;
    m.condicion = int16_t(U16(elf, r + 0x02));
    m.escenario = U16(elf, r + 0x08);
    const int t = U16(elf, r + 0x0C);
    // Tiempo de la mision: +0x0C es un indice de la tabla de tiempos, NO minutos. RE/medido 2026-10-25: el texto de
    // DETAILS de la mision 21 (tabla 0x4774D0, +0x0C = 5) dice "The time limit is 120 seconds!" en la copia original.
    // Reparto de los 201 registros del ELF USA (0x376D50, paso 0x60): 1 -> 3 (las 3 Endure de 60 s de Dragon Mission),
    // 3 -> 10 (los 10 entrenamientos de 180 s), 5 -> 15 (los 15 combates "Fight" de 120 s), 4 -> 173 (67 K.O. y
    // "dejar ganar" de Dragon Mission SIN limite + los 105 de Fighter's Road); el indice 2 no aparece.
    m.tiempo = t == 1 ? 60 : t == 3 ? 180 : t == 5 ? 120 : -1;
    m.premio = U16(elf, r + 0x0E);
    m.jugador = U16(elf, r + 0x14);
    m.forma1 = U16(elf, r + 0x16);
    m.rival = U16(elf, r + 0x3A);
    m.forma2 = U16(elf, r + 0x3C);
    m.nivel = U16(elf, r + 0x04);
    m.notas = U16(elf, r + 0x06);
    // Tecnicas equipadas del JUGADOR (DS135): 8 indices SCD en +0x1E/+0x20/+0x22 y +0x2C/+0x2E/+0x30/+0x32/+0x34.
    // Se convierten a ID de capsula con la tabla de EU 0x474340. NO son recompensas (Battle_ApplyResultRewards,
    // EU 0x28A020, no lee estos campos): son la lista que carga el Skill Edit de la mision (EU 0x2FAD10) y con la
    // que pelea el jugador. Los tres primeros van tambien a `caps` (compatibilidad con el recuento de DS134).
    std::array<int, 8> tech{};
    static const int kSlot[8] = {0x1E, 0x20, 0x22, 0x2C, 0x2E, 0x30, 0x32, 0x34};
    int primera = 0;
    for (int k = 0; k < 8; ++k) {
      const int scd = U16(elf, r + size_t(kSlot[k]));
      if (scd == 0 || scd == 0xFFFF) continue;
      const auto it = scd2cap.find(scd);
      if (it == scd2cap.end()) continue;   // SCD sin ficha: no se inventa la capsula
      tech[size_t(k)] = it->second;
      if (primera < 3) m.caps[primera++] = tech[size_t(k)];
    }
    tecnicas.push_back(tech);
    out.push_back(m);
  }
  {
    std::lock_guard<std::mutex> lk(g_tec_mu);
    g_tecnicas_iw = std::move(tecnicas);
  }
  return out;
}
}  // namespace

std::vector<MisionTablaIW> LeerTablaIW(const std::filesystem::path& iw, std::string& error) {
  const std::vector<uint8_t> elf = LeerElf(iw);
  if (elf.empty()) {
    error = "sin ELF de Infinite World en " + iw.string();
    return {};
  }
  return TablaMisionesIW(elf, error);
}

// IDs de IW = los de B3 hasta el 43 (Goku 0 ... Saibaman 42); del 44 en adelante son de IW (GT, peliculas, fusiones).
// Del 44 en adelante: el port instalado con el Mod Kit (importar.py iw; clave = "c:" + nombre del modelo de IW en
// minusculas, mod center hd/iw_modelos.json). Las fusiones de IW (64 Gotenks, 70 Gogeta SS4, 74 Vegito, 79 Bu con
// Gohan) son en B3 personajes internos con el mismo numero (Gotenks 64, Vegito 74: los que pone la fusion en combate;
// probados: misiones 67 y 69) o formas de Super Bu (35; tabla de transformaciones de B3, docs/07_ports/GOHAN_FUTURO_
// EXPLORACION_2026-10-06/04_transformaciones/evid_tabla_b3.md: forma 1 = modelo 84 Piccolo, 2 = 78 Gotenks, 3 = 79
// Gohan). El Bu con Gohan de IW no se pide como forma 3: en B3 la forma 3 solo se monta con la transformacion en pleno
// combate y arrancar ya en ella cuelga al cargar (medido 2026-10-16, D:/DBZ3HD/_pruebas/20261016-031905_modos_test_
// iw_mapa: sale el Super Bu de serie), asi que se queda en el Super Bu base (forma 0) y no se pide la 3. Gogeta SS4 (70)
// sigue en gris.
int IWaB3(int id_iw) {
  if (id_iw >= 0 && id_iw <= 43 && id_iw != 1) return id_iw;
  // Fusiones nativas (s7 frente 06): Gotenks 64 y Gogeta SS4 70 son registros de B3 con su forma de fusion; Vegito de
  // Goku (IW 74) es el registro 72 de B3 (el 74 esta vacio en el char372: nf = 0). Sin el flag, comportamiento antiguo.
  if (id_iw == 64) return 64;
  if (id_iw == 74) return kFusionesNativas ? 72 : 74;
  if (id_iw == 70 && kFusionesNativas) return 70;
  // Gogeta SS4 (IW 70), R7: el registro 70 de B3 se llama "GOGITA SS4 (GOKOU)", pero pedido con forma 0 sale Goku
  // normal (D:/DBZ3HD/_pruebas/20261017-171216_iw_fix_gogeta77): la fusion de B3 se hace en pleno combate (fusion.cpp,
  // tag::Fuse). Ronda studies 5: se porta como personaje propio (receta imp_iw_gogeta_ss4, modelo y moveset de IW) y se
  // mapea aqui; sin el port instalado la mision 77 sigue en gris con el aviso de FaltaIW.
  if (id_iw == 78 || id_iw == 79 || id_iw == 84) return 35;
  static const std::pair<int, const char*> kPorts[] = {{44, "c:goku (gt)"}, {45, "c:super baby vegeta 2"},
                                                       {46, "c:super 17"},   {47, "c:janemba"},
                                                       {48, "c:pikkon"},     {54, "c:vegeta (gt)"},
                                                       {55, "c:great saiyaman 2"}, {56, "c:pan"},
                                                       {70, "c:gogeta"}};   // en el disco de IW se llama "Gogeta" (2 formas)
  for (const auto& [iw, clave] : kPorts) {
    if (iw == id_iw) return IdPort("iw", clave);
  }
  return -1;
}

// Tabla de escenarios del ELF de IW (0x34A430, 0x5C B: BUDO, MIND & TIME, ISLAND, TOWN, ROCK, PLANE, GOKU, NAMEC,
// CELL, KAIOU, BUU BODY, ISLAND B, TOWN B, (13: infierno de las peliculas), NAMEC B, KAIOU B, R&R BASE) -> mapa de B3.
int MapaIW(int escenario) {
  constexpr int k[] = {46, 58, 56, 68, 64, 62, 52, 60, 50, 54, 48, 56, 66, 54, 60, 54, 44};
  return escenario >= 0 && escenario < int(std::size(k)) ? k[escenario] : 62;
}

// Forma con la que empieza la fusion de IW (0 = normal). Para la 67 (Gotenks), la 69 (Vegito) y la 77 (Gogeta SS4).
int FormaFusionIW(int id_iw) {
  if (!kFusionesNativas) return 0;
  switch (id_iw) {
    case 64: return 3;   // Gotenks: char372 de B3 id 64 forma 3, capsula 150 "Fusion <Gotenks> (Goten)"
    case 70: return 4;   // Gogeta SS4: id 70 forma 4, capsula 175 "Fusion <SS4 Gogeta> (Goku)"
    case 74: return 2;   // Vegito: id 72 forma 2, capsula 187 "Potara <Vegito> (Goku)"
    default: return 0;
  }
}

// Tecnicas equipadas del jugador en una mision (1..201): IDs de capsula IW ya convertidos desde el indice SCD.
// Devuelve cuantas hay (hasta `max`); 0 si la mision no esta leida. DS135.
int TecnicasMisionIW(int mision, int* salida, int max) {
  if (!salida || max <= 0) return 0;
  std::lock_guard<std::mutex> lk(g_tec_mu);
  if (mision < 1 || size_t(mision) > g_tecnicas_iw.size()) return 0;
  int n = 0;
  for (const int v : g_tecnicas_iw[size_t(mision - 1)])
    if (v > 0 && n < max) salida[n++] = v;
  return n;
}

// Variante de modelo del registro de mision (+0x16 jugador, +0x3C rival) -> FORMA inicial de B3.
// EU 0x2281D0 DragonMissionBattleCharacterProperties guarda ese campo en el luchador como FORMA (+0x2580) y el ID en
// +0x257C. NO es la forma de char372 de B3: IW tiene por personaje una tabla de variantes de modelo de 0x54 B
// (0x3d0000; 14 huecos de 6 B en +0x10) que mezcla transformaciones y trajes. Los trajes son los otros campos
// (+0x18/+0x1A jugador, +0x3E/+0x40 rival): DragonMissionBattleCharacterDataBlocks (EU 0x229F20) los usa como indice
// de la tabla de 8 trajes (+0x78, {id, forma, color}). Prueba de que +0x16/+0x3C es la variante de modelo: la mision
// 30 («Goku con scouter») trae rival 0 Goku forma 3 y traje 0, y el modelo 4 de Goku en All_Character_Slots es
// "Goku Scouter (4)". Las 21 misiones con rival forma != 0 son 1,2,3,4,23,24,26,27,28,30,31,32,50,51,55,64,88,89,
// 90,93,96 (45 misiones contando las del jugador). Copiar el valor a `forma` de B3 es imposible: alli 0..nf son solo
// transformaciones de char372 y el numero no significa lo mismo (Piccolo 2 = Fusion con Kami, Goku 4 = SSJ3, A18 1 =
// modelo de A17...). La tabla de IW se monta en RAM (0x3d0000) y no se ha localizado en el ELF ni en DATA_CMN, asi
// que el nombre de cada variante es [H]: el equivalente seguro es la forma de serie (0).
int VarianteB3IW(int id_iw, int variante) {
  (void)id_iw;
  (void)variante;
  return 0;
}

// ---- Dragon Mission (frente P1, 2026-10-16) ----
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

// I03 (studies 4): el vuelo 0x7D6/0x7E0 ya esta validado con el xex europeo y la ISO europea (0x7D6 y 0x7E0
// PASS en EU). Ya no hay motivo especial para EU; solo queda el aviso generico de los modulos sin portar.
const char* MotivoMinijuegoIW(int clave) {
  (void)clave;
  return T::T("Minijuego de Infinite World: aun no disponible.", "Infinite World minigame: not available yet.");
}

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kSelect = 0x0020,
                   kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000;
// Texturas por indice (hojas de E:/DBZ3HD_trabajo/estudio_ports/iw_assets/png): 334/5 iconos, 334/1 interfaz, 333/0.
constexpr int kNew = 16, kAro = 89, kGokuMapa = 90;
constexpr int kPanel = 0, kMarco = 3, kFlecha = 8, kDifCar = 13, kMissionStart = 14, kBestScore = 15, kDificultades = 16,
              kEtiquetas = 17, kCifrasG = 19, kCifrasB = 22, kRangos = 24, kEstrella = 31;
constexpr int kZeniTxt = 7, kGlobo = 18, kEarned = 19, kTotal = 20;   // 333/0: EARNED!, ZENI y TOTAL
constexpr int kInterrogante = 0, kEsfera = 6;   // 334/5: "?" en su peana (minijuego), esfera de cristal
// 334/3 (ademas de los mapas): HUD del mapa. Medido en E:/DBZ3HD_trabajo/P1/h334_3.png y medir.py.
constexpr int kRobot = 0, kGloboMapa = 1, kFlechas = 17, kCifrasRosa = 18, kBarraBolas = 19, kBolas = 20, kClear = 69;
constexpr int kNombreSaga[6] = {2, 26, 36, 40, 42, 43};   // "Saiyan Saga" ... "Another Story" (letras rojas)
// DATA_USA 65 (#AMT de 32 texturas, 600x450): la sala de guerreros / Skill Edit del original. Indices medidos con
// amt_ps2.py sobre la copia del jugador (USR/DATA_USA.AFS) y cotejados con las capturas del menu de IW.
constexpr int kSalaFondo = 0, kSalaDragon = 1, kSalaTitulo = 2, kSalaDialogo = 3, kSalaGuerrero = 4,
              kSalaCapsulaAmarilla = 5, kSalaMarco = 6, kSalaPanelOscuro = 7, kSalaCapsulaRoja = 8, kSalaEtiquetas = 9,
              kSalaMando = 10, kSalaP1P2 = 11, kSalaPatron = 12, kSalaTienda = 13, kSalaDialogo2 = 14, kSalaVentana = 15,
              kSalaCategorias = 16, kSalaBarra = 17, kSalaFrascos = 18, kSalaCifras = 19, kSalaNuevo = 20,
              kSalaFlecha = 21, kSalaMarco2 = 22, kSalaPanel2 = 23, kSalaSiNo = 24, kSalaPergamino = 25,
              kSalaZeni = 26, kSalaCifras2 = 27, kSalaShenron = 28, kSalaAros = 29, kSalaShenronGrande = 30,
              kSalaSkillEdit = 31;
// DATA_USA 370 (#AMT de 55 texturas, 600x450): menu de opciones del original. El rotulo de t23 (184x192) lleva seis
// lineas de 32 px; la cuarta (y 96..128) es "Brightness". t27 (336x120) es el deslizador con su marco.
constexpr int kOptFondo = 19, kOptEtiquetas = 23, kOptDeslizador = 27, kOptMarco = 14, kOptMando = 11,
              kOptTitulo = 47;
constexpr float kOptLineaAlto = 32.0f;   // alto de cada linea del rotulo t23 (184x192 / 6)
constexpr int kOptLineaBrillo = 3;       // "Brightness" es la cuarta linea (0 Speaker, 1 Music, 2 Voice/SFX, 3 Brightness)
// Bulma de la guia en cada saga: textos 13/30/38/39/41/44 de 334/3 (el atlas del mapa). RE 2026-10-25:
// BulmaSprite_SagaTitleNames (EU 0x2FF7C0) pinta DOS objetos de 334/2 por saga, el primero con la animacion de Bulma y el
// segundo con el titulo: 0xC/0x38, 0x44/0x48, 0x50/0x54, 0x58/0x5C, 0x60/0x64 y 0x68/0x6C. El segundo de cada par da en
// 334/3 justo kNombreSaga (2, 26, 36, 40, 42, 43) y el primero, por tanto, la Bulma (13, 30, 38, 39, 41, 44; la clave 0
// de la pista de cuadros +8 del objeto en el #AMA 334/2). Las de antes eran indices de objeto, no de textura.
constexpr int kBulma[6] = {13, 30, 38, 39, 41, 44};
// Scale2x (EPX): dobla un sprite de pixel art sin emborronarlo.
Imagen Escala2x(const Imagen& s) {
  Imagen d;
  if (!s) return d;
  d.w = s.w * 2;
  d.h = s.h * 2;
  d.rgba.resize(size_t(d.w) * size_t(d.h) * 4);
  auto px = [&](int x, int y) {
    uint32_t v;
    std::memcpy(&v, &s.rgba[(size_t(std::clamp(y, 0, s.h - 1)) * size_t(s.w) + size_t(std::clamp(x, 0, s.w - 1))) * 4], 4);
    return v;
  };
  auto put = [&](int x, int y, uint32_t v) { std::memcpy(&d.rgba[(size_t(y) * size_t(d.w) + size_t(x)) * 4], &v, 4); };
  for (int y = 0; y < s.h; ++y) {
    for (int x = 0; x < s.w; ++x) {
      const uint32_t p = px(x, y), a = px(x, y - 1), b = px(x + 1, y), c = px(x - 1, y), e = px(x, y + 1);
      put(2 * x, 2 * y, c == a && c != e && a != b ? a : p);
      put(2 * x + 1, 2 * y, a == b && a != c && b != e ? b : p);
      put(2 * x, 2 * y + 1, e == c && e != b && c != a ? c : p);
      put(2 * x + 1, 2 * y + 1, b == e && b != a && e != c ? e : p);
    }
  }
  return d;
}
bool (*g_minijuego)(int clave, int fila) = nullptr;   // frente P4
int g_mini_clave = 0;   // R7: el ultimo minijuego empezado desde el mapa (su musica)
// Esferas de cristal del mapa (#DMD tipo 2, claves 0x850-0x865, +0x1C = indice de escena 0..21). En IW, al recoger
// una, GeneralMapScripts (EU 0x2FE270) marca 0xC0 en su DragonMissionStoryModeBattlePointCapsules_Moderation, ejecuta
// ExecuteScript_MMI2, copia el registro (CopyRecordToEngineBuffer) y transiciona a 0x11 con el +0x1C. DS104: 0x11/0x12
// SI son la escena de cine (Scene_GotoCutscenes -> DragonMissionSelectionLinkFunc -> Movie_OpenContext, EU 0x3393B0),
// que usa ese indice directo sobre los 22 nombres de 0x481960 + i*0x100; el SFD se reproduce y la esfera se marca al
// acabar. El port guarda la mascara de 22 bits en `iw_esferas`; el guion MMI2 no esta en el C ([H]). Cache de 0,25 s:
// leer el progreso abre el fichero.
int g_esferas_mask = -1;
float g_esferas_t = -1.0e9f;
int MascaraEsferasIW(float t) {
  if (g_esferas_mask < 0 || t - g_esferas_t > 0.25f) {
    g_esferas_t = t;
    g_esferas_mask = LeerProgreso("iw_esferas", 0);
  }
  return g_esferas_mask;
}
bool EsferaRecogidaIW(int indice, float t) {
  return indice >= 0 && indice < 22 && ((MascaraEsferasIW(t) >> indice) & 1) != 0;
}
void MarcarEsferaIW(int indice, float t) {
  if (indice < 0 || indice >= 22) return;
  const int mascara = MascaraEsferasIW(t) | (1 << indice);
  GuardarProgreso("iw_esferas", mascara);
  g_esferas_mask = mascara;
  g_esferas_t = t;
}
bool g_pose_resultado = false;   // vuelta de combate: Goku con la animacion 8 hasta la recarga (101 cuadros)
bool g_salida_saga = false;      // cambio de saga (EU 0x2FDF80): Goku con la animacion 10 (tabla 0x498A70)
// Insignia "VS" (textura de 334/5) de cada valor +0x10 del #DMD (30..87). R7 quinta tanda: leida del #AMA 334/4 (sus
// texturas son las de 334/5): el objeto 40 + 2 (v - 30) lleva en la clave 0 de su pista de cuadros (registro +8: +4
// cuantas claves, +0x14 claves de 0x20 B, u16 +0 = textura; SprAnim_SampleFrameTrack_ EU L150910) la insignia; los
// pares 40-155 dan justo 58 (los 30-37 coinciden con lo medido a ojo antes). E:\DBZ3HD_trabajo\R7\iw\s5/ama2.py.
// Cierre 2026-10-25 (datos, no la llamada): el #AMA 334/4 tiene 190 objetos y los 40,42,...,154 son 58 insignias
// seguidas; el #DMD de DATA_CMN 1093 usa 53 valores distintos dentro de 30..87 en sus 96 misiones (faltan 44, 67, 78,
// 79 y 81, que son de otras casillas), asi que el bloque de 58 cubre el rango entero y el orden es el del propio
// recurso: al cargar se leen del #AMA (TexturaObjetoAma) y se comprueba que coinciden 58/58 con esta tabla, que queda
// solo de respaldo.
constexpr int kInsignia[58] = {
    4,  9,  10, 11, 12, 83, 84, 81, 74, 24, 25, 26, 27, 28, 29, 31, 32, 33, 34, 35,    // 30..49
    36, 37, 38, 39, 40, 49, 50, 51, 52, 53, 54, 55, 56, 57, 62, 61, 63, 64, 65, 66,    // 50..69
    67, 68, 69, 75, 82, 87, 85, 86, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104};      // 70..87
struct TextoSaga {
  const char *es, *en;
};
constexpr TextoSaga kSagas[6] = {{"Saga de los Saiyans", "Saiyan Saga"}, {"Saga de Freezer", "Frieza Saga"},
                                 {"Saga de Cell", "Cell Saga"},          {"Saga de Bu", "Buu Saga"},
                                 {"Saga de GT", "GT Saga"},              {"Otra historia", "Another Story"}};

const Imagen& De(const std::vector<Imagen>& v, int k) {
  static const Imagen nada;
  return k >= 0 && k < int(v.size()) ? v[size_t(k)] : nada;
}

// ---- DS62: conexion del motor de sprites #AMA de IW con el mapa (ama::Pool) ----
// El #AMA 334/2 es el juego de sprites del mapa (objeto 0 = nubes, 1/61/113/76 = las cuatro losetas del mapa,
// 16 = globo de la guia) y el 334/4 el de las insignias de la historia (objetos 40+2*(cara-30)). Se cargan una vez
// y se dejan en un Pool; los objetos los pinta el mismo codigo que el original (Fondo/Pases).
struct AmaMapaEstado {
  ama::Obra obra;
  ama::Pool pool;
  bool obra_ok = false;
  bool creado = false;
  int saga = -1;
  int mapa = -1, nubes = -1, globo = -1;
};
struct AmaHistEstado {
  ama::Obra obra;
  ama::Pool pool;
  bool obra_ok = false;
  int saga = -1;
  std::vector<int> sprite;   // indice en el Pool por casilla; -1 = aun no creada
  std::vector<int> evento;   // indice en el Pool por casilla de evento tipo 3/4; -1 = aun no creada
};
AmaMapaEstado g_ama_mapa;
AmaHistEstado g_ama_hist;
bool g_ama_cargada = false;

bool CargarAma() {
  if (g_ama_cargada) return g_ama_mapa.obra_ok;
  g_ama_cargada = true;
  if (!g_ama_mapa_datos.empty()) g_ama_mapa.obra_ok = g_ama_mapa.obra.Cargar(g_ama_mapa_datos);
  if (!g_ama_historia_datos.empty()) g_ama_hist.obra_ok = g_ama_hist.obra.Cargar(g_ama_historia_datos);
  REXLOG_INFO("dbz3 iw mapa: #AMA 334/2 {} ({} objetos, {} nodos) / #AMA 334/4 {} ({} objetos, {} nodos) para ama::Pool",
              g_ama_mapa.obra_ok ? "OK" : "no", g_ama_mapa.obra.NumObjetos(), g_ama_mapa.obra.NumNodos(),
              g_ama_hist.obra_ok ? "OK" : "no", g_ama_hist.obra.NumObjetos(), g_ama_hist.obra.NumNodos());
  return g_ama_mapa.obra_ok;
}

// Crea una raiz del Pool para el objeto `objeto` del #AMA. t0 = cuadro inicial (para que una insignia que ya deberia
// estar a medias de su aparicion entre por donde toca).
int CrearSpriteAma(ama::Obra& obra, ama::Pool& pool, int objeto, uint32_t mascara, float t0 = 0.0f) {
  const int nd = obra.Objeto(uint32_t(objeto));
  if (nd < 0) return -1;
  const ama::Arbol a = ama::CrearArbol(obra, nd, t0);
  if (a.raiz < 0) return -1;
  return pool.Crear(a, objeto, -1, -1, mascara, 1);
}

// Sprites del mapa y de sus nubes (los dos viven en el mismo Pool y se pintan por mascaras, como en 0x2F95A0:
// mascara 1 las losetas del mapa, mascara 2 las nubes; el globo va en la mascara 4 del HUD).
bool AsegurarAmaMapa(int m) {
  if (!CargarAma() || !g_ama_mapa.obra_ok) return false;
  m = std::clamp(m, 0, 3);
  if (g_ama_mapa.creado && g_ama_mapa.saga == m) return true;
  static constexpr int kMapa[4] = {1, 61, 113, 76};     // Tierra, Namek, GT sepia, espacio
  static constexpr int kNubes[4] = {0, 60, 112, 120};   // el mismo juego de 6 nubes en cada mapa
  g_ama_mapa.pool = ama::Pool{};
  g_ama_mapa.saga = m;
  g_ama_mapa.creado = true;
  g_ama_mapa.mapa = CrearSpriteAma(g_ama_mapa.obra, g_ama_mapa.pool, kMapa[m], 1);
  g_ama_mapa.nubes = CrearSpriteAma(g_ama_mapa.obra, g_ama_mapa.pool, kNubes[m], 2);
  g_ama_mapa.globo = CrearSpriteAma(g_ama_mapa.obra, g_ama_mapa.pool, 16, 4);
  return true;
}

// Insignias de la historia: un Pool por mapa abierto (se rehace al cambiar de saga). `n` = casillas del #DMD.
bool AsegurarAmaHistoria(int saga, size_t n) {
  if (!CargarAma() || !g_ama_hist.obra_ok) return false;
  if (g_ama_hist.saga == saga && g_ama_hist.sprite.size() == n) return true;
  g_ama_hist.pool = ama::Pool{};
  g_ama_hist.sprite.assign(n, -1);
  g_ama_hist.evento.clear();
  g_ama_hist.saga = saga;
  return true;
}

// Casillas de evento tipo 3/4: mismo banco de sprites que las insignias (#AMA 334/4). El #DMD +0x10 es la fila de la
// tabla del ELF que Story_SpawnSpriteNode (EU 0x31B770) pasa a MiscDisplay(1, A, B, -1, 0): tipo 3 = 0x480750 en el
// decomp EU (copia USA: fichero 0x380020) y tipo 4 = 0x480780 EU (USA 0x380050). Las 5 filas 100..104 dan el par
// (aparicion, reposo) 172..181 y las 8 filas 110..117 dan 156..171 del mismo #AMA; no hay que inventar iconos.
int ObjetoEventoIW(const CasillaDM& e) {
  if (e.tipo == 3 && e.cara >= 100 && e.cara <= 104) return 172 + 2 * (e.cara - 100);
  if (e.tipo == 4 && e.cara >= 110 && e.cara <= 117) return 156 + 2 * (e.cara - 110);
  return -1;
}

void AsegurarAmaEventos(size_t n) {
  if (g_ama_hist.evento.size() != n) g_ama_hist.evento.assign(n, -1);
}

void ReiniciarAmaMapa() {
  g_ama_mapa.pool = ama::Pool{};
  g_ama_mapa.creado = false;
  g_ama_mapa.saga = -1;
  g_ama_mapa.mapa = g_ama_mapa.nubes = g_ama_mapa.globo = -1;
  g_ama_hist.pool = ama::Pool{};
  g_ama_hist.sprite.clear();
  g_ama_hist.evento.clear();
  g_ama_hist.saga = -1;
}
// Encaja w x h en una caja de cw x ch (sin pasar de `max` veces su tamano) centrada en (x, y) px de 1200x900.
// Caja de los pixeles visibles (alfa > 0) de una imagen, en px (x0, y0, x1, y1): los titulos de IW llevan mucho margen.
ImVec4 Recorte(const Imagen& im) {
  int x0 = im.w, y0 = im.h, x1 = 0, y1 = 0;
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x)
      if (im.rgba[(size_t(y) * size_t(im.w) + size_t(x)) * 4 + 3]) {
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
      }
  return x1 > x0 ? ImVec4(float(x0), float(y0), float(x1), float(y1)) : ImVec4(0, 0, float(im.w), float(im.h));
}
void Encajar(float w, float h, float x, float y, float cw, float ch, float max, ImVec2& p0, ImVec2& p1) {
  const float k = std::min({cw / std::max(w, 1.0f), ch / std::max(h, 1.0f), max});
  p0 = ImVec2(x - w * k * 0.5f, y - h * k * 0.5f);
  p1 = ImVec2(x + w * k * 0.5f, y + h * k * 0.5f);
}

}  // namespace

// Musica del minijuego (R7): LoadWaitPlayerFlyingMinigame toca Audio_GetFilteredTableEntry(0x6b, 3) = fila 0x12 de la
// tabla EU 0x4803B0 (USA 0x47FB50: 63..71, 49..52, 72) -> ADX_CMN 66; las demas no tienen indice leido (-1: silencio).
int PistaMinijuegoIW() { return g_mini_clave == 0x7D6 || g_mini_clave == 0x7E0 ? 66 : -1; }

// EU 0x298380 MinigameAudioTriggerFilter: tabla de claves de disparo del audio de los minijuegos. El original la
// aplica al arrancar el minijuego para elegir su entrada de audio; el port la deja expuesta al frente P4.
int FiltroSonidoMinijuego(int clave) {
  switch (clave) {
    case 0x6C: return 0x13;
    case 0x6B: return 0x12;
    case 0x4D: return 0x11;
    case 0x39: return 0x10;
    case 99:   // 0x63
    case 0x21: return 0xF;
    case 0x5F:
    case 0x5E:
    case 0x1B: return 0xE;
    case 0x0F: return 0xD;
    case 0x0E: return 0xC;
    case 0x0D: return 0xB;
    case 0x0C: return 10;
    case 0x0B: return 9;
    case 10:   return 8;
    case 9:    return 7;
    case 8:    return 6;
    case 7:    return 5;
    case 6:    return 4;
    case 4:    return 3;
    case 3:    return 2;
    case 2:    return 1;
    default:   return 0;   // 5, 0 y 1 no tienen disparador
  }
}
int MapaDeSaga(int saga) {
  constexpr int k[6] = {0, 1, 0, 0, 2, 3};   // Tierra, Namek, Tierra, Tierra, GT sepia, Otra historia (espacio)
  return saga >= 0 && saga < 6 ? k[saga] : 0;
}

void RegistrarMinijuegoIW(bool (*empezar)(int clave, int fila)) { g_minijuego = empezar; }

namespace {
constexpr int kVideos = 22;          // FD01A, FD01B, FD02..FD07, FD08A, FD08B, FD09..FD20
constexpr int kSubtitulos = 77;      // DATA_USA 77 + video (tabla del ELF USA 0x4827B0: 77..98)
// Las dos tablas de escenas del ELF (antes y, 0x60 B despues, despues; ver EscenaIW): se buscan por su forma (16
// registros {tipo 0/1, indice, video} de 16 bits acabados en -1, y entre las dos, cada video 0..21 una vez), asi vale
// cualquier version del ELF.
std::vector<EscenaIW> LeerEscenas(const std::vector<uint8_t>& elf) {
  auto tabla = [&](size_t p, bool despues, std::vector<EscenaIW>& v, uint32_t& vistos) {
    for (int i = 0; i < 16; ++i) {
      const size_t r = p + 6 * size_t(i);
      const int tipo = int16_t(U16(elf, r)), indice = int16_t(U16(elf, r + 2)), video = int16_t(U16(elf, r + 4));
      if (tipo == -1) return i > 0;
      if (tipo < 0 || tipo > 1 || indice < 0 || indice > 200 || video < 0 || video >= kVideos || ((vistos >> video) & 1))
        return false;
      vistos |= 1u << video;
      v.push_back({tipo, indice, video, despues});
    }
    return false;
  };
  for (size_t p = 0; p + 0xC0 <= elf.size(); p += 2) {
    std::vector<EscenaIW> v;
    uint32_t vistos = 0;
    if (tabla(p, false, v, vistos) && tabla(p + 0x60, true, v, vistos) && vistos == (1u << kVideos) - 1) return v;
  }
  return {};
}
}  // namespace

namespace {
std::filesystem::path ResolverRutaSinCase(const std::filesystem::path& raiz, const std::filesystem::path& rel) {
  std::error_code ec;
  std::filesystem::path actual = raiz;
  for (const auto& componente : rel) {
    if (componente == "." || componente.empty()) continue;
    const auto directa = actual / componente;
    if (std::filesystem::exists(directa, ec)) {
      actual = directa;
      continue;
    }
    if (!std::filesystem::is_directory(actual, ec)) return {};
    const std::string buscada = componente.string();
    bool encontrada = false;
    for (const auto& entrada : std::filesystem::directory_iterator(actual, ec)) {
      std::string nombre = entrada.path().filename().string();
      if (nombre.size() != buscada.size()) continue;
      const bool igual = std::equal(nombre.begin(), nombre.end(), buscada.begin(), [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
      });
      if (igual) {
        actual = entrada.path();
        encontrada = true;
        break;
      }
    }
    if (!encontrada || ec) return {};
  }
  return actual;
}

// Busca `rel` dentro de una carpeta extraida: tal cual (raiz/USR/FDxx) y un nivel o dos mas abajo (raiz/<juego>/USR/
// FDxx, como queda al extraer una ISO en su propia carpeta). Sin recorrer ficheros: solo directorios, para no tardar.
std::filesystem::path EnCarpetaIW(const std::filesystem::path& raiz, const std::filesystem::path& rel) {
  std::error_code ec;
  const auto directo = ResolverRutaSinCase(raiz, rel);
  if (!directo.empty() && std::filesystem::is_regular_file(directo, ec)) return directo;
  if (!std::filesystem::is_directory(raiz, ec)) return {};
  for (const auto& a : std::filesystem::directory_iterator(raiz, ec)) {
    if (!a.is_directory(ec)) continue;
    const auto un_nivel = ResolverRutaSinCase(a.path(), rel);
    if (!un_nivel.empty() && std::filesystem::is_regular_file(un_nivel, ec)) return un_nivel;
    for (const auto& b : std::filesystem::directory_iterator(a.path(), ec)) {
      if (b.is_directory(ec)) {
        const auto dos_niveles = ResolverRutaSinCase(b.path(), rel);
        if (!dos_niveles.empty() && std::filesystem::is_regular_file(dos_niveles, ec)) return dos_niveles;
      }
    }
  }
  return {};
}
// ISO 9660 de solo lectura para extraer un SFD. `partes` = componentes de la ruta (p. ej. {"USR","FD01A.SFD"}),
// sin distinguir mayusculas ni el ";1". Devuelve el LBA y el tamano del fichero.
bool BuscarEnIso(std::ifstream& f, const std::vector<std::string>& partes, uint32_t& lba, uint32_t& tam) {
  auto sector = [&](uint64_t off, uint32_t n) {
    std::vector<uint8_t> b(n);
    f.clear();
    f.seekg(std::streamoff(off));
    f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
    b.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
    return b;
  };
  const std::vector<uint8_t> pvd = sector(16ull * 2048, 2048);
  if (pvd.size() < 190 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return false;
  lba = U32(pvd, 156 + 2);
  tam = U32(pvd, 156 + 10);
  for (const std::string& parte : partes) {
    const std::vector<uint8_t> dir = sector(uint64_t(lba) * 2048, ((tam + 2047) / 2048) * 2048);
    std::string quiero = parte;
    for (char& c : quiero) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (const size_t p = quiero.find(';'); p != std::string::npos) quiero.resize(p);
    bool hallado = false;
    for (size_t o = 0; o < dir.size();) {
      const uint8_t len = dir[o];
      if (len == 0) {
        o = (o / 2048 + 1) * 2048;
        continue;
      }
      if (o + 33 > dir.size()) break;
      const uint32_t l = U32(dir, o + 2), s = U32(dir, o + 10);
      const uint8_t nl = dir[o + 32];
      std::string n(reinterpret_cast<const char*>(&dir[o + 33]), std::min<size_t>(nl, dir.size() - o - 33));
      o += len;
      if (const size_t p = n.find(';'); p != std::string::npos) n.resize(p);
      for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
      if (n != quiero) continue;
      lba = l;
      tam = s;
      hallado = true;
      break;
    }
    if (!hallado) return false;
  }
  return true;
}
// El reproductor de SFD (video_sfd.h) necesita un fichero: con una ISO se extrae el SFD a la cache de datos del
// usuario (una vez; si ya esta con su tamano, se reutiliza). No toca la ISO ni borra nada.
std::filesystem::path ExtraerSfdDeIso(const std::filesystem::path& iso, const std::filesystem::path& rel) {
  std::ifstream f(iso, std::ios::binary);
  if (!f) return {};
  uint32_t lba = 0, tam = 0;
  const std::string nombre = rel.filename().string();
  if (!BuscarEnIso(f, {"usr", nombre}, lba, tam) || !tam || tam > (512u << 20)) return {};
  std::error_code ec;
  const auto dir = dbz3::settings::UserDataRoot() / "iw_video_cache";
  std::filesystem::create_directories(dir, ec);
  const auto dst = dir / (iso.filename().string() + "_" + nombre);
  if (std::filesystem::is_regular_file(dst, ec) && std::filesystem::file_size(dst, ec) == tam) return dst;
  auto parcial = dst;
  parcial += ".parcial";
  std::ofstream out(parcial, std::ios::binary | std::ios::trunc);
  if (!out) return {};
  for (uint64_t off = 0; off < tam;) {
    const uint32_t n = uint32_t(std::min<uint64_t>(4u << 20, uint64_t(tam) - off));
    const std::vector<uint8_t> b = [&] {
      std::vector<uint8_t> v(n);
      f.clear();
      f.seekg(std::streamoff(uint64_t(lba) * 2048 + off));
      f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n));
      v.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
      return v;
    }();
    if (b.empty() || !out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()))) return {};
    off += b.size();
  }
  out.close();
  if (std::filesystem::exists(dst, ec)) std::filesystem::remove(dst, ec);   // cache incompleta de otra version
  std::filesystem::rename(parcial, dst, ec);
  if (ec) return {};
  REXLOG_INFO("dbz3 misiones: iw: video {} extraido de la ISO a {}", nombre, dst.string());
  return dst;
}
}  // namespace

std::filesystem::path RutaVideoIW(const DragonMissionIW& dm, int video) {
  if (video < 0 || video >= int(dm.videos.size()) || dm.raiz.empty()) return {};
  std::error_code ec;
  const auto rel = std::filesystem::path(dm.videos[size_t(video)]).relative_path();
  // 1) carpeta extraida (la habitual): la ruta tal cual o con uno/dos niveles de mas.
  if (std::filesystem::is_directory(dm.raiz, ec)) {
    if (const auto p = EnCarpetaIW(dm.raiz, rel); !p.empty()) return p;
  } else {
    // 2) ISO: el SFD no es un fichero del sistema de ficheros. Se acepta una carpeta extraida al lado con el mismo
    // nombre que la ISO (convencion de extraccion), la carpeta USR hermana y DBZ3_IW_VIDEO_DIR (pruebas).
    const auto padre = dm.raiz.parent_path();
    for (const auto& cand : {padre / dm.raiz.stem(), padre / "USR", padre / dm.raiz.stem() / "USR"}) {
      if (const auto p = EnCarpetaIW(cand, rel); !p.empty()) return p;
    }
    if (const char* dir = std::getenv("DBZ3_IW_VIDEO_DIR")) {
      if (const auto p = EnCarpetaIW(std::filesystem::u8path(dir), rel); !p.empty()) return p;
    }
    // 3) ISO sin carpeta al lado: se extrae el SFD a la cache de datos del usuario (el reproductor necesita fichero).
    if (const auto p = ExtraerSfdDeIso(dm.raiz, rel); !p.empty()) return p;
    REXLOG_INFO("dbz3 misiones: iw: la ISO {} no trae el video {} como fichero: se usan los subtitulos",
                dm.raiz.filename().string(), rel.filename().string());
  }
  return {};
}

const std::vector<CasillaDM>& Cas(const InfoDM& in) { return in.road ? in.dm->casillas_road : in.dm->casillas; }

namespace {
// DS104: la tabla de videos de minijuego que leen EU 0x289C20 y EU 0x20D800, medida en el ELF EU (SLES_553.47,
// fichero 0x3730B0 / 0x373110; USA SLUS_218.42, 0x3728B0 / 0x372910; VA = fichero + 0xFFF80):
//   antes  (EU 0x473030): (0,0,0) y 10 misiones; tabla de 16 registros {tipo,indice,video} de 16 bits acabada en -1.
//   despues(EU 0x473090): (0,10,9) y las otras 10 misiones.
// Solo hay dos filas de tipo 0 (minijuego): indice 0 -> video 0 (FD01A, antes) e indice 10 -> video 9 (FD08B,
// despues). Los otros 16 indices (1..9, 11..18) no tienen fila: el original no reproduce SFD en ellos. Las 22
// escenas estan asignadas exactamente una vez entre las dos tablas y las 10 misiones con pareja previa/final; no
// queda ninguna libre para los otros 16 minijuegos. Sus previews de ~8-16 s son escenas del motor, no SFD.
constexpr int kMiniPre[19] = {0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr int kMiniPost[19] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9,  -1, -1, -1, -1, -1, -1, -1, -1};

// DS104: las mismas dos tablas tienen las 10 misiones con pareja previa/final (indice = mision - 1; EU 0x473030
// antes y EU 0x473090 despues). Se usan si el escaneo por forma de LeerEscenas no encuentra las tablas (copia rara),
// para que la ficha de las 96 siga pidiendo el SFD que el original pide (misiones 11, 23, 37, 38, 49, 54, 63, 65,
// 70 y 77).
constexpr int kMisionPre[96] = {
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1,  3, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1,  5,  7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                10, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, 14, -1,
                                16, -1, -1, -1, -1, 18, -1, -1, -1, -1, -1, -1, 20, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};
constexpr int kMisionPost[96] = {
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  2, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1,  4, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1,  6,  8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                11, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1,
                                17, -1, -1, -1, -1, 19, -1, -1, -1, -1, -1, -1, 21, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

int VideoDeMiniFijo(int indice, bool despues) {
  if (indice < 0 || indice > 18) return -1;
  return despues ? kMiniPost[indice] : kMiniPre[indice];
}

int VideoDeMisionFijo(int indice, bool despues) {
  if (indice < 0 || indice >= int(std::size(kMisionPre))) return -1;
  return despues ? kMisionPost[indice] : kMisionPre[indice];
}
}  // namespace

int IndiceMinijuegoIW(int clave) {
  if (clave < 0x7D0 || clave > 0x7E1) return -1;
  const int i = clave - 0x7D0;
  return i >= 4 ? i + 1 : i;   // 0x7D4 es el indice 5: el indice 4 no tiene casilla en el #DMD
}

int VideoEscenaIW(const DragonMissionIW& dm, int indice) {
  // Movie_OpenContext (EU 0x3393B0) usa uRam0049cbec (el parametro de Transition_SetupWithParams(0x11/0x12,...)) como
  // indice de la tabla de nombres 0x481960 + indice*0x100. Las 22 marcas tipo 2 del #DMD 1093 llevan ese indice en
  // +0x1C (0..21), sin tabla intermedia: el SFD es videos[+0x1C].
  return indice >= 0 && indice < int(dm.videos.size()) ? indice : -1;
}

int VideoIW(const DragonMissionIW& dm, int tipo, int indice, bool despues) {
  // El video no depende de los subtitulos: el original lo reproduce siempre que la tabla lo defina y el SFD exista.
  // Antes se exigia un juego de subtitulos no vacio y, si faltaba DATA_USA 77 en la copia, no salia ningun video.
  for (const EscenaIW& e : dm.escenas) {
    if (e.tipo == tipo && e.indice == indice && e.despues == despues && e.video >= 0 &&
        e.video < int(dm.videos.size()))
      return e.video;
  }
  // DS104: si el escaneo de las tablas del ELF fallo (copia rara), las filas medidas de minijuego (0x7D0 previo y
  // 0x7D9 final) y las 10 parejas de mision siguen valiendo.
  if (tipo == 0) return VideoDeMiniFijo(indice, despues);
  if (tipo == 1) return VideoDeMisionFijo(indice, despues);
  return -1;
}

// EU 0x289C20 LoadWaitDragonMissionMinigame_Battle: recorre las dos tablas de escenas buscando (tipo, indice). El
// tipo 0 es minijuego y el indice es el +0x1C de su casilla del #DMD; para los minijuegos solo hay dos entradas en el
// ELF: antes (EU 0x473030) minijuego indice 0 -> video 0 (FD01A) y despues (EU 0x473090) minijuego indice 10 ->
// video 9 (FD08B). DS104: las 18 claves se resuelven por la clave (0x7D0..0x7E1) aunque la casilla no este cargada,
// con el indice del #DMD (0x7D4 = indice 5). -1 = el original no reproduce SFD en ese minijuego.
VideosMinijuegoIW VideosDeMinijuegoIW(const DragonMissionIW& dm, int clave) {
  int indice = -1;
  for (const CasillaDM& c : dm.casillas)
    if (c.tipo == 0 && c.clave == clave) indice = c.indice;
  if (indice < 0) indice = IndiceMinijuegoIW(clave);
  if (indice < 0) return {};
  VideosMinijuegoIW v;
  v.previo = VideoIW(dm, 0, indice, false);
  v.final = VideoIW(dm, 0, indice, true);
  return v;
}

int VideoMinijuegoIW(const DragonMissionIW& dm, int clave, bool despues) {
  const VideosMinijuegoIW v = VideosDeMinijuegoIW(dm, clave);
  return despues ? v.final : v.previo;
}

// EU 0x001FC660 Audio_PickRandomVoiceVariant: el indice se acota a 0..0x10; cada fila son 2 bytes y hay dos bloques
// de 0x22 (masculino y femenino) en la tabla del ELF (EU VA 0x4948A0; USA 0x4940A0, mismos bytes). Cuenta hasta 4
// variantes validas (byte >= 0) y devuelve una al azar; con 0 devuelve -1 y con 1, esa. El original usa Memory()
// (aqui std::rand). La tabla se localiza por su firma REAL (la de 0x4948A0; la firma vieja caia 0x80 B antes, en la
// tabla de capsulas de 0x494820, y devolvia valores de otra tabla: por eso la variante elegida no sonaba).
int ElegirVarianteVozIW(int id, bool femenina) {
  id = std::clamp(id, 0, 0x10);
  static std::vector<uint8_t> tabla;
  static bool cargada = false;
  if (!cargada) {
    cargada = true;
    const std::vector<uint8_t> elf = LeerElf(BuscarIW());
    static const uint8_t kFirma[22] = {0x13, 0xFF, 0x15, 0xFF, 0x02, 0xFF, 0x09, 0xFF, 0x01, 0xFF, 0x00,
                                       0xFF, 0x05, 0xFF, 0x04, 0xFF, 0x03, 0xFF, 0x0F, 0xFF, 0x08, 0xFF};
    if (elf.size() > 0x100) {
      const auto it = std::search(elf.begin(), elf.end(), std::begin(kFirma), std::end(kFirma));
      if (it != elf.end() && size_t(std::distance(elf.begin(), it)) + 0x44 <= elf.size())
        tabla.assign(it, it + 0x44);
    }
    REXLOG_INFO("dbz3 iw: tabla de variantes de voz (EU 0x4948A0): {}", tabla.empty() ? "no encontrada" : "2x17 filas");
  }
  if (tabla.size() < 0x44) return -1;
  const int8_t* p = reinterpret_cast<const int8_t*>(tabla.data()) + id * 2 + (femenina ? 0x22 : 0);
  int n = 0;
  while (n < 4 && p[n] >= 0) ++n;
  if (n < 1) return -1;
  if (n == 1) return p[0];
  return p[std::rand() % n];
}

// EU 0x001FCE00 AudioFadeMainFunc / EU 0x001FCAE0 s_ADX_FADE: +0x7c6 (la "variante" de Audio_PickRandomVoiceVariant)
// NO es una voz: indexa la tabla de temas de batalla del ELF (puntero en EU 0x44E3EC -> array EU 0x44E120, USA
// 0x44D920; 40 entradas de 32 bits, 0xFFFFFFFF = sin tema). s_ADX_FADE recibe ese numero como pista de ADX_CMN.
int PistaTemaBatallaIW(int variante) {
  static std::vector<uint32_t> pistas;
  static bool cargada = false;
  if (!cargada) {
    cargada = true;
    const std::vector<uint8_t> elf = LeerElf(BuscarIW());
    static const uint8_t kFirma[84] = {
        34, 0, 0, 0, 35, 0, 0, 0, 36, 0, 0, 0, 37, 0, 0, 0, 38, 0, 0, 0, 39, 0, 0, 0, 40, 0, 0, 0, 41, 0, 0, 0,
        42, 0, 0, 0, 43, 0, 0, 0, 79, 0, 0, 0, 28, 0, 0, 0, 29, 0, 0, 0, 30, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,
        31, 0, 0, 0, 32, 0, 0, 0, 33, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 49, 0, 0, 0, 50, 0, 0, 0};
    if (elf.size() > 0x100) {
      const auto it = std::search(elf.begin(), elf.end(), std::begin(kFirma), std::end(kFirma));
      if (it != elf.end() && size_t(std::distance(elf.begin(), it)) + 160 <= elf.size()) {
        pistas.resize(40);
        std::memcpy(pistas.data(), &*it, 160);
      }
    }
    REXLOG_INFO("dbz3 iw: tabla de temas de batalla (EU 0x44E120): {}", pistas.empty() ? "no encontrada" : "40 pistas");
  }
  if (variante < 0 || variante >= int(pistas.size())) return -1;
  const uint32_t p = pistas[size_t(variante)];
  return p == 0xFFFFFFFFu ? -1 : int(p);
}

// El alta del combate de mision (EU 0x20E860 -> 0x20E630) sortea la variante (EU 0x1FC660) y guarda en +0x7c6 el
// indice de la lista de temas de batalla; AudioFadeMainFunc (EU 0x1FCE00) lo convierte en la pista de ADX_CMN.
// El port guarda la pista elegida para que Lanzar() la reproduzca durante el combate (B3 no trae musica de pelea).
static int g_tema_batalla_iw = -1;
int ElegirTemaBatallaIW(int id, bool femenina) {
  const int variante = ElegirVarianteVozIW(id, femenina);
  const int pista = PistaTemaBatallaIW(variante);
  g_tema_batalla_iw = pista;
  REXLOG_INFO("dbz3 iw ficha: variante {} del jugador {} (EU 0x1FC660) -> tema de batalla ADX_CMN {} (EU 0x1FCE00)",
              variante, id, pista);
  return pista;
}
int TemaBatallaIW() { return g_tema_batalla_iw; }

// EU 0x0018B138 s_E1122604_mwPlyPause_handle_is_in: pausa/reanuda el reproductor Sofdec. El original exige que este
// abierto (FUN_0018cd28 == 1), comprueba el byte de estado +0x82, es idempotente y al cambiar actualiza los dos
// canales (+0x518/+0x53c) y el byte. Aqui el estado es el de la escena de la historia y se aplica al reproductor real
// (video_sfd.cpp: congela imagen, audio y subtitulos), que es lo que hace FUN_001a1e30 en el original.
bool PausaPeliculaIW(bool pausar) {
  static bool pausado = false;
  if (pausado == pausar) return false;
  pausado = pausar;
  const bool real = dbz3::video::Pausar(pausar);
  REXLOG_INFO("dbz3 iw historia: mwPlyPause {} (EU 0x18B138; byte +0x82 = {}, reproductor {})", pausar ? "pausa" : "reanuda",
              pausar ? 1 : 0, real ? "pausado" : "sin SFD");
  return true;
}

// Cobertura de la historia (DS76): recorre las 6 sagas y sus 96 misiones del #DMD + las 105 de Fighter's Road y
// comprueba, con los datos del ELF/AFS, que cada una tiene ficha, combate (personajes mapeados o motivo), escena si
// la tabla la define, premio de capsula presente en Capsules_SCDindexes y, por saga, esferas y eventos de desbloqueo
// con su sprite del #AMA. Solo registra el recuento; no cambia el estado.
void RegistrarCoberturaIW(const DragonMissionIW& dm, const std::vector<MisionTablaIW>& tabla) {
  if (tabla.empty()) {
    REXLOG_WARN("dbz3 iw cobertura: sin tabla de misiones del ELF; no se puede comprobar la historia");
    return;
  }
  std::map<int, const CasillaDM*> por_mision;
  int minis = 0, minis_sin_titulo = 0, esferas = 0, esferas_ok = 0, eventos = 0, eventos_ok = 0;
  for (const CasillaDM& c : dm.casillas) {
    if (c.mision > 0) por_mision[c.mision] = &c;
    if (c.tipo == 0) {
      ++minis;
      const int im = c.clave - 0x7D0;
      if (im < 0 || im >= int(dm.titulos_mini.size()) || !dm.titulos_mini[size_t(im)]) ++minis_sin_titulo;
    }
    if (c.tipo == 2) {
      ++esferas;
      if (c.indice >= 0 && c.indice < 22) ++esferas_ok;
    }
  }
  for (const CasillaDM& e : dm.eventos) {
    ++eventos;
    if (ObjetoEventoIW(e) >= 0) ++eventos_ok;
  }
  int sin_ficha = 0, sin_casilla = 0, sin_caps = 0, caps = 0, caps_fuera = 0, sin_pj = 0, con_escena = 0;
  std::string faltan_casilla, faltan_caps;
  for (int n = 1; n <= kMisiones; ++n) {
    if (n > int(tabla.size())) {
      ++sin_ficha;
      continue;
    }
    const MisionTablaIW& m = tabla[size_t(n - 1)];
    if (!por_mision.count(n)) {
      ++sin_casilla;
      faltan_casilla += (faltan_casilla.empty() ? "" : ",") + std::to_string(n);
    }
    bool tiene_caps = false;
    for (int k = 0; k < 3; ++k) {
      if (m.caps[k] <= 0) continue;
      tiene_caps = true;
      ++caps;
      if (!CapsulaEnTablaIW(m.caps[k])) {
        ++caps_fuera;
        faltan_caps += (faltan_caps.empty() ? "" : ",") + std::to_string(m.caps[k]);
      }
    }
    if (!tiene_caps) ++sin_caps;
    if (IWaB3(m.jugador) < 0 || IWaB3(m.rival) < 0) ++sin_pj;
    if (VideoIW(dm, 1, n - 1, false) >= 0 || VideoIW(dm, 1, n - 1, true) >= 0) ++con_escena;
  }
  int road = 0, road_eventos = 0, road_eventos_ok = 0, road_sin_pj = 0;
  for (const CasillaDM& c : dm.casillas_road)
    if (c.mision > 0) ++road;
  for (const CasillaDM& e : dm.eventos_road) {
    ++road_eventos;
    if (ObjetoEventoIW(e) >= 0) ++road_eventos_ok;
  }
  for (const MisionTablaIW& m : tabla)
    if (m.numero > kMisiones && IWaB3(m.rival) < 0) ++road_sin_pj;
  int por_saga[6] = {};
  for (const CasillaDM& c : dm.casillas)
    if (c.mision > 0 && c.saga >= 0 && c.saga < 6) ++por_saga[c.saga];
  const int caps_filas = int(g_caps_iw.size());
  REXLOG_INFO("dbz3 iw cobertura por saga (Saiyan/Freezer/Cell/Bu/GT/Otra): {}/{}/{}/{}/{}/{} misiones", por_saga[0],
              por_saga[1], por_saga[2], por_saga[3], por_saga[4], por_saga[5]);
  REXLOG_INFO("dbz3 iw cobertura: {} registros de mision ({} DM + {} Road); DM: {} casillas con ficha, {} sin casilla ({}), "
              "{} con escena SFD, premios {} capsulas ({} fuera de Capsules_SCDindexes [{}], {} sin capsula por diseno), "
              "{} misiones con personaje sin mapear; {} minijuegos ({} sin titulo), {} esferas ({} indices validos; se "
              "marcan en iw_esferas), {} eventos tipo 3/4 ({} con sprite #AMA); Road: {} casillas, {} eventos ({} con "
              "sprite), {} rivales sin mapear; Capsules_SCDindexes: {} filas",
              int(tabla.size()), kMisiones, int(tabla.size()) - kMisiones, int(por_mision.size()), sin_casilla,
              faltan_casilla, con_escena, caps, caps_fuera, faltan_caps, sin_caps, sin_pj, minis, minis_sin_titulo, esferas,
              esferas_ok, eventos, eventos_ok, road, road_eventos, road_eventos_ok, road_sin_pj, caps_filas);
  if (sin_ficha || sin_casilla || caps_fuera)
    REXLOG_WARN("dbz3 iw cobertura: faltan fichas ({}) o casillas ({}) o capsulas del premio ({})", sin_ficha, sin_casilla,
                caps_fuera);
}

bool LeerDragonMission(const std::filesystem::path& iw, DragonMissionIW& out, std::string& error) {
  // EU 0x300CC0 SkillEdit_InitScreen: el alta del mapa de Dragon Mission (reserva el contexto, abre las carpetas CMN,
  // copia la posicion del Goku y fija el modo). Aqui: el paquete del mapa de DATA_CMN 1093, sus graficos y sus videos.
  out = DragonMissionIW{};
  const std::vector<uint8_t> elf = LeerElf(iw);
  g_caps_iw = LeerTablaCapsulasIW(elf);
  REXLOG_INFO("dbz3 misiones: iw: Capsules_SCDindexes {} filas (GeneralCapsuleProperties EU 0x2ACA10)", g_caps_iw.size());
  AfsPs2 usa, cmn;
  if (!usa.Abrir(iw, "DATA_USA.AFS") || !cmn.Abrir(iw, "DATA_CMN.AFS")) {
    error = "sin DATA_USA.AFS o DATA_CMN.AFS en " + iw.string();
    return false;
  }
  auto todas = [](const std::vector<uint8_t>& amt) {
    std::vector<Imagen> v;
    for (int j = 0, n = CuantasAmt(amt); j < n; ++j) v.push_back(TexturaAmt(amt, j));
    return v;
  };
  const std::vector<uint8_t> b334 = usa.Entrada(334), b333 = usa.Entrada(333);
  const std::vector<uint8_t> mapas = HijoAmb(b334, 3);
  // Insignias de las misiones: #DMD +0x10 (30..87) -> objeto 40 + 2*(v-30) del #AMA 334/4 -> textura de su pista 1.
  // La tabla kInsignia queda como respaldo; se comprueba al cargar (58/58) y se registra.
  const std::vector<uint8_t> ama4 = HijoAmb(b334, 4);
  // DS62: datos del motor de sprites (334/2 mapa/nubes/globo y 334/4 insignias). Se copian al cargar; el estado de
  // los Pool se rehace en la primera pintura (CargarAma) y al entrar al mapa (ReiniciarAmaMapa).
  g_ama_mapa_datos = HijoAmb(b334, 2);
  g_ama_historia_datos = ama4;
  g_ama_cargada = false;
  g_ama_mapa = AmaMapaEstado{};
  g_ama_hist = AmaHistEstado{};
  std::vector<int> insignia;
  insignia.reserve(58);
  int insignia_ok = 0;
  for (int k = 0; k < 58; ++k) {
    insignia.push_back(TexturaObjetoAma(ama4, 40 + 2 * k));
    if (insignia[size_t(k)] == kInsignia[k]) ++insignia_ok;
  }
  REXLOG_INFO("dbz3 misiones: iw: insignias del #DMD: {}/58 coinciden con el #AMA 334/4 (objeto 40+2*(v-30))", insignia_ok);
  auto icono_de = [&](uint32_t cara) {
    if (cara < 30 || cara >= 88) return -1;
    const int k = int(cara) - 30;
    return insignia[size_t(k)] >= 0 ? insignia[size_t(k)] : kInsignia[k];
  };
  constexpr int kPiezas[4][4] = {{9, 10, 11, 12}, {22, 23, 24, 25}, {48, 49, 50, 51}, {32, 33, 34, 35}};
  for (int m = 0; m < 4; ++m)
    for (int i = 0; i < 4; ++i) out.piezas[m][i] = TexturaAmt(mapas, kPiezas[m][i]);
  out.hud.resize(size_t(std::max(CuantasAmt(mapas), 0)));
  // Nubes del mapa (t03..t08 de 334/3): las anima el #AMA 334/2 (objetos 0/60/112/120) via ama::Pool en DibujarFondo.
  for (int k = 3; k <= 8; ++k)
    if (k < int(out.hud.size())) out.hud[size_t(k)] = TexturaAmt(mapas, k);
  std::vector<int> hud = {kRobot, kGloboMapa, kFlechas, kCifrasRosa, kBarraBolas, kBolas, kClear};
  hud.insert(hud.end(), std::begin(kNombreSaga), std::end(kNombreSaga));
  hud.insert(hud.end(), std::begin(kBulma), std::end(kBulma));
  for (int k : hud) {
    if (k < int(out.hud.size()) && !out.hud[size_t(k)]) out.hud[size_t(k)] = TexturaAmt(mapas, k);
  }
  const std::vector<uint8_t> amt_iconos = HijoAmb(b334, 5);
  out.iconos = todas(amt_iconos);
  // 334/5 t71 y t94 son PSMCT32 (psm=0): sin esto, el arte de los eventos 0x8DA/0x8DC saldria vacio.
  for (int k = 0; k < int(out.iconos.size()); ++k)
    if (!out.iconos[size_t(k)]) out.iconos[size_t(k)] = Textura32Amt(amt_iconos, k);
  out.goku = Escala2x(Escala2x(De(out.iconos, kGokuMapa)));
  out.frases = todas(HijoAmb(b334, 6));
  out.ui = todas(HijoAmb(b334, 1));
  out.road = todas(HijoAmb(b333, 0));
  // DS55: arte de las pantallas que faltaban, del AFS del jugador y con los mismos lectores (#AMT directo).
  // 65 = Warrior's Room / Skill Edit (fondo 600x450, marcos, frascos de capsula y el Shenron de los deseos);
  // 370 = opciones (rotulo de brillo y deslizador); 62 = frases del deseo de Shenron (una textura por indice).
  out.sala = todas(usa.Entrada(65));
  out.opciones = todas(usa.Entrada(370));
  out.deseos = todas(usa.Entrada(62));
  REXLOG_INFO("dbz3 misiones: iw: pantallas DS55: sala {} texturas (DATA_USA 65), opciones {} (370), deseos {} (62)",
              out.sala.size(), out.opciones.size(), out.deseos.size());
  for (int i = 0; i < 18; ++i)   // s1 F04.3: titulos de los 18 minijuegos (0x7D0..0x7E1)
    out.titulos_mini.push_back(TexturaAmt(HijoAmb(usa.Entrada(size_t(kTituloMini0 + i)), 1), 0));
  // #DMD del mapa de Dragon Mission: el hijo con esa marca de DATA_CMN 1093 (el 11 en USA)
  const std::vector<uint8_t> e = cmn.Entrada(1093);
  std::vector<uint8_t> d;
  for (uint32_t c = 0; c < 32 && d.empty(); ++c) {
    std::vector<uint8_t> h = HijoAmb(e, c);
    if (h.size() >= 0x14 && std::memcmp(h.data(), "#DMD", 4) == 0) d = std::move(h);
  }
  const uint32_t n = d.size() >= 0x14 ? std::min<uint32_t>(U32(d, 8), 1024) : 0;
  // Recuento por tipo del #DMD (RE 2026-10-25): la copia USA trae 149 registros = 18 minijuegos (0), 96 misiones (1),
  // 22 marcas de Bola/esfera (2, +0x1C = 0..21), 5 eventos tipo 3 (claves 0x8D0-0x8D4, una por saga 1..5) y 8 tipo 4
  // (0x8D5-0x8DC). Los tipos 3-4 son los desbloqueos de guion: no los elige el cursor (Story_SpawnSpriteNode 0x31B770
  // solo da el bit 0x2 a los tipos 0-2 y Story_PickObjectAtCursor 0x2F8200 lo exige) y su puesta en escena al entrar al
  // mapa es _StartUnlock___Func (EU 0x2F8830: pan, sonido y 420 cuadros solo el tipo 3). El efecto de cada clave lo
  // aplica el guion MMI2, que no esta en el C: el port hace la escena y apunta la clave como aplicada [VERIFICAR].
  int n_mini = 0, n_bolas = 0, n_eventos = 0;
  std::string claves_evento;
  for (uint32_t i = 0; i < n && 0x10 + 4 * size_t(i + 1) <= d.size(); ++i) {
    const uint32_t o = U32(d, 0x10 + 4 * size_t(i));
    if (size_t(o) + 0x28 > d.size()) continue;
    const uint32_t cara = U32(d, o + 0x10), tipo = U32(d, o + 0x18), clave = U32(d, o + 0x20);
    CasillaDM c;
    c.fila = int(i);
    c.tipo = int(tipo);
    c.clave = int(clave);
    c.saga = int(U32(d, o + 0x04));
    c.x = float(U32(d, o + 0x08));
    c.y = float(U32(d, o + 0x0C));
    c.indice = int(U32(d, o + 0x1C));
    c.cara = int(cara & 0xFFFF);
    if (tipo == 1 && clave > 0x7E1 && clave <= 0x7E1 + kMisiones) {
      c.mision = int(clave - 0x7E1);
      c.icono = icono_de(cara);
    } else if (tipo == 0) {
      c.icono = kInterrogante;   // minijuego (frente P4)
      ++n_mini;
    } else if (tipo == 2) {
      c.icono = kEsfera;
      ++n_bolas;
    } else {
      // Tipos 3/4: desbloqueos de guion. Van a su lista (fuera del cursor y de Cas()) y al entrar al mapa se hace su
      // puesta en escena (_StartUnlock___Func). RE 2026-10-25: Story_SpawnSpriteNode (EU 0x31B770) solo da el bit 0x2
      // a los tipos 0-2 y Story_PickObjectAtCursor (EU 0x2F8200) lo exige, asi que no son elegibles con el cursor.
      ++n_eventos;
      claves_evento += (claves_evento.empty() ? "" : ",") + std::to_string(clave);
      if (c.saga >= 0 && c.saga < 6) out.eventos.push_back(c);
      continue;
    }
    if (c.saga >= 0 && c.saga < 6 && c.x <= 1280 && c.y <= 960) out.casillas.push_back(c);
  }
  if (n) {
    REXLOG_INFO("dbz3 misiones: iw: #DMD 1093 {} registros: {} misiones, {} minijuegos, {} marcas de Bola, {} eventos "
                "tipo 3/4 (claves {}) con escena de desbloqueo", n, n - n_mini - n_bolas - n_eventos, n_mini, n_bolas,
                n_eventos, claves_evento);
  }
  // Fighter's Road: #DMD de DATA_CMN 1092 (mismo formato; claves 0x867.. = combates 97..201) y sus frases (333/2)
  {
    const std::vector<uint8_t> e2 = cmn.Entrada(1092);
    std::vector<uint8_t> d2;
    for (uint32_t c = 0; c < 32 && d2.empty(); ++c) {
      std::vector<uint8_t> h = HijoAmb(e2, c);
      if (h.size() >= 0x14 && std::memcmp(h.data(), "#DMD", 4) == 0) d2 = std::move(h);
    }
    const uint32_t n2 = d2.size() >= 0x14 ? std::min<uint32_t>(U32(d2, 8), 1024) : 0;
    for (uint32_t i = 0; i < n2 && 0x10 + 4 * size_t(i + 1) <= d2.size(); ++i) {
      const uint32_t o = U32(d2, 0x10 + 4 * size_t(i));
      if (size_t(o) + 0x28 > d2.size()) continue;
      const uint32_t cara = U32(d2, o + 0x10), tipo2 = U32(d2, o + 0x18), clave = U32(d2, o + 0x20);
      if (tipo2 == 3 || tipo2 == 4) {   // el unico evento de Fighter's Road (fila 105, clave 0x8CF)
        CasillaDM e;
        e.fila = int(i);
        e.tipo = int(tipo2);
        e.clave = int(clave);
        e.saga = int(U32(d2, o + 0x04));
        e.x = float(U32(d2, o + 0x08));
        e.y = float(U32(d2, o + 0x0C));
        e.cara = int(cara & 0xFFFF);
        out.eventos_road.push_back(e);
        continue;
      }
      if (tipo2 != 1 || clave < 0x867 || clave > 0x866 + uint32_t(kRegistros - kMisiones)) continue;
      CasillaDM c;
      c.fila = int(i);
      c.clave = int(clave);
      c.mision = kMisiones + int(clave - 0x866);
      c.saga = int(U32(d2, o + 0x04));
      c.x = float(U32(d2, o + 0x08));
      c.y = float(U32(d2, o + 0x0C));
      c.icono = icono_de(cara);
      if (c.saga >= 0 && c.saga < 4 && c.x <= 1280 && c.y <= 960) out.casillas_road.push_back(c);
    }
    out.frases_road = todas(HijoAmb(b333, 2));
  }
  int misiones = 0;
  for (const auto& c : out.casillas) misiones += c.mision > 0;
  if (misiones == 0 || !out.piezas[0][0] || out.iconos.size() <= size_t(kGokuMapa)) {
    error = "Dragon Mission: sin #DMD 1093, mapa o iconos (" + std::to_string(misiones) + " misiones en el mapa)";
    return false;
  }
  // escenas de la historia: tablas del ELF y subtitulos de cada video (si faltan, el mapa sigue sin ellas)
  out.escenas = LeerEscenas(elf);
  out.raiz = iw;
  constexpr char kPrimero[] = "/USR/FD01A.sfd";   // nombres de los videos: tabla del ELF de 0x100 B por video
  const auto it = std::search(elf.begin(), elf.end(), kPrimero, kPrimero + sizeof(kPrimero) - 1);
  for (size_t o = size_t(it - elf.begin()), v = 0; it != elf.end() && v < size_t(kVideos) && o + 0x100 <= elf.size();
       ++v, o += 0x100)
    out.videos.emplace_back(reinterpret_cast<const char*>(&elf[o]), strnlen(reinterpret_cast<const char*>(&elf[o]), 0x100));
  if (!out.escenas.empty()) {
    for (int v = 0; v < kVideos; ++v) out.subtitulos.push_back(todas(usa.Entrada(size_t(kSubtitulos + v))));
  }
  // DS104: tabla derivada de videos de minijuego, con las 18 claves del #DMD 1093. Las tablas del ELF solo definen
  // tipo 0 para los indices 0 (FD01A, antes) y 10 (FD08B, despues); los otros 16 no reproducen SFD en el original.
  {
    std::string con_sfd, sin_sfd;
    int con = 0;
    for (int clave = 0x7D0; clave <= 0x7E1; ++clave) {
      const VideosMinijuegoIW v = VideosDeMinijuegoIW(out, clave);
      const int indice = IndiceMinijuegoIW(clave);
      std::string fila = std::to_string(clave) + " idx " + std::to_string(indice);
      if (v.previo >= 0) fila += " prev " + std::to_string(v.previo);
      if (v.final >= 0) fila += " final " + std::to_string(v.final);
      if (v.previo >= 0 || v.final >= 0) {
        ++con;
        if (!con_sfd.empty()) con_sfd += " | ";
        con_sfd += fila;
      } else {
        if (!sin_sfd.empty()) sin_sfd += " ";
        sin_sfd += std::to_string(clave);
      }
    }
    REXLOG_INFO("dbz3 misiones: iw: videos de minijuego (EU 0x289C20/0x20D800; tablas 0x473030/0x473090): {} de 18 "
                "claves con SFD [{}]; sin escena en el original [{}]", con, con_sfd, sin_sfd);
  }
  // DS134: revision mision a mision. Vuelca las 10 misiones de la tabla de escenas con su pareja previa/final (o el
  // respaldo medido si el escaneo fallo) para que la cobertura de los 96 registros quede en el log.
  {
    std::string parejas;
    int con = 0;
    for (int n = 1; n <= kMisiones; ++n) {
      const int pre = VideoIW(out, 1, n - 1, false), post = VideoIW(out, 1, n - 1, true);
      if (pre < 0 && post < 0) continue;
      ++con;
      parejas += (parejas.empty() ? "" : " | ") + std::to_string(n) + ":";
      parejas += pre >= 0 ? std::to_string(pre) : "-";
      parejas += "/";
      parejas += post >= 0 ? std::to_string(post) : "-";
    }
    REXLOG_INFO("dbz3 misiones: iw: misiones con SFD (tablas EU 0x473030/0x473090): {} de 96 [{}]", con, parejas);
  }
  // DS104: cruce indice de escena -> SFD de las 22 marcas tipo 2 del #DMD (+0x1C 0..21). El cargador de peliculas
  // (Movie_OpenContext, EU 0x3393B0) usa el indice directo sobre los nombres de 0x481960 + i*0x100.
  {
    std::string cruce;
    for (const CasillaDM& c : out.casillas) {
      if (c.tipo != 2) continue;
      const int v = VideoEscenaIW(out, c.indice);
      if (v < 0) continue;
      const std::filesystem::path rel = v < int(out.videos.size()) ? std::filesystem::path(out.videos[size_t(v)])
                                                                   : std::filesystem::path();
      cruce += (cruce.empty() ? "" : ", ") + std::to_string(c.indice) + "=" + rel.filename().string();
    }
    REXLOG_INFO("dbz3 misiones: iw: 22 marcas tipo 2 (#DMD +0x1C) -> SFD por indice directo: {}", cruce);
  }
  // DS76: cobertura de las 6 sagas y de las 105 de Fighter's Road con la tabla ya leida del mismo ELF.
  std::string error_tabla;
  const std::vector<MisionTablaIW> tabla = TablaMisionesIW(elf, error_tabla);
  RegistrarCoberturaIW(out, tabla);
  return true;
}

// ---- Pantalla ----
struct PantallaDM::Cache {
  struct Tx {
    std::unique_ptr<rex::ui::ImmediateTexture> tex;
    float w = 0, h = 0;
  };
  std::map<int, Tx> m;
};

ImTextureID PantallaDM::Tex(int clave, const Imagen& img, ImVec2* tam) {
  if (!tex_) tex_ = std::make_shared<Cache>();
  auto it = tex_->m.find(clave);
  if (it == tex_->m.end()) {
    Cache::Tx x;
    x.w = float(img.w);
    x.h = float(img.h);
    if (img) {
      auto* drawer = dbz3::launcher::PreviewDrawer();
      if (!drawer) return ImTextureID{};
      x.tex = drawer->CreateTexture(uint32_t(img.w), uint32_t(img.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                    img.rgba.data());
    }
    it = tex_->m.emplace(clave, std::move(x)).first;
  }
  if (tam) *tam = ImVec2(it->second.w, it->second.h);
  return it->second.tex ? reinterpret_cast<ImTextureID>(it->second.tex.get()) : ImTextureID{};
}

void PantallaDM::Imagen2(ImDrawList* dl, int clave, const Imagen& img, ImVec2 p0, ImVec2 p1, ImVec4 src, ImU32 tint) {
  ImVec2 tam;
  const ImTextureID id = Tex(clave, img, &tam);
  if (!id || tam.x <= 0 || tam.y <= 0) return;
  ImVec2 uv0(0, 0), uv1(1, 1);
  if (src.x != 0 || src.y != 0 || src.z != 0 || src.w != 0) {
    uv0 = ImVec2(src.x / tam.x, src.y / tam.y);
    uv1 = ImVec2(src.z / tam.x, src.w / tam.y);
  }
  dl->AddImage(id, p0, p1, uv0, uv1, tint);
}

// Cifras del juego alineadas a la derecha de `dcha` (centro vertical): grandes = amarillas de 334/1 t19 (24 x 44, con
// ' y " para el tiempo); si no, blancas de t22 (24 x 24, con /), o las rosadas del mapa (334/3 t18, igual). Devuelve el
// borde izquierdo.
float PantallaDM::Cifras(ImDrawList* dl, bool grandes, const std::string& s, ImVec2 dcha, float alto, const InfoDM& in,
                         bool rosa) {
  const int k = grandes ? kCifrasG : rosa ? kCifrasRosa : kCifrasB;
  const Imagen& tex = rosa && !grandes ? De(in.dm->hud, k) : De(in.dm->ui, k);
  const int base = rosa && !grandes ? 7000 : 3000;
  const float cw = 24, ch = grandes ? 44.0f : 24.0f, w = alto * cw / ch;
  float x = dcha.x;
  for (int i = int(s.size()) - 1; i >= 0; --i) {
    const char c = s[size_t(i)];
    int col = 0, fila = 0;
    float y0 = 0, hh = ch;
    if (c >= '0' && c <= '4') {
      col = c - '0';
    } else if (c >= '5' && c <= '9') {
      col = c - '5';
      fila = 1;
    } else if (grandes && (c == '\'' || c == '"')) {
      col = 5;
      y0 = c == '"' ? 22.0f : 0.0f;
      hh = 22;
    } else if (!grandes && c == '/') {
      col = 1;
      fila = 2;
    } else {
      x -= w * 0.5f;
      continue;
    }
    const ImVec2 p0(x - w, dcha.y - alto * 0.5f), p1(x, dcha.y - alto * 0.5f + alto * hh / ch);
    const float sx = fila == 2 ? 36.0f : float(col) * cw;   // la "/" va tras el infinito ancho (x 40-56)
    Imagen2(dl, base + k, tex, p0, p1, ImVec4(sx, fila * ch + y0, sx + cw, fila * ch + y0 + hh));
    x -= w * 0.9f;
  }
  return x;
}

// ---- Pantallas DS55: capsulas (Skill Edit), deseos de Shenron y brillo ----
// Los premios de capsula de cada mision salen del registro de 0x60 B del ELF (+0x1E/+0x20/+0x22; LeerTablaIW) y el
// estado de cada una de `iw_cap_<mision>_<ranura>` (lo escribe GanarIW, Battle_ApplyResultRewards EU 0x28A020). No se
// inventa ninguna: si un ID no aparece en la tabla, no esta en la lista.
std::vector<PantallaDM::CapsulaIW> PantallaDM::CapsulasDeMisiones(const InfoDM& in) const {
  std::vector<CapsulaIW> v;
  if (!in.tabla) return v;
  // EU 0x2ACA10 GeneralCapsuleProperties: ID de capsula -> su ficha (registro) del juego. El original busca el ID
  // en Capsules_SCDindexes (0x319 filas de 8 u16, 16 B, del bloque de datos de arranque); el port indexa por el
  // premio de cada mision, que es la ficha de la que sale la capsula. [H] la fila de 16 B de arranque no esta en el AFS.
  std::map<int, size_t> indice;   // ID -> fila
  for (const MisionTablaIW& m : *in.tabla) {
    if (m.numero < 1 || m.numero > kMisiones) continue;   // Dragon Mission; el Road no reparte deseos
    for (int k = 0; k < 3; ++k) {
      const int id = m.caps[k];
      if (id <= 0) continue;
      const auto it = indice.find(id);
      if (it == indice.end()) {
        indice.emplace(id, v.size());
        v.push_back({id, m.numero, k, false});
      }
    }
  }
  // Estado: solo las misiones ya superadas pueden haber entregado su capsula (GanarIW); una lectura por ranura.
  for (const MisionTablaIW& m : *in.tabla) {
    if (m.numero < 1 || m.numero > std::min(in.hecho, kMisiones)) continue;
    for (int k = 0; k < 3; ++k) {
      const int id = m.caps[k];
      if (id <= 0) continue;
      if (LeerProgreso("iw_cap_" + std::to_string(m.numero) + "_" + std::to_string(k), 0) > 0) {
        const auto it = indice.find(id);
        if (it != indice.end()) v[it->second].tiene = true;
      }
    }
  }
  // Las que entrega el deseo de Shenron (mision aun no jugada): clave propia por ID.
  for (const auto& [id, fila] : indice)
    if (!v[fila].tiene) v[fila].tiene = LeerProgreso("iw_cap_id_" + std::to_string(id), 0) > 0;
  return v;
}

void PantallaDM::AbrirCapsulas(const InfoDM& in) {
  // EU 0x2E3F40 SkillEditFunc1: alta de la pantalla (contexto, 4 carpetas CMN y estado). Aqui: se listan las capsulas
  // de las misiones con su estado y la tabla de bloqueadas (StoryModeLockedCapsules EU 0x2ABEC0).
  edit_caps_ = CapsulasDeMisiones(in);
  edit_sel_ = 0;
  edit_tab_ = 0;
  edit_abierto_ = true;
  REXLOG_INFO("dbz3 iw capsulas: Skill Edit con {} capsulas de mision ({} conseguidas)", edit_caps_.size(),
              std::count_if(edit_caps_.begin(), edit_caps_.end(), [](const CapsulaIW& c) { return c.tiene; }));
}

// EU 0x2B1220 CapsulePick_Start_LoadCmnFiles
void PantallaDM::AbrirShenron(const InfoDM& in) {
  // De la lista de capsulas, descarta las que ya estan (byte de estado 0xE0; el port, `tiene`) y saca tres al azar
  // sin repetir. Aqui la lista es la de premios de mision y el estado, `iw_cap_*`.
  const std::vector<CapsulaIW> todas = CapsulasDeMisiones(in);
  std::vector<CapsulaIW> libres;
  for (const CapsulaIW& c : todas)
    if (!c.tiene) libres.push_back(c);
  shenron_3_.clear();   // el original limpia los tres destinos (+0x14/+0x18/+0x1c = 0)
  std::mt19937 rng{std::random_device{}()};
  // EU 0x280990 CapsuleSelect_PickRandomThree_
  while (!libres.empty() && shenron_3_.size() < 3) {
    const size_t i = rng() % libres.size();
    shenron_3_.push_back(libres[i]);
    libres.erase(libres.begin() + std::ptrdiff_t(i));
  }
  shenron_sel_ = 0;
  shenron_t_ = 0;
  // El original configura y muestra la pantalla aunque queden menos de tres capsulas (AMA_MenuScreenShowUp_ y
  // +0x10 = 3): tambien con cero. Antes el port dejaba la fase cerrada si no habia ninguna.
  shenron_fase_ = 0;
  std::string ids;
  for (const CapsulaIW& c : shenron_3_) ids += (ids.empty() ? "" : ",") + std::to_string(c.id);
  REXLOG_INFO("dbz3 iw shenron: CapsulePick_Start_LoadCmnFiles (EU 0x2B1220): {} capsulas {} de {} libres",
              shenron_3_.size(), ids, libres.size() + shenron_3_.size());
}

// Skill Edit (EU 0x2E3F40) con el arte de DATA_USA 65. Dos vistas: las capsulas de mision y la tabla de bloqueadas
// (EU 0x2ABEC0 StoryModeLockedCapsules). La sustitucion de una capsula bloqueada por su variante libre necesita
// Capsules_SCDindexes (0x319 filas del bloque de datos de arranque), que no esta en el AFS: se marca [H].
PantallaDM::Accion PantallaDM::PantallaCapsulas(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado,
                                                const InfoDM& in) {
  (void)t;
  const auto a = S::GameArea(io);
  const auto& sala = in.dm->sala;
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  Imagen2(dl, 12000 + kSalaFondo, De(sala, kSalaFondo), a.At(0, 0), a.At(1200, 900));
  Imagen2(dl, 12000 + kSalaTitulo, De(sala, kSalaTitulo), a.At(40, 22), a.At(340, 168));
  Imagen2(dl, 12100 + kSalaSkillEdit, De(sala, kSalaSkillEdit), a.At(900, 38), a.At(1150, 155));
  // panel central (marco del juego) y categorias decorativas de la pantalla original (t16)
  Imagen2(dl, 12000 + kSalaMarco, De(sala, kSalaMarco), a.At(350, 150), a.At(1150, 700));
  Imagen2(dl, 12000 + kSalaCategorias, De(sala, kSalaCategorias), a.At(50, 250), a.At(300, 660));
  Imagen2(dl, 12000 + kSalaFrascos, De(sala, kSalaFrascos), a.At(70, 700), a.At(300, 800));
  // pestanas: CAPSULAS / BLOQUEADAS (LB/RB o izquierda/derecha)
  if (pulsado & (kLB | kRB | kLeft | kRight)) {
    edit_tab_ ^= 1;
    edit_sel_ = 0;
  }
  const char* tab0 = T::T("CAPSULAS", "CAPSULES");
  const char* tab1 = T::T("BLOQUEADAS", "LOCKED");
  S::MenuText(dl, a.At(560, 190), S::mode::kHead * a.s, tab0, edit_tab_ == 0, false, 220 * a.s);
  S::MenuText(dl, a.At(860, 190), S::mode::kHead * a.s, tab1, edit_tab_ == 1, false, 260 * a.s);
  // EU 0x2ABEC0 StoryModeLockedCapsules: capsulas bloqueadas del modo historia (las que no tienen el byte de
  // estado 0xE0). El original sustituye cada una por su variante libre buscandola en Capsules_SCDindexes; esa
  // tabla no esta en el AFS, asi que el port se queda en la lista filtrada por `!tiene` (marcado [H]).
  // filas visibles (8); la tabla de bloqueadas es la misma lista filtrada por !tiene
  std::vector<const CapsulaIW*> filas;
  for (const CapsulaIW& c : edit_caps_)
    if (edit_tab_ == 0 || !c.tiene) filas.push_back(&c);
  if (filas.empty()) {
    S::MenuText(dl, a.At(750, 430), S::mode::kRow * a.s,
                edit_tab_ == 0 ? T::T("Sin capsulas de mision en esta partida.", "No mission capsules in this save.")
                               : T::T("No queda ninguna capsula bloqueada.", "No locked capsules left."),
                false, false, 700 * a.s);
  } else {
    if (pulsado & kUp) edit_sel_ = (edit_sel_ + int(filas.size()) - 1) % int(filas.size());
    if (pulsado & kDown) edit_sel_ = (edit_sel_ + 1) % int(filas.size());
    const int visibles = 8;
    const int primero = std::clamp(edit_sel_ - visibles / 2, 0, std::max(0, int(filas.size()) - visibles));
    for (int i = 0; i < visibles && primero + i < int(filas.size()); ++i) {
      const CapsulaIW& c = *filas[size_t(primero + i)];
      const bool sel = primero + i == edit_sel_;
      const float y = 245.0f + 50.0f * float(i);
      if (sel)
        Imagen2(dl, 12000 + kSalaBarra, De(sala, kSalaBarra), a.At(390, y - 22), a.At(1090, y + 22), ImVec4(0, 0, 0, 0),
                IM_COL32(255, 255, 255, 190));
      Imagen2(dl, 12000 + kSalaCapsulaRoja, De(sala, kSalaCapsulaRoja), a.At(405, y - 29), a.At(425, y + 29));
      char linea[128];
      std::snprintf(linea, sizeof(linea), T::T("Cápsula %d  ·  misión %d", "Capsule %d  ·  mission %d"), c.id, c.mision);
      S::MenuText(dl, a.At(450, y), S::mode::kRow * a.s, linea, sel, false, 620 * a.s);
      S::MenuText(dl, a.At(980, y), S::mode::kSmall * a.s,
                  c.tiene ? T::T("CONSEGUIDA", "OBTAINED") : T::T("BLOQUEADA", "LOCKED"), false, !c.tiene, 170 * a.s);
    }
    const CapsulaIW& c = *filas[size_t(std::clamp(edit_sel_, 0, int(filas.size()) - 1))];
    // detalle: titulo y texto de la mision que la entrega (imagenes del AFS, las mismas de la ficha)
    if (in.textos && c.mision >= 1 && c.mision - 1 < int(in.textos->misiones.size())) {
      const MisionIW& mi = in.textos->misiones[size_t(c.mision - 1)];
      ImVec2 p0, p1;
      const ImVec4 rt = Recorte(mi.titulo);
      Encajar(rt.z - rt.x, rt.w - rt.y, 750, 690, 690, 44, 1.8f, p0, p1);
      Imagen2(dl, 15000 + c.mision, mi.titulo, a.At(p0.x, p0.y), a.At(p1.x, p1.y), rt);
    }
    if (edit_tab_ == 1) {
      S::MenuText(dl, a.At(750, 742), S::mode::kSmall * a.s,
                  T::T("Variante libre: [H] Capsules_SCDindexes (0x319 filas) no está en el AFS.",
                       "Free variant: [H] Capsules_SCDindexes (0x319 rows) is not in the AFS."),
                  false, true, 760 * a.s);
    }
  }
  S::mode::Help(dl, a, {{"LB/RB", T::T("Vista", "View")}, {"B", T::T("Volver", "Back")}});
  if (pulsado & kB) {
    // EU 0x2E3E10 WarriorsRoomSkillEditScreen_SkillTrayMenuClear
    edit_abierto_ = false;
    REXLOG_INFO("dbz3 iw capsulas: Skill Edit cerrado (WariorsRoomSkillEditScreen_SkillTrayMenuClear EU 0x2E3E10)");
  }
  return kNada;
}

// EU 0x381A30 LoadWaitShenronWishMainMenuFunc
// Deseos de Shenron (EU 0x2B1220 / 0x2B1210 / 0x381A30). Arte: DATA_USA 65 (ventana, Shenron, dialogos, aros y
// capsulas) y las tres frases del banco 62. La lista son tres capsulas de mision que aun no se tienen, sorteadas como
// CapsuleSelect_PickRandomThree_ (EU 0x280990). Al elegir se entrega (iw_cap_<mision>_<ranura>) y se apunta el deseo.
PantallaDM::Accion PantallaDM::PantallaShenron(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado,
                                               const InfoDM& in) {
  (void)t;
  const auto a = S::GameArea(io);
  const auto& sala = in.dm->sala;
  shenron_t_ += std::clamp(io.DeltaTime, 0.0f, 0.1f);
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  Imagen2(dl, 12000 + kSalaVentana, De(sala, kSalaVentana), a.At(0, 0), a.At(1200, 900));
  Imagen2(dl, 12000 + kSalaShenronGrande, De(sala, kSalaShenronGrande), a.At(30, 40), a.At(570, 520));
  Imagen2(dl, 12000 + kSalaDialogo, De(sala, kSalaDialogo), a.At(500, 700), a.At(1020, 876));
  Imagen2(dl, 12000 + kSalaAros, De(sala, kSalaAros), a.At(760, 570), a.At(1150, 700));
  if (shenron_fase_ == 0) {
    // EU 0x2B10F0/0x381A30 LoadWaitShenronWishMainMenuFunc y s_Shenron_Msg: el dragon habla y espera confirmacion
    // (0x305D5/0x305D7/0x305D8 del dialogo original; el texto exacto de esas lineas no esta en el AFS: [H]).
    S::MenuText(dl, a.At(770, 770), S::mode::kHead * a.s,
                T::T("Has reunido las 7 Bolas. Pide un deseo.", "You have gathered the 7 Dragon Balls. Make a wish."),
                true, false, 640 * a.s);
    S::mode::Help(dl, a, {{"A", T::T("Hablar", "Talk")}, {"B", T::T("Luego", "Later")}});
    if (pulsado & kA) shenron_fase_ = 1;
    if (pulsado & kB) {
      deseo_postpuesto_ = true;
      shenron_fase_ = -1;
    }
    return kNada;
  }
  if (shenron_fase_ == 2) {
    Imagen2(dl, 12000 + kSalaEtiquetas, De(sala, kSalaEtiquetas), a.At(840, 760), a.At(1080, 808),
            ImVec4(0, 64, 192, 96));
    const CapsulaIW& c = shenron_3_[size_t(std::clamp(shenron_sel_, 0, int(shenron_3_.size()) - 1))];
    char linea[128];
    std::snprintf(linea, sizeof(linea), T::T("Cápsula %d conseguida", "Capsule %d obtained"), c.id);
    S::MenuText(dl, a.At(760, 800), S::mode::kRow * a.s, linea, true, false, 700 * a.s);
    if (shenron_t_ > 1.4f || (pulsado & (kA | kB))) shenron_fase_ = -1;
    return kNada;
  }
  // fase 1: tres capsulas (CapsulePick_SelectLoop 0x281400: arriba/abajo y 0x4000 confirma). Si no queda ninguna
  // el original muestra igualmente la pantalla (CapsuleSelect_PickRandomThree_ EU 0x280990): aqui se avisa y se cierra.
  if (shenron_3_.empty()) {
    S::MenuText(dl, a.At(760, 800), S::mode::kRow * a.s,
                T::T("No quedan cápsulas que desear.", "No capsules left to wish for."), true, false, 700 * a.s);
    S::mode::Help(dl, a, {{"A", T::T("Cerrar", "Close")}, {"B", T::T("Luego", "Later")}});
    if (pulsado & (kA | kB)) {
      deseo_postpuesto_ = true;
      shenron_fase_ = -1;
    }
    return kNada;
  }
  if (pulsado & kUp) shenron_sel_ = (shenron_sel_ + int(shenron_3_.size()) - 1) % int(shenron_3_.size());
  if (pulsado & kDown) shenron_sel_ = (shenron_sel_ + 1) % int(shenron_3_.size());
  for (size_t i = 0; i < shenron_3_.size(); ++i) {
    const CapsulaIW& c = shenron_3_[i];
    const float y = 320.0f + 120.0f * float(i);
    const bool sel = int(i) == shenron_sel_;
    if (sel) Imagen2(dl, 12000 + kSalaBarra, De(sala, kSalaBarra), a.At(420, y - 36), a.At(1120, y + 36));
    Imagen2(dl, 12000 + kSalaCapsulaAmarilla, De(sala, kSalaCapsulaAmarilla), a.At(450, y - 30), a.At(560, y + 30));
    char linea[128];
    std::snprintf(linea, sizeof(linea), T::T("Cápsula %d  ·  misión %d", "Capsule %d  ·  mission %d"), c.id, c.mision);
    S::MenuText(dl, a.At(770, y), S::mode::kHead * a.s, linea, sel, false, 620 * a.s);
  }
  S::mode::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Luego", "Later")}});
  if (pulsado & kA) {
    const CapsulaIW& c = shenron_3_[size_t(shenron_sel_)];
    GuardarProgreso("iw_cap_" + std::to_string(c.mision) + "_" + std::to_string(c.ranura), c.id);
    GuardarProgreso("iw_cap_id_" + std::to_string(c.id), 1);   // aunque la mision aun no se haya jugado
    GuardarProgreso("iw_deseo", LeerProgreso("iw_deseo", 0) + 1);
    // EU 0x2B1210 LoadWaitShenronWishEndGameFunc: cierre del deseo (ayuda fuera, subtask muerto). La dispersion de
    // las Bolas al acabar no esta en esa funcion: no se toca iw_bolas ([H]).
    shenron_fase_ = 2;
    shenron_t_ = 0;
    REXLOG_INFO("dbz3 iw shenron: deseo {}: capsula {} de la mision {} entregada (EU 0x28A020)", LeerProgreso("iw_deseo", 0),
                c.id, c.mision);
  }
  if (pulsado & kB) {
    deseo_postpuesto_ = true;
    shenron_fase_ = -1;
  }
  return kNada;
}

// Brillo (EU 0x143920 GameBrightnessFunc; valores y rango de 0x2CEF20 Options_BrightnessInput_: 0..0x80, defecto 0x1A).
// El original escribe tres globales del GS; el present del port no expone ese registro, asi que el valor se guarda
// (`iw_brillo`) y la pantalla lo muestra con el deslizador del juego. [H] la aplicacion al frame.
PantallaDM::Accion PantallaDM::PantallaBrillo(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado,
                                              const InfoDM& in) {
  (void)t;
  (void)in;
  const auto a = S::GameArea(io);
  const auto& op = in.dm->opciones;
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  Imagen2(dl, 13000 + kOptFondo, De(op, kOptFondo), a.At(0, 0), a.At(1200, 900));
  Imagen2(dl, 13000 + kOptTitulo, De(op, kOptTitulo), a.At(60, 30), a.At(300, 160));
  Imagen2(dl, 13000 + kOptMarco, De(op, kOptMarco), a.At(340, 130), a.At(620, 626), ImVec4(0, 0, 280, 248));
  Imagen2(dl, 13000 + kOptEtiquetas, De(op, kOptEtiquetas), a.At(380, 260), a.At(564, 304),
          ImVec4(0, kOptLineaAlto * float(kOptLineaBrillo), 184, kOptLineaAlto * float(kOptLineaBrillo + 1)));
  // deslizador t27 (336x120) y valor
  Imagen2(dl, 13000 + kOptDeslizador, De(op, kOptDeslizador), a.At(580, 250), a.At(916, 370));
  const float frac = std::clamp(float(brillo_) / 128.0f, 0.0f, 1.0f);
  dl->AddRectFilled(a.At(610, 344), a.At(610 + 276 * frac, 356), IM_COL32(180, 220, 40, 220), 4 * a.s);
  char valor[32];
  std::snprintf(valor, sizeof(valor), "%d/128", brillo_);
  S::MenuText(dl, a.At(750, 410), S::mode::kHead * a.s, valor, true, false, 300 * a.s);
  Imagen2(dl, 13000 + kOptMando, De(op, kOptMando), a.At(880, 500), a.At(1160, 760));
  S::mode::Help(dl, a, {{"←/→", T::T("Ajustar", "Adjust")}, {"Y", T::T("Defecto (26)", "Default (26)")},
                        {"B", T::T("Guardar", "Save")}});
  if (pulsado & kLeft) brillo_ = std::max(0, brillo_ - (pulsado & kLB ? 10 : 1));
  if (pulsado & kRight) brillo_ = std::min(128, brillo_ + (pulsado & kRB ? 10 : 1));
  constexpr uint16_t kY = 0x8000;   // triangulo: 0x1A del original (0x2CEF20, uVar1 & 8)
  if (pulsado & kY) brillo_ = 26;
  if (pulsado & (kA | kB)) {
    GuardarProgreso("iw_brillo", brillo_);
    brillo_abierto_ = false;
    REXLOG_INFO("dbz3 iw brillo: {} (GameBrightnessFunc EU 0x143920; Options_BrightnessInput_ EU 0x2CEF20)", brillo_);
  }
  return kNada;
}

int PantallaDM::Mision(const InfoDM& in) const {
  return in.dm && sel_ >= 0 && sel_ < int(Cas(in).size()) ? Cas(in)[size_t(sel_)].mision : 0;
}

namespace {
// ---- Nubes del mapa de Dragon Mission (DS62: ama::Pool, no muestreador a mano) ----
// El objeto 0 del #AMA 334/2 (y 60/112/120 en los otros mapas) es el arbol de 6 nubes. Sus pistas (escala 2D
// 0x1B65D0, posicion spline 0x1B5F40, cuadro/textura 0x1B5440) y el recorrido de SprAnim_UpdateAndDrawNode los
// ejecuta ahora el motor ama::Pool de anim_iw.cpp. Lo unico que queda aqui es elegir el objeto segun el mapa
// (AsegurarAmaMapa) y pintar las Entradas que devuelve Pool::Pases. El objeto 0 sigue siendo el unico sin
// StoryMg_PositionAdjust: sus posiciones son pantalla (600x450), no mapa (DS36 §5).
}  // namespace

void PantallaDM::ReiniciarMapa() {
  ReiniciarAmaMapa();   // sprites del mapa recreados; el Pool vuelve al cuadro 0 (Sprite_DisplayHudSetByMode)
  bloqueo_scroll_ = false;
  volviendo_ = false;   // una entrada nueva cancela cualquier vuelta de combate pendiente
  t_intro_ = 0;
  ap_t_ = 0;
  tcam_ = 0;
  menu_mapa_ = 0;       // una entrada nueva cierra el menu START
  ev_ = -1; ev_fase_ = 0; ev_t_ = 0; ev_espera_ = 0; ev_saga_ = -1;   // cola de _StartUnlock___Func desde el principio
  ordenes_.clear();     // la cola del mapa (0x5D9220) empieza vacia: la llena PrepararEntradaMapa
  estado_ = 7;
  t_orden_ = 0;
  cambio_fase_ = 0;
  cambio_t_ = 0;
  cambio_saga_ = cambio_mision_ = -1;
  edit_abierto_ = false;          // una entrada nueva cierra las pantallas DS55
  brillo_abierto_ = false;
  esfera_video_ = -1;             // DS104: no arrastrar una marca tipo 2 de una entrada anterior
  mini_final_ = -1;               // DS104: ni un video final de minijuego pendiente
  mini_final_t0_ = -1;
  shenron_fase_ = -1;
  deseo_postpuesto_ = false;      // y permite volver a ofrecer el deseo si toca
  REXLOG_INFO("dbz3 iw mapa: intro Story_MapIntroDelayTick 101 cuadros (> 1,0); mandos bloqueados");
}

// ---- Casillas de evento del mapa (tipos 3 y 4 del #DMD) ----
// RE 2026-10-25 (EU): LoadWaitGeneralDragonMissionIcons_PlayerCommands (0x2FD0F0) crea al cargar el mapa los sprites de
// tipo 3 visibles (Story_MapObjectVisibility 0x2FD3A0) y Story_MapObjectsAppearTick (0x2FCCB0) el resto; al crear el
// primero con el byte de campana (0x2AB8A0) en 0x80 y sin 0x01, Story_SpawnMapObjectSprite (0x2FD580) llama a
// Story_CollectBattlePointObjects (0x2F7F40), que llena la cola de desbloqueos (lista 0x3A0, cuenta 0x4A0) y pone 0x01:
// la cola se llena UNA vez por partida. Story_MapIntroDelayTick (0x2F8790) acumula 0,01 por cuadro y, al pasar de 1,0,
// si la cola no esta vacia entra en _StartUnlock___Func en vez de en Story_MapScrollToTargetTick.
// EU 0x2F7F40 Story_CollectBattlePointObjects (elige el siguiente evento de la saga sin hacer: la cola 0x3A0 se
// llena al crear los sprites de tipo 3 y aqui se recorre en el mismo orden)
bool PantallaDM::EmpezarEventos(const InfoDM& in) {
  if (!in.dm || sel_ < 0) return false;
  const auto& cas = Cas(in);
  if (sel_ >= int(cas.size())) return false;
  const int saga = cas[size_t(sel_)].saga;
  const auto& evs = in.road ? in.dm->eventos_road : in.dm->eventos;
  const int vistos = LeerProgreso("iw_eventos", 0);   // bit = clave - 0x8CF (0x8CF Fighter's Road; 0x8D0..0x8DC DM)
  for (size_t i = 0; i < evs.size(); ++i) {
    const CasillaDM& e = evs[i];
    if (e.saga != saga) continue;
    const int bit = e.clave - 0x8CF;
    if (bit >= 0 && bit < 30 && ((vistos >> bit) & 1)) continue;   // ya hizo su escena
    ev_ = int(i);
    ev_fase_ = 0;
    ev_t_ = 0;
    ev_espera_ = 0;
    REXLOG_INFO("dbz3 iw mapa: evento {:#x} (tipo {}, saga {}, en {},{}): paneo de _StartUnlock___Func (EU 0x2F8830, "
                "x 300..980 y 225..735)", e.clave, e.tipo, e.saga, int(e.x), int(e.y));
    return true;
  }
  return false;
}

void PantallaDM::TerminarEvento(const InfoDM& in) {
  if (ev_ >= 0 && in.dm) {
    const auto& evs = in.road ? in.dm->eventos_road : in.dm->eventos;
    if (ev_ < int(evs.size())) {
      const CasillaDM& e = evs[size_t(ev_)];
      // El desbloqueo real lo aplica el guion MMI2 (ExecuteScript_MMI2 en _StartUnlock___Func2, EU 0x2F86B0), que no esta
      // en el C. Se apunta la clave como hecha (el original pone el bit 0x01 de la marca de campana, que es de partida)
      // para no repetir la escena en cada entrada al mapa. [VERIFICAR] en pantalla: que la casilla del evento ya no
      // vuelva a hacer el paneo y que el objeto que desbloquee (personaje/escena) lo haga el guion.
      if (const int bit = e.clave - 0x8CF; bit >= 0 && bit < 30)
        GuardarProgreso("iw_eventos", LeerProgreso("iw_eventos", 0) | (1 << bit));
      REXLOG_INFO("dbz3 iw mapa: evento {:#x} (tipo {}) terminado ({})", e.clave, e.tipo,
                  e.tipo == 3 ? "420 cuadros de _StartUnlock___Func2 + ExecuteScript_MMI2" : "Story_RefreshSpriteNode");
    }
  }
  ev_ = -1;
  ev_fase_ = 0;
  ev_t_ = 0;
  ev_espera_ = 0;
  estado_ = 7;   // _StartUnlock___Func2 vuelve al scroll (o al siguiente desbloqueo de la cola)
  if (!EmpezarEventos(in)) tcam_ = 0;   // cola agotada: Story_MapScrollToTargetTick hacia Goku
}

void PantallaDM::SonidoEvento(const CasillaDM& e) {
  // Sonidos de _StartUnlock___Func (EU 0x2F8830): 0x19 al empezar el destello del tipo 3 (con 0x1C/0x1B/0x1A si el +0xC
  // del sprite vale 0x68/0x66/0x65) y 0x16 en el tipo 4. Son IDs del banco de efectos del IOP (AMSOUND.IRX), que no esta
  // en el AFS de la copia: no hay fichero que reproducir (ya documentado en DS25/DS27), asi que solo se registran.
  // [VERIFICAR] en pantalla: el original suena justo al empezar el destello.
  const int id = e.tipo == 3 ? 0x19 : 0x16;
  REXLOG_INFO("dbz3 iw mapa: evento {:#x} tipo {}: sonido {:#x} (banco del IOP; sin fichero en el AFS)", e.clave, e.tipo, id);
  if (e.tipo == 3) {
    const int extra = e.cara == 0x68 ? 0x1C : e.cara == 0x66 ? 0x1B : e.cara == 0x65 ? 0x1A : 0;
    if (extra) REXLOG_INFO("dbz3 iw mapa: evento {:#x}: sonido extra {:#x} (cara {:#x})", e.clave, extra, e.cara);
  }
}

void PantallaDM::VolverDelCombate() {
  // EU 0x300F80 ScreenTransition_PushCommands(saga, 1, 0): al volver de un combate o minijuego el original no cambia
  // de estado a mano: empuja a la cola 5 (entrar en la saga), 0xE (camara al objetivo con la posicion guardada, arg 1),
  // 10 (iconos), 4 (DragonMissionFightScenarioExitLoadWaitEnd), 1 (desbloqueos), 8 (Bulma y Bolas) y 0xD (Bolas
  // nuevas). `DragonMissionMiscPlayerFuncs` (EU 0x2F98D0) los ejecuta de uno en uno. Los 60 + 100 cuadros de H03 los
  // lleva el comando 4 (Story_MapResultDelayTick / Story_MapReloadAfterDelayTick).
  volviendo_ = false;
  t_vuelta_ = 0;
  ReiniciarAmaMapa();   // el original recrea los sprites del mapa (Sprite_DisplayHudSetByMode)
  res_delay_hecho_ = recarga_hecha_ = false;
  g_pose_resultado = false;
  t_intro_ = 1.001f;    // sin espera de entrada (Story_MapIntroDelayTick no se repite)
  ap_t_ = 1.0e6f;       // casillas ya visibles: al volver no se repite la aparicion escalonada (comando 10 con arg 1)
  tcam_ = 1.0f;
  menu_mapa_ = 0;       // la vuelta cierra el menu START
  deseo_postpuesto_ = false;   // con las 7 Bolas, el deseo se vuelve a ofrecer tras el combate
  t_deseo_ = -1;
  ev_ = -1; ev_fase_ = 0; ev_t_ = 0; ev_espera_ = 0;   // la vuelta no repite _StartUnlock___Func
  ordenes_.clear();
  EmpujarComandosDeVuelta(saga_actual_, true, 0);
  estado_ = 7;
  bloqueo_scroll_ = false;
  REXLOG_INFO("dbz3 iw mapa: vuelta de combate: ScreenTransition_PushCommands 5/0xE/10/4/1/8/0xD ({} ordenes)",
              ordenes_.size());
}

// ---- Cola de ordenes del mapa ----
// En IW el mapa no encadena estados: empuja comandos en 0x5D9220/0x5D9260 (cuenta 0x5D92A0) y
// `DragonMissionMiscPlayerFuncs` (EU 0x2F98D0) ejecuta el primero; cada uno deja el estado puesto en ctx[0x1C].
void PantallaDM::EmpujarOrden(int cmd, int arg) {
  // EU 0x2F9FE0 Story_PushMapState: comando y argumento en la cola, cuenta +1.
  ordenes_.push_back({cmd, arg});
}

void PantallaDM::SacarOrdenYRecargar() {
  // EU 0x2FA030 Story_PopMapStateAndReload: LoadWaitPlayerCommands (recarga los mandos: aqui bloqueo hasta que el
  // scroll vuelva a pasar de 1), los dos acumuladores del scroll a 0, iconos y comandos recargados y cuenta -1.
  // EU 0x140930 LoadWaitPlayerCommands
  // EU 0x11DDF0 LoadWaitMapIcons
  if (!ordenes_.empty()) ordenes_.erase(ordenes_.begin());
  estado_ = 7;
  t_orden_ = 0;
  tcam_ = 0;
  bloqueo_scroll_ = true;
}

void PantallaDM::EmpujarComandosDeVuelta(int saga, bool de_combate, int arg) {
  // EU 0x300F80 ScreenTransition_PushCommands
  EmpujarOrden(5, saga);
  if (de_combate) {
    EmpujarOrden(0xE, 1);
    EmpujarOrden(10, 1);
    EmpujarOrden(4, 0);
    EmpujarOrden(1, 0);
    EmpujarOrden(8, 0);
    EmpujarOrden(0xD, 0);
  } else {
    EmpujarOrden(0xE, arg);   // sin combate: la camara va a la posicion por defecto del area (arg 1) o a la guardada
    EmpujarOrden(10, 0);
    EmpujarOrden(1, 0);
    EmpujarOrden(8, 0);
  }
}

void PantallaDM::PrepararEntradaMapa(const InfoDM& in) {
  const auto& cas = Cas(in);
  const int saga = sel_ >= 0 && sel_ < int(cas.size()) ? cas[size_t(sel_)].saga : 0;
  ordenes_.clear();
  EmpujarOrden(5, saga);    // entrar en la saga (musica, dialogo de entrada, cuenta de misiones y HUD)
  EmpujarOrden(0xE, 0);     // camara al objetivo
  EmpujarOrden(10, 0);      // iconos de la saga (con su aparicion escalonada)
  EmpujarOrden(1, 0);       // desbloqueos pendientes
  estado_ = 7;
}

void PantallaDM::EsperarYPop(float cuadros) {
  // EU 0x2FF3E0 Story_MapWaitThenPop: acumula fRam0049c7dc (1,0 por cuadro) en +0x34 y, cuando pasa el argumento
  // de la orden, saca la orden de la cola.
  // EU 0x2F9FD0 Get_uRam005d9260 (el argumento que se compara es el de la orden de delante)
  t_orden_ += 1.0f;
  if (t_orden_ > cuadros) SacarOrdenYRecargar();
}

void PantallaDM::SalirDelCombate() {
  // EU 0x2F9200 DragonMissionFightScenarioExitLoadWaitEnd: SkillEditIconClear(4,1,0) y (8,1,0) (en el port los
  // iconos se redibujan cada cuadro desde el #DMD: no hay sprites que borrar), la camara toma la posicion guardada
  // (+0x74/+0x75 pasan a +0xA7/+0xA8), el angulo del Goku (+0x8B) a 0 y el estado pasa a Story_MapResultDelayTick.
  t_vuelta_ = 0;
  res_delay_hecho_ = recarga_hecha_ = false;
  volviendo_ = true;
  tcam_ = 0;
}

void PantallaDM::DespacharOrden(const InfoDM& in) {
  // EU 0x2F98D0 DragonMissionMiscPlayerFuncs
  if (ordenes_.empty()) {
    estado_ = 7;
    return;
  }
  const OrdenMapa o = ordenes_.front();
  switch (o.cmd) {
    case 1: {   // desbloqueos pendientes: _StartUnlock___Func si la cola 0x3A0 tiene algo; si no, se saca
      ordenes_.erase(ordenes_.begin());
      if (!EmpezarEventos(in)) {
        estado_ = 7;
        tcam_ = 0;
      } else {
        estado_ = 1;
        bloqueo_scroll_ = true;
      }
      return;
    }
    case 2:   // cambio de saga con L1/R1
      CambiarSaga(o.arg, in);
      return;
    case 3:   // fin del cambio de saga
      FinCambioSaga(in);
      return;
    case 4:   // vuelta de combate
      SalirDelCombate();
      estado_ = 4;   // la orden se saca al acabar Story_MapReloadAfterDelayTick (100 cuadros)
      return;
    case 5:   // entrar en una saga: +0xE5 = saga, area en DAT_0049C348/50, musica, dialogo y cuenta de misiones
      saga_actual_ = o.arg;
      ev_saga_ = o.arg;
      REXLOG_INFO("dbz3 iw mapa: comando 5: saga {} (musica de la tabla 0x47DC80, dialogo de entrada y cuenta de "
                  "misiones de la saga)", o.arg);
      SacarOrdenYRecargar();
      return;
    case 8:   // Bulma y las Bolas: el port las pinta siempre con la guia, no hay sprites que crear
    case 0xD:   // Bolas nuevas (CloudSprites_SavedDragonballs(1)): la mascara iw_bolas ya se lee en el dibujo
      SacarOrdenYRecargar();
      return;
    case 9:   // menu de mision: en el port es la ficha (PantallaDM::Ficha la abre el mapa con kElegir)
    case 0xB:   // pantalla del menu de START (Story_MapSetupScreen)
    case 0xC:   // dibujo del menu de START (Story_MapDrawMenu)
    case 7:   // titulo de la saga: el HUD lo pinta siempre
      REXLOG_INFO("dbz3 iw mapa: comando {:#x} ({})", o.cmd, o.cmd == 9 ? "menu de mision = ficha del port" : "no portado");
      SacarOrdenYRecargar();
      return;
    case 0xE:   // camara al objetivo (posicion guardada o la del area)
      tcam_ = 0;
      mx_ = mx_ >= 0 ? mx_ : 0;
      SacarOrdenYRecargar();
      return;
    case 10:   // iconos de la saga: aparecen uno cada 20 cuadros desde el final de la lista
      // EU 0x2FD0F0 LoadWaitGeneralDragonMissionIcons_PlayerCommands (crea los sprites de la saga; la aparicion
      // escalonada la hace Story_MapObjectsAppearTick, EU 0x2FCCB0)
      if (o.arg == 0) ap_t_ = 0;
      SacarOrdenYRecargar();
      return;
    case 0xF:   // esperar y sacar (Story_MapWaitThenPop)
      estado_ = 0xF;
      t_orden_ = 0;
      return;
    case 0x10:   // fin
      ordenes_.clear();
      estado_ = 7;
      return;
    default:   // 0, 6 y el resto: vuelta al control del cursor (GeneralMapScripts)
      ordenes_.clear();
      estado_ = 7;
      return;
  }
}

void PantallaDM::CambiarSaga(int dir, const InfoDM& in) {
  // EU 0x2FDF80 DragonMissionScenarioChange_L1/R1: quita la instancia de la entrada de saga (ctx+0x90), pone a Goku
  // la animacion 10 de la tabla 0x498A70 (la de salida) y, segun el lado: L1 (arg 1) 180 cuadros y sonido 0x0F; R1
  // (arg 2) 240 y sonido 0x11; sin lado (arg 0) 20 y sonido 0x13. Al acabar, s_Start_Stage_Out
  // (0x002FDD30 / 0x002FDE00 / 0x002FDED0) hace Transition_SetSpeed(0,02) y Transition_StartWithCallback(
  // 10, Story_MapExitWithFade 0x2FDC80), que es el fundido y la recarga de iconos (comando 3 -> FinCambioSaga).
  cambio_fase_ = 1;
  cambio_dir_ = dir;
  cambio_t_ = 0;
  bloqueo_scroll_ = true;
  // [H] sonidos 0x0F/0x11/0x13 de la salida (CambiarSaga): son del banco del IOP, que no esta en el AFS de la copia.
  REXLOG_INFO("dbz3 iw mapa: comando 2: cambio de saga {} ({} cuadros de salida, sonido {} [H] banco IOP) hacia la saga {}",
              dir == 1 ? "L1" : dir == 2 ? "R1" : "sin lado", dir == 1 ? 180 : dir == 2 ? 240 : 20,
              dir == 1 ? "0x0F" : dir == 2 ? "0x11" : "0x13", cambio_saga_);
}

void PantallaDM::FinCambioSaga(const InfoDM& in) {
  // EU 0x2FDC30 DragonMissionScenarioChangeLoadWaitEnd: Transition_SetSpeed(fRam0049c7dc * 0,02),
  // MenuScripts(10, 0x2FDC10, 0) y estado 0 (vuelta al despachador).
  if (cambio_saga_ >= 0 && cambio_mision_ >= 0) {
    const auto& cas = Cas(in);
    if (cambio_mision_ < int(cas.size())) {
      sel_ = cambio_mision_;
      mx_ = -1;   // paseo hasta la casilla nueva
    }
    ev_saga_ = cambio_saga_;
  }
  ap_t_ = 0;    // iconos de la saga nueva (LoadWaitMapIcons)
  tcam_ = 0;
  cambio_fase_ = 0;
  cambio_saga_ = cambio_mision_ = -1;
  SacarOrdenYRecargar();
  REXLOG_INFO("dbz3 iw mapa: cambio de saga terminado: saca el comando y recarga los iconos");
}

void PantallaDM::PasoCambioSaga(float dt, const InfoDM& in) {
  // Salida de Goku (180/240/20 cuadros), fundido a negro a 0,02 por cuadro (Transition_SetSpeed, 50 cuadros) y
  // vuelta del fundido. `g_salida_saga` hace que el Goku del mapa use la animacion 10 durante la fase 1.
  if (cambio_fase_ == 0) return;
  cambio_t_ += dt * 60.0f;
  if (cambio_fase_ == 1) {
    g_salida_saga = true;
    const float dur = cambio_dir_ == 1 ? 180.0f : cambio_dir_ == 2 ? 240.0f : 20.0f;
    if (cambio_t_ > dur) {
      cambio_fase_ = 2;
      cambio_t_ = 0;
    }
  } else if (cambio_fase_ == 2) {
    if (cambio_t_ > 50.0f) {   // 1 / 0,02
      cambio_fase_ = 3;
      cambio_t_ = 0;
      g_salida_saga = false;
      const auto& cas = Cas(in);
      if (cambio_mision_ >= 0 && cambio_mision_ < int(cas.size())) {
        sel_ = cambio_mision_;
        mx_ = -1;
      }
      if (cambio_saga_ >= 0) ev_saga_ = cambio_saga_;
    }
  } else if (cambio_fase_ == 3) {
    if (cambio_t_ > 50.0f) {
      cambio_fase_ = 0;
      cambio_t_ = 0;
      FinCambioSaga(in);
    }
  }
}

// ---- Desbloqueo de capsulas ----
namespace {
// Una entrada de la tabla de prerrequisitos (registro de 0x10 B en RAM 0x5C6030, cargada al arrancar por
// Capsule_LoadAndInitData EU 0x2AB8E0): +0x00 tipo (0x06 = basta con UNA de sus capsulas, el resto = todas) y
// +0x06/+0x08/+0x0A los IDs de capsula (0xFFFF = hueco).
struct PrerrequisitoCapsula {
  int tipo = 0;
  int caps[3] = {-1, -1, -1};
};
// EU 0x2AB590 Capsule_UpdateUnlockFlagsAt0xC4: recorre las capsulas cuyo byte de estado esta a 0 y, si tiene sus
// prerrequisitos hechos (byte con la mascara 0xF0 en 0xE0; tipo 6, con uno basta), la marca 0x80 (desbloqueada).
// `indice` es CapsuleModerator (EU 0x2AB730): ID de capsula -> posicion en el registro.
void DesbloquearCapsulasAt0xC4(const std::vector<PrerrequisitoCapsula>& tabla, const std::vector<int>& indice,
                               std::vector<uint8_t>& estado) {
  for (size_t n = 1; n < tabla.size() && n < estado.size(); ++n) {
    if (estado[n] != 0) continue;
    int hechos = 0, total = 0;
    for (int k = 0; k < 3; ++k) {
      const int id = tabla[n].caps[k];
      // EU 0x2AB730 CapsuleModerator (ID de capsula -> posicion en el registro; -1 si no esta)
      if (id < 0 || id >= int(indice.size()) || indice[size_t(id)] < 0) continue;
      ++total;
      const size_t pos = size_t(indice[size_t(id)]);
      if (pos < estado.size() && (estado[pos] & 0xF0) == 0xE0) ++hechos;
    }
    if (tabla[n].tipo == 6 ? hechos != 0 : (hechos != 0 && hechos == total)) estado[n] = 0x80;
  }
}
}  // namespace

void PantallaDM::ActualizarRegistro(const InfoDM& in, bool forzar) {
  // EU 0x2B81D0 Save_UpdateRecordIfChanged: compara el registro guardado con los contadores y, si CUALQUIERA cambio,
  // reescribe el bloque entero (no campo a campo); si no, no toca nada. Los contadores del original son los globals
  // 0x5C1510..0x5C1530 y el bloque 0x5C1AD4 (0x2A2 B) con la posesion de capsulas 0x5C1554 (0x580 B); en el port el
  // equivalente son las claves de progreso de la historia, y el bloque de capsulas son los `iw_cap_<mision>_<k>` que
  // entrega GanarIW (pasarela_misiones.cpp) [H] el significado exacto de cada global del original.
  const int hechas = in.hecho, road = LeerProgreso("iw_road_hecho", 0), zeni = LeerProgreso("iw_zeni", 0),
            bolas = LeerProgreso("iw_bolas", 0), eventos = LeerProgreso("iw_eventos", 0),
            esferas = LeerProgreso("iw_esferas", 0);
  int caps = 0;
  if (in.tabla) {
    for (const MisionTablaIW& m : *in.tabla)
      for (int k = 0; k < 3; ++k)
        if (m.caps[k] > 0 && LeerProgreso("iw_cap_" + std::to_string(m.numero) + "_" + std::to_string(k), 0) > 0) ++caps;
  }
  char nuevo[160];
  std::snprintf(nuevo, sizeof(nuevo), "1,%d,%d,%d,%d,%d,%d,%d", hechas, road, zeni, bolas, eventos, caps, esferas);
  const int campos[8] = {1, hechas, road, zeni, bolas, eventos, caps, esferas};
  static const char* const kClaves[8] = {"iw_reg_0", "iw_reg_hechas", "iw_reg_road", "iw_reg_zeni",
                                         "iw_reg_bolas", "iw_reg_eventos", "iw_reg_caps", "iw_reg_esferas"};
  bool igual = true;
  bool existe = false;
  for (int i = 0; i < 8; ++i) {
    const int v = LeerProgreso(kClaves[i], i == 0 ? 0 : -1);
    existe = existe || (i == 0 ? v != 0 : v >= 0);
    if (v != campos[i]) igual = false;
  }
  if (igual && existe && !forzar) return;   // ningun campo cambio: no se escribe (salvo guardado explicito)
  const bool primera = !existe;
  for (int i = 0; i < 8; ++i) GuardarProgreso(kClaves[i], campos[i]);   // el bloque entero, como el original
  if (primera) {
    // EU 0x2ADA10 DragonMissionCapsules_: al no haber partida guardada, el original copia el estado de las capsulas de
    // las misiones (0x7D0..0x866 y 0x8D0..0x8DC) a +0xC4 del registro, recalcula los desbloqueos
    // (Capsule_UpdateUnlockFlagsAt0xC4 0x2AB590) y pone a 0xFF los rangos de las 128 fichas de luchador (+0x95C..).
    // En el port la partida nueva es "sin claves" (los rangos salen 0 = "-" en la ficha) y las capsulas ya las
    // entrega GanarIW, asi que el paso crea el registro y pasa el desbloqueo con la tabla de prerrequisitos vacia
    // (esa tabla viene de los datos de arranque, RAM 0x5C6030: no esta en el AFS, [H]).
    std::vector<PrerrequisitoCapsula> tabla;
    std::vector<int> indice;
    std::vector<uint8_t> estado;
    DesbloquearCapsulasAt0xC4(tabla, indice, estado);
    REXLOG_INFO("dbz3 iw mapa: registro de la historia nuevo (DragonMissionCapsules_ EU 0x2ADA10): {} capsulas de "
                "mision, {} misiones hechas", caps, hechas);
  } else {
    REXLOG_INFO("dbz3 iw mapa: Save_UpdateRecordIfChanged (EU 0x2B81D0): registro reescrito: {}", nuevo);
  }
}

// EU 0x2AB070 ExtraCostumes_CapsulesModeration: si el personaje `id` (0..0x38) tiene el traje extra `traje` (2..9).
// El original devuelve 0 si el ID no tiene tabla; 1 para los trajes 0 y 1; y para 2..9 lee el ID de capsula de
// ExtraCostume_Capsules (ELF) y exige que su byte de estado tenga 0xE0 (CapsuleModerator sobre RAM 0x5C1554).
// [H] ni la tabla del ELF ni el registro de capsulas de la partida estan en el AFS: 0 y 1 salen siempre; 2..9, 0.
int TrajeExtraDisponible(int id, int traje) {
  if (id < 0 || id >= 0x39) return 0;
  if (traje < 2) return 1;
  return 0;
}

// EU 0x2AE4D0 DragonMission_RandomizeMatchSetup: sorteo de personajes, trajes y escenario para el combate aleatorio
// (lo llama GoBackToNewGame/LoadGame/Continue/TitleFunc, EU 0x002B0B00, con el codigo 3). El original marca los 42
// personajes de Model_CharacterIDs que tienen su capsula (CapsuleCharactersModeration), saca uno con Memory() % n,
// arma la lista de trajes del elegido (0 y 1 siempre; 2..9 segun ExtraCostumes_CapsulesModeration) y saca el
// escenario de los 1..16 de CapsuleStagesModeration saltando el 0xB (el infierno de las peliculas).
SetupAleatorioIW SeleccionAleatoriaIW() {
  static std::mt19937 rng{std::random_device{}()};
  std::vector<int> personajes;
  // EU 0x2AB310 CapsuleCharactersModeration (el original pregunta por la capsula de cada uno de los 42 personajes;
  // el port pregunta si el personaje tiene ficha en B3 con IWaB3)
  for (int id = 0; id <= 41; ++id)   // 42 fichas de Model_CharacterIDs
    if (IWaB3(id) >= 0) personajes.push_back(id);
  std::vector<int> escenarios;
  // EU 0x2AB270 CapsuleStagesModeration (1..16, y el bucle del original corta antes del 0xB)
  for (int e = 1; e < 0x11; ++e)
    if (e != 0xB) escenarios.push_back(e);
  SetupAleatorioIW s;
  if (personajes.empty() || escenarios.empty()) return s;
  s.jugador = personajes[rng() % personajes.size()];
  s.escenario = escenarios[rng() % escenarios.size()];
  // Trajes: la lista del original arranca con los dos de serie (0 y 1, local_b0 = {0,1}) y suma 2..9 con
  // ExtraCostumes_CapsulesModeration si el jugador tiene esa capsula (EU 0x2AB070, ver TrajeExtraDisponible).
  std::vector<int> trajes;
  for (int t = 0; t <= 9; ++t)
    if (TrajeExtraDisponible(s.jugador, t)) trajes.push_back(t);
  s.traje = trajes[rng() % trajes.size()];
  REXLOG_INFO("dbz3 iw: combate aleatorio (DragonMission_RandomizeMatchSetup EU 0x2AE4D0): personaje {} (B3 {}), "
              "traje {}, escenario {} (B3 {})", s.jugador, IWaB3(s.jugador), s.traje, s.escenario, MapaIW(s.escenario));
  return s;
}


void PantallaDM::Elegir(const InfoDM& in, int mision) {
  // EU 0x2AFD00 DragonMissionSelectionSystem
  if (!in.dm) return;
  const auto& cas = Cas(in);
  for (size_t j = 0; j < cas.size(); ++j) {
    if (cas[j].mision == mision) {
      if (sel_ < 0 || cas[size_t(std::max(sel_, 0))].saga != cas[j].saga) mx_ = -1;   // otro mapa: sin paseo
      sel_ = int(j);
      ReiniciarMapa();   // entrar al mapa: Story_MapIntroDelayTick + aparicion de casillas
      PrepararEntradaMapa(in);   // y la cola de ordenes (5, 0xE, 10 y 1)
      return;
    }
  }
  if (sel_ < 0) {
    for (size_t j = 0; j < cas.size(); ++j) {
      if (cas[j].mision > 0) {
        sel_ = int(j);
        break;
      }
    }
    mx_ = -1;
    ReiniciarMapa();
    PrepararEntradaMapa(in);
  }
}

int PantallaDM::NivelCom() const {
  constexpr int k[5] = {0, 0, 1, 2, 2};   // Very Easy, Easy -> Novato; Normal -> Experto; Hard, Very Hard -> Avanzado
  return k[std::clamp(dificultad_, 0, 4)];
}

int PantallaDM::Rango(int puntos) {   // 0 C .. 4 Z. RE 2026-10-25: 3600/3000/2500/2000 son los puntos de los rangos
  // Z/S/A/B del bloque de dificultad 0 del #MSS de DATA_CMN 890/4 (medidos en la copia USA; el bloque 1 sube a
  // 6300/5300/4400/3400, etc.). Se usan los de la dificultad Normal, que es el fallback de las misiones sin rango.
  // EU 0x2AC0D0 Char_ComputeStatGrade (sus tres grados sumados dan el rango 0..4) [H] el port lo saca de los puntos:
  // los tres grados salen de las estadisticas del luchador, que no estan en el AFS de la mision.
  return puntos >= 3600 ? 4 : puntos >= 3000 ? 3 : puntos >= 2500 ? 2 : puntos >= 2000 ? 1 : 0;
}

// Goku del mapa en 3D, como en IW: DATA_CMN 1093 (el paquete del mapa, con el #DMD) trae a Goku en miniatura (huesos
// GKS_, como el del tablero de B2 pero otro archivo): hijos 0 #AMT, 1 #AMM (sus animaciones) y 2 #AMO (F1, 2026-10-16;
// los grupos 3-5 y 6-8 son otras figuras). Espera y andar: DBZ3_TEST_GOKU_IW_ANIM="espera,andar" (indices del 1093/1).
// RE 2026-10-25 (GeneralMapScripts EU 0x2FE270): espera 0 y a los 600 cuadros la 1; andar 2 (la 3 es con B).
namespace {
struct GokuMapa3D {
  ps2::Modelo m;
  std::vector<ps2::Tex> tx;
  ps2::Anim espera, espera2, andar, resultado, salida;
  float alto = 0, giro = 0, ymin = 1e9f, quieto = 0, t_ant = 0;
  bool cargado = false;
};
GokuMapa3D g_gm;
void CargarGokuMapa() {
  // EU 0x300150 LoadWaitDragonMissionFunc1: carga los sprites del mapa (Char_InitAnimInstance con la animacion
  // 0x498930 del Goku, Sprite_DisplayHudSetByMode(0) y la matriz de camara). Aqui: el Goku 3D de DATA_CMN 1093 y sus
  // animaciones del #AMM hijo 1.
  if (g_gm.cargado) return;
  g_gm.cargado = true;
  AfsPs2 cmn;
  if (!cmn.Abrir(BuscarIW(), "DATA_CMN.AFS")) return;
  const std::vector<uint8_t> e = cmn.Entrada(1093);
  // DBZ3_TEST_GOKU_IW_TEX="hijo,relativa" (pruebas): #AMT del 1093 (el 0: blancas con detalles, el color lo pone el
  // material de cada parte) y claves sobre el reposo (como B2) o no
  int itx = 0, rel = 0;
  if (const char* v = std::getenv("DBZ3_TEST_GOKU_IW_TEX")) std::sscanf(v, "%d,%d", &itx, &rel);
  g_gm.m = ps2::CargarSuelto(HijoAmb(e, 2), HijoAmb(e, uint32_t(itx)), true, true, true);
  if (!g_gm.m.esq) return;
  const std::vector<uint8_t> amm = HijoAmb(e, 1);
  std::string largos;
  for (uint32_t i = 0; i < 32; ++i) {
    const ps2::Anim a = ps2::LeerAnim(amm, i, g_gm.m.esq->padre.size());
    if (!a) break;
    largos += std::to_string(a.cuadros) + " ";
  }
  int ie = 0, ia = 2;   // andar: la 2 (GeneralMapScripts, EU 0x2FE270: con palanca > 60 usa la 2; la 3 es con B, x1,5)
  if (const char* v = std::getenv("DBZ3_TEST_GOKU_IW_ANIM")) std::sscanf(v, "%d,%d", &ie, &ia);
  g_gm.espera = ps2::LeerAnim(amm, uint32_t(ie), g_gm.m.esq->padre.size());
  g_gm.espera2 = ps2::LeerAnim(amm, 1, g_gm.m.esq->padre.size());   // segunda espera: a los 600 cuadros quieto (10 s)
  g_gm.andar = ps2::LeerAnim(amm, uint32_t(ia), g_gm.m.esq->padre.size());
  g_gm.resultado = ps2::LeerAnim(amm, 8, g_gm.m.esq->padre.size());  // pose del resultado (Story_MapResultDelayTick)
  // Salida de saga (EU 0x2FDF80): MiniGokuAnimations(ctx+0x76, 10, 0x498A70) al cambiar de saga con L1/R1.
  g_gm.salida = ps2::LeerAnim(amm, 10, g_gm.m.esq->padre.size());
  g_gm.espera.relativa = g_gm.andar.relativa = g_gm.espera2.relativa = g_gm.resultado.relativa =
      g_gm.salida.relativa = rel != 0;
  for (const auto& tr : g_gm.m.malla.tris)
    for (const auto& v : tr.p) g_gm.alto = std::max(g_gm.alto, v.y);
  g_gm.tx = ps2::Texturas(g_gm.m);
  REXLOG_INFO("dbz3 iw dm: Goku del mapa 3D (CMN 1093/2): {} triangulos, alto {:.1f}, animaciones (cuadros) {}; espera {} "
              "(luego la 1 a los 600), andar {}, resultado {}", g_gm.m.malla.tris.size(), g_gm.alto, largos, ie, ia, 8);
}
// EU 0x00136A60 CharacterWheelPoses2: mezcla dos poses del mismo esqueleto. El original hace dos etapas sobre los
// bloques de 3x4 de cada hueso: `u` elige A (0), B (1) o interpola (param_1) y `v` mezcla el resultado con la pose
// que ya habia (param_2; 1 = copia). Aqui las poses son mallas ya posadas por ps2::Posar (misma topologia): se
// interpolan posiciones y normales por vertice, igual que los VU del original.
void MezclarPosesIW(const ps2::Malla& a, const ps2::Malla& b, float u, float v, ps2::Malla& actual) {
  if (a.tris.size() != b.tris.size()) return;
  u = std::clamp(u, 0.0f, 1.0f);
  v = std::clamp(v, 0.0f, 1.0f);
  const bool hay_actual = actual.tris.size() == a.tris.size();
  ps2::Malla out = a;
  for (size_t i = 0; i < a.tris.size(); ++i) {
    const ps2::Malla::Tri& ta = a.tris[i];
    const ps2::Malla::Tri& tb = b.tris[i];
    const ps2::Malla::Tri& tc = hay_actual ? actual.tris[i] : ta;
    ps2::Malla::Tri& to = out.tris[i];
    for (int c = 0; c < 3; ++c) {
      ps2::V3 p{ta.p[c].x + (tb.p[c].x - ta.p[c].x) * u, ta.p[c].y + (tb.p[c].y - ta.p[c].y) * u,
                ta.p[c].z + (tb.p[c].z - ta.p[c].z) * u};
      ps2::V3 n{ta.n[c].x + (tb.n[c].x - ta.n[c].x) * u, ta.n[c].y + (tb.n[c].y - ta.n[c].y) * u,
                ta.n[c].z + (tb.n[c].z - ta.n[c].z) * u};
      if (hay_actual) {
        p = {tc.p[c].x + (p.x - tc.p[c].x) * v, tc.p[c].y + (p.y - tc.p[c].y) * v,
             tc.p[c].z + (p.z - tc.p[c].z) * v};
        n = {tc.n[c].x + (n.x - tc.n[c].x) * v, tc.n[c].y + (n.y - tc.n[c].y) * v,
             tc.n[c].z + (n.z - tc.n[c].z) * v};
      }
      const float largo = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
      if (largo > 1e-6f) n = {n.x / largo, n.y / largo, n.z / largo};
      to.p[c] = p;
      to.n[c] = n;
    }
  }
  actual = std::move(out);
}

// Goku del mapa de pie en `pie` (pantalla), `alto_px` de alto, mirando hacia (dx, dy) del mapa si anda; camara casi de
// frente y 20 grados desde arriba (la de R2 para el tablero de B2), con el cel shading de P4.
bool DibGokuMapa(ImDrawList* dl, ImVec2 pie, float alto_px, float t, bool anda, float dx, float dy) {
  CargarGokuMapa();
  if (!g_gm.m.malla || g_gm.alto <= 0) return false;
  if (anda && (dx != 0 || dy != 0)) g_gm.giro = std::atan2(dx, dy);
  // Espera/andar como el original (GeneralMapScripts, EU 0x2FE270): quieto empieza con la 0 y a los 600 cuadros (10 s)
  // pasa a la 1; con la palanca (> 60) usa la 2 y al soltarla vuelve a la 0. La 8 es la pose del resultado de la vuelta.
  if (anda)
    g_gm.quieto = 0;
  else
    g_gm.quieto += std::clamp(t - g_gm.t_ant, 0.0f, 0.1f) * 60.0f;
  g_gm.t_ant = t;
  // EU 0x31BFA0 MiniGokuAnimations (tabla 0x498930 de espera/andar y 0x498A70 la de la salida)
  const ps2::Anim* elegida = &g_gm.espera;
  if (g_salida_saga && g_gm.salida)
    elegida = &g_gm.salida;   // EU 0x2FDF80: animacion 10 (tabla 0x498A70) mientras se sale de la saga
  else if (anda && g_gm.andar)
    elegida = &g_gm.andar;
  else if (g_pose_resultado && g_gm.resultado)
    elegida = &g_gm.resultado;
  else if (g_gm.quieto > 600.0f && g_gm.espera2)
    elegida = &g_gm.espera2;
  const ps2::Anim& an = *elegida;
  static ps2::Malla pose;
  const bool anima = bool(an);
  if (anima) ps2::Posar(*g_gm.m.esq, an, t, pose);
  // EU 0x00136A60 CharacterWheelPoses2 (lo llama Char_AdvanceAnimFrame EU 0x31BC80 cada cuadro): la pose nueva se
  // mezcla con la del cuadro anterior 8 cuadros al cambiar de animacion; despues, copia (param_2 = 1 como el original).
  static ps2::Malla anterior;
  static const ps2::Anim* anterior_anim = nullptr;
  static int mezcla = 0;
  const ps2::Anim* anim_actual = anima ? &an : nullptr;
  if (anim_actual != anterior_anim) {
    anterior_anim = anim_actual;
    mezcla = 8;
  }
  const ps2::Malla& objetivo = anima ? pose : g_gm.m.malla;
  if (anterior.tris.size() != objetivo.tris.size()) anterior = objetivo;
  MezclarPosesIW(objetivo, objetivo, 1.0f, mezcla > 0 ? 1.0f / float(mezcla + 1) : 1.0f, anterior);
  if (mezcla > 0) --mezcla;
  if (g_gm.ymin > 1e8f)
    for (const auto& tr : anterior.tris)
      for (const auto& v : tr.p) g_gm.ymin = std::min(g_gm.ymin, v.y);
  ps2::Camara cam;
  // EU 0x278800 Effect_SetViewMatrixFromMtx (la camara del mapa: matriz de vista construida aqui a mano)
  cam.fov = 0.785f;
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  const float f = 450.0f / std::tan(cam.fov * 0.5f), d = g_gm.alto * f / std::max(alto_px, 1.0f);
  const float th = 0.35f, cth = std::cos(th), sth = std::sin(th);
  cam.m[0] = 1;
  cam.m[5] = cth, cam.m[9] = -sth;
  cam.m[6] = sth, cam.m[10] = cth;
  cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = g_gm.alto * 0.009f;
  esc.Poner(cam, anterior, g_gm.tx, {0, -g_gm.ymin, 0}, g_gm.giro, 1.0f, toon);
  esc.Pintar(dl);
  return true;
}
}  // namespace

// Musica del mapa (R7): DragonMissionMiscPlayerFuncs (EU, iw_sles55347.c l. 385748) toca la pista de banco 2 (ADX_CMN)
// de la tabla EU 0x47DC80 (Dragon Mission) o 0x47DCA0 (Fighter's Road, mgr+0x4AC = 1) por el indice mgr[0xE6], que es
// la posicion del area del mapa en DAT_0049C348 (4 mapas: Tierra, Namek, GT, espacio) o DAT_0049C350 (4 tramos del
// Road). Valores leidos del ELF USA (0x47D680 = 35..41, 0x47D6A0 = 49..55; areas 0x49BD88 / 0x49BD90).
int PantallaDM::Pista(const InfoDM& in) const {
  const auto& cas = Cas(in);
  if (sel_ < 0 || sel_ >= int(cas.size())) return -1;
  const int saga = cas[size_t(sel_)].saga;
  return in.road ? 49 + std::clamp(saga, 0, 3) : 35 + MapaDeSaga(saga);
}

// Mapa de la saga de la casilla elegida (640 x 480 px del mapa en la pantalla, como en la PS2) con la camara siguiendo
// al Goku del mapa, que anda hasta la casilla; insignias de las misiones abiertas y dibujos de los minijuegos.
// EU 0x2F95A0 Story_MapRender
void PantallaDM::DibujarFondo(ImDrawList* dl, const ImGuiIO& io, float t, const InfoDM& in) {
  const auto& cas = Cas(in);
  if (sel_ < 0 || sel_ >= int(cas.size())) Elegir(in, std::clamp(in.hecho + 1, 1, kRegistros));
  if (sel_ < 0) return;
  const float dt = std::clamp(t - t_ultimo_, 0.0f, 0.1f);
  // Entrada al mapa (no hay aviso del coordinador): un hueco grande entre dos dibujos = mapa reabierto (vuelta de un
  // combate/minijuego). Tras un combate (VolverDelCombate) no se repite la intro: IW recarga las casillas con sus dos
  // esperas. ReiniciarMapa() tambien lo hace Elegir, que es el aviso exacto que existe hoy.
  if (t_intro_ < 0 || (t - t_ultimo_ > 0.5f && ordenes_.empty())) {
    if (volviendo_) {
      t_intro_ = 1.0f;
      ap_t_ = 1.0e6f;
      tcam_ = 1.0f;
    } else {
      ReiniciarMapa();
      PrepararEntradaMapa(in);
    }
  }
  t_ultimo_ = t;
  // EU 0x2F9100 Story_MapResultDelayTick
  // EU 0x2F9060 Story_MapReloadAfterDelayTick
  // H03: espera de resultado y recarga de iconos.
  if (volviendo_) {
    t_vuelta_ += dt * 60.0f;
    if (!res_delay_hecho_ && t_vuelta_ >= 60.0f) {
      res_delay_hecho_ = true;
      g_pose_resultado = true;   // MiniGokuAnimations(...,8,...): la pose de resultado (EU 0x2F9100)
      REXLOG_INFO("dbz3 iw mapa: Story_MapResultDelayTick 60 cuadros: Goku vuelve a su sitio (pose 8)");
    }
    if (!recarga_hecha_ && t_vuelta_ > 100.0f) {
      recarga_hecha_ = true;
      volviendo_ = false;
      g_pose_resultado = false;
      REXLOG_INFO("dbz3 iw mapa: Story_MapReloadAfterDelayTick 100 superado (cuadro 101): casillas recargadas");
      if (estado_ == 4) SacarOrdenYRecargar();   // el comando 4 acaba aqui y devuelve el turno al despachador
    }
  }
  // EU 0x2F8790 Story_MapIntroDelayTick
  // Story_MapIntroDelayTick: +0,01 por cuadro (fRam0049c7dc = 1,0 a 60 Hz; 1,2 en el modo de 50) y al
  // pasar de 1,0 se pasa al scroll. Son 100 cuadros (101 por el > 1,0), no un sleep de interfaz.
  if (t_intro_ < 1.0f) {
    t_intro_ += 0.01f * dt * 60.0f;
    if (t_intro_ >= 1.0f) {   // > 1,0 = cuadro 101 (el original compara estrictamente); se deja por encima de 1,0
      t_intro_ = 1.001f;      // para que la vuelta de combate (t_intro_ = 1,0) NO repita la intro
      ap_t_ = 0;      // Story_MapObjectsAppearTick empieza a contar
      tcam_ = 0;      // Story_MapScrollToTargetTick arranca con su acumulador a 0
      bloqueo_scroll_ = true;   // B3: el original recarga los mandos al pasar el scroll de 1,0 (cuadro 101)
      REXLOG_INFO("dbz3 iw mapa: intro acabada (cuadro 101: > 1,0); aparecen {} casillas", cas.size());
      // Desde aqui manda el despachador: la cola (5 entrar en la saga, 0xE camara, 10 iconos, 1 desbloqueos) la
      // preparo PrepararEntradaMapa. EmpezarEventos solo se llamaba aqui antes de tener la cola.
    }
  } else {
    ap_t_ += dt * 60.0f;
  }
  const bool listo = t_intro_ >= 1.0f;
  // EU 0x2F98D0 DragonMissionMiscPlayerFuncs: con el mapa listo y sin estado en curso, ejecuta la primera orden de la
  // cola. Los mandos estan descargados (bloqueo_scroll_) hasta que el scroll pasa de 1,0, como LoadWaitPlayerCommands.
  if (listo && estado_ == 7 && !bloqueo_scroll_ && !volviendo_ && cambio_fase_ == 0 && ev_ < 0 && !ordenes_.empty())
    DespacharOrden(in);
  // EU 0x2FF3E0 Story_MapWaitThenPop (comando 0xF)
  if (listo && estado_ == 0xF) EsperarYPop(float(ordenes_.empty() ? 0 : ordenes_.front().arg));
  if (cambio_fase_ != 0) PasoCambioSaga(dt, in);
  // EU 0x2B81D0 Save_UpdateRecordIfChanged: al abrir el mapa y cuando cambia la mision superada (cache 0,5 s: leer el
  // progreso abre el fichero).
  if (t - t_registro_ > 0.5f && hecho_registro_ != in.hecho) {
    t_registro_ = t;
    hecho_registro_ = in.hecho;
    ActualizarRegistro(in);
  }
  const CasillaDM& c = cas[size_t(sel_)];
  if (mx_ < 0) {
    mx_ = cx_ = c.x;
    my_ = cy_ = c.y;
  }
  constexpr float kX0 = 300, kX1 = 980, kY0 = 225, kY1 = 735;
  if (listo) {   // durante la espera de entrada, Goku y la camara se quedan quietos (el original no carga mandos)
    const float k1 = 1.0f - std::exp(-dt * 6.0f);
    mx_ += (c.x - mx_) * k1;
    my_ += (c.y - my_) * k1;
  }
  // EU 0x2F8830 _StartUnlock___Func
  if (ev_ >= 0) {
    // _StartUnlock___Func (EU 0x2F8830): panea la camara hasta la casilla de evento con el mismo esquema que
    // Story_MapScrollToTargetTick (0,01 por cuadro, salto a menos de 2 px) y con el clamp x 300..980, y 225..735.
    const auto& evs = in.road ? in.dm->eventos_road : in.dm->eventos;
    if (ev_ < int(evs.size())) {
      const CasillaDM& e = evs[size_t(ev_)];
      const float ox = std::clamp(e.x, kX0, kX1), oy = std::clamp(e.y, kY0, kY1);
      if (ev_fase_ == 0) {
        if (std::hypot(ox - cx_, oy - cy_) < 2.0f) {
          cx_ = ox, cy_ = oy, ev_t_ = 1.0f;
        } else {
          cx_ += (ox - cx_) * ev_t_;
          cy_ += (oy - cy_) * ev_t_;
        }
        if ((ev_t_ += 0.01f * dt * 60.0f) > 1.0f) {
          cx_ = ox, cy_ = oy;
          SonidoEvento(e);
          if (e.tipo == 3) {   // _StartUnlock___Func2: 420 cuadros (1,0 por cuadro) y ExecuteScript_MMI2
            ev_fase_ = 1;
            ev_espera_ = 0;
          } else {             // tipo 4: Story_RefreshSpriteNode(1,1) + sonido 0x16 y a seguir, sin espera
            TerminarEvento(in);
          }
        }
      } else {
        // EU 0x2F86B0 "StartUnlock!!"Func2: 420 cuadros (+0x34 > 420, fRam0049c7dc = 1,0) y luego el guion (MMI2)
        ev_espera_ += dt * 60.0f;
        if (ev_espera_ > 420.0f) TerminarEvento(in);
      }
      cx_ = std::clamp(cx_, kX0, kX1), cy_ = std::clamp(cy_, kY0, kY1);
    } else {
      ev_ = -1, bloqueo_scroll_ = false, tcam_ = 0;   // no deberia pasar: no dejar los mandos bloqueados
    }
  } else if (listo) {
    // EU 0x2F9280 Story_MapCameraUpdate
    // EU 0x2F8390 Story_MapScrollToTargetTick
    // aplicar_estudios (entrega 16 P4): objetivo = Goku del
    // mapa (+0x1D0) limitado a x [300, 980] e y [225, 735]; camara += (objetivo - camara) * t, t += 0,01 por cuadro y
    // vuelve a 0 al pasar de 1; a menos de 2 px, la camara salta al objetivo con t = 1; despues se limita igual.
    const float ox = std::clamp(mx_, kX0, kX1), oy = std::clamp(my_, kY0, kY1);
    if (std::hypot(ox - cx_, oy - cy_) < 2.0f) {
      cx_ = ox, cy_ = oy, tcam_ = 1.0f;
    } else {
      cx_ += (ox - cx_) * tcam_;
      cy_ += (oy - cy_) * tcam_;
    }
    if ((tcam_ += 0.01f * dt * 60.0f) > 1.0f) {
      tcam_ = 0;
      if (bloqueo_scroll_) {
        bloqueo_scroll_ = false;
        REXLOG_INFO("dbz3 iw mapa: Story_MapScrollToTargetTick 1,0 superado: mandos recargados");
      }
    }
    cx_ = std::clamp(cx_, kX0, kX1), cy_ = std::clamp(cy_, kY0, kY1);
  } else {
    cx_ = std::clamp(cx_, kX0, kX1), cy_ = std::clamp(cy_, kY0, kY1);
  }
  const S::Imagen gi = S::GuestImage(io);   // L05: la imagen del juego (21:9/16:10/4:3), no la ventana
  const ImVec2 D = gi.Size();
  // I03 (studies 4): 1:1 con la pantalla de 640x480 del original (640 px de mapa a lo alto).
  // Antes kZoom = 1,7: se veian 282 px de mapa de alto (1,7x mas cerca) y los iconos salian
  // un 70 % mas grandes que en el original. Con 1,0 el alto visible es 480 y el ancho 853
  // (16:9 muestra mas mapa a los lados; el alto coincide con el original).
  constexpr float kZoom = 1.0f;
  const float s = D.y / 480.0f * kZoom, vw = D.x / s, vh = 480.0f / kZoom;
  // IW pone la camara en el punto (300, 225) de su pantalla de 640 x 480 (Story_WorldToScreenX/Y): el centro es +20, +15.
  // Pantallas muy anchas: ademas, sin salirse del mapa de 1280 x 960.
  const float vx = cx_ + 20, vy = cy_ + 15;
  const float camx = vw >= 1280 ? 640.0f : std::clamp(vx, vw * 0.5f, 1280 - vw * 0.5f);
  const float camy = vh >= 960 ? 480.0f : std::clamp(vy, vh * 0.5f, 960 - vh * 0.5f);
  flechas_[0] = listo && cy_ > 225.0f + 1;
  flechas_[1] = listo && cy_ < 735.0f - 1;
  flechas_[2] = listo && cx_ > 300.0f + 1;
  flechas_[3] = listo && cx_ < 980.0f - 1;
  // EU 0x2F8370 Story_WorldToScreenX
  // EU 0x2F8350 Story_WorldToScreenY
  // EU 0x31B5A0 Story_SpriteApplyWorldScreenPos (mundo -> pantalla: se resta la camara y se suma la mitad del lienzo)
  auto P = [&](float x, float y) { return ImVec2(gi.p0.x + D.x * 0.5f + (x - camx) * s, gi.p0.y + D.y * 0.5f + (y - camy) * s); };
  dl->AddRectFilled(gi.p0, gi.p1, IM_COL32(0, 0, 0, 255));
  // Pinta las Entradas que devuelve ama::Pool: se respeta la ventana de textura en texeles (e.uv), el tinte y el
  // alfa que salen de las pistas del #AMA (el tinte[3] de la pista de color es el alfa de la entrada original).
  auto PintarAma = [&](const std::vector<ama::Entrada>& es, auto T, auto IMG, auto CLAVE, int alfa = 255,
                       bool gris = false) {
    for (const ama::Entrada& e : es) {
      if (e.SinTextura()) continue;
      const Imagen& im = IMG(int(e.textura));
      if (!im) continue;
      const ImVec2 p0 = T(e.pos[0] + e.rect[0], e.pos[1] + e.rect[1]);
      const ImVec2 p1 = T(e.pos[0] + e.rect[2], e.pos[1] + e.rect[3]);
      int a = int(std::clamp(e.tinte[3], 0.0f, 1.0f) * 255.0f + e.rgba[3] + 0.5f);
      a = a * alfa / 255;
      const int r = gris ? 110 : int(std::clamp(e.tinte[0], 0.0f, 1.0f) * 255.0f + 0.5f);
      const int g = gris ? 110 : int(std::clamp(e.tinte[1], 0.0f, 1.0f) * 255.0f + 0.5f);
      const int b = gris ? 110 : int(std::clamp(e.tinte[2], 0.0f, 1.0f) * 255.0f + 0.5f);
      Imagen2(dl, CLAVE(int(e.textura)), im, p0, p1, ImVec4(e.uv[0], e.uv[1], e.uv[2], e.uv[3]),
              IM_COL32(r, g, b, std::clamp(a, 0, 255)));
    }
  };
  const int m = MapaDeSaga(c.saga);
  const bool ama_ok = AsegurarAmaMapa(m);
  if (ama_ok) {
    // EU 0x2F95A0 Story_MapRender: pasada de mascara 1 (las cuatro losetas del mapa). Los objetos 1/61/113/76 del
    // #AMA 334/2 son exactamente las cuatro piezas de 640x480 en (0,0), (640,0), (0,480), (640,480).
    static constexpr int kPiezasTex[4][4] = {{9, 10, 11, 12}, {22, 23, 24, 25}, {48, 49, 50, 51}, {32, 33, 34, 35}};
    auto ImagenMapa = [&](int tex) -> const Imagen& {
      for (int i = 0; i < 4; ++i)
        if (kPiezasTex[m][i] == tex) return in.dm->piezas[m][i];
      return De(in.dm->hud, tex);
    };
    std::vector<ama::Entrada> es;
    ama::Contexto ctx;
    g_ama_mapa.pool.Pases(0, 1, dt * 60.0f, ctx, es);
    PintarAma(es, P, ImagenMapa, [&](int tex) {
      for (int i = 0; i < 4; ++i)
        if (kPiezasTex[m][i] == tex) return 100 + m * 4 + i;   // misma clave de cache que las piezas de siempre
      return 4000 + tex;
    });
  } else {
    for (int i = 0; i < 4; ++i) {   // respaldo si el #AMA no se pudo leer: las piezas del atlas tal cual
      const float x = float(i % 2) * 640, y = float(i / 2) * 480;
      Imagen2(dl, 100 + m * 4 + i, in.dm->piezas[m][i], P(x, y), P(x + 640, y + 480));
    }
  }
  // Nubes del mapa: mascara 2 de Story_MapRender (EU 0x2F95A0, jal 0x1B1A00 con a1=2 en 0x2F97E0). El objeto 0 del
  // #AMA 334/2 (60/112/120 en los otros mapas) lo anima ama::Pool; la posicion es de PANTALLA (600x450), no mapa.
  if (ama_ok) {
    const float nux = D.x / 600.0f, nuy = D.y / 450.0f;
    auto SP = [&](float x, float y) { return ImVec2(gi.p0.x + x * nux, gi.p0.y + y * nuy); };
    dl->PushClipRect(gi.p0, gi.p1, true);   // el original las recorta en el borde de la pantalla
    std::vector<ama::Entrada> es;
    ama::Contexto ctx;
    g_ama_mapa.pool.Pases(0, 2, dt * 60.0f, ctx, es);
    PintarAma(es, SP, [&](int tex) -> const Imagen& { return De(in.dm->hud, tex); }, [](int tex) { return 9000 + tex; });
    dl->PopClipRect();
  }
  // EU 0x2FCCB0 Story_MapObjectsAppearTick
  // Story_MapObjectsAppearTick: tras la intro, cada 20 cuadros (param+0x34 > 20) aparece UNA casilla,
  // desde la ultima de la lista hacia la primera; el sprite entra con su fundido (w_Appear_FadeFlag_ -> animacion 1 de
  // su banco). RE 2026-10-25: en el #AMA 334/4 (insignias, texturas de 334/5) la pista de la animacion 1 de los 58
  // objetos 40..155 ocupa los cuadros 4..90 en todos, asi que el fundido dura 90 cuadros (antes 20, a ojo).
  constexpr float kFundido = 90.0f;
  int n_saga = 0;
  for (const CasillaDM& q : cas)
    if (q.saga == c.saga && q.icono >= 0 && !(q.tipo == 1 && q.mision > in.abiertas) &&
        !(q.tipo == 2 && EsferaRecogidaIW(q.indice, t)))
      ++n_saga;
  int pos_saga = 0;
  const bool hist_ok = AsegurarAmaHistoria(c.saga, cas.size());
  // Casillas de evento (tipos 3/4): son sprites del mismo banco #AMA 334/4 que las insignias. Story_SpawnSpriteNode
  // (EU 0x31B770) toma el #DMD +0x10 como fila de la tabla 0x480750 EU / 0x380020 USA (tipo 3: 100..104) o
  // 0x480780 EU / 0x380050 USA (tipo 4: 110..117), y MiscDisplay(1,A,B,-1,0) recibe el par (aparicion, reposo)
  // 172..181 / 156..171. Se pintan ANTES de las insignias y de Goku: ninguna figura ni el luchador pueden quedar
  // tapados por el arte del evento.
  const auto& evs = in.road ? in.dm->eventos_road : in.dm->eventos;
  if (hist_ok && !evs.empty()) {
    AsegurarAmaEventos(evs.size());
    int n_ev = 0;
    for (const CasillaDM& e : evs)
      if (e.saga == c.saga) ++n_ev;
    int pos_ev = 0;
    for (size_t j = 0; j < evs.size(); ++j) {
      const CasillaDM& e = evs[j];
      if (e.saga != c.saga) continue;
      // Story_MapObjectsAppearTick: una casilla cada 20 cuadros, de la ultima a la primera; los tipo 3 ya entran en
      // LoadWaitGeneralDragonMissionIcons_PlayerCommands (EU 0x2FD0F0) al acabar la intro, sin espera escalonada.
      const int rank = n_ev - 1 - pos_ev++;
      const float t_ap = e.tipo == 3 ? 0.0f : 20.0f * float(rank + 1);
      if (!listo || ap_t_ < t_ap) continue;
      const int objeto = ObjetoEventoIW(e);
      if (objeto < 0) continue;
      int& idx = g_ama_hist.evento[j];
      if (idx < 0) {
        idx = CrearSpriteAma(g_ama_hist.obra, g_ama_hist.pool, objeto, 16, ap_t_ - t_ap);
        REXLOG_INFO("dbz3 iw mapa: evento {:#x} tipo {}: #AMA 334/4 objeto {} (reposo {}) en #DMD ({}, {})", e.clave, e.tipo,
                    objeto, objeto + 1, int(e.x), int(e.y));
      }
      if (idx < 0) continue;
      ama::SpriteEstado& se = g_ama_hist.pool.s[idx];
      // Reposo = objeto impar del par (el B de MiscDisplay). Se cambia al acabar la aparicion, como las insignias.
      if (se.max > 0.0f && ap_t_ - t_ap > se.max && se.id != objeto + 1) {
        const int nd = g_ama_hist.obra.Objeto(uint32_t(objeto + 1));
        if (nd >= 0) {
          se.arbol = ama::CrearArbol(g_ama_hist.obra, nd, 0.0f);
          se.max = float(ama::MaxCuadros(se.arbol));
          se.cuadro = 0.0f;
          se.id = objeto + 1;
        }
      }
      ama::Contexto bc;
      bc.pos[0] = e.x;
      bc.pos[1] = e.y;
      std::vector<ama::Entrada> es;
      ama::Fondo(se.arbol, 1.0f, dt * 60.0f, bc, es);
      PintarAma(es, P, [&](int tex) -> const Imagen& { return De(in.dm->iconos, tex); },
                [](int tex) { return 1000 + tex; });
    }
  }
  // EU 0x2FD3A0 Story_MapObjectVisibility
  for (size_t j = 0; j < cas.size(); ++j) {
    const CasillaDM& q = cas[j];
    if (q.saga != c.saga || q.icono < 0) continue;
    if (q.tipo == 1 && q.mision > in.abiertas) continue;   // las de mas adelante aun no salen
    if (q.tipo == 2 && EsferaRecogidaIW(q.indice, t)) continue;   // esfera ya recogida: su byte queda 0xE0
    // EU 0x2FD580 Story_SpawnMapObjectSprite
    const int rank = n_saga - 1 - pos_saga++;              // la ultima primero (indice count-1 hacia abajo)
    const float t_ap = 20.0f * float(rank + 1);            // una cada 20 cuadros
    const float alfa = listo ? std::clamp((ap_t_ - t_ap) / kFundido, 0.0f, 1.0f) : 0.0f;
    if (alfa <= 0.0f) continue;                            // aun no ha aparecido
    const int al = int(255.0f * alfa + 0.5f);
    const Imagen& im = De(in.dm->iconos, q.icono);
    float w = float(im.w), h = float(im.h);
    if (int(j) == sel_) {   // aro verde (t89) bajo la elegida, que late
      const float r = 40.0f + 5.0f * std::sin(t * 5.0f);
      Imagen2(dl, 1000 + kAro, De(in.dm->iconos, kAro), P(q.x - r, q.y - r), P(q.x + r, q.y + r), {},
              IM_COL32(255, 255, 255, al));
      w *= 1.15f;
      h *= 1.15f;
    }
    const bool no_b3 = q.mision > 0 && q.mision < int(in.motivo.size()) && !in.motivo[size_t(q.mision)].empty();
    // DS62: las insignias de mision (cara 30..87) son el objeto 40+2*(cara-30) del #AMA 334/4. Su animacion de
    // aparicion (90 cuadros: escala con rebote y alfa por la pista de color) la ejecuta ama::Pool; el objeto impar
    // siguiente es el reposo de 1 cuadro. Los minijuegos/esferas sin insignia siguen con la textura de 334/5.
    const bool insignia = hist_ok && q.tipo == 1 && q.cara >= 30 && q.cara < 88 && j < g_ama_hist.sprite.size();
    if (insignia) {
      int& idx = g_ama_hist.sprite[j];
      const int objeto = 40 + 2 * (q.cara - 30);
      if (idx < 0)   // primera vez que se ve: entra por el cuadro que le toca (aparicion escalonada)
        idx = CrearSpriteAma(g_ama_hist.obra, g_ama_hist.pool, objeto, 8, std::min(ap_t_ - t_ap, 89.0f));
      if (idx >= 0) {
        ama::SpriteEstado& se = g_ama_hist.pool.s[idx];
        if (ap_t_ - t_ap > 90.0f && se.id != objeto + 1) {   // aparicion acabada: objeto de reposo
          const int nd = g_ama_hist.obra.Objeto(uint32_t(objeto + 1));
          if (nd >= 0) {
            se.arbol = ama::CrearArbol(g_ama_hist.obra, nd, 0.0f);
            se.max = float(ama::MaxCuadros(se.arbol));
            se.cuadro = 0.0f;
            se.id = objeto + 1;
          }
        }
        ama::Contexto bc;
        bc.pos[0] = q.x;
        bc.pos[1] = q.y;
        std::vector<ama::Entrada> es;
        ama::Fondo(se.arbol, 1.0f, dt * 60.0f, bc, es);
        PintarAma(es, P, [&](int tex) -> const Imagen& { return De(in.dm->iconos, tex); }, [](int tex) { return 1000 + tex; },
                  255, no_b3);
      }
    } else {
      const ImU32 tint = no_b3 ? IM_COL32(110, 110, 110, al) : IM_COL32(255, 255, 255, al);
      Imagen2(dl, 1000 + q.icono, im, P(q.x - w * 0.5f, q.y - h * 0.5f), P(q.x + w * 0.5f, q.y + h * 0.5f), {}, tint);
    }
    if (q.mision > 0 && q.mision <= in.hecho && !no_b3) {   // superada: estrella de 334/1 t31
      Imagen2(dl, 3000 + kEstrella, De(in.dm->ui, kEstrella), P(q.x + w * 0.5f - 22, q.y - h * 0.5f - 6),
              P(q.x + w * 0.5f + 2, q.y - h * 0.5f + 18), {}, IM_COL32(255, 255, 255, al));
    } else if (q.mision > 0 && !no_b3) {   // abierta y sin hacer: "New!" (t16)
      const float b = 2.0f * std::sin(t * 4.0f);
      Imagen2(dl, 1000 + kNew, De(in.dm->iconos, kNew), P(q.x - 26, q.y - h * 0.5f - 34 + b), P(q.x + 26, q.y - h * 0.5f + 4 + b),
              {}, IM_COL32(255, 255, 255, al));
    }
  }
  // Goku del mapa, de pie junto a la casilla: ~21 % del alto de la pantalla, como el original. En 3D (su modelo de
  // DATA_CMN 1093, andando mientras va a la casilla); sin el, el dibujo t90 a 4x con Scale2x
  const float gh = 0.21f * 480.0f / kZoom, gw = gh * 0.8f, gx = mx_ - 48, gy = my_ + 34;
  const bool anda = std::hypot(c.x - mx_, c.y - my_) > 2.0f;
  dl->AddEllipseFilled(P(gx, gy), ImVec2(gw * 0.4f * s, gw * 0.12f * s), IM_COL32(0, 0, 0, 90));
  // el Goku 3D de IW (1093): con la piel, las texturas opacas y los pesos bien leidos (auditoria10/B y E) sale como el
  // original; DBZ3_IW_SIN_GOKU3D=1 vuelve al dibujo 2D
  static const bool goku3d = std::getenv("DBZ3_IW_SIN_GOKU3D") == nullptr;
  if (!goku3d || !DibGokuMapa(dl, P(gx, gy), gh * s, t, anda, c.x - mx_, c.y - my_)) {
    const float bob = std::fabs(std::sin(t * 7.0f)) * 2.0f;
    Imagen2(dl, 8000, in.dm->goku, P(gx - gw * 0.5f, gy - gh - bob), P(gx + gw * 0.5f, gy - bob));
  }
  // Fundido del cambio de saga (EU 0x2FDF80 -> s_Start_Stage_Out -> Transition_StartWithCallback(10,
  // Story_MapExitWithFade 0x2FDC80), a Transition_SetSpeed(fRam0049c7dc * 0,02) = 0,02 por cuadro (50 cuadros).
  if (cambio_fase_ >= 2) {
    // EU 0x27DAB0 Transition_SetSpeed (0,02 por cuadro)
    const float f = cambio_fase_ == 2 ? std::min(cambio_t_ / 50.0f, 1.0f) : std::max(1.0f - cambio_t_ / 50.0f, 0.0f);
    dl->AddRectFilled(gi.p0, gi.p1, IM_COL32(0, 0, 0, int(255.0f * f)));
  }
}

// EU 0x2FE270 GeneralMapScripts_MissionDots_Locations_Icons_BulmaTextScripts
// EU 0x3655C0 Story_MapSetupScreen: prepara la pantalla del menu START (fundido, sprites de la interfaz y estado).
// EU 0x365110 Story_MapDrawMenu: dibuja la interfaz del menu y deja el tick en Story_MapHandleStartButton.
// EU 0x365BC0 MemCard_StorySaveMenuTick: dos opciones (GUARDAR / SALIR), arriba/abajo mueven el cursor, A confirma
// (0x4000) y B cancela (0x1000; en el original Transition_SetupWithParams(5,...) y vuelve al mapa). El guardado lo
// hace MemCardDlg_Start (EU 0x2D75F0), que en el port es el registro de la historia escrito en progreso.toml.
PantallaDM::Accion PantallaDM::MenuMapa(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado,
                                        const InfoDM& in) {
  DibujarFondo(dl, io, t, in);
  if (menu_mapa_ == 4) {
    // EU 0x365900 MemCard_StorySaveTick (opcion 1): FUN_00100260(..., 30.0) = 30 cuadros antes de la transicion
    // al mapa (Transition_SetupWithParams(0xF,...)); durante la espera se ve el mapa, sin el panel del menu.
    if (t - t_menu_t0_ > 30.0f / 60.0f) menu_mapa_ = 0;
    return kNada;
  }
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 176));
  const auto a = S::GameArea(io);
  const auto& ui = in.dm->ui;
  ImVec2 p0, p1;
  dl->AddRectFilled(a.At(360, 120), a.At(920, 760), IM_COL32(6, 22, 40, 255), 14 * a.s);
  Imagen2(dl, 3000 + kPanel, De(ui, kPanel), a.At(360, 120), a.At(920, 760));
  S::MenuText(dl, a.At(640, 190), S::mode::kHead * a.s, T::T("PAUSA", "PAUSE"), true, false, 500 * a.s);
  constexpr uint16_t kY = 0x8000;   // triangulo: el port mantiene aqui el brillo de DS55 (EU 0x2CEF20); no es del menu original
  if (menu_mapa_ == 1) {
    // EU 0x365BC0 MemCard_StorySaveMenuTick: el NUMERO de opciones depende de cRam005c1d88 (1 si aun no se ha
    // guardado, 2 despues). El orden del C: opcion 0 -> guardar (rows 0xB/0xE), opcion 1 -> volver al mapa
    // (rows 0xC/0xD; MemCard_StorySaveTick, EU 0x365900, hace Transition_SetupWithParams(0xF,...)). Antes el port
    // ofrecia tres opciones con BRILLO en medio; el brillo pasa a Y para no perderlo (pantalla de Opciones no portada).
    const bool ya_guardado = LeerProgreso("iw_guardado", 0) > 0;
    const int n_op = ya_guardado ? 2 : 1;
    if (pulsado & (kUp | kDown))
      sel_mapa_ = (sel_mapa_ + ((pulsado & kUp) ? n_op - 1 : 1)) % n_op;
    sel_mapa_ = std::clamp(sel_mapa_, 0, n_op - 1);
    const char* opt[2] = {T::T("GUARDAR", "SAVE"), T::T("VOLVER AL MAPA", "BACK TO MAP")};
    for (int i = 0; i < n_op; ++i) {
      const float y = 360.0f + 96.0f * float(i);
      Imagen2(dl, 3000 + 11, De(ui, 11), a.At(500, y - 26), a.At(780, y + 26),
              ImVec4(0, 40.0f * float(i), 208, 40.0f * float(i + 1)),
              sel_mapa_ == i ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 120));
      if (sel_mapa_ == i)
        Imagen2(dl, 3000 + kFlecha, De(ui, kFlecha), a.At(466, y - 14), a.At(494, y + 14), ImVec4(24, 0, 0, 24));
      S::MenuText(dl, a.At(640, y), S::mode::kHead * a.s, opt[i], true, false, 280 * a.s);
    }
    S::mode::Help(dl, a, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Menú principal", "Main menu")},
                          {"START", T::T("Volver al mapa", "Back to map")}, {"Y", T::T("Brillo", "Brightness")}});
    // EU 0x365090 Story_MapHandleStartButton: con el menu abierto cierra el estado (Story_PopMapStateAndReload) y,
    // si el scroll sigue pendiente (ctx+0x4A0 < 1), asigna el tick de scroll. El fundido de texto (0x400) es cosmetico.
    if (pulsado & kStart) {
      menu_mapa_ = 0;
      if (tcam_ < 1.0f || bloqueo_scroll_) {
        tcam_ = 0;
        bloqueo_scroll_ = true;
      }
      REXLOG_INFO("dbz3 iw mapa: START en el menu: Story_PopMapStateAndReload (EU 0x365090)");
      return kNada;
    }
    if (pulsado & kY) {   // port: la pantalla de Opciones de IW no existe; DS55 deja el brillo aqui (EU 0x2CEF20)
      brillo_ = LeerProgreso("iw_brillo", 26);
      brillo_abierto_ = true;
      REXLOG_INFO("dbz3 iw brillo: abrir (valor {})", brillo_);
      return kNada;
    }
    if (pulsado & kA) {
      if (sel_mapa_ == 0) {
        // EU 0x2D75F0 MemCardDlg_Start: el original abre el dialogo de tarjeta en modo guardado; el port escribe el
        // bloque del registro en su fichero de progreso (GuardarProgreso) y muestra el aviso.
        guardado_antes_ = ya_guardado;
        ActualizarRegistro(in, true);
        GuardarProgreso("iw_guardado", 1);   // cRam005c1d88 = 1: a partir de aqui el menu ofrece 2 opciones
        REXLOG_INFO("dbz3 iw mapa: menu START: partida guardada (MemCardDlg_Start EU 0x2D75F0)");
        menu_mapa_ = 2;
        t_menu_t0_ = t;
      } else {
        // EU 0x365900 MemCard_StorySaveTick (opcion 1): ScreenTransition_PushCommands + Transition a la escena 0xF
        // (el mapa) tras esperar 30 cuadros; el port vuelve al mapa tras esa espera.
        menu_mapa_ = 4;
        t_menu_t0_ = t;
        REXLOG_INFO("dbz3 iw mapa: menu START: volver al mapa (Transition_SetupWithParams(0xF), EU 0x365900)");
      }
    }
    if (pulsado & kB) {   // 0x1000: Transition_SetupWithParams(5,0,10,0) = menu principal
      REXLOG_INFO("dbz3 iw mapa: menu START: B = Transition_SetupWithParams(5) (menu principal)");
      return kSalir;
    }
  } else if (menu_mapa_ == 2) {
    S::MenuText(dl, a.At(640, 430), S::mode::kHead * a.s, T::T("GUARDANDO...", "SAVING..."), true, false, 400 * a.s);
    if (t - t_menu_t0_ > 0.35f) {
      menu_mapa_ = 3;
      t_menu_t0_ = t;
    }
  } else {
    S::MenuText(dl, a.At(640, 430), S::mode::kHead * a.s, T::T("PARTIDA GUARDADA", "GAME SAVED"), true, false, 400 * a.s);
    if (t - t_menu_t0_ > 0.9f) {
      // MemCard_StorySaveWaitResult (1.a vez) vuelve al mapa (transicion 0xF); MemCard_StorySaveHandleResult
      // (siguientes) vuelve a este menu.
      menu_mapa_ = guardado_antes_ ? 1 : 0;
      sel_mapa_ = 0;
    }
  }
  return kNada;
}

PantallaDM::Accion PantallaDM::Mapa(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in) {
  // EU 0x2B13B0 DragonMissionCutscenesFuncLink: la transicion a Historia usa el reproductor del port.
  if (!in.dm) return kNada;
  // Pantallas DS55: tapan el mapa por completo y llevan su propio mando (se dibujan antes de todo lo demas).
  if (edit_abierto_) return PantallaCapsulas(dl, io, t, pulsado, in);
  if (brillo_abierto_) return PantallaBrillo(dl, io, t, pulsado, in);
  if (shenron_fase_ >= 0) return PantallaShenron(dl, io, t, pulsado, in);
  // Deseo de Shenron: el original lo abre al volver de un combate con las 7 Bolas (LoadWaitDragonMissionMinigame_Battle
  // 0x289C20: bRam005c1d78 & 0x7F == 0x7F y salida 0xF -> 0xD; ShenronSystemFunc 0x2AF360 -> CapsulePick). En el port
  // la mascara es `iw_bolas` y el deseo se apunta en `iw_deseo` (un deseo por partida).
  if (!in.road && video_ < 0 && menu_mapa_ == 0 && !deseo_postpuesto_ && LeerProgreso("iw_deseo", 0) == 0) {
    if (t - t_deseo_ > 0.5f) {
      t_deseo_ = t;
      deseo_bolas_ = LeerProgreso("iw_bolas", 0) & 0x7F;
      deseo_hecho_ = in.hecho;
    }
    if (deseo_bolas_ == 0x7F && deseo_hecho_ > 0) {
      AbrirShenron(in);
      if (shenron_fase_ >= 0) return PantallaShenron(dl, io, t, pulsado, in);
      deseo_postpuesto_ = true;   // no quedan capsulas que desear: no repetir la comprobacion en cada cuadro
    }
  }
  if (!env_mision_aplicada_) {
    env_mision_aplicada_ = true;
    if (!in.road) {
      if (const char* valor = std::getenv("DBZ3_IW_MISION")) {
        char* fin = nullptr;
        const long mision = std::strtol(valor, &fin, 10);
        if (fin != valor && *fin == '\0' && mision >= 1 && mision <= kMisiones) {
          Elegir(in, static_cast<int>(mision));
          REXLOG_INFO("dbz3 iw: prueba: DBZ3_IW_MISION={} seleccionado desde entorno", mision);
        } else {
          REXLOG_WARN("dbz3 iw: DBZ3_IW_MISION invalido (usa 1..{}): {}", kMisiones, valor);
        }
      }
    }
  }
  // DS104: video final del minijuego al volver al mapa, como la salida normal del original (EU 0x20D800 consulta la
  // tabla de despues sin mirar el resultado). Se salta si pasarela_misiones ya lo pidio (mismo indice en video_).
  if (mini_final_ >= 0 && video_ == mini_final_) mini_final_ = -1;
  if (mini_final_ >= 0 && video_ < 0 && !dbz3::iwmini::Activo() && mini_final_t0_ >= 0 &&
      t - mini_final_t0_ > 2.0f) {
    const int v = mini_final_;
    mini_final_ = -1;
    const std::filesystem::path ruta = RutaVideoIW(*in.dm, v);
    REXLOG_INFO("dbz3 misiones: iw: video final {} ({}) al volver del minijuego (EU 0x20D800; tabla despues "
                "EU 0x473090)", v, ruta.filename().string());
    Contar(v);
  }
  if (video_ >= 0) return Historia(dl, io, t, pulsado, in);
  if (menu_mapa_ != 0) return MenuMapa(dl, io, t, pulsado, in);   // el menu pinta el mapa de fondo
  const float dt = std::clamp(t - t_ultimo_, 0.0f, 0.1f);   // para el globo (ama::Pool); DibujarFondo actualiza t_ultimo_
  DibujarFondo(dl, io, t, in);
  if (sel_ < 0) return pulsado & kB ? kSalir : kNada;
  // Story_MapIntroDelayTick: el original no carga los mandos del mapa hasta acabar la espera (101 cuadros, > 1,0) y el
  // scroll. RE 2026-10-25 (EU 0x2F8390): el scroll suma 0,01 por cuadro y, si la camara esta a menos de 2 px del
  // objetivo (lo normal al entrar: Goku esta en su casilla), pone t = 1,0 y en ese mismo cuadro pasa de 1,0 y llama a
  // LoadWaitPlayerCommands: el bloqueo del scroll es de 1-2 cuadros, no del tramo recorrido.
  // EU 0x365090 Story_MapHandleStartButton: con START y el scroll pendiente (ctx+0x4A0 < 1) pasa a
  // Story_MapScrollToTargetTick; en el port eso es dar la entrada por terminada y recargar los mandos. Con el mapa
  // libre, START abre el menu (Story_MapSetupScreen/Story_MapDrawMenu, comandos 0xB/0xC de la cola).
  if (EnIntroMapa() || bloqueo_scroll_) {
    if (pulsado & kStart) {
      t_intro_ = 1.0f;
      bloqueo_scroll_ = false;
      REXLOG_INFO("dbz3 iw mapa: START durante la entrada: scroll al objetivo (EU 0x365090)");
    }
    return pulsado & kB ? kSalir : kNada;
  }
  if (cambio_fase_ != 0) pulsado = 0;   // cambio de saga: mandos descargados (el HUD se sigue dibujando)
  const auto& cas = Cas(in);
  const CasillaDM c = cas[size_t(sel_)];
  auto abierta = [&](const CasillaDM& q) {
    if (q.tipo == 2) return !EsferaRecogidaIW(q.indice, t);   // las esferas de cristal tambien se eligen
    return q.tipo == 0 || (q.mision > 0 && q.mision <= in.abiertas);
  };
  const int antes = sel_;
  if (pulsado & (kUp | kDown | kLeft | kRight)) {   // la abierta mas cerca en esa direccion
    // EU 0x2F8200 Story_PickObjectAtCursor (el original elige el nodo bajo el cursor proyectado; aqui, la casilla
    // abierta mas cercana en la direccion de la cruceta: mismos criterios de "abierta" y de tipo elegible)
    float ux = (pulsado & kLeft) ? -1.0f : (pulsado & kRight) ? 1.0f : 0.0f;
    float uy = (pulsado & kUp) ? -1.0f : (pulsado & kDown) ? 1.0f : 0.0f;
    const float l = std::hypot(ux, uy);
    ux /= l;
    uy /= l;
    int mejor = -1;
    float md = 1e9f;
    for (size_t j = 0; j < cas.size(); ++j) {
      const CasillaDM& q = cas[j];
      if (int(j) == sel_ || q.saga != c.saga || !abierta(q)) continue;
      const float dx = q.x - c.x, dy = q.y - c.y, along = dx * ux + dy * uy, perp = std::fabs(dx * uy - dy * ux);
      if (along <= 1.0f) continue;
      if (along + 2.0f * perp < md) {
        md = along + 2.0f * perp;
        mejor = int(j);
      }
    }
    if (mejor >= 0) sel_ = mejor;
  }
  if (pulsado & (kLB | kRB)) {   // otra saga: su mision abierta mas alta
    // EU 0x2FDF80 DragonMissionScenarioChange_L1/R1: L1 empuja la orden 2 con argumento 1 (180 cuadros de salida) y
    // R1 con 2 (240); el cambio de saga lo aplica el fundido, no el cursor. El destino es la mision abierta mas alta
    // de la saga siguiente (el original mueve el area con DAT_0049C348/50, 6 areas en Dragon Mission).
    const int dir = (pulsado & kRB) ? 2 : 1;
    for (int paso = 1; paso <= 6; ++paso) {
      const int saga = ((c.saga + ((pulsado & kRB) ? paso : -paso)) % 6 + 6) % 6;
      int mejor = -1;
      for (size_t j = 0; j < cas.size(); ++j) {
        if (cas[j].saga == saga && cas[j].tipo == 1 && abierta(cas[j]) &&
            (mejor < 0 || cas[j].mision > cas[size_t(mejor)].mision))
          mejor = int(j);
      }
      if (mejor >= 0) {
        cambio_saga_ = saga;
        cambio_mision_ = mejor;
        EmpujarOrden(2, dir);
        DespacharOrden(in);   // arranca ya el estado del cambio (como el despachador del original)
        aviso_.clear();
        break;
      }
    }
    pulsado = 0;   // el cambio de saga se lleva el mando de este cuadro
  }
  if (sel_ != antes) aviso_.clear();
  // Si se acaba de recoger la esfera del cursor, pasa a la casilla abierta mas cercana de la saga (la recogida deja
  // de existir en Story_MapObjectVisibility).
  if (sel_ >= 0 && cas[size_t(sel_)].tipo == 2 && EsferaRecogidaIW(cas[size_t(sel_)].indice, t)) {
    int mejor = -1;
    float md = 1.0e9f;
    for (size_t j = 0; j < cas.size(); ++j) {
      const CasillaDM& x = cas[j];
      if (int(j) == sel_ || x.saga != c.saga || !abierta(x)) continue;
      const float d = std::hypot(x.x - c.x, x.y - c.y);
      if (d < md) md = d, mejor = int(j);
    }
    if (mejor >= 0) sel_ = mejor;
  }
  constexpr uint16_t kBack = 0x0020;   // SELECT: la guia
  if (pulsado & kBack) guia_ = !guia_;
  const CasillaDM& q = cas[size_t(sel_)];
  // HUD del mapa con las piezas de 334/3, en px de la ventana (u = 1 px de la pantalla de 448 de la PS2)
  const S::Imagen gi = S::GuestImage(io);   // L05: HUD sobre la imagen del juego
  const ImVec2 D = gi.Size();
  const float u = D.y / 448.0f;
  auto H = [&](int k) -> const Imagen& { return De(in.dm->hud, k); };
  auto pon = [&](int k, ImVec2 p0, ImVec2 p1, ImVec4 src = ImVec4(0, 0, 0, 0)) { Imagen2(dl, 7000 + k, H(k), ImVec2(gi.p0.x + p0.x, gi.p0.y + p0.y), ImVec2(gi.p0.x + p1.x, gi.p0.y + p1.y), src); };
  // flechas azules en los bordes hacia donde se puede desplazar (t17: arriba x 4-60 y 28-59; derecha x 69-101 y 7-63)
  const float lat = 3.0f * u * (0.5f + 0.5f * std::sin(t * 5.0f)), al = 46 * u, an = 26 * u;
  if (flechas_[0]) pon(kFlechas, ImVec2(D.x * 0.5f - al * 0.5f, 6 * u - lat), ImVec2(D.x * 0.5f + al * 0.5f, 6 * u + an - lat), ImVec4(4, 28, 61, 60));
  if (flechas_[1]) pon(kFlechas, ImVec2(D.x * 0.5f - al * 0.5f, D.y - 6 * u - an + lat), ImVec2(D.x * 0.5f + al * 0.5f, D.y - 6 * u + lat), ImVec4(4, 60, 61, 28));
  if (flechas_[2]) pon(kFlechas, ImVec2(6 * u - lat, D.y * 0.5f - al * 0.5f), ImVec2(6 * u + an - lat, D.y * 0.5f + al * 0.5f), ImVec4(102, 7, 69, 64));
  if (flechas_[3]) pon(kFlechas, ImVec2(D.x - 6 * u - an + lat, D.y * 0.5f - al * 0.5f), ImVec2(D.x - 6 * u + lat, D.y * 0.5f + al * 0.5f), ImVec4(69, 7, 102, 64));
  // EU 0x2FFD30 Sprite_DisplayHudSetByMode
  // arriba a la izquierda: el robot con la cinta (t00) y el nombre de la saga en letras rojas
  const ImVec2 r0(8 * u, 8 * u);
  pon(kRobot, r0, ImVec2(r0.x + 304 * u, r0.y + 88 * u));
  {
    const int k = kNombreSaga[std::clamp(q.saga, 0, 5)];
    const Imagen& nombre = in.road ? De(in.dm->road, 0) : H(k);   // Fighter's Road: su logo (333/0 t00)
    const float w = float(nombre.w) * 0.85f * u, h = float(nombre.h) * 0.85f * u, x = r0.x + 196 * u, y = r0.y + 42 * u;
    if (in.road) Imagen2(dl, 4000, nombre, ImVec2(x - w * 0.5f, y - h * 0.5f), ImVec2(x + w * 0.5f, y + h * 0.5f));
    else pon(k, ImVec2(x - w * 0.5f, y - h * 0.5f), ImVec2(x + w * 0.5f, y + h * 0.5f));
  }
  // (Gemini/OPUS iw_mision_1: el mapa de IW no muestra el indicador "L2 R2 Saga"; el cambio de saga sigue en LB/RB
  // pero sin el cartel encima del mapa. El resto del HUD es el del original: mapa 334/3, robot/globo, Bulma y Bolas.)
  // EU 0x2FF7C0 BulmaSprite_SagaTitleNames
  // [H] el original pinta DOS objetos animados del #AMA 334/2 (Bulma y el titulo); el port dibuja las texturas estaticas
  // de 334/3 (kBulma/kNombreSaga): el fotograma es el mismo, pero no se muestrea su pista de animacion.
  // abajo a la derecha: Bulma (la guia) asomada en su vehiculo
  float bw = 0;
  // EU 0x2FF480 DragonMissionBulmaSpriteVisible
  if (guia_) {
    const Imagen& bu = H(kBulma[std::clamp(q.saga, 0, 5)]);
    const float bh = 0.40f * D.y;
    bw = float(bu.w) * bh / float(std::max(bu.h, 1));
    pon(kBulma[std::clamp(q.saga, 0, 5)], ImVec2(D.x - bw - 4 * u, D.y - bh * 0.94f), ImVec2(D.x - 4 * u, D.y + bh * 0.06f));
  }
  // abajo a la izquierda: barra con las 7 Bolas (t19, huecos cada 40 px desde x 32), CLEAR (t69) y hechas/total
  int total = 0, hechas = 0;
  for (const CasillaDM& x : cas) {
    if (x.saga != q.saga || x.tipo > 1) continue;
    ++total;
    const bool no = x.mision > 0 && x.mision < int(in.motivo.size()) && !in.motivo[size_t(x.mision)].empty();
    hechas += x.mision > 0 && x.mision <= in.hecho && !no;
  }
  // EU 0x2FFAB0 CloudSprites_SavedDragonballs_WaitDrawSpawnFunc
  // Bolas del mapa: la barra del original son los 7 bits de las Bolas guardadas (CloudSprites_SavedDragonballs, EU
  // 0x2FFAB0: bRam005c1d78, un sprite por bit). El bit lo enciende el resultado de cada combate con un OR aleatorio
  // (Minigame_StartButton_PauseMenu 0x289880 -> +0x16c |= tabla[RNG % 7]; Battle_ApplyResultRewards 0x28A020 copia
  // el byte a bRam005c1d78) y al cerrar la historia FUN_00133cd0(7) la deja en 0x7F. GanarIW() guarda esa mascara en
  // `iw_bolas` (una Bola nueva por mision ganada, las 7 al acabar la 96). La lectura va cacheada 0,25 s: leer el
  // progreso abre el fichero y esto se dibuja cada cuadro.
  static int bolas_mask = 0;
  static float t_bolas = -1.0e9f;
  if (t - t_bolas > 0.25f) {
    t_bolas = t;
    bolas_mask = in.road ? 0 : LeerProgreso("iw_bolas", 0) & 0x7F;
  }
  const float k = 0.9f * u;
  const ImVec2 b0(8 * u, D.y - 8 * u - 56 * k);
  pon(kBarraBolas, b0, ImVec2(b0.x + 304 * k, b0.y + 56 * k));
  // Un sprite por bit encendido y con SU fotograma (el bit i = la Bola i+1 del atlas, 40 px cada una): si solo se
  // tienen la 1 y la 3, el original pinta esas dos, no las dos primeras.
  int bolas = 0;
  for (int i = 0; i < 7; ++i) {
    if (!((bolas_mask >> i) & 1)) continue;
    const float cx = b0.x + (32 + 40 * float(bolas)) * k, cy = b0.y + 28 * k;
    pon(kBolas, ImVec2(cx - 22 * k, cy - 20 * k), ImVec2(cx + 18 * k, cy + 20 * k),
        ImVec4(40.0f * float(i), 0, 40.0f * float(i) + 40, 40));
    ++bolas;
  }
  const float cx0 = b0.x + 304 * k + 6 * u, cy = b0.y + 28 * k;
  pon(kClear, ImVec2(cx0, cy - 20 * k), ImVec2(cx0 + 88 * k, cy + 20 * k));
  char cuenta[16];
  FormatearTextoIW(cuenta, sizeof(cuenta), "%02d/%02d", hechas, total);
  Cifras(dl, false, cuenta, ImVec2(gi.p0.x + cx0 + 88 * k + 6 * u + 5 * 0.9f * 24 * k, gi.p0.y + cy), 24 * k, in, true);
  // globo de la guia (334/3 t01) a lo ancho, encima de la barra, con la frase original de la casilla (334/6)
  const bool no_b3 = q.mision > 0 && q.mision < int(in.motivo.size()) && !in.motivo[size_t(q.mision)].empty();
  const float gx0 = 8 * u, gx1 = std::max(D.x * 0.5f, D.x - std::max(bw * 0.7f, 60 * u)), gy1 = b0.y - 4 * u, gy0 = gy1 - 104 * u;
  // EU 0x2FF600 Story_SetupSagaTitleAndPop
  if (guia_) {
    // DS62: el globo es el objeto 16 del #AMA 334/2 (rect -424..0 x 0..120, textura 1). Lo muestrea ama::Pool
    // (mascara 4) y se pinta en la caja del HUD del port; si el #AMA no esta, queda la textura directa de 334/3.
    // 25/10 (medido en el video original, Aq0WlA3UZiU 46 s: dentro del globo RGB 200/209/206 sobre un mapa de
    // 108/140/130 justo encima = textura blanca con alfa ~0,6): el juego pinta t01 TRANSLUCIDO (se ve el mapa detras);
    // la textura es opaca, asi que el alfa va en el color del sprite (0x50/0x80 = 0,625).
    bool pintado = false;
    if (CargarAma() && g_ama_mapa.obra_ok) {
      if (g_ama_mapa.globo < 0 && g_ama_mapa.creado)
        g_ama_mapa.globo = CrearSpriteAma(g_ama_mapa.obra, g_ama_mapa.pool, 16, 4);
      std::vector<ama::Entrada> es;
      ama::Contexto ctx;
      g_ama_mapa.pool.Pases(0, 4, dt * 60.0f, ctx, es);
      for (const ama::Entrada& e : es) {
        if (e.SinTextura()) continue;
        const Imagen& im = De(in.dm->hud, e.textura);
        if (!im) continue;
        Imagen2(dl, 7000 + int(e.textura), im, ImVec2(gi.p0.x + gx0, gi.p0.y + gy0),
                ImVec2(gi.p0.x + gx1, gi.p0.y + gy1), ImVec4(e.uv[0], e.uv[1], e.uv[2], e.uv[3]),
                IM_COL32(255, 255, 255, 160));
        pintado = true;
      }
    }
    if (!pintado)
      Imagen2(dl, 7000 + kGloboMapa, H(kGloboMapa), ImVec2(gi.p0.x + gx0, gi.p0.y + gy0),
              ImVec2(gi.p0.x + gx1, gi.p0.y + gy1), ImVec4(0, 0, 0, 0), IM_COL32(255, 255, 255, 160));
    const Imagen& fr = De(in.road ? in.dm->frases_road : in.dm->frases, q.fila);
    if (fr.w > 16) {
      ImVec2 p0, p1;
      Encajar(float(fr.w), float(fr.h), gi.p0.x + (gx0 + gx1) * 0.5f - 14 * u, gi.p0.y + (gy0 + gy1) * 0.5f, gx1 - gx0 - 90 * u, 80 * u,
              1.4f * u, p0, p1);
      Imagen2(dl, 2000 + q.fila, fr, p0, p1);
    }
  }
  const std::string nota = !aviso_.empty() ? aviso_ : no_b3 ? in.motivo[size_t(q.mision)] : std::string();
  if (!nota.empty()) S::MenuText(dl, ImVec2(gi.p0.x + (gx0 + gx1) * 0.5f, gi.p0.y + gy0 - 14 * u), 22 * u, nota.c_str(), false, true, gx1 - gx0);
  constexpr uint16_t kY = 0x8000;   // Y: Fighter's Road (o de vuelta a Dragon Mission)
  if (pulsado & kY) return in.road ? kSalir : kRoad;
  // Gemini/OPUS iw_mision_1: en el mapa de IW no hay acceso ni cartel de "Fighter's Road" (es un modo del menu
  // principal); la linea de ayuda del original solo lleva guia/elegir/salir. La tecla Y sigue disponible para no
  // cerrar el acceso al modo en el port, pero ya no se anuncia.
  S::mode::Help(dl, S::GameArea(io), {{"BACK", T::T("Guía", "Guide")}, {"A", T::T("Elegir", "Select")},
                                                             {"B", in.road ? T::T("Volver", "Back") : T::T("Salir", "Quit")}});
  if (pulsado & kB) return kSalir;
  // START del mapa libre: abre el menu de pausa/guardado (EU 0x365090 -> comandos 0xB/0xC de la cola).
  if (pulsado & kStart) {
    if (cambio_fase_ == 0) {
      // EU 0x3655C0 Story_MapSetupScreen: fundido y MenuPopUpSound(0,0,0x17,0) al abrir. El sonido es del banco
      // del IOP (AMSOUND.IRX), que no esta en el AFS de la copia: solo se registra (como los de _StartUnlock___Func).
      menu_mapa_ = 1;
      sel_mapa_ = 0;
      REXLOG_INFO("dbz3 iw mapa: menu START: Story_MapSetupScreen (EU 0x3655C0), sonido 0x17 del banco IOP [H]");
    }
    return kNada;
  }
  if (pulsado & kA) {
    if (q.tipo == 2) {   // esfera: GeneralMapScripts (EU 0x2FE270)
      // Original: marca 0xC0 en DragonMissionStoryModeBattlePointCapsules_Moderation (la esfera deja de salir),
      // ExecuteScript_MMI2 (desbloqueo del guion, no esta en el C: [H]), CopyRecordToEngineBuffer y
      // Transition_SetupWithParams(0x11, +0x1C, 9, 0). DS104: el destino 0x11/0x12 SI es la escena de cine
      // (LoadWaitEndDragonMissionSystemFunc -> Scene_GotoCutscenes -> DragonMissionSelectionLinkFunc ->
      // Movie_OpenContext(uRam0049cbec)), y el parametro es el +0x1C, que Movie_OpenContext usa directo como indice
      // de los 22 nombres (0x481960 + i*0x100): la marca reproduce el SFD de su indice. El port guarda la mascara de
      // 22 bits en `iw_esferas`, pero primero reproduce ese SFD (Historia) y marca la esfera al acabar.
      if (q.indice < 0 || q.indice >= 22) {
        aviso_ = T::T("Esfera sin indice de escena.", "Crystal ball without a scene index.");
      } else if (EsferaRecogidaIW(q.indice, t)) {
        aviso_ = T::T("Esfera ya recogida.", "Crystal ball already collected.");
      } else if (const int v = VideoEscenaIW(*in.dm, q.indice); v >= 0) {
        const std::filesystem::path ruta = v < int(in.dm->videos.size()) ? in.dm->videos[size_t(v)] : std::string();
        REXLOG_INFO("dbz3 iw mapa: esfera {:#x} (indice de escena {}): SFD {} ({}) antes de marcarla "
                    "(EU 0x2FE270 -> 0x11; Movie_OpenContext 0x3393B0; tabla de nombres 0x481960)",
                    q.clave, q.indice, v, ruta.filename().string());
        esfera_video_ = q.indice;
        Contar(v);
        return kNada;
      } else {
        MarcarEsferaIW(q.indice, t);
        ActualizarRegistro(in);
        aviso_ = T::T("Esfera de cristal recogida.", "Crystal ball collected.");
        REXLOG_INFO("dbz3 iw mapa: esfera {:#x} (indice {}) recogida: batalla 0xC0 en su capsula, registro guardado; "
                    "guion MMI2 [H]", q.clave, q.indice);
      }
    } else if (q.tipo == 0) {   // minijuego: lo hace el frente P4 (RegistrarMinijuegoIW)
      if (dbz3::ingame::tutorial::MostrarMinijuego(q.clave)) return kNada;   // F3: la primera vez, sus controles
      // EU 0x289C20 LoadWaitDragonMissionMinigame_Battle: si la tabla de antes (EU 0x473030) tiene (tipo 0, +0x1C),
      // el original pasa por la escena y solo despues entra al minijuego (target 0x10 = Scene_GotoMinigames). Es
      // decir, el video va ANTES del READY del minijuego. DS104: se resuelven las 18 claves (0x7D0 -> FD01A previo;
      // 0x7D9 -> FD08B final); las otras 16 no tienen fila tipo 0 en ninguna de las dos tablas del ELF.
      const VideosMinijuegoIW vv = VideosDeMinijuegoIW(*in.dm, q.clave);
      if (vv.previo >= 0 && g_minijuego) {
        const int v = vv.previo;
        const std::filesystem::path ruta = v < int(in.dm->videos.size()) ? in.dm->videos[size_t(v)] : std::string();
        REXLOG_INFO("dbz3 misiones: iw: minijuego {:#x} (indice {}): video previo {} ({}) antes de READY "
                    "(EU 0x289C20; tabla antes EU 0x473030)", q.clave, q.indice, v, ruta.filename().string());
        esfera_video_ = -1;
        mini_final_ = vv.final;
        mini_final_t0_ = t;
        Contar(v);
        mini_clave_ = q.clave;
        mini_fila_ = q.fila;
        return kNada;
      }
      if (g_minijuego && g_minijuego(q.clave, q.fila)) {
        esfera_video_ = -1;
        mini_final_ = vv.final;
        mini_final_t0_ = t;
        return g_mini_clave = q.clave, kMinijuego;
      }
      aviso_ = MotivoMinijuegoIW(q.clave);
    } else if (q.mision > 0 && q.mision <= in.abiertas && !no_b3) {
      return kElegir;
    } else if (q.mision > 0 && no_b3) {   // bloqueada: el motivo (p. ej. "Necesita a Super 17...") en el globo
      aviso_ = in.motivo[size_t(q.mision)];
    }
  }
  return kNada;
}

// Escena de la historia: el mapa de fondo con bandas de cine y, abajo, la linea del subtitulo del video (imagen del
// juego, texto blanco con borde, como sale sobre el video). A pasa de linea; B la salta entera.
// Primero el video original con sus subtitulos (video_sfd.h, frente R7; los tiempos van en el SFD); si no se puede
// (ISO, falta el archivo), de respaldo los subtitulos sobre el mapa.
PantallaDM::Accion PantallaDM::Historia(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in) {
  // EU 0x2AFF40 DragonMissionTitleSystemFunc
  const std::vector<Imagen>* lineas =
      video_ < int(in.dm->subtitulos.size()) ? &in.dm->subtitulos[size_t(video_)] : nullptr;
  if (!probado_) {
    probado_ = true;
    const auto ruta = RutaVideoIW(*in.dm, video_);
    en_video_ = !ruta.empty() && dbz3::video::Reproducir(ruta, lineas ? *lineas : std::vector<Imagen>());
    REXLOG_INFO("dbz3 misiones: iw: video de la historia {} ({}): {}", video_, ruta.filename().string(),
                en_video_ ? "reproduciendo" : "sin video, subtitulos sobre el mapa");
  }
  // EU 0x338C10 Movie_DrawFrameAndSubtitle
  if (en_video_) {
    DibujarFondo(dl, io, t, in);
    if (dbz3::video::Activo()) {
      pausado_ = dbz3::video::Pausado();   // SELECT lo lleva el panel del SFD (video_sfd.cpp); la escena lo refleja
      return kNada;                        // el video va encima y lee A/START/SELECT
    }
    linea_ = lineas ? int(lineas->size()) : 0;  // acabado o saltado
    pulsado = 0;
    pausado_ = false;
  }
  // EU 0x18B138 s_E1122604_mwPlyPause: SELECT pausa/reanuda la escena (el original lo hace desde el reproductor;
  // el port no tiene tecla de pausa en el video, asi que se ata a SELECT, libre en esta pantalla).
  if (pulsado & kSelect) {
    pausado_ = !pausado_;
    PausaPeliculaIW(pausado_);
  }
  if (!pausado_ && (pulsado & (kA | kStart))) ++linea_;
  if (!lineas || (!pausado_ && ((pulsado & kB) || linea_ >= int(lineas->size())))) {
    REXLOG_INFO("dbz3 misiones: iw: escena de la historia (video {}) acabada en la linea {}", video_, linea_);
    const int visto = video_;
    video_ = -1;
    if (esfera_video_ >= 0) {   // DS104: la escena era la de una marca tipo 2: ahora se marca la esfera
      const int indice = esfera_video_;
      esfera_video_ = -1;
      if (indice >= 0 && indice < 22 && !EsferaRecogidaIW(indice, t)) {
        MarcarEsferaIW(indice, t);
        ActualizarRegistro(in);
        aviso_ = T::T("Esfera de cristal recogida.", "Crystal ball collected.");
        REXLOG_INFO("dbz3 iw mapa: esfera de indice {} marcada al acabar su SFD {} (batalla 0xC0; registro guardado)",
                    indice, visto);
      }
      return kNada;
    }
    if (mini_clave_ >= 0) {   // la escena era la de antes de un minijuego: ahora, el minijuego
      const int clave = mini_clave_;
      mini_clave_ = -1;
      REXLOG_INFO("dbz3 misiones: iw: video previo {} acabado: empieza el minijuego {:#x} (antes de READY)", visto, clave);
      if (g_minijuego && g_minijuego(clave, mini_fila_)) return g_mini_clave = clave, kMinijuego;
      mini_final_ = -1;   // no empezo: no hay salida de la que pedir el video de despues
      aviso_ = MotivoMinijuegoIW(clave);
    }
    return kNada;
  }
  DibujarFondo(dl, io, t, in);
  const S::Imagen gi = S::GuestImage(io);   // L05: bandas y video sobre la imagen del juego
  const ImVec2 D = gi.Size();
  const float banda = D.y * 0.13f;
  dl->AddRectFilled(gi.p0, gi.p1, IM_COL32(0, 0, 0, 110));
  dl->AddRectFilled(gi.p0, ImVec2(gi.p0.x + D.x, gi.p0.y + banda), IM_COL32(0, 0, 0, 255));
  dl->AddRectFilled(ImVec2(gi.p0.x, gi.p1.y - banda * 1.7f), gi.p1, IM_COL32(0, 0, 0, 255));
  const Imagen& l = (*lineas)[size_t(linea_)];
  ImVec2 p0, p1;   // a la escala de la PS2 (448 lineas), centrada en la banda de abajo
  Encajar(float(l.w), float(l.h), gi.p0.x + D.x * 0.5f, gi.p0.y + D.y - banda * 0.95f, D.x * 0.9f, banda * 1.3f, D.y / 448.0f, p0, p1);
  Imagen2(dl, 20000 + video_ * 256 + linea_, l, p0, p1);
  S::mode::Help(dl, S::GameArea(io), {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Saltar", "Skip")},
                                     {"SELECT", pausado_ ? T::T("Reanudar", "Resume") : T::T("Pausa", "Pause")}});
  return kNada;
}

PantallaDM::Accion PantallaDM::Ficha(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in) {
  // EU 0x2FCBC0 LoadWaitDragonMainMissionMenus: el menu de mision (Minigame_RAMMainMenu abre sus sprites; el estado
  // pasa a DragonMissionMainMenu_TitleScripts, que pinta la ficha). Aqui es esta pantalla: las cuatro entradas y
  // MISSION START.
  if (!in.dm) return kVolver;
  if (edit_abierto_) return PantallaCapsulas(dl, io, t, pulsado, in);   // Skill Edit (DS55)
  DibujarFondo(dl, io, t, in);
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 215));
  const int n = Mision(in);
  if (n <= 0) return kVolver;
  const CasillaDM& q = Cas(in)[size_t(sel_)];
  const auto a = S::GameArea(io);
  const auto& ui = in.dm->ui;
  ImVec2 p0, p1;
  auto en = [&](ImVec2 x) { return a.At(x.x, x.y); };
  const MisionTablaIW* mt = in.tabla && n - 1 < int(in.tabla->size()) ? &(*in.tabla)[size_t(n - 1)] : nullptr;
  // Titulo de la mision en la banda superior, como la ficha original ("Go after Raditz!"): es la primera textura de
  // DATA_USA 132 + n - 1 (la misma que ya usa DETAILS), centrada sobre el mapa. El original lo pinta siempre.
  if (in.textos && n - 1 < int(in.textos->misiones.size())) {
    const Imagen& ti = in.textos->misiones[size_t(n - 1)].titulo;
    if (ti) {
      const ImVec4 rt = Recorte(ti);
      Encajar(rt.z - rt.x, rt.w - rt.y, 600, 38, 1000, 64, 3.0f, p0, p1);
      Imagen2(dl, 5000 + n, ti, en(p0), en(p1), rt);
    }
  }
  // Menu de la mision de IW (334/1 t11, filas de 40 px: MISSION SETTINGS, CONTROLS, DETAILS, SKILL EDIT; luego
  // MISSION START t14). Arriba/abajo mueve el cursor; el panel de la izquierda ensena la entrada del cursor
  // (DETAILS tambien con el cursor en MISSION START).
  // EU 0x2FA200 LoadWaitDragonMissionExitMenuOptionEnd: el cursor (ctx+0x370) da la vuelta: arriba en la primera
  // entrada pasa a la ultima y abajo en la ultima a la primera, y cada movimiento suena (MenuPopUpSound(0,0,2,0)).
  // MISSION START es la fila ctx[0x35C] == iVar9, que vale 4 con las cuatro entradas de serie, 5 y 6 si la mision ya
  // esta hecha (los rotulos de esas dos filas extra no estan en el C: [H], no se dibujan).
  if (pulsado & kUp) menu_ = menu_ == 0 ? 4 : menu_ - 1;
  if (pulsado & kDown) menu_ = menu_ == 4 ? 0 : menu_ + 1;
  if (pulsado & kLeft) dificultad_ = std::max(0, dificultad_ - 1);
  if (pulsado & kRight) dificultad_ = std::min(4, dificultad_ + 1);
  const int vista = menu_ == 4 ? 2 : menu_;
  auto dificultad = [&](float x, float y) {   // DIFFICULTY (t13 fila 0) < Very Easy .. Very Hard (t16) >
    Imagen2(dl, 3000 + kDifCar, De(ui, kDifCar), a.At(x, y - 26), a.At(x + 208, y + 26), ImVec4(0, 0, 160, 40));
    Imagen2(dl, 3000 + kFlecha, De(ui, kFlecha), a.At(x + 218, y - 14), a.At(x + 246, y + 14));
    Imagen2(dl, 3000 + kDificultades, De(ui, kDificultades), a.At(x + 250, y - 21), a.At(x + 450, y + 21),
            ImVec4(0, 40.0f * float(dificultad_), 192, 40.0f * float(dificultad_ + 1)));
    Imagen2(dl, 3000 + kFlecha, De(ui, kFlecha), a.At(x + 455, y - 14), a.At(x + 483, y + 14), ImVec4(24, 0, 0, 24));
  };
  // izquierda: panel de la computadora (t00) con la entrada del menu
  dl->AddRectFilled(a.At(70, 100), a.At(570, 668), IM_COL32(6, 22, 40, 255));
  Imagen2(dl, 3000 + kPanel, De(ui, kPanel), a.At(70, 100), a.At(570, 668));
  const float lin = S::mode::kHead * a.s;
  // parrafo centrado en el panel (x 320): parte en lineas de `cols` letras por palabras
  auto parrafo = [&](const char* txt, float y, float tam, size_t cols) {
    std::string linea, palabra;
    auto sacar = [&] {
      if (linea.empty()) return;
      S::MenuText(dl, a.At(320, y), tam, linea.c_str(), false, false, 440 * a.s);
      y += tam / a.s * 1.3f;
      linea.clear();
    };
    for (const char* c = txt;; ++c) {
      if (*c && *c != ' ') {
        palabra += *c;
        continue;
      }
      if (!linea.empty() && linea.size() + 1 + palabra.size() > cols) sacar();
      if (!palabra.empty()) linea += (linea.empty() ? "" : " ") + palabra;
      palabra.clear();
      if (!*c) break;
    }
    sacar();
  };
  if (vista == 2) {   // DETAILS: titulo y texto de la mision (DATA_USA 131 + n) y la condicion
    if (in.textos && n - 1 < int(in.textos->misiones.size())) {
      const MisionIW& mi = in.textos->misiones[size_t(n - 1)];
      const ImVec4 rt = Recorte(mi.titulo);
      Encajar(rt.z - rt.x, rt.w - rt.y, 320, 160, 460, 64, 3.0f, p0, p1);
      Imagen2(dl, 5000 + n, mi.titulo, en(p0), en(p1), rt);
      Encajar(float(mi.texto.w), float(mi.texto.h), 320, 380, 440, 340, 2.0f, p0, p1);
      Imagen2(dl, 6000 + n, mi.texto, en(p0), en(p1));
    }
    if (!in.regla.empty()) parrafo(in.regla.c_str(), 572, lin * 0.75f, 34);
  } else if (vista == 0) {   // MISSION SETTINGS: DIFFICULTY y CHARACTER (t13 filas 0 y 1)
    dificultad(86, 220);
    Imagen2(dl, 3000 + kDifCar, De(ui, kDifCar), a.At(86, 314), a.At(294, 366), ImVec4(0, 40, 160, 80));
    const int jug = mt ? IWaB3(mt->jugador) : -1;
    if (jug >= 0) S::MenuText(dl, a.At(320, 420), lin, NombreDe(jug).c_str(), true, false, 440 * a.s);
    // RE 2026-10-25: la mision fija el personaje en +0x14; la lista de 0x1D IDs de DragonMissionMenusCharacterNames...
    // (TitleScripts EU 0x2FC330) es para los minijuegos, no se recorre para la ficha.
  } else if (vista == 1) {   // CONTROLS: los del modo (combate de B3) y lo que pide la mision
    const char* l[] = {T::T("Combate con los controles de Budokai 3.", "Battle with the Budokai 3 controls."),
                       T::T("START: pausa.", "START: pause."), in.regla.c_str()};
    float y = 180;
    for (const char* s : l) {
      if (*s) parrafo(s, y, lin, 24);
      y += 130;
    }
  } else {   // SKILL EDIT: pantalla de capsulas (EU 0x2E3F40 SkillEditFunc1; DS55)
    parrafo(T::T("Abre la lista de cápsulas de misión y la tabla de bloqueadas.",
                 "Opens the mission capsule list and the locked-capsule table."),
            280, lin, 24);
    S::MenuText(dl, a.At(320, 520), S::mode::kHead * a.s, T::T("Pulsa A para abrir SKILL EDIT", "Press A to open SKILL EDIT"),
                true, false, 440 * a.s);
  }
  // DIFFICULTY en las vistas que no son MISSION SETTINGS: un poco mas arriba (700 en vez de 712) para dejar libre el
  // panel BEST SCORE de abajo a la izquierda, que en el original va justo debajo (su cabecera es la misma fila).
  if (vista != 0) dificultad(70, 700);
  // derecha: marco (t03) con la insignia del rival y su nombre
  dl->AddRectFilled(a.At(640, 108), a.At(1110, 412), IM_COL32(6, 22, 40, 235), 12 * a.s);
  Imagen2(dl, 3000 + kMarco, De(ui, kMarco), a.At(620, 90), a.At(1130, 430));
  const Imagen& ins = De(in.dm->iconos, q.icono);
  Encajar(float(ins.w), float(ins.h), 875, 240, 190, 190, 4.0f, p0, p1);
  Imagen2(dl, 1000 + q.icono, ins, en(p0), en(p1));
  if (mt) {
    const int rival = IWaB3(mt->rival);
    if (rival >= 0) S::MenuText(dl, a.At(875, 375), lin, NombreDe(rival).c_str(), true, false, 420 * a.s);
  }
  // Panel «Normal — BEST SCORE» de la ficha original (captura original_ficha_mision1.png de DS19): cabecera BEST SCORE
  // (334/1 t15) y debajo las filas SCORE (t17 fila 3) y RANK (t17 fila 1), con el guion de "sin marca" (t24 fila 1,
  // columna 2). RE 2026-10-25: DragonMissionMainMenu_TitleScripts (EU 0x2FC330) pinta la cabecera con los elementos 5/7
  // y 6 de la hoja 0xF (MiscellaneousGameProperties_Display_(1,5,7,0xF,0x20080) y (1,6,-1,0xF,0x40080)); las filas son
  // el mismo t17 del resultado. La historia de IW no guarda puntos: Battle_ComputeRankAndRecord (EU 0x28A5E0) escribe
  // solo el rango en el registro (+0x60) y el marcador +0x00 se queda a 0 (Story_DrawMapResultIcon 0x2FC970 pinta el
  // cartel vacio si vale 0), asi que SCORE sale siempre con el guion; RANK, con el mejor rango guardado (iw_rango_N).
  // [VERIFICAR] en pantalla: la caja va abajo a la izquierda, como en el original, no donde estaba la fila RANK suelta.
  dl->AddRectFilled(a.At(86, 726), a.At(556, 850), IM_COL32(6, 22, 40, 225), 12 * a.s);
  {
    const Imagen& bs = De(ui, kBestScore);
    if (bs) Imagen2(dl, 3000 + kBestScore, bs, a.At(110, 730), a.At(310, 770));
    else S::MenuText(dl, a.At(210, 750), lin * 0.8f, "BEST SCORE", true, false, 200 * a.s);
    // fila SCORE (t17 fila 3): BEST SCORE de la mision. Los digitos salen del registro por mision (bloque 0x5c1d8c);
    // 0 = guion de "sin marca" (t24, fila 1 columna 2), como la captura de DS19. El port guarda ese marcador en
    // `iw_mejor_N` (DS108).
    // EU 0x32A980 StoryMg_ScoreDigitSelect
    Imagen2(dl, 3000 + kEtiquetas, De(ui, kEtiquetas), a.At(110, 774), a.At(280, 806), ImVec4(0, 96, 152, 128));
    const int sc = n < int(in.puntaje.size()) ? in.puntaje[size_t(n)] : 0;
    if (sc > 0) Cifras(dl, true, std::to_string(sc), a.At(460, 790), 38 * a.s, in);
    else Imagen2(dl, 3000 + kRangos, De(ui, kRangos), a.At(470, 774), a.At(496, 806), ImVec4(64, 40, 96, 80));
    // fila RANK (t17 fila 1) con el mejor rango de la mision (t24; "-" sin superar)
    const int rg = (n < int(in.mejor.size()) ? in.mejor[size_t(n)] : 0) - 1;
    const int col = rg < 0 ? 2 : rg >= 3 ? rg - 3 : rg, fila = rg < 0 || rg >= 3 ? 1 : 0;
    Imagen2(dl, 3000 + kEtiquetas, De(ui, kEtiquetas), a.At(110, 810), a.At(280, 842), ImVec4(0, 32, 152, 64));
    Imagen2(dl, 3000 + kRangos, De(ui, kRangos), a.At(470, 810), a.At(496, 842),
            ImVec4(32.0f * float(col), 40.0f * float(fila), 32.0f * float(col + 1), 40.0f * float(fila + 1)));
  }
  for (int i = 0; i < 4; ++i) {   // t11: la elegida entera y con la flecha (t08); las otras, apagadas
    const float y = 530.0f + 48.0f * float(i);
    Imagen2(dl, 3000 + 11, De(ui, 11), a.At(680, y - 24), a.At(930, y + 24),
            ImVec4(0, 40.0f * float(i), 208, 40.0f * float(i + 1)), menu_ == i ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 120));
    if (menu_ == i) Imagen2(dl, 3000 + kFlecha, De(ui, kFlecha), a.At(646, y - 14), a.At(674, y + 14), ImVec4(24, 0, 0, 24));
  }
  // MISSION START (t14), latiendo si es la elegida
  const float pulso = menu_ == 4 ? 0.8f + 0.2f * std::sin(t * 6.0f) : 0.8f;
  Imagen2(dl, 3000 + kMissionStart, De(ui, kMissionStart), a.At(875 - 185 * pulso, 760 - 44 * pulso),
          a.At(875 + 185 * pulso, 760 + 44 * pulso), ImVec4(0, 0, 0, 0),
          menu_ == 4 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 120));
  S::mode::Help(dl, a, {{"A", menu_ == 4 ? T::T("Empezar", "Start") : T::T("Elegir", "Select")},
                        {"B", T::T("Volver", "Back")}});
  if (pulsado & kB) {
    menu_ = 4;
    return kVolver;
  }
  if ((pulsado & kStart) || ((pulsado & kA) && menu_ == 4)) {
    menu_ = 4;
    // EU 0x20E860 LoadWaitEndDragonMissionSelectionBattleFunc -> EU 0x20E630 LoadWaitVsStageBattleFunc:
    // Audio_PickRandomVoiceVariant (EU 0x1FC660) elige la variante de voz al montar el combate de la mision; en
    // realidad esa variante indexa la lista de temas de batalla (EU 0x1FCE00) y la pista suena en ADX_CMN.
    if (mt) ElegirTemaBatallaIW(mt->jugador);
    return kEmpezar;
  }
  if ((pulsado & kA) && menu_ == 3) {   // SKILL EDIT: abre la pantalla de capsulas (EU 0x2B13B0/0x300CC0)
    AbrirCapsulas(in);
    return kNada;
  }
  if (pulsado & kA) menu_ = 4;   // A en una entrada: ya esta en el panel; vuelve a MISSION START
  return kNada;
}

// Sonidos del resultado (F5): ADX de la copia del jugador (pasarela::OirAdxPs2). kSonidos[evento] = {AFS, entrada};
// entrada -1 = sin localizar (no suena).
void PantallaDM::SonidoResultado(const InfoDM& in, int evento) {
  struct Son {
    const char* afs;
    int entrada;
  };
  static constexpr Son kSonidos[3] = {{"ADX_CMN.AFS", -1}, {"ADX_CMN.AFS", -1}, {"ADX_CMN.AFS", -1}};
  const Son& x = kSonidos[std::clamp(evento, 0, 2)];
  REXLOG_INFO("dbz3 misiones: iw: resultado, sonido {} ({} {})", evento, x.afs, x.entrada);
  if (x.entrada < 0 || in.dm->raiz.empty()) return;
  OirAdxPs2(in.dm->raiz, x.afs, x.entrada,
            dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()));
}

bool PantallaDM::Resultado(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in,
                           const ResultadoDM& r) {
  if (!in.dm) return true;
  if (r.mision <= 0) {   // DS153: resultado de un minijuego (0x7D0-0x7E1)
    // Si su modulo ya pinto su propia pantalla (iw_mini_disparo.cpp / iw_mini_botones.cpp), el original ensena UN solo
    // panel: el mapa no repite otro; solo consume el resultado y sigue (escena/video de despues, como siempre).
    if (iwmini::ClaveConPanelPropioIW(r.clave_mini)) {
      res_t0_ = -1;
      res_saltar_ = false;
      return true;
    }
    // Sin panel propio (MSNMW, NB007, NB012, vuelo): la pantalla de resultado de IW con las filas EXACTAS de su clave
    // (FilasResultadoIW). Es el mismo dibujo que usan los modulos (titulo F04.3, filas de 334/1, GREAT/GOOD/NORMAL de
    // 4/0, EARNED/TOTAL de 333/0), asi que no hay dos estilos ni filas de mas.
    DibujarFondo(dl, io, t, in);
    const auto a = S::GameArea(io);
    const ImVec2 p0 = a.At(280, 226), p1 = a.At(920, 674);   // lienzo 640x448 del original, centrado
    iwmini::Resultado x;
    x.clave = r.clave_mini;
    x.centesimas = r.centesimas >= 0 ? r.centesimas : r.segundos * 100;
    x.puntos = r.puntos;
    x.score = r.score;
    x.zeni = r.zeni;
    x.saldo = r.saldo;
    x.rango = r.rango;
    x.objetos = r.objetos;
    x.objetos_total = r.objetos_total;
    x.great = r.great, x.good = r.good, x.normal = r.normal;
    x.offence = r.offence, x.defence = r.defence;
    x.con_tiempo = r.con_tiempo;
    if (res_t0_ < 0) res_t0_ = t, res_saltar_ = false;
    const bool completo = iwmini::DibujarResultadoIW(dl, p0.x, p0.y, p1.x - p0.x, p1.y - p0.y, x, t - res_t0_,
                                                     res_saltar_);
    if (pulsado & (kA | kStart | kB)) {
      if (completo || res_saltar_) {   // A con el conteo acabado (o ya saltado) cierra; antes, salta el conteo
        res_t0_ = -1;
        res_saltar_ = false;
        CallarAdxPs2();
        return true;
      }
      res_saltar_ = true;
    }
    S::mode::Help(dl, a, {{"A", completo ? T::T("Al mapa", "To the map") : T::T("Saltar", "Skip")}});
    return false;
  }
  DibujarFondo(dl, io, t, in);
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 215));
  const auto a = S::GameArea(io);
  const auto& ui = in.dm->ui;
  ImVec2 p0, p1;
  dl->AddRectFilled(a.At(330, 80), a.At(870, 820), IM_COL32(6, 22, 40, 255));
  Imagen2(dl, 3000 + kPanel, De(ui, kPanel), a.At(330, 80), a.At(870, 820));
  if (in.textos && r.mision >= 1 && r.mision - 1 < int(in.textos->misiones.size())) {
    const Imagen& ti = in.textos->misiones[size_t(r.mision - 1)].titulo;
    const ImVec4 rt = Recorte(ti);
    Encajar(rt.z - rt.x, rt.w - rt.y, 600, 145, 500, 64, 3.0f, p0, p1);
    Imagen2(dl, 5000 + r.mision, ti, a.At(p0.x, p0.y), a.At(p1.x, p1.y), rt);
  } else if (const int im = r.clave_mini - 0x7D0; r.mision <= 0 && im >= 0 && im < int(in.dm->titulos_mini.size()) &&
                                                  in.dm->titulos_mini[size_t(im)]) {   // s1 F04.3: titulo del minijuego
    const Imagen& ti = in.dm->titulos_mini[size_t(im)];
    const ImVec4 rt = Recorte(ti);
    Encajar(rt.z - rt.x, rt.w - rt.y, 600, 145, 500, 64, 3.0f, p0, p1);
    Imagen2(dl, 5600 + im, ti, a.At(p0.x, p0.y), a.At(p1.x, p1.y), rt);
  }
  // Filas en el orden de la hoja 334/1 t17 (filas de 32 px: 0 TIME, 1 RANK, 2 BRICKS DESTROYED, 3 SCORE, 4 TOTAL
  // SCORE). Combate: TIME y RANK (con New!), sin puntos, como IW. Minijuego: TIME, RANK, objetos (solo cuando la
  // pantalla del minijuego los da), SCORE y TOTAL SCORE. DS108: SCORE/TOTAL SCORE salen de `score` (fila TOTAL SCORE
  // de Results_ComputeTotalsAndRewards, EU 0x2E9AF0: +0x0C/+0x0D del bloque de resultados), EARNED de `zeni` (+0x13)
  // y TOTAL ZENI de `saldo` (iRam005c154c, el monedero que ya sumo Battle_ApplyResultRewards, EU 0x28A020).
  const bool mini = r.mision <= 0;
  const int score = r.score >= 0 ? r.score : r.puntos;
  const int zeni = r.zeni;
  const int saldo = r.saldo >= 0 ? r.saldo : LeerProgreso("iw_zeni", 0) + zeni;
  std::vector<int> filas;
  if (r.con_tiempo) filas.push_back(0);   // MSN019 (0x7E1) no lleva TIME en su resultado
  filas.push_back(1);
  if (mini) {
    if (r.ladrillos >= 0 || r.objetos >= 0) filas.push_back(2);
    filas.push_back(3);
    filas.push_back(4);
  }
  // Ritmo de IW (60 fps): Results_StateBeginRoll espera 100 cuadros antes de la primera fila y Results_UpdateTally
  // pasa de fila cada 60; al acabar, el premio (Results_BeginOutcome). A salta el conteo.
  constexpr float kAntes = 100.0f / 60.0f, kFila = 60.0f / 60.0f;
  if (res_t0_ < 0) res_t0_ = t, res_sonadas_ = 0;
  const int n = int(filas.size());
  const int extra = mini ? 2 : 1;   // EARNED (+ TOTAL ZENI: solo la pantalla Results_* lo lleva)
  if (pulsado & (kA | kStart | kB)) {
    if (res_sonadas_ >= n + extra) {
      res_t0_ = -1;
      CallarAdxPs2();
      return true;
    }
    res_t0_ = t - kAntes - kFila * float(n + extra) - 0.01f;   // todo a la vista
  }
  const float el = t - res_t0_;
  const int vistas = el < kAntes ? 0 : std::min(n + extra, 1 + int((el - kAntes) / kFila));
  const float frac = el < kAntes ? 0.0f : std::fmod(el - kAntes, kFila) / kFila;          // de la fila que entra
  while (res_sonadas_ < vistas) SonidoResultado(in, res_sonadas_++ < n ? 0 : zeni > 0 ? 1 : 2);
  const float paso = n > 4 ? 56.0f : n > 3 ? 64.0f : 80.0f;
  const float ez = mini ? (n > 4 ? 530.0f : 520.0f) : 600.0f;
  const float tz = ez + 60.0f;
  auto etiqueta = [&](int fila, float y) {
    Imagen2(dl, 3000 + kEtiquetas, De(ui, kEtiquetas), a.At(370, y - 24), a.At(598, y + 24),
            ImVec4(0, 32.0f * float(fila), 152, 32.0f * float(fila + 1)));
  };
  auto rango = [&](float y) {
    const int rg = r.rango >= 0 ? std::min(r.rango, 4) : Rango(mini ? score : r.puntos), col = rg >= 3 ? rg - 3 : rg,
              fila = rg >= 3 ? 1 : 0;
    Imagen2(dl, 3000 + kRangos, De(ui, kRangos), a.At(778, y - 34), a.At(830, y + 34),
            ImVec4(32.0f * float(col), 40.0f * float(fila), 32.0f * float(col + 1), 40.0f * float(fila + 1)));
  };
  auto nuevo = [&](float x1, float y) {   // New! a la izquierda de x1 (px de pantalla)
    const float b = 3.0f * std::sin(t * 5.0f);
    Imagen2(dl, 1000 + kNew, De(in.dm->iconos, kNew), ImVec2(x1 - 88 * a.s, a.At(0, y - 32 + b).y),
            ImVec2(x1, a.At(0, y + 32 + b).y));
  };
  // EU 0x2E9D40 Results_UpdateTally
  for (int i = 0; i < std::min(vistas, n); ++i) {
    const float y = 235.0f + paso * float(i);
    // La cifra de la fila que entra sube de 0 a su valor en media fila. RE 2026-10-25: Results_UpdateTally (EU
    // 0x2E9D40) suma la fila al total al acabar su espera de 60 cuadros (iRam0049cd68+0x70 += +0x74) y
    // Results_ComputeTotalsAndRewards (0x2E9AF0) vuelca la suma en el bloque de resultado (+0x0C/+0x0D).
    const float k = i + 1 < vistas ? 1.0f : std::min(1.0f, frac * 2.0f);
    etiqueta(filas[size_t(i)], y);
    switch (filas[size_t(i)]) {
      case 0: {   // TIME con centesimas (M'SS"CC), como +0x74 del bloque y Results_UpdateTally 0x2E9D40
        const int cs = int(float(r.centesimas >= 0 ? r.centesimas : r.segundos * 100) * k);
        char tiempo[24];
        FormatearTextoIW(tiempo, sizeof(tiempo), "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
        Cifras(dl, true, tiempo, a.At(830, y), 54 * a.s, in);
        break;
      }
      case 1:
        rango(y);
        if (r.mision > 0 && r.nuevo_record) nuevo(a.At(770, 0).x, y);
        break;
      case 2: {   // objetos: en 0x7D3 es BRICKS DESTROYED (334/1 t17 fila 2); en los demas, [H] la etiqueta
        const int obj = r.ladrillos >= 0 ? r.ladrillos : r.objetos;
        Cifras(dl, true, std::to_string(int(float(obj) * k)), a.At(830, y), 54 * a.s, in);
        break;
      }
      case 3: {
        const float izq = Cifras(dl, true, std::to_string(int(float(score) * k)), a.At(830, y), 54 * a.s, in);
        if (r.nuevo_record) nuevo(izq - 4 * a.s, y);
        break;
      }
      default:
        Cifras(dl, true, std::to_string(int(float(score) * k)), a.At(830, y), 54 * a.s, in);
    }
  }
  if (vistas > n) {   // EARNED! ... ZENI (333/0 t19 y t07): premio del bloque de resultados (+0x13)
    Imagen2(dl, 4000 + kEarned, De(in.dm->road, kEarned), a.At(370, ez - 32), a.At(575, ez + 32));
    Cifras(dl, true, std::to_string(zeni), a.At(735, ez), 54 * a.s, in);
    Imagen2(dl, 4000 + kZeniTxt, De(in.dm->road, kZeniTxt), a.At(742, ez - 10), a.At(832, ez + 28));
  }
  // TOTAL ... ZENI (333/0 t20 y t07): monedero de IW ya sumado (iRam005c154c), como pinta LoadWaitRewardScreen.
  // EU 0x285F30 LoadWaitRewardScreen
  if (mini && vistas > n + 1) {
    Imagen2(dl, 4000 + kTotal, De(in.dm->road, kTotal), a.At(370, tz - 32), a.At(575, tz + 32));
    Cifras(dl, true, std::to_string(std::max(saldo, 0)), a.At(735, tz), 54 * a.s, in);
    Imagen2(dl, 4000 + kZeniTxt, De(in.dm->road, kZeniTxt), a.At(742, tz - 10), a.At(832, tz + 28));
  }
  S::mode::Help(dl, a, {{"A", vistas >= n + extra ? T::T("Al mapa", "To the map") : T::T("Saltar", "Skip")}});
  return false;
}

}  // namespace dbz3::pasarela
