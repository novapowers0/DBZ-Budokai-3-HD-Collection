// dbz3 - Modo Heroe, Habitacion (maqueta_2_habitacion de E:\DBZ3HD_trabajo\heroe_rediseno; ajuste dbz3_heroe_habitacion, APAGADO
// de serie). La casa del heroe (ref. 23 de UT): carta del heroe a la izquierda, lista de 5 entradas a la derecha (Aspecto,
// Evolucion, Maestros, Tecnicas, Ficha) con su valor, una linea de descripcion, radar con las Esferas.
// Solo dibuja y mueve su cursor: lo que hace cada entrada lo decide rpg.cpp (abre Crear, Evolucion, Maestros, Movesets o la ficha
// y vuelve aqui). Cambiar el traje o el maestro con izquierda/derecha se hace aqui y avisa con Hab::kCambio (rpg.cpp guarda).
// Se incluye en heroe_ui.cpp junto a los otros heroe_v2_*.inl (usa FiguraV2, ControlesVistaV2, RadarV2, PlacaDuV2).
// Medidas en px del lienzo 1600x900 (= maqueta 1920x1080 x 0,8333).
namespace {

int g_hab_cur = 0;                // fila elegida
constexpr int kHabFilas = 6;      // Aspecto, Evolucion, Maestros, Tecnicas, Ficha y (con dbz3_heroe_recompensas) Coleccion
int NumFilasHab() { return H::RecompensasActivas() ? kHabFilas : kHabFilas - 1; }   // HEROE-REC: la 6.a fila solo con el ajuste
std::string g_hab_aviso;
int g_hab_receta = -1;

// Tabla propia de siete idiomas: no depende de anadir claves al launcher.
const char* TextoCasaHabitacion(bool nombre) {
  struct Texto { const char *idioma, *casa, *capsule, *kame; };
  static const Texto textos[] = {
      {"es", "Casa", "Capsule Corp.", "Kame House"},
      {"en", "Home", "Capsule Corp.", "Kame House"},
      {"it", "Casa", "Capsule Corp.", "Casa di Kame"},
      {"de", "Zuhause", "Capsule Corp.", "Kame-Haus"},
      {"fr", "Maison", "Capsule Corp.", "Kame House"},
      {"pt", "Casa", "Corporação Cápsula", "Casa do Kame"},
      {"ja", "住まい", "カプセルコーポレーション", "カメハウス"},
  };
  const Texto* texto = &textos[1];
  for (const Texto& t : textos)
    if (!std::strcmp(T::LangCode(), t.idioma)) { texto = &t; break; }
  return !nombre ? texto->casa : H::CasaActual() == H::CasaHabitacion::kKame ? texto->kame : texto->capsule;
}

const char* FondoCasaHabitacion() {
  return H::CasaActual() == H::CasaHabitacion::kKame ? "fondo_habitacion_kame.png" : "fondo_habitacion_capsule.png";
}

bool PulsarCasaHabitacion(const S::Area& a) {
  const S::Area q = v2::Lienzo(a);
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(q.At(583, 128), q.At(1533, 186));
}

// Formas de la raza del heroe: (abiertas + base, total + base) y el nombre de la ultima abierta (la "actual").
struct ResumenEvo {
  int abiertas = 1, total = 1;
  std::string actual;
};
ResumenEvo ResumenEvolucion() {
  ResumenEvo r;
  r.actual = T::T("Forma base", "Base form");
  const std::vector<H::InfoForma> v = H::FormasDeRaza(H::RazaEvolucion());
  r.total += int(v.size());
  for (const H::InfoForma& f : v)
    if (f.estado == 0) ++r.abiertas, r.actual = f.nombre;
  return r;
}

std::string ValorHabitacion(int fila) {
  switch (fila) {
    case 0:
      if (g_hab_receta >= 0) return RecetaEtiqueta(g_hab_receta) + T::T(" · A: aplicar", " · A: apply");
      return H::NombreCuerpo() + ", " + T::T("traje ", "costume ") + Num(H::Traje() + 1) + "/" + Num(H::NumTrajes());
    case 1: {
      if (!H::EvolucionActiva()) return T::T("Apagado", "Off");
      const ResumenEvo r = ResumenEvolucion();
      return r.actual + ", " + Num(r.abiertas) + "/" + Num(r.total);
    }
    case 2: {
      const int m = H::MaestroActual();
      return m >= 0 ? M::Nombre(H::Maestro(m).id) + " " + Num(H::Maestro(m).entreno) + " %" : std::string(T::T("Sin maestro", "No master"));
    }
    case 3: {
      const int huecos = H::TecnicasV2On() ? 4 : 3;   // HEROE-TEC: el 4.o es la esquiva
      int n = 0;
      for (int k = 0; k < huecos; ++k) n += H::Equipada(k) != 0;
      return Num(n) + "/" + Num(huecos) + " " + T::T("huecos", "slots");
    }
    case 5: return Num(H::ColeccionPorcentaje()) + " %";   // HEROE-REC: Coleccion
    default: {
      std::string r;   // camino recorrido: las marcas de ruta con nombre ("Nave y Laboratorio")
      for (const std::string& m : H::MarcasRuta()) {
        const char* n = m == "camino_nave" ? T::T("Nave", "Ship") : m == "camino_vegeta" ? "Vegeta" : m == "camino_lab" ? T::T("Laboratorio", "Lab")
                        : m == "camino_ciudad" ? T::T("Ciudad", "City") : nullptr;
        if (n) r += (r.empty() ? "" : T::T(" y ", " and ")) + std::string(n);
      }
      if (H::ColeccionVueltasOn()) {
        const int nf = int(H::FinalesCompletados().size());
        if (H::RutaCompletada())
          return std::string(T::T("Finales ", "Endings ")) + Num(nf) + "/3 · " + T::T("Nueva vuelta", "New lap");
        if (!r.empty() && (H::VueltaRuta() > 1 || nf > 0))
          return std::string(T::T("Vuelta ", "Lap ")) + Num(H::VueltaRuta()) + " · " + r + " · " + Num(nf) + "/3";
        if (!r.empty()) return r;
        if (H::VueltaRuta() > 1 || nf > 0)
          return std::string(T::T("Vuelta ", "Lap ")) + Num(H::VueltaRuta()) + " · " + T::T("Finales ", "Endings ") + Num(nf) + "/3";
      }
      if (!r.empty()) return r;
      const int s = H::Saga();
      return s >= 0 && s < H::kSagasJugables ? std::string(H::NombreSaga(s)) : std::string(T::T("Completada", "Completed"));
    }
  }
}

std::string DescripcionHabitacion(int fila) {
  switch (fila) {
    case 0: return T::T("Cambia pelo, traje, colores y logo. La raza no cambia.", "Change hair, costume, colors and logo. Your race stays.");
    case 1: return T::T("Las formas de tu raza y lo que abre cada camino.", "The forms of your race and what each path unlocks.");
    case 2: return T::T("Elige maestro: cada uno da su estilo y sus técnicas.", "Choose a master: each gives a style and techniques.");
    case 3:
      if (REXCVAR_GET(dbz3_heroe_ayuda_esquiva) && H::TecnicasV2On())
        return T::T("Equipa tus 4 técnicas: definitiva, dos de ki y esquiva.", "Equip your 4 techniques: ultimate, two ki moves and dodge.");
      return T::T("Equipa tus 3 técnicas: definitiva y dos de ki.", "Equip your 3 techniques: ultimate and two ki moves.");
    case 5: return T::T("Técnicas, títulos y formas que has ganado, y lo que falta.", "Techniques, titles and forms you have earned, and what is missing.");
    default: return T::T("Tu nivel, tus barras y el camino recorrido.", "Your level, bars and the path so far.");
  }
}

// Caja fina de una fila / cabecera: interior oscuro del panel y el marco naranja del juego a escala pequena (las filas de la
// maqueta llevan el mismo marco 2033_05 en fino); el marco sobresale ~7 px, por eso el hueco entre filas.
void CajaFinaHab(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1) {
  S::Frame(dl, q.At(x0 + 6, y0 + 6), q.At(x1 - 6, y1 - 6), q.s * 0.42f);
}

void DibujarHabitacionV2(ImDrawList* dl, const S::Area& a, float t, uint16_t p, double ahora) {
  if (!FondoGpt3(dl, a, FondoCasaHabitacion()) && !v2::Fondo(dl, a, v2::kFondoFicha)) FondoSinAnfitrion(dl, a);
  {   // -25 % en el tercio de abajo (degradado negro desde y 680 de 1080)
    const S::Imagen g = S::GuestImage(ImGui::GetIO());
    const float y0 = g.p0.y + (g.p1.y - g.p0.y) * 680.0f / 1080.0f;
    dl->AddRectFilledMultiColor(ImVec2(g.p0.x, y0), g.p1, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 64),
                                IM_COL32(0, 0, 0, 64));
  }
  const S::Area q = v2::Lienzo(a);
  S::BannerBeta(dl, q, T::T("Habitación", "Room"), nullptr, 1.2f, -14.0f);   // rótulo al tamaño de la maqueta; BETA sin tocar el marco de la carta
  {   // chip "START > Habitacion": solo ayuda de como se llega desde el mapa
    v2::Caja(dl, q, 470, 50, 760, 98);
    S::HelpLine(dl, q.At(615, 74), 34 * q.s, {{"START", T::T("Habitación", "Room")}});
  }
  // carta del heroe (izquierda): marco naranja, el heroe en vivo de rodillas para arriba y la peana cortada por el borde
  constexpr float cx0 = 67, cy0 = 142, cx1 = 533, cy1 = 717;
  v2::Panel(dl, q, cx0, cy0, cx1, cy1);
  {
    const ImVec2 c0 = q.At(cx0 + 16, cy0 + 16), c1 = q.At(cx1 - 16, cy1 - 16);
    dl->PushClipRect(c0, c1, true);
    const S::Pic gpt = ArteGpt3(FondoCasaHabitacion());
    if (const S::Pic casa = gpt ? gpt : S::Img(v2::kFondoFicha)) {
      const float sw = gpt ? gpt.w : 912.0f, sh = gpt ? gpt.h : 684.0f;   // interior = la casa desenfocada (9 tomas desplazadas) con tinte azul, como la maqueta
      const S::Imagen g = S::GuestImage(ImGui::GetIO());
      const float gw = g.p1.x - g.p0.x, gh = g.p1.y - g.p0.y;
      const ImVec4 src(sw * (c0.x - g.p0.x) / gw, sh * (c0.y - g.p0.y) / gh, sw * (c1.x - g.p0.x) / gw, sh * (c1.y - g.p0.y) / gh);
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          S::DrawPic(dl, casa, ImVec2(c0.x + dx * 9 * q.s, c0.y + dy * 9 * q.s), ImVec2(c1.x + dx * 9 * q.s, c1.y + dy * 9 * q.s), src, IM_COL32(255, 255, 255, 80));
      dl->AddRectFilled(c0, c1, IM_COL32(18, 32, 85, 175));
    }
    v2::Peana(dl, q, (cx0 + cx1) * 0.5f, cy1 - 26, 470);
    constexpr float k = 1.5f;   // el heroe llena la carta: de la cabeza a las rodillas; el borde de abajo corta la peana
    S::Area qf = q;
    qf.s = q.s * k;
    qf.p0 = q.At((cx0 + cx1) * 0.5f - v2::kFigCx * k, -70 - 161.0f * k);
    qf.p1 = ImVec2(qf.p0.x + 1600.0f * qf.s, qf.p0.y + 900.0f * qf.s);
    FiguraV2(dl, qf);
    dl->PopClipRect();
  }
  // descripcion de la fila activa, una linea bajo la carta
  v2::Explicacion(dl, q, cx0, cy1 + 18, 833, g_hab_aviso.empty() ? DescripcionHabitacion(g_hab_cur) : g_hab_aviso, 1);
  // cabecera y lista (derecha)
  constexpr float lx0 = 583, lx1 = 1533;
  {
    CajaFinaHab(dl, q, lx0, 128, lx1, 186);   // la cabecera, caja fina como la maqueta
    const std::string cab = std::string(TextoCasaHabitacion(false)) + ": " + TextoCasaHabitacion(true);
    v2::Texto(dl, q, (lx0 + lx1) * 0.5f, 157, v2::kLetraTitulo, cab, 1, 2, false, false, 720);
    const float h = 38 * q.s;
    S::Glyph(dl, "LB", q.At(lx0 + 28, 157), h);
    const float rw = S::Glyph(nullptr, "RB", ImVec2(), h);
    S::Glyph(dl, "RB", ImVec2(q.At(lx1 - 28, 157).x - rw, q.At(0, 157).y), h);
  }
  static const char* kNomEs[kHabFilas] = {"Aspecto", "Evolución", "Maestros", "Técnicas", "Ficha", "Colección"};
  static const char* kNomEn[kHabFilas] = {"Appearance", "Evolution", "Masters", "Techniques", "Profile", "Collection"};
  const bool seis_filas = NumFilasHab() == kHabFilas;
  for (int i = 0; i < NumFilasHab(); ++i) {
    // Con Coleccion, repartir las seis filas por encima del radar sin cambiar su navegacion.
    const float y0 = seis_filas ? 210 + 82.0f * i : 217 + 93.0f * i;
    const float y1 = y0 + (seis_filas ? 70.0f : 80.0f), cy = (y0 + y1) * 0.5f;
    const bool sel = i == g_hab_cur;
    CajaFinaHab(dl, q, lx0, y0, lx1, y1);   // cada fila es su propia caja, con hueco entre ellas (maqueta)
    if (sel) {
      v2::Franja(dl, q, lx0 + 8, y0 + 5, lx1 - 8, y1 - 5, t);
      dl->AddRect(q.At(lx0 + 8, y0 + 5), q.At(lx1 - 8, y1 - 5), IM_COL32(150, 245, 240, 255), 3 * q.s, 0, 2 * q.s);
    }
    const bool apagada = i == 1 && !H::EvolucionActiva();
    // sin iconos: la maqueta aprobada no los lleva (el arte de GPT queda en ingame_ui/heroe, sin usar)
    v2::Texto(dl, q, lx0 + 36, cy, v2::kLetraTitulo, T::T(kNomEs[i], kNomEn[i]), 0, 0, false, apagada, 330);
    const std::string val = ValorHabitacion(i);
    v2::Texto(dl, q, 1242, cy, v2::kLetraFila, val, 1, 0, false, apagada, 400);   // valor nitido y separado de las flechas
    if (sel && (i == 0 || i == 2)) S::Arrows(dl, q.At(1022, cy - 26), q.At(1462, cy + 26), 40 * q.s, t);
  }
  // pie: ayuda de botones y el radar con las Esferas (no toca la carta)
  v2::BarraAyuda(dl, q, {{"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Volver al mapa", "Back to the map")},
                         {"RS", T::T("Girar", "Rotate")}, {"LB", T::T("Cambiar casa", "Change home")}},
                 980, 864);
  S::Area qr = q;   // dejar margen para la etiqueta larga en ingles y otros idiomas
  qr.p0.x -= 36 * q.s;
  qr.p1.x -= 36 * q.s;
  RadarV2(dl, qr, t, __builtin_popcount(unsigned(H::Esferas())), seis_filas ? 145 : 160, seis_filas ? 850 : 838);
  (void)p;
  (void)ahora;
}

}  // namespace
