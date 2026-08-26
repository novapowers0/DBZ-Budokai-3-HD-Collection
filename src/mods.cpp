// dbz3 - Mod manager (project-side).

#include "mods.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
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
  if (info.type.empty()) {
    if (has_audio) info.type = "audio";
    else if (has_data) info.type = "data";
    else info.type = "other";
  }
}

}  // namespace

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
  return "other";
}

int ModTypeColor(const std::string& type) {
  if (type == "swap_b3" || type == "port_b3") return 0xFFB347;  // orange
  if (type == "audio") return 0x4FC3F7;                          // light blue
  if (type == "moveset") return 0x81C784;                        // green
  if (type == "data") return 0xCFD8DC;                           // gray-blue
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

#else  // !_WIN32

bool InstallModFromZip(const std::string&, std::string&, std::string& err) {
  err = "no soportado en esta plataforma";
  return false;
}

#endif  // _WIN32

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