// dbz3 - Misiones del Dragon de Infinite World (ronda 3, frentes F y P1): la tabla de misiones del ELF de la copia del
// jugador (rival, escenario, tiempo y condicion de cada mision) y la pantalla de Dragon Mission con los graficos del
// juego (mapa del mundo, insignias, ficha y resultado), leidos en marcha; nada del juego va en la version. Docs:
// docs/16_pasarela_modos/03_infinite_world.md §2.
#include "pasarela_iw.h"
#include "guest_region.h"
#include "ingame/tutorial.h"
#include "ps2_modelo.h"
#include "video_sfd.h"

#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <map>

#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"

namespace dbz3::pasarela {
namespace {

constexpr int kMisiones = 96;    // Misiones del Dragon (despues, 105 de Fighter's Road)
constexpr int kRegistros = 201;
constexpr int kRegistro = 0x60;

uint16_t U16(const std::vector<uint8_t>& b, size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); }
uint32_t U32(const std::vector<uint8_t>& b, size_t o) { return U16(b, o) | (uint32_t(U16(b, o + 2)) << 16); }

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

std::vector<MisionTablaIW> LeerTablaIW(const std::filesystem::path& iw, std::string& error) {
  std::vector<MisionTablaIW> out;
  const std::vector<uint8_t> elf = LeerElf(iw);
  if (elf.empty()) {
    error = "sin ELF de Infinite World en " + iw.string();
    return out;
  }
  // La tabla: registros de 0x60 B numerados 1, 2, 3... (USA: fichero 0x376D50).
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
  for (int n = 1; n <= kRegistros && base + size_t(kRegistro) * size_t(n) <= elf.size() &&
                  U16(elf, base + size_t(kRegistro) * (n - 1)) == n; ++n) {   // 1-96 y Fighter's Road 97-201
    const size_t r = base + size_t(kRegistro) * (n - 1);
    MisionTablaIW m;
    m.numero = n;
    m.condicion = int16_t(U16(elf, r + 0x02));
    m.escenario = U16(elf, r + 0x08);
    const int t = U16(elf, r + 0x0C);
    m.tiempo = t == 1 ? 60 : t == 3 ? 180 : t == 5 ? 120 : -1;   // ponytail: solo los indices que usan las 96
    m.premio = U16(elf, r + 0x0E);
    m.jugador = U16(elf, r + 0x14);
    m.forma1 = U16(elf, r + 0x16);
    m.rival = U16(elf, r + 0x3A);
    m.forma2 = U16(elf, r + 0x3C);
    m.nivel = U16(elf, r + 0x04);
    m.notas = U16(elf, r + 0x06);
    out.push_back(m);
  }
  return out;
}

// IDs de IW = los de B3 hasta el 43 (Goku 0 ... Saibaman 42); del 44 en adelante son de IW (GT, peliculas, fusiones).
// Del 44 en adelante: el port instalado con el Mod Kit (importar.py iw; clave = "c:" + nombre del modelo de IW en
// minusculas, mod center hd/iw_modelos.json). Las fusiones de IW (64 Gotenks, 70 Gogeta SS4, 74 Vegito, 79 Bu con
// Gohan) son en B3 personajes internos con el mismo numero (Gotenks 64, Vegito 74: los que pone la fusion en combate;
// probados: misiones 67 y 69) o formas de Super Bu (35; tabla de transformaciones de B3, docs/07_ports/GOHAN_FUTURO_
// EXPLORACION_2026-10-06/04_transformaciones/evid_tabla_b3.md: forma 1 = modelo 84 Piccolo, 2 = 78 Gotenks, 3 = 79
// Gohan). ponytail: empezar en la forma 3 cuelga el juego al cargar (D:/DBZ3HD/_pruebas/20261016-031905_modos_test_
// iw_mapa): Bu con Gohan sale como Super Bu de serie. Gogeta SS4 (70) sigue en gris.
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

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kLB = 0x0100,
                   kRB = 0x0200, kA = 0x1000, kB = 0x2000;
// Texturas por indice (hojas de E:/DBZ3HD_trabajo/estudio_ports/iw_assets/png): 334/5 iconos, 334/1 interfaz, 333/0.
constexpr int kNew = 16, kAro = 89, kGokuMapa = 90;
constexpr int kPanel = 0, kMarco = 3, kFlecha = 8, kDifCar = 13, kMissionStart = 14, kBestScore = 15, kDificultades = 16,
              kEtiquetas = 17, kCifrasG = 19, kCifrasB = 22, kRangos = 24, kEstrella = 31;
constexpr int kZeniTxt = 7, kGlobo = 18, kEarned = 19;
constexpr int kInterrogante = 0, kEsfera = 6;   // 334/5: "?" en su peana (minijuego), esfera de cristal
// 334/3 (ademas de los mapas): HUD del mapa. Medido en E:/DBZ3HD_trabajo/P1/h334_3.png y medir.py.
constexpr int kRobot = 0, kGloboMapa = 1, kFlechas = 17, kCifrasRosa = 18, kBarraBolas = 19, kBolas = 20, kClear = 69;
constexpr int kNombreSaga[6] = {2, 26, 36, 40, 42, 43};   // "Saiyan Saga" ... "Another Story" (letras rojas)
// Bulma de la guia en cada saga. ponytail: la del coche 32 (t38) segun las capturas; las demas por su ropa, sin confirmar
constexpr int kBulma[6] = {38, 30, 41, 39, 44, 38};
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
// Insignia "VS" (textura de 334/5) de cada valor +0x10 del #DMD (30..87). R7 quinta tanda: leida del #AMA 334/4 (sus
// texturas son las de 334/5): el objeto 40 + 2 (v - 30) lleva en la clave 0 de su pista de cuadros (registro +8: +4
// cuantas claves, +0x14 claves de 0x20 B, u16 +0 = textura; SprAnim_SampleFrameTrack_ EU L150910) la insignia; los
// pares 40-155 dan justo 58 (los 30-37 coinciden con lo medido a ojo antes). E:\DBZ3HD_trabajo\R7\iw\s5/ama2.py.
// ponytail: que +0x10 del #DMD elige el objeto 40 + 2 (v - 30) se deduce del orden, no de la llamada del juego.
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
// Busca `rel` dentro de una carpeta extraida: tal cual (raiz/USR/FDxx) y un nivel o dos mas abajo (raiz/<juego>/USR/
// FDxx, como queda al extraer una ISO en su propia carpeta). Sin recorrer ficheros: solo directorios, para no tardar.
std::filesystem::path EnCarpetaIW(const std::filesystem::path& raiz, const std::filesystem::path& rel) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(raiz / rel, ec)) return raiz / rel;
  if (!std::filesystem::is_directory(raiz, ec)) return {};
  for (const auto& a : std::filesystem::directory_iterator(raiz, ec)) {
    if (!a.is_directory(ec)) continue;
    if (std::filesystem::is_regular_file(a.path() / rel, ec)) return a.path() / rel;
    for (const auto& b : std::filesystem::directory_iterator(a.path(), ec)) {
      if (b.is_directory(ec) && std::filesystem::is_regular_file(b.path() / rel, ec)) return b.path() / rel;
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

int VideoIW(const DragonMissionIW& dm, int tipo, int indice, bool despues) {
  for (const EscenaIW& e : dm.escenas) {
    if (e.tipo == tipo && e.indice == indice && e.despues == despues && e.video < int(dm.subtitulos.size()) &&
        !dm.subtitulos[size_t(e.video)].empty())
      return e.video;
  }
  return -1;
}

bool LeerDragonMission(const std::filesystem::path& iw, DragonMissionIW& out, std::string& error) {
  out = DragonMissionIW{};
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
  constexpr int kPiezas[4][4] = {{9, 10, 11, 12}, {22, 23, 24, 25}, {48, 49, 50, 51}, {32, 33, 34, 35}};
  for (int m = 0; m < 4; ++m)
    for (int i = 0; i < 4; ++i) out.piezas[m][i] = TexturaAmt(mapas, kPiezas[m][i]);
  out.hud.resize(size_t(std::max(CuantasAmt(mapas), 0)));
  std::vector<int> hud = {kRobot, kGloboMapa, kFlechas, kCifrasRosa, kBarraBolas, kBolas, kClear};
  hud.insert(hud.end(), std::begin(kNombreSaga), std::end(kNombreSaga));
  hud.insert(hud.end(), std::begin(kBulma), std::end(kBulma));
  for (int k : hud) {
    if (k < int(out.hud.size()) && !out.hud[size_t(k)]) out.hud[size_t(k)] = TexturaAmt(mapas, k);
  }
  out.iconos = todas(HijoAmb(b334, 5));
  out.goku = Escala2x(Escala2x(De(out.iconos, kGokuMapa)));
  out.frases = todas(HijoAmb(b334, 6));
  out.ui = todas(HijoAmb(b334, 1));
  out.road = todas(HijoAmb(b333, 0));
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
    if (tipo == 1 && clave > 0x7E1 && clave <= 0x7E1 + kMisiones) {
      c.mision = int(clave - 0x7E1);
      c.icono = cara >= 30 && cara < 88 ? kInsignia[cara - 30] : -1;
    } else if (tipo == 0) {
      c.icono = kInterrogante;   // minijuego (frente P4)
    } else if (tipo == 2) {
      c.icono = kEsfera;
    } else {
      continue;   // 3-4: otras marcas del mapa (sin descifrar)
    }
    if (c.saga >= 0 && c.saga < 6 && c.x <= 1280 && c.y <= 960) out.casillas.push_back(c);
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
      const uint32_t cara = U32(d2, o + 0x10), clave = U32(d2, o + 0x20);
      if (U32(d2, o + 0x18) != 1 || clave < 0x867 || clave > 0x866 + uint32_t(kRegistros - kMisiones)) continue;
      CasillaDM c;
      c.fila = int(i);
      c.clave = int(clave);
      c.mision = kMisiones + int(clave - 0x866);
      c.saga = int(U32(d2, o + 0x04));
      c.x = float(U32(d2, o + 0x08));
      c.y = float(U32(d2, o + 0x0C));
      c.icono = cara >= 30 && cara < 88 ? kInsignia[cara - 30] : -1;
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
  const std::vector<uint8_t> elf = LeerElf(iw);
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

int PantallaDM::Mision(const InfoDM& in) const {
  return in.dm && sel_ >= 0 && sel_ < int(Cas(in).size()) ? Cas(in)[size_t(sel_)].mision : 0;
}

void PantallaDM::ReiniciarMapa() {
  volviendo_ = false;   // una entrada nueva cancela cualquier vuelta de combate pendiente
  t_intro_ = 0;
  ap_t_ = 0;
  tcam_ = 0;
  REXLOG_INFO("dbz3 iw mapa: intro Story_MapIntroDelayTick 100 cuadros; mandos bloqueados");
}

void PantallaDM::VolverDelCombate() {
  // H03 P0: al volver de un combate o minijuego, IW no repite Story_MapIntroDelayTick. El panel de resultado se cierra
  // y corren Story_MapResultDelayTick (60 cuadros) y Story_MapReloadAfterDelayTick (~100) antes de dar las casillas por
  // recargadas. [H] el dato exacto de la recarga (s1: ~100; falta captura sincronizada): no se inventa otro tiempo.
  volviendo_ = true;
  t_vuelta_ = 0;
  res_delay_hecho_ = recarga_hecha_ = false;
  t_intro_ = 1.0f;      // sin espera de entrada
  ap_t_ = 1.0e6f;       // casillas ya visibles (sin la aparicion escalonada)
  tcam_ = 1.0f;
  REXLOG_INFO("dbz3 iw mapa: vuelta de combate: sin Story_MapIntroDelayTick; "
              "Story_MapResultDelayTick 60 y Story_MapReloadAfterDelayTick ~100 [H]");
}

void PantallaDM::Elegir(const InfoDM& in, int mision) {
  if (!in.dm) return;
  const auto& cas = Cas(in);
  for (size_t j = 0; j < cas.size(); ++j) {
    if (cas[j].mision == mision) {
      if (sel_ < 0 || cas[size_t(std::max(sel_, 0))].saga != cas[j].saga) mx_ = -1;   // otro mapa: sin paseo
      sel_ = int(j);
      ReiniciarMapa();   // entrar al mapa: Story_MapIntroDelayTick + aparicion de casillas
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
  }
}

int PantallaDM::NivelCom() const {
  constexpr int k[5] = {0, 0, 1, 2, 2};   // Very Easy, Easy -> Novato; Normal -> Experto; Hard, Very Hard -> Avanzado
  return k[std::clamp(dificultad_, 0, 4)];
}

int PantallaDM::Rango(int puntos) {   // ponytail: umbrales del #MSS de DATA_CMN 890 (3600/3000/2500/2000), sin confirmar
  return puntos >= 3600 ? 4 : puntos >= 3000 ? 3 : puntos >= 2500 ? 2 : puntos >= 2000 ? 1 : 0;
}

// Goku del mapa en 3D, como en IW: DATA_CMN 1093 (el paquete del mapa, con el #DMD) trae a Goku en miniatura (huesos
// GKS_, como el del tablero de B2 pero otro archivo): hijos 0 #AMT, 1 #AMM (sus animaciones) y 2 #AMO (F1, 2026-10-16;
// los grupos 3-5 y 6-8 son otras figuras). Espera y andar: DBZ3_TEST_GOKU_IW_ANIM="espera,andar" (indices del 1093/1).
// ponytail: los indices de espera/andar elegidos por su largo (sin leer la tabla del mapa de IW).
namespace {
struct GokuMapa3D {
  ps2::Modelo m;
  std::vector<ps2::Tex> tx;
  ps2::Anim espera, andar;
  float alto = 0, giro = 0, ymin = 1e9f;
  bool cargado = false;
};
GokuMapa3D g_gm;
void CargarGokuMapa() {
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
  int ie = 0, ia = 3;   // andar: la 3 (52 cuadros, paso simetrico); la 1 era un tiron de 10 (auditoria10/B)
  if (const char* v = std::getenv("DBZ3_TEST_GOKU_IW_ANIM")) std::sscanf(v, "%d,%d", &ie, &ia);
  g_gm.espera = ps2::LeerAnim(amm, uint32_t(ie), g_gm.m.esq->padre.size());
  g_gm.andar = ps2::LeerAnim(amm, uint32_t(ia), g_gm.m.esq->padre.size());
  g_gm.espera.relativa = g_gm.andar.relativa = rel != 0;
  for (const auto& tr : g_gm.m.malla.tris)
    for (const auto& v : tr.p) g_gm.alto = std::max(g_gm.alto, v.y);
  g_gm.tx = ps2::Texturas(g_gm.m);
  REXLOG_INFO("dbz3 iw dm: Goku del mapa 3D (CMN 1093/2): {} triangulos, alto {:.1f}, animaciones (cuadros) {}; espera {}, "
              "andar {}", g_gm.m.malla.tris.size(), g_gm.alto, largos, ie, ia);
}
// Goku del mapa de pie en `pie` (pantalla), `alto_px` de alto, mirando hacia (dx, dy) del mapa si anda; camara casi de
// frente y 20 grados desde arriba (la de R2 para el tablero de B2), con el cel shading de P4.
bool DibGokuMapa(ImDrawList* dl, ImVec2 pie, float alto_px, float t, bool anda, float dx, float dy) {
  CargarGokuMapa();
  if (!g_gm.m.malla || g_gm.alto <= 0) return false;
  if (anda && (dx != 0 || dy != 0)) g_gm.giro = std::atan2(dx, dy);
  const ps2::Anim& an = anda && g_gm.andar ? g_gm.andar : g_gm.espera;
  static ps2::Malla pose;
  const bool anima = bool(an);
  if (anima) ps2::Posar(*g_gm.m.esq, an, t, pose);
  if (g_gm.ymin > 1e8f)
    for (const auto& tr : (anima ? pose : g_gm.m.malla).tris)
      for (const auto& v : tr.p) g_gm.ymin = std::min(g_gm.ymin, v.y);
  ps2::Camara cam;
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
  esc.Poner(cam, anima ? pose : g_gm.m.malla, g_gm.tx, {0, -g_gm.ymin, 0}, g_gm.giro, 1.0f, toon);
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
void PantallaDM::DibujarFondo(ImDrawList* dl, const ImGuiIO& io, float t, const InfoDM& in) {
  const auto& cas = Cas(in);
  if (sel_ < 0 || sel_ >= int(cas.size())) Elegir(in, std::clamp(in.hecho + 1, 1, kRegistros));
  if (sel_ < 0) return;
  const float dt = std::clamp(t - t_ultimo_, 0.0f, 0.1f);
  // Entrada al mapa (no hay aviso del coordinador): un hueco grande entre dos dibujos = mapa reabierto (vuelta de un
  // combate/minijuego). Tras un combate (VolverDelCombate) no se repite la intro: IW recarga las casillas con sus dos
  // esperas. ReiniciarMapa() tambien lo hace Elegir, que es el aviso exacto que existe hoy.
  if (t_intro_ < 0 || t - t_ultimo_ > 0.5f) {
    if (volviendo_) {
      t_intro_ = 1.0f;
      ap_t_ = 1.0e6f;
      tcam_ = 1.0f;
    } else {
      ReiniciarMapa();
    }
  }
  t_ultimo_ = t;
  // H03: Story_MapResultDelayTick (60 cuadros) y Story_MapReloadAfterDelayTick (~100) de la vuelta de combate.
  if (volviendo_) {
    t_vuelta_ += dt * 60.0f;
    if (!res_delay_hecho_ && t_vuelta_ >= 60.0f) {
      res_delay_hecho_ = true;
      REXLOG_INFO("dbz3 iw mapa: Story_MapResultDelayTick 60 cuadros: Goku vuelve a su sitio");
    }
    if (!recarga_hecha_ && t_vuelta_ >= 100.0f) {
      recarga_hecha_ = true;
      volviendo_ = false;
      REXLOG_INFO("dbz3 iw mapa: Story_MapReloadAfterDelayTick ~100 cuadros: casillas recargadas [H]");
    }
  }
  // Story_MapIntroDelayTick (EU 0x2F8790): +0,01 por cuadro (fRam0049c7dc = 1,0 a 60 Hz; 1,2 en el modo de 50) y al
  // pasar de 1,0 se pasa al scroll. Son 100 cuadros (101 por el > 1,0), no un sleep de interfaz.
  if (t_intro_ < 1.0f) {
    t_intro_ = std::min(1.0f, t_intro_ + 0.01f * dt * 60.0f);
    if (t_intro_ >= 1.0f) {
      ap_t_ = 0;      // Story_MapObjectsAppearTick empieza a contar
      tcam_ = 0;      // Story_MapScrollToTargetTick arranca con su acumulador a 0
      REXLOG_INFO("dbz3 iw mapa: intro acabada; aparecen {} casillas", cas.size());
    }
  } else {
    ap_t_ += dt * 60.0f;
  }
  const bool listo = t_intro_ >= 1.0f;
  const CasillaDM& c = cas[size_t(sel_)];
  if (mx_ < 0) {
    mx_ = cx_ = c.x;
    my_ = cy_ = c.y;
  }
  if (listo) {   // durante la espera de entrada, Goku y la camara se quedan quietos (el original no carga mandos)
    const float k1 = 1.0f - std::exp(-dt * 6.0f);
    mx_ += (c.x - mx_) * k1;
    my_ += (c.y - my_) * k1;
    // aplicar_estudios (entrega 16 P4), Story_MapScrollToTargetTick (EU 0x2F8390, L384840-384937): objetivo = Goku del
    // mapa (+0x1D0) limitado a x [300, 980] e y [225, 735]; camara += (objetivo - camara) * t, t += 0,01 por cuadro y
    // vuelve a 0 al pasar de 1; a menos de 2 px, la camara salta al objetivo con t = 1; despues se limita igual.
    constexpr float kX0 = 300, kX1 = 980, kY0 = 225, kY1 = 735;
    const float ox = std::clamp(mx_, kX0, kX1), oy = std::clamp(my_, kY0, kY1);
    if (std::hypot(ox - cx_, oy - cy_) < 2.0f) {
      cx_ = ox, cy_ = oy, tcam_ = 1.0f;
    } else {
      cx_ += (ox - cx_) * tcam_;
      cy_ += (oy - cy_) * tcam_;
    }
    if ((tcam_ += 0.01f * dt * 60.0f) > 1.0f) tcam_ = 0;
    cx_ = std::clamp(cx_, kX0, kX1), cy_ = std::clamp(cy_, kY0, kY1);
  } else {
    cx_ = std::clamp(cx_, 300.0f, 980.0f), cy_ = std::clamp(cy_, 225.0f, 735.0f);
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
  auto P = [&](float x, float y) { return ImVec2(gi.p0.x + D.x * 0.5f + (x - camx) * s, gi.p0.y + D.y * 0.5f + (y - camy) * s); };
  dl->AddRectFilled(gi.p0, gi.p1, IM_COL32(0, 0, 0, 255));
  const int m = MapaDeSaga(c.saga);
  for (int i = 0; i < 4; ++i) {
    const float x = float(i % 2) * 640, y = float(i / 2) * 480;
    Imagen2(dl, 100 + m * 4 + i, in.dm->piezas[m][i], P(x, y), P(x + 640, y + 480));
  }
  // Story_MapObjectsAppearTick (EU 0x2FCCB0): tras la intro, cada 20 cuadros (param+0x34 > 20) aparece UNA casilla,
  // desde la ultima de la lista hacia la primera; el sprite entra con su fundido (w_Appear_FadeFlag_). El orden de
  // casillas es el del #DMD filtrado por area: aqui, las dibujables de esta saga. [H] el fundido exacto: se usa 20
  // cuadros (el mismo umbral que el contador), sin captura que lo separe.
  int n_saga = 0;
  for (const CasillaDM& q : cas)
    if (q.saga == c.saga && q.icono >= 0 && !(q.tipo == 1 && q.mision > in.abiertas)) ++n_saga;
  int pos_saga = 0;
  for (size_t j = 0; j < cas.size(); ++j) {
    const CasillaDM& q = cas[j];
    if (q.saga != c.saga || q.icono < 0) continue;
    if (q.tipo == 1 && q.mision > in.abiertas) continue;   // las de mas adelante aun no salen
    const int rank = n_saga - 1 - pos_saga++;              // la ultima primero (indice count-1 hacia abajo)
    const float t_ap = 20.0f * float(rank + 1);            // una cada 20 cuadros
    const float alfa = listo ? std::clamp((ap_t_ - t_ap) / 20.0f, 0.0f, 1.0f) : 0.0f;
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
    const ImU32 tint = no_b3 ? IM_COL32(110, 110, 110, al) : IM_COL32(255, 255, 255, al);
    Imagen2(dl, 1000 + q.icono, im, P(q.x - w * 0.5f, q.y - h * 0.5f), P(q.x + w * 0.5f, q.y + h * 0.5f), {}, tint);
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
}

PantallaDM::Accion PantallaDM::Mapa(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in) {
  if (!in.dm) return kNada;
  if (video_ >= 0) return Historia(dl, io, t, pulsado, in);
  DibujarFondo(dl, io, t, in);
  if (sel_ < 0) return pulsado & kB ? kSalir : kNada;
  // Story_MapIntroDelayTick: el original no carga los mandos del mapa hasta acabar la espera y el scroll. Aqui solo se
  // bloquea la espera de entrada (100 cuadros); B sigue saliendo para no dejar al jugador atrapado. [H] el bloqueo
  // durante todo el scroll, que depende del tramo recorrido y no se ha medido en pantalla.
  if (EnIntroMapa()) return pulsado & kB ? kSalir : kNada;
  const auto& cas = Cas(in);
  const CasillaDM c = cas[size_t(sel_)];
  auto abierta = [&](const CasillaDM& q) { return q.tipo == 0 || (q.mision > 0 && q.mision <= in.abiertas); };
  const int antes = sel_;
  if (pulsado & (kUp | kDown | kLeft | kRight)) {   // la abierta mas cerca en esa direccion
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
    for (int paso = 1; paso <= 6; ++paso) {
      const int saga = ((c.saga + ((pulsado & kRB) ? paso : -paso)) % 6 + 6) % 6;
      int mejor = -1;
      for (size_t j = 0; j < cas.size(); ++j) {
        if (cas[j].saga == saga && cas[j].tipo == 1 && abierta(cas[j]) &&
            (mejor < 0 || cas[j].mision > cas[size_t(mejor)].mision))
          mejor = int(j);
      }
      if (mejor >= 0) {
        if (saga != c.saga) mx_ = -1;
        sel_ = mejor;
        break;
      }
    }
  }
  if (sel_ != antes) aviso_.clear();
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
  S::HelpLine(dl, ImVec2(gi.p0.x + D.x - 96 * u, gi.p0.y + 18 * u), 20 * u, {{"LB", ""}, {"RB", T::T("Saga", "Saga")}});
  // abajo a la derecha: Bulma (la guia) asomada en su vehiculo
  float bw = 0;
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
  const int bolas = total ? 7 * hechas / total : 0;   // ponytail: una Bola por cada septimo de la saga, sin confirmar
  const float k = 0.9f * u;
  const ImVec2 b0(8 * u, D.y - 8 * u - 56 * k);
  pon(kBarraBolas, b0, ImVec2(b0.x + 304 * k, b0.y + 56 * k));
  for (int i = 0; i < bolas; ++i) {
    const float cx = b0.x + (32 + 40 * float(i)) * k, cy = b0.y + 28 * k;
    pon(kBolas, ImVec2(cx - 22 * k, cy - 20 * k), ImVec2(cx + 18 * k, cy + 20 * k),
        ImVec4(40.0f * float(i), 0, 40.0f * float(i) + 40, 40));
  }
  const float cx0 = b0.x + 304 * k + 6 * u, cy = b0.y + 28 * k;
  pon(kClear, ImVec2(cx0, cy - 20 * k), ImVec2(cx0 + 88 * k, cy + 20 * k));
  char cuenta[16];
  std::snprintf(cuenta, sizeof(cuenta), "%02d/%02d", hechas, total);
  Cifras(dl, false, cuenta, ImVec2(gi.p0.x + cx0 + 88 * k + 6 * u + 5 * 0.9f * 24 * k, gi.p0.y + cy), 24 * k, in, true);
  // globo de la guia (334/3 t01) a lo ancho, encima de la barra, con la frase original de la casilla (334/6)
  const bool no_b3 = q.mision > 0 && q.mision < int(in.motivo.size()) && !in.motivo[size_t(q.mision)].empty();
  const float gx0 = 8 * u, gx1 = std::max(D.x * 0.5f, D.x - std::max(bw * 0.7f, 60 * u)), gy1 = b0.y - 4 * u, gy0 = gy1 - 104 * u;
  if (guia_) {
    pon(kGloboMapa, ImVec2(gx0, gy0), ImVec2(gx1, gy1));
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
  S::mode::Help(dl, S::GameArea(io), {{"Y", in.road ? T::T("A Dragon Mission", "To Dragon Mission") : T::T("A Fighter's Road", "To Fighter's Road")}, {"BACK", T::T("Guía", "Guide")}, {"A", T::T("Elegir", "Select")},
                                                             {"B", in.road ? T::T("Volver", "Back") : T::T("Salir", "Quit")}});
  if (pulsado & kB) return kSalir;
  if (pulsado & (kA | kStart)) {
    if (q.tipo == 0) {   // minijuego: lo hace el frente P4 (RegistrarMinijuegoIW)
      if (dbz3::ingame::tutorial::MostrarMinijuego(q.clave)) return kNada;   // F3: la primera vez, sus controles
      if (const int v = VideoIW(*in.dm, 0, q.indice, false); v >= 0 && g_minijuego) {   // antes, su escena
        Contar(v);
        mini_clave_ = q.clave;
        mini_fila_ = q.fila;
        return kNada;
      }
      if (g_minijuego && g_minijuego(q.clave, q.fila)) return g_mini_clave = q.clave, kMinijuego;
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
  const std::vector<Imagen>* lineas =
      video_ < int(in.dm->subtitulos.size()) ? &in.dm->subtitulos[size_t(video_)] : nullptr;
  if (!probado_) {
    probado_ = true;
    const auto ruta = RutaVideoIW(*in.dm, video_);
    en_video_ = !ruta.empty() && dbz3::video::Reproducir(ruta, lineas ? *lineas : std::vector<Imagen>());
    REXLOG_INFO("dbz3 misiones: iw: video de la historia {} ({}): {}", video_, ruta.filename().string(),
                en_video_ ? "reproduciendo" : "sin video, subtitulos sobre el mapa");
  }
  if (en_video_) {
    DibujarFondo(dl, io, t, in);
    if (dbz3::video::Activo()) return kNada;   // el video va encima y lee A/START
    linea_ = lineas ? int(lineas->size()) : 0;  // acabado o saltado
    pulsado = 0;
  }
  if (pulsado & (kA | kStart)) ++linea_;
  if (!lineas || (pulsado & kB) || linea_ >= int(lineas->size())) {
    REXLOG_INFO("dbz3 misiones: iw: escena de la historia (video {}) acabada en la linea {}", video_, linea_);
    video_ = -1;
    if (mini_clave_ >= 0) {   // la escena era la de antes de un minijuego: ahora, el minijuego
      const int clave = mini_clave_;
      mini_clave_ = -1;
      if (g_minijuego && g_minijuego(clave, mini_fila_)) return g_mini_clave = clave, kMinijuego;
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
  S::mode::Help(dl, S::GameArea(io), {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Saltar", "Skip")}});
  return kNada;
}

PantallaDM::Accion PantallaDM::Ficha(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in) {
  if (!in.dm) return kVolver;
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
  // Menu de la mision de IW (334/1 t11, filas de 40 px: MISSION SETTINGS, CONTROLS, DETAILS, SKILL EDIT; luego
  // MISSION START t14). Arriba/abajo mueve el cursor; el panel de la izquierda ensena la entrada del cursor
  // (DETAILS tambien con el cursor en MISSION START).
  if (pulsado & kUp) menu_ = std::max(0, menu_ - 1);
  if (pulsado & kDown) menu_ = std::min(4, menu_ + 1);
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
    // ponytail: en IW la mision fija el personaje ("como Piccolo"); la lista de t20/t26.. no se recorre aqui.
  } else if (vista == 1) {   // CONTROLS: los del modo (combate de B3) y lo que pide la mision
    const char* l[] = {T::T("Combate con los controles de Budokai 3.", "Battle with the Budokai 3 controls."),
                       T::T("START: pausa.", "START: pause."), in.regla.c_str()};
    float y = 180;
    for (const char* s : l) {
      if (*s) parrafo(s, y, lin, 24);
      y += 130;
    }
  } else {   // SKILL EDIT: B3 no tiene pantalla de capsulas que se pueda abrir desde aqui
    parrafo(T::T("Las misiones usan las cápsulas de serie de cada luchador.",
                 "Missions use each fighter's default capsules."),
            300, lin, 24);
  }
  if (vista != 0) dificultad(70, 712);
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
  // RANK (334/1 t17, fila 1) con el mejor rango de la mision (t24; "-" sin superar): en IW los combates no tienen
  // puntos, guardan el mejor rango (Battle_ComputeRankAndRecord).
  Imagen2(dl, 3000 + kEtiquetas, De(ui, kEtiquetas), a.At(640, 441), a.At(868, 489), ImVec4(0, 32, 152, 64));
  {
    const int rg = (n < int(in.mejor.size()) ? in.mejor[size_t(n)] : 0) - 1;
    const int col = rg < 0 ? 2 : rg >= 3 ? rg - 3 : rg, fila = rg < 0 || rg >= 3 ? 1 : 0;
    Imagen2(dl, 3000 + kRangos, De(ui, kRangos), a.At(1068, 431), a.At(1120, 499),
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
    return kEmpezar;
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
  // SCORE). Combate: TIME y RANK (con New!), sin puntos, como IW. Minijuego: TIME, RANK, BRICKS (solo el de
  // ladrillos), SCORE y TOTAL SCORE = el total de esta partida (Results_ComputeTotalsAndRewards suma las filas; el
  // record no va aqui: es el BEST SCORE).
  std::vector<int> filas = {0, 1};
  if (r.mision <= 0) {
    if (r.ladrillos >= 0) filas.push_back(2);
    filas.push_back(3);
    filas.push_back(4);
  }
  // Ritmo de IW (60 fps): Results_StateBeginRoll espera 100 cuadros antes de la primera fila y Results_UpdateTally
  // pasa de fila cada 60; al acabar, el premio (Results_BeginOutcome). A salta el conteo.
  constexpr float kAntes = 100.0f / 60.0f, kFila = 60.0f / 60.0f;
  if (res_t0_ < 0) res_t0_ = t, res_sonadas_ = 0;
  const int n = int(filas.size());
  if (pulsado & (kA | kStart | kB)) {
    if (res_sonadas_ > n) {
      res_t0_ = -1;
      CallarAdxPs2();
      return true;
    }
    res_t0_ = t - kAntes - kFila * float(n) - 0.01f;   // todo a la vista
  }
  const float el = t - res_t0_;
  const int vistas = el < kAntes ? 0 : std::min(n + 1, 1 + int((el - kAntes) / kFila));   // n + 1 = con el premio
  const float frac = el < kAntes ? 0.0f : std::fmod(el - kAntes, kFila) / kFila;          // de la fila que entra
  while (res_sonadas_ < vistas) SonidoResultado(in, res_sonadas_++ < n ? 0 : r.zeni > 0 ? 1 : 2);
  const float paso = n > 4 ? 66.0f : 80.0f;
  auto etiqueta = [&](int fila, float y) {
    Imagen2(dl, 3000 + kEtiquetas, De(ui, kEtiquetas), a.At(370, y - 24), a.At(598, y + 24),
            ImVec4(0, 32.0f * float(fila), 152, 32.0f * float(fila + 1)));
  };
  auto rango = [&](float y) {
    const int rg = r.rango >= 0 ? std::min(r.rango, 4) : Rango(r.puntos), col = rg >= 3 ? rg - 3 : rg,
              fila = rg >= 3 ? 1 : 0;
    Imagen2(dl, 3000 + kRangos, De(ui, kRangos), a.At(778, y - 34), a.At(830, y + 34),
            ImVec4(32.0f * float(col), 40.0f * float(fila), 32.0f * float(col + 1), 40.0f * float(fila + 1)));
  };
  auto nuevo = [&](float x1, float y) {   // New! a la izquierda de x1 (px de pantalla)
    const float b = 3.0f * std::sin(t * 5.0f);
    Imagen2(dl, 1000 + kNew, De(in.dm->iconos, kNew), ImVec2(x1 - 88 * a.s, a.At(0, y - 32 + b).y),
            ImVec2(x1, a.At(0, y + 32 + b).y));
  };
  for (int i = 0; i < std::min(vistas, n); ++i) {
    const float y = 235.0f + paso * float(i);
    // la cifra de la fila que entra sube de 0 a su valor en media fila (ponytail: IW la suma al final de la fila)
    const float k = i + 1 < vistas ? 1.0f : std::min(1.0f, frac * 2.0f);
    etiqueta(filas[size_t(i)], y);
    switch (filas[size_t(i)]) {
      case 0: {
        const int s = int(float(r.segundos) * k);
        char tiempo[16];
        std::snprintf(tiempo, sizeof(tiempo), "%d'%02d\"", s / 60, s % 60);
        Cifras(dl, true, tiempo, a.At(830, y), 54 * a.s, in);
        break;
      }
      case 1:
        rango(y);
        if (r.mision > 0 && r.nuevo_record) nuevo(a.At(770, 0).x, y);
        break;
      case 2:
        Cifras(dl, true, std::to_string(int(float(r.ladrillos) * k)), a.At(830, y), 54 * a.s, in);
        break;
      case 3: {
        const float izq = Cifras(dl, true, std::to_string(int(float(r.puntos) * k)), a.At(830, y), 54 * a.s, in);
        if (r.nuevo_record) nuevo(izq - 4 * a.s, y);
        break;
      }
      default:
        Cifras(dl, true, std::to_string(int(float(r.puntos) * k)), a.At(830, y), 54 * a.s, in);
    }
  }
  if (vistas > n) {   // EARNED! ... ZENI (333/0 t19 y t07)
    const float ez = n > 4 ? 640.0f : 600.0f;
    Imagen2(dl, 4000 + kEarned, De(in.dm->road, kEarned), a.At(370, ez - 32), a.At(575, ez + 32));
    Cifras(dl, true, std::to_string(r.zeni), a.At(735, ez), 54 * a.s, in);
    Imagen2(dl, 4000 + kZeniTxt, De(in.dm->road, kZeniTxt), a.At(742, ez - 10), a.At(832, ez + 28));
  }
  S::mode::Help(dl, a, {{"A", vistas > n ? T::T("Al mapa", "To the map") : T::T("Saltar", "Skip")}});
  return false;
}

}  // namespace dbz3::pasarela
