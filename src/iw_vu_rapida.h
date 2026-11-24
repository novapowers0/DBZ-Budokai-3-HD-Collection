// R18: cuatro carriles VU con las MISMAS operaciones separadas y truncado hacia cero.
// Exp 255 del R5900 no significa Inf/NaN: esos operandos siguen por la referencia escalar.
#pragma once
#include <cstdint>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif
namespace dbz3::iwmips {
enum class OpVu { Suma, Resta, Producto, Acumula, Desacumula };
#if defined(_MSC_VER)
__forceinline
#else
inline __attribute__((always_inline))
#endif
bool VuRapida(OpVu op, const uint32_t* a, const uint32_t* b, const uint32_t* c, uint32_t* salida) {
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif
#if defined(__x86_64__) || defined(_M_X64)
  const __m128i aa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
  const __m128i bb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
  const __m128i exp = _mm_set1_epi32(0x7F800000);
  auto especial = [&](const __m128i& v) {
    const __m128i e255 = _mm_cmpeq_epi32(_mm_and_si128(v, exp), exp);
    const __m128i maximo = _mm_cmpeq_epi32(_mm_and_si128(v, _mm_set1_epi32(0x7FFFFFFF)), _mm_set1_epi32(0x7F7FFFFF));
    return _mm_movemask_epi8(_mm_or_si128(e255, maximo)) != 0;
  };
  if (especial(aa) || especial(bb)) return false;
  const bool acum = op == OpVu::Acumula || op == OpVu::Desacumula;
  const __m128i cc = acum ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(c)) : _mm_setzero_si128();
  if (acum && especial(cc)) return false;
  const unsigned csr = _mm_getcsr();
  // RZ + FTZ + DAZ: producto y suma se redondean por separado. Sin FMA ni aproximacion.
  _mm_setcsr(csr | 0xE040);
  const __m128 x = _mm_castsi128_ps(aa), y = _mm_castsi128_ps(bb);
  __m128 r;
  if (op == OpVu::Suma) r = _mm_add_ps(x, y);
  else if (op == OpVu::Resta) r = _mm_sub_ps(x, y);
  else {
    r = _mm_mul_ps(x, y);
    // La referencia satura antes de corregir el error de la suma en el borde MAXF.
    // Mantener esa semantica incluso cuando el producto acaba de alcanzar ese borde.
    if (acum && especial(_mm_castps_si128(r))) { _mm_setcsr(csr); return false; }
    if (op == OpVu::Acumula) r = _mm_add_ps(_mm_castsi128_ps(cc), r);
    else if (op == OpVu::Desacumula) r = _mm_sub_ps(_mm_castsi128_ps(cc), r);
  }
  _mm_storeu_si128(reinterpret_cast<__m128i*>(salida), _mm_castps_si128(r));
  _mm_setcsr(csr);
  return true;
#else
  return false;
#endif
}
}  // namespace dbz3::iwmips
