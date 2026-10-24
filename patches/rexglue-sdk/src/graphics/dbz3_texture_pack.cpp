// DBZ3 HD Collection: indice y decodificacion de "packs" de texturas.
// Ver dbz3_texture_pack.h para el formato.

#include "dbz3_texture_pack.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

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

std::atomic<uint64_t> g_dbz3_pack_uploads{0};
std::atomic<uint64_t> g_dbz3_pack_hash_us{0};
std::atomic<uint64_t> g_dbz3_pack_hash_n{0};

bool Dbz3PackSwapsRedBlue(xenos::TextureFormat format) {
  return format == xenos::TextureFormat::k_8_8_8_8 ||
         format == xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16;
}

const Dbz3TexturePackEntry* Dbz3PackRevalidate(const Dbz3TexturePackEntry* cached, uint64_t hash) {
  return Dbz3PackRevalidate(cached, hash, Dbz3TexturePacksOn(), Dbz3SideBordersHidden());
}

const Dbz3TexturePackEntry* Dbz3PackRevalidate(const Dbz3TexturePackEntry* cached, uint64_t hash,
                                               bool packs_on, bool side_borders_hidden) {
  if (cached == nullptr || cached->hash == hash) {
    return cached;
  }
  const Dbz3TexturePackEntry* now = Dbz3HiddenEntry(hash, side_borders_hidden);
  if (now == nullptr && packs_on) {
    now = Dbz3TexturePackIndex::Get().Find(hash);
  }
  if (now != nullptr && now->width == cached->width && now->height == cached->height) {
    return now;
  }
  return nullptr;
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

// Hash de un fichero de pack: los 16 primeros caracteres hex del nombre (el
// resto es libre: "<hash>_<W>x<H>_<fmt>" del volcado, con el tamano ORIGINAL o el
// de la imagen, da igual). El tamano real se lee de la cabecera del fichero.
bool ParsePackHash(const std::string& stem, uint64_t& out_hash) {
  if (stem.size() < 16 || (stem.size() > 16 && stem[16] != '_' && stem[16] != '.')) {
    return false;
  }
  uint64_t hash = 0;
  for (int i = 0; i < 16; ++i) {
    const char c = stem[i];
    if (!IsHex(c)) {
      return false;
    }
    uint32_t nibble = (c >= '0' && c <= '9')   ? uint32_t(c - '0')
                      : (c >= 'a' && c <= 'f') ? uint32_t(c - 'a' + 10)
                                               : uint32_t(c - 'A' + 10);
    hash = (hash << 4) | nibble;
  }
  out_hash = hash;
  return true;
}

// Ancho/alto de un PNG (IHDR) o DDS (cabecera) leyendo solo los primeros bytes.
bool ReadImageSize(const std::filesystem::path& path, uint32_t& width, uint32_t& height) {
  FILE* f = rex::filesystem::OpenFile(path, "rb");
  if (!f) {
    return false;
  }
  uint8_t h[24] = {};
  const size_t got = std::fread(h, 1, sizeof(h), f);
  std::fclose(f);
  if (got < 24) {
    return false;
  }
  if (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') {
    width = (uint32_t(h[16]) << 24) | (uint32_t(h[17]) << 16) | (uint32_t(h[18]) << 8) | h[19];
    height = (uint32_t(h[20]) << 24) | (uint32_t(h[21]) << 16) | (uint32_t(h[22]) << 8) | h[23];
  } else if (h[0] == 'D' && h[1] == 'D' && h[2] == 'S' && h[3] == ' ') {
    height = uint32_t(h[12]) | (uint32_t(h[13]) << 8) | (uint32_t(h[14]) << 16) |
             (uint32_t(h[15]) << 24);
    width = uint32_t(h[16]) | (uint32_t(h[17]) << 8) | (uint32_t(h[18]) << 16) |
            (uint32_t(h[19]) << 24);
  } else {
    return false;
  }
  return width != 0 && height != 0 && width <= 8192 && height <= 8192;
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
  size_t data_size = file.size() - 128;
  if (compressed && fourcc == 0x30315844u) {  // 'DX10': cabecera DDS_HEADER_DXT10 de 20 B
    if (file.size() < 148) {
      return false;
    }
    const uint32_t dxgi = u32(128);
    data += 20;
    data_size -= 20;
    // Formatos del header DX10 que no llevan compresion (los escribe DirectXTex al
    // guardar RGBA8). Se decodifican como 32 bits con las mascaras de su orden.
    switch (dxgi) {
      case 28:  // R8G8B8A8_UNORM
      case 29:  // R8G8B8A8_UNORM_SRGB
        return DecodeUncompressed32(data, data_size, width, height, 0x000000FFu, 0x0000FF00u,
                                    0x00FF0000u, 0xFF000000u, rgba);
      case 87:  // B8G8R8A8_UNORM
      case 91:  // B8G8R8A8_UNORM_SRGB
      case 88:  // B8G8R8X8_UNORM
        return DecodeUncompressed32(data, data_size, width, height, 0x00FF0000u, 0x0000FF00u,
                                    0x000000FFu, dxgi == 88 ? 0u : 0xFF000000u, rgba);
      default: {
        static uint32_t warned = 0;
        if (warned != dxgi) {
          warned = dxgi;
          REXGPU_WARN(
              "dbz3: pack: DDS DX10 con formato DXGI {} no soportado (BC7 = 98); exporta esa "
              "textura como PNG (sin perdida y ya soportado)",
              dxgi);
        }
        return false;
      }
    }
  }
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

namespace {
// Texto UTF-8 de una ruta: path::string() lanza excepcion con nombres que no caben
// en la pagina de codigos de Windows (acentos raros, japones...).
std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

// Ruta con prefijo \?\ en Windows: un pack HD dentro de una carpeta del juego ya
// larga pasa de 260 caracteres en sus subcarpetas y el recorrido fallaba con
// "The system cannot find the path specified" (log de un usuario, 2026-10-09).
std::filesystem::path LongPath(const std::filesystem::path& p) {
#ifdef _WIN32
  std::error_code ec;
  std::filesystem::path full = std::filesystem::weakly_canonical(p, ec);
  if (ec) {
    full = std::filesystem::absolute(p, ec);
  }
  std::wstring w = full.make_preferred().wstring();
  if (ec || w.rfind(L"\\\\", 0) == 0) {
    return p;  // ya lleva prefijo o es UNC
  }
  // "\\?\" (dos barras): con una sola ("\?\D:\...") Windows la toma como ruta relativa a la
  // raiz de la unidad actual y ningun pack se abria ("no existe", 2026-10-13).
  return std::filesystem::path(L"\\\\?\\" + w);
#else
  return p;
#endif
}

// Cabeceras ya leidas en arranques anteriores: ruta -> {tamano, fecha, ancho, alto}. Abrir
// 12000 PNG con el antivirus revisando cada uno tardaba ~60 s tras reiniciar el PC (y hasta
// entonces todo eran texturas originales); listar la carpeta da tamano y fecha sin abrirlos.
constexpr const char* kIndexFile = "shader_cache/dbz3_pack_indice.txt";
using HeaderCache = std::unordered_map<std::string, std::array<uint64_t, 4>>;

HeaderCache LoadHeaderCache() {
  HeaderCache cache;
  if (FILE* f = std::fopen(kIndexFile, "rb")) {
    char line[2048];
    while (std::fgets(line, sizeof(line), f)) {
      unsigned long long size, time, width, height;
      int used = 0;
      if (std::sscanf(line, "%llu\t%llu\t%llu\t%llu\t%n", &size, &time, &width, &height, &used) == 4 &&
          used > 0) {
        std::string path(line + used);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
          path.pop_back();
        }
        cache[path] = {size, time, width, height};
      }
    }
    std::fclose(f);
  }
  return cache;
}

void SaveHeaderCache(const HeaderCache& cache) {
  std::error_code ec;
  std::filesystem::create_directories("shader_cache", ec);
  static std::atomic<uint32_t> index_tmp_counter{0};
#ifdef _WIN32
  const unsigned long index_pid = GetCurrentProcessId();
#else
  const unsigned long index_pid = static_cast<unsigned long>(getpid());
#endif
  const std::string tmp = std::string(kIndexFile) + "." + std::to_string(index_pid) + "." +
                          std::to_string(index_tmp_counter.fetch_add(1)) + ".tmp";
  if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
    for (const auto& [path, v] : cache) {
      std::fprintf(f, "%llu\t%llu\t%llu\t%llu\t%s\n", static_cast<unsigned long long>(v[0]),
                   static_cast<unsigned long long>(v[1]), static_cast<unsigned long long>(v[2]),
                   static_cast<unsigned long long>(v[3]), path.c_str());
    }
    std::fclose(f);
    std::filesystem::rename(tmp, kIndexFile, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
    }
  }
}

// Lee un pack (carpeta, UTF-8): sus texturas <hash>_<W>x<H>_*.png/.dds, en todas las
// subcarpetas. Nunca lanza excepcion: una subcarpeta ilegible corta el recorrido con
// un aviso y se queda con lo leido. `misses` cuenta las cabeceras leidas del disco.
bool ScanPack(const std::string& dir, std::string& name,
              std::vector<Dbz3TexturePackEntry>& files, const HeaderCache& old_headers,
              HeaderCache& new_headers, size_t& misses) {
  const std::filesystem::path dir_path = rex::to_path(dir);
  name = Utf8(dir_path.filename());
  std::error_code ec;
  const std::filesystem::path root = LongPath(dir_path);
  if (!std::filesystem::is_directory(root, ec)) {
    REXGPU_WARN("dbz3: pack de texturas '{}' no existe", dir);
    return false;
  }
  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    std::error_code fec;
    // <pack>/update/ = originales que FALTAN en el pack, volcados por el juego para el autor
    // (RecordMissingPackTexture). No son reemplazos: indexarlos los cargaba como x1 en la
    // siguiente sesion (videos 1280x720 resubidos cada frame, device removed INVALID_CALL).
    if (it.depth() == 0 && entry.is_directory(fec) && Utf8(entry.path().filename()) == "update") {
      it.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file(fec)) {
      continue;
    }
    std::string ext = Utf8(entry.path().extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".dds" && ext != ".png") {
      continue;
    }
    Dbz3TexturePackEntry e;
    if (!ParsePackHash(Utf8(entry.path().stem()), e.hash)) {
      continue;
    }
    const std::string key = Utf8(entry.path());
    const uint64_t size = entry.file_size(fec);
    const uint64_t time = uint64_t(entry.last_write_time(fec).time_since_epoch().count());
    auto known = old_headers.find(key);
    if (known != old_headers.end() && known->second[0] == size && known->second[1] == time &&
        known->second[2] - 1 < 8192 && known->second[3] - 1 < 8192) {
      e.width = uint32_t(known->second[2]);
      e.height = uint32_t(known->second[3]);
    } else {
      ++misses;
      if (!ReadImageSize(entry.path(), e.width, e.height)) {
        continue;
      }
    }
    new_headers[key] = {size, time, e.width, e.height};
    e.path = entry.path();
    e.pack_name = name;
    files.push_back(std::move(e));
  }
  if (ec) {
    REXGPU_WARN("dbz3: pack de texturas '{}': recorrido cortado ({}), {} texturas leidas", name,
                ec.message(), files.size());
  }
  // D3: alias de nombres. `alias.txt` en la raiz del pack, una linea por textura:
  //   <hash de 16 hex> = <ruta relativa a la raiz del pack>
  // Permite nombrar los ficheros con sentido (goku_ssj4_cara.png) sin renombrarlos al hash.
  {
    const std::filesystem::path alias_path = root / "alias.txt";
    std::error_code aec;
    if (std::filesystem::is_regular_file(alias_path, aec)) {
      FILE* af = rex::filesystem::OpenFile(alias_path, "rb");
      if (af) {
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), af)) > 0) {
          text.append(buf, n);
        }
        std::fclose(af);
        size_t pos = 0, aliases = 0;
        while (pos < text.size()) {
          size_t end = text.find('\n', pos);
          if (end == std::string::npos) {
            end = text.size();
          }
          std::string line = text.substr(pos, end - pos);
          pos = end + 1;
          if (!line.empty() && line.back() == '\r') {
            line.pop_back();
          }
          const size_t eq = line.find('=');
          if (eq == std::string::npos) {
            continue;
          }
          std::string hash_text = line.substr(0, eq), rel = line.substr(eq + 1);
          auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
          };
          trim(hash_text);
          trim(rel);
          if (hash_text.size() != 16 || rel.empty() || rel[0] == '#') {
            continue;
          }
          Dbz3TexturePackEntry e;
          if (!ParsePackHash(hash_text + "_", e.hash)) {
            continue;
          }
          const std::filesystem::path file = root / rex::to_path(rel);
          std::error_code fec;
          if (!std::filesystem::is_regular_file(file, fec)) {
            REXGPU_WARN("dbz3: pack '{}': alias '{}' apunta a un fichero que no existe", name, rel);
            continue;
          }
          if (!ReadImageSize(file, e.width, e.height)) {
            continue;
          }
          e.path = file;
          e.pack_name = name;
          files.push_back(std::move(e));
          ++aliases;
        }
        if (aliases != 0) {
          REXGPU_INFO("dbz3: pack de texturas '{}': {} alias de alias.txt", name, aliases);
        }
      }
    }
  }
  return true;
}
}  // namespace

void Dbz3ClearDecodeCache();   // mas abajo: imagenes ya decodificadas (clave = hash, no ruta)

void Dbz3TexturePackIndex::EnsureInitialized() const {
  // Se llama en cada busqueda: la lista de packs se mira como mucho 2 veces por segundo.
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  int64_t next = next_check_.load(std::memory_order_relaxed);
  if (now < next || building_.load(std::memory_order_acquire)) {
    return;
  }
  std::unique_lock<std::mutex> lock(check_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }
  next_check_.store(
      now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(500)).count(),
      std::memory_order_relaxed);
  // Leer por el REGISTRO por nombre: el launcher (exe) registra esta misma cvar
  // antes que el plugin, asi que el registro del plugin se ignora y su storage
  // local se queda por defecto. La consulta por nombre devuelve el valor real.
  const std::string list = REXCVAR_QUERY(std::string, dbz3_texture_packs);
  if (started_ && list == built_list_) {
    return;
  }
  // La cache de texturas se crea al abrir el launcher: con el pack instalado desde Nova's
  // Mods en esa misma sesion la lista estaba vacia entonces y el pack no se veia hasta
  // reiniciar (reporte del usuario, 1.4.5). Ahora se rehace: active() pasa a false y las
  // caches rehacen lo cargado cuando vuelve a true.
  if (started_) {
    REXGPU_INFO("dbz3: la lista de packs ha cambiado; se rehace el indice");
  }
  // Lista cambiada con el juego en marcha (p. ej. un zip "update" soltado tras el pack grande): lo ya
  // decodificado esta indexado por hash y seguiria devolviendo la imagen del pack anterior.
  if (started_) Dbz3ClearDecodeCache();
  started_ = true;
  built_list_ = list;
  ready_.store(false, std::memory_order_release);
  if (list.empty()) {
    map_.store(nullptr, std::memory_order_release);
    ready_.store(true, std::memory_order_release);
    return;
  }
  building_.store(true, std::memory_order_release);
  // El primer recorrido de un pack recien instalado lee la cabecera de miles de ficheros
  // (con el antivirus revisando cada uno): 12345 PNG tardaron ~60 s y, en el hilo de la
  // GPU, la ventana salia como "no responde" (2026-10-13). Se indexa en segundo plano.
  // ponytail: hilo suelto como la precarga; salir del juego a mitad del indexado no espera.
  std::thread([this, list] {
    const auto t0 = std::chrono::steady_clock::now();
    Map* entries = new Map();  // nunca se libera (ver dbz3_texture_pack.h)
    Build(list, *entries);
    map_.store(entries, std::memory_order_release);
    ready_.store(true, std::memory_order_release);
    building_.store(false, std::memory_order_release);
    if (!entries->empty()) {
      REXGPU_INFO("dbz3: indice de packs listo en {} ms (hasta ahora, texturas originales)",
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count());
      StartPreload(*entries);
    }
  }).detach();
}

const Dbz3TexturePackIndex::Map* Dbz3TexturePackIndex::Entries() const {
  EnsureInitialized();
  if (!ready_.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return map_.load(std::memory_order_acquire);
}

void Dbz3TexturePackIndex::Build(const std::string& list, Map& entries_) {
  // Primero se lee cada pack; luego se indexan del MAS PEQUENO al mas grande: si dos
  // packs traen la misma textura gana el pequeno (un pack de idioma o de retoques va
  // encima del pack HD completo, sin depender del orden alfabetico de las carpetas).
  struct Pack {
    std::string name;
    std::vector<Dbz3TexturePackEntry> files;
  };
  std::vector<Pack> packs;
  const HeaderCache old_headers = LoadHeaderCache();
  HeaderCache new_headers;
  size_t misses = 0;
  size_t pos = 0;
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
    if (!dir.empty()) {
      Pack pack;
      if (ScanPack(dir, pack.name, pack.files, old_headers, new_headers, misses)) {
        packs.push_back(std::move(pack));
      }
    }
    if (sep == list.size()) {
      break;
    }
  }
  if (misses != 0 || new_headers.size() != old_headers.size()) {
    SaveHeaderCache(new_headers);
  }
  REXGPU_INFO("dbz3: packs: {} cabeceras ya conocidas, {} leidas del disco",
              new_headers.size() - std::min(new_headers.size(), misses), misses);
  std::stable_sort(packs.begin(), packs.end(),
                   [](const Pack& x, const Pack& y) { return x.files.size() < y.files.size(); });
  std::unordered_map<uint64_t, std::string> seen;  // hash -> pack (conflictos)
  uint32_t pack_count = 0;
  for (Pack& pack : packs) {
    ++pack_count;
    uint32_t found = 0, shadowed = 0;
    for (Dbz3TexturePackEntry& e : pack.files) {
      if (!seen.emplace(e.hash, pack.name).second) {
        ++shadowed;
        continue;
      }
      entries_.emplace(e.hash, std::move(e));
      ++found;
    }
    REXGPU_INFO("dbz3: pack de texturas '{}' -> {} texturas", pack.name, found);
    if (shadowed != 0) {
      REXGPU_INFO("dbz3: pack '{}': {} texturas repetidas (ya las trae un pack mas pequeno o "
                  "el mismo pack en otra carpeta; gana la primera)", pack.name, shadowed);
    }
  }
  if (!entries_.empty()) {
    REXGPU_INFO("dbz3: {} pack(s) de texturas, {} texturas en total", pack_count, entries_.size());
  }
}

bool Dbz3TexturePacksOn() { return rex::cvar::GetFlagByName("dbz3_texture_packs_on") != "false"; }

bool Dbz3TexturePackIndex::active() const {
  const Map* entries = Entries();
  return entries != nullptr && !entries->empty();
}

size_t Dbz3TexturePackIndex::size() const {
  const Map* entries = Entries();
  return entries != nullptr ? entries->size() : 0;
}

const Dbz3TexturePackEntry* Dbz3TexturePackIndex::Find(uint64_t hash) const {
  const Map* entries = Entries();
  if (entries == nullptr) {
    return nullptr;
  }
  auto it = entries->find(hash);
  return it != entries->end() ? &it->second : nullptr;
}

// --- cache de imagenes decodificadas + hilos de fondo (como Burst Limit 4.2) ------
// El hilo de la GPU no decodifica (D3D12): pide la imagen con TryDecoded, muestra la
// original mientras tanto y la recarga cuando un hilo de fondo la deja lista, ya con su
// cadena de mips. Lo decodificado queda en RAM (LRU con presupuesto segun la RAM LIBRE).
// Precarga: el pack entero si cabe; si no, las texturas usadas en partidas anteriores
// (shader_cache/dbz3_pack_uso.txt: menus, escenarios y personajes que mas se juegan).
namespace {

struct Ram {
  uint64_t total = 0;
  uint64_t avail = 0;
};

Ram QueryRam() {
  Ram r;
#ifdef _WIN32
  MEMORYSTATUSEX status = {};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    r.total = status.ullTotalPhys;
    r.avail = status.ullAvailPhys;
  }
#else
  const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
  const long avail = sysconf(_SC_AVPHYS_PAGES);
  if (pages > 0 && page > 0) {
    r.total = uint64_t(pages) * uint64_t(page);
    r.avail = avail > 0 ? uint64_t(avail) * uint64_t(page) : r.total / 2;
  }
#endif
  if (r.total == 0) {
    r.total = uint64_t(8) << 30;
    r.avail = r.total / 2;
  }
  return r;
}

// Un cuarto de la RAM, pero nunca mas de la mitad de la que esta LIBRE al arrancar (entre
// 512 MB y 8 GB). Con 32 GB y el navegador abierto quedaban ~4 GB libres: llenar 8 GB hacia
// que Windows mandara el juego al disco y se quedaba congelado 45 s (2026-10-14).
uint64_t CacheBudget() {
  static const uint64_t budget = [] {
    const Ram r = QueryRam();
    return std::clamp<uint64_t>(std::min(r.total / 4, r.avail / 2), uint64_t(512) << 20, uint64_t(8) << 30);
  }();
  return budget;
}

// Poca RAM libre en el sistema: la precarga se para y la cache suelta lo mas viejo.
bool RamLow() {
  const Ram r = QueryRam();
  return r.avail < std::max<uint64_t>(uint64_t(1536) << 20, r.total / 12);
}

// Tamano en RAM de una entrada ya decodificada con todos sus mips (4/3 de la base).
uint64_t DecodedBytes(const Dbz3TexturePackEntry& e) {
  return uint64_t(e.width) * e.height * 4 * 4 / 3;
}

struct PackCache {
  std::mutex mutex;
  std::unordered_map<uint64_t, std::pair<std::shared_ptr<const Dbz3TexturePackIndex::Image>,
                                         std::list<uint64_t>::iterator>>
      images;
  std::list<uint64_t> lru;  // frente = mas reciente
  uint64_t bytes = 0;
  std::unordered_set<uint64_t> failed;  // ilegibles: no se reintentan (se usa la original)
};

PackCache& Cache() {
  static PackCache* cache = new PackCache();  // nunca se destruye: los hilos pueden seguir al salir
  return *cache;
}

std::shared_ptr<const Dbz3TexturePackIndex::Image> CacheGet(uint64_t hash) {
  PackCache& c = Cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  auto it = c.images.find(hash);
  if (it == c.images.end()) {
    return nullptr;
  }
  c.lru.splice(c.lru.begin(), c.lru, it->second.second);
  return it->second.first;
}

bool CacheHas(uint64_t hash) {
  PackCache& c = Cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  return c.images.count(hash) != 0;
}

bool CacheFailed(uint64_t hash) {
  PackCache& c = Cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  return c.failed.count(hash) != 0;
}

}  // namespace

void Dbz3ClearDecodeCache() {
  std::vector<std::shared_ptr<const Dbz3TexturePackIndex::Image>> victims;   // se sueltan fuera del candado
  {
    PackCache& c = Cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    for (auto& kv : c.images) victims.push_back(std::move(kv.second.first));
    c.images.clear();
    c.lru.clear();
    c.failed.clear();
    c.bytes = 0;
  }
}

namespace {

void CachePut(uint64_t hash, std::shared_ptr<const Dbz3TexturePackIndex::Image> image) {
  const bool low = RamLow();
  // Las expulsadas se liberan FUERA del candado: soltar cientos de MB tarda y el hilo de la
  // GPU espera ese mismo candado en IsDecoded/TryDecoded.
  std::vector<std::shared_ptr<const Dbz3TexturePackIndex::Image>> victims;
  {
    PackCache& c = Cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    if (c.images.count(hash)) {
      return;
    }
    c.lru.push_front(hash);
    c.bytes += image->rgba.size();
    c.images.emplace(hash, std::make_pair(std::move(image), c.lru.begin()));
    // Sin RAM libre se baja a la mitad del presupuesto (el resto ya lo pediran de nuevo).
    const uint64_t limit = low ? CacheBudget() / 2 : CacheBudget();
    while (c.bytes > limit && c.lru.size() > 1) {
      auto victim = c.images.find(c.lru.back());
      c.bytes -= victim->second.first->rgba.size();
      victims.push_back(std::move(victim->second.first));
      c.images.erase(victim);
      c.lru.pop_back();
    }
  }
}

void MarkFailed(uint64_t hash) {
  PackCache& c = Cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  c.failed.insert(hash);
}

// Cadena de mips completa hasta 1x1, escrita ya en la disposicion de la GPU (filas y niveles
// alineados a 512 B) para que la subida sea una sola copia. Media 2x2 ponderada por alfa
// (como Burst Limit): el color de los texels transparentes no oscurece los bordes del pelo,
// las auras y los recortes al alejarse. `base` es la imagen decodificada (width*height*4).
void BuildMipChain(Dbz3TexturePackIndex::Image& image, const std::vector<uint8_t>& base) {
  constexpr uint64_t kAlign = 512;
  uint32_t levels = 0;
  for (uint32_t w = image.width, h = image.height;; w = std::max(w >> 1, 1u), h = std::max(h >> 1, 1u)) {
    ++levels;
    if (w == 1 && h == 1) {
      break;
    }
  }
  image.layout.assign(levels, Dbz3PackLevel{});
  uint64_t total = 0;
  for (uint32_t l = 0; l < levels; ++l) {
    Dbz3PackLevel& level = image.layout[l];
    level.width = std::max(image.width >> l, 1u);
    level.height = std::max(image.height >> l, 1u);
    level.row_pitch = uint32_t(rex::align(uint64_t(level.width) * 4, kAlign));
    total = rex::align(total, kAlign);
    level.offset = uint32_t(total);
    total += uint64_t(level.row_pitch) * level.height;
  }
  image.rgba.assign(size_t(total), 0);
  const Dbz3PackLevel& top = image.layout[0];
  for (uint32_t y = 0; y < top.height; ++y) {
    std::memcpy(image.rgba.data() + top.offset + size_t(y) * top.row_pitch,
                base.data() + size_t(y) * top.width * 4, size_t(top.width) * 4);
  }
  for (uint32_t l = 1; l < levels; ++l) {
    const Dbz3PackLevel& prev = image.layout[l - 1];
    const Dbz3PackLevel& cur = image.layout[l];
    const uint8_t* src = image.rgba.data() + prev.offset;
    uint8_t* dst = image.rgba.data() + cur.offset;
    const uint32_t pw = prev.width, ph = prev.height;
    for (uint32_t y = 0; y < cur.height; ++y) {
      const uint32_t sy = std::min(y * 2, ph - 1), sy1 = std::min(sy + 1, ph - 1);
      const uint8_t* row0 = src + size_t(sy) * prev.row_pitch;
      const uint8_t* row1 = src + size_t(sy1) * prev.row_pitch;
      uint8_t* out = dst + size_t(y) * cur.row_pitch;
      for (uint32_t x = 0; x < cur.width; ++x) {
        const uint32_t sx = std::min(x * 2, pw - 1), sx1 = std::min(sx + 1, pw - 1);
        const uint8_t* p[4] = {row0 + sx * 4, row0 + sx1 * 4, row1 + sx * 4, row1 + sx1 * 4};
        const uint32_t alpha = uint32_t(p[0][3]) + p[1][3] + p[2][3] + p[3][3];
        uint8_t* d = out + x * 4;
        for (int c = 0; c < 3; ++c) {
          d[c] = alpha != 0 ? uint8_t((uint32_t(p[0][c]) * p[0][3] + uint32_t(p[1][c]) * p[1][3] +
                                       uint32_t(p[2][c]) * p[2][3] + uint32_t(p[3][c]) * p[3][3] +
                                       alpha / 2) / alpha)
                            : uint8_t((uint32_t(p[0][c]) + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
        }
        d[3] = uint8_t((alpha + 2) / 4);
      }
    }
  }
}

// Las imagenes grandes (>= 64 MB con mips, las 4096x4096 y 8192x8192 del pack) se preparan
// de una en una: cuatro a la vez eran ~4 GB de RAM de golpe.
std::mutex& BigDecodeMutex() {
  static std::mutex* m = new std::mutex();
  return *m;
}

std::shared_ptr<const Dbz3TexturePackIndex::Image> DecodeEntry(const Dbz3TexturePackEntry& e) {
  std::unique_lock<std::mutex> big(BigDecodeMutex(), std::defer_lock);
  if (DecodedBytes(e) >= (uint64_t(64) << 20)) {
    big.lock();
  }
  auto image = std::make_shared<Dbz3TexturePackIndex::Image>();
  bool ok = false;
  try {  // sin esto, un bad_alloc (imagen enorme, poca RAM) en un hilo de fondo cerraba el juego
    std::vector<uint8_t> base;
    ok = Dbz3DecodePackImage(e.path, base, image->width, image->height) &&
         image->width == e.width && image->height == e.height;
    if (ok) {
      BuildMipChain(*image, base);
    }
  } catch (const std::bad_alloc&) {
    // sin memoria AHORA: no se marca como rota (se volvera a pedir cuando haga falta)
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 4) {
      REXGPU_WARN("dbz3: pack '{}': sin memoria para '{}' (se reintentara)", e.pack_name, Utf8(e.path.filename()));
    }
    return nullptr;
  } catch (const std::exception&) {
    ok = false;
  }
  if (!ok) {
    MarkFailed(e.hash);
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 8) {
      REXGPU_WARN("dbz3: pack '{}': no se pudo leer '{}' (se usa la textura original)", e.pack_name,
                  Utf8(e.path.filename()));
    }
    return nullptr;
  }
  return image;
}

// Hilos de fondo: primero lo que el juego necesita ya (urgent), despues la precarga.
struct Decoder {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<const Dbz3TexturePackEntry*> urgent;
  std::deque<const Dbz3TexturePackEntry*> preload;
  std::unordered_set<uint64_t> pending;      // en cola o decodificandose
  std::unordered_set<uint64_t> in_urgent;    // ya en la cola urgente (sin repetir)
  std::unordered_set<uint64_t> decoding;     // en manos de un hilo ahora mismo
  bool started = false;
  bool preload_logged = false;
  uint64_t preload_done = 0, preload_skipped = 0;
};

Decoder& Dec() {
  static Decoder* decoder = new Decoder();
  return *decoder;
}

void DecoderLoop() {
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);  // el juego primero
#endif
  Decoder& d = Dec();
  for (;;) {
    const Dbz3TexturePackEntry* e;
    bool from_preload;
    {
      std::unique_lock<std::mutex> lock(d.mutex);
      d.cv.wait(lock, [&d] { return !d.urgent.empty() || !d.preload.empty(); });
      from_preload = d.urgent.empty();
      std::deque<const Dbz3TexturePackEntry*>& q = from_preload ? d.preload : d.urgent;
      e = q.front();
      q.pop_front();
      if (!from_preload) {
        d.in_urgent.erase(e->hash);
      }
      if (!d.decoding.insert(e->hash).second) {
        continue;   // otro hilo ya la esta preparando (pedida otra vez como urgente)
      }
    }
    bool skipped = false;
    if (!CacheHas(e->hash) && !CacheFailed(e->hash)) {
      if (from_preload && RamLow()) {
        skipped = true;   // la precarga nunca empuja al sistema a usar el disco
      } else if (auto image = DecodeEntry(*e)) {
        CachePut(e->hash, std::move(image));
      }
    }
    std::lock_guard<std::mutex> lock(d.mutex);
    d.decoding.erase(e->hash);
    if (!d.in_urgent.count(e->hash)) {
      d.pending.erase(e->hash);
    }
    if (from_preload) {
      ++(skipped ? d.preload_skipped : d.preload_done);
      if (d.preload.empty() && !d.preload_logged) {
        d.preload_logged = true;
        REXGPU_INFO("dbz3: precarga terminada: {} texturas{}", d.preload_done,
                    d.preload_skipped ? " (" + std::to_string(d.preload_skipped) +
                                            " saltadas: poca RAM libre)" : std::string());
      }
    }
  }
}

// Las entradas no cambian tras el indice (se hace una sola vez): punteros estables.
void Enqueue(const Dbz3TexturePackEntry& e, bool urgent) {
  Decoder& d = Dec();
  {
    std::lock_guard<std::mutex> lock(d.mutex);
    if (!d.started) {
      d.started = true;
      const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
      for (unsigned t = 0; t < threads; ++t) {
        std::thread(DecoderLoop).detach();
      }
    }
    const bool fresh = d.pending.insert(e.hash).second;
    if (urgent) {
      // tambien si ya esta en la precarga: el hilo que llegue primero la hace, el otro la salta
      if (!d.in_urgent.insert(e.hash).second) {
        return;
      }
      d.urgent.push_back(&e);
    } else if (fresh) {
      d.preload.push_back(&e);
    } else {
      return;
    }
  }
  d.cv.notify_one();
}

// --- texturas usadas en partidas anteriores (precarga aprendida) ---
constexpr const char* kUsageFile = "shader_cache/dbz3_pack_uso.txt";
constexpr size_t kUsageMax = 6000;

struct Usage {
  std::mutex mutex;
  std::vector<uint64_t> session;  // usadas en esta partida, en orden
  std::unordered_set<uint64_t> seen;
  std::vector<uint64_t> previous;  // las del fichero al arrancar
  std::atomic<bool> saving{false};
};

Usage& Use() {
  static Usage* usage = new Usage();
  return *usage;
}

// Nombre temporal propio (dos juegos o dos guardados a la vez no se pisan el .tmp).
std::string UniqueTmp(const char* path) {
  static std::atomic<uint32_t> counter{0};
#ifdef _WIN32
  const unsigned long pid = GetCurrentProcessId();
#else
  const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
  return std::string(path) + "." + std::to_string(pid) + "." + std::to_string(counter.fetch_add(1)) + ".tmp";
}

std::vector<uint64_t> LoadUsage() {
  std::vector<uint64_t> out;
  if (FILE* f = std::fopen(kUsageFile, "rb")) {
    char line[64];
    while (out.size() < kUsageMax && std::fgets(line, sizeof(line), f)) {
      std::string text(line);
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
      }
      uint64_t hash;
      if (ParsePackHash(text, hash)) {
        out.push_back(hash);
      }
    }
    std::fclose(f);
  }
  return out;
}

// Las de esta partida primero y despues las anteriores que no se repiten.
void SaveUsage(std::vector<uint64_t> session, const std::vector<uint64_t>& previous) {
  std::unordered_set<uint64_t> in(session.begin(), session.end());
  for (uint64_t hash : previous) {
    if (session.size() >= kUsageMax) {
      break;
    }
    if (in.insert(hash).second) {
      session.push_back(hash);
    }
  }
  std::error_code ec;
  std::filesystem::create_directories("shader_cache", ec);
  const std::string tmp = UniqueTmp(kUsageFile);
  if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
    for (uint64_t hash : session) {
      std::fprintf(f, "%016llX\n", static_cast<unsigned long long>(hash));
    }
    std::fclose(f);
    std::filesystem::rename(tmp, kUsageFile, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);   // otro juego lo acaba de escribir: vale el suyo
    }
  }
  Use().saving = false;
}
}  // namespace

void Dbz3PackNoteUsed(uint64_t hash) {
  Usage& u = Use();
  std::vector<uint64_t> session, previous;
  {
    std::lock_guard<std::mutex> lock(u.mutex);
    if (!u.seen.insert(hash).second) {
      return;
    }
    u.session.push_back(hash);
    if (u.session.size() % 32 != 0) {
      return;
    }
    session = u.session;
    previous = u.previous;
  }
  // ponytail: se guarda cada 32 texturas nuevas (sin gancho de cierre en el plugin); lo de
  // las ultimas <32 de una partida se pierde, da igual para una precarga.
  if (u.saving.exchange(true)) {
    return;   // ya hay uno escribiendo: el siguiente lote de 32 lo recoge
  }
  std::thread(SaveUsage, std::move(session), std::move(previous)).detach();  // sin E/S en la GPU
}

namespace {
// Tiras de los menus que se ocultan (XXH3 del nivel base, RGBA8, volcado de la copia del jugador):
//  - "borde" de data_usi (128x1024): tira interior, dentro del 4:3;
//  - greca dorada exterior del marco 16:9 (64x1024), sobre el relleno amarillo (10BD011B92384BD7, se queda).
struct HiddenTexture {
  Dbz3TexturePackEntry entry;
  std::shared_ptr<const Dbz3TexturePackIndex::Image> image;  // transparente (alfa 0) con mips
};
std::array<HiddenTexture, 2>& Hidden() {
  static std::array<HiddenTexture, 2> hidden = [] {
    std::array<HiddenTexture, 2> h{{{{0x4B735AD364D513BAull, {}, 128, 1024, 1, "bordes dorados apagados"}, {}},
                                    {{0xB21DA43CB61A3DCDull, {}, 64, 1024, 1, "bordes dorados apagados"}, {}}}};
    for (HiddenTexture& t : h) {
      auto im = std::make_shared<Dbz3TexturePackIndex::Image>();
      im->width = t.entry.width;
      im->height = t.entry.height;
      BuildMipChain(*im, std::vector<uint8_t>(size_t(im->width) * im->height * 4, 0));
      t.image = std::move(im);
    }
    return h;
  }();
  return hidden;
}
std::shared_ptr<const Dbz3TexturePackIndex::Image> HiddenImage(const Dbz3TexturePackEntry& entry) {
  for (const HiddenTexture& t : Hidden()) {
    if (&entry == &t.entry) {
      return t.image;
    }
  }
  return nullptr;
}
}  // namespace

bool Dbz3SideBordersHidden() { return rex::cvar::GetFlagByName("dbz3_ui_side_borders") == "false"; }

bool Dbz3MaybeHiddenSize(uint32_t width, uint32_t height) {
  return height == 1024 && (width == 128 || width == 64);
}

const Dbz3TexturePackEntry* Dbz3HiddenEntry(uint64_t hash) {
  return Dbz3HiddenEntry(hash, Dbz3SideBordersHidden());
}

const Dbz3TexturePackEntry* Dbz3HiddenEntry(uint64_t hash, bool side_borders_hidden) {
  if (!side_borders_hidden) {
    return nullptr;
  }
  for (const HiddenTexture& t : Hidden()) {
    if (t.entry.hash == hash) {
      return &t.entry;
    }
  }
  return nullptr;
}

std::shared_ptr<const Dbz3TexturePackIndex::Image> Dbz3TexturePackIndex::Decoded(
    const Dbz3TexturePackEntry& entry) const {
  if (auto hidden = HiddenImage(entry)) {
    return hidden;
  }
  if (auto cached = CacheGet(entry.hash)) {
    return cached;
  }
  if (CacheFailed(entry.hash)) {
    return nullptr;
  }
  auto image = DecodeEntry(entry);
  if (image) {
    CachePut(entry.hash, image);
  }
  return image;
}

std::shared_ptr<const Dbz3TexturePackIndex::Image> Dbz3TexturePackIndex::TryDecoded(
    const Dbz3TexturePackEntry& entry) const {
  if (auto hidden = HiddenImage(entry)) {
    return hidden;
  }
  if (auto cached = CacheGet(entry.hash)) {
    return cached;
  }
  if (!CacheFailed(entry.hash)) {
    Enqueue(entry, true);
  }
  return nullptr;
}

bool Dbz3TexturePackIndex::IsDecoded(uint64_t hash) const { return CacheHas(hash); }

bool Dbz3TexturePackIndex::IsBroken(uint64_t hash) const { return CacheFailed(hash); }

void Dbz3TexturePackIndex::StartPreload(const Map& entries_) {
  const uint64_t budget = CacheBudget();
  uint64_t all = 0;
  for (const auto& [hash, e] : entries_) {
    all += DecodedBytes(e);
  }
  std::vector<uint64_t> learned = LoadUsage();
  {
    std::lock_guard<std::mutex> lock(Use().mutex);
    Use().previous = learned;
  }
  // Las aprendidas van primero; si el pack entero cabe, detras todas las demas. Si no
  // cabe, solo las aprendidas hasta el 60 % del presupuesto (el resto queda para lo que
  // pida la partida).
  std::vector<const Dbz3TexturePackEntry*> work;
  std::unordered_set<uint64_t> added;
  uint64_t bytes = 0;
  const uint64_t cap = all <= budget ? budget : budget * 6 / 10;
  for (uint64_t hash : learned) {
    auto it = entries_.find(hash);
    if (it == entries_.end() || !added.insert(hash).second) {
      continue;
    }
    if (bytes + DecodedBytes(it->second) > cap) {
      break;
    }
    bytes += DecodedBytes(it->second);
    work.push_back(&it->second);
  }
  const size_t learned_count = work.size();
  if (all <= budget) {
    for (const auto& [hash, e] : entries_) {
      if (added.insert(hash).second) {
        work.push_back(&e);
      }
    }
    bytes = all;
  }
  REXGPU_INFO(
      "dbz3: precarga del pack: {} texturas ({} MB; {} de partidas anteriores; pack {} MB, "
      "presupuesto {} MB)",
      work.size(), bytes >> 20, learned_count, all >> 20, budget >> 20);
  for (const Dbz3TexturePackEntry* e : work) {
    Enqueue(*e, false);
  }
}

uint64_t Dbz3PackLayout(const Dbz3TexturePackIndex::Image& image, uint32_t levels,
                        std::vector<Dbz3PackLevel>& layouts_out) {
  levels = std::clamp(levels, 1u, uint32_t(image.layout.size()));
  layouts_out.assign(image.layout.begin(), image.layout.begin() + levels);
  const Dbz3PackLevel& last = layouts_out.back();
  return uint64_t(last.offset) + uint64_t(last.row_pitch) * last.height;
}

void Dbz3PackCopyLevels(const Dbz3TexturePackIndex::Image& image, uint64_t bytes, uint8_t* dst,
                        bool swap_rb) {
  bytes = std::min<uint64_t>(bytes, image.rgba.size());
  if (!swap_rb) {
    std::memcpy(dst, image.rgba.data(), size_t(bytes));
    return;
  }
  // Por palabras de 32 bits (el compilador lo vectoriza): nunca se lee de `dst`.
  const uint8_t* src = image.rgba.data();
  for (size_t i = 0; i + 4 <= bytes; i += 4) {
    uint32_t v;
    std::memcpy(&v, src + i, 4);
    v = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16);
    std::memcpy(dst + i, &v, 4);
  }
}

}  // namespace rex::graphics
