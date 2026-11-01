// dbz3 - Ver native_ui.h. Formatos (los mismos que leen mod center hd/roster_build.py y
// texturas_originales.py):
//   - AFS: "AFS\0", n (u32 LE) y tabla de (direccion, tamano) u32 LE desde +8.
//   - Entrada comprimida con XMemCompress "nativo": cabecera de 0x30 B (BE: magia 0FF512EE,
//     ventana +0x10, tamano sin comprimir u64 +0x18) y bloques (u32 tamano) de tramas LZX:
//     [FF, u16 sin comprimir, u16 comprimido] o [u16 comprimido] (0x8000 sin comprimir).
//   - Texturas: DDS normales ("DDS " + 124) dentro de la entrada; se reconocen por el XXH3 del
//     nivel base, el mismo hash que usa el pack de texturas HD.
#include "native_ui.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/system/lzx.h>

#include "../launcher/settings.h"
#include "../region.h"

namespace dbz3::ingame::native_ui {
namespace {

struct Piece {
  const char* name;
  int entry;        // data_usi.afs
  uint64_t hash;    // XXH3 del nivel base (nombre de la textura en el pack HD)
  bool trim;        // recortar al dibujo (los anfitriones se colocan por los pies)
  bool cmn = false; // H11: la entrada es de data_cmn.afs (efectos de combate) y no de data_usi.afs
};
constexpr Piece kPieces[] = {
    {"fondo_duelo", 2055, 0x13AF5A36B28A0842ull, false},  // Duelo: cielo rosa y arboles
    {"pergamino", 2033, 0xF35DB93F312ABC3Eull, false},    // cinta del titulo (rodillo verde y cordon)
    {"borde", 2033, 0x4B735AD364D513BAull, false},        // tira dorada de los lados (sombra a la izquierda)
    {"marco", 2033, 0xDECD0591337A311Bull, false},
    {"agua", 2033, 0x20EA5281A0037C1Dull, false},         // agua azul de la franja del texto elegido (512x128)
    {"panel", 2033, 0x4048A4AF7E6C64CFull, false},        // interior oscuro de las listas (4,7-309,350)
    {"filas_vs", 2055, 0x1732359AF24A2EFBull, false},     // 1P VS COM, 1P VS 2P...
    {"bocadillo", 2055, 0xBEFBE5B92F49B4F9ull, false},
    {"flechas", 2062, 0x31874164D2247BDEull, false},
    {"botones", 2062, 0xAEA34DDB47477372ull, false},      // LB LT RB RT Y B A X
    {"boton_back", 2033, 0x9607A868D438558Eull, false},
    {"boton_start", 2062, 0xE56FC5E5DEFE917Bull, false},
    {"anfitrion_tagteam_a", 2055, 0x356EB63B3D15A405ull, true},   // Kaio del Duelo
    {"anfitrion_tagteam_b", 2055, 0x44657FFB8C1D8B3Full, true},   // Kaio del Duelo
    {"anfitrion_online", 2055, 0x107B679B010365CDull, true},      // Kaio del Norte
    {"icono", 2045, 0x7E84F2B3B86A667Dull, true},                 // bola de 1 estrella: icono de la ventana
    {"bola_estrella", 2045, 0x7E84F2B3B86A667Dull, true},         // H11: la misma bola para Dragon Tactics (la de arriba la consume el icono de la ventana)
    // Skill Shop (tiendas de los retos, rpg.cpp)
    {"capsulas", 2064, 0x109EBFC124CBEB25ull, false},      // botellas roja, verde, amarilla y gris (31x83, cada 36 px)
    {"caja_zenie", 2053, 0xE607F676540C0AF1ull, false},    // caja del dinero con monedas (1,3-323,105)
    {"letras_zenie", 2053, 0xC1F09CA7DC159B3Cull, false},  // rotulo ZENIE (2,6-152,55)
    {"cifras_zenie", 2053, 0xF5EAAD6A69134362ull, false},  // cifras naranjas 0-9 y coma (1024x256, y 10-127)
    // marcador del combate (data_usi 0, 256x256): cifras 0-9 blancas con perfil (0-4 en y 4-34, 5-9 en y 40-70),
    // caras del contador de rivales, "/" (124,76-139,94), "V" y "%"; el juego las tine (el "2/2", naranja). Frente R6
    {"cifras_hud", 0, 0x38CE22710151CFA1ull, false},
    {"vs_torneo", 2047, 0xEEC9E52D040DC990ull, false},     // "VS" naranja del Torneo Mundial (0,4-236,103). R6
    // H11 (Dragon Tactics): texturas de efectos del combate del juego (data_cmn, DXT3). 504 = efectos comunes; 553 = efectos de Vegeta.
    {"fx_impacto", 504, 0x40AF1EBA62CD4A73ull, false, true},   // estallido radial blanco 256x256
    {"fx_anillo", 504, 0xC5C95EF10DC7667Dull, false, true},    // onda de choque 64x64
    {"fx_nucleo", 504, 0xBC4107469032B90Full, false, true},    // punto blanco 32x32
    {"fx_rayo", 504, 0xA85E604E0EA9A4C9ull, false, true},      // chorro de energia azul 64x64
    {"fx_halo", 553, 0x9E585F231E5CD258ull, false, true},      // ovalo blanco suave 64x64
    {"fx_brillo", 553, 0x0F42A9F23C423FC0ull, false, true},    // bola de brillo rosa 64x64
    {"fx_chispas", 531, 0xC8611BC86718688Bull, false, true},   // chispas de golpe 128x128
    // Dragon Tactics: bola y rayo de la tecnica de energia de cada unidad, tal como los liga el juego nativo (BSP del personaje en data_cmn;
    // tabla generada por mod center hd/tactics_fx.py: solo entrada y hash, la imagen sale de la copia del jugador). Nombres tkfx/<id>/bola|rayo.
#define TKFX(id, entrada, hbola, hrayo, r, g, b)     {"tkfx/" id "/bola", entrada, hbola, false, true}, {"tkfx/" id "/rayo", entrada, hrayo, false, true},
#include "../tactics/fx_nativo.inc"
#undef TKFX
};
// Cara del marcador: ID -> entrada de data_cmn (#AZT hud_face; textura 0 = forma normal).
constexpr std::pair<int, int> kFaces[] = {
    {0, 476},  {1, 474},  {2, 473},  {3, 472},  {4, 471},  {5, 478},  {6, 480},  {7, 500},
    {8, 494},  {9, 493},  {10, 482}, {11, 487}, {12, 496}, {13, 502}, {14, 492}, {15, 498},
    {16, 485}, {17, 497}, {18, 489}, {19, 486}, {20, 475}, {21, 490}, {27, 468}, {28, 452},
    {29, 453}, {30, 454}, {32, 456}, {33, 463}, {34, 460}, {35, 461}, {36, 462}, {37, 466},
    {38, 465}, {39, 457}, {40, 459}, {41, 455}, {42, 491}, {43, 464}};

std::mutex g_mu;
std::map<std::string, Image> g_images;
std::atomic<bool> g_started{false}, g_ready{false};

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }

std::vector<uint8_t> ReadEntry(const std::filesystem::path& afs, int n) {
  if (n < 0) return {};
  std::ifstream f(afs, std::ios::binary | std::ios::ate);
  const auto end = f.tellg();
  if (end < std::streamoff(8)) return {};
  const uint64_t file_size = static_cast<uint64_t>(end);
  f.seekg(0);
  uint8_t head[8];
  if (!f.read(reinterpret_cast<char*>(head), 8) || std::memcmp(head, "AFS", 3) != 0) return {};
  const uint64_t count = Le32(head + 4);
  const uint64_t table_end = 8 + count * 8;
  if (static_cast<uint64_t>(n) >= count || table_end > file_size) return {};
  uint8_t ent[8];
  f.seekg(8 + 8 * std::streamoff(n));
  if (!f.read(reinterpret_cast<char*>(ent), 8)) return {};
  const uint64_t offset = Le32(ent), length = Le32(ent + 4);
  // El lector de UI ya limita descompresión a 64 MiB; acotar también entrada cruda.
  if (!length || length > (64ull << 20) || offset < table_end ||
      offset > file_size || length > file_size - offset) return {};
  std::vector<uint8_t> raw(static_cast<size_t>(length));
  f.seekg(static_cast<std::streamoff>(offset));
  if (raw.empty() || !f.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()))) return {};
  // XMemCompress nativo: las tramas LZX seguidas, sin sus cabeceras, como hace el SDK con el xex
  if (raw.size() < 0x30 || Be32(raw.data()) != 0x0FF512EE) return raw;
  const uint32_t window = Be32(&raw[0x10]);
  const uint64_t total = uint64_t(Be32(&raw[0x18])) << 32 | Be32(&raw[0x1C]);
  if (total == 0 || total > (64ull << 20)) return {};
  std::vector<uint8_t> stream;
  stream.reserve(raw.size());
  for (size_t p = 0x30; p + 4 <= raw.size();) {
    const size_t end = std::min(raw.size(), p + 4 + Be32(&raw[p]));
    if (end == p + 4) break;
    for (size_t q = p + 4; q + 2 <= end;) {
      size_t c;
      if (raw[q] == 0xFF) {
        if (q + 5 > end) break;
        c = Be16(&raw[q + 3]);
        q += 5;
      } else {
        c = Be16(&raw[q]);
        q += 2;
      }
      if (c == 0 || q + c > end) break;
      stream.insert(stream.end(), raw.begin() + std::ptrdiff_t(q), raw.begin() + std::ptrdiff_t(q + c));
      q += c;
    }
    p = end;
  }
  std::vector<uint8_t> out(total);
  if (lzx_decompress(stream.data(), stream.size(), out.data(), out.size(), window, nullptr, 0) != 0) return {};
  return out;
}

// DXT3 (BC2): bloques de 4x4 con alfa de 4 bits (8 B) y color 565 (8 B).
void DecodeDxt3(const uint8_t* src, int w, int h, uint8_t* dst) {
  for (int by = 0; by < (h + 3) / 4; ++by) {
    for (int bx = 0; bx < (w + 3) / 4; ++bx, src += 16) {
      uint8_t c[4][3];
      for (int k = 0; k < 2; ++k) {
        const uint16_t v = uint16_t(src[8 + 2 * k] | src[9 + 2 * k] << 8);
        c[k][0] = uint8_t((v >> 11) * 255 / 31);
        c[k][1] = uint8_t(((v >> 5) & 63) * 255 / 63);
        c[k][2] = uint8_t((v & 31) * 255 / 31);
      }
      for (int j = 0; j < 3; ++j) {
        c[2][j] = uint8_t((2 * c[0][j] + c[1][j]) / 3);
        c[3][j] = uint8_t((c[0][j] + 2 * c[1][j]) / 3);
      }
      const uint32_t cbits = Le32(src + 12);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= w || y >= h) continue;
        uint8_t* o = dst + (size_t(y) * w + x) * 4;
        const int ci = (cbits >> (2 * i)) & 3;
        o[0] = c[ci][0];
        o[1] = c[ci][1];
        o[2] = c[ci][2];
        const int a4 = (src[i / 2] >> (4 * (i & 1))) & 15;
        o[3] = uint8_t(a4 * 17);
      }
    }
  }
}

// DXT5 (BC3): bloques de 4x4 con alfa interpolado (8 B) y color 565 (8 B).
void DecodeDxt5(const uint8_t* src, int w, int h, uint8_t* dst) {
  for (int by = 0; by < (h + 3) / 4; ++by) {
    for (int bx = 0; bx < (w + 3) / 4; ++bx, src += 16) {
      uint8_t a[8] = {src[0], src[1]};
      for (int i = 2; i < 8; ++i) {
        a[i] = a[0] > a[1] ? uint8_t(((8 - i) * a[0] + (i - 1) * a[1]) / 7)
                           : i < 6 ? uint8_t(((6 - i) * a[0] + (i - 1) * a[1]) / 5) : (i == 6 ? 0 : 255);
      }
      uint64_t abits = 0;
      for (int i = 0; i < 6; ++i) abits |= uint64_t(src[2 + i]) << (8 * i);
      uint8_t c[4][3];
      for (int k = 0; k < 2; ++k) {
        const uint16_t v = uint16_t(src[8 + 2 * k] | src[9 + 2 * k] << 8);
        c[k][0] = uint8_t((v >> 11) * 255 / 31);
        c[k][1] = uint8_t(((v >> 5) & 63) * 255 / 63);
        c[k][2] = uint8_t((v & 31) * 255 / 31);
      }
      for (int j = 0; j < 3; ++j) {
        c[2][j] = uint8_t((2 * c[0][j] + c[1][j]) / 3);
        c[3][j] = uint8_t((c[0][j] + 2 * c[1][j]) / 3);
      }
      const uint32_t cbits = Le32(src + 12);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= w || y >= h) continue;
        uint8_t* o = dst + (size_t(y) * w + x) * 4;
        const int ci = (cbits >> (2 * i)) & 3;
        o[0] = c[ci][0];
        o[1] = c[ci][1];
        o[2] = c[ci][2];
        o[3] = a[(abits >> (3 * i)) & 7];
      }
    }
  }
}

int Shift(uint32_t m) {
  int s = 0;
  while (m && !(m & 1)) {
    m >>= 1;
    ++s;
  }
  return s;
}

// DDS en d (cabecera LE de 128 B: alto +12, ancho +16, formato +80/+84/+88, mascaras +92):
// tamano del nivel base si es RGBA8 o DXT5 y cabe en avail; si no, 0.
size_t DdsSize(const uint8_t* d, size_t avail) {
  if (avail < 128) return 0;
  const int h = int(Le32(d + 12)), w = int(Le32(d + 16));
  const uint32_t flags = Le32(d + 80), bits = Le32(d + 88);
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
  const size_t size = !(flags & 4) && bits == 32                           ? size_t(w) * h * 4
                      : (flags & 4) && (std::memcmp(d + 84, "DXT5", 4) == 0 || std::memcmp(d + 84, "DXT3", 4) == 0) ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16
                                                                            : 0;
  return 128 + size <= avail ? size : 0;
}

bool DecodeDds(const uint8_t* d, size_t avail, Image& img) {
  const size_t size = DdsSize(d, avail);
  if (!size) return false;
  const int h = int(Le32(d + 12)), w = int(Le32(d + 16));
  const bool dxt5 = Le32(d + 80) & 4, dxt3 = dxt5 && std::memcmp(d + 84, "DXT3", 4) == 0;
  img.w = w;
  img.h = h;
  img.rgba.assign(size_t(w) * h * 4, 0);
  if (dxt5) {
    (dxt3 ? DecodeDxt3 : DecodeDxt5)(d + 128, w, h, img.rgba.data());
    return true;
  }
  uint32_t m[4] = {Le32(d + 92), Le32(d + 96), Le32(d + 100), Le32(d + 104)};
  if (!m[0] && !m[1] && !m[2]) {   // sin mascaras: A8R8G8B8
    m[0] = 0x00FF0000;
    m[1] = 0x0000FF00;
    m[2] = 0x000000FF;
    m[3] = 0xFF000000;
  }
  const int sh[4] = {Shift(m[0]), Shift(m[1]), Shift(m[2]), Shift(m[3])};
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const uint32_t v = Le32(d + 128 + 4 * i);
    for (int k = 0; k < 4; ++k) img.rgba[4 * i + k] = m[k] ? uint8_t((v & m[k]) >> sh[k]) : 255;
  }
  return true;
}

void Trim(Image& img) {
  int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x)
      if (img.rgba[(size_t(y) * img.w + x) * 4 + 3] > 8) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
  if (x1 < 0) return;
  Image out;
  out.w = x1 - x0 + 1;
  out.h = y1 - y0 + 1;
  out.rgba.resize(size_t(out.w) * out.h * 4);
  for (int y = 0; y < out.h; ++y) {
    std::memcpy(&out.rgba[size_t(y) * out.w * 4], &img.rgba[(size_t(y + y0) * img.w + x0) * 4], size_t(out.w) * 4);
  }
  img = std::move(out);
}

void Extract(std::filesystem::path dir) {
  int found = 0, faces = 0, fx_tecnicas = 0;
  // EU no trae data_usi.afs: la de US si esta al lado, si no su data_eng.afs. Las piezas se
  // reconocen por el hash de la textura, asi que un archivo que no las tenga solo no las da.
  std::filesystem::path usi = dir / "data_usi.afs";
  std::error_code ec;
  for (const auto& alt : {dir.parent_path() / "us" / "data_usi.afs", dir / "data_eng.afs"}) {
    if (std::filesystem::exists(usi, ec)) break;
    usi = alt;
  }
  for (const bool cmn : {false, true}) {   // H11: primero las de data_usi, luego las de data_cmn
    std::vector<int> entries;
    for (const Piece& p : kPieces)
      if (p.cmn == cmn && std::find(entries.begin(), entries.end(), p.entry) == entries.end()) entries.push_back(p.entry);
    for (const int n : entries) {
      const std::vector<uint8_t> b = ReadEntry(cmn ? dir / "data_cmn.afs" : usi, n);
      for (size_t i = 0; i + 128 <= b.size(); ++i) {
        if (std::memcmp(&b[i], "DDS |", 5) != 0) continue;
        const size_t size = DdsSize(&b[i], b.size() - i);
        if (!size) continue;
        const uint64_t hash = XXH3_64bits(&b[i + 128], size);
        for (const Piece& p : kPieces) {
          Image img;
          if (p.cmn != cmn || p.entry != n || p.hash != hash || !DecodeDds(&b[i], b.size() - i, img)) continue;
          if (p.trim) Trim(img);
          std::lock_guard<std::mutex> lock(g_mu);
          if (std::strncmp(p.name, "tkfx/", 5) == 0) ++fx_tecnicas;
          g_images[p.name] = std::move(img);
          ++found;
        }
      }
    }
  }
  for (const auto& [id, n] : kFaces) {
    const std::vector<uint8_t> b = ReadEntry(dir / "data_cmn.afs", n);
    const auto z = std::search(b.begin(), b.end(), "#AZT", "#AZT" + 4) - b.begin();
    if (size_t(z) + 0x18 > b.size()) continue;
    const size_t idx = size_t(z) + Be32(&b[size_t(z) + 0x14]);
    if (idx + 4 > b.size()) continue;
    const size_t o = size_t(z) + Be32(&b[idx]);
    if (o + 24 > b.size()) continue;
    const size_t at = size_t(z) + Be32(&b[o + 20]);
    Image img;
    if (at < b.size() && DecodeDds(&b[at], b.size() - at, img)) {
      std::lock_guard<std::mutex> lock(g_mu);
      g_images["caras/" + std::to_string(id)] = std::move(img);
      ++faces;
    }
  }
  REXLOG_INFO("dbz3 ui: {} de {} piezas ({} de tecnicas de Tactics) y {} caras sacadas del juego ({})", found, std::size(kPieces), fx_tecnicas, faces,
              dir.string());
}

}  // namespace

void Prefetch() {
  const std::filesystem::path root = dbz3::HostDataRoot();
  if (root.empty() || g_started.exchange(true)) return;
  // B24: la carpeta de la region montada (dbz3_region) gana aunque exista la otra
  std::thread([dir = root / (dbz3::settings::Region() == "eu" ? "eu" : "us")]() {
    Extract(dir);
    g_ready = true;
  }).detach();
}

bool Ready() { return g_ready.load(); }

bool Take(const std::string& name, Image& out) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_images.find(name);
  if (it == g_images.end()) return false;
  out = std::move(it->second);
  g_images.erase(it);
  return true;
}

}  // namespace dbz3::ingame::native_ui
