// TACTICS-V03: logica del menu propio de Dragon Tactics (opciones, dificultad, variedad, menu de inicio, continuar y guardar). Sacada tal cual
// de la clase Arena de entrada.cpp: se incluye DENTRO de su cuerpo (miembros de Arena), asi que no cambia nada. Sin dibujo.
// Solo para incluir desde src/tactics/entrada.cpp.
  // Pruebas (tools/pruebas.py): DBZ3_TEST_TACTICS_PRUEBA=1 guarda en hub_tactics_prueba.toml (nunca toca la partida del jugador) y no ofrece
  // "Continuar" salvo con DBZ3_TEST_TACTICS_MENU=1.
  static bool EnPrueba() { return EnvPrueba("PRUEBA") || EnvPrueba("AUTO") || EnvPrueba("SEMILLA"); }
  static std::filesystem::path RutaGuardado() { return dbz3::settings::UserDataRoot() / (EnPrueba() ? "hub_tactics_prueba.toml" : "hub_tactics.toml"); }
  static std::filesystem::path RutaOpc() { return dbz3::settings::UserDataRoot() / (EnPrueba() ? "hub_tactics_opciones_prueba.toml" : "hub_tactics_opciones.toml"); }
  void CargarOpc() {
    opc_ = Opc{};
    try {
      const toml::table doc = toml::parse_file(RutaOpc().string());
      opc_.reloj = std::clamp(int(doc["reloj"].value_or<int64_t>(0)), 0, 300);
      opc_.vel = doc["velocidad"].value_or<int64_t>(1) == 4 ? 4 : doc["velocidad"].value_or<int64_t>(1) == 2 ? 2 : 1;
      opc_.dif = std::clamp(int(doc["dificultad"].value_or<int64_t>(1)), 0, 2);
      opc_.sonido = doc["sonido"].value_or(true);
    } catch (...) {
    }
    if (const char* r = EnvPrueba("RELOJ")) opc_.reloj = std::clamp(std::atoi(r), 0, 300);   // solo pruebas
  }
  void GuardarOpc() const {
    std::error_code ec;
    std::filesystem::create_directories(RutaOpc().parent_path(), ec);
    std::ofstream f(RutaOpc());
    f << "# Dragon Tactics: opciones del menu del modo (reloj en segundos, 0 = apagado; velocidad 1, 2 o 4; dificultad 0 facil, 1 normal, 2 dificil)\n";
    f << "reloj = " << opc_.reloj << "\nvelocidad = " << opc_.vel << "\ndificultad = " << opc_.dif << "\nsonido = " << (opc_.sonido ? "true" : "false") << "\n";
  }
  // dificultad: la calidad de decision de los bots 1..7 se mueve -25 / 0 / +15 puntos (nunca pasa de 0-100)
  void AplicarDificultad() {
    eco_.calidad_bots = calidad_base_;
    for (int& q : eco_.calidad_bots) q = std::clamp(q + (opc_.dif == 0 ? -25 : opc_.dif == 2 ? 15 : 0), 0, 100);
  }
  // TACTICS-05: eco_ (lo que se juega y se dibuja) con o sin el bloque [variedad] aplicado; conserva la dificultad. Solo antes de crear o
  // continuar una partida: la partida sin variedad apunta a eco_, la continuada con variedad lleva su propia copia (igual a esta).
  void FijarVariedad(bool on) {
    if (eco_.variedad == on) return;
    const std::vector<int> cal = eco_.calidad_bots;
    eco_ = on ? ConVariedad(eco_cargada_) : eco_cargada_;
    eco_.calidad_bots = cal;
  }
  void CambiarOpc(int i, int d) {
    static const int relojes[] = {0, 30, 60, 90, 120};
    const auto ciclo = [&](int v, const int* tabla, int n) {
      int k = 0;
      for (int j = 0; j < n; ++j) if (tabla[j] == v) k = j;
      return tabla[(k + d + n) % n];
    };
    static const int vels[] = {1, 2, 4}, difs[] = {0, 1, 2};
    if (i == 0) opc_.reloj = ciclo(opc_.reloj, relojes, 5);
    else if (i == 1) opc_.vel = ciclo(opc_.vel, vels, 3), vel_ = opc_.vel;
    else if (i == 2) opc_.sonido = !opc_.sonido;
    else if (i == 3) opc_.dif = ciclo(opc_.dif, difs, 3);
    else return;
    GuardarOpc();
    REXLOG_INFO("dbz3 tactics: opcion {} -> reloj {} s, velocidad x{}, sonido {}, dificultad {}", i, opc_.reloj, opc_.vel, opc_.sonido, opc_.dif);
  }
  // Menu propio: Continuar (si hay partida guardada), Jugar, Como se juega, Opciones y Volver. Las pruebas lo saltan salvo con DBZ3_TEST_TACTICS_MENU=1.
  void IniciarOMenu() {
    menu_ = false;
    menu_pag_ = 0;
    guardada_.reset();
    mc_ = 0;
    if (EnPrueba() && !EnvPrueba("MENU")) return NuevaPartida(semilla_);
    menu_ = true;
    std::error_code ec;
    if (std::filesystem::exists(RutaGuardado(), ec) || std::filesystem::exists(RutaGuardado().string() + ".bak", ec)) {
      std::string e;
      FijarVariedad(false);
      if (CargarArchivo(RutaGuardado().string(), cat_, eco_, &guardada_, &e)) {
        FijarVariedad(guardada_->Variedad());   // TACTICS-05: para dibujar las sinergias con las que se creo
        REXLOG_INFO("dbz3 tactics: menu de inicio: hay una partida guardada (ronda {}, {} vivos)", guardada_->Ronda(), guardada_->Vivos());
        return;
      }
      REXLOG_WARN("dbz3 tactics: la partida guardada no se puede usar ({}): partida nueva", e);
      for (const std::string& d : ApartarGuardado(RutaGuardado().string()))   // TACTICS-01: se aparta (nunca se borra) para que no se pierda ni se vuelva a leer
        REXLOG_INFO("dbz3 tactics: partida guardada vieja apartada en {}", d);
      aviso_ = T::T("Tu partida guardada es de una versión anterior y no se puede cargar. Empieza una nueva.", "Your saved game is from an older version and can't be loaded. Starting a new one.");
    }
    REXLOG_INFO("dbz3 tactics: menu de inicio: sin partida guardada");
  }
  // ids de las filas del menu principal: 0 Continuar, 1 Jugar, 2 Como se juega, 3 Opciones, 4 Volver
  std::vector<int> FilasMenu() const {
    std::vector<int> r;
    if (guardada_) r.push_back(0);
    for (int i = 1; i <= 4; ++i) r.push_back(i);
    return r;
  }
  void ElegirMenu(int id) {
    if (id == 0 && guardada_) {
      partida_.emplace(std::move(*guardada_));
      if (partida_->Historial())   // TACTICS-08: el ultimo duelo contra cada rival vuelve con la partida guardada
        for (int q = 0; q < kMaxJug; ++q) hist_[q] = partida_->UltimoContra(0, q);
      guardada_.reset();
      menu_ = false;
      fin_partida_ = false;
      REXLOG_INFO("dbz3 tactics: partida continuada (ronda {}, huella {:016x})", partida_->Ronda(), partida_->Huella());
      if (EnvPrueba("AUDITAR")) AuditarHistorialPrueba("continuada");
      EntrarPreparacion(false);
    } else if (id == 1) {
      menu_ = false;
      guardada_.reset();
      AplicarDificultad();
      if (!aviso_.empty()) aviso_t_ = ImGui::GetTime(), aviso_dura_ = 9.0;
      NuevaPartida(semilla_);
    } else if (id == 2) {
      AbrirTutorial(true);
    } else if (id == 3) {
      menu_pag_ = 1, mc_ = 0;
      REXLOG_INFO("dbz3 tactics: menu: opciones");
    } else if (id == 4) {
      g_cerrar = true;
    }
  }
  void GuardarPartida() {
    std::string e;
    const std::string t = partida_->Guardar();
    if (t.empty() || !GuardarArchivo(RutaGuardado().string(), t, &e)) REXLOG_WARN("dbz3 tactics: no se pudo guardar la partida: {}", e);
    else {
      REXLOG_INFO("dbz3 tactics: partida guardada (ronda {}, huella {:016x})", partida_->Ronda(), partida_->Huella());
      if (EnvPrueba("AUDITAR")) AuditarHistorialPrueba("guardada");
    }
  }
