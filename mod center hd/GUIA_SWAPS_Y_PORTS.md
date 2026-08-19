# GUÍA DE MODEL SWAPS Y PORTS — DBZ Budokai 3 HD Collection

> 2026-08-17. Documento vivo para el proyecto B3. Consolida el conocimiento
> validado en el proyecto hermano **B1** (mismo runtime ReXGlue, MISMO formato
> HD) y lo adapta a B3. Antes de nada: **lea las lecciones del B1 en
> `C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai HD Collection\AGENTS.md`**
> (especialmente lecciones 8-15) — este documento es el resumen operativo.

---

## 1. RESUMEN EJECUTIVO

| Swap | Estado | Herramienta |
|---|---|---|
| **B1 → B1** (dentro del B1) | ✅ **100% FUNCIONAL** (Android 19 → Tenshinhan) | `swap_b1.py` (proyecto B1) |
| **B3 → B1** (port de modelos) | ✅ **100% FUNCIONAL** (Dr. Gero → Tenshinhan) | `install_b3_to_b1.py` + `launcher_mod_pipeline.py` (proyecto B1) |
| **B3 → B3** (dentro del B3) | 🔧 **Aplicable, requiere sistema de mods del B3** | Hoja de ruta §7 |
| **B1 → B3** (port inverso) | 🔬 No probado aún; mismo principio + conversión de sellos inversa | Hoja de ruta §7 |

**El hallazgo que lo cambió todo** (lección 9 del B1, 16/08): el runtime HD
**NO valida conteos fijos del slot**. Instala un bin `#AWO` COMPLETO de otro
personaje y el runtime lo dibuja tal cual (mesh group, IB, bones, UVs). Lo
único que exige: que **geom (`#AWO`/`#AMB`) y tex (`#AZT`) sean del MISMO
personaje**.

---

## 2. CÓMO FUNCIONAN LOS MODEL SWAPS (el principio)

### 2.1 Qué hace el runtime

Cuando el combate carga un personaje, el runtime lee sus bins del AFS y los
renderiza **sin reinterpretarlos**: usa el mesh group, el index buffer, los
bones y las UVs **que vienen dentro del bin instalado**. Por eso:

- **Swap de bin completo** (la vía correcta): el modelo nuevo se ve perfecto
  porque su topología/IB viajan en el bin.
- **Inyección parcial** (la vía vieja, DESCARTADA): sobrescribir solo las
  coordenadas del sec34 de un bin nativo → el runtime dibuja la **topología
  del anfitrión** sobre las coordenadas nuevas → **deformación**.
- **Reconstrucción desde cero con IB propio** (v20/v22 del B3): caos, el
  runtime usa su propio IB.

### 2.2 Requisitos del swap

1. **Par geom + tex del MISMO personaje**: el bin de geometría y su textura
   deben corresponder al mismo personaje. Un `#AWO` de X con `#AZT` de Y →
   **crash 0xC0000005** (mismatch de textura).
2. **Sellos del bin correctos** para el juego destino:
   - B1: flag AWG `+0x0C` = `0x2`; type2 mesh = `0x1BD`/`0x11BD`; sombra `0x190`.
   - B3: flag AWG `+0x0C` = `0x4`; type2 mesh = `0x29BD`.
   - Portar B3→B1 requiere convertir los sellos (ver §5).
3. **Materiales compatibles** (solo B3→B1): escala 4×128.0 + pesos
   `0.85/0.80/0.70/1.0` (torso) o `0.85/0.85/0.80/1.0` (extremidades) + type2
   `0x11BD` para shader con specular.
4. **Textura opaca** (solo B3→B1): el runtime B1 espera AZT con alpha DXT3 a
   `0xFF` (el B3 usa alpha variable → cuerpo negro).

### 2.3 El runtime anima por labels

El runtime B1/B3 anima **por coincidencia de labels** entre el `#AWO` (modelo)
y el `#ACM` (esqueleto) del slot. Los bones del AWO sin label coincidente
quedan en bind pose. Para el Gero portado al slot Tenshinhan, los labels del
rig no coinciden con el `#ACM` del slot → algunos bones (boca, pelo) no se
animan perfecto. **No bloquea el swap** (el modelo renderiza), pero limita la
animación.

---

## 3. EL FORMATO HD (igual en B1 y B3 — verificado 17/08)

### 3.1 Archivos del personaje (data_cmn.afs / data_sp.afs)

| Magic | Rol | B1 slots (ej. TSH) |
|---|---|---|
| `#ACM` | esqueleto + expresiones | 2445 |
| `#CCM` | comandos/moveset | 2446 |
| `#CSK` | tabla de animaciones (2037, mismas IDs en todos) | 2448 |
| `#AWO` | modelo (mesh group + IB + bones + UVs) | 2450 |
| `#AZT` | texturas | 2451 |

En **B3**, el modelo vive en un contenedor `#AMB` que incluye `#AWO` + `#AZT`
juntos (una sola entrada AFS). Verificado: el Gero B3 = bin 91 (`#AMB` con
`X20G_BODY`, 2501 verts, 16 AWGs, 46 bones).

### 3.2 Vértice sec34 (stride 44) — CORRECTO (B1 v10+, B3 igual)

```
+00 pos.x  +04 pos.y  +08 pos.z          (floats BE)
+12 weight (0.7/0.8/0.9/1.0)
+16 BONE index (u32, válido 1-46)
+20 nrm.x  +24 nrm.y  +28 nrm.z
+32 0xFFFFFFFF
+36 blend/scale
+40 uv
```
`n_sec = sec_size // 44`.

> ⚠️ ANTES usábamos un layout viejo `[nan,u,v,z,x,y,peso,bone,nz,-ny,nx]` (sesión
> 5 del B1). Fue CORREGIDO en la v10. **No usar el layout viejo.**

### 3.3 Offsets del header AWG0 (+0x50) — RELATIVOS al AWG0

```
+0x28 sec_off   → sec_abs = AWG0 + val     (n_sec = sec_size//44)
+0x2C sec_size
+0x30 post_off  → post_abs = AWG0 + val    (IB u16 + sub-mesh)
+0x34 post_size → n_ib = post_size//2
+0x38 siguiente zona (REL AWG0)
+0x3C bones count   +0x40 nombre (16B)
```

> ⚠️ Los scripts viejos del B3 leían `sc=+0x34`, `vb=+0x2C`, `ib=+0x30`
> (formato pre-v10). **CORREGIDO** en `awg_to_obj.py` (17/08).

### 3.4 Mesh group y arms

- Cada `#AWG` tiene: header (quat local, pos local, sello, arm_ptr, child/
  sibling/parent) + mesh parts (type2, stride, materiales).
- Los `arm` (20B: `[bone, fin, 0, ini, 0]`) apuntan a rangos del IB.
- Los mesh parts de sombra (`0x190`/`0x204`) marcan límites del IB.

---

## 4. EL CATÁLOGO DE PERSONAJES (base de todo swap)

El proyecto B1 escanea los AFS y genera un catálogo:
`mod center hd/cache/characters.cat`:

```
juego|label|nombre|slot_geom|slot_tex|slot_acm|slot_csk|verts|awgs
B1|XTSH_BODY|Tenshinhan|2450|2451|2449|0|4272|23
B3|X20G_BODY|Dr. Gero|91|0|0|0|2501|16
```

- **B1**: 26 personajes jugables (`XGOK_BODY`=Goku, `XTRX_BODY`=Trunks,
  `X19G_BODY`=Android 19...).
- **B3**: 56 personajes (`XGOK_BODY`, `XVGT_BODY`, `XPIC_BODY`, `XTSH_BODY`,
  `XFRZ_BODY`, `XCEL_BODY`...). El `slot_geom` del B3 = índice del `#AMB`.

Generación: `python launcher_mod_pipeline.py catalog`
(script del proyecto B1, reutilizable — apunta a ambos AFS).

---

## 5. CÓMO HACER LOS SWAPS (pipeline paso a paso)

### 5.1 Port B3 → B1 (VALIDADO: Gero → Tenshinhan)

```
python launcher_mod_pipeline.py port --b3 X20G_BODY --dest 2450 --tex 2451 --mod mi_port
```
1. Extrae el `#AMB` del bin B3 (91) y descomprime.
2. `extract_amb_awo.py` → `#AWO` + `#AZT` del AMB.
3. `install_b3_to_b1.py`:
   - `port_b3_to_b1_v2.py`: flag AWG `0x4→0x2`, type2 `0x29BD→0x1BD`/`0x11BD`,
     materiales B1 (escala 4×128, pesos, sombra `0x190`).
   - AZT alpha DXT3 → `0xFF`.
   - Compresión LZX `/N:2048` + padding al slot + round-trip verificado.
   - Instala en `mods/<mod>/us/data_sp.afs/<2450>/geom.bin` y `2451/tex.bin`
     y activa el mod.
4. Reiniciar el juego → el modelo nuevo renderiza en combate.

### 5.2 Swap B1 → B1 (VALIDADO: Android 19 → Tenshinhan)

```
python launcher_mod_pipeline.py swap --origen X19G_BODY --dest 2450 --tex 2451 --mod mi_swap
```
Extrae el par geom+tex del origen (49/48 o 45/46), comprime, padda y lo
instala en los slots del destino.

### 5.3 Swap B3 → B3 (el mismo principio, pendiente del sistema de mods)

El `#AMB` del B3 ya contiene AWO+AZT del MISMO personaje → un swap dentro del
B3 es **sustituir la entrada del AFS** (el AMB de X en la entrada de Y). El
runtime dibujará el AMB nuevo completo. Requisitos:
1. El B3 debe soportar override **por entrada AFS** (hoy solo tiene override
   por archivo completo — ver hoja de ruta §7).
2. Herramienta `swap_b3.py`: dado origen/destino (del catálogo B3), extrae el
   AMB del origen, comprime LZX `/N:2048`, padda al tamaño del slot destino e
   instala en `mods/<mod>/us/data_cmn.afs/<dest>/geom.bin`.

### 5.4 Port B1 → B3 (inverso, no probado)

Mismo principio con conversión inversa de sellos:
- flag AWG `0x2→0x4`, type2 `0x1BD/0x11BD→0x29BD`, materiales B3 (escala 1.0),
  AZT con alpha variable (no forzar a 0xFF).
- El par geom (`#AWO` B1) + tex (`#AZT` B1) del MISMO personaje → empaquetar en
  `#AMB` o instalar por entrada.

---

## 6. HERRAMIENTAS DEL PROYECTO B3 — ESTADO Y DIAGNÓSTICO

### 6.1 Tabla de estado (17/08)

| Herramienta | Estado | Problema |
|---|---|---|
| `awg_to_obj.py` | ✅ **ARREGLADA** (17/08) | Usaba offsets viejos del header (`+0x34`/`+0x2C`/`+0x30`) y layout viejo del vértice (`nan,u,v,z,x,y...`). Corregida a `+0x28..+0x34` y layout v10+. Detecta `#AWO` directo o `#AMB`. Verificado: Gero → 2501 verts / 5443 IB / 1814 caras. |
| `obj_to_awg.py` | 🔧 requiere fix | Mismo bug de offsets/layout. Además, la vía de retopología que usa quedó **superada** (no hace falta si usas swap nativo). |
| `build_awo_v20/v22.py` | ❌ superado | Intentaba reconstruir con "conteos fijos" (sec34=1956, IB=5140). El runtime NO exige conteos fijos (lección 9). |
| `build_awo_from_json.py` | ❌ superado | Retargeting binario con matrices → shear/deformación (lecciones 10-12). |
| `inject_a18.py` / `inject_a18_v21.py` | ❌ superado | Inyección parcial de coordenadas → deforma (el runtime usa su topología). |
| `empaquetar_v20.py` | ❌ superado | Empaca sec34/vb2/ib viejos. |
| `emd_to_awo_hd.py` | 🔬 incompleta | Parsing de EMD SDBH a medias; la vía EMD ya no es necesaria (el B3 HD ya tiene los modelos en `#AMB`). |
| `json_to_obj.py` / `fbx_*.py` | 🔬 helper | Utilidades auxiliares para el flujo viejo. |

### 6.2 Bug común de TODOS los scripts viejos

1. **Offsets del header AWG0 equivocados** (lección 8 del B1): leían el sec34
   desde `+0x34` (que en realidad es `post_size`). El header correcto es:
   `sec_off=+0x28`, `sec_size=+0x2C`, `post_off=+0x30`, `post_size=+0x34`,
   **relativos al AWG0**.
2. **Layout del vértice equivocado** (lección 5→v10): el layout correcto es
   `pos(0/4/8) weight(12) bone(16) nrm(20/24/28) 0xFFFFFFFF(32) blend(36)
   uv(40)`.

---

## 7. HOJA DE RUTA PARA HACER FUNCIONAL EL B3 (orden sugerido)

### Paso 1 — Sistema de mods del B3 (override por entrada AFS) [imprescindible]

Hoy el B3 solo reemplaza **archivos completos** (`PrepareRegionData` copia
`mods/<mod>/us/<archivo>` sobre el `us/`). Para model swaps se necesita el
override **por entrada AFS** que ya tiene el B1.

Opciones:
- **A (rápida)**: copiar `src/mods.cpp`/`mods.h` del B1 al B3 + la UI de la
  pestaña Mods del B1. El override de entradas vive en el SDK
  (`rexglue-sdk/src/filesystem/devices/host_path_file.cpp` →
  `AfsFindModOverride`) — si el B3 usa el mismo SDK con ese cambio, el hook ya
  funciona; hay que verificar que `rexruntime.dll` del B3 lo incluya.
- **B (sin recompilar el SDK)**: empaquetar el AFS con la entrada sustituida
  (tools de AFS packer) y usar el override por archivo completo existente.
  Más lento de iterar, pero no toca el runtime.

### Paso 2 — Herramienta `swap_b3.py` (swap dentro del B3)

Nuevo script (o extensión de `launcher_mod_pipeline.py`):
- `catalog --b3` (ya existe → 56 personajes).
- `swap3 --origen <label> --dest <bin>`: extrae el `#AMB` del origen del
  `data_cmn.afs` del B3, comprime `/N:2048`, padda al tamaño del slot destino
  e instala en `mods/<mod>/us/data_cmn.afs/<bin>/geom.bin`.
- Validar con un swap conocido (ej. Android 19 → Krillin) en runtime.

### Paso 3 — Corregir el resto de extractores del B3

- `obj_to_awg.py`: aplicar el mismo fix de offsets/layout que `awg_to_obj.py`
  (útil si algún día se quiere retopología manual, aunque ya no es la vía).
- Marcar los scripts `build_awo_*`/`inject_*` como obsoletos (moverlos a
  `mod center hd/obsoletos/`).

### Paso 4 — Port B1 → B3 (inverso)

Crear `port_b1_to_b3.py`:
- Conversión inversa de sellos (flag `0x2→0x4`, type2 `0x1BD→0x29BD`,
  materiales B3 escala 1.0, AZT con alpha original).
- Empaquetar AWO+AZT B1 en `#AMB` o instalar por entrada.
- Probar con un personaje B1 jugable (ej. Tenshinhan HD) en un slot B3.

### Paso 5 — Integrar todo en el launcher del B3

Portar la pestaña "Mods → Model pipeline" del B1 (catálogo + combos + botón
portar/swapar) al B3. Reutilizar `mod_pipeline.{h,cpp}` del B1.

### Paso 6 — (Opcional) Ports de movesets

El moveset del B3 → B1 se descartó (lección 13: el `#ACM` del slot no es
sustituible sin RE completa de sus poses). Para swaps de modelos esto NO es
necesario.

---

## 8. REFERENCIAS

- **Metodología de swaps** (B1): `docs/tutoriales/MODEL_SWAPS_METODOLOGIA.md`.
- **Sesión del port Gero B3→B1**: `docs/re/SESION10_PORT_B3_B1_FUNCIONAL.md`.
- **Sesión de swaps B1→B1**: `docs/re/SESION9_MODEL_SWAPS_B1_B1.md`.
- **Animaciones/movesets HD**: `docs/re/ANIMACIONES_MOVESETS_HD.md`.
- **Lecciones clave**: `AGENTS.md` (lecciones 1-15) del proyecto B1.
- **Catálogo de personajes**: `mod center hd/cache/characters.cat` (B1).
- **Pipeline funcional (B1)**: `mod center hd/launcher_mod_pipeline.py`,
  `mod center hd/swaps/swap_b1.py`,
  `mod center hd/conversores/install_b3_to_b1.py`.
