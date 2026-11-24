// Aura competitiva: filtro corporal B3 y particulas originales dibujadas por el host.
#include "comp_aura.h"

#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "comp_estado.h"
#include "generated/dbz3_init.h"
#include "guest_region.h"
#include "ingame/game_style.h"
#include "ps2_modelo.h"

namespace dbz3::comp::aura {
namespace {
constexpr uint32_t kMgr = 0x823D6CF4, kPrioridad = 0x2000;
struct Estado {
  uint32_t actor = 0;
  bool activo = false;
  float reloj = -1;
  float intensidad = 0;
};
std::array<Estado, 2> estados{};
uint32_t gestor = 0;

struct Cuadro {
  float reloj = -1, paso = 0;
  bool valido = false, parado = false;
  uint32_t gestor = 0;
  std::array<bool, 2> burst{};
  std::array<ps2::V3, 2> origen{};
  ps2::Camara cam;
};
std::mutex buzon_mutex;
std::deque<Cuadro> buzon;
void Publicar(Cuadro c) {
  std::lock_guard<std::mutex> lock(buzon_mutex);
  if (buzon.size() >= 256 || !c.valido) buzon.clear();
  buzon.push_back(std::move(c));
}

// Estelas generadas proceduralmente: sin imagenes ni datos de otro juego.
struct Particula { float x, y, z, velocidad, edad, vida, ancho, largo; };
struct Envoltura {
  std::vector<Particula> particulas;
  float acumulador = 0;
  uint32_t semilla = 0x87654321;
};
float Azar(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return float(s >> 8) * (1.0f / 16777216.0f);
}
void Paso(Envoltura& e, float dt, bool emitir) {
  if (!(dt > 0) || !std::isfinite(dt)) return;
  dt = std::min(dt, 4.0f);
  e.acumulador = emitir ? e.acumulador + dt * 2.2f : 0;
  while (e.acumulador >= 1 && e.particulas.size() < 100) {
    e.acumulador -= 1;
    const float a = Azar(e.semilla) * 6.2831853f;
    const float r = 5.0f + 3.0f * Azar(e.semilla);
    e.particulas.push_back({std::cos(a)*r, .5f + 15.0f*Azar(e.semilla), std::sin(a)*r,
                            .20f + .30f*Azar(e.semilla), 0,
                            9 + 10*Azar(e.semilla), .6f + .8f*Azar(e.semilla),
                            1.8f + 3.0f*Azar(e.semilla)});
  }
  if (e.particulas.size() >= 100) e.acumulador = 0;
  for (auto& p : e.particulas) { p.edad += dt; p.y += p.velocidad * dt; }
  e.particulas.erase(std::remove_if(e.particulas.begin(), e.particulas.end(),
      [](const Particula& p) { return p.edad >= p.vida; }), e.particulas.end());
}
void Llama(ImDrawList* dl, ImVec2 s, float ancho, float largo, ImU32 centro, ImU32 borde) {
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  dl->PrimReserve(12, 5);
  // PrimReserve puede abrir otro rango de vertices al llegar al limite de ImDrawIdx.
  const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
  for (int i = 1; i <= 4; ++i) {
    dl->PrimWriteIdx(base); dl->PrimWriteIdx(base + i);
    dl->PrimWriteIdx(base + (i == 4 ? 1 : i + 1));
  }
  dl->PrimWriteVtx(s, uv, centro);
  dl->PrimWriteVtx({s.x, s.y-largo}, uv, borde);
  dl->PrimWriteVtx({s.x+ancho, s.y}, uv, borde);
  dl->PrimWriteVtx({s.x, s.y+largo*.5f}, uv, borde);
  dl->PrimWriteVtx({s.x-ancho, s.y}, uv, borde);
}
void Pintar(const Envoltura& e, const ps2::V3& origen, const ps2::Camara& cam, ImDrawList* dl) {
  dl->AddCallback(rex::ui::kImDrawBlendAdditive, nullptr);
  const float foco = cam.Foco();
  for (const auto& p : e.particulas) {
    ImVec2 s; float escala = 0;
    if (!cam.P({origen.x+p.x, origen.y+p.y, origen.z+p.z}, s, escala, foco)) continue;
    const float t = p.edad / p.vida;
    const float alpha = std::clamp(std::min(t * 5, (1-t) * 3), 0.0f, 1.0f);
    const float ancho = std::max(1.0f, p.ancho * escala);
    const float largo = std::max(2.0f, p.largo * escala);
    // Desvanecer los bordes evita siluetas geometricas duras.
    Llama(dl, s, ancho, largo, IM_COL32(75,165,255,int(70*alpha)), IM_COL32(75,165,255,0));
    // Nucleo blanco frio propio dentro de la envoltura azul; mismo fundido y camara.
    Llama(dl, s, ancho*.38f, largo*.72f, IM_COL32(210,240,255,int(85*alpha)), IM_COL32(210,240,255,0));
  }
  dl->AddCallback(rex::ui::kImDrawBlendAlpha, nullptr);
}
class Particulas final : public rex::ui::ImGuiDialog {
 public:
  explicit Particulas(rex::ui::ImGuiDrawer* dr) : ImGuiDialog(dr) {}
 protected:
  void OnDraw(ImGuiIO& io) override {
    std::deque<Cuadro> cola;
    { std::lock_guard<std::mutex> lock(buzon_mutex); cola.swap(buzon); }
    if (dbz3::g_guest_eu || !REXCVAR_QUERY(bool, dbz3_modo_competitivo)) { Reiniciar(); return; }
    for (const auto& c : cola) {
      if (!c.valido || c.gestor != cuadro_.gestor || c.reloj < cuadro_.reloj) Reiniciar();
      const bool nuevo = c.reloj != cuadro_.reloj;
      for (size_t k = 0; k < 2; ++k) {
        if (nuevo && !c.parado) Paso(env_[k], c.paso, c.burst[k]);
      }
      cuadro_ = c;
    }
    if (!cuadro_.valido) return;
    auto cam = cuadro_.cam;
    const auto im = ingame::style::GuestImage(io);
    const auto tam = im.Size();
    cam.h = {tam.x*.5f,tam.y*.5f}; cam.c = {im.p0.x+cam.h.x,im.p0.y+cam.h.y};
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(im.p0, im.p1, true);
    for (size_t k = 0; k < 2; ++k) Pintar(env_[k], cuadro_.origen[k], cam, dl);
    dl->PopClipRect();
  }
 private:
  void Reiniciar() { env_ = {}; cuadro_ = {}; }
  std::array<Envoltura, 2> env_{};
  Cuadro cuadro_;
};
float F(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f; std::memcpy(&f, &u, sizeof(f)); return f;
}
void F(uint8_t* base, uint32_t a, float f) {
  uint32_t u; std::memcpy(&u, &f, sizeof(u)); REX_STORE_U32(a, u);
}
void Insertar(uint8_t* base, uint32_t x, const Muestra& m) {
  if (!(m.intensidad > 0)) return;
  int i = 0;
  while (i < 2 && kPrioridad >= REX_LOAD_U32(x + 6264 + 16 * i)) ++i;
  if (i == 2) return;
  if (i == 0) std::memcpy(base + x + 6276, base + x + 6260, 16);
  const uint32_t b = x + 6260 + 16 * i;
  F(base, b, m.intensidad);
  REX_STORE_U32(b + 4, kPrioridad);
  REX_STORE_U32(b + 8, 0); REX_STORE_U32(b + 12, 0);
  for (uint32_t c = 0; c < 3; ++c) {
    REX_STORE_U8(b + 8 + c, m.azul[c]); REX_STORE_U8(b + 12 + c, m.blanco[c]);
  }
  REX_STORE_U32(x + 6256, std::min(REX_LOAD_U32(x + 6256) + 1u, 2u));
}
}  // namespace

void Actualizar(uint8_t* base) {
  if (!base || dbz3::g_guest_eu || !REXCVAR_QUERY(bool, dbz3_modo_competitivo)) {
    estados = {}; gestor = 0; Publicar({}); return;
  }
  const uint32_t g = REX_LOAD_U32(kMgr);
  if (!g || REX_LOAD_U32(g + 35128) != 4) {
    estados = {}; gestor = 0; Publicar({}); return;
  }
  if (g != gestor) estados = {}, gestor = g;
  const float reloj = F(base, g + 35160), paso = F(base, g + 35120);
  const bool parado = REX_LOAD_U32(g + 35116) != 0;
  if (!std::isfinite(reloj) || !std::isfinite(paso) || paso < 0) return;
  Cuadro cuadro;
  cuadro.valido = true; cuadro.gestor = g; cuadro.reloj = reloj; cuadro.paso = paso; cuadro.parado = parado;
  for (uint32_t i = 0; i < 16; ++i) cuadro.cam.m[i] = F(base, g + 0x60 + 4*i);
  const float fov = F(base, g + 0x1c4);
  if (fov > .1f && fov < 3) cuadro.cam.fov = fov;
  cuadro.cam.ok = true;
  for (int k = 0; k < 2; ++k) {
    auto& e = estados[size_t(k)];
    const uint32_t x = g + 2096 + 8240 * uint32_t(k);
    const bool en = dbz3::comp::EnBurst(k);
    cuadro.burst[size_t(k)] = en;
    cuadro.origen[size_t(k)] = {F(base,x+64),F(base,x+68),F(base,x+72)};
    if (e.actor != x || reloj < e.reloj) e = {}, e.actor = x;
    if (!parado) e.intensidad = std::clamp(e.intensidad + (en ? paso : -paso) / 30.0f, 0.0f, 1.0f);
    Muestra muestra;
    muestra.intensidad = e.intensidad * (.67f + .09f * std::sin(reloj * .07f));
    Insertar(base, x, muestra);
    e.activo = en; e.reloj = reloj;
  }
  Publicar(std::move(cuadro));
}
void Crear() {
  static Particulas* p = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!p && rt && rt->imgui_drawer()) p = new Particulas(rt->imgui_drawer());
}
}  // namespace dbz3::comp::aura
