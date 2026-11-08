// dbz3 - Cuerpo de Infinite World pintado con ps2::Escena sobre un luchador de B3 oculto (31/10). El modelo, sus texturas y sus
// animaciones salen de la copia del jugador (DATA_CMN); se usa donde B3 no tiene ese personaje o ese traje: Cell de 0x7DD
// (PINE: AMO 184 en RAM), Krilin con armadura de 0x7D7, Ten verde de 0x7DE. El luchador de B3 sigue debajo, oculto: su logica,
// sus puntos de cuerpo y su codigo de animacion mandan.
#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <rex/logging.h>

#include "anim_iw.h"
#include "pasarela.h"
#include "ps2_modelo.h"

namespace dbz3::iwcuerpo {
struct Cuerpo {
  ps2::Modelo m, pose;
  std::vector<ps2::Tex> tx;
  struct Fuente { std::vector<uint8_t> bsk, propio; bool ok = false; };
  std::vector<Fuente> fuentes;
  std::vector<uint8_t> global;   // 204/0: almacen global
  std::vector<std::string> nombres;
  std::map<uint32_t, ps2::Anim> anims;
  uint32_t ultimo = 0xFFFFFFFF;
  bool sin_raiz = false;   // actores del guion de vuelo: la cadera sin su pista (da la vuelta al mundo); los coloca el consumidor
  explicit operator bool() const { return m.esq && m.malla; }
  // `amo` y amo + 1 (#AMT) del DATA_CMN; `banco`: el #AMB con #BSK en el hijo 0 y su #AMM en el hijo 1.
  bool Cargar(pasarela::AfsPs2& cmn, size_t amo, size_t banco) {
    m = ps2::CargarSuelto(cmn.Entrada(amo), cmn.Entrada(amo + 1), true, true, true);   // con el material de piel (cabezas sin textura)
    if (!m.esq) return false;
    // Las caras y la piel traen alfa 0 donde va el color de piel: el GS las pinta sin alfa de textura (TCC RGB).
    for (auto& im : m.tex)
      for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
    REXLOG_INFO("dbz3 iwcuerpo: amo {} rampas de piel 0x{:X}, {} texturas", amo, ps2::RampasPiel(cmn.Entrada(amo + 1)), m.tex.size());
    m.esq->centro = {0, 0, 0};   // pista absoluta, como el motor de IW
    // Nombres de hueso del #AMO: lista de 32 B que empieza en X???_BODY. El #AMM del banco trae mas huesos que cada traje
    // (armadura, cinturon): la animacion se reparte por nombre.
    const auto raw = cmn.Entrada(amo);
    for (size_t o = 0; o + 32 < raw.size(); o += 16)
      if (raw[o] == 'X' && std::memcmp(&raw[o + 4], "_BODY", 6) == 0) {
        for (size_t i = 0; i < m.esq->padre.size() && o + 32 * (i + 1) <= raw.size(); ++i)
          nombres.emplace_back(reinterpret_cast<const char*>(&raw[o + 32 * i]), strnlen(reinterpret_cast<const char*>(&raw[o + 32 * i]), 32));
        break;
      }
    // Bancos donde buscar un codigo, en orden: el del actor y los comunes (EU: #BSK comun 204/3 -> almacen global 204/0; 915 y 922 de los campos).
    auto leer = [&](size_t n) {
      const auto e = cmn.Entrada(n);
      Fuente f;
      const bool bsk_al_final = n == 204 || n == 915;
      f.bsk = pasarela::HijoAmb(e, bsk_al_final ? 3 : 0);
      f.propio = pasarela::HijoAmb(e, 1);
      f.ok = f.bsk.size() > 0x20 && f.propio.size() > 0x20;
      if (f.ok) fuentes.push_back(std::move(f));
    };
    leer(banco);
    for (const size_t n : {size_t(204), size_t(915), size_t(922)}) if (n != banco) leer(n);
    global = pasarela::HijoAmb(cmn.Entrada(204), 0);
    return !fuentes.empty();
  }
  void CrearTexturas() { tx = ps2::Texturas(m); }
  const ps2::Anim* Anim(uint32_t codigo) {
    if (const auto it = anims.find(codigo); it != anims.end()) return it->second ? &it->second : nullptr;
    ps2::Anim an;
    for (const Fuente& fu : fuentes) {
      const auto& bsk = fu.bsk;
      auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= bsk.size()) std::memcpy(&v, bsk.data() + o, 4); return v; };
      if (codigo >= u32(0x10)) continue;
      const uint32_t blk = u32(u32(0x14) + 4 * size_t(codigo));
      if (!blk) continue;
      const uint32_t cab = u32(blk);
      const auto& amm = (cab >> 16) == 3 ? fu.propio : global;
      auto am = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amm.size()) std::memcpy(&v, amm.data() + o, 4); return v; };
      const uint32_t nb = am(0x18), nom = am(0x1C);
      ps2::Anim todo = ps2::LeerAnim(amm, cab & 0xFFFF, nb);
      if (!todo) continue;
      an = todo;
      const size_t n = m.esq->padre.size();
      an.ang.assign(n, {});
      an.pos.assign(n, {});
      // Por nombre sin el prefijo del personaje (XGOK_WAIST / XVDL_WAIST): el almacen global lo comparten todos
      for (size_t i = 0; i < n && i < nombres.size(); ++i)
        for (uint32_t h = 0; h < nb && nom + 32 * size_t(h + 1) <= amm.size(); ++h) {
          const char* nh = reinterpret_cast<const char*>(&amm[nom + 32 * size_t(h)]);
          if (nombres[i].size() > 4 && std::strlen(nh) > 4 && nombres[i].compare(4, std::string::npos, nh + 4) == 0) {
            an.ang[i] = todo.ang[h];
            if (!sin_raiz || nombres[i].find("_WAIST") == std::string::npos) an.pos[i] = todo.pos[h];
            break;
          }
        }
      break;
    }
    REXLOG_INFO("dbz3 iwcuerpo: codigo 0x{:X}: {} cuadros, {} huesos del modelo, {} tri", codigo, an.cuadros, m.esq->padre.size(), m.malla.tris.size());
    anims[codigo] = std::move(an);
    return anims[codigo] ? &anims[codigo] : nullptr;
  }
  // Hilo de la interfaz: la pose del cuerpo A del luchador x, en su sitio. Sin esa animacion en el banco, la ultima pose.
  void Poner(ps2::Escena& e, const ps2::Camara& cam, uint8_t* base, uint32_t x, const ps2::Escena::Toon& toon, const ps2::V3* fija = nullptr) {
    anim::EstadoCuerpo st{};
    if (!*this || !anim::LeerEstadoCuerpo(base, x, 0, st) || !st.hay) return;
    const uint32_t cod = anim::CodigoOriginalIW(st.codigo & 0xFFF);
    if (const ps2::Anim* a = Anim(cod)) {
      ps2::Posar(*m.esq, *a, st.cuadro / 60.0f, pose.malla);
      ultimo = cod;
    }
    if (!pose.malla) return;
    auto f = [&](uint32_t o) { const uint8_t* b = base + x + o; const uint32_t u = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]; float v; std::memcpy(&v, &u, 4); return v; };
    const float giro = std::atan2(f(32), f(40));
    if (static int n = 0; std::getenv("DBZ3_IW_EVENTOS_LOG") && ++n % 120 == 1) {
      ps2::V3 c0{};
      const auto& t0 = pose.malla.tris[0];
      c0 = t0.p[0];
      ImVec2 o; float k = 0;
      const bool vis = cam.P(ps2::V3{f(48), f(52) + 5, f(56)}, o, k);
      REXLOG_INFO("dbz3 iwcuerpo: pos ({:.1f},{:.1f},{:.1f}) tri0 ({:.1f},{:.1f},{:.1f}) tris {} tex {} cam ok {} vis {} px ({:.0f},{:.0f}) k {:.1f}", f(48), f(52), f(56), c0.x, c0.y, c0.z, pose.malla.tris.size(), tx.size(), cam.ok, vis, o.x, o.y, k);
    }
    e.Tapable(false);
    e.Poner(cam, pose.malla, tx, fija ? *fija : ps2::V3{f(64), f(68), f(72)}, giro, 1.0f, toon);
    e.Tapable(true);
  }
};

// Los dos actores de un minijuego: oculta los de B3 y pinta los de IW. Los modelos y bancos salen de PINE (TABLA_REVISION_USUARIO.md).
struct Pareja {
  Cuerpo c[2];
  ps2::Escena escena;
  bool tex = false;
  explicit operator bool() const { return bool(c[0]) || bool(c[1]); }
  // Hilo de la interfaz, con el combate en marcha. x0/x1: los dos luchadores.
  void Pintar(ImGuiIO& io, uint8_t* base, uint32_t x0, uint32_t x1) {
    if (!*this) return;
    if (!tex) { tex = true; for (auto& k : c) if (k) k.CrearTexturas(); }
    ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    if (!cam.ok) return;
    static ps2::Escena::Toon toon;
    toon.borde = 0.12f;
    escena.Empezar();
    if (c[0]) c[0].Poner(escena, cam, base, x0, toon);
    if (c[1]) c[1].Poner(escena, cam, base, x1, toon);
    escena.Pintar(ImGui::GetBackgroundDrawList());
  }
};
}  // namespace dbz3::iwcuerpo
