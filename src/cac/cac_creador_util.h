// NCAC2: historial de aspecto del creador; no conserva ni restaura progreso RPG.
#ifndef DBZ3_CAC_CREADOR_UTIL_H_
#define DBZ3_CAC_CREADOR_UTIL_H_

#include <deque>
#include <utility>

#include "cac.h"

namespace dbz3::cac {
namespace detalle_creador {

// Una sola lista de campos para capturar, comparar y restaurar. El traje que
// se está editando vive en heroe::Traje(), no necesariamente en la ficha.
template <class A, class B, class F>
void Campos(A& a, B& b, F f) {
  f(a.nombre, b.nombre);
  f(a.logo, b.logo);
  f(a.scouter, b.scouter);
  f(a.pelo, b.pelo);
  f(a.pelo_fid_cac2, b.pelo_fid_cac2);
  f(a.cara, b.cara);
  f(a.pelo_c1, b.pelo_c1);
  f(a.pelo_c2, b.pelo_c2);
  f(a.ojos_c, b.ojos_c);
  f(a.piel_c, b.piel_c);
  f(a.b3_accesorio, b.b3_accesorio);
  f(a.ocultar, b.ocultar);
  f(a.ocultar_de, b.ocultar_de);
  f(a.texcol, b.texcol);
  f(a.material17, b.material17);
  f(a.pintura17, b.pintura17);
  f(a.foto_pose, b.foto_pose);
  f(a.foto_yaw, b.foto_yaw);
  for (int i = 0; i < kNumPartes; ++i) f(a.parte[i], b.parte[i]);
  for (int i = 0; i < 4; ++i) f(a.b3_piezas[i], b.b3_piezas[i]);
  for (int i = 0; i < kNumCapas; ++i) f(a.capas[i], b.capas[i]);
  for (int i = 0; i < kMaxRecetas; ++i) f(a.recetas[i], b.recetas[i]);
  for (int i = 0; i < kMaxFormasReceta; ++i) {
    f(a.forma_receta[i], b.forma_receta[i]);
    f(a.forma_estallido[i], b.forma_estallido[i]);
  }
}

template <class T>
bool Igual(const T& a, const T& b) { return a == b; }
inline bool Igual(const Hsv& a, const Hsv& b) { return a.h == b.h && a.s == b.s && a.v == b.v; }
inline bool Igual(const Capa& a, const Capa& b) { return a.fid == b.fid && a.parte == b.parte && a.quitar == b.quitar; }
inline bool Igual(const RecetaGuardada& a, const RecetaGuardada& b) { return a.nombre == b.nombre && a.ficha == b.ficha; }
}  // namespace detalle_creador

class AspectoCreador {
 public:
  AspectoCreador(const Heroe& h, int traje) : traje_(traje) { Copiar(campos_, h); }
  int Traje() const { return traje_; }
  void Restaurar(Heroe& h) const { Copiar(h, campos_); }
  bool Coincide(const Heroe& h, int traje) const {
    if (traje != traje_) return false;
    bool igual = true;
    detalle_creador::Campos(campos_, h, [&igual](const auto& a, const auto& b) {
      igual = igual && detalle_creador::Igual(a, b);
    });
    return igual;
  }

 private:
  static void Copiar(Heroe& dst, const Heroe& src) {
    detalle_creador::Campos(dst, src, [](auto& a, const auto& b) { a = b; });
  }
  int traje_;
  Heroe campos_;
};

class HistorialCreador {
 public:
  static constexpr int kMaxEstados = 32;
  HistorialCreador(const Heroe& h, int traje) : inicial_(h, traje) {}

  bool RegistrarSiCambio(AspectoCreador antes, const Heroe& h, int traje) {
    if (antes.Coincide(h, traje)) return false;
    Push(deshacer_, std::move(antes));
    rehacer_.clear();
    return true;
  }
  bool Deshacer(const Heroe& h, int traje, auto aplicar) { return Paso(deshacer_, rehacer_, h, traje, aplicar); }
  bool Rehacer(const Heroe& h, int traje, auto aplicar) { return Paso(rehacer_, deshacer_, h, traje, aplicar); }
  int PendientesDeshacer() const { return int(deshacer_.size()); }
  int PendientesRehacer() const { return int(rehacer_.size()); }
  bool HayCambiosSinGuardar(const Heroe& h, int traje) const { return !inicial_.Coincide(h, traje); }

 private:
  using Pila = std::deque<AspectoCreador>;
  static void Push(Pila& pila, AspectoCreador estado) {
    pila.push_back(std::move(estado));
    if (pila.size() > kMaxEstados) pila.pop_front();
  }
  static bool Paso(Pila& origen, Pila& destino, const Heroe& h, int traje, auto aplicar) {
    if (origen.empty()) return false;
    Push(destino, AspectoCreador(h, traje));
    aplicar(origen.back());
    origen.pop_back();
    return true;
  }

  AspectoCreador inicial_;
  Pila deshacer_, rehacer_;
};

}  // namespace dbz3::cac
#endif  // DBZ3_CAC_CREADOR_UTIL_H_
