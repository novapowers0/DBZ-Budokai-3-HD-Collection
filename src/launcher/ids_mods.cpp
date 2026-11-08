// dbz3 launcher - reparto de numeros de los mods (ver ids_mods.h). Solo biblioteca estandar: lo prueba
// tools/test_ids_mods.cpp con mods falsos sin compilar el launcher entero.
#include "ids_mods.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace dbz3::launcher::ids {
namespace {
namespace fs = std::filesystem;

constexpr int kUltima = 9999;
const char* kRegistro = "_ids_escenarios.toml";

std::string Trim(std::string s) {
  const auto no = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && no(s.front())) s.erase(0, 1);
  while (!s.empty() && no(s.back())) s.pop_back();
  return s;
}

// "clave = valor  # nota" -> clave, valor (sin comillas ni nota). false si no es una linea clave = valor.
bool ClaveValor(const std::string& raw, std::string& k, std::string& v) {
  const std::string s = Trim(raw);
  if (s.empty() || s.front() == '#' || s.front() == '[') return false;
  const size_t eq = s.find('=');
  if (eq == std::string::npos) return false;
  k = Trim(s.substr(0, eq));
  v = Trim(s.substr(eq + 1));
  if (!v.empty() && v.front() == '"') {
    const size_t end = v.find('"', 1);
    v = v.substr(1, end == std::string::npos ? std::string::npos : end - 1);
  } else if (const size_t h = v.find('#'); h != std::string::npos) {
    v = Trim(v.substr(0, h));
  }
  return true;
}

std::vector<std::string> Lineas(const fs::path& f) {
  std::vector<std::string> out;
  std::ifstream in(f);
  for (std::string l; std::getline(in, l);) out.push_back(l);
  return out;
}

// Bloques [[seccion]] de un toml sencillo: por cada bloque, sus clave = valor.
std::vector<std::map<std::string, std::string>> Bloques(const fs::path& f, const std::string& seccion) {
  std::vector<std::map<std::string, std::string>> out;
  bool dentro = false;
  for (const auto& l : Lineas(f)) {
    const std::string s = Trim(l);
    if (!s.empty() && s.front() == '[') {
      dentro = s == "[[" + seccion + "]]" || s == "[" + seccion + "]";
      if (dentro) out.emplace_back();
      continue;
    }
    std::string k, v;
    if (dentro && ClaveValor(s, k, v)) out.back()[k] = v;
  }
  return out;
}

int Num(const std::map<std::string, std::string>& b, const char* k, int def = -1) {
  const auto it = b.find(k);
  if (it == b.end() || it->second.empty()) return def;
  char* end = nullptr;
  const long n = std::strtol(it->second.c_str(), &end, 10);
  return end && *end == 0 ? int(n) : def;
}

struct Esc {
  int entrada = -1, menu = -1;
};

// Igual que src/escenarios.cpp CargarDeMods: [[escenario]] con `entrada`; `menu` por defecto entrada + 100.
std::vector<Esc> Escenarios(const fs::path& mod) {
  std::vector<Esc> out;
  std::error_code ec;
  if (!fs::is_regular_file(mod / "stage.toml", ec)) return out;
  for (const auto& b : Bloques(mod / "stage.toml", "escenario")) {
    Esc e{Num(b, "entrada"), Num(b, "menu", 0)};
    if (e.entrada < 0) continue;
    if (e.menu <= 0) e.menu = e.entrada + 100;
    out.push_back(e);
  }
  return out;
}

// Entradas de data_cmn.afs que trae la carpeta del mod (us/data_cmn.afs/<n>/...).
std::vector<int> EntradasCmn(const fs::path& mod) {
  std::vector<int> out;
  std::error_code ec;
  const fs::path base = mod / "us" / "data_cmn.afs";
  if (!fs::is_directory(base, ec)) return out;
  for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string n = it->path().stem().string();
    char* fin = nullptr;
    const long v = std::strtol(n.c_str(), &fin, 10);
    if (!n.empty() && fin && *fin == 0) out.push_back(int(v));
  }
  return out;
}

struct Apunte {
  std::string mod;
  int antes = 0, despues = 0, menu = 0;
};

std::vector<Apunte> LeerRegistro(const fs::path& f) {
  std::vector<Apunte> out;
  for (const auto& b : Bloques(f, "cambio")) {
    const auto it = b.find("mod");
    Apunte a{it == b.end() ? "" : it->second, Num(b, "antes"), Num(b, "despues"), Num(b, "menu")};
    if (!a.mod.empty() && a.antes > 0 && a.despues > 0) out.push_back(a);
  }
  return out;
}

void GuardarRegistro(const fs::path& f, const std::vector<Apunte>& regs) {
  std::ostringstream o;
  o << "# GENERADO por el launcher (src/launcher/ids_mods.cpp): numero de data_cmn.afs que se dio a cada escenario\n"
       "# de mod que chocaba con otro. El mismo mod conserva siempre su numero. Editar a mano solo a proposito.\n";
  for (const auto& a : regs)
    o << "\n[[cambio]]\nmod = \"" << a.mod << "\"\nantes = " << a.antes << "\ndespues = " << a.despues
      << "\nmenu = " << a.menu << "\n";
  std::error_code ec;
  const fs::path tmp = f.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << o.str();
  }
  fs::rename(tmp, f, ec);
}

// Mueve us/data_cmn.afs/<de> a <a> dentro del mod. Lo que ya hubiera en <a> se aparta a mods/_respaldo_ids.
void Mover(const fs::path& root, const std::string& mod, int de, int a) {
  std::error_code ec;
  const fs::path base = root / fs::u8path(mod) / "us" / "data_cmn.afs";
  for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().stem().string() != std::to_string(de)) continue;
    const fs::path dst = base / (std::to_string(a) + it->path().extension().string());
    if (fs::exists(dst, ec)) {
      const fs::path resp = root / "_respaldo_ids";
      fs::create_directories(resp, ec);
      fs::rename(dst, resp / fs::u8path(mod + "_" + std::to_string(a) + "_" + std::to_string(std::time(nullptr))), ec);
    }
    fs::rename(it->path(), dst, ec);
    return;
  }
}

// Reescribe entrada/menu/zona_b del stage.toml del mod con los numeros nuevos (el resto se conserva).
void ReescribirToml(const fs::path& f, const std::map<int, int>& entradas, const std::map<int, int>& menus) {
  std::ostringstream o;
  for (const auto& l : Lineas(f)) {
    std::string k, v;
    const std::map<int, int>* m = nullptr;
    if (ClaveValor(l, k, v)) m = (k == "entrada" || k == "zona_b") ? &entradas : k == "menu" ? &menus : nullptr;
    const auto it = m ? m->find(std::atoi(v.c_str())) : std::map<int, int>::const_iterator{};
    if (m && it != m->end() && !v.empty())
      o << k << " = " << it->second << "    # antes " << it->first << ": el launcher le dio un numero libre (chocaba)\n";
    else
      o << l << "\n";
  }
  std::error_code ec;
  const fs::path tmp = f.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << o.str();
  }
  fs::rename(tmp, f, ec);
}

}  // namespace

Revision Revisar(const fs::path& root, std::vector<std::string> activos) {
  static std::mutex mu;   // el hilo de la lista de mods y JUGAR pueden coincidir
  const std::lock_guard<std::mutex> lock(mu);
  std::sort(activos.begin(), activos.end());   // mismo orden que el juego: gana el primero por orden alfabetico
  Revision r;
  std::error_code ec;

  // --- escenarios ---------------------------------------------------------------------------------------------
  struct ModEsc {
    std::string mod;
    bool generado;
    std::vector<Esc> esc;
  };
  std::vector<ModEsc> con_esc;
  std::map<int, std::string> ocupado;   // entrada de data_cmn >= 5000 -> mod que la tiene
  std::set<std::string> generados;
  for (const auto& mod : activos) {
    const fs::path dir = root / fs::u8path(mod);
    auto esc = Escenarios(dir);
    std::set<int> propios;
    for (const auto& e : esc) propios.insert({e.entrada, e.menu});
    for (int n : EntradasCmn(dir))   // archivos de data_cmn que no son escenarios: fijos (no se mueven)
      if (n >= 5000 && !propios.count(n)) ocupado.emplace(n, mod);
    if (esc.empty()) continue;
    const bool gen = fs::is_regular_file(dir / "origen.txt", ec);
    if (gen) generados.insert(mod);
    con_esc.push_back({mod, gen, std::move(esc)});
  }
  // 1) los generados desde las ISO conservan sus numeros fijos (dos generados que chocan: versiones viejas que
  //    campos_iw.py aparta al pulsar JUGAR; aqui no se tocan).
  for (const auto& m : con_esc) {
    if (!m.generado) continue;
    for (const auto& e : m.esc) {
      for (int n : {e.entrada, e.menu}) {
        const auto [it, nuevo] = ocupado.emplace(n, m.mod);
        if (!nuevo && it->second != m.mod && !generados.count(it->second))
          r.bloqueos.push_back({it->second, m.mod, n, false});
      }
    }
  }
  // 2) los demas, por orden: si su numero esta libre se queda; si no, uno libre y estable (registro).
  const fs::path freg = root / kRegistro;
  auto regs = LeerRegistro(freg);
  const auto reservado = [&](int n, const std::string& mod) {
    return std::any_of(regs.begin(), regs.end(),
                       [&](const Apunte& a) { return a.mod != mod && (a.despues == n || a.menu == n); });
  };
  bool registro_cambia = false;
  for (const auto& m : con_esc) {
    if (m.generado) continue;
    std::set<int> propios;
    for (const auto& e : m.esc) propios.insert({e.entrada, e.menu});
    std::map<int, int> nuevas_e, nuevas_m;
    for (const auto& e : m.esc) {
      const int off = e.menu - e.entrada;
      const auto libre = [&](int n) {
        const auto it = ocupado.find(n);
        return (it == ocupado.end() || it->second == m.mod) && !reservado(n, m.mod);
      };
      auto ap = std::find_if(regs.begin(), regs.end(),
                             [&](const Apunte& a) { return a.mod == m.mod && (a.antes == e.entrada || a.despues == e.entrada); });
      int objetivo = ap == regs.end() ? e.entrada : ap->despues;
      std::string con;
      if (!libre(objetivo) || !libre(objetivo + off)) {
        const int choque = libre(objetivo) ? objetivo + off : objetivo;
        const auto it = ocupado.find(choque);
        const auto rv = std::find_if(regs.begin(), regs.end(), [&](const Apunte& a) {
          return a.mod != m.mod && (a.despues == choque || a.menu == choque);
        });
        con = it != ocupado.end() && it->second != m.mod ? it->second : rv != regs.end() ? rv->mod : "";
        objetivo = -1;
        for (int n = kPrimeraLibre; n + off <= kUltima && objetivo < 0; ++n)
          if (!ocupado.count(n) && !ocupado.count(n + off) && !propios.count(n) && !propios.count(n + off) &&
              !reservado(n, m.mod) && !reservado(n + off, m.mod))
            objetivo = n;
        if (objetivo < 0) {
          r.bloqueos.push_back({m.mod, con, e.entrada, false});
          continue;
        }
        if (ap == regs.end()) {
          regs.push_back({m.mod, e.entrada, objetivo, objetivo + off});
        } else {
          ap->despues = objetivo;
          ap->menu = objetivo + off;
        }
        registro_cambia = true;
      } else if (ap == regs.end()) {
        // su numero queda apuntado: un mod que llegue despues (aunque vaya antes por orden alfabetico) no se lo quita
        regs.push_back({m.mod, e.entrada, e.entrada, e.menu});
        registro_cambia = true;
      }
      ocupado[objetivo] = ocupado[objetivo + off] = m.mod;
      if (objetivo != e.entrada) {
        nuevas_e[e.entrada] = objetivo;
        nuevas_m[e.menu] = objetivo + off;
        r.cambios.push_back({m.mod, con, e.entrada, objetivo});
      }
    }
    if (nuevas_e.empty()) continue;
    if (registro_cambia) GuardarRegistro(freg, regs);   // primero el registro: si algo se corta, manda el
    registro_cambia = false;
    for (const auto& [de, a] : nuevas_e) Mover(root, m.mod, de, a);
    for (const auto& [de, a] : nuevas_m) Mover(root, m.mod, de, a);
    ReescribirToml(root / fs::u8path(m.mod) / "stage.toml", nuevas_e, nuevas_m);
  }
  if (registro_cambia) GuardarRegistro(freg, regs);

  // --- capsulas -----------------------------------------------------------------------------------------------
  // roster_build.py las reparte sin choques; dos mods activos con la misma (un pack montado aparte) no se arreglan
  // solos: se dice cual desactivar (el que no es generado, o el segundo).
  std::map<int, std::string> caps;
  for (const auto& mod : activos) {
    std::error_code e2;
    if (!fs::is_regular_file(root / fs::u8path(mod) / "roster.toml", e2)) continue;
    for (const auto& b : Bloques(root / fs::u8path(mod) / "roster.toml", "capsula")) {
      const int id = Num(b, "id");
      if (id < 0) continue;
      const auto [it, nuevo] = caps.emplace(id, mod);
      if (nuevo || it->second == mod) continue;
      const bool otro_generado = !it->second.empty() && it->second.front() == '_';
      const bool este_generado = !mod.empty() && mod.front() == '_';
      if (este_generado && !otro_generado) r.bloqueos.push_back({it->second, mod, id, true});
      else r.bloqueos.push_back({mod, it->second, id, true});
    }
  }
  return r;
}

}  // namespace dbz3::launcher::ids
