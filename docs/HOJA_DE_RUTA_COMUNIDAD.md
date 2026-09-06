# HOJA DE RUTA COMUNIDAD — Feedback comunitario (histórico, 2026-08-25)

> **SUPERSEDIDA por `docs/HOJA_DE_RUTA_2026_09.md`** (2026-09-02). Se conserva
> como registro de la demanda comunitaria P0-P5 y su resolución. Reescrita
> 2026-09-02 para arreglar el mojibake (§14.19).
> Toda la P0-P3 quedó **COMPLETADA**; P4 (centro de mods) y P5 (visión largo
> plazo) se absorbieron en la hoja de ruta actual.

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

### 0.1 ✅ Subir el SDK a 0.10.0 — HECHO y VALIDADO (2026-08-25)
- **Por qué**: el proyecto usaba una base 0.9.x. Acaba de salir **0.10.0** con
  mejoras relevantes: input (`gate mnk mouse look`, `comma-list binds`,
  `modifier prefixes`, `optional mouse`, `rstick keys`), ui (`imgui style
  hook`), cvar (`track value source`), system (mejor crash-reporting y
  estabilidad del recompilado), filesystem (fix POSIX access mode).
- **Riesgo**: SDK "en desarrollo temprano" (breaking API). El parche del
  runtime propio (mid-insert virtual en `afs.cpp`/`host_path_file.cpp`/
  `host_path_entry.cpp`) y los fixes de input/presenter había que re-aplicarlos.
- **Plan ejecutado**: SDK 0.10.0 compilado en paralelo en `rexglue-sdk-0.10/`
  (sin tocar el 0.9); parche re-aplicado (9 archivos); instalado en `rexglue/`
  (respaldo `rexglue_0.9/`); dbz3.exe compilado contra 0.10 (codegen
  regenerado: `REX_WEAK_FUNC` eliminado en 0.10); **validado en juego** (mods
  + mid-insert virtual funcionan).
- **Esfuerzo**: Medio-Alto. **Impacto**: Alto. — **COMPLETADO**.

---

## PRIORIDAD 1 — PRIMER USO / ESTABILIDAD (bug de bloqueo)

### 1.1 ✅ Diagnóstico automático de layout + mensajes claros — HECHO (2026-08-25)
- Banner de validación en el launcher (`launcher_state.cpp` OnDraw): verde
  `[OK] Datos del juego en: <ruta>` o rojo qué falta (default.xex / us / eu).
- Botón **"Seleccionar carpeta de datos..."**: diálogo nativo de Windows,
  valida `us/`/`eu/` o `default.xex` (`IsValidGameDataDir`), persiste en
  `dbz3_game_dir` y **remonta el juego en caliente** (`RelocateGameData` →
  `RemountGameDrive` re-registra `game:/d:` + re-aplica región) sin reiniciar.
- **PLAY bloqueado** cuando faltan los assets (`BeginDisabled`); el banner
  indica cómo arreglarlo.
- `OnConfigurePaths` prioridad: arg CLI > `dbz3_game_dir` > auto-detección.
  Raíz efectiva en `dbz3::EffectiveGameRoot` (región.cpp).
- **Esfuerzo**: Bajo. **Impacto**: Alto. — **COMPLETADO**.

### 1.2 ✅ Crash inmediato tras Play (sin mensaje) — HECHO (2026-08-25)
- `src/main.cpp` SetupCrashHandler: la captura de minidump (`crash_*.dmp`)
  se mantiene. Ante excepción no controlada se muestra ventana
  "DBZ Budokai 3 - Error" con: código de excepción, dirección, **ruta del
  log** (`logs/dbz3_*.log` vía `LatestLogPath`) y ruta del minidump. Con
  depurador conectado se delega (`EXCEPTION_CONTINUE_SEARCH`).
  `std::terminate` también muestra la ventana.
- **Esfuerzo**: Bajo-Medio. **Impacto**: Alto. — **COMPLETADO**.
- Pendiente (release): `README_PRIMER_ARRANQUE.txt` en el zip (reforzar sección
  de RELEASE_README).

---

## PRIORIDAD 2 — COMPATIBILIDAD DE HARDWARE

### 2.1 ✅ Detector eficiente de AVX2 + build fallback — HECHO (2026-08-25)
- **Hallazgo clave**: el exe del juego (dbz3.exe) se compila SIN `-march`
  (baseline) — el AVX2 vive SOLO en las DLLs del SDK. Por eso el core es un
  único binario y solo cambian las DLLs.
- Bootstrap `dbz3.exe` (`src/bootstrap.cpp`) chequeaba CPUID y lanzaba
  `dbz3_avx2\` o `dbz3_legacy\`. Build v2 del SDK con `-march=x86-64-v2` +
  `REXGLUE_OUTPUT_DIR`. Mods walk-up (`AfsModsRoot`/`ModsRoot`/`ModsOutDir`).
  Release con dbz3.exe + variantes.
- **NOTA (2026-08-28, v1.1.1)**: el bootstrap se **ELIMINÓ** (§9.1): ahora el
  SDK se compila entero en baseline `-march=x86-64 -mssse3` → un solo dbz3.exe
  universal (Core 2 2006+). El fallback `v1.1.0-clasico` (runtime avx2) queda
  como release no-Latest. Detalle en `AGENTS.md` §9.
- **Esfuerzo**: Medio. **Impacto**: Alto. — **COMPLETADO** (evolucionado).

### 2.2 ✅ Backends / rendimiento en máquinas modestas — HECHO (2026-08-25)
- **Realidad**: Xenia/ReXGlue NO soporta OpenGL ni D3D11 — solo **D3D12 y
  Vulkan** (fragment shader interlock / rasterizer-ordered views). D3D12 es YA
  el backend MÁS compatible. → **NO perseguir OpenGL/D3D11**.
- **Lo práctico**: presets de calidad por GPU (`dbz3_quality_preset`
  auto/low/medium/high/ultra/manual; Auto detecta GPU vía DXGI y aplica
  perfil), **frame_cap REAL** a 30 FPS (parche `d3d12_presenter.cpp` restaura
  el cvar `frame_cap`). Perfilado Tracy (build win-amd64-tracy) como
  optimización fina opcional.
- **Esfuerzo**: Bajo-Medio. **Impacto**: Medio. — **COMPLETADO**.

### 2.3 ✅ Frame pacing ("el juego corre acelerado") — HECHO (2026-08-25)
- **Hallazgo**: en el SDK 0.10 el pacing del guest lo hace el worker `vsync`
  de `GraphicsSystem`. Con `vsync` OFF el vblank corre a ~1000 Hz → la lógica
  corre ~16x = "acelerado". El cvar `frame_cap` del 0.9 ya no existía.
- **Qué se hizo**: `vsync` forzado a true (el guest DEBE correr a 60 Hz; el
  checkbox placebo eliminado → "Game speed: fixed 60 FPS"). `frame_cap` real
  restaurado (throttle de presentación host). `dbz3_frame_cap` default 60.
- **Esfuerzo**: Bajo. **Impacto**: Medio. — **COMPLETADO**.
- Pendiente: validar en juego el frame cap y el preset auto en una máquina con
  integrada; perfilado Tracy opcional.

---

## PRIORIDAD 3 — CONTROLES

### 3.1 ✅ Teclado y mando robusto (compatibilidad SDL) — HECHO (2026-08-25)
- **Diagnóstico**: el driver MnK del SDK existía completo pero `mnk_mode` estaba
  en `false` por defecto → el teclado no hacía nada. Los sliders de
  deadzone/rumble del launcher eran placebo (el SDK 0.10 eliminó esos cvars).
- **Qué se hizo**: `dbz3_mnk_mode` default **TRUE** (teclado emula el mando de
  serie). Mando por **XInput** (evita el cuelgue con RTSS/OBS); SDL como
  selector para mandos genéricos (con aviso del riesgo). **Mapeo configurable**
  en la pestaña Input: 24 keybinds (sintaxis `Tecla`, comas = alternativas,
  `Shift+/Ctrl+/Alt+` = modificadores). **Deadzone/rumble REALES** (parche
  `input_system.cpp` con cvars `deadzone`/`rumble`; registro de cvars de
  rexruntime.dll compartido con el exe).
- **Esfuerzo**: Medio. **Impacto**: Medio-Alto. — **COMPLETADO**.

---

## PRIORIDAD 4 — MODS MÁS FÁCILES DE GESTIONAR

### 4.1 Centro de mods en el launcher — HECHO (absorbido)
- **Instalar mod desde `.zip`** (PowerShell Expand-Archive vía
  `-EncodedCommand` base64; normaliza wrapper de una carpeta).
- **Perfiles de mods** (`mods/profiles.txt`, cvar `dbz3_mod_profile`).
- La pestaña Mods lista/activa/desactiva y edita manifiestos; núcleo vanilla
  por defecto. Detalle en `AGENTS.md` §8.
- **Esfuerzo**: Medio. **Impacto**: Medio.

---

## VISIÓN A LARGO PLAZO (mayor esfuerzo, menor prioridad)

### 5.1 Port nativo a Android
- Retargetear el recompilador ReXGlue + GPU (Vulkan ya en Android) a ARM64 +
  launcher como app. Proyecto grande. — No iniciado.

### 5.2 Juego online
- Netplay (sincronización determinista tipo rollback) sobre el recompilador.
  Muy complejo. — No iniciado.

### 5.3 Compatibilidad D3D9/10/11
- **No recomendado / inviable**: el render es D3D12/Vulkan por diseño del SDK
  (§2.2). Mejor invertir en optimizar D3D12 + afinar Vulkan.

---

## REFERENCIAS

| Tema | Dónde |
|---|---|
| Hoja de ruta actual (2026-09) | `docs/HOJA_DE_RUTA_2026_09.md` |
| Migración a Rexglue 0.10.0 | `docs/MIGRACION_REXGLUE_010.md` |
| Parche del runtime (SDK) | `github/patches/` |
| Roadmap de modding | `docs/HOJA_DE_RUTA.md` (histórico) |
| Estado actual | `docs/01_estructura/ESTADO.md` |
| Historial de sesiones | `docs/01_estructura/HISTORICO_AGENTS.md` |