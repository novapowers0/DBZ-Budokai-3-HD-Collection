// dbz3 bootstrap - ISA dispatcher for the DBZ Budokai 3 HD Collection.
//
// The ReXGlue runtime DLLs are compiled with -march=x86-64-v3 (AVX2). CPUs
// without AVX2 crash with 0xc0000142 / 0xc000001d the moment the runtime is
// used. This tiny executable is compiled at the BASELINE x86-64 ISA (no
// -march), so it runs on any CPU. At startup it probes the CPU once (a single
// CPUID call) for the x86-64-v3 feature set and launches the matching build:
//
//   dbz3_avx2\dbz3.exe       (AVX2 capable CPUs - optimized runtime)
//   dbz3_legacy\dbz3.exe     (older CPUs - SSE4.2 runtime fallback)
//
// The core executable (dbz3.exe in the variant folder) is a single dual-region
// binary: it contains BOTH the US/NA and EU/PAL codegens and activates the
// right one at runtime based on the default.xex the user supplied. The
// bootstrap only picks the ISA variant (the SDK DLLs differ); it passes the
// user's command line through and propagates the game's exit code.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace {

bool HasCpuX86V3() {
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, 0);
  const int max_leaf = regs[0];
  if (max_leaf < 7) {
    return false;
  }
  __cpuid(regs, 1);
  const bool fma = (regs[2] & (1u << 12)) != 0;
  const bool osxsave = (regs[2] & (1u << 27)) != 0;
  __cpuidex(regs, 7, 0);
  const bool avx2 = (regs[1] & (1u << 5)) != 0;
  const bool bmi1 = (regs[1] & (1u << 3)) != 0;
  const bool bmi2 = (regs[1] & (1u << 8)) != 0;
  if (!(avx2 && bmi1 && bmi2 && fma && osxsave)) {
    return false;
  }
  // The OS must actually save the AVX register state (XCR0 bits 1|2). Only
  // executed when OSXSAVE is set, otherwise xgetbv would fault.
  const uint64_t xcr0 = _xgetbv(0);
  if ((xcr0 & 0x6) != 0x6) {
    return false;
  }
  return true;
}

std::wstring ExecutableDir() {
  wchar_t path[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path, len);
  const size_t slash = full.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : full.substr(0, slash);
}

std::wstring ErrorCodeText(DWORD code) {
  switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return L"carpeta o ejecutable no encontrado";
    case ERROR_BAD_EXE_FORMAT:
      return L"formato de ejecutable no valido";
    case ERROR_DLL_NOT_FOUND:
      return L"no se encontro una DLL necesaria";
    case ERROR_DLL_INIT_FAILED:
      return L"fallo al inicializar una DLL (posible instruccion no soportada)";
    case ERROR_NOT_ENOUGH_MEMORY:
      return L"memoria insuficiente";
    default:
      break;
  }
  wchar_t buf[16] = {};
  swprintf(buf, 16, L"0x%08X", code);
  return std::wstring(buf);
}

void ShowError(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"DBZ Budokai 3 - Error",
              MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const std::wstring exe_dir = ExecutableDir();
  const bool v3 = HasCpuX86V3();

  const std::wstring variant = v3 ? L"dbz3_avx2" : L"dbz3_legacy";
  const std::wstring child = exe_dir + L"\\" + variant + L"\\dbz3.exe";

  // Rebuild the child command line: quoted child path + original arguments.
  std::wstring cmd = L"\"" + child + L"\"";
  if (argv != nullptr) {
    for (int i = 1; i < argc; ++i) {
      const std::wstring arg(argv[i]);
      const bool needs_quote = arg.find_first_of(L" \t") != std::wstring::npos;
      if (needs_quote) {
        cmd += L" \"" + arg + L"\"";
      } else {
        cmd += L" " + arg;
      }
    }
    LocalFree(argv);
  }

  PROCESS_INFORMATION pi = {};
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  // Working directory = the game root (same folder as this bootstrap), so
  // relative paths resolve the same way as running the game directly.
  if (!CreateProcessW(child.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0,
                      nullptr, exe_dir.c_str(), &si, &pi)) {
    const DWORD err = GetLastError();
    ShowError(
        L"DBZ Budokai 3 HD Collection no pudo iniciar la variante " + variant +
        L".\n\n"
        L"El juego se ejecutara en modo " +
        (v3 ? L"optimizado (AVX2)." : L"compatible (sin AVX2).") +
        L"\n\n"
        L"Motivo: " + ErrorCodeText(err) + L" (" + variant + L"\\dbz3.exe).\n"
        L"Asegurate de que la carpeta " + variant +
        L" este completa junto a este ejecutable.");
    return 1;
  }

  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);

  // Exception-ish exit codes (>= 0x80000000, e.g. 0xC0000005) mean the child
  // crashed; surface a hint instead of silently returning the status.
  if (exit_code >= 0x80000000u) {
    wchar_t buf[32] = {};
    swprintf(buf, 32, L"0x%08X", exit_code);
    ShowError(L"DBZ Budokai 3 HD Collection se cerro con un error.\n\n"
              L"Codigo de salida: " + std::wstring(buf) +
              L"\n\nComprueba el registro en la carpeta logs/ junto al juego "
              L"(variante " + variant + L").");
  }
  return static_cast<int>(exit_code);
}