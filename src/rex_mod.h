// rex_mod.h — ABI publica de los plugins de codigo (issue #23). Copiala tal cual en tu plugin.
//
// El juego carga cada DLL de la carpeta "plugins" junto a dbz3.exe (o .so en Linux) solo si el jugador
// activa "Plugins de codigo" en el launcher (cvar dbz3_plugins). Exporta, con enlace C:
//   void rex_mod_init(const RexModHost* host);   // una vez, con el codigo del juego cargado y antes de que arranque
//   void rex_mod_shutdown(void);                 // opcional, al cerrar el juego
// El puntero host vale toda la partida. Comprueba api_version antes de usar campos nuevos: solo se anaden al final.
#pragma once
#include <stdint.h>

#define REX_MOD_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// Funcion recompilada del juego: void f(PPCContext& ctx, uint8_t* base).
typedef void RexModPPCFunc(void* ctx, uint8_t* base);

typedef struct RexModHost {
  uint32_t struct_size;      // sizeof(RexModHost) del juego
  uint32_t api_version;      // REX_MOD_API_VERSION del juego
  const char* port_version;  // p. ej. "1.5.5.6" (vacio en Linux)
  const char* region;        // "us" o "eu" (cambia las direcciones del juego)
  uint8_t* guest_base;       // memoria del juego: direccion guest A = guest_base + A (big-endian)
  // Funcion recompilada en una direccion guest (0x82xxxxxx), o NULL si no hay.
  RexModPPCFunc* (*get_function)(uint32_t guest_address);
  // Sustituye la entrada de la tabla de funciones de esa direccion. 1 = hecho. OJO: la tabla solo la usan las
  // llamadas indirectas (punteros, vtables); el codigo recompilado se llama directamente entre si. Para enganchar
  // todas las llamadas, desvia (detour) el puntero que da get_function con tu libreria de hooks.
  int (*set_function)(uint32_t guest_address, RexModPPCFunc* func);
  // Escribe una linea en el log del juego (logs/), con el nombre del plugin delante.
  void (*log)(const char* plugin, const char* message);
} RexModHost;

#ifdef __cplusplus
}
#endif
