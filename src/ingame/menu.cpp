// dbz3 - In-game overlay menu implementation (F10).

#include "menu.h"

#include <rex/cvar.h>

#include "../launcher/settings.h"

namespace dbz3::ingame {

InGameMenu::InGameMenu(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

void InGameMenu::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.65f);
  if (!ImGui::Begin("dbz3##ingame", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGui::Text("%.1f FPS (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
  ImGui::Separator();

  bool dev_mode = dbz3::settings::DevMode();
  if (ImGui::Checkbox("Dev mode", &dev_mode)) {
    dbz3::settings::SetDevMode(dev_mode);
  }
  ImGui::TextDisabled("Dev mode exposes hot test switches used for debugging.");

  if (dev_mode) {
    DrawDevModeSection();
  }

  ImGui::Separator();
  ImGui::TextDisabled("F10 toggles this overlay. F4 opens advanced settings.");
  ImGui::End();
}

void InGameMenu::DrawDevModeSection() {
  ImGui::Separator();
  ImGui::TextColored(ImVec4(0.96f, 0.54f, 0.10f, 1.0f), "Dev mode");

  bool diag = dbz3::settings::DiagLogging();
  if (ImGui::Checkbox("GPU diagnostic logging (dbz3_gpu_diag.log)", &diag)) {
    dbz3::settings::SetDiagLogging(diag);
  }
  ImGui::TextDisabled("Per-frame GPU diagnostics. Writes a large log file.");

  int scale = dbz3::settings::ResolutionScale();
  if (ImGui::SliderInt("Internal render scale", &scale, 1, 4, "%dx", ImGuiSliderFlags_AlwaysClamp)) {
    dbz3::settings::SetResolutionScale(scale);
  }
  ImGui::TextDisabled("Supersampling of the 720p framebuffer (2x=1440p, 3x=2160p). Restart required.");

  ImGui::TextDisabled("Game speed: fixed 60 FPS (synchronized)");

  int cap = dbz3::settings::FrameCap();
  if (ImGui::SliderInt("Frame cap", &cap, 0, 240, cap == 0 ? "Uncapped" : "%d FPS")) {
    dbz3::settings::SetFrameCap(cap);
  }
  ImGui::TextDisabled("Host present rate. Applied on the next launch.");
}

}  // namespace dbz3::ingame
