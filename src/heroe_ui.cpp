// dbz3 - Modo Heroe: pantallas Create / Edit y Select Master al estilo Ultimate Tenkaichi (ver heroe_ui.h).
// Referencia: studies 7\10_heroe_rework_ut.md ("Referencia visual de UT": vista previa a la izquierda, lista a la
// derecha, Part 1-4 con Hue/Saturation/Brightness, Logo Mark, Scouter, botones Confirm/Back/Reset y la linea de ayuda;
// Select Master con retrato, "Training Completion %", modificadores y la regla del 60 %). Estilo: el de los menus de
// Budokai 3 (dbz3::ingame::style, marcos naranjas, letra turquesa), como el resto de pantallas del HUB.
#include "heroe_ui.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
using DWORD = unsigned long;   // Linux: Lanzar va por std::system
#endif

#include <imgui.h>

#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "cac/cac.h"
#include "escenarios.h"
#include "heroe_api.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "modos.h"
#include "mods.h"
#include "ps2_modelo.h"   // ps2::Escena: el pintor 3D con ImGui de los modelos de IW/B2 (toon + borde)

namespace dbz3::heroe_ui {
namespace {
namespace S = dbz3::ingame::style;
namespace D = S::mode;
namespace H = dbz3::heroe;
namespace M = dbz3::modos;
namespace T = dbz3::i18n;
namespace C = dbz3::cac;
namespace fs = std::filesystem;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

std::string Num(int n) { return std::to_string(n); }

// ---------------------------------------------------------------------------------------------------------------------
// Vista previa: tira de N giros (vista_heroe.py) -> textura. Se relee si el fichero cambia (regenerada al guardar).
// ---------------------------------------------------------------------------------------------------------------------
fs::path Juego() { return dbz3::ModsRoot().parent_path(); }
fs::path VistaPng(int id, int traje) {
  return Juego() / "ingame_ui" / "heroe_vista" / (Num(id) + "_" + Num(traje) + ".png");
}
struct Tira {
  std::unique_ptr<rex::ui::ImmediateTexture> tex;
  S::Pic pic;
  fs::file_time_type cuando{};
  double visto = -1e9;
};
std::map<std::string, Tira> g_tiras;
std::mutex g_gen_mu;
std::set<std::string> g_generando, g_fallidas;   // con g_gen_mu

bool Lanzar(const std::wstring& cmd, DWORD* code) {
#if defined(_WIN32)
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  *code = 1;
  if (WaitForSingleObject(pi.hProcess, 180000) == WAIT_OBJECT_0) {
    GetExitCodeProcess(pi.hProcess, code);
  } else {
    TerminateProcess(pi.hProcess, 1);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  const int r = std::system(fs::path(cmd).string().c_str());   // sin consola que ocultar
  *code = r == 0 ? 0 : 1;
  return r != -1;
#endif
}

// python + vista_heroe.py --solo id_traje en un hilo (sin consola). Interpretes: DBZ3_PYTHON, python/ junto al exe
// (el del Kit de Modding), py -3, python (como mod_pipeline.cpp; el alias de la Store da 9009 y se salta).
// args: argumentos de vista_heroe.py tras --juego; hecho: fichero que debe existir al acabar.
void GenerarCon(const std::string& clave, const std::wstring& args, const fs::path& hecho);
void Generar(int id, int traje) {
  if (id >= 100 && id <= 105) return;   // G06a: MallaFicha genera el cuerpo UT con sus materiales
  const std::string clave = Num(id) + "_" + Num(traje);
  GenerarCon(clave, L"--solo " + fs::path(clave).wstring(), VistaPng(id, traje));
}
void GenerarCon(const std::string& clave, const std::wstring& args, const fs::path& hecho) {
  {
    std::lock_guard<std::mutex> lk(g_gen_mu);
    if (g_generando.count(clave) || g_fallidas.count(clave)) return;
    g_generando.insert(clave);
  }
  std::thread([clave, args, hecho] {
    const fs::path script = Juego() / "mod center hd" / "cac" / "vista_heroe.py";
    std::error_code ec;
    bool ok = false;
    if (!fs::exists(script, ec)) {
      REXLOG_WARN("dbz3 heroe: vista previa {}: falta {} (se queda el retrato)", clave, script.string());
    } else {
      std::vector<std::wstring> pys;
      if (const char* e = std::getenv("DBZ3_PYTHON"); e && *e) pys.push_back(L"\"" + fs::path(e).wstring() + L"\"");
      const fs::path port = Juego() / "python" / "python.exe";
      if (fs::exists(port, ec)) pys.push_back(L"\"" + port.wstring() + L"\"");
      pys.push_back(L"py -3");
      pys.push_back(L"python");
      if (const char* e = std::getenv("DBZ3_TEST_SIN_PYTHON"); e && *e == '1') pys.clear();   // prueba: PC sin Python
      const auto t0 = std::chrono::steady_clock::now();
      for (const std::wstring& py : pys) {
        const std::wstring cmd =
            py + L" -X utf8 \"" + script.wstring() + L"\" --juego \"" + Juego().wstring() + L"\" " + args;
        DWORD code = 1;
        if (!Lanzar(cmd, &code)) continue;
        if (code == 9009) continue;   // alias de la Microsoft Store
        ok = code == 0 && fs::exists(hecho, ec);
        break;
      }
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (ok) REXLOG_INFO("dbz3 heroe: vista previa {} generada ({} ms): {}", clave, ms, hecho.string());
      else REXLOG_WARN("dbz3 heroe: vista previa {} no se pudo generar (sin Python o sin data_cmn.afs): se queda la "
                       "anterior o el retrato", clave);
    }
    std::lock_guard<std::mutex> lk(g_gen_mu);
    g_generando.erase(clave);
    if (!ok) g_fallidas.insert(clave);
  }).detach();
}

bool Generando2(const std::string& clave) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_generando.count(clave) != 0 || g_fallidas.count(clave) != 0;
}
bool Generando(int id, int traje) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_generando.count(Num(id) + "_" + Num(traje)) != 0;
}

// Hilo de dibujo. Mira el fichero como mucho una vez por segundo.
S::Pic Vista(int id, int traje, double ahora) {
  Tira& t = g_tiras[Num(id) + "_" + Num(traje)];
  if (ahora - t.visto < 1.0) return t.pic;
  t.visto = ahora;
  const fs::path f = VistaPng(id, traje);
  std::error_code ec;
  const auto cuando = fs::last_write_time(f, ec);
  if (ec) {
    if (!t.pic) Generar(id, traje);
    return t.pic;
  }
  if (t.pic && cuando == t.cuando) return t.pic;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return t.pic;
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return t.pic;
  t.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  t.pic = t.tex ? S::Pic{reinterpret_cast<ImTextureID>(t.tex.get()), float(w), float(h)} : S::Pic{};
  t.cuando = cuando;
  REXLOG_INFO("dbz3 heroe: vista previa {}_{} cargada ({}x{})", id, traje, w, h);
  return t.pic;
}

// ---------------------------------------------------------------------------------------------------------------------
// Vista previa 3D: la malla posada del cuerpo (vista_heroe.py, fichero .malla junto a la tira) pintada EN VIVO con
// ps2::Escena (toon de 3 tonos y borde negro, como los modelos de IW en los minijuegos), girando de verdad.
// ---------------------------------------------------------------------------------------------------------------------
struct Modelo3D {
  dbz3::ps2::Malla malla;
  std::vector<dbz3::ps2::Tex> tx;
  float alto = 0;
  float ancho = 0;
  fs::file_time_type cuando{};
  double visto = -1e9;
  explicit operator bool() const { return alto > 0 && !malla.tris.empty(); }
};
std::map<std::string, Modelo3D> g_mallas;

bool LeerMalla(const fs::path& f, Modelo3D& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 0;
  auto u32 = [&](uint32_t& v) {
    if (o + 4 > d.size()) return false;
    std::memcpy(&v, d.data() + o, 4);
    o += 4;
    return true;
  };
  auto f32 = [&](float& v) {
    if (o + 4 > d.size()) return false;
    std::memcpy(&v, d.data() + o, 4);
    o += 4;
    return true;
  };
  if (d.size() < 16 || std::memcmp(d.data(), "HMV1", 4) != 0) return false;
  o = 4;
  uint32_t ntex = 0, ntri = 0;
  float alto = 0;
  if (!u32(ntex) || !u32(ntri) || !f32(alto) || ntex > 256 || ntri > 200000) return false;
  std::vector<dbz3::pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  dbz3::ps2::Malla malla;
  malla.tris.resize(ntri);
  for (auto& t : malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      for (float& x : v)
        if (!f32(x)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.malla = std::move(malla);
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(dbz3::ps2::Crear(im, false));
  m.alto = alto;
  // Caja real en planta: la usa el encuadre para no recortar hombros ni brazos al girar.
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (const auto& t : m.malla.tris)
    for (const auto& p : t.p) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z),
                            z1 = std::max(z1, p.z);
  m.ancho = std::max(x1 - x0, z1 - z0);
  return true;
}

// Hilo de dibujo; mira el fichero como mucho una vez por segundo (se relee si se regenera al guardar).
Modelo3D* MallaClave(const std::string& clave, double ahora, double cada = 1.0) {
  Modelo3D& m = g_mallas[clave];
  if (ahora - m.visto >= cada) {
    m.visto = ahora;
    const fs::path f = Juego() / "ingame_ui" / "heroe_vista" / (clave + ".malla");
    std::error_code ec;
    const auto cuando = fs::last_write_time(f, ec);
    if (!ec && (!m || cuando != m.cuando)) {
      if (LeerMalla(f, m)) {
        m.cuando = cuando;
        REXLOG_INFO("dbz3 heroe: vista previa 3D {} cargada ({} triangulos, {} texturas, alto {:.2f})", clave,
                    m.malla.tris.size(), m.tx.size(), m.alto);
      } else {
        m.cuando = cuando;   // rota: no se reintenta hasta que cambie
        REXLOG_WARN("dbz3 heroe: vista previa 3D {}: {} no se puede leer", clave, f.string());
      }
    }
  }
  return m ? &m : nullptr;
}
Modelo3D* Malla3D(int id, int traje, double ahora) { return MallaClave(Num(id) + "_" + Num(traje), ahora); }

// Vista de la FICHA (studies 8 fase 2, H2): colores de Parte 1-4 y del pelo, peinado UT, logo y scouter, aplicados
// por vista_heroe.py --ficha a la malla del cuerpo. Clave = f_<hash de los valores>: cada combinacion su fichero.
// (los valores: H::ArgsFicha / H::FichaNativa, heroe.cpp: el mismo texto que ficha.txt del traje de combate)
std::string ClaveFicha(const std::string& args) {
  uint32_t x = 2166136261u;
  for (unsigned char c : args) x = (x ^ c) * 16777619u;
  char b[16];
  std::snprintf(b, sizeof(b), "f_%08x", x);
  return b;
}
std::string g_ficha_clave;
double g_ficha_cambio = 0;
// Malla de la ficha si ya esta; si no, la pide (0,4 s despues del ultimo cambio, para no lanzar una por pulsacion).
Modelo3D* MallaFicha(int id, int traje, const C::Heroe& h, double ahora) {
  (void)h;
  if (id >= 100 && id <= 105) return nullptr;   // G06a: tira 384/12 (la malla completa no cabe en el batch de ImGui)
  if (H::FichaNativa()) return nullptr;
  const std::string args = H::ArgsFicha(id, traje), clave = ClaveFicha(args);
  if (clave != g_ficha_clave) {
    g_ficha_clave = clave;
    g_ficha_cambio = ahora;
  }
  static std::string ultima;   // mientras se genera la nueva, la ultima de la ficha (sin parpadeo al cuerpo nativo)
  static int u_id = -1, u_traje = -1;
  Modelo3D* m = MallaClave(clave, ahora, 0.25);
  if (m) ultima = clave, u_id = id, u_traje = traje;
  if (!m && ahora - g_ficha_cambio > 0.4) {
    const fs::path f = Juego() / "ingame_ui" / "heroe_vista" / (clave + ".malla");
    std::error_code ec;
    // UT: VistaUT ya esta generando la misma ficha (PNG + .malla); no lanzar dos procesos.
    if (!fs::exists(f, ec) && !Generando2(clave) && !Generando2("ut_" + clave)) {
      REXLOG_INFO("dbz3 heroe: vista de la ficha {}: {}", clave, args);
      GenerarCon(clave, L"--ficha \"" + fs::path(args).wstring() + L"\" --clave " + fs::path(clave).wstring(), f);
    }
  }
  if (!m && !ultima.empty() && u_id == id && u_traje == traje) {
    auto it = g_mallas.find(ultima);
    if (it != g_mallas.end() && it->second) return &it->second;
  }
  return m;
}

bool CuerpoUT() { return H::Cuerpo() >= 100 && H::Cuerpo() <= 105; }

S::Pic VistaUT(int id, int traje, double ahora) {   // G06a: turntable del cuerpo completo, no una silueta sustituta
  const std::string args = H::ArgsFicha(id, traje), clave = ClaveFicha(args);
  Tira& t = g_tiras["ut_" + clave];
  if (ahora - t.visto < 1.0) return t.pic;
  t.visto = ahora;
  const fs::path f = Juego() / "ingame_ui" / "heroe_vista" / (clave + ".png");
  std::error_code ec;
  const auto cuando = fs::last_write_time(f, ec);
  if (ec) {
    if (!t.pic && !Generando2("ut_" + clave))
      GenerarCon("ut_" + clave, L"--ficha \"" + fs::u8path(args).wstring() + L"\" --clave " + fs::path(clave).wstring(), f);
    return t.pic;
  }
  if (t.pic && cuando == t.cuando) return t.pic;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return t.pic;
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const auto px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return t.pic;
  t.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  t.cuando = cuando;
  t.pic = t.tex ? S::Pic{reinterpret_cast<ImTextureID>(t.tex.get()), float(w), float(h)} : S::Pic{};
  return t.pic;
}

// Camara de la vista previa (H06): presets medidos sobre la caja real de la malla. 0 cuerpo, 1 tres cuartos (defecto),
// 2 busto, 3 retrato. El zoom se guarda aparte para no perderlo al cambiar de preset.
struct CamPreview {
  const char* nombre;
  float pitch, dist, fov, target;
};
constexpr CamPreview kCams[4] = {
    {"cuerpo", 4.0f, 2.12f, 0.55f, 0.50f},
    {"tres cuartos", 8.0f, 2.12f, 0.55f, 0.52f},
    {"busto", 6.0f, 1.40f, 0.50f, 0.72f},
    {"retrato", 6.0f, 1.00f, 0.22f, 0.90f},
};
int g_cam_preset = 1;
float g_cam_zoom = 1.0f;
float g_cam_yaw = 25.0f;   // grados; LB/RB y el giro automatico mueven el modelo, no la camara
// Pinta el modelo en la caja p0..p1 (px de pantalla), girado `giro` radianes. La camara orbita el centro de la caja:
// antes estaba fija a la altura del pecho (pitch 0), no se podia ver la coronilla y el cuerpo flotaba.
void Pintar3D(ImDrawList* dl, const Modelo3D& m, ImVec2 p0, ImVec2 p1, float giro) {
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_CAM")) g_cam_preset = std::clamp(std::atoi(e), 0, 3);
  const CamPreview& cp = kCams[std::clamp(g_cam_preset, 0, 3)];
  dbz3::ps2::Camara cam;
  cam.ok = true;
  cam.fov = cp.fov;
  cam.c = ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
  cam.h = ImVec2((p1.x - p0.x) * 0.5f, (p1.y - p0.y) * 0.5f);
  const float alto = std::max(m.alto, 0.01f);
  // El plano cercano del proyector es 1.0: no acercar la camara por debajo de 1.05 unidades.
  const float dist = std::max(cp.dist * alto * g_cam_zoom, 1.05f);
  const float ty = cp.target * alto;
  const float yaw = g_cam_yaw * 3.14159265f / 180.0f, pitch = cp.pitch * 3.14159265f / 180.0f;
  const dbz3::ps2::V3 ojo{std::sin(yaw) * std::cos(pitch) * dist, ty + std::sin(pitch) * dist,
                          std::cos(yaw) * std::cos(pitch) * dist};
  const dbz3::ps2::V3 obj{0, ty, 0};
  auto sub = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) { return dbz3::ps2::V3{a.x - b.x, a.y - b.y, a.z - b.z}; };
  auto dot = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  auto norm = [&](const dbz3::ps2::V3& a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? dbz3::ps2::V3{a.x / l, a.y / l, a.z / l} : dbz3::ps2::V3{0, 0, -1};
  };
  auto cross = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) {
    return dbz3::ps2::V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  };
  const dbz3::ps2::V3 fwd = norm(sub(obj, ojo));
  const dbz3::ps2::V3 der = norm(cross(fwd, dbz3::ps2::V3{0, 1, 0}));
  const dbz3::ps2::V3 arr = cross(der, fwd);
  cam.m[0] = der.x, cam.m[1] = arr.x, cam.m[2] = -fwd.x, cam.m[3] = 0;
  cam.m[4] = der.y, cam.m[5] = arr.y, cam.m[6] = -fwd.y, cam.m[7] = 0;
  cam.m[8] = der.z, cam.m[9] = arr.z, cam.m[10] = -fwd.z, cam.m[11] = 0;
  cam.m[12] = -dot(der, ojo), cam.m[13] = -dot(arr, ojo), cam.m[14] = dot(fwd, ojo), cam.m[15] = 1;
  dbz3::ps2::Escena::Toon toon;
  toon.borde = 2.0f / std::max(1.0f, cam.h.y / std::tan(cam.fov * 0.5f) / dist);
  static dbz3::ps2::Escena escena;
  escena.Empezar();
  escena.Poner(cam, m.malla, m.tx, dbz3::ps2::V3{0, 0, 0}, giro, 1.0f, toon);
  dl->PushClipRect(p0, p1, true);
  escena.Pintar(dl);
  dl->PopClipRect();
}

// ---------------------------------------------------------------------------------------------------------------------
// Piezas de dibujo comunes
// ---------------------------------------------------------------------------------------------------------------------
void Fondo(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, int alfa = 225) {
  dl->AddRectFilled(a.At(x0 + 10, y0 + 10), a.At(x1 - 10, y1 - 10), IM_COL32(10, 22, 52, alfa), 10 * a.s);
}
void Marco(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, int alfa = 225) {
  Fondo(dl, a, x0, y0, x1, y1, alfa);
  S::Frame(dl, a.At(x0, y0), a.At(x1, y1), a.s);
}
void Texto(ImDrawList* dl, const S::Area& a, float x, float y, float tam, const std::string& s, bool sel, bool off,
           float max_w = 0) {
  S::MenuText(dl, a.At(x, y), tam * a.s, s.c_str(), sel, off, max_w * a.s);
}
void Cara(ImDrawList* dl, const S::Area& a, int id, float x, float y, float h, int alfa = 255) {
  if (const S::Pic p = S::Img(("caras/" + Num(id)).c_str())) {
    const float w = h * p.w / std::max(p.h, 1.0f);
    S::DrawPic(dl, p, a.At(x - w * 0.5f, y - h * 0.5f), a.At(x + w * 0.5f, y + h * 0.5f), ImVec4(0, 0, 0, 0),
               IM_COL32(255, 255, 255, alfa));
  }
}
// Color de la muestra: h tono, s y v de -100..100 alrededor de la mitad [H] (los rangos de UT no estan medidos).
ImU32 ColorHsv(const C::Hsv& c, int alfa = 255) {
  if (c.h == 0 && c.s == 0 && c.v == 0) return IM_COL32(120, 120, 128, alfa);   // "Original": el color del traje
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(float(c.h % 360) / 360.0f, std::clamp(0.55f + c.s / 222.0f, 0.0f, 1.0f),
                              std::clamp(0.6f + c.v / 250.0f, 0.0f, 1.0f), r, g, b);
  return IM_COL32(int(r * 255), int(g * 255), int(b * 255), alfa);
}
void Muestra(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, ImU32 col) {
  dl->AddRectFilled(a.At(x0, y0), a.At(x1, y1), col, 4 * a.s);
  dl->AddRect(a.At(x0, y0), a.At(x1, y1), IM_COL32(20, 10, 0, 255), 4 * a.s, 0, 2.5f * a.s);
  dl->AddRect(a.At(x0 - 2, y0 - 2), a.At(x1 + 2, y1 + 2), IM_COL32(255, 190, 70, 255), 5 * a.s, 0, 1.5f * a.s);
}

// ---------------------------------------------------------------------------------------------------------------------
// Create / Edit
// ---------------------------------------------------------------------------------------------------------------------
bool g_edit = false;
int g_fila = 0, g_boton = 0, g_panel = -1, g_canal = 0;
C::Hsv g_panel_antes;
C::Heroe g_antes;
int g_traje_antes = 0;
float g_giro = 0;          // giro de la vista previa (en tiras: 0..N)
double g_giro_mano = -1e9;  // ultima vez que se giro a mano (el giro automatico espera 3 s)
double g_t_ant = 0;

const char* const kLogos[][2] = {{"Ninguno", "None"},           {"Tortuga (Kame)", "Turtle"},
                                 {"Rey Kaio", "King Kai"},       {"Capsule Corp.", "Capsule Corp."},
                                 {"Patrulla Roja", "Red Ribbon"}, {"Majin", "Majin"},
                                 {"Kaio-shin", "Supreme Kai"},   {"Patrulla Galáctica", "Galactic Patrol"}};
constexpr int kNumLogos = int(std::size(kLogos));
constexpr int kNumPelos = 10;   // pelos UT del catalogo (mod center hd/cac/piezas_cac.toml: hair_000_01..hair_008_01 + base)

int Filas() { return CuerpoUT() ? (g_edit ? 3 : 6) : (g_edit ? 4 : 8); }   // la ultima: la de los botones
int FilaBotones() { return Filas() - 1; }
C::Heroe& Ficha() { return C::SlotEditable(C::SlotActivo()); }
C::Hsv* ColorDeFila(int f) {
  C::Heroe& h = Ficha();
  if (!g_edit && f >= 1 && f <= 4) return &h.parte[f - 1];
  if (g_edit && f == 1) return &h.pelo_c1;
  if (g_edit && f == 2 && !CuerpoUT()) return &h.pelo_c2;
  return nullptr;
}
std::string NombreColor(int f) {
  if (!g_edit && CuerpoUT()) {
    switch (f) {
      case 1: return T::T("Parte de arriba", "Top");
      case 2: return T::T("Parte de abajo", "Bottoms");
      case 3: return T::T("Cinturón", "Belt");
      case 4: return T::T("Calzado", "Footwear");
    }
  }
  if (!g_edit) return std::string(T::T("Parte ", "Part ")) + Num(f);
  return f == 1 ? T::T("Color de pelo 1", "Hair color 1") : T::T("Color de pelo 2", "Hair color 2");
}
std::string HsvTxt(const C::Hsv& c) { return Num(c.h) + " / " + Num(c.s) + " / " + Num(c.v); }

void Restablecer() {
  C::Heroe& h = Ficha();
  if (g_edit) {
    h.pelo = 0;
    h.pelo_c1 = h.pelo_c2 = C::Hsv{};
  } else {
    H::PonerTraje(0);
    for (C::Hsv& c : h.parte) c = C::Hsv{};
    h.logo = h.scouter = 0;
  }
  REXLOG_INFO("dbz3 heroe: {}: reset (valores por defecto)", g_edit ? "Edit" : "Create");
}
void Descartar() {
  C::Heroe& h = Ficha();
  for (int i = 0; i < C::kNumPartes; ++i) h.parte[i] = g_antes.parte[i];
  h.logo = g_antes.logo, h.scouter = g_antes.scouter, h.pelo = g_antes.pelo;
  h.pelo_c1 = g_antes.pelo_c1, h.pelo_c2 = g_antes.pelo_c2;
  H::PonerTraje(g_traje_antes);
}

std::string Ayuda() {
  if (g_panel >= 0) {
    return g_canal == 0 ? T::T("Elige el tono.", "Select hue.")
         : g_canal == 1 ? T::T("Elige la saturación.", "Select saturation.")
                        : T::T("Elige el brillo.", "Select brightness.");
  }
  if (g_fila == FilaBotones()) {
    return g_boton == 0 ? T::T("Guarda los cambios en la ficha.", "Save the changes to the slot.")
         : g_boton == 1 ? T::T("Vuelve sin guardar los cambios.", "Go back without saving.")
                        : T::T("Deja los valores por defecto.", "Restore the default values.");
  }
  if (g_edit) {
    return g_fila == 0 ? T::T("Elige el peinado.", "Select a hairstyle.")
         : g_fila == 1 ? T::T("Elige el color base del pelo.", "Select the base hair color.")
                       : T::T("Elige el color propio del pelo.", "Select the custom hair color.");
  }
  switch (g_fila) {
    case 0: return T::T("Elige el traje.", "Select a costume.");
    case 5: return T::T("Elige la marca del logo.", "Select a logo mark.");
    case 6: return T::T("Lleva o no el rastreador.", "Wear the scouter or not.");
    default: return T::T("Elige el color de la parte (A: abrir).", "Select the part color (A: open).");
  }
}

// Barra de un canal del panel HSV: tono = arcoiris; saturacion = gris -> color; brillo = negro -> color -> blanco.
void Barra(ImDrawList* dl, const S::Area& a, float x0, float y, float x1, int canal, const C::Hsv& c) {
  const float h = 13;
  const ImVec2 p0 = a.At(x0, y - h), p1 = a.At(x1, y + h);
  if (canal == 0) {
    const int n = 12;
    for (int i = 0; i < n; ++i) {
      float r0, g0, b0, r1, g1, b1;
      ImGui::ColorConvertHSVtoRGB(float(i) / n, 0.85f, 0.95f, r0, g0, b0);
      ImGui::ColorConvertHSVtoRGB(float(i + 1) / n, 0.85f, 0.95f, r1, g1, b1);
      const ImU32 ca = IM_COL32(int(r0 * 255), int(g0 * 255), int(b0 * 255), 255);
      const ImU32 cb = IM_COL32(int(r1 * 255), int(g1 * 255), int(b1 * 255), 255);
      const float xa = p0.x + (p1.x - p0.x) * i / n, xb = p0.x + (p1.x - p0.x) * (i + 1) / n;
      dl->AddRectFilledMultiColor(ImVec2(xa, p0.y), ImVec2(xb + 0.5f, p1.y), ca, cb, cb, ca);
    }
  } else if (canal == 1) {
    C::Hsv g = c, f = c;
    g.s = -100, f.s = 100;
    dl->AddRectFilledMultiColor(p0, p1, ColorHsv(g), ColorHsv(f), ColorHsv(f), ColorHsv(g));
  } else {
    C::Hsv m = c;
    m.v = 0;
    const float xm = (p0.x + p1.x) * 0.5f;
    const ImU32 neg = IM_COL32(0, 0, 0, 255), bla = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilledMultiColor(p0, ImVec2(xm, p1.y), neg, ColorHsv(m), ColorHsv(m), neg);
    dl->AddRectFilledMultiColor(ImVec2(xm, p0.y), p1, ColorHsv(m), bla, bla, ColorHsv(m));
  }
  dl->AddRect(p0, p1, IM_COL32(20, 10, 0, 255), 0, 0, 2.5f * a.s);
  const float v = canal == 0 ? c.h / 360.0f : ((canal == 1 ? c.s : c.v) + 100) / 200.0f;
  const float xv = p0.x + (p1.x - p0.x) * std::clamp(v, 0.0f, 1.0f);
  dl->AddTriangleFilled(ImVec2(xv, p0.y + 2 * a.s), ImVec2(xv - 9 * a.s, p0.y - 12 * a.s), ImVec2(xv + 9 * a.s, p0.y - 12 * a.s),
                        IM_COL32(255, 255, 255, 255));
  dl->AddTriangle(ImVec2(xv, p0.y + 2 * a.s), ImVec2(xv - 9 * a.s, p0.y - 12 * a.s), ImVec2(xv + 9 * a.s, p0.y - 12 * a.s),
                  IM_COL32(20, 10, 0, 255), 2 * a.s);
  dl->AddLine(ImVec2(xv, p0.y), ImVec2(xv, p1.y), IM_COL32(255, 255, 255, 255), 2.5f * a.s);
}

// Columnas de la pantalla (px del area 1200x900)
constexpr float kPx0 = 40, kPx1 = 410, kPy0 = 172, kPy1 = 760;   // vista previa
constexpr float kLx0 = 440, kLx1 = 1130, kLy0 = 172;             // lista
constexpr float kPaso = 54;

void DibujarVista(ImDrawList* dl, const S::Area& a, float t) {
  Marco(dl, a, kPx0, kPy0, kPx1, kPy1, 235);
  const int id = H::Cuerpo(), traje = H::Traje();
  const bool ut = CuerpoUT();
  const float ix0 = kPx0 + 34, ix1 = kPx1 - 34, iy0 = kPy0 + 30, iy1 = kPy1 - 104;
  const Modelo3D* mf = ut ? nullptr : MallaFicha(id, traje, Ficha(), ImGui::GetTime());   // colores/pelo/logo de la ficha
  const Modelo3D* m3 = ut ? nullptr : (mf ? mf : Malla3D(id, traje, ImGui::GetTime()));
  const S::Pic p = m3 ? S::Pic{} : (ut ? VistaUT(id, traje, ImGui::GetTime()) : Vista(id, traje, ImGui::GetTime()));
  if (m3) {   // 3D en vivo
    const float cx = (ix0 + ix1) * 0.5f;
    if (g_cam_preset <= 1)   // cuerpo entero: sombra de contacto bajo los pies; en busto/retrato no hay suelo
      dl->AddEllipseFilled(a.At(cx, iy1 - 0.16f * (iy1 - iy0)), ImVec2(62 * a.s, 8 * a.s), IM_COL32(0, 0, 0, 110));
    static bool dicho = false;
    if (!dicho) {
      dicho = true;
      REXLOG_INFO("dbz3 heroe: vista previa 3D en vivo (ps2::Escena, {} triangulos)", m3->malla.tris.size());
    }
    Pintar3D(dl, *m3, a.At(ix0 - 20, iy0 - 10), a.At(ix1 + 20, iy1), g_giro * 6.2831853f / 8.0f);
  } else if (p) {
    const int giros = std::max(1, int(p.w / std::max(p.h, 1.0f) + 0.5f));
    const int k = (int(std::floor(g_giro)) % giros + giros) % giros;
    const float tw = p.w / giros;
    // recorte central de cada giro (el modelo de pie ocupa el centro): aspecto 0.6
    const float cw = tw * 0.6f, sx0 = tw * k + (tw - cw) * 0.5f;
    const float hh = iy1 - iy0, ww = std::min(ix1 - ix0, hh * cw / p.h);
    const float cx = (ix0 + ix1) * 0.5f;
    // suelo: sombra eliptica bajo los pies
    dl->AddEllipseFilled(a.At(cx, iy1 - 6), ImVec2(ww * 0.42f * a.s, 10 * a.s), IM_COL32(0, 0, 0, 110));
    S::DrawPic(dl, p, a.At(cx - ww * 0.5f, iy0), a.At(cx + ww * 0.5f, iy1), ImVec4(sx0, 0, sx0 + cw, p.h));
  } else {
    Cara(dl, a, id, (ix0 + ix1) * 0.5f, (iy0 + iy1) * 0.5f - 40, 160);
    Texto(dl, a, (ix0 + ix1) * 0.5f, (iy0 + iy1) * 0.5f + 90, D::kSmall,
          Generando(id, traje) ? T::T("Preparando la vista previa...", "Preparing the preview...")
                               : T::T("Vista previa: retrato", "Preview: portrait"),
          false, true, ix1 - ix0);
  }
  // nombre del heroe, cuerpo + traje y el giro
  Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 82, D::kRow, H::Nombre(), true, false, kPx1 - kPx0 - 60);
  Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 50, D::kSmall,
        H::NombreCuerpo() + "  -  " + T::T("Traje ", "Costume ") + Num(traje + 1) + "/" + Num(H::NumTrajes()), false, true,
        kPx1 - kPx0 - 60);
  // studies 8 fase 2 (H2): si el combate ya lleva los colores de la ficha (mira los ficheros 1 vez por segundo)
  static double visto = -1e9;
  static int st = 0;
  if (ImGui::GetTime() - visto > 1.0) {
    visto = ImGui::GetTime();
    H::TrajeCombate(&st);
  }
  if (st > 0)
    Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 22, D::kSmall * 0.85f,
          st == 1   ? T::T("En combate: con estos colores", "In battle: with these colors")
          : st == 2 ? T::T("En combate: al reiniciar el juego", "In battle: after restarting the game")
                    : T::T("En combate: al confirmar", "In battle: after confirming"),
          false, true, kPx1 - kPx0 - 60);
  (void)t;
}

void DibujarLista(ImDrawList* dl, const S::Area& a, float t) {
  C::Heroe& h = Ficha();
  const int n = Filas() - 1;
  const float y1 = kLy0 + 2 * D::kPad + kPaso * n;
  Marco(dl, a, kLx0, kLy0, kLx1, y1);
  const float xm = 700, xv = (xm + 40 + kLx1 - D::kLine) * 0.5f;   // etiqueta | valor
  for (int f = 0; f < n; ++f) {
    const float y = kLy0 + D::kPad + kPaso * (f + 0.5f);
    const bool sel = f == g_fila && g_panel < 0;
    if (sel) D::Stripe(dl, a, kLx0, y - kPaso * 0.5f + 2, kLx1, y + kPaso * 0.5f - 2, t);
    std::string et, val;
    bool flechas = true;
    if (const C::Hsv* c = ColorDeFila(f)) {
      et = NombreColor(f);
      val = c->h == 0 && c->s == 0 && c->v == 0 ? std::string(T::T("Original", "Original"))
                                                : std::string(T::T("Color propio", "Custom color"));
      flechas = false;
      Muestra(dl, a, xm + 40, y - 14, xm + 92, y + 14, ColorHsv(*c));
      Texto(dl, a, xv + 40, y, D::kRow, val, sel, false, kLx1 - xm - 160);
    } else {
      if (g_edit) {
        et = T::T("Pelo", "Hair");
        // studies 8 fase 2: el 1 es el pelo propio del cuerpo; 2..10 = piezas UT (vista_heroe.py buscar_pelo)
        val = h.pelo == 0 ? std::string(T::T("Peinado 1 (del cuerpo)", "Hairstyle 1 (body's own)")) + " / " + Num(kNumPelos)
                          : std::string(T::T("Peinado ", "Hairstyle ")) + Num(h.pelo + 1) + " (UT) / " + Num(kNumPelos);
      } else if (f == 0) {
        et = T::T("Traje", "Costume");
        val = std::string(T::T("Traje nativo ", "Native costume ")) + Num(H::Traje() + 1);
      } else if (f == 5) {
        et = T::T("Logo", "Logo mark");
        const int k = std::clamp(h.logo, 0, kNumLogos - 1);
        val = T::T(kLogos[k][0], kLogos[k][1]);
      } else {
        et = "Scouter";
        val = h.scouter ? "ON" : "OFF";
      }
      Texto(dl, a, xv, y, D::kRow, val, sel, false, kLx1 - xm - 120);
    }
    Texto(dl, a, (kLx0 + xm) * 0.5f + 10, y, D::kRow, et, sel, !sel, xm - kLx0 - 30);
    if (sel && flechas) S::Arrows(dl, a.At(xm + 20, y - kPaso * 0.5f), a.At(kLx1 - 14, y + kPaso * 0.5f), 32 * a.s, t);
  }
  // botones Confirmar / Atras / Reset
  const float by0 = y1 + 18, by1 = by0 + 62, bw = (kLx1 - kLx0 - 40) / 3;
  const char* nb[3] = {T::T("Confirmar", "Confirm"), T::T("Atrás", "Back"), T::T("Reset", "Reset")};
  for (int b = 0; b < 3; ++b) {
    const float x0 = kLx0 + (bw + 20) * b, x1 = x0 + bw;
    const bool sel = g_fila == FilaBotones() && g_boton == b && g_panel < 0;
    Marco(dl, a, x0, by0, x1, by1);
    if (sel) D::Stripe(dl, a, x0, by0 + 8, x1, by1 - 8, t);
    Texto(dl, a, (x0 + x1) * 0.5f, (by0 + by1) * 0.5f, D::kRow, nb[b], sel, !sel, bw - 40);
  }
  // linea de ayuda (UT: "Select brightness.")
  const float hy0 = by1 + 16, hy1 = hy0 + 58;
  Marco(dl, a, kLx0, hy0, kLx1, hy1, 200);
  Texto(dl, a, (kLx0 + kLx1) * 0.5f, (hy0 + hy1) * 0.5f, D::kSmall, Ayuda(), false, false, kLx1 - kLx0 - 70);
}

void DibujarPanel(ImDrawList* dl, const S::Area& a, float t) {
  C::Hsv* c = ColorDeFila(g_panel);
  if (!c) return;
  const int n = Filas() - 1;
  const float y1 = kLy0 + 2 * D::kPad + kPaso * n;
  Marco(dl, a, kLx0, kLy0, kLx1, y1, 245);
  const float cab = kLy0 + D::kPad + 30;
  Texto(dl, a, (kLx0 + kLx1) * 0.5f - 60, cab, D::kHead, NombreColor(g_panel) + ": " + T::T("color propio", "custom color"),
        true, false, kLx1 - kLx0 - 260);
  Muestra(dl, a, kLx1 - 150, cab - 22, kLx1 - 60, cab + 22, ColorHsv(*c));
  const char* nom[3] = {T::T("Tono", "Hue"), T::T("Saturación", "Saturation"), T::T("Brillo", "Brightness")};
  const int val[3] = {c->h, c->s, c->v};
  const float paso = std::min(90.0f, (y1 - cab - 60) / 3);
  for (int k = 0; k < 3; ++k) {
    const float y = cab + 56 + paso * (k + 0.5f);
    const bool sel = k == g_canal;
    if (sel) D::Stripe(dl, a, kLx0, y - paso * 0.5f + 4, kLx1, y + paso * 0.5f - 4, t);
    Texto(dl, a, kLx0 + 110, y, D::kRow, nom[k], sel, !sel, 160);
    Barra(dl, a, kLx0 + 210, y + 4, kLx1 - 140, k, *c);
    Texto(dl, a, kLx1 - 80, y, D::kRow, Num(val[k]), sel, false, 90);
    if (sel) S::Arrows(dl, a.At(kLx0 + 186, y - paso * 0.42f), a.At(kLx1 - 118, y + paso * 0.42f), 30 * a.s, t);
  }
}

}  // namespace

bool EnEdit() { return g_edit; }

void PedirVista(int id, int traje) {
  std::error_code ec;
  fs::path malla = VistaPng(id, traje);
  malla.replace_extension(".malla");
  if (!fs::exists(VistaPng(id, traje), ec) || !fs::exists(malla, ec)) Generar(id, traje);
}

// studies 9: monta mods/heroe_propio = el heroe de la ficha como PERSONAJE NUEVO (ID propio en el roster: nombre de la
// ficha, modelo con los colores, el logo y el peinado UT, moveset del estilo). El juego lo carga al arrancar (roster_build
// en JUGAR): hasta reiniciar, el combate sale como antes (estilo + cuerpo). Antes: traje extra heroe_fichaN (studies 8).
void PedirCombate() {
  const std::string args = H::ArgsPropio();
  const fs::path hecho = dbz3::ModsRoot() / "heroe_propio" / "ficha.txt";
  std::error_code ec;
  if (fs::exists(hecho, ec)) {
    std::ifstream f(hecho);
    std::string l;
    std::getline(f, l);
    while (!l.empty() && (l.back() == 13 || l.back() == ' ')) l.pop_back();
    if (l == args) return;   // ya esta hecho con estos valores
  }
  REXLOG_INFO("dbz3 heroe: el heroe propio de la ficha {}: se monta ({})", H::FichaNumero(), args);
  const std::string con_donante = args + ";donante=" + Num(H::DonanteCombate());
  GenerarCon("combate_" + ClaveFicha(args),
             // studies 9b: UTF-8 -> UTF-16 (fs::path(std::string) usa la pagina ANSI y rompe los acentos del nombre)
             L"--ficha \"" + fs::u8path(con_donante).wstring() + L"\" --combate " + fs::path(Num(H::FichaNumero())).wstring(),
             hecho);
}

void AbrirCrear(bool edit) {
  g_edit = edit;
  g_fila = g_boton = g_canal = 0;
  g_panel = -1;
  g_antes = Ficha();
  g_traje_antes = H::Traje();
  g_giro_mano = -1e9;
  PedirVista(H::Cuerpo(), H::Traje());
  REXLOG_INFO("dbz3 heroe: {} abierto (ficha {}, cuerpo {}, traje {}, logo {}, scouter {}, pelo {})", edit ? "Edit" : "Create",
              C::SlotActivo() + 1, H::Cuerpo(), H::Traje() + 1, g_antes.logo, g_antes.scouter, g_antes.pelo);
}

// Fondo del Modo Heroe otra vez encima del anfitrion (lo dibuja modos.cpp antes): estas pantallas usan toda el area,
// como las de UT, y la vista previa / la lista no deben tener al anfitrion asomando por detras.
static void FondoSinAnfitrion(ImDrawList* dl, const S::Area& a) {
  const char* fondo = S::Img("hub/fondo_heroe") ? "hub/fondo_heroe" : S::Img("hub/fondo") ? "hub/fondo" : "fondo_duelo";
  S::Backdrop(dl, a, fondo);
  S::Sides(dl, a);
}

void DibujarAvatarUT(ImDrawList* dl, const S::Area& a) {   // G06a: selector con el cuerpo propio
  FondoSinAnfitrion(dl, a);
  if (const auto p = VistaUT(H::Cuerpo(), H::Traje(), ImGui::GetTime())) {
    const float frame = p.h;   // fotogramas cuadrados: 12 giros
    S::DrawPic(dl, p, a.At(55, 210), a.At(440, 825), ImVec4(0, 0, frame, p.h));
  }
}

Accion Crear(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  C::Heroe& h = Ficha();
  FondoSinAnfitrion(dl, a);
  // giro de la vista previa: automatico (1,6 giros por segundo) salvo 3 s despues de girar a mano (LB/RB)
  const double ahora = ImGui::GetTime();
  const float dt = float(std::clamp(ahora - g_t_ant, 0.0, 0.1));
  g_t_ant = ahora;
  // H06: giro mas lento (30 grados/s), zoom con rueda, orbita con Q/E y reset con R; X cambia de preset.
  if (ahora - g_giro_mano > 3.0) g_giro += 0.667f * dt;
  if (p & (kLb | kRb)) {
    g_giro = std::floor(g_giro) + ((p & kRb) ? 1.0f : -1.0f) + 0.01f;
    g_giro_mano = ahora;
  }
  ImGuiIO& io = ImGui::GetIO();
  if (io.MouseWheel != 0.0f) {
    g_cam_zoom = std::clamp(g_cam_zoom * std::pow(1.10f, io.MouseWheel), 0.62f, 3.20f);
    REXLOG_INFO("dbz3 heroe: camara zoom {:.2f}", g_cam_zoom);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Q)) g_cam_yaw -= 5.0f;
  if (ImGui::IsKeyPressed(ImGuiKey_E)) g_cam_yaw += 5.0f;
  if (ImGui::IsKeyPressed(ImGuiKey_R)) {
    g_cam_preset = 1, g_cam_zoom = 1.0f, g_cam_yaw = 25.0f;
    REXLOG_INFO("dbz3 heroe: camara reset (tres cuartos)");
  }
  if (p & kX) {
    g_cam_preset = (g_cam_preset + 1) % 4;
    REXLOG_INFO("dbz3 heroe: camara preset={} ({}) zoom={:.2f}", g_cam_preset, kCams[g_cam_preset].nombre, g_cam_zoom);
  }
  if (g_panel >= 0) {   // panel HSV abierto: consume la entrada
    C::Hsv& c = *ColorDeFila(g_panel);
    if (p & kUp) g_canal = (g_canal + 2) % 3;
    if (p & kDown) g_canal = (g_canal + 1) % 3;
    const int d = ((p & kRight) ? 1 : 0) - ((p & kLeft) ? 1 : 0);
    if (d) {
      if (g_canal == 0) c.h = ((c.h + 10 * d) % 360 + 360) % 360;
      else if (g_canal == 1) c.s = std::clamp(c.s + 10 * d, -100, 100);
      else c.v = std::clamp(c.v + 10 * d, -100, 100);
    }
    if (p & kB) c = g_panel_antes;
    if (p & (kA | kB)) {
      C::ClampHsv(c);
      REXLOG_INFO("dbz3 heroe: {}: {} = ({}, {}, {}){}", g_edit ? "Edit" : "Create", NombreColor(g_panel), c.h, c.s, c.v,
                  (p & kB) ? " (sin cambios)" : "");
      g_panel = -1;
    }
  } else {
    const int n = Filas();
    if (p & kUp) g_fila = (g_fila + n - 1) % n;
    if (p & kDown) g_fila = (g_fila + 1) % n;
    const int d = ((p & kRight) ? 1 : 0) - ((p & kLeft) ? 1 : 0);
    if (d) {
      if (g_fila == FilaBotones()) {
        g_boton = (g_boton + d + 3) % 3;
      } else if (g_edit && g_fila == 0) {
        h.pelo = (h.pelo + d + kNumPelos) % kNumPelos;
      } else if (!g_edit && g_fila == 0) {
        const int nt = std::max(1, H::NumTrajes());
        H::PonerTraje((H::Traje() + d + nt) % nt);
        PedirVista(H::Cuerpo(), H::Traje());
        REXLOG_INFO("dbz3 heroe: Create: traje {} / {}", H::Traje() + 1, nt);
      } else if (!g_edit && g_fila == 5) {
        h.logo = (h.logo + d + kNumLogos) % kNumLogos;
      } else if (!g_edit && g_fila == 6) {
        h.scouter = !h.scouter;
      }
    }
    if (p & kY) {   // Create <-> Edit (UT: dos pantallas con la misma vista previa); se conserva lo cambiado
      g_edit = !g_edit;
      g_fila = g_boton = 0;
      REXLOG_INFO("dbz3 heroe: pasa a {}", g_edit ? "Edit" : "Create");
    }
    if (p & kB) {
      Descartar();
      REXLOG_INFO("dbz3 heroe: {}: atras (sin guardar)", g_edit ? "Edit" : "Create");
      return Accion::kVolver;
    }
    if (p & kA) {
      if (ColorDeFila(g_fila)) {
        g_panel = g_fila;
        g_canal = 0;
        g_panel_antes = *ColorDeFila(g_fila);
        REXLOG_INFO("dbz3 heroe: {}: panel de color de {}", g_edit ? "Edit" : "Create", NombreColor(g_fila));
      } else if (g_fila == FilaBotones()) {
        if (g_boton == 0) {
          REXLOG_INFO("dbz3 heroe: {}: confirmar (traje {}, partes {} | {} | {} | {}, logo {}, scouter {}, pelo {}, "
                      "colores de pelo {} | {})",
                      g_edit ? "Edit" : "Create", H::Traje() + 1, HsvTxt(h.parte[0]), HsvTxt(h.parte[1]),
                      HsvTxt(h.parte[2]), HsvTxt(h.parte[3]), h.logo, h.scouter, h.pelo, HsvTxt(h.pelo_c1),
                      HsvTxt(h.pelo_c2));
          PedirVista(H::Cuerpo(), H::Traje());   // la vista previa del traje guardado (si aun no esta)
          PedirCombate();                        // studies 8 fase 2 (H2): el traje de combate con los colores
          return Accion::kConfirmar;
        }
        if (g_boton == 1) {
          Descartar();
          REXLOG_INFO("dbz3 heroe: {}: atras (sin guardar)", g_edit ? "Edit" : "Create");
          return Accion::kVolver;
        }
        Restablecer();
      } else {
        g_fila = std::min(g_fila + 1, FilaBotones());
      }
    }
  }
  // --- dibujo ---
  S::Banner(dl, a, g_edit ? T::T("Editar héroe", "Edit hero") : T::T("Crear héroe", "Create hero"));
  // esquina: ficha y nivel
  {
    constexpr float c0 = D::kCornerY0, c1 = D::kTop - 24;
    Marco(dl, a, D::kCornerX0 + 40, c0, kLx1, c1);
    const std::string s = std::string(T::T("Ficha ", "Slot ")) + Num(C::SlotActivo() + 1) + "/" + Num(C::NumSlots()) +
                          "   " + H::Nombre();
    Texto(dl, a, (D::kCornerX0 + 40 + kLx1) * 0.5f, (c0 + c1) * 0.5f, D::kRow, s, true, false, kLx1 - D::kCornerX0 - 100);
  }
  DibujarVista(dl, a, t);
  DibujarLista(dl, a, t);
  if (g_panel >= 0) DibujarPanel(dl, a, t);
  if (g_panel >= 0) {
    S::HelpLine(dl, a.At(600, 868), 38 * a.s,
                {{"A", T::T("Aceptar", "OK")}, {"B", T::T("Cancelar", "Cancel")}});
  } else {
    S::HelpLine(dl, a.At(600, 868), 38 * a.s,
                {{"A", T::T("Elegir", "Select")},
                 {"B", T::T("Atrás", "Back")},
                 {"LB", T::T("Girar", "Rotate")},
                 {"RB", T::T("Girar", "Rotate")},
                 {"Y", g_edit ? T::T("Crear", "Create") : T::T("Editar (pelo)", "Edit (hair)")}});
  }
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Movesets (studies 8 fase 2): elegir de quien son los golpes sin salir del modo
// ---------------------------------------------------------------------------------------------------------------------
namespace {
std::vector<int> g_movs;
int g_mov_cur = 0, g_mov_scroll = 0, g_mov_antes = -1;
constexpr int kMovCols = 5, kMovFilas = 4;
}  // namespace

void AbrirMovesets() {
  g_movs = H::Movesets();
  g_mov_antes = H::MovesetId();
  g_mov_cur = 0;
  for (size_t i = 0; i < g_movs.size(); ++i)
    if (g_movs[i] == g_mov_antes) g_mov_cur = int(i);
  g_mov_scroll = std::max(0, g_mov_cur / kMovCols - kMovFilas + 1);
  REXLOG_INFO("dbz3 heroe: pantalla de movesets abierta ({} opciones, actual {}, estilo {})", g_movs.size(), g_mov_antes,
              H::EstiloId());
}

Accion MovesetsUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int n = std::max(1, int(g_movs.size()));
  if (p & kLeft) g_mov_cur = (g_mov_cur + n - 1) % n;
  if (p & kRight) g_mov_cur = (g_mov_cur + 1) % n;
  if (p & kUp) g_mov_cur = std::max(0, g_mov_cur - kMovCols);
  if (p & kDown) g_mov_cur = std::min(n - 1, g_mov_cur + kMovCols);
  const int fila = g_mov_cur / kMovCols;
  if (fila < g_mov_scroll) g_mov_scroll = fila;
  if (fila >= g_mov_scroll + kMovFilas) g_mov_scroll = fila - kMovFilas + 1;
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: movesets: atras (sigue {})", g_mov_antes);
    return Accion::kVolver;
  }
  if ((p & kA) && !g_movs.empty()) {
    H::PonerMoveset(g_movs[size_t(g_mov_cur)]);
    return Accion::kConfirmar;
  }
  FondoSinAnfitrion(dl, a);
  S::Banner(dl, a, T::T("Elige moveset", "Select moveset"));
  // rejilla (izquierda)
  constexpr float gx0 = 40, gx1 = 760, gy0 = 172, celda_w = (gx1 - gx0 - 2 * 24) / kMovCols, celda_h = 128;
  const float gy1 = gy0 + 2 * 24 + celda_h * kMovFilas;
  Marco(dl, a, gx0, gy0, gx1, gy1, 235);
  for (int k = 0; k < kMovFilas * kMovCols; ++k) {
    const int i = g_mov_scroll * kMovCols + k;
    if (i >= int(g_movs.size())) break;
    const int id = g_movs[size_t(i)];
    const float cx = gx0 + 24 + celda_w * (k % kMovCols + 0.5f), cy = gy0 + 24 + celda_h * (k / kMovCols + 0.5f);
    const bool sel = i == g_mov_cur, actual = id == g_mov_antes;
    if (sel) D::Stripe(dl, a, cx - celda_w * 0.5f + 2, cy - celda_h * 0.5f + 4, cx + celda_w * 0.5f - 2, cy + celda_h * 0.5f - 4, t);
    const int cara = id < 0 ? H::EstiloId() : id;
    Cara(dl, a, cara, cx, cy - 18, 64, sel || actual ? 255 : 170);
    Texto(dl, a, cx, cy + 34, 18, id < 0 ? std::string(T::T("Del estilo", "Style's own")) : M::Nombre(id), sel, !sel,
          celda_w - 12);
    if (actual) Texto(dl, a, cx, cy - 56, 15, T::T("ACTUAL", "CURRENT"), true, false, celda_w - 10);
  }
  if (g_mov_scroll > 0) S::Arrows(dl, a.At(gx0 + 300, gy0 - 30), a.At(gx0 + 420, gy0 + 10), 26 * a.s, t);
  // ficha del elegido (derecha)
  const int id = g_movs.empty() ? -1 : g_movs[size_t(g_mov_cur)];
  constexpr float px0 = 790, px1 = 1130, py0 = 172;
  const float py1 = gy1;
  Marco(dl, a, px0, py0, px1, py1, 235);
  const int cara = id < 0 ? H::EstiloId() : id;
  Cara(dl, a, cara, (px0 + px1) * 0.5f, py0 + 110, 130);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 200, D::kHead, id < 0 ? M::Nombre(H::EstiloId()) : M::Nombre(id), true, false,
        px1 - px0 - 60);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 240, D::kSmall,
        id < 0 ? std::string(T::T("(el moveset del estilo)", "(the style's own moveset)"))
               : std::string(T::T("Cápsulas propias: ", "Own capsules: ")) + Num(H::CapsulasMoveset(id)),
        false, false, px1 - px0 - 60);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 290, D::kSmall, T::T("Golpes, combos y cámara:", "Moves, combos and camera:"),
        false, true, px1 - px0 - 60);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 320, D::kSmall, T::T("los de este luchador.", "this fighter's."), false, true,
        px1 - px0 - 60);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 360, D::kSmall, T::T("Vida, daño y formas:", "Health, damage and forms:"), false,
        true, px1 - px0 - 60);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 390, D::kSmall,
        std::string(T::T("los del estilo (", "the style's (")) + M::Nombre(H::EstiloId()) + ").", false, true,
        px1 - px0 - 60);
  Marco(dl, a, gx0, gy1 + 16, px1, gy1 + 74, 200);
  Texto(dl, a, (gx0 + px1) * 0.5f, gy1 + 45, D::kSmall,
        std::string(Num(g_mov_cur + 1)) + " / " + Num(n) + "   " +
            T::T("Se aplica en el combate siguiente.", "Applies from the next fight."),
        false, false, px1 - gx0 - 70);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s, {{"A", T::T("Elegir", "Select")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fichas de heroe (K): lista de cac.toml, duplicar (X) y archivar (Y). A elige y vuelve a la creacion.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_slot_cur = 0;
}
void AbrirSlots() {
  g_slot_cur = C::SlotActivo();
  REXLOG_INFO("dbz3 heroe: fichas abiertas ({} activas, {} archivadas)", C::NumSlots(), C::NumArchivados());
}
Accion SlotsUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int n = std::max(1, C::NumSlots());
  g_slot_cur = std::clamp(g_slot_cur, 0, n - 1);
  if (p & kLeft) g_slot_cur = (g_slot_cur + n - 1) % n;
  if (p & kRight) g_slot_cur = (g_slot_cur + 1) % n;
  if (p & kUp) g_slot_cur = std::max(0, g_slot_cur - 4);
  if (p & kDown) g_slot_cur = std::min(n - 1, g_slot_cur + 4);
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: fichas: atras");
    return Accion::kVolver;
  }
  if (p & kX) {
    if (C::NumSlots() >= C::kMaxSlots) {
      REXLOG_WARN("dbz3 heroe: fichas: maximo {} fichas", C::kMaxSlots);
    } else {
      C::DuplicarSlot(g_slot_cur);
      g_slot_cur = C::NumSlots() - 1;
      REXLOG_INFO("dbz3 heroe: ficha duplicada: ahora {} fichas (activa {})", C::NumSlots(), g_slot_cur + 1);
    }
  }
  if (p & kY) {
    if (C::NumSlots() <= 1) {
      REXLOG_WARN("dbz3 heroe: fichas: no se puede archivar la unica ficha");
    } else {
      C::ArchivarSlot(g_slot_cur);
      g_slot_cur = std::clamp(g_slot_cur, 0, C::NumSlots() - 1);
      REXLOG_INFO("dbz3 heroe: ficha archivada: quedan {} activas, {} archivadas", C::NumSlots(), C::NumArchivados());
    }
  }
  if (p & kA) {
    C::ElegirSlot(g_slot_cur);
    REXLOG_INFO("dbz3 heroe: ficha elegida {}/{}", g_slot_cur + 1, C::NumSlots());
    return Accion::kConfirmar;
  }
  FondoSinAnfitrion(dl, a);
  S::Banner(dl, a, T::T("Fichas de héroe", "Hero slots"));
  constexpr float gx0 = 40, gx1 = 1130, gy0 = 172, cw = (gx1 - gx0 - 3 * 18) / 4, ch = 250;
  for (int i = 0; i < C::kMaxSlots; ++i) {
    const float x0 = gx0 + (i % 4) * (cw + 18), y0 = gy0 + (i / 4) * (ch + 18);
    const float x1 = x0 + cw, y1 = y0 + ch;
    if (i >= n) {
      Marco(dl, a, x0, y0, x1, y1, 90);
      Texto(dl, a, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, D::kRow, T::T("(vacía)", "(empty)"), false, true, cw - 20);
      continue;
    }
    const bool sel = i == g_slot_cur, act = i == C::SlotActivo();
    Marco(dl, a, x0, y0, x1, y1, sel ? 235 : 200);
    if (sel) D::Stripe(dl, a, x0 + 2, y0 + 4, x1 - 2, y1 - 4, t);
    const C::Heroe& h = C::Slot(i);
    Cara(dl, a, h.cuerpo, (x0 + x1) * 0.5f, y0 + 92, 110, sel || act ? 255 : 180);
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 168, D::kRow, h.nombre, sel, !sel, cw - 20);
    const std::string det = std::string(T::T("Nv ", "Lv ")) + Num(h.nivel) + "  ·  " + M::Nombre(h.cuerpo) + "  ·  " +
                            M::Nombre(h.estilo);
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 202, D::kSmall, det, false, true, cw - 20);
    if (act) Texto(dl, a, (x0 + x1) * 0.5f, y0 + 228, D::kSmall, T::T("ACTIVA", "CURRENT"), true, false, cw - 20);
  }
  Texto(dl, a, 600, 738, D::kSmall,
        std::string(T::T("Fichas: ", "Slots: ")) + Num(C::NumSlots()) + "/" + Num(C::kMaxSlots) + "   " +
            T::T("Archivadas: ", "Archived: ") + Num(C::NumArchivados()),
        false, false);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Elegir", "Select")}, {"X", T::T("Duplicar", "Duplicate")},
               {"Y", T::T("Archivar", "Archive")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mapa del mundo (studies 8 fase 2, H5): las sagas como puntos de un mapa propio (dibujado por codigo, sin arte), el
// heroe en 3D (la malla de su ficha, ps2::Escena) vuela de un punto a otro y el radar cuenta las Esferas (una por saga
// completada). A en la saga siguiente la empieza. Es la "alternativa barata" de heroe_pasos.md H5 (no el mapa de DU).
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_mapa_cur = 0;
float g_mapa_x = 0, g_mapa_y = 0;   // posicion del heroe (px del area), va hacia el punto del cursor
double g_mapa_t = 0;
std::string g_mapa_aviso;
constexpr float kMx0 = 40, kMx1 = 820, kMy0 = 172, kMy1 = 760;
// Puntos de las sagas en el mapa (fraccion del panel) [H]: un camino de oeste a este, la ultima en el centro.
constexpr float kPuntos[][2] = {{0.12f, 0.70f}, {0.26f, 0.42f}, {0.38f, 0.72f}, {0.50f, 0.36f}, {0.62f, 0.66f},
                                {0.74f, 0.30f}, {0.86f, 0.58f}, {0.55f, 0.15f}, {0.30f, 0.20f}, {0.80f, 0.85f}};
ImVec2 Punto(int i) {
  const int n = int(std::size(kPuntos));
  const float* p = kPuntos[std::clamp(i, 0, n - 1)];
  return ImVec2(kMx0 + 30 + (kMx1 - kMx0 - 60) * p[0], kMy0 + 30 + (kMy1 - kMy0 - 60) * p[1]);
}
void Tierra(ImDrawList* dl, const S::Area& a) {   // mar, islas y montes (formas fijas) [H]
  dl->AddRectFilledMultiColor(a.At(kMx0 + 12, kMy0 + 12), a.At(kMx1 - 12, kMy1 - 12), IM_COL32(30, 90, 170, 255),
                              IM_COL32(30, 90, 170, 255), IM_COL32(10, 40, 110, 255), IM_COL32(10, 40, 110, 255));
  static const float kIslas[][4] = {{0.20f, 0.60f, 0.20f, 0.30f}, {0.45f, 0.50f, 0.25f, 0.34f},
                                    {0.75f, 0.50f, 0.22f, 0.36f}, {0.40f, 0.18f, 0.24f, 0.13f},
                                    {0.82f, 0.86f, 0.10f, 0.08f}, {0.08f, 0.25f, 0.06f, 0.10f}};
  const float w = kMx1 - kMx0 - 60, h = kMy1 - kMy0 - 60;
  for (const auto& i : kIslas) {
    const ImVec2 c = a.At(kMx0 + 30 + w * i[0], kMy0 + 30 + h * i[1]);
    const ImVec2 r(w * i[2] * 0.5f * a.s, h * i[3] * 0.5f * a.s);
    dl->AddEllipseFilled(c, ImVec2(r.x + 6 * a.s, r.y + 6 * a.s), IM_COL32(225, 205, 140, 255), 0, 40);   // playa
    dl->AddEllipseFilled(c, r, IM_COL32(70, 150, 60, 255), 0, 40);
    dl->AddEllipseFilled(ImVec2(c.x - r.x * 0.2f, c.y - r.y * 0.15f), ImVec2(r.x * 0.5f, r.y * 0.45f),
                         IM_COL32(95, 170, 75, 255), 0, 30);
    for (int m = 0; m < 3; ++m) {   // montes
      const float mx = c.x + r.x * (-0.4f + 0.4f * m), my = c.y + r.y * (m == 1 ? -0.1f : 0.25f), s = 14 * a.s;
      dl->AddTriangleFilled(ImVec2(mx, my - s), ImVec2(mx - s, my + s * 0.6f), ImVec2(mx + s, my + s * 0.6f),
                            IM_COL32(130, 105, 70, 255));
      dl->AddTriangleFilled(ImVec2(mx, my - s), ImVec2(mx - s * 0.35f, my - s * 0.4f), ImVec2(mx + s * 0.35f, my - s * 0.4f),
                            IM_COL32(245, 245, 250, 255));
    }
  }
}
void Esfera(ImDrawList* dl, ImVec2 c, float r, int estrellas, int alfa = 255) {
  dl->AddCircleFilled(c, r, IM_COL32(250, 150, 30, alfa), 20);
  dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.35f, IM_COL32(255, 230, 170, alfa), 12);
  dl->AddCircle(c, r, IM_COL32(120, 50, 0, alfa), 20, 1.5f);
  for (int k = 0; k < estrellas; ++k) {
    const float ang = 6.2831853f * k / std::max(estrellas, 1), d = estrellas > 1 ? r * 0.38f : 0;
    dl->AddCircleFilled(ImVec2(c.x + std::cos(ang) * d, c.y + std::sin(ang) * d), r * 0.16f, IM_COL32(200, 20, 20, alfa), 8);
  }
}
}  // namespace

void AbrirMapa() {
  g_mapa_cur = std::clamp(H::Saga(), 0, std::max(0, H::SagasTotal() - 1));
  const ImVec2 p = Punto(g_mapa_cur);
  g_mapa_x = p.x, g_mapa_y = p.y;
  g_mapa_t = 0;
  g_mapa_aviso.clear();
  REXLOG_INFO("dbz3 heroe: mapa abierto ({} sagas, completadas {}, la siguiente {})", H::SagasTotal(),
              std::min(H::Saga(), H::SagasTotal()), H::Saga() + 1);
}

Accion MapaUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int total = std::max(1, std::min(H::SagasTotal(), int(std::size(kPuntos))));
  const int sig = std::min(H::Saga(), total - 1), hechas = std::min(H::Saga(), total);
  const int antes = g_mapa_cur;
  if (p & (kRight | kDown)) g_mapa_cur = std::min(g_mapa_cur + 1, sig);   // hasta la siguiente: las demas, bloqueadas
  if (p & (kLeft | kUp)) g_mapa_cur = std::max(g_mapa_cur - 1, 0);
  if (g_mapa_cur != antes) {
    g_mapa_aviso.clear();
    REXLOG_INFO("dbz3 heroe: mapa: saga {} ({})", g_mapa_cur + 1, H::NombreSaga(g_mapa_cur));
  }
  if (p & kB) return Accion::kVolver;
  if (p & kA) {
    if (g_mapa_cur == H::Saga()) {
      REXLOG_INFO("dbz3 heroe: mapa: empezar la saga {} ({})", g_mapa_cur + 1, H::NombreSaga(g_mapa_cur));
      return Accion::kConfirmar;
    }
    g_mapa_aviso = T::T("Saga completada: ya tienes su Esfera.", "Saga cleared: you already have its Dragon Ball.");
    REXLOG_INFO("dbz3 heroe: mapa: la saga {} ya esta completada", g_mapa_cur + 1);
  }
  FondoSinAnfitrion(dl, a);
  S::Banner(dl, a, T::T("Mapa del mundo", "World map"));
  Marco(dl, a, kMx0, kMy0, kMx1, kMy1, 235);
  Tierra(dl, a);
  // camino entre sagas (completado: dorado; por hacer: discontinuo)
  for (int i = 0; i + 1 < total; ++i) {
    const ImVec2 p0 = a.At(Punto(i).x, Punto(i).y), p1 = a.At(Punto(i + 1).x, Punto(i + 1).y);
    if (i < hechas) {
      dl->AddLine(p0, p1, IM_COL32(255, 210, 80, 255), 4 * a.s);
    } else {
      for (int k = 0; k < 12; k += 2)
        dl->AddLine(ImVec2(p0.x + (p1.x - p0.x) * k / 12, p0.y + (p1.y - p0.y) * k / 12),
                    ImVec2(p0.x + (p1.x - p0.x) * (k + 1) / 12, p0.y + (p1.y - p0.y) * (k + 1) / 12),
                    IM_COL32(230, 230, 240, 160), 3 * a.s);
    }
  }
  for (int i = 0; i < total; ++i) {
    const ImVec2 c = a.At(Punto(i).x, Punto(i).y);
    const bool hecha = i < hechas, siguiente = i == H::Saga(), sel = i == g_mapa_cur;
    if (sel) dl->AddCircle(c, (26 + 4 * std::sin(t * 6)) * a.s, IM_COL32(255, 255, 255, 230), 32, 3 * a.s);
    if (hecha) {
      Esfera(dl, c, 15 * a.s, i % 7 + 1);
    } else {
      dl->AddCircleFilled(c, 13 * a.s, siguiente ? IM_COL32(220, 40, 40, 255) : IM_COL32(90, 90, 100, 230), 20);
      dl->AddCircle(c, 13 * a.s, IM_COL32(20, 10, 0, 255), 20, 2 * a.s);
    }
    Texto(dl, a, Punto(i).x, Punto(i).y + 30, D::kSmall * 0.8f, Num(i + 1), sel, !sel && !siguiente && !hecha, 60);
  }
  // el heroe vuela hacia el punto del cursor (3D, la malla de su ficha)
  const double ahora = ImGui::GetTime();
  const float dt = g_mapa_t > 0 ? float(std::min(ahora - g_mapa_t, 0.1)) : 0.0f;
  g_mapa_t = ahora;
  const ImVec2 dest = Punto(g_mapa_cur);
  const float k = 1.0f - std::exp(-4.0f * dt);
  const float dx = dest.x - g_mapa_x;
  g_mapa_x += (dest.x - g_mapa_x) * k;
  g_mapa_y += (dest.y - g_mapa_y) * k;
  const bool vuela = std::abs(dest.x - g_mapa_x) + std::abs(dest.y - g_mapa_y) > 3;
  const float alto = vuela ? 26.0f : 8.0f + 4.0f * std::sin(t * 3);
  const Modelo3D* mf = MallaFicha(H::Cuerpo(), H::Traje(), Ficha(), ahora);
  if (const Modelo3D* m3 = CuerpoUT() ? nullptr : (mf ? mf : Malla3D(H::Cuerpo(), H::Traje(), ahora))) {
    dl->AddEllipseFilled(a.At(g_mapa_x, g_mapa_y - 4), ImVec2(22 * a.s, 6 * a.s), IM_COL32(0, 0, 0, 90));
    const float giro = vuela ? (dx >= 0 ? 1.5708f : -1.5708f) : 0.0f;
    Pintar3D(dl, *m3, a.At(g_mapa_x - 60, g_mapa_y - 150 - alto), a.At(g_mapa_x + 60, g_mapa_y - alto), giro);
  } else if (CuerpoUT()) {
    if (const auto p = VistaUT(H::Cuerpo(), H::Traje(), ahora)) {
      const float frame = p.h;
      const float kf = vuela ? (dx > 0 ? 3.0f : 9.0f) : 0.0f;   // 12 giros: 90/270 grados
      S::DrawPic(dl, p, a.At(g_mapa_x - 55, g_mapa_y - 130 - alto), a.At(g_mapa_x + 55, g_mapa_y - alto),
                 ImVec4(kf * frame, 0, (kf + 1) * frame, p.h));
    }
  } else {
    Cara(dl, a, H::Cuerpo(), g_mapa_x, g_mapa_y - 40 - alto, 64);
  }
  // radar (derecha): una Esfera por saga completada
  constexpr float rx0 = 840, rx1 = 1160;
  Marco(dl, a, rx0, kMy0, rx1, kMy1, 235);
  const ImVec2 rc = a.At((rx0 + rx1) * 0.5f, kMy0 + 130);
  const float rr = 95 * a.s;
  dl->AddCircleFilled(rc, rr, IM_COL32(20, 70, 30, 255), 48);
  for (int g = 1; g <= 3; ++g) dl->AddCircle(rc, rr * g / 3, IM_COL32(90, 220, 110, 160), 48, 1.5f * a.s);
  dl->AddLine(ImVec2(rc.x - rr, rc.y), ImVec2(rc.x + rr, rc.y), IM_COL32(90, 220, 110, 160), 1.5f * a.s);
  dl->AddLine(ImVec2(rc.x, rc.y - rr), ImVec2(rc.x, rc.y + rr), IM_COL32(90, 220, 110, 160), 1.5f * a.s);
  const float barrido = t * 2.0f;
  dl->AddLine(rc, ImVec2(rc.x + std::cos(barrido) * rr, rc.y + std::sin(barrido) * rr), IM_COL32(180, 255, 190, 220),
              2.5f * a.s);
  const int esferas = std::min(hechas, 7);
  for (int i = 0; i < 7; ++i) {   // recogidas: en el centro; por recoger: puntos que parpadean donde estan sus sagas
    const float ang = 6.2831853f * i / 7 - 1.5708f;
    if (i < esferas) {
      Esfera(dl, ImVec2(rc.x + std::cos(ang) * rr * 0.32f, rc.y + std::sin(ang) * rr * 0.32f), 9 * a.s, i + 1);
    } else if (std::fmod(t * 2.0f + i * 0.3f, 2.0f) < 1.3f) {
      dl->AddCircleFilled(ImVec2(rc.x + std::cos(ang * 1.7f) * rr * 0.8f, rc.y + std::sin(ang * 1.7f) * rr * 0.8f),
                          5 * a.s, IM_COL32(255, 200, 60, 255), 10);
    }
  }
  Texto(dl, a, (rx0 + rx1) * 0.5f, kMy0 + 250, D::kSmall, std::string(T::T("Esferas: ", "Dragon Balls: ")) +
        Num(esferas) + " / 7", true, false, rx1 - rx0 - 40);
  // ficha de la saga del cursor
  const int s = g_mapa_cur;
  float y = kMy0 + 300;
  Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kRow, H::NombreSaga(s), true, false, rx1 - rx0 - 40);
  y += 40;
  Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kSmall,
        s < hechas ? T::T("Completada", "Cleared") : T::T("Siguiente: A para empezar", "Next: A to start"), false,
        s < hechas, rx1 - rx0 - 40);
  y += 38;
  const auto combates = H::CombatesDeSaga(s);
  for (const auto& [rival, esc] : combates) {
    if (y > kMy1 - 70) break;
    Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kSmall * 0.85f,
          (rival >= 0 ? M::Nombre(rival) : std::string("?")) + "  -  " + dbz3::escenarios::Nombre(esc), false, true,
          rx1 - rx0 - 40);
    y += 28;
  }
  if (const int m = H::MaestroQueEntrena(s); m >= 0 && y <= kMy1 - 40)
    Texto(dl, a, (rx0 + rx1) * 0.5f, y + 6, D::kSmall * 0.85f, std::string(T::T("Maestro: ", "Master: ")) + M::Nombre(m),
          false, false, rx1 - rx0 - 40);
  if (!g_mapa_aviso.empty())
    Texto(dl, a, (kMx0 + kMx1) * 0.5f, kMy1 + 40, D::kSmall, g_mapa_aviso, true, false, kMx1 - kMx0);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Empezar", "Start")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Select Master (H4)
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_mcur = 0;
std::string g_maviso;
std::string Pct(int v) { return (v > 0 ? "+" : "") + Num(v) + "%"; }
}  // namespace

void AbrirMaestros() {
  g_mcur = std::max(0, H::MaestroActual());
  g_maviso.clear();
  const int act = H::MaestroActual();
  REXLOG_INFO("dbz3 heroe: Select Master abierto (maestro actual {}, entrenamiento {}%)", act,
              act >= 0 ? H::Maestro(act).entreno : 0);
}

Accion Maestros(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  constexpr int n = H::kMaestros;
  if (p & (kUp | kDown)) g_maviso.clear();
  if (p & kUp) g_mcur = (g_mcur + n - 1) % n;
  if (p & kDown) g_mcur = (g_mcur + 1) % n;
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: Select Master: atras");
    return Accion::kVolver;
  }
  if (p & kA) {
    if (H::ElegirMaestro(g_mcur)) {
      return Accion::kConfirmar;
    }
    if (const H::InfoMaestro im = H::Maestro(g_mcur); !im.desbloqueado && im.saga >= 0)
      g_maviso = std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(im.saga);
    else g_maviso = std::string(T::T("Para cambiar de maestro hace falta un ", "To switch master you need ")) +
               Num(H::EntrenoMinimo()) + T::T("% de entrenamiento con el actual.", "% training with the current one.");
  }
  const int act = H::MaestroActual();
  FondoSinAnfitrion(dl, a);
  S::Banner(dl, a, T::T("Elige maestro", "Select Master"));
  // lista de maestros con su retrato (izquierda)
  constexpr float lx0 = 40, lx1 = 470, ly0 = 172, paso = 76;
  const float ly1 = ly0 + 2 * D::kPad + paso * n;
  Marco(dl, a, lx0, ly0, lx1, ly1, 235);
  for (int i = 0; i < n; ++i) {
    const H::InfoMaestro m = H::Maestro(i);
    const float y = ly0 + D::kPad + paso * (i + 0.5f);
    const bool sel = i == g_mcur;
    if (sel) D::Stripe(dl, a, lx0, y - paso * 0.5f + 3, lx1, y + paso * 0.5f - 3, t);
    Cara(dl, a, m.id, lx0 + 78, y, 60, !m.desbloqueado ? 60 : sel ? 255 : 190);   // s9: bloqueado, en sombra
    Texto(dl, a, lx0 + 230, y - 12, D::kRow, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), sel, !sel, 190);
    // barra fina de entrenamiento bajo el nombre
    const float bx0 = lx0 + 140, bx1 = lx0 + 320, by = y + 20;
    dl->AddRectFilled(a.At(bx0, by - 5), a.At(bx1, by + 5), IM_COL32(0, 0, 0, 160), 3 * a.s);
    dl->AddRectFilled(a.At(bx0, by - 5), a.At(bx0 + (bx1 - bx0) * m.entreno / 100.0f, by + 5),
                      m.entrenado ? IM_COL32(255, 200, 40, 255) : IM_COL32(60, 200, 255, 255), 3 * a.s);
    Texto(dl, a, lx1 - 70, y + (i == act ? 10 : 0), D::kSmall, Num(m.entreno) + "%", sel, !sel, 80);
    if (i == act) Texto(dl, a, lx1 - 70, y - 22, 16, T::T("ACTUAL", "CURRENT"), true, false, 90);
  }
  // panel MASTER SKILL (derecha)
  const H::InfoMaestro m = H::Maestro(g_mcur);
  constexpr float px0 = 500, px1 = 1130, py0 = 172, py1 = 700;
  Marco(dl, a, px0, py0, px1, py1, 235);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 44, D::kHead, "MASTER SKILL", true, false, px1 - px0 - 80);
  Cara(dl, a, m.id, px0 + 110, py0 + 140, 120, m.desbloqueado ? 255 : 60);   // s9: bloqueado, sin desvelarlo
  Texto(dl, a, px0 + 380, py0 + 104, D::kHead, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), true, false, 380);
  Texto(dl, a, px0 + 380, py0 + 146, D::kSmall,
        std::string(T::T("Entrenamiento completado: ", "Training Completion: ")) + Num(m.entreno) + "%", false, false, 400);
  {   // barra grande de entrenamiento con la marca del 60 %
    const float bx0 = px0 + 200, bx1 = px1 - 50, by = py0 + 186;
    dl->AddRectFilled(a.At(bx0, by - 9), a.At(bx1, by + 9), IM_COL32(0, 0, 0, 180), 4 * a.s);
    dl->AddRectFilled(a.At(bx0, by - 9), a.At(bx0 + (bx1 - bx0) * m.entreno / 100.0f, by + 9),
                      m.entrenado ? IM_COL32(255, 200, 40, 255) : IM_COL32(60, 200, 255, 255), 4 * a.s);
    const float x60 = bx0 + (bx1 - bx0) * H::EntrenoMinimo() / 100.0f;
    dl->AddLine(a.At(x60, by - 14), a.At(x60, by + 14), IM_COL32(255, 80, 60, 255), 3 * a.s);
    dl->AddRect(a.At(bx0, by - 9), a.At(bx1, by + 9), IM_COL32(255, 190, 70, 255), 4 * a.s, 0, 1.5f * a.s);
  }
  // modificadores (los de heroe.cpp kEstilos: los aplica heroe::Reglas)
  std::vector<std::pair<std::string, bool>> mods;   // texto, positivo
  if (m.vida) mods.push_back({std::string(T::T("Vida ", "Health ")) + (m.vida > 0 ? "+" : "") + Num(m.vida), m.vida > 0});
  if (m.dano != 100) mods.push_back({std::string(T::T("Ataque ", "Attack ")) + Pct(m.dano - 100), m.dano > 100});
  if (m.recibido != 100)
    mods.push_back({std::string(T::T("Daño recibido ", "Damage taken ")) + Pct(m.recibido - 100), m.recibido < 100});
  if (m.ki < 0) mods.push_back({T::T("El ki se recarga solo", "Ki recharges by itself"), true});
  float y = py0 + 236;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kRow, T::T("Modificadores", "Modifiers"), true, false, px1 - px0 - 80);
  y += 38;
  for (const auto& [s, bueno] : mods) {
    Texto(dl, a, (px0 + px1) * 0.5f, y, D::kSmall, s, bueno, !bueno, px1 - px0 - 80);
    y += 32;
  }
  // tecnicas y capsula que desbloquea
  y += 10;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kRow, T::T("Técnicas", "Techniques"), true, false, px1 - px0 - 80);
  y += 36;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kSmall, m.tecnicas, false, false, px1 - px0 - 80);
  y += 40;
  if (const S::Pic cap = S::Img("capsulas")) {   // botella de capsula de la Skill Shop (data del juego)
    const float h = 50 * a.s, w = h * 31 / 83;
    const ImVec2 c = a.At(px0 + 90, y);
    S::DrawPic(dl, cap, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f),
               ImVec4(3, 7, 34, 90), m.entrenado ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
  }
  Texto(dl, a, (px0 + px1) * 0.5f + 40, y, D::kSmall,
        std::string(T::T("Cápsula ", "Capsule ")) + m.capsula_n + ": " +
            (m.entrenado ? T::T("desbloqueada", "unlocked") : T::T("se desbloquea al 100 %", "unlocks at 100%")),
        m.entrenado, !m.entrenado, px1 - px0 - 200);
  // regla del 60 % (UT) o el aviso
  const bool puede = H::PuedeCambiarA(g_mcur);
  if (!m.desbloqueado && g_maviso.empty() && m.saga >= 0)   // s9: maestro aun no desbloqueado
    g_maviso = std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(m.saga);
  std::string regla = g_maviso.empty()
                          ? std::string(T::T("Para cambiar de maestro hace falta un ", "To switch master you need ")) +
                                Num(H::EntrenoMinimo()) + T::T("% con el actual.", "% with the current one.")
                          : g_maviso;
  Marco(dl, a, px0, py1 + 16, px1, py1 + 74, 200);
  Texto(dl, a, (px0 + px1) * 0.5f, py1 + 45, D::kSmall, regla, !puede && !g_maviso.empty(), false, px1 - px0 - 70);
  // capsulas desbloqueadas de la ficha (cac.toml tecnicas[])
  {
    const C::Heroe& h = C::Slot(C::SlotActivo());
    std::string s = T::T("Desbloqueadas: ", "Unlocked: ");
    int k = 0;
    for (int i = 0; i < n; ++i) {
      const H::InfoMaestro x = H::Maestro(i);
      if (!x.entrenado) continue;
      s += std::string(k++ ? ", " : "") + x.capsula_n;
    }
    if (!k) s += T::T("ninguna", "none");
    (void)h;
    Marco(dl, a, lx0, ly1 + 16, lx1, ly1 + 74, 200);
    Texto(dl, a, (lx0 + lx1) * 0.5f, ly1 + 45, D::kSmall, s, false, false, lx1 - lx0 - 60);
  }
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", puede              ? T::T("Elegir maestro", "Choose master")
                     : !m.desbloqueado ? T::T("Bloqueado", "Locked")
                                       : T::T("Bloqueado (60 %)", "Locked (60%)")},
               {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

}  // namespace dbz3::heroe_ui
