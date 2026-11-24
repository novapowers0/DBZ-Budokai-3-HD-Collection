// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): dibujo de Create / Edit (maqueta 01_creador) y de su editor de color HSV
// (maqueta 02_editor_color). Casi todo es DIBUJO: la entrada del mando, el estado (g_fila, g_boton, g_panel, g_canal...) y
// lo que hace cada fila siguen en Crear (heroe_ui.cpp). HEROE-V05: con el aspecto nuevo Crear reparte los botones como la
// barra de ayuda de la maqueta 01 (ver kStartV2...) y el editor de color tiene la fila "Colores rapidos" (kRapidosV2).
// Se incluye en heroe_ui.cpp, dentro de dbz3::heroe_ui y antes de Crear (usa sus ayudantes del espacio anonimo).
// Kit: heroe_ui_v2.h. Medidas en px del lienzo 1600x900 (= maqueta 1280x720 x 1.25).
namespace {
std::string RasgoRazaTexto(bool corto = false);   // R23 HER-12: heroe_v2_evolucion.inl (la tabla de rasgos de la maqueta 12)

// HEROE-V05: botones del mando que el aspecto de siempre no usa en Create/Edit (mascara XInput de modos.cpp Pad())
constexpr uint16_t kStartV2 = 0x0010, kBackV2 = 0x0020, kLsV2 = 0x0040, kRsV2 = 0x0080;
// Reparto con el aspecto nuevo (lista): A Cambiar, X Nombre, Y Exportar, RB Confirmar, B Atras; LT/RT Girar;
// segunda linea: BACK Crear/Editar (antes Y), START Reset, RS Camara (antes X), LS Importar (si dbz3_heroe_importar).
int g_ayuda_v2 = 0;    // 4 / 5: la linea de ayuda ensena el resultado de Exportar / Importar hasta moverse de fila
int g_rapido_v2 = 0;   // color rapido elegido en el editor de color (g_canal == 3)
// Colores rapidos de la maqueta 02 (h 0..360, s y v -100..100 como C::Hsv; ColorHsv los pinta): dorado, gris, rojo,
// amarillo, violeta, azul, verde, naranja, naranja claro, azul claro, amarillo verdoso, ocre y celeste. Multiplos de 10
// para que Tono/Saturacion/Brillo (pasos de 10) los sigan afinando.
const C::Hsv kRapidosV2[] = {{45, 20, 20},  {0, -100, 10}, {0, 80, 30},   {60, 60, 50},  {280, 40, 20}, {230, 80, 0}, {150, 40, 0},
                             {30, 50, 30},  {30, 10, 70},  {210, 60, 60}, {70, 40, 30},  {40, 20, -20}, {200, 50, 80}};
constexpr int kNumRapidosV2 = int(std::size(kRapidosV2));

// LT / RT: son analogicos y no van en la mascara `p` de modos.cpp; se leen aqui como Pad() lee el mando (en el hilo de
// la UI, con DeferUi). Devuelve -1 (LT), 1 (RT) o 0.
std::atomic<int> g_rx_v2{0};   // POST3: palanca derecha (x), leida con los gatillos (teclado: flechas)
float GatillosV2() {
  static std::atomic<int> lt{0}, rt{0};
  static std::atomic<bool> pendiente{false};
  if (!pendiente.exchange(true)) {
    dbz3::launcher::DeferUi([]() {
      int l = 0, r = 0, x = 0;
      if (auto* run = rex::Runtime::instance())
        if (auto* inp = static_cast<rex::input::InputSystem*>(run->input_system())) {
          rex::input::X_INPUT_STATE s = {};
          if (inp->GetStateForUI(0, &s) == 0)
            l = int(s.gamepad.left_trigger), r = int(s.gamepad.right_trigger), x = int(int16_t(s.gamepad.thumb_rx));
        }
      lt = l, rt = r, g_rx_v2 = x;
      pendiente = false;
    });
  }
  return (rt.load() > 64 ? 1.0f : 0.0f) - (lt.load() > 64 ? 1.0f : 0.0f);
}

// POST3/HEROE-VISTAS: vista previa del creador con vistas fijas, giro libre y animaciones reales de B3 (.malla ANI1).
// LT / RT (teclado Q / E) pulsados: vista anterior / siguiente (frente, derecha, espalda, izquierda); mantenidos, la palanca
// derecha (flechas) o arrastrar con el raton: giro libre. LB (teclado 1): siguiente animacion. RB sigue siendo Confirmar.
// Pruebas (solo tools/pruebas.py): DBZ3_TEST_HEROE_YAW=grados fija el giro (0 frente, 90 derecha, 180 espalda, 270
// izquierda) y DBZ3_TEST_HEROE_ANIM=tipo[:cuadro] la animacion (0 guardia, 1 andar, 2 golpe, 3 ki, 4 victoria, 5 transf.).
const char* const kVistasV2[4][2] = {{"Frente", "Front view"}, {"Derecha", "Right view"}, {"Espalda", "Back view"}, {"Izquierda", "Left view"}};
const char* const kAnimsVista[6][2] = {{"En guardia", "On guard"}, {"Andar", "Walking"},          {"Golpe", "Punch"},
                                       {"Cargar ki", "Charge ki"}, {"Pose de victoria", "Victory pose"}, {"Transformación", "Transformation"}};
int g_vista_v2 = 0;              // 0..3 = kVistasV2; -1 = giro libre
int g_anim_v2 = 0;               // tipo de kAnimsVista (si la malla lo trae)
double g_anim_t0 = 0;            // cuando se eligio (la animacion empieza en su cuadro 0)
int g_anim_fijo = -1;            // DBZ3_TEST_HEROE_ANIM=tipo:cuadro -> cuadro fijo
std::vector<int> g_tipos_v2;     // animaciones de la malla que se pinta (las pone FiguraV2)
constexpr float kGiroVistaV2 = 2.0f;   // g_giro de una vista a la siguiente (g_giro va en octavos de vuelta)

void ControlesVistaV2(const S::Area& a, uint16_t p, float dt, double ahora) {
  static double t_gat = -1;
  static float dir_gat = 0;
  const float g = GatillosV2();
  auto paso = [&](float d) {
    const int v = g_vista_v2 >= 0 ? g_vista_v2 : int(std::lround(g_giro / kGiroVistaV2));
    g_vista_v2 = ((v + int(d)) % 4 + 4) % 4;
    REXLOG_INFO("dbz3 heroe: vista previa: {}", kVistasV2[g_vista_v2][1]);
  };
  if (g != 0.0f) {
    if (t_gat < 0) t_gat = ahora, dir_gat = g;
    if (ahora - t_gat > 0.3) g_giro += 3.0f * g * dt, g_vista_v2 = -1;   // mantenido: 3/8 de vuelta por segundo
  } else if (t_gat >= 0) {
    if (ahora - t_gat <= 0.3) paso(dir_gat);   // pulsado: la vista de al lado
    t_gat = -1;
  }
  if (const int rx = g_rx_v2.load(); std::abs(rx) > 8000) g_giro += 3.0f * float(rx) / 32767.0f * dt, g_vista_v2 = -1;
  ImGuiIO& io = ImGui::GetIO();
  const S::Area q = v2::Lienzo(a);
  if (ImGui::IsMouseDragging(0, 2.0f) && io.MouseClickedPos[0].x < q.At(v2::kPanX0 - 20, 0).x &&
      io.MouseDelta.x != 0.0f) {
    g_giro += io.MouseDelta.x / (45.0f * 2.0f * std::max(q.s, 0.1f));   // medio grado por px del lienzo
    g_vista_v2 = -1;
  }
  if (g_vista_v2 >= 0) {   // hacia la vista elegida por el camino corto, sin salto
    float obj = kGiroVistaV2 * float(g_vista_v2);
    obj += 8.0f * std::round((g_giro - obj) / 8.0f);
    g_giro += (obj - g_giro) * std::min(1.0f, dt * 12.0f);
  }
  if (p & kLb) {
    const auto& t = g_tipos_v2;
    if (!t.empty()) {
      const auto it = std::upper_bound(t.begin(), t.end(), g_anim_v2);
      g_anim_v2 = it == t.end() ? t.front() : *it;
      g_anim_t0 = ahora;
      const bool nombre = g_anim_v2 >= 0 && g_anim_v2 < 6;
      REXLOG_INFO("dbz3 heroe: vista previa: animacion {} ({})", g_anim_v2, nombre ? kAnimsVista[g_anim_v2][1] : "extra");
    }
  }
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_YAW")) g_giro = float(std::atof(e)) / 45.0f, g_vista_v2 = -1;
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_ANIM")) {
    g_anim_v2 = std::clamp(std::atoi(e), 0, REXCVAR_GET(dbz3_lim_ani2) ? 31 : 5);
    const char* c = std::strchr(e, ':');
    g_anim_fijo = c ? std::max(0, std::atoi(c + 1)) : -1;
  }
}

// Fila de Create/Edit (o su editor de color abierto) que cambia la cabeza o el pecho (MEJORAS 01, ENCAJA): pelo y sus
// colores, cara, rostro, ojos, pecho/torso. Piernas, botas o guantes no: no hay preset de camara para ellos.
bool FilaArribaV2() {
  if (g_nom_abierto || g_imp_abierto) return false;
  const int f = g_panel >= 0 ? g_panel : g_fila;
  if (f < 0 || f >= FilaBotones()) return false;
  if (Xv2()) return f <= 2 || f == 8 || f == 9;   // Hair, FaceBase, Bust; color de pelo y de torso
  if (g_edit) return f <= 4;                      // pelo, sus dos colores, rostro y ojos (en UT solo pelo y color)
  return ZonaB3Fila(f) == 0;                      // torso de las piezas de B3
}

// Personaje grande a la izquierda sobre la peana, sin marco. El aviso de error (sin Python, sin datos...) va en una caja
// en su columna; la ruta del registro solo va al registro (texto tecnico fuera de la vista del jugador).
// acercar (solo el creador): con la vista 3D y la camara de serie (preset 1, zoom 1), si la fila es de cabeza o pecho se
// pinta con el preset "busto" que ya existe (kCams[2]) y se deja como estaba; sin peana, que quedaria bajo el pecho.
// No genera nada nuevo: las tiras (PNG) y el retrato no se acercan.
// Aro pintado en el suelo de fondo_creador_domos.png (1920x1080: centro 480,859, ancho 518) en el lienzo de 1600x900 (x0,8333);
// la figura del creador va 30 px mas arriba (qf), de ahi el +30. Usuario 21/11: la peana y el aro no coincidian.
bool g_aro_creador = false;
constexpr float kAroCx = 400, kAroPies = 716 + 30, kAroAncho = 432;
// CAC3: dos imágenes nativas del ACE 12. Consumo directo: no admite PNG de mods ni reservas ajenas al juego.
S::Pic TexturaEstallidoCac3(const char* nombre) {
  namespace N = dbz3::ingame::native_ui;
  using rex::ui::ImmediateTexture;
  struct Pieza {
    N::Image imagen;
    std::unique_ptr<ImmediateTexture> tex;
    S::Pic pic;
  };
  static std::map<std::string, Pieza> piezas;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer || !N::Ready()) return {};
  auto [it, nueva] = piezas.try_emplace(nombre);
  auto& p = it->second;
  if (nueva && N::Take(nombre, p.imagen)) {
    auto& px = p.imagen.rgba;
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
      // t16 viene ya azulado. Conservar su silueta e intensidad y dar el RGB al vértice.
      px[i + 3] = uint8_t((unsigned(px[i + 3]) * std::max({px[i], px[i + 1], px[i + 2]}) + 127) / 255);
      px[i] = px[i + 1] = px[i + 2] = 255;
    }
  }
  if (!p.tex && !p.imagen.rgba.empty()) {
    const auto& im = p.imagen;
    p.tex = drawer->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
    if (p.tex) {
      p.pic = {reinterpret_cast<ImTextureID>(p.tex.get()), float(im.w), float(im.h)};
      p.imagen = {};
    }
  }
  return p.pic;
}

void PintarEstallidoCac3(ImDrawList* dl, const S::Area& q, float cx, float y0, float ancho, float alto,
                        int anim, std::string_view color) {
  if ((anim != 0 && anim != 5) || color.size() != 7 || color.front() != '#') return;
  unsigned rgb = 0;
  for (const char c : color.substr(1)) {
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 :
                  c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) return;
    rgb = (rgb << 4) | unsigned(d);
  }
  const S::Pic estallido = TexturaEstallidoCac3("cac3_estallido");
  if (!estallido) return;
  const S::Pic nucleo = TexturaEstallidoCac3("cac3_nucleo");
  const auto tintar = [&](int a) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a); };
  const float cy = y0 + alto * .45f, r = ancho * .46f;
  // Muestra fija del estallido en la columna de la figura; no tapa el menú ni cambia la animación de combate.
  S::DrawPic(dl, estallido, q.At(cx - r, cy - r), q.At(cx + r, cy + r), {}, tintar(190));
  const float rn = r * .31f;
  S::DrawPic(dl, nucleo, q.At(cx - rn, cy - rn), q.At(cx + rn, cy + rn), {}, tintar(72));
  if (std::getenv("DBZ3_TEST_CAC3_ESTALLIDO")) {
    static unsigned ultimo = 0x1000000u;
    if (ultimo != rgb) {
      ultimo = rgb;
      REXLOG_INFO("dbz3 heroe: CAC3 previa ACE12 #{:06X}", rgb);
    }
  }
}

void FiguraV2(ImDrawList* dl, const S::Area& q, bool acercar = false, bool creador = false) {
  const bool forma = creador && !g_nom_abierto && !g_imp_abierto && g_panel < 0 && EsFilaFormaCac2(g_fila) && g_forma_cac2;
  VistaElegida v = forma ? VistaFormaCac2() : ElegirVista();
  // Arnes: muestra una geometria sintetica en vez de su vista temporal, sin tocar el cache ni el contenido nativo.
  if (creador) {
    if (const char* path = std::getenv("DBZ3_TEST_HEROE_LIM")) {
      static Modelo3D test;
      static const bool leido = LeerMalla(fs::u8path(path), test);
      if (leido) v.m3 = &test;
    }
  }
  if (creador && !forma) GuardarTirasCreador(v.m3);
  const bool aro = creador && g_aro_creador;
  const float cx = aro ? kAroCx : v2::kFigCx, pies = aro ? kAroPies : v2::kFigPies;
  const bool cerca = acercar && v.m3 && g_cam_preset == 1 && g_cam_zoom == 1.0f && FilaArribaV2();
  if (!cerca) v2::Peana(dl, q, cx, pies, aro ? kAroAncho : 375);
  bool pintada = false;
  if (v.m3) {
    // misma caja que la de siempre (342x464) escalada a 680 de alto: los pies caen al 94 % de la caja (medido en las
    // capturas del juego del 11/11: Goku y Piccolo; con el 84 % de antes quedaban 75 px bajo la peana, tras la placa)
    const float alto = 680, ancho = alto * 342.0f / 464.0f, y0 = pies - 0.94f * alto;
    const int preset = g_cam_preset;
    const float yaw = g_cam_yaw;
    if (cerca) g_cam_preset = 2;
    g_cam_yaw = 30.0f * std::max(0.0f, 1.0f - std::min(std::fmod(std::fmod(g_giro, 8.0f) + 8.0f, 8.0f), 8.0f - std::fmod(std::fmod(g_giro, 8.0f) + 8.0f, 8.0f)));   // 30 grados solo de frente (la guardia de frente tapa la cara con el puno); lados y espalda exactos
    // POST3: las vistas son de verdad (frente, lado, espalda): la camara no orbita los 25 grados de siempre
    g_tipos_v2.clear();
    for (const auto& an : v.m3->anims) g_tipos_v2.push_back(an.tipo);
    std::sort(g_tipos_v2.begin(), g_tipos_v2.end());
    const double t_anim = g_anim_fijo >= 0 ? g_anim_fijo / 60.0 : dbz3::RelojT(ImGui::GetTime()) - g_anim_t0;
    const auto& cuadro = MallaCuadro(*v.m3, g_anim_v2, t_anim);
    pintada = Pintar3D(dl, *v.m3, q.At(cx - ancho * 0.5f, y0), q.At(cx + ancho * 0.5f, y0 + alto), g_giro * 6.2831853f / 8.0f,
             forma ? &cuadro : &MallaConOcultas(*v.m3, cuadro));
    if (forma && pintada && H::RecetaFormaCac2() && H::FormaReceta(g_forma_cac2) >= 0)
      PintarEstallidoCac3(dl, q, cx, y0, ancho, alto, g_anim_v2, Ficha().forma_estallido[g_forma_cac2]);
    g_cam_preset = preset;
    g_cam_yaw = yaw;
  }
  if (!pintada) {
    if (v.m3 && std::getenv("DBZ3_TEST_VISTA_VERTICES")) {
      static int ultimo_cuerpo = -1;
      if (ultimo_cuerpo != H::Cuerpo()) {
        ultimo_cuerpo = H::Cuerpo();
        REXLOG_INFO("dbz3 heroe: vista 3D sin presupuesto, usando reserva");
      }
    }
    g_tipos_v2.clear();
    const S::Pic tira = forma ? S::Pic{} : v.p ? v.p : v.m3 && !CuerpoUT() ? Vista(H::Cuerpo(), H::Traje(), dbz3::RelojT(ImGui::GetTime())) : S::Pic{};
    if (tira) PintarTira(dl, q, tira, cx - 240, 170, cx + 240, pies + 6);
    else if (!forma) {
      Cara(dl, q, CuerpoUT() ? H::EstiloId() : H::Cuerpo(), cx, 430, 220);
      v2::Texto(dl, q, cx, 590, v2::kLetraPeque,
                HayGenerando() ? std::string(T::T("Preparando la vista previa", "Preparing the preview")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')
                               : T::T("Vista previa: retrato", "Preview: portrait"),
                1, 0, false, true, 500);
    }
  }
  if (!v.aviso.empty()) {
    static std::string dicho;
    if (dicho != v.aviso + v.clave_error) {
      dicho = v.aviso + v.clave_error;
      REXLOG_WARN("dbz3 heroe: {} (registro: {})", v.aviso, Utf8(Vistas() / (v.clave_error + ".log")));
    }
    v2::Explicacion(dl, q, 40, 560, 585, v.aviso, 4);
  }
  if (creador && !v.clave_error.empty() && v.clave_error.rfind("f_", 0) == 0) {   // CAC-13: barra de progreso y aviso del tope de ventanas
    int pn = 0, pt = 1;
    std::string ptxt;
    if (EnMarcha(v.clave_error) && ProgresoClave(v.clave_error, &pn, &pt, &ptxt)) {
      dl->AddRectFilled(q.At(60, 742), q.At(540, 747), IM_COL32(255, 255, 255, 40), 2 * q.s);
      dl->AddRectFilled(q.At(60, 742), q.At(60 + 480.0f * std::min(pn, pt) / float(pt), 747), IM_COL32(255, 200, 90, 230), 2 * q.s);
    }
    if (const TopeInfo* ti = TopeClave(v.clave_error); ti && ti->excedido) {
      const std::string msg = std::string(T::T("Esta mezcla pasa del tope (", "This mix goes over the limit (")) + std::to_string(ti->ventanas) + "/" +
                              std::to_string(ti->tope) + T::T("): el cuerpo desaparecería en combate. Quita una pieza.",
                                                              "): the body would vanish in battle. Remove a piece.");
      dl->AddRectFilled(q.At(40, 640), q.At(585, 700), IM_COL32(40, 26, 8, 235), 6 * q.s);
      dl->AddText(ImGui::GetFont(), 17 * q.s, q.At(52, 648), IM_COL32(255, 190, 60, 255), msg.c_str(), nullptr, 520 * q.s);
    }
  }
}

// Linea de ayuda bajo el panel (HEROE-V05): la de la fila elegida, con los textos de siempre (Ayuda) salvo las filas de
// color, que dicen que hace A y de que parte es (maqueta: "A: elige el color de la camisa (Parte 2)."; el nombre de la
// prenda no se sabe en el creador, se dice la parte). Tras Exportar / Importar, su resultado.
std::string AyudaV2() {
  if (g_panel < 0 && !g_imp_abierto && g_ayuda_v2 == 4) return TextoExportar();
  if (g_panel < 0 && !g_imp_abierto && g_ayuda_v2 == 5 && REXCVAR_GET(dbz3_heroe_importar)) return TextoImportar();
  if (g_panel >= 0) {
    const std::string n = NombreColor(g_panel);
    switch (g_canal) {
      case 0: return n + ": " + T::T("elige el tono.", "choose the hue.");
      case 1: return n + ": " + T::T("elige la saturación.", "choose the saturation.");
      case 2: return n + ": " + T::T("elige el brillo.", "choose the brightness.");
      default: return T::T("Colores rápidos: A pone el color elegido; X vuelve al original.",
                           "Quick colors: A applies the chosen color; X goes back to the original.");
    }
  }
  if (!g_imp_abierto && EsFilaFormaCac2(g_fila) && g_forma_cac2 && H::RecetaFormaCac2()) {
    const auto& col = Ficha().forma_estallido[g_forma_cac2];
    return std::string(T::T("Y: receta · X: color de estallido (solo vista previa): ",
                          "Y: recipe · X: burst color (preview only): ")) +
        (col.empty() ? T::T("Nativo", "Native") : col);
  }
  if (!g_imp_abierto && EsFilaFormaCac2(g_fila))
    return T::T("Izq./der. o A: recorrer las formas. No equipa ni desbloquea cápsulas.",
                "Left/right or A: browse forms. Does not equip or unlock capsules.");
  if (!g_imp_abierto && EsFilaFotoCac2(g_fila))
    return T::T("A: cambiar pose. Izq./der.: giro. Se aplica al volver a montar el personaje.",
                "A: change pose. Left/right: angle. Takes effect when the character is rebuilt.");
  if (!g_imp_abierto && ColorDeFila(g_fila)) {
    if (g_edit && !Xv2()) {   // Edit ya tiene una frase por fila ("Elige el color base del pelo..."): con "A: " delante
      std::string s = Ayuda();
      if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = char(s[0] - 'A' + 'a');
      return "A: " + s;
    }
    return std::string(T::T("A: elige el color propio (", "A: choose a custom color (")) + NombreColor(g_fila) + ").";
  }
  return Ayuda();
}

// R23 HER-12 (DISENO 7.3, "miniaturas en Aspecto"): en la fila Traje, una miniatura por traje del cuerpo (el busto de frente de
// la tira de giros que vista_heroe.py hace en el PC del jugador al montar: nada se reparte) y aro cian en el elegido. Un traje
// sin tira aun la pide en segundo plano y sale como "?" mientras.
void MiniaturasTrajeV2(ImDrawList* dl, const S::Area& q, float x0, float cy, float alto) {
  const int n = std::min(H::NumTrajes(), 4), id = H::Cuerpo(), elegido = H::Traje();
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  const float lado = alto - 8, paso = lado + 8;
  for (int k = 0; k < n; ++k) {
    const float x = x0 + paso * float(k), y = cy - lado * 0.5f;
    const ImVec2 p0 = q.At(x, y), p1 = q.At(x + lado, y + lado);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 18, 34, 220), 6 * q.s);
    if (const S::Pic p = Vista(id, k, ahora)) {   // fotograma 0 (de frente) de la tira: su mitad de arriba centrada = el busto
      const float fw = p.h;   // fotogramas cuadrados
      S::DrawPic(dl, p, p0, p1, ImVec4(fw * 0.25f, fw * 0.04f, fw * 0.75f, fw * 0.54f));
    } else {
      v2::Texto(dl, q, x + lado * 0.5f, cy, 26, "?", 1, 2);
    }
    dl->AddRect(p0, p1, k == elegido ? IM_COL32(150, 245, 240, 255) : IM_COL32(232, 150, 40, 160), 6 * q.s, 0, (k == elegido ? 3.0f : 1.5f) * q.s);
  }
}

// R23 HER-12: color de serie de Parte 1-4 del traje elegido (bola "Original", maqueta 01). Lo escribe vista_heroe.py junto a la
// tira (<cuerpo>_<traje>.colores: 4 lineas "r g b" o "-"); sin archivo, 0 = la bola gris de antes. Se relee cada 3 s.
ImU32 ColorOriginalParte(int parte) {
  static std::string clave;
  static ImU32 col[4] = {};
  static double leido = -1e9;
  const std::string k = Num(H::Cuerpo()) + "_" + Num(H::Traje());
  const double ahora = ImGui::GetTime();
  if (k != clave || ahora - leido > 3.0) {
    clave = k, leido = ahora;
    std::fill(std::begin(col), std::end(col), ImU32(0));
    const fs::path f = Vistas() / (k + ".colores");
    std::error_code ec;
    if (!fs::exists(f, ec) && fs::exists(VistaPng(H::Cuerpo(), H::Traje()), ec) && !H::CuerpoPropio(H::Cuerpo()))   // cache de antes
      GenerarCon("col_" + k, L"--solo " + fs::path(k).wstring() + L" --faltan", f);
    std::ifstream in(f);
    std::string lin;
    for (int i = 0; i < 4 && std::getline(in, lin); ++i) {
      int r, g, b;
      if (std::sscanf(lin.c_str(), "%d %d %d", &r, &g, &b) == 3) col[i] = IM_COL32(r, g, b, 255);
    }
  }
  return parte >= 0 && parte < 4 ? col[parte] : 0;
}

// Lista de Create/Edit (maqueta 01): las mismas filas, en el mismo orden y con los mismos textos que DibujarLista
// (TextosFila). HEROE-V05: sin la fila de botones (van en la barra de ayuda), filas de 60 px, etiqueta fina, el orbe en
// la columna 1022 y los valores en la 1115 (maqueta x 818 / 892, x 1.25). Devuelve el y de abajo del panel.
float ListaV2(ImDrawList* dl, const S::Area& q, float t) {
  const bool tabs = PestanasCac2();
  const auto filas = tabs ? FilasPestanaCac2() : std::vector<int>{};
  const int n = tabs ? int(filas.size()) : FilaBotones();
  const float x0 = v2::kPanX0, x1 = v2::kPanX1, y0 = v2::kPanY0 + (tabs ? 42 : 0);
  if (tabs) {
    const float paso = (x1 - x0) / float(std::size(kTabsCac2));
    for (int k = 0; k < int(std::size(kTabsCac2)); ++k) {
      const float x = x0 + k * paso;
      v2::Caja(dl, q, x + 2, y0 - 42, x + paso - 2, y0 - 4);
      if (k == g_tab_cac2) dl->AddRect(q.At(x + 2, y0 - 42), q.At(x + paso - 2, y0 - 4),
                                      IM_COL32(255, 214, 64, 255), 9 * q.s, 0, 2 * q.s);
      v2::Texto(dl, q, x + paso * .5f, y0 - 23, 22, T::T(kTabsCac2[k][0], kTabsCac2[k][1]),
                1, k == g_tab_cac2 ? 2 : 0, false, false, paso - 14);
    }
  }
  const float alto = std::min(55.0f, (tabs ? 500.0f : 550.0f) / float(std::max(n, 1)));   // 13 filas en XV2: se aprietan, no se salen
  const float y1 = y0 + 18 + alto * float(n) + 22;                       // 10 filas: 678 (maqueta 1: 815 / 1080 x 900)
  v2::Panel(dl, q, x0, y0, x1, y1);
  const float rx0 = x0 + 28, rx1 = x1 - 25;   // franja de la elegida (maqueta 522..1240 x 1.25)
  const float xv = 1022 - alto * 0.36f;       // Fila pone el centro del orbe en xv + 0.36 alto
  for (int i = 0; i < n; ++i) {
    const int f = tabs ? filas[i] : i;
    const FilaCrear r = TextosFila(f);
    const bool sel = f == g_fila && g_panel < 0;
    // "Original" (0/0/0): el color de serie de la parte, que mide vista_heroe.py (zonas por hueso y textura) en el PC del
    // jugador (ColorOriginalParte); sin su archivo, gris como antes.
    v2::Fila(dl, q, t, rx0, rx1, y0 + 18 + alto * (float(i) + 0.5f), alto, r.et, r.val, sel, xv,
             r.color ? (r.orig && r.color->h == 0 && r.color->s == 0 && r.color->v == 0 ? r.orig : r.color->h == 0 && r.color->s == 0 && r.color->v == 0 && !g_edit && !Xv2() && !CuerpoUT() && f >= 1 && f <= 4 &&
                        ColorOriginalParte(f - 1)
                            ? ColorOriginalParte(f - 1)
                            : ColorHsv(*r.color))
                     : 0,
             r.flechas, true, 1115);
    if (f == 0 && !g_edit && !Xv2() && !CuerpoUT()) MiniaturasTrajeV2(dl, q, 1335, y0 + 18 + alto * 0.5f, alto);   // R23 HER-12
  }
  return y1;
}

// Barra de un canal del editor de color (maqueta 02): redondeada, borde oscuro, tono = arcoiris; saturacion y brillo
// tenidos con el tono elegido (de poco a mucho, con los mismos ColorHsv que la muestra); marcador triangular claro encima.
void BarraV2(ImDrawList* dl, const S::Area& q, float x0, float y, float x1, int canal, const C::Hsv& c) {
  const float r = 14 * q.s;
  const ImVec2 p0 = q.At(x0, y - 14), p1 = q.At(x1, y + 14);
  auto col = [&](float u) -> ImU32 {
    if (canal == 0) {
      float cr, cg, cb;
      ImGui::ColorConvertHSVtoRGB(std::min(u, 0.999f), 0.85f, 0.95f, cr, cg, cb);
      return IM_COL32(int(cr * 255), int(cg * 255), int(cb * 255), 255);
    }
    // POST2: como la maqueta 02 (tono 210 -> azul): saturacion de gris claro al tono vivo; brillo de negro al tono vivo
    float cr, cg, cb;
    const float h = float(((c.h % 360) + 360) % 360) / 360.0f;
    if (canal == 1) ImGui::ColorConvertHSVtoRGB(h, 0.87f * u, 0.92f - 0.04f * u, cr, cg, cb);
    else ImGui::ColorConvertHSVtoRGB(h, 0.87f, 0.04f + 0.86f * u, cr, cg, cb);
    return IM_COL32(int(cr * 255), int(cg * 255), int(cb * 255), 255);
  };
  dl->AddRectFilled(ImVec2(p0.x - 3 * q.s, p0.y - 3 * q.s), ImVec2(p1.x + 3 * q.s, p1.y + 3 * q.s), IM_COL32(6, 10, 20, 255), r + 3 * q.s);
  const float cy = (p0.y + p1.y) * 0.5f, xa = p0.x + r, xb = p1.x - r;
  dl->AddCircleFilled(ImVec2(xa, cy), r, col(0), 20);
  dl->AddCircleFilled(ImVec2(xb, cy), r, col(1), 20);
  const int tramos = 24;
  for (int i = 0; i < tramos; ++i) {
    const float u0 = float(i) / tramos, u1 = float(i + 1) / tramos;
    const ImU32 ca = col(u0), cb = col(u1);
    dl->AddRectFilledMultiColor(ImVec2(xa + (xb - xa) * u0, p0.y), ImVec2(xa + (xb - xa) * u1 + 0.5f, p1.y), ca, cb, cb, ca);
  }
  dl->AddLine(ImVec2(xa, p0.y + 3 * q.s), ImVec2(xb, p0.y + 3 * q.s), IM_COL32(255, 255, 255, 60), 2 * q.s);   // brillo
  const float v = canal == 0 ? c.h / 360.0f : ((canal == 1 ? c.s : c.v) + 100) / 200.0f;
  const float xm = xa + (xb - xa) * std::clamp(v, 0.0f, 1.0f), ym = p0.y - 6 * q.s, w = 11 * q.s, h = 16 * q.s;
  dl->AddTriangleFilled(ImVec2(xm, ym), ImVec2(xm - w, ym - h), ImVec2(xm + w, ym - h), IM_COL32(196, 238, 252, 255));
  dl->AddTriangle(ImVec2(xm, ym), ImVec2(xm - w, ym - h), ImVec2(xm + w, ym - h), IM_COL32(12, 40, 60, 255), 2 * q.s);
}

// Editor de color (maqueta 02): "Parte N: color propio" con una sola bola del color a la derecha; Tono / Saturacion /
// Brillo con BarraV2, flechas a los lados de la elegida y el numero a la derecha; debajo "Colores rapidos" (13 bolas).
float ColorV2(ImDrawList* dl, const S::Area& q, float t) {
  const C::Hsv* c = ColorDeFila(g_panel);
  if (c && EsFilaEnt(g_panel)) c = &g_panel_texcol;
  const float x0 = v2::kPanX0, x1 = v2::kPanX1, y0 = v2::kPanY0, y1 = y0 + 572;   // maqueta 72..528 x 1.25
  v2::Panel(dl, q, x0, y0, x1, y1);
  if (!c) return y1;
  v2::Texto(dl, q, x0 + 60, y0 + 67, 34, NombreColor(g_panel) + ": " + T::T("color propio", "custom color"), 0, 2, false,
            false, x1 - x0 - 220);
  v2::Orbe(dl, q, x1 - 99, y0 + 66, 37, ColorHsv(*c));
  const char* nom[3] = {T::T("Tono", "Hue"), T::T("Saturación", "Saturation"), T::T("Brillo", "Brightness")};
  const int val[3] = {c->h, c->s, c->v};
  const float bx0 = x0 + 315, bx1 = bx0 + 420;   // maqueta 752..1086 x 1.25
  for (int k = 0; k < 3; ++k) {
    const float y = y0 + 182 + 97.5f * float(k);
    const bool sel = k == g_canal;
    if (sel) v2::Franja(dl, q, x0 + 31, y - 48, x1 - 25, y + 37, t);
    v2::Etiqueta(dl, q, x0 + 59, y, v2::kLetraFila, nom[k], sel, bx0 - x0 - 120);
    BarraV2(dl, q, bx0, y, bx1, k, *c);
    if (sel) S::Arrows(dl, q.At(bx0 - 14, y - 17), q.At(bx1 + 14, y + 17), 34 * q.s, t);
    v2::Texto(dl, q, x1 - 63, y, v2::kLetraFila - 2, Num(val[k]), 2, 0, false, false, 120);
  }
  // colores rapidos: izquierda/derecha elige, A lo pone (Crear, g_canal == 3)
  const bool sel = g_canal == 3;
  if (sel) v2::Franja(dl, q, x0 + 31, y0 + 434, x1 - 25, y0 + 548, t);
  v2::Etiqueta(dl, q, x0 + 59, y0 + 458, v2::kLetraPeque, T::T("Colores rápidos", "Quick colors"), sel);
  for (int i = 0; i < kNumRapidosV2; ++i) {
    const float cx = x0 + 76 + 65 * float(i), cy = y0 + 511;
    if (sel && i == g_rapido_v2) dl->AddCircle(q.At(cx, cy), 31 * q.s, IM_COL32(196, 238, 252, 255), 32, 3.5f * q.s);
    v2::Orbe(dl, q, cx, cy, 25, ColorHsv(kRapidosV2[i]));
  }
  return y1;
}

// HEROE-MAQ/creador: selector de vistas en UNA ficha bajo el pergamino (maqueta 1): "LT [Frente] Derecha Espalda Izquierda RT",
// la activa con aro dorado; en giro libre ninguna. La ficha mide al texto (no pasa de x 600, antes del panel). Las etiquetas
// salen de las tablas de idioma (kVistasV2). La pose (LB) pasa a la segunda barra de ayuda (DibujarCreadorV2).
void VistasV2(ImDrawList* dl, const S::Area& q) {
  const float h = 30, y = 174, x0 = 17, x1_max = 600;
  float w[4], gl = S::Glyph(nullptr, "LT", ImVec2(), h * q.s) / q.s, gr = S::Glyph(nullptr, "RT", ImVec2(), h * q.s) / q.s;
  float textos = 0;
  for (int k = 0; k < 4; ++k) textos += (w[k] = v2::Ancho(v2::kLetraPeque * q.s, T::T(kVistasV2[k][0], kVistasV2[k][1])) / q.s);
  // el hueco entre pestanas encoge hasta que todo cabe antes del panel (usuario 21/11: RT quedaba bajo el panel)
  const float fijo = 14 + gl + 10 + 10 + gr + 14;
  const float pad = std::clamp((x1_max - x0 - fijo - textos) / 8.0f, 4.0f, 16.0f);
  const float total = fijo + textos + 8 * pad;
  v2::Caja(dl, q, x0, y - 26, std::min(x0 + total, x1_max), y + 26);
  float x = x0 + 14;
  x += S::Glyph(dl, "LT", q.At(x, y), h * q.s) / q.s + 10;
  for (int k = 0; k < 4; ++k) {
    const bool act = k == g_vista_v2;
    if (act) dl->AddRect(q.At(x, y - 21), q.At(x + w[k] + 2 * pad, y + 21), IM_COL32(255, 214, 64, 255), 14 * q.s, 0, 2.5f * q.s);
    v2::Texto(dl, q, x + pad, y, v2::kLetraPeque, T::T(kVistasV2[k][0], kVistasV2[k][1]), 0, act ? 2 : 0);
    x += w[k] + 2 * pad;
  }
  S::Glyph(dl, "RT", q.At(x + 10, y), h * q.s);
}

bool DibujarRetratoCac2(ImDrawList* dl, const S::Area& q, bool lista, const char*& aviso) {
  const auto& h = Ficha();
  // Solo una textura de retrato y un trabajo enviado (en espera del turno UT o ejecutandose).
  static Tira textura;
  static std::string pedido, activo;
  static double cambio = 0, sondeo = 0;
  static bool en_disco = false;
  if (!lista || !EsFilaFotoCac2(g_fila) || !h.foto_pose) {
    // Al volver a la fila se estabiliza de nuevo; el trabajo ya enviado puede acabar en disco.
    if (!pedido.empty()) {
      pedido.clear();
      textura = {};
    }
    return false;
  }
  const std::string args = H::ArgsPropio();
  const std::string clave = ClaveFicha(args + ";cac2_foto=1");
  const auto dst = Vistas() / (clave + ".png");
  const double ahora = ImGui::GetTime();
  if (pedido != clave) {
    pedido = clave;
    cambio = sondeo = ahora;
    en_disco = false;
    textura = {};   // nunca mostrar el PNG de una seleccion anterior
  }
  const bool lista_antes = bool(textura.pic);
  if (const auto p = PicPng(textura, dst, ahora, 0.25)) {
    S::DrawPic(dl, p, q.At(136, 221), q.At(608, 693), ImVec4(0, 0, p.w, p.h));
    if (!lista_antes) REXLOG_INFO("dbz3 heroe: CAC2 retrato dibujado {} pose {} giro {}", clave, h.foto_pose, h.foto_yaw);
    return true;
  }
  const std::string trabajo = "ut_" + clave;
  if (ahora >= sondeo) {
    sondeo = ahora + 0.25;
    std::error_code ec;
    en_disco = fs::exists(dst, ec);
    // No encolar giros intermedios. Al terminar el activo se toma solo la seleccion vigente.
    if (!en_disco && !ec && ahora - cambio >= 0.4 && (activo.empty() || !EnMarcha(activo)) && !Generando2(trabajo)) {
      activo = trabajo;
      GenerarCon(trabajo, L"--retrato-cac2 --clave " + fs::path(clave).wstring(), dst, "vista_heroe.py", args);
      REXLOG_INFO("dbz3 heroe: CAC2 retrato pedido {} pose {} giro {}", clave, h.foto_pose, h.foto_yaw);
    }
  }
  aviso = en_disco || Fallida(trabajo) ? T::T("Retrato no disponible", "Portrait unavailable") :
      T::T("Preparando retrato…", "Preparing portrait…");
  return false;   // mantener FiguraV2 hasta que la textura se pueda dibujar
}

// Pantalla entera de Create / Edit con el aspecto nuevo. Mismos estados que el dibujo de siempre: nombre abierto, lista de
// recetas (Importar), editor de color o la lista.
void DibujarCreadorV2(ImDrawList* dl, const S::Area& a, float t) {
  const S::Area q = v2::Lienzo(a);
  v2::Titulo(dl, q, g_edit ? T::T("Editar héroe", "Edit hero") : T::T("Crear héroe", "Create hero"));
  // texto tecnico (piezas XV2 apagadas / sin XV2): al registro, una vez; ya no se pinta sobre el suelo
  if (!g_edit && (!H::Xv2Instalado() || !H::Xv2PiezasActivas())) {
    static bool dicho = false;
    if (!dicho) {
      dicho = true;
      REXLOG_INFO("dbz3 heroe: {}", H::Xv2Instalado() ? "piezas XV2 desactivadas: heroe base de B3" : "XV2 no instalado: heroe base de B3");
    }
  }
  // ranura, pequena arriba a la derecha (maqueta: "Ranura 1 de 1"; el nombre va en la placa)
  const std::string ranura = std::string(T::T("Ranura ", "Slot ")) + Num(C::SlotActivo() + 1) + T::T(" de ", " of ") + Num(C::NumSlots());
  v2::Pastilla(dl, q, v2::kPanX1, 22, ranura);
  const bool lista = !g_nom_abierto && !g_imp_abierto && g_panel < 0;
  {   // 30 px mas arriba: la peana queda encima de la placa (que con su nota empieza en y 800)
    S::Area qf = q;
    qf.p0.y -= 30 * q.s;
    qf.p1.y -= 30 * q.s;
    // HEROE-V05: en Edit el cuerpo entero como la maqueta 01 (el busto quedaba cortado a media pierna por la placa)
    const char* aviso = nullptr;
    if (!DibujarRetratoCac2(dl, qf, lista, aviso)) FiguraV2(dl, qf, !g_edit, true);
    if (aviso) v2::Texto(dl, q, v2::kFigCx, 228, v2::kLetraPeque, aviso, 1, 0);
  }
  if (lista) VistasV2(dl, q);
  // placa del nombre bajo los pies; la linea pequena: el aviso de XV2 componiendo o como saldra en combate
  const char* combate = TextoCombate();
  const std::string nota = Xv2() && HayGenerando()
                               ? std::string(T::T("Actualizando el modelo", "Updating the model")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')
                               : *combate ? std::string(combate) : (!Xv2() && !CuerpoUT() ? RasgoRazaTexto(true) : std::string());   // R23 HER-12: el rasgo de raza
  v2::Placa(dl, q, 88, 538, 892, H::NombreMostrado(),
            H::NombreCuerpo() + " - " + T::T("Traje ", "Costume ") + Num(H::Traje() + 1) + "/" + Num(H::NumTrajes()), nota);
  // panel de la derecha y la explicacion de una linea debajo
  float y1;
  if (g_nom_abierto || g_imp_abierto) {
    // nombre y recetas: el dibujo de siempre, movido al hueco del panel (su columna 440..1130 centrada en el panel)
    S::Area sub = q;
    sub.p0 = q.At((v2::kPanX0 + v2::kPanX1) * 0.5f - (kLx0 + kLx1) * 0.5f, v2::kPanY0 - kLy0);
    if (g_nom_abierto) NombreDibujo(dl, sub, t);
    else DibujarImportar(dl, sub, t);
    y1 = v2::kPanY0 + 2 * D::kPad + Paso() * float(Filas() - 1);
  } else if (g_panel >= 0) {
    y1 = ColorV2(dl, q, t);
  } else {
    y1 = ListaV2(dl, q, t);
  }
  // lineas que caben entre el panel y la barra de ayuda (1 con 10 filas; los textos largos de Exportar, hasta 3)
  const int lineas = std::clamp(int(((lista ? 769.0f : v2::kAyudaY - 36) - (y1 + 30) - 26) / 34), 1, 3);   // en la lista, sobre la segunda barra (y 769)
  if (!g_nom_abierto) v2::Explicacion(dl, q, v2::kPanX0, y1 + 22, v2::kPanX1, AyudaV2(), lineas);
  // barra de ayuda (maquetas 01 y 02); en la lista, los botones que no caben abajo van en una segunda linea discreta
  // arriba, a la izquierda de la ranura
  if (g_nom_abierto) {
    v2::BarraAyuda(dl, q, {{"A", T::T("Escribir", "Type")}, {"X", T::T("Borrar", "Delete")}, {"Y", T::T("Aceptar", "OK")}, {"B", T::T("Cancelar", "Cancel")}});
  } else if (g_panel >= 0) {
    if (ResetColoresCac2())
      v2::BarraAyuda(dl, q, {{"A", T::T("Aceptar", "OK")}, {"X", T::T("Original", "Original")},
                            {"Y", T::T("Restaurar colores", "Reset colors")}, {"B", T::T("Cancelar", "Cancel")}});
    else
    if (EsFilaEnt(g_panel))
      v2::BarraAyuda(dl, q, {{"A", T::T("Aceptar", "OK")}, {"X", T::T("Original", "Original")},
                            {"Y", T::T("Todo original", "Reset all")}, {"B", T::T("Cancelar", "Cancel")}});
    else
      v2::BarraAyuda(dl, q, {{"A", T::T("Aceptar", "OK")}, {"X", T::T("Original", "Original")}, {"B", T::T("Cancelar", "Cancel")}});
  } else if (g_imp_abierto) {
    v2::BarraAyuda(dl, q, {{"A", T::T("Importar", "Import")}, {"B", T::T("Cancelar", "Cancel")}});
  } else {
    const bool receta_forma = EsFilaFormaCac2(g_fila) && g_forma_cac2 && H::RecetaFormaCac2();
    v2::BarraAyuda(dl, q, {{"A", T::T("Cambiar", "Change")},
                           {"X", receta_forma ? T::T("Estallido", "Burst") : T::T("Nombre", "Name")},
                           {"Y", receta_forma ? T::T("Receta", "Recipe") : T::T("Exportar", "Export")},
                           {"RB", T::T("Confirmar", "Confirm")},
                           {"B", T::T("Atrás", "Back")}});
    // HEROE-MAQ/creador: segunda barra sobre la principal, mismo tamano y estilo (maqueta 1): pose, camara, editar pelo, reiniciar
    std::vector<std::pair<const char*, const char*>> otros;
    std::string pose;   // vive hasta el dibujo
    if (!g_tipos_v2.empty()) {
      const bool hay = std::find(g_tipos_v2.begin(), g_tipos_v2.end(), g_anim_v2) != g_tipos_v2.end();
      const int id_pose = hay ? g_anim_v2 : 0;
      pose = std::string(T::T("Pose", "Pose")) + ": " + (id_pose >= 0 && id_pose < 6
          ? std::string(T::T(kAnimsVista[id_pose][0], kAnimsVista[id_pose][1]))
          : std::to_string(id_pose));
      otros.push_back({"LB", pose.c_str()});
    }
    otros.push_back({"RS", T::T("Cámara", "Camera")});
    if (PestanasCac2()) otros.push_back({"BACK", T::T("Siguiente pestaña", "Next tab")});
    else if (!Xv2()) otros.push_back({"BACK", g_edit ? T::T("Crear", "Create") : T::T("Editar pelo", "Edit hair")});
    otros.push_back({"START", T::T("Reiniciar", "Reset")});
    if (REXCVAR_GET(dbz3_heroe_importar)) otros.push_back({"LS", T::T("Importar", "Import")});
    v2::BarraAyuda(dl, q, otros, 1585, 797);
  }
}

}  // namespace
