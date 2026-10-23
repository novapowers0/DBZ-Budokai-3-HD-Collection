// dbz3 - Reproductor reducido de los efectos #AME de Infinite World (frente R13, tercera vuelta). Sacado de
// iw_mini_disparo.cpp para usarlo tambien en los minijuegos a pie (el polvo del sprint, 934/26: auditoria7/O).
#pragma once

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ps2_modelo.h"

namespace dbz3::iwame {
using ps2::V3;
inline V3 Mas(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Por(const V3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Largo(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// ---- Efectos de IW (#AME v2, el mismo formato que usa B3): reproductor reducido (frente R13, tercera vuelta).
// Motor de IW (decomp EU 0x26F9C0-0x278340; tablas EU 0x4724A0 emisores y 0x472510 particulas, bloques de 16 B
// [iniciar, actualizar, pintar]): cabecera "#AME", 2, n.o de nodos, raiz 0x10; nodo +4 tipo (clase << 16 | subtipo),
// +8 banderas, +0x24 primer hijo, +0x28 hermano (desplazamientos en el archivo), +0x30 posicion.
// Emisor (0x1000x: 0 esfera, 1 cono, 2 caja, 3 radial): +0x50 duracion (-1 sin fin), +0x54 retraso, +0x58 factor de la
// velocidad del reloj, +0x5C/+0x60 radio (base + azar), +0x68/+0x6C velocidad (base + azar, por segundo), +0x74 tope
// de vivas por grupo, +0x78 por cuadro, +0x7C forma (cono: semiangulo +0x7C * pi).
// Particula (0x2000x: 0 linea, 1 cuadro girado, 2 rayo, 3 sprite, 4 modelo): +0x50 vida, +0x54 retraso, +0x58 factor
// de la velocidad del emisor, +0x5C/+0x60 tamano (medio = 0,5 (base + azar)), +0x68 -> +0x6C factor a lo largo de la
// vida, +0x70 + azar +0x74 angulo y +0x7C giro por segundo (cuadro girado, bandera 0x8), +0x80 -> +0x90 color (bandera
// 0x10), +0xA0 estado de mezcla y +0xA4 la textura del atlas del paquete (en el rayo +0xC0/+0xC4), +0xA8 rect UV
// (bandera 0x2000), +0xC4 cuadros de animacion y desde +0xC8 {duracion, u0, v0, u1, v1} (bandera 0x8000). Cada cuadro:
// edad + 1, posicion += velocidad / 60. ponytail: sin los afectores (nodos 0x3000x), sin la mezcla aditiva de la PS2
// (ImGui mezcla por alfa), sin el modelo (tipo 4); lineas y rayos como estelas a lo largo de su velocidad.
namespace ame {
struct Nodo {
  bool emisor = false;
  int tipo = 0;
  uint32_t flags = 0;
  V3 pos;
  float f[48] = {};   // floats desde +0x50
  int tex = -1, cuadros = 0;
  int estado = 0;   // +0xA0 (rayo +0xC0): palabra de mezcla de 0x472480 = ALPHA del GS: 0 0x44 normal, 1 0x48 suma,
                    // 2 0x42 resta, 3 0x06
  std::vector<std::array<float, 5>> anim;   // {duracion, u0, v0, u1, v1}
};
struct Efecto {   // un #AME: los emisores de la raiz con sus particulas hijas
  struct Grupo {
    Nodo emisor;
    std::vector<Nodo> parts;
  };
  std::vector<Grupo> grupos;
  explicit operator bool() const { return !grupos.empty(); }
};
inline Efecto Leer(const std::vector<uint8_t>& b) {
  Efecto e;
  if (b.size() < 0x20 || std::memcmp(b.data(), "#AME", 4) != 0) return e;
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
  auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
  auto nodo = [&](size_t o) {
    Nodo n;
    const uint32_t t = u32(o + 4);
    n.emisor = (t >> 16) == 1;
    n.tipo = int(t & 0xFFFF);
    n.flags = u32(o + 8);
    n.pos = {f32(o + 0x30), f32(o + 0x34), f32(o + 0x38)};
    for (size_t i = 0; i < 48; ++i) n.f[i] = f32(o + 0x50 + 4 * i);
    if (!n.emisor) {
      const size_t s = n.tipo == 2 ? 0x20 : 0;   // el rayo lleva sus campos 0x20 mas abajo
      n.tex = int(u32(o + 0xA4 + s));
      n.estado = int(u32(o + 0xA0 + s) & 3);
      if (n.flags & 0x8000) {
        n.cuadros = int(std::min<uint32_t>(u32(o + 0xC4 + s), 64));
        for (int k = 0; k < n.cuadros; ++k) {
          std::array<float, 5> a{};
          for (int j = 0; j < 5; ++j) a[size_t(j)] = f32(o + 0xC8 + s + 20 * size_t(k) + 4 * size_t(j));
          n.anim.push_back(a);
        }
      }
    }
    return n;
  };
  size_t o = u32(0x0C);
  for (int guarda = 0; o >= 0x10 && o + 0x60 <= b.size() && guarda < 256; ++guarda) {
    const uint32_t t = u32(o + 4);
    if ((t >> 16) == 1) {   // emisor: sus particulas por +0x24 y +0x28
      Efecto::Grupo g;
      g.emisor = nodo(o);
      for (size_t c = u32(o + 0x24), k = 0; c >= 0x10 && c + 0x60 <= b.size() && k < 32; c = u32(c + 0x28), ++k)
        if ((u32(c + 4) >> 16) == 2) g.parts.push_back(nodo(c));
      e.grupos.push_back(std::move(g));
    }
    o = u32(o + 0x28);
    if (o == 0) break;
  }
  return e;
}
struct Particula {
  V3 p, v;
  float edad = 0, vida = 1, medio = 1, ang = 0;
  const Nodo* n = nullptr;
};
struct Instancia {
  const Efecto* ef = nullptr;
  int atlas = 0;   // que atlas (0 el del paquete del minijuego)
  V3 origen, dir{0, 0, 1};
  float edad = 0, esc = 1;
  std::vector<float> acum;
  std::vector<std::vector<Particula>> ps;   // por grupo y particula hija: aplanado por grupo
  bool vivo = true;
  bool modelos = false;   // tambien las particulas de modelo (tipo 4); las pinta el que llama (PintarModelos)
};
inline float U(uint32_t& s) {
  s = s * 1103515245u + 12345u;
  return float((s >> 8) & 0xFFFF) / 65535.0f;
}
// Un cuadro (60 por segundo): emision y particulas.
inline void Cuadro(Instancia& in, uint32_t& sem) {
  if (!in.ef) return;
  const size_t ng = in.ef->grupos.size();
  if (in.acum.size() != ng) in.acum.assign(ng, 0), in.ps.assign(ng, {});
  in.edad += 1;
  const V3 f = in.dir;
  V3 r{f.z, 0, -f.x};
  r = Largo(r) < 1e-3f ? V3{1, 0, 0} : Por(r, 1.0f / Largo(r));
  const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
  bool queda = false;
  for (size_t g = 0; g < ng; ++g) {
    const auto& gr = in.ef->grupos[g];
    const float* E = gr.emisor.f;
    auto& lista = in.ps[g];
    const float dur = E[0] < 0 ? 600.0f : E[0], edad = in.edad - E[1];
    if (edad > 0 && edad <= dur) {
      queda = true;
      in.acum[g] += std::max(E[10], 0.0f);
      while (in.acum[g] >= 1.0f) {
        in.acum[g] -= 1.0f;
        if (E[9] >= 0 && float(lista.size()) >= E[9] * float(std::max<size_t>(gr.parts.size(), 1))) break;
        // direccion segun la forma del emisor
        V3 d = f;
        const float ph = U(sem) * 6.2831853f;
        if (gr.emisor.tipo == 0) {   // esfera
          const float z = U(sem) * 2.0f - 1.0f, q = std::sqrt(std::max(0.0f, 1.0f - z * z));
          d = {q * std::cos(ph), z, q * std::sin(ph)};
        } else if (gr.emisor.tipo == 1) {   // cono: semiangulo +0x7C * pi
          const float th = U(sem) * E[11] * 3.14159265f;
          d = Mas(Por(f, std::cos(th)), Por(Mas(Por(r, std::cos(ph)), Por(u, std::sin(ph))), std::sin(th)));
        } else if (gr.emisor.tipo == 3) {   // radial: en el plano perpendicular a la direccion
          d = Mas(Por(r, std::cos(ph)), Por(u, std::sin(ph)));
        }
        const V3 base = Mas(in.origen, Por(Mas(Por(r, gr.emisor.pos.x), Mas(Por(u, gr.emisor.pos.y), Por(f, gr.emisor.pos.z))), in.esc));
        const V3 off = Por(d, (E[3] + U(sem) * E[4]) * in.esc);
        const V3 vel = Por(d, (E[6] + U(sem) * E[7]) * in.esc);
        for (const Nodo& n : gr.parts) {
          if (n.tipo == 4 ? !in.modelos : n.tex < 0) continue;   // tipo 4 (modelo): lo pinta quien lo pidio
          Particula p;
          p.n = &n;
          p.p = Mas(Mas(base, off), Por(n.pos, in.esc));
          p.v = vel;
          p.edad = -std::max(n.f[1], 0.0f);
          p.vida = n.f[0] > 0 ? n.f[0] : 60.0f;
          p.medio = 0.5f * (n.f[3] + U(sem) * n.f[4]) * in.esc;
          p.ang = (n.f[8] + U(sem) * n.f[9]) * 0.017453292f;
          lista.push_back(p);
        }
      }
    }
    for (Particula& p : lista) {
      p.edad += 1;
      if (p.edad <= 0) continue;
      p.p = Mas(p.p, Por(p.v, 1.0f / 60.0f));
      if (p.n->tipo == 1 && (p.n->flags & 0x8)) p.ang += p.n->f[11] / 60.0f * 0.017453292f;
    }
    lista.erase(std::remove_if(lista.begin(), lista.end(), [](const Particula& p) { return p.edad >= p.vida; }),
                lista.end());
    if (!lista.empty()) queda = true;
  }
  in.vivo = queda;
}
// `suma`: el atlas con alfa = brillo (la mezcla de suma del GS, Cs·As + Cd, con la mezcla por alfa de ImGui).
// B2: la resta (0x42, Cd - Cs·As) ya se pinta; ImGui no permite cambiar el blend por draw, asi que se aproxima
// oscureciendo con el brillo de Cs (negro con alfa = brillo·alfa). No es 1:1 (marcado [H]).
inline void Pintar(const Instancia& in, const ps2::Camara& cam, const std::vector<ps2::Tex>& atlas_n,
                   const std::vector<ps2::Tex>& suma, ImDrawList* dl, float tope_px) {
  for (const auto& lista : in.ps)
    for (const Particula& p : lista) {
      const Nodo& n = *p.n;
      if (n.tipo == 4) continue;   // tipo 4 (modelo): lo pinta quien lo pidio; la resta (estado 2) si se pinta
      // ponytail: tambien la mezcla normal (0x44) con alfa = brillo: con el alfa de las texturas tal cual salian
      // manchas oscuras (sus texturas de efecto son grises con alfa plano); la 3 (0x06) va normal
      const std::vector<ps2::Tex>& atlas = n.estado == 3 ? atlas_n : suma;
      if (p.edad <= 0 || n.tex >= int(atlas.size()) || !atlas[size_t(n.tex)]) continue;
      const float t = std::clamp(p.edad / p.vida, 0.0f, 1.0f);
      const float* F = n.f;
      float col[4];
      for (int i = 0; i < 4; ++i)
        col[i] = std::clamp((n.flags & 0x10) ? F[12 + i] + (F[16 + i] - F[12 + i]) * t : F[12 + i], 0.0f, 1.0f);
      if (n.estado == 2) {   // 0x42: aproximacion de REV_SUBTRACT (ver cabecera de Pintar)
        const float br = std::max({col[0], col[1], col[2]});
        col[0] = col[1] = col[2] = 0.0f;
        col[3] = std::clamp(br * col[3], 0.0f, 1.0f);
      }
      const float tam = p.medio * (F[6] + (F[7] - F[6]) * t);
      ImVec2 s;
      float k = 0;
      if (!cam.P(p.p, s, k) || col[3] <= 0.01f || tam <= 0) continue;
      ImVec2 uv0(0, 0), uv1(1, 1);
      if (n.flags & 0x2000) uv0 = ImVec2(F[22], F[23]), uv1 = ImVec2(F[24], F[25]);
      if ((n.flags & 0x8000) && !n.anim.empty()) {   // cuadro de la animacion por la edad
        float acc = 0;
        size_t i = 0;
        for (; i + 1 < n.anim.size(); ++i) {
          acc += std::max(n.anim[i][0], 1.0f);
          if (p.edad < acc) break;
        }
        uv0 = ImVec2(n.anim[i][1], n.anim[i][2]), uv1 = ImVec2(n.anim[i][3], n.anim[i][4]);
      }
      const ImU32 c = IM_COL32(int(col[0] * 255), int(col[1] * 255), int(col[2] * 255), int(col[3] * 255));
      const ImTextureID id = atlas[size_t(n.tex)].id();
      if (n.tipo == 0 || n.tipo == 2) {   // linea y rayo: estela a lo largo de la velocidad
        const float lv = Largo(p.v);
        const V3 dirv = lv > 1e-3f ? Por(p.v, 1.0f / lv) : in.dir;
        ImVec2 s2;
        float k2 = 0;
        if (!cam.P(Mas(p.p, Por(dirv, -2.0f * tam)), s2, k2)) continue;
        const float ancho = std::clamp(tam * 0.25f * k, 1.0f, tope_px * 0.3f);
        const float dx = s.x - s2.x, dy = s.y - s2.y, l = std::max(std::hypot(dx, dy), 1e-3f);
        const ImVec2 nr(-dy / l * ancho, dx / l * ancho);
        dl->AddImageQuad(id, ImVec2(s2.x + nr.x, s2.y + nr.y), ImVec2(s.x + nr.x, s.y + nr.y), ImVec2(s.x - nr.x, s.y - nr.y),
                         ImVec2(s2.x - nr.x, s2.y - nr.y), uv0, ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), c);
      } else {   // cuadro girado y sprite, de cara a la camara
        const float rr = std::clamp(tam * k, 1.0f, tope_px);
        const float ca = std::cos(p.ang) * rr, sa = std::sin(p.ang) * rr;
        dl->AddImageQuad(id, ImVec2(s.x - ca + sa, s.y - sa - ca), ImVec2(s.x + ca + sa, s.y + sa - ca),
                         ImVec2(s.x + ca - sa, s.y + sa + ca), ImVec2(s.x - ca - sa, s.y - sa + ca), uv0,
                         ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), c);
      }
    }
}
}  // namespace ame

}  // namespace dbz3::iwame
