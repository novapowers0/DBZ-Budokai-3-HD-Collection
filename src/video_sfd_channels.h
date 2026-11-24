#pragma once

namespace dbz3::video {

// Canal del CRITAG segun el idioma, como el ELF (Sys_GetOsdParamA_ -> 0x5C54B2): ingles 2, frances 3,
// aleman 4, italiano 5, espanol 6. Los IDs XGetLanguage del port son 3 DE, 4 FR, 5 ES, 6 IT (no coinciden con los canales).
constexpr int CanalSubtitulosIW(int dbz3_language) {
  switch (dbz3_language) {
    case 3: return 4;
    case 4: return 3;
    case 5: return 6;
    case 6: return 5;
    default: return 2;
  }
}

}  // namespace dbz3::video
