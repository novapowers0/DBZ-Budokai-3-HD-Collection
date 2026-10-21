// dbz3 - Creador de Personaje (CaC): implementacion del modulo (frente 02, studies 6).
// Guardado con el patron de rpg.toml: escribir en .tmp, renombrar sobre el bueno y dejar
// el anterior en .bak. Autoreparable si el .toml no parsea: se aparta a .roto.
#include "cac.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dbz3::cac {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Catalogo de piezas (por defecto, el de B3 nativo; se amplia con piezas UT)
// ---------------------------------------------------------------------------
namespace {
std::vector<Pieza> g_catalogo = {
    {"cuerpo", 0, "Goku", -1, ""},   {"cuerpo", 1, "Vegeta", -1, ""},
    {"cuerpo", 2, "Gohan", -1, ""},  {"cuerpo", 3, "Krilin", -1, ""},
    {"pelo", 0, "Nativo 1", -1, ""}, {"pelo", 1, "Nativo 2", -1, ""},
    {"cara", 0, "Nativa 1", -1, ""}, {"cara", 1, "Nativa 2", -1, ""},
    {"ropa", 0, "Gi", -1, ""},       {"ropa", 1, "Armadura Saiyan", -1, ""},
    {"color", 0, "Piel clara", -1, ""}, {"color", 1, "Piel media", -1, ""},
    {"color", 2, "Piel oscura", -1, ""},
    {"voz", 0, "Grave", -1, ""},     {"voz", 1, "Aguda", -1, ""},
    {"voz", 2, "Joven", -1, ""},
    {"aura", 0, "Blanca", -1, ""},   {"aura", 1, "Dorada", -1, ""},
    {"aura", 2, "Azul", -1, ""},     {"aura", 3, "Roja", -1, ""},
};
std::vector<Heroe> g_slots = {Heroe{}};
std::vector<Heroe> g_archivados;
int g_activo = 0;
Camara g_cam;
}  // namespace

const std::vector<Pieza>& Catalogo() { return g_catalogo; }

const std::vector<Pieza>& PiezasDe(const std::string& tipo) {
  static std::vector<Pieza> v;
  v.clear();
  for (const Pieza& p : g_catalogo) {
    if (p.tipo == tipo) v.push_back(p);
  }
  return v;
}

void CargarCatalogo(const std::string& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  toml::table t;
  try {
    t = toml::parse_file(path);
  } catch (...) {   // noqa: (toml invalido -> se sigue con el catalogo por defecto)
    return;
  }
  if (auto* arr = t["pieza"].as_array()) {
    std::vector<Pieza> nuevo;
    for (const auto& e : *arr) {
      const toml::table* p = e.as_table();
      if (!p) continue;
      Pieza z;
      z.tipo = (*p)["tipo"].value_or(std::string{});
      z.id = int((*p)["id"].value_or(int64_t{0}));
      z.nombre = (*p)["nombre"].value_or(std::string{});
      z.idx_afs = int((*p)["idx_afs"].value_or(int64_t{-1}));
      z.archivo = (*p)["archivo"].value_or(std::string{});
      if (!z.tipo.empty()) nuevo.push_back(z);
    }
    if (!nuevo.empty()) g_catalogo = std::move(nuevo);
  }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
int NumSlots() { return int(g_slots.size()); }
const Heroe& Slot(int i) { return g_slots[std::clamp(i, 0, NumSlots() - 1)]; }
Heroe& SlotEditable(int i) { return g_slots[std::clamp(i, 0, NumSlots() - 1)]; }
int SlotActivo() { return g_activo; }
void ElegirSlot(int i) { g_activo = std::clamp(i, 0, NumSlots() - 1); }

int NuevoSlot() {
  Heroe h;
  h.nombre = "Heroe " + std::to_string(NumSlots() + 1);
  h.progreso = kProgresoFicha;   // ficha nueva: su progreso empieza de cero y ya es suyo
  g_slots.push_back(h);
  g_activo = NumSlots() - 1;
  return g_activo;
}

void DuplicarSlot(int i) {
  Heroe h = Slot(i);
  h.nombre += " x2";
  g_slots.push_back(h);
  g_activo = NumSlots() - 1;
}

void BorrarSlot(int i) {
  if (NumSlots() <= 1) return;              // siempre queda uno
  g_slots.erase(g_slots.begin() + std::clamp(i, 0, NumSlots() - 1));
  g_activo = std::clamp(g_activo, 0, NumSlots() - 1);
}

void ArchivarSlot(int i) {
  if (NumSlots() <= 1) return;              // siempre queda una ficha activa
  i = std::clamp(i, 0, NumSlots() - 1);
  g_archivados.push_back(Slot(i));
  g_slots.erase(g_slots.begin() + i);
  g_activo = std::clamp(g_activo, 0, NumSlots() - 1);
}
const std::vector<Heroe>& Archivados() { return g_archivados; }
int NumArchivados() { return int(g_archivados.size()); }

void ClampHsv(Hsv& c) {
  c.h = std::clamp(c.h, 0, 360);
  c.s = std::clamp(c.s, -100, 100);
  c.v = std::clamp(c.v, -100, 100);
}

void AjustarSlot(int i) {
  Heroe& h = SlotEditable(i);
  // Rangos del Modo Heroe (heroe.cpp valida otra vez al importar). Sin catalogo: los IDs son del juego.
  auto n_de = [](const std::string& tipo) { return int(PiezasDe(tipo).size()); };
  h.raza = std::clamp(h.raza, 0, 6);   // G06a: raza 6 = catálogo UT, cuerpos 100..105
  h.cuerpo = std::max(0, h.cuerpo);
  // studies 8 fase 2: 10 peinados (Edit, heroe_ui.cpp kNumPelos: el del cuerpo + 9 piezas UT) aunque el catalogo
  // piezas_cac.toml no este cargado (sin el, solo hay 2 "nativos" y el peinado elegido se perdia al guardar)
  h.pelo = std::clamp(h.pelo, 0, std::max(9, n_de("pelo") - 1));
  h.cara = std::clamp(h.cara, 0, std::max(0, n_de("cara") - 1));
  h.ropa = std::clamp(h.ropa, 0, std::max(0, n_de("ropa") - 1));
  h.color = std::clamp(h.color, 0, 31);
  h.voz = std::clamp(h.voz, 0, 31);
  h.aura = std::clamp(h.aura, 0, 7);
  for (Hsv& c : h.parte) ClampHsv(c);
  ClampHsv(h.pelo_c1);
  ClampHsv(h.pelo_c2);
  h.logo = std::max(0, h.logo);
  h.scouter = std::clamp(h.scouter, 0, 1);
  h.estilo = std::max(0, h.estilo);
  h.moveset = std::clamp(h.moveset, -1, 63);
  h.traje = std::max(0, h.traje);
  h.nivel = std::clamp(h.nivel, 1, 20);
  h.xp = std::max(0, h.xp);
  h.saga = std::max(0, h.saga);
  h.ph = std::max(0, h.ph);
  h.ph_vida = std::max(0, h.ph_vida);
  h.ph_dano = std::max(0, h.ph_dano);
  h.maestros &= (1 << kNumEntreno) - 1;
  for (int& e : h.entreno) e = std::clamp(e, 0, 100);
}

// ---------------------------------------------------------------------------
// Guardado (patron rpg.toml)
// ---------------------------------------------------------------------------
std::string RutaPorDefecto() {
  return (fs::current_path() / "user_data" / "dbz3" / "cac.toml").string();
}

namespace {
std::string HsvTxt(const Hsv& c);
void EscribirHeroe(std::ostream& o, const char* tabla, const Heroe& h) {
  o << "[[" << tabla << "]]\n";
  o << "nombre = \"" << h.nombre << "\"\n";
  o << "raza = " << h.raza << "\ncuerpo = " << h.cuerpo << "\ntraje = " << h.traje
    << "\ncara = " << h.cara << "\npelo = " << h.pelo << "\nropa = " << h.ropa
    << "\ncolor = " << h.color << "\nvoz = " << h.voz << "\naura = " << h.aura
    << "\nestilo = " << h.estilo << "\nmoveset = " << h.moveset
    << "\ntecnicas = [" << h.tecnicas[0] << ", " << h.tecnicas[1] << ", " << h.tecnicas[2]
    << ", " << h.tecnicas[3] << "]\n"
    << "\nxp = " << h.xp << "\nnivel = " << h.nivel << "\n"
    << "logo = " << h.logo << "\nscouter = " << h.scouter << "\n"
    << "partes = [" << HsvTxt(h.parte[0]) << ", " << HsvTxt(h.parte[1]) << ", " << HsvTxt(h.parte[2])
    << ", " << HsvTxt(h.parte[3]) << "]\n"
    << "pelo_color1 = " << HsvTxt(h.pelo_c1) << "\npelo_color2 = " << HsvTxt(h.pelo_c2) << "\n";
  // studies 8 H1: progreso del Modo Heroe de esta ficha
  o << "progreso = " << h.progreso << "\nsaga = " << h.saga << "\nph = " << h.ph << "\nph_vida = " << h.ph_vida
    << "\nph_dano = " << h.ph_dano << "\nmaestros = " << h.maestros << "\nentreno = [";
  for (int i = 0; i < kNumEntreno; ++i) o << (i ? ", " : "") << h.entreno[i];
  o << "]\n";
}
std::string HsvTxt(const Hsv& c) {
  return "[" + std::to_string(c.h) + ", " + std::to_string(c.s) + ", " + std::to_string(c.v) + "]";
}
Hsv LeerHsv(const toml::node* n) {
  Hsv c;
  const toml::array* a = n ? n->as_array() : nullptr;
  if (!a || a->size() < 3) return c;
  c.h = int((*a)[0].value_or(int64_t{0}));
  c.s = int((*a)[1].value_or(int64_t{0}));
  c.v = int((*a)[2].value_or(int64_t{0}));
  return c;
}
std::string Escapa(const std::string& s) {
  std::string r;
  for (char c : s) {
    if (c == '"' || c == '\\') r += '\\';
    r += c;
  }
  return r;
}
}  // namespace

Heroe LeerHeroe(const toml::table* p) {
  Heroe h;
  h.nombre = (*p)["nombre"].value_or(std::string{"Heroe"});
  h.raza = int((*p)["raza"].value_or(int64_t{0}));
  h.cuerpo = int((*p)["cuerpo"].value_or(int64_t{0}));
  h.traje = int((*p)["traje"].value_or(int64_t{0}));
  h.cara = int((*p)["cara"].value_or(int64_t{0}));
  h.pelo = int((*p)["pelo"].value_or(int64_t{0}));
  h.ropa = int((*p)["ropa"].value_or(int64_t{0}));
  h.color = int((*p)["color"].value_or(int64_t{0}));
  h.voz = int((*p)["voz"].value_or(int64_t{0}));
  h.aura = int((*p)["aura"].value_or(int64_t{0}));
  h.estilo = int((*p)["estilo"].value_or(int64_t{0}));
  h.moveset = int((*p)["moveset"].value_or(int64_t{-1}));
  if (auto* tec = (*p)["tecnicas"].as_array()) {
    int k = 0;
    for (const auto& v : *tec) {
      if (k >= kNumTecnicas) break;
      h.tecnicas[k++] = int(v.value_or(int64_t{0}));
    }
  }
  h.xp = int((*p)["xp"].value_or(int64_t{0}));
  h.nivel = int((*p)["nivel"].value_or(int64_t{1}));
  h.logo = int((*p)["logo"].value_or(int64_t{0}));
  h.scouter = int((*p)["scouter"].value_or(int64_t{0}));
  if (auto* ps = (*p)["partes"].as_array()) {
    for (size_t k = 0; k < ps->size() && k < kNumPartes; ++k) h.parte[k] = LeerHsv(&(*ps)[k]);
  }
  h.pelo_c1 = LeerHsv((*p)["pelo_color1"].node());
  h.pelo_c2 = LeerHsv((*p)["pelo_color2"].node());
  h.progreso = int((*p)["progreso"].value_or(int64_t{0}));
  h.saga = int((*p)["saga"].value_or(int64_t{0}));
  h.ph = int((*p)["ph"].value_or(int64_t{0}));
  h.ph_vida = int((*p)["ph_vida"].value_or(int64_t{0}));
  h.ph_dano = int((*p)["ph_dano"].value_or(int64_t{0}));
  h.maestros = int((*p)["maestros"].value_or(int64_t{0}));
  if (auto* en = (*p)["entreno"].as_array()) {
    for (size_t k = 0; k < en->size() && k < size_t(kNumEntreno); ++k) h.entreno[k] = int((*en)[k].value_or(int64_t{0}));
  }
  return h;
}

bool Leer(const std::string& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) return false;
  toml::table t;
  try {
    t = toml::parse_file(path);
  } catch (...) {
    fs::rename(path, path + ".roto", ec);            // se aparta, no se pierde
    if (fs::exists(path + ".bak", ec)) return Leer(path + ".bak");
    return false;
  }
  const int64_t ver = t["version"].value_or(int64_t{1});
  if (ver > kVersion) return false;                   // version mas nueva: no se toca
  g_slots.clear();
  g_archivados.clear();
  if (auto* arr = t["heroe"].as_array()) {
    for (const auto& e : *arr) {
      const toml::table* p = e.as_table();
      if (p) g_slots.push_back(LeerHeroe(p));
    }
  }
  if (auto* arr = t["archivado"].as_array()) {        // v2: fichas archivadas
    for (const auto& e : *arr) {
      const toml::table* p = e.as_table();
      if (p) g_archivados.push_back(LeerHeroe(p));
    }
  }
  if (g_slots.empty()) g_slots.push_back(Heroe{});
  g_activo = std::clamp(int(t["activo"].value_or(int64_t{0})), 0, NumSlots() - 1);
  for (int i = 0; i < NumSlots(); ++i) AjustarSlot(i);
  return true;
}

bool Guardar(const std::string& path) {
  const std::string tmp = path + ".tmp", bak = path + ".bak";
  std::error_code ec;
  if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream o(tmp, std::ios::binary);
  if (!o) return false;
  o << "version = 2\n";
  o << "activo = " << g_activo << "\n\n";
  for (const Heroe& h : g_slots) {
    Heroe e = h;
    e.nombre = Escapa(h.nombre);
    EscribirHeroe(o, "heroe", e);
    o << "\n";
  }
  for (const Heroe& h : g_archivados) {
    Heroe e = h;
    e.nombre = Escapa(h.nombre);
    EscribirHeroe(o, "archivado", e);
    o << "\n";
  }
  o.close();
  if (!o) return false;
  if (fs::exists(path, ec)) fs::rename(path, bak, ec);
  fs::rename(tmp, path, ec);
  return !ec;
}

// ---------------------------------------------------------------------------
// Camara propia
// ---------------------------------------------------------------------------
Camara& Cam() { return g_cam; }

namespace {
void limites() {
  g_cam.yaw = std::clamp(g_cam.yaw, -80.0f, 80.0f);
  g_cam.pitch = std::clamp(g_cam.pitch, -25.0f, 45.0f);
  g_cam.dist = std::clamp(g_cam.dist, 0.8f, 6.0f);
}
}  // namespace

void CamReset(int preset) {
  switch (preset) {
    case 0: g_cam = {0.0f, 6.0f, 2.2f, false, 0.0f}; break;
    case 2: g_cam = {6.0f, 6.0f, 1.1f, true, 8.0f}; break;
    case 3: g_cam = {20.0f, 4.0f, 4.2f, true, 12.0f}; break;
    case 4: g_cam = {0.0f, 8.0f, 2.6f, true, 35.0f}; break;
    default: g_cam = {28.0f, 10.0f, 2.4f, true, 18.0f}; break;   // tres cuartos
  }
}

void CamMover(float dyaw, float dpitch) {
  g_cam.giro_auto = false;
  g_cam.yaw += dyaw;
  g_cam.pitch += dpitch;
  limites();
}

void CamZoom(float factor) {
  g_cam.dist *= factor;
  limites();
}

void CamTick(float dt) {
  if (!g_cam.giro_auto || g_cam.spin == 0.0f) return;
  g_cam.yaw += g_cam.spin * dt;
  if (g_cam.yaw >= 80.0f || g_cam.yaw <= -80.0f) g_cam.spin = -g_cam.spin;
  limites();
}

// ---------------------------------------------------------------------------
Preset PresetSlot(int i) {
  const Heroe& h = Slot(i);
  return {h.estilo, h.cuerpo, h.traje, h.moveset};
}

}  // namespace dbz3::cac
