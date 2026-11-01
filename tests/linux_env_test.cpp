// Prueba de src/launcher/linux_env.h (sin juego): clang++ -std=c++20 -Isrc/launcher tests/linux_env_test.cpp && a.out
#include "linux_env.h"

#include <cassert>
#include <cstdio>

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "tests/linux_env_data";
  const std::string s = dbz3::launcher::LinuxEnvSummary((dir + "/os-release").c_str(), (dir + "/osrelease").c_str(),
                                                         (dir + "/meminfo").c_str());
  std::puts(s.c_str());
  assert(s.find("distro=\"Arch Linux\"") != std::string::npos);
  assert(s.find("kernel=6.9.1-arch1-1") != std::string::npos);
  assert(s.find("ram=31982MB") != std::string::npos);
  // ficheros que no existen: no revienta y marca "?"
  const std::string n = dbz3::launcher::LinuxEnvSummary("nada1", "nada2", "nada3");
  assert(n.find("distro=\"?\" kernel=? ram=0MB") != std::string::npos);
  std::puts("OK");
}
