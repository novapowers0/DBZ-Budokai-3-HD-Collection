// F10 R6: observacion del nodo de tarea en luchador+6832. Nunca restaura memoria.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
namespace dbz3::net::rb {
class TaskProbe {
 public:
  void Reset() {
    for (auto* ring : {&boundary_, &replay_})
      for (auto& s : *ring) s.tick = UINT32_MAX;
  }

  void Capture(uint8_t* base, uint32_t mgr, uint32_t tick, bool replay) {
    auto& s = (replay ? replay_ : boundary_)[tick % kSlots];
    s.tick = tick;
    s.mgr = mgr;
    // sub_82092638: cabeceras de listas, indices, contadores y punteros del pool.
    s.globals_ok = Read(base, 0x8238CD50, s.globals.data(), s.globals.size());
    const uint32_t payload = Word(s.globals.data() + 0x90);
    const uint32_t count = Word(s.globals.data() + 0x94);
    const uint32_t pool = Word(s.globals.data() + 0x98);
    const uint64_t end = uint64_t(pool) + uint64_t(payload + 80) * count;
    const bool valid = s.globals_ok && payload <= 4096 && count && count <= 4096 &&
                       pool >= 0x40000000u && end <= 0x80000000u && !(pool & 3);
    for (uint32_t f = 0; f < s.nodes.size(); ++f) {
      auto& n = s.nodes[f];
      uint8_t p[4] = {};
      n.pointer_ok = Read(base, mgr + 2096 + 8240 * f + 6832, p, sizeof p);
      n.address = Word(p);
      n.data.clear();
      n.status = n.pointer_ok ? (n.address ? "fuera_pool" : "nulo") : "puntero_ilegible";
      if (!n.pointer_ok || !n.address || !valid) continue;
      const uint32_t stride = payload + 80;
      if (n.address < pool || uint64_t(n.address) + stride > end || (n.address - pool) % stride) continue;
      n.data.resize(stride);
      if (Read(base, n.address, n.data.data(), n.data.size())) n.status = "leido";
      else { n.data.clear(); n.status = "nodo_ilegible"; }
    }
  }

  bool Dump(const std::filesystem::path& path, uint32_t tick, uint32_t t0) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "# F10 tareas v1; bytes guest BE; solo observacion, sin normalizar ni restaurar\n"
           "# frontera=ultima muestra del limite del tick; resim=ultima resimulacion guardada; no historial exhaustivo\n"
           "# globales: CD50..CDEF; payload=CDE0, cantidad=CDE4, pool=CDE8, tabla_libres=CDEC\n"
           "tick\trelativo\tfase\tgestor\ttipo\tdireccion\testado\tbytes_hex\n";
    const uint32_t first = tick > 15 ? tick - 15 : 0;
    for (uint32_t k = first;; ++k) {
      for (bool replay : {false, true}) {
        const auto& s = (replay ? replay_ : boundary_)[k % kSlots];
        if (s.tick != k) continue;
        const auto prefix = [&]() -> std::ostream& {
          return out << std::dec << k << '\t' << (int64_t(k) - t0) << '\t'
                     << (replay ? "resim" : "frontera") << '\t' << std::hex << s.mgr << '\t';
        };
        prefix() << "globales\t8238cd50\t" << (s.globals_ok ? "leido" : "ilegible") << '\t';
        if (s.globals_ok) Hex(out, s.globals.data(), s.globals.size());
        out << '\n';
        for (uint32_t f = 0; f < s.nodes.size(); ++f) {
          const auto& n = s.nodes[f];
          prefix() << "luch" << f << '\t' << n.address << '\t' << n.status << '\t';
          Hex(out, n.data.data(), n.data.size());
          out << '\n';
        }
      }
      if (k == tick) break;
    }
    out.flush();
    return out.good();
  }

 private:
  static constexpr size_t kSlots = 64;
  static bool Read(uint8_t* base, uint32_t address, void* dest, size_t length) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), base + address, dest, length, &got) && got == length;
  }
  static uint32_t Word(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  }
  static void Hex(std::ostream& out, const uint8_t* bytes, size_t length) {
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) out << digits[bytes[i] >> 4] << digits[bytes[i] & 15];
  }
  struct Node {
    uint32_t address = 0;
    bool pointer_ok = false;
    const char* status = "sin_muestra";
    std::vector<uint8_t> data;
  };
  struct State {
    uint32_t tick = UINT32_MAX, mgr = 0;
    bool globals_ok = false;
    std::array<uint8_t, 160> globals{};
    std::array<Node, 2> nodes;
  };
  std::array<State, kSlots> boundary_, replay_;
};
inline TaskProbe g_task_probe;
}  // namespace dbz3::net::rb
#endif
