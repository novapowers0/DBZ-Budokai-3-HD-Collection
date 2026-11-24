// dbz3 - CLIMA (ficha 10): el cielo del escenario se oscurece durante el combate. Ver clima.cpp. Solo US.
#pragma once

#include <cstdint>

namespace dbz3::clima {

// Encendido: opcion dbz3_clima, regla de reto (Activar) o DBZ3_CLIMA en pruebas. Apagado por defecto.
bool Activo();
void Activar(bool on);   // regla de reto: true durante el reto, false al salir
// 0 despejado, 1 lluvia, 2 nieve (dbz3_clima_tiempo; DBZ3_CLIMA_TIEMPO en pruebas). 0 si el clima esta apagado.
int Precipitacion();
// Gancho de escenarios.cpp: se pide el escenario de un combate (sub_820C26E0): todo vuelve a empezar.
void EscenarioPedido();
// 0..1: cuanto es de noche ahora (el ciclo de sub_820C2058). Para las estrellas de escenarios.cpp.
float Noche();
// Pasa Noche() al cielo de noche de rexgpu-xenos (cvar dbz3_clima_noche). Cada cuadro de la interfaz.
void PublicarNoche();

}  // namespace dbz3::clima
