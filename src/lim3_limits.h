#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace dbz3::lim3 {

// The runtime's existing flag filters ALL enabled source-looking mods. Only
// request it if each such directory contains only manifests and subdirectories
// used for source data (the runtime only serves root files or us/eu content).
// Reject ambiguous trees; in particular a source may also override a whole file.
inline bool CanFilterSources(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto dir = fs::file_type::directory;
  const auto has = [&](const fs::path& p) { return fs::exists(p, ec); };
  size_t sources = 0;
  for (fs::directory_iterator i(root, ec), end; !ec && i != end; i.increment(ec)) {
    const auto name = i->path().filename().native();
    const auto off = fs::path(".disabled").native();
    if (name.empty() || name.front() == '.' || name.ends_with(off)) continue;
    const auto type = i->status(ec).type();
    if (ec) return false;
    if (type != dir) continue;
    const auto& p = i->path();
    if (has(p / ".disabled")) continue;
    if (ec) return false;
    bool source = has(p / "personaje.toml");
    if (ec) return false;
    if (!source) source = has(p / "traje.toml");
    if (ec) return false;
    if (!source) continue;
    if (has(p / "us")) continue;
    if (ec) return false;
    if (has(p / "eu")) continue;
    if (ec) return false;
    bool manifest = false;
    for (fs::directory_iterator j(p, ec); !ec && j != end; j.increment(ec)) {
      const auto file = j->path().filename();
      const auto kind = j->symlink_status(ec).type();
      if (ec) return false;
      if (file == "personaje.toml" || file == "traje.toml") {
        if (kind != fs::file_type::regular) return false;
        manifest = true;
      } else if (kind != dir) {
        return false;
      }
    }
    if (ec || !manifest) return false;
    ++sources;
  }
  return !ec && sources >= 8;
}

constexpr bool PreviewMeshFits(bool expanded, uint32_t tris, size_t remaining) {
  const uint32_t limit = expanded ? 400000 : 200000;
  // HMV1 has 3 x 8 floats + 2 u32 per triangle, after the textures.
  return tris <= limit && (!expanded || tris <= remaining / 104);
}

constexpr bool PreviewDrawFits(bool expanded, bool offsets, size_t used, size_t pieces) {
  const size_t budget = offsets ? (expanded ? 480000 : 240000) : 42000;
  return used <= budget && pieces <= (budget - used) / 3;
}

}  // namespace dbz3::lim3
