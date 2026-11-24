// TACTICS-V02b: maqueta v2 del menu propio (01 menu de entrada, 01b opciones, 02 continuar, 09 aviso de partida vieja).
// Copia de Arena::Menu con la MISMA logica (filas, mc_, menu_pag_, Marcar(Zona::Botones, i) en el mismo orden); solo cambia el dibujo.
// Se incluye DENTRO de la clase Arena (src/tactics/entrada.cpp), despues de v2_piezas.inc. Lienzo 1600x900 (maquetas 1280x720 x1,25).
  void MenuV2(ImDrawList* dl, const ImGuiIO& io) {
    const Lienzo q = LienzoDe(io);
    hits_.clear();
    const float t = float(ImGui::GetTime());
    // Titulo arriba a la izquierda y subtitulo (pauta 15)
    // V05 (maquetas 01/01b/02/09): fondo del Torneo Mundial del juego, logo grande con subtitulo y el locutor grande abajo a la izquierda
    // ponytail: recorte 16:9 del dibujo de 900x672 que se supone en tac_torneo_fondo (como los fondos 1024 del Heroe); se ajusta si la captura lo desmiente
    if (const S::Pic fo = S::Img("tac_torneo_fondo")) S::DrawPic(dl, fo, q.At(0, 0), q.At(1600, 900), ImVec4(0, 83, 900, 589), IM_COL32_WHITE);
    if (const S::Pic lo = S::Img("tac_locutor")) {
      const float alto = 516, ancho = alto * lo.w / std::max(1.0f, lo.h);   // R3 (medidas_v05/01): locutor 0-459 x 384-900
      S::DrawPic(dl, lo, q.At(0, 900 - alto), q.At(ancho, 900), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    }
    LogoV2(dl, q, 0.95f);
    Txt(dl, q, 50, 155, 26, menu_pag_ == 1 ? std::string(T::T("Opciones", "Options")) : std::string(T::T("Torneo de 8 entrenadores", "8-trainer tournament")), 0, false, false, 460);
    std::vector<std::string> filas;
    std::vector<std::string> valores;
    if (menu_pag_ == 0) {
      for (int id : FilasMenu()) {
        if (id == 0) filas.push_back(T::T("Continuar", "Continue"));   // ronda y entrenadores van en la tarjeta "Tu partida"
        else filas.push_back(id == 1 ? (guardada_ ? T::T("Nueva partida", "New game") : T::T("Jugar", "Play")) : id == 2 ? T::T("Cómo se juega", "How to play") : id == 3 ? T::T("Opciones", "Options") : T::T("Volver al HUB", "Back to the HUB"));
      }
    } else {
      filas = {T::T("Reloj de preparación", "Prep timer"), T::T("Velocidad inicial", "Starting speed"), T::T("Sonidos de combate", "Combat sounds"), T::T("Dificultad de los bots", "Bot difficulty"), T::T("Atrás", "Back")};
      valores = {opc_.reloj ? std::to_string(opc_.reloj) + " s" : std::string(T::T("Apagado", "Off")), "x" + std::to_string(opc_.vel), opc_.sonido ? T::T("Sí", "On") : T::T("No", "Off"),
                 opc_.dif == 0 ? T::T("Fácil", "Easy") : opc_.dif == 1 ? T::T("Normal", "Normal") : T::T("Difícil", "Hard"), ""};
    }
    // Panel de las filas (marco nativo, mas translucido que el v1): a la derecha, como en las maquetas
    const float px0 = menu_pag_ == 0 ? 600.0f : 512.0f, px1 = menu_pag_ == 0 ? 1332.0f : 1494.0f, py0 = 140, paso = menu_pag_ == 0 ? 78.5f : 70.0f, alto = menu_pag_ == 0 ? 68.0f : 62.0f;   // R3: filas en 198 + 78,5 k (01) / 195 + 70 k (01b)
    const float yc = PanelV2(dl, q, px0, py0, px1, py0 + 36 + paso * float(filas.size()), nullptr, 130);   // V05: translucido (se ve el Torneo detras)
    for (size_t i = 0; i < filas.size(); ++i) {
      const float y = yc + 12 + paso * float(i), x0 = px0 + 22, x1 = px1 - 22;
      const bool el = mc_ == int(i);
      if (el) S::Highlight(dl, q.At(x0, y), q.At(x1, y + alto), t);
      else dl->AddRectFilled(q.At(x0, y + 2), q.At(x1, y + alto - 2), IM_COL32(8, 16, 40, 70), 4 * q.s);   // fila sin elegir: fondo oscuro suave para el contraste
      if (menu_pag_ == 0) {
        Botella(dl, q.At(x0 + 26, y + alto * 0.5f), 58 * q.s, el ? 1 : 2);
        FilaMenuV2(dl, q, x0 + 92, y + alto * 0.5f, 40, filas[i], el, x1 - x0 - 110);   // V05: filas grandes en turquesa, como la maqueta
      } else if (!valores[i].empty()) {
        FilaMenuV2(dl, q, x0 + 28, y + alto * 0.5f, 38, filas[i], el, 560);   // V05: turquesa grande y el valor entre las flechas del juego
        S::Arrows(dl, q.At(x1 - 250, y + 12), q.At(x1 - 20, y + alto - 12), (alto - 30) * q.s, t);
        S::MenuTextB3(dl, q.At(x1 - 135, y + alto * 0.5f), 38 * q.s, valores[i].c_str(), false, false, 170 * q.s);   // R3: valor en blanco como la maqueta 01b
      } else {
        FilaMenuV2(dl, q, x0 + 28, y + alto * 0.5f, 38, filas[i], el, 560);
      }
      Marcar(Zona::Botones, int(i), q.At(x0, y), q.At(x1, y + alto));
    }
    // 02 Continuar: tarjeta "Tu partida" con lo que trae la partida guardada (ronda, vida y el equipo del tablero)
    if (menu_pag_ == 0 && guardada_) {
      const Partida& g = *guardada_;
      const float cy = PanelV2(dl, q, 44, 182, 532, 182 + 58 + 3 * 44 + 6, T::T("Tu partida", "Your game"));   // V05: tres placas, como la maqueta 02
      char ronda[64];
      std::snprintf(ronda, sizeof ronda, T::T("%d (quedan %d)", "%d (%d left)"), g.Ronda(), g.Vivos());
      PlacaV2(dl, q, 58, cy, 518, cy + 38, T::T("Ronda", "Round"), ronda);
      PlacaV2(dl, q, 58, cy + 44, 518, cy + 82, T::T("Vida", "Health"), std::to_string(g.J(0).vida));
      PlacaV2(dl, q, 58, cy + 88, 518, cy + 126, T::T("Equipo", "Team"), "");
      float fx = 492;   // caras en circulo de las piezas del tablero, de derecha a izquierda (como mucho 6)
      for (int s = kBanco, k = 0; s < kSlots && k < 6; ++s) {
        const int u = g.J(0).pz[s].unidad;
        if (u < 0 || size_t(u) >= eco_.unidades.size()) continue;
        CaraRedondaV2(dl, CaraV2(eco_.unidades[size_t(u)].id), q.At(fx, cy + 107), 16 * q.s, IM_COL32(20, 24, 40, 255));
        fx -= 38, ++k;
      }
    }    // Bocadillo del Locutor ajustado al texto, con la pestana de quien habla (01, 01b, 02, 09)
    const bool vieja = menu_pag_ == 0 && !aviso_.empty() && !guardada_;
    std::string dice;
    if (vieja) dice = aviso_;
    else if (menu_pag_ == 1) {
      static const char* const es[] = {"Tiempo para colocar tu equipo en cada ronda.\nApagado: empiezas cuando tú digas.", "Velocidad a la que empiezan los combates.", "Golpes y ráfagas durante los combates.",
                                       "Lo bien que juegan los demás entrenadores.", "Vuelve al menú del torneo."};
      static const char* const en[] = {"Time to place your team each round.\nOff: you start when you're ready.", "Speed the fights start at.", "Hits and blasts during the fights.",
                                       "How well the other trainers play.", "Back to the tournament menu."};
      dice = T::T(es[std::clamp(mc_, 0, 4)], en[std::clamp(mc_, 0, 4)]);
    } else if (guardada_) dice = T::T("Tienes un torneo a medias.\n¿Seguimos donde lo dejaste?", "You have a tournament in progress.\nShall we pick up where you left off?");
    else dice = T::T("¡Bienvenido al torneo de Dragon Tactics!\nSe guarda al empezar cada preparación y antes del combate.", "Welcome to the Dragon Tactics tournament!\nSaved at the start of each preparation and before the fight.");
    {
      const auto& fuentes = dbz3::launcher::ui::GetFonts();
      ImFont* f = std::strcmp(T::LangCode(), "ja") == 0 && fuentes.menu ? fuentes.menu : fuentes.speech ? fuentes.speech : ImGui::GetFont();
      const float fs = 36 * q.s, ancho_max = 880 * q.s;   // R3 (medidas_v05/01): lineas de 42 en el bocadillo
      const ImVec2 tam = f->CalcTextSizeA(fs, 1e9f,ancho_max, dice.c_str());
      const float cab = vieja ? 40.0f : 0.0f;                                   // linea "Aviso" encima del texto
      const float bw = std::max(940.0f, tam.x / q.s + 60), bh = std::max(140.0f, tam.y / q.s + cab + 50);
      const float bx0 = 472, by1 = 814, by0 = by1 - bh, bx1 = std::max(1428.0f, std::min(1560.0f, bx0 + bw));   // R3: bocadillo 472-1428 x 675-814
      S::Speech(dl, q.At(bx0, by0), q.At(bx1, by1), q.s, "", -1.0f);
      dl->AddTriangleFilled(q.At(bx0 + 3, by0 + 30), q.At(bx0 + 3, by0 + 62), q.At(bx0 - 34, by0 + 40), IM_COL32(250, 248, 236, 250));   // R3: pico hacia el locutor (maqueta 01)
      PestanaV2(dl, q, bx0, by0, T::T("Locutor del Torneo", "Tournament Announcer"));
      if (vieja) Txt(dl, q, bx0 + 28, by0 + 22 + cab * 0.5f, kV2Fila, T::T("Aviso", "Notice"), 0, true);
      dl->AddText(f, fs, q.At(bx0 + 30, by0 + (bh - cab - tam.y / q.s) * 0.5f + cab), IM_COL32(28, 28, 32, 255), dice.c_str(), nullptr, ancho_max);
    }
    // V05: ayuda con los botones del juego
    if (menu_pag_ == 1) AyudaGlifosV2(dl, q, 1584, 862, {{"A", T::T("Cambiar", "Change")}, {"B", T::T("Volver", "Back")}});
    else if (vieja) AyudaGlifosV2(dl, q, 1584, 862, {{"A", T::T("Vale", "OK")}});   // V05: maqueta 09
    else AyudaGlifosV2(dl, q, 1584, 862, {{"A", T::T("Elegir", "Choose")}, {"B", T::T("Volver", "Back")}});
    if (tut_ > 0) Tutorial(dl, q, io);
  }
