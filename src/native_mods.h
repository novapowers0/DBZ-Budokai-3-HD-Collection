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

const std::vector<NativeModInfo>& NativeModCatalog();

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

}  // namespace dbz3
