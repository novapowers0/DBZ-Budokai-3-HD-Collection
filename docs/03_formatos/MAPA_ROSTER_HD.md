# MAPA DE ROSTER HD — personaje → bins (data_cmn.afs)

> Fecha: 2026-09-07. Resultado de la Fase 3.3 (mapear roster + select).
> Consolidación de: `mod center hd/catalog_b3.cat` (modelos), probe real del
> AFS (`out/analysis/probe_result.txt`), `data_cmn.afl` Pal (nombres) y
> `modding resources/DBZ_B3_Character_Bin_List.txt` (agrupaciones comunitarias).
> Verificado empíricamente descomprimiendo entradas con xbdecompress.

## 1. CÓMO SE LEEN ESTAS TABLAS

- **Modelo** = bin `#AMB1 #AWO1 #AWG15-26 #AZT1` (stride 44, layout B3, §3.2 de AGENTS).
- **CAM** = bin de cámara (pequeño, ~20-110 KB, solo `#AMB`).
- **LIPS** = bin de animación de boca (~5 KB, `#AWO#AWG#ACM`).
- **ANM** = bin de animación/moveset (grande, `#ACM#AMB`, 1.2-2.4 MB descomp.).
- **SCOUT** = accesorio scouter (~5-6 KB).
- **AURA** = bin de aura del personaje (entradas 0-43).
- Los bins de modelos son **autocontenidos** (formato A/B/C autodetectado por el
  guest); el swap nativo HD→HD ya lo explota (AGENTS §3.4).

## 2. RELACIÓN DE NUMERACIÓN (validada empíricamente)

La HD 360 usa la numeración de `data_cmn` de la **Greatest Hits PS2**. La lista
de nombres `data_cmn.afl` (edición **Pal**, `modding resources/`) coincide con la
HD **exactamente** hasta la entrada 286 y a partir de ~293 con desfase
**+6** (la GH añadió 6 modelos extra de Goku "angelic", 281-287). La deriva
local rompe el desfase en Recoome/Raditz/Saibaman y Trunks; en esas zonas el
**catálogo HD (`catalog_b3.cat`) es la autoridad** para los modelos.

Regla práctica para nombres auxiliares: `nombre = AFL[entrada - 6]` para
entradas ≥ 287, **salvo** en las zonas con deriva (358-375, 381-395), donde
usar el tipo del probe + posición relativa al grupo.

**🔴 Desfase de retratos (región IMG, entrada ≥ 3839)**: `US = PAL + 45`.
PAL 3839-3917 (`*_IMG_0/_1.amt`) → US 3884-3962. Este desfase resuelve por
nombre TODOS los slots del select (§7) — verificado con los confirmados en
juego (Krillin US 3930, Nappa US 3934).

## 3. AURAS (entradas 0-43, en orden de personaje)

| # | Nombre | Personaje |
|---:|---|---|
| 0 | 16G_AURA | Android 16 |
| 1 | 17G_AURA | Android 17 |
| 2 | 18G_AURA | Android 18 |
| 3 | 1-STAR-DRAGON_AURA | Syn Shenron |
| 4 | 20G_AURA | Dr. Gero |
| 5 | BARDOCK_AURA | Bardock |
| 6 | BROLY_AURA | Broly |
| 7 | BUU_LONG_AURA | Majin Buu (gordo) |
| 8 | BUU_MED_AURA | Super Buu |
| 9 | BUU_SMALL_AURA | Kid Buu |
| 10 | CELL_AURA | Cell |
| 11 | CELL_Jr._AURA | Cell Jr. |
| 12 | LOADING_AURA | aura de carga |
| 13 | COOLER_AURA | Cooler |
| 14 | DABURA_AURA | Dabura |
| 15 | FREEZA_AURA | Freeza |
| 16 | GOGETA_0_AURA | Gogeta SSJ |
| 17 | GOGETA_1_AURA | Gogeta SSJ4 |
| 18 | GOHAN_LONG_AURA | Gohan adulto |
| 19 | GOHAN_MED_AURA | Gohan saga Cell |
| 20 | GOHAN_SMALL_AURA | Gohan niño |
| 21 | GOKU_SMALL_AURA | Goku niño |
| 22 | GINYU_AURA | Ginyu |
| 23 | GOKU_AURA | Goku |
| 24 | GOKU_GINYU_AURA | Goku (cuerpo Ginyu) |
| 25 | GREAT_SAIYAMAN_AURA | Great Saiyaman |
| 26 | GOTEN_AURA | Goten |
| 27 | GOTENKS_AURA | Gotenks |
| 28 | KULILIN_AURA | Krillin |
| 29 | KAIO-SHIN_AURA | Kaio-Shin |
| 30 | NAPPA_AURA | Nappa |
| 31 | PICCOLO_AURA | Piccolo |
| 32 | RADITZ_AURA | Raditz |
| 33 | RECOOME_AURA | Recoome |
| 34 | SAIBAIMAN_AURA | Saibaman |
| 35 | SATAN_AURA | Mr. Satan |
| 36 | TRUNKS_SMALL_AURA | Trunks niño |
| 37 | TRUNKS_LONG_AURA | Trunks futuro |
| 38 | TENSHINHAN_AURA | Tenshinhan |
| 39 | UUB_AURA | Uub |
| 40 | VIDEL_AURA | Videl |
| 41 | VEGETA_AURA | Vegeta |
| 42 | VEGETTO_AURA | Vegito |
| 43 | YAMCHA_AURA | Yamcha |

> Las auras comparten "LOADING_AURA" como patrón; un mod de aura sustituye el
> bin completo (mismo mecanismo que el modelo, override por entrada).

## 4. PERSONAJES — MODELO + AUXILIARES (bins HD definitivos)

> Los números de modelo = entrada AFS (verificados por descompresión). Las
> columnas CAM/LIPS/ANM/SCOUT = entrada AFS del auxiliar (probe + nombres Pal).
> `—` = el personaje no tiene ese bin propio (comparte el pool del grupo).

| Personaje | Modelos | CAM | LIPS | ANM | Scout |
|---|---:|---:|---:|---:|---:|
| Android 16 | 70, 71 | 72 | 73 | 74 | — |
| Android 17 | 75, 76 | 77 | 78 | 79 | — |
| Android 18 | 80, 81, 82 | 83 | 84 | 85 | — |
| Syn Shenron | 86, 87 | 88 | 89 | 90 | — |
| Dr. Gero | 91, 92 | 93 | 94 | 95 | — |
| Babidi | 96 | — | 97 | — | — |
| Bardock | 99, 100 | 101 | 102 | 103 | 98 |
| Buu Gohan | 104, 105 | — | 106 | pool 137-140 | — |
| Buu Gotenks | 107, 108 | — | 109 | pool 137-140 | — |
| Bulma | 110 | 111 | 112 | 113 | — |
| Buu Ghost | 114 | — | 115 | pool 137-140 | — |
| Buu Piccolo | 116, 117 | — | 118 | pool 137-140 | — |
| Broly | 119-122 | 123 | 124, 125 | 126, 127 | — |
| Majin Buu (gordo) | 128, 129 | 130 | 131 | 132 | — |
| Super Buu | 133, 134 | 135 | 136 | pool 137-140 | — |
| Kid Buu | 141, 142 | 143 | 144 | 145 | — |
| Cell | 146-151 | 152 | 153-155 | 156-159 | — |
| Cell Jr. | 160, 161 | 162 | 163 | 164 (165 pool) | — |
| Cooler | 166-171 | 172 | 173 | 174, 175 | — |
| Dabura | 176, 177 | 178 | 179 | 180 | — |
| Freeza | 181-193 | 194 | 195-198 | 199-213 | — |
| Veku (Gogeta gordo) | 214, 215 | 222 | 216, 221 | 223, 224 | — |
| Gogeta | 217-220 | 222 | 221 | 223, 224 | — |
| Gohan adulto | 225-230 | 231 | 232 | 233, 234 | — |
| Gohan saga Cell | 235-240 | 241 | 242, 243 | 244, 245 | — |
| Gohan niño | 246-248 | 249 | 250 | 251 | — |
| Goku niño | 252, 253 | 254 | 255 | 256 | — |
| Ginyu | 258, 259 | 260 | 261 | 262 | 257 |
| Goku (todas las formas) | 264-287 | 288 | 289 | 290-292 | 263 |
| Great Saiyaman | 293, 294 | 295 | 296 | 297 | — |
| Goten | 298-301 | 302 | 303 | 304 | — |
| Gotenks | 305-310 | 317 | 311, 316 | 318-324 | — |
| Ghost Gotenks | 312, 313 | 317 | 316 | 318-324 | — |
| Fat Gotenks | 314, 315 | 317 | 316 | 318-324 | — |
| Kibito | 325 | — | 326 | — | — |
| **Krillin** | **327, 328, 329** | **330** | **331** | **332, 333** | — |
| Kibito Kai | 334, 335 | 336 | 337 | 338 | — |
| Kaio-Shin | 339, 340 | 341 | 342 | 343 | — |
| Nappa | 345, 346 | 347 | 348 | 349 | 344 |
| Piccolo | 350-353 | 354 | 355, 356 | 358 | 357 |
| Raditz | 360, 361 | 362 | 363 | 364 | — |
| Recoome | 366, 367 | 368 | 369 | 370 | — |
| Saibaman | 371, 372 | 373 | 374 | 375 | — |
| Mr. Satan | 376 (pelo), 377, 378 | 379 | 380 | 381, 382 | — |
| Trunks niño | 383-386 | 387 | 388 | 389 | — |
| Trunks futuro | 390-395 | 396 | 397 | 398, 399 | — |
| Tenshinhan | 400, 401 | 402 | 403 | 404 | — |
| Uub | 405, 406 | 407 | 408 | 409 | — |
| Videl | 410, 411 | 412 | 413 | 414 | — |
| Vegeta | 416-429 | 430 | 431, 432 | 433-435, 437 | — |
| Vegito | 438-441 | 443 | 442 | 444 | — |
| Yamcha | 445-447 | 448 | 449 | 450 | — |

> ⚠️ Entradas sin catálogo pero con modelo real (probe): `237` = GOHAN_MED_2_00
> (Gohan saga Cell SSJ2/místico), `366`/`367` = Recoome, `371`/`372` = Saibaman,
> `376` = pelo Mr. Satan. El catálogo omite 237 y desplaza algunos labels de
> Recoome/Saibaman (359, 363 son scouts/accesorios, no modelos).

## 5. EFECTOS (451-505) Y RANGO DE TEXTURAS SUELTAS

- **451-503**: 52 bloques `#AZT` autónomos (128 KB-1 MB descomp.) — texturas de
  efectos/habilidades/HUD. `481` (~1 MB) está en este grupo.
- **504-555**: bins `#AMB#AZT` de efectos por personaje (el listado comunitario
  los nombra: 504 Fighting effects, 505 Android 16 ... 555 Yamcha).

## 6. RETRATOS DEL SELECT (candidatos `#AZT1` 3884-3960)

Extraídos 17 candidatos (todos `288x352`, hashes distintos):
`3892, 3898, 3900, 3902, 3912, 3918, 3922, 3924, 3925, 3930, 3934, 3938,
3940, 3942, 3952, 3958, 3960` (`out/analysis/azt_exports/`).

Asociación **temporal** del trace (`AUDITORIA_DATA_CMN.md` §3.2.1), confianza
media — pendiente de confirmación visual o por experimento de sustitución:

| Retrato | Modelo siguiente | Personaje |
|---:|---:|---|
| 3930 | 327 | Krillin |
| 3952 | 400 | Tenshinhan |
| 3960 | 445 | Yamcha |
| 3958 | 416 | Vegeta |
| 3938 | 350 | Piccolo |
| 3940 | 360 | Raditz |
| 3934 | 345 | Nappa |
| 3922 | 258 | Ginyu |
| 3942 | 366 | Recoome |
| 3912 | 181 | Freeza forma 1 |
| 3892 | 91 | Dr. Gero |
| 3898 | 128 | Majin Buu |
| 3900 | 133 | Super Buu |
| 3902 | 141 | Kid Buu |
| 3924-3925 | 264/270 | Goku |
| 3918 | 246 | Gohan niño |

> **✅ CONFIRMADO 2026-09-07**: el mod `portrait_swap_test` (servir la entry
> **3934** en el slot **3930** vía override por entrada) hizo aparecer el
> retrato de **Nappa en Krillin**. Quedan confirmadas AMBAS parejas:
> **3930 = retrato Krillin** y **3934 = retrato Nappa**, y además se valida el
> mecanismo completo: override por entrada de `data_cmn.afs` sirve retratos
> (AFS override + mid-insert virtual, sin tocar `data_eng.afs`).

## 7. 🔴 ROSTER EN EL GUEST (HALLAZGO 2026-09-07)

El roster del select **NO vive en `data_eng.afs`** (composite): vive en la
**imagen descifrada del guest** (`dbz3_us_image.bin`, volcada con
`out/analysis/guest_image/dump_image`). Localizadas dos tablas:

1. **Tabla de retratos / slots del select** en `0x82372818` (offset imagen
   `0x372818`): 78 u32 = **39 slots × 2 entradas** (par por slot, p.ej.
   slot 10 = `[3930, 3931]` = Krillin, slot 19 = `[3934, 3935]` = Nappa).
   Orden de slots del select documentado abajo.
2. **Tabla de bins por personaje** en `0x823268C0` (offset `0x3268C0`):
   runs de índices AFS separados por `0xFFFFFFFF` (modelos → CAM → LIPS/ANM);
   el run `[323..329]` contiene el grupo de Krillin (327/328/329).

La función consumidora de la tabla de retratos es `sub_8217F3F0` (recomp
`.15.cpp:13877`): `lis -32201; addi r9,r9,10264` → `0x82372818`, índice
`slot*8 + flag*4`. Esto abre la puerta a **añadir slots nativos** (§9): la
tabla está en datos del guest, no en código host.

**Mecanismo de slot (RE inicial)**: `sub_8217F478` (`.14.cpp:14437`) lee el
**slot id** como u16 en `r3+64`; si es `0xFFFF` = slot vacío (retorna sin
actuar); si no, llama a `sub_8217F3F0` para resolver el retrato. En
`sub_82180AA0` (`.15.cpp:13916`) se indexa una base de personaje con
**`mulli r10,r10,184`** (struct de 184 bytes/personaje) y se leen offsets
`+14/+18/+114` (u16). Los slots se enumeran con `cmpw r29,r11` (contador vs
byte en `r30+12`). → Para un slot nativo hay que entender esta base de 184 B
(cómo se llena) y dónde se decide el conteo.

### Orden real del select (39 slots — ✅ RESUELTO POR NOMBRE 2026-09-08)

> **Método**: cruce de la tabla de retratos del guest con el AFL Pal. En la
> región de IMG, **US = PAL + 45** (PAL 3839-3917 → US 3884-3962). Verificado:
> PAL 3885 KULILIN_IMG_0 → US 3930 (confirmado en juego), PAL 3889 NAPPA_IMG_0
> → US 3934 (confirmado), y PAL 3859 CELL_IMG_0 → US 3904, etc. Los retratos
> `*_IMG_0/_1.amt` del AFL nombran cada slot sin ambigüedad. Fuente completa:
> `out/analysis/roster_slots.txt`.

| Slot | Retratos US | Personaje | Slot | Retratos US | Personaje |
|---:|---|---:|---:|---|---|
| 0 | 3924 3925 | Goku | 20 | 3922 3923 | Ginyu |
| 1 | 3920 3921 | Goku (niño) | 21 | 3942 3943 | Recoome |
| 2 | 3918 3919 | Gohan (niño) | 22 | 3912 3913 | Freeza |
| 3 | 3916 3917 | Gohan (saga Cell) | 23 | 3884 3885 | Android 16 |
| 4 | 3914 3915 | Gohan (adulto) | 24 | 3886 3887 | Android 17 |
| 5 | 3926 3927 | Great Saiyaman | 25 | 3888 3889 | Android 18 |
| 6 | 3928 3929 | Goten | 26 | 3892 3893 | Dr. Gero |
| 7 | 3958 3959 | Vegeta | 27 | 3904 3905 | Cell |
| 8 | 3950 3951 | Trunks (futuro) | 28 | 3898 3899 | Majin Buu (gordo) |
| 9 | 3948 3949 | Trunks (niño) | 29 | 3900 3901 | Super Buu |
| 10 | 3930 3931 | **Krillin** | 30 | 3902 3903 | Kid Buu |
| 11 | 3938 3939 | Piccolo | 31 | 3910 3911 | Dabura |
| 12 | 3952 3953 | Tenshinhan | 32 | 3908 3909 | Cooler |
| 13 | 3960 3961 | Yamcha | 33 | 3894 3895 | Bardock |
| 14 | 3946 3947 | Mr. Satan | 34 | 3896 3897 | Broly |
| 15 | 3956 3957 | Videl | 35 | 3890 3891 | Syn Shenron |
| 16 | 3932 3933 | Kaio-Shin | 36 | 3944 3945 | Saibaman |
| 17 | 3954 3955 | Uub | 37 | 3906 3907 | Cell Jr. |
| 18 | 3940 3941 | Raditz | 38 | 3936 3937 | Random |
| 19 | 3934 3935 | **Nappa** | | | |

## 8. QUÉ HABILITA ESTO (F3.3)

- **Añadir personaje por duplicado**: duplicar modelo + CAM/LIPS/ANM + aura +
  retrato, y **extender la tabla de retratos del guest** (`0x82372818`) + la
  tabla de bins (`0x823268C0`) — el roster vive en la imagen del guest, y la
  función `sub_8217F3F0` lo indexa (§7). Paso siguiente: ver cómo el guest
  enumera los 39 slots (bucle/conteo) y si se puede parchear la tabla en
  runtime (hook host) o hay que re-codegen.
- **Sustitución en slot existente**: swap del bin modelo (ya validado) o del
  retrato (confirmado §6).
- **Moveset por duplicado**: el ANM del personaje es el bin grande `#ACM`
  de la columna ANM (validado: Freeza usa 199-213, Goku 290-292, etc.).
- **No tocar** `data_eng.afs` completo para cambiar un retrato: el override por
  entrada de `data_cmn.afs` alcanza para modelos y retratos sueltos.

## 9. PRÓXIMOS PASOS PARA SLOTS NATIVOS

### 9.1 ✅ REGISTRO DE 184 B / SLOT — ESTRUCTURA DECODIFICADA (RE estática 2026-09-08)

> Los VALORES del registro son runtime (la imagen estática los tiene a cero),
> pero la ESTRUCTURA ya está mapeada leyendo los accesos en `generated/`.

**Registro de 184 B** (base `0x8238B780`, `lis 0x8239 / addi -18560`):
- Indexado por la **tabla índice `0x8238B670`** (`lis 0x8239 / addi -18832`),
  array de u32 con **≥4 entradas** (offsets +0, +4, +8, +12 del array; en
  `sub_8217ADE8` se usan 4 índices).
- Acceso: `índice = tabla[i]` → `ptr = base + índice * 184`.
- Campos leídos (u16, relativos a `ptr`):

| Offset | Tipo | Quién | Uso observado |
|---|---|---|---|
| +12 | u16 | ADE8 | flag/id (selección por flags del slot) |
| +14 | u16 | ADE8, 80AA0, F3F0 | flag set (bit0/bit1 → lectura) |
| +18 | u16 | ADE8, 80AA0, F3F0 | se OR-mergea con +114 |
| +68 | u16 | 80AA0 | extra |
| +114 | u16 | ADE8, 80AA0, F3F0 | flag set, OR con +18 |

**Slot object** (`r3` en sub_8217F3F0/F520/80AA0, NO es el registro 184 B):

| Offset | Tipo | Contenido |
|---|---|---|
| +28 | u32 | callback/vtable (F520 escribe `0x8237F478`) |
| +40 | u32 | ptr (ADE8 lo pasa a sub_8217E008) |
| +48 | u32 | data ptr (`[r48]` = objeto de estado: `[+0]u8`, `[+2]u8`, `[+3]u8` flags, `[+12]u32`, `[+120]u32` en 80AA0) |
| +60 | u8 | 0 (reset) |
| +62 | u16 | flags: **bit0 = selector de retrato** (F3F0), bits 1-2 borrados en F520 |
| +64 | u16 | **índice de retrato s16**; `0xFFFF` = **slot VACÍO** (F478 retorna) |
| +68 | f32 | default (const -2548) |

**Tabla de retratos `0x82372818`** (`lis 0x8237 / addi 0x2818`, consumidor
F3F0): indexada `((s16@+64) * 2 + bit0@+62) << 2` → u32 (los 39×2 retratos).

**Tabla por personaje** `~0x8239F1xx` (base `lis 0x8246 / addi -3720`, escrito
por A8B8, leído por A618):
- `+4 + idx*2`: u16 (char id del slot `+4`)
- `+8 + idx*2`: u16 (id o 38)
- `+12 + idx`: u8 (`[slot+5]`)
- `+18 + idx`: u8 (`[slot+12]`)
- `+20 + idx`: u8 (`[slot+13]`)

**Global de personaje `0x823BA110`** (`lis 0x823C / addi -24304`): bytes +1/+2,
+20/+21/+22, +28, +192/+193/+194 (usados por A920/A618/80AA0/ADE8).

### 9.2 Siguiente paso (Hito 2 → 3)

1. **RE de la base de personaje (184 B/slot)**: en `sub_82180AA0` se indexa
   con `mulli 184` y se leen offsets `+12/+14/+18/+68/+114` (u16) — estructura
   mapeada (9.1); falta confirmar el contenido con traza runtime
   (`dbz1_roster_trace.log`, gate `dbz1_diag_logging`).
2. Ver si el guest lee la tabla con límite fijo o con contador — decidir entre:
   - **Hook host** que parchee la tabla en memoria al arranque (añadir slot),
   - **Re-codegen** con tabla extendida (más invasivo).
3. Probar añadir UN slot nativo: retrato + modelo + CAM/LIPS/ANM por duplicado
   en `data_cmn.afs` (mid-insert virtual ya lo soporta) + entrada en la tabla.

## 8. FUENTES Y HERRAMIENTAS

| Recurso | Ruta |
|---|---|
| Catálogo modelos HD | `mod center hd/catalog_b3.cat` |
| Mapa crudo (3990) | `mod center hd/data_cmn_map.txt` |
| Nombres AFL Pal | `modding resources/Data_CMN file name list (Budokai 3 Pal)/data_cmn.afl` (parseado en `out/analysis/data_cmn_pal_afl.txt`) |
| Listado comunitario | `modding resources/DBZ_B3_Character_Bin_List.txt` |
| Probe de clasificación | `out/analysis/probe_range.py`, resultados en `out/analysis/probe_result.txt` |
| Tabla anotada completa | `out/analysis/data_cmn_annotated.txt` |
| Extractores | `awo_tools/afs_probe.py`, `awo_tools/extract_azt_afs.py`, `awo_tools/afs_list.py` |