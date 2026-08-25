// dbz3 bootstrap - ISA dispatcher for the DBZ Budokai 3 HD Collection.
//
// The ReXGlue runtime DLLs are compiled with -march=x86-64-v3 (AVX2). CPUs
// without AVX2 crash with 0xc0000142 / 0xc000001d the moment the runtime is
// used. This tiny executable is compiled at the BASELINE x86-64 ISA (no
// -march), so it runs on any CPU. At startup it probes the CPU once (a single
// CPUID call) for the x86-64-v3 feature set and launches the matching build:
//
//   dbz3_avx2\dbz3_core.exe       (AVX2 capable CPUs - optimized runtime)
//   dbz3_legacy\dbz3_core.exe     (older CPUs - SSE4.2 runtime fallback)
//   dbz3_eu_avx2\dbz3_core.exe    (same, but recompiled from the EU/PAL xex)
//   dbz3_eu_legacy\dbz3_core.exe  (same, EU/PAL, older CPUs)
//
// The core executable (dbz3_core.exe) is the same baseline binary in both
// variant folders; only the SDK DLLs differ. Which xex the user supplied
// (default.xex, US/NA or EU/PAL) picks the US vs EU core, so a user can drop
// either executable and it just works. The bootstrap passes the user's command
// line through and propagates the game's exit code.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <wincrypt.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

// Which executable this build is a recompilation of. Each core only boots ONE
// xex: the US/NA core (dbz3_avx2|legacy) runs yae3_xenon.xex and the EU/PAL
// core (dbz3_eu_avx2|legacy) runs yae3_xenon_eu.xex. The bootstrap hashes the
// default.xex the user supplied and launches the matching core, so a user can
// drop either the US or the EU xex and it just works.
enum class XexKind { kUs, kEu, kUnknown };

bool XexMd5(const std::wstring& path, unsigned char out[16]) {
  FILE* f = nullptr;
  _wfopen_s(&f, path.c_str(), L"rb");
  if (!f) {
    return false;
  }
  HCRYPTPROV prov = 0;
  HCRYPTHASH hash = 0;
  bool ok = false;
  if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT) &&
      CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
    BYTE buf[65536];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      if (!CryptHashData(hash, buf, static_cast<DWORD>(n), 0)) {
        break;
      }
    }
    DWORD len = 16;
    if (CryptGetHashParam(hash, HP_HASHVAL, out, &len, 0) && len == 16) {
      ok = true;
    }
  }
  if (hash) {
    CryptDestroyHash(hash);
  }
  if (prov) {
    CryptReleaseContext(prov, 0);
  }
  fclose(f);
  return ok;
}

bool BytesEqual(const unsigned char* a, const unsigned char* b) {
  for (int i = 0; i < 16; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

// Fingerprint the supplied default.xex: US/NA = A53E..., EU/PAL = C37E....
XexKind DetectXex(const std::wstring& exe_dir) {
  // Look for default.xex next to the bootstrap or inside assets/ (the two
  // documented release layouts).
  std::wstring candidates[] = {
      exe_dir + L"\\default.xex",
      exe_dir + L"\\assets\\default.xex",
      exe_dir + L"\\..\\default.xex",
      exe_dir + L"\\..\\assets\\default.xex",
  };
  unsigned char md5[16];
  for (const auto& path : candidates) {
    if (XexMd5(path, md5)) {
      static const unsigned char kUsMd5[16] = {0xA5, 0x3E, 0x32, 0x4B, 0x5D, 0x2A, 0x65, 0xEB,
                                               0xCB, 0xF6, 0x48, 0xE4, 0xF8, 0x5A, 0x72, 0x71};
      static const unsigned char kEuMd5[16] = {0xC3, 0x7E, 0xB9, 0x79, 0xB7, 0x62, 0xDA, 0x0A,
                                               0xB5, 0xB8, 0xC9, 0xBA, 0x80, 0x37, 0xCE, 0x4E};
      if (BytesEqual(md5, kUsMd5)) {
        return XexKind::kUs;
      }
      if (BytesEqual(md5, kEuMd5)) {
        return XexKind::kEu;
      }
      return XexKind::kUnknown;
    }
  }
  return XexKind::kUnknown;
}

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
  const bool eu = DetectXex(exe_dir) == XexKind::kEu;

  std::wstring variant = v3 ? L"dbz3_avx2" : L"dbz3_legacy";
  if (eu) {
    variant = v3 ? L"dbz3_eu_avx2" : L"dbz3_eu_legacy";
  }
  const std::wstring child = exe_dir + L"\\" + variant + L"\\dbz3_core.exe";

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
        L"Motivo: " + ErrorCodeText(err) + L" (" + variant + L"\\dbz3_core.exe).\n"
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