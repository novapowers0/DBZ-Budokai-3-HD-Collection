// Preferencias independientes de las partidas/mods; publicación recuperable.
#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dbz3::forms {
using Preferences = std::map<std::string, int>;
inline Preferences ParsePreferences(std::istream& in) {
  std::string line;
  if (!std::getline(in, line) || line != "DBZ3_FORMS_1") throw std::runtime_error("version de preferencias desconocida");
  Preferences result;
  size_t total = line.size();
  while (std::getline(in, line)) {
    total += line.size();
    if (total > 1048576 || result.size() >= 4096) throw std::runtime_error("preferencias demasiado grandes");
    std::istringstream row(line);
    std::string key, tail; int value = -1;
    if (!(row >> std::quoted(key) >> value) || (row >> tail) || key.empty() || key.size() > 512 ||
        key.find_first_of("\r\n\t") != std::string::npos || value < 0 || value >= 256 || !result.emplace(key, value).second)
      throw std::runtime_error("registro de preferencias no valido");
  }
  if (in.bad()) throw std::runtime_error("error de lectura de preferencias");
  return result;
}
inline std::string EncodePreferences(const Preferences& values) {
  std::ostringstream out; out << "DBZ3_FORMS_1\n";
  for (const auto& [key, value] : values) out << std::quoted(key) << ' ' << value << '\n';
  std::istringstream verify(out.str()); ParsePreferences(verify);
  return out.str();
}
// Clang 22.1.8 crashes in InstCombine for this filesystem publication routine
// at -O3. Keep the workaround local: this cold UI persistence path is not a
// game/render loop. Recheck the optimizer with a future compiler before removal.
#if defined(__clang__)
__attribute__((optnone))
#endif
inline void SavePreferences(const std::filesystem::path& path, const Preferences& values) {
  namespace fs = std::filesystem;
  const auto text = EncodePreferences(values); // validar antes de tocar ningún destino
  fs::create_directories(path.parent_path());
  if (fs::is_symlink(path) || (fs::exists(path) && !fs::is_regular_file(path)))
    throw std::runtime_error("destino de preferencias no es fichero regular");
  const fs::path lock(path.string() + ".lock");
  if (!fs::create_directory(lock)) throw std::runtime_error("preferencias ocupadas o recuperacion pendiente");
  static std::atomic<unsigned> counter{0};
  fs::path op;
  bool moved = false, published = false;
  try {
    for (;;) {
      op = fs::path(path.string() + ".backup." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(counter++));
      if (fs::create_directory(op)) break;
    }
    { std::ofstream owner(lock / "owner.txt"); owner << op.filename().string(); owner.flush(); if (!owner) throw std::runtime_error("dueno de operacion imposible"); }
    { std::ofstream journal(op / "journal.txt"); journal << path.filename().string() << "\npreparing\n"; journal.flush(); if (!journal) throw std::runtime_error("diario imposible"); }
    { std::ofstream out(op / "candidate.txt", std::ios::binary); out << text; out.flush(); if (!out) throw std::runtime_error("escritura de preferencias imposible"); }
    { std::ifstream in(op / "candidate.txt", std::ios::binary); if (ParsePreferences(in) != values) throw std::runtime_error("preferencias incompletas"); }
    if (fs::exists(path)) { fs::rename(path, op / "previous.txt"); moved = true; }
    // Publicación sin overwrite también en POSIX: conservar candidate y rechazar
    // cualquier destino creado por otro proceso entre los movimientos.
    fs::create_hard_link(op / "candidate.txt", path); published = true;
    { std::ofstream journal(op / "journal.txt", std::ios::app); journal << "published\n"; }
    fs::rename(lock, op / "lock.completed");
  } catch (...) {
    if (moved && !published && !fs::exists(path)) {
      std::error_code ec; fs::copy_file(op / "previous.txt", path, fs::copy_options::none, ec);
      if (ec) throw std::runtime_error("recuperacion pendiente; original conservado en respaldo");
    }
    std::error_code ec;
    if (!op.empty()) fs::rename(lock, op / "lock.failed", ec);
    else fs::remove(lock, ec); // solo bloqueo vacío propio anterior a crear diario
    throw;
  }
}
inline void RecoverPreferences(const std::filesystem::path& path,
                               const std::filesystem::path& operation) {
  namespace fs = std::filesystem;
  const fs::path lock(path.string() + ".lock");
  if (fs::weakly_canonical(operation.parent_path()) != fs::weakly_canonical(path.parent_path()) ||
      !operation.filename().string().starts_with(path.filename().string() + ".backup.") ||
      fs::is_symlink(operation) || fs::is_symlink(lock)) throw std::runtime_error("operacion de otra preferencia");
  std::string owner;
  { std::ifstream in(lock / "owner.txt"); std::getline(in, owner); }
  if (owner != operation.filename().string()) throw std::runtime_error("lock de otra operacion: no recuperar");
  auto read = [](const fs::path& p) {
    if (fs::is_symlink(p) || !fs::is_regular_file(p) || fs::file_size(p) > 1048576)
      throw std::runtime_error("fichero de recuperacion no valido");
    std::ifstream in(p, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("lectura de recuperacion fallida");
    return text;
  };
  const auto previous = operation / "previous.txt", candidate = operation / "candidate.txt";
  if (!fs::exists(path)) {
    read(previous);
    fs::copy_file(previous, path, fs::copy_options::none);
  } else {
    const auto current = read(path);
    const bool old = fs::exists(previous) && current == read(previous);
    const bool fresh = fs::exists(candidate) && current == read(candidate);
    if (!old && !fresh) throw std::runtime_error("destino cambiado: conservar lock y revisar");
  }
  fs::rename(lock, operation / "lock.recovered"); // conserva dueño y evidencia
}
} // namespace dbz3::forms
