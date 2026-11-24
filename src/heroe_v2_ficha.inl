// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): dibujo de la ficha del heroe (pantalla principal, maqueta 03_ficha_heroe).
// Solo DIBUJO: el cursor, la pestana, las filas y sus textos llegan de rpg.cpp como hoy (Principal); la entrada sigue alli.
// La barra de ayuda la pinta rpg.cpp detras de Principal (V05-B: pastilla del kit abajo a la derecha, x 1085-1585):
// aqui no se pinta otra y nada baja de y 846.
// Se incluye en heroe_ui.cpp, dentro de dbz3::heroe_ui, detras de heroe_v2_creador.inl (usa su FiguraV2).
// Kit: heroe_ui_v2.h. Medidas en px del lienzo 1600x900 (= maqueta 1280x720 x 1.25).
namespace {

// Placa de fila de Dragon Universe (data_usi 2020_09, pieza `heroe_placa_du`); sin ella, sustituto sobrio con primitivas.
// La usan tambien los Modificadores de heroe_v2_maestros.inl.
void PlacaDuV2(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1) {
  if (const S::Pic p = S::Img(v2::kPiezaPlacaDu)) {
    S::DrawPic(dl, p, q.At(x0, y0), q.At(x1, y1), ImVec4(1, 7, 442, 67));
    return;
  }
  const float r = (y1 - y0) * 0.3f * q.s;
  dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), IM_COL32(26, 66, 108, 215), r);
  dl->AddRectFilled(q.At(x0 + 4, y0 + 3), q.At(x1 - 4, (y0 + y1) * 0.5f), IM_COL32(120, 190, 240, 34), r);
  dl->AddRect(q.At(x0, y0), q.At(x1, y1), IM_COL32(110, 190, 230, 200), r, 0, 2 * q.s);
}

// HEROE-V05-B: sin la tira "Nv + EXP" ni la pastilla "Ficha 1/1" de arriba a la derecha (la maqueta no las tiene): el
// nivel y la experiencia van en Progreso y la ficha, en la barra de ayuda ("X Ranuras 1/2", rpg.cpp).
void DibujarFichaV2(ImDrawList* dl, const S::Area& a, float t, int cursor, int tab_sel, const std::vector<std::string>& filas,
                    const std::string& detalle, const std::string& aviso, const std::string& /*ficha*/) {
  if (!v2::Fondo(dl, a, v2::kFondoFicha)) FondoSinAnfitrion(dl, a);
  const S::Area q = v2::Lienzo(a);
  v2::Titulo(dl, q, T::T("Tu héroe", "Your hero"));
  const H::Barras b = H::Barras_();
  // personaje a la izquierda: el de Crear (FiguraV2) 70 px mas arriba, para que la placa del nombre quede encima de la
  // barra de ayuda de rpg.cpp
  {
    S::Area qf = q;
    qf.p0.y -= 70 * q.s;
    qf.p1.y -= 70 * q.s;
    FiguraV2(dl, qf);
    const char* combate = TextoCombate();
    const std::string nota = Xv2() && HayGenerando()
                                 ? std::string(T::T("Actualizando el modelo", "Updating the model")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')
                                 : std::string(combate);
    v2::Placa(dl, q, 60, 566, 842, H::NombreMostrado(),
              H::NombreCuerpo() + " - " + T::T("Traje ", "Costume ") + Num(H::Traje() + 1) + "/" + Num(H::NumTrajes()), nota);
  }
  // panel de la derecha: pestanas con LB / RB, filas de la pestana, nivel (Progreso), barras y "Empezar" fijo abajo
  const float x0 = v2::kPanX0, x1 = v2::kPanX1, y0 = v2::kPanY0, y1 = y0 + 672;   // POST2: alto de la maqueta 03 (marco hasta y 760; antes 684)
  v2::Panel(dl, q, x0, y0, x1, y1);
  // HEROE-V05-B: pestanas pequenas como la maqueta (letra de fila pequena, sin mayusculas); solo la activa con la franja
  static const char* kTabES[3] = {"Apariencia", "Combate", "Progreso"};
  static const char* kTabEN[3] = {"Appearance", "Combat", "Progress"};
  const int tab = std::clamp(tab_sel, 0, 2);
  {
    const float gh = 30 * q.s, cy = y0 + 44;
    S::Glyph(dl, "LB", q.At(x0 + 30, cy), gh);
    S::Glyph(dl, "RB", ImVec2(q.At(x1 - 30, cy).x - S::Glyph(nullptr, "RB", ImVec2(), gh), q.At(0, cy).y), gh);
    const float tw = (x1 - x0 - 200) / 3.0f;
    for (int i = 0; i < 3; ++i) {
      const float a0 = x0 + 100 + tw * i, a1 = a0 + tw - 10;
      if (i == tab) v2::Franja(dl, q, a0 + 20, cy - 22, a1 - 20, cy + 22, t);
      const std::string et = T::T(kTabES[i], kTabEN[i]);
      const float ew = std::min(tw - 50, v2::Ancho(v2::kLetraPeque * q.s, et) / q.s);   // etiqueta fina del kit, centrada
      v2::Etiqueta(dl, q, (a0 + a1 - ew) * 0.5f, cy, v2::kLetraPeque, et, i == tab, tw - 50);
    }
    dl->AddLine(q.At(x0 + 30, y0 + 82), q.At(x1 - 30, y0 + 82), IM_COL32(232, 150, 40, 255), 2 * q.s);
  }
  static const int kFilasTab[3][4] = {{0, 1, 2, 10}, {3, 4, 5, 9}, {6, 7, -1, -1}};   // las mismas que Principal
  constexpr float alto = 62;   // POST2: filas de la maqueta (Puntos en y 209, Maestros en 275)
  const float xv = x0 + 375;   // columna de los valores (como el creador)
  for (int k = 0; k < 4; ++k) {
    const int f = kFilasTab[tab][k];
    if (f < 0 || f >= int(filas.size())) continue;
    const float cy = y0 + 92 + alto * (k + 0.5f);
    const bool sel = f == cursor;
    std::string et = filas[size_t(f)], val;
    if (const size_t p = et.find(": "); p != std::string::npos) {
      val = et.substr(p + 2);
      et.resize(p);
    }
    const bool gris = f == 10 && !H::Xv2Instalado();   // piezas XV2: gris si no hay Xenoverse 2
    if (gris) {
      if (sel) v2::Franja(dl, q, x0, cy - alto * 0.5f + 2, x1, cy + alto * 0.5f - 2, t);
      v2::Etiqueta(dl, q, x0 + 22, cy, v2::kLetraFila, et, sel, xv - x0 - 40);
      v2::Texto(dl, q, xv + alto * 0.72f + 30, cy, v2::kLetraFila - 2, val, 0, 0, false, true, x1 - xv - alto * 0.72f - 90);
    } else {
      v2::Fila(dl, q, t, x0, x1, cy, alto, et, val, sel, xv, 0, f != 2 && f != 4 && f != 7, true);   // V05-B: etiqueta fina
    }
  }
  if (tab == 2) {   // Progreso: nivel, experiencia y lo que falta, sobre placas de fila de Dragon Universe
    const std::string vals[3] = {Num(b.nivel), Num(b.xp), Num(std::max(0, b.xp_max - b.xp))};
    const char* ets[3] = {T::T("Nivel", "Level"), T::T("Experiencia", "Experience"), T::T("Al siguiente nivel", "To next level")};
    for (int i = 0; i < 3; ++i) {
      const float cy = y0 + 252 + 57.0f * i;   // POST2: placas de la maqueta (y 340/397/455, 50 de alto, letra de fila)
      PlacaDuV2(dl, q, x0 + 50, cy - 25, x1 - 50, cy + 25);
      v2::Texto(dl, q, x0 + 80, cy, v2::kLetraFila, ets[i], 0, 0, false, false, 500);
      v2::Texto(dl, q, x1 - 80, cy, v2::kLetraFila, vals[i], 2, 0, false, false, 200);
    }
  }
  {   // barras de siempre (salud, dano, guardia) con su numero; el nivel va en Progreso. V05-B: turquesa redondeada (kit)
    struct Est { const char* es; const char* en; int v, m; std::string txt; };
    const Est es[3] = {{"Salud", "Health", b.vida, b.vida_max, Num(b.vida)},
                       {"Daño", "Damage", b.dano, b.dano_max, Num(b.dano) + " %"},
                       {"Guardia", "Guard", b.resist, b.resist_max, Num(b.resist) + " %"}};
    for (int i = 0; i < 3; ++i) {
      const float cy = y0 + 430 + 47.0f * i;   // POST2: y 518/565/612 como la maqueta
      v2::Texto(dl, q, x0 + 60, cy, v2::kLetraPeque, T::T(es[i].es, es[i].en), 0, 0, false, false, 220);
      v2::BarraFina(dl, q, x0 + 290, cy, x0 + 700, float(es[i].v) / float(std::max(1, es[i].m)), 20);
      v2::Texto(dl, q, x1 - 55, cy, v2::kLetraPeque, es[i].txt, 2, 0, false, false, 160);
    }
  }
  if (filas.size() > 8) {   // "Empezar": fijo abajo dentro del marco, a todo el ancho, letra grande
    // V05-B: siempre sobre la banda de agua con borde (maqueta); sin elegir, velada; elegida, viva y con aro dorado
    const float ey0 = y0 + 574, ey1 = y0 + 650;   // POST2: banda de la maqueta (y 662..738)
    const bool sel = cursor == 8;
    v2::Franja(dl, q, x0 + 24, ey0, x1 - 24, ey1, t);
    if (!sel) dl->AddRectFilled(q.At(x0 + 24, ey0), q.At(x1 - 24, ey1), IM_COL32(6, 20, 40, 80));
    dl->AddRect(q.At(x0 + 22, ey0 - 2), q.At(x1 - 22, ey1 + 2),
                sel ? IM_COL32(255, 220, 70, 235) : IM_COL32(120, 200, 230, 170), 4 * q.s, 0, (sel ? 3.0f : 2.0f) * q.s);
    v2::Texto(dl, q, (x0 + x1) * 0.5f, (ey0 + ey1) * 0.5f, v2::kLetraTitulo, filas[8], 1, 2, sel, false, x1 - x0 - 120);
  }
  // explicacion bajo el panel: el detalle de hoy (o el aviso, con borde naranja), las lineas que quepan (hasta 3)
  const float ey = y1 + 20;
  const int lineas = 1;   // POST2: una linea como la maqueta (antes 3, pisaba la barra de ayuda)
  const float ey1 = v2::Explicacion(dl, q, x0 - 18, ey, x1 + 18, aviso.empty() ? detalle : aviso, lineas);
  if (!aviso.empty()) dl->AddRect(q.At(x0 - 18, ey), q.At(x1 + 18, ey1), IM_COL32(240, 150, 40, 230), 14 * q.s, 0, 2.5f * q.s);
}

}  // namespace
