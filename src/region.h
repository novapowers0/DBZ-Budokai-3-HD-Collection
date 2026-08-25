// dbz3 - Region selection support (project-side, no SDK changes).
//
// The game hardcodes game:\us\... asset paths. The launcher selects a region
// (us/eu) and we mount a HostPathDevice at \Device\Harddisk0\Partition1\us so
// D:\us resolves to the chosen region's assets folder (game_dir/<region>),
// reading them directly without copying or duplicating anything. Built
// entirely against the public ReXGlue SDK API (mirrors the dbz1 approach).

#pragma once

#include <filesystem>

namespace dbz3 {

// Mounts game:\us (D:\us) to <game_data_root>/<region> for the currently
// selected dbz3_region cvar. Re-applies (unmounts + remounts) on each call so a
// launcher region change takes effect before the guest launches. game_data_root
// is the folder that directly contains the us/ and eu/ asset folders.
bool ApplyRegionMount();

// Effective game data root (the folder that directly contains us/ and eu/).
// Tracks the folder chosen at startup (OnConfigurePaths) and any later
// relocation from the launcher, so region mounting always uses the folder the
// user actually pointed at (the runtime's internal copy is stale after a
// runtime relocation).
std::filesystem::path EffectiveGameRoot();

// Sets the effective game data root (called from OnConfigurePaths).
void SetEffectiveGameRoot(const std::filesystem::path& root);

// Re-mounts the game drive (game:/d: -> \Device\Harddisk0\Partition1) at `root`
// and updates the effective game data root, so a folder picked in the launcher
// takes effect immediately (no restart needed; safe because the guest has not
// launched yet). Returns false if the runtime/VFS is not ready or the folder
// cannot be mounted.
bool RemountGameDrive(const std::filesystem::path& root);

// Relocates the game data to `root`: remounts the drive, updates the effective
// root and re-applies the region mount. Returns false on failure.
bool RelocateGameData(const std::filesystem::path& root);

}  // namespace dbz3
