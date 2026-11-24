// dbz3 - Modo Heroe, Tecnicas v2 (maqueta_6_tecnicas de E:\DBZ3HD_trabajo\heroe_rediseno, DISENO.md s9; ajuste
// dbz3_heroe_tecnicas_v2, APAGADO de serie: apagado, TecnicasUI dibuja la pantalla de siempre).
// Fila de 6 placas (Golpes, Especial E+derecha, Especial E+izquierda, Definitiva, Esquiva, Dragon Rush) con LB/RB; lista de
// cabeceras de carta (E equipada, NUEVO, "no disponible con este estilo", condicion de apertura); vista previa con
// "RT Probar en Entrenamiento"; panel de datos (ki, poder, hueco, forma, nivel); el heroe en vivo sobre la peana.
// Datos: heroe_tecnicas_v2.inl (H::TecnicasV2). Se incluye en heroe_ui.cpp justo antes de TecnicasUI (usa CabeceraCarta,
// PicPng, DonanteDe, g_tab, g_pregunta_cap y FiguraV2). Medidas en px del lienzo 1600x900 (= maqueta 1920x1080 x 0,8333).
namespace {

int g_tv_hueco = 3;   // placa activa: 0 Golpes, 1 E+der, 2 E+izq, 3 Definitiva, 4 Esquiva, 5 Dragon Rush
int g_tv_cur = 0, g_tv_scroll = 0;
bool g_tv_rt = false;
double g_tv_leido = -10;
std::vector<H::TecnicaV2> g_tv_todas;
std::string g_tv_aviso;
constexpr int kTvFilas = 7;
constexpr int kTvEquipo[6] = {-1, 1, 2, 0, 3, -1};   // placa -> g_equipo
const char* const kTvPlaca[6][2] = {{"Golpes", "Strikes"},   {"Especial E+ →", "Special E+ →"}, {"Especial E+ ←", "Special E+ ←"},
                                    {"Definitiva", "Ultimate"}, {"Esquiva", "Dodge"},            {"Dragon Rush", "Dragon Rush"}};

// Poder 1-5: tecnicas_dano.txt (T5, medido una vez por tecnica: "capsula poder") si existe; si no, estimado por el ki que gasta
// (+1 las definitivas). [H] la estimacion se marca con "≈" en la pantalla.
int PoderTec(const H::TecnicaV2& x, bool* medido) {
  static std::map<int, int> medidos;
  static bool leido = false;
  if (!leido) {
    leido = true;
    std::ifstream f(Juego() / "ingame_ui" / "heroe_vista" / "tecnicas_dano.txt");
    int c = 0, p = 0;
    while (f >> c >> p) medidos[c] = std::clamp(p, 1, 5);
  }
  if (const auto it = medidos.find(x.cap); it != medidos.end()) {
    *medido = true;
    return it->second;
  }
  *medido = false;
  return std::clamp((x.ki + 5) / 10 + (x.tipo == 0 ? 1 : 0), 1, 5);
}

std::string FormaTec(const H::TecnicaV2& x) {
  if (x.tipo == 3) return T::T("igual en todas", "same in all");
  if (x.cap == 139 && H::RazaEvolucion() != 0) return T::T("solo Saiyan", "Saiyan only");
  if (x.requiere) return T::T("pide transformación", "needs a transformation");
  const int n = __builtin_popcount(unsigned(x.formas & 0x3F));
  if (x.formas == 1) return T::T("solo forma normal", "base form only");
  if ((x.formas & 1) && n >= 5) return T::T("igual en todas", "same in all");
  return T::T("cambia con la forma", "changes with form");
}

std::string CondicionTec(const H::TecnicaV2& x) {
  if (x.escuela >= 0) return std::string(T::T("Escuela de ", "School of ")) + M::Nombre(x.escuela);
  if (x.maestro < 0) return T::T("Premio del camino", "Path reward");
  if (x.entreno <= 0) return std::string(T::T("Elige a ", "Pick ")) + M::Nombre(x.maestro) + T::T(" de maestro", " as master");
  return std::string(T::T("Se abre con ", "Unlocks with ")) + M::Nombre(x.maestro) + " " + Num(x.entreno) + " %";
}

// R23 HER-07: marco fino como el de las placas (el completo del kit es el doble de grueso que el de la maqueta 14)
void PanelFinoTv(ImDrawList* dl, const S::Area& q, float x0, float y0, float x1, float y1) { S::Frame(dl, q.At(x0, y0), q.At(x1, y1), q.s * 0.5f); }

bool CabeEnPlaca(const H::TecnicaV2& x, int placa) { return placa == 4 ? x.tipo == 3 : x.tipo >= 0 && x.tipo <= 2; }

// ponytail: nombres de las 4 capsulas de esquiva escritos a mano (en B3 son imagenes de data_usi 2663); leer el rotulo #AZT si
// se anaden mas por datos.
std::string NombreEsquiva(int cap) {
  switch (cap) {
    case 409: return "Gero's Deflection R&D";
    case 410: return "Gero's Deflect-Back R&D";
    case 444: return "Turtle Shell";
    case 445: return "Concentration";
    default: return std::string(T::T("Cápsula ", "Capsule ")) + Num(cap);
  }
}

// Lista de la placa activa: esquiva -> sus capsulas; si no, los ataques. Abiertas arriba, las del hueco activo, lo nuevo,
// luego las del estilo de ahora, por tipo y por ki.
std::vector<H::TecnicaV2> ListaTv() {
  std::vector<H::TecnicaV2> v;
  for (const H::TecnicaV2& x : g_tv_todas)
    if (CabeEnPlaca(x, g_tv_hueco)) {
      v.push_back(x);
      if (x.tipo != 3 && H::TecnicaActiva(x.cap)) v.back().donante = H::DonanteEfectivo();
    }
  const int ef = H::DonanteEfectivo(), tipo = kTvEquipo[g_tv_hueco];
  std::stable_sort(v.begin(), v.end(), [ef, tipo](const H::TecnicaV2& a, const H::TecnicaV2& b) {
    // R23 HER-07: primero las que puedes usar YA (abiertas y de tu estilo); las "no disponible con este estilo" despues
    const bool ua = a.abierta && (a.tipo == 3 || a.donante == ef), ub = b.abierta && (b.tipo == 3 || b.donante == ef);
    if (ua != ub) return ua;
    if (a.abierta != b.abierta) return a.abierta;
    if ((a.tipo == tipo) != (b.tipo == tipo)) return a.tipo == tipo;   // las del hueco activo arriba
    if (a.nueva != b.nueva) return a.nueva;
    if ((a.donante == ef) != (b.donante == ef)) return a.donante == ef;
    if (a.tipo != b.tipo) return a.tipo > b.tipo;   // E+der, E+izq, definitiva (como la fila de placas)
    return a.ki != b.ki ? a.ki < b.ki : a.cap < b.cap;
  });
  return v;
}

int DonanteTv(int cap) {
  const auto carta = std::find_if(g_tv_todas.begin(), g_tv_todas.end(), [cap](const H::TecnicaV2& x) { return x.cap == cap; });
  return carta != g_tv_todas.end() && carta->donante >= 0 ? carta->donante : DonanteDe(cap);
}

void ChipNuevo(ImDrawList* dl, const S::Area& q, float cx, float cy) {
  const std::string s = T::T("NUEVO", "NEW");
  const float w = v2::Ancho(18 * q.s, s) / q.s + 26;
  dl->AddRectFilled(q.At(cx - w * 0.5f, cy - 14), q.At(cx + w * 0.5f, cy + 14), IM_COL32(200, 60, 20, 240), 14 * q.s);
  dl->AddRect(q.At(cx - w * 0.5f, cy - 14), q.At(cx + w * 0.5f, cy + 14), IM_COL32(255, 210, 120, 255), 14 * q.s, 0, 2 * q.s);
  v2::Texto(dl, q, cx, cy, 18, s, 1, 0);
}

void Interrogante(ImDrawList* dl, const S::Area& q, float cx, float cy, float r) {   // el "?" naranja de las cosas por abrir
  dl->AddCircleFilled(q.At(cx, cy), (r + 3) * q.s, IM_COL32(60, 30, 0, 255), 32);
  dl->AddCircleFilled(q.At(cx, cy), r * q.s, IM_COL32(250, 190, 30, 255), 32);
  v2::Texto(dl, q, cx, cy, r * 1.5f, "?", 1, 2);
}

void Estrellas(ImDrawList* dl, const S::Area& q, float x, float y, int nivel) {
  for (int k = 0; k < 3; ++k) {
    const ImVec2 c = q.At(x + 30.0f * k, y);
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
      const float ang = -1.5707963f + i * 3.14159265f / 5, rr = (i % 2 ? 5.0f : 12.0f) * q.s;
      pts[i] = ImVec2(c.x + rr * std::cos(ang), c.y + rr * std::sin(ang));
    }
    if (k < nivel) dl->AddConcavePolyFilled(pts, 10, IM_COL32(250, 200, 40, 255));
    dl->AddPolyline(pts, 10, k < nivel ? IM_COL32(120, 70, 0, 255) : IM_COL32(250, 200, 40, 255), ImDrawFlags_Closed, 2 * q.s);
  }
}

// Probar en Entrenamiento (T4): la practica del laboratorio con el luchador del heroe y las capsulas de su combate.
void ProbarTv(const H::TecnicaV2* sel) {
  if (!sel || g_tv_hueco == 0 || g_tv_hueco == 5) {
    g_tv_aviso = T::T("Elige una técnica para probarla.", "Select a technique to try it.");
    return;
  }
  if (!sel->abierta) {
    g_tv_aviso = CondicionTec(*sel);
    return;
  }
  if (sel->tipo < 0 || sel->tipo > 3 || (sel->tipo != 3 && sel->donante >= 0 && sel->donante != H::DonanteEfectivo())) {
    g_tv_aviso = T::T("No disponible con este estilo.", "Not available with this style.");
    return;
  }
  if (REXCVAR_GET(dbz3_heroe_probar_aislada)) {
    // Sin un evento que identifique la capsula, ni gastar ki ni tener una sola capsula prueba que fue esta tecnica.
    if (sel->tipo == 3 || sel->requiere || !(sel->formas & 1) || !H::TecnicaActiva(sel->cap)) {
      g_tv_aviso = T::T("La prueba aislada requiere un ataque de la forma normal.", "Isolated practice requires a base-form attack.");
      return;
    }
    int st = 0;
    const int traje = H::TrajeCombate(&st);
    H::Reglas(H::EstiloId(), 1000 + H::Cuerpo(), traje);   // monta el cuerpo/moveset como en la ruta de siempre
    VistaTecnicaPedir(0);   // invalida peticiones antiguas de captura
    const bool ok = M::PracticaConCapsulas(H::Luchador(H::EstiloId()), {sel->cap});
    g_tv_aviso = ok ? T::T("Prueba aislada pedida. Vuelve con START > salir.", "Isolated practice requested. Return via START > quit.")
                    : T::T("Ahora no se puede abrir el Entrenamiento.", "Training can't open right now.");
    REXLOG_INFO("dbz3 heroe: prueba aislada capsula {} ({})", sel->cap, ok ? "pedido" : "no");
    return;
  }
  int st = 0;
  const int traje = H::TrajeCombate(&st);
  const M::Reglas g = H::Reglas(H::EstiloId(), 1000 + H::Cuerpo(), traje);
  std::vector<int> caps;
  for (int i = 0; i < std::max(g.ncaps, 0); ++i) caps.push_back(g.caps[i]);
  const int anterior = H::Equipada(sel->tipo);
  if (anterior > 0 && anterior != sel->cap) {
    const auto it = std::find(caps.begin(), caps.end(), anterior);
    if (it != caps.end()) caps.erase(it);
  }
  if (std::find(caps.begin(), caps.end(), sel->cap) == caps.end()) {
    if (caps.size() >= std::size(g.caps)) {
      // El equipo del jugador tiene prioridad también al probar una carta temporal.
      auto auxiliar = [](int cap) {
        for (int k = 0; k < 4; ++k)
          if (H::Equipada(k) == cap) return false;
        return true;
      };
      const auto it = std::find_if(caps.rbegin(), caps.rend(), auxiliar);
      if (it == caps.rend()) {
        g_tv_aviso = T::T("Sin espacio para probar esta técnica.", "No room to try this technique.");
        return;
      }
      caps.erase(std::prev(it.base()));
    }
    caps.push_back(sel->cap);
  }
  const int id = H::Luchador(H::EstiloId());
  const bool ok = M::PracticaConCapsulas(id, caps);
  g_tv_aviso = ok ? T::T("Entrenamiento pedido: sal con START > salir para volver.", "Training requested: quit from START to come back.")
                  : T::T("Ahora no se puede abrir el Entrenamiento.", "Training can't open right now.");
  REXLOG_INFO("dbz3 heroe: tecnicas v2: probar en entrenamiento: id {} con {} capsulas ({})", id, caps.size(), ok ? "ok" : "no");
  if (ok) VistaTecnicaPedir(sel->cap);   // R23 HER-07: la primera vez que la lances alli, su fotograma pasa a la vista previa
  REXLOG_INFO("dbz3 heroe: tecnicas v2: prueba temporal capsula {} sustituye {}", sel->cap, anterior);
}

Accion TecnicasV2UI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  AsegurarCartas();
  if (!H::TiposListos()) GenerarCon("tipos", L"", H::TiposRutaPublica(), "tecnicas_tipo.py");
  if (ahora - g_tv_leido > 1.0 || (p & (kA | kX | kLb | kRb))) {
    if (ahora - g_tv_leido > 3.0) {   // pantalla recien abierta
      g_tv_aviso.clear();   // R23 HER-07: sin restos del aviso de «Probar» al volver del Entrenamiento
      g_tv_todas = H::TecnicasV2();
      int abiertas = 0, esquivas = 0;
      for (const H::TecnicaV2& x : g_tv_todas) abiertas += x.abierta, esquivas += x.abierta && x.tipo == 3;
      REXLOG_INFO("dbz3 heroe: tecnicas v2: pantalla: {} tecnicas ({} abiertas, {} de esquiva); equipo {} {} {} {}", g_tv_todas.size(),
                  abiertas, esquivas, H::Equipada(0), H::Equipada(1), H::Equipada(2), H::Equipada(3));
    }
    g_tv_todas = H::TecnicasV2();
    g_tv_leido = ahora;
  }
  auto releerTiposActivos = [] {
    const int donante = H::DonanteEfectivo();
    for (H::TecnicaV2& x : g_tv_todas)
      if (x.tipo != 3 && H::TecnicaActiva(x.cap)) x.tipo = H::TipoTecnica(x.cap, donante);
  };
  if (g_tv_leido == ahora) releerTiposActivos();
  // entrada
  if (g_pregunta_cap > 0) {   // aviso Si/No de siempre: tecnica de otro donante
    if (p & (kA | kB)) {
      const int cap = g_pregunta_cap, don = DonanteTv(cap);
      const int anterior = H::MovesetEleccion();
      if (p & kA) H::PonerMoveset(don == H::EstiloId() ? -1 : don);
      const int tipo = H::TipoTecnica(cap, H::DonanteEfectivo());
      if ((p & kA) && tipo >= 0 && H::TecnicaActiva(cap) && H::TecnicaFormaBloqueada(cap)) {   // solo al aceptar (repaso dsREV)
        H::PonerMoveset(anterior);
        g_pregunta_cap = 0;
        g_tv_aviso = T::T("Te falta una forma compatible.", "You need a compatible form.");
        return Accion::kNada;
      }
      if (tipo >= 0) H::Equipar(tipo, cap);
      REXLOG_INFO("dbz3 heroe: tecnicas v2: capsula {} del donante {}: {}", cap, don, (p & kA) ? "moveset cambiado" : "no disponible con este estilo");
      g_tec_cambio = true;
      g_pregunta_cap = 0;
      g_tv_todas = H::TecnicasV2();
      releerTiposActivos();
    }
    p = 0;
  }
  if (p & kLb) g_tv_hueco = (g_tv_hueco + 5) % 6, g_tv_cur = g_tv_scroll = 0, g_tv_aviso.clear();
  if (p & kRb) g_tv_hueco = (g_tv_hueco + 1) % 6, g_tv_cur = g_tv_scroll = 0, g_tv_aviso.clear();
  const std::vector<H::TecnicaV2> lista = ListaTv();
  const int n = int(lista.size());
  if (n > 0) {
    if (p & kUp) g_tv_cur = (g_tv_cur + n - 1) % n, g_tv_aviso.clear();
    if (p & kDown) g_tv_cur = (g_tv_cur + 1) % n, g_tv_aviso.clear();
    g_tv_cur = std::clamp(g_tv_cur, 0, n - 1);
  }
  if (g_tv_cur < g_tv_scroll) g_tv_scroll = g_tv_cur;
  if (g_tv_cur >= g_tv_scroll + kTvFilas) g_tv_scroll = g_tv_cur - kTvFilas + 1;
  const H::TecnicaV2* sel = n > 0 ? &lista[size_t(g_tv_cur)] : nullptr;
  if (sel && (p & (kUp | kDown | kLb | kRb))) REXLOG_INFO("dbz3 heroe: tecnicas v2: cursor en la capsula {}", sel->cap);   // R23: para los guiones
  if (sel && sel->nueva) H::TecnicaVista(sel->cap);   // el NUEVO se quita al verla (la fila sigue en su sitio hasta releer)
  auto equipadaEn = [](int cap) {
    for (int k = 0; k < 4; ++k)
      if (H::Equipada(k) == cap) return k;
    return -1;
  };
  if ((p & kA) && (g_tv_hueco == 0)) {   // Golpes = el moveset (pestana de siempre)
    g_tab = 0;
    return Accion::kNada;
  }
  if ((p & kA) && sel && g_tv_hueco != 5) {
    if (!sel->abierta) {
      g_tv_aviso = CondicionTec(*sel);
    } else if (const int k = equipadaEn(sel->cap); k >= 0) {
      H::Equipar(k, 0);
      g_tec_cambio = true;
      REXLOG_INFO("dbz3 heroe: tecnicas v2: quitada la capsula {} del hueco {}", sel->cap, k + 1);
    } else if ((sel->donante == H::DonanteEfectivo() || sel->donante < 0) && H::TecnicaFormaBloqueada(sel->cap)) {
      g_tv_aviso = T::T("Te falta una forma compatible.", "You need a compatible form.");
    } else if (sel->tipo == 3 || sel->donante == H::DonanteEfectivo() || sel->donante < 0) {
      H::Equipar(sel->tipo, sel->cap);
      g_tec_cambio = true;
      for (int i = 0; i < 6; ++i)
        if (kTvEquipo[i] == sel->tipo) g_tv_hueco = i;
      REXLOG_INFO("dbz3 heroe: tecnicas v2: equipada la capsula {} (tipo {}) -> equipo {} {} {} {}", sel->cap, sel->tipo, H::Equipada(0),
                  H::Equipada(1), H::Equipada(2), H::Equipada(3));
    } else {
      g_pregunta_cap = sel->cap;
    }
    g_tv_todas = H::TecnicasV2();
    releerTiposActivos();
  }
  if ((p & kX) && kTvEquipo[g_tv_hueco] >= 0) {
    H::Equipar(kTvEquipo[g_tv_hueco], 0);
    g_tec_cambio = true;
  }
  if (p & kY) {
    g_tab = 0;
    return Accion::kNada;
  }
  {   // RT (teclado E): probar en el Entrenamiento
    const bool rt = GatillosV2() > 0 || ImGui::IsKeyPressed(ImGuiKey_E, false);
    if (rt && !g_tv_rt && g_pregunta_cap <= 0) ProbarTv(sel);
    g_tv_rt = GatillosV2() > 0;
  }
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: tecnicas v2: atras ({})", g_tec_cambio ? "con cambios" : "sin cambios");
    return g_tec_cambio ? Accion::kConfirmar : Accion::kVolver;
  }

  // ---- dibujo ----
  if (!v2::Fondo(dl, a, v2::kFondoFicha)) FondoSinAnfitrion(dl, a);
  const S::Area q = v2::Lienzo(a);
  v2::Titulo(dl, q, T::T("Técnicas", "Techniques"));
  const bool es = std::string(T::T("1", "0")) == "1";
  // selector de hueco (arriba a la derecha)
  {
    v2::Caja(dl, q, 900, 38, 1550, 92, 220);
    S::Glyph(dl, "LB", q.At(945, 65), 40 * q.s);
    S::Glyph(dl, "RB", q.At(1505, 65), 40 * q.s);
    S::Arrows(dl, q.At(985, 45), q.At(1465, 85), 34 * q.s, t);
    v2::Texto(dl, q, 1225, 65, v2::kLetraTitulo - 4,
              std::string(T::T("Hueco: ", "Slot: ")) + kTvPlaca[g_tv_hueco][es ? 0 : 1], 1, 0, false, false, 430);
  }
  // fila de 6 placas
  for (int i = 0; i < 6; ++i) {
    const float x0 = 54 + 254.0f * i, x1 = x0 + 229, y0 = 150, y1 = 249;
    const bool act = i == g_tv_hueco, rush = i == 5;
    // El marco completo sobresale 17 px por lado: en placas contiguas se pisa
    // con sus vecinas y con la lista. Como en Habitacion, usar el marco fino.
    S::Frame(dl, q.At(x0, y0), q.At(x1, y1), q.s * 0.42f);
    if (act) {
      v2::Franja(dl, q, x0 + 8, y0 + 8, x1 - 8, y1 - 8, t);
      dl->AddRect(q.At(x0 + 2, y0 + 2), q.At(x1 - 2, y1 - 2), IM_COL32(250, 205, 70, 255), 6 * q.s, 0, 3 * q.s);
    }
    v2::Texto(dl, q, x0 + 18, y0 + 27, 22, kTvPlaca[i][es ? 0 : 1], 0, rush ? 0 : 2, false, rush, x1 - x0 - 30);
    const int e = kTvEquipo[i];
    if (i == 0) {
      const int don = H::DonanteEfectivo();
      Cara(dl, q, don, x0 + 42, y0 + 66, 54);
      v2::Texto(dl, q, x0 + 78, y0 + 52, 19, T::T("Moveset de", "Moveset of"), 0, 0, false, false, x1 - x0 - 90);
      v2::Texto(dl, q, x0 + 78, y0 + 76, 21, M::Nombre(don), 0, 0, false, false, x1 - x0 - 90);
    } else if (rush) {
      dl->AddRectFilled(q.At(x0 + 6, y0 + 6), q.At(x1 - 6, y1 - 6), IM_COL32(40, 40, 40, 150), 6 * q.s);
      v2::Texto(dl, q, x0 + 22, y0 + 58, 26, T::T("de serie", "built in"), 0, 0, false, true, x1 - x0 - 30);
      v2::Texto(dl, q, x0 + 20, y0 + 84, 17, T::T("no se puede cambiar", "can't be changed"), 0, 0, false, true, x1 - x0 - 30);
    } else if (const int cap = H::Equipada(e); cap > 0) {
      if (e == 3) {   // la esquiva no tiene carta (no es un ataque): cabecera de texto
        v2::Texto(dl, q, x0 + 20, y0 + 66, 20, NombreEsquiva(cap), 0, 0, false, false, x1 - x0 - 34);
      } else {
        CabeceraCarta(dl, q, cap, x0 + 12, y0 + 38, x1 - x0 - 24, ahora, H::TecnicaActiva(cap) ? 255 : 120);
      }
    } else if (e == 3) {
      Interrogante(dl, q, x0 + 44, y0 + 68, 25);
      std::string cond;
      int mejor = 999;
      for (const H::TecnicaV2& x : g_tv_todas)
        if (x.tipo == 3 && !x.abierta && x.entreno < mejor) mejor = x.entreno, cond = CondicionTec(x);
      bool alguna = false;
      for (const H::TecnicaV2& x : g_tv_todas) alguna = alguna || (x.tipo == 3 && x.abierta);
      v2::Texto(dl, q, x0 + 80, y0 + 68, 19, alguna ? std::string(T::T("Libre: elige una", "Empty: pick one")) : cond, 0, 2, false, false,
                x1 - x0 - 90);
    } else {
      v2::Texto(dl, q, (x0 + x1) * 0.5f, y0 + 66, 22, T::T("libre", "empty"), 1, 0, false, true, x1 - x0 - 30);
    }
    if (e >= 0) {   // NUEVO en la placa que admite una tecnica nueva
      bool nueva = false;
      for (const H::TecnicaV2& x : g_tv_todas) nueva = nueva || (x.nueva && x.tipo == e);
      if (nueva) ChipNuevo(dl, q, x1 - 60, y0 - 4);
    }
  }
  // lista (izquierda)
  constexpr float lx0 = 38, lx1 = 512, ly0 = 264, ly1 = 808, paso = 74;
  PanelFinoTv(dl, q, lx0, ly0, lx1, ly1);
  if (n == 0)
    v2::Texto(dl, q, (lx0 + lx1) * 0.5f, (ly0 + ly1) * 0.5f, v2::kLetraPeque,
              g_tv_hueco == 4 ? T::T("Las esquivas se abren con los maestros.", "Dodges unlock with masters.")
                              : T::T("Sin técnicas todavía.", "No techniques yet."), 1, 0, false, true, 420);
  const int ef = H::DonanteEfectivo();
  for (int k = 0; k < kTvFilas; ++k) {
    const int i = g_tv_scroll + k;
    if (i >= n) break;
    const H::TecnicaV2& x = lista[size_t(i)];
    const float y0 = ly0 + 18 + paso * k, y1 = y0 + paso - 6;
    const bool s = i == g_tv_cur, apagada = !x.abierta || (x.tipo != 3 && x.donante != ef);
    if (s) {
      v2::Franja(dl, q, lx0 + 22, y0, lx1 - 22, y1, t);
      dl->AddRect(q.At(lx0 + 30, y0), q.At(lx1 - 30, y1), IM_COL32(250, 205, 70, 255), 8 * q.s, 0, 3 * q.s);
    }
    if (x.tipo == 3) {
      dl->AddRectFilled(q.At(lx0 + 36, y0 + 4), q.At(lx1 - 36, y1 - 4), IM_COL32(28, 74, 88, x.abierta ? 230 : 130), 8 * q.s);
      v2::Texto(dl, q, lx0 + 56, (y0 + y1) * 0.5f - 6, 26, NombreEsquiva(x.cap), 0, 0, false, !x.abierta, lx1 - lx0 - 110);
    } else {
      CabeceraCarta(dl, q, x.cap, lx0 + 36, y0 + 2, lx1 - lx0 - 72, ahora, apagada ? 110 : 255);
    }
    if (equipadaEn(x.cap) >= 0) v2::Texto(dl, q, lx1 - 50, y0 + 20, 26, "E", 1, 2);
    if (x.nueva && !apagada) ChipNuevo(dl, q, lx1 - 110, y0 + 20);   // R23 HER-07: NUEVO solo en las que puedes usar ya (el juez: saturaba la lista)   // R23 HER-07: arriba, junto a la "E" (abajo pisaba "no disponible...")
    if (!x.abierta)
      v2::Texto(dl, q, lx1 - 50, y1 - 16, 18, CondicionTec(x), 2, 2, false, false, 300);
    else if (x.tipo != 3 && x.donante != ef)
      v2::Texto(dl, q, lx1 - 50, y1 - 16, 17, T::T("no disponible con este estilo", "not available with this style"), 2, 0, false, false, 300);
  }
  if (g_tv_scroll > 0) S::Arrows(dl, q.At(lx0 + 180, ly0 - 26), q.At(lx0 + 300, ly0 + 4), 22 * q.s, t);
  // vista previa (centro): fotograma propio de la tecnica (vista_<cap>.png) o, si no lo hay, la carta entera
  constexpr float px0 = 546, px1 = 1088, py0 = 264, py1 = 567;
  PanelFinoTv(dl, q, px0, py0, px1, py1);
  if (sel) {
    const fs::path vista = Juego() / "ingame_ui" / "heroe_tecnicas" / ("vista_" + Num(sel->cap) + ".png");
    std::error_code ec;
    const S::Pic pic = fs::exists(vista, ec) ? PicPng(vista, ahora) : S::Pic{};   // R23: sin fotograma propio ya no se pinta la carta de texto entera (se desbordaba)
    if (pic) {
      const float mw = px1 - px0 - 24, mh = py1 - py0 - 82, sc = std::min(mw / pic.w, mh / pic.h);   // R23: llena el panel (maqueta 14)
      const float cx = (px0 + px1) * 0.5f, cy = py0 + 12 + mh * 0.5f;
      S::DrawPic(dl, pic, q.At(cx - pic.w * sc * 0.5f, cy - pic.h * sc * 0.5f), q.At(cx + pic.w * sc * 0.5f, cy + pic.h * sc * 0.5f),
                 ImVec4(0, 0, 0, 0), IM_COL32(255, 255, 255, sel->abierta ? 255 : 110));
    } else if (sel->tipo == 3) {
      Interrogante(dl, q, (px0 + px1) * 0.5f, (py0 + py1) * 0.5f - 20, 50);
    } else {   // sin fotograma: la cabecera de la carta en grande y la forma en que vale
      CabeceraCarta(dl, q, sel->cap, px0 + 40, py0 + 76, px1 - px0 - 80, ahora, sel->abierta ? 255 : 130);   // R23 HER-07: mas grande
      v2::Texto(dl, q, (px0 + px1) * 0.5f, py0 + 172, 24, FormaTec(*sel), 1, 0, false, !sel->abierta, px1 - px0 - 80);
      if (!sel->abierta || sel->escuela >= 0) v2::Texto(dl, q, (px0 + px1) * 0.5f, py0 + 206, 22, CondicionTec(*sel), 1, 2, false, false, px1 - px0 - 80);
      else v2::Texto(dl, q, (px0 + px1) * 0.5f, py0 + 206, 22, REXCVAR_GET(dbz3_heroe_probar_aislada)
                                      ? T::T("RT: prueba aislada sin captura.", "RT: isolated practice without capture.")
                                      : T::T("Pruébala con RT: su fotograma quedará aquí.", "Try it with RT: its frame will show here."), 1, 0, false, true, px1 - px0 - 80);
    }
    v2::Caja(dl, q, px0 + 14, py0 + 14, px0 + 162, py0 + 52, 210);
    v2::Texto(dl, q, px0 + 26, py0 + 33, 20, T::T("Vista previa", "Preview"), 0, 1, true);
    v2::Caja(dl, q, px0 + 14, py1 - 62, px1 - 14, py1 - 16, 220);
    S::Glyph(dl, "RT", q.At(px0 + 42, py1 - 39), 34 * q.s);
    v2::Texto(dl, q, px0 + 72, py1 - 39, 22, T::T("Probar carta (equipo temporal)", "Try card (temporary loadout)"), 0, 0, false, false, px1 - px0 - 100);
  }
  // panel de datos (abajo del centro)
  constexpr float dx0 = 557, dx1 = 1079, dy0 = 592, dy1 = 807;
  PanelFinoTv(dl, q, dx0, dy0, dx1, dy1);
  if (sel) {
    const float c1 = dx0 + 30, c2 = dx0 + 120;
    if (sel->tipo == 3) v2::Texto(dl, q, c1, dy0 + 30, 26, NombreEsquiva(sel->cap), 0, 2, false, false, 280);
    else CabeceraCarta(dl, q, sel->cap, c1 - 8, dy0 + 10, 270, ahora, 255);
    static const char* kTipoEs[4] = {"Definitiva (Hyper)", "Especial E+ →", "Especial E+ ←", "Esquiva (apoyo)"};
    static const char* kTipoEn[4] = {"Ultimate (Hyper)", "Special E+ →", "Special E+ ←", "Dodge (support)"};
    const int tp = std::clamp(sel->tipo, 0, 3);
    v2::Texto(dl, q, dx1 - 26, dy0 + 32, 22, es ? kTipoEs[tp] : kTipoEn[tp], 2, 0, false, false, 220);
    // Ki
    v2::Etiqueta(dl, q, c1, dy0 + 76, 24, "Ki", true);
    const int barras = (sel->ki + 5) / 10;
    if (barras == 0) {
      v2::Texto(dl, q, c2, dy0 + 76, 22, T::T("sin coste", "no cost"), 0, 0);
    } else {
      for (int k = 0; k < std::max(5, barras); ++k)
        v2::Orbe(dl, q, c2 + 14 + 33.0f * k, dy0 + 76, 14, k < barras ? IM_COL32(110, 190, 255, 255) : IM_COL32(40, 40, 46, 255));
      v2::Texto(dl, q, c2 + 33.0f * std::max(5, barras) + 18, dy0 + 76, 22,
                Num(barras) + (barras == 1 ? T::T(" barra", " bar") : T::T(" barras", " bars")), 0, 0);
    }
    // Poder
    v2::Etiqueta(dl, q, c1, dy0 + 111, 24, T::T("Poder", "Power"), true);
    if (sel->tipo == 3) {
      v2::Texto(dl, q, c2, dy0 + 111, 22, T::T("al esquivar o desviar", "when dodging or deflecting"), 0, 0, false, false, 330);
    } else {
      bool medido = false;
      const int pw = PoderTec(*sel, &medido);
      v2::BarraFina(dl, q, c2, dy0 + 111, c2 + 260, pw / 5.0f, 16);
      v2::Texto(dl, q, c2 + 280, dy0 + 111, 24, std::string(medido ? "" : "≈") + Num(pw) + "/5", 0, 2);
    }
    // Hueco y Forma
    v2::Etiqueta(dl, q, c1, dy0 + 146, 24, T::T("Hueco", "Slot"), true);
    v2::Texto(dl, q, c2, dy0 + 146, 20, kTvPlaca[sel->tipo == 3 ? 4 : sel->tipo == 0 ? 3 : std::clamp(sel->tipo, 1, 2)][es ? 0 : 1], 0, 0, false,
              false, 125);
    v2::Etiqueta(dl, q, c2 + 135, dy0 + 146, 24, T::T("Forma", "Form"), true);
    v2::Texto(dl, q, c2 + 212, dy0 + 146, 20, FormaTec(*sel), 0, 0, false, false, dx1 - c2 - 226);
    // Nivel
    v2::Etiqueta(dl, q, c1, dy0 + 180, 24, T::T("Nivel", "Level"), true);
    Estrellas(dl, q, c2 + 12, dy0 + 180, sel->abierta ? sel->nivel : 0);
    std::string falta;
    if (!sel->abierta) falta = CondicionTec(*sel);
    else if (sel->nivel < 3)
      falta = Num((sel->nivel == 1 ? 5 : 15) - sel->victorias) + T::T(" victorias más para Nv ", " more wins for Lv ") + Num(sel->nivel + 1);
    else falta = T::T("Nivel máximo", "Max level");
    v2::Texto(dl, q, dx1 - 26, dy0 + 180, 20, falta, 2, 0, false, false, 330);
  }
  // el heroe en vivo sobre la peana (derecha), sin nada encima; chip del moveset bajo los pies
  {
    constexpr float k = 0.74f, cx = 1342, pies = 725;
    S::Area qf = q;
    qf.s = q.s * k;
    qf.p0 = q.At(cx - v2::kFigCx * k, pies - v2::kFigPies * k);
    qf.p1 = ImVec2(qf.p0.x + 1600.0f * qf.s, qf.p0.y + 900.0f * qf.s);
    FiguraV2(dl, qf);
    v2::Caja(dl, q, 1175, 760, 1508, 802, 220);
    Cara(dl, q, ef, 1200, 781, 34);
    v2::Texto(dl, q, 1226, 781, 24, std::string(T::T("Moveset: ", "Moveset: ")) + M::Nombre(ef), 0, 0, false, false, 270);
  }
  if (!g_tv_aviso.empty()) v2::Explicacion(dl, q, 546, 812, 1088, g_tv_aviso, 1);
  if (g_pregunta_cap > 0) {
    const int don = DonanteTv(g_pregunta_cap);
    v2::Panel(dl, q, 450, 330, 1150, 560);
    v2::Texto(dl, q, 800, 380, v2::kLetraFila, std::string(T::T("Esta técnica es de ", "This technique belongs to ")) + M::Nombre(don), 1, 2,
              false, false, 640);
    v2::Texto(dl, q, 800, 430, v2::kLetraPeque, T::T("¿Cambiar tu moveset a ese estilo?", "Switch your moveset to that style?"), 1, 0, false,
              false, 640);
    v2::Texto(dl, q, 800, 474, v2::kLetraMin,
              T::T("Si no, queda equipada pero no disponible con este estilo.", "If not, it stays equipped but unavailable with this style."), 1, 0,
              false, true, 640);
    S::HelpLine(dl, q.At(800, 522), 36 * q.s, {{"A", T::T("Sí", "Yes")}, {"B", T::T("No", "No")}});
  }
  v2::BarraAyuda(dl, q,
                 {{"A", T::T("Equipar/Quitar", "Equip/Remove")}, {"X", T::T("Vaciar", "Clear")}, {"RB", T::T("Hueco", "Slot")},
                  {"Y", T::T("Moveset", "Moveset")}, {"RT", T::T("Probar", "Try")}, {"B", T::T("Volver", "Back")}},
                 1229, 856);
  return Accion::kNada;
}

}  // namespace
