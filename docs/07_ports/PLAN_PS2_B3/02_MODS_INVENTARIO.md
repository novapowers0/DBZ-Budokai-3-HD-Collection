# 02 — INVENTARIO: mods, herramientas y recursos para port PS2→B3 HD / cara / huesos extra

> Sesión de inventario 2026-09-10. Objetivo: localizar TODO lo existente relacionado
> con conversión/port de modelos, swap de cabeza/cara, huesos extra (48-63) y
> formatos de vértice. **No se modificó código.**
>
> El problema pendiente que motiva este inventario: los **AWGs de 1 hueso**
> (huesos 48-63 en Cell F2; cara/manos en Kuillin) **no tienen equivalente PS2**
> y la inyección de vértices (Vía A) solo toca el `sec34` del primer AWG0.

---

## 0. RESUMEN EJECUTIVO

1. **No existe NINGUNA herramienta (comunidad o propia) que convierta PS2→HD 360.**
   Verificado por código: todas las tools de `mod center/` usan `struct.pack('<L')`
   (little-endian → PS2). La única BE del acervo es `A3T Analyzer` (solo texturas).
   El salto PS2→360 es un **re-layout propio** (endianness + magics renombrados +
   tabla de offsets AMG→AWG). Ver `docs/07_ports/ESTUDIO_ECOSISTEMA_MODS.md` §3.
2. **El swap de cabeza HD→HD está resuelto a medias y pausado**: existe
   `awo_tools/swap_cabeza.py` (reconstrucción de bloque → **crash**) y
   `awo_tools/swap_cabeza_inplace.py` (inyección in-place → **carga y entra en
   combate**, con z-fighting). Mod resultante: `mods/goku_armadura` v3.0.
3. **El bloqueo de la cara NO es el formato**: es que el **AWG0 dibuja cara/cabello/
   dientes** vía descriptores propios, además de los AWGs de cara separados. La
   inyección en los AWGs de cara no sustituye la cara del AWG0 → z-fighting.
4. **Huesos extra 48-63 (Cell F2)**: confirmado. El bin HD tiene **17 AWGs**:
   AWG0 (48 huesos, cuerpo) + **16 AWGs de 1 hueso = huesos 48-63**. El PS2 solo
   tiene 48 huesos (0-47) → esos 16 AWGs quedan HD. Detalle en `AGENTS.md` §3.4.2
   y §10; instrumentos en `awo_tools/`.
5. **Formatos de vértice documentados**: A y C en el AWG0 (autodetectados), layout
   propio de AWG de cara (buffer fijo en `h+0x1F0`), y layout propio de `vb2`.
   El pipeline tiene que autodetectar A/C (`awg0_export.py:detect_format`).

---

## 1. HERRAMIENTAS QUE CONVIERTEN/PORTAN MODELOS PS2→HD (o reconstruyen el formato)

### 1.1 `mod center hd/ports/` — pipeline nombrado PS2→B3 HD (nuestro)

Carpeta creada 2026-08-26 (`docs/07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md`). Cada script
es autocontenido y devuelve JSON intermedio.

| Script | Función | Estado |
|---|---|---|
| `port_ps2_b3_extract.py` | Parsea modelo PS2 (`#AMB`/`#AMO0`): malla por FaceType, rig/skin (bone+peso por vértice), esqueleto (ejes 80B) y labels. Auto-detecta el AMG0 y rechaza HD. | ✅ HECHO |
| `port_ps2_b3_geometry.py` | Coords locales + bone → buffers HD (`sec34` 44B formato A + `vb2` 44B + IB u16 BE). | ✅ HECHO |
| `port_ps2_b3_draw.py` | [PIEZA CLAVE] Estructura de dibujo HD (mesh-ref blocks, arms, descriptores, ejes). Detecta descriptores reales (stride `0x2C00`). | 🔴 **NO EXISTE el regenerador para pool reordenado** (bloqueo real) |
| `port_ps2_b3_inject.py` | **Vía A (validada)**: plantilla HD intacta + reescribe `+12/+16/+20` del `sec34` con geometría PS2 en bone-local. Flags `--npm`, `--bone-aware`, `--bone-thr`, `--soft`. | ✅ FUNCIONA |
| `port_ps2_b3_pack.py` | Empaqueta `#AMB` autocontenido + LZX + override. Neutraliza descriptores sobrantes (A fuera de rango, B=0). | ✅ |
| `port_ps2_b3_verify.py` | Exporta OBJ + chequeo bounds/NaN. | ✅ |
| `port_ps2_b3_decimate.py` | Decima geometría (step 44B). | aux |
| `test_injection.py` | Test offline de la inyección. | aux |

**Snippet clave de `port_ps2_b3_inject.py`** (conversión a bone-local y normal HD):
```python
# ... world_mats(t, awo): ejes del template, parent = (AWG0+poff-axes_base)//80
sec_real = AWG0 + sec_rel + 2
n_slots = (vb2_rel - sec_rel - 2)//44
bone = be32(templ, o + 28)
z, x, y = be_f(templ, o+12), be_f(templ, o+16), be_f(templ, o+20)
slot_world.append(world[bone].dot(np.array([x, y, z, 1.0]))[:3])
...
lc = inv[bone].dot(np.concatenate([pos, [1.0]]))
f32i(templ, o+12, float(lc[2])); f32i(templ, o+16, float(lc[0])); f32i(templ, o+20, float(lc[1]))
# formato normal HD: [nz, -ny, nx]
```

- **Límite estructural documentado (2026-09-10)**: `port_ps2_b3_inject` solo toca el
  `sec34` del **primer AWG0**. Los **16 AWGs de 1 hueso** (48-63) no se PS2-izan
  (cara en HD). Para PS2-izar la cara habría que remapear por label los huesos PS2
  33-40 a los AWGs 48-63 con **formatos de vértice variables por AWG**.

### 1.2 `awo_tools/` — RE del formato y experimentos (referencia)

| Script | Función | Estado |
|---|---|---|
| `awg0_export.py` | Exporta AWG0 → OBJ **autodetectando formato A/C** y ubicación del buffer. | ✅ Recomendada |
| `awg_to_obj_b3.py` | Exporta bins B3 completos → OBJ. | ✅ |
| `awg_cara_export.py` | Exporta un AWG de cara (nb=1) → OBJ. | ✅ (usada para validar swap de cabeza) |
| `awg_parts.py` / `awg_parts2.py` | Parseo por-AWG. | aux |
| `parse_ps2_mesh.py` | Parser de malla PS2 (verts + IB real por FaceType). | ✅ |
| `ps2_rig_skin.py` | Rig PS2: bone+peso por vértice (chunks/sub-chunks). | ✅ |
| `pose_matrix.py` | Matrices world de huesos PS2. | ✅ |
| `ps2_to_hd_geometry.py` | PS2 → buffers HD (predecesor de `port_ps2_b3_geometry`). | ✅ |
| `build_from_template.py` | Construye bin HD autocontenido usando **Cell F2 (147, 48 huesos)** como plantilla. Mid-insert interno. | ✅ (generó `janemba_from_cell`/`janemba_cell48`) |
| `build_awo_autocontenido.py`, `build_awo_desde_cero.py`, `build_awo*.py` | Construcción de AWO. | 🔸 experimentales |
| `port_ps2_to_b3.py`, `port_b1_to_b3.py`, `mezclar_ps2_hd*.py` (v1-v6), `inyeccion_awg.py`, `inject_a18*.py`, `relayout_*.py`, `retarget_hd.py` | Experimentos previos. | ❌ **NO usar como base** |
| `swap_cabeza.py`, `swap_cabeza_inplace.py` | Swap de cabeza (ver §2). | ◑ parcial (cara) |
| `swap_cuerpo_hd*.py` | Inyección de cuerpo. | ❌ fallido |
| `phase_b_*.py` | Instrumentos Fase B (census, consumer scan, arms dump, tests T2-T7). | ✅ investigación |
| `cell_align_check.py`, `cell_dist_stats.py`, `scan_bones.py`, `trace_bone.py`, `analyze_awg*.py`, `analyze_mesh*.py` | Análisis / rigs. | ✅ |

> ⚠️ `analyze_bin_hd.py` está **desactualizado** (layout PS3) — no usar.

### 1.3 `mod center/` — comunidad PS2 (36 programas) — **ninguno hace PS2→HD**

Conversión de modelos (todos LE):
- `OBJ to AMG v0.92` — OBJ→mesh parts PS2 desde templates (`model_part_header.bin`,
  `triangle.bin`). Vértices 48B expandidos por triángulo, `FaceType=1`.
- `EMD to AMG v0.90` — EMD Xenoverse/SDBH → AMG PS2.
- `B3-IW AMO Converter + Shadows` — B3/IW→B1 (re-mapea cabeceras mesh part).
- `Bin to OBJ (English Version) V3` / `AMG to OBJ V2` — AMG PS2→OBJ.
- `Model Merger Tool (32-Bit)` — fusiona 2 AMO (AMO_LGBT).

Rig/huesos (relevante para huesos extra y cara):
- `Model Rig Toolset V0.6` / `Model-Rig Extractor Tool V1.0` — extraen rig por hueso
  (chunks 32B/sub-chunks 16B con offset de vértice en +12) → **mapeo rig→malla**.
- `Bone Addition Tool v1.02` — **añade hueso** al AMO (eje 80B + child/sibling/parent
  + labels). Útil como referencia para generar huesos extra.
- `Model Part Editor` — convierte mesh parts B3⇄B1 (headers/shader/rgb_lines).
- `Axis Line Tool`, `BoneAxis Display` — ejes/huesos.

Empaquetado / compresión:
- `AFS Toolset v0.90`, `AMB Tool`, `AMBStudio`, `Budokai AMB Packer-Unpacker`,
  `AMB_AMT.Manipulator 1.5`, `B3_IW Model Converter` (solo empaqueta AMB).
- `Xbox 360 Compression - Decompression tool from the XBOX Development Kit` →
  **`xbcompress.exe /N:2048`** (crítico).

Otros: `A3T Analyzer` (texturas BE), `Budokai3_SLUS_Editor_v08`, `SLXS Editor v0.50`,
`LST Event Editor 0.7`, `PSound`, `CRI ADX Tools`, `Set Unlimited Fusion`,
`Transformation Input Stuff`, `Shin Budokai 2 Tools 0.4`, `Zero Devs' Tool`, etc.

> **`mod center\Binary Templates` NO existe** en este repo. Las plantillas binarias
> están en: `mod center\<tool>\Files\Templates\`, `modding resources discord\research\B3_AMB_PS3.bt`
> (010 Editor, big-endian) y `modding resources update 2\lean bone tutorial\Budokai Toolset\Files\AMG\*.bin`.

### 1.4 Otras carpetas
- `SDBH_body/` — solo **texturas DDS** (`DATA000.dds`… `FACE_W`, `HAIR`, etc.) y
  `embFiles.xml`. No hay herramienta de modelo.
- `portforge/` — solo config de un launcher de forge (`.forge.json`, `.mediaitem.json`).
  No relevante al port.
- `tools/` — utilidades del proyecto (build/release/codegen): `fix_eu_bctr.py`,
  `prefix_eu_codegen.py`, `make_release.ps1`, `verify_release.ps1`, `sync_github.ps1`,
  `lab_f0.ps1`, `cleanup.ps1`, `find_jtables.cpp`, `extract_jt.cpp`. **No son de modelos.**

---

## 2. SWAP DE CABEZA / CARA — cómo funcionan

### 2.1 `awo_tools/swap_cabeza.py` — reconstrucción de bloque ❌ (crash)

Toma el bin de **Vegeta armadura (424, base)** y reemplaza su bloque de AWGs de cara
(`AWG19-25, XVGT_Lxx_S00_FACE`) por el de **Goku (16-22, XGOK_Lxx_S00_FACE)** por
correspondencia de **label numérico** (`L09→L00_S09` y `L42→L44` con alias).

**Cómo funciona (código `awo_tools/swap_cabeza.py`):**
1. `get_awg_labels()`: recorre la tabla de AWGs (`AWO+0x1C`, count en `AWO+0x18`),
   lee el mesh group (`h + u32(h+0x20)`) y hace regex de
   `X?[A-Z0-9]{3}_L([0-9A-Z]+)_S[0-9A-Z]+_FACE` para extraer el número de label.
2. `read_awg_cara()`: header `0x1F0`; descriptor en `h+0x180` (`+0x1C` n_verts,
   `+0x24` n_tris); **buffer de vértices SIEMPRE en `h+0x1F0`** (stride 44);
   IB en `h+ib_rel` (`+0x30`), tamaño en `+0x34`.
3. `build_awg_cara()`: re-empaqueta header de **Vegeta** + geometría de **Goku**:
   ```python
   ib_rel = 0x1F0 + buf_size - 32          # el IB SOLAPA 32 B el buffer
   end_rel = ib_rel + n_idx*2
   data = buf_bytes[:buf_size - 32] + ib_bytes
   ```
4. Sustituye todo el bloque de cara de una vez, reescribe offsets de AWGs
   posteriores + `AZT` (offset 0x30) + tamaño AWO (+0x24).

**Resultado**: estructuralmente válido (0 NaN, 148-156 tris) pero **el guest CRASHEA**
(`0xC0000005`, hilo GPU, sin `AFS327 READ`). Causa probable: el **AWG0 referencia los
AWGs de cara por offsets que se rompen al mover el bloque**. → `mods/goku_armadura` v1.

### 2.2 `awo_tools/swap_cabeza_inplace.py` — inyección in-place ◑ (funciona con defectos)

**Parte del bin de Vegeta (que ya funciona) y copia la geometría de Goku en los buffers
EXISTENTES** de los AWGs de cara de Vegeta, por label. **Sin mid-insert, sin mover
offsets, sin tocar el AWG0.** Si el buffer de Goku es menor → rellena `0xFF`; si es
mayor → **trunca a la capacidad de Vegeta**. Actualiza `n_verts`/`n_tris` del descriptor.

**Resultado en juego (v2.0)**: carga y entra en combate (Vegeta armadura con cabeza de
Goku). **PERO z-fighting en frente/ojos** porque la cara/cabello/dientes de Vegeta
siguen dibujándose **desde el AWG0** (descriptores `XVGT_L00_S00_FACE`, `XVGT_HAIR`,
`XVGT_M_DTEETH/UTEETH`).

**Fix v3.0** (`mods/goku_armadura`): **neutralizar** esos descriptores del AWG0 poniendo
`A_size/B_size = 0` (2×`XVGT_HAIR`, 2×`XVGT_M_DTEETH`, 1×`XVGT_M_UTEETH`; se conservan
20 descriptores). `RESULTADO`: desaparecen partes del pelo, pero **sigue sin ser la cara
completa de Goku**; el z-fighting se redujo pero no se resuelve.

**Conclusión (HISTORICO §13.10, 2026-08-19)**: para una cara completa hay que sustituir
**también la geometría de cara/cabello del `sec34` del AWG0** (no solo neutralizarla,
que deja huecos), lo que requiere **re-mapear vértices entre formatos A/C**. Pausado.

### 2.3 Mod resultante
- `out/build/win-amd64-release/mods/goku_armadura/` — manifest:
  ```
  type=swap_cabeza_inplace
  description=Goku con armadura saiyan v3: cuerpo Vegeta 424 + cabeza Goku
              + cara/cabello de Vegeta neutralizada
  source=Vegeta 424 + Goku 264   target=327
  ```
  (Actualmente `.disabled`.)

### 2.4 Layout del AWG de cara (nb=1) — resuelto
`HISTORICO_AGENTS.md` §13.9 y `awo_tools/awg_cara_export.py`:
```
+0  u32 0xFFFFFFFF (marcador)
+4  u | +8  v            (UV)
+12 x | +16 y | +20 z    (posición en espacio LOCAL del hueso cabeza)
+24 weight (=1.0) | +28 pad 0.0
+32 nx | +36 ny | +40 nz (normal unitaria)
```
Header del AWG de cara (rel `h`): `+0x10 n_bones=1`, **`+0x2C` = TAMAÑO del buffer
(n*44)**, `+0x30` ib_rel, **`+0x34` = TAMAÑO del IB (bytes, ¡no offset!)**, `+0x38` end.
Descriptor en `h+0x180`. Buffer SIEMPRE en `h+0x1F0`. IB = **lista de triángulos**
(cada 3 = 1 tri), no strip. Clave: las caras de Goku y Vegeta comparten el espacio
local del hueso cabeza (bounds casi idénticos) → copia 1:1 sin transformación.

---

## 3. FORMATOS DE VÉRTICE DOCUMENTADOS Y VARIANTES

### 3.1 `sec34` del AWG0 — dos formatos autodetectados (stride 44)

De `awo_tools/awg0_export.py:detect_format` y `AGENTS.md` §3.2:

**Formato A** (Krillin 327, Cell F2 147), buffer en `sec_rel+2`:
```
+0   FFFFFFFF | +4 u | +8 v | +12 z_local | +16 x_local | +20 y_local
+24  peso     | +28 BONE (u32) | +32 nz | +36 -ny | +40 nx
```

**Formato C** (Goku 264, Vegeta 424, Babidi 96, Goten, Krillin armadura 329),
buffer en `sec_rel` (o `fin_mg`):
```
+0 x | +4 y | +8 z | +12 FFFFFFFF | +16 u | +20 v | +24 nx | +28 ny | +32 nz
+36 peso | +40 BONE (u32)
```

`detect_format` prueba 3 ubicaciones del buffer (`sec_rel+2`, `sec_rel`, `mg+mg_size`)
× 7 offsets de marker (`0,2,12,16,24,38,40`) y elige la de ≥50% markers `FFFFFFFF`.
⚠️ La ubicación del `sec34` **varía por bin** (Goku en `sec_rel`, Vegeta en `fin_mg`,
Krillin en `sec_rel+2`).

### 3.2 `vb2` (stride 44) — layout propio (estático, bone=0xFFFFFFFF)
```
[x, y, z, 0, 0, 0, peso=0, 0xFFFFFFFF@+28, nx, ny, nz]
```
Cubre **cabeza/caras (y piernas en algunos bins)** y **NO lo toca la inyección PS2**
(→ cara/piernas no se arreglan por Vía A). En Krillin: 226 slots, 15.4% del IB.
`vb2` de Cell F2 = layout B (aún no emitido correctamente por el port).

### 3.3 Formatos PS2 (fuente) — `port_ps2_b3_extract.py:VERT_STRIDE`
```python
VERT_STRIDE = {0xBD:48, 0xFD:48, 0x3D:48, 0xB5:48, 0xB6:48, 0xF5:48,
               0x199:32, 0xB4:32, 0xA4:32, 0x99:32, 0x92:32, 0x19:32,
               0x90:16}
```
Detalle en `modding resources update 2/INFORME_modding_resources_update_2.md` §3:
`B5`=48B estándar de personaje; `B4/BD`=32B faciales; `0x90`=16B sombras.
**El IB PS2 es implícito** (FaceType 1=strip, 0=triplete), no lista de índices.

### 3.4 Headers de AWG (HD) — offsets canónicos
De `docs/07_ports/SESION_FASE_B_ARMS_2026-09-10.md` §3.1 (⚠️ `docs/03_formatos/BIN_LAYOUT.md`
y `AMO_AWO.md` están **MAL**):
```
+0x14 axes | +0x2C vb2 | +0x30 IB | +0x34 sec34 (align +2) | +0x38 end
```
Descriptor de submesh (0x60): `A=+0x50/+0x54` (vértices), `B=+0x58/+0x5C` (índices IB),
todos `<<8` y flag `0x01` en la 2ª tabla. **Hay DOS tablas de descriptores**: mesh-group
@`AWG0+~0x2D49` (0x60) y **`AWG0+0x1F80`** (u32 en plano: `42,60,74,126`…) probablemente
la que consume el draw en runtime.

---

## 4. HUESOS EXTRA 48-63 / AWGs DE 1 HUESO / CELL & SEMI-PERFECT

### 4.1 Cell F2 (bin 147) = plantilla de 48 huesos
- `awo_tools/build_from_template.py`: usa **Cell Forma 2 (bin 147)** como plantilla HD
  válida; genera `janemba_from_cell`/`janemba_cell48` con geometría de Janemba.
- `AGENTS.md` §10 y §3.4.2: el bin HD de Cell F2 tiene **17 AWGs** = AWG0 (48 huesos,
  2661 verts, cuerpo) + **16 AWGs de 1 hueso = huesos 48-63** (cara/detalles).
  El PS2 solo tiene **48 huesos (0-47)** → esos 16 AWGs **NO tienen equivalente PS2**
  y quedan HD (por eso la cara sale en HD).
- `HISTORICO_AGENTS.md` §1796 (tabla): `Cell F2 | 147 | 17 | 48 | FFFF en +0 | formato A`.
- Mod de test `mods/cell_best` (manifest): *"Cell F2 PS2->HD: extractor corregido +
  NPM 0.8, manos en HD real (plantilla) + guardia anti-estirado (68 verts)"*.
- `mods/cell_npm4_test`: mejor inyección histórica (NPM + normales + umbral 0.8).
- Mods de test relacionados (todos `.disabled`): `cell_npm2/3/6/7/8`, `cell_bone0_test`,
  `cell_clamp33_test`, `cell_boneclamp_test`, `cell_conv2_test`, `cell_desc_test`,
  `cell_ps2_port`, `cell_port_Afix_test`, `cell_reverse_test`, `cell_bodyfmt_test`,
  `cell_delta0_test`, `cell_nopad_test`, `cell_inject*_test`, etc.

### 4.2 Huesos extra en otros contextos
- **Tien con capa (IW→PS2)**: `docs/07_ports/SESION_TIEN_RIG_2026-09-08.md` — 42 labels
  comunes en el **mismo orden** que Tenshinhan HD (entry 400) + **10 labels extra de
  capa** (`XTSH_MANT*`, `XTSH_RMANT`, `XTSH_LMANT`). **Pasa rig base 1:1**; la capa se
  aísla como segundo experimento. → candidato activo para Vía B.
- **Pikkon IW** — descartado: esqueleto PKH (58 bones con falda) ≠ KLL → no 1:1.
- **Modelo IW → AMB** — `modding resources/All Character Models from IW into AMB format/`
  (241 `.amb`, incl. Janemba 48 huesos/17 AMGs igual que IW bin 541).

---

## 5. DOCS / TUTORIALES DE LA COMUNIDAD (portar modelos, huesos, cara, skinning)

### 5.1 Informes propios (los más útiles, ya extraídos)
- `modding resources update 2/INFORME_modding_resources_update_2.md` — **informe
  técnico completo** de formatos PS2 y herramientas (contiene §2 formato de vértice,
  §3 tablas de `meshType`, §4 rig/axis lines, §7 tools, §8 edición hex).
- `awo_tools/HALLAZGO_COMUNIDAD.md` — ecosistema IW→B3 PS2 ya resuelto; el gap es PS2→HD.
- `awo_tools/RE_AWO_HD_CONVERSOR.md` — RE del AWO HD, mapa de los 18 AWGs de Krillin
  (AWG11-17 = `XKLL_*_FACE`), layout `sec34`, descriptores y mesh-ref blocks.
- `awo_tools/SUBMESH_DATA_B3.md` — layout de descriptores (0x60), contigüidad del rango A,
  mapa estructural del AWG0. Nota: el `vb2` cubre 15.4% del IB (cabeza/caras).
- `awo_tools/CONSOLIDADO.md`, `awo_tools/RE_PROGRESO.md` — RE histórica.

### 5.2 Tutoriales de la comunidad (rutas)
En `modding resources update 2/`:
- `SB2 Breakdowns/SB2_-Transplanting_head_from_one_model_to_another_By_Lean_and_Cueliton.docx`
  — **transplante de cabeza** (SB2). El más cercano al problema de cara.
- `lean bone tutorial/` — `Tutorial12.rtf` (**re-rigging por hueso**, escalar hueso a 0 y
  exportar OBJ por hueso), `budokai_updated.ms` (**importador AMO/AMG**, documenta el
  **IB implícito**: FaceType 1=strip, 0=triplete), `Rig Data Tool`, `Budokai Toolset`
  (Nexus: `amg_c`, `amo_s`, `amo_lgbt`, `axis_e`, `b1_i_e`, `m_p_e`…), `OBJ to AMG v0.92`.
- `- Budokai OBJ Editing Tutorial 2/` y `- Tutorial de Edición de OBJ para Budokai/`
  — editar mesh parts en Blender y reconstruir con OBJ→AMG (UVs PS2 "upside down",
  usar Mirror Y).
- `Tutorial #1 Añadir AMG manualmente (Español latino)/Tutorial.docx` — añadir AMG por
  hex (editar longitud, nº de partes, axis lines, hueso destino).
- `Acidicionando partes do personagem.docx` — añadir partes, detección `01..46`,
  re-mapeo de axis lines.

En `modding resources discord/tutorials/`:
- `SB2_-Transplanting_head...`, `CREATE_AMG_WITH_...`, `AMG_and_adding_m...`,
  `Adding_Model_Par...`, `How_to_combine_m...`, `Inject_and_debug...`,
  `SLXS Edit Tutorial` (1-1 model data blocks, 1-2 añadir modelos a trajes),
  `Tutorial_anadir...`, `Tutorial_remove...`, `LGBT_Method.zip`, etc.

En `modding resources update 2/`:
- Serie `SLXS Edit Tutorial - Lesson 1-1..4-1` (B3 GH e IW) — bloques de datos del
  personaje, model bin list, face AMM, añadir transformaciones, select.
- `DBZ B3 (X360) - Lesson 1` (LZX `/N:2048`), `DBZ B3HD - Lesson 2` (texturas AZT/DDS).

> ⚠️ **Ningún tutorial comunitario cubre PS2→HD 360**: solo PS2→PS2 (B1⇄B3, IW→B3,
> EMD→AMG, OBJ→AMG) o edición del HD con 010 Editor + template.

---

## 6. MODS INSTALADOS RELEVANTES (`out/build/win-amd64-release/mods/`)

Un mod está activo si NO tiene `.disabled`. `AfsFindModOverride` sirve el **primer**
mod activo por orden alfabético → **un solo mod por test**.

| Mod | Tipo | Descripción | Estado |
|---|---|---|---|
| `cell_best` | port_b3 | Cell F2 PS2→HD, NPM 0.8 + manos HD + guardia anti-estirado (68 verts) | **ACTIVO** |
| `goku_armadura` | swap_cabeza_inplace | Cuerpo Vegeta 424 + cabeza Goku 264; cara Vegeta neutralizada | `.disabled` |
| `sw_goten_nativo` | swap nativo | Goten nativo (validado) | `.disabled` |
| `sw_vegeta424` | swap nativo | Vegeta armadura formato C (validado) | `.disabled` |
| `swap_96_on_327` | swap nativo | Babidi sobre Krillin | `.disabled` |
| `cell_npm4_test` | port_b3 | Mejor inyección NPM histórica | `.disabled` |
| `cell_npm_fix` / `cell_npm_fix_nohand` | port_b3 | Inyección con fix del padre de eje; sin mano | `.disabled` |
| `janemba_cell48` / `janemba_from_cell` | port | Janemba IW→B3 vía plantilla Cell F2 (48 bones) | `.disabled` |
| `tien_ps2_on_krillin` | port | Tien PS2 sobre Krillin | `.disabled` |
| `_t2_swap` / `_t3_reverse` / `_t4_inpart` / `_t5_noremap` / `_t6_adesc` / `_t7_ibrev` | tests Fase B | Permutaciones del pool/IB (ver §8) | todos `.disabled` |
| `tex_*`, `krillin_*`, `janemba_v2`, `nappa_portrait`, etc. | varios | Texturas / experimentos | `.disabled` |

No existe ningún mod llamado `swap_cabeza*`; el resultado del swap de cabeza es
`goku_armadura`.

---

## 7. EL BLOQUEO REAL (referencia para el plan)

De `docs/07_ports/SESION_FASE_B_ARMS_2026-09-10.md` y `AGENTS.md` §3.4:

- La **Vía A (inyección, conserva el orden del pool)** es la única entrega validada
  (`cell_npm4`, umbral 0.8).
- La **Vía B (port completo, topología PS2)** está bloqueada: aunque el IB se remapee
  de forma consistente (T3/T4) el render **deforma** → hay un **consumo POSICIONAL del
  pool** que NO es el descriptor A (T6 normal) y NO es solo el IB (T7 deforma).
  Probablemente es la **estructura de skinning (arms)/zonas** atada al orden del pool.
- **Dos tablas de descriptores**: mesh-group (0x60) y `AWG0+0x1F80` (u32 plano).
- Instrumentos Fase B en `awo_tools/phase_b_*.py` (census, consumer scan, arms dump,
  make_t2..t7, ab_compare, deep_scan).

---

## 8. LO MÁS REUTILIZABLE PARA EL PROBLEMA DE LA CARA / HUESOS 48-63

| Pieza | Ruta | Por qué es reutilizable |
|---|---|---|
| Layout AWG de cara (nb=1) + `awg_cara_export.py` | `awo_tools/awg_cara_export.py` | Ya exporta/valida los AWGs de cara; layout resuelto (buffer fijo `0x1F0`, IB lista de tris). |
| Swap in-place de geometría de cara | `awo_tools/swap_cabeza_inplace.py` | Único método que **carga y entra en combate** sin mover offsets. |
| Neutralización de descriptores del AWG0 | `HISTORICO_AGENTS.md` §13.10 (v3.0) | Patrón para desactivar cara/cabello/dientes del AWG0 (A/B_size=0). |
| Autodetección de formato A/C + ubicación del buffer | `awo_tools/awg0_export.py:detect_format` | Necesario para re-mapear vértices entre formatos A/C (cara AWG0). |
| Inyección NPM bone-local (Vía A) | `mod center hd/ports/port_ps2_b3_inject.py` | Reescribe `sec34` con geometría PS2; flags `--npm --bone-aware --bone-thr`. |
| Generación de bin autocontenido desde plantilla de 48 huesos | `awo_tools/build_from_template.py` | Plantilla Cell F2 (147) con 16 AWGs de 1 hueso 48-63. |
| Parsing PS2 completo (malla+rig+esqueleto) | `mod center hd/ports/port_ps2_b3_extract.py` | Detecta base `#AMB`/`#AMO0`, `VERT_STRIDE`, padre de eje. |
| Herramienta de huesos de la comunidad | `mod center/Bone Addition Tool v1.02/` | Referencia para **añadir huesos** (eje 80B + child/sibling/parent + labels). |
| Extractor de rig por hueso | `mod center/Model Rig Toolset V0.6/` | Documenta el mapeo rig→malla (chunks/sub-chunks, offset vértice en +12). |
| Mapa AWO HD (18 AWGs, cara=11-17) | `awo_tools/RE_AWO_HD_CONVERSOR.md` | Identifica los AWGs de cara y su rol. |
| Layout de descriptores + dos tablas | `awo_tools/SUBMESH_DATA_B3.md` + `SESION_FASE_B_ARMS` §3.15 | Base para un futuro regenerador (Vía B). |

### Brechas detectadas (lo que NO existe)
1. **Remapeo de huesos PS2 33-40 → AWGs HD 48-63** con formatos de vértice
   variables por AWG. No hay herramienta.
2. **Sustitución de la geometría de cara/cabello del `sec34` del AWG0** entre
   formatos A/C (requerida para una cara completa sin z-fighting). No hay herramienta.
3. **Regenerador de estructura de dibujo HD** (mesh-ref + arms + 2 tablas de
   descriptores) coherente con un pool reordenado (Vía B). No existe.
4. **Conversor #AMT (PS2) → #AZT (HD)**. Solo `texture_b3.py` (AZT→PNG→AZT).
</content>
</invoke>
