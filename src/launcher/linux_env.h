// dbz3 - Resumen del entorno Linux para el registro (distro, nucleo, RAM, sesion grafica, Steam/Flatpak).
// Solo biblioteca estandar: se prueba en Windows con tests/linux_env_test.cpp. Los issues #14/#19/#61 de Linux llegan
// sin saber en que escritorio y con que paquete corre el juego; esta linea lo deja en el registro sin pedirselo al jugador.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

namespace dbz3::launcher {

namespace linux_env_detail {
inline std::string Trim(std::string s) {
  const char* ws = " \t\r\n\"";
  const size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(ws) - a + 1);
}
inline std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v && *v ? std::string(v) : std::string();
}
inline std::string Or(const std::string& s, const char* otherwise = "?") { return s.empty() ? otherwise : s; }
}  // namespace linux_env_detail

// Ejemplo: `distro="Arch Linux" kernel=6.9.1 ram=31987MB session=wayland desktop=KDE display=:0 wayland=wayland-0 steam=1 flatpak=0`
// Las rutas son parametros solo para la prueba.
inline std::string LinuxEnvSummary(const char* os_release = "/etc/os-release",
                                   const char* kernel_file = "/proc/sys/kernel/osrelease",
                                   const char* meminfo = "/proc/meminfo") {
  using namespace linux_env_detail;
  std::string distro, kernel;
  uint64_t ram_mb = 0;
  {
    std::ifstream f(os_release);
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("PRETTY_NAME=", 0) == 0) {
        distro = Trim(line.substr(12));
        break;
      }
      if (distro.empty() && line.rfind("NAME=", 0) == 0) distro = Trim(line.substr(5));
    }
  }
  {
    std::ifstream f(kernel_file);
    std::getline(f, kernel);
    kernel = Trim(kernel);
  }
  {
    std::ifstream f(meminfo);
    std::string key;
    uint64_t kb = 0;
    if (f >> key >> kb && key == "MemTotal:") ram_mb = kb / 1024;
  }
  std::string s = "distro=\"" + Or(distro) + "\" kernel=" + Or(kernel);
  s += " ram=" + std::to_string(ram_mb) + "MB";
  s += " session=" + Or(Env("XDG_SESSION_TYPE")) + " desktop=" + Or(Env("XDG_CURRENT_DESKTOP"));
  s += " display=" + Or(Env("DISPLAY"), "-") + " wayland=" + Or(Env("WAYLAND_DISPLAY"), "-");
  // Steam (nativo o Proton), Flatpak o AppImage cambian donde esta el ejecutable y que bibliotecas ve el juego.
  const bool steam = !Env("SteamAppId").empty() || !Env("SteamGameId").empty() || !Env("STEAM_COMPAT_DATA_PATH").empty();
  const bool flatpak = !Env("FLATPAK_ID").empty();
  s += std::string(" steam=") + (steam ? "1" : "0") + " flatpak=" + (flatpak ? "1" : "0") +
       " appimage=" + (Env("APPIMAGE").empty() ? "0" : "1");
  return s;
}

}  // namespace dbz3::launcher
