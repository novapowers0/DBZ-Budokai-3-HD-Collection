// R16: puente al gestor #ACE de B3. La UI nunca toca memoria del invitado.
#pragma once
#include <cstdint>
#include <vector>

namespace dbz3::survivors::fxreal {
struct Vertice { float x, y, z; };
struct Pedido {
  uint64_t id;
  float x, y;
  int banco = 7, ace = 9, bsp = 504; // selector de 820CF580, indice y atlas
  float alto = .9f, escala = .025f, giro = 0;
  Vertice ojo{0, 0, 1000}; // camara del modo, en coordenadas locales del emisor
  bool repetir = false;
  int b3 = -1, cst_codigo = -1; // ID invitado esperado y codigo CST (si aplica)
  float largo_rayo = 0; // cuerpo CST, en unidades locales, desde -Z hasta el ancla
};
struct Cuadro {
  uint64_t id;
  Vertice v[4];
  Vertice centro;
  float rgba[4][4], uv[4];
  int textura = -1, bsp = 504;
  int banco = -1, ace = -1; // selector realmente resuelto en el hilo invitado
  bool frontal = false, aditiva = false;
  bool cuerpo_rayo = false; // UV ya normalizadas; evidencia separada de la cabeza ACE
};
bool Activo();
void Publicar(uint64_t paso, const std::vector<Pedido>& pedidos);
std::vector<Cuadro> Leer();
}  // namespace dbz3::survivors::fxreal
