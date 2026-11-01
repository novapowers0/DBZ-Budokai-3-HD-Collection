// Opus R5 (rollback): enganche de las escrituras del codigo recompilado (solo la imagen US: lo incluye generated/dbz3_pch.h al final).
// Apagado (g_on == 0) cuesta una comparacion por escritura. Encendido, la via lenta (rb_wlog.h) apunta el valor ANTERIOR de cada palabra
// que escribe la logica del hilo del juego, para deshacer ticks byte a byte sin copiar paginas enteras (las paginas mixtas con audio/GPU
// bloquearon R1-R4). Cada funcion generada sabe su direccion en tiempo de compilacion (rbw_fn, de __func__): la via lenta excluye las
// librerias (D3D, sonido) y sus colas compartidas con otros hilos.
#pragma once
#include <cstdint>
#include <cstring>

namespace rbw {
inline volatile uint32_t g_on = 0;                                       // != 0 mientras el hilo del juego esta en un tramo registrado
inline void (*g_fn)(uint32_t fn, uint32_t ea, uint32_t n, uint32_t sp, uint32_t lr) = nullptr;   // via lenta (rb_wlog.h)
inline volatile uint32_t g_cas_on = 0;                                   // marca las palabras atomicas (sesion con rollback)
inline void (*g_cas)(const void* host, uint32_t n) = nullptr;
inline bool (*g_enter)(uint32_t fn, PPCContext& ctx) = nullptr;         // entrada de funcion mientras se registra; true = no ejecutarla (rb_wlog.h)
inline uint8_t* g_mem = nullptr;                                        // base de la memoria del invitado (la pone rb_wlog.h)
constexpr uint32_t Hex(const char* s) {   // "__imp__sub_8222D790" / "sub_8222D790" -> 0x8222D790; otro nombre -> 0
  int i = (s[0] == '_' && s[1] == '_' && s[2] == 'i' && s[3] == 'm' && s[4] == 'p' && s[5] == '_' && s[6] == '_') ? 7 : 0;
  if (!(s[i] == 's' && s[i + 1] == 'u' && s[i + 2] == 'b' && s[i + 3] == '_')) return 0;
  uint32_t v = 0;
  for (int k = i + 4; k < i + 12; ++k) {
    const char c = s[k];
    if (c >= '0' && c <= '9') v = v * 16 + uint32_t(c - '0');
    else if (c >= 'A' && c <= 'F') v = v * 16 + uint32_t(c - 'A' + 10);
    else return 0;
  }
  return v;
}
// Ejecuta la funcion registrada en esa direccion (el gancho si lo hay), como REX_CALL_INDIRECT_FUNC.
inline void Invoke(uint32_t fn, PPCContext& c) { REX_LOOKUP_FUNC(g_mem, fn)(c, g_mem); }
template <class T, class U, class V>
inline bool Cas(T* p, U o, V n) {
  if (g_cas_on && g_cas) g_cas(p, sizeof(T));
  return __sync_bool_compare_and_swap(p, T(o), T(n));
}
}  // namespace rbw
constexpr uint32_t rbw_fn = 0;   // fuera de una funcion generada (ganchos de src/): cuenta como logica

// r1/lr del invitado al escribir: solo el codigo generado los tiene (ctx); en los ganchos de src/ valen "desconocido".
#ifdef DBZ3_RBW_TU
#define RBW_SP ctx.r1.u32
#define RBW_LR uint32_t(ctx.lr)
#else
#define RBW_SP 0xFFFFFFFFu
#define RBW_LR 0u
#endif
#define RBW_LOG(x, n) (__builtin_expect(rbw::g_on, 0) ? rbw::g_fn(rbw_fn, (u32)(x), (n), RBW_SP, RBW_LR) : (void)0)
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_STORE_U8(x, y)  (RBW_LOG(x, 1), *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (RBW_LOG(x, 2), *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (RBW_LOG(x, 4), *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (RBW_LOG(x, 8), *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
#undef REX_FUNC_PROLOGUE
#define REX_FUNC_PROLOGUE()                            \
  [[maybe_unused]] constexpr uint32_t rbw_fn = rbw::Hex(__func__); \
  if (__builtin_expect(rbw::g_on, 0) && rbw::g_enter(rbw_fn, ctx)) return; \
  __builtin_assume(((size_t)base & 0x1F) == 0)

// Solo en las unidades del codigo generado US (CMakeLists: DBZ3_RBW_TU): escrituras vectoriales, dcbz y reservas atomicas.
#ifdef DBZ3_RBW_TU
namespace rbw {
inline void Host(uint32_t fn, const void* p, uint32_t n, uint32_t sp, uint32_t lr) {
  const uint64_t d = uint64_t(static_cast<const uint8_t*>(p) - g_mem);
  if (g_mem && d < 0x100000000ull) g_fn(fn, uint32_t(d), n, sp, lr);   // los registros vectoriales (ctx) no son memoria del invitado
}
}  // namespace rbw
#define simde_mm_store_si128(p, v) (__builtin_expect(rbw::g_on, 0) ? rbw::Host(rbw_fn, (p), 16, RBW_SP, RBW_LR) : (void)0, (simde_mm_store_si128)((p), (v)))
#define simde_mm_store_ps(p, v) (__builtin_expect(rbw::g_on, 0) ? rbw::Host(rbw_fn, (p), 16, RBW_SP, RBW_LR) : (void)0, (simde_mm_store_ps)((p), (v)))
#define memset(p, c, n) (__builtin_expect(rbw::g_on, 0) ? rbw::Host(rbw_fn, (p), uint32_t(n), RBW_SP, RBW_LR) : (void)0, (memset)((p), (c), (n)))
#define __sync_bool_compare_and_swap(p, o, n) rbw::Cas((p), (o), (n))
#endif
