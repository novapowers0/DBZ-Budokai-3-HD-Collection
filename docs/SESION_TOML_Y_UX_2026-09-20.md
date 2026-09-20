# Sesión 2026-09-20 — Autorreparación del TOML + UX anti-abuso de la escala

> Cierra los problemas reales de los logs de SSGPrinceVegeta (v1.2.1) y el
> malentendido del consumo de GPU. **Sin commit+push hasta validación del usuario.**

## 1. Problemas reales en los logs de Prince Vegeta (v1.2.1)

Revisados sus 27 logs (`Logs SSGPrinceVegeta/`), solo aparecen **dos** errores
únicos:

1. `Failed to parse config ... unknown escape sequence '\G'` (en casi todos).
   Ruta `E:\Game Roms\...` guardada sin escapar → **se pierden TODOS los ajustes**.
2. `XThread::Execute - No function registered at 820D54C8` = arrancaba el menú de
   la HD Collection (ya cubierto por la autodetección de xex de la v1.2.2).

Config: `preset=ultra internal_scale=3x msaa aniso=5 fsr=quality sharp=0.2
vrr cap=60` + endpoint de audio `VB-Audio Virtual Cable` + `master_vol=0`.
`ultra` ya no existe → ahora es alias de `quality` (que baja la escala a 1x);
para conservar su 3x se usa `manual` + escala 3x.

## 2. Autorreparación del TOML (launcher, no SDK)

Antes: si el toml no parseaba, `rex::cvar::LoadConfig` tragaba la excepción y
seguía con **defaults**, perdiendo la config en silencio (y el cierre
autoguardaba encima → destruía el fichero).

Ahora (`src/launcher/settings.cpp`):

- `TomlParses(path)`: valida con **toml++** leyendo el **texto** del fichero, no
  `parse_file(path.string())` (la ruta en Windows es ANSI → una carpeta con
  no-ASCII daría un falso "corrupto").
- Si no parsea → `EscapeTomlStrings(path)` (idempotente) y reintenta:
  - **reparado** → `ConfigLoadState::kRepaired` (aviso **verde**).
  - **sigue roto** → `ConfigLoadState::kInvalid`, **copia a
    `dbz3_user.toml.bak`** y no carga (aviso **rojo**); el autoguardado del
    cierre puede escribir defaults pero el original queda en `.bak`.
- `LastConfigLoadState()` → el launcher lo pinta **arriba de los tabs**
  (`launcher_state.cpp`, junto al aviso de datos del juego).
- ⚠️ `LoadUserSettings` corre **dos veces** por arranque (OnConfigurePaths +
  OnPreSetup). El estado `kRepaired`/`kInvalid` **se preserva** entre llamadas
  (si no, el segundo pase lo pisa con `kOk` y el aviso no se ve).
- `#include <toml++/toml.hpp>` disponible vía `rex::runtime` (no toca CMake).

Verificado: toml corrupto → reparado (`\\` escapado) e **idempotente**; toml
irreparable → `.bak` creado y original intacto; captura del launcher con el
aviso verde y el naranja.

## 3. UX anti-abuso de la escala interna (el consumo NO son las texturas HD)

Medido (RTX 4070 SUPER, combate, `dbz3_175..178`):

| Config | GPU | Potencia | VRAM |
|---|---|---|---|
| **1x + FSR** (nativo) | **22-23 %** | **29-30 W** | 1.35 GB |
| 3x interno (supersampling) | 51 % | 50 W | 2.9 GB |
| 3x + HD x4 (versión vieja) | **80 %** | **132 W** | 3.1 GB |

El consumo de su captura (80 %/132 W) era la **versión vieja con x4 HD**.
`draw_resolution_scale` hace que el guest **renderice de verdad a Nx**
(supersampling real); el FSR queda **inerte** (frontbuffer ≥ salida) y **no hay
pasada extra** (`presenter.cpp:1065`). **No hay bug.**

Acción (petición del usuario "mantener 3x pero avisar fuerte"), en el tab Video:

- Aviso naranja **envuelto** cuando `scale > 1` ("la GPU trabajara mucho mas…").
- **Botón "Volver a nativo (1x)"** de un clic (escala 1x + persiste).
- Etiquetas del combo con coste: "1x (nativa 720p) - recomendado" … "4x … solo
  GPUs de gama alta".
- Tooltip del preset: **ningún preset sube la escala**.
- Tooltip del MSAA: coste moderado, se puede quitar con escala alta.

## 4. i18n

Strings nuevos ES/EN/IT/DE/FR en `i18n.cpp` (aviso supersampling, botón nativo,
etiquetas de escala, tooltips, avisos de config recuperada/inválida).

## 5. Pendiente

- **Validación visual del usuario** del HUD (clamp anti-ringing + min size) —
  no capturable offscreen.
- **Commit+push** solo con el OK del usuario.
