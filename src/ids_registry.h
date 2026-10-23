#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dbz3::ids {

using LogicalId = uint32_t;

// Guest indices are physical slots in retail tables. These six cut records and
// twenty extra records are the only safe first proxy candidates; they are not
// themselves the logical identity of an added character.
inline constexpr std::array<uint8_t, 26> kProxyCandidates = {
    22, 23, 24, 25, 26, 31,
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
inline constexpr LogicalId kFirstLogicalId = 64;
inline constexpr uint16_t kMaxLogicalForms = 255;
inline constexpr uint16_t kGuestFormRows = 8;

struct Character {
  LogicalId id = 0;
  std::string key;
  std::string source;
  uint16_t forms = 1;
};

struct ProxyBinding {
  LogicalId logical = 0;
  uint8_t guest = 0;
};

struct FormBinding {
  uint16_t logical = 0;
  uint8_t guest = 0;
};

struct PageRange {
  size_t first = 0;
  size_t count = 0;
};

inline std::optional<PageRange> PageRangeFor(size_t total, uint32_t page, size_t page_size) {
  if (!page_size) return std::nullopt;
  const uint64_t first = uint64_t(page) * uint64_t(page_size);
  if (first >= total) return std::nullopt;
  return PageRange{size_t(first), std::min(page_size, total - size_t(first))};
}

// Host-side registry. It can hold far more identities than the guest has
// character rows. Only the currently selected page is bound to guest proxies.
class Registry {
 public:
  void Reset() {
    characters_.clear();
    keys_.clear();
    reserved_guest_.clear();
    active_bindings_.clear();
    logical_by_guest_.clear();
  }

  void ReserveGuestId(uint32_t id) {
    if (id < 64) reserved_guest_.insert(uint8_t(id));
  }

  bool Register(LogicalId id, std::string key, std::string source, uint32_t forms) {
    if (id < kFirstLogicalId || key.empty() || forms == 0 || forms > kMaxLogicalForms ||
        characters_.contains(id) || keys_.contains(key)) {
      return false;
    }
    Character c{id, std::move(key), std::move(source), uint16_t(forms)};
    keys_.emplace(c.key, id);
    characters_.emplace(id, std::move(c));
    return true;
  }

  // Assigns the requested logical page to free guest rows. Previous page
  // bindings are replaced atomically; native rows and explicitly reserved IDs
  // are never selected as proxies.
  // L: pin - `pins` (logical id + the guest row it already holds) survive the page change: their
  // rows are kept out of `available` and their logical ids are skipped in `ids`; they stay bound.
  std::optional<std::vector<ProxyBinding>> BindPage(const std::vector<LogicalId>& ids,
                                                    const std::vector<ProxyBinding>& pins = {}) {
    std::unordered_set<LogicalId> unique;
    std::unordered_set<uint8_t> pinned_guest;
    std::vector<ProxyBinding> next;
    for (const ProxyBinding& pin : pins) {
      const bool candidate = std::find(kProxyCandidates.begin(), kProxyCandidates.end(), pin.guest) != kProxyCandidates.end();
      if (!candidate || reserved_guest_.contains(pin.guest) || !characters_.contains(pin.logical) ||
          !unique.insert(pin.logical).second || !pinned_guest.insert(pin.guest).second) {
        continue;   // pin invalido: se ignora (la plaza vuelve a la pagina)
      }
      next.push_back(pin);
    }
    std::vector<uint8_t> available;
    for (uint8_t candidate : kProxyCandidates) {
      if (!reserved_guest_.contains(candidate) && !pinned_guest.contains(candidate)) available.push_back(candidate);
    }
    size_t used = 0;
    for (LogicalId id : ids) {
      if (unique.contains(id)) {
        if (std::any_of(pins.begin(), pins.end(), [id](const ProxyBinding& b) { return b.logical == id; })) continue;
        return std::nullopt;   // duplicado en la pagina
      }
      if (!characters_.contains(id) || used >= available.size()) return std::nullopt;
      unique.insert(id);
      next.push_back({id, available[used++]});
    }
    active_bindings_ = std::move(next);
    logical_by_guest_.clear();
    for (const auto& binding : active_bindings_) logical_by_guest_.emplace(binding.guest, binding.logical);
    return active_bindings_;
  }

  const Character* Find(LogicalId id) const {
    const auto it = characters_.find(id);
    return it == characters_.end() ? nullptr : &it->second;
  }

  std::optional<LogicalId> LogicalForGuest(uint32_t guest) const {
    const auto it = logical_by_guest_.find(uint8_t(guest));
    return guest < 64 && it != logical_by_guest_.end() ? std::optional<LogicalId>(it->second) : std::nullopt;
  }

  std::optional<uint8_t> GuestForLogical(LogicalId logical) const {
    const auto it = std::find_if(active_bindings_.begin(), active_bindings_.end(),
                                 [logical](const ProxyBinding& binding) { return binding.logical == logical; });
    return it == active_bindings_.end() ? std::nullopt : std::optional<uint8_t>(it->guest);
  }

  std::optional<std::vector<FormBinding>> FormWindow(LogicalId id, uint32_t first) const {
    const Character* character = Find(id);
    if (!character || first >= character->forms) return std::nullopt;
    const uint32_t count = std::min<uint32_t>(kGuestFormRows, uint32_t(character->forms) - first);
    std::vector<FormBinding> window;
    window.reserve(count);
    for (uint32_t i = 0; i < count; ++i) window.push_back({uint16_t(first + i), uint8_t(i)});
    return window;
  }

  size_t size() const { return characters_.size(); }
  const std::vector<ProxyBinding>& active_bindings() const { return active_bindings_; }
  size_t ProxyCapacity() const {
    return size_t(std::count_if(kProxyCandidates.begin(), kProxyCandidates.end(),
                                [&](uint8_t candidate) { return !reserved_guest_.contains(candidate); }));
  }
  uint64_t ActiveProxyMask() const {
    uint64_t mask = 0;
    for (const auto& binding : active_bindings_) mask |= uint64_t(1) << binding.guest;
    return mask;
  }

 private:
  std::unordered_map<LogicalId, Character> characters_;
  std::unordered_map<std::string, LogicalId> keys_;
  std::unordered_set<uint8_t> reserved_guest_;
  std::vector<ProxyBinding> active_bindings_;
  std::unordered_map<uint8_t, LogicalId> logical_by_guest_;
};

}  // namespace dbz3::ids
