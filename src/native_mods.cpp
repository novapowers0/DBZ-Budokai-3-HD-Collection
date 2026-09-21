#include "native_mods.h"

#include "launcher/settings.h"

#include <rex/logging.h>

#include <fstream>
#include <cstring>

namespace dbz3 {
namespace {

constexpr char kSpfMagic[] = "#SPF 1.0";

const std::vector<NativeModInfo> kCatalog = {
    {"save_100", "Guardar al 100%",
     "Desbloquea objetos, tecnicas y contenido permanente sin tocar los archivos del juego.",
     "Progreso / guardado", NativeModState::kNeedsResearch},
    {"infinite_health", "Vida infinita",
     "Mantiene la vida del jugador al maximo durante los combates.",
     "Gameplay", NativeModState::kNoKnownPatch},
    {"infinite_ki", "Ki infinito",
     "Evita que el ki del jugador disminuya durante los combates.",
     "Gameplay", NativeModState::kNoKnownPatch},
};

bool IsDataFile(const std::filesystem::path& path) {
  return path.filename() == "data.bin" && std::filesystem::is_regular_file(path);
}

}  // namespace

const std::vector<NativeModInfo>& NativeModCatalog() { return kCatalog; }

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
    error = "No se encontro el guardado.";
    return false;
  }
  if (!IsSupportedNativeSave(save)) {
    error = "El guardado no tiene un formato reconocido (#SPF).";
    return false;
  }

  backup = save;
  backup += ".native.bak";
  std::error_code ec;
  std::filesystem::copy_file(save, backup,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "No se pudo crear la copia de seguridad: " + ec.message();
    return false;
  }
  REXLOG_INFO("dbz3: native save backup created at {}", backup.string());
  return true;
}

}  // namespace dbz3
