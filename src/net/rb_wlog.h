// Opus R5 (DBZ3_RB_WLOG=1, solo US/Windows): rollback por registro de deshacer en vez de instantaneas de paginas.
// Cada tick apunta el valor anterior de cada palabra que escribe la LOGICA del hilo del juego (rb_store.h engancha todas las escrituras
// del codigo generado). Deshacer los ticks t-1..u deja la memoria del juego como al empezar u, sin tocar lo que escriben otros hilos
// ni las librerias. Queda fuera, y no se deshace:
//  - las funciones de librerias (D3D/Xam/kernel 8221C000+, sonido 821DC000-8221C000 y 82283000-822F4000): sus colas, locks y nodos
//    los comparten el hilo de audio y la GPU (bloqueo de R3/R4: paginas mixtas 824E6000/824F0000/40105000...);
//  - las tareas de DIBUJO del despachador (prioridad < 0x4000: [BTL] PRE DRAW .. [BTL] POST DRAW), que tampoco se resimulan;
//  - las palabras que alguien toca con reserva atomica (lwarx/stwcx: locks), la pila del hilo y la memoria fisica/GPU (>= A0000000).
// Lo que se resimula (ResimTick: mandos + sub_820927E0 + LFSR) es exactamente lo que se registra.
#pragma once
#ifdef _WIN32
#include "rb_store.h"
namespace dbz3::net::rb {
struct WLogTick {
  struct E { uint32_t a, v, fn, n; };   // n: valor al acabar el tick
  struct Api { uint32_t sz = 0; uint32_t fn, lr, out, value; uint64_t r[8], f[3], ret, fret; std::vector<uint64_t> outw; uint64_t ar[8] = {}, af1 = 0; uint32_t ah[8] = {}; };   // llamada al motor de sonido del tick
  uint32_t tick = 0xFFFFFFFFu, api_tick = 0xFFFFFFFFu;
  std::vector<E> e;
  std::vector<Api> api;
};
struct WLog {
  static bool On() { static const bool v = (std::getenv("DBZ3_RB_WLOG") || std::getenv("DBZ3_RB_ON") || rex::cvar::GetFlagByName("dbz3_net_rollback") == "true") && !dbz3::g_guest_eu; return v; }
  using Tick = WLogTick;
  static constexpr int kTicks = 16;
  static constexpr uint64_t kTop = 0xA0000000ull;   // por encima: espejos fisicos y GPU
  static inline std::array<Tick, kTicks> ring;
  static constexpr uint32_t kClock = 0x8238D6B4;   // solo US
  // Contador de altas del monton (solo lo toca sub_820A2550, nadie lo lee). Al resimular las altas reutilizadas del diario no lo suben
  // y las nuevas si: se deja como estaba antes de deshacer para que la suma del estado no dependa de cuantas se repitieron.
  static constexpr uint32_t kHeapCnt = 0x8238D6D8;
  static inline uint32_t heap_hold = 0;
  static inline std::array<uint32_t, kTicks> clock_v{}, clock_t_{}, cosm_v{};
  static inline uint32_t* cosm = nullptr;   // LFSR cosmetico de los efectos (g_cosm_rng, fuera de la memoria del invitado)
  static inline Tick* cur = nullptr;
  static inline uint8_t* mem = nullptr;
  static inline DWORD tid = 0;
  static inline uint32_t stk_lo = 0, stk_hi = 0;
  static inline uint64_t* seen = nullptr;               // 1 bit por palabra: ya apuntada en este tick
  static inline uint64_t* lw = nullptr;                 // 1 bit por palabra: la logica la ha escrito alguna vez (compartida con el dibujo)
  static inline std::atomic<uint64_t>* atom = nullptr;  // 1 bit por palabra: tocada con reserva atomica, nunca se deshace
  static inline int depth = 0, mute = 0;
  static inline bool replay = false;
  static inline uint32_t skip_node = 0, skip_cb = 0;
  static inline std::vector<std::pair<uint32_t, uint32_t>> ext;   // [desde, hasta) de funciones que no se registran
  static inline std::vector<uint32_t> keep;
  static inline std::vector<std::pair<uint32_t, uint32_t>> nolog;
  static inline std::vector<std::pair<uint32_t, uint32_t>> skip;    // funciones externas que no se ejecutan al resimular
  static inline uint64_t n_skipped = 0, n_dyn = 0, n_snd_kept = 0, n_snd_real = 0;
  // Pila (r1) al entrar en una funcion externa desde la logica: mientras r1 < ext_sp se esta dentro de ella. Asi las retrollamadas de
  // las librerias al codigo del juego (sub_82082D38 desde el sonido, que pide bloques al monton con sub_820A2550 y se los da al hilo
  // de audio) tampoco se rebobinan.
  static inline uint32_t ext_sp = 0, inv_fn = 0, inv_sp = 0;
  static inline bool no_journal = std::getenv("DBZ3_RB_WLOG_NOJOURNAL") != nullptr;
  static inline bool spying = false;
  static inline std::vector<uint32_t> watch;   // diagnostico (DBZ3_RB_WLOG_WATCHFN): quien llama a estas funciones dentro del registro   // direcciones que no se registran (DBZ3_RB_WLOG_NOLOG)                       // tareas de dibujo que SI se registran y resimulan
  static inline std::vector<WLogTick::Api> prev_api;   // llamadas del tick antes de resimularlo
  static inline std::vector<bool> prev_used;
  static inline bool in_api = false;
  static inline uint64_t n_api_real = 0, n_api_kept = 0;
  static inline uint64_t n_foreign = 0, n_ext = 0, n_stack = 0, n_top = 0, n_atom = 0, n_words = 0;

  static void Ranges(const char* env, std::vector<std::pair<uint32_t, uint32_t>>& out) {
    for (const char* p = env; p && *p;) {
      char* e = nullptr;
      const uint32_t a = uint32_t(std::strtoul(p, &e, 16));
      if (*e != '-') break;
      const uint32_t b = uint32_t(std::strtoul(e + 1, &e, 16));
      out.push_back({a, b});
      p = *e == ',' ? e + 1 : e;
    }
  }
  static bool Init(uint8_t* base, uint32_t* cosm_rng) {
    cosm = cosm_rng;
    if (!On()) return false;
    if (!seen) {
      const size_t bytes = size_t(kTop / 4 / 8);
      seen = static_cast<uint64_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      lw = static_cast<uint64_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      atom = static_cast<std::atomic<uint64_t>*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      if (!seen || !atom || !lw) { REXLOG_ERROR("dbz3 rb: WLOG sin memoria para los mapas de bits"); return false; }
      ext.clear();
      // Solo lo que de verdad comparte estado con otros hilos (soltar referencia 82080750 no: es logica y su CAS se registra):
      // archivos cargados y su preparacion 82080000-82082D00 (sub_821349F0 ->
      // 820800A8 reubica punteros del archivo recien cargado y crea sus objetos con referencias; deshacerla y repetirla destruia objetos
      // vivos: purecall/abort, punteros nulos) y el cargador 82082D00-82084A00, sonido, monton y recursos
      // 820A2000-820AB000 y el monton del juego 82089B60-8208A0A0 (alta 82089F58 / baja 82089C10: tambien lo usa el cargador; deshacer
      // sus listas descuadraba bloques de 127 KB y acababa sin memoria -> memset sobre nulo en sub_821388D0) (las matematicas 820AB000+ escriben salidas en memoria de la logica: se registran), D3D/Xam/CRT.
      // La camara 820823B8-82082A80 es calculo de la logica, no libreria: con el resultado grabado al resimular los dos PCs se separaban.
      // Igual el seno y coseno 82084988/820849E0 (salidas en memoria): 400 llamadas por tanda con el resultado de otro angulo.
      Ranges(std::getenv("DBZ3_RB_WLOG_EXT") ? std::getenv("DBZ3_RB_WLOG_EXT") : "82080000-82080750,820807F8-820823B8,82082A80-82084988,82089B60-8208A0A0,8209D7E8-8209EA00,820A2000-820AB000,821DC000-8221C000,8221C000-82283000,82283000-822F4000", ext);
      nolog.clear();
      Ranges(std::getenv("DBZ3_RB_WLOG_NOLOG"), nolog);
      draw_mem.clear();
      Ranges(std::getenv("DBZ3_RB_WLOG_DRAWMEM"), draw_mem);
      watch.clear();
      for (const char* p = std::getenv("DBZ3_RB_WLOG_WATCHFN"); p && *p;) { char* e = nullptr; watch.push_back(uint32_t(std::strtoul(p, &e, 16))); if (e == p) break; p = *e == ',' ? e + 1 : e; }
      skip.clear();
      Ranges(std::getenv("DBZ3_RB_WLOG_SKIP"), skip);   // omitir el gestor de sonido dejaba el combate sin terminar (la logica espera sus respuestas)
      keep.clear();
      const char* k = std::getenv("DBZ3_RB_WLOG_KEEP");
      for (const char* p = k ? k : "820FC3D0"; *p;) {
        char* e = nullptr;
        const uint32_t cb = uint32_t(std::strtoul(p, &e, 16));
        if (e == p) break;
        keep.push_back(cb);
        p = *e == ',' ? e + 1 : e;
      }
    }
    mem = base;
    tid = GetCurrentThreadId();
    if (auto* ts = rex::runtime::ThreadState::Get()) {   // limites de la pila del hilo (X_KPCR +74/+70, como rb_snapshot.h)
      const uint32_t pcr = ts->context()->r13.u32;
      stk_lo = REX_LOAD_U32(pcr + 0x74);
      stk_hi = REX_LOAD_U32(pcr + 0x70);
    }
    if (!(stk_lo < stk_hi && stk_hi - stk_lo <= 16u * 1024 * 1024)) { REXLOG_ERROR("dbz3 rb: WLOG sin limites de pila"); return false; }
    frees.clear();   // de una partida cortada a medias: mejor perderlas que liberar sobre otro combate
    for (auto& r : ring) { r.tick = 0xFFFFFFFFu; r.e.clear(); }
    cur = nullptr; depth = mute = 0; replay = false;
    rbw::g_mem = base;
    rbw::g_fn = &Store;
    rbw::g_enter = &Enter;
    rbw::g_cas = &Cas;
    rbw::g_cas_on = 1;
    REXLOG_INFO("dbz3 rb: WLOG en marcha: pila {:08X}-{:08X}, {} rangos de libreria, {} tareas de dibujo registradas", stk_lo, stk_hi, ext.size(), keep.size());
    return true;
  }
  static void Release() {
    End();
    rbw::g_cas_on = 0;
  }
  static void Store(uint32_t fn, uint32_t ea, uint32_t n, uint32_t sp, uint32_t lr) {
    static const char* wa = std::getenv("DBZ3_RB_WLOG_WATCHA");   // direccion o lo-hi
    static const uint32_t watcha = wa ? uint32_t(std::strtoul(wa, nullptr, 16)) : 0, watchb = wa && std::strchr(wa, '-') ? uint32_t(std::strtoul(std::strchr(wa, '-') + 1, nullptr, 16)) : watcha + 1;
    if (watcha && ea < watchb && watcha < ea + n) {   // diagnostico: quien escribe esa palabra mientras se registra
      static int shown = 0;
      uint32_t v = 0; if (mem) { std::memcpy(&v, mem + watcha, 4); v = _byteswap_ulong(v); }
      if (shown++ < 5000) REXLOG_INFO("dbz3 rb: WLOG escribe {:08X} ({:08X}+{}): tick {} replay {} mute {} cb {:08X} fn {:08X} lr {:08X} ext_sp {:08X} hilo {} antes {:08X}", ea, watcha, n, cur ? cur->tick : 0, replay, mute, draw_cb, fn, lr, ext_sp, GetCurrentThreadId() == tid, v);
    }
    static const char* wr = std::getenv("DBZ3_RB_WLOG_WATCHR");   // diagnostico: escrituras NO registradas en lo..hi (quien cambia palabras "ajenas")
    if (wr) {
      static const uint32_t lo = uint32_t(std::strtoul(wr, nullptr, 16)), hi = uint32_t(std::strtoul(std::strchr(wr, '-') ? std::strchr(wr, '-') + 1 : wr, nullptr, 16));
      if (ea < hi && ea + n > lo && (!cur || GetCurrentThreadId() != tid || ext_sp || mute)) {
        static int shown = 0;
        if (shown++ < 400) REXLOG_INFO("dbz3 rb: WLOG sin registro {:08X}: tick {} cur {} replay {} mute {} cb {:08X} fn {:08X} lr {:08X} ext_sp {:08X} hilo {}", ea, cur ? cur->tick : 0, cur != nullptr, replay, mute, draw_cb, fn, lr, ext_sp, GetCurrentThreadId() == tid);
      }
    }
    if (!cur || GetCurrentThreadId() != tid) return;
    if (ea < 0x10000u) {   // puntero nulo: la pagina 0 no esta mapeada; se informa y la escritura del juego fallara con su contexto
      static int shown = 0;
      if (shown++ < 8) { REXLOG_ERROR("dbz3 rb: WLOG escritura en {:08X}: fn {:08X} lr {:08X} tick {} replay {} mute {} cb {:08X} ext_sp {:08X}", ea, fn, lr, cur->tick, replay, mute, draw_cb, ext_sp); rex::FlushLogging(); }
      return;
    }
    if (rec && !mute) RecOut(ea, n);
    if (ea - stk_lo < stk_hi - stk_lo) { ++n_stack; return; }   // lo mas frecuente: 7 de cada 10 escrituras
    // Rutinas comunes de la CRT (memcpy 822F56C0, memset...): cuenta quien las llama (lr: son hojas), no ellas.
    if (fn >= 0x822F4000u && fn < 0x82310000u && lr) fn = lr;
    for (const auto& r : ext) if (fn >= r.first && fn < r.second) { ++n_ext; return; }
    if (ext_sp && sp != 0xFFFFFFFFu) {
      if (sp < ext_sp) { ++n_dyn; return; }
      ext_sp = 0;
    }
    for (const auto& r : nolog) if (ea >= r.first && ea < r.second) { ++n_ext; return; }
    if (fn == 0x820927E0u && TaskStat(ea)) return;
    const uint64_t end = uint64_t(ea) + n;
    if (end > kTop || !n) { ++n_top; return; }
    // Tarea de dibujo al avanzar: solo cuenta lo que comparte con la logica (palabras que la logica tambien escribe: lista de efectos
    // que AR/SP/HM DRAW matan con cb = -1, pila de matrices 823959A0...). Eso se deshace y se graba su valor final para reponerlo al
    // resimular (la tarea no se ejecuta). El resto (memoria de render) ni se deshace ni se repone: reponerlo rompia el D3D.
    for (uint32_t w = ea >> 2, last = uint32_t((end - 1) >> 2); w <= last; ++w) {
      const uint64_t bit = 1ull << (w & 63);
      if (mute) {
        const bool shared = (lw[w >> 6] & bit) != 0;
        if (!shared && !InDrawMem(w << 2)) continue;
        draw_out[cur->tick % kTicks].push_back({draw_cb, w << 2, 0});
        if (!shared) continue;
      } else {
        lw[w >> 6] |= bit;
      }
      if (seen[w >> 6] & bit) continue;
      if (atom[w >> 6].load(std::memory_order_relaxed) & bit) { ++n_atom; continue; }
      seen[w >> 6] |= bit;
      uint32_t v;
      std::memcpy(&v, mem + (uint64_t(w) << 2), 4);
      cur->e.push_back({w << 2, v, fn | uint32_t(spying), 0});
    }
  }
  // Salidas de una llamada externa del diario: lo que escribe en los marcos del llamante (parametros de salida: sub_82080DA8 recibe en
  // r1+80 el objeto que crea la fabrica 820A7480). Al resimular la llamada no se repite y devolver solo r3/f1 dejaba esas salidas con
  // el valor de antes (puntero basura -> caida). Se graban al avanzar y se reponen. Fuera de la pila, solo palabras que la logica ya
  // escribio en ESTE tick (seen: ya tienen entrada para deshacer), como el objeto de 52 B de sub_82081300 que sub_820A2FD8 rellena.
  // Usar lw ("alguna vez") deshacia estado interno de las librerias en bloques del monton reutilizados y el sonido (sub_82084210 ->
  // 821DC898) caia al segundo combate.
  static inline bool rec = false;
  static inline uint32_t rec_sp = 0;
  static inline std::vector<uint32_t> rec_w;
  static void RecOut(uint32_t ea, uint32_t n) {
    if (!n || uint64_t(ea) + n > kTop) return;   // fisica (texturas, buffers de la GPU): fuera de los mapas de bits
    for (uint32_t w = ea >> 2, last = uint32_t((uint64_t(ea) + n - 1) >> 2); w <= last; ++w) {
      const uint32_t a = w << 2;
      const bool stk = a - stk_lo < stk_hi - stk_lo;
      if (stk ? a >= rec_sp : (seen[w >> 6] >> (w & 63)) & 1) rec_w.push_back(a);
    }
  }
  // Estadisticas de tiempo que el despachador escribe en cada nodo (+payload+72/+76: reloj de sub_822F6A00 antes/despues de la tarea).
  // Son medidas del reloj real, distintas en cada pasada; nadie de la logica las lee.
  static bool TaskStat(uint32_t ea) {
    uint32_t payload, pool;
    std::memcpy(&payload, mem + 0x8238CDE0u, 4); payload = _byteswap_ulong(payload);
    std::memcpy(&pool, mem + 0x8238CDE8u, 4); pool = _byteswap_ulong(pool);
    if (ea < pool || payload > 4096) return false;
    const uint32_t off = (ea - pool) % (payload + 80);
    return off == payload + 72 || off == payload + 76;
  }
  static bool Ext(uint32_t a) { for (const auto& r : ext) if (a >= r.first && a < r.second) return true; return false; }
  // Diagnostico: llamadas de la logica registrada a funciones de libreria (frontera), por separado en forward y al resimular.
  static inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> cross[2];
  // Capa de sonido del juego (gestor S = *(82386260)+808, ranuras de 9320 B con las voces: sub_8209E008 toca, 8209DE30 encola).
  // No se rebobina (esta en ext) y al resimular no se vuelve a llamar: la logica entra por envoltorios 820922A0..820925E0 que
  // devuelven 0 siempre y no leen nada de ella. Asi una voz ya parada no se vuelve a parar (doble liberacion en sub_822CAA08).
  static bool Enter(uint32_t fn, PPCContext& c) {
    const uint32_t lr = uint32_t(c.lr);
    if (!cur || GetCurrentThreadId() != tid) return false;
    if (inv_fn && fn == inv_fn && c.r1.u32 == inv_sp) { inv_fn = 0; ext_sp = c.r1.u32; return false; }   // la ejecucion de Invoke
    if (!fn) return false;   // __savegprlr/__restgprlr: entran con r1 == ext_sp antes del stwu de la funcion externa
    if (ext_sp && c.r1.u32 >= ext_sp) ext_sp = 0;   // ya volvio de la llamada externa
    if (!ext_sp && fn && !watch.empty() && std::find(watch.begin(), watch.end(), fn) != watch.end()) {
      ++cross[replay ? 1 : 0][{fn, lr}];
      static int shown = 0;
      if (shown++ < 4) {   // cadena de retorno por la pila (cadena r1 -> [r1], LR guardado en -8 del marco del llamante)
        std::string txt;
        uint32_t sp = c.r1.u32;
        for (int i = 0; i < 30 && sp >= stk_lo && sp < stk_hi; ++i) {
          uint32_t prev; std::memcpy(&prev, mem + sp, 4); prev = _byteswap_ulong(prev);
          if (prev <= sp || prev >= stk_hi) break;
          uint32_t l; std::memcpy(&l, mem + prev - 8, 4); l = _byteswap_ulong(l);
          char b[16]; std::snprintf(b, sizeof b, " %08X", l); txt += b;
          sp = prev;
        }
        REXLOG_INFO("dbz3 rb: WLOG vigilada {:08X} lr {:08X} r1 {:08X} ext_sp {:08X} mute {} cadena:{}", fn, lr, c.r1.u32, ext_sp, mute, txt);
      }
    }
    if (ext_sp || !fn || !Ext(fn) || Ext(lr) || (lr >= 0x822F4000u && lr < 0x82310000u)) return false;
    ++cross[replay ? 1 : 0][{fn, lr}];
    ext_sp = c.r1.u32;   // todo lo que corra por debajo de esta pila es la llamada externa (incluidas las retrollamadas al juego)
    if (replay) for (const auto& r : skip) if (fn >= r.first && fn < r.second) { c.r3.u64 = 0; ++n_skipped; ext_sp = 0; return true; }
    // Diario de las llamadas de la logica a librerias (sonido, gestor de sonido 8209D7E8.., Xam, ADX...). Al avanzar, la llamada se
    // ejecuta aqui mismo (Invoke) para grabar r3/f1 al volver; al resimular, la misma llamada (sitio y orden en el tick) no se repite y
    // devuelve lo grabado: lo que escribio la libreria no se deshizo. Repetirlas reiniciaba voces y cargas (el locutor del final, las
    // escenas con fundido) y el combate se quedaba esperando. Una llamada nueva (prediccion corregida) se ejecuta y se graba.
    if (mute || no_journal || fn == 0x8221C178u) return false;   // XamInputGetState: al resimular el mando lo pone el gancho
    // Monton del juego. Una baja no se hace hasta que su tick ya no se puede deshacer: si no, el bloque se reutilizaba, el monton dejaba
    // su cabecera dentro y deshacer las palabras antiguas de la logica la pisaba (alta nula y escritura en 0x6C en sub_820F7A78).
    if (fn == kFree) {
      frees.push_back({cur->tick, c.r3.u32});
      ext_sp = 0;
      return true;
    }
    if (replay)
      for (size_t i = 0; i < prev_api.size(); ++i) {
        const auto& p = prev_api[i];
        if (prev_used[i] || p.fn != fn || p.lr != lr) continue;
        if (fn == kAlloc && c.r3.u32 > p.sz) continue;   // la prediccion corregida pide mas que el bloque grabado: alta nueva
        prev_used[i] = true;
        if (args_on) {
          uint64_t r[8]; uint32_t h[8];
          ArgGrab(c, r, h);
          auto& st = arg_stat[{fn, lr}];
          ++st.kept;
          st.reg += std::memcmp(r, p.ar, sizeof r) != 0 || c.f1.u64 != p.af1;
          st.memo += std::memcmp(h, p.ah, sizeof h) != 0;
          st.out += !p.outw.empty();
        }
        cur->api.push_back(p);
        c.r3.u64 = p.ret; c.f1.u64 = p.fret;
        for (const uint64_t o : p.outw) {
          const uint32_t a = uint32_t(o >> 32), v = uint32_t(o);
          std::memcpy(mem + a, &v, 4);
        }
        ++n_snd_kept;
        ext_sp = 0;
        return true;
      }
    WLogTick::Api a{};
    a.fn = fn; a.lr = lr;
    if (fn == kAlloc) a.sz = c.r3.u32;
    if (args_on) { ArgGrab(c, a.ar, a.ah); a.af1 = c.f1.u64; }
    inv_fn = fn; inv_sp = c.r1.u32;
    rec = true; rec_sp = c.r1.u32; rec_w.clear();
    rbw::Invoke(fn, c);   // vuelve a entrar en esta misma funcion: Enter la deja pasar (inv_fn/inv_sp)
    rec = false;
    inv_fn = 0;
    a.ret = c.r3.u64; a.fret = c.f1.u64;
    std::sort(rec_w.begin(), rec_w.end());
    rec_w.erase(std::unique(rec_w.begin(), rec_w.end()), rec_w.end());
    for (const uint32_t w : rec_w) { uint32_t v; std::memcpy(&v, mem + w, 4); a.outw.push_back(uint64_t(w) << 32 | v); }
    if (cur) cur->api.push_back(a);
    n_snd_real += replay;
    if (replay) ++api_new[{fn, lr}];
    ext_sp = 0;
    return true;
  }
  // Diagnostico (DBZ3_RB_WLOG_ARGS): llamadas del diario que al resimular llegan con otros argumentos (registros o 64 B apuntados):
  // su resultado grabado ya no vale.
  static inline const bool args_on = std::getenv("DBZ3_RB_WLOG_ARGS") != nullptr;
  struct ArgStat { uint32_t kept = 0, reg = 0, memo = 0, out = 0; };
  static inline std::map<std::pair<uint32_t, uint32_t>, ArgStat> arg_stat;
  static void ArgGrab(const PPCContext& c, uint64_t* r, uint32_t* h) {
    const uint64_t v[8] = {c.r3.u64, c.r4.u64, c.r5.u64, c.r6.u64, c.r7.u64, c.r8.u64, c.r9.u64, c.r10.u64};
    for (int i = 0; i < 8; ++i) {
      r[i] = v[i];
      h[i] = 0;
      const uint32_t a = uint32_t(v[i]);
      if (a < 0x40000000u || uint64_t(a) + 64 > kTop || (a - stk_lo < stk_hi - stk_lo && a < c.r1.u32)) continue;
      MEMORY_BASIC_INFORMATION mi{};
      if (!VirtualQuery(mem + a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
      if (static_cast<const uint8_t*>(mi.BaseAddress) + mi.RegionSize < mem + a + 64) continue;
      uint32_t x = 2166136261u;
      for (int k = 0; k < 64; ++k) x = (x ^ mem[a + k]) * 16777619u;
      h[i] = x;
    }
  }
  static constexpr uint32_t kAlloc = 0x82089F58u, kFree = 0x82089C10u;
  struct Free { uint32_t tick, p; };
  static inline std::vector<Free> frees;
  // Hace las bajas de los ticks <= hasta. Solo al empezar un tick nuevo y en el orden de los ticks: el monton da las mismas direcciones
  // en los dos PCs, tengan los rollbacks que tengan (las direcciones se guardan en el luchador y entran en las sumas).
  static void FlushFrees(PPCContext& c, uint32_t hasta, bool todo = false) {
    size_t n = 0;
    for (const Free& f : frees) {
      if (!todo && f.tick > hasta) { frees[n++] = f; continue; }
      c.r3.u64 = f.p;
      inv_fn = kFree; inv_sp = c.r1.u32;
      rbw::Invoke(kFree, c);
      inv_fn = 0;
    }
    frees.resize(n);
    ext_sp = 0;
  }
  static inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> api_new, api_unused;   // diario: nuevas al resimular / grabadas que no se repitieron
  static void Cross() {
    for (int r = 0; r < 2; ++r) {
      std::string txt;
      for (const auto& [k, n] : cross[r]) { char b[40]; std::snprintf(b, sizeof b, " %08X<%08X:%u", k.first, k.second, n); txt += b; }
      REXLOG_INFO("dbz3 rb: WLOG frontera {}:{}", r ? "resimulando" : "forward", txt);
    }
  }
  static void Cas(const void* host, uint32_t n) {
    const uint64_t d = uint64_t(static_cast<const uint8_t*>(host) - mem);
    if (!mem || d + n > kTop) return;
    // Contadores de referencias que sube/baja la logica en su hilo (sub_82080750): se deshacen como cualquier palabra. Si no, al
    // resimular la baja se repetia sin deshacer la anterior y los destructores liberaban dos veces (monton sub_82089D70 agotado ->
    // memset sobre nulo en sub_821388D0). Los de otros hilos o dentro de una llamada externa siguen fuera.
    if (cur && rbw::g_on && !mute && !ext_sp && GetCurrentThreadId() == tid) {
      for (uint64_t w = d >> 2; w <= (d + n - 1) >> 2; ++w) {
        const uint64_t bit = 1ull << (w & 63);
        lw[w >> 6] |= bit;
        if (seen[w >> 6] & bit) continue;
        seen[w >> 6] |= bit;
        uint32_t v;
        std::memcpy(&v, mem + (w << 2), 4);
        cur->e.push_back({uint32_t(w << 2), v, 0x80000000u, 0});
      }
      return;
    }
    for (uint64_t w = d >> 2; w <= (d + n - 1) >> 2; ++w) atom[w >> 6].fetch_or(1ull << (w & 63), std::memory_order_relaxed);
  }
  static void Sync() { rbw::g_on = (cur && depth > 0 && (!mute || (mute && !replay))) ? 1u : 0u; }
  struct Scope {
    Scope() { ++depth; Sync(); }
    ~Scope() { --depth; Sync(); }
  };
  // Abre el registro del tick k (forward o resimulado). Lo cierra End (al empezar el tick siguiente o tras resimularlo).
  static void Begin(uint32_t k, bool resim) {
    if (!seen || !mem) return;
    End();
    cur = &ring[k % kTicks];
    cur->tick = k;
    if (!resim) { std::memcpy(&clock_v[k % kTicks], mem + kClock, 4); clock_t_[k % kTicks] = k; if (cosm) cosm_v[k % kTicks] = *cosm; }
    cur->e.clear();
    prev_api.clear();
    if (resim && cur->api_tick == k) prev_api.swap(cur->api);
    prev_used.assign(prev_api.size(), false);
    if (resim) frees.erase(std::remove_if(frees.begin(), frees.end(), [k](const Free& f) { return f.tick >= k; }), frees.end());   // se repetiran
    cur->api.clear();
    cur->api_tick = k;
    replay = resim;
    depth = mute = 0;
    Sync();
  }
  static void End() {
    if (cur) {
      if (replay) std::memcpy(mem + kHeapCnt, &heap_hold, 4);
      if (replay) for (size_t i = 0; i < prev_api.size(); ++i) if (!prev_used[i]) {
        ++api_unused[{prev_api[i].fn, prev_api[i].lr}];
        if (prev_api[i].fn == kAlloc) frees.push_back({cur->tick, uint32_t(prev_api[i].ret)});   // nadie lo usa ya: la logica se deshizo
      }
      for (auto& e : cur->e) { seen[e.a >> 8] &= ~(1ull << ((e.a >> 2) & 63)); std::memcpy(&e.n, mem + e.a, 4); }
      static const uint32_t every = std::getenv("DBZ3_RB_WLOG_TRACE") ? uint32_t(std::atoi(std::getenv("DBZ3_RB_WLOG_TRACE"))) : 0;
      if (every && !replay && cur->tick % every == 0) {   // diagnostico: palabras cambiadas por la logica en este tick
        static std::ofstream o(OutDir() / ("wtrace_" + std::to_string(GetCurrentProcessId()) + ".csv"));
        char l[64];
        for (const auto& e : cur->e)
          if (e.v != e.n) { std::snprintf(l, sizeof l, "%u,%08X,%08X,%08X,%08X%c", cur->tick, e.a, e.fn, _byteswap_ulong(e.v), _byteswap_ulong(e.n), '\n'); o << l; }
      }
      n_words += cur->e.size();
      Diff();
    }
    cur = nullptr;
    depth = mute = 0;
    replay = false;
    ext_sp = 0;
    Sync();
  }
  // Diagnostico (DBZ3_RB_WLOG_DIFF): compara lo que escribe la logica al resimular un tick con lo que escribio en el avance. Valor
  // anterior distinto = estado de partida no repuesto; mismo anterior y final distinto = la logica leyo algo distinto (p.ej. del dibujo).
  static inline std::array<std::vector<WLogTick::E>, kTicks> fwd;
  static inline std::array<uint32_t, kTicks> fwd_tick{};
  static void Diff() {
    static const bool on = std::getenv("DBZ3_RB_WLOG_DIFF") != nullptr;
    if (!on) return;
    const uint32_t k = cur->tick, i = k % kTicks;
    if (!replay) { fwd[i] = cur->e; fwd_tick[i] = k; return; }
    if (fwd_tick[i] != k) return;
    static int shown = 0;
    if (shown >= 400) return;
    std::unordered_map<uint32_t, const WLogTick::E*> f;
    for (const auto& e : fwd[i]) f[e.a] = &e;
    int nv = 0, nn = 0, nnew = 0;
    std::string txt;
    for (const auto& e : cur->e) {
      auto it = f.find(e.a);
      const char* why = nullptr;
      if (it == f.end()) { if (e.v == e.n) continue; why = "nueva"; ++nnew; }
      else if (it->second->v != e.v) { why = "antes"; ++nv; }
      else if (it->second->n != e.n) { why = "final"; ++nn; }
      if (why) {
        static std::ofstream o(OutDir() / ("wdiff_" + std::to_string(GetCurrentProcessId()) + ".csv"));
        char l[96]; std::snprintf(l, sizeof l, "%u,%s,%08X,%08X,%08X,%08X%c", k, why, e.a, e.fn, _byteswap_ulong(it == f.end() ? e.v : it->second->n), _byteswap_ulong(e.n), 10); o << l;
      }
      if (why && txt.size() < 900) {
        char b[96];
        std::snprintf(b, sizeof b, " %s:%08X fn %08X %08X/%08X", why, e.a, e.fn, _byteswap_ulong(it == f.end() ? e.v : it->second->n), _byteswap_ulong(e.n));
        txt += b;
      }
      if (it != f.end()) f.erase(it);
    }
    int nmiss = 0;
    for (const auto& [a, e] : f) if (e->v != e->n) { { static std::ofstream o(OutDir() / ("wdiff_" + std::to_string(GetCurrentProcessId()) + "_falta.csv")); char l[64]; std::snprintf(l, sizeof l, "%u,%08X,%08X%c", k, a, e->fn, 10); o << l; } if (txt.size() < 1200) { char b[64]; std::snprintf(b, sizeof b, " falta:%08X fn %08X", a, e->fn); txt += b; } ++nmiss; }
    if (nv + nn + nnew + nmiss) { ++shown; REXLOG_WARN("dbz3 rb: WLOG DIFF tick {}: antes {} final {} nuevas {} faltan {}:{}", k, nv, nn, nnew, nmiss, txt); }
  }
  static bool Has(uint32_t u, uint32_t t) {
    for (uint32_t k = u; k < t; ++k) if (ring[k % kTicks].tick != k) return false;
    return u < t && t - u < kTicks;
  }
  // Deshace t-1..u. Despues la memoria registrada es la del principio de u.
  static bool Restore(uint32_t u, uint32_t t) {
    End();
    if (!Has(u, t)) return false;
    // Contador de ticks que sube el gestor de sonido (externo, no registrado) en sub_8209DA08 y que la logica usa como reloj: vuelve al
    // valor del principio de u; ResimTick lo sube uno por tick resimulado. Sin esto el reloj del combate se paraba al resimular.
    // Igual el LFSR cosmetico: si no, al resimular los efectos salen con otros aleatorios, viven otro tiempo y la lista de efectos
    // (823C76D0, recorrida por sub_820CF8D0) acaba con nodos ya liberados.
    std::memcpy(&heap_hold, mem + kHeapCnt, 4);
    if (clock_t_[u % kTicks] == u) { std::memcpy(mem + kClock, &clock_v[u % kTicks], 4); if (cosm) *cosm = cosm_v[u % kTicks]; }
    static int dumps = 0;   // diagnostico: que funcion escribio cada palabra deshecha (primeras restauraciones)
    if (dumps < 3 && std::getenv("DBZ3_RB_WLOG_DUMP")) {
      ++dumps;
      std::ofstream o(OutDir() / ("wlog_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(u) + ".csv"));
      o << "tick,addr,fn,old,new" << '\n';
      for (uint32_t k = u; k < t; ++k)
        for (const auto& e : ring[k % kTicks].e) {
          uint32_t now; std::memcpy(&now, mem + e.a, 4);
          char l[80]; std::snprintf(l, sizeof l, "%u,%08X,%08X,%08X,%08X%c", k, e.a, e.fn, _byteswap_ulong(e.v), _byteswap_ulong(now), '\n'); o << l;
        }
    }
    // Conflictos: palabra registrada que, despues de su ultimo tick, cambio alguien que no es la logica registrada (otro hilo, librerias,
    // dibujo). Deshacerla pisaria a ese otro dueno: no se deshace y se informa (funcion que la escribio).
    std::unordered_map<uint32_t, uint32_t> last;   // palabra -> valor al acabar su ultimo tick
    for (uint32_t k = u; k < t; ++k) for (const auto& e : ring[k % kTicks].e) last[e.a] = e.n;
    std::unordered_set<uint32_t> foreign;
    for (const auto& [a, n] : last) { uint32_t now; std::memcpy(&now, mem + a, 4); if (now != n) foreign.insert(a); }
    if (!foreign.empty()) {
      n_foreign += foreign.size();
      static int shown = 0;
      if (shown < 30) {
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> byfn;   // fn -> (cuantas, primera direccion)
        for (uint32_t k = u; k < t; ++k) for (const auto& e : ring[k % kTicks].e) if (foreign.count(e.a)) { auto& b = byfn[e.fn]; if (!b.first++) b.second = e.a; }
        for (const auto& [fn, b] : byfn) if (shown++ < 30) REXLOG_WARN("dbz3 rb: WLOG ajena tras {}: fn {:08X} {} palabras (p.ej. {:08X})", u, fn, b.first, b.second);
      }
    }
    for (uint32_t k = t; k-- > u;) {
      auto& tk = ring[k % kTicks];
      for (size_t i = tk.e.size(); i-- > 0;) if (!foreign.count(tk.e[i].a)) std::memcpy(mem + tk.e[i].a, &tk.e[i].v, 4);
      tk.tick = 0xFFFFFFFFu;
    }
    return true;
  }
  // Despachador (gancho de sub_822F6A00): lr 82092814 justo antes de leer la callback del nodo r30, 82092858 justo despues.
  static inline uint32_t draw_cb = 0;   // tarea de dibujo en curso (silenciada)
  // Salida de las tareas de dibujo: la logica del tick siguiente lee parte de lo que calculan (sub_82101520 desde [PLY] DRAW escribe
  // estado que lee [PLY] PRE EXEC; matrices de huesos). Al avanzar se graba, por tick y tarea, el valor final de cada palabra que
  // escriben (fuera de librerias, pila y GPU); al resimular, la tarea no se ejecuta y se reponen esos valores. Con los mismos mandos
  // es exacto; con mandos corregidos repone lo del avance (cosmetico salvo que la logica dependa de ello: lo mide el oraculo).
  struct DrawOut { uint32_t cb, a, v; };
  static inline std::array<std::vector<DrawOut>, kTicks> draw_out;
  static inline std::array<uint32_t, kTicks> draw_tick{};
  static inline size_t draw_from = 0;
  static inline std::vector<std::pair<uint32_t, uint32_t>> draw_mem;   // donde se repone (vacio = nada; la memoria de render rompia el D3D)
  static bool InDrawMem(uint32_t a) { for (const auto& r : draw_mem) if (a >= r.first && a < r.second) return true; return false; }
  // Diagnostico (DBZ3_RB_WLOG_PRECHK=cb): al entrar en esa tarea, compara al resimular las palabras que escribio el dibujo con las del avance.
  static inline std::array<std::vector<uint32_t>, kTicks> pre_v;   // inicio de las palabras de la tarea en curso
  static void Task(uint32_t lr, uint32_t node, PPCContext& c, uint8_t* base) {
    if (!cur) return;
    if (lr == 0x82092814) {
      uint32_t cb, prio;
      std::memcpy(&cb, mem + node + 28, 4); cb = _byteswap_ulong(cb);
      std::memcpy(&prio, mem + node + 24, 4); prio = _byteswap_ulong(prio);
      static const uint32_t prechk = std::getenv("DBZ3_RB_WLOG_PRECHK") ? uint32_t(std::strtoul(std::getenv("DBZ3_RB_WLOG_PRECHK"), nullptr, 16)) : 0;
      if (cb == prechk && draw_tick[cur->tick % kTicks] == cur->tick) {
        auto& d = draw_out[cur->tick % kTicks];
        auto& pv = pre_v[cur->tick % kTicks];
        if (!replay) { pv.resize(d.size()); for (size_t i = 0; i < d.size(); ++i) std::memcpy(&pv[i], mem + d[i].a, 4); }
        else if (pv.size() == d.size()) {
          static int shown = 0;
          std::map<uint32_t, std::pair<uint32_t, uint32_t>> bycb;
          for (size_t i = 0; i < d.size(); ++i) { uint32_t now; std::memcpy(&now, mem + d[i].a, 4); if (now != pv[i]) { auto& b = bycb[d[i].cb]; if (!b.first++) b.second = d[i].a; } }
          for (const auto& [c2, b] : bycb) if (shown++ < 40) REXLOG_WARN("dbz3 rb: WLOG antes de {:08X} tick {}: dibujo {:08X} deja {} palabras distintas (p.ej. {:08X})", cb, cur->tick, c2, b.first, b.second);
        }
      }
      static const bool all = std::getenv("DBZ3_RB_WLOG_KEEP") && std::strcmp(std::getenv("DBZ3_RB_WLOG_KEEP"), "all") == 0;
      const bool draw = !all && prio < 0x4000 && cb != 0 && cb != 0xFFFFFFFFu && std::find(keep.begin(), keep.end(), cb) == keep.end();
      if (!draw) return;
      static const uint32_t spy = std::getenv("DBZ3_RB_WLOG_SPY") ? uint32_t(std::strtoul(std::getenv("DBZ3_RB_WLOG_SPY"), nullptr, 16)) : 0;
      if (!replay && cb == spy) { spying = true; return; }   // diagnostico: se registra (fn | 1) para ver que escribe esta tarea de dibujo
      mute = 1;
      draw_cb = cb;
      if (replay) {   // al resimular, el nodo se salta: callback 0 hasta que vuelve el reloj (escritura directa, sin registrar)
        skip_node = node; skip_cb = cb;
        const uint32_t z = 0;
        std::memcpy(mem + node + 28, &z, 4);
        if (draw_tick[cur->tick % kTicks] == cur->tick)   // repone lo que dejo esta tarea en el avance (registrado: se deshace)
          for (const auto& d : draw_out[cur->tick % kTicks])
            if (d.cb == cb) { mute = 0; Store(0, d.a, 4, 0xFFFFFFFFu, 0); mute = 1; const uint32_t v = d.v; std::memcpy(mem + d.a, &v, 4); }
      } else {
        auto& d = draw_out[cur->tick % kTicks];
        if (draw_tick[cur->tick % kTicks] != cur->tick) { d.clear(); draw_tick[cur->tick % kTicks] = cur->tick; }
        draw_from = d.size();
      }
      Sync();
    } else if (lr == 0x82092858 && spying) {
      spying = false;
    } else if (lr == 0x82092858 && mute) {
      if (!replay) {   // valor final de lo que escribio la tarea
        auto& d = draw_out[cur->tick % kTicks];
        for (size_t i = draw_from; i < d.size(); ++i) std::memcpy(&d[i].v, mem + d[i].a, 4);
      }
      if (skip_node) {
        const uint32_t v = _byteswap_ulong(skip_cb);
        std::memcpy(mem + skip_node + 28, &v, 4);
        skip_node = 0;
      }
      mute = 0;
      draw_cb = 0;
      Sync();
    }
  }
  // APIs del motor de sonido (ganchos RB_AUDIO, firmas en EVIDENCIA_4.md de sol-R4). Al resimular, una llamada que el tick ya hizo
  // con los mismos argumentos NO se repite: devuelve r3/f1 y la salida grabados (repetir "parar"/"soltar" liberaba dos veces el
  // mismo nodo y tiraba el asignador del sonido, sub_822CAA08, en el hilo de audio). Una llamada nueva (prediccion corregida) va al motor.
  static bool Audio(uint32_t fn, PPCContext& c, uint8_t* base, void (*orig)(PPCContext&, uint8_t*), uint32_t gp, uint32_t fp, uint32_t out_arg) {
    if (!cur || in_api || GetCurrentThreadId() != tid || !depth || mute) return false;
    WLogTick::Api a{};
    a.fn = fn; a.lr = uint32_t(c.lr);
    a.out = out_arg == 4 ? c.r4.u32 : out_arg == 8 ? c.r8.u32 : 0;
    const uint64_t r[8] = {c.r3.u64, c.r4.u64, c.r5.u64, c.r6.u64, c.r7.u64, c.r8.u64, c.r9.u64, c.r10.u64};
    const uint64_t f[3] = {c.f0.u64, c.f1.u64, c.f2.u64};
    for (int i = 0; i < 8; ++i) a.r[i] = (gp >> i & 1) ? r[i] : 0;
    for (int i = 0; i < 3; ++i) a.f[i] = (fp >> i & 1) ? f[i] : 0;
    if (replay)
      for (size_t i = 0; i < prev_api.size(); ++i) {
        const auto& p = prev_api[i];
        if (prev_used[i] || p.fn != a.fn || p.lr != a.lr || p.out != a.out) continue;   // por sitio y orden: los argumentos dependen del sonido (no se rebobina)
        prev_used[i] = true;
        c.r3.u64 = p.ret; c.f1.u64 = p.fret;
        if (p.out) REX_STORE_U32(p.out, p.value);
        cur->api.push_back(p);
        ++n_api_kept;
        return true;
      }
    in_api = true;
    orig(c, base);
    in_api = false;
    a.ret = c.r3.u64; a.fret = c.f1.u64;
    a.value = a.out ? REX_LOAD_U32(a.out) : 0;
    cur->api.push_back(a);
    n_api_real += replay;
    return true;
  }
  static void Report() {
    {
      std::string txt;
      for (int r = 0; r < 2; ++r) {
        txt += r ? " | sin repetir:" : " nuevas:";
        for (const auto& [k, n] : r ? api_unused : api_new) { char b[40]; std::snprintf(b, sizeof b, " %08X<%08X:%u", k.first, k.second, n); txt += b; }
      }
      uint32_t h[3]; std::memcpy(h, mem + 0x8238A6F0u, sizeof h);
      REXLOG_INFO("dbz3 rb: WLOG monton: {} bloques, {} sin marca, {} bytes; diario{}", int32_t(_byteswap_ulong(h[0])), int32_t(_byteswap_ulong(h[1])), int32_t(_byteswap_ulong(h[2])), txt);
    }
    if (args_on) {
      std::string txt;
      for (const auto& [k, st] : arg_stat) if (st.reg || st.memo) { char b[80]; std::snprintf(b, sizeof b, " %08X<%08X:%u/r%u/m%u/o%u", k.first, k.second, st.kept, st.reg, st.memo, st.out); txt += b; }
      REXLOG_INFO("dbz3 rb: WLOG argumentos distintos al resimular (llamadas/registros/memoria/con salida):{}", txt);
    }
    REXLOG_INFO("dbz3 rb: WLOG sonido al resimular: APIs {} reutilizadas / {} nuevas; gestor {} repetidas omitidas / {} nuevas", n_api_kept, n_api_real, n_snd_kept, n_snd_real);
    REXLOG_INFO("dbz3 rb: WLOG palabras apuntadas {}, ajenas sin deshacer {}, dentro de llamadas externas {}, fuera: libreria {}, pila {}, fisica {}, atomicas {}", n_words, n_foreign, n_dyn, n_ext, n_stack, n_top, n_atom);
  }
};
}  // namespace dbz3::net::rb
#endif
