// RESTO-01: deshacer por diferencia los contadores atomicos (fn 0x80000000 = WLog::Cas).
// Puro, sin memoria del juego: lo usa WLog::Restore y lo prueba src/net/rb_wlog_bench.py.
#pragma once
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dbz3::net::rb {

constexpr uint32_t kCasFn = 0x80000000u;
constexpr uint32_t Bswap32(uint32_t x) { return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24); }

// ticks: entradas de cada tick deshecho (a, v, fn, n; v y n tal cual en memoria, big endian). Devuelve, para cada palabra escrita SOLO
// por CAS en la ventana, lo que sumo la logica (suma de n - v por tick, aritmetica modulo 2^32). El valor bueno al deshacer es
// ahora - delta: se conserva lo que cambio otro dueno entre ticks (otro hilo, o una llamada externa del diario que al resimular no se
// repite). Sin otro dueno da lo mismo que reponer el v mas antiguo. Las palabras con alguna escritura normal quedan fuera (camino viejo).
template <class E>
std::unordered_map<uint32_t, uint32_t> CasDeltas(const std::vector<const std::vector<E>*>& ticks) {
  std::unordered_map<uint32_t, uint32_t> d;
  std::unordered_set<uint32_t> mixed;
  for (const auto* tk : ticks)
    for (const auto& e : *tk) {
      if (e.fn != kCasFn) mixed.insert(e.a);
      else d[e.a] += Bswap32(e.n) - Bswap32(e.v);
    }
  for (auto it = d.begin(); it != d.end();) it = mixed.count(it->first) ? d.erase(it) : std::next(it);
  return d;
}

}  // namespace dbz3::net::rb
