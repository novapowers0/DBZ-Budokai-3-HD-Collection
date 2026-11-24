// F10: reglas puras del banco offline. El llamador las activa con dbz3_net_validacion_f10 (false).
#pragma once
#include <cstdint>

namespace dbz3::net::f10 {

// El emisor usa trozos de 1000 B y un ultimo trozo exacto; nunca huecos ni solapes.
constexpr bool SnapshotChunkOk(uint32_t total, uint32_t idx, uint32_t cnt, uint32_t n) {
  return total > 0u && total <= 262144u && cnt == (total + 999u) / 1000u &&
         idx < cnt && n > 0u && n <= 1000u && idx * 1000u + n <= total &&
         (idx * 1000u + n == total || n == 1000u);
}

// No permitir que una suma ajena pise otra vuelta de la casilla modulo 256.
constexpr bool SumTickOk(uint32_t tick, uint32_t now) {
  return tick != 0xFFFFFFFFu && uint64_t(tick) + 255u >= now &&
         uint64_t(tick) <= uint64_t(now) + 32u;
}

// WLog::Has exige edad estrictamente menor que kTicks, ademas de cada etiqueta de tick.
constexpr bool HistoryRangeOk(uint32_t oldest, uint32_t now, uint32_t capacity) {
  return oldest < now && now - oldest < capacity;
}

// La suma solo vale fuera del periodo de gracia de una reparacion anterior.
constexpr bool StopOnBadSum(bool enabled, bool rollback, uint32_t tick, uint32_t ignore_until) {
  return enabled && rollback && tick > ignore_until;
}

}  // namespace dbz3::net::f10
