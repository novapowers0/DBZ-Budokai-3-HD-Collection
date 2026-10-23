// dbz3 - Online: checksum por fotograma (R1) y sonda / prototipo de la puerta del rollback (R2).
// TODO apagado por defecto (cvars dbz3_net_sumas / dbz3_rb_prueba o variables de entorno de pruebas). Cabecera
// solo para netplay_hooks.cpp (hilo del juego, una llamada por iteracion del bucle principal = un tick).
//
//  R1  Sums: 5 bloques de la region del combate (gestor, luchador 0, luchador 1, resto del gestor, RNG).
//      mgr = *0x823D6CF4 (US y EU), luchador k = mgr + 2096 + 8240 k, estado del combate mgr+35128 (4 = pelea).
//  R2  DBZ3_RB_PROBE=1       hash por pagina (4 KB) de la memoria comprometida del invitado en cada fotograma de
//                            pelea 30..330: paginas que cambian por fotograma, mapa de zonas, coste de la logica.
//      DBZ3_RB_RESTORE=n     tras la sonda: guarda en el fotograma S, deja correr 8, restaura, y repite n veces
//                            comparando los hashes de los fotogramas repetidos con los de la primera pasada.
//      DBZ3_RB_REGION=mgr|paginas   que se guarda (gestor 35 KB + RNG, o las paginas que la sonda vio cambiar).
//      DBZ3_RB_OUT=dir           donde se escriben informes y volcados (por defecto D:\tmp\rb).
//  R3  (solo pruebas; ver studies 5/ENTREGAS/R3/INFORME.md)
//      DBZ3_RB_SOLO=1            combate 1P VS 2P sin red (DBZ3_RB_COM=1: 1P VS COM)   DBZ3_RB_SCRIPT=1  mandos por guion
//      DBZ3_RB_PADS=1            anade los registros de mando a la region   DBZ3_RB_EXTRA="dir:len,..."  tramos extra (hex:dec)
//      DBZ3_RB_RESIM=m1,m2,..    tras la 1.a pasada, restaurar y resimular 8 ticks con esa mascara de llamadas (0024 = mandos+927E0+LFSR)
//      DBZ3_RB_COSMRNG=1         efectos/sonido con LFSR propio   DBZ3_RB_DUMPREP=1  volcados rep<p>_<k>.bin / st<p>_<k>.bin
//      DBZ3_RB_RNGCALLS=1        cuenta de llamadas al LFSR por llamante y tick (rng_llamadas.txt)
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <rex/hash.h>
#include "../eu_map.h"
#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dbz3::net::rb {

constexpr uint32_t kMgrPtr = 0x823D6CF4, kRngAddr = 0x82310670;
constexpr uint32_t kFight0 = 2096, kFightSz = 8240, kMgrLen = 35200;   // 35200 = hasta 35180 + 4 (clima) y un poco mas
constexpr uint32_t kStateOff = 35128;
constexpr uint32_t kPage = 4096, kLimit = 0xC0000000u;   // se mira hasta aqui; 0xC0.. y 0xE0.. son espejos del fisico 0xA0.. (se cuentan aparte)
constexpr uint32_t kMirror0 = 0x90000000u, kMirror1 = 0xA0000000u;   // 0x9.. repite 0x8.. (medido: mismas paginas y cuentas)

struct Sums {
  uint32_t b[5] = {};   // mgr[0,2096) | luchador 0 | luchador 1 | mgr[18576,kMgrLen) | RNG
};

inline uint64_t H(const uint8_t* p, size_t n) { return XXH3_64bits(p, n); }

// Gestor del combate si hay pelea (estado 4); 0 si no.
inline uint32_t Fight(uint8_t* base) {
  const uint32_t m = REX_LOAD_U32(kMgrPtr);
  if (m < 0x10000 || m > kLimit - kMgrLen || (m & 3)) return 0;
  return REX_LOAD_U32(m + kStateOff) == 4 ? m : 0;
}

// Bytes que NO son simulacion y distintos en cada PC (medido 2026-10-22, dos copias, volcados del primer fotograma de
// pelea, tools: studies 5/ENTREGAS/R1_R2/diff_volcados.py): punteros de monton del invitado (0x4014.../0x4015..: el
// monton no se reparte igual en los dos PCs) y un contador de tiempo. Se ponen a 0 antes del hash. Una lista abierta:
// cada diferencia nueva que salga en el log se mide con los volcados y se anade aqui.
// ponytail: lista medida a mano con una sola escena (idle + COM); otras escenas pueden anadir mas punteros. Techo: si crece,
// aprender la mascara sola comparando dos volcados (diff_volcados.py ya da los desplazamientos).
struct MaskRange { uint32_t off, len; };
// R3 (medido con la resimulacion, mandos por guion): mgr+0x891C = puntero que alterna entre dos bufferes segun la paridad de un
// contador global fuera del gestor (823D0A40 / 823CEA10); luchador +0x1AF0 = 11 B de coordenadas de trabajo que valen otra cosa solo
// en el primer tick tras restaurar (se igualan en el 2.o).
// gestor +0x2D4/+0x354/+0x358/+0x5C0: contadores de peticiones de sonido/efecto (+1 en la repeticion cuando el canal 0x823A5B10 estaba
// libre); medido: no cambian ni el LFSR ni los luchadores en 8 ticks [H: tampoco mas alla].
constexpr MaskRange kMaskMgr[] = {{0x22C, 4}, {0x200, 80}, {0x600, 0x80}, {0x82C, 4}, {0x891C, 8}, {0x2D4, 8}, {0x354, 4}, {0x358, 4}, {0x5C0, 4}, {0x3C0, 4}, {0x424, 4}};   // R4: +0x66C (16 B, se corrige solo en 3 ticks); R7: +0x64C..0x67B (antes solo 0x66C) (FFFFFFFF vs 2/3/1, transitorio 3 ticks, aparece en 1 de los dos lados segun el momento) // R7: +0x22C (4 B, basura transitoria: 5EE10100 / 00650000 vs 0) // R7: +0x208..0x24F basura transitoria (FFFFFFFF, 1AA, valores absurdos) // R7b: +0x200..0x24F y +0x600..0x67F (tabla de ranuras con paso 0x20: 0x60C/0x62C/0x64C, mismo latch transitorio)
constexpr MaskRange kMaskFighter[] = {{0x13A8, 16}, {0x1A6C, 4}, {0x1AF0, 12}, {0x1E0C, 4}, {0x1EEC, 4}, {0x1EF4, 8}};   // R7: +0x1A6C (2 B: 0 vs 0x491B0000 en los dos luchadores a la vez, estable 8 ticks, nada mas diverge; no es mando ni vblank)

// Suma de una copia de la region (gestor kMgrLen bytes) y el RNG que tenia entonces.
inline Sums ComputeBuf(const uint8_t* mgr, uint32_t rng) {
  static thread_local uint8_t tmp[kMgrLen];
  std::memcpy(tmp, mgr, kMgrLen);
  for (const auto& r : kMaskMgr) std::memset(tmp + r.off, 0, r.len);
  for (int f = 0; f < 2; ++f)
    for (const auto& r : kMaskFighter) std::memset(tmp + kFight0 + f * kFightSz + r.off, 0, r.len);
  Sums s;
  s.b[0] = uint32_t(H(tmp, kFight0));
  s.b[1] = uint32_t(H(tmp + kFight0, kFightSz));
  s.b[2] = uint32_t(H(tmp + kFight0 + kFightSz, kFightSz));
  s.b[3] = uint32_t(H(tmp + kFight0 + 2 * kFightSz, kMgrLen - kFight0 - 2 * kFightSz));
  s.b[4] = rng;
  return s;
}
inline Sums Compute(uint8_t* base, uint32_t m) { return ComputeBuf(base + m, REX_LOAD_U32(kRngAddr)); }

inline std::filesystem::path OutDir() {
  const char* o = std::getenv("DBZ3_RB_OUT");
  std::filesystem::path d = o && *o ? o : "D:\\tmp\\rb";
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return d;
}

// R3 (pruebas, DBZ3_RB_SCRIPT=1): mandos de los dos jugadores generados por el numero de tick de pelea (cambian cada 6 ticks),
// iguales en la primera pasada y en la resimulacion; sin mando real ni COM. Botones XInput: A 1000, B 2000, X 4000, Y 8000, LB 100, RB 200.
inline void ScriptPad(uint32_t user, uint32_t id, uint16_t& b, int16_t& lx, int16_t& ly) {
  uint32_t h = (id / 6 + 1) * 2654435761u ^ (user + 1) * 40503u;
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  static const uint16_t kBtn[8] = {0, 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x5000};
  b = kBtn[h & 7];
  lx = int16_t(int((h >> 3) % 3) - 1) * 32000;
  ly = int16_t(int((h >> 5) % 3) - 1) * 32000;
}

// Volcado de la region de los ultimos 128 fotogramas (para diffs fuera de linea al detectar una desincronia).
class DumpRing {
 public:
  void Put(uint8_t* base, uint32_t m, uint32_t tick) {
    Slot& s = ring_[tick % ring_.size()];
    s.tick = tick;
    s.d.assign(base + m, base + m + kMgrLen);
  }
  void Dump(uint32_t tick, const char* who) const {
    const Slot& s = ring_[tick % ring_.size()];
    if (s.tick != tick || s.d.empty()) return;
    const auto f = OutDir() / (std::string("dump_") + who + "_" + std::to_string(tick) + ".bin");
    std::ofstream(f, std::ios::binary).write(reinterpret_cast<const char*>(s.d.data()), std::streamsize(s.d.size()));
  }

 private:
  struct Slot { uint32_t tick = 0xFFFFFFFFu; std::vector<uint8_t> d; };
  std::array<Slot, 1024> ring_;   // R7: 128 se quedaba corto si la suma del otro llega tarde
};


// DBZ3_RB_LOG=etiqueta (pruebas, sin red): en cada fotograma de pelea escribe los 5 bloques en frames_<etiqueta>.csv, y
// vuelca la region en los fotogramas 60, 120 y 300 (dump_<etiqueta>_<n>.bin) para compararlas entre dos copias.
// DBZ3_RB_SEED=n: reinicia el RNG con n en el primer fotograma de pelea (los dos juegos parten igual).
class FrameLog {
 public:
  void Frame(uint8_t* base, uint32_t m) {
    static const char* tag = std::getenv("DBZ3_RB_LOG");
    if (!tag || !*tag) return;
    if (!m) { if (n_) { f_.flush(); } return; }
    if (!n_) {
      f_.open(OutDir() / (std::string("frames_") + tag + ".csv"));
      f_ << "n,state,gestor,luch0,luch1,resto,rng\n";
      if (const char* sd = std::getenv("DBZ3_RB_SEED")) { REX_STORE_U32(kRngAddr, uint32_t(std::strtoul(sd, nullptr, 0))); }
    }
    ++n_;
    const Sums sm = Compute(base, m);
    char line[160];
    std::snprintf(line, sizeof(line), "%u,%u,%08X,%08X,%08X,%08X,%08X\n", n_, REX_LOAD_U32(m + kStateOff), sm.b[0], sm.b[1], sm.b[2], sm.b[3], sm.b[4]);
    f_ << line;
    if (n_ == 60 || n_ == 120 || n_ == 300) {
      const auto d = OutDir() / (std::string("dump_") + tag + "_" + std::to_string(n_) + ".bin");
      std::ofstream(d, std::ios::binary).write(reinterpret_cast<const char*>(base + m), kMgrLen);
    }
    if (n_ == 400) f_.flush();
  }

 private:
  uint32_t n_ = 0;
  std::ofstream f_;
};

#ifdef _WIN32
// ------------------------------------------------------------------ R2: sonda de paginas + prototipo
class Gate {
 public:
  using Clock = std::chrono::steady_clock;
  static double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

  struct Cfg {
    bool probe = false;
    int restore = 0;
    bool pages = false;
    bool statics = false;   // paginas aprendidas pero solo las del ejecutable (0x82000000-0x8FFFFFFF)
    bool memcpy_ = false;   // restaurar con memcpy en vez de WriteProcessMemory
    std::vector<uint32_t> resim;   // DBZ3_RB_RESIM=mascara,mascara,...(hex): tras la primera pasada, restaurar y resimular 8 ticks llamando a esas funciones
    static Cfg Read() {
      Cfg c;
      auto env = [](const char* k) { const char* v = std::getenv(k); return v && *v ? v : nullptr; };
      c.probe = env("DBZ3_RB_PROBE") != nullptr;
      if (auto v = env("DBZ3_RB_RESTORE")) c.restore = std::max(0, std::atoi(v));
      if (auto v = env("DBZ3_RB_REGION")) { c.pages = std::string(v) == "paginas"; c.statics = std::string(v) == "estaticos"; }
      c.memcpy_ = env("DBZ3_RB_MEMCPY") != nullptr;
      if (auto v = env("DBZ3_RB_RESIM"))
        for (const char* q = v; *q;) {
          char* e = nullptr;
          c.resim.push_back(uint32_t(std::strtoul(q, &e, 16)));
          if (e == q) break;
          q = *e == ',' ? e + 1 : e;
        }
      if (c.restore) c.probe = c.probe || c.pages || c.statics;   // el modo paginas necesita aprender el conjunto
      return c;
    }
  };

  // Llamar al empezar cada iteracion (antes de orig). Devuelve true si este fotograma es de pelea.
  void Frame(uint8_t* base, uint32_t m) {
    if (!cfg_.probe && !cfg_.restore) return;
    if (!m) {   // la pelea acabo con la prueba a medias: informe parcial
      sid_ = -1;
      if (cfg_.restore && !done_ && restores_ > 0) Report();
      return;
    }
    ++n_;
    sid_ = int32_t(n_);
    if (cfg_.probe && n_ >= kW0 && n_ <= kW1) {
      ULONG64 cy = 0;
      QueryThreadCycleTime(GetCurrentThread(), &cy);
      cyc_.push_back(cy);   // inicio; el final (tras la sonda) va en cyc_end_
      const auto now = Clock::now();
      if (last_ != Clock::time_point{}) intervalo_ms_.push_back(Ms(now - last_));
      last_ = now;
      if (n_ == kW0) cpu100_a_ = CpuTimes();
      if (n_ == kW1) cpu100_b_ = CpuTimes();
    }
    if (cfg_.probe && n_ >= kW0 && n_ < kW1) Scan(base);
    if (cfg_.probe && n_ >= kW0 && n_ <= kW1) {
      ULONG64 cy = 0;
      QueryThreadCycleTime(GetCurrentThread(), &cy);
      cyc_end_.push_back(cy);
    }
    if (cfg_.probe && n_ == kW1) Finish(base);
    if (cfg_.restore && n_ >= kW1 + 10 && !done_) Cycle(base, m);
  }
  void OrigMs(double ms, bool fight) {
    if (!fight || (!cfg_.probe && !cfg_.restore)) return;
    if (orig_ms_.size() < 4096) orig_ms_.push_back(ms);
  }
  bool active() const { return cfg_.probe || cfg_.restore; }
  // R3: quien consume el LFSR (hook de sub_82084A78): llamadas por (lr del que llama) en el tick en curso, hilo del juego aparte
  // del resto. Solo con DBZ3_RB_RNGCALLS=1 (cuenta de prueba, el informe va a rng_llamadas.txt).
  void NoteRng(uint32_t lr) {
    if (!rngcalls_) return;
    if (GetCurrentThreadId() != game_tid_) { other_.fetch_add(1, std::memory_order_relaxed); return; }
    ++cur_[lr];
  }
  bool rngcalls() const { return rngcalls_; }
  // objetos: llamadas a las rutinas que consumen el LFSR (fn, lr, r3 = objeto), en orden, por tick
  void NoteObj(uint32_t fn, uint32_t lr, uint32_t r3) {
    if (rngcalls_ && GetCurrentThreadId() == game_tid_) objs_cur_.push_back({fn, lr, r3});
  }
  int32_t ScriptId() const { return script_ ? sid_ : -1; }   // tick de pelea cuyo mando se lee ahora (-1: no hay guion)
  void ScriptAt(int32_t id) { sid_ = id; }
  int32_t CurN() const { return int32_t(n_); }
  int32_t SaveN() const { return int32_t(save_n_); }
  // R3 resimulacion (prueba): el gancho pregunta antes de orig(); si hay una, llama a los ticks y a ResimCheck tras cada uno.
  bool ResimPending() const { return resim_pending_; }
  uint32_t ResimMask() const { return resim_mask_; }
  void ResimCheck(uint8_t* base, uint32_t r) {   // tras el tick r (1..8): compara con la primera pasada
    const Sums now = Compute(base, mgr_);
    for (int i = 0; i < 5; ++i)
      if (now.b[i] != ref_[r].b[i]) ++rmiss_[resim_idx_][r][i];
    if (rngcalls_ && restores_ == 1) { calls_[1][r] = cur_; objs_[1][r] = objs_cur_; other_n_[1][r] = other_.load(); }
    cur_.clear();
    objs_cur_.clear();
    other_ = 0;
    DumpRep(base, mgr_, r);   // rep1_r.bin / st1_r.bin de la primera resimulacion (si DBZ3_RB_DUMPREP)
  }
  void ResimDone() { resim_pending_ = false; }

 private:
  static constexpr uint32_t kW0 = 30, kW1 = 330, kCycle = 8;
  Cfg cfg_ = Cfg::Read();
  uint32_t n_ = 0;
  bool done_ = false;

  // --- sonda
  std::vector<std::pair<uint32_t, uint32_t>> ranges_;   // [ini, fin) comprometido y legible, por debajo de kLimit
  std::vector<uint64_t> prev_;
  std::vector<uint32_t> chg_;       // fotogramas en que cambio cada pagina
  std::vector<uint32_t> per_frame_;   // paginas cambiadas por fotograma
  std::vector<double> scan_ms_, orig_ms_;
  uint64_t committed_ = 0, committed_hi_ = 0;
  std::map<uint32_t, char> prot_;
  uint32_t ro_pages_ = 0;
  std::vector<uint32_t> set_;       // paginas del conjunto aprendido (indices de pagina)
  uint32_t scans_ = 0;
  std::vector<uint64_t> cyc_end_;
  std::vector<uint64_t> cyc_;       // QueryThreadCycleTime del hilo del juego en cada inicio de iteracion de pelea
  uint64_t cpu100_a_ = 0, cpu100_b_ = 0;   // GetThreadTimes (usuario+nucleo, 100 ns) en W0 y W1
  std::vector<double> intervalo_ms_;
  Clock::time_point last_ = {};
  static uint64_t CpuTimes() {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    return ((uint64_t(k.dwHighDateTime) << 32) | k.dwLowDateTime) + ((uint64_t(u.dwHighDateTime) << 32) | u.dwLowDateTime);
  }

  void Ranges(uint8_t* base) {
    ranges_.clear();
    committed_ = committed_hi_ = 0;
    uint8_t* p = base;
    uint8_t* const end = base + 0x100000000ull;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
      uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress);
      uint8_t* e = b + mbi.RegionSize;
      const bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0;
      const uint64_t g0 = uint64_t(b - base), g1 = std::min<uint64_t>(uint64_t(e - base), 0x100000000ull);
      if (ok && g0 < g1) {
        if (g0 < kLimit && !(g0 >= kMirror0 && g1 <= kMirror1)) {
          const uint32_t a = uint32_t(g0), z = uint32_t(std::min<uint64_t>(g1, kLimit));
          if (!ranges_.empty() && ranges_.back().second == a) ranges_.back().second = z;
          else ranges_.push_back({a, z});
          committed_ += z - a;
        } else {
          committed_hi_ += g1 - g0;
        }
      }
      p = e;
    }
  }

  // 'W' escribible, 'R' solo lectura (el SDK la protege para vigilar escrituras: GPU / texturas), '-' otra
  static char Prot(const uint8_t* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) return '-';
    if (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) return 'W';
    if (mbi.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ)) return 'R';
    return '-';
  }
  static bool Read(const uint8_t* src, uint8_t* dst, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), src, dst, n, &got) && got == n;
  }

  void Scan(uint8_t* base) {
    const auto t0 = Clock::now();
    if (prev_.empty()) {
      prev_.assign(kLimit / kPage, 0);
      chg_.assign(kLimit / kPage, 0);
    }
    if (scans_ % 30 == 0) Ranges(base);
    std::vector<uint8_t> buf(64 * 1024);
    uint32_t changed = 0;
    for (const auto& [a, z] : ranges_) {
      for (uint32_t o = a; o < z; o += uint32_t(buf.size())) {
        const uint32_t n = std::min<uint32_t>(uint32_t(buf.size()), z - o);
        if (!Read(base + o, buf.data(), n)) continue;
        for (uint32_t k = 0; k + kPage <= n; k += kPage) {
          const uint32_t pi = (o + k) / kPage;
          const uint64_t h = H(buf.data() + k, kPage) | 1;   // 0 = nunca visto
          if (scans_ > 0 && prev_[pi] && prev_[pi] != h) { ++chg_[pi]; ++changed; }
          prev_[pi] = h;
        }
      }
    }
    if (scans_ > 0) per_frame_.push_back(changed);
    ++scans_;
    scan_ms_.push_back(Ms(Clock::now() - t0));
  }

  static double Pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))];
  }
  static double PctU(std::vector<uint32_t> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))];
  }

  void Finish(uint8_t* base) {
    Ranges(base);
    set_.clear();
    std::map<uint32_t, std::pair<uint32_t, uint64_t>> by_mb;   // MB -> (paginas, suma de cambios)
    uint32_t hot = 0;
    for (uint32_t pi = 0; pi < chg_.size(); ++pi) {
      if (!chg_[pi]) continue;
      set_.push_back(pi);
      if (Prot(base + pi * kPage) == 'R') ++ro_pages_;
      prot_[pi] = Prot(base + pi * kPage);
      auto& e = by_mb[(pi * kPage) >> 20];
      ++e.first;
      e.second += chg_[pi];
      if (chg_[pi] * 2 >= uint32_t(per_frame_.size())) ++hot;
    }
    const auto f = OutDir() / ("sonda_" + std::to_string(GetCurrentProcessId()) + ".txt");
    std::ofstream o(f);
    o << "fotogramas de pelea medidos: " << per_frame_.size() << "\n";
    o << "comprometido < 0xC0000000 sin el espejo 0x9..: " << committed_ / 1048576.0 << " MB; espejos >= 0xC0000000 y 0x9..: " << committed_hi_ / 1048576.0 << " MB\n";
    o << "paginas cambiadas por fotograma: min " << PctU(per_frame_, 0) << " p50 " << PctU(per_frame_, 0.5) << " p95 "
      << PctU(per_frame_, 0.95) << " max " << PctU(per_frame_, 1) << "\n";
    o << "de ellas solo lectura (vigiladas por el SDK): " << ro_pages_ << "\n";
    o << "conjunto (cambio alguna vez): " << set_.size() << " paginas = " << set_.size() * 4 / 1024.0 << " MB; calientes (>=50% de los fotogramas): "
      << hot << "\n";
    o << "coste de recorrer toda la memoria (ms por fotograma, hash 4 KB): p50 " << Pct(scan_ms_, 0.5) << " p95 " << Pct(scan_ms_, 0.95) << "\n";
    o << "coste de orig() del bucle (ms, p50/p95/max; incluye sondeo de mandos y lo que cuelgue de el): " << Pct(orig_ms_, 0.5) << " / "
      << Pct(orig_ms_, 0.95) << " / " << Pct(orig_ms_, 1) << "\n";
    if (cyc_.size() > 2 && cpu100_b_ > cpu100_a_) {   // CPU del hilo del juego por fotograma, calibrado con GetThreadTimes
      const double cpu_ms = double(cpu100_b_ - cpu100_a_) / 1e4;
      const double cyc_per_ms = double(cyc_.back() - cyc_.front()) / cpu_ms;
      std::vector<double> d;
      for (size_t i = 1; i < cyc_.size() && i < cyc_end_.size(); ++i) d.push_back(double(cyc_[i] - cyc_end_[i - 1]) / cyc_per_ms);
      o << "CPU del hilo del juego por fotograma (sin esperas; ms): media " << [&] { double t = 0; for (double x : d) t += x; return t / double(d.size()); }() << " p50 " << Pct(d, 0.5)
        << " p95 " << Pct(d, 0.95) << " max " << Pct(d, 1) << "\n";
      o << "intervalo entre fotogramas (ms): p50 " << Pct(intervalo_ms_, 0.5) << " p95 " << Pct(intervalo_ms_, 0.95) << "\n";
    }
    o << "\nMB (direccion del invitado) | paginas cambiadas | cambios medios por pagina\n";
    char line[160];
    for (const auto& [mb, e] : by_mb) {
      std::snprintf(line, sizeof(line), "0x%08X | %u | %.1f\n", mb << 20, e.first, double(e.second) / e.first);
      o << line;
    }
    o << "\nTramos contiguos (inicio, paginas, cambios medios):\n";
    for (size_t i = 0; i < set_.size();) {
      size_t j = i;
      uint64_t sum = 0;
      while (j < set_.size() && (j == i || set_[j] == set_[j - 1] + 1)) sum += chg_[set_[j++]];
      std::snprintf(line, sizeof(line), "0x%08X %zu %.1f %c\n", set_[i] * kPage, j - i, double(sum) / double(j - i), prot_[set_[i]]);
      o << line;
      i = j;
    }
    REXLOG_INFO("dbz3 rb: sonda: {} paginas cambiadas por fotograma (p50), conjunto {} paginas; informe {}", PctU(per_frame_, 0.5),
                set_.size(), rex::path_to_utf8(f));
  }

  // --- prototipo guardar/restaurar
  struct Run { uint32_t a, n; };
  std::vector<Run> runs_;
  std::vector<uint8_t> save_;
  uint64_t save_hash_ = 0;
  uint32_t restores_ = 0, region_bad_ = 0, wfail_ = 0, slow_logged_ = 0;
  Sums ref_[kCycle + 1] = {};
  uint32_t miss_[kCycle + 1][5] = {};
  uint32_t miss_frames_ = 0;
  std::vector<double> save_ms_, rest_ms_, hash_ms_;

  void BuildRuns(uint32_t m) {
    runs_.clear();
    if (cfg_.pages || cfg_.statics) {
      for (size_t i = 0; i < set_.size();) {
        size_t j = i + 1;
        while (j < set_.size() && set_[j] == set_[j - 1] + 1 && prot_[set_[j]] == prot_[set_[i]]) ++j;
        const uint32_t ga = set_[i] * kPage;
        const bool in = !cfg_.statics || (ga >= 0x82000000u && ga < 0x90000000u);
        if (in && (prot_[set_[i]] == 'W' || std::getenv("DBZ3_RB_RO"))) runs_.push_back({set_[i] * kPage, uint32_t((j - i) * kPage)});
        i = j;
      }
    } else {
      runs_.push_back({m, kMgrLen});
      runs_.push_back({kRngAddr, 4});
      if (std::getenv("DBZ3_RB_PADS")) { runs_.push_back({0x8238B670u, 16}); runs_.push_back({0x8238B780u, 736}); }   // registros de mando (US)
      // PRUEBAS: DBZ3_RB_EXTRA="dir:len,dir:len" (hex) anade tramos medidos (contadores / listas fuera del gestor)
      if (const char* ex = std::getenv("DBZ3_RB_EXTRA"))
        for (const char* q = ex; *q;) {
          char* e = nullptr;
          const uint32_t a = uint32_t(std::strtoul(q, &e, 16));
          if (*e != ':') break;
          const uint32_t n = uint32_t(std::strtoul(e + 1, &e, 10));
          runs_.push_back({a, n});
          q = *e == ',' ? e + 1 : e;
        }
    }
    uint64_t tot = 0;
    for (const auto& r : runs_) tot += r.n;
    save_.assign(tot, 0);
  }

  void Save(uint8_t* base) {
    size_t o = 0;
    for (const auto& r : runs_) {
      if (!Read(base + r.a, save_.data() + o, r.n)) std::memset(save_.data() + o, 0, r.n);
      o += r.n;
    }
  }
  void Restore(uint8_t* base) {
    size_t o = 0;
    for (const auto& r : runs_) {
      SIZE_T w = 0;
      const auto t0 = Clock::now();
      bool ok;
      if (cfg_.memcpy_) { std::memcpy(base + r.a, save_.data() + o, r.n); ok = true; }
      else ok = WriteProcessMemory(GetCurrentProcess(), base + r.a, save_.data() + o, r.n, &w) && w == r.n;
      const double ms = Ms(Clock::now() - t0);
      if (!ok) ++wfail_;
      if ((!ok || ms > 0.5) && slow_logged_ < 12) {
        ++slow_logged_;
        REXLOG_INFO("dbz3 rb: tramo 0x{:08X} +{} {} en {:.3f} ms (error {})", r.a, r.n, ok ? "escrito" : "FALLO", ms, ok ? 0 : GetLastError());
      }
      o += r.n;
    }
  }
  uint64_t Live(uint8_t* base) {   // hash de lo que hay AHORA en las zonas guardadas
    std::vector<uint8_t> t(save_.size());
    size_t o = 0;
    for (const auto& r : runs_) {
      if (!Read(base + r.a, t.data() + o, r.n)) std::memset(t.data() + o, 0, r.n);
      o += r.n;
    }
    return H(t.data(), t.size());
  }

  // k_ = 0: guardar aqui (fotograma S). 1..8: fotogramas siguientes; en el 8 se restaura y se vuelve a k_ = 1.
  // La primera pasada fija las referencias; las repeticiones se comparan con ellas.
  void Cycle(uint8_t* base, uint32_t m) {
    const Sums now = Compute(base, m);
    if (runs_.empty()) {
      BuildRuns(m);
      REXLOG_INFO("dbz3 rb: region {}: {} tramos, {} bytes", cfg_.pages ? "paginas" : cfg_.statics ? "estaticos" : "mgr", runs_.size(), save_.size());
    }
    SnapRng();
    mgr_ = m;
    if (!cfg_.resim.empty() && first_done_) {
      if (resim_stop_) { if (!done_) Report(); return; }
      StartResim(base);
      return;
    }
    DumpRep(base, m, k_);
    if (k_ == 0) {
      save_n_ = n_;
      const auto t0 = Clock::now();
      Save(base);
      save_ms_.push_back(Ms(Clock::now() - t0));
      const auto t1 = Clock::now();
      save_hash_ = H(save_.data(), save_.size());
      hash_ms_.push_back(Ms(Clock::now() - t1));
      ref_[0] = now;
      k_ = 1;
      if (restores_ < 2) REXLOG_INFO("dbz3 rb: guardado ({:.3f} ms, hash {:.3f} ms)", save_ms_.back(), hash_ms_.back());
      return;
    }
    if (restores_ == 0) {
      ref_[k_] = now;
    } else {
      bool any = false;
      for (int i = 0; i < 5; ++i)
        if (now.b[i] != ref_[k_].b[i]) { ++miss_[k_][i]; any = true; }
      miss_frames_ += any;
    }
    if (k_ < kCycle) { ++k_; return; }
    if (!cfg_.resim.empty()) { first_done_ = true; StartResim(base); return; }
    if (restores_ < 2) REXLOG_INFO("dbz3 rb: restaurando {} bytes", save_.size());
    const auto t0 = Clock::now();
    Restore(base);
    if (restores_ < 2) REXLOG_INFO("dbz3 rb: escrito ({:.3f} ms)", Ms(Clock::now() - t0));
    rest_ms_.push_back(Ms(Clock::now() - t0));
    if (Live(base) != save_hash_) ++region_bad_;
    ++restores_;
    if (restores_ <= 5 || restores_ % 50 == 0)
      REXLOG_INFO("dbz3 rb: restauracion {} ({:.3f} ms; distintos hasta ahora {}; hash vivo malo {}; escrituras fallidas {})", restores_,
                  rest_ms_.back(), miss_frames_, region_bad_, wfail_);
    k_ = 1;
    if (int(restores_) >= cfg_.restore) Report();
  }
  uint32_t k_ = 0;
  bool script_ = std::getenv("DBZ3_RB_SCRIPT") != nullptr;
  int32_t sid_ = -1;
  uint32_t save_n_ = 0;
  bool resim_pending_ = false, first_done_ = false, resim_stop_ = false;
  uint32_t resim_mask_ = 0, resim_idx_ = 0, mgr_ = 0;
  uint32_t rmiss_[64][kCycle + 1][5] = {};
  uint32_t rcount_[64] = {};
  void StartResim(uint8_t* base) {
    resim_idx_ = uint32_t(restores_ % cfg_.resim.size());
    resim_mask_ = cfg_.resim[resim_idx_];
    const auto t0 = Clock::now();
    Restore(base);
    rest_ms_.push_back(Ms(Clock::now() - t0));
    cur_.clear();
    objs_cur_.clear();
    other_ = 0;
    ++rcount_[resim_idx_];
    ++restores_;
    resim_pending_ = true;
    if (int(restores_) >= cfg_.restore) resim_stop_ = true;
  }
  bool rngcalls_ = std::getenv("DBZ3_RB_RNGCALLS") != nullptr;
  DWORD game_tid_ = 0;
  std::atomic<uint32_t> other_{0};
  std::map<uint32_t, uint32_t> cur_;
  struct Obj { uint32_t fn, lr, r3; };
  std::vector<Obj> objs_cur_, objs_[2][kCycle + 1];
  std::map<uint32_t, uint32_t> calls_[2][kCycle + 1];
  uint32_t other_n_[2][kCycle + 1] = {};
  void SnapRng() {   // al empezar el fotograma k_: lo contado desde el anterior es la logica del tick k_-1 -> k_
    if (!rngcalls_) return;
    if (restores_ <= 1 && k_ >= 1) {
      calls_[restores_][k_] = cur_;
      objs_[restores_][k_] = objs_cur_;
      other_n_[restores_][k_] = other_.load();
    }
    cur_.clear();
    objs_cur_.clear();
    other_ = 0;
    game_tid_ = GetCurrentThreadId();
  }
  void ReportRng() {
    if (!rngcalls_) return;
    std::ofstream o(OutDir() / "rng_llamadas.txt");
    o << "llamadas al LFSR por tick (lr: pasada0 / repeticion1); solo los que difieren\n";
    for (uint32_t k = 1; k <= kCycle; ++k) {
      o << "k=" << k << " otros hilos " << other_n_[0][k] << " / " << other_n_[1][k] << "\n";
      std::map<uint32_t, std::pair<uint32_t, uint32_t>> m;
      for (auto& [lr, c] : calls_[0][k]) m[lr].first = c;
      for (auto& [lr, c] : calls_[1][k]) m[lr].second = c;
      for (int ps = 0; ps < 2; ++ps) {
        o << "  objetos " << (ps ? "repeticion" : "pasada0") << ":";
        for (const auto& ob : objs_[ps][k]) { char b[64]; std::snprintf(b, sizeof b, " %08X@%08X(%08X)", ob.fn, ob.lr, ob.r3); o << b; }
        o << "\n";
      }
      for (auto& [lr, c] : m)
        if (c.first != c.second) { char b[96]; std::snprintf(b, sizeof b, "  %08X: %u / %u\n", lr, c.first, c.second); o << b; }
    }
  }

  // DBZ3_RB_DUMPREP=1: volcado crudo (gestor 35200 | RNG 4 | mandos 16 | mandos 736) al empezar cada fotograma k=0..8 de la
  // primera pasada (rep0_k.bin) y de la primera repeticion (rep1_k.bin). `diff_volcados.py rep0_k.bin rep1_k.bin` los compara.
  void DumpRep(uint8_t* base, uint32_t m, uint32_t k) {
    static const bool on = std::getenv("DBZ3_RB_DUMPREP") != nullptr;
    if (!on || restores_ > 4) return;
    std::vector<uint8_t> b(kMgrLen + 4 + 16 + 736);
    std::memcpy(b.data(), base + m, kMgrLen);
    std::memcpy(b.data() + kMgrLen, base + kRngAddr, 4);
    std::memcpy(b.data() + kMgrLen + 4, base + 0x8238B670u, 16);
    std::memcpy(b.data() + kMgrLen + 20, base + 0x8238B780u, 736);
    std::ofstream(OutDir() / ("rep" + std::to_string(restores_) + "_" + std::to_string(k) + ".bin"), std::ios::binary)
        .write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    // estaticos del juego 0x82300000-0x82600000 (3 MB) para ver que campos NO restaurados cambian (st<pasada>_<k>.bin)
    std::vector<uint8_t> st(0x300000);
    for (uint32_t o = 0; o < st.size(); o += 0x10000)
      if (!Read(base + 0x82300000u + o, st.data() + o, 0x10000)) std::memset(st.data() + o, 0, 0x10000);
    std::ofstream(OutDir() / ("st" + std::to_string(restores_) + "_" + std::to_string(k) + ".bin"), std::ios::binary)
        .write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
  }

  void Report() {
    done_ = true;
    ReportRng();
    const auto f = OutDir() / ("restaurar_" + std::to_string(GetCurrentProcessId()) + ".txt");
    std::ofstream o(f);
    o << "region: " << (cfg_.pages ? "paginas aprendidas" : cfg_.statics ? "paginas aprendidas del ejecutable (0x82-0x8F)" : "gestor + RNG") << ", " << save_.size() << " bytes en " << runs_.size() << " tramos\n";
    o << "guardar (ms): p50 " << Pct(save_ms_, 0.5) << " max " << Pct(save_ms_, 1) << "\n";
    o << "restaurar (ms): p50 " << Pct(rest_ms_, 0.5) << " p95 " << Pct(rest_ms_, 0.95) << " max " << Pct(rest_ms_, 1) << "\n";
    o << "hash de la region guardada (ms): p50 " << Pct(hash_ms_, 0.5) << "\n";
    o << "restauraciones: " << restores_ << "; hash de la region distinto justo tras restaurar: " << region_bad_
      << "; escrituras fallidas: " << wfail_ << "\n";
    o << "repeticiones de ocho fotogramas con algun bloque distinto de la primera pasada: " << miss_frames_ << " fotogramas de " << restores_ * kCycle << "\n";
    if (!cfg_.resim.empty()) {
      o << "RESIMULACION (por mascara: ticks resimulados que difieren de la primera pasada, por bloque [gestor luch0 luch1 resto rng])\n";
      for (size_t i = 0; i < cfg_.resim.size(); ++i) {
        uint32_t tot[5] = {};
        for (uint32_t r = 1; r <= kCycle; ++r)
          for (int b = 0; b < 5; ++b) tot[b] += rmiss_[i][r][b];
        char l[160];
        std::snprintf(l, sizeof l, "  0x%04X: ciclos %u; difieren [%u %u %u %u %u] de %u ticks\n", cfg_.resim[i], rcount_[i], tot[0], tot[1], tot[2], tot[3], tot[4], rcount_[i] * kCycle);
        o << l;
        for (uint32_t r = 1; r <= kCycle; ++r) {   // por tick resimulado (1..8): ciclos en que difiere cada bloque
          std::snprintf(l, sizeof l, "      tick %u: [%u %u %u %u %u]\n", r, rmiss_[i][r][0], rmiss_[i][r][1], rmiss_[i][r][2], rmiss_[i][r][3], rmiss_[i][r][4]);
          o << l;
        }
      }
    }
    for (uint32_t c = 1; c <= kCycle; ++c) {
      o << "  fotograma +" << c << ": difieren por bloque [gestor, luch0, luch1, resto, rng] = " << miss_[c][0] << " " << miss_[c][1] << " "
        << miss_[c][2] << " " << miss_[c][3] << " " << miss_[c][4] << "\n";
    }
    REXLOG_INFO("dbz3 rb: restaurar: {} restauraciones, guardar p50 {:.3f} ms, restaurar p50 {:.3f} ms, {} fotogramas distintos; informe {}",
                restores_, Pct(save_ms_, 0.5), Pct(rest_ms_, 0.5), miss_frames_, rex::path_to_utf8(f));
  }
};
#endif

#ifdef _WIN32
// ------------------------------------------------------------------ R4: nucleo del rollback (instantaneas)
// Region guardada (medida en R3, 0 diferencias en 2400 ticks resimulados): gestor del combate + LFSR + registros de mando
// + 6 contadores/relojes que la logica compara con el estado guardado. Todo US (ver R4/PENDIENTE.md para EU).
class Core {
 public:
  static constexpr int kSlots = 64;   // ticks recientes (ventana <= 6; la reparacion tarda ~15 ticks en llegar)
  struct Run { uint32_t a, n; };
  using Clock = std::chrono::steady_clock;

  void Init(uint32_t mgr) {
    using dbz3::GA;
    runs_ = {{mgr, kMgrLen}, {kRngAddr, 4}, {GA(0x8238B670), 80}, {GA(0x8238B6C0), 192}, {GA(0x8238B780), 736}, {GA(0x8238D6D8), 4}, {GA(0x8238D6B4), 4},
             {GA(0x823212B4), 4}, {GA(0x82321434), 4}, {GA(0x823212EC), 8}, {GA(0x8232146C), 8}};
    base_total_ = 0;
    for (const auto& r : runs_) base_total_ += r.n;   // sin los tramos de pruebas (XR): la prueba de pureza solo compara estos
    // PRUEBAS: DBZ3_RB_XR="dir:len,dir:len" (hex:dec) tramos extra de estado fuera del gestor que el rollback tiene que restaurar
    if (const char* ex = std::getenv("DBZ3_RB_XR"))
      if (std::strcmp(ex, "HEAP") != 0)
      for (const char* q = ex; *q;) {
        char* e = nullptr;
        const uint32_t a = uint32_t(std::strtoul(q, &e, 16));
        if (*e != ':') break;
        const uint32_t n = uint32_t(std::strtoul(e + 1, &e, 10));
        runs_.push_back({a, n});
        q = *e == ',' ? e + 1 : e;
      }
    total_ = 0;
    for (const auto& r : runs_) total_ += r.n;
    for (auto& sl : slot_) { sl.tick = 0xFFFFFFFFu; sl.d.assign(total_, 0); }
    mgr_ = mgr;
    save_ms_.clear(); rest_ms_.clear();
  }
  uint32_t mgr() const { return mgr_; }
  size_t size() const { return base_total_; }   // lo que se intercambia con el otro PC: sin los tramos de pruebas
  // PRUEBAS (DBZ3_RB_XR=HEAP): anade tramos aprendidos en marcha; vacia el anillo (los tamanos cambian)
  void AddRuns(const std::vector<Run>& v) {
    xr_from_ = runs_.size();
    for (const auto& r : v) { runs_.push_back(r); total_ += r.n; }
    for (auto& sl : slot_) { sl.d.resize(total_, 0); sl.xr = false; }   // las viejas siguen valiendo para un rollback real, sin los tramos nuevos
  }
  // Reparacion: sustituye la instantanea local del tick por la del anfitrion, conservando lo local en los bytes que no son simulacion
  // (punteros de monton y contadores enmascarados: distintos en cada PC).
  bool Adopt(uint32_t tick, const uint8_t* remote, size_t n) {
    Slot& sl = slot_[tick % kSlots];
    if (sl.tick != tick || n != base_total_) return false;
    const std::vector<uint8_t> keep = sl.d;
    std::memcpy(sl.d.data(), remote, n);
    for (const auto& r : kMaskMgr) std::memcpy(sl.d.data() + r.off, keep.data() + r.off, r.len);
    for (int f = 0; f < 2; ++f)
      for (const auto& r : kMaskFighter) std::memcpy(sl.d.data() + kFight0 + f * kFightSz + r.off, keep.data() + kFight0 + f * kFightSz + r.off, r.len);
    return true;
  }
  void Save(uint8_t* base, uint32_t tick) {
    const auto t0 = Clock::now();
    Slot& sl = slot_[tick % kSlots];
    size_t o = 0;
    for (const auto& r : runs_) {
      SIZE_T got = 0;
      if (!ReadProcessMemory(GetCurrentProcess(), base + r.a, sl.d.data() + o, r.n, &got) || got != r.n) std::memset(sl.d.data() + o, 0, r.n);
      o += r.n;
    }
    sl.tick = tick;
    sl.xr = true;
    if (ptr_len_) SavePtrs(base, sl);
    if (save_ms_.size() < 4096) save_ms_.push_back(float(std::chrono::duration<double, std::milli>(Clock::now() - t0).count()));
  }
  const uint8_t* Get(uint32_t tick) const {
    const Slot& sl = slot_[tick % kSlots];
    return sl.tick == tick ? sl.d.data() : nullptr;
  }
  bool HasXr(uint32_t tick) const { return slot_[tick % kSlots].xr; }
  bool Restore(uint8_t* base, uint32_t tick) {
    const uint8_t* d = Get(tick);
    if (!d) return false;
    const auto t0 = Clock::now();
    size_t o = 0;
    bool ok = true;
    const bool with_xr = slot_[tick % kSlots].xr;
    size_t ri = 0;
    for (const auto& r : runs_) {
      SIZE_T w = 0;
      if (with_xr || ri < xr_from_) ok &= WriteProcessMemory(GetCurrentProcess(), base + r.a, d + o, r.n, &w) && w == r.n;
      o += r.n;
      ++ri;
    }
    if (ptr_len_)
      for (const auto& pr : slot_[tick % kSlots].ptrs) {
        SIZE_T w = 0;
        WriteProcessMemory(GetCurrentProcess(), base + pr.a, pr.d.data(), pr.d.size(), &w);
      }
    if (rest_ms_.size() < 4096) rest_ms_.push_back(float(std::chrono::duration<double, std::milli>(Clock::now() - t0).count()));
    return ok;
  }
  // suma de la instantanea del tick (hash enmascarado de R1); false si ya no esta en el anillo
  bool SumsAt(uint32_t tick, Sums* out) const {
    const uint8_t* d = Get(tick);
    if (!d) return false;
    uint32_t rng;   // la memoria del invitado es big endian; REX_LOAD_U32 lo da al reves de lo copiado tal cual
    std::memcpy(&rng, d + kMgrLen, 4);
    rng = _byteswap_ulong(rng);
    *out = ComputeBuf(d, rng);
    return true;
  }
  // Lo que hay AHORA en las zonas guardadas (para la prueba de pureza del rollback)
  void Live(uint8_t* base, std::vector<uint8_t>& out) const {
    out.assign(total_, 0);
    size_t o = 0;
    for (const auto& r : runs_) {
      SIZE_T got = 0;
      ReadProcessMemory(GetCurrentProcess(), base + r.a, out.data() + o, r.n, &got);
      o += r.n;
    }
  }
  // Describe las diferencias entre dos copias de la region, sin los bytes enmascarados; devuelve cuantos bytes difieren
  size_t Describe(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, std::string* txt) const {
    std::vector<uint8_t> x = a, y = b;
    for (auto* v : {&x, &y}) {
      for (const auto& r : kMaskMgr) std::memset(v->data() + r.off, 0, r.len);
      for (int f = 0; f < 2; ++f)
        for (const auto& r : kMaskFighter) std::memset(v->data() + kFight0 + f * kFightSz + r.off, 0, r.len);
    }
    size_t n = 0, groups = 0;
    for (size_t i = 0; i < std::min(x.size(), base_total_); ++i) {
      if (x[i] == y[i]) continue;
      ++n;
      size_t j = i;
      while (j + 1 < x.size() && j - i < 8 && x[j + 1] != y[j + 1]) ++j;
      if (groups++ < 24 && txt) {
        char l[96];
        if (i < kMgrLen) {
          const long rel = long(i) - long(kFight0);
          if (rel >= 0 && rel < 2L * kFightSz) std::snprintf(l, sizeof l, " luch%ld+0x%lX(%zu)", rel / long(kFightSz), rel % long(kFightSz), j - i + 1);
          else std::snprintf(l, sizeof l, " mgr+0x%zX(%zu)", i, j - i + 1);
        } else {
          std::snprintf(l, sizeof l, " extra+%zu(%zu)", i - kMgrLen, j - i + 1);
        }
        *txt += l;
      }
      i = j;
    }
    return n;
  }
  std::vector<float> save_ms_, rest_ms_;

 private:
  struct PtrRun { uint32_t a; std::vector<uint8_t> d; };
  struct Slot { uint32_t tick = 0xFFFFFFFFu; std::vector<uint8_t> d; std::vector<PtrRun> ptrs; bool xr = true; };
  size_t xr_from_ = SIZE_MAX;   // indice del primer tramo aprendido en marcha (HEAP); las instantaneas anteriores no lo tienen
  // PRUEBA (DBZ3_RB_PTR=bytes): ademas de la region, copia `bytes` a partir de cada puntero de monton (0x40000000-0x7FFFFFFF) que hay en el
  // gestor y los dos luchadores (controladores de animacion y demas objetos del combate que no viven en la region)
  void SavePtrs(uint8_t* base, Slot& sl) {
    std::vector<uint32_t> v;
    for (size_t o = 0; o + 4 <= kMgrLen; o += 4) {
      uint32_t x;
      std::memcpy(&x, sl.d.data() + o, 4);
      x = _byteswap_ulong(x);
      if (x >= 0x40000000u && x < 0x80000000u && !(x & 3)) v.push_back(x);
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    sl.ptrs.clear();
    uint32_t end = 0;
    for (uint32_t x : v) {
      if (x < end || sl.ptrs.size() >= 600) continue;   // ya cubierto por el anterior
      PtrRun pr{x, std::vector<uint8_t>(ptr_len_)};
      SIZE_T got = 0;
      if (!ReadProcessMemory(GetCurrentProcess(), base + x, pr.d.data(), ptr_len_, &got) || got != ptr_len_) continue;
      end = x + ptr_len_;
      sl.ptrs.push_back(std::move(pr));
    }
  }
  uint32_t ptr_len_ = std::getenv("DBZ3_RB_PTR") ? uint32_t(std::atoi(std::getenv("DBZ3_RB_PTR"))) : 0;
  std::array<Slot, kSlots> slot_;
  std::vector<Run> runs_;
  size_t total_ = 0, base_total_ = 0;
  uint32_t mgr_ = 0;
};

// Prueba (DBZ3_RB_DUMPAT="1,2,30"): vuelca el gestor (35 200 B) de esos ticks relativos al inicio del combate, para comparar
// la corrida con rollback contra el lockstep (diff_volcados.py).
inline void DumpAt(const char* who, uint32_t fight, uint32_t rel, const uint8_t* mgr) {
  static const char* e = std::getenv("DBZ3_RB_DUMPAT");
  if (!e || !*e) return;
  for (const char* q = e; *q;) {
    char* z = nullptr;
    const uint32_t v = uint32_t(std::strtoul(q, &z, 10));
    if (z == q) break;
    if (v == rel) {
      std::ofstream(OutDir() / ("dump_" + std::string(who) + "_f" + std::to_string(fight) + "_" + std::to_string(rel) + ".bin"), std::ios::binary)
          .write(reinterpret_cast<const char*>(mgr), kMgrLen);
      return;
    }
    q = *z == ',' ? z + 1 : z;
  }
}

// Prueba (DBZ3_RB_DUMPST="20,30"): 3 MB de estaticos (0x82300000-0x82600000) en esos ticks relativos, para ver que estado fuera de la region
// difiere entre los dos PCs tras un rollback (comparar con una corrida sin rollback). Una vez por tick de la lista.
inline void DumpStatics(uint8_t* base, const char* who, uint32_t fight, uint32_t rel) {
  static const char* e = std::getenv("DBZ3_RB_DUMPST");
  static uint32_t done[64]; static int nd = 0;
  if (!e || !*e) return;
  bool hit = false;
  for (const char* q = e; *q;) {
    char* z = nullptr;
    const uint32_t v = uint32_t(std::strtoul(q, &z, 10));
    if (z == q) break;
    if (v == rel) hit = true;
    q = *z == ',' ? z + 1 : z;
  }
  if (!hit) return;
  for (int i = 0; i < nd; ++i) if (done[i] == (fight << 16 | rel)) return;
  if (nd < 64) done[nd++] = fight << 16 | rel;
  std::vector<uint8_t> st(0x300000);
  for (uint32_t o = 0; o < st.size(); o += 0x10000)
    if (!ReadProcessMemory(GetCurrentProcess(), base + 0x82300000u + o, st.data() + o, 0x10000, nullptr)) std::memset(st.data() + o, 0, 0x10000);
  std::ofstream(OutDir() / ("st_" + std::string(who) + "_f" + std::to_string(fight) + "_" + std::to_string(rel) + ".bin"), std::ios::binary)
      .write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
}

inline double Pctl(std::vector<float> v, double q) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))];
}

// Prueba (DBZ3_RB_CSV=etiqueta): suma de cada tick confirmado, tick relativo al inicio del combate, para comparar
// corridas con retardo fijo (lockstep contra rollback: tienen que salir iguales).
class SumCsv {
 public:
  void Put(const char* who, uint32_t fight, uint32_t rel, const Sums& s) {
    static const char* tag = std::getenv("DBZ3_RB_CSV");
    if (!tag || !*tag) return;
    if (!f_.is_open()) f_.open(OutDir() / (std::string("csv_") + tag + "_" + who + ".csv"), std::ios::app);
    char line[120];
    std::snprintf(line, sizeof line, "%u,%u,%08X,%08X,%08X,%08X,%08X\n", fight, rel, s.b[0], s.b[1], s.b[2], s.b[3], s.b[4]);
    f_ << line;
    if (++n_ % 120 == 0) f_.flush();
  }
  void Flush() { if (f_.is_open()) f_.flush(); }
 private:
  std::ofstream f_;
  uint32_t n_ = 0;
};
#endif

}  // namespace dbz3::net::rb
