// dbz3 - Update check against the project's GitHub releases.
//
// Inspired by Dusklight's prelaunch screen: the launcher asks GitHub once per
// run (on a background thread, never blocking the UI) and, when a newer release
// exists, shows the new version next to a one-click download. This is the
// cheapest way to stop users from reporting bugs that are already fixed in a
// build they never installed.

#pragma once

#include <filesystem>
#include <functional>
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

// true si `a` es una version estrictamente mas nueva que `b` ("1.2.10" > "1.2.9").
bool IsVersionNewer(const std::string& a, const std::string& b);

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

// HTTPS GET (short timeouts, background threads only). `headers` = extra request
// headers, each ending in CRLF. Returns the body of a 200 response, or "".
std::string HttpGet(const wchar_t* host, const wchar_t* path, const wchar_t* headers = nullptr);

// GET de una URL completa (matchmaking del online). https:// usa las mismas vias que HttpGet (3 de WinHTTP, luego curl);
// http:// (solo servidor de pruebas local) va con curl. Devuelve false si no llego al servidor o no fue un 200;
// un 200 sin cuerpo es true con body vacio. Solo hilos de fondo.
bool HttpGetUrl(const std::string& url, std::string& body);

// Motivo del ultimo fallo de red ("auto: error 12007, sistema: error 12029, curl: ...") o "" si la ultima
// peticion salio bien. Para el aviso del launcher y para que el jugador lo copie al pedir ayuda.
std::string LastNetworkError();

// HTTPS download straight to a file (streamed: zips of several GB never sit in RAM).
// Redirects are followed by hand and `headers` are only sent to the first host (GitHub's
// signed asset links reject the API token). `append` adds to the end of `out` (split zips).
// `progress(bytes)` reports what this call wrote so far. Returns the bytes written, or -1.
long long HttpDownload(const std::string& url, const std::wstring& headers,
                       const std::filesystem::path& out, bool append,
                       const std::function<void(long long)>& progress);

// Descarga con curl.exe del sistema (http/https, reintentos, `-C -`): si `out` ya tiene un trozo, sigue por ahi, tambien
// tras cerrar el launcher. `headers` como en HttpDownload (solo al primer host). Devuelve el tamano de `out` o -1.
long long HttpDownloadReanudable(const std::string& url, const std::string& headers, const std::filesystem::path& out,
                                 const std::function<void(long long)>& progress);

// Opens an https URL in the user's default browser (ShellExecute / xdg-open).
void OpenUrl(const std::string& url);

// Opens a folder in the file manager (explorer / xdg-open): one helper for every "Abrir carpeta" button.
void OpenFolder(const std::filesystem::path& dir);

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
