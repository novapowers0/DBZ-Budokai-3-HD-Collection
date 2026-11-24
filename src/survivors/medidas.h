// R16: medidas optativas; el intervalo pertenece al dibujo ANTERIOR, no al actual.
#pragma once
#include <array>
#include <chrono>
#include <cstdlib>
#include <rex/logging.h>

namespace dbz3::survivors::medidas {
using Reloj = std::chrono::steady_clock;
inline bool Activas() {
  static const bool si = [] { const char* v = std::getenv("DBZ3_TEST_SURVIVORS_TIEMPOS"); return v && v[0] == '1'; }();
  return si;
}
enum Fase { Carga, Audio, Pantalla, Simulacion, Mundo, Textura, Modelo, Clips, Atlas, Hud, Animacion, Ruta, Guardado, NumFases };
inline constexpr const char* nombres[] = {"carga", "audio", "pantalla", "simulacion", "mundo", "textura", "modelo", "clips", "atlas", "hud", "animacion", "ruta", "guardado"};
inline thread_local std::array<double, NumFases> fases{};
struct Tramo {
  Fase fase;
  Reloj::time_point t = Activas() ? Reloj::now() : Reloj::time_point{};
  ~Tramo() { if (Activas()) fases[fase] += std::chrono::duration<double, std::milli>(Reloj::now() - t).count(); }
};
struct Cuadro {
  int pantalla;
  bool activo;
  Reloj::time_point t = Activas() ? Reloj::now() : Reloj::time_point{};
  Cuadro(int p, bool a) : pantalla(p), activo(a) { if (Activas()) fases.fill(0); }
  ~Cuadro() {
    if (!Activas()) return;
    struct Anterior {
      Reloj::time_point t{};
      std::array<double, NumFases> fases{};
      double cpu = 0;
      int pantalla = 0;
      unsigned cuadros = 0, picos = 0, picos_cpu = 0;
    };
    static Anterior a;
    if (!activo) { a = {}; return; }
    const double cpu = std::chrono::duration<double, std::milli>(Reloj::now() - t).count();
    if (a.t != Reloj::time_point{} && pantalla == a.pantalla) {
      const double frame = std::chrono::duration<double, std::milli>(t - a.t).count();
      ++a.cuadros;
      a.picos += frame > 25.0;
      a.picos_cpu += a.cpu > 25.0;
      if (frame > 25.0 || a.cpu > 25.0) {
        int causa = 0;
        // Preferir subfases concretas a sus contenedores (pantalla/carga).
        for (int i = 1; i < NumFases; ++i) if (a.fases[i] > a.fases[causa]) causa = i;
        for (int i = Textura; i < NumFases; ++i) if (a.fases[i] >= a.cpu * 0.5) causa = i;
        REXLOG_INFO("dbz3 survivors tiempos: pico pantalla={} frame_ms={:.3f} cpu_ms={:.3f} causa={} carga={:.3f} audio={:.3f} pantalla_ms={:.3f} sim={:.3f} mundo={:.3f} textura={:.3f} modelo={:.3f} clips={:.3f} atlas={:.3f} hud={:.3f} animacion={:.3f} ruta={:.3f} guardado={:.3f}",
                    a.pantalla, frame, a.cpu, a.cpu < 12.0 ? "sin_bloqueo_cpu_medido" : nombres[causa], a.fases[Carga], a.fases[Audio], a.fases[Pantalla], a.fases[Simulacion], a.fases[Mundo], a.fases[Textura], a.fases[Modelo], a.fases[Clips], a.fases[Atlas], a.fases[Hud], a.fases[Animacion], a.fases[Ruta], a.fases[Guardado]);
      }
    }
    if (a.t != Reloj::time_point{} && (pantalla != a.pantalla || a.cuadros >= 600)) {
      REXLOG_INFO("dbz3 survivors tiempos: resumen pantalla={} cuadros={} picos25={} cpu25={}", a.pantalla, a.cuadros, a.picos, a.picos_cpu);
      a.cuadros = a.picos = a.picos_cpu = 0;
    }
    a.t = t, a.pantalla = pantalla, a.cpu = cpu, a.fases = fases;
  }
};
}  // namespace dbz3::survivors::medidas
