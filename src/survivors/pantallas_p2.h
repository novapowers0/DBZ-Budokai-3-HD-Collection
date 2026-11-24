// Dragon Budokai Survivors, paquete P2 (pantallas, menu y meta-progresion): todo el estado propio de las pantallas nuevas en UNA estructura, para que
// la clase Modo (modo_interno.h, del coordinador) solo lleve un miembro `EstadoP2 p2_`. Solo cabecera: sin archivo nuevo en CMake.
// Ajustes (apagados de serie, definidos en modo_pantallas.cpp): dbz3_survivors_menu_v2 (portada y menu, seleccion v2, clasificacion) y
// dbz3_survivors_meta_v2 (subida de nivel con rareza, cofre, resultados v2, tienda v2, logros y coleccion).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, dbz3_survivors_menu_v2);   // definidos en modo_pantallas.cpp
REXCVAR_DECLARE(bool, dbz3_survivors_meta_v2);

namespace dbz3::survivors::interno {

inline bool MenuV2() { return REXCVAR_GET(dbz3_survivors_menu_v2); }
inline bool MetaV2() { return REXCVAR_GET(dbz3_survivors_meta_v2); }

// Opciones del menu v2 (Progreso::opc_*), espejo para que P1 (sacudida de camara) y P3 (volumen) las lean sin tocar el progreso:
// interno::g_opciones.sacudida / .volumen (0..100). Solo valen con dbz3_survivors_menu_v2 encendido.
struct OpcionesSurv { int volumen = 100; bool sacudida = true; };
inline OpcionesSurv g_opciones;

struct EstadoP2 {
  // ---- A12 portada y estaciones
  int portada_sel = 0;                // 0 Jugar, 1 Tienda, 2 Coleccion, 3 Logros, 4 Clasificacion, 5 Como jugar, 6 Opciones
  uint8_t volver_a = 1;               // pantalla (Pantalla::) a la que vuelve la tienda: 1 = Eleccion de siempre, 7 = Portada
  int paso_prueba = -1;               // survivors_menu: estacion que visita el recorrido automatico (-1 = no hay)
  float t_prueba = 0;
  // ---- A15 clasificacion
  int cla_tab = 0, cla_fil_luchador = 0, cla_fil_escenario = 0, cla_fil_dificultad = 0, cla_fila = 0;
  // ---- C08 coleccion / logros
  int col_tab = 0, col_sel = 0, log_sel = 0;
  // ---- Opciones
  int opc_sel = 0;
  // ---- A10 subida de nivel v2
  float sub_msg_t = -100;             // aviso breve de la subida ("faltan zeni", "desterrada")
  char sub_msg[64] = {};
  // ---- A11 cofre
  float cofre_t = 0;                  // segundos desde que se abrio el cofre
  // ---- C08 avisos de logros en partida (cola: uno cada vez)
  std::vector<int> logro_cola;
  float logro_t = -100;
  int logro_mostrado = -1;
  // ---- A14 resultados
  int res_partidas = -1;              // prog_.partidas ya procesado en los resultados
  bool res_record = false;
  std::vector<int> res_logros;        // logros que saltaron en esta partida
  uint64_t ult_pasos = 0;             // para detectar partida nueva
  int cofres = 0;                     // cofres abiertos en la partida (logros)
  // ---- registro (una vez por apertura)
  uint32_t dicho = 0;
};

}  // namespace dbz3::survivors::interno
