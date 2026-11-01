// Se incluye una vez desde iw_nave_hud_b2.inl, en la MISMA TU/cola del consumidor.
namespace hud_b2_detalle {
int CuentaVisible(const MuestraMarcadorB2& m) {
  return m.planta == 0 ? m.vencidos : m.planta == 1 ? (int(m.cuadros)+59)/60 : m.planta == 2 ? m.golpes : m.ondas;
}
bool ResolverCaras(MuestraMarcadorB2& m, noche::ContextoVisual& c) {
  for (int i = 0; i < (m.planta == 0 ? 2 : 1); ++i) {
    const int id = m.personaje_b3[i], forma = m.forma_b3[i];
    auto cara = noche::CaraDeB3Documentada(id, forma);
    if (id >= 0 && !noche::FamiliaDocumentada(id) && forma == 0) cara = {B2DeB3(id),0};
    Paquete* p = cara.personaje_b2 >= 0 ? Cara(cara.personaje_b2) : nullptr;
    const bool ok = p && cara.textura >= 0 && cara.textura < int(p->b.tx.size()) && p->b.tx[size_t(cara.textura)].t;
    auto& s = c.lado[i];
    if (DentroArnesNave() && (id != s.log_id || forma != s.log_forma)) {
      s.log_id = id; s.log_forma = forma;
      REXLOG_INFO("dbz3 nave hud-b2 forma: lado={} idB3={} forma={} idB2={} cara={} contador={} vida={} B2={}",
                  i+1,id,forma,cara.personaje_b2,cara.textura,CuentaVisible(m),m.vida[i],ok);
    }
    if (!ok) return false;  // No esconder B3 con una cara SSJ4/fusion/mod inexistente.
    m.personaje_b2[i] = cara.personaje_b2; m.cara_b2[i] = cara.textura;
  }
  return true;
}

bool ColorEstado(Orden& o, int clip, float tick) {
  const auto& p = R().cuerpo;
  if (clip < 0 || clip >= int(p.a.size()) || clip >= int(p.meta.size())) return false;
  const Pose2 color = PoseAma2(p.a[size_t(clip)],noche::MuestraPiezasOriginal(tick,p.meta[size_t(clip)]),false);
  for (int k = 0; k < 3; ++k) o.q.m[k] = color.m[k];
  o.q.a *= color.a; o.q.niebla = color.niebla;
  return true;
}

bool PrepararContexto(const MuestraMarcadorB2& m, bool transitorios, noche::ContextoVisual& c) {
  const bool reiniciar = !c.iniciado || c.encuentro != m.encuentro || c.planta != m.planta ||
                         m.cuadros < c.ultimo_cuadro || c.transitorios != transitorios;
  if (reiniciar) {
    c = {}; c.iniciado = true; c.encuentro = m.encuentro; c.planta = m.planta;
    c.ultimo_cuadro = m.cuadros; c.transitorios = transitorios;
    for (int i = 0; i < 2; ++i) noche::InicializarVida(c.lado[i].vida,int(std::clamp(m.vida[i]*0.1f,0.0f,700.0f)));
  }
  if (!transitorios) { c.ultimo_cuadro = m.cuadros; return true; }
  if (R().cuerpo.meta.size() <= 47 || !std::isfinite(m.paso_hud_original) ||
      m.paso_hud_original <= 0 || m.paso_hud_original > 4) return false;
  const float delta = m.cuadros-c.ultimo_cuadro;
  c.ultimo_cuadro = m.cuadros;
  if (delta > 600) { c.iniciado = false; return false; } // faltan eventos entre lecturas: ceder, no inventarlos
  if (!m.pausa) c.credito += std::max(0.0f,delta);
  for (int i = 0; i < 2; ++i) {
    auto& s = c.lado[i]; const auto& e = m.dano[i];
    s.fuente_dano = e.valido;
    if (!e.valido) {
      s.vida.flags &= ~(0x40u|0x80u|0x800000u);
      s.pendiente_inicio = s.pendiente_fin = false;
    } else if (e.secuencia > s.secuencia) {
      s.secuencia = e.secuencia;
      s.pendiente_inicio = s.pendiente_inicio || e.inicio;
      s.pendiente_fin = s.pendiente_fin || e.fin_recepcion;
    } else if (e.secuencia < s.secuencia) {
      s.fuente_dano = false; s.vida.flags &= ~(0x40u|0x80u|0x800000u);
      s.pendiente_inicio = s.pendiente_fin = false;
    }
  }
  while (c.credito+0.00001f >= m.paso_hud_original && !m.pausa) {
    c.credito = std::max(0.0f,c.credito-m.paso_hud_original);
    for (int i = 0; i < 2; ++i) {
      auto& s = c.lado[i];
      noche::EntradaVida e;
      e.unidades = int(std::clamp(m.vida[i]*0.1f,0.0f,700.0f));
      e.delta_original = m.paso_hud_original;
      e.nuevo_evento_dano = s.fuente_dano && s.pendiente_inicio;
      e.fin_recepcion = s.fuente_dano && s.pendiente_fin;
      noche::TickVidaOriginal(s.vida,e);
      s.pendiente_inicio = s.pendiente_fin = false;
      if (s.vida.flags & 0x200) noche::TickClipOriginal(s.vida.tiempo_ko,e.delta_original,false,R().cuerpo.meta[47]);
      else if (s.vida.flags & 0x20) noche::TickClipOriginal(s.vida.tiempo_critico,e.delta_original,false,R().cuerpo.meta[46]);
    }
  }
  if (DentroArnesNave()) for (int i = 0; i < (m.planta == 0 ? 2 : 1); ++i) {
    auto& s = c.lado[i]; const uint32_t estado = s.vida.flags & (0x20u|0x200u);
    if (estado != s.log_estado) {
      s.log_estado = estado;
      REXLOG_INFO("dbz3 nave hud-b2 estado: lado={} critico={} KOvisual={} contador={} vida={} estela={} fuente_dano={} cuadros={}",
                  i+1,bool(estado&0x20),bool(estado&0x200),CuentaVisible(m),m.vida[i],s.vida.estela,s.fuente_dano,m.cuadros);
    }
  }
  return true;
}

bool Cargar2P(int planta, InformacionDosJugadoresB2& info, bool permiso) {
  info = {};
  if (!DentroArnesNave() || !permiso || planta < 0 || planta > 3 || !Preparar()) return false;
  auto& r = R();
  if (r.intento2p[planta]) { info = r.info2p[planta]; return info.listo; }
  r.intento2p[planta] = true;
  InformacionDosJugadoresB2 out; out.modo = 10+planta;
  uint32_t entrada = ~0u;
  if (!DatoElf(0x373C50+4*uint32_t(out.modo),entrada) ||
      !DatoElf(0x373ED0+4*uint32_t(out.modo),out.dibujo_cuerpos) ||
      !DatoElf(0x373F10+4*uint32_t(out.modo),out.dibujo_modo) ||
      !DatoElf(0x373FD0+4*uint32_t(out.modo),out.actualizar_cuerpos)) return false;
  if (entrada == 0xFFFFFFFFu) {
    out.sin_paquete = true; out.listo = out.modo == 13; // IV2P usa comunes, no una entrada AFS -1.
  } else {
    out.entrada = int(entrada);
    if (entrada >= r.afs.Cuantas() || !Cargar(r.dos_jugadores[planta],r.afs.Entrada(entrada),0,1)) return false;
    out.nodo_ama = int(r.dos_jugadores[planta].a.size());
    out.texturas = int(r.dos_jugadores[planta].b.tx.size());
    out.listo = true;
  }
  r.info2p[planta] = info = out;
  REXLOG_INFO("dbz3 nave hud-b2 recursos2P: modo={} entrada={} nodos={} texturas={} comun={} dibujo=OFF",
              out.modo,out.entrada,out.nodo_ama,out.texturas,out.sin_paquete);
  return out.listo;
}
}  // namespace hud_b2_detalle

bool LeerRecursosDosJugadoresB2(int planta, InformacionDosJugadoresB2& info) {
  return hud_b2_detalle::Cargar2P(planta,info,hud_b2_detalle::OpcionPrueba("DBZ3_TEST_GP2_HUD_RECURSOS_2P"));
}
