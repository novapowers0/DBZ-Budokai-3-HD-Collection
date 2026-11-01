// plugins.h — cargador de plugins de codigo (ver rex_mod.h para la ABI que ven los plugins).
#pragma once

namespace dbz3::plugins {
void CargarTodos();   // OnPreLaunchModule: codigo del juego cargado, aun sin arrancar
void CerrarTodos();   // en cada salida (OnShutdown y el _Exit del cierre de ventana)
}  // namespace dbz3::plugins
