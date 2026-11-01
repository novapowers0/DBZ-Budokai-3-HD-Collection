// sol-R3: registro local de paginas, experimental y apagado por defecto.
// No viaja por red. VEH sin asignaciones ni E/S; el anillo se reserva antes de armar.
#pragma once
#ifdef _WIN32
namespace dbz3::net::rb {
class Undo {
 public:
  static bool On() { static const bool on = [] { const char* s = std::getenv("DBZ3_RB_UNDO"), *snap = std::getenv("DBZ3_RB_SNAP"); return (s && std::strcmp(s, "1") == 0) || (snap && std::strcmp(snap, "1") == 0); }(); return on; }
  static bool Persistent() { static const bool v = std::getenv("DBZ3_RB_UNDO_PERSIST") != nullptr; return v; }
  static constexpr size_t kViews = 0x120000000ull / 4096, kCapacity = 8192, kWindow = 9;
  struct Entry { uint32_t page, writers, lr, cb, other_lr, other_tid; std::array<uint8_t, 4096> old; };
  struct Slot { uint32_t tick, cosm, count; bool valid; std::vector<Entry> entries; Slot() : tick(~0u), cosm(0), count(0), valid(false) {} };
  struct Range { uint64_t off, len; bool protect; };
  struct Meta { uint32_t generation = 0, index = 0; bool protected_ = false; };
  static inline std::vector<Meta> meta;
  static inline std::vector<uint32_t> canonical_index, canonical_generation;
  static inline std::vector<uint32_t> touched;
  static inline size_t touched_count = 0;
  static inline std::vector<Range> ranges;
  static inline std::array<Slot, kWindow> slots;
  static inline uint8_t* base_ = nullptr;
  static inline Slot* current = nullptr;
  static inline uint32_t generation = 0, fight = 0, tick = 0, owner = 0;
  static inline uint32_t* cosm_ = nullptr;
  static inline bool armed = false, replay = false, failed = false;
  static inline uint64_t faults = 0, fault_ns = 0, protect_fail = 0, candidates = 0, skipped_ro = 0;
  static inline double arm_ms = 0;
  static inline double boundary_ms = 0;
  static inline std::ofstream csv, writers;
  static inline SRWLOCK lock = SRWLOCK_INIT;
  struct Locked { Locked() { AcquireSRWLockExclusive(&lock); } ~Locked() { ReleaseSRWLockExclusive(&lock); } };
  // xmemory.cpp map_info: TODOS los espejos, incluido el fisico crudo, comparten clave.
  static uint32_t Canon(uint64_t off) {
    if (off >= 0x100000000ull) off = 0xA0000000ull + off - 0x100000000ull;
    else if (off >= 0xE0000000ull) off = 0xA0001000ull + off - 0xE0000000ull;
    else if (off >= 0xC0000000ull) off = 0xA0000000ull + off - 0xC0000000ull;
    else if (off >= 0x90000000ull && off < 0xA0000000ull) off -= 0x10000000ull;
    else if (off >= 0x7F000000ull && off < 0x80000000ull) off = 0xA0000000ull + off - 0x7F000000ull;
    return uint32_t(off / 4096);
  }
  static LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        e->ExceptionRecord->NumberParameters < 2 || e->ExceptionRecord->ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t addr = e->ExceptionRecord->ExceptionInformation[1], b = reinterpret_cast<uintptr_t>(base_);
    if (!b || addr < b || addr - b >= 0x120000000ull) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t off = (addr - b) & ~4095ull;
    Locked hold;
    auto& m = meta[off / 4096];
    if (m.generation != generation) return EXCEPTION_CONTINUE_SEARCH;
    if (!m.protected_) {
      // Falta ya pendiente de otro hilo/alias, despues de desarmar.
      MEMORY_BASIC_INFORMATION q{};
      return VirtualQuery(base_ + off, &q, sizeof q) == sizeof q && q.Protect == PAGE_READWRITE
                 ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
    }
    const auto start = std::chrono::steady_clock::now();
    if (!m.index) { m.index = 1; touched[touched_count++] = uint32_t(off / 4096); }
    if (armed && current) {
      const uint32_t key = Canon(off);
      uint32_t i = canonical_index[key];
      if (canonical_generation[key] != generation) {
        i = current->count++;
        canonical_generation[key] = generation; canonical_index[key] = i;
        if (i < kCapacity) {
          auto& en = current->entries[i];
          en.page = uint32_t(off / 4096); en.writers = en.lr = en.cb = en.other_lr = en.other_tid = 0;
          std::memcpy(en.old.data(), base_ + off, 4096);
        } else { current->valid = false; failed = true; }
      }
      if (i < kCapacity) {
        auto* ts = rex::runtime::ThreadState::Get(); auto* ctx = ts ? ts->context() : nullptr;
        const uint32_t tid = ts ? ts->thread_id() : 0;
        auto& en = current->entries[i];
        if (GetCurrentThreadId() == owner) {
          en.writers |= 1; en.lr = ctx ? uint32_t(ctx->lr) : 0; en.cb = Audit::callback;
        } else {
          en.writers |= 2; en.other_tid = tid; en.other_lr = ctx ? uint32_t(ctx->lr) : 0;
        }
      }
      ++faults;
    }
    DWORD old;
    if (!VirtualProtect(base_ + off, 4096, PAGE_READWRITE, &old)) {
      ++protect_fail; failed = true; return EXCEPTION_CONTINUE_SEARCH;
    }
    m.protected_ = false;
    if (armed) fault_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  static bool Init(uint8_t* base, uint32_t f, uint32_t* cosm) {
    if (!On()) return true;
    End(); Release(); base_ = base; fight = f; cosm_ = cosm; failed = false;
    if (meta.empty()) {
      meta.resize(kViews); touched.resize(kViews); canonical_index.resize(kViews); canonical_generation.resize(kViews);
      static void* veh = AddVectoredExceptionHandler(1, Handler);
      if (!veh) { failed = true; return false; }
    }
    for (auto& s : slots) s.entries.resize(kCapacity);
    for (auto& s : slots) { s.valid = false; s.tick = ~0u; }
    if (!csv.is_open()) {
      csv.open(OutDir() / ("undo_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      csv << "fight,tick,replay,candidates,skipped_ro,all_pages,game_pages,mixed_pages,faults,handler_ms,arm_ms,disarm_ms,w8_bytes,protect_fail,valid,boundary_ms,allocated_bytes\n";
      writers.open(OutDir() / ("undo_writers_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      writers << "fight,tick,replay,page,writers,lr,callback,other_tid,other_lr\n";
    }
    auto* ts = rex::runtime::ThreadState::Get();
    REXLOG_INFO("dbz3 rb: UNDO combate {} guest {}: {} slots, {} bytes reservados; espejos incluidos; mixtas no restaurables", f,
                ts ? ts->thread_id() : 0, kWindow, kWindow * kCapacity * sizeof(Entry));
    return true;
  }
  static void Begin(uint32_t t, bool resim) {
    if (!On() || !base_ || failed) return;
    const auto start = std::chrono::steady_clock::now();
    Locked hold;
    owner = GetCurrentThreadId(); tick = t; replay = resim;
    current = &slots[t % kWindow]; current->tick = t; current->count = 0; current->valid = true;
    current->cosm = cosm_ ? *cosm_ : 0;
    ++generation; faults = fault_ns = candidates = skipped_ro = 0; boundary_ms = 0; touched_count = 0; ranges.clear();
    // Reserva del invitado = secciones mapeadas, NO MEM_WRITE_WATCH. Recorremos
    // cada tick tambien para ver nuevos compromisos/cambios de proteccion del SDK.
    for (uint64_t p = 0; p < 0x120000000ull;) {
      MEMORY_BASIC_INFORMATION q{};
      if (VirtualQuery(base_ + p, &q, sizeof q) != sizeof q) { failed = true; break; }
      const uint64_t end = std::min<uint64_t>(reinterpret_cast<uintptr_t>(q.BaseAddress) - reinterpret_cast<uintptr_t>(base_) + q.RegionSize, 0x120000000ull);
      if (end <= p) { failed = true; break; }
      if (q.State == MEM_COMMIT && q.Protect == PAGE_READONLY) {
        // Persistencia opt-in: conservar solo nuestras RO; nunca apropiarnos
        // de las guardas del SDK que VirtualQuery haya fusionado con ellas.
        for (uint64_t a = p; a < end;) {
          if (!Persistent() || !meta[a / 4096].protected_) { ++skipped_ro; a += 4096; continue; }
          const uint64_t first = a;
          do { a += 4096; } while (a < end && meta[a / 4096].protected_);
          ranges.push_back({first, a - first, false});
        }
      }
      if (q.State == MEM_COMMIT && q.Protect == PAGE_READWRITE) ranges.push_back({p, end - p, true});
      p = end;
    }
    armed = true;
    for (const auto& r : ranges) {
      for (uint64_t p = r.off; p < r.off + r.len; p += 4096) { meta[p / 4096] = {generation, 0, true}; ++candidates; }
      DWORD old;
      if (r.protect && !VirtualProtect(base_ + r.off, size_t(r.len), PAGE_READONLY, &old)) { ++protect_fail; failed = true; current->valid = false; }
    }
    arm_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  // Re-armar al cambiar de tarea permite DEMOSTRAR escritores mixtos. No
  // demuestra exclusividad: dos escrituras entre fronteras pueden no observarse.
  static void Boundary() {
    if (!On() || GetCurrentThreadId() != owner || !armed) return;
    const auto start = std::chrono::steady_clock::now();
    static const bool mix = std::getenv("DBZ3_RB_UNDO_MIX") != nullptr;
    Locked hold;
    for (size_t i = 0; i < touched_count; ++i) {
      const uint64_t p = uint64_t(touched[i]) * 4096;
      auto& m = meta[p / 4096];
      const auto index = canonical_index[Canon(p)];
      if (index >= kCapacity) continue;
      const auto& en = current->entries[index];
      if (!mix && (en.writers & 1)) continue;
      if (!m.protected_) {
        DWORD old;
        if (VirtualProtect(base_ + p, 4096, PAGE_READONLY, &old)) m.protected_ = true;
        else { ++protect_fail; failed = true; current->valid = false; }
      }
    }
    boundary_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  static void End() {
    if (!On() || !armed) return;
    const auto start = std::chrono::steady_clock::now();
    {
      Locked hold;
      if (!Persistent()) ReleaseLocked();
      armed = false;
    }
    uint32_t game = 0, mixed = 0;
    for (uint32_t i = 0; i < std::min<uint32_t>(current->count, kCapacity); ++i) {
      const auto& en = current->entries[i]; game += (en.writers & 1) != 0; mixed += en.writers == 3;
      if (en.writers == 3 || tick % 30 == 0) {
        char line[180];
        std::snprintf(line, sizeof line, "%u,%u,%u,%08llX,%u,%08X,%08X,%u,%08X\n", fight, tick, replay,
                      static_cast<unsigned long long>(uint64_t(en.page) * 4096), en.writers, en.lr, en.cb, en.other_tid, en.other_lr);
        writers << line;
      }
    }
    const double end_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    csv << fight << ',' << tick << ',' << replay << ',' << candidates << ',' << skipped_ro << ',' << current->count << ',' << game << ',' << mixed << ','
        << faults << ',' << double(fault_ns) / 1e6 << ',' << arm_ms << ',' << end_ms << ',' << uint64_t(game) * 4096 * 8 << ',' << protect_fail << ',' << (current->valid && !failed)
        << ',' << boundary_ms << ',' << kWindow * kCapacity * sizeof(Entry) + meta.size() * sizeof(Meta) + (canonical_index.size() + canonical_generation.size() + touched.size()) * sizeof(uint32_t) << '\n';
    csv.flush(); writers.flush();
  }
  static void ReleaseLocked() {
    for (const auto& r : ranges) {
      // No pisar protecciones que el SDK haya puesto despues de nuestro fault.
      for (uint64_t p = r.off; p < r.off + r.len;) {
        if (!meta[p / 4096].protected_) { p += 4096; continue; }
        const uint64_t first = p;
        do { meta[p / 4096].protected_ = false; p += 4096; } while (p < r.off + r.len && meta[p / 4096].protected_);
        DWORD old;
        if (!VirtualProtect(base_ + first, size_t(p - first), PAGE_READWRITE, &old)) { ++protect_fail; failed = true; current->valid = false; }
      }
    }
    ranges.clear();
  }
  static void Release() {
    if (!On() || !base_) return;
    Locked hold; ReleaseLocked();
  }
  static void ForgetPayload() {
    if (!On() || armed) return;
    Locked hold;
    current = nullptr;
    for (auto& s : slots) { std::vector<Entry>().swap(s.entries); s.valid = false; }
  }
  static bool Restore(uint32_t target, uint32_t now) {
    if (!On()) return true;
    End(); Release();
    if (failed || target > now || now - target >= kWindow) return false;
    // Comprobar TODO antes de modificar el primer byte.
    for (uint32_t t = target; t < now; ++t) {
      const auto& s = slots[t % kWindow]; if (!s.valid || s.tick != t) return false;
      for (uint32_t i = 0; i < s.count; ++i) if (s.entries[i].writers == 3) {
        const auto& en = s.entries[i];
        REXLOG_ERROR("dbz3 rb: UNDO bloqueado: tick {} pagina {:X}, guest 6 lr {:08X}, otro guest {} lr {:08X}; falta barrera de recursos/audio",
                     t, uint64_t(en.page) * 4096, en.lr, en.other_tid, en.other_lr);
        return false;   // no devolver bloqueos/colas compartidas al pasado con sus consumidores vivos
      }
    }
    for (uint32_t t = now; t > target;) {
      auto& s = slots[--t % kWindow];
      for (uint32_t i = s.count; i > 0;) {
        const auto& en = s.entries[--i];
        if (!(en.writers & 1)) continue;   // audio / workers exclusivamente externos NO se deshacen
        SIZE_T n = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), base_ + uint64_t(en.page) * 4096, en.old.data(), 4096, &n) || n != 4096) { failed = true; return false; }
      }
    }
    if (cosm_) *cosm_ = slots[target % kWindow].cosm;
    return true;
  }
};
}  // namespace dbz3::net::rb
#endif
