#include "native_mods.h"

#include "launcher/i18n.h"
#include "launcher/settings.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <chrono>
#include <ctime>

#include <fstream>
#include <cstring>

namespace dbz3 {
namespace {

constexpr char kSpfMagic[] = "#SPF 1.0";

// Textos en el idioma del launcher (i18n::T): se construye en cada llamada porque el idioma
// se puede cambiar en caliente.
std::vector<NativeModInfo> Catalog() {
  using i18n::T;
  return {
      {"save_100", T("Guardar al 100%", "100% save"),
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

bool IsDataFile(const std::filesystem::path& path) {
  return path.filename() == "data.bin" && std::filesystem::is_regular_file(path);
}

constexpr char kProfile[] = "B13EBABEBABEBABE";
constexpr char kTitle[] = "4E4D0856";

std::filesystem::path TemplateDir() {
  return rex::filesystem::GetExecutableFolder() / "mods_nativos" / "partida_100";
}
std::filesystem::path BackupRoot() { return settings::UserDataRoot() / "respaldos_partida"; }

bool CopyAtomic(const std::filesystem::path& from, const std::filesystem::path& to,
                std::error_code& ec) {
  std::filesystem::create_directories(to.parent_path(), ec);
  if (ec) return false;
  auto tmp = to;
  tmp += ".tmp";
  std::filesystem::copy_file(from, tmp, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) return false;
  std::filesystem::rename(tmp, to, ec);
  return !ec;
}

}  // namespace

bool Save100Available() {
  return IsSupportedNativeSave(TemplateDir() / "data.bin") &&
         std::filesystem::is_regular_file(TemplateDir() / "DBZ3.header");
}

bool ApplySave100(std::string& message) {
  if (!Save100Available()) {
    message = i18n::T("Falta la partida al 100% en mods_nativos/partida_100.", "The 100% save is missing from mods_nativos/partida_100.");
    return false;
  }
  const auto root = settings::UserDataRoot();
  std::error_code ec;
  // 1. Backup of every profile folder (all content, not only data.bin).
  char stamp[32] = {};
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
  const auto dest = BackupRoot() / stamp;
  bool any = false;
  for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
    const auto name = e.path().filename().string();
    if (!e.is_directory() || name.size() != 16) continue;
    std::filesystem::create_directories(dest, ec);
    std::filesystem::copy(e.path(), dest / name, std::filesystem::copy_options::recursive, ec);
    if (ec) {
      message = std::string(i18n::T("No se pudo copiar tu partida; no se ha cambiado nada: ", "Could not back up your save; nothing was changed: ")) + ec.message();
      return false;
    }
    any = true;
  }
  // 2. Install into each existing profile (or the default one).
  std::vector<std::filesystem::path> profiles;
  for (const auto& e : std::filesystem::directory_iterator(root, ec))
    if (e.is_directory() && e.path().filename().string().size() == 16 &&
        std::filesystem::is_directory(e.path() / kTitle))
      profiles.push_back(e.path());
  if (profiles.empty()) profiles.push_back(root / kProfile);
  for (const auto& p : profiles) {
    if (!CopyAtomic(TemplateDir() / "data.bin", p / kTitle / "00000001" / "DBZ3" / "data.bin", ec) ||
        !CopyAtomic(TemplateDir() / "DBZ3.header", p / kTitle / "Headers" / "00000001" / "DBZ3.header", ec)) {
      message = std::string(i18n::T("Error al instalar la partida: ", "Error installing the save: ")) + ec.message();
      return false;
    }
  }
  message = any ? std::string(i18n::T("Partida al 100% instalada. Copia de tu partida: ",
                                      "100% save installed. Backup of your save: ")) + dest.string()
                : std::string(i18n::T("Partida al 100% instalada (no habia partida previa).",
                                      "100% save installed (there was no previous save)."));
  REXLOG_INFO("dbz3: save_100 applied ({})", message);
  return true;
}

bool RestoreLastSaveBackup(std::string& message) {
  std::error_code ec;
  std::filesystem::path last;
  for (const auto& e : std::filesystem::directory_iterator(BackupRoot(), ec))
    if (e.is_directory() && (last.empty() || e.path().filename() > last.filename())) last = e.path();
  if (last.empty()) {
    message = i18n::T("No hay ninguna copia de partida que restaurar.", "There is no save backup to restore.");
    return false;
  }
  const auto root = settings::UserDataRoot();
  for (const auto& e : std::filesystem::directory_iterator(last, ec)) {
    std::filesystem::copy(e.path(), root / e.path().filename(),
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      message = std::string(i18n::T("No se pudo restaurar: ", "Could not restore: ")) + ec.message();
      return false;
    }
  }
  message = std::string(i18n::T("Partida restaurada desde ", "Save restored from ")) + last.string();
  return true;
}

std::vector<NativeModInfo> NativeModCatalog() { return Catalog(); }

std::vector<std::filesystem::path> FindNativeSaveFiles() {
  std::vector<std::filesystem::path> result;
  const auto root = settings::UserDataRoot();
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) return result;

  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (!it->is_regular_file(ec) || !IsDataFile(it->path())) continue;
    result.push_back(it->path());
  }
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
