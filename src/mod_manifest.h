#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace dbz3::mod_manifest {

inline std::string Value(const std::filesystem::path& path, const std::string& wanted) {
  std::ifstream in(path, std::ios::binary);
  std::string line, result;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      const size_t b = s.find_first_not_of(" \t");
      if (b == std::string::npos) { s.clear(); return; }
      const size_t e = s.find_last_not_of(" \t\r");
      s = s.substr(b, e - b + 1);
    };
    trim(key);
    trim(value);
    if (key == wanted) result = std::move(value);  // the newest entry wins
  }
  return result;
}

// Appends rather than replacing the manifest so the author's fields/comments survive.
inline bool AppendValue(const std::filesystem::path& path, const std::string& key, const std::string& value) {
  if (Value(path, key) == value) return true;
  std::ifstream in(path, std::ios::binary);
  const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) return false;
  if (!old.empty() && old.back() != '\n') out << '\n';
  out << key << '=' << value << '\n';
  return bool(out);
}

}  // namespace dbz3::mod_manifest
