# CONSOLIDACIÓN — CONVERSOR #AMO0 → #AWO (estado completo)

> Documento final que consolida TODA la ingeniería inversa del formato de
> modelos de DBZ Budokai 3 HD Collection (Xbox 360) y el camino del conversor.
> Creado: 2026-08-13. Detalle completo en `awo_tools/RE_PROGRESO.md`.

---

## 1. RESUMEN DE TODO LO APRENDIDO

### 1.1 El formato HD (360) vs PS2

| Aspecto | PS2 (Budokai 3 GH / IW) | HD 360 (Budokai 3 HD Collection) |
|---------|------------------------|-----------------------------------|
| Endianness | little-endian | **big-endian** |
| Contenedor | `#AMB` | `#AMB` (2 entradas: AWO + AZT) |
| Modelo | `#AMO0` | `#AWO` |
| Mesh group | `#AMG` (bloques secuenciales) | `#AWG` (vía tabla de offsets) |
| Textura | `#AMT ` | `#AZT ` |
| Esqueleto | idéntico (51 huesos/68 labels Krillin) | idéntico |
| Geometría | vértices expandidos por triángulo | **re-topologizada + skinning por vértice** |

### 1.2 Datos verificados empíricamente

- **Krillin bin 327 es EL MISMO modelo** en PS2 GH y HD 360 (51 huesos, 18 AMG/AWG, 68 labels idénticos).
- La conversión es genérica: Cell 54/19, Goku 64/19 (PS2 = HD).
- Krillin tiene **3 bins de modelo** (327/328/329 = 3 trajes con 51/47/50 huesos).
- El formato de textura **#AZT está resuelto** (A3T Analyzer, `mod center`).
- **No existe herramienta/documentación del formato 360** en todo el ecosistema
  (1007 archivos #AMO0, 0 con #AWO).

## 2. INFRAESTRUCTURA DEL RECOMP (lo que NO estaba documentado)

### 2.1 Cómo funciona el sistema de mods del runtime (CRÍTICO)

El runtime NO reemplaza el AFS completo. Usa **override por entrada**:
```
mods/<mod>/us/<afs_filename>/<entry_index>   <- archivo suelto con el bin LZX comprimido
```
- Código: `rexglue-sdk/src/filesystem/afs.cpp` (`AfsFindModOverride`) +
  `host_path_file.cpp` (`ReadSync`).
- El guest lee el bin con el **tamaño del AFS original** (del header). El mod
  puede ser más corto (EOF), pero no más largo (se trunca).
- El bin debe estar **comprimido LZX** (`xbcompress /N:32`, magic `0F F5 12 EE`).
- **IMPORTANTE**: el bin descomprimido debe tener las MISMAS entradas que el
  original (Krillin bin 327 = AWO + AZT). Si falta la textura → crash.

### 2.2 El launcher construye un overlay `active_region/`
- `src/launcher/settings.cpp` `PrepareRegionData`: copia/enlaza los archivos de
  `mods/<mod>/us/` al overlay `active_region/us/` (archivos completos).
- El runtime `game:` drive apunta al overlay.

## 3. ESTRUCTURA DEL AWO (completa)

### 3.1 Layout del AWO (offsets relativos al inicio del AWO)
```
0x0000: header (0x30)
0x0030: tabla de relaciones de huesos (bone × 32B) → 0x690
0x0690: tabla de offsets AMG (amg × 4B) → 0x6D8
0x06D8: labels de huesos (bone × 32B) → 0xD40
0x0D40: AWG0 ... AWG17 (18 bloques)
0x42360: axes-array (51×24=1224 punteros a ejes, referenciado por +0x34)
0x46FE0: fin del AWO
```
**Puntero +0x34 del header AWO = offset del axes-array (al final).**
Si se cambia el tamaño de cualquier sección anterior, hay que actualizar +0x34.

### 3.2 Mapa del AWG0 (offsets relativos al AWG, bin 327)
```
+0x20 (0x6A0):  mesh part headers (880B)
+0x14 (0xA10):  ejes (7408B, 80B c/u, 13 ejes)
+0x28 (0x2700): 294B (pequeño)
+0x34 (0x2826): VÉRTICES PRINCIPALES (86082B = 1956 verts, stride 44)
+0x2C (0x17868): VÉRTICES SECUNDARIOS (9984B = 226 verts, stride 44)
+0x30 (0x19F68): INDEX BUFFER (10280B = 5140 índices, max 2189)
+0x38 (0x1C790): 144B (restart)
```
El IB indexa 1956+226 = **2189 vértices** (dos buffers de vértices).

### 3.3 Layout del vértice HD (stride 44 = 0x2C, alineado +2)
```
+00: nan (flag/w)
+04: VT.v
+08: VT.u
+12: V.z
+16: pos.x (local al hueso / skinning)
+20: pos.y (local al hueso)
+24: peso/bone index
+28: 0
+32: VN.z
+36: -VN.y
+40: VN.x
```

## 4. EL RETO FUNDAMENTAL (por qué crasheaba)

**La geometría HD es re-topologizada + skinning por vértice.**

- El PS2 guarda vértices **absolutos** (V+VN+VT en 48B LE), expandidos por triángulo.
- El HD guarda vértices con **posiciones locales al hueso** (skinning), normales
  reordenadas (Y negada), UVs, y weights/bone index.
- No hay correspondencia 1:1 vértice→vértice (test exhaustivo: 0/5 coinciden).
- El HD usa **dos buffers de vértices** (principal + secundario) indexados por
  un IB compartido.

## 5. LAS 4 CAUSAS RAÍZ DEL CRASH (todas corregidas)

1. **Estructura del mod incorrecta**: se usaba un AFS completo reconstruido;
   el runtime espera archivo suelto `mods/<mod>/us/data_cmn.afs/327`.
2. **Puntero +0x34 (axes-array) no actualizado** al agrandar el AWG0.
3. **Índices del IB fuera del VB** (leían más allá del buffer).
4. **Textura #AZT faltante**: el AMB generado tenía solo AWO; el guest busca la
   textura y crashea. El bin descomprimido debe tener AWO + AZT (682528 bytes).

## 6. PRÓXIMO PASO: TRANSFORMACIÓN DE SKINNING

El conversor v4 (`build_awo_v4.py`) ya produce un bin con la estructura correcta
(AWO+AZT, mismo tamaño, índices válidos). El siguiente paso para que el modelo
PS2 se renderice es:

1. **Extraer la matriz de transformación de cada hueso** del esqueleto PS2
   (los ejes de 80B contienen la transformación).
2. **Transformar cada vértice PS2 (absoluto) al espacio local de su hueso**:
   `v_local = inv(bind_matrix_hueso) * v_absoluto`.
3. **Reordenar al layout HD** (stride 44): VT al inicio, V.z, pos local, VN
   (Y negada), weights.
4. **Poner el bone index y los weights** correctos en el vértice HD.
5. **Reconstruir el index buffer** del PS2 (triangle list → formato HD).

Esto haría que el modelo PS2 se renderice con skinning correcto dentro de la
estructura HD.

### 6.1 AVANCE: conversor v5 (build_awo_v5.py) — skinning extraído

El conversor v5 implementa la transformación de skinning:
- **Rig PS2 parseado**: 3056 entradas de skinning (hueso, weight, coords locales, offset al vértice).
- **Mapeo offset→vértice RESUELTO**: el offset del rig es absoluto al mesh group;
  cae en un part, y `(offset - inicio_vértices) % 48 == 0` da el índice de vértice.
  Verificado: offset 0x1FFD0 → part10, vértice 28 (0x540/48).
- **Vértices convertidos al layout HD**: [nan, VT.v, VT.u, V.z, pos_local, weight, 0, VN.z, -VN.y, VN.x].
- Resultado: 4331 vértices PS2 transformados listos para inyección.

**PENDIENTE (re-layout de buffers)**: la geometría HD usa 2189 vértices ÚNICOS
en 2 buffers (sec34 principal 1956 + sec2C secundario 226). El PS2 tiene 4331
vértices expandidos. Para inyectar:
1. Deduplicar vértices PS2 (por pos+normal+uv) → ~2189 únicos
2. Reconstruir el IB apuntando a los únicos
3. Re-layout de los buffers (+0x34 y +0x2C) en el AWG0 + actualizar punteros

### 6.2 CÓMO DIBUJA EL RUNTIME (descifrado — clave para el re-layout)

- El mesh group del hueso0 tiene `count=13` (13 mesh-ref blocks en +0x28).
- Cada mesh-ref block tiene un `arm` = lista de huesos consecutivos (0x17-0x24)
  con **offsets al IB** (ej. 0x24E0, 0x1E80).
- Los offsets del arm apuntan a **índices u16 en el IB** (verificado: 0x24E0 →
  [2057,2059,2058...]). El runtime dibuja esos rangos por part.
- El IB HD tiene 5140 índices sin restarts (strips/listas por hueso).
- El HD tiene ~1713 triángulos vs PS2 1443 → geometría más densa (re-topologizada).

**El re-layout completo (Opción A) es la única vía viable**: reconstruir los
buffers de vértices + IB + arms + mesh-ref blocks con la geometría PS2. Los
huesos difieren entre PS2 (1-48) y HD (23-36) en qué vértices cubren, así que
hay que agrupar los triángulos PS2 por hueso y reconstruir los arms.

**Test de validación pendiente**: mod `krillin_texture` (bin 327 con textura
#AZT alterada) para confirmar que el pipeline de mods + textura funciona
end-to-end. Si Krillin sale con color alterado → la infraestructura es correcta
y solo falta el re-layout de geometría.

## 7. CAUSA RAÍZ #5 (LA DEFINITIVA): EL PARÁMETRO DE COMPRESIÓN LZX

**TODOS los crashs (incluido el de solo-textura) eran por la compresión LZX.**

- El bin 327 original del AFS es **105296 bytes** comprimido.
- `xbcompress /N:32` (el parámetro que veníamos usando) produce **128236 bytes**
  → MÁS grande que el original.
- El guest lee el bin con el **tamaño de la tabla AFS original (105296)**.
  Al ser el mod más grande, se **trunca** → LZX incompleto → crash.
- Esto explica TODOS los crashs de TODAS las versiones (v1-v5 y el de textura).

**Solución**: `xbcompress /N:2048` (bloques de 2MB) produce **exactamente
105296 bytes** (igual al original), con round-trip perfecto.
- `/N:2048` → 105296 ✓
- `/N:1024` → 105296 ✓
- `/N:512` → 105650 ✗
- `/N:32` → 128204 ✗ (el parámetro incorrecto que usábamos)

**Comando correcto**:
```
xbcompress /N:2048 <src_descomprimido> <dst_comprimido>
```

## 8. CAUSA RAÍZ #6: EL GUEST LEE EL SLOT COMPLETO (106496 bytes)

**Instrumentación del runtime reveló el problema final**:
```
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=121968
```
- El guest lee **106496 bytes** (el tamaño del SLOT del bin 327 al 328, no el
  tamaño del bin 105296).
- El slot 327 va de 0x3AAE000 a 0x3AC8000 = 0x1A000 = **106496 bytes**.
- El bin 327 comprimido real es 105296 (+1200 de padding en el slot).

**Fix**: el mod debe tener **al menos 106496 bytes** (el slot). Se rellena el
LZX comprimido con padding al final hasta 106496 bytes. El round-trip es
perfecto (el decompresor ignora el padding).

**Comando**:
```
xbcompress /N:2048 bin.bin bin.lzx   # da 105296
# rellenar bin.lzx a 106496 bytes (slot) con bytes 0x00
```

Verificado: LZX padded a 106496 bytes → descomprime a 682528 exacto. Juego
arranca estable.

## 9. CAUSA RAÍZ #7 (LA QUE BLOQUEABA TODO): MÚLTIPLES MODS ACTIVOS

**Aunque el bin fuera correcto, el juego crasheaba** porque había MÚLTIPLES mods
habilitados a la vez (krillin_test, krillin_control, krillin_1byte, etc.).
El runtime (`afs.cpp` `ScanModDirs` + `std::sort`) busca el override en orden
**alfabético**, y el primero que tiene el archivo gana. `krillin_test` (con un
bin MALO de 121968 bytes) ganaba sobre `krillin_texture` (bin bueno).

**Solución**: desactivar todos los mods excepto el que se quiere probar
(marcador `.disabled` en la carpeta del mod).

## 10. ✅ VALIDADO END-TO-END (2026-08-13, tarde)

**El override por entrada FUNCIONA perfectamente.** Krillin carga sin crash
con el bin original recomprimido:
- `mod_size=106496` (bin padded al slot)
- `to_read=106496, got=106496` (el guest obtiene todos los bytes)
- 4 lecturas del bin 327 (previews de trajes + combate)
- Sin errores en el log

**REQUISITOS del bin mod (CRÍTICOS)**:
1. Contenido descomprimido: AWO + AZT (682528 bytes para bin 327)
2. Comprimir con `xbcompress /N:2048` (no /N:32)
3. **Rellenar a 106496 bytes** (el tamaño del SLOT que lee el guest)
4. Solo UN mod habilitado por bin (orden alfabético)

**PRÓXIMO PASO**: conversor v6 con la geometría PS2 convertida (skinning),
usando los mismos requisitos de bin (AWO+AZT, /N:2048, padded a slot).

## 11. ✅✅ HITO CONFIRMADO: MOD DE TEXTURA FUNCIONA END-TO-END

**CONFIRMADO VISUALMENTE** (2026-08-13): Krillin muestra **píxeles rojos en el
dogi/traje exterior** (zona del cuello) con el mod de textura.

### Formato de textura #AZT (360) — DDS DXT3

- La textura 1 del bin 327 es un **DDS DXT3** (256×256).
- Estructura: header DDS (128 bytes) + bitmap DXT3.
- El A3T Analyzer: `data_offset` apunta al header DDS, `data_offset+128` al bitmap.
- DXT3: bloques 4×4 = 16 bytes (8B alpha + 2B color0 RGB565 + 2B color1 RGB565 + 4B indices).
- **Para cambiar el color**: modificar los colores RGB565 de los bloques DXT
  (no los bytes "al azar" del bitmap — eso no produce cambio visible).
  `color0 = (R<<11)|(G<<5)|B`, little-endian.

### Pipeline de mod de textura validado
1. Descomprimir bin 327 → #AMB (AWO + AZT).
2. Editar los bloques DXT3 de la AZT (colores RGB565).
3. Recomprimir con `xbcompress /N:2048`.
4. Rellenar a 106496 bytes (slot).
5. Colocar en `mods/<mod>/us/data_cmn.afs/327` (único mod activo).

**Este hito demuestra que el sistema de mods funciona end-to-end** — la base
del modding hard (texturas, y próximamente geometría/personajes).

## 12. ✅ HALLAZGO: LA GEOMETRÍA ES MODIFICABLE (experimento de deformación)

**Confirmado visualmente** (2026-08-13): desplazando los vértices del buffer
principal (sec34) del bin 327 en X (+3.0), **Krillin apareció deforme**
(altísimo, cuerpo deformado), conservando la forma de la cabeza y los zapatos.

**Implicaciones**:
- El runtime **renderiza los cambios de geometría** (los floats de posición del
  buffer principal controlan la forma).
- No hace falta re-layout para modificar geometría — se puede alterar en sitio.
- La cabeza y los zapatos se conservaron → probablemente usan otros buffers
  (sec2C secundario) o otros AWG.
- El juego no crasheó; se pudo jugar (aunque la cámara apuntaba a la estatura
  original, y la intro se buggeó temporalmente).

**Layout del vértice HD (stride 44, buffer principal sec34, alineado +2)**:
```
+00: nan (flag)
+04: VT.v
+08: VT.u
+12: V.z
+16: pos.x (LOCAL al hueso — modificar aquí deforma el modelo)
+20: pos.y (local al hueso)
+24: peso/bone
+28: 0
+32: VN.z
+36: -VN.y
+40: VN.x
```

**NOTA**: las posiciones son LOCALES al hueso (skinning). Desplazar en X
uniformemente no es una traslación del modelo — deforma porque cada vértice
está en el espacio de su hueso.

### 12.1 BUFFER SECUNDARIO = CABEZA Y ZAPATOS (experimento 2)

Desplazando SOLO el buffer principal (sec34), cabeza y zapatos se conservaron
→ **cabeza/zapatos usan el buffer secundario (sec2C/vb)**, no sec34.

**Datos**:
- IB: 5140 índices, 234 únicos (1956-2189) apuntan a sec2C.
- sec34 (principal): 1956 vértices stride 44, `nan` en +0. Layout:
  `[nan, VT.v, VT.u, V.z, pos.x_local, pos.y_local, weight, 0, VN.z, -VN.y, VN.x]`
- sec2C (secundario): stride 44 pero `nan` en +28 — layout DIFERENTE.
  vert0: `[-0.1370, 0.0434, 1.0000, 0,0,0,0, nan, 0.1683, 0.0473, -0.1428]`

### 12.2 EXPERIMENTO 3: SKIN PS2 EN BUFFER PRINCIPAL (viabilidad del skinning)

**Resultado** (2026-08-13): reemplazando los 1956 vértices de sec34 con los
primeros 1956 vértices PS2 skinneados (rig 3056 entradas, coords locales):
- **NO crashea** → el runtime renderiza los datos PS2 skinneados.
- Krillin queda CORROMPIDO (solo frente + un puño reconocibles, con texturas
  intactas). El resto deformado. Textura roja del experimento 1 visible en el
  rostro → confirma que la textura se asigna por mesh part/material.

**Diagnóstico**: el orden de los vértices NO coincide. El buffer HD está
re-topologizado (orden específico de Krillin HD, 1713 triángulos) y los vértices
PS2 van en orden de mesh part PS2 (4331 expandidos, 1443 triángulos). Meterlos
slot por slot asigna cada posición PS2 a un hueso distinto del HD → corrupción.

**Implicación**: el skinning PS2 es VÁLIDO (datos aceptados por el runtime).
El problema es la TOPOLOGÍA (cómo indexa el IB). Para una conversión correcta
hace falta re-layout completo: dedup vértices PS2 → ~2190 únicos (cabrían en
sec34+sec2C = 2190 slots), reconstruir IB con triángulos PS2, y re-mapear
arms/mesh-ref. Esto es la Opción A del plan (re-layout), ahora desbloqueada por
el modo archivo completo (bins de cualquier tamaño).

**Dato útil**: PS2 Krillin = 19 mesh parts, 4331 vértices expandidos, 3792
únicos (por pos+nrm+uv), 2492 únicos solo-pos. HD = 2190 slots (1956+234).

## 13.5 RE-LAYOUT DE BUFFERS (2026-08-14): ESTADO Y BLOQUEOS

**OBJETIVO**: la geometría PS2 (2492 vértices únicos solo-pos / 3792 por
pos+nrm+uv) NO cabe en los 2190 slots HD (1956 sec34 + 234 sec2C). Para
convertir personajes se necesita **agrandar los buffers** (re-layout).

### 13.5.1 ESTRUCTURA REAL DEL AWO (correcciones críticas de la RE)

- **AWO header** @0x40: `+0x18` amg_am(18), `+0x1C` tabla AMG (rel AWO),
  `+0x34` axes-base.
- **Tabla AMG** apunta al **magic #AWG** (NO +0x40). Los offsets internos del
  AWG (+0x2C vb2, +0x30 ib, +0x34 sec34, +0x38 restart) están **en el magic**.
  - ANTES pensábamos `AWG = awg0_off + 0x40` (error que rompía todo).
  - CORRECTO: `AWG = awg0_off` (el valor de la tabla AMG apunta al magic).
- AWO header tiene **51 entradas de 0x20** (una por hueso) con punteros a la
  zona de ejes/axes (0x42360+): `+0x34, +0x54, +0x74, ...` cada +0x20.

### 13.5.2 MODO "ARCHIVO COMPLETO" (AFS reconstruido) VALIDADO ✅

- El launcher copia `mods/<mod>/us/<archivo>.afs` al overlay `active_region`.
- El **AFS completo reconstruido** (con bin 327 original, tabla recalculada)
  **funciona perfectamente** — Krillin carga y combate sin crash.
- Esto permite bins **más grandes que el slot** (el override por entrada está
  limitado al tamaño del slot 106496).

### 13.5.3 SCRIPT `build_big_amb.py` (re-layout de buffers)

Uso: `python build_big_amb.py <bin_amb> <sec34|vb2> <n_verts> <output>`
- `sec34`: agranda el buffer principal (vértices 0-1955).
- `vb2`: agranda el buffer secundario (cabeza/zapatos).
- Recalcula: header AWG offsets, tabla AMG (AWG1-17), 51 punteros de zona ejes.

### 13.5.4 BUGS ENCONTRADOS Y CORREGIDOS (causas del crash)

1. **`AWG = awg0_off + 0x40`** (leía los offsets del AWG en la posición
   equivocada) → corregido a `AWG = awg0_off`.
2. **Double-delta en tabla AMG**: el loop de "punteros a zona ejes"
   (`for j in 0x34..0x700`) barría TAMBIÉN la tabla AMG (0x690-0x6D8). Los
   offsets de AWG16/17 (≥ axes_base) recibían delta 2× → posiciones corruptas
   → null deref del guest. **FIX**: excluir la tabla AMG del loop.
3. **Header AMB duplicado** en el repack (corrompía los punteros del AWO).

### 13.5.5 RESULTADO DEL RE-LAYOUT (vb2 +536 slots, bin 1097792 bytes)

- **El modelo CARGA** (ya no crashea al seleccionar) — gran avance.
- **PROBLEMA**: falta la **mano derecha** del personaje.
- **PROBLEMA**: **crash al entrar en combate** (después de cargar).
- Rellenar el padding de vb2 con **vértices válidos** (copias cíclicas) en vez
  de ceros **NO resolvió** ni la mano ni el crash.

### 13.5.6 HIPÓTESIS PENDIENTES (no resueltas)

- La mano derecha usa índices del IB (1956-2189 → vb2). El IB usa **234 slots**
  de vb2 pero vb2 real solo tiene **226** (9984/44). Con el re-layout vb2 crece
  a 762 slots; el runtime puede desalinear los índices de la mano.
- El crash en combate: el guest procesa los vértices del buffer secundario con
  el nuevo tamaño; posible que espere un conteo fijo de vértices por buffer.
- Los mesh-ref blocks (0x1ED8, 13×0x50) y arms (offsets relativos al IB)
  quedaron válidos según la RE, pero el runtime puede usar el tamaño del buffer
  (ib - vb2) para algo que no hemos mapeado.

### 13.5.7 PRÓXIMOS PASOS

1. **EXPERIMENTO DECISIVO (2026-08-14)**: agrandar sec34 a solo **1957 vértices
   (+1)** con IB re-mapeado → **CRASHEA en combate**. El runtime espera que
   `sec34_count`/`vb2_count` sean EXACTAMENTE los originales (derivados de los
   offsets del AWG header). CUALQUIER cambio de tamaño rompe la renderización.
2. **CONCLUSIÓN**: el re-layout de buffers es INCOMPATIBLE con el runtime.
   El guest usa los conteos derivados de los offsets del AWG para validar los
   índices dibujados por los arms. Al cambiar los tamaños, los conteos no
   coinciden → crash null-deref en combate.
3. **ALTERNATIVA VIABLE**: mantener los buffers HD del MISMO tamaño y **decimar
   la geometría PS2 a ≤2190 vértices** (de 2493 únicos), reconstruyendo el IB
   (los 4329 índices PS2 caben en los 5140 del IB HD) y re-mapeando los arms.
   El experimento de skin PS2 (reemplazar datos de sec34 sin cambiar tamaño)
   NO crasheó → validado que el runtime acepta cambios de datos en sitio.

### 13.5.8 HALLAZGO CLAVE: sec34 SENSIBLE, vb2 TOLERANTE (2026-08-14)

**Experimento de control decisivo**:
- **vb2 +1 vértice** (sec34 intacto a 1956): **ENTRA EN COMBATE**. Hay lag,
  las texturas de Krillin parpadean, falta la mano derecha (a veces la
  izquierda), pero se puede pelear/interactuar/lanzar técnicas. **NO crashea**.
- **sec34 +1 vértice** (vb2 intacto, IB re-mapeado): **CRASHEA en combate**.

**Implicación**: el runtime es sensible al tamaño de sec34 (buffer principal)
pero tolerante a cambios en vb2 (buffer secundario). Esto aísla el
comportamiento:
- sec34_count se usa críticamente (posible validación/alojamiento en combate).
- vb2_count puede variar sin crash (solo desalineación de índices de la mano).

**Dato pendiente RESUELTO**: sec34+1 SIN re-mapear el IB también **CRASHEA**
(esta vez en la preview al seleccionar, Addr=0x7ff7f59c295c — el mismo punto
de parseo). Con re-mapeo llegaba a la preview pero crasheaba en combate.

**CONCLUSIÓN FINAL**: `sec34_count`/`vb2_count` son parámetros FIJOS que el
guest usa para deserializar TODA la estructura del modelo. Cambiarlos (aunque
sea +1) rompe el parseo → null deref. El guest deriva estos conteos de los
offsets del AWG header y los usa para localizar estructuras posteriores.

**Implicación definitiva**: NO se puede agrandar ningún buffer de un modelo HD
existente. Para añadir un personaje IW (Janemba 4415 verts, Pikkon 3643, Pan
4517, Super 17 4604, Super Baby 4967 — todos con más del doble de los 2190
slots de Krillin), hay que **construir el AWO HD completo desde cero** con los
conteos correctos del personaje (no reutilizar la estructura de Krillin).

### 13.5.9 INSTRUMENTACIÓN DEL RUNTIME (completada 2026-08-14)

- Se recompiló el SDK con ninja + clang (`rexglue-sdk/out/build-win-vulkan`):
  `ninja -C <build> rexruntime`
- Se agregó logging en `host_path_file.cpp` ReadSync:
  `AFS327 READ: off=... to_read=... entry_start=... entry_size=...`
- **DATOS CAPTURADOS** (bin 327 agrandado):
  ```
  AFS327 READ: off=0x3AAE000 to_read=131072 entry_start=0x3AAE000 entry_size=130752
  ```
  El guest lee el slot completo (131072), el bin LZX (130752) se sirve completo.
  El bin se lee correctamente; el crash es AL PROCESAR el modelo, no al leer.
- **Restart buffer** (0x38, 144 bytes): contiene índices u32 (FFFFFFFF, 1, 2, 3,
  ... 48) — índices de sec34, NO necesita re-mapeo (apuntan a 0-1955).
- El rexruntime.dll instrumentado se copió a `out/build/win-amd64-release/` y
  `rexglue/bin/`.

### 13.5.8 ARCHIVOS DE TRABAJO (re-layout)

| Archivo | Propósito |
|---------|-----------|
| `awo_tools/build_big_amb.py` | Re-layout buffers (sec34/vb2) + repack AMB |
| `awo_tools/relayout_awg.py` | Re-layout AWG0 (versión simple) |
| `%TEMP%\opencode\b327_vb2fix4.bin` | vb2 +536 slots con tabla AMG corregida |
| `%TEMP%\opencode\data_cmn_vb2fix4.afs` | AFS con bin vb2 agrandado |
| `%TEMP%\opencode\data_cmn_original_rebuilt.afs` | AFS control (bin original, funciona) |

### 13.5.10 ✅ HITO: JANEMBA ENTRA SIN CRASH (2026-08-14)

**Primer personaje IW inyectado en el recomp** — el bin 327 con la geometría
de Janemba (decimada a sec34=2386, IB=8484 índices) **CARGA sin crash**.

**Lo validado**:
- El runtime acepta un AWO con sec34=2386 (mayor que los 2277 del bin 329).
  Confirma que los conteos variables funcionan si la estructura es coherente.
- El pipeline completo funciona: extraer PS2 → skinning → layout HD →
  decimación (voxel cell=0.10) → reconstruir IB → empaquetar AMB → AFS.

**El problema (no crash)**: el modelo se ve como **"masa uniforme"** — no se
ve Janemba. Causa: los vértices de Janemba están en **coords locales a los
huesos de Janemba (JNB_*, 48 huesos)**, pero el slot de Krillin aplica las
**matrices de los huesos de Krillin (KLL_*, 51 huesos)** vía los arms del
mesh group. Esqueletos distintos → cada vértice se transforma mal → masa.

**Esto es el reto de re-rigging** (documentado en AGENTS.md): el experimento
de skin Krillin→Krillin funcionó (esqueletos idénticos); Janemba→Krillin no
(esqueletos difieren en cantidad y orientación de huesos).

**Herramientas generadas**: `awo_tools/convert_personaje.py` (skinning+layout
HD), `awo_tools/decimar.py` (voxel grid), `awo_tools/build_janemba2.py`
(construcción AMB con conteos propios).

### 13.5.11 SIGUIENTE BLOQUEADOR: MESH-REF BLOCKS + ARMS (2026-08-14)

**El modelo de Janemba entra sin crash pero se ve como masa deforme.** La causa
es que los **mesh-ref blocks + arms de Krillin** definen CÓMO dibuja el runtime
cada part (qué rango del IB, qué huesos, qué material). Al meter la geometría
de Janemba con un IB distinto, esos arms apuntan a rangos del IB que ya no
corresponden → cada part dibuja triángulos de Janemba en rangos equivocados.

**Estructura mapeada**:
- Mesh group @AWG+0x1F80: count=13, tabla mesh-ref @+0x28.
- Cada mesh-ref block (0x50): +0x18 sello (0x9000020C mesh / 0x8000020C rig /
  0x00000204), +0x1C ptr arm, +0x20 ptr idx (dat/material), +0x28 ptr tr.
- Arm: lista de huesos `[bone, 0, 0, 0]` (16B) + offsets del IB intercalados
  (0x24E0=4720, 0x2550=4776, 0x2620=4880, 0x2690=4936 — índices del IB).
- El runtime dibuja cada part como `[offset_previo, offset_bone)`. Los offsets
  del arm son índices del IB (byte offsets ÷ 2).
- Dat (material): floats + sello 0x8000020C en +0x30 + ptr al siguiente
  (cadena recursiva): +0x34 arm siguiente, +0x38 dat siguiente.

**Para que Janemba se vea**: reconstruir el mesh group completo (mesh-ref
blocks + arms + cadena dat) con la geometría de Janemba, agrupando sus
triángulos por material y asignando los huesos de Krillin (mapeo JNB→KLL).
Requiere: (1) transformar posiciones de Janemba al espacio local de los huesos
de Krillin (re-rigging por labels JNB_HEAD→KLL_HEAD, etc.), (2) construir el IB
agrupado por material, (3) re-mapear arms con los nuevos offsets del IB.

**Estado**: bloqueado en el re-rigging fino + reconstrucción de arms. Es un
trabajo de conversión completo que requiere más sesiones de RE.

### 13.5.12 HALLAZGO DE LA COMUNIDAD (2026-08-14) — EL TRASPASO LÓGICO

**Los modelos IW → B3 PS2 YA EXISTEN** (hechos por la comunidad):
- `modding resources\All Character Models from IW into AMB format\`
  → **241 modelos .amb** (#AMB PS2 LE: #AMO0 + #AMT). Janemba, Pikkon, Pan,
    Super 17, Super Baby, Gogeta, Vegito, todos los Freeza/Buu, etc.
- `Janemba.amb` = idéntico al bin 541 IW (48 huesos, 17 AMGs, 4415 únicos).
- Los movesets port IW→B3 ya existen (AGENTS.md).

**Herramientas de la comunidad** (`mod center\`):
- `AMO Decompiler.py` / `AMO Compiler.py` (Model Compiling Tools): pipeline
  #AMO0 PS2. Base del parseo que usamos.
- `B3_IW Model Converter` (amb_model.py): empaqueta/desempaqueta #AMB.
- `Model Rig Toolset V0.6`, `Model Merger Tool`, `Bone Addition Tool`,
  `OBJ to AMG / Bin to OBJ`, `EMD to AMG` (ecosistema Xenoverse).

**Dato clave del traspaso**:
- Krillin PS2: 3216 triángulos, 4252 posiciones únicas.
- Krillin HD: 1713 triángulos, 2182 slots (~50% de reducción).
- **El HD 360 decima a la mitad la geometría PS2.** El conversor debe replicar
  esto (reducir Janemba 4415 → ~2200 posiciones, 3141 → ~1700 triángulos).

**El bloqueador técnico** (por qué el modelo de Janemba sale deforme):
- Los mesh-ref blocks + arms de Krillin definen cómo dibuja el runtime cada
  part (offsets del IB, huesos, materiales).
- Al inyectar geometría de Janemba con un IB distinto, los arms apuntan a
  rangos equivocados → masa deforme.
- **build_janemba3.py** intenta reconstruir los arms, pero tiene un bug de
  offsets relativos: el arm_ptr (rel magic) no coincide con la posición en el
  AWG0 reconstruido (el mesh group se re-ubica).

**Próximo paso técnico correcto**: resolver el bug de offsets del arm_ptr en
build_janemba3 (el mesh group del AWG0 reconstruido no está en la misma
posición relativa que en Krillin; hay que recalcular arm_abs respecto al
mesh group del bin nuevo, no del original).

### 13.5.13 BUG DEL ARM_PTR RESUELTO (2026-08-14) — ZONA DE ARMS DECODIFICADA

**El bug no era de offsets relativos sino de dos errores conceptuales**:

1. **Los offsets del arm van en BYTES del IB, no en índices.** Krillin:
   `0x24E0` = 9440 bytes = 4720 índices (×2). El v3 escribía índices.
2. **La zona de arms es una lista contigua de bloques de 20 bytes**, cada
   uno `[bone_idx, offset_A_bytes, 0, offset_B_bytes, 0]`. Cada mesh-ref
   block apunta (arm_ptr rel magic) al INICIO de SU bloque. El v3 barría
   96 bytes desde cada arm_ptr, pisando los bloques vecinos de otros MR
   (por eso MR[1] sobrescribía el offset de MR[0] con 9423).

**Zona de arms de Krillin (14 bloques de 20 bytes, @0x1BCC-0x1CD0)**:
```
0x1BCC: [0x17, 0,0,0,0]        <- MR[0] mesh
0x1BE0: [0x18, 0,0,0,0]        <- MR[1] rig
0x1BF4: [0x19, 0x24E0,0,0x1E80,0]  <- MR[2] shadow  [3904,4720) idx
0x1C08: [0x1A, 0,0,0,0]        <- MR[3] rig
...      (mesh blocks sin offsets)
0x1C80: [0x20, 0x2550,0,0x1EC0,0]  <- MR[9] shadow  [3936,4776)
0x1CBC: [0x23, 0x2620,0,0x1F00,0]  <- MR[12] shadow [3968,4880)
0x1CD0: [0x24, 0x2690,0,0x1F40,0]  <- extra (sin MR) [4000,4936)
```
Solo los bloques con sello 0x204 (shadow) definen límites del IB (en bytes).
El runtime dibuja cada part como `[offset_previo, offset_bone)`.

**Fix aplicado en build_janemba3.py (v3.1)**:
- Escribir SOLO en los bloques shadow (sello 0x204), en +4 (end_byte) y +0xC
  (start_byte), con valores = índice × 2.
- El bloque extra (bone 0x24 @0x1CD0) se actualiza al final del IB.
- Regiones de Janemba: [0,3468), [3468,6396), [6396,9423) (3 shadows) +
  extra al final. sec34=2128, vb2=226, IB=9423 índices.

**Generado**: `%TEMP%\opencode\janemba_v3.bin` (1090304 bytes AMB) →
`janemba_v3.lzx` (149860, /N:2048) → `data_cmn_janemba_v3.afs` (modo
archivo completo, entrada 327 = Janemba, 3989 restantes intactas).
Instalado en `mods\krillin_afs\us\data_cmn.afs`.
**Probar**: seleccionar mod krillin_afs y cargar Krillin. Si se ve la forma
de Janemba (no masa deforme), el mapeo de arms es correcto.

### 13.5.14 SESIÓN 3 (2026-08-14): PIPELINE VALIDADO + v6 FUNCIONA (2026-08-14)

**RESULTADO FINAL DE LA SESIÓN**: **Janemba entra en combate sin crash** (v6).
El modelo se ve como masa deforme (re-rigging pendiente). Este es el primer
personaje IW que arranca y entra en combate en el recomp.

**Aprendizajes clave de la sesión**:

1. **El bin que el guest lee es la e326 del AFS** (682528 = `b327_hd.bin`),
   NO la e327 (624000 = `test327.bin`). La numeración "327-329" del AGENTS.md
   era 1-off (índice de tabla A vs índice real). `b327_hd.bin` = e326.

2. **Método AFS VALIDADO** (`build_afs.py`): e326 loc INTACTO, bin crece en
   su lugar, e327+ desplazadas por `delta redondeado a 0x100`, entradas
   vacías (loc=0) preservadas. Reproduce byte a byte `data_cmn_janemba.afs`.
   Método INCORRECTO (re-alinear a 0x80) → cuelgue de arranque.

3. **Pipeline de vértices CORRECTO**: `convert_personaje.py` (skinning
   PS2 → posiciones locales HD) → `decimar.py` (voxel cell=0.10 → 2386 verts)
   → `build_janemba2.py` (empaquetar AMB). **NO usar posiciones absolutas**
   (valores enormes como -8.7 → cuelgue de arranque).

4. **Restricciones del runtime** (crítico):
   - v4 (sec34=2386, IB=8484, AWG0 crece a 142320): entra al select pero
     **CRASH en combate** (crash 0x7ff6180e6cc5).
   - v5 (sec34=1313, IB=5100, AWG0 se encoge a 88352): **NO arranca**
     (el guest deserializa por los offsets del AWG header; si se encoge,
     los offsets de AMG1+ apuntan a datos que se solapan).
   - **v6 (sec34=1956, IB=5140, AWG0 mantiene 116720): FUNCIONA.** Rellenar
     sec34/IB a los conteos EXACTOS de Krillin con slots vacíos es la clave.
   - El AWG0 NO puede encogerse (cuélga) ni crecer en exceso (crash combate).

5. **Re-mapear arms CRASHEA**: cambiar offsets de los shadows a rangos
   nuevos [0,1275,2550,3825,5100] → crash al procesar el modelo
   (0x7ff6180cf202). **HALLAZGO: los offsets de los arms NO son rangos del
   IB a dibujar.** En Krillin ORIGINAL todos los 5140 índices están en
   [0,3904); los rangos [3904,4936) de los shadows están VACÍOS. El IB se
   dibuja completo; los offsets de los arms definen otra información
   (skinning de huesos, no qué triángulos dibujar).

6. **La masa deforme del v6 es RE-RIGGING**: los vértices de Janemba tienen
   posiciones locales por hueso (y=0.358, y=-8.706, y=-8.374...) skinneadas
   con HUESOS DE JANEMBA (JNB). El guest las interpreta con los HUESOS DE
   KRILLIN (KLL) del arm del mesh group → posiciones mal interpretadas →
   masa deforme. **Fix: mapear huesos JNB→KLL por labels (JNB_HEAD→KLL_HEAD,
   JNB_WAIST→KLL_WAIST, JNB_LLEGROT→KLL_LLEGROT...) y transformar posiciones
   locales de Janemba al espacio local de Krillin.**

**Archivos generados** (en `%TEMP%\opencode\`):
- v4 = `janemba_v4.bin` (1099760, = `janemba_amb10.bin`) — entra al select,
  crash combate
- v5 = `janemba_v5.bin` (1045792, sec34=1313) — no arranca
- v6 = `janemba_v6.bin` (1074160, sec34=1956/IB=5140) — **FUNCIONA**,
  `janemba_v6.lzx` (123520), `data_cmn_janemba_v6.afs`
- v6r = arms re-mapeados — crash arranque (descartado)

**Herramienta nueva**: `awo_tools/decimar_tri.py` (decimación por triángulos
para caber en límites de Krillin) y `build_afs.py` reescrito con el método
validado de reconstrucción AFS.

### 13.5.15 RE-RIGGING JNB→KLL: ANÁLISIS (2026-08-14, sesión 3 — final)

**El bloqueador actual**: Janemba v6 entra en combate pero se ve como masa
deforme. Causa: los vértices de Janemba tienen posiciones locales skinneadas
con huesos JNB, pero el guest las interpreta con los huesos KLL del arm del
mesh group de Krillin.

**Análisis completado**:
1. **Labels de huesos**: Krillin HD tiene 51 huesos (KLL_*, labels en +0x24
   del AWO, 102 cadenas = 2 bloques duplicados de 51). Janemba IW tiene 64
   labels (JNB_*, 48 huesos + cola T_TAIL1-6 + dedos múltiples).
2. **Mapeo por labels**: 46/64 mapean 1:1 (JNB_HEAD→KLL_HEAD,
   JNB_LARM1→KLL_LARM1, XJNB_BODY→XKLL_BODY, etc). Los que NO mapean:
   dedos L01-L41 (Krillin solo tiene L00), faces L01-L18 (Krillin solo
   L00), y la cola T_TAIL1-6 (Krillin no tiene).
3. **Las poses por índice NO coinciden**: bone 2 de KLL=(0,0,0) vs
   JNB=(-0.71,-0.71,0). El orden de huesos difiere entre esqueletos — solo
   se puede mapear por labels.
4. **El eje de 80 bytes NO contiene la matriz de pose**: los floats 0x00-0x2F
   son identidad (0,0,0,1.0 ×2 + 1.0×5). El eje tiene: +0x30 sello
   (0x6000020F raíz / 0x9000020C hueso), +0x34 ptr armature (bloque 16B),
   +0x38 child, +0x3C sibling, +0x40 parent. La jerarquía se recorre por
   estos punteros.
5. **AMG header PS2**: +0x10 bone_am, +0x14 axes_loc (0x20), +0x18
   mesh_groups (17), +0x1C labels_off. Los labels están en una tabla con
   offsets (no secuencial).

**El re-rigging requiere**:
1. Parsear jerarquía de huesos JNB (48) y KLL (51) via child/sibling/parent.
2. Extraer las matrices de pose bind de cada hueso (NO en el eje 80B —
   buscar en el armature o mesh part headers).
3. Mapear JNB→KLL por labels (46 directos; resolver dedos L01+→L00,
   faces→L00_FACE, cola→WAIST o ignorar).
4. Para cada vértice skinned a (bone_jnb, weight, pos_local):
   world = M_jnb_bind · pos_local; pos_local_kll = M_kll_bind⁻¹ · world.
5. Reconstruir el bin con las posiciones transformadas.

**Alternativa no explorada**: copiar los ejes de Janemba al bin v6 (para que
el guest skinee con las poses de Janemba). Requiere que el guest use los ejes
del bin (no del arm). Vale la pena probar ANTES del re-rigging completo por
ser un cambio barato.

**DESCARTADA (verificado)**: copiar los ejes NO funciona. Los ejes de 80B de
ambos esqueletos son la matriz IDENTIDAD (bind pose). El guest NO usa los
ejes para la pose — las posiciones locales de los vértices se usan
directamente (multiplicadas por identidad = sin cambio). La masa deforme es
porque las posiciones locales de Janemba (y=-8.7, en el espacio del hueso
JNB) se interpretan en el espacio del hueso KLL equivalente del arm.
**El re-rigging correcto**: transformar cada vértice de Janemba del espacio
del hueso JNB al espacio del hueso KLL equivalente, usando la relación entre
las poses bind de ambos esqueletos. Para huesos equivalentes en la misma
pose base (figura de pie), la transformación es una traslación: 
`v_kll_local = v_jnb_local + (origen_kll - origen_jnb)`, donde origen es la
posición del hueso en el espacio del modelo.

**Herramienta nueva**: `awo_tools/decimar_tri.py` (decimación por triángulos
para caber en límites de Krillin) y `build_afs.py` reescrito con el método
validado de reconstrucción AFS.

### 13.5.16 ✅ EL VÉRTICE HD LLEVA EL BONE INDEX EN +28 (DESCUBRIMIENTO CLAVE)

**Inspirado por el port B3→B1** (docs/INVESTIGACION_FORMATO_B1_HD.md §9 del
proyecto hermano): el formato de vértice HD es `[pos.xyz, w, bone, normal,
FFFF, uv]` stride 44. **En B3 el layout es**:
```
+00: nan (flag)   +04: u   +08: v
+12: pos.z_local  +16: pos.x_local  +20: pos.y_local
+24: peso (float)
+28: BONE INDEX (u32)      <-- el guest skinea con la matriz de este hueso
+32: normal.z  +36: normal.y  +40: normal.x
```

**BUG RAÍZ del v6**: `build_vertex_hd` escribía `f32(0.0)` en +28 (bone index),
así que TODOS los vértices de Janemba apuntaban al hueso 0 (BODY) → masa
deforme. Corregido: escribir `bone_idx` como u32 en +28.

**Mapeo de labels**:
- Krillin HD: AWO +0x24 → tabla de 102 bloques de 16B; el label del bone N
  está en el bloque `N*2` (índices pares 0..100). bone 0=XKLL_BODY,
  1=KLL_WAIST, 2=KLL_STMC... 12=CHEST, 15=LARM1, 28=HEAD, 38=LLEGROT...
- Janemba PS2: AMG +0x1C (labels_off) → bloques de 16B por bone (`bone_idx*16`).
  AMG0 tiene 24 labels pares (0=XJNB_BODY, 2=JNB_WAIST, 4=JNB_LLEGROT...).
  Los dedos/caras están en AMGs separados (AMG1-10 dedos, AMG11-16 caras).

**Mapeo JNB→KLL** (rig_mapeo.py): bone_jnb→label→label_kll→bone_kll. 24
directos + manual (dedos impares→L00_LHAND/RHAND, caras→L00_FACE).

**Resultados**:
- v7 (24 mapeos directos, sin mapeo→bone 0): **FUNCIONA, cuerpo de Janemba
  reconocible** pero con triángulos corruptos y parpadeo (dedos/caras a bone 0).
- v8 (mapeo completo incl. dedos→18/25, caras→36): **CRASH** — las posiciones
  locales de dedos JNB interpretadas con L00_LHAND de Krillin dan valores
  extremos que rompen el render.
- **Estado actual**: v7 instalado (cuerpo reconocible, corrupto). Siguiente
  paso: refinar el mapeo (probar caras→36 sin dedos→18, o mantener dedos en
  fallback).

**Herramientas**: `rig_mapeo.py` (mapeo + remap de bone indices), pipeline:
`convert_personaje.py` → `rig_mapeo.py` → `decimar_tri.py` → `build_janemba2.py`.

### 13.5.17 ✅ EXPERIMENTO DE VALIDACIÓN: KRILLIN B3 PS2 → B3 HD (CONVERSOR)

**Objetivo**: validar el conversor #AMO0→#AWO con Krillin (mismo esqueleto,
sin re-rigging) para aprender el proceso de traer modelos del B3 original
(PS2) — aplicable a Shin Budokai, Budokai 2, etc.

**Resultado**: **el conversor FUNCIONA** — Krillin del B3 PS2 renderiza en HD
con **silueta reconocible** (no masa deforme). El pipeline validado:
```
convert_personaje.py (skinning PS2→posiciones locales HD + bone index)
  → build_ib_from_ps2.py (dedup voxel + IB de triángulos PS2)
  → build_janemba2.py (empaquetar en plantilla b327_hd)
  → build_afs.py (AFS reconstruido, e326)
```

**Lecciones**:
1. El layout del vértice HD (corregido en build_vertex_hd): `[nan, u, v,
   pos.z, pos.x, pos.y, peso, BONE_INDEX(u32), normal.z, normal.y, normal.x]`.
2. Krillin PS2 y HD comparten esqueleto (51 huesos, labels KLL idénticos) —
   los bone indices del PS2 son directamente válidos en HD. **Sin re-rigging.**
3. v2 (1443 verts, bin 134044 LZX): **CUELGA** la carga inicial (bin > slot
   por mucho + más vértices).
4. v3 (1109 verts, bin 125764): **ARRANCA y Krillin es reconocible por
   silueta** pero corrupto (triángulos sueltos).
5. La corrupción viene de que el conversor solo llena sec34 (cuerpo); el HD
   original usa **vb2 (226 verts) para la cabeza/rostro** y el IB referencia
   AMBOS buffers (max índice HD=2189 > sec34=1956). El IB HD no es un
   triangle list secuencial — es optimizado (vértices cercanos).

**Siguiente paso**: replicar la estructura HD completa — llenar vb2 con la
cabeza/rostro y construir un IB que referencie ambos buffers en el orden del
HD. Esto mejora el detalle (la cabeza dejaría de corromperse).

**Herramienta nueva**: `build_ib_from_ps2.py` (vert + IB desde triángulos PS2).

### 13.5.18 REVISIÓN 2026-08-14 (tarde): EL MODELO HD DE KRILLIN ES DISTINTO AL PS2

**El usuario reportó que el conversor NO es funcional** (masa deforme). Tras
investigar a fondo, se encontró un **hallazgo que cambia el enfoque**:

**El HD original de Krillin (e326 = b327_hd.bin)**:
- sec34 (1956 verts): **SOLO bones 0-35** (cuerpo, brazos, cabeza) skinned.
- vb2 (226 verts): bone=0xFFFFFFFF (sin skin), posiciones ABSOLUTAS — la
  cabeza/rostro/caras.
- **NO usa los bones 36-50 en ningún vértice skinned.** Las piernas/cara del
  modelo HD NO están skinned con bones de pierna.

**El PS2 de Krillin (b327_ps2.bin)**:
- El skin usa bones 1-48, incluyendo piernas (38-48) skinned.
- La geometría es 9700 verts expandidos, 4331 únicos (sin decimar).

**El conversor #AMO0→#AWO NO es una simple conversión de formato** — el HD
es un re-trabajo: cuerpo skinned (0-35) + cabeza/caras sin skin en vb2.
El guest valida los bone indices; poner bones 36-50 (piernas PS2) en sec34
**cuelga** la carga (el guest no tiene esas matrices configuradas para el
bin).

**Lecciones del experimento Krillin**:
1. Los fixes de IB (index mismatch, se perdían triángulos) y UV (orden u,v)
   son correctos y necesarios.
2. Pero el bone index PS2→HD de Krillin NO es directo: el HD usa un rig
   simplificado (0-35) + vb2 sin skin.
3. **El Janemba v7 funcionaba** (cuerpo reconocible) porque IW usa un rig
   completo que coincide mejor con los 51 huesos del bin.

**Para un conversor funcional** hay que replicar la estructura HD:
- Mapear piernas/cabeza PS2 → bones 0-35 o al vb2 (sin skin).
- Construir el IB referenciando sec34 + vb2.
Esto es un re-trabajo de geometría, no una conversión mecánica.

### 13.5.19 🔴 VERDAD FUNDAMENTAL: EL PARSER PS2 NO LEE EL IB DE TRIÁNGULOS

**Revisión final (2026-08-14)**: el pipeline de conversión **NUNCA fue
correcto**. `extract_geometry.py::_parse_part` lee los vértices de cada part
como **únicos** (`n_verts = len(mesh_data[32:]) // stride`), pero **NO lee el
index buffer (IB) de triángulos** de la part. 

**Consecuencias**:
1. `build_ib_from_ps2.py` genera triángulos como `global_off + t*3`
   asumiendo verts expandidos (3 por triángulo), pero los verts son únicos →
   el IB resultante es incorrecto.
2. El `janemba_ib.bin` que hizo funcionar "Janemba v7" es un artefacto
   (`[0,256,512,...]` saltos de 256, max 65294) — **no es un triangle list
   real**. Janemba v7 "funcionaba" por accidente (el guest dibujó un patrón
   pseudo-aleatorio que parecía un cuerpo).
3. El formato del mesh part PS2 (header 0xA0 + mesh_data) tiene el IB en un
   layout aún no mapeado (el área tras los verts contiene datos no-IB).

**El siguiente paso REAL**: hacer RE del formato de mesh part PS2 para
localizar el IB de triángulos (índices que referencian los verts únicos de
la part), y reconstruir el conversor con triángulos correctos. Sin esto, el
conversor produce geometría corrupta.

### 13.5.20 ✅ RESUELTO: EL FORMATO DEL IB PS2 (MaxScript budokai_updated.ms)

**El hallazgo de `modding resources update 2`** (informe completo en
`modding resources update 2\INFORME_modding_resources_update_2.md`) reveló el
formato del IB de triángulos PS2:

**El mesh part PS2 NO tiene un index buffer explícito.** Cada mesh part se
compone de **submeshes en cadena**, cada uno con header de 0x20 bytes:
```
mesh_data:
  +0x00..0x0F: cabecera (12B) + ukw
  +0x10: FaceType (long)   <- 1 = triangle strip, 0 = tripletes
  +0x14: VertCount (long)
  +0x18: Null (8B)
  +0x20: [VertCount vertices de 48B]
  [siguiente submesh en cadena]
```
- **FaceType == 1**: triangle strip (winding alternado zig-zag): f1=0, f2=1,
  y para x=2..: f3=x, dirección alterna, append [f1,f2,f3] o [f1,f3,f2].
- **FaceType == 0**: triángulos consecutivos (cada 3 vértices un triángulo).

**Formato de vértice PS2** (primer byte del meshType, stride):
- 0xB5/0xB6/0xF5 = 48B (estándar personaje): pos(3)+null+normal(3)+null+uv(2)+null+skip4
- 0xBD/0xFD/0x3D = 48B (con normales+UV)
- 0x199 = 32B (pos+normal, sin UV)
- 0xB4/0xA4/0x99/0x92/0x19 = 32B (faciales: pos+uv, sin normal)
- 0x90 = 16B (sombras)

**Herramienta nueva**: `awo_tools/parse_ps2_mesh.py` — parser del mesh part
PS2 basado en el MaxScript. Resultados para Krillin PS2 (b327_ps2):
- AMG0: 3990 verts, 2392 tris (19 parts)
- Total 18 AMGs: 9144 verts, 5182 tris
- Esto ES la geometría real con triángulos correctos.

**Herramienta**: `awo_tools/build_hd_pipeline.py` — pipeline completo:
parse → skin (SkinData) → verts HD (layout bone index) → decimar → IB.
El v7 resultante (1018 verts, 1700 tris) **cuelga el arranque** — los
vértices sin skin (30%) usan posiciones absolutas con bone 0, lo que cuelga
el guest. **Pendiente**: mapear/descartar vértices sin skin.

**NOTA sobre la geometría real**: el conteo de verts del AMG0 varía según el
`end` usado: `md+mesh_size` (flag +0x90) da 3990 verts AMG0 / 9144 total
(fuente autoritativa del MaxScript); `part siguiente` da 354 / 5005. El
`mesh_size` del flag es la extensión real de la part.

**Pendiente principal**: el mapeo skin→malla. El SkinData de convert_personaje
da voffs que NO coinciden exactamente con los offsets de vértices de la malla
(solo 20/52 en la part 0). Los voffs del skin apuntan a la v/vn list del rig
con un mapeo que requiere más RE (relación entre la v/vn list y los vértices
de los submeshes). Hasta resolverlo, los vértices sin skin usan posiciones
absolutas → cuelgue.

**Documentación clave nueva**: `modding resources update 2\` contiene los
tutoriales y herramientas de la comunidad (lista de bins B3/IW, formato AMG,
re-rigging con Tutorial12, SLXS para añadir personajes, compresión LZX
512KB, texturas AZT/DDS). Ver el informe completo.

## 13. MANTENIMIENTO: ARCHIVOS .bmp/.dmp

El runtime genera capturas de debug del GPU (`.bmp`, ~30MB cada una) y dumps
de crash (`.dmp`) en el directorio del build. Se limpiaron 906MB. Los `.bmp`
son capturas de debug (probablemente un cvar) y pueden eliminarse.

## 7. ARCHIVOS DE TRABAJO

| Archivo | Propósito |
|---------|-----------|
| `awo_tools/parse_model.py` | Parser #AMB → #AMO0/#AMG o #AWO/#AWG |
| `awo_tools/analyze_awg.py` | Análisis de un #AWG |
| `awo_tools/analyze_mesh.py` | Mesh parts dentro de un AWG |
| `awo_tools/trace_bone.py` | Trazado jerárquico de huesos |
| `awo_tools/extract_geometry.py` | Extraer geometría PS2 (vértices B5) |
| `awo_tools/build_awo.py` | Conversor v1 (estructura simplificada, crasheaba) |
| `awo_tools/build_awo_v2.py` | Conversor v2 (reemplazo geometría, rompía +0x34) |
| `awo_tools/build_awo_v3.py` | Conversor v3 (índices fuera de VB) |
| `awo_tools/build_awo_v4.py` | **Conversor v4 (estructura correcta: AWO+AZT, tamaño fijo)** |
| `awo_tools/RE_PROGRESO.md` | Documento de RE completo (825 líneas) |

Datos en `%TEMP%\opencode\`: b327_ps2.bin, b327_hd.bin, b327_hd.lzx, b328_hd.bin,
b329_hd.bin, b146_ps2.bin, b146_hd.bin, b352_hd.bin, y los .lzx/.bin generados.
