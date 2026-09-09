# HOJA DE RUTA ACELERADA 2026-09 — Compresión radical de tiempos

> Actualizado: 2026-09-08. Esta hoja **sustituye como plan de ejecución activo**
> a `HOJA_DE_RUTA_2026_09.md` (madurez post-1.1.1, Fases 1-3) y a
> `RE_MASTER_2026_09.md` (plan rector de RE extremo a extremo). Consolida el
> **dictamen de arquitectura (Astra)** cuyo objetivo es reducir los tiempos de
> Luna (2-5 días → 3-6 meses) a **semanas** invirtiendo el orden de trabajo:
> *modding primero, comprensión como subproducto*.
>
> El `RE_MASTER_2026_09.md` sigue siendo válido como protocolo de laboratorio
> (baseline, hashes, clasificación de fallos). El `DICTAMEN_GPT6_ASTRA.md`
> sigue siendo la guía vigente para slots nativos y port. Esta hoja añade la
> capa de ejecución acelerada y su automatización.

---

## 1. PRINCIPIO RECTOR (por qué los tiempos se comprimen)

Luna estimó con el modelo clásico de RE: *entender el formato → luego
modificarlo*. Ese supuesto está invertido en este proyecto porque ya existe el
70% de la infraestructura. El principio rector nuevo:

> **Hacer que la RE sea un subproducto del modding, no su prerequisito.**

Consecuencias operativas:
1. **Swap-first, formato-después**: para redistribuir un recurso (modelo,
   retrato, CAM, LIPS, aura, voz, moveset) NO hace falta entender su formato:
   se mueve como blob con mid-insert. Solo se RE-documenta el formato cuando se
   quiere *editar* ese recurso.
2. **Reader-first RE**: cada formato se documenta encontrando la función del
   guest que lo lee (código recompilado en `generated/`) y trazando el PC guest
   por lectura AFS. No se diseca binario a ciegas.
3. **Corpus-driven parsing**: el parser "sólido" se logra parseando TODOS los
   bins a la vez y validando contra el render del juego, no uno a uno.
4. **Reuso del conocimiento PS2**: la HD 360 es gemela big-endian del PS2. La
   comunidad ya decodificó AMO/AMT/AMB/aura/SLES. El diff BE/LE es el parser.
5. **Exponer lo que ya existe**: el disco contiene ~183 personajes (Bulma,
   Babidi, Kibito, Giru, Saibaiman…). No crear personajes de la nada: hacer
   jugables los que ya tienen recursos en `data_cmn.afs` vía parche de memoria
   del guest (Vía 2 del dictamen).
6. **LLM sobre código recompilado**: `generated/` es C++ analizable por LLM.
   Con los traces, el mapeo de tablas se hace en horas, no semanas.

---

## 2. ESTADO REAL DEL TERRENO (inventario de lo ya hecho)

| Sistema | Estado | Evidencia |
|---|---|---|
| Override AFS por entrada + mid-insert virtual | ✅ FUNCIONA | `AfsFindModOverride`, `AfsGetVirtualTable` en `rexglue-sdk-0.10/src/filesystem/afs.cpp` |
| LZX compresión `/N:2048` + padding to_read | ✅ FUNCIONA | `swap_b3.py`, `texture_b3.py` |
| Swap nativo HD→HD | ✅ VALIDADO | `sw_goten_nativo`, `sw_vegeta424`, `swap_96_on_327` |
| Inyección PS2→HD (Vía A) | ✅ VALIDADO | `cell_npm4` (umbral binario 0.8) |
| Catálogo de personajes | ✅ 183 | `mod center hd/catalog_b3.cat` |
| Trazado de reads AFS → entrada | ✅ | `HostPathFile::ReadSync` + `HostPathEntry::OpenMapped` → `dbz1_afs_reads.log` (gate `dbz1_diag_logging`) |
| Trazado del roster/select (guest) | ✅ | `src/roster_trace.cpp`: `sub_8217F478/F3F0/F520/A920/A8B8/A618/ADE8/80AA0` → `dbz1_roster_trace.log` |
| Tabla de retratos del select | ✅ LOCALIZADA | `0x82372818` (39 slots × 2 u32), consumidor `sub_8217F3F0` |
| Tabla de bins por personaje | ✅ LOCALIZADA | `0x823268C0` (runs de índices AFS, separador 0xFFFFFFFF) |
| Auditoría de `data_cmn.afs` | ✅ 3990 entradas | `docs/03_formatos/AUDITORIA_DATA_CMN.md` + `mod center hd/data_cmn_map.txt` |
| Stages | ✅ ~20 localizados | bins 44-69 + 3735/3784…; layout de vértice ≠ personaje (pendiente RE) |
| Movesets | ◑ a nivel de bin | bins `#ACM#AMB` grandes por personaje (Krillin 332/333, Goku 290-292) |
| Dump imagen del guest | ✅ | `out/analysis/guest_image/dbz3_us_image.bin` (0x82000000-0x826D0000) |
| Parser PS2/AWO | ✅ parcial | `awo_tools/` (65 scripts): `awg_to_obj_b3`, `awg0_export`, `awg_cara_export`, `afs_scan`, `stage_analyze` |
| Pipeline de port PS2→HD | ✅ | `mod center hd/ports/port_ps2_b3_{extract,geometry,draw,pack,verify,inject}.py` |

**Conclusión**: el "Laboratorio/AFS" que Luna estimó en 2-5 días ya está
construido. Lo que falta es automatización (F0) + las capas de contenido.

---

## 3. LOS 7 ACELERADORES ARQUITECTÓNICOS (A1-A7)

| Id | Acelerador | Qué desbloquea | Coste |
|---|---|---|---|
| A1 | Swap-first, formato-después | 80% de la intención (modelos/retratos/CAM/LIPS/aura/voz/movesets) se cubre moviendo blobs | 1-2 días (swap_matrix.py) |
| A2 | Reader-first RE | Cualquier formato se documenta por su función lectora en `generated/` | horas/forma |
| A3 | Corpus-driven parsing | Parser sólido validando contra TODOS los bins + Content DB gratis | 1-2 días (corpus_scan.py) |
| A4 | Reuso del conocimiento PS2 | Parser HD por diff BE/LE, sin RE desde cero | días |
| A5 | LLM sobre código recompilado | Mapa guest/tablas en horas | — |
| A6 | Memoria-patch sobre slot nuevo | Personaje jugable (exponer NPC) en días, no semanas | 2-4 días |
| A7 | Plataforma = ingeniería, no RE | Experiment Runner + Content DB son capas sobre primitivas | 2-4 semanas |

---

## 4. LÍNEAS DE TRABAJO Y CÓMO EJECUTARLAS

### L4.1 — Laboratorio AFS (CERRADO, solo F0 de automatización)

**Objetivo**: todo lo que se toca se verifica solo: hashes, región, mods
activos, logs clasificados. **Herramienta**: `tools/lab_f0.ps1`.

Procedimiento de uso:
```
tools/lab_f0.ps1 -OutDir out\analysis\f0          # manifest + inventario + log summary
tools/lab_f0.ps1 -ClassifyLogs only               # clasifica logs sin tocar nada más
tools/lab_f0.ps1 -DryRun                          # revisar antes de escribir
```
Qué produce:
- `manifest.json`: SHA256 de `dbz3.exe`, DLLs canónicas, `default.xex`,
  `data_cmn.afs` us/eu; detección de región US/EU por MD5 del xex.
- `mods_inventory.txt`: lista de mods y su estado (activo / `.disabled`), y
  qué entradas AFS overridean (crítica para evitar la "contaminación de tests").
- `logs_classified.txt`: clasificación automática del último log de cada sesión
  (`dbz3_*.log`): `AFS OVERRIDE HIT`, `AFS MOD READ`, `got < to_read` (padding
  roto), crash (código), `rexruntime` stale (ausencia de `AfsGetVirtualTable`).
- `exit_report.txt`: resumen legible (todo OK / anomalías).

Criterio de aceptación: una sesión de trabajo empieza con `lab_f0.ps1` y el
resultado en 2 minutos.

### L4.2 — Parser HD sólido + Content Database (CORPUS)

**Objetivo**: JSON/SQLite de TODAS las entradas de los AFS (formato, tamaño,
magics, dependencias, estructura AWO). **Herramienta**: `awo_tools/corpus_scan.py`.

Procedimiento:
```
python awo_tools/corpus_scan.py us\data_cmn.afs -o out\analysis\corpus -r 0-3990
python awo_tools/corpus_scan.py eu\data_cmn.afs -o out\analysis\corpus -r 0-3990
python awo_tools/corpus_scan.py us\data_eng.afs -o out\analysis\corpus
```
Qué produce (por AFS):
- `corpus_<afs>.json` — por entrada: índice, size físico, size descomprimido,
  magics (#AMB/#AWO/#AWG/#AZT/#ACM/#AWM/#AMO0), nº de AWG/descriptores/bones,
  labels raíz, cluster de formato, sospechoso (NaN/ERR).
- `formats_summary.json` — clusters: cuántas entradas por formato, rango de
  tamaños, bins de personaje vs stages vs retratos.
- `corpus_all.db` (SQLite) — consultable por SQL para cruzar dependencias.
- `anomalias.txt` — entradas que no parsean (ERR) → candidatas a nuevos
  formatos o a error de tooling.

Regla: **NO se sobreescribe el corpus de referencia sin diff**. El corpus es la
semilla de la Content DB (fase plataforma).

Criterio de aceptación: >95% de entradas clasificadas; el 100% de los bins de
personaje identificados en el corpus coinciden con `MAPA_ROSTER_HD.md`.

### L4.3 — Swap-matrix genérica (A1)

**Objetivo**: mover CUALQUIER blob de recurso entre slots y entre regiones,
sin saber su formato. **Herramienta**: `mod center hd/swap_matrix.py`
(generaliza `swap_b3.py`).

Procedimiento:
```
python swap_matrix.py --afs us\data_cmn.afs --from 3934 --to 3930 \
    --type portrait --mod nappa_portrait_on_krillin
python swap_matrix.py --afs us\data_cmn.afs --from 333 --to 404 \
    --type moveset --mod krillin_moveset_on_tenshinhan
python swap_matrix.py --region us --from 327 --to 327 --type model \
    --mod tien_ps2  (bins ya generados por el pipeline de port)
```
> ⚠️ El ejemplo del roadmap original (`333→400`) era incorrecto: **400 es MODELO
> de Tenshinhan** (#AWO+23×#AWG), no moveset. El moveset de Tenshinhan es **404**
> (#ACM×3, 1747360 B ≈ el de Krillin). Verificado con `--describe`.

✅ **Mods de control creados y verificados 2026-09-08** (LZX validado por
decompresión, en `out/build/win-amd64-release/mods/`):
- `nappa_portrait_on_krillin` (3934→3930, mid-insert virtual 122880 ✓)
- `krillin_moveset_on_tenshinhan` (333→404, anm.bin 917504 = to_read ✓)
- `krillin_dmg_test` (attack 15 Krillin → daño 200, geom.bin 900344, daño
  confirmado en el bin descomprimido ✓)
> ⚠️ Testear UNO a la vez (regla 1). Los mods viejos de test están `.disabled`.
Qué hace:
- Extrae la entrada origen (LZX → bin → verifica magic según `--type`).
- Comprime `/N:2048`, calcula `to_read` + `to_read_virtual` (mid-insert).
- Instala como override `mods/<mod>/<us|eu>/<afs>/<entry>/geom.bin` + manifest.
- `--dry-run` informa sin instalar; `--force` reemplaza.

Criterio de aceptación: el primer swap de tipo `portrait` y `moveset` en juego
tienen `AFS OVERRIDE HIT` en el log (control = verificar el log con L4.1).

### L4.4 — Roster/slots nativos (A6 + dictamen)

✅ **Mapa definitivo del roster resuelto 2026-09-08**: los 39 slots del select
identificados por nombre cruzando la tabla del guest con el AFL Pal (US = PAL
+45 en la región IMG): `out/analysis/roster_slots.txt` + `MAPA_ROSTER_HD.md`
§7. El roster son 38 personajes + Random (slot 38). Esto deja la tabla
`0x82372818` completamente decodificada (slot → 2 retratos US).

Guía de ejecución (hitos 0-3 del dictamen, ya avanzados por `roster_trace.cpp`):

1. **Hito 0 (hecho)**: baseline congelado + override efectivo + hashes.
2. **Hito 1**: re-validar `cell_port_Afix_test` en solitario (un solo mod
   activo). Control: swap HD→HD conocido antes de cualquier PS2→HD.
3. **Hito 2**: ◑ **estructura del registro de 184 B decodificada 2026-09-08**
   (RE estática, `MAPA_ROSTER_HD.md` §9.1): campos u16 +12/+14/+18/+68/+114,
   tabla índice `0x8238B670` (≥4 u32), slot object (+28/+48/+60/+62/+64/+68),
   tabla de retratos `0x82372818` indexada por `(s16@+64)*2 + bit0@+62`,
   `0xFFFF` en +64 = slot vacío. **Los VALORES son runtime**: cerrar el
   conteo/celdas con `dbz1_roster_trace.log` (F478/F3F0/F520/A920/A8B8/A618/
   ADE8/80AA0).
4. **Hito 3**: añadir una celda alias de un HD existente (candidato Android 16),
   resolviendo al ORIGINAL sin duplicar CAM/ANM/voz/aura.
5. **Hito 4+**: identidad independiente → personaje NPC expuesto (Bulma/Babidi).
6. Paralelo: discriminador de Vía B (port exacto) — investigación acotada.

> 🔴 **Tabla de bins `0x823268C0`: consumidor NO localizable (2026-09-08)** — el
> address no se referencia en NINGUNA forma en `generated/` (ni lis/addi, ni
> ori, ni puntero en imagen). Acceso vía puntero runtime o base de struct.
> No bloquea: el mapeo personaje→bins ya está resuelto empíricamente.

No tocar `generated/` en esta línea: los cambios van en `src/mods/native_roster`
(hooks + parche de memoria), manifest por región+hash, opt-in.

### L4.5 — ACM/movesets (A1 + A2)

Orden de avance:
1. **Swap ACM como blob** (día 1): `swap_matrix.py --type moveset` para
   redistribuir movesets entre personajes. Requiere confirmar cuál bin del
   grupo es el moveset (columna ANM de `MAPA_ROSTER_HD.md`).
2. **Formato ACM** — ✅ **RE ESTÁTICA HECHA 2026-09-08** (sin entrar en combate):
   `docs/03_formatos/ACM_FORMAT.md`. El bin moveset es `#AMB → #CSK + 3×#ACM`
   (BSK/AMM PS2 renombrados, BE). Decodificada la cadena de edición:
   attack code → bloque de animación → lista HR → datos HR (daño/stun/pushback).
   **🔴 Identidad PS2↔HD confirmada**: BSK/AMM PS2 (LE, `ps2_games/B3 GH/USR/
   data_cmn.afs`, MISMOS índices) = #CSK/#ACM HD (BE) — mismos tamaños y
   offsets. El diff LE/BE ES el parser (A4). Campos HR = `[damage u16][code u16]`
   empaquetados. Herramientas: `awo_tools/acm_parse.py` + `acm_analyze.py`.
3. **Edición de habilidades**: ✅ **HERRAMIENTA CORREGIDA 2026-09-08** tras el
   crash de `krillin_dmg_test`. El primer `csk_edit.py` parcheaba FRAMES de los
   bloques AP (mal interpretados como daño) → crash en combate (NULL en
   `sub_820800A8`). **Formato real decodificado** (BSK_breakdown.pdf + dif PS2):
   attack code → bloque animación → **AP type 1** (Hit properties) → **HR code**
   (u16 @bytes 6-7) → **bloque HR** (`hr_off + code*128`, 8 líneas × 16 B con
   `damage u16`). `csk_chain.py` (analiza) + `csk_edit.py` (edita, verificado
   attack 0x21b → HR 28/29/... daño 100). Mod `krillin_dmg_test` reinstalado
   con la cadena correcta.
   🔴 **SEGUNDO CRASH DIAGNOSTICADO Y ARREGLADO 2026-09-09**: el mod corregido
   seguía crasheando en el SELECT (misma firma NULL, 3/3). Causa: el runtime
   virtual mid-insert dejaba lecturas inconsistentes (offsets viejos sin
   traducir) cuando un mod CREÍA una entrada temprana (333), y el parser #AMB
   despachaba el magic basura `#ACP` (sin handler). Fix: **`AfsRebuildPath`**
   materializa un AFS físico reconstruido cuando hay crecimiento (todas las
   lecturas consistentes). Pendiente: validar en juego que `krillin_dmg_test`
   ya no crashea y que el daño 100 se aplica.

Criterio de aceptación: un moveset redistribuido visible en combate + una
habilidad editada funcional.

### L4.6 — Stages

1. ✅ **Estructura del bin de stage RE'd 2026-09-08** (sin entrar en combate):
   `docs/03_formatos/STAGES_FORMAT.md`. Stage = contenedor `#AMB` →
   `#ZDD` (geo 1.48 MB) + `#CAD`/`#CAS` (tablas cámara) + `#SPX` (script LE
   "ver 0.01") + `#AMB` anidado esparso (641 slots: `#ACE` colisión + `#AWO`/
   `#AZT`/`#ACM` por elemento). Stages confirmados: bins pares 44-68 (bins
   impares 45-69 = auxiliares); 3735-3847 = variantes HD.
2. ✅ **Nombres de stages confirmados** (AFL Pal, alineado 1:1): 44=RED_RIBBON
   BASE_MAP, 46=WORLD_TOUR, 48=BUU_BODY, 50=CELL_RING, 52=GRANDPA_HOUSE,
   54=KAI_WORLD, 56=ISLAND, 58=TIME_BOLIC_CHAMBER, 60=NAMEK, 62=PLAINS,
   64=DESERT, 66=CITY, 68=DAMAGED_CITY (impares = SE_). Videos select = `*_SELEC.sfd`
   (~US 3936-3943).
3. RE del layout de vértice de stage (lecturas FLT_MAX ≠ personaje).
4. RE del bytecode #SPX (VM de script; `+0x18`=count, LE) — PENDIENTE.
5. Duplicar un stage + modificar geometría/textura → validar cargable.
6. Referencia: los mods de stages PS2 de la comunidad (Muscle Tower) como
   fuente de práctica.

### L4.7 — Plataforma (A7, tras L4.1-L4.3)

- Experiment Runner: `tools/run_experiment.ps1` — manifiesto de experimento
  (región, mods activos, pasos, criterio) + ejecución + captura + clasificación
  automática (reusa la lógica de clasificación de L4.1).
- Content DB: se alimenta del corpus (L4.2) + `MAPA_ROSTER_HD.md`.
- Mod Authoring: capa sobre `swap_matrix.py` + catálogo `catalog_b3.cat`.

---

## 5. SPRINT 0-4 (CALENDARIO DE EJECUCIÓN)

| Sprint | Contenido | Entregable |
|---|---|---|
| S0 (hoy) | Automatización: `lab_f0.ps1`, `corpus_scan.py`, `swap_matrix.py` + roadmap | 3 herramientas + esta hoja |
| S1 | F0 ejecutado (manifest) + corpus completo us/eu + primer swap portrait/moveset | `out/analysis/f0/`, `out/analysis/corpus/`, mod de control |
| S2 | Mapa roster/select cerrado + exponer 1er NPC (memoria-patch) | personaje NPC jugable |
| S3 | ACM: swap + formato + 1ª habilidad editada | moveset redistribuido + habilidad nueva |
| S4 | Stage duplicado + Experiment Runner | stage nuevo + runner |

### 5.1 S0 — HECHO 2026-09-08 (resultados iniciales)

- **`tools/lab_f0.ps1`** operativo. Primer uso destapó que `tien_ps2_on_krillin`
  estaba ACTIVO (sin `.disabled`) → desactivado para baseline limpio.
- **`awo_tools/corpus_scan.py`** operativo. `out/analysis/corpus/corpus_all.db`
  (6699 filas): data_cmn us/eu (3990) + data_eng (2709).
- **Hallazgo de formato por corpus**: `#SPX ver 0.01` = formato de STAGE
  (presente en bins 44-69; 1501 entradas #SPX). Los stages grandes (44-69 y
  3735-3847) se confirman en el cluster `model` (>1.5 MB, hasta 6.2 MB).
- **Grupos de personaje mapeados por corpus**: cada grupo = modelos
  (`#AWO`+18 `#AWG`, 600-900 KB) + moveset (`#ACM`×3, 1.5-2.4 MB) + auxiliares
  (`#AMB`+`#SPX`+`#ACC`). P.ej. Krillin: modelos 327-329, movesets 320/324/333.
- **`mod center hd/swap_matrix.py`** operativo (describe + swap dry-run
  validados: retrato 3934→3930 con mid-insert virtual).
- **Diagnóstico pendiente (crash 0xC0000005 @0x7ff7b6ccd279)**: el log de la
  última sesión muestra el crash con Tien activo pero SIN `AFS OVERRIDE HIT` →
  el override no se sirve por esa ruta. Hipótesis a investigar: (a) el guest
  lee la entrada 327 por mmap (`OpenMapped`) que NO sirve overrides, o (b)
  desfase de región. El crash es reproducible (2 sesiones, mismo addr) y NO
  puede atribuirse al modelo hasta tener un override servido confirmado.

### 5.2 S1 — plan concreto (próximo)

1. `tools\lab_f0.ps1` antes de tocar nada (verde = baseline limpio).
2. Activar UN solo mod de control nativo (p.ej. `sw_goten_nativo` → slot 327) y
   verificar `AFS OVERRIDE HIT` + `AFS MOD READ` en el log (criterio: got=to_read).
3. Crear el mod de control portrait con `swap_matrix.py` (3934→3930) y validar
   en el select.
4. Crear el mod de moveset con `swap_matrix.py` (333→400 Krillin→Tenshinhan) y
   validar en combate.
5. Solo cuando un override haya sido servido y verificado: reactivar Tien
   (`swap_matrix.py --bin <bins/tien.bin> --to 327`).

## 6. REGLAS DE OPERACIÓN (heredadas, críticas)

1. **UN SOLO mod activo por test** — `AfsFindModOverride` sirve el primero en
   orden alfabético; un mod olvidado invalida los tests (contaminación).
2. **Verificar región en los logs**: el override vive bajo `us/` o `eu/` según
   la región real del juego (lección de Tien: se probó EU con override US).
3. **`to_read` FIJO del guest**: el bin comprimido debe caber salvo mid-insert
   virtual (crece solo si bin > to_read).
4. **Compresión LZX `/N:2048`** y **padding exacto** (si `got < to_read` → crash).
5. **DLLs canónicas**: tras `cmake --build` volver a copiar `rexruntime.dll`
   (el build la sobrescribe con versión stale). Verificar con grep de
   `AfsGetVirtualTable`.
6. **No tocar `generated/`** para slots: hooks + memoria en `src/mods/native_roster`.
7. **Tabla AFS en offset 8**, no 0x10.

## 7. CRITERIOS DE ACEPTACIÓN GLOBALES

- Un `lab_f0.ps1` verde antes y después de cada cambio.
- Corpus: >95% clasificado, consistente con `MAPA_ROSTER_HD.md`.
- Al menos UN personaje NPC jugable (select + combate).
- Un moveset redistribuido + una habilidad editada funcionales.
- Un stage nuevo cargable.
- Todo instalable como mod (override por entrada).

## 8. RIESGOS (honestos)

- **Port completo (Vía B) sigue bloqueado** — no es ruta crítica (la inyección
  entrega); no invertir hasta el discriminador.
- **VB2 (cara/piernas)** sigue bloqueador puntual; rodear con swap HD→HD.
- **La validación en juego es el cuello real** → por eso F0 (logs automáticos)
  es el sprint 0, no el último.
- **Los hallazgos PS2 de la comunidad son referencia, no autoridad**: todo mod
  portado requiere re-validación HD.

## 9. REFERENCIAS

| Tema | Dónde |
|---|---|
| Dictamen slots/port (guía vigente) | `docs/DICTAMEN_GPT6_ASTRA.md` |
| Plan rector RE (protocolo laboratorio) | `docs/RE_MASTER_2026_09.md` |
| Auditoría de contenido | `docs/03_formatos/AUDITORIA_DATA_CMN.md`, `mod center hd/data_cmn_map.txt` |
| Roster HD | `docs/03_formatos/MAPA_ROSTER_HD.md` |
| Formato bin/AWO | `docs/03_formatos/` + `docs/03_formatos/AWO_FORMAT.md` |
| Trazado roster (guest) | `src/roster_trace.cpp` |
| Trazado reads AFS (runtime) | `rexglue-sdk-0.10/src/filesystem/devices/host_path_file.cpp`, `host_path_entry.cpp` |
| Override/mid-insert (runtime) | `rexglue-sdk-0.10/src/filesystem/afs.cpp` |