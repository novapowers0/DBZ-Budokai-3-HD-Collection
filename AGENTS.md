# DBZ Budokai 3 HD Collection — Contexto del proyecto (operativo)

> Documento de contexto para agentes/AI. **Versión compactada 2026-09-02**
> (236 KB → ~60 KB). El relato detallado de todas las sesiones vive VERBATIM
> en `docs/01_estructura/HISTORICO_AGENTS.md` — leerlo solo si se necesita el
> detalle histórico de un tema. Este documento es la referencia OPERATIVA:
> estado actual, constraints de ingeniería y comandos.

---

## 1. QUÉ ES ESTO

Port recompilado a PC de **DBZ Budokai 3 HD Collection (Xbox 360)** usando el
**ReXGlue SDK** (derivado de Xenia). Incluye launcher custom (`src/launcher/`),
lógica de región/mods, y runtime.

- **dbz3** (este proyecto): Budokai 3 HD Collection
- **dbz1** (proyecto hermano): `C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai HD Collection`

## 2. UBICACIONES CLAVE

| Ruta | Contenido |
|------|-----------|
| `src/` | Código del launcher y del juego (main.cpp, launcher/, ingame/) |
| `rexglue-sdk-0.10/` | **SDK fuente 0.10 (activo)** — builds en `out/` |
| `rexglue/` | SDK 0.10 **instalado** (usa el build del juego); respaldo 0.9 eliminado (limpieza 2026-09-09) |
| `out/build/win-amd64-release/` | **Build del juego** (dbz3.exe, DLLs, mods/) |
| `out/build/win-amd64-dual/` | Build dual (US+EU) |
| `eu/`, `us/` | Assets de región (AFS del juego) |
| `ps2_games/` | AFS de B1, B2, B2V, B3 GH, IW (referencias PS2) |
| `mod center/` | Herramientas de modding PS2 (36 programas) |
| `mod center hd/` | Herramientas HD propias (swap_b3.py, texture_b3.py, ports/) |
| `modding resources/`, `modding resources update*/`, `modding resources discord/` | Docs + recursos de la comunidad |
| `generated/` | Código recompilado del guest US (`dbz3_recomp.*.cpp`) |
| `generated_eu/` | Código recompilado del guest EU (`dbz3_eu_recomp.*.cpp`) |
| `awo_tools/` | Herramientas de RE del formato (parse/export/port) |
| `docs/` | **Documentación (leer PRIMERO `docs/README.md`)** |
| `github/` | Copia versionable para GitHub (sync manual, §11) |

## 2.1 DOCUMENTACIÓN (docs/ — LECTURA PRIORITARIA)

- `docs/README.md` — índice general
- `docs/01_estructura/ESTADO.md` — qué funciona / qué falla
- `docs/02_mods/COMO_HACER_MODS.md`, `MODEL_SWAP.md`, `TEXTURAS_MOD.md`
- `docs/03_formatos/AMO_AWO.md` + `BIN_LAYOUT.md` + `AWO_FORMAT.md` (formato bin)
- `docs/03_formatos/ACM_FORMAT.md` (moveset HD) + `STAGES_FORMAT.md` (stages/SPX/PS2)
- `docs/04_herramientas/TOOLS.md` — inventario de herramientas
- `docs/05_build/COMO_COMPILAR.md` — compilar juego/SDK
- `docs/06_limpieza/PLAN_LIMPIEZA.md` + `INVENTARIO_MODDING.md`
- `docs/07_ports/` — RE del port de modelos (ESTRUCTURA_DIBUJO_HD, sesiones, HOJA_DE_RUTA)
- `docs/HOJA_DE_RUTA_2026_09.md` — **hoja de ruta actual** (doc ligera / limpieza / RE contenido)
- `docs/PLAN_1.1.1.md`, `docs/PLAN_LINUX.md` — planes de depurado y port Linux
- `docs/MIGRACION_REXGLUE_010.md` — migración SDK 0.9→0.10 (leer ANTES de tocar el SDK)
- `docs/01_estructura/HISTORICO_AGENTS.md` — historial verbatim de sesiones (solo bajo demanda)

## 3. ESTADO ACTUAL (RESUMEN EJECUTIVO)

- **v1.1.4 EX publicada (Latest, 2026-09-10)**: hotfix de la v1.1.4 que cierra
  los issues de la comunidad. (a) **Crash EU al empezar CUALQUIER pelea**
  (`0xC000001D`, `ctr=0x820F24D8`): el re-codegen volvió a clasificar como
  "jump table" de 1 caso los `bctr` de `sub_820F2370` y `sub_820BB8C8`
  (despacho por tabla de punteros de función); cualquier caso != 0 caía en
  `__builtin_trap()`. Fix aplicado al codegen EU + **`tools/fix_eu_bctr.py`
  reescrito** (antes fallaba en silencio con el prefijo nuevo `dbz3eu_sub_*`).
  ⚠️ **Ejecutar `python tools/fix_eu_bctr.py --apply generated_eu generated`
  SIEMPRE tras re-codegen** y comprobar "NO PATCH"/0 sites. (b) **Detección de
  xex por entry point**: fallback en `CheckDefaultXex` cuando el MD5 es
  desconocido (dump modificado/otra tirada): entry `0x8221DDB0`=US,
  `0x8221C570`=EU (leído de la cabecera XEX2, offset 0x18, key 0x00010100).
  Evita que el dual caiga al config US con un xex EU → `No function registered`.
  (c) **Caché del xex del ISO invalidado** (`EnsureIsoXexCache`): guarda
  ruta+tamaño+fecha del disco de origen en `iso_cache/source.stamp` y re-extrae
  al cambiar de ISO. Binario `1.1.4.1`; zip `DBZ-Budokai-3-HD-Collection-v1.1.4-EX.zip`.
- **v1.1.3**; v1.1.3 "El parche de la ISO" (2026-09-09):
  selector de fuente siempre visible (carpeta extraida / ISO), detección y
  bloqueo del xex de DBZ1, i18n completa auditada (0 gaps), mensajes para
  usuarios no técnicos, pulido 0 warnings y empaquetador más estricto. Incluye
  la v1.1.2 (fixes de issues de la comunidad: crash EU `sub_820F2398`
  registrada, regiones incompletas con `ResolveRegion()`, backend Vulkan real
  con cvar `gpu_backend`, y **modo disco (ISO)** — juega directamente desde el
  `.iso`).
- **Fix crash EU Dragon Universe (v1.1.4, 2026-09-10)**: `0x8215B378`
  registrada manualmente en el codegen EU (11792 funciones, +1). Mismo patrón
  que `0x820F2398` (función plegada como dead fall-through, solo alcanzable vía
  puntero de función). ⚠️ **REGLAS OPERATIVAS del codegen EU**: (1) los fixes se
  aplican MANUALMENTE al codegen (el recompilador actual genera símbolos SIN
  prefijo `dbz3eu_` → colisión con US en el build dual; el codegen probado vino
  de un rexglue.exe anterior); (2) las entradas de `dbz3_config_eu.toml` deben
  ir SIEMPRE dentro de `[functions]`, ANTES del primer `[[switch_tables]]` (si
  no, el recompilador las ignora y se pierden en cada re-codegen); (3) el cvar
  `dbz1_diag_logging` vive en `rexruntime.dll` — si se reinstala el SDK y el
  build dual falla al enlazar `roster_trace.cpp`, recompilar el runtime
  baseline (`rexglue-sdk-0.10/out/build-win-vulkan-baseline`, targets
  `rexruntime rexgpu-xenos`) y reinstalar DLL+lib en `rexglue/`.
  Juego muy funcional: D3D12 principal, Vulkan
  experimental, XInput default, teclado por defecto (mnk_mode=true), presets de
  calidad por GPU, frame_cap real, idioma→juego (ES/EN/IT/DE/FR + JP), región US
  y EU con **núcleo dual** (un solo dbz3.exe detecta el xex por MD5).
- **Un solo ejecutable universal**: SDK compilado en baseline `-march=x86-64
  -mssse3` (Core 2 2006+); SIN bootstrap de ISA ni variantes (§9). Fallback
  `v1.1.0-clasico` (runtime avx2) publicado como release no-Latest.
- **Mando**: `input_backend = "xinput"` (evita cuelgue con RTSS/OBS); SDL
  disponible como selector.
- **Mods**: override por entrada AFS + mid-insert virtual (§8). Swaps nativos
  HD→HD validados (Goten, Vegeta 424, Babidi, Bulma).

### 3.1 SWAPS Y PORT — VÍAS VALIDADAS (2026-08-17..08-26)

- **✅ SWAP B3→B3 NATIVO = FUNCIONA**: bin #AMB completo (AWO+AZT) del personaje
  en el slot de otro. Validado (sw_goten_nativo, sw_vegeta424, swap_96_on_327).
- **El método AFS correcto es MID-INSERT** (bin crece en su slot, entradas
  posteriores +delta, delta redondeado a 0x800). **`--append` DESCARTADO**
  (rompe el orden de la tabla → el guest usa búsqueda binaria → crash 0xC0000005).
- **CONSTRAINT CRÍTICO (override simple)**: el guest lee la entrada con
  `to_read = ceil(slot/0x1000)*0x1000` FIJO. El bin comprimido del mod DEBE
  caber, salvo que se use el **mid-insert virtual** (§8).
- **MATRICES HD == PS2 (47/47, zona a zona)**: mismo esqueleto, mismo world.
  Los coords locales PS2 del hueso B se inyectan en slots HD del hueso B.
- **Inyección (template HD + posiciones PS2) = RECONOCIBLE** (mejor: cell_npm4,
  umbral binario 0.8): cuerpo PS2 + extremidades/cabeza HD. Ver §3.4.
- **Port completo (topología PS2) = BLOQUEADO** (amorfo): el orden del pool
  sec34 está atado a los mesh-ref/zonas → hay que reconstruir toda la estructura
  de dibujo. La inyección funciona porque mantiene el pool de la plantilla.
- **VB2 = BLOQUEADOR de cara/piernas**: layout propio; la inyección solo toca
  sec34. Para arreglar cara/piernas: reconstrucción completa.
- **Janemba (IW→B3) = FRACASO DOCUMENTADO**: masa deforme, eliminado/archivado
  (detalle en HISTORICO y `awo_tools/CONSOLIDADO.md` §13.5). NO reintentar sin
  conversor completo validado.
- **Caso de prueba descartado**: Pikkon IW (esqueleto PKH distinto a KLL, 58
  bones con falda) → NO es 1:1. Para port completo buscar esqueleto 1:1.

### 3.2 🔴 LAYOUT REAL DEL VÉRTICE B3 (VERIFICADO EMPÍRICAMENTE)

**sec34 (stride 44, align +2)** — b327_hd.bin + goten_298.bin:
```
+0   0xFFFFFFFF (nan marker)
+4   u | +8   v | +12  z_local | +16  x_local | +20  y_local
+24  peso (0.1-1.0) | +28  BONE (u32, 0-35)
+32  nrm.z | +36  nrm.y negado | +40  nrm.x
```
Verificado: 36 bones únicos 0-35, normales |mag|≈1, peso 0.1-1.0, FFFF en +0.
⚠️ **El bone va en +28, NO en +0x10** (las herramientas viejas escribían en
+4/+16 → masa deforme). Las herramientas que usan layout B1
(`[pos@+0, weight@+12, BONE@+16, nrm@+20, uv@+40]`) NO valen para el B3.

**vb2 (Krillin, layout "estático")**: `[pos.x_abs, y, z, 0,0,0, peso=0,
0xFFFFFFFF@+28, nx, ny, nz]` (posiciones ABSOLUTAS, bone=FFFF = sin skin).
**vb2 de Cell F2 (layout B, 276 slots)**: `[1.0, 0, 0, ?, ?, ?, nan@+20,
U@+24, V@+28, nrm@+32]`.

**Formato C (mayoría de bins: Goku 264, Vegeta 424, Babidi, Goten)** — AWG0,
stride 44 SIN align:
```
+0 x | +4 y | +8 z (local del hueso, [-1,1]) | +12 0xFFFFFFFF
+16 u | +20 v | +24 n.x | +28 n.y | +32 n.z | +36 weight | +40 BONE
```
IB = triangle STRIP (consecutivos, winding alternado, degenerados como saltos),
SIN restarts 0xFFFF. El `+0x2C` es el TAMAÑO en bytes del buffer (no offset).

**AWG de cara (nb=1, Goku/Vegeta)** — buffer SIEMPRE en h+0x1F0:
```
+0 FFFF | +4 u | +8 v | +12 x | +16 y | +20 z (local hueso cabeza)
+24 weight=1.0 | +28 pad 0.0 | +32 nx | +36 ny | +40 nz
```
`+0x2C` = tamaño buffer (n*44), `+0x30` ib_rel, `+0x34` = TAMAÑO del IB en
bytes (¡no offset!), `+0x38` end. Descriptor en h+0x180 (+0x1C n_verts,
+0x24 n_tris). IB = lista de triángulos. Buffer e IB se solapan 32 B
(`ib_rel = 0x1F0 + n*44 - 32`).

**Estructura del mesh group (AWG0 +0x640)**: mesh-ref blocks (0x50: X=índice
de descriptor, Y=hueso primario), ejes (80B: +0x34 arm_ptr, +0x38 hijo,
+0x3C hermano, +0x40 padre — **padre es OFFSET rel AWG0, no índice**), matriz
de zonas (0x28E0: diagonal de huesos + punteros a bboxes), bboxes (AABB por
zona, 0x40), descriptores (0x60). **Descriptor**: `A_start<<8 | A_count<<8 |
B_start<<8 | B_count<<8 | 0x01` (flag en +0x5C); `A = [min(B), max(B)+1)` del
IB (NUNCA asumir contigüidad: las parts comparten vértices); B = conteo de
ÍNDICES del strip (no 3×triángulos). Los índices del IB del rango B caen
SIEMPRE en el rango A.

### 3.3 MAPEO DE HUESOS B3 (Krillin, 51 huesos, índice = label)

```
0=XKLL_BODY 1=KLL_WAIST 2=KLL_STMC 3=KLL_OBI 4-7=KLL_ROBI1-4 8-11=KLL_LOBI1-4
12=KLL_CHEST 13=KLL_LCHN 14=KLL_LARMROT 15=KLL_LARM1 16=KLL_LARM2
17=KLL_LHANDROT 18=KLL_L00_LHAND 19=XKLL_NLA 20=KLL_RCHN 21=KLL_RARMROT
22=KLL_RARM1 23=KLL_RARM2 24=KLL_RHANDROT 25=KLL_L00_RHAND 26=XKLL_NRA
27=KLL_NECK 28=KLL_HEAD 29-37=XKLL_M_* (cara) 38=KLL_LLEGROT 39=KLL_LLEG1
40=KLL_LLEG2 41=KLL_LFOOT1 42=KLL_LFOOT2 43=XKLL_NLF 44=KLL_RLEGROT
45=KLL_RLEG1 46=KLL_RLEG2 47=KLL_RFOOT1 48=KLL_RFOOT2 49=XKLL_NRF 50=XKLL_NW
```
El sec34 usa bones 0-35 (sin piernas/rostro → van al vb2). **B1** (52 huesos)
comparte labels pero en ORDEN distinto → mapeo POR LABEL, no por índice.
Herramienta: `analyze_awo_b1.py` (estructura AWO B1, en dbz1).

### 3.4 🔴 MODEL PORT PS2→B3 HD — ESTADO CONSOLIDADO (leer ANTES de tocar el port)

> Referencia ÚNICA del port. Historial detallado: §3.1, `docs/07_ports/` y
> `docs/01_estructura/HISTORICO_AGENTS.md`.

#### 3.4.1 ESTADO ACTUAL (verificado en juego 2026-08-26)

| Vía | Estado | Mejor resultado | Notas |
|---|---|---|---|
| Swap nativo B3→B3 | ✅ FUNCIONA | sw_goten_nativo, sw_vegeta424 | bin #AMB completo en slot ajeno |
| Inyección (template + posiciones PS2) | ✅ FUNCIONA (reconocible) | **cell_npm4** (umbral binario 0.8) | cuerpo PS2 + extremidades/cabeza HD |
| Port completo (topología PS2) | ❌ NO (amorfo) | — | pool reordenado rompe la estructura |
| Swap de cabeza HD→HD | ◑ parcial | goku_armadura v3 | z-fighting, pausado por decisión |

#### 3.4.2 HECHOS VALIDADOS (cómo renderiza el guest)

1. **Dibuja por descriptores A/B + IB**: encoding `A<<8|B<<8` (flag 0x01 en
   +0x5C); índices del IB del rango B caen SIEMPRE en el rango A (22/22).
2. **Usa el bone del vértice (+28) para el transform** (test bone0: bones→0
   colapsa TODO a los pies; cara sup. y una mano se salvan → viven en vb2).
3. **ESTÁ ATADO al orden del pool** (test reverse REAL: pool invertido →
   deformidades). Mesh-ref/zonas referencian el pool por índice original.
4. **Layout sec34** (§3.2): stride 44. La plantilla usa SOLO bones 0-33 en el
   sec34 (los 34-47 van a vb2/otros AWG).
5. **vb2 de Cell F2** = layout B (§3.2) — aún no emitido correctamente por el port.
6. **El bin es AUTOCONTENIDO** (cada personaje con su formato A/B/C; el guest
   autodetecta). El nº de AWGs/huesos varía por personaje (Krillin 18 AWG/51
   bones; Bulma 2/43; Babidi 1/41).
7. **Conversión PS2→bone-local**: `local = inv(world[bone])·model` (verificado).

#### 3.4.3 LAS DOS VÍAS

- **Vía A — INYECCIÓN (FUNCIONA)**: mantener el ORDEN del pool de la plantilla
  y reescribir +12/+16/+20 (y normales `[nz,-ny,nx]`) con la geometría PS2
  convertida a bone-local. Parámetro crítico: **umbral binario** (0.8 bueno,
  2.0 malo; blends/soft SIEMPRE malos). Limitación: no es la topología PS2.
- **Vía B — PORT COMPLETO (BLOQUEADO)**: reordenar el pool a topología PS2.
  Geometría/conversión/A-B resueltos; bloqueo = reconstruir mesh-ref + zonas +
  bboxes + descriptores coherentes con el nuevo pool.

#### 3.4.4 CRONOLOGÍA DE INTENTOS (para no repetir)

| Fecha | Intento | Resultado | Lección |
|---|---|---|---|
| 14/08 | Janemba IW→B3 (v4-v10) | masa deforme | el parser PS2 no leía el IB real (FaceType) |
| 14/08 | Krillin PS2→HD (v1-v7) | silueta pero deforme | el HD es RE-TRABAJO (0% match), no 1:1 |
| 17/08 | Swap nativo B3→B3 | ✅ FUNCIONA | el guest acepta bins autocontenidos |
| 17/08 | Inyección v5-v7 | reconocible, deforme parcial | el bone va en **+28** |
| 18/08 | Bins autocontenidos | rig PS2 resuelto (chunks) | el bin HD es autocontenido |
| 19/08 | Formatos de vértice | formatos A/B/C distintos | el guest autodetecta cada bin |
| 26/08 | Inyección NPM+normales+umbral | **cell_npm4 = MEJOR** | umbral binario 0.8; blends malos |
| 26/08 | Port completo (conv2) | amorfo | descriptor A mal + pool reordenado |
| 26/08 | Reverse test (pool invertido) | **deforma** | **el orden del pool importa** |
| 26/08 | bone0 test (bones→0) | colapsa a pies | **el guest usa el bone del vértice** |

#### 3.4.5 BLOQUEADORES Y ERRORES CONOCIDOS

1. **mesh-ref/zonas atados al pool por índice** → para pool reordenado hay que
   reconstruirlos (RE pendiente: decodificar el enlace hueso→pool).
2. **vb2 layout B** de Cell F2: aún no emitido correctamente por el port.
3. **Descriptor A**: usar `[min(B), max(B)+1)`, NUNCA asumir contigüidad.
4. **⚠️ Contaminación de tests**: `AfsFindModOverride` sirve el PRIMER mod
   activo (orden alfabético). Un mod olvidado invalida los tests del mismo slot.
   → **UN SOLO mod activo por test**.
5. **Crecimiento del AWG0/sec34**: en exceso → crash 0x856AC389 (histórico §27).
   Para el port usar conteos ≤ plantilla o resolver el crecimiento.
6. Los soft/blends (npm6/npm7) y umbral 2.0 SIEMPRE empeoran vs npm4.

#### 3.4.6 PRÓXIMOS PASOS (orden de avance)

1. Re-validar `cell_port_Afix_test` en solitario (port con A corregido).
2. Decidir: reconstruir la estructura completa (Vía B) vs aceptar la inyección
   como port práctico (Vía A).
3. Si Vía B: decodificar el enlace mesh-ref/zonas→pool y escribir el regenerador.
4. Para estado jugable: **reactivar `cell_npm4`** (mejor inyección).

#### 3.4.7 REFERENCIAS

- `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md`, `SESION_INYECCION_2026-08-26.md`,
  `SESION_PORT_RE_2026-08-26.md`, `HOJA_DE_RUTA_PORT_PS2_B3.md`.
- Pipeline en `mod center hd/ports/` (`port_ps2_b3_extract/geometry/draw/pack/
  verify.py` + `port_ps2_b3_inject.py`).

## 4. COMANDOS ÚTILES

```powershell
# Compilar el juego (release, usa el SDK instalado en rexglue/)
cmake --build "out\build\win-amd64-release"
# Build dual (US+EU): cmake -B out/build/win-amd64-dual -DDBZ3_DUAL_REGION=ON ...
# Build EU (histórico, ya no usado): -DDBZ3_GENERATED_DIR=generated_eu

# Compilar el SDK 0.10 (baseline universal SSSE3 — el ÚNICO en uso)
cmake -G Ninja -S rexglue-sdk-0.10 -B rexglue-sdk-0.10\out\build-win-vulkan-baseline `
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" `
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" `
  -DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe" `
  -DREXGLUE_ENABLE_FIDELITYFX=ON -DREXGLUE_USE_VULKAN=ON `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64 -mssse3"
# ⚠️ bug build: copiar ffx_api_dll.rc ARREGLADO (ASCII) desde otro build ANTES de compilar
# La FFX dx12 se genera en bin/ → copiarla al out (no se instala sola)
cmake --build rexglue-sdk-0.10\out\build-win-vulkan-baseline --target rexruntime rexgpu-xenos

# Compresión LZX de bins 360 (formato del juego) — /N:2048, NO /N:32
xbcompress /N:2048 <src> <dst>   # comprimir
xbdecompress <src> <dst>         # descomprimir
# Herramientas XDK: "mod center\Xbox 360 Compression - Decompression tool..."

# Hoja de ruta ACELERADA (S0-S4) y sus automatismos
#  docs/HOJA_DE_RUTA_ACELERADA.md            <- plan de ejecución activo
#  tools/lab_f0.ps1                          <- baseline: hashes+región+mods+logs clasificados
#  awo_tools/corpus_scan.py                  <- parse-all AFS -> JSON+SQLite (Content DB)
#  mod center hd/swap_matrix.py              <- mover CUALQUIER blob entre slots/regiones
```

**⚠️ Compilar el juego**: pasar SIEMPRE
`-DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe"` y
`-DCMAKE_PREFIX_PATH=.../rexglue` (el preset win-amd64-release resuelve clang
al toolchain retcomm/MinGW que NO compila `rex/chrono/chrono.h`).

## 5. FORMATO DE ARCHIVOS — VER `AWO_FORMAT.md` (+ docs/03_formatos/)

**Resumen**: AFS (bins comprimidos LZX con magic `0F F5 12 EE`) → #AMB
big-endian → #AWO (modelo 360) vs #AMO0/#AMG (modelo PS2 LE). La HD 360 usa la
numeración de bins de la **data_cmn de la Greatest Hits PS2** (Krillin 327-329).
El #AWO ES el mismo modelo PS2 (51 huesos, 18 mesh-groups, 68 labels idénticos,
**NO hay re-rigging**) solo en big-endian con magics renombrados
(#AMO0→#AWO, #AMG→#AWG, #AMT→#AZT) y layout de mesh-groups distinto.

**Datos clave del formato HD** (offsets rel AWO):
- `+0x1C` tabla de offsets AMG | `+0x24` labels huesos | header con 51
  entradas de 0x20 (punteros a zonas de ejes en +0x34,+0x54,...).
- Cada #AWG: labels en 0x40, ejes en +0x14 (rel AWG), `+0x2C` vb2, `+0x30` ib,
  `+0x34` sec34, `+0x38` restart. **La tabla AMG apunta al magic #AWG**.
- Textura = bloque #AZT (header tex_am@+0x10, index_loc@+0x14; textura: idx@+0,
  type@+4, w@+16, h@+18, data_off@+0x14 rel AZT del header DDS 128B + bitmap
  DXT3/BC2 w*h bytes, mipmaps=0).
- **La tabla AFS del runtime se lee en offset 8** (magic "AFS"(3B)+pad(1B)+
  count(4B)=8B, luego (addr u32, size u32)×8B). **NO en 0x10** (los scripts
  con el off-by-one servían el bin N+1 → crash).

**AFS del juego**: `adx_jpn/usa.afs` (audio), `data_cmn.afs` (personajes +
contenido principal, 286 MB), `data_eng/ger/spn/fra/ita/usi.afs` (select/menús
por idioma), `lang_jpn/usa.afs`, `data_yah.afs` (pequeño, sin localizar aún).

## 6. PIPELINE DE MODS CORRECTO (override por entrada)

El runtime tiene `AfsFindModOverride` (rexglue-sdk-0.10/src/filesystem/afs.cpp)
que sirve archivos por ENTRADA del AFS sin reempaquetar:

```
mods/<mod>/us/<afs>/<entry_index>            ← archivo directo
mods/<mod>/us/<afs>/<entry_index>/<archivo>  ← carpeta con archivo dentro
```

### Los 3 fixes (causa de cuelgues históricos)
1. El hook soporta carpeta (iterar y usar el primer archivo regular) → log
   `AFS OVERRIDE HIT (folder)`.
2. **Compresión LZX `/N:2048`** (NO /N:32): con /N:32 el bin excede el slot →
   el guest trunca → crash.
3. **Padding al tamaño exacto** del slot que lee el guest (`to_read`). Si es más
   corto → crash.

### 🔴 MID-INSERT VIRTUAL (swaps en cualquier dirección) — 100% LIGERO (2026-09-09)
- `AfsGetVirtualTable`: si un override excede `to_read`, la entrada crece
  in-place (alineado 0x800) y las posteriores se desplazan por el delta
  acumulado (tabla virtual CONSISTENTE, replica un rebuild mid-insert).
- 🔴 **SIN archivos gigantes (promesa de bajo peso)**: NO se materializa ningún
  AFS reconstruido en disco. Toda la consistencia se resuelve en memoria vía
  `AfsVirtualRange` (ReadSync): cada byte del rango pedido se traduce al archivo
  físico o al override, y los huecos/pads/EOF se sirven como CEROS. Nunca se
  hace un read físico con offset sin traducir.
- **Histórico (crash 2026-09-09)**: la primera versión virtual servía solo la
  entrada de inicio de cada read y caía a un read físico con el offset virtual
  en el resto → basura → el parser #AMB despachaba un magic inexistente
  (`#ACP`, sin handler en la tabla 0x82310110) → crash `UNREGISTERED indirect
  call` target=0 en `sub_820800A8`. También se probó un REBUILD FÍSICO
  (`AfsRebuildPath`, AFS de 286 MB en %TEMP%) → funcionaba pero VIOLABA la
  promesa de bajo peso → descartado. El fix definitivo es el rango virtual
  ligero (`AfsVirtualRange` + loop en ReadSync).
- **Criterio de crecimiento**: solo crece si override > `to_read` (lo que el
  guest ya aloca), NO si excede el slot físico.
- `AfsFindModFileOverride`: reemplazo de ARCHIVO COMPLETO en
  `mods/<mod>/<filename>` o `mods/<mod>/us|eu/<filename>` (og_music, sfd, etc.),
  servido vía `HostPathEntry::Open`.

### Verificación en logs
```
AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```
> `got=106496` = bin completo servido. Si `got < to_read` → falta padding.

### Entrada correcta
- Tabla AFS en **offset 8**. Krillin visible = **entrada 327** (105296 B →
  padded 106496). Histórico: editábamos la 326 (tabla@0x10) = otro bin.

### Reglas de activación de mods
- **UN SOLO sistema**: un mod está activo si NO tiene el marker `.disabled`
  (`IsModEnabled` usa SOLO el marker). El cvar `dbz3_enabled_mods` es CÓDIGO
  MUERTO (ver Fase 2). Los AFS completos de mods viejos se migran
  automáticamente al regenerar (override por entrada).

## 7. RUNTIME / SDK — DLLs CANÓNICAS Y TRAMPAS DE BUILD

- **DLLs canónicas del SDK 0.10** (NO reemplazar por las regeneradas del build):
  - Baseline (único en uso): `out/win-amd64-baseline/` → rexruntime 10856960,
    rexgpu-xenos 6164992, amd_fidelityfx_dx12 5413888.
  - avx2 (para el fallback clasico): `out/win-amd64/` → rexruntime 10951168,
    rexgpu-xenos 6207488 (o 6210048 regenerado 08/27), ffx 5420544,
    TracyClient 246784.
  - legacy: `out/win-amd64-legacy/` (variante eliminada; se puede limpiar).
- **🔴 El build del juego SOBRESCRIBE `rexruntime.dll`** con la versión stale
  de `rexglue/bin` (§13.6): tras `cmake --build`, VOLVER A COPIAR la DLL
  correcta del SDK al build. Verificar siempre:
  `Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"` debe dar PRESENTE.
- **🔴 Al recompilar el SDK, el FFX de `rexglue-sdk-0.10/bin/` se regenera
  distinto** — NO copiarlo. Usar las de los `out/` canónicos.
- **Parches del SDK** en `github/patches/` (afs.cpp/h, host_path_file.cpp,
  host_path_entry.cpp, input_system.cpp, d3d12_presenter.cpp, presenter.cpp,
  sdl_input_driver.{h,cpp}, xam_info.cpp, graphics_system.cpp,
  function_dispatcher.cpp, rex_app.cpp). **Si se toca el SDK: actualizar
  patches/ + recompilar + copiar DLLs.**
- CVars importantes del runtime: `deadzone`, `rumble` (input_system), `frame_cap`
  (d3d12_presenter), `vsync` (blindado en graphics_system — el guest corre
  SIEMPRE a 60 Hz), `user_language` (XGetLanguage → idioma del juego).
- **Trace de reads AFS** (2026-09-07, F3.1.4): `HostPathFile::ReadSync` y
  `HostPathEntry::OpenMapped` loguean reads/mappings AFS→entrada
  (`dbz1_afs_reads.log`: afs entry off n eoff esize), gateados por
  `dbz1_diag_logging` (F10/dev). Útil para mapear roster/stages: activar diag,
  abrir el select, pasar por cada personaje, entregar el log. Un mapping largo
  puede no generar eventos por cada página interna.

## 8. LAUNCHER — FUNCIONALIDAD (resumen de §4/§12-§14 del histórico)

- Tabs: Video / Upscaling / Audio / Input / Mods / Model Swap / Texturas / Dev.
  Footer con **PLAY verde siempre visible** (los tabs reservan su alto) +
  resumen "Inicio: región-backend-escala-efecto-idioma" + selector de región.
- **Idioma** (`dbz3_language`): ES/EN/IT/DE/FR + JP (launcher vía i18n.cpp
  `kTable[]`; el juego vía `XGetLanguage`). El fichero i18n es GENERADO por
  script (si se añaden strings, regenerar con `extract_i18n.py`/`gen_i18n.py`).
- **Model Swap**: catálogo `mod center hd/catalog_b3.cat` (183 personajes) →
  origen HD → slot destino → `swap_b3.py` extrae el bin #AMB, comprime LZX
  /N:2048, instala como override por entrada. Restricción: bin comprimido ≤
  to_read o usar mid-insert virtual. `texture_b3.py` extract (PNG) / build
  (re-codifica DXT3/BC2, mantiene tamaño) + `--slot` destino + `--dir`.
- **Centro de mods**: instalar mod desde `.zip` (PowerShell Expand-Archive vía
  `-EncodedCommand` base64, inmuno a espacios; normaliza wrapper de una carpeta),
  perfiles (`mods/profiles.txt`, cvar `dbz3_mod_profile`), "Abrir carpeta".
- **Dev**: FPS counter, diag logging gateado por `DevMode() && DiagLogging()`
  (los .bmp solo con ambos ON), minidump en crash.
- **Banner de validación**: default.xex + us/eu verificados (verde/rojo),
  botón "Seleccionar carpeta de datos..." (remonta en caliente vía
  `dbz3::RelocateGameData`), PLAY bloqueado sin assets. Ventana de crash con
  código + ruta del log.
- **Selector de fuente SIEMPRE visible**: dos botones destacados
  ("Carpeta extraida" / "ISO (.iso)") que eligen el origen de los datos en
  CUALQUIER momento, no solo cuando faltan assets (el activo se resalta; elegir
  el otro conmuta el game drive al instante). El launcher distingue el juego:
  `CheckDefaultXex` conoce US/EU de DBZ3 (`A53E...`/`C37E...`, 4890624 B) Y el
  ejecutable de DBZ1 (`5A6AB28A...`, 4464640 B, igual para US/EU) → status
  `kDbz1` bloquea PLAY con mensaje "usa el launcher dbz1.exe" (evita que el core
  DBZ3 crashee con un xex de otro juego). El launcher dbz1 (proyecto hermano)
  NO distingue nada aún (sin ISO ni validación de xex).
- **Modo disco (ISO, v1.1.2)**: cvar `dbz3_iso_path` + selector "ISO (.iso)" siempre visible.
  Juega directamente desde el `.iso` (GDFX) sin extraer nada: `OnConfigurePaths`
  extrae SOLO `default.xex` (pocos MB) a `user_data/dbz3/iso_cache/`, y en Play
  `RemountGameDrive` monta un `DiscImageDevice` (ya en el SDK) como `game:`.
  La región se remapea DENTRO del device (`RegionDiscDevice`: `us\`→`eu\`),
  evitando el shadowing del VFS (los devices se resuelven por primer-match de
  prefijo y el orden de registro importa). Mods requieren la carpeta extraída.
- **XexStatus**: `CheckDefaultXex` (MD5 portable RFC 1321) — US
  `A53E324B5D2A65EBCBF648E4F85A7271`, EU `C37EB979B762DA0AB5B8C9BA8037CE4E`.
  Con núcleo dual acepta ambos; bloquea solo variante conocida equivocada.
- **Video**: presets (`dbz3_quality_preset` auto/low/medium/high/ultra/manual;
  `auto` detecta GPU por DXGI — la dGPU de más VRAM — y aplica perfil), escala
  interna (draw_resolution_scale_x/y), MSAA, aniso, FSR/CAS, frame_cap REAL
  (0/15-1000; 30 para integradas), VRR (`dbz3_vrr`), "Game speed: fixed 60".
- **Input**: `dbz3_input_backend` (xinput/sdl), `dbz3_mnk_mode` (teclado,
  default TRUE), `dbz3_mnk_mouse`, deadzone/rumble, 24 keybinds
  (`dbz3_keybind_*`, formato `Tecla`/comas/Shift+/Ctrl+/Alt+).
- **Auto-guardado**: los cambios se persisten al marcarlos + `SaveUserSettings`
  en OnClose (no depende de "Save settings").

## 9. EJECUTABLE UNIVERSAL + RELEASES + GITHUB

### 9.1 Un solo dbz3.exe (baseline SSSE3) — arquitectura actual
- SDK entero compilado con `-march=x86-64 -mssse3` → funciona en CUALQUIER CPU
  x64 (Core 2 2006+). **SIN bootstrap de ISA ni variantes** (bootstrap.cpp
  eliminado). El AVX/ymm restante en las DLLs está en 2 funciones con dispatch
  protegido por `__isa_available` (seguro).
- Core = **dual-region** (US+EU): `ResolveImageInfo` elige PPCImageConfig por
  MD5 del default.xex. Codegen: `generated/` (US) + `generated_eu/` (EU).
  Re-aplicar `tools/fix_eu_bctr.py` SIEMPRE tras re-codegen (convierte los
  bctr single-case mal clasificados en `REX_CALL_INDIRECT_FUNC`).
- Config codegen EU: `dbz3_config_eu.toml` (funciones no registradas →
  declarar de a una; usar `DBZ3_COLLECT_UNREGISTERED` para recolectarlas;
  `DBZ3_DUMP_IMAGE` para volcar la imagen descifrada).

### 9.2 Releases y estado GitHub
- **v1.1.3 = Latest** (core dual 1.1.3.0, baseline, "El parche de la ISO").
  **v1.1.2**, **v1.1.1**, **v1.1.0-clasico** = fallback no-Latest (runtime avx2)
  para CPU modernas. Tags v1.0.0..v1.0.9 + v1.0.5-EX conservados (código
  archivado; los zips binarios viejos NO existen).
- Empaquetado: `tools/make_release.ps1` (lee versión de `src/version.rc`,
  default `$Version`; **SIN UPX** — falso positivo AV). Verificación:
  `tools/verify_release.ps1` (hashes DLL vs SDK, VERSIONINFO, cvar vsync en
  rexgpu, mods/ vacía, zip sin assets).
- `make_release.ps1` monta una carpeta única: dbz3.exe + DLLs + `mod center hd/`
  (toolkit + tools/ XDK) + `mods/` + docs.

### 9.3 🔴 CARPETA `github/` — REPO DE SUBIDA (sync manual)
`github/` es la copia versionable (NO es repo git local; se sube manualmente).
El SDK NO se sube (`.gitignore` lo excluye); los cambios del runtime van como
parches en `github/patches/`. **Sincronizar con `tools/sync_github.ps1`**
(copia src/docs/awo_tools/mod center hd/tools + archivos raíz respetando
`.gitignore`; `-DryRun` para ver; patches/ es manual).

Reglas clave:
- **No subir**: `*.xex *.afs *.bin *.awo *.amb *.amo *.amg *.azt *.dds *.iso
  *.png *.bmp *.log`.
- `generated/` y `generated_eu/`: solo README.md (código derivado, no subir).
- `mods/`: vacía con README.md. `tools/xbcompress.exe`/`xbdecompress.exe` SÍ
  (excepción `!tools/*.exe`).
- Commit + push manuales. Credenciales git: `git config --global
  credential.helper "!gh auth git-credential"` si el push https cuelga.

## 10. PORT DE MODELOS — PIPELINE Y ESTADO

- **Swap nativo HD→HD**: extraer bin #AMB (AWO+AZT), LZX /N:2048, override.
  Restricción to_read o mid-insert virtual. Scripts: `mod center hd/swap_b3.py`.
- **Inyección PS2→HD** (Vía A, FUNCIONA): `mod center hd/ports/port_ps2_b3_
  inject.py <plantilla> <geometry.json> <umbral> <salida>` — world-matching +
  conversión bone-local + normales `[nz,-ny,nx]`. Mejor: umbral binario 0.8.
- **Port completo** (Vía B, bloqueado): pipeline `port_ps2_b3_extract →
  geometry → draw → pack → verify` (en `mod center hd/ports/`); el bloqueo es
  la estructura de dibujo atada al pool (§3.4).
- **Exportadores OBJ de verificación** (feedback rápido sin abrir el juego):
  `awo_tools/awg_to_obj_b3.py` (bins completos), `awg0_export.py` (AWG0 con
  autodetección A/C), `awg_cara_export.py` (AWG de cara). Chequear bounds/NaN.
- `awo_tools/analyze_bin_hd.py` está **DESACTUALIZADO** (layout PS3, n_sec
  absurdos) — no usarlo.

## 11. HISTORIAL → docs/01_estructura/HISTORICO_AGENTS.md

Todo el relato histórico (items 8-65 del antiguo §8, Janemba §11.1, inyección
§65.1.x, sesiones de swap de cabeza, releases 1.0.x, limpieza de disco §14.24)
vive verbatim en `docs/01_estructura/HISTORICO_AGENTS.md`. Referencias a
documentos de sesión: `awo_tools/CONSOLIDADO.md`, `awo_tools/RE_PROGRESO.md`,
`docs/07_ports/`, `docs/PLAN_1.1.1.md`, `docs/PLAN_LINUX.md`.

## 12. HOJA DE RUTA ACTUAL

Ver **`docs/HOJA_DE_RUTA_2026_09.md`** — 3 fases:
1. **Documentación ligera** (esta compactación; AGENTS ≤ 60 KB).
2. **Limpieza de código muerto** (`dbz3_enabled_mods`, `PrepareRegionData`
   stub, `analyze_bin_hd.py`, artifacts legacy) + depuración pendiente.
3. **RE de contenido por duplicados** (habilidades, slots de personaje,
   stages): auditoría de bins de `data_cmn.afs`, localizar stages/movesets,
   mapear SLXS/roster + select, y duplicar+modificar entradas.

> **⚠️ Guía vigente para slots nativos y port**: `docs/DICTAMEN_GPT6_ASTRA.md`
> (plan 0-7 + 3 correcciones: `0xFFFF`=celda vacía no personaje libre; bone
> `+28` solo sec34, formato C usa `+40`; el mid-insert no añade índices AFS).
> Vías slot nativo: (1) celda reservada → (2) parche datos memoria guest →
> (3) híbrido tablas+hooks; **sin re-codegen primero**. Port: Vía A (inyección)
> = entrega, Vía B = investigación acotada.

## 13. NOTAS DE OPERACIÓN

- Los datos de referencia que vivían en `%TEMP%\opencode\` (b327_*.bin,
  cell_*.bin, etc.) **ya NO existen** (limpieza 2026-09-02): regenerar desde
  `us/` + `ps2_games/` con las herramientas de `awo_tools/`.
- **Limpieza 2026-09-09 (~46 GB → ~28.4 GB)**: borrados `out/analysis/corpus/.work/`
  (caché de bins extraídos, regenerable con `corpus_scan.py`), `rexglue-sdk/` (0.9)
  y `rexglue_0.9/`, `out/build/_archivo_builds/` + `_archivo_dlls/`, y duplicados
  exactos de docs en `modding resources discord/tutorials/`. Detalle en
  `docs/06_limpieza/INVENTARIO_FISICO_2026-09.md`.
- `out/build/win-amd64-tracy` (perfilado) se borró: regenerar con el preset
  Tracy del CMake si se necesita.
- El usuario habla español. Sesiones largas de juego.
