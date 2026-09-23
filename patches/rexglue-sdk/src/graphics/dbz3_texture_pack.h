// DBZ3 HD Collection: "packs" de texturas al estilo PCSX2.
//
// Un pack es una CARPETA dentro de `mods/` que contiene ficheros de textura
// nombrados igual que el volcado dev:
//
//   <hash:16 hex>_<W>x<H>_<FOURCC>.dds     (o .png)
//
// El `hash` es XXH3-64 del bitmap ORIGINAL de la textura guest (linealizado y
// con endian-swap), es decir, exactamente el contenido del fichero DDS que
// produce el volcado. El runtime identifica la textura por ese hash y, si hay
// una entrada en el pack, sube la imagen del pack en lugar de los datos del
// juego. El factor (x1..x4) se deduce del tamano: W_pack / W_guest.
//
// El pack NO toca ficheros del juego ni la memoria guest: es una capa host.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/graphics/xenos.h>

namespace rex::graphics {

// DDS FourCC de los formatos comprimidos del juego que un pack puede
// representar; 0 = formato no soportado (ni volcable ni reemplazable).
uint32_t Dbz3DdsFourCc(xenos::TextureFormat format);

// Descripcion del volcado DDS de un formato guest: el sufijo del fichero
// (`<hash>_<W>x<H>_<sufijo>.dds`) y el formato DDS que hay que declarar para que
// el bitmap crudo se lea bien. El `suffix` de los comprimidos es el FourCC
// (DXT1/DXT3/DXT5); el de los formatos sin comprimir es un nombre legible
// (RGBA8, RGB565, RGB5A1, RGBA4, L8, L8A8, RGBA1010102).
//
// El layout de bits de cada formato es el del guest (el volcado es el bitmap
// CRUDO, byte a byte), verificado contra la conversion de Xenia
// (`pixel_formats.xesli` / `XePack*UNorm`): p.ej. k_5_6_5 tiene R en los bits
// BAJOS (R=0x001F, B=0xF800), y k_8_8_8_8 es R,G,B,A en memoria.
//
// Devuelve nullptr si el formato no se puede volcar (ni reemplazar).
struct Dbz3DumpFormat {
  const char* suffix;
  bool compressed;
  uint32_t fourcc;  // valido si `compressed`
  uint32_t bits;    // valido si !`compressed` (8/16/32)
  uint32_t r_mask;
  uint32_t g_mask;
  uint32_t b_mask;
  uint32_t a_mask;
};
const Dbz3DumpFormat* Dbz3DumpFormatFor(xenos::TextureFormat format);

// Formato que un pack puede REEMPLAZAR, en los DOS backends. La imagen del pack
// se decodifica a RGBA8 y se sube tal cual, asi que solo valen:
//  - los comprimidos DXT1/DXT3/DXT5 (el recurso host se crea RGBA8, ya
//    descomprimido), y
//  - las texturas que YA son RGBA8 (k_8_8_8_8), cuyo recurso host es RGBA8 con
//    swizzle identidad.
// Los formatos de 8/16 bits (k_8, k_8_8, k_5_6_5, k_4_4_4_4...) SI se vuelcan
// como referencia, pero todavia NO se pueden reemplazar (su recurso host no es
// RGBA8 y su swizzle es especifico del formato).
bool Dbz3PackReplaceableFormat(xenos::TextureFormat format);

// Entrada de un pack ya indexada por hash.
struct Dbz3TexturePackEntry {
  std::filesystem::path path;
  uint32_t width = 0;
  uint32_t height = 0;
  // Factor de resolucion respecto a la textura guest (1..4).
  uint32_t factor = 1;
  // Nombre del pack (carpeta) al que pertenece, para diagnosticos.
  std::string pack_name;
};

// Indice de packs activos. Se construye una sola vez (los packs requieren
// reiniciar, como el volcado). Es thread-safe para lecturas tras Init.
class Dbz3TexturePackIndex {
 public:
  static Dbz3TexturePackIndex& Get();

  // Devuelve true si hay al menos un pack con entradas.
  bool active() const;

  // Busca una textura por su hash (XXH3-64 del bitmap guest). Devuelve nullptr
  // si no hay pack para ese hash.
  const Dbz3TexturePackEntry* Find(uint64_t hash) const;

  // Numero de entradas indexadas (diagnostico).
  size_t size() const;

 private:
  Dbz3TexturePackIndex() = default;
  void EnsureInitialized() const;

  mutable bool initialized_ = false;
  mutable std::vector<std::pair<uint64_t, Dbz3TexturePackEntry>> entries_;
};

// Decodifica una imagen de pack (DDS: DXT1/3/5 o 32bpp sin comprimir, o PNG) a
// RGBA8 empaquetado (width*height*4). Devuelve false si el formato no se
// soporta.
bool Dbz3DecodePackImage(const std::filesystem::path& path, std::vector<uint8_t>& rgba,
                         uint32_t& width, uint32_t& height);

// Descripcion de un nivel de la cadena de mips generada para un pack.
struct Dbz3PackLevel {
  // Offset en bytes dentro del buffer devuelto.
  uint32_t offset = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  // Bytes por fila (alineado a la alineacion pedida).
  uint32_t row_pitch = 0;
};

// Construye la cadena de mips (box filter 2x2, con clamp en los bordes) de un
// pack a partir de su imagen base RGBA8 (`width * height * 4`). `levels` suele
// ser `key.mip_max_level + 1`. Las filas quedan con `row_pitch` alineado a
// `row_pitch_alignment` y cada nivel empieza en un offset tambien alineado (lo
// que necesitan tanto las copias de D3D12 como las de Vulkan). `out` recibe el
// buffer completo y `layouts_out` la descripcion de cada nivel.
void Dbz3BuildPackMips(const std::vector<uint8_t>& base, uint32_t width, uint32_t height,
                       uint32_t levels, uint32_t row_pitch_alignment, std::vector<uint8_t>& out,
                       std::vector<Dbz3PackLevel>& layouts_out);

}  // namespace rex::graphics
