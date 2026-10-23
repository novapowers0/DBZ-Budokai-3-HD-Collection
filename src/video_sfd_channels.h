#pragma once

namespace dbz3::video {

// La pista inglesa es el canal 2. En la edición europea, los IDs XGetLanguage
// 3..6 corresponden directamente a alemán, francés, español e italiano.
constexpr int CanalSubtitulosIW(int dbz3_language) {
  return dbz3_language >= 3 && dbz3_language <= 6 ? dbz3_language : 2;
}

}  // namespace dbz3::video
