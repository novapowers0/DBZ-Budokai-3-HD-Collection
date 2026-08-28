# PLAN DE DISEÑO v1.1.1 — Depurado, mejora interna y bases de Linux

> Documento de diseño de la fase v1.1.1 (sucesora de la v1.1.0 universal).
> Objetivo: cerrar los flecos abiertos del feedback, endurecer el runtime,
> ordenar el proceso interno y **sentar las bases de un port a Linux**.
> Estado: diseño aprobado por AGENTS §3.4/§14 (consolidado) — fases en curso.

---

## 1. Contexto y principios

La v1.1.0 dejó el proyecto con **un solo ejecutable universal** (baseline SSSE3)
y GitHub limpio (v1.1.0 Latest + v1.1.0-clasico de respaldo). El feedback de
usuarios (v1.0.6 → v1.1.0) está prácticamente cerrado; lo que queda son:

- **Flecos de robustez** (caminos de error, arranque, teardown).
- **Mejoras internas de proceso** (la fragilidad de `github/` como repo espejo).
- **Fundamentos de portabilidad** (el SDK ya es multiplataforma; `src/` no).

Principios de la fase: (1) el build Windows actual no debe romperse jamás,
(2) cada cambio se valida con smoke test, (3) no tocar la arquitectura ganada
de "un solo exe", (4) el port de modelos PS2→B3 queda **pausado** (trabajo en
paralelo, no bloquea 1.1.1).

---

## 2. Fase A — Depurado (robustez, feedback abierto)

### A1. ✅ Sin assets → mensaje claro, no crash (HECHO en 1.1.1)
- **Síntoma**: con la carpeta de datos vacía, el proceso abría la ventana y
  moría con 0xC0000005 (teardown tras fallar `Runtime::Setup`).
- **Fix aplicado** (`src/main.cpp`): pre-flight en `OnPreSetup` — si no hay
  `default.xex` en la carpeta de datos efectiva, se muestra un MessageBox claro
  (cómo colocar los assets) y se sale limpio (`_Exit(1)`). Validado: exit 1 sin
  crash; el camino feliz sigue llegando al launcher a ~880 ms.

### A2. ✅ Marcadores de timing de arranque (HECHO en 1.1.1)
- **Motivo**: reportes de "pantalla negra lenta antes del launcher" (§14.10).
- **Fix aplicado**: `PhaseLog()` registra ms desde el inicio en cada override
  (`OnPreSetup/OnPostSetup/OnCreateDialogs/OnPreLaunchModule/OnPostLaunchModule`).
  Con esto un log del usuario dirime si el cuello de botella es el init
  D3D12/swapchain (gap hasta `first present OK`) o algo anterior.

### A3. Pendiente — `std::terminate` intermitente en `LaunchModule` (mitigado, sin reproducir)
- §14.14: try/catch + stack dump añadidos; el throw exacto sigue sin localizar
  (no se reproduce en frío). La medida es diagnóstica: si un usuario lo pilla,
  el log traerá `LaunchModule deferred threw std::exception: <msg>` + el stack.
  **Acción 1.1.1**: analizar ese mensaje si llega; si no, cerrar como "mitigado".

### A4. Pendiente — teardown/crash window tras fallos del guest
- Revisar que las rutas de fallo (`ConstructRuntime`, cuelgue del hilo guest,
  `OnGuestThreadExit`) no dejen un proceso zombie o un crash sin ventana.
  Verificar con pruebas sin assets, con regiones a medias (us/ sin eu) y con
  xex desconocido.

### A5. Pendiente — validar el core EU en combate real
- §14.13/14.16: el boot EU es estable y la demo pasa, pero los paths profundos
  (combate real, eventos, skills) podrían revelar funciones no registradas.
  **Acción**: sesión de juego con el core dual y `DBZ3_COLLECT_UNREGISTERED=1`
  para recolectar; iterar `dbz3_config_eu.toml`.

### A6. Pendiente — idioma Japonés del selector
- El launcher traduce EN/ES/IT/DE/FR; el juego tiene 6 idiomas (JP incluido).
  Decidir si el launcher se traduce a JP o se marca claramente "no disponible".

---

## 3. Fase B — Mejora interna (proceso)

### B1. ✅ `tools/sync_github.ps1` (HECHO en 1.1.1)
- `github/` es un repo espejo versionable; el sync manual (§9.1) era frágil.
- **Fix**: script que replica el proceso (src/docs/awo_tools/mod center hd/tools
  + archivos raíz), respeta el `.gitignore` (conserva `tools/*.exe` canónicos,
  no toca `mods/`), con `-DryRun`. Recuerda que `patches/` es manual.

### B2. ✅ Versión con fuente única (HECHO en 1.1.1)
- La versión vivía en 3 sitios (version.rc, make_release.ps1, RELEASE_README).
- **Fix**: `make_release.ps1` lee `VERSION_MAJOR/MINOR/PATCH` de `src/version.rc`
  por defecto (override con `-Version` para sufijos tipo `-clasico`).

### B3. Pendiente — limpiar herramientas/documentos obsoletos
- `awo_tools/analyze_bin_hd.py` está DESACTUALIZADO (§13.2, layout 010 "PS3"
  incorrecto para X360) → marcar o corregir.
- `docs/HOJA_DE_RUTA_COMUNIDAD.md` tiene mojibake (§14.19) → reescribir.
- `tools/make_release.ps1` ya avisa de NO usar UPX (§14.20) — documentación OK.

### B4. Pendiente — CI-lite de release (verificación repetible)
- Un script `tools/verify_release.ps1` que, dado el stage, compruebe: hashes de
  las DLLs canónicas, presencia del clamp V-Sync (string en rexgpu), VERSIONINFO
  del exe, ausencia de assets del juego en el zip. Reutilizable antes de subir.

### B5. Pendiente — nota en RELEASE_README sobre el fallback
- Añadir a RELEASE_README la existencia de `v1.1.0-clasico` y cuándo usarlo.

---

## 4. Fase C — Bases de Linux (ver `docs/PLAN_LINUX.md` para el detalle)

Resumen del estado tras la fase (auditoría + guards):

- **El SDK ya es portable**: `REX_PLATFORM_WIN32/LINUX/MAC`, pares
  `*_win/*_posix`, Vulkan ON por defecto en Linux, input/audio SDL, filesystem
  `std::filesystem` + `FileHandle` abstraído. El cuello de botella era `src/`.
- **HECHO en 1.1.1 (guards de plataforma en `src/`)**:
  - `settings.cpp`: MD5 **portable** (elimina CryptoAPI) para `CheckDefaultXex` +
    detección de GPU DXGI protegida con fallback (tier "medium") + defaults de
    backend por plataforma (`d3d12`+`xinput` en Windows, `vulkan`+`sdl` en el
    resto).
  - `launcher_state.cpp`: diálogos COM (PickFolder/PickFile) y conversiones
    UTF-16 protegidos con fallback "cancelado" (diálogo portable pendiente).
  - `mod_pipeline.cpp`: `CreateProcessW` protegido (fallback con error claro).
  - `mods.cpp`: ya tenía fallback no-Windows para el instalador de zips.
  - `main.cpp`: `OutputDebugStringA` como no-op fuera de Windows; el crash
    handler ya era Win32-only; pre-flight portable.
- **Pendiente para el build Linux real**:
  1. Dialogos de archivo portables (SDL/GTK/zenity) para el launcher.
  2. Spawn de scripts portable (`posix_spawn`) en `mod_pipeline.cpp`.
  3. Extracción de zips portable (libzip/minizip) en `mods.cpp`.
  4. Verificar que `ppc/` y `codegen/` del SDK no tengan Win32 residual.
  5. CMake preset `linux-*` + instalación de dependencias (Vulkan, SDL3, X11-xcb,
     Wayland) y validar Vulkan como backend de juego (hoy experimental: 6.5x
     más lento que D3D12 en IssueSwap — el reto de rendimiento del port).

---

## 5. Criterios de aceptación de 1.1.1

1. El build Windows (dual + release) compila sin warnings nuevos y el smoke test
   del paquete pasa (launcher shown + first present OK + sin FATAL).
2. Sin assets → mensaje claro y salida limpia (nunca 0xC0000005).
3. El log de arranque permite diagnosticar la "pantalla negra" (marcadores A2).
4. `tools/sync_github.ps1` sincroniza en un solo comando y `make_release.ps1`
   genera el zip con la versión de `version.rc`.
5. `src/` compila conceptualmente en Linux (guards en su sitio); `PLAN_LINUX.md`
   define el orden de trabajo del port.
6. Release 1.1.1 empaquetada (universal) + respaldo `-clasico` actualizado.

---

## 6. Trabajo no incluido en 1.1.1 (paralelo o posterior)

- Port de modelos PS2→B3 (pipeline `port_ps2_b3_*`): **pausado por decisión del
  usuario** (AGENTS §3.4/§15). No bloquea 1.1.1.
- Reto de rendimiento del backend Vulkan (requiere sesión de perfilado en Linux).
- Versión de Linux publicable (es el objetivo de largo plazo; 1.1.1 solo sienta
  las bases de código).