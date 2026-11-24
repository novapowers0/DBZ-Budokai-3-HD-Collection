// dbz3 - Piezas del propio juego para las pantallas propias (marco, bocadillo, botones, fondos,
// anfitriones de los menus y caras del marcador). Se sacan de los AFS del jugador al arrancar, en
// memoria: ninguna imagen del juego va en la version publicada. Las usa style::Img.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace dbz3::ingame::native_ui {

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
};

// Una vez, con la ruta del juego ya resuelta: hilo propio que lee y decodifica las piezas.
void Prefetch();
bool Ready();
// Pieza `name` ("marco", "caras/11"...): la mueve a `out` (cada una se pide una vez).
bool Take(const std::string& name, Image& out);
// Atlas completo, sin recortar: las UV las produce el motor ACE original.
bool CombatAtlas(int entrada);
// Nivel base de un DDS seleccionado por su consumidor, sin publicarlo en la cache de imagenes.
bool DecodificarDds(std::span<const uint8_t> dds, Image& out);
// Entrada `n` de un AFS del juego, ya descomprimida (LZX de XMemCompress); vacia si falla. Cualquier hilo.
std::vector<uint8_t> LeerEntrada(const std::filesystem::path& afs, int n);

}  // namespace dbz3::ingame::native_ui
