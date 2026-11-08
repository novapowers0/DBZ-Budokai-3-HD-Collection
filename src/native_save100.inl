// Included by native_mods.cpp inside namespace dbz3. Only the two B3 package files
// are replaced: Hero/Tag Team/DU extra progress and other titles are never restored over.
namespace {
namespace fs = std::filesystem;

bool ProfileName(const fs::path& p) {
  const auto s = p.filename().string();
  return s.size() == 16 && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

std::vector<fs::path> NativeTargets(const fs::path& root, std::error_code& ec) {
  std::vector<fs::path> targets;
  if (!fs::exists(root, ec)) return targets;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!ProfileName(it->path()) || !it->is_directory(ec)) continue;
    targets.push_back(it->path().filename() / kTitle / "00000001" / "DBZ3" / "data.bin");
    targets.push_back(it->path().filename() / kTitle / "Headers" / "00000001" / "DBZ3.header");
  }
  return targets;
}

bool SaveWriteAllowed(bool in_game, std::string& message) {
  bool busy = in_game;
#ifdef _WIN32
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) busy = true;  // fail closed
  else {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) busy = true;
    else do {
      if (entry.th32ProcessID != GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"dbz3.exe") == 0) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t path[32768];
        DWORD length = DWORD(std::size(path));
        if (!process || !QueryFullProcessImageNameW(process, 0, path, &length)) busy = true;
        else {
          const auto folder = fs::path(path).parent_path();
          // Portable installations have independent saves. A Documents fallback is shared.
          if (folder == rex::filesystem::GetExecutableFolder() ||
              settings::UserDataRoot() != rex::filesystem::GetExecutableFolder() / "user_data" / "dbz3") busy = true;
        }
        if (process) CloseHandle(process);
      }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
  }
#elif defined(__linux__)
  std::error_code ec;
  for (fs::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
    const auto pid = it->path().filename().string();
    if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos || pid == std::to_string(getpid())) continue;
    std::ifstream comm(it->path() / "comm");
    std::string name;
    std::getline(comm, name);
    if (name == "dbz3" || name == "dbz3.exe") {
      std::error_code process_ec;
      const auto exe = fs::read_symlink(it->path() / "exe", process_ec);
      if (process_ec || exe.parent_path() == rex::filesystem::GetExecutableFolder() ||
          settings::UserDataRoot() != rex::filesystem::GetExecutableFolder() / "user_data" / "dbz3") busy = true;
    }
  }
  if (ec) busy = true;
#endif
  if (busy) {
    message = i18n::T("Cierra el juego y las otras ventanas de dbz3 antes de cambiar la partida. No se ha cambiado nada.",
                      "Close the game and other dbz3 windows before changing the save. Nothing was changed.");
    REXLOG_INFO("dbz3: save_100 blocked: game/other instance running");
  }
  return !busy;
}

void SaveMarker(const fs::path& path, std::error_code& ec) {
  if (ec) return;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return;
  std::ofstream out(path, std::ios::binary);
  out << "save100-v2\n";
  out.close();
  if (!out) ec = std::make_error_code(std::errc::io_error);
  if (!ec) FlushSaveFile(path, ec);
}

// Bind every file (including absence markers) to its original bytes before
// publishing a complete snapshot. This is an accidental-corruption checksum.
bool SnapshotChecksum(const fs::path& file, bool write, std::error_code& ec) {
  const auto size = fs::file_size(file, ec);
  if (ec) {
    if (!write) ec = std::make_error_code(std::errc::illegal_byte_sequence);
    return false;
  }
  const auto hash = rex::hash_file(file);
  if (hash.empty()) {
    ec = std::make_error_code(write ? std::errc::io_error : std::errc::illegal_byte_sequence);
    return false;
  }
  auto sidecar = file;
  sidecar += ".hash";
  if (write) {
    std::ofstream out(sidecar, std::ios::binary);
    out << size << ' ' << hash << '\n';
    out.close();
    if (!out) ec = std::make_error_code(std::errc::io_error);
    if (!ec) FlushSaveFile(sidecar, ec);
  } else {
    std::ifstream in(sidecar);
    uintmax_t expected_size = 0;
    std::string expected_hash;
    if (!(in >> expected_size >> expected_hash) || size != expected_size || hash != expected_hash)
      ec = std::make_error_code(std::errc::illegal_byte_sequence);
  }
  return !ec;
}

bool ValidateSnapshot(const fs::path& from, const std::vector<fs::path>& targets, std::error_code& ec) {
  if (!fs::is_regular_file(from / "complete", ec) || targets.empty()) {
    ec = std::make_error_code(std::errc::illegal_byte_sequence);
    return false;
  }
  if (!SnapshotChecksum(from / "targets", false, ec)) return false;
  std::ifstream inventory(from / "targets");
  std::string rel_name;
  size_t count = 0;
  while (inventory >> rel_name) {
    if (std::find(targets.begin(), targets.end(), fs::path(rel_name)) == targets.end()) {
      ec = std::make_error_code(std::errc::illegal_byte_sequence);
      return false;
    }
    ++count;
  }
  if (!inventory.eof() || count != targets.size()) {
    ec = std::make_error_code(std::errc::illegal_byte_sequence);
    return false;
  }
  for (const auto& rel : targets) {
    auto absent = from / rel;
    absent += ".absent";
    const bool missing = fs::exists(absent, ec);
    if (ec) return false;
    // A file and an absence marker together are contradictory.
    if (missing && fs::exists(from / rel, ec)) ec = std::make_error_code(std::errc::illegal_byte_sequence);
    if (ec || !SnapshotChecksum(missing ? absent : from / rel, false, ec)) return false;
  }
  return true;
}

// Each call gets a unique, exclusively-created folder, including on the first run.
fs::path SnapshotSave(const std::vector<fs::path>& targets, const fs::path& base, std::error_code& ec) {
  fs::create_directories(base, ec);
  if (ec) return {};
  const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto dest = base / ("v2_" + std::to_string(ticks));
  if (!fs::create_directory(dest, ec)) {
    if (!ec) ec = std::make_error_code(std::errc::file_exists);
    return {};
  }
  for (const auto& rel : targets) {
    const auto source = settings::UserDataRoot() / rel;
    const bool exists = fs::exists(source, ec);
    if (ec) break;
    if (exists) CopyAtomic(source, dest / rel, ec);
    else {
      auto absent = dest / rel;
      absent += ".absent";
      SaveMarker(absent, ec);
    }
    if (!ec) {
      auto saved = dest / rel;
      if (!exists) saved += ".absent";
      SnapshotChecksum(saved, true, ec);
      if (!ec && exists && rex::hash_file(source) != rex::hash_file(saved))
        ec = std::make_error_code(std::errc::io_error);
    }
    if (ec) break;
  }
  if (!ec) {
    std::ofstream inventory(dest / "targets", std::ios::binary);
    for (const auto& rel : targets) inventory << rel.generic_string() << '\n';
    inventory.close();
    if (!inventory) ec = std::make_error_code(std::errc::io_error);
    if (!ec) FlushSaveFile(dest / "targets", ec);
    if (!ec) SnapshotChecksum(dest / "targets", true, ec);
  }
  SaveMarker(dest / "complete", ec);
  return dest;
}

// Absence is a real state. Move newly created files aside instead of deleting them.
bool PutSnapshot(const fs::path& from, const std::vector<fs::path>& targets,
                 const fs::path& safety, std::error_code& ec) {
  for (const auto& rel : targets) {
    auto absent = from / rel;
    absent += ".absent";
    if (fs::exists(absent, ec)) {
      if (fs::exists(settings::UserDataRoot() / rel, ec)) {
        const auto moved = safety / "apartados" / rel;
        fs::create_directories(moved.parent_path(), ec);
        if (!ec) fs::rename(settings::UserDataRoot() / rel, moved, ec);
      }
    } else if (!ec) {
      CopyAtomic(from / rel, settings::UserDataRoot() / rel, ec);
    }
    if (ec) return false;
  }
  return true;
}

fs::path LastSaveBackup(std::error_code& ec) {
  fs::path last;
  if (!fs::exists(BackupRoot(), ec)) return last;
  for (fs::directory_iterator it(BackupRoot(), ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    const auto name = it->path().filename().string();
    if (name.starts_with("v2_")) {
      const bool applied = fs::exists(it->path() / "applied", ec);
      const bool pending = fs::exists(it->path() / "pending", ec) && !fs::exists(it->path() / "rolled_back", ec);
      if (!applied && !pending) continue;
    } else {
      // Backups made by older releases: restore only known B3 files, not the profile tree.
      if (name.size() != 15 || name[8] != '_') continue;
      if (NativeTargets(it->path(), ec).empty()) continue;
    }
    if (last.empty() || it->path().filename() > last.filename()) last = it->path();
  }
  return last;
}

bool SaveFailure(const fs::path& backup, std::error_code ec, std::string& message) {
  message = std::string(i18n::T("No se pudo cambiar la partida. Copia de seguridad: ",
                                "Could not change the save. Backup: ")) + rex::path_to_utf8(backup) + "\n" + ec.message();
  if (ec == std::errc::illegal_byte_sequence) {
    message += "\n";
    message += i18n::T("El respaldo está dañado o no se puede verificar. No se ha restaurado. Conserva las copias y recupera un respaldo sano.",
                        "The backup is damaged or cannot be verified. It was not restored. Keep the copies and recover a healthy backup.");
  } else if (ec == std::errc::no_space_on_device) {
    message += "\n";
    message += i18n::T("No hay espacio suficiente. Libera espacio y usa Restaurar mi partida antes de volver a intentarlo.",
                        "Not enough disk space. Free some space and use Restore my save before trying again.");
  } else {
    message += "\n";
    message += i18n::T("Comprueba el espacio libre y cierra programas que bloqueen los archivos. Conserva la copia indicada; usa Restaurar mi partida si el cambio quedó interrumpido.",
                        "Check free disk space and close programs locking the files. Keep the indicated backup; use Restore my save if the change was interrupted.");
  }
  REXLOG_ERROR("dbz3: save_100 failed: {}", message);
  return false;
}
}  // namespace

bool ApplySave100(std::string& message, bool in_game) {
  if (!SaveWriteAllowed(in_game, message)) return false;
  if (!Save100Available()) {
    message = i18n::T("Falta la carpeta mods_nativos\\partida_100 junto a dbz3.exe: vuelve a instalar la version.",
                      "The mods_nativos\\partida_100 folder next to dbz3.exe is missing: reinstall the release.");
    return false;
  }
  std::error_code ec;
  const auto unfinished = LastSaveBackup(ec);
  if (ec) return SaveFailure({}, ec, message);
  if (!unfinished.empty() && fs::exists(unfinished / "pending", ec) &&
      !fs::exists(unfinished / "applied", ec) && !fs::exists(unfinished / "rolled_back", ec)) {
    message = i18n::T("El cambio de partida quedó interrumpido. Pulsa Restaurar mi partida antes de desbloquear de nuevo.",
                       "The save change was interrupted. Click Restore my save before unlocking again.");
    REXLOG_WARN("dbz3: save_100 interrupted: {}", rex::path_to_utf8(unfinished));
    return false;
  }
  if (ec) return SaveFailure(unfinished, ec, message);
  auto targets = NativeTargets(settings::UserDataRoot(), ec);
  if (ec) return SaveFailure({}, ec, message);
  if (targets.empty()) {
    targets.push_back(fs::path(kProfile) / kTitle / "00000001" / "DBZ3" / "data.bin");
    targets.push_back(fs::path(kProfile) / kTitle / "Headers" / "00000001" / "DBZ3.header");
  }
  const auto backup = SnapshotSave(targets, BackupRoot(), ec);
  if (ec) return SaveFailure(backup, ec, message);  // no writes until ALL originals are backed up
  SaveMarker(backup / "pending", ec);  // recovery is discoverable BEFORE the first replacement
  if (ec) return SaveFailure(backup, ec, message);
  for (const auto& rel : targets) {
    if (!CopyAtomic(TemplateDir() / rel.filename(), settings::UserDataRoot() / rel, ec)) break;
  }
  SaveMarker(backup / "applied", ec);
  if (ec) {
    const auto cause = ec;
    ec.clear();
    PutSnapshot(backup, targets, backup / "rollback", ec);
    if (!ec) SaveMarker(backup / "rolled_back", ec);
    REXLOG_WARN("dbz3: save_100 rollback: {}", ec ? ec.message() : "ok");
    return SaveFailure(backup, cause, message);
  }
  message = std::string(i18n::T("Todo desbloqueado. Héroe y Tag Team conservados. Copia de seguridad: ",
                                "Everything unlocked. Hero and Tag Team preserved. Backup: ")) + rex::path_to_utf8(backup);
  REXLOG_INFO("dbz3: save_100 applied: {}", rex::path_to_utf8(backup));
  return true;
}

bool RestoreLastSaveBackup(std::string& message, bool in_game) {
  if (!SaveWriteAllowed(in_game, message)) return false;
  std::error_code ec;
  const auto last = LastSaveBackup(ec);
  if (ec) return SaveFailure({}, ec, message);
  if (last.empty()) {
    message = i18n::T("No hay ninguna copia de partida que restaurar.", "There is no save backup to restore.");
    return false;
  }
  auto targets = NativeTargets(last, ec);
  // Old backups can lack a header: do not invent or erase files in that case.
  if (!last.filename().string().starts_with("v2_")) {
    std::erase_if(targets, [&](const auto& rel) { return !fs::is_regular_file(last / rel, ec); });
  }
  if (ec || targets.empty()) return SaveFailure(last, ec ? ec : std::make_error_code(std::errc::invalid_argument), message);
  // Validate ALL files before touching even the first destination. Old backups
  // without checksums stay on disk for manual recovery, never silently trusted.
  if (!ValidateSnapshot(last, targets, ec)) return SaveFailure(last, ec, message);
  const auto safety = SnapshotSave(targets, BackupRoot() / "antes_restaurar", ec);
  if (ec) return SaveFailure(safety, ec, message);
  if (!PutSnapshot(last, targets, safety, ec)) {
    const auto cause = ec;
    ec.clear();
    PutSnapshot(safety, targets, safety / "rollback", ec);
    REXLOG_WARN("dbz3: save_100 restore rollback: {}", ec ? ec.message() : "ok");
    return SaveFailure(safety, cause, message);
  }
  if (!fs::exists(last / "applied", ec)) SaveMarker(last / "rolled_back", ec);
  if (ec) return SaveFailure(safety, ec, message);
  message = std::string(i18n::T("Partida restaurada. La partida sustituida también se ha guardado en: ",
                                "Save restored. The replaced save was also backed up in: ")) + rex::path_to_utf8(safety);
  REXLOG_INFO("dbz3: save_100 restored: {} (current backed up: {})", rex::path_to_utf8(last), rex::path_to_utf8(safety));
  return true;
}
