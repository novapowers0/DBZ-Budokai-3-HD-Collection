// dbz3 - Suelo real de los campos de Infinite World (frente R11, 2026-10-16).
//
// tools/campos_iw.py monta cada campo de IW (DATA_CMN 884/897/900/903/906/909) sobre la colision de Llanuras estirada
// (suelo plano a y = 0, paredes solo en el borde): el terreno que se ve sube a mesetas y baja a valles y el luchador
// flotaba o se metia bajo la roca. Aqui, la colision del propio campo (#WCF, la entrada siguiente), como la usa IW en
// sus minijuegos a pie (Char_UpdateMovementPhysics, decomp EU 0x2A8E20; no Coll_GetGroundHeightAtXZ, que es la de los
// escenarios de combate):
//  - Suelo: Collision_Raycast desde la cabeza (pos.y + altura del personaje) hasta 500 por debajo, mascara 7: el primer
//    triangulo que corta. Aqui, en la consulta de suelo del juego sub_820CB0A0(f1 = x, f2 = z, r5 = registro opcional)
//    -> f1 = altura (la usan el luchador, sus esferas del cuerpo (sub_8211B1F0) y los efectos que se posan): el plano
//    mas alto que no pasa de la cabeza del luchador que esta en (x, z) (o el mas alto, si no es de un luchador). Sin
//    triangulo, lo del juego (suelo plano a 0; IW: sin corte o a mas de 500, cae al vacio).
//  - Paredes: Collision_MoveSphere del sitio al destino (mascara 7), en sub_8210E700 (Paredes). Ver alli.
//  - Respaldo si el #WCF no se lee: la malla de los huesos de suelo del #AMO del campo (hijo 5; JIMEN, MITI/DAI,
//    ROCK/RISE, _A1G), como en la vuelta 1. Rejilla de 20 unidades con los triangulos de cada celda. Todo leido en
//    marcha de la copia del jugador (AfsPs2), en un hilo, una vez por campo; nunca se reparte.
#include "suelo_iw.h"

#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "pasarela.h"
#include "ports_guard.h"

namespace dbz3::suelo_iw {
namespace {
namespace P = dbz3::pasarela;

constexpr dbz3::GA kRecs = 0x82323760, kG = 0x823A3D80, kMgrPtr = 0x823D6CF4;
constexpr uint32_t kRec = 92, kAnfitrion = 16, kEntrada = 5060;   // escenarios.cpp / campos_iw.py (ENTRADA)
constexpr int kCampos[6] = {884, 897, 900, 903, 906, 909};         // sorted(CAMPOS) de campos_iw.py
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kOculto = 3261;     // luchadores (iw_minijuegos.cpp)
constexpr uint32_t kCopias[3] = {48, 64, 80};                      // la posicion esta tres veces (tagteam.cpp)
constexpr float kCelda = 20.0f;
constexpr float kSalto = 60.0f;                    // mas lejos en un cuadro = lo puso alguien (salida, relevo): vale
constexpr float kCabeza = 20.0f;   // perilla: H, la altura del personaje (IW: *(char+3000)+0xC, sin leer: el puntero
                                   // se pone fuera del decomp legible). El rayo de suelo sale de pos.y + H
// ponytail: radio de la esfera de las paredes calibrado, 4 (IW: H - 1 con H = *(char+3000)+0xC, sin encontrar: el
// puntero no se escribe en ninguna linea del decomp ni con sw/sq/sd a +0xBB8 en el ELF USA). Medido (R11, vueltas 4-5):
// con r = H - 1 = 14,7 (H de la cabeza de Goku en B3) no cabe por la rampa del 903; con 4 la sube y para en la meseta.
constexpr float kRadioPared = 4.0f;
// ponytail: la esfera va con su parte baja 8 por encima de los pies (centro a pies + 8 + r): lo de debajo (el escalon
// empinado al pie de la rampa del 903) se sube andando. Medido con E:\DBZ3HD_trabajo\R11\centro.py: con el centro a pies + 4
// u 8 la rampa no se sube; a pies + 12 o 16 se sube entera (y 59) y la meseta frena igual (-148, -598).
constexpr float kBajoEsfera = 8.0f;
constexpr float kCerca = 30.0f;    // consulta de suelo a menos de esto de un luchador = la suya

struct Tri {
  float a[3], b[3], c[3];
  float n[3], d;   // plano en mundo, n.p + d = 0: el del registro del #WCF (en la malla, el de sus vertices)
  uint32_t mat;    // material del #WCF: 7 solido (suelo, troncos, vallas), 6 copas y casa, 8 agua; la malla: 7
};
constexpr uint32_t kMascara = 7;   // rayo de suelo: Collision_Raycast con mascara 7 (ELF USA 0x296774: addiu t2, 7)
constexpr uint32_t kMascaraPared = 2;   // paredes: Collision_MoveSphere con mascara 2 (ELF USA 0x2967E4: addiu t1, 2)
constexpr float kParedNy = 0.7f;   // perilla: cara con |ny| menor = pared para el barrido lateral

bool Dentro(const Tri& t, float x, float z) {   // (x, z) dentro del triangulo en planta (cualquier sentido)
  auto lado = [&](const float* p, const float* q) { return (q[0] - p[0]) * (z - p[2]) - (q[2] - p[2]) * (x - p[0]); };
  const float s1 = lado(t.a, t.b), s2 = lado(t.b, t.c), s3 = lado(t.c, t.a);
  return (s1 >= -1e-3f && s2 >= -1e-3f && s3 >= -1e-3f) || (s1 <= 1e-3f && s2 <= 1e-3f && s3 <= 1e-3f);
}
// Corte del segmento p-q con el triangulo (Moller-Trumbore): fraccion del segmento, o -1.
// EU 0x1D6BF0 Coll_RayTriangleIntersect: el original corta el segmento con cada cara de la esfera BSP por el mismo
// metodo (punto de corte, y el signo de los tres productos para saber si cae dentro del triangulo); aqui la
// funcion devuelve la fraccion del segmento (0..1) y deja el punto a quien la llama.
float Corta(const Tri& t, const float p[3], const float q[3]) {
  float e1[3], e2[3], dir[3], h[3], s[3], qv[3];
  for (int i = 0; i < 3; ++i) e1[i] = t.b[i] - t.a[i], e2[i] = t.c[i] - t.a[i], dir[i] = q[i] - p[i], s[i] = p[i] - t.a[i];
  auto cruz = [](const float* a, const float* b, float* r) {
    r[0] = a[1] * b[2] - a[2] * b[1], r[1] = a[2] * b[0] - a[0] * b[2], r[2] = a[0] * b[1] - a[1] * b[0];
  };
  auto punto = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  cruz(dir, e2, h);
  const float det = punto(e1, h);
  if (std::fabs(det) < 1e-9f) return -1;
  const float f = 1.0f / det, u = f * punto(s, h);
  if (u < 0 || u > 1) return -1;
  cruz(s, e1, qv);
  const float v = f * punto(dir, qv);
  if (v < 0 || u + v > 1) return -1;
  const float tt = f * punto(e2, qv);
  return tt >= 0 && tt <= 1 ? tt : -1;
}

struct Terreno {
  float x0 = 0, z0 = 0;
  int nx = 0, nz = 0;
  bool wcf = false;   // triangulos del #WCF (si no, la malla de suelo del #AMO)
  // Mundo que da la vuelta (R7 quinta tanda): bits 1 (x) y 2 (z) de la palabra +0x10 del #MCF del campo (hijo 0), que
  // Stage_ParseResourceBundle (EU 0x307060) copia a cdb8+0x630 y Node_WrapPositionInGrid (EU 0x305300) usa. Leido de
  // DATA_CMN: 897/903/906/909 = 7, 900 = 0xB (envuelven x y z), 884 = 8 (no). Ventana [x0, x1) = la del juego
  // [lo, lo + tamano] de la rejilla de piezas (Node_BuildGridFromModel); ponytail: aqui el borde del #WCF (las piezas de
  // terreno lo cubren entero), no la rejilla de nombres de piezas.
  uint32_t envolver = 0;
  float x1 = 0, z1 = 0;
  std::vector<uint32_t> ini, lista;
  std::vector<Tri> tris;
  template <class F>
  void Celdas(float xa, float za, float xb, float zb, F&& f) const {
    const int i0 = std::max(0, int(std::floor((std::min(xa, xb) - x0) / kCelda)));
    const int i1 = std::min(nx - 1, int(std::floor((std::max(xa, xb) - x0) / kCelda)));
    const int j0 = std::max(0, int(std::floor((std::min(za, zb) - z0) / kCelda)));
    const int j1 = std::min(nz - 1, int(std::floor((std::max(za, zb) - z0) / kCelda)));
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i) {
        const size_t c = size_t(j) * size_t(nx) + size_t(i);
        for (uint32_t k = ini[c]; k < ini[c + 1]; ++k) f(tris[lista[k]]);
      }
  }
  // EU 0x1E7070 Stage_GetGroundHeight: el original recorre el BSP desde la hoja de (x,z), filtra caras por bit 0
  // del material y devuelve la primera cara que contiene el punto; en escenario 1 llama ademas a la altura tiled.
  // No es equivalente a Suelo: este port usa #WCF/rejilla, mascara de rayo a pie y elige la superficie mas alta
  // bajo y0, porque eso replica Collision_Raycast de los minijuegos, no la consulta de altura del BSP.
  // Altura del suelo en (x, z) para la consulta del juego sub_820CB0A0 y los minijuegos a pie. Modela el rayo
  // hacia abajo de Char_UpdateMovementPhysics (EU 0x2A8E20): desde la cabeza (y0) hacia abajo, mascara 7
  // (Collision_Raycast 0x2E77D0; ELF USA 0x296774: addiu t2,7) y de los cortes se queda la superficie MAS ALTA bajo
  // y0. NO es Stage_GetGroundHeight (0x1E7070): esa baja por el BSP (Coll_FindBspLeafXZ 0x1E4350), prueba el bit 0
  // del material (en el #WCF solo pasa el material 7; 0x1E4DB0/0x1E5C60 usan los bits 2/1) y devuelve la PRIMERA
  // cara valida, no la mas alta. El port sustituye el indice BSP por la rejilla (`Celdas`, ver
  // SUSTITUIDAS_ESCENARIOS.txt) y usa la semantica del rayo de a pie, que es la que miden los minijuegos de IW.
  // [H] el limite de 500 por debajo de la cabeza no se aplica: IW, sin corte o a mas de 500, cae al vacio; el port
  // deja entonces el resultado de B3 (no se toca el estado del luchador a ciegas). `y0` es el limite superior.
  bool Suelo(float x, float z, float y0, float& y) const {
    bool hay = false;
    Celdas(x, z, x, z, [&](const Tri& t) {
      if (!(t.mat & kMascara) || std::fabs(t.n[1]) < 1e-3f || !Dentro(t, x, z)) return;
      const float h = -(t.n[0] * x + t.n[2] * z + t.d) / t.n[1];
      if (h <= y0 + 1e-3f && (!hay || h > y)) y = h, hay = true;
    });
    return hay;
  }
  // Colision de un segmento con el #WCF: recorre los triangulos de la parte cuyo material pasa la mascara y se
  // queda el corte MAS CERCANO a `desde` (Coll_RayTriangleIntersect 0x1D6BF0). Modela Collision_Raycast (EU
  // 0x2E77D0, la que usan los minijuegos a pie: mascara 1 la camara y 7 el suelo).
  // EU 0x1E5C60 Coll_SegmentVsStageNearest: filtra por el bit 1 del material y se queda el mas cercano; aqui la
  // rejilla de celdas sustituye el recorrido de las hojas BSP.
  // EU 0x1E5DD0 Coll_SegmentVsStageNearest_Frag2: segunda mitad de la misma funcion (recorrido de las caras de la
  // hoja y afinado del corte), que es el bucle de abajo.
  // El Collect (0x1E4DB0, bit 2, TODOS los cortes) no se necesita: B3 solo pide el primero. La rejilla solo acelera
  // la busqueda; el orden geometrico del resultado (primer corte) es el del original.
  template <class V>
  const Tri* Rayo(const float p[3], const float q[3], uint32_t mascara, V&& vale, float& fr) const {
    const Tri* mejor = nullptr;
    fr = 2;
    Celdas(p[0], p[2], q[0], q[2], [&](const Tri& t) {
      if (!(t.mat & mascara) || !vale(t)) return;
      const float f = Corta(t, p, q);
      if (f >= 0 && f < fr) fr = f, mejor = &t;
    });
    return mejor;
  }
};

std::atomic<const Terreno*> g_terreno[6] = {};   // uno por campo, se queda cargado (pocos MB)
std::atomic<const Terreno*> g_malla[6] = {};     // la malla que se DIBUJA del campo (todo el #AMO del terreno)
std::atomic<bool> g_pedido[6] = {};
std::atomic<int> g_activo{-1};                    // campo cargado (indice de kCampos) o -1

bool HuesoDeSuelo(const std::string& n) {
  for (const char* k : {"JIMEN", "MITI", "DAI", "ROCK", "RISE"})
    if (n.find(k) != std::string::npos) return true;
  return n.size() > 2 && n.back() == 'G' && n[n.size() - 2] >= '0' && n[n.size() - 2] <= '9';   // 900: FFNMHA1_A1G
}

// Triangulos del #AMO de PS2 (como LeerAmo de iw_minijuegos.cpp, solo posiciones). `solo_suelo`: solo los huesos de
// suelo (respaldo de la colision, vuelta 1); si no, TODAS las partes = la malla que se DIBUJA en el campo.
void LeerSuelo(const std::vector<uint8_t>& b, std::vector<std::array<float, 9>>& out, bool solo_suelo = true) {
  auto u32 = [&](size_t o) -> uint32_t {
    uint32_t v = 0;
    if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
    return v;
  };
  auto f32 = [&](size_t o) -> float {
    float v = 0;
    if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
    return v;
  };
  if (b.size() < 0x40 || std::memcmp(b.data(), "#AMO", 4) != 0 || !u32(0x18)) return;
  const uint32_t tabla = u32(0x14), nombres = u32(0x24), a = u32(u32(0x1C));
  if (a + 0x20 > b.size() || std::memcmp(&b[a], "#AMG", 4) != 0) return;
  const uint32_t nb = std::min<uint32_t>(u32(a + 0x10), 4096), ejes = u32(a + 0x14);
  std::vector<std::array<float, 12>> mundo(nb);
  for (uint32_t i = 0; i < nb; ++i) {
    const size_t e = a + ejes + 80 * size_t(i);
    const float x = f32(e), y = f32(e + 4), z = f32(e + 8), w = f32(e + 12);
    const float s[3] = {f32(e + 32), f32(e + 36), f32(e + 40)};
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    std::array<float, 12> l{};
    for (int f = 0; f < 3; ++f) {
      for (int c = 0; c < 3; ++c) l[size_t(f * 4 + c)] = r[f * 3 + c] * s[c];
      l[size_t(f * 4 + 3)] = f32(e + 16 + 4 * size_t(f));
    }
    const uint32_t padre = u32(tabla + 32 * i + 0x10);
    const uint32_t ip = padre >= tabla ? (padre - tabla) / 32 : ~0u;
    if (padre && ip < i) {
      const auto& m = mundo[ip];
      std::array<float, 12> o{};
      for (int f = 0; f < 3; ++f)
        for (int c = 0; c < 4; ++c)
          o[size_t(f * 4 + c)] = m[size_t(f * 4)] * l[size_t(c)] + m[size_t(f * 4 + 1)] * l[size_t(4 + c)] +
                                 m[size_t(f * 4 + 2)] * l[size_t(8 + c)] + (c == 3 ? m[size_t(f * 4 + 3)] : 0.0f);
      mundo[i] = o;
    } else {
      mundo[i] = l;
    }
  }
  for (uint32_t i = 0; i < nb; ++i) {
    const size_t no = nombres + 32 * size_t(i);
    if (no + 32 > b.size()) break;
    const std::string nombre(reinterpret_cast<const char*>(&b[no]), strnlen(reinterpret_cast<const char*>(&b[no]), 32));
    const uint32_t brazo = u32(a + ejes + 80 * i + 0x34);
    const uint32_t grupo = brazo ? u32(a + brazo + 4) : 0;
    if (!grupo || (solo_suelo && !HuesoDeSuelo(nombre))) continue;
    const auto& m = mundo[i];
    const size_t g = a + grupo;
    const uint32_t n = std::min<uint32_t>(u32(g), 256), t = u32(g + 4);
    for (uint32_t k = 0; k < n; ++k) {
      const size_t po = a + grupo + u32(g + t + 4 * k);
      const uint32_t vtype = u32(po), tam = u32(po + 0x90);
      if ((vtype & 0xFFFF) == 0xFFFF || tam < 0x60000000 || tam >= 0x70000000) continue;
      size_t pos = po + 0xA0;
      const size_t fin = std::min(b.size(), pos + size_t(tam - 0x60000000) * 16);
      while (pos + 0x20 <= fin) {
        const uint32_t vif = u32(pos + 0xC), ft = u32(pos + 0x10), cnt = u32(pos + 0x14), num = (vif >> 16) & 0xFF;
        if (cnt == 0 || cnt > 0xFFFF || ((vif >> 24) & 0x7F) != 0x6C || num < 1) break;
        const uint32_t paso = (num - 1) * 16 / cnt;
        const size_t vp = pos + 0x20;
        if (paso < 16 || vp + size_t(cnt) * paso > b.size()) break;
        std::vector<std::array<float, 3>> vs(cnt);
        for (uint32_t v = 0; v < cnt; ++v) {
          const size_t o = vp + size_t(v) * paso;
          const float p[3] = {f32(o), f32(o + 4), f32(o + 8)};
          for (int f = 0; f < 3; ++f)
            vs[v][size_t(f)] = m[size_t(f * 4)] * p[0] + m[size_t(f * 4 + 1)] * p[1] + m[size_t(f * 4 + 2)] * p[2] +
                               m[size_t(f * 4 + 3)];
        }
        auto tri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
          out.push_back({vs[i0][0], vs[i0][1], vs[i0][2], vs[i1][0], vs[i1][1], vs[i1][2], vs[i2][0], vs[i2][1],
                         vs[i2][2]});
        };
        if (ft == 1) {
          for (uint32_t v = 0; v + 2 < cnt; ++v) tri(v, v + 1, v + 2);
        } else {
          for (uint32_t v = 0; v + 2 < cnt; v += 3) tri(v, v + 1, v + 2);
        }
        pos = vp + size_t(cnt) * paso;
      }
    }
  }
}

// #WCF (DATA_CMN de IW, entrada siguiente a la del campo, hijo 0), leido como Collision_LoadAndRelocFile y
// Collision_RelocTree (decomp EU 0x2E7C80 / 0x2E73A0): +0x0C = nodo raiz; +8 = n << 16 | 0x10 (n nodos de 0x90,
// seguidos). Nodo: +0x24 (u8) registros de plano en +0x28, +0x25 (u8) hijos en +0x2C, +0x3C nombre, +0x50 matriz 4x4
// de la pieza en mundo (filas; fila 3 = traslacion; comprobado: los planos del registro pasan por los vertices
// transformados, error < 0,25). Registro de plano (0x20): [nx ny nz d en mundo][grupo, material, 0, 0]; grupo =
// [n << 16, ?, n x (v1, v2, v3 punteros a vec4 locales, f)].
bool LeerWcf(const std::vector<uint8_t>& w, std::vector<Tri>& out) {
  auto u32 = [&](size_t o) -> uint32_t {
    uint32_t v = 0;
    if (o + 4 <= w.size()) std::memcpy(&v, &w[o], 4);
    return v;
  };
  auto f32 = [&](size_t o) -> float {
    float v = 0;
    if (o + 4 <= w.size()) std::memcpy(&v, &w[o], 4);
    return v;
  };
  if (w.size() < 0x20 || std::memcmp(w.data(), "#WCF", 4) != 0) return false;
  const uint32_t n = u32(8) >> 16, raiz = u32(0xC);
  if (!raiz || raiz + size_t(n) * 0x90 > w.size()) return false;
  for (uint32_t r = 0; r < n; ++r) {
    const size_t o = raiz + 0x90 * size_t(r);
    const uint32_t planos = w[o + 0x24], pr = u32(o + 0x28);
    if (!planos || !pr || pr + size_t(planos) * 32 > w.size()) continue;
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = f32(o + 0x50 + 4 * size_t(i));
    for (uint32_t k = 0; k < planos; ++k) {
      const size_t q = pr + 32 * size_t(k);
      const uint32_t g = u32(q + 16), mat = u32(q + 20), ng = u32(g) >> 16;
      if (!g || g + 8 + size_t(ng) * 16 > w.size()) continue;
      Tri t{};
      for (int i = 0; i < 3; ++i) t.n[i] = f32(q + 4 * size_t(i));
      t.d = f32(q + 12);
      t.mat = mat;
      for (uint32_t j = 0; j < ng; ++j) {
        float* v[3] = {t.a, t.b, t.c};
        bool bien = true;
        for (int i = 0; i < 3; ++i) {
          const uint32_t pv = u32(g + 8 + 16 * size_t(j) + 4 * size_t(i));
          if (!pv || pv + 12 > w.size()) bien = false;
          const float l[3] = {f32(pv), f32(pv + 4), f32(pv + 8)};
          for (int c = 0; c < 3; ++c) v[i][c] = l[0] * m[c] + l[1] * m[4 + c] + l[2] * m[8 + c] + m[12 + c];
        }
        if (bien) out.push_back(t);
      }
    }
  }
  return !out.empty();
}

// Triangulos en mundo de una lista cruda (9 floats) con la normal hacia arriba y material 7 (solido).
void Rellenar(const std::vector<std::array<float, 9>>& crudos, std::vector<Tri>& out) {
  for (const auto& c : crudos) {
    Tri r{};
    std::memcpy(r.a, &c[0], 12), std::memcpy(r.b, &c[3], 12), std::memcpy(r.c, &c[6], 12);
    const float u[3] = {r.b[0] - r.a[0], r.b[1] - r.a[1], r.b[2] - r.a[2]};
    const float v[3] = {r.c[0] - r.a[0], r.c[1] - r.a[1], r.c[2] - r.a[2]};
    float nn[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    if (l < 1e-6f) continue;
    const float s = nn[1] < 0 ? -1.0f / l : 1.0f / l;   // hacia arriba
    for (int i = 0; i < 3; ++i) r.n[i] = nn[i] * s;
    r.d = -(r.n[0] * r.a[0] + r.n[1] * r.a[1] + r.n[2] * r.a[2]);
    r.mat = 7;
    out.push_back(r);
  }
}

// Rejilla de celdas (kCelda) sobre los triangulos de `t`.
void Rejilla(Terreno& t) {
  float x1 = -1e30f, z1 = -1e30f;
  t.x0 = t.z0 = 1e30f;
  for (const Tri& r : t.tris) {
    t.x0 = std::min({t.x0, r.a[0], r.b[0], r.c[0]}), x1 = std::max({x1, r.a[0], r.b[0], r.c[0]});
    t.z0 = std::min({t.z0, r.a[2], r.b[2], r.c[2]}), z1 = std::max({z1, r.a[2], r.b[2], r.c[2]});
  }
  t.nx = int((x1 - t.x0) / kCelda) + 1;
  t.nz = int((z1 - t.z0) / kCelda) + 1;
  t.x1 = x1, t.z1 = z1;
  auto celdas = [&](const Tri& r, auto&& f) {
    const int i0 = int((std::min({r.a[0], r.b[0], r.c[0]}) - t.x0) / kCelda);
    const int i1 = int((std::max({r.a[0], r.b[0], r.c[0]}) - t.x0) / kCelda);
    const int j0 = int((std::min({r.a[2], r.b[2], r.c[2]}) - t.z0) / kCelda);
    const int j1 = int((std::max({r.a[2], r.b[2], r.c[2]}) - t.z0) / kCelda);
    for (int j = j0; j <= std::min(j1, t.nz - 1); ++j)
      for (int i = i0; i <= std::min(i1, t.nx - 1); ++i) f(size_t(j) * size_t(t.nx) + size_t(i));
  };
  t.ini.assign(size_t(t.nx) * size_t(t.nz) + 1, 0);
  for (const Tri& r : t.tris) celdas(r, [&](size_t c) { ++t.ini[c + 1]; });
  for (size_t c = 1; c < t.ini.size(); ++c) t.ini[c] += t.ini[c - 1];
  t.lista.resize(t.ini.back());
  std::vector<uint32_t> llenas(t.ini.begin(), t.ini.end() - 1);
  for (uint32_t k = 0; k < t.tris.size(); ++k) celdas(t.tris[k], [&](size_t c) { t.lista[llenas[c]++] = k; });
}

// Terreno del campo: la colision (#WCF o, sin el, la malla de suelo) y la malla que se DIBUJA (todo el #AMO del
// terreno, hijo 5). La camara usa la malla dibujada: el #WCF y ella coinciden en el 99,9 % de las columnas (medido en
// DS33) pero en los cantos de acantilado el #WCF da una altura que no se ve (cara vertical) y el ojo quedaba dentro.
void Construir(int campo, std::atomic<const Terreno*>& dst_col, std::atomic<const Terreno*>& dst_dib) {
  const std::filesystem::path iw = P::BuscarIW();
  P::AfsPs2 cmn;
  if (iw.empty() || !cmn.Abrir(iw, "DATA_CMN.AFS")) {
    REXLOG_WARN("dbz3 suelo_iw: sin la copia de Infinite World en ps2_games: el campo {} queda con suelo plano", campo);
    return;
  }
  auto* t = new Terreno;
  t->wcf = LeerWcf(P::HijoAmb(cmn.Entrada(size_t(campo) + 1), 0), t->tris);
  std::vector<std::array<float, 9>> dibujo;
  LeerSuelo(P::HijoAmb(cmn.Entrada(size_t(campo)), 5), dibujo, /*solo_suelo=*/false);
  if (!t->wcf) {   // respaldo: la malla de suelo del #AMO del campo (vuelta 1)
    t->tris.clear();
    std::vector<std::array<float, 9>> suelo;
    LeerSuelo(P::HijoAmb(cmn.Entrada(size_t(campo)), 5), suelo, /*solo_suelo=*/true);
    Rellenar(suelo, t->tris);
  }
  if (t->tris.empty()) {
    REXLOG_WARN("dbz3 suelo_iw: el campo {} no tiene #WCF ni malla de suelo: queda con suelo plano", campo);
    delete t;
    return;
  }
  if (const auto mcf = P::HijoAmb(cmn.Entrada(size_t(campo)), 0); mcf.size() >= 0x14 && std::memcmp(mcf.data(), "#MCF", 4) == 0)
    std::memcpy(&t->envolver, &mcf[0x10], 4), t->envolver &= 3;
  Rejilla(*t);
  REXLOG_INFO("dbz3 suelo_iw: campo {}: {} triangulos ({}), x {:.0f}..{:.0f}, z {:.0f}..{:.0f}, rejilla {}x{}, envuelve {}",
              campo, t->tris.size(), t->wcf ? "#WCF" : "malla del #AMO, sin #WCF", t->x0, t->x1, t->z0, t->z1, t->nx, t->nz,
              t->envolver);
  dst_col = t;
  auto* m = new Terreno;   // la malla dibujada: todas las partes del terreno (sin filtro de huesos)
  Rellenar(dibujo, m->tris);
  if (m->tris.empty()) {
    REXLOG_WARN("dbz3 suelo_iw: el campo {} no tiene malla del #AMO (hijo 5): la camara usa solo la colision", campo);
    delete m;
    return;
  }
  Rejilla(*m);
  REXLOG_INFO("dbz3 suelo_iw: campo {}: malla dibujada {} triangulos, x {:.0f}..{:.0f}, z {:.0f}..{:.0f}, rejilla {}x{}",
              campo, m->tris.size(), m->x0, m->x1, m->z0, m->z1, m->nx, m->nz);
  dst_dib = m;
}

// Campo de IW cargado: el alta de campos_iw.py esta en el registro anfitrion y es el escenario cargado (G+0).
int CampoCargado(uint8_t* base) {
  if (REX_LOAD_U32(kG) != kAnfitrion) return -1;
  const uint32_t e = REX_LOAD_U32(kRecs + kAnfitrion * kRec + 0x10);
  return e >= kEntrada && e < kEntrada + 6 ? int(e - kEntrada) : -1;
}

// Terreno del campo cargado (nullptr mientras se lee en su hilo, o sin campo de IW).
const Terreno* Activo(uint8_t* base) {
  const int k = CampoCargado(base);
  g_activo = k;
  if (k < 0) return nullptr;
  if (!g_pedido[k].exchange(true))
    std::thread([k] { Construir(kCampos[k], g_terreno[k], g_malla[k]); }).detach();
  return g_terreno[k].load();
}

// La malla que se dibuja del campo cargado (nullptr mientras se lee o sin campo).
const Terreno* ActivoDibujo() {
  const int k = g_activo.load();
  return k >= 0 ? g_malla[k].load() : nullptr;
}

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

struct Bueno {
  bool vale = false;
  float x = 0, y = 0, z = 0;
  float alto = kCabeza;   // H del luchador de B3 (Alto), para el rayo de suelo y el radio de la esfera
};
Bueno g_bueno[2];   // ultimo sitio bueno de cada luchador (paredes) y su altura (inicio del rayo de suelo)

// Prueba (DBZ3_SUELO_PRUEBA="x,z;x,z;..."): 6 s despues de que el minijuego ponga al luchador 0 en la salida, se le
// lleva a la primera marca y anda (0,2 por cuadro) por las demas, sin mando ni camara: rampas y paredes por un camino
// conocido del #WCF (E:\DBZ3HD_trabajo\R11\rampa.py). Las paredes y el suelo actuan igual que al andar.
void Prueba(uint8_t* base, uint32_t x) {
  static const std::vector<std::array<float, 2>> marcas = [] {
    std::vector<std::array<float, 2>> m;
    const char* v = std::getenv("DBZ3_SUELO_PRUEBA");
    for (const char* c = v; c && *c;) {
      float a, b;
      if (std::sscanf(c, "%f,%f", &a, &b) != 2) break;
      m.push_back({a, b});
      c = std::strchr(c, ';');
      if (c) ++c;
    }
    return m;
  }();
  if (marcas.empty()) return;
  static float sx = 1e9f, sz = 1e9f;
  static int quieto = -1;
  static size_t sig = 0;
  float px = LoadF(base, x + 64), pz = LoadF(base, x + 72);
  if (quieto < 0) {   // espera el salto a la salida
    if (sx < 1e8f && std::hypot(px - sx, pz - sz) > kSalto) quieto = 0;
    sx = px, sz = pz;
    return;
  }
  if (++quieto < 720 || sig > marcas.size()) return;
  const auto& m = marcas[sig == 0 ? 0 : sig - 1];
  if (sig == 0) {   // con el pivote (luchador 1): si quedan lejos, el juego los junta (medido: 330 u en 2 s)
    px = m[0], pz = m[1], sig = 1;
    for (const uint32_t o : kCopias) StoreF(base, x + kXPaso + o, px + 40.0f), StoreF(base, x + kXPaso + o + 8, pz);
  } else {
    const float dx = m[0] - px, dz = m[1] - pz, d = std::hypot(dx, dz);
    static float antes = 1e9f;
    static int sin_avance = 0;
    sin_avance = d < antes - 0.05f ? 0 : sin_avance + 1;
    antes = d;
    if (d < 0.5f || sin_avance > 60) {
      if (sin_avance > 60) REXLOG_INFO("dbz3 suelo_iw: prueba: la marca {} no se alcanza (pared): siguiente", sig);
      sin_avance = 0, antes = 1e9f;
      if (++sig > marcas.size()) REXLOG_INFO("dbz3 suelo_iw: prueba: fin del camino en ({:.0f}, {:.0f})", px, pz);
      return;
    }
    static const float largo = [] {   // DBZ3_SUELO_PRUEBA_PASO: paso mas largo (R7: llegar al borde y envolver)
      const char* v = std::getenv("DBZ3_SUELO_PRUEBA_PASO");
      return v ? float(std::atof(v)) : 0.1f;
    }();
    const float paso = std::min(largo, d);
    px += dx / d * paso, pz += dz / d * paso;
  }
  for (const uint32_t o : kCopias) StoreF(base, x + o, px), StoreF(base, x + o + 8, pz);
  static int registro = 0;
  if (++registro % 120 == 1) {
    float h = -9999;
    const Terreno* t = g_terreno[g_activo.load()].load();
    if (t) t->Suelo(px, pz, LoadF(base, x + 68) + kCabeza, h);
    REXLOG_INFO("dbz3 suelo_iw: prueba: marca {} luchador en ({:.0f}, {:.1f}, {:.0f}), suelo {:.1f}", sig, px,
                LoadF(base, x + 68), pz, h);
  }
}

// H del luchador de B3: lo alto de la esfera de la cabeza (la 3.a de las 22 del cuerpo: cintura, pecho, cuello, cabeza...;
// centro en X+1616 + 16 * 3, radio en [X+2996] + 24 + 4 * 3; las de los golpes, gigantes.cpp) sobre sus pies (X+68). Es
// la altura del personaje de IW (*(char+3000)+0xC) con el modelo de B3 que se ve. Solo la cabeza: con el maximo de las
// 22 esferas, los brazos levantados la subian a 31 (medido en el 906).
float Alto(uint8_t* base, uint32_t x) {
  const uint32_t tabla = REX_LOAD_U32(x + 2996);
  if (tabla < 0x40000000u || tabla >= 0xA0000000u) return kCabeza;
  const float alto = LoadF(base, x + 1616 + 16 * 3 + 4) + LoadF(base, tabla + 24 + 4 * 3) - LoadF(base, x + 68);
  return alto > 2.0f && alto < 400.0f ? alto : kCabeza;
}

// Punto del triangulo mas cercano a p (Ericson, Real-Time Collision Detection, 5.1.5).
void Cercano(const Tri& t, const float p[3], float c[3]) {
  float ab[3], ac[3], ap[3];
  for (int i = 0; i < 3; ++i) ab[i] = t.b[i] - t.a[i], ac[i] = t.c[i] - t.a[i], ap[i] = p[i] - t.a[i];
  auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
  auto en = [&](const float* o, float u, const float* e1, float v, const float* e2) {
    for (int i = 0; i < 3; ++i) c[i] = o[i] + u * e1[i] + v * e2[i];
  };
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return en(t.a, 0, ab, 0, ac);
  float bp[3];
  for (int i = 0; i < 3; ++i) bp[i] = p[i] - t.b[i];
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return en(t.b, 0, ab, 0, ac);
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return en(t.a, d1 / (d1 - d3), ab, 0, ac);
  float cp[3];
  for (int i = 0; i < 3; ++i) cp[i] = p[i] - t.c[i];
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return en(t.c, 0, ab, 0, ac);
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return en(t.a, 0, ab, d2 / (d2 - d6), ac);
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    for (int i = 0; i < 3; ++i) c[i] = t.b[i] + w * (t.c[i] - t.b[i]);
    return;
  }
  const float den = 1.0f / (va + vb + vc);
  return en(t.a, vb * den, ab, vc * den, ac);
}

// EU 0x2E63B0 Collision_SweepSphereVsModel: barrido de la esfera contra las caras del MODELO de colision (el
// modulo que recorren Collision_MoveSphere/SweepSphereVsModel): contactos con su distancia y su normal.
// EU 0x1E63B0 Coll_SphereSweepVsStageCollect: barrido de esfera contra la colision del escenario (mascara 2 en
// Collision_MoveSphere, EU 0x2E7640). El original barre la esfera de `desde` a `hasta`, junta los contactos de las
// caras y devuelve la penetracion de cada uno; Collision_MoveSphere quita al paso la componente que se opone a la
// normal y vuelve a probar. Aqui, `p` = centro de la esfera, apoyada en los pies (centro a pies + r); ponytail: el
// centro de IW (+0x640 del personaje) no esta medido; con el centro en los pies (el rayo de IW sale de
// +0x640.y + H) las caras empinadas de debajo frenaban al bajar una cuesta (medido con
// E:\DBZ3HD_trabajo\R11\barrer.py). Paso horizontal `d`: en el destino, el contacto mas profundo de una cara que se
// opone al paso (normal del punto mas cercano, en horizontal) quita al paso esa componente y se vuelve a probar
// (hasta 4 contactos). Solo cuentan las caras empinadas (|ny| < kParedNy): con el centro en los pies, los bordes de
// los triangulos de suelo dan normales horizontales y frenaban al pie de las rampas (medido,
// E:\DBZ3HD_trabajo\R11\barrer.py); el suelo andable lo lleva el rayo. ponytail: IW junta todos los contactos y aqui
// se aplica el mas profundo de cada pasada (la respuesta exacta de Collision_SweepSphereVsModel no esta leida).
// Devuelve el sitio final en x/z (la y la pone el suelo).
void Barrer(const Terreno* t, const float p[3], float dx, float dz, float r, float& ox, float& oz) {
  float d[3] = {dx, 0, dz};
  for (int it = 0; it < 4; ++it) {
    const float largo = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (largo < 1e-4f) break;
    const float q[3] = {p[0] + d[0], p[1] + d[1], p[2] + d[2]};
    float mejor = r, n[3] = {0, 0, 0};
    bool hay = false;
    t->Celdas(q[0] - r, q[2] - r, q[0] + r, q[2] + r, [&](const Tri& tr) {
      if (!(tr.mat & kMascaraPared) || std::fabs(tr.n[1]) >= kParedNy) return;   // el suelo andable lo lleva el rayo
      float c[3];
      Cercano(tr, q, c);
      float v[3] = {q[0] - c[0], q[1] - c[1], q[2] - c[2]};
      const float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      if (dist >= mejor) return;
      if (dist > 1e-3f) {
        for (float& e : v) e /= dist;
      } else {
        std::copy(tr.n, tr.n + 3, v);
      }
      if (v[0] * d[0] + v[1] * d[1] + v[2] * d[2] >= -1e-4f * largo) return;   // no se opone al paso
      mejor = dist, std::copy(v, v + 3, n), hay = true;
    });
    if (!hay) {
      ox = q[0], oz = q[2];
      return;
    }
    if (std::fabs(n[1]) < kParedNy) {   // empinada: desliza en horizontal
      const float h = std::hypot(n[0], n[2]);
      if (h < 1e-4f) break;
      n[0] /= h, n[1] = 0, n[2] /= h;
    }
    const float dn = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
    for (int i = 0; i < 3; ++i) d[i] -= dn * n[i];
  }
  ox = p[0], oz = p[2];   // sin salida en 4 contactos: se queda
}

// EU 0x225780 Battle_PushOutOfStageCollision (iw_sles55347.c l.235362): saca al luchador de la colision del escenario
// con un barrido de esfera. Si char+0x2063 (lo pone el movimiento del cuadro, l.219603 y siguientes) esta puesto:
//  - centro = char+0x640 y desplazamiento = char+0x640 - char+0x7A0 (lo que se movio en el cuadro, l.235372-235382);
//  - Coll_SphereSweepVsStageCollect(centro, desplazamiento, 0) junta los contactos; entre medias pone y quita el flag
// EU 0x1E1090 Stage_ClearFlag2 y EU 0x1E10B0 Stage_SetFlag2: son operaciones distintas del port; el original
// limpia/activa respectivamente el bit 0x2 de la palabra global de flags del stage (0x4DB2CC). En el port no hay
// estado equivalente ni se escribe esa palabra: el ciclo de vida y las propiedades de escena se gestionan en B3.
//    2 del escenario (Stage_SetFlag2/Stage_ClearFlag2, l.235385-235388);
//  - por cada contacto de tipo 1 (l.235401): penetracion = 8,0 - distancia (local_10 = 0x41000000) y push = normal *
//    penetracion; los que van en contra de los anteriores se descartan (l.235410-235434) y los demas se suman;
//  - la suma se divide por el numero de contactos (CameraAdjustment(1/n), l.235488) y se suma a char+0x40 (posicion,
//    l.235490-235494); Char_SyncPositionFromCameraLocation (l.235495) propaga el cambio;
//  - si iRam0049ca70+0x8d24 esta puesto, el mismo empuje va tambien al companero (char+0x1384, l.235509-235516).
// Aqui, el mismo calculo sobre las caras del #WCF: se miran las caras empinadas a menos de 8,0 del centro (el
// desplazamiento del cuadro solo acota la busqueda, kEmpujeMax), cada una aporta su normal por la penetracion (8,0 -
// distancia al punto mas cercano) si se opone al movimiento y se aplica la media de los contactos, que es lo que hace
// CameraAdjustment(1/n). Devuelve true si movio. Lo llama Paredes cuando el luchador viene colocado a mano (no hay
// paso que barrer): es el caso del original, en el que +0x2063 marca "hay que sacarlo".
constexpr float kEmpujeRadio = 8.0f;    // local_10 = 0x41000000 (l.235371)
constexpr float kEmpujeMax = 64.0f;     // local_c = 0x42800000 (l.235371)
bool Empujar(const Terreno* t, const float p[3], const float antes[3], float& ox, float& oz) {
  const float r2 = kEmpujeRadio * kEmpujeRadio;
  const float d[3] = {p[0] - antes[0], 0, p[2] - antes[2]};
  const float largo = std::sqrt(d[0] * d[0] + d[2] * d[2]);
  float suma[3] = {0, 0, 0};
  int n = 0;
  const float busca = kEmpujeRadio + (largo < kEmpujeMax ? largo : kEmpujeMax);
  t->Celdas(p[0] - busca, p[2] - busca, p[0] + busca, p[2] + busca, [&](const Tri& tr) {
    if (!(tr.mat & kMascaraPared) || std::fabs(tr.n[1]) >= kParedNy) return;   // el suelo andable no empuja
    float c[3];
    Cercano(tr, p, c);
    const float v[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
    const float dist2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (dist2 >= r2) return;
    if (largo > 1e-4f && v[0] * d[0] + v[2] * d[2] >= 0.0f) return;   // solo lo que se opone al movimiento
    const float dist = std::sqrt(dist2);
    const float pen = kEmpujeRadio - dist;
    if (pen <= 0.0f) return;
    for (int i = 0; i < 3; ++i) suma[i] += tr.n[i] * pen;
    ++n;
  });
  if (!n) return false;
  ox = p[0] + suma[0] / float(n);
  oz = p[2] + suma[2] / float(n);
  return std::fabs(ox - p[0]) > 1e-4f || std::fabs(oz - p[2]) > 1e-4f;
}

// Paredes de IW en la logica de luchadores (sub_8210E700): el paso de cada luchador (lo que movio el minijuego antes de
// la logica y lo que movio el juego en ella) pasa por Barrer con radio H - 1 (H de su modelo de B3). DBZ3_SUELO_PAREDES=0
// las quita.
void Paredes(uint8_t* base) {
  const Terreno* t = Activo(base);
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  static const bool activas = [] {
    const char* v = std::getenv("DBZ3_SUELO_PAREDES");
    return !(v && *v == '0');
  }();
  if (!t || !mgr) {
    g_bueno[0].vale = g_bueno[1].vale = false;
    return;
  }
  Prueba(base, mgr + kX0);
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + kX0 + k * kXPaso;
    Bueno& b = g_bueno[k];
    const float px = LoadF(base, x + 64), py = LoadF(base, x + 68), pz = LoadF(base, x + 72);
    const float alto = Alto(base, x);
    if (REX_LOAD_U8(x + kOculto)) {   // pivote de P4
      b = {true, px, py, pz, alto};
      continue;
    }
    float nx = px, ny = py, nz = pz;
    const bool barre = b.vale && activas && std::hypot(px - b.x, pz - b.z) <= kSalto;   // si no: puesto a mano o sin paredes
    const float r = kRadioPared, centro[3] = {b.x, b.y + kBajoEsfera + r, b.z};
    if (barre) {
      Barrer(t, centro, px - b.x, pz - b.z, r, nx, nz);
    } else if (activas) {
      // EU 0x225780 Battle_PushOutOfStageCollision: sin paso que barrer (puesto a mano: salida, relevo, prueba), el
      // original lo saca igual de la colision con su barrido de esfera de radio 8 y la media de los contactos.
      const float pos[3] = {px, py + kBajoEsfera + kEmpujeRadio, pz};
      const float antes[3] = {b.vale ? b.x : px, py, b.vale ? b.z : pz};
      Empujar(t, pos, antes, nx, nz);
    }
    if (std::fabs(nx - px) > 1e-3f || std::fabs(nz - pz) > 1e-3f) {
      ny = barre ? b.y : py;
      t->Suelo(nx, nz, ny + alto, ny);
      for (const uint32_t o : kCopias)
        StoreF(base, x + o, nx), StoreF(base, x + o + 4, ny), StoreF(base, x + o + 8, nz);
      static int avisos = 0;
      if (avisos < 40 && ++avisos)
        REXLOG_INFO("dbz3 suelo_iw: {}: luchador {} (H {:.1f}) de ({:.0f}, {:.0f}) a ({:.0f}, {:.0f}) queda en ({:.0f}, {:.0f})",
                    barre ? "pared" : "empuje", k, alto, b.x, b.z, px, pz, nx, nz);
    }
    // EU 0x305150 ObjectsCountEffect: calcula la extension de la rejilla en x/z y, si el bit de eje esta activo,
    // desplaza la coordenada una extension cuando rebasa el limite de media extension. Aqui se aplica el mismo
    // criterio de wrap a la posicion del campo; no se cuenta ni reordena la lista de efectos del original.
    // EU 0x305300 Node_WrapPositionInGrid: con el mundo que da la vuelta (bits 1 = x y 2 = z del #MCF +0x10,
    // iRam0049cdb8+0x630) y fuera de [lo, lo + extension) se suma o resta un periodo (un salto, no un tope). Como va
    // sobre el sitio del luchador tras la logica, cubre el andar, el scatto y el desplazamiento de la raiz de las
    // poses (Char_ApplyPoseRootMotion tambien envuelve). ponytail: sin las copias vecinas del campo en pantalla (IW
    // pinta la rejilla envuelta, Node_RenderVisibleGrid): se ve el borde del terreno antes del salto.
    bool envuelto = false;
    auto envuelve = [&](float& v, float lo, float hi) {
      const float p = hi - lo;
      if (p < 100.0f) return;
      if (v < lo) v += p, envuelto = true;
      else if (v >= hi) v -= p, envuelto = true;
    };
    if (t->envolver & 1) envuelve(nx, t->x0, t->x1);
    if (t->envolver & 2) envuelve(nz, t->z0, t->z1);
    if (envuelto) {
      t->Suelo(nx, nz, ny + alto, ny);
      for (const uint32_t o : kCopias)
        StoreF(base, x + o, nx), StoreF(base, x + o + 4, ny), StoreF(base, x + o + 8, nz);
      REXLOG_INFO("dbz3 suelo_iw: envoltura: luchador {} pasa al otro lado, ({:.0f}, {:.0f})", k, nx, nz);
    }
    b = {true, nx, ny, nz, alto};
  }
  static int cuadro = 0;   // registro de la altura cada 5 s (pruebas: el luchador sobre el terreno)
  if (++cuadro % 600 == 0) {
    const uint32_t x = mgr + kX0;
    const float px = LoadF(base, x + 64), py = LoadF(base, x + 68), pz = LoadF(base, x + 72);
    float h = -9999;
    t->Suelo(px, pz, py + g_bueno[0].alto, h);
    REXLOG_INFO("dbz3 suelo_iw: luchador 0 en ({:.0f}, {:.1f}, {:.0f}), suelo {:.1f}, H {:.1f}", px, py, pz, h,
                g_bueno[0].alto);
  }
}

// Luchador cuyo sitio esta a menos de kCerca de (x, z), o nullptr.
const Bueno* Cerca(float x, float z) {
  for (const Bueno& b : g_bueno)
    if (b.vale && std::hypot(x - b.x, z - b.z) < kCerca) return &b;
  return nullptr;
}

// Inicio del rayo de suelo para una consulta en (x, z): la cabeza del luchador que esta ahi (como IW); si no es de un
// luchador (efectos), desde arriba del todo.
float InicioRayo(float x, float z) {
  const Bueno* b = Cerca(x, z);
  return b ? b->y + b->alto : 1e9f;
}

}  // namespace

float Altura(float x, float z, float sin_suelo) {
  const int k = g_activo.load();
  const Terreno* t = k >= 0 ? g_terreno[k].load() : nullptr;
  float y = sin_suelo;
  if (t) t->Suelo(x, z, 1e9f, y);
  return y;
}

// Altura de la MALLA QUE SE DIBUJA del campo en (x, z): la superficie horizontal mas alta del #AMO del terreno. En un
// canto de acantilado (solo caras verticales) no hay superficie en planta y devuelve `sin_suelo`; es justo lo que hay
// que distinguir de Altura() (el #WCF da alli una altura que no se ve).
float AlturaMalla(float x, float z, float sin_suelo) {
  const Terreno* t = ActivoDibujo();
  float y = sin_suelo;
  if (t) t->Suelo(x, z, 1e9f, y);
  return y;
}

// Rayo contra la MALLA QUE SE DIBUJA (todas sus caras, verticales incluidas): punto de corte mas cercano a `desde`.
// Es lo que necesita la camara del minijuego (IW Camera_UpdateFollow, EU 0x297DE0: MiniGame_DrawPromptSingle), que
// colisiona con lo que se ve, no con el mapa de alturas.
bool RayoMalla(const float desde[3], const float hasta[3], float corte[3]) {
  const Terreno* t = ActivoDibujo();
  float fr;
  if (!t || !t->Rayo(desde, hasta, 7, [](const Tri&) { return true; }, fr)) return false;
  for (int i = 0; i < 3; ++i) corte[i] = desde[i] + (hasta[i] - desde[i]) * fr;
  return true;
}

// Punto a menos de `radio` de la malla dibujada (la camara pegada a una cara: el plano cercano entra en la roca).
bool CercaMalla(const float p[3], float radio) {
  const Terreno* t = ActivoDibujo();
  if (!t || radio <= 0) return false;
  const float r2 = radio * radio;
  bool cerca = false;
  t->Celdas(p[0] - radio, p[2] - radio, p[0] + radio, p[2] + radio, [&](const Tri& tr) {
    if (cerca) return;
    float c[3];
    Cercano(tr, p, c);
    const float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
    if (dx * dx + dy * dy + dz * dz < r2) cerca = true;
  });
  return cerca;
}

// EU 0x2E7640 Collision_MoveSphere: mueve una esfera de `desde` a `hasta` contra la colision del escenario (mascara
// 7 en los minijuegos a pie) y devuelve el sitio resultante; el paso se recorta al chocar y la altura la fija el rayo
// de suelo desde la cabeza. Aqui se usa desde los minijuegos con el sitio llevado a mano (P4) y `escala` = radio.
bool Mover(const float desde[3], const float hasta[3], float salida[3], float escala) {
  const int k = g_activo.load();
  const Terreno* t = k >= 0 ? g_terreno[k].load() : nullptr;
  std::copy(hasta, hasta + 3, salida);
  if (!t) return false;
  const Bueno* b = Cerca(desde[0], desde[2]);
  const float alto = b ? b->alto : kCabeza;
  const float r = kRadioPared * escala, centro[3] = {desde[0], desde[1] + kBajoEsfera + r, desde[2]};
  Barrer(t, centro, hasta[0] - desde[0], hasta[2] - desde[2], r, salida[0], salida[2]);
  salida[1] = desde[1];
  t->Suelo(salida[0], salida[2], desde[1] + alto, salida[1]);
  return true;
}

// EU 0x2E77D0 Collision_Raycast: rayo de `desde` a `hasta` contra la colision del escenario con la mascara de
// materiales; devuelve el corte mas cercano a `desde` (la camara del minijuego lo usa con la mascara 1, el rayo de
// suelo con la 7). La logica del corte y del filtro de materiales es la de Coll_SegmentVsStageNearest (0x1E5C60).
bool Rayo(const float desde[3], const float hasta[3], uint32_t mascara, float corte[3]) {
  const int k = g_activo.load();
  const Terreno* t = k >= 0 ? g_terreno[k].load() : nullptr;
  float fr;
  if (!t || !t->Rayo(desde, hasta, mascara, [](const Tri&) { return true; }, fr)) return false;
  for (int i = 0; i < 3; ++i) corte[i] = desde[i] + (hasta[i] - desde[i]) * fr;
  return true;
}

// R12 (vuelo): suelo fijo a la altura de vuelo de IW
namespace {
std::atomic<bool> g_fijo_on{false};
std::atomic<float> g_fijo{0.0f};
}  // namespace
void SueloFijo(float altura, bool on) {
  g_fijo = altura;
  g_fijo_on = on;
}

}  // namespace dbz3::suelo_iw

// Consulta de suelo del juego: con un campo de IW cargado, la de IW (rayo hacia abajo desde la cabeza sobre el #WCF).
DBZ3_HOOK_EU(sub_820CB0A0) {
  const float x = float(ctx.f1.f64), z = float(ctx.f2.f64);
  orig(ctx, base);
  if (dbz3::suelo_iw::g_fijo_on.load()) {   // R12: vuelo de IW (SueloFijo)
    ctx.f1.f64 = double(dbz3::suelo_iw::g_fijo.load());
    return;
  }
  if (const auto* t = dbz3::suelo_iw::Activo(base)) {
    float y;
    if (t->Suelo(x, z, dbz3::suelo_iw::InicioRayo(x, z), y)) ctx.f1.f64 = double(y);
  }
}

// Logica de los luchadores (cada fotograma de combate): paredes antes (lo que movio el minijuego) y despues (el juego);
// tambien guarda la altura de cada luchador para el rayo de suelo. US y EU (pareja 8210E700 -> 8210E800 de
// tools/eu_map.py: bloque de 188 funciones iguales, 0 diferencias sin explicar; docs/14_frentes/eu_mapa.txt).
DBZ3_HOOK_EU(sub_8210E700) {
  if (!dbz3::ports::GuardLuchadores(base)) return;   // lista/accion a medias: no se llama a un puntero basura
  dbz3::suelo_iw::Paredes(base);
  orig(ctx, base);
  dbz3::suelo_iw::Paredes(base);
}
