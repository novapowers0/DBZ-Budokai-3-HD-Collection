// TACTICS-V03: utilidades de dibujo 2D de Dragon Tactics (lienzo 1600x900, textos, cajas, estrellas, monedas, capsulas, colores) y textos
// de tactics_textos.toml. Sacadas tal cual de entrada.cpp (se incluye dentro de su espacio de nombres anonimo); no cambia nada de lo que se ve.
// Solo para incluir desde src/tactics/entrada.cpp.
// Lienzo de 1600x900 sobre la imagen del juego (16:9): la maqueta pone los paneles en los bordes de la imagen, no en el area 4:3 de los menus.
struct Lienzo {
  ImVec2 p0;
  float s = 1.0f;
  ImVec2 At(float x, float y) const { return ImVec2(p0.x + x * s, p0.y + y * s); }
};
Lienzo LienzoDe(const ImGuiIO& io) {
  const S::Imagen g = S::GuestImage(io);
  const ImVec2 sz = g.Size();
  Lienzo l;
  l.s = std::min(sz.x / 1600.0f, sz.y / 900.0f);
  l.p0 = ImVec2(g.p0.x + (sz.x - 1600.0f * l.s) * 0.5f, g.p0.y + (sz.y - 900.0f * l.s) * 0.5f);
  return l;
}
// Texto con la letra de B3 alineado a la izquierda (0), al centro (1) o a la derecha (2) de x; maxw = ancho maximo (se encoge).
constexpr float kMinTxt = 18.0f;   // H12: ningun texto por debajo de 18 (14,4 px a 720p); lo que no cabe se acorta con ".." en vez de encogerse mas
void Txt(ImDrawList* dl, const Lienzo& q, float x, float y, float size, const std::string& t0, int al, bool sel = false, bool dis = false, float maxw = 0) {
  size = std::max(size, kMinTxt);
  std::string t = t0;
  ImFont* f = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();   // la misma letra con la que se pinta (MenuTextB3)
  float w = f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x;
  if (maxw > 0 && w > maxw * q.s) {   // primero se encoge hasta el minimo; si aun no cabe, se recorta
    const float k = std::max(kMinTxt / size, maxw * q.s / w);
    size = std::max(kMinTxt, size * k);
    while (t.size() > 2 && f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x > maxw * q.s) {
      size_t n = t.size() - 1;
      while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;   // no partir un caracter UTF-8
      t.resize(n);
      if (f->CalcTextSizeA(size * q.s, 1e9f, 0, (t + "..").c_str()).x <= maxw * q.s) { t += ".."; break; }
    }
    w = f->CalcTextSizeA(size * q.s, 1e9f, 0, t.c_str()).x;
  }
  const ImVec2 p = q.At(x, y);
  S::MenuTextB3(dl, ImVec2(p.x + (al == 0 ? w * 0.5f : al == 1 ? 0.0f : -w * 0.5f), p.y), size * q.s, t.c_str(), sel, dis, 0);
}
void Caja(ImDrawList* dl, const Lienzo& q, float x0, float y0, float x1, float y1, ImU32 borde, ImU32 relleno = IM_COL32(10, 14, 30, 215), float grosor = 2.0f) {
  dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), relleno, 6 * q.s);
  dl->AddRect(q.At(x0, y0), q.At(x1, y1), borde, 6 * q.s, 0, grosor * q.s);
}
// H10: borde negro suave (dos capas translucidas) para que la estrella se vea sobre cualquier fondo, claro u oscuro
void Estrella(ImDrawList* dl, ImVec2 c, float r) {
  for (int capa = 0; capa < 3; ++capa) {
    const float k = capa == 0 ? 1.38f : capa == 1 ? 1.2f : 1.0f;
    const ImU32 col = capa == 0 ? IM_COL32(0, 0, 0, 80) : capa == 1 ? IM_COL32(0, 0, 0, 190) : IM_COL32(255, 205, 40, 255);
    ImVec2 p[10];
    for (int i = 0; i < 10; ++i) {
      const float a = -1.5708f + 0.62832f * float(i), rr = (i % 2 ? r * 0.45f : r) * k;
      p[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    for (int i = 0; i < 10; ++i) dl->AddTriangleFilled(c, p[i], p[(i + 1) % 10], col);
  }
}
// H10: texto plano (el que no usa la letra de B3) con el mismo borde negro suave; wrap > 0 = ancho de ajuste de linea
void TextoSuave(ImDrawList* dl, float fs, ImVec2 p, ImU32 col, const char* t, float wrap = 0, float alfa = 1.0f) {
  const float g = std::max(1.0f, fs * 0.07f);
  const ImU32 borde = IM_COL32(0, 0, 0, int(170.0f * alfa));
  const auto& fuentes = dbz3::launcher::ui::GetFonts();
  ImFont* f = std::strcmp(T::LangCode(), "ja") == 0 && fuentes.menu ? fuentes.menu : ImGui::GetFont();   // la letra del juego trae los kana y kanji; la de ImGui no
  static const float d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& o : d) dl->AddText(f, fs, ImVec2(p.x + o[0] * g, p.y + o[1] * g), borde, t, nullptr, wrap);
  dl->AddText(f, fs, p, col, t, nullptr, wrap);
}
void Estrellas(ImDrawList* dl, ImVec2 centro, float r, int n) {   // n estrellas en fila centrada en `centro`
  // H11: cada estrella es la bola de dragon de 1 estrella del propio juego (native_ui "icono"); sin la pieza, la estrella dibujada
  if (const S::Pic bola = S::Img("bola_estrella")) {
    const float d = r * 2.5f, paso = d * 0.92f;
    for (int i = 0; i < n; ++i) {
      const ImVec2 c(centro.x + (float(i) - float(n - 1) * 0.5f) * paso, centro.y);
      S::DrawPic(dl, bola, ImVec2(c.x - d * 0.5f, c.y - d * 0.5f), ImVec2(c.x + d * 0.5f, c.y + d * 0.5f));
    }
    return;
  }
  for (int i = 0; i < n; ++i) Estrella(dl, ImVec2(centro.x + (float(i) - float(n - 1) * 0.5f) * r * 2.1f, centro.y), r);
}
// H11: el oro es la pila de monedas de la caja de ZENIE de la Skill Shop (native_ui "caja_zenie", recorte 36,14-108,80); sin la pieza, el circulo
void Moneda(ImDrawList* dl, ImVec2 c, float r) {
  if (const S::Pic caja = S::Img("caja_zenie")) {
    const float h = r * 2.9f, w = h * 72.0f / 66.0f;
    S::DrawPic(dl, caja, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec4(36, 14, 108, 80));
    return;
  }
  dl->AddCircleFilled(c, r, IM_COL32(255, 200, 40, 255));
  dl->AddCircle(c, r, IM_COL32(150, 90, 10, 255), 0, r * 0.18f);
  dl->AddCircle(c, r * 0.6f, IM_COL32(200, 130, 20, 255), 0, r * 0.14f);
}

// Textos nuevos de H5 (tutorial y capsulas): mod center hd/tactics_textos.toml, siete idiomas; sin el fichero o sin el idioma sale el ingles.
std::string Texto(const char* clave) {
  static const std::map<std::string, std::map<std::string, std::string>> tabla = [] {
    std::map<std::string, std::map<std::string, std::string>> t;
    try {
      const toml::table doc = toml::parse_file(((dbz3::ModsRoot().parent_path() / "mod center hd") / "tactics_textos.toml").string());
      for (const auto& [k, v] : doc)
        if (const toml::table* e = v.as_table())
          for (const auto& [l, x] : *e)
            if (auto txt = x.value<std::string>()) t[std::string(k.str())][std::string(l.str())] = *txt;
    } catch (...) {
    }
    return t;
  }();
  const auto it = tabla.find(clave);
  if (it == tabla.end()) return clave;
  const auto l = it->second.find(T::LangCode());
  if (l != it->second.end()) return l->second;
  const auto en = it->second.find("en");
  return en != it->second.end() ? en->second : std::string(clave);
}
// "+25 % vida, +10 % ataque": generada de los mismos numeros de tactics_capsulas.toml, asi que nunca los contradice
std::string Describir(const CapsulaDef& c) {
  std::string r;
  const auto sumar = [&](int v, const char* clave, bool casillas) {
    if (!v) return;
    if (!r.empty()) r += ", ";
    r += (v > 0 ? "+" : "") + std::to_string(v) + (casillas ? " " : " % ") + Texto(clave);
  };
  sumar(c.vida, "ef_vida", false), sumar(c.ataque, "ef_ataque", false), sumar(c.defensa, "ef_defensa", false), sumar(c.velocidad, "ef_velocidad", false);
  sumar(c.ki_inicial, "ef_ki_inicial", false), sumar(c.ki_max, "ef_ki_max", false), sumar(c.alcance, "ef_alcance", true), sumar(c.tecnica, "ef_tecnica", false);
  return r;
}
ImU32 ColorCaps(int c) {
  static const ImU32 col[kMaxCaps] = {IM_COL32(230, 80, 80, 255),  IM_COL32(255, 150, 40, 255), IM_COL32(90, 170, 255, 255), IM_COL32(110, 230, 140, 255),
                                      IM_COL32(190, 120, 255, 255), IM_COL32(255, 230, 80, 255), IM_COL32(80, 230, 230, 255),  IM_COL32(255, 120, 190, 255),
                                      IM_COL32(220, 220, 220, 255), IM_COL32(160, 200, 80, 255), IM_COL32(200, 150, 100, 255), IM_COL32(120, 120, 255, 255)};
  return col[c % kMaxCaps];
}
// H11: botella de capsula de la Skill Shop del propio juego (native_ui "capsulas": roja, verde, amarilla y gris, 31x83 cada 36 px), centrada en `c`.
// Cada tipo de capsula usa el color que le toca; sin la pieza, el circulo de color de siempre.
void Botella(ImDrawList* dl, ImVec2 c, float alto, int tipo, bool apagada = false) {
  static constexpr int kBot[kMaxCaps] = {1, 0, 3, 2, 2, 0, 2, 1, 3, 0, 1, 2};   // vigor verde, fuerza roja, guardia gris, viento y ki amarillos...
  if (const S::Pic cap = S::Img("capsulas")) {
    const float bx = 3 + 36.0f * float(kBot[tipo % kMaxCaps]), w = alto * 31 / 83;
    S::DrawPic(dl, cap, ImVec2(c.x - w * 0.5f, c.y - alto * 0.5f), ImVec2(c.x + w * 0.5f, c.y + alto * 0.5f), ImVec4(bx, 7, bx + 31, 90),
               apagada ? IM_COL32(255, 255, 255, 80) : IM_COL32_WHITE);
    return;
  }
  dl->AddCircleFilled(c, alto * 0.25f, apagada ? IM_COL32(60, 64, 84, 255) : ColorCaps(tipo));
  dl->AddCircle(c, alto * 0.25f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
}
// H11: color del marco segun el rango (coste 1 a 4): gris azulado, verde, azul y dorado, como los rangos de cualquier autobattler
ImU32 ColorRango(int coste) {
  static const ImU32 col[4] = {IM_COL32(150, 165, 200, 255), IM_COL32(90, 205, 110, 255), IM_COL32(70, 170, 255, 255), IM_COL32(255, 190, 40, 255)};
  return col[std::clamp(coste, 1, 4) - 1];
}
