#pragma once
#include <cstdint>

namespace dbz3::iwchrfade {
// EU2F4450 lee R/G/B en +18FC/FD/FE y +1900/01/02; B3 US82109D90
// lee los mismos bytes en +6268/69/70 y +6272/73/74. Conservar los
// bytes del color PS2 al escribir un u32 big-endian en el actor B3.
constexpr uint32_t ColorB3(uint32_t ps2) {
  return (ps2 >> 24) | ((ps2 >> 8) & 0x0000FF00u) |
         ((ps2 << 8) & 0x00FF0000u) | (ps2 << 24);
}
// EU480C54: 20,20,.95,0,003C90DC,003C90DC (EU32B2A0 -> EU2F2D70).
constexpr uint32_t kImpacto = ColorB3(0x003C90DCu);
constexpr uint32_t kNeutro = 0xFFFFFFFFu;
}  // namespace dbz3::iwchrfade
