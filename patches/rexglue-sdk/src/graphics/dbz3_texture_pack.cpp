// DBZ3 HD Collection: indice y decodificacion de "packs" de texturas.
// Ver dbz3_texture_pack.h para el formato.

#include "dbz3_texture_pack.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/math.h>

// Decodificacion PNG (stb_image, solo PNG). El plugin no puede usar el helper
// de rexui (visibilidad oculta entre DLLs), asi que compila su propia copia.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>

namespace rex::graphics {

// Lista de carpetas de pack activas, separadas por ';'. La escribe el launcher
// (`src/launcher/settings.cpp`) en el TOML tras detectar los packs en `mods/`.
//
// 🔴 NO se define aqui a proposito: el registro de cvars es COMPARTIDO entre el
// exe y este plugin y el launcher se carga antes, asi que una definicion
// duplicada aqui se DESCARTA con un error ("duplicate registration ... second
// registration ignored") y el storage de este modulo nunca se rellenaria. Se lee
// por nombre con `REXCVAR_QUERY`, que resuelve el valor desde el registro.
// (Mismo caso que `dbz3_texture_dump` en `d3d12/texture_cache.cpp`.)

uint32_t Dbz3DdsFourCc(xenos::TextureFormat format) {
  switch (format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT1_AS_16_16_16_16:
      return 0x31545844u;  // 'DXT1'
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
      return 0x33545844u;  // 'DXT3'
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return 0x35545844u;  // 'DXT5'
    default:
      return 0;
  }
}

bool Dbz3PackReplaceableFormat(xenos::TextureFormat format) {
  if (Dbz3DdsFourCc(format) != 0) {
    return true;  // DXT1/3/5: el recurso host se crea RGBA8 (descomprimido).
  }
  // Nativas que ya son RGBA8 en memoria (swizzle identidad en el host).
  return format == xenos::TextureFormat::k_8_8_8_8 ||
         format == xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16;
}

// Formatos sin comprimir volcables. Los masks son los del guest (el volcado es
// el bitmap CRUDO). Ver el comentario del header para la verificacion.
const Dbz3DumpFormat* Dbz3DumpFormatFor(xenos::TextureFormat format) {
  // Comprimidos: el sufijo es el FourCC (mismo que ya usaba el volcado).
  static const Dbz3DumpFormat kDxt1 = {"DXT1", true, 0x31545844u, 0, 0, 0, 0, 0};
  static const Dbz3DumpFormat kDxt3 = {"DXT3", true, 0x33545844u, 0, 0, 0, 0, 0};
  static const Dbz3DumpFormat kDxt5 = {"DXT5", true, 0x35545844u, 0, 0, 0, 0, 0};
  // Sin comprimir (bits, R, G, B, A).
  // k_8_8_8_8: R,G,B,A en memoria (XePackR8G8B8A8UNorm: R | G<<8 | B<<16 | A<<24).
  static const Dbz3DumpFormat kRgba8 = {"RGBA8", false, 0, 32, 0x000000FFu, 0x0000FF00u,
                                        0x00FF0000u, 0xFF000000u};
  // k_2_10_10_10: passthrough 32bpp -> mismo layout que DXGI R10G10B10A2_UNORM.
  static const Dbz3DumpFormat kRgba1010102 = {"RGBA1010102", false, 0, 32, 0x000003FFu, 0x000FFC00u,
                                              0x3FF00000u, 0xC0000000u};
  // k_1_5_5_5 (XePackR5G5B5A1UNorm: R | G<<5 | B<<10 | A<<15).
  static const Dbz3DumpFormat kRgb5a1 = {"RGB5A1", false, 0, 16, 0x001Fu, 0x03E0u, 0x7C00u, 0x8000u};
  // k_5_6_5 (XePackR5G6B5UNorm: R | G<<5 | B<<11).
  static const Dbz3DumpFormat kRgb565 = {"RGB565", false, 0, 16, 0x001Fu, 0x07E0u, 0xF800u, 0};
  // k_6_5_5 (XePackR5G5B6UNorm: R5 | G5<<5 | B6<<10).
  static const Dbz3DumpFormat kRgb655 = {"RGB655", false, 0, 16, 0x001Fu, 0x03E0u, 0xFC00u, 0};
  // k_4_4_4_4 (XeR4G4B4A4ToB4G4R4A4: A en los bits altos).
  static const Dbz3DumpFormat kRgba4 = {"RGBA4", false, 0, 16, 0x000Fu, 0x00F0u, 0x0F00u, 0xF000u};
  // k_8 / k_8_A / k_8_B: un canal (se muestra como luminancia).
  static const Dbz3DumpFormat kL8 = {"L8", false, 0, 8, 0xFFu, 0, 0, 0};
  // k_8_8: R,G por texel (con el swizzle RGGG se muestrea como luminancia+alpha).
  static const Dbz3DumpFormat kL8a8 = {"L8A8", false, 0, 16, 0x00FFu, 0, 0, 0xFF00u};
  switch (format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT1_AS_16_16_16_16:
      return &kDxt1;
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
      return &kDxt3;
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return &kDxt5;
    case xenos::TextureFormat::k_8_8_8_8:
    case xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16:
      return &kRgba8;
    case xenos::TextureFormat::k_2_10_10_10:
    case xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16:
      return &kRgba1010102;
    case xenos::TextureFormat::k_1_5_5_5:
      return &kRgb5a1;
    case xenos::TextureFormat::k_5_6_5:
      return &kRgb565;
    case xenos::TextureFormat::k_6_5_5:
      return &kRgb655;
    case xenos::TextureFormat::k_4_4_4_4:
      return &kRgba4;
    case xenos::TextureFormat::k_8:
    case xenos::TextureFormat::k_8_A:
    case xenos::TextureFormat::k_8_B:
      return &kL8;
    case xenos::TextureFormat::k_8_8:
      return &kL8a8;
    default:
      return nullptr;
  }
}

namespace {

bool IsHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Parsea "<hash:16 hex>_<W>x<H>_<FOURCC>" -> hash, W, H. Devuelve false si el
// nombre no sigue la convencion del volcado.
bool ParsePackFileName(const std::string& stem, uint64_t& out_hash, uint32_t& out_width,
                       uint32_t& out_height) {
  if (stem.size() < 16) {
    return false;
  }
  uint64_t hash = 0;
  for (int i = 0; i < 16; ++i) {
    if (!IsHex(stem[i])) {
      return false;
    }
    const char c = stem[i];
    uint32_t nibble = (c >= '0' && c <= '9')   ? uint32_t(c - '0')
                      : (c >= 'a' && c <= 'f') ? uint32_t(c - 'a' + 10)
                                               : uint32_t(c - 'A' + 10);
    hash = (hash << 4) | nibble;
  }
  if (stem[16] != '_') {
    return false;
  }
  size_t pos = 17;
  uint32_t width = 0;
  while (pos < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pos]))) {
    width = width * 10 + uint32_t(stem[pos] - '0');
    ++pos;
  }
  if (pos >= stem.size() || stem[pos] != 'x' || width == 0) {
    return false;
  }
  ++pos;
  uint32_t height = 0;
  while (pos < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pos]))) {
    height = height * 10 + uint32_t(stem[pos] - '0');
    ++pos;
  }
  if (height == 0) {
    return false;
  }
  out_hash = hash;
  out_width = width;
  out_height = height;
  return true;
}

// --- decodificacion DDS ---------------------------------------------------

uint32_t Rgb565ToR(uint16_t c) {
  uint32_t v = (c >> 11) & 0x1F;
  return (v << 3) | (v >> 2);
}
uint32_t Rgb565ToG(uint16_t c) {
  uint32_t v = (c >> 5) & 0x3F;
  return (v << 2) | (v >> 4);
}
uint32_t Rgb565ToB(uint16_t c) {
  uint32_t v = c & 0x1F;
  return (v << 3) | (v >> 2);
}

void DecodeBcColorBlock(const uint8_t* src, uint8_t* dst, uint32_t dst_pitch, bool allow_1bit_alpha,
                        uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
  const uint16_t c0 = uint16_t(src[0] | (src[1] << 8));
  const uint16_t c1 = uint16_t(src[2] | (src[3] << 8));
  uint8_t palette[4][4];
  palette[0][0] = uint8_t(Rgb565ToR(c0));
  palette[0][1] = uint8_t(Rgb565ToG(c0));
  palette[0][2] = uint8_t(Rgb565ToB(c0));
  palette[0][3] = 255;
  palette[1][0] = uint8_t(Rgb565ToR(c1));
  palette[1][1] = uint8_t(Rgb565ToG(c1));
  palette[1][2] = uint8_t(Rgb565ToB(c1));
  palette[1][3] = 255;
  if (c0 > c1 || !allow_1bit_alpha) {
    for (int i = 0; i < 3; ++i) {
      palette[2][i] = uint8_t((2 * palette[0][i] + palette[1][i] + 1) / 3);
      palette[3][i] = uint8_t((palette[0][i] + 2 * palette[1][i] + 1) / 3);
    }
    palette[2][3] = 255;
    palette[3][3] = 255;
  } else {
    for (int i = 0; i < 3; ++i) {
      palette[2][i] = uint8_t((palette[0][i] + palette[1][i] + 1) / 2);
    }
    palette[2][3] = 255;
    palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
  }
  uint32_t bits = uint32_t(src[4]) | (uint32_t(src[5]) << 8) | (uint32_t(src[6]) << 16) |
                  (uint32_t(src[7]) << 24);
  for (uint32_t y = 0; y < 4; ++y) {
    if (by + y >= bh) {
      break;
    }
    for (uint32_t x = 0; x < 4; ++x) {
      if (bx + x >= bw) {
        continue;
      }
      const uint32_t idx = (bits >> ((y * 4 + x) * 2)) & 0x3;
      uint8_t* p = dst + size_t(by + y) * dst_pitch + size_t(bx + x) * 4;
      p[0] = palette[idx][0];
      p[1] = palette[idx][1];
      p[2] = palette[idx][2];
      p[3] = palette[idx][3];
    }
  }
}

void DecodeBc2AlphaBlock(const uint8_t* src, uint8_t* dst, uint32_t dst_pitch, uint32_t bx,
                         uint32_t by, uint32_t bw, uint32_t bh) {
  for (uint32_t y = 0; y < 4; ++y) {
    if (by + y >= bh) {
      break;
    }
    for (uint32_t x = 0; x < 4; ++x) {
      if (bx + x >= bw) {
        continue;
      }
      const uint32_t byte = src[(y * 4 + x) >> 1];
      const uint32_t nibble = (x & 1) ? (byte >> 4) : (byte & 0xF);
      dst[size_t(by + y) * dst_pitch + size_t(bx + x) * 4 + 3] = uint8_t(nibble * 17);
    }
  }
}

void DecodeBc3AlphaBlock(const uint8_t* src, uint8_t* dst, uint32_t dst_pitch, uint32_t bx,
                         uint32_t by, uint32_t bw, uint32_t bh) {
  const uint8_t a0 = src[0];
  const uint8_t a1 = src[1];
  uint8_t palette[8];
  palette[0] = a0;
  palette[1] = a1;
  if (a0 > a1) {
    for (int i = 1; i <= 6; ++i) {
      palette[1 + i] = uint8_t(((7 - i) * a0 + i * a1 + 3) / 7);
    }
    palette[7] = 255;
  } else {
    for (int i = 1; i <= 4; ++i) {
      palette[1 + i] = uint8_t(((5 - i) * a0 + i * a1 + 2) / 5);
    }
    palette[6] = 0;
    palette[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) {
    bits |= uint64_t(src[2 + i]) << (8 * i);
  }
  for (uint32_t y = 0; y < 4; ++y) {
    if (by + y >= bh) {
      break;
    }
    for (uint32_t x = 0; x < 4; ++x) {
      if (bx + x >= bw) {
        continue;
      }
      const uint32_t idx = uint32_t((bits >> ((y * 4 + x) * 3)) & 0x7);
      dst[size_t(by + y) * dst_pitch + size_t(bx + x) * 4 + 3] = palette[idx];
    }
  }
}

bool DecodeBc(const uint8_t* data, size_t data_size, uint32_t fourcc, uint32_t width,
              uint32_t height, std::vector<uint8_t>& out_rgba) {
  uint32_t block_bytes;
  switch (fourcc) {
    case 0x31545844u:  // DXT1
      block_bytes = 8;
      break;
    case 0x33545844u:  // DXT3
    case 0x35545844u:  // DXT5
      block_bytes = 16;
      break;
    default:
      return false;
  }
  const uint32_t blocks_x = (width + 3) / 4;
  const uint32_t blocks_y = (height + 3) / 4;
  if (data_size < size_t(blocks_x) * blocks_y * block_bytes) {
    return false;
  }
  out_rgba.assign(size_t(width) * height * 4, 0);
  const uint32_t dst_pitch = width * 4;
  const uint8_t* src = data;
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint32_t px = bx * 4;
      const uint32_t py = by * 4;
      if (fourcc == 0x31545844u) {
        DecodeBcColorBlock(src, out_rgba.data(), dst_pitch, true, px, py, width, height);
      } else if (fourcc == 0x33545844u) {
        DecodeBc2AlphaBlock(src, out_rgba.data(), dst_pitch, px, py, width, height);
        DecodeBcColorBlock(src + 8, out_rgba.data(), dst_pitch, false, px, py, width, height);
      } else {
        DecodeBc3AlphaBlock(src, out_rgba.data(), dst_pitch, px, py, width, height);
        DecodeBcColorBlock(src + 8, out_rgba.data(), dst_pitch, false, px, py, width, height);
      }
      src += block_bytes;
    }
  }
  return true;
}

bool DecodeUncompressed32(const uint8_t* data, size_t data_size, uint32_t width, uint32_t height,
                          uint32_t r_mask, uint32_t g_mask, uint32_t b_mask, uint32_t a_mask,
                          std::vector<uint8_t>& out_rgba) {
  if (data_size < size_t(width) * height * 4) {
    return false;
  }
  auto extract = [](uint32_t v, uint32_t mask) -> uint8_t {
    if (mask == 0) {
      return 255;
    }
    uint32_t shift = 0;
    while (((mask >> shift) & 1) == 0 && shift < 32) {
      ++shift;
    }
    const uint32_t field = mask >> shift;
    const uint32_t value = (v & mask) >> shift;
    uint32_t max = field;
    return uint8_t((value * 255 + max / 2) / max);
  };
  out_rgba.resize(size_t(width) * height * 4);
  for (size_t i = 0; i < size_t(width) * height; ++i) {
    const uint32_t v = uint32_t(data[i * 4]) | (uint32_t(data[i * 4 + 1]) << 8) |
                       (uint32_t(data[i * 4 + 2]) << 16) | (uint32_t(data[i * 4 + 3]) << 24);
    out_rgba[i * 4 + 0] = extract(v, r_mask);
    out_rgba[i * 4 + 1] = extract(v, g_mask);
    out_rgba[i * 4 + 2] = extract(v, b_mask);
    out_rgba[i * 4 + 3] = a_mask ? extract(v, a_mask) : 255;
  }
  return true;
}

}  // namespace

bool Dbz3DecodePackImage(const std::filesystem::path& path, std::vector<uint8_t>& rgba,
                         uint32_t& width, uint32_t& height) {
  FILE* f = rex::filesystem::OpenFile(path, "rb");
  if (!f) {
    return false;
  }
  std::vector<uint8_t> file;
  uint8_t buffer[65536];
  size_t got;
  while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
    file.insert(file.end(), buffer, buffer + got);
  }
  std::fclose(f);
  if (file.size() < 4) {
    return false;
  }
  const uint32_t magic = uint32_t(file[0]) | (uint32_t(file[1]) << 8) | (uint32_t(file[2]) << 16) |
                         (uint32_t(file[3]) << 24);
  if (magic != 0x20534444u) {  // "DDS " -> cualquier otra cosa: probar PNG.
    int png_width = 0, png_height = 0, png_channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(file.data(), int(file.size()), &png_width, &png_height,
                                            &png_channels, 4);
    if (pixels == nullptr || png_width <= 0 || png_height <= 0) {
      return false;
    }
    width = uint32_t(png_width);
    height = uint32_t(png_height);
    rgba.assign(pixels, pixels + size_t(png_width) * png_height * 4);
    stbi_image_free(pixels);
    return true;
  }
  if (file.size() < 128) {
    return false;
  }
  const uint32_t header_size = uint32_t(file[4]) | (uint32_t(file[5]) << 8) |
                               (uint32_t(file[6]) << 16) | (uint32_t(file[7]) << 24);
  if (header_size != 124) {
    return false;
  }
  auto u32 = [&](size_t off) {
    return uint32_t(file[off]) | (uint32_t(file[off + 1]) << 8) | (uint32_t(file[off + 2]) << 16) |
           (uint32_t(file[off + 3]) << 24);
  };
  height = u32(12);
  width = u32(16);
  const uint32_t pf_flags = u32(80);
  const uint32_t fourcc = u32(84);
  const uint32_t bit_count = u32(88);
  const uint32_t r_mask = u32(92);
  const uint32_t g_mask = u32(96);
  const uint32_t b_mask = u32(100);
  const uint32_t a_mask = u32(104);
  if (width == 0 || height == 0) {
    return false;
  }
  const bool compressed = (pf_flags & 0x4) != 0;  // DDPF_FOURCC
  const uint8_t* data = file.data() + 128;
  const size_t data_size = file.size() - 128;
  if (compressed) {
    return DecodeBc(data, data_size, fourcc, width, height, rgba);
  }
  if (bit_count == 32) {
    return DecodeUncompressed32(data, data_size, width, height, r_mask, g_mask, b_mask, a_mask,
                                rgba);
  }
  return false;
}

Dbz3TexturePackIndex& Dbz3TexturePackIndex::Get() {
  static Dbz3TexturePackIndex index;
  return index;
}

void Dbz3TexturePackIndex::EnsureInitialized() const {
  if (initialized_) {
    return;
  }
  initialized_ = true;
  // Leer por el REGISTRO por nombre: el launcher (exe) registra esta misma cvar
  // antes que el plugin, asi que el registro del plugin se ignora y su storage
  // local se queda por defecto. La consulta por nombre devuelve el valor real.
  const std::string list = REXCVAR_QUERY(std::string, dbz3_texture_packs);
  if (list.empty()) {
    return;
  }
  std::unordered_map<uint64_t, std::string> seen;  // hash -> pack (conflictos)
  size_t pos = 0;
  uint32_t pack_count = 0;
  while (pos <= list.size()) {
    size_t sep = list.find(';', pos);
    if (sep == std::string::npos) {
      sep = list.size();
    }
    std::string dir = list.substr(pos, sep - pos);
    pos = sep + 1;
    while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\r' || dir.back() == '\n')) {
      dir.pop_back();
    }
    if (dir.empty()) {
      if (sep == list.size()) {
        break;
      }
      continue;
    }
    std::error_code ec;
    const std::filesystem::path dir_path(dir);
    if (!std::filesystem::is_directory(dir_path, ec)) {
      REXGPU_WARN("dbz3: pack de texturas '{}' no existe", dir);
      if (sep == list.size()) {
        break;
      }
      continue;
    }
    ++pack_count;
    uint32_t found = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (ext != ".dds" && ext != ".png") {
        continue;
      }
      uint64_t hash = 0;
      uint32_t w = 0, h = 0;
      if (!ParsePackFileName(entry.path().stem().string(), hash, w, h)) {
        continue;
      }
      auto existing = seen.find(hash);
      if (existing != seen.end()) {
        REXGPU_WARN("dbz3: textura {:016X} definida en '{}' y '{}' (gana el primero)",
                    hash, existing->second, dir_path.filename().string());
        continue;
      }
      seen.emplace(hash, dir_path.filename().string());
      Dbz3TexturePackEntry e;
      e.path = entry.path();
      e.width = w;
      e.height = h;
      e.pack_name = dir_path.filename().string();
      entries_.emplace_back(hash, std::move(e));
      ++found;
    }
    REXGPU_INFO("dbz3: pack de texturas '{}' -> {} texturas", dir_path.filename().string(), found);
  }
  if (!entries_.empty()) {
    REXGPU_INFO("dbz3: {} pack(s) de texturas, {} texturas en total", pack_count, entries_.size());
  }
}

bool Dbz3TexturePackIndex::active() const {
  EnsureInitialized();
  return !entries_.empty();
}

size_t Dbz3TexturePackIndex::size() const {
  EnsureInitialized();
  return entries_.size();
}

const Dbz3TexturePackEntry* Dbz3TexturePackIndex::Find(uint64_t hash) const {
  EnsureInitialized();
  for (const auto& e : entries_) {
    if (e.first == hash) {
      return &e.second;
    }
  }
  return nullptr;
}

void Dbz3BuildPackMips(const std::vector<uint8_t>& base, uint32_t width, uint32_t height,
                       uint32_t levels, uint32_t row_pitch_alignment, std::vector<uint8_t>& out,
                       std::vector<Dbz3PackLevel>& layouts_out) {
  if (levels == 0) {
    levels = 1;
  }
  if (row_pitch_alignment == 0) {
    row_pitch_alignment = 4;
  }
  // Generar la cadena completa (nivel 0 = imagen base; siguientes por box
  // filter 2x2 con clamp en los bordes).
  std::vector<std::vector<uint8_t>> mips(levels);
  mips[0] = base;
  for (uint32_t l = 1; l < levels; ++l) {
    const uint32_t prev_width = std::max(width >> (l - 1), UINT32_C(1));
    const uint32_t prev_height = std::max(height >> (l - 1), UINT32_C(1));
    const uint32_t level_width = std::max(width >> l, UINT32_C(1));
    const uint32_t level_height = std::max(height >> l, UINT32_C(1));
    std::vector<uint8_t>& src = mips[l - 1];
    std::vector<uint8_t>& dst = mips[l];
    dst.assign(size_t(level_width) * level_height * 4, 0);
    for (uint32_t y = 0; y < level_height; ++y) {
      for (uint32_t x = 0; x < level_width; ++x) {
        const uint32_t sx = std::min(x * 2, prev_width - 1);
        const uint32_t sy = std::min(y * 2, prev_height - 1);
        const uint32_t sx1 = std::min(sx + 1, prev_width - 1);
        const uint32_t sy1 = std::min(sy + 1, prev_height - 1);
        const uint8_t* p00 = src.data() + (size_t(sy) * prev_width + sx) * 4;
        const uint8_t* p10 = src.data() + (size_t(sy) * prev_width + sx1) * 4;
        const uint8_t* p01 = src.data() + (size_t(sy1) * prev_width + sx) * 4;
        const uint8_t* p11 = src.data() + (size_t(sy1) * prev_width + sx1) * 4;
        uint8_t* dst_px = dst.data() + (size_t(y) * level_width + x) * 4;
        for (int c = 0; c < 4; ++c) {
          dst_px[c] = uint8_t((uint32_t(p00[c]) + p10[c] + p01[c] + p11[c] + 2) / 4);
        }
      }
    }
  }
  // Disponer los niveles con offsets y pitch alineados.
  layouts_out.assign(levels, Dbz3PackLevel{});
  uint64_t total = 0;
  for (uint32_t l = 0; l < levels; ++l) {
    const uint32_t level_width = std::max(width >> l, UINT32_C(1));
    const uint32_t level_height = std::max(height >> l, UINT32_C(1));
    const uint32_t pitch =
        uint32_t(rex::align(uint64_t(level_width) * 4, uint64_t(row_pitch_alignment)));
    total = rex::align(total, uint64_t(row_pitch_alignment));
    layouts_out[l].offset = uint32_t(total);
    layouts_out[l].width = level_width;
    layouts_out[l].height = level_height;
    layouts_out[l].row_pitch = pitch;
    total += uint64_t(pitch) * level_height;
  }
  out.assign(size_t(total), 0);
  for (uint32_t l = 0; l < levels; ++l) {
    const Dbz3PackLevel& layout = layouts_out[l];
    for (uint32_t y = 0; y < layout.height; ++y) {
      std::memcpy(out.data() + layout.offset + size_t(y) * layout.row_pitch,
                  mips[l].data() + size_t(y) * layout.width * 4, size_t(layout.width) * 4);
    }
  }
}

}  // namespace rex::graphics
