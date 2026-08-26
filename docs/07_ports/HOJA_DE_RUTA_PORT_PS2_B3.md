# HOJA DE RUTA — Port PS2 → B3 HD (pipeline nombrado)

> Fecha: 2026-08-26. Parte del ESTUDIO_ECOSISTEMA_MODS.md. Define el pipeline
> de herramientas especializadas en **ports PS2 (#AMO0/#AMG LE) → B3 HD
> (#AWO/#AWG/#AZT BE)** con nombres que indican su propósito. Las herramientas
> estables del Launcher (`swap_b3.py`, `texture_b3.py`, `catalog_b3.cat`) NO se
> tocan; el pipeline nuevo las reutiliza para LZX/padding/instalación.

---

## 1. CONVENCIÓN DE NOMBRES

Todas las herramientas del pipeline de port llevan el prefijo **`port_ps2_b3_`**
y viven en `mod center hd\ports\` (nueva carpeta, separada del toolkit estable
del Launcher). Cada una es un **script único y autocontenido** (no dependiente
de los experimentos de `awo_tools/`), con CLI documentada y validación interna.

```
port_ps2_b3_extract.py    → extrae y parsa el modelo PS2 (malla+rig+esqueleto)
port_ps2_b3_geometry.py   → coords locales + bone → buffers HD (sec34/vb2/IB)
port_ps2_b3_draw.py       → [PIEZA CLAVE] estructura de dibujo HD desde cero
port_ps2_b3_pack.py       → empaqueta bin #AMB autocontenido (AWO+AZT) + LZX
port_ps2_b3_verify.py     → bucle de feedback: export OBJ + chequeo bounds/NaN
port_ps2_b3_textures.py   → #AMT (PS2) → #AZT (HD) (opcional por ahora)
```

Pipeline end-to-end: `extract → geometry → draw → pack → verify → instalar
(override)`. Cada script devuelve JSON intermedio (estructura del modelo,
conteos) que el siguiente consume; así cada etapa se valida de forma aislada.

---

## 2. FASES

### Fase 0 — Validar la premisa del conversor (barata)

**Re-test `janemba_from_cell` en juego con la DLL correcta** (nunca se validó
por el runtime stale del §13.6, ya arreglado). Es el bin HD construido por
`build_from_template.py` (plantilla Cell F2, 48 huesos = Janemba) con geometría
real de Janemba, estructuralmente válido (0 NaN, bounds plausibles). Si renderiza
→ la estructura de dibujo de una plantilla del mismo nº de huesos acepta
geometría extranjera → el conversor solo necesita emitir en plantilla probada.
Si crashea → el generador de estructura de dibujo (`port_ps2_b3_draw`) es el
bloqueo real y el foco va ahí.

### Fase 1 — Consolidar el pipeline PS2 (lo ya resuelto, renombrado)

- **`port_ps2_b3_extract.py`**: clonar/consolidar `parse_ps2_mesh.py` (malla +
  IB real por FaceType) + `ps2_rig_skin.py` (rig: bone+peso por vértice, con el
  desfase `amg_abs` resuelto) + `pose_matrix.py` (world mats). Salida: JSON con
  vértices (pos local, normal, uv), IB real, skin (bone+peso por vértice),
  esqueleto (labels + jerarquía + matrices).
- **`port_ps2_b3_geometry.py`**: clonar `ps2_to_hd_geometry.py` (coords locales
  + bone → sec34 44B skinned formato A + vb2 44B estático + IB u16 BE). Salida:
  JSON de buffers HD listos para empaquetar.

### Fase 2 — La pieza clave: estructura de dibujo HD desde cero

**`port_ps2_b3_draw.py`** genera la estructura que el guest necesita para
deserializar y dibujar, coherente con la geometría nueva:

- **AWG header** (nb=1 o n_bones por AWG, offsets sec34/vb2/IB correctos según
  el formato emitido).
- **Mesh-ref blocks + arms** (límites del IB en bytes, sellos 0x204).
- **Descriptores de submesh** (0x60 bytes; label en +00, `max N m` en +18,
  rangos A en +50/+54, B en +58/+5C — layout de SUBMESH_DATA_B3.md) y la zona
  de submesh data del AWG0.
- **Ejes/esqueleto**: reutilizar los del PS2 (mismos sellos, §12.2) o copiar la
  zona de ejes de la plantilla HD.

Base de estudio: `analyze_meshgroup.py`, `analyze_mesh.py`, `awg0_export.py`,
`awg_cara_export.py` (parsing) + el patrón `amg_c.py` de la comunidad (templates
`b3_amg_*.bin`) traducido a HD/BE. **Este es el RE fino pendiente** y el
verdadero hito del port.

### Fase 3 — Empaquetado e instalación (reusar lo estable)

- **`port_ps2_b3_pack.py`**: empaqueta el #AMB autocontenido (header + AWO +
  AZT), comprime LZX `/N:2048`, padea al to_read y genera el override
  `mods/<mod>/us/data_cmn.afs/<entry>/geom.bin` + manifest. Reutiliza las
  utilidades de `swap_b3.py` (comprimido/padding/instalación).
- **`port_ps2_b3_verify.py`**: exporta el bin construido a OBJ con
  `awg_to_obj_b3.py`/`awg0_export.py` y chequea 0 NaN + bounds plausibles +
  conteo de tris (feedback en segundos sin abrir el juego).

### Fase 4 — Testeo en juego con personaje real

Ver §3. Criterios y candidatos.

### Fase 5 — Automatización e integración en el Launcher

- Una vez validado un personaje en combate, exponerlo en la pestaña Model Swap
  ("Port PS2→B3") como pipeline asíncrono (patrón `ModPipeline::RunAsync`).
- Ampliar el `catalog_b3.cat` con los personajes portados.
- (Largo plazo) Texturas #AMT→#AZT automáticas con `port_ps2_b3_textures.py`.

---

## 3. FASE DE TESTEO — ELECCIÓN DEL PERSONAJE

### 3.1 Criterios (derivados de las lecciones §5 del estudio)

1. **Esqueleto 1:1 con el bin HD de destino** (mismo juego = sin retargeting;
   el retargeting por rotación fue donde Janemba fracasó).
2. **Plantilla HD simple** para validar el conversor de forma aislada (no
   Krillin de 18 AWGs; preferir Babidi de 1 AWG o Bulma de 2).
3. **Disponible en B3 PS2 GH o IW** (acceso ilimitado a los AFS).
4. Resultado deseado a largo plazo: **añadir personajes sin versión HD** (IW).

### 3.2 Recomendación — dos etapas

**Etapa A — VALIDADOR DEL CONVERSOR: Babidi (B3 PS2 GH → HD).**
- Por qué: el bin HD de Babidi (entrada 96) es la plantilla MÁS SIMPLE del
  juego (1 AWG, 41 huesos, formato simple). Babidi existe en el B3 PS2 GH con
  la MISMA numeración (GH=HD, §5) → tenemos el PS2 fuente. Esqueleto 1:1
  (mismo juego). Permite aislar cada etapa del pipeline sin el ruido de la
  estructura compleja de Krillin.
- Validación: portar Babidi PS2 → bin autocontenido → instalar en slot de
  prueba → debe renderizar un Babidi correcto.
- Si el esqueleto PS2 GH de Babidi difiere en huesos del HD (a verificar con
  `scan_bones.py`), elegir otra plantilla simple del mismo personaje.

**Etapa B — PORT REAL (añadir contenido): un IW con esqueleto 1:1 a un HD.**
- Buscar en `ps2_games\Infinite World` y los 241 .amb un personaje cuyo
  esqueleto mapee 1:1 (mismos labels/orden) con un bin HD existente.
- Candidato por descartar primero: traje alternativo de un personaje existente
  (AGENTS §3.1), o Pikkon/Pan si se confirma esqueleto compatible (PKH con
  SKIRT → NO 1:1 según §3.1; verificar antes de comprometer).
- Validación: el personaje nuevo aparece en combate con su silueta correcta.

### 3.3 Plan de la fase de testeo (pasos)

```
1. Verificar Babidi PS2 en ps2_games\Budokai 3 Greatest Hits (USA)\USR\data_cmn.afs
   (misma entrada que HD = 96; descomprimir con xbdecompress).
2. `port_ps2_b3_extract` sobre el bin PS2 → JSON (verts/tris/skin/esqueleto).
3. `port_ps2_b3_geometry` → buffers HD.
4. `port_ps2_b3_draw` → estructura de dibujo (RE fina, fase 2).
5. `port_ps2_b3_pack` → bin autocontenido + override en slot de prueba.
6. `port_ps2_b3_verify` → OBJ + bounds (feedback rápido).
7. Probar en juego (slot 327 Krillin o slot Babidi 96) → ajustar.
```

---

## 4. QUÉ NO HACER (errores del pasado, §5 del estudio)

- NO inyectar geometría PS2 en plantillas HD de otro personaje.
- NO reconstruir IB/arms de un bin HD existente (conteos fijos).
- NO usar layout del vértice del B1 (bone@+16) en B3 (bone@+28).
- NO asumir tripletes en el IB PS2 (usar FaceType).
- NO comprimir con /N:32 ni olvidar el padding al to_read.
- NO confiar en la DLL del build sin verificar el mid-insert virtual
  (`Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"`).
- NO forzar el formato A de Krillin: cada bin es autocontenido (formatos A/C).

---

## 5. ESTADO Y PRÓXIMOS PASOS

| Fase | Estado |
|---|---|
| 0. Re-test `janemba_from_cell` (DLL correcta) | PENDIENTE (premisa — ahora con fundamento, ver ESTRUCTURA_DIBUJO_HD) |
| 1. `extract` + `geometry` (consolidar lo resuelto) | **✅ HECHO 2026-08-26**: `port_ps2_b3_extract.py` (PS2→JSON) + `port_ps2_b3_geometry.py` (buffers HD formato A + grupos por part) |
| 2. `draw` (RE de la estructura de dibujo HD) | **✅ RESUELTO 2026-08-26** (ESTRUCTURA_DIBUJO_HD: descriptores A/B confirmados). `port_ps2_b3_draw.py` calcula descriptores por mesh part (corrige el reparto uniforme de build_from_template) |
| 3. `pack` + `verify` (reusar lo estable) | **✅ HECHO**: `port_ps2_b3_pack.py` (clona plantilla + mid-insert + descriptores) + `port_ps2_b3_verify.py` (OBJ + bounds/NaN) |
| 4. Testeo en juego | **PRIMER PORT GENERADO**: Cell F2 PS2 (GH bin 147) → plantilla Cell F2 HD (48 huesos). Mod `cell_ps2_port` instalado en slot 327. Verificación estructural OK (0 NaN, 0 OOB, 29/29 descriptores). **PENDIENTE: validar render en juego** |
| 5. Launcher (pestaña Model Swap + catálogo) | PENDIENTE |

Próximo paso inmediato: **Fase 0** (re-test de `janemba_from_cell` con la DLL
fija). Si funciona, la Fase 2 baja de dificultad (la estructura de plantilla
funciona con geometría ajena) y el foco pasa a emitir en plantilla probada.
Si falla, la Fase 2 es el bloqueo y la investigación se centra en los
descriptores/arms del AWG0.