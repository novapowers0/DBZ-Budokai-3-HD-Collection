// B2: lector de recursos originales de transición (B2TZ v1/v2).
// Cámara y trayectoria son recursos distintos aunque compartan formato ACC.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace dbz3::escenarios::b2 {
struct Vista {
  const uint8_t* p = nullptr;
  size_t n = 0;
  bool Rango(size_t off, size_t tam) const { return off <= n && tam <= n - off; }
  uint32_t U(size_t off, bool be = false) const {
    if (!Rango(off, 4)) return 0;
    if (be) return (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                   (uint32_t(p[off + 2]) << 8) | uint32_t(p[off + 3]);
    return uint32_t(p[off]) | (uint32_t(p[off + 1]) << 8) |
           (uint32_t(p[off + 2]) << 16) | (uint32_t(p[off + 3]) << 24);
  }
  float F(size_t off) const {
    const uint32_t v = U(off, true);
    float f = 0;
    std::memcpy(&f, &v, 4);
    return f;
  }
};

struct Clave {
  uint32_t cuadro = 0;
  std::array<float, 3> valor{};
};
struct Clip {
  uint32_t cuadros = 0, tipo = 0;
  std::array<std::vector<Clave>, 4> pistas;
};
struct Recurso {
  uint32_t grupo = 0, version = 0;
  std::vector<uint8_t> datos;
  std::array<uint32_t, 13> off{}, tam{};
  std::vector<Clip> camara, trayectoria;
  Vista Bloque(size_t i) const {
    if (i >= off.size() || off[i] > datos.size() || tam[i] > datos.size() - off[i]) return {};
    return {datos.data() + off[i], tam[i]};
  }
};

inline uint32_t Crc32(Vista b) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < b.n; ++i) {
    crc ^= b.p[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
  }
  return crc ^ 0xFFFFFFFFu;
}

inline bool LeerAcc(Vista b, std::vector<Clip>& salida) {
  if (!b.Rango(0, 32) || std::memcmp(b.p, "#ACC", 4)) return false;
  const uint32_t n = b.U(16, true), t = b.U(20, true);
  if (!n || n > 64 || t < 32 || !b.Rango(t, size_t(n) * 16)) return false;
  std::vector<Clip> clips;
  for (uint32_t i = 0; i < n; ++i) {
    Clip c;
    c.tipo = b.U(t + 16 * i, true);
    c.cuadros = b.U(t + 16 * i + 8, true);
    const uint32_t d = b.U(t + 16 * i + 12, true);
    const uint32_t np = (c.tipo & 0x10u) ? 4 : 3;
    if ((c.tipo != 0x0Du && c.tipo != 0x1Du) || !c.cuadros || c.cuadros > 10000 ||
        size_t(d) < size_t(t) + size_t(n) * 16 || !b.Rango(d, np * 4)) return false;
    for (uint32_t j = 0; j < np; ++j) {
      const uint32_t p = b.U(d + 4 * j, true);
      if (!p || !b.Rango(p, 12)) return false;
      const uint32_t nk = b.U(p + 8, true), paso = j < 2 ? 16 : 8;
      if (!nk || nk > 20000 || !b.Rango(size_t(p) + 12, size_t(nk) * paso)) return false;
      for (uint32_t k = 0; k < nk; ++k) {
        const size_t q = size_t(p) + 12 + size_t(k) * paso;
        Clave clave;
        clave.cuadro = b.U(q, true);
        if (clave.cuadro >= c.cuadros || (k && clave.cuadro <= c.pistas[j].back().cuadro)) return false;
        for (uint32_t comp = 0; comp < (j < 2 ? 3u : 1u); ++comp) {
          clave.valor[comp] = b.F(q + 4 + 4 * comp);
          if (!std::isfinite(clave.valor[comp])) return false;
        }
        c.pistas[j].push_back(clave);
      }
    }
    clips.push_back(std::move(c));
  }
  salida = std::move(clips);
  return true;
}

// Devuelve posición, objetivo, giro y FOV del recurso, sin convertirlos en
// coordenadas del luchador: FUN_001F3D90 resta después el hueso transformado.
// Cámara y trayectoria usan el mismo esquema de pistas en B2.
inline bool Muestrear(const Clip& c, float tiempo, std::array<std::array<float, 3>, 4>& v) {
  if (!c.cuadros || !std::isfinite(tiempo)) return false;
  const float f = std::clamp(tiempo, 0.0f, float(c.cuadros - 1));
  v = {};
  for (size_t j = 0; j < c.pistas.size(); ++j) {
    const auto& pista = c.pistas[j];
    if (pista.empty()) {
      if (j == 3 && !(c.tipo & 0x10u)) continue;
      return false;
    }
    const auto it = std::upper_bound(pista.begin(), pista.end(), f,
                                    [](float t, const Clave& k) { return t < float(k.cuadro); });
    const auto& a = it == pista.begin() ? pista.front() : *(it - 1);
    if (it == pista.end() || it == pista.begin()) v[j] = a.valor;
    else {
      const float u = (f - float(a.cuadro)) / float(it->cuadro - a.cuadro);
      for (size_t k = 0; k < v[j].size(); ++k) v[j][k] = a.valor[k] * (1.0f - u) + it->valor[k] * u;
    }
  }
  return true;
}

inline bool Leer(std::vector<uint8_t> datos, Recurso& salida, std::string& error) {
  auto fallo = [&](const char* causa) { error = causa; return false; };
  const Vista b{datos.data(), datos.size()};
  if (!b.Rango(0, 64) || datos.size() > 64u * 1024u * 1024u || std::memcmp(b.p, "B2TZ", 4))
    return fallo("cabecera B2TZ inválida");
  const uint32_t version = b.U(4), componentes = b.U(12);
  if (!((version == 1 && componentes == 5) || (version == 2 && (componentes == 7 || componentes == 8 || componentes == 10 || componentes == 11 || componentes == 12 || componentes == 13))) || b.U(16) != 64 || b.U(20) != datos.size())
    return fallo("versión/tamaño B2TZ inválidos");
  for (size_t i = 24; i < 64; ++i) if (b.p[i]) return fallo("reservado B2TZ no nulo");
  Recurso r;
  r.version = version;
  r.grupo = b.U(8);
  if (r.grupo != 0 && r.grupo != 2 && r.grupo != 4 && r.grupo != 7 && r.grupo != 8) return fallo("grupo B2TZ inválido");
  constexpr char tags[13][8] = {"CAMR", "TRAY", "SPX0", "MDD0", "FX00", "MAD0", "MAS0", "SND0", "ANM0", "ANM1", "FXHD", "BSK0", "ENV0"};
  size_t fin = (64 + 24 * componentes + 31) & ~size_t(31);
  for (size_t i = 0; i < componentes; ++i) {
    const size_t q = 64 + 24 * i;
    if (!b.Rango(q, 24) || std::memcmp(b.p + q, tags[i], 8) || b.U(q + 20)) return fallo("tabla B2TZ inválida");
    r.off[i] = b.U(q + 8);
    r.tam[i] = b.U(q + 12);
    if (!r.tam[i] || r.off[i] % 32 || r.off[i] < fin || !b.Rango(r.off[i], r.tam[i]))
      return fallo("bloques B2TZ solapados/truncados");
    if (Crc32({b.p + r.off[i], r.tam[i]}) != b.U(q + 16)) return fallo("CRC B2TZ incorrecto");
    fin = size_t(r.off[i]) + r.tam[i];
  }
  r.datos = std::move(datos);
  if (!LeerAcc(r.Bloque(0), r.camara) || !LeerAcc(r.Bloque(1), r.trayectoria)) return fallo("ACC B2TZ inválido");
  constexpr char magics[11][5] = {"#SPX", "#MDD", "#AMB", "#MAD", "#MAS", "#B2S", "#ACM", "#ACM", "#AMB", "#BSK", "#B2E"};
  for (size_t i = 2; i < componentes; ++i) {
    const Vista v = r.Bloque(i);
    if (!v.Rango(0, 4) || std::memcmp(v.p, magics[i - 2], 4)) return fallo("componente B2TZ inválido");
  }
  salida = std::move(r);
  error.clear();
  return true;
}

inline bool Cargar(const std::filesystem::path& ruta, Recurso& salida, std::string& error) {
  std::ifstream f(ruta, std::ios::binary | std::ios::ate);
  if (!f) { error = "no se pudo abrir B2TZ"; return false; }
  const auto n = f.tellg();
  if (n < 64 || n > 64 * 1024 * 1024) { error = "tamaño B2TZ inválido"; return false; }
  std::vector<uint8_t> datos(static_cast<size_t>(n));
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(datos.data()), std::streamsize(datos.size()))) {
    error = "lectura B2TZ incompleta";
    return false;
  }
  return Leer(std::move(datos), salida, error);
}
}  // namespace dbz3::escenarios::b2
