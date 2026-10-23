// dbz3 - CINE (ficha 22): intro de combate de 10 s en tiempo real con camara propia, rotulos y salto. US y EU (src/eu_map.h).
//
// Apagada por defecto: opcion dbz3_cine_intro, Pedir() (una vez, para retos/campanas) o DBZ3_CINE=1 (pruebas).
// Empieza con la pelea (estado 4, tras la presentacion del juego, que no se toca): los dos luchadores se quedan en
// guardia en su sitio (el COM lleva el mando 1 mientras dura) y la camara hace 3 planos (escenario, jugador 1, jugador 2) con
// bandas de cine y rotulos (nombre del escenario y de los luchadores, en el idioma del jugador). A la salta; al
// terminar o saltar la camara vuelve a la del juego y el COM a su IA: el combate sigue igual en los dos casos.
//
// Camara del combate (medido 2026-10-15 con DBZ3_SONDA_VOLCADO): gestor mgr = *0x823D6CF4;
//   mgr+0x60  matriz de vista 4x4 (mano derecha; filas: der.x arr.x atr.x 0 / ... / -der.ojo -arr.ojo -atr.ojo 1)
//   mgr+0x190 ojo (x y z 1), mgr+0x1A0 objetivo, mgr+0x1B0 arriba (0 1 0 0), mgr+0x1C4 fov vertical (0,785).
//   En la pelea el juego la calcula antes de dibujar; al empezar el dibujo (sub_820C2058, clima.cpp) se pisan ojo,
//   objetivo y matriz y todo el fotograma sale con la camara nueva. La camara del juego parte de su ojo anterior:
//   la intro acaba fundiendose con ella. En la presentacion (estados 1-2) manda la camara del guion del escenario
//   (ojo mgr+0x150, objetivo +0x160) y su matriz final no es esta: ahi no se puede (probado).

#include "cine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "escenarios.h"
#include "generated/dbz3_init.h"
#include "gigantes.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "modos.h"
#include "net/netplay.h"

REXCVAR_DEFINE_BOOL(dbz3_cine_intro, false, "DBZ3/Mods",
                    "Intro de cine: 10 s de camara propia al empezar cada combate (A la salta)");

namespace dbz3::cine {
namespace {

constexpr uint32_t kDur = 600;        // 10 s a 60 fotogramas

std::atomic<bool> g_pedido{false};   // Pedir(): la intro del combate siguiente
std::atomic<bool> g_en_curso{false};
std::atomic<float> g_t{0.0f};        // 0..1 de la intro (para el panel)
std::atomic<bool> g_saltar{false};   // A pulsado durante la intro (lo ve el panel, hilo de la interfaz)
std::mutex g_mu;
std::string g_rotulo[3];             // escenario, jugador 1, jugador 2
uint32_t g_frame = 0;                // fotograma de la intro (0 = no ha empezado)
bool g_hecha = false;                // ya se vio (o se salto) en este combate
int16_t g_mando[2] = {-1, -1};       // X+5028 (16 bits; -1 = COM) de cada luchador antes de la intro
bool g_cambiado[2] = {};             // la intro le cambio el mando (era el COM)

struct V3 {
  float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 Cruz(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Punto(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Unit(V3 a) {
  const float l = std::sqrt(Punto(a, a));
  return l > 1e-6f ? a * (1.0f / l) : V3{0, 0, -1};
}
V3 Mezcla(V3 a, V3 b, float t) { return a + (b - a) * t; }
float Suave(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// gira v (horizontal) un angulo en radianes alrededor del eje vertical
V3 Gira(V3 v, float a) { return {v.x * std::cos(a) - v.z * std::sin(a), v.y, v.x * std::sin(a) + v.z * std::cos(a)}; }

float LeeF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PonF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
V3 LeeV(uint8_t* base, uint32_t a) { return {LeeF(base, a), LeeF(base, a + 4), LeeF(base, a + 8)}; }
void PonV(uint8_t* base, uint32_t a, V3 v, float w) {
  PonF(base, a, v.x);
  PonF(base, a + 4, v.y);
  PonF(base, a + 8, v.z);
  PonF(base, a + 12, w);
}

bool Encendida() {
  static const bool prueba = std::getenv("DBZ3_CINE") != nullptr;
  return (prueba || g_pedido.load() || REXCVAR_GET(dbz3_cine_intro)) &&
         !dbz3::net::Session::Get().active();
}

// SONDA (DBZ3_SONDA_VOLCADO=f1,f2,...): vuelca .data/.bss (0x82300000, 2 MB) en esos fotogramas de pelea. Con ella
// se midieron la camara (mgr+0x60/+0x190), la matriz de mundo del luchador (X+0, gigantes.cpp) y el
// oscurecimiento del modo hiper (mgr+35180, clima.cpp): volcados antes/durante/despues y comparados fuera.
void Volcar(uint8_t* base, int n) {
  const std::string nombre = "sonda_volcado_" + std::to_string(n) + ".bin";
  std::FILE* f = std::fopen(nombre.c_str(), "wb");
  if (!f) return;
  const uint32_t a = 0x82300000, tam = 0x200000;
  std::fwrite(&a, 4, 1, f);
  std::fwrite(&tam, 4, 1, f);
  std::fwrite(base + a, 1, tam, f);
  std::fclose(f);
  REXLOG_INFO("dbz3 sonda: volcado {}", nombre);
}

// ---- panel: bandas de cine (tapan el marcador), rotulo del plano y "A: saltar" ------------------------------
namespace S = dbz3::ingame::style;
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    static bool antes = true;   // A ya pulsado al empezar no cuenta
    if (!g_en_curso.load()) {
      antes = true;
      return;
    }
    if (auto* rt = rex::Runtime::instance())
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) {
        rex::input::X_INPUT_STATE st = {};
        const bool a = in->GetStateForUI(0, &st) == 0 && (st.gamepad.buttons & 0x1000);
        if (a && !antes) g_saltar = true;
        antes = a;
      }
    // S1-F06: bandas y rotulos medidos en la imagen del juego (g), no en la ventana: en 16:9 es lo mismo; en 4:3 o
    // 16:10 la imagen va con bandas negras y antes la de abajo y los rotulos caian fuera de ella.
    const S::Imagen g = S::GuestImage(io);
    const float t = g_t.load(), w = io.DisplaySize.x, h = io.DisplaySize.y, gw = g.Size().x;
    const float abre = Suave(std::min(t * 10.0f, (1.0f - t) * 10.0f));   // las bandas entran y salen en 1 s
    const S::Area a = S::GameArea(io);
    const float arriba = g.p0.y + 170.0f * a.s * abre;   // la de arriba tapa el marcador
    const float abajo = (h - g.p1.y) + g.Size().y * 0.12f * abre;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, arriba), IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(ImVec2(0, h - abajo), ImVec2(w, h), IM_COL32(0, 0, 0, 255));
    if (abre < 0.99f) return;
    const int plano = t < 0.35f ? 0 : t < 0.65f ? 1 : 2;
    std::string rotulo;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      rotulo = g_rotulo[plano];
    }
    if (plano == 2) rotulo = std::string("VS  ") + rotulo;
    const float banda = g.Size().y * 0.12f;   // S1-F06
    if (!rotulo.empty())
      S::MenuText(dl, ImVec2(g.p0.x + gw * 0.5f, g.p1.y - banda * 0.5f), 40.0f * a.s, rotulo.c_str(), true, false);
    S::HelpLine(dl, ImVec2(g.p0.x + gw * 0.86f, g.p0.y + 85.0f * a.s), 36.0f * a.s, {{"A", dbz3::i18n::T("Saltar", "Skip")}});
  }
};

void CrearPanel() {
  static Panel* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

void Empezar(uint8_t* base, uint32_t mgr) {
  g_frame = 1;
  g_hecha = true;
  g_pedido = false;
  g_saltar = false;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_rotulo[0] = dbz3::escenarios::Nombre(int(REX_LOAD_U32(dbz3::GA(0x823A3D80))));   // G+0: registro cargado
    for (uint32_t k = 0; k < 2; ++k)
      g_rotulo[1 + k] = dbz3::modos::Nombre(int(REX_LOAD_U32(mgr + 2096 + 8240 * k + 7420)));
  }
  // el COM se queda en guardia: mientras dura lleva el mando 1, que esta conectado (uno sin nadie saca el aviso de
  // "mando desconectado", visto por hub) y sin tocar no hace nada. X+5028 es de 16 bits (X+5030 va aparte).
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + 2096 + 8240 * k;
    g_mando[k] = int16_t(REX_LOAD_U16(x + 5028));
    g_cambiado[k] = g_mando[k] == -1;
    if (g_cambiado[k]) REX_STORE_U16(x + 5028, 0);
  }
  static bool panel = false;
  if (!panel) {
    panel = true;
    dbz3::launcher::DeferUi(&CrearPanel);
  }
  g_en_curso = true;
  REXLOG_INFO("dbz3 cine: empieza la intro (escenario '{}', {} contra {})", g_rotulo[0], g_rotulo[1], g_rotulo[2]);
}

void Terminar(uint8_t* base, const char* por) {
  if (!g_en_curso.exchange(false)) return;
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + 2096 + 8240 * k;
    if (mgr && g_cambiado[k] && REX_LOAD_U16(x + 5028) == 0) REX_STORE_U16(x + 5028, uint16_t(g_mando[k]));
    g_cambiado[k] = false;
  }
  REXLOG_INFO("dbz3 cine: intro {} en el fotograma {}", por, g_frame);
  g_frame = 0;
}

// Plano de la intro en el fotograma f (0..kDur): ojo y objetivo a partir de los luchadores (posicion y escala de
// dibujo: un jefe gigante se encuadra entero) y la camara del juego.
void Plano(uint32_t f, V3 p1, V3 p2, float s1, float s2, V3 ojo_juego, V3 obj_juego, V3* ojo, V3* obj) {
  const V3 c = Mezcla(p1, p2, 0.5f);
  const V3 eje = Unit(V3{p2.x - p1.x, 0, p2.z - p1.z});   // de 1 a 2, horizontal
  const V3 lado = Unit(Cruz(eje, {0, 1, 0}));
  const float t = float(f) / float(kDur);
  if (t < 0.35f) {   // 1: el escenario, gran angular en arco alto que baja hacia los dos
    const float u = Suave(t / 0.35f);
    const float s = std::max(s1, s2), k = 1.0f + 0.5f * (s - 1.0f);
    const V3 dir = Gira(lado, -1.1f + 1.4f * u);
    *ojo = c + dir * ((95.0f - 40.0f * u) * k) + V3{0, (42.0f - 26.0f * u) * k, 0};
    *obj = c + V3{0, 9.0f * s, 0};
  } else if (t < 0.65f) {   // 2: jugador 1 de frente, acercandose
    const float u = Suave((t - 0.35f) / 0.30f);
    const V3 frente = Gira(eje, 0.9f);
    *ojo = p1 + frente * ((22.0f - 8.0f * u) * s1) + V3{0, (11.0f - 1.0f * u) * s1, 0};
    *obj = p1 + V3{0, 10.0f * s1, 0};
  } else {   // 3: jugador 2 de frente y vuelta a la camara del juego al final
    const float u = Suave((t - 0.65f) / 0.30f);
    const V3 frente = Gira(eje * -1.0f, -0.9f);
    *ojo = p2 + frente * ((22.0f - 8.0f * u) * s2) + V3{0, (11.0f - 1.0f * u) * s2, 0};
    *obj = p2 + V3{0, 10.0f * s2, 0};
    const float v = Suave((t - 0.92f) / 0.08f);
    *ojo = Mezcla(*ojo, ojo_juego, v);
    *obj = Mezcla(*obj, obj_juego, v);
  }
}

}  // namespace

void Camara(uint8_t* base, const float ojo[3], const float obj[3], float giro) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr) return;
  const V3 e{ojo[0], ojo[1], ojo[2]}, o{obj[0], obj[1], obj[2]};
  const V3 f = Unit(o - e), d0 = Unit(Cruz(f, {0, 1, 0})), u0 = Cruz(d0, f), a = f * -1.0f;
  const float cg = std::cos(giro), sg = std::sin(giro);
  const V3 d = d0 * cg - u0 * sg, u = u0 * cg + d0 * sg;   // giro de derecha/arriba alrededor de f
  PonV(base, mgr + 0x190, e, 1.0f);
  PonV(base, mgr + 0x1A0, o, 1.0f);
  const float m[16] = {d.x, u.x, a.x, 0, d.y, u.y, a.y, 0, d.z, u.z, a.z, 0, -Punto(d, e), -Punto(u, e), -Punto(a, e), 1};
  for (int i = 0; i < 16; ++i) PonF(base, mgr + 0x60 + 4 * uint32_t(i), m[i]);
}

bool LeerCamara(uint8_t* base, float ojo[3], float obj[3]) {
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr) return false;
  for (uint32_t c = 0; c < 3; ++c) {
    ojo[c] = LeeF(base, mgr + 0x190 + 4 * c);
    obj[c] = LeeF(base, mgr + 0x1A0 + 4 * c);
  }
  return true;
}

void Pedir() { g_pedido = true; }
int16_t Mando(uint8_t* base, int lado) {
  if (lado < 0 || lado > 1) return 0;
  if (g_en_curso.load() && g_cambiado[lado]) return g_mando[lado];
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  return mgr ? int16_t(REX_LOAD_U16(mgr + 2096 + 8240 * uint32_t(lado) + 5028)) : 0;
}
bool EnCurso() { return g_en_curso.load(); }
void NuevoCombate() {
  g_hecha = false;
  g_en_curso = false;
  g_frame = 0;
}

void Fotograma(uint8_t* base, uint32_t estado, uint32_t frames_pelea) {
  static const std::string volcados = std::getenv("DBZ3_SONDA_VOLCADO") ? std::getenv("DBZ3_SONDA_VOLCADO") : "";
  if (!volcados.empty() && frames_pelea &&
      ("," + volcados + ",").find("," + std::to_string(frames_pelea) + ",") != std::string::npos)
    Volcar(base, int(frames_pelea));

  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  // K2 PRUEBA: DBZ3_TEST_CAMARA_LUCHA="lado ox oy oz alto" = camara fija en combate: objetivo = pies del luchador `lado` (0/1) + (0, alto, 0),
  // ojo = objetivo + (ox, oy, oz) en ejes del mundo. Para primeros planos de cara, hombro y antebrazo (la vista previa del heroe XV2 es una tira 2D).
  if (mgr && estado == 4) {
    static const char* tl = std::getenv("DBZ3_TEST_CAMARA_LUCHA");
    if (tl) {
      float v[6] = {0, 0, 0, 6, 12, 0};      // v[5] = fov vertical (0 = el del juego, 0,785)
      std::sscanf(tl, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
      const V3 p = LeeV(base, mgr + 2096 + 8240 * (v[0] > 0.5f ? 1u : 0u) + 48);
      const float o[3] = {p.x, p.y + v[4], p.z}, e[3] = {o[0] + v[1], o[1] + v[2], o[2] + v[3]};
      static int nlog = 0;
      if (nlog++ % 120 == 0) REXLOG_INFO("dbz3 cine: CAMARA_LUCHA pie ({:.1f} {:.1f} {:.1f}) ojo ({:.1f} {:.1f} {:.1f})", p.x, p.y, p.z, e[0], e[1], e[2]);
      Camara(base, e, o);
      if (v[5] > 0.01f) PonF(base, mgr + 0x1C4, v[5]);
      return;
    }
  }
  const bool saltar = g_saltar.exchange(false);
  if (!mgr || estado != 4 || saltar) {   // A, o fuera de la pelea: se acaba
    Terminar(base, saltar ? "saltada" : "cortada");
    return;
  }
  if (!g_en_curso.load()) {
    if (g_hecha || frames_pelea > 2 || !Encendida()) return;   // solo al empezar la pelea
    Empezar(base, mgr);
  }
  if (g_frame > kDur) {
    Terminar(base, "terminada");
    return;
  }
  g_t = float(g_frame) / float(kDur);
  const V3 p1 = LeeV(base, mgr + 2096 + 48), p2 = LeeV(base, mgr + 2096 + 8240 + 48);   // fila de posicion (X+48)
  float oj[3], ob[3];
  LeerCamara(base, oj, ob);
  V3 ojo, obj;
  Plano(g_frame, p1, p2, dbz3::gigantes::EscalaDe(base, 0), dbz3::gigantes::EscalaDe(base, 1), {oj[0], oj[1], oj[2]},
        {ob[0], ob[1], ob[2]}, &ojo, &obj);
  const float e[3] = {ojo.x, ojo.y, ojo.z}, o[3] = {obj.x, obj.y, obj.z};
  Camara(base, e, o);
  ++g_frame;
}

}  // namespace dbz3::cine
