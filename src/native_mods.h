// dbz3 - Native gameplay/progression mod catalog.
//
// Native mods are deliberately separate from AFS asset overrides. They operate
// on the guest/runtime or on the game's own save data, never on models/textures.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbz3 {

enum class NativeModState {
  kReady,
  kNeedsResearch,
  kNoKnownPatch,
};

struct NativeModInfo {
  std::string id;
  std::string name;
  std::string description;
  std::string scope;
  NativeModState state = NativeModState::kNeedsResearch;
};

std::vector<NativeModInfo> NativeModCatalog();

// Finds the game's native content packages below the configured user-data root.
// No files are changed.
std::vector<std::filesystem::path> FindNativeSaveFiles();

// Makes a non-destructive backup beside a save file. Returns false and explains
// the reason when the source is missing or the backup cannot be written.
bool BackupNativeSave(const std::filesystem::path& save,
                      std::filesystem::path& backup,
                      std::string& error);

// A save currently observed in this port is an SPF container. Keep this check
// explicit so future native transformers cannot silently edit an unknown file.
bool IsSupportedNativeSave(const std::filesystem::path& save);

// "Partida al 100%": template shipped in <exe>/mods_nativos/partida_100
// (data.bin + DBZ3.header of a complete Xbox 360 save, same title 4E4D0856).
bool Save100Available();
// Copies the whole current profile folders to user_data/respaldos_partida/<date>
// and then installs the template atomically (temp file + rename).
bool ApplySave100(std::string& message);
// Puts back the most recent backup made by ApplySave100.
bool RestoreLastSaveBackup(std::string& message);

}  // namespace dbz3
