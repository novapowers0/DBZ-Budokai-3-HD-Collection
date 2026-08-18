# PLAN: CONSTRUIR EL AWO HD DESDE CERO (añadir personajes IW)

> Documento de planificación (2026-08-14). Objetivo: añadir personajes de
> Budokai Infinite World (Janemba, Pikkon, Pan, Super 17...) al recomp B3.

---

## 1. POR QUÉ EL RE-LAYOUT NO FUNCIONÓ (resumen ejecutivo)

- El runtime B3 espera que `sec34_count`/`vb2_count` (derivados de los offsets
  del AWG header) sean coherentes con la estructura completa del modelo.
- Agrandar un buffer de un modelo EXISTENTE (aunque sea +1 vértice) rompe la
  deserialización del guest → crash (null deref en combate).
- **PERO**: el runtime acepta conteos VARIABLES entre bins distintos
  (Krillin 327: sec34=1956; Krillin 328: sec34=1791; ambos funcionan).

## 2. LA ESTRATEGIA VIABLE

**Construir un AWO HD COMPLETO desde cero** con los conteos correctos del
personaje objetivo (no modificar un modelo existente). Un AWO nuevo con
estructura 100% coherente debería ser aceptado por el runtime.

**Dato de referencia**: el B1 (proyecto hermano) maneja modelos de combate con
sec34=3729 vértices (Tenshinhan). El formato AWO soporta buffers grandes.

## 3. REFERENCIAS DE FORMATO (mapeadas)

### 3.1 AWO (contenedor #AMB del B3)
```
#AMB header (0x40): entry0=AWO (loc 0x40, size), entry1=AZT (loc, size)
#AWO header:
  +0x10: bones (51 Krillin)
  +0x18: amg_count (18)
  +0x1C: amg_table (0x690, rel AWO)
  +0x34: axes_base (zona de ejes al final)
  +0x54..0x674: 51 entradas de 0x20 con punteros a zona ejes
Tabla AMG (18 entradas): apunta a magics #AWG (rel AWO)
```

### 3.2 AWG0 (magic en awg0_off, offsets rel magic)
```
+0x10: bone_am   +0x14: axes_loc   +0x18: axis_lines
+0x2C: vb2 (secundario)  +0x30: ib (index buffer)
+0x34: sec34 (principal) +0x38: restart
+0x1C: labels (51×32B)   +0x20: sec20 (mesh parts)  +0x28: meshgroup
```

### 3.3 Vértice HD (stride 44, alineado +2)
```
sec34:  [nan, VT.v, VT.u, V.z, pos.x_local, pos.y_local, weight, 0, VN.z, -VN.y, VN.x]
vb2:    [pos.x, pos.y, pos.z(1.0), 0,0,0,0, nan, VN.x, VN.y, VN.z] (layout distinto)
```

### 3.4 Mesh group (hueso0)
```
+0x00: count (13)   +0x28: ptr tabla mesh-ref blocks (0x1ED8)
Cada mesh-ref block (0x50): +0x18 sello, +0x1C arm, +0x20 idx, +0x28 tr
Cadena recursiva: dat@+0x30 → siguiente arm+dat
```

## 4. PLAN DE CONSTRUCCIÓN (AWO de Janemba)

### Fase A — Preparar geometría Janemba (bin 541 IW)
1. Extraer #AMO0 de Janemba (48 huesos, 17 AMGs, 4415 vértices únicos).
2. Aplicar skinning (rig 3056+ entradas → coords locales por hueso).
3. Convertir vértices al layout HD (stride 44): sec34 para cuerpo, vb2 para
   cabeza/accesorios (como hace el HD).
4. Deduplicar → N1 vértices para sec34, N2 para vb2.

### Fase B — Construir el AWO HD (estructura desde cero)
1. Header AWO: bones=48, amg_count=17 (los AMGs de Janemba), tabla AMG.
2. 17 AWGs, cada uno con su estructura (ejes, labels, mesh group).
3. AWG0: sec34 con N1 vértices, vb2 con N2, IB con los triángulos de Janemba,
   restart buffer.
4. Mesh group + mesh-ref blocks + arms reconstruidos para la geometría de
   Janemba (agrupando triángulos por hueso/material).
5. Zona de ejes (axes-array) con los 48 punteros del header.

### Fase C — Empaquetar y validar
1. Empaquetar #AMB (AWO + AZT). El AZT de Janemba es el bin 542 del IW
   (convertido a #AZT HD).
2. Comprimir con `xbcompress /N:2048`.
3. Colocar en un slot (override por entrada o AFS completo).
4. Validar: Janemba carga, se ve correcto, combate sin crash.

## 5. RIESGOS Y MITIGACIONES

| Riesgo | Mitigación |
|--------|-----------|
| El runtime rechaza un AWO con conteos muy distintos | Los bins 327/328 ya tienen conteos distintos; se probará incrementalmente |
| Los mesh-ref/arms reconstruidos no son aceptados | Se validará con un AWG0 mínimo primero (solo el cuerpo) |
| El layout del vértice de Janemba difiere | Usar el mismo layout que Krillin (validado por el skin PS2) |
| El skinning (huesos 48 vs 51) | Janemba tiene su propio esqueleto; los moveset ports ya mapean Janemba→Krillin |

## 6. PRÓXIMO PASO INMEDIATO

Construir el **AWG0 de prueba mínimo**: header AWO + AWG0 + sec34 con los
vértices skinneados de Janemba (primeros 2190) + IB reconstruido, para validar
que el runtime acepta la estructura antes de construir los 17 AWGs completos.
