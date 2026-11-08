// SPDX-License-Identifier: MIT
// P1 (7/11): arnes offline de los puentes de colision del 0x7DD (Collision_Raycast -> wcf::Backend::Ray y
// Collision_MoveSphere -> wcf::Backend::Sweep de src/iw_bubbles_wcf.h, el mismo codigo que usa el juego). Lee el #WCF RAW
// del campo (DATA_CMN 907/0 de la copia del jugador) y por stdin, una consulta por linea:
//   "x y z  x y z  mascara"            rayo
//   "S x y z  x y z  radio mascara"    barrido
// y escribe "hit px py pz tipo material nx ny nz d" por consulta (una linea, al momento). Lo usa persecucion.py.
// Compilar: clang++ -std=c++20 -O1 -I src tools/iw_mips/rayo7dd.cpp -o <salida>/rayo7dd.exe
#include <cstdio>
#include <cstring>
#include <vector>
#include "iw_bubbles_wcf.h"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::FILE* f = std::fopen(argv[1], "rb");
  if (!f) return 3;
  std::vector<std::uint8_t> b;
  for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(std::uint8_t(c));
  std::fclose(f);
  dbz3::iwbubbles::wcf::Backend w;
  if (!w.Load(b.data(), b.size(), {-1500, -1500, 1500, 1500, 3})) { std::fprintf(stderr, "%s\n", w.Error().c_str()); return 4; }
  char linea[512];
  while (std::fgets(linea, sizeof linea, stdin)) {
    float a[3], z[3], radio = 0;
    unsigned m = 0;
    dbz3::iwbubbles::wcf::Result r;
    if (linea[0] == 'S') {
      if (std::sscanf(linea + 1, "%f %f %f %f %f %f %f %u", a, a + 1, a + 2, z, z + 1, z + 2, &radio, &m) != 8) continue;
      r = w.Sweep({a[0], a[1], a[2]}, {z[0], z[1], z[2]}, radio, m);
    } else {
      if (std::sscanf(linea + (linea[0] == 'R'), "%f %f %f %f %f %f %u", a, a + 1, a + 2, z, z + 1, z + 2, &m) != 7) continue;
      r = w.Ray({a[0], a[1], a[2]}, {z[0], z[1], z[2]}, m);
    }
    std::printf("%d %.9g %.9g %.9g %u %u %.9g %.9g %.9g %.9g\n", int(r.hit), r.point.x, r.point.y, r.point.z,
                unsigned(r.contact.type), unsigned(r.contact.material), r.contact.normal.x, r.contact.normal.y,
                r.contact.normal.z, r.contact.d);
    std::fflush(stdout);  // tambien en tuberia, linea a linea
  }
  return 0;
}
