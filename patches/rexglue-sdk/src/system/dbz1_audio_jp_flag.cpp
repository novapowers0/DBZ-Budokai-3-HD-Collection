// dbz1 - Audio language flag living in the shared rexruntime.dll.
//
// The game plays the English voice/music pack (adx_us.afs). When the user wants
// the Japanese audio, the runtime overrides game:\us\adx_us.afs to point at the
// Japanese pack (adx_jp.afs) so the game plays Japanese voices/music without
// needing to know the file name. Defined here so both the launcher and the
// runtime share the same storage.

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(dbz1_audio_jp, false, "DBZ1/Audio",
                    "Use the Japanese voice/music pack (adx_jp.afs) instead of English")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
