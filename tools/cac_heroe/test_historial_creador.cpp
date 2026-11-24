#include "cac/cac_creador_util.h"

#include <cassert>
#include <iostream>

namespace C = dbz3::cac;

int main() {
  C::Heroe h;
  h.id = "h123456";
  h.traje = 0;  // el traje persistido no se escribe durante la edición
  int traje = 1;
  h.capas[0] = {416, 2, 8};
  h.texcol = "1,2,120,40,10";
  h.foto_yaw = 22;
  C::HistorialCreador hist(h, traje);
  auto aplicar = [&](const C::AspectoCreador& estado) {
    traje = estado.Traje();
    h.capas[0] = {};  // simula la limpieza previa de PonerTraje
    h.texcol.clear();
    estado.Restaurar(h);
  };

  C::AspectoCreador antes(h, traje);
  h.xp = 42;
  h.saga = 7;
  h.desbloqueos = {"r_h25", "ssj1"};
  assert(!hist.RegistrarSiCambio(antes, h, traje));
  assert(!hist.HayCambiosSinGuardar(h, traje));
  assert(!hist.Deshacer(h, traje, aplicar));

  // Una acción de reset cambia traje, capas y textura, pero no el RPG.
  traje = 0;
  h.capas[0] = {};
  h.texcol.clear();
  assert(hist.RegistrarSiCambio(antes, h, traje));
  assert(hist.HayCambiosSinGuardar(h, traje));
  h.xp = 43;
  assert(hist.Deshacer(h, traje, aplicar));
  assert(traje == 1 && h.traje == 0 && h.capas[0].fid == 416 && h.capas[0].quitar == 8 && h.texcol == "1,2,120,40,10");
  assert(h.id == "h123456" && h.xp == 43 && h.saga == 7 && h.desbloqueos.size() == 2);
  assert(!hist.HayCambiosSinGuardar(h, traje));
  assert(!hist.RegistrarSiCambio(C::AspectoCreador(h, traje), h, traje));
  assert(hist.Rehacer(h, traje, aplicar));
  assert(traje == 0 && h.capas[0].fid == -1 && h.texcol.empty());
  assert(hist.Deshacer(h, traje, aplicar));
  antes = C::AspectoCreador(h, traje);
  h.nombre = "Segunda rama";
  h.recetas[0] = {"Prueba", "cuerpo=0;traje=1"};
  h.forma_receta[1] = 0;
  assert(hist.RegistrarSiCambio(antes, h, traje));
  assert(hist.PendientesRehacer() == 0 && !hist.Rehacer(h, traje, aplicar));
  assert(hist.Deshacer(h, traje, aplicar));
  assert(h.nombre == "Heroe" && h.recetas[0].ficha.empty() && h.forma_receta[1] == -1);

  for (int n = 1; n <= 40; ++n) {
    antes = C::AspectoCreador(h, traje);
    h.parte[0].h = n * 5;
    assert(hist.RegistrarSiCambio(antes, h, traje));
  }
  assert(hist.PendientesDeshacer() == 32);
  for (int n = 39; n >= 8; --n) {
    assert(hist.Deshacer(h, traje, aplicar));
    assert(h.parte[0].h == n * 5);
  }
  assert(!hist.Deshacer(h, traje, aplicar) && hist.PendientesRehacer() == 32);
  for (int n = 9; n <= 40; ++n) {
    assert(hist.Rehacer(h, traje, aplicar));
    assert(h.parte[0].h == n * 5);
  }
  assert(!hist.Rehacer(h, traje, aplicar));
  std::cout << "PASS historial NCAC2: 32 deshacer/rehacer, deduplicacion, rama nueva, reset y progreso intacto\n";
}
