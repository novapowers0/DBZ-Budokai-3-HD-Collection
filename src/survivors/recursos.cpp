// Dragon Budokai Survivors: arte propio a textura y ayudas de dibujo (recursos.h).
#include "recursos.h"
#include "medidas.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>

#include "../pasarela.h"

namespace dbz3::survivors {
namespace {

namespace fs = std::filesystem;

// metadatos_ui.json (res/mejoras/ui; el repo no tiene lector de JSON y los valores son fijos del arte entregado): px del PNG.
struct Barra {
  const char* tipo;
  float x0, y0, x1, y1;   // rect_interior
};
// tanda 2 del arte (metadatos_ui.json version 2, 11/11): fill_rect_xyxy_exclusive
constexpr Barra kBarras[] = {{"vida", 12, 16, 500, 48}, {"ki", 13, 17, 499, 47}, {"exp", 9, 10, 1911, 23}};
struct Nueve {
  const char* pieza;
  float l, t, r, b;   // orden_margenes: izquierda, arriba, derecha, abajo
};
constexpr Nueve kNueve[] = {{"ui_boton_normal", 36, 18, 36, 18},     {"ui_boton_encima", 36, 18, 36, 18},
                            {"ui_boton_pulsado", 36, 18, 36, 18},    {"ui_boton_deshabilitado", 36, 18, 36, 18},
                            {"ui_ventana_dialogo", 64, 64, 64, 64},  {"ui_panel_resultados", 64, 64, 64, 64},
                            {"ui_tooltip", 64, 64, 64, 64}};

std::unordered_map<std::string, ps2::Tex>& Cache() {
  static std::unordered_map<std::string, ps2::Tex> c;   // hilo de dibujo; los punteros a los valores no cambian al crecer
  return c;
}

fs::path Carpeta(const char* nombre, std::atomic<bool>& avisado, fs::path& cache) {
  medidas::Tramo tramo{medidas::Ruta};
  static std::mutex mx;   // auditoria #11: la carpeta, una vez comprobada, no vuelve a hacer stat en cada llamada
  std::lock_guard<std::mutex> lk(mx);
  if (!cache.empty()) return cache;
  const fs::path p = rex::filesystem::GetExecutableFolder() / "res" / nombre;
  std::error_code ec;
  if (fs::is_directory(p, ec)) cache = p;
  else if (!avisado.exchange(true))
    REXLOG_ERROR("dbz3 survivors: falta la carpeta {} (la copia CMake al compilar y make_release/pruebas.py al preparar): el modo sale sin arte",
                 p.string());
  return p;
}

// Dos trabajadores bajo demanda. Una imagen pendiente devuelve nullptr y se reintenta;
// el dibujo nunca decodifica y solo publica dos texturas por cuadro. Estado con vida de proceso.
struct Predec {
  std::mutex mx;
  std::condition_variable aviso;
  std::unordered_map<std::string, pasarela::Imagen> listas;   // clave = ruta normalizada
  std::unordered_set<std::string> pedidos;
  std::deque<std::string> cola;
  uint64_t generacion = 0;
  size_t bytes = 0;
  bool iniciado = false;
};
Predec& Pre() {
  static Predec* p = new Predec;
  return *p;
}
std::string Clave(const fs::path& p) { return p.lexically_normal().make_preferred().string(); }
std::unordered_map<std::string, std::string>& Claves() {
  static std::unordered_map<std::string, std::string> c;
  return c;
}
void LimpiarClaves() { Claves().clear(); }
constexpr size_t kPresupuestoRam = size_t(320) << 20;   // RGBA ya decodificado y aun sin subir a la GPU

void LeerPng(const std::string& ruta, pasarela::Imagen& im) {
  std::ifstream in(ruta, std::ios::binary | std::ios::ate);
  const auto n = in.tellg();
  if (n <= 0 || n > 128 * 1024 * 1024) return;
  std::vector<uint8_t> datos(static_cast<size_t>(n));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(datos.data()), n)) return;
  if (!datos.empty()) im.rgba = rex::ui::DecodeImageRGBA(datos.data(), datos.size(), im.w, im.h);
}

void IniciarPredec() {
  Predec& P = Pre();
  std::lock_guard<std::mutex> l(P.mx);
  if (P.iniciado) return;
  P.iniciado = true;
  for (int k = 0; k < 2; ++k) std::thread([&P] {
      for (;;) {
        std::string ruta;
        uint64_t generacion;
        {
          std::unique_lock<std::mutex> lk(P.mx);
          P.aviso.wait(lk, [&] { return !P.cola.empty() && P.bytes < kPresupuestoRam; });
          ruta = std::move(P.cola.front());
          P.cola.pop_front();
          generacion = P.generacion;
        }
        pasarela::Imagen im;
        LeerPng(ruta, im);
        std::lock_guard<std::mutex> lk(P.mx);
        if (generacion != P.generacion) continue;
        P.bytes += im.rgba.size();
        P.listas.emplace(ruta, std::move(im)); // tambien el fallo: no solicitar sin fin un PNG ausente
      }
  }).detach();
}

bool TomarPredec(const std::string& clave, pasarela::Imagen& out) {
  Predec& P = Pre();
  std::lock_guard<std::mutex> l(P.mx);
  const auto it = P.listas.find(clave);
  if (it == P.listas.end()) {
    if (P.pedidos.insert(clave).second) P.cola.push_back(clave), P.aviso.notify_all();
    return false;
  }
  out = std::move(it->second);
  P.bytes -= out.rgba.size();
  P.listas.erase(it);
  P.pedidos.erase(clave);
  P.aviso.notify_all();
  return true;
}

}  // namespace

fs::path RutaRecursos() {
  static std::atomic<bool> avisado{false};
  static fs::path cache;
  return Carpeta("survivors", avisado, cache);
}
fs::path RutaMejoras() {
  static std::atomic<bool> avisado{false};
  static fs::path cache;
  return Carpeta("mejoras", avisado, cache);
}

const ps2::Tex* Textura(const fs::path& png) {
  medidas::Tramo tramo{medidas::Textura};
  auto& claves = Claves();   // auditoria #11: ruta tal cual -> clave normalizada (se vacia en LiberarTexturas)
  std::string crudo = png.string();
  auto ic = claves.find(crudo);
  if (ic == claves.end()) ic = claves.emplace(std::move(crudo), Clave(png)).first;
  const std::string& clave = ic->second;
  auto& c = Cache();
  if (const auto it = c.find(clave); it != c.end()) return it->second ? &it->second : nullptr;
  IniciarPredec();
  // Presupuesto compartido por todas las peticiones del cuadro (fondos, iconos y efectos).
  static int cuadro = -1, subidas = 0;
  if (cuadro != ImGui::GetFrameCount()) cuadro = ImGui::GetFrameCount(), subidas = 0;
  const auto t0 = std::chrono::steady_clock::now();
  pasarela::Imagen im;
  if (subidas >= 2) return nullptr;
  if (!TomarPredec(clave, im)) return nullptr; // la lectura/decodificacion nunca vuelve al dibujo
  if (im.rgba.empty()) {
    REXLOG_WARN("dbz3 survivors: no se pudo abrir o decodificar {}", clave);
    c[clave] = ps2::Tex{};   // no se reintenta
    return nullptr;
  }
  ps2::Tex t = ps2::Crear(im);
  ++subidas;
  if (!t) {   // aun sin dibujante: se reintenta el cuadro siguiente (lo ya decodificado se pierde si venia de los hilos; es raro)
    Predec& P = Pre();
    std::lock_guard<std::mutex> l(P.mx);
    P.bytes += im.rgba.size();
    P.listas.emplace(clave, std::move(im));
    return nullptr;
  }
  static int n_lentas = 0;
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (ms > 8.0 && n_lentas++ < 40)   // A17: lo que aun cuesta tiron en el hilo de dibujo (si "previa": solo la subida a GPU)
    REXLOG_INFO("dbz3 survivors: textura {} {:.1f} ms (subida, {}x{})", fs::path(clave).filename().string(), ms, im.w, im.h);
  return &(c[clave] = std::move(t));
}

const ps2::Tex* Icono(const char* id) { return Textura(RutaMejoras() / "iconos" / (std::string(id) + ".png")); }
const ps2::Tex* Ui(const char* pieza) { return Textura(RutaMejoras() / "ui" / (std::string(pieza) + ".png")); }
const ps2::Tex* Fondo(const char* escenario, const char* capa) {
  return Textura(RutaRecursos() / "03_fondos" / escenario / ("fondo_" + std::string(escenario) + "_" + capa + ".png"));
}
const ps2::Tex* Arte(const char* rel) { return Textura(RutaRecursos() / rel); }
const ps2::Tex* ArteMejoras(const char* rel) { return Textura(RutaMejoras() / rel); }
void LiberarTexturas() {
  Cache().clear();
  LimpiarClaves();
  Predec& P = Pre();   // lo decodificado que no se llego a usar tambien libera su RAM (una apertura nueva del modo lo repone al pedirlo)
  std::lock_guard<std::mutex> l(P.mx);
  ++P.generacion; // descarta tambien los resultados que todavia estan en vuelo
  P.cola.clear();
  P.pedidos.clear();
  P.listas.clear(), P.bytes = 0;
  P.aviso.notify_all(); // los dos trabajadores siguen disponibles para la siguiente apertura
}

void DibujarCuadro(ImDrawList* dl, const ps2::Tex* t, int cuadro, int cuadros, ImVec2 ancla, ImVec2 p, float escala, ImU32 col, bool espejo) {
  if (!dl || !t || t->w <= 0 || cuadros < 1 || cuadro < 0 || cuadro >= cuadros) return;
  const float cw = float(t->w) / float(cuadros), ch = float(t->h);
  const float x0 = p.x - ancla.x * escala, y0 = p.y - ancla.y * escala;
  const float u0 = float(cuadro) / float(cuadros), u1 = float(cuadro + 1) / float(cuadros);
  const ImVec2 a(espejo ? p.x + ancla.x * escala - cw * escala : x0, y0), b(a.x + cw * escala, y0 + ch * escala);
  dl->AddImage(t->id(), a, b, ImVec2(espejo ? u1 : u0, 0), ImVec2(espejo ? u0 : u1, 1), col);
}

void DibujarGirada(ImDrawList* dl, const ps2::Tex* t, ImVec2 c, ImVec2 semi, float ang, float aplastar, ImU32 col) {
  if (!dl || !t) return;
  const float cs = std::cos(ang), sn = std::sin(ang);
  ImVec2 q[4];
  const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; ++i) {   // y de pantalla hacia abajo: este giro es horario
    const float x = sx[i] * semi.x, y = sy[i] * semi.y;
    q[i] = ImVec2(c.x + x * cs - y * sn, c.y + (x * sn + y * cs) * aplastar);
  }
  dl->AddImageQuad(t->id(), q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
}

float DibujarCifras(ImDrawList* dl, bool amarillas, int valor, ImVec2 base, float alto, ImU32 col) {
  // glyph_rect de metadatos_tanda3.json (x, ancho; y = 4 y alto = 56 en todas): 0..9
  static constexpr float kBlancas[10][2] = {{8, 47}, {81, 30}, {137, 45}, {201, 45}, {263, 50}, {329, 46}, {393, 45}, {457, 45}, {520, 47}, {585, 46}};
  static constexpr float kAmarillas[10][2] = {{8, 48}, {81, 30}, {137, 45}, {202, 44}, {263, 49}, {329, 45}, {393, 45}, {457, 45}, {520, 47}, {585, 46}};
  const ps2::Tex* t = Arte(amarillas ? "04_interfaz/ui_dano_cifras_amarillas.png" : "04_interfaz/ui_dano_cifras_blancas.png");
  if (!dl || !t || t->w <= 0 || valor < 0) return 0;
  const auto& g = amarillas ? kAmarillas : kBlancas;
  int c[10], n = 0;
  do c[n++] = valor % 10, valor /= 10; while (valor > 0 && n < 10);
  const float k = alto / 56.0f;
  float w = 0;
  for (int i = 0; i < n; ++i) w += g[c[i]][1] * k;
  float x = base.x - w * 0.5f;
  const float v0 = 4.0f / float(t->h), v1 = 60.0f / float(t->h);
  for (int i = n - 1; i >= 0; --i) {
    const float gx = g[c[i]][0], gw = g[c[i]][1];
    dl->AddImage(t->id(), ImVec2(x, base.y - alto), ImVec2(x + gw * k, base.y), ImVec2(gx / float(t->w), v0), ImVec2((gx + gw) / float(t->w), v1), col);
    x += gw * k;
  }
  return w;
}

void DibujarImagen(ImDrawList* dl, const ps2::Tex* t, ImVec2 a, ImVec2 b, ImU32 col) {
  if (dl && t) dl->AddImage(t->id(), a, b, ImVec2(0, 0), ImVec2(1, 1), col);
}

void DibujarAjustada(ImDrawList* dl, const ps2::Tex* t, ImVec2 a, ImVec2 b, ImU32 col) {
  if (!dl || !t || t->w <= 0 || t->h <= 0) return;
  const float k = std::min((b.x - a.x) / t->w, (b.y - a.y) / t->h), w = t->w * k, h = t->h * k;
  const ImVec2 o((a.x + b.x - w) * 0.5f, (a.y + b.y - h) * 0.5f);
  dl->AddImage(t->id(), o, ImVec2(o.x + w, o.y + h), ImVec2(0, 0), ImVec2(1, 1), col);
}

bool DibujarNueve(ImDrawList* dl, const char* pieza, ImVec2 a, ImVec2 b, float escala, ImU32 col) {
  const Nueve* n = nullptr;
  for (const Nueve& q : kNueve)
    if (std::strcmp(q.pieza, pieza) == 0) n = &q;
  return n && DibujarNueveTex(dl, Ui(pieza), n->l, n->t, n->r, n->b, a, b, escala, col);
}

bool DibujarNueveTex(ImDrawList* dl, const ps2::Tex* t, float l, float tp, float r, float bt, ImVec2 a, ImVec2 b, float escala, ImU32 col) {
  if (!dl || !t || t->w <= 0 || t->h <= 0) return false;
  // margenes en pantalla (si la caja es mas pequena que las dos esquinas, se reparten a partes iguales)
  const float kx = std::min(escala, (b.x - a.x) / std::max(l + r, 1.0f)), ky = std::min(escala, (b.y - a.y) / std::max(tp + bt, 1.0f));
  const float X[4] = {a.x, a.x + l * kx, b.x - r * kx, b.x}, Y[4] = {a.y, a.y + tp * ky, b.y - bt * ky, b.y};
  const float U[4] = {0, l / t->w, 1 - r / t->w, 1}, V[4] = {0, tp / t->h, 1 - bt / t->h, 1};
  dl->PushTextureID(t->id());
  dl->PrimReserve(9 * 6, 9 * 4);
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) dl->PrimRectUV(ImVec2(X[i], Y[j]), ImVec2(X[i + 1], Y[j + 1]), ImVec2(U[i], V[j]), ImVec2(U[i + 1], V[j + 1]), col);
  dl->PopTextureID();
  return true;
}

void DibujarBarra(ImDrawList* dl, const char* tipo, ImVec2 a, ImVec2 b, float pct, ImU32 col) {
  const Barra* r = nullptr;
  for (const Barra& q : kBarras)
    if (std::strcmp(q.tipo, tipo) == 0) r = &q;
  if (!dl || !r) return;
  const std::string base = std::string("ui_barra_") + tipo;
  const ps2::Tex* relleno = Ui((base + "_relleno").c_str());
  const ps2::Tex* marco = Ui((base + "_marco").c_str());
  if (relleno && relleno->w > 0) {   // relleno debajo del marco, recortado por porcentaje (no se estira)
    const float u = (r->x0 + std::clamp(pct, 0.0f, 1.0f) * (r->x1 - r->x0)) / relleno->w;
    dl->AddImage(relleno->id(), a, ImVec2(a.x + (b.x - a.x) * u, b.y), ImVec2(0, 0), ImVec2(u, 1), col);
  }
  DibujarImagen(dl, marco, a, b);
}

void DibujarSuelo(ImDrawList* dl, const char* escenario, ImVec2 a, ImVec2 b, ImVec2 camara, float tesela) {
  const ps2::Tex* t = Fondo(escenario, "suelo");
  if (!dl || !t || t->w <= 0 || tesela < 8.0f) return;
  const float tw = tesela, th = tesela * t->h / t->w;
  const float x0 = a.x - (camara.x - std::floor(camara.x / tw) * tw), y0 = a.y - (camara.y - std::floor(camara.y / th) * th);
  const int nx = int(std::ceil((b.x - x0) / tw)), ny = int(std::ceil((b.y - y0) / th));
  dl->PushClipRect(a, b, true);
  dl->PushTextureID(t->id());
  dl->PrimReserve(nx * ny * 6, nx * ny * 4);
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i)
      dl->PrimRectUV(ImVec2(x0 + i * tw, y0 + j * th), ImVec2(x0 + (i + 1) * tw, y0 + (j + 1) * th), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
  dl->PopTextureID();
  dl->PopClipRect();
}

void DibujarCapa(ImDrawList* dl, const char* escenario, const char* capa, ImVec2 a, ImVec2 b, ImU32 col) {
  const ps2::Tex* t = Fondo(escenario, capa);
  if (!dl || !t || t->w <= 0 || t->h <= 0) return;
  const float k = std::max((b.x - a.x) / t->w, (b.y - a.y) / t->h), w = t->w * k, h = t->h * k;
  const ImVec2 o((a.x + b.x - w) * 0.5f, (a.y + b.y - h) * 0.5f);
  dl->PushClipRect(a, b, true);
  dl->AddImage(t->id(), o, ImVec2(o.x + w, o.y + h), ImVec2(0, 0), ImVec2(1, 1), col);
  dl->PopClipRect();
}

}  // namespace dbz3::survivors
