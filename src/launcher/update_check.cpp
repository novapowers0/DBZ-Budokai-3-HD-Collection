// dbz3 - Update check implementation (GitHub releases API).

#include "update_check.h"

#include <rex/cvar.h>  // REX_PLATFORM_WIN32 (via rex/platform.h)

#include <atomic>
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
std::atomic<bool> g_started{false};
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

// True when `a` is a strictly newer dotted version than `b` ("1.2.10" > "1.2.9").
bool VersionNewer(const std::string& a, const std::string& b) {
  int av[3] = {0, 0, 0};
  int bv[3] = {0, 0, 0};
  std::sscanf(a.c_str(), "%d.%d.%d", &av[0], &av[1], &av[2]);
  std::sscanf(b.c_str(), "%d.%d.%d", &bv[0], &bv[1], &bv[2]);
  for (int i = 0; i < 3; ++i) {
    if (av[i] != bv[i]) {
      return av[i] > bv[i];
    }
  }
  return false;
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
          std::snprintf(buf, sizeof(buf), "%d.%d.%d", HIWORD(info->dwFileVersionMS),
                        LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS));
          return buf;
        }
      }
    }
  }
#endif
  return {};
}

void StartUpdateCheck() {
  if (g_started.exchange(true)) {
    return;
  }
  std::thread([]() {
    const std::string body = HttpGet(kApiHost, kApiPath);
    const std::string tag = JsonStringField(body, "tag_name");
    const std::string url = JsonStringField(body, "html_url");
    if (tag.empty() || url.empty()) {
      g_state.store(UpdateState::kFailed, std::memory_order_release);
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
