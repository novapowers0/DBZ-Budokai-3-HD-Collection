// TRAD/2: ver iw_gs_traductor.h. Semantica del GS (manual del GS, cap. 3-5) en lo que el IW usa: cola de vertices por
// tipo de primitiva, XYZ2/XYZF2 con disparo y XYZ3/XYZF3 sin el, contexto 1/2 por CTXT, PRMODECONT/PRMODE.
#include "iw_gs_traductor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef IWTRAD_PRUEBA
#include <rex/cvar.h>
#include <rex/logging.h>   // PAQ_5: las trazas del 7DE solo existen con el runtime (no en el banco)
REXCVAR_DEFINE_BOOL(dbz3_iw_traducido, true, "DBZ3/IW",
                    "IW: dibujo traducido de las ordenes GS del original (de serie encendido desde R23 IW-7: G1-G6 cerradas)");
REXCVAR_DEFINE_BOOL(dbz3_b2_traducido, false, "DBZ3/Dev",
                    "B2: el tablero original corre desde su ELF y su 2D se traduce (en pruebas; plan traducir, no recrear)");
REXCVAR_DEFINE_BOOL(dbz3_trad_panoramico, true, "DBZ3/Dev",
                    "B2/IW traducidos: 2D en rectangulo 4:3 centrado (sin estirar a 16:9) y parche de FOV panoramico del 3D");
#endif
#include <string>

// R23 G2 (pruebas): md5 como gs_captura.py. libavutil ya va enlazada (video SFD); el banco suelto no la tiene.
#ifndef IWTRAD_PRUEBA
extern "C" void av_md5_sum(uint8_t* dst, const uint8_t* src, size_t len);
#endif
static std::string Md5Hex(const uint8_t* p, size_t n) {
#ifndef IWTRAD_PRUEBA
  static const bool crudo = std::getenv("DBZ3_IWTRAD_VIF_CRUDO") != nullptr;   // pruebas: datos en claro
  if (crudo) {
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) std::snprintf(&s[i * 2], 3, "%02x", p[i]);
    return s;
  }
  uint8_t h[16];
  av_md5_sum(h, p, n);
  char s[33];
  for (int i = 0; i < 16; ++i) std::snprintf(s + i * 2, 3, "%02x", h[i]);
  return s;
#else
  (void)p; (void)n; return "-";
#endif
}

namespace dbz3::iwtrad {
namespace {
constexpr uint64_t M32 = 0xFFFFFFFFull;
constexpr int kNecesita[8] = {1, 2, 2, 3, 3, 3, 2, 0};   // punto, linea, tira, tri, tira tri, abanico, sprite, reservado
}  // namespace

bool Activo() {
#ifndef IWTRAD_PRUEBA
  return REXCVAR_GET(dbz3_iw_traducido) && DisponibleIW();
#else
  return true;
#endif
}

bool Panoramico() {
#ifndef IWTRAD_PRUEBA
  return REXCVAR_GET(dbz3_trad_panoramico);
#else
  return false;
#endif
}

void CajaX(float W, float H, float& ancho, float& ox) {   // rectangulo 4:3 centrado dentro de la ventana W x H
  ancho = W; ox = 0;
  if (Panoramico() && W > H * 4.0f / 3.0f) { ancho = H * 4.0f / 3.0f; ox = (W - ancho) * 0.5f; }
}

bool ActivoB2() {
  // R18, decision 16/11: el tablero nativo es la unica autoridad del jugador.
  // El ELF se conserva para comparaciones fuera de la partida, incluso si un
  // ajuste antiguo conserva dbz3_b2_traducido=true.
  return false;
}

void Traductor::Registro(int reg, uint64_t v) {
  if (reg < 0 || reg >= 0x64) return;
  g_[reg] = v;
  switch (reg) {
    case 0x00: n_ = 0; total_ = 0; break;                       // PRIM: cola nueva
    case 0x04: Vertice_(v, true, true); break;                  // XYZF2
    case 0x05: Vertice_(v, false, true); break;                 // XYZ2
    case 0x0C: Vertice_(v, true, false); break;                 // XYZF3
    case 0x0D: Vertice_(v, false, false); break;                // XYZ3
    case 0x53:                                                  // TRXDIR: 0 = de host a local
      if ((v & 3) == 0) subidas.push_back({g_[0x50], g_[0x51], g_[0x52], {}});
      break;
    case 0x61:                                                  // FINISH, tambien por GIF A+D
#ifndef IWTRAD_PRUEBA
      if (!terminado && std::getenv("DBZ3_TEST_IW_7DE_FINISH"))
        REXLOG_INFO("dbz3 iwtrad 7de: registro 0x61 (FINISH) vertices {}", n_);   // PAQ_5: instrumentacion
#endif
      terminado = true;
      break;
    default: break;
  }
}

void Traductor::Imagen(const uint8_t* d, size_t n) {
  if (subidas.empty()) return;                                  // IMAGE sin TRXDIR: el GS lo ignora
  auto& s = subidas.back().datos;
  s.insert(s.end(), d, d + n);
}

void Traductor::Vertice_(uint64_t v, bool f, bool kick) {
  Vertice x;
  x.x = uint16_t(v & 0xFFFF);
  x.y = uint16_t((v >> 16) & 0xFFFF);
  if (f) { x.z = uint32_t((v >> 32) & 0xFFFFFF); x.f = uint8_t(v >> 56); }
  else   { x.z = uint32_t(v >> 32); x.f = uint8_t(g_[0x0A] >> 56); }   // XYZ2 toma F de FOG
  x.rgba = uint32_t(g_[1] & M32);
  x.q = uint32_t(g_[1] >> 32);
  x.st = g_[2];
  x.uv = uint32_t(g_[3] & 0x3FFF3FFF);
  const int tipo = int(g_[0] & 7), need = kNecesita[tipo];
  if (!need) return;
  cola_[n_++] = x;
  ++total_;
  if (n_ < need) return;
  if (kick) {
    const uint64_t attr = (g_[0x1A] & 1) ? g_[0] : g_[0x1B];
    const int c = int((attr >> 9) & 1);
    Primitiva p;
    p.prim = uint32_t(tipo | (attr & 0x7F8));
    p.tex0 = g_[0x06 + c]; p.tex1 = g_[0x14 + c]; p.clamp = g_[0x08 + c]; p.xyoffset = g_[0x18 + c];
    p.scissor = g_[0x40 + c]; p.alpha = g_[0x42 + c]; p.test = g_[0x47 + c]; p.fba = g_[0x4A + c];
    p.frame = g_[0x4C + c]; p.zbuf = g_[0x4E + c];
    p.pabe = g_[0x49]; p.dimx = g_[0x44]; p.dthe = g_[0x45]; p.colclamp = g_[0x46]; p.fogcol = g_[0x3D]; p.texa = g_[0x3B];
    p.nv = need;
    p.nsub = uint32_t(subidas.size());
    for (int i = 0; i < need; ++i) p.v[i] = cola_[i];
    prims.push_back(p);
  }
  switch (tipo) {
    case 2: cola_[0] = cola_[1]; n_ = 1; break;                       // tira de lineas
    case 4: cola_[0] = cola_[1]; cola_[1] = cola_[2]; n_ = 2; break;  // tira de triangulos
    case 5: cola_[1] = cola_[2]; n_ = 2; break;                       // abanico: el primero se queda
    default: n_ = 0; break;
  }
}

void Traductor::Empaquetado(int r, uint64_t a, uint64_t b) {
  switch (r) {
    case 0x0: Registro(0, a & 0x7FF); break;
    case 0x1: Registro(1, (a & 0xFF) | ((a >> 32) & 0xFF) << 8 | (b & 0xFF) << 16 | ((b >> 32) & 0xFF) << 24 | uint64_t(q_) << 32); break;
    case 0x2: q_ = uint32_t(b & M32); Registro(2, a); break;
    case 0x3: Registro(3, (a & 0x3FFF) | ((a >> 32) & 0x3FFF) << 16); break;
    case 0x4: Registro(((b >> 47) & 1) ? 0x0C : 0x04,
                       (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | ((b >> 4) & 0xFFFFFF) << 32 | ((b >> 36) & 0xFF) << 56); break;
    case 0x5: Registro(((b >> 47) & 1) ? 0x0D : 0x05, (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | (b & M32) << 32); break;
    case 0xE: Registro(int(b & 0xFF), a); break;               // A+D
    case 0xF: break;                                           // NOP
    default: Registro(r, a); break;
  }
}

void Traductor::Gif(const uint8_t* d, size_t n) {
  auto u64 = [&](size_t p) { uint64_t v; std::memcpy(&v, d + p, 8); return v; };
  size_t p = 0;
  while (p + 16 <= n) {
    const uint64_t lo = u64(p), hi = u64(p + 8);
    p += 16;
    const int nloop = int(lo & 0x7FFF), pre = int((lo >> 46) & 1), flg = int((lo >> 58) & 3);
    const uint64_t prim = (lo >> 47) & 0x7FF;
    int nreg = int((lo >> 60) & 15);
    if (!nreg) nreg = 16;
    if (pre && flg != 2) Registro(0, prim);
    if (flg == 0) {                                            // PACKED
      for (int k = 0; k < nloop; ++k)
        for (int i = 0; i < nreg; ++i) {
          if (p + 16 > n) return;
          Empaquetado(int((hi >> (4 * i)) & 15), u64(p), u64(p + 8));
          p += 16;
        }
    } else if (flg == 1) {                                     // REGLIST
      for (int k = 0; k < nloop * nreg; ++k) {
        if (p + 8 > n) return;
        const int r = int((hi >> (4 * (k % nreg))) & 15);
        if (r != 15) Registro(r, u64(p));
        p += 8;
      }
      p = (p + 15) & ~size_t(15);
    } else {                                                   // IMAGE
      const size_t tam = size_t(nloop) * 16;
      Imagen(d + p, tam <= n - p ? tam : n - p);
      p += tam;
    }
  }
}

namespace {
constexpr uint8_t kBloque32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                     {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
constexpr uint8_t kBloque4[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                    {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
// palabra (0..15) de una columna de 8x2 en PSMCT32: x 0..7, fila 0..1
inline uint32_t Palabra(uint32_t x, uint32_t f) { return ((x >> 1) << 2) | (f << 1) | (x & 1); }
}  // namespace

uint32_t Vram::Dir32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const {
  const uint32_t pag = (x >> 6) + (y >> 5) * (bw ? bw : 1), bl = kBloque32[(y >> 3) & 3][(x >> 3) & 7];
  const uint32_t yy = y & 7;
  return ((bp + pag * 32 + bl) * 64 + (yy >> 1) * 16 + Palabra(x & 7, yy & 1)) & 0xFFFFF;
}
uint32_t Vram::Dir8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const {
  const uint32_t pag = (x >> 7) + (y >> 6) * ((bw + 1) >> 1), bl = kBloque32[(y >> 4) & 3][(x >> 4) & 7];
  const uint32_t xx = x & 15, yy = y & 15, col = yy >> 2, r = yy & 3;
  const uint32_t wx = (xx & 7) ^ ((((r >> 1) ^ (col & 1)) & 1) << 2);
  const uint32_t byte = col * 64 + Palabra(wx, r & 1) * 4 + ((xx >> 3) & 1) * 2 + (r >> 1);
  return (((bp + pag * 32 + bl) * 256) + byte) & 0x3FFFFF;
}
uint32_t Vram::Dir4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const {
  const uint32_t pag = (x >> 7) + (y >> 7) * ((bw + 1) >> 1), bl = kBloque4[(y >> 4) & 7][(x >> 5) & 3];
  const uint32_t xx = x & 31, yy = y & 15, col = yy >> 2, r = yy & 3;
  const uint32_t wx = (xx & 7) ^ ((((r >> 1) ^ (col & 1)) & 1) << 2);
  const uint32_t nib = col * 128 + Palabra(wx, r & 1) * 8 + ((xx >> 3) & 3) * 2 + (r >> 1);
  return (((bp + pag * 32 + bl) * 512) + nib) & 0x7FFFFF;
}

bool Vram::SubirBloques(const Subida& s) {
  const uint32_t bp = (s.bitbltbuf >> 32) & 0x3FFF, bw = (s.bitbltbuf >> 48) & 0x3F, psm = (s.bitbltbuf >> 56) & 0x3F;
  const uint32_t x0 = (s.trxpos >> 32) & 0x7FF, y0 = (s.trxpos >> 48) & 0x7FF;
  const uint32_t w = s.trxreg & 0xFFF, h = (s.trxreg >> 32) & 0xFFF;
  if (psm != 0 && psm != 1 && psm != 0x13 && psm != 0x14) return false;
  const uint32_t dx = psm == 0x14 ? 32 : psm == 0x13 ? 16 : 8, dy = psm >= 0x13 ? 16 : 8;
  const size_t pixeles = size_t(w) * h;
  const size_t n = psm == 0x14 ? (pixeles + 1) / 2 : pixeles * (psm == 0 ? 4 : psm == 1 ? 3 : 1);
  if (!n || n > 4 * 1024 * 1024 || s.datos.size() < n) return false;
  const bool parcial = psm == 1 || x0 % dx || y0 % dy || w % dx || h % dy;
  const size_t nb = size_t((x0 % dx + w + dx - 1) / dx) * ((y0 % dy + h + dy - 1) / dy);
  const size_t bytes = nb * (parcial ? 512 : 256);
  if (bytes > 8 * 1024 * 1024) return false;
  const auto clave = std::make_tuple(s.bitbltbuf & ~(0x3FFFull << 32), s.trxpos, s.trxreg,
                                    HuellaImagen(s.datos.data(), n));
  auto it = subidas_cache_.find(clave);
  if (it == subidas_cache_.end()) {
    // Limite por bytes ademas de entradas: una pelicula o atlas animado no crece sin limite.
    while (!subidas_cache_.empty() && (subidas_cache_.size() >= 128 || subidas_bytes_ + bytes > 32 * 1024 * 1024)) {
      auto vieja = std::min_element(subidas_cache_.begin(), subidas_cache_.end(),
          [](const auto& a, const auto& b) { return a.second.uso < b.second.uso; });
      subidas_bytes_ -= vieja->second.datos.size() + vieja->second.mascaras.size() * 8;
      subidas_cache_.erase(vieja);
    }
    SubidaCache c;
    c.datos.resize(nb * 256);
    if (parcial) c.mascaras.resize(nb * 32);
    for (uint32_t y = y0 / dy * dy; y < y0 + h; y += dy) for (uint32_t x = x0 / dx * dx; x < x0 + w; x += dx) {
      const uint32_t bloque = psm == 0x14 ? Dir4(0, bw, x, y) >> 9 :
                              psm == 0x13 ? Dir8(0, bw, x, y) >> 8 : Dir32(0, bw, x, y) >> 6;
      auto* dst = c.datos.data() + c.bloques.size() * 256;
      auto* mascara = parcial ? c.mascaras.data() + c.bloques.size() * 32 : nullptr;
      c.bloques.push_back(uint16_t(bloque));
      for (uint32_t yy = std::max(y, y0) - y; yy < std::min(y + dy, y0 + h) - y; ++yy)
        for (uint32_t xx = std::max(x, x0) - x; xx < std::min(x + dx, x0 + w) - x; ++xx) {
        const size_t src = size_t(y + yy - y0) * w + x + xx - x0;
        if (psm == 0 || psm == 1) {
          const uint32_t a = (Dir32(0, bw, x + xx, y + yy) & 63) * 4;
          std::memcpy(dst + a, s.datos.data() + src * (psm == 0 ? 4 : 3), psm == 0 ? 4 : 3);
          if (mascara) mascara[a / 8] |= uint64_t(psm == 0 ? 0xFFFFFFFFu : 0xFFFFFFu) << ((a & 7) * 8);
        } else if (psm == 0x13) {
          const uint32_t a = Dir8(0, bw, x + xx, y + yy) & 255;
          dst[a] = s.datos[src];
          if (mascara) mascara[a / 8] |= 0xFFull << ((a & 7) * 8);
        }
        else {
          const uint32_t a = Dir4(0, bw, x + xx, y + yy) & 511;
          const uint8_t nib = (s.datos[src / 2] >> ((src & 1) * 4)) & 15;
          dst[a >> 1] = uint8_t((dst[a >> 1] & ((a & 1) ? 15 : 240)) | (nib << ((a & 1) * 4)));
          if (mascara) mascara[a / 16] |= 0xFull << ((a & 15) * 4);
        }
      }
    }
    subidas_bytes_ += c.datos.size() + c.mascaras.size() * 8;
    it = subidas_cache_.emplace(clave, std::move(c)).first;
  }
  ++version;
  auto& c = it->second;
  c.uso = version;
  for (size_t i = 0; i < c.bloques.size(); ++i) {
    const uint32_t bloque = (bp + c.bloques[i]) & 0x3FFF;
    auto* dst = reinterpret_cast<uint8_t*>(m.data()) + bloque * 256;
    const auto* src = c.datos.data() + i * 256;
    if (c.mascaras.empty()) {
      if (std::memcmp(dst, src, 256) != 0) { std::memcpy(dst, src, 256); paginas[bloque] = version; }
    } else {
      bool cambio = false;
      for (size_t j = 0; j < 32; ++j) {
        uint64_t a, b;
        std::memcpy(&a, dst + j * 8, 8); std::memcpy(&b, src + j * 8, 8);
        const uint64_t nuevo = (a & ~c.mascaras[i * 32 + j]) | (b & c.mascaras[i * 32 + j]);
        if (a != nuevo) { std::memcpy(dst + j * 8, &nuevo, 8); cambio = true; }
      }
      if (cambio) paginas[bloque] = version;
    }
  }
  return true;
}

void Vram::Subir(const Subida& s, bool reutilizar) {
  if (reutilizar && SubirBloques(s)) return;
  const uint32_t dbp = uint32_t((s.bitbltbuf >> 32) & 0x3FFF), dbw = uint32_t((s.bitbltbuf >> 48) & 0x3F);
  const uint32_t psm = uint32_t((s.bitbltbuf >> 56) & 0x3F);
  const uint32_t x0 = uint32_t((s.trxpos >> 32) & 0x7FF), y0 = uint32_t((s.trxpos >> 48) & 0x7FF);
  const uint32_t w = uint32_t(s.trxreg & 0xFFF), hgt = uint32_t((s.trxreg >> 32) & 0xFFF);
  if (!w || !hgt) return;
  const uint8_t* d = s.datos.data();
  const size_t n = s.datos.size();
  ++version;
  auto* b8 = reinterpret_cast<uint8_t*>(m.data());
  auto palabra = [&](uint32_t a, uint32_t v) {
    if (m[a] != v) { m[a] = v; paginas[a >> 6] = version; }
  };
  auto byte = [&](uint32_t a, uint8_t v) {
    if (b8[a] != v) { b8[a] = v; paginas[a >> 8] = version; }
  };
  uint32_t x = x0, y = y0;
  for (uint32_t i = 0; y < y0 + hgt; ++i) {   // sin dos divisiones enteras por texel subido
    if (psm == 0x00) { if (size_t(i) * 4 + 4 > n) break; uint32_t v; std::memcpy(&v, d + size_t(i) * 4, 4); palabra(Dir32(dbp, dbw, x, y), v); }
    else if (psm == 0x01) { if (size_t(i) * 3 + 3 > n) break; const uint32_t a = Dir32(dbp, dbw, x, y);
                             palabra(a, (m[a] & 0xFF000000u) | d[i * 3] | (d[i * 3 + 1] << 8) | (d[i * 3 + 2] << 16)); }
    else if (psm == 0x13) { if (i >= n) break; byte(Dir8(dbp, dbw, x, y), d[i]); }
    else if (psm == 0x14) { if (i / 2 >= n) break; const uint32_t a = Dir4(dbp, dbw, x, y);
                            const uint8_t nib = (d[i / 2] >> ((i & 1) * 4)) & 15;
                             byte(a >> 1, uint8_t((b8[a >> 1] & ((a & 1) ? 0x0F : 0xF0)) | (nib << ((a & 1) * 4)))); }
    else { ++sin_formato; return; }
    if (++x == x0 + w) { x = x0; ++y; }
  }
}

std::vector<uint16_t> Vram::PaginasTextura(uint64_t tex0) const {
  const uint32_t bp = tex0 & 0x3FFF, bw = (tex0 >> 14) & 0x3F, psm = (tex0 >> 20) & 0x3F;
  const uint32_t tw = (tex0 >> 26) & 15, th = (tex0 >> 30) & 15;
  if (tw > 10 || th > 10 || (psm != 0 && psm != 1 && psm != 0x13 && psm != 0x14)) return {};
  bool usadas[16384]{};
  const uint32_t w = 1u << tw, h = 1u << th;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t a = psm == 0x13 ? Dir8(bp, bw, x, y) : psm == 0x14 ? Dir4(bp, bw, x, y) >> 1 : Dir32(bp, bw, x, y) * 4;
      usadas[a >> 8] = true;
    }
  if (psm == 0x13 || psm == 0x14) {
    const uint32_t cbp = (tex0 >> 37) & 0x3FFF;
    for (uint32_t i = 0; i < (psm == 0x13 ? 256u : 16u); ++i)
      usadas[Dir32(cbp, 1, i & (psm == 0x13 ? 15 : 7), i >> (psm == 0x13 ? 4 : 3)) >> 6] = true;
  }
  std::vector<uint16_t> r;
  for (uint16_t p = 0; p < 16384; ++p) if (usadas[p]) r.push_back(p);
  return r;
}

uint64_t Vram::Revision(const std::vector<uint16_t>& p) const {
  uint64_t r = 0;
  for (uint16_t i : p) if (paginas[i] > r) r = paginas[i];
  return r;
}

std::vector<std::pair<uint32_t, uint32_t>> Vram::LecturasTextura(uint64_t tex0) const {
  const uint32_t bp = tex0 & 0x3FFF, bw = (tex0 >> 14) & 0x3F, psm = (tex0 >> 20) & 0x3F;
  const uint32_t tw = (tex0 >> 26) & 15, th = (tex0 >> 30) & 15;
  if (tw > 10 || th > 10 || (psm != 0 && psm != 1 && psm != 0x13 && psm != 0x14)) return {};
  std::vector<uint32_t> mascara(m.size());
  const uint32_t w = 1u << tw, h = 1u << th;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      if (psm == 0 || psm == 1) mascara[Dir32(bp, bw, x, y)] = psm == 1 ? 0xFFFFFFu : 0xFFFFFFFFu;
      else if (psm == 0x13) {
        const uint32_t a = Dir8(bp, bw, x, y);
        mascara[a >> 2] |= 0xFFu << ((a & 3) * 8);
      } else {
        const uint32_t a = Dir4(bp, bw, x, y);
        mascara[a >> 3] |= 0xFu << ((a & 7) * 4);
      }
    }
  if (psm == 0x13 || psm == 0x14) {
    const uint32_t cbp = (tex0 >> 37) & 0x3FFF;
    for (uint32_t i = 0; i < (psm == 0x13 ? 256u : 16u); ++i)
      mascara[Dir32(cbp, 1, i & (psm == 0x13 ? 15 : 7), i >> (psm == 0x13 ? 4 : 3))] = 0xFFFFFFFFu;
  }
  std::vector<std::pair<uint32_t, uint32_t>> r;
  for (uint32_t a = 0; a < mascara.size(); ++a) if (mascara[a]) r.push_back({a, mascara[a]});
  return r;
}

uint64_t Vram::FirmaLecturas(const std::vector<std::pair<uint32_t, uint32_t>>& lecturas) const {
  uint64_t r = 0xCBF29CE484222325ull;
  for (const auto& [a, mascara] : lecturas) r = (r ^ (m[a] & mascara)) * 0x100000001B3ull;
  return r;
}

uint64_t HuellaImagen(const uint8_t* datos, size_t n) {
  // Dos cadenas independientes de palabras de 64 bits: evita la dependencia por cada byte RGBA.
  uint64_t a = 0xCBF29CE484222325ull, b = 0x9E3779B185EBCA87ull;
  size_t k = 0;
  for (; k + 16 <= n; k += 16) {
    uint64_t x, y;
    std::memcpy(&x, datos + k, 8); std::memcpy(&y, datos + k + 8, 8);
    a = (a ^ x) * 0x9E3779B185EBCA87ull;
    b = (b ^ y) * 0xC2B2AE3D27D4EB4Full;
  }
  for (; k < n; ++k) a = (a ^ datos[k]) * 0x100000001B3ull;
  a ^= b + uint64_t(n);
  a ^= a >> 33; a *= 0xFF51AFD7ED558CCDull;
  a ^= a >> 33; a *= 0xC4CEB9FE1A85EC53ull;
  return (a ^ (a >> 33)) | 1ull;
}

uint64_t Vram::FirmaTextura(uint64_t tex0, int x0, int y0, int rw, int rh) const {
  const uint32_t bp = tex0 & 0x3FFF, bw = (tex0 >> 14) & 0x3F, psm = (tex0 >> 20) & 0x3F;
  const uint32_t tw = (tex0 >> 26) & 15, th = (tex0 >> 30) & 15;
  if (tw > 10 || th > 10 || (psm != 0 && psm != 1 && psm != 0x13 && psm != 0x14)) return 0;
  const int dx = psm == 0x14 ? 32 : psm == 0x13 ? 16 : 8;
  const int dy = psm == 0x13 || psm == 0x14 ? 16 : 8;
  const int iw = 1 << tw, ih = 1 << th;
  if (!rw || !rh) { x0 = y0 = 0; rw = iw; rh = ih; }
  const int bx = int(std::floor(double(x0) / dx)) * dx, by = int(std::floor(double(y0) / dy)) * dy;
  uint64_t firma = 0xCBF29CE484222325ull;
  for (int yy = by; yy < y0 + rh; yy += dy)
    for (int xx = bx; xx < x0 + rw; xx += dx) {
      const uint32_t x = uint32_t(xx) & uint32_t(iw - 1), y = uint32_t(yy) & uint32_t(ih - 1);
      const uint32_t bloque = psm == 0x14 ? Dir4(bp, bw, x, y) >> 9 :
                              psm == 0x13 ? Dir8(bp, bw, x, y) >> 8 : Dir32(bp, bw, x, y) >> 6;
      if (!firmas_paginas_[bloque] || firmas_revision_[bloque] != paginas[bloque]) {
        firmas_paginas_[bloque] = HuellaImagen(reinterpret_cast<const uint8_t*>(m.data()) + bloque * 256, 256);
        firmas_revision_[bloque] = paginas[bloque];
      }
      firma = (firma ^ firmas_paginas_[bloque]) * 0x100000001B3ull;
    }
  if (psm == 0x13 || psm == 0x14) {
    const uint32_t cbp = (tex0 >> 37) & 0x3FFF, n = psm == 0x13 ? 256 : 16;
    for (uint32_t i = 0; i < n; ++i)
      firma = (firma ^ m[Dir32(cbp, 1, i & (psm == 0x13 ? 15 : 7), i >> (psm == 0x13 ? 4 : 3))]) * 0x100000001B3ull;
  }
  return firma;
}

bool VentanaTextura(int iw, int ih, float u0, float v0, float u1, float v1, int& x, int& y, int& w, int& h) {
  if (iw <= 0 || ih <= 0 || size_t(iw) * ih < 32768 ||
      !(u0 >= 0 && u1 <= 1 && v0 >= 0 && v1 <= 1 && u0 <= u1 && v0 <= v1)) return false;
  x = int(std::floor(u0 * iw)) - 1; y = int(std::floor(v0 * ih)) - 1;
  w = int(std::ceil(u1 * iw)) + 2 - x; h = int(std::ceil(v1 * ih)) + 2 - y;
  if (size_t(w) * h * 2 > size_t(iw) * ih) return false;
  return true;
}

bool RecortarTextura(const std::vector<uint8_t>& rgba, int iw, int ih, float u0, float v0, float u1, float v1,
                     int& x, int& y, int& w, int& h, std::vector<uint8_t>& salida) {
  if (rgba.size() != size_t(iw) * ih * 4 || !VentanaTextura(iw, ih, u0, v0, u1, v1, x, y, w, h)) return false;
  salida.resize(size_t(w) * h * 4);
  // Un texel de guarda alrededor del recorte conserva el filtro lineal, incluido REPEAT en los bordes.
  for (int yy = 0; yy < h; ++yy) {
    const int sy = (y + yy + ih) % ih;
    for (int xx = 0; xx < w;) {
      const int sx = (x + xx + iw) % iw, n = std::min(w - xx, iw - sx);
      std::memcpy(salida.data() + (size_t(yy) * w + xx) * 4,
                  rgba.data() + (size_t(sy) * iw + sx) * 4, size_t(n) * 4);
      xx += n;
    }
  }
  return true;
}

bool Vram::Textura(uint64_t tex0, uint64_t texa, int& w, int& h, std::vector<uint8_t>& rgba, bool limitar512,
                    int x0, int y0, int rw, int rh) const {
  const uint32_t tbp = uint32_t(tex0 & 0x3FFF), tbw = uint32_t((tex0 >> 14) & 0x3F), psm = uint32_t((tex0 >> 20) & 0x3F);
  const uint32_t tw = uint32_t((tex0 >> 26) & 15), th = uint32_t((tex0 >> 30) & 15);
  const uint32_t cbp = uint32_t((tex0 >> 37) & 0x3FFF), cpsm = uint32_t((tex0 >> 51) & 15), csm = uint32_t((tex0 >> 55) & 1);
  const bool tcc = (tex0 >> 34) & 1;
  if (tw > 10 || th > 10) return false;
  // IW usa TW=10 para el panel inferior de 800 px: truncarlo a 512 repetia su primera mitad.
  w = 1 << (limitar512 && tw > 9 ? 9 : tw); h = 1 << (limitar512 && th > 9 ? 9 : th);
  const int iw = w, ih = h;
  if (rw > 0 && rh > 0 && rw <= iw + 3 && rh <= ih + 3) { w = rw; h = rh; }
  else { x0 = y0 = 0; }
  if ((psm == 0x13 || psm == 0x14) && (cpsm != 0 || csm)) return false;   // ponytail: solo CLUT PSMCT32 CSM1
  if (psm != 0 && psm != 1 && psm != 0x13 && psm != 0x14) return false;
  rgba.assign(size_t(w) * h * 4, 0);
  const auto* b8 = reinterpret_cast<const uint8_t*>(m.data());
  auto alfa = [](uint32_t a) { return uint8_t(a >= 0x80 ? 255 : a * 2); };
  // La paleta no cambia durante esta conversion: resolverla una vez evita calcular su direccion por cada pixel.
  uint32_t paleta[256];
  if (psm == 0x13 || psm == 0x14) {
    const uint32_t n = psm == 0x13 ? 256u : 16u;
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t j = psm == 0x13 ? (i & 0xE7) | ((i & 8) << 1) | ((i & 16) >> 1) : i;
      const uint32_t c = m[Dir32(cbp, 1, j & (psm == 0x13 ? 15 : 7), j >> (psm == 0x13 ? 4 : 3))];
      paleta[i] = (c & 0xFFFFFFu) | (uint32_t(tcc ? alfa(c >> 24) : 255) << 24);
    }
  }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint32_t sx = uint32_t(x0 + x) & uint32_t(iw - 1), sy = uint32_t(y0 + y) & uint32_t(ih - 1);
      uint32_t c;
      if (psm == 0 || psm == 1) c = m[Dir32(tbp, tbw, sx, sy)];
      else {
        uint32_t i;
        if (psm == 0x13) {
          i = b8[Dir8(tbp, tbw, sx, sy)];
          c = paleta[i];
        } else {
          const uint32_t a = Dir4(tbp, tbw, sx, sy);
          i = (b8[a >> 1] >> ((a & 1) * 4)) & 15;
          c = paleta[i];
        }
        std::memcpy(&rgba[(size_t(y) * w + x) * 4], &c, 4);
        continue;
      }
      uint32_t a = c >> 24;
      if (psm == 1) a = ((texa >> 15) & 1) && !(c & 0xFFFFFF) ? 0 : uint32_t(texa & 0xFF);
      uint8_t* o = &rgba[(size_t(y) * w + x) * 4];
      o[0] = uint8_t(c); o[1] = uint8_t(c >> 8); o[2] = uint8_t(c >> 16); o[3] = tcc ? alfa(a) : 255;
    }
  return true;
}

Traductor& Global() { static Traductor t; return t; }
Dma& GlobalDma() { static Dma d; return d; }

bool Dma::Escribir(uint32_t a, uint32_t v, Traductor& t, void* ctx, Lee64 r) {
  const uint32_t base = a & 0xFFFFFF00u;
  const int c = base == 0x10009000u ? 1 : base == 0x1000A000u ? 2 : 0;
  if (!c) return false;
  const uint32_t k = (a & 0xFF) >> 4;
  if (k > 3 || (a & 0xF)) return true;                         // otros registros del canal: sin efecto aqui
  reg[c][k] = v;
  if (k == 0 && (v & 0x100)) {                                  // CHCR con STR: la DMA corre y acaba en el acto
    Envio(c, v, reg[c][1], reg[c][2], reg[c][3], t, ctx, r);
    reg[c][0] = v & ~0x100u;
  }
  return true;
}

void Dma::Envio(int canal, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr, Traductor& t, void* ctx, Lee64 r,
                LeeBloque bloque) {
  std::vector<uint8_t> d;
  size_t corte = size_t(-1);
  auto leer = [&](uint32_t a, uint32_t n) {
    if (canal == 1 && corte_direccion && corte_primitivas == size_t(-1) && corte == size_t(-1) &&
        corte_direccion >= a && uint64_t(corte_direccion) <= uint64_t(a) + n)
      corte = d.size() + (corte_direccion - a);
    if (bloque && !(n & 7)) {
      if (const auto* p = bloque(ctx, a, n)) { d.insert(d.end(), p, p + n); return; }
    }
    for (uint32_t k = 0; k < n; k += 8) { const uint64_t v = r(ctx, a + k); d.insert(d.end(), (const uint8_t*)&v, (const uint8_t*)&v + 8); }
  };
  ++envios;
  if (((chcr >> 2) & 3) == 0) {
    leer(madr, (qwc & 0xFFFF) * 16);
  } else {                                                      // cadena de origen (gs_captura.cadena)
    const bool tte = (chcr & 0x40) && canal == 1;
    std::vector<uint32_t> pila;
    uint32_t tg = tadr;
    int i = 0;
    for (; i < 100000; ++i) {
      if (canal == 1 && corte_direccion == tg && corte_primitivas == size_t(-1) && corte == size_t(-1)) corte = d.size();
      const uint64_t tag = r(ctx, tg);
      const uint32_t n = uint32_t(tag & 0xFFFF), id = uint32_t((tag >> 28) & 7);
      uint32_t addr = uint32_t((tag >> 32) & 0x7FFFFFFF);
      if (tag >> 63) addr = 0x70000000u | (addr & 0x3FFF);     // bit SPR
      if (tte) leer(tg + 8, 8);
      if (id == 0 || id == 3 || id == 4) { leer(addr, n * 16); tg += 16; if (id == 0) break; }
      else if (id == 1) { leer(tg + 16, n * 16); tg += 16 + n * 16; }
      else if (id == 2) { leer(tg + 16, n * 16); tg = addr; }
      else if (id == 5) { leer(tg + 16, n * 16); pila.push_back(tg + 16 + n * 16); tg = addr; }
      else if (id == 6) { leer(tg + 16, n * 16); if (pila.empty()) break; tg = pila.back(); pila.pop_back(); }
      else { leer(tg + 16, n * 16); break; }
    }
    if (i == 100000) ++errores;
  }
  if (canal == 2) { t.Gif(d.data(), d.size()); return; }
  const size_t nw = d.size() / 4;                               // VIF1 (gs_captura.vif): solo DIRECT llega al GS
  auto w = [&](size_t p) { uint32_t x; std::memcpy(&x, d.data() + p * 4, 4); return x; };
  size_t p = 0;
  auto marcar = [&] {
    if (corte != size_t(-1) && corte_primitivas == size_t(-1) && p * 4 >= corte) corte_primitivas = t.prims.size();
  };
  while (p < nw) {
    marcar();
    const uint32_t code = w(p++), cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
    if (cmd == 0 || (cmd >= 2 && cmd <= 7) || cmd == 0x10 || cmd == 0x11 || cmd == 0x13) continue;
    if (cmd == 1) { cl = imm & 0xFF; wl = imm >> 8; }
    else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {
      ++vu1;
      if (vif_registrar) vif_texto += "VM " + std::to_string(cmd == 0x17 ? -1 : int(imm)) + "\n";
    }
    else if (cmd == 0x20) p += 1;
    else if (cmd == 0x30 || cmd == 0x31) p += 4;
    else if (cmd == 0x4A) {
      const size_t k = size_t(num ? num : 256) * 2;
      if (vif_registrar) vif_texto += "VP " + std::to_string(imm) + " " + std::to_string(num ? num : 256) + " " +
                                      Md5Hex(d.data() + p * 4, std::min(k, nw - p) * 4) + "\n";
      p += k; ++vu1;
    }
    else if (cmd == 0x50 || cmd == 0x51) {
      const size_t k = size_t(imm ? imm : 65536) * 4, m = k < nw - p ? k : nw - p;
      t.Gif(d.data() + p * 4, m * 4);
      p += k;
    } else if (cmd >= 0x60) {
      const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, nn = num ? num : 256;
      const uint32_t elems = (wl <= cl || wl == 0) ? nn : cl * (nn / wl) + (nn % wl < cl ? nn % wl : cl);
      const uint32_t bits = (vn == 3 && vl == 3) ? 16 : (32 >> vl) * (vn + 1);
      const size_t k = (elems * bits + 31) / 32;
      if (vif_registrar) {
        char c[64];
        std::snprintf(c, sizeof c, "VU %u %u %u %u ", imm & 0x3FF, nn, cmd & 0xF, (imm >> 14) | ((cmd >> 4) & 1) << 2);
        vif_texto += c + Md5Hex(d.data() + p * 4, std::min(k, nw - p) * 4) + "\n";
      }
      p += k;
      ++vu1;
    } else { ++errores; return; }
  }
  marcar();
}

std::string Traductor::Texto() const {
  std::string s;
  char t[160];
  for (const auto& p : prims) {
    std::snprintf(t, sizeof t, "P %X %llX %llX %llX %llX %llX %llX %llX %llX %llX", p.prim,
                  (unsigned long long)p.tex0, (unsigned long long)p.tex1, (unsigned long long)p.clamp,
                  (unsigned long long)p.alpha, (unsigned long long)p.test, (unsigned long long)p.scissor,
                  (unsigned long long)p.xyoffset, (unsigned long long)p.frame, (unsigned long long)p.zbuf);
    s += t;
    std::snprintf(t, sizeof t, " %llX %llX %llX %llX %llX %llX %llX", (unsigned long long)p.fba,
                  (unsigned long long)p.pabe, (unsigned long long)p.dimx, (unsigned long long)p.dthe,
                  (unsigned long long)p.colclamp, (unsigned long long)p.fogcol, (unsigned long long)p.texa);
    s += t;
    for (int i = 0; i < p.nv; ++i) {
      const auto& v = p.v[i];
      std::snprintf(t, sizeof t, " | %X %X %X %X %X %X %llX %X", v.x, v.y, v.z, v.f, v.rgba, v.q,
                    (unsigned long long)v.st, v.uv);
      s += t;
    }
    s += '\n';
  }
  for (const auto& u : subidas) {
    uint64_t h = 0xCBF29CE484222325ull;                        // FNV-1a 64
    for (uint8_t b : u.datos) h = (h ^ b) * 0x100000001B3ull;
    std::snprintf(t, sizeof t, "I %llX %llX %llX %zX %llX\n", (unsigned long long)u.bitbltbuf,
                  (unsigned long long)u.trxpos, (unsigned long long)u.trxreg, u.datos.size(), (unsigned long long)h);
    s += t;
  }
  return s;
}

}  // namespace dbz3::iwtrad
