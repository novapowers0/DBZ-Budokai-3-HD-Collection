// plugins.cpp — cargador oficial de plugins de codigo (issue #23: mod de accesibilidad Budokai3Access).
// Apagado de serie: es codigo de terceros con el mismo acceso que el juego. Con dbz3_plugins activo, carga cada
// DLL de <carpeta de dbz3.exe>/plugins en orden alfabetico y le pasa RexModHost (rex_mod.h). Todo queda en el log.
#include "plugins.h"
#include "rex_mod.h"
#include "eu_map.h"
#include "launcher/update_check.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

REXCVAR_DEFINE_BOOL(dbz3_plugins, false, "DBZ3/Mods",
                    "Carga los plugins de codigo de la carpeta plugins (codigo de terceros: solo los que conozcas)");

namespace dbz3::plugins {
namespace {

using InitFn = void (*)(const RexModHost*);
using ShutdownFn = void (*)();

struct Cargado {
  std::string nombre;
  ShutdownFn shutdown;
};
std::vector<Cargado> g_cargados;
RexModHost g_host{};

rex::runtime::FunctionDispatcher* Despachador() {
  auto* rt = rex::Runtime::instance();
  return rt ? rt->function_dispatcher() : nullptr;
}
RexModPPCFunc* GetFunction(uint32_t a) {
  auto* d = Despachador();
  return d ? reinterpret_cast<RexModPPCFunc*>(d->GetFunction(a)) : nullptr;
}
int SetFunction(uint32_t a, RexModPPCFunc* f) {
  auto* d = Despachador();
  if (!d || !f || !d->GetFunction(a)) return 0;   // solo funciones que existen: nada de entradas nuevas
  REXLOG_INFO("dbz3 plugins: funcion {:08X} sustituida", a);
  return d->SetFunction(a, reinterpret_cast<::PPCFunc*>(f)) ? 1 : 0;
}
void Log(const char* plugin, const char* msg) {
  REXLOG_INFO("plugin {}: {}", plugin ? plugin : "?", msg ? msg : "");
}

std::filesystem::path CarpetaExe() {
#ifdef _WIN32
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  return std::filesystem::path(self).parent_path();
#else
  return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

}  // namespace

void CargarTodos() {
  if (!REXCVAR_GET(dbz3_plugins)) return;
  const auto dir = CarpetaExe() / "plugins";
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    REXLOG_INFO("dbz3 plugins: activados, pero no existe la carpeta {}", dir.string());
    return;
  }
#ifdef _WIN32
  const char* ext = ".dll";
#else
  const char* ext = ".so";
#endif
  std::vector<std::filesystem::path> dlls;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec))
    if (e.is_regular_file() && e.path().extension() == ext) dlls.push_back(e.path());
  std::sort(dlls.begin(), dlls.end());

  static const std::string version = dbz3::launcher::CurrentVersion();   // la del exe (version.rc)
  g_host = {sizeof(RexModHost), REX_MOD_API_VERSION, version.c_str(), g_guest_eu ? "eu" : "us",
            rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr,
            GetFunction, SetFunction, Log};
  for (const auto& p : dlls) {
    const std::string nombre = p.filename().string();
#ifdef _WIN32
    HMODULE h = LoadLibraryW(p.wstring().c_str());
    auto sym = [&](const char* s) { return h ? reinterpret_cast<void*>(GetProcAddress(h, s)) : nullptr; };
#else
    void* h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto sym = [&](const char* s) { return h ? dlsym(h, s) : nullptr; };
#endif
    if (!h) {
      REXLOG_WARN("dbz3 plugins: no se pudo cargar {}", nombre);
      continue;
    }
    auto init = reinterpret_cast<InitFn>(sym("rex_mod_init"));
    if (!init) {
      REXLOG_WARN("dbz3 plugins: {} no exporta rex_mod_init; se ignora", nombre);
      continue;   // ponytail: la DLL queda cargada (descargarla podria romper hilos que ya creo en DllMain)
    }
    REXLOG_INFO("dbz3 plugins: cargando {} ({} bytes, API {})", nombre,
                std::filesystem::file_size(p, ec), REX_MOD_API_VERSION);
    init(&g_host);
    g_cargados.push_back({nombre, reinterpret_cast<ShutdownFn>(sym("rex_mod_shutdown"))});
  }
  REXLOG_INFO("dbz3 plugins: {} cargado(s)", g_cargados.size());
}

void CerrarTodos() {
  for (auto it = g_cargados.rbegin(); it != g_cargados.rend(); ++it)
    if (it->shutdown) it->shutdown();
  g_cargados.clear();
}

}  // namespace dbz3::plugins
