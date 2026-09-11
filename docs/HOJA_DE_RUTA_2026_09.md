# HOJA DE RUTA 2026-09 — Madurez del proyecto (post-1.1.1)

> Actualizado: 2026-09-14. Estado base: **v1.2.1 publicada** (Latest), juego muy
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
3. **Localizar los MOVESETS/habilidades — RESUELTO a nivel de bin (2026-09-07)**:
   el moveset/animación de cada personaje es el bin `#ACM#AMB` grande de su
   grupo (columna ANM en `MAPA_ROSTER_HD.md`, p.ej. Krillin 332/333, Goku
   290-292, Vegeta 433-435/437). Identificados por nombre AFL (CAM/LIPS/ANM) +
   tamaño (1.2-2.4 MB), no solo por firma `#ACM`. Pendiente: estructura interna
   de la tabla de movimientos en `generated/` (F3.2).
4. **🔴 Mapear el SLXS/roster HD: ✅ RESUELTO (2026-09-07)** — no existe archivo
   SLXS en el HD; el roster vive en la **imagen descifrada del guest**, no en
   `data_eng.afs`. Se volcó `out/analysis/guest_image/dbz3_us_image.bin` con un
   mini-tool (`out/analysis/guest_image/`, CMake+fuente; carga el xex en
   tool_mode con el SDK instalado y vierte membase 0x82000000-0x826D0000) y se
   localizaron:
   - **Tabla de retratos/slots del select** `0x82372818`: 78 u32 = 39 slots ×
     2 entradas; slot 10 = Krillin (3930), slot 19 = Nappa (3934) —
     **CONFIRMADO con el experimento de sustitución** (§3.3.2). Orden completo
     en `MAPA_ROSTER_HD.md` §7.
   - **Tabla de bins por personaje** `0x823268C0`: runs de índices AFS con
     separador 0xFFFFFFFF (modelos→CAM→LIPS/ANM).
   - **Función consumidora** `sub_8217F3F0` (recomp `.15.cpp:13877`,
     `lis -32201; addi r9,r9,10264` → índice slot*8+flag*4 sobre 0x82372818).
   El trace previo de reads AFS (`dbz1_afs_reads.log`) y el experimento de
   sustitución convirtieron las asociaciones temporales en mapeo confirmado;
   las 17 `#AZT1` 288x352 exportadas quedan documentadas en
   `AUDITORIA_DATA_CMN.md` §3.2.1.

### 3.2 Habilidad adicional (por duplicado)
1. RE del formato de habilidad en `generated/` (tabla de movimientos).
2. Duplicar una habilidad existente (p.ej. un Ki Blast) + modificar parámetros
   (daño/animación/nombre/textura).
3. Instalar como mod (override por entrada) + validar en combate.

### 3.3 Slot de personaje adicional (por duplicado) — el más cercano
1. **Inventario de dependencias del personaje — CERRADO (2026-09-07)**: modelo
   `#AMB`, auxiliares (CAM/LIPS/SCOUT), moveset/animación (bin `#ACM` grande) y
   aura (0-43) mapeados por personaje en
   `docs/03_formatos/MAPA_ROSTER_HD.md` (catálogo HD + probe + AFL Pal, desfase
   +6 validado). Los retratos son candidatos `#AZT1` 3884-3960 con asociación
   temporal de confianza media (`AUDITORIA_DATA_CMN.md` §3.2.1).
2. **✅ Experimento de menor riesgo — HECHO (2026-09-07)**: sustitución sobre
   slot existente. El mod `portrait_swap_test` (servir entry 3934 en slot 3930
   vía override por entrada) hizo aparecer el retrato de **Nappa en Krillin**
   en el select → confirma las parejas 3930=Krillin y 3934=Nappa y valida el
   override de retratos. Además se volcó la imagen del guest y se localizó la
   **tabla de retratos del select** (`0x82372818`, 39 slots) y la tabla de bins
   (`0x823268C0`) — el roster vive en datos del guest, con consumidor
   `sub_8217F3F0` (§7 de MAPA_ROSTER_HD). 🟢 El siguiente paso es estudiar cómo
   el guest enumera los 39 slots (bucle/conteo) y probar UN slot nativo.
3. Confirmar dónde vive el índice de slot en el guest recompilado: buscar el
   consumidor de la tabla de entradas del select y validar si el bin es un
   inmediato, una tabla estática o un descriptor cargado desde `data_eng`.
   **→ Avanzado 2026-09-07**: consumidor localizado (`sub_8217F3F0`), la tabla
   es estática en la imagen del guest (`0x82372818`), no un descriptor de
   `data_eng`. Pendiente: el bucle/conteo de 39 slots y decidir hook-host vs
   re-codegen (§9 de MAPA_ROSTER_HD).
4. Implementar la duplicación real solo después: ampliar/reemplazar el
   descriptor de slot, asociar el modelo de `data_cmn`, asociar el retrato del
   `#AZT1`/composite y conservar los auxiliares de voz/animación del destino.
5. Validar por capas: modelo en combate, selección/retrato, transformaciones,
   voz y guardado. Cada capa debe tener un override aislado para localizar la
   dependencia que falte.

### 3.4 Stage adicional (por duplicado)
1. Localizar bins de stage (3.1.2) + dónde se lista en el select.
2. Duplicar un stage existente (bins + entrada de select) + modificar
   geometría/textura.
3. Validar: stage seleccionable y cargable.

### 3.5 Herramienta común: "duplicar entrada"
Script/pipeline genérico que duplique una entrada (AFS + SLXS/select) y apunte
a un bin nuevo — reutilizable para personajes, stages y habilidades.

### 3.6 🔴 PLAN GPT-6 ASTRA — slots nativos + port (dictamen 2026-09-07)

> **Documento completo**: `docs/DICTAMEN_GPT6_ASTRA.md` (verbatim + anexo con
> hallazgos nuevos). El dictamen externo corrige 3 supuestos y propone el plan
> de ejecución 0-7 que sustituye el "paso 4/5" de 3.3 para slots nativos.

**Correcciones que aplican ya (verificadas en nuestro estado):**
1. **`0xFFFF` = celda vacía, NO personaje libre**: distinguir celda de
   interfaz / ID de retrato / ID de personaje / ID de forma. No asumir slots
   libres.
2. **`bone@+28` solo es válido para sec34 (Krillin)**: formato C usa `+40`.
   Seleccionar layout por AWG, nunca offset global.
3. **El mid-insert amplía una entrada AFS existente; NO añade índices AFS
   nuevos**: aumentar el conteo de `data_cmn.afs` exige validar conteo, tabla
   virtual y consumidores por separado.

**Plan de ejecución (0-7 + paralelo acotado):**

| Orden | Trabajo | Criterio de aceptación |
|---|---|---|
| 0 | Congelar baseline, DLL y override efectivo | Resultados repetibles y hashes registrados |
| 1 | Restaurar inyección conocida y ejecutar Afix aislado | Modelo visible estable; diagnóstico sin contaminación |
| 2 | Trazar conteo, celdas y registro de 184 B | Distinguir capacidad, identidad y enumeración |
| 3 | Añadir una celda alias de un HD existente | Original y duplicado seleccionables, sin sustitución |
| 4 | Crear identidad independiente y resolución de recursos | Ambos combaten simultáneamente sin compartir estado indebido |
| 5 | Escanear rigs y producir primer IW compatible | Silueta y animación aceptables sobre rig HD |
| 6 | Integrar ese modelo en el slot independiente | Select→combate→victoria→revancha estable |
| 7 | Completar voz, textos, formas y persistencia | Guardado probado con perfil desechable; regresión del roster |
| Paralelo | Discriminador de Vía B (port exacto) | Primera dependencia demostrada antes del regenerador general |

**Decisiones del dictamen que adoptamos como criterio:**
- **Slots nativos**: orden de vías = (1) reutilizar celda reservada real →
  (2) parche de datos en memoria guest → (3) híbrido tablas+hooks mínimos.
  **NO re-codegen ni tocar `generated/` como primer paso.** Módulo
  `src/mods/native_roster` fuera de `generated/`, manifest por región+hash,
  opt-in, abort si discrepancia.
- **Primer personaje = duplicar comportamiento, no archivos**: celda alias que
  resuelve al ORIGINAL (candidato: Android 16), sin duplicar CAM/ANM/voz/aura
  inicialmente; modelo/retrato propios solo cuando la resolución independiente
  esté demostrada.
- **Port**: Vía A (inyección) = vía de ENTREGA; Vía B = investigación acotada
  con entregables cerrados (round-trip, permutación mínima fallida, primera
  divergencia runtime). Inversión en Vía A: correspondencias por hueso/zona/
  material, preservación de costuras, umbrales por región, rechazo de
  correspondencias dudosas conservando vértice HD.
- **Guardado**: perfil desechable + slot experimental NO persistente; no
  escribir IDs nuevos en partidas normales.

**Orden de ejecución recomendado** (por viabilidad): 3.3 (personajes, swap
ya validado) → 3.4 (stages, requiere localizar bins) → 3.2 (habilidades, la
RE más profunda). El orden del usuario (habilidades → personajes → stages) es
válido como prioridad de interés; la ejecución técnica sigue el de viabilidad.
**⚠️ El plan del dictamen (§3.6) es la guía vigente para slots nativos y port**:
hitos 0-3 primero, escáner de rigs, luego IW.

---

## ORDEN DE EJECUCIÓN SUGERIDO

1. **Fase 1** (doc ligera) — beneficio inmediato en tokens de cada sesión.
2. **Fase 2.1** (código muerto) — riesgo bajo, limpieza rápida + release menor.
3. **Fase 2.2** (depuración) — solo lo que aparezca; no bloquea.
4. **Fase 3.1** (auditoría) — el mapa de contenido habilita todo el RE.
5. **Fase 3.3 → 3.4 → 3.2** (contenido nuevo por duplicados), siguiendo el
   **plan del dictamen (§3.6)** para slots nativos y port.

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
| **Dictamen GPT-6 Astra (plan slots + port)** | `docs/DICTAMEN_GPT6_ASTRA.md` |
| Briefing que originó el dictamen | `docs/BRIEFING_GPT6_ASTRA.md` |
