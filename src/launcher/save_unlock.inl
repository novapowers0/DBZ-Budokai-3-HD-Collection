// Shared footer entry, including F4 (where disk changes must be refused).
void LauncherDialog::DrawSaveUnlock() {
  auto button = [](const char* label, const char* id, ImVec2 size = ImVec2(0, 0)) {
    const bool pressed = ImGui::Button((std::string(label) + "###unlock_" + id).c_str(), size);
    if (std::getenv("DBZ3_TEST_SAVE100_UI")) {
      // Test reads real widget positions, then clicks through SDL/ImGui. No auto-confirm path.
      static std::map<std::string, ImVec2> positions;
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      const ImVec2 p((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
      if (!positions.contains(id) || positions[id].x != p.x || positions[id].y != p.y) {
        positions[id] = p;
        REXLOG_INFO("dbz3: save_100 widget {} at {},{}", id, int(p.x), int(p.y));
      }
    }
    return pressed;
  };
  ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentSoft);
  if (button(i18n::T("Desbloquear todo", "Unlock everything"), "open", ImVec2(205, 38))) {
    save100_ok_ = dbz3::Save100Available();
    native_mods_status_.clear();
    ImGui::OpenPopup("###save_unlock");
  }
  ImGui::PopStyleColor();
  ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
  if (!ImGui::BeginPopupModal((std::string(i18n::T("Desbloquear todo", "Unlock everything")) + "###save_unlock").c_str(),
                             nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
  ImGui::TextUnformatted(i18n::T(
      "US y EU: instala la partida completa de B3 (personajes, trajes, cápsulas y modos). Sustituye tu progreso del juego original. Antes se guarda una copia; Héroe y Tag Team se conservan. ¿Continuar?",
      "US and EU: install the complete B3 save (characters, costumes, capsules and modes). This replaces your original-game progress. A backup is made first; Hero and Tag Team are preserved. Continue?"));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (in_game_) {
    ui::Hint(i18n::T("Cierra el juego y las otras ventanas de dbz3 antes de cambiar la partida. No se ha cambiado nada.",
                     "Close the game and other dbz3 windows before changing the save. Nothing was changed."));
  }
  // Keep buttons usable: an unavailable template or running game gets a visible explanation.
  if (button(i18n::T("Sí, desbloquear todo", "Yes, unlock everything"), "apply")) {
    save_unlock_ok_ = dbz3::ApplySave100(native_mods_status_, in_game_);
    saves_loaded_ = false;
  }
  if (!save100_ok_)
    ui::Hint(i18n::T("Falta la carpeta mods_nativos\\partida_100 junto a dbz3.exe: vuelve a instalar la version.",
                     "The mods_nativos\\partida_100 folder next to dbz3.exe is missing: reinstall the release."));
  ImGui::Spacing();
  if (button(i18n::T("Restaurar mi partida", "Restore my save"), "restore"))
    ImGui::OpenPopup("###save_unlock_restore");
  if (ConfirmModal((std::string(i18n::T("Partida", "Save")) + "###save_unlock_restore").c_str(),
                   i18n::T("Se recupera la partida anterior al último desbloqueo. La partida actual se guarda aparte antes de sustituirla. Héroe y Tag Team se conservan. ¿Restaurar?",
                           "Restore the save from before the last unlock. The current save is backed up separately before replacement. Hero and Tag Team are preserved. Restore?"),
                   i18n::T("Si, restaurar", "Yes, restore"))) {
    save_unlock_ok_ = dbz3::RestoreLastSaveBackup(native_mods_status_, in_game_);
    saves_loaded_ = false;
  }
  if (!native_mods_status_.empty()) {
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
    ImGui::TextColored(save_unlock_ok_ ? kOk : kWarn, "%s", native_mods_status_.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Spacing();
  if (button(i18n::T("Cerrar", "Close"), "close") || CloseKeyPressed()) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}
