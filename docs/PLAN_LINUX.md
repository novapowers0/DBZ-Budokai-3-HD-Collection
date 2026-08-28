# PLAN_LINUX — Port de DBZ Budokai 3 HD Collection a Linux

> Estrategia para portar el proyecto a Linux, basada en una auditoría real del
> código (AGENTS §14.21/§3.4 + barrido de plataforma 2026-08-28). El SDK
> (derivado de Xenia) ya es multiplataforma; el trabajo está en `src/` (launcher)
> y en validar Vulkan como backend de juego.

---

## 1. Viabilidad (veredicto)

**ALTA.** El SDK (`rexglue-sdk-0.10`) ya tiene:
- Capa de plataforma explícita: `REX_PLATFORM_WIN32 / LINUX / MAC` y pares de
  archivos `*_win.cpp` / `*_posix.cpp` (seleccionados por CMake).
- **Backend Vulkan ON por defecto en Linux** (`REXGLUE_USE_D3D12=OFF,
  REXGLUE_USE_VULKAN=ON` en el CMake raíz); D3D12 es Windows-only.
- Input/audio SDL3 (multiplataforma), filesystem `std::filesystem` +
  `FileHandle` abstraído (Win32FileHandle vs PosixFileHandle), excepciones
  SEH/POSIX, sockets, memoria mapeada, DLL/son, etc. — todos con su par posix.
- El CMake raíz del juego ya tiene ramas `WIN32` vs `UNIX`.

El cuello de botella real estaba en `src/` (launcher): 6 archivos con APIs Win32
sin guardar. **La fase 1.1.1 ya los protegió** (ver §3). Lo que queda es
funcional (diálogos, spawn, zips) y la validación de Vulkan como backend de
juego.

---

## 2. Mapa de plataforma (auditoría 2026-08-28)

### 2.1 `src/` — código específico de Windows (antes de 1.1.1)

| Archivo | API Win32 | Estado tras 1.1.1 |
|---|---|---|
| `src/main.cpp` | `SetUnhandledExceptionFilter`, `CreateFileA`, `MessageBoxA`, `RtlCaptureStackBackTrace`, `OutputDebugStringA` | crash handler ya era Win32-only; `OutputDebugStringA` → no-op fuera de Windows; pre-flight portable |
| `src/launcher/settings.cpp` | `CryptAcquireContext/CALG_MD5`, `CreateDXGIFactory1/EnumAdapters1`, `EnumDisplaySettingsW`, `WideCharToMultiByte` | **MD5 portable escrito** (elimina CryptoAPI); DXGI protegido con fallback tier "medium"; refresh ya guardado |
| `src/launcher/launcher_state.cpp` | `CoInitializeEx`, `IFileOpenDialog`, `SHCreateItemFromParsingName`, `MultiByteToWideChar` | diálogos COM y UTF-16 protegidos; fallback "cancelado" |
| `src/launcher/mod_pipeline.cpp` | `CreateProcessW`, `CreatePipe`, `ReadFile`, `WaitForSingleObject` | protegido; fallback con error claro |
| `src/mods.cpp` | PowerShell `Expand-Archive` (vía `CreateProcessW`) | ya tenía fallback `#else` (no soportado) |
| `src/region.cpp`, `src/ingame/menu.cpp`, `i18n.*` | — | multiplataforma (std::filesystem, ImGui, datos) |

### 2.2 SDK — subsistemas (resumen)

| Subsistema | Linux | Notas |
|---|---|---|
| core (base) | ✅ `*_posix` | threading/clock/seh/memory/dynlib/filesystem/exception/socket/system |
| filesystem | ✅ | `std::filesystem` + `FileHandle` (Win/Posix) |
| graphics | ✅ **Vulkan** | D3D12 solo Windows; Vulkan experimental y lento (reto de perf) |
| ui | ✅ | `surface_gnulinux.cpp`; deps `x11-xcb` + `wayland-client` |
| input | ✅ | SDL por defecto; XInput solo Windows |
| audio | ✅ | SDL / NOP |
| kernel/ppc | ✅ | guards `#if REX_PLATFORM`; codegen portable (verificar `ppc/`) |

---

## 3. HECHO en 1.1.1 (bases de código)

1. **`settings.cpp`**: MD5 portable (RFC 1321, ~120 líneas) para `CheckDefaultXex`
   — elimina la dependencia de CryptoAPI. Detección de GPU DXGI protegida
   (`GetPrimaryGpu/DetectGpuName/DetectGpuTier`) con fallback (tier medium).
   Defaults de backend por plataforma: `dbz3_gpu_backend` = `d3d12`/`vulkan`,
   `dbz3_input_backend` = `xinput`/`sdl`.
2. **`launcher_state.cpp`**: `PickFolder`/`PickFile` (COM) y `Utf8ToWide`/
   `WideToUtf8` bajo `#if REX_PLATFORM_WIN32`; fuera de Windows devuelven
   "cancelado" (el launcher mantiene las rutas por defecto).
3. **`mod_pipeline.cpp`**: el lanzador de scripts (`CreateProcessW`) protegido
   con fallback de error claro.
4. **`mods.cpp`**: ya portable (fallback para el instalador de zips).
5. **`main.cpp`**: `OutputDebugStringA` no-op fuera de Windows; pre-flight
   portable (MessageBox en Windows / stderr en el resto).

Con esto, `src/` **compila conceptualmente en Linux** (sin dependencias Win32
obligatorias en rutas de build).

---

## 4. Pendiente para un build Linux real

Orden de trabajo recomendado:

### 4.1 Build toolchain
1. **Preset CMake `linux-release`** en `CMakePresets.json`: compilador clang,
   flags baseline `-march=x86-64 -mssse3` (igual que el SDK baseline de
   Windows), `DBZ3_DUAL_REGION=ON`, `DBZ3_GENERATED_DIR=generated`.
2. **Dependencias** (apt/pacman): vulkan (libvulkan-dev), SDL3, X11-xcb,
   wayland-client, xdg. El CMake del SDK ya las contempla.
3. **Recompilar el codegen**: `generated/` (US) y `generated_eu/` (EU) se
   regeneran con `rexglue codegen` (el recompilador es portable). Los `.xex`
   descifrados son los mismos → los `dbz3_config*.toml` funcionan igual.
4. **DLLs → .so**: el plugin GPU se carga por nombre (`gpu_plugin_loader.cpp` ya
   distingue `.dll/.dylib/.so`). El runtime Linux produce `librexruntime.so`,
   `librexgpu-xenos.so`, FFX vk.

### 4.2 Funcional pendiente (launcher)
1. **Diálogos de archivo portables**: `PickFolder`/`PickFile` hoy devuelven
   "cancelado". Opciones: (a) diálogo SDL (pequeño, sin deps extra), (b) invocar
   `zenity`/`kdialog` (común en escritorios Linux), (c) integración GTK/Qt.
   Recomendado: (b) para el primer port, (a) como meta.
2. **Spawn de scripts portable**: `mod_pipeline.cpp` — reemplazar
   `CreateProcessW` por `posix_spawn`/`fork+exec` con pipe para capturar salida
   (mismo contrato que hoy).
3. **Instalación de zips**: `mods.cpp` — backend con libzip/minizip o `unzip`
   externo (mismo flujo: extraer a temp → normalizar layout → mover a mods/).
4. **Gestion de `mod center hd/`**: los scripts (`swap_b3.py`, `texture_b3.py`)
   llaman a `xbcompress.exe`/`xbdecompress.exe` (binarios XDK, solo Windows).
   En Linux hay que: (a) portar la compresión LZX (el SDK ya tiene
   `mspack`/`libmspack` — ver si expone LZX), o (b) ejecutar vía Wine, o
   (c) marcar las herramientas como Windows-only en el launcher.

### 4.3 Validación de Vulkan como backend de juego (el reto)
- Vulkan ya existe y renderiza, pero es **6.5x más lento que D3D12 en IssueSwap**
  (§3 / AGENTS). El port Linux entero depende de que Vulkan sea fluido.
- **Plan**: perfilado con Tracy (build `win-amd64-tracy`/Linux equivalente),
  buscar cuellos en el command processor / barriers / fences del path Vulkan,
  y el fix de pacing ya blindado (§14.17 clamp vsync a 60 Hz) aplicar igual.
- Si Vulkan no alcanza el rendimiento, alternativas: DXVK no aplica (el render
  es nativo); sería revisar el path Vulkan a fondo (está en el SDK).

### 4.4 Verificaciones
- Barrido fino de `ppc/` y `codegen/` del SDK en busca de Win32 residual
  (no encontrado en el grep inicial, confirmar).
- Smoke test en Linux: launcher shown + first present OK (log Vulkan) + intro
  del guest sin FATAL.

---

## 5. Alcance por fases

| Fase | Contenido | Resultado |
|---|---|---|
| **1.1.1 (hecho)** | Guards de plataforma en `src/` + MD5 portable + defaults por plataforma + docs | `src/` portable; estrategia documentada |
| **1.2 (próxima)** | Preset CMake Linux + build del core dual en Linux + diálogos/spawn/zips portables | Primer binario Linux que arranca el launcher |
| **1.3** | Vulkan fluido (perfilado + optimización) | Juego jugable en Linux |
| **1.4** | Toolkit de modding en Linux (LZX portable o Wine) + release Linux | Release Linux pública |

---

## 6. Riesgos y decisiones abiertas

- **Rendimiento Vulkan** (crítico): sin Vulkan fluido no hay release Linux.
- **Mando**: SDL por defecto (XInput no existe en Linux) — el mando USB/Bluetooth
  genérico debería funcionar; validar deadzone/rumble (§14.7) en Linux.
- **Distribución**: AppImage/Flatpak vs tar.gz con dependencias documentadas.
  Decidir cuando exista el primer binario jugable.
- **El port de modelos y el modding dependen de las herramientas de compresión
  LZX**: es el bloqueador de paridad de características, no del juego base.