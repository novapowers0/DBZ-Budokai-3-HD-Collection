# SESIÓN 2026-08-26 — VÍA DE LA INYECCIÓN: TEMPLATE HD + POSICIONES PS2 CONVERTIDAS

> **RESUMEN EJECUTIVO**: el port PS2→B3 HD que reconstruye el pool/IB/descriptores
> (pipeline `port_ps2_b3_*`) **rompe la consistencia de los arms** de la plantilla →
> siempre amorfo. La vía que FUNCIONA es la **inyección**: plantilla HD COMPLETA
> intacta (pool, IB, descriptores, arms, ejes) y SOLO reescribir las posiciones
> (+12/+16/+20) de los slots sec34 con la geometría PS2 convertida al espacio
> bone-local. **VALIDADO EN JUEGO**: Cell F2 PS2 se ve reconocible (manos, torso,
> cabeza, pierna, silueta). Limitación actual: el PS2 tiene 1880 vértices únicos vs
> 2661 slots HD → el llenado colapsa zonas (aspecto "decimado"). La vía para
> conservar la calidad original = resamplear la superficie PS2 (nearest-point-on-
> surface) o reconstruir los arms.

---

## 1. LAS DOS VÍAS Y POR QUÉ SOLO UNA FUNCIONA

### 1.1 Port completo (pool/IB/descriptores reconstruidos) = AMORFO SIEMPRE

El pipeline `port_ps2_b3_{extract,geometry,decimate,draw,pack}` reconstruye:
- pool sec34 (nuestro orden, 1880 verts)
- IB (strips consecutivos nuestros)
- descriptores A/B (nuestros rangos)
- vb2, AWG/AZT desplazados (mid-insert interno)

El resultado en juego es **amorfo/no distinguible**. Causa raíz (con evidencia):
- El draw log instrumentado (`DBZ3_DRAW`, rexgpu-xenos.dll) **PROBÓ** que el guest
  DIBUJA nuestros strips correctamente: 23 strips en los offsets exactos de
  nuestros `B_start×2` y con conteo `B_count+2` (el +2 = padding degenerado).
  → El IB/descriptores/posicionamiento del pool **NO eran el bug**.
- El problema está en la **estructura de dibujo de la plantilla** (arms + mesh-ref
  + descriptores + pool intercalado por parts) que el guest usa para SKINNEAR y
  dibujar. Al reconstruir el pool en orden distinto, los arms de la plantilla
  referencian índices que ya no corresponden → skinning/parseo roto → amorfo.
- Test negativo adicional: clampear bones (35 y 33), +0x10=9 (body format) en
  todos, 0xFFFF→0 en el IB, flip de winding → **NINGUNO cambió el render**.
  El bin SÍ se sirve correctamente (el pool nuestro está en sec_rel).

### 1.2 Inyección (plantilla completa + posiciones) = RECONOCIBLE ✅

`test_injection.py` (mod center hd/ports/): toma la plantilla HD (cell147_hd.bin)
COMPLETA y solo reescribe +12/+16/+20 de los slots sec34. Mantiene IB,
descriptores, vb2, otros AWGs, ejes y arms → el guest lo dibuja con su estructura
intacta.

- **Inyección v1 (model-space)**: inyectó las posiciones PS2 EN MODEL-SPACE contra
  slots bone-local → emparejamiento en espacios DISTINTOS → partes reconocibles
  (mano, brazo der, rostro superior) pero mezcla/amorfo. Fue el test que el
  usuario recordaba como "lo que hicimos bien".
- **Inyección v2 (bone-local, mismo espacio)**: posiciones convertidas a bone-local
  → Cell mucho más reconocible (manos, torso, parte de cabeza, una pierna,
  silueta).
- **Inyección v3/v4 (world-matching + umbral)**: emparejamiento por VECINDAD EN
  WORLD (el slot's world = world[B]·local, contra los verts PS2 model-space) con
  conversión al bone-local del slot + umbral 2.0 (el 10% peor conserva la
  posición HD original). → **MUCHO MÁS Cell** pero con aspecto "decimado/reducido"
  y polígonos deformes. **ESTADO ACTUAL DE LA VÍA.**

## 2. 🔴 HALLAZGOS CLAVE DE LA SESIÓN

### 2.1 EL PARENT DEL EJE ES UN OFFSET, NO UN ÍNDICE (corrige todo lo anterior)

Los ejes (80B) en `mg+0x6E0` (48×80 para Cell F2). El campo `+0x40` es el **puntero
al eje padre expresado como OFFSET relativo al AWG0**, NO un índice de hueso:

```
parent_idx = (AWG0 + poff - axes_base) // 80    # poff = be32(eje+0x40)
```

Verificado: bone 1 parent=3360 (0xD20) → AWG0+0xD20 = 0x19E0 = el eje de bone 0.
bone 2 parent=3440 → 0x1A30 = bone 1. etc. (incrementos de 80).

**Consecuencia**: TODAS las matrices world calculadas antes de este hallazgo eran
basura (se usaba el offset como índice → no se acumulaba el parent). La conversión
`cell_conv` (model→bone-local) de la sesión anterior era inválida. Con el parent
corregido, el world del template traza un cuerpo coherente:
- pies y≈-12.6, rodillas ≈-5, cadera ≈0-1, pecho 2.8-4.6, hombros ≈6.6,
  cabeza y≈8.7 (bone 32), cráneo 8.72 (bone 33).
- **Coincide con el model-space del PS2** (pies -11.5, rodillas -7.2, pecho
  3.4-5.5, cabeza 9.6) → ambos cuerpos están en el MISMO world space.

### 2.2 EL SEC34 DEL HD ALMACENA POSICIONES BONE-LOCAL (no model-space)

- El template (Cell F2, formato A): sec34 = posiciones en espacio local del hueso
  (mag_med 1.70, max 6.37). El shader transforma `world[bone]·local`.
- El PS2: el rig asigna coords **model-space** (centroides por hueso trazan el
  cuerpo: pies -11.5, cabeza 9.6). Al alimentar model-space como bone-local el
  render se estira (amorfo).
- **La conversión correcta**: `local = inv(world[bone]) · model`. Verificado: local
  convertido mediana 2.15 (≈ template 1.7). Con esta conversión la inyección
  empareja en el MISMO espacio y funciona.

### 2.3 EL PS2 TIENE 1880 VÉRTICES ÚNICOS (4938 expandidos)

- El geometry del PS2 (cell_geometry.json) tiene **4938 verts** = vértices
  EXPANDIDOS del strip (cada triángulo guarda sus 3 verts, con duplicados).
- El dedup voxel (0.05) → **1880 únicos** = la densidad real de la superficie PS2.
- La plantilla HD tiene **2661 slots** sec34. → 1880 < 2661 → el llenado completo
  REUSA ~781 vértices → triángulos colapsados/planos → aspecto "decimado".
  **Esta es la causa del look "versión decimada y reducida" que ve el usuario.**

## 3. HERRAMIENTAS DE LA SESIÓN

| Archivo | Función |
|---|---|
| `mod center hd/ports/test_injection.py` | Inyección per-bone greedy (v1/v2). Reescribe +12/+16/+20 de los slots. |
| `mod center hd/ports/port_ps2_b3_inject.py` | **NUEVO** — inyección world-matching + conversión por slot + umbral (v3/v4). Uso: `python port_ps2_b3_inject.py <plantilla> <geometry.json> <umbral> <salida>` |
| `%TEMP%\opencode\cell_conv_geom.json` | geometry.json del PS2 con sec34 convertido a bone-local. |
| `%TEMP%\opencode\cell147_hd.bin` | Plantilla Cell F2 HD (bin 147). |
| `%TEMP%\opencode\cell_geometry.json` | Geometry PS2 sin decimar (4938 verts). |
| `%TEMP%\opencode\cell_delta0_geometry.json` | Geometry decimado (1880 verts) usado en el port/inyección. |

**Mods de prueba (out/build/win-amd64-release/mods/)**: cell_inject2_test (1001
slots, bone-local), cell_inject4_test (2385+276, world-matching+umbral 2.0 —
**ACTUAL**). Tests negativos documentados: cell_desc_test, cell_bodyfmt_test,
cell_nopad_test, cell_clamp33_test, cell_boneclamp_test, cell_conv_test (matrices
rotas).

## 4. CÓMO CONSERVAR LA CALIDAD ORIGINAL (próximos pasos)

El "look decimado" viene de rellenar 2661 slots con 1880 posiciones únicas.
Opciones, en orden de esfuerzo:

1. **Nearest-point-on-surface (NPM)**: para cada slot HD (world), proyectar al
   punto MÁS CERCANO de la superficie PS2 (sobre los triángulos del mesh, no al
   vértice más cercano). Produce 2661 posiciones distintas sobre la superficie PS2
   → sin colapso, sin "decimado". Requiere construir la lista de triángulos PS2
   (del extract: strips por part con FaceType) y un point-triangle distance.
   **Esta es la vía recomendada para conservar la calidad.**
2. **Subdivisión/refinamiento del PS2**: generar vértices intermedios sobre los
   triángulos PS2 hasta alcanzar ~2661. Equivalente al NPM pero generando malla.
3. **Reconstruir los arms** (port completo de verdad): RE del formato de los arms
   de la plantilla y regenerarlos para nuestro pool → el guest skinnea con la
   estructura correcta y acepta nuestro pool/IB. Es el "port real" pero requiere
   descifrar el formato de arms (estructura de skinning por hueso, aún sin mapear
   100%).
4. **Reducir el umbral / afinar el matching**: mejora incremental (menos estirado)
   pero NO elimina el colapso por falta de vértices.

**Dato para NPM**: la superficie PS2 completa (4938 expandidos) tiene TODAS las
posiciones; los triángulos se reconstruyen desde los parts del extract (strip
consecutivo, winding alternado, degenerados). El vértice expandido i del part → la
superficie. Proyectar cada slot HD (world) al triángulo PS2 más cercano da la
posición de la superficie PS2 en el punto más próximo → densidad 1:1 con el HD.

## 5. ✅ RESULTADOS EN JUEGO (2026-08-26 tarde) — UMBRAL ESTRICTO = LA CLAVE

**Progresión de tests y resultados (usuario)**: ver §5.1.

### 5.1 PROGRESIÓN COMPLETA DE LA INYECCIÓN

| Mod | Método | Resultado en juego |
|---|---|---|
| inject2 (1001 slots) | per-bone greedy, bone-local | Cell reconocible: manos, torso, parte cabeza, una pierna. |
| inject4 (2385+276) | world-matching + umbral 2.0 | Más Cell pero "decimado/reducido" + polígonos deformes. |
| npm (2443+218) | NPM (density fix) | Prácticamente igual a inject4. |
| npm2 (normales geométricos) | NPM + normal del triángulo | Más brillante (specular B3HD), Cell silueta, pero amorfo en boca/cola/manos/brazos. |
| npm3 (normales suavizados) | NPM + normal de vértice interpolado | Prácticamente igual; la mano mala mejoró "ligeramente". |
| **npm4 (1821+840)** | **NPM + normales + umbral ESTRICTO 0.8** | ✅ **MEJORA SIGNIFICATIVA**: torso, cabeza sup., cintura inf., piernas, pies, brazos, manos. Boca mejoró ligeramente. Sigue sin ser perfecto. |
| npm6 (2443+218) | NPM + normales + **soft[0.5,2.0]** | ❌ **DEFORMIDAD IMPORTANTE**: solo pies/cintura/piernas parcial. El soft reinyectaba las extremidades (0.5-2.0) con pesos parciales → posiciones a medias → estiradas. |
| npm7 (1821+840) | NPM + normales + **soft[0.3,0.8]** | ❌ PEOR que npm4: solo cabeza sup. normal. Los pesos parciales dentro del core (0.3-0.8) rompen las posiciones completas. |

### 5.2 🔴🔴 LECCIÓN: BINARIO SÍ, BLEND NO

Los dos tests de blend (npm6 y npm7) fueron PEORES que el binario npm4. **La
inyección completa de los slots bien alineados funciona; cualquier peso parcial
(blend) produce posiciones a medias (ni PS2 ni HD) → amorfo.** El umbral binario
es el mecanismo correcto. El único parámetro a afinar es el VALOR del umbral.

### 5.2 🔴 HALLAZGOS DEL ANÁLISIS (match distances por hueso)

El mismatch de forma entre el PS2 y el HD NO está distribuido uniformemente:

| Zona | bones | dist med | p90 | máx | ¿Alinea? |
|---|---|---|---|---|---|
| Cuerpo core (BODY/WAIST/CHEST/RCHN) | 0,1,15,19 | 0.33-0.75 | 1.0-1.2 | 1.7 | ✅ SÍ |
| Piernas (LLEG1/RLEG1/RFOOT) | 5,9,10,11 | 0.4-0.65 | ~1.0 | 2.4 | ✅ SÍ |
| OBI / rotación pierna | 3,4 | 0.9-1.17 | 3.3-4.3 | 4.9 | ❌ mal |
| bone 13/14 | 13,14 | 1.5-1.95 | 2.0-2.4 | 2.8 | ❌ mal |
| Manos/brazos (LHAND/RARM/RHAND) | 18,20,21 | 0.67-1.63 | 2.5-8.7 | **8.9** | ❌ MUY mal |
| Cabeza | 32,33 | 1.0-1.25 | 2.4-2.6 | 2.7 | ◑ medio |

**Conclusión**: el umbral 2.0 inyectaba las extremidades (distancia 2-8.9) con
posiciones mal emparejadas → estiradas → amorfo (boca/cola/manos/brazos). El
**umbral 0.8** solo toca el cuerpo bien alineado (core + piernas), dejando las
extremidades con la forma HD correcta → mejora significativa. **El umbral es el
parámetro crítico de la inyección.**

### 5.3 🔴 NORMALES — SEGUNDO HALLAZGO

La inyección v4/NPM solo escribía +12/+16/+20 (posiciones). Los normales
(+32/+36/+40) quedaban del HD → el sombreado seguía calculado para la forma HD
→ "polígonos deformes" con el specular roto. Al escribir los normales de la
superficie PS2:
- **Normal GEOMÉTRICO del triángulo** (cruz de aristas): el specular B3HD
  funciona (más brillante, silueta Cell clara) pero facetado → amorfo localizado
  (boca/cola/manos/brazos).
- **Normal de VÉRTICE interpolado** (baricéntrico, suavizado): mejora la mano
  mala "ligeramente"; el resto igual. El facetado no era la causa principal.

Formato normal HD: `[nz, -ny, nx]` (y negada) en +32/+36/+40. Verificado: 100%
unitarios (med 1.000, 0 fuera de [0.8,1.2]).

### 5.4 LO QUE QUEDA (perfecting)

- **Costuras**: las transiciones entre las zonas inyectadas (PS2) y las HD
  (extremidades) crean seam blends. Un umbral SUAVE (blend lineal por distancia)
  podría suavizarlas.
- **Cabeza/boca**: mismatch medio (1.0-1.25) → la boca mejoró solo ligeramente.
  Requiere alinear la cabeza o inyectarla con un umbral dedicado.
- El port completo (topología PS2 + arms reconstruidos) sigue siendo el objetivo
  final para reproducir el Cell PS2 exacto.

## 6. VERIFICACIÓN NUMÉRICA (para reproducción)

```
Template Cell F2 HD (bin 147): sec34=2661 slots (align +2), bones 0-33,
  world coherente con parent corregido.
Geometry PS2: 4938 expandidos → 1880 únicos (voxel 0.05) → 2661 slots con
  reuso de ~781.
Match distances (slot world ↔ superficie PS2): med 0.62, p90 2.02, max 8.91.
Umbral 2.0 → 276 slots HD | 0.8 → 840 slots HD | 1.2 → 560 slots HD.
NPM: 2908 triángulos PS2 (36 parts).
Inyección npm4 (umbral 0.8): 1821 inyectados + 840 HD.
```

---

**Enlaces**: `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md` (descriptores/mesh-ref/ejes/
arms), `docs/07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md`, AGENTS §15.