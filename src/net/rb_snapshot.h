// sol-R4: copia local del conjunto aprendido. Experimental, nunca se transmite.
// DBZ3_RB_SNAP=1; SNAP_LEARN=N (48), SNAP_CHECK=N (1, 0 desactiva la sonda).
#pragma once
#ifdef _WIN32
namespace dbz3::net::rb {
class Snapshot {
 public:
  static bool On() { static const bool v = Env("DBZ3_RB_SNAP"); return v; }
  static bool Env(const char* name) { const char* s = std::getenv(name); return s && std::strcmp(s, "1") == 0; }
  static uint32_t Number(const char* name, uint32_t fallback) {
    const char* s = std::getenv(name); return s ? uint32_t(std::max(0, std::atoi(s))) : fallback;
  }
  struct Page { uint32_t at = 0, writers = 0, lr = 0, other_lr = 0, other_tid = 0; };
  struct Slot { uint32_t tick, cosm, epoch; std::vector<uint8_t> bytes; Slot() : tick(~0u), cosm(0), epoch(0) {} };
  struct Mask { uint32_t at, len; const char* proof; };
  struct HashPage { uint32_t at; uint64_t hash; bool seen; };
  static inline std::vector<Page> pages;
  static inline std::map<uint32_t, size_t> index;
  static inline std::array<Slot, 9> slots;
  static inline std::vector<Mask> masks;
  static inline std::vector<std::pair<uint32_t, uint32_t>> stacks;
  static inline std::vector<HashPage> outside;
  static inline std::vector<uint8_t> census_seen;
  static inline uint8_t* base_ = nullptr;
  static inline uint32_t *cosm_ = nullptr, fight = 0, tick = 0, epoch = 0, learned = 0;
  static inline bool learning = false, active = false, resim = false, failed = false;
  static inline double save_ms = 0, copy_ms = 0, check_ms = 0;
  static inline uint32_t added = 0;
  static inline size_t scan_pos = 0;
  static inline uint64_t census_pos = 0;
  static inline bool census = false;
  static inline uint32_t device_ = 0;
  static inline std::ofstream csv, map_csv, mask_csv;
  static inline std::vector<uint8_t> readbuf;
  static bool Stack(uint32_t at) {
    // Limites EXACTOS del KPCR del hilo del tick; no excluir un heap entero.
    for (const auto& s : stacks) if (uint64_t(at) + 4096 > s.first && at < s.second) return true;
    return false;
  }
  static void Preserve(uint32_t at, uint32_t len, const char* proof) {
    if (!On() || !len || uint64_t(at) + len > 0x100000000ull) return;
    // Las mascaras y las copias deben tener LA MISMA clave de alias. Por
    // ejemplo, una reserva E... del driver tambien se ve por A.../C.../raw.
    at = Undo::Canon(at) * 4096 + (at & 4095);
    for (const auto& m : masks) if (m.at == at && m.len == len) return;
    masks.push_back({at, len, proof});
    if (mask_csv.is_open()) {
      char row[320]; std::snprintf(row, sizeof row, "%u,%08X,%u,%s\n", fight, at, len, proof);
      mask_csv << row; mask_csv.flush();
    }
  }
  static bool Init(uint8_t* base, uint32_t f, uint32_t* cosm) {
    if (!On()) return Undo::Init(base, f, cosm);
    End(); Undo::Release();
    base_ = base; cosm_ = cosm; fight = f; learned = 0; ++epoch;
    pages.clear(); index.clear(); outside.clear(); masks.clear(); stacks.clear(); failed = active = census = false; scan_pos = 0; census_pos = 0; device_ = 0;
    census_seen.assign(kLimit / 4096, 0);
    if (dbz3::g_guest_eu || std::getenv("DBZ3_RB_ORACLE")) {
      REXLOG_ERROR("dbz3 rb: SNAP bloqueado: sonda US; los oraculos antiguos no usan este anillo");
      failed = true; return false;
    }
    for (auto& s : slots) { s.tick = ~0u; s.bytes.clear(); }
    if (!csv.is_open()) {
      const auto suffix = std::to_string(GetCurrentProcessId()) + ".csv";
      csv.open(OutDir() / ("snap_" + suffix));
      csv << "fight,tick,replay,learning,pages,mixed,unknown,payload,w8_bytes,ring_bytes,save_ms,copy_ms,check_ms,added,epoch,outside,scan_pos,ring_alloc_bytes,learning_alloc_bytes,meta_alloc_bytes,outside_alloc_bytes,valid\n";
      map_csv.open(OutDir() / ("snap_pages_" + suffix));
      map_csv << "fight,tick,page,writers,lr,other_tid,other_lr\n";
      mask_csv.open(OutDir() / ("snap_masks_" + suffix));
      mask_csv << "fight,addr,len,proof\n";
    }
    // Objetos exactos, no exclusiones de pagina. sub_821EE838 inicializa 28 B.
    Preserve(0x824E6A88, 28, "821DF920/.35:19278;821EE838/.32:20151");
    for (uint32_t i = 0; i < 8; ++i)
      Preserve(0x824F0CD0 + i * 32, 28, "821E9768/.27:19817;821EE838/.32:20151");
    Preserve(0x824F0DDC, 28, "821EA170/.2:20180;821EE838/.32:20151");
    Preserve(0x824F0F08, 28, "821EB630/.12:19501;821EE838/.32:20151");
    Preserve(0x824F0FA4, 28, "821EF6E0/.1:19859;821EE838/.32:20151");
    if (auto* ts = rex::runtime::ThreadState::Get()) {
      const uint32_t pcr = ts->context()->r13.u32;
      const uint32_t low = REX_LOAD_U32(pcr + 0x74), high = REX_LOAD_U32(pcr + 0x70);
      if (low < high && high - low <= 16 * 1024 * 1024) {
        stacks.push_back({low, high});
        Preserve(low, high - low, "xthread.cpp:425-426;X_KPCR.stack_end/base+74/70;marco activo no rebobinable");
      } else { REXLOG_ERROR("dbz3 rb: SNAP bloqueado: limites KPCR de pila invalidos"); failed = true; return false; }
    }
    readbuf.resize(4096);
    return Undo::Init(base, f, cosm);
  }
  static void Device(uint32_t g, uint8_t* base) {
    if (!On() || !active || GetCurrentThreadId() != Undo::owner || device_ == g) return;
    if (g < 0x10000 || uint64_t(g) + 24704 >= kLimit) return;
    device_ = g;
    Preserve(g, 24704, "822360E8/.5:24009 pide0x6080;82246370/82246908 dispositivo vivo");
    // Incluye cursor, limites, dos CS, spinlocks y cola de presentacion del
    // MISMO objeto; las estructuras apuntadas se conservan por separado.
    const uint32_t ring = REX_LOAD_U32(g + 15300), main = REX_LOAD_U32(g + 15308), end = REX_LOAD_U32(g + 15312);
    const uint64_t ring_len = (uint64_t(REX_LOAD_U32(g + 15304)) + 1) * 4;
    if (ring && ring_len <= 1024 * 1024) Preserve(ring, uint32_t(ring_len), "82232870/.21:23976 anillo=(mask+1)*4");
    if (main && end >= main && uint64_t(end) + 4 - main <= 64 * 1024 * 1024)
      Preserve(main, end + 4 - main, "82232870/.21:23976 buffer principal;limite ultimo dword");
    for (const auto& v : {std::make_pair(11024u,128u),{11028u,32u},{24400u,4096u},{24408u,512u},{24424u,8192u},{24432u,1260u},{24448u,256u},{24452u,256u}}) {
      const uint32_t at = REX_LOAD_U32(g + v.first);
      if (at) Preserve(at, v.second, "82246908/.19:24736;82235F70/.26:23867;82246370/.13:24117 reservas driver");
    }
    REXLOG_INFO("dbz3 rb: SNAP objeto D3D {:08X}+0x6080, anillo {:08X}, comandos {:08X}..{:08X}; mascaras con dueno",g,ring,main,end);
  }
  static void AudioSource(uint32_t s, uint8_t* base) {
    if (!On() || !active || GetCurrentThreadId() != Undo::owner) return;
    const uint32_t c = REX_LOAD_U32(s + 88);
    Preserve(s + 188, 4, "822849D8/.7:28680 lista libre;S+188 en8228D568");
    Preserve(s + 212, 12, "8228D568/.25:28955;8228FC28/.17:28787 cabeza/cola/cantidad");
    Preserve(s + 228, 4, "8228D568/.25:28955 wrapper notificaciones;NO cubre nodos");
    if (c) {
      const uint32_t vtable = REX_LOAD_U32(c + 44);
      const uint32_t enter = REX_LOAD_U32(vtable + 8);
      // No promover el supuesto wrapper+4 sin comprobar su implementacion.
      if (enter == 0x82283F58)
        Preserve(c + 48, 28, "82283F58/.34:27879 enter wrapper+4;82283EC0/.24:28077 init28B;vtable verificada");
    }
  }
  static uint32_t Covered(uint32_t at) {
    std::vector<std::pair<uint32_t,uint32_t>> spans;
    for (const auto& m : masks) {
      const uint64_t b = std::max<uint64_t>(at,m.at), e = std::min<uint64_t>(uint64_t(at)+4096,uint64_t(m.at)+m.len);
      if (b<e) spans.push_back({uint32_t(b-at),uint32_t(e-at)});
    }
    std::sort(spans.begin(),spans.end()); uint32_t total=0,pos=0;
    for (const auto& s : spans) if (s.second>pos) { total+=s.second-std::max(pos,s.first);pos=s.second; }
    return total;
  }
  static void Add(uint32_t at, uint32_t flags, uint32_t lr = 0, uint32_t tid = 0, uint32_t other = 0) {
    at = Undo::Canon(at) * 4096;
    if (Stack(at)) return;
    const auto it = index.find(at);
    if (it != index.end()) {
      auto& p = pages[it->second];
      const bool fresh = (p.writers | flags) != p.writers;
      p.writers |= flags;
      if (lr && !p.lr) p.lr = lr;
      if (tid) { p.other_tid = tid; p.other_lr = other; }
      if (fresh) {
        char row[160]; std::snprintf(row, sizeof row, "%u,%u,%08X,%u,%08X,%u,%08X\n", fight, tick, at, p.writers, p.lr, p.other_tid, p.other_lr);
        map_csv << row; map_csv.flush();
      }
      return;
    }
    if (pages.size() >= 8192) {
      if (!failed) REXLOG_ERROR("dbz3 rb: SNAP bloqueado: conjunto excede 8192 paginas; no se trunca silenciosamente");
      failed = true; return;
    }
    index[at] = pages.size(); pages.push_back({at, flags, lr, other, tid}); ++added;
    char row[160]; std::snprintf(row, sizeof row, "%u,%u,%08X,%u,%08X,%u,%08X\n", fight, tick, at, flags, lr, tid, other);
    map_csv << row; map_csv.flush();
  }
  static void Boundary() { if (!On() || learning) Undo::Boundary(); }
  static bool Ready(uint32_t t) {
    if (!On()) return true;
    const auto& s = slots[t % slots.size()];
    return !failed && !learning && s.tick == t && s.epoch == epoch && s.bytes.size() == pages.size() * 4096;
  }
  static bool Read(uint32_t at, uint8_t* out) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), base_ + at, out, 4096, &got) && got == 4096;
  }
  static bool Copy(uint8_t* out, const uint8_t* in) {
    // No VirtualQuery por pagina/tick: en esta reserva fragmentada su coste
    // medido domina al memcpy. SEH solo en el helper sin objetos C++ locales;
    // una desasignacion invalida la instantanea, nunca se usa una copia parcial.
    __try { std::memcpy(out, in, 4096); return true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
  }
  static void CensusStep() {
    // Descubrir compromisos RW nuevos sin recorrer toda la reserva en un tick.
    // Un compromiso nuevo es candidato desconocido, nunca "estado de juego"
    // demostrado. Los cambios de proteccion transitorios entre sondas no se ven.
    size_t queried=0, visited=0;
    while (queried++ < 32 && visited < 1024) {
      MEMORY_BASIC_INFORMATION q{};
      if (VirtualQuery(base_ + census_pos, &q, sizeof q) != sizeof q) { failed=true; return; }
      const uint64_t end=std::min<uint64_t>(uint64_t(reinterpret_cast<uint8_t*>(q.BaseAddress)-base_)+q.RegionSize,kLimit);
      if (end<=census_pos) { failed=true; return; }
      if (q.State==MEM_COMMIT && q.Protect==PAGE_READWRITE) {
        for (;census_pos+4096<=end && visited<1024;census_pos+=4096,++visited) {
          const uint32_t at=uint32_t(census_pos);
          if (Undo::Canon(census_pos)*4096!=census_pos || Stack(at)) continue;
          auto& seen=census_seen[at/4096];
          if (!seen && !index.count(at)) Add(at,4);
          seen=1;
        }
        if (visited==1024) { if (census_pos==kLimit) census_pos=0; break; }
      }
      census_pos=end==kLimit?0:end;
    }
  }
  static void Check(bool first) {
    const auto start = std::chrono::steady_clock::now();
    // Modo de prueba: hashes fuera del conjunto, NO prueba de autoria. Una
    // diferencia amplía el conjunto pero queda marcada desconocida (bit 4).
    // No se restaura una pagina desconocida con consumidores vivos.
    if (first) {
    for (uint64_t p = 0; p < 0xC0000000ull;) {
      MEMORY_BASIC_INFORMATION q{};
      if (VirtualQuery(base_ + p, &q, sizeof q) != sizeof q) { failed = true; break; }
      const uint64_t end = std::min<uint64_t>(uint64_t(reinterpret_cast<uint8_t*>(q.BaseAddress) - base_) + q.RegionSize, 0xC0000000ull);
      if (end <= p) { failed = true; break; }
      if (q.State == MEM_COMMIT && q.Protect == PAGE_READWRITE) {
        for (uint64_t a = p; a + 4096 <= end; a += 4096) {
          const uint32_t at = uint32_t(a);
          if (Undo::Canon(a) * 4096 != a || Stack(at) || index.count(at)) continue;
          if (!Read(at, readbuf.data())) continue;
          outside.push_back({at, H(readbuf.data(),4096), true});
          census_seen[at/4096]=1;
        }
      }
      p = end;
    }
    census = true;
    }
    // El baseline completo es un coste UNICO tras aprender; despues solo una
    // franja y un censo incremental, con latencia de deteccion finita.
    // El CSV expone tamano/cursor para no vender la sonda como cobertura total.
    const size_t budget = std::min<size_t>(outside.size(), std::max(1u, Number("DBZ3_RB_SNAP_SCAN_PAGES", 1024)));
    for (size_t n = 0; n < budget; ++n) {
      auto& p = outside[scan_pos++];
      if (scan_pos == outside.size()) scan_pos = 0;
      if (index.count(p.at) || !Read(p.at, readbuf.data())) continue;
      const auto h = H(readbuf.data(), 4096);
      if (p.seen && p.hash != h) Add(p.at, 4);
      p.hash = h; p.seen = true;
    }
    if (!first) CensusStep();
    if (added) ++epoch;   // el pasado sin las paginas nuevas no es restaurable
    check_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  static void Begin(uint32_t t, bool replay) {
    if (!On()) { Undo::Begin(t, replay); return; }
    if (!base_ || failed) return;
    tick = t; resim = replay; added = 0; save_ms = copy_ms = check_ms = 0;
    learning = !replay && learned < std::max(1u, Number("DBZ3_RB_SNAP_LEARN", 48));
    active = true;
    if (learning) { Undo::Begin(t, false); return; }
    const uint32_t interval = Number("DBZ3_RB_SNAP_CHECK", 1);
    if (!replay && interval && (!census || t % interval == 0)) Check(!census);
    if (failed) { REXLOG_ERROR("dbz3 rb: SNAP bloqueado: fallo de censo/verificacion"); return; }
    auto& s = slots[t % slots.size()];
    s.tick = ~0u; s.epoch = epoch; s.cosm = cosm_ ? *cosm_ : 0;
    s.bytes.resize(pages.size() * 4096);
    const auto start = std::chrono::steady_clock::now();
    const auto copy_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < pages.size(); ++i)
      if (!Copy(s.bytes.data() + i * 4096, base_ + pages[i].at)) {
        REXLOG_ERROR("dbz3 rb: SNAP bloqueado: pagina {:08X} ya no legible", pages[i].at); failed = true; break;
      }
    copy_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copy_start).count();
    save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!failed) s.tick = t;
  }
  static void End() {
    if (!On()) { Undo::End(); return; }
    if (!active) return;
    if (learning) {
      Undo::End();
      if (Undo::failed || !Undo::current || !Undo::current->valid) {
        failed = true; REXLOG_ERROR("dbz3 rb: SNAP bloqueado: aprendizaje incompleto");
      }
      else for (uint32_t i = 0; i < Undo::current->count; ++i) {
        const auto& en = Undo::current->entries[i];
        // Solo paginas con escritor del tick; externos puros quedan vivos.
        if (en.writers & 1) Add(en.page * 4096, en.writers, en.lr, en.other_tid, en.other_lr);
      }
      if (++learned >= std::max(1u, Number("DBZ3_RB_SNAP_LEARN", 48))) {
        Undo::Release(); ++epoch;
        Undo::ForgetPayload();
        REXLOG_INFO("dbz3 rb: SNAP aprendido: {} paginas, {} bytes W8; desde ahora copia sin guardas", pages.size(), pages.size() * 4096 * 8);
      }
    }
    uint32_t mixed = 0, unknown = 0;
    for (const auto& p : pages) { mixed += (p.writers & 3) == 3; unknown += (p.writers & 4) != 0; }
    uint64_t ring_alloc = 0, undo_alloc = 0;
    for (const auto& s : slots) ring_alloc += s.bytes.capacity();
    for (const auto& s : Undo::slots) undo_alloc += s.entries.capacity() * sizeof(Undo::Entry);
    const uint64_t meta_alloc = Undo::meta.capacity() * sizeof(Undo::Meta) +
        (Undo::canonical_index.capacity() + Undo::canonical_generation.capacity() + Undo::touched.capacity()) * sizeof(uint32_t);
    csv << fight << ',' << tick << ',' << resim << ',' << learning << ',' << pages.size() << ',' << mixed << ',' << unknown << ','
        << pages.size() * 4096 << ',' << pages.size() * 4096 * 8 << ',' << pages.size() * 4096 * slots.size() << ','
        << save_ms << ',' << copy_ms << ',' << check_ms << ',' << added << ',' << epoch << ',' << outside.size() << ',' << scan_pos << ','
        << ring_alloc << ',' << undo_alloc << ',' << meta_alloc << ',' << outside.capacity() * sizeof(HashPage) + census_seen.capacity() + readbuf.capacity() << ',' << !failed << '\n'; csv.flush();
    active = false;
  }
  static void Release() { if (On()) End(); Undo::Release(); }
  static bool Restore(uint32_t target, uint32_t now) {
    if (!On()) return Undo::On() ? Undo::Restore(target, now) : true;
    End(); Undo::Release();
    if (!Ready(target) || now < target || now - target >= slots.size()) return false;
    // No basta conocer un lock: los nodos/cursores de sus colas tambien son
    // vivos. Hasta demostrar los objetos completos, rechazo ANTES de escribir.
    uint32_t shared=0, uncovered=0;
    for (const auto& p : pages) if ((p.writers & 3) == 3 || (p.writers & 4)) {
      ++shared;
      const uint32_t covered = Covered(p.at);
      if (covered == 4096) continue;
      if (++uncovered <= 8)
        REXLOG_ERROR("dbz3 rb: SNAP bloqueado: pagina {:08X}, writers {}, lr {:08X}, otro guest {} lr {:08X}; {} de4096 bytes preservados; falta cerrar mascara de objeto/cola", p.at, p.writers, p.lr, p.other_tid, p.other_lr, covered);
    }
    if (uncovered) {
      REXLOG_ERROR("dbz3 rb: SNAP preflight: {} compartidas/desconocidas, {} preservadas completas, {} incompletas; 0 bytes restaurados",shared,shared-uncovered,uncovered);
      return false;
    }
    const auto& s = slots[target % slots.size()];
    // Las mascaras nunca se pisan temporalmente: copiar solo huecos logicos.
    for (size_t i = 0; i < pages.size(); ++i) {
      const uint32_t at = pages[i].at;
      std::vector<std::pair<uint32_t, uint32_t>> holes;
      for (const auto& m : masks) {
        const uint64_t b = std::max<uint64_t>(at, m.at), e = std::min<uint64_t>(uint64_t(at) + 4096, uint64_t(m.at) + m.len);
        if (b < e) holes.push_back({uint32_t(b - at), uint32_t(e - at)});
      }
      std::sort(holes.begin(), holes.end()); uint32_t pos = 0;
      for (const auto& h : holes) { if (pos < h.first) std::memcpy(base_ + at + pos, s.bytes.data() + i * 4096 + pos, h.first - pos); pos = std::max(pos, h.second); }
      if (pos < 4096) std::memcpy(base_ + at + pos, s.bytes.data() + i * 4096 + pos, 4096 - pos);
    }
    if (cosm_) *cosm_ = s.cosm;
    return true;
  }
};
}  // namespace dbz3::net::rb
#endif
