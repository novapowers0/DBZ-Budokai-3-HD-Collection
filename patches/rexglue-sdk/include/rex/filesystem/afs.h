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

}  // namespace rex::filesystem
