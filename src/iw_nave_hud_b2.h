// Marcador B2 de staging: incluir fuera de namespaces en nave y misiones.
// No contiene arte. Implementacion en iw_nave_hud_b2.inl, una sola vez en nave.
#pragma once
#include <imgui.h>
#include "iw_nave_hud_b2_noche.h"
#include "iw_nave_hud_b2_2p.h"
#include "modo_original.h"
#include <fstream>
#include <sstream>

namespace dbz3::pasarela::nave {
// Base I..IV cotejada por cifras/AMA (01/11, focales solGP2; NAVE_PLANTAS.md).
// Normal sin flags en el siguiente build; logs solo con el gate explicito de prueba.
// Formas/2P sin correspondencia y recursos ausentes conservan el HUD de B3.
inline constexpr bool kMarcadorB2Probado = true;

struct MuestraMarcadorB2 {
  int planta = 0;                 // 0..3 = I..IV, NO modo original 6..9
  int personaje_b2[2] = {0, 0};   // IDs originales, -1 = sin equivalente
  int cara_b2[2] = {0, 0};        // indice de textura del AMA de cara; NO forma de B3
  float vida[2] = {3000, 1000};   // unidades del combate (1000 por barra)
  float ki[2] = {3000, 3000}, ki_max[2] = {7000, 7000};
  float cuadros = 0;             // tiempo ACTIVO a 60 Hz, nunca reloj host
  int limite_cuadros = 5940;      // limite real pedido al combate
  int vencidos = 0, golpes = 0, ondas = 0;
  // Extensiones nuevas: false normal; solo dentro del arnes MISIONES=5 con flag/fixture explicito.
  int personaje_b3[2] = {-1, -1}, forma_b3[2] = {0, 0};
  bool dos_jugadores = false, pausa = false, fase_resultado = false;
  uint64_t encuentro = 0;              // cambia entre encuentros; no decide el flujo
  float paso_hud_original = ::dbz3::original::Delta(::dbz3::original::Fuente::B2Pal);   // PAL medido (B2 NTSC bloqueado). cuadros esta en ticks60Hz: no multiplicarlo otra vez.
  noche::OpcionesPrueba prueba;
  noche::DanoHud dano[2];              // valido=false si no hay productor exacto
  noche::ContextoVisual* contexto = nullptr;  // fixture independiente; nullptr usa solo cache visual del consumidor
  const original2p::Muestra* muestra_2p = nullptr; // solo archivo-fixture, nunca productor inferido del combate
};

struct InformacionDosJugadoresB2 {
  int modo = -1, entrada = -1, nodo_ama = 0, texturas = 0;
  uint32_t dibujo_cuerpos = 0, dibujo_modo = 0, actualizar_cuerpos = 0;
  bool listo = false, sin_paquete = false;
};
// Inspeccion/carga de recursos reales, NO dibuja 2P ni oculta B3. Solo flag RECURSOS_2P en el arnes.
bool LeerRecursosDosJugadoresB2(int planta, InformacionDosJugadoresB2& info);

// Solo dibujo, sin tocar RAM ni ocultar el marcador: sirve tambien para staging.
// Todos estos puntos se llaman en el hilo UI. La primera lectura de recursos
// reutiliza HijoAmb/LeerAma2/TexturaAmt de la nave y la copia del jugador.
bool PintarMarcadorB2(ImDrawList* dl, const MuestraMarcadorB2& m);

// Llamar TODOS los cuadros desde Panel::OnDraw, bajo g_mu de misiones.
// activo=false libera solo la ocultacion adquirida por este adaptador.
// Lee el combate B3 con GA y llama PintarMarcadorB2; dibujo2P queda fuera (modos originales10..13).
void MarcadorCombateB2(ImGuiIO& io, bool activo, int golpes);
void SoltarMarcadorB2();         // tambien al cerrar el panel/salir del modo
}  // namespace dbz3::pasarela::nave
