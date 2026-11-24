// Banco nativo para Z; NO forma parte de DBZ3_SOURCES. Compilar solo en el turno de integracion.
// clang++ -std=c++20 src/net/f10_offline_test.cpp -o <carpeta de trabajo>/f10_offline_test.exe
#include "f10_guards.h"
#include <cstdio>
#include <initializer_list>

int main() {
  unsigned passed = 0, total = 0;
  auto check = [&](bool ok, const char* name) {
    ++total;
    if (ok) ++passed;
    else std::printf("primera diferencia/caso fallido: %s\n", name);
  };
  using namespace dbz3::net::f10;
  for (uint32_t size : {1u, 999u, 1000u, 1001u, 35204u, 262144u}) {
    const uint32_t count = (size + 999) / 1000;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t n = i + 1 == count ? size - i * 1000 : 1000;
      check(SnapshotChunkOk(size, i, count, n), "fragmento del emisor");
      check(!SnapshotChunkOk(size, i, count + 1, n), "recuento falso");
    }
  }
  check(!SnapshotChunkOk(2000, 0, 2, 1), "hueco antes del ultimo fragmento");
  check(!SnapshotChunkOk(2000, 0, 2, 1100), "solape");
  check(!SnapshotChunkOk(1001, 1, 2, 2), "ultimo fragmento largo");
  check(!SnapshotChunkOk(0, 0, 0, 0), "instantanea vacia");
  check(!SnapshotChunkOk(262145, 0, 263, 1000), "limite de memoria");
  for (uint32_t age = 0; age <= 32; ++age)
    check(HistoryRangeOk(17840 - age, 17840, 16) == (age > 0 && age < 16), "edad journal 17840");
  check(SumTickOk(17840 - 255, 17840), "suma mas vieja admisible");
  check(!SumTickOk(17840 - 256, 17840), "suma pisa otra vuelta");
  check(SumTickOk(17840 + 32, 17840), "margen futuro");
  check(!SumTickOk(17840 + 33, 17840), "suma demasiado futura");
  check(!SumTickOk(0xFFFFFFFFu, 17840), "centinela");
  check(!StopOnBadSum(false, true, 17840, 0), "apagado de serie");
  check(!StopOnBadSum(true, false, 17840, 0), "lockstep sin cambio");
  check(!StopOnBadSum(true, true, 17840, 17840), "gracia");
  check(StopOnBadSum(true, true, 17840, 17839), "corte con aviso");
  std::printf("F10 nativo: %u/%u; compilacion y ejecucion corresponden a Z\n", passed, total);
  return passed == total ? 0 : 1;
}
