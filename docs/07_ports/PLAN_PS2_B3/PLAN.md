# PLAN — Port de modelos PS2 → Budokai 3 HD Collection (360)

> **Fecha**: 2026-09-10. **Origen**: consolidación de 4 informes de subagentes
> (`01_WEB.md`, `02_MODS_INVENTARIO.md`, `03_DOCS.md`, `04_FORMATO_RE.md`).
> **Caso de prueba**: Cell (Semi-Perfect), plantilla HD bin 147 → slot 327.
> **Objetivo**: decidir y ordenar cómo mejorar el port y, si procede, desbloquear
> la Vía B. Este documento NO ejecuta cambios; define la hoja de ruta.

---

## 0. RESUMEN EJECUTIVO Y DECISIÓN

Hay **dos problemas separados** que la documentación mezclaba:

- **Problema A — cobertura de la inyección (bajo riesgo, YA especificado).**
  La Vía A sólo toca el `sec34` del AWG0 (cuerpo). Quedan intactos **16 AWGs de
  1 hueso** que NO son sólo cara: son **10 de manos + 6 de cara** (huesos 48-63).
  El informe RE (`04`) ha resuelto su **layout de vértice (6 familias), su espacio
  (`world[23]/[30]/[32]`) y el mapeo PS2→HD**, y demuestra que la geometría HD ya
  está a **<0.1 u** de la superficie PS2. ⇒ **Extender el mismo NPM a esos 16 AWGs
  es de bajo riesgo y alto valor inmediato.**

- **Problema B — el bloqueo de la Vía B (alto riesgo, investigación).**
  El pool se consume **posicionalmente** por una vía que no es el descriptor A
  (T6) ni sólo el IB (T7). La hipótesis externa más fuerte es el **"Vertex Offset
  Method"** de NVIDIA (vértices en espacio de hueso, offsets posicionales por
  primitiva) y la **2ª tabla de descriptores `AWG0+0x1F80`**. Requiere RE del
  bucle de draw.

**Decisión propuesta**: ejecutar **A ya** (Fase 1), instrumentar **B** en paralelo
con *time-box* (Fase 2), y **gates de decisión** para no quedarnos atascados
(Fase 3). Antes de tocar nada, resolver las **contradicciones documentales** (§2).

> ⚠️ **Nota estratégica**: para Cell (que ya tiene modelo HD nativo), el "swap
> nativo HD→HD" da un resultado perfecto sin port. El port PS2 es **investigación
> de tecnología**, no la vía óptima para dejar a Cell jugable. Mantener ambos
> objetivos separados.

---

## 1. HECHOS CONSOLIDADOS (con fuente)

### 1.1 Estructura del modelo HD
| Hecho | Valor | Fuente |
|---|---|---|
| AWGs por bin | Cell F2: 17 = AWG0 (48 huesos, 2661 v) + 16 de 1 hueso (48-63) | 03/04 |
| Los 16 auxiliares | **10 manos + 6 cara** (corrige la creencia "16 de cara") | 04 §0 |
| Header AWG | `+0x14 axes, +0x2C vb2, +0x30 IB, +0x34 sec34 (align+2), +0x38 end` | 03 §2.1 |
| Formatos sec34 | A (marker+0, bone+28, normal `[nz,-ny,nx]`) y C (marker+12, bone+40) | 03 §2.2/2.3 |
| vb2 | layout propio, **posiciones absolutas / sin skin**; cara y piernas en algunos bins | 03 §2.4 |
| AWG de cara (nb=1) | buffer fijo en `h+0x1F0`; IB = lista de triángulos | 03 §2.5 |
| 6 familiaslayout (16 AWGs) | offsets de marker/peso/pad/normal/uv/pos por familia | 04 §3 |
| Espacio de los 16 | `world[23]` (mano izq), `world[30]` (der), `world[32]` (cara) | 04 §4 |
| Geometría HD vs surface PS2 | **distancia media <0.1 u** en los 16 AWGs | 04 §0.4 |
| Ejes: parent | **offset relativo** al AWG/AMG, no índice | 03 §3.4 |

### 1.2 Dibujo / skinning / bloqueo
| Hecho | Valor | Fuente |
|---|---|---|
| Dos tablas de descriptores | mesh-group `~0x2D49` (0x60) + **`AWG0+0x1F80`** (u32 plano, runtime) | 03 §3.3 |
| Descriptor A/B | A=vértices, B=índices; B⊂A; A particiona contiguo | 03 §3.3 |
| El IB SÍ gobierna | T7 (IB invertido) → masivo | 03 §4.1 |
| El rango A NO se usa | T6 (rotar A) → normal | 03 §4.1 |
| Consumo posicional | T4/T5 deforman con IB consistente; IB-follow `same=5125 diff=0` | 03 §4.1 |
| Arms | `[bone, ptr, 0, ptr_mat4x4, 0]`, armature +64 B/hueso | 03 §3.1 |
| T7/T6/T4/T5, bone0 | ver tabla | 03 §4.1/4.2 |
| Hipótesis externa | NVIDIA **Vertex Offset Method** (4 offsets/vértice en espacio hueso) | 01 §0.2/2.3 |
| Port PS3 equivalente | `gnome41/dbz-budokai-hd` (EDGE/SPU, #A3T) → espejo de pipeline | 01 §1.2 |

### 1.3 Cara — dónde se dibuja (a resolver, §2.1)
- AWG0 tiene **descriptores propios** de cara/cabello/dientes (`X*_L00_S00_FACE`, `HAIR`, `DTEETH`, `UTEETH`) → 02 §0.3.
- Los **6 AWGs de 1 hueso** (58-63) son la cara detallada → 04 §6.
- Los **descriptores faciales tienen A en `vb2`** → 03 §3.3.
⇒ La cara puede estar repartida en TRES sitios. Hay que censarla (§2.1).

### 1.4 Ecosistema / herramientas
- **No existe conversor público PS2↔HD** (ni wiki, ni Noesis) → 01 §0.1/2.1.
- Mejor herramienta PS2: `SamuelDBZMAAM/Budokai-Modding-Tool` (AMG cara, AMO0) → 01 §1.3.
- Swap de cabeza **in-place FUNCIONA** (z-fighting); neutralizar descriptores deja huecos → 02 §2.2/03 §5.4.
- Retargeting: Blender Shrinkwrap + Data Transfer; R3DS Wrap; Houdini Topo Transfer → 01 §4.
- Constraints AFS/LZX/mid-insert/1-mod-por-test → 03 §6.

---

## 2. CONTRADICCIONES E INCÓGNITAS A RESOLVER (antes de codificar)

1. **¿Dónde vive la cara exactamente?** (AWG0 descriptors vs 6 AWGs 48-63 vs vb2).
   *Acción*: censo de descriptores del AWG0 + material/`#AZT` por mesh-group.
2. **Si la HD ya está <0.1 u del surface PS2 (04), ¿cuánto cambia realmente la
   inyección en los 16 AWGs?** Si el cambio es marginal, el "antes/después" visual
   de la cara puede ser mínimo → ajustar expectativas.
3. **El "estirado" de las láminas**: ¿es stretch de triángulos (guardia) o
   geometría HD legítima? Confirmar con un render fiable (ver §4.0).
4. **Render fiable**: el render propio no usa las matrices reales (arms), por eso
   sale amorfo. Necesario un visualizador correcto para iterar sin abrir el juego.
5. **`sec34` sólo bones 0-35 / piernas en vb2** (03 §5.2) vs `04` (16 AWGs con
   geometría propia) — reconciliar el reparto real de piezas.

---

## 3. PLAN POR FASES

### FASE 0 — Parón y documentación ✅ (esta sesión)
- [x] 4 informes de subagentes (`01..04`).
- [x] Este plan (`PLAN.md`).
- [ ] Reconciliar §2.1 (censo de la cara) y fijar el "mapa de piezas".

### FASE 1 — Vía A extendida a los 16 AWGs (manos + cara) — BAJO RIESGO
**Objetivo**: PS2-izar manos y cara con el mismo esquema validado, sin tocar
tamaños ni topología.
- T1.1 Implementar `port_ps2_b3_inject_aux.py` según spec `04 §7`:
  - por AWG: localizar layout (familia), `world` del padre (23/30/32), región PS2.
  - nearest-point-on-surface + conversión bone-local; umbral cara 0.8 / manos 1.0.
  - **no tocar** weight/pad/marker/uv/IB/descriptores/arms.
- T1.2 Validar offline: recomputar distancia media al surface (debe ≈0) y exportar
  OBJ (`awg_cara_export.py`) chequeando bounds/NaN.
- T1.3 Empaquetar `cell_best2` (LZX `/N:2048`, pad exacto) y probar **UN solo mod**.
- T1.4 Afinar por familia/por AWG según resultado; considerar "proyectar sólo en
  tangente" para las 6 capas de cara (evitar solape piel/ojos/boca).

**Gate G1**: si `cell_best2` mejora cara/manos → consolidar como entrega Vía A.
Si empeora → revertir a `cell_best` y documentar.

### FASE 2 — RE del consumo posicional (Vía B) — INVESTIGACIÓN (time-box)
**✅ LOCALIZADO + LÍMITE (Fase C, 2026-09-10)**: el pool se particiona por
**rangos de parte** (A de descriptores 0x60 + arms); `AWG0+0x1F80` era la tabla de
**matrices bind-pose**. Tests en juego: **T8** (mover partes enteras) = IDÉNTICO;
**T9** (reordenar runs mono-hueso dentro de un bloque) = **DEFORME**. No existe
tabla posición→hueso en el bin y el `+28` sí se usa ⇒ la dependencia es del
**draw/vertex-fetch (GPU)**, no observable offline.
⇒ **Vía B requiere RE del draw a nivel GPU** (instrumentar el vertex fetch / el
bucle de draw en `rexgpu`/`generated`). Detalle:
`docs/07_ports/SESION_FASE_C_CONSUMER_2026-09-10.md`.
**2026-09-11 — GPU RE iniciada**: instrumentado el draw (`command_processor.cpp`)
→ captura de 318 draws **todos indexados**, vertex buffer **derivado vec4**,
índices globales (max 8490). Siguiente: log del `vfetch` (offset/stride/format).
Ver `docs/07_ports/SESION_GPU_DRAW_2026-09-11.md`.
**Objetivo restante**: instrumentación GPU; luego reconstruidor pool/A/B/IB.
- T2.1 ✅ Censo de descriptores (A = vértices, B = IB): `awo_tools/phase_c_descriptors.py`.
- T2.2 ✅ Tests T8/T9 (`awo_tools/phase_c_make_t8.py`, `phase_c_make_t9.py`).
- T2.2 Aplicar la **hipótesis Vertex Offset** (01 §2.3): buscar arrays de offsets
  por hueso/primitiva que apunten a `sec34+k*44` cerca de arms/mesh-ref.
- T2.3 Instrumentar el **bucle de draw** en `generated/dbz3_recomp.*.cpp`
  (buscar accesos a `sec34`, IB y tabla `0x1F80`).
- T2.4 Tests nuevos **T8/T9** (1 mod activo): tocar SÓLO la 2ª tabla (rotar A/B) y
  ver si el render cambia; y tocar los offsets por hueso.
- T2.5 Espejo PS3: comparar el layout de `gnome41/dbz-budokai-hd` vs 360.

**Gate G2 (time-box)**: si se identifica el consumidor → diseñar regenerador
(Vía B real). Si no → cerrar Vía B como "no viable a corto plazo".

### FASE 3 — Alternativas / decisión estratégica (si G2 falla)
Ordenadas por coste/beneficio:
- **A3.1 Swap nativo HD→HD** (para personajes con modelo HD): perfecto, ya validado.
  Es la opción correcta para Cell si el objetivo es "jugable".
- **A3.2 Retarget por bake** (01 §4): Blender Shrinkwrap + Data Transfer para
  *bakear* la forma PS2 sobre la **topología HD** (mantiene el orden del pool) →
  convierte "reordenar pool" en "mover vértices" (lo que el guest acepta).
- **A3.3 Trasplante in-place** (02 §2.2, validado): copiar buffers de cara/mano
  entre bins HD sin mover offsets, + neutralizar descriptores del AWG0.
- **A3.4 Cerrar el caso** y archivar (como Janemba/Pikkon) si nada convence.

### FASE 4 — Verificación, empaquetado y documentación
- V4.1 Verificación en juego con **un solo mod activo** (regla anti-contaminación).
- V4.2 `tools/make_release.ps1` / `verify_release.ps1` si se publica.
- V4.3 Actualizar `AGENTS.md §3.4/§10` y `docs/07_ports/` con resultados y
  correcciones a los docs desactualizados (`BIN_LAYOUT.md`, `AMO_AWO.md`,
  `AWO_FORMAT.md` `/N:32`).

---

## 4. TAREAS TRANSVERSALES (habilitadores)

### 4.0 VISUALIZADOR FIABLE (prioritario)
El render actual no usa las matrices reales (arms) → amorfo. Sin un visor correcto
no se puede iterar offline. Opciones:
- (a) RE de las matrices de los arms y aplicarlas en el render propio;
- (b) exportar OBJ por hueso (estilo `Tutorial12.rtf`: hueso a escala 0 y export)
  y componer;
- (c) usar Blender/Noesis con el OBJ + esqueletos para previsualizar.
**Sin esto, cada iteración cuesta una sesión de juego.**

### 4.1 Mapa de piezas y materiales
Censo de descriptores (AWG0 y `0x1F80`) + índice de material → bloque `#AZT`,
para saber qué malla corresponde a ojos/boca/dientes y qué textura usa.

### 4.2 Herramientas PS2 a canibalizar
`SamuelDBZMAAM/Budokai-Modding-Tool` (`amo_s.py`, `amg_c.py`, "Removing face AMGs")
como mapa de la estructura de cara PS2 (huesos 33-41).

### 4.3 Preguntar en ResHax
Publicar el layout de vértice ya deducido (#AWO/#AWG) en `https://reshax.com/`
para validar con terceros.

---

## 5. RIESGOS

| Riesgo | Mitigación |
|---|---|
| Los 6 AWGs de cara comparten bbox/centroide → proyección ciega solapa capas | Proyectar sólo en tangente / conservar componente local / usar material para separar |
| Dientes PS2 (b36/b38) sin AWG HD dedicado | Localizarlos en vb2/AWG0 o tratarlos aparte |
| Vía B sin consumidor localizado | Time-box + Gate G2 + alternativas Fase 3 |
| Contaminación de tests | UN mod activo por test; verificar `.disabled` |
| Presupuesto/crash por conteos | usar conteos ≤ plantilla; LZX `/N:2048`; pad exacto |
| Docs desactualizados inducen error | Corregir `BIN_LAYOUT.md`/`AMO_AWO.md`/`AWO_FORMAT.md` |

---

## 6. CRITERIOS DE ÉXITO

1. **Fase 1**: `cell_best2` con manos y cara PS2 sin artefactos nuevos; mejora
   visible vs `cell_best`.
2. **Fase 2**: ✅ RESUELTO 2026-09-11 — NO hay consumidor posicional GPU: el
   buffer es copia verbatim del pool y el IB es el del fichero; la deformación de
   T4/T9 era un bug de base de índices del tool (T10 = geometría idéntica).
   Pendiente: skew de UV (+1). Evidencia: `SESION_GPU_DRAW_2026-09-11.md` §6-7.
3. **Visor offline** fiable (reduce iteraciones).
4. **Documentación** corregida y plan trazable.

---

## 7. ORDEN DE EJECUCIÓN RECOMENDADO (siguiente sesión)

1. §4.0 **visor fiable** (desbloquea la iteración rápida).
2. §2.1 **censo de la cara/materiales** (resuelve la contradicción).
3. **Fase 1** T1.1-T1.4 (`cell_best2`).
4. **Fase 2** T2.1-T2.2 (tabla `0x1F80` + hipótesis Vertex Offset) en paralelo.
5. Gate G2 → Fase 3 si procede.

---

### Anexo — informes fuente
`01_WEB.md` (ecosistema, no existe conversor, Vertex Offset, retargeting) ·
`02_MODS_INVENTARIO.md` (herramientas, swap de cabeza, huecos) ·
`03_DOCS.md` (Vías A/B, formatos, tests, constraints) ·
`04_FORMATO_RE.md` (layout y mapeo de los 16 AWGs, spec de implementación).

---

## 8. RESULTADO FASE 1 (2026-09-10) Y HALLAZGO CLAVE

Implementado `mod center hd/ports/port_ps2_b3_inject_aux.py` (spec `04 §7`):
inyecta los 16 AWGs auxiliares (10 manos + 6 cara). **3079/3085 vértices,
distancias →0, sin NaN** (mods `cell_best2`, `cell_face_only`).

**Resultado en juego: prácticamente sin cambios.** Esperado, y el dato lo
explica:

| Parte | Distancia geometría HD ↔ superficie PS2 |
|---|---|
| **Cuerpo (AWG0, 48 huesos)** | **media 0.69 · máx 5.31** |
| Manos/cara (16 AWGs de 1 hueso) | media 0.01‑0.21 |

**Conclusiones**:
1. El **cuerpo HD fue re‑modelado** (no coincide con el PS2). El **AWG0 es la
   única parte donde la inyección aporta algo… y es justo donde deforma**
   (mueve vértices 0.7 de media, hasta 5.3 → láminas planas / híbrido).
2. Manos y cara HD **ya son el modelo PS2** → extender la Vía A ahí es un no‑op.
3. ⇒ Para un personaje **con modelo HD** (Cell, Krillin, etc.), la Vía A es un
   **retroceso** frente al **swap nativo HD→HD** (resultado perfecto).
4. La Vía A sólo tiene sentido para personajes **sin modelo HD** (candidatos:
   Janemba-like, Tien capa, o modelos PS2‑only) — y siempre que el cuerpo PS2
   encaje mejor que el HD de la plantilla elegida.

**Gate G1 (revisado)**: la entrega "Cell PS2→HD" se cierra como **demostración
técnica** de la Vía A; para dejar a Cell jugable, usar `cell_hd_only` (nativo).
Ver mod de comparación `mods/cell_hd_only` (bin 147 tal cual).

**Reencuadre del plan**: priorizar (a) **swap nativo** como vía de entrega,
(b) Vía A sólo para PS2‑only, (c) Fase 2 (RE Vía B) como investigación.

**CIERRE (2026-09-10)**: el **swap nativo HD→HD** (Cell Forma 2 → Krillin) se ha
validado en juego como **100% funcional** (boca incluida) con
`mod center hd/swap_b3.py --origen 147 --dest 327`. Detalle:
`docs/07_ports/SESION_SWAP_NATIVO_2026-09-10.md`.

⇒ **Respuesta a "¿PS2→B3 HD ready?"**: **NO**. Lo ready es **HD→HD (swap
nativo)**. PS2→HD (conversión de un modelo inexistente en HD) sigue abierto:
Vía A deforma (cuerpo HD re‑modelado, dist. 0.69/5.31) y Vía B está bloqueada.
El esfuerzo de port PS2→HD sólo tiene sentido para **personajes que no existen
en HD**.

