// Dragon Budokai Survivors: la pantalla del modo (carga, eleccion de luchador, partida, subida de nivel, pausa y resultados) sobre juego.h, con el
// arte de recursos.h y los enemigos impostores de impostores.h. Contrato: studies 5/ENTREGAS/SURVIVORS_DISENO_MVP.md (Partida, Contenido,
// Pantallas). Se abre desde la fila del HUB (entrada.cpp) como Dragon Tactics: Abrir() crea una vez el dialogo (vive lo que el juego) y
// mientras Abierto() el HUB no se dibuja y la entrada del juego queda bloqueada.
// Camara cenital inclinada (Impostores::kPicado) que sigue al jugador, como la prueba tecnica (prueba.cpp): plano (x, y) de juego.h con +y
// hacia arriba en pantalla (en 3D, z = -y). Paso fijo de 60 Hz con acumulador; se dibuja el estado mas reciente.
// Pruebas (tools/pruebas.py, una sola lectura en Pr()): DBZ3_TEST_SURVIVORS_AUTO=1 abre el modo al llegar al menu, elige PERSONAJE (0-2)
// y juega con piloto; MANDO=1 lo abre igual pero sin piloto (registra la posicion cada segundo); DURA=segundos comprime la partida; SEMILLA.
// Tanda 4: ESCENARIO=llanura|desierto|ruinas|glaciar y DIFICULTAD=0|1 saltan la pantalla del mapa (cualquier escenario, sin desbloquear);
// ZENI=n pone los zeni de la tienda; GUARDADO=ruta = archivo de progreso de PRUEBA (en pruebas, sin el, survivors_progreso_prueba.toml:
// nunca el de verdad).
#include "modo_interno.h"
#include "pantallas_cierre.h"
#include "medidas.h"

// Sonido del combate (golpes, gritos y KO del juego). Encendido de serie (SUR-8, con dbz3_survivors_combate_oculto): necesita un combate nativo de fondo como Dragon Tactics (ver Combate*).
REXCVAR_DEFINE_BOOL(dbz3_survivors_sonido_combate, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: golpes, gritos y KO del juego (pide un combate oculto de fondo, como Dragon Tactics)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::survivors::interno {

namespace {
Reloj::time_point g_t_abre = Reloj::now();   // al abrir el modo (Reiniciar): para el registro "carga N ms" (reloj de pared; t_pant_ topa dt a 0,25 s)
}  // namespace

void Modo::OnDraw(ImGuiIO& io) {
  medidas::Cuadro medida(int(pant_), g_abierta.load());
  // 13/11: registro de los cuadros en los que NUESTRO dibujo tarda >30 ms (que pantalla, para saber quien da el tiron; tope 40 lineas)
  struct Lento {
    Modo* m;
    Reloj::time_point t0 = Reloj::now();
    ~Lento() {
      static int n = 0;
      const double ms = std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
      if (ms > 30.0 && m->dentro_ && n++ < 40)
        REXLOG_INFO("dbz3 survivors: cuadro lento {:.0f} ms en la pantalla {} (t_pant={:.1f})", ms, int(m->pant_), m->t_pant_);
    }
  } lento{this};
  if (!g_abierta.load()) {
    if (dentro_) {
      dentro_ = false;
      AudioFin();
      Bloquear(false);
      LiberarTexturas();   // los fondos (~25 MB) y la tanda 3 (~30 MB); los atlas de los impostores se quedan para la proxima vez
      std::fill(std::begin(tx_), std::end(tx_), nullptr), std::fill(std::begin(tx_reintento_), std::end(tx_reintento_), 0.0f);
      std::fill(std::begin(deco_tx_), std::end(deco_tx_), nullptr), std::fill(std::begin(deco_rt_), std::end(deco_rt_), 0.0f);   // tanda 4
      for (auto& fila : romp_tx_) std::fill(std::begin(fila), std::end(fila), nullptr);
      for (auto& fila : romp_rt_) std::fill(std::begin(fila), std::end(fila), 0.0f);
      std::fill(std::begin(precarga_), std::end(precarga_), 0);
    }
    return;
  }
  if (!dentro_) Reiniciar();
  Bloquear(true);
  const auto t0 = Reloj::now();
  const float dt = std::min(io.DeltaTime, 0.25f);
  t_ += dt, t_pant_ += dt;
  const uint16_t nuevo = Pad();
  if (!soltado_ && g_pad.load() == 0 && t_pant_ > 0.25f) soltado_ = true;   // lo pulsado al abrir (A del HUB) no cuenta
  const uint16_t pulsado = soltado_ ? nuevo : 0;
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  k_ = H / 1080.0f;
  ImDrawList* fondo = ImGui::GetBackgroundDrawList();
  ImDrawList* arriba = ImGui::GetForegroundDrawList();
  { medidas::Tramo tramo{medidas::Carga}; CargaPerezosa(); }
  {
  medidas::Tramo tramo{medidas::Audio};
  if (combate_) VigilarFondo();
  AudioCuadro(!saliendo_ && pant_ == Pantalla::Partida && j_ && j_->estado != Estado::Victoria && j_->estado != Estado::Derrota,
              j_ && size_t(j_->config.escenario) < datos_.escenarios.size() ? datos_.escenarios[size_t(j_->config.escenario)].id.c_str() : "llanura",
               j_ && j_->jefe.estado != EstadoJefe::Ausente && j_->jefe.estado != EstadoJefe::Muerto);
  }
  if (saliendo_) return Saliendo(fondo, arriba, W, H, dt);
  medidas::Tramo pantalla{medidas::Pantalla};
  switch (pant_) {
    case Pantalla::Carga: Carga(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Eleccion: Eleccion(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Mapa: Mapa(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Tienda: Tienda(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Partida: Partida(fondo, arriba, io, W, H, dt, pulsado, t0); break;
    case Pantalla::Resultados: Resultados(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Error: Mensaje(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Portada: Portada(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Coleccion: Coleccion(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Logros: Logros(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Clasificacion: Clasificacion(fondo, arriba, W, H, pulsado); break;
    case Pantalla::ComoJugar: ComoJugar(fondo, arriba, W, H, pulsado); break;
    case Pantalla::Opciones: Opciones(fondo, arriba, W, H, pulsado); break;
  }
}

void Modo::Reiniciar() {
  dentro_ = true, soltado_ = false, ultimo_ = 0;
  t_pant_ = 0, pausa_ = false;
  g_t_abre = Reloj::now(), imp_.Esperar(false);
  combate_ = saliendo_ = false, combate_pedido_ = false, se_ = tactics::MezclaSe(), zoom_via_ = 0;
  error_.clear();
  if (!datos_ok_) {
    std::string err;
    // P3 (contenido_v2): survivors_v2.toml es copia completa con lo nuevo; si no existe o no carga, vuelve al de siempre.
    if (REXCVAR_GET(dbz3_survivors_contenido_v2)) {
      datos_ok_ = CargarDatos((RutaRecursos() / "survivors_v2.toml").string(), &datos_, &err);
      if (!datos_ok_) {
        REXLOG_WARN("dbz3 survivors: survivors_v2.toml: {}", err);
        err.clear();
        datos_ = Datos{};
      }
    }
    if (!datos_ok_) datos_ok_ = CargarDatos((RutaRecursos() / "survivors.toml").string(), &datos_, &err);
    if (!err.empty()) REXLOG_WARN("dbz3 survivors: survivors.toml: {}", err);
    if (datos_ok_ && datos_.enemigos.size() < size_t(kTiposEnemigo)) datos_ok_ = false;
  }
  if (!datos_ok_) return Fallo(T::T("No se pudieron leer los datos de Dragon Budokai Survivors (res/survivors/survivors.toml).",
                                    "Could not read the Dragon Budokai Survivors data (res/survivors/survivors.toml)."));
  if (!j_) j_ = new Juego;   // ~0,3 MB: nunca en pila
  sel_ = std::clamp(sel_, 0, int(datos_.personajes.size()) - 1);
  LeerProg();
  Ir(cargado_ ? Inicial() : Pantalla::Carga);
}

void Modo::Ir(Pantalla p) {
  pant_ = p, t_pant_ = 0, sur2_cierre_dicho_ = false;
  if (p == Pantalla::Eleccion) REXLOG_INFO("dbz3 survivors: eleccion de personaje");
  if (p == Pantalla::Mapa) REXLOG_INFO("dbz3 survivors: mapa");
}

void Modo::Fallo(const char* texto) {
  error_ = texto;
  REXLOG_ERROR("dbz3 survivors: {}", error_);
  Ir(Pantalla::Error);
}

void Modo::Bloquear(bool si) {
  if (si == bloqueado_) return;
  if (auto* inp = Input()) {
    if (si) inp->AddUIInputBlocker();
    else inp->RemoveUIInputBlocker();
    bloqueado_ = si;
  }
}

uint16_t Modo::Pad() {
  if (!g_pad_pendiente.exchange(true)) {
    dbz3::launcher::DeferUi([] {
      uint16_t b = 0;
      int lx = 0, ly = 0, ry = 0;
      if (auto* inp = Input()) {
        rex::input::X_INPUT_STATE s = {};
        if (inp->GetStateForUI(0, &s) == 0) {
          b = static_cast<uint16_t>(s.gamepad.buttons);
          lx = int16_t(s.gamepad.thumb_lx), ly = int16_t(s.gamepad.thumb_ly), ry = int16_t(s.gamepad.thumb_ry);
          if (ly > 16000) b |= kArriba;
          if (ly < -16000) b |= kAbajo;
          if (lx > 16000) b |= kDer;
          if (lx < -16000) b |= kIzq;
        }
      }
      g_pad = b, g_lx = lx, g_ly = ly, g_ry = ry;
      g_pad_pendiente = false;
    });
  }
  const uint16_t b = g_pad.load();
  uint16_t nuevo = uint16_t(b & ~ultimo_);
  ultimo_ = b;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) nuevo |= kB;
  if (nuevo && Pr().mando) REXLOG_INFO("dbz3 survivors: pad {:04x}", nuevo);
  return nuevo;
}

Entrada Modo::Mando() const {
  Entrada e;
  const float lx = float(g_lx.load()) / 32767.0f, ly = float(g_ly.load()) / 32767.0f, m = std::sqrt(lx * lx + ly * ly);
  if (m > 0.25f) {
    const float r = std::min(1.0f, (m - 0.25f) / 0.75f) / m;
    e.x = lx * r, e.y = ly * r;
  }
  const uint16_t b = g_pad.load();
  if (b & (kIzq | kDer | kArriba | kAbajo)) {   // la cruceta manda (la palanca ya puso sus bits arriba: se recalcula sin ellos)
    const bool palanca = m > 0.5f;
    if (!palanca) {
      e.x = (b & kDer) ? 1.0f : (b & kIzq) ? -1.0f : 0.0f;
      e.y = (b & kArriba) ? 1.0f : (b & kAbajo) ? -1.0f : 0.0f;
      const float l = std::sqrt(e.x * e.x + e.y * e.y);
      if (l > 1) e.x /= l, e.y /= l;
    }
  }
  return e;
}

std::filesystem::path Modo::RutaProg() const {
  if (!Pr().guardado.empty()) return std::filesystem::path(Pr().guardado);
  return dbz3::settings::UserDataRoot() / (Pr().Alguna() ? "survivors_progreso_prueba.toml" : "survivors_progreso.toml");
}

void Modo::LeerProg() {
  std::string aviso;
  const bool ok = LeerProgreso(RutaProg(), &prog_, &aviso);
  if (!aviso.empty()) REXLOG_WARN("dbz3 survivors: progreso: {}", aviso);
  if (Pr().zeni >= 0) prog_.zeni = Pr().zeni;   // pruebas: zeni para la tienda
  REXLOG_INFO("dbz3 survivors: progreso {} zeni={} desbloqueados={:x} ({})", ok ? "leido" : "de serie", prog_.zeni, prog_.desbloqueados, RutaProg().string());
}

void Modo::GuardarProg() {
  medidas::Tramo tramo{medidas::Guardado};
  if (!GuardarProgreso(RutaProg(), prog_)) {
    REXLOG_WARN("dbz3 survivors: no se pudo guardar el progreso en {}", RutaProg().string());
    std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, "%s", T::T("No se pudo guardar el progreso.", "Could not save your progress.")), p2_.sub_msg_t = t_;   // auditoria #7: aviso donde la pantalla ya lo dibuja
  }
}

int Modo::EscenarioPrueba() const {   // DBZ3_TEST_SURVIVORS_ESCENARIO (por nombre); -1 = sin el
  for (int i = 0; i < int(datos_.escenarios.size()) && !Pr().escenario.empty(); ++i)
    if (datos_.escenarios[size_t(i)].id == Pr().escenario) return i;
  return -1;
}

void Modo::Salir() {
  REXLOG_INFO("dbz3 survivors: salir al hub");
  if (!combate_) return Cerrar();
  saliendo_ = true, t_salida_ = 0;
  dbz3::combate::CargarVoces(0);   // el combate aun corre unos segundos: ahi se sueltan las ranuras de voz
  dbz3::cinematica::PedirResultadoRapido();
  REXLOG_INFO("dbz3 survivors: saliendo (acaba el combate de fondo)");
}

void Modo::Saliendo(ImDrawList* fondo, ImDrawList* dl, float W, float H, float dt) {
  t_salida_ += dt;
  // el aviso de ring out del rival (retos de pasarela.cpp) en cada cuadro: si el combate aun cargaba, cuenta en cuanto empieza la pelea
  if (uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr) {
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (mgr && dbz3::hub::OwnBattle()) REX_STORE_U8(mgr + kAvisoFuera, 1);
  }
  fondo->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
  TxtC(dl, 34 * k_, ImVec2(W * 0.5f, H * 0.5f), IM_COL32(220, 225, 240, 255), T::T("Volviendo al HUB...", "Back to the HUB..."));
  const bool fin = g_fin_combate.load();
  if (!fin && t_salida_ < 60.0f) return;
  if (!fin) REXLOG_WARN("dbz3 survivors: el combate de fondo no acabo en 60 s: se cierra igual");
  REXLOG_INFO("dbz3 survivors: combate de fondo acabado ({:.1f} s)", t_salida_);
  combate_ = saliendo_ = false;
  Cerrar();
}

void Modo::Empezar(int pj, int esc, int dif) {
  const uint64_t semilla = Pr().semilla ? Pr().semilla : uint64_t(Reloj::now().time_since_epoch().count()) | 1;
  escn_ = std::clamp(esc, 0, int(datos_.escenarios.size()) - 1), dif_ = std::clamp(dif, 0, 1);
  Config cfg = ConfigDe(prog_, escn_, dif_);
  cfg.meta_v2 = MetaV2();
  AjustesP1(&cfg);
  cfg.autoapuntar = REXCVAR_GET(dbz3_survivors_autoapuntado) && prog_.opc_autoapunte != 0;   // SUR-8: la cvar habilita la opcion; opt-in hasta prueba en juego
  if (!j_->Iniciar(datos_, pj, semilla, Pr().dura, cfg))
    return Fallo(T::T("Los datos de Dragon Budokai Survivors están incompletos.", "The Dragon Budokai Survivors data is incomplete."));
  sel_ = pj;
  // tanda 4: escenario, dificultad y mejoras de la tienda (las pruebas lo leen)
  REXLOG_INFO("dbz3 survivors: escenario={} dificultad={} jefe={}", datos_.escenarios[size_t(escn_)].id, dif_, j_->DJ().id);
  {
    std::string m;
    for (int k = 0; k < int(datos_.mejoras.size()); ++k)
      if (j_->config.mejoras[k] > 0) m += " " + datos_.mejoras[size_t(k)].id + "=" + std::to_string(j_->config.mejoras[k]);
    REXLOG_INFO("dbz3 survivors: mejoras permanentes{} vida_max={:.0f} velocidad={:.2f} recogida={:.2f} vidas_extra={}", m.empty() ? " ninguna" : m.c_str(),
                j_->jugador.vida_max, j_->jugador.velocidad, j_->jugador.recogida, j_->vidas_extra);
  }
  std::fill(std::begin(deco_tx_), std::end(deco_tx_), nullptr), std::fill(std::begin(deco_rt_), std::end(deco_rt_), 0.0f);
  for (auto& fila : romp_tx_) std::fill(std::begin(fila), std::end(fila), nullptr);
  for (auto& fila : romp_rt_) std::fill(std::begin(fila), std::end(fila), 0.0f);
  pres_t_ = -1, nuevo_esc_ = -1, n_rotos_log_ = n_rayos_log_ = 0, amb_suma_ = mini_suma_ = 0;
  PedirFondo(B3Jugador());   // solo con dbz3_survivors_sonido_combate (y una vez por apertura)
  acum_ = 0, opc_dicha_ = false, pausa_ = false, aviso_jefe_ = -1, flash_ = 0;
  cuadros_ = 0, dt_suma_ = peor_ = cpu_suma_ = sim_suma_ = fx_suma_ = 0, t_estado_ = t_mando_ = 0;
  ta_ = 0, fin_ = false, t_fin_ = 0, pj_ = Anim{}, jefe_ = Anim{}, n_caidos_ = n_chispas_ = 0, t_tecnica_ = -100;
  n_vis_ = 0, min_dicho_ = 0, t_polvo_ = 0, eleccion_cofre_ = false, jefe_prev_ = EstadoJefe::Ausente, deco_suma_ = hojas_suma_ = 0;
  for (Cifra& c : cifras_) c.valor = 0;
  giro_ = 3.14159265f;   // de espaldas a la camara: mira hacia arriba (+y)
  {   // 12/11: el modelo del jefe se pide aqui (solo el de este escenario; sale a los minutos), no en la pantalla de carga
    const Modelo m = ModeloDe(j_->DJ().fid);
    pasarela::nave::EstadoFiguraB3(m.b3, m.forma, j_->DJ().fid);
  }
  REXLOG_INFO("dbz3 survivors: partida empezada personaje={} semilla={} dura={:.0f}", datos_.personajes[size_t(j_->personaje)].id, semilla, j_->duracion);
  Ir(Pantalla::Partida);
}

void Modo::Partida(ImDrawList* fondo, ImDrawList* dl, ImGuiIO& io, float W, float H, float dt, uint16_t pad, Reloj::time_point t0) {
  Juego& j = *j_;
  // final: la simulacion ya no corre; se ve entera la derrota (o la victoria) del jugador y despues los resultados
  if (fin_) {
    ta_ += dt, t_fin_ += dt;
    Mundo(fondo, W, H);
    if (j.estado != Estado::Derrota || !DerrotaNativa(dl, W, H)) Marcador(dl, W, H, dt);
    if (t_fin_ >= (j.estado == Estado::Victoria ? cierre::kPoseVictoria : cierre::kPoseDerrota)) {
      REXLOG_INFO("dbz3 survivors: pantalla de resultados");
      Ir(Pantalla::Resultados);
    }
    return;
  }
  if (!pausa_ && (j.estado == Estado::Jugando || (j.estado == Estado::Eligiendo && pj_.fin > ta_))) ta_ += dt;
  // menus encima de la partida
  if (pausa_) {
    if (pad & (kArriba | kAbajo)) pausa_sel_ ^= 1;
    if ((pad & (kB | kStart)) || ((pad & kA) && pausa_sel_ == 0)) {
      pausa_ = false;
      REXLOG_INFO("dbz3 survivors: continuar t={:.1f}", j.tiempo);
    } else if ((pad & kA) && pausa_sel_ == 1) {
      REXLOG_INFO("dbz3 survivors: partida abandonada t={:.1f}", j.tiempo);
      nuevo_esc_ = Anotar(j, int(datos_.escenarios.size()), &prog_);   // tanda 4: los zeni recogidos tambien van a la tienda
      if (MetaV2() && j.config.meta_v2) FinV2(j);   // auditoria #13: cofres y logros de fin tambien al abandonar (guarda)
      else GuardarProg();
      REXLOG_INFO("dbz3 survivors: progreso zeni={} (+{})", prog_.zeni, j.zeni);
      if (nuevo_esc_ >= 0) REXLOG_INFO("dbz3 survivors: desbloqueado {}", datos_.escenarios[size_t(nuevo_esc_)].id);
      return Salir();
    }
  } else if (j.estado == Estado::Jugando && (pad & kStart)) {
    pausa_ = true, pausa_sel_ = 0;
    REXLOG_INFO("dbz3 survivors: pausa t={:.1f}", j.tiempo);
  } else if (j.estado == Estado::Eligiendo) {
    t_opc_ += dt;
    SubidaMando(pad, dt);
    if (pad & kIzq) op_sel_ = (op_sel_ + j.nOpciones - 1) % j.nOpciones;
    if (pad & kDer) op_sel_ = (op_sel_ + 1) % j.nOpciones;
    const bool listo = t_opc_ > 0.3f;   // la animacion de entrada no se salta sin querer
    if (Pr().piloto && t_opc_ > (j.jugador.nivel <= 2 ? 1.5f : 0.35f)) op_sel_ = 0, pad |= kA;
    if (listo && (pad & kA)) {
      j.Elegir(op_sel_);
      opc_dicha_ = false, eleccion_cofre_ = false;
      Eventos();
    }
  }

  // simulacion: pasos fijos de 1/60 s (0-4 por cuadro, sin espiral)
  const auto ts = Reloj::now();
  {
  medidas::Tramo tramo{medidas::Simulacion};
  if (!pausa_ && j.estado == Estado::Jugando) {
    acum_ += dt;
    int pasos = 0;
    const Entrada manual = Mando();
    while (acum_ >= Sim::kDt && pasos < 4 && j.estado == Estado::Jugando) {
      j.Paso(Pr().piloto ? Piloto() : manual);
      Eventos();
      acum_ -= Sim::kDt, ++pasos;
    }
    if (pasos == 4) acum_ = 0;
    if (j.jugador.moviendo) giro_ = std::atan2(j.jugador.dx, -j.jugador.dy);   // giro 0 = de cara a +z = hacia -y
    if (j.jugador.moviendo && ta_ - t_polvo_ > 0.2f)   // polvo al correr, a los pies y un poco detras
      t_polvo_ = ta_, Lanzar(kPolvo, j.jugador.x - j.jugador.dx * 0.35f, j.jugador.y - j.jugador.dy * 0.35f, 0);
    // rotulo de oleada cada minuto de la linea de 15 (el del jefe sale con su aparicion)
    const int mi = int(j.Minuto());
    if (mi > min_dicho_) {
      min_dicho_ = mi;
      if (mi >= 1 && float(mi) < datos_.minuto_jefe) {
        char buf[128];
        std::snprintf(buf, sizeof buf, T::T("Minuto %d: ¡la horda crece!", "Minute %d: the horde grows!"), mi);
        Aviso(buf, false);
        REXLOG_INFO("dbz3 survivors: aviso oleada minuto={}", mi);
      }
    }
  }
  }
  const double sim_ms = Ms(ts, Reloj::now());
  if (j.estado == Estado::Eligiendo && !opc_dicha_) {
    opc_dicha_ = true, t_opc_ = 0, op_sel_ = 0;
    char ops[160] = {};
    for (int i = 0; i < j.nOpciones; ++i) {
      std::strncat(ops, i ? "," : "", sizeof ops - std::strlen(ops) - 1);
      std::strncat(ops, IdOpcion(datos_, j.opciones[i]), sizeof ops - std::strlen(ops) - 1);
    }
    REXLOG_INFO("dbz3 survivors: nivel {} opciones={}", j.jugador.nivel, ops);
  }
  if (j.estado == Estado::Jugando && Pr().piloto) {
    static const auto orden = [] { const char* e = std::getenv("DBZ3_TEST_SURVIVORS_CIERRE"); return std::string_view(e ? e : ""); }();
    const auto forzado = cierre::Forzar(true, j.tiempo, orden);
    if (forzado != cierre::FinalPrueba::No) {
      j.estado = forzado == cierre::FinalPrueba::Victoria ? Estado::Victoria : Estado::Derrota;
      if (j.estado == Estado::Derrota) j.jugador.vida = 0;
      REXLOG_INFO("dbz3 survivors: sur2 final de prueba");
    }
  }
  if (j.estado == Estado::Victoria || j.estado == Estado::Derrota) {
    REXLOG_INFO("dbz3 survivors: fin resultado={} t={:.1f} nivel={} derrotados={} zeni={}", j.estado == Estado::Victoria ? "victoria" : "derrota", j.tiempo,
                j.jugador.nivel, j.derrotados, j.zeni);
    const int clip = j.estado == Estado::Victoria ? kClipVictoria : kClipDerrota;
    Cambiar(pj_, clip);
    pj_.fin = 0, fin_ = true, t_fin_ = 0;
    REXLOG_INFO("dbz3 survivors: anim jugador clip={}", clip);
    // tanda 4: zeni a la tienda, medalla, desbloqueo y jefes; se guarda
    nuevo_esc_ = Anotar(j, int(datos_.escenarios.size()), &prog_);
    GuardarProg();
    REXLOG_INFO("dbz3 survivors: progreso zeni={} (+{}) medalla={} rotos={} racha_max={}", prog_.zeni, j.zeni,
                int(MedallaDe(j.estado == Estado::Victoria, j.Minuto(), j.config.dificultad)), j.rotos, j.racha_max);
    if (nuevo_esc_ >= 0) REXLOG_INFO("dbz3 survivors: desbloqueado {}", datos_.escenarios[size_t(nuevo_esc_)].id);
    if (j.estado == Estado::Victoria) Sonar("victoria", 3, 0);
    if (j.estado == Estado::Victoria) SonarCombate("victoria", tactics::PrioSe::Muerte, 1.2, B3Jugador(), GritoAtaque(), t_voz_se_, 0);
    else SonarCombate("derrota", tactics::PrioSe::Muerte, 1.2, B3Jugador(), GritoKo(), t_voz_se_, 0);
    return;
  }

  {
    medidas::Tramo tramo{medidas::Animacion};
    AnimJugador();
    AnimJefe();
  }
  { medidas::Tramo tramo{medidas::Mundo}; Mundo(fondo, W, H); }
  Marcador(dl, W, H, dt);
  if (j.estado == Estado::Eligiendo) Subida(dl, W, H);
  if (pausa_) Pausa(dl, W, H);

  // medidas (cada 10 s) y posicion (prueba de mando, cada segundo)
  ++cuadros_, dt_suma_ += io.DeltaTime, peor_ = std::max(peor_, double(io.DeltaTime)), sim_suma_ += sim_ms;
  cpu_suma_ += Ms(t0, Reloj::now());
  if ((t_estado_ += dt) >= 10.0f) {
    const double c = std::max(1, cuadros_);
    REXLOG_INFO("dbz3 survivors: estado t={:.1f} enemigos={} efectos={} fps={:.1f} peor_ms={:.1f} cpu_ms={:.2f} sim_ms={:.3f} fx_ms={:.3f} caidos={} "
                "deco_ms={:.3f} hojas_ms={:.3f} objetos={} ambiente_ms={:.3f} minimapa_ms={:.3f} rompibles={} rotos={} racha={}",
                j.tiempo, j.NumEnemigos(), j.nEfectos, dt_suma_ > 0 ? c / dt_suma_ : 0.0, peor_ * 1000.0, cpu_suma_ / c, sim_suma_ / c, fx_suma_ / c, n_caidos_,
                deco_suma_ / c, hojas_suma_ / c, j.nObjetos, amb_suma_ / c, mini_suma_ / c, j.nRompibles, j.rotos, j.racha);
    cuadros_ = 0, dt_suma_ = peor_ = cpu_suma_ = sim_suma_ = fx_suma_ = deco_suma_ = hojas_suma_ = amb_suma_ = mini_suma_ = 0, t_estado_ = 0;
  }
  if (Pr().mando && !Pr().piloto && (t_mando_ += dt) >= 1.0f) {
    double dano = 0;
    for (double d : j.dano_tecnica) dano += d;
    REXLOG_INFO("dbz3 survivors: jugador x={:.2f} y={:.2f} derrotados={} dano={:.0f}", j.jugador.x, j.jugador.y, j.derrotados, dano);
    t_mando_ = 0;
  }
}

void Modo::Eventos() {
  const Juego& j = *j_;
  golpes_paso_ = cifras_paso_ = humos_paso_ = destellos_paso_ = 0;
  // carga del jefe: grieta al arrancar y al frenar
  if ((j.jefe.estado == EstadoJefe::Carga) != (jefe_prev_ == EstadoJefe::Carga)) Lanzar(kGrieta, j.jefe.x, j.jefe.y, 0, 1.3f);
  jefe_prev_ = j.jefe.estado;
  for (int i = 0; i < j.nEventos; ++i) {
    const Evento& e = j.eventos[i];
    MundoAlEvento(e), PantallasAlEvento(e), AudioAlEvento(e);   // ganchos P0 (P1, P2, P3)
    switch (e.tipo) {
      case TipoEvento::JefeAparece: {
        if (T3(kRotuloJefe) && T3(kMarcoRetrato)) {   // tanda 4: rotulo de presentacion con su nombre y su retrato 3D
          pres_t_ = 0;
          REXLOG_INFO("dbz3 survivors: jefe presentado {}", j.DJ().id);
        } else {
          char buf[128];
          std::snprintf(buf, sizeof buf, T::T("¡Aparece %s!", "%s appears!"), Es(j.DJ().txt));
          Aviso(buf, true);
        }
        REXLOG_INFO("dbz3 survivors: jefe aparece t={:.1f}", j.tiempo);
        Sonar("jefe_aparece", 5, 0);
        SonarCombate("jefe_aparece", tactics::PrioSe::Muerte, 1.2, B3Jefe(), GritoAtaque(), t_jefe_se_, 0);
        break;
      }
      case TipoEvento::JefeMuere:
        REXLOG_INFO("dbz3 survivors: jefe derrotado t={:.1f}", j.tiempo);
        Lanzar(kExplosion, e.x, e.y, 0, 1.4f), Lanzar(kGrieta, e.x, e.y, 0, 2.2f);
        Sonar("jefe_derrota", 3, 0);
        SonarCombate("jefe_ko", tactics::PrioSe::Muerte, 1.2, B3Jefe(), GritoKo(), t_jefe_se_, 0);
        SonarCombate("explosion", tactics::PrioSe::Muerte, 0.9, -1, kSeExplosion, t_explosion_se_, 0);
        break;
      case TipoEvento::JefeAtaque:   // SUR-04: cada ataque del kit con su propio grito del banco del jefe (siempre el mismo para el mismo ataque)
        if (j.config.jefes_v2) SonarCombate("jefe_ataque_kit", tactics::PrioSe::Tecnica, 0.9, B3Jefe(), 42 + (e.a * 5) % 9, t_jefe_se_, 0.3f);
        break;
      case TipoEvento::JefeFase:   // SUR-04: el grito largo de la transformacion de fase
        SonarCombate("jefe_fase", tactics::PrioSe::Muerte, 1.5, B3Jefe(), 50, t_jefe_se_, 0);
        break;
      case TipoEvento::AvisoJefe:   // el jefe anuncia la carga o el anillo: su grito de ataque
        if (j.config.jefes_v2 && e.a == 2 && j.jefe.estado == EstadoJefe::AvisoKit) break;   // los del kit ya gritan por JefeAtaque
        SonarCombate("jefe_ataque", tactics::PrioSe::Tecnica, 0.9, B3Jefe(), GritoAtaque(), t_jefe_se_, 1.5f);
        break;
      case TipoEvento::Recogida:   // destello de la gema (como mucho 4 por paso)
        if (destellos_paso_++ < 4) Lanzar(kRecoger, e.x, e.y, 0.35f);
        if (recogido_dicho_ < 3) {
          ++recogido_dicho_;
          REXLOG_INFO("dbz3 survivors: recogido gema valor={}", e.a);
        }
        break;
      case TipoEvento::Objeto: {
        const TipoObjeto o = TipoObjeto(e.a);
        REXLOG_INFO("dbz3 survivors: recogido {} t={:.1f}", NombreObjeto(o), j.tiempo);
        if (o == TipoObjeto::Cofre) {
          REXLOG_INFO("dbz3 survivors: cofre abierto");
          Lanzar(kCofreAbiertoFx, e.x, e.y, 0, 1.0f, false, true);   // se ve abierto detras de las tarjetas
          eleccion_cofre_ = true;
          Sonar("cofre", 3, 0);
        } else if (o == TipoObjeto::Bomba) {
          Lanzar(kExplosion, j.jugador.x, j.jugador.y, 0), Lanzar(kGrieta, j.jugador.x, j.jugador.y, 0, 2.0f);
          flash_ = 0.6f;
          SonarCombate("explosion", tactics::PrioSe::Muerte, 0.9, -1, kSeExplosion, t_explosion_se_, 0);
        } else {
          Lanzar(kRecoger, e.x, e.y, 0.4f, 1.4f);
        }
        break;
      }
      // tanda 4
      case TipoEvento::Rompible:
        Lanzar(kHumo, e.x, e.y, 0, 0.9f), Lanzar(kImpacto, e.x, e.y, 0.5f, 1.2f);
        if (n_rotos_log_++ < 40)
          REXLOG_INFO("dbz3 survivors: rompible roto {} t={:.1f}", e.a >= 0 && e.a < int(datos_.rompibles.size()) ? datos_.rompibles[size_t(e.a)].id : "?", j.tiempo);
        SonarCombate("rompible", tactics::PrioSe::Golpe, 0.35, -1, kSeImpacto, t_golpe_se_, 0.2f);
        break;
      case TipoEvento::RayoCae:
        Lanzar(kGrieta, e.x, e.y, 0, 1.0f), Lanzar(kImpacto, e.x, e.y, 0.3f, 2.0f);
        flash_ = std::max(flash_, 0.35f);
        if (n_rayos_log_++ < 40) REXLOG_INFO("dbz3 survivors: rayo cae t={:.1f}", j.tiempo);
        SonarCombate("rayo", tactics::PrioSe::Muerte, 0.5, -1, kSeImpacto, t_explosion_se_, 0.3f);
        break;
      case TipoEvento::Racha:
        REXLOG_INFO("dbz3 survivors: racha {}", e.a);
        break;
      case TipoEvento::Reanimado:
        Lanzar(kSubidaFx, e.x, e.y, 0.9f, 1.2f, true);
        REXLOG_INFO("dbz3 survivors: reanimado t={:.1f}", j.tiempo);
        Sonar("reanimado", 3, 0);
        break;
      case TipoEvento::Evolucion:
        if (e.a >= 0 && e.a < int(datos_.tecnicas.size())) REXLOG_INFO("dbz3 survivors: evolucion {}", datos_.tecnicas[size_t(e.a)].id);
        Sonar("evolucion", 3, 0);
        break;
      case TipoEvento::Subida:
        Lanzar(kSubidaFx, e.x, e.y, 0.9f, 1.0f, true, true);   // sobre el jugador; corre tambien con la eleccion delante
        Cambiar(pj_, kClipVictoria, 0.8f);
        pj_.fin = ta_ + 0.8f, pj_.giro = giro_;
        Sonar("subida", 3, 0.1f);
        break;
      case TipoEvento::DanoJugador:
        flash_ = 1.0f;
        if (j.jugador.vida > 0) {   // golpe recibido: la sacudida de B3 (corta) aunque estuviera lanzando algo
          Cambiar(pj_, kClipDano, 0.3f);
          pj_.fin = ta_ + 0.3f, pj_.giro = giro_;
          if (log_clip_[kClipDano]++ < 4) REXLOG_INFO("dbz3 survivors: anim jugador clip={}", kClipDano);
          if (combate_ || SePropio()) SonarCombate("dano", tactics::PrioSe::Tecnica, 0.35, -1, kSeImpacto, t_dano_se_, 0.5f);   // el golpe del juego
          else Sonar("dano", 5, 0.6f);
        }
        break;
      case TipoEvento::Tecnica: {
        if (e.a < 0 || e.a >= int(datos_.tecnicas.size())) break;
        const Arquetipo q = datos_.tecnicas[size_t(e.a)].arq;
        // sonido al lanzarse (antes del filtro de la pose): el grito de ataque del luchador; las continuas (aura, barrera...) no gritan
        if (q != Arquetipo::Aura && q != Arquetipo::Barrera && q != Arquetipo::Orbita && q != Arquetipo::Onda)
          SonarCombate("tecnica", tactics::PrioSe::Tecnica, 0.9, B3Jugador(), GritoAtaque(), t_voz_se_, 1.2f);
        if (pj_.fin > ta_ || ta_ - t_tecnica_ < 0.25f) break;   // una pose cada vez y un respiro andando
        if (q == Arquetipo::Aura || q == Arquetipo::Barrera || q == Arquetipo::Orbita || q == Arquetipo::Onda) break;   // continuas: sin pose
        t_tecnica_ = ta_;
        if (q == Arquetipo::Cono) Accion(kClipAtaque, 0.45f, e.x, e.y);   // golpe fisico
        else Accion(kClipTecnica, q == Arquetipo::Rayo ? 0.9f : q == Arquetipo::RayoFino ? 0.75f : 0.55f, e.x, e.y);   // ki
        break;
      }
      case TipoEvento::Muerte: {   // el enemigo sale de la simulacion: aqui cae con el clip de derrota de su tipo
        int s = n_caidos_ < kCaidos ? n_caidos_++ : 0;
        if (s == 0 && n_caidos_ == kCaidos)   // llena: el que cayo antes deja su sitio
          for (int i = 1; i < kCaidos; ++i) s = caidos_[i].t0 < caidos_[s].t0 ? i : s;
        caidos_[s] = {e.x, e.y, std::atan2(j.jugador.x - e.x, e.y - j.jugador.y), ta_, e.a};
        if (humos_paso_++ < 10) Lanzar(kHumo, e.x, e.y, 0, e.a >= 0 && e.a < int(datos_.enemigos.size()) ? datos_.enemigos[size_t(e.a)].escala : 1.0f);
        if (e.a >= 0 && e.a < int(datos_.enemigos.size()))   // el KO del enemigo con su voz (como mucho uno cada 0,6 s)
          SonarCombate("derrota_enemigo", tactics::PrioSe::Golpe, 0.8, ModeloDe(datos_.enemigos[size_t(e.a)].fid).b3, GritoKo(), t_ko_se_, 0.6f);
        if (e.a >= 0 && e.a < 32 && !(caido_dicho_ & (1u << e.a))) {
          caido_dicho_ |= 1u << e.a;
          REXLOG_INFO("dbz3 survivors: caido tipo={}", e.a);
        }
        break;
      }
      case TipoEvento::Golpe: {   // impacto (hoja de la tanda 3; sin ella, chispas de B3) y cifra de dano; como mucho 6 nuevos por paso
        Cifra_(e.x, e.y, e.v);
        SonarCombate("golpe", tactics::PrioSe::Golpe, 0.35, -1, kSeImpacto, t_golpe_se_, 0.2f);   // impacto del juego: como mucho 5 por segundo
        if (golpes_paso_++ >= 6) break;
        chispas_[n_chispas_++ % kChispas] = {e.x, e.y, ta_};
        Lanzar(kImpacto, e.x, e.y, 0.9f);
        if (std::abs(e.x - j.jefe.x) < 1e-4f && std::abs(e.y - j.jefe.y) < 1e-4f && j.jefe.estado == EstadoJefe::Persigue && ta_ - jefe_golpe_ > 1.5f)
          jefe_golpe_ = ta_, jefe_.fin = ta_ + 0.3f;   // el jefe acusa un golpe de vez en cuando (si no, temblaria siempre)
        break;
      }
      default: break;
    }
  }
}

Entrada Modo::Piloto() {
  const Juego& j = *j_;
  const float px = j.jugador.x, py = j.jugador.y;
  float ax = 0, ay = 0;
  for (int i = 0, n = j.NumEnemigos(); i < n; ++i) {
    const VistaEnemigo v = j.Enemigo(i);
    const float dx = px - v.x, dy = py - v.y, d2 = dx * dx + dy * dy;
    if (d2 < 30.0f) ax += dx / (d2 + 0.1f), ay += dy / (d2 + 0.1f);
  }
  if (j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) {
    const float dx = px - j.jefe.x, dy = py - j.jefe.y, d2 = dx * dx + dy * dy;
    if (d2 < 120.0f) ax += 6 * dx / (d2 + 0.1f), ay += 6 * dy / (d2 + 0.1f);
  }
  for (int k = 0; k < j.nEfectos; ++k) {   // tanda 4: fuera de las marcas del rayo y de las sombras de la lluvia
    const Efecto2D& e = j.efectos[k];
    if (!e.hostil || (e.forma != Forma::AvisoRayo && e.forma != Forma::Sombra)) continue;
    const float dx = px - e.x, dy = py - e.y, d2 = dx * dx + dy * dy, r = e.radio + 1.0f;
    if (d2 < r * r) ax += 3 * dx / (d2 + 0.1f), ay += 3 * dy / (d2 + 0.1f);
  }
  int g = -1;
  float gd = 1e9f;
  for (int i = 0; i < j.nGemas; ++i) {
    const float dx = j.gx[i] - px, dy = j.gy[i] - py, d2 = dx * dx + dy * dy;
    if (d2 < gd) gd = d2, g = i;
  }
  if (g >= 0) {
    const float d = std::sqrt(gd) + 1e-3f;
    ax += 0.8f * (j.gx[g] - px) / d, ay += 0.8f * (j.gy[g] - py) / d;
  } else {
    ax += 0.4f * std::cos(j.tiempo * 0.4f), ay += 0.4f * std::sin(j.tiempo * 0.4f);   // sin gemas: da vueltas
  }
  ax -= px * 0.004f, ay -= py * 0.004f;   // no se aleja sin fin del centro
  const float l = std::sqrt(ax * ax + ay * ay);
  return l > 1e-3f ? Entrada{ax / l, ay / l} : Entrada{};
}

const ps2::Tex* Modo::T3(int i) {
  if (!tx_[i] && tx_reintento_[i] <= t_) {
    tx_[i] = Arte(kArte3[i]);
    if (!tx_[i]) tx_reintento_[i] = t_ + 5.0f;   // archivo ausente: no reintentar cada cuadro
  }
  return tx_[i];
}

void Modo::CargaPerezosa() {
  // Una sola salida de Carga: luchadores y atlas completos, nunca cambiar de pantalla desde aqui.
  if (cargado_ && imp_definidos_) imp_.Avanzar();
  // el modelo del jefe del escenario que se va a jugar (el del mapa, o el de la prueba / el primero) se lee ya en los menus: Empezar lo pedia al
  // arrancar la partida, y leer su esqueleto (~80 ms) en el hilo de dibujo era el tiron de los primeros segundos
  if (cargado_ && (pant_ == Pantalla::Eleccion || pant_ == Pantalla::Mapa || pant_ == Pantalla::Portada)) {
    const int e = pant_ == Pantalla::Mapa ? map_sel_ : std::max(0, EscenarioPrueba());
    if (e >= 0 && size_t(e) < datos_.escenarios.size() && size_t(datos_.escenarios[size_t(e)].jefe) < datos_.jefes.size()) {
      const int fid = datos_.jefes[size_t(datos_.escenarios[size_t(e)].jefe)].fid;
      const Modelo m = ModeloDe(fid);
      pasarela::nave::EstadoFiguraB3(m.b3, m.forma, fid);
    }
  }
}

void Modo::Precarga(int tope) {   // en los menus: unas pocas por cuadro, para que la partida no empiece con un tiron
  for (int i = 0; i < kNumArte && tope > 0; ++i)
    if (!tx_[i] && tx_reintento_[i] <= t_) T3(i), --tope;
}

void Modo::PrecargaEscenario(int e, int tope) {
  if (e < 0 || e >= int(datos_.escenarios.size()) || e >= kMaxEscenarios) return;
  const DefEscenario& d = datos_.escenarios[size_t(e)];
  const DefDecorado& g = d.deco;
  const int ns = int(g.suelos.size()), nm = int(g.manchas.size()), na = int(g.adornos.size()), nr = int(d.rompibles.size()) * 2;
  for (int& i = precarga_[e]; tope > 0 && i < ns + nm + na + nr + int(d.ambiente.size()); ++i, --tope) {
    int k = i;
    if (k < ns) { Arte(g.suelos[size_t(k)].c_str()); continue; }
    if ((k -= ns) < nm) { Arte(g.manchas[size_t(k)].c_str()); continue; }
    if ((k -= nm) < na) { Arte(g.adornos[size_t(k)].c_str()); continue; }
    if ((k -= na) < nr) { RompTex(d.rompibles[size_t(k / 2)], (k & 1) != 0); continue; }
    Arte(("tanda4/03_ambiente/ambiente_" + d.ambiente[size_t(k - nr)] + ".png").c_str());
  }
}

void CrearPanel() {
  static Modo* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Modo(rt->imgui_drawer());
  else if (!panel) g_panel_pedido = false;   // sin dibujante todavia: el proximo Abrir lo vuelve a pedir
}

// pruebas: AUTO o MANDO abren el modo directamente al llegar al menu principal (pantalla 5), sin pasar por el HUB
const bool g_prueba = [] {
  if (!Pr().piloto && !Pr().mando) return false;
  std::thread([] {
    using namespace std::chrono_literals;
    while (dbz3::ingame::GameScreen() != 5) std::this_thread::sleep_for(100ms);
    std::this_thread::sleep_for(1500ms);
    REXLOG_INFO("dbz3 survivors: apertura de prueba ({})", Pr().piloto ? "piloto" : "mando");
    Abrir();
  }).detach();
  return true;
}();

}  // namespace dbz3::survivors::interno

namespace dbz3::survivors {

using namespace interno;
void Abrir() {
  if (g_abierta.exchange(true)) return;
  REXLOG_INFO("dbz3 survivors: abierto");
  if (!g_panel_pedido.exchange(true)) dbz3::launcher::DeferUi(&CrearPanel);
}
void Cerrar() {
  if (g_abierta.exchange(false)) {
    REXLOG_INFO("dbz3 survivors: cerrado (vuelve al HUB)");
    AvisarVueltaHub();
  }
}
bool Abierto() { return g_abierta.load(); }

}  // namespace dbz3::survivors
