# RE DEL CONVERSOR #AMO0 → #AWO (fase 3) — PROGRESO

> Documento de trabajo de la ingeniería inversa del formato HD 360 (#AWO/#AWG/#AZT)
> comparado contra PS2 (#AMO0/#AMG/#AMT). Krillin bin 327 (GH PS2 = HD 360).
> Archivos: `%TEMP%\opencode\b327_ps2.bin` (812KB #AMB LE) y `b327_hd.bin` (682KB
> #AMB BE descomprimido). Herramientas: `awo_tools\*.py`.
> Estado: MAPEO DE ESTRUCTURAS EN CURSO.

## 1. RESUMEN DE HALLAZGOS

- **Mismo esqueleto**: PS2 GH y HD 360 comparten el mismo modelo Krillin
  (51 huesos, 18 AMG/AWG, 68 labels de hueso idénticos). **NO hay re-rigging**.
- **Mismos formatos de vértice**: B5 (`01 B5` LE / `00 00 01 B5` BE), B4, 90.
- **Mismo número de mesh parts**: hueso 0 = 13 parts en ambos.
- Diferencias: endianness + magics renombrados + layout de secciones.

## 2. MAGICS RENOMBRADOS

| PS2 (LE) | HD 360 (BE) | Contenido |
|----------|-------------|-----------|
| `#AMB ` | `#AMB ` | Contenedor |
| `#AMO0` | `#AWO ` | Modelo (header 0x30) |
| `#AMG ` | `#AWG ` | Mesh group (header 0x40 HD) |
| `#AMT ` | `#AZT ` | Textura |

## 3. HEADER DEL CONTENEDOR #AMB

```
+0x00: magic #AMB
+0x0C: nº entradas (PS2=3: AMO0+AMT+pad; HD=2: AWO+AZT)
+0x20: tabla de entradas (loc+size) × 16B
```

## 4. HEADER DEL MODELO (#AMO0 vs #AWO) — ambos 0x30

| Campo | PS2 #AMO0 | HD #AWO |
|-------|-----------|---------|
| magic | `#AMO0` | `#AWO ` |
| +0x10 bone_am | 51 | 51 |
| +0x14 bone_loc | 0x80 | 0x30 |
| +0x18 amg_am | 18 | 18 |
| +0x1C | (amg_loc en 0x30) | **tabla de offsets AMG = 0x690** |
| +0x20 array_am | 24 | 24 |
| +0x24 | label_loc AMO | **labels huesos = 0x6D8** |
| +0x34 | amg_loc2 | 0x42360 (fin área) |

**Layout HD del #AWO** (offsets relativos al AWO):
```
+0x00: header 0x30
+0x30: tabla de relaciones de huesos (51 × 32B = 0x660) → termina 0x690
+0x690: tabla de offsets AMG (18 × 4B = 0x48) → termina 0x6D8
+0x6D8: labels de huesos (51 × 32B = 0x660) → termina 0xD38
+0xD40: primer #AWG
```

## 5. TABLA DE RELACIONES DE HUESO (32B/entrada) — MISMA en ambos

```
+0:  índice de hueso
+4:  ptr a su entrada del axes-array (16B)
+8:  ptr CHILD (a otra entrada de relación)
+12: ptr SIBLING
+16: ptr PARENT
+20..31: padding
```

- PS2: bone_loc=0x80 (rel AMO0). Ej: hueso0=[0, 0x6e0, 0xa0, 0, 0]
- HD: bone_loc=0x30 (rel AWO). Ej: hueso0=[0, 0x42360, 0x50, 0, 0]
- Los punteros son **relativos al AMO/AWO** en ambos.

## 6. AXES-ARRAY (16B/entrada) — MISMO formato

```
+0: 0
+4: índice de hueso
+8: ptr a su "axis line" (entrada de eje de 80B)
+12: 0
```

- PS2: hueso0 en 0x6e0 → axis_line_ptr=0x5380 (rel AMO0 → abs 0x53C0)
- HD: hueso0 en 0x42360 → axis_line_ptr=0x1750 (rel AWO → abs 0x1790)

## 7. ENTRADA DE EJE (80B) — MISMO formato, punteros relativos distintos

```
+0x00..0x2F: transformación (floats identidad: 0,0,0,1.0 ×2 + 1.0×5)
+0x30: sello 0x6000020F (LE `0F 02 00 60`, BE `60 00 02 0F`) — IDÉNTICO
+0x34: ptr a bloque de hueso/armature (PS2 rel AMG / HD rel AWG)
+0x38: ptr CHILD (a otro eje) 
+0x3C: ptr SIBLING
+0x40: ptr PARENT
+0x44..0x4F: padding
```

- PS2 eje0: +0x34=0x1010 → armature 0x63B0 (rel AMG)
- HD eje0: +0x34=0x1A00 → armature 0x2780 (rel AWG)

**NOTA**: en HD el +0x34 apunta relativo al AWG; en PS2 relativo al AMG. En
ambos el armature del eje0 (0x2780 HD) contiene: `[0, mesh_hdr=0x1F80, 0, mesh_end=0x1E00, ...]`.

## 8. BLOQUE DE HUESO / ARMATURE (16B) — MISMO formato

```
+0:  índice de hueso
+4:  ptr header de mesh-group (rel AMG/AWG) o 0
+8:  ptr datos de rig (weights) o 0
+12: ptr bloque "Mesh End" (64B) o 0
```

- PS2 hueso0 armature @0x63B0: [0, 0x1020, 0, 0x26CA0] → mesh hdr @0x63C0
- HD hueso0 armature @0x2780: [0, 0x1F80, 0, 0x1E00] → mesh hdr @0x2D00

## 9. HEADER DE MESH-GROUP — DIFIERE (PENDIENTE MAPEO FINO)

- **PS2** @0x63C0: `[mp_amnt=0xD, 0x10, 0, 0, offs...]` + tabla de offsets de
  mesh parts en +16 (relativos al mesh-group header). part0 = +0x50 → 0x6410.
- **HD** @0x2D00: `[0xD, 0, 0, 0, 0, floats...]`. Los mesh parts HD están
  **ANTES** del mesh-group (part0 @0x1450 vs mg @0x2D00). Estructura distinta.

## 10. MESH PART — LAYOUT DISTINTO

**PS2 (header 0xA0=160B)**: type en +0x00, mesh_size en +0x90, vértices desde +0xA0.
```
+0x00: type1 (01 B5) | +0x04: type2 (BD 29) | +0x08: textura | +0x0C: shader
+0x90: 0x60000000 + (mesh_bytes/16)
+0xA0: datos de mesh (face blocks + vértices)
```
PS2 part0 @0x6410: size=0xA90 → part0 ocupa 0xB30 (hasta 0x6F40 = part1).

**HD (header 0x50=80B)**: headers contiguos; vértices en bloque aparte.
```
+0x00: índice del part (0,1,2...)
+0x04: nº textura/material (1,3,6... o 0xFFFFFFFF)
+0x08: type1 (00 00 01 B5)
+0x0C: type2 (00 00 29 BD)
+0x10: 0x44 (68)
+0x14: 0x44 (68)
+0x18..0x1F: 0
+0x20..0x3F: floats 3F800000 (material, patrón 3×1.0+0 repetido)
+0x40: 0
+0x44: 0x5 (¿count?)
+0x48: 0
+0x4C: 0x5
```
HD parts contiguos: 0x1450, 0x14A0, 0x14F0, 0x1540, 0x1590, 0x15E0, 0x1630,
0x1680, 0x16D0, 0x1720(B4), 0x1770 — separados 0x50 (80B).

## 11. FORMATO DE VÉRTICES (confirmado en ambos)

- **B5** (48B/vértice): V xyz(12) + pad(4) + VN xyz(12) + pad(4) + VT uv(8) + pad(8)
- **B4** (32B): V(12) + pad(4) + VT(8) + pad(8)
- **90** (16B): V(12) + pad(4)
- LE en PS2, BE en HD. Las posiciones/normales coinciden (misma geometría).

## 12. ZONAS DE DATOS DEL AWG HD (del header +0x28..0x3C)

```
+0x28: 0x2700   (rel AWG) 
+0x2C: 0x17868  (rel AWG) → datos de rig/index
+0x30: 0x19F68  (rel AWG) → índices de triángulos
+0x34: 0x2826
+0x38: 0x1C790  (rel AWG) → ?
+0x3C: 0x24
```

## 13. ESTRATEGIA DE CONVERSIÓN

### 13.0 CONFIRMACIÓN: conversión 1:1 genérica (verificado con 3 personajes)

| Personaje | PS2 GH (huesos/AMG) | HD 360 (huesos/AMG) |
|-----------|---------------------|---------------------|
| Krillin | 51 / 18 | 51 / 18 ✓ |
| Cell | 54 / 19 | 54 / 19 ✓ |
| Goku | 64 / 19 | 64 / 19 ✓ |
| Janemba (IW) | 48 / 17 | — (no existe) |

La conversión #AMO0→#AWO es **sistemática y genérica**: el mismo personaje
tiene el mismo nº de huesos/AMGs en ambos. No depende del personaje. Los bins
HD descomprimidos adicionales: `%TEMP%\opencode\b146_hd.bin` (Cell HD),
`b352_hd.bin` (Goku HD), `b146_ps2.bin`/`b352_ps2.bin` para la comparación
campo a campo.

### 13.1 Enfoque PLANTILLA (para personajes que ya existen en HD)
Dado que el esqueleto y la jerarquía son idénticos:
1. **Plantilla**: usar un #AWO HD existente (mismo personaje base) como plantilla
   de estructura — headers, tablas, punteros, ejes, jerarquía.
2. **Geometría**: convertir los mesh parts PS2 (vértices/índices/texturas) a BE
   y reemplazar los del AWO HD.
3. **Mesh group**: el mesh-group HD (13 parts) referencia sus parts vía bloques
   mesh-ref de 0x50B contiguos (sello 0x9000020C/0x8000020C + ptr armature + ptr
   datos de índices + ptr transformación).

### 13.2 Enfoque PORT (para IW-exclusivos como Janemba) — RECOMENDADO
Los moveset ports IW→B3 ya existen y **usan el rig de Krillin** (el personaje
anfitrión). El bin `modding resources\Infinite World to Budokai 3 Moveset
Ports\Janemba\B3\unnamed_333.bin` (2.2MB) es el modelo Janemba ya adaptado al
rig de B3. El bin `unnamed_331.bin` (#AMO0) es un mini-modelo.

**PENDIENTE**:
- [ ] Confirmar que el modelo del port (unnamed_333/331) es compatible con el
      formato AWO (mismo nº huesos que Krillin HD)
- [ ] Mapear el mesh-group HD (cómo referencia sus 13 parts) — en curso
- [ ] Localizar los datos de vértices HD por part — en curso
- [ ] Mapear el formato de textura #AZT vs #AMT
- [ ] Escribir el conversor (endianness + renombrado + re-layout)
- [ ] Validar con Krillin (convertir GH→HD y comparar bytes/render)

## 14. HALLAZGOS ADICIONALES DEL MESH-REF HD

El mesh-group HD @0x2D00 referencia sus 13 mesh parts vía bloques de 0x50B
contiguos (en 0x1ED8 en Krillin). Cada bloque:
```
+0x00..0x17: floats de transformación (matriz/identidad)
+0x18: sello 0x9000020C / 0x8000020C (tipo de part)
+0x1C: ptr armature (rel AWG): 0x1BCC, 0x1BE0, 0x1BF4... (→ estructuras de hueso)
+0x20: ptr datos de índices/normales (rel AWG): 0x1190, 0x11E0, 0x1230...
+0x24: 0
+0x28: ptr transformación (rel AWG): 0x10F0, 0x1140...
+floats del material después
```

El mesh-group header HD (0x2D00):
```
+0x00: mp_amnt (13)
+0x04..0x0F: 0
+0x10: 0 | +0x14..0x1F: floats | +0x20/0x24: 0
+0x28: ptr a la tabla de bloques mesh-ref (0x1158 rel AWG → 0x1ED8)
+0x2C: 0x2C (ptr a zona de labels de hueso del AWG)
+0x30: 0x5 | +0x34..0x38: 0 | +0x3C: 0x2A | +0x40: 0 | +0x44: 0x48
+0x48+: labels (XKLL_BODY...)
```

## 15. CONCLUSIÓN CRÍTICA: LA GEOMETRÍA NO ES 1:1

**El atajo de "mismos bloques en BE" NO funciona.** Verificado:
- Las posiciones de vértice del part0 PS2 (1.0988, -4.9691, 0.4898...) **NO
  existen** como floats BE en el HD.
- El HD tiene ~42K floats de posición vs ~141K del PS2 (menos vértices).
- El HD usa **index buffer con primitive restart** (0xFFFF separa strips):
  - `AWG+0x30` → 0x1ACE8: índices u16 de triángulos (valores < 64, coherentes)
  - `AWG+0x38` → 0x1D510: restart FFFF + índices de lineas/triangles
  - `AWG+0x2C` → 0x185E8: vertex buffer (floats)
  - `AWG+0x34` → 0x35A6: más datos de vértices

**Implicación**: el conversor PS2→HD no es un byte-swap de geometría. Requiere
re-empaquetar los vértices expandidos PS2 (B5: V+VN+VT por triángulo) al
formato de vertex/index buffers HD. Es un conversor de geometría real.

## 16. COMPONENTES RESTANTES PARA EL CONVERSOR

- [x] Mapear el vertex buffer HD (layout: stride 0x2C, Z primero, VN reordenado/negado Y, VT al final)
- [x] Mapear el index buffer HD (triangle list 5140 u16 sin restarts + buffer de restart FFFF)
- [x] Entender cómo cada mesh part HD referencia su geometría (mesh-ref blocks 0x50B)
- [ ] Mapear el formato de textura #AZT (vs #AMT)
- [ ] Escribir el conversor (endianness + renombrado + re-layout + re-empaquetado geométrico)
- [ ] Validar con Krillin (convertir GH→HD y comparar render)

## 17. CONCLUSIÓN FINAL DE LA GEOMETRÍA (IMPORTANTE)

**La geometría HD NO es una conversión de la PS2 — es una re-topología densa.**

Evidencias (Krillin bin 327):
- Las posiciones X/Y del PS2 (1.0988, -4.9691) NO existen en el HD.
- Los Z coinciden SOLO parcialmente (~17/56 vértices del part0).
- El AWG principal HD tiene ~2190 vértices; el PS2 tiene ~42 mesh parts B5 en todo
  el modelo con menos densidad.
- Los valores compartidos (Z=0.49, -0.472, VN=0.7080/0.3271/0.6259, VT=0.4455/0.6194)
  son los de las esquinas/siluetas que se conservaron.

**Implicación**: los modelos HD fueron re-topologizados (más densos, con
normales/UVs recomputadas). No existe un conversor trivial PS2→HD que preserve
la geometría porque el HD usa SU PROPIA geometría.

**Consecuencia estratégica**: para añadir personajes IW (Janemba, Pikkon, etc.),
el conversor tendría que:
1. Re-topologizar/refinar la geometría PS2 al estilo HD, O
2. Usar la geometría PS2 tal cual dentro de un AWO (si el runtime HD acepta
   vértices con formato B5 LE→BE sin re-topología), O
3. Investigar si existe otra versión HD del modelo en los archivos (el personaje
   ya existe en HD con otra numeración, ej. Krillin en bin 327 vs el de IW).

**NOTA**: aunque la geometría difiere, la ESTRUCTURA (huesos, jerarquía, mesh
groups, mesh-ref blocks, formato de vértices) es idéntica y está mapeada. Esto
permite construir el AWO HD con geometría PS2 si el runtime lo acepta.

## Archivos de trabajo
- `awo_tools/parse_model.py` — parser #AMB → #AMO0/#AMG o #AWO/#AWG
- `awo_tools/analyze_awg.py` — análisis de un #AWG
- `awo_tools/analyze_mesh.py` — mesh parts dentro de un AWG
- `awo_tools/trace_bone.py` — trazado jerárquico de huesos (PS2 vs HD)

## 18. LAYOUT DEL VÉRTICE HD (descifrado)

El vértice HD (stride 0x2C = 44B) tiene atributos en 0x3480:
`
+00: V.z
+04: weight/material ?  (varía por vértice)
+08: weight/material ?
+0C: weight/material ?  (0.5, 0.6, 0.4...)
+10: 0
+14: VN.z
+18: -VN.y   (Y negada)
+1C: VN.x
+20: nan
+24: VT.u
+28: VT.v
`
El PS2 vert1 (V=(1.2942,-4.9691,0.002), VN=(0.9849,0.1730,-0.0063), VT=(0.4457,0.6853)):
`
HD: +00=0.002(V.z) +14=-0.0063(VN.z) +18=-0.1730(-VN.y) +1C=0.9849(VN.x) +24=0.4457(U) +28=0.6853(V)
`
Los campos VN y VT coinciden EXACTAMENTE (con Y negada). Las posiciones X/Y del
PS2 NO existen en el HD como floats — el HD usa posiciones transformadas al
espacio local del hueso (skinning), almacenadas en un vertex buffer de posiciones
separado (referenciado por AWG+0x2C).

## 19. CONCLUSIÓN DEFINITIVA — CONVERSOR COMPLETO REQUERIDO

**El formato HD y el PS2 son estructuralmente equivalentes pero geométricamente
distintos.** Para convertir un modelo PS2→HD se necesita:
1. Parsear el AMO0 PS2 (huesos, jerarquía, mesh parts, vértices absolutos)
2. **Transformar cada vértice absoluto PS2 al espacio local de su hueso**
   (usando la matriz de skinning del esqueleto PS2)
3. Reorganizar al layout HD: posiciones (buffer separado) + atributos
   (Z, VN negado-Y, VT) + weights
4. Reconstruir el index buffer (triangle list con restart FFFF)
5. Re-mapear cabeceras y punteros (AWO/AWG, mesh-ref blocks)

Es viable pero es un conversor de geometría 3D completo. No hay atajo de
byte-swap posible.

**ALTERNATIVA MÁS RÁPIDA (a validar)**: el runtime del recomp podría aceptar
vértices B5 sin re-topología si el AWO se construye con la geometría PS2
directamente (LE→BE sin transformación), reusando la estructura del AWO HD.
Esto produciría un modelo con la geometría PS2 (menos densa) pero funcional.
Requiere probar empíricamente cargando el resultado en el juego.

## 20. CONVERSOR IMPLEMENTADO (build_awo.py)

Se escribió `awo_tools/build_awo.py` que convierte un #AMO0 PS2 a #AWO HD:
- Extrae geometría PS2 (extract_geometry.py): 43 mesh parts, 9715 vértices (Krillin)
- Convierte cada vértice al layout HD (stride 0x2C): V.z + weights + VN(z,-y,x) + VT
- Construye el AWO: header + tabla relaciones + tabla offsets AMG + labels + AWGs
- Envuelve en #AMB y comprime con xbcompress /N:32

**Validación en curso**: mod `krillin_test` reemplaza el bin 327 (Krillin) en
`out\build\win-amd64-release\mods\krillin_test\us\data_cmn.afs`. El juego
arranca estable (15s sin crash). Pendiente prueba visual del render de Krillin.

**Resultados posibles de la prueba**:
- Krillin deformado/invisible sin crash → estructura OK, falta transformación
  de vértices al espacio local del hueso (skinning)
- Krillin normal → conversor funciona, aplicar a Janemba/Pikkon
- Crash al cargar → ajustar formato (pesos de skinning, mesh-ref blocks)

**Siguientes pasos según resultado**:
1. Si estructura OK pero geometría mal → implementar la transformación de
   skinning (matriz del hueso PS2 → espacio local)
2. Si funciona → convertir Janemba (bin 541) y añadir al roster
3. Textura #AZT pendiente de mapear

## 21. RESULTADO DE LA VALIDACIÓN: CRASH al seleccionar Krillin

El primer conversor (build_awo.py, estructura simplificada) **crashó** al
seleccionar Krillin. Diagnóstico:

**Causa**: mi AWG generado era una simplificación sin la estructura completa
que el runtime espera. Comparación Mi-AWG vs AWG-real:
| Campo | Mi AWG0 | AWG real |
|-------|---------|----------|
| +0x14 axes_loc | 0x40 | 0xA10 |
| +0x18 axis_lines | 1 | 0xD (13) |
| +0x28 mesh_group | 0x40 | 0x2700 |
| +0x2C vertex buffer | 0 | 0x17868 |
| +0x30 index buffer | 0 | 0x19F68 |
| +0x38 restart | 0x310C0 | 0x1C790 |

El runtime sigue la cadena `eje → armature → mesh group → mesh-ref blocks →
geometría`. Mi AWG no tenía ejes (80B) ni la cadena completa → crash.

**Estructura real del AWG0 HD** (Krillin):
```
0x00: header 0x40
0x40: labels (bone x 32 = 0x660) -> termina 0x6A0
0x6A0: continua labels + secciones de mesh groups
0xA10: ejes (80B cada uno)
0x2700: mesh group de XKLL_L00_FACE (cara) - sub-grupo
0x17868: vertex buffer (atributos stride 0x2C)
0x19F68: index buffer (triangle list)
0x2826: datos intermedios
0x1C790: restart buffer (FFFF)
```

**Datos de skinning HD**: los campos +04..+0C del vértice (stride 0x2C) son
weights de skinning que NO derivan del vértice PS2 (V+VN+VT). El PS2 guarda
los pesos en el rig data (weight groups con coords locales al hueso + offset
al vértice del mesh).

**Rig PS2**: 35 huesos tienen rig data. Cada weight group: weight (float),
vvn_am, vvn_loc (entradas de 32B: coords locales + offset al vértice).
Los coords del rig YA son posiciones locales al hueso (listas para HD).
Ej bone1: weight=1.0, vvn_am=52, coords=(1.338, 1.222, 0.375)...

**Conclusión**: el conversor completo requiere:
1. Reconstruir la estructura AWG completa (ejes, armatures, mesh groups, mesh-ref)
2. Mapear cada vértice PS2 a su hueso via el rig data (offset → mesh part)
3. Usar los coords del rig (locales al hueso) como posiciones HD
4. Poner los weights correctos en el vértice HD
5. Reconstruir index buffer

Es un trabajo de ingeniería inversa sustancial (más sesiones). La estructura
completa del AWG y el mapeo rig→vértice están documentados arriba.

## 22. MAPEO ADICIONAL DE LA ESTRUCTURA DEL AWG (comparando Krillin/Cell)

**Header AWG (0x40)**: layout confirmado determinista comparando Krillin y Cell:
```
+0x10: bone_am (51 vs 54) — varía por personaje
+0x14: axes_loc — varía
+0x18: axis_lines = 0xD (13) — CONSTANTE
+0x1C: label_loc = 0x40 — CONSTANTE
+0x20: offset tras labels (0x6A0 vs 0x700) — inicia mesh part headers
+0x24: nº mesh parts (11 vs 14) — varía
+0x28: mesh group data (materiales)
+0x2C: vertex buffer
+0x30: index buffer
+0x34: normales/datos extra
+0x38: restart buffer
+0x3C: 0x24 vs 0x1E (size de algo)
```

**Mesh part headers HD** (stride 0x50, en +0x20):
```
+0x00: 1.0 | +0x04: 1.0 | +0x08: 0
+0x0C: índice del part (0, 0, 0, 2, 4, 5, 7...)
+0x10: 0x44 (68)
+... (resto del header)
```

**Materiales** (mesh group data en +0x28): bloques de ~0x40/0x44 con floats
de color/especular + 0xFFFFFFFF (sin textura).

**sec3C (rel 0x24)**: tabla de offsets del AWG que repite los valores del
header: [count, 0x2700, 0x17868, 0x19F68, 0x2826, 0x1C790, 0x24, "XKLL_BODY"...]

**Estructura del AWG0 (Krillin)**:
```
0xDC0: labels (51×32=0x660) → termina 0x1420
0x1420 (+0x20): mesh part headers (11 × 0x50 = 0x370) → termina 0x1790
0x1790: ejes (80B c/u)
0x3480 (+0x28): mesh group data (materiales)
0x185E8 (+0x2C): vertex buffer (atributos stride 0x2C)
0x1ACE8 (+0x30): index buffer (triangle list)
0x1D510 (+0x38): restart buffer (FFFF)
```

**Pendiente**: cómo cada mesh part header HD referencia su rango de índices en
el ib (la conexión material↔geometría). Este es el último enigma del AWG.

## 23. MESH PART HEADER HD (descifrado)

El mesh part header HD (stride 0x50, en +0x20 del AWG) para Krillin:
```
+0x00..0x1C: 1.0, 1.0, 1.0, idx, 1.0, 1.0, 1.0, 0  (idx = índice del part)
+0x20: 0
+0x24: 5  (conteo de atributos?)
+0x28: 0
+0x2C: 5  (conteo de atributos?)
+0x30: 0
+0x34: tex index (1)
+0x38: type1 (0x01B5 = B5)
+0x3C: type2 (0x29BD)
+0x40: 0x44 (68)
+0x44: 0x44 (68)
+0x48: 0
+0x4C: 0
```
Los 11 parts tienen +0x24=+0x2C=5 constante. El mesh group data (+0x28)
tiene conteos 0x1D=29 repetidos (probablemente índices por part).

**ENIGMA PENDIENTE**: la conexión exacta entre los mesh part headers HD, los
materiales del mesh group data, y los rangos de índices en el index buffer.
Resolver esto requiere trazar cómo el runtime asigna índices a cada part.

## 24. RESUMEN DEL ESTADO DE LA RE (2026-08-13)

**MAPEADO y DOCUMENTADO**:
- Layout del AWO (header + relaciones + tabla AMG + labels + AWGs) ✓
- Layout del AWG (header 0x40 + labels + mesh part headers + ejes + mesh group
  data + vb + ib + restart) ✓
- Mesh part headers HD (stride 0x50) ✓
- Vértice HD (stride 0x2C): V.z + weights + VN(z,-y,x) + VT ✓
- Vértice PS2 (B5, 48B): V+VN+VT ✓
- Rig PS2: coords locales por hueso + weights + offset al vértice ✓
- El esqueleto PS2/HD es idéntico (51 huesos, 68 labels) ✓
- La geometría HD es re-topologizada (más densa) — NO 1:1 ✓
- **Formato de textura #AZT (360) — RESUELTO vía A3T Analyzer** ✓

**PENDIENTE**:
- Conexión material↔rango de índices en el AWG (el enigma final)
- Mapeo completo rig PS2 → vértices (offset→part→vértice)
- Conversor completo funcional (build_awo.py necesita la estructura AWG completa)
- Validación visual en el juego

## 25. FORMATO DE TEXTURA #AZT (360) — RESUELTO

El `mod center\A3T Analyzer\A3T Analyzer.py` (Nexus-sama) analiza el formato
de textura 360. Verificado sobre el #AZT de Krillin (bin 327 HD):
```
+0x10: tex_am (14 texturas)
+0x14: index_loc (0x20)
+0x20: tabla de offsets de texturas (14 × 4B)
Cada textura (en su offset):
  +0x00: 0
  +0x04: tipo (00 00 00 21=[T] comprimida, 80 00 00 01=[S] simple, 00 00 00 01=[B])
  +0x08: 01 00 01 00
  +0x0C: width, height (u16 BE c/u)
  +0x14: data_offset (u32 BE)
  +0x24: 00 00 83 20
  ...
  Palette en data_offset, Bitmap en data_offset+128
```
Las 14 texturas de Krillin HD: 256×256, 64×64, 128×128... (compuestas y simples).

**Conversión #AMT (PS2) → #AZT (HD)**: casi 1:1 endianness. Comparando
textura 1: +0x04 tipo SAME, +0x0C dims SAME, +0x14 data_offset SAME.
Diferencias menores en bitmap_offset (+0x18: 0x8000 vs 0x10080).
El HD usa más bytes (391KB vs 206KB) — compresión distinta (DXT).

## 26. HALLAZGO: EL INDEX BUFFER HD ES UN TRIANGLE STRIP CONTINUO

El IB del AWG0 (0x1ACE8, 5140 u16) **no tiene restarts FFFF internos** — es un
**triangle strip continuo** (5138 triángulos) que dibuja todo el cuerpo.

Los mesh part headers HD (stride 0x50) tienen:
```
+0x30: índice de part (0, 0, 2, 4, 5, 7, 8, 9, 10, 11, 12)
+0x34: textura (1, 3, 3, 1, 6, 6, 3, 1, 1, 0xFFFFFFFF, 6)
+0x38: type1 (0x01B5) | +0x3C: type2 (0x29BD)
```
El índice no es secuencial (0,0,2,4,5,7...) — los parts se dibujan en orden
por ese índice, cada uno con un rango del strip.

**ENIGMA PENDIENTE**: dónde están los límites (inicio+longitud) del rango del
strip para cada mesh part. Los campos +0x24/+0x2C=5 no son los tamaños.

## 27. HALLAZGO DEFINITIVO: EL VÉRTICE HD ES EL PS2 REORDENADO (VALIDADO)

Verificación exacta por VN+VT: el vert0 HD (@0x3480) = **part0 vert0 PS2**
`pos=(1.099, -4.969, 0.490)`. Layout del vértice HD (stride 0x2C=44B):
```
+00: V.z (0.4898) — coincide con PS2 vert0 Z
+04: 0.0752 (pos local X del hueso?)
+08: 0.3621 (pos local Y del hueso?)
+0C: 0.5000
+10: 0x1D=29 (índice de hueso/weight group)
+14: VN.z (0.7080) — coincide
+18: -VN.y (-0.3271) — coincide (Y negada)
+1C: VN.x (0.6259) — coincide
+20: FFFFFFFF
+24: U (0.4455) — coincide
+28: V (0.6194) — coincide
```

**Conclusión**: el HD usa EXACTAMENTE los mismos vértices PS2 (VN+VT idénticos,
posiciones Z idénticas), pero en un layout reordenado con datos de skinning
adicionales (+04/+08/+0C/+10). La geometría NO es re-topologizada desde cero —
es la misma malla con campos extra.

**Implicación para el conversor**: los vértices PS2 → HD es una transformación
determinista por vértice (reordenar campos + añadir skinning). El desafío es
obtener los datos de skinning (+04/+08/+0C/+10) que el HD tiene por vértice.
Estos provienen del rig PS2 (coords locales por hueso + weights).

**PENDIENTE**: descifrar exactamente qué son +04/+08/+0C (pos local?) y +10
(índice de hueso/weight) y cómo derivarlos del rig PS2 para cada vértice.

## 28. REFUTACIÓN DEL VERTEX MATCH (2026-08-13, tarde)

Test exhaustivo de 5 vértices HD (0x3480) contra el PS2 completo:
- **NINGUNO coincide** por VN+VT+UV exacto.
- Solo el vert0 coincidía antes (part0 vert0 PS2) — era una coincidencia de
  esquina compartida (los Z y VN eran de un vértice frontera que existe en ambos).

**Conclusión final**: la geometría HD es **re-topologizada** (vértices en
distinto orden, con duplicados por esquina, y campos de skinning extra).
No existe una correspondencia 1:1 vértice→vértice entre PS2 y HD.

**Decisión estratégica**: la conversión de geometría PS2→HD por este camino
requiere re-modelar/re-topologizar, lo cual es inviable automáticamente.
El conversor PS2→HD **no es factible** para producir geometría HD nativa.

## 29. RUTAS VIABLES ALTERNATIVAS (a evaluar)

1. **Usar el AWO HD existente como plantilla COMPLETA** y reemplazar solo las
   texturas (#AMT→#AZT, resuelto) + colores/materiales, manteniendo la geometría
   HD nativa. Sirve para: mods de color/costume, no para personajes nuevos.

2. **Usar los modelos SDBH WM (EMD/Xenoverse)** con el ecosistema
   EmdFbx/FbxEmd + EMD-to-AMG: convertir modelos Xenoverse a AMG PS2, luego
   re-empaquetar a AWO HD. Los EMD ya tienen skinning moderno (viable).

3. **Inyectar el modelo PS2 completo como AWO con geometría PS2** pero
   construyendo la estructura AWG completa (ejes + mesh groups + mesh-ref) —
   el runtime renderizaría la geometría PS2 (menos densa) dentro de la
   estructura HD. Riesgo: crash si la estructura no es exacta.

4. **Personajes ya existentes en HD**: para Janemba/Pikkon/etc (que NO existen
   en HD), usar el modelo IW directamente si el runtime acepta un AWO con la
   geometría PS2 sin re-topología (requiere validar la estructura AWG completa).

**Recomendación**: la ruta 3 es la más prometedora para añadir personajes IW.
Requiere reconstruir la estructura AWG completa (ejes + mesh groups anidados +
mesh-ref blocks), que es el enigma pendiente. La geometría PS2 se usa tal cual
(convertida a BE), el runtime debería aceptarla aunque sea menos densa.

## 30. ESTRUCTURA DE LA CADENA DE MESH PARTS (descubierta)

El AWG0 tiene **13 ejes**:
- **eje0** (sello `60 00 02 0F`): mesh principal (XKLL_BODY) con mesh group
- **eje1-12** (sellos `90 00 02 0C` / `80 00 02 0C`): huesos de rig (sin mesh)

**Mesh group del hueso0** (@0x2D00 = awg_hdr + 0x1F80 del armature):
```
+0x00: count (13)
+0x28: tabla de mesh parts (0x1158 rel AWG → 0x1ED8)
+0x2C: 0x2C (stride vértice 44)
+0x30: 5 | +0x3C: 0x2A | +0x44: 0x48
+0x48: nombre del hueso ("XKLL_BODY")
```

**Mesh-ref blocks** (13 × 0x50 en 0x1ED8): cada uno
```
+0x18: sello (0x9000020C mesh / 0x8000020C rig / 0x204 shadow)
+0x1C: arm (ptr a huesos: 0x294C = [0x17,0,0,0, 0,0x18,0,0,0, 0,0x19,0x24E0,0,0x1E80,0,0x1A])
+0x20: dat (ptr a material/normales: 0x1F10 = [floats, 0x8000020C, ptr_siguiente])
+0x28: tr (ptr a transformación)
```

**Cadena recursiva**: el `dat` de cada part contiene en +0x30 un sello
`0x8000020C` seguido del `arm` y `dat` del SIGUIENTE part. El runtime dibuja
part0 (material de dat, huesos de arm), luego sigue la cadena.

**ENIGMA PENDIENTE**: el conteo exacto de índices/triángulos que dibuja cada
part del strip. No está en los mesh-ref blocks ni en el arm. Probablemente
deriva de los datos del `dat` (materiales) o está implícito en la cadena.

## 31. CONVERSOR v2 (build_awo_v2.py) — plantilla AWG real completa

Corrige el crash del v1: ahora usa el AWO HD como **plantilla binaria completa**
(preserva los 13 ejes + mesh group + mesh-ref blocks encadenados + materiales)
y reemplaza SOLO el vertex buffer e index buffer del AWG0 con la geometría PS2.

**Correcciones clave**:
- El magic `#AWG` está en `tabla_AMG[0]` (0xD40), los labels en +0x40
- Los offsets +0x2C/+0x30/+0x38 son **relativos al inicio del AWG**
- El nuevo AWO = header+tablas+labels (0 a awg0_off) + AWG0 nuevo + AWGs 1-17

**Estado de validación**: AWO v2 generado (470KB), estructura verificada
(magic #AWG correcto, VB con V.z del PS2, IB triangle list, labels XKLL_BODY).
AFS reconstruido, mod `krillin_test` activo, juego arranca estable 15s.

**PENDIENTE**: prueba visual (seleccionar Krillin en batalla). Posibles:
- Deformado/invisible sin crash → estructura OK, falta skinning (posiciones
  locales por hueso vs absolutas PS2)
- Normal → conversor listo para Janemba
- Crash → ajustar (los mesh-ref blocks dibujan rangos del strip viejo, puede
  no coincidir con el IB nuevo)

## 32. CONVERSOR v3 (build_awo_v3.py) — tamaños fijos (CORRIGE EL CRASH)

**Causa del crash v2**: el runtime lee el VB/IB con los TAMAÑOS originales del
HD (VB=9984B, IB=10280B). Al reemplazar con tamaños distintos (VB PS2=190KB),
los punteros internos del AWG (relativos al header AWG) quedaban inválidos →
`0xC0000005` (acceso a memoria inválido) al procesar el modelo.

**Solución v3**: mantener los tamaños originales:
- VB: rellena los 9984B con vértices PS2 (los que quepan) + padding
- IB: rellena los 10280B con índices PS2 + 0xFFFF (restart) al final
- Los offsets del header AWG NO cambian → los mesh-ref blocks siguen válidos

**Estado**: AWO v3 generado (294KB, solo +3.4KB vs original). Offsets
verificados idénticos al HD (vb=0x17868, ib=0x19F68, restart=0x1C790).
AFS reconstruido, mod krillin_test activo. Juego arranca estable 20s sin
excepciones en el log.

**PENDIENTE**: validación visual (seleccionar Krillin en batalla).
NOTA: como el VB PS2 (4331 vértices) no cabe en 9984B (solo caben ~226
vértices), el modelo resultante mostrará SOLO los primeros ~226 vértices del
PS2 (geométricamente incompleto). Esto valida la estructura; para el modelo
completo se necesita re-layout del AWG (mover vb/ib y actualizar punteros).

## 34. CAUSA RAÍZ DEL CRASH ENCONTRADA (v4)

**Layout completo del AWO** (descifrado):
```
0x0000: header (0x30)
0x0030: tabla relaciones (bone × 32B) → 0x690
0x0690: tabla AMG (amg × 4B) → 0x6D8
0x06D8: labels (bone × 32B) → 0xD40
0x0D40: AWG0 ... AWG17 (18 bloques)
0x42360: axes-array (referenciado por +0x34 del header)
0x46FE0: fin del AWO
```

**El bug del v2/v3**: el AWO header `+0x34 = 0x42360` es el offset del
**axes-array** (que está al FINAL del AWO, 51×24=1224 punteros a ejes). Mis
conversores v2/v3 agrandaban el AWG0 (geometría PS2), desplazando el axes-array,
pero NO actualizaban el puntero `+0x34`. El guest seguía `+0x34` y leía vértices
PS2 en lugar de punteros de ejes → crash 0xC0000005.

**Solución v4**: mantener el AWG0 con el MISMO tamaño total (reemplazar vb/ib
dentro de su espacio original), de modo que NADA se desplaza y todos los
punteros (+0x34 axes-array, tabla AMG, mesh-ref blocks) quedan intactos.

**Estado**: v4 genera un AWO del mismo tamaño (290784 bytes), verificado con
assert. Juego arranca estable. PENDIENTE validación visual (seleccionar Krillin).

NOTA: como el VB solo caben 226 vértices (de 4331 del PS2), el modelo mostrará
geometría muy incompleta (74 triángulos). Esto valida la estructura; para el
modelo completo se necesita re-layout real (agrandar AWG0 + actualizar +0x34).

## 35. CAUSA RAÍZ #2: LA ESTRUCTURA DEL MOD ES INCORRECTA

**El runtime NO reemplaza el AFS completo.** Leyendo el código del SDK
(`rexglue-sdk/src/filesystem/afs.cpp` y `host_path_file.cpp`):

- `AfsFindModOverride` busca el override en `mods/<mod>/us/<afs_filename>/<entry_index>`
  (un **archivo suelto** con el nombre del índice, NO un archivo .afs completo).
- `AfsFindEntry` usa el índice del AFS **original** (cacheado) para mapear
  byte_offset → entrada.
- `host_path_file.cpp` lee del mod en `byte_offset - entry_start`.

**Mi error**: puse `mods/krillin_test/us/data_cmn.afs` (archivo completo de
293MB). El runtime espera `mods/krillin_test/us/data_cmn.afs/327` (archivo
suelto llamado "327").

**CORRECCIÓN**: la estructura correcta del mod es:
```
mods/<mod>/us/data_cmn.afs/327   <- el bin comprimido LZX (suelto)
```

Esto simplifica todo: NO hay que reconstruir el AFS completo. Solo poner el
bin comprimido como archivo suelto. (El sistema de "reconstrucción AFS" que
usábamos antes era para el mod janemba con archivo completo — era un enfoque
equivocado que se arrastró desde sesiones anteriores.)

**Límite de tamaño**: el guest lee el bin con el tamaño del AFS original
(105296B). El mod puede ser más corto (el runtime devuelve EOF al final, OK)
pero si es más largo se trunca.

## 36. CAUSA RAÍZ #3 (DEFINITIVA): FALTABA LA TEXTURA #AZT

**El bin 327 original descomprime a 682528 bytes con DOS entradas**:
```
[0] #AWO @0x40 size=290784  (modelo)
[1] #AZT @0x47020 size=391680  (textura)
```

Mi conversor v4 solo generaba el AWO (290848 bytes) sin la textura. El guest
carga el AWO y luego busca la #AZT (entrada 1 del AMB) que no existía → crash.

**Solución**: incluir la textura #AZT del original (copiada tal cual) en el AMB
generado. El bin final tiene 682528 bytes descomprimidos (igual que el original)
con 2 entradas: AWO (geometría PS2) + AZT (textura original).

**Verificación**: el header LZX del bin comprimido ahora dice `0xA6A20 = 682528`
(el tamaño descomprimido correcto). Juego arranca estable 30s.

**Resumen de TODAS las causas raíz corregidas**:
1. Estructura del mod: archivo suelto `mods/<mod>/us/data_cmn.afs/327`, NO AFS completo
2. Puntero +0x34 (axes-array) al final del AWO: v4 mantiene tamaño AWG0 fijo
3. Tamaño del VB/IB: índices limitados a los que caben en el VB
4. **Textura #AZT faltante**: el AMB debe tener 2 entradas (AWO + AZT)

## 37. HALLAZGO DECISIVO: DOS BUFFERS DE VÉRTICES + MAPA COMPLETO DEL AWG0

**Krillin tiene 3 bins de modelo** (no 1): 327 (51 huesos), 328 (47 huesos),
329 (50 huesos) — son los 3 trajes/costumes. Cada uno es un #AMB con AWO+AZT.

**Mapa completo del AWG0** (bin 327), offsets relativos al AWG:
```
+0x20 (0x6A0):  mesh part headers (880B)
+0x14 (0xA10):  ejes (7408B, 80B c/u)
+0x28 (0x2700): 294B (pequeño)
+0x34 (0x2826): VÉRTICES PRINCIPALES (86082B = 1956 verts stride 44)
+0x2C (0x17868): VÉRTICES SECUNDARIOS (9984B = 226 verts)
+0x30 (0x19F68): INDEX BUFFER (10280B = 5140 índices, max 2189)
+0x38 (0x1C790): 144B (restart)
```

**El error de TODOS mis conversores**: reemplazaba la zona `+0x2C` (226 vértices
secundarios) pensando que era el VB principal. Pero el buffer principal de
vértices es `+0x34` (1956 vértices). El IB indexa 1956+226 = 2189 vértices.

**Búsqueda en el ecosistema (exhaustiva)**: NO existe ninguna herramienta,
script ni documentación del formato 360 (#AWO/#AWG/#AZT) en mod center,
modding resources ni modding resources update. Verificado: 1007 archivos con
#AMO0 (PS2), CERO con #AWO (360). La única fuente de verdad es la comparación
binaria PS2↔HD (este documento).

## 38. LAYOUT REAL DEL VÉRTICE HD (stride 44, alineado +2)

El buffer principal de vértices (sec34) está **desalineado 2 bytes** (empieza
en 0x3568, no 0x3566). Con alineación +2, el layout de cada vértice (44B):
```
+00: nan (flag/w)
+04: VT.v
+08: VT.u
+12: V.z
+16: pos.x (local al hueso)
+20: pos.y (local al hueso)
+24: peso/bone
+28: 0
+32: VN.z
+36: -VN.y
+40: VN.x
```

Verificado contra PS2 vert (V.z=0.4898, VT=(0.4455,0.6194)): los campos VT
y VN coinciden (con Y negada), igual que en el buffer secundario.

**Conclusión del layout**: el vértice HD contiene V.z + posiciones locales al
hueso (skinning) + VN (reordenadas, Y negada) + VT. El PS2 tiene V (absoluta)
+ VN + VT. La conversión requiere transformar V absoluta → local al hueso.

## 32. RE-LAYOUT DE BUFFERS (2026-08-14) — estructura del AWO y bugs

### 32.1 ESTRUCTURA REAL DEL AWO (correcciones de la RE)

**Header AWO** (@0x40 del AMB):
```
+0x10: bone_am (51)     +0x18: amg_am (18)    +0x1C: ptr tabla AMG (rel AWO)
+0x34: axes-base (0x42360)  <- 51 entradas de 0x20 con punteros a zona ejes
     +0x34, +0x54, +0x74, +0x94, ... (cada +0x20, hasta +0x674)
     Cada entrada: [ptr_axes, campo2, campo3, ..., bone_idx en +0x1C]
```

**Tabla AMG** (18 entradas): apunta a los magics `#AWG` de cada AWG.
- `AWG0 = 0xD40` (rel AWO). **Los offsets internos están EN el magic** (0xD40),
  NO en +0x40. Error previo: `AWG = awg0_off + 0x40` (crasheaba todo).

**AWG0** (magic en awg0_off): los offsets de secciones son **relativos al magic**:
```
+0x2C: vb2 (buffer secundario)     +0x30: ib (index buffer)
+0x34: sec34 (buffer principal)    +0x38: restart
```

**Ejes (axes)**: `+0x14` del AWG = axes_loc (0xA10), `+0x18` = axis_lines (13).
Eje0 tiene el mesh principal (armature → mesh group). Ejes 1-12 = huesos rig.

### 32.2 PUNTEROS QUE HAY QUE ACTUALIZAR EN UN RE-LAYOUT

1. **Header AWG0**: +0x2C (si agranda sec34), +0x30 ib, +0x38 restart.
2. **Tabla AMG**: entradas 1-17 (+delta).
3. **51 punteros de zona ejes** del header AWO (≥ axes_base, +delta).
   - **¡CRÍTICO!** Excluir la tabla AMG de este loop (está en 0x690-0x6D8,
     dentro del rango 0x34-0x700). Los offsets de AWG16/17 (≥ axes_base)
     recibían delta 2× → null deref del guest.
4. **Header AMB**: entry0 size (AWO) + entry1 loc (AZT). ¡No duplicar el header!

### 32.3 MODO ARCHIVO COMPLETO (AFS reconstruido) — VALIDADO ✅

- El launcher copia `mods/<mod>/us/data_cmn.afs` al overlay `active_region`.
- AFS reconstruido (bin 327 ORIGINAL, tabla recalculada) **funciona**.
- Permite bins más grandes que el slot (override por entrada = límite 106496).

### 32.4 RESULTADO DEL RE-LAYOUT vb2 (+536 slots) — BLOQUEADO

- El modelo **carga** (antes crasheaba al seleccionar) ✅
- **Falta la mano derecha** + **crash al entrar en combate** ❌
- Rellenar padding con vértices válidos (copias) no lo resuelve.

**Datos**: IB usa 234 slots de vb2 (indices 1956-2189) pero vb2 real = 226
(9984/44). Con re-layout vb2 crece a 762 slots.

**Hipótesis**: el runtime puede usar el tamaño del buffer (ib - vb2) para el
conteo de vértices, y espera un conteo fijo. La mano derecha (índices
1956-2189) se desalinea con el buffer agrandado.

### 32.5 PRÓXIMOS PASOS

1. Instrumentar runtime (recompilar SDK — no hay cmake en PATH).
2. Verificar si el guest usa el tamaño del buffer para el conteo de vértices.
3. Alternativa: deduplicar/decimar geometría PS2 a ≤2190 vértices para
   mantener los buffers HD sin re-layout.
