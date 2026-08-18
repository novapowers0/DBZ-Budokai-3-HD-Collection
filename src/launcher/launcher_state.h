// dbz3 - Pre-game launcher screen (ImGui dialog).
// Dark modern style with Dragon Ball accent colors.

#pragma once

#include <functional>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "mod_pipeline.h"
#include "../mods.h"

namespace dbz3::launcher {

class LauncherDialog : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_play);

  // Apply the shared dark DBZ theme (also used by the in-game F4 menu).
  static void ApplyTheme();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawVideoTab();
  void DrawUpscaleTab();
  void DrawAudioTab();
  void DrawInputTab();
  void DrawModsTab();
  void DrawModelSwapTab();
  void DrawTexturesTab();
  void DrawDevTab();

  std::function<void()> on_play_;
  int active_tab_ = 0;

  // Model swap pipeline state.
  ModPipeline mod_pipeline_;
  int pipeline_src_idx_ = -1;
  int pipeline_dst_idx_ = -1;
  bool catalog_load_attempted_ = false;
  bool scan_was_running_ = false;
  char output_buf_[8192] = {};
  // Custom data_cmn.afs path for the model swap (empty = auto-detect).
  bool afs_path_auto_ = true;
  char afs_path_buf_[1024] = {};

  // Texture mod pipeline state.
  int tex_src_idx_ = -1;
  int tex_dst_idx_ = -1;  // -1 = mismo bin que el origen (sin swap)
  char tex_mod_buf_[128] = {};
  char tex_dir_buf_[512] = {};  // carpeta de texturas (default = mods/<mod>/textures)
  bool tex_catalog_attempted_ = false;

  // Mod manifest editing state.
  bool editing_mod_ = false;
  std::string edit_mod_name_;
  char edit_name_buf_[256] = {};
  char edit_desc_buf_[2048] = {};
  char edit_author_buf_[256] = {};
  char edit_version_buf_[128] = {};
  bool pending_manifest_reload_ = false;
};

}  // namespace dbz3::launcher