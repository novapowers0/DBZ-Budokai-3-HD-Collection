// dbz3 - Region selection support (project-side, no SDK changes).
//
// The game hardcodes game:\us\... asset paths. The launcher selects a region
// (us/eu) and we mount a HostPathDevice at \Device\Harddisk0\Partition1\us so
// D:\us resolves to the chosen region's assets folder (game_dir/<region>),
// reading them directly without copying or duplicating anything. Built
// entirely against the public ReXGlue SDK API (mirrors the dbz1 approach).

#pragma once

namespace dbz3 {

// Mounts game:\us (D:\us) to <game_data_root>/<region> for the currently
// selected dbz3_region cvar. Re-applies (unmounts + remounts) on each call so a
// launcher region change takes effect before the guest launches. game_data_root
// is the folder that directly contains the us/ and eu/ asset folders.
bool ApplyRegionMount();

}  // namespace dbz3
