// dbz3 - Modelos 3D de PS2 (IW, B1, B2: #AMO + #AMG + #AMT, animaciones #AMM) leidos en marcha de la copia del
// jugador y pintados encima del combate con ImGui, con la camara del combate y tapados por los luchadores (frente P4,
// 2026-10-16; lo usan iw_minijuegos.cpp y los frentes R2/R11).
//
// Uso (hilo propio para cargar; hilo de la interfaz para pintar):
//   pasarela::AfsPs2 cmn; cmn.Abrir(pasarela::BuscarIW(), "DATA_CMN.AFS");
//   ps2::Modelo m = ps2::CargarDeAfs(cmn, 118, 119, /*animable=*/true);        // #AMO suelto + su #AMT
//   ps2::Modelo o = ps2::CargarModelo(cmn.Entrada(887), 1, 0, 0);              // hijos de un #AMB (+ #AMM de giro)
//   m.anim = ps2::LeerAnim(pasarela::HijoAmb(cmn.Entrada(912), 1), 12, m.esq->padre.size());
//   // cada cuadro, en ImGui:
//   std::vector<ps2::Tex> tx = ps2::Texturas(m);                               // una vez (sube las texturas)
//   ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
//   ps2::Malla pose; ps2::Posar(*m.esq, m.anim, t_segundos, pose);            // si se anima
//   escena.Empezar();
//   escena.Poner(cam, pose, tx, {x, y, z}, giro_radianes, 1.0f);
//   escena.Tapar(cam, base, mgr + 2096);                                        // luchador 1 (y + 8240 el 2)
//   escena.Pintar(ImGui::GetBackgroundDrawList());
#pragma once

#include <imgui.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "pasarela.h"

namespace dbz3::ps2 {

struct V3 {
  float x = 0, y = 0, z = 0;
};

// ---- #AMO de PS2 (el mismo lector que awo_tools/amo2awo.py PS2AMO/PS2AMG) ----
// Cabecera: +0x10 huesos, +0x14 tabla de huesos (32 B; +0x10 = entrada del padre), +0x18 n.o de #AMG (variantes:
// oro/plata/bronce...), +0x1C tabla de #AMG. Por #AMG: +0x10 huesos, +0x14 ejes (80 B por hueso: cuaternion xyzw,
// traslacion, escala; +0x34 = su registro: grupo de partes o lista de pesos). Parte: +0 vtype, +8 textura, +0x90
// tamano (0x6000xxxx x 16), desde +0xA0 bloques VIF (+0xC vif, +0x10 1 = tira / 0 = triangulos, +0x14 vertices) de
// xyz [normal] [color] [uv].
struct Malla {
  struct Tri {
    V3 p[3];
    ImVec2 uv[3];
    int tex = 0;
    bool trans = false;      // vtype con el bit alto (barrera del aro, sombra): translucida
    uint8_t hueso[3] = {};   // hueso de cada vertice (para animar; ver Esqueleto)
    // color de cada vertice (vtype & 2: RGBA en floats 0..1 tras la normal), solo si se pidio en LeerAmo (frente R2: la
    // escena del deseo de B2 lo usa para el cielo oscuro y las nubes translucidas); si no, el de siempre
    bool con_color = false;
    ImU32 col[3] = {IM_COL32_WHITE, IM_COL32_WHITE, IM_COL32_WHITE};
    V3 n[3];   // normal de cada vertice (vtype & 1; la del modelo en mundo, unitaria), para el toon de Escena::Poner
  };
  std::vector<Tri> tris;
  explicit operator bool() const { return !tris.empty(); }
};
struct M34 {
  float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // filas: R*S | t
  V3 por(const V3& p) const {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
  }
  V3 gira(const V3& v) const {   // solo el giro (normales)
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z};
  }
  M34 operator*(const M34& o) const {
    M34 r;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 4; ++j)
        r.m[i * 4 + j] = m[i * 4] * o.m[j] + m[i * 4 + 1] * o.m[4 + j] + m[i * 4 + 2] * o.m[8 + j] + (j == 3 ? m[i * 4 + 3] : 0);
    return r;
  }
  M34 Inversa() const {   // rigida (sin escala): R^T | -R^T t
    M34 r;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = m[j * 4 + i];
      r.m[i * 4 + 3] = -(m[i] * m[3] + m[4 + i] * m[7] + m[8 + i] * m[11]);
    }
    return r;
  }
};
// Huesos del #AMO (para animar): padre, eje local en reposo y la inversa del de mundo; `crudo` = la malla en mundo antes
// de Centrar. Los vertices de las piezas van rigidos a su hueso; los del cuerpo (hueso 0), con sus pesos reales.
struct Esqueleto {
  struct Peso {
    uint8_t hueso;
    float w;
    V3 local;    // posicion en el espacio del hueso
    V3 normal;   // y su normal (bytes 16..27 de la entrada de 32 B)
  };
  std::vector<int> padre;
  std::vector<M34> local, inv_mundo;
  Malla crudo;
  V3 centro;                                // lo que quito Centrar en x/z
  std::vector<std::vector<Peso>> pesos;     // listas de pesos (una por vertice pesado)
  std::vector<int32_t> pesos_de;            // por vertice de triangulo (t * 3 + c): su lista, o -1 (rigido)
};
// Animacion de un #AMM de PS2: por hueso, claves angulares (cuadro, x, y, z en 1/65536 de vuelta; R = Rz Ry Rx) y
// posicionales (cuadro, xyz). Sin pista: el eje de reposo.
struct Anim {
  int cuadros = 0;
  std::vector<std::vector<std::array<uint16_t, 4>>> ang;
  std::vector<std::vector<std::array<float, 4>>> pos;
  // B2 (frente R2): las claves van sobre el eje de reposo (posicion = la de reposo + la clave; giro = el de reposo por el
  // de la clave). En IW (false, lo de siempre) sustituyen al de reposo. Shenron de B2 (CMN 328/3) trae posiciones 0:
  // sustituyendo, toda la cadena caia en el origen y el cuerpo quedaba en columna.
  bool relativa = false;
  // Medido en PCSX2 (23/10, instancia gp-0x7028, bancos CMN 931): B2 aplica los giros de las pistas ABSOLUTOS (el hueso sin pista
  // conserva el reposo) y la posicion de la pista sustituye; `absoluta` con `relativa` = piel de B2 pero claves absolutas.
  bool absoluta = false;
  explicit operator bool() const { return cuadros > 0 && !ang.empty(); }
};
using AnimIW = Anim;
// Giro del #AMM que va con un objeto (887/2 el aro, 887/6 la moneda): una pista de claves (valor, cuadro), valor en
// 1/65536 de vuelta, en bucle de `cuadros` a 60 por segundo; cada vuelta del bucle avanza `vuelta`.
struct Giro {
  std::vector<std::pair<int, float>> claves;   // (cuadro, angulo en radianes)
  int cuadros = 0;
  float vuelta = 0;
  float En(float t) const {
    if (cuadros <= 0 || claves.size() < 2) return 0;
    const float f = t * 60.0f, bucles = std::floor(f / float(cuadros)), c = f - bucles * float(cuadros);
    size_t i = 1;
    while (i < claves.size() && float(claves[i].first) < c) ++i;
    const auto& a = claves[i - 1];
    const auto b = i < claves.size() ? claves[i] : std::make_pair(cuadros, vuelta);
    const float u = b.first > a.first ? (c - float(a.first)) / float(b.first - a.first) : 0.0f;
    return bucles * vuelta + a.second + (b.second - a.second) * u;
  }
};
struct Modelo {
  Malla malla;                      // centrada: x/z en el centro, y minima = 0 (se pone en el suelo de su punto)
  std::vector<pasarela::Imagen> tex;   // #AMT que va con el #AMO
  Giro giro;
  std::shared_ptr<Esqueleto> esq;   // solo los cargados como animables
  Anim anim;
};

// ---- Carga (cualquier hilo) ----
Giro LeerGiro(const std::vector<uint8_t>& amm);
// material (F1, Goku del mapa de IW, DATA_CMN 1093/2): con colores, las partes con textura y sin color de vertice se
// tinen con su color de material (+0x30); sus texturas son blancas con detalles (como modula el GS de PS2).
// piel_rampas: mascara de los indices de rampa (u32(parte+0x0C)) de las partes de piel (RampasPiel); por defecto la 3
// (Goku del mapa de IW)
bool LeerAmo(const std::vector<uint8_t>& amo, int variante, Malla& out, Esqueleto* esq = nullptr, bool colores = false,
             bool material = false, uint32_t piel_rampas = 1u << 3);
// Rampas de piel de un #AMT de IW: las texturas rampa (bandera 0x80000000) azuladas (media B - R >= 40); auditoria10/E
uint32_t RampasPiel(const std::vector<uint8_t>& amt);
// Animacion `anim` de un #AMM de PS2 para un esqueleto de `huesos` huesos (mismo orden).
Anim LeerAnim(const std::vector<uint8_t>& amm, uint32_t anim, size_t huesos);
// Malla de `esq` en el segundo `t` (bucle a 60 cuadros por segundo) de `an`, con el centro x/z de Centrar.
void Posar(const Esqueleto& esq, const Anim& an, float t, Malla& out);
void Centrar(Malla& m);
// #AMO y #AMT sueltos (entradas propias de un AFS); animable: con esqueleto y pesos para Posar.
// colores (frente R2, B2): color por vertice (vtype & 2) y, en las partes sin textura, el color de material (+0x30).
Modelo CargarSuelto(const std::vector<uint8_t>& amo, const std::vector<uint8_t>& amt, bool animable = false,
                    bool colores = false, bool material = false);
Modelo CargarDeAfs(pasarela::AfsPs2& afs, size_t amo, size_t amt, bool animable = false);
// Hijos `amo` y `amt` de un #AMB (y el #AMM de giro en amo + 1).
Modelo CargarModelo(const std::vector<uint8_t>& amb, uint32_t amo, uint32_t amt, int variante);

// ---- Pintado (hilo de la interfaz) ----
// Camara del combate: vista mgr+0x60 (p * M, filas d, u, -f; delante z < 0) y fov vertical mgr+0x1C4.
struct Camara {
  float m[16] = {};
  float fov = 0.785f;
  ImVec2 c, h;   // centro y media pantalla (px)
  bool ok = false;
  bool P(const V3& p, ImVec2& o, float& k) const {
    const float xv = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float yv = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float zv = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    if (!(zv <= -1.0f) || !std::isfinite(xv) || !std::isfinite(yv) || !std::isfinite(zv)) return false;   // y NaN fuera
    const float f = h.y / std::tan(fov * 0.5f);
    k = f / -zv;   // px por unidad a esa distancia
    o = ImVec2(c.x + xv * k, c.y - yv * k);
    return true;
  }
  // Profundidad del juego (frente R15): planos cerca/lejos de la camara del combate (mgr+0x20 = 1, mgr+0x24 = 65535,
  // volcado) y proyeccion D3D normal, 0 cerca y 1 lejos (prueba LESS): z = lejos / (lejos - cerca) * (1 - cerca / d).
  // Medido con la sonda DBZ3_ZBUF_DEBUG contra las esferas de Goku (d 25-31: z de la escena 0.964-0.968, d ~ 1 / (1 - z))
  // y el cielo (0.9997). za = 0 (otra camara): -1, sin prueba de profundidad.
  float za = 0, zb = 0;   // cerca, lejos
  float Prof(float k) const {   // k de P()
    if (za <= 0) return -1.0f;
    const float d = h.y / std::tan(fov * 0.5f) / k;   // distancia en vista
    return std::clamp(zb / (zb - za) * (1.0f - za / d), 0.0f, 1.0f);
  }
};
Camara CamaraCombate(uint8_t* base, ImVec2 pantalla);
struct Tex {
  std::unique_ptr<rex::ui::ImmediateTexture> t;
  float w = 0, h = 0;
  bool prof = false;   // R15: de las que se prueban contra el z-buffer del juego (solo para Escena)
  ImTextureID id() const { return reinterpret_cast<ImTextureID>(t.get()); }
  explicit operator bool() const { return t != nullptr; }
};
Tex Crear(const pasarela::Imagen& im, bool prof = false);
// Texturas de un modelo para Escena: con el z-buffer del juego si el SDK lo permite (R15, D3D12). Solo se pintan con
// Escena::Pintar (sus vertices van empaquetados); para dibujos 2D, Crear.
std::vector<Tex> Texturas(const Modelo& m);
// R15: 0 = sin z-buffer en la interfaz (Vulkan o SDK viejo), 1 = se puede, 2 = el ultimo cuadro se probo contra el.
int EstadoProfundidad();
// Triangulos de un cuadro: se ponen todos los modelos, se tapa con los luchadores y se pintan de lejos a cerca.
class Escena {
 public:
  void Empezar() { tris_.clear(); }
  size_t Cuantos() const { return tris_.size(); }   // triangulos puestos en este cuadro
  // malla en `pos`, girada `giro` (radianes) en vertical y escalada `esc`
  void Poner(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro, float esc);
  // Lo mismo con toon (opcional, por modelo): 3 tonos por vertice con la luz de la camara (la de B3 en vista,
  // (-0.45, 0.55, 0.70)) y un borde negro de `borde` unidades de modelo (caras de detras empujadas por la normal, como
  // hacen B3 e IW). Necesita las normales de la malla (vtype & 1). Para IW y para las figuras de B2 (frente R2).
  struct Toon {
    float borde = 0.12f;
    float tonos[3] = {1.0f, 0.78f, 0.58f};   // luz, media, sombra
    float cortes[2] = {0.25f, -0.25f};       // n.l por encima de cortes[0]: luz; de cortes[1]: media; si no, sombra
  };
  void Poner(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro, float esc,
             const Toon& toon);
  // Oclusion por el luchador X: sus 22 esferas de cuerpo (centros X+1616 + 16 i, radios [[X+2996] + 24 + 4 i]); un
  // triangulo mas lejos que una esfera y con su centro dentro de ella no se pinta. Se mantiene con el
  // z-buffer del juego (R15: escenario y luchadores tapan pixel a pixel) como respaldo, por orden del coordinador.
  void Tapar(const Camara& cam, uint8_t* base, uint32_t x);
  void Pintar(ImDrawList* dl);

 private:
  void PonerT(const Camara& cam, const Malla& malla, const std::vector<Tex>& tx, const V3& pos, float giro, float esc,
              const Toon* toon);
  struct Tri {
    ImVec2 p[3], uv[3];
    ImTextureID tex{};
    ImU32 col = IM_COL32_WHITE;
    bool vcol = false;   // con color por vertice (Malla::Tri::con_color)
    ImU32 cols[3] = {};
    float k = 0;   // px por unidad: mas pequeno = mas lejos
    bool prof = false;   // R15: con prueba de profundidad (vertices empaquetados, textura de Tex::prof)
    float z[3] = {-1, -1, -1};   // R15: profundidad del juego de cada vertice (Camara::Prof)
    bool borde = false;          // del borde toon (lo primero que se deja si no caben los vertices)
  };
  struct Tapa {
    ImVec2 c;
    float r = 0, k = 0;
  };
  std::vector<Tri> tris_;
  std::vector<Tapa> tapas_;
};

}  // namespace dbz3::ps2
