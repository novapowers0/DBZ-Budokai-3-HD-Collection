// dbz3 - Mod manager (project-side).

#include "mods.h"
#include "mod_manifest.h"
#include "launcher/i18n.h"

#include <rex/filesystem.h>
#include <rex/filesystem/afs.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <toml++/toml.hpp>
#include <system_error>

#if REX_PLATFORM_WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dbz3 {

std::filesystem::path ModsRoot() {
  // Mods live next to the executable (where the runtime's VFS resolves them),
  // matching dbz3::settings. The build output dir is not wiped for mods.
  return rex::filesystem::AfsModsRoot();   // unico punto (vanilla online: carpeta vacia)
}

namespace {

bool EndsWithDotDisabled(const std::string& name) {
  const std::string suffix = ".disabled";
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StripDotDisabled(const std::string& name) {
  return EndsWithDotDisabled(name)
             ? name.substr(0, name.size() - std::string(".disabled").size())
             : name;
}

bool FolderHasDisabledMarker(const std::filesystem::path& dir) {
  return std::filesystem::exists(dir / ".disabled");
}

bool FolderIsDisabled(const std::filesystem::path& dir,
                      const std::string& name) {
  return EndsWithDotDisabled(name) || FolderHasDisabledMarker(dir);
}

// Reads a simple "key=value" manifest (one per line, '#' = comment).
void LoadManifest(const std::filesystem::path& dir, ModInfo& info) {
  std::ifstream in(dir / "manifest.txt");
  if (!in.is_open()) {
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      size_t b = 0;
      while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
      s.erase(0, b);
    };
    trim(key);
    trim(value);
    if (key == "name") info.display_name = value;
    else if (key == "description") info.description = value;
    else if (key == "author") info.author = value;
    else if (key == "version") info.version = value;
    else if (key == "dbz3_version") info.made_for = value;
    else if (key == "type") info.type = value;
    else if (key == "source") info.source = value;
    else if (key == "target") info.target = value;
  }
}

// Infer a mod's type from its folder layout when no manifest is present, and
// count the files it overrides. Types:
//   swap_b3   -> a data_cmn.afs override (whole repacked AFS, model swap)
//   audio     -> any adx_*.afs override
//   data      -> anything else
void InferTypeAndCount(const std::filesystem::path& dir, ModInfo& info) {
  bool has_data = false, has_audio = false;
  int count = 0;
  std::error_code ec;
  for (auto [it_1, iec_1] = std::pair{std::filesystem::recursive_directory_iterator(dir, ec), std::error_code{}}; !iec_1 && it_1 != std::filesystem::recursive_directory_iterator(); it_1.increment(iec_1)) {
    const auto& entry = *it_1;
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string rel =
        rex::path_to_utf8(entry.path().lexically_relative(dir));
    ++count;
    if (rel.find("adx_") != std::string::npos &&
        (rel.find(".afs") != std::string::npos ||
         rel.find(".adx") != std::string::npos)) {
      has_audio = true;
    }
    if (rel.find("data_") != std::string::npos &&
        rel.find(".afs") != std::string::npos) {
      has_data = true;
    }
  }
  info.file_count = count;
  std::error_code ec2;
  if (std::filesystem::exists(dir / "personaje.toml", ec2)) {
    if (info.type.empty()) info.type = "personaje";
    if (info.display_name.empty()) {      // nombre del personaje (nombre = "...")
      std::ifstream pt(dir / "personaje.toml");
      std::string ln;
      while (std::getline(pt, ln)) {
        const auto eq = ln.find('=');
        if (eq == std::string::npos || ln.compare(0, 6, "nombre") != 0) continue;
        std::string v = ln.substr(eq + 1);
        v.erase(0, v.find_first_not_of(" \t\""));
        v.erase(v.find_last_not_of(" \t\"\r") + 1);
        info.display_name = v;
        break;
      }
    }
  }
  if (info.type.empty() && std::filesystem::exists(dir / "roster.toml", ec2)) {
    info.type = "generado";
  }
  if (info.type.empty()) {
    if (has_audio) info.type = "audio";
    else if (has_data) info.type = "data";
    else info.type = "other";
  }
}

}  // namespace

namespace {
// <hash16>_<W>x<H>_<formato> (nombre de textura de pack, el del volcado).
bool PackStem(const std::string& stem, uint32_t& w, uint32_t& h) {
  if (stem.size() < 20 || stem[16] != '_') return false;
  for (int i = 0; i < 16; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(stem[i]))) return false;
  }
  return std::sscanf(stem.c_str() + 17, "%ux%u", &w, &h) == 2 && w && h;
}

// Tamano real de un PNG (IHDR) o DDS (cabecera); false si no se puede leer.
bool ImageSize(const std::filesystem::path& p, uint32_t& w, uint32_t& h) {
  std::ifstream f(p, std::ios::binary);
  unsigned char b[24] = {};
  if (!f.read(reinterpret_cast<char*>(b), sizeof(b))) return false;
  auto be = [&](int o) { return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3]; };
  auto le = [&](int o) { return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24; };
  if (b[1] == 'P' && b[2] == 'N' && b[3] == 'G') { w = be(16); h = be(20); return true; }
  if (b[0] == 'D' && b[1] == 'D' && b[2] == 'S') { h = le(12); w = le(16); return true; }
  return false;
}

bool HasModContent(const std::filesystem::path& d) {
  std::error_code ec;
  for (const char* n : {"us", "eu", "personaje.toml", "traje.toml", "roster.toml", "manifest.txt", "ui_ingame"}) {
    if (std::filesystem::exists(d / n, ec)) return true;
  }
  uint32_t w, h;
  // Los packs de texturas pueden ir en subcarpetas (el runtime las recorre).
  for (auto [it_2, iec_2] = std::pair{std::filesystem::recursive_directory_iterator(
           d, std::filesystem::directory_options::skip_permission_denied, ec), std::error_code{}}; !iec_2 && it_2 != std::filesystem::recursive_directory_iterator(); it_2.increment(iec_2)) {
    const auto& e = *it_2;
    std::error_code fec;   // sin ec, una entrada rara (ruta > MAX_PATH) lanzaria fuera del launcher
    if (e.is_regular_file(fec) && PackStem(rex::path_to_utf8(e.path().stem()), w, h)) return true;
  }
  return false;
}

// Rutas relativas que pide un personaje.toml ("modelos/t1.bin"...) y no estan.
void MissingTomlFiles(const toml::node& n, const std::filesystem::path& dir, std::vector<std::string>& out) {
  if (const auto* t = n.as_table()) {
    for (const auto& [k, v] : *t) MissingTomlFiles(v, dir, out);
  } else if (const auto* a = n.as_array()) {
    for (const auto& v : *a) MissingTomlFiles(v, dir, out);
  } else if (const auto* s = n.as_string()) {
    const std::string& v = s->get();
    const auto dot = v.rfind('.');
    const bool looks_file = v.find(':') == std::string::npos && dot != std::string::npos &&
                            v.size() - dot <= 5 && v.find(' ') == std::string::npos &&
                            (v.find('/') != std::string::npos || v.find('\\') != std::string::npos);
    std::error_code ec;
    if (looks_file && !std::filesystem::exists(dir / std::filesystem::u8path(v), ec)) out.push_back(v);
  }
}

void CheckProblems(const std::filesystem::path& dir, ModInfo& info) {
  std::error_code ec;
  if (info.file_count == 0) {
    info.problems.emplace_back(ModInfo::kEmpty, "");
    return;
  }
  if (!HasModContent(dir)) {
    std::vector<std::filesystem::path> subs;
    for (auto [it_3, iec_3] = std::pair{std::filesystem::directory_iterator(dir, ec), std::error_code{}}; !iec_3 && it_3 != std::filesystem::directory_iterator(); it_3.increment(iec_3)) {
    const auto& e = *it_3;
      if (e.is_directory(ec) && e.path().filename() != "__MACOSX") subs.push_back(e.path());   // = WrappedModDir
    }
    if (subs.size() == 1 && HasModContent(subs[0])) {
      info.problems.emplace_back(ModInfo::kNested, rex::path_to_utf8(subs[0].filename()));
      return;
    }
    // pack de varios mods copiado tal cual (LEEME + mods/_roster, mods/cut_*...): FixNestedMod lo reparte
    if (subs.size() == 1 && subs[0].filename() == "mods") {
      for (auto [it_m, iec_m] = std::pair{std::filesystem::directory_iterator(subs[0], ec), std::error_code{}};
           !iec_m && it_m != std::filesystem::directory_iterator(); it_m.increment(iec_m)) {
        if (it_m->is_directory(ec) && HasModContent(it_m->path())) {
          info.problems.emplace_back(ModInfo::kNested, "mods");
          return;
        }
      }
    }
  }
  for (const char* tn : {"personaje.toml", "traje.toml"}) {
    if (!std::filesystem::exists(dir / tn, ec)) continue;
    try {
      const toml::table t = toml::parse_file(rex::path_to_utf8(dir / tn));
      std::vector<std::string> missing;
      MissingTomlFiles(t, dir, missing);
      for (const auto& m : missing) info.problems.emplace_back(ModInfo::kMissingFile, m);
    } catch (const toml::parse_error& e) {
      info.problems.emplace_back(ModInfo::kBadToml, std::string(tn) + ": " + std::string(e.description()));
    }
  }
  // avisos del ultimo montaje (roster_build.py): ID ocupado, sin IDs, sin casilla, traje roto...
  std::ifstream avisos(dir.parent_path() / "roster_avisos.txt");
  for (std::string line; std::getline(avisos, line);) {
    const size_t tab = line.find('\t');
    if (info.enabled && tab == info.name.size() && line.compare(0, tab, info.name) == 0)
      info.problems.emplace_back(ModInfo::kRoster, line.substr(tab + 1));
  }
  // Pack de texturas: nombres que el juego no reconoce y tamanos que el runtime descarta (no son
  // un multiplo entero del original, igual en ancho y alto, o pasan de 16384; texture_cache.cpp y
  // dbz3_texture_pack.cpp del SDK).
  int pack = 0, bad_name = 0, bad_sizes = 0;
  std::string bad_size;
  // Abrir cada imagen es lo caro: un pack recien instalado (12345 PNG) tardaba ~70 s con el
  // antivirus revisando cada fichero, con el launcher congelado. Como mucho 2 s por mod; la
  // siguiente lista vuelve a empezar con esas ya en cache y llega mas lejos.
  // ponytail: tope por tiempo, no por fichero; hilo aparte si algun dia hace falta el 100% al abrir.
  const auto size_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (auto [it_4, iec_4] = std::pair{std::filesystem::recursive_directory_iterator(
           dir, std::filesystem::directory_options::skip_permission_denied, ec), std::error_code{}}; !iec_4 && it_4 != std::filesystem::recursive_directory_iterator(); it_4.increment(iec_4)) {
    const auto& e = *it_4;
    if (!e.is_regular_file()) continue;
    std::string ext = rex::path_to_utf8(e.path().extension());   // .string() lanza con nombres no ANSI
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".png" && ext != ".dds") continue;
    uint32_t w0, h0, w, h;
    const std::string stem = rex::path_to_utf8(e.path().stem());
    std::string label = stem;
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (label == "preview" || label == "cover" || label == "icon") continue;
    if (!PackStem(stem, w0, h0)) { ++bad_name; continue; }
    ++pack;
    if (std::chrono::steady_clock::now() < size_deadline && ImageSize(e.path(), w, h) &&
        (w % w0 || h % h0 || w / w0 != h / h0 || w / w0 < 1 || w > 16384 || h > 16384)) {
      // con la carpeta: el usuario tiene que poder encontrarlo dentro del pack
      if (!bad_sizes++) bad_size = rex::path_to_utf8(e.path().lexically_relative(dir));
    }
  }
  if (pack && bad_name) info.problems.emplace_back(ModInfo::kBadTextureName, std::to_string(bad_name));
  if (bad_sizes > 1) bad_size += " (+" + std::to_string(bad_sizes - 1) + ")";
  if (!bad_size.empty()) info.problems.emplace_back(ModInfo::kBadTextureSize, bad_size);
}

// ---- instalacion transaccional (Fase 0 A1/A2, B11, B16): todo se prepara en una carpeta
// oculta de mods/ (mismo disco) y se publica con un solo rename; lo que se pisa va a
// mods_respaldo/ (junto a mods/, como Nova's Mods) y vuelve a su sitio si el segundo paso falla. Nada se borra.
namespace fs = std::filesystem;

std::string Stamp() {   // unico por operacion: nunca se reutiliza una carpeta .install_ vieja
  static std::atomic<uint32_t> n{0};
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  return std::to_string(ms) + "_" + std::to_string(n++);
}

// El antivirus o el indexador sueltan un archivo recien escrito en unos ms: se reintenta.
bool RenameRetry(const fs::path& from, const fs::path& to) {
  for (int i = 0; i < 10; ++i) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return false;
}

// Archivos del propio mod en la raiz (no documentos): con ellos la carpeta ya es el mod.
bool RootHasModFiles(const fs::path& d) {
  std::error_code ec;
  for (const char* n : {"us", "eu", "personaje.toml", "traje.toml", "roster.toml", "manifest.txt", "ui_ingame"}) {
    if (fs::exists(d / n, ec)) return true;
  }
  uint32_t w, h;
  for (auto [it, iec] = std::pair{fs::directory_iterator(d, ec), std::error_code{}};
       !iec && it != fs::directory_iterator(); it.increment(iec)) {
    if (it->is_regular_file(ec) && PackStem(rex::path_to_utf8(it->path().stem()), w, h)) return true;
  }
  return false;
}

// Carpeta unica que envuelve el mod (ignora __MACOSX); vacia si no hay que desenvolver.
// B11: los documentos sueltos (LEEME, capturas...) ya no impiden desenvolver.
fs::path WrappedModDir(const fs::path& d) {
  std::error_code ec;
  fs::path inner;
  for (auto [it, iec] = std::pair{fs::directory_iterator(d, ec), std::error_code{}};
       !iec && it != fs::directory_iterator(); it.increment(iec)) {
    if (!it->is_directory(ec) || it->path().filename() == "__MACOSX") continue;
    if (!inner.empty()) return {};
    inner = it->path();
  }
  if (inner.empty() || RootHasModFiles(d) || !HasModContent(inner)) return {};
  return inner;
}

// Mete los archivos sueltos de 'from' en 'to' sin pisar nada (choque -> to/_del_zip/).
void MoveLooseFiles(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (auto [it, iec] = std::pair{fs::directory_iterator(from, ec), std::error_code{}};
       !iec && it != fs::directory_iterator(); it.increment(iec)) {
    if (it->is_regular_file(ec)) files.push_back(it->path());
  }
  for (const auto& f : files) {
    fs::path target = to / f.filename();
    if (fs::exists(target, ec)) {
      fs::create_directories(to / "_del_zip", ec);
      target = to / "_del_zip" / f.filename();
      if (fs::exists(target, ec)) continue;   // se queda donde estaba (no se pierde)
    }
    RenameRetry(f, target);
  }
}

// Publica 'ready' (completa, dentro de mods/) como mods/<name>. Si ya habia uno, va al respaldo
// y vuelve si la publicacion falla (compensacion del primer paso).
bool PublishMod(const fs::path& ready, const std::string& name, std::string& err) {
  std::error_code ec;
  const fs::path root = ModsRoot(), dest = root / fs::u8path(name);   // nombres UTF-8 (tildes, japones)
  // la version anterior, activa ("X") o desactivada ("X.disabled"), va al respaldo como Nova's Mods
  std::vector<std::pair<fs::path, fs::path>> moved;   // (respaldo, sitio original)
  auto restore = [&] {
    for (const auto& [bak, old] : moved) {
      if (!RenameRetry(bak, old)) REXLOG_ERROR("dbz3: mod anterior queda en {}", rex::path_to_utf8(bak));
    }
  };
  for (const fs::path& old : {dest, root / fs::u8path(name + ".disabled")}) {
    if (!fs::exists(old, ec)) continue;
    const fs::path bak = root.parent_path() / "mods_respaldo" / fs::u8path(rex::path_to_utf8(old.filename()) + "_" + Stamp());
    fs::create_directories(bak.parent_path(), ec);
    if (!RenameRetry(old, bak)) {
      restore();
      err = std::string(dbz3::i18n::T("el mod '", "the mod '")) + name +
            dbz3::i18n::T("' ya instalado está abierto en otro programa. Cierra el juego (y el Mod Kit o la "
                          "carpeta mods en el Explorador) y vuelve a instalarlo.",
                          "' that is already installed is open in another program. Close the game (and the "
                          "Mod Kit or the mods folder in Explorer) and install it again.");
      return false;
    }
    moved.emplace_back(bak, old);
  }
  if (!RenameRetry(ready, dest)) {
    restore();
    err = dbz3::i18n::T("no se pudo copiar el mod a la carpeta mods. Cierra lo que la tenga abierta; si el juego "
                        "está dentro de Archivos de programa, muévelo a otra carpeta (por ejemplo Documentos).",
                        "the mod could not be copied into the mods folder. Close whatever has it open; if the "
                        "game is inside Program Files, move it to another folder (for example Documents).");
    return false;
  }
  for (const auto& [bak, old] : moved) {
    REXLOG_INFO("dbz3: mod '{}' sustituido; el anterior en {}", name, rex::path_to_utf8(bak));
  }
  return true;
}

// Extraccion terminada en 'staging' (oculta, en mods/): desenvuelve y publica.
std::string SanitizeModName(const std::string& raw);   // mas abajo (Windows / POSIX)

// Pack de varios mods (el zip oficial de personajes trae LEEME + mods/_roster, mods/cut_*): cada
// carpeta de 'mods' de dentro se publica como un mod suelto (PublishMod respalda el que hubiera) y
// lo que sobra (documentos) va a mods_respaldo. Devuelve el primer mod publicado ("" = no aplica).
std::string PublishModsFolder(const fs::path& container, const std::string& label, std::string& err) {
  std::error_code ec;
  const fs::path inner = container / "mods";
  if (HasModContent(container) || !fs::is_directory(inner, ec)) return {};
  std::vector<fs::path> kids;
  for (auto [it, iec] = std::pair{fs::directory_iterator(inner, ec), std::error_code{}};
       !iec && it != fs::directory_iterator(); it.increment(iec)) {
    const std::string kid = rex::path_to_utf8(it->path().filename());
    if (it->is_directory(ec) && !kid.empty() && kid[0] != '.' && HasModContent(it->path())) kids.push_back(it->path());
  }
  std::string first;
  for (const fs::path& kid : kids) {
    const std::string name = SanitizeModName(rex::path_to_utf8(kid.filename()));
    std::string e;
    if (PublishMod(kid, name, e)) {
      if (first.empty()) first = name;
    } else {
      err = e;
      REXLOG_WARN("dbz3: pack '{}': no se pudo instalar '{}' ({})", label, name, e);
    }
  }
  if (!first.empty()) {
    fs::create_directories(ModsRoot().parent_path() / "mods_respaldo", ec);
    RenameRetry(container, ModsRoot().parent_path() / "mods_respaldo" / fs::u8path(label + "_documentos_" + Stamp()));
    REXLOG_INFO("dbz3: pack '{}' repartido en {} mods", label, kids.size());
  }
  return first;
}

bool FinishInstall(const fs::path& staging, const std::string& name, std::string& out_name,
                   std::string& out_error) {
  std::error_code ec;
  if (std::string multi_err, first = PublishModsFolder(staging, name, multi_err); !first.empty()) {
    out_name = first;   // un mod real (SetModEnabled sobre el nombre del zip crearia una carpeta)
    return true;
  }
  fs::path ready = staging;
  if (const fs::path inner = WrappedModDir(staging); !inner.empty()) {
    MoveLooseFiles(staging, inner);   // los documentos del zip viajan con el mod
    ready = inner;
  }
  if (!PublishMod(ready, name, out_error)) {
    fs::remove_all(staging, ec);      // solo la copia extraida: el zip sigue intacto
    return false;
  }
  if (ready != staging) {
    // queda __MACOSX / carpetas vacias; si algun documento no se pudo mover, no se pierde
    bool loose_left = false;
    for (auto [it, iec] = std::pair{fs::directory_iterator(staging, ec), std::error_code{}};
         !iec && it != fs::directory_iterator(); it.increment(iec)) {
      loose_left = loose_left || it->is_regular_file(ec);
    }
    if (!loose_left) {
      fs::remove_all(staging, ec);
    } else {
      RenameRetry(staging, ModsRoot().parent_path() / "mods_respaldo" / fs::u8path(name + "_documentos_" + Stamp()));
    }
  }
  out_name = name;
  REXLOG_INFO("dbz3: mod instalado desde zip -> '{}'", name);
  return true;
}
}  // namespace

// B16: se mueve la carpeta entera (un rename), no archivo a archivo; si algo falla vuelve igual.
bool FixNestedMod(const std::string& mod_name) {
  std::error_code ec;
  const fs::path root = ModsRoot(), dir = root / fs::u8path(mod_name);
  if (std::string err; !PublishModsFolder(dir, mod_name, err).empty()) return true;
  const fs::path inner = WrappedModDir(dir);
  if (inner.empty()) return false;
  const fs::path work = root / (".arreglo_" + Stamp());
  if (!RenameRetry(dir, work)) return false;               // nada ha cambiado
  const fs::path inner_w = work / inner.filename();
  MoveLooseFiles(work, inner_w);
  if (!RenameRetry(inner_w, dir)) {
    RenameRetry(work, dir);                                // vuelve como estaba (docs ya dentro)
    return false;
  }
  if (fs::is_empty(work, ec)) {
    fs::remove(work, ec);
  } else {                                                 // __MACOSX y demas: al respaldo
    fs::create_directories(root.parent_path() / "mods_respaldo", ec);
    RenameRetry(work, root.parent_path() / "mods_respaldo" / fs::u8path(mod_name + "_envoltorio_" + Stamp()));
  }
  REXLOG_INFO("dbz3: mod '{}' sacado de su carpeta interior", mod_name);
  return true;
}

std::vector<ModInfo> ListMods(bool check_problems) {
  std::vector<ModInfo> result;
  std::error_code ec;
  const std::filesystem::path mods_root = ModsRoot();
  if (!std::filesystem::is_directory(mods_root, ec)) {
    return {};
  }
  for (auto [it_7, iec_7] = std::pair{std::filesystem::directory_iterator(mods_root, ec), std::error_code{}}; !iec_7 && it_7 != std::filesystem::directory_iterator(); it_7.increment(iec_7)) {
    const auto& mod_entry = *it_7;
    // A mod that cannot be read (path too long, odd name...) is skipped, never a crash.
    try {
    std::error_code fec;
    if (!mod_entry.is_directory(fec)) {
      continue;
    }
    const std::string raw_name = rex::path_to_utf8(mod_entry.path().filename());
    if (!raw_name.empty() && raw_name[0] == '.') continue;   // .install_* a medias (launcher cerrado al instalar)
    if (EndsWithDotDisabled(raw_name) &&
        std::filesystem::exists(mods_root / std::filesystem::u8path(StripDotDisabled(raw_name)))) {
      continue;
    }
    ModInfo info;
    info.name = StripDotDisabled(raw_name);
    info.enabled = !FolderIsDisabled(mod_entry.path(), raw_name);
    LoadManifest(mod_entry.path(), info);
    {   // D4: version del juego con la que se dejo activo el mod
      std::ifstream ver(mod_entry.path() / ".dbz3_version");
      std::string linea;
      if (ver && std::getline(ver, linea)) {
        while (!linea.empty() && (linea.back() == '\r' || linea.back() == ' ')) linea.pop_back();
        info.made_for = linea;
      }
    }
    InferTypeAndCount(mod_entry.path(), info);
    if (check_problems) CheckProblems(mod_entry.path(), info);
    result.push_back(std::move(info));
    } catch (const std::exception& ex) {
      REXLOG_WARN("dbz3: mod '{}' skipped while listing: {}", rex::path_to_utf8(mod_entry.path()), ex.what());
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ModInfo& a, const ModInfo& b) {
                     if (a.enabled != b.enabled) {
                       return a.enabled;
                     }
                     return a.name < b.name;
                   });
  return result;
}

void StampModVersion(const std::string& mod_name, const std::string& version) {
  if (mod_name.empty() || version.empty()) return;
  std::error_code ec;
  const std::filesystem::path dir = ModsRoot() / std::filesystem::u8path(mod_name);
  if (!std::filesystem::is_directory(dir, ec)) return;
  std::ofstream out(dir / ".dbz3_version", std::ios::trunc);
  if (out) out << version << "\n";
  dbz3::mod_manifest::AppendValue(dir / "manifest.txt", "dbz3_version", version);
}

void SetModEnabled(const std::string& mod_name, bool enable) {
  const std::filesystem::path mods_root = ModsRoot();
  const std::filesystem::path mod_dir = mods_root / std::filesystem::u8path(mod_name);
  const std::filesystem::path marker = mod_dir / ".disabled";
  std::error_code ec;
  if (enable) {
    std::filesystem::remove(marker, ec);
    const std::filesystem::path suffixed = mods_root / std::filesystem::u8path(mod_name + ".disabled");
    if (!std::filesystem::exists(mod_dir) &&
        std::filesystem::exists(suffixed)) {
      std::filesystem::rename(suffixed, mod_dir, ec);
    }
  } else {
    std::filesystem::create_directories(mod_dir, ec);
    std::ofstream marker_file(marker);
    marker_file << "disabled\n";
  }
  REXLOG_INFO("dbz3: mod '{}' {}", mod_name, enable ? "enabled" : "disabled");
}

namespace {

std::filesystem::path ModDir(const std::string& mod_name) {
  return ModsRoot() / std::filesystem::u8path(StripDotDisabled(mod_name));
}

std::string ReadManifestValue(const std::filesystem::path& manifest,
                              const std::string& key) {
  std::ifstream in(manifest);
  if (!in.is_open()) {
    return "";
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string k = line.substr(0, eq);
    auto trim = [](std::string& s) {
      size_t b = 0;
      while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
      s.erase(0, b);
    };
    trim(k);
    if (k == key) {
      std::string v = line.substr(eq + 1);
      trim(v);
      return v;
    }
  }
  return "";
}

}  // namespace

std::string GetModManifestValue(const std::string& mod_name,
                                const std::string& key) {
  auto dir = ModDir(mod_name);
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    dir = ModsRoot() / std::filesystem::u8path(StripDotDisabled(mod_name) + ".disabled");
  }
  return ReadManifestValue(dir / "manifest.txt", key);
}

bool SetModManifestValue(const std::string& mod_name, const std::string& key,
                         const std::string& value) {
  const std::filesystem::path dir = ModDir(mod_name);
  const std::filesystem::path manifest = dir / "manifest.txt";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  std::vector<std::string> keep;
  if (std::filesystem::exists(manifest, ec)) {
    std::ifstream in(manifest);
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                               line.back() == '\t')) {
        line.pop_back();
      }
      const size_t eq = line.find('=');
      bool drop = false;
      if (eq != std::string::npos) {
        std::string k = line.substr(0, eq);
        auto trim = [](std::string& s) {
          size_t b = 0;
          while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
          s.erase(0, b);
        };
        trim(k);
        drop = (k == key);
      }
      if (!drop) keep.push_back(line);
    }
  }
  if (!value.empty()) {
    keep.push_back(key + "=" + value);
  }
  std::ofstream out(manifest);
  if (!out.is_open()) {
    REXLOG_WARN("dbz3: no se pudo escribir manifest de '{}'", mod_name);
    return false;
  }
  for (const auto& line : keep) {
    out << line << "\n";
  }
  REXLOG_INFO("dbz3: manifest '{}' key '{}' actualizada", mod_name, key);
  return true;
}

const char* ModTypeLabel(const std::string& type) {
  if (type == "swap_b3") return "swap B3";
  if (type == "port_b3") return "port B3";
  if (type == "audio") return "audio";
  if (type == "moveset") return "moveset";
  if (type == "data") return "data";
  if (type == "personaje") return "character";
  if (type == "generado") return "generated";
  return "other";
}

int ModTypeColor(const std::string& type) {
  if (type == "swap_b3" || type == "port_b3") return 0xFFB347;  // orange
  if (type == "audio") return 0x4FC3F7;                          // light blue
  if (type == "moveset") return 0x81C784;                        // green
  if (type == "data") return 0xCFD8DC;                           // gray-blue
  if (type == "personaje") return 0xCE93D8;                      // purple
  if (type == "generado") return 0x90A4AE;                       // slate
  return -1;  // dim gray
}

// ---------------------------------------------------------------------------
// Zip install + profiles
// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) |
                       uint32_t(data[i + 2]);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
    i += 3;
  }
  if (i + 1 == len) {
    const uint32_t v = uint32_t(data[i]) << 16;
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == len) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

// Runs a command line hidden (no console window). Blocks up to `timeout_ms` and
// returns whether the process exited 0. On timeout the process is terminated: a
// pack HD de 3 GB tardaba mas de 60 s y el instalador borraba la carpeta temporal
// mientras Expand-Archive seguia escribiendo en ella.
bool RunHidden(std::wstring cmdline, DWORD timeout_ms) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  DWORD code = 1;
  if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_OBJECT_0) {
    GetExitCodeProcess(pi.hProcess, &code);
  } else {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 10000);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

// PowerShell via -EncodedCommand (immune to quoting/encoding issues in paths).
bool RunHiddenPowerShell(const std::wstring& script, DWORD timeout_ms) {
  std::vector<uint8_t> le;
  le.reserve(script.size() * 2);
  for (wchar_t c : script) {
    le.push_back(uint8_t(c & 0xFF));
    le.push_back(uint8_t((c >> 8) & 0xFF));
  }
  const std::string b64 = Base64Encode(le.data(), le.size());
  std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand ";
  cmdline.append(b64.begin(), b64.end());
  return RunHidden(std::move(cmdline), timeout_ms);
}

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring out(len > 1 ? len - 1 : 0, L'\0');
  if (len > 1) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
  }
  return out;
}

std::string SanitizeModName(const std::string& raw) {
  std::string s = raw;
  for (char& c : s) {
    // ';' separa la lista de packs de texturas (dbz3_texture_packs): rompia "Pack;HD.zip".
    // unsigned: los bytes UTF-8 (tildes) son >= 0x80, no caracteres de control
    if (static_cast<unsigned char>(c) < 32 || std::strchr("\\/:*?\"<>|;", c)) {
      c = '_';
    }
  }
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) {
    s.pop_back();
  }
  // un nombre con punto delante seria una carpeta oculta (ListMods no la ve)
  while (!s.empty() && s.front() == '.') s.erase(s.begin());
  if (s.empty()) {
    s = "mod";
  }
  return s;
}

// Descomprime con el tar.exe de Windows (10 1803+; un pack HD de 2,9 GB en segundos) y,
// si no esta o falla, con Expand-Archive. Sin el tope de 60 s de antes.
bool ExtractZipWin(const std::wstring& zip, const std::filesystem::path& dest) {
  constexpr DWORD kTimeout = 30 * 60 * 1000;
  std::error_code ec;
  std::filesystem::create_directories(dest, ec);
  wchar_t sys[MAX_PATH] = {};
  const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    const std::wstring tar = std::wstring(sys) + L"\\tar.exe";
    if (std::filesystem::exists(tar, ec) &&
        RunHidden(L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + dest.wstring() + L"\"", kTimeout)) {
      return true;
    }
  }
  std::filesystem::remove_all(dest, ec);
  const auto ps_quote = [](const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) {
      out += (c == L'\'') ? L"''" : std::wstring(1, c);
    }
    out += L"'";
    return out;
  };
  return RunHiddenPowerShell(L"Expand-Archive -LiteralPath " + ps_quote(zip) + L" -DestinationPath " +
                                 ps_quote(dest.wstring()) + L" -Force",
                             kTimeout);
}

}  // namespace

bool InstallModFromZip(const std::string& zip_path_utf8, std::string& out_name,
                       std::string& out_error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path mods_root = ModsRoot();
  fs::create_directories(mods_root, ec);

  const std::wstring zip_wide = Utf8ToWide(zip_path_utf8);
  if (zip_wide.empty()) {
    out_error = dbz3::i18n::T("no se encuentra el .zip. Muévelo a una carpeta normal (por ejemplo Descargas) y "
                              "vuelve a probar.",
                              "the .zip could not be found. Move it to a normal folder (for example Downloads) "
                              "and try again.");
    return false;
  }
  const fs::path zip_fs(zip_wide);
  const std::string base =
      SanitizeModName(rex::path_to_utf8(zip_fs.stem()));

  // Extraccion en una carpeta oculta y nueva de mods/ (nunca se reutiliza ni se borra una vieja).
  const fs::path temp_dir = mods_root / (".install_" + Stamp());
  if (!ExtractZipWin(zip_wide, temp_dir)) {
    fs::remove_all(temp_dir, ec);
    out_error = dbz3::i18n::T("no se pudo descomprimir el .zip. Puede estar a medio descargar o dañado: "
                              "descárgalo otra vez. Si tiene contraseña, descomprímelo tú dentro de la carpeta "
                              "mods. Mira también que quede espacio libre en el disco.",
                              "the .zip could not be extracted. It may be incomplete or damaged: download it "
                              "again. If it has a password, extract it yourself into the mods folder. Also "
                              "check that there is free disk space.");
    return false;
  }
  return FinishInstall(temp_dir, base, out_name, out_error);
}

#else  // !REX_PLATFORM_WIN32

namespace {

std::string SanitizeModName(const std::string& raw) {
  std::string s = raw;
  for (char& c : s) {
    if (static_cast<unsigned char>(c) < 32 || c == '/' || c == '\\' || c == ';') c = '_';
  }
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
  while (!s.empty() && s.front() == '.') s.erase(s.begin());
  return s.empty() ? "mod" : s;
}

bool ExtractZip(const std::filesystem::path& zip,
                const std::filesystem::path& destination) {
  const std::string program = "unzip";
  const std::string zip_arg = zip.string();
  const std::string dest_arg = destination.string();
  std::vector<char*> argv = {
      const_cast<char*>(program.c_str()), const_cast<char*>("-q"),
      const_cast<char*>("-o"), const_cast<char*>(zip_arg.c_str()),
      const_cast<char*>("-d"), const_cast<char*>(dest_arg.c_str()), nullptr};
  pid_t pid = -1;
  if (posix_spawnp(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    return false;
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

bool InstallModFromZip(const std::string& zip_path_utf8, std::string& out_name,
                       std::string& out_error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path mods_root = ModsRoot();
  fs::create_directories(mods_root, ec);
  const fs::path zip = fs::u8path(zip_path_utf8);
  if (!fs::is_regular_file(zip, ec)) {
    out_error = dbz3::i18n::T("no se encuentra el .zip. Muévelo a una carpeta normal (por ejemplo Descargas) y "
                              "vuelve a probar.",
                              "the .zip could not be found. Move it to a normal folder (for example Downloads) "
                              "and try again.");
    return false;
  }

  const std::string base = SanitizeModName(rex::path_to_utf8(zip.stem()));
  const fs::path temp_dir = mods_root / (".install_" + Stamp());
  fs::create_directories(temp_dir, ec);
  if (!ExtractZip(zip, temp_dir)) {
    fs::remove_all(temp_dir, ec);
    out_error = dbz3::i18n::T("no se pudo descomprimir el .zip: instala el paquete 'unzip' y vuelve a probar.",
                              "the .zip could not be extracted: install the 'unzip' package and try again.");
    return false;
  }
  return FinishInstall(temp_dir, base, out_name, out_error);
}

#endif  // REX_PLATFORM_WIN32

namespace {

std::filesystem::path ProfilesFile() { return ModsRoot() / "profiles.txt"; }

void LoadProfiles(std::map<std::string, std::vector<std::string>>& out) {
  out.clear();
  std::ifstream in(ProfilesFile());
  if (!in.is_open()) {
    return;
  }
  std::string line;
  std::string current;
  while (std::getline(in, line)) {
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM (guardado con el Bloc de notas)
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[' && line.back() == ']') {   // "[HD] Goku" es un mod, no un perfil
      current = line.substr(1, line.size() - 2);
    } else if (!current.empty()) {
      out[current].push_back(line);
    }
  }
}

}  // namespace

std::vector<std::string> ListProfiles() {
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  std::vector<std::string> names;
  names.reserve(profiles.size());
  for (const auto& kv : profiles) {
    names.push_back(kv.first);
  }
  return names;
}

std::vector<std::string> ProfileEnabledMods(const std::string& profile) {
  if (profile == "vanilla") {
    return {};
  }
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  const auto it = profiles.find(profile);
  return it == profiles.end() ? std::vector<std::string>{} : it->second;
}

bool SaveProfile(const std::string& name,
                 const std::vector<std::string>& enabled_mods) {
  if (name.empty() || name == "vanilla") {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(ModsRoot(), ec);
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  profiles[name] = enabled_mods;

  std::ofstream out(ProfilesFile());
  if (!out.is_open()) {
    REXLOG_WARN("dbz3: no se pudo escribir profiles.txt");
    return false;
  }
  out << "# Perfiles de mods (generados por el launcher). 'vanilla' no se "
         "guarda.\n";
  for (const auto& kv : profiles) {
    out << "[" << kv.first << "]\n";
    for (const auto& mod : kv.second) {
      out << mod << "\n";
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' guardado ({} mods)", name, enabled_mods.size());
  return true;
}

bool DeleteProfile(const std::string& name) {
  if (name.empty() || name == "vanilla") {
    return false;
  }
  std::map<std::string, std::vector<std::string>> profiles;
  LoadProfiles(profiles);
  if (profiles.erase(name) == 0) {
    return false;
  }
  std::ofstream out(ProfilesFile());
  if (!out.is_open()) {
    return false;
  }
  out << "# Perfiles de mods (generados por el launcher). 'vanilla' no se "
         "guarda.\n";
  for (const auto& kv : profiles) {
    out << "[" << kv.first << "]\n";
    for (const auto& mod : kv.second) {
      out << mod << "\n";
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' borrado", name);
  return true;
}

void ApplyProfile(const std::string& name) {
  const std::vector<std::string> want = ProfileEnabledMods(name);
  for (const ModInfo& mod : ListMods(false)) {
    const bool on = std::find(want.begin(), want.end(), mod.name) != want.end();
    if (mod.enabled != on) {
      SetModEnabled(mod.name, on);
    }
  }
  REXLOG_INFO("dbz3: perfil '{}' aplicado ({} mods activos)", name, want.size());
}

std::vector<ModConflict> FindConflicts() {
  namespace fs = std::filesystem;
  std::vector<std::string> mods;
  for (const ModInfo& m : ListMods(false)) {
    // _roster tambien: si otro mod sustituye una entrada que el montaje usa (rueda del select, poses,
    // modelos nuevos), el jugador tiene que saber cual se ve.
    if (m.enabled) mods.push_back(m.name);
  }
  // mismo orden que ScanModDirs del runtime (ruta de carpeta, alfabetico)
  std::sort(mods.begin(), mods.end(), [](const std::string& a, const std::string& b) {
    return fs::u8path(a) < fs::u8path(b);
  });
  std::map<std::string, std::string> owner;                  // recurso -> primer mod (el que gana)
  std::map<std::pair<std::string, std::string>, int> pairs;  // (gana, pierde) -> recursos
  for (const std::string& mod : mods) {
    for (const char* region : {"us", "eu"}) {
      const fs::path base = ModsRoot() / fs::u8path(mod) / region;
      std::error_code ec;
      for (auto [it, iec] = std::pair{fs::directory_iterator(base, ec), std::error_code{}};
           !iec && it != fs::directory_iterator(); it.increment(iec)) {
        const std::string top = rex::path_to_utf8(it->path().filename());
        std::vector<std::string> keys;
        if (it->is_regular_file(ec)) {
          keys.push_back(top);                                  // archivo entero (data_*.afs...)
        } else if (it->is_directory(ec)) {
          for (auto [e, eec] = std::pair{fs::directory_iterator(it->path(), ec), std::error_code{}};
               !eec && e != fs::directory_iterator(); e.increment(eec)) {
            keys.push_back(top + "/" + rex::path_to_utf8(e->path().filename()));   // entrada <afs>/<n>
          }
        }
        for (const std::string& k : keys) {
          const auto [at, fresh] = owner.emplace(std::string(region) + "/" + k, mod);
          if (!fresh && at->second != mod) ++pairs[{at->second, mod}];
        }
      }
    }
  }
  std::vector<ModConflict> out;
  for (const auto& [p, n] : pairs) out.push_back({p.first, p.second, n});
  return out;
}

// ---- memoria de la ultima partida: una palabra en dbz3_ultima_partida.txt junto al exe
namespace {
std::filesystem::path SessionFile() { return ModsRoot().parent_path() / "dbz3_ultima_partida.txt"; }
std::string SessionState() {
  std::ifstream in(SessionFile());
  std::string s;
  std::getline(in, s);
  return s;
}
void SetSessionState(const char* s) {
  std::ofstream(SessionFile(), std::ios::trunc) << s << "\n";
}
std::vector<std::string> EnabledMods() {
  std::vector<std::string> on;
  for (const ModInfo& m : ListMods(false)) {
    if (m.enabled && m.name != "_roster") on.push_back(m.name);   // _roster lo genera el kit
  }
  return on;
}
}  // namespace

// Sesion vanilla online: no cuenta como partida del jugador (no pisa su memoria de "ultima partida buena").
bool VanillaSession() { return std::getenv("DBZ3_VANILLA_SESSION") != nullptr; }

// Fichero de reconexion de la sesion vanilla: lo escribe el PC que se reinicia y lo lee (y borra, es nuestro temporal
// en la carpeta temp del usuario, no un dato suyo) el arranque vanilla. Su ruta viaja en DBZ3_VANILLA_PENDING.
bool TakeVanillaPending(std::string& out) {
  out.clear();
  const char* path = std::getenv("DBZ3_VANILLA_PENDING");
  if (!path || !*path) return false;
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::u8path(path);
  {
    std::ifstream f(p, std::ios::binary);
    if (f) out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  std::filesystem::remove(p, ec);
#if REX_PLATFORM_WIN32
  SetEnvironmentVariableA("DBZ3_VANILLA_PENDING", nullptr);   // una sola vez por arranque
#else
  unsetenv("DBZ3_VANILLA_PENDING");
#endif
  return !out.empty();
}

bool RelaunchVanilla(const std::string& pending, bool skip_launcher) {
#if REX_PLATFORM_WIN32
  if (VanillaSession()) return false;
  std::error_code ec;
  std::filesystem::create_directories(ModsRoot().parent_path() / "mods_vanilla", ec);   // raiz de mods vacia de la sesion
  std::string pend_path;
  if (!pending.empty()) {
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path(ec) / ("dbz3_vanilla_" + std::to_string(GetCurrentProcessId()) + ".txt");
    std::ofstream f(tmp, std::ios::binary);
    f << pending;
    f.close();
    if (!f) return false;
    pend_path = rex::path_to_utf8(tmp);
  }
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring cmd = GetCommandLineW();
  if (skip_launcher && cmd.find(L"dbz3_skip_launcher") == std::wstring::npos) cmd += L" --dbz3_skip_launcher";
  SetEnvironmentVariableA("DBZ3_VANILLA_SESSION", "1");   // lo hereda el hijo; este proceso se cierra enseguida
  if (!pend_path.empty()) SetEnvironmentVariableA("DBZ3_VANILLA_PENDING", pend_path.c_str());
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  const BOOL ok = CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  SetEnvironmentVariableA("DBZ3_VANILLA_SESSION", nullptr);
  SetEnvironmentVariableA("DBZ3_VANILLA_PENDING", nullptr);
  if (!ok) {
    if (!pend_path.empty()) std::filesystem::remove(std::filesystem::u8path(pend_path), ec);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  REXLOG_INFO("dbz3: reiniciando en modo vanilla (sesion online sin mods)");
  SessionEnd(true);   // cierre normal: su memoria de "ultima partida buena" queda como estaba
  rex::FlushLogging();
  std::_Exit(0);
#else
  (void)pending; (void)skip_launcher;
  return false;
#endif
}

void SessionStart() { if (!VanillaSession()) SetSessionState("en_marcha"); }

void SessionEnd(bool ok) {
  if (VanillaSession()) return;
  if (SessionState() != "en_marcha") return;   // sin JUGAR desde el launcher (o ya cerrada)
  if (!ok) {   // desde el gestor de cierres: solo la marca, nada pesado
    SetSessionState("cerrado_de_golpe");
    return;
  }
  SetSessionState("ok");
  SaveProfile(kGoodProfile, EnabledMods());
}

std::vector<std::string> SessionSuspects() {
  if (VanillaSession()) return {};
  const std::string st = SessionState();
  if (st.empty() || st == "ok") return {};
  const auto good = ProfileEnabledMods(kGoodProfile);
  std::vector<std::string> out;
  for (const std::string& m : EnabledMods()) {
    // sin partida buena anterior no se sabe cual fue: se ofrecen todos los activos
    if (std::find(good.begin(), good.end(), m) == good.end()) out.push_back(m);
  }
  REXLOG_WARN("dbz3: la ultima partida no termino bien ({}); mods nuevos desde la buena: {}", st, out.size());
  return out;
}

void SessionForget() { SetSessionState("ok"); }

}  // namespace dbz3
