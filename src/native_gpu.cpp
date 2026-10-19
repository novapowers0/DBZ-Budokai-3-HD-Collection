// dbz3 - Motor grafico "Direct3D 12 nativo": lanzamiento desde el launcher (ver native_gpu.h).
#include "native_gpu.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace dbz3::native_gpu {
namespace {
std::filesystem::path Dir() { return rex::filesystem::GetExecutableFolder() / "native_gpu"; }
constexpr const char* kFiles[] = {"dbz3-native-renderer.dll", "materials.hlsl", "materials.dbzregistry"};
}  // namespace

bool Supported() {
#if defined(DBZ3_NATIVE_GPU)
  return true;
#else
  return false;
#endif
}

bool ChildActive() {
  const char* value = std::getenv("DBZ3_NATIVE_GRAPHICS");
  return Supported() && value && std::string(value) == "1";
}

std::string MissingFile() {
  for (const char* name : kFiles)
    if (!std::filesystem::is_regular_file(Dir() / name)) return rex::path_to_utf8(Dir() / name);
  return {};
}

bool LaunchChild() {
#if REX_PLATFORM_WIN32
  if (!Supported()) return false;
  if (const std::string missing = MissingFile(); !missing.empty()) {
    REXLOG_ERROR("dbz3: motor D3D12 nativo: falta {}; se juega con Direct3D 12", missing);
    return false;
  }
  const std::filesystem::path dir = Dir();
  // los lee native_gpu/lab (native_stream.cpp, native_services.cpp) en el proceso hijo
  SetEnvironmentVariableW(L"DBZ3_NATIVE_GRAPHICS", L"1");
  SetEnvironmentVariableW(L"DBZ3_NATIVE_STREAM", L"1");
  SetEnvironmentVariableW(L"DBZ3_NATIVE_RENDERER", (dir / "dbz3-native-renderer.dll").c_str());
  SetEnvironmentVariableW(L"DBZ3_NATIVE_SHADERS", (dir / "materials.hlsl").c_str());
  SetEnvironmentVariableW(L"DBZ3_NATIVE_MATERIALS", (dir / "materials.dbzregistry").c_str());
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring cmd(GetCommandLineW());
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  const BOOL ok = CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  SetEnvironmentVariableW(L"DBZ3_NATIVE_GRAPHICS", nullptr);   // si no arranco, este proceso sigue como siempre
  if (!ok) {
    REXLOG_ERROR("dbz3: motor D3D12 nativo: CreateProcess fallo (WinError {})", GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  REXLOG_INFO("dbz3: motor D3D12 nativo: juego relanzado (pid {})", pi.dwProcessId);
  return true;
#else
  return false;
#endif
}
}  // namespace dbz3::native_gpu
