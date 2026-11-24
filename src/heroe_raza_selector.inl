// Panel compacto de informacion de raza para la ficha del Heroe.
// Incluido en heroe_ui.cpp, dentro de dbz3::heroe_ui.
namespace {
// La lista es deliberadamente conservadora: el archivo compartido solo contiene
// capsulas identificadas. Una lista vacia no se rellena con ataques inventados.
std::vector<int> CapsulasRazaSelector(int raza) {
  std::ifstream in(Juego() / "mod center hd" / "cac" / "razas_reglas.toml");
  std::vector<int> ids;
  std::string linea;
  int actual = -1;
  auto numeros = [&](const std::string& s) {
    const size_t a = s.find('['), b = s.find(']', a);
    if (a == std::string::npos || b == std::string::npos) return;
    std::istringstream entrada(s.substr(a + 1, b - a - 1));
    std::string n;
    while (std::getline(entrada, n, ',')) {
      try { ids.push_back(std::stoi(n)); } catch (...) {}
    }
  };
  while (std::getline(in, linea)) {
    const size_t p = linea.find_first_not_of(" \t");
    if (p == std::string::npos || linea[p] == '#') continue;
    linea.erase(0, p);
    if (linea.rfind("[[raza]]", 0) == 0) actual = -1;
    else if (linea.rfind("id =", 0) == 0) {
      try { actual = std::stoi(linea.substr(linea.find('=') + 1)); } catch (...) { actual = -1; }
    } else if (linea.rfind("tecnicas =", 0) == 0 && actual == raza) numeros(linea);
  }
  return ids;
}

std::vector<std::string> InfoRazaSelector() {
  const int raza = H::RazaEvolucion();
  std::vector<std::string> lineas;
  std::string formas = std::string(T::T("Formas: ", "Forms: ")) + T::T("Base", "Base");
  for (const auto& f : H::FormasDeRaza(raza)) {
    const std::string nombre = f.nombre;
    if (!nombre.empty()) formas += " / " + nombre;
  }
  lineas.push_back(formas);
  std::string tec = T::T("Técnicas propias: ", "Race techniques: ");
  const auto caps = CapsulasRazaSelector(raza);
  if (caps.empty()) tec += T::T("pendientes de identificar", "pending identification");
  for (size_t i = 0; i < caps.size(); ++i) {
    if (i) tec += ", ";
    tec += caps[i] == 6 ? "Kamehameha" : std::string(T::T("cápsula ", "capsule ")) + Num(caps[i]);
  }
  lineas.push_back(tec);
  const int cuerpo = H::Cuerpo();
  if (H::CuerpoXv2(cuerpo)) {
    for (int inicio : {0, 4}) {
      std::string s = inicio == 0 ? T::T("Piezas: ", "Parts: ") : "";
      for (int k = inicio; k < std::min(inicio + 4, 7); ++k) {
        if (k != inicio) s += "  ";
        s += std::string(H::NombreTipoXv2(k)) + " " + Num(int(H::PiezasXv2(cuerpo, k).size()));
      }
      lineas.push_back(s);
    }
  } else if (PiezasB3UI() && cuerpo == 0 && H::Traje() == 0) {
    std::string s = T::T("Piezas: ", "Parts: ");
    for (int k = 0; k < 4; ++k) {
      if (k) s += "  ";
      s += std::string(T::T(kB3ZonaEs[k], kB3ZonaEn[k])) + " " + Num(int(OpcionesB3(k).size()) + 1);
    }
    lineas.push_back(s);
  } else {
    lineas.push_back(T::T("Piezas: según el cuerpo elegido", "Parts: depend on the selected body"));
  }
  return lineas;
}

void PanelRazaSelector(ImDrawList* dl, const S::Area& q, const std::vector<std::string>& lineas) {
  // Ocupa el lugar de las barras del panel derecho, lejos del personaje.
  constexpr float x0 = 650, x1 = 1550, y0 = 492, y1 = 654;
  dl->AddRectFilled(q.At(x0, y0), q.At(x1, y1), IM_COL32(10, 27, 53, 250), 8 * q.s);
  dl->AddRect(q.At(x0, y0), q.At(x1, y1), IM_COL32(232, 150, 40, 240), 8 * q.s, 0, 2 * q.s);
  dl->PushClipRect(q.At(x0 + 8, y0 + 5), q.At(x1 - 8, y1 - 5), true);
  for (size_t i = 0; i < lineas.size() && i < 5; ++i)
    v2::Texto(dl, q, x0 + 18, y0 + 19 + 30 * float(i), 21, lineas[i], 0, 0, false, false, x1 - x0 - 36);
  dl->PopClipRect();
}
}  // namespace
