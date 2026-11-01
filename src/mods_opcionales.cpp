// dbz3 - Mods opcionales: lector comun (ver mods_opcionales.h). Solo lee ficheros del jugador (mods/ y su ISO).
#include "mods_opcionales.h"

#include "launcher/settings.h"
#include "mods.h"
#include "net/netplay.h"
#include "pasarela.h"
#include "region.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace dbz3::opcional {
namespace {

bool EndsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// id logico por convencion: "opc_b2_musica_combate" -> "b2.musica_combate" (juego.resto). El toml puede
// fijarlo con [opcional] id = "..."; si no, se usa la carpeta tal cual cuando no sigue la convencion.
std::string IdDeCarpeta(const std::string& carpeta) {
  std::string s = carpeta;
  if (s.rfind("opc_", 0) == 0) s.erase(0, 4);
  const size_t guion = s.find('_');
  if (guion != std::string::npos && guion > 0) s = s.substr(0, guion) + "." + s.substr(guion + 1);
  return s;
}

double AhoraMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::mutex g_mu;
std::shared_ptr<const std::vector<Mod>> g_cache;
double g_cache_ms = -1e18;

toml::table LeerToml(const std::filesystem::path& p) {
  try {
    return toml::parse_file(rex::path_to_utf8(p));
  } catch (const std::exception& ex) {
    REXLOG_WARN("dbz3 opcional: {} no se pudo leer: {}", rex::path_to_utf8(p), ex.what());
    return {};
  }
}

std::string CarpetasManifest(const std::filesystem::path& dir, const std::string& clave) {
  std::ifstream in(dir / "manifest.txt");
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line.compare(0, eq, clave) != 0 || line.size() <= eq) continue;
    std::string v = line.substr(eq + 1);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    return v;
  }
  return {};
}

// Mod con ese id logico (o nombre de carpeta); `keep` mantiene vivo el vector de la cache mientras se usa.
const Mod* Encontrar(const std::string& id, std::shared_ptr<const std::vector<Mod>>& keep) {
  keep = Listar();
  for (const Mod& m : *keep) {
    if (m.id == id || m.carpeta == id) return &m;
  }
  return nullptr;
}

// Nodo del opcional.toml: `clave` (admite puntos) en la raiz y, si no esta, dentro de [opcional].
const toml::node* Nodo(const Mod& m, const std::string& clave) {
  auto navegar = [&](const toml::table& raiz) -> const toml::node* {
    const toml::node* n = &raiz;
    size_t p = 0;
    while (p <= clave.size()) {
      const size_t d = clave.find('.', p);
      const std::string k = clave.substr(p, d == std::string::npos ? std::string::npos : d - p);
      const toml::table* t = n ? n->as_table() : nullptr;
      if (!t) return nullptr;
      n = t->get(k);
      if (d == std::string::npos) return n;
      p = d + 1;
    }
    return nullptr;
  };
  if (const toml::node* n = navegar(m.datos)) return n;
  if (const toml::table* op = m.datos.get("opcional") ? m.datos.get("opcional")->as_table() : nullptr) return navegar(*op);
  return nullptr;
}

// ---- ISO9660 de solo lectura (extraer un fichero de la copia del jugador; nada se reparte) ----
constexpr uint32_t kSector = 2048;

uint32_t U32le(const std::vector<uint8_t>& b, size_t o) {
  return o + 4 <= b.size() ? uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24
                           : 0;
}
std::string Mayus(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::vector<uint8_t> LeerIso(std::ifstream& f, uint64_t off, uint32_t n) {
  std::vector<uint8_t> b(n);
  f.clear();
  f.seekg(std::streamoff(off));
  f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
  b.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
  return b;
}
// Baja por el arbol comparando la ruta en mayusculas; `quiero` = "USR/MOVIE_EU.SFD".
bool IsoBuscar(std::ifstream& f, uint32_t lba, uint32_t tam, const std::string& quiero, const std::string& prefijo,
               int prof, uint32_t& out_lba, uint32_t& out_tam) {
  if (prof > 8 || tam > (64u << 20)) return false;
  const std::vector<uint8_t> d = LeerIso(f, uint64_t(lba) * kSector, (tam + kSector - 1) / kSector * kSector);
  for (size_t o = 0; o + 33 <= d.size();) {
    const uint8_t len = d[o];
    if (len == 0) {   // el resto del sector va vacio
      o = (o / kSector + 1) * kSector;
      continue;
    }
    if (o + len > d.size()) break;
    const uint32_t elba = U32le(d, o + 2), etam = U32le(d, o + 10);
    const bool dir = (d[o + 25] & 2) != 0;
    std::string n(reinterpret_cast<const char*>(&d[o + 33]), std::min<size_t>(d[o + 32], d.size() - o - 33));
    o += len;
    if (n.empty() || n[0] == '\0' || n[0] == '\1') continue;
    if (const size_t s = n.find(';'); s != std::string::npos) n.resize(s);
    const std::string full = prefijo.empty() ? Mayus(n) : prefijo + "/" + Mayus(n);
    if (full == quiero) {
      if (dir) return false;
      out_lba = elba;
      out_tam = etam;
      return true;
    }
    if (dir && quiero.rfind(full + "/", 0) == 0 &&
        IsoBuscar(f, elba, etam, quiero, full, prof + 1, out_lba, out_tam))
      return true;
  }
  return false;
}
bool ExtraerDeIso(const std::filesystem::path& iso, const std::string& ruta, const std::filesystem::path& destino) {
  std::ifstream f(iso, std::ios::binary);
  if (!f) return false;
  const std::vector<uint8_t> pvd = LeerIso(f, uint64_t(16) * kSector, kSector);
  if (pvd.size() < 2048 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return false;
  const uint32_t raiz_lba = U32le(pvd, 156 + 2), raiz_tam = U32le(pvd, 156 + 10);
  std::string quiero = Mayus(ruta);
  for (char& c : quiero)
    if (c == '\\') c = '/';
  uint32_t lba = 0, tam = 0;
  if (!IsoBuscar(f, raiz_lba, raiz_tam, quiero, "", 0, lba, tam) || !tam) return false;
  std::filesystem::path parcial = destino;
  parcial += ".parcial";   // y al final un rename: nunca queda un fichero a medias con el nombre bueno
  {
    std::ofstream out(parcial, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (uint64_t off = 0; off < tam;) {
      const uint32_t n = uint32_t(std::min<uint64_t>(1u << 20, tam - off));
      const std::vector<uint8_t> b = LeerIso(f, uint64_t(lba) * kSector + off, n);
      if (b.empty()) return false;
      out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
      off += b.size();
    }
    if (!out.good()) return false;
  }
  std::error_code ec;
  std::filesystem::rename(parcial, destino, ec);
  REXLOG_INFO("dbz3 opcional: extraido {} de la ISO a {} ({} bytes)", ruta, rex::path_to_utf8(destino), tam);
  return !ec;
}

// PRUEBAS (tools/pruebas.py): DBZ3_TEST_OPCIONAL="id1,id2" los da por activos sin tocar mods/.
bool Forzado(const std::string& id) {
  const char* t = std::getenv("DBZ3_TEST_OPCIONAL");
  if (!t) return false;
  const std::string s = "," + std::string(t) + ",";
  return s.find("," + id + ",") != std::string::npos;
}

}  // namespace

std::shared_ptr<const std::vector<Mod>> Listar() {
  const double ms = AhoraMs();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_cache && ms - g_cache_ms < 2000) return g_cache;
  }
  auto out = std::make_shared<std::vector<Mod>>();
  std::error_code ec;
  for (auto [it, iec] = std::pair{std::filesystem::directory_iterator(ModsRoot(), ec), std::error_code{}};
       !iec && it != std::filesystem::directory_iterator(); it.increment(iec)) {
    const std::string carpeta = rex::path_to_utf8(it->path().filename());
    if (carpeta.empty() || carpeta[0] == '.' || !it->is_directory(ec)) continue;
    const std::filesystem::path toml = it->path() / "opcional.toml";
    if (!std::filesystem::exists(toml, ec)) continue;   // no es un mod opcional
    Mod m;
    m.carpeta = carpeta;
    m.datos = LeerToml(toml);
    m.textos = LeerToml(it->path() / "textos.toml");
    m.activo = !EndsWith(carpeta, ".disabled") && !std::filesystem::exists(it->path() / ".disabled", ec);
    m.id = IdDeCarpeta(carpeta);
    if (const toml::table* op = m.datos.get("opcional") ? m.datos.get("opcional")->as_table() : nullptr)
      if (const toml::node* id = op->get("id"))
        if (const toml::value<std::string>* s = id->as_string()) m.id = s->get();
    out->push_back(std::move(m));
  }
  std::sort(out->begin(), out->end(), [](const Mod& a, const Mod& b) { return a.carpeta < b.carpeta; });
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cache = out;
    g_cache_ms = ms;
  }
  return out;
}

void Refrescar() {
  std::lock_guard<std::mutex> lk(g_mu);
  g_cache_ms = -1e18;
}

bool Activo(const std::string& id) {
  if (Forzado(id)) return true;
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  return m && m->activo;
}

bool Desactivado(const std::string& id) {
  if (Forzado(id)) return false;
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  return m && !m->activo;
}

long long Entero(const std::string& id, const std::string& clave, long long defecto) {
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  const toml::node* n = m ? Nodo(*m, clave) : nullptr;
  if (const toml::value<int64_t>* v = n ? n->as_integer() : nullptr) return v->get();
  return defecto;
}

std::string Texto(const std::string& id, const std::string& clave, const std::string& defecto) {
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  const toml::node* n = m ? Nodo(*m, clave) : nullptr;
  if (const toml::value<std::string>* v = n ? n->as_string() : nullptr) return v->get();
  return defecto;
}

std::vector<int> Lista(const std::string& id, const std::string& clave) {
  std::vector<int> out;
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  const toml::node* n = m ? Nodo(*m, clave) : nullptr;
  const toml::array* a = n ? n->as_array() : nullptr;
  if (!a) return out;
  for (const toml::node& e : *a)
    if (const toml::value<int64_t>* v = e.as_integer()) out.push_back(int(v->get()));
  return out;
}

std::vector<std::string> Claves(const std::string& id, const std::string& tabla) {
  std::vector<std::string> out;
  std::shared_ptr<const std::vector<Mod>> keep;
  const Mod* m = Encontrar(id, keep);
  const toml::node* n = m ? Nodo(*m, tabla) : nullptr;
  const toml::table* t = n ? n->as_table() : nullptr;
  if (!t) return out;
  for (auto&& [k, v] : *t) {
    (void)v;
    out.push_back(std::string(k.str()));
  }
  return out;
}

std::string Nombre(const Mod& m, const std::string& idioma) {
  const toml::node* n = m.textos.get("nombre_" + idioma);
  if (const toml::value<std::string>* v = n ? n->as_string() : nullptr) return v->get();
  std::string v = CarpetasManifest(ModsRoot() / m.carpeta, "name");
  return v.empty() ? m.carpeta : v;
}

std::string Descripcion(const Mod& m, const std::string& idioma) {
  const toml::node* n = m.textos.get("descripcion_" + idioma);
  if (const toml::value<std::string>* v = n ? n->as_string() : nullptr) return v->get();
  return CarpetasManifest(ModsRoot() / m.carpeta, "description");
}

std::filesystem::path ArchivoDeJuegoPs2(const std::string& juego, const std::string& ruta) {
  const std::string clave = juego == "b2" ? "budokai 2" : juego == "iw" ? "infinite world" : juego;
  const std::filesystem::path j = pasarela::BuscarJuego(clave);
  if (j.empty()) {
    REXLOG_WARN("dbz3 opcional: sin copia de {} en ps2_games (falta la ISO para {})", juego, ruta);
    return {};
  }
  std::error_code ec;
  if (std::filesystem::is_directory(j, ec)) {   // carpeta extraida: el fichero tal cual
    const std::filesystem::path p = j / std::filesystem::path(ruta);
    if (std::filesystem::is_regular_file(p, ec)) return p;
    REXLOG_WARN("dbz3 opcional: {} no esta en la carpeta {}", ruta, rex::path_to_utf8(j));
    return {};
  }
  // ISO: extraer a la cache (una vez; la ISO no se toca)
  const std::filesystem::path cache = dbz3::settings::UserDataRoot() / "cache_ps2";
  const std::filesystem::path destino = cache / std::filesystem::path(juego) / std::filesystem::path(ruta).filename();
  if (std::filesystem::is_regular_file(destino, ec)) return destino;
  std::filesystem::create_directories(cache, ec);
  if (!ExtraerDeIso(j, ruta, destino)) {
    REXLOG_WARN("dbz3 opcional: no se pudo extraer {} de {}", ruta, rex::path_to_utf8(j));
    return {};
  }
  return std::filesystem::is_regular_file(destino, ec) ? destino : std::filesystem::path();
}

}  // namespace dbz3::opcional

// ---------------------------------------------------------------------------------------------------------
// G05: selector de musica de combate (ver mods_opcionales.h). Backend sin UI: enumera pistas ADX reales de
// la copia del jugador y guarda la eleccion. La reproduccion usa pasarela::OirMusicaPs2 (misma via que el
// tablero de B2 y los menus de IW), asi que no hace falta otro decodificador.
// ---------------------------------------------------------------------------------------------------------
namespace dbz3::musica {
namespace {

uint32_t Be32(const std::vector<uint8_t>& b, size_t o) {
  return o + 4 <= b.size() ? uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3] : 0;
}

bool CabeceraAdx(pasarela::AfsPs2& afs, int entrada, int& hz, int& canales, double& segundos) {
  if (entrada < 0) return false;
  const std::vector<uint8_t> h = afs.Entrada(size_t(entrada), 32);
  if (h.size() < 32 || h[0] != 0x80) return false;   // firma ADX (0x8000 big-endian)
  hz = int(Be32(h, 8));
  canales = h[7];
  const uint32_t muestras = Be32(h, 12);
  if (hz < 8000 || hz > 48000 || canales < 1 || canales > 2) return false;
  segundos = hz > 0 ? double(muestras) / hz : 0.0;
  return true;
}

std::filesystem::path BaseDeJuego(const std::string& juego) {
  if (juego == "b3") return dbz3::HostDataRoot();
  if (juego == "b2") return pasarela::BuscarJuego("budokai 2");
  if (juego == "iw") return pasarela::BuscarJuego("infinite world");
  if (juego == "b1") return pasarela::BuscarIsoB1();
  if (juego == "t3") return pasarela::IsoPs2(5);   // ISO de Tenkaichi 3 del jugador (solo lectura)
  return {};
}

std::string NombreFuente(const std::string& juego) {
  if (juego == "b3") return "B3 HD";
  if (juego == "b2") return "Budokai 2";
  if (juego == "iw") return "Infinite World";
  if (juego == "b1") return "Budokai 1";
  if (juego == "t3") return "Tenkaichi 3";
  if (juego == "propia") return "Mi musica";
  return juego;
}

std::mutex g_pistas_mu;
std::shared_ptr<const std::vector<Pista>> g_pistas;
std::chrono::steady_clock::time_point g_pistas_at;

void AgregarJuego(std::vector<Pista>& out, std::set<std::string>& vistos, const std::string& juego,
                  const std::string& afs_nombre, double minimo_segundos) {
  const std::filesystem::path base = BaseDeJuego(juego);
  if (base.empty()) return;
  // otras regiones: mismos AFS con otro sufijo (no se reparte nada; solo se lee la copia del jugador).
  std::vector<std::string> nombres{afs_nombre};
  for (const char* alt : {"ADX_EU.AFS", "ADX_JP.AFS", "adx_eur.afs", "adx_jpn.afs", "ADX_CMN.AFS", "adx_usa.afs"})
    if (afs_nombre != alt) nombres.push_back(alt);
  pasarela::AfsPs2 afs;
  std::string usado;
  for (const std::string& nombre : nombres) {
    if (afs.Abrir(base, nombre)) {
      usado = nombre;
      break;
    }
  }
  if (usado.empty()) return;
  for (size_t k = 0; k < afs.Cuantas(); ++k) {
    int hz = 0, canales = 1;
    double segundos = 0.0;
    if (!CabeceraAdx(afs, int(k), hz, canales, segundos)) continue;
    if (segundos < minimo_segundos) continue;   // fuera voces y efectos cortos
    const std::string clave = juego + "|" + usado + "|" + std::to_string(k);
    if (!vistos.insert(clave).second) continue;
    Pista p;
    p.fuente = juego;
    p.afs = usado;
    p.juego = base;
    p.entrada = int(k);
    p.hz = hz;
    p.canales = canales;
    p.segundos = segundos;
    p.nombre = NombreFuente(juego) + " #" + std::to_string(k);
    out.push_back(std::move(p));
  }
}

// Fase 1: Tenkaichi 3 como fuente. Su musica son 20 ADX (zs3_00..19, estereo 24 kHz) en PZS3xx2.AFS, entradas 64974..64993
// (t3_audio.py, medido en studies 5/ENTREGAS/E2). Solo se miran esas 20 (el AFS tiene 65201 entradas). Se lee la ISO del
// jugador; no se copia ni se reparte nada.
void AgregarT3(std::vector<Pista>& out, std::set<std::string>& vistos) {
  constexpr int kPrimera = 64974, kNum = 20;
  const std::filesystem::path base = BaseDeJuego("t3");
  if (base.empty()) return;
  pasarela::AfsPs2 afs;
  std::string usado;
  for (const char* nombre : {"PZS3EU2.AFS", "PZS3US2.AFS", "PZS3JP2.AFS", "PZS3JA2.AFS"})
    if (afs.Abrir(base, nombre)) {
      usado = nombre;
      break;
    }
  if (usado.empty()) return;
  for (int n = 0; n < kNum && size_t(kPrimera + n) < afs.Cuantas(); ++n) {
    int hz = 0, canales = 1;
    double segundos = 0.0;
    if (!CabeceraAdx(afs, kPrimera + n, hz, canales, segundos)) continue;
    if (!vistos.insert("t3|" + usado + "|" + std::to_string(kPrimera + n)).second) continue;
    Pista p;
    p.fuente = "t3";
    p.afs = usado;
    p.juego = base;
    p.entrada = kPrimera + n;
    p.hz = hz;
    p.canales = canales;
    p.segundos = segundos;
    char num[8];
    std::snprintf(num, sizeof num, "%02d", n);
    p.nombre = std::string("Tenkaichi 3 - Pista ") + num;
    out.push_back(std::move(p));
  }
}

// Fase 4 del Kit: musica PROPIA del jugador. Cada carpeta mods/<x>/ con un musica.afs (la crea el asistente «Poner musica»
// del Kit: un AFS con el ADX ya convertido) aporta sus pistas a la fuente "propia". Se lee con el mismo AfsPs2 y se reproduce con
// OirMusicaPs2 que las demas: no se toca la musica nativa. La carpeta desactivada (.disabled) no cuenta. Nombre = pistas.txt
// (una linea por entrada) o el de la carpeta.
void AgregarPropias(std::vector<Pista>& out, std::set<std::string>& vistos) {
  std::error_code ec;
  std::vector<std::filesystem::path> dirs;
  for (auto [it, iec] = std::pair{std::filesystem::directory_iterator(ModsRoot(), ec), std::error_code{}};
       !iec && it != std::filesystem::directory_iterator(); it.increment(iec)) {
    const std::string carpeta = rex::path_to_utf8(it->path().filename());
    if (carpeta.empty() || carpeta[0] == '.' || !it->is_directory(ec) || carpeta.ends_with(".disabled")) continue;
    if (std::filesystem::exists(it->path() / ".disabled", ec) || !std::filesystem::exists(it->path() / "musica.afs", ec)) continue;
    dirs.push_back(it->path());
  }
  std::sort(dirs.begin(), dirs.end());
  for (const std::filesystem::path& dir : dirs) {
    pasarela::AfsPs2 afs;
    if (!afs.Abrir(dir, "musica.afs")) continue;
    std::vector<std::string> nombres;
    std::ifstream in(dir / "pistas.txt");
    for (std::string ln; std::getline(in, ln);) {
      while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
      nombres.push_back(ln);
    }
    const std::string carpeta = rex::path_to_utf8(dir.filename());
    for (size_t k = 0; k < afs.Cuantas(); ++k) {
      int hz = 0, canales = 1;
      double segundos = 0.0;
      if (!CabeceraAdx(afs, int(k), hz, canales, segundos) || segundos < 1.0) continue;
      if (!vistos.insert("propia|" + carpeta + "|" + std::to_string(k)).second) continue;
      Pista p;
      p.fuente = "propia";
      p.afs = "musica.afs";
      p.juego = dir;
      p.entrada = int(k);
      p.hz = hz;
      p.canales = canales;
      p.segundos = segundos;
      p.nombre = k < nombres.size() && !nombres[k].empty() ? nombres[k] : carpeta;
      out.push_back(std::move(p));
    }
  }
}

std::string ModoName(Modo modo) {
  switch (modo) {
    case Modo::kFija: return "fija";
    case Modo::kAleatoria: return "aleatoria";
    default: return "original";
  }
}

Modo ParseModo(const std::string& text) {
  if (text == "fija" || text == "fixed") return Modo::kFija;
  if (text == "aleatoria" || text == "random") return Modo::kAleatoria;
  return Modo::kOriginal;
}

std::string Seguro(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') out += c;
    if (out.size() >= 64) break;
  }
  return out.empty() ? "global" : out;
}

std::string Escapa(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

void LeerArchivo(const std::filesystem::path& path, Opcion& o) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return;
  try {
    const toml::table t = toml::parse_file(rex::path_to_utf8(path));
    if (const auto v = t["modo"].value<std::string>()) o.modo = ParseModo(*v);
    if (const auto v = t["fuente"].value<std::string>()) o.fuente = *v;
    if (const auto v = t["entrada"].value<int64_t>()) o.entrada = int(*v);
    if (const auto v = t["partida"].value<std::string>()) o.partida = *v;
  } catch (const std::exception& ex) {
    REXLOG_WARN("dbz3 musica: {} no se pudo leer: {}", rex::path_to_utf8(path), ex.what());
  }
}

bool EscribirArchivo(const std::filesystem::path& path, const Opcion& o) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# dbz3: musica de combate elegida por el jugador (G05)\n";
    out << "modo = \"" << ModoName(o.modo) << "\"\n";
    out << "fuente = \"" << Escapa(o.fuente) << "\"\n";
    out << "entrada = " << o.entrada << "\n";
    out << "partida = \"" << Escapa(o.partida) << "\"\n";
    if (!out.good()) return false;
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    REXLOG_WARN("dbz3 musica: no se pudo publicar {} ({})", rex::path_to_utf8(path), ec.message());
    return false;
  }
  return true;
}

}  // namespace

std::vector<Pista> Pistas() {
  {
    std::lock_guard<std::mutex> lock(g_pistas_mu);
    const auto now = std::chrono::steady_clock::now();
    if (g_pistas && std::chrono::duration_cast<std::chrono::seconds>(now - g_pistas_at).count() < 5) return *g_pistas;
  }
  auto out = std::make_shared<std::vector<Pista>>();
  std::set<std::string> vistos;
  AgregarJuego(*out, vistos, "b3", "adx_usa.afs", 20.0);
  AgregarJuego(*out, vistos, "b2", "ADX_US.AFS", 20.0);
  AgregarJuego(*out, vistos, "iw", "ADX_CMN.AFS", 20.0);
  AgregarJuego(*out, vistos, "b1", "ADX_JP.AFS", 20.0);
  AgregarT3(*out, vistos);
  AgregarPropias(*out, vistos);
  // Mods opcionales: [pistas] nombre = entrada, con [opcional] juego/afs (o el prefijo del id: b2.xxx).
  for (const opcional::Mod& m : *opcional::Listar()) {
    if (!m.activo) continue;
    const std::vector<std::string> claves = opcional::Claves(m.id, "pistas");
    if (claves.empty()) continue;
    std::string juego = opcional::Texto(m.id, "juego", "");
    if (juego.empty()) juego = m.id.substr(0, std::min<size_t>(2, m.id.size()));
    const std::string afs_nombre = opcional::Texto(m.id, "afs", juego == "b2" ? "ADX_US.AFS"
                                                                 : juego == "iw" ? "ADX_CMN.AFS"
                                                                 : juego == "b1" ? "ADX_JP.AFS" : "adx_usa.afs");
    const std::filesystem::path base = BaseDeJuego(juego);
    if (base.empty()) continue;
    pasarela::AfsPs2 afs;
    if (!afs.Abrir(base, afs_nombre)) continue;
    for (const std::string& clave : claves) {
      const int entrada = int(opcional::Entero(m.id, "pistas." + clave, -1));
      int hz = 0, canales = 1;
      double segundos = 0.0;
      if (!CabeceraAdx(afs, entrada, hz, canales, segundos)) continue;
      const std::string id = "mod:" + m.carpeta + "|" + afs_nombre + "|" + std::to_string(entrada);
      if (!vistos.insert(id).second) continue;
      Pista p;
      p.fuente = "mod:" + m.carpeta;
      p.afs = afs_nombre;
      p.juego = base;
      p.entrada = entrada;
      p.hz = hz;
      p.canales = canales;
      p.segundos = segundos;
      p.nombre = clave + " (" + m.carpeta + ")";
      out->push_back(std::move(p));
    }
  }
  std::sort(out->begin(), out->end(), [](const Pista& a, const Pista& b) {
    if (a.fuente != b.fuente) return a.fuente < b.fuente;
    return a.entrada < b.entrada;
  });
  {
    std::lock_guard<std::mutex> lock(g_pistas_mu);
    g_pistas = out;
    g_pistas_at = std::chrono::steady_clock::now();
  }
  return *out;
}

void RefrescarPistas() {
  std::lock_guard<std::mutex> lock(g_pistas_mu);
  g_pistas_at = std::chrono::steady_clock::time_point{};
}

std::filesystem::path RutaRegistro(const std::string& partida) {
  const std::filesystem::path root = dbz3::settings::UserDataRoot();
  if (partida.empty()) return root / "musica.toml";
  return root / ("musica_partida_" + Seguro(partida) + ".toml");
}

Opcion Leer(const std::string& partida) {
  Opcion o;
  LeerArchivo(RutaRegistro(), o);
  if (!partida.empty()) LeerArchivo(RutaRegistro(partida), o);
  o.partida = partida;
  return o;
}

void Guardar(const Opcion& opcion) {
  EscribirArchivo(RutaRegistro(), opcion);
  if (!opcion.partida.empty()) EscribirArchivo(RutaRegistro(opcion.partida), opcion);
}

Pista Elegir(const Opcion& opcion, uint32_t semilla) {
  if (opcion.modo == Modo::kOriginal) return {};
  std::vector<Pista> lista = Pistas();
  if (!opcion.fuente.empty())
    std::erase_if(lista, [&](const Pista& p) { return p.fuente != opcion.fuente; });
  if (lista.empty()) return {};
  if (opcion.modo == Modo::kFija) {
    for (const Pista& p : lista)
      if (p.entrada == opcion.entrada) return p;
    return lista.front();
  }
  uint32_t x = semilla + 0x9E3779B9u;
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  return lista[x % lista.size()];
}

bool ModoConMusicaNativa(const std::string& ambito) {
  return ambito == "b2_tablero" || ambito == "b2_menu" || ambito == "iw_mapa" || ambito == "iw_menu" ||
         ambito == "nave" || ambito == "b1_menu" || ambito == "menu";
}

void Escuchar(const Pista& pista, float volumen) {
  if (pista.juego.empty() || pista.afs.empty() || pista.entrada < 0) return;
  pasarela::OirMusicaPs2(pista.juego, pista.afs, pista.entrada, volumen);
}

void Parar() { pasarela::CallarMusicaPs2(); }

namespace {

// Valor del cvar dbz3_musica_combate: "original" | "aleatoria[:fuente]" | "fija:<fuente>:<n>" (n = posicion en la lista de esa
// fuente, ordenada por entrada: para t3 es el numero de pista 0..19).
struct Eleccion {
  Modo modo = Modo::kOriginal;
  std::string fuente;
  int ordinal = -1;
};

Eleccion ParseCvar(const std::string& v) {
  Eleccion e;
  if (v.rfind("aleatoria", 0) == 0) {
    e.modo = Modo::kAleatoria;
    if (v.size() > 10 && v[9] == ':') e.fuente = v.substr(10);
  } else if (v.rfind("fija:", 0) == 0) {
    const size_t c = v.find(':', 5);
    if (c != std::string::npos) {
      e.modo = Modo::kFija;
      e.fuente = v.substr(5, c - 5);
      e.ordinal = std::atoi(v.c_str() + c + 1);
    }
  }
  return e;
}

// Pista n de la fuente (posicion en la lista filtrada); vacia (entrada -1) si no existe.
Pista PistaDe(const std::string& fuente, int ordinal) {
  std::vector<Pista> lista = Pistas();
  std::erase_if(lista, [&](const Pista& p) { return p.fuente != fuente; });
  return ordinal >= 0 && size_t(ordinal) < lista.size() ? lista[size_t(ordinal)] : Pista{};
}

// Fase 4: musica.toml refleja siempre el cvar (lo escribe el selector y el menu F1) y, al arrancar con el cvar en "original",
// restaura la eleccion guardada. Solo local: nada de esto entra en el estado compartido del online.
void SincronizarRegistro() {
  static bool inicio = true;
  static std::string ultimo;
  const std::string v = rex::cvar::GetFlagByName("dbz3_musica_combate");
  if (inicio) {
    inicio = false;
    ultimo = v;
    if (v == "original") {
      const Opcion o = Leer();
      std::string nuevo;
      if (o.modo == Modo::kFija && !o.fuente.empty() && o.entrada >= 0) {
        std::vector<Pista> lista = Pistas();
        std::erase_if(lista, [&](const Pista& p) { return p.fuente != o.fuente; });
        for (size_t k = 0; k < lista.size(); ++k)
          if (lista[k].entrada == o.entrada) nuevo = "fija:" + o.fuente + ":" + std::to_string(k);
      } else if (o.modo == Modo::kAleatoria) {
        nuevo = o.fuente.empty() ? "aleatoria" : "aleatoria:" + o.fuente;
      }
      if (!nuevo.empty()) {
        ultimo = nuevo;
        rex::cvar::SetFlagByName("dbz3_musica_combate", nuevo);
        REXLOG_INFO("dbz3 musica: eleccion recuperada de musica.toml: {}", nuevo);
      }
    } else {   // el cvar ya trae una eleccion (config o linea de comandos): musica.toml la refleja
      const Eleccion e = ParseCvar(v);
      Opcion o;
      o.modo = e.modo;
      o.fuente = e.fuente;
      if (e.modo == Modo::kFija) o.entrada = PistaDe(e.fuente, e.ordinal).entrada;
      Guardar(o);
      REXLOG_INFO("dbz3 musica: eleccion guardada en musica.toml: {}", v);
    }
    return;
  }
  if (v == ultimo) return;
  ultimo = v;
  const Eleccion e = ParseCvar(v);
  Opcion o;
  o.modo = e.modo;
  o.fuente = e.fuente;
  if (e.modo == Modo::kFija) o.entrada = PistaDe(e.fuente, e.ordinal).entrada;
  Guardar(o);
  REXLOG_INFO("dbz3 musica: eleccion guardada en musica.toml: {}", v);
}

}  // namespace

int DeltaTeclado() {
#if defined(_WIN32)
  static bool antes[2] = {false, false};
  const bool ahora[2] = {(GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0, (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0};
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  int d = 0;
  if (pid == GetCurrentProcessId()) {
    if (ahora[1] && !antes[1]) d = 1;
    if (ahora[0] && !antes[0]) d = -1;
  }
  antes[0] = ahora[0];
  antes[1] = ahora[1];
  return d;
#else
  return 0;
#endif
}

std::string TextoEleccion() {
  const Eleccion e = ParseCvar(rex::cvar::GetFlagByName("dbz3_musica_combate"));
  if (e.modo == Modo::kOriginal) return "Original";
  if (e.modo == Modo::kAleatoria) return e.fuente.empty() ? "Aleatoria" : "Aleatoria: " + NombreFuente(e.fuente);
  const Pista p = PistaDe(e.fuente, e.ordinal);
  return p.entrada < 0 ? "Original" : p.nombre;
}

void CambiarEleccion(int delta) {
  // Ciclo: Original, luego cada pista de cada fuente (Pistas() ya viene ordenada por fuente y entrada).
  const std::vector<Pista> todas = Pistas();
  if (todas.empty()) return;
  SincronizarRegistro();   // la primera vez recupera lo guardado antes de moverse
  const Eleccion e = ParseCvar(rex::cvar::GetFlagByName("dbz3_musica_combate"));
  const int n = int(todas.size()) + 1;   // 0 = Original
  int pos = 0;
  if (e.modo == Modo::kFija) {
    int ord = 0;
    for (size_t k = 0; k < todas.size(); ++k) {
      if (todas[k].fuente != e.fuente) continue;
      if (ord++ == e.ordinal) pos = int(k) + 1;
    }
  }
  pos = ((pos + delta) % n + n) % n;
  std::string nuevo = "original";
  if (pos > 0) {
    const Pista& p = todas[size_t(pos - 1)];
    int ord = 0;
    for (int k = 0; k < pos - 1; ++k)
      if (todas[size_t(k)].fuente == p.fuente) ++ord;
    nuevo = "fija:" + p.fuente + ":" + std::to_string(ord);
    REXLOG_INFO("dbz3 musica: eleccion musical: {} #{} ({})", p.fuente, ord, p.nombre);
  } else {
    REXLOG_INFO("dbz3 musica: eleccion musical: original");
  }
  rex::cvar::SetFlagByName("dbz3_musica_combate", nuevo);
  dbz3::settings::SaveUserSettings();
  SincronizarRegistro();   // escribe musica.toml ya, sin esperar al siguiente fotograma
}

namespace {
struct AvisoOnlineEstado { int tipo = 0; std::chrono::steady_clock::time_point at{}; };
AvisoOnlineEstado g_aviso_online;   // solo hilo de la interfaz
}  // namespace

int AvisoOnline() {
  if (!g_aviso_online.tipo) return 0;
  const auto seg = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_aviso_online.at).count();
  if (seg >= 8 || !dbz3::net::Session::Get().active()) g_aviso_online.tipo = 0;
  return g_aviso_online.tipo;
}

void ActualizarCombate(bool en_combate) {
  static bool sonando = false;
  static uint32_t combates = 0;
  SincronizarRegistro();
  if (!en_combate) {
    if (sonando) {
      sonando = false;
      Parar();
      REXLOG_INFO("dbz3 musica: fin del combate: musica cortada");
    }
    return;
  }
  if (sonando) return;
  const Eleccion e = ParseCvar(rex::cvar::GetFlagByName("dbz3_musica_combate"));
  if (e.modo == Modo::kOriginal) return;   // "original": no se toca nada
  sonando = true;   // una sola vez por combate, haya pista o no
  auto& red = dbz3::net::Session::Get();
  const bool online = red.active();
  Pista p;
  if (e.modo == Modo::kFija) {
    p = PistaDe(e.fuente, e.ordinal);
  } else {
    Opcion o;
    o.modo = Modo::kAleatoria;
    o.fuente = e.fuente;
    // online: misma semilla en los dos PC: con las mismas pistas instaladas, la misma pista
    p = Elegir(o, online ? red.AzarCompartido(0x6D75) : ++combates * 2654435761u);
  }
  // La eleccion es de cada jugador y el protocolo no la lleva: el aviso lo dice en pantalla (hub_panel.cpp).
  if (online) g_aviso_online = {p.entrada < 0 ? 2 : 1, std::chrono::steady_clock::now()};
  if (p.entrada < 0) {
    REXLOG_INFO("dbz3 musica: combate: no hay pistas de '{}' (se queda la del juego)", e.fuente);
    return;
  }
  if (online) REXLOG_INFO("dbz3 musica: online: {} {} entrada {}: solo la oye este jugador", p.fuente, p.afs, p.entrada);
  const float vol = settings::AudioMute() ? 0.0f : float(settings::MasterVolume()) * 0.6f;
  if (e.modo == Modo::kFija)
    REXLOG_INFO("dbz3 musica: combate: {} ({} {} #{} entrada {})", p.nombre, p.fuente, p.afs, e.ordinal, p.entrada);
  else
    REXLOG_INFO("dbz3 musica: combate: {} ({} {} #{})", p.nombre, p.fuente, p.afs, p.entrada);
  Escuchar(p, vol);
}

}  // namespace dbz3::musica
