"""Banco sin juego de RESTO-01: deshacer contadores de referencias (rb_cas_undo.h) como WLog::Restore.

python -B src/net/rb_wlog_bench.py
Compila con el clang++ local (artefactos en E:/DBZ3HD_trabajo/f10-online). El arnes repite el reparto de WLog::Restore
(ajenas, CasDeltas, bucle de deshacer) sobre una memoria simulada; CasDeltas es el codigo real del juego.
PASS = tras deshacer y resimular, cada palabra vale lo mismo que en el avance, con altas ajenas que no se repiten.
"""
from pathlib import Path
import subprocess
import tempfile

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <random>
#include "rb_cas_undo.h"
using namespace dbz3::net::rb;
struct E { uint32_t a, v, fn, n; };
struct Op { uint32_t a; int32_t d; bool cas; };
struct Sim {
  std::map<uint32_t, uint32_t> mem;             // valor en orden del PC
  std::vector<std::vector<E>> ring;
  std::vector<uint32_t> seen;
  void Begin() { ring.emplace_back(); seen.clear(); }
  void Logic(const Op& o) {                     // WLog::Store / WLog::Cas: valor anterior a la primera escritura del tick
    bool s = false; for (auto x : seen) s |= x == o.a;
    if (!s) { seen.push_back(o.a); ring.back().push_back({o.a, Bswap32(mem[o.a]), o.cas ? kCasFn : 0x82111598u, 0}); }
    mem[o.a] += uint32_t(o.d);
  }
  void End() { for (auto& e : ring.back()) e.n = Bswap32(mem[e.a]); }
  void Restore(bool fix) {                      // espejo de WLog::Restore
    std::map<uint32_t, uint32_t> last; std::unordered_set<uint32_t> foreign;
    for (auto& tk : ring) for (auto& e : tk) last[e.a] = e.n;
    for (auto& [a, n] : last) if (Bswap32(mem[a]) != n) foreign.insert(a);
    std::unordered_map<uint32_t, uint32_t> cas;
    if (fix) {
      std::vector<const std::vector<E>*> t; for (auto& tk : ring) t.push_back(&tk);
      cas = CasDeltas(t);
      for (auto& [a, x] : cas) mem[a] -= x;
    }
    for (size_t k = ring.size(); k-- > 0;)
      for (size_t i = ring[k].size(); i-- > 0;)
        if (!foreign.count(ring[k][i].a) && !cas.count(ring[k][i].a)) mem[ring[k][i].a] = Bswap32(ring[k][i].v);
    ring.clear();
  }
};
// ticks[k] = mandos de la logica; ajeno[k] = cambios de otro dueno tras el tick k (persisten, no se repiten al resimular).
std::map<uint32_t, uint32_t> Run(std::map<uint32_t, uint32_t> m0, const std::vector<std::vector<Op>>& ticks,
                                 const std::vector<std::vector<Op>>& ajeno, bool fix, std::map<uint32_t, uint32_t>* fwd) {
  Sim s; s.mem = m0;
  for (size_t k = 0; k < ticks.size(); ++k) {
    s.Begin(); for (auto& o : ticks[k]) s.Logic(o); s.End();
    for (auto& o : ajeno[k]) s.mem[o.a] += uint32_t(o.d);
  }
  *fwd = s.mem;
  s.Restore(fix);
  for (auto& t : ticks) { s.Begin(); for (auto& o : t) s.Logic(o); s.End(); }
  return s.mem;
}
int main() {
  std::map<uint32_t, uint32_t> fwd;
  // Caso del cierre 17840: baja (sub_82080750) en el tick 0, alta del diario en el tick 1 (no se repite).
  {
    std::map<uint32_t, uint32_t> m0{{0x405D6424, 2}};
    std::vector<std::vector<Op>> t{{{0x405D6424, -1, true}}, {}}, aj{{{0x405D6424, +1, true}}, {}};
    auto bad = Run(m0, t, aj, false, &fwd);
    assert(fwd[0x405D6424] == 2 && bad[0x405D6424] == 1);   // sin el arreglo: una referencia de menos -> destruccion temprana
    auto ok = Run(m0, t, aj, true, &fwd);
    assert(ok[0x405D6424] == 2);
  }
  // Alta ajena ENTRE dos ticks que bajan la misma palabra (no sale como ajena: el camino viejo la perdia).
  {
    std::map<uint32_t, uint32_t> m0{{0x40100000, 3}};
    std::vector<std::vector<Op>> t{{{0x40100000, -1, true}}, {{0x40100000, -1, true}}}, aj{{{0x40100000, +1, true}}, {}};
    auto ok = Run(m0, t, aj, true, &fwd);
    assert(ok == fwd);
  }
  // Palabra con escritura normal: camino viejo intacto.
  {
    std::map<uint32_t, uint32_t> m0{{0x40200000, 7}};
    std::vector<std::vector<Op>> t{{{0x40200000, 5, false}, {0x40200000, -1, true}}}, aj{{}};
    auto ok = Run(m0, t, aj, true, &fwd);
    assert(ok == fwd);
  }
  // Aleatorio: 4 contadores, 1-16 ticks, bajas/altas de la logica y ajenas entre ticks.
  std::mt19937 rng(77);
  int casos = 0, viejo_mal = 0;
  for (int it = 0; it < 20000; ++it) {
    std::map<uint32_t, uint32_t> m0;
    for (uint32_t w = 0; w < 4; ++w) m0[0x40300000 + 4 * w] = rng() % 50;
    const int n = 1 + rng() % 16;
    std::vector<std::vector<Op>> t(n), aj(n);
    for (int k = 0; k < n; ++k) {
      for (int j = rng() % 4; j-- > 0;) t[k].push_back({0x40300000 + 4 * (rng() % 4), int32_t(rng() % 3) - 1, true});
      if (rng() % 3 == 0) aj[k].push_back({0x40300000 + 4 * (rng() % 4), int32_t(rng() % 5) - 2, true});
    }
    auto ok = Run(m0, t, aj, true, &fwd);
    assert(ok == fwd);
    auto bad = Run(m0, t, aj, false, &fwd);
    viejo_mal += bad != fwd;
    ++casos;
  }
  std::printf("casos %d; camino viejo distinto del avance en %d; con dbz3_net_rb_cas_delta 0 distintos\n", casos, viejo_mal);
  assert(viejo_mal > 0);
}
'''


def main():
    out = Path(tempfile.mkdtemp(prefix="rb_wlog_banco_", dir="E:/DBZ3HD_trabajo/f10-online"))
    source = out / "rb_wlog_bench.cpp"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = Path("C:/Program Files/LLVM/bin/clang++.exe")
    command = [str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
               "-I", str(Path(__file__).resolve().parent), str(source), "-o", str(out / "rb_wlog_bench.exe")]
    subprocess.run(command, check=True, cwd=out)
    r = subprocess.run([str(out / "rb_wlog_bench.exe")], check=True, cwd=out, capture_output=True, text=True)
    print(r.stdout.strip())
    print(f"PASS: cierre 17840 (2 vs 1 sin arreglo), alta entre ticks, escritura normal y 20000 aleatorios. Artefactos: {out}")


if __name__ == "__main__":
    main()
