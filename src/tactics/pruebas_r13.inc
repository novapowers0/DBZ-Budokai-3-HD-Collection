// Miembros de Arena. Solo se invocan tras EnvPrueba("AUDITAR") (getenv).
// No alteran la partida presentada ni escriben archivos. tools/tactics_r13.py
// comprueba los registros contra los resultados de duelo y el guardado real.
  void AuditarHistorialPrueba(const char* momento) const {
    std::string matriz;
    for (int p = 0; p < partida_->N(); ++p)
      for (int q = 0; q < partida_->N(); ++q) matriz += char('0' + partida_->UltimoContra(p, q));
    REXLOG_INFO("dbz3 tactics audit: historial {} ronda {} activo {} huella {:016x} matriz {}",
                momento, partida_->Ronda(), partida_->Historial(), partida_->Huella(), matriz);
  }
  void AuditarDuelosPrueba() const {
    for (size_t k = 0; k < partida_->Duelos().size(); ++k) {
      const Duelo& d = partida_->Duelos()[k];
      REXLOG_INFO("dbz3 tactics audit: duelo ronda {} a {} b {} copia {} ganador {}",
                  partida_->Ronda(), d.a, d.b, d.copia, partida_->Resultados()[k].ganador);
    }
    AuditarHistorialPrueba("resultado");
  }
  void AuditarVariedadPrueba() const {
    // Mismas reglas efectivas que la partida nueva, sin presentacion. Un lote
    // acotado permite demostrar que no gana siempre una sola composicion.
    if (!partida_->Variedad()) return;
    for (int t = 0; t < 32; ++t) {
      Partida p(cat_, eco_, uint64_t(500000 + t), 0,
                partida_->ConCarrusel(), partida_->BotsListos(), true);
      if (partida_->Historial()) p.ActivarHistorial();
      while (!p.Acabada() && p.Ronda() < 200) {
        if (p.FaseActual() == Fase::Carrusel) p.JugarCarrusel(0);
        p.Jugar(0);
        p.Listo();
        if (p.DueloHumano() >= 0) while (!p.Combate().Terminada()) p.Combate().Paso();
        p.TerminarCombate();
        p.SiguienteRonda();
      }
      std::vector<std::string> ids;
      int principal = -1, cuenta = -1, umbral = -1;
      if (p.Acabada() && p.Campeon() >= 0) {
        for (int s = kBanco; s < kSlots; ++s) {
          const Pieza& z = p.J(p.Campeon()).pz[s];
          if (z.hay()) ids.push_back(p.Eco().unidades[size_t(z.unidad)].id);
        }
        for (const SinActiva& s : p.Sinergias(p.Campeon()))
          if (s.umbral >= 0 && (s.cuenta > cuenta || (s.cuenta == cuenta && s.umbral > umbral)))
            principal = s.sinergia, cuenta = s.cuenta, umbral = s.umbral;
      }
      std::sort(ids.begin(), ids.end());
      ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
      std::string comp;
      for (const std::string& id : ids) comp += (comp.empty() ? "" : ",") + id;
      REXLOG_INFO("dbz3 tactics audit: torneo {} fin {} ronda {} campeon {} principal {} composicion {}",
                  500000 + t, p.Acabada(), p.Ronda(), p.Campeon(),
                  principal < 0 ? "ninguna" : p.Eco().sinergias[size_t(principal)].id, comp);
    }
  }
