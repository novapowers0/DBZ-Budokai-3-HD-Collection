// sol-R4: journal de frontera CPU -> audio/D3D, solo diagnostico US.
// DBZ3_RB_EFFECTS=1. Replay verdadero: devuelve el resultado grabado sin
// volver a publicar el efecto. NO sirve con eventos nuevos de mandos falsos:
// una forma/orden diferente se rechaza. Map/creacion requieren estado sombra.
#pragma once
#ifdef _WIN32
namespace dbz3::net::rb {
struct Effects {
  static bool On() { static const bool v = Snapshot::Env("DBZ3_RB_EFFECTS"); return v; }
  struct Stop {};
  struct Regs {
    // Registros volatiles de la ABI Xenon; no reproducir pila, TLS ni callee-save.
    std::array<uint64_t, 10> r;
    std::array<uint64_t, 14> f;
    decltype(PPCContext::cr0) cr0, cr1, cr5, cr6, cr7;
    decltype(PPCContext::xer) xer;
    decltype(PPCContext::fpscr) fpscr;
    uint64_t ctr;
    void Get(const PPCContext& c) {
      r = {c.r3.u64,c.r4.u64,c.r5.u64,c.r6.u64,c.r7.u64,c.r8.u64,c.r9.u64,c.r10.u64,c.r11.u64,c.r12.u64};
      f = {c.f0.u64,c.f1.u64,c.f2.u64,c.f3.u64,c.f4.u64,c.f5.u64,c.f6.u64,c.f7.u64,c.f8.u64,c.f9.u64,c.f10.u64,c.f11.u64,c.f12.u64,c.f13.u64};
      cr0=c.cr0;cr1=c.cr1;cr5=c.cr5;cr6=c.cr6;cr7=c.cr7;xer=c.xer;fpscr=c.fpscr;ctr=c.ctr.u64;
    }
    void Put(PPCContext& c) const {
      c.r3.u64=r[0];c.r4.u64=r[1];c.r5.u64=r[2];c.r6.u64=r[3];c.r7.u64=r[4];c.r8.u64=r[5];c.r9.u64=r[6];c.r10.u64=r[7];c.r11.u64=r[8];c.r12.u64=r[9];
      c.f0.u64=f[0];c.f1.u64=f[1];c.f2.u64=f[2];c.f3.u64=f[3];c.f4.u64=f[4];c.f5.u64=f[5];c.f6.u64=f[6];c.f7.u64=f[7];c.f8.u64=f[8];c.f9.u64=f[9];c.f10.u64=f[10];c.f11.u64=f[11];c.f12.u64=f[12];c.f13.u64=f[13];
      c.cr0=cr0;c.cr1=cr1;c.cr5=cr5;c.cr6=cr6;c.cr7=cr7;c.xer=xer;c.fpscr=fpscr;c.ctr.u64=ctr;
    }
  };
  struct Call { uint32_t fn, lr, out, value, gpr_mask, fpr_mask; Regs in, result; };
  struct Slot { uint32_t tick; std::vector<Call> calls; Slot() : tick(~0u) {} };
  static inline thread_local std::array<Slot, 9> slots;
  static inline thread_local bool active = false, replay = false;
  static inline thread_local uint32_t tick = 0, fight = 0, depth = 0, cursor = 0, suppressed = 0;
  static inline thread_local uint32_t dispatch = 0;
  static inline thread_local uint32_t first_tick = ~0u, replay_count = 0;
  static inline thread_local std::ofstream csv, stats;
  static void Init(uint32_t f) {
    if (!On()) return;
    active = false; fight = f; first_tick=~0u; replay_count=0;
    for (auto& s : slots) { s.tick = ~0u; s.calls.clear(); }
    if (!csv.is_open()) {
      csv.open(OutDir() / ("effects_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      csv << "fight,tick,replay,ordinal,fn,lr,arg3,arg4,out,value,status\n";
      stats.open(OutDir() / ("effects_stats_" + std::to_string(GetCurrentProcessId()) + ".csv"));
      stats << "fight,tick,replay,calls,used,journal_bytes,status\n";
    }
  }
  static void Begin(uint32_t t, bool resim) {
    if (!On() || !fight || dbz3::g_guest_eu) return;
    tick=t; replay=resim; cursor=depth=suppressed=dispatch=0; active=true;
    if (first_tick==~0u) first_tick=t;
    if (resim) ++replay_count;
    auto& s=slots[t%slots.size()];
    if (!resim) { s.tick=t; s.calls.clear(); }
    else if (s.tick!=t) Block(0, 0, "sin journal del tick");
  }
  static void End() {
    if (!active) return;
    if (replay && cursor!=slots[tick%slots.size()].calls.size()) Block(0, 0, "faltan llamadas del forward");
    Stats("complete");
    csv.flush(); active=false;
  }
  static void Abort() { active=false; depth=dispatch=0; if (csv.is_open()) csv.flush(); }
  static bool Replaying() { return On() && active && replay && !dbz3::g_guest_eu; }
  static void Block(uint32_t fn, uint32_t lr, const char* reason) {
    REXLOG_ERROR("dbz3 rb: EFFECTS bloqueado: tick {} ordinal {} fn {:08X} lr {:08X}: {}", tick,cursor,fn,lr,reason);
    Stats("blocked"); csv.flush(); throw Stop{};
  }
  static void Stats(const char* status) {
    size_t bytes=0; for (const auto& s : slots) bytes+=s.calls.capacity()*sizeof(Call);
    stats<<fight<<','<<tick<<','<<replay<<','<<slots[tick%slots.size()].calls.size()<<','<<cursor<<','<<bytes<<','<<status<<'\n';stats.flush();
  }
  // out_arg: 0 ninguno; 4/8 puntero r4/r8 a exactamente 4 bytes.
  static void CallApi(uint32_t fn, PPCContext& c, uint8_t* base, void (*orig)(PPCContext&,uint8_t*), uint32_t gpr, uint32_t fpr=0, uint32_t out_arg=0) {
    if (!On() || !active || dbz3::g_guest_eu || depth) { orig(c,base); return; }
    // El replay 0024 repite el despachador, no las otras 15 llamadas del bucle.
    // El journal se delimita en esa misma funcion en las dos pasadas.
    if (!dispatch) { if (replay) Block(fn,uint32_t(c.lr),"API fuera del despachador grabado"); orig(c,base); return; }
    Regs in; in.Get(c);
    const uint32_t lr=uint32_t(c.lr), out=out_arg==4?c.r4.u32:out_arg==8?c.r8.u32:0;
    auto& s=slots[tick%slots.size()];
    if (replay) {
      if (cursor>=s.calls.size()) Block(fn,lr,"llamada nueva; no se inventa retorno");
      const auto& a=s.calls[cursor];
      if (a.fn!=fn || a.lr!=lr || a.gpr_mask!=gpr || a.fpr_mask!=fpr || bool(a.out)!=bool(out)) Block(fn,lr,"orden/forma distintos");
      for (size_t i=0;i<in.r.size();++i) if ((gpr>>i&1) && in.r[i]!=a.in.r[i]) {
        REXLOG_ERROR("dbz3 rb: EFFECTS r{} esperado {:X} recibido {:X}",i+3,a.in.r[i],in.r[i]);
        Block(fn,lr,"argumento entero distinto");
      }
      for (size_t i=0;i<in.f.size();++i) if ((fpr>>i&1) && in.f[i]!=a.in.f[i]) Block(fn,lr,"argumento flotante distinto");
      Regs result = a.result;
      if (a.out) for (auto& r : result.r) if (r >= a.out && r < uint64_t(a.out) + 4) r = uint64_t(out) + r - a.out;
      result.Put(c);
      if (out) REX_STORE_U32(out,a.value);
      ++suppressed;
    } else {
      Call a{}; a.fn=fn;a.lr=lr;a.out=out;a.in=in;a.gpr_mask=gpr;a.fpr_mask=fpr;
      ++depth; orig(c,base); --depth;
      a.result.Get(c);a.value=out?REX_LOAD_U32(out):0;s.calls.push_back(a);
    }
    const auto& a=s.calls[cursor];
    if ((replay && replay_count<=4) || (!replay && tick<first_tick+2) || out) {
      char row[200];std::snprintf(row,sizeof row,"%u,%u,%u,%u,%08X,%08X,%08X,%08X,%08X,%08X,%s\n",fight,tick,replay,cursor,fn,lr,uint32_t(in.r[0]),uint32_t(in.r[1]),out,a.value,replay?"sink":"forward");
      csv<<row;
    }
    ++cursor;
  }
  static void Dispatch(PPCContext& c, uint8_t* base, void (*orig)(PPCContext&,uint8_t*)) {
    if (!On() || !active) { orig(c,base); return; }
    ++dispatch;
    try { orig(c,base); } catch (...) { --dispatch; throw; }
    --dispatch;
  }
  static void GpuApi(uint32_t fn, PPCContext& c, uint8_t* base, void (*orig)(PPCContext&,uint8_t*)) {
    if (!On() || !active || dbz3::g_guest_eu || depth) { orig(c,base); return; }
    static const bool gpu = Snapshot::Env("DBZ3_RB_GPU_TAPE");
    if (!gpu) { Guard(fn,c); orig(c,base); return; }
    // Solo control verdadero->verdadero: identidad de dispositivo/recurso +
    // orden exacto. NO acredita equivalencia con argumentos graficos corregidos.
    CallApi(fn,c,base,orig,1);
  }
  static void Guard(uint32_t fn, PPCContext& c) {
    if (Replaying()) Block(fn,uint32_t(c.lr),"frontera externa no aislada: requiere destino privado");
  }
};
}  // namespace dbz3::net::rb
#endif
