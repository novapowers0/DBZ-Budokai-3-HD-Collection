# Sesión RE del port completo — 2026-08-26

**Objetivo**: reconstruir la estructura de dibujo HD para un pool reordenado
(topología PS2 exacta). Decisión del usuario: comprometerse al port completo.

## 1. ESTRUCTURA DE DIBUJO DEL AWG0 MAPEADA (Cell F2, bin 147)

```
AWG0 = 0xCC0 (rel bin: 0x40 + tabla AWG)
mesh group en +0x640 (0x1300), mg_size 0x2F90
```

| Región | Off rel AWG0 | Contenido |
|---|---|---|
| mesh-ref blocks | 0x640 | 22 bloques de parts de dibujo |
| ejes | 0xD20 (axes_base) | 48×0x50: quat+pos+scale, sello 0x6000020F, +0x34 arm_ptr, +0x38 hijo, +0x3C hermano, +0x40 padre |
| matriz de zonas | 0x1C20 (0x28E0) | 48+ filas de 0x10: diagonal de índices de hueso + punteros a bboxes |
| bboxes | 0x1FE0 (0x2CA0) | 0x40 c/u: AABB min/max por zona (model-space) |
| descriptores | 0x2209 (0x2EA9) | 0x60 c/u: A/B de dibujo |
| sec34 | +0x34 (0x313A+2=0x3DFC) | 2661 slots × 44 |
| vb2 | +0x2C (0x20770) | 276 slots × 44 (layout B) |
| IB | +0x30 (0x23700) | 6302 índices u16 BE |

## 2. MESH-REF BLOCKS (0x50, bloques 1+; el 0 es 0x40)

```
[0x44, 0x44, 0, 0][identidad 0x20][0, 5, 0, 5][X, Y][type u16, texture u16]
```
- **X = índice de descriptor** (varios mesh-ref por descriptor).
- **Y = hueso primario de la parte**.
- type: 0x1B5 (B5, cuerpo), 0x1B4 (B4, cara), 0x1F5 (F5).
- Los 22 bloques (X=descriptor, Y=hueso):
  (0,1) (2,1) (2,7) (3,4) (3,6) (3,1) (5,6) (5,1) (8,FF) (9,6) (9,7) (9,1)
  (10,4) (11,13) (11,7) (12,4) (12,6) (12,1) (14,4) (14,7) (14,1) (8,FF)

## 3. DESCRIPTOR (0x60) — ENCODING A/B CONFIRMADO

```
+00 label (8 chars, "XCEL_BODY")
+10 0x09 | +14 0x0F
+18 "max N m"
+24 0x34 | +28/+2C/+30 floats | +34 0x80000000
+3C 1/2/2 (varía)
+40 0x1158 (pool off const) | +44 0x2C (stride 44) | +48 0x05
+50 A_start <<8 | +54 A_count <<8 | +58 B_start <<8 | +5C B_count <<8 | 0x01 flag
```
**Verificado**: los índices del IB en rango B caen SIEMPRE en rango A (correlación
directa). Cell F2: 22 descriptores XCEL_BOD (A contiguos 56→1551) + manos/cara/cola
que viven en OTROS AWGs.

## 4. 🔴🔴 HALLAZGOS DE LA SESIÓN

### 4.1 EL ORDEN DEL POOL NO IMPORTA (test de pool invertido)

`cell_reverse_test`: el pool sec34 se INVIERTE (mismas posiciones/bones, orden
nuevo), IB remapeado, A/B recomputados, mesh-ref/zonas/bboxes INTACTOS.
**RESULTADO EN JUEGO: renderiza correctamente (Cell normal).**

→ El guest lee el pool por los descriptores A/B + IB, NO por los mesh-ref/zonas/
bboxes por índice. El port PUEDE usar cualquier orden de pool.

### 4.2 🔴 EL BUG DEL PORT: DESCRIPTOR A

El port (port_ps2_b3_geometry.py) calculaba `A = [primer_vértice, nº_vértices]`
asumiendo contigüidad. PERO las parts COMPARTEN vértices (dedup) → el strip de
cada part referencia índices MUCHO más amplios:
```
conv2 desc1: A=[48,60) pero índices[47,107]  → 22 de 23 descriptores A mal
```
Fix: `A = [min(índices de B), max+1)`. `cell_conv2_fixA` → 22/22 correctos.
**RESULTADO EN JUEGO: SIN CAMBIO (se ve exactamente igual)** → A NO era el bug
visual (aunque el encoding estaba mal).

### 4.3 LA GEOMETRÍA DEL PORT ES CORRECTA (punto a punto)

conv2 transformado por las matrices world (parent corregido) = modelo PS2 EXACTO:
```
bbox conv2: min[-9.66,-12.45,-8] max[10.66,13.5,3.57]
bbox PS2  : min[-9.66,-12.45,-8] max[ 9.66,13.5,3.57]
nearest PS2: med 0.000 p90 0.689 max 1.518  →  geometría 100% correcta
```

### 4.4 DIFERENCIAS CONFIRMADAS conv2 vs template (posibles causas del amorfo)

1. **IB nuevo**: conv2 usa IB de topología PS2 (6298/6302 índices distintos,
   4298 padding 0xFFFF). El template usa el IB original.
2. **Bones 34-47 en el sec34**: conv2 tiene 293 slots (11%) con bones 34-47
   (cara/cola) en el sec34. El template usa SOLO bones 0-33 en el sec34.
3. **vb2 con layout DISTINTO**: 
   - Template (layout B Cell F2): `[1.0, 0, 0, ?, ?, ?, nan@+20, U@+24, V@+28, normal@+32]` — SIN posiciones claras (276 slots).
   - Port (geometry.py): `[x, y, z, 0,0,0, 0, FFFFFFFF, nx, ny, nz]` — layout distinto.
4. **Decimación**: el port usa geometría decimada (1880 verts, voxel 0.05) →
   triángulos estirados (p90 5.06) → "poligonado mal".

### 4.5 DISCRIMINADOR EN CURSO: ¿USA EL GUEST EL BONE DEL VÉRTICE?

`cell_bone0_test`: plantilla con TODOS los bones sec34 = 0 (posiciones intactas).
- Si se DEFORMA → el guest USA el bone del vértice → los bones 34-47 de conv2
  son la causa → mapear/clampear.
- Si queda IGUAL → el guest usa el mesh-ref/estructura → el bone del vértice
  no es la causa → el amorfo es vb2/topología/decimación.

## 5. CONCLUSIÓN PARCIAL

El port está MUY cerca: geometría correcta + A/B corregido + orden de pool libre.
El amorfo persiste por (2) bones 34-47 en sec34 y/o (3) vb2 layout. El test
bone0 discrimina la hipótesis del bone. Pendiente: decidir el fix del vb2
(replicar el layout B o conservar el vb2 del template como la inyección).

## 7. 🔴🔴🔴 CONTAMINACIÓN DE LOS TESTS — cell_npm8_test QUEDÓ ACTIVO (2026-08-26)

**Descubrimiento crítico al analizar dbz3_038.log**: `cell_npm8_test` (inyección
con umbral 1.4 en cabeza, de la sesión anterior) **quedó activo durante toda la
sesión de RE del port**. Nunca se le puso `.disabled`.

**Mecanismo del runtime** (`afs.cpp` `AfsListMods` + `AfsFindModOverride`):
los mods ACTIVOS van primero (orden alfabético) en `g_mod_dirs_cache`, y el
override sirve el **PRIMER mod con entrada para ese slot**. Con npm8 activo:

| Test | Cache (alfabético) | Mod servido | Resultado real |
|---|---|---|---|
| cell_reverse_test | [**npm8**, reverse] | **npm8** (inyección) | ❌ "prácticamente igual" = INVALIDO (vio la inyección) |
| cell_port_Afix_test | [**npm8**, Afix] | **npm8** (inyección) | ❌ "exactamente igual" = INVALIDO (vio la inyección) |
| cell_bone0_test | [**bone0**, npm8] | **bone0** | ✅ "colapsó a los pies" = VÁLIDO |

**Consecuencias**:
- "El orden del pool NO importa" (reverse) — **SIN VALIDAR**.
- "El fix de A no cambia nada" (Afix) — **SIN VALIDAR**.
- "El guest usa el bone del vértice" (bone0) — **VÁLIDO**.

**Fix aplicado**: `cell_npm8_test` desactivado. Solo un mod activo a la vez.
⚠️ **LECCIÓN**: antes de cada test, verificar que SOLO el mod de prueba esté
activo (`Get-ChildItem mods | Where -not .disabled`). El launcher activa/desactiva
por marker; un mod olvidado contamina todos los tests del mismo slot.

**PENDIENTE RE-VALIDAR**: `cell_reverse_test` (pool invertido) y
`cell_port_Afix_test` (port con A corregido) AHORA SÍ en solitario.

## 7.1 🔴🔴🔴 RESULTADO REAL DEL REVERSE — EL ORDEN DEL POOL SÍ IMPORTA (2026-08-26)

**cell_reverse_test re-testado EN SOLITARIO** (npm8 desactivado, era el único
activo): **"una serie de deformidades impresionantes"**. 

**CONCLUSIÓN CORREGIDA**: la afirmación de §4.1 ("el orden del pool NO importa")
era FALSA — provenía del test contaminado (se servía npm8, no el reverse). 
**El guest SÍ está atado al orden del pool** por los mesh-ref/zonas (estructura
que referencia el pool por índice original). Un pool reordenado (aunque con los
mismos vértices/bones) deforma el render.

**Reconciliación con el bone0 (válido)**:
- bone0 (bones→0): colapsa → el guest USA el bone del vértice (+28) para el transform.
- reverse (pool reordenado): deforma → el guest TAMBIÉN está atado al orden del
  pool (mesh-ref Y por parte / zonas por hueso en el orden original).

Ambos son ciertos: el transform usa el bone del vértice, PERO la estructura de
dibujo (mesh-ref/zonas) referencia el pool por su orden original. **El port con
pool reordenado requiere reconstruir TODA la estructura (mesh-ref + matriz de
zonas + bboxes + descriptores) coherente con el nuevo pool.** La inyección
funciona porque mantiene el orden del pool de la plantilla → estructura válida.

**Estado de la sesión (decisión del usuario: no crear versiones nuevas, traspaso
a otra sesión)**:
- Mod activo: `cell_reverse_test` (el último probado, DEFORME — no es un buen
  estado de juego, solo diagnóstico).
- `cell_npm8_test` desactivado (causó la contaminación).
- El mejor resultado de inyección sigue siendo `cell_npm4` (umbral binario 0.8,
  "silueta mejoró significativamente") — reactivarlo para un estado jugable si
  se desea.
- **Pendiente para la próxima sesión**: (a) re-validar `cell_port_Afix_test` en
  solitario (el port con A corregido — ahora sin npm8); (b) decidir si el port
  reconstruye la estructura completa (mesh-ref/zonas/bboxes) o se acepta la
  inyección como el port práctico.

## 6. HERRAMIENTAS / ARCHIVOS

- `%TEMP%\opencode\pool_reorder_test.py` — test de reordenamiento de pool.
- `%TEMP%\opencode\cell_reverse.amb` — pool invertido (funciona en juego).
- `%TEMP%\opencode\cell_conv2.amb` — port original (A mal).
- `%TEMP%\opencode\cell_conv2_fixA.amb` — port con A corregido (sin cambio visual).
- `%TEMP%\opencode\cell_bone0.amb` — discriminador del bone.
- Mods: cell_reverse_test (ok), cell_port_Afix_test (sin cambio), cell_bone0_test (ACTIVO).