// TACTICS maqueta v2: InterfazV2 = Arena::Interfaz con la misma logica (reloj, zonas, indices, clics) y otra disposicion.
// Se incluye DENTRO de la clase Arena (entrada.cpp). Ayudas V2 en v2_piezas.inc; Carrusel/Tutorial/Clasificacion/Ficha V2 en otro archivo.
  void InterfazV2(ImDrawList* dl, const ImGuiIO& io, double ahora) {
    const Lienzo q = LienzoDe(io);
    const Partida& pa = *partida_;
    const Economia& eco = eco_;
    const Jugador& yo = pa.J(0);
    if ((pa.FaseActual() == Fase::Carrusel || carr_fin_t_ >= 0) && !fin_partida_) return PantallaCarruselV2(dl, io, ahora);
    const bool prep = pa.FaseActual() == Fase::Preparacion;
    if (prep && opc_.reloj > 0 && !fin_partida_ && !tut_) {   // reloj de preparacion opcional (igual que Interfaz)
      prep_resto_ -= std::min(io.DeltaTime, 0.25f);
      if (prep_resto_ <= 0) {
        REXLOG_INFO("dbz3 tactics: se acabo el tiempo de preparacion");
        IniciarCombate();
        return;
      }
    }
    const bool resultado = fin_t_ >= 0 && !prep;
    const ImU32 naranja = IM_COL32(255, 160, 30, 255), oscuro = IM_COL32(10, 14, 30, 215), amarillo = IM_COL32(255, 220, 60, 255);
    const ImU32 rojo = IM_COL32(230, 70, 60, 255), verde = IM_COL32(80, 220, 90, 255), cian = IM_COL32(90, 230, 255, 255);
    const auto es_en = [](const SinergiaDef& s) { return std::string(T::T(s.es.c_str(), s.en.c_str())); };
    const auto nombre = [&](int unidad) { const Modelo* m = ModeloDe(eco.unidades[size_t(unidad)].id); return std::string(T::T(m->es, m->en)); };
    const auto uidx = [&](const char* id) { for (size_t u = 0; u < eco.unidades.size(); ++u) if (eco.unidades[u].id == id) return int(u); return 0; };
    const auto entrenador = [&](int p) { return p == 0 ? std::string(T::T("Tú", "You")) : nombre(uidx(kEntrenador[p])); };
    const auto cara = [&](int unidad, ImVec2 a, ImVec2 b, ImU32 tinte = IM_COL32_WHITE) {
      const Modelo* m = ModeloDe(eco.unidades[size_t(unidad)].id);
      if (const S::Pic pic = S::Img(("caras/" + std::to_string(m->b3)).c_str())) S::DrawPic(dl, pic, a, b, ImVec4(0, 0, 0, 0), tinte);
      else dl->AddRectFilled(a, b, IM_COL32(40, 48, 80, 255));
    };
    // V05: cara en circulo (lienzo), como las de la maqueta
    const auto redonda = [&](int unidad, float cx, float cy, float r, ImU32 aro, ImU32 tinte = IM_COL32_WHITE) {
      const Modelo* m = ModeloDe(eco.unidades[size_t(unidad)].id);
      CaraRedondaV2(dl, S::Img(("caras/" + std::to_string(m->b3)).c_str()), q.At(cx, cy), r * q.s, aro, tinte);
    };
    // texto de color libre (rojo, verde...) en coordenadas del lienzo; al: 0 izquierda, 1 centro, 2 derecha; y = centro vertical
    const auto txtCol = [&](float x, float y, float fs, const std::string& t, ImU32 col, int al) { TxtColV2(dl, q, x, y, fs, t, col, al); };   // V05: letra del juego
    const auto iconoSin = [&](ImVec2 c, float r, const std::string& sid, bool activa) { IconoSinV2(dl, c, r, sid, activa); };
    const Duelo* dh = pa.DueloHumano() >= 0 ? &pa.Duelos()[size_t(pa.DueloHumano())] : nullptr;

    // 1. cabecera: bloque de fase arriba al centro (en combate, una pastilla mas baja bajo las barras nativas)
    const char* fase = pa.FaseActual() == Fase::Preparacion ? T::T("PREPARACIÓN", "PREPARATION") : pa.FaseActual() == Fase::Combate ? T::T("COMBATE", "COMBAT")
                       : pa.FaseActual() == Fase::Resultado ? T::T("RESULTADO", "RESULT") : T::T("FIN", "END");
    char tiempo[32];
    if (prep && opc_.reloj > 0) std::snprintf(tiempo, sizeof tiempo, "%d:%02d", int(prep_resto_ + 0.99f) / 60, int(prep_resto_ + 0.99f) % 60);
    else if (prep) std::snprintf(tiempo, sizeof tiempo, "%s", T::T("SIN LÍMITE", "NO LIMIT"));
    else { const int s = sim_ ? int(sim_->Tick() / sim::kHz) : 0; std::snprintf(tiempo, sizeof tiempo, "%02d:%02d", s / 60, s % 60); }
    const std::string ronda = std::string(T::T("Ronda ", "Round ")) + std::to_string(pa.Ronda());
    if (prep) {
      PanelV2(dl, q, 512, 6, 1088, 86, nullptr);
      Txt(dl, q, 530, 38, 28, ronda, 0, false, false, 130);
      Txt(dl, q, 800, 38, 40, fase, 1, true, false, 330);
      Txt(dl, q, 1072, 38, opc_.reloj > 0 ? kV2Num : kV2Pie, tiempo, 2, opc_.reloj > 0 && prep_resto_ < 10.0f);
      // barra fina de tiempo y unidades en el tablero
      const float f = opc_.reloj > 0 ? std::clamp(prep_resto_ / float(opc_.reloj), 0.0f, 1.0f) : 1.0f;
      dl->AddRectFilled(q.At(530, 66), q.At(990, 74), IM_COL32(0, 0, 0, 200), 3 * q.s);
      dl->AddRectFilled(q.At(530, 66), q.At(530 + 460 * f, 74), f < 0.2f && opc_.reloj > 0 ? rojo : IM_COL32(70, 190, 255, 255), 3 * q.s);
    } else if (!fin_partida_) {   // en la clasificacion final no hay cabecera (maqueta 07)
      // V05: pastilla oscura "Ronda N  COMBATE" bajo las barras del juego, sin marco ni reloj (el reloj es el del combate), como las maquetas 05/06
      // V04b: bajada a y 80-124 para no pisar las barras; R3 (medidas_v05/05-06): pastilla centrada en x 800, y 62-108, ajustada al texto ("Ronda 1" 26 blanco + fase 28 dorado, hueco 24, margen 28)
      ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
      const float wr = fm->CalcTextSizeA(26 * q.s, 1e9f, 0, ronda.c_str()).x / q.s, wf = fm->CalcTextSizeA(28 * q.s, 1e9f, 0, fase).x / q.s;
      const float px = 800 - (wr + 24 + wf) * 0.5f;
      dl->AddRectFilled(q.At(px - 28, 80), q.At(px + wr + 24 + wf + 28, 124), IM_COL32(16, 20, 30, 225), 23 * q.s);
      Txt(dl, q, px, 103, 26, ronda, 0, false, false, 140);
      Txt(dl, q, px + wr + 24, 103, 28, fase, 0, true, false, 260);
      if (sim_) MarcadorV2(dl, q, dh);   // V05: barras, caras y reloj como las maquetas 05/06 (el marcador nativo sigue oculto: ver Pedir())
    }
    // ficha fija del rival arriba a la derecha (preparacion): cara, nombre y cabezas de su equipo
    if (prep && dh && !fin_partida_) {
      Caja(dl, q, 1094, 10, 1590, 72, dh->copia ? naranja : rojo, oscuro, 2.5f);
      if (dh->b == kNeutral) {
        txtCol(1110, 41, kV2Pie, T::T("Rival", "Rival"), rojo, 0);
        Txt(dl, q, 1180, 41, kV2Pie, T::T("Ronda neutral: gana para conseguir oro y una cápsula", "Neutral round: win to earn gold and a capsule"), 0, true, false, 400);
      } else {
        txtCol(1108, 41, kV2Pie, dh->copia ? T::T("Copia", "Copy") : T::T("Rival", "Rival"), dh->copia ? naranja : rojo, 0);
        redonda(uidx(kEntrenador[dh->b]), 1206, 41, 26, dh->copia ? naranja : rojo);
        Txt(dl, q, 1240, 41, kV2Fila, entrenador(dh->b), 0, false, false, 120); // deja espacio a los cinco retratos
        const Jugador& jr = pa.J(dh->b);
        int n = 0;
        for (int sl = kBanco; sl < kSlots && n < 5; ++sl) {
          if (!jr.pz[sl].hay()) continue;
          redonda(jr.pz[sl].unidad, 1562 - 44.0f * float(n), 41, 20, IM_COL32(20, 24, 40, 255));
          ++n;
        }
      }
    }
    // transicion preparacion -> combate (igual que Interfaz)
    if (transicion_t0_ > 0 && ahora - transicion_t0_ < 1.2) {
      const float k = float(ahora - transicion_t0_);
      const int al = int(235 * std::clamp(1.0f - (k - 0.55f) / 0.65f, 0.0f, 1.0f));
      dl->AddRectFilled(ImVec2(0, q.At(0, 372).y), ImVec2(io.DisplaySize.x, q.At(0, 478).y), IM_COL32(0, 0, 0, al / 2));
      if (al > 40) OroV2(dl, q, 800, 425, 84, 700, T::T("¡COMBATE!", "COMBAT!"));   // V05: letra del logo (TitleText no lleva alfa: se quita al apagarse)
    }
    // arriba a la izquierda: titulo y salida
    if (!fin_partida_) LogoV2(dl, q);   // la clasificacion final lo pone mas grande
    if (prep) {   // V05: la maqueta no los tiene; quedan como dos pastillas pequenas entre el logo y la cabecera (mismas zonas de clic y mando)
      const bool el = Elegido(Zona::Botones, 3);
      Caja(dl, q, 384, 12, 490, 42, el ? amarillo : IM_COL32(120, 130, 170, 255), oscuro, el ? 3.0f : 1.5f);
      Txt(dl, q, 437, 27, 18, "HUB", 1, el, false, 96);
      Marcar(Zona::Botones, 3, q.At(384, 12), q.At(490, 42));
      Caja(dl, q, 384, 48, 490, 78, IM_COL32(120, 130, 170, 255), oscuro, 1.5f);
      Txt(dl, q, 437, 63, 18, Texto("tut_boton"), 1, false, false, 96);
      Marcar(Zona::Ayuda, 0, q.At(384, 48), q.At(490, 78));
    }
    if (ahora - salir_t_ < 3.0) Txt(dl, q, 800, 146, 24, T::T("Pulsa B otra vez para salir al HUB", "Press B again to go back to the HUB"), 1, true);

    // 2. izquierda: panel Sinergias (no en el resultado de ronda)
    {
      const std::vector<SinActiva> sin = pa.Sinergias(0);
      nsin_ = int(sin.size());
      if (zc_ == Zona::Sin && ic_ >= nsin_) zc_ = Zona::Tablero, ic_ = 0;
      if (!resultado && (prep || !sin.empty())) {   // V05: en preparacion el panel sale siempre (vacio, con una linea que dice como llenarlo)
        float y = PanelV2(dl, q, 10, 106, 310, 106 + 58 + 52.0f * float(std::max<size_t>(1, sin.size())) + 14, T::T("Sinergias", "Synergies"));
        if (sin.empty()) Txt(dl, q, 160, y + 23, kV2Pie, T::T("Pon unidades en el tablero", "Put units on the board"), 1, false, true, 270);
        int idx_sin = -1;
        for (const SinActiva& s : sin) {
          ++idx_sin;
          const SinergiaDef& d = eco.sinergias[size_t(s.sinergia)];
          int sig = d.umbrales.back().n;
          for (const Umbral& u : d.umbrales) if (u.n > s.cuenta) { sig = u.n; break; }
          const bool activa = s.umbral >= 0, el = Elegido(Zona::Sin, idx_sin);
          if (el) Caja(dl, q, 18, y, 302, y + 46, amarillo, IM_COL32(255, 255, 255, 22), 3.0f);
          Marcar(Zona::Sin, idx_sin, q.At(18, y), q.At(302, y + 46));
          iconoSin(q.At(44, y + 23), 21 * q.s, d.id, activa);   // POST 12/11: maqueta 05, caras de 42 px
          Txt(dl, q, 84, y + 23, 28, es_en(d), 0, false, !activa, 160);   // R3: x 84, 28
          Txt(dl, q, 296, y + 23, kV2Fila, std::to_string(s.cuenta) + "/" + std::to_string(sig), 2, activa, !activa);
          y += 52;
        }
      }
    }
    if (zc_ == Zona::Sin && ic_ >= 0 && ic_ < nsin_) {   // ficha de la sinergia enfocada, a la derecha del panel
      const SinActiva sa = pa.Sinergias(0)[size_t(ic_)];
      const SinergiaDef& d = eco.sinergias[size_t(sa.sinergia)];
      std::string quien;
      for (const UnidadEco& u : eco.unidades) if (u.sin[0] == sa.sinergia || u.sin[1] == sa.sinergia) quien += (quien.empty() ? "" : ", ") + nombre(int(&u - eco.unidades.data()));
      const float h = 92 + 30.0f * float(d.umbrales.size()) + 44;
      PanelV2(dl, q, 320, 106, 766, 106 + h, nullptr, 235);
      Txt(dl, q, 543, 136, 26, es_en(d) + "  (" + std::to_string(sa.cuenta) + ")", 1, true, false, 400);
      for (size_t k = 0; k < d.umbrales.size(); ++k) {
        const Umbral& u = d.umbrales[k];
        std::string t = std::to_string(u.n) + T::T(" miembros: ", " members: ");
        const auto suma = [&](int v, const char* es, const char* en) { if (v) t += std::string(t.back() == ' ' ? "" : "  ") + T::T(es, en) + std::to_string(v) + "%"; };
        suma(u.vida, "Vida +", "HP +"), suma(u.ataque, "ataque +", "attack +"), suma(u.defensa, "defensa +", "defense +");
        const bool act = int(k) <= sa.umbral;
        Txt(dl, q, 342, 178 + 30.0f * float(k), 20, t, 0, act, !act, 410);
      }
      TextoV2(dl, 18 * q.s, q.At(342, 178 + 30.0f * float(d.umbrales.size()) + 8), IM_COL32(220, 230, 255, 255), (std::string(T::T("Unidades: ", "Units: ")) + quien).c_str(), 404 * q.s);
    }

    // 7. ficha de unidad (o descripcion de capsula): se decide antes porque la ficha sustituye a la lista de jugadores
    const int ncap = int(eco.capsulas.size());
    int fu = -1, fest = 1, fc0 = -1, fc1 = -1, fvende = -1;
    if (prep && zc_ != Zona::Caps) {
      const auto de = [&](const Pieza& z) { fu = z.unidad, fest = z.estrellas, fc0 = z.caps[0], fc1 = z.caps[1], fvende = eco.unidades[size_t(z.unidad)].coste * pa.Peso(z.estrellas); };
      if (zc_ == Zona::Tienda && yo.tienda[ic_] >= 0) fu = yo.tienda[ic_];
      else if (zc_ == Zona::Banco && yo.pz[ic_].hay()) de(yo.pz[ic_]);
      else if (zc_ == Zona::Tablero && yo.pz[kBanco + ic_].hay()) de(yo.pz[kBanco + ic_]);
      else if (mano_ >= 0 && yo.pz[mano_].hay()) de(yo.pz[mano_]);
    }
    const bool con_ficha = fu >= 0 && ficha_ver_ && !tut_ && espia_ < 0;   // inspeccionar un rival muestra la lista aunque estuviera abierta la ficha

    // 2. derecha: panel Jugadores (tu en amarillo, rival de la ronda en rojo); en el resultado, -N / V sobre la cabeza
    std::vector<int> orden(static_cast<size_t>(pa.N()));
    for (int p = 0; p < pa.N(); ++p) orden[size_t(p)] = p;
    std::stable_sort(orden.begin(), orden.end(), [&](int x, int y) {
      const Jugador &jx = pa.J(x), &jy = pa.J(y);
      return jx.vivo != jy.vivo ? jx.vivo : jx.vivo ? jx.vida > jy.vida : jx.puesto < jy.puesto;
    });
    float jug_y0 = 0;
    bool ronda_sin_dano = resultado;   // OTR-04: empate (nadie recibio dano): «=» gris en vez de la «V» verde de quien no perdio vida
    if (resultado)
      for (int p = 0; p < pa.N(); ++p) ronda_sin_dano = ronda_sin_dano && !(pa.J(p).vivo && pa.DanoRonda(p) > 0);
    if (!con_ficha && !fin_partida_) {   // en la clasificacion final la lista ya es la tabla
      jug_y0 = PanelV2(dl, q, 1290, 106, 1588, 106 + 58 + 52.0f * float(pa.N()) + 14, T::T("Jugadores", "Players"));
      for (int k = 0; k < pa.N(); ++k) {
        const int p = orden[size_t(k)];
        const Jugador& jp = pa.J(p);
        const float y = jug_y0 + 52.0f * float(k);
        const bool rival = dh && p == dh->b;
        if (p == 0 || rival) Caja(dl, q, 1298, y, 1580, y + 46, p == 0 ? amarillo : dh->copia ? naranja : rojo, IM_COL32(0, 0, 0, 0), 2.5f);
        dl->AddCircleFilled(q.At(1314, y + 23), 10 * q.s, IM_COL32(255, 160, 30, 255), 16);
        dl->AddCircle(q.At(1314, y + 23), 10 * q.s, IM_COL32(60, 30, 0, 255), 16, 1.5f * q.s);
        txtCol(1314, y + 23, kV2Pie - 2, std::to_string(k + 1), IM_COL32(255, 255, 255, 255), 1);
        redonda(uidx(kEntrenador[p]), 1352, y + 23, 19, IM_COL32(20, 24, 40, 255), jp.vivo ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
        Txt(dl, q, 1394, y + 23, 28, entrenador(p), 0, false, !jp.vivo, 124);   // V05: como la maqueta, sin barra de vida (la vida es la cifra)
        Txt(dl, q, 1574, y + 23, kV2Num, jp.vivo ? std::to_string(jp.vida) : "#" + std::to_string(jp.puesto), 2, jp.vivo, !jp.vivo);
        if (resultado && jp.vivo) {   // 6. resultado de la ronda sobre la cabeza
          const int dn = pa.DanoRonda(p);
          // POST 12/11 (maqueta 06): insignia redonda sobre la cara, siempre visible (antes una cifra suelta de 18)
          dl->AddCircleFilled(q.At(1368, y + 36), 12 * q.s, dn > 0 ? IM_COL32(170, 30, 30, 240) : ronda_sin_dano ? IM_COL32(90, 100, 120, 240) : IM_COL32(30, 130, 50, 240), 20);
          dl->AddCircle(q.At(1368, y + 36), 12 * q.s, IM_COL32(255, 255, 255, 220), 20, 1.5f * q.s);
          txtCol(1368, y + 36, 18, dn > 0 ? "-" + std::to_string(dn) : std::string(ronda_sin_dano ? "=" : "V"), IM_COL32_WHITE, 1);
        }
      }
    }

    // capsulas: el panel con el inventario sale encima del bloque de nivel (el boton que lo abre esta dentro de ese bloque)
    if (prep) {
      const bool abierta = caps_abiertas_ || zc_ == Zona::Caps;
      if (abierta) {
        const int filas = (ncap + kColsCaps - 1) / kColsCaps;
        const float y1 = 752, y0 = y1 - (46 + 50.0f * float(filas) + 50);
        PanelV2(dl, q, 8, y0, 424, y1, nullptr, 235);
        Txt(dl, q, 216, y0 + 26, 22, Texto("caps_titulo"), 1, true);
        for (int c = 0; c < ncap; ++c) {
          const float x0 = 16 + 133.0f * float(c % kColsCaps), cy0 = y0 + 46 + 50.0f * float(c / kColsCaps);
          const bool el = Elegido(Zona::Caps, c), tengo = yo.inv[c] > 0;
          Caja(dl, q, x0, cy0, x0 + 129, cy0 + 46, el ? amarillo : capmano_ == c ? cian : tengo ? ColorCaps(c) : IM_COL32(70, 76, 100, 255), IM_COL32(18, 28, 62, 235),
               el || capmano_ == c ? 4.0f : 1.5f);
          Botella(dl, q.At(x0 + 15, cy0 + 23), 36 * q.s, c, !tengo);
          Txt(dl, q, x0 + 32, cy0 + 14, 18, std::string(T::T(eco.capsulas[size_t(c)].es.c_str(), eco.capsulas[size_t(c)].en.c_str())), 0, el, !tengo, 92);
          Txt(dl, q, x0 + 32, cy0 + 33, 18, "x" + std::to_string(yo.inv[c]), 0, tengo, !tengo);
          Marcar(Zona::Caps, c, q.At(x0, cy0), q.At(x0 + 129, cy0 + 46));
        }
        const float by = y0 + 46 + 50.0f * float(filas);
        const bool el = Elegido(Zona::Caps, ncap), puede = yo.oro >= eco.precio_capsula;
        Caja(dl, q, 16, by, 416, by + 36, el ? amarillo : naranja, IM_COL32(20, 34, 70, 230), el ? 4.0f : 2.0f);
        Txt(dl, q, 28, by + 18, 20, Texto("caps_comprar"), 0, el, !puede, 270);
        Moneda(dl, q.At(396, by + 18), 9 * q.s);
        Txt(dl, q, 384, by + 18, 20, std::to_string(eco.precio_capsula), 2, false, !puede);
        Marcar(Zona::Caps, ncap, q.At(16, by), q.At(416, by + 36));
      }
    }
    if (prep && zc_ == Zona::Caps) {   // descripcion de la capsula bajo el cursor
      PanelV2(dl, q, 320, 340, 736, 462, nullptr, 235);
      if (ic_ < ncap) {
        const CapsulaDef& c = eco.capsulas[size_t(ic_)];
        Botella(dl, q.At(346, 372), 44 * q.s, ic_);
        Txt(dl, q, 374, 372, 24, std::string(T::T(c.es.c_str(), c.en.c_str())), 0, true, false, 250);
        Txt(dl, q, 726, 372, 20, Texto("caps_tienes") + " " + std::to_string(yo.inv[ic_]), 2, true);
        TextoV2(dl, 20 * q.s, q.At(336, 400), IM_COL32(220, 230, 255, 255), Describir(c).c_str(), 384 * q.s);
      } else {
        TextoV2(dl, 20 * q.s, q.At(336, 360), IM_COL32(220, 230, 255, 255), (Texto("caps_sin_oro") + " " + std::to_string(eco.precio_capsula)).c_str(), 384 * q.s);
      }
    } else if (con_ficha) FichaV2(dl, q, fu, fest, fc0, fc1, fvende);

    // 4. banquillo bajo el tablero (en preparacion, con miniaturas 3D; en combate solo retratos y sin zonas de clic)
    // V05: como la maqueta, retratos en circulo con su numero y sus estrellas (sin las miniaturas 3D de V04); el carrusel usa el mismo
    if (prep || !resultado) BanquilloV2(dl, q, prep);
    minis_ = 0;
    PintarDestellos(ImGui::GetForegroundDrawList(), dl, q, ahora);

    // 4. abajo a la izquierda: nivel, EXP, oro, capsulas y probabilidades por coste (en combate, una pastilla con nivel y oro)
    if (prep && !tut_) {   // V04b: en el tutorial el locutor tapa este panel (se oculta); V05: en combate no hay pastilla de nivel y oro (la maqueta 05 no la tiene)
      PanelV2(dl, q, 10, 759, 300, 894, nullptr);
      const bool max = yo.nivel >= eco.NivelMax();
      Txt(dl, q, 28, 790, 28, std::string(T::T("Nivel ", "Level ")) + std::to_string(yo.nivel), 0, true, false, 110);   // R3 (medidas_v05/03): x 28, y 785-798
      const int need = max ? 1 : eco.exp_subir[size_t(yo.nivel - 1)];
      // R3: barra de EXP 143-232 x 783-797 redondeada con perfil; 0/4 a la derecha en 285
      dl->AddRectFilled(q.At(143, 783), q.At(232, 797), IM_COL32(8, 22, 30, 230), 7 * q.s);
      dl->AddRectFilled(q.At(143, 783), q.At(143 + 89 * (max ? 1.0f : float(yo.exp) / float(need)), 797), IM_COL32(70, 170, 255, 255), 7 * q.s);
      dl->AddRect(q.At(143, 783), q.At(232, 797), IM_COL32(60, 90, 100, 255), 7 * q.s, 0, 2.0f * q.s);
      Txt(dl, q, 285, 790, kV2Fila, max ? "MAX" : std::to_string(yo.exp) + "/" + std::to_string(need), 2);
      ZenisV2(dl, q, 54, 822, 56);   // maqueta: maletin gris 28-80 x 806-838
      OroV2(dl, q, 118, 822, 64, 80, std::to_string(yo.oro));   // maqueta: "8" dorado 102-128 x 800-840
      // boton de capsulas (misma zona Ayuda 1 que el original); sin caja salvo abierto o con una capsula en la mano
      int total = 0;
      for (int c = 0; c < ncap; ++c) total += yo.inv[c];
      const bool abierta = caps_abiertas_ || zc_ == Zona::Caps;
      if (abierta || capmano_ >= 0) Caja(dl, q, 184, 804, 292, 844, abierta ? amarillo : cian, IM_COL32(20, 34, 70, 230), 3.0f);
      Botella(dl, q.At(204, 822), 36 * q.s, capmano_ >= 0 ? capmano_ : 0);   // R3: botella 196-212 x 804-836; "x0" acaba en 262
      Txt(dl, q, 262, 822, kV2Fila, "x" + std::to_string(total), 2, total > 0, total == 0);
      Marcar(Zona::Ayuda, 1, q.At(184, 804), q.At(292, 844));
      if (yo.nivel <= eco.NivelMax()) {   // probabilidades de coste 1/2/3/4, cada una del color de su rango
        const auto& w = eco.peso_coste[size_t(yo.nivel - 1)];
        int tot = 0;
        for (int v : w) tot += v;
        static constexpr float kPx[4] = {30, 118, 180, 242};   // R3: columnas de la maqueta (100% 0% 0% 0%), y 869, letra 22
        int k = 0;
        for (int v : w) {
          txtCol(kPx[std::min(k, 3)], 869, 22, std::to_string(tot ? 100 * v / tot : 0) + "%", ColorRango(k + 1), 0);
          ++k;
        }
      }
    }

    // 3. tienda compacta: 5 cartas bajas con la pieza nativa de carta segun coste
    const bool ver_tienda = prep && (tut_ == 0 || tut_ == 1);   // V04b: el bocadillo del tutorial (paginas 2-4) pisa la tienda; solo la pagina 1 la senala
    if (ver_tienda) PanelV2(dl, q, 310, 759, 1268, 894, nullptr);
    for (int c = 0; c < eco.tienda && ver_tienda; ++c) {
      const float x0 = 322 + 188.0f * float(c), y0 = 768, x1 = x0 + 180, y1 = 884;
      const int u = yo.tienda[c];
      const bool el = Elegido(Zona::Tienda, c);
      if (u < 0) {
        Caja(dl, q, x0, y0, x1, y1, IM_COL32(70, 76, 100, 255), IM_COL32(8, 10, 24, 160), 1.5f);
        Marcar(Zona::Tienda, c, q.At(x0, y0), q.At(x1, y1));
        continue;
      }
      const UnidadEco& ue = eco.unidades[size_t(u)];
      const bool puede = yo.oro >= ue.coste;
      int tengo = 0;
      for (const Pieza& z : yo.pz) tengo += z.unidad == u ? pa.Peso(z.estrellas) : 0;
      const int sube = tengo + 1 == 27 ? 4 : tengo + 1 == 9 ? 3 : tengo + 1 == 3 ? 2 : 0;
      CartaV2(dl, q, x0, y0, u, puede, el);   // V05: carta de la maqueta (v2_carrusel.inc)
      // borde: foco amarillo > tienda bloqueada cian > 2 copias naranja brillante (late) > rango
      if (el) Caja(dl, q, x0, y0, x1, y1, amarillo, IM_COL32(255, 255, 255, 22), 4.0f);
      else if (yo.bloq) Caja(dl, q, x0, y0, x1, y1, cian, IM_COL32(0, 0, 0, 0), 3.0f);
      else if (sube) {
        const int pulso = 190 + int(65.0f * std::sin(float(ahora) * 6.0f));
        Caja(dl, q, x0 - 2, y0 - 2, x1 + 2, y1 + 2, IM_COL32(255, 140, 0, pulso), IM_COL32(0, 0, 0, 0), 4.5f);
      }
      if (sube)   // dos bolas de estrella arriba a la derecha
        for (int b = 0; b < 2; ++b) {
          const ImVec2 bc = q.At(x1 - 12 - 22.0f * float(b), y0 - 2);
          dl->AddCircleFilled(bc, 10 * q.s, IM_COL32(255, 150, 20, 255), 20);
          dl->AddCircle(bc, 10 * q.s, IM_COL32(120, 50, 0, 255), 20, 1.5f);
          Estrella(dl, bc, 5 * q.s);
        }
      Marcar(Zona::Tienda, c, q.At(x0, y0), q.At(x1, y1));
    }
    // arrastrando una unidad con el raton: su cara sigue al cursor y la tienda se ofrece como "soltar para vender"
    const bool arrastrando = kArrastrar && prep && arr_ >= 0 && ImGui::IsMouseDown(0) && yo.pz[arr_].hay() && std::abs(io.MousePos.x - arr_p_.x) + std::abs(io.MousePos.y - arr_p_.y) > 14.0f;
    if (arrastrando) {
      const float r = 34 * q.s;
      cara(yo.pz[arr_].unidad, ImVec2(io.MousePos.x - r, io.MousePos.y - r * 0.8f), ImVec2(io.MousePos.x + r, io.MousePos.y + r * 0.8f));
      dl->AddRect(ImVec2(io.MousePos.x - r, io.MousePos.y - r * 0.8f), ImVec2(io.MousePos.x + r, io.MousePos.y + r * 0.8f), cian, 4 * q.s, 0, 3.0f);
      dl->AddRectFilled(q.At(310, 759), q.At(1268, 894), IM_COL32(120, 20, 20, 150), 6 * q.s);
      Txt(dl, q, 789, 826, 26, Texto("soltar_vender"), 1, true, false, 900);
    }
    // vista previa de sinergias al mover una unidad cogida (igual que Interfaz)
    std::string previa;
    if (kPrevia && prep && mano_ >= 0 && yo.pz[mano_].hay() && (zc_ == Zona::Tablero || zc_ == Zona::Banco) && !arrastrando) {
      std::array<int, kSlots> antes{}, desp{};
      for (int sl = 0; sl < kSlots; ++sl) antes[size_t(sl)] = desp[size_t(sl)] = yo.pz[sl].hay() ? yo.pz[sl].unidad : -1;
      std::swap(desp[size_t(mano_)], desp[size_t(zc_ == Zona::Banco ? ic_ : kBanco + ic_)]);
      const auto contar = [&](const std::array<int, kSlots>& v, int k) {
        std::set<int> m;
        for (int sl = kBanco; sl < kSlots; ++sl)
          if (v[size_t(sl)] >= 0 && (eco.unidades[size_t(v[size_t(sl)])].sin[0] == k || eco.unidades[size_t(v[size_t(sl)])].sin[1] == k)) m.insert(v[size_t(sl)]);
        return int(m.size());
      };
      for (size_t k = 0; k < eco.sinergias.size(); ++k) {
        const int a0 = contar(antes, int(k)), b0 = contar(desp, int(k));
        if (a0 == b0) continue;
        bool tramo = false;
        for (const Umbral& um : eco.sinergias[k].umbrales) tramo |= um.n == b0;
        previa += (previa.empty() ? "" : "   ") + es_en(eco.sinergias[k]) + " " + std::to_string(a0) + " > " + std::to_string(b0) + (tramo ? "*" : "");
      }
    }
    if (EnPrueba()) { static std::string previa_ant; if (previa != previa_ant) { previa_ant = previa; REXLOG_INFO("dbz3 tactics: previa [{}] arrastrando {}", previa, arrastrando); } }

    // 5. UN aviso abajo a la izquierda fuera del tablero; la ayuda de mandos, en la barra de abajo a la derecha (combate)
    if (!fin_partida_ && !resultado) {
      std::string aviso;
      bool fuerte = false;
      if (ahora - aviso_t_ < aviso_dura_ && prep) aviso = aviso_, fuerte = true;
      else if (!previa.empty()) aviso = std::string(Texto("previa_sin")) + " " + previa, fuerte = true;
      else if (prep && (capmano_ >= 0 || zc_ == Zona::Caps)) aviso = Texto("caps_ayuda");
      else if (prep && zc_ == Zona::Botones && ic_ == 0 && yo.nivel <= eco.NivelMax()) {
        const auto& w = eco.peso_coste[size_t(yo.nivel - 1)];
        int suma = 0;
        for (int v : w) suma += v;
        aviso = std::string(T::T("Probabilidades de la tienda (coste 1/2/3/4): ", "Shop odds (cost 1/2/3/4): "));
        for (size_t k = 0; k < w.size(); ++k) aviso += (k ? " / " : "") + std::to_string(suma ? w[k] * 100 / suma : 0);
        aviso += " %";
      } else if (prep && zc_ == Zona::Botones && ic_ == 4)
        aviso = T::T("Bloquear: la tienda se conserva en la ronda siguiente hasta que la actualices (RB)", "Lock: keeps the shop for the next round until you refresh it (RB)");
      // V05: sin aviso no sale cuadro (la maqueta no lo tiene); la racha, si la hay, va en una linea corta
      else if (prep && std::abs(yo.racha) >= 2)
        aviso = std::string(yo.racha > 0 ? T::T("Racha ", "Win streak ") : T::T("Derrotas ", "Loss streak ")) + std::to_string(std::abs(yo.racha)) + (pa.OroRacha(yo.racha) ? " (+" + std::to_string(pa.OroRacha(yo.racha)) + ")" : "");
      if (prep) {
        if (aviso.empty()) {
        } else if (fuerte && ahora - aviso_t_ < aviso_dura_ && aviso_dura_ > 5.0) {   // aviso largo (partida guardada vieja): lo dice el locutor, como en la maqueta 09
          GloboV2(dl, q, 330, 640, 940, aviso, T::T("Locutor del Torneo", "Tournament Announcer"));
        } else {
          ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
          const float ah = std::max(44.0f, fm->CalcTextSizeA(kV2Pie * q.s, 1e9f, 270 * q.s, aviso.c_str()).y / q.s + 18);   // el cuadro crece hacia arriba con el texto
          Caja(dl, q, 10, 748 - ah, 300, 748, fuerte ? amarillo : naranja, IM_COL32(10, 14, 30, 200), 1.5f);
          TextoV2(dl, kV2Pie * q.s, q.At(20, 756 - ah), fuerte ? amarillo : IM_COL32(220, 230, 255, 255), aviso.c_str(), 270 * q.s);
        }
      } else {
        // V05 (maqueta 05): sin otro aviso, la sinergia a la que le falta una unidad, con su emblema, en una pastilla abajo a la izquierda
        const SinergiaDef* cerca = nullptr;
        if (aviso.empty()) {
          int mc = 0, mn = 0;
          for (const SinActiva& sa : pa.Sinergias(0))
            for (const Umbral& um : eco.sinergias[size_t(sa.sinergia)].umbrales)
              if (um.n == sa.cuenta + 1 && sa.cuenta > mc) cerca = &eco.sinergias[size_t(sa.sinergia)], mc = sa.cuenta, mn = um.n;
          if (cerca) aviso = Texto("por_que_sin") + " " + es_en(*cerca) + " " + std::to_string(mc) + "/" + std::to_string(mn);
        }
        if (!aviso.empty()) {
          dl->AddRectFilled(q.At(24, 830), q.At(640, 890), IM_COL32(8, 12, 28, 215), 14 * q.s);
          if (cerca) iconoSin(q.At(56, 860), 22 * q.s, cerca->id, true);
          Txt(dl, q, cerca ? 88 : 40, 860, 26, aviso, 0, true, false, cerca ? 540 : 590);
        }
        AyudaGlifosV2(dl, q, 1584, 861, {{"Y", std::string(T::T("Velocidad", "Speed")) + " x" + std::to_string(vel_)}, {"B", T::T("Salir", "Exit")}});
      }
    }
    // 4. Listo a la derecha del banquillo; Subir nivel / Cambiar / Bloquear abajo a la derecha
    if (prep) {
      const float x0 = 1170, y0 = 660, x1 = 1440, y1 = 743;   // R3: panel de la maqueta 1170-1439 x 654-749
      const bool el = Elegido(Zona::Botones, 2);
      // V05: panel oscuro con RT y "¡Listo!" en la letra dorada del titulo (RT = ¡Listo! con v2)
      PanelV2(dl, q, x0, y0 - 6, x1, y1 + 6, nullptr);
      if (el) dl->AddRect(q.At(x0 + 4, y0 - 2), q.At(x1 - 4, y1 + 2), amarillo, 6 * q.s, 0, 4.0f * q.s);
      S::Glyph(dl, "RT", q.At(x0 + 30, (y0 + y1) * 0.5f), 40 * q.s);   // R3: RT en 1206-1240, "¡Listo!" centrado en 1316 (maqueta 1185-1422)
      OroV2(dl, q, (x0 + x1) * 0.5f + 22, (y0 + y1) * 0.5f, 56, 200, T::T("¡Listo!", "Ready!"));
      Marcar(Zona::Botones, 2, q.At(x0, y0), q.At(x1, y1));
      PanelV2(dl, q, 1276, 759, 1590, 894, nullptr);
      const bool max = yo.nivel >= eco.NivelMax();
      const struct { int zona; const char* tecla; const char* es; const char* en; int precio; } btn[3] = {
          {0, "Y", "Subir nivel", "Level up", eco.precio_exp}, {1, "X", "Cambiar", "Reroll", eco.precio_actualizar}, {4, "RB", "Bloquear", "Lock", -1}};
      for (int b = 0; b < 3; ++b) {
        const float ry0 = 770 + 39.0f * float(b), ry1 = ry0 + 34;
        const bool elb = Elegido(Zona::Botones, btn[b].zona);
        const bool bloq = btn[b].zona == 4 && yo.bloq;
        const bool puede = btn[b].precio < 0 || (yo.oro >= btn[b].precio && (btn[b].zona == 1 || !max));
        // V05: filas sin caja como la maqueta (solo el foco o el bloqueo la marcan); icono de mando solo donde el mando lo hace de verdad (RB)
        if (elb || bloq) Caja(dl, q, 1284, ry0, 1582, ry1, elb ? amarillo : cian, bloq ? IM_COL32(20, 70, 90, 200) : IM_COL32(255, 255, 255, 22), elb ? 3.0f : 1.5f);
        const float cy = (ry0 + ry1) * 0.5f;
        // R3 (medidas_v05/03): boton 1290-1322 (44), texto en 1344 a 26, maletin de zenis en 1530 y precio dorado acabando en 1562
        S::Glyph(dl, btn[b].tecla, q.At(1290, cy), 44 * q.s);   // V05: Y/X/RB hacen esto de verdad con v2 (entrada.cpp, Entrada)
        const char* et = bloq ? T::T("Bloqueada", "Locked") : T::T(btn[b].es, btn[b].en);
        Txt(dl, q, 1344, cy, 26, et, 0, elb || bloq, !puede, 160);
        if (btn[b].precio >= 0) {
          ZenisV2(dl, q, 1530, cy, 30);
          txtCol(1562, cy, 28, std::to_string(btn[b].precio), puede ? IM_COL32(255, 205, 60, 255) : IM_COL32(150, 150, 160, 255), 2);
        }
        Marcar(Zona::Botones, btn[b].zona, q.At(1284, ry0), q.At(1582, ry1));
      }
    }
    // 6. resultado de la ronda: panel abajo al centro con Derrota en rojo / ¡Victoria! en dorado
    if (resultado && fin_partida_) ClasificacionV2(dl, q);   // sin el panel de la ronda debajo
    if (resultado && !fin_partida_) {
      const int g = pa.GanadorRonda();
      const bool copia = dh && dh->copia;
      const bool neutral = dh && dh->b == kNeutral;
      std::string t;
      if (neutral) t = g == 0 ? std::string(T::T("Ronda neutral superada: +", "Neutral round cleared: +")) + std::to_string(eco.neutral_oro) + T::T(" de oro y una cápsula", " gold and a capsule") : std::string(T::T("Ronda neutral perdida: nadie pierde vida", "Neutral round lost: nobody loses health"));
      else if (g == 0) t = copia ? std::string(T::T("Has vencido a la copia: nadie pierde vida", "You beat the copy: nobody loses health")) : std::string(T::T("El rival pierde ", "The rival loses ")) + std::to_string(pa.DanoRival()) + T::T(" de vida", " HP");
      else if (g == 1) t = std::string(T::T("Pierdes ", "You lose ")) + std::to_string(pa.DanoRonda(0)) + T::T(" de vida, te quedan ", " HP, ") + std::to_string(pa.J(0).vida) + T::T("", " left");
      else t = std::string(T::T("Los dos pierden ", "Both lose ")) + std::to_string(pa.DanoRonda(0)) + T::T(" de vida", " HP");
      if (!pa.J(0).vivo) t = std::string(T::T("Has sido eliminado: puesto ", "You are out: place ")) + std::to_string(pa.J(0).puesto) + " / " + std::to_string(pa.N());
      // R3 (medidas_v05/06): panel central 522-1081 x 654-889 (mas alto que los de dano), titulo centrado en y 710 con 1,4x de alto,
      // raya en 772, frase en 810 con las cifras grandes de color ("Pierdes [6] de vida, te quedan [74]") y "A Continuar" en 855
      PanelV2(dl, q, 522, 654, 1081, 889, nullptr);
      const bool gana = g == 0 && pa.J(0).vivo;
      const char* titulo = gana ? T::T("¡Victoria!", "Victory!") : g == 1 || !pa.J(0).vivo ? T::T("Derrota", "Defeat") : T::T("Empate", "Draw");
      if (gana) OroV2(dl, q, 800, 712, 92, 500, titulo);
      else RotuloV2(dl, q, 800, 710, 92, 500, titulo, g == 1 || !pa.J(0).vivo ? IM_COL32(235, 60, 50, 255) : IM_COL32(225, 232, 255, 255));
      dl->AddLine(q.At(544, 772), q.At(1060, 772), naranja, 2 * q.s);
      if (g == 1 && pa.J(0).vivo && !neutral) {   // frase en trozos: blanco 28, cifras 36 (perdida en rojo, vida que queda en dorado)
        ImFont* fm = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
        const struct { std::string t; float fs; ImU32 col; } tr[4] = {
            {T::T("Pierdes ", "You lose "), 28, IM_COL32(240, 244, 255, 255)}, {std::to_string(pa.DanoRonda(0)), 38, IM_COL32(255, 110, 110, 255)},
            {T::T(" de vida, te quedan ", " HP, "), 28, IM_COL32(240, 244, 255, 255)}, {std::to_string(pa.J(0).vida) + T::T("", " left"), 38, IM_COL32(255, 205, 60, 255)}};
        float tw = 0;
        for (const auto& s : tr) tw += fm->CalcTextSizeA(s.fs * q.s, 1e9f, 0, s.t.c_str()).x / q.s;
        float x = 800 - tw * 0.5f;
        for (const auto& s : tr) { txtCol(x, 810, s.fs, s.t, s.col, 0); x += fm->CalcTextSizeA(s.fs * q.s, 1e9f, 0, s.t.c_str()).x / q.s; }
      } else {
        Txt(dl, q, 800, 810, 28, t, 1, false, false, 520);
      }
      {
        const float gw = S::Glyph(nullptr, "A", ImVec2(), 40 * q.s) / q.s;
        S::Glyph(dl, "A", q.At(1048 - 128 - gw, 855), 40 * q.s);   // boton 879-910, "Continuar" acaba en 1048
        Txt(dl, q, 1048, 855, 26, T::T("Continuar", "Continue"), 2, false, false, 124);
      }
    }
    // 6. paneles de dano tras la ronda: total grande arriba y una barra por unidad
    if (resultado && sim_ && !fin_partida_) {
      std::vector<int> hecho(pa.Combatientes().size(), 0);
      for (const sim::Evento& e : sim_->Eventos())
        if (e.tipo == sim::Tipo::Impacto && e.unidad >= 0 && size_t(e.unidad) < hecho.size()) hecho[size_t(e.unidad)] += e.valor;
      int tope = 1;
      for (int v : hecho) tope = std::max(tope, v);
      for (int lado = 0; lado < 2; ++lado) {
        const float x0 = lado ? 1100.0f : 31.0f, x1 = x0 + 469;
        std::vector<size_t> ord;
        for (size_t i = 0; i < hecho.size(); ++i) if (pa.Combatientes()[i].jugador == lado) ord.push_back(i);
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return hecho[a] > hecho[b]; });
        int total = 0;
        for (size_t i : ord) total += hecho[i];
        PanelV2(dl, q, x0, 676, x1, 894, nullptr);
        // R3 (medidas_v05/06): titulo en x0+33 y 712, total a la derecha en x1-34; raya en 737; filas centradas en 765 + 47,5 k;
        // cara r 17,5 en x0+34, barra x0+69..x0+344 de 12 de alto, cifra a la derecha en x1-32
        Txt(dl, q, x0 + 33, 712, 28, lado ? T::T("Daño del rival", "Rival damage") : T::T("Tu daño", "Your damage"), 0, true, false, 260);
        txtCol(x1 - 34, 712, kV2Num, std::to_string(total), lado ? IM_COL32(255, 120, 100, 255) : IM_COL32(255, 210, 60, 255), 2);
        dl->AddLine(q.At(x0 + 18, 737), q.At(x1 - 18, 737), naranja, 2 * q.s);
        if (ord.empty()) Txt(dl, q, (x0 + x1) * 0.5f, 812, kV2Pie, T::T("Sin unidades en el tablero", "No units on the board"), 1, false, true, 420);
        const float paso = ord.size() <= 3 ? 47.5f : 142.0f / float(std::min<size_t>(ord.size(), 5));
        for (size_t k = 0; k < ord.size() && k < 5; ++k) {
          const Combatiente& c = pa.Combatientes()[ord[k]];
          const float cy = 741.25f + paso * (float(k) + 0.5f);
          const bool viva = sim_->Unidades()[ord[k]].viva();
          redonda(c.unidad, x0 + 34, cy, paso >= 44 ? 17.5f : 13.0f, IM_COL32(20, 24, 40, 255), viva ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255));
          dl->AddRectFilled(q.At(x0 + 69, cy - 7), q.At(x0 + 344, cy + 7), IM_COL32(8, 18, 26, 230), 7 * q.s);
          dl->AddRectFilled(q.At(x0 + 69, cy - 6), q.At(x0 + 69 + 275.0f * float(hecho[ord[k]]) / float(tope), cy + 6), lado ? IM_COL32(230, 90, 70, 255) : IM_COL32(80, 190, 230, 255), 6 * q.s);
          Txt(dl, q, x1 - 32, cy, kV2Fila, std::to_string(hecho[ord[k]]), 2);
        }
      }
    }
    // espiar a un rival (raton): su tablero al pasar por su fila de la lista de jugadores (solo si la lista esta a la vista)
    if (prep && !tut_ && !con_ficha) {
      for (int k = 0; k < pa.N(); ++k) {
        const float y = jug_y0 + 52.0f * float(k);
        const ImVec2 m((io.MousePos.x - q.p0.x) / q.s, (io.MousePos.y - q.p0.y) / q.s);
        if (espia_ != k && (m.x < 1290 || m.x > 1588 || m.y < y || m.y > y + 46)) continue;
        if (espia_ >= 0 && espia_ != k) continue;
        const int p = orden[size_t(k)];
        const Jugador& jp = pa.J(p);
        if (EnPrueba() && espiado_ != p) {
          espiado_ = p;
          REXLOG_INFO("dbz3 tactics: espiando al jugador {}", p);
        }
        const float ey = std::min(y, 690.0f);
        Caja(dl, q, 1030, ey, 1282, ey + 200, amarillo, IM_COL32(10, 14, 30, 238), 2.0f);
        Txt(dl, q, 1040, ey + 16, 20, entrenador(p) + "  " + T::T("Nivel ", "Lvl ") + std::to_string(jp.nivel) + "  " + std::to_string(jp.oro) + "g", 0, true, false, 232);
        int n = 0;
        for (int sl = kBanco; sl < kSlots; ++sl) {
          if (!jp.pz[sl].hay()) continue;
          const float cx = 1040 + 40.0f * float(n % 6), cy = ey + 34 + 50.0f * float(n / 6);
          cara(jp.pz[sl].unidad, q.At(cx, cy), q.At(cx + 36, cy + 30));
          Estrellas(dl, q.At(cx + 18, cy + 40), 4.5f * q.s, jp.pz[sl].estrellas);
          ++n;
        }
        if (hist_[p]) Txt(dl, q, 1040, ey + 186, 18, std::string(T::T("Último duelo: ", "Last duel: ")) + (hist_[p] == 1 ? T::T("ganaste", "you won") : hist_[p] == 2 ? T::T("perdiste", "you lost") : T::T("empate", "draw")), 0, false, true, 232);
        if (espia_ == k) Txt(dl, q, 1156, ey + 166, 18, T::T("Start: siguiente jugador", "Start: next player"), 1, false, true, 240);
        if (!n) Txt(dl, q, 1156, ey + 100, 18, T::T("Sin unidades en el tablero", "No units on the board"), 1, false, true, 230);
        const std::vector<SinActiva> sn = pa.Sinergias(p);
        std::string st;
        for (const SinActiva& sa : sn) if (sa.umbral >= 0) st += (st.empty() ? "" : ", ") + es_en(eco.sinergias[size_t(sa.sinergia)]) + " " + std::to_string(sa.cuenta);
        if (!st.empty() && espia_ != k && !hist_[p]) Txt(dl, q, 1040, ey + 186, 18, st, 0, false, false, 236);
        break;
      }
    }
    if (tut_ > 0) TutorialV2(dl, q, io);
  }

  // V05 (maquetas 05/06): marcador de combate propio con piezas nativas de B3 (tac_barra_azul/roja, caras redondas, cifras_hud):
  // vida total de cada bando de la simulacion (se vacia hacia el centro) y segundos que quedan del combate (tope sim::kTopeTicks).
  void MarcadorV2(ImDrawList* dl, const Lienzo& q, const Duelo* dh) {
    int vida[2] = {0, 0}, tope[2] = {0, 0}, primera[2] = {-1, -1}, vivas[2] = {0, 0}, total[2] = {0, 0};
    const auto uidx = [&](const std::string& id) { for (size_t k = 0; k < eco_.unidades.size(); ++k) if (eco_.unidades[k].id == id) return int(k); return -1; };
    for (const sim::Unidad& u : sim_->Unidades()) {
      const int e = u.equipo ? 1 : 0;
      vida[e] += std::max(0, u.vida), tope[e] += std::max(1, u.def.vida);
      ++total[e], vivas[e] += u.vida > 0 ? 1 : 0;
      if (primera[e] < 0) primera[e] = uidx(u.def.id);
    }
    // cara de cada lado: el entrenador (tu y tu rival); en la ronda neutral, la primera unidad del rival
    const int cara[2] = {uidx(kEntrenador[0]), dh && dh->b >= 0 && dh->b < kMaxJug ? uidx(kEntrenador[dh->b]) : primera[1]};
    // R3 (medidas_v05/05): disposicion del marcador de B3 de la maqueta, medida en lienzo (lado izquierdo; el derecho es su espejo en x = 800):
    // remate blanco/rojo 440-471 x 14-76; panel azul 469-765 x 24-72; vida verde 483-679 x 33-45 (se vacia hacia el centro);
    // fila amarilla debajo 483-640 x 52-66 = unidades vivas (una raya por unidad, oscura si cayo); cara 696-760 junto al reloj.
    // ponytail: dibujado; el marcador nativo de B3 no esta registrado como pieza (native_ui.cpp), pedirlo si debe ser identico.
    const auto X = [](int e, float x) { return e ? 1600 - x : x; };
    for (int e = 0; e < 2; ++e) {
      const float f = std::clamp(float(vida[e]) / float(std::max(1, tope[e])), 0.0f, 1.0f);
      const float pa0 = std::min(X(e, 469), X(e, 765)), pa1 = std::max(X(e, 469), X(e, 765));
      if (const S::Pic pl = S::Img("tac_hud_placa")) {   // POST 12/11: placa nativa del marcador de B3 (data_usi 0, tex 30); el lado derecho, en espejo
        S::DrawPic(dl, pl, q.At(pa0, 20), q.At(pa1, 76), ImVec4(4, 1, 361, 68), IM_COL32_WHITE, e == 1);
      } else {
        dl->AddRectFilledMultiColor(q.At(pa0, 24), q.At(pa1, 72), IM_COL32(70, 120, 235, 235), IM_COL32(70, 120, 235, 235), IM_COL32(18, 36, 130, 235), IM_COL32(18, 36, 130, 235));
        dl->AddRect(q.At(pa0, 24), q.At(pa1, 72), IM_COL32(190, 215, 255, 255), 8 * q.s, 0, 2.5f * q.s);
      }
      // remate del extremo
      const float r0 = std::min(X(e, 440), X(e, 471)), r1 = std::max(X(e, 440), X(e, 471));
      // ronda 2 (maqueta 05): ala blanca inclinada hacia fuera con la franja roja dentro, no una pastilla recta
      const float sk = e ? -10.0f : 10.0f;   // inclinacion: arriba hacia fuera
      dl->AddQuadFilled(q.At(r0 - sk, 14), q.At(r1 - sk, 14), q.At(r1 + sk, 76), q.At(r0 + sk, 76), IM_COL32(235, 235, 240, 255));
      dl->AddQuadFilled(q.At(r0 + 8 - sk * 0.6f, 26), q.At(r1 - 8 - sk * 0.6f, 26), q.At(r1 - 8 + sk * 0.6f, 64), q.At(r0 + 8 + sk * 0.6f, 64), IM_COL32(200, 40, 40, 255));
      dl->AddQuad(q.At(r0 - sk, 14), q.At(r1 - sk, 14), q.At(r1 + sk, 76), q.At(r0 + sk, 76), IM_COL32(40, 40, 50, 255), 2 * q.s);
      // vida (verde) anclada al extremo de fuera
      const float v0 = std::min(X(e, 483), X(e, 679)), v1 = std::max(X(e, 483), X(e, 679)), w = (v1 - v0) * f;
      dl->AddRectFilled(q.At(v0, 33), q.At(v1, 45), IM_COL32(10, 20, 40, 255), 3 * q.s);
      if (w > 0.5f) dl->AddRectFilled(q.At(e ? v1 - w : v0, 33), q.At(e ? v1 : v0 + w, 45), IM_COL32(60, 225, 60, 255), 3 * q.s);
      // unidades vivas en rayas amarillas
      const int n = std::max(1, total[e]);
      const float paso = std::min(30.0f, 157.0f / float(n));
      for (int k = 0; k < total[e]; ++k) {
        const float a = 483 + paso * float(k), b = a + paso - 6;
        const float s0 = std::min(X(e, a), X(e, b)), s1 = std::max(X(e, a), X(e, b));
        // ronda 2 (maqueta 05): rayas inclinadas como la barra de ki de B3
        const float d = e ? -4.0f : 4.0f;
        dl->AddQuadFilled(q.At(s0 + d, 52), q.At(s1 + d, 52), q.At(s1 - d, 66), q.At(s0 - d, 66), k < vivas[e] ? IM_COL32(250, 225, 40, 255) : IM_COL32(16, 26, 60, 255));
      }
      if (cara[e] >= 0) {
        const Modelo* m = ModeloDe(eco_.unidades[size_t(cara[e])].id);
        // POST 12/11 (maqueta 05): el busto del marcador de B3 (cara del HUD, data_cmn #AZT 256x128, dibujo 3,0-181,120) dentro de la
        // placa junto al reloj, en espejo a la derecha como en el combate; sin la pieza, la cara redonda de antes
        const S::Pic busto = S::Img(("caras/" + std::to_string(m->b3)).c_str());
        if (busto && busto.w >= 2 * busto.h) {
          const float b0 = std::min(X(e, 676), X(e, 768)), b1 = std::max(X(e, 676), X(e, 768));
          dl->PushClipRect(q.At(b0, 20), q.At(b1, 76), true);
          S::DrawPic(dl, busto, q.At(b0, 16), q.At(b1, 78), ImVec4(3, 0, 181, 120), IM_COL32_WHITE, e == 1);
          dl->PopClipRect();
        } else {
          CaraRedondaV2(dl, busto, q.At(X(e, 728), 48), 28 * q.s, IM_COL32(190, 215, 255, 255));
        }
      }
    }
    // reloj: circulo oscuro con las cifras del marcador del juego (cifras_hud, data_usi 0); sin ellas, la letra del juego
    const int s = std::max(0, (sim::kTopeTicks - int(sim_->Tick()) + sim::kHz - 1) / sim::kHz);
    const ImVec2 c = q.At(800, 42);
    dl->AddCircleFilled(c, 30 * q.s, IM_COL32(10, 22, 44, 240), 32);
    dl->AddCircle(c, 30 * q.s, IM_COL32(230, 236, 255, 255), 32, 3 * q.s);
    char t[8];
    std::snprintf(t, sizeof t, "%02d", std::min(s, 99));
    if (const S::Pic hud = S::Img("cifras_hud")) {
      static constexpr float kX[10][2] = {{3, 30}, {46, 62}, {76, 104}, {112, 139}, {148, 175}, {2, 29}, {40, 67}, {77, 102}, {112, 140}, {148, 175}};
      const float h = 30 * q.s;
      float wt = 0;
      for (const char* k = t; *k; ++k) wt += (kX[*k - '0'][1] - kX[*k - '0'][0]) * h / 30 + q.s;
      float x = c.x - wt * 0.5f;
      for (const char* k = t; *k; ++k) {
        const int d = *k - '0';
        const float yy = d < 5 ? 4.0f : 40.0f, w = (kX[d][1] - kX[d][0]) * h / 30;
        S::DrawPic(dl, hud, ImVec2(x, c.y - h * 0.5f), ImVec2(x + w, c.y + h * 0.5f), ImVec4(kX[d][0], yy, kX[d][1], yy + 30));
        x += w + q.s;
      }
    } else {
      Txt(dl, q, 800, 42, kV2Num, t, 1, false, false, 56);
    }
  }
