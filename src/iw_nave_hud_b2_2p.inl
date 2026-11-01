// Reproduccion de snapshots originales2P: solo fixture. NO productores inventados para el combate B3.
namespace original2p {
bool TextoPorcentaje(const Muestra& m, int lado) {
  using namespace hud_b2_detalle;
  auto& r = R(); auto& p = r.dos_jugadores[2];
  const int numero = m.lado[lado].porcentaje_presentado;
  if (numero < 0 || numero > 999999) return false;
  std::vector<size_t> lista;
  const auto add = [&](int nodo, int cifra) {
    if (!Poner(p.a,r.reloj.b,nodo,0,-1,0,cifra)) return false;
    lista.push_back(r.cola.size()-1); return true;
  };
  if (!add(5,-1)) return false;
  const size_t mas = lista.back();
  for (char c : std::to_string(numero)) if (!add(6,c-'0')) return false;
  if (!add(7,-1)) return false;
  const auto anchor = r.cola[m.ancla_modo12[lado] == 5 ? mas : lista.back()];
  float total = 0;
  for (size_t i : lista) {
    const auto& o = r.cola[i]; if (o.q.rot != 0) return false;
    total += std::abs((o.a.x1-o.a.x0)*o.q.sx);
  }
  float x = anchor.q.x;
  if (m.alineacion_modo12[lado] == 4) x += anchor.a.x1*anchor.q.sx-total;
  else if (m.alineacion_modo12[lado] != 1) return false;
  for (size_t i : lista) {
    auto& o = r.cola[i]; const float w = std::abs((o.a.x1-o.a.x0)*o.q.sx);
    o.a.x0 = 0; o.a.x1 = w; o.a.y0 *= o.q.sy; o.a.y1 *= o.q.sy;
    o.q.sx = o.q.sy = 1; o.q.x = x; o.q.y = anchor.q.y; x += w;
  }
  return true;
}
bool PintarFixture(ImDrawList* dl, const Muestra& m) {
  using namespace hud_b2_detalle;
  if (!OpcionPrueba("DBZ3_TEST_GP2_HUD_FIXTURE") || !m.productores_autoritativos || !dl || m.modo < 10 || m.modo > 13) return false;
  const int planta = m.modo-10;
  InformacionDosJugadoresB2 info;
  if (!Cargar2P(planta,info,true)) return false;
  if (m.modo == 10 && m.lado_activo >= 0) return false;
  for (const auto& l : m.lado) {
    if (l.coronas < 0 || l.coronas > 9 || l.vida_frente < 0 || l.vida_frente > 700) return false;
    if (m.modo == 11 && l.estado_popup != 0) return false;
    if (m.modo == 12 && (l.flags_vista & 0x100)) return false;
  }
  auto& r = R(); r.cola.clear(); auto& p = r.dos_jugadores[planta];
  for (int lado = 0; lado < 2; ++lado) {
    const auto& l = m.lado[lado]; auto* cara = Cara(l.personaje_b2);
    noche::EstadoVida e; e.flags = l.flags_vista; e.tiempo_ko = l.tiempo_ko; e.tiempo_critico = l.tiempo_critico;
    if (!cara || l.cara < 0 || l.cara >= int(cara->b.tx.size()) || !Retrato(lado,*cara,l.cara,&e)) return false;
    if (!Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,0x70)) ||
        !Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,0x74),0,-1,0,l.coronas)) return false;
    if (m.modo == 11) {
      if (!Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,0x58)) ||
          !Poner(p.a,p.b,m.vida_modo11[lado],float(l.vida_frente)) ||
          !Poner(p.a,p.b,m.marco_modo11[lado])) return false;
    } else if (m.modo == 12) {
      if (l.porcentaje_objetivo < 0 || l.porcentaje_objetivo > 700 || !Poner(p.a,p.b,l.burbuja_nodo,m.tick_modo)) return false;
      const float fr = float(l.porcentaje_objetivo)/700;
      auto& o = r.cola.back(); o.q.sx *= 1+fr; o.q.m[1] *= 1-fr; o.q.m[2] *= 1-fr;
      if (!TextoPorcentaje(m,lado)) return false;
    } else if (m.modo == 13) {
      if (!Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,8),float(l.vida_frente)) ||
          !Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,16),float(l.vida_frente),-1,0,-1,true) ||
          !Poner(r.cuerpo.a,r.cuerpo.b,Nodo(lado,0x60)) ||
          !Poner(r.cuerpo.a,r.reloj.b,Nodo(lado,0x68),0,-1,0,l.vida_frente/100)) return false;
    }
  }
  if (m.modo == 11 && !Poner(p.a,p.b,1)) return false;
  return Descargar(dl);
}
}  // namespace original2p
