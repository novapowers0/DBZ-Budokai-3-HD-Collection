# VIABILIDAD: METER MODELOS 3D EXTERNOS EN DBZ BUDOKAI 3 HD

> 2026-08-17 noche. Respuesta documentada a: "¿es viable hacer algo así o en
> general meter modelos 3D externos?" Basada en el feedback en juego del mod
> `krillin_ps2` (v7) y en los docs del proyecto hermano DBZ Budokai HD.

---

## 1. RESPUESTA CORTA

**SÍ es viable, pero NO por inyección de posiciones en slots** (eso ya llegó
a su límite). La vía validada por el proyecto B1 es **RECONSTRUIR el bin
completo** desde el PS2 (sec34 + IB + arms + submesh data regenerados), o
usar el **swap nativo** para personajes que ya existen en HD.

---

## 2. LO QUE DEMOSTRÓ EL MOD krillin_ps2 (v7 con umbral 0.3)

| Resultado | Zonas | Diagnóstico |
|-----------|-------|-------------|
| ✅ Perfecto | Manos, brazo der, rostro sup | Coords PS2/HD coinciden |
| ✅ Muy bien | Resto del cuerpo | Umbral 0.3 deja HD original |
| ❌ Fallan | Oreja, cabeza trasera, boca | Cara en bones 29-37 + vb2 → casi nada reescrito |
| ❌ Fallan | Hombro der, cinturón | 15-38% reescritos → mezcla HD+PS2 visible |
| ❌ Fallan | Rodilla der, pie izq | **0 slots en sec34 → viven en vb2, NO tocable** |

**El v7 es el MÁXIMO de la inyección en slots**: el HD de Krillin es un
re-trabajo decimado con 0% correspondencia de vértices. Solo ~197 slots
(10%) tienen correspondencia world real con el PS2.

---

## 3. POR QUÉ LA INYECCIÓN NO PUEDE MÁS (3 causas)

1. **El HD es re-topologizado** (IB propio, vértices reordenados). Inyectar
   coords PS2 sobre el IB del anfitrión conecta triángulos que no corresponden
   → deformación en las zonas sin coincidencia exacta.
2. **Las piernas/rodilla/pie viven en el vb2** (buffer secundario, posiciones
   ABSOLUTAS, bone=0xFFFFFFFF). La inyección solo toca el sec34. El vb2 usa
   layout distinto: `[pos.x, pos.y, pos.z, ...]` con z=1.0, sin marker +2.
3. **El 34% de slots (bones 36-50) no tienen coords PS2** para el sec34.

---

## 4. LA VÍA CORRECTA (validada en B1, transferible al B3)

### 4.1 Swap nativo (para personajes que YA existen en HD)
El runtime dibuja el bin #AWO/#AMB completo tal cual (mesh group + IB + bones
+ UVs). No valida el slot. **B3 ya lo tiene: `sw_goten_nativo` (Goten→Krillin,
100% funcional)**. Para añadir personajes B3 que existen en otros slots, solo
hay que instalar su par geom+tex.

### 4.2 Reconstrucción completa (para personajes SIN versión HD: IW)
Basada en la metodología SESION11 del B1:

```
1. Extraer #AMO0 del AFS PS2 (IW/B2/B3) + parsear mesh parts/verts/rig
2. Verificar esqueleto 1:1 con el anfitrión HD (labels, mismos labels)
3. Usar el bin HD nativo del MISMO esqueleto como PLANTILLA estructural
4. Reconstruir sec34 (44B) + IB desde los triángulos PS2 (FaceType)
5. Regenerar arms (rangos del IB por bone)
6. Regenerar la ZONA DE SUBMESH DATA (descriptores por mesh part)  ← clave
7. Convertir textura #AMT PS2 → #AZT HD
8. Comprimir ≤106496 → mid-insert → validar en combate
```

**La pieza que faltaba = SUBMESH DATA** (descifrada en B1):
```
+00..+5F floats de transformación/material
+60 c08 = inicio rango A (contiguos entre descriptores)
+64 c0C = tamaño rango A
+68 c10 = inicio rango B
+6C c14 = tamaño rango B
+70 label 16B (X??_BODY, ??_L01_LHAND...)
+80 string debug "max N m"
```
Copiar esta zona de una plantilla sobre geometría nueva → **hang**. Hay que
GENERAR un descriptor por mesh part PS2 con los rangos de los buffers nuevos.

---

## 5. ESTADO DEL B3 PARA LA RECONSTRUCCIÓN

**✅ Verificado en el B3 (hoy)**:
- El layout del vértice sec34 es `[0xFFFFFFFF, u, v, z, x, y, peso, BONE@+28,
  nrm.z, -nrm.y, nrm.x]` (stride 44, align +2).
- Las matrices de pose HD == PS2 (47/47 idénticas) → mismo espacio world.
- **La zona de submesh data EXISTE en el B3**: labels XKLL_BODY,
  KLL_L00_LHAND, KLL_L00_RHAND, XKLL_M_DTEETH, XKLL_M_UTEETH, XKLL_L00_FACE
  + strings `max N m` en 0x2D61-0x3471 del AWG0 y en cada AWG de manos/caras.
- El parser PS2 (parse_ps2_mesh.py) lee mesh parts + rig correctamente.

**⏳ Pendiente**:
- **Mapear el layout EXACTO del descriptor de submesh B3** (los offsets
  c08/c0C/c10/c14 NO están en +0x60 como en B1 — difieren). Es el único
  bloqueador para adaptar el pipeline de reconstrucción del B1.
- Adaptar `amo0_to_awo.py` (B1) al layout B3 (BONE@+28, offsets AWG relativos,
  vb2 distinto).

---

## 6. PERSONAJES RECOMENDADOS PARA EL PRIMER PORT REAL

Los 241 modelos IW→B3 PS2 ya existen en
`modding resources\All Character Models from IW into AMB format\`:

| Personaje | Por qué |
|-----------|---------|
| **Pikkon** (583-586) | ⚠️ DESCARTADO: esqueleto PKH distinto a KLL (58 bones con falda SKIRT) → NO 1:1 |
| **Pan** (566-569) | Pequeño, cabría fácil en el slot |
| **Super 17** (606-609) | Moveset port ya existe |

⚠️ **2026-08-17**: el esqueleto del personaje debe ser 1:1 con un anfitrión HD
del B3 (mismos labels en mismo orden). Pikkon NO lo es. La búsqueda correcta:
un traje alternativo de un personaje que ya existe en HD (como el B1 usó
Tenshinhan B2 con esqueleto 1:1). Ver `SESION_2026-08-17.md` §2.7.

Requisito: el personaje NO debe tener versión HD en el B3 (los que sí la
tienen → swap nativo). Ver `docs/PERSONAJES_BINS.md`.

---

## 7. RECURSOS REUTILIZABLES DEL PROYECTO B1

| Recurso | Uso |
|---------|-----|
| `DBZ Budokai HD\mod center hd\conversores\amo0_to_awo.py` | Parser PS2 + reempaquetado (adaptar al B3) |
| `DBZ Budokai HD\mod center hd\conversores\obj_to_awg_hd.py` | Retopología con umbral + inv_rigid (ya = v7) |
| `DBZ Budokai HD\mod center hd\conversores\port_b3_to_b1_v2.py` | Conversión de sellos B3↔B1 |
| `DBZ Budokai HD\docs\re\SESION11_PORT_PS2_METODOLOGIA.md` | Metodología completa del port PS2→HD |
| `DBZ Budokai HD\docs\re\RECONSTRUCCION_PORT_GERO_B3_B1.md` | Análisis binario del port entre juegos |

---

## 8. CONCLUSIÓN FINAL

1. **Para personajes que ya existen en HD**: swap nativo (hecho, funciona).
2. **Para personajes nuevos (IW)**: RECONSTRUIR el bin completo desde el PS2,
   usando el bin HD del mismo esqueleto como plantilla estructural. La vía es
   la misma que el B1 validó al 100% (Gero B3→B1, Chaozu HD→TSH).
3. **Bloqueador actual**: el layout del descriptor de submesh B3 (una tarde de
   RE, siguiendo el método B1). Una vez mapeado, el primer port real (Pikkon
   o Pan de IW) es la validación definitiva.
4. **La inyección en slots (krillin_ps2) NO es el camino para modelos
   externos** — es una técnica de "mejora local" del HD, ya en su límite.

---

## 9. ESTADO DE LA VÍA DE RECONSTRUCCIÓN (2026-08-17 noche)

### 9.1 Progreso del RE (todo verificado en el bin real)

| Pieza | Estado |
|-------|--------|
| Layout vértice sec34 | ✅ `[0xFFFFFFFF, u, v, z, x, y, peso, BONE@+28, nrm.z, -nrm.y, nrm.x]` (44B, align+2) |
| Matrices pose HD==PS2 | ✅ 47/47 idénticas → mismo espacio world |
| Zona submesh data B3 | ✅ **MAPEADA** (ver `awo_tools/SUBMESH_DATA_B3.md`): descriptor 0x60, rango A contiguo en +50/+54, rango B en +58/+5C |
| Mesh group B3 | ✅ Parcial: 13 grupos, headers 0x40B con type2=0x29BD (sello B3) |
| Arms B3 | ⏳ Pendiente de mapear con precisión (rangos del IB por bone) |
| Layout vb2 | ⏳ Pendiente: buffer de cara/rostro, posiciones 0..2, bone=0xFFFFFFFF |
| Parser PS2 (verts+tris+skin) | ✅ `parse_ps2_mesh.py` (FaceType, strides) |

### 9.2 El bloqueador real de la inyección (confirmado)

El **vb2** (buffer secundario) cubre el **15.4% del IB** (789 de 5140 índices)
= cabeza/caras. Tiene layout propio (posiciones 0..2, no world). La inyección
solo toca el sec34 → cabeza/piernas/rodilla/pie quedan HD SIEMPRE. Por eso el
v7 se ve bien en el cuerpo pero falla en oreja/boca/cabeza trasera/rodilla/
pie.

### 9.3 Vía óptima a seguir (ordenada)

1. **Swap nativo** para personajes que existen en HD (B3→B3): **hecho**.
2. **Mapear arms + vb2 del B3** (RE, 1-2 sesiones) para completar el mapa
   estructural del AWG0. **Arm ya mapeado** (51 bloques de 0x14, bones con
   mesh: 0/18/25/32/35/36 → rangos del IB). **vb2 parcial** (layout propio,
   posiciones 0..2, bone=0xFFFFFFFF).
3. **Buscar un personaje con esqueleto 1:1** con un HD del B3 (traje
   alternativo de personaje existente, estilo Tenshinhan B2 del B1). Pikkon
   descartado (esqueleto PKH distinto).
4. **Adaptar `amo0_to_awo.py` del B1 al B3**: layout vértice B3 + descriptor
   submesh B3 (+50/+54/+58/+5C) + offsets AWG relativos + vb2.
5. **Primer port real** del personaje 1:1 → bin desde cero con topología PS2
   + submesh regenerada → validar en combate.
6. **Automatizar** para el resto.

### 9.4 Recursos listos

- `awo_tools/SUBMESH_DATA_B3.md` — layout del descriptor de submesh B3.
- `awo_tools/parse_ps2_mesh.py` — parser PS2 (verts+tris con FaceType).
- `awo_tools/mezclar_ps2_hd_v6.py` — inyección world+umbral (v7, el límite).
- `DBZ Budokai HD\mod center hd\conversores\amo0_to_awo.py` — pipeline B1 a adaptar.