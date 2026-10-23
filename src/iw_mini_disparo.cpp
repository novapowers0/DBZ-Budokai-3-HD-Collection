// dbz3 - Minijuegos de DISPARO del mapa de Dragon Mission de Infinite World (frente R13, ronda 4, 2026-10-16).
// Registrados en iwmini::RegistrarModulo (src/iw_minijuegos.h). Ver docs/14_frentes/RONDA4_PULIDO.md ("R13").
//
// Hecho: 9 (0x7D8) "Fire beams at Piccolo! Go easy on him, though!" (modulo MSN010). Del decomp EU de IW
// (E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c, Viewer_* 0x3245D0-0x327F90 y 0x32B2A0-0x32E8xx):
//  - Primera persona: apuntar = girar la camara (Camera_OrbitInputUpdate 0x3245D0): 0.0165 rad por cuadro (0.01 desde
//    el 3.er disparo), zona muerta 32 de la palanca o la cruceta; inclinacion entre 20 y 160 grados. Reticula en el
//    centro (DATA_USA 9/1 t01), que cambia de dibujo con un hueso de Piccolo dentro (10 px + su radio en pantalla).
//  - Disparo (bit 0x2000, circulo = B): rayo de una mano y luego de la otra, hacia el punto a 60 delante de la camara,
//    a 8 por cuadro; acierta a distancia <= 1 + radio del hueso; falla a mas de 80 de la camara (s_HBCORE_MAIN).
//  - Piccolo orbita a 60 + 10 sin(fase) (fase + 0.1 por cuadro) y +-45 grados; esquiva de lado (direccion 0..3 al azar,
//    distinta de la anterior; 2.0, o 2.25 con vida < 40 %); con vida < 25 % se queda quieto.
//  - Vida por dificultad {900, 950, 1000, 1050, 1100, 1200}; dano por grupo de huesos (tabla 0x499A60): cabeza 40,
//    tronco 3, brazos 12, piernas 10. A 0: derribo. Tiempo {180, 160, 140, 120, 110, 90} s (el "130" de la captura).
//  - Rango por segundos hasta el derribo (Menu_ClassifyValueByThreshold, tabla 999/75/60/45/30): <= 30 Z (10000),
//    <= 45 S (8000), <= 60 A (6000), <= 75 B (5000), si no C (3000); x {0.6, 0.8, 1.0, 1.3, 1.6, 2.0} por dificultad,
//    redondeado a la centena. Sin derribo: fallo, 100 puntos (Viewer_StoreResultToConfig 0x324C50).
// En el motor de B3: combate propio (hub::RequestBattleEx) con Piccolo de B3 (el rival, muneco quieto) y el jugador
// oculto (primera persona); las manos son los modelos de IW ITEMB04/ITEMB05 (DATA_CMN 892) y el HUD el de IW (DATA_USA
// 9/1). Los impactos se miden contra las 22 esferas de cuerpo del Piccolo de B3 (X+1616 + 16 i; mismo orden que los
// 22 huesos de IW en 0-3: cintura, pecho, cuello, cabeza) con los radios de la tabla de IW.
//
// Hecho tambien: 8 (0x7D7) "Attack Frieza with Destructo-Discs!" (MSN009; reglas en CuadroDisco) y 15 (0x7DE) "Tien!
// Stop Cell!" (MgShoot_* + la IA de Cell; reglas en Cuadro15). Escenarios: los de B3 que IW comparte (Namek 7 para el 8 y
// el 9, Archipielago 2 para el 15). Solo RE: 3 (0x7D3) los ladrillos (motor de vuelo MgFly_* + #MSS 890/4; con R12).
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "reloj_prueba.h"
#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "cine.h"
#include "combate.h"
#include "escenarios.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "iw_ame.h"
#include "iw_minijuegos.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "ps2_modelo.h"

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3

namespace dbz3::iwdisparo {
namespace {

namespace P = dbz3::pasarela;
namespace S = dbz3::ingame::style;
using ps2::V3;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kFin = 35569, kVida = 6080, kVidaMax = 6084;
constexpr uint32_t kOcultoA = 3261, kOcultoB = 4925, kPadIdx = 5028;
constexpr uint32_t kCopias[3] = {48, 64, 80};
constexpr int kKrilin = 10, kPiccolo = 11, kTen = 12, kFreezer = 27, kCell = 33, kNamek = 7, kIslas = 2;
constexpr uint16_t kDisparo = 0x2000;                      // registro +12: circulo (B)
constexpr uint16_t kArriba = 0x10, kDerecha = 0x20, kAbajo = 0x40, kIzquierda = 0x80;   // +108

// MSN010 (Viewer_InitParamTables y tablas del ELF; ver cabecera)
constexpr int kVidaPicc[6] = {900, 950, 1000, 1050, 1100, 1200};
constexpr int kLimite[6] = {180, 160, 140, 120, 110, 90};
constexpr float kMult[6] = {0.6f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f};
constexpr int kBase[5] = {3000, 5000, 6000, 8000, 10000};   // rango 0 (C) .. 4 (Z)
constexpr float kUmbral[5] = {999, 75, 60, 45, 30};
// Huesos de IW (0x499A60): grupo y radio; dano por grupo
constexpr int kGrupo[22] = {1, 1, 0, 0, 4, 5, 4, 5, 2, 3, 2, 3, 4, 5, 2, 3, 2, 3, 1, 0, 1, 0};
constexpr float kRadioHueso[22] = {2.75f, 1.65f, 1.1f, 1.1f, 2.75f, 2.75f, 2.2f, 2.2f, 1.65f, 1.65f, 1.65f,
                                   1.65f, 2.2f,  2.2f,  1.65f, 1.65f, 1.76f, 1.76f, 2.2f, 2.53f, 2.2f, 2.53f};
constexpr int kDano[6] = {40, 3, 12, 12, 10, 10};
constexpr float kOrbita = 60.0f, kRayoVel = 8.0f, kRayoMax = 80.0f, kRayoMira = 60.0f;
// ponytail: altura del ojo, sitio de las manos, cadencia de cada mano y los limites de altura de Piccolo no estan
// leidos (dependen de las animaciones de IW); valores a ojo.
constexpr float kOjo = 9.0f, kCadencia = 18.0f, kElevMax = 0.35f;
// MSN009 (Viewer_InitRoundTables 0x34B800, tabla 0x5D9B44 + 0x18 r): discos, % de esquiva al apuntarle (B50), paso
// angular de la esquiva (B48); 180 s; gana con 7 aciertos; bases por banda {4000, 5000, 6000, 8000, 10000}
constexpr int kTiros[6] = {12, 12, 11, 10, 9, 8};
constexpr int kB50[6] = {45, 50, 60, 65, 70, 75};
constexpr float kB48[6] = {0.021f, 0.021f, 0.022f, 0.022f, 0.023f, 0.023f};
constexpr int kBaseDisco[5] = {4000, 5000, 6000, 8000, 10000};
// MSN016 (Scene_InitDefaultParams EU 0x30D150; tablas USA 0x47D860 + 0x20 k, EU 0x47E010): tiempo, energia inicial 1,
// divisor del rango 1, campo +0xC = 3 (Cell corriendo, por cuadro), +0x10 = subida de Cell por cuadro, multiplicador;
// puntos por rango 0x5D9392; altura de Ten = 400 (controlador +0x84).
constexpr int kTiempo15[6] = {40, 60, 80, 100, 120, 160};
constexpr float kSubida15[6] = {3.0f, 3.0f, 3.5f, 3.5f, 4.0f, 4.0f};
constexpr int kBase15[5] = {3600, 4200, 4800, 5400, 6000};
constexpr float kAltura15 = 400.0f;
constexpr float kEsc3 = 1.0f;   // 3: unidades de IW = las de B3 (el campo 897 de R8 va a escala 1)
constexpr int kGoku = 0, kRaditz = 18, kLlanuras = 5;
// MSN009 con sus distancias de IW: R16 (sub_82114910) ya no acerca a los luchadores durante el minijuego, asi que
// no hace falta el x0.4 que compensaba el tope de B3. Los angulos y los radios de los huesos van tal cual.
constexpr float kEsc9 = 1.0f, kRadioFz = 150.0f * kEsc9, kDiscoVel = 5.0f * kEsc9, kDiscoRadio = 5.0f * kEsc9;

float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
void Poner(uint8_t* base, uint32_t x, const V3& p) {
  for (uint32_t o : kCopias) {
    StoreF(base, x + o, p.x);
    StoreF(base, x + o + 4, p.y);
    StoreF(base, x + o + 8, p.z);
  }
}
uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}
V3 Mas(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Por(const V3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Largo(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
V3 Dir(float yaw, float polar) {   // polar desde +Y (90 grados = horizontal); yaw 0 = +Z
  return {std::sin(polar) * std::sin(yaw), std::cos(polar), std::sin(polar) * std::cos(yaw)};
}

// ---- Efectos de IW (#AME v2): reproductor en iw_ame.h
namespace ame = dbz3::iwame::ame;

struct Datos {
  int clave = 0, fila = 0, dif = 2;
  int juego = 9;                       // 9: MSN010 (Piccolo), 8: MSN009 (Freezer y los Kienzan), 15: MSN016
  ps2::Modelo mano_i, mano_d;          // DATA_CMN 892/1 ITEMB04 y 892/3 ITEMB05 (texturas 892/0 y 892/2)
  // Proyectil, modelo de IW: el rayo rosa de Freezer (DATA_CMN 842/7 BEAM + CORE, texturas 842/8) o el Kienzan
  // (851/6 KIENCORE, texturas 851/2). ponytail: el minijuego los pinta con efectos #AME de su paquete (DATA_CMN 935 =
  // 0x3A7, Effect_OpenBundleForMode con modo 15/9/8/3: atlas 935/0, mallas MODEL_L00/L01/L03_EFT 935/1 y 21 #AME) cuyo
  // formato no esta leido: se usa el modelo de la tecnica de IW.
  ps2::Modelo efecto;
  // 3 (ladrillos): el bloque del #MSS 890/4 de su dificultad
  std::vector<std::array<float, 7>> tramos;
  int n_tramos = 0, umbral[5] = {}, puntos[5] = {}, fallo = 100;
  float lad_s = 18.0f;   // #MSS bloque +0x08: tamano que se proyecta para el radio de fijado (18/18/18/10/6)
  ps2::Modelo ladrillo;
  // efectos de IW: el paquete de los minijuegos de disparo (DATA_CMN 935 = 0x3A7, Effect_OpenBundleForMode; en el 3 su
  // copia 890/6): atlas (hijo 0) y los #AME (hijos 3..23) por indice de hijo
  std::vector<P::Imagen> atlas;
  std::vector<ame::Efecto> efectos;
  std::vector<P::Imagen> hud;          // DATA_USA 9/1 (HUD de los minijuegos de disparo)
  P::Imagen clear;                     // DATA_USA 334/3 t69 "CLEAR" (el mismo final que los de P4)
};
std::mutex g_mu;
std::shared_ptr<Datos> g_d;
std::atomic<bool> g_on{false}, g_fin_pedido{false};
iwmini::Resultado g_res;
bool g_auto = false;   // prueba: apunta y dispara solo
// hilo del juego <-> interfaz
std::atomic<float> g_yaw{0}, g_polar{1.5708f};
std::atomic<float> g_px{0}, g_py{kOjo}, g_pz{kOrbita};
std::atomic<uint32_t> g_mando{0};   // +12 (16 bits) | +108 << 16
std::atomic<int> g_palanca{0};      // +26 | +27 << 8
std::atomic<bool> g_suelo{false};   // derribado: Piccolo cae (ya no se le sostiene en el aire)
// MSN016: camara libre (ojo y mirada), sitio del jugador oculto y el luchador de Cell (para anim::Corredor)
std::atomic<bool> g_cam15{false};
std::atomic<int> g_juego{9};   // el que corre (3: el jugador se ve y el rival se oculta)
std::atomic<float> g_ex{0}, g_ey{0}, g_ez{0}, g_tx{0}, g_ty{0}, g_tz{0}, g_jx{0}, g_jz{0};
std::atomic<uint32_t> g_x1{0};

// Hilo de la interfaz: logica a 60 por segundo y dibujo.
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::shared_ptr<Datos> d;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      d = g_d;
    }
    if (!g_on.load() || !d) return;
    if (d.get() != hecho_) Preparar(*d);
    uint8_t* base = Base();
    const int st = P::EstadoCombate();
    if (!base || st < 3 || st > 8) return;   // iw_fix: hasta el 8 (el menu azul de B3, ~1,7 s)
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    if (st == 4 && !pausa) {
      acc_ += dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
      while (acc_ >= 1.0f / 60.0f) {
        acc_ -= 1.0f / 60.0f;
        Efectos();
        if (d->juego == 3) Cuadro3(base, mgr, *d);
        else if (d->juego == 15) Cuadro15(base, mgr, *d);
        else if (d->juego == 8) CuadroDisco(base, mgr, *d);
        else Cuadro(base, mgr, *d);
      }
    }
    if (pausa) return;
    Dibujar(io, base, mgr, *d);
  }

 private:
  struct Rayo {
    V3 p, v;
    bool vivo = true;
    std::shared_ptr<ame::Instancia> nucleo;   // s_HBCORE_MAIN: Effect 0xF (935/13) que va con el rayo
  };
  enum Fase { kListo, kJuego, kDerribo, kFinal };

  void Preparar(const Datos& d) {
    hecho_ = &d;
    tx_i_ = ps2::Texturas(d.mano_i);
    tx_ef_ = ps2::Texturas(d.efecto);
    tx_lad_ = ps2::Texturas(d.ladrillo);
    tx_atlas_.clear();
    tx_suma_.clear();
    for (const P::Imagen& im : d.atlas) {
      tx_atlas_.push_back(ps2::Crear(im));
      P::Imagen su = im;   // suma: alfa = el canal mas brillante (el negro no suma nada)
      for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
        su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
      tx_suma_.push_back(ps2::Crear(su));
    }
    efx_.clear();
    aura_.reset();
    datos_ = &d;
    giro_disco_ = 0;
    tx_d_ = ps2::Texturas(d.mano_d);
    hud_.clear();
    for (const P::Imagen& im : d.hud) hud_.push_back(ps2::Crear(im));
    clear_ = ps2::Crear(d.clear);
    juego_ = d.juego;
    vida_ = vida_max_ = kVidaPicc[d.dif];
    restante_ = float(kLimite[d.dif]);
    fase_ = kListo;
    t_ = 0;
    acc_ = 0;
    disparos_ = 0;
    mano_ = 0;
    espera_[0] = espera_[1] = 0;
    retroceso_[0] = retroceso_[1] = 0;
    rayos_.clear();
    chispas_.clear();
    az_ = 0;
    elev_ = 0.12f;
    picc_fase_ = 0;
    esquiva_ = -1;
    esquiva_t_ = 60;
    semilla_ = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());

    g_yaw = 0;
    g_polar = 1.5708f - 0.06f;
    g_suelo = false;
    auto_t_ = 0;
    g_cam15 = d.juego == 15 || d.juego == 3;
    g_juego = d.juego;
    if (d.juego == 3) {   // camara del #MSS: (0, 30, 100) -> (0, 35, 0)
      tiros_ = 7;
      aciertos_ = 0;
      quedan_ = d.n_tramos;
      lad_s_ = d.lad_s;
      mira_x_ = 320, mira_y_ = 224;
      tramo_espera_ = 0;
      ladrillo_vivo_ = fijado_ = false;
      antes15_ = 0;
      g_ex = 0, g_ey = 30, g_ez = 100, g_tx = 0, g_ty = 35, g_tz = 0;
      g_jx = 0, g_jz = 0;
      g_px = 0, g_py = 0, g_pz = -40;   // el rival (oculto) delante: Goku mira hacia los ladrillos
    }
    if (d.juego == 15) {   // MSN016: Cell en (0, 0, 0) (0x5D9320), Ten en (0, 400, 200), energia 1
      dif15_ = d.dif;
      restante_ = float(kTiempo15[d.dif]);
      energia_ = 1.0f;
      baja_usada_ = false;
      sub15_ = t15_ = kikoho_ = 0;
      mx_ = 0;
      mz_ = 200;
      cx_ = cy_ = cz_ = cvy_ = 0;
      cst_ = 0;
      cc0_ = ce_t_ = 0;
      golpe_y_ = 0;
      rumbo_ = 0;
      p15_ = -1;
      antes15_ = 0;
      Camara15();
    }
    if (d.juego == 8) {   // MSN009: 180 s, N discos; Freezer al frente (yaw pi/2, pitch 0 de IW)
      restante_ = 180.0f;
      tiros_ = kTiros[d.dif];
      aciertos_ = fallos_seguidos_ = 0;
      disco_ = 0;
      pausa_ = 0;
      esquiva_t_ = 0;
      tirada_ = false;
      elev_ = 0.1f;
      g_polar = 1.5708f - 0.08f;
    }
  }
  uint32_t Azar() {
    semilla_ = semilla_ * 1103515245u + 12345u;
    return semilla_ >> 8;
  }
  V3 Ojo() const { return {0, kOjo, 0}; }

  // Un cuadro de IW (60 por segundo).
  void Cuadro(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    if (fase_ == kListo) {   // Viewer_StateReady: 100 cuadros
      if (t_ >= 100) fase_ = kJuego, t_ = 0;
      MoverPiccolo(false);
      return;
    }
    if (fase_ == kJuego) {
      restante_ -= 1.0f / 60.0f;
      // camara (Camera_OrbitInputUpdate): palanca fuera de la zona muerta (32) o la cruceta (+108, palanca << 8)
      const float vel = disparos_ < 3 ? 0.0165f : 0.01f;
      float gx = 0, gy = 0;
      if (std::abs(sx) > 32 || std::abs(sy) > 32) {
        gx = float(sx) / 127.0f;
        gy = -float(sy) / 127.0f;   // arriba negativo
      } else {
        const uint16_t dd = uint16_t(dirs | (dirs >> 8));
        gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
        gy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
      }
      bool fuego = (bot & kDisparo) != 0;
      if (g_auto) Piloto(base, mgr, gx, gy, fuego);
      g_yaw = g_yaw.load() - gx * vel;   // derecha de la camara = -X con yaw 0 (cine::Camara)
      g_polar = std::clamp(g_polar.load() - gy * vel, 0.34906587f, 2.792527f);
      // disparo: una mano y luego la otra (+0xA4), cada una ocupada un rato
      for (float& e : espera_) e = std::max(0.0f, e - 1.0f);
      for (float& r : retroceso_) r = std::max(0.0f, r - 0.15f);
      if (fuego && espera_[mano_] <= 0) {
        const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
        const V3 sale = Mano(mano_, 0);
        const V3 mira = Mas(ojo, Por(f, kRayoMira));
        V3 v = Mas(mira, Por(sale, -1.0f));
        v = Por(v, kRayoVel / std::max(Largo(v), 1e-3f));
        Lanzar(7, sale, v);   // Viewer_SetPlayerPoseState: Effect 8 (935/7) en la mano
        rayos_.push_back({sale, v, true, Lanzar(13, sale, v)});
        espera_[mano_] = kCadencia;
        retroceso_[mano_] = 1.0f;
        mano_ ^= 1;
        ++disparos_;
      }
      Rayos(base, mgr);
      MoverPiccolo(vida_ < 0.25f * vida_max_);
      if (vida_ <= 0) {
        fase_ = kDerribo;
        t_ = 0;
        segundos_ = float(kLimite[d.dif]) - restante_;
        g_suelo = true;
        REXLOG_INFO("dbz3 iwdisparo: Piccolo derribado en {:.2f} s ({} disparos)", segundos_, disparos_);
      } else if (restante_ <= 0) {
        restante_ = 0;
        fase_ = kDerribo;
        t_ = 0;
        segundos_ = 0;   // Viewer_StateInput: tiempo agotado -> +0x10 = 0 -> sin rango
        REXLOG_INFO("dbz3 iwdisparo: TIEMPO (vida de Piccolo {} de {})", vida_, vida_max_);
      }
      return;
    }
    if (fase_ == kDerribo) {   // Viewer_StateActive: 140 cuadros y se acaba
      Rayos(base, mgr);
      if (t_ >= 140) {
        fase_ = kFinal;
        Acabar(base, mgr, d);
      }
    }
  }

  // ---- MSN009: Kienzan contra Freezer (decomp EU Viewer_State_* 0x348B40-0x34A410, Effect_StartChargedHitState
  // 0x364510, Char_PoseModeUpdateAll 0x35D8D0). Camara 0.015 rad por cuadro (0.010 cargando o con el disco en vuelo),
  // giro +-65.73 grados e inclinacion -25.73..65.73. Circulo: carga (40 cuadros) y otra vez: lanza el disco hacia el
  // punto a 150 de la camara, a 5 por cuadro; si la mira tocaba un hueso (5 + radio), se guia hacia el con un peso que
  // baja 0.04 por cuadro. Acierto: disco (5) contra hueso (5 + radio). Fallo: a mas de 150. 3 fallos seguidos: 120
  // cuadros de pausa. Gana con 7 aciertos; pierde si aciertos + discos < 7 o a los 180 s.
  void CuadroDisco(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    if (fase_ == kListo) {   // AimStart: 100 cuadros
      if (t_ >= 100) fase_ = kJuego, t_ = 0;
      MoverFreezer(d);
      return;
    }
    if (fase_ != kJuego) {
      if (disco_ == 3) DiscoVuela(base, mgr);
      if (fase_ == kDerribo && t_ >= 140) {   // ShootRecoil: 140 cuadros
        fase_ = kFinal;
        Acabar(base, mgr, d);
      }
      return;
    }
    restante_ -= 1.0f / 60.0f;
    float gx = 0, gy = 0;
    if (std::abs(sx) > 32 || std::abs(sy) > 32) {
      gx = float(sx) / 127.0f;
      gy = -float(sy) / 127.0f;
    } else {
      const uint16_t dd = uint16_t(dirs | (dirs >> 8));
      gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
      gy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
    }
    bool fuego = (bot & kDisparo) != 0;
    if (g_auto) Piloto(base, mgr, gx, gy, fuego);
    if (pausa_ > 0) {   // AimShootC: 120 cuadros o el boton
      --pausa_;
      if (fuego) pausa_ = 0;
      gx = gy = 0;
    }
    const float vel = disco_ == 0 ? 0.015f : 0.010f;
    g_yaw = std::clamp(g_yaw.load() - gx * vel, -1.1472f, 1.1472f);
    g_polar = std::clamp(g_polar.load() - gy * vel, 1.5708f - 1.1472f, 1.5708f + 0.44907f);
    for (float& r : retroceso_) r = std::max(0.0f, r - 0.08f);
    if (pausa_ <= 0 && fuego && disco_ == 0) {   // AimShootA: carga (pose 9)
      disco_ = 1;
      carga_ = 0;
    } else if (disco_ == 1 && ++carga_ >= 40) {   // fase 0x12 a los 40: listo
      disco_ = 2;
    } else if (disco_ == 2 && fuego && !fuego_antes_) {   // AimShootB: lanza (pose 10), un disco menos
      const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
      const V3 mira = Mas(ojo, Por(f, kRadioFz));
      disco_p_ = Mano(1, 0);
      const V3 v = Mas(mira, Por(disco_p_, -1.0f));
      disco_v_ = Por(v, kDiscoVel / std::max(Largo(v), 1e-3f));
      bloqueo_ = HuesoCerca(base, mgr, mira, kDiscoRadio);
      combustible_ = 1.0f;
      disco_ = 3;
      --tiros_;
      retroceso_[1] = 1.0f;
      REXLOG_INFO("dbz3 iwdisparo: Kienzan lanzado (quedan {}; mira {})", tiros_,
                  bloqueo_ >= 0 ? "en un hueso" : "libre");
    }
    fuego_antes_ = fuego;
    if (disco_ == 3) DiscoVuela(base, mgr);
    MoverFreezer(d);
    if (disco_ == 3) return;   // AimShootD mira el resultado cuando el disco acaba
    if (aciertos_ >= 7) {
      fase_ = kDerribo;
      t_ = 0;
      segundos_ = 180.0f - restante_;
      REXLOG_INFO("dbz3 iwdisparo: 7 aciertos en {:.2f} s", segundos_);
    } else if (restante_ <= 0 || aciertos_ + tiros_ < 7) {
      restante_ = std::max(restante_, 0.0f);
      fase_ = kDerribo;
      t_ = 0;
      segundos_ = 0;   // ce78+8 = -1: sin banda
      REXLOG_INFO("dbz3 iwdisparo: FALLO ({} aciertos, {} discos, {:.1f} s)", aciertos_, tiros_, restante_);
    }
  }
  int HuesoCerca(uint8_t* base, uint32_t mgr, const V3& p, float radio) const {
    const uint32_t x1 = mgr + kX0 + kXPaso;
    int mejor = -1;
    float dmin = 1e9f;
    for (uint32_t i = 0; i < 22; ++i) {
      const uint32_t a = x1 + 1616 + 16 * i;
      const float dd = Largo(Mas(p, Por(V3{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)}, -1.0f)));
      if (dd <= radio + kRadioHueso[i] && dd < dmin) dmin = dd, mejor = int(i);
    }
    return mejor;
  }
  void DiscoVuela(uint8_t* base, uint32_t mgr) {
    const uint32_t x1 = mgr + kX0 + kXPaso;
    const V3 ojo = Ojo();
    if (bloqueo_ >= 0 && combustible_ > 0 && Largo(Mas(disco_p_, Por(ojo, -1.0f))) < kRadioFz) {
      const uint32_t a = x1 + 1616 + 16 * uint32_t(bloqueo_);
      V3 h = Mas(V3{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)}, Por(disco_p_, -1.0f));
      h = Por(h, 1.0f / std::max(Largo(h), 1e-3f));
      const V3 v = Mas(Por(disco_v_, (1.0f - combustible_) / kDiscoVel), Por(h, combustible_));
      disco_v_ = Por(v, kDiscoVel / std::max(Largo(v), 1e-3f));
      combustible_ = std::max(0.0f, combustible_ - 0.04f);
    }
    disco_p_ = Mas(disco_p_, disco_v_);
    if (const int h = HuesoCerca(base, mgr, disco_p_, kDiscoRadio); h >= 0) {
      ++aciertos_;
      fallos_seguidos_ = 0;
      disco_ = 0;
      chispas_.push_back({disco_p_, 0});
      REXLOG_INFO("dbz3 iwdisparo: Kienzan acierta (hueso {}): {} aciertos", h, aciertos_);
    } else if (Largo(Mas(disco_p_, Por(ojo, -1.0f))) > kRadioFz) {
      disco_ = 0;
      if (++fallos_seguidos_ >= 3) pausa_ = 120;
      REXLOG_INFO("dbz3 iwdisparo: Kienzan falla ({} seguidos)", fallos_seguidos_);
    }
  }
  // Freezer (Char_PoseModeUpdateAll / Char_PoseInputTransition): en la esfera de 150 alrededor de la camara, giro
  // +-60 grados e inclinacion -20..60. Si la camara le apunta (producto > 0.98), una tirada (B50 %) por vez le hace
  // esquivar 40 + azar % 80 cuadros hacia un lado (0..3). ponytail: el paso de la esquiva se toma B48 rad por cuadro
  // (en el decomp ese paso se trunca a entero y el movimiento visible sale de la animacion, sin seguir).
  void MoverFreezer(const Datos& d) {
    const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
    const V3 a{std::cos(elev_) * std::sin(az_), std::sin(elev_), std::cos(elev_) * std::cos(az_)};
    const float apunta = f.x * a.x + f.y * a.y + f.z * a.z;
    if (fase_ == kJuego && esquiva_t_ <= 0) {
      if (apunta > 0.98f && !tirada_) {
        tirada_ = true;
        if (int(Azar() % 100) < kB50[d.dif]) {
          esquiva_ = int(Azar() & 3);
          esquiva_t_ = 40 + int(Azar() % 80);
        }
      } else if (apunta <= 0.98f) {
        tirada_ = false;
      }
    }
    if (esquiva_t_ > 0) {
      --esquiva_t_;
      const float v = kB48[d.dif];
      if (esquiva_ == 0) az_ += v;
      if (esquiva_ == 1) az_ -= v;
      if (esquiva_ == 2) elev_ += v;
      if (esquiva_ == 3) elev_ -= v;
      if (std::abs(az_) > 1.0472f || elev_ < -0.349f || elev_ > 1.0472f) esquiva_t_ = 0;   // fuera: a la pose 1
      az_ = std::clamp(az_, -1.0472f, 1.0472f);
      elev_ = std::clamp(elev_, -0.349f, 1.0472f);
    }
    g_px = ojo.x + kRadioFz * std::cos(elev_) * std::sin(az_);
    g_pz = ojo.z + kRadioFz * std::cos(elev_) * std::cos(az_);
    g_py = std::max(0.0f, ojo.y - 6.0f + kRadioFz * std::sin(elev_));
  }

  // ---- MSN016: Ten contra Cell (MgShoot_* EU 0x3099E0-0x30CEF0 y la IA de Cell 0x31C400-0x31DF30: tablas de estado
  // 0x4996B0 entrar / 0x4996E0 al acabar la animacion / 0x499710 cada cuadro). Ten (oculto) es la camara: ojo en
  // (x, 400, z) (UiAnim_SetPos de 0x5D9350 = (0, 400, 200) en el controlador +0x80), x -111..74 y z 60..280 a 1,5 por
  // cuadro (MoveAimByPad). Cuadrado mantenido: carga +0,1 cada 2 s (tope 1); circulo: Kikoho (32 cuadros de carga, el
  // disparo gasta 0,1 y otros 32), la primera vez con <= 0,1 una espera de 220 cuadros. Le da a Cell si su posicion
  // proyectada cae en el rombo (300,147)-(403,250)-(300,353)-(197,250) de la pantalla de 600x500 (EU 0x480A10,
  // Char_IsProjectedInQuad). Peligro = altura de Cell / 400: >= 0,5 DANGER, >= 1 o energia <= 0 pierde. Tiempo
  // {40..160} s; al acabar p = altura / 400: <= 0,0001 Z, <= 0,1 S, <= 0,4 A, <= 0,7 B, si no C; {3600..6000} x mult.
  void Cuadro15(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const bool nuevo = (bot & kDisparo) && !(antes15_ & kDisparo);
    antes15_ = bot;
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    if (fase_ == kListo) {   // Ready: 100 cuadros, despues Cell a la pose 1 (a correr)
      if (t_ == 1 && g_x1.load()) dbz3::anim::Bucle(g_x1.load(), 0x960);   // en reposo (0x640, estado 0)
      if (t_ >= 100) {
        fase_ = kJuego;
        t_ = 0;
        CellEstado(1);
      }
      Camara15();
      return;
    }
    if (fase_ == kJuego) {
      restante_ -= 1.0f / 60.0f;
      if (sub15_ != 4) CellCuadro(d);   // en la espera de energia baja Cell se para (Store5d94c0Pair(0, 0))
      // fin por tiempo (MgShoot_CheckTimeUp) o derrota (energia <= 0, Cell a la altura de Ten)
      const float p = cy_ / kAltura15;
      if (restante_ <= 0) {
        restante_ = 0;
        p15_ = p;
        Final15(p >= 0 && p < 1 ? "TIEMPO: Cell contenido" : "TIEMPO: Cell arriba");
      } else if (energia_ <= 0 || p >= 1.0f) {
        p15_ = -1;
        Final15(energia_ <= 0 ? "sin energia" : "Cell llega a Ten");
      } else {
        bool fuego = nuevo;
        bool carga = (bot & 0x8000) != 0;   // cuadrado (X del mando)
        float gx = 0, gz = 0;
        if (std::abs(sx) > 32 || std::abs(sy) > 32) {
          gx = float(sx) / 127.0f;
          gz = float(sy) / 127.0f;
        } else {
          const uint16_t dd = uint16_t(dirs | (dirs >> 8));
          gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
          gz = float((dd & kAbajo) != 0) - float((dd & kArriba) != 0);
        }
        if (g_auto) Piloto15(gx, gz, fuego, carga);
        if (sub15_ == 0) {   // mira
          mx_ = std::clamp(mx_ + gx * 1.5f, -111.0f, 74.0f);   // derecha (0x20) X + 1,5; arriba (0x10) Z - 1,5
          mz_ = std::clamp(mz_ + gz * 1.5f, 60.0f, 280.0f);
          if (carga && energia_ < 1.0f) {
            sub15_ = 1;
          } else if (fuego && (baja_usada_ || energia_ > 0.1f)) {
            sub15_ = 2;
            t15_ = 0;
          } else if (!baja_usada_ && energia_ <= 0.1f) {
            sub15_ = 4;   // LowEnergyWait: 220 cuadros
            t15_ = 0;
            baja_usada_ = true;
          }
        } else if (sub15_ == 1) {   // Charge: +0,1 / (2 x 60) por cuadro; Effect 6 (935/19) en las manos
          energia_ += 0.1f / 120.0f;
          if (!aura_ || !aura_->vivo) aura_ = Lanzar(19, Cam({0, -0.6f, 4.0f}), Mas(Cam({0, 0, 1}), Por(Cam({0, 0, 0}), -1.0f)));
          if (energia_ > 1.0f || !carga) energia_ = std::min(energia_, 1.0f), sub15_ = 0;
        } else if (sub15_ == 2) {   // Fire: 32 cuadros de carga, el disparo, 32 de luces
          const V3 manos = Cam({0, -0.6f, 4.0f}), fv = Mas(Cam({0, 0, 1}), Por(Cam({0, 0, 0}), -1.0f));
          if (t15_ == 0) Lanzar(3, manos, fv);   // Effect 2 (935/3): la carga
          if (++t15_ == 32) {
            Lanzar(4, manos, Por(fv, -1.0f));   // Effect 3 (935/4) con la direccion negada
            Lanzar(6, manos, fv);               // Effect 5 (935/6): el Kikoho hacia la mira
            energia_ -= 0.1f;
            kikoho_ = 32;
            const bool dentro = CellEnRombo();
            REXLOG_INFO("dbz3 iwdisparo: Kikoho ({}): Cell a {:.0f} de altura, energia {:.2f}", dentro ? "le da" : "falla",
                        cy_, energia_);
            if (dentro) {
              Lanzar(5, V3{cx_, cy_ + 6.0f, cz_}, V3{0, 1, 0});   // Effect 4 (935/5) en Cell (Char_EnterPoseState4Alt)
              CellGolpe();
              chispas_.push_back({V3{cx_, cy_ + 6.0f, cz_}, 0});   // el golpe (Effect 4 de IW): la chispa t21
            }
          } else if (t15_ >= 64) {
            sub15_ = 0;
          }
        } else if (sub15_ == 4) {
          if (++t15_ >= 220) sub15_ = 0;
        }
      }
      if (kikoho_ > 0) --kikoho_;
      for (auto& c : chispas_) c.second += 1;
      chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                     chispas_.end());
      Camara15();
      return;
    }
    if (fase_ == kDerribo) {   // GameOver: 140 cuadros
      if (kikoho_ > 0) --kikoho_;
      Camara15();
      if (t_ >= 140) {
        fase_ = kFinal;
        Acabar(base, mgr, d);
      }
    }
  }
  void Final15(const char* por) {
    fase_ = kDerribo;
    t_ = 0;
    segundos_ = p15_ >= 0 && p15_ < 1 ? float(kTiempo15[dif15_]) : 0;   // para el CLEAR y el registro
    REXLOG_INFO("dbz3 iwdisparo: fin del 15 ({}): altura de Cell {:.1f}, p {:.4f}", por, cy_, p15_);
    if (g_x1.load()) dbz3::anim::Corredor(g_x1.load(), false);
  }
  void Camara15() {   // ojo de Ten y su mirada (la del principio, (0, 400, 200) -> (0, 15, 0), trasladada)
    g_ex = mx_;
    g_ey = kAltura15;
    g_ez = mz_;
    g_tx = mx_;
    g_ty = 15.0f;
    g_tz = mz_ - 200.0f;
    g_px = cx_;
    g_py = cy_;
    g_pz = cz_;
    // prueba (DBZ3_DISPARO_VER_CELL=1): la camara de lado, cerca de Cell, para ver sus animaciones de IW
    if (static const bool ver = std::getenv("DBZ3_DISPARO_VER_CELL") != nullptr; ver) {
      g_ex = cx_ + 45.0f;
      g_ey = cy_ + 12.0f;
      g_ez = cz_ + 10.0f;
      g_tx = cx_;
      g_ty = cy_ + 8.0f;
      g_tz = cz_;
    }
    // Ten (oculto) junto a Cell, delante de su marcha: Cell mira hacia donde va (B3 encara al rival)
    g_jx = cx_ + std::sin(rumbo_) * 20.0f;
    g_jz = cz_ + std::cos(rumbo_) * 20.0f;
  }
  // Rombo del Kikoho (pantalla de 600x500 de IW, centro (300, 250), media diagonal 103 px)
  bool CellEnRombo() const {
    const V3 e{mx_, kAltura15, mz_}, f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
    const V3 r{-f.z, 0, f.x};   // f x arriba (sin normalizar: f.y no entra)
    const V3 rn = Por(r, 1.0f / std::max(Largo(r), 1e-4f));
    const V3 u{rn.y * f.z - rn.z * f.y, rn.z * f.x - rn.x * f.z, rn.x * f.y - rn.y * f.x};
    const V3 v = Mas(V3{cx_, cy_, cz_}, Por(e, -1.0f));
    const float z = v.x * f.x + v.y * f.y + v.z * f.z;
    if (z <= 1) return false;
    const float k = 250.0f / std::tan(0.3926991f) / z;   // px de IW por unidad (FOV 45, media altura 250)
    const float px = (v.x * rn.x + v.y * rn.y + v.z * rn.z) * k, py = (v.x * u.x + v.y * u.y + v.z * u.z) * k;
    return std::abs(px) + std::abs(py) <= 103.0f;
  }
  static V3 Unir(const V3& a) { return Por(a, 1.0f / std::max(Largo(a), 1e-4f)); }

  // ---- IA de Cell (estados 0..6 de las tablas de arriba). Cada fase dura su animacion de IW (#BSK de 914: 0x654 10,
  // 0x656 21, 0x657 60 / 0x658 35 / 0x659 2, 0x66A 52, 0x66C 40 cuadros; auditoria7/I).
  void CellEstado(int e) {
    cc0_ = cst_ == e ? cc0_ + 1 : 0;
    cst_ = e;
    ce_t_ = 0;
    // la animacion de IW de cada estado (auditoria7/I, Char_EnterPoseState0..6Alt; la 1.a entrada y las siguientes)
    if (const uint32_t x1 = g_x1.load()) {
      const uint32_t an[7][3] = {{0x960, 0x960, 0x960}, {0x961, 0x962, 0x962}, {0x963, 0x963, 0x963},
                                 {0x964, 0x965, 0x966}, {0x967, 0x968, 0x968}, {0x969, 0x96A, 0x96A},
                                 {0x96B, 0x96B, 0x96B}};
      if (e >= 0 && e < 7) dbz3::anim::Bucle(x1, an[e][std::min(cc0_, 2)]);
    }
    if (e == 3) cvy_ = kSubida15[dif15_];   // Char_EnterPoseState3Alt: vy = campo +0x10 (3,0..4,0)
    if (e == 4) cvy_ = cvy_ > 0 ? -0.08f : cvy_ - 0.08f;   // Char_EnterPoseState4Alt (golpe)
    if (e == 1 && cc0_ == 0) {   // DashTrail: 5 marcas al azar (Char_InitTrailMarkerRandom)
      for (auto& mk : marcas_) {
        mk.tipo = int(Azar() % 10);
        mk.x = float(Azar() % 230) - 140.0f;
        mk.z = float(Azar() % 270) - 90.0f;
        mk.paso = float(Azar() & 0x7F) / 16.0f;
        mk.dura = float(Azar() & 0x7F) + 32.0f;
      }
      marca_ = 0;
      wx_ = marcas_[0].x;
      wz_ = marcas_[0].z;
      rumbo_ = std::atan2(wx_ - cx_, wz_ - cz_);
    }
  }
  void CellGolpe() {   // Char_HandleQuadLanding: estado 0xC -> 4; guarda la altura del golpe (recuperacion)
    golpe_y_ = cy_;
    CellEstado(4);
  }
  void CellCuadro(const Datos& d) {
    (void)d;
    ++ce_t_;
    switch (cst_) {
      case 1: {   // UpdateSteer: la primera animacion a 1,5 por cuadro; despues las marcas a 3 (campo +0xC)
        if (cc0_ < 1) {
          cx_ += std::sin(rumbo_) * 1.5f;
          cz_ += std::cos(rumbo_) * 1.5f;
          if (ce_t_ >= 10) CellEstado(1);
        } else if (Perseguir()) {
          CellEstado(2);
        }
        break;
      }
      case 2:   // SteerInput (< 12 cuadros a 1,5) y al acabar la animacion: a subir
        if (ce_t_ < 12) cx_ += std::sin(rumbo_) * 1.5f, cz_ += std::cos(rumbo_) * 1.5f;
        if (ce_t_ >= 21) CellEstado(3);
        break;
      case 3:   // sube: y += vy desde la 2.a animacion (0x31CE60); 0x657 -> 0x658 -> 0x659 (DispatchEarly)
        if (cc0_ != 0) cy_ += cvy_;
        if (ce_t_ >= (cc0_ == 0 ? 60 : cc0_ == 1 ? 35 : 2) && cc0_ < 3) CellEstado(3);
        break;
      case 4:   // cae (Char_PoseTickRise): y += vy, vy - 0,06; al suelo (0,1): 5
        if (cy_ + cvy_ > 0.1f) {
          cy_ += cvy_;
          cvy_ -= 0.06f;
        } else {
          cy_ = 0.1f;
          CellEstado(5);
        }
        break;
      case 5:   // TimedDispatch: tras la animacion, y despues altura del golpe / 400 x 60 cuadros: 6
        if (cc0_ == 0 && ce_t_ >= 52) CellEstado(5);
        else if (cc0_ > 0 && float(ce_t_) > golpe_y_ / kAltura15 * 60.0f) CellEstado(6);
        break;
      case 6:   // DashTrail al acabar: marcas nuevas; si la 1.a es de tipo 0, a subir; si no, a correr
        if (ce_t_ >= 40) {
          CellEstado(1);
          if (marcas_[0].tipo == 0) CellEstado(3);
        }
        break;
      default:
        break;
    }
    cx_ = std::clamp(cx_, -250.0f, 250.0f);
    cz_ = std::clamp(cz_, -250.0f, 250.0f);
  }
  // Char_PursueTrailMarker: hacia el punto de trabajo a 3 por cuadro; tipo 1/9 = la marca, 2-8 = se acerca a ella
  // `paso` por cuadro, 0 = fin. Se pasa a la siguiente a menos de 30 (campo +0xC x 10) o al acabar su tiempo.
  bool Perseguir() {
    while (marca_ < 5) {
      const Marca& mk = marcas_[size_t(marca_)];
      if (mk.tipo == 0) return true;
      if (mk.tipo == 1 || mk.tipo == 9) {
        wx_ = mk.x, wz_ = mk.z;
      } else {
        const float dx = mk.x - wx_, dz = mk.z - wz_, l = std::hypot(dx, dz);
        if (l > mk.paso) wx_ += dx / l * mk.paso, wz_ += dz / l * mk.paso;
        else wx_ = mk.x, wz_ = mk.z;
      }
      const float dmx = std::hypot(mk.x - cx_, mk.z - cz_);
      if (dmx < 30.0f || ++marca_t_ > int(mk.dura)) {
        ++marca_;
        marca_t_ = 0;
        continue;
      }
      break;
    }
    if (marca_ >= 5) return true;
    const float dx = wx_ - cx_, dz = wz_ - cz_, l = std::hypot(dx, dz);
    if (l > 1e-3f) {
      const float paso = std::min(3.0f, l);
      cx_ += dx / l * paso;
      cz_ += dz / l * paso;
      rumbo_ = std::atan2(dx, dz);
    }
    return false;
  }
  // Prueba: la mira sigue a Cell y dispara cuando esta dentro del rombo; carga con poca energia.
  void Piloto15(float& gx, float& gz, bool& fuego, bool& carga) {
    // donde estara Cell cuando salga el Kikoho (32 cuadros), en el centro de la mirada: z del ojo = z + 0,52 (400 - y)
    const float y = std::min(cy_ + (cst_ == 3 && sub15_ == 0 ? 32.0f * cvy_ : 0.0f), kAltura15);
    const float objx = cx_, objz = cz_ + 200.0f / 385.0f * (kAltura15 - y);
    gx = std::clamp((objx - mx_) / 1.5f, -1.0f, 1.0f);
    gz = std::clamp((objz - mz_) / 1.5f, -1.0f, 1.0f);
    fuego = sub15_ == 0 && cst_ == 3 && CellEnRombo() && ++auto_t_ % 2 == 0;   // al subir
    carga = energia_ < 0.95f && (cst_ == 1 || cst_ == 4 || cst_ == 5 || cst_ == 6);   // mientras no sube
  }

  // ---- 3: ladrillos con el Kaio (`# SHOOT #`, motor MgFly: EU 0x366480-0x369370; datos DATA_CMN 890 = #MSS 890/4,
  // ladrillo ITEMB03_BRICK 890/1; leidos por R12 y por mi). Bloque del #MSS por dificultad: N ladrillos (60/50/40/30/30/
  // 15), recorrido de 115 por tramo, umbrales de aciertos 7/6/5/4/3 (Z..C) y sus puntos, camara (0, 30, 100) -> (0, 35,
  // 0) a 45 grados, y 60 trayectorias (velocidad, inicio x, y, profundidad, fin x, y, profundidad) en la pantalla de PS2
  // (640 x 448). Mira 2D con la palanca/cruceta a 10 px por cuadro; si el ladrillo cae dentro de su radio proyectado
  // (ctx+0x514: focal*S/z, con el factor de direccion del tramo) lo fija y lo imanta 5 cuadros; circulo = disparo (7 en
  // total): fijado = roto. Sin tiempo: acaba con N tramos o los 7 disparos.
  void Cuadro3(uint8_t* base, uint32_t mgr, const Datos& d) {
    (void)base;
    (void)mgr;
    ++t_;
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    bool fuego = (bot & kDisparo) && !(antes15_ & kDisparo);
    antes15_ = bot;
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    if (fase_ == kListo) {
      if (t_ >= 100) fase_ = kJuego, t_ = 0, Tramo(d);
      return;
    }
    if (fase_ == kDerribo) {
      if (t_ >= 140) fase_ = kFinal, Acabar(base, mgr, d);
      return;
    }
    if (fase_ != kJuego) return;
    // mira: palanca (x*x + y*y > 0xFFF) o cruceta, a la velocidad del #MSS (+0x10 = 10 px de PS2 por cuadro)
    float gx = 0, gy = 0;
    if (sx * sx + sy * sy > 0xFFF) {
      const float l = std::hypot(float(sx), float(sy));
      gx = float(sx) / l, gy = float(sy) / l;
    } else {
      const uint16_t dd = uint16_t(dirs | (dirs >> 8));
      gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
      gy = float((dd & kAbajo) != 0) - float((dd & kArriba) != 0);
    }
    if (g_auto) Piloto3(gx, gy, fuego);
    // el ladrillo avanza por su tramo
    if (tramo_espera_ > 0) {
      if (--tramo_espera_ == 0) {
        if (quedan_ <= 0 || tiros_ <= 0) return Fin3(d);
        Tramo(d);
      }
    } else if (ladrillo_vivo_) {
      progreso_ += lad_vel_;
      const float u = std::min(progreso_ / 115.0f, 1.0f);
      lad_x_ = lad_a_[0] + (lad_b_[0] - lad_a_[0]) * u;
      lad_y_ = lad_a_[1] + (lad_b_[1] - lad_a_[1]) * u;
      lad_z_ = lad_a_[2] + (lad_b_[2] - lad_a_[2]) * u;
      if (u >= 1.0f) {   // se escapa: sin acierto, el siguiente a los 60 cuadros
        ladrillo_vivo_ = fijado_ = false;
        --quedan_;
        tramo_espera_ = 60;
      }
    }
    const float px = mira_x_, py = mira_y_;
    if (!fijado_) {
      mira_x_ = std::clamp(mira_x_ + gx * 10.0f, 0.0f, 640.0f);   // +0x10 del #MSS: 10 px de PS2 por cuadro
      mira_y_ = std::clamp(mira_y_ + gy * 10.0f, 0.0f, 448.0f);
      // fijar: distancia del ladrillo al recorrido de la mira en este cuadro < su radio en pantalla
      if (ladrillo_vivo_ && lad_z_ <= 700.0f) {
        const float ax = mira_x_ - px, ay = mira_y_ - py, bx = lad_x_ - px, by = lad_y_ - py;
        const float l2 = ax * ax + ay * ay, s = l2 > 0 ? std::clamp((ax * bx + ay * by) / l2, 0.0f, 1.0f) : 0.0f;
        const float dist = std::hypot(bx - ax * s, by - ay * s);
        if (dist < RadioLadrillo()) fijado_ = true, tiro_t_ = 5.0f;   // 0x41400000: el iman dura 5
      }
    } else if (ladrillo_vivo_) {   // 5 cuadros hacia el ladrillo y luego encima
      if (tiro_t_ > 1.0f) {
        mira_x_ += (lad_x_ - mira_x_) / tiro_t_;
        mira_y_ += (lad_y_ - mira_y_) / tiro_t_;
        tiro_t_ -= 1.0f;
      } else {
        mira_x_ = lad_x_, mira_y_ = lad_y_;
      }
    }
    if (fuego && tiros_ > 0 && tramo_espera_ == 0) {
      --tiros_;
      {   // Effect 7 (890/6/22) en la mira, a la profundidad del ladrillo
        const float zz = lad_z_, xx = lad_x_, yy = lad_y_;
        lad_x_ = mira_x_, lad_y_ = mira_y_, lad_z_ = std::max(zz, 60.0f);
        Lanzar(22, Lad3D(), V3{0, 0, 1});
        lad_x_ = xx, lad_y_ = yy, lad_z_ = zz;
      }
      if (fijado_ && ladrillo_vivo_) {
        ++aciertos_;
        chispas_.push_back({Lad3D(), 0});
        ladrillo_vivo_ = fijado_ = false;
        --quedan_;
        tramo_espera_ = 120;   // 30 + 90 cuadros (LoadNodeStart, SegmentProgress)
        REXLOG_INFO("dbz3 iwdisparo: ladrillo roto ({} de {} disparos, quedan {})", aciertos_, 7 - tiros_, quedan_);
      } else {
        REXLOG_INFO("dbz3 iwdisparo: disparo al aire (quedan {} disparos)", tiros_);
      }
      if (tiros_ <= 0 && tramo_espera_ == 0) tramo_espera_ = 30;
    }
    for (auto& c : chispas_) c.second += 1;
    chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                   chispas_.end());
  }
  void Tramo(const Datos& d) {   // MgFly_SetupNextNode: una trayectoria al azar de las 60
    if (d.tramos.empty()) return;
    const auto& tr = d.tramos[Azar() % d.tramos.size()];
    lad_vel_ = tr[0];
    lad_a_ = {tr[1], tr[2], tr[3]};
    lad_b_ = {tr[4], tr[5], tr[6]};
    progreso_ = 0;
    lad_x_ = lad_a_[0], lad_y_ = lad_a_[1], lad_z_ = lad_a_[2];
    ladrillo_vivo_ = true;
    fijado_ = false;
  }
  void Fin3(const Datos& d) {
    (void)d;
    fase_ = kDerribo;
    t_ = 0;
    segundos_ = aciertos_ >= 3 ? 1.0f : 0.0f;   // CLEAR si hay rango
    REXLOG_INFO("dbz3 iwdisparo: fin de los ladrillos: {} aciertos", aciertos_);
  }
  // pantalla de PS2 (640 x 448, centro 320, 224) a mundo: el punto a la profundidad z por la mira de la camara
  V3 Lad3D() const {
    const V3 e{0, 30, 100}, f = Unir(V3{0, 5, -100}), r = Unir(V3{-f.z, 0, f.x});
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    const float k = std::tan(0.3926991f) / 224.0f * lad_z_ * kEsc3;   // unidades por px a esa profundidad
    return Mas(e, Mas(Por(f, lad_z_ * kEsc3), Mas(Por(r, (lad_x_ - 320.0f) * k), Por(u, (224.0f - lad_y_) * k))));
  }
  // F1: el ladrillo en el mundo bajo su punto de la pantalla de PS2 (donde va la mira) a su profundidad, con la camara
  // real del combate (inversa de su vista). Lad3D supone la camara del #MSS: si la de B3 no coincide, el ladrillo
  // caia fuera de la vista (no se veia en ninguna captura).
  V3 LadCam(const ps2::Camara& cam, const ImGuiIO& io) const {
    const float esc = io.DisplaySize.y / 448.0f;
    const float sx = io.DisplaySize.x * 0.5f + (lad_x_ - 320.0f) * esc, sy = io.DisplaySize.y * 0.5f + (lad_y_ - 224.0f) * esc;
    const float z = std::max(lad_z_ * kEsc3, 2.0f), k = cam.h.y / std::tan(cam.fov * 0.5f) / z;
    const float* m = cam.m;
    const float vx = (sx - cam.c.x) / k - m[12], vy = -(sy - cam.c.y) / k - m[13], vz = -z - m[14];
    return {m[0] * vx + m[1] * vy + m[2] * vz, m[4] * vx + m[5] * vy + m[6] * vz, m[8] * vx + m[9] * vy + m[10] * vz};
  }
  float RadioLadrillo() const {
    // MgFly_ProjectToScreen (EU 0x366480): +0x514 = (focal * S / z) * (0,5*|n.z| + hipot(n.x, n.y)), con n la normal
    // de A->B en espacio de camara y S = bloque +0x08 (MgFly_SetupNextNode: +0x510). focal = 448/(2*tan(fov/2)) con el
    // fov de 45 del #MSS = 540,7839 px; pantalla PS2 640x448. IW pone el radio a 0 si la profundidad pasa de 700.
    if (lad_z_ > 700.0f) return 0.0f;
    constexpr float kFocal = 540.78384f, kCx = 320.0f, kCy = 224.0f;
    const V3 a{lad_a_[2] * (lad_a_[0] - kCx) / kFocal, lad_a_[2] * (lad_a_[1] - kCy) / kFocal, lad_a_[2]};
    const V3 b{lad_b_[2] * (lad_b_[0] - kCx) / kFocal, lad_b_[2] * (lad_b_[1] - kCy) / kFocal, lad_b_[2]};
    const V3 n = Unir(Mas(b, Por(a, -1.0f)));
    return kFocal * lad_s_ / std::max(lad_z_, 1.0f) * (0.5f * std::fabs(n.z) + std::hypot(n.x, n.y));
  }
  void Piloto3(float& gx, float& gy, bool& fuego) {
    gx = std::clamp((lad_x_ - mira_x_) / 10.0f, -1.0f, 1.0f);
    gy = std::clamp((lad_y_ - mira_y_) / 10.0f, -1.0f, 1.0f);
    fuego = fijado_ && tiro_t_ <= 1.0f && ++auto_t_ % 2 == 0;
  }

  // Camara: derecha (cine::Camara: f x arriba), arriba y delante. Las manos de IW (892/1 y 892/3) estan hechas en el
  // espacio de la camara: la mano en z = 0 y el brazo hacia -z (hasta -7.5), la izquierda en -x.
  // ponytail: su sitio en IW lo pone la animacion de cada mano (+0xD0/+0xE0 del motor de disparo, sin leer): a ojo.
  V3 Cam(const V3& c, float escala = 1.0f) const {
    const float yaw = g_yaw.load(), polar = g_polar.load();
    V3 f = Dir(yaw, polar), r{-std::cos(yaw), 0, std::sin(yaw)}, ojo = Ojo();
    if (juego_ == 15) {   // la camara de Ten (Camara15)
      f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
      r = Unir(V3{-f.z, 0, f.x});
      ojo = {mx_, kAltura15, mz_};
    }
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    return Mas(ojo, Por(Mas(Por(r, c.x), Mas(Por(u, c.y), Por(f, c.z))), escala));
  }
  static V3 SitioMano(int k, float retro) { return {k == 0 ? -1.3f : 1.3f, -2.1f, 6.5f - 1.5f * retro}; }
  V3 Mano(int k, float retro) const {   // punta de la mano k en el mundo (de ahi sale el rayo)
    const V3 o = SitioMano(k, retro);
    return Cam({o.x, o.y + 0.3f, o.z + 0.6f});
  }

  void Rayos(uint8_t* base, uint32_t mgr) {
    const uint32_t x1 = mgr + kX0 + kXPaso, rt = REX_LOAD_U32(x1 + 2996);
    for (Rayo& r : rayos_) {
      if (!r.vivo) continue;
      r.p = Mas(r.p, r.v);
      if (r.nucleo) r.nucleo->origen = r.p, r.nucleo->dir = Unir(r.v);
      if (Largo(Mas(r.p, Por(Ojo(), -1.0f))) > kRayoMax) {   // fuera: fallo
        r.vivo = false;
        continue;
      }
      if (!rt || vida_ <= 0) continue;
      // el hueso mas cercano dentro de 1 + su radio (Viewer_PickNearestBoneWorld + Math_SphereHitDist)
      int mejor = -1;
      float dmin = 1e9f;
      for (uint32_t i = 0; i < 22; ++i) {
        const uint32_t a = x1 + 1616 + 16 * i;
        const V3 c{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
        const float dd = Largo(Mas(r.p, Por(c, -1.0f)));
        if (dd <= 1.0f + kRadioHueso[i] && dd < dmin) dmin = dd, mejor = int(i);
      }
      if (mejor < 0) continue;
      r.vivo = false;
      const int g = kGrupo[mejor];
      vida_ = std::max(0, vida_ - kDano[g]);
      chispas_.push_back({r.p, 0});
      REXLOG_INFO("dbz3 iwdisparo: impacto hueso {} (grupo {}, -{}): vida {}/{}", mejor, g, kDano[g], vida_, vida_max_);
    }
    rayos_.erase(std::remove_if(rayos_.begin(), rayos_.end(), [](const Rayo& r) { return !r.vivo; }), rayos_.end());
    for (auto& c : chispas_) c.second += 1;
    chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                   chispas_.end());
  }

  // Piccolo en su orbita (Viewer_UpdateCharactersAndCamera / Viewer_PoseScheduler): radio 60 + 10 sin(fase), +-45
  // grados, esquivas de lado al azar (0..3, distinta de la anterior) cada azar % 120 cuadros. ponytail: la duracion de
  // cada esquiva y su velocidad angular (2.0 / 60 rad por cuadro, 2.25 con vida < 40 %) no estan medidas en IW.
  void MoverPiccolo(bool quieto) {
    picc_fase_ += 0.1f;
    if (!quieto && fase_ == kJuego) {
      if (--esquiva_t_ <= 0) {
        int nueva;
        do nueva = int(Azar() & 3);
        while (nueva == esquiva_);
        esquiva_ = nueva;
        esquiva_t_ = 20 + int(Azar() % 120);
      }
      const float v = (vida_ < 0.4f * vida_max_ ? 2.25f : 2.0f) / kOrbita * 0.5f;
      if (esquiva_ == 0) az_ += v;
      if (esquiva_ == 1) az_ -= v;
      if (esquiva_ == 2) elev_ += v * 0.5f;
      if (esquiva_ == 3) elev_ -= v * 0.5f;
      az_ = std::clamp(az_, -0.7853982f, 0.7853982f);
      elev_ = std::clamp(elev_, -0.05f, kElevMax);
    }
    const float rad = kOrbita + 10.0f * std::sin(picc_fase_);   // ponytail: f(fase) de IW tomada como seno
    const V3 o = Ojo();
    g_px = o.x + rad * std::cos(elev_) * std::sin(az_);
    g_pz = o.z + rad * std::cos(elev_) * std::cos(az_);
    g_py = std::max(0.0f, o.y - 6.0f + rad * std::sin(elev_));
  }

  // Prueba (DBZ3_TEST_IWMINI=0x7D8,auto): gira hacia Piccolo y dispara.
  void Piloto(uint8_t* base, uint32_t mgr, float& gx, float& gy, bool& fuego) {
    const uint32_t pecho = mgr + kX0 + kXPaso + 1616 + 16;   // la esfera del pecho del blanco (donde esta de verdad)
    const V3 p{LoadF(base, pecho), LoadF(base, pecho + 4), LoadF(base, pecho + 8)}, o = Ojo();
    const V3 d = Mas(p, Por(o, -1.0f));
    const float yaw = std::atan2(d.x, d.z), polar = std::acos(std::clamp(d.y / std::max(Largo(d), 1e-3f), -1.0f, 1.0f));
    float dy = yaw - g_yaw.load();
    while (dy > 3.14159265f) dy -= 6.2831853f;
    while (dy < -3.14159265f) dy += 6.2831853f;
    gx = std::clamp(-dy / 0.0165f, -1.0f, 1.0f);
    gy = std::clamp(-(polar - g_polar.load()) / 0.0165f, -1.0f, 1.0f);
    fuego = std::abs(dy) < 0.08f && ++auto_t_ % 2 == 0;
    if (hecho_ && hecho_->juego == 8)
      fuego = std::abs(dy) < 0.03f && std::abs(polar - g_polar.load()) < 0.03f && auto_t_ % 2 == 0;
  }

  void Acabar(uint8_t* base, uint32_t mgr, const Datos& d) {
    iwmini::Resultado r;
    r.clave = d.clave;
    r.fila = d.fila;
    const float x = segundos_;
    int idx = -1;   // Menu_ClassifyValueByThreshold
    if (x > 0 && x < kUmbral[0])
      for (int i = 4; i >= 0; --i)
        if (x <= kUmbral[i]) {
          idx = i;
          break;
        }
    if (d.juego == 3) {   // MgFly_StateSegmentProgress: el primer umbral <= aciertos (r), rango 4 - r; si no, fallo
      int rr = 5;
      for (int i = 0; i < 5 && rr == 5; ++i)
        if (d.umbral[i] <= aciertos_) rr = i;
      r.clave = d.clave;
      r.fila = d.fila;
      r.superado = rr < 5;
      r.rango = rr < 5 ? 4 - rr : 0;
      r.puntos = rr < 5 ? d.puntos[rr] : d.fallo;
      r.monedas = aciertos_;
      {
        std::lock_guard<std::mutex> lk(g_mu);
        g_res = r;
      }
      REXLOG_INFO("dbz3 iwdisparo: fin: {} ladrillos, rango {}, puntos {}", aciertos_, "CBASZ"[r.rango], r.puntos);
      REX_STORE_U8(mgr + kFin, uint8_t(r.superado ? 1 : 2));
      g_fin_pedido = true;
      return;
    }
    if (d.juego == 15) {   // MgShoot_GetRankFromTime con p = altura / 400; p = 0 exacto no tiene rango
      const float q = p15_;
      idx = q <= 0 || q >= 1 ? -1 : q <= 0.0001f ? 4 : q <= 0.1f ? 3 : q <= 0.4f ? 2 : q <= 0.7f ? 1 : 0;
    }
    r.superado = d.juego == 15 ? (p15_ >= 0 && p15_ < 1) : idx >= 0;
    r.rango = std::max(idx, 0);
    if (idx >= 0) {
      const int raw = int(float(d.juego == 15 ? kBase15[idx] : d.juego == 8 ? kBaseDisco[idx] : kBase[idx]) * kMult[d.dif]);
      r.puntos = (raw % 100 >= 50 ? raw / 100 + 1 : raw / 100) * 100;
    } else {
      r.puntos = 100;
    }
    r.centesimas = int(x * 100.0f);
    r.monedas = d.juego == 8 ? aciertos_ : disparos_;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_res = r;
    }
    REXLOG_INFO("dbz3 iwdisparo: fin: {} en {:.2f} s, rango {}, puntos {}", r.superado ? "DERRIBO" : "FALLO", x,
                "CBASZ"[r.rango], r.puntos);
    REX_STORE_U8(mgr + kFin, uint8_t(r.superado ? 1 : 2));   // el combate acaba como un ring out (pasarela.cpp)
    g_fin_pedido = true;
  }

  // ---- dibujo ----
  ImVec2 Px(const S::Area& a, float x, float y) const { return a.At(x, y); }
  void Trozo(ImDrawList* dl, const ps2::Tex& t, float x0, float y0, float x1, float y1, ImVec2 p, float k) const {
    if (!t) return;
    dl->AddImage(t.id(), p, ImVec2(p.x + (x1 - x0) * k, p.y + (y1 - y0) * k), ImVec2(x0 / t.w, y0 / t.h),
                 ImVec2(x1 / t.w, y1 / t.h));
  }
  const ps2::Tex& Hud(int j) const {
    static const ps2::Tex vacia;
    return j < int(hud_.size()) ? hud_[size_t(j)] : vacia;
  }
  void Cifras(ImDrawList* dl, int n, ImVec2 centro, float k) const {   // DATA_USA 9/1 t04 (0-4 arriba, 5-9 abajo)
    static constexpr float kX[10][2] = {{4, 29},  {39, 55},  {68, 92},  {101, 124}, {131, 157},
                                        {5, 29},  {37, 60},  {69, 93},  {100, 124}, {132, 156}};
    char s[8];
    std::snprintf(s, sizeof s, "%d", std::max(n, 0));
    float ancho = 0;
    for (const char* p = s; *p; ++p) ancho += kX[*p - '0'][1] - kX[*p - '0'][0];
    float x = centro.x - ancho * k * 0.5f;
    for (const char* p = s; *p; ++p) {
      const int c = *p - '0';
      const float y0 = c < 5 ? 1 : 33;
      Trozo(dl, Hud(4), kX[c][0], y0, kX[c][1], y0 + 29, ImVec2(x, centro.y - 14.5f * k), k);
      x += (kX[c][1] - kX[c][0]) * k;
    }
  }

  void Dibujar(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d) {
    const ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    if (cam.ok) {   // rayos, chispas y manos (modelos de IW) sobre el combate
      ImDrawList* bg = ImGui::GetBackgroundDrawList();
      escena_.Empezar();
      for (int k = 0; k < 2; ++k) {
        const ps2::Modelo& m = k == 0 ? d.mano_i : d.mano_d;
        if (!m.malla) continue;
        ps2::Malla w = m.malla;
        // 15: CameraCtrl_Create (EU 0x3105A0): a 5,5 por la mirada y (0, -1,1, -3,7) -> (0, -1,1, 1,8). ponytail: su giro
        // (+0x50: -1, pi, 0) no se aplica (el modelo ya mira hacia +z en el espacio de la camara)
        const V3 o = juego_ == 15 ? V3{0, -1.1f, 1.8f} : SitioMano(k, retroceso_[k]);
        // x5 alrededor del ojo: la misma imagen, pero lejos del corte de Escena::Poner (no pinta lo pegado a la camara)
        for (auto& t : w.tris)
          for (V3& p : t.p) p = Cam({p.x + o.x, p.y + o.y, p.z + o.z}, 5.0f);
        if (juego_ == 15)   // ponytail: el bit alto del vtype de ITEMB00 (translucido en ps2::Malla) no es de piel
          for (auto& t : w.tris) t.trans = false;
        escena_.Poner(cam, w, k == 0 ? tx_i_ : tx_d_, {0, 0, 0}, 0, 1.0f);
      }
      // Kikoho (15): las mallas del paquete del minijuego (935/1, conos MODEL_L0x_EFT de 25 x 120 hacia -y, atlas 935/0)
      // desde las manos de Ten por su mirada hasta el suelo, 32 cuadros
      if (juego_ == 15 && kikoho_ > 0 && d.efecto.malla) {
        const V3 e{mx_, kAltura15, mz_}, f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
        const float largo = (kAltura15 - 15.0f) / std::max(-f.y, 0.1f);
        OrientadoY(cam, d.efecto, Mas(e, Por(f, 30.0f)), f, largo / 120.0f * (0.6f + 0.4f * float(kikoho_) / 32.0f));
      }
      // 3: el ladrillo de IW (890/1 ITEMB03_BRICK) en su punto del tramo
      if (juego_ == 3 && ladrillo_vivo_ && d.ladrillo.malla) {
        V3 lp = LadCam(cam, io);
        lp.y -= 4.8f;   // el modelo tiene la base en y = 0 (caja 0..9,6): su centro en el punto de la mira
        escena_.Poner(cam, d.ladrillo.malla, tx_lad_, lp, float(t_) * 0.05f, 1.0f);
        static int64_t log_t = 0;
        if (const int64_t ms = int64_t(dbz3::RelojT(ImGui::GetTime()) * 1000); ms - log_t > 1000) {   // F1: sonda (1 por segundo)
          log_t = ms;
          ImVec2 sp;
          float kk = 0;
          const bool vis = cam.P(lp, sp, kk);
          REXLOG_INFO("dbz3 iwdisparo: ladrillo pintado en ({:.0f},{:.0f},{:.0f}) -> pantalla ({:.0f},{:.0f}) k {:.2f} vis {} "
                      "(ps2 {:.0f},{:.0f} z {:.0f}), {} tex, prof {}", lp.x, lp.y, lp.z, sp.x, sp.y, kk, vis, lad_x_, lad_y_,
                      lad_z_, tx_lad_.size(), ps2::EstadoProfundidad());
        }
      }
      // rayos: el modelo del rayo de IW orientado a su marcha (la cabeza, z = 0, delante; la cola hacia -z)
      for (const Rayo& r : rayos_) Orientado(cam, d.efecto, r.p, r.v, 0.6f);   // ponytail: escala a ojo (la pone su #AME)
      // Kienzan: el modelo de IW, plano y girando; cargando, encima de la mano (x5 alrededor del ojo, como las manos)
      if (disco_ >= 1 && d.efecto.malla) {
        giro_disco_ += 0.5f;
        if (disco_ == 3)
          escena_.Poner(cam, d.efecto.malla, tx_ef_, disco_p_, giro_disco_, 2.2f);
        else
          escena_.Poner(cam, d.efecto.malla, tx_ef_, Cam({1.4f, 1.6f, 7.0f}, 5.0f), giro_disco_,
                        5.0f * (0.6f + 1.2f * float(std::min(carga_, 40)) / 40.0f));
      }
      escena_.Pintar(bg);
      for (const auto& e : efx_) ame::Pintar(*e, cam, tx_atlas_, tx_suma_, bg, 160.0f * io.DisplaySize.y / 720.0f);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      for (const auto& c : chispas_) {   // chispa (9/1 t21)
        ImVec2 s;
        float k = 0;
        if (!cam.P(c.first, s, k)) continue;
        const float rr = std::max(6.0f, (2.0f + 0.15f * float(c.second)) * k);
        const ImU32 col = IM_COL32(255, 255, 255, int(255 * (1.0f - float(c.second) / 21.0f)));
        if (Hud(21)) fg->AddImage(Hud(21).id(), ImVec2(s.x - rr, s.y - rr), ImVec2(s.x + rr, s.y + rr), ImVec2(0, 0),
                                  ImVec2(1, 1), col);
      }
    }
    HudPantalla(io, base, mgr, cam);
    // final: fundido a negro (tapa el final de combate de B3) con CLEAR si hubo derribo
    if (fase_ == kDerribo || fase_ == kFinal) {
      const float al = fase_ == kFinal ? 1.0f : std::clamp(float(t_ - 80) / 60.0f, 0.0f, 1.0f);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * al)));
      if (segundos_ > 0 && clear_) {
        const S::Area a = S::GameArea(io);
        const float w = clear_.w * 1.6f * a.s, h = clear_.h * 1.6f * a.s;
        const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        fg->AddImage(clear_.id(), ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f));
      }
    }
  }

  // Modelo m en `pos` con su -y hacia `dir` (los conos de los efectos de IW), escala `esc`.
  void OrientadoY(const ps2::Camara& cam, const ps2::Modelo& m, const V3& pos, const V3& dir, float esc) {
    const V3 f = Unir(dir), r = Unir(V3{f.z, 0, -f.x});
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    ps2::Malla w = m.malla;
    for (auto& t : w.tris)
      for (V3& p : t.p) p = Mas(pos, Por(Mas(Por(r, p.x), Mas(Por(f, -p.y), Por(u, p.z))), esc));
    escena_.Poner(cam, w, tx_ef_, {0, 0, 0}, 0, 1.0f);
  }
  // Modelo m en `pos` con su +z hacia `dir` (y su +y hacia arriba), escala `esc`.
  void Orientado(const ps2::Camara& cam, const ps2::Modelo& m, const V3& pos, const V3& dir, float esc) {
    if (!m.malla) return;
    const V3 f = Por(dir, 1.0f / std::max(Largo(dir), 1e-4f));
    V3 r{f.z, 0, -f.x};   // f x arriba
    r = Por(r, 1.0f / std::max(Largo(r), 1e-4f));
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    ps2::Malla w = m.malla;
    for (auto& t : w.tris)
      for (V3& p : t.p) p = Mas(pos, Por(Mas(Por(r, p.x), Mas(Por(u, p.y), Por(f, p.z))), esc));
    escena_.Poner(cam, w, tx_ef_, {0, 0, 0}, 0, 1.0f);
  }

  void HudPantalla(ImGuiIO& io, uint8_t* base, uint32_t mgr, const ps2::Camara& cam) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    const float k = 1.6f * a.s;
    // reticula (objeto 9, centro): anillo; la cruz si un hueso de Piccolo cae dentro (10 px + su radio en pantalla)
    bool dentro = false;
    if (cam.ok && juego_ != 15 && juego_ != 3) {
      const uint32_t x1 = mgr + kX0 + kXPaso;
      const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      for (uint32_t i = 0; i < 22 && !dentro; ++i) {
        const uint32_t ad = x1 + 1616 + 16 * i;
        ImVec2 s;
        float kk = 0;
        if (!cam.P({LoadF(base, ad), LoadF(base, ad + 4), LoadF(base, ad + 8)}, s, kk)) continue;
        dentro = std::hypot(s.x - c.x, s.y - c.y) <=
                 (juego_ == 8 ? (kDiscoRadio + kRadioHueso[i]) * kk : 10.0f * a.s + kRadioHueso[i] * kk);
      }
      const float x0 = dentro ? 60 : 7, x1r = dentro ? 107 : 48;
      const float w = (x1r - x0) * k * 0.8f, h = 56 * k * 0.8f;
      Trozo(fg, Hud(1), x0, 0, x1r, 56, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), k * 0.8f);
    }
    // TIME: el aro azul (t05) arriba en el centro con los segundos (referencias 083 y la del Kikoho: x 600 de 1200)
    const ImVec2 aro = Px(a, 564, 36);
    Trozo(fg, Hud(5), 0, 0, 72, 72, aro, k);
    Cifras(fg, int(std::ceil(restante_)), ImVec2(aro.x + 36 * k, aro.y + 36 * k), k * 0.62f);
    if (juego_ == 3) {   // ladrillos: mira 2D (9/1 t01: anillo; fijado, la cruz), disparos y ladrillos que quedan
      const float esc = io.DisplaySize.y / 448.0f;
      const ImVec2 c(io.DisplaySize.x * 0.5f + (mira_x_ - 320.0f) * esc, io.DisplaySize.y * 0.5f + (mira_y_ - 224.0f) * esc);
      const float x0 = fijado_ ? 60 : 7, x1r = fijado_ ? 107 : 48;
      const float w = (x1r - x0) * k * 0.8f, h = 56 * k * 0.8f;
      Trozo(fg, Hud(1), x0, 0, x1r, 56, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), k * 0.8f);
      const ImVec2 b0 = Px(a, 150, 812);
      Trozo(fg, Hud(19), 0, 0, 304, 56, b0, k * 0.8f);   // los 7 disparos (estrellas: rotos)
      for (int i = 0; i < std::min(aciertos_, 7); ++i)
        Trozo(fg, Hud(20), 0, 0, 40, 40, ImVec2(b0.x + (14 + 34 * float(i)) * k * 0.8f, b0.y + 8 * k * 0.8f), k * 0.8f);
      Cifras(fg, tiros_, ImVec2(Px(a, 100, 830).x, Px(a, 100, 830).y), k * 0.5f);
      const ImVec2 aro = Px(a, 564, 36);   // ladrillos que quedan (MgFly_AddToCounter) en el aro azul, arriba al centro
      Trozo(fg, Hud(5), 0, 0, 72, 72, aro, k);
      Cifras(fg, quedan_, ImVec2(aro.x + 36 * k, aro.y + 36 * k), k * 0.62f);
      return;
    }
    if (juego_ == 15) {   // MSN016: energia de Ten (cara t22 en su aro t12, barra), peligro (arco t09 + escala t07)
      const ImVec2 b0 = Px(a, 150, 812);
      Trozo(fg, Hud(6), 0, 0, 240, 40, b0, k);
      const float e = std::clamp(energia_, 0.0f, 1.0f);
      if (Hud(3) && e > 0)
        fg->AddImage(Hud(3).id(), ImVec2(b0.x + 24 * k, b0.y + 14 * k), ImVec2(b0.x + (24 + 192 * e) * k, b0.y + 26 * k),
                     ImVec2(0, 0), ImVec2(e, 1));
      Trozo(fg, Hud(22), 0, 0, 64, 64, Px(a, 28, 752), k);
      Trozo(fg, Hud(12), 0, 0, 88, 80, Px(a, 6, 730), k);
      // medidor como el de IW (referencia del usuario 1_ten_kikoho_danger, area 1200x900): el arco t09 a la derecha,
      // 11 segmentos de calor dentro de su canal negro (IW los pinta por codigo hasta el nivel: UiFade_SetRatio), los
      // numeros t07 fila a fila dentro del panel azul, DANGER (t11) fijo arriba, SAFE (t10) fijo abajo y el triangulo
      // t08 a la izquierda del canal, a la altura del nivel (auditoria10/C)
      constexpr float kS = 1.75f;                           // unidades del area por px de t09
      const ImVec2 o9 = Px(a, 890, 300);
      const float ks = kS * a.s;                            // px de pantalla por px de t09
      Trozo(fg, Hud(9), 0, 0, 176, 312, o9, ks);
      auto canal = [](float y) {                            // centro del canal negro de t09 (px de t09): arco ")"
        const float u = (y - 156.0f) / 130.0f;
        return 84.0f - 45.0f * u * u;
      };
      const float q = std::clamp(cy_ / kAltura15, 0.0f, 1.0f);   // nivel: la altura de Cell (como antes)
      constexpr int kSeg = 11;
      constexpr float y_top = 38.0f, y_bot = 278.0f, paso = (y_bot - y_top) / kSeg;
      static constexpr ImU32 kCol[kSeg] = {IM_COL32(245, 245, 245, 255), IM_COL32(245, 245, 245, 255),
                                           IM_COL32(250, 230, 40, 255),  IM_COL32(250, 210, 30, 255),
                                           IM_COL32(250, 185, 20, 255),  IM_COL32(250, 150, 20, 255),
                                           IM_COL32(245, 120, 20, 255),  IM_COL32(240, 90, 20, 255),
                                           IM_COL32(230, 50, 20, 255),   IM_COL32(220, 30, 20, 255),
                                           IM_COL32(205, 20, 20, 255)};
      for (int i = 0; i < kSeg; ++i) {                      // 0 abajo (blanco) .. 10 arriba (rojo)
        const float yb = y_bot - paso * float(i) - 1.5f, yt = yb - paso + 3.0f;
        const float xb = canal(yb), xt = canal(yt), hw = 8.0f;
        const bool lleno = float(i) < q * float(kSeg) + 0.5f;
        const ImU32 c = lleno ? kCol[i] : (kCol[i] & 0x00FFFFFFu) | 0x50000000u;
        auto P = [&](float x, float y) { return ImVec2(o9.x + x * ks, o9.y + y * ks); };
        fg->AddQuadFilled(P(xt - hw, yt), P(xt + hw, yt), P(xb + hw, yb), P(xb - hw, yb), c);
      }
      for (int i = 0; i < 7; ++i)                           // 350 .. 50, una fila de t07 (16 px) cada 66,7 del area
        Trozo(fg, Hud(7), 0, 16.0f * float(i), 40, 16.0f * float(i + 1), Px(a, 1072, 376 + 66.7f * float(i)), 1.5f * a.s);
      Trozo(fg, Hud(11), 0, 0, 80, 24, Px(a, 990, 300), 1.8f * a.s);   // DANGER arriba
      Trozo(fg, Hud(10), 0, 0, 72, 24, Px(a, 990, 826), 1.8f * a.s);   // SAFE abajo
      const float yq = y_bot - (y_bot - y_top) * q;         // triangulo del nivel, a la izquierda del canal
      Trozo(fg, Hud(8), 0, 0, 24, 24, ImVec2(o9.x + (canal(yq) - 8.0f - 24.0f) * ks, o9.y + yq * ks - 12.0f * a.s * 1.4f),
            1.4f * a.s);
      // rombo del Kikoho (objeto 9/1 t17, el aro amarillo de trazos) en el centro
      const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      const float lado = 103.0f / 250.0f * io.DisplaySize.y * 0.5f * 2.0f;
      if (Hud(17)) fg->AddImage(Hud(17).id(), ImVec2(c.x - lado * 0.5f, c.y - lado * 0.5f), ImVec2(c.x + lado * 0.5f, c.y + lado * 0.5f),
                                ImVec2(55.0f / 168.0f, 0), ImVec2(105.0f / 168.0f, 1));
      return;
    }
    if (juego_ == 8) {   // MSN009: aciertos (estrellas t20 sobre la barra t19, 7 para ganar) y discos que quedan
      const ImVec2 e0 = Px(a, 150, 812);
      Trozo(fg, Hud(19), 0, 0, 304, 56, e0, k * 0.8f);
      for (int i = 0; i < std::min(aciertos_, 7); ++i)
        Trozo(fg, Hud(20), 0, 0, 40, 40, ImVec2(e0.x + (14 + 34 * float(i)) * k * 0.8f, e0.y + 8 * k * 0.8f), k * 0.8f);
      const ImVec2 dc = Px(a, 70, 830);
      if (!tx_ef_.empty() && tx_ef_[0])   // icono: la textura del Kienzan de IW (851/2 t00)
        fg->AddImage(tx_ef_[0].id(), ImVec2(dc.x - 26 * a.s, dc.y - 16 * a.s), ImVec2(dc.x + 26 * a.s, dc.y + 16 * a.s));
      Cifras(fg, tiros_, ImVec2(dc.x + 60 * a.s, dc.y), k * 0.5f);
      return;
    }
    // vida de Piccolo: cara (t23) en su aro (t12) y la barra (marco t06, relleno t03) abajo a la izquierda
    const ImVec2 b0 = Px(a, 150, 812);
    Trozo(fg, Hud(6), 0, 0, 240, 40, b0, k);
    const float e = std::clamp(float(vida_) / float(std::max(vida_max_, 1)), 0.0f, 1.0f);
    if (Hud(3) && e > 0)
      fg->AddImage(Hud(3).id(), ImVec2(b0.x + 24 * k, b0.y + 14 * k), ImVec2(b0.x + (24 + 192 * e) * k, b0.y + 26 * k),
                   ImVec2(0, 0), ImVec2(e, 1));
    Trozo(fg, Hud(23), 0, 0, 64, 64, Px(a, 28, 752), k);
    Trozo(fg, Hud(12), 0, 0, 88, 80, Px(a, 6, 730), k);
  }

  const Datos* hecho_ = nullptr;
  std::vector<ps2::Tex> tx_i_, tx_d_, hud_, tx_ef_, tx_lad_, tx_atlas_, tx_suma_;
  std::vector<std::shared_ptr<ame::Instancia>> efx_;
  std::shared_ptr<ame::Instancia> aura_;
  float giro_disco_ = 0;
  const Datos* datos_ = nullptr;
  // Lanza el #AME del hijo `hijo` del paquete en `pos` hacia `dir` (Effect_DispatchById: el id elige el hijo)
  std::shared_ptr<ame::Instancia> Lanzar(int hijo, const V3& pos, const V3& dir) {
    if (!datos_ || hijo < 0 || hijo >= int(datos_->efectos.size()) || !datos_->efectos[size_t(hijo)]) return nullptr;
    auto in = std::make_shared<ame::Instancia>();
    in->ef = &datos_->efectos[size_t(hijo)];
    in->origen = pos;
    in->dir = Unir(dir);
    efx_.push_back(in);
    return in;
  }
  void Efectos() {   // un cuadro de los efectos
    for (auto& e : efx_) ame::Cuadro(*e, semilla_);
    efx_.erase(std::remove_if(efx_.begin(), efx_.end(), [](const auto& e) { return !e->vivo; }), efx_.end());
  }
  ps2::Tex clear_;
  int juego_ = 9;
  ps2::Escena escena_;
  Fase fase_ = kListo;
  int t_ = 0, vida_ = 0, vida_max_ = 1, disparos_ = 0, mano_ = 0, esquiva_ = -1, esquiva_t_ = 0, auto_t_ = 0;
  float acc_ = 0, restante_ = 0, segundos_ = 0, az_ = 0, elev_ = 0, picc_fase_ = 0;
  float espera_[2] = {}, retroceso_[2] = {};
  int tiros_ = 0, aciertos_ = 0, fallos_seguidos_ = 0, disco_ = 0, carga_ = 0, pausa_ = 0, bloqueo_ = -1;
  struct Marca {
    int tipo = 0;
    float x = 0, z = 0, paso = 0, dura = 0;
  };
  std::array<Marca, 5> marcas_{};
  int marca_ = 0, marca_t_ = 0, cst_ = 0, cc0_ = 0, ce_t_ = 0, sub15_ = 0, t15_ = 0, kikoho_ = 0, dif15_ = 2;
  float mx_ = 0, mz_ = 200, cx_ = 0, cy_ = 0, cz_ = 0, cvy_ = 0, golpe_y_ = 0, rumbo_ = 0, wx_ = 0, wz_ = 0;
  float energia_ = 1, p15_ = -1;
  bool baja_usada_ = false;
  uint16_t antes15_ = 0;
  int quedan_ = 0, tramo_espera_ = 0;
  float mira_x_ = 320, mira_y_ = 224, progreso_ = 0, lad_vel_ = 1, lad_x_ = 0, lad_y_ = 0, lad_z_ = 0, tiro_t_ = 0;
  float lad_s_ = 18.0f;   // tamano del ladrillo que se proyecta (#MSS +0x08) para el radio de fijado
  std::array<float, 3> lad_a_{}, lad_b_{};
  bool ladrillo_vivo_ = false, fijado_ = false;
  bool tirada_ = false, fuego_antes_ = false;
  float combustible_ = 0;
  V3 disco_p_, disco_v_;
  uint32_t semilla_ = 1;

  std::vector<Rayo> rayos_;
  std::vector<std::pair<V3, int>> chispas_;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

// Carga (modelos de las manos y HUD de la copia del jugador) y el combate. 0x7D8: MSN010 (Piccolo); 0x7D7: MSN009.
bool Empezar(int clave, int fila, int dificultad) {
  auto d = std::make_shared<Datos>();
  d->clave = clave;
  d->fila = fila;
  d->dif = std::clamp(dificultad, 0, 5);
  d->juego = clave == 0x7D7 ? 8 : clave == 0x7DE ? 15 : clave == 0x7D3 ? 3 : 9;
  P::AfsPs2 cmn, usa;
  if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS") || !usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
    REXLOG_WARN("dbz3 iwdisparo: sin la copia de IW (DATA_CMN/DATA_USA)");
    return false;
  }
  if (d->juego == 3) {   // DATA_CMN 890: 1 ITEMB03_BRICK (+0 #AMT), 4 #MSS (bloque por dificultad en +0x2C)
    const std::vector<uint8_t> amb = cmn.Entrada(890);
    d->ladrillo = ps2::CargarModelo(amb, 1, 0, 0);
    const std::vector<uint8_t> mss = P::HijoAmb(amb, 4);
    auto i32 = [&](size_t o) { int32_t v = 0; if (o + 4 <= mss.size()) std::memcpy(&v, mss.data() + o, 4); return v; };
    auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= mss.size()) std::memcpy(&v, mss.data() + o, 4); return v; };
    if (mss.size() < 0x44 || std::memcmp(mss.data(), "#MSS", 4) != 0) {
      REXLOG_WARN("dbz3 iwdisparo: sin el #MSS de 890");
      return false;
    }
    int bdif = d->dif;
    size_t b = size_t(i32(0x2C + 4 * size_t(bdif)));
    while (bdif > 0 && b + 0x64 > mss.size()) {   // el #MSS EU solo trae 5 bloques (la copia USA tiene 6)
      --bdif;
      b = size_t(i32(0x2C + 4 * size_t(bdif)));
    }
    if (bdif != d->dif)
      REXLOG_INFO("dbz3 iwdisparo: la copia de IW no tiene bloque para la dificultad {}; se usa la {}",
                  d->dif, bdif);
    d->n_tramos = i32(b);
    d->lad_s = f32(b + 0x08);   // MgFly_SetupNextNode: +0x510 = bloque +0x08 (tamano proyectado del ladrillo)
    for (int k = 0; k < 5; ++k) d->umbral[k] = i32(b + 0x14 + 4 * size_t(k)), d->puntos[k] = i32(b + 0x28 + 4 * size_t(k));
    d->fallo = i32(b + 0x3C);
    const int pool = std::clamp(i32(b + 0x60), 0, 200);
    for (int k = 0; k < pool; ++k) {
      std::array<float, 7> t{};
      for (int j = 0; j < 7; ++j) t[size_t(j)] = f32(b + 0x64 + 0x1C * size_t(k) + 4 * size_t(j));
      d->tramos.push_back(t);
    }
    REXLOG_INFO("dbz3 iwdisparo: #MSS: {} ladrillos, umbrales {} {} {} {} {}, {} trayectorias", d->n_tramos, d->umbral[0],
                d->umbral[1], d->umbral[2], d->umbral[3], d->umbral[4], d->tramos.size());
    {   // F1: medidas del ladrillo y de las trayectorias (para la escala/camara)
      float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
      for (const auto& t : d->ladrillo.malla.tris)
        for (const V3& p : t.p) {
          const float v[3] = {p.x, p.y, p.z};
          for (int q = 0; q < 3; ++q) lo[q] = std::min(lo[q], v[q]), hi[q] = std::max(hi[q], v[q]);
        }
      REXLOG_INFO("dbz3 iwdisparo: ladrillo {} tri, caja ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}), {} texturas",
                  d->ladrillo.malla.tris.size(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], d->ladrillo.tex.size());
      for (size_t k = 0; k < std::min<size_t>(d->tramos.size(), 4); ++k) {
        const auto& t = d->tramos[k];
        REXLOG_INFO("dbz3 iwdisparo: tramo {}: v {:.2f} ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f})", k, t[0], t[1], t[2],
                    t[3], t[4], t[5], t[6]);
      }
    }
  }
  if (d->juego == 15) {   // manos de Ten: DATA_CMN 895 (0x37F, Minigame_LoadShootingData): 1 ITEMB00, 0 #AMT
    const std::vector<uint8_t> manos = cmn.Entrada(895);
    // con el color de material de cada parte: la textura 895/0 es blanca con lineas y la piel va en el material
    // (auditoria10/C: las manos salian blancas y planas)
    ps2::LeerAmo(P::HijoAmb(manos, 1), 0, d->mano_i.malla, nullptr, true, true, ps2::RampasPiel(P::HijoAmb(manos, 0)));
    const std::vector<uint8_t> t = P::HijoAmb(manos, 0);
    for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) d->mano_i.tex.push_back(P::TexturaAmt(t, j));
  }
  if (d->juego == 9) {   // el MSN009 no carga manos (sus carpetas: 873 #AMC y 935)
    const std::vector<uint8_t> manos = cmn.Entrada(892);   // #AMB: 0 #AMT, 1 ITEMB04, 2 #AMT, 3 ITEMB05
    for (int k = 0; k < 2; ++k) {   // sin Centrar: estan en el espacio de la camara
      ps2::Modelo& m = k == 0 ? d->mano_i : d->mano_d;
      ps2::LeerAmo(P::HijoAmb(manos, uint32_t(1 + 2 * k)), 0, m.malla);
      const std::vector<uint8_t> t = P::HijoAmb(manos, uint32_t(2 * k));
      for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) m.tex.push_back(P::TexturaAmt(t, j));
    }
  }
  {   // proyectil (ver Datos::efecto): sin Centrar, la cabeza del rayo / el centro del disco en el origen
    const int e = d->juego == 15 ? 935 : d->juego == 8 ? 851 : 842;   // 15: el paquete del minijuego (Kikoho)
    const std::vector<uint8_t> amb = cmn.Entrada(size_t(e));
    ps2::LeerAmo(P::HijoAmb(amb, e == 935 ? 1u : e == 851 ? 6u : 7u), 0, d->efecto.malla);
    const std::vector<uint8_t> t = P::HijoAmb(amb, e == 935 ? 0u : e == 851 ? 2u : 8u);
    for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) d->efecto.tex.push_back(P::TexturaAmt(t, j));
  }
  // Manos opacas: en las texturas de personaje de IW el alfa de la CLUT no es transparencia (elige la fila del toon) y
  // la PS2 las pinta opacas (P4: `TexturaAmt` ya pasa el alfa 0x80 -> 255, asi que proyectil y ladrillo van tal cual).
  for (auto& t : d->ladrillo.malla.tris) t.trans = false;   // ponytail: el ladrillo es macizo (bit alto del vtype)
  for (ps2::Modelo* m : {&d->mano_i, &d->mano_d})
    for (P::Imagen& im : m->tex)
      for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
  {   // efectos de IW (#AME) del paquete del minijuego: 935, o su copia 890/6 en el 3
    const std::vector<uint8_t> pq = d->juego == 3 ? P::HijoAmb(cmn.Entrada(890), 6) : cmn.Entrada(935);
    const std::vector<uint8_t> at = P::HijoAmb(pq, 0);
    for (int j = 0, n = P::CuantasAmt(at); j < n; ++j) d->atlas.push_back(P::TexturaAmt(at, j));
    d->efectos.resize(25);
    int cuantos = 0;
    for (uint32_t h = 3; h < 25; ++h) {
      d->efectos[h] = ame::Leer(P::HijoAmb(pq, h));
      cuantos += d->efectos[h] ? 1 : 0;
    }
    REXLOG_INFO("dbz3 iwdisparo: efectos de IW: {} #AME, atlas de {} texturas", cuantos, d->atlas.size());
  }
  const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(9), 1);
  for (int j = 0, n = P::CuantasAmt(hud); j < n; ++j) d->hud.push_back(P::TexturaAmt(hud, j));
  d->clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
  REXLOG_INFO("dbz3 iwdisparo: 0x{:X} ({}, dificultad {}): manos {}+{} triangulos, proyectil {} ({} texturas), HUD {} "
              "texturas", clave, d->juego == 15 ? "Kikoho contra Cell" : d->juego == 8 ? "Kienzan contra Freezer" : "rayos contra Piccolo", d->dif,
              d->mano_i.malla.tris.size(), d->mano_d.malla.tris.size(), d->efecto.malla.tris.size(), d->efecto.tex.size(),
              d->hud.size());
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_d = d;
    g_res = iwmini::Resultado{};
    g_res.clave = clave;
    g_res.fila = fila;
  }
  g_fin_pedido = false;
  g_mando = 0;
  g_palanca = 0;
  dbz3::launcher::DeferUi(&CreatePanel);
  // Jugador oculto (primera persona) y el blanco. MSN010: las manos de IW (892) tienen la piel lila y las unas oscuras de
  // Freezer (y W3 dice que Freezer dispara a Piccolo): el jugador es Freezer. MSN009: Krilin contra Freezer.
  // ponytail: el escenario de IW (Stage_BeginLoad de ctx+0x30 / ce78+0x20) no esta identificado: Namek de B3 (saga).
  // Escenario (Stage_BeginLoad del id que pone cada constructor: MSN016 ISLAND 2, MSN010 y MSN009 NAMEC 7; IW los
  // comparte con B3: tabla 0x44ABC0 -> mapa 60 / 64): Archipielago y Namek de B3.
  // 3: el campo de IW del planeta del Kaio (DATA_CMN 897, R8) si esta instalado; si no, Llanuras de B3. Goku (variante
  // 0; Piccolo/Yamcha/Ten segun el personaje que lleva el mapa, que R1 aun no pasa) y un rival oculto.
  const bool disco = d->juego == 8, ten = d->juego == 15, lad = d->juego == 3;
  const int j1 = lad ? kGoku : ten ? kTen : disco ? kKrilin : kFreezer;
  const int j2 = lad ? kRaditz : ten ? kCell : disco ? kFreezer : kPiccolo;
  const int campo = lad ? dbz3::escenarios::CampoIW(897) : -1;
  const int esc = lad ? (campo >= 0 ? campo : kLlanuras) : ten ? kIslas : kNamek;
  // Cell en su segunda forma (semiperfecto), como en IW (MSN016; sus animaciones de 914 son de ese esqueleto)
  if (!dbz3::hub::RequestBattleEx(j1, j2, esc, true, -1, 0, 0, 0, ten ? 1 : 0)) {
    REXLOG_WARN("dbz3 iwdisparo: el conector no acepto el combate");
    return false;
  }
  g_on = true;
  dbz3::combate::SetMuneco(1);         // el blanco quieto (mando virtual 2): lo mueve el minijuego
  dbz3::combate::OcultarHud(true);     // sin marcador de B3: el HUD es el de IW
  P::Reglas reglas;
  reglas.capsulas = false;
  P::VigilarCombate("iwdisparo", reglas, [](const P::Fin&) {
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    if (g_x1.load()) dbz3::anim::Corredor(g_x1.load(), false);
    g_on = false;
    g_cam15 = false;
    iwmini::Resultado r;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      r = g_res;   // sin Acabar (salio desde la pausa): no superado
      g_d.reset();
    }
    iwmini::Terminar(r);
  });
  return true;
}

const bool g_reg = (iwmini::RegistrarModulo(0x7D8, &Empezar), iwmini::RegistrarModulo(0x7D7, &Empezar),
                    iwmini::RegistrarModulo(0x7DE, &Empezar), iwmini::RegistrarModulo(0x7D3, &Empezar),
                    g_auto = std::getenv("DBZ3_TEST_IWMINI") && std::strstr(std::getenv("DBZ3_TEST_IWMINI"), "auto"),
                    true);

}  // namespace
}  // namespace dbz3::iwdisparo

namespace dbz3::iwmini {
// Hilo del juego, al empezar el dibujo (desde iwmini::Fotograma, una linea en iw_minijuegos.cpp): primera persona.
void FotogramaDisparo(uint8_t* base, uint32_t estado) {
  using namespace dbz3::iwdisparo;
  if (!g_on.load() || (estado != 3 && estado != 4)) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  // el jugador: oculto (cuerpo A y B, como el relevo de tagteam.cpp), quieto en el centro y con el mando virtual del
  // muneco (no se mueve ni ataca); el mando 0 lo lee el minijuego
  g_x1 = x1;
  const bool c15 = g_cam15.load();
  Poner(base, x0, c15 ? V3{g_jx.load(), 0, g_jz.load()} : V3{0, 0, 0});
  const bool lad = g_juego.load() == 3;   // 3: Goku se ve (tercera persona) y el rival no
  REX_STORE_U8(x0 + kOcultoA, lad ? 0 : 1);
  REX_STORE_U8(x0 + kOcultoB, lad ? 0 : 1);
  if (lad) REX_STORE_U8(x1 + kOcultoA, 1), REX_STORE_U8(x1 + kOcultoB, 1);
  if (REX_LOAD_U16(x0 + kPadIdx) != 1) REX_STORE_U16(x0 + kPadIdx, 1);
  StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));   // la vida de Piccolo es la del minijuego
  if (!g_suelo.load()) Poner(base, x1, {g_px.load(), g_py.load(), g_pz.load()});
  const uint32_t r = kPads;   // registro del mando 0
  g_mando = uint32_t(REX_LOAD_U16(r + 12)) | uint32_t(REX_LOAD_U16(r + 108)) << 16;
  g_palanca = int(REX_LOAD_U8(r + 26)) | int(REX_LOAD_U8(r + 27)) << 8;
  static uint32_t visto = 0;
  if (g_mando.load() != visto) {
    visto = g_mando.load();
    REXLOG_INFO("dbz3 iwdisparo: mando +12 0x{:04X} +108 0x{:04X}", visto & 0xFFFF, visto >> 16);
  }
  const V3 f = Dir(g_yaw.load(), g_polar.load());
  const float ojo[3] = {c15 ? g_ex.load() : 0, c15 ? g_ey.load() : kOjo, c15 ? g_ez.load() : 0};
  const float obj[3] = {c15 ? g_tx.load() : f.x * 30.0f, c15 ? g_ty.load() : kOjo + f.y * 30.0f,
                        c15 ? g_tz.load() : f.z * 30.0f};
  dbz3::cine::Camara(base, ojo, obj);
  static int64_t traza = 0;
  const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
  if (estado == 4 && ahora - traza > 2000) {
    traza = ahora;
    REXLOG_INFO("dbz3 iwdisparo: mando 0x{:04X} +108 0x{:04X} palanca {} {}; camara {:.2f} {:.2f}; Piccolo ({:.0f}, "
                "{:.0f}, {:.0f})", REX_LOAD_U16(r + 12), REX_LOAD_U16(r + 108), int8_t(REX_LOAD_U8(r + 26)),
                int8_t(REX_LOAD_U8(r + 27)), g_yaw.load(), g_polar.load(), g_px.load(), g_py.load(), g_pz.load());
  }
}
}  // namespace dbz3::iwmini
