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
| `data_location.json` portable/AppData (`previousPath`) | `user_data/` junto al exe; **falta** fallback a `%LOCALAPPDATA%` si no es escribible |
| `backend.wasPresetChosen` (asistente 1ª vez) | `dbz3_quality_preset=auto` (detección GPU) |
| `config.json` plano con claves con punto | `dbz3_user.toml` con escape de rutas Windows |
| Update check en prelaunch (checking/available/failed + descarga) | **implementado** (arriba) |
| `game.enableFpsOverlay` + esquina configurable | contador FPS solo en Dev |
| `achievements.json`, `texture_replacements/` | fuera de alcance (nuestro equivalente: Texturas/Model Swap) |
| crashpad + pipeline cache | minidump propio + `dbz3_perf_logging` |

Pendiente (no hecho en esta sesión, por decisión del usuario): knob
`async_shader_compilation` (stutter), `occlusion_query_enable`, FXAA
(`swap_post_effect`), dither, overscan y sensibilidad de ratón; y el fallback
`user_data` a `%LOCALAPPDATA%`.
