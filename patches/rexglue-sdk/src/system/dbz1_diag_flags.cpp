// dbz1 - Diagnostic flag living in the shared rexruntime.dll.
//
// This flag is shared by the app executable and the GPU plugin DLL. It is
// defined here (rexruntime.dll) so both binaries access the SAME storage via
// the exported accessor FLAGS_dbz1_diag_logging_storage_(). A cvar defined
// only in the exe's own rexcore copy is invisible to the plugin's registry,
// so the per-frame GPU diagnostic toggle must live in the shared runtime.
//
// NOTE: because it is defined in rexruntime.dll (not in the exe), the exe's
// SaveConfig cannot persist it to dbz1_user.toml; it resets to false on every
// boot. That is intentional for a debug diagnostic toggle (use the F10 dev
// overlay to enable it per session).

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(dbz1_diag_logging, false, "DBZ1/Dev",
                    "Enable per-frame GPU diagnostic logging (dbz1_gpu_diag.log)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
