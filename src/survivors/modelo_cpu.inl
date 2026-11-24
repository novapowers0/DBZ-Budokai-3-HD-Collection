// Incluido en pasarela_nave.cpp: lector HMS2/HMS3 de LeerSk (pasarela_nave_rueda.inl),
// separado de la subida GPU. El trabajador solo posee su resultado; g_sk sigue en el dibujo.
SkB3 LeerSkCpu(const std::filesystem::path& ruta) {
  SkB3 m;
  std::ifstream in(ruta, std::ios::binary | std::ios::ate);
  const auto n = in.tellg();
  if (n < 20 || n > 128 * 1024 * 1024) return {};
  std::vector<uint8_t> d(static_cast<size_t>(n));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(d.data()), n)) return {};
  const bool hms3 = !std::memcmp(d.data(), "HMS3", 4);
  if (!hms3 && std::memcmp(d.data(), "HMS2", 4)) return {};
  size_t o = 4;
  const auto leer = [&](auto& v) {
    if (o + sizeof v > d.size()) return false;
    std::memcpy(&v, d.data() + o, sizeof v), o += sizeof v;
    return true;
  };
  uint32_t nt = 0, tri = 0, nb = 0;
  if (!leer(nt) || !leer(tri) || !leer(nb) || !leer(m.alto) || nt > 256 || tri > 200000 || nb == 0 || nb > 256) return {};
  m.imgs.resize(nt);
  for (auto& im : m.imgs) {
    uint32_t w = 0, h = 0;
    if (!leer(w) || !leer(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return {};
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + o, d.begin() + o + size_t(w) * h * 4);
    o += size_t(w) * h * 4;
  }
  for (uint32_t b = 0; b < nb; ++b) {
    uint32_t padre = 0, con = 0;
    ps2::M34 l;
    if (!leer(padre) || o + 24 > d.size()) return {};
    m.esq.padre.push_back(int(int32_t(padre)));
    // El horno recorre los padres en orden. Rechazar una cache que no lo respete.
    if (int32_t(padre) < -1 || int32_t(padre) >= int32_t(b)) return {};
    m.suf.emplace_back(reinterpret_cast<const char*>(d.data() + o), strnlen(reinterpret_cast<const char*>(d.data() + o), 24));
    o += 24;
    for (float& x : l.m) if (!leer(x)) return {};
    m.reposo.push_back(l);
    float g[9];
    if (!leer(con)) return {};
    for (float& x : g) if (!leer(x)) return {};
    if (con) for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) l.m[r * 4 + c] = g[r * 3 + c];
    m.esq.local.push_back(l);
  }
  m.malla.tris.resize(tri);
  for (auto& t : m.malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      uint32_t h = 0;
      for (float& x : v) if (!leer(x)) return {};
      if (!leer(h)) return {};
      t.p[c] = {v[0], v[1], v[2]}, t.n[c] = {v[3], v[4], v[5]}, t.uv[c] = ImVec2(v[6], v[7]);
      t.hueso[c] = uint8_t(std::min(h, nb - 1));
    }
    uint32_t tex = 0, fl = 0;
    if (!leer(tex) || !leer(fl)) return {};
    t.tex = int(tex), t.trans = (fl & 1) != 0;
  }
  uint32_t originales = 0;
  if (hms3 && leer(originales) && originales <= nt) {
    m.rampa.resize(originales);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t filas = 0;
      if (!leer(filas) || filas > 8) { bien = false; break; }
      for (uint32_t k = 0; k < filas && bien; ++k) {
        float luz = 0;
        uint32_t ti = 0;
        if (!leer(luz) || !leer(ti) || ti >= nt) bien = false;
        else fila.emplace_back(luz, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  const auto util = [&](uint8_t h) { return h < m.suf.size() && (m.suf[h] == "CAMERA" || m.suf[h] == "GAME"); };
  m.malla.tris.erase(std::remove_if(m.malla.tris.begin(), m.malla.tris.end(), [&](const auto& t) {
    return util(t.hueso[0]) || util(t.hueso[1]) || util(t.hueso[2]);
  }), m.malla.tris.end());
  if (m.alto <= 0 || m.malla.tris.empty()) return {};
  return m;
}

// Oraculo de prueba: contrastar con el lector original, incluidos UV, huesos, rampa y todas las imagenes.
// Solo se usa si se pide expresamente: LeerSk tambien sube texturas y no pertenece a la ruta normal.
bool VerificarSkCpu(const SkB3& m, int fid) {
  SkB3 r;
  if (!LeerSk(RutaSk(fid), r)) return false;
  if (m.alto != r.alto || m.suf != r.suf || m.esq.padre != r.esq.padre || m.rampa != r.rampa ||
      m.imgs.size() != r.imgs.size() || m.malla.tris.size() != r.malla.tris.size() || m.reposo.size() != r.reposo.size()) return false;
  for (size_t i = 0; i < m.imgs.size(); ++i)
    if (m.imgs[i].w != r.imgs[i].w || m.imgs[i].h != r.imgs[i].h || m.imgs[i].rgba != r.imgs[i].rgba) return false;
  for (size_t i = 0; i < m.reposo.size(); ++i)
    for (int j = 0; j < 12; ++j)
      if (m.reposo[i].m[j] != r.reposo[i].m[j] || m.esq.local[i].m[j] != r.esq.local[i].m[j]) return false;
  for (size_t i = 0; i < m.malla.tris.size(); ++i) {
    const auto &a = m.malla.tris[i], &b = r.malla.tris[i];
    if (a.tex != b.tex || a.trans != b.trans) return false;
    for (int c = 0; c < 3; ++c)
      if (a.hueso[c] != b.hueso[c] || a.p[c].x != b.p[c].x || a.p[c].y != b.p[c].y || a.p[c].z != b.p[c].z ||
          a.n[c].x != b.n[c].x || a.n[c].y != b.n[c].y || a.n[c].z != b.n[c].z || a.uv[c].x != b.uv[c].x || a.uv[c].y != b.uv[c].y) return false;
  }
  return true;
}

int SkEstadoCpu(int fid, const SkB3** sk, int cid) {
  if (fid < 0 || std::getenv("DBZ3_SEL_SIN_SK")) return 0;
  if (const auto it = g_sk.find(fid); it != g_sk.end()) {
    *sk = &it->second;
    return it->second.alto > 0 ? 2 : 0;
  }
  struct Pendiente { std::future<SkB3> hilo; SkB3 cpu; bool leido = false; };
  static std::map<int, Pendiente> pendientes;
  auto it = pendientes.find(fid);
  if (it == pendientes.end()) {
    {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      if (g_sk_fallo.count(fid)) return 0;
      if (g_sk_pidiendo.count(fid)) return 1;
    }
    std::error_code ec;
    if (!std::filesystem::exists(RutaSk(fid), ec)) return SkEstado(fid, sk, cid); // generacion externa, ya asincrona
    int activos = 0;
    for (auto& [id, p] : pendientes) if (p.hilo.valid() && p.hilo.wait_for(std::chrono::seconds(0)) != std::future_status::ready) ++activos;
    if (activos >= 8) return 1;   // SUR-08: 8 lecturas a la vez (eran 2): la carga fria tardaba 25 s
    auto& p = pendientes[fid];
    p.hilo = std::async(std::launch::async, [ruta = RutaSk(fid)] { return LeerSkCpu(ruta); });
    return 1;
  }
  auto& p = it->second;
  if (!p.leido) {
    if (p.hilo.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 1;
    p.cpu = p.hilo.get(), p.leido = true;
    const char* verificar = std::getenv("DBZ3_TEST_SURVIVORS_VERIFICAR_MODELOS");
    if (p.cpu.alto > 0 && verificar && verificar[0] == '1') {
      const bool ok = VerificarSkCpu(p.cpu, fid);
      REXLOG_INFO("dbz3 survivors: modelo verificado fid={} equivalente={}", fid, ok);
      if (!ok) p.cpu.alto = 0;
    }
    if (p.cpu.alto <= 0) {
      std::lock_guard<std::mutex> lk(g_modb3_mu);
      g_sk_fallo.insert(fid);
      pendientes.erase(it);
      REXLOG_ERROR("dbz3 survivors: modelo CPU fid {}: cache HMS2/HMS3 invalida", fid);
      return 0;
    }
  }
  // Una sola textura de modelo por cuadro, siempre desde el dibujante.
  static int cuadro_subida = -1, subidas = 0;   // SUR-08: hasta 3 texturas por cuadro (era 1)
  if (p.cpu.tx.size() < p.cpu.imgs.size()) {
    if (cuadro_subida != ImGui::GetFrameCount()) cuadro_subida = ImGui::GetFrameCount(), subidas = 0;
    if (subidas++ >= 4) return 1;
    auto tex = ps2::Crear(p.cpu.imgs[p.cpu.tx.size()], false);
    if (!tex) return 1;
    p.cpu.tx.push_back(std::move(tex));
    if (p.cpu.tx.size() < p.cpu.imgs.size()) return 1;
  }
  auto [publicado, nuevo] = g_sk.emplace(fid, std::move(p.cpu));
  pendientes.erase(it);
  *sk = &publicado->second;
  REXLOG_INFO("dbz3 survivors: modelo CPU fid {} listo ({} triangulos, {} huesos)", fid, (*sk)->malla.tris.size(), (*sk)->suf.size());
  return 2;
}
