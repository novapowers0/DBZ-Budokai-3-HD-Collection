// Maqueta v2 (studies 5/ENTREGAS/TACTICS_VISUAL/maquetas_v2): carrusel, ficha, tutorial y clasificacion final.
// Se incluye DENTRO de la clase Arena (entrada.cpp). La logica (Marcar, Elegido, reloj, eleccion, paginas) es la de los originales;
// solo cambia donde y como se dibuja. Ayudas PanelV2/PlacaV2/AyudaV2/PestanaV2 y tamanos kV2* en v2_piezas.inc.

  // 08: cartas compactas abajo, orden a la izquierda, ficha a la derecha, cuenta atras grande arriba y "¡Te toca!" en una ficha a la izquierda
  void PantallaCarruselV2(ImDrawList* dl, const ImGuiIO& io, double ahora) {
    const Lienzo q = LienzoDe(io);
    const Partida& pa = *partida_;
    const bool hecho = carr_fin_t_ >= 0;
    const std::vector<ObjCarrusel>& objs = hecho ? pa.UltimoCarrusel() : pa.Carrusel();
    const ImU32 amarillo = IM_COL32(255, 220, 60, 255);
    if (hecho && ahora - carr_fin_t_ > 3.0) {
      carr_fin_t_ = -1;
      EntrarPreparacion();
      return;
    }
    const bool mi_turno = !hecho && pa.QuienElige() == 0;
    if (mi_turno && carr_resto_ > 0 && !tut_) {   // con el reloj de Opciones encendido, 20 s para elegir; si se acaba, el objeto enfocado (o el primero libre)
      carr_resto_ -= std::min(io.DeltaTime, 0.25f);
      if (carr_resto_ <= 0) {
        int i = ic_ >= 0 && ic_ < int(objs.size()) && objs[size_t(ic_)].quien < 0 ? ic_ : -1;
        for (size_t k = 0; i < 0 && k < objs.size(); ++k) if (objs[k].quien < 0) i = int(k);
        REXLOG_INFO("dbz3 tactics: se acabo el tiempo del carrusel");
        return ElegirCarrusel(i, "reloj");
      }
    }
    const auto nombre = [&](int unidad) { const Modelo* m = ModeloDe(eco_.unidades[size_t(unidad)].id); return std::string(T::T(m->es, m->en)); };
    const auto entrenador = [&](int p) { return p == 0 ? std::string(T::T("Tú", "You")) : nombre([&] { for (size_t u = 0; u < eco_.unidades.size(); ++u) if (eco_.unidades[u].id == kEntrenador[p]) return int(u); return 0; }()); };
    // V05 (maqueta 08): cabecera como la de preparacion, orden a la izquierda como la lista de Jugadores, banquillo, 8 cartas como las de la tienda
    LogoV2(dl, q);
    const bool con_reloj = mi_turno && carr_resto_ > 0;
    PanelV2(dl, q, 512, 6, 1088, 86, nullptr);
    Txt(dl, q, 530, 38, 28, std::string(T::T("Ronda ", "Round ")) + std::to_string(pa.Ronda()), 0, false, false, 130);
    Txt(dl, q, 800, 38, 40, Texto("carr_fase"), 1, true, false, 330);
    char tiempo[32];
    if (con_reloj) std::snprintf(tiempo, sizeof tiempo, "%d:%02d", int(carr_resto_ + 0.99f) / 60, int(carr_resto_ + 0.99f) % 60);
    else std::snprintf(tiempo, sizeof tiempo, "%s", T::T("SIN LÍMITE", "NO LIMIT"));
    Txt(dl, q, 1072, 38, con_reloj ? kV2Num : kV2Pie, tiempo, 2, con_reloj && carr_resto_ < 6.0f, false, 170);
    const float fr = con_reloj ? std::clamp(carr_resto_ / 20.0f, 0.0f, 1.0f) : 1.0f;   // 20 s: EntrarCarrusel
    dl->AddRectFilled(q.At(530, 66), q.At(990, 74), IM_COL32(0, 0, 0, 200), 3 * q.s);
    dl->AddRectFilled(q.At(530, 66), q.At(530 + 460 * fr, 74), fr < 0.3f && con_reloj ? IM_COL32(230, 70, 60, 255) : IM_COL32(70, 190, 255, 255), 3 * q.s);
    if (ahora - salir_t_ < 3.0) Txt(dl, q, 800, 124, kV2Fila, T::T("Pulsa B otra vez para salir al HUB", "Press B again to go back to the HUB"), 1, true);
    // abajo: 8 cartas (las de la tienda) dentro de un panel; las ya elegidas, oscuras con "Elegida" y la cara de quien se la llevo
    const int n = int(objs.size());
    // R3 (medidas_v05/08): marco 74-1530 x 759-891; 8 cartas de 168 cada 180 centradas en 802
    const float paso = 180, total = float(n) * paso - 12, xi = 802 - total * 0.5f, y0 = 768;
    PanelV2(dl, q, xi - 14, 759, xi + total + 14, 891, nullptr);
    for (int i = 0; i < n; ++i) {
      const ObjCarrusel& o = objs[size_t(i)];
      const float x0 = xi + float(i) * paso, x1 = x0 + 168, y1 = y0 + 116;
      const bool libre = o.quien < 0, el = !hecho && Elegido(Zona::Tienda, i);
      CartaV2(dl, q, x0, y0, o.unidad, libre, el, 168);
      if (o.capsula >= 0) Botella(dl, q.At(x1 - 52, y0 + 52), 30 * q.s, o.capsula, !libre);
      if (el) dl->AddRect(q.At(x0 - 2, y0 - 2), q.At(x1 + 2, y1 + 2), amarillo, 6 * q.s, 0, 4.0f * q.s);
      if (el && libre && mi_turno) dl->AddRectFilled(q.At(x0 + 3, y0 + 3), q.At(x1 - 3, y1 - 3), IM_COL32(255, 255, 255, 22 + int(18.0f * std::sin(float(ahora) * 6.0f))), 5 * q.s);
      if (!libre) {
        dl->AddRectFilled(q.At(x0 + 2, y0 + 30), q.At(x1 - 2, y1 - 2), IM_COL32(0, 0, 0, 140), 5 * q.s);
        Txt(dl, q, x0 + 64, y0 + 72, kV2Fila, T::T("Elegida", "Taken"), 1, false, false, 110);
        CaraRedondaV2(dl, CaraV2(kEntrenador[o.quien]), q.At(x1 - 36, y0 + 72), 24 * q.s,
                      o.quien == 0 ? IM_COL32(70, 150, 255, 255) : IM_COL32(230, 70, 60, 255));
      }
      if (!hecho) Marcar(Zona::Tienda, i, q.At(x0, y0), q.At(x1, y1));
    }
    // izquierda: el orden de eleccion (menos vida primero), con el aspecto de la lista de Jugadores
    const std::vector<int>& lista = hecho ? carr_orden_ : pa.OrdenCarrusel();   // tras el reparto, el orden que tenia la vuelta
    float y = PanelV2(dl, q, 10, 106, 310, 106 + 58 + 52.0f * float(lista.size()) + 14, T::T("Orden de elección", "Pick order"));
    for (size_t k = 0; k < lista.size(); ++k) {
      const int p = lista[k];
      const bool ahora_elige = !hecho && pa.QuienElige() == p;
      if (ahora_elige || p == 0) Caja(dl, q, 18, y, 302, y + 46, ahora_elige ? amarillo : IM_COL32(70, 150, 255, 255), IM_COL32(0, 0, 0, 0), ahora_elige ? 2.5f : 1.5f);
      dl->AddCircleFilled(q.At(34, y + 23), 10 * q.s, IM_COL32(255, 160, 30, 255), 16);
      TxtColV2(dl, q, 34, y + 23, 18, std::to_string(k + 1), IM_COL32(255, 255, 255, 255), 1);
      CaraRedondaV2(dl, CaraV2(kEntrenador[p]), q.At(72, y + 23), 19 * q.s, IM_COL32(20, 24, 40, 255));
      Txt(dl, q, 102, y + 23, 28, entrenador(p), 0, false, false, 140);
      Txt(dl, q, 296, y + 23, kV2Num, std::to_string(pa.J(p).vida), 2, true, false, 50);
      y += 52;
    }
    // izquierda abajo: pastilla oscura con el estado ("¡Te toca!", esperando o reparto hecho), crece hacia arriba con el texto
    {
      const std::string t = Texto(hecho ? "carr_hecho" : mi_turno ? "carr_tu_turno" : "carr_espera");
      ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
      const float alto = std::max(81.0f, fm->CalcTextSizeA(28 * q.s, 1e9f, 250 * q.s, t.c_str()).y / q.s + 24), y1 = 741;
      dl->AddRectFilled(q.At(19, y1 - alto), q.At(300, y1), IM_COL32(34, 40, 52, 225), 14 * q.s);   // R3: pastilla gris oscura sin borde (maqueta 19-300 x 660-741)
      TextoV2(dl, 28 * q.s, q.At(36, y1 - alto + 12), mi_turno ? IM_COL32(255, 225, 90, 255) : IM_COL32(235, 240, 255, 255), t.c_str(), 250 * q.s);
    }
    BanquilloV2(dl, q, false);
    // derecha: ficha unica del objeto enfocado (la de la tienda, con su capsula)
    if (ic_ >= 0 && ic_ < n && zc_ == Zona::Tienda) FichaV2(dl, q, objs[size_t(ic_)].unidad, 1, objs[size_t(ic_)].capsula, -1, -1, true);   // c1 = -1, sin Vender, ficha alta
    // ayuda: el boton del juego, sobre las cartas a la derecha
    if (mi_turno) AyudaGlifosV2(dl, q, 1584, 722, {{"A", T::T("Elegir", "Choose")}});
  }

  // V05: carta de la maqueta (tienda y carrusel): cabecera oscura con el nombre, cuerpo en degradado del color del coste, cara en circulo,
  // emblemas de sinergia (aro amarillo si ya cuentan en tu tablero), bola, moneda y coste en la letra dorada. 180 x 116 del lienzo.
  // ponytail: dibujada a mano hasta la pieza "carta de la tienda" de ENCARGOS_ASSETS/tactics/ENCARGO_GPT.md (la carta del Skill List no se parece)
  void CartaV2(ImDrawList* dl, const Lienzo& q, float x0, float y0, int u, bool viva, bool el, float ancho = 180) {
    const float x1 = x0 + ancho, y1 = y0 + 116;
    const UnidadEco& ue = eco_.unidades[size_t(u)];
    static const ImU32 arriba[4] = {IM_COL32(120, 190, 70, 255), IM_COL32(50, 150, 165, 255), IM_COL32(80, 100, 210, 255), IM_COL32(215, 150, 35, 255)};
    static const ImU32 abajo[4] = {IM_COL32(205, 225, 100, 255), IM_COL32(120, 205, 195, 255), IM_COL32(140, 165, 240, 255), IM_COL32(250, 215, 95, 255)};
    const int rango = std::clamp(ue.coste, 1, 4) - 1;
    const auto apaga = [&](ImU32 c) { return viva ? c : (c & 0xFF000000u) | ((c >> 1) & 0x007F7F7Fu); };
    // arte de GPT (360x232, hueco de la cara O128 en (76,156)); sin el, la carta dibujada a mano de siempre
    const S::Pic arte = S::Img(("tac_carta_v2_" + std::to_string(rango + 1)).c_str());
    if (arte) S::DrawPic(dl, arte, q.At(x0, y0), q.At(x1, y1), ImVec4(0, 0, 0, 0), viva ? IM_COL32_WHITE : IM_COL32(128, 128, 128, 255));
    else {
      dl->AddRectFilledMultiColor(q.At(x0, y0 + 28), q.At(x1, y1), apaga(arriba[rango]), apaga(arriba[rango]), apaga(abajo[rango]), apaga(abajo[rango]));
      dl->AddRectFilled(q.At(x0, y0), q.At(x1, y0 + 30), IM_COL32(14, 40, 30, 240), 5 * q.s, ImDrawFlags_RoundCornersTop);
      dl->AddRect(q.At(x0, y0), q.At(x1, y1), IM_COL32(230, 235, 220, 255), 5 * q.s, 0, 2.0f * q.s);
    }
    const Modelo* m = ModeloDe(ue.id);
    Txt(dl, q, (x0 + x1) * 0.5f, y0 + 15, kV2Fila, T::T(m->es, m->en), 1, el, !viva, 168);
    // R3 (medidas_v05/03, carta C-17 329-505 x 771-880): cara (x0+34, y0+68) r 28; emblemas en x0+84/+114, y0+57, r 12;
    // bola en (x0+84, y0+92); maletin de zenis en (x0+148, y0+48) de 28; coste dorado en (x0+148, y0+82) de 44
    CaraRedondaV2(dl, S::Img(("caras/" + std::to_string(m->b3)).c_str()), q.At(x0 + (arte ? 38 : 34), y0 + (arte ? 78 : 68)), (arte ? 30 : 28) * q.s, IM_COL32(20, 24, 40, 255), viva ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255));
    const std::vector<SinActiva> sin = partida_->Sinergias(0);
    for (int h = 0; h < 2; ++h) {
      int cuenta = 0;
      for (const SinActiva& sa : sin) if (sa.sinergia == ue.sin[h]) cuenta = sa.cuenta;
      const ImVec2 ic = q.At(x0 + 84 + 30.0f * float(h), y0 + 57);
      IconoSinV2(dl, ic, 12 * q.s, eco_.sinergias[size_t(ue.sin[h])].id, true);
      if (cuenta > 0) dl->AddCircle(ic, 14.5f * q.s, IM_COL32(255, 220, 60, 255), 0, 2.0f * q.s);
    }
    Estrellas(dl, q.At(x0 + 84, y0 + 92), 7 * q.s, 1);
    ZenisV2(dl, q, x0 + 148, y0 + 48, 28);
    OroV2(dl, q, x0 + 148, y0 + 82, 44, 40, std::to_string(ue.coste));
    if (!viva) dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), IM_COL32(0, 0, 0, 70), 5 * q.s);
    // POST 12/11 (maqueta 03): si ya tienes copias de esta unidad (banquillo o tablero), borde dorado ancho y una estrella por copia
    // arriba a la derecha: comprarla acerca la fusion de rango
    int copias = 0;
    for (int s = 0; s < kSlots; ++s) copias += partida_->J(0).pz[s].hay() && partida_->J(0).pz[s].unidad == u ? 1 : 0;
    if (copias > 0 && viva) {
      dl->AddRect(q.At(x0 - 2, y0 - 2), q.At(x1 + 2, y1 + 2), IM_COL32(240, 160, 30, 255), 6 * q.s, 0, 5.0f * q.s);
      for (int k = 0; k < std::min(copias, 2); ++k) Estrellas(dl, q.At(x1 - 14 - 18.0f * float(k), y0 - 2), 7 * q.s, 1);
    }
  }
  // Emblema de una sinergia: la cara del personaje que la representa, en circulo (aro dorado si esta activa).
  void IconoSinV2(ImDrawList* dl, ImVec2 c, float r, const std::string& sid, bool activa) {
    static const std::map<std::string, const char*> emblema = {{"saiyan", "goku"}, {"terricolas", "krilin"}, {"freezer", "freezer"}, {"androides", "a18"},
                                                                {"misticos", "superbuu"}, {"guerreros_z", "gohan"}, {"fuerza_bruta", "broly"},
                                                                {"guardianes", "a16"}, {"artilleros", "vegeta"}, {"veloces", "trunks"}, {"regeneradores", "cell"}};
    const auto it = emblema.find(sid);
    const Modelo* m = it != emblema.end() ? ModeloDe(it->second) : nullptr;
    CaraRedondaV2(dl, m ? S::Img(("caras/" + std::to_string(m->b3)).c_str()) : S::Pic{}, c, r, activa ? IM_COL32(255, 200, 50, 255) : IM_COL32(70, 76, 100, 255),
                  activa ? IM_COL32_WHITE : IM_COL32(200, 200, 205, 255));   // POST 12/11: maqueta 05, la cara apagada se ve (no casi negra)
  }
  S::Pic CaraV2(const std::string& id) const { const Modelo* m = ModeloDe(id); return m ? S::Img(("caras/" + std::to_string(m->b3)).c_str()) : S::Pic{}; }
  // Banquillo de la maqueta bajo el tablero: 8 casillas con numero, cara en circulo, estrellas y capsulas. interactivo = zonas de clic (preparacion).
  void BanquilloV2(ImDrawList* dl, const Lienzo& q, bool interactivo) {
    const Jugador& yo = partida_->J(0);
    const ImU32 amarillo = IM_COL32(255, 220, 60, 255), cian = IM_COL32(90, 230, 255, 255);
    PanelV2(dl, q, 452, 652, 1146, 754, nullptr);
    for (int i = 0; i < kBanco; ++i) {
      const float x0 = 462 + 85.0f * float(i), y0 = 662, x1 = x0 + 79, y1 = 744;
      const Pieza& z = yo.pz[i];
      const bool el = interactivo && Elegido(Zona::Banco, i), mano = interactivo && mano_ == i;
      const S::Pic arte = S::Img("tac_casilla");   // arte de GPT 158x164 con su chapa de numero; sin ella, la casilla dibujada
      if (arte) S::DrawPic(dl, arte, q.At(x0, y0), q.At(x1, y1), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
      else dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), IM_COL32(14, 30, 58, 230), 6 * q.s);
      if (z.hay()) {
        const UnidadEco& ue = eco_.unidades[size_t(z.unidad)];
        CaraRedondaV2(dl, CaraV2(ue.id), q.At((x0 + x1) * 0.5f, y0 + 39), 33 * q.s, ColorRango(ue.coste));
        Estrellas(dl, q.At((x0 + x1) * 0.5f, y1 - 10), 6 * q.s, z.estrellas);
      }
      if (!arte || el || mano) Caja(dl, q, x0, y0, x1, y1, el ? amarillo : mano ? cian : IM_COL32(150, 165, 200, 255), IM_COL32(0, 0, 0, 0), el || mano ? 4.0f : 1.5f);
      if (el) dl->AddRectFilled(q.At(x0 + 3, y0 + 3), q.At(x1 - 3, y1 - 3), IM_COL32(255, 255, 255, 22), 5 * q.s);
      if (!arte) dl->AddCircleFilled(q.At(x0 + 11, y0 + 11), 9 * q.s, IM_COL32(255, 160, 30, 255), 16);   // numero de la casilla
      TxtColV2(dl, q, x0 + (arte ? 8.5f : 11), y0 + (arte ? 8.5f : 11), arte ? 13 : 18, std::to_string(i + 1), IM_COL32(20, 14, 6, 255), 1);
      for (int h = 0; h < 2; ++h)
        if (z.hay() && z.caps[h] >= 0) Botella(dl, q.At(x1 - 10, y0 + 14 + 22.0f * float(h)), 22 * q.s, z.caps[h]);
      if (interactivo) Marcar(Zona::Banco, i, q.At(x0, y0), q.At(x1, y1));
    }
  }

  // 03b: ficha unica a la derecha: titulo dorado (nombre), cara, estrellas y coste, atributos en placas DU, tecnica, pasiva, sinergias y capsulas
  // V05: compacta como la maqueta 03b (y 108-640, no pisa ¡Listo!): placas de 32, texto en la letra del juego, sinergias en azul y "X Vender +n"
  void FichaV2(ImDrawList* dl, const Lienzo& q, int u, int est, int c0, int c1, int vende = -1, bool alta = false) {
    const UnidadEco& ue = eco_.unidades[size_t(u)];
    const sim::DefUnidad& d = cat_.unidades[size_t(ue.cat)];
    const Modelo* m = ModeloDe(ue.id);
    const int mult = eco_.mult_estrella[size_t(est - 1)];
    const auto es_en = [](const SinergiaDef& s) { return std::string(T::T(s.es.c_str(), s.en.c_str())); };
    // R3 (medidas_v05/03b): panel 1290-1591 x 106-644; nombre dorado en x 1324 y 146; bola 1550,141; cara (1437,212) r 35;
    // 5 placas 1306-1572 de 32 de alto centradas en 275 + 37,5 k; "Tecnica" en 469; texto desde 490 (24); sinergias en 574 (22, azul);
    // raya en 594; fila Vender en 619 (boton 1309, texto 1359, zenis 1494, "+n" acaba en 1544)
    // alta (carrusel, medidas_v05/08): panel hasta 689, cara (1434,234) r 45, placas de 35 centradas en 332,5 + 42,5 k, Tecnica en 550, texto desde 572, sinergias en 646
    const float x0 = 1290, x1 = 1591, yfin = alta ? 689.0f : 644.0f;
    PanelV2(dl, q, x0, 106, x1, yfin, nullptr);
    Txt(dl, q, x0 + 34, 146, 32, T::T(m->es, m->en), 0, true, false, x1 - x0 - 90);
    dl->AddRectFilled(q.At(x0 + 18, 163), q.At(x1 - 18, 166), IM_COL32(255, 150, 30, 255));
    Estrellas(dl, q.At(1550, 141), 10 * q.s, 1);   // bola del rango arriba a la derecha, como la maqueta
    CaraRedondaV2(dl, S::Img(("caras/" + std::to_string(m->b3)).c_str()), q.At(alta ? 1434.0f : 1437.0f, alta ? 234.0f : 212.0f), (alta ? 45.0f : 35.0f) * q.s, IM_COL32(200, 225, 255, 255));   // POST 12/11: aro claro como la maqueta 03b (el rango ya lo dice la carta)
    if (est > 1) Estrellas(dl, q.At(1437, alta ? 280.0f : 248.0f), 5 * q.s, est);   // estrellas solo si sube de rango (la maqueta, de 1, no las pinta)
    float y = alta ? 315.0f : 259.0f;
    const float ph = alta ? 35.0f : 32.0f, pp = alta ? 42.5f : 37.5f;
    const struct { const char *es, *en; int v; } at[] = {
        {"Vida", "HP", d.vida * mult / 100}, {"Ataque", "Attack", d.ataque * mult / 100}, {"Defensa", "Defense", d.defensa},
        {"Alcance", "Range", d.alcance}, {"Velocidad", "Speed", d.velocidad}};
    for (const auto& a : at) {
      PlacaV2(dl, q, 1306, y, 1572, y + ph, T::T(a.es, a.en), std::to_string(a.v));
      y += pp;
    }
    const char* forma = d.tecnica.forma == sim::Forma::Objetivo ? T::T("un objetivo", "single target") : d.tecnica.forma == sim::Forma::Linea ? T::T("en línea", "line") : T::T("en área", "area");
    Txt(dl, q, x0 + 31, alta ? 550.0f : 469.0f, 28, T::T("Técnica", "Skill"), 0, true);
    y = alta ? 572.0f : 490.0f;
    const float ysin = alta ? 646.0f : 563.0f;
    static const struct { const char *es, *en; } pasiva[] = {
        {"Sin pasiva", "No passive"},
        {"Ki inicial: empieza con %d%% de ki", "Starting ki: begins with %d%% ki"},
        {"Furia: +%d%% de ataque bajo la mitad de vida", "Fury: +%d%% attack below half health"},
        {"Perfora: sus golpes ignoran el %d%% de la defensa", "Pierce: hits ignore %d%% of defense"},
        {"Cazagigantes: su técnica hace +%d%% a quien tiene más vida", "Giant slayer: skill deals +%d%% to bigger targets"},
        {"Coraza: recibe un %d%% menos de daño", "Armor: takes %d%% less damage"},
        {"Ki constante: +%d de ki por segundo", "Steady ki: +%d ki per second"},
        {"Remate: +%d%% de daño a quien tiene menos del 30%% de vida", "Finisher: +%d%% damage to targets under 30%% health"},
        {"Regenera: %d%% de vida por segundo (hasta el 25%% por ronda)", "Regen: %d%% health per second (up to 25%% per round)"}};
    char buf[160];
    std::snprintf(buf, sizeof buf, T::T(pasiva[int(d.pasiva)].es, pasiva[int(d.pasiva)].en), d.pasiva_valor);
    // tecnica (y la pasiva, si la hay) en un solo bloque de 24 que se encoge a 20 si no cabe antes de las sinergias (565)
    std::string tec = std::to_string(d.tecnica.dano) + T::T("% del ataque, ", "% of attack, ") + forma + ", " + T::T("alc. ", "rng ") + std::to_string(d.tecnica.alcance) + ".";
    if (int(d.pasiva) != 0) tec += std::string(" ") + buf;
    ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
    float fs = 24;
    while (fs > 18 && fm->CalcTextSizeA(fs * q.s, 1e9f, 256 * q.s, tec.c_str()).y / q.s > ysin + 2 - y) fs -= 2;
    TextoV2(dl, fs * q.s, q.At(x0 + 26, y), IM_COL32(240, 240, 240, 255), tec.c_str(), 256 * q.s);
    TextoV2(dl, 22 * q.s, q.At(x0 + 26, ysin), IM_COL32(120, 190, 255, 255),
            (es_en(eco_.sinergias[size_t(ue.sin[0])]) + " · " + es_en(eco_.sinergias[size_t(ue.sin[1])])).c_str(), 256 * q.s);
    for (int h = 0; h < 2; ++h)   // capsulas equipadas: botellas a la izquierda de la cara (el nombre esta en la ficha de la capsula)
      if (const int c = h ? c1 : c0; c >= 0) Botella(dl, q.At(x0 + 34 + 26.0f * float(h), 212), 34 * q.s, c);
    if (!alta) dl->AddLine(q.At(x0 + 18, 594), q.At(x1 - 18, 594), IM_COL32(255, 150, 30, 255), 2 * q.s);
    if (vende >= 0) {   // con v2, LT vende (entrada.cpp, Entrada: X es Cambiar)
      const float vy = 619;
      S::Glyph(dl, "LT", q.At(1309, vy), 40 * q.s);
      Txt(dl, q, 1359, vy, 26, T::T("Vender", "Sell"), 0, false, false, 120);
      ZenisV2(dl, q, 1494, vy, 34);
      TxtColV2(dl, q, 1544, vy, 28, "+" + std::to_string(vende), IM_COL32(255, 205, 60, 255), 2);
    }
    (void)yfin;
  }

  // 04 (V05): el tercio de abajo se oscurece salvo lo que explica cada pagina (con marco amarillo y flecha cian), el locutor grande a la izquierda y un
  // bocadillo claro con la pestana, el titulo, las bolitas 1-4 del Torneo y los botones del juego A Siguiente / B Saltar.
  void TutorialV2(ImDrawList* dl, const Lienzo& q, const ImGuiIO& io) {
    // lo que senala cada pagina (lienzo): 1 tienda, 2 banquillo, 3 sinergias, 4 ¡Listo!; y donde va el bocadillo (su borde de abajo)
    static const struct { float x0, y0, x1, y1, by1; bool flecha_arriba; } kPag[4] = {
        {310, 759, 1268, 894, 740, false}, {452, 652, 1146, 754, 888, false}, {10, 106, 310, 420, 888, true}, {1158, 656, 1440, 750, 888, false}};
    const auto& g = kPag[std::clamp(tut_, 1, 4) - 1];
    // oscurecer de y=600 hacia abajo, con un hueco para lo senalado
    const ImVec2 a = q.At(g.x0 - 6, g.y0 - 6), b = q.At(g.x1 + 6, g.y1 + 6);
    // POST 12/11 ronda 2: la maqueta 04 SI oscurece la franja de abajo (desde y 625) y deja el hueco de lo senalado; marco dorado con halo
    const float ytop = q.At(0, 625).y, W = io.DisplaySize.x, H = io.DisplaySize.y;
    const ImU32 sombra = IM_COL32(0, 0, 0, 150);
    if (b.y <= ytop) dl->AddRectFilled(ImVec2(0, ytop), ImVec2(W, H), sombra);
    else {
      const float t0 = std::max(ytop, a.y);
      dl->AddRectFilled(ImVec2(0, ytop), ImVec2(W, t0), sombra);
      dl->AddRectFilled(ImVec2(0, t0), ImVec2(a.x, b.y), sombra);
      dl->AddRectFilled(ImVec2(b.x, t0), ImVec2(W, b.y), sombra);
      dl->AddRectFilled(ImVec2(0, b.y), ImVec2(W, H), sombra);
    }
    dl->AddRect(ImVec2(a.x - 3 * q.s, a.y - 3 * q.s), ImVec2(b.x + 3 * q.s, b.y + 3 * q.s), IM_COL32(255, 220, 60, 90), 10 * q.s, 0, 6.0f * q.s);
    dl->AddRect(a, b, IM_COL32(255, 220, 60, 255), 8 * q.s, 0, 4.0f * q.s);
    // flecha cian que rebota, encima de lo senalado (o debajo, apuntando arriba, si esta arriba de la pantalla)
    const float rebote = 6.0f * std::sin(float(ImGui::GetTime()) * 5.0f), fx = (g.x0 + g.x1) * 0.5f;
    const float fy = g.flecha_arriba ? g.y1 + 14 - rebote : g.y0 - 62 + rebote;
    // R3 (medidas_v05/04): triangulo cian de 44 x 34 con perfil blanco (la pieza tac_flecha_cian salia diminuta)
    {
      const float ty0 = fy + 6, ty1 = fy + 40;
      const ImVec2 p0 = q.At(fx - 22, g.flecha_arriba ? ty1 : ty0), p1 = q.At(fx + 22, g.flecha_arriba ? ty1 : ty0), p2 = q.At(fx, g.flecha_arriba ? ty0 : ty1);
      dl->AddTriangleFilled(p0, p1, p2, IM_COL32(60, 200, 255, 255));
      dl->AddTriangle(p0, p1, p2, IM_COL32(255, 255, 255, 255), 3 * q.s);
    }
    // locutor abajo a la izquierda, al tamano de la maqueta
    if (const S::Pic lo = S::Img("tac_locutor")) {
      const float alto = 330, ancho = alto * lo.w / std::max(1.0f, lo.h);
      S::DrawPic(dl, lo, q.At(0, 900 - alto), q.At(ancho, 900), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    }
    // bocadillo de ancho fijo y alto ajustado al texto (nunca se sale), con la cabecera arriba
    // R3 (medidas_v05/04): bocadillo 322-1328 (abajo en 888 en la pagina 2); pestana del locutor a su izquierda (225-456 x 734-765);
    // titulo en bx0+49; bolitas de 11 cada 25 desde bx0+367; A en bx0+609, B en bx1-188
    const float bx0 = 322, bx1 = 1328, by1 = g.by1;
    const float by0 = GloboV2(dl, q, bx0, by1, bx1 - bx0, Texto(("tut_texto_" + std::to_string(tut_)).c_str()), "", 38);
    PestanaV2(dl, q, 207, by0 - 4, T::T("Locutor del Torneo", "Tournament Announcer"));
    dl->AddTriangleFilled(q.At(bx0 + 2, by1 - 70), q.At(bx0 + 2, by1 - 40), q.At(bx0 - 34, by1 - 58), IM_COL32(250, 248, 236, 245));   // pico hacia el locutor
    Txt(dl, q, bx0 + 49, by0 + 27, kV2Titulo, Texto(("tut_titulo_" + std::to_string(tut_)).c_str()), 0, true, false, 310);
    const S::Pic bol = S::Img("tac_bolitas");   // bolitas 1-8 del Torneo (cada 60 px: 4,3-50,48)
    for (int i = 1; i <= 4; ++i) {
      const ImVec2 c = q.At(bx0 + 367 + 25.0f * float(i - 1), by0 + 27);
      const float r = 11 * q.s;
      if (bol) S::DrawPic(dl, bol, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec4(4 + 60.0f * float(i - 1), 3, 50 + 60.0f * float(i - 1), 48), i == tut_ ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 200));
      else dl->AddCircleFilled(c, r * 0.5f, i == tut_ ? IM_COL32(255, 160, 30, 255) : IM_COL32(90, 96, 120, 255));
    }
    // botones del juego, a la derecha de la cabecera: A Siguiente (Empezar en la ultima) y B Saltar
    const auto boton = [&](const char* bt, const char* t, float x) {
      S::Glyph(dl, bt, q.At(x, by0 + 26), 34 * q.s);
      ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
      const ImVec2 sz = fm->CalcTextSizeA(28 * q.s, 1e9f, 0, t), p = q.At(x + 40, by0 + 26);
      for (int k = 0; k < 8; ++k) {   // perfil claro fino: el texto oscuro va sobre el bocadillo claro
        const float an = 0.785f * float(k);
        dl->AddText(fm, 28 * q.s, ImVec2(p.x + std::cos(an) * 1.2f, p.y - sz.y * 0.5f + std::sin(an) * 1.2f), IM_COL32(255, 255, 255, 200), t);
      }
      dl->AddText(fm, 28 * q.s, ImVec2(p.x, p.y - sz.y * 0.5f), IM_COL32(28, 28, 32, 255), t);
    };
    boton("A", tut_ == 4 ? T::T("Empezar", "Start") : T::T("Siguiente", "Next"), bx0 + 609);
    boton("B", T::T("Saltar", "Skip"), bx1 - 188);
  }

  // 07 (V05): sin tablero (entrada.cpp, Tablero), logo grande con "Clasificacion final", lista con las bolitas del Torneo y caras en circulo, campeon
  // en el destello con su cara redonda, placas "Tu puesto" y "Tu equipo" y la ayuda con los botones del juego.
  void ClasificacionV2(ImDrawList* dl, const Lienzo& q) {
    const Partida& pa = *partida_;
    const ImU32 amarillo = IM_COL32(255, 220, 60, 255);
    const auto unidad_de = [&](int p) { int u = 0; for (size_t k = 0; k < eco_.unidades.size(); ++k) if (eco_.unidades[k].id == kEntrenador[p]) u = int(k); return u; };
    if (const S::Pic fo = S::Img("tac_torneo_fondo")) S::DrawPic(dl, fo, q.At(0, 0), q.At(1600, 900), ImVec4(0, 83, 900, 589), IM_COL32_WHITE);   // mismo recorte que MenuV2
    LogoV2(dl, q, 0.95f);
    // R3 (medidas_v05/07): subtitulo x 51 y 153; lista 55-849 x 189-834 con filas de 75 centradas en 246 + 75 k
    Txt(dl, q, 51, 153, 26, T::T("Clasificación final", "Final standings"), 0, false, false, 360);
    const std::vector<int> cl = pa.Clasificacion();
    const float fila = std::min(75.0f, 600.0f / float(std::max(1, pa.N()))), y0 = 208.5f;
    PanelV2(dl, q, 55, 189, 849, std::max(834.0f, y0 + fila * float(pa.N()) + 14), nullptr);
    const S::Pic bol = S::Img("tac_bolitas");   // bolitas 1-8 del Torneo (cada 60 px: 4,3-50,48)
    for (int i = 0; i < pa.N(); ++i) {
      const int p = cl[size_t(i)];
      const Jugador& j = pa.J(p);
      const float y = y0 + fila * float(i), yc = y + fila * 0.5f;
      if (i == 0) S::Highlight(dl, q.At(80, y + 4), q.At(828, y + fila - 4), float(ImGui::GetTime()));
      if (p == 0) dl->AddRect(q.At(78, y + 3), q.At(830, y + fila - 3), amarillo, 6 * q.s, 0, 3.0f * q.s);
      const ImVec2 bc = q.At(110, yc);   // bolita x 110 r 17,5; cara x 169 r 28; nombre x 223 (36); derecha acaba en 801 (24)
      const float br = 17.5f * q.s;
      if (bol && j.puesto >= 1 && j.puesto <= 8) S::DrawPic(dl, bol, ImVec2(bc.x - br, bc.y - br), ImVec2(bc.x + br, bc.y + br), ImVec4(4 + 60.0f * float(j.puesto - 1), 3, 50 + 60.0f * float(j.puesto - 1), 48));
      else Txt(dl, q, 110, yc, kV2Fila, "#" + std::to_string(j.puesto), 1, i == 0);
      const Modelo* m = ModeloDe(eco_.unidades[size_t(unidad_de(p))].id);
      CaraRedondaV2(dl, CaraV2(m->id), q.At(169, yc), 28 * q.s, IM_COL32(20, 24, 40, 255), j.vivo || i == 0 ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 255));
      Txt(dl, q, 223, yc, 36, p == 0 ? std::string(T::T("Tú", "You")) : std::string(T::T(m->es, m->en)), 0, i == 0, false, 260);
      Txt(dl, q, 801, yc, 24, j.vivo ? std::string(T::T("Campeón", "Champion")) : std::string(T::T("Eliminado en la ronda ", "Out in round ")) + std::to_string(j.ronda_baja), 2, i == 0, false, 330);
    }
    // campeon: rotulo dorado, destello con su cara en circulo y su nombre
    const int pc = cl.empty() ? 0 : cl[0];
    const Modelo* mc = ModeloDe(eco_.unidades[size_t(unidad_de(pc))].id);
    const float cx = 1240, cy = 372;
    OroV2(dl, q, cx, 212, 84, 440, T::T("¡Campeón!", "Champion!"));
    if (const S::Pic de = S::Img("tac_destello")) S::DrawPic(dl, de, q.At(cx - 250, cy - 150), q.At(cx + 250, cy + 150), ImVec4(0, 0, 0, 0), IM_COL32_WHITE);
    else
      for (int k = 0; k < 16; ++k) {   // estrella de 16 puntas como abanico de triangulos
        const ImVec2 c = q.At(cx, cy);
        const auto pt = [&](int t, float r) { const float a = 6.2831853f * float(t) / 32.0f; return ImVec2(c.x + std::cos(a) * r * q.s * 1.4f, c.y + std::sin(a) * r * q.s); };
        dl->AddTriangleFilled(c, pt(2 * k, 150), pt(2 * k + 1, 80), IM_COL32(255, 180, 40, 230));
        dl->AddTriangleFilled(c, pt(2 * k + 1, 80), pt(2 * k + 2, 150), IM_COL32(255, 180, 40, 230));
      }
    CaraRedondaV2(dl, CaraV2(mc->id), q.At(cx, cy), 66 * q.s, IM_COL32(20, 60, 120, 255));
    RotuloV2(dl, q, cx, 532, 54, 400, pc == 0 ? T::T("Tú", "You") : T::T(mc->es, mc->en), IM_COL32(255, 255, 255, 255));
    // placas: tu puesto y tu equipo (el "daño total" de la maqueta no existe en Partida: no se inventa)
    const Jugador& yo = pa.J(0);
    PlacaV2(dl, q, 1050, 588, 1525, 642, T::T("Tu puesto", "Your place"), std::to_string(yo.puesto) + ".º", true);
    // POST 12/11 (maqueta 07): "Tu equipo" sale siempre (tablero; si esta vacio al acabar, el banquillo) y "Daño total" debajo
    bool en_tablero = false;
    for (int s = kBanco; s < kSlots; ++s) en_tablero = en_tablero || yo.pz[s].hay();
    PlacaV2(dl, q, 1050, 652, 1525, 706, T::T("Tu equipo", "Your team"), "");
    PlacaV2(dl, q, 1050, 716, 1525, 770, T::T("Daño total", "Total damage"), std::to_string(dano_total_));
    float x = 1496;   // ronda 2: placas hasta 1525 como la maqueta 07
    // POST2: la partida jugada mostro la placa vacia (al caer se vacian tablero y banquillo): el equipo de la ultima ronda
    std::vector<int> eq;
    for (int s = en_tablero ? kSlots - 1 : kBanco - 1; s >= (en_tablero ? kBanco : 0); --s)
      if (yo.pz[s].hay()) eq.push_back(yo.pz[s].unidad);
    if (eq.empty()) eq.assign(ultimo_equipo_.rbegin(), ultimo_equipo_.rend());
    for (size_t k = 0; k < eq.size() && x > 1350; ++k, x -= 42)   // caras, de derecha a izquierda
      if (eq[k] >= 0 && size_t(eq[k]) < eco_.unidades.size())
        CaraRedondaV2(dl, CaraV2(eco_.unidades[size_t(eq[k])].id), q.At(x, 679), 19 * q.s, IM_COL32(20, 24, 40, 255));
    AyudaGlifosV2(dl, q, 1584, 862, {{"A", T::T("Nueva partida", "New game")}, {"B", T::T("Volver al HUB", "Back to the HUB")}});
  }
