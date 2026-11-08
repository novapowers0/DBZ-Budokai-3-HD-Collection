#include "native_mods.h"

#include "launcher/i18n.h"
#include "launcher/settings.h"

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/hash.h>

#include <chrono>
#include <ctime>

#include <fstream>
#include <cstring>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace dbz3 {
namespace {

constexpr char kSpfMagic[] = "#SPF 1.0";

// Textos en el idioma del launcher (i18n::T): se construye en cada llamada porque el idioma
// se puede cambiar en caliente.
std::vector<NativeModInfo> Catalog() {
  using i18n::T;
  return {
      {"save_100", T("Desbloquear todo", "Unlock everything"),
       T("Instala una partida completa de Xbox 360 (todo desbloqueado). Tu partida se copia antes y se puede restaurar.",
         "Installs a complete Xbox 360 save (everything unlocked). Your save is backed up first and can be restored."),
       T("Progreso / guardado", "Progress / save"), NativeModState::kReady},
      {"infinite_health", T("Vida infinita", "Infinite health"),
       T("Mantiene la vida del jugador al maximo durante los combates.",
         "Keeps the player's health full during fights."),
       "Gameplay", NativeModState::kNoKnownPatch},
      {"infinite_ki", T("Ki infinito", "Infinite ki"),
       T("Evita que el ki del jugador disminuya durante los combates.",
         "Keeps the player's ki from going down during fights."),
       "Gameplay", NativeModState::kNoKnownPatch},
  };
}

constexpr char kProfile[] = "B13EBABEBABEBABE";
constexpr char kTitle[] = "4E4D0856";

std::filesystem::path TemplateDir() {
  return rex::filesystem::GetExecutableFolder() / "mods_nativos" / "partida_100";
}
std::filesystem::path BackupRoot() { return settings::UserDataRoot() / "respaldos_partida"; }

bool FlushSaveFile(const std::filesystem::path& path, std::error_code& ec) {
#ifdef _WIN32
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) ec = std::error_code(GetLastError(), std::system_category());
  else {
    if (!FlushFileBuffers(h)) ec = std::error_code(GetLastError(), std::system_category());
    CloseHandle(h);
  }
#else
  const int fd = open(path.c_str(), O_WRONLY);
  if (fd < 0) ec = std::error_code(errno, std::generic_category());
  else {
    if (fsync(fd)) ec = std::error_code(errno, std::generic_category());
    close(fd);
  }
#endif
  return !ec;
}

bool CopyAtomic(const std::filesystem::path& from, const std::filesystem::path& to,
                std::error_code& ec) {
  std::filesystem::create_directories(to.parent_path(), ec);
  if (ec) return false;
  auto tmp = to;
  tmp += ".tmp";
  // Only the official UI test harness can inject a one-shot partial write.
  // No test variable / request means the normal filesystem path below.
  if (std::getenv("DBZ3_TEST_SAVE100_UI") && std::getenv("DBZ3_TEST_SAVE100_FAULTS") &&
      to.filename() == "DBZ3.header" &&
      to.lexically_relative(settings::UserDataRoot()).generic_string().find("respaldos_partida") == std::string::npos) {
    const auto request = rex::filesystem::GetExecutableFolder() / "dbz3_save100_fault.req";
    std::ifstream input(request);
    std::string mode;
    input >> mode;
    input.close();
    if (mode == "quota" || mode == "cut") {
      auto used = request;
      used += ".used_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      std::filesystem::rename(request, used, ec);
      if (ec) return false;
      std::ifstream source(from, std::ios::binary);
      std::string half(std::filesystem::file_size(from, ec) / 2, '\0');
      if (ec) return false;
      source.read(half.data(), half.size());
      std::ofstream output(tmp, std::ios::binary);
      output.write(half.data(), half.size());
      output.close();
      FlushSaveFile(tmp, ec);
      REXLOG_WARN("dbz3: save_100 injected {} after partial header write", mode);
      if (mode == "cut") std::_Exit(73);
      ec = std::make_error_code(std::errc::no_space_on_device);
      return false;
    }
  }
  std::filesystem::copy_file(from, tmp, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) return false;
  if (!FlushSaveFile(tmp, ec)) return false;
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    ec = std::error_code(GetLastError(), std::system_category());
#else
  std::filesystem::rename(tmp, to, ec);
#endif
  return !ec;
}

}  // namespace

bool Save100Available() {
  std::error_code ec;
  return IsSupportedNativeSave(TemplateDir() / "data.bin") &&
         std::filesystem::is_regular_file(TemplateDir() / "DBZ3.header", ec) &&
         std::filesystem::file_size(TemplateDir() / "DBZ3.header", ec) > 0 && !ec;
}

#include "native_save100.inl"

std::vector<NativeModInfo> NativeModCatalog() { return Catalog(); }

std::vector<std::filesystem::path> FindNativeSaveFiles() {
  std::vector<std::filesystem::path> result;
  std::error_code ec;
  for (const auto& rel : NativeTargets(settings::UserDataRoot(), ec))
    if (rel.filename() == "data.bin" && std::filesystem::is_regular_file(settings::UserDataRoot() / rel, ec))
      result.push_back(settings::UserDataRoot() / rel);
  return result;
}

bool IsSupportedNativeSave(const std::filesystem::path& save) {
  std::ifstream in(save, std::ios::binary);
  if (!in.is_open()) return false;
  char magic[sizeof(kSpfMagic) - 1] = {};
  in.read(magic, sizeof(magic));
  return in.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
         std::memcmp(magic, kSpfMagic, sizeof(magic)) == 0;
}

bool BackupNativeSave(const std::filesystem::path& save,
                      std::filesystem::path& backup,
                      std::string& error) {
  error.clear();
  if (!std::filesystem::is_regular_file(save)) {
    error = i18n::T("No se encontro el guardado.", "The save was not found.");
    return false;
  }
  if (!IsSupportedNativeSave(save)) {
    error = i18n::T("El guardado no tiene un formato reconocido (#SPF).", "The save has an unrecognized format (#SPF).");
    return false;
  }

  backup = save;
  backup += ".native.bak";
  std::error_code ec;
  std::filesystem::copy_file(save, backup,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    error = std::string(i18n::T("No se pudo crear la copia de seguridad: ", "Could not create the backup: ")) + ec.message();
    return false;
  }
  REXLOG_INFO("dbz3: native save backup created at {}", backup.string());
  return true;
}

}  // namespace dbz3
