// Muestreador externo de un hilo (Windows x64).
//
//   sampler <pid> <segundos> <subcadena_del_nombre_del_hilo> <salida.txt>
//
// Suspende el hilo, lee su RIP con GetThreadContext, lo reanuda y escribe una linea
//   <ms> <tid> <rip>
// por muestra (~1 kHz). Con "*" muestrea todos los hilos del proceso (mas lento por hilo).
// No inyecta nada en el juego ni necesita simbolos.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <string>
#include <vector>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")

struct TimerRes {
  TimerRes() { timeBeginPeriod(1); }
  ~TimerRes() { timeEndPeriod(1); }
};

struct Th {
  DWORD tid;
  HANDLE h;
  std::string name;
};

static std::string TName(HANDLE h) {
  typedef HRESULT(WINAPI * PFN)(HANDLE, PWSTR*);
  static PFN p = nullptr;
  static bool once = false;
  if (!once) {
    HMODULE k = GetModuleHandleA("kernel32.dll");
    p = (PFN)GetProcAddress(k, "GetThreadDescription");
    once = true;
  }
  if (!p) return "";
  PWSTR d = nullptr;
  std::string s;
  if (SUCCEEDED(p(h, &d)) && d) {
    int n = WideCharToMultiByte(CP_UTF8, 0, d, -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
      s.resize(n);
      WideCharToMultiByte(CP_UTF8, 0, d, -1, &s[0], n, nullptr, nullptr);
      if (!s.empty() && s.back() == 0) s.pop_back();
    }
    LocalFree(d);
  }
  return s;
}

static void close_all(std::vector<Th>& ths) {
  for (auto& t : ths)
    if (t.h) CloseHandle(t.h);
  ths.clear();
}

int main(int argc, char** argv) {
  if (argc < 5) {
    printf("uso: sampler <pid> <segundos> <subcadena_hilo|*> <salida>\n");
    return 2;
  }
  // Sin resolucion de 1 ms, Sleep(1) en Windows es ~15,6 ms y el muestreo caeria a ~65 Hz.
  TimerRes timer_res;
  const DWORD pid = (DWORD)atoi(argv[1]);
  const int secs = atoi(argv[2]);
  const std::string want = argv[3];
  const char* out = argv[4];
  if (pid == 0 || secs <= 0) {
    printf("pid o segundos invalidos\n");
    return 2;
  }

  std::vector<Th> ths;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    THREADENTRY32 te{sizeof(te)};
    if (Thread32First(snap, &te)) {
      do {
        if (te.th32OwnerProcessID != pid) continue;
        Th t;
        t.tid = te.th32ThreadID;
        t.h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION |
                             THREAD_QUERY_LIMITED_INFORMATION,
                         FALSE, t.tid);
        if (t.h) {
          t.name = TName(t.h);
          ths.push_back(t);
        }
      } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
  }
  if (ths.empty()) {
    printf("no se encontro el proceso %lu o no se pudo abrir ningun hilo\n", (unsigned long)pid);
    return 3;
  }

  std::vector<Th> sel;
  if (want == "*") {
    sel = ths;
  } else {
    for (auto& t : ths)
      if (t.name.find(want) != std::string::npos) sel.push_back(t);
  }
  if (sel.empty()) {
    printf("no hay hilo con '%s'. hilos:\n", want.c_str());
    for (auto& t : ths) printf("  %6lu %s\n", (unsigned long)t.tid, t.name.c_str());
    close_all(ths);
    return 4;
  }

  FILE* f = fopen(out, "wb");
  if (!f) {
    printf("no se pudo abrir la salida '%s'\n", out);
    close_all(ths);
    return 5;
  }

  HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (hp) {
    DWORD need = 0;
    EnumProcessModules(hp, nullptr, 0, &need);
    if (need > 0) {
      std::vector<HMODULE> mods(need / sizeof(HMODULE));
      if (EnumProcessModules(hp, mods.data(), need, &need)) {
        const unsigned count = need / sizeof(HMODULE);
        for (unsigned i = 0; i < count && i < mods.size(); ++i) {
          char nm[MAX_PATH] = {0};
          MODULEINFO mi{};
          if (GetModuleBaseNameA(hp, mods[i], nm, sizeof(nm)) &&
              GetModuleInformation(hp, mods[i], &mi, sizeof(mi))) {
            fprintf(f, "#mod %s %p %lx\n", nm, mi.lpBaseOfDll, (unsigned long)mi.SizeOfImage);
          }
        }
      }
    }
  }
  fprintf(f, "#mode %s %d %d\n", sel.size() == 1 ? "single" : "multi", (int)sel.size(), secs);
  if (ferror(f)) {
    fclose(f);
    if (hp) CloseHandle(hp);
    close_all(ths);
    printf("error escribiendo la cabecera\n");
    return 6;
  }

  const auto t0 = std::chrono::steady_clock::now();
  long long n = 0;
  bool write_ok = true;
  bool resume_error = false;
  while (write_ok && !resume_error) {
    const auto el =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    if (el >= (long long)secs * 1000) break;
    for (auto& t : sel) {
      CONTEXT c{};
      bool got = false;
      bool resumed = true;
      if (SuspendThread(t.h) != (DWORD)-1) {
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        got = GetThreadContext(t.h, &c) != 0;
        // Se reanuda siempre, antes de cualquier E/S. Si no se puede confirmar, se para:
        // seguir leyendo un hilo suspendido daria un perfil falso.
        if (ResumeThread(t.h) == (DWORD)-1) resumed = false;
      }
      if (!resumed) {
        fprintf(stderr, "no se pudo reanudar el hilo %lu; se detiene el muestreo\n",
                (unsigned long)t.tid);
        resume_error = true;
        break;
      }
      if (!got) continue;
      if (fprintf(f, "%lld %lu %llx\n", el, (unsigned long)t.tid,
                  (unsigned long long)c.Rip) < 0) {
        write_ok = false;
        break;
      }
      ++n;
    }
    Sleep(1);
  }
  const bool flush_ok = fflush(f) == 0 && !ferror(f);
  if (fclose(f) != 0) write_ok = false;
  if (hp) CloseHandle(hp);
  close_all(ths);
  if (resume_error) {
    printf("error: no se pudo reanudar un hilo; muestreo incompleto\n");
    return 8;
  }
  if (!write_ok || !flush_ok) {
    printf("error escribiendo muestras\n");
    return 7;
  }
  printf("muestras %lld hilos %d\n", n, (int)sel.size());
  return 0;
}
