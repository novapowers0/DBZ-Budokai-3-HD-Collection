#include "src/tactics/catalogo.h"

#include <map>
#include <set>

#include <toml++/toml.hpp>

namespace dbz3::tactics {
namespace {
struct Nombre { const char* texto; int valor; };
constexpr Nombre kFormas[] = {{"objetivo", 0}, {"linea", 1}, {"circulo", 2}};
constexpr Nombre kPasivas[] = {{"ninguna", 0}, {"ki_inicial", 1}, {"furia", 2}, {"perfora", 3}, {"resistentes", 4},
                               {"coraza", 5}, {"ki_regular", 6}, {"remate", 7}, {"regenera", 8}};

template <size_t N>
int Buscar(const Nombre (&t)[N], const std::string& s) {
  for (const Nombre& n : t) if (s == n.texto) return n.valor;
  return -1;
}

struct Lector {
  const std::string& id;
  std::string& err;
  int Num(const toml::table& t, const char* clave, int lo, int hi) {
    auto v = t[clave].value<int64_t>();
    if (!v) { err += id + ": falta '" + clave + "' (entero)\n"; return lo; }
    if (*v < lo || *v > hi) err += id + ": '" + clave + "' fuera de rango " + std::to_string(lo) + ".." + std::to_string(hi) + "\n";
    return static_cast<int>(*v);
  }
};
}  // namespace

const sim::DefUnidad* Catalogo::Buscar(const std::string& id) const {
  for (const auto& u : unidades) if (u.id == id) return &u;
  return nullptr;
}

bool CargarCatalogo(const std::string& ruta, const std::string& ruta_manifest, Catalogo* out, std::string* error) {
  std::string err;
  toml::table raiz;
  try {
    raiz = toml::parse_file(ruta);
  } catch (const toml::parse_error& e) {
    *error = ruta + ": " + std::string(e.description()) + "\n";
    return false;
  }
  if (raiz["version"].value_or<int64_t>(0) != 1) err += "version debe ser 1\n";

  std::set<std::string> en_manifest;
  std::map<int64_t, std::array<int, 3>> por_modelo;   // fid del modelo -> {b3, forma, fid}
  if (!ruta_manifest.empty()) {
    try {
      auto m = toml::parse_file(ruta_manifest);
      if (auto* a = m["unidad"].as_array())
        for (auto& n : *a)
          if (auto* t = n.as_table()) {
            en_manifest.insert(t->at("id").value_or<std::string>(""));
            por_modelo[(*t)["modelo"].value_or<int64_t>(-1)] = {int((*t)["b3"].value_or<int64_t>(-1)), int((*t)["forma"].value_or<int64_t>(0)), int((*t)["modelo"].value_or<int64_t>(-1))};
          }
    } catch (const toml::parse_error& e) {
      err += ruta_manifest + ": " + std::string(e.description()) + "\n";
    }
  }

  Catalogo cat;
  std::set<std::string> vistos;
  const toml::array* lista = raiz["unidad"].as_array();
  if (!lista || lista->empty()) err += "no hay [[unidad]]\n";
  for (size_t i = 0; lista && i < lista->size(); ++i) {
    const toml::table* t = lista->get(i)->as_table();
    if (!t) continue;
    sim::DefUnidad d;
    d.id = (*t)["id"].value_or<std::string>("");
    if (d.id.empty()) { err += "unidad #" + std::to_string(i) + " sin id\n"; continue; }
    if (!vistos.insert(d.id).second) err += d.id + ": id repetido\n";
    if (!ruta_manifest.empty() && !en_manifest.count(d.id)) err += d.id + ": no esta en el manifiesto de clips\n";
    Lector l{d.id, err};
    d.vida = l.Num(*t, "vida", 1, 5000);
    d.ataque = l.Num(*t, "ataque", 1, 500);
    d.defensa = l.Num(*t, "defensa", 0, 200);
    d.velocidad = l.Num(*t, "velocidad", 1, 60);
    d.alcance = l.Num(*t, "alcance", 1, 5);
    d.ki_max = l.Num(*t, "ki_max", 20, 1000);
    d.ataque_ticks = l.Num(*t, "ataque_ticks", 2, 600);
    d.tecnica_ticks = l.Num(*t, "tecnica_ticks", 2, 600);
    if (const toml::table* tc = (*t)["tecnica"].as_table()) {
      d.tecnica.dano = l.Num(*tc, "dano", 1, 2000);
      d.tecnica.alcance = l.Num(*tc, "alcance", 1, 6);
      d.tecnica.area = l.Num(*tc, "area", 0, 5);
      int f = Buscar(kFormas, (*tc)["forma"].value_or<std::string>(""));
      if (f < 0) err += d.id + ": tecnica.forma debe ser objetivo, linea o circulo\n";
      else d.tecnica.forma = static_cast<sim::Forma>(f);
    } else {
      err += d.id + ": falta [unidad.tecnica]\n";
    }
    if (const toml::table* tp = (*t)["pasiva"].as_table()) {
      int p = Buscar(kPasivas, (*tp)["tipo"].value_or<std::string>(""));
      if (p < 0) err += d.id + ": pasiva.tipo desconocido\n";
      else d.pasiva = static_cast<sim::Pasiva>(p);
      d.pasiva_valor = l.Num(*tp, "valor", 0, 200);
    }
    if (const toml::table* tv = (*t)["voz"].as_table()) {   // opcional: banco e ids de los gritos de ataque y de KO (3 de cada)
      d.voz_banco = l.Num(*tv, "banco", 0, 15);
      for (const char* k : {"ataque", "ko"}) {
        const toml::array* a = (*tv)[k].as_array();
        if (!a || a->size() != 3) { err += d.id + ": voz." + k + " debe tener 3 ids\n"; continue; }
        int* dst = k[0] == 'a' ? d.voz_ataque : d.voz_ko;
        for (size_t j = 0; j < 3; ++j) {
          const int64_t v = a->get(j)->value_or<int64_t>(-1);
          if (v < 0 || v > 63) err += d.id + ": voz." + k + " fuera de 0..63\n";
          dst[j] = static_cast<int>(v);
        }
      }
    }
    std::vector<std::array<int, 3>> fs;   // H9: formas = [fid, ...]: el modelo de cada estrella
    if (!ruta_manifest.empty()) {
      const toml::array* a = (*t)["formas"].as_array();
      if (!a || a->empty() || a->size() > 4) err += d.id + ": 'formas' debe tener de 1 a 4 ids de modelo\n";
      for (size_t j = 0; a && j < a->size(); ++j) {
        const auto it = por_modelo.find(a->get(j)->value_or<int64_t>(-1));
        if (it == por_modelo.end()) { err += d.id + ": formas[" + std::to_string(j) + "] no es un modelo del manifiesto\n"; continue; }
        if (!fs.empty() && fs.back() == it->second) err += d.id + ": formas repetidas\n";
        fs.push_back(it->second);
      }
    }
    cat.formas.push_back(std::move(fs));
    cat.unidades.push_back(std::move(d));
  }
  *error = err;
  if (!err.empty()) return false;
  *out = std::move(cat);
  return true;
}

}  // namespace dbz3::tactics
