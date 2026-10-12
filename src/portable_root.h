// dbz3 - Carpeta "portable" del juego: ajustes, user_data, mods, logs y datos
// del juego (default.xex + us/ o la .iso).
//
// En Windows y Linux es la carpeta del ejecutable (todo junto al .exe). En
// macOS, si el juego va dentro de un bundle (DBZ Budokai 3.app/Contents/MacOS),
// no se debe escribir en el .app (rompe la firma; en /Applications puede no
// tener permiso): se usa ~/Library/Application Support/DBZ Budokai 3.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/platform.h>

namespace dbz3 {

inline bool RunningFromMacBundle() {
#if REX_PLATFORM_MAC
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  const auto contents = exe_dir.parent_path();
  return exe_dir.filename() == "MacOS" && contents.filename() == "Contents" &&
         contents.parent_path().extension() == ".app";
#else
  return false;
#endif
}

inline std::filesystem::path PortableRoot() {
#if REX_PLATFORM_MAC
  if (RunningFromMacBundle()) {
    static const std::filesystem::path root = [] {
      const char* home = std::getenv("HOME");
      std::filesystem::path dir = std::filesystem::path(home ? home : "/tmp") / "Library" /
                                  "Application Support" / "DBZ Budokai 3";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      return dir;
    }();
    return root;
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}

}  // namespace dbz3
