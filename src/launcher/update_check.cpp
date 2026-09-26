// dbz3 - Update check implementation (GitHub releases API).

#include "update_check.h"

#include <rex/cvar.h>  // REX_PLATFORM_WIN32 (via rex/platform.h)
#include <rex/logging.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "version.lib")
#endif

namespace dbz3::launcher {

namespace {

// Repository to check. Kept in sync with tools/make_release.ps1.
constexpr const wchar_t* kApiHost = L"api.github.com";
constexpr const wchar_t* kApiPath =
    L"/repos/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest";

std::atomic<UpdateState> g_state{UpdateState::kChecking};
std::atomic<bool> g_autostarted{false};  // the automatic check ran at least once
std::atomic<bool> g_inflight{false};     // an HTTP request is in flight
std::mutex g_mutex;
std::string g_latest_version;
std::string g_latest_url;

// "v1.2.3" -> "1.2.3"
std::string StripTag(const std::string& tag) {
  if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
    return tag.substr(1);
  }
  return tag;
}

// Minimal extraction of a top-level JSON string field. The GitHub release
// payload is large, but `tag_name` / `html_url` appear before any nested object
// that could contain another field with the same name, and the API's own
// formatting is stable in practice. A full JSON parser would be overkill here.
std::string JsonStringField(const std::string& body, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) {
    return {};
  }
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) {
    return {};
  }
  p = body.find('"', p + 1);
  if (p == std::string::npos) {
    return {};
  }
  const size_t end = body.find('"', p + 1);
  if (end == std::string::npos) {
    return {};
  }
  return body.substr(p + 1, end - p - 1);
}

// Version with an optional "repack" marker. A release tag like "1.2.4-EX"
// names the version it is a repack of, so only its release number is
// comparable; the local FileVersion carries the extra build number instead
// (1.2.4.1). Both forms mean the same thing: this is a repack of 1.2.4.
struct Version {
  int parts[4] = {0, 0, 0, 0};
  bool suffix = false;  // a tag suffix right after the numbers ("-EX")
};

Version ParseVersion(const std::string& s) {
  Version v;
  std::sscanf(s.c_str(), "%d.%d.%d.%d", &v.parts[0], &v.parts[1], &v.parts[2], &v.parts[3]);
  size_t i = 0;
  while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
    ++i;
  }
  v.suffix = i > 0 && i < s.size();
  return v;
}

bool IsRepack(const Version& v) { return v.suffix || v.parts[3] > 0; }

// True when `a` is a strictly newer version than `b` ("1.2.10" > "1.2.9").
// Repacks rank above the plain release they are based on (1.2.4-EX > 1.2.4) but
// never above the next release (1.2.4-EX < 1.2.5) and not above the repack build
// itself (a 1.2.4 EX install comparing against the v1.2.4-EX tag is up to date).
bool VersionNewer(const std::string& a, const std::string& b) {
  const Version va = ParseVersion(a);
  const Version vb = ParseVersion(b);
  const int count = (va.suffix || vb.suffix) ? 3 : 4;
  for (int i = 0; i < count; ++i) {
    if (va.parts[i] != vb.parts[i]) {
      return va.parts[i] > vb.parts[i];
    }
  }
  return IsRepack(va) && !IsRepack(vb);
}

#if REX_PLATFORM_WIN32

// HTTPS GET with short timeouts, so a stalled network can never keep a thread
// (or the launcher) waiting. Returns the body, or an empty string on any error.
std::string HttpGet(const wchar_t* host, const wchar_t* path) {
  std::string body;
  HINTERNET session = WinHttpOpen(L"dbz3-update-check/1.0",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    return body;
  }
  WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
  HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (connect) {
    // GitHub rejects API requests without a User-Agent (WinHttpOpen sets one).
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request) {
      if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                             0, 0, 0) &&
          WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD status_len = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
          for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
              break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
              break;
            }
            chunk.resize(read);
            body += chunk;
          }
        }
      }
      WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return body;
}

#else

std::string HttpGet(const wchar_t*, const wchar_t*) { return {}; }

#endif  // REX_PLATFORM_WIN32

}  // namespace

std::string CurrentVersion() {
#if REX_PLATFORM_WIN32
  wchar_t path[MAX_PATH] = {};
  if (GetModuleFileNameW(GetModuleHandleW(nullptr), path, MAX_PATH)) {
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &unused);
    if (size) {
      std::vector<uint8_t> data(size);
      if (GetFileVersionInfoW(path, 0, size, data.data())) {
        VS_FIXEDFILEINFO* info = nullptr;
        UINT len = 0;
        if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) && info) {
          char buf[64];
          std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", HIWORD(info->dwFileVersionMS),
                        LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
                        LOWORD(info->dwFileVersionLS));
          return buf;
        }
      }
    }
  }
#endif
  return {};
}

std::string CurrentVersionLabel() {
  const std::string raw = CurrentVersion();
  if (raw.empty()) {
    return {};
  }
  const Version v = ParseVersion(raw);
  char buf[64];
  if (v.parts[3] > 0) {
    std::snprintf(buf, sizeof(buf), "%d.%d.%d EX", v.parts[0], v.parts[1], v.parts[2]);
  } else {
    std::snprintf(buf, sizeof(buf), "%d.%d.%d", v.parts[0], v.parts[1], v.parts[2]);
  }
  return buf;
}

void StartUpdateCheck() {
  if (g_autostarted.exchange(true)) {
    return;
  }
  RequestUpdateCheck();
}

void RequestUpdateCheck() {
  if (g_inflight.exchange(true)) {
    return;  // a request is already on its way; keep the current state
  }
  g_state.store(UpdateState::kChecking, std::memory_order_release);
  std::thread([]() {
    const std::string body = HttpGet(kApiHost, kApiPath);
    const std::string tag = JsonStringField(body, "tag_name");
    const std::string url = JsonStringField(body, "html_url");
    if (tag.empty() || url.empty()) {
      g_state.store(UpdateState::kFailed, std::memory_order_release);
      g_inflight.store(false, std::memory_order_release);
      return;
    }
    const std::string latest = StripTag(tag);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_latest_version = latest;
      g_latest_url = url;
    }
    const std::string current = CurrentVersion();
    const bool newer = current.empty() || VersionNewer(latest, current);
    g_state.store(newer ? UpdateState::kAvailable : UpdateState::kUpToDate,
                  std::memory_order_release);
    g_inflight.store(false, std::memory_order_release);
  }).detach();
}

UpdateState GetUpdateState() {
  return g_state.load(std::memory_order_acquire);
}

std::string LatestVersion() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_latest_version;
}

std::string LatestReleaseUrl() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_latest_url;
}
// --- Installed-file consistency ---------------------------------------------
// See the header for why: a user who updates by copying only some of the files
// ends up with a build that cannot be identified from its log, and the report
// becomes unactionable. Reading each component's VERSIONINFO (once, cached)
// makes a mixed install obvious both in the log and on screen.

namespace {

#if REX_PLATFORM_WIN32

std::string FileVersionString(const std::wstring& path) {
  DWORD unused = 0;
  const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
  if (size == 0) {
    return {};
  }
  std::vector<uint8_t> data(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
    return {};
  }
  VS_FIXEDFILEINFO* info = nullptr;
  UINT len = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) {
    return {};
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", HIWORD(info->dwFileVersionMS),
                LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
                LOWORD(info->dwFileVersionLS));
  return buf;
}

// Directory of the running executable (where the launcher and the DLLs live).
std::wstring ExeDirectory() {
  wchar_t path[MAX_PATH] = {};
  if (!GetModuleFileNameW(GetModuleHandleW(nullptr), path, MAX_PATH)) {
    return {};
  }
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    dir.resize(slash + 1);
  }
  return dir;
}

// VERSIONINFO of a file that sits next to the executable (empty when the file
// has no version resource, which is the case for the ReXGlue runtime DLLs).
std::string DllFileVersion(const char* file_name) {
  const int needed = MultiByteToWideChar(CP_UTF8, 0, file_name, -1, nullptr, 0);
  std::wstring wide(needed > 0 ? size_t(needed) : 0, L'\0');
  if (needed > 0) {
    MultiByteToWideChar(CP_UTF8, 0, file_name, -1, wide.data(), needed);
    wide.resize(size_t(needed - 1));
  }
  return FileVersionString(ExeDirectory() + wide);
}

// Real OS version. GetVersionExW lies unless the process is manifested for the// newest Windows, so RtlGetVersion (ntdll) is used instead -- it is what a
// support log needs to be useful.
std::string OsVersionString() {
  struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform_id;
    wchar_t csd[128];
  };
  using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
  OsVersionInfo info = {};
  info.size = sizeof(info);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto rtl_get_version =
      ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  if (!rtl_get_version || rtl_get_version(&info) != 0) {
    return "?";
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%lu.%lu.%lu", info.major, info.minor, info.build);
  return buf;
}

uint64_t TotalRamMb() {
  MEMORYSTATUSEX status = {};
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status)) {
    return 0;
  }
  return status.ullTotalPhys >> 20;
}

#else

std::string DllFileVersion(const char*) { return {}; }

#endif  // REX_PLATFORM_WIN32

// Name of the component that does not match the executable, filled by the probe
// inside InstalledComponents() (empty when the install is consistent).
std::string g_component_mismatch;

// Logs one line with everything a support report needs to be actionable without
// a second round trip (OS, RAM, versions of every installed component) plus a
// warning when they do not match.
void LogEnvironmentOnce(const std::vector<InstalledComponent>& components,
                        const std::string& mismatch) {
  std::string line = "dbz3: entorno os=";
#if REX_PLATFORM_WIN32
  line += OsVersionString();
  const uint64_t ram_mb = TotalRamMb();
  if (ram_mb) {
    line += " ram=" + std::to_string(ram_mb) + "MB";
  }
#else
  line += "?";
#endif
  for (const InstalledComponent& component : components) {
    line += " " + component.file_name + "=";
    line += component.version.empty() ? "?" : component.version;
  }
  REXLOG_INFO("{}", line);
  if (!mismatch.empty()) {
    REXLOG_WARN(
        "dbz3: aviso - instalacion mixta: {0} no coincide con dbz3.exe (o es de una version "
        "que no se puede identificar) - al actualizar hay que reemplazar TODOS los ficheros: "
        "descomprime el zip completo en una carpeta nueva (o copia el exe Y las DLLs)",
        mismatch);
  }
}

}  // namespace

const std::vector<InstalledComponent>& InstalledComponents() {
  static std::vector<InstalledComponent> components;
  static bool probed = false;
  if (probed) {
    return components;
  }
  probed = true;
  // The runtime DLLs carry no VERSIONINFO, so each one publishes its build stamp
  // as a cvar of its own (`dbz3_gpu_build` / `dbz3_runtime_build`, defined in
  // rex/dbz3_build.h and bumped together with src/version.rc). The cvar registry
  // is shared between the exe and the DLLs, so reading them from here is enough.
#if REX_PLATFORM_WIN32
  components.push_back({"dbz3.exe", CurrentVersion(), true});
#else
  components.push_back({"dbz3", CurrentVersion(), true});
#endif
  components.push_back({"rexgpu-xenos", rex::cvar::GetFlagByName("dbz3_gpu_build"), true});
  components.push_back({"rexruntime", rex::cvar::GetFlagByName("dbz3_runtime_build"), true});
  components.push_back({"amd_fidelityfx_dx12.dll", DllFileVersion("amd_fidelityfx_dx12.dll"),
                        false});
  // Major.minor.patch must match the executable; a repack build number
  // (1.2.4.1 vs 1.2.4.0) is not a mismatch. A component that cannot be
  // identified at all (an older build, which has no stamp) counts as a mismatch
  // too: that is exactly the "new exe over an old folder" case.
  const std::string reference = components[0].version;
  if (!reference.empty()) {
    const Version ref = ParseVersion(reference);
    for (const InstalledComponent& component : components) {
      if (!component.must_match) {
        continue;
      }
      if (component.version.empty()) {
        g_component_mismatch = component.file_name;
        break;
      }
      const Version v = ParseVersion(component.version);
      if (v.parts[0] != ref.parts[0] || v.parts[1] != ref.parts[1] ||
          v.parts[2] != ref.parts[2]) {
        g_component_mismatch = component.file_name;
        break;
      }
    }
  }
  LogEnvironmentOnce(components, g_component_mismatch);
  return components;
}

std::string InstalledVersionMismatch() {
  InstalledComponents();
  return g_component_mismatch;
}

void OpenUrl(const std::string& url) {
#if REX_PLATFORM_WIN32
  if (url.empty()) {
    return;
  }
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)url;
#endif
}

}  // namespace dbz3::launcher
