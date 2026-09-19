#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::filesystem {

// Find the AFS entry containing the given byte offset. Returns the entry index
// (0-based) or -1 if the file is not an AFS / the offset is not inside an entry.
// On success fills out_entry_start/out_entry_size with the entry's data range.
int AfsFindEntry(const std::filesystem::path& host_path, uint64_t byte_offset,
                 uint64_t& out_entry_start, uint64_t& out_entry_size);

// Root folder where mods live (next to the executable, "mods").
std::filesystem::path AfsModsRoot();

// True when at least one enabled mod folder exists (cached scan). With no mods
// installed there is no override lookup and no virtual AFS table work to do on
// the read path, so callers can take a fast path (see HostPathFile::ReadSync).
bool AfsModsPresent();

// dbz3 - AFS I/O instrumentation (cvar `dbz3_io_logging`).
//
// Every guest read goes through HostPathFile::ReadSync, which is SYNCHRONOUS:
// the guest thread blocks for the whole physical read plus the per-read lookup
// work done in the host layer. Sampling both halves is the only way to tell a
// slow disk (read_ns high) from host-side overhead (pre_ns high) -- neither the
// game nor the emulator log any I/O timing otherwise. AfsIoRecordRead() feeds
// the counters; every 5 s one summary line is emitted and, for reads above
// `dbz3_io_slow_ms`, one line per slow read (capped per window).
struct AfsIoReadSample {
  const std::filesystem::path* host_path;  // container path; nullptr when unknown
  int entry_index;       // AFS entry index, or -1 when unknown
  uint64_t offset;       // byte offset inside the container
  uint64_t bytes;        // bytes requested
  uint64_t pre_ns;       // time resolving the read (lookup/override/virtual table)
  uint64_t read_ns;      // time inside the physical read
  bool from_cache;       // served by the readahead cache (no physical read)
};
void AfsIoRecordRead(const AfsIoReadSample& sample);

// Counts one host file open (an open storm on a slow disk is a red flag and is
// reported in the same summary line).
void AfsIoRecordOpen();

// Look for a mod-provided replacement for the given AFS entry. A mod is a
// subfolder under <exe>/mods/<mod>/us/<afs_filename>/<entry_index>. Returns true
// and fills out_path if a replacement exists.
bool AfsFindModOverride(const std::filesystem::path& host_path, int entry_index,
                        std::filesystem::path& out_path);

// Look for a mod-provided replacement of an ENTIRE file (not a single AFS
// entry): mods/<mod>/<filename>, mods/<mod>/us/<filename> or
// mods/<mod>/eu/<filename>. Returns true and fills out_path if found.
bool AfsFindModFileOverride(const std::filesystem::path& host_path,
                            std::filesystem::path& out_path);

// List mod folders under the mods root. Returns mod folder names; enabled ones
// first. Rescans the directory (used by the launcher UI).
std::vector<std::string> AfsListMods();

// Reset the mod cache so the next lookup rescans the mods folder (used by the
// launcher to refresh enable/disable without restarting the app).
void AfsResetModCache();

// Enable (enable=true) or disable a mod by name via a ".disabled" marker file.
void AfsSetModEnabled(const std::string& mod_name, bool enable);

// Map an EUR (PAL) asset filename to the US-style name the game expects. When
// running the EUR asset layout, the game looks for US names; this returns the
// name to present for a given real file. Returns the input if no mapping.
std::string AfsRegionFileName(const std::string& region, const std::string& real_name);

// Virtual mid-insert AFS table. Builds a consistent table where entries with a
// mod override larger than their physical slot grow in place and all following
// entries shift by the accumulated delta (exactly like a rebuilt AFS with
// mid-insert). Returns the size of the header+table region (8 + count*8) and
// fills out_vtable with the bytes to present to the guest. out_any_growth is
// set when at least one entry grew (so data reads need offset translation).
// Returns 0 if the file is not a parseable AFS.
size_t AfsGetVirtualTable(const std::filesystem::path& host_path,
                          std::vector<uint8_t>& out_vtable, bool& out_any_growth);

// Same as AfsGetVirtualTable but WITHOUT copying the bytes: returns a pointer to
// the cached table (nullptr when the file is not a parseable AFS). The pointer is
// valid until AfsResetModCache(). Used on the read path, which must not copy the
// whole table (thousands of reads per transition) just to check `any_growth`.
const std::vector<uint8_t>* AfsGetVirtualTableFast(const std::filesystem::path& host_path,
                                                   bool& out_any_growth);

// Translate a virtual file offset (as the guest sees it) to the physical offset
// inside the real AFS file, using the virtual mid-insert layout. Returns the
// entry index or -1 if the offset is not inside any entry (table region /
// padding). On success fills out_physical_offset and, if that entry has a mod
// override, out_override_path with the mod file and out_override_mod_offset with
// the offset inside the override file.
int AfsTranslateOffset(const std::filesystem::path& host_path, uint64_t virtual_offset,
                       uint64_t& out_physical_offset,
                       std::filesystem::path& out_override_path,
                       uint64_t& out_override_mod_offset);

// Resolve a run of contiguous bytes in the virtual mid-insert layout starting at
// the given (guest-visible) offset. Fills out_source_file with the file to read
// from (the real AFS or a mod override) and out_source_offset with the offset
// inside it, and out_run_length with how many contiguous bytes that source
// covers. An EMPTY out_source_file means the run is padding/gap/EOF and should
// be served as zeros. Returns false when the offset is past the virtual end of
// the AFS (everything after is zeros). The header+table region ([0, hdr_size))
// is reported as a zero-run (the caller serves the virtual table bytes itself).
bool AfsVirtualRange(const std::filesystem::path& host_path, uint64_t virtual_offset,
                     std::filesystem::path& out_source_file, uint64_t& out_source_offset,
                     uint64_t& out_run_length);

}  // namespace rex::filesystem
