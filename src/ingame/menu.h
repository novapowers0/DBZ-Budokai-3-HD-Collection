// dbz3 - In-game overlay menu (F10) with Dev mode toggles.

#pragma once

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

namespace dbz3::ingame {

class InGameMenu : public rex::ui::ImGuiDialog {
 public:
  explicit InGameMenu(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawDevModeSection();
};

}  // namespace dbz3::ingame