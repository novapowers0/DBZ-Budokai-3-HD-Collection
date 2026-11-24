// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): HUD de combate del Heroe (maqueta 06_hud_combate). Solo DIBUJO, con los
// mismos datos que la linea de hoy (rpg::Hud) y los del heroe que ya existen. Solo esquinas: arriba, por encima de las
// barras nativas, y abajo, por debajo de los pies; el centro queda libre y el HUD nativo no se toca.
// Se incluye en rpg.cpp (espacio anonimo de dbz3::rpg, antes de Hud). Kit: heroe_ui_v2.h. Medidas en px del lienzo
// 1600x900 sobre la imagen del juego (v2::LienzoImagen: el HUD nativo no se mueve con los "Bordes dorados").

// Cara redonda (retrato caras/<id> recortado en circulo) con aro claro; sin retrato, el circulo oscuro.
void CaraRedondaV2(ImDrawList* dl, const S::Area& q, int id, float cx, float cy, float r) {
  const ImVec2 c = q.At(cx, cy);
  dl->AddCircleFilled(c, (r + 3) * q.s, IM_COL32(8, 18, 34, 230), 40);
  if (const S::Pic p = S::Img(("caras/" + Num(id)).c_str())) {
    const float lado = std::min(p.w, p.h), u0 = (p.w - lado) * 0.5f / p.w, v0 = (p.h - lado) * 0.5f / p.h;
    dl->AddImageRounded(p.tex, ImVec2(c.x - r * q.s, c.y - r * q.s), ImVec2(c.x + r * q.s, c.y + r * q.s), ImVec2(u0, v0),
                        ImVec2(1 - u0, 1 - v0), IM_COL32_WHITE, r * q.s);
  }
  dl->AddCircle(c, r * q.s, IM_COL32(150, 245, 240, 220), 40, 2.5f * q.s);
}

void HudV2(ImDrawList* dl, const M::Estado& st, const Run& r) {
  const S::Area q = v2::LienzoImagen();
  // arriba a la izquierda (y 10..72, por encima de las barras nativas): el heroe y su nivel
  v2::Caja(dl, q, 10, 8, 208, 66);   // PM-32: acaba antes de la oreja del marcador nativo (x ~215)
  CaraRedondaV2(dl, q, r.extra >= 1000 ? CaraHeroe() : H::CuerpoDe(r.extra, r.jugador), 40, 37, 24);   // R23 CAC: su cara (ID propio)
  v2::Texto(dl, q, 74, 26, v2::kLetraPeque, H::NombreMostrado(), 0, 0, false, false, 130);
  v2::Texto(dl, q, 74, 52, v2::kLetraMin, std::string(T::T("Nivel ", "Level ")) + Num(H::Nivel()), 0, 2, false, false, 130);
  // arriba a la derecha: el rival y en que combate va (la misma palabra que el marcador del reto)
  v2::Caja(dl, q, 1392, 8, 1590, 66);   // PM-32: a la derecha de la oreja del marcador nativo (x ~1385)
  if (st.rival >= 0) {
    CaraRedondaV2(dl, q, st.rival, 1560, 37, 24);
    v2::Texto(dl, q, 1526, 26, v2::kLetraPeque, M::Nombre(st.rival), 2, 0, false, false, 120);
  }
  v2::Texto(dl, q, 1526, 52, v2::kLetraMin,
            std::string(Palabra(r.modo)) + " " + Num(st.ganados + 1) + " " + T::T("de", "of") + " " + Num(st.total), 2, 2, false,
            false, 150);
  // Este HUD pertenece a la partida de Empezar: FinRun suma el % al maestro del estilo.
  // DuVictoria (Dragon Universe) usa el maestro de la saga, no esta barra.
  const int mi = H::MaestroActual();
  if (mi >= 0) {
    const H::InfoMaestro m = H::Maestro(mi);
    v2::Caja(dl, q, 15, 860, 552, 896);   // PM-32: bajo el rotulo nativo "Mision N/M" (y 810-850)
    v2::Texto(dl, q, 35, 878, v2::kLetraMin,
              std::string(T::T("Entrena: ", "Trains: ")) + M::Nombre(m.id), 0, 0, false, false, 200);
    v2::Progreso(dl, q, 245, 878, 430, float(m.entreno) / 100.0f, float(H::EntrenoMinimo()) / 100.0f, m.entrenado, 14);
    v2::Texto(dl, q, 535, 878, v2::kLetraMin, Num(m.entreno) + "%", 2, 0, false, false, 90);
  }
  // abajo a la derecha: la linea de reglas de hoy (dano hecho/recibido, estilo y moveset)
  const M::Reglas g = ReglasDe(r);
  // POST2: como la maqueta ("Dano 110 % - Estilo Goku - Moveset Goku"): sin dos puntos, separador corto y caja mas ancha
  // hacia el centro solo si hace falta: antes "Moveset" salia cortado.
  std::string s;
  const char* sep = " - ";
  if (g.dano_hecho != 100) s += Lang("Daño ", "Damage ", "Dégâts ", "Schaden ", "Danni ") + Num(g.dano_hecho) + " %";
  if (g.dano_recibido != 100)
    s += std::string(s.empty() ? "" : sep) + Lang("Recibido ", "Taken ", "Subis ", "Erlitten ", "Subiti ") + Num(g.dano_recibido) + " %";
  const int mov = H::MovesetId() < 0 ? H::EstiloId() : H::MovesetId();
  s += std::string(s.empty() ? "" : sep) + T::T("Estilo ", "Style ") + M::Nombre(H::EstiloId()) + sep + "Moveset " + M::Nombre(mov);
  // caja al ancho del texto (maqueta: 848..1268 de 1280), como mucho hasta x 940 (el rotulo nativo de Mision pasa por aqui)
  const float bx0 = std::clamp(1585 - 40 - v2::Ancho(v2::kLetraMin * q.s, s) / q.s, 940.0f, 1060.0f);
  v2::Caja(dl, q, bx0, 860, 1585, 896);
  v2::Texto(dl, q, 1565, 878, v2::kLetraMin, s, 2, 0, false, false, 1565 - bx0 - 20);
}
