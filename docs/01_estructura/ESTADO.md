# Estado actual del proyecto

> Actualizado: 2026-09-19 (v1.2.4 EX Latest: **volumen REAL en el launcher**
> —ganancia `audio_gain` en el runtime— + **aviso de nueva version** consultando
> GitHub; **FXAA/dither**, **sensibilidad del raton**, **palancas de diagnostico
> GPU** (async shaders / occlusion queries) y **datos de usuario portables**
> —caen a `Documents/dbz3` si la carpeta del juego no es escribible—; eliminados
> los controles muertos de gamma y de volumen por categoria).
> Sobre la v1.2.3 (contador de FPS en partida, log AFS silenciado y Texturas HD
> en WIP/OFF), la v1.2.2 EX (el launcher encuentra el ejecutable solo: volcado
> retail del disco e ISO original arrancan sin renombrar nada), la v1.2.1
> (hotfix del launcher) y la v1.2.0 (Centro de mods + Model Swap).
> Port PS2→HD aparcado, ver §3.4.10 de AGENTS.md)

---

## QUÉ FUNCIONA

| Cosa | Estado | Notas |
|---|---|---|
| **El juego arranca y se juega** | ✅ | D3D12, 60fps, mando XInput. `out\build\win-amd64-release\dbz3.exe` |
| **Núcleo dual US+EU** | ✅ | Un solo exe detecta el xex por MD5 (US `A53E...`/EU `C37E...`). v1.1.3 "El parche de la ISO" |
| **Auto-detección del ejecutable (v1.2.2)** | ✅ | Lo busca por **tamaño+MD5** (`DBZ3\yae3_xenon.xex`, `assets\DBZ3\`, …) y lo cachea en `user_data/dbz3/xex_cache\`. Sin renombrar nada |
| **Volcado retail del disco** | ✅ | Raíz = menú HD Collection (3317760 B) + `DBZ3\`: monta `DBZ3\` como unidad de juego y arranca (validado 2026-09-17) |
| **Menú HD Collection / DBZ1 detectados** | ✅ | `kHdMenu`/`kDbz1` bloquean Play con mensaje claro (antes: muerte críptica `No function registered at 820D54C8`) |
| **Fix TOML con rutas Windows (v1.2.2)** | ✅ | `EscapeTomlStrings` idempotente: se acabó el `unknown escape sequence '\G'` que perdía los ajustes |
| **Modo disco (ISO)** | ✅ | v1.1.3: selector de fuente siempre visible (carpeta extraída / ISO); juega directo del `.iso` sin extraer. v1.2.2 EX: extrae `DBZ3\yae3_xenon.xex` del ISO original, resuelve `DBZ3\us\...` y usa el ISO si la carpeta no arranca (validado con XDVDFS sintético) |
| **Crash EU Dragon Universe** | ✅ | Fix `0x8215B378` aplicado (2026-09-10, para v1.1.4). Boot EU validado sin FATAL |
| **Launcher custom** | ✅ | Tabs: Video/Upscaling/Audio/Input/Mods/Model Swap/Texturas/Dev |
| **Mod de música** (`og_music`) | ✅ | Reemplaza ADX/SFD, funciona (override de audio por AFS) |
| **Mod de texturas B3 HD** | ✅ | `texture_b3.py` + pestaña Texturas; override por entrada (~118KB) |
| **Swap nativo B3→B3** | ✅ | `swap_b3.py` + pestaña Model Swap; override por entrada (~100KB) |
| **Swaps en cualquier dirección** | ✅ | **Mid-insert virtual**: bins > o < que el slot funcionan (Goten 107006B en slot Krillin 106496B validado) |
| **2+ mods simultáneos** | ✅ | Cada mod toca entradas distintas del mismo AFS (goten_override_test + tex_91 = OK) |
| **Override por entrada (mecanismo)** | ✅ | `AfsFindModOverride` + tabla AFS virtual (`AfsGetVirtualTable`/`AfsTranslateOffset`) |
| **Exportación/verificación del bin HD** | ✅ | `awo_tools/awg_to_obj_b3.py`, `awg0_export.py` y `awg_cara_export.py`; `analyze_bin_hd.py` queda obsoleto |
| **Extracción PS2→datos** | ✅ | `parse_ps2_mesh.py` extrae vértices/IB de AMG PS2 |

## QUÉ NO FUNCIONA (PRIORIDAD)

| Cosa | Estado | Causa probable |
|---|---|---|
| **Port PS2→HD de personajes (Vía B)** | ⏸️ Aparcado (2026-09-13) | Geometría y draw CORRECTOS; bloqueo = `M_bind` real (el renderer no lo expone). Ver AGENTS §3.4.10 |
| **Inyección PS2→HD (Vía A)** | ✅ Aproximada | Techo conocido: no re-topologiza (cuerpo PS2 + extremidades/cabeza HD) |
| **Port de personajes IW→B3** | 🔴 Descartado | Janemba fracasó (formato/retargeting); archivado. Ver AGENTS §11.1 |

---

## LOS FIXES DEL OVERRIDE (descubiertos)

1. **Hook `AfsFindModOverride` solo soportaba archivo directo**, no carpeta
   (`mods/<mod>/us/<afs>/<entry>/<file>`). Portado el manejo de carpetas del B1.
2. **Compresión**: el juego usa LZX `/N:2048`, no `/N:32`. Con `/N:32` el bin
   excedía el slot → el guest truncaba el LZX → crash.
3. **Padding**: el bin del mod se paddea al `to_read` del slot
   (`ceil(size/0x1000)*0x1000`, p.ej. 106496 para la entrada 327).
4. **Off-by-one de la tabla AFS**: los scripts leían la tabla en offset 0x10,
   el runtime en offset 8 → desfase de 1 entrada (bin N = física N+1). Corregido
   a offset 8. Era la causa del crash de tex_91.
5. **🔴 Mid-insert virtual (2026-08-18)**: para bins que EXCEDEN el `to_read`
   del slot (p.ej. Goten 107006B > Krillin 106496B), el runtime presenta al
   guest una **tabla AFS virtual consistente**: la entrada crece in-place y las
   posteriores se desplazan (como un AFS reconstruido), y traduce las lecturas
   al archivo físico. Antes, el intento "naive" de inflar sizes manteniendo
   addr rompía el arranque (el guest recalcula offsets acumulando sizes).

---

## ESTADO DEL JUEGO AHORA MISMO

- **v1.2.3 publicada** (Latest, 2026-09-18): contador de rendimiento en partida
  (`dbz3_perf_logging`), log de overrides AFS silenciado (gateado por
  `dbz1_diag_logging`) y **Texturas HD (WIP, OFF por defecto**: upscale en runtime
  con mips; funciona pero da tirones). Base: v1.2.2 EX.
- **v1.2.2 EX publicada** (no-Latest, 2026-09-17; sustituye a la v1.2.2 plana, que se retiró): **arranque garantizado** — el
  launcher busca el ejecutable de Budokai 3 por tamaño+MD5 en la carpeta elegida
  (y en `DBZ3\`, `assets\`, `assets\DBZ3\`), lo prepara como
  `user_data/dbz3/xex_cache/default.xex` (sin escribir en la carpeta del usuario)
  y monta el data root correcto; el menú de la HD Collection se detecta y se
  bloquea con mensaje específico. Incluye el fix del `dbz3_user.toml` con rutas
  Windows y logs de diagnóstico del xex. Ver `RELEASE_README.md`.
- **v1.2.1 publicada** (no-Latest, 2026-09-14): crash al cerrar tras Model
  Swap/Texturas (hilo del pipeline sin unir), etiqueta de nitidez FSR invertida,
  refresco automático de la lista de mods. Sobre la **v1.2.0** (Centro de mods
  renovado + Model Swap HD↔HD pulido + aviso de modo ISO + nitidez FSR/CAS
  ajustable + limpieza de mods). Ver `RELEASE_README.md`.
- **Fix crash EU `0x8215B378`** (para v1.1.4): Dragon Universe EU ya no crashea al
  seleccionar personaje. Mismo tratamiento que `0x820F2398` (función plegada como
  dead fall-through, solo alcanzable vía puntero de función). Aplicado MANUALMENTE
  al codegen EU (4 sitios) + declarado en `dbz3_config_eu.toml` dentro de
  `[functions]`.
- **Runtime**: `rexruntime.dll` baseline (10.86 MB) con cvar `dbz1_diag_logging`
  (diagnóstico F3.1). El build del juego SOBRESCRIBE la DLL instalada — verificar
  siempre tras `cmake --build` (AGENTS §7).

## HALLAZGOS 2026-09-10 (sesión de depuración)

1. **El re-codegen EU NO es reproducible con el config actual**: el recompilador
   actual genera nombres SIN prefijo `dbz3eu_` (solo `sub_*`/`rex_*`), rompiendo
   el build dual (colisión de símbolos con US). El codegen probado (con prefijo)
   se generó con un rexglue.exe anterior. → **los fixes EU se aplican MANUALMENTE
   al codegen**, no vía re-codegen.
2. **Entradas del config EU tras `[[switch_tables]]` se ignoran**: `0x820F2398`
   estaba declarada fuera de `[functions]` y el recompilador la perdía en cada
   re-codegen. Regla: declarar SIEMPRE dentro de `[functions]`, antes del primer
   `[[switch_tables]]`.
3. **`dbz1_diag_logging` se perdió en la reinstalación del SDK**: la DLL
   instalada en `rexglue/bin` (recompilada 2026-09-09 23:29) no tenía el cvar →
   el build dual fallaba al enlazar `roster_trace.cpp`. Solución: recompilar
   `rexruntime rexgpu-xenos` del baseline (`rexglue-sdk-0.10/out/
   build-win-vulkan-baseline`) y reinstalar DLL+lib en `rexglue/`.
4. **Backup del codegen EU probado**: `out/analysis/codegen_backup_20260909/`
   (pre-fix). El diff de direcciones registradas confirmó que el único cambio vs
   el re-codegen era `0x820f2398` (perdida) → el xex `yae3_xenon_eu.xex` SÍ es el
   correcto.

---

## DATOS DE REFERENCIA (en `%TEMP%\opencode\`)

| Archivo | Contenido |
|---|---|
| `rt_327.bin` | Krillin visible (entrada 327 descomprimida, 682528B, 51 huesos, 18 AWGs) |
| `goten_298.bin` | Goten (entrada 298, 666752B, 56 huesos, 21 AWGs) |
| `b327_ps2.bin` | Krillin PS2 (#AMO0 LE) |
| `janemba.amb` | Janemba IW→B3 PS2 (48 huesos JNB) |
| `piccolo_hd.bin` | Piccolo B1 HD (el port que SÍ funcionó en el B1) |
