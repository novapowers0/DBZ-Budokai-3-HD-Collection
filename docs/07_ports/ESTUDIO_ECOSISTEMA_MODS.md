# ESTUDIO DEL ECOSISTEMA DE MODS — Ports PS2 → B3 HD

> Fecha: 2026-08-26. Estudio sistemático de cómo funcionan los mods, inventario
> y comparativa de las herramientas (comunidad vs nuestras), y evaluación de
> cuánto hay que modificar las nuestras para especializarlas en ports
> **PS2 (#AMO0/#AMG LE) → B3 HD (#AWO/#AWG/#AZT BE)**.

---

## 1. CÓMO FUNCIONAN LOS MODS EN ESTE PROYECTO

### 1.1 Los dos tipos de mod (runtime rexruntime.dll)

| Tipo | Layout | Hook | Uso |
|---|---|---|---|
| Override de archivo completo | `mods/<mod>/us/<afs>` | `AfsFindModFileOverride` | Música/packs (og_music) |
| **Override por entrada** (RECOMENDADO) | `mods/<mod>/us/data_cmn.afs/<entry>/geom.bin` | `AfsFindModOverride` + **mid-insert virtual** | Modelos, texturas (~100KB) |

El runtime sirve archivos por ENTRADA del AFS sin reempaquetar nada; con el
**mid-insert virtual** (tabla AFS virtual consistente) el bin del mod puede ser
cualquier tamaño (>slot), la entrada crece in-place y las posteriores se
desplazan. 2+ mods de modelo/textura activos simultáneamente.

### 1.2 Pipeline de un mod de modelo (validado end-to-end)

```
1. extraer bin LZX del AFS (tabla en offset 8)      → xbdecompress
2. modificar el bin (swap/textura/geometría)
3. comprimir  xbcompress /N:2048                    → SIEMPRE /N:2048
4. pad al to_read (o to_read_virtual si excede)     → swap_b3.py lo hace solo
5. instalar  mods/<mod>/us/data_cmn.afs/<entry>/geom.bin
6. activar   (quitar .disabled)
7. verificar logs: "AFS OVERRIDE HIT (folder)" + "AFS MOD READ: got=to_read"
```

### 1.3 Herramientas ESTABLES del Launcher (NO tocar)

| Herramienta | Función | Launcher |
|---|---|---|
| `swap_b3.py` | Swap nativo B3→B3 (extrae bin #AMB, LZX /N:2048, override) | Pestaña Model Swap |
| `texture_b3.py` | Texturas: #AZT→PNG, re-codifica DXT3, reconstruye bin | Pestaña Texturas |
| `catalog_b3.cat` | Catálogo de 183 personajes del data_cmn.afs | Pestañas Model Swap/Texturas |

Estas funcionan y están en producción. El estudio **no las toca** salvo para
reusar sus utilidades (LZX, padding, instalación de override) dentro del nuevo
pipeline de ports.

---

## 2. EL ECOSISTEMA DE LA COMUNIDAD (inventario verificado)

Exploradas: `mod center\` (36 programas), `modding resources\`,
`modding resources discord\` (research/245, tools/58, tutorials/78),
`modding resources update\`, `modding resources update 2\` (MOD EJEMPLO).

### 2.1 Herramientas de modelo (todas PS2 little-endian)

| Herramienta | Qué hace | Formato |
|---|---|---|
| **B3_IW Model Converter** (amb_model.py/functions.py) | Solo desempaqueta/empaqueta el contenedor #AMB (AMO+AMT por tabla). NO toca geometría | PS2 |
| **OBJ to AMG v0.92** | OBJ→mesh parts PS2 desde cero (templates `model_part_header.bin`, `triangle.bin`; vértices 48B expandidos por triángulo, FaceType=1) | PS2 |
| **EMD to AMG v0.90** | EMD Xenoverse/SDBH → AMG PS2 por model part (solo malla V/VN/UV, lee bone labels pero NO mapea) | PS2 |
| **AMO Decompiler/Compiler** (Model Compiling Tools) | #AMO0 ⇄ (AXES.bin + *.mesh + *.rig1/rig2 + AMO.txt). Reconstruye ejes, rig chunks, labels | PS2 |
| **Model Merger (AMO_LGBT)** | Fusiona el 1er AMG de 2 #AMO0 (Ginyu bodyswap), reubica rig offsets | PS2 |
| **Model-Rig Extractor v0.6/v0.9** | Extrae part + **rig data por hueso**: chunks 32B/sub-chunks 16B con offset de vértice en +12 → mapeo rig→malla 100% | PS2 |
| **Model Part Editor** | Convierte mesh parts B3⇄B1 (headers/shader/rgb_lines) | PS2 |
| **B3-IW AMO Converter + Shadows** (Scoops999) | B3/IW→B1 (re-mapea cabeceras mesh part; sin source) | PS2 |
| **B3-to-SB2.py** | AMG B5 01 de B3/IW→SB2 | PS2 |
| **Bone Addition Tool v1.02** | Añade hueso al AMO (eje 80B, child/sibling/parent, labels) | PS2 |
| **Bin to OBJ V3 / AMG to OBJ V2** | AMG PS2 → OBJ (solo los generados por las herramientas de la comunidad) | PS2 |
| **budokai_updated.ms** (MaxScript) | Importa AMO/AMG B3 PS2 a 3ds Max; **documenta el IB implícito** (FaceType 1=strip, 0=triplete) | PS2 |
| **Zero Devs' Tool** | BT3p→Budokai (APK Android; solo tutorials en el acervo) | PS2 |
| **ama10.java, AMA tools** | Parser del esqueleto AMA de B1/B2/GC | PS2 |

### 2.2 Herramientas HD 360 (las únicas que tocan el lado 360)

| Herramienta | Qué hace |
|---|---|
| **xbcompress / xbdecompress** (XDK) | LZX `/N:2048` (el formato del juego). Agnóstico del contenido |
| **AZT_Tools** | Edición de texturas #AZT (360) |
| **B3_AMB_PS3.bt** (010 Editor) | Template big-endian del #AMB→#AWO→#AWG. ⚠️ PS3: algunos campos (off+size vs offsets) no coinciden con X360 (AGENTS §13.2) |
| **DBZ B3 (X360) Lesson 1/2** | Tutoriales de compresión LZX y edición de texturas AZT (DDS BC2/DXT3) |

### 2.3 Datasets de modelos fuente (acceso ilimitado)

| Dataset | Contenido | Uso |
|---|---|---|
| `ps2_games\` (B1, B2, B2V, B3 GH, IW) | AFS completos PS2 | **Fuente para ports** |
| `modding resources\All Character Models from IW into AMB format\` | **241 .amb** (Janemba, Pikkon, Pan, Super 17, Super Baby...) | Personajes que NO existen en HD |
| `modding resources\Budokai Models\` | 279 .amo/.amt IW + B3GHC (28) + B2V (48) | Idem |
| `modding resources update\Budokai 1 Models Converted to AMB\` | 230 .bin B1→AMB | B1 |
| `modding resources update 2\MOD EJEMPLO\` | Ginyu Force en B3 AMB + IW AMO/AMT | Ejemplo del patrón comunitario |
| SDBH WM (`modding resources\Super Dragon Ball Heroes...\`) | 10.997 .emd + 972 .esk + 442 .ean | Modelos Xenoverse chibi |

---

## 3. COMPARATIVA: COMUNIDAD vs NUESTRAS HERRAMIENTAS

### 3.1 VEREDICTO CENTRAL

**NINGUNA herramienta de la comunidad convierte PS2 → HD 360.** Verificado por
código: todas usan `struct.pack('<L')` (LE → PS2); la única BE es A3T Analyzer
(solo lee texturas). La comunidad trabaja PS2→PS2 (B1⇄B3, IW→B3, EMD→AMG,
OBJ→AMG) o edita el HD con 010 Editor + template. El salto PS2→360 es un
**re-layout propio** (endianness + magics renombrados + tabla de offsets AMG).

### 3.2 Qué aporta cada lado

| Pieza | Comunidad | Nuestro proyecto |
|---|---|---|
| Parseo malla PS2 (IB real por FaceType) | `budokai_updated.ms` (MaxScript) | `parse_ps2_mesh.py` ✅ |
| Skin/rig PS2 (bone+peso por vértice) | `Model-Rig Extractor` (documenta el desfase relativo al AMG) | `ps2_rig_skin.py` ✅ (desfase `amg_abs` resuelto) |
| Esqueleto/pose PS2 | `AMO Decompiler`, `Axis Line Tool` | `pose_matrix.py` ✅ |
| Retopología (OBJ→AMG→AMB) | `OBJ to AMG`, `EMD to AMG`, Blender+EmdFbx | `obj_to_awg_hd.py` (layout B1, desactualizado) |
| Estructura de dibujo HD (descriptores/arms) | **NADA** (editan con 010 Editor) | `analyze_meshgroup.py`, `analyze_mesh.py` (parseo) — **falta generador** |
| Export HD→OBJ (feedback) | **NADA** | `awg_to_obj_b3.py`, `awg0_export.py`, `awg_cara_export.py` ✅ |
| Geometría PS2→buffers HD | **NADA** | `ps2_to_hd_geometry.py` ✅ |
| Empaquetar bin HD autocontenido | **NADA** | `build_from_template.py` / `build_awo_autocontenido.py` (a medio hacer) |
| Texturas #AMT→#AZT | `AZT_Tools` (edita AZT directo) | `texture_b3.py` (AZT→PNG→AZT) |
| Instalación del mod | AFS Toolset | `swap_b3.py` + runtime (override + mid-insert virtual) ✅ |

### 3.3 Conclusión de la comparativa

- **No hay atajo comunitario PS2→360**: nadie en la comunidad lo ha resuelto
  públicamente (lo más cercano es `.aerithdevs`, Java en desarrollo, sin link).
- **La mitad PS2 está resuelta** (parseo, rig, retopología) — por la comunidad
  y por nosotros.
- **La mitad HD la construimos nosotros** y ya está validada hasta: swap nativo
  B3→B3 ✅, formato C descifrado ✅, mid-insert virtual ✅, exportadores OBJ ✅,
  geometría PS2→HD ✅, rig PS2 ✅.
- **EL GAP ÚNICO** es la **estructura de dibujo HD** (mesh-ref blocks +
  descriptores + arms) generada desde cero coherente con geometría nueva. Es lo
  que falló en Janemba/Krillin PS2/A18 (inyección en plantilla) y lo que nunca
  se validó en `janemba_from_cell` (por la DLL stale del runtime).

---

## 4. EVALUACIÓN: CUÁNTO HAY QUE MODIFICAR NUESTRAS HERRAMIENTAS

### 4.1 Clasificación de nuestras herramientas (inventario completo)

| Categoría | Scripts | Acción |
|---|---|---|
| **STABLE-Launcher** | `swap_b3.py`, `texture_b3.py`, `catalog_b3.cat` | **NO tocar** (producción). Reusar sus utilidades |
| **ANALISIS-funciona** | `awg_to_obj_b3.py`, `awg0_export.py`, `awg_cara_export.py`, `awg_parts2.py`, `parse_ps2_mesh.py`, `ps2_rig_skin.py`, `pose_matrix.py`, `build_hd_world_mats_b3.py`, `build_afs.py`, `build_big_amb.py`, `build_ib_from_ps2.py`, `decimar.py`, `decimar_tri.py`, `fbx_ascii.py`, `fbx_parser.py`, `json_to_obj.py` | **Mantener** (bucle de feedback + etapas) |
| **COMPONENTE-conversor** (a medio hacer) | `ps2_to_hd_geometry.py`, `build_awo*.py` (4), `build_hd_pipeline.py`, `build_awo_autocontenido.py`, `build_from_template.py`, `retarget_hd.py`, `emd_to_awo_hd.py`, `obj_to_awg_hd.py`, `build_awo_from_json.py` | **Reconstruir/consolidar** en pipeline nombrado |
| **EXPERIMENTO-fallido** | `mezclar_ps2_hd*.py` (v1-v6), `inyeccion_awg.py`, `port_ps2_to_b3.py`, `port_b1_to_b3.py`, `relayout_*.py`, `swap_cabeza*.py`, `swap_cuerpo_hd*.py`, `inject_a18*.py`, `build_janemba*.py`, `convert_personaje.py`, `rig_mapeo.py` | **NO usar como base** (archivados/`historial_fallidos`) |
| **DESACTUALIZADO** | `analyze_bin_hd.py` (layout PS3), `awg_to_obj.py`/`obj_to_awg.py` (layout B1) | Corregir o marcar como PS3/B1-only |

### 4.2 El gap a resolver (único bloqueador)

La estructura de dibujo de un bin HD se compone de:
- **mesh-ref blocks** (13×0x50 en el mesh group) + **arms** (sellos 0x204) que
  definen límites del IB en bytes.
- **descriptores de submesh** (0x60 bytes; label en +00, `max N m` en +18,
  rangos A en +50/+54, B en +58/+5C) — layout mapeado en
  `awo_tools/SUBMESH_DATA_B3.md`.
- La **zona de submesh data** del AWG0 (labels + strings `max N m` en
  0x2D61-0x3471).

La comunidad construye esto para PS2 con `amg_c.py` (templates `b3_amg_*.bin`).
**Falta el equivalente HD**: un generador que, dada la geometría convertida
(sec34/vb2/IB) y el esqueleto, emita la estructura de dibujo coherente.

### 4.3 Evaluación de esfuerzo por pieza

| Pieza | Esfuerzo | Base |
|---|---|---|
| `port_ps2_b3_extract` (parseo PS2 completo) | Bajo | Clonar `parse_ps2_mesh.py` + `ps2_rig_skin.py` + `pose_matrix.py` |
| `port_ps2_b3_geometry` (coords→buffers HD) | Bajo | Clonar `ps2_to_hd_geometry.py` |
| `port_ps2_b3_draw` (**estructura de dibujo HD**) | **ALTO (RE fina)** | `analyze_meshgroup.py` + patrón `amg_c.py` (comunidad) en HD |
| `port_ps2_b3_pack` (AMB autocontenido + LZX + override) | Medio | Clonar `build_from_template.py`/`build_awo_autocontenido.py` + utilidades de `swap_b3.py` |
| `port_ps2_b3_verify` (feedback loop) | Bajo | Clonar `awg_to_obj_b3.py` + chequeo bounds/NaN |
| `port_ps2_b3_textures` (#AMT→#AZT) | Medio | Formato AZT mapeado + `texture_b3.py` |

---

## 5. LECCIONES DE LOS FRACASOS (para NO repetir)

1. **Inyectar geometría PS2 en la plantilla de Krillin SIEMPRE deforma**: el HD
   de Krillin es re-topologizado (0% correspondencia de vértices) y la
   estructura de dibujo de Krillin no coincide con la geometría inyectada. →
   Construir **bins HD autocontenidos** con estructura de dibujo propia.
2. **Reconstruir el IB/arms de un bin existente rompe el render**: el guest
   deserializa la estructura por los offsets del AWG header y los conteos son
   fijos. → Para un personaje NUEVO, emitir con conteos propios y estructura
   coherente (formato de plantilla simple probada, Babidi/Bulma).
3. **El bone index del vértice HD va en +28 (u32)**; el layout real es
   `[0xFFFFFFFF, u, v, z_local, x_local, y_local, peso, BONE@+28, nz, -ny, nx]`
   (formato A). **NO copiar el layout del B1** (bone@+16) al B3.
4. **El IB PS2 es implícito** (FaceType strips/tripletes), NO lista de índices.
   No asumir tripletes (`extract_geometry.py` obsoleto; usar `parse_ps2_mesh.py`).
5. **Compresión SIEMPRE `/N:2048`** (no `/N:32`). **Padding al to_read** exacto.
6. **El runtime del build debe tener la DLL correcta** (mid-insert virtual);
   el cmake sobrescribe `rexruntime.dll` con la versión stale de `rexglue/bin`.
   Si falta, los overrides grandes fallan EN SILENCIO (se ve el personaje
   original, sin crash) — costó 2 sesiones entenderlo.
7. **Cada bin HD es autocontenido con su propio formato de vértice** (A/C/otros).
   El guest autodetecta. No forzar el formato A de Krillin.
8. **La vía de la comunidad para añadir personajes = reconstrucción completa**
   (EMD/OBJ→AMG→AMB), NO inyección. Confirmado repetidamente.

---

## 6. DOCUMENTOS DE REFERENCIA

- `AWO_FORMAT.md` — formato del modelo PS2 vs HD (re-layout, no formato distinto).
- `awo_tools/CONSOLIDADO.md`, `RE_PROGRESO.md`, `RE_AWO_HD_CONVERSOR.md` — RE completa.
- `awo_tools/SUBMESH_DATA_B3.md` — layout de los descriptores de submesh (mapeado).
- `awo_tools/SESION_2026-08-17.md` — vía de reconstrucción completa.
- `mod center hd/GUIA_SWAPS_Y_PORTS.md` — principio de los swaps.
- `docs/02_mods/COMO_HACER_MODS.md` — pipeline de mods.
- `docs/03_formatos/AMO_AWO.md`, `BIN_LAYOUT.md` — formatos.
- Siguiente: `docs/07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md` — hoja de ruta con el
  pipeline nombrado.