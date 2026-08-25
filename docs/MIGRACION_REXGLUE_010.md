# MIGRACIÓN A REXGLUE 0.10.0 — ESTADO Y PUNTO DE REANUDACIÓN

> Actualizado: 2026-08-25. Documento de trabajo para retomar la migración
> del SDK ReXGlue **0.9.0 → 0.10.0** en una sesión futura sin perder contexto.
> **✅ MIGRACIÓN COMPLETA Y VALIDADA EN JUEGO (2026-08-25)**: SDK 0.10.0
> instalado en `rexglue/` (respaldo `rexglue_0.9/`), dbz3.exe compilado contra
> 0.10, mods y mid-insert virtual funcionando. Solo queda actualizar
> release/README antes de subir a GitHub (cuando se complete el plan general).

---

## 1. OBJETIVO (P0 de la hoja de ruta)

Subir el SDK de `rexglue-sdk` (base **0.9.0**, `CMakeLists.txt` `VERSION 0.9.0`)
a **0.10.0** (release estable `v0.10.0`, tag 2026-08-21, commit `f5337cd`),
re-aplicando el **parche del runtime** (mid-insert virtual + override de
archivo completo + cvars dbz1) que es el corazón del sistema de mods.

Beneficios esperados de 0.10.0: mejoras de input (`comma-list binds`, `gate mnk
mouse look`), `cvar track value source`, mejor crash-reporting (`report guest
arena base`, APCs por trap frame, cr2-cr4), `imgui style hook`.

---

## 2. QUÉ SE HA HECHO (COMPLETADO) — 2026-08-21

### 2.1 SDK 0.10.0 clonado y submodulos
- **Ruta**: `rexglue-sdk-0.10/` (hermano de `rexglue-sdk/`, NO es repo git del
  proyecto; es un clone git del tag `v0.10.0`).
- Clone: `git clone --branch v0.10.0 --single-branch --depth 1
  https://github.com/rexglue/rexglue-sdk.git rexglue-sdk-0.10`
- Submodulos: `git -C rexglue-sdk-0.10 submodule update --init --depth 1` (22
  submodulos descargados OK).

### 2.2 Build configurado y compilado (rexruntime)
Comando idéntico al 0.9:
```powershell
cmake -G Ninja -S rexglue-sdk-0.10 -B rexglue-sdk-0.10\out\build-win-vulkan `
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" `
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" `
  -DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe" `
  -DREXGLUE_ENABLE_FIDELITYFX=ON -DREXGLUE_USE_VULKAN=ON `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64-v3"
```
Config summary: Version 0.10.0, C++23, D3D12=ON Vulkan=ON FidelityFX=ON.

### 2.3 🔴 DOS BUGS DEL BUILD 0.10.0 RESUELTOS (workarounds locales)

**Bug 1 — libmspack (wrappers)**: el submodulo `libmspack` en 0.10 reestructuró
`cabextract/mspack/`: los `.c`/`.h` ahora son **wrappers** con un path relativo
(`../../libmspack/mspack/lzxd.c`) en vez de código real. El CMake de ReXGlue
compila `cabextract/mspack/lzxd.c` como fuente → falla `expected identifier or '('`.
- **Fix**: copiar el código real de `thirdparty/libmspack/libmspack/mspack/`
  sobre `thirdparty/libmspack/cabextract/mspack/` (ReXGlue solo necesita lzxd.c).

**Bug 2 — FidelityFX .rc UTF-16**: `ffx_api_dll.rc` (en `out/build-win-vulkan/
_deps/fidelityfx-src/ffx-api/src/resource/`) está en **UTF-16 LE con BOM**, y
`llvm-rc.exe` no lo soporta → `fatal error: UTF-16 byte order mark detected`.
- **Fix**: convertir el archivo a **UTF-8 sin BOM**
  (`ReadAllText(Unicode)` + `WriteAllText(UTF8Encoding(false))`).
- ⚠️ Ambos workarounds se pierden si se re-clona/limpia el build. Están en
  `out/build-win-vulkan/_deps/` (ffx) y `thirdparty/libmspack` (local).

### 2.4 ✅ Parche del runtime PORTADO a 0.10 (los 4 archivos + 3 cvars)

En 0.10 el filesystem fue refactorizado: `src/filesystem/afs.cpp` y
`include/rex/filesystem/afs.h` **YA NO EXISTEN** (se eliminaron), y
`host_path_file.cpp` (66 líneas) / `host_path_entry.cpp` (183 líneas) son mucho
más simples (sin lógica AFS/override). El parche completo NO se copia tal cual:
**hay que portarlo**. Esto ya está hecho:

1. **`include/rex/filesystem/afs.h`** — creado (copiado del parche 0.9, API
   compatible: `AfsFindEntry`, `AfsFindModOverride`, `AfsFindModFileOverride`,
   `AfsListMods`, `AfsResetModCache`, `AfsSetModEnabled`, `AfsRegionFileName`,
   `AfsGetVirtualTable`, `AfsTranslateOffset`, `AfsModsRoot`).
2. **`src/filesystem/afs.cpp`** — creado (copiado del parche 0.9, compila sin
   cambios; usa `rex::path_to_utf8` y `rex::filesystem::GetExecutableFolder`
   que SÍ existen en 0.10).
3. **`src/filesystem/CMakeLists.txt`** — añadido `afs.cpp` a `rexfilesystem`.
4. **`src/filesystem/devices/host_path_file.cpp`** — portada la lógica de
   `ReadSync`: tabla AFS virtual (mid-insert), `AfsTranslateOffset`, override
   por entrada (`AfsFindModOverride`). El hook es idéntico al 0.9 (mismo
   `ReadSync`).
5. **`src/filesystem/devices/host_path_entry.cpp`** — portado `Open()`:
   overrides de audio (`dbz1_audio_jp`), región (`dbz1_region`), diag log y
   `AfsFindModFileOverride` (archivo completo). El `Open()` 0.10 era simple.

### 2.5 ✅ Los 3 archivos de cvars dbz1 restaurados en 0.10

0.10 eliminó los 3 archivos que definen los cvars compartidos (necesarios para
linkear `REXCVAR_DECLARE` en host_path_entry.cpp):
- `src/system/dbz1_audio_jp_flag.cpp`
- `src/system/dbz1_diag_flags.cpp`
- `src/system/dbz1_region_flag.cpp`

Copiados desde el SDK 0.9 y añadidos a `src/system/CMakeLists.txt`
(`REXSYSTEM_SOURCES`). **Sin estos, el link de rexruntime falla** con
`undefined symbol: FLAGS_dbz1_*_storage_(void)`.

### 2.6 ✅ Resultado: rexruntime.dll 0.10.0 COMPILADO

- `cmake --build rexglue-sdk-0.10/out/build-win-vulkan --target rexruntime` → **exit 0**.
- Output: `rexglue-sdk-0.10/out/win-amd64/rexruntime.dll` (**10933248 B**).
- Verificado: contiene los marcadores del parche
  `AfsGetVirtualTable`, `AfsTranslateOffset`, `AfsFindModOverride`,
  `AfsFindModFileOverride` (Select-String = True).
- ⚠️ Aún NO se ha construido `rexgpu-xenos.dll` (la GPU plugin) ni el resto del
  SDK completo, ni se ha instalado en `rexglue/`.

---

## 3. LO QUE FALTA (PRÓXIMOS PASOS EN ORDEN)

### 3.1 Construir el resto del SDK 0.10 (GPU plugin y tooling)
El juego enlaza `rex::gpu-xenos` (rexgpu-xenos.dll) además de `rex::runtime`.
```powershell
cmake --build rexglue-sdk-0.10/out/build-win-vulkan   # todo (o --target rexgpu-xenos)
```

### 3.2 Instalar el SDK 0.10 en `rexglue/` (la instalación local que usa el juego)
El juego resuelve el SDK así (verificado en `out/build/win-amd64-release/CMakeCache.txt`):
- `CMAKE_PREFIX_PATH = .../rexglue`
- `rexglue_DIR = .../rexglue/lib/cmake/rexglue`

**Plan de instalación** (NO borrar `rexglue/` sin respaldo del 0.9):
1. **Respaldo** del SDK 0.9 instalado: renombrar `rexglue/` → `rexglue_0.9/`
   (o copia) para poder revertir.
2. **Instalar el 0.10**: si el SDK 0.10 tiene target de `install`
   (`cmake --install rexglue-sdk-0.10/out/build-win-vulkan --prefix rexglue`),
   usarlo. Si no genera `lib/cmake/rexglue/`, replicar manualmente la estructura
   del 0.9: `bin/` (DLLs), `lib/` (.lib + cmake configs), `include/` (headers),
   `cmake/` (configs SDL3), `licenses/`, `share/`.
   - Mínimo imprescindible para compilar el juego: `rexglue/lib/rexruntime.lib`,
     `rexglue/lib/rexruntime.dll` (o bin/), `rexglue/include/`, `rexglue/lib/cmake/rexglue/`,
     y las libs que enlaza (fmt, spdlog, SDL3, mspack, xxhash, simde, etc. — el
     `install` del SDK los exporta automáticamente).
3. **Verificar** que `find_package(rexglue)` encuentra la versión 0.10.

**Alternativa más segura** (sin tocar `rexglue/` todavía): re-configurar el
build del juego con `-DCMAKE_PREFIX_PATH=.../rexglue-sdk-0.10/out/build-win-vulkan`
o un `install` del 0.10 en una ruta nueva, y compilar `dbz3` contra ella. Así el
0.9 queda intacto y es fácil revertir.

### 3.3 Compilar el juego (dbz3.exe) contra 0.10
```powershell
cmake --build "out\build\win-amd64-release"
```
⚠️ **Lección conocida (AGENTS §13.6/§14)**: al recompilar, el cmake puede
sobrescribir `rexruntime.dll` del build con la instalada en `rexglue/bin`.
Después del build, copiar la DLL 0.10 correcta al build y al
`github/release-stage/`.

### 3.4 Re-validar los fixes propios del proyecto sobre 0.10
- **Input**: `input_backend = "xinput"` (fix del cuelgue con RTSS/OBS) — verificar
  que sigue funcionando (el 0.10 cambió input: `gate mnk mouse look`,
  `comma-list binds`).
- **`CallInUIThreadSynchronous` timeout** (windowed_app_context.cpp) — revisar si
  0.10 lo incorpora o hay que re-aplicarlo.
- **Presenter pacing** (`WaitForUITickFromUIThread`) — verificar.
- **Launcher**: el proyecto enlaza cvars del runtime (`REXCVAR_DECLARE(bool,
  dbz1_diag_logging)` en settings.cpp) — confirmar que el símbolo
  `FLAGS_dbz1_diag_logging_storage_()` se exporta igual en el 0.10.

### 3.5 Probar en juego (end-to-end)
- Arrancar el launcher → Play → entrar en combate.
- Verificar que los mods siguen funcionando: `tex_91` (entrada 91) y algún swap
  (p.ej. `sw_vegeta424`, entrada 327 con mid-insert virtual — EJERCITA la tabla
  virtual, el punto más delicado).
- Verificar región us/eu, audio JP, y que NO se generan .bmp con Dev+Diag off.

### 3.6 Actualizar parches del repo (github/patches/)
Cuando el port esté validado, actualizar `github/patches/` con los archivos
0.10 (ahora son 6: `afs.h`, `afs.cpp`, `host_path_file.cpp`, `host_path_entry.cpp`
+ los 3 `dbz1_*_flag.cpp`, y los 2 CMakeLists modificados) + README del parche
explicando la refactorización. Decidir si mantener también los parches 0.9 para
quien use 0.9.

---

## 4. REFERENCIAS Y DATOS ÚTILES

| Ítem | Ruta / valor |
|---|---|
| SDK 0.10 clonado | `rexglue-sdk-0.10/` (git tag v0.10.0, commit f5337cd) |
| SDK 0.9 (actual) | `rexglue-sdk/` (CMakeLists `VERSION 0.9.0`) |
| Instalación que usa el juego | `rexglue/` (CMAKE_PREFIX_PATH → `rexglue/lib/cmake/rexglue`) |
| Build SDK 0.10 | `rexglue-sdk-0.10/out/build-win-vulkan/` |
| Output SDK 0.10 | `rexglue-sdk-0.10/out/win-amd64/` |
| rexruntime.dll 0.10 (con parche) | `rexglue-sdk-0.10/out/win-amd64/rexruntime.dll` (10933248 B) |
| Patches originales (0.9) | `github/patches/rexglue-sdk/` (4 archivos + README) |
| Build del juego | `out/build/win-amd64-release/` |
| DLLs del build de juego | `out/build/win-amd64-release/rexruntime.dll`, `rexgpu-xenos.dll` |
| Tools | `C:/Program Files/LLVM/bin/clang++.exe` (22.1.8), `ninja` 1.13.2 |

### Flujo exacto del parche (qué hace cada archivo)
1. `afs.cpp`/`afs.h` → parseo de índice AFS + mods + tabla virtual mid-insert.
2. `host_path_file.cpp::ReadSync` → intercepta lecturas de `data_cmn.afs`: sirve
   la tabla virtual (cabecera+tabla), traduce offsets, o sirve el override.
3. `host_path_entry.cpp::Open` → override de archivo completo + audio JP + región.

### Logs de verificación del parche en juego (formato esperado)
```
AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```

---

## 5. RIESGOS Y DECISIONES PENDIENTES

- **Breaking API**: 0.10 es "early development"; la refactorización del
  filesystem ya rompió el patch (hubo que portarlo). Otros subsistemas (input,
  presenter) pueden tener cambios que afecten a los fixes del launcher.
- **mspack/FFX workarounds**: si se borra el build 0.10 hay que re-aplicar los 2
  fixes (§2.3). Documentar en el README del parche.
- **Decisión**: instalar 0.10 sobre `rexglue/` (con respaldo `rexglue_0.9/`) vs
  usar un install separado. Recomendado: **instalar en `rexglue/` con respaldo**
  porque el flujo del proyecto ya está configurado así (CMAKE_PREFIX_PATH fijo).
- **No tocar todavía** `rexglue-sdk/` (0.9): sigue siendo el SDK funcional del
  proyecto hasta que el 0.10 pase la validación en juego.

---

## 6. RESUMEN DE ESTADO

- [x] Release 0.10.0 confirmado (v0.10.0, 2026-08-21)
- [x] SDK 0.10 clonado + submodulos
- [x] Build configurado (D3D12+Vulkan+FFX, clang 22, x86-64-v3)
- [x] 2 bugs del build resueltos (mspack wrappers, ffx .rc UTF-16)
- [x] Parche del runtime portado (afs.h/afs.cpp recreados, host_path_* portados)
- [x] 3 cvars dbz1 restaurados + CMake
- [x] **rexruntime.dll 0.10.0 compilado con parche (10933248 B, marcadores OK)**
- [x] **rexgpu-xenos.dll 0.10.0 compilado (6207488 B) + SDK completo (rexglue.exe, libs)**
- [x] **SDK 0.10 instalado en `rexglue/` (respaldo 0.9 → `rexglue_0.9/`) — PACKAGE_VERSION 0.10.0**
- [x] **dbz3.exe compilado contra 0.10 (17298432 B, 2026-08-25)** — ver §7
- [x] **Fixes propios re-validados: ambos del SDK ya son nativos en 0.10** — ver §7.3
- [x] **✅ VALIDADO EN JUEGO (usuario, 2026-08-25)**: mods `swap_96_on_327`
      (override simple) + `tex_91` OK, y **mid-insert virtual OK**
      (`sw_vegeta424`, bin 126976 > to_read 106496). **Migración COMPLETA.**
- [x] **Actualizar github/patches/ (9 archivos 0.10) + README** — hecho 2026-08-25
- [ ] **Cuando se complete el plan general**: actualizar release/README + README
      para el SDK 0.10 antes de subir a GitHub

---

## 7. SESIÓN 2026-08-25 — SDK COMPLETO, INSTALACIÓN Y JUEGO COMPILADO

### 7.1 Qué se hizo
1. `cmake --build rexglue-sdk-0.10/out/build-win-vulkan --target rexgpu-xenos`
   → rexgpu-xenos.dll 0.10 (6207488 B). Build completo del SDK → rexglue.exe
   también compilado.
2. **Instalación**: `rexglue/` → renombrado a `rexglue_0.9/` (respaldo), luego
   `cmake --install rexglue-sdk-0.10/out/build-win-vulkan --prefix rexglue`.
   Verificado: `rexglueConfigVersion.cmake` PACKAGE_VERSION = "0.10.0",
   `rexglue/bin/{rexruntime,rexgpu-xenos,amd_fidelityfx_dx12,TracyClient}.dll`,
   `rexglue/lib/rexruntime.lib` (5997310 B) con los 3 símbolos dbz1 exportados
   (llvm-nm: `FLAGS_dbz1_{diag_logging,audio_jp,region}_storage_` = T).
3. **Compilar dbz3 contra 0.10**:
   - El build del juego estaba configurado contra el **SDK del proyecto hermano
     dbz1** (`DBZ Budokai HD Collection/rexglue-sdk/out/install/win-amd64`,
     Debug) — rexglue_DIR hardcodeado en el cache. Se reconfiguró limpio
     (borrado CMakeCache.txt + CMakeFiles) con `CMAKE_PREFIX_PATH=.../rexglue`
     y `CMAKE_BUILD_TYPE=Release`.
   - **🔴 CAUSA RAÍZ del fallo de compilación (2 errores)**:
     a) `cmake --preset win-amd64-release` resolvía `clang` vía PATH al
        toolchain **retcomm (x86_64-w64-windows-gnu, libstdc++)**, que NO tiene
        `std::chrono::clock_time_conversion` → error en `rex/chrono/chrono.h`.
        Fix: usar explícitamente `C:/Program Files/LLVM/bin/clang++.exe`
        (target MSVC). El build original ya usaba el clang de LLVM.
     b) El código generado (0.9) usaba `REX_WEAK_FUNC`, macro **eliminada en
        0.10** (el template 0.10 `pch_h.inja` emite `DEFINE_REX_FUNC` sin ella).
        Fix: **regenerar `generated/` con el codegen 0.10**:
        `cmake --build out/build/win-amd64-release --target dbz3_codegen`
        (respaldo previo de `generated/` en %TEMP%). 94 archivos escritos,
        `dbz3_manifest.toml` auto-sellado `sdk_version = "0.10.0"`, ahora 44
        archivos `dbz3_recomp.*.cpp`.
   - Build OK: dbz3.exe (17298432 B). El `rexglue_configure_target` staged las
     DLLs 0.10 automáticamente (rexruntime.dll 10933248, rexgpu-xenos.dll
     6207488). Copiadas además `amd_fidelityfx_dx12.dll` (5420544, 0.10) y
     `TracyClient.dll` del install.
   - ⚠️ En 0.10 NO hay `amd_fidelityfx_vk.dll` (solo DX12). El Vulkan
     experimental sin FFX Vulkan.
4. **Smoke test**: dbz3.exe arranca, D3D12 init (RTX 4070 SUPER), guest arena
   mapeado, "launcher shown, waiting for Play". Sin crash.
5. **Validación de fixes propios** (§3.4):
   - **CallInUIThreadSynchronous timeout**: YA incorporado en 0.10
     (`windowed_app_context.cpp` líneas 97-132, con mensaje de timeout).
   - **Presenter pacing** (`WaitForUITickFromUIThread`): YA incorporado en 0.10
     (`presenter.cpp` líneas 1629-1646, `ui_tick_last_paint_time_` + sleep_until).
   - **REXCVAR dbz1**: símbolos exportados por la lib 0.10 y el exe enlaza bien.
   - **Input xinput**: cvar del proyecto, flujo sin cambios; validar en juego.

### 7.2 Estado del build de juego (win-amd64-release) tras la migración
- dbz3.exe 17298432 B (25/08 12:58) — Release, enlazado contra 0.10.
- rexruntime.dll 10933248 B (0.10, parche verificado: AfsGetVirtualTable /
  AfsFindModOverride / AfsFindModFileOverride = True).
- rexgpu-xenos.dll 6207488 B (0.10).
- amd_fidelityfx_dx12.dll 5420544 B (0.10).
- Mods activos: `swap_96_on_327` (override simple, 106496 B exacto) y `tex_91`.
- **Limpieza hecha (25/08)**: eliminados los ~28 `black_*.bmp`/`frontbuf_*.bmp`
  de diagnóstico (~880 MB) y las DLLs de debug obsoletas (`rexruntimerd.dll`,
  `rexgpu-xenosrd.dll`, `TracyClientrd.dll`, `SPIRV-Tools-sharedrd.dll`,
  `amd_fidelityfx_*drel.dll`). Queda `amd_fidelityfx_vk.dll` (9332432 B, 0.9):
  el Vulkan experimental lo sigue usando (0.10 no trae FFX Vulkan).

### 7.3 ⚠️ Notas para futuras sesiones
- El build del juego usa `CMAKE_PREFIX_PATH=.../rexglue` (0.10). NO usar
  `cmake --preset` sin `-DCMAKE_CXX_COMPILER=...LLVM/clang++.exe`.
- Si se re-clona el SDK 0.10 hay que re-aplicar los 2 workarounds del build
  (§2.3) y los 9 archivos del parche (github/patches/).
- El SDK 0.9 (`rexglue-sdk/`) NO se toca; el respaldo `rexglue_0.9/` permite
  revertir si algo falla (ya no es el default).

---

## 8. ✅ VALIDACIÓN EN JUEGO (2026-08-25, usuario) — MIGRACIÓN COMPLETA

El usuario probó el juego compilado contra 0.10 y todo funcionó:

1. **Mods por override simple**: `swap_96_on_327` (Babidi→Krillin, geom.bin =
   106496 B = to_read exacto del slot) → Krillin muestra a Babidi. OK.
2. **Mod de texturas**: `tex_91` (Dr. Gero) → texturas aplicadas. OK.
3. **Mid-insert virtual**: `sw_vegeta424` (Vegeta armadura saiyan, bin 126976 B
   > to_read 106496 del slot 327) → Vegeta entra correctamente en combate con
   Krillin. **El mid-insert virtual por fin se validó en juego** (en 0.9 quedó
   sin validar porque la DLL del build estaba stale — ver AGENTS §13.6).

**Conclusión**: la migración 0.9.0 → 0.10.0 está COMPLETA. El parche del runtime
portado (afs.h/afs.cpp recreados + host_path_* refactorizados + 3 cvars dbz1)
funciona idéntico a 0.9, y los fixes del SDK que eran propios en 0.9 ya son
nativos en 0.10.

**Pendiente futuro (NO urgente, cuando se complete el plan general)**:
- Actualizar `release/README.md` + `README.md` para el SDK 0.10 (bump de
  versión, mención del mid-insert virtual validado).
- Subir a GitHub (el repo `github/` ya tiene parches y docs actualizados en
  local, sin commitear).