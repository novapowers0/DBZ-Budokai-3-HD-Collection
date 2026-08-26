# HOJA DE RUTA â€” Feedback de la comunidad (estabilidad, compatibilidad, nuevas funciones)

> Actualizado: 2026-08-25. Documento complementario a `HOJA_DE_RUTA.md` (que cubre
> model swaps/costumes/roster). AquÃ­ se priorizan los **problemas tÃ©cnicos y de
> ejecuciÃ³n reportados** por la comunidad tras la publicaciÃ³n, mÃ¡s las funciones
> nuevas solicitadas. Cada Ã­tem indica esfuerzo/impacto y estado real actual.

> **Cambio 2026-08-21**: aÃ±adido **P0 â€” Actualizar a Rexglue 0.10.0** (el SDK
> reciÃ©n saliÃ³ y estamos en una versiÃ³n anterior). Reescrita la secciÃ³n
> "Compatibilidad de hardware" con la investigaciÃ³n real sobre el detector AVX2
> y los backends OpenGL/D3D11.
>
> **Cambio 2026-08-25**: **P0 COMPLETADA** â€” la migraciÃ³n a Rexglue 0.10.0 estÃ¡
> hecha y **validada en juego** (mods + mid-insert virtual). Ver
> `MIGRACION_REXGLUE_010.md` Â§8. Solo queda actualizar release/README antes de
> subir a GitHub (pendiente hasta completar el plan general).

---

## RESUMEN DE LA DEMANDA COMUNITARIA

| CategorÃ­a | Reportes | Impacto percibido |
|---|---|---|
| **Inicio/ejecuciÃ³n** | No abre, se cierra tras "Play", "Entrypoint XEX not found" | Alto (bloquea primer uso) |
| **Compatibilidad CPU** | Requiere AVX2 â†’ 0xc0000142 en CPUs antiguas | Alto (deja fuera a usuarios) |
| **Rendimiento** | 10 FPS en integradas; juego "acelerado" | Medio-alto |
| **Estructura de archivos** | ConfusiÃ³n `default.xex` / `assets/` vs raÃ­z | Medio (fricciÃ³n primer uso) |
| **Controles** | Teclado/botones no responden en juego | Medio |
| **Nuevas funciones** | Android nativo, online, mods fÃ¡ciles de gestionar | Bajo-medio (visiÃ³n a futuro) |

---

## PRIORIDAD 0 â€” ACTUALIZAR A REXGLUE 0.10.0 (fundaciÃ³n)

### 0.1 âœ… Subir el SDK a 0.10.0 â€” **HECHO y VALIDADO (2026-08-25)**
- **Por quÃ©**: el proyecto usa una versiÃ³n anterior de `rexglue-sdk`
  (base 0.9.x). Acaba de salir **0.10.0** (bump "chore: bump version floor to
  0.10.0"), y las nightlies `0.10.0-dev.*` ya traen mejoras relevantes:
  - **input**: `gate mnk mouse look at runtime`, `comma-list binds, modifier
    prefixes, optional mouse, rstick keys` â†’ **directamente Ãºtil para el
    problema de controles** (P3).
  - **ui**: `imgui style hook` â†’ Ãºtil para el launcher.
  - **cvar**: `track value source so launch args outrank env and config` â†’
    Ãºtil para el auto-guardado de ajustes (v1.0.4).
  - **system**: `report the guest arena base and unhandled access violations`,
    `deliver APCs through a trap frame to preserve guest register state`,
    `add cr2-cr4 and guest-owned fpscr to the non-volatile save area` â†’
    mejor crash-reporting y estabilidad del recompilado.
  - **filesystem**: `select the POSIX access mode instead of ORing O_RDONLY
    and O_WRONLY`.
- **Riesgo**: el SDK estÃ¡ "en desarrollo temprano" (breaking API). Actualizar
  puede romper integraciones propias:
  - El parche del runtime de este proyecto (mid-insert virtual en
    `afs.cpp`/`host_path_file.cpp`/`host_path_entry.cpp`, Â§13.6/Â§14) hay que
    **re-aplicarlo sobre la versiÃ³n nueva** (los archivos en `github/patches/`).
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
  futura, fixes de estabilidad/input gratis). â€” **COMPLETADO**.

---

## PRIORIDAD 1 â€” PRIMER USO / ESTABILIDAD (bug de bloqueo)

### 1.1 âœ… DiagnÃ³stico automÃ¡tico de layout + mensajes claros â€” **HECHO (2026-08-25)**
- **Problema**: "Entrypoint XEX not found" y confusiÃ³n `assets/` vs raÃ­z (Â§14.1 ya
  soporta ambos, pero el usuario no sabe CUÃL usar ni dÃ³nde poner cada cosa).
- **QuÃ© se hizo**:
  - **Banner de validaciÃ³n en el launcher** (`launcher_state.cpp` OnDraw): tras el
    header muestra en verde `[OK] Datos del juego en: <ruta>` cuando encuentra
    `default.xex` + `us/`/`eu/` (regiÃ³n actual), o en rojo quÃ© falta exactamente
    (default.xex / us / eu) cuando no.
  - **BotÃ³n "Seleccionar carpeta de datos..."**: abre el diÃ¡logo nativo de
    Windows, valida que la carpeta contenga `us/`/`eu/` o `default.xex`
    (`IsValidGameDataDir`), persiste en el cvar `dbz3_game_dir` (dbz3_user.toml)
    y **remonta el juego en caliente** (`dbz3::RelocateGameData` â†’
    `RemountGameDrive` re-registra `game:/d:` en la raÃ­z nueva + re-aplica la
    regiÃ³n) â€” sin reiniciar.
  - **PLAY bloqueado** cuando faltan los assets (`BeginDisabled`), evitando el
    crash de arranque; el banner indica cÃ³mo arreglarlo.
  - `OnConfigurePaths` prioridad: arg CLI > `dbz3_game_dir` (override del
    launcher) > auto-detecciÃ³n (exe/assets/proyecto/padre). La raÃ­z efectiva se
    registra en `dbz3::EffectiveGameRoot` (regiÃ³n.cpp) para que el montaje de
    regiÃ³n siempre use la carpeta elegida (el Runtime interno es fijo al Setup).
- **Esfuerzo**: Bajo. **Impacto**: Alto. â€” **COMPLETADO**.

### 1.2 âœ… Crash inmediato tras Play (sin mensaje) â€” **HECHO (2026-08-25)**
- **Problema**: varios usuarios "no abre o se cierra inmediatamente tras Play".
- **QuÃ© se hizo** (`src/main.cpp` SetupCrashHandler):
  - La captura de minidump (`crash_*.dmp`, Dev tab) se mantiene.
  - Ahora, ante una excepciÃ³n no controlada, se **muestra una ventana
    "DBZ Budokai 3 - Error"** con: cÃ³digo de excepciÃ³n, direcciÃ³n, **ruta del
    log** (`logs/dbz3_*.log` vÃ­a `LatestLogPath`) y ruta del minidump si se
    generÃ³. Con depurador conectado se delega (`EXCEPTION_CONTINUE_SEARCH`).
  - `std::terminate` tambiÃ©n muestra la ventana.
- **Esfuerzo**: Bajo-Medio. **Impacto**: Alto. â€” **COMPLETADO**.
- Pendiente (release): `README_PRIMER_ARRANQUE.txt` en el zip (ya hay secciÃ³n en
  RELEASE_README, reforzar).

---

## PRIORIDAD 2 â€” COMPATIBILIDAD DE HARDWARE

### 2.1 âœ… Detector eficiente de AVX2 + build fallback â€” **HECHO (2026-08-25)**
- **Problema**: el SDK se compila con `-march=x86-64-v3` (implica AVX2). CPUs
  sin AVX2 (Intel pre-Haswell 4Âª gen, AMD pre-Excavator 2015) â†’ `0xc0000142`
  / `0xc000001d` (instrucciÃ³n ilegal) al arrancar.
- **Hallazgo clave**: el exe del juego (dbz3.exe) se compila SIN `-march`
  (baseline) â€” el AVX2 vive SOLO en las DLLs del SDK (rexruntime, rexgpu-xenos,
  FFX). Por eso el core es un Ãºnico binario y solo cambian las DLLs.
- **QuÃ© se hizo**:
  - **Bootstrap `dbz3.exe`** (`src/bootstrap.cpp`, baseline x86-64, sin SDK):
    chequea CPUID UNA vez (AVX2+BMI1+BMI2+FMA+OSXSAVE+XGETBV) y lanza
    `dbz3_avx2\dbz3.exe` o `dbz3_legacy\dbz3.exe`, pasando los args
    y propagando el exit code. Ventana de error clara si falta la variante.
  - **Build v2 del SDK**: `out/build-win-vulkan-legacy` con `-march=x86-64-v2`
    + `REXGLUE_OUTPUT_DIR` (nueva cache var) â†’ `out/win-amd64-legacy` con
    rexruntime/rexgpu-xenos/FFX v2.
  - **Mods walk-up**: `AfsModsRoot` (runtime) + `ModsRoot` (launcher) + 
    `ModsOutDir` (mod_pipeline) suben hasta 3 niveles buscando `mods/` (el
    core vive en subcarpeta en release).
  - **Release**: `tools/make_release.ps1` monta release-stage con dbz3.exe +
    dbz3_avx2/ + dbz3_legacy/ + toolkit + docs y genera el zip. README_PRIMER_
    ARRANQUE.txt documenta disposiciones y variantes.
- **Esfuerzo**: Medio. **Impacto**: Alto. â€” **COMPLETADO**.
- Pendiente: validar en una CPU sin AVX2 real y la confirmaciÃ³n visual de que
  los mods se listan desde la raÃ­z.

### 2.2 âœ… Backends / rendimiento en mÃ¡quinas modestas â€” **HECHO (parte prÃ¡ctica, 2026-08-25)**
- **Problema reportado**: "se deberÃ­a integrar Direct3D9/10/11, no solo D3D12"
  por los bajos FPS en grÃ¡ficas integradas/viejas.
- **Realidad de la investigaciÃ³n (Xenia, la base de ReXGlue)**: Xenia **NO
  soporta OpenGL ni Direct3D 11** â€” solo **D3D12 y Vulkan**. La emulaciÃ³n Xenos
  usa **fragment shader interlock / rasterizer-ordered views**, que solo
  existen de forma usable en D3D12 y Vulkan. **D3D12 es YA el backend MÃS
  compatible** de los dos. Un backport D3D11 es grande y ni Xenia lo ha hecho.
  â†’ **NO perseguir OpenGL/D3D11** (inviable en esta arquitectura).
- **Lo prÃ¡ctico hecho**:
  - **Presets de calidad por GPU** (la pestaÃ±a Video): `dbz3_quality_preset`
    con Auto/Low/Medium/High/Ultra/Manual. `Auto` detecta la GPU vÃ­a DXGI
    (nombre + VRAM) y aplica el perfil recomendado en cada arranque
    (low: 1x+sin MSAA+bilinear; medium: 1x+FSR+4x aniso; high: 1x+MSAA+FSR+16x
    aniso; ultra: 2x+MSAA, solo manual). Instalaciones frescas â†’ "auto";
    instalaciones con toml previo â†’ migradas a "manual" (nunca se les cambia
    la config).
  - **Frame cap REAL a 30 FPS** (opciÃ³n para integradas): parche
    `d3d12_presenter.cpp` que restaura el cvar `frame_cap` (0.10 lo habÃ­a
    eliminado) â†’ limita la presentaciÃ³n host (no la velocidad del juego).
  - El perfilado **Tracy** del path D3D12 (build win-amd64-tracy) queda como
    optimizaciÃ³n fina opcional (requiere sesiÃ³n de juego para datos reales).
- **Esfuerzo**: Bajo-Medio (presets+frame cap). **Impacto**: Medio. â€” COMPLETADO.

### 2.3 âœ… Frame pacing ("el juego corre acelerado") â€” **HECHO (2026-08-25)**
- **Problema**: usuarios ven el juego "a velocidad acelerada".
- **Hallazgo**: en el SDK 0.10 el pacing del guest lo hace el worker `vsync` de
  `GraphicsSystem` (vblank del guest a `1/video_mode_refresh_rate`). Con
  `vsync` OFF el vblank corre a ~1000 Hz â†’ la lÃ³gica del juego corre ~16x
  rÃ¡pida = "acelerado". El cvar `frame_cap` del 0.9 **ya no existe** en 0.10
  (el slider era placebo).
- **QuÃ© se hizo**:
  - **`vsync` forzado a true en el juego** (el guest DEBE correr a 60 Hz; se
    eliminÃ³ el checkbox "VSync" del launcher y del menÃº Dev, que era un
    placebo/contraproducente â†’ ahora muestra "Game speed: fixed 60 FPS").
  - **`frame_cap` real restaurado** (parche `d3d12_presenter.cpp`): throttle de
    la presentaciÃ³n host (no de la velocidad del juego). `dbz3_frame_cap`
    default 0 â†’ **60** (instalaciones frescas con pacing correcto).
  - Documentada la opciÃ³n "Frame cap" en la pestaÃ±a Video (60 fluido, 30 para
    integradas, 0 sin lÃ­mite).
- **Esfuerzo**: Bajo. **Impacto**: Medio. â€” **COMPLETADO**.
- Pendiente: validar en juego el frame cap (60/30) y el preset auto en una
  mÃ¡quina con integrada; perfilado Tracy opcional.

---

## PRIORIDAD 3 â€” CONTROLES

### 3.1 âœ… Teclado y mando robusto (compatibilidad SDL) â€” **HECHO (2026-08-25)**
- **Problema**: teclado/botones no responden en juego (hay antecedentes de cuelgues
  con `SDL_INIT_GAMEPAD` + RTSS/OBS â†’ se pasÃ³ a XInput nativo, Â§4.1).
- **DiagnÃ³stico**: el driver MnK del SDK (tecladoâ†’mando) ya existÃ­a completo pero
  `mnk_mode` estaba en `false` por defecto y nadie lo activaba â†’ el teclado no hacÃ­a
  nada. Los sliders de deadzone/rumble del launcher eran placebo (el SDK 0.10 eliminÃ³
  esos cvars).
- **QuÃ© se hizo**:
  - **Teclado por defecto**: `dbz3_mnk_mode` (default **TRUE**) â†’ el teclado emula
    el mando de serie en PC. Mando por **XInput** (evita el cuelgue con RTSS/OBS,
    sigue siendo el backend por defecto); SDL queda disponible como selector para
    mandos genÃ©ricos (con aviso del riesgo de cuelgue).
  - **Mapeo configurable en la pestaÃ±a Input**: 24 campos de keybind (a,b,x,y,lt,rt,
    lb,rb,ls,rs,dpad,back,start,guide) con sintaxis `Tecla`, comas = alternativas,
    `Shift+/Ctrl+/Alt+` = modificadores. `dbz3_input_backend`, `dbz3_mnk_mouse`.
  - **Deadzone/rumble REALES**: parche SDK `input_system.cpp` con los cvars
    `deadzone` (aplicado al estado fusionado en `GetState`, cubre XInput+SDL+MnK)
    y `rumble` (gating en `SetState`). El registro de cvars de rexruntime.dll estÃ¡
    compartido con el exe (exporta SetFlagByName/RegisterFlag/Query).
- **Esfuerzo**: Medio. **Impacto**: Medio-Alto. â€” **COMPLETADO**.
- Pendiente (validaciÃ³n en juego por el usuario): teclado en menÃºs+combate, remap
  aplicado, mando XInput con deadzone/rumble; regenerar las carpetas de release.

---

## PRIORIDAD 4 â€” MODS MÃS FÃCILES DE GESTIONAR

### 4.1 Centro de mods en el launcher
- **Problema**: la comunidad quiere mods opcionales y fÃ¡ciles de gestionar.
- **QuÃ© hacer** (mucho ya estÃ¡ hecho): la pestaÃ±a Mods ya lista/activa/desactiva y
  edita manifiestos. Mejorar:
  - **Instalar mod desde un .zip** (arrastrar/soltar â†’ descomprimir a `mods/`).
  - **Vista previa / info** de cada mod (capturas, descripciÃ³n, versiÃ³n).
  - **Perfiles de mods** (activa/desactiva un conjunto de una vez).
  - Mantener el nÃºcleo "vanilla" por defecto (ya es asÃ­: sin mods, juego original).
- **Esfuerzo**: Medio. **Impacto**: Medio (engagement de la comunidad modding).

---

## VISIÃ“N A LARGO PLAZO (mayor esfuerzo, menor prioridad)

### 5.1 Port nativo a Android
- **Requisito**: portar el recompilador ReXGlue + GPU (Vulkan ya disponible en
  Android) a ARM64, mÃ¡s el launcher a una app. El nÃºcleo recompilado del .xex
  (RISCâ†’x86) deberÃ­a retargetearse a ARM64. Es un proyecto grande.
- **Esfuerzo**: Muy alto. **Impacto**: Alto (alcance masivo).

### 5.2 Juego online
- **Requisito**: netplay (sincronizaciÃ³n determinista estado/input tipo rollback)
  sobre el emulador. Muy complejo en un recompilador.
- **Esfuerzo**: Muy alto. **Impacto**: Alto (longevidad).

### 5.3 Compatibilidad D3D9/10/11
- **No recomendado / inviable**: el render es D3D12/Vulkan por diseÃ±o del SDK.
  Mejor invertir en optimizar D3D12 + afinar Vulkan.

---

## ORDEN SUGERIDO DE EJECUCIÃ“N

1. ~~**0.1** (actualizar a Rexglue 0.10.0)~~ â€” **HECHO** (2026-08-25): migraciÃ³n
   completada y validada en juego; el parche del runtime quedÃ³ re-aplicado.
2. ~~**1.1 + 1.2** (primer uso/UX)~~ â€” **HECHO** (2026-08-25): banner de
   validaciÃ³n + "Seleccionar carpeta de datos..." (remonta en caliente, PLAY
   bloqueado sin assets) + ventana de crash con ruta del log.
3. ~~**2.1** (detector AVX2 + build fallback)~~ â€” **HECHO** (2026-08-25):
   bootstrap dbz3.exe + dbz3_avx2/ + dbz3_legacy/ + build SDK v2.
4. ~~**3.1** (controles/mapeo)~~ â€” **HECHO** (2026-08-25): teclado por defecto
   (mnk_mode=true) + 24 keybinds configurables en la pestaÃ±a Input + deadzone/
   rumble reales (parche SDK input_system.cpp). Pendiente validaciÃ³n en juego.
5. ~~**2.2/2.3** (rendimiento/presets)~~ â€” **HECHO** (2026-08-25): vsync forzado
   (guest a 60 Hz, fin del "juego acelerado"), frame_cap real en el presenter
   (30 FPS para integradas), presets de calidad por GPU detectada
   (dbz3_quality_preset auto/low/medium/high/ultra). Pendiente validaciÃ³n en
   juego (frame cap 60/30 + preset auto en integrada) y perfilado Tracy opcional.
6. **4.1** (centro de mods) â€” ecosistema de mods.
7. **5.1/5.2** (Android/online) â€” decisiÃ³n estratÃ©gica a futuro.

---

## REFERENCIAS

| Tema | DÃ³nde |
|---|---|
| Layout de datos / assets | `AGENTS.md` Â§14.1, `src/main.cpp` (`FindGameRoot`) |
| Fixes runtime/input | `AGENTS.md` Â§4.1, Â§13.6, Â§14.3 |
| Build / flags AVX2 | `AGENTS.md` Â§6, `CMakePresets.json` |
| Parche del runtime (SDK) | `github/patches/` (9 archivos, version 0.10.0; aplicado y validado) |
| Migracion a Rexglue 0.10.0 | `docs/MIGRACION_REXGLUE_010.md` (COMPLETA y validada 2026-08-25) |
| Roadmap de modding | `docs/HOJA_DE_RUTA.md` |
| Estado actual | `docs/01_estructura/ESTADO.md` |
| Rexglue 0.10.0 (releases) | `https://github.com/rexglue/rexglue-sdk/releases` |
| Xenia: sin OpenGL/D3D11 (cÃ³digo fuente) | `xenia-project/xenia` `src/xenia/app/xenia_main.cc` |
