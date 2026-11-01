// sol-R2: RE opt-in. No mandos falsos, mascaras nuevas ni restauraciones de monton.
#pragma once
#ifdef _WIN32
namespace dbz3::net::rb {
struct Audit {
  static bool On() { static const bool v = std::getenv("DBZ3_RB_PURE") != nullptr; return v; }
  static inline uint32_t tick = 0, rel = 0, fight = 0;
  static inline thread_local int phase = 0;   // 0 forward; 1 replay verdadero; 2 segundo replay
  static inline thread_local uint32_t node = 0, callback = 0;
  static inline thread_local uint32_t probe_seq = 0;
  static inline thread_local std::vector<uint8_t> before;
  static inline std::ofstream tasks;
  static inline std::atomic<uint32_t> game_tid{0};

  static void Clock(PPCContext& ctx, uint8_t* base) {
    if (!On()) return;
    // Solo el hilo del tick usa el CSV y las copias. Los otros se estudian con Watch.
    if (GetCurrentThreadId() != game_tid.load() || rel > 450) return;
    const uint32_t lr = uint32_t(ctx.lr), m = Fight(base);
    if (!m) return;
    if (!tasks.is_open()) {
      tasks.open(OutDir() / ("tasks_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      tasks << "fight,tick,rel,phase,tid,node,callback,changed,first_off,node_words\n";
    }
    if (lr == 0x82092814) {
      node = ctx.r30.u32;
      probe_seq = 0;
      callback = REX_LOAD_U32(node + 28);
      before.assign(base + m, base + m + kMgrLen);
    } else if (lr == 0x82092858 && !before.empty()) {
      size_t n = 0, first = kMgrLen;
      for (size_t i = 0; i < kMgrLen; ++i) if (before[i] != base[m + i]) { ++n; first = std::min(first, i); }
      // Primera parte del nodo: datos de la tarea, callback y enlaces, no solo su direccion.
      char line[256];
      std::snprintf(line, sizeof line, "%u,%u,%u,%d,%lu,%08X,%08X,%zu,%zX,", fight, tick, rel, phase,
                    GetCurrentThreadId(), node, callback, n, first);
      tasks << line;
      for (uint32_t o = 0; o < 64; o += 4) {
        char word[12]; std::snprintf(word, sizeof word, "%08X ", REX_LOAD_U32(node + o)); tasks << word;
      }
      tasks << '\n';
      static const bool trace = std::getenv("DBZ3_RB_PURE_TRACE") != nullptr;
      if (trace && rel <= 12) {
        char name[128];
        std::snprintf(name, sizeof name, "task_%lu_%u_%u_%d_%08X.bin", GetCurrentProcessId(), fight, tick, phase, callback);
        std::ofstream(OutDir() / name, std::ios::binary).write(reinterpret_cast<const char*>(base + m), kMgrLen);
      }
      if (phase || rel % 30 == 0) tasks.flush();
      before.clear(); node = callback = 0;
    }
  }

  static void Probe(uint32_t fn, PPCContext& ctx, uint8_t* base, void (*orig)(PPCContext&, uint8_t*)) {
    static const bool trace = std::getenv("DBZ3_RB_PURE_TRACE") != nullptr;
    if (!On() || !trace || GetCurrentThreadId() != game_tid || rel > 12 || callback != 0x8210E700) { orig(ctx, base); return; }
    const uint32_t arg = ctx.r3.u32;
    const uint32_t seq = probe_seq++;
    orig(ctx, base);
    const uint32_t m = Fight(base);
    if (!m) return;
    char stem[160];
    std::snprintf(stem, sizeof stem, "probe_%lu_%u_%u_%d_%08X_%08X_%u", GetCurrentProcessId(), fight, tick, phase, fn, arg, seq);
    std::ofstream(OutDir() / (std::string(stem) + ".bin"), std::ios::binary).write(reinterpret_cast<const char*>(base + m), kMgrLen);
    static std::ofstream out;
    if (!out.is_open()) {
      out.open(OutDir() / ("probes_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      out << "fight,tick,phase,fn,arg,seq\n";
    }
    char line[100]; std::snprintf(line, sizeof line, "%u,%u,%d,%08X,%08X,%u\n", fight, tick, phase, fn, arg, seq);
    out << line; out.flush();
  }

  // Primera ESCRITURA por pagina/intervalo. No PAGE_GUARD (un lector se llevaria la guarda).
  // Solo paginas RW seleccionadas explicitamente; conserva la proteccion original. Sin alloc/log en VEH.
  struct Watch {
    struct Page { uint32_t a; DWORD prot; };
    struct Hit { uint32_t a, host_tid, guest_tid, lr, sp, cb; int phase; uint64_t pc; };
    static inline std::array<Page, 256> pages{};
    static inline std::array<Hit, 1024> hits{};
    static inline std::atomic<uint32_t> count{0};
    static inline size_t np = 0;
    static inline uint8_t* membase = nullptr;
    static inline std::atomic<bool> armed{false};
    static inline SRWLOCK lock = SRWLOCK_INIT;
    struct Locked {
      Locked() { AcquireSRWLockExclusive(&lock); }
      ~Locked() { ReleaseSRWLockExclusive(&lock); }
    };
    static LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
      if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
          e->ExceptionRecord->NumberParameters < 2 || e->ExceptionRecord->ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
      Locked hold;
      if (!membase) return EXCEPTION_CONTINUE_SEARCH;
      const uintptr_t addr = e->ExceptionRecord->ExceptionInformation[1], b = reinterpret_cast<uintptr_t>(membase);
      if (addr < b || addr - b >= 0xC0000000ull) return EXCEPTION_CONTINUE_SEARCH;
      const uint32_t a = uint32_t(addr - b);
      for (size_t j = 0; j < np; ++j) if ((a & ~4095u) == pages[j].a) {
        if (!armed.load()) {
          // Un fault ya pendiente puede entrar despues de Stop: si ya reabrimos
          // la pagina, se reintenta sin registrar ni alterar otra proteccion.
          MEMORY_BASIC_INFORMATION mbi{};
          return VirtualQuery(membase + pages[j].a, &mbi, sizeof mbi) == sizeof mbi && mbi.Protect == PAGE_READWRITE
                     ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
        }
        const uint32_t i = count.fetch_add(1);
        if (i < hits.size()) {
          auto* ts = rex::runtime::ThreadState::Get();
          auto* c = ts ? ts->context() : nullptr;
          hits[i] = {a, GetCurrentThreadId(), ts ? ts->thread_id() : 0, c ? uint32_t(c->lr) : 0,
                     c ? c->r1.u32 : 0, callback, phase, uint64_t(e->ContextRecord->Rip)};
        }
        DWORD old; VirtualProtect(membase + pages[j].a, 4096, pages[j].prot, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
      }
      return EXCEPTION_CONTINUE_SEARCH;
    }
    static void Stop() {
      if (!armed.load()) return;
      {
        Locked hold;
        for (size_t i = 0; i < np; ++i) { DWORD old; VirtualProtect(membase + pages[i].a, 4096, pages[i].prot, &old); }
        armed = false;
      }
      static std::ofstream out;
      if (!out.is_open()) {
        out.open(OutDir() / ("writers_" + std::to_string(GetCurrentProcessId()) + ".csv"));
        out << "fight,tick,rel,addr,host_tid,guest_tid,lr,sp,callback,phase,pc\n";
      }
      for (uint32_t i = 0; i < std::min<uint32_t>(count.load(), uint32_t(hits.size())); ++i) {
        const auto& h = hits[i]; char line[256];
        std::snprintf(line, sizeof line, "%u,%u,%u,%08X,%u,%u,%08X,%08X,%08X,%d,%llX\n", fight, tick, rel,
                      h.a, h.host_tid, h.guest_tid, h.lr, h.sp, h.cb, h.phase, static_cast<unsigned long long>(h.pc));
        out << line;
      }
      out.flush();
    }
    static void Start(uint8_t* base) {
      static const char* spec = std::getenv("DBZ3_RB_WATCH");
      if (!On() || !spec || rel > 450) return;
      static void* veh = AddVectoredExceptionHandler(1, Handler);
      if (!veh) return;
      Locked hold;
      membase = base; np = 0; count = 0;
      for (const char* p = spec; *p && np < pages.size();) {
        char* end = nullptr; uint32_t a = uint32_t(std::strtoul(p, &end, 16));
        if (*end != ':') break;
        const uint32_t len = uint32_t(std::strtoul(end + 1, &end, 10));
        for (uint64_t q = a & ~4095u; q < uint64_t(a) + len && q < kLimit && np < pages.size(); q += 4096) {
          MEMORY_BASIC_INFORMATION mbi{};
          if (VirtualQuery(base + q, &mbi, sizeof mbi) == sizeof mbi && mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE)
            pages[np++] = {uint32_t(q), mbi.Protect};
        }
        p = *end == ',' ? end + 1 : end;
      }
      armed = true;
      for (size_t i = 0; i < np; ++i) { DWORD old; VirtualProtect(base + pages[i].a, 4096, PAGE_READONLY, &old); }
    }
  };

  struct Blocks { std::vector<uint32_t> a; std::vector<uint64_t> h; };
  static Blocks Grab(uint8_t* base) {
    Blocks out;
    std::vector<uint8_t> buf(1 << 20);
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t* p = base; p < base + kLimit;) {
      if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi) break;
      auto* b = static_cast<uint8_t*>(mbi.BaseAddress); auto* e = b + mbi.RegionSize;
      const uint64_t ga = uint64_t(b - base);
      if (mbi.State == MEM_COMMIT && mbi.Protect && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && !(ga >= kMirror0 && ga < kMirror1)) {
        for (auto* q = b; q < e && q < base + kLimit;) {
          const size_t n = std::min<size_t>({buf.size(), size_t(e - q), size_t(base + kLimit - q)});
          SIZE_T got = 0;
          if (ReadProcessMemory(GetCurrentProcess(), q, buf.data(), n, &got) && got == n)
            for (size_t o = 0; o + 128 <= n; o += 128) { out.a.push_back(uint32_t(q - base) + uint32_t(o)); out.h.push_back(H(buf.data() + o, 128)); }
          q += n;
        }
      }
      p = e;
    }
    return out;
  }
  static void DiffBlocks(const Blocks& a, const Blocks& b, uint32_t m) {
    std::ofstream out(OutDir() / ("pure_blocks_" + std::to_string(GetCurrentProcessId()) + ".csv"), std::ios::app);
    size_t i = 0, j = 0;
    while (i < a.a.size() && j < b.a.size()) {
      if (a.a[i] < b.a[j]) { ++i; continue; } if (b.a[j] < a.a[i]) { ++j; continue; }
      if (a.h[i] != b.h[j]) {
        char line[96]; std::snprintf(line, sizeof line, "%u,%u,%08X,%d\n", fight, rel, a.a[i], a.a[i] + 128 > m && a.a[i] < m + kMgrLen);
        out << line;
      }
      ++i; ++j;
    }
  }
};
}  // namespace dbz3::net::rb
#endif
