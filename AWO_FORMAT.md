# FORMATO DE ARCHIVOS — DBZ BUDOKAI 3 HD COLLECTION (Xbox 360)

> Documento de referencia para el proyecto de modding del recomp (dbz3).
> Consolidación del conocimiento adquirido por ingeniería inversa y estudio
> de las herramientas de la comunidad (mod center / modding resources).
> **Objetivo**: permitir añadir personajes/mapas/movesets nuevos al port nativo.
> Actualizado: 2026-08-13

---

## 1. RESUMEN EJECUTIVO

El juego recompilado (yae3_xenon.xex) lee sus datos de archivos **AFS** que
contienen bins individuales (modelos, texturas, movesets, audio). Cada bin está
**comprimido con LZX de Xbox 360** (magic `0F F5 12 EE`) y al descomprimirse
revela un contenedor **#AMB** en **big-endian** (PowerPC).

**Diferencia crítica con PS2/PSP**: los juegos PS2 (Budokai 1/2/3, Infinite
World) usan el mismo contenedor #AMB pero en **little-endian** (MIPS), con
secciones internas `#AMO0` + `#AMG` ×N + `#AMT`. La versión Xbox 360 usa
magics renombrados en **big-endian**: `#AWO` (modelo), `#AWG` (mesh), `#AZT`
(textura).

**Conclusión del estudio (VERIFICADA 2026-08-13)**: el `#AWO` de 360 ES el
mismo modelo `#AMO0`/`#AMG` de PS2 re-empaquetado en big-endian, con los
mismos huesos y mesh-groups (NO hay re-rigging). Comparación directa
Krillin GH PS2 (bin 327) vs HD 360 (bin 327, misma numeración):
- **51 huesos en ambos**, **18 mesh-groups en ambos**
- **68 labels de hueso idénticos** (KLL_*, XKLL_*)
- Cambia el endianness, los magics y el layout (tabla de offsets vs secuencial)

Convertir un modelo PS2→360 requiere:
1. Leer el #AMO0/#AMG little-endian (estructura documentada abajo)
2. Reescribir cada campo u32/u16 como big-endian
3. Renombrar magic: `#AMO0`→`#AWO`, `#AMG`→`#AWG`, `#AMT`→`#AZT`
4. Convertir el layout interno (bloques secuenciales → tabla de offsets)
5. Recomprimir con `xbcompress /N:32`

---

## 2. SISTEMA DE ARCHIVOS

### 2.1 Contenedor AFS

```
offset 0:  "AFS" magic (3 bytes) + 1 byte padding
offset 4:  entry count (uint32 LE)
offset 8:  tabla de entradas: (address uint32, size uint32) — 8 bytes por entrada
```

- Entries alineadas a 0x800 (2048 bytes); datos en [address, address+size).
- En la HD 360 los datos de cada bin están **comprimidos LZX** (magic `0F F5 12 EE`).
- En PS2 los bins van **sin comprimir** y son #AMB little-endian directo.
- El header ocupa hasta 0x8000 (32768); la primera entrada típicamente empieza ahí.

**Archivos AFS del juego** (directorio activo):
- `data_cmn.afs` (~280MB, 3990 bins) — modelos, movesets, mapas
- `data_usa.afs` / `data_en.afs` — menús, textos, cápsulas
- `adx_usa.afs` / `adx_jpn.afs` — audio (ADX)
- `lang_*.afs` — idiomas

### 2.2 Lista de nombres (AFL)

```
offset 0:  "AFL\0" (4 bytes)
offset 4:  version (uint32, =1)
offset 8:  0xFFFFFFFF
offset 12: entry count (uint32)
offset 16: registros de 32 bytes fijos (nombre null-padded), count × 32
```

- **Importante**: los nombres NO son strings consecutivos sino **registros fijos
  de 32 bytes** (verificado en los 4 AFL analizados: `16 + 32×count = tamaño`).
- El **índice del AFL = número de bin del AFS** (mapeo directo nombre→bin).
- El AFL de la GH/Collector's (DATA_ENG.afl) usa la numeración de bins de la
  HD 360 para el archivo de región (data_usa).
- **Dos numeraciones distintas según el AFS**:
  - `data_cmn.afl` → bins de modelos de batalla/movesets/mapas (ej. Krillin 327-329)
  - `DATA_ENG.afl` (región data_usa) → SCM/select (SCMKLL=35), BT-B00 (Krillin 420-432)
- Nombres tipo `SCMXXX.amb` (selección), `BT-B00_XXXnnn.amt` (modelos de
  batalla), `CN-M0X_XXXnnn.amt` (cinemáticas), `EN-E00_XXXnnn.amt` (endings),
  `SK-SKL_XXX.amt` (skills).
- Códigos de personaje: 16G/17G/18G (andriodes), GKS (Goku kid), GOK (Goku),
  GHS/GHM/GHL (Gohan), VGT (Vegeta), TRX/TRS (Trunks), KLL (Krillin),
  GNY (Ginyu), PIC (Piccolo), CEL (Cell), FRZ (Frieza), BRL (Broly), etc.

### 2.3 Compresión LZX de Xbox 360

- Herramientas: `xbcompress.exe` / `xbdecompress.exe` (XDK 2.0.7645.0)
  en `mod center\Xbox 360 Compression - Decompression tool...\`.
- **El juego usa `/N:32`** (native blocks de 32KB) → magic `0F F5 12 EE 01 03 00 00`.
- `/Z:32` produce `0F F5 12 ED` (transparent segments) — NO es lo que usa el juego.
- Sintaxis: `xbcompress /N:32 <src> <dst>` y `xbdecompress <src> <dst>`.
- Round-trip verificado: descomprimir→comprimir reproduce el bin exacto.

### 2.4 Sistema de mods del recomp (runtime)

- Un mod reemplaza una entrada del AFS o un archivo completo:
  - **Por entrada**: `mods/<mod>/us/<afs_filename>/<entry_index>` (bins sueltos)
  - **Archivo completo**: `mods/<mod>/us/<archivo>` (ej. `data_cmn.afs` completo)
- El runtime (afsi.cpp + host_path_file.cpp) intercepta las lecturas y sirve
  los bytes del mod cuando existe un override.
- Mods habilitados: carpeta `mods/<mod>/` sin marcador `.disabled`.
- El launcher construye el overlay `active_region/` desde region + mods.
- **Límite del override por bin**: el guest lee cada bin con el tamaño que
  tiene en la tabla AFS original. Si el bin mod es más grande, se trunca
  (crash). Por eso, para bins más grandes hay que **reconstruir el AFS completo**
  con la tabla actualizada (ver script `rebuild_afs2.py`).
- El script de reconstrucción re-laya los datos, actualiza addr+size del header,
  preservando el resto byte a byte.

---

## 3. CONTENEDOR #AMB

```
offset 0x00: magic "#AMB" (23 41 4D 42)
offset 0x04: header/versión (0x20 = 32) — en HD es BE
offset 0x0C: nº de entradas (uint32)
offset 0x10: nº de modelos
offset 0x14: 32 (header size)
offset 0x18: 64
offset 0x24: offset del primer bloque de datos
offset 0x28: 1
...
Tabla de entradas: en offsets 0x20/0x30/0x40... (16 B por entrada: loc + size)
```

- AMB de modelo: entrada 1 = AMO/AWO, entrada 2 = AMT/AZT (textura).
- AMB de moveset: AMC/AML/BCM/SPX en 32/48/64/80.
- Datos alineados a 16 B.
- **PS2 = little-endian, HD 360 = big-endian** (los campos u32 se leen invertidos).
- Mapeo de magics: `#AMO0`→`#AWO`, `#AMG`→`#AWG`, `#AMT `→`#AZT `.

---

## 4. FORMATO DE MODELO — #AMO0 (PS2) / #AWO (360)

### 4.1 Header del archivo de modelo

```
offset 0x00: magic "#AMO0" (PS2) o "#AWO" (360)
offset 0x10: bone_am  — nº de huesos/ejes
offset 0x14: bone_loc — offset de la tabla de relaciones de huesos (32 B/entrada)
offset 0x18: amg_am   — nº total de bloques AMG/mesh-groups
offset 0x1C: padding
offset 0x20: array_am — nº de líneas del axes-array por hueso (B3 = 3)
offset 0x24: label_loc — offset de la lista de labels (al final)
offset 0x28-0x2C: desconocido
offset 0x30: amg_loc  — offset del primer AMG (main)
offset 0x34: amg_loc2 — offset del 2º AMG; la lista de offsets AMG sigue aquí (4 B c/u)
```

Luego: tabla de offsets de AMGs (amg_am × 4 B, padding a 16), tabla de
relaciones, axes-array.

### 4.2 Entrada de relación de hueso (32 B, en bone_loc)

```
+0:  índice de hueso
+4:  ptr a su primera entrada del axes-array (16 B)
+8:  ptr CHILD
+12: ptr SIBLING
+16: ptr PARENT
+20..31: padding
```

### 4.3 Entrada del axes-array (16 B; array_am por hueso)

```
+0: 0
+4: índice de hueso
+8: ptr a la "axis line" (entrada de 80 B dentro del AMG)
+12: 0
```

### 4.4 Header del AMG (bloque de malla, 32 B) — magic "#AMG "

```
offset 0x04: 0x20 (base)
offset 0x0C: 0x04 (versión/sello)
offset 0x10: bone_am (nº de ejes)
offset 0x14: axes_loc (offset primera entrada de eje, normalmente 32)
offset 0x18: axis-lines por hueso (B3 = 3, SB2 = 1)
offset 0x1C: label_loc (offset lista de labels, al final del AMG)
offset 0x7C: ptr de la tabla de mesh-group (end_loc − 64)
offset 0x80: nº de model parts
```

### 4.5 Entrada de eje (80 B, en axes_loc)

```
+0..47:  datos de transformación (48 B):
         3×float posición (0,0,0) + float rot w=1.0 + 3×float + w=1.0
         + 3×float escala (1.0) + w=1.0 + 0x0F020060
+48:     padding
+52:     ptr a bloque de hueso/mesh/rig
+56:     ptr CHILD (offset a otro eje)
+60:     ptr SIBLING
+64:     ptr PARENT
+68..79: padding
```

### 4.6 Bloque de datos de hueso (16 B mínimo, en el ptr del eje +52)

```
+0:  índice de hueso
+4:  ptr header de mesh-group  (o 0 si solo rig)
+8:  ptr datos de rig (weights) (o 0 si solo mesh)
+12: ptr bloque "Mesh End" (64 B) (o 0)
```

Tipos de hueso (según punteros +4/+8/+12):
- empty:  0,0,0
- norms:  +4=0, +8≠0, +12=0  (solo rig)
- model:  +4≠0, +8=0, +12≠0  (solo mesh)
- mixed:  todos ≠0

### 4.7 Header de mesh-group (en el ptr de hueso +4)

```
+0:  mp_amnt (nº de model parts)
+4:  0x10
+8..15: padding (8 B)
+16..: tabla de offsets de model parts (mp_amnt × 4 B, relativos al inicio del mesh-group)
luego: los model parts
```

### 4.8 Header de rig (en el ptr de hueso +8)

```
+0..11: padding (12 B)
+12:    wv_am (nº de weight groups)
+16..:  tabla de weight groups (wv_am × 32 B)
luego:  datos v/vn (rig1) y v (rig2)
```

Entrada de weight group (32 B):
```
+0:  weight (float32)
+4:  vvn_am (nº entradas v/vn = rig1)
+8:  vvn_loc (ptr a datos rig1)
+12: v_am (nº entradas v-only = rig2)
+16: v_loc (ptr a datos rig2)
+20..31: padding
```

Entradas de rig:
- **rig1 (v/vn): 32 B** = coords V (3×float) + offset a vértice del mesh (u32) + normal VN (3×float + 4 pad)
- **rig2 (v only): 16 B** = coords V (3×float) + offset a vértice del mesh (u32)

### 4.9 Model part / mesh (formato de vértices — el más importante)

**Header del model part (160 B = 0xA0):**
```
+0:  type1: 0x01B5=437 (B5), 0x01B4=436 (B4), 0x0190=400 (90)
+4:  type2: 0x21B5=8629, 0x21B4=8628
+8:  índice de textura (0xFFFFFFFF = sin textura)
+12: índice de shader (0xFFFFFFFF = sin shader)
+16..143: parámetros de material (mayormente 1.0f)
+144: tamaño: 0x60000000 + (mesh_bytes / 16)
+160: datos de mesh (face blocks)
```

Fórmulas:
```
mesh_size  = (valor_en_+144 − 0x60000000) * 16
long_total = mesh_size + 160
```

**Formatos de vértice por tipo de part:**
- **B5 (48 B/vértice):** V x,y,z (12) + pad (4) + VN x,y,z (12) + pad (4) + VT u,v (8) + pad (8)
- **B4 (32 B):** V (12) + pad (4) + VT (8) + pad (8)
- **90 (16 B):** V (12) + pad (4)

**Face block / triángulo (176 B = header 32 + 3 vértices × 48):**
```
+0..11:  08 00 00 14 | 00 00 00 00 | 00 00 00 00  (tag)
+12..15: 00 C0 0A 6C → magia 0xC06C (detección de bloque)
+16:     01 00 00 00  (marcador 01)
+20:     03 00 00 00  (conteo de vértices del bloque: 3)
+24..31: padding
+32, +80, +128: los 3 vértices (48 B c/u en B5)
```

Footer del mesh-group: `triangle_end_bottom.bin` (16 B `08 00 00 14 ...`).
Bloque "Mesh End": 64 B (dummy floats + `00 00 80 3F`).
Labels: `bone_am × 32 B` al final de cada AMG (nombre en los primeros 16 B).

---

## 5. DIFERENCIAS OBSERVADAS PS2 vs 360 (Krillin, verificado bin a bin)

Comparación directa del MISMO bin (327 = Krillin) en GH PS2 y HD 360:

| Propiedad | PS2 GH (bin 327) | HD 360 (bin 327) |
|-----------|------------------|-------------------|
| Compresión | ninguna | LZX `/N:32` |
| Endianness | little-endian | big-endian |
| Magic modelo | `#AMO0` | `#AWO` |
| Magic mesh | `#AMG` (18, secuenciales) | `#AWG` (18, vía tabla de offsets) |
| Magic textura | `#AMT ` | `#AZT ` |
| Tamaño descomprimido | 812KB | 682KB |
| **Huesos** | **51** | **51** |
| **Mesh-groups** | **18** | **18** |
| **Labels de hueso** | **68 (idénticos)** | **68 (idénticos)** |
| Layout de AMGs | bloques consecutivos con magic | tabla de 18 offsets en 0x690 |

**Conclusión (corrige análisis previos)**: NO hay re-rigging. Es el mismo
esqueleto y las mismas mallas, solo que re-empaquetado en big-endian con
magics renombrados y un layout de mesh-groups distinto. Esto simplifica
enormemente el conversor: no hay que adaptar huesos, solo endianness +
renombrado de magic + reconstrucción de la tabla de offsets AMG.

**Detalle del layout interno HD (#AWO):**
```
#AWO  (header 0x30, big-endian):
  +0x10: bone_am (51)
  +0x14: bone_loc (0x30)
  +0x18: amg_am (18)
  +0x1C: offset tabla de offsets AMG (0x690)
  +0x20: array_am (24)
  +0x24: offset labels (0x6D8)
  +0x34: 0x42360 (fin del área de datos)

Tabla de offsets AMG (en +0x1C): 18 × uint32 BE → offset absoluto de cada #AWG
Cada #AWG (header 0x40, big-endian):
  +0x04: 0x40 (base)
  +0x0C: 0x04 (versión)
  +0x10: bone_am (51)
  +0x14: axes_loc
  +0x18: nº de ejes
  +0x1C: label_loc
  +0x20: offset del contenido
  ...labels de hueso al final del AWG (XKLL_BODY, etc.)
```

**Para convertir un modelo IW (PS2) → HD (360):**
1. Leer el #AMO0 + N×#AMG + #AMT little-endian
2. Convertir cada campo u32/u16 a big-endian
3. Renombrar magics (#AMO0→#AWO, #AMG→#AWG, #AMT→#AZT)
4. Reconstruir el layout: tabla de offsets AMG + ajustar punteros relativos
5. Recomprimir con `xbcompress /N:32` y empaquetar en el AFS
6. Validar comparando el render del personaje base convertido vs original

---

## 6. BINS DE PERSONAJES (mapeo real, del data_cmn GH = HD)

**IMPORTANTE**: hay DOS numeraciones distintas. La que importa para el recomp
es la del **data_cmn.afs** (modelos de batalla, movesets, mapas), que la HD
comparte con la **Greatest Hits/Collector's de PS2** (verificado por
instrumentación del runtime: Krillin lee bins 327-329 del data_cmn).

### 6.1 data_cmn.afs (GH = HD) — modelos de batalla

La fuente de verdad es `DBZ_B3_GH_Character_Bin_List.txt` (verificada contra
el AFL GH y la instrumentación). Los bins varían por región:
- **Krillin = 327-329** (GH) — VERIFICADO por instrumentación en el recomp
  (PAL es 321-323, +6)
- Cell = 146-151 (no cambia entre PAL y GH)
- Auras = 0-43 | Efectos = 504-555 | HUD = 452-503

### 6.2 data_usa/data_en.afs (región) — select/menús

Del `DATA_ENG.afl` GH (2705 entradas, índice = bin):
- `SCMKLL.amb` = bin 35 (modelo del select de Krillin)
- `BT-B00_KLL000-012` = bins 420-432 (variantes de batalla de región)
- `BT-B00_CEL000-027` = bins 157-184
- `CN-M0X` cinemáticas, `EN-E00` endings, `SK-SKL_XXX.amt` skills

### 6.3 Personajes IW que NO existen en la HD (bins IW reales)

Bins verificados leyendo el `ps2_games\Infinite World (USA)\USR\DATA_CMN.AFS`
(1136 entradas, sin comprimir, #AMO0 LE):

| Personaje | Bins IW |
|-----------|---------|
| **Janemba** | 541-544 (541 amo + 542 amt + 543/544 recolour) |
| **Pikkon** | 583-586 |
| **Pan** | 566-569 |
| **Super 17** | 606-609 |
| **Super Baby Vegeta 2** | 678-681 |
| Syn Shenron | 97-98 (default) + 101-102 (recolour) |
| Omega Shenron | 99-100 + 103-104 |
| Bubbles | 118-119 (NPC) |
| Giru | 338-339 (cutscene) |
| Goku GT | 341-358 + 383-396 + 419-432 |
| Vegeta GT | 685-696 + 705-722 |
| Great Saiyaman 2 (Saiyawoman) | 482-489 |
| Shenron | 970-971 (NPC) |

Para añadirlos: convertir sus modelos IW (#AMO0 LE) → #AWO BE (ver sección 5)
y los movesets de los ports (ver sección 7).

---

## 7. RECURSOS DISPONIBLES (rutas)

### Herramientas (mod center)
- `Model Compiling Tools\` — AMO Compiler.py / Decompiler.py (**código fuente de la estructura**)
- `OBJ to AMG v0.92\source code.zip` — escritor de AMG LE (plantillas binarias)
- `EMD to AMG v0.90\source code.zip` — conversor EMD→AMG (desde Xenoverse)
- `B3_IW Model Converter\amb_model.py` — reempaque AMB↔AMO/AMT
- `Axis Line Tool\`, `Bone Addition Tool v1.02\` — estructuras de ejes/huesos
- `BoneAxis Display\axis_data.py`, `Delete_AXIS.py` — lectura de AMO
- `Model Rig Toolset V0.6\`, `Model-Rig Extractor Tool V1.0\` — extracción de rigs
- `Model Merger Tool (32-Bit)\` — fusión de modelos
- `B3-to-SB2.py` — conversión AMG B3→SB2 (vértices u16)
- `Xbox 360 Compression...\` — xbcompress/xbdecompress (LZX)
- `basic_functions.py` (en varias herramientas) — **helpers hex_to_int_be/int_to_hex_be**
- `A3T Analyzer.py` — texturas A3T big-endian (referencia BE)

### Recursos de datos (modding resources)
- `ps2_games\` — AFS completos de B1, B2, B2V, **B3 GH**, **IW** (referencias PS2)
  - B3 GH: `USR\data_cmn.afs` (3990 bins) = **misma numeración que la HD** → comparar bin a bin
  - IW: `USR\DATA_CMN.AFS` (1136 bins) = personajes exclusivos sin comprimir
- `Budokai Models\` — 279 pares .amo/.amt (modelos IW) + exclusivos B3GHC/B2V
- `All Character Models from IW into AMB format\` — 241 modelos IW en .amb
  (Janemba.amb 934KB: 48 huesos/17 AMG; Pikkon.amb 805KB: 58 huesos/16 AMG)
- `Infinite World to Budokai 3 Moveset Ports\` — 8 ports de moveset:
  - **Janemba** → Krillin (B3: 330-333, 539 = #AMB LE: #AMC moveset, #AMO0+#AMG, #AMT+#AMO0+#AMG×3)
  - Goku GT → Teen Gohan | Great Saiyawoman → Kid Gohan | Pan → Nappa
  - Pikkon → Raditz | Super 17 → Android 17 | Super Baby Vegeta → Kid Trunks
  - Future Gohan (Shin Budokai) → ghf_365/367
- `map swap in b1\Stages\` — 13 escenarios de B1 extraídos (BBKAM, BBTEN, BBHAKB...)
  formatos: .BD colisión, .HD, .MAD, .MAS, .MDD geometría, .SPX, .SQ
- `Complete AFL ... DATA_ENG.afl` — AFL región GH (2705 entradas, 32B/reg)
- `Data_CMN file name list (Budokai 3 Pal)\data_cmn.afl` — AFL data_cmn PAL (3945 entradas)
- `ADX AFL v4 for Budokai 3\` — AFL de audio (adx_jpn/usa), bins ADX por personaje
- `DBZ_B3_Character_Bin_List.txt` (PAL) / `DBZ_B3_GH_Character_Bin_List.txt` (GH)
- `All_Character_Slots Infinite World.txt` — slots IW (Janemba (1)(2), Pikkon (1)(2)...)
- `Voice list for Infinite World.txt` — rangos de bins de voz IW
- `Budokai_3_Capsules_IDs.txt`, `Budokai 1 and 2 Capsule Data\` — cápsulas
- `OCR scanned...\SkillList.dson` — 579 skills SK-SKL_### (DSON)
- `Budokai 3 GH's menus relevant stuff...\B3_GH_-_Data_USA_breakdown.txt` — mapa de menús
- `Story Mode lists for Budokai 3 GH\` — bins BPL/LST de historia GH
- `Story_Mode_B3_Pal_Tool_V0.4.rar\` — editor de story mode PAL (.NET, patchea el exe)
- `Tail AMO\` — AMO de cola custom (7 huesos) + aviso: **NO mergear WAIST (crash)**
- `EmbPack-v2-LibXenoverse\` + `EmdFbx-and-FbxEmd-LibXenoverse\` — ecosistema Xenoverse
  (EMD/ESK/EAN/EMB ↔ FBX): eslabón para importar/exportar modelos
- `Super Dragon Ball Heroes World Mission\` — **30402 archivos EMD/ESK/EAN/EMB**
  (Xenoverse): 392 personajes, incluidos Janemba (bcbjn), Pikkon (bcbjk/bcpkk),
  Super 17 (bcs17), Pan (bcpan), Super Baby (bcvby). Estilo "chibi" — evaluar si vale.
- `DDS_PNG\` — conversor DDS↔PNG (texturas)

### Texturas
- PS2: `#AMT ` (206KB en Krillin); HD: `#AZT ` (391KB en Krillin).
- `A3T Analyzer.py` (mod center) maneja texturas A3T big-endian (referencia).
- `DDS_PNG\DDS_PNG.exe` para convertir texturas extraídas a PNG.

---

## 8. PENDIENTES / PRÓXIMOS PASOS

El plan se ha **simplificado** tras verificar que PS2 y HD comparten el mismo
esqueleto (51 huesos, 68 labels idénticos). Ya no hace falta adaptar rigs,
solo convertir endianness + magics + layout. Pasos:

1. [x] Verificar que PS2 GH y HD comparten numeración de bins (Krillin = 327)
2. [x] Confirmar mismo esqueleto (51 huesos / 18 AMG / 68 labels idénticos)
3. [ ] **Mapear campo a campo** el #AMO0 vs #AWO (punteros relativos vs absolutos)
4. [ ] **Mapear la estructura del #AWG** (header 0x40, ejes, mesh parts, vértices B5/B4)
5. [ ] **Mapear el formato de textura #AZT** (vs #AMT/A3T)
6. [ ] Escribir el conversor #AMO0→#AWO (helpers BE + re-layout)
7. [ ] Validar: convertir Krillin IW/GH → cargar en HD y comparar render
8. [ ] Aplicar a personajes IW (Janemba 541-544, Pikkon 583-586, Pan, Super 17...)
9. [ ] Añadir personaje nuevo: slot + modelo + moveset (ports existentes) + voz
10. [ ] Explorar bins vacíos del data_cmn para slots de personajes nuevos
11. [ ] Integrar el conversor en el launcher (mod center UI)
