// dbz3 - Modo Heroe: lectura/escritura atomica de coleccion.toml y sincronizacion global (NHER4).
// Cabecera pura (sin dependencias del runtime guest), reutilizada por heroe_recompensas.inl y verificada
// directamente por tests C++ nativos (toml++ real, claves extra, multilinea, flush+MoveFileExW, fallo IO/corrupto).
#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dbz3::heroe::col_io {

struct DatosColeccion {
  bool todo = false;
  std::set<std::string> vistos;
  std::set<std::string> conseguidos;
  std::set<std::string> finales;
  toml::table extras;   // conserva claves adicionales de versiones futuras, Save100 o mods
};

struct ReglaRecompensa {
  std::string id;
  int coleccion = -1;
  bool solo_coleccion = false;
};

inline bool IdValido(const std::string& id) {
  if (id.empty() || id.size() >= 48) return false;
  for (unsigned char c : id)
    if (c < 0x20 || c == 0x7F) return false;
  return true;
}

inline void LeerLista(const toml::table& t, const char* clave, std::set<std::string>& dst) {
  if (const toml::array* a = t[clave].as_array())
    for (const auto& e : *a)
      if (auto s = e.value<std::string>(); s && IdValido(*s)) dst.insert(*s);
}

inline bool FlushArchivo(const std::filesystem::path& ruta, std::error_code& ec) {
#ifdef _WIN32
  HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    ec = std::error_code(int(GetLastError()), std::system_category());
    return false;
  }
  const BOOL ok = FlushFileBuffers(h);
  const DWORD err = ok ? 0 : GetLastError();
  CloseHandle(h);
  if (!ok) {
    ec = std::error_code(int(err), std::system_category());
    return false;
  }
  return true;
#else
  const int fd = ::open(ruta.c_str(), O_WRONLY);
  if (fd < 0) {
    ec = std::error_code(errno, std::generic_category());
    return false;
  }
  const int r = ::fsync(fd);
  const int err = r == 0 ? 0 : errno;
  ::close(fd);
  if (r != 0) {
    ec = std::error_code(err, std::generic_category());
    return false;
  }
  return true;
#endif
}

inline bool ReemplazarAtomico(const std::filesystem::path& tmp, const std::filesystem::path& dst,
                              std::error_code& ec) {
  if (!FlushArchivo(tmp, ec)) return false;
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(int(GetLastError()), std::system_category());
    return false;
  }
  return true;
#else
  std::filesystem::rename(tmp, dst, ec);
  return !ec;
#endif
}

inline bool Cargar(const std::filesystem::path& ruta, DatosColeccion& out, std::string* error = nullptr) {
  std::error_code ec;
  if (!std::filesystem::exists(ruta, ec)) {
    if (ec) {
      if (error) *error = ec.message();
      return false;
    }
    return true;
  }
  std::ifstream in(ruta, std::ios::binary);
  if (!in) {
    if (error) *error = "no se pudo abrir coleccion.toml";
    return false;
  }
  try {
    const toml::table t = toml::parse(in, std::string_view{"coleccion.toml"});
    DatosColeccion tmp;
    tmp.todo = t["todo"].value_or(false);
    LeerLista(t, "vistos", tmp.vistos);
    LeerLista(t, "conseguidos", tmp.conseguidos);
    LeerLista(t, "finales", tmp.finales);
    for (const auto& [k, v] : t) {
      const std::string_view sv = k.str();
      if (sv == "todo" || sv == "vistos" || sv == "conseguidos" || sv == "finales") continue;
      v.visit([&](const auto& node) { tmp.extras.insert_or_assign(k, node); });
    }
    out = std::move(tmp);
    return true;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
}

inline bool GuardarAtomico(const std::filesystem::path& ruta, const DatosColeccion& d, bool escribir_global,
                           std::string* error = nullptr) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!ruta.parent_path().empty()) {
    fs::create_directories(ruta.parent_path(), ec);
    if (ec) {
      if (error) *error = "mkdir: " + ec.message();
      return false;
    }
  }
  fs::path tmp = ruta;
  tmp += ".tmp";
  fs::path bak = ruta;
  bak += ".bak";
  if (fs::is_directory(tmp, ec)) {
    if (error) *error = "tmp es un directorio";
    return false;
  }
  if (fs::is_directory(bak, ec)) {
    if (error) *error = "bak es un directorio";
    return false;
  }
  auto a_array = [](const std::set<std::string>& vals) {
    toml::array arr;
    for (const std::string& s : vals)
      if (IdValido(s)) arr.push_back(s);
    return arr;
  };
  toml::table tbl = d.extras;
  tbl.insert_or_assign("todo", d.todo);
  tbl.insert_or_assign("vistos", a_array(d.vistos));
  const bool con_global = escribir_global || !d.conseguidos.empty() || !d.finales.empty();
  if (con_global) {
    tbl.insert_or_assign("conseguidos", a_array(d.conseguidos));
    tbl.insert_or_assign("finales", a_array(d.finales));
  }
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    if (!o) {
      if (error) *error = "no se pudo abrir .tmp";
      return false;
    }
    o << "# Coleccion del Modo Heroe (global): lo visto, lo conseguido entre fichas y el desbloqueo total.\n"
      << tbl << '\n';
    o.flush();
    if (!o) {
      o.close();
      std::error_code rme;
      fs::remove(tmp, rme);
      if (error) *error = "error escribiendo .tmp";
      return false;
    }
  }
  if (fs::exists(ruta, ec)) {
    fs::path bak_tmp = bak;
    bak_tmp += ".tmp";
    if (fs::is_directory(bak_tmp, ec)) {
      std::error_code rme;
      fs::remove(tmp, rme);
      if (error) *error = "bak.tmp es un directorio";
      return false;
    }
    fs::copy_file(ruta, bak_tmp, fs::copy_options::overwrite_existing, ec);
    if (ec || !ReemplazarAtomico(bak_tmp, bak, ec)) {
      std::error_code rme;
      fs::remove(bak_tmp, rme);
      fs::remove(tmp, rme);
      if (error) *error = "copia .bak: " + ec.message();
      return false;
    }
  }
  ec.clear();
  if (!ReemplazarAtomico(tmp, ruta, ec)) {
    std::error_code rme;
    fs::remove(tmp, rme);
    if (error) *error = "reemplazo atomico: " + ec.message();
    return false;
  }
  return true;
}

// Migracion no destructiva y union global de premios (r_*) entre todas las fichas (activas y archivadas).
// No modifica las listas de las fichas (cac.toml); distingue asi el progreso por cuenta del progreso por ficha.
inline bool SincronizarConFichas(DatosColeccion& col, const std::vector<ReglaRecompensa>& tabla,
                                 const std::vector<std::vector<std::string>>& listas_desbloqueos) {
  if (tabla.empty()) return false;
  std::set<std::string> validos;
  for (const auto& r : tabla)
    if (IdValido(r.id)) validos.insert(r.id);
  bool cambio = false;
  for (const auto& lista : listas_desbloqueos)
    for (const std::string& id : lista)
      if (validos.count(id)) cambio |= col.conseguidos.insert(id).second;
  bool otorgado = true;
  for (int vuelta = 0; otorgado && vuelta < 4; ++vuelta) {
    otorgado = false;
    int n = 0, base_ok = 0, base_tot = 0;
    for (const auto& x : tabla) {
      const bool ok = col.conseguidos.count(x.id) != 0;
      n += ok;
      if (x.coleccion < 100) ++base_tot, base_ok += ok;
    }
    const int pct = 100 * n / int(tabla.size());
    for (const auto& r : tabla) {
      if (!r.solo_coleccion || r.coleccion < 0) continue;
      if (col.conseguidos.count(r.id)) continue;
      if (r.coleccion >= 100 ? (base_tot == 0 || base_ok != base_tot) : (pct < r.coleccion)) continue;
      col.conseguidos.insert(r.id);
      cambio = otorgado = true;
    }
  }
  return cambio;
}

}  // namespace dbz3::heroe::col_io
