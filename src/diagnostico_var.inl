// solVAR (#29/#39/#59): medidas de solo lectura; no modifican el mando ni el render.
namespace dbz3::diagnostico_var {
using Reloj = std::chrono::steady_clock;

// Una consulta del mando 1 por tick. El intervalo es cadencia del guest, NO latencia
// boton-pantalla. El coste incluye XamInputGetState y su traduccion al registro nativo.
class Mando {
 public:
  explicit Mando(uint32_t user)
      : activo_(user == 0 && REXCVAR_QUERY(bool, dbz3_perf_logging)) {
    if (activo_) inicio_ = Reloj::now();
  }
  void Fin() {
    if (!activo_) return;
    const auto fin = Reloj::now();
    struct Ventana {
      Reloj::time_point inicio{}, anterior{};
      uint32_t n = 0, intervalos = 0;
      double total_us = 0, max_us = 0, total_ms = 0, max_ms = 0;
    };
    static thread_local Ventana v;
    // No mezclar ventanas separadas por una pausa, ni por logging apagado.
    if (!v.n || inicio_ - v.anterior > std::chrono::seconds(1)) v = {inicio_, inicio_};
    const double us = std::chrono::duration<double, std::micro>(fin - inicio_).count();
    v.total_us += us;
    v.max_us = std::max(v.max_us, us);
    if (v.n) {
      const double ms = std::chrono::duration<double, std::milli>(inicio_ - v.anterior).count();
      v.total_ms += ms;
      v.max_ms = std::max(v.max_ms, ms);
      ++v.intervalos;
    }
    v.anterior = inicio_;
    ++v.n;
    if (fin - v.inicio < std::chrono::seconds(5)) return;
    REXLOG_INFO("dbz3: input_diag user=0 backend={} mnk={} polls={} poll_avg_us={:.1f} poll_max_us={:.1f} "
                "tick_avg_ms={:.2f} tick_max_ms={:.2f} (guest cadence, not button-to-display latency)",
                rex::cvar::GetFlagByName("input_backend"), rex::cvar::GetFlagByName("mnk_mode"),
                v.n, v.total_us / v.n, v.max_us,
                v.intervalos ? v.total_ms / v.intervalos : 0.0, v.max_ms);
    v = {};
  }

 private:
  bool activo_;
  Reloj::time_point inicio_{};
};
}  // namespace dbz3::diagnostico_var

// Gemelos verificados: misma seleccion HD/SD y mismos campos +3400..3412.
// XGetVideoMode informa el modo de pantalla, pero B3 elige su propio backbuffer:
// HD = 1280x720; SD panoramico = 640x360. Registrar el resultado, no inferirlo
// del tamano de la ventana ni de una captura que ya puede estar reescalada.
DBZ3_HOOK(sub_820A7980, dbz3eu_sub_820A75D8) {
  const uint32_t video = ctx.r3.u32;
  orig(ctx, base);
  // #29: el guest no tiene modo propio por debajo de 360p; se le sirve un tamano menor (16:9, +3408/+3412
  // intactos). Solo el 3D: el HUD y los menus se dibujan con el tamano de video y siguen nitidos (medido).
  // Apagado por defecto (dbz3_low_render = 0). DBZ3_TEST_RENDER=ANCHOxALTO lo fuerza para las pruebas.
  unsigned w = 0, h = 0;
  bool forzado = false;
  if (const char* s = std::getenv("DBZ3_TEST_RENDER")) {
    forzado = std::sscanf(s, "%ux%u", &w, &h) == 2;
  } else {
    const int32_t alto = REXCVAR_QUERY(int32_t, dbz3_low_render);
    if (alto >= 120 && alto <= 480) {
      h = unsigned(alto);
      w = h * 16u / 9u;
      forzado = true;
    }
  }
  if (forzado && w >= 64 && h >= 64 && w <= 1920 && h <= 1080) {
    REX_STORE_U32(video + 3400, w);
    REX_STORE_U32(video + 3404, h);
    // PRUEBA (DBZ3_TEST_RENDER_ASPECT="4:3"): el aspecto que el guest usa para la
    // proyeccion (+3408/+3412, p. ej. 16:9). Solo para comparar con el original
    // 4:3; sin la variable no se toca.
    if (const char* a = std::getenv("DBZ3_TEST_RENDER_ASPECT")) {
      unsigned aw = 0, ah = 0;
      if (std::sscanf(a, "%u:%u", &aw, &ah) == 2 && aw >= 1 && ah >= 1 && aw <= 64 && ah <= 64) {
        REX_STORE_U32(video + 3408, aw);
        REX_STORE_U32(video + 3412, ah);
      }
    }
    REXLOG_INFO("dbz3: render bajo {}x{} (aspecto {}:{}, dbz3_low_render)", w, h,
                REX_LOAD_U32(video + 3408), REX_LOAD_U32(video + 3412));
  }
  REXLOG_INFO("dbz3: guest_render={}x{} aspect={}:{} video_requested={}x{} scale={}x{}",
              REX_LOAD_U32(video + 3400), REX_LOAD_U32(video + 3404),
              REX_LOAD_U32(video + 3408), REX_LOAD_U32(video + 3412),
              rex::cvar::GetFlagByName("video_mode_width"), rex::cvar::GetFlagByName("video_mode_height"),
              rex::cvar::GetFlagByName("draw_resolution_scale_x"), rex::cvar::GetFlagByName("draw_resolution_scale_y"));
}
