// HUD Nave: nucleo visual sin escritura guest ni decisiones de combate/resultado.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dbz3::pasarela::nave::noche {
inline constexpr bool kCarasVerificadasEnPort = false;
inline constexpr bool kKaiokenVerificadoEnPort = true; // Goku0->1 por mando, RAM+foto065555.
inline constexpr bool kTransitoriosVerificadosEnPort = false;
inline constexpr bool kDosJugadoresVerificadoEnOriginal = false;
inline constexpr bool dibujo2p = false;

struct CamposB3 {
  static constexpr uint32_t inicio = 2096, paso = 8240;
  static constexpr uint32_t id = 7420, forma = 2980;
  static constexpr uint32_t vida = 6080, ki = 6100, ki_max = 6104, cuadros = 35144;
};
struct SeleccionCara { int personaje_b2 = -1, textura = -1; };
inline bool FamiliaDocumentada(int id) { return id == 0 || id == 3 || id == 4 || id == 7 || id == 27; }
// Original B2: 1EF890 escribe HUD+14; 1C4800 lo copia a sprite+18.
inline SeleccionCara CaraDeB3Documentada(int id, int forma) {
  if (forma < 0) return {};
  if (id == 0 && forma <= 4) return {0, forma};   // 0 normal, 1 KAI0KEN, 2 SSJ, 3 SSJ2, 4 SSJ3
  if (id == 3 && forma <= 2) return {2, forma};
  if (id == 4 && forma <= 3) return {28, forma};
  if (id == 7) {
    if (forma <= 2) return {3, forma};
    if (forma == 4) return {3, 3};               // Majin: NO confundir B3SSJ4=3 con B2Majin=3
  }
  if (id == 27) {
    if (forma == 3) return {15, 0};
    if (forma == 4) return {15, 1};              // final /100%; otras formas sin cara equivalente
  }
  return {};
}

struct Tex1 { bool lcm = false; unsigned mxl = 0, mmag = 0, mmin = 0; int k16 = 0; };
inline Tex1 DecodificarTex1(uint64_t q) {
  int k = int((q >> 32) & 0xFFFu); if (k & 0x800) k -= 4096;
  return {bool(q & 1), unsigned((q >> 2) & 7), unsigned((q >> 5) & 1), unsigned((q >> 6) & 7), k};
}
// Medido en 14 AD del HUD: LCM1/K=-4.5/MMAG1 => lineal. MIN0 no obliga nearest con LOD negativo.
inline constexpr uint64_t kTex1HudMedido = 0xFFFFFFB800000221ull;

struct MetaClip { uint32_t fin_movimiento = 0, fin_piezas = 0, flags = 0; };
inline uint32_t TickClipOriginal(float& tick, float delta, bool pausa, const MetaClip& m) {
  if (!pausa || (m.flags & 0x200)) tick += delta;
  const uint32_t a = tick < 0 ? 1u : tick >= float(int64_t(m.fin_movimiento)-1) ? ((m.flags & 2) ? 4u : 0u) : 2u;
  const uint32_t b = tick < 0 ? 8u : tick >= float(int64_t(m.fin_piezas)-1) ? ((m.flags & 4) ? 32u : 0u) : 16u;
  return a | b;
}
// 1A8200 envuelve la muestra de piezas si flag4; no reinicia el contador de tarea.
inline float MuestraPiezasOriginal(float tick, const MetaClip& m) {
  const float fin = float(m.fin_piezas) - 1;
  if ((m.flags & 4) && fin > 0 && tick > fin) {
    const float r = std::fmod(tick, fin); return r == 0 ? fin : r;
  }
  return tick;
}

struct EstadoVida {
  bool iniciado = false;
  int anterior = 0, frente = 0, estela = 0;
  uint32_t flags = 0;
  float alfa_cruce = 0, tiempo_ko = 0, tiempo_critico = 0;
};
inline void InicializarVida(EstadoVida& s, int hp) {
  s = {}; s.iniciado = true; s.anterior = s.frente = s.estela = std::max(0, hp);
  if (hp <= 0) s.flags |= 0x200;
  else if (hp < 51) s.flags |= 0x20;
}
struct EntradaVida {
  int unidades = 0; float delta_original = 1;
  bool pausa_original = false, nuevo_evento_dano = false, fin_recepcion = false;
};
// Fragmento determinista 1C6E10; nunca infiere fin_recepcion de ms/accion del actor.
inline void TickVidaOriginal(EstadoVida& s, const EntradaVida& e) {
  const int hp = std::max(0, e.unidades);
  if (!s.iniciado) InicializarVida(s, hp);
  if (e.nuevo_evento_dano) s.flags |= 0x1000000;
  if (e.fin_recepcion) s.flags |= 0x200000;
  if (hp < s.anterior) {
    s.frente = hp;
    if (!(s.flags & 0x40) && (s.flags & 0x1000000)) {
      s.estela = s.anterior; s.flags |= 0x40; s.flags &= ~0x800000u;
      if (s.flags & 0x80) s.flags &= ~0x40u;
    }
    if (hp < 1) { s.flags &= ~(1u | 0x20u | 0x10u); s.flags |= 0x200; s.tiempo_critico = 0; }
  } else if (!e.pausa_original) {
    if (s.frente < hp) {
      s.frente += std::max(1, (hp-s.frente)/10);
      if ((s.flags & 0x20) && hp > 50) { s.flags &= ~0x20u; s.tiempo_critico = 0; }
    } else s.frente = hp;
  }
  s.flags &= ~0x1000000u;
  if (!e.pausa_original) {
    if (s.flags & 0x200000) {
      s.flags &= ~0x200000u;
      if (s.flags & 0x40) { s.flags &= ~0x40u; s.flags |= 0x80; }
    }
    if (s.flags & 0x40) {
      if ((s.frente-1)/100 != (s.estela-1)/100) s.flags |= 0x800000;
      s.flags &= ~0x80u;
    }
    if (s.flags & 0x80) {
      s.estela -= (s.estela-s.frente)/100+1;
      if (s.estela <= s.frente) s.flags &= ~0x80u;
      if ((s.frente-1)/100 == (s.estela-1)/100) s.flags &= ~0x800000u;
    }
    s.alfa_cruce = std::clamp(s.alfa_cruce + ((s.flags & 0x800000) ? 1.0f : -1.0f)*e.delta_original*0.05f, 0.0f, 1.0f);
    if (!(s.flags & 0x200)) {
      if (!(s.flags & 0x20) && hp < 51) s.flags |= 0x20;
    } else if (hp > 0) {
      s.flags &= ~0x200u; s.tiempo_ko = 0;
      if (hp < 51) s.flags |= 0x20;
    }
  }
  s.anterior = hp;
}

struct OpcionesPrueba {
  bool caras = false, medio_texel = false, transitorios = false, cargar2p = false;
};
// Evento autoritativo externo/fixture. El wrapper runtime NO fabrica esta fuente.
struct DanoHud {
  bool valido = false; uint64_t secuencia = 0;
  bool inicio = false, fin_recepcion = false;
};
struct EstadoLado {
  EstadoVida vida;
  uint64_t secuencia = 0;
  bool fuente_dano = false, pendiente_inicio = false, pendiente_fin = false;
  int log_id = -1, log_forma = -1;
  uint32_t log_estado = 0;
};
struct ContextoVisual {
  bool iniciado = false, transitorios = false;
  uint64_t encuentro = 0;
  int planta = -1;
  float ultimo_cuadro = 0, credito = 0;
  EstadoLado lado[2];
};
}  // namespace dbz3::pasarela::nave::noche
