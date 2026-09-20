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

namespace rex::graphics::d3d12 {

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

// Decodifica una imagen de pack (DDS: DXT1/3/5 o 32bpp sin comprimir) a RGBA8
// empaquetado (width*height*4). Devuelve false si el formato no se soporta.
bool Dbz3DecodePackImage(const std::filesystem::path& path, std::vector<uint8_t>& rgba,
                         uint32_t& width, uint32_t& height);

}  // namespace rex::graphics::d3d12
