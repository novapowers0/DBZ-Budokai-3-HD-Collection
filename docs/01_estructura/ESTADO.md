# Estado actual del proyecto

> Actualizado: 2026-09-26 (tras la **v1.2.9 Latest**). Referencia operativa:
> `AGENTS.md`. Detalle release a release: `01_estructura/HISTORICO_RELEASES.md`
> §A y los docs de sesión. Port PS2→HD **aparcado** (AGENTS §3.4.10).

---

## QUÉ FUNCIONA

| Cosa | Estado | Notas |
|---|---|---|
| **El juego arranca y se juega** | ✅ | D3D12 principal, 60,0 fps, núcleo dual US+EU. `out\build\win-amd64-release\dbz3.exe` |
| **Núcleo dual US+EU** | ✅ | Un solo exe detecta el xex por MD5 (US `A53E…`/EU `C37E…`) |
| **Auto-detección del ejecutable (v1.2.2)** | ✅ | Lo busca por **tamaño+MD5** (`DBZ3\yae3_xenon.xex`, `assets\DBZ3\`, …) y lo cachea en `user_data/dbz3/xex_cache\`. Sin renombrar nada |
| **Volcado retail del disco** | ✅ | Raíz = menú HD Collection (3317760 B) + `DBZ3\`: monta `DBZ3\` como unidad de juego y arranca |
| **Menú HD Collection / DBZ1 detectados** | ✅ | `kHdMenu`/`kDbz1` bloquean Play con mensaje claro (`XexStatus`) |
| **Fix TOML + autorreparación (v1.2.2 + v1.2.6)** | ✅ | `EscapeTomlStrings` idempotente + `LoadUserSettings` valida con toml++ y repara (`ConfigLoadState`); `.bak` si es irreparable |
| **Modo disco (ISO)** | ✅ | Juega directo del `.iso` (XDVDFS) sin extraer; extrae solo el xex a `iso_cache/`; `RegionDiscDevice` remapea región y prefija `DBZ3\`; fallback carpeta→ISO. ⚠️ **Los mods NO se aplican en ISO** |
| **Crash EU Dragon Universe** | ✅ | Fix `0x8215B378` + `fix_eu_bctr.py` (aplicar tras re-codegen) |
| **Launcher custom** | ✅ | Tabs: Video/Upscaling/Audio/Input/Mods/Model Swap/Texturas/Dev |
| **Mod de música** (`og_music`) | ✅ | Reemplaza ADX/SFD (override de archivo completo) |
| **Mod de texturas B3 HD** | ✅ | `texture_b3.py` + pestaña Texturas; override por entrada (~118 KB) |
| **Packs de texturas (v1.2.7)** | ✅ | Estilo PCSX2; volcado dev + cargador en runtime (D3D12 y Vulkan) |
| **Swap nativo B3→B3** | ✅ | `swap_b3.py` + pestaña Model Swap; override por entrada (~100 KB) |
| **Swaps en cualquier dirección** | ✅ | **Mid-insert virtual**: bins > o < que el slot (Goten 107006 B en slot Krillin 106496 B) |
| **2+ mods simultáneos** | ✅ | Cada mod toca entradas distintas del mismo AFS |
| **Mejora de texturas HD** | ✅ | `dbz3_hd_textures` x2/x3, DXT + RGBA8 nativas, mips; coste en VRAM. Off por defecto |
| **Diagnóstico v1.2.9** | ✅ | Avisos SIEMPRE activos (fps sostenido, disco lento, instalación mixta), `vram=`/`lim=` en `perf`, línea `entorno` |
| **Exportación/verificación del bin HD** | ✅ | `awo_tools/awg_to_obj_b3.py`, `awg0_export.py`, `awg_cara_export.py`; `analyze_bin_hd.py` obsoleto |
| **Extracción PS2→datos** | ✅ | `parse_ps2_mesh.py` (AMG PS2) |

## QUÉ NO FUNCIONA / APARCADO

| Cosa | Estado | Causa |
|---|---|---|
| **Port PS2→HD completo (Vía B)** | ⏸️ Aparcado (2026-09-13) | Geometría y draw CORRECTOS (1 draw strip, VB+IB verbatim); bloqueo = `M_bind` real + mapeo hueso→slot (σ). Ver AGENTS §3.4.5/§3.4.10 |
| **Inyección PS2→HD (Vía A)** | ✅ Aproximada | No re-topologiza (cuerpo PS2 + extremidades/cabeza HD); umbral binario 0.8 |
| **Port de personajes IW→B3** | 🔴 Descartado | Janemba fracasó (formato/retargeting). No reintentar sin conversor validado |
| **Bajones de FPS con escala>1x + texturas** | 🟡 Vigilado | Issue #8 abierto: comentado el fix de la v1.2.8.2, esperando el log `perf` del reporter |
| **Pausa real al perder el foco** | 🔴 No viable | No hay mecanismo seguro; solo mute/dim (QoL v1.2.5) |

---

## LOS FIXES DEL OVERRIDE (descubiertos)

1. **Hook `AfsFindModOverride` solo soportaba archivo directo**, no carpeta
   (`mods/<mod>/us/<afs>/<entry>/<file>`). Portado el manejo de carpetas del B1.
2. **Compresión**: el juego usa LZX `/N:2048`, no `/N:32`. Con `/N:32` el bin
   excedía el slot → el guest truncaba el LZX → crash.
3. **Padding**: el bin del mod se paddea al `to_read` del slot
   (`ceil(size/0x1000)*0x1000`, p.ej. 106496 para la entrada 327).
4. **Off-by-one de la tabla AFS**: los scripts leían la tabla en offset 0x10, el
   runtime en offset 8 → desfase de 1 entrada (bin N = física N+1). Corregido a
   offset 8. Era la causa del crash de tex_91.
5. **🔴 Mid-insert virtual (2026-08-18)**: para bins que EXCEDEN el `to_read` del
   slot, el runtime presenta al guest una **tabla AFS virtual consistente**: la
   entrada crece in-place y las posteriores se desplazan; las lecturas se traducen
   al archivo físico (`AfsVirtualRange`), sin materializar ficheros gigantes.

> Detalle completo del pipeline de mods en `AGENTS.md` §6 y
> `02_mods/COMO_HACER_MODS.md`.

---

## NOTA HISTÓRICA: RE-CODEGEN EU (2026-09-10)

- El re-codegen EU **no es reproducible** con el config actual: el recompilador
  genera símbolos SIN prefijo `dbz3eu_` → colisión con US en el build dual. Por
  eso los fixes EU se aplican **MANUALMENTE** al codegen.
- Las entradas de `dbz3_config_eu.toml` deben ir SIEMPRE dentro de `[functions]`,
  ANTES del primer `[[switch_tables]]` (si no, se pierden en cada re-codegen).
- El cvar `dbz1_diag_logging` vive en `rexruntime.dll`; si el build dual falla al
  enlazar `roster_trace.cpp`, recompilar el runtime baseline y reinstalar DLL+lib.
- Backup del codegen EU probado: `out/analysis/codegen_backup_20260909/`.

## DATOS DE REFERENCIA

Los datos que vivían en `%TEMP%\opencode\` (b327_*.bin, cell_*.bin, …) **ya NO
existen** (limpieza 2026-09-02): regenerar desde `us/` + `ps2_games/` con las
herramientas de `awo_tools/` (`rt_327.bin` = Krillin entrada 327 descomprimida).
