// dbz3 - Ayudantes de los minijuegos de Infinite World compartidos por los frentes R12/R14 (DS107, 2026-10-27).
// Traducciones literales de funciones del ELF EU (E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c) que no dependen
// de globales del guest. Lo que depende de DMA/VU/AMA/MMI2 o del ciclo de B3 esta en
// MAPAS_CODIGO/SUSTITUIDAS_IW_MINIJUEGOS.txt con el archivo:funcion que lo cubre.
#pragma once

namespace dbz3::iwmini::extra {

// EU 0x3394F0 Pad_GetStickAxes: palancas del mando con zona muerta 64 y escala /64. Y = la derecha; X = la de
// mayor magnitud de las dos. `izq_x`/`der_x`/`der_y` son los bytes con signo (-127..127) de las palancas.
void PadGetStickAxes(int izq_x, int der_x, int der_y, float& x, float& y);

// EU 0x2F3770 Math_RectsOverlap: solape de dos rectangulos [x0,y0,x1,y1] en X (0), Y (1) o los dos (-1).
bool MathRectsOverlap(const float* a, const float* b, int axis);

}  // namespace dbz3::iwmini::extra
