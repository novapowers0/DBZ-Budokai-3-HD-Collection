// Dragon Budokai Survivors: puerta del modo (ajuste dbz3_survivors, apagado de serie) y su fila del HUB.
#pragma once

namespace dbz3::survivors {

bool Activo();             // ajuste dbz3_survivors (o DBZ3_TEST_SURVIVORS_MODO en las pruebas)
void RegistrarEntrada();   // hub_panel.cpp, cada cuadro: registra la fila «Dragon Budokai Survivors» la primera vez que el ajuste esta encendido

// Pantalla del modo (modo.cpp). Cualquier hilo. Abrir() la pide desde el HUB; Cerrar() la suelta y el HUB vuelve a dibujarse
// (Abierto() es el `busy` de la fila: mientras sea true el HUB no se dibuja).
void Abrir();
void Cerrar();
bool Abierto();
// Cerrar() lo llama: la siguiente vez que la fila HUB se dibuja, RegistrarEntrada anota "dbz3 survivors: HUB dibujado de nuevo" (pruebas).
void AvisarVueltaHub();

}  // namespace dbz3::survivors
