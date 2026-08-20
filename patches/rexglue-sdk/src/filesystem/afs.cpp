// dbz1 - AFS (ADX File System) parsing and mod override manager.
//
// The game stores its data in AFS containers (data_XX.afs, adx_XX.afs) and
// references entries by position. This module parses the AFS header/table so we
// can map byte ranges to entry indices, and (with mods enabled) redirect reads
// of a specific entry to a file provided by a mod -- allowing model swaps and
// move-set replacements without repacking the .afs.
//
// Format (confirmed from community docs + binary inspection):
//   offset 0: "AFS" magic (3 bytes) + 1 padding byte
//   offset 4: entry count (uint32)
//   offset 8: table of (address uint32, size uint32) -- 8 bytes per entry
//   after table: optional name/metadata block (not needed for indexing)
// Entries are aligned to 0x800 and their data lives at [address, address+size).

#include <rex/filesystem/afs.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace rex::filesystem {

namespace {

constexpr char kAfsMagic[3] = {'A', 'F', 'S'};

// Parsed AFS index for one container file.
struct AfsFileIndex {
  std::filesystem::path host_path;
  uint32_t entry_count = 0;
  std::vector<uint64_t> entry_offsets;
  std::vector<uint64_t> entry_sizes;
};

std::mutex g_afs_index_mutex;
std::map<std::string, AfsFileIndex> g_afs_index_cache;

bool LoadAfsIndex(const std::filesystem::path& host_path, AfsFileIndex& out_index) {
  std::ifstream file(host_path, std::ios::binary);
  if (!file) {
    return false;
  }
  char magic[3];
  file.read(magic, 3);
  if (file.gcount() != 3 || std::memcmp(magic, kAfsMagic, 3) != 0) {
    return false;
  }
  file.seekg(1, std::ios::cur);  // padding byte
  uint32_t count = 0;
  file.read(reinterpret_cast<char*>(&count), 4);
  if (!file || count == 0 || count > 1u << 20) {
    return false;
  }
  out_index.host_path = host_path;
  out_index.entry_count = count;
  out_index.entry_offsets.resize(count);
  out_index.entry_sizes.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t addr = 0, size = 0;
    file.read(reinterpret_cast<char*>(&addr), 4);
    file.read(reinterpret_cast<char*>(&size), 4);
    if (!file) {
      return false;
    }
    out_index.entry_offsets[i] = addr;
    out_index.entry_sizes[i] = size;
  }
  return true;
}

const AfsFileIndex* GetOrLoadAfsIndex(const std::filesystem::path& host_path) {
  std::lock_guard<std::mutex> lock(g_afs_index_mutex);
  const std::string key = rex::path_to_utf8(host_path);
  auto it = g_afs_index_cache.find(key);
  if (it != g_afs_index_cache.end()) {
    return &it->second;
  }
  AfsFileIndex index;
  if (LoadAfsIndex(host_path, index)) {
    auto [inserted_it, _] = g_afs_index_cache.emplace(key, std::move(index));
    return &inserted_it->second;
  }
  return nullptr;
}

}  // namespace

// Map a byte offset within the .afs to an entry index, or -1 if not inside any
// entry. Returns the entry whose [address, address+size) contains the offset.
int AfsFindEntry(const std::filesystem::path& host_path, uint64_t byte_offset,
                 uint64_t& out_entry_start, uint64_t& out_entry_size) {
  const AfsFileIndex* index = GetOrLoadAfsIndex(host_path);
  if (!index) {
    return -1;
  }
  // Binary search for the last entry whose start address <= byte_offset.
  int lo = 0, hi = int(index->entry_count) - 1, found = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (index->entry_offsets[mid] <= byte_offset) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (found < 0) {
    return -1;
  }
  const uint64_t start = index->entry_offsets[found];
  const uint64_t size = index->entry_sizes[found];
  if (byte_offset < start + size) {
    out_entry_start = start;
    out_entry_size = size;
    return found;
  }
  return -1;
}

// Root folder where mods live (next to the executable, "mods").
std::filesystem::path AfsModsRoot() {
  return rex::filesystem::GetExecutableFolder() / "mods";
}

namespace {

// Cache of enabled mod directories, scanned once (sorted for priority).
std::mutex g_mod_dirs_mutex;
std::vector<std::filesystem::path> g_mod_dirs_cache;
bool g_mod_dirs_scanned = false;

void ScanModDirs() {
  std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
  if (g_mod_dirs_scanned) {
    return;
  }
  g_mod_dirs_cache.clear();
  const std::filesystem::path mods_root = AfsModsRoot();
  std::error_code ec;
  if (std::filesystem::is_directory(mods_root, ec)) {
    for (const auto& mod_entry : std::filesystem::directory_iterator(mods_root, ec)) {
      if (mod_entry.is_directory()) {
        // Disabled mods have a ".disabled" marker file.
        if (!std::filesystem::exists(mod_entry.path() / ".disabled")) {
          g_mod_dirs_cache.push_back(mod_entry.path());
        }
      }
    }
  }
  std::sort(g_mod_dirs_cache.begin(), g_mod_dirs_cache.end());
  g_mod_dirs_scanned = true;
}

}  // namespace

// Look for a mod-provided replacement for the given AFS entry. A mod is a
// subfolder under <exe>/mods/<mod>/us/<afs_filename>/<entry_index>. The first
// match (alphabetical mod order) wins. Returns true and fills out_path if found.
bool AfsFindModOverride(const std::filesystem::path& host_path, int entry_index,
                        std::filesystem::path& out_path) {
  const std::string afs_name = host_path.filename().string();
  const std::string entry_name = std::to_string(entry_index);
  ScanModDirs();
  std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
  REXLOG_INFO("AFS OVERRIDE LOOKUP: afs={} entry={} host={}", afs_name, entry_name,
              rex::path_to_utf8(host_path));
  for (const auto& mod_dir : g_mod_dirs_cache) {
    auto candidate = mod_dir / "us" / afs_name / entry_name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      // Plain-file form: mods/<mod>/us/<afs>/<entry_index>
      REXLOG_INFO("AFS OVERRIDE HIT: {}", rex::path_to_utf8(candidate));
      out_path = candidate;
      return true;
    }
    if (std::filesystem::is_directory(candidate, ec)) {
      // Folder form: mods/<mod>/us/<afs>/<entry_index>/<file>. Use the first
      // regular file inside (lexicographic order).
      for (const auto& mod_file : std::filesystem::directory_iterator(candidate, ec)) {
        if (mod_file.is_regular_file()) {
          REXLOG_INFO("AFS OVERRIDE HIT (folder): {}", rex::path_to_utf8(mod_file.path()));
          out_path = mod_file.path();
          return true;
        }
      }
    }
  }
  REXLOG_INFO("AFS OVERRIDE MISS: mods_cache={} entries:",
              std::to_string(g_mod_dirs_cache.size()));
  for (const auto& mod_dir : g_mod_dirs_cache) {
    REXLOG_INFO("  mod_dir={}", rex::path_to_utf8(mod_dir));
  }
  return false;
}

// Look for a mod-provided replacement of an ENTIRE file (not a single AFS
// entry). A mod is a subfolder under <exe>/mods/<mod>/<filename> (or the region
// subfolders mods/<mod>/us/<filename> / mods/<mod>/eu/<filename>). The first
// match (alphabetical mod order) wins. Returns true and fills out_path if found.
bool AfsFindModFileOverride(const std::filesystem::path& host_path,
                            std::filesystem::path& out_path) {
  const std::string file_name = host_path.filename().string();
  ScanModDirs();
  std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
  for (const auto& mod_dir : g_mod_dirs_cache) {
    std::error_code ec;
    // Preferred: mods/<mod>/<filename>
    auto candidate = mod_dir / file_name;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out_path = candidate;
      return true;
    }
    // Region subfolders: mods/<mod>/us/<filename> and mods/<mod>/eu/<filename>.
    candidate = mod_dir / "us" / file_name;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out_path = candidate;
      return true;
    }
    candidate = mod_dir / "eu" / file_name;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out_path = candidate;
      return true;
    }
  }
  return false;
}

namespace {

// Virtual mid-insert AFS layout, cached per container. Builds a consistent
// table where entries with an override larger than their physical slot grow in
// place and later entries shift by the accumulated delta -- exactly like a
// rebuilt AFS (mid-insert). Data reads are then translated back to the physical
// file (or served from the override file).
struct VirtualAfsLayout {
  uint32_t entry_count = 0;
  std::vector<uint64_t> virt_addrs;  // address the guest sees (shifted)
  std::vector<uint64_t> virt_sizes;  // size the guest sees (grown entries)
  std::vector<uint64_t> phys_addrs;  // real address in the AFS file
  std::vector<uint64_t> delta;       // virt - phys per entry
  std::vector<uint8_t> table_bytes;  // "AFS" + count + virtual table
  bool any_growth = false;           // at least one entry grew (needs translation)
};
std::mutex g_vafs_mutex;
std::map<std::string, VirtualAfsLayout> g_vafs_cache;

// Non-logging override lookup (used by the virtual table builder, which probes
// every entry of the AFS). g_mod_dirs_mutex must be held.
bool FindModOverrideQuiet(const std::filesystem::path& host_path, int entry_index,
                          std::filesystem::path& out_path) {
  const std::string afs_name = host_path.filename().string();
  const std::string entry_name = std::to_string(entry_index);
  for (const auto& mod_dir : g_mod_dirs_cache) {
    auto candidate = mod_dir / "us" / afs_name / entry_name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out_path = candidate;
      return true;
    }
    if (std::filesystem::is_directory(candidate, ec)) {
      for (const auto& mod_file : std::filesystem::directory_iterator(candidate, ec)) {
        if (mod_file.is_regular_file()) {
          out_path = mod_file.path();
          return true;
        }
      }
    }
  }
  return false;
}

const VirtualAfsLayout* GetOrLoadVirtualAfs(const std::filesystem::path& host_path) {
  std::lock_guard<std::mutex> lock(g_vafs_mutex);
  const std::string key = rex::path_to_utf8(host_path);
  auto it = g_vafs_cache.find(key);
  if (it != g_vafs_cache.end()) {
    return &it->second;
  }
  const AfsFileIndex* index = GetOrLoadAfsIndex(host_path);
  if (!index) {
    return nullptr;
  }
  VirtualAfsLayout layout;
  layout.entry_count = index->entry_count;
  const size_t count = index->entry_count;
  layout.virt_addrs.resize(count);
  layout.virt_sizes.resize(count);
  layout.phys_addrs.resize(count);
  layout.delta.resize(count);

  // Build the virtual table: for each entry, if it has an override file larger
  // than the physical slot, the entry grows to the override size (aligned to
  // the AFS slot granularity 0x800) and the accumulated delta shifts all later
  // entries. This mirrors exactly a mid-insert AFS rebuild.
  ScanModDirs();
  std::lock_guard<std::mutex> lock_mods(g_mod_dirs_mutex);
  uint64_t acc_delta = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t phys_addr = index->entry_offsets[i];
    const uint64_t phys_size = index->entry_sizes[i];
    // Guest read size: the guest allocates ceil(size/0x1000)*0x1000 for each
    // entry (its buffer for the bin). A mod bin fits without growing as long as
    // it does not exceed this to_read. Physical slot length (distance to the
    // next entry) is only used to compute the shift when the entry must grow.
    const uint64_t next_phys =
        (i + 1 < count) ? index->entry_offsets[i + 1] : phys_addr + phys_size;
    const uint64_t slot_len = next_phys - phys_addr;
    const uint64_t to_read = (phys_size + 0xFFF) & ~uint64_t(0xFFF);

    layout.phys_addrs[i] = phys_addr;
    layout.virt_addrs[i] = phys_addr + acc_delta;
    layout.delta[i] = acc_delta;
    uint64_t virt_size = phys_size;

    std::filesystem::path mod_path;
    if (FindModOverrideQuiet(host_path, int(i), mod_path)) {
      std::error_code ec;
      const uint64_t fsz = std::filesystem::file_size(mod_path, ec);
      if (!ec && fsz > to_read) {
        // The override bin is larger than what the guest would allocate for the
        // original entry: grow in place (align the new slot to 0x800 like the
        // AFS) and shift all later entries by the delta.
        const uint64_t grown_slot = (fsz + 0x7FF) & ~uint64_t(0x7FF);
        virt_size = fsz;
        acc_delta += grown_slot - slot_len;
        layout.any_growth = true;
      }
    }
    layout.virt_sizes[i] = virt_size;
  }

  // Serialize the virtual header+table.
  const size_t hdr = 8;
  const size_t tbl = size_t(count) * 8;
  layout.table_bytes.assign(hdr + tbl, 0);
  layout.table_bytes[0] = 'A';
  layout.table_bytes[1] = 'F';
  layout.table_bytes[2] = 'S';
  layout.table_bytes[3] = 0;
  std::memcpy(&layout.table_bytes[4], &count, 4);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t addr = static_cast<uint32_t>(layout.virt_addrs[i]);
    uint32_t size = static_cast<uint32_t>(layout.virt_sizes[i]);
    std::memcpy(&layout.table_bytes[hdr + i * 8], &addr, 4);
    std::memcpy(&layout.table_bytes[hdr + i * 8 + 4], &size, 4);
  }

  auto [inserted_it, _] = g_vafs_cache.emplace(key, std::move(layout));
  return &inserted_it->second;
}

}  // namespace

// Virtual mid-insert AFS table (public API). See header.
size_t AfsGetVirtualTable(const std::filesystem::path& host_path,
                          std::vector<uint8_t>& out_vtable, bool& out_any_growth) {
  const VirtualAfsLayout* layout = GetOrLoadVirtualAfs(host_path);
  if (!layout) {
    return 0;
  }
  out_vtable = layout->table_bytes;
  out_any_growth = layout->any_growth;
  return layout->table_bytes.size();
}

// Translate a virtual offset back to the physical file (or to a mod override).
// See header.
int AfsTranslateOffset(const std::filesystem::path& host_path, uint64_t virtual_offset,
                       uint64_t& out_physical_offset,
                       std::filesystem::path& out_override_path,
                       uint64_t& out_override_mod_offset) {
  const VirtualAfsLayout* layout = GetOrLoadVirtualAfs(host_path);
  if (!layout) {
    return -1;
  }
  out_override_path.clear();
  out_override_mod_offset = 0;

  // Find the entry whose [virt_addr, virt_addr + virt_size) contains the offset
  // (binary search on the sorted virtual addresses).
  int lo = 0, hi = int(layout->entry_count) - 1, found = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (layout->virt_addrs[mid] <= virtual_offset) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (found < 0) {
    return -1;
  }
  const uint64_t start = layout->virt_addrs[found];
  const uint64_t size = layout->virt_sizes[found];
  if (virtual_offset >= start + size) {
    return -1;
  }
  out_physical_offset = virtual_offset - layout->delta[found];

  // If the entry has a mod override, return the mod file path.
  const std::string afs_name = host_path.filename().string();
  const std::string entry_name = std::to_string(found);
  ScanModDirs();
  std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
  for (const auto& mod_dir : g_mod_dirs_cache) {
    auto candidate = mod_dir / "us" / afs_name / entry_name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out_override_path = candidate;
      out_override_mod_offset = virtual_offset - start;
      return found;
    }
    if (std::filesystem::is_directory(candidate, ec)) {
      for (const auto& mod_file : std::filesystem::directory_iterator(candidate, ec)) {
        if (mod_file.is_regular_file()) {
          out_override_path = mod_file.path();
          out_override_mod_offset = virtual_offset - start;
          return found;
        }
      }
    }
  }
  return found;
}

// List mod folders under the mods root. Returns enabled mods first.
std::vector<std::string> AfsListMods() {
  std::vector<std::string> result;
  std::error_code ec;
  const std::filesystem::path mods_root = AfsModsRoot();
  if (!std::filesystem::is_directory(mods_root, ec)) {
    return result;
  }
  std::vector<std::string> enabled, disabled;
  for (const auto& mod_entry : std::filesystem::directory_iterator(mods_root, ec)) {
    if (!mod_entry.is_directory()) {
      continue;
    }
    const std::string name = rex::path_to_utf8(mod_entry.path().filename());
    if (std::filesystem::exists(mod_entry.path() / ".disabled")) {
      disabled.push_back(name);
    } else {
      enabled.push_back(name);
    }
  }
  std::sort(enabled.begin(), enabled.end());
  std::sort(disabled.begin(), disabled.end());
  result.insert(result.end(), enabled.begin(), enabled.end());
  result.insert(result.end(), disabled.begin(), disabled.end());
  return result;
}

void AfsSetModEnabled(const std::string& mod_name, bool enable) {
  const std::filesystem::path mod_dir = AfsModsRoot() / mod_name;
  const std::filesystem::path marker = mod_dir / ".disabled";
  std::error_code ec;
  if (enable) {
    std::filesystem::remove(marker, ec);
  } else {
    std::filesystem::create_directories(mod_dir, ec);
    std::ofstream marker_file(marker);
    marker_file << "disabled\n";
  }
  // Force a rescan on next lookup.
  {
    std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
    g_mod_dirs_scanned = false;
  }
}

void AfsResetModCache() {
  // Force rescan: clear index cache and mod cache so the next file open /
  // launcher refresh picks up new mods and enable/disable changes.
  {
    std::lock_guard<std::mutex> lock(g_mod_dirs_mutex);
    g_mod_dirs_scanned = false;
    g_mod_dirs_cache.clear();
  }
  {
    std::lock_guard<std::mutex> lock(g_afs_index_mutex);
    g_afs_index_cache.clear();
  }
  {
    std::lock_guard<std::mutex> lock(g_vafs_mutex);
    g_vafs_cache.clear();
  }
}

// Map an EUR (PAL) asset filename to the name the game expects. The game's
// language filename for English is "data_us.afs" in the US layout but
// "data_en.afs" in the EUR (PAL) layout; all other language files keep their
// names (data_fr, data_sp, data_ge, data_it, data_yah, adx_jp). Returns the
// presented name, or the original if no mapping applies.
std::string AfsRegionFileName(const std::string& region, const std::string& real_name) {
  if (region != "eur") {
    return real_name;
  }
  if (real_name == "data_en.afs") {
    return "data_us.afs";
  }
  return real_name;
}

}  // namespace rex::filesystem
