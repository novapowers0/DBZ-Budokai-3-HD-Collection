# 04_FORMATO_RE — Layout de vértice y espacio de los 16 AWGs auxiliares (huesos 48–63)

> Fecha: 2026-09-10. Autor: ingeniero RE (opencode).
> Bin analizado: `e147.bin` (#AMB, BE, 715872 B) = Cell (Semi-Perfect) slot 147.
> Fuente PS2: `cell_extract2.json` (36 `parts`, 48 labels, `skin`, `worlds`).
> **Alcance**: trabajo empírico con scripts temporales en `%TEMP%\opencode\phaseb\`.
> No se ha modificado ningún fichero del repo. Se reutilizan `awo_tools\awg0_export.py`
> (`detect_format`) y `mod center hd\ports\port_ps2_b3_inject.py` (`world_mats`,
> `closest_point_triangles`, `barycentric`).

---

## 0. RESUMEN EJECUTIVO

1. **Corrección de premisa**: de los 16 AWGs de 1 hueso (huesos globales 48–63) **NO los 16 son
   de cara**. Son **10 de manos** (48–57) + **6 de cara** (58–63):
   - 48–52 `CEL_L01/L02/L04/L05/L10_LHAND`, 53–57 `CEL_L01/L02/L04/L05/L10_RHAND`.
   - 58–63 `XCEL_L01/L18/L09/L04/L05/L06_FACE`.
2. **6 familias de layout** (stride 44 en todas) con el marcador `0xFFFFFFFF` en +0, +12, +20,
   +24, +28 o +32; peso/pad en posiciones distintas; **la posición se detecta por la bbox del
   arm** (`arm field[3]` = 6 floats min/max) → identifica las 3 columnas de posición **y su
   orden de componentes**.
3. **Espacio confirmado**: los 16 AWGs tienen ejes **identidad** (sin rotación) y sus vértices
   están en el espacio **local del hueso padre**:
   - manos izquierdas → `world[23]` (CEL_L00_LHAND) — distancia media al surface PS2 **0.12**.
   - manos derechas → `world[30]` (CEL_L00_RHAND) — **0.12**.
   - cara → `world[32]` (CEL_HEAD; identidad + traslación y=8.466) — **0.01–0.09**.
   (vs. identidad 0.22–0.26 y huesos vecinos 0.44–0.58 → hipótesis del enunciado **verificada**).
4. **La geometría HD ya está a <0.1 u del surface PS2** → el HD es una **re-malla más densa del
   mismo modelo PS2**. Reescribir posiciones con *nearest-point-on-surface* es por tanto de
   **bajo riesgo** (no es reconstruir topología, es una proyección local fina).
5. **Mapeo PS2→HD**: las 3 `parts` de mano PS2 (`bone 23` / `bone 30`) alimentan **5 AWGs HD cada
   una**; la cara PS2 `XCEL_L00_FACE` (`bone 40`) alimenta **los 6 AWGs de cara**. Los dientes PS2
   (`b36`/`b38`) **no tienen AWG HD de 1 hueso dedicado** en este bin.

---

## 1. MÉTODO DE DETECCIÓN DE LAYOUT

Para cada AWG (índices 0–16):

1. Leer la cabecera: `n_bones +0x10`, `axes +0x14`, `mg +0x20`, `sec +0x34`, `ib +0x30`,
   `end +0x38`.
2. El buffer de vértices de los AWGs de 1 hueso ocupa **[sec, ib)** con **stride 44** y **un
   `0xFFFFFFFF` por registro**. Se busca el par `(r, mo)` (desplazamiento de inicio de registro
   `r∈[0,43]` y offset del marcador `mo`) que hace que **el 100 % de los registros** tenga el
   marcador. El nº de vértices = nº de marcadores.
3. Identificar columnas por invariantes estadísticas sobre todos los registros:
   - **peso**: columna == `1.0` en el 100 %;
   - **pad**: columna == `0.0` en el 100 %;
   - **normal**: triple de floats consecutivos con |n|≈1 en ≈el 100 %;
   - **posición**: las 3 columnas cuyo `(min,max)` coincide exactamente con
     `arm field[3]` (6 floats = min/max). El orden de componentes (x,y,z) lo da ese mismo match.
   - **uv**: las 2 columnas restantes (valores en [0,≈2]).
4. Verificación del orden de componentes de la normal: se comparó cada permutación de la terna
   con la normal PS2 proyectada; **la permutación identidad (0,1,2) gana siempre** (dot 0.45–0.92),
   es decir, la normal se escribe en el **mismo orden que aparecen sus offsets** y **sin
   negación/swizzle** (a diferencia del sec34 del AWG0, que usa `[nz,-ny,nx]`).

> Reutilización: `awg0_export.detect_format` solo cubre A/C del AWG0; para estos 16 ha hecho falta
> un detector por invariantes (arriba). El `arm field[3]` (bbox) es la pieza clave: da la lista y
> el orden de las columnas de posición sin ambigüedad.

---

## 2. TABLA GENERAL DE LOS 17 AWGs

Offsets relativos al inicio del `#AWG`. `reg.sec` = `sec_rel..ib_rel`. `marker/weight/pad/normal/
uv/pos` en bytes dentro del registro de 44 B. `r` = bytes de retardo del primer registro
(align). Todos BE.

| AWG | hueso | label | n_vert | reg.sec | marker | weight | pad | normal (x,y,z) | uv (u,v) | pos (x,y,z) | r |
|----:|:-----:|:------|------:|--------:|-------:|-------:|----:|:--------------:|:--------:|:-----------:|:-:|
| AWG0 | 0–47 (cuerpo) | XCEL_BODY… | 2661 | +0x313A..+0x1FAB0 | +0 | +24 | — | +32/+36/+40 | +4/+8 | z+12/x+16/y+20 | +2 |
| AWG1 | 48 | CEL_L01_LHAND | 192 | +0x330..+0x2430 | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |
| AWG2 | 49 | CEL_L02_LHAND | 196 | +0x390..+0x2564 | +24 | +4 | +8 | +12/+16/+20 | +28/+32 | +36/+40/+0 | 0 |
| AWG3 | 50 | CEL_L04_LHAND | 187 | +0x420..+0x245C | +12 | +36 | +40 | +0/+4/+8 | +16/+20 | +24/+28/+32 | 0 |
| AWG4 | 51 | CEL_L05_LHAND | 167 | +0x330..+0x1FE4 | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |
| AWG5 | 52 | CEL_L10_LHAND | 214 | +0x4B0..+0x2984 | +0 | +24 | +28 | +32/+36/+40 | +4/+8 | +12/+16/+20 | 0 |
| AWG6 | 53 | CEL_L01_RHAND | 196 | +0x330..+0x24E0 | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |
| AWG7 | 54 | CEL_L02_RHAND | 200 | +0x390..+0x2614 | +24 | +4 | +8 | +12/+16/+20 | +28/+32 | +36/+40/+0 | 0 |
| AWG8 | 55 | CEL_L04_RHAND | 191 | +0x420..+0x250C | +12 | +36 | +40 | +0/+4/+8 | +16/+20 | +24/+28/+32 | 0 |
| AWG9 | 56 | CEL_L05_RHAND | 167 | +0x330..+0x1FE4 | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |
| AWG10 | 57 | CEL_L10_RHAND | 214 | +0x4B0..+0x2984 | +0 | +24 | +28 | +32/+36/+40 | +4/+8 | +12/+16/+20 | 0 |
| AWG11 | 58 | XCEL_L01_FACE | 190 | +0x3E4..+0x24B4 | +28 | +8 | +12 | +16/+20/+24 | +32/+36 | +40/+0/+4 | 0 |
| AWG12 | 59 | XCEL_L18_FACE | 173 | +0x3C0..+0x219C | +20 | +0 | +4 | +8/+12/+16 | +24/+28 | +32/+36/+40 | 0 |
| AWG13 | 60 | XCEL_L09_FACE | 203 | +0x438..+0x271C | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |
| AWG14 | 61 | XCEL_L04_FACE | 198 | +0x40E..+0x2640 | +28 | +8 | +12 | +16/+20/+24 | +32/+36 | +40/+0/+4 | **2** |
| AWG15 | 62 | XCEL_L05_FACE | 194 | +0x40E..+0x2590 | +28 | +8 | +12 | +16/+20/+24 | +32/+36 | +40/+0/+4 | **2** |
| AWG16 | 63 | XCEL_L06_FACE | 203 | +0x438..+0x271C | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0/+4/+8 | 0 |

Notas:
- `AWG0` se incluye de referencia; su sec34 usa el **formato A** documentado en
  `AGENTS §3.2` (con align `+2`, bone en `+28`, normal `[nz,-ny,nx]`). Su buffer de vértices
  termina en **`vb2`** (`+0x1FAB0`), NO en `ib`; y sus ejes NO son identidad (esqueleto real).
- **AWG14/15 tienen `r=2`**: el primer registro empieza en `AWG+sec_rel+2`.
- Los AWGs de 1 hueso **no tienen campo `bone` en el vértice**: el hueso está implícito en el AWG
  (se lee en el arm: struct de 5 u32 en `awg + be32(axis+0x34)`, `struct[0]` = hueso global;
  `struct[3]` = offset relativo al AWG de la bbox min/max).
- Los ejes de los 1 hueso (`awg + be32(awg+0x14)`, registro de 80 B) tienen **cuaternión
  identidad y traslación 0**.

---

## 3. FAMILIAS DE LAYOUT

| Familia | AWGs | marker | weight | pad | normal | uv | pos (x,y,z) |
|:-------:|:-----|:------:|:------:|:---:|:------:|:--:|:-----------:|
| **F1** | 1, 4, 6, 9, 13, 16 | +32 | +12 | +16 | +20/+24/+28 | +36/+40 | +0 / +4 / +8 |
| **F2** | 2, 7 | +24 | +4 | +8 | +12/+16/+20 | +28/+32 | +36 / +40 / +0 |
| **F3** | 3, 8 | +12 | +36 | +40 | +0/+4/+8 | +16/+20 | +24 / +28 / +32 |
| **F4** | 5, 10 | +0 | +24 | +28 | +32/+36/+40 | +4/+8 | +12 / +16 / +20 |
| **F5** | 11, 14, 15 | +28 | +8 | +12 | +16/+20/+24 | +32/+36 | +40 / +0 / +4 |
| **F6** | 12 | +20 | +0 | +4 | +8/+12/+16 | +24/+28 | +32 / +36 / +40 |

Estructura observada: el registro es una **rotación de una plantilla de 10 campos**
(`pos(3) + weight + pad + normal(3) + marker + uv(2)`), con el punto de arranque distinto por
AWG (el declarador de vértices 360 admite orden libre). F3 y F6 son las más "canónicas"; F5 es
una permutación de F6 (pos `(x=+40,y=+0,z=+4)`).

---

## 4. ESPACIO/MUNDO — VERIFICACIÓN CUANTITATIVA

Para cada AWG se leyó su geometría local (con el layout de §3), se aplicó cada matriz world
candidata y se midió la distancia media al **surface PS2 completo** (2908 triángulos, model
space). Menor = mejor.

**AWGs de cara (11–16), distancia media al surface PS2:**

| AWG | world[32] CEL_HEAD | world[0] (identidad) | world[23] LHAND | world[30] RHAND |
|----:|:------------------:|:--------------------:|:---------------:|:---------------:|
| 11 | **0.09** (max 0.59) | 0.25 | 0.54 | 0.44 |
| 12 | **0.01** (max 0.07) | 0.23 | 0.58 | 0.47 |
| 13 | **0.01** (max 0.06) | 0.22 | 0.54 | 0.43 |
| 14 | **0.09** (max 0.66) | 0.24 | 0.55 | 0.44 |
| 15 | **0.09** (max 0.54) | 0.24 | 0.55 | 0.44 |
| 16 | **0.03** (max 0.18) | 0.22 | 0.56 | 0.46 |

**AWGs de manos** (muestra; `world[23]` gana en izquierda y `world[30]` en derecha):

| AWG | world[23] | world[30] | world[32] | world[0] |
|----:|:---------:|:---------:|:---------:|:--------:|
| 1 (L01) | **0.12** | 0.16 | 0.25 | 0.26 |
| 5 (L10) | **0.16** | 0.22 | 0.34 | 0.24 |
| 6 (R01) | 0.16 | **0.12** | 0.25 | 0.26 |
| 10 (R10) | 0.22 | **0.15** | 0.31 | 0.21 |

**Conclusión**: `world[32]` (CEL_HEAD) es **el** espacio de los 6 AWGs de cara (hipótesis del
enunciado confirmada); `world[23]`/`world[30]` para las manos. `world[32]` tiene rotación
identidad y traslación `(0, 8.466, 0)`; por tanto para la cara `local = model − (0, 8.466, 0)`.

Centroides model-space resultantes (aplicando el world del padre):

| AWG | local centroid | model centroid | PS2 de referencia |
|----:|:--------------:|:--------------:|:------------------|
| 1–5 | ≈ (0.8–1.1, 0, 0) | ≈ (10.3–10.6, 6.2–6.4, 0.05) | b23 LHAND c=(11.5,6.1,0.1) |
| 6–10 | idem | ≈ (−10.3..−10.6, 6.2–6.4, 0.05) | b30 RHAND c=(−11.5,6.1,0.1) |
| 11–16 | ≈ (0.04, 0.75, 1.14) | ≈ (0.04, 9.2, 1.14) | b40 FACE c=(0,9.5,1.13)/(0,9.0,1.04) |

---

## 5. GEOMETRÍA PS2 DE LOS HUESOS 32–41 (`cell_extract2.json`)

`parts` con hueso 32–41 en **model space**. Sólo 3 huesos tienen `parts`; el resto (33,34,35,37,39,41)
no tienen geometría propia separada (sus vértices viven dentro de `b40` y/o `b0`).

| part idx | hueso | label PS2 | n_vert | centroide (model) | notas |
|:--------:|:-----:|:----------|------:|:------------------|:------|
| 29 | 36 | XCEL_M_DTEETH | 58 | (0.03, 8.38, 1.24) | dientes inferiores |
| 30 | 36 | XCEL_M_DTEETH | 54 | (0.00, 8.41, 1.10) | dientes inferiores |
| 31 | 38 | XCEL_M_UTEETH | 54 | (−0.01, 8.46, 1.24) | dientes superiores |
| 32 | 40 | XCEL_L00_FACE | 160 | (0.00, 9.47, 1.13) | **cara principal** |
| 33 | 40 | XCEL_L00_FACE | 22 | (−0.01, 8.81, 1.42) | parche pequeño (frente/nariz) |
| 34 | 40 | XCEL_L00_FACE | 94 | (−0.02, 8.97, 1.04) | cara baja (boca/mentón) |
| 35 | 47 | CEL_T_TAIL6 | 94 | (−0.04,−12.06,−7.69) | cola (no cara) |

- **33 XCEL_M_JAW, 34/37 LMOUTH1/2, 35/39 RMOUTH1/2, 41 XCEL_NH**: **sin `parts`** en el
  extract (la malla de la boca/labios está dentro de `b40`; los dientes son `b36`/`b38`).
- Manos PS2: `b23` ×3 parts (166+130+44) y `b30` ×3 parts (180+126+44).

---

## 6. MAPEO PROPUESTO PS2 → AWGs HD

La relación **no es 1:1**: el HD subdivide el mismo surface PS2 en más sub-mallas (5 por mano,
6 por cara). El mapeo se hace **por región de superficie**, no por cuentas de vértices.

### 6.1 Tabla de mapeo (HD ← PS2)

| AWG HD | hueso HD | label HD | world (padre) | Región PS2 objetivo | Evidencia (dist. media) |
|:------:|:--------:|:---------|:-------------:|:--------------------|:-----------------------:|
| 1–5 | 48–52 | CEL_L01/L02/L04/L05/L10_LHAND | `world[23]` | `bone 23` (b23, 3 parts) | 0.12–0.16 |
| 6–10 | 53–57 | CEL_L01/L02/L04/L05/L10_RHAND | `world[30]` | `bone 30` (b30, 3 parts) | 0.12–0.15 |
| 11, 14, 15 | 58, 61, 62 | XCEL_L01/L04/L05_FACE | `world[32]` | `bone 40` (b40) | 0.09 |
| 12, 13 | 59, 60 | XCEL_L18/L09_FACE | `world[32]` | `bone 40` (b40) | 0.01 |
| 16 | 63 | XCEL_L06_FACE | `world[32]` | `bone 40` (b40) | 0.03 |

### 6.2 Justificación

- **Nombre de label**: `***_LHAND`/`***_RHAND` fija el lado (los 10 de mano); `XCEL_***_FACE`
  fija la cara. Coincide con las etiquetas PS2 `CEL_L00_LHAND` (b23), `CEL_L00_RHAND` (b30),
  `XCEL_L00_FACE` (b40).
- **Posición/centroide**: los 10 AWGs de mano caen en `x≈±10.5, y≈6.3, z≈0` (las 6 parts PS2 de
  mano), y los 6 de cara en `(0, 9.2, 1.14)` (b40). Ver §4.
- **Bone-aware / nearest-surface**: el 100 % de los vértices de manos empareja con triángulos PS2
  de `bone 23`/`bone 30`; los de cara con `bone 40` (nunca con dientes 36/38). Confirma que la
  división HD↔PS2 es por región.

### 6.3 Hipótesis semántica de los 6 AWGs de cara (best-effort)

Los 6 comparten bbox y centroide; **no se pueden separar por posición**. Por **protrusión**
respecto al surface PS2 (`dist > 0.10`) se propone:

| AWG HD | label | dist media | perfil | hipótesis |
|:------:|:------|:----------:|:-------|:----------|
| 12 | XCEL_L18_FACE | 0.01 | sobre el surface | **piel/cara principal** |
| 13 | XCEL_L09_FACE | 0.01 | sobre el surface | **piel/cara principal** (2ª capa) |
| 11 | XCEL_L01_FACE | 0.09 | 55 verts +z (0.06–0.09) | **detalle** (cejas/pestañas/ojos) |
| 14 | XCEL_L04_FACE | 0.09 | 59 verts +z | **detalle** |
| 15 | XCEL_L05_FACE | 0.09 | 53 verts +z | **detalle** |
| 16 | XCEL_L06_FACE | 0.03 | 14 verts centrales y 0.5–0.9, \|x\|<0.6, z 1.0–1.4 | **boca/nariz** (parche central) |

> La asignación exacta ojos/boca/dientes **requiere el material/textura** (índice de material del
> mesh-group → bloque `#AZT`; p.ej. AWG16 contiene la tabla `#AZT` con varios `DDS|DXT3`). La
> geometría sola no la determina. Los dientes PS2 (`b36`/`b38`) no tienen AWG HD dedicado en este
> bin: hay que buscarlos en el `vb2`/AGW0 o tratarlos aparte.

---

## 7. ESPECIFICACIÓN DE IMPLEMENTACIÓN

Objetivo: reescribir **sólo posición y normal** de los 16 AWGs auxiliares a partir del surface PS2
(*nearest-point-on-surface*), convirtiendo a bone-local del padre. **No** tocar weight, pad,
marker, uv, IB, descriptores ni arms (se conserva la plantilla, igual que la Vía A).

### 7.1 Datos de entrada / constantes

- Plantilla `templ` (#AMB) y `extract` PS2 (`parts` + normales de vértice por cara).
- `world, AWG0 = world_mats(templ, 0x40)` (de `port_ps2_b3_inject.py`).
- Mapa AWG → hueso padre (world):
  - AWG 1–5 → 23; AWG 6–10 → 30; AWG 11–16 → 32.
- Mapa AWG → región de surface PS2 (conjunto de `p['bone']`):
  - manos izq `{23}`; manos der `{30}`; cara `{40}` (opcional `{36,38,40}` si se quieren dientes).
- Familias de layout (§3) con: `r` (retardo de inicio), `mo` (marker), `pos[3]` (offsets x,y,z en
  orden), `nrm[3]` (offsets n x,y,z en orden), `weight`, `pad`.
- Umbral `THR`: cara 0.8, manos 1.0 (holgado; los errores reales son ≤0.66). Si `d>THR` se
  **conserva** la posición HD original (evita saltos en huecos del surface PS2).

### 7.2 Surface PS2 por región

```python
def build_region_surface(extract, bones):
    T, N = [], []
    for p in extract['parts']:
        if p['bone'] not in bones: continue
        V  = [v[1:4] for v in p['verts']]   # model-space
        NV = [v[4:7] for v in p['verts']]
        for (a,b,c) in p['tris']:
            T.append([V[a], V[b], V[c]])
            N.append([NV[a], NV[b], NV[c]])
    return np.array(T), np.array(N)          # (tri,3,3), (tri,3,3)
```

### 7.3 Algoritmo de inyección por AWG

```python
world, AWG0 = world_mats(templ, 0x40)
HOST = {1:23,2:23,3:23,4:23,5:23, 6:30,7:30,8:30,9:30,10:30,
        11:32,12:32,13:32,14:32,15:32,16:32}
REGION = {23:{23}, 30:{30}, 32:{40}}         # región de surface por padre

awg_tbl = 0x40 + be32(templ, 0x40 + 0x1C)
def awg_off(i): return 0x40 + be32(templ, awg_tbl + i*4)

for i, host in HOST.items():
    awg   = awg_off(i)
    lay   = LAYOUT[i]                         # de la tabla §3 (r, mo, pos[3], nrm[3])
    sec   = awg + be32(templ, awg + 0x34) + lay['r']
    ib    = awg + be32(templ, awg + 0x30)
    n     = (ib - sec) // 44                  # nº de registros (ver nota)

    T, N  = build_region_surface(extract, REGION[host])
    W     = world[host]                       # 4x4 local->model
    Rm, tm = W[:3,:3], W[:3,3]
    invW  = np.linalg.inv(W)

    for k in range(n):
        o = sec + k*44
        # 1) leer posición local (orden x,y,z de layout)
        p_loc = np.array([be_f(templ, o+lay['pos'][0]),
                          be_f(templ, o+lay['pos'][1]),
                          be_f(templ, o+lay['pos'][2])])
        p_mod = Rm.dot(p_loc) + tm
        # 2) nearest-point-on-surface (model space)
        cp   = closest_point_triangles(p_mod, T)
        d2   = ((cp - p_mod)**2).sum(1); kk = int(np.argmin(d2))
        d    = float(np.sqrt(d2[kk]))
        if d > THR:            # conservar HD
            continue
        target_mod = cp[kk]
        # 3) normal PS2 interpolada (model space)
        a,b,c = T[kk,0], T[kk,1], T[kk,2]
        u,v,w = barycentric(target_mod, a, b, c)
        n_mod = u*N[kk,0] + v*N[kk,1] + w*N[kk,2]
        n_mod /= (np.linalg.norm(n_mod) + 1e-12)
        # 4) convertir a local y escribir
        lc  = invW.dot(np.append(target_mod, 1.0))
        for c in range(3):
            f32i(templ, o + lay['pos'][c], float(lc[c]))
        n_loc = invW[:3,:3].dot(n_mod)       # sólo rotación
        for c in range(3):
            f32i(templ, o + lay['nrm'][c], float(n_loc[c]))
```

### 7.4 Puntos críticos / reglas

1. **No tocar** `weight`, `pad`, `marker`, `uv`, ni el `ib`/`end`/descriptores/arms: la Vía A
   conserva toda la estructura de dibujo. Sólo se sobreescriben 3 floats de posición y 3 de normal
   por vértice, dentro del propio registro de 44 B (no cambia tamaños → no rompe slots AFS).
2. **El orden de componentes** es el detectado (pos de la bbox; normal identidad). **No** aplicar el
   swizzle del sec34 del AWG0 (`[nz,-ny,nx]`): aquí se escribe `(nx,ny,nz)` tal cual.
3. **Normal**: rotar con `invW[:3,:3]` **sin traslación** y renormalizar. El orden de escritura es
   `nrm[0],nrm[1],nrm[2]` (identidad verificada). No hay negación de `y`.
4. **AWG14/15**: recordar `r=2` (primer registro en `sec_rel+2`).
5. **n**: usar `(ib−sec)//44` cuando sea exacto; si no (p.ej. AWG2 deja 36 B finales), usar el
   **contador de marcadores** `FFFF` en `mo` para no inventar un registro parcial.
6. **Umbral**: si `d>THR` conservar posición y normal HD originales (evita el "estirado" en huecos).
7. **Compresión posterior** (si se empaqueta como override): LZX `/N:2048` y padding al `to_read`
   del slot (reglas AFS de `AGENTS §6`).
8. **Verificación sin abrir el juego**: exportar a OBJ con `awo_tools/awg_to_obj_b3.py` /
   `awg_cara_export.py` y comprobar bounds/NaN antes de empaquetar. Un test binario útil:
   para cada AWG, recomputar la distancia media al surface PS2 tras la inyección; debe quedar ≈0
   (salvo los vértices descartados por umbral).

---

## 8. VIABILIDAD

- **Alta**. La geometría HD ya está a <0.1 u del surface PS2 bajo `world[32]/[23]/[30]`; la
  inyección no cambia tamaños ni topología, sólo mueve posiciones/normales locales. Es el mismo
  esquema de la **Vía A** que ya funciona (inyección sobre `sec34`), extendido a los 16 AWGs.
- **Riesgo principal**: los 6 AWGs de cara comparten bbox y centroide; una proyección ciega puede
  solapar capas (piel/ojos/boca) sobre la misma superficie. Mitigación: conservar la componente
  local no proyectada (o la normal) para mantener la "capa" y proyectar sólo en la dirección
  tangente; y, si se quiere semántica fina, leer el **índice de material → `#AZT`** de cada
  mesh-group (pendiente, fuera de este análisis).
- **Pendiente para cerrar cara/dientes**: localizar la geometría de dientes (b36/b38) en el HD
  (probablemente en el `vb2`/AWG0 o en `#AZT` de AWG16) y mapearla aparte.
- **Reutilización directa**: el bucle es prácticamente un clon de `npm_surface_mapping` +
  `npm_boneaware_mapping` de `port_ps2_b3_inject.py`, aplicado a los 16 AWGs con su layout y su
  `world` de padre en lugar de al `sec34` del AWG0.

---

### Referencias cruzadas
- `docs/07_ports/PLAN_PS2_B3/01_WEB.md`, `02_MODS_INVENTARIO.md`, `03_DOCS.md`.
- `docs/07_ports/SESION_INYECCION_2026-08-26.md` (Vía A), `ESTRUCTURA_DIBUJO_HD.md`,
  `AGENTS §3.2/§3.4/§10`.
- Instrumentos: `awo_tools/awg0_export.py`, `awo_tools/awg_to_obj_b3.py`,
  `mod center hd/ports/port_ps2_b3_inject.py`.
- Scripts temporales de este análisis (`%TEMP%\opencode\phaseb\`): `ana.py`, `markers.py`,
  `detect2.py`, `final.py`, `report_data.py`, `step2.py`–`step8.py`, `table_out.py`.
