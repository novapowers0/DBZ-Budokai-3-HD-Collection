# CONSEJOS PARA BUDOKAI 3 HD — desde el proyecto B1 HD (17/08/2026)

> Lecciones transferibles del port PS2→HD del B1 (Chaozu HD nativo validado,
> submesh data descifrado, mapeo B2 PS2→HD 1:1) que deberían influir en el
> proyecto `DBZ Budokai 3 HD Collection` (añadir personajes IW/B2/B3).

---

## 1. EL SWAP NATIVO ES LA VÍA PRIMARIA (validado en B1)

**Principio** (lección 9/10 B1): el runtime dibuja el bin `#AWO`/`#AMB`
completo tal cual (mesh group + IB + bones + UVs). No valida el slot.

**Consecuencia para B3**: si un personaje ya existe en algún bin HD del B3
(o del B1), el swap con bins HD completos del MISMO personaje es la vía
definitiva — render perfecto sin conversión de geometría. En B1, el Chaozu
HD completo (bin 352+353, 3 AWGs = cuerpo+manos) en el slot Tenshinhan
validó esto al 100%.

**Aplicación en B3**:
- Para personajes B3→B3 (nuevos slots): usar el bin HD del personaje tal
  cual (par geom+tex del mismo personaje).
- Para B1→B3 (si el personaje existe en B1 HD): el `awg_to_obj.py` del B3
  + `port_b1_to_b3.py` ya cubren la dirección opuesta.

## 2. PARA PERSONAJES QUE NO EXISTEN EN HD: RECONSTRUIR, NO INYECTAR

**El error más común** (documentado en `RE_PROGRESO.md` del B3 §15-19 y
confirmado en B1): inyectar posiciones PS2 sobre un bin HD anfitrión
**deforma** porque la geometría HD es **re-topologizada** (IB propio,
vértices reordenados). El matching por vecino al 98% sigue deformando en
brazos/manos/cabeza/piernas.

**La vía correcta**: reconstruir el bin HD COMPLETO con la topología PS2:
1. Parsear el `#AMO0` PS2 (mesh parts, verts, rig → coords locales).
2. Generar sec34 (44B) + IB desde los triángulos PS2.
3. Regenerar arms (rangos del IB por bone).
4. **Regenerar la zona de submesh data** (descriptores por mesh part).

## 3. SUBMESH DATA: LA PIEZA QUE FALTABA (descifrada en B1)

En los AWG HD, entre la zona de arms y el sec34 hay una zona de **descriptores
de submesh** (uno por mesh part) con:
- floats de transformación/material
- `c08/c0C` = inicio/tamaño rango A (contiguos entre descriptores)
- `c10/c14` = inicio/tamaño rango B
- label del part (X??_BODY, ??_L01_LHAND...) + string debug `max N m`

**Riesgo**: copiar la zona de submesh de una plantilla (sin regenerarla)
sobre geometría nueva → **hang** (el runtime se queda esperando datos cuyos
offsets ya no coinciden). Para portar hay que **generar un descriptor por
cada mesh part PS2** con los rangos de los buffers nuevos.

## 4. ESQUELETO B2 PS2 = HD 1:1 (verificado con Tenshinhan)

El B2 PS2 usa el MISMO formato `#AMO0`/`#AMG` que el B1 PS2, y los personajes
comparten esqueleto con el HD:

- **Tenshinhan B2 PS2** (entry 282 del data_cmn.afs): 14 mesh parts, 4427
  verts, 2944 skin. Los 42 labels base (`TSH_BODY, TSH_WAIST, TSH_STMC...`)
  son **idénticos y en el mismo orden** que el TSH HD. Solo difieren los
  labels extra de manos/caras (24 más en PS2, que en HD viven en AWGs
  separados).
- **Aplicación en B3**: el mismo mapeo aplica para añadir personajes IW/B2/B3
  con traje distinto: el esqueleto base es el del personaje, solo cambia la
  malla. Se puede usar el bin HD nativo del mismo personaje como plantilla
  estructural (ejes, arms, mesh headers, submesh data) y reemplazar solo la
  geometría.

## 5. GAMECUBE: NO ES FUENTE PARA MODELS

El ISO GC del B1 (`DragonBall Z - Budokai [NGC].iso`) usa formatos
`#ACO/#ACB/#AMB` (`.act/.aco/.acm/.acb`) — distintos al `#AMO0` PS2 y al
`#AWO` HD. Los nombres de archivo no corresponden a personajes (la entry
"TSH" es Trunks). **Usar solo los AFS del PS2 como fuente de modelos.**

## 6. RECOMENDACIÓN PARA EL PIPELINE B3

1. **Catálogo de personajes**: escanear los AFS PS2 (B1/B2/B3/IW) por
   labels `X??_BODY` (escaneando el AMO completo, no solo el inicio) →
   mismo catálogo que `launcher_mod_pipeline.py` del B1 pero multi-juego.
2. **Priorizar swap nativo** para personajes que existen en HD.
3. **Port PS2→HD** solo para personajes sin versión HD: reconstrucción
   completa (sec34+IB+arms+submesh data), usando el bin HD del mismo
   esqueleto como plantilla estructural.
4. **Validar con personaje de prueba** antes de automatizar (en B1 el
   Tenshinhan B2 PS2 es el caso de prueba perfecto: esqueleto 1:1, traje
   distinto).

## 7. ARCHIVOS DE REFERENCIA

- B1: `docs/re/SESION11_PORT_PS2_METODOLOGIA.md` (metodología completa).
- B1: `conversores/amo0_to_awo.py` (parser PS2 + reempaquetado a extender).
- B1: `mods/test_chz_hd_completo_on_tsh/` (swap nativo validado).
- B3: `awo_tools/RE_PROGRESO.md`, `AWO_FORMAT.md`, `PLAN_AWO_DESDE_CERO.md`.
- B3: `mod center\B3_IW Model Converter\amb_model.py` (reempaque AMB↔AMO/AMT).