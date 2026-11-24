// F10 R6: captura anterior a la salida del guest. No cambia su retorno ni evita la trampa.
#pragma once

#ifdef _WIN32
namespace dbz3::net::rb {
inline void ExitProbe(const char* where, PPCContext& c, uint8_t* base) {
  if (!REXCVAR_GET(dbz3_net_cierre_sonda_f10) || dbz3::g_guest_eu) return;
  auto& s = dbz3::net::Session::Get();
  if (!s.active() || !s.rb_on()) return;
  REXLOG_ERROR("dbz3 rb: F10 pre-cierre {} tick {} resim {} lr {:08X} sp {:08X} r3 {:08X} r4 {:08X}",
               where, s.tick(), s.resim_tick(), uint32_t(c.lr), c.r1.u32, c.r3.u32, c.r4.u32);
  REXLOG_ERROR("dbz3 rb: F10 registros r5 {:08X} r6 {:08X} r7 {:08X} r8 {:08X} r9 {:08X} r10 {:08X} r11 {:08X} r28 {:08X} r29 {:08X} r30 {:08X} r31 {:08X}",
               c.r5.u32, c.r6.u32, c.r7.u32, c.r8.u32, c.r9.u32, c.r10.u32, c.r11.u32,
               c.r28.u32, c.r29.u32, c.r30.u32, c.r31.u32);
  // Leer sin provocar otra excepcion ni pasar por los stores instrumentados.
  const auto word = [base](uint32_t a, uint32_t& v) {
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), base + a, &v, sizeof(v), &n) &&
           n == sizeof(v) ? (v = _byteswap_ulong(v), true) : false;
  };
  uint32_t sp = c.r1.u32;
  for (unsigned i = 0; i < 32; ++i) {
    uint32_t prev = 0, lr = 0;
    if ((sp & 15) || !word(sp, prev) || prev <= sp || prev - sp > 0x100000 ||
        (prev & 15) || !word(prev - 8, lr)) break;
    REXLOG_ERROR("dbz3 rb: F10 pila guest {} sp {:08X} anterior {:08X} lr {:08X}", i, sp, prev, lr);
    sp = prev;
  }
  void* frames[48] = {};
  const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = {};
    const bool mapped = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                         reinterpret_cast<LPCSTR>(frames[i]), &module) != FALSE;
    if (mapped) GetModuleFileNameA(module, name, MAX_PATH);
    REXLOG_ERROR("dbz3 rb: F10 pila nativa {} pc {:p} modulo {} offset {:X}", i, frames[i],
                 mapped ? name : "desconocido", mapped ? uintptr_t(frames[i]) - uintptr_t(module) : 0);
  }
  rex::FlushLogging();
}
}  // namespace dbz3::net::rb
#endif
