// Dragon Budokai Survivors: lo compartido por las partes del modo (modo.cpp nucleo, modo_mundo.cpp, modo_pantallas.cpp, modo_audio.cpp): la clase
// Modo y sus ayudas. Troceo P0 (survivors_comercial/PLAN_COMERCIAL.md): solo traslado, sin cambios de comportamiento. Lo toca el coordinador.
#pragma once
#include "entrada.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "../cinematica.h"
#include "../combate.h"
#include "../eu_map.h"
#include "../pasarela.h"
#include "../tactics/audio.h"   // MezclaSe: voces a la vez con prioridad (la de Dragon Tactics)
#include "generated/dbz3_init.h"
#include "../ingame/game_style.h"
#include "../ingame/hub_panel.h"
#include "../ingame/online_panel.h"
#include "../ranking_online.h"   // RANK-ONLINE
#include "../launcher/i18n.h"
#include "../launcher/launcher_state.h"
#include "../launcher/settings.h"
#include "../launcher/ui_kit.h"
#include "../ps2_modelo.h"
#include "ambiente.h"
#include "impostores.h"
#include "juego.h"
#include "recursos.h"
#include "tienda.h"
#include "pantallas_p2.h"
#include "mundo_p1.h"
#include "audio_p3.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // zoom: GetAsyncKeyState / GetForegroundWindow (teclado real aunque ImGui no reciba la tecla)
#endif

REXCVAR_DECLARE(bool, dbz3_survivors_sonido_combate);   // definido en modo.cpp
REXCVAR_DECLARE(bool, dbz3_survivors_combate_oculto);   // definido en modo_audio.cpp (SUR-07: reserva)
REXCVAR_DECLARE(bool, dbz3_survivors_audio_v2);         // definidos en modo_audio.cpp (P3)
REXCVAR_DECLARE(bool, dbz3_survivors_contenido_v2);

namespace dbz3::hub {   // hub.cpp (los usa tambien Dragon Tactics)
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
void BattleModeNext(int mode);
bool OwnBattle();
}  // namespace dbz3::hub

namespace dbz3::pasarela::nave {   // pasarela_nave.cpp
int EstadoFiguraB3(int b3, int forma, int fid);
int FidTrajeB3(int b3, int traje, int forma);
bool DibujarFiguraB3En(dbz3::ps2::Escena* esc, const dbz3::ps2::Camara& cam, int b3, int forma, dbz3::ps2::V3 suelo, float alto, float giro, int clip, float t, int fid, float dura = 0.0f,
                       float ciclo = -1.0f, int clip_prev = -1, float t_prev = 0.0f, float mezcla = 0.0f);
}  // namespace dbz3::pasarela::nave

namespace dbz3::survivors::interno {


namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;
using Reloj = std::chrono::steady_clock;
inline double Ms(Reloj::time_point a, Reloj::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

constexpr uint16_t kArriba = 0x0001, kAbajo = 0x0002, kIzq = 0x0004, kDer = 0x0008, kStart = 0x0010, kA = 0x1000, kB = 0x2000, kY = 0x8000;
constexpr float kAlto = 1.8f;          // alto de una figura en unidades de mundo (juego.h)
constexpr float kTopeCarga = 180.0f;   // s: el mismo tope que la precarga de Tactics
constexpr int kClipEspera = 0, kClipAndar = 1, kClipAtaque = 2, kClipTecnica = 3, kClipDano = 4, kClipDerrota = 5, kClipVictoria = 7;
constexpr float kMezcla = 0.18f;   // s de mezcla entre clips (la de Dragon Tactics)
constexpr int kCaidos = 64;        // enemigos cayendo a la vez (lista visual fija; el mas viejo deja sitio)
constexpr int kChispas = 48;       // chispas de golpe a la vez (solo sin la hoja fx_impacto de la tanda 3)

// ---------------------------------------------------------------- arte de la tanda 3 (res/survivors; medidas de metadatos_tanda3.json)
enum Arte3 : int {
  kGemaAzul, kGemaVerde, kGemaRoja,
  kObjeto0,                         // + TipoObjeto: capsula, iman, bomba, reloj, zeni, cofre (cerrado)
  kCofreAbierto = kObjeto0 + kTiposObjeto,
  kHoja0,                           // + Hoja: las 7 hojas de efectos
  kAnillo = kHoja0 + 7, kFranja, kDiana, kFlechaJefe, kFlechaCofre,
  kJefeMarco, kJefeRelleno, kPuntoVacio, kPuntoLleno, kPuntoMax, kVelo, kAvisoOleada, kRotuloVictoria, kRotuloDerrota, kLogo,
  kStatVida, kStatVelocidad, kStatDano, kStatAlcance,
  // tanda 4 (res/survivors/tanda4; medidas de sus metadatos). El decorado de cada escenario va en survivors.toml ([escenario.decorado]).
  kRayo, kMarcaRayo, kRotuloJefe, kMarcoRetrato, kAnilloOro, kAnilloRojo, kAnilloAzul, kZona, kCraneo, kFlechaElite, kFlechaEspecial, kSombra,
  kTiendaPanel, kCasillaBloq, kCasillaDisp, kCasillaComp, kTarjetaEsc, kTarjetaEscRes, kDif1, kDif2,
  kCargaMarco, kCargaRelleno, kRachaMarco, kMiniMarco, kMiniJugador, kMiniJefe, kMiniCofre, kMiniElite, kPanelPausa, kConsejo, kMedallaPlatino,
  kNumArte
};
constexpr const char* kArte3[kNumArte] = {
    "01_recogibles/recogible_gema_exp_azul.png", "01_recogibles/recogible_gema_exp_verde.png", "01_recogibles/recogible_gema_exp_roja.png",
    "01_recogibles/recogible_capsula_vida.png", "01_recogibles/recogible_iman.png", "01_recogibles/recogible_bomba_energia.png",
    "01_recogibles/recogible_reloj.png", "01_recogibles/recogible_moneda_zeni.png", "01_recogibles/recogible_cofre_cerrado.png",
    "01_recogibles/recogible_cofre_abierto.png",
    "02_efectos/fx_impacto.png", "02_efectos/fx_humo_derrota.png", "02_efectos/fx_recoger_gema.png", "02_efectos/fx_subida_nivel.png",
    "02_efectos/fx_explosion_grande.png", "02_efectos/fx_grieta_suelo.png", "02_efectos/fx_polvo_carrera.png",
    "03_avisos_jefe/aviso_anillo_peligro_octavo.png", "03_avisos_jefe/aviso_franja_carga.png", "03_avisos_jefe/aviso_impacto_suelo.png",
    "03_avisos_jefe/aviso_flecha_jefe.png", "03_avisos_jefe/aviso_flecha_cofre.png",
    "04_interfaz/ui_jefe_vida_marco.png", "04_interfaz/ui_jefe_vida_relleno.png", "04_interfaz/ui_tecnica_punto_vacio.png",
    "04_interfaz/ui_tecnica_punto_lleno.png", "04_interfaz/ui_tecnica_punto_maximo.png", "04_interfaz/ui_tecnica_velo_recarga.png",
    "04_interfaz/ui_aviso_oleada.png", "04_interfaz/ui_rotulo_victoria.png", "04_interfaz/ui_rotulo_derrota.png",
    "04_interfaz/logo_dragon_budokai_survivors.png",   // 16/11 nombre nuevo: sin este archivo se ve el titulo en texto
    "04_interfaz/ui_estadistica_vida.png", "04_interfaz/ui_estadistica_velocidad.png", "04_interfaz/ui_estadistica_dano.png",
    "04_interfaz/ui_estadistica_alcance.png",
    "tanda4/03_ambiente/ambiente_rayo.png", "tanda4/03_ambiente/ambiente_marca_rayo.png", "tanda4/04_jefes_elites/jefe_rotulo_presentacion.png",
    "tanda4/04_jefes_elites/jefe_marco_retrato.png", "tanda4/04_jefes_elites/elite_anillo_oro.png", "tanda4/04_jefes_elites/elite_anillo_rojo.png",
    "tanda4/04_jefes_elites/elite_anillo_azul.png", "tanda4/04_jefes_elites/aviso_zona_rectangular.png", "tanda4/04_jefes_elites/jefe_craneo_derrotado.png",
    "tanda4/04_jefes_elites/aviso_flecha_elite.png", "tanda4/04_jefes_elites/aviso_flecha_objeto_especial.png", "tanda4/04_jefes_elites/aviso_proyectil_caida.png",
    "tanda4/05_tienda/tienda_panel.png", "tanda4/05_tienda/tienda_casilla_bloqueada.png", "tanda4/05_tienda/tienda_casilla_disponible.png",
    "tanda4/05_tienda/tienda_casilla_comprada.png", "tanda4/05_tienda/ui_tarjeta_escenario_normal.png", "tanda4/05_tienda/ui_tarjeta_escenario_resaltada.png",
    "tanda4/05_tienda/ui_dificultad_1.png", "tanda4/05_tienda/ui_dificultad_2.png",
    "tanda4/06_marcador/ui_barra_carga_marco.png", "tanda4/06_marcador/ui_barra_carga_relleno.png", "tanda4/06_marcador/ui_contador_racha.png",
    "tanda4/06_marcador/ui_minimapa_marco.png", "tanda4/06_marcador/ui_minimapa_jugador.png", "tanda4/06_marcador/ui_minimapa_jefe.png",
    "tanda4/06_marcador/ui_minimapa_cofre.png", "tanda4/06_marcador/ui_minimapa_elite.png", "tanda4/06_marcador/ui_panel_pausa.png",
    "tanda4/06_marcador/ui_tarjeta_consejo.png", "tanda4/06_marcador/ui_medalla_platino.png"};
// consejos de la pantalla de carga (tarjeta de la tanda 4)
constexpr const char* kConsejos[][2] = {
    {"Rompe cajas, barriles y vasijas con tus técnicas: sueltan zeni, gemas y objetos.", "Break crates, barrels and jars with your techniques: they drop zeni, gems and items."},
    {"Gasta los zeni en la tienda (botón Y al elegir luchador): las mejoras se quedan para siempre.", "Spend your zeni in the shop (Y button when choosing a fighter): upgrades are permanent."},
    {"Gana, o aguanta hasta el minuto 10, para abrir el siguiente escenario.", "Win, or survive until minute 10, to unlock the next stage."},
    {"Las sombras y las marcas en el suelo avisan de lo que va a caer: ¡apártate!", "Shadows and marks on the ground warn you of what is about to fall: move away!"},
    {"Derrota enemigos sin parar para mantener la racha: da un poco más de experiencia.", "Keep defeating enemies to keep your streak going: it gives a bit more experience."},
    {"Una técnica al nivel 5 y su mejora pasiva pueden evolucionar en algo mucho más fuerte.", "A level 5 technique plus its passive can evolve into something much stronger."}};
constexpr int kNumConsejos = int(sizeof(kConsejos) / sizeof(kConsejos[0]));
// hojas de efectos: una fila de celdas cuadradas (lado = alto del PNG); cuadros, fps sugeridos y ancla fija de la celda
enum Hoja : uint8_t { kImpacto, kHumo, kRecoger, kSubidaFx, kExplosion, kGrieta, kPolvo, kCofreAbiertoFx /* pieza suelta, no hoja */ };
struct DefHoja { int cuadros; float fps, ax, ay, ancho; bool suelo; };   // ancho = mundo que ocupa una celda; suelo = debajo de las figuras
constexpr DefHoja kHojas[8] = {{6, 18, 64, 64, 1.6f, false},    {8, 14, 64, 104, 2.4f, false},  {6, 18, 48, 48, 1.2f, false},
                               {8, 12, 128, 128, 4.2f, false},  {10, 16, 192, 276, 9.0f, false}, {4, 6, 128, 148, 3.6f, true},
                               {6, 16, 48, 72, 1.3f, true},     {1, 0.8f, 64, 124, 1.4f, false}};   // el cofre abierto mide lo que el cerrado
constexpr int kVis = 160, kCifras = 40;   // efectos de hoja y cifras de dano a la vez (listas fijas; el efecto mas viejo deja sitio)
inline uint32_t Mezcla(int i, int j, uint32_t sal) {   // ruido por posicion (decorado determinista sin listas)
  uint32_t h = uint32_t(i) * 0x8DA6B343u ^ uint32_t(j) * 0xD8163841u ^ sal * 0xCB1AB31Fu;
  h ^= h >> 13, h *= 0x5BD1E995u, h ^= h >> 15;
  return h;
}

// ---------------------------------------------------------------- pruebas (variables DBZ3_TEST_SURVIVORS_*, leidas una vez)
struct Prueba {
  bool piloto = false, mando = false;
  int personaje = 0;
  float dura = 0;
  uint64_t semilla = 0;
  std::string escenario, guardado;   // tanda 4: escenario por nombre (salta el mapa); archivo de progreso de prueba
  int dificultad = 0, zeni = -1;
  bool otra = false;   // auditoria #14: cualquier otra DBZ3_TEST_SURVIVORS_* tambien es prueba
  bool Alguna() const { return piloto || mando || !guardado.empty() || zeni >= 0 || otra; }   // una prueba: nunca el progreso de verdad
};
inline const Prueba& Pr() {
  static const Prueba p = [] {
    const auto env = [](const char* k) { return std::getenv((std::string("DBZ3_TEST_SURVIVORS_") + k).c_str()); };
    Prueba r;
    const char* v = nullptr;
    r.piloto = (v = env("AUTO")) && std::atoi(v) > 0;
    r.mando = (v = env("MANDO")) && std::atoi(v) > 0;
    if ((v = env("PERSONAJE"))) r.personaje = std::clamp(std::atoi(v), 0, 15);
    if ((v = env("DURA"))) r.dura = float(std::max(0.0, std::atof(v)));
    if ((v = env("SEMILLA"))) r.semilla = std::strtoull(v, nullptr, 10);
    if ((v = env("ESCENARIO"))) r.escenario = v;
    if ((v = env("DIFICULTAD"))) r.dificultad = std::clamp(std::atoi(v), 0, 1);
    if ((v = env("ZENI"))) r.zeni = std::max(0, std::atoi(v));
    if ((v = env("GUARDADO"))) r.guardado = v;
    for (char** e = _environ; e && *e; ++e)   // auditoria #14: cualquier DBZ3_TEST_SURVIVORS_* es prueba
      if (std::strncmp(*e, "DBZ3_TEST_SURVIVORS_", 20) == 0) r.otra = true;
    return r;
  }();
  return p;
}

// Luchador de B3 de cada modelo de survivors.toml (fid -> b3, forma de mod center hd/tactics_manifest.toml).
// ponytail: tabla fija de los 7 fid del toml; si el toml usa otros, anadir aqui (o un campo b3 en el toml).
struct Modelo { int fid, b3, forma; };
constexpr Modelo kModelos[] = {{264, 0, 0},  {416, 7, 0},  {350, 11, 0}, {146, 33, 0}, {366, 21, 0}, {371, 42, 0},  {160, 43, 0},
                               // tanda 4: elites (Nappa, Dabura, Ginyu) y jefes (Freezer forma final, Super Buu, Cooler forma final)
                               {345, 19, 0}, {176, 37, 0}, {258, 20, 0}, {185, 27, 4}, {133, 35, 0}, {167, 38, 1},
                               // C01 (contenido_v2): Gohan, Krilin, Trunks futuro, Ten Shin Han, Goten (hace de Gotenks)
                               {235, 3, 0}, {327, 10, 0}, {390, 8, 0}, {400, 12, 0}, {298, 6, 0}};
inline Modelo ModeloDe(int fid) {
  for (const Modelo& m : kModelos)
    if (m.fid == fid) return m;
  REXLOG_WARN("dbz3 survivors: fid {} sin luchador de B3 en la tabla de modo.cpp (se usa Goku)", fid);
  return {fid, 0, 0};
}

inline std::atomic<bool> g_abierta{false}, g_panel_pedido{false}, g_pad_pendiente{false};
inline std::atomic<bool> g_fin_combate{false};   // el combate de fondo del sonido acabo (callback de pasarela::VigilarCombate, hilo del juego)
constexpr dbz3::GA kMgrPtr = 0x823D6CF4;    // gestor del combate (el de Dragon Tactics): +35569 aviso de ring out del rival
constexpr uint32_t kAvisoFuera = 35569;
constexpr int kEscenarioFondo = 0;          // el Torneo (no se ve: la partida tapa la pantalla entera)
constexpr int kSeImpacto = 2;               // banco 2 id 2: impacto (medido en un combate normal; el de Dragon Tactics)
constexpr int kSeExplosion = 23;            // SUR-07: banco 2 id 23 (data_cmn 3983): 0,9 s, el 94 % de la energia por debajo de 300 Hz (elegido por espectro, sin oirlo)
inline std::atomic<uint16_t> g_pad{0};
inline std::atomic<int> g_lx{0}, g_ly{0}, g_ry{0};   // g_ry: palanca derecha (zoom)

inline rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

// ---------------------------------------------------------------- dibujo comun
inline ImU32 Rgb(uint32_t rgb, float a) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, int(std::clamp(a, 0.0f, 1.0f) * 255.0f)); }
// la letra de los menus del juego (la de S::MenuTextB3); sin ella, la de ImGui
inline ImFont* Letra() {
  ImFont* f = dbz3::launcher::ui::GetFonts().menu;
  return f ? f : ImGui::GetFont();
}
inline ImVec2 Medir(float fs, const char* t, float wrap = 0) { return Letra()->CalcTextSizeA(fs, FLT_MAX, wrap, t); }
// texto con perfil negro suave (como TextoSuave de Tactics); wrap > 0 = ancho de ajuste
inline void Txt(ImDrawList* dl, float fs, ImVec2 p, ImU32 col, const char* t, float wrap = 0) {
  ImFont* f = Letra();
  const float g = std::max(1.0f, fs * 0.07f);
  const ImU32 borde = IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) * 180 / 255);
  static const float d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& o : d) dl->AddText(f, fs, ImVec2(p.x + o[0] * g, p.y + o[1] * g), borde, t, nullptr, wrap);
  dl->AddText(f, fs, p, col, t, nullptr, wrap);
}
inline void TxtMax(ImDrawList* dl, float fs, ImVec2 p, ImU32 col, const char* t, float max_w) {   // una linea: la letra baja hasta caber en max_w
  const float w = Medir(fs, t).x;
  if (w > max_w && w > 0) {
    const float f2 = fs * max_w / w;
    p.y += (fs - f2) * 0.5f;   // misma linea de centro
    fs = f2;
  }
  Txt(dl, fs, p, col, t);
}
inline void TxtC(ImDrawList* dl, float fs, ImVec2 c, ImU32 col, const char* t) {   // centrado en c
  const ImVec2 m = Medir(fs, t);
  Txt(dl, fs, ImVec2(c.x - m.x * 0.5f, c.y - m.y * 0.5f), col, t);
}
// rectangulo de proporcion fija (la del PNG) centrado en c con alto h
inline void Pieza(ImDrawList* dl, const char* pieza, ImVec2 c, float h, ImU32 col = IM_COL32_WHITE) {
  const ps2::Tex* t = Ui(pieza);
  if (!t || t->h <= 0) return;
  const float w = h * t->w / t->h;
  DibujarImagen(dl, t, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), col);
}
inline void IconoEn(ImDrawList* dl, const char* id, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE) { DibujarAjustada(dl, Icono(id), a, b, col); }
// quad de a a b con media anchura hw (px)
inline void Franja(ImDrawList* dl, ImVec2 a, ImVec2 b, float hw, ImU32 col) {
  float nx = -(b.y - a.y), ny = b.x - a.x;
  const float l = std::sqrt(nx * nx + ny * ny);
  if (l < 1e-3f) return;
  nx *= hw / l, ny *= hw / l;
  dl->AddQuadFilled(ImVec2(a.x + nx, a.y + ny), ImVec2(b.x + nx, b.y + ny), ImVec2(b.x - nx, b.y - ny), ImVec2(a.x - nx, a.y - ny), col);
}
inline void Reloj15(char* buf, size_t n, float minuto) {   // minuto de la linea de tiempo (0..15) -> "mm:ss"
  const int s = std::max(0, int(minuto * 60.0f));
  std::snprintf(buf, n, "%02d:%02d", s / 60, s % 60);
}
inline float Suave(float a) { a = std::clamp(a, 0.0f, 1.0f); return 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a); }

inline const char* IdOpcion(const Datos& d, const Opcion& o) {
  switch (o.tipo) {
    case TipoOpcion::Pasiva: return d.pasivas[size_t(o.def)].id.c_str();
    case TipoOpcion::Curar: return "curar";
    default: return d.tecnicas[size_t(o.def)].id.c_str();
  }
}
inline const char* Es(const Textos& x) { return T::T(x.es.c_str(), x.en.c_str()); }
inline const char* Desc(const Textos& x) { return T::T(x.desc_es.c_str(), x.desc_en.c_str()); }

enum class Pantalla : uint8_t { Carga, Eleccion, Mapa, Tienda, Partida, Resultados, Error, Portada, Coleccion, Logros, Clasificacion, ComoJugar, Opciones };
class Modo : public rex::ui::ImGuiDialog {
 public:
  explicit Modo(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}
  ~Modo() override {
    Bloquear(false);
    delete j_;
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // ---------------------------------------------------------------- estado general
  void Reiniciar();
  void Ir(Pantalla p);
  void Fallo(const char* texto);
  void Bloquear(bool si);
  // mando 1 (hilo de la interfaz, como Pad() de Tactics): botones recien pulsados; la palanca cuenta como cruceta para los menus y
  // queda en g_lx / g_ly para moverse. Las teclas del teclado llegan por el mismo mando emulado; Escape = B.
  uint16_t Pad();
  // palanca (con zona muerta) o cruceta, -1..1, y hacia arriba
  Entrada Mando() const;

  // ---------------------------------------------------------------- fondo de los menus: el suelo quieto con las capas de adorno
  void FondoMenu(ImDrawList* dl, float W, float H, float oscuro, const char* esc = "llanura");
  void Titulo(ImDrawList* dl, float W, float y, const char* t, float tam = 64) { S::MenuTextB3(dl, ImVec2(W * 0.5f, y * k_), tam * k_, t, true, false, W * 0.9f); }
  void Ayuda(ImDrawList* dl, float W, float H, const char* a, const char* b, const char* y = nullptr);
  // R23 SURV-6: sistema visual de los menus = el de resultados: titulo en el rotulo con laureles (zona de texto centrada, 736x160 de 1024x256)
  // y paneles oscuros con marco bronce (ui_panel_resultados)
  void Rotulo(ImDrawList* dl, ImVec2 c, float w, const char* t);
  void PanelMenu(ImDrawList* dl, ImVec2 a, ImVec2 b);

  // ---------------------------------------------------------------- tanda 4: progreso entre partidas (tienda.h)
  // El de verdad: UserDataRoot()/survivors_progreso.toml. En pruebas (piloto, mando, ZENI o GUARDADO) otro, para no tocar nunca el real.
  std::filesystem::path RutaProg() const;
  void LeerProg();
  void GuardarProg();
  void FinV2(const Juego& j);   // auditoria #13: logros de fin + cofres de la partida a los totales + guardado (una vez por partida)
  int EscenarioPrueba() const;   // DBZ3_TEST_SURVIVORS_ESCENARIO (por nombre); -1 = sin el

  // ---------------------------------------------------------------- 1. carga: atlas de los 6 enemigos + modelos 3D de los 3 luchadores y el jefe
  void Carga(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void PrecargaFormas(int& listos, int& total, bool& falla);
  bool PrecargaFx();
  // tarjeta de consejo (tanda 4, 1200x300 en nueve secciones de 28): el texto en su zona (31,42 + 1139x216); cambia cada 7 s
  void Consejo(ImDrawList* dl, float W, ImVec2 c);

  // ---------------------------------------------------------------- 2. eleccion de personaje
  void Eleccion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  // zeni acumulados (moneda + cifra), alineados a la derecha en p (centro vertical)
  void ZeniTotal(ImDrawList* dl, ImVec2 p, float fs, ImU32 col = IM_COL32(255, 230, 120, 255));

  // ---------------------------------------------------------------- 2b. mapa (tanda 4): escenario y dificultad
  // Tarjetas de escenario (512x288; la imagen de dentro la pone el juego, recortada a su image_rect) con una miniatura del suelo y sus
  // adornos; candado (res/mejoras/rogue, tanda 2) si no esta desbloqueado; la mejor medalla en la esquina. Insignias de dificultad debajo.
  void Mapa(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  // Miniatura de un escenario dentro de [a, b]: su suelo base (recortado sin deformar) y unos adornos suyos con su ancla, de atras adelante
  void Miniatura(ImDrawList* dl, int e, ImVec2 a, ImVec2 b);

  // ---------------------------------------------------------------- 2c. tienda (tanda 4): 10 mejoras permanentes con los zeni de las partidas
  void AbrirTienda();
  void Tienda(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);

  // ---------------------------------------------------------------- animacion de las figuras 3D (clips de B3 con mezcla, como Dragon Tactics)
  struct Anim {
    int clip = kClipEspera, prev = -1;
    float t0 = 0, t_prev = 0, mez0 = -100;   // inicio del clip, tiempo dentro del anterior al cambiar, inicio de la mezcla (reloj ta_)
    float fin = 0, dura = 0;                 // accion (ataque, tecnica, dano): hasta cuando manda y cuanto dura su clip (0 = el suyo)
    float giro = 0;                          // hacia donde mira mientras dura la accion
  };
  void Cambiar(Anim& a, int clip, float dura = 0.0f);
  bool Pintar3D(const Anim& a, const ps2::Camara& cam, const Modelo& m, int fid, float x, float y, float alto, float giro);
  // accion del jugador (pose de golpe o de tecnica, mirando hacia donde sale); `dura` = lo que se ve el clip
  void Accion(int clip, float dura, float dx, float dy);
  // clip de base del jugador (cada cuadro): andar / espera cuando no hay accion; derrota y victoria al acabar
  void AnimJugador();
  void AnimJefe();

  // ---------------------------------------------------------------- sonido
  // Menu: efectos del banco del sistema (0: 2 mover, 3 aceptar, 5 atras: hub_panel.h) por hub::QueueSe, que solo suena con el menu principal.
  // Combate (ajustes dbz3_survivors_sonido_combate y dbz3_survivors_combate_oculto, ENCENDIDOS de serie desde SUR-8): los golpes (banco 2 id 2) y los gritos de ataque (ids 42..50) y de KO (19..25)
  // de cada luchador solo suenan dentro de una pelea (combate.cpp los saca en su gancho). Como Dragon Tactics, al empezar la primera partida se
  // pide un combate NATIVO de fondo (escondido bajo la partida, que tapa la pantalla; rival quieto, sin marcador, sin tiempo) con el luchador
  // elegido y el jefe: sus voces quedan en las ranuras 3 y 4; las de los enemigos y los otros luchadores se cargan en ranuras libres
  // (combate::CargarVoces). Con el combate en marcha, los del sistema suenan por el mismo camino (banco 0). Al salir al HUB se fuerza el ring
  // out del rival y se espera a que el combate acabe (el juego vuelve al menu principal y el HUB reaparece; tope 60 s). El juego no tiene
  // efecto propio por tecnica en esos bancos (Tactics tampoco): cada tecnica suena con el grito de ataque del luchador.
  // Silencio en pruebas: DBZ3_TEST_SURVIVORS_SIN_SONIDO (o el de Tactics); con el, tampoco se pide el combate de fondo.
  static bool Silencio();
  // SUR-07: golpes y gritos por el decodificador XMA propio (sin combate oculto); falso con dbz3_survivors_combate_oculto (encendido de serie desde SUR-8)
  static bool SePropio();
  void Sonar(const char* cual, int id, float min_s);
  // Ranura del gestor de efectos con la voz del luchador b3 (3/4: los dos del combate de fondo; si no, la que cargo combate.cpp; -1 = aun no)
  int Ranura(int b3) const { return b3 == fondo_b3_[0] ? 3 : b3 == fondo_b3_[1] ? 4 : dbz3::combate::SlotVoz(b3); }
  // Efecto del combate de fondo: b3 < 0 = el impacto comun; b3 >= 0 = su voz con ese id. `ultimo`/`cada`: limite propio (s) de ese tipo,
  // ademas de la mezcla de voces de Tactics (6 a la vez, con sitio reservado para tecnicas y KO).
  void SonarCombate(const char* cual, tactics::PrioSe prio, double dura, int b3, int id, float& ultimo, float cada);
  int B3Jugador() const { return ModeloDe(datos_.personajes[size_t(j_->personaje)].fid).b3; }
  int B3Jefe() const { return ModeloDe(j_->DJ().fid).b3; }
  int GritoAtaque() { return 42 + int(n_voz_++ % 9u); }   // ids 42..50 del banco de gritos (tactics_units.toml)
  int GritoKo() { return 19 + int(n_voz_++ % 7u); }       // ids 19..25

  // Combate de fondo para el sonido (una vez por apertura; "otra partida" sigue con el mismo). Si el conector no lo acepta, sin sonido de combate.
  void PedirFondo(int b3_jugador);
  void VigilarFondo();
  // Salir al HUB (B en los menus, "Salir al HUB" en la pausa, B en los resultados): sin combate de fondo, al momento; con el, primero se acaba.
  void Salir();
  void Saliendo(ImDrawList* fondo, ImDrawList* dl, float W, float H, float dt);

  void Empezar(int pj, int esc, int dif);

  // ---- P2 (modo_pantallas.cpp): portada y estaciones del menu v2, subida de nivel v2
  void Portada(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void Coleccion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void Logros(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void Clasificacion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void ComoJugar(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  void Opciones(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  Pantalla Inicial() const;                  // Eleccion de siempre; Portada con dbz3_survivors_menu_v2 (y sin piloto/mando de prueba)
  void SubidaMando(uint16_t& pad, float dt);  // en Eligiendo, antes de leer izq/der/A: X/Y/LB (re-sortear, saltar, desterrar); puede quitar bits de pad
  EstadoP2 p2_;

  // ---------------------------------------------------------------- 3. partida
  void Partida(ImDrawList* fondo, ImDrawList* dl, ImGuiIO& io, float W, float H, float dt, uint16_t pad, Reloj::time_point t0);

  void Eventos();
  // ganchos de Eventos: cada suceso de la partida pasa tambien por aqui (P1 mundo, P2 pantallas, P3 audio; vacios de serie)
  void MundoAlEvento(const Evento& e);
  void PantallasAlEvento(const Evento& e);
  void AudioAlEvento(const Evento& e);

  // piloto de pruebas: huye de los enemigos cercanos (y del jefe) y va a por la gema mas cercana
  Entrada Piloto();

  // ---------------------------------------------------------------- dibujo del mundo
  struct Vista {   // camara del cuadro
    float S, cx, cy, sp, cp, jx, jy;
    ImVec2 P(float x, float y) const { return ImVec2(cx + S * (x - jx), cy - S * sp * (y - jy)); }
  };
  static uint32_t Bits(float sy);   // y de pantalla -> clave que ordena como el float

  // Zoom de la camara (usuario, 11/11): rueda del raton, teclas + / - (o Re Pag / Av Pag) y palanca derecha arriba / abajo.
  // 1 = lo mas lejos (mas lejos se veria el anillo donde aparecen los enemigos); por encima de ~2 los impostores (celda de 96 px)
  // se ven borrosos. ponytail: tope 2,2 con atlas de 96 px; hornear a 128 px si se quiere acercar mas.
  // Como llega cada cosa (12/11): la rueda y las teclas de verdad entran por la ventana al dibujante de ImGui (ImGuiDrawer::OnMouseWheel /
  // OnKey -> io.MouseWheel, ImGuiKey_*; el driver de teclado y raton no las marca como usadas). Las teclas que inyecta tools/pruebas.py con
  // j.tecla (dbz3_input.req) solo entran al MANDO emulado (mnk_input_driver: key_down -> palanca/botones), nunca a ImGui: por eso en la
  // prueba de 11/11 + / - / Re Pag / Av Pag "no llegaron" y las flechas (palanca derecha emulada) si. Para no depender de que ImGui reciba la
  // tecla, se mira tambien el teclado de Windows con la ventana del juego delante (GetAsyncKeyState; sin sumar dos veces).
  static bool TeclaWin(int vk);
  void Zoom();

  void Mundo(ImDrawList* dl, float W, float H);
  // Presentacion del jefe (tanda 4): retrato (marco de 256 con la zona segura 32,32 + 192x192) a la izquierda del rotulo (1280x360, nombre
  // en 176,110 + 928x170), el grupo centrado a un 30 % del alto; entra, se queda y se va en 3,4 s
  struct Pres { float a; ImVec2 rc; float rs; ImVec2 pc; float ps; };
  bool PresGeom(float W, float H, Pres* o) const;
  // arte del rompible (tanda4/02_rompibles/rompible_<id>_{entero,roto}.png), a la cache por tipo
  const ps2::Tex* RompTex(int tipo, bool roto);

  void Enemigos(ImDrawList* dl, int a, int b);

  // ---------------------------------------------------------------- efectos con las texturas del combate de B3 (las de Dragon Tactics)
  // native_ui las saca del data_cmn del jugador: fx_* (estallido, onda, nucleo, ovalo suave, brillo, chispas) y tkfx/<id>/bola|rayo (la bola y
  // el rayo de energia de cada personaje tal como los liga el juego: el Kamehameha de Goku, la bola de Vegeta, la de Cell). Sin ellas (falta
  // data_cmn) se queda el dibujo de primitivas de siempre. Makankosappo, Kienzan y el aura no tienen textura propia en esa tabla: se hacen con
  // las piezas comunes del juego (ovalo, onda, nucleo) en su forma.
  void CargarFx();

  // ---------------------------------------------------------------- POST2 12/11: particulas copiadas de los #ACE de data_cmn
  // Lectura: E:\DBZ3HD_trabajo\tmp\post2_fx\resumen.py (formato de iw_ame.h, el mismo de B3 en big-endian). Por particula: vida (cuadros),
  // tamano base + azar (+0x5C/+0x60; medio = 0,5 (base + azar/2)), escala al nacer -> al morir (+0x68/+0x6C), color de vertice al nacer ->
  // al morir (+0x80/+0x90) y su cuadro del atlas (+0xC8: u0 v0 u1 v1). Mezcla (+0xA0/+0xC0) = 1 aditiva en todas las de aqui.
  //   golpe = banco 0 (data_cmn 504) #ACE 1: rayas radiales (rayo, tipo 2) en el atlas t2;
  //   bola  = banco 0 #ACE 9: cabeza de la rafaga de ki (codigo 350 de Goku, enlace (0,9)) en el atlas t15, estirada en su velocidad;
  //   rayo  = BSP 533 AMB0 #ACE 2: cabeza del Kamehameha (codigo 0, enlace (2,2)), t31/t32 cuadros girados y t33 rayas;
  //   onda  = banco 0 #ACE 8, la particula del anillo (fx_anillo): color y alfa; el radio sigue al del juego (zona de dano).
  struct PartB3 { float vida, tam, azar, e0, e1, c0[4], c1[4], uv[4]; };
  static constexpr PartB3 kGolpeB3[] = {
      {6, 7.5f, 6.0f, 2.0f, 1.6f, {1, 1, 1, 1}, {1, 0.86f, 0.55f, 1}, {0, 0, 0.5f, 0.5f}},
      {8, 6.0f, 5.5f, 2.4f, 2.0f, {1, 0.86f, 0.55f, 1}, {1, 0.67f, 0.47f, 0}, {0, 0.5f, 0.5f, 1}},
      {6, 5.0f, 4.0f, 2.5f, 2.0f, {1, 1, 1, 1}, {1, 0.86f, 0.55f, 1}, {0.5f, 0, 1, 0.5f}},
      {8, 4.0f, 3.0f, 3.0f, 2.5f, {1, 0.86f, 0.55f, 1}, {1, 0.67f, 0.47f, 0}, {0.5f, 0.5f, 1, 1}}};
  static constexpr PartB3 kBolaB3[] = {
      {5, 10.0f, 16.0f, 3.0f, 2.0f, {0.59f, 0.86f, 1, 1}, {0, 0.59f, 1, 0.78f}, {0, 0, 0.5f, 0.5f}},
      {4, 16.0f, 18.0f, 4.0f, 3.0f, {0, 0.59f, 1, 0.78f}, {0, 0.59f, 1, 0.2f}, {0, 0.5f, 0.5f, 1}},
      {5, 3.0f, 7.0f, 3.5f, 2.0f, {1, 1, 1, 1}, {0, 0.71f, 1, 0.78f}, {0.5f, 0, 1, 0.5f}},
      {4, 7.0f, 9.0f, 5.0f, 3.5f, {0, 0.71f, 1, 0.78f}, {0, 0.71f, 1, 0.2f}, {0.5f, 0.5f, 1, 1}}};
  static constexpr PartB3 kKameCabezaB3[] = {   // t31, t32 (cuadros girados) y t33 (raya)
      {3, 16.0f, 0, 1, 1, {0.78f, 0.86f, 1, 0.88f}, {0.78f, 0.86f, 1, 0.75f}, {0, 0, 1, 1}},
      {2, 19.0f, 0, 1, 1, {0, 0.88f, 1, 0.94f}, {0, 0.66f, 1, 0.37f}, {0, 0, 1, 1}},
      {4, 13.0f, 25.0f, 2.0f, 5.0f, {0, 0.71f, 1, 0.63f}, {0, 0.9f, 1, 0.6f}, {0, 0, 1, 1}}};
  static constexpr PartB3 kOndaB3 = {20, 2.5f, 0, 1, 40, {0.87f, 0.95f, 1, 0.49f}, {0.85f, 0.95f, 1, 0}, {0, 0, 1, 1}};
  static constexpr float kUnidadB3 = 0.09f;   // v.S por unidad de B3 (luchador: ~20 u alli, ~1,8 v.S aqui). ponytail: medido a ojo, no del juego
  static ImU32 ColB3(const PartB3& p, float k, float mult = 1.0f);
  static float MedioB3(const PartB3& p, float k, float px);   // medio lado en px a la edad k (0..1 de su vida)
  static void Aditiva(ImDrawList* dl, bool si) { dl->AddCallback(si ? rex::ui::kImDrawBlendAdditive : rex::ui::kImDrawBlendAlpha, nullptr); }
  // raya (tipo 2): su recorte del atlas a lo largo de a->b, ancho = la mitad del largo
  static void RayaB3(ImDrawList* dl, const S::Pic& t, const PartB3& p, ImVec2 a, ImVec2 b, float hw, ImU32 col);
  // golpe de B3 en c: 8 rayas radiales (las 4 particulas del #ACE 1, dos de cada) a la edad 'cuadros' (60 por segundo)
  void GolpeB3(ImDrawList* dl, ImVec2 c, float px, float cuadros, float giro);
  const S::Pic& Usa(const S::Pic& p, int bit, const char* nombre);   // registro: la primera vez que se ve cada textura del juego
  static void Spr(ImDrawList* dl, const S::Pic& p, ImVec2 c, float w, float h, ImU32 col);
  // banda de a a b con media anchura hw: perfil = columna central de la textura de lado a lado (ovalo suave -> resplandor); tira = la
  // textura a lo largo (u de u0 a u1), como el rayo de Tactics
  static void Banda(ImDrawList* dl, const S::Pic& p, ImVec2 a, ImVec2 b, float hw, ImU32 col, bool tira = false, float u0 = 0, float u1 = 1);
  Arquetipo ArqDe(const Efecto2D& e) const { return e.def < datos_.tecnicas.size() ? datos_.tecnicas[e.def].arq : Arquetipo::Teledirigida; }

  bool SueloNativo(ImDrawList* dl, const Vista& v, const Efecto2D& e);

  // Rayos (Kamehameha, Makankosappo): el origen en las manos, kManos de mundo delante del luchador hacia donde sale (antes, en su centro: la
  // bola tapaba al luchador con el zoom alto); RayoDetras = sale hacia arriba en pantalla (se aleja de la camara: va detras del luchador).
  static constexpr float kManos = 0.55f;
  static ImVec2 OrigenRayo(const Vista& v, const Efecto2D& e, float alto);
  static bool RayoDetras(const Efecto2D& e) { return e.y2 > e.y; }

  bool AireNativo(ImDrawList* dl, const Vista& v, const Efecto2D& e);

  // chispas de los golpes (la textura de golpe del juego), pequenas y breves
  void Chispas(ImDrawList* dl, const Vista& v);

  // Aura, anillos, conos y avisos del jefe: en el suelo, debajo de las figuras
  void EfectoSuelo(ImDrawList* dl, const Vista& v, const Efecto2D& e);
  // pieza repetible en UV (aviso_zona_rectangular) cubriendo el rectangulo de mundo [x0, x1] x [y0, y1] con teselas de `tesela` de lado;
  // cada esquina proyectada con la camara (la ultima tesela, recortada). v = 0 arriba (+y del plano)
  void TeselaSuelo(ImDrawList* dl, const Vista& v, const ps2::Tex* t, float x0, float y0, float x1, float y1, float tesela, ImU32 col);

  // Bolas (con halo), discos, rayos (cuadro con nucleo blanco y borde de color) y explosiones: encima de todo
  void EfectoAire(ImDrawList* dl, const Vista& v, const Efecto2D& e);

  // ---------------------------------------------------------------- marcador (en los bordes: nunca sobre el jugador, centro de la pantalla)
  void Ficha(ImDrawList* dl, const char* icono, ImVec2 a, float lado, int nivel, bool evo, bool max, float recarga = 0);

  void Marcador(ImDrawList* dl, float W, float H, float dt);

  // ---------------------------------------------------------------- 4. subida de nivel
  void Subida(ImDrawList* dl, float W, float H);

  // ---------------------------------------------------------------- 5. pausa y resultados
  void Pausa(ImDrawList* dl, float W, float H);

  void Resultados(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);
  bool DerrotaNativa(ImDrawList* dl, float W, float H);

  void Mensaje(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad);

  // ---------------------------------------------------------------- tanda 3: arte, efectos de hoja, cifras y avisos
  // Textura de la tanda 3 al primer uso (hilo de dibujo); la que falte se reintenta cada 2 s (sin una llamada al disco por cuadro).
  const ps2::Tex* T3(int i);
  void Precarga(int tope);   // en los menus: unas pocas por cuadro, para que la partida no empiece con un tiron
  // 13/11 carga perezosa (cada cuadro, antes de la pantalla): en la carga solo se esperan los modelos de los luchadores (la eleccion los
  // dibuja en vivo); los enemigos (impostores) esperan a que esten y se hornean despues, en segundo plano y con tope por cuadro
  // (Impostores::Avanzar), tambien en los menus y la partida. Sale de Carga cuando estan los luchadores o tras kEsperaLuchadores s.
  void CargaPerezosa();
  // tanda 4: el decorado, los rompibles y el ambiente de un escenario (en los menus, `tope` piezas por cuadro; quedan en la cache de recursos)
  void PrecargaEscenario(int e, int tope);
  // ancla (px del PNG) en p con escala px de pantalla / px del PNG
  void Pieza3(ImDrawList* dl, int i, ImVec2 ancla, ImVec2 p, float escala, ImU32 col = IM_COL32_WHITE) { DibujarCuadro(dl, T3(i), 0, 1, ancla, p, escala, col); }

  // franja de carga en el suelo de (x0, y0) a una fraccion `hasta` de (x1, y1): la pieza de 256x64 repetida SOLO en X (tramos enteros de
  // textura, el ultimo recortado) con su ancho visible (56 de 64 px) = `ancho`; cada esquina se proyecta con la camara
  void FranjaSuelo(ImDrawList* dl, const Vista& v, const ps2::Tex* t, float x0, float y0, float x1, float y1, float ancho, float hasta, ImU32 col);

  void Lanzar(uint8_t hoja, float x, float y, float alto, float mult = 1.0f, bool sigue = false, bool real = false);
  // efectos de hoja: celda y ancla fijas, cuadros de izquierda a derecha a sus fps; los de suelo debajo de las figuras
  void Hojas(ImDrawList* dl, const Vista& v, bool suelo, float W, float H);
  // 12/11: golpes, explosiones y ondas con las texturas del combate de B3 (data_cmn) en lugar de las hojas dibujadas de la tanda 3; humo
  // y polvo no existen en el combate de B3: no se pintan. u = 0..1 de la vida del efecto; lado = px que ocupaba la celda de la hoja.
  // true = tratado (pintado o descartado); false = sin texturas del juego: la hoja de siempre.
  bool HojaNativa(ImDrawList* dl, const Vista& v, const auto& f, float u, float lado, float W, float H);   // f: Vis (declarado despues)
  // cifras de dano: suben y se apagan en 0,75 s; blancas, amarillas (y algo mayores) en los golpes fuertes
  void Cifras(ImDrawList* dl, const Vista& v);
  void Cifra_(float x, float y, float dano);
  void Aviso(const char* texto, bool jefe);   // rotulo de oleada (texto del motor encima del arte)

  // Decorado del escenario (determinista por posicion; tanda 3 en la llanura, generalizado en la tanda 4 con [escenario.decorado] del toml):
  // zonas de 24 x 24 con una de las variantes de suelo (la base de siempre o una de 'suelos'; la textura se repite cada 12 y se funde 6
  // hacia la zona vecina distinta con alfa por vertice: sin costura), manchas de transicion en los bordes entre zonas y sueltas, adornos
  // planos (crateres, grietas, losas, dunas) con el suelo y los demas a la lista de figuras (orden por los pies).
  static constexpr float kZona = 24.0f, kCeldaMancha = 11.0f, kCeldaAdorno = 5.5f;
  static constexpr int kDecoMancha = 3, kDecoAdorno = 11, kDecoPiezas = 35;   // indices en deco_tx_: 3 suelos, 8 manchas, 24 adornos
  const DefDecorado& Deco() const { return j_->Escenario().deco; }
  const ps2::Tex* DecoTex(int k);   // al primer uso; la que falte se reintenta cada 2 s
  // 0 = el suelo base (ya pintado debajo); 1..3 = los de 'suelos' (con 3, las cuatro variantes por igual, como la llanura de la tanda 3)
  int Variante(int i, int j) const { return int(Mezcla(i, j, 11) % uint32_t(1 + std::min<size_t>(3, Deco().suelos.size()))); }
  void SueloZona(ImDrawList* dl, const Vista& v, int zi, int zj, const ps2::Tex* t);
  void Mancha(ImDrawList* dl, const Vista& v, float x, float y, uint32_t h, float ancho);
  // Devuelve cuantos adornos ha puesto en spr_[m..] (con su clave de profundidad).
  int Decorado(ImDrawList* dl, const Vista& v, float W, float H, int m);

  // flecha en el borde de la pantalla hacia algo que queda fuera (jefe, cofre); la pieza mira hacia arriba y se gira entera
  // p = pies del objetivo; (semi, alto) = su media anchura y su alto en pantalla: si se ve algo de el, no hay flecha. Las flechas viven en el
  // marco libre entre el marcador de arriba (barras de exp y vida, y la del jefe si esta), las casillas de abajo y los lados.
  bool Flecha(ImDrawList* dl, int pieza, ImVec2 p, float W, float H, float semi, float alto, ImVec2* donde = nullptr);   // true = dibujada

  // Minimapa (tanda 4): marco redondo de 256 (hueco transparente de radio 101 en 128,128) con el mapa debajo, recortado a ese circulo:
  // el jugador en el centro (triangulo hacia donde mira), el jefe y los cofres (al borde si quedan lejos) y las elites cercanas.
  void Minimapa(ImDrawList* dl, ImVec2 c, float lado);

  // ---------------------------------------------------------------- miembros
  Datos datos_;
  bool datos_ok_ = false, cargado_ = false, imp_definidos_ = false;
  Juego* j_ = nullptr;
  Impostores imp_;
  ps2::Escena esc_;
  Pantalla pant_ = Pantalla::Carga;
  std::string error_;
  bool dentro_ = false, soltado_ = false, bloqueado_ = false, pausa_ = false, opc_dicha_ = false;
  uint16_t ultimo_ = 0;
  int sel_ = 0, op_sel_ = 0, pausa_sel_ = 0;
  bool zoom_tocado_ = false;   // auditoria #6: el usuario ya eligio zoom (no se fuerza el 1,8 de serie)
  float zoom_ = 1.5f, zoom_dicho_ = 1.5f;   // de serie mas cerca que antes (1,0): las figuras y sus animaciones se aprecian
  float k_ = 1, t_ = 0, t_pant_ = 0, t_opc_ = 0, acum_ = 0, giro_ = 0, flash_ = 0, aviso_jefe_ = -1;
  // dibujo de la horda y de los caidos (sin memoria nueva por cuadro)
  static constexpr int kSpr = Sim::kMax + kCaidos + kMaxObjetos + kMaxRompibles + 128;   // + objetos, rompibles y adornos (los visibles: ~10-40)
  uint64_t claves_[kSpr] = {};
  Sprite spr_[kSpr];
  float dest_[kSpr] = {}, alfa_[kSpr] = {};
  struct Caido { float x, y, giro, t0; int tipo; };
  Caido caidos_[kCaidos] = {};
  int n_caidos_ = 0;
  uint32_t caido_dicho_ = 0;
  struct Chispa { float x, y, t0; };
  Chispa chispas_[kChispas] = {};
  int n_chispas_ = 0, golpes_paso_ = 0;
  // animacion (reloj ta_: solo corre jugando y en el final) y sonido
  Anim pj_, jefe_;
  float ta_ = 0, t_fin_ = 0, t_tecnica_ = -100, jefe_golpe_ = -100, t_sonido_ = -100;
  bool fin_ = false;
  bool sur2_cierre_dicho_ = false;   // una marca por pantalla, se rearma al cambiar de pantalla
  int log_clip_[8] = {}, n_sonidos_ = 0;
  // combate de fondo para el sonido (dbz3_survivors_sonido_combate)
  bool combate_ = false, combate_pedido_ = false, saliendo_ = false;
  float t_salida_ = 0;
  int fondo_b3_[2] = {-1, -1};   // luchadores del combate de fondo: sus voces van en las ranuras 3 y 4
  tactics::MezclaSe se_;
  float t_golpe_se_ = -100, t_ko_se_ = -100, t_voz_se_ = -100, t_dano_se_ = -100, t_jefe_se_ = -100, t_explosion_se_ = -100;
  int n_se_combate_ = 0;
  uint32_t n_voz_ = 0;
  uint32_t zoom_via_ = 0;   // bits: rueda, teclado, palanca derecha (registro una vez por apertura)
  int pase_rayo_ = 0;       // 1 = pase de los rayos debajo del luchador (origen y los que se alejan); 0 = el de siempre, encima
  // efectos del combate de B3 (native_ui: data_cmn del jugador), pedidos una vez por cuadro
  struct Fx {
    S::Pic impacto, anillo, nucleo, halo, brillo, chispas, kame_rayo, kame_bola, vegeta_bola, jefe_bola, propia_bola;
    S::Pic golpe, kibola, kame_cabeza, kame_brillo, kame_raya;   // POST2: las de los #ACE exactos (ver kGolpeB3...)
  } fx_;
  uint32_t fx_dicho_ = 0;
  // medidas
  int cuadros_ = 0;
  double dt_suma_ = 0, peor_ = 0, cpu_suma_ = 0, sim_suma_ = 0, fx_suma_ = 0;
  float t_estado_ = 0, t_mando_ = 0;
  double deco_suma_ = 0, hojas_suma_ = 0;
  // tanda 3
  const ps2::Tex* tx_[kNumArte] = {};
  float tx_reintento_[kNumArte] = {};
  struct Vis { float x, y, alto, t0, mult; uint8_t hoja; bool sigue, real; };   // efecto de hoja (real: con el reloj t_, sigue en la eleccion)
  Vis vis_[kVis] = {};
  int n_vis_ = 0;
  struct Cifra { float x, y, t0; int valor; bool fuerte; };
  Cifra cifras_[kCifras] = {};
  int cifras_paso_ = 0, humos_paso_ = 0, destellos_paso_ = 0, min_dicho_ = 0;
  char aviso_txt_[128] = {};
  bool aviso_rojo_ = false, eleccion_cofre_ = false;
  EstadoJefe jefe_prev_ = EstadoJefe::Ausente;
  float t_polvo_ = 0;
  uint32_t recogido_dicho_ = 0;
  Vista vista_{};
  // tanda 4
  Progreso prog_;
  int escn_ = 0, dif_ = 0, map_sel_ = 0, dif_sel_ = 0, tienda_sel_ = 0, nuevo_esc_ = -1;
  float map_aviso_ = -100, tienda_msg_ = -100, pres_t_ = -1;
  bool tienda_ok_ = false;
  const ps2::Tex* deco_tx_[kDecoPiezas] = {};
  float deco_rt_[kDecoPiezas] = {};
  const ps2::Tex* romp_tx_[kMaxRompDefs][2] = {};
  float romp_rt_[kMaxRompDefs][2] = {};   // auditoria #10: proximo reintento de la textura
  int n_rotos_log_ = 0, n_rayos_log_ = 0, precarga_[kMaxEscenarios] = {};
  double amb_suma_ = 0, mini_suma_ = 0;
};

}  // namespace dbz3::survivors::interno
