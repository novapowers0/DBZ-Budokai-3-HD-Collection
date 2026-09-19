// dbz3 - Update check against the project's GitHub releases.
//
// Inspired by Dusklight's prelaunch screen: the launcher asks GitHub once per
// run (on a background thread, never blocking the UI) and, when a newer release
// exists, shows the new version next to a one-click download. This is the
// cheapest way to stop users from reporting bugs that are already fixed in a
// build they never installed.

#pragma once

#include <string>

namespace dbz3::launcher {

enum class UpdateState {
  kChecking,   // request in flight (or not started yet)
  kUpToDate,   // the latest release is the running version
  kAvailable,  // a newer release exists
  kFailed,     // offline / rate limited / parse error (never blocks the UI)
};

// Running build version as "1.2.3". Read from this executable's VERSIONINFO, so
// src/version.rc stays the single source of truth (no duplicated constant).
std::string CurrentVersion();

// Starts the check. Idempotent: safe to call every frame. The HTTP request runs
// on a detached background thread.
void StartUpdateCheck();

// Thread-safe snapshot of the check state.
UpdateState GetUpdateState();

// Latest release version ("1.2.4", no leading 'v') and its page URL. Only
// meaningful when GetUpdateState() == kAvailable.
std::string LatestVersion();
std::string LatestReleaseUrl();

// Opens a URL in the user's default browser (no-op off Windows).
void OpenUrl(const std::string& url);

}  // namespace dbz3::launcher
