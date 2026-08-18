// dbz3 - Mod manager (project-side, no SDK changes).
//
// Manages the "mods" folder next to the project assets. A mod is a subfolder
// that replaces game files by providing whole-file copies (e.g. a repacked
// .afs). The launcher lists mods and toggles enable/disable via a ".disabled"
// marker. Whole-file overrides are resolved by the SDK's VFS layer.
//
// Formato (igual que el proyecto hermano DBZ Budokai HD):
//   mods/<name>/<region>/file...     -> override de assets
//   mods/<name>/manifest.txt         -> metadata key=value:
//        name, description, author, version, type, source, target

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbz3 {

// Root folder where mods live (project root "mods", next to the assets).
std::filesystem::path ModsRoot();

// One mod as seen by the launcher. `enabled` reflects the effective state
// after normalizing both disable conventions (folder named "foo.disabled" OR
// a ".disabled" marker file inside the folder).
struct ModInfo {
  std::string name;
  bool enabled = true;
  // Optional metadata from a "manifest.txt" inside the mod folder (key=value
  // lines). Empty fields fall back to the folder name / inferred type.
  std::string display_name;
  std::string description;
  std::string author;
  std::string version;
  std::string type;     // e.g. "swap_b3", "port_b3", "audio", "moveset"
  std::string source;   // source character/bin (e.g. "Goten (bin 298)")
  std::string target;   // target slot (e.g. "Krillin (326)")
  // Human-readable file count inside the mod (0 when empty).
  int file_count = 0;
};

// List mod folders under the mods root. Enabled mods first, then disabled.
std::vector<ModInfo> ListMods();

// Enable/disable a mod (creates/removes the ".disabled" marker).
void SetModEnabled(const std::string& mod_name, bool enable);

// Reads the raw manifest key for a mod ("" if the key is missing). Keys:
// name, description, author, version, type, source, target.
std::string GetModManifestValue(const std::string& mod_name,
                                const std::string& key);

// Sets a manifest key for a mod, creating/updating manifest.txt in the mod
// folder. Empty value removes the line. Returns false if not writable.
bool SetModManifestValue(const std::string& mod_name, const std::string& key,
                         const std::string& value);

// Infer/return a human-readable label for a mod type.
const char* ModTypeLabel(const std::string& type);

// Return a color for a mod type badge (0xRGB packed, negative = dim gray).
int ModTypeColor(const std::string& type);

}  // namespace dbz3