// dbz3 - Update check implementation (GitHub releases API).

#include "update_check.h"
#include "linux_env.h"

#include <rex/cvar.h>  // REX_PLATFORM_WIN32 (via rex/platform.h)
#include <rex/logging.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
#else
#include <sys/wait.h>
#include <unistd.h>
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

}  // namespace

bool IsVersionNewer(const std::string& a, const std::string& b) { return VersionNewer(a, b); }

namespace {

// Ultimo fallo de red, para el log y para el aviso del launcher ("auto: error 12007, ...").
std::mutex g_err_mu;
std::string g_last_error;
void SetNetError(std::string e) {
  std::lock_guard<std::mutex> lk(g_err_mu);
  g_last_error = std::move(e);
}

std::string Narrow(const wchar_t* w) {
  std::string out;
  for (; w && *w; ++w) out += char(*w < 128 ? *w : '?');
  return out;
}

// Ultima via: el curl del sistema (Windows 10 1803+ lo trae en System32; Linux/SteamOS casi siempre). Tiene su
// propia pila de proxy y de certificados, asi que funciona en equipos donde WinHTTP no (antivirus que inspecciona
// HTTPS, revocacion sin respuesta, WPAD roto). `headers` = cabeceras separadas por CRLF. Con `location` no sigue
// redirecciones y devuelve la cabecera Location. Devuelve el cuerpo de un 200 o "".
std::string CurlGet(const std::string& url, const std::string& headers, std::string* location, std::string& why) {
  std::vector<std::string> args = {"curl", "-sS", "--max-time", "40", "--connect-timeout", "15", "-A",
                                   "dbz3-launcher/1.5"};
  for (size_t i = 0; i < headers.size();) {
    size_t e = headers.find("\r\n", i);
    if (e == std::string::npos) e = headers.size();
    if (e > i) {
      args.push_back("-H");
      args.push_back(headers.substr(i, e - i));
    }
    i = e + 2;
  }
  if (location) {
    args.push_back("-o");
#if REX_PLATFORM_WIN32
    args.push_back("NUL");
#else
    args.push_back("/dev/null");
#endif
    args.push_back("-w");
    args.push_back("%{http_code} %{redirect_url}");
  } else {
    args.push_back("-f");
    args.push_back("-L");
  }
  args.push_back(url);
  std::string out;
  int code = -1;
#if REX_PLATFORM_WIN32
  std::wstring cmd;
  for (const std::string& a : args) {
    cmd += L"\"";
    for (char c : a) {
      if (c == '"') cmd += L'\\';
      cmd += wchar_t(uint8_t(c));
    }
    cmd += L"\" ";
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) {
    why = "curl: sin tuberia";
    return {};
  }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  wchar_t sys[MAX_PATH] = {};
  GetSystemDirectoryW(sys, MAX_PATH);
  const std::wstring exe = std::wstring(sys) + L"\\curl.exe";
  const BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                      nullptr, &si, &pi);
  CloseHandle(wr);
  if (!started) {
    CloseHandle(rd);
    why = "curl: no esta en el sistema";
    return {};
  }
  char buf[65536];
  DWORD n = 0;
  while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, 45000);
  DWORD ec = 1;
  GetExitCodeProcess(pi.hProcess, &ec);
  code = int(ec);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
#else
  std::string cmd;
  for (const std::string& a : args) {
    cmd += "'";
    for (char c : a) cmd += c == '\'' ? std::string("'\\''") : std::string(1, c);
    cmd += "' ";
  }
  cmd += "2>&1";
  if (FILE* f = popen(cmd.c_str(), "r")) {
    char buf[65536];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    code = pclose(f);
  } else {
    why = "curl: no se pudo lanzar";
    return {};
  }
#endif
  if (location) {
    const size_t sp = out.find(' ');
    const int status = std::atoi(out.c_str());
    if (code == 0 && status >= 300 && status < 400 && sp != std::string::npos) {
      *location = out.substr(sp + 1);
      while (!location->empty() && std::isspace(uint8_t(location->back()))) location->pop_back();
      return {};
    }
    why = "curl: HTTP " + std::to_string(status);
    return {};
  }
  if (code != 0) {
    why = "curl: codigo " + std::to_string(code) + " " + out.substr(0, 120);
    return {};
  }
  return out;
}
}  // namespace

std::string LastNetworkError() {
  std::lock_guard<std::mutex> lk(g_err_mu);
  return g_last_error;
}

#if REX_PLATFORM_WIN32
namespace {

// Sesiones a probar, en orden. Muchos PCs fallan solo con una: la deteccion automatica del proxy (WPAD/PAC) se
// cuelga o da un proxy muerto (12180/12002), y hay redes donde solo sirve la directa o el proxy del sistema.
// Se fuerza TLS 1.2/1.3: en Windows antiguos WinHTTP sale con TLS 1.0, que GitHub rechaza.
HINTERNET OpenSession(int mode, const wchar_t* agent) {
  static const DWORD kAccess[3] = {WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_ACCESS_TYPE_NO_PROXY};
  HINTERNET s = WinHttpOpen(agent, kAccess[mode], WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) return nullptr;   // p. ej. AUTOMATIC_PROXY antes de Windows 8.1: pasa a la siguiente via
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
  DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
    protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // sistemas sin TLS 1.3
    WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
  }
  return s;
}

const char* kModeName[3] = {"auto", "sistema", "directa"};

// Una peticion. status = codigo HTTP (0 sin respuesta); err = GetLastError del fallo. Con `location` no sigue
// redirecciones y devuelve la cabecera Location.
std::string GetOnce(HINTERNET session, const wchar_t* host, const wchar_t* path, const wchar_t* headers,
                    DWORD& status, DWORD& err, std::wstring* location) {
  std::string body;
  status = 0;
  err = 0;
  HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect) {
    err = GetLastError();
    return body;
  }
  // GitHub rejects API requests without a User-Agent (WinHttpOpen sets one).
  HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (request) {
    if (location) {
      DWORD no_redirect = WINHTTP_DISABLE_REDIRECTS;
      WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirect, sizeof(no_redirect));
    }
    if (WinHttpSendRequest(request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                           headers ? DWORD(-1) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
      DWORD status_len = sizeof(status);
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX);
      if (location && status >= 300 && status < 400) {
        DWORD size = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                            WINHTTP_NO_HEADER_INDEX);
        location->resize(size / sizeof(wchar_t));
        if (!size || !WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                          location->data(), &size, WINHTTP_NO_HEADER_INDEX)) {
          location->clear();
        } else {
          location->resize(size / sizeof(wchar_t));
        }
      } else if (status == 200) {
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
    } else {
      err = GetLastError();
    }
    WinHttpCloseHandle(request);
  } else {
    err = GetLastError();
  }
  WinHttpCloseHandle(connect);
  return body;
}

// Prueba las tres vias de WinHTTP y, si ninguna llega al servidor, curl. Un 4xx/5xx es respuesta del servidor:
// no se reintenta por otra via. Deja el motivo en el log y en LastNetworkError().
std::string GetAnyRoute(const wchar_t* host, const wchar_t* path, const wchar_t* headers, std::wstring* location) {
  std::string why;
  const auto add = [&](const std::string& w) { why += (why.empty() ? "" : ", ") + w; };
  // PRUEBA: DBZ3_TEST_NET_SKIP=N salta las N primeras vias de WinHTTP (3 = solo curl).
  static const int skip = std::getenv("DBZ3_TEST_NET_SKIP") ? std::atoi(std::getenv("DBZ3_TEST_NET_SKIP")) : 0;
  for (int mode = 0; mode < 3; ++mode) {
    if (mode < skip) {
      add(std::string(kModeName[mode]) + ": saltada (prueba)");
      continue;
    }
    HINTERNET session = OpenSession(mode, L"dbz3-launcher/1.5");
    if (!session) {
      add(std::string(kModeName[mode]) + ": sin sesion " + std::to_string(GetLastError()));
      continue;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
    DWORD status = 0, err = 0;
    std::string body = GetOnce(session, host, path, headers, status, err, location);
    WinHttpCloseHandle(session);
    if (status == 200 || (location && !location->empty())) {
      if (!why.empty()) REXLOG_INFO("dbz3 red: conectado por la via {} tras: {}", kModeName[mode], why);
      SetNetError({});
      return body;
    }
    if (status != 0) {
      add(std::string(kModeName[mode]) + ": HTTP " + std::to_string(status));
      break;
    }
    add(std::string(kModeName[mode]) + ": error " + std::to_string(err));
  }
  if (why.find("HTTP") == std::string::npos) {   // ninguna via llego al servidor: curl
    std::string cw, loc;
    const std::string url = "https://" + Narrow(host) + Narrow(path);
    std::string body = CurlGet(url, Narrow(headers), location ? &loc : nullptr, cw);
    if (!body.empty() || !loc.empty()) {
      if (location) *location = std::wstring(loc.begin(), loc.end());
      REXLOG_INFO("dbz3 red: conectado con curl tras: {}", why);
      SetNetError({});
      return body;
    }
    add(cw);
  }
  REXLOG_WARN("dbz3 red: no se pudo leer https://{}{} ({})", Narrow(host), Narrow(path), why);
  SetNetError(why);
  return {};
}

}  // namespace

std::string HttpGet(const wchar_t* host, const wchar_t* path, const wchar_t* headers) {
  return GetAnyRoute(host, path, headers, nullptr);
}

namespace {
std::string GetRedirect(const wchar_t* host, const wchar_t* path) {
  std::wstring location;
  GetAnyRoute(host, path, nullptr, &location);
  return std::string(location.begin(), location.end());
}
}  // namespace

namespace {
// Plan B de las descargas (2026-10-19, pack HD de 2,9 GB que "no baja" en el PC de un jugador): curl.exe del sistema
// (Windows 10 1803+), con reintentos que siguen donde se corto (-C -). El token solo va al primer host: curl no reenvia
// -H Authorization a otro host al seguir la redireccion. El progreso se lee del tamano del fichero temporal.
long long CurlDownloadWin(const std::string& url, const std::string& headers, const std::filesystem::path& tmp,
                          const std::function<void(long long)>& progress, std::string& why) {
  std::vector<std::string> args = {"curl", "-sSfL", "--connect-timeout", "20", "--retry", "8", "--retry-delay", "3",
                                   "-C", "-", "-A", "dbz3-novas-mods/1.5", "-o", "@OUT@"};   // @OUT@: la ruta (ancha)
  for (size_t i = 0; i < headers.size();) {
    size_t e = headers.find("\r\n", i);
    if (e == std::string::npos) e = headers.size();
    if (e > i) {
      args.push_back("-H");
      args.push_back(headers.substr(i, e - i));
    }
    i = e + 2;
  }
  args.push_back(url);
  std::wstring cmd;
  for (const std::string& a : args) {
    std::wstring w;
    for (char c : a) w += wchar_t(uint8_t(c));   // cabeceras y URL: ASCII
    if (a == "@OUT@") w = tmp.wstring();
    cmd += L"\"";
    for (wchar_t c : w) {
      if (c == L'"') cmd += L'\\';
      cmd += c;
    }
    cmd += L"\" ";
  }
  wchar_t sys[MAX_PATH] = {};
  GetSystemDirectoryW(sys, MAX_PATH);
  const std::wstring exe = std::wstring(sys) + L"\\curl.exe";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    why = "curl: no esta en el sistema";
    return -1;
  }
  std::error_code ec;
  while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
    if (progress) progress((long long)std::filesystem::file_size(tmp, ec));
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0) {
    why = "curl: codigo " + std::to_string(code);
    return -1;
  }
  const long long n = (long long)std::filesystem::file_size(tmp, ec);
  return ec ? -1 : n;
}
}  // namespace

long long HttpDownloadReanudable(const std::string& url, const std::string& headers, const std::filesystem::path& out,
                                 const std::function<void(long long)>& progress) {
  std::string why;
  const long long n = CurlDownloadWin(url, headers, out, progress, why);
  if (n < 0) SetNetError(why);
  return n;
}

long long HttpDownload(const std::string& url, const std::wstring& headers,
                       const std::filesystem::path& out, bool append,
                       const std::function<void(long long)>& progress) {
  std::error_code size_ec;
  const auto out_start = append && std::filesystem::exists(out, size_ec) ? std::filesystem::file_size(out, size_ec) : 0;
  std::wstring next(url.begin(), url.end());
  std::wstring first_host;
  long long written = -1;
  const std::wstring start = next;
  std::string why;
  // PRUEBA: DBZ3_TEST_NET_SKIP=3 baja directamente con curl (como en GetAnyRoute)
  static const int skip = std::getenv("DBZ3_TEST_NET_SKIP") ? std::atoi(std::getenv("DBZ3_TEST_NET_SKIP")) : 0;
  for (int mode = skip; mode < 3 && written < 0; ++mode) {
  next = start;
  first_host.clear();
  bool answered = false;   // el servidor contesto (aunque fuese con un error): no se prueba otra via
  HINTERNET session = OpenSession(mode, L"dbz3-novas-mods/1.5");
  if (!session) continue;
  WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
  for (int hop = 0; hop < 5 && written < 0; ++hop) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    std::vector<wchar_t> path(next.size() + 1), extra(next.size() + 1);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = DWORD(path.size());
    uc.lpszExtraInfo = extra.data();   // la query (firma de los enlaces de assets) va aparte
    uc.dwExtraInfoLength = DWORD(extra.size());
    if (!WinHttpCrackUrl(next.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) break;
    if (hop == 0) first_host = host;
    const std::wstring object = std::wstring(path.data()) + extra.data();
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) break;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    std::wstring location;
    bool redirect = false;
    if (request) {
      DWORD no_redirect = WINHTTP_DISABLE_REDIRECTS;
      WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirect, sizeof(no_redirect));
      const bool same_host = first_host == host;
      if (WinHttpSendRequest(request, same_host && !headers.empty() ? headers.c_str() : WINHTTP_NO_ADDITIONAL_HEADERS,
                             same_host && !headers.empty() ? DWORD(-1) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        answered = true;
        if (status != 200 && !(status >= 301 && status <= 308)) why = "HTTP " + std::to_string(status);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
          DWORD size = 0;
          WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                              WINHTTP_NO_HEADER_INDEX);
          location.resize(size / sizeof(wchar_t));
          if (size && WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                          location.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
            location.resize(size / sizeof(wchar_t));
            redirect = true;
          }
        } else if (status == 200) {
          std::ofstream file(out, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
          long long total = 0;
          std::vector<char> chunk(1 << 20);
          bool ok = bool(file);
          while (ok) {
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), DWORD(chunk.size()), &read)) {
              ok = false;
              break;
            }
            if (read == 0) break;   // fin del cuerpo
            file.write(chunk.data(), read);
            ok = bool(file);
            total += read;
            if (progress) progress(total);
          }
          file.close();
          if (ok && !file.fail()) written = total;
          else why = "descarga cortada o disco lleno";
        }
      } else {
        why = std::string(kModeName[mode]) + ": error " + std::to_string(GetLastError());
      }
      WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connect);
    if (!redirect) break;
    next = location;
  }
  WinHttpCloseHandle(session);
  if (answered) break;
  }
  if (written < 0 && why.rfind("HTTP 4", 0) != 0) {   // corte, disco o red: curl (un 4xx no lo arregla otra via)
    REXLOG_WARN("dbz3 red: descarga por WinHTTP fallida ({}); se reintenta con curl", why);
    std::error_code ec;
    std::filesystem::resize_file(out, out_start, ec);   // fuera lo que dejo a medias en este fichero
    if (!append) std::filesystem::remove(out, ec);
    const std::filesystem::path tmp = out.string() + ".parte";
    std::filesystem::remove(tmp, ec);
    std::string cw;
    const long long n = CurlDownloadWin(url, std::string(headers.begin(), headers.end()), tmp, progress, cw);
    if (n >= 0) {
      std::ifstream in(tmp, std::ios::binary);
      std::ofstream o(out, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
      o << in.rdbuf();
      in.close();
      o.close();
      if (o) written = n;
      else why = "disco lleno al copiar";
    } else {
      why += ", " + cw;
    }
    std::filesystem::remove(tmp, ec);
  }
  if (written < 0) {
    REXLOG_WARN("dbz3 red: descarga fallida ({})", why);
    SetNetError(why);
  }
  return written;
}

#else

long long HttpDownloadReanudable(const std::string&, const std::string&, const std::filesystem::path&,
                                 const std::function<void(long long)>&) {
  return -1;   // los videos HD solo existen en Windows
}

std::string HttpGet(const wchar_t* host, const wchar_t* path, const wchar_t* headers) {
  std::string why;
  std::string body = CurlGet("https://" + Narrow(host) + Narrow(path), Narrow(headers), nullptr, why);
  if (body.empty()) {
    REXLOG_WARN("dbz3 red: no se pudo leer https://{}{} ({})", Narrow(host), Narrow(path), why);
    SetNetError(why);
  } else {
    SetNetError({});
  }
  return body;
}

namespace {
std::string GetRedirect(const wchar_t* host, const wchar_t* path) {
  std::string why, loc;
  CurlGet("https://" + Narrow(host) + Narrow(path), {}, &loc, why);
  return loc;
}
}  // namespace

// ponytail: sin progreso en Linux (curl escribe directo al fichero); add when a progress bar is needed there.
long long HttpDownload(const std::string& url, const std::wstring& headers, const std::filesystem::path& out,
                       bool append, const std::function<void(long long)>& progress) {
  const std::filesystem::path tmp = out.string() + ".curl";
  std::string cmd = "curl -sSfL --connect-timeout 15 -A dbz3-launcher/1.5 -o '" + tmp.string() + "' ";
  const std::string h(headers.begin(), headers.end());   // el token solo va al primer host (curl no lo reenvia)
  for (size_t i = 0; i < h.size();) {
    size_t e = h.find("\r\n", i);
    if (e == std::string::npos) e = h.size();
    if (e > i) cmd += "-H '" + h.substr(i, e - i) + "' ";
    i = e + 2;
  }
  cmd += "'" + url + "'";
  if (std::system(cmd.c_str()) != 0) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    SetNetError("curl: descarga fallida");
    return -1;
  }
  std::ifstream in(tmp, std::ios::binary);
  std::ofstream o(out, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
  o << in.rdbuf();
  const long long n = (long long)in.tellg();
  in.close();
  o.close();
  std::error_code ec;
  const long long size = (long long)std::filesystem::file_size(tmp, ec);
  std::filesystem::remove(tmp, ec);
  if (!o) return -1;
  if (progress) progress(size);
  (void)n;
  return size;
}

#endif  // REX_PLATFORM_WIN32

std::string CurrentVersion() {
  static const std::string kVersion = []() -> std::string {
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
  }();
  return kVersion;
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
  try {
  std::thread([]() {
    struct ReleaseInflight {
      ~ReleaseInflight() { g_inflight.store(false, std::memory_order_release); }
    } release;
    try {
    const std::string body = HttpGet(kApiHost, kApiPath);
    std::string tag = JsonStringField(body, "tag_name");
    std::string url = JsonStringField(body, "html_url");
    if (tag.empty()) {   // API agotada (60/hora por IP compartida) o bloqueada: la pagina publica redirige al tag
      const std::string loc = GetRedirect(L"github.com", L"/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest");
      const size_t at = loc.rfind("/tag/");
      if (at != std::string::npos) {
        tag = loc.substr(at + 5);
        url = loc;
      }
    }
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
    } catch (...) {
      g_state.store(UpdateState::kFailed, std::memory_order_release);
    }
  }).detach();
  } catch (...) {
    g_state.store(UpdateState::kFailed, std::memory_order_release);
    g_inflight.store(false, std::memory_order_release);
  }
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
  line += "linux " + LinuxEnvSummary();   // distro, nucleo, RAM, sesion grafica, Steam/Flatpak (linux_env.h)
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
          v.parts[2] != ref.parts[2]) {   // el 4.o numero (repack) no cuenta, como dice el comentario
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

#if !REX_PLATFORM_WIN32
// fork+exec de xdg-open sin pasar por /bin/sh; un hilo recoge al hijo para no dejar zombis.
static void SpawnXdgOpen(const std::string& arg) {
  const pid_t pid = fork();
  if (pid == 0) {
    execlp("xdg-open", "xdg-open", arg.c_str(), static_cast<char*>(nullptr));
    _exit(127);   // sin xdg-open: no hace nada, como antes
  }
  if (pid > 0) {
    std::thread([pid] { int st = 0; waitpid(pid, &st, 0); }).detach();
  }
}
#endif

void OpenUrl(const std::string& url) {
  // Solo https: los enlaces de autor vienen del catalogo de Nova's Mods y un texto como
  // "C:\algo.exe" no debe poder abrir un fichero local.
  if (url.rfind("https://", 0) != 0) {
    return;
  }
#if REX_PLATFORM_WIN32
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  SpawnXdgOpen(url);   // argv directo, sin shell
#endif
}

void OpenFolder(const std::filesystem::path& dir) {
#if REX_PLATFORM_WIN32
  std::system(("explorer \"" + dir.string() + "\"").c_str());   // identico a los 7 botones de antes
#else
  SpawnXdgOpen(dir.string());
#endif
}

bool HttpGetUrl(const std::string& url, std::string& body) {
  body.clear();
  if (url.rfind("http://", 0) == 0) {   // solo para el servidor falso de las pruebas
    std::string why;
    body = CurlGet(url, {}, nullptr, why);
    SetNetError(why);
    return why.empty();
  }
  if (url.rfind("https://", 0) != 0) return false;
  const size_t slash = url.find('/', 8);
  const std::string host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
  const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
  body = HttpGet(std::wstring(host.begin(), host.end()).c_str(), std::wstring(path.begin(), path.end()).c_str());
  return LastNetworkError().empty();
}

}  // namespace dbz3::launcher
