// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): dibujo de Elige maestro (maqueta 05_elige_maestro).
// Solo DIBUJO: la entrada (arriba/abajo, A, B), el cursor g_mcur y el aviso g_maviso siguen en Maestros (heroe_ui.cpp);
// llegan por parametro porque se declaran mas abajo. Lo unico que se escribe es el aviso de "se desbloquea al llegar a",
// igual que lo hace hoy el dibujo de siempre.
// Se incluye en heroe_ui.cpp, dentro de dbz3::heroe_ui, junto a heroe_v2_creador.inl. Usa PlacaDuV2 (heroe_v2_ficha.inl).
// Kit: heroe_ui_v2.h. Medidas en px del lienzo 1600x900 (= maqueta 1280x720 x 1.25).
namespace {

// definidas mas abajo en heroe_ui.cpp (mapa y Select Master), en este mismo espacio anonimo
void Esfera(ImDrawList* dl, ImVec2 c, float r, int estrellas, int alfa);
std::string Pct(int v);

// Botella de capsula de la Skill Shop (pieza nativa "capsulas", el mismo recorte que hoy), encendida o en gris.
void CapsulaV2(ImDrawList* dl, const S::Area& q, float cx, float cy, float h, bool encendida) {
  if (const S::Pic cap = S::Img("capsulas")) {
    const float w = h * 31 / 83;
    S::DrawPic(dl, cap, q.At(cx - w * 0.5f, cy - h * 0.5f), q.At(cx + w * 0.5f, cy + h * 0.5f), ImVec4(3, 7, 34, 90),
               encendida ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
  }
}

// Cara del maestro en un circulo oscuro con aro (las caras redondas del Torneo 2047_13..20 FALTAN en la tabla nativa).
void CaraRedonda(ImDrawList* dl, const S::Area& q, int id, float cx, float cy, float r, int alfa, bool sel) {
  dl->AddCircleFilled(q.At(cx, cy), r * q.s, IM_COL32(8, 18, 34, 220), 40);
  if (const S::Pic p = S::Img(("caras/" + Num(id)).c_str())) {   // PM-32: cuadrado central recortado al aro (antes asomaba por la izquierda)
    const float lado = std::min(p.w, p.h), u0 = (p.w - lado) * 0.5f / p.w, v0 = (p.h - lado) * 0.5f / p.h;
    const ImVec2 c = q.At(cx, cy);
    dl->AddImageRounded(p.tex, ImVec2(c.x - r * q.s, c.y - r * q.s), ImVec2(c.x + r * q.s, c.y + r * q.s), ImVec2(u0, v0),
                        ImVec2(1 - u0, 1 - v0), IM_COL32(255, 255, 255, alfa), r * q.s);
  }
  dl->AddCircle(q.At(cx, cy), r * q.s, sel ? IM_COL32(150, 245, 240, 255) : IM_COL32(232, 150, 40, 200), 40, 3 * q.s);
}

void DibujarMaestrosV2(ImDrawList* dl, const S::Area& a, float t, int cur, int act, std::string& maviso) {
  constexpr int n = H::kMaestros;
  if (!v2::Fondo(dl, a, v2::kFondoMaestros)) FondoSinAnfitrion(dl, a, "hub/heroe_fondo_maestros");
  const S::Area q = v2::Lienzo(a);
  const float frac60 = H::EntrenoMinimo() / 100.0f;
  // lista de maestros (izquierda): cara, nombre, barra con la marca del 60 % y el porcentaje; entrenado = esfera
  constexpr float lx0 = 18, lx1 = 668, ly0 = 140, ly1 = 732;
  const float alto = (ly1 - ly0 - 40) / n;
  v2::Panel(dl, q, lx0, ly0, lx1, ly1);
  for (int i = 0; i < n; ++i) {
    const H::InfoMaestro m = H::Maestro(i);
    const float y = ly0 + 20 + alto * (i + 0.5f);
    const bool sel = i == cur;
    if (sel) v2::Franja(dl, q, lx0 + 20, y - alto * 0.5f + 3, lx1 - 20, y + alto * 0.5f - 3, t);
    CaraRedonda(dl, q, m.id, lx0 + 74, y, 36, !m.desbloqueado ? 40 : sel ? 255 : 210, sel);   // bloqueado: en sombra
    v2::Texto(dl, q, lx0 + 130, y - 18, v2::kLetraFila, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), 0, 1, sel,
              !m.desbloqueado, 330);
    if (!m.desbloqueado && m.saga >= 0) {
      v2::Texto(dl, q, lx0 + 130, y + 20, v2::kLetraMin,
                std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(m.saga), 0, 0, false,
                true, lx1 - lx0 - 170);
      continue;
    }
    v2::Progreso(dl, q, lx0 + 130, y + 20, lx0 + 440, m.entreno / 100.0f, frac60, m.entrenado, 14);
    if (m.entrenado) Esfera(dl, q.At(lx1 - 62, y + 8), 20 * q.s, 1, 255);   // la esfera sustituye al porcentaje
    else v2::Texto(dl, q, lx1 - 38, y + 10, v2::kLetraFila, Num(m.entreno) + "%", 2, 0, false, !sel, 110);
    if (i == act) v2::Texto(dl, q, lx1 - 38, y - 24, v2::kLetraMin, T::T("ACTUAL", "CURRENT"), 2, 2, false, false, 120);
  }
  // ficha del maestro elegido (derecha)
  const H::InfoMaestro m = H::Maestro(cur);
  constexpr float px0 = 695, px1 = 1583, py0 = ly0, py1 = ly1;
  v2::Panel(dl, q, px0, py0, px1, py1);
  // T2 (11/11): el titulo va despues de los paneles: el borde del panel izquierdo tapaba el rotulo BETA
  v2::Titulo(dl, q, T::T("Elige maestro", "Select Master"));
  CaraRedonda(dl, q, m.id, px0 + 130, py0 + 125, 82, m.desbloqueado ? 255 : 60, false);
  v2::Texto(dl, q, px0 + 250, py0 + 80, v2::kLetraTitulo, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), 0, 2, false,
            false, px1 - px0 - 300);
  v2::Texto(dl, q, px0 + 250, py0 + 140, v2::kLetraFila,
            std::string(T::T("Entrenamiento completado: ", "Training Completion: ")) + Num(m.entreno) + "%", 0, 0, false, false,
            px1 - px0 - 300);
  v2::Progreso(dl, q, px0 + 250, py0 + 190, px1 - 60, m.entreno / 100.0f, frac60, m.entrenado, 24);
  // maqueta 05: la nota de la regla del 60 % bajo la barra
  v2::Texto(dl, q, px0 + 250, py0 + 226, v2::kLetraMin,
            Num(H::EntrenoMinimo()) + (m.entreno >= H::EntrenoMinimo() ? T::T(" %: ya puedes cambiar de maestro", "%: you can switch master now")
                                                                       : T::T(" %: a partir de ahí puedes cambiar de maestro", "%: from there you can switch master")),
            0, 0, false, true, px1 - px0 - 310);
  dl->AddLine(q.At(px0 + 30, py0 + 250), q.At(px1 - 30, py0 + 250), IM_COL32(232, 150, 40, 255), 2 * q.s);
  // modificadores (los de heroe.cpp kEstilos) sobre placas, y tecnicas con su capsula
  std::vector<std::pair<std::string, bool>> mods;   // texto, positivo
  if (m.vida) mods.push_back({std::string(T::T("Vida ", "Health ")) + (m.vida > 0 ? "+" : "") + Num(m.vida), m.vida > 0});
  if (m.dano != 100) mods.push_back({std::string(T::T("Ataque ", "Attack ")) + Pct(m.dano - 100), m.dano > 100});
  if (m.recibido != 100)
    mods.push_back({std::string(T::T("Daño recibido ", "Damage taken ")) + Pct(m.recibido - 100), m.recibido < 100});
  if (m.ki < 0) mods.push_back({T::T("El ki se recarga solo", "Ki recharges by itself"), true});
  std::vector<std::string> tecs;
  for (std::string s = m.tecnicas; !s.empty();) {
    const size_t c = s.find(", ");
    tecs.push_back(s.substr(0, c));
    s = c == std::string::npos ? std::string() : s.substr(c + 2);
  }
  const float cx = px0 + 490;   // columna de Tecnicas
  v2::Texto(dl, q, px0 + 50, py0 + 285, v2::kLetraFila, T::T("Modificadores", "Modifiers"), 0, 2, false, false, cx - px0 - 80);
  v2::Texto(dl, q, cx, py0 + 285, v2::kLetraFila, T::T("Técnicas", "Techniques"), 0, 2, false, false, px1 - cx - 40);
  const size_t filas = std::min<size_t>(3, std::max(mods.size(), tecs.size()));
  for (size_t i = 0; i < filas; ++i) {
    const float y = py0 + 328 + 44.0f * float(i);
    if (i < mods.size()) {
      PlacaDuV2(dl, q, px0 + 45, y - 21, cx - 40, y + 21);
      v2::Texto(dl, q, px0 + 65, y, v2::kLetraPeque, mods[i].first, 0, 1, mods[i].second, !mods[i].second, cx - px0 - 125);
    }
    if (i < tecs.size()) {
      CapsulaV2(dl, q, cx + 14, y, 40, true);
      v2::Texto(dl, q, cx + 44, y, v2::kLetraPeque, tecs[i], 0, 0, false, false, px1 - cx - 90);
    }
  }
  // lo que da al llegar al 100 %: encendido si ya lo tiene, en gris con su tope si no (MEJORAS 05, ENCAJA)
  const float yc = py0 + 328 + 44.0f * 3 - 12;   // T2 (11/11): la fila del 100 % se salia por abajo
  dl->AddLine(q.At(px0 + 30, yc), q.At(px1 - 30, yc), IM_COL32(232, 150, 40, 255), 2 * q.s);
  v2::Texto(dl, q, px0 + 50, yc + 30, v2::kLetraFila, T::T("Al llegar al 100 %", "At 100%"), 0, 2, false, false, px1 - px0 - 100);
  CapsulaV2(dl, q, px0 + 72, yc + 72, 44, m.entrenado);
  v2::Texto(dl, q, px0 + 110, yc + 72, v2::kLetraPeque,
            std::string(T::T("Cápsula ", "Capsule ")) + m.capsula_n + ": " +
                (m.entrenado ? T::T("desbloqueada", "unlocked") : T::T("se desbloquea al 100 %", "unlocks at 100%")),
            0, 1, m.entrenado, !m.entrenado, px1 - px0 - 160);
  std::string entrena;   // PM-32 HM-03: las sagas en las que gana % este maestro (MaestroQueEntrena), siempre visible
  for (int s = 0; s < H::SagasTotal(); ++s)
    if (s < H::kSagasJugables && H::MaestroQueEntrena(s) == m.id)
      entrena += std::string(entrena.empty() ? "" : ", ") + H::NombreSaga(s);
  v2::Texto(dl, q, px0 + 110, yc + 108, v2::kLetraMin,
            entrena.empty() ? std::string(T::T("Dragon Universe: sin saga asignada", "Dragon Universe: no assigned saga"))
                            : std::string(T::T("Dragon Universe: ", "Dragon Universe: ")) + entrena,
            0, 0, false, false, px1 - px0 - 160);
  // regla del 60 % o el aviso (lo mismo que hoy, con la misma escritura del aviso de "se desbloquea")
  const bool puede = H::PuedeCambiarA(cur);
  if (!m.desbloqueado && maviso.empty() && m.saga >= 0)
    maviso = std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(m.saga);
  if (REXCVAR_GET(dbz3_heroe_motivo_maestro) && !puede && m.desbloqueado && maviso.empty())
    maviso = AvisoAfinidadMaestro();
  const std::string regla = maviso.empty()
                                ? std::string(T::T("Cambia de maestro cuando quieras; a ", "Switch master any time; at ")) +
                                      Num(H::EntrenoMinimo()) + T::T("% abres su técnica clave.", "% you unlock their key technique.")
                                : maviso;
  const float by = py1 + 12;
  const float by1 = v2::Explicacion(dl, q, px0, by, px1, regla, 2);
  if (!puede && !maviso.empty()) dl->AddRect(q.At(px0, by), q.At(px1, by1), IM_COL32(240, 150, 40, 230), 14 * q.s, 0, 2.5f * q.s);
  {   // capsulas ya ganadas (bajo la lista)
    std::string s = T::T("Desbloqueadas: ", "Unlocked: ");
    int k = 0;
    for (int i = 0; i < n; ++i) {
      const H::InfoMaestro x = H::Maestro(i);
      if (!x.entrenado) continue;
      s += std::string(k++ ? ", " : "") + x.capsula_n;
    }
    if (!k) s += T::T("ninguna", "none");
    v2::Explicacion(dl, q, lx0, by, lx1, s, 2);
  }
  v2::BarraAyuda(dl, q,
                 {{"A", puede              ? T::T("Elegir maestro", "Choose master")
                        : !m.desbloqueado ? T::T("Bloqueado", "Locked")
                                          : REXCVAR_GET(dbz3_heroe_motivo_maestro) ? T::T("Falta afinidad", "Affinity required")
                                           : T::T("Bloqueado (60 %)", "Locked (60%)")},
                  {"B", T::T("Atrás", "Back")}},
                 1585, 872);
}

}  // namespace
