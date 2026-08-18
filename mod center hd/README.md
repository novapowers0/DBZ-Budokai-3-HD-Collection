# mod center hd — Herramientas para el pipeline PS2→HD (Budokai 3 HD Collection)

> Carpeta creada el 14/08/2026. Objetivo: refactorizar y mejorar las herramientas
> de la comunidad (que son primitivas y específicas de PS2) para el objetivo
> HD (Xbox 360 / ReXGlue).
>
> **ACTUALIZADO 17/08/2026 — LEA PRIMERO `GUIA_SWAPS_Y_PORTS.md`.** El
> proyecto hermano B1 validó que los model swaps funcionan por **instalación
> del bin completo** (el runtime NO valida conteos fijos): par geom+tex del
> MISMO personaje, sellos del bin correctos. La retopología 3D y la
> reconstrucción binaria (v20/v22) quedaron **superadas**.

---

## 1. POR QUÉ EXISTE ESTA CARPETA

Las herramientas de `mod center hd\` son
**scripts Python 2/3 primitivos** (tkinter, 15-20KB, sin manejo de errores,
hardcodeadas a un formato concreto) o **binarios compilados antiguos** (2006-2018).
La comunidad las usa para el formato **PS2** (AMO/AMG/AMT), que está resuelto.

**El objetivo de este proyecto es el formato HD (360/AWO/AWG/AZT)**, que las
herramientas de la comunidad NO cubren. Esta carpeta aloja las herramientas
refactorizadas y adaptadas.

---

## 2. VEREDICTO DE VIABILIDAD DEL PORT (investigación 14/08/2026)

**El port entre juegos ES VIABLE** (contrario a lo que parecía):

1. **La comunidad porta SDBH WM / B1 / B2 / IW → B3 PS2 con éxito**. El formato
   EMD de SDBH WM usa el MISMO esqueleto que Budokai (labels `waist`, `llegrot`,
   `stmc`, `chest`, `neck`, `head`...). Verificado: `bc18gb00.esk` de Android 18
   → mapeo directo a bones KLL (28 bones).

2. **El pipeline correcto** (lo que la comunidad hace, no "inyección en slots"):
   ```
   Modelo fuente (EMD/OBJ/FBX)
     → convertir a mesh parts PS2 (AMG) CON IB reconstruido  [EMD to AMG / OBJ to AMG]
     → empaquetar AMB PS2 (#AMO0 + #AMT)                    [AMB Packer]
     → (NUEVO) re-layout a HD (#AWO + #AZT)                 [NUESTRO pipeline]
   ```

3. **⚠️ ACTUALIZACIÓN 17/08 (desde el proyecto B1)**: para swaps ENTRE JUEGOS
   HD (B1↔B3 y dentro de cada uno) ya NO hace falta retopología ni
   reconstrucción. El runtime dibuja el bin `#AWO` completo tal cual (mesh
   group, IB, bones, UVs) sin validar conteos del slot. **Port = convertir
   sellos + material + AZT, e instalar el bin completo.** Ver
   `GUIA_SWAPS_Y_PORTS.md`. La retopología 3D (README original §2.3-§4 y
   `RETOPOLOGIA_3D.md`) queda como vía secundaria.

4. **El salto PS2→360 es un RE-LAYOUT** (endianness + magics renombrados +
   re-layout de mesh groups), no un formato distinto. Documentado en AWO_FORMAT.md.

---

## 3. HERRAMIENTAS CLAVE (fuente) y su estado

| Herramienta | Estado | Comentario |
|---|---|---|
| `EMD to AMG v0.90` (mod center) | 🔬 primitiva | Script Python 15KB con UI tkinter. Convierte EMD→mesh part PS2. Reconstruye mesh part desde templates binarias. |
| `OBJ to AMG v0.92` (mod center) | 🔬 primitiva | Script Python 10KB. OBJ→mesh part PS2. |
| `EmdFbx-and-FbxEmd-LibXenoverse` (modding resources) | ✅ funciona | emdfbx.exe convierte EMD→FBX binario (3MB). fbxfmd invierte. Blender 2.78 plugin incluido. |
| `Model-Rig_Extractor_v0.9.py` (discord tools) | 🔬 clave | Documenta el mapeo skin→malla (ch_loc/sb_loc → offsets de vértices). |
| `B3-IW AMO Converter + Shadows` (mod center) | ✅ comunidad | B3/IW→B1 PS2 (re-layout de headers mesh part). |
| `AFS Toolset`, `AMB Packer-Unpacker` | ✅ | Empaquetado. |
| `xbcompress/xbdecompress` (XDK) | ✅ | Compresión LZX /N:2048. |

## 4. PIPELINE HD PROPUESTO (objetivo)

```
1. Modelo fuente: SDBH WM EMD / Xenoverse / B1/B2/IW PS2 / OBJ
2. Convertir a mesh parts PS2 con IB real     (EMD to AMG / OBJ to AMG)
3. (opcional) Editar en Blender via FBX       (EmdFbx + Blender 2.78 plugin)
4. Re-layout a #AWO HD                         (NUESTRO build_awo / build_janemba2)
   - header + zonas de hueso + mesh group + arms
   - sec34 (stride 44: [nan,u,v,z,x,y,peso,bone,nz,-ny,nx])
   - vb2 (estático, 0xFFFFFFFF)
   - IB reconstruido
5. Empaquetar AMB (#AWO + #AZT)               (build_janemba2)
6. Comprimir LZX /N:2048                       (xbcompress)
7. Instalar como mod (override por entrada)    (build_afs)
```

## 5. ARCHIVOS EN ESTA CARPETA

- `GUIA_SWAPS_Y_PORTS.md` — **LEER PRIMERO (17/08)**: principio de los swaps,
  estado real (B1→B1 ✅, B3→B1 ✅, B3→B3 pendiente), pipeline paso a paso,
  diagnóstico de herramientas y hoja de ruta del B3.
- `awg_to_obj.py` — **ARREGLADO 17/08**: exporta un `#AWO`/`#AMB` HD a OBJ en
  world space (offsets `+0x28..+0x34`, layout v10+ del vértice). Verificado
  con el Gero B3 (2501 verts, 1814 caras).
- `obj_to_awg.py` — inverso de awg_to_obj (retopología; requiere el mismo fix
  de offsets/layout; vía secundaria).
- `emd_to_awo_hd.py` — v1: parseo de ESK (esqueleto SDBH WM) + mapeo de bones
  a KLL (28 mapeos verificados). Fase 2 pendiente: parseo completo del EMD.
- `build_awo_v20/v22.py`, `build_awo_from_json.py`, `inject_a18*.py`,
  `empaquetar_v20.py` — **OBSOLETOS** (vía de conteos fijos/retopología,
  superada por el swap nativo). Ver GUIA_SWAPS_Y_PORTS.md §6.
- `EMD_NOTAS.txt` — formato EMD Xenoverse (big-endian, header, modelo en 0x100).
