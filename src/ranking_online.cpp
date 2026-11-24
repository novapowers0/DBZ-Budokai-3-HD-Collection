// dbz3 - RANK-ONLINE: ranking mundial (BETA) de Dragon Budokai Survivors y del reto diario de Dragon Rogue. Ver ranking_online.h.
// Maquetas: E:\DBZ3HD_trabajo\ranking_online\maquetas\ (01 mundial de Survivors, 02 reto diario, 03 ficha, 04* boton).
// Coordenadas: px de esas maquetas a 1280x720; quien llama pasa la esquina y la escala (u).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

#include "ranking_online.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "ingame/online_panel.h"
#include "launcher/i18n.h"
#include "launcher/settings.h"
#include "launcher/ui_kit.h"
#include "launcher/update_check.h"
#include "modos.h"
#include "rogue.h"
#include "survivors/recursos.h"

REXCVAR_DEFINE_BOOL(dbz3_ranking_online, false, "DBZ3/Online",
                    "Ranking mundial (BETA) de Dragon Budokai Survivors y del reto diario de Dragon Rogue: un boton envia la partida");
REXCVAR_DEFINE_STRING(dbz3_rank_url, "https://ntfy.sh", "DBZ3/Online", "Buzon del ranking online (ntfy o compatible)");
REXCVAR_DEFINE_STRING(dbz3_rank_tema, "dbz3hd-rank-c604011f9edf", "DBZ3/Online", "Tema del buzon del ranking online");
REXCVAR_DEFINE_STRING(dbz3_rank_datos, "https://raw.githubusercontent.com/novapowers0/DBZ3HD-Ranking/main", "DBZ3/Online",
                      "Carpeta de donde se descargan las tablas del ranking online");

namespace dbz3::rank {
namespace {
namespace M = dbz3::modos;
namespace S = dbz3::ingame::style;
namespace D = dbz3::ingame::style::mode;
namespace R = dbz3::survivors;
namespace fs = std::filesystem;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200, kA = 0x1000,
                   kB = 0x2000, kX = 0x4000, kY = 0x8000;

// ---------------------------------------------------------------- clave y firma
// La clave de verdad la escribe tools/ranking_online/clave.py en ranking_clave.local.inc (no se sube); sin ella, una de
// desarrollo que la Action no acepta. Ofuscada con kXor: frena a quien edita el mensaje, no a quien desmonta el exe.
constexpr unsigned char kXor = 0x5A;
constexpr unsigned char kClave[] = {
#if __has_include("ranking_clave.local.inc")
#include "ranking_clave.local.inc"
#else
    0x3E, 0x38, 0x20, 0x69, 0x77, 0x28, 0x3B, 0x34, 0x31, 0x77, 0x3E, 0x3F, 0x2C   // "dbz3-rank-dev"
#endif
};

std::string Hex(const unsigned char* b, size_t n) {
  static const char* k = "0123456789abcdef";
  std::string o;
  for (size_t i = 0; i < n; ++i) o += k[b[i] >> 4], o += k[b[i] & 15];
  return o;
}
std::string Hmac(const std::string& msg) {
  std::string key;
  for (unsigned char c : kClave) key += char(c ^ kXor);
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  unsigned char out[32] = {};
  const bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)) &&
                  BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, PUCHAR(key.data()), ULONG(key.size()), 0)) &&
                  BCRYPT_SUCCESS(BCryptHashData(h, PUCHAR(const_cast<char*>(msg.data())), ULONG(msg.size()), 0)) &&
                  BCRYPT_SUCCESS(BCryptFinishHash(h, out, sizeof out, 0));
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  std::fill(key.begin(), key.end(), '\0');
  return ok ? Hex(out, sizeof out) : std::string();
}
std::string Enc(const std::string& s, bool coma) {   // como quote(safe='-_.,') de validar.py (coma=false: tambien la coma)
  std::string o;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || (coma && c == ',')) o += char(c);
    else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}
std::string Azar(int bytes) {
  std::random_device rd;
  unsigned char b[16] = {};
  for (int i = 0; i < bytes && i < 16; ++i) b[i] = static_cast<unsigned char>(rd());
  return Hex(b, size_t(std::min(bytes, 16)));
}
int64_t Ahora() { return int64_t(std::time(nullptr)); }

// ---------------------------------------------------------------- JSON minimo (lo que escribe validar.py)
struct J {
  enum T { kNull, kNum, kStr, kArr, kObj } t = kNull;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  const J& operator[](const char* k) const {
    static const J z;
    for (const auto& [kk, v] : o)
      if (kk == k) return v;
    return z;
  }
  int I() const { return int(n); }
  int64_t L() const { return int64_t(n); }
};
struct Lector {
  const char* p;
  const char* e;
  bool ok = true;
  void Ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  }
  std::string Str() {
    std::string o;
    ++p;
    while (p < e && *p != '"') {
      if (*p != '\\') { o += *p++; continue; }
      if (++p >= e) break;
      const char c = *p++;
      if (c == 'n') o += '\n';
      else if (c == 't') o += '\t';
      else if (c == 'u' && e - p >= 4) {
        const unsigned v = unsigned(std::strtoul(std::string(p, 4).c_str(), nullptr, 16));
        p += 4;
        if (v < 0x80) o += char(v);
        else if (v < 0x800) o += char(0xC0 | (v >> 6)), o += char(0x80 | (v & 63));
        else o += char(0xE0 | (v >> 12)), o += char(0x80 | ((v >> 6) & 63)), o += char(0x80 | (v & 63));
      } else o += c;
    }
    if (p < e) ++p;
    else ok = false;
    return o;
  }
  J Val(int prof = 0) {
    J v;
    Ws();
    if (p >= e || prof > 32) { ok = false; return v; }
    if (*p == '{' || *p == '[') {
      const bool obj = *p == '{';
      const char cierre = obj ? '}' : ']';
      v.t = obj ? J::kObj : J::kArr;
      ++p;
      Ws();
      if (p < e && *p == cierre) { ++p; return v; }
      while (ok && p < e) {
        Ws();
        if (obj) {
          if (p >= e || *p != '"') { ok = false; break; }
          std::string k = Str();
          Ws();
          if (p >= e || *p != ':') { ok = false; break; }
          ++p;
          v.o.emplace_back(std::move(k), Val(prof + 1));
        } else {
          v.a.push_back(Val(prof + 1));
        }
        Ws();
        if (p < e && *p == ',') { ++p; continue; }
        if (p < e && *p == cierre) { ++p; return v; }
        ok = false;
      }
      ok = false;   // solo vale el contenedor que encontro su cierre (JSON truncado = error)
    } else if (*p == '"') {
      v.t = J::kStr, v.s = Str();
    } else if (*p == 't' || *p == 'f' || *p == 'n') {
      v.t = *p == 'n' ? J::kNull : J::kNum, v.n = *p == 't';
      while (p < e && std::isalpha(static_cast<unsigned char>(*p))) ++p;
    } else {
      char* q = nullptr;
      v.t = J::kNum, v.n = std::strtod(p, &q);
      if (q == p) ok = false;
      else p = q;
    }
    return v;
  }
};
bool LeerJson(const std::string& texto, J* out) {
  Lector l{texto.c_str(), texto.c_str() + texto.size()};
  *out = l.Val();
  l.Ws();
  return l.ok && l.p == l.e && out->t == J::kObj;
}

// ---------------------------------------------------------------- tablas
struct Fila {
  int pos = 0, j = 0, p = 0, sg = 0, k = 0, n = 0, w = 0, g = 0, b = 0, vi = 0, d = 0, df = 0;   // df: dificultad (Survivors)
  int64_t t = 0;
  char m = 's';
  std::string id, u, al, ver, e;
  std::vector<std::string> c;
};
struct Perfil {
  int ms = 0, mr = 0, ra = 0, en = 0, fav = 0;
};
struct Tab {
  bool hay = false, error = false;
  int64_t generado = 0, cierra = 0, bajada = 0;   // bajada: ultimo intento (0 = nunca)
  int total = 0;
  std::vector<Fila> filas;
  std::map<std::string, Perfil> jug;
};
bool TablaDe(const std::string& texto, Tab* t) {
  J j;
  if (!LeerJson(texto, &j)) return false;
  if (j["generado"].t != J::kNum || j["total"].t != J::kNum || j["filas"].t != J::kArr || j["jugadores"].t != J::kObj ||
      (j["cierra"].t != J::kNull && j["cierra"].t != J::kNum)) return false;   // tabla sin estructura minima: se conserva la anterior
  t->generado = j["generado"].L(), t->cierra = j["cierra"].L(), t->total = j["total"].I();
  t->filas.clear();
  t->jug.clear();
  for (const J& f : j["filas"].a) {
    Fila x;
    x.pos = f["pos"].I(), x.j = f["j"].I(), x.p = f["p"].I(), x.sg = f["sg"].I(), x.k = f["k"].I(), x.n = f["n"].I();
    x.w = f["w"].I(), x.g = f["g"].I(), x.b = f["b"].I(), x.vi = f["vi"].I(), x.d = f["d"].I(), x.df = f["df"].I(), x.t = f["t"].L();
    x.m = f["m"].s.empty() ? 's' : f["m"].s[0];
    x.id = f["id"].s, x.u = f["u"].s, x.al = f["al"].s, x.ver = f["ver"].s, x.e = f["e"].s;
    for (const J& c : f["c"].a) x.c.push_back(c.s);
    t->filas.push_back(std::move(x));
  }
  for (const auto& [u, p] : j["jugadores"].o) t->jug[u] = {p["ms"].I(), p["mr"].I(), p["ra"].I(), p["en"].I(), p["fav"].I()};
  t->hay = true;
  return true;
}

// ---------------------------------------------------------------- estado (g_mx) y persistencia
enum Estado { kSinEnviar, kEnviando, kEnviada, kEnTabla, kSinConexion, kRechazada, kSoloLocal };
struct Pendiente {
  std::string id, msg;
  char modo = 's';
  int64_t ultimo = 0;   // ultimo envio (0 = aun no)
  bool publicado = false, mods_ok = false;
};
struct Enviada {   // lo que se ha mandado (ranking.toml): estado de la caja del reto diario y del bloque
  std::string id;
  char modo = 's';
  int dia = 0, puntos = 0, estado = kEnviando;
  std::string motivo;
};
std::mutex g_mx;
std::condition_variable g_cv;
bool g_despertar = false, g_cargado = false;
std::string g_uid, g_alias;
// R16: consentimiento explicito. 0 = sin preguntar, 1 = Si, 2 = No. Hasta el Si no sale ninguna peticion de red (ni envio ni tablas).
std::atomic<int> g_consent{0};
std::atomic<int> g_peticiones{0};   // pruebas: peticiones de red intentadas
std::vector<Pendiente> g_cola;
std::vector<Enviada> g_env;
std::map<std::string, Tab> g_tablas;
std::set<std::string> g_pedidas;
int64_t g_recibidos_bajado = 0;

fs::path Dir() { return dbz3::settings::UserDataRoot(); }
std::string CacheDe(const std::string& nombre) {
  std::string s = nombre;
  std::replace(s.begin(), s.end(), '/', '_');
  return (Dir() / "ranking_cache" / s).string();
}
std::string LeerTexto(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}
void Escribir(const fs::path& p, const std::string& texto) {   // .tmp + rename
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << texto;
  }
  fs::rename(tmp, p, ec);
}
void GuardarCola() {   // con g_mx
  std::string o;
  for (const Pendiente& x : g_cola) o += x.id + "\t" + x.modo + "\t" + std::to_string(x.ultimo) + "\t" + (x.publicado ? "1" : "0") + "\t" + x.msg + "\n";
  Escribir(Dir() / "ranking_pendientes.txt", o);
}
void GuardarDatos() {   // con g_mx
  toml::table t;
  t.insert("uid", g_uid);
  t.insert("alias", g_alias);
  t.insert("consent", int64_t(g_consent.load()));
  toml::array env;
  for (const Enviada& e : g_env) {
    env.push_back(toml::table{{"id", e.id}, {"modo", std::string(1, e.modo)}, {"dia", e.dia}, {"puntos", e.puntos}, {"estado", e.estado}, {"motivo", e.motivo}});
  }
  t.insert("env", env);
  std::ostringstream o;
  o << "# Ranking online (RANK-ONLINE): id del PC al azar, alias y partidas enviadas\n" << t << "\n";
  Escribir(Dir() / "ranking.toml", o.str());
}
void Cargar() {   // con g_mx, una vez
  if (g_cargado) return;
  g_cargado = true;
  std::error_code ec;
  bool leido = !std::filesystem::exists(Dir() / "ranking.toml", ec) && !ec;   // sin fichero (primera vez) no hay nada que conservar
  try {
    const toml::table t = toml::parse(LeerTexto(Dir() / "ranking.toml"));
    leido = true;
    g_uid = t["uid"].value_or(std::string());
    g_alias = t["alias"].value_or(std::string());
    g_consent = int(std::clamp<int64_t>(t["consent"].value_or(int64_t(0)), 0, 2));
    if (const toml::array* a = t["env"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* e = n.as_table();
        if (!e) continue;
        const std::string modo = (*e)["modo"].value_or(std::string("s"));
        g_env.push_back({(*e)["id"].value_or(std::string()), modo.empty() ? 's' : modo[0], int((*e)["dia"].value_or(int64_t(0))),
                         int((*e)["puntos"].value_or(int64_t(0))), int((*e)["estado"].value_or(int64_t(kEnviada))), (*e)["motivo"].value_or(std::string())});
      }
    }
  } catch (...) {
  }
  if (g_uid.size() != 16) g_uid = Azar(8);   // id del PC al azar: ningun dato del equipo
  std::istringstream in(LeerTexto(Dir() / "ranking_pendientes.txt"));
  for (std::string l; std::getline(in, l);) {
    std::vector<std::string> c;
    std::istringstream ls(l);
    for (std::string x; std::getline(ls, x, '\t');) c.push_back(x);
    if (c.size() == 5 && !c[4].empty()) g_cola.push_back({c[0], c[4], c[1].empty() ? 's' : c[1][0], std::atoll(c[2].c_str()), c[3] == "1", false});
  }
  // Un fallo de lectura (fichero a medias, antivirus) no autoriza a reescribir
  // consent/uid ni a borrar la cola: se conserva todo hasta la proxima respuesta.
  if (!leido) {
    REXLOG_WARN("dbz3 rank: no se pudo leer ranking.toml; se conservan {} pendientes y el consentimiento en disco", g_cola.size());
  } else {
    GuardarDatos();
  }
  if (leido && g_consent != 1 && !g_cola.empty()) {   // R16: nada recogido antes del Si se manda con un Si posterior
    REXLOG_INFO("dbz3 rank: cola: {} pendientes anteriores al consentimiento descartados", g_cola.size());
    g_cola.clear();
    GuardarCola();
  }
  if (!g_cola.empty()) REXLOG_INFO("dbz3 rank: cola: {} pendientes al arrancar", g_cola.size());
}
Enviada* EnvDe(const std::string& id) {   // con g_mx
  for (Enviada& e : g_env)
    if (e.id == id) return &e;
  return nullptr;
}
void PonEstado(const std::string& id, int estado, const std::string& motivo = {}) {   // con g_mx
  if (Enviada* e = EnvDe(id)) e->estado = estado, e->motivo = motivo;
}

std::string Base(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}
std::string NombreDia(int dia) {   // rogue/diario/AAAA-MM-DD.json
  const std::time_t t = std::time_t(int64_t(dia) * 86400);
  std::tm g{};
  gmtime_s(&g, &t);
  char b[48];
  std::snprintf(b, sizeof b, "rogue/diario/%04d-%02d-%02d.json", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
  return b;
}

// Unica puerta a la red del ranking. Con DBZ3_TEST_RANKING_RED=falsa (solo pruebas) no sale nada: cuenta la peticion y da por
// buenos los envios. Sin consentimiento (g_consent != 1) no se llega aqui: Red() no hace nada.
bool Http(const std::string& url, std::string& out) {
  ++g_peticiones;
  static const bool falsa = [] {
    const char* e = std::getenv("DBZ3_TEST_RANKING_RED");
    return e && !std::strcmp(e, "falsa");
  }();
  if (falsa) {
    out.clear();
    REXLOG_INFO("dbz3 rank: PETICION {} (red falsa, {} bytes)", g_peticiones.load(), url.size());
    return url.find("/publish?") != std::string::npos;
  }
  return dbz3::launcher::HttpGetUrl(url, out);
}

// Descarga una tabla (hilo de la red). true = bajada y leida.
bool Bajar(const std::string& datos, const std::string& nombre, Tab* t, std::string* texto_out = nullptr) {
  std::string cuerpo;
  const bool ok = Http(datos + "/" + nombre, cuerpo);
  Tab nueva;
  const bool leida = ok && TablaDe(cuerpo, &nueva);
  if (texto_out) *texto_out = leida ? cuerpo : std::string();
  if (leida) {
    *t = std::move(nueva);
    Escribir(CacheDe(nombre), cuerpo);
  }
  REXLOG_INFO("dbz3 rank: descarga {}: {}", nombre, leida ? "ok (" + std::to_string(t->filas.size()) + " filas)" : ok ? "sin tabla" : "error de red");
  return leida;
}

// Hilo de la red: envia la cola, mira recibidos.json y baja las tablas pedidas (caducan a los 5 min, como la cache de
// raw.githubusercontent.com). Se despierta cada 30 s o cuando el juego lo pide.
void Red() {
  for (;;) {
    std::vector<Pendiente> cola;
    std::vector<std::string> pedidas;
    std::string url, tema, datos;
    {
      std::unique_lock<std::mutex> lk(g_mx);
      g_cv.wait_for(lk, std::chrono::seconds(30), [] { return g_despertar; });
      g_despertar = false;
      if (!REXCVAR_GET(dbz3_ranking_online) || g_consent != 1) continue;   // R16: sin Si, cero peticiones
      cola = g_cola;
      for (const std::string& n : g_pedidas) {
        const Tab& t = g_tablas[n];
        if (t.bajada == 0 || Ahora() - t.bajada >= 300) pedidas.push_back(n);
      }
      url = Base(REXCVAR_GET(dbz3_rank_url)), tema = REXCVAR_GET(dbz3_rank_tema), datos = Base(REXCVAR_GET(dbz3_rank_datos));
    }
    const int64_t ahora = Ahora();
    bool hay_publicado = false;
    for (Pendiente& x : cola) {
      if (!x.mods_ok) {   // solo partidas sin mods de juego (la huella del online)
        const std::string mods = dbz3::ingame::ModsDePartida();
        if (!mods.empty()) {
          REXLOG_INFO("dbz3 rank: envio {}: solo local (mods de juego activos)", x.id);
          std::lock_guard<std::mutex> lk(g_mx);
          std::erase_if(g_cola, [&x](const Pendiente& o) { return o.id == x.id; });
          PonEstado(x.id, kSoloLocal);
          GuardarCola(), GuardarDatos();
          x.id.clear();
          continue;
        }
        x.mods_ok = true;
      }
      const int64_t espera = x.publicado ? 6 * 3600 : 60;   // ntfy guarda 12 h: se vuelve a mandar si la Action no la vio
      if (x.ultimo != 0 && ahora - x.ultimo < espera) {
        hay_publicado |= x.publicado;
        continue;
      }
      std::string r;
      const bool ok = !tema.empty() && Http(url + "/" + tema + "/publish?message=" + Enc(x.msg, false), r);
      REXLOG_INFO("dbz3 rank: envio {} ({}, {} bytes) a {}/{}: {}", x.id, x.modo == 'r' ? "rogue diario" : "survivors", x.msg.size(), url, tema,
                  ok ? "ok" : "sin conexion");
      std::lock_guard<std::mutex> lk(g_mx);
      for (Pendiente& o : g_cola) {
        if (o.id != x.id) continue;
        o.ultimo = ahora, o.mods_ok = true;
        o.publicado |= ok;
      }
      x.publicado |= ok;
      PonEstado(x.id, ok ? kEnviada : kSinConexion);
      GuardarCola(), GuardarDatos();
      hay_publicado |= x.publicado;
    }
    // recibidos.json: lo que la Action ya contesto (ok o el motivo). Lo que este ahi sale de la cola.
    if (hay_publicado && ahora - g_recibidos_bajado >= 60) {
      g_recibidos_bajado = ahora;
      std::string cuerpo;
      J j;
      if (Http(datos + "/recibidos.json", cuerpo) && LeerJson(cuerpo, &j)) {
        const J& ids = j["ids"];
        std::lock_guard<std::mutex> lk(g_mx);
        bool cambio = false;
        for (auto it = g_cola.begin(); it != g_cola.end();) {
          const J& r = ids[it->id.c_str()];
          if (r.t != J::kArr || r.a.empty()) { ++it; continue; }
          const std::string motivo = r.a[0].s;
          PonEstado(it->id, motivo == "ok" ? kEnTabla : kRechazada, motivo == "ok" ? std::string() : motivo);
          REXLOG_INFO("dbz3 rank: recibida {}: {}; cola {}", it->id, motivo, g_cola.size() - 1);
          it = g_cola.erase(it);
          cambio = true;
          for (const std::string& n : g_pedidas) g_tablas[n].bajada = 0;   // la tabla ya la lleva: bajarla otra vez
        }
        if (cambio) {
          GuardarCola(), GuardarDatos();
          pedidas.assign(g_pedidas.begin(), g_pedidas.end());
        }
      } else {
        REXLOG_INFO("dbz3 rank: descarga recibidos.json: {}", cuerpo.empty() ? "error de red" : "sin tabla");
      }
    }
    for (const std::string& n : pedidas) {
      Tab t;
      const bool ok = Bajar(datos, n, &t);
      std::lock_guard<std::mutex> lk(g_mx);
      Tab& d = g_tablas[n];
      if (ok) d = std::move(t);
      d.error = !ok;
      d.bajada = Ahora();
    }
  }
}

std::once_flag g_hilo;
bool TablaLocal();   // P2: tablas de prueba sin red (mas abajo)
void Arrancar() {   // con g_mx
  Cargar();
  if (TablaLocal()) return;   // pruebas: ni hilo ni red
  std::call_once(g_hilo, [] {
    std::thread(Red).detach();
    REXLOG_INFO("dbz3 rank: ranking online encendido (beta): {}/{} -> {}", Base(REXCVAR_GET(dbz3_rank_url)), REXCVAR_GET(dbz3_rank_tema),
                Base(REXCVAR_GET(dbz3_rank_datos)));
  });
}
void Despertar() {   // con g_mx
  g_despertar = true;
  g_cv.notify_all();
}
// P2 (A15): con DBZ3_TEST_SURVIVORS_RANKING=local las tablas de Survivors son de prueba (sin red ni cache): filas inventadas, una es la tuya.
bool TablaLocal() {
  static const bool v = [] {
    const char* e = std::getenv("DBZ3_TEST_SURVIVORS_RANKING");
    return e && !std::strcmp(e, "local");
  }();
  return v;
}
Tab TablaDePrueba(bool siempre) {
  static const char* const al[] = {"Kakaroto", "Prince", "Namek88", "Cellfan", "Majin_J", "Yamcha", "Tien", "Krilin_Z", "Bulma", "Gohan_K", "Mr_Satan", "Trunks_F", "Dende", "Raditz"};
  static const char* const esc[] = {"llanura", "desierto", "ruinas", "glaciar"};
  Tab t;
  t.hay = true, t.generado = Ahora() - 120, t.bajada = Ahora(), t.cierra = Ahora() + 86400;
  const int n = 14;
  t.total = siempre ? 1280 : 214;
  for (int i = 0; i < n; ++i) {
    Fila f;
    f.pos = i + 1 + (i >= 3 ? (siempre ? 8 : 2) : 0), f.j = (i * 7 + 3) % 3 * 7;   // 0, 7, 14: Goku, Vegeta y otro de B3
    f.sg = 900 - 35 * i, f.k = 2400 - 130 * i, f.n = 31 - i, f.w = i < 5, f.df = i % 3 == 0, f.e = esc[i % 4];
    f.p = PuntosSurvivors(f.sg, f.k, f.w), f.al = al[i], f.u = "prueba" + std::to_string(i), f.id = "p" + std::to_string(i), f.ver = "1.5.6", f.t = Ahora() - 3600 * (i + 1);
    t.filas.push_back(f);
  }
  t.filas[3].u = g_uid, t.filas[3].al = g_alias.empty() ? "Tu" : g_alias;
  return t;
}

// Tab `nombre` (hilo de dibujo): la de la cache al primer uso y la pide al hilo de la red si caduco.
Tab TablaPedida(const std::string& nombre) {
  std::lock_guard<std::mutex> lk(g_mx);
  Arrancar();
  if (TablaLocal() && nombre.rfind("survivors/", 0) == 0) return TablaDePrueba(nombre == "survivors/siempre.json");
  if (g_consent != 1) return {};   // tampoco cargar ni devolver cache desde llamadores nuevos
  Tab& t = g_tablas[nombre];
  if (g_pedidas.insert(nombre).second) {
    if (!t.hay) TablaDe(LeerTexto(CacheDe(nombre)), &t);
    Despertar();
  } else if (t.bajada != 0 && Ahora() - t.bajada >= 300) {
    Despertar();
  }
  return t;
}

// ---------------------------------------------------------------- el envio
std::string Armar(const Partida& p, const std::string& id) {   // con g_mx; el mismo orden que armar() de validar.py
  std::string b = "v=1&m=" + std::string(1, p.modo) + "&id=" + id + "&u=" + g_uid + "&al=" + Enc(g_alias, true) + "&ver=" +
                  Enc(dbz3::launcher::CurrentVersion(), true) + "&t=" + std::to_string(Ahora()) + "&j=" + std::to_string(p.jugador) +
                  "&sg=" + std::to_string(p.segundos) + "&p=" + std::to_string(p.puntos);
  if (p.modo == 'r') {
    std::string cartas;
    for (const dbz3::rogue::CartaVista& c : dbz3::rogue::VerCartas(&p.semilla))
      for (int i = 0; i < std::max(c.veces, 1) && cartas.size() < 300; ++i) cartas += (cartas.empty() ? "" : ",") + c.id;
    b += "&d=" + std::to_string(p.dia) + "&se=" + std::to_string(p.semilla) + "&g=" + std::to_string(p.ganados) + "&b=" +
         std::to_string(p.jefes) + "&vi=" + std::to_string(p.vida) + "&c=" + Enc(cartas, true);
  } else {
    b += "&k=" + std::to_string(p.bajas) + "&n=" + std::to_string(p.nivel) + "&w=" + std::string(p.victoria ? "1" : "0") + "&e=" +
         Enc(p.escenario, true) + "&df=" + std::to_string(p.dificultad);
  }
  return b + "&h=" + Hmac(b);
}

// Partida de la pantalla final (hilo de dibujo)
struct Actual {
  std::string clave, id;
  Partida p;
  bool alias = false, escribiendo = false, consulta = false;   // consulta = aviso de privacidad (Si / No)
  int sel = 0;
  uint16_t boton = kY;
  char buf[20] = {};
  std::vector<std::string> nombres;
} g_act;
enum Vista { kNada, kTablaVista, kFicha };
Vista g_vista = kNada, g_vuelta = kNada;   // pantalla encima (Encima) o ficha; g_vuelta = a donde vuelve la ficha
int g_cual = 0, g_periodo = 0, g_dia_menos = 0, g_cursor = 0, g_filtro = -1, g_filtro_esc = -1, g_filtro_dif = -1;   // P2: -1 = todos
bool g_mis_dias = false;
Fila g_ficha;
Perfil g_ficha_perfil;
int g_ficha_total = 0;

std::string ClaveDe(const Partida& p) {
  char b[160];
  std::snprintf(b, sizeof b, "%c|%d|%d|%d|%d|%u|%d|%d", p.modo, p.jugador, p.segundos, p.puntos, p.dia, p.semilla, p.bajas, p.nivel);
  return b;
}
void Ver(const Partida& p) {   // pantalla final nueva: id nuevo (una vez por partida)
  const std::string c = ClaveDe(p);
  if (c == g_act.clave) return;
  g_act = {};
  g_act.clave = c, g_act.p = p, g_act.id = Azar(8);
}
int EstadoDe(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_mx);
  Arrancar();
  const Enviada* e = EnvDe(id);
  return e ? e->estado : kSinEnviar;
}
void Enviar() {   // hilo de dibujo
  std::lock_guard<std::mutex> lk(g_mx);
  Arrancar();
  if (g_consent != 1 || EnvDe(g_act.id)) return;   // R16: nada a la cola sin el Si
  const std::string msg = Armar(g_act.p, g_act.id);
  g_cola.push_back({g_act.id, msg, g_act.p.modo, 0, false, false});
  g_env.push_back({g_act.id, g_act.p.modo, g_act.p.dia, g_act.p.puntos, kEnviando, {}});
  if (g_env.size() > 60) g_env.erase(g_env.begin());
  GuardarCola(), GuardarDatos();
  REXLOG_INFO("dbz3 rank: partida {} a la cola ({}, {} puntos, alias {}); cola {}", g_act.id, g_act.p.modo == 'r' ? "rogue diario" : "survivors",
              g_act.p.puntos, g_alias, g_cola.size());
  Despertar();
}
void Reintentar() {
  std::lock_guard<std::mutex> lk(g_mx);
  for (Pendiente& x : g_cola) x.ultimo = 0;
  Despertar();
}
bool AliasValido(const std::string& s) {
  if (s.size() < 3 || s.size() > 16) return false;
  for (unsigned char c : s)
    if (!std::isalnum(c) && c != '_' && c != '-') return false;
  std::string l = s;
  std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  static const char* const kProhibidas[] = {"puta", "puto", "mierda", "polla", "cono", "nazi", "hitler", "fuck", "shit", "bitch",
                                            "cunt", "nigg", "fag", "retard", "maricon", "subnormal", "admin", "moderador"};   // = validar.py
  for (const char* w : kProhibidas)
    if (l.find(w) != std::string::npos) return false;
  return true;
}
std::vector<std::string> NombresSugeridos() {
  std::string u;
  char b[64] = {};
  DWORD n = sizeof b;
  if (GetUserNameA(b, &n))
    for (const char* c = b; *c && u.size() < 12; ++c)
      if (std::isalnum(static_cast<unsigned char>(*c))) u += *c;
  std::string uid;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    uid = g_uid;
  }
  const unsigned x = unsigned(std::strtoul(uid.substr(0, 4).c_str(), nullptr, 16)) % 10000;
  char namek[24], saiyan[24];
  std::snprintf(namek, sizeof namek, "Namek_%04u", (x * 7 + 3) % 10000);
  std::snprintf(saiyan, sizeof saiyan, "Saiyan_%04u", x);
  // el nombre de usuario de Windows solo se propone (no sale del PC si no se elige)
  return {u.size() >= 2 && AliasValido(u + "_B3") ? u + "_B3" : std::string(namek), saiyan};
}
void PonerAlias(const std::string& a) {
  {
    std::lock_guard<std::mutex> lk(g_mx);
    g_alias = a;
    GuardarDatos();
  }
  REXLOG_INFO("dbz3 rank: alias elegido ({} letras)", a.size());
  g_act.alias = g_act.escribiendo = false;
  Enviar();
}

// ---------------------------------------------------------------- dibujo (como rogue_resultado.cpp)
ImFont* Fuente() {
  const auto& f = dbz3::launcher::ui::GetFonts();
  return f.menu ? f.menu : ImGui::GetFont();
}
float Ancho(float size, const char* s) { return Fuente()->CalcTextSizeA(size, FLT_MAX, 0, s).x; }
float Ajustar(float size, const char* s, float max_w) {
  const float w = Ancho(size, s);
  return max_w > 0 && w > max_w ? size * max_w / w : size;
}
void Izq(ImDrawList* dl, ImVec2 p, float size, const std::string& s, bool oro, float max_w = 0) {
  size = Ajustar(size, s.c_str(), max_w);
  S::MenuTextB3(dl, ImVec2(p.x + Ancho(size, s.c_str()) * 0.5f, p.y), size, s.c_str(), oro, false);
}
void Der(ImDrawList* dl, ImVec2 p, float size, const std::string& s, bool oro, float max_w = 0) {
  size = Ajustar(size, s.c_str(), max_w);
  S::MenuTextB3(dl, ImVec2(p.x - Ancho(size, s.c_str()) * 0.5f, p.y), size, s.c_str(), oro, false);
}
void Cen(ImDrawList* dl, ImVec2 p, float size, const std::string& s, bool oro, float max_w = 0) {
  S::MenuTextB3(dl, p, Ajustar(size, s.c_str(), max_w), s.c_str(), oro, false);
}
void Color(ImDrawList* dl, ImVec2 p, float size, const std::string& s, ImU32 col, float u, int alin = 0, float max_w = 0) {   // alin 0 izq, 1 der
  size = Ajustar(size, s.c_str(), max_w);
  const float w = Ancho(size, s.c_str());
  const ImVec2 o(alin == 1 ? p.x - w : p.x, p.y - size * 0.5f);
  dl->AddText(Fuente(), size, ImVec2(o.x + 1.5f * u, o.y + 1.5f * u), IM_COL32(0, 0, 0, 220), s.c_str());
  dl->AddText(Fuente(), size, o, col, s.c_str());
}
constexpr ImU32 kVerde = IM_COL32(124, 232, 140, 255), kCian = IM_COL32(120, 220, 255, 255), kRojo = IM_COL32(255, 110, 90, 255),
                kAmarillo = IM_COL32(255, 214, 64, 255), kNaranja = IM_COL32(232, 140, 32, 255);
std::string Num(int n) { return std::to_string(n); }
std::string Miles(int n) {   // 1.204 (el punto de miles de las maquetas)
  std::string s = std::to_string(std::abs(n)), o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i && (s.size() - i) % 3 == 0) o += '.';
    o += s[i];
  }
  return n < 0 ? "-" + o : o;
}
std::string Reloj(int sg) {
  char b[16];
  std::snprintf(b, sizeof b, "%d:%02d", sg / 60, sg % 60);
  return b;
}
std::string FechaCorta(int dia) {
  static const char* const kMes[5][12] = {
      {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"janv", "févr", "mars", "avr", "mai", "juin", "juil", "août", "sept", "oct", "nov", "déc"},
      {"Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"},
      {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"}};
  const std::time_t t = std::time_t(int64_t(dia) * 86400);
  std::tm g{};
  gmtime_s(&g, &t);
  const char* l = M::L("0", "1", "2", "3", "4");
  return Num(g.tm_mday) + " " + kMes[std::clamp(l[0] - '0', 0, 4)][g.tm_mon];
}
void Pastilla(ImDrawList* dl, ImVec2 c0, float u, const std::string& s, ImU32 col) {   // "o Enviada": izquierda en c0, centro vertical
  const float h = 22 * u, w = Ancho(15 * u, s.c_str()) + 26 * u;
  dl->AddRectFilled(ImVec2(c0.x, c0.y - h * 0.5f), ImVec2(c0.x + w, c0.y + h * 0.5f), IM_COL32(10, 26, 36, 235), h * 0.5f);
  dl->AddRect(ImVec2(c0.x, c0.y - h * 0.5f), ImVec2(c0.x + w, c0.y + h * 0.5f), col, h * 0.5f, 0, 1.5f * u);
  dl->AddCircleFilled(ImVec2(c0.x + 10 * u, c0.y), 3 * u, col);
  dl->AddText(Fuente(), 15 * u, ImVec2(c0.x + 17 * u, c0.y - 7.5f * u), col, s.c_str());
}
float AnchoPastilla(float u, const std::string& s) { return Ancho(15 * u, s.c_str()) + 26 * u; }
void Rueda(ImDrawList* dl, ImVec2 c, float r, float u) {   // "enviando"
  const float t = float(ImGui::GetTime());
  for (int i = 0; i < 8; ++i) {
    const float a = float(i) * 0.785398f + t * 4.0f;
    dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), 2.2f * u, IM_COL32(120, 220, 255, 60 + 24 * i));
  }
}
void Caja(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, bool brillo = false, ImU32 borde = IM_COL32(90, 160, 200, 200)) {   // fila azul
  dl->AddRectFilled(p0, p1, brillo ? IM_COL32(70, 150, 205, 245) : IM_COL32(34, 98, 138, 235), 8 * u);
  dl->AddRect(p0, p1, borde, 8 * u, 0, (brillo ? 2.0f : 1.2f) * u);
}
void Cara(ImDrawList* dl, int id, ImVec2 c, float r) {   // retrato redondo con la cara del juego
  dl->AddCircleFilled(c, r, IM_COL32(20, 44, 66, 255), 40);
  if (const S::Pic cara = S::Img(("caras/" + Num(id)).c_str()))
    dl->AddImageRounded(cara.tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
  dl->AddCircle(c, r, kNaranja, 40, 1.5f);
}
void Medalla(ImDrawList* dl, int pos, ImVec2 c, float r) {   // medallas del juego (las del Ranking de modos.cpp)
  static const char* const k[] = {"retos/medalla_3_oro", "retos/medalla_2_plata", "retos/medalla_1_bronce"};
  if (pos >= 1 && pos <= 3)
    if (const S::Pic md = S::Img(k[pos - 1])) S::DrawPic(dl, md, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
}
void Beta(ImDrawList* dl, ImVec2 p, float u) {   // "BETA" rojo detras de un titulo (izquierda en p.x)
  Color(dl, p, 15 * u, "BETA", IM_COL32(255, 70, 60, 255), u);
}
const char* NombreTramo(int tramo) {
  static const char* const k[5][5] = {{"Guerrero Z", "Z Fighter", "Guerrier Z", "Z-Kämpfer", "Guerriero Z"},
                                      {"Super Saiyan", "Super Saiyan", "Super Saiyan", "Super-Saiyajin", "Super Saiyan"},
                                      {"Kaio", "King Kai", "Maître Kaïo", "Meister Kaio", "Re Kaioh"},
                                      {"Kaio-shin", "Supreme Kai", "Kaïo Shin", "Kaioshin", "Kaioshin"},
                                      {"Shenron", "Shenron", "Shenron", "Shenlong", "Shenron"}};
  const auto& t = k[std::clamp(tramo, 0, 4)];
  return M::L(t[0], t[1], t[2], t[3], t[4]);
}
int TramoDe(int pos, int total) {   // por percentil: 1 % Shenron, 5 % Kaio-shin, 20 % Kaio, 50 % Super Saiyan
  if (total <= 0 || pos <= 0) return 0;
  const float f = float(pos) / float(total);
  return f <= 0.01f || pos == 1 ? 4 : f <= 0.05f ? 3 : f <= 0.2f ? 2 : f <= 0.5f ? 1 : 0;
}
std::string Tu() { return M::L("Tú", "You", "Toi", "Du", "Tu"); }

// R16: texto en los 7 idiomas (es, en, fr, de, it, pt, ja).
const char* L7(const char* es, const char* en, const char* fr, const char* de, const char* it, const char* pt, const char* ja) {
  const char* c = dbz3::i18n::LangCode();
  return !std::strcmp(c, "pt") ? pt : !std::strcmp(c, "ja") ? ja : M::L(es, en, fr, de, it);
}
// R16: el aviso que ven las tablas cuando aun no hay un Si (7 idiomas).
const char* TextoRankingApagado() {
  return L7("Ránking online sin activar: no se descarga ni se envía nada. Al acabar una partida, pulsa Enviar y responde Sí.",
            "Online ranking is off: nothing is downloaded or sent. After a run, press Send and answer Yes.",
            "Classement en ligne désactivé : rien n'est téléchargé ni envoyé. Après une partie, appuie sur Envoyer et réponds Oui.",
            "Online-Rangliste aus: nichts wird geladen oder gesendet. Nach einem Run Senden drücken und mit Ja antworten.",
            "Classifica online non attiva: non si scarica né si invia nulla. A fine partita premi Invia e rispondi Sì.",
            "Ranking online desativado: nada é baixado nem enviado. Ao fim de uma partida, pressione Enviar e responda Sim.",
            "オンラインランキングはオフです。何も送受信しません。プレイ後に「送信」を押して「はい」を選んでください。");
}
void Responder(int r) {   // 1 = Si, 2 = No; guarda la respuesta
  bool sin_alias;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    Arrancar();
    g_consent = r;
    GuardarDatos();
    sin_alias = g_alias.empty();
  }
  g_act.consulta = false;
  REXLOG_INFO("dbz3 rank: consentimiento: {}", r == 1 ? "SI" : "NO (no se envia nada)");
  if (r == 1) {
    if (sin_alias) g_act.alias = true, g_act.sel = 0;
    else Enviar();
  }
}

// Tab de cada pagina: 0 survivors (semana o siempre), 1 rogue diario (hoy o ayer)
std::string NombreTabla(int cual) {
  if (cual == 0) return g_periodo == 0 ? "survivors/semana.json" : "survivors/siempre.json";
  return NombreDia(dbz3::rogue::DiaUtc() - g_dia_menos);
}
std::string MiUid() {
  std::lock_guard<std::mutex> lk(g_mx);
  Arrancar();
  return g_uid;
}
std::string MiAlias() {
  std::lock_guard<std::mutex> lk(g_mx);
  return g_alias;
}
const Fila* Mia(const Tab& t, const std::string& uid) {
  for (const Fila& f : t.filas)
    if (f.u == uid) return &f;
  return nullptr;
}
void AbrirFicha(const Tab& t, const Fila& f, int cual) {
  g_ficha = f, g_ficha_total = t.total, g_cual = cual;
  const auto it = t.jug.find(f.u);
  g_ficha_perfil = it != t.jug.end() ? it->second : Perfil{};
  if (g_vista != kFicha) g_vuelta = g_vista;
  g_vista = kFicha;
  REXLOG_INFO("dbz3 rank: ficha de la partida {} (puesto {})", f.id, f.pos);
}
std::string Edad(int64_t generado) {
  const int64_t m = std::max<int64_t>(0, (Ahora() - generado) / 60);
  if (m < 60) return std::string(M::L("Al día · ", "Up to date · ", "À jour · ", "Aktuell · ", "Aggiornato · ")) + Num(int(m)) + " min";
  return std::string(M::L("Hace ", "", "Il y a ", "Vor ", "")) + Num(int(m / 60)) + M::L(" h", " h ago", " h", " h", " h fa");
}

// ---- 03: ficha de una partida
void Ficha(ImDrawList* dl, const S::Area& a, uint16_t p, const Tab& t) {
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  // LB/RB: la anterior / la siguiente de la tabla
  if (p & (kLb | kRb)) {
    for (size_t i = 0; i < t.filas.size(); ++i) {
      if (t.filas[i].id != g_ficha.id) continue;
      const size_t j = p & kLb ? (i + t.filas.size() - 1) % t.filas.size() : (i + 1) % t.filas.size();
      AbrirFicha(t, t.filas[j], g_cual);
      break;
    }
  }
  const Fila& f = g_ficha;
  const Perfil& pf = g_ficha_perfil;
  const bool r = f.m == 'r';
  S::Backdrop(dl, a, S::Img("hub/fondo") ? "hub/fondo" : nullptr);
  S::Sides(dl, a);
  dl->AddRectFilled(S::GuestImage(ImGui::GetIO()).p0, S::GuestImage(ImGui::GetIO()).p1, IM_COL32(6, 20, 30, 120));
  S::BannerBeta(dl, a, r ? "Dragon Rogue" : "Dragon Budokai Survivors");
  // izquierda: el jugador
  S::Frame(dl, P(47, 127), P(547, 667), a.s);
  Cara(dl, f.j, P(113, 187), 33 * u);
  Izq(dl, P(160, 173), 27 * u, f.al, true, 360 * u);
  const std::string donde = std::string(M::L("Puesto ", "Rank ", "Place ", "Platz ", "Posto ")) + Miles(f.pos) + "  ·  " +
                            (r ? M::L("Rogue diario", "Daily Rogue", "Rogue du jour", "Tages-Rogue", "Rogue giornaliero")
                               : (g_periodo == 0 ? M::L("Survivors · semana", "Survivors · week", "Survivors · semaine", "Survivors · Woche", "Survivors · settimana")
                                                 : M::L("Survivors · siempre", "Survivors · all time", "Survivors · toujours", "Survivors · ewig", "Survivors · sempre")));
  Izq(dl, P(160, 209), 17 * u, donde, false, 370 * u);
  dl->AddLine(P(62, 233), P(532, 233), kNaranja, 1.5f * u);
  Medalla(dl, 1, P(87, 265), 15 * u);
  Color(dl, P(110, 265), 21 * u, std::string(M::L("Rango: ", "Rank: ", "Rang : ", "Rang: ", "Grado: ")) + NombreTramo(TramoDe(f.pos, g_ficha_total)), kCian, u);
  const std::pair<std::string, std::string> filas[] = {
      {M::L("Mejor Survivors", "Best Survivors", "Meilleur Survivors", "Bester Survivors", "Miglior Survivors"), pf.ms ? Miles(pf.ms) : "-"},
      {M::L("Mejor Rogue diario", "Best daily Rogue", "Meilleur Rogue du jour", "Bester Tages-Rogue", "Miglior Rogue giornaliero"), pf.mr ? Miles(pf.mr) : "-"},
      {M::L("Días seguidos", "Days in a row", "Jours d'affilée", "Tage in Folge", "Giorni di fila"), Num(pf.ra)},
      {M::L("Partidas enviadas", "Runs sent", "Parties envoyées", "Gesendete Runs", "Partite inviate"), Num(pf.en)},
      {M::L("Luchador favorito", "Favorite fighter", "Combattant favori", "Lieblingskämpfer", "Lottatore preferito"), M::Nombre(pf.en ? pf.fav : f.j)}};
  float y = 303;
  for (int i = 0; i < 5; ++i) {
    Caja(dl, P(64, y), P(513, y + 40), u);
    Izq(dl, P(80, y + 20), 20 * u, filas[i].first, false, 260 * u);
    Der(dl, P(500, y + 20), 20 * u, filas[i].second, i < 2, 160 * u);
    y += 48;
  }
  Izq(dl, P(71, 560), 15 * u, M::L("Solo se publica el alias, el luchador", "Only the alias, the fighter", "Seuls l'alias, le combattant", "Nur Alias, Kämpfer", "Si pubblicano solo l'alias, il lottatore"), false);
  Izq(dl, P(71, 582), 15 * u, M::L("y la puntuación. Nada más.", "and the score are published. Nothing else.", "et le score sont publiés. Rien d'autre.", "und Punkte werden veröffentlicht. Sonst nichts.", "e il punteggio. Nient'altro."), false);
  // derecha: la partida
  S::Frame(dl, P(607, 127), P(1247, 560), a.s);
  const std::string tit = r ? std::string(M::L("Partida del ", "Run of ", "Partie du ", "Run vom ", "Partita del ")) + FechaCorta(f.d)
                            : std::string(M::L("Partida", "Run", "Partie", "Run", "Partita")) + "  ·  " + M::Nombre(f.j);
  Izq(dl, P(640, 169), 25 * u, tit, true, 420 * u);
  Der(dl, P(1207, 169), 15 * u, std::string(M::L("versión ", "version ", "version ", "Version ", "versione ")) + f.ver, false);
  dl->AddLine(P(622, 200), P(1232, 200), kNaranja, 1.5f * u);
  std::vector<std::pair<std::string, std::string>> d;
  if (r) {
    d = {{std::string(M::L("Pisos  ", "Floors  ", "Étages  ", "Etagen  ", "Piani  ")) + Num(f.g) + " x 100", Miles(f.g * 100)},
         {std::string(M::L("Jefes  ", "Bosses  ", "Boss  ", "Bosse  ", "Boss  ")) + Num(f.b) + " x 500", Miles(f.b * 500)},
         {std::string(M::L("Vida final  ", "Final health  ", "Vie finale  ", "Restleben  ", "Vita finale  ")) + Num(f.vi) + " / 10", Miles(f.vi / 10)},
         {std::string(M::L("Tiempo  ", "Time  ", "Temps  ", "Zeit  ", "Tempo  ")) + Num(f.sg) + " s", "-" + Miles(f.sg)}};
  } else {
    d = {{std::string(M::L("Bajas  ", "Kills  ", "Éliminés  ", "Besiegte  ", "Abbattuti  ")) + Miles(f.k) + " x 10", Miles(f.k * 10)},
         {std::string(M::L("Tiempo  ", "Time  ", "Temps  ", "Zeit  ", "Tempo  ")) + Reloj(f.sg) + " x 30", Miles(f.sg * 30)},
         {M::L("Victoria", "Victory", "Victoire", "Sieg", "Vittoria"), f.w ? "3.000" : "-"},
         {M::L("Nivel", "Level", "Niveau", "Stufe", "Livello"), Num(f.n)}};
  }
  y = 216;
  for (const auto& [n, v] : d) {
    Caja(dl, P(629, y), P(1213, y + 37), u);
    Izq(dl, P(648, y + 18.5f), 20 * u, n, false, 380 * u);
    Der(dl, P(1196, y + 18.5f), 20 * u, v, false, 160 * u);
    y += 46.7f;
  }
  Izq(dl, P(640, 419), 23 * u, M::L("Puntos", "Points", "Points", "Punkte", "Punti"), true);
  R::DibujarCifras(dl, true, f.p, P(1140, 440), 42 * u);
  if (r) {
    Izq(dl, P(640, 469), 19 * u, M::L("Cartas", "Cards", "Cartes", "Karten", "Carte"), false);
    float x = 720;
    for (size_t i = 0; i < f.c.size() && i < 6; ++i, x += 73) {
      if (!R::DibujarNueve(dl, "ui_casilla_mejora", P(x, 441), P(x + 60, 501), 0.22f * u)) R::DibujarImagen(dl, R::Ui("ui_casilla_mejora"), P(x, 441), P(x + 60, 501));
      R::DibujarAjustada(dl, R::Icono(f.c[i].c_str()), P(x + 6, 447), P(x + 54, 495));
    }
    if (f.c.empty()) Izq(dl, P(720, 469), 17 * u, "-", false);
  }
  // abajo: la firma (la Action solo publica las de firma valida)
  dl->AddRectFilled(P(593, 607), P(1247, 653), IM_COL32(8, 24, 36, 225), 10 * u);
  const std::string firma = M::L("Firma correcta", "Valid signature", "Signature valide", "Gültige Signatur", "Firma valida");
  Pastilla(dl, P(613, 630), u, firma, kVerde);
  char cuando[96] = "";
  if (f.t > 0) {
    const std::time_t tt = std::time_t(f.t);
    std::tm g{};
    gmtime_s(&g, &tt);
    std::snprintf(cuando, sizeof cuando, "%s %s %s %02d:%02d UTC  ·  %s %s", M::L("Enviada el", "Sent on", "Envoyée le", "Gesendet am", "Inviata il"),
                  FechaCorta(int(f.t / 86400)).c_str(), M::L("a las", "at", "à", "um", "alle"), g.tm_hour, g.tm_min,
                  M::L("versión", "version", "version", "Version", "versione"), f.ver.c_str());
  }
  Izq(dl, ImVec2(P(613, 0).x + AnchoPastilla(u, firma) + 14 * u, P(0, 630).y), 16 * u, cuando, false, 440 * u);
  D::Help(dl, a, {{"LB", M::L("Anterior", "Previous", "Précédent", "Zurück", "Precedente")},
                  {"RB", M::L("Siguiente", "Next", "Suivant", "Weiter", "Successivo")},
                  {"A", M::L("Ver ránking", "View ranking", "Voir le classement", "Rangliste", "Vedi classifica")},
                  {"B", M::L("Volver", "Back", "Retour", "Zurück", "Indietro")}});
}

const char* NombreEscenario(const std::string& id) {   // los escenarios de survivors.toml; otro id se ve tal cual
  if (id == "llanura") return M::L("Llanura", "Plains", "Plaine", "Ebene", "Pianura");
  if (id == "desierto") return M::L("Desierto", "Desert", "Désert", "Wüste", "Deserto");
  if (id == "ruinas") return M::L("Ruinas", "Ruins", "Ruines", "Ruinen", "Rovine");
  if (id == "glaciar") return M::L("Glaciar", "Glacier", "Glacier", "Gletscher", "Ghiacciaio");
  return id.c_str();
}

// ---- 01: tabla mundial (y Rogue diario a toda pantalla)
void DibujarTabla(ImDrawList* dl, const S::Area& a, float t, uint16_t p, int cual) {
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  const bool r = cual == 1;
  if (p & (kLb | kRb)) {
    if (r) g_dia_menos = 1 - g_dia_menos;
    else g_periodo = 1 - g_periodo;
    g_cursor = 0, g_filtro = -1, g_filtro_esc = g_filtro_dif = -1;
  }
  const Tab tb = TablaPedida(NombreTabla(cual));
  const std::string uid = MiUid();
  // filtro por luchador (X): los que hay en la tabla
  std::vector<int> luch;
  for (const Fila& f : tb.filas)
    if (std::find(luch.begin(), luch.end(), f.j) == luch.end()) luch.push_back(f.j);
  if (p & kX) {
    const auto it = std::find(luch.begin(), luch.end(), g_filtro);
    g_filtro = it == luch.end() ? (luch.empty() ? -1 : luch[0]) : (it + 1 == luch.end() ? -1 : *(it + 1));
    g_cursor = 0;
  }
  // P2 (A15): escenario (izquierda / derecha) y dificultad (Start) de Survivors, sobre las filas de la tabla
  std::vector<std::string> escs;
  for (const Fila& f : tb.filas)
    if (!r && !f.e.empty() && std::find(escs.begin(), escs.end(), f.e) == escs.end()) escs.push_back(f.e);
  if (!r && (p & (kLeft | kRight)) && !escs.empty()) {
    const int ne = int(escs.size());
    if (p & kRight) g_filtro_esc = g_filtro_esc + 1 >= ne ? -1 : g_filtro_esc + 1;
    else g_filtro_esc = g_filtro_esc - 1 < -1 ? ne - 1 : g_filtro_esc - 1;
    g_cursor = 0;
  }
  if (!r && (p & 0x0010)) g_filtro_dif = g_filtro_dif >= 1 ? -1 : g_filtro_dif + 1, g_cursor = 0;
  if (g_filtro_esc >= int(escs.size())) g_filtro_esc = -1;
  std::vector<const Fila*> v;
  for (const Fila& f : tb.filas)
    if ((g_filtro < 0 || f.j == g_filtro) && (r || g_filtro_esc < 0 || f.e == escs[size_t(g_filtro_esc)]) && (r || g_filtro_dif < 0 || f.df == g_filtro_dif)) v.push_back(&f);
  const int n = int(v.size());
  if (p & kUp) g_cursor = std::max(g_cursor - 1, 0);
  if (p & kDown) g_cursor = std::min(g_cursor + 1, std::max(n - 1, 0));
  const Fila* mia = Mia(tb, uid);
  if ((p & kY) && mia)
    for (int i = 0; i < n; ++i)
      if (v[size_t(i)] == mia) g_cursor = i;
  g_cursor = std::clamp(g_cursor, 0, std::max(n - 1, 0));
  if ((p & kA) && n > 0) AbrirFicha(tb, *v[size_t(g_cursor)], cual);
  // titulo y selector de pagina (arriba a la derecha, como la maqueta 01)
  S::BannerBeta(dl, a, M::L("Ranking", "Ranking", "Classement", "Rangliste", "Classifica"));
  S::Frame(dl, P(712, 22), P(1133, 105), a.s);
  Cen(dl, ImVec2((P(712, 0).x + P(1133, 0).x) * 0.5f, P(0, 64).y), 24 * u, TituloTabla(cual), false, 380 * u);
  S::Arrows(dl, P(680, 22), P(1165, 105), 40 * a.s, t);
  // marco de la tabla
  S::Frame(dl, P(437, 127), P(1250, 518), a.s);
  const float yh = 155;
  Izq(dl, P(462, yh), 17 * u, r ? M::L("Día", "Day", "Jour", "Tag", "Giorno") : M::L("Periodo", "Period", "Période", "Zeitraum", "Periodo"), true);
  Izq(dl, P(535, yh), 17 * u,
      r ? (g_dia_menos ? M::L("Ayer", "Yesterday", "Hier", "Gestern", "Ieri") : M::L("Hoy", "Today", "Aujourd'hui", "Heute", "Oggi"))
        : (g_periodo ? M::L("Siempre", "All time", "Toujours", "Ewig", "Sempre") : M::L("Esta semana", "This week", "Cette semaine", "Diese Woche", "Questa settimana")),
      false, 120 * u);
  if (!r) {   // P2 (A15): luchador, escenario y dificultad (la version de la tabla no aporta nada)
    const char* e = g_filtro_esc < 0 ? M::L("Todos", "All", "Tous", "Alle", "Tutti") : NombreEscenario(escs[size_t(g_filtro_esc)]);
    const char* df = g_filtro_dif < 0 ? M::L("Todas", "All", "Toutes", "Alle", "Tutte") : g_filtro_dif ? M::L("Difícil", "Hard", "Difficile", "Schwer", "Difficile") : M::L("Normal", "Normal", "Normal", "Normal", "Normale");
    Izq(dl, P(655, yh), 15 * u, M::L("Luchador", "Fighter", "Combattant", "Kämpfer", "Lottatore"), true);
    Izq(dl, P(722, yh), 16 * u, g_filtro < 0 ? M::L("Todos", "All", "Tous", "Alle", "Tutti") : M::Nombre(g_filtro), false, 58 * u);
    Izq(dl, P(785, yh), 15 * u, M::L("Escenario", "Stage", "Scène", "Stufe", "Scenario"), true);
    Izq(dl, P(850, yh), 16 * u, e, false, 62 * u);
    Izq(dl, P(920, yh), 15 * u, M::L("Dific.", "Diff.", "Diff.", "Schw.", "Diff."), true);
    Izq(dl, P(965, yh), 16 * u, df, false, 55 * u);
  } else {
    Izq(dl, P(705, yh), 17 * u, M::L("Luchador", "Fighter", "Combattant", "Kämpfer", "Lottatore"), true);
    Izq(dl, P(793, yh), 17 * u, g_filtro < 0 ? M::L("Todos", "All", "Tous", "Alle", "Tutti") : M::Nombre(g_filtro), false, 130 * u);
    Izq(dl, P(955, yh), 17 * u, M::L("Versión", "Version", "Version", "Version", "Versione"), true);
    Izq(dl, P(1025, yh), 17 * u, dbz3::launcher::CurrentVersion(), false);
  }
  const std::string edad = tb.hay ? Edad(tb.generado) : tb.error ? M::L("Sin conexión", "Offline", "Hors ligne", "Offline", "Offline")
                                                               : M::L("Cargando...", "Loading...", "Chargement...", "Lädt...", "Caricamento...");
  Pastilla(dl, ImVec2(P(1238, 0).x - AnchoPastilla(u, edad), P(0, yh).y), u, edad, tb.hay ? kVerde : tb.error ? kRojo : kCian);
  // cabecera de columnas
  const float yc = 192;
  const char* c3 = r ? M::L("Pisos", "Floors", "Étages", "Etagen", "Piani") : M::L("Tiempo", "Time", "Temps", "Zeit", "Tempo");
  const char* c4 = r ? M::L("Jefes", "Bosses", "Boss", "Bosse", "Boss") : M::L("Bajas", "Kills", "Éliminés", "Besiegte", "Abbattuti");
  Cen(dl, P(501, yc), 15 * u, M::L("Puesto", "Rank", "Rang", "Platz", "Posto"), false);
  Izq(dl, P(627, yc), 15 * u, M::L("Jugador", "Player", "Joueur", "Spieler", "Giocatore"), false);
  Izq(dl, P(868, yc), 15 * u, M::L("Luchador", "Fighter", "Combattant", "Kämpfer", "Lottatore"), false);
  Cen(dl, P(995, yc), 15 * u, c3, false);
  Cen(dl, P(1091, yc), 15 * u, c4, false);
  Der(dl, P(1225, yc), 15 * u, M::L("Puntos", "Points", "Points", "Punkte", "Punti"), false);
  auto fila = [&](const Fila& f, float y, bool cursor, bool yo) {
    Caja(dl, P(460, y - 16), P(1228, y + 16), u, cursor, yo ? kAmarillo : cursor ? IM_COL32(220, 240, 255, 255) : IM_COL32(90, 160, 200, 200));
    if (f.pos <= 3) Medalla(dl, f.pos, P(501, y), 14 * u);
    else Cen(dl, P(501, y), 19 * u, Miles(f.pos), false, 80 * u);
    Cara(dl, f.j, P(571, y), 14 * u);
    Izq(dl, P(627, y), 19 * u, yo ? Tu() + "  ·  " + f.al : f.al, yo, 230 * u);
    Izq(dl, P(868, y), 19 * u, M::Nombre(f.j), false, 110 * u);
    Cen(dl, P(995, y), 19 * u, r ? Num(f.g) : Reloj(f.sg), false);
    Cen(dl, P(1091, y), 19 * u, r ? Num(f.b) : Miles(f.k), false);
    Der(dl, P(1215, y), 19 * u, Miles(f.p), true);
  };
  const int primero = std::clamp(g_cursor - 3, 0, std::max(n - 7, 0));
  for (int i = 0; i < 7 && primero + i < n; ++i) {
    const Fila& f = *v[size_t(primero + i)];
    fila(f, 222 + 37.3f * float(i), primero + i == g_cursor, &f == mia);
  }
  if (n == 0)
    Cen(dl, P(843, 300), 21 * u, !tb.hay && !tb.error ? M::L("Cargando el ránking...", "Loading the ranking...", "Chargement du classement...", "Rangliste lädt...", "Caricamento della classifica...")
                                  : tb.error && !tb.hay ? M::L("Sin conexión: se verá al volver la red.", "Offline: it will show when the network is back.", "Hors ligne : il s'affichera au retour du réseau.", "Offline: erscheint, sobald das Netz zurück ist.", "Offline: si vedrà al ritorno della rete.")
                                  : M::L("Aún no hay partidas", "No runs yet", "Pas encore de parties", "Noch keine Runs", "Ancora nessuna partita"), false, 700 * u);
  // mi fila, fija abajo
  dl->AddLine(P(460, 466), P(1228, 466), IM_COL32(232, 140, 32, 160), 1.2f * u);
  bool mia_en_vista = false;   // P2: si mi fila ya se ve en la lista, no se repite abajo (solo Survivors)
  if (!r)
    for (int i = 0; i < 7 && primero + i < n; ++i) mia_en_vista |= v[size_t(primero + i)] == mia;
  if (mia && !mia_en_vista) fila(*mia, 491, v.size() > size_t(g_cursor) && v[size_t(g_cursor)] == mia, true);
  else if (!mia) Izq(dl, P(480, 491), 17 * u, Tu() + "  ·  " + (MiAlias().empty() ? std::string("-") : MiAlias()) + "   " +
                                    M::L("(aún sin partida en esta tabla)", "(no run in this table yet)", "(pas encore de partie ici)", "(noch kein Run hier)", "(ancora nessuna partita qui)"), false, 700 * u);
}
}  // namespace

// ---------------------------------------------------------------- API
bool Activo() { return REXCVAR_GET(dbz3_ranking_online); }

uint16_t Entrada(uint16_t pulsado, const Partida& p, uint16_t boton) {
  if (!Activo() || p.modo == 0) return pulsado;
  Ver(p);
  g_act.boton = boton;
  if (g_act.escribiendo) return 0;   // el teclado es del cuadro del alias (Intro / Esc)
  if (g_act.consulta) {   // aviso de privacidad (Si / No): modal, ninguna otra tecla de la pantalla pasa
    if (pulsado & kA) Responder(1);
    else if (pulsado & kB) Responder(2);
    return 0;
  }
  if (g_act.alias) {
    if (g_act.nombres.empty()) g_act.nombres = NombresSugeridos();
    const int n = int(g_act.nombres.size()) + 1;
    if (pulsado & kLeft) g_act.sel = (g_act.sel + n - 1) % n;
    if (pulsado & kRight) g_act.sel = (g_act.sel + 1) % n;
    if (pulsado & kA) {
      if (g_act.sel < int(g_act.nombres.size())) PonerAlias(g_act.nombres[size_t(g_act.sel)]);
      else g_act.escribiendo = true, g_act.buf[0] = 0;
    }
    if (pulsado & kB) {
      g_act.alias = false;
      REXLOG_INFO("dbz3 rank: alias: ahora no (no se envia)");
    }
    return uint16_t(pulsado & ~(kA | kB | kLeft | kRight | boton));
  }
  if (pulsado & boton) {
    const int e = EstadoDe(g_act.id);
    if (e == kSinEnviar) {
      if (g_consent != 1) {   // primera vez, o antes dijo No: se pregunta (asi se puede cambiar la respuesta)
        g_act.consulta = true;
        REXLOG_INFO("dbz3 rank: aviso de privacidad ({})", g_consent == 2 ? "antes dijo No" : "primera vez");
        return 0;   // consumir tambien botones simultaneos al abrir el aviso
      } else if (MiAlias().empty()) {
        g_act.alias = true, g_act.sel = 0;
        REXLOG_INFO("dbz3 rank: alias: primera vez");
      } else {
        Enviar();
      }
    } else if (e == kSinConexion) {
      Reintentar();
    } else if (e == kEnviada || e == kEnTabla) {
      g_vista = kTablaVista, g_cual = p.modo == 'r' ? 1 : 0, g_cursor = 0, g_filtro = -1, g_dia_menos = 0, g_periodo = 0;
    }
    pulsado = uint16_t(pulsado & ~boton);
  }
  return pulsado;
}

const char* EtiquetaBoton() {
  if (!Activo() || g_act.clave.empty() || g_act.alias || g_act.consulta) return nullptr;
  switch (EstadoDe(g_act.id)) {
    case kSinEnviar: return M::L("Enviar al ránking", "Send to ranking", "Envoyer au classement", "An Rangliste senden", "Invia in classifica");
    case kSinConexion: return M::L("Reintentar", "Retry", "Réessayer", "Erneut", "Riprova");
    case kEnviada:
    case kEnTabla: return M::L("Ver ránking", "View ranking", "Voir le classement", "Rangliste", "Vedi classifica");
    default: return nullptr;
  }
}
bool EligiendoAlias() { return Activo() && g_act.alias; }
bool Consultando() { return Activo() && g_act.consulta; }

void Bloque(ImDrawList* dl, ImVec2 p0, float w, float u, const Partida& p) {
  if (!Activo() || p.modo == 0) return;
  Ver(p);
  auto Q = [p0, u](float x, float y) { return ImVec2(p0.x + x * u, p0.y + y * u); };
  const std::string tit = M::L("Ránking mundial", "World ranking", "Classement mondial", "Weltrangliste", "Classifica mondiale");
  Izq(dl, Q(0, 0), 23 * u, tit, true);
  Beta(dl, Q(Ancho(23 * u, tit.c_str()) / u + 14, 0), u);
  if (g_act.consulta) {   // R16: aviso de privacidad (que se envia) con Si / No
    const ImVec2 pantalla = ImGui::GetIO().DisplaySize;
    const float escala = pantalla.y / 720.0f;
    const float margen = pantalla.x * 0.12f, ancho = pantalla.x - 2 * margen;
    dl->AddRectFilled(ImVec2(0, 0), pantalla, IM_COL32(0, 0, 0, 190));
    dl->AddRectFilled(ImVec2(margen, 95 * escala), ImVec2(pantalla.x - margen, 605 * escala), IM_COL32(10, 38, 55, 250), 10 * escala);
    dl->AddRect(ImVec2(margen, 95 * escala), ImVec2(pantalla.x - margen, 605 * escala), kNaranja, 10 * escala, 0, 2 * escala);
    const float x = margen + 24 * escala, texto_ancho = ancho - 48 * escala;
    auto Q = [x, escala](float, float y) { return ImVec2(x, (110 + y) * escala); };
    float y_texto = 175 * escala;
    auto Envuelto = [dl, texto_ancho, escala, x, &y_texto](const char* texto) {
      const float tam = 20 * escala;
      dl->AddText(Fuente(), tam, ImVec2(x, y_texto), IM_COL32_WHITE, texto, nullptr, texto_ancho);
      y_texto += Fuente()->CalcTextSizeA(tam, FLT_MAX, texto_ancho, texto).y + 20 * escala;
    };
    Izq(dl, Q(0, 20), 23 * escala, L7("¿Enviar tus partidas al ránking mundial?", "Send your runs to the world ranking?", "Envoyer tes parties au classement mondial ?",
                                  "Deine Runs an die Weltrangliste senden?", "Inviare le tue partite alla classifica mondiale?",
                                  "Enviar suas partidas ao ranking mundial?", "ワールドランキングにプレイ結果を送信しますか？"), true, texto_ancho);
    Envuelto(L7("Se envía: tu alias, el luchador, los puntos y los datos de la partida (Survivors: tiempo, bajas, nivel, victoria, escenario y dificultad; reto diario: día, semilla, pisos, jefes, vida y cartas).",
                                 "Sent: your alias, the fighter, the score and the run data (Survivors: time, kills, level, win, stage and difficulty; daily challenge: day, seed, floors, bosses, health and cards).",
                                 "Envoyé : ton alias, le combattant, les points et les données de la partie (Survivors : temps, éliminés, niveau, victoire, scène et difficulté ; défi du jour : jour, graine, étages, boss, vie et cartes).",
                                 "Gesendet: dein Alias, der Kämpfer, die Punkte und die Run-Daten (Survivors: Zeit, Besiegte, Level, Sieg, Stufe und Schwierigkeit; Tagesherausforderung: Tag, Seed, Etagen, Bosse, Leben und Karten).",
                                 "Inviato: il tuo alias, il lottatore, i punti e i dati della partita (Survivors: tempo, abbattuti, livello, vittoria, scenario e difficoltà; sfida del giorno: giorno, seed, piani, boss, vita e carte).",
                                 "Enviado: seu alias, o lutador, os pontos e os dados da partida (Survivors: tempo, abates, nível, vitória, cenário e dificuldade; desafio diário: dia, semente, andares, chefes, vida e cartas).",
                                 "送信内容：名前、キャラクター、スコア、プレイ内容（サバイバーズ：時間・撃破数・レベル・勝利・ステージ・難易度／デイリーチャレンジ：日付・シード・階数・ボス・体力・カード）。"));
    Envuelto(L7("También un código al azar, un identificador anónimo de este PC, la fecha del envío, la versión del juego y una firma de autenticidad. Sin correo, sin cuenta ni datos personales. Con «No» no se envía nada.",
                                 "Also a random code, an anonymous ID for this PC, the sending date, the game version and an authenticity signature. No email, no account, no personal data. With \"No\" nothing is sent.",
                                 "Aussi un code aléatoire, un identifiant anonyme de ce PC, la date d'envoi, la version du jeu et une signature d'authenticité. Ni e-mail, ni compte, ni données personnelles. Avec « Non », rien n'est envoyé.",
                                 "Auch ein Zufallscode, eine anonyme Kennung dieses PCs, das Sendedatum, die Spielversion und eine Echtheitssignatur. Keine E-Mail, kein Konto, keine persönlichen Daten. Bei „Nein“ wird nichts gesendet.",
                                 "Inoltre un codice casuale, un identificativo anonimo di questo PC, la data di invio, la versione del gioco e una firma di autenticità. Niente email, account o dati personali. Con «No» non si invia nulla.",
                                 "Também um código aleatório, um identificador anónimo deste PC, a data do envio, a versão do jogo e uma assinatura de autenticidade. Sem e-mail, sem conta e sem dados pessoais. Com «Não» nada é enviado.",
                                 "また、ランダムなコード、このPCの匿名ID、送信日、ゲームのバージョン、改ざん防止の署名も送信します。メール・アカウント・個人情報は送りません。「いいえ」なら何も送信しません。"));
    Izq(dl, ImVec2(x, y_texto + 10 * escala), 20 * escala, std::string("A: ") + L7("Sí, enviar", "Yes, send", "Oui, envoyer", "Ja, senden", "Sì, invia", "Sim, enviar", "はい、送る") + "     B: " +
                                  L7("No", "No", "Non", "Nein", "No", "Não", "いいえ") + "     " +
                                  L7("(se cambia al pulsar Enviar o en Ajustes rápidos)", "(change it by pressing Send or in Quick settings)",
                                     "(modifiable via Envoyer ou les Réglages rapides)", "(änderbar über Senden oder die Schnelleinstellungen)",
                                     "(si cambia con Invia o nelle Impostazioni rapide)", "(muda ao pressionar Enviar ou nos Ajustes rápidos)",
                                      "（送信ボタンまたはクイック設定で変更可）"), true, texto_ancho);
    return;
  }
  if (g_act.alias) {
    Izq(dl, Q(0, 40), 18 * u, M::L("Elige el nombre que verán los demás (solo la primera vez):", "Pick the name others will see (only the first time):",
                                  "Choisis le nom que verront les autres (la première fois) :", "Wähle den Namen, den andere sehen (nur beim ersten Mal):",
                                  "Scegli il nome che vedranno gli altri (solo la prima volta):"), false, w * u);
    const int n = int(g_act.nombres.size()) + 1;
    const float bw = (w - 10 * float(n - 1)) / float(n);
    for (int i = 0; i < n; ++i) {
      const float x = (bw + 10) * float(i);
      Caja(dl, Q(x, 60), Q(x + bw, 100), u, i == g_act.sel, i == g_act.sel ? IM_COL32(220, 240, 255, 255) : IM_COL32(90, 160, 200, 200));
      const std::string s = i < int(g_act.nombres.size()) ? g_act.nombres[size_t(i)]
                                                            : M::L("Escribir otro...", "Type another...", "En écrire un autre...", "Anderen eingeben...", "Scrivine un altro...");
      Cen(dl, Q(x + bw * 0.5f, 80), 18 * u, s, false, (bw - 16) * u);
    }
    Izq(dl, Q(0, 124), 15 * u, M::L("Sin correo ni cuenta: solo el alias, el luchador y los puntos.", "No email or account: only the alias, the fighter and the score.",
                                   "Ni e-mail ni compte : seulement l'alias, le combattant et les points.", "Keine E-Mail, kein Konto: nur Alias, Kämpfer und Punkte.",
                                   "Niente email né account: solo l'alias, il lottatore e i punti."), false, w * u);
    if (g_act.escribiendo) {   // cuadro de texto (teclado): Intro acepta, Esc vuelve
      ImGui::SetNextWindowPos(Q(w * 0.5f, 80), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(360 * u, 0));
      ImGui::Begin("##rank_alias", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
      ImGui::TextUnformatted(M::L("Alias (3-16: letras, números, _ y -)", "Alias (3-16: letters, digits, _ and -)", "Alias (3-16 : lettres, chiffres, _ et -)",
                                  "Alias (3-16: Buchstaben, Ziffern, _ und -)", "Alias (3-16: lettere, numeri, _ e -)"));
      ImGui::SetNextItemWidth(-1);
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool intro = ImGui::InputText("##a", g_act.buf, 17, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
      const bool vale = AliasValido(g_act.buf);
      if (!vale && g_act.buf[0]) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", M::L("Ese nombre no vale", "That name is not allowed", "Ce nom n'est pas permis", "Dieser Name geht nicht", "Quel nome non è valido"));
      ImGui::End();
      if (intro && vale) PonerAlias(g_act.buf);
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_act.escribiendo = false;
    }
    return;
  }
  // barra de la partida
  Caja(dl, Q(0, 30), Q(w, 78), u);
  const std::string alias = MiAlias();
  Izq(dl, Q(16, 54), 21 * u, (alias.empty() ? std::string("?") : alias) + "  ·  " + M::Nombre(p.jugador) + "  ·  " + p.lugar, false, (w - 170) * u);
  Der(dl, Q(w - 12, 54), 22 * u, Miles(p.puntos) + " pts", true);
  // estado del envio
  const int e = EstadoDe(g_act.id);
  std::string pas, txt;
  ImU32 col = kAmarillo, tcol = IM_COL32_WHITE;
  const char* glifo = g_act.boton == kX ? "X" : "Y";
  switch (e) {
    case kSinEnviar:
      pas = M::L("Sin enviar", "Not sent", "Pas envoyée", "Nicht gesendet", "Non inviata");
      txt = std::string(M::L("Pulsa ", "Press ", "Appuie sur ", "Drücke ", "Premi ")) + glifo +
            M::L(" para enviarla (una vez por partida).", " to send it (once per run).", " pour l'envoyer (une fois par partie).", ", um ihn zu senden (einmal pro Run).",
                 " per inviarla (una volta per partita).");
      break;
    case kEnviando:
      pas = M::L("Enviando...", "Sending...", "Envoi...", "Sende...", "Invio...");
      txt = M::L("Tu puntuación sale hacia el ránking mundial.", "Your score is on its way to the world ranking.", "Ton score part vers le classement mondial.",
                 "Deine Punkte gehen an die Weltrangliste.", "Il tuo punteggio va verso la classifica mondiale.");
      col = kCian;
      Rueda(dl, Q(w - 20, 108), 9 * u, u);
      break;
    case kEnviada:
      pas = M::L("Enviada", "Sent", "Envoyée", "Gesendet", "Inviata");
      txt = M::L("Recibida: saldrá en el ránking en unos minutos.", "Received: it will show in the ranking in a few minutes.", "Reçue : elle apparaîtra d'ici quelques minutes.",
                 "Empfangen: erscheint in ein paar Minuten.", "Ricevuta: apparirà in classifica tra pochi minuti.");
      col = kVerde;
      break;
    case kEnTabla: {
      pas = M::L("Enviada", "Sent", "Envoyée", "Gesendet", "Inviata");
      col = tcol = kVerde;
      const Tab t = TablaPedida(p.modo == 'r' ? NombreDia(p.dia) : "survivors/semana.json");
      if (const Fila* f = Mia(t, MiUid()); f && t.total > 0) {
        const int mejor = t.total > 1 ? int(std::lround(100.0 * double(t.total - f->pos) / double(t.total - 1))) : 100;
        txt = std::string(M::L("Puesto ", "Rank ", "Place ", "Platz ", "Posto ")) + Miles(f->pos) + M::L(" de ", " of ", " sur ", " von ", " di ") + Miles(t.total) +
              (p.modo == 'r' ? M::L(" hoy", " today", " aujourd'hui", " heute", " oggi") : M::L(" esta semana", " this week", " cette semaine", " diese Woche", " questa settimana")) +
              "  ·  " + M::L("mejor que el ", "better than ", "meilleur que ", "besser als ", "meglio del ") + Num(mejor) + " %";
      } else {
        txt = M::L("Ya está en el ránking.", "It is in the ranking.", "Elle est au classement.", "Sie ist in der Rangliste.", "È in classifica.");
      }
      break;
    }
    case kSinConexion:
      pas = M::L("Sin conexión", "Offline", "Hors ligne", "Offline", "Offline");
      txt = M::L("Guardada en este PC. Se enviará sola al volver la red.", "Saved on this PC. It will be sent when the network is back.",
                 "Gardée sur ce PC. Elle partira au retour du réseau.", "Auf diesem PC gespeichert. Wird gesendet, sobald das Netz zurück ist.",
                 "Salvata su questo PC. Si invierà al ritorno della rete.");
      col = kRojo;
      break;
    case kRechazada:
      pas = M::L("Rechazada", "Rejected", "Refusée", "Abgelehnt", "Rifiutata");
      txt = M::L("El ránking no la aceptó (no cuadra con las reglas).", "The ranking did not accept it (it breaks the rules).", "Le classement l'a refusée (règles).",
                 "Die Rangliste hat ihn abgelehnt (Regeln).", "La classifica l'ha rifiutata (regole).");
      col = kRojo;
      break;
    default:
      pas = M::L("Solo local", "Local only", "Local seulement", "Nur lokal", "Solo locale");
      txt = M::L("Con mods de juego la partida no va al ránking mundial.", "With gameplay mods the run does not go to the world ranking.",
                 "Avec des mods de jeu, la partie ne va pas au classement.", "Mit Spiel-Mods geht der Run nicht in die Weltrangliste.",
                 "Con mod di gioco la partita non va in classifica.");
      break;
  }
  Pastilla(dl, Q(4, 108), u, pas, col);
  Color(dl, Q(4 + AnchoPastilla(u, pas) / u + 14, 108), 16 * u, txt, tcol, u, 0, (w - AnchoPastilla(u, pas) / u - 50) * u);
}

bool Encima(ImDrawList* dl, const S::Area& a, float t, uint16_t pulsado) {
  if (!Activo() || g_vista == kNada) return false;
  // A/B cierran; el cuadro en que se cierra aun se dibuja (la pantalla de debajo no ve la B)
  const bool cierra = (pulsado & (g_vista == kFicha ? (kA | kB) : kB)) != 0;
  if (g_vista == kFicha) {
    if (g_consent != 1 && !TablaLocal()) {   // R16: sin el Si no se pinta una ficha cacheada
      g_vista = kNada;
      return false;
    }
    Ficha(dl, a, cierra ? 0 : pulsado, TablaPedida(NombreTabla(g_cual)));
    if (cierra) g_vista = g_vuelta;
    return true;
  }
  S::Backdrop(dl, a, S::Img("hub/fondo") ? "hub/fondo" : nullptr);
  S::Sides(dl, a);
  if (g_consent != 1 && !TablaLocal()) {   // R16: sin el Si no se dibuja ni se toca la cache
    S::BannerBeta(dl, a, M::L("Ranking", "Ranking", "Classement", "Rangliste", "Classifica"));
    S::Frame(dl, a.At(437 * 1.25f - 200.0f, 127 * 1.25f), a.At(1250 * 1.25f - 200.0f, 518 * 1.25f), a.s);
    Izq(dl, a.At(437 * 1.25f - 200.0f, 545 * 1.25f), 17 * 1.25f * a.s, TextoRankingApagado(), true, 820 * 1.25f * a.s);
    if (cierra) g_vista = kNada;
    D::Help(dl, a, {{"B", M::L("Atrás", "Back", "Retour", "Zurück", "Indietro")}});
    return true;
  }
  DibujarTabla(dl, a, t, cierra ? 0 : pulsado, g_cual);
  if (cierra) g_vista = kNada;
  D::Help(dl, a, {{"LB", g_cual == 1 ? M::L("Hoy / Ayer", "Today / Yesterday", "Aujourd'hui / Hier", "Heute / Gestern", "Oggi / Ieri") : M::L("Periodo", "Period", "Période", "Zeitraum", "Periodo")},
                  {"X", M::L("Luchador", "Fighter", "Combattant", "Kämpfer", "Lottatore")},
                  {"A", M::L("Ver partida", "View run", "Voir la partie", "Run ansehen", "Vedi partita")},
                  {"Y", M::L("Ir a mi puesto", "Go to my rank", "Aller à ma place", "Zu meinem Platz", "Vai al mio posto")},
                  {"B", M::L("Atrás", "Back", "Retour", "Zurück", "Indietro")}});
  return true;
}

const char* TituloTabla(int cual) {
  return cual == 1 ? M::L("Dragon Rogue · Mundial", "Dragon Rogue · World", "Dragon Rogue · Monde", "Dragon Rogue · Welt", "Dragon Rogue · Mondo")
                   : M::L("Dragon Budokai Survivors · Mundial", "Dragon Budokai Survivors · World", "Dragon Budokai Survivors · Monde", "Dragon Budokai Survivors · Welt", "Dragon Budokai Survivors · Mondo");
}

bool Tabla(ImDrawList* dl, const S::Area& a, float t, uint16_t pulsado, int cual) {
  if (g_vista == kFicha && (g_consent == 1 || TablaLocal())) {
    const bool cierra = (pulsado & (kA | kB)) != 0;
    Ficha(dl, a, cierra ? 0 : pulsado, TablaPedida(NombreTabla(g_cual)));
    if (cierra) g_vista = kNada;
    return cierra;
  }
  g_vista = kNada, g_cual = cual;
  if (g_consent != 1 && !TablaLocal()) {   // R16: sin el Si no se dibuja ni se toca la cache
    S::BannerBeta(dl, a, M::L("Ranking", "Ranking", "Classement", "Rangliste", "Classifica"));
    S::Frame(dl, a.At(437 * 1.25f - 200.0f, 127 * 1.25f), a.At(1250 * 1.25f - 200.0f, 518 * 1.25f), a.s);
    Izq(dl, a.At(437 * 1.25f - 200.0f, 545 * 1.25f), 17 * 1.25f * a.s, TextoRankingApagado(), true, 820 * 1.25f * a.s);
    D::Help(dl, a, {{"B", M::L("Atrás", "Back", "Retour", "Zurück", "Indietro")}});
    return false;
  }
  DibujarTabla(dl, a, t, pulsado, cual);
  const std::string dice = cual == 1 ? M::L("El reto diario de todo el mundo: mismo luchador, mismos rivales y mismas cartas. Tu fila va siempre abajo.",
                                            "Everyone's daily challenge: same fighter, same rivals and same cards. Your row is always at the bottom.",
                                            "Le défi du jour du monde entier : même combattant, mêmes rivaux et mêmes cartes. Ta ligne reste en bas.",
                                            "Die Tagesherausforderung der ganzen Welt: gleicher Kämpfer, gleiche Gegner, gleiche Karten. Deine Zeile steht immer unten.",
                                            "La sfida del giorno di tutto il mondo: stesso lottatore, stessi rivali e stesse carte. La tua riga è sempre in fondo.")
                                     : M::L("Las mejores partidas del mundo. Filtra por luchador (X), periodo (LB), escenario (izq./der.) y dificultad (Start).\nPuntos = bajas x 10 + segundos x 30 + 3.000 si ganas.",
                                            "The best runs in the world. Filter by fighter (X), period (LB), stage (left/right) and difficulty (Start).\nPoints = kills x 10 + seconds x 30 + 3,000 if you win.",
                                            "Les meilleures parties du monde. Filtre par combattant (X), période (LB), scène (gauche/droite) et difficulté (Start).\nPoints = éliminés x 10 + secondes x 30 + 3 000 si tu gagnes.",
                                            "Die besten Runs der Welt. Filter: Kämpfer (X), Zeitraum (LB), Stufe (links/rechts), Schwierigkeit (Start).\nPunkte = Besiegte x 10 + Sekunden x 30 + 3.000 bei Sieg.",
                                            "Le migliori partite del mondo. Filtra per lottatore (X), periodo (LB), scenario (sinistra/destra) e difficoltà (Start).\nPunti = abbattuti x 10 + secondi x 30 + 3.000 se vinci.");
  if (cual == 0) {   // P2 (A15): Survivors lleva los filtros y la formula en dos lineas bajo la tabla (el globo tapaba filas)
    const std::string l = dice;
    const size_t cut = l.find('\n');
    Izq(dl, a.At(437 * 1.25f - 200.0f, 545 * 1.25f), 17 * 1.25f * a.s, cut == std::string::npos ? l : l.substr(0, cut), true, 820 * 1.25f * a.s);
    if (cut != std::string::npos) Izq(dl, a.At(437 * 1.25f - 200.0f, 580 * 1.25f), 17 * 1.25f * a.s, l.substr(cut + 1), false, 820 * 1.25f * a.s);
  } else {
    D::Bubble(dl, a, dice.c_str());
  }
  D::Help(dl, a, {{"LB", cual == 1 ? M::L("Hoy / Ayer", "Today / Yesterday", "Aujourd'hui / Hier", "Heute / Gestern", "Oggi / Ieri") : M::L("Periodo", "Period", "Période", "Zeitraum", "Periodo")},
                  {"X", M::L("Luchador", "Fighter", "Combattant", "Kämpfer", "Lottatore")},
                  {"A", M::L("Ver partida", "View run", "Voir la partie", "Run ansehen", "Vedi partita")},
                  {"Y", M::L("Ir a mi puesto", "Go to my rank", "Aller à ma place", "Zu meinem Platz", "Vai al mio posto")},
                  {"B", M::L("Atrás", "Back", "Retour", "Zurück", "Indietro")}});
  return false;
}

bool PanelDiario(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, uint16_t pulsado, int dia, int jugador, const std::string& camino) {
  if (!Activo()) return false;
  if (pulsado & kY) g_mis_dias = !g_mis_dias;
  if (g_mis_dias) return false;
  if (g_consent != 1 && !TablaLocal()) {   // R16: sin el Si no se dibuja ni se toca la cache
    S::Frame(dl, p0, p1, u / 1.25f);
    dl->AddText(Fuente(), 17 * u, ImVec2(p0.x + 28 * u, p0.y + 80 * u), IM_COL32_WHITE,
                TextoRankingApagado(), nullptr, std::max(1.0f, p1.x - p0.x - 56 * u));
    return true;
  }
  if (pulsado & (kLb | kRb)) g_dia_menos = 1 - g_dia_menos;
  const Tab tb = TablaPedida(NombreDia(dia - g_dia_menos));
  const std::string uid = MiUid();
  const Fila* mia = Mia(tb, uid);
  if ((pulsado & kX) && !tb.filas.empty()) AbrirFicha(tb, mia ? *mia : tb.filas[0], 1);
  // p0..p1 = el marco de la maqueta 02 (668,128)-(1245,668); Q en px de la maqueta
  auto Q = [p0, u](float x, float y) { return ImVec2(p0.x + (x - 668) * u, p0.y + (y - 128) * u); };
  S::Frame(dl, p0, p1, u / 1.25f);
  Izq(dl, Q(696, 160), 26 * u, std::string(M::L("Mundial  ·  ", "World  ·  ", "Monde  ·  ", "Welt  ·  ", "Mondo  ·  ")) +
                                   (g_dia_menos ? M::L("ayer", "yesterday", "hier", "gestern", "ieri") : M::L("hoy", "today", "aujourd'hui", "heute", "oggi")),
      true, 330 * u);
  if (!g_dia_menos) {
    const int64_t queda = std::max<int64_t>(0, int64_t(dia + 1) * 86400 - Ahora());
    char b[48];
    std::snprintf(b, sizeof b, "%s %02d:%02d h", M::L("Cierra en", "Closes in", "Ferme dans", "Endet in", "Chiude tra"), int(queda / 3600), int(queda / 60 % 60));
    Color(dl, Q(1213, 163), 17 * u, b, kVerde, u, 1);
  } else {
    Color(dl, Q(1213, 163), 17 * u, M::L("Cerrado", "Closed", "Fermé", "Beendet", "Chiuso"), kAmarillo, u, 1);
  }
  dl->AddLine(Q(683, 187), Q(1230, 187), kNaranja, 1.5f * u);
  Izq(dl, Q(751, 209), 15 * u, M::L("Jugador", "Player", "Joueur", "Spieler", "Giocatore"), false);
  Cen(dl, Q(980, 209), 15 * u, M::L("Pisos", "Floors", "Étages", "Etagen", "Piani"), false);
  Cen(dl, Q(1071, 209), 15 * u, M::L("Jefes", "Bosses", "Boss", "Bosse", "Boss"), false);
  Der(dl, Q(1213, 209), 15 * u, M::L("Puntos", "Points", "Points", "Punkte", "Punti"), false);
  auto fila = [&](const Fila& f, float y, bool yo) {
    Caja(dl, Q(687, y - 18), Q(1227, y + 18), u, f.pos == 1 && !yo, yo ? kAmarillo : IM_COL32(90, 160, 200, 200));
    if (f.pos <= 3) Medalla(dl, f.pos, Q(716, y), 14 * u);
    else Cen(dl, Q(716, y), 19 * u, Miles(f.pos), false, 50 * u);
    Izq(dl, Q(751, y), 20 * u, yo ? Tu() + "  ·  " + f.al : f.al, yo, 200 * u);
    Cen(dl, Q(980, y), 20 * u, Num(f.g), false);
    Cen(dl, Q(1071, y), 20 * u, Num(f.b), false);
    Der(dl, Q(1213, y), 20 * u, Miles(f.p), true);
  };
  for (size_t i = 0; i < tb.filas.size() && i < 7; ++i) fila(tb.filas[i], 243 + 42.7f * float(i), false);
  if (tb.filas.empty())
    Cen(dl, Q(957, 300), 19 * u, !tb.hay && !tb.error ? M::L("Cargando el ránking...", "Loading the ranking...", "Chargement du classement...", "Rangliste lädt...", "Caricamento della classifica...")
                                 : !tb.hay ? M::L("Sin conexión: se verá al volver la red.", "Offline: it will show when the network is back.", "Hors ligne : il s'affichera au retour du réseau.", "Offline: erscheint, sobald das Netz zurück ist.", "Offline: si vedrà al ritorno della rete.")
                                           : M::L("Aún no hay partidas este día", "No runs this day yet", "Pas encore de parties ce jour", "Noch keine Runs an diesem Tag", "Ancora nessuna partita oggi"), false, 520 * u);
  dl->AddLine(Q(687, 545), Q(1227, 545), IM_COL32(232, 140, 32, 160), 1.2f * u);
  if (mia) fila(*mia, 575, true);
  else Izq(dl, Q(705, 575), 17 * u, Tu() + ": " + M::L("aún sin partida enviada este día", "no run sent this day yet", "pas encore de partie envoyée ce jour", "an diesem Tag noch kein Run gesendet", "ancora nessuna partita inviata oggi"), false, 500 * u);
  Izq(dl, Q(690, 627), 15 * u, std::string(M::L("Todos con ", "Everyone with ", "Tous avec ", "Alle mit ", "Tutti con ")) + M::Nombre(jugador) + M::L(" en ", " in ", " dans ", " in ", " in ") + camino +
                                   M::L(": mismos rivales y cartas.", ": same rivals and cards.", " : mêmes rivaux et cartes.", ": gleiche Gegner und Karten.", ": stessi rivali e carte."), false, 520 * u);
  return true;
}

void CajaDiario(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, int dia, const std::string& codigo) {
  if (!Activo()) return;
  auto Q = [p0, u](float x, float y) { return ImVec2(p0.x + (x - 62) * u, p0.y + (y - 588) * u); };
  dl->AddRectFilled(p0, p1, IM_COL32(8, 24, 36, 225), 10 * u);
  int estado = kSinEnviar;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    Arrancar();
    for (const Enviada& e : g_env)
      if (e.modo == 'r' && e.dia == dia) estado = e.estado;
  }
  std::string pas = M::L("Sin enviar", "Not sent", "Pas envoyée", "Nicht gesendet", "Non inviata");
  ImU32 col = kAmarillo;
  if (estado == kEnviada || estado == kEnTabla) pas = M::L("Enviada", "Sent", "Envoyée", "Gesendet", "Inviata"), col = kVerde;
  else if (estado == kEnviando) pas = M::L("Enviando...", "Sending...", "Envoi...", "Sende...", "Invio..."), col = kCian;
  else if (estado == kSinConexion) pas = M::L("Sin conexión", "Offline", "Hors ligne", "Offline", "Offline"), col = kRojo;
  else if (estado == kRechazada) pas = M::L("Rechazada", "Rejected", "Refusée", "Abgelehnt", "Rifiutata"), col = kRojo;
  else if (estado == kSoloLocal) pas = M::L("Solo local", "Local only", "Local seulement", "Nur lokal", "Solo locale");
  Pastilla(dl, Q(80, 608), u, pas, col);
  Izq(dl, Q(80 + AnchoPastilla(u, pas) / u + 24, 608), 19 * u, M::L("Tu puesto hoy:", "Your rank today:", "Ta place aujourd'hui :", "Dein Platz heute:", "Il tuo posto oggi:"), false);
  if (g_consent == 1 || TablaLocal()) {   // R16: sin el Si ni se lee la cache
    const Tab tb = TablaPedida(NombreDia(dia));
    const Fila* mia = Mia(tb, MiUid());
    Der(dl, Q(580, 608), 21 * u, mia ? Miles(mia->pos) + M::L(" de ", " of ", " sur ", " von ", " di ") + Miles(tb.total) : std::string("-"), true);
  } else {
    Der(dl, Q(580, 608), 21 * u, std::string("-"), true);
  }
  if (!codigo.empty())
    Izq(dl, Q(80, 639), 15 * u, std::string(M::L("El código ", "The code ", "Le code ", "Der Code ", "Il codice ")) + codigo +
                                    M::L(" sigue valiendo para compartir", " still works for sharing", " sert toujours à partager", " gilt weiter zum Teilen", " vale ancora per condividere"),
        false, 500 * u);
  else
    Izq(dl, Q(80, 639), 15 * u, M::L("Se envía desde «Fin del camino» (botón Y).", "It is sent from 'End of the path' (Y button).", "Elle s'envoie depuis « Fin du chemin » (Y).",
                                     "Wird über „Ende des Weges“ gesendet (Y).", "Si invia da «Fine del cammino» (Y)."), false, 500 * u);
}


// R16 autoprueba (solo con DBZ3_TEST_RANKING_AUTOPRUEBA=1 y DBZ3_TEST_RANKING_RED=falsa): recorre el aviso de privacidad con la red
// falsa. Esperado: sin Si, 0 peticiones y nada en la cola; con Si, el envio sale. Deja las lineas "RANKCONS ..." en el registro.
namespace {
void AutoPrueba() {
  std::this_thread::sleep_for(std::chrono::seconds(5));
  Partida p;
  p.modo = 's', p.jugador = 3, p.segundos = 300, p.puntos = 3500, p.bajas = 20, p.nivel = 5, p.escenario = "llanura", p.lugar = "prueba";
  const int boton = kX;
  auto cola = [] { std::lock_guard<std::mutex> lk(g_mx); return int(g_cola.size()); };
  { std::lock_guard<std::mutex> lk(g_mx); Arrancar(); g_consent = 0; g_alias.clear(); g_cola.clear(); g_env.clear(); }
  Entrada(0, p, boton);
  Entrada(kX, p, boton);   // primera vez: sale el aviso
  const bool aviso = g_act.consulta;
  const bool modal = Entrada(kY | kLb, p, boton) == 0 && g_act.consulta;
  Entrada(kB, p, boton);   // No
  const bool no = g_consent == 2 && !g_act.consulta;
  Entrada(kX, p, boton);   // pulsar Enviar tras un No: pregunta de nuevo
  const bool otra = g_act.consulta;
  Entrada(kB, p, boton);
  TablaPedida("survivors/semana.json");
  { std::lock_guard<std::mutex> lk(g_mx); Despertar(); }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  const int pet_no = g_peticiones.load(), cola_no = cola();
  REXLOG_INFO("RANKCONS paso No: aviso={} no_guardado={} reabre={} peticiones={} cola={}", aviso, no, otra, pet_no, cola_no);
  REXLOG_INFO("RANKCONS atajos bloqueados={}", modal);
  Entrada(kX, p, boton);   // cambia de idea
  Entrada(kA, p, boton);   // Si -> pide alias
  const bool si = g_consent == 1 && g_act.alias;
  PonerAlias("Prueba_B3");
  TablaPedida("survivors/semana.json");   // descarga con red falsa aun si los mods impiden publicar
  std::this_thread::sleep_for(std::chrono::seconds(2));
  const int pet_si = g_peticiones.load();
  REXLOG_INFO("RANKCONS paso Si: si_guardado={} peticiones={} cola={}", si, pet_si, cola());
  const bool ok = aviso && modal && no && otra && pet_no == 0 && cola_no == 0 && si && pet_si > 0;
  REXLOG_INFO("RANKCONS {}", ok ? "PASS" : "FAIL");
}
struct ArrancaAutoPrueba {
  ArrancaAutoPrueba() {
    const char* e = std::getenv("DBZ3_TEST_RANKING_AUTOPRUEBA");
    if (e && e[0] == '1') std::thread(AutoPrueba).detach();
  }
} g_arranca_autoprueba;
}  // namespace

}  // namespace dbz3::rank
#else
#include "ranking_online.h"
// PLAT-04: el ranking mundial requiere el backend de firma de Windows.
// Sin el no se consume input, no se toca la cola y no se abre la red.
namespace dbz3::rank {
bool Activo() { return false; }
uint16_t Entrada(uint16_t pulsado, const Partida&, uint16_t) { return pulsado; }
void Bloque(ImDrawList*, ImVec2, float, float, const Partida&) {}
const char* EtiquetaBoton() { return nullptr; }
bool EligiendoAlias() { return false; }
bool Consultando() { return false; }
bool Encima(ImDrawList*, const ingame::style::Area&, float, uint16_t) { return false; }
bool Tabla(ImDrawList*, const ingame::style::Area&, float, uint16_t, int) { return false; }
const char* TituloTabla(int cual) { return cual == 1 ? "Dragon Rogue" : "Dragon Budokai Survivors"; }
bool PanelDiario(ImDrawList*, ImVec2, ImVec2, float, uint16_t, int, int, const std::string&) { return false; }
void CajaDiario(ImDrawList*, ImVec2, ImVec2, float, int, const std::string&) {}
}  // namespace dbz3::rank
#endif

int dbz3::rank::PuntosSurvivors(int segundos, int bajas, bool victoria) {
  return bajas * 10 + segundos * 30 + (victoria ? 3000 : 0);
}
