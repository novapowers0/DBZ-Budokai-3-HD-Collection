// dbz3 - Creador de Personaje (CaC): implementacion del modulo (frente 02, studies 6).
// Guardado con el patron de rpg.toml: escribir en .tmp, renombrar sobre el bueno y dejar
// el anterior en .bak. Autoreparable si el .toml no parsea: se aparta a .roto.
#include "cac.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <charconv>
#include <string_view>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
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
bool g_migrado = false;
std::string g_ruta;   // el ultimo cac.toml leido o guardado (para RespaldarCopia)
std::recursive_mutex g_archivo_mu;   // Leer puede recuperar recursivamente el .bak.
int g_por_limite = 0;
Camara g_cam;
std::string NuevoId() {
  static unsigned n = 0;
  const auto t = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
  for (;;) {   // 24 bits: se repite hasta que ningun heroe (activo o archivado) lo lleve ya
    char b[16];
    std::snprintf(b, sizeof b, "h%06llx", (t * 2654435761ull + (++n) * 40503ull) & 0xFFFFFFull);
    bool dup = false;
    for (const Heroe& h : g_slots) dup = dup || h.id == b;
    for (const Heroe& h : g_archivados) dup = dup || h.id == b;
    if (!dup) return b;
  }
}
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

bool SlotsLlenos() { return NumSlots() >= kMaxSlots; }
bool MigradoDeV1() { return g_migrado; }
int ArchivadosPorLimite() { return g_por_limite; }

int NuevoSlot() {
  if (SlotsLlenos()) return -1;
  Heroe h;
  static const int kCuerposNuevos[] = {7, 39, 8, 0};   // saiyan nativos distintos: cada heroe nuevo se distingue a la vista
  h.cuerpo = kCuerposNuevos[g_slots.size() % 4];
  h.id = NuevoId();
  h.nombre = "Heroe " + std::to_string(NumSlots() + 1);
  h.progreso = kProgresoFicha;   // ficha nueva: su progreso empieza de cero y ya es suyo
  g_slots.push_back(h);
  g_activo = NumSlots() - 1;
  return g_activo;
}

void DuplicarSlot(int i) {
  if (SlotsLlenos()) return;
  Heroe h = Slot(i);
  h.id = NuevoId();
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
void RestaurarArchivado(int i) {
  if (SlotsLlenos() || i < 0 || i >= int(g_archivados.size())) return;
  g_slots.push_back(g_archivados[i]);
  g_archivados.erase(g_archivados.begin() + i);
  g_activo = NumSlots() - 1;
  AjustarSlot(g_activo);
}
const std::vector<Heroe>& Archivados() { return g_archivados; }
int NumArchivados() { return int(g_archivados.size()); }

// Cinco enteros por entrada: el editor admite saturación y valor negativos.
static bool TexcolValido(std::string_view s) {
  if (s.size() > 2048) return false;
  constexpr int minimos[] = {0, 0, 0, -100, -100};
  constexpr int maximos[] = {2147483647, 5, 360, 100, 100};
  while (!s.empty()) {
    for (int i = 0; i < 5; ++i) {
      int valor = 0;
      const auto [fin, error] = std::from_chars(s.data(), s.data() + s.size(), valor);
      if (error != std::errc{} || valor < minimos[i] || valor > maximos[i]) return false;
      s.remove_prefix(size_t(fin - s.data()));
      if (i == 4 && s.empty()) return true;
      if (s.empty() || s.front() != (i == 4 ? '|' : ',')) return false;
      s.remove_prefix(1);
      if (s.empty()) return false;
    }
  }
  return true;
}

void RestaurarColoresCac2(Heroe& h) {
  for (auto& c : h.parte) c = {};
  h.pelo_c1 = h.pelo_c2 = h.ojos_c = h.piel_c = {};
  h.texcol.clear();
  h.pintura17.clear();  // se quita la referencia, no se toca el archivo de pintura ni los acabados
}
void ClampHsv(Hsv& c) {
  c.h = std::clamp(c.h, 0, 360);
  c.s = std::clamp(c.s, -100, 100);
  c.v = std::clamp(c.v, -100, 100);
}

static void AjustarHeroe(Heroe& h) {
  if (!TexcolValido(h.texcol)) {
    // texcol invalido: se conserva el color original (sin log: este fichero se compila tambien en las pruebas sin rex)
    h.texcol.clear();
  }
  // Rangos del Modo Heroe (heroe.cpp valida otra vez al importar). Sin catalogo: los IDs son del juego.
  auto n_de = [](const std::string& tipo) { return int(PiezasDe(tipo).size()); };
  h.raza = std::clamp(h.raza, 0, 12);   // G06a: raza 6 = catálogo UT (100..105); C4: 7..12 = piezas XV2 (110..115)
  h.cuerpo = std::max(0, h.cuerpo);
  // studies 8 fase 2: 10 peinados (Edit, heroe_ui.cpp kNumPelos: el del cuerpo + 9 piezas UT) aunque el catalogo
  // piezas_cac.toml no este cargado (sin el, solo hay 2 "nativos" y el peinado elegido se perdia al guardar)
  h.pelo = std::clamp(h.pelo, 0, std::max(10, n_de("pelo") - 1));   // 25/10: 11 peinados (vista_heroe PELOS_NATIVOS)
  h.cara = std::clamp(h.cara, 0, std::max(13, n_de("cara") - 1));   // 25/10: tipos de rostro (0 = el del cuerpo, 1..13)
  h.ropa = std::clamp(h.ropa, 0, std::max(0, n_de("ropa") - 1));
  h.color = std::clamp(h.color, 0, 31);
  h.voz = std::clamp(h.voz, 0, 31);
  h.aura = std::clamp(h.aura, 0, 7);
  for (Hsv& c : h.parte) ClampHsv(c);
  h.foto_pose = std::clamp(h.foto_pose, 0, 2);
  h.foto_yaw = std::clamp(h.foto_yaw, -40, 40);
  ClampHsv(h.pelo_c1);
  if (!FidPeloCac2(h.pelo_fid_cac2) || h.pelo > 0) h.pelo_fid_cac2 = -1;
  ClampHsv(h.pelo_c2);
  ClampHsv(h.ojos_c);
  ClampHsv(h.piel_c);
  h.logo = std::max(0, h.logo);
  h.scouter = std::clamp(h.scouter, 0, 1);
  for (int& fid : h.b3_piezas) fid = std::clamp(fid, -1, 4095);
  h.b3_accesorio = std::clamp(h.b3_accesorio, -1, 4095);
  for (int& fid : h.orig) fid = std::clamp(fid, -1, 3999);
  for (Capa& c : h.capas) {
    // Una parte inválida no se puede convertir en otra pieza (por ejemplo, en pelo).
    if (c.fid < 0 || c.fid > 4095 || c.parte < 0 || c.parte >= 4) c = Capa{};
    else c.quitar = c.quitar < 0 ? 0 : c.quitar & ((int64_t{1} << 47) - 1);
  }
  for (RecetaGuardada& r : h.recetas) { if (r.ficha.empty()) r.nombre.clear(); else if (r.nombre.empty()) r.nombre = "Traje"; if (r.ficha.size() > 4096) r = RecetaGuardada{}; }
  for (int i = 0; i < kMaxFormasReceta; ++i) {
    int& f = h.forma_receta[i];
    if (i == 0 || f < -1 || f >= kMaxRecetas || h.recetas[std::max(f, 0)].ficha.empty()) f = -1;
    auto& color = h.forma_estallido[i];
    if (f < 0 || color.size() != 7 || color[0] != '#' ||
        color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos) color.clear();
  }
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
  // inventario: IDs de capsula validos (1..2047), sin repetidos; el equipo solo con capsulas del inventario
  std::vector<int> v;
  for (int c : h.inv) {
    if (c >= 1 && c < 2048 && std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
  }
  h.inv = v;
  for (int& q : h.equipo) {
    if (q != 0 && std::find(h.inv.begin(), h.inv.end(), q) == h.inv.end()) q = 0;
  }
  for (int a = 0; a < 4; ++a) {
    for (int b = a + 1; b < 4; ++b) {
      if (h.equipo[a] != 0 && h.equipo[a] == h.equipo[b]) h.equipo[b] = 0;
    }
  }
  h.esferas = std::clamp(h.esferas, 0, 127);
  h.deseos = std::clamp(h.deseos, 0, 9);
  if (h.id.empty()) h.id = NuevoId();
}

void AjustarSlot(int i) { AjustarHeroe(SlotEditable(i)); }

// ---------------------------------------------------------------------------
// Guardado (patron rpg.toml)
// ---------------------------------------------------------------------------
std::string RutaPorDefecto() {
  return (fs::current_path() / "user_data" / "dbz3" / "cac.toml").string();
}

namespace {
std::string HsvTxt(const Hsv& c);
std::string Escapa(const std::string& s);
void EscribirHeroe(std::ostream& o, const char* tabla, const Heroe& h) {
  o << "[[" << tabla << "]]\n";
  o << "id = \"" << Escapa(h.id) << "\"\n";
  o << "esferas = " << h.esferas << "\ndeseos = " << h.deseos << "\n";
  o << "desbloqueos = [";
  for (size_t i = 0; i < h.desbloqueos.size(); ++i) o << (i ? ", " : "") << '"' << Escapa(h.desbloqueos[i]) << '"';
  o << "]\n";
  o << "nombre = \"" << Escapa(h.nombre) << "\"\n";
  o << "inventario = [";
  for (size_t i = 0; i < h.inv.size(); ++i) o << (i ? ", " : "") << h.inv[i];
  o << "]\nequipo = [" << h.equipo[0] << ", " << h.equipo[1] << ", " << h.equipo[2] << ", " << h.equipo[3] << "]\n";
  if (FidPeloCac2(h.pelo_fid_cac2)) o << "pelo_fid_cac2 = " << h.pelo_fid_cac2 << '\n';
  if (h.moveset_logico >= 64 && h.moveset_logico <= INT32_MAX) o << "moveset_logico = " << h.moveset_logico << '\n';
  o << "raza = " << h.raza << "\ncuerpo = " << h.cuerpo << "\ntraje = " << h.traje
    << (h.foto_pose ? "\nfoto_pose = " + std::to_string(h.foto_pose) : std::string())
    << (h.foto_pose || h.foto_yaw != 22 ? "\nfoto_yaw = " + std::to_string(h.foto_yaw) : std::string())
    << "\ncara = " << h.cara << "\npelo = " << h.pelo << "\nropa = " << h.ropa
    << "\ncolor = " << h.color << "\nvoz = " << h.voz << "\naura = " << h.aura
    << "\nmaterial17 = \"" << Escapa(h.material17) << "\"\npintura17 = \"" << Escapa(h.pintura17) << "\""
    << "\ntexcol = \"" << Escapa(h.texcol) << "\"\naura_col = \"" << Escapa(h.aura_col) << "\"\nki_col = \"" << Escapa(h.ki_col) << "\"\nestilo = " << h.estilo << "\nmoveset = " << h.moveset
    << "\ntecnicas = [" << h.tecnicas[0] << ", " << h.tecnicas[1] << ", " << h.tecnicas[2]
    << ", " << h.tecnicas[3] << "]\n"
    << "\nxp = " << h.xp << "\nnivel = " << h.nivel << "\n"
    << "logo = " << h.logo << "\nscouter = " << h.scouter << "\n"
     << "partes = [" << HsvTxt(h.parte[0]) << ", " << HsvTxt(h.parte[1]) << ", " << HsvTxt(h.parte[2])
     << ", " << HsvTxt(h.parte[3]) << ", " << HsvTxt(h.parte[4]) << ", " << HsvTxt(h.parte[5]) << "]\n"
     << "xv2 = [" << h.xv2[0] << ", " << h.xv2[1] << ", " << h.xv2[2] << ", " << h.xv2[3] << ", " << h.xv2[4] << ", " << h.xv2[5] << ", " << h.xv2[6] << "]\n"
     << "b3_piezas = [" << h.b3_piezas[0] << ", " << h.b3_piezas[1] << ", " << h.b3_piezas[2] << ", " << h.b3_piezas[3] << "]\n"
     << "b3_accesorio = " << h.b3_accesorio << "\n"
     << "orig = [" << h.orig[0] << ", " << h.orig[1] << ", " << h.orig[2] << ", " << h.orig[3] << "]\n"
     << "ocultar = " << h.ocultar << "\nocultar_de = " << h.ocultar_de << "\n"
     << "capas = [";
  for (int i = 0; i < kNumCapas; ++i) o << (i ? ", " : "") << "[" << h.capas[i].fid << ", " << h.capas[i].parte << ", " << h.capas[i].quitar << "]";
  o << "]\nreceta_nombres = [";
  for (int i = 0; i < kMaxRecetas; ++i) o << (i ? ", " : "") << '"' << Escapa(h.recetas[i].nombre) << '"';
  o << "]\nreceta_fichas = [";
  for (int i = 0; i < kMaxRecetas; ++i) o << (i ? ", " : "") << '"' << Escapa(h.recetas[i].ficha) << '"';
  o << "]\nforma_receta = [";
  for (int i = 0; i < kMaxFormasReceta; ++i) o << (i ? ", " : "") << h.forma_receta[i];
  o << "]\nforma_estallido = [";
  for (int i = 0; i < kMaxFormasReceta; ++i) o << (i ? ", " : "") << '"' << Escapa(h.forma_estallido[i]) << '"';
  o << "]\n"
     << "pelo_color1 = " << HsvTxt(h.pelo_c1) << "\npelo_color2 = " << HsvTxt(h.pelo_c2) << "\n"
     << "ojos_color = " << HsvTxt(h.ojos_c) << "\n"
     << "piel_color = " << HsvTxt(h.piel_c) << "\n";
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
  for (unsigned char c : s) {
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          constexpr char hex[] = "0123456789abcdef";
          r += "\\u00";
          r += hex[c >> 4];
          r += hex[c & 15];
        } else {
          r += char(c);
        }
    }
  }
  return r;
}
}  // namespace

Heroe LeerHeroe(const toml::table* p) {
  Heroe h;
  h.esferas = int((*p)["esferas"].value_or(int64_t{0}));
  h.deseos = int((*p)["deseos"].value_or(int64_t{0}));
  if (auto* dv = (*p)["desbloqueos"].as_array()) {
    for (const auto& e : *dv) {
      if (auto s = e.value<std::string>(); s && !s->empty() && s->size() < 48 && h.desbloqueos.size() < 128) h.desbloqueos.push_back(*s);
    }
  }
  h.id = (*p)["id"].value_or(std::string{});
  if (auto* iv = (*p)["inventario"].as_array()) {
    for (const auto& v : *iv) h.inv.push_back(int(v.value_or(int64_t{0})));
  }
  if (auto* eq = (*p)["equipo"].as_array()) {
    for (size_t k = 0; k < eq->size() && k < 4; ++k) h.equipo[k] = int((*eq)[k].value_or(int64_t{0}));
  }
  h.nombre = (*p)["nombre"].value_or(std::string{"Heroe"});
  h.raza = int((*p)["raza"].value_or(int64_t{0}));
  h.cuerpo = int((*p)["cuerpo"].value_or(int64_t{0}));
  h.traje = int((*p)["traje"].value_or(int64_t{0}));
  h.cara = int((*p)["cara"].value_or(int64_t{0}));
  h.foto_pose = int(std::clamp((*p)["foto_pose"].value_or(int64_t{0}), int64_t{0}, int64_t{2}));
  h.foto_yaw = int(std::clamp((*p)["foto_yaw"].value_or(int64_t{22}), int64_t{-40}, int64_t{40}));
  h.pelo = int((*p)["pelo"].value_or(int64_t{0}));
  h.ropa = int((*p)["ropa"].value_or(int64_t{0}));
  h.color = int((*p)["color"].value_or(int64_t{0}));
  h.voz = int((*p)["voz"].value_or(int64_t{0}));
  h.aura = int((*p)["aura"].value_or(int64_t{0}));
  h.material17 = (*p)["material17"].value_or(std::string());
  h.pintura17 = (*p)["pintura17"].value_or(std::string());
  if (const auto* nodo = (*p)["texcol"].as_string()) {
    const std::string& texto = nodo->get();
    if (TexcolValido(texto)) h.texcol = texto;
  }
  h.aura_col = (*p)["aura_col"].value_or(std::string());
  h.ki_col = (*p)["ki_col"].value_or(std::string());
  h.estilo = int((*p)["estilo"].value_or(int64_t{0}));
  h.moveset = int((*p)["moveset"].value_or(int64_t{-1}));
  const auto logical = (*p)["moveset_logico"].value_or(int64_t{-1});
  h.moveset_logico = logical >= 64 && logical <= INT32_MAX ? logical : -1;
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
  h.ocultar = (*p)["ocultar"].value_or(int64_t{0});
  h.b3_accesorio = int((*p)["b3_accesorio"].value_or(int64_t{-1}));
  h.ocultar_de = int((*p)["ocultar_de"].value_or(int64_t{-1}));
  h.scouter = int((*p)["scouter"].value_or(int64_t{0}));
  if (auto* ps = (*p)["partes"].as_array()) {
    for (size_t k = 0; k < ps->size() && k < kNumPartes; ++k) h.parte[k] = LeerHsv(&(*ps)[k]);
  }
  if (auto* xs = (*p)["xv2"].as_array()) {
    for (size_t k = 0; k < xs->size() && k < 7; ++k) h.xv2[k] = int((*xs)[k].value_or(int64_t{0}));
  }
  if (auto* os = (*p)["orig"].as_array())
    for (size_t k = 0; k < os->size() && k < 4; ++k) h.orig[k] = int((*os)[k].value_or(int64_t{-1}));
  if (auto* bs = (*p)["b3_piezas"].as_array()) {
    for (size_t k = 0; k < bs->size() && k < 4; ++k) h.b3_piezas[k] = int((*bs)[k].value_or(int64_t{-1}));
  }
  if (auto* cs = (*p)["capas"].as_array()) {   // v3
    for (size_t k = 0; k < cs->size() && k < size_t(kNumCapas); ++k)
      if (auto* par = (*cs)[k].as_array(); par && par->size() >= 2) {
        const auto fid = (*par)[0].value_or(int64_t{-1});
        const auto parte = (*par)[1].value_or(int64_t{-1});
        if (fid < 0 || fid > 4095 || parte < 0 || parte >= 4) continue;
        h.capas[k].fid = int(fid);
        h.capas[k].parte = int(parte);
        h.capas[k].quitar = par->size() > 2 ? (*par)[2].value_or(int64_t{0}) : 0;
      }
  }
  auto texto4 = [&](const char* clave, auto&& poner, size_t tope) {
    if (auto* ar = (*p)[clave].as_array())
      for (size_t k = 0; k < ar->size() && k < tope; ++k) poner(k, (*ar)[k].value_or(std::string{}));
  };
  texto4("receta_nombres", [&](size_t k, std::string v) { h.recetas[k].nombre = std::move(v); }, size_t(kMaxRecetas));
  texto4("receta_fichas", [&](size_t k, std::string v) { h.recetas[k].ficha = std::move(v); }, size_t(kMaxRecetas));
  texto4("forma_estallido", [&](size_t k, std::string v) { h.forma_estallido[k] = std::move(v); }, size_t(kMaxFormasReceta));
  const int64_t pelo_fid = (*p)["pelo_fid_cac2"].value_or(int64_t{-1});
  h.pelo_fid_cac2 = FidPeloCac2(pelo_fid) ? int(pelo_fid) : -1;
  if (auto* fr = (*p)["forma_receta"].as_array())
    for (size_t k = 0; k < fr->size() && k < size_t(kMaxFormasReceta); ++k) h.forma_receta[k] = int((*fr)[k].value_or(int64_t{-1}));
  h.pelo_c1 = LeerHsv((*p)["pelo_color1"].node());
  h.pelo_c2 = LeerHsv((*p)["pelo_color2"].node());
  h.ojos_c = LeerHsv((*p)["ojos_color"].node());
  h.piel_c = LeerHsv((*p)["piel_color"].node());
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
  const std::lock_guard<std::recursive_mutex> lock(g_archivo_mu);
  std::error_code ec;
  if (path.size() < 4 || path.compare(path.size() - 4, 4, ".bak") != 0) g_ruta = path;
  if (!fs::exists(path, ec)) {
    // Un cierre interrumpido de builds anteriores puede dejar solo el respaldo.
    if (!ec && fs::exists(path + ".bak", ec)) return Leer(path + ".bak");
    return false;
  }
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
  g_migrado = false;
  g_por_limite = 0;
  if (ver < 2) {                                      // v1 -> v2: copia INTACTA antes de tocar nada (una sola vez)
    const std::string v1 = path + ".v1.bak";
    if (!fs::exists(v1, ec)) fs::copy_file(path, v1, ec);
    g_migrado = true;
  }
  if (ver == 2) {                                     // v2 -> v3 (capas y recetas): copia INTACTA, una sola vez
    const std::string v2 = path + ".v2.bak";
    if (!fs::exists(v2, ec)) fs::copy_file(path, v2, ec);
  }
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
  // limite de heroes activos: el primero (el de la v1) queda en la ficha 1; los que no caben se archivan, nunca se pierden
  int activo = int(t["activo"].value_or(int64_t{0}));
  if (int(g_slots.size()) > kMaxSlots) {
    if (ver >= 2) {   // un v2 con 4-8 heroes (escrito por otra build): copia aparte antes de archivar nada
      const std::string rec = path + ".recorte.bak";
      if (!fs::exists(rec, ec)) fs::copy_file(path, rec, ec);
    }
    // el heroe ACTIVO no se archiva: pasa al ultimo de los 3 (si no, su progreso se asignaba a otro heroe)
    if (activo >= kMaxSlots && activo < int(g_slots.size())) {
      std::swap(g_slots[size_t(activo)], g_slots[size_t(kMaxSlots - 1)]);
      activo = kMaxSlots - 1;
    }
    while (int(g_slots.size()) > kMaxSlots) {
      g_archivados.insert(g_archivados.begin(), g_slots.back());
      g_slots.pop_back();
      ++g_por_limite;
    }
  }
  for (Heroe& h : g_archivados) AjustarHeroe(h);
  if (g_slots.empty()) g_slots.push_back(Heroe{});
  g_activo = std::clamp(activo, 0, NumSlots() - 1);
  for (int i = 0; i < NumSlots(); ++i) AjustarSlot(i);
  return true;
}

bool RespaldarCopia(const char* motivo) {
  const std::lock_guard<std::recursive_mutex> lock(g_archivo_mu);
  std::error_code ec;
  if (g_ruta.empty() || !fs::exists(g_ruta, ec)) return false;
  const auto t = std::time(nullptr);
  char f[32];
  std::strftime(f, sizeof f, "%Y%m%d-%H%M%S", std::localtime(&t));
  fs::copy_file(g_ruta, g_ruta + "." + motivo + "-" + f + ".bak", fs::copy_options::overwrite_existing, ec);
  return !ec;
}

bool Guardar(const std::string& path) {
  const std::lock_guard<std::recursive_mutex> lock(g_archivo_mu);
  g_ruta = path;
  for (Heroe& h : g_slots) {   // el heroe inicial nace sin id: se le da UNO y no cambia
    if (h.id.empty()) h.id = NuevoId();
  }
  for (Heroe& h : g_archivados) {
    if (h.id.empty()) h.id = NuevoId();
  }
  // Un generador de una build anterior puede conservar abierto el viejo .tmp.
  // Cada transaccion usa su propio nombre; los restos nunca se truncan ni borran.
  static unsigned secuencia = 0;
  const std::string tmp = path + ".tmp." + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(++secuencia);
  const std::string bak = path + ".bak";
  std::error_code ec;
  if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) return false;
  std::ostringstream o;
  o << "version = 3\nschema = \"dbz3-cac/3\"\n";
  o << "activo = " << g_activo << "\n\n";
  for (const Heroe& h : g_slots) {
    EscribirHeroe(o, "heroe", h);
    o << "\n";
  }
  for (const Heroe& h : g_archivados) {
    EscribirHeroe(o, "archivado", h);
    o << "\n";
  }
  // fopen/ifstream normales en MSVC crean handles heredables. Un CreateProcess
  // del preview con redireccion de salida podia heredarlos y retener el .tmp
  // incluso despues de cerrar el juego. N evita esa herencia en Windows.
  std::FILE* f = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&f, fs::path(tmp).c_str(), L"wbN") != 0) return false;
#else
  f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
#endif
  const std::string texto = o.str();
  const bool escrito = std::fwrite(texto.data(), 1, texto.size(), f) == texto.size();
  const bool vaciado = std::fflush(f) == 0;
  const bool cerrado = std::fclose(f) == 0;
  if (!escrito || !vaciado || !cerrado) return false;
  const bool hay_principal = fs::exists(path, ec);
  if (ec) return false;
  if (hay_principal) {
    // El principal sigue disponible hasta el commit. Preparar tambien el respaldo
    // por separado evita dejarlo truncado si se interrumpe la copia.
    const std::string bak_tmp = bak + ".tmp";
    fs::copy_file(path, bak_tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::rename(bak_tmp, bak, ec);
    if (ec) return false;
  }
  // Ambos archivos estan en el mismo directorio: rename reemplaza el destino
  // sin el intervalo sin principal del antiguo principal->bak, tmp->principal.
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
