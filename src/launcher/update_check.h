// dbz3 - Update check against the project's GitHub releases.
//
// Inspired by Dusklight's prelaunch screen: the launcher asks GitHub once per
// run (on a background thread, never blocking the UI) and, when a newer release
// exists, shows the new version next to a one-click download. This is the
// cheapest way to stop users from reporting bugs that are already fixed in a
// build they never installed.

#pragma once

#include <string>
#include <vector>

namespace dbz3::launcher {

enum class UpdateState {
  kChecking,   // request in flight (or not started yet)
  kUpToDate,   // the latest release is the running version
  kAvailable,  // a newer release exists
  kFailed,     // offline / rate limited / parse error (never blocks the UI)
};

// Running build version as "1.2.3" (or "1.2.4.1" for an EX repack). Read from
// this executable's VERSIONINFO, so src/version.rc stays the single source of
// truth (no duplicated constant).
std::string CurrentVersion();

// Same version, in the form the release tags use: "1.2.3", "1.2.4 EX" (a local
// build number above zero means this is a repack of that release). For display.
std::string CurrentVersionLabel();
// Starts the automatic check. Idempotent: safe to call every frame, only the
// first call does anything. The HTTP request runs on a detached background
// thread.
void StartUpdateCheck();

// Re-runs the check on demand (the "Check for updates" button). Ignored while a
// request is still in flight; never blocks the UI.
void RequestUpdateCheck();

// Thread-safe snapshot of the check state.
UpdateState GetUpdateState();

// Latest release version ("1.2.4", no leading 'v') and its page URL. Only
// meaningful when GetUpdateState() == kAvailable.
std::string LatestVersion();
std::string LatestReleaseUrl();

// Opens a URL in the user's default browser (no-op off Windows).
void OpenUrl(const std::string& url);

// --- Installed-file consistency --------------------------------------------
// Every release replaces dbz3.exe AND the runtime DLLs together. Users routinely
// update by dropping a new exe (or a new DLL) on top of an old folder, and then
// report behaviour that belongs to a build nobody can identify -- which is
// exactly what happened in the logs that motivated this check (the game folder
// was v1.2.1 while the DLLs were already newer). Reading the VERSIONINFO of each
// component makes a mixed install visible in the log and in the launcher, so a
// future report always names the exact build that produced it.
struct InstalledComponent {
  std::string file_name;  // "dbz3.exe", "rexgpu-xenos.dll", ...
  std::string version;    // "1.2.9.0" / the published build stamp, or empty
  // Whether this component has to be from the same release as the executable.
  // The AMD FidelityFX DLL is third-party and carries its own versioning, so it
  // is listed but never compared.
  bool must_match = true;
};

// Versions of the executable and the runtime components next to it, in a fixed
// order. The probe (file reads + VERSIONINFO) runs once and is cached; the first
// call also writes one `dbz3: entorno ...` line (OS + RAM + versions) to the log
// and a warning when the components do not match.
const std::vector<InstalledComponent>& InstalledComponents();

// Relative path of the file that does not match the executable, empty when the
// install is consistent (or when a version could not be read: never warn about
// missing data). The comparison uses major.minor.patch, so a repack build
// number (1.2.4.1 vs 1.2.4.0) is not a mismatch.
std::string InstalledVersionMismatch();

}  // namespace dbz3::launcher
