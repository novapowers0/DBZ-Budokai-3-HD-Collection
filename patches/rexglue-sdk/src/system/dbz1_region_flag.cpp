// dbz1 - Region flag living in the shared rexruntime.dll.
//
// The game hardcodes the "us" subfolder in its asset paths (it opens
// game:\us\data_*.afs). The launcher selects a region (us/eur) and the runtime
// mounts a device overriding game:\us to point at the chosen region's assets.
// Defined here in rexruntime.dll so both the launcher and the runtime share the
// same storage.

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(dbz1_region, "us", "DBZ1/Video",
                      "Game region assets folder: us or eur")
    .allowed({"us", "eur"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
