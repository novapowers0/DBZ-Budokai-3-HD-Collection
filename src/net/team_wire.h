#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace dbz3::net::team_wire {

enum class Version : uint8_t { kLegacy = 7, kWide = 8 };
// HELLO is still 100 bytes with envelope v7. mods_hash is at 46 and its 32
// hex digits end at 77. Preserve the NUL at 78: v7 Reader::str ignores padding.
inline constexpr size_t kOfferAt = 79;
inline constexpr std::array<uint8_t, 7> kOffer{'T', 'E', 'A', 'M', 'I', 'D', 8};
inline void Offer(std::vector<uint8_t>& hello) {
  if (hello.size() == 100 && hello[kOfferAt - 1] == 0)
    std::copy(kOffer.begin(), kOffer.end(), hello.begin() + kOfferAt);
}
inline Version Negotiate(bool enabled, const std::vector<uint8_t>& hello) {
  return enabled && hello.size() == 100 && hello[kOfferAt - 1] == 0 &&
             std::equal(kOffer.begin(), kOffer.end(), hello.begin() + kOfferAt)
         ? Version::kWide : Version::kLegacy;
}

constexpr uint32_t MaxId(Version v) {
  return v == Version::kWide ? uint32_t(INT32_MAX) : uint32_t(UINT16_MAX);
}
template <typename Writer>
bool Write(Writer& w, Version v, const std::vector<int>& ids) {
  if (ids.size() > 3 || std::any_of(ids.begin(), ids.end(), [v](int id) {
        return id < 0 || uint32_t(id) > MaxId(v);
      })) return false;
  w.u8(uint8_t(ids.size()));
  for (int id : ids) {
    if (v == Version::kWide) w.u32(uint32_t(id));
    else w.u16(uint16_t(id));
  }
  return true;
}
template <typename Reader, typename ValidId>
bool Read(Reader& r, Version v, std::vector<int>& ids, ValidId valid_id) {
  const auto n = r.u8();
  if (!r.ok || n > 3) return false;
  std::vector<int> parsed;
  for (uint8_t i = 0; i < n; ++i) {
    const uint32_t id = v == Version::kWide ? r.u32() : r.u16();
    if (!r.ok || id > MaxId(v) || !valid_id(int(id))) return false;
    parsed.push_back(int(id));
  }
  ids.swap(parsed);
  return true;
}
}  // namespace dbz3::net::team_wire
