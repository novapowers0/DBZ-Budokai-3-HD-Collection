// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): resultado del combate del Heroe (maqueta 07_resultado). Solo DIBUJO: la
// entrada (A/B cierran), los textos y los datos son los que ya calcula DibujarFin (rpg.cpp); la burbuja pasa a ser un
// panel con filas. Va a la DERECHA (la maqueta lo pone a la izquierda): abajo a la izquierda esta el anfitrion que ya
// pone el panel de modos (modos.cpp, S::Host, cara hasta x ~530 del lienzo) y el panel no lo tapa.
// Se incluye en rpg.cpp (espacio anonimo de dbz3::rpg, antes de DibujarFin). Kit: heroe_ui_v2.h. Lienzo 1600x900.

// Placa azul de una fila (la placa de DU 2020_09 no esta en la tabla nativa: primitiva con su color).
void PlacaV2(ImDrawList* dl, const S::Area& q, float x0, float y, float x1, float alto) {
  dl->AddRectFilled(q.At(x0, y - alto * 0.5f), q.At(x1, y + alto * 0.5f), IM_COL32(32, 84, 124, 235), 10 * q.s);
  dl->AddRect(q.At(x0, y - alto * 0.5f), q.At(x1, y + alto * 0.5f), IM_COL32(110, 170, 215, 200), 10 * q.s, 0, 2 * q.s);
}
void SeparadorV2(ImDrawList* dl, const S::Area& q, float x0, float y, float x1) {
  dl->AddLine(q.At(x0, y), q.At(x1, y), IM_COL32(230, 140, 30, 230), 2 * q.s);
}
// Cifras naranjas de ZENIE del juego (cifras_zenie, las de CajaZenie) desde x, centro y, alto h; detras, el rotulo ZENIE.
// Sin la pieza, el numero con la letra del menu.
void ZenieV2(ImDrawList* dl, const S::Area& q, float x, float y, float h, int zeni) {
  const std::string n = Num(std::max(zeni, 0));
  const S::Pic d = S::Img("cifras_zenie");
  if (!d) {
    v2::Texto(dl, q, x, y, v2::kLetraTitulo, n + " Zenie", 0, 2);
    return;
  }
  const float kk = h / 112.0f;
  for (const char c : n) {
    const float* g = kCifrasZenie[c - '0'];
    const float gw = (g[1] - g[0]) * kk;
    S::DrawPic(dl, d, q.At(x, y - h * 0.5f), q.At(x + gw, y + h * 0.5f), ImVec4(g[0], 10, g[1], 122));
    x += gw - 4 * kk;
  }
  if (const S::Pic z = S::Img("letras_zenie")) {
    const float zh = h * 0.55f, zw = zh * 150.0f / 49.0f;
    S::DrawPic(dl, z, q.At(x + 24, y - zh * 0.5f), q.At(x + 24 + zw, y + zh * 0.5f), ImVec4(2, 6, 152, 55));
  }
}

// HEROE-REC R4 (maqueta 13_coleccion, variante del aviso): texto de la etiqueta de una recompensa y chip NUEVO.
std::string EtiquetaPremioV2(const H::InfoPremio& p) {
  if (p.repetido) return T::T("Repetido", "Duplicate");
  if (p.tipo == "tecnica") return T::T("Técnica nueva", "New technique");
  if (p.tipo == "escenario") return T::T("Escenario nuevo", "New stage");
  if (p.tipo == "luchador") return T::T("Luchador nuevo", "New fighter");   // R23 HER-06
  return T::T("Título nuevo", "New title");
}
void ChipNuevoV2(ImDrawList* dl, const S::Area& q, float x1, float y) {
  const float w = 78, h = 24;
  dl->AddRectFilledMultiColor(q.At(x1 - w, y - h * 0.5f), q.At(x1, y + h * 0.5f), IM_COL32(255, 214, 80, 255), IM_COL32(255, 214, 80, 255),
                              IM_COL32(235, 140, 20, 255), IM_COL32(235, 140, 20, 255));
  dl->AddRect(q.At(x1 - w, y - h * 0.5f), q.At(x1, y + h * 0.5f), IM_COL32(120, 60, 10, 255), 8 * q.s, 0, 2 * q.s);
  v2::Texto(dl, q, x1 - w * 0.5f, y, v2::kLetraMin, T::T("NUEVO", "NEW"), 1, 0);
}
// Bloque "¡Recompensa!" dentro del panel: hasta 3 filas (cabecera real de la carta o placa + etiqueta + de donde viene + NUEVO o
// "+500 Zenie" si estaba repetido); si hay mas, "+N mas en Coleccion". Devuelve el y de abajo.
float BloquePremiosV2(ImDrawList* dl, const S::Area& q, const std::vector<H::InfoPremio>& prem, float fx0, float fx1, float y) {
  const float cx = (fx0 + fx1) * 0.5f;
  if (const S::Pic de = S::Img("tac_destello")) S::DrawPic(dl, de, q.At(cx - 90, y - 26), q.At(cx + 90, y + 26));
  v2::Texto(dl, q, cx, y, v2::kLetraFila, T::T("¡Recompensa!", "Reward!"), 1, 2);
  if (prem.size() > 3) v2::Texto(dl, q, fx1, y, v2::kLetraMin, std::string("+") + Num(int(prem.size()) - 3) + T::T(" más en Colección", " more in Collection"), 2, 0);
  const float rh = prem.size() >= 3 ? 40.0f : 50.0f, cw = std::min(250.0f, rh * 5.0f);
  y += 14 + rh * 0.5f;
  for (size_t i = 0; i < prem.size() && i < 3; ++i, y += rh + 6) {
    const H::InfoPremio& p = prem[i];
    PlacaV2(dl, q, fx0, y, fx1, rh);
    const float xl = fx0 + cw + 24, fl = rh > 50 ? v2::kLetraFila - 4 : v2::kLetraPeque;
    if (p.tipo == "tecnica") {
      if (!dbz3::heroe_ui::AvisoCabeceraTecnica(dl, q, p.capsula, fx0 + 4, y - rh * 0.5f + 2, cw - 6)) {   // sin la carta generada: cara y nombre
        if (p.cara >= 0) dbz3::heroe_ui::AvisoCara(dl, q, p.cara, fx0 + 34, y, 20);
        v2::Texto(dl, q, fx0 + 66, y, v2::kLetraPeque, p.nombre, 0, 0, false, false, cw - 80);
      }
    } else if (p.tipo == "titulo") {
      if (const S::Pic e = dbz3::heroe_ui::AvisoPiezaTitulo()) S::DrawPic(dl, e, q.At(fx0 + 10, y - rh * 0.45f), q.At(fx0 + 10 + rh * 0.9f, y + rh * 0.45f));
      v2::Texto(dl, q, fx0 + 22 + rh * 0.9f, y, v2::kLetraPeque, p.nombre, 0, 0, false, false, cw - rh);
    } else {
      v2::Texto(dl, q, fx0 + 14, y, v2::kLetraPeque, p.nombre, 0, 0, false, false, cw - 24);
    }
    const float zw = fx1 - xl - 100;   // texto en dos lineas: etiqueta arriba, de donde viene debajo
    v2::Texto(dl, q, xl, y - 12, fl, EtiquetaPremioV2(p), 0, 2, false, false, zw);
    if (p.repetido) {
      v2::Texto(dl, q, xl, y + 14, v2::kLetraMin + 2, std::string("-> +") + Num(500) + " Zenie", 0, 0, false, false, zw);
    } else {
      v2::Texto(dl, q, xl, y + 14, v2::kLetraMin + 2, p.condicion, 0, 0, false, false, zw);
      if (p.nuevo) ChipNuevoV2(dl, q, fx1 - 10, y);
    }
  }
  y -= rh * 0.5f - 6;   // bajo la ultima fila, con aire
  return y;
}

// POST 12/11: cierre de saga con el MISMO panel que el resultado (maqueta 07_resultado; el cierre no tiene maqueta propia):
// titulo dorado, nombre de la saga, placa de Nivel, el texto del cierre y "Proxima saga" al pie. `l` = lineas ya partidas
// por DibujarFin (l[0] = "Fin de la saga"); `pie` = lo que antes decia el bocadillo.
void CierreV2(ImDrawList* dl, const S::Area& a, const std::string& saga, const std::vector<std::string>& l, const std::string& pie) {
  const S::Area q = v2::Lienzo(a);
  const float x0 = 660, x1 = 1530, cx = (x0 + x1) * 0.5f, fx0 = x0 + 44, fx1 = x1 - 44;
  v2::Panel(dl, q, x0, 40, x1, 826);
  if (const S::Pic de = S::Img("tac_destello")) S::DrawPic(dl, de, q.At(cx - 160, 40), q.At(cx + 160, 168));   // POST 12/11: destello 2047_21 de la maqueta
  v2::Texto(dl, q, cx, 105, v2::kLetraTitulo, l.empty() ? std::string() : l[0], 1, 2, false, false, x1 - x0 - 80);
  v2::Texto(dl, q, cx, 170, v2::kLetraFila, saga, 1, 0, false, false, x1 - x0 - 80);
  PlacaV2(dl, q, fx0, 228, fx1, 54);
  v2::Texto(dl, q, fx0 + 26, 228, v2::kLetraFila, T::T("Nivel ", "Level "), 0, 0, false, false, 400);
  v2::Texto(dl, q, fx1 - 26, 228, v2::kLetraFila, Num(H::Nivel()), 2, 2);
  // POST2: mismas filas que la maqueta 07 (Nivel, Experiencia, Al siguiente nivel y barra), luego una seccion con cabecera
  // dorada como "Sube de nivel" (aqui: las Esferas del Heroe, la que da cada saga) y el texto del cierre; al pie, la saga siguiente.
  const H::Barras xb = H::Barras_();
  const bool maximo = xb.xp >= xb.xp_max;
  PlacaV2(dl, q, fx0, 290, fx1, 54);
  v2::Texto(dl, q, fx0 + 26, 290, v2::kLetraFila, Lang("Experiencia", "Experience", "Expérience", "Erfahrung", "Esperienza"), 0, 0);
  v2::Texto(dl, q, fx1 - 26, 290, v2::kLetraFila, maximo ? std::string(T::T("máximo", "max")) : Num(xb.xp) + " / " + Num(xb.xp_max), 2, 0);
  float y = 352;
  if (!maximo) {
    PlacaV2(dl, q, fx0, y, fx1, 54);
    v2::Texto(dl, q, fx0 + 26, y, v2::kLetraFila, T::T("Al siguiente nivel", "To next level"), 0, 0, false, false, 480);
    v2::Texto(dl, q, fx1 - 26, y, v2::kLetraFila, Num(xb.xp_max - xb.xp), 2, 0);
    y += 52;
  }
  v2::Progreso(dl, q, fx0 + 20, y, fx1 - 20, float(xb.xp) / float(std::max(xb.xp_max, 1)), -1, maximo, 16);
  y += 42;
  SeparadorV2(dl, q, x0 + 20, y, x1 - 20);
  y += 40;
  v2::Texto(dl, q, fx0, y, v2::kLetraFila, Lang("Esferas del Dragón", "Dragon Balls", "Dragon Balls", "Dragonballs", "Sfere del Drago"), 0, 2);
  const int esf = H::Esferas();
  const S::Pic bola = S::Img(v2::kPiezaEstrella);
  for (int i = 0; i < 7; ++i) {   // las 7, apagadas las que faltan
    const float bx = fx1 - 26 - 62.0f * float(6 - i);
    const ImU32 tinte = (esf >> i & 1) ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 160);
    if (bola) S::DrawPic(dl, bola, q.At(bx - 26, y - 26), q.At(bx + 26, y + 26), ImVec4(0, 0, 0, 0), tinte);
    else v2::Orbe(dl, q, bx, y, 19, (esf >> i & 1) ? IM_COL32(245, 150, 30, 255) : IM_COL32(90, 90, 90, 200));
  }
  y += 62;
  if (const std::vector<H::InfoPremio> prem = H::PremiosAviso(); !prem.empty()) {   // HEROE-REC: lo que dio el tramo, en una linea
    std::string r = Lang("¡Recompensa! ", "Reward! ", "Récompense ! ", "Belohnung! ", "Ricompensa! ") + EtiquetaPremioV2(prem[0]) + ": " + prem[0].nombre;
    if (prem.size() > 1) r += " (+" + Num(int(prem.size()) - 1) + ")";
    v2::Texto(dl, q, fx0, y, v2::kLetraFila - 2, r, 0, 2, false, false, fx1 - fx0);
    y += 46;
  }
  for (size_t i = 1; i < l.size() && y < 660; ++i, y += 44)
    v2::Texto(dl, q, fx0 + 20, y, v2::kLetraFila - 2, l[i], 0, 0, false, false, fx1 - fx0 - 40);
  // la saga siguiente justo debajo, como el Premio de la maqueta (antes al pie, con un hueco vacio en medio)
  const float ys = std::clamp(y + 6, 560.0f, 720.0f);
  SeparadorV2(dl, q, x0 + 20, ys, x1 - 20);
  v2::Texto(dl, q, cx, ys + 56, v2::kLetraFila, pie, 1, 0, false, false, x1 - x0 - 80);
  v2::BarraAyuda(dl, q, {{"A", Aceptar()}});
}

// `titulo`, `l` (l[1] = "Heroe X: n / total") y `b` (lo que hoy dice la burbuja) los arma DibujarFin como siempre.
void ResultadoV2(ImDrawList* dl, const S::Area& a, const M::Estado& st, const Run& r, const std::string& titulo,
                 const std::vector<std::string>& l, const std::string& b) {
  const S::Area q = v2::Lienzo(a);
  const float x0 = 660, x1 = 1530, cx = (x0 + x1) * 0.5f, fx0 = x0 + 44, fx1 = x1 - 44;
  v2::Panel(dl, q, x0, 40, x1, 826);   // hasta 826: debajo, la barra de ayuda (836..892)
  if (const S::Pic de = S::Img("tac_destello")) S::DrawPic(dl, de, q.At(cx - 160, 40), q.At(cx + 160, 168));   // POST 12/11: destello 2047_21 (pieza ya registrada para Tactics)
  v2::Texto(dl, q, cx, 105, v2::kLetraTitulo, titulo, 1, 2, false, false, x1 - x0 - 80);
  v2::Texto(dl, q, cx, 170, v2::kLetraFila, l.size() > 1 ? l[1] : std::string(), 1, 0, false, false, x1 - x0 - 80);
  if (st.tipo == M::kHeroe && g_fin_pierde && H::TecnicasV2On())
    v2::Texto(dl, q, cx, 203, v2::kLetraMin,
              T::T("Derrota de guion: no cuenta para el nivel de las técnicas.", "Story defeat: does not count toward technique levels."),
              1, 0, false, true, x1 - x0 - 80);
  float y = 228;
  const bool con_nivel = st.resultado == M::kVictoria || st.resultado == M::kDerrota;   // = cuando hoy sale LineaNivel
  const int sube = con_nivel ? H::Nivel() - H::NivelAntes() : 0;
  const std::vector<H::InfoPremio> prem = st.resultado == M::kVictoria ? H::PremiosAviso() : std::vector<H::InfoPremio>{};   // HEROE-REC R4 (vacio con el ajuste apagado)
  const bool con_prem = !prem.empty();
  if (con_nivel) {
    const H::Barras x = H::Barras_();
    const bool maximo = x.xp >= x.xp_max;
    PlacaV2(dl, q, fx0, y, fx1, 54);
    v2::Texto(dl, q, fx0 + 26, y, v2::kLetraFila, T::T("Nivel ", "Level "), 0, 0, false, false, 400);
    v2::Texto(dl, q, fx1 - 26, y, v2::kLetraFila, sube > 0 ? Num(H::NivelAntes()) + " > " + Num(H::Nivel()) : Num(H::Nivel()), 2, 2);
    y += 62;
    PlacaV2(dl, q, fx0, y, fx1, 54);
    v2::Texto(dl, q, fx0 + 26, y, v2::kLetraFila, "XP", 0, 0);
    v2::Texto(dl, q, fx1 - 26, y, v2::kLetraFila, maximo ? std::string(T::T("máximo", "max")) : Num(x.xp) + " / " + Num(x.xp_max), 2, 0);
    if (!maximo) {   // ENCAJA (T8 "goal"): lo que falta para el nivel siguiente
      y += 62;
      PlacaV2(dl, q, fx0, y, fx1, 54);
      v2::Texto(dl, q, fx0 + 26, y, v2::kLetraFila, T::T("Al siguiente nivel", "To next level"), 0, 0, false, false, 480);
      v2::Texto(dl, q, fx1 - 26, y, v2::kLetraFila, Num(x.xp_max - x.xp), 2, 0);
    }
    if (con_prem) {   // la maqueta del aviso quita la barra de XP y la linea de PH para dar sitio al bloque de recompensa
      y += 36;
    } else {
      y += 52;
      v2::Progreso(dl, q, fx0 + 20, y, fx1 - 20, float(x.xp) / float(std::max(x.xp_max, 1)), -1, maximo, 16);
      y += 36;
      v2::Texto(dl, q, cx, y, v2::kLetraMin, H::LineaNivel(0), 1, 0, false, true, x1 - x0 - 80);   // PH y prestigio, como hoy
      y += 32;
    }
  }
  if (sube > 0) {   // lo que da cada nivel (heroe.cpp: Vida, Reglas), con sus capsulas de la Skill Shop
    SeparadorV2(dl, q, x0 + 20, y, x1 - 20);
    y += con_prem ? 24 : 34;
    v2::Texto(dl, q, fx0, y, v2::kLetraFila, Lang("¡Sube de nivel! ", "Level up! ", "Niveau supérieur ! ", "Stufenaufstieg! ", "Livello su! "),
              0, 2);
    y += con_prem ? 32 : 46;
    const std::string vida = std::string(T::T("Vida ", "Health ")) + "+" + Num(100 * sube);
    const std::string dano = Lang("Daño ", "Damage ", "Dégâts ", "Schaden ", "Danni ") + "+" + Num(2 * sube) + "%";
    const S::Pic cap = S::Img("capsulas");
    for (int k = 0; k < 2; ++k) {
      const float bx = fx0 + 20 + 380 * float(k);
      if (cap) {   // verde (vida) y amarilla (dano): botellas 31x83 cada 36 px, como Tactics
        const float h = con_prem ? 40.0f : 56.0f, w = h * 31 / 83, sx = 3 + 36.0f * float(k + 1);
        S::DrawPic(dl, cap, q.At(bx - w * 0.5f, y - h * 0.5f), q.At(bx + w * 0.5f, y + h * 0.5f), ImVec4(sx, 7, sx + 31, 90));
      }
      v2::Texto(dl, q, bx + 40, y, v2::kLetraFila, k == 0 ? vida : dano, 0, 0, false, false, 310);
    }
    y += con_prem ? 24 : 34;
  }
  if (con_prem) {   // HEROE-REC R4: bloque "¡Recompensa!" bajo "¡Sube de nivel!"
    SeparadorV2(dl, q, x0 + 20, y, x1 - 20);
    y = BloquePremiosV2(dl, q, prem, fx0, fx1, y + 30);
    y -= 14;
  }
  // premio: hoy es una sola cifra por reto (PremioDe), sin bonus separados: una linea, sin desglose
  const bool sin_premio = st.tipo == M::kHeroe && g_fin_pierde;
  const int premio = st.resultado == M::kVictoria && !sin_premio ? PremioDe(st.tipo, r.extra) : 0;
  if (premio > 0) {
    SeparadorV2(dl, q, x0 + 20, y, x1 - 20);
    y += con_prem ? 32 : 42;
    v2::Texto(dl, q, fx0, y, v2::kLetraFila, Lang("Premio: ", "Prize: ", "Prix : ", "Preis: ", "Premio: "), 0, 2);
    ZenieV2(dl, q, fx0 + 230, y, con_prem ? 48 : 64, premio);
    y += con_prem ? 24 : 36;
  }
  // lo que hoy dice la burbuja (premio al monedero, proxima saga, monedero), linea a linea, al pie del panel
  std::vector<std::string> pie;
  for (size_t i = 0; i <= b.size();) {
    const size_t j = std::min(b.find('\n', i), b.size());
    pie.push_back(b.substr(i, j - i));
    i = j + 1;
  }
  if (con_prem && pie.size() > 1) pie.erase(pie.begin(), pie.end() - 1);   // con el bloque de recompensa solo queda "Monedero: ..."
  const float paso = 32, ypie = con_prem ? y + 14 : std::max(y + 8, 800 - paso * float(pie.size()));   // HEROE-V05: antes el ultimo renglon (Monedero) salia cortado por el borde del panel
  for (size_t i = 0; i < pie.size(); ++i)
    v2::Texto(dl, q, cx, ypie + paso * (float(i) + 0.5f), v2::kLetraPeque, pie[i], 1, 0, false, i + 1 == pie.size(), x1 - x0 - 80);
  v2::BarraAyuda(dl, q, {{"A", Aceptar()}});
}
