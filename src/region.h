// dbz3 - Region selection support (project-side, no SDK changes).
//
// The game hardcodes game:\us\... asset paths. Two modes:
//   - folder mode: mounts a HostPathDevice at \Device\Harddisk0\Partition1\us so
//     D:\us resolves to the chosen region's assets folder (game_dir/<region>).
//   - ISO mode: mounts the disc image (GDFX) as the whole game drive and remaps
//     us\ -> eu\ inside the device, so no region shadowing can occur.
// Built entirely against the public ReXGlue SDK API.

#pragma once

#include <filesystem>
#include <string>

namespace dbz3 {

// Mounts game:\us (D:\us) to <game_data_root>/<region> for the currently
// selected dbz3_region cvar (folder mode), or remaps us\ -> eu\ inside the ISO
// device (ISO mode). Re-applies (unmounts + remounts) on each call so a
// launcher region change takes effect before the guest launches.
bool ApplyRegionMount();

// Effective game data root (folder mode: the folder that directly contains us/
// and eu/; ISO mode: the ISO cache folder holding the extracted default.xex).
// Tracks the source chosen at startup (OnConfigurePaths) and any later
// relocation from the launcher, so region mounting always uses the source the
// user actually pointed at (the runtime's internal copy is stale after a
// runtime relocation).
std::filesystem::path EffectiveGameRoot();

// Sets the effective game data root (called from OnConfigurePaths).
void SetEffectiveGameRoot(const std::filesystem::path& root);

// Re-mounts the game drive (game:/d: -> \Device\Harddisk0\Partition1) at `root`
// (folder mode) or from the active disc image (ISO mode, dbz3_iso_path) and
// updates the effective game data root, so a source picked in the launcher
// takes effect immediately (no restart needed; safe because the guest has not
// launched yet). Returns false if the runtime/VFS is not ready or the source
// cannot be mounted.
bool RemountGameDrive(const std::filesystem::path& root);

// Relocates the game data to `root`: remounts the drive, updates the effective
// root and re-applies the region mount. Returns false on failure.
bool RelocateGameData(const std::filesystem::path& root);

}  // namespace dbz3
