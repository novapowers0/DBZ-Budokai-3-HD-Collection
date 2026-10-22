#pragma once

#include <rex/ppc/context.h>
#include <cstdint>

namespace dbz3::save_autoload {

// Inicia la comprobacion y lectura en background de data.bin si no se ha iniciado ya.
void StartBackgroundRead();

// Devuelve si existe una partida guardada valida.
bool HasSave();

// Devuelve si la partida ya ha sido aplicada a la memoria guest.
bool IsApplied();

// Aplica los datos de la partida guardada a la memoria del guest (UI_STATE).
bool ApplySave(PPCContext& ctx, uint8_t* base);

// Tick por frame: lanza la lectura en background y aplica la partida durante el menu de loading/inicio.
void Tick(PPCContext& ctx, uint8_t* base);

}  // namespace dbz3::save_autoload
