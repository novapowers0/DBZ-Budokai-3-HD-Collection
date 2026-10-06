// dbz3 - Mod manager (project-side).

#include "mods.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <toml++/toml.hpp>
#include <system_error>

#if REX_PLATFORM_WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dbz3 {

std::filesystem::path ModsRoot() {
  // Mods live next to the executable (where the runtime's VFS resolves them),
  // matching dbz3::settings. The build output dir is not wiped for mods.
  return rex::filesystem::GetExecutableFolder() / "mods";
}

namespace {

bool EndsWithDotDisabled(const std::string& name) {
  const std::string suffix = ".disabled";
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StripDotDisabled(const std::string& name) {
  return EndsWithDotDisabled(name)
             ? name.substr(0, name.size() - std::string(".disabled").size())
             : name;
}

bool FolderHasDisabledMarker(const std::filesystem::path& dir) {
  return std::filesystem::exists(dir / ".disabled");
}

bool FolderIsDisabled(const std::filesystem::path& dir,
                      const std::string& name) {
  return EndsWithDotDisabled(name) || FolderHasDisabledMarker(dir);
}

// Reads a simple "key=value" manifest (one per line, '#' = comment).
void LoadManifest(const std::filesystem::path& dir, ModInfo& info) {
  std::ifstream in(dir / "manifest.txt");
  if (!in.is_open()) {
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      size_t b = 0;
      while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
      s.erase(0, b);
    };
    trim(key);
    trim(value);
    if (key == "name") info.display_name = value;
    else if (key == "description") info.description = value;
    else if (key == "author") info.author = value;
    else if (key == "version") info.version = value;
    else if (key == "type") info.type = value;
    else if (key == "source") info.source = value;
    else if (key == "target") info.target = value;
  }
}

// Infer a mod's type from its folder layout when no manifest is present, and
// count the files it overrides. Types:
//   swap_b3   -> a data_cmn.afs override (whole repacked AFS, model swap)
//   audio     -> any adx_*.afs override
//   data      -> anything else
void InferTypeAndCount(const std::filesystem::path& dir, ModInfo& info) {
  bool has_data = false, has_audio = false;
  int count = 0;
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string rel =
        rex::path_to_utf8(entry.path().lexically_relative(dir));
    ++count;
    if (rel.find("adx_") != std::string::npos &&
        (rel.find(".afs") != std::string::npos ||
         rel.find(".adx") != std::string::npos)) {
      has_audio = true;
    }
    if (rel.find("data_") != std::string::npos &&
        rel.find(".afs") != std::string::npos) {
      has_data = true;
    }
  }
  info.file_count = count;
  std::error_code ec2;
  if (std::filesystem::exists(dir / "personaje.toml", ec2)) {
    if (info.type.empty()) info.type = "personaje";
    if (info.display_name.empty()) {      // nombre del personaje (nombre = "...")
      std::ifstream pt(dir / "personaje.toml");
      std::string ln;
      while (std::getline(pt, ln)) {
        const auto eq = ln.find('=');
        if (eq == std::string::npos || ln.compare(0, 6, "nombre") != 0) continue;
        std::string v = ln.substr(eq + 1);
        v.erase(0, v.find_first_not_of(" \t\""));
        v.erase(v.find_last_not_of(" \t\"\r") + 1);
        info.display_name = v;
        break;
      }
    }
  }
  if (info.type.empty() && std::filesystem::exists(dir / "roster.toml", ec2)) {
    info.type = "generado";
  }
  if (info.type.empty()) {
    if (has_audio) info.type = "audio";
    else if (has_data) info.type = "data";
    else info.type = "other";
  }
}

}  // namespace

namespace {
// <hash16>_<W>x<H>_<formato> (nombre de textura de pack, el del volcado).
bool PackStem(const std::string& stem, uint32_t& w, uint32_t& h) {
  if (stem.size() < 20 || stem[16] != '_') return false;
  for (int i = 0; i < 16; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(stem[i]))) return false;
  }
  return std::sscanf(stem.c_str() + 17, "%ux%u", &w, &h) == 2 && w && h;
}

// Tamano real de un PNG (IHDR) o DDS (cabecera); false si no se puede leer.
bool ImageSize(const std::filesystem::path& p, uint32_t& w, uint32_t& h) {
  std::ifstream f(p, std::ios::binary);
  unsigned char b[24] = {};
  if (!f.read(reinterpret_cast<char*>(b), sizeof(b))) return false;
  auto be = [&](int o) { return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3]; };
  auto le = [&](int o) { return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24; };
  if (b[1] == 'P' && b[2] == 'N' && b[3] == 'G') { w = be(16); h = be(20); return true; }
  if (b[0] == 'D' && b[1] == 'D' && b[2] == 'S') { h = le(12); w = le(16); return true; }
  return false;
}

bool HasModContent(const std::filesystem::path& d) {
  std::error_code ec;
  for (const char* n : {"us", "eu", "personaje.toml", "traje.toml", "roster.toml", "manifest.txt"}) {
    if (std::filesystem::exists(d / n, ec)) return true;
  }
  uint32_t w, h;
  for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
    if (e.is_regular_file() && PackStem(e.path().stem().string(), w, h)) return true;
  }
  return false;
}

// Rutas relativas que pide un personaje.toml ("modelos/t1.bin"...) y no estan.
void MissingTomlFiles(const toml::node& n, const std::filesystem::path& dir, std::vector<std::string>& out) {
  if (const auto* t = n.as_table()) {
    for (const auto& [k, v] : *t) MissingTomlFiles(v, dir, out);
  } else if (const auto* a = n.as_array()) {
    for (const auto& v : *a) MissingTomlFiles(v, dir, out);
  } else if (const auto* s = n.as_string()) {
    const std::string& v = s->get();
    const auto dot = v.rfind('.');
    const bool looks_file = v.find(':') == std::string::npos && dot != std::string::npos &&
                            v.size() - dot <= 5 && v.find(' ') == std::string::npos &&
                            (v.find('/') != std::string::npos || v.find('\\') != std::string::npos);
    std::error_code ec;
    if (looks_file && !std::filesystem::exists(dir / std::filesystem::u8path(v), ec)) out.push_back(v);
  }
}

void CheckProblems(const std::filesystem::path& dir, ModInfo& info) {
  std::error_code ec;
  if (info.file_count == 0) {
    info.problems.emplace_back(ModInfo::kEmpty, "");
    return;
  }
  if (!HasModContent(dir)) {
    std::vector<std::filesystem::path> subs;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.is_directory()) subs.push_back(e.path());
    }
    if (subs.size() == 1 && HasModContent(subs[0])) {
      info.problems.emplace_back(ModInfo::kNested, rex::path_to_utf8(subs[0].filename()));
      return;
    }
  }
  for (const char* tn : {"personaje.toml", "traje.toml"}) {
    if (!std::filesystem::exists(dir / tn, ec)) continue;
    try {
      const toml::table t = toml::parse_file(rex::path_to_utf8(dir / tn));
      std::vector<std::string> missing;
      MissingTomlFiles(t, dir, missing);
      for (const auto& m : missing) info.problems.emplace_back(ModInfo::kMissingFile, m);
    } catch (const toml::parse_error& e) {
      info.problems.emplace_back(ModInfo::kBadToml, std::string(tn) + ": " + std::string(e.description()));
    }
  }
  // Pack de texturas: nombres que el juego no reconoce y tamanos que no son x1-x4 del original.
  int pack = 0, bad_name = 0;
  std::string bad_size;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (!e.is_regular_file()) continue;
    std::string ext = e.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".png" && ext != ".dds") continue;
    uint32_t w0, h0, w, h;
    if (!PackStem(e.path().stem().string(), w0, h0)) { ++bad_name; continue; }
    ++pack;
    if (bad_size.empty() && ImageSize(e.path(), w, h) &&
        (w % w0 || h % h0 || w / w0 != h / h0 || w / w0 < 1 || w / w0 > 4)) {
      bad_size = rex::path_to_utf8(e.path().filename());
    }
  }
  if (pack && bad_name) info.problems.emplace_back(ModInfo::kBadTextureName, std::to_string(bad_name));
  if (!bad_size.empty()) info.problems.emplace_back(ModInfo::kBadTextureSize, bad_size);
}
}  // namespace

bool FixNestedMod(const std::string& mod_name) {
  const std::filesystem::path dir = ModsRoot() / mod_name;
  std::error_code ec;
  std::filesystem::path inner;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.is_directory()) {
      if (!inner.empty()) return false;
      inner = e.path();
    }
  }
  if (inner.empty() || !HasModContent(inner)) return false;
  for (const auto& e : std::filesystem::directory_iterator(inner, ec)) {
    const auto to = dir / e.path().filename();
    if (std::filesystem::exists(to, ec)) return false;   // nunca se pisa nada
    std::filesystem::rename(e.path(), to, ec);
    if (ec) return false;
  }
  std::filesystem::remove(inner, ec);   // ya vacia
  REXLOG_INFO("dbz3: mod '{}' sacado de su carpeta interior", mod_name);
  return true;
}

std::vector<ModInfo> ListMods() {
  std::vector<ModInfo> result;
  std::error_code ec;
  const std::filesystem::path mods_root = ModsRoot();
  if (!std::filesystem::is_directory(mods_root, ec)) {
    return {};
  }
  for (const auto& mod_entry : std::filesystem::directory_iterator(mods_root, ec)) {
    if (!mod_entry.is_directory()) {
      continue;
    }
    const std::string raw_name = rex::path_to_utf8(mod_entry.path().filename());
    if (EndsWithDotDisabled(raw_name) &&
        std::filesystem::exists(mods_root / StripDotDisabled(raw_name))) {
      continue;
    }
    ModInfo info;
    info.name = StripDotDisabled(raw_name);
    info.enabled = !FolderIsDisabled(mod_entry.path(), raw_name);
    LoadManifest(mod_entry.path(), info);
    InferTypeAndCount(mod_entry.path(), info);
    CheckProblems(mod_entry.path(), info);
    result.push_back(std::move(info));
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ModInfo& a, const ModInfo& b) {
                     if (a.enabled != b.enabled) {
                       return a.enabled;
                     }
                     return a.name < b.name;
                   });
  return result;
}

void SetModEnabled(const std::string& mod_name, bool enable) {
  const std::filesystem::path mods_root = ModsRoot();
  const std::filesystem::path mod_dir = mods_root / mod_name;
  const std::filesystem::path marker = mod_dir / ".disabled";
  std::error_code ec;
  if (enable) {
    std::filesystem::remove(marker, ec);
    const std::filesystem::path suffixed = mods_root / (mod_name + ".disabled");
    if (!std::filesystem::exists(mod_dir) &&
        std::filesystem::exists(suffixed)) {
      std::filesystem::rename(suffixed, mod_dir, ec);
    }
  } else {
    std::filesystem::create_directories(mod_dir, ec);
    std::ofstream marker_file(marker);
    marker_file << "disabled\n";
  }
  REXLOG_INFO("dbz3: mod '{}' {}", mod_name, enable ? "enabled" : "disabled");
}

namespace {

std::filesystem::path ModDir(const std::string& mod_name) {
  return ModsRoot() / StripDotDisabled(mod_name);
}

std::string ReadManifestValue(const std::filesystem::path& manifest,
                              const std::string& key) {
  std::ifstream in(manifest);
  if (!in.is_open()) {
    return "";
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string k = line.substr(0, eq);
    auto trim = [](std::string& s) {
      size_t b = 0;
      while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
      s.erase(0, b);
    };
    trim(k);
    if (k == key) {
      std::string v = line.substr(eq + 1);
      trim(v);
      return v;
    }
  }
  return "";
}

}  // namespace

std::string GetModManifestValue(const std::string& mod_name,
                                const std::string& key) {
  return ReadManifestValue(ModDir(mod_name) / "manifest.txt", key);
}

bool SetModManifestValue(const std::string& mod_name, const std::string& key,
                         const std::string& value) {
  const std::filesystem::path dir = ModDir(mod_name);
  const std::filesystem::path manifest = dir / "manifest.txt";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  std::vector<std::string> keep;
  if (std::filesystem::exists(manifest, ec)) {
    std::ifstream in(manifest);
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                               line.back() == '\t')) {
        line.pop_back();
      }
      const size_t eq = line.find('=');
      bool drop = false;
      if (eq != std::string::npos) {
        std::string k = line.substr(0, eq);
        auto trim = [](std::string& s) {
          size_t b = 0;
          while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
          s.erase(0, b);
        };
        trim(k);
        drop = (k == key);
      }
      if (!drop) keep.push_back(line);
    }
  }
  if (!value.empty()) {
    keep.push_back(key + "=" + value);
  }
  std::ofstream out(manifest);
  if (!out.is_open()) {
    REXLOG_WARN("dbz3: no se pudo escribir manifest de '{}'", mod_name);
    return false;
  }
  for (const auto& line : keep) {
    out << line << "\n";
  }
  REXLOG_INFO("dbz3: manifest '{}' key '{}' actualizada", mod_name, key);
  return true;
}

const char* ModTypeLabel(const std::string& type) {
  if (type == "swap_b3") return "swap B3";
  if (type == "port_b3") return "port B3";
  if (type == "audio") return "audio";
  if (type == "moveset") return "moveset";
  if (type == "data") return "data";
  if (type == "personaje") return "character";
  if (type == "generado") return "generated";
  return "other";
}

int ModTypeColor(const std::string& type) {
  if (type == "swap_b3" || type == "port_b3") return 0xFFB347;  // orange
  if (type == "audio") return 0x4FC3F7;                          // light blue
  if (type == "moveset") return 0x81C784;                        // green
  if (type == "data") return 0xCFD8DC;                           // gray-blue
  if (type == "personaje") return 0xCE93D8;                      // purple
  if (type == "generado") return 0x90A4AE;                       // slate
  return -1;  // dim gray
}

// ---------------------------------------------------------------------------
// Zip install + profiles
// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) |
                       uint32_t(data[i + 2]);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
    i += 3;
  }
  if (i + 1 == len) {
    const uint32_t v = uint32_t(data[i]) << 16;
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == len) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

// Runs a PowerShell script hidden (no console window) via -EncodedCommand,
// which is immune to quoting/encoding issues in the path strings. Blocks up to
// 60 s and returns whether the process exited 0.
bool RunHiddenPowerShell(const std::wstring& script) {
  std::vector<uint8_t> le;
  le.reserve(script.size() * 2);
  for (wchar_t c : script) {
    le.push_back(uint8_t(c & 0xFF));
    le.push_back(uint8_t((c >> 8) & 0xFF));
  }
  const std::string b64 = Base64Encode(le.data(), le.size());
  std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand ";
  cmdline.append(b64.begin(), b64.end());

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  WaitForSingleObject(pi.hProcess, 60000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring out(len > 1 ? len - 1 : 0, L'\0');
  if (len > 1) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
  }
  return out;
}

std::string SanitizeModName(const std::string& raw) {
  std::string s = raw;
  for (char& c : s) {
    if (c < 32 || std::strchr("\\/:*?\"<>|", c)) {
      c = '_';
    }
  }
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) {
    s.pop_back();
  }
  if (s.empty()) {
    s = "mod";
  }
  return s;
}

}  // namespace

bool InstallModFromZip(const std::string& zip_path_utf8, std::string& out_name,
                       std::string& out_error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path mods_root = ModsRoot();
  fs::create_directories(mods_root, ec);

  const std::wstring zip_wide = Utf8ToWide(zip_path_utf8);
  if (zip_wide.empty()) {
    out_error = "ruta del zip invalida";
    return false;
  }
  const fs::path zip_fs(zip_wide);
  const std::string base =
      SanitizeModName(rex::path_to_utf8(zip_fs.stem()));

  // Extract to a temp folder under mods/ (cleaned on failure).
  const fs::path temp_dir = mods_root / (".install_" + base);
  fs::remove_all(temp_dir, ec);
  const auto ps_quote = [](const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) {
      out += (c == L'\'') ? L"''" : std::wstring(1, c);
    }
    out += L"'";
    return out;
  };
  const std::wstring script =
      L"Expand-Archive -LiteralPath " + ps_quote(zip_wide) +
      L" -DestinationPath " + ps_quote(temp_dir.wstring()) + L" -Force";
  if (!RunHiddenPowerShell(script)) {
    fs::remove_all(temp_dir, ec);
    out_error = "Expand-Archive fallo (zip corrupto o protegido)";
    return false;
  }

  // Normalize the layout: if the archive wraps everything in a single folder
  // with no loose files at its root, unwrap that folder.
  fs::path base_dir = temp_dir;
  {
    bool loose_file = false;
    std::vector<fs::path> subdirs;
    for (const auto& e : fs::directory_iterator(temp_dir, ec)) {
      if (e.is_regular_file()) {
        loose_file = true;
      } else if (e.is_directory()) {
        subdirs.push_back(e.path());
      }
    }
    if (!loose_file && subdirs.size() == 1) {
      base_dir = subdirs[0];
    }
  }

  // Final name (suffix on collision).
  std::string name = base;
  fs::path dest = mods_root / name;
  int k = 2;
  while (fs::exists(dest, ec)) {
    dest = mods_root / (name + "_" + std::to_string(k++));
  }
  name = rex::path_to_utf8(dest.filename());

  std::error_code rv;
  fs::rename(base_dir, dest, rv);
  if (rv) {
    fs::copy(base_dir, dest, fs::copy_options::recursive, rv);
    if (rv) {
      out_error = "no se pudieron mover los archivos extraidos";
      fs::remove_all(temp_dir, ec);
      return false;
    }
    fs::remove_all(base_dir, ec);
  }
  fs::remove_all(temp_dir, ec);

  out_name = name;
  REXLOG_INFO("dbz3: mod instalado desde zip -> '{}'", name);
  return true;
}

#else  // !REX_PLATFORM_WIN32

namespace {

std::string SanitizeModName(const std::string& raw) {
  std::string s = raw;
  for (char& c : s) {
    if (c < 32 || c == '/' || c == '\\') c = '_';
  }
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
  return s.empty() ? "mod" : s;
}

bool ExtractZip(const std::filesystem::path& zip,
                const std::filesystem::path& destination) {
  const std::string program = "unzip";
  const std::string zip_arg = zip.string();
  const std::string dest_arg = destination.string();
  std::vector<char*> argv = {
      const_cast<char*>(program.c_str()), const_cast<char*>("-q"),
      const_cast<char*>("-o"), const_cast<char*>(zip_arg.c_str()),
      const_cast<char*>("-d"), const_cast<char*>(dest_arg.c_str()), nullptr};
  pid_t pid = -1;
  if (posix_spawnp(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    return false;
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

bool InstallModFromZip(const std::string& zip_path_utf8, std::string& out_name,
                       std::string& out_error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path mods_root = ModsRoot();
  fs::create_directories(mods_root, ec);
  const fs::path zip = fs::u8path(zip_path_utf8);
  if (!fs::is_regular_file(zip, ec)) {
    out_error = "ruta del zip invalida";
    return false;
  }

  const std::string base = SanitizeModName(rex::path_to_utf8(zip.stem()));
  const fs::path temp_dir = mods_root / (".install_" + base);
  fs::remove_all(temp_dir, ec);
  fs::create_directories(temp_dir, ec);
  if (!ExtractZip(zip, temp_dir)) {
    fs::remove_all(temp_dir, ec);
    out_error = "no se pudo extraer el zip (instala unzip)";
    return false;
  }

  fs::path base_dir = temp_dir;
  bool loose_file = false;
  std::vector<fs::path> subdirs;
  for (const auto& entry : fs::directory_iterator(temp_dir, ec)) {
    if (entry.is_regular_file()) loose_file = true;
    else if (entry.is_directory()) subdirs.push_back(entry.path());
  }
  if (!loose_file && subdirs.size() == 1) base_dir = subdirs[0];

  std::string name = base;
  fs::path dest = mods_root / name;
  for (int suffix = 2; fs::exists(dest, ec); ++suffix) {
    dest = mods_root / (name + "_" + std::to_string(suffix));
  }
  name = rex::path_to_utf8(dest.filename());
  fs::rename(base_dir, dest, ec);
  if (ec) {
    ec.clear();
    fs::copy(base_dir, dest, fs::copy_options::recursive, ec);
    if (ec) {
      fs::remove_all(temp_dir, ec);
      out_error = "no se pudieron mover los archivos extraidos";
      return false;
    }
  }
  fs::remove_all(temp_dir, ec);
  out_name = name;
  REXLOG_INFO("dbz3: mod instalado desde zip -> '{}'", name);
  return true;
}

#endif  // REX_PLATFORM_WIN32

namespace {

std::filesystem::path ProfilesFile() { return ModsRoot() / "profiles.txt"; }

void LoadProfiles(std::map<std::string, std::vector<std::string>>& out) {
  out.clear();
  std::ifstream in(ProfilesFile());
  if (!in.is_open()) {
    return;
  }
  std::string line;
  std::string current;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      const size_t close = line.find(']');
      current = close == std::string::npos ? "" : line.substr(1, close - 1);
    } else if (!current.empty()) {
      out[current].push_back(line);
    }
  }
}

}  // namespace

std::vector<std::string> ListProfiles() {
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  std::vector<std::string> names;
  names.reserve(profiles.size());
  for (const auto& kv : profiles) {
    names.push_back(kv.first);
  }
  return names;
}

std::vector<std::string> ProfileEnabledMods(const std::string& profile) {
  if (profile == "vanilla") {
    return {};
  }
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  const auto it = profiles.find(profile);
  return it == profiles.end() ? std::vector<std::string>{} : it->second;
}

bool SaveProfile(const std::string& name,
                 const std::vector<std::string>& enabled_mods) {
  if (name.empty() || name == "vanilla") {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(ModsRoot(), ec);
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  profiles[name] = enabled_mods;

  std::ofstream out(ProfilesFile());
  if (!out.is_open()) {
    REXLOG_WARN("dbz3: no se pudo escribir profiles.txt");
    return false;
  }
  out << "# Perfiles de mods (generados por el launcher). 'vanilla' no se "
         "guarda.\n";
  for (const auto& kv : profiles) {
    out << "[" << kv.first << "]\n";
    for (const auto& mod : kv.second) {
      out << mod << "\n";
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' guardado ({} mods)", name, enabled_mods.size());
  return true;
}

bool DeleteProfile(const std::string& name) {
  if (name.empty() || name == "vanilla") {
    return false;
  }
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  if (profiles.erase(name) == 0) {
    return false;
  }
  std::ofstream out(ProfilesFile());
  if (!out.is_open()) {
    return false;
  }
  out << "# Perfiles de mods (generados por el launcher). 'vanilla' no se "
         "guarda.\n";
  for (const auto& kv : profiles) {
    out << "[" << kv.first << "]\n";
    for (const auto& mod : kv.second) {
      out << mod << "\n";
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' borrado", name);
  return true;
}

void ApplyProfile(const std::string& name) {
  const std::vector<std::string> want = ProfileEnabledMods(name);
  for (const ModInfo& mod : ListMods()) {
    const bool on = std::find(want.begin(), want.end(), mod.name) != want.end();
    if (mod.enabled != on) {
      SetModEnabled(mod.name, on);
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' aplicado ({} mods activos)", name, want.size());
}

}  // namespace dbz3
