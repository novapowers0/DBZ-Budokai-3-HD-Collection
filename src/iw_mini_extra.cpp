// dbz3 - Traducciones sueltas de IW para los minijuegos (DS107, 2026-10-27). Fuente: decomp EU
// E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c. Solo logica sin globales del guest; lo que depende de
// DMA/VU/AMA/MMI2 o del ciclo de B3 esta en MAPAS_CODIGO/SUSTITUIDAS_IW_MINIJUEGOS.txt.
#include "iw_mini_extra.h"

#include <cmath>

namespace dbz3::iwmini::extra {
namespace {

// Decodificado del paquete PS2 (EU 0x3394F0): byte con signo, zona muerta 64 y escala /64.
float Eje(int v) { return v > 64 ? float(v - 64) / 64.0f : v < -64 ? float(v + 64) / 64.0f : 0.0f; }

}  // namespace

// Y = palanca derecha (+0xD9). X = la de mayor magnitud entre +0xD8 y +0xDA (decomp: `if (cVar2*cVar2 <
// cVar1*cVar1)` usa cVar1; si no, cVar2). La inversion por bRam005c1534&1 no se aplica: el port usa el signo
// del mando de B3 (misma convencion que EjeIW en iw_mini_vuelo.cpp).
// EU 0x3394F0 Pad_GetStickAxes
void PadGetStickAxes(int izq_x, int der_x, int der_y, float& x, float& y) {
  const float ix = Eje(izq_x), dx = Eje(der_x);
  y = Eje(der_y);
  x = std::fabs(dx) > std::fabs(ix) ? dx : ix;
}

// Solape de dos rectangulos [x0,y0,x1,y1] en X (0), Y (1) o los dos (-1). Cualquier otro eje devuelve 1, como el
// original. X: `a[2] < b[0]` o `b[2] < a[0]`; Y: `a[3] < b[1]` o `b[3] < a[1]`.
// EU 0x2F3770 Math_RectsOverlap
bool MathRectsOverlap(const float* a, const float* b, int axis) {
  if (axis == 1) {
    if (a[3] < b[1]) return false;
    if (b[3] < a[1]) return false;
  } else if (axis == 0) {
    if (a[2] < b[0]) return false;
    if (b[2] < a[0]) return false;
  } else {
    if (axis != -1) return true;
    if (a[2] < b[0]) return false;
    if (b[2] < a[0]) return false;
    if (a[3] < b[1]) return false;
    if (b[3] < a[1]) return false;
  }
  return true;
}

}  // namespace dbz3::iwmini::extra
