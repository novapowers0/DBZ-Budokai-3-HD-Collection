# HOJA DE RUTA 2026-09 — Madurez del proyecto (post-1.1.1)

> Actualizado: 2026-09-02. Estado base: **v1.1.1 publicada** (Latest), juego muy
> funcional y validado en combate/menús (US+EU, teclado, presets, mods por
> override). Documentos previos: `HOJA_DE_RUTA.md` (modding, 2026-08-14) y
> `HOJA_DE_RUTA_COMUNIDAD.md` (feedback comunidad, 2026-08-25, P0-P5 casi todo
> HECHO). Esta hoja de ruta los **actualiza y consolida** en 3 fases.

**Principio rector (heredado de HOJA_DE_RUTA.md)**: *no adivinar el formato —
leerlo del guest*. El código recompilado en `generated/` (y `generated_eu/`) es
el parser REAL; cada campo está validado por una instrucción del guest.
Instrumentar el runtime (loguear el PC guest) para ver qué lee y validar
nuestros bins contra eso.

---

## FASE 1 — DOCUMENTACIÓN LIGERA (gasto de tokens)

**Objetivo**: reducir el coste de contexto de las sesiones sin perder datos
relevantes. Hoy `AGENTS.md` = **236 KB / ~3392 líneas** (~60k tokens por
sesión). `docs/` total = 173 KB.

### 1.1 `AGENTS.md` 236 KB → ~60 KB operativo + archivo histórico
- **Conservar en AGENTS.md (comprimido)**: todo lo OPERATIVO — estado
  consolidado (§0-§3.4), constraints duros (layout vértice B3, to_read/mid-insert,
  DLLs canónicas y el bug del cmake-stale §13.6, comandos de build §6),
  fixes del launcher/runtime resumidos (§4, §12-§14), pipeline de mods (§10),
  estado del port (§15 → referencia a docs), comandos útiles.
- **Archivar el historial verbatim**: §8 items 8-65 (el log numerado largo),
  §65.1.x (sesiones detalladas de inyección), §11.1 (Janemba) → a
  `docs/01_estructura/HISTORICO_AGENTS.md` (nuevo), referenciado desde AGENTS.
  Cero datos borrados: se mueven.
- **Regla**: toda constraint de ingeniería (offset, tamaño, causa-raíz, comando
  de compilación) se mantiene; solo el relato histórico pasa al archivo.
- **Método**: reescritura manual + verificación por greps de que los tokens
  clave (offsets, tamaños, hashes, comandos) sobreviven.

### 1.2 Otros .md
- `HOJA_DE_RUTA_COMUNIDAD.md` (16 KB): **reescribir limpio** (tiene mojibake
  doble-encoded, §14.19) y marcar lo ya completado como histórico.
- `HOJA_DE_RUTA.md` (11 KB): marcar como histórico/superado por esta hoja.
- `AWO_FORMAT.md` (19 KB): mantener (es la referencia de formato); comprimir
  solo si se puede sin perder el layout campo a campo.
- Actualizar `docs/README.md` (índice) con el nuevo mapa.

**Criterio de éxito**: AGENTS ≤ 60 KB; todas las constraints verificables por
grep; ninguna sesión futura necesita el historial para operar.

---

## FASE 2 — LIMPIEZA DE CÓDIGO MUERTO Y DEPURACIÓN

### 2.1 Código muerto (verificado en `src/`)
- **`dbz3_enabled_mods` (cvar + rutas)**: código muerto desde §4.2 (la
  activación real es el marker `.disabled`). Quitar: cvar (settings.cpp:62),
  `SetFlagByName("dbz3_enabled_mods", ...)` (launcher_state.cpp:490), el
  bloque `JoinList`/`SetModEnabledList` de `IsModEnabled` (settings.cpp:609-625).
  Conservar `IsModEnabled` (usado).
- **`PrepareRegionData`** (settings.cpp:633): stub no-op desde §14 (el juego
  lee directo sin overlay). Verificar llamadas y eliminar o reducir a su rol
  real (`project_root`). Limpiar comentarios `active_region`.
- **`awo_tools/analyze_bin_hd.py`**: DESACTUALIZADO (§13.2, layout PS3) —
  borrar o dejar solo como aviso; apuntar a `awg_to_obj_b3.py`/`awg0_export.py`.
- **Artifacts de variantes eliminadas**: DLLs de `out/win-amd64-legacy/`
  (variante legacy fuera desde §14.21; el clasico usa avx2 de
  `out/win-amd64/`). Verificar nada las referencia antes de borrar.
- **Historial de herramientas de port**: `awo_tools/historial_fallidos/`
  (scripts Janemba) ya archivados — mantener como archivo, no enlazar.
- **CMakeLists**: revisar targets/cache obsoletos (p.ej. restos de bootstrap,
  `DBZ3_EU_VARIANT`, win-amd64-release-eu ya borrado).
- Barrido de warnings: compilar dual+release con `-Wall` y cerrar los no
  intencionados.

### 2.2 Depuración pendiente (deuda técnica conocida)
- **`std::terminate` intermitente de `LaunchModule`** (§14.14): mitigado con
  try/catch + log, pero el `throw` exacto NO se ha localizado. Resolver con
  la captura de stack ya instrumentada si algún usuario lo pilla.
- **Core EU en combate real**: solo boot validado (§14.13/§14.16); paths
  profundos (combate/eventos) podrían revelar funciones sin registrar →
  usar `DBZ3_COLLECT_UNREGISTERED` si aparece un crash.
- **`verify_release.ps1` como gate**: correrlo en CADA release (ya existe).
- **Mojibake** en `HOJA_DE_RUTA_COMUNIDAD.md` (→ Fase 1.2).

---

## FASE 3 — RE: CONTENIDO NUEVO POR DUPLICADOS (habilidades, personajes, stages)

**Hipótesis del usuario (válida)**: duplicar una entrada existente y modificarla
es el camino — ya está PROBADO que el runtime acepta bins autocontenidos en
cualquier slot (swap nativo §3.4.1) y que el guest autodetecta el formato de
cada bin (§13.1). Lo que falta es **mapear las tablas de contenido** (roster,
stages, movimientos) para duplicar la ENTRADA correcta.

### 3.1 Auditoría de contenido (base de todo) — 🔴 EN CURSO (2026-09-02)
1. **✅ Mapa completo de `data_cmn.afs`** (3990 entradas): hecho y documentado en
   `docs/03_formatos/AUDITORIA_DATA_CMN.md` + mapa crudo en
   `mod center hd/data_cmn_map.txt`. Clasificación por magics internos
   (#AWO/#AWG/#AZT/#ACM/#AMB) con `awo_tools/afs_scan.py`.
2. **✅ Localizar los STAGES: VALIDADO** (2026-09-02) — dos zonas confirmadas con
   `awo_tools/stage_analyze.py` (conteo #AWO/vértices): bins 44-69 (entornos
   multi-pieza, 13 stages + bins de colisión 53-69) y 3735/3786/3788/3821/3823/
   3845/3847 (#AWO gigante de 16K-102K vértices, 7 stages) = **~20 total**
   (coincide con el juego; ningún personaje pasa de ~3K verts). Detalle en
   `docs/03_formatos/AUDITORIA_DATA_CMN.md` + `mod center hd/stages_b3.txt`.
   ⚠️ **Layout de vértice de stage ≠ personaje** (lecturas FLT_MAX): para
   editar stages (F3.4) hace falta RE del layout. Pendiente: cruzar cada bin
   con el nombre real del stage (select en data_eng.afs).
3. **Localizar los MOVESETS/habilidades**: #ACM identificado como contenedor de
   animaciones (bins 127, 358, 435, 444, 2208, 2209, 3881) pero NO distingue
   moveset de personaje vs animación de escenario. Pendiente: mapear el moveset
   exacto por personaje vía RE del guest.
4. **🔴 Mapear el SLXS/roster HD: 🔍 NO existe archivo SLXS en el HD** (2026-09-02).
   El roster→trajes→bins vive en el **código del guest** + los composites de
   `data_eng.afs` (auditoría en `AUDITORIA_DATA_CMN.md` §3.1): entry 0 =
   character select (28 MB, 5 secciones #ACA + 5 #AZT + 1 #AWO), 1976/2043 =
   texturas de retratos, `#SKC` = config UI. **Para F3.3 el mapeo exacto
   (slot→bin) se obtiene instrumentando el guest**: captura parcial realizada
   (2026-09-07) — trace de reads AFS por entrada en
   `rexglue-sdk-0.10/src/filesystem/devices/host_path_file.cpp` (`ReadSync`,
   gateado por `dbz1_diag_logging`, escribe `dbz1_afs_reads.log`). Identificados
   14 bins de modelo (`#AWO1#AWG*#AZT1#AMB1`) y varias parejas de recursos
   auxiliares; resultado completo en `AUDITORIA_DATA_CMN.md` §3.2. `OpenMapped`
   ya está instrumentado para registrar mappings, aunque un mapping largo no
   produce necesariamente un evento por cada página tocada. Pendiente:
   captura adicional con personajes/stages bloqueados y, si hace falta, trace
   de faults del backend `MappedMemory` para resolver el slot→retrato fino.

### 3.2 Habilidad adicional (por duplicado)
1. RE del formato de habilidad en `generated/` (tabla de movimientos).
2. Duplicar una habilidad existente (p.ej. un Ki Blast) + modificar parámetros
   (daño/animación/nombre/textura).
3. Instalar como mod (override por entrada) + validar en combate.

### 3.3 Slot de personaje adicional (por duplicado) — el más cercano
1. Elegir un slot vacío del `data_cmn.afs` (§8 item 15 ya explorado) o
   duplicar una entrada.
2. Modelo: swap nativo (ya funciona) o port (pipeline §3.4/§15).
3. Entrada SLXS/roster duplicada + textura del select + voz.
4. Validar: personaje nuevo en el select y en combate.

### 3.4 Stage adicional (por duplicado)
1. Localizar bins de stage (3.1.2) + dónde se lista en el select.
2. Duplicar un stage existente (bins + entrada de select) + modificar
   geometría/textura.
3. Validar: stage seleccionable y cargable.

### 3.5 Herramienta común: "duplicar entrada"
Script/pipeline genérico que duplique una entrada (AFS + SLXS/select) y apunte
a un bin nuevo — reutilizable para personajes, stages y habilidades.

**Orden de ejecución recomendado** (por viabilidad): 3.3 (personajes, swap
ya validado) → 3.4 (stages, requiere localizar bins) → 3.2 (habilidades, la
RE más profunda). El orden del usuario (habilidades → personajes → stages) es
válido como prioridad de interés; la ejecución técnica sigue el de viabilidad.

---

## ORDEN DE EJECUCIÓN SUGERIDO

1. **Fase 1** (doc ligera) — beneficio inmediato en tokens de cada sesión.
2. **Fase 2.1** (código muerto) — riesgo bajo, limpieza rápida + release menor.
3. **Fase 2.2** (depuración) — solo lo que aparezca; no bloquea.
4. **Fase 3.1** (auditoría) — el mapa de contenido habilita todo el RE.
5. **Fase 3.3 → 3.4 → 3.2** (contenido nuevo por duplicados).

## CRITERIOS DE ACEPTACIÓN
- F1: AGENTS ≤ 60 KB sin perder constraints; índices coherentes.
- F2: build dual+release sin warnings nuevos; sin cvars/código muerto rastreable.
- F3: al menos UN personaje nuevo jugable (select + combate), UN stage nuevo
  cargable y UNA habilidad nueva funcional, todo instalable como mod.

## REFERENCIAS
| Tema | Dónde |
|---|---|
| Estado consolidado del port | `AGENTS.md` §3.4 |
| Swap nativo / mods override | `AGENTS.md` §4.2, §10; `docs/02_mods/` |
| Roster/SLXS (histórico) | `docs/HOJA_DE_RUTA.md` (fases 2-3) |
| Feedback comunidad (P0-P5) | `docs/HOJA_DE_RUTA_COMUNIDAD.md` |
| Formato bin | `docs/03_formatos/` + `AWO_FORMAT.md` |
| Plan 1.1.1 (depurado/Linux) | `docs/PLAN_1.1.1.md`, `docs/PLAN_LINUX.md` |
| Código del guest (parser real) | `generated/`, `generated_eu/` |
