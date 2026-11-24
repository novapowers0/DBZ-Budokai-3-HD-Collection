// dbz3 - Sorteo de las absorciones de Super Bu del modo Budokai 2 (B2-P19), copiado de B2 (ELF SLES-52440, b2.c):
// FUN_001ef5b0 (llamado por la orden de guion FUN_001f9200). Pesos byte en 0x393B38 = 10,10,15,20,20,25 (Gotenks, Gohan,
// Vegeta, Cell, Freezer, Ten+Yamcha) y listas de exclusion de 3 IDs en 0x393B40. Total 100 menos los excluidos;
// r = rand() % total; se recorre en ciclo saltando los excluidos y se resta el peso mientras r > 0 (r = 0 cae en el primero:
// por eso, sin exclusiones, 11/10/15/20/20/24 de cada 100). Resultado = forma de B2 47 + indice.
// Exclusiones: B2 compara los IDs de las formas del rival (la base siempre); aqui con los IDs de B3 por la tabla de
// pasarela_b2.cpp kB2aB3 (B2 26,27,36 -> 6,9,64; 2,24,28 -> 3,5,4; 3,38 -> 7,72; 20 -> 33; 15 -> 27; 7,8 -> 12,13; 37 Yamhan -> mod).
#pragma once
#include <cstdint>

namespace dbz3::fusion::b2 {

constexpr int kPesos[6] = {10, 10, 15, 20, 20, 25};
constexpr int kExcl[6][3] = {{6, 9, 64}, {3, 5, 4}, {7, 72, -1}, {33, -1, -1}, {27, -1, -1}, {12, 13, -1}};

// bit k = entrada k excluida (rival con ese ID); yamhan = ID del mod YAMHAN (B2 37) o -1; vegito = el rival lleva la Potara (B2 38)
inline unsigned Mascara(int rival, int yamhan, bool vegito) {
  unsigned m = 0;
  for (int k = 0; k < 6; ++k)
    for (int j = 0; j < 3; ++j)
      if (kExcl[k][j] >= 0 && rival == kExcl[k][j]) m |= 1u << k;
  if (yamhan >= 0 && rival == yamhan) m |= 1u << 5;
  if (vegito) m |= 1u << 2;
  return m;
}
inline int Total(unsigned m) {
  int t = 100;
  for (int k = 0; k < 6; ++k)
    if (m >> k & 1) t -= kPesos[k];
  return t;
}
// r ya reducido (0 <= r < Total(m)); devuelve la entrada 0..5 (-1 si todo excluido)
inline int Sorteo(unsigned m, int r) {
  if ((m & 63u) == 63u) return -1;
  int i = -1;
  do {
    i = (i + 1) % 6;
    while (m >> i & 1) i = (i + 1) % 6;
    r -= kPesos[i];
  } while (r > 0);
  return i;
}

}  // namespace dbz3::fusion::b2
