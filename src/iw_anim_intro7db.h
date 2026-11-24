#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace dbz3::anim7db {
inline bool Habilitada() {
  static const bool on = [] {
    const char* v = std::getenv("DBZ3_IW_7DB_INTRO_FIEL");
    return v && std::strcmp(v, "1") == 0;
  }();
  return on;
}
inline bool Solicitada(int clave, bool presenta) { return clave == 0x7DB && presenta && Habilitada(); }
enum Tramo { Fuera, Paisaje, Conversacion, Corte, Relanza1, Relanza2, Juego };
inline Tramo Secuencia(int fase, int sub, int paso) {
  if (fase == 0 && paso >= 0 && paso <= 3) return paso == 3 ? Conversacion : Paisaje;
  if (fase == 1 && sub == 0 && paso >= 0 && paso <= 5)
    return paso <= 1 ? Corte : paso == 2 ? Relanza1 : Relanza2;
  return fase == 1 && sub == 1 ? Juego : Fuera;
}
inline int Estado(Tramo t, int rol) {
  if (t == Paisaje) return 0;
  if (t == Conversacion) return rol ? 0 : 1;
  return t == Juego ? (rol ? 2 : 3) : (rol ? 1 : 2);
}
struct Reloj {
  Tramo tramo = Fuera;
  int inicio = 0;
  bool Entrar(Tramo t, int campo) {
    if (t <= tramo) return false;   // no repetir snapshots ni reproducir los tramos omitidos por START
    tramo = t; inicio = campo; return true;
  }
  float Muestra(int campo, float paso, bool traducido, float original, float ultimo) const {
    if (tramo == Fuera || tramo == Juego) return -1.f;
    const float f = traducido ? original : tramo == Corte ? 0.f : (campo - inicio) * paso;
    return f >= 0.f ? std::min(f, ultimo) : -1.f;
  }
};
inline int Cara(int rol, int linea) {
  if (rol == 0 && (linea == 9 || linea == 10)) return linea == 9 ? 0xD6 : 0xD7;
  return rol == 1 && linea == 0 ? 0xDF : -1;
}
// Un solo escritor (panel). El hilo de animacion nunca combina un reloj viejo con otro alias/actor.
inline std::atomic<bool> escena{false};
inline std::atomic<uint64_t> cuerpo[2]{}, cara[2]{};
// Un contador por transicion (pulsar/soltar alternan). Dos lecturas libpad en
// un mismo campo devuelven el mismo nivel, y un START sostenido no da dos flancos.
struct MandoIntro {
  unsigned escritos = 0, leidos = 0;
  uint64_t ultimo = ~uint64_t(0);
  void Nivel(bool abajo) { if (bool(escritos & 1) != abajo) ++escritos; }
  uint16_t Leer(uint64_t n, uint16_t otros) {
    if (n != ultimo) { ultimo = n; if (leidos != escritos) ++leidos; }
    return uint16_t((otros & ~8u) | ((leidos & 1) << 3));
  }
};
inline std::mutex mando_mu;
inline MandoIntro mando;
inline void Limpiar() {
  { std::lock_guard lk(mando_mu); mando = {}; }
  escena = false;
  for (int i = 0; i < 2; ++i) { cuerpo[i] = 0; cara[i] = 0; }
}
inline void Publicar(int r, uint32_t x, uint32_t alias, float f) {
  cuerpo[r] = x && alias && alias < 4096 && f >= 0.f && f < 4096.f ?
      (uint64_t(x) << 32) | (uint64_t(alias) << 20) | uint32_t(f * 256.f) : 0;
}
inline float ParaCuerpo(uint32_t x, uint32_t alias) {
  if (!escena.load()) return -1.f;
  for (auto& a : cuerpo) {
    const uint64_t v = a.load();
    if (v && uint32_t(v >> 32) == x && ((v >> 20) & 4095) == alias) return float(v & 0xFFFFF) / 256.f;
  }
  return -1.f;
}
inline void PedirCara(int r, uint32_t x, int linea) {
  const int id = Cara(r, linea);
  if (id >= 0 && x) cara[r] = (uint64_t(x) << 32) | uint32_t(id);
}
}  // namespace dbz3::anim7db
