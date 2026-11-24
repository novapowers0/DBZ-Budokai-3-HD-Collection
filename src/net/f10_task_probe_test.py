"""Banco aislado de la sonda F10; memoria simulada, sin abrir el juego.

Compila con el clang++ local y conserva los artefactos en E:/DBZ3HD_trabajo.
"""
from pathlib import Path
import subprocess
import tempfile


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
using SIZE_T = size_t;
struct Region { uint32_t address; std::vector<uint8_t> data; };
std::vector<Region> memory;
size_t reads = 0;
bool partial = false;
void* GetCurrentProcess() { return nullptr; }
bool ReadProcessMemory(void*, const void* address, void* output, size_t length, SIZE_T* got) {
  ++reads;
  const auto a = reinterpret_cast<uintptr_t>(address);
  for (const auto& r : memory) {
    if (a >= r.address && a + length <= uint64_t(r.address) + r.data.size()) {
      *got = partial ? length - 1 : length;
      std::memcpy(output, r.data.data() + a - r.address, *got);
      return true;
    }
  }
  *got = 0;
  return false;
}
#include "rb_task_probe.h"
void word(Region& r, size_t off, uint32_t v) {
  for (unsigned b = 0; b < 4; ++b) r.data[off + b] = uint8_t(v >> (24 - b * 8));
}
std::string contents(const char* path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in), {});
}
int main() {
  constexpr uint32_t mgr = 0x82300000, pool = 0x400A0000;
  memory = {{0x8238CD50, std::vector<uint8_t>(160)},
            {mgr, std::vector<uint8_t>(35200)},
            {pool, std::vector<uint8_t>(112 * 4)}};
  word(memory[0], 0x90, 32); word(memory[0], 0x94, 4); word(memory[0], 0x98, pool);
  word(memory[1], 2096 + 6832, pool);
  word(memory[1], 2096 + 8240 + 6832, pool + 112);
  word(memory[2], 28, 0x82118598);
  dbz3::net::rb::TaskProbe probe;
  probe.Capture(nullptr, mgr, 100, false);
  // Cambiar la memoria despues no debe cambiar la muestra original.
  word(memory[2], 28, 0xDEADBEEF);
  probe.Capture(nullptr, mgr, 100, true);
  assert(probe.Dump("samples.tsv", 100, 90));
  const auto saved = contents("samples.tsv");
  assert(saved.find("100\t10\tfrontera") != std::string::npos);
  assert(saved.find("100\t10\tresim") != std::string::npos);
  assert(saved.find("82118598") != std::string::npos);
  assert(saved.find("deadbeef") != std::string::npos);
  // Un puntero desalineado al slot no provoca lecturas fuera del nodo.
  word(memory[1], 2096 + 6832, pool + 4);
  word(memory[1], 2096 + 8240 + 6832, 0);
  reads = 0;
  probe.Capture(nullptr, mgr, 101, false);
  assert(reads == 3);
  assert(probe.Dump("invalid.tsv", 101, 90));
  const auto invalid = contents("invalid.tsv");
  assert(invalid.find("400a0004\tfuera_pool\t\n") != std::string::npos);
  assert(invalid.find("0\tnulo\t\n") != std::string::npos);
  // Una lectura parcial se declara ilegible, no se rellena con valores inventados.
  partial = true;
  probe.Capture(nullptr, mgr, 102, false);
  partial = false;
  assert(probe.Dump("partial.tsv", 102, 90));
  const auto failed = contents("partial.tsv");
  assert(failed.find("globales\t8238cd50\tilegible\t\n") != std::string::npos);
  assert(failed.find("puntero_ilegible\t\n") != std::string::npos);
  probe.Capture(nullptr, mgr, 164, false);
  assert(probe.Dump("expired.tsv", 100, 90));
  const auto expired = contents("expired.tsv");
  assert(expired.find("100\t10\tfrontera") == std::string::npos);
  probe.Reset();
  assert(probe.Dump("reset.tsv", 102, 90));
  assert(contents("reset.tsv").find("102\t12\t") == std::string::npos);
  assert(!probe.Dump("missing/dir/output.tsv", 102, 90));
}
'''


def main():
    out = Path(tempfile.mkdtemp(prefix="sonda_tareas_banco_", dir="E:/DBZ3HD_trabajo/f10-online"))
    source = out / "probe_test.cpp"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = Path("C:/Program Files/LLVM/bin/clang++.exe")
    command = [str(compiler), "-std=c++20", "-D_WIN32", "-Wall", "-Wextra", "-Werror",
               "-UNDEBUG", "-I", str(Path(__file__).resolve().parent), str(source),
               "-o", str(out / "probe_test.exe")]
    subprocess.run(command, check=True, cwd=out)
    subprocess.run([str(out / "probe_test.exe")], check=True, cwd=out)
    print(f"PASS: frontera/resim, copia independiente, limites del pool, lecturas parciales, caducidad, reset y error de salida. Artefactos: {out}")


if __name__ == "__main__":
    main()
