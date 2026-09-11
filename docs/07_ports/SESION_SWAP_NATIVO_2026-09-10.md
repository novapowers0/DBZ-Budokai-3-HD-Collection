# SESIÓN 2026-09-10 — SWAP NATIVO B3 HD→B3 HD VALIDADO (Cell Forma 2 → Krillin)

> Resultado verificado en juego por el usuario: **100% funcional** (incluida la
> boca). Mod `cell_native`. Herramienta `mod center hd/swap_b3.py`.
> Este documento cierra el arco "Cell en el slot de Krillin" y **deslinda** lo
> que es un *swap nativo HD→HD* (resuelto) de un *port PS2→HD* (pendiente).

---

## 0. RESULTADO

- **Cell Forma 2 HD (bin 147) renderiza perfecto en el slot de Krillin (327)**,
  con todas las funciones (boca incluida). Sin peros.
- Mod generado: `out/build/win-amd64-release/mods/cell_native/`
  (`us/data_cmn.afs/327/geom.bin`, override por entrada, ~120 KB).
- Verificación binaria: el `geom.bin` descomprimido es **idéntico** (MD5) al bin
  origen 147 extraído de `us/data_cmn.afs`.

---

## 1. QUÉ ES Y QUÉ NO ES (crítico)

| | Swap nativo B3 HD→B3 HD | Port PS2→B3 HD |
|---|---|---|
| Qué mueve | El `#AMB` **COMPLETO** (AWO+AZT) de un personaje HD a otro slot | Geometría del modelo **PS2** hacia el bin HD |
| Topología | La del propio bin (viaja con él) | La del HD (inyección) o reconstruida (Vía B) |
| Estado | ✅ **100% FUNCIONAL** | ⛔ Vía A deforma · Vía B bloqueada |
| Uso | Cualquier personaje que **ya exista en HD** | Personajes que **solo** existen en PS2/IW |

> **Lo conseguido hoy es un swap nativo HD→HD, NO una conversión PS2→HD.**
> Cell Forma 2 ya existía en HD (entrada 147); lo hemos colocado en el slot de
> Krillin. Por eso sale perfecto: el runtime dibuja el bin tal cual.

---

## 2. RECETA REPRODUCIBLE

```powershell
# 0) Listar el catálogo (bin = índice de entrada AFS)
python "mod center hd\swap_b3.py" --list

# 1) Swap nativo: bin ORIGEN -> slot DESTINO
python "mod center hd\swap_b3.py" --origen 147 --dest 327 --mod cell_native
```

- **Origen/destino**: números del catálogo `mod center hd/catalog_b3.cat`
  (formato `bin|nombre|label|variante|jugable`). **bin == entrada AFS**
  (147 = Cell Forma 2, 327 = Krillin).
- **AFS por defecto**: `<raíz>/us/data_cmn.afs` (293 423 104 B).
- **Salida**: `out\build\win-amd64-release\mods\<mod>\us\data_cmn.afs\<dest>\geom.bin`.
- **Sin `.disabled`** = mod activo. ⚠️ **Un solo mod activo por slot**
  (el runtime sirve el primero por orden alfabético).

### Verificación (recomendada)
```powershell
python "mod center hd\swap_b3.py" --origen 147 --dest 327 --mod check
# y comprobar que el geom.bin descomprimido == bin origen (MD5)
```

---

## 3. MECÁNICA (por qué funciona)

1. El `#AMB` HD contiene `#AWO` (malla) + `#AZT` (texturas) del **mismo
   personaje** → se mueven juntos, sin mismatch.
2. El runtime **no valida conteos fijos del slot**: dibuja el mesh group, IB,
   bones y UVs **que vienen dentro del bin instalado**.
3. Se sirve como **override por entrada AFS** (bajo peso): el mod sólo contiene
   el bin, no el AFS entero (293 MB).
4. **Compresión LZX `/N:2048`** + padding; si el bin comprimido excede el
   `to_read` del slot, el **mid-insert virtual** del runtime hace crecer la
   entrada in-place y desplaza las posteriores (en memoria).
5. La animación/expresiones van por **match de labels** entre el modelo y el
   `#ACM` del slot; al ser el mismo juego, los labels coinciden (boca OK).

---

## 4. CATÁLOGO DE PERSONAJES

- Fichero: `mod center hd/catalog_b3.cat` (183 entradas).
- Columnas: `bin | nombre | label | variante | jugable`.
- **bin = índice de entrada AFS**. Ejemplos:
  `146/147/148 = Cell Forma 1/2/3`, `149/150/151 = Cell Forma 1/2/3 alt`,
  `327/328/329 = Krillin (sin pelo/con pelo/armadura)`.
- Listar: `python "mod center hd\swap_b3.py" --list`.

---

## 5. IMPLICACIONES

### 5.1 Lo que ESTÁ ready
- **Model swap nativo B3 HD→B3 HD**: 100% funcional y validado
  (Cell F2 → Krillin). Permite poner cualquier modelo HD en cualquier slot,
  completamente jugable.
- Herramientas: `swap_b3.py` (swap), `swap_matrix.py` (mover blobs entre
  slots/regiones), `texture_b3.py` (texturas), catálogo.

### 5.2 Lo que NO está ready
- **Conversión PS2 → B3 HD**. Estado (2026-09-10):
  - **Vía A (inyección)**: funciona técnicamente pero **deforma el cuerpo**,
    porque el cuerpo HD fue **re-modelado** (distancia media HD↔PS2 **0.69**,
    máx **5.31**); manos/cara HD ya son PS2 (dist. 0.01‑0.21) → inyectarlas es un
    no-op. `cell_best2`/`cell_face_only` confirman que no mejora.
  - **Vía B (port completo)**: bloqueada (consumo posicional del pool; hipótesis
    NVIDIA "Vertex Offset Method" + 2ª tabla `AWG0+0x1F80`). Requiere RE.
- **Regla de decisión** (nueva):
  1. ¿El personaje **existe en HD**? → **swap nativo** (perfecto).
  2. ¿Solo existe en **PS2/IW**? → Vía A (limitada) o RE Vía B (pendiente).

### 5.3 Consecuencia estratégica
El port PS2→HD **solo aporta valor para modelos inexistentes en HD**
(p. ej. personajes de Infinite World / modelos custom). Para el roster de B3,
el swap nativo cubre todo. Conviene dirigir el esfuerzo de RE (Vía B) a esos
casos, no a personajes ya presentes en HD.

---

## 6. ESTADO DE HERRAMIENTAS (replicabilidad)

| Herramienta | Estado | Uso |
|---|---|---|
| `swap_b3.py` | ✅ (`--origen/--dest/--mod/--list`) | Swap nativo HD→HD |
| `swap_matrix.py` | ✅ | Mover cualquier blob entre slots/regiones |
| `texture_b3.py` | ✅ | Texturas AZT (extract/build) |
| `port_ps2_b3_inject.py` | ◑ investigación | Vía A (inyección) |
| `port_ps2_b3_inject_aux.py` | ◑ investigación | Vía A extendida (16 AWGs) |
| `catalog_b3.cat` | ✅ | Catálogo (bin|nombre|label|variante) |
| `mod center\Xbox 360 ...\xbcompress.exe` | ✅ | LZX `/N:2048` |

Instrumentos RE de apoyo: `awo_tools/awg0_export.py` (autodetecta formato A/C),
`awo_tools/awg_to_obj_b3.py`, `awo_tools/cell_align_check.py`.

---

## 7. MODS DE ESTA SESIÓN (`out/build/win-amd64-release/mods/`)

| Mod | Estado | Contenido |
|---|---|---|
| **`cell_native`** | **ACTIVO** | Swap nativo validado: Cell F2 (147) en slot 327 |
| `cell_hd_only` | disabled | Igual que `cell_native` (hecho a mano) |
| `cell_best2` | disabled | Vía A extendida a 16 AWGs (no mejora) |
| `cell_face_only` | disabled | Vía A sólo cara (fallback) |
| `cell_best` | disabled | Vía A + guardia anti-estirado |
| `cell_npm_fix` / `cell_npm4_test` / resto | disabled | Histórico Vía A |

---

## 8. RESPUESTA A "¿PS2 → B3 HD ESTÁ READY?"

**No.** Lo que está ready es el **swap nativo HD→HD**.

- **HD→HD** (mismo motor, formato `#AWO`): ✅ 100% (esto).
- **PS2→HD** (convertir un modelo que no está en HD): ⛔ no. Vía A deforma
  (cuerpo HD re-modelado), Vía B bloqueada. Es un problema abierto, con plan en
  `docs/07_ports/PLAN_PS2_B3/PLAN.md`.

---

### Referencias
- `docs/07_ports/PLAN_PS2_B3/PLAN.md` (plan del port PS2→HD) y sus 4 informes.
- `mod center hd/GUIA_SWAPS_Y_PORTS.md` (guía de swaps; actualizada).
- `AGENTS.md` §3.1 (estado), §3.4 (Vía A/B), §10 (pipeline).
