// CAC-17: ventana local de pintura de la textura nativa elegida y materiales regionales.
// Los lienzos son versiones inmutables. Descartar la ficha recupera las referencias anteriores.
struct Pintura17 {
  bool abierta = false, cargada = false, repintar = false, cambiado = false, relleno = false;
  int cuerpo = 0, traje = 0, tex = -1, w = 0, h = 0, zona = 0, radio = 2, previo = -1;
  int valores[6][4] = {};
  float rgb[3] = {1.f, .4f, .1f};
  std::string clave, huella, error;
  std::map<int, std::string> archivos;
  std::vector<uint8_t> base, mascara, capa, visible;
  std::vector<std::vector<uint8_t>> deshacer, rehacer;
  std::unique_ptr<rex::ui::ImmediateTexture> gpu;
} g_p17;

fs::path Carpeta17() { return dbz3::settings::UserDataRoot() / "cac_paint"; }

void AbrirMateriales17(const C::Heroe& h) {
  auto& s = g_p17;
  s = {};
  s.abierta = true;
  s.cuerpo = H::Cuerpo(); s.traje = H::Traje();
  const auto& ts = TcLista();
  if (!ts.empty()) {
    const auto& t = ts[std::min(g_tc_tex, int(ts.size()) - 1)];
    if (t.w > 0 && t.h > 0 && t.w <= 256 && t.h <= 256) {
      s.tex = t.tex; s.w = t.w; s.h = t.h;
      const auto a = Num(s.cuerpo) + "_" + Num(s.traje) + "_" + Num(s.tex);
      s.clave = "p17_" + a;
      if (!fs::exists(Vistas() / (s.clave + ".p17")) && !Generando2(s.clave))
        GenerarCon(s.clave, L"--lienzo17 " + fs::path(a).wstring(), Vistas() / (s.clave + ".p17"));
    }
  }
  for (size_t k = 0; k < h.material17.size() && k < 128;) {
    const size_t fin = std::min(h.material17.find('|', k), h.material17.size());
    int z, r, g, b, l;
    if (std::sscanf(h.material17.substr(k, fin - k).c_str(), "%d,%d,%d,%d,%d", &z, &r, &g, &b, &l) == 5 && z >= 0 && z < 6) {
      s.valores[z][0] = std::clamp(r, 0, 255); s.valores[z][1] = std::clamp(g, 0, 255);
      s.valores[z][2] = std::clamp(b, 0, 255); s.valores[z][3] = std::clamp(l, 0, 3);
    }
    k = fin + 1;
  }
  for (size_t k = 0; k < h.pintura17.size() && k < 320 && s.archivos.size() < 8;) {
    const size_t fin = std::min(h.pintura17.find('|', k), h.pintura17.size());
    int ti, pos = 0;
    const auto item = h.pintura17.substr(k, fin - k);
    if (std::sscanf(item.c_str(), "%d,%n", &ti, &pos) == 1 && pos && ti >= 0 && ti <= 9999) {
      const auto id = item.substr(pos);
      if (id.size() >= 16 && id.size() <= 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos)
        s.archivos[ti] = id;
    }
    k = fin + 1;
  }
}

void LeerMateriales17(Pintura17& s) {
  if (s.cargada || s.tex < 0) return;
  std::ifstream f(Vistas() / (s.clave + ".p17"), std::ios::binary);
  if (!f) return;
  const size_t n = size_t(s.w) * s.h;
  std::string huella(64, '\0');
  std::vector<uint8_t> img(n * 4), mask(n);
  if (!f.read(huella.data(), 64) || !f.read(reinterpret_cast<char*>(img.data()), img.size()) ||
      !f.read(reinterpret_cast<char*>(mask.data()), mask.size()) || f.peek() != EOF) return;
  s.huella = std::move(huella); s.base = std::move(img); s.mascara = std::move(mask);
  s.capa.assign(n * 4, 0);
  if (const auto a = s.archivos.find(s.tex); a != s.archivos.end()) {
    std::ifstream p(Carpeta17() / (a->second + ".rgba"), std::ios::binary);
    std::string otra(64, '\0');
    if (p.read(otra.data(), 64) && otra == s.huella) {
      std::vector<uint8_t> buf(n * 4);
      if (p.read(reinterpret_cast<char*>(buf.data()), buf.size()) && p.peek() == EOF) s.capa = std::move(buf);
    }
  }
  s.cargada = s.repintar = true;
}

void PasoMateriales17(Pintura17& s) {
  if (s.deshacer.size() == 12) s.deshacer.erase(s.deshacer.begin());
  s.deshacer.push_back(s.capa); s.rehacer.clear();
  s.cambiado = s.repintar = true;
}

bool GuardarMateriales17(C::Heroe& h, Pintura17& s) {
  if (s.cambiado) {
    bool tinta = false;
    for (size_t k = 3; k < s.capa.size(); k += 4) tinta |= s.capa[k] != 0;
    if (!tinta) s.archivos.erase(s.tex);
    else {
      if (s.archivos.size() == 8 && !s.archivos.count(s.tex)) {
        s.error = T::T("Máximo de ocho texturas pintadas.", "Up to eight painted textures.");
        return false;
      }
      std::error_code ec;
      fs::create_directories(Carpeta17(), ec);
      if (ec) return false;
      static unsigned secuencia = 0;
      char id[32];
      std::snprintf(id, sizeof id, "%016llx%08x", static_cast<unsigned long long>(
          std::chrono::system_clock::now().time_since_epoch().count()), ++secuencia);
      const auto dst = Carpeta17() / (std::string(id) + ".rgba");
      const auto tmp = Carpeta17() / (std::string(id) + ".tmp");
      if (fs::exists(dst, ec) || ec || fs::exists(tmp, ec) || ec) return false;
      std::ofstream f(tmp, std::ios::binary);
      f.write(s.huella.data(), 64);
      f.write(reinterpret_cast<const char*>(s.capa.data()), s.capa.size());
      f.close();
      if (!f) return false;
      fs::rename(tmp, dst, ec);
      if (ec) return false;
      s.archivos[s.tex] = id;
    }
  }
  h.material17.clear(); h.pintura17.clear();
  for (int i = 0; i < 6; ++i) {
    const auto& v = s.valores[i];
    if (v[0] || v[1] || v[2] || v[3]) {
      if (!h.material17.empty()) h.material17 += '|';
      h.material17 += Num(i) + ',' + Num(v[0]) + ',' + Num(v[1]) + ',' + Num(v[2]) + ',' + Num(v[3]);
    }
  }
  for (const auto& [ti, id] : s.archivos) {
    if (!h.pintura17.empty()) h.pintura17 += '|';
    h.pintura17 += Num(ti) + ',' + id;
  }
  return true;
}

void Trazo17(Pintura17& s, int i, bool inicio) {
  uint8_t c[4] = {uint8_t(s.rgb[0] * 255 + .5f), uint8_t(s.rgb[1] * 255 + .5f), uint8_t(s.rgb[2] * 255 + .5f), 255};
  auto pinta = [&](int j) { std::copy_n(c, 4, &s.capa[4*j]); };
  if (s.relleno) {
    if (!inicio || !s.mascara[i]) return;
    std::vector<int> cola{ i };
    auto vistos = s.mascara;
    vistos[i] = 0;
    auto meter = [&](int j) { if (vistos[j]) { vistos[j] = 0; cola.push_back(j); } };
    for (size_t n = 0; n < cola.size(); ++n) {
      const int j = cola[n]; pinta(j);
      if (j % s.w) meter(j - 1);
      if (j % s.w + 1 < s.w) meter(j + 1);
      if (j >= s.w) meter(j - s.w);
      if (j + s.w < s.w*s.h) meter(j + s.w);
    }
  } else {
    const int ant = s.previo < 0 ? i : s.previo, dx = i % s.w - ant % s.w, dy = i / s.w - ant / s.w;
    const int n = std::max(1, std::max(std::abs(dx), std::abs(dy)));
    for (int k = 0; k <= n; ++k) {
      const int x = ant % s.w + dx*k/n, y = ant / s.w + dy*k/n;
      for (int v = std::max(0, y-s.radio+1); v < std::min(s.h, y+s.radio); ++v)
        for (int u = std::max(0, x-s.radio+1); u < std::min(s.w, x+s.radio); ++u)
          if (s.mascara[v*s.w+u] && (u-x)*(u-x)+(v-y)*(v-y) < s.radio*s.radio) pinta(v*s.w+u);
    }
  }
  s.previo = i; s.repintar = true;
}

void EditarMateriales17(C::Heroe& h, uint16_t p) {
  static std::unique_ptr<rex::ui::ImmediateTexture> ultima;
  ultima.reset();  // la ventana ya no referencia la textura de una sesión anterior
  auto& s = g_p17;
  if ((p & kB) || ImGui::IsKeyPressed(ImGuiKey_Escape) || H::Cuerpo() != s.cuerpo || H::Traje() != s.traje) { s = {}; return; }
  LeerMateriales17(s);
  ImGui::SetNextWindowSize(ImVec2(570, 740), ImGuiCond_FirstUseEver);
  bool mostrar = true, aplicar = false;
  if (ImGui::Begin(T::T("Materiales del héroe###cac17", "Hero materials###cac17"), &mostrar, ImGuiWindowFlags_NoSavedSettings)) {
    const char* zonas[] = {T::T("Pelo", "Hair"), T::T("Cara", "Face"), T::T("Torso", "Torso"), T::T("Piernas", "Legs"),
                            T::T("Guantes / brazos", "Gloves / arms"), T::T("Botas", "Boots")};
    ImGui::Combo(T::T("Región", "Region"), &s.zona, zonas, 6);
    auto& v = s.valores[s.zona];
    float sombra[3] = {v[0]/255.f, v[1]/255.f, v[2]/255.f};
    if (ImGui::ColorEdit3(T::T("Color de sombra", "Shadow color"), sombra, ImGuiColorEditFlags_InputRGB))
      for (int i = 0; i < 3; ++i) v[i] = int(sombra[i]*255 + .5f);
    if (ImGui::Button(T::T("Sombra original", "Original shadow"))) v[0] = v[1] = v[2] = 0;
    ImGui::SliderInt(T::T("Brillo (solo forma SSJ)", "Glow (SSJ form only)"), &v[3], 0, 3);
    ImGui::Separator();
    if (s.cargada) {
      ImGui::ColorEdit3(T::T("Pintura", "Paint"), s.rgb, ImGuiColorEditFlags_InputRGB);
      ImGui::Checkbox(T::T("Rellenar isla", "Fill island"), &s.relleno);
      ImGui::SliderInt(T::T("Radio del pincel", "Brush radius"), &s.radio, 1, 16);
      if (ImGui::Button(T::T("Deshacer", "Undo")) && !s.deshacer.empty()) {
        s.rehacer.push_back(std::move(s.capa)); s.capa = std::move(s.deshacer.back()); s.deshacer.pop_back(); s.repintar = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(T::T("Rehacer", "Redo")) && !s.rehacer.empty()) {
        s.deshacer.push_back(std::move(s.capa)); s.capa = std::move(s.rehacer.back()); s.rehacer.pop_back(); s.repintar = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(T::T("Borrar pintura", "Clear paint"))) { PasoMateriales17(s); std::fill(s.capa.begin(), s.capa.end(), 0); }
      ImGui::TextUnformatted(T::T("Clic derecho: pipeta", "Right-click: eyedropper"));
      if (s.repintar) {
        s.visible = s.base;
        for (size_t j = 0; j < s.mascara.size(); ++j)
          if (s.mascara[j] && s.capa[j*4+3]) std::copy_n(&s.capa[j*4], 3, &s.visible[j*4]);
        if (auto* d = dbz3::launcher::PreviewDrawer()) {
          s.gpu = d->CreateTexture(uint32_t(s.w), uint32_t(s.h), rex::ui::ImmediateTextureFilter::kNearest, false, s.visible.data());
          s.repintar = !s.gpu;
        }
      }
      if (s.gpu) {
        const float k = std::min(512.f, std::max(64.f, ImGui::GetContentRegionAvail().x)) / std::max(s.w, s.h);
        ImGui::Image(reinterpret_cast<ImTextureID>(s.gpu.get()), ImVec2(s.w*k, s.h*k));
        const auto xy = ImGui::GetItemRectMin(), m = ImGui::GetMousePos();
        const float u = (m.x-xy.x)/k, v = (m.y-xy.y)/k;
        if (ImGui::IsMousePosValid(&m) && ImGui::IsItemHovered() && u >= 0 && v >= 0 && u < s.w && v < s.h) {
          const int x = int(u), y = int(v);
          const int i = y*s.w+x;
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            for (int j = 0; j < 3; ++j) s.rgb[j] = s.visible[4*i+j] / 255.f;
          const bool inicio = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
          if (inicio && s.mascara[i]) PasoMateriales17(s);
          if ((inicio && s.mascara[i]) || (s.previo >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))) Trazo17(s, i, inicio);
        } else s.previo = -1;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.previo = -1;
      }
    } else ImGui::TextUnformatted(s.tex < 0 || Fallida(s.clave) ? T::T("Textura no editable", "Texture not editable") :
                                 T::T("Cargando textura…", "Loading texture…"));
    aplicar = ImGui::Button(T::T("Aplicar", "Apply"));
    ImGui::SameLine();
    if (ImGui::Button(T::T("Cancelar", "Cancel"))) mostrar = false;
    if (!s.error.empty()) ImGui::TextUnformatted(s.error.c_str());
  }
  ImGui::End();
  if (!mostrar) { ultima = std::move(s.gpu); s = {}; }
  else if (aplicar) {
    if (GuardarMateriales17(h, s)) {
      // La textura usada por ImGui en ESTE frame debe vivir hasta que el renderer haya consumido sus comandos.
      ultima = std::move(s.gpu);
      s = {};
    } else if (s.error.empty()) s.error = T::T("No se pudo guardar la pintura.", "Could not save the paint.");
  }
}
