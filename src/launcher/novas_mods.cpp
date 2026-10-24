// dbz3 - "Nova's Mods" catalog and downloads (see novas_mods.h).

#include "novas_mods.h"

#include "../mods.h"
#include "i18n.h"
#include "update_check.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <toml++/toml.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace dbz3::launcher {

namespace {

namespace fs = std::filesystem;

constexpr const wchar_t* kHost = L"api.github.com";
constexpr const char* kRepoPath = "/repos/novapowers0/DBZ3HD-Novas-Mods/contents/";
constexpr const char* kAssetUrl = "https://api.github.com/repos/novapowers0/DBZ3HD-Novas-Mods/releases/assets/";

struct Entry {
  NovaMod mod;
  std::map<std::string, std::string> names, descs;  // by language code
};

std::atomic<NovaState> g_state{NovaState::kLoading};
std::atomic<bool> g_started{false}, g_inflight{false};
std::mutex g_mu;
std::vector<Entry> g_catalog;
std::set<std::string> g_busy;
std::string g_status;

fs::path ExeDir() { return ModsRoot().parent_path(); }

// novas_mods.key: the read-only token, hex of its bytes XOR 0x5A (keeps it out of
// plain-text searches; tools/novas_mods.py clave writes it).
std::string Token() {
  std::ifstream in(ExeDir() / "novas_mods.key");
  std::string hex, out;
  in >> hex;
  if (hex.rfind("github_pat_", 0) == 0) return hex;   // pegada tal cual (sin pasar por la herramienta)
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    if (!std::isxdigit(uint8_t(hex[i])) || !std::isxdigit(uint8_t(hex[i + 1]))) return {};   // fichero roto: sin clave
    out += char(std::stoi(hex.substr(i, 2), nullptr, 16) ^ 0x5A);
  }
  return out;
}

std::string Get(const std::string& repo_file) {
  const std::string token = Token();
  if (token.empty()) return {};
  const std::string path = kRepoPath + repo_file;
  const std::wstring wpath(path.begin(), path.end());
  const std::string h = "Authorization: Bearer " + token +
                        "\r\nAccept: application/vnd.github.raw\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
  const std::wstring wh(h.begin(), h.end());
  return HttpGet(kHost, wpath.c_str(), wh.c_str());
}

std::string Pick(const std::map<std::string, std::string>& m) {
  for (const char* k : {i18n::LangCode(), "en", "es"}) {
    const auto it = m.find(k);
    if (it != m.end() && !it->second.empty()) return it->second;
  }
  return {};
}

void Fetch() {
  if (g_inflight.exchange(true)) return;
  if (Token().empty()) {
    g_state = NovaState::kNoKey;
    g_inflight = false;
    return;
  }
  g_state = NovaState::kLoading;
  std::thread([] {
    std::vector<Entry> list;
    const std::string body = Get("catalogo.toml");
    try {
      const toml::table tbl = toml::parse(body);
      if (const toml::array* mods = tbl["mod"].as_array()) {
        for (const toml::node& n : *mods) {
          const toml::table* t = n.as_table();
          if (!t) continue;
          Entry e;
          e.mod.id = (*t)["id"].value_or(std::string());
          e.mod.version = (*t)["version"].value_or(std::string());
          e.mod.zip = (*t)["zip"].value_or(std::string());
          // "archivo" = "zip" de los mods que necesitan este exe o uno posterior: los launchers de antes solo leen
          // "zip" y no los ven (tools/novas_mods.py, exe_nuevo). Un id repetido: gana la entrada de despues.
          if (std::string a = (*t)["archivo"].value_or(std::string()); !a.empty()) e.mod.zip = std::move(a);
          e.mod.tipo = (*t)["tipo"].value_or(std::string());
          e.mod.size = (*t)["size"].value_or(0LL);
          // sha256 opcional (tools/novas_mods.py): un catalogo viejo sin el campo se instala como antes
          e.mod.sha256 = (*t)["sha256"].value_or(std::string());
          std::transform(e.mod.sha256.begin(), e.mod.sha256.end(), e.mod.sha256.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          e.mod.autor = (*t)["autor"].value_or(std::string());
          if (const toml::array* a = (*t)["enlaces"].as_array()) {
            for (const toml::node& u : *a) {
              const std::string url = u.value_or(std::string());
              if (url.rfind("https://", 0) == 0) e.mod.enlaces.push_back(url);   // nada de rutas locales
            }
          }
          if (const toml::array* a = (*t)["excluye"].as_array()) {
            for (const toml::node& x : *a) e.mod.excluye.push_back(x.value_or(std::string()));
          }
          if (const toml::array* a = (*t)["partes"].as_array()) {
            for (const toml::node& p : *a) {
              if (const long long id = p.value_or(0LL); id > 0) e.mod.partes.push_back(id);
            }
          }
          for (const auto& [k, v] : *t) {
            const std::string key(k.str());
            if (key.rfind("nombre_", 0) == 0) e.names[key.substr(7)] = v.value_or(std::string());
            if (key.rfind("descripcion_", 0) == 0) e.descs[key.substr(12)] = v.value_or(std::string());
          }
          // id = nombre de carpeta (solo letras, cifras, _ y -: ni rutas ni "C:"); zip sin ?, # ni ".."
          const auto plain = [](const std::string& s) {
            return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(uint8_t(c)) || c == '_' || c == '-'; });
          };
          if (!e.mod.id.empty() && (!e.mod.zip.empty() || !e.mod.partes.empty()) && plain(e.mod.id) &&
              e.mod.zip.find_first_of("?#\\") == std::string::npos && e.mod.zip.find("..") == std::string::npos) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Entry& x) { return x.mod.id == e.mod.id; }),
                       list.end());
            list.push_back(std::move(e));
          }
        }
      }
    } catch (const std::exception& ex) {
      REXLOG_WARN("dbz3: Nova's Mods: catalogo ilegible ({})", ex.what());
    }
    {
      std::lock_guard<std::mutex> lock(g_mu);
      g_catalog = std::move(list);
    }
    g_state = body.empty() || g_catalog.empty() ? NovaState::kFailed : NovaState::kReady;
    g_inflight = false;
  }).detach();
}

std::string Stamp() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
  return buf;
}

void SetStatus(const std::string& s) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_status = s;
}

bool StartsWithPK(const fs::path& zip) {
  std::ifstream in(zip, std::ios::binary);
  char pk[2] = {};
  return in.read(pk, 2) && pk[0] == 'P' && pk[1] == 'K';
}

// SHA-256 (hex en minusculas) de un fichero; "" si no se puede leer o no hay BCrypt (Linux: no se descarga nada).
std::string Sha256File(const fs::path& path) {
  std::string hex;
#ifdef _WIN32
  std::ifstream in(path, std::ios::binary);
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  if (in && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0) {
      std::vector<char> buf(1 << 20);   // por trozos: el pack HD pesa gigas
      bool ok = true;
      while (ok && (in.read(buf.data(), std::streamsize(buf.size())) || in.gcount() > 0)) {
        ok = BCryptHashData(h, PUCHAR(buf.data()), ULONG(in.gcount()), 0) >= 0;
      }
      UCHAR d[32];
      if (ok && in.eof() && BCryptFinishHash(h, d, sizeof(d), 0) >= 0) {
        char two[3];
        for (UCHAR b : d) {
          std::snprintf(two, sizeof(two), "%02x", b);
          hex += two;
        }
      }
      BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
  }
#else
  (void)path;
#endif
  return hex;
}

// El catalogo trae la huella del zip: si no coincide (corrupto o cambiado por el camino) no se instala nada.
// Sin huella (catalogo viejo) no se comprueba. Si no se pudo leer el zip tampoco coincide.
bool BadHash(const NovaMod& mod, const fs::path& zip) {
  if (mod.sha256.empty()) return false;
  SetStatus(std::string(i18n::T("Comprobando ", "Checking ")) + mod.name + "...");
  const std::string got = Sha256File(zip);
  if (got == mod.sha256) return false;
  REXLOG_WARN("dbz3: Nova's Mods: {} no coincide con el catalogo (esperado {}, bajado '{}')", mod.id, mod.sha256, got);
  return true;
}

// Zip pequeno del repositorio (Contents API).
long long DownloadWhole(const NovaMod& mod, const fs::path& zip) {
  const std::string data = Get(mod.zip);
  if (data.empty()) return -1;
  std::ofstream out(zip, std::ios::binary | std::ios::trunc);
  out.write(data.data(), std::streamsize(data.size()));
  out.close();
  return out.fail() ? -1 : (long long)data.size();
}

// Zip grande partido en assets de una release (GitHub: <2 GiB por asset, la Contents API no
// sirve mas de ~100 MB): se bajan en orden a disco, uno detras de otro, con el % en el estado.
long long DownloadParts(const NovaMod& mod, const fs::path& zip) {
  const std::string token = Token();
  if (token.empty()) return -1;
  const std::string h = "Authorization: Bearer " + token +
                        "\r\nAccept: application/octet-stream\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
  const std::wstring wh(h.begin(), h.end());
  long long done = 0;
  int last_pct = -1;
  for (size_t i = 0; i < mod.partes.size(); ++i) {
    const long long base = done;
    const long long n = HttpDownload(kAssetUrl + std::to_string(mod.partes[i]), wh, zip, i > 0, [&](long long b) {
      const int pct = mod.size > 0 ? int((base + b) * 100 / mod.size) : -1;
      if (pct == last_pct) return;
      last_pct = pct;
      SetStatus(std::string(i18n::T("Descargando ", "Downloading ")) + mod.name + "... " +
                (pct >= 0 ? std::to_string(pct) + "%" : std::to_string((base + b) >> 20) + " MB"));
    });
    if (n < 0) return -1;
    done += n;
  }
  SetStatus(std::string(i18n::T("Instalando ", "Installing ")) + mod.name + "...");
  return done;
}

}  // namespace

void StartNovaCatalog() {
  if (g_started.exchange(true)) return;
  // DBZ3_TEST_SHA256=<fichero>: escribe su SHA-256 en el log (comprobacion del ayudante, tools/novas_mods.py prueba)
  if (const char* f = std::getenv("DBZ3_TEST_SHA256"); f && *f) REXLOG_INFO("dbz3: sha256 de '{}' = {}", f, Sha256File(f));
  Fetch();
}

void RefreshNovaCatalog() { Fetch(); }

NovaState GetNovaState() { return g_state.load(); }

std::vector<NovaMod> NovaCatalog() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<NovaMod> out;
  for (const Entry& e : g_catalog) {
    NovaMod m = e.mod;
    m.name = Pick(e.names);
    if (m.name.empty()) m.name = m.id;
    m.description = Pick(e.descs);
    out.push_back(std::move(m));
  }
  return out;
}

std::string NovaInstalledVersion(const std::string& id) {
  std::error_code ec;
  for (const std::string& dir : {id, id + ".disabled"}) {
    if (fs::is_directory(ModsRoot() / dir, ec)) {
      const std::string v = GetModManifestValue(id, "version");
      return v.empty() ? "?" : v;
    }
  }
  return {};
}

bool NovaBusy(const std::string& id) {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_busy.count(id) > 0;
}

std::string NovaStatus() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_status;
}

void InstallNovaMod(const NovaMod& mod) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_busy.insert(mod.id).second) return;
    g_status = std::string(i18n::T("Descargando ", "Downloading ")) + mod.name + "...";
  }
  std::thread([mod] {
    std::error_code ec;
    std::string error;
    const fs::path tmp_dir = ExeDir() / "novas_tmp";
    fs::create_directories(tmp_dir, ec);
    const fs::path zip = tmp_dir / (mod.id + ".zip");
    long long got = -1;
    // D3: espacio ANTES de bajar: el zip (tamano del catalogo) y lo que ocupa extraido.
    // Un pack HD de 2,9 GB necesita ~6 GB libres; sin esto la descarga se cortaba a medias
    // y el mensaje culpaba a la conexion.
    {
      const uintmax_t need =
          (mod.size > 0 ? uintmax_t(mod.size) : (uintmax_t(1) << 30)) * 2 + (uintmax_t(256) << 20);
      std::error_code sec;
      const fs::space_info sp = fs::space(ExeDir(), sec);
      if (!sec && sp.available < need) {
        error = i18n::T("no hay espacio libre suficiente para descargar e instalar (hacen falta ",
                        "not enough free space to download and install (needed ") +
                std::to_string((need + (uintmax_t(1) << 20) - 1) >> 20) + " MB)";
        REXLOG_WARN("dbz3: Nova's Mods: {} sin espacio (libres {} MB, hacen falta {} MB)", mod.id,
                    sp.available >> 20, need >> 20);
      }
    }
    if (error.empty()) {
      try {
        got = mod.partes.empty() ? DownloadWhole(mod, zip) : DownloadParts(mod, zip);
      } catch (const std::exception& ex) {   // un bad_alloc en este hilo cerraba el launcher entero
        REXLOG_WARN("dbz3: Nova's Mods: descarga de {} cortada ({})", mod.id, ex.what());
        got = -1;
      }
    }
    // un zip empieza por "PK": una respuesta de error de GitHub no se instala
    if (error.empty() && (got < 4 || !StartsWithPK(zip) || (mod.size > 0 && got != mod.size))) {
      error = i18n::T("no se pudo descargar (revisa tu conexion y el espacio libre)",
                      "download failed (check your connection and free disk space)");
    } else if (BadHash(mod, zip)) {
      error = i18n::T("la descarga no coincide con el catalogo (archivo danado o alterado); no se instala",
                      "the download does not match the catalog (damaged or altered file); not installed");
    } else {
      // la copia instalada pasa a mods_respaldo (nunca se borra) y se respeta si estaba desactivada
      bool was_disabled = false;
      std::vector<std::pair<fs::path, fs::path>> moved;   // (copia apartada, sitio original)
      for (const std::string& dir : {mod.id, mod.id + ".disabled"}) {
        const fs::path old = ModsRoot() / dir;
        if (!fs::is_directory(old, ec)) continue;
        was_disabled = was_disabled || dir != mod.id || fs::exists(old / ".disabled", ec);
        const fs::path bak = ExeDir() / "mods_respaldo" / (dir + "_" + Stamp());
        fs::create_directories(bak.parent_path(), ec);
        fs::rename(old, bak, ec);
        if (ec) error = i18n::T("no se pudo apartar la version instalada", "could not move the installed version aside");
        else moved.emplace_back(bak, old);
      }
      std::string name;
      if (error.empty() && !InstallModFromZip(rex::path_to_utf8(zip), name, error)) {
        error = i18n::T("el zip no se pudo instalar", "the zip could not be installed");
      }
      if (!error.empty()) {   // fallo: lo copiado a medias se aparta y la version que habia vuelve a su sitio
        for (const auto& [bak, old] : moved) {
          if (fs::exists(old, ec)) fs::rename(old, ExeDir() / "mods_respaldo" / (old.filename().string() + "_fallido_" + Stamp()), ec);
          fs::rename(bak, old, ec);
          if (ec) REXLOG_WARN("dbz3: Nova's Mods: no se pudo devolver {} (sigue en mods_respaldo)", rex::path_to_utf8(old));
        }
      }
      if (error.empty() && was_disabled) SetModEnabled(name, false);
      // la otra version del mismo mod (pack HD V1 / V2) se aparta: dos a la vez se pisan las texturas
      for (const std::string& other : error.empty() ? mod.excluye : std::vector<std::string>{}) {
        const bool plain = !other.empty() && other != mod.id &&
            std::all_of(other.begin(), other.end(), [](char c) { return std::isalnum(uint8_t(c)) || c == '_' || c == '-'; });
        for (const std::string& dir : {other, other + ".disabled"}) {
          if (!plain || !fs::is_directory(ModsRoot() / dir, ec)) continue;
          fs::create_directories(ExeDir() / "mods_respaldo", ec);
          fs::rename(ModsRoot() / dir, ExeDir() / "mods_respaldo" / (dir + "_" + Stamp()), ec);
          REXLOG_INFO("dbz3: Nova's Mods: {} apartado a mods_respaldo ({})", dir, ec ? ec.message() : "ok");
        }
      }
    }
    fs::remove(zip, ec);   // el zip descargado es temporal (lo instalado queda en mods/)
    REXLOG_INFO("dbz3: Nova's Mods: {} {} -> {}", mod.id, mod.version, error.empty() ? "instalado" : error);
    std::lock_guard<std::mutex> lock(g_mu);
    g_busy.erase(mod.id);
    g_status = error.empty() ? mod.name + i18n::T(": instalado. Se aplica al pulsar JUGAR.", ": installed. It applies when you press PLAY.")
                             : mod.name + ": " + error;
  }).detach();
}

}  // namespace dbz3::launcher
