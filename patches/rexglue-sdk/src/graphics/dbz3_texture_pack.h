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
// juego. El factor (entero, x1, x4, x8...) se deduce del tamano: W_pack / W_guest.
//
// El pack NO toca ficheros del juego ni la memoria guest: es una capa host.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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

// Un nivel de la cadena de mips de una imagen de pack ya dispuesta para la GPU.
struct Dbz3PackLevel {
  uint32_t offset = 0;     // bytes desde el inicio (multiplo de 512)
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t row_pitch = 0;  // bytes por fila (multiplo de 512)
};

// Entrada de un pack ya indexada por hash.
struct Dbz3TexturePackEntry {
  uint64_t hash = 0;
  std::filesystem::path path;
  uint32_t width = 0;
  uint32_t height = 0;
  // Factor de resolucion respecto a la textura guest (entero >= 1; imagen <= 16384).
  uint32_t factor = 1;
  // Nombre del pack (carpeta) al que pertenece, para diagnosticos.
  std::string pack_name;
};

// Indice de packs activos. Se construye al arrancar y se rehace si cambia la lista de
// packs (instalar o activar uno en el launcher), en SEGUNDO PLANO: hasta que termina, active() es
// false y el juego usa las texturas originales; las caches de texturas rehacen lo
// cargado cuando pasa a true. Lecturas thread-safe en cuanto active() es true.
class Dbz3TexturePackIndex {
 public:
  static Dbz3TexturePackIndex& Get();

  // Devuelve true si el indice ya esta listo y hay al menos un pack con entradas.
  bool active() const;

  // Busca una textura por su hash (XXH3-64 del bitmap guest). Devuelve nullptr
  // si no hay pack para ese hash.
  const Dbz3TexturePackEntry* Find(uint64_t hash) const;

  // Numero de entradas indexadas (diagnostico).
  size_t size() const;

  // Imagen decodificada (RGBA8) de una entrada CON su cadena de mips completa (hasta
  // 1x1) y ya dispuesta como la quiere la copia a la GPU (filas y niveles alineados a
  // 512 B, vale para D3D12 y Vulkan): la prepara un hilo de fondo y el hilo de la GPU
  // la sube con UNA copia. nullptr si el fichero no se puede leer.
  struct Image {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Dbz3PackLevel> layout;  // todos los niveles
  };
  // Bloqueante: de la cache de RAM o decodificando en el hilo que llama (Vulkan, que no
  // puede mostrar la original en el recurso Nx mientras tanto).
  std::shared_ptr<const Image> Decoded(const Dbz3TexturePackEntry& entry) const;
  // Sin bloquear (como Burst Limit): de la cache, o nullptr y se encarga a los hilos de
  // fondo con prioridad; el juego muestra la original y la recarga cuando IsDecoded.
  std::shared_ptr<const Image> TryDecoded(const Dbz3TexturePackEntry& entry) const;
  bool IsDecoded(uint64_t hash) const;
  // Fichero ilegible o de otro tamano: no se reintenta, se queda la original.
  bool IsBroken(uint64_t hash) const;

 private:
  using Map = std::unordered_map<uint64_t, Dbz3TexturePackEntry>;
  Dbz3TexturePackIndex() = default;
  void EnsureInitialized() const;
  static void Build(const std::string& list, Map& entries);
  static void StartPreload(const Map& entries);
  const Map* Entries() const;

  // El indice se rehace si cambia la lista de packs (pack instalado desde el launcher en
  // la misma sesion: antes se quedaba el indice vacio del arranque hasta reiniciar).
  // Los indices viejos no se liberan: la precarga y las caches guardan punteros a sus
  // entradas (pocos MB y solo al cambiar la lista).
  mutable std::mutex check_mutex_;
  mutable std::string built_list_;           // con check_mutex_
  mutable bool started_ = false;             // con check_mutex_
  mutable std::atomic<int64_t> next_check_{0};
  mutable std::atomic<bool> building_{false};
  mutable std::atomic<bool> ready_{false};
  mutable std::atomic<const Map*> map_{nullptr};
};

// Interruptor en vivo de los packs (menu F1, cvar dbz3_texture_packs_on del
// launcher, leida por nombre): false = texturas originales sin reiniciar.
bool Dbz3TexturePacksOn();

// Decodifica una imagen de pack (DDS: DXT1/3/5 o 32bpp sin comprimir, o PNG) a
// RGBA8 empaquetado (width*height*4). Devuelve false si el formato no se
// soporta.
bool Dbz3DecodePackImage(const std::filesystem::path& path, std::vector<uint8_t>& rgba,
                         uint32_t& width, uint32_t& height);

// Los `levels` primeros niveles de `image` (un prefijo de su disposicion) y los bytes
// que ocupan en el buffer de subida.
uint64_t Dbz3PackLayout(const Dbz3TexturePackIndex::Image& image, uint32_t levels,
                        std::vector<Dbz3PackLevel>& layouts_out);
// Copia esos `bytes` a `dst` (el buffer de subida ya mapeado) en un solo bloque: copiar
// fila a fila a esa memoria (write-combined) iba a ~15 MB/s y una textura 4096x4096
// tardaba 6 s en el hilo de la GPU; en bloque son ~10 ms.
// swap_rb: la imagen del pack viene en colores reales (RGBA) y la textura guest
// es k_8_8_8_8, que el B3 HD guarda como BGRA (mascaras del #AZT 00FF0000 = R):
// se intercambian R y B para quedar en el orden de bytes del guest.
void Dbz3PackCopyLevels(const Dbz3TexturePackIndex::Image& image, uint64_t bytes, uint8_t* dst,
                        bool swap_rb);

// Texturas de pack subidas (acumulado; la linea `perf` lo muestra como pack=).
extern std::atomic<uint64_t> g_dbz3_pack_uploads;
// Coste de buscar en el pack (linealizar + XXH3 en el hilo de GPU): `packhash=` en la linea perf.
extern std::atomic<uint64_t> g_dbz3_pack_hash_us;
extern std::atomic<uint64_t> g_dbz3_pack_hash_n;

// Bytes de VRAM de una entrada con todos sus mips (RGBA8, 4/3 de la base) y reparto de
// texturas HD nuevas por submission: ~64 MB, para que al encender el pack o entrar a un
// escenario no entren todas en el mismo frame (siempre entra al menos una).
inline uint64_t Dbz3PackGpuBytes(const Dbz3TexturePackEntry& e) {
  return uint64_t(e.width) * e.height * 16 / 3;
}
constexpr uint64_t kDbz3PackGrantBytesPerSubmission = uint64_t(64) << 20;

// Anota que la partida uso esta textura del pack: la proxima vez se precarga primero
// (shader_cache/dbz3_pack_uso.txt). Barato; la escritura va en otro hilo.
void Dbz3PackNoteUsed(uint64_t hash);

// true si la textura guest es k_8_8_8_8 (el pack se sube con R y B intercambiados).
bool Dbz3PackSwapsRedBlue(xenos::TextureFormat format);

// Entrada que corresponde AHORA al contenido `hash` de una textura cuya entrada
// cacheada es `cached`: la misma si el hash coincide, otra del mismo tamano si
// la memoria se reutilizo para otra textura del pack, o nullptr si ya no es del
// pack (el recurso host tiene el tamano de `cached`, asi que no vale otro).
const Dbz3TexturePackEntry* Dbz3PackRevalidate(const Dbz3TexturePackEntry* cached, uint64_t hash);
// Igual, pero con el estado APLICADO del pack (D3D12 transaccional): no relee
// las cvars vivas durante la submission.
const Dbz3TexturePackEntry* Dbz3PackRevalidate(const Dbz3TexturePackEntry* cached, uint64_t hash,
                                               bool packs_on, bool side_borders_hidden);

// Tiras doradas de los lados de los menus del juego: textura "borde" de data_usi (128x1024, la misma en
// 2033/2039/2043/2049/2055/2062) y la greca dorada del marco 16:9 (64x1024). Con "Bordes dorados" apagado
// (F1 > Imagen, cvar dbz3_ui_side_borders del launcher, leida por nombre; apagado de serie) se suben
// transparentes por el camino del pack, haya pack o no.
bool Dbz3SideBordersHidden();
// Tamano de alguna de esas tiras (sin pack HD solo se hashean estas texturas).
bool Dbz3MaybeHiddenSize(uint32_t width, uint32_t height);
// Entrada virtual (imagen transparente) si `hash` es una tira y estan ocultas; si no, nullptr.
// Decoded/TryDecoded la devuelven ya hecha.
const Dbz3TexturePackEntry* Dbz3HiddenEntry(uint64_t hash);
// Igual, con el estado aplicado de "Bordes dorados" (D3D12 transaccional).
const Dbz3TexturePackEntry* Dbz3HiddenEntry(uint64_t hash, bool side_borders_hidden);

}  // namespace rex::graphics
