// Dragon Budokai Survivors: ajuste dbz3_survivors y fila del HUB (como Dragon Rogue en modos.cpp: los ajustes aun no estan leidos en los
// inicializadores estaticos, asi que la fila se registra la primera vez que el HUB se dibuja con el ajuste encendido; sin el
// ajuste no se registra nada y el HUB queda como estaba). Apagar el ajuste con la fila ya registrada la deja hasta reiniciar.
#include "entrada.h"

#include <atomic>
#include <cstdlib>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "../ingame/hub_panel.h"
#include "recursos.h"   // RutaRecursos: la ficha del HUB

REXCVAR_DEFINE_BOOL(dbz3_survivors, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: entrada nueva del HUB con el modo de hordas en vista cenital (beta)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::survivors {

bool Activo() { return REXCVAR_GET(dbz3_survivors) || std::getenv("DBZ3_TEST_SURVIVORS_AUTO") != nullptr; }

namespace {
std::atomic<bool> g_vuelta{false};   // el modo se cerro: la proxima vez que se dibuje la fila HUB se anota (pruebas: el HUB volvio)
}  // namespace

void AvisarVueltaHub() { g_vuelta = true; }

void RegistrarEntrada() {
  if (g_vuelta.load() && g_vuelta.exchange(false)) REXLOG_INFO("dbz3 survivors: HUB dibujado de nuevo");
  static std::atomic<bool> hecho{false};
  if (hecho.load() || !Activo() || hecho.exchange(true)) return;
  const bool ok = dbz3::hub::RegisterEntry(
      "survivors", {"Dragon Budokai Survivors", "Dragon Budokai Survivors", "Dragon Budokai Survivors", "Dragon Budokai Survivors", "Dragon Budokai Survivors"},
      {"Aguanta 15 minutos contra hordas cada vez mayores: tus técnicas atacan solas y al subir de nivel eliges una mejora. "
       "Versión de prueba (beta).",
       "Survive 15 minutes against ever larger hordes: your techniques fire on their own and each level up lets you pick an "
       "upgrade. Test version (beta).",
       "Survis 15 minutes face à des hordes toujours plus grandes : tes techniques attaquent seules et à chaque niveau tu "
       "choisis une amélioration. Version d'essai (bêta).",
       "Überlebe 15 Minuten gegen immer größere Horden: Deine Techniken greifen von selbst an und bei jedem Levelaufstieg "
       "wählst du eine Verbesserung. Testversion (Beta).",
       "Resisti 15 minuti contro orde sempre più grandi: le tue tecniche attaccano da sole e a ogni livello scegli un "
       "potenziamento. Versione di prova (beta)."},
      &Abrir, &Abierto);
  REXLOG_INFO("dbz3 survivors: entrada del HUB {}", ok ? "registrada" : "sin sitio");
  // la ficha de arte de la tanda 3 como insignia de la fila (sin ella, la insignia generica modo_extra de siempre)
  if (ok) dbz3::hub::SetEntryImage("survivors", RutaRecursos() / "04_interfaz" / "hub_ficha_survivors.png");
}

// Abrir / Cerrar / Abierto: la pantalla del modo, en modo.cpp.

}  // namespace dbz3::survivors
