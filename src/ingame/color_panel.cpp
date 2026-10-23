// dbz3 - Editor de colores del traje en la rueda de personajes (ver color_panel.h).
// Flujo: LB en la rueda -> recolorear.py ficha (regiones con nombre, en cache) -> panel: arriba/abajo = region,
// izquierda/derecha = valor del canal, LB/RB = canal (tono / saturacion / brillo), Y = region a cero, X = todo a cero,
// A = guardar como traje extra, B = cerrar. La vista previa la genera recolorear.py vista 0,6 s despues del ultimo
// cambio (mismo patron que la ficha del Heroe); el traje guardado sale en la rueda al volver a montar los personajes.
#include "color_panel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

#include "../launcher/i18n.h"
#include "../launcher/launcher_state.h"
#include "../mods.h"
#include "game_style.h"

namespace dbz3::ingame {
namespace {

namespace fs = std::filesystem;
namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200;
constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

struct Reg {
  int tex = 0;
  std::string nombre;
  int v[3] = {0, 0, 0};   // tono (-180..180 grados), saturacion y brillo (-100..100 = factor 0..2)
};

std::atomic<int> g_fase{0};   // 0 cerrado, 1 cargando regiones, 2 editando, 3 no se puede
std::mutex g_mu;
std::vector<Reg> g_regs;      // bajo g_mu al escribirse desde el hilo de la ficha
int g_id = -1, g_sel = 0, g_canal = 0;
std::string g_msg;
double g_msg_hasta = 0;
bool g_bloqueado = false;
double g_cambio = 0;          // ultimo cambio de un valor (retardo de la vista previa)
std::string g_clave_pedida, g_clave_hecha;
std::atomic<bool> g_generando{false};
std::atomic<int> g_png_nuevo{-1};   // 0/1: PNG listo por cargar (alternan para no leer mientras se escribe)
std::unique_ptr<rex::ui::ImmediateTexture> g_tex;
S::Pic g_pic;
// iluminar la region elegida (cuerpo con esa region en magenta, res_<id>_<textura>.png de la ficha) medio segundo
std::map<int, std::pair<std::unique_ptr<rex::ui::ImmediateTexture>, S::Pic>> g_res;
int g_res_tex = -1;
double g_res_hasta = 0;
int g_vez = 0;
double g_visto_select = 0;

fs::path Juego() { return dbz3::ModsRoot().parent_path(); }
fs::path DirColor() { return Juego() / "ingame_ui" / "color"; }

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

void Bloquear(bool si) {
  if (si == g_bloqueado) return;
  if (auto* in = Input()) {
    if (si) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
    g_bloqueado = si;
  }
}

#if defined(_WIN32)
bool Lanzar(const std::wstring& cmd, DWORD* code) {
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
  if (WaitForSingleObject(pi.hProcess, 300000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
#endif

// python -X utf8 "<juego>/mod center hd/recolorear.py" <args>; devuelve true si salio con 0.
bool RunPy(const std::wstring& args) {
#if defined(_WIN32)
  std::error_code ec;
  const fs::path script = Juego() / "mod center hd" / "recolorear.py";
  if (!fs::exists(script, ec)) return false;
  std::vector<std::wstring> pys;
  if (const char* e = std::getenv("DBZ3_PYTHON"); e && *e) pys.push_back(L"\"" + fs::path(e).wstring() + L"\"");
  const fs::path port = Juego() / "python" / "python.exe";
  if (fs::exists(port, ec)) pys.push_back(L"\"" + port.wstring() + L"\"");
  pys.push_back(L"py -3");
  pys.push_back(L"python");
  for (const std::wstring& py : pys) {
    DWORD code = 1;
    if (!Lanzar(py + L" -X utf8 \"" + script.wstring() + L"\" " + args, &code)) continue;
    if (code == 9009) continue;   // alias de la Microsoft Store
    return code == 0;
  }
#else
  (void)args;
#endif
  return false;
}

std::string Num(int n) { return std::to_string(n); }

void Aviso(const std::string& s, double ahora) {
  g_msg = s;
  g_msg_hasta = ahora + 6.0;
}

// Regiones (JSON de la vista previa): solo las que cambian; tono en grados, sat/val como factor.
std::string RegionesJson() {
  std::ostringstream o;
  o << "[";
  bool primero = true;
  for (const Reg& r : g_regs) {
    if (!r.v[0] && !r.v[1] && !r.v[2]) continue;
    char b[160];
    std::snprintf(b, sizeof(b), "%s{\"textura\":%d,\"tono\":%d,\"sat\":%.2f,\"val\":%.2f}", primero ? "" : ",", r.tex, r.v[0],
                  1.0 + r.v[1] / 100.0, 1.0 + r.v[2] / 100.0);
    o << b;
    primero = false;
  }
  o << "]";
  return o.str();
}

void CargarFicha(int id) {
  g_fase = 1;
  std::thread([id] {
    const fs::path tsv = DirColor() / ("ficha_" + Num(id) + ".tsv");
    std::error_code ec;
    fs::create_directories(DirColor(), ec);
    const bool ok = RunPy(L"ficha --personaje " + std::to_wstring(id) + L" --juego \"" + Juego().wstring() + L"\" --mods \"" +
                        dbz3::ModsRoot().wstring() + L"\"") &&
                    fs::exists(tsv, ec);
    std::vector<Reg> regs;
    if (ok) {
      std::ifstream in(tsv);
      std::string ln;
      while (std::getline(in, ln)) {
        const auto tab = ln.find('\t');
        if (tab == std::string::npos) continue;
        Reg r;
        r.tex = std::atoi(ln.substr(0, tab).c_str());
        r.nombre = ln.substr(tab + 1);
        while (!r.nombre.empty() && (r.nombre.back() == '\r' || r.nombre.back() == '\n')) r.nombre.pop_back();
        regs.push_back(r);
      }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_regs = regs;
    g_sel = 0;
    g_fase = ok && !regs.empty() ? 2 : 3;
    REXLOG_INFO("dbz3 color: personaje {}: {} regiones{}", id, regs.size(), ok ? "" : " (no se pudo leer la ficha)");
  }).detach();
}

void PedirVista(int id) {
  if (g_generando.exchange(true)) return;
  const int n = g_vez ^ 1;
  g_vez = n;
  const fs::path regs = DirColor() / "vista_regiones.json", png = DirColor() / ("vista_" + Num(n) + ".png");
  { std::ofstream(regs, std::ios::binary) << RegionesJson(); }
  std::thread([id, n, regs, png] {
    const bool ok = RunPy(L"vista --personaje " + std::to_wstring(id) + L" --regiones-archivo \"" + regs.wstring() +
                          L"\" --salida \"" + png.wstring() + L"\" --juego \"" + Juego().wstring() + L"\" --mods \"" +
                          dbz3::ModsRoot().wstring() + L"\" --ss 2");
    if (ok) g_png_nuevo = n;
    g_generando = false;
  }).detach();
}

void CargarPng(int n) {
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return;
  std::ifstream in(DirColor() / ("vista_" + Num(n) + ".png"), std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return;
  g_tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  g_pic = g_tex ? S::Pic{reinterpret_cast<ImTextureID>(g_tex.get()), float(w), float(h)} : S::Pic{};
}

S::Pic ResaltoPic(int id, int tex) {
  auto& e = g_res[tex];
  if (e.second) return e.second;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  std::ifstream in(DirColor() / ("res_" + Num(id) + "_" + Num(tex) + ".png"), std::ios::binary);
  if (!drawer || !in) return {};
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return {};
  e.first = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  e.second = e.first ? S::Pic{reinterpret_cast<ImTextureID>(e.first.get()), float(w), float(h)} : S::Pic{};
  return e.second;
}

void Guardar(double ahora) {
  std::string regs;
  for (const Reg& r : g_regs) {
    if (!r.v[0] && !r.v[1] && !r.v[2]) continue;
    char b[200];
    std::snprintf(b, sizeof(b), " --region %d:tono=%d,sat=%.2f,val=%.2f", r.tex, r.v[0], 1.0 + r.v[1] / 100.0, 1.0 + r.v[2] / 100.0);
    regs += b;
  }
  if (regs.empty()) {
    Aviso(T::T("Cambia algun color antes de guardar.", "Change a colour before saving."), ahora);
    return;
  }
  char hora[16];
  const std::time_t t = std::time(nullptr);
  std::strftime(hora, sizeof(hora), "%H%M%S", std::localtime(&t));
  const std::string mod = "color_" + Num(g_id) + "_" + hora;
  const std::wstring wregs(regs.begin(), regs.end()), wmod(mod.begin(), mod.end());
  const int id = g_id;
  Aviso(T::T("Guardando el traje...", "Saving the costume..."), ahora);
  std::thread([id, wregs, wmod, mod] {
    const bool ok = RunPy(L"crear --personaje " + std::to_wstring(id) + L" --mod " + wmod + L" --nombre \"Color " +
                          wmod.substr(wmod.size() - 6) + L"\" --mods \"" + dbz3::ModsRoot().wstring() + L"\"" + wregs);
    REXLOG_INFO("dbz3 color: traje {} {}", mod, ok ? "guardado" : "NO se pudo guardar");
    std::lock_guard<std::mutex> lk(g_mu);
    g_msg = ok ? std::string(T::T("Guardado como traje extra. Sale en la rueda al volver a montar los personajes (JUGAR).",
                                  "Saved as an extra costume. It shows up on the wheel after the characters are rebuilt (PLAY)."))
               : std::string(T::T("No se pudo guardar (hace falta Python).", "Could not save (Python is required)."));
    g_msg_hasta = 1e18;   // se queda hasta la siguiente tecla
  }).detach();
}

// Barra de un canal: degradado y marcador (como las de UT). v en -100..100 (tono -180..180).
void Barra(ImDrawList* dl, const S::Area& a, float x0, float y, float x1, int canal, int v, bool activa, bool fila) {
  const ImVec2 p0 = a.At(x0, y - 9), p1 = a.At(x1, y + 9);
  if (canal == 0) {
    const int n = 12;
    for (int i = 0; i < n; ++i) {
      float r0, g0, b0, r1, g1, b1;
      ImGui::ColorConvertHSVtoRGB(float(i) / n, 0.8f, 0.95f, r0, g0, b0);
      ImGui::ColorConvertHSVtoRGB(float(i + 1) / n, 0.8f, 0.95f, r1, g1, b1);
      const ImU32 ca = IM_COL32(int(r0 * 255), int(g0 * 255), int(b0 * 255), 255);
      const ImU32 cb = IM_COL32(int(r1 * 255), int(g1 * 255), int(b1 * 255), 255);
      const float xa = p0.x + (p1.x - p0.x) * i / n, xb = p0.x + (p1.x - p0.x) * (i + 1) / n;
      dl->AddRectFilledMultiColor(ImVec2(xa, p0.y), ImVec2(xb + 0.5f, p1.y), ca, cb, cb, ca);
    }
  } else {
    const ImU32 a0 = canal == 1 ? IM_COL32(120, 120, 120, 255) : IM_COL32(0, 0, 0, 255);
    const ImU32 a1 = canal == 1 ? IM_COL32(255, 70, 70, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddRectFilledMultiColor(p0, p1, a0, a1, a1, a0);
  }
  dl->AddRect(p0, p1, activa && fila ? IM_COL32(255, 220, 80, 255) : IM_COL32(20, 10, 0, 255), 0, 0, (activa && fila ? 3.0f : 2.0f) * a.s);
  const float frac = canal == 0 ? (v + 180) / 360.0f : (v + 100) / 200.0f;
  const float xv = p0.x + (p1.x - p0.x) * std::clamp(frac, 0.0f, 1.0f);
  dl->AddLine(ImVec2(xv, p0.y - 3 * a.s), ImVec2(xv, p1.y + 3 * a.s), IM_COL32(255, 255, 255, 255), 3.0f * a.s);
}

}  // namespace

bool ColorPanelActive() { return g_fase.load() != 0; }

void ColorPanelTick(ImGuiIO& io, uint16_t pressed, bool select_live, int id) {
  const double ahora = ImGui::GetTime();
  if (select_live) g_visto_select = ahora;
  int fase = g_fase.load();
  if (fase == 0) {
    Bloquear(false);
    if (!select_live || !(pressed & kLb) || id < 0) return;
    g_id = id;
    g_canal = 0;
    g_msg.clear();
    g_clave_pedida.clear();
    g_clave_hecha.clear();
    g_pic = {};
    g_tex.reset();
    g_res.clear();
    g_res_tex = -1;
    g_res_hasta = 0;
    g_png_nuevo = -1;
    CargarFicha(id);   // del juego o de un mod: si no se puede, la ficha falla y sale el aviso
    Bloquear(true);
    return;
  }
  if (!select_live && ahora - g_visto_select > 1.5) {   // salio de la rueda (combate, menu): se cierra
    g_fase = 0;
    Bloquear(false);
    return;
  }
  Bloquear(true);
  // ---- entrada
  if (pressed & kB) {
    g_fase = 0;
    Bloquear(false);
    return;
  }
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (fase == 2 && !g_regs.empty()) {
      const int n = int(g_regs.size());
      Reg& r = g_regs[size_t(std::clamp(g_sel, 0, n - 1))];
      const int antes = g_sel;
      if (pressed & kUp) g_sel = (g_sel + n - 1) % n;
      if (pressed & kDown) g_sel = (g_sel + 1) % n;
      if (g_sel != antes || g_res_tex < 0) {
        g_res_tex = g_regs[size_t(g_sel)].tex;
        g_res_hasta = ahora + 0.5;
      }
      if (pressed & kLb) g_canal = (g_canal + 2) % 3;
      if (pressed & kRb) g_canal = (g_canal + 1) % 3;
      const int d = ((pressed & kRight) ? 1 : 0) - ((pressed & kLeft) ? 1 : 0);
      if (d) {
        const int lim = g_canal == 0 ? 180 : 100;
        r.v[g_canal] = std::clamp(r.v[g_canal] + 10 * d, -lim, lim);
        g_cambio = ahora;
      }
      if (pressed & kY) {
        r.v[0] = r.v[1] = r.v[2] = 0;
        g_cambio = ahora;
      }
      if (pressed & kX) {
        for (Reg& q : g_regs) q.v[0] = q.v[1] = q.v[2] = 0;
        g_cambio = ahora;
      }
      if (pressed & kA) Guardar(ahora);
    }
    // vista previa: 0,6 s despues del ultimo cambio, si la receta cambio
    if (fase == 2 && !g_regs.empty() && ahora - g_cambio > 0.6) {
      const std::string clave = RegionesJson();
      if (clave != g_clave_hecha && !g_generando.load()) {
        g_clave_hecha = clave;
        PedirVista(g_id);
      }
    }
    if (fase == 2 && g_clave_hecha.empty() && !g_generando.load() && !g_regs.empty()) {   // primera vista: el original
      g_clave_hecha = RegionesJson();
      PedirVista(g_id);
    }
  }
  if (const int n = g_png_nuevo.exchange(-1); n >= 0) CargarPng(n);

  // ---- dibujo
  const S::Area a = S::GameArea(io);
  auto* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(a.p0, ImVec2(a.p0.x + 1200 * a.s, a.p0.y + 900 * a.s), IM_COL32(8, 14, 36, 232));
  S::Banner(dl, a, T::T("Color del traje", "Costume colour"));
  // vista previa a la izquierda
  S::Frame(dl, a.At(40, 150), a.At(450, 790), a.s);
  S::Pic mostrar = g_pic;
  if (g_res_tex >= 0 && ahora < g_res_hasta) {
    if (const S::Pic r = ResaltoPic(g_id, g_res_tex)) mostrar = r;
  }
  if (mostrar) {
    const float k = std::min(390.0f / mostrar.w, 620.0f / mostrar.h);
    const float w = mostrar.w * k, h = mostrar.h * k;
    S::DrawPic(dl, mostrar, a.At(245 - w / 2, 160), a.At(245 - w / 2 + w, 160 + h));
  } else if (fase == 2) {
    S::MenuTextB3(dl, a.At(245, 470), 24 * a.s, (std::string(T::T("Preparando la vista", "Preparing the preview")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')).c_str(), false, false, 360 * a.s);
  }
  // lista a la derecha
  S::Frame(dl, a.At(470, 150), a.At(1170, 790), a.s);
  if (fase == 1) {
    S::MenuTextB3(dl, a.At(820, 450), 26 * a.s, (std::string(T::T("Analizando el personaje", "Analysing the character")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')).c_str(), false, false, 600 * a.s);
  } else if (fase == 3) {
    S::MenuTextB3(dl, a.At(820, 450), 24 * a.s,
                  T::T("No se pudo preparar este personaje (hace falta Python y su modelo).", "Could not prepare this character (Python and its model are required)."),
                  false, false, 640 * a.s);
  } else {
    std::lock_guard<std::mutex> lk(g_mu);
    static const char* canales_es[] = {"Tono", "Sat.", "Brillo"};
    static const char* canales_en[] = {"Hue", "Sat.", "Value"};
    for (int c = 0; c < 3; ++c)
      S::MenuTextB3(dl, a.At(720 + 150.0f * c + 70, 178), 18 * a.s, T::T(canales_es[c], canales_en[c]), c == g_canal, false, 120 * a.s);
    const float fila = std::min(46.0f, 560.0f / std::max<size_t>(1, g_regs.size()));
    for (size_t i = 0; i < g_regs.size(); ++i) {
      const float y = 230 + fila * float(i);
      const bool sel = int(i) == g_sel;
      if (sel) S::Highlight(dl, a.At(480, y - fila / 2), a.At(1160, y + fila / 2), float(ahora));
      S::MenuTextB3(dl, a.At(570, y), 20 * a.s, g_regs[i].nombre.c_str(), sel, false, 150 * a.s);
      for (int c = 0; c < 3; ++c) Barra(dl, a, 650.0f + 170 * c, y, 650.0f + 170 * c + 150, c, g_regs[i].v[c], c == g_canal, sel);
    }
  }
  const char* ayuda = fase == 2 ? T::T("Arriba/Abajo: region  Izq/Der: valor  LB/RB: canal  Y: region a cero  X: todo a cero  A: guardar  B: salir",
                                       "Up/Down: region  Left/Right: value  LB/RB: channel  Y: reset region  X: reset all  A: save  B: exit")
                                    : T::T("B: salir", "B: exit");
  S::MenuTextB3(dl, a.At(600, 835), 17 * a.s, ayuda, false, false, 1120 * a.s);
  std::string msg;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (ahora < g_msg_hasta) msg = g_msg;
  }
  if (!msg.empty()) S::MenuTextB3(dl, a.At(600, 805), 20 * a.s, msg.c_str(), true, false, 1120 * a.s);
}

}  // namespace dbz3::ingame
