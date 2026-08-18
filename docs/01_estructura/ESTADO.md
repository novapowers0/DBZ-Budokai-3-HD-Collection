# Estado actual del proyecto

> Actualizado: 2026-08-18 (mid-insert virtual — swaps en cualquier dirección)

---

## QUÉ FUNCIONA

| Cosa | Estado | Notas |
|---|---|---|
| **El juego arranca y se juega** | ✅ | D3D12, 60fps, mando XInput. `out\build\win-amd64-release\dbz3.exe` |
| **Launcher custom** | ✅ | Tabs: Video/Upscaling/Audio/Input/Mods/Model Swap/Texturas/Dev |
| **Mod de música** (`og_music`) | ✅ | Reemplaza ADX/SFD, funciona |
| **Mod de texturas B3 HD** | ✅ | `texture_b3.py` + pestaña Texturas; override por entrada (~118KB) |
| **Swap nativo B3→B3** | ✅ | `swap_b3.py` + pestaña Model Swap; override por entrada (~100KB) |
| **Swaps en cualquier dirección** | ✅ | **Mid-insert virtual**: bins > o < que el slot funcionan (Goten 107006B en slot Krillin 106496B validado) |
| **2+ mods simultáneos** | ✅ | Cada mod toca entradas distintas del mismo AFS (goten_override_test + tex_91 = OK) |
| **Override por entrada (mecanismo)** | ✅ | `AfsFindModOverride` + tabla AFS virtual (`AfsGetVirtualTable`/`AfsTranslateOffset`) |
| **Parser del bin HD** | ✅ | `awo_tools/analyze_bin_hd.py` lee la estructura con la template oficial |
| **Extracción PS2→datos** | ✅ | `parse_ps2_mesh.py` extrae vértices/IB de AMG PS2 |

## QUÉ NO FUNCIONA (PRIORIDAD)

| Cosa | Estado | Causa probable |
|---|---|---|
| **Port PS2→HD de personajes** | ⚠️ Investigado | El HD es re-trabajo; requiere reconstrucción completa (sec34+IB+arms+submesh) |
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

- **Mods activos**: `goten_override_test` (Goten→Krillin por override) + `tex_91`
  (texturas de Gero) — ambos a la vez, validado.
- **Runtime**: `rexruntime.dll` con el parche mid-insert virtual (ver
  `patches/` en el repo).

---

## DATOS DE REFERENCIA (en `%TEMP%\opencode\`)

| Archivo | Contenido |
|---|---|
| `rt_327.bin` | Krillin visible (entrada 327 descomprimida, 682528B, 51 huesos, 18 AWGs) |
| `goten_298.bin` | Goten (entrada 298, 666752B, 56 huesos, 21 AWGs) |
| `b327_ps2.bin` | Krillin PS2 (#AMO0 LE) |
| `janemba.amb` | Janemba IW→B3 PS2 (48 huesos JNB) |
| `piccolo_hd.bin` | Piccolo B1 HD (el port que SÍ funcionó en el B1) |
