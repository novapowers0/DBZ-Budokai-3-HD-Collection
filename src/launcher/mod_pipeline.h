// dbz3 - Model swap pipeline integration (project-side, no SDK changes).
//
// Wraps the validated Python swap script (mod center hd/swap_b3.py) so the
// launcher can scan the B3 character catalog and run model swaps (B3 HD ->
// B3 HD) from the UI. Python is invoked asynchronously; output is captured.

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dbz3::launcher {

// One character entry from the B3 catalog (catalog_b3.cat).
struct B3Char {
  int bin = 0;        // AFS entry (bin) of the model
  std::string label;  // e.g. "XGTN_BODY"
  std::string name;   // friendly name, e.g. "Goten"
  std::string variant;  // outfit/transformation ("" for main)
  bool playable = true;
  std::string note;

  std::string DisplayName() const {
    if (variant.empty()) return name;
    return name + " (" + variant + ")";
  }
};

class ModPipeline {
 public:
  // Reads the cached B3 catalog from disk. Returns false if missing.
  bool LoadCatalog();

  const std::vector<B3Char>& B3() const { return b3_; }
  bool CatalogLoaded() const { return loaded_; }

  // Asynchronous operation. Poll IsRunning() to know when done; read the
  // output via Output().
  void SwapB3ToB3(const B3Char& src, const B3Char& dst);

  // Texture mod pipeline (texture_b3.py): extract a character's textures as
  // editable PNGs into mods/<mod>/textures/, then rebuild the mod from the
  // edited PNGs. Both are asynchronous.
  void ExtractTextures(const B3Char& src, const std::string& mod_name,
                       const std::string& dir = "");
  void BuildTextures(const std::string& mod_name, int dest_slot = -1,
                     const std::string& dir = "");

  // Path to the data_cmn.afs to operate on (the model source/destination).
  // Auto-detected by default; the launcher can override it if the user picks
  // a custom location.
  void SetAfsPath(const std::string& path);
  std::string AfsPath() const;

  bool IsRunning() const { return running_.load(); }
  std::string Output() const;

 private:
  void RunAsync(const std::filesystem::path& script,
                const std::vector<std::string>& args);
  void AppendOutput(const std::string& text);
  std::vector<std::string> SwapArgs(const B3Char& src, const B3Char& dst,
                                    const std::string& mod) const;
  std::vector<std::string> TextureArgs(const B3Char& src,
                                       const std::string& mod,
                                       const std::string& dir) const;
  std::vector<std::string> BuildTextureArgs(const std::string& mod,
                                          int dest_slot,
                                          const std::string& dir) const;

  std::vector<B3Char> b3_;
  bool loaded_ = false;
  std::atomic<bool> running_{false};
  mutable std::mutex mutex_;
  std::string output_;
  std::string afs_path_;
  std::thread worker_;
};

}  // namespace dbz3::launcher