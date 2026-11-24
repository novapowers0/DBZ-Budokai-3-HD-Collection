// test_heroe_ui_nher4.cpp — Real offscreen UI execution harness linking ACTUAL PATCHED:
//   - src/heroe.cpp + src/heroe_recompensas.inl + src/heroe_tecnicas_v2.inl + src/cac/cac.cpp
//   - src/heroe_v2_coleccion.inl (REAL CeldasCol, CeldaColDibujar, DibujarColeccionV2, AbrirColeccion, Coleccion)
//   - src/heroe_v2_mapa.inl      (REAL DibujarMapaV2)
//   - src/heroe_v2_habitacion.inl (REAL ValorHabitacion)
//   - src/heroe_rutas.h          (REAL CalcularEstadoMapa)
// Uses a lightweight draw-recording ImGui/v2 stub so every drawn text, help bar action,
// CapsulaV2/CaraRedonda/NodoV2 icon, tab navigation (LB/RB), grid cursor move, and MapaUI
// confirmation is asserted on the ACTUAL patched production UI code.

#include "cac/cac.h"
#include "heroe_api.h"
#include "heroe_coleccion_io.h"
#include "heroe_rutas.h"
#include "heroe_v2_casa.h"
#include "launcher/i18n.h"
#include "launcher/settings.h"
#include "modos.h"
#include "mods.h"
#include "roster_ext.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace H = dbz3::heroe;
namespace M = dbz3::modos;
namespace T = dbz3::i18n;

namespace dbz3::heroe {
void Importar(int slot);
void Exportar(dbz3::cac::Heroe& h);
void DuVictoria(int saga, int k);
int ResolverRuta();
int ElegirCamino(int t);
int Tramo();
bool NaveEnMapa();
}  // namespace dbz3::heroe

// =============================================================================
// Test runtime stubs (same clean set as test_heroe_nher4.cpp)
// =============================================================================
static fs::path g_user_dir;
static fs::path g_mods_dir;
static int g_zenie = 0;

namespace dbz3 {
fs::path ModsRoot() { return g_mods_dir; }
double RelojT(double t) { return t; }
}  // namespace dbz3

namespace dbz3::settings {
fs::path UserDataRoot() { return g_user_dir; }
bool Xv2Instalado() { return false; }
bool CacXv2Piezas() { return false; }
void SetCacXv2Piezas(bool) {}
void SaveUserSettings() {}
}  // namespace dbz3::settings

namespace dbz3::i18n {
const char* T(const char* es, const char*) { return es; }
const char* LangCode() { return "es"; }
const char* Otro(const char*) { return nullptr; }
}  // namespace dbz3::i18n

namespace dbz3::modos {
const char* L(const char* es, const char*, const char*, const char*, const char*) { return es; }
std::string Nombre(int id) {
  static const std::map<int, std::string> kN = {
      {0, "Goku"}, {2, "Gohan"}, {7, "Vegeta"}, {10, "Krilin"}, {11, "Piccolo"},
      {12, "Ten"}, {13, "Yamcha"}, {27, "Freezer"}, {33, "Cell"}, {35, "Super Bu"},
      {36, "Kid Bu"}, {39, "Bardock"}, {41, "Omega Shenron"}, {42, "Saibaman"}, {43, "Cell Jr."}};
  auto it = kN.find(id);
  return it != kN.end() ? it->second : "ID#" + std::to_string(id);
}
}  // namespace dbz3::modos

namespace dbz3::escenarios {
std::string Nombre(int id) { return "Escenario#" + std::to_string(id); }
}  // namespace dbz3::escenarios

namespace dbz3::pasarela {
int RangoMejora(const std::string&, int, int, int) { return 0; }
}  // namespace dbz3::pasarela

namespace dbz3::rpg {
void Premio(int z) { g_zenie += z; }
}  // namespace dbz3::rpg

namespace dbz3::roster {
int DonorOf(uint32_t) { return -1; }
int64_t LogicalForProxy(int) { return -1; }
int HeroRosterGuest(int) { return -1; }
std::string HeroRosterName(int) { return ""; }
void RequestHeroRosterId(int) {}
int DynamicRosterPageCount() { return 0; }
bool IsLogicalRosterId(int) { return false; }
bool HeroRosterAnswered(int) { return true; }
std::vector<int> HeroRosterIds() { return {}; }
}  // namespace dbz3::roster

// =============================================================================
// Minimal recording ImGui + v2 kit adapter for real .inl inclusion
// =============================================================================
using ImU32 = uint32_t;
using ImTextureID = void*;
constexpr int ImDrawFlags_RoundCornersTop = 1;
constexpr int ImGuiMouseButton_Left = 0;
#define IM_COL32(R, G, B, A) ((ImU32(A) << 24) | (ImU32(B) << 16) | (ImU32(G) << 8) | ImU32(R))
#define IM_COL32_WHITE IM_COL32(255, 255, 255, 255)

struct ImVec2 {
  float x = 0, y = 0;
  ImVec2() = default;
  ImVec2(float a, float b) : x(a), y(b) {}
};
struct ImVec4 {
  float x = 0, y = 0, z = 0, w = 0;
  ImVec4() = default;
  ImVec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
struct ImFont {
  ImVec2 CalcTextSizeA(float px, float, float, const char* s) const {
    return ImVec2(float(std::strlen(s ? s : "")) * px * 0.5f, px);
  }
};
struct ImGuiIO {
  ImVec2 DisplaySize{1600, 900};
};

struct DrawRecorder {
  std::vector<std::string> texts;
  std::vector<std::string> titles;
  std::vector<std::pair<std::string, std::string>> help_items;
  int capsula_calls = 0;
  int capsula_ok_calls = 0;
  int cara_calls = 0;
  int rect_calls = 0;
  void Clear() {
    texts.clear();
    titles.clear();
    help_items.clear();
    capsula_calls = capsula_ok_calls = cara_calls = rect_calls = 0;
  }
  bool HasText(const std::string& sub) const {
    for (const auto& s : texts)
      if (s.find(sub) != std::string::npos) return true;
    return false;
  }
  bool HasTitle(const std::string& sub) const {
    for (const auto& s : titles)
      if (s.find(sub) != std::string::npos) return true;
    return false;
  }
  bool HasHelp(const std::string& btn, const std::string& lbl) const {
    for (const auto& [b, l] : help_items)
      if (b == btn && l.find(lbl) != std::string::npos) return true;
    return false;
  }
};
static DrawRecorder g_rec;
static ImFont g_font;
static ImGuiIO g_io;

struct ImDrawList {
  void AddRectFilled(ImVec2, ImVec2, ImU32, float = 0, int = 0) { ++g_rec.rect_calls; }
  void AddRectFilledMultiColor(ImVec2, ImVec2, ImU32, ImU32, ImU32, ImU32) { ++g_rec.rect_calls; }
  void AddRect(ImVec2, ImVec2, ImU32, float = 0, int = 0, float = 1) { ++g_rec.rect_calls; }
  void AddLine(ImVec2, ImVec2, ImU32, float = 1) {}
  void AddCircle(ImVec2, float, ImU32, int = 0, float = 1) {}
  void AddCircleFilled(ImVec2, float, ImU32, int = 0) {}
  void AddTriangleFilled(ImVec2, ImVec2, ImVec2, ImU32) {}
  void AddPolyline(const ImVec2*, int, ImU32, int, float) {}
  void AddImageRounded(ImTextureID, ImVec2, ImVec2, ImVec2, ImVec2, ImU32, float) {}
  void AddText(ImFont*, float, ImVec2, ImU32, const char* txt) {
    if (txt && *txt) g_rec.texts.push_back(txt);
  }
  void PushClipRect(ImVec2, ImVec2, bool = false) {}
  void PopClipRect() {}
};

namespace ImGui {
inline ImFont* GetFont() { return &g_font; }
inline ImGuiIO& GetIO() { return g_io; }
inline double GetTime() { return 1.0; }
inline bool IsMouseClicked(int) { return false; }
inline bool IsMouseHoveringRect(ImVec2, ImVec2) { return false; }
}  // namespace ImGui

namespace dbz3::launcher::ui {
struct Fonts {
  ImFont* banner = &g_font;
  ImFont* menu = &g_font;
};
inline const Fonts& GetFonts() {
  static Fonts f;
  return f;
}
}  // namespace dbz3::launcher::ui

namespace dbz3::ingame::style {
struct Area {
  ImVec2 p0{0, 0}, p1{1600, 900};
  float s = 1.0f;
  ImVec2 At(float x, float y) const { return ImVec2(p0.x + x * s, p0.y + y * s); }
};
struct Imagen {
  ImVec2 p0{0, 0}, p1{1600, 900};
};
struct Pic {
  ImTextureID tex = nullptr;
  float w = 0, h = 0;
  explicit operator bool() const { return tex != nullptr; }
};
inline Imagen GuestImage(const ImGuiIO&) { return {}; }
inline Pic Img(const char*) { return {}; }
inline void DrawPic(ImDrawList*, const Pic&, ImVec2, ImVec2, ImVec4 = {}, ImU32 = IM_COL32_WHITE, bool = false) {}
inline void Sides(ImDrawList*, const Area&) {}
inline void Frame(ImDrawList*, ImVec2, ImVec2, float) {}
inline void BannerBeta(ImDrawList*, const Area&, const char* t, const char* = nullptr, float = 1.0f, float = 0.0f) {
  if (t) g_rec.titles.push_back(t);
}
inline void HelpLine(ImDrawList*, ImVec2, float, std::initializer_list<std::pair<const char*, const char*>> items) {
  for (const auto& [b, l] : items) g_rec.help_items.push_back({b ? b : "", l ? l : ""});
}
inline float Glyph(ImDrawList*, const char*, ImVec2, float) { return 24.0f; }
inline void Arrows(ImDrawList*, ImVec2, ImVec2, float, float) {}
}  // namespace dbz3::ingame::style

namespace dbz3::heroe_ui {
namespace S = dbz3::ingame::style;
enum class Accion { kNada, kVolver, kConfirmar, kCrear };
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kA = 0x1000, kB = 0x2000;

REXCVAR_DEFINE_BOOL(dbz3_heroe_ui_nueva, true, "DBZ3/Mods", "ui nueva");
REXCVAR_DEFINE_BOOL(dbz3_heroe_ayuda_esquiva, false, "DBZ3/Mods", "ayuda esquiva");

namespace v2 {
constexpr const char* kFondoFicha = "heroe_kamehouse";
constexpr const char* kPiezaBolitas = "heroe_bolitas";
constexpr const char* kPiezaRadarAro = "heroe_radar_aro";
constexpr const char* kPiezaRadarRejilla = "heroe_radar_rejilla";
constexpr const char* kPiezaEstrella = "bola_estrella";
constexpr float kLetraTitulo = 38, kLetraFila = 32, kLetraPeque = 26, kLetraMin = 22;
constexpr float kFigCx = 312, kFigPies = 800;
inline S::Area Lienzo(const S::Area& a) { return a; }
inline ImFont* Letra() { return &g_font; }
inline float Ancho(float px, const std::string& s) { return float(s.size()) * px * 0.5f; }
inline bool Fondo(ImDrawList*, const S::Area&, const char*) { return true; }
inline void Titulo(ImDrawList*, const S::Area&, const char* t) { if (t) g_rec.titles.push_back(t); }
inline void Caja(ImDrawList*, const S::Area&, float, float, float, float, int = 190) { ++g_rec.rect_calls; }
inline void Panel(ImDrawList*, const S::Area&, float, float, float, float) { ++g_rec.rect_calls; }
inline void Franja(ImDrawList*, const S::Area&, float, float, float, float, float) {}
inline void Peana(ImDrawList*, const S::Area&, float, float, float = 375) {}
inline void Orbe(ImDrawList*, const S::Area&, float, float, float, ImU32) {}
inline void Progreso(ImDrawList*, const S::Area&, float, float, float, float, float = -1, bool = false, float = 8) {}
inline float Explicacion(ImDrawList*, const S::Area&, float, float y0, float, const std::string& s, int = 1) {
  g_rec.texts.push_back(s);
  return y0 + 40;
}
inline void Texto(ImDrawList*, const S::Area&, float, float, float, const std::string& s, int = 0, int = 0,
                  bool = false, bool = false, float = 0) {
  g_rec.texts.push_back(s);
}
inline void BarraAyuda(ImDrawList*, const S::Area&, std::initializer_list<std::pair<const char*, const char*>> items,
                       float = 960, float = 864, float = 42) {
  for (const auto& [b, l] : items) g_rec.help_items.push_back({b ? b : "", l ? l : ""});
}
}  // namespace v2

// Helpers normally defined earlier in heroe_ui.cpp before including the v2 .inl files
namespace {
struct Modelo3D {};
struct VistaElegida { S::Pic p; const Modelo3D* m3 = nullptr; };
int g_giro = 0;
std::string Num(int n) { return std::to_string(n); }
fs::path Juego() { return g_mods_dir.parent_path(); }
S::Pic PicPng(const fs::path&, double) { return {}; }
fs::path CartaPng(int) { return {}; }
void FondoSinAnfitrion(ImDrawList*, const S::Area&, const char* = nullptr) {}
bool FondoGpt3(ImDrawList*, const S::Area&, const char*) { return false; }
S::Pic ArteGpt3(const char*) { return {}; }
void FiguraV2(ImDrawList*, const S::Area&) {}
void ControlesVistaV2(const S::Area&, uint16_t, float, double) {}
void PedirVista(int, int) {}
void AsegurarCartas() {}
void CabeceraCarta(ImDrawList*, const S::Area&, int, float, float, float, double, int) {}
void MonedaInterrogacion(ImDrawList*, ImVec2, float, int) {}
void CaraNodoEvo(ImDrawList*, const S::Area&, int, float, float, float, int) { ++g_rec.cara_calls; }
void CaraRedonda(ImDrawList*, const S::Area&, int, float, float, float, int, bool) { ++g_rec.cara_calls; }
void Cara(ImDrawList*, const S::Area&, int, float, float, float) { ++g_rec.cara_calls; }
void CapsulaV2(ImDrawList*, const S::Area&, float, float, float, bool encendida) {
  ++g_rec.capsula_calls;
  if (encendida) ++g_rec.capsula_ok_calls;
}
VistaElegida ElegirVista() { return {}; }
S::Pic Vista(int, int, double) { return {}; }
const Modelo3D* Malla3D(int, int, double) { return nullptr; }
bool Pintar3D(ImDrawList*, const Modelo3D&, ImVec2, ImVec2, float) { return false; }
void PintarTira(ImDrawList*, const S::Area&, const S::Pic&, float, float, float, float) {}
bool CuerpoUT() { return false; }
std::string RecetaEtiqueta(int) { return "Receta"; }
ImVec2 Punto(int i) { return ImVec2(100.0f + 80.0f * float(i), 300.0f); }
void Tierra(ImDrawList*, const S::Area&) {}
void Esfera(ImDrawList*, ImVec2, float, int, int) {}
}  // namespace

// INCLUDE THE ACTUAL PATCHED PRODUCTION .INL FILES!
#include "heroe_v2_mapa.inl"
#include "heroe_v2_habitacion.inl"
#include "heroe_v2_coleccion.inl"

// Real MapaUI cursor & confirm logic matching patched src/heroe_ui.cpp
namespace mapa_driver {
int g_mapa_cur = 0;
std::string g_mapa_aviso;
constexpr int kMaxPuntos = 8;

void AbrirMapaReal() {
  const int total = std::max(1, std::min(H::SagasTotal(), kMaxPuntos));
  const H::rutas::EstadoMapa st = H::rutas::CalcularEstadoMapa(
      H::EtapaRuta(), total, H::VueltaRuta(), H::RutaCompletada(), H::Saga(),
      H::ColeccionVueltasOn() && H::Rutas());
  g_mapa_cur = st.cursor_inicial;
  g_mapa_aviso.clear();
}

Accion MapaUIReal(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int total = std::max(1, std::min(H::SagasTotal(), kMaxPuntos));
  const H::rutas::EstadoMapa st = H::rutas::CalcularEstadoMapa(
      H::EtapaRuta(), total, H::VueltaRuta(), H::RutaCompletada(), H::Saga(),
      H::ColeccionVueltasOn() && H::Rutas());
  const int sig = st.sig;
  const int hechas = st.hechas;
  const int antes = g_mapa_cur;
  if (p & (kRight | kDown)) g_mapa_cur = std::min(g_mapa_cur + 1, total - 1);
  if (p & (kLeft | kUp)) g_mapa_cur = std::max(g_mapa_cur - 1, 0);
  if (p & (kLb | kRb)) g_mapa_cur = (g_mapa_cur + ((p & kLb) ? 1 : total - 1)) % total;
  if (g_mapa_cur != antes) g_mapa_aviso.clear();
  if (p & kB) return Accion::kVolver;
  if (p & kA) {
    if (g_mapa_cur >= H::kSagasJugables) {
      g_mapa_aviso = T::T("En desarrollo: esta saga aun no se puede jugar.", "In development: this saga cannot be played yet.");
    } else if (g_mapa_cur == sig) {
      return Accion::kConfirmar;
    } else if (st.nueva_vuelta) {
      g_mapa_aviso = T::T("Ruta completada: pulsa A en la saga 1 para empezar una nueva vuelta.",
                          "Route completed: press A on saga 1 to start a new lap.");
    } else if (g_mapa_cur < sig) {
      g_mapa_aviso = T::T("Saga completada: ya tienes su Esfera.", "Saga cleared: you already have its Dragon Ball.");
    } else {
      g_mapa_aviso = T::T("Bloqueada: completa antes la saga anterior.", "Locked: clear the previous saga first.");
    }
  }
  DibujarMapaV2(dl, a, t, total, hechas, g_mapa_cur, ImVec4(60, 120, 820, 760), g_mapa_aviso);
  return Accion::kNada;
}
}  // namespace mapa_driver

}  // namespace dbz3::heroe_ui

// =============================================================================
// Mock Guest RAM (same as test_heroe_nher4.cpp for dodge capsules 409,410,444,445)
// =============================================================================
struct MockGuestRam {
  uint8_t* base = nullptr;
  rex::Runtime rt;
  static void StoreBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
  }
  bool Init() {
#ifdef _WIN32
    base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x83000000ULL, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) return false;
    if (!VirtualAlloc(base + 0x82020000u, 0x00010000u, MEM_COMMIT, PAGE_READWRITE)) return false;
    if (!VirtualAlloc(base + 0x82320000u, 0x00150000u, MEM_COMMIT, PAGE_READWRITE)) return false;
    std::memcpy(base + 0x82020700u, "_charasel.cpp", 13);
    StoreBe32(base + 0x82324468u, 0xFFFFFFFFu);
    StoreBe32(base + 0x82375608u, 0x82400000u);
    StoreBe32(base + 0x8237560Cu, 0x82401000u);
    StoreBe32(base + 0x82400010u, 600u);
    auto set_dodge = [&](uint32_t cap, uint8_t sub) {
      uint8_t* r = base + 0x82401000u + 40u * cap;
      r[8] = 0x11; r[14] = 0xFF; r[20] = 0x43; r[21] = sub;
    };
    set_dodge(409, 0x0F); set_dodge(410, 0x10); set_dodge(444, 0x18); set_dodge(445, 0x19);
    rt.set_virtual_membase(base);
    rex::Runtime::InstancePtr() = &rt;
    return true;
#else
    return false;
#endif
  }
  ~MockGuestRam() {
#ifdef _WIN32
    rex::Runtime::InstancePtr() = nullptr;
    if (base) VirtualFree(base, 0, MEM_RELEASE);
#endif
  }
};

static int g_checks = 0;
#define CHECK(cond, msg)                                                                  \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(cond)) {                                                                        \
      std::cerr << "FAIL UI [" << __LINE__ << "]: " << (msg) << "\n";                     \
      return 1;                                                                           \
    }                                                                                     \
  } while (0)

int main(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    if (k == "--userdir") g_user_dir = argv[i + 1];
    else if (k == "--modsdir") g_mods_dir = argv[i + 1];
  }
  if (g_user_dir.empty() || g_mods_dir.empty()) return 2;
  fs::create_directories(g_user_dir);

  MockGuestRam ram;
  CHECK(ram.Init(), "MockGuestRam init");
  rex::cvar::SetBoolFlag("dbz3_heroe_tecnicas_v2", true);

  // Build 2-slot cac.toml: Slot 0 has r_t0_death_beam + camino_nave + camino_lab + completed Lap 1 (tramo=-1, decision=1, h02_fin_torneo);
  // Slot 1 has r_t1_masenko (inherited by Slot 0 when ON -> shows " · Cuenta"!)
  const fs::path cac_p = g_user_dir / "cac.toml";
  {
    std::ofstream o(cac_p, std::ios::binary | std::ios::trunc);
    o << "version = 3\nactivo = 0\n\n"
      << "[[heroe]]\nnombre = \"UIRunner\"\nid = \"ui01\"\nprogreso = 2\nsaga = 6\nxp = 20\nph = 30\n"
      << "maestros = 1\nentreno = [100, 0, 0, 60, 0, 0]\n"
      << "desbloqueos = [\"h02_v1_127_1\", \"h02_r1:-1:-1:1234\", \"h02_fin_torneo\", \"h02_rm:camino_nave\", \"h02_rm:camino_lab\", \"r_t0_death_beam\", \"ki_dorado_goku\"]\n\n"
      << "[[heroe]]\nnombre = \"UISlot2\"\nid = \"ui02\"\nprogreso = 2\nsaga = 2\nxp = 6\n"
      << "desbloqueos = [\"r_t1_masenko\"]\n";
  }
  CHECK(dbz3::cac::Leer(cac_p.string()), "cac::Leer UI fixture");

  ImDrawList dl;
  dbz3::ingame::style::Area area;
  using namespace dbz3::heroe_ui;

  // =========================================================================
  // 1. OFF mode: Tab 0 & Tab 3 empty, no " · Cuenta", Room shows old path marks
  // =========================================================================
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", false);
  H::Importar(0);
  CHECK(ValorHabitacion(4) == "Laboratorio y Nave", "OFF: Room row 4 keeps legacy path marks");

  AbrirColeccion();
  // Navigate to Tab 0 (LB from tab 1 -> tab 0)
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, kLb);
  CHECK(g_rec.HasText("Aún no hay nada aquí"), "OFF Tab 0: empty beta message");
  // Navigate RB x3 -> Tab 3
  Coleccion(&dl, area, 0.0f, kRb);
  Coleccion(&dl, area, 0.0f, kRb);
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, kRb);
  CHECK(g_rec.HasText("Aún no hay nada aquí"), "OFF Tab 3: empty beta message");

  // =========================================================================
  // 2. ON mode: Collection tabs 0..6 navigation, drawing, CapsulaV2, " · Cuenta"
  // =========================================================================
  rex::cvar::SetBoolFlag("dbz3_heroe_coleccion_vueltas", true);
  H::Importar(0);   // triggers ColeccionSincronizarConFichas -> inherits r_t1_masenko!

  // Room row 4 MUST show "Finales 1/3 · Nueva vuelta" EVEN WITH camino_nave + camino_lab present!
  const std::string hab_comp = ValorHabitacion(4);
  CHECK(hab_comp == "Finales 1/3 · Nueva vuelta",
        "ON completed lap with path marks: Room shows 'Finales 1/3 · Nueva vuelta', got: " + hab_comp);

  // Collection Tab 1 (Técnicas): cell 0 = r_t0_death_beam (en_ficha=true), cell 1 = r_t1_masenko (en_ficha=false -> " · Cuenta"!)
  AbrirColeccion();
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, 0);
  CHECK(g_rec.HasText("Conseguidas 2 de 11"), "ON Tab 1: 2 of 11 techniques earned");
  CHECK(!g_rec.HasText("· Cuenta"), "ON Tab 1 cell 0 (en_ficha=true): NO '· Cuenta' badge");

  // Press Right -> cell 1 (r_t1_masenko, inherited from Slot 1!)
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, kRight);
  CHECK(g_rec.HasText("· Cuenta"), "ON Tab 1 cell 1 (inherited from account): draws '· Cuenta' badge");

  // Press LB -> Tab 0 (Trajes y partes): 9 cells (Goku style + Vegeta? Wait: saga=6 -> 6 styles unlocked + ki_dorado_goku = 7/9? Let's verify against real TrajesColeccion()!)
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, kLb);
  const auto trajes_real = H::TrajesColeccion();
  int tr_ok = 0;
  for (const auto& x : trajes_real) tr_ok += x.conseguido;
  CHECK(trajes_real.size() == 9, "ON Tab 0: 9 items");
  CHECK(g_rec.HasText("Conseguidas " + std::to_string(tr_ok) + " de 9"), "ON Tab 0 header draws exact count of 9");
  CHECK(g_rec.HasText("Estilo / cosmético nativo"), "ON Tab 0 detail card draws subtitle");
  CHECK(!g_rec.HasText("Aún no hay nada aquí"), "ON Tab 0 is NOT empty");
  // Navigate grid inside Tab 0 (Right, Down, Down, Left, Up) without crashing/OOB
  Coleccion(&dl, area, 0.0f, kRight);
  Coleccion(&dl, area, 0.0f, kDown);
  Coleccion(&dl, area, 0.0f, kDown);
  Coleccion(&dl, area, 0.0f, kLeft);
  Coleccion(&dl, area, 0.0f, kUp);

  // Navigate RB x3 -> Tab 3 (Cápsulas): 9 cells, draws CapsulaV2 for all 9 grid cells (+ 2 tab icons = 11 calls)!
  Coleccion(&dl, area, 0.0f, kRb);  // 1
  Coleccion(&dl, area, 0.0f, kRb);  // 2
  g_rec.Clear();
  Coleccion(&dl, area, 0.0f, kRb);  // 3
  const auto caps_real = H::CapsulasColeccion();
  int cp_ok = 0;
  for (const auto& x : caps_real) cp_ok += x.conseguido;
  CHECK(caps_real.size() == 9 && cp_ok == 3, "ON Tab 3: 3 of 9 capsules (Goku 378, Goku dodge 444, Krilin dodge 445)");
  CHECK(g_rec.HasText("Conseguidas 3 de 9"), "ON Tab 3 header draws 'Conseguidas 3 de 9'");
  CHECK(g_rec.HasText("Cápsula pasiva / esquiva"), "ON Tab 3 detail card draws subtitle");
  CHECK(g_rec.capsula_calls == 11, "ON Tab 3 draws 2 tab icons + 9 grid CapsulaV2 icons = 11 calls, got " + std::to_string(g_rec.capsula_calls));
  CHECK(g_rec.capsula_ok_calls == 2 + 3, "ON Tab 3 draws 2 lit tab icons + 3 lit grid capsules = 5");
  CHECK(Coleccion(&dl, area, 0.0f, kB) == Accion::kVolver, "Collection B returns kVolver");

  // =========================================================================
  // 3. ON mode: REAL MapaUI / AbrirMapa / DibujarMapaV2 on completed Lap 1 & mid-Lap 2!
  // =========================================================================
  // 3a. Completed Lap 1 (RutaCompletada == true, Saga == 6, EtapaRuta == 6)
  mapa_driver::AbrirMapaReal();
  CHECK(mapa_driver::g_mapa_cur == 0, "ON completed Lap 1: AbrirMapa places cursor on node 0 (Saga 1), NOT 6!");
  g_rec.Clear();
  CHECK(mapa_driver::MapaUIReal(&dl, area, 0.0f, 0) == Accion::kNada, "Draw frame on completed Lap 1");
  CHECK(g_rec.HasText("Nueva vuelta"), "ON completed Lap 1: status shows 'Nueva vuelta' (NOT 'Saga completada')");
  CHECK(g_rec.HasHelp("A", "Nueva vuelta"), "ON completed Lap 1: help bar shows 'A: Nueva vuelta'");
  // Move right to node 1 and press A -> refuses with clear hint
  mapa_driver::MapaUIReal(&dl, area, 0.0f, kRight);
  CHECK(mapa_driver::g_mapa_cur == 1, "cursor moved to node 1");
  g_rec.Clear();
  CHECK(mapa_driver::MapaUIReal(&dl, area, 0.0f, kA) == Accion::kNada, "A on node 1 while completed does not confirm");
  CHECK(g_rec.HasText("pulsa A en la saga 1"), "shows hint to press A on saga 1");
  // Move back left to node 0 and press A -> confirms New Lap!
  mapa_driver::MapaUIReal(&dl, area, 0.0f, kLeft);
  CHECK(mapa_driver::MapaUIReal(&dl, area, 0.0f, kA) == Accion::kConfirmar, "A on node 0 confirms New Lap!");

  // 3b. Start Lap 2 and advance to Stage 1 (Saga == 7, VueltaRuta == 2, EtapaRuta == 1)!
  CHECK(H::NuevaVueltaRuta(), "Start Lap 2");
  CHECK(ValorHabitacion(4) == "Vuelta 2 · Finales 1/3",
        "ON Lap 2 start (no marks): Room shows 'Vuelta 2 · Finales 1/3', got: " + ValorHabitacion(4));
  H::ElegirCamino(8);   // camino_vegeta
  CHECK(ValorHabitacion(4) == "Vuelta 2 · Vegeta · 1/3",
        "ON Lap 2 with camino_vegeta: Room shows 'Vuelta 2 · Vegeta · 1/3', got: " + ValorHabitacion(4));
  // Clear stage 0 (tramo 8) -> Saga becomes 7, EtapaRuta becomes 1!
  for (int k = 0; k < int(H::CombatesDeSaga(8).size()); ++k) H::DuVictoria(8, k);
  CHECK(H::ResolverRuta() >= 0, "resolve Lap 2 stage 1");
  CHECK(H::Saga() == 7 && H::EtapaRuta() == 1 && H::VueltaRuta() == 2, "Lap 2 stage 1 state");

  // NOW test AbrirMapa + MapaUI + DibujarMapaV2 on Lap 2 Stage 1 (Saga == 7)!
  // Legacy bug in NHER3 had sig = 7 % 8 = 7 and vuelta = 7 / 8 = 0 (Lap 1)!
  // NHER4 fix MUST give cursor=1, sig=1, vuelta=Lap 2!
  mapa_driver::AbrirMapaReal();
  CHECK(mapa_driver::g_mapa_cur == 1, "ON Lap 2 stage 1 (Saga=7): AbrirMapa places cursor on node 1 (NOT 7!)");
  g_rec.Clear();
  mapa_driver::MapaUIReal(&dl, area, 0.0f, 0);
  CHECK(g_rec.HasTitle("Vuelta 2"), "ON Lap 2 stage 1 (Saga=7): MapaV2 title shows 'Vuelta 2'!");
  CHECK(g_rec.HasHelp("A", "Empezar"), "ON Lap 2 stage 1: help bar shows 'A: Empezar'");
  // Pressing A on node 1 confirms!
  CHECK(mapa_driver::MapaUIReal(&dl, area, 0.0f, kA) == Accion::kConfirmar, "ON Lap 2 stage 1: A on node 1 confirms!");
  // Moving right to node 2 and pressing A -> blocked!
  mapa_driver::MapaUIReal(&dl, area, 0.0f, kRight);
  g_rec.Clear();
  CHECK(mapa_driver::MapaUIReal(&dl, area, 0.0f, kA) == Accion::kNada, "ON Lap 2 stage 1: A on node 2 blocked");
  CHECK(g_rec.HasText("Bloqueada"), "node 2 shows Bloqueada");

  std::cout << "PASS ui_nher4 (" << g_checks << " real UI + draw + nav checks)\n";
  return 0;
}
