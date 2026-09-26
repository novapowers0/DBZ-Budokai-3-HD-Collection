# DBZ Budokai 3 HD Collection — Contexto del proyecto (operativo)

> Documento de contexto para agentes/AI. **Compactado 2026-09-26** (117 KB → ~60 KB)
> al publicar la **v1.2.9**. El relato detallado verbatim vive en
> `docs/01_estructura/HISTORICO_AGENTS.md` (hasta 2026-09-02) y
> `docs/01_estructura/HISTORICO_RELEASES.md` (releases 1.1.3→1.2.9, investigación
> del port PS2→B3 y detalle del launcher). Este documento es la referencia
> OPERATIVA: estado actual, constraints de ingeniería y comandos.

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
| `out/build/win-amd64-dual/` | Build dual (US+EU) — **de aquí sale el exe de release** |
| `eu/`, `us/` | Assets de región (AFS del juego) |
| `ps2_games/` | AFS de B3 GH e IW (referencias PS2; B1/B2/B2V/Shin Budokai eliminados 2026-09-14) |
| `mod center/` | Herramientas de modding PS2 (36 programas) |
| `mod center hd/` | Herramientas HD propias (swap_b3.py, texture_b3.py, texture_pack.py, ports/) |
| `modding resources/`, `modding resources discord/` | Docs + recursos de la comunidad (pendientes de decidir su limpieza) |
| `generated/` | Código recompilado del guest US (`dbz3_recomp.*.cpp`) |
| `generated_eu/` | Código recompilado del guest EU (`dbz3_eu_recomp.*.cpp`) |
| `awo_tools/` | Herramientas de RE del formato (parse/export/port) |
| `docs/` | **Documentación (leer PRIMERO `docs/README.md`)** |
| `github/` | Copia versionable para GitHub (sync manual, §9.3) |

## 2.1 DOCUMENTACIÓN (docs/ — LECTURA PRIORITARIA)

**Índice completo: `docs/README.md`.** Atajos por tema:

- **Estado / estructura**: `01_estructura/ESTADO.md` (qué funciona/falla),
  `01_estructura/ARBOL.md`, `01_estructura/HISTORICO_AGENTS.md` y
  `01_estructura/HISTORICO_RELEASES.md` (histórico, solo bajo demanda).
- **Formatos**: `03_formatos/AMO_AWO.md`, `BIN_LAYOUT.md`, `AWO_FORMAT.md`
  (formato bin), `ACM_FORMAT.md` (moveset), `STAGES_FORMAT.md` (stages/SPX/PS2),
  `MAPA_ROSTER_HD.md`.
- **Mods**: `02_mods/COMO_HACER_MODS.md`, `MODEL_SWAP.md`, `TEXTURAS_MOD.md`,
  `PACKS_DE_TEXTURAS.md`.
- **Port PS2→B3**: `07_ports/` (ESTRUCTURA_DIBUJO_HD + sesiones + HOMA_DE_RUTA);
  resumen operativo en §3.4 de este documento.
- **Herramientas / build / limpieza**: `04_herramientas/TOOLS.md`,
  `05_build/COMO_COMPILAR.md`, `06_limpieza/`.
- **Rendimiento / texturas HD**: `07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md`,
  `ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md`.
- **Sesiones recientes** (una por release): `SESION_*_2026-09-*.md` — ver la
  tabla §3.0.
- **Plan rector / dictámenes**: `HOJA_DE_RUTA_2026_09.md` (activa),
  `HOJA_DE_RUTA_ACELERADA.md`, `RE_MASTER_2026_09.md`, `DICTAMEN_GPT6_ASTRA.md`.

## 3. ESTADO ACTUAL (RESUMEN EJECUTIVO)

**Juego funcional**: D3D12 principal, 60,0 fps, **núcleo dual US+EU** en un solo
exe, mando XInput, teclado por defecto (`mnk_mode=true`). Vulkan experimental
(entra en el CI de Linux). Swap nativo HD↔HD y texturas funcionan. Port completo
PS2→HD **aparcado** (§3.4.10). El diagnóstico de la v1.2.9 se explica solo.

> El detalle release a release (diagnósticos, causas, mediciones, tamaños de DLL)
> está en `docs/01_estructura/HISTORICO_RELEASES.md` §A y en los docs de sesión.
> Aquí solo la tabla y los invariantes que hay que recordar.

### 3.0 TABLA DE RELEASES

| Release | Fecha | Contenido clave | FileVersion | Doc de sesión |
|---|---|---|---|---|
| **v1.2.9** (Latest) | 2026-09-26 | Diagnóstico autoexplicativo: avisos SIEMPRE activos (fps sostenido, disco lento, instalación mixta), `vram=`/`lim=` en `perf`, guardia de VRAM, línea `entorno`, `copy_sdk_dlls.ps1` | 1.2.9 | SESION_DIAGNOSTICO_2026-09-26 |
| v1.2.8.2 | 2026-09-24 | La mejora de texturas deja de hundir los FPS (throttle de dinámicas: solo nivel 0) + `cfg=`/`upx_dyn=`/`texload=` | 1.2.8.2 | SESION_PERF_TEXTURAS_2026-09-24 |
| v1.2.8.1 | 2026-09-23 | Volcado de HUD/UI sin comprimir + packs RGBA8 + tope 4 versiones/identidad (issue #11) | 1.2.8.1 | SESION_VOLCADO_FORMATOS_2026-09-23 |
| v1.2.8 | 2026-09-21 | Fix del volcado: registro de cvars compartido → `REXCVAR_QUERY` (issue #11) | 1.2.8.0 | SESION_FIX_VOLCADO_2026-09-21 |
| v1.2.7 | 2026-09-21 | Packs de texturas estilo PCSX2 (D3D12 + Vulkan) + herramienta y guía | 1.2.7.0 | SESION_TEXTURAS_PACK_2026-09-20 |
| v1.2.6 | 2026-09-20 | Mejora de texturas HD pulida (RGBA8, min-size anti-ringing) + autorreparación del TOML + UX anti-abuso de escala | 1.2.6.0 | SESION_TOML_Y_UX_2026-09-20 |
| v1.2.5 | 2026-09-19 | E/S (`dbz3_io_logging`, readahead) + foco (`fg=`, mute/dim) + diag OFF por defecto + poda de logs | 1.2.5.0 | SESION_IO_FOCO_2026-09-19 |
| v1.2.4 EX | 2026-09-19 | FXAA/dither, sensibilidad de ratón, palancas GPU, datos portables; update check pulido | 1.2.4.1 | SESION_LAUNCHER_AUDIT_2026-09-19 |
| v1.2.4 | 2026-09-19 | Auditoría del launcher: volumen real `audio_gain`, update check, controles muertos eliminados (sustituida por la EX) | 1.2.4 | idem |
| v1.2.3 | 2026-09-18 | Contador `dbz3_perf_logging`, log AFS silenciado, Texturas HD WIP/OFF | 1.2.3 | ANALISIS_RENDIMIENTO_LOGS_2026-09-18 |
| v1.2.2 EX | 2026-09-17 | Auto-detección del ejecutable + fixes del modo ISO (`NormalizeGuestPath`, fallback carpeta→ISO) + fix TOML | 1.2.2.1 | SESION_AUTODETECCION_XEX_2026-09-17 |
| v1.2.1 | 2026-09-14 | Hotfix launcher: join del pipeline, etiqueta de nitidez FSR, refresco de lista de mods | 1.2.1 | — |
| v1.2.0 | 2026-09-14 | Centro de mods renovado + Model Swap HD↔HD pulido + nitidez FSR/CAS | 1.2.0 | SESION_MODS_LAUNCHER_2026-09-14 |
| v1.1.4 EX | 2026-09-10 | Crash EU al pelear (`fix_eu_bctr.py`), detección de xex por entry point, caché del xex del ISO | 1.1.4.1 | — |
| v1.1.3 | 2026-09-09 | Selector de fuente siempre visible, bloqueo del xex de DBZ1, i18n auditada | 1.1.3 | — |
| v1.1.2 | 2026-09-09 | Vulkan real (`gpu_backend`), `ResolveRegion()`, **modo disco (ISO)** | 1.1.2 | — |
| v1.0.x / v1.1.0-clasico | 2026-09 | Archivados (no-Latest). Los zips binarios viejos NO existen | — | HISTORICO_AGENTS |

### 3.0b INVARIANTES (no re-descubrir)

- **Coste real = supersampling**, no las texturas HD: `draw_resolution_scale`
  hace que el guest renderice de verdad a Nx. A 3x interno sin texturas HD ≈ 51 %
  GPU; a 1x+FSR ≈ 22 %. Por eso `1x` es default y recomendado y los presets
  **nunca** suben la escala. Combinación escala>1x + mejora de texturas = origen
  más común del reporte "va a 30" (frame > 16,7 ms ⇒ vsync a media tasa).
- **Texturas HD** (`dbz3_hd_textures` = Off/x2/x3): recurso host Nx + mips, sin
  tocar ficheros ni memoria del guest. Escala **DXT y RGBA8 nativas**; coste en
  VRAM; `dbz3_upscale_min_size`=16 evita el HUD; clamp anti-ringing.
- **Un cvar fuera de rango NO rompe el toml**: se rechaza solo ese cvar
  (`warning Config: invalid value for cvar`). (Un `dbz3_texture_upscale=4` viejo sí
  rompía el fichero entero; ese cvar ya no existe.)
- **Sello de build del runtime**: `rex/dbz3_build.h` (`DBZ3_RUNTIME_BUILD`) se
  publica por `dbz3_runtime_build` / `dbz3_gpu_build` (las DLL **no** tienen
  VERSIONINFO). Subirlo junto con `src/version.rc`; `verify_release.ps1` lo exige.
- **Mando**: `input_backend = "xinput"` (evita cuelgue con RTSS/OBS); SDL como
  selector alternativo.
- **Mods**: override por entrada AFS + mid-insert virtual (§6).
- **Port PS2→HD**: Vía A (inyección) = entrega aproximada; Vía B (port completo)
  = aparcada por el bind/skin (§3.4.10). Janemba IW→B3 descartado.

### 3.1 SWAPS Y PORT — VÍAS VALIDADAS (2026-08-17..08-26)

- **✅ SWAP B3→B3 NATIVO = FUNCIONA**: bin #AMB completo (AWO+AZT) del personaje
  en el slot de otro. Validado (sw_goten_nativo, sw_vegeta424, swap_96_on_327).
- **El método AFS correcto es MID-INSERT** (bin crece en su slot, entradas
  posteriores +delta, delta redondeado a 0x800). **`--append` DESCARTADO**
  (rompe el orden de la tabla → el guest usa búsqueda binaria → crash 0xC0000005).
- **CONSTRAINT CRÍTICO (override simple)**: el guest lee la entrada con
  `to_read = ceil(slot/0x1000)*0x1000` FIJO. El bin comprimido del mod DEBE
  caber, salvo que se use el **mid-insert virtual** (§6).
- **MATRICES HD == PS2 (47/47, zona a zona)**: mismo esqueleto, mismo world.
  Los coords locales PS2 del hueso B se inyectan en slots HD del hueso B.
- **Inyección (template HD + posiciones PS2) = RECONOCIBLE** (mejor: cell_npm4,
  umbral binario 0.8): cuerpo PS2 + extremidades/cabeza HD. Ver §3.4.
- **Port completo (topología PS2) = ❌ NO RENDERIZA (2026-09-11)**: el GPU dibuja
  con **ventanas de 44 B autocontenidas** en la región `[vb0,ib)` del AWO + IB.
  La geometría del port es **exacta** y **llega al GPU**; el **draw también es
  correcto** (1 draw strip, VB+IB verbatim) y el **bloqueo restante es el
  skinning/bind**. ⚠️ La "validación previa" era el swap nativo (`cell_native`),
  NO el port. Ver §3.4 + `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md`.
  Herramienta: `awo_tools/awg_vertex_buffer.py`, `mod center hd/ports/port_b3_strip.py`.
- **VB2 = parte de la región de ventanas**: la "inyección solo toca sec34" era
  del modelo viejo; con ventanas+IB se reconstruye todo el cuerpo de una vez.
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
zona, 0x40), descriptores (0x60). **Descriptor** (§3.4.8): `A_start<<8 |
A_count<<8 | B_start<<8 | B_count<<8 | 0x01` (flag en +0x5C). **A = rango de
VÉRTICES del pool `[A_start, A_start+A_count)`** (NO `[min(B),max(B)+1]`; los
rangos A **teselan el pool sin solapes**). B = rango de ÍNDICES del IB; los
índices del IB son **globales** (cubren todo A). Los descriptores "max N m"
(localizados en `+0x18`) están a stride 0x60.

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

### 3.4 🔴 MODEL PORT PS2→B3 HD — ESTADO CONSOLIDADO

> Referencia ÚNICA del port. Detalle verbatim (tabla de intentos T2-T11,
> cronología, experimentos GPU) en `HISTORICO_RELEASES.md` §B; documentos de
> sesión en `docs/07_ports/`.

### 3.4.1 ESTADO DE LAS VÍAS

| Vía | Estado | Mejor resultado | Notas |
|---|---|---|---|
| Swap nativo B3→B3 | ✅ FUNCIONA | sw_goten_nativo, sw_vegeta424 | bin #AMB completo en slot ajeno |
| Inyección (template + posiciones PS2) | ✅ FUNCIONA (reconocible) | **cell_npm4** (umbral binario 0.8) | cuerpo PS2 + extremidades/cabeza HD |
| Port completo (topología PS2) | ❌ NO RENDERIZA | geometría + draw CORRECTOS (1 strip draw, VB+IB verbatim); falta el **bind/skinning** | Ver 3.4.5 y 3.4.10 |
| Swap de cabeza HD→HD | ◑ parcial | goku_armadura v3 | z-fighting, pausado |

### 3.4.2 HECHOS VALIDADOS (cómo renderiza el guest)

1. **Draws = descriptores 0x60 + prim POR DESCRIPTOR** (probado 2026-09-11):
   `(dma-0x1BD00000)//2 == B_start`; el IB se usa **verbatim** (33/33 draws del
   cuerpo). Cuerpo = `prim=6` (strip), manos/cara = `prim=4` (list); el prim va
   en **`+0x48`** del descriptor (`0x500` strip / `0x400` list) y en `+0x30` de
   los part-descriptores de los arms (5/4). Enum D3D: `kTriangleList=4`,
   `kTriangleStrip=5`.
   ⇒ **🔴 CAUSA RAÍZ del port**: `port_b3_windows.py` emitía el IB como **LISTA**
   pero el guest dibuja el cuerpo como **STRIP** ⇒ "explosión". Fix: IB como
   **strip** (`port_b3_strip.py`).
2. **Fetch de vértices GLOBAL** (`VF[95] 0x1BD04000 size=5148×44`); **los rangos
   A NO se usan para el fetch**; solo importan `B` + prim.
3. **El guest usa el bone del vértice (+28) para el transform** (test bone0:
   bones→0 colapsa TODO a los pies; cara sup. y una mano se salvan → viven en vb2).
4. ~~Consumo posicional del pool~~ ⇒ **REFUTADO**: el GPU buffer es copia
   verbatim del pool y el guest usa el IB del fichero; la deformación de T3/T4/T9
   era un **bug de base de índices del tool** (`awg_vertex_buffer._parse` usaba el
   máximo índice del IB; ahora usa `g(0x2C)//44`).
5. **vb2/bones**: la plantilla sec34 usa SOLO bones 0-33; el nº de AWGs/huesos
   varía por personaje (Krillin 18 AWG/51; Bulma 2/43; Babidi 1/41). El bin es
   **AUTOCONTENIDO** (formatos A/B/C; el guest autodetecta). ⚠️ **Formato C**
   (Babidi): marker ≠ FFFFFFFF, sin align +2 y bone en **+40**. El pipeline debe
   autodetectar A vs C.
6. **Conversión PS2→bone-local**: `local = inv(world[bone])·model` (verificado).

### 3.4.3 LAS DOS VÍAS (operativo)

- **Vía A — INYECCIÓN (entrega)**: mantener el ORDEN del pool de la plantilla y
  reescribir pos/normales (`[nz,-ny,nx]`) con geometría PS2 convertida a
  bone-local. Parámetro crítico: **umbral binario** (0.8 bueno; 2.0 y los
  blends/soft SIEMPRE malos). No re-topologiza.
- **Vía B — PORT COMPLETO (aparcada)**: pipeline `port_b3_windows.py` →
  `port_b3_strip.py` (IB a **strip** + anula arms `+0x3C` **y `+0x44`** +
  `desc[0]=B[0,n_ib)`). Tras eso queda **1 solo draw** con VB+IB verbatim
  correctos, **pero sigue deforme**. `grow()` está BIEN (validado con `_grow_tpl`
  nativo). ⚠️ NO usar como entrega.

### 3.4.4 CRONOLOGÍA DE INTENTOS (resumen)

Tabla completa de intentos T2-T11 (Fases B/C/GPU) en `HISTORICO_RELEASES.md` §B.
Resumen: T2/T8/T10/T11 = **IDÉNTICO** (relabeling consistente = identidad
geométrica); T3/T4/T9 = **DEFORME** (era bug de base de índices del tool);
T5 = peor; T6 = normal (**A no se usa para dibujar**); T7 = masivo (**el IB
gobierna**). Ver §3.4.9.

### 3.4.5 BLOQUEADORES / COSAS A NO REPETIR

1. **Partición del pool por rangos de parte** (Fase C): los vértices están en
   rangos `(start,count)` por parte, declarados en **A de los descriptores 0x60**
   y en los **part-descriptores de los arms** (`+0x38/+0x3C`); su unión **tesela
   `[0,n_pool)` sin solapes ni huecos** (Cell F2: 29 desc. + 7 partes =
   2937/2937). Los índices del IB (B) son **globales**. La "2ª tabla
   @AWG0+0x1F80" era la **tabla de matrices bind-pose** (los `p2` de los arms).
2. **T8**: permutar **partes enteras** + remapear el IB → **IDÉNTICO** (el orden
   GLOBAL de partes es libre). **T9**: reordenar runs mono-hueso **dentro** de un
   bloque → **DEFORME** (el orden intra-bloque importa y no hay tabla
   posición→hueso en el bin). ⇒ mover solo partes enteras es seguro.
3. **`_bone0port` (todo rígido al hueso 0) CRASHEA**: el record 0 de la paleta
   capturada es todo ceros ⇒ el slot de paleta NO se indexa por el bone crudo.
   ⚠️ NO reintentar.
4. **`--hd-skin` (vecino más cercano) EMPEORA**; `--fit`/`cluster_fit` decima y
   deforma la malla (inválido para validar render).
5. **⚠️ Contaminación de tests**: `AfsFindModOverride` sirve el PRIMER mod
   activo (orden alfabético) → **UN SOLO mod activo por test**.
6. **Crecimiento del AWG0/sec34**: en exceso → crash 0x856AC389. Para el port usar
   conteos ≤ plantilla o resolver el crecimiento.
7. **Fuente PS2**: los `ps2_games/*/data_cmn.afs` SON #AMO0/#AMG LE auténticos
   (B3 GH 558 #AMO0 / 0 #AWO).
8. ⚠️ Instrumentaciones de draw/paleta ya **REVERTIDAS**; la DLL canónica NO lleva
   instrumentación. `dump_shaders` debe quitarse del `dbz3_user.toml` al terminar.

### 3.4.6 PRÓXIMOS PASOS (si se retoma)

1. Vía A práctica: reactivar/refinar `cell_npm4`; extender a los 16 AWGs
   auxiliares (`port_ps2_b3_inject_aux.py`, ver §10).
2. Vía B: el bloqueo es el **bind/skin** (`M_bind` real que el renderer no
   expone): RE de `sub_82087F58` o capturar la paleta en el frame de BIND/T-pose.
   La paleta se decodificó (`[T.xyz][qA.x][qB.xyz][qA.y][qC.xyz][qA.z]`,
   `model=R(qA)·pos+T`; `bone`@byte16 = índice DIRECTO a la paleta; `weight`@off3
   = blend intra-hueso). El eslabón que falta es el mapeo hueso→slot (σ).
3. `vb2` (layout B) para cara/piernas.

### 3.4.7 REFERENCIAS

- `docs/07_ports/SESION_FASE_B_ARMS_2026-09-10.md`,
  `SESION_FASE_C_CONSUMER_2026-09-10.md`,
  `SESION_GPU_DRAW_2026-09-11.md`,
  `SESION_DRAW_SEMANTICS_2026-09-11.md` (definitiva + RETOMO §0),
  `SESION_VIA_B_RENDER_2026-09-12.md`, `INVESTIGACION_PS2_HD_2026-09-13.md`,
  `ESTRUCTURA_DIBUJO_HD.md`, `HOJA_DE_RUTA_PORT_PS2_B3.md`, `PLAN_PS2_B3/`.
- Instrumento canónico Vía B: `awo_tools/awg_vertex_buffer.py` (`info`/`permute`/
  `roundtrip`/`grow`/`selftest`; `bind_worlds()`/`bone_labels()`/
  `window_from_model()`; API `load().vertices/.indices/.emit()`).
- Herramientas fase: `phase_c_descriptors.py`, `phase_c_arms_targets.py`,
  `phase_c_meshgroup.py`, `phase_b_*.py`, `afs_extract_hd.py`.
- Pipeline en `mod center hd/ports/` (`port_ps2_b3_extract/geometry/draw/pack/
  verify.py` + `port_ps2_b3_inject.py` + `port_b3_windows/strip.py`).

### 3.4.8 DESCRIPTORES 0x60 Y PARTICIÓN DEL POOL (Fase C, 2026-09-10)

**Descriptor 0x60** (tag ASCII `"max N m"` en `+0x18`):
```
+0x00 label[]     +0x18 "max N m"   +0x44 type==0x2C00   +0x48 prim (0x500 strip / 0x400 list)
+0x50 A_start<<8  +0x54 A_count<<8     A = rango de VÉRTICES del pool
+0x58 B_start<<8  +0x5C B_count<<8     B = rango de ÍNDICES del IB
```
- **A tesela el pool** `[0, n_pool)` **sin solapes**; los índices del IB (B) son
  **globales** (`min==A_start`, `max==A_start+A_count-1`).
- Los descriptores de parte (arms) tienen el rango en `+0x38/+0x3C` + label en
  `+0x48`; **completan los huecos**.
- **Partición total (Cell F2)**: `[0,2937)` = 29 descriptores + 7 partes, 0
  solapes, 0 huecos (`awo_tools/phase_c_descriptors.py`).
- Matriz bind-pose 4×4 (64 B) de cada arm en la tabla `AWG0+0x1F80` (los `p2`).

### 3.4.9 SEMÁNTICA DEL DRAW (definitiva, 2026-09-11)

- **El guest usa el IB del port VERBATIM** (33/33 draws coinciden con
  `AwgVertexBuffer.load(port).indices()`).
- **Ventana de 44 B** (copia VERBATIM de `[vb0, ib)` del AWO; `ib = awg0+g(0x30)`,
  `vb0 = ib - g(0x2C)`; `g(0x2C)` = TAMAÑO del buffer en bytes):
  ```
  +0 pos.xyz(3f) | +12 w | +16 bone(u32,1B) | +20 nrm.xyz(3f) | +32 FFFFFFFF | +36 uv.xy(2f)
  ```
  El IB (`g(0x30)`, int16 BE) referencia índices de ventana. `pos` = bone-local;
  semántica `pos = inv(world[bone])·model`.
- **2ª fuente de draw = part-descriptores de los arms** (`+0x40 idx_start`,
  `+0x44 idx_count`); anular solo `+0x3C` NO basta: hay que anular **también
  `+0x44`** (`port_b3_strip.py` ya lo hace).
- **Intentos descartados** (NO repetir): `_desc_one` (`+0x48` no basta para
  cambiar el prim), `_strip2` (deformidad cambia pero sigue explotando),
  `_body33`, `_nottail`, `_bone0port` (crash), `_strip4*`, `_hdskin_strip`.
- ✅ **`grow()` es correcto** (con `awg+0x2C` y `awg+0x34` actualizados);
  `_grow_tpl` (plantilla nativa crecida) renderiza PERFECTO.
- **Bloqueo restante = bind/skin** (ver §3.4.6). Los `world` PS2==HD (48/48) y
  los labels 48/48 ⇒ esqueleto y mapeo correctos.

### 3.4.10 CIERRE — Vía B APARCADA; HD↔HD = entrega

**Decisión** (2026-09-13): Vía B (bind real) **aparcada** (no imposible). Vía A
documentada como aproximada. La entrega es el **swap nativo B3 HD↔HD** (ya en el
launcher). Detalle: `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` §20.
- **🔴 LAYOUT REAL (los 17 AWGs)**: TODOS usan el **layout de ventana**
  (`pos@0`, `w@12`, `bone@16`, `nrm@20`, `FFFFFFFF@32`, `uv@36`, stride 44);
  región `[ib−g(0x2C), ib)` con `g(0x2C)/44` slots exactos (FFFF@32 en el 100 %).
  ⚠️ **El layout "sec34" (`FFFF@0`,`bone@28`) es un error** para estos bins;
  `port_ps2_b3_inject.py` escribía con desfase de 428 B (10 slots). Fixes en
  `%TEMP%\opencode\phaseb\make_winbody.py` + `make_winaux.py`.
- **Vía B — bloqueo = `M_bind`**: `world` (ejes) da un T-pose correcto pero no es
  el bind exacto del skin; la paleta = transform aplicada a `pos` (bone-local).
  Para retomar: RE de `sub_82087F58` o capturar la paleta en BIND/T-pose.
- **SWAP HD↔HD (entrega)**: `mod center hd/swap_b3.py` + `catalog_b3.cat` (183),
  mid-insert virtual. En el launcher: pestaña "Cambio de modelo". Guardia
  origen==destino en `src/launcher/mod_pipeline.cpp`.

## 4. COMANDOS ÚTILES

```powershell
# Compilar el juego (release, usa el SDK instalado en rexglue/)
cmake --build "out\build\win-amd64-release"
# 🔴 DESPUES de cada build del juego: recopiar las DLL canonicas (el build las
# sobrescribe con las stale/avx2 de rexglue/bin -> un test mediria otro runtime)
powershell -ExecutionPolicy Bypass -File tools\copy_sdk_dlls.ps1
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
#  tools/make_test_iso.py <out.iso> <carpeta> <- XDVDFS de prueba (validar el modo disco/ISO)
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
- 🔴 **SIN archivos gigantes**: NO se materializa ningún AFS reconstruido en
  disco. Toda la consistencia se resuelve en memoria vía `AfsVirtualRange`
  (ReadSync): cada byte del rango pedido se traduce al archivo físico o al
  override, y los huecos/pads/EOF se sirven como CEROS. Nunca se hace un read
  físico con offset sin traducir.
- **Histórico (crash 2026-09-09)**: la primera versión virtual servía solo la
  entrada de inicio de cada read y caía a un read físico con el offset virtual
  en el resto → basura → el parser #AMB despachaba un magic inexistente
  (`#ACP`) → crash `UNREGISTERED indirect call` target=0 en `sub_820800A8`. El
  rebuild FÍSICO (`AfsRebuildPath`, 286 MB en %TEMP%) funcionaba pero VIOLABA la
  promesa de bajo peso → descartado.
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
  (`IsModEnabled` usa SOLO el marker). El cvar `dbz3_enabled_mods` es **CÓDIGO
  MUERTO**. Los AFS completos de mods viejos se migran automáticamente al
  regenerar (override por entrada).

## 7. RUNTIME / SDK — DLLs CANÓNICAS Y TRAMPAS DE BUILD

- **DLLs canónicas del SDK 0.10** (NO reemplazar por las regeneradas del build):
  - **Baseline (único en uso)**: `rexglue-sdk-0.10/out/win-amd64-baseline/` →
    `rexruntime.dll` **10917888 B** y `rexgpu-xenos.dll` **6355456 B** (v1.2.9);
    `amd_fidelityfx_dx12.dll` 5413888.
    - rexruntime lleva: `audio_gain`, `dbz3_perf_logging`, `dbz3_io_logging`/
      `dbz3_io_readahead`, poda de logs, `dbz3_mute_unfocused`, `frame_cap`
      (definido en `src/ui/presenter.cpp`), **aviso de disco lento** y sello
      `dbz3_runtime_build`.
    - rexgpu-xenos lleva: `fg=` y `cfg=`/`upx`/`upx_dyn=`/`texload=`/`vram=`/
      `lim=` en la línea `perf`, fix de mips del shader `texture_upscale_cs` +
      clamp anti-ringing, extensión a RGBA8 nativas, `dbz3_upscale_min_size`,
      guardia de vídeo `UpscaleBudgetAllows`, throttle de texturas dinámicas
      (v1.2.8.2), guardia de VRAM y aviso de fps sostenido (v1.2.9), volcado dev
      `dbz3_texture_dump`/`dbz3_texture_dump_max`, cargador `dbz3_texture_packs`
      y sello `dbz3_gpu_build`; **sin** instrumentación de draw.
  - avx2 (fallback clásico): `out/win-amd64/` → rexruntime 10951168,
    rexgpu-xenos 6207488 (o 6210048), ffx 5420544, TracyClient 246784.
  - legacy: `out/win-amd64-legacy/` (variante eliminada; se puede limpiar).
- ⚠️ **El SHA256 varía por build** (embebe timestamp) → comparar por **tamaño** o
  recompilar y copiar. Los tamaños de AMBAS DLL cambian al recompilar el SDK; el
  valor de referencia es el de `out/win-amd64-baseline/` (lo que usa
  `verify_release.ps1`). Tamaños de releases previas: 10910720/6346240
  (v1.2.8.2), 10910208/6227456 (v1.2.6), 6346752 (v1.2.7), 6340096 (v1.2.8),
  6342656 (v1.2.8.1), 10910208/6355456 (v1.2.9).
- ⚠️ **Sello de build** (v1.2.9): `rex/dbz3_build.h` (`DBZ3_RUNTIME_BUILD`) se
  publica por las cvars `dbz3_runtime_build` / `dbz3_gpu_build`; **subirlo junto
  con `src/version.rc`** (`verify_release.ps1` lo comprueba). El launcher lo usa
  para avisar de instalaciones mixtas.
- **🔴 El build del juego SOBRESCRIBE `rexruntime.dll`** con la versión stale de
  `rexglue/bin`: tras `cmake --build`, copiar las canónicas con
  `powershell -ExecutionPolicy Bypass -File tools\copy_sdk_dlls.ps1` (avisa si el
  sello no está; cerrar dbz3.exe o el fichero está bloqueado). Verificar:
  `Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"` debe dar PRESENTE
  y debe existir el marker `dbz3_perf_logging` (si falta es el stale 10.863.616 B
  y las líneas `perf fps` no salen).
- **🔴 Al recompilar el SDK, el FFX de `rexglue-sdk-0.10/bin/` se regenera
  distinto** — NO copiarlo. Usar las de los `out/` canónicos.
- **Parches del SDK** en `github/patches/` (afs.cpp/h, host_path_file.cpp,
  host_path_entry.cpp, input_system.cpp, d3d12_presenter.cpp, presenter.cpp,
  sdl_input_driver.{h,cpp}, xam_info.cpp, graphics_system.cpp,
  function_dispatcher.cpp, rex_app.cpp). **Si se toca el SDK: actualizar
  patches/ + recompilar + copiar DLLs.**
- **Medir rendimiento**: cvar `dbz3_perf_logging` (default false; activar en Dev)
  → línea `dbz3: perf fps=... frames=... max_frame_ms=...` cada 5 s en el **swap
  del guest** (`IssueSwap`, rexgpu-xenos). (v1.2.9) añade `vram=uso/presupuesto`
  MB y `lim=` (0 nada / 1 racha de recargas / 2 guardia de VRAM), más `cfg=` y
  `upx_dyn=`/`texload=` (v1.2.8.2). Para probar sin ventana: `tools/hidden_run.ps1`
  (aplica overrides al `dbz3_user.toml` y restaura; `dbz3_skip_launcher=true`
  bootea directo; los strings van entre comillas o el parser descarta el fichero
  entero).
- **Avisos SIEMPRE activos** (v1.2.9, no dependen de las cvars de logging; el log
  normal sigue limpio y solo sale la línea si hay algo accionable, máx 3/sesión):
  **fps sostenido bajo** (con escala/MSAA/mejora → vsync a media tasa) y **disco
  lento** (5+ lecturas físicas ≥ 50 ms). Ver `docs/SESION_DIAGNOSTICO_2026-09-26.md`.
- **Arnés para llegar a la DEMO 3D** (2026-09-19): `tools/long_run.ps1`
  (Start/Status/Stop, silenciado y restaura el toml), `tools/press_key.ps1`
  (PostMessage; mapa W/A/S/D/Backspace/Tab), `tools/grab_window.ps1` (captura PNG;
  requiere ventana **on-screen**) y `tools/click_window.ps1`. Receta: boot con
  `dbz3_skip_launcher=true` → opening (~90-100 s) → **Start (Return)** → menú →
  idle ~2-2,5 min → attract demo battle 3D. Medido: 2x y 3x + MSAA = 60,0 FPS,
  0 errores, 6 min sin crash.
- **CVars del runtime**: `deadzone`, `rumble`, `frame_cap`, `vsync` (blindado: el
  guest corre SIEMPRE a 60 Hz), `user_language` (XGetLanguage), `audio_gain`/
  `audio_mute`.
- **Trace de reads AFS**: `dbz1_afs_reads.log` (`HostPathFile::ReadSync` +
  `HostPathEntry::OpenMapped`) y el log de overrides `AFS OVERRIDE LOOKUP/HIT/MISS`
  están gateados por `dbz1_diag_logging` (dev).

## 8. LAUNCHER — FUNCIONALIDAD

> Detalle completo verbatim en `HISTORICO_RELEASES.md` §C. Resumen operativo:

- **Tabs**: Video / Upscaling / Audio / Input / Mods / Model Swap / Texturas /
  Dev. Footer con **PLAY verde siempre visible** + resumen "Inicio:
  región-backend-escala-efecto-idioma" + selector de región. **Idioma**
  (`dbz3_language`): ES/EN/IT/DE/FR + JP (launcher vía i18n.cpp `kTable[]`;
  fichero i18n GENERADO: si se añaden strings, regenerar con
  `extract_i18n.py`/`gen_i18n.py`; juego vía `XGetLanguage`).
- **Resolución del ejecutable (v1.2.2)**: `ResolveBootSource()` = `CheckDefaultXex`
  de la ruta canónica y, si no, `FindGameExecutable()` (por **tamaño+MD5**;
  escaneo del root depth ≤3 + `root`/`DBZ3`/`assets`/`assets/DBZ3`) →
  `EnsureXexCache()` copia a `user_data/dbz3/xex_cache/default.xex` y fija el data
  root. `GameDataHostDevice` (carpeta) y `RegionDiscDevice` (ISO) sirven
  `game:\default.xex` y prefijan `DBZ3\`. ⚠️ Los logs de `OnConfigurePaths` se
  pierden (logging arranca después): el diagnóstico aparece en Play
  (`RelocateGameData`). **Banner**: "Ejecutable detectado: … (no hay que renombrar
  nada)", PLAY bloqueado si `!assets_ready`.
- **XexStatus** (`ClassifyXexFile`: MD5 RFC 1321 + entry point + tamaño): US
  `A53E324B5D2A65EBCBF648E4F85A7271`, EU `C37EB979B762DA0AB5B8C9BA8037CE4E`,
  DBZ1 `5A6AB28A4911851FCA955B5925CDFEBB` (4464640 B) → bloquea PLAY, menú HD
  3317760 B → `kHdMenu` bloquea. `kUnknown` avisa en ámbar pero no bloquea.
- **Modo disco (ISO)**: cvar `dbz3_iso_path` + selector "ISO (.iso)" siempre
  visible. Juega del `.iso` (GDFX = XDVDFS crudo; magic `MICROSOFT*XBOX*MEDIA`)
  sin extraer; `ExtractGameXexFromIso` extrae solo el xex a
  `user_data/dbz3/iso_cache/` (`source.stamp`) y `RemountGameDrive` monta un
  `DiscImageDevice`. `RegionDiscDevice` remapea `us\`→`eu\` DENTRO del device;
  ⚠️ **`ResolvePath` normaliza la ruta** (`NormalizeGuestPath`, el VFS la da con
  `\us\...`) o no se aplica el remapeo ni el prefijo `DBZ3\` (0xc000000f).
  **Fallback carpeta→ISO** si la carpeta no es bootable y hay un `.iso` al lado.
  ⚠️ **Los mods NO se aplican en modo ISO** (aviso ámbar; swap deshabilitado).
- **Fix TOML (v1.2.2 + autorreparación v1.2.6)**: `SaveUserSettings` pasa el
  fichero por `EscapeTomlStrings` (escapa `\`/`"`; **idempotente**). `LoadUserSettings`
  **AUTORREPARA**: valida con toml++ (`TomlParses`, leyendo TEXTO), y si falla
  aplica `EscapeTomlStrings` y recarga; estado `ConfigLoadState`
  (kOk/kRepaired/kInvalid) → aviso verde/rojo arriba. Si sigue inválido no carga y
  guarda `dbz3_user.toml.bak`. ⚠️ Corre DOS veces por arranque → preservar el
  estado. Requiere `<toml++/toml.hpp>`.
- **Video**: presets (`dbz3_quality_preset` auto/performance/balanced/quality/
  manual; `auto` detecta GPU por DXGI; **ningún preset sube la escala**, tope 1x;
  alias viejos low/medium/high/ultra), escala interna (draw_resolution_scale_x/y),
  MSAA, aniso, FSR/CAS, frame_cap REAL (0/15-1000), VRR (`dbz3_vrr`), "Game
  speed: fixed 60". **Mejora de texturas (experimental)**: `dbz3_hd_textures`
  (Off/**Nitidas x2**/**Muy nítidas x3**) + ajuste de VRAM
  (`dbz3_hd_texture_max_texels`, Bajo/Medio/Alto) en Dev. En **Escalado**:
  **FXAA** (`dbz3_fxaa` → `swap_post_effect`; corre ANTES del upscaler) y
  **dither** (`dbz3_present_dither`).
- **Audio (real desde 2026-09-19)**: `dbz3_master_volume` → `audio_gain` (ganancia
  del callback SDL) + checkbox Silenciar → `audio_mute` (en caliente). Los sliders
  de música/SFX/voces y Gamma se **eliminaron** (muertos).
- **Model Swap (HD↔HD)**: catálogo `mod center hd/catalog_b3.cat` (183) → combos
  con buscador, vista previa, aviso origen==destino; `swap_b3.py` extrae el bin
  #AMB, comprime LZX /N:2048 y lo instala (mid-insert virtual si excede `to_read`).
  `texture_b3.py` extract/build (DXT3/BC2, mantiene tamaño) + `--slot`/`--dir`.
- **Centro de mods**: lista cacheada, buscador, activar/desactivar todos,
  refrescar, badges de tipo, filas alternas. Instalar desde `.zip`,
  perfiles (`mods/profiles.txt`, `dbz3_mod_profile`).
- **Update check** (`src/launcher/update_check.{h,cpp}`): hilo de fondo consulta
  `api.github.com/.../releases/latest` (WinHTTP) y compara con el VERSIONINFO
  (`VersionNewer`; `1.2.4-EX` > `1.2.4` pero < `1.2.5`; un build local > 0 cuenta
  como repack). Muestra versión instalada + estado + botón "Buscar
  actualizaciones"/"Reintentar"; nunca bloquea PLAY. Toggle `dbz3_update_check`.
  Requiere linkear `winhttp` + `version`.
- **Instalación mixta / sello (v1.2.9)**: las DLL publican su build por cvar; el
  launcher compara major.minor.patch con el exe. Si no coincide (o sin sello) →
  banner naranja + `[warning]` + línea en Dev; además línea `dbz3: entorno
  os=... ram=... dbz3.exe=... rexgpu-xenos=... rexruntime=... amd_fidelityfx_dx12.dll=...`
  (sistema real vía `RtlGetVersion`).
- **Input**: `dbz3_input_backend` (xinput/sdl), `dbz3_mnk_mode` (default TRUE),
  `dbz3_mnk_mouse`, `dbz3_mnk_sensitivity` (0.1-5.0 → `mnk_sensitivity`),
  deadzone/rumble, 24 keybinds (`dbz3_keybind_*`).
- **Dev**: FPS counter, diag logging gateado por `DevMode() && DiagLogging()`,
  minidump en crash, palancas GPU `dbz3_async_shaders` → `async_shader_compilation`
  y `dbz3_occlusion_queries` → `occlusion_query_enable`, versiones de ficheros.
- **Datos de usuario**: `UserDataRoot()`/`UserSettingsPath()` usan
  `<exe_dir>/user_data/dbz3` y `<exe_dir>/dbz3_user.toml` si son escribibles;
  si no caen a `Documents/dbz3` (sonda real cacheada; ruta visible en Dev).
- **QoL al perder el foco (v1.2.5)**: `dbz3_mute_unfocused` (ON) y
  `dbz3_dim_unfocused` (ON, overlay "Juego en segundo plano"). **NO hay pausa
  real** (no hay mecanismo seguro).
- **Diagnóstico de E/S (v1.2.5)**: `dbz3_io_logging` (OFF por defecto) → resumen
  cada 5 s (`reads/phys/cache/mb/pre_avg_us/read_avg_us/p95/p99/max/slow/opens`)
  + línea por lectura > `dbz3_io_slow_ms` (25). `dbz3_io_readahead` (ON,
  `_kb`=2048; solo sin mods). La línea `perf` lleva `fg=` (foco; DWM limita a la
  MITAD una ventana visible sin foco). `logging.cpp` **poda** los `dbz3_NNN.log`
  (`log_max_files`=20).
- **Auto-guardado**: los cambios se persisten al marcarlos + OnClose.

## 9. EJECUTABLE UNIVERSAL + RELEASES + GITHUB

### 9.1 Un solo dbz3.exe (baseline SSSE3)
- SDK compilado con `-march=x86-64 -mssse3` → funciona en CUALQUIER CPU x64
  (Core 2 2006+). SIN bootstrap de ISA ni variantes. El AVX restante está en 2
  funciones con dispatch por `__isa_available` (seguro).
- Core **dual-region** (US+EU): `ResolveImageInfo` elige PPCImageConfig por MD5
  del default.xex. Codegen: `generated/` (US) + `generated_eu/` (EU). Re-aplicar
  `tools/fix_eu_bctr.py` SIEMPRE tras re-codegen. Config EU:
  `dbz3_config_eu.toml` (entradas SIEMPRE dentro de `[functions]`, antes del
  primer `[[switch_tables]]`; usar `DBZ3_COLLECT_UNREGISTERED` / `DBZ3_DUMP_IMAGE`).

### 9.2 Releases y estado GitHub
- **v1.2.9 = Latest** (2026-09-26): diagnóstico autoexplicativo (ver §3.0).
- **v1.2.8.2 / 1.2.8.1 / 1.2.8 / 1.2.7 / 1.2.6 / 1.2.5 / 1.2.4 EX / 1.2.4 /
  1.2.3 / 1.2.2 EX / 1.2.1 / 1.2.0 / 1.1.4 EX / 1.1.3 / 1.1.2 / 1.1.1** =
  no-Latest (contenido en §3.0); **v1.1.0-clasico** = fallback (runtime avx2);
  tags v1.0.0..v1.0.9 + v1.0.5-EX conservados (zips binarios viejos NO existen).
  ⚠️ La **v1.2.2 plana se retiró** (le faltaban los fixes del ISO).
- **PortForge**: `defaultVersion` = 1.2.9; visibles 1.2.9 / 1.2.8.2 / 1.2.8.1;
  el resto al archivo (`portforge/archive/`).
- ⚠️ **El exe de release se compila desde `out\build\win-amd64-dual`** (core dual);
  `make_release.ps1` toma `dbz3.exe` de ahí + DLLs del
  `rexglue-sdk-0.10\out\win-amd64-baseline\`.
- Empaquetado: `tools/make_release.ps1` (lee versión de `src/version.rc`, SIN
  UPX). Verificación: `tools/verify_release.ps1` (hashes DLL vs SDK, VERSIONINFO,
  sello vs `version.rc`, cvar vsync en rexgpu, mods/ vacía, zip sin assets).
  `make_release.ps1` monta: dbz3.exe + DLLs + `mod center hd/` + `mods/` + docs.
- **Issues (triaje 2026-09-25)**: cerrados #7 (crash al título = menú HD, v1.2.2
  EX), #11 (volcado: v1.2.8 + HUD/RGBA8 v1.2.8.1), #3 (CrossOver Mac; el splash
  sin canal rojo es de D3DMetal). Abiertos: #8 (bajones de FPS: comentado el fix
  de la v1.2.8.2, esperando el log `perf`), #9 (importar saves: receta por carpeta
  + helper pendiente de decidir), #1 (pico de volumen al volar sin repro).
  #10/#6/#5/#4/#2 cerrados antes.

### 9.3 🔴 CARPETA `github/` — REPO DE SUBIDA (sync manual)
`github/` es la copia versionable (NO es repo git local; se sube manualmente). El
SDK NO se sube (`.gitignore`); los cambios del runtime van como parches en
`github/patches/`. **Sincronizar con `tools/sync_github.ps1`** (`-DryRun` para
ver; patches/ es manual).
- **No subir**: `*.xex *.afs *.bin *.awo *.amb *.amo *.amg *.azt *.dds *.iso
  *.png *.bmp *.log`.
- `generated/` y `generated_eu/`: solo README.md. `mods/`: vacía con README.md.
  `tools/xbcompress.exe`/`xbdecompress.exe` SÍ (excepción `!tools/*.exe`).
- Commit + push manuales. Si el push https cuelga: `git config --global
  credential.helper "!gh auth git-credential"`.

## 10. PORT DE MODELOS — PIPELINE

- **Swap nativo HD→HD** (✅ **entrega principal**, validada 2026-09-10 con Cell
  Forma 2 (147) → slot Krillin (327), 100 % funcional incl. boca):
  `python "mod center hd\swap_b3.py" --origen 147 --dest 327 --mod cell_native`
  (`--list` lista el catálogo). `bin == índice de entrada AFS`; catálogo
  `mod center hd/catalog_b3.cat`. LZX /N:2048 + override por entrada. **Regla**:
  si el personaje existe en HD → swap nativo; el port PS2→HD solo aporta para
  modelos que NO están en HD.
- **Inyección PS2→HD (Vía A)**: `port_ps2_b3_inject.py <plantilla> <geometry.json>
  <umbral> <salida> [--npm] [--bone-aware]` (world-matching + conversión
  bone-local + normales `[nz,-ny,nx]`; mejor umbral binario 0.8).
  `port_ps2_b3_extract.py` lee el PADRE del eje PS2 (`+0x40`, rel AMG;
  `axes_rel=0x20`) y transforma las partes "L00" (manos/cara) de local a
  model-space. El `axes_base` del inject usa AWG+0x14 (no hardcodeado).
- **🔴 LÍMITE ESTRUCTURAL (Cell F2)**: el bin HD tiene **17 AWGs**: AWG0 (48
  huesos, 2661 verts, cuerpo) + **16 AWGs de 1 hueso = huesos 48-63** (**10
  manos + 6 cara**). El PS2 solo tiene **48 huesos (0-47)** → los 16 AWGs extra NO
  tienen equivalente PS2 y quedan HD. La inyección solo toca el `sec34` del AWG0.
- **✅ FASE 1 EJECUTADA**: `port_ps2_b3_inject_aux.py` extiende la Vía A a los 16
  AWGs auxiliares (10 manos `world[23]/[30]`; 6 cara `world[32]`) con las 6
  familias de layout de `PLAN_PS2_B3/04_FORMATO_RE.md` (3079/3085 verts). Mods:
  `cell_best2` (16 AWGs), `cell_face_only` (6 de cara). Flags
  `--only all|face|hands`, `--face-thr`, `--hand-thr`.
- **Port completo (Vía B)**: pipeline `port_b3_windows.py` → `port_b3_strip.py`;
  geometría + draw correctos, bloqueo = bind/skin (§3.4). Mods de test:
  `_strip3` (mejor), `_grow327`, `_hdskin_strip` (peor) — **UNO activo** (slot
  327). `cell_native` (327) = swap nativo (renderiza, NO es el port). Pipeline
  viejo `port_ps2_b3_geometry/draw/pack` (descriptores A/B) SUPERADO.
  **RETOMO + comandos**: `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` §0.
- **Exportadores OBJ** (feedback sin abrir el juego): `awo_tools/awg_to_obj_b3.py`
  (bins completos), `awg0_export.py` (AWG0 con autodetección A/C),
  `awg_cara_export.py`. Chequear bounds/NaN.
- `awo_tools/analyze_bin_hd.py` está **DESACTUALIZADO** (layout PS3) — no usarlo.

## 11. HISTORIAL

- `docs/01_estructura/HISTORICO_AGENTS.md` — histórico verbatim hasta 2026-09-02
  (items 8-65, Janemba §11.1, inyección §65.1.x, swaps de cabeza, releases
  1.0.x-1.1.1, limpieza de disco §14.24).
- `docs/01_estructura/HISTORICO_RELEASES.md` — detalle verbatim de: §A narrativa
  de releases 1.1.3→1.2.9 y runtime; §B investigación del port PS2→B3 (Fases
  B/C, GPU, intentos T2-T11); §C launcher completo.
- Otras referencias: `awo_tools/CONSOLIDADO.md`, `awo_tools/RE_PROGRESO.md`,
  `docs/07_ports/`, `docs/PLAN_1.1.1.md`, `docs/PLAN_LINUX.md`.

## 12. HOJA DE RUTA ACTUAL

Ver **`docs/HOJA_DE_RUTA_2026_09.md`** — 3 fases:
1. **Documentación ligera** (compactación 2026-09-26: AGENTS ≤ 60 KB; detalle a
   HISTORICO_RELEASES.md).
2. **Limpieza de código muerto** (`dbz3_enabled_mods`, `PrepareRegionData` stub,
   `analyze_bin_hd.py`, artifacts legacy) + depuración pendiente.
3. **RE de contenido por duplicados** (habilidades, slots, stages): auditar bins
   de `data_cmn.afs`, localizar stages/movesets, mapear SLXS/roster + select, y
   duplicar+modificar entradas.

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
  (regenerable con `corpus_scan.py`), `rexglue-sdk/` (0.9) y `rexglue_0.9/`,
  `out/build/_archivo_builds/` + `_archivo_dlls/`, duplicados de docs en
  `modding resources discord/tutorials/`. Detalle:
  `docs/06_limpieza/INVENTARIO_FISICO_2026-09.md`.
- **Limpieza 2026-09-14 (~10.6 GB; 29.4 → 18.8 GB)**: borrados
  `out/build/_archivo_mods/`, `out/build/win-amd64-release/mods_archivo/` (83
  tests), `github/release-stage/` + `release-stage/`, `rexglue_backup/`, el build
  SDK avx2 `out/build-win-vulkan/` (se conserva `out/win-amd64/` con las DLLs
  avx2), los AFS de B1/B2/B2V/Shin Budokai PSP **y sus ISOs** (se conservan **B3
  Greatest Hits** e **Infinite World**), y dedup de `modding resources update*`.
  **Se conserva** `mods/og_music`. Barrido de `__pycache__`/`*.pyc`/`.tmp`/`.bak`.
  **Pendiente de decidir**: `modding resources` (2.2 GB) y `modding resources
  discord` (0.86 GB).
- `out/build/win-amd64-tracy` (perfilado) se borró: regenerar con el preset Tracy
  del CMake si se necesita.
- El usuario habla español. Sesiones largas de juego.
