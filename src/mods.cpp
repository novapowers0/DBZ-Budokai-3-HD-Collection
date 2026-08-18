// dbz3 - Mod manager (project-side).

#include "mods.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <fstream>
#include <system_error>

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

}  // namespace dbz3