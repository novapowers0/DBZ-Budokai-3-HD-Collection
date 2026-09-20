// DBZ3 HD Collection: indice y decodificacion de "packs" de texturas.
// Ver dbz3_texture_pack.h para el formato.

#include "dbz3_texture_pack.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

// Decodificacion PNG (stb_image, solo PNG). El plugin no puede usar el helper
// de rexui (visibilidad oculta entre DLLs), asi que compila su propia copia.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>

namespace rex::graphics::d3d12 {

// Lista de carpetas de pack activas, separadas por ';'. El launcher la escribe
// en el TOML (mismo nombre de cvar) tras detectar los packs en `mods/`.
REXCVAR_DEFINE_STRING(dbz3_texture_packs, "", "GPU",
                      "DBZ3: carpetas de packs de texturas activos, separadas por ';' "
                      "(vacio = desactivado)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

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

}  // namespace rex::graphics::d3d12
