// Test-only mouse input. Goes through ImGui's normal press/release handling;
// never calls the save functions or accepts a confirmation on their behalf.
static void SaveUnlockTestInput(ImGuiIO& io) {
  if (!std::getenv("DBZ3_TEST_SAVE100_UI")) return;
  static int release_frame = -1;
  if (release_frame >= 0 && ImGui::GetFrameCount() >= release_frame) {
    io.AddMouseButtonEvent(0, false);
    release_frame = -1;
  }
  const auto request = rex::filesystem::GetExecutableFolder() / "dbz3_save100_input.req";
  std::ifstream input(request);
  float x = 0, y = 0;
  if (!(input >> x >> y) || !std::isfinite(x) || !std::isfinite(y)) return;
  input.close();
  auto consumed = request;
  consumed += ".used_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::error_code ec;
  std::filesystem::rename(request, consumed, ec);
  if (ec) return;
  io.AddMousePosEvent(x, y);
  io.AddMouseButtonEvent(0, true);
  release_frame = ImGui::GetFrameCount() + 1;
  REXLOG_INFO("dbz3: save_100 test mouse {},{}", int(x), int(y));
}
