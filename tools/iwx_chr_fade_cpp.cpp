#include "../src/iw_chr_fade.h"
#include <cstdio>

int main() {
  unsigned color;
  while (std::scanf("%x", &color) == 1) {
    std::printf("%08X\n", dbz3::iwchrfade::ColorB3(color));
  }
}
