# HOJA DE RUTA — Feedback de la comunidad (estabilidad, compatibilidad, nuevas funciones)

> Actualizado: 2026-08-25. Documento complementario a `HOJA_DE_RUTA.md` (que cubre
> model swaps/costumes/roster). Aquí se priorizan los **problemas técnicos y de
> ejecución reportados** por la comunidad tras la publicación, más las funciones
> nuevas solicitadas. Cada ítem indica esfuerzo/impacto y estado real actual.

> **Cambio 2026-08-21**: añadido **P0 — Actualizar a Rexglue 0.10.0** (el SDK
> recién salió y estamos en una versión anterior). Reescrita la sección
> "Compatibilidad de hardware" con la investigación real sobre el detector AVX2
> y los backends OpenGL/D3D11.
>
> **Cambio 2026-08-25**: **P0 COMPLETADA** — la migración a Rexglue 0.10.0 está
> hecha y **validada en juego** (mods + mid-insert virtual). Ver
> `MIGRACION_REXGLUE_010.md` §8. Solo queda actualizar release/README antes de
> subir a GitHub (pendiente hasta completar el plan general).

---

## RESUMEN DE LA DEMANDA COMUNITARIA

| Categoría | Reportes | Impacto percibido |
|---|---|---|
| **Inicio/ejecución** | No abre, se cierra tras "Play", "Entrypoint XEX not found" | Alto (bloquea primer uso) |
| **Compatibilidad CPU** | Requiere AVX2 → 0xc0000142 en CPUs antiguas | Alto (deja fuera a usuarios) |
| **Rendimiento** | 10 FPS en integradas; juego "acelerado" | Medio-alto |
| **Estructura de archivos** | Confusión `default.xex` / `assets/` vs raíz | Medio (fricción primer uso) |
| **Controles** | Teclado/botones no responden en juego | Medio |
| **Nuevas funciones** | Android nativo, online, mods fáciles de gestionar | Bajo-medio (visión a futuro) |

---

## PRIORIDAD 0 — ACTUALIZAR A REXGLUE 0.10.0 (fundación)

### 0.1 ✅ Subir el SDK a 0.10.0 — **HECHO y VALIDADO (2026-08-25)**
- **Por qué**: el proyecto usa una versión anterior de `rexglue-sdk`
  (base 0.9.x). Acaba de salir **0.10.0** (bump "chore: bump version floor to
  0.10.0"), y las nightlies `0.10.0-dev.*` ya traen mejoras relevantes:
  - **input**: `gate mnk mouse look at runtime`, `comma-list binds, modifier
    prefixes, optional mouse, rstick keys` → **directamente útil para el
    problema de controles** (P3).
  - **ui**: `imgui style hook` → útil para el launcher.
  - **cvar**: `track value source so launch args outrank env and config` →
    útil para el auto-guardado de ajustes (v1.0.4).
  - **system**: `report the guest arena base and unhandled access violations`,
    `deliver APCs through a trap frame to preserve guest register state`,
    `add cr2-cr4 and guest-owned fpscr to the non-volatile save area` →
    mejor crash-reporting y estabilidad del recompilado.
  - **filesystem**: `select the POSIX access mode instead of ORing O_RDONLY
    and O_WRONLY`.
- **Riesgo**: el SDK está "en desarrollo temprano" (breaking API). Actualizar
  puede romper integraciones propias:
  - El parche del runtime de este proyecto (mid-insert virtual en
    `afs.cpp`/`host_path_file.cpp`/`host_path_entry.cpp`, §13.6/§14) hay que
    **re-aplicarlo sobre la versión nueva** (los archivos en `github/patches/`).
  - Los fixes de input (`input_backend=xinput`), `CallInUIThreadSynchronous`
    timeout y el presenter pacing hay que re-validarlos.
- **Plan ejecutado**:
  1. SDK 0.10.0 compilado en paralelo en `rexglue-sdk-0.10/` (sin tocar el 0.9).
  2. Parche del runtime re-aplicado (9 archivos: 4 filesystem + 3 cvars +
     2 CMakeLists).
  3. Instalado en `rexglue/` (respaldo `rexglue_0.9/`), dbz3.exe compilado
     contra 0.10 (codegen regenerado: `REX_WEAK_FUNC` eliminado en 0.10).
  4. **Validado en juego**: mods (`swap_96_on_327`, `tex_91`) y mid-insert
     virtual (`sw_vegeta424`) funcionan.
- **Esfuerzo**: Medio-Alto (riesgo de breaking API). **Impacto**: Alto (base
  futura, fixes de estabilidad/input gratis). — **COMPLETADO**.

---

## PRIORIDAD 1 — PRIMER USO / ESTABILIDAD (bug de bloqueo)

### 1.1 ✅ Diagnóstico automático de layout + mensajes claros — **HECHO (2026-08-25)**
- **Problema**: "Entrypoint XEX not found" y confusión `assets/` vs raíz (§14.1 ya
  soporta ambos, pero el usuario no sabe CUÁL usar ni dónde poner cada cosa).
- **Qué se hizo**:
  - **Banner de validación en el launcher** (`launcher_state.cpp` OnDraw): tras el
    header muestra en verde `[OK] Datos del juego en: <ruta>` cuando encuentra
    `default.xex` + `us/`/`eu/` (región actual), o en rojo qué falta exactamente
    (default.xex / us / eu) cuando no.
  - **Botón "Seleccionar carpeta de datos..."**: abre el diálogo nativo de
    Windows, valida que la carpeta contenga `us/`/`eu/` o `default.xex`
    (`IsValidGameDataDir`), persiste en el cvar `dbz3_game_dir` (dbz3_user.toml)
    y **remonta el juego en caliente** (`dbz3::RelocateGameData` →
    `RemountGameDrive` re-registra `game:/d:` en la raíz nueva + re-aplica la
    región) — sin reiniciar.
  - **PLAY bloqueado** cuando faltan los assets (`BeginDisabled`), evitando el
    crash de arranque; el banner indica cómo arreglarlo.
  - `OnConfigurePaths` prioridad: arg CLI > `dbz3_game_dir` (override del
    launcher) > auto-detección (exe/assets/proyecto/padre). La raíz efectiva se
    registra en `dbz3::EffectiveGameRoot` (región.cpp) para que el montaje de
    región siempre use la carpeta elegida (el Runtime interno es fijo al Setup).
- **Esfuerzo**: Bajo. **Impacto**: Alto. — **COMPLETADO**.

### 1.2 ✅ Crash inmediato tras Play (sin mensaje) — **HECHO (2026-08-25)**
- **Problema**: varios usuarios "no abre o se cierra inmediatamente tras Play".
- **Qué se hizo** (`src/main.cpp` SetupCrashHandler):
  - La captura de minidump (`crash_*.dmp`, Dev tab) se mantiene.
  - Ahora, ante una excepción no controlada, se **muestra una ventana
    "DBZ Budokai 3 - Error"** con: código de excepción, dirección, **ruta del
    log** (`logs/dbz3_*.log` vía `LatestLogPath`) y ruta del minidump si se
    generó. Con depurador conectado se delega (`EXCEPTION_CONTINUE_SEARCH`).
  - `std::terminate` también muestra la ventana.
- **Esfuerzo**: Bajo-Medio. **Impacto**: Alto. — **COMPLETADO**.
- Pendiente (release): `README_PRIMER_ARRANQUE.txt` en el zip (ya hay sección en
  RELEASE_README, reforzar).

---

## PRIORIDAD 2 — COMPATIBILIDAD DE HARDWARE

### 2.1 ✅ Detector eficiente de AVX2 + build fallback — **HECHO (2026-08-25)**
- **Problema**: el SDK se compila con `-march=x86-64-v3` (implica AVX2). CPUs
  sin AVX2 (Intel pre-Haswell 4ª gen, AMD pre-Excavator 2015) → `0xc0000142`
  / `0xc000001d` (instrucción ilegal) al arrancar.
- **Hallazgo clave**: el exe del juego (dbz3.exe) se compila SIN `-march`
  (baseline) — el AVX2 vive SOLO en las DLLs del SDK (rexruntime, rexgpu-xenos,
  FFX). Por eso el core es un único binario y solo cambian las DLLs.
- **Qué se hizo**:
  - **Bootstrap `dbz3.exe`** (`src/bootstrap.cpp`, baseline x86-64, sin SDK):
    chequea CPUID UNA vez (AVX2+BMI1+BMI2+FMA+OSXSAVE+XGETBV) y lanza
    `dbz3_avx2\dbz3_core.exe` o `dbz3_legacy\dbz3_core.exe`, pasando los args
    y propagando el exit code. Ventana de error clara si falta la variante.
  - **Build v2 del SDK**: `out/build-win-vulkan-legacy` con `-march=x86-64-v2`
    + `REXGLUE_OUTPUT_DIR` (nueva cache var) → `out/win-amd64-legacy` con
    rexruntime/rexgpu-xenos/FFX v2.
  - **Mods walk-up**: `AfsModsRoot` (runtime) + `ModsRoot` (launcher) + 
    `ModsOutDir` (mod_pipeline) suben hasta 3 niveles buscando `mods/` (el
    core vive en subcarpeta en release).
  - **Release**: `tools/make_release.ps1` monta release-stage con dbz3.exe +
    dbz3_avx2/ + dbz3_legacy/ + toolkit + docs y genera el zip. README_PRIMER_
    ARRANQUE.txt documenta disposiciones y variantes.
- **Esfuerzo**: Medio. **Impacto**: Alto. — **COMPLETADO**.
- Pendiente: validar en una CPU sin AVX2 real y la confirmación visual de que
  los mods se listan desde la raíz.

### 2.2 ✅ Backends / rendimiento en máquinas modestas — **HECHO (parte práctica, 2026-08-25)**
- **Problema reportado**: "se debería integrar Direct3D9/10/11, no solo D3D12"
  por los bajos FPS en gráficas integradas/viejas.
- **Realidad de la investigación (Xenia, la base de ReXGlue)**: Xenia **NO
  soporta OpenGL ni Direct3D 11** — solo **D3D12 y Vulkan**. La emulación Xenos
  usa **fragment shader interlock / rasterizer-ordered views**, que solo
  existen de forma usable en D3D12 y Vulkan. **D3D12 es YA el backend MÁS
  compatible** de los dos. Un backport D3D11 es grande y ni Xenia lo ha hecho.
  → **NO perseguir OpenGL/D3D11** (inviable en esta arquitectura).
- **Lo práctico hecho**:
  - **Presets de calidad por GPU** (la pestaña Video): `dbz3_quality_preset`
    con Auto/Low/Medium/High/Ultra/Manual. `Auto` detecta la GPU vía DXGI
    (nombre + VRAM) y aplica el perfil recomendado en cada arranque
    (low: 1x+sin MSAA+bilinear; medium: 1x+FSR+4x aniso; high: 1x+MSAA+FSR+16x
    aniso; ultra: 2x+MSAA, solo manual). Instalaciones frescas → "auto";
    instalaciones con toml previo → migradas a "manual" (nunca se les cambia
    la config).
  - **Frame cap REAL a 30 FPS** (opción para integradas): parche
    `d3d12_presenter.cpp` que restaura el cvar `frame_cap` (0.10 lo había
    eliminado) → limita la presentación host (no la velocidad del juego).
  - El perfilado **Tracy** del path D3D12 (build win-amd64-tracy) queda como
    optimización fina opcional (requiere sesión de juego para datos reales).
- **Esfuerzo**: Bajo-Medio (presets+frame cap). **Impacto**: Medio. — COMPLETADO.

### 2.3 ✅ Frame pacing ("el juego corre acelerado") — **HECHO (2026-08-25)**
- **Problema**: usuarios ven el juego "a velocidad acelerada".
- **Hallazgo**: en el SDK 0.10 el pacing del guest lo hace el worker `vsync` de
  `GraphicsSystem` (vblank del guest a `1/video_mode_refresh_rate`). Con
  `vsync` OFF el vblank corre a ~1000 Hz → la lógica del juego corre ~16x
  rápida = "acelerado". El cvar `frame_cap` del 0.9 **ya no existe** en 0.10
  (el slider era placebo).
- **Qué se hizo**:
  - **`vsync` forzado a true en el juego** (el guest DEBE correr a 60 Hz; se
    eliminó el checkbox "VSync" del launcher y del menú Dev, que era un
    placebo/contraproducente → ahora muestra "Game speed: fixed 60 FPS").
  - **`frame_cap` real restaurado** (parche `d3d12_presenter.cpp`): throttle de
    la presentación host (no de la velocidad del juego). `dbz3_frame_cap`
    default 0 → **60** (instalaciones frescas con pacing correcto).
  - Documentada la opción "Frame cap" en la pestaña Video (60 fluido, 30 para
    integradas, 0 sin límite).
- **Esfuerzo**: Bajo. **Impacto**: Medio. — **COMPLETADO**.
- Pendiente: validar en juego el frame cap (60/30) y el preset auto en una
  máquina con integrada; perfilado Tracy opcional.

---

## PRIORIDAD 3 — CONTROLES

### 3.1 ✅ Teclado y mando robusto (compatibilidad SDL) — **HECHO (2026-08-25)**
- **Problema**: teclado/botones no responden en juego (hay antecedentes de cuelgues
  con `SDL_INIT_GAMEPAD` + RTSS/OBS → se pasó a XInput nativo, §4.1).
- **Diagnóstico**: el driver MnK del SDK (teclado→mando) ya existía completo pero
  `mnk_mode` estaba en `false` por defecto y nadie lo activaba → el teclado no hacía
  nada. Los sliders de deadzone/rumble del launcher eran placebo (el SDK 0.10 eliminó
  esos cvars).
- **Qué se hizo**:
  - **Teclado por defecto**: `dbz3_mnk_mode` (default **TRUE**) → el teclado emula
    el mando de serie en PC. Mando por **XInput** (evita el cuelgue con RTSS/OBS,
    sigue siendo el backend por defecto); SDL queda disponible como selector para
    mandos genéricos (con aviso del riesgo de cuelgue).
  - **Mapeo configurable en la pestaña Input**: 24 campos de keybind (a,b,x,y,lt,rt,
    lb,rb,ls,rs,dpad,back,start,guide) con sintaxis `Tecla`, comas = alternativas,
    `Shift+/Ctrl+/Alt+` = modificadores. `dbz3_input_backend`, `dbz3_mnk_mouse`.
  - **Deadzone/rumble REALES**: parche SDK `input_system.cpp` con los cvars
    `deadzone` (aplicado al estado fusionado en `GetState`, cubre XInput+SDL+MnK)
    y `rumble` (gating en `SetState`). El registro de cvars de rexruntime.dll está
    compartido con el exe (exporta SetFlagByName/RegisterFlag/Query).
- **Esfuerzo**: Medio. **Impacto**: Medio-Alto. — **COMPLETADO**.
- Pendiente (validación en juego por el usuario): teclado en menús+combate, remap
  aplicado, mando XInput con deadzone/rumble; regenerar las carpetas de release.

---

## PRIORIDAD 4 — MODS MÁS FÁCILES DE GESTIONAR

### 4.1 Centro de mods en el launcher
- **Problema**: la comunidad quiere mods opcionales y fáciles de gestionar.
- **Qué hacer** (mucho ya está hecho): la pestaña Mods ya lista/activa/desactiva y
  edita manifiestos. Mejorar:
  - **Instalar mod desde un .zip** (arrastrar/soltar → descomprimir a `mods/`).
  - **Vista previa / info** de cada mod (capturas, descripción, versión).
  - **Perfiles de mods** (activa/desactiva un conjunto de una vez).
  - Mantener el núcleo "vanilla" por defecto (ya es así: sin mods, juego original).
- **Esfuerzo**: Medio. **Impacto**: Medio (engagement de la comunidad modding).

---

## VISIÓN A LARGO PLAZO (mayor esfuerzo, menor prioridad)

### 5.1 Port nativo a Android
- **Requisito**: portar el recompilador ReXGlue + GPU (Vulkan ya disponible en
  Android) a ARM64, más el launcher a una app. El núcleo recompilado del .xex
  (RISC→x86) debería retargetearse a ARM64. Es un proyecto grande.
- **Esfuerzo**: Muy alto. **Impacto**: Alto (alcance masivo).

### 5.2 Juego online
- **Requisito**: netplay (sincronización determinista estado/input tipo rollback)
  sobre el emulador. Muy complejo en un recompilador.
- **Esfuerzo**: Muy alto. **Impacto**: Alto (longevidad).

### 5.3 Compatibilidad D3D9/10/11
- **No recomendado / inviable**: el render es D3D12/Vulkan por diseño del SDK.
  Mejor invertir en optimizar D3D12 + afinar Vulkan.

---

## ORDEN SUGERIDO DE EJECUCIÓN

1. ~~**0.1** (actualizar a Rexglue 0.10.0)~~ — **HECHO** (2026-08-25): migración
   completada y validada en juego; el parche del runtime quedó re-aplicado.
2. ~~**1.1 + 1.2** (primer uso/UX)~~ — **HECHO** (2026-08-25): banner de
   validación + "Seleccionar carpeta de datos..." (remonta en caliente, PLAY
   bloqueado sin assets) + ventana de crash con ruta del log.
3. ~~**2.1** (detector AVX2 + build fallback)~~ — **HECHO** (2026-08-25):
   bootstrap dbz3.exe + dbz3_avx2/ + dbz3_legacy/ + build SDK v2.
4. ~~**3.1** (controles/mapeo)~~ — **HECHO** (2026-08-25): teclado por defecto
   (mnk_mode=true) + 24 keybinds configurables en la pestaña Input + deadzone/
   rumble reales (parche SDK input_system.cpp). Pendiente validación en juego.
5. ~~**2.2/2.3** (rendimiento/presets)~~ — **HECHO** (2026-08-25): vsync forzado
   (guest a 60 Hz, fin del "juego acelerado"), frame_cap real en el presenter
   (30 FPS para integradas), presets de calidad por GPU detectada
   (dbz3_quality_preset auto/low/medium/high/ultra). Pendiente validación en
   juego (frame cap 60/30 + preset auto en integrada) y perfilado Tracy opcional.
6. **4.1** (centro de mods) — ecosistema de mods.
7. **5.1/5.2** (Android/online) — decisión estratégica a futuro.

---

## REFERENCIAS

| Tema | Dónde |
|---|---|
| Layout de datos / assets | `AGENTS.md` §14.1, `src/main.cpp` (`FindGameRoot`) |
| Fixes runtime/input | `AGENTS.md` §4.1, §13.6, §14.3 |
| Build / flags AVX2 | `AGENTS.md` §6, `CMakePresets.json` |
| Parche del runtime (SDK) | `github/patches/` (9 archivos, version 0.10.0; aplicado y validado) |
| Migracion a Rexglue 0.10.0 | `docs/MIGRACION_REXGLUE_010.md` (COMPLETA y validada 2026-08-25) |
| Roadmap de modding | `docs/HOJA_DE_RUTA.md` |
| Estado actual | `docs/01_estructura/ESTADO.md` |
| Rexglue 0.10.0 (releases) | `https://github.com/rexglue/rexglue-sdk/releases` |
| Xenia: sin OpenGL/D3D11 (código fuente) | `xenia-project/xenia` `src/xenia/app/xenia_main.cc` |
