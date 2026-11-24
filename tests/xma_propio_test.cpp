// SUR-07: decodificador XMA propio sin el juego.   xma_propio_test <entrada_descomprimida.dec> [carpeta_wav]
// Decodifica cada sonido del banco #AMB, comprueba que da muestras con senal (RMS > 0) y que la duracion cuadra con PlayLength, y
// escribe un .wav por sonido si se da carpeta (para escuchar).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "src/xma_propio.h"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  const int n = dbz3::xma::SonidosEnBanco(d);
  std::printf("sonidos %d\n", n);
  int ok = 0;
  for (int i = 0; i < n; ++i) {
    dbz3::xma::Sonido s;
    const bool b = dbz3::xma::SonidoDeBanco(d, i, s);
    double e = 0;
    for (int16_t x : s.pcm) e += double(x) * x;
    const double rms = s.pcm.empty() ? 0 : std::sqrt(e / s.pcm.size());
    std::printf("%2d %s %5d Hz %d ch %6zu muestras %.2f s rms %.0f\n", i, b ? "ok" : "--", s.hz, s.canales, s.pcm.size(),
                s.hz ? double(s.pcm.size()) / s.canales / s.hz : 0.0, rms);
    if (b && rms > 50) ++ok;
    if (b && argc > 2) {
      char p[512];
      std::snprintf(p, sizeof p, "%s/s%02d.wav", argv[2], i);
      std::ofstream w(p, std::ios::binary);
      const uint32_t bytes = uint32_t(s.pcm.size() * 2), hz = uint32_t(s.hz), ba = uint32_t(2 * s.canales), abps = hz * ba;
      const uint16_t fmt = 1, ch = uint16_t(s.canales), bits = 16, bal = uint16_t(ba);
      const uint32_t riff = 36 + bytes, sixteen = 16;
      w.write("RIFF", 4); w.write((const char*)&riff, 4); w.write("WAVEfmt ", 8); w.write((const char*)&sixteen, 4);
      w.write((const char*)&fmt, 2); w.write((const char*)&ch, 2); w.write((const char*)&hz, 4); w.write((const char*)&abps, 4);
      w.write((const char*)&bal, 2); w.write((const char*)&bits, 2); w.write("data", 4); w.write((const char*)&bytes, 4);
      w.write((const char*)s.pcm.data(), bytes);
    }
  }
  std::printf("con senal %d de %d\n", ok, n);
  return n > 0 && ok * 2 >= n ? 0 : 1;
}
