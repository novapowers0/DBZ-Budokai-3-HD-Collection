# Sesión: limpieza de mods + cierre/pulido del Model Swap HD↔HD (2026-09-14)

Cierre del bloque de model swaps. Objetivo del usuario: (1) barrido de mods,
(2) dejar cerrado/pulido el **swap nativo B3 HD↔HD**, (3) dejar claro que las
**`.iso` no aprovechan el sistema de mods**, (4) refactor/modernización del
apartado de mods (QoL + visual).

## 1. Barrido de mods

`out/build/win-amd64-release/mods/` tenía **90 mods** (casi todos tests
experimentales a lo largo del tiempo). Se movieron **83 a
`out/build/win-amd64-release/mods_archivo/`** (sin borrar nada) y se conservaron
**7 útiles**:

| Conservado | Qué es |
|---|---|
| `cell_native` | Swap nativo HD↔HD Cell Forma 2 (147) → Krillin (327). Validado. |
| `sw_goten_nativo`, `sw_vegeta424` | Swaps nativos HD↔HD validados. |
| `cell_best2`, `cell_win2` | Port PS2→HD (Vía A, aproximado) — referencia. |
| `goku_armadura` | Swap de cabeza HD↔HD (parcial). |
| `og_music` | Contenido real (música original). |

- Todos quedan **desactivados** (juego vanilla por defecto).
- `mods/README.md` (guía + tabla) y `mods_archivo/README.md` (categorías)
  añadidos.
- `mods_archivo/` está **fuera de `mods/`**, así que el runtime nunca lo escanea
  ni el empaquetador de release lo incluye.

## 2. Model Swap B3 HD↔HD — cerrado y pulido

Pipeline ya validado (`swap_b3.py` + `catalog_b3.cat` + mid-insert virtual).
Pulidos:
- **Combos con buscador** (183 personajes): filtro por texto dentro del desplegable,
  cada fila muestra `[bin N]` y `[NO JUGABLE]`.
- **Tarjeta de vista previa** (origen/destino, bin/slot, avisos de no-jugable).
- **Guardia origen==destino** (en la UI y en `mod_pipeline.cpp`).
- **Aviso en modo ISO** + botón de swap **deshabilitado** (el mod no se aplicaría).
- `swap_b3.py` ahora escribe el manifest con **nombres del catálogo**
  (`name=Cell Forma 2 en Krillin`, `type=swap_b3`, `source`/`target` con nombre y
  bin) en vez de solo números.
- Eliminado el log de diagnóstico temporal `pipeline_cmd.log`.

## 3. Modo disco (ISO) — los mods NO se aplican

Al jugar del `.iso` los overrides por entrada AFS resuelven a ficheros host de la
carpeta extraída → **ningún mod tiene efecto**. Se dejó explícito:
- Banner de validación (ya existía) + **aviso ámbar en la pestaña Mods** +
  **aviso ámbar en Model Swap**.
- El **botón "Cambiar B3→B3" se deshabilita** en modo ISO.
- i18n en los 5 idiomas (verificado: **0 gaps** con auditoría de claves).

## 4. Refactor del Centro de mods

`src/launcher/launcher_state.cpp` (`DrawModsTab`):
- **Lista cacheada** (`mods_cache_` + `mods_loaded_`): ya no re-escanea el disco
  (y cuenta archivos recursivamente) en cada frame; se invalida tras toggles,
  instalar, editar, aplicar perfil o pulsar "Refrescar".
- **Buscador** por nombre/título/descripción/autor/origen/destino/tipo.
- **Activar todos / Desactivar todos / Refrescar / Abrir carpeta**.
- **Badges de tipo** con color (pill) + `ON` verde.
- **Filas alternas** (`ImGuiTableFlags_RowBg`) y estado vacío / sin-resultados.
- Helpers nuevos: `IContains`, `DrawBadge`, `CharacterCombo` (combo con filtro,
  reutilizado en Model Swap y Texturas).

## 5. Archivos tocados

- `src/launcher/launcher_state.h` — caché de mods + buffers de búsqueda.
- `src/launcher/launcher_state.cpp` — helpers + `DrawModsTab` / `DrawModelSwapTab`
  / `DrawTexturesTab`.
- `src/launcher/i18n.cpp` — entradas nuevas de traducción (ES/IT/DE/FR).
- `src/launcher/mod_pipeline.cpp` — quitar log temporal + guardia origen==destino.
- `mod center hd/swap_b3.py` — manifest con nombres.
- `mods/README.md`, `mods_archivo/README.md` (nuevos).

## 6. Verificación

- Build release **sin warnings**; `rexruntime.dll` 10863616 y `rexgpu-xenos.dll`
  6165504 (canónicas, sin instrumentación).
- Auditoría i18n: 209 claves de call-site, **0 faltantes** en la tabla.
- `swap_b3.py --origen 147 --dest 327` genera el mod y el manifest esperado.
- Sin mods activos por defecto.
