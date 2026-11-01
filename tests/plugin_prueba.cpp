// Plugin minimo para probar el cargador (issue #23). Se compila a mano a plugins/prueba.dll de una copia de prueba:
//   clang++ -shared -O1 -I src -o <copia>/plugins/prueba.dll tests/plugin_prueba.cpp
// En el log debe salir "plugin prueba.dll: ok api=1 ..." y, al cerrar, "plugin prueba.dll: adios".
#include <cstdio>
#include "rex_mod.h"

static const RexModHost* g_host;

extern "C" __declspec(dllexport) void rex_mod_init(const RexModHost* h) {
  g_host = h;
  char msg[256];
  RexModPPCFunc* f = h->get_function(0x820922A0u);   // sub_820922A0 (sonido) de la imagen US
  const int set = f ? h->set_function(0x820922A0u, f) : 0;    // se reinstala ella misma: no cambia nada
  std::snprintf(msg, sizeof msg, "ok api=%u size=%u version=%s region=%s base=%d func=%d set=%d", h->api_version,
                h->struct_size, h->port_version, h->region, h->guest_base != nullptr, f != nullptr, set);
  h->log("prueba.dll", msg);
}

extern "C" __declspec(dllexport) void rex_mod_shutdown() { g_host->log("prueba.dll", "adios"); }
