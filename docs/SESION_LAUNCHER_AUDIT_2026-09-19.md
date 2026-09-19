# Sesión 2026-09-19 — Auditoría del launcher (controles muertos + update check)

> Escaneo del launcher tras la v1.2.3, comparado con **Dusklight**
> (`E:\Games\Dusk`, reimplementación nativa de Twilight Princess) para inspirar
> mejoras de eficacia. Objetivo: "el launcher funcionando lo mejor posible".

## 1. Método

Se cruzaron **todas** las cvars del launcher (`dbz3_*`, 35 definiciones en
`src/launcher/settings.cpp`) contra las **199 cvars registradas en el SDK**
(`REXCVAR_DEFINE_*` en `rexglue-sdk-0.10/src`), y se siguió cada una hasta su
consumidor real (`ApplyUserSettingsToSdk` / `ApplyRuntimeSettingsToSdk` /
plugins del SDK).

## 2. Controles MUERTOS encontrados (corregidos)

| Control | Cvar | Lo que escribía | Resultado |
|---|---|---|---|
| Volumen general / Musica / SFX / Voces (4 sliders) | `dbz3_master/music/sfx/voice_volume` | `SetSdkDouble("master_volume")` | **Muerto**: el SDK no tiene `master_volume`/`music_volume`/`sfx_volume`/`voice_volume` (0 hits) |
| (además) | — | `SetSdkBool("audio_mute", false)` fijo | El launcher **forzaba audio ON**: no se podía silenciar desde la UI |
| Gamma | `dbz3_gamma` | nada | **Muerto**: no existe cvar de gamma en el SDK (`gamma` solo aparece como parámetro/`gamma_render_target_as_unorm16`) |
| — | — | `SetSdkString("audio_output_device","")` | **Muerto**: cvar inexistente (línea huérfana) |
| Reset to defaults | — | lista fija | Olvidaba `dbz3_vrr` y `dbz3_hd_textures` |

**Causa raíz del diseño**: el guest mezcla TODOS los canales en un único stream,
así que el volumen por categoría (música/SFX/voz) no es separable en el host.

## 3. Cambios aplicados

### SDK (rexruntime) — `src/audio/sdl/sdl_audio_driver.cpp`
- Nueva cvar **`audio_gain`** (double, 0.0-1.0): `gain = GetOutputGain() *
  clamp(audio_gain, 0, 1)` en `SDLCallback`. `audio_mute` ya existía.
- Parche archivado en `github/patches/rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp`.

### Launcher
- **Audio real**: slider "Volumen general" → `dbz3_master_volume` → SDK
  `audio_gain`; checkbox "Silenciar todo el audio" → `dbz3_mute` → SDK
  `audio_mute`; ambos se aplican al instante (`ApplyRuntimeSettingsToSdk`).
  Se eliminaron los 3 sliders muertos (música/SFX/voces) y sus cvars.
- **Gamma**: slider eliminado (era decorativo).
- **Reset to defaults**: añadidos `dbz3_vrr=false`, `dbz3_hd_textures=1`,
  `dbz3_mute=false`; quitados gamma y los volúmenes muertos.
- **Update check** (`src/launcher/update_check.{h,cpp}`): consulta
  `api.github.com/repos/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest`
  en un hilo de fondo (WinHTTP, timeouts 8 s, User-Agent propio), compara
  `tag_name` con la versión leída del VERSIONINFO del propio exe (`src/version.rc`
  = fuente única) y muestra: verde "Nueva version disponible: vX" + botón
  "Descargar" (`ShellExecute`), "Version actualizada (vX)", o una nota gris si
  falla (nunca bloquea PLAY). Toggle de privacidad `dbz3_update_check` en el tab
  Dev. Linkea `winhttp` + `version` en CMake.
- **i18n**: 10 strings nuevas (ES/IT/DE/FR) en `i18n.cpp`.

## 4. Verificación

- Build SDK (`rexruntime`) + juego OK. **Trampa de DLL confirmada**: tras
  `cmake --build` del juego, `rexruntime.dll` vuelve a 10.863.616 B (stale) →
  recopiar el baseline (**10.873.856 B**, con `audio_gain` + `dbz3_perf_logging`).
- Launcher: captura `launcher_final.png` → muestra **"Version actualizada
  (v1.2.3)."** (el update check funciona de extremo a extremo) y ya **no** hay
  slider de Gamma.
- Audio funcional (log): toml de prueba con `dbz3_master_volume = 0.35` +
  `dbz3_mute = true` → `dbz3: applied runtime settings -> ... audio_gain=0.35
  mute=true ...`. La cvar existe y se aplica (antes el log imprimía
  `master_vol=0` porque no existía).
- Herramienta nueva: `tools/click_window.ps1` (click por coordenadas cliente;
  en el launcher ImGui el click sintético no cambia de pestaña sin foco real —
  la verificación del tab Audio se hizo por log).

## 5. Dusklight — comparación (qué copiamos y qué no)

| Dusklight | Nosotros |
|---|---|
| `data_location.json` portable/AppData (`previousPath`) | `user_data/` junto al exe; **fallback automático** a `Documents/dbz3` si la carpeta del exe no es escribible (abajo) |
| `backend.wasPresetChosen` (asistente 1ª vez) | `dbz3_quality_preset=auto` (detección GPU) |
| `config.json` plano con claves con punto | `dbz3_user.toml` con escape de rutas Windows |
| Update check en prelaunch (checking/available/failed + descarga) | **implementado** (arriba) |
| `game.enableFpsOverlay` + esquina configurable | contador FPS solo en Dev |
| `achievements.json`, `texture_replacements/` | fuera de alcance (nuestro equivalente: Texturas/Model Swap) |
| crashpad + pipeline cache | minidump propio + `dbz3_perf_logging` |

Pendiente (no hecho): `present_safe_area_x/y` (overscan de TV) y la esquina
configurable del contador de FPS.

## 6. Segunda tanda (misma fecha) — knobs de GPU + datos de usuario (v1.2.4 EX)

Implementado lo que quedaba apuntado arriba (excepto overscan y la esquina del
FPS). Cinco controles nuevos en el launcher, todos cableados a cvars REALES del
SDK, más el fallback de datos de usuario.

### Controles nuevos

| Launcher | Cvar launcher | Cvar SDK | Dónde |
|---|---|---|---|
| Suavizado de bordes (FXAA) | `dbz3_fxaa` (`none`/`fxaa`/`fxaa_extreme`) | `swap_post_effect` | Escalado |
| Tramado de color (dither) | `dbz3_present_dither` | `present_dither` | Escalado |
| Sensibilidad del ratón | `dbz3_mnk_sensitivity` (0.1-5.0) | `mnk_sensitivity` | Controles (solo con ratón activado) |
| Compilar shaders en segundo plano | `dbz3_async_shaders` | `async_shader_compilation` | Desarrollo |
| Consultas de oclusión del juego | `dbz3_occlusion_queries` | `occlusion_query_enable` | Desarrollo |

FXAA corre **antes** del escalado (`swap_post_effect`), así que se combina con
FSR/CAS: es la vía barata de antialiasing para GPUs que no pueden con la escala
interna ni el MSAA. Los dos toggles de GPU (async shaders, occlusion queries)
son **palancas de diagnóstico** del problema de rendimiento reportado: permiten
descartar (o confirmar) tirones por compilación de shaders y esperas por
oclusión sin recompilar nada.

### Datos de usuario escribibles (`settings.cpp`)

`UserDataRoot()` decide dónde viven saves/memory cards + cachés
(`user_data/dbz3`, `xex_cache`, `iso_cache`) y `UserSettingsPath()` dónde va
`dbz3_user.toml`:

1. Si `<exe_dir>/user_data/dbz3` (o la carpeta del exe, para el toml) es
   escribible → **portable**, como hasta ahora (sin cambios para nadie).
2. Si no (instalado en `Program Files`, recurso de red, OneDrive bloqueado) →
   `Documents/dbz3` (el `GetUserFolder()` del SDK = `FOLDERID_Documents`, que es
   justo el default del runtime cuando `user_data_root` está vacío).

La comprobación es una sonda real (crear carpeta + escribir/borrar
`.dbz3_write_test`), cacheada (una sola vez por proceso; el tab Dev muestra la
ruta y si es portable). Sin esto, en una carpeta de solo lectura el guardado
fallaba **en silencio** y los ajustes/saves se perdían.

### Tercera pasada — el update check, pulido (mismo tag `v1.2.4-EX`)

El aviso de versión era lo único visible de la release que el usuario no podía
controlar, así que el mismo asset se reemplazó con:

1. **Repacks** (commit `7a96385`, ya en el asset previo): `VersionNewer` compara
   4 componentes e `IsRepack()` marca un tag con sufijo (`1.2.4-EX`) o un
   FileVersion con build > 0 (`1.2.4.1`) ⇒ **1.2.4 < 1.2.4-EX < 1.2.5**; un EX
   instalado compara empate y no se auto-avisa, un 1.2.4 sí recibe el aviso.
   Antes el aviso no distinguía repacks (un 1.2.4 nunca vería la EX).
2. **Versión instalada siempre en el header**: `CurrentVersionLabel()`
   (`"1.2.4 EX"`) + estado + botones, vía `ImGui::TextDisabled` + `SmallButton`.
3. **Re-chequeo manual**: `RequestUpdateCheck()` (botón "Buscar actualizaciones"
   al estar al día, "Reintentar" si falló) con guardia `g_inflight` (una petición
   a la vez); `StartUpdateCheck()` conserva la idempotencia por frame
   (`g_autostarted`). El estado vuelve a "Buscando actualizaciones..." al repetir.
4. i18n: +3 strings y eliminada la variante `Version actualizada (vX)` (ya
   redundante con la línea de versión instalada).

### Verificación

- **Funcional (log)**: toml con `dbz3_fxaa="fxaa_extreme"`,
  `dbz3_present_dither=true`, `dbz3_async_shaders=false`,
  `dbz3_occlusion_queries=false`, `dbz3_mnk_sensitivity=2.5` →
  `dbz3: applied runtime settings -> ... fxaa=fxaa_extreme dither=true
  async_shaders=false occ_queries=false mnk_sens=2.5 ...`. Los valores se leen
  del registro de cvars del **SDK** (no del launcher), así que confirman que el
  cableado llega al runtime. 0 errores.
- **Fallback**: `icacls <user_data/dbz3> /deny javie:(W)` → el juego creó
  `Documents/dbz3/cache` y siguió funcionando sin errores. ACL retirado y
  carpeta de prueba borradas después.
- **UI**: captura del launcher OK. El header quedó `Version instalada: v1.2.4 EX
  | Version actualizada. | [Buscar actualizaciones]` (imagen
  `%TEMP%\opencode\launcher_ua.png`), es decir: el EX instalado **no** se
  auto-avisa y la versión que corre está siempre a la vista. Sin `[error]`/
  `Assert` en `logs/dbz3_011.log`. El click sintético sigue sin cambiar de pestaña
  (ImGui + foco real), así que los tabs nuevos no se capturaron por imagen.
- ⚠️ **DLLs**: el build sobrescribe `rexruntime.dll`/`rexgpu-xenos.dll` con los
  stale (`rexglue/bin`); tras compilar hay que recopiar del baseline. Los
  canónicos ACTUALES del baseline son `rexruntime.dll` **10.873.856 B** y
  `rexgpu-xenos.dll` **6.202.368 B** (el 6.165.504 B que cita AGENTS §7 es
  viejo). `verify_release.ps1` compara por SHA256 contra ese baseline.

