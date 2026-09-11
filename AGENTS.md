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

- **v1.2.1 publicada (Latest, 2026-09-14)**: hotfix del launcher — (a) **crash al
  cerrar tras Model Swap/Texturas** (el hilo del pipeline quedaba sin unir →
  `std::terminate`; ahora `~ModPipeline` hace `join`); (b) etiqueta de nitidez FSR
  invertida; (c) la lista de mods se refresca al terminar el pipeline
  (`ModPipeline::Generation()`); (d) lectura de la carpeta de texturas sin
  excepciones. Sobre la
- **v1.2.0 publicada (2026-09-14)**: Centro de mods renovado (lista
  cacheada, buscador, activar/desactivar todos, badges de tipo, filas alternas),
  Model Swap HD↔HD pulido (combos con buscador, vista previa, guard
  origen==destino, manifest con nombres de catálogo), nitidez FSR/CAS ajustable,
  aviso de modo ISO en Mods y Model Swap. Limpieza: 83 mods de prueba archivados
  (release con `mods/` vacía). Docs nuevas:
  `docs/ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14.md` (**FSR3/DLSS no viables a
  corto plazo**: el renderer no expone motion vectors/jitter; FSR1/CAS sí) y
  `docs/02_mods/SESION_MODS_LAUNCHER_2026-09-14.md`. Binario 1.2.0; zip
  `DBZ-Budokai-3-HD-Collection-v1.2.0.zip`.
- **v1.1.4 EX publicada (2026-09-10)**: hotfix de la v1.1.4 que cierra
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
- **Port completo (topología PS2) = ❌ NO RENDERIZA (2026-09-11)**: el GPU dibuja
  con **ventanas de 44 B autocontenidas** en la región `[vb0,ib)` del AWO + IB.
  La geometría del port es **exacta** y **llega al GPU**; tras §3.4.9 el **draw
  también es correcto** (1 draw strip, VB+IB verbatim) y el **bloqueo restante es
  el skinning**. ⚠️ La "validación previa" era el swap nativo (`cell_native`), NO
  el port. Ver **§3.4.9** (definitivo) + `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md`.
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
VÉRTICES del pool `[A_start, A_start+A_count)`** (NO `[min(B),max(B)+1)`; los
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

### 3.4 🔴 MODEL PORT PS2→B3 HD — ESTADO CONSOLIDADO (leer ANTES de tocar el port)

> Referencia ÚNICA del port. Historial detallado: §3.1, `docs/07_ports/` y
> `docs/01_estructura/HISTORICO_AGENTS.md`.

#### 3.4.1 ESTADO ACTUAL (verificado en juego 2026-08-26)

| Vía | Estado | Mejor resultado | Notas |
|---|---|---|---|
| Swap nativo B3→B3 | ✅ FUNCIONA | sw_goten_nativo, sw_vegeta424 | bin #AMB completo en slot ajeno |
| Inyección (template + posiciones PS2) | ✅ FUNCIONA (reconocible) | **cell_npm4** (umbral binario 0.8) | cuerpo PS2 + extremidades/cabeza HD |
| Port completo (topología PS2) | ❌ NO RENDERIZA (2026-09-11) | geometría + **draw CORRECTOS** (1 strip draw, VB+IB verbatim); falta el **skinning** | Tras §3.4.9 la causa raíz (list-vs-strip) y la 2ª fuente (arms) están resueltas; ver §3.4.9 |
| Swap de cabeza HD→HD | ◑ parcial | goku_armadura v3 | z-fighting, pausado por decisión |

#### 3.4.2 HECHOS VALIDADOS (cómo renderiza el guest)

1. **Dibuja por los `B` de los descriptores 0x60 + el prim POR DESCRIPTOR
   (probado 2026-09-11, §3.4.9)**: el `(dma-0x1BD00000)//2` de cada draw == el
   `B_start` del descriptor; el IB se usa **verbatim** (33/33 draws del cuerpo
   coinciden con `AwgVertexBuffer.load(port).indices()`). El prim del draw es
   por descriptor: cuerpo = `prim=6` (Xenos raw = strip), manos/cara = `prim=4`
   (list); correlaciona con el campo **`+0x48`** del descriptor (`0x500`=strip,
   `0x400`=list; enum D3D `kTriangleList=4/kTriangleStrip=5`) y con `+0x30` de
   los part-descriptores de los arms (5/4). **El fetch de vértices es GLOBAL**
   (`VF[95] 0x1BD04000 size=5148×44`), así que **los rangos `A` NO se usan para
   el fetch**; solo importan `B` + prim.
   ⇒ **🔴 CAUSA RAÍZ del port (Vía B)**: `port_b3_windows.py` emite el IB como
   **LISTA**, pero el guest dibuja el cuerpo como **STRIP** ⇒ triangulación
   incorrecta ⇒ "explosión". Fix: **emitir el IB como strip** (§3.4.9).
2. **Usa el bone del vértice (+28) para el transform** (test bone0: bones→0
   colapsa TODO a los pies; cara sup. y una mano se salvan → viven en vb2).
3. ~~**HAY un consumo del pool POR POSICIÓN (no solo por IB)**~~ ⇒ **🔴
   REFUTADO 2026-09-11** (T10: el GPU buffer es copia verbatim del pool y el
   guest usa el IB del fichero ⇒ un relabeling consistente es identidad; la
   deformación de T3/T4/T9 era un **bug de base de índices del tool**, no un
   consumidor posicional — ver §3.4.6.1 y `SESION_GPU_DRAW_2026-09-11.md` §6-7).
   Contexto histórico de la Fase B (2ª iteración 2026-09-10), ahora superado:
   - **Prueba dura**: siguiendo el IB índice a índice, los registros de vértice
     son **IDÉNTICOS** en T2, T3 y T4 (`IB-follow same=5125 diff=0`). Un
     relabeling consistente (permutar el pool + remapear el IB) es una
     **identidad geométrica** → por eso T2 (swap) es idéntico.
   - **Pero T3** (reverse del pool + IB remapeado + A/B recomputados) y **T4**
     (reverse dentro de cada bloque A, A intacto) renderizan **DEFORME** (el
     usuario: "las mismas exactas deformidades"). ⇒ el guest **NO dibuja solo por
     el IB**: hay una estructura que referencia el pool por posición/offset.
   - **H3 (bloque A = unidad) es INSUFICIENTE**: cada bloque A contiene 2–85
     **runs de hueso** (no es "un bloque = un hueso").
   - **Descartado**: no es fallo de carga (logs `AFS OVERRIDE HIT` en 327), ni de
     compresión, ni bug de remapeo del IB (validado por IB-follow).
   - El consumo posicional **no aparece** como índice u16/u32 crudo en AWG0 ni
     con encodings `v*44`, `v*44+sec`, `v<<2`, `v<<8` (scans `phase_b_consumer_
     scan` + `phase_b_deep_scan`). 15 entradas del IB fuera de rango (2182–2189,
     8 más allá del pool) al final del IB → posible buffer/pool adicional.
   - **Test T5** (`mods/_t5_noremap`): invertir el pool **sin** tocar el IB →
     **MUCHO PEOR** + textura de cara extendida por el cuerpo.
   - **Test T6** (`mods/_t6_adesc`): pool e IB intactos, **rotar SOLO los rangos
     A** entre descriptores → **NORMAL**. ⇒ **el rango A NO se usa para dibujar**
     (no es la vía posicional).
   - ⇒ **el pool se consume POSICIONALMENTE por una vía que NO es A** (T4/T5
     deforman con pool cambiado; T6 normal con pool intacto). T5≫T4 pudo ser solo
     una permutación peor (no prueba que el IB importe).
   - **Test T7** (`mods/_t7_ibrev`): pool intacto, **IB entero invertido** →
     **DEFORMIDAD MASIVA** (cabeza destruida, una mano bien, silueta mal). ⇒ **el
     IB SÍ gobierna la conectividad**. Pero T4 (IB consistente) deformaba ⇒ **hay
     un consumo POSICIONAL adicional que NO es A** (T6).
   - **Histórico**: el `DBZ3_DRAW` log probó que el guest dibuja los strips
     correctos (B_start/B_count) y el amorfo venía de la **estructura de skinning
     (arms) atada al orden del pool** (`SESION_INYECCION_2026-08-26.md`). Encaja
     con todo: T2 (swap intra-hueso) OK, T4 (cruza huesos) deforme, T6 (A)
     normal, T7 (IB) deforme.
   - **Dos tablas** (CORREGIDO en Fase C, 2026-09-10): lo que parecía una 2ª
     tabla de descriptores en `AWG0+0x1F80` es la **tabla de matrices bind-pose**
     (a la que apuntan los `p2` de los arms). La tabla de descriptores real es la
     de 0x60 del mesh-group. **A = rango de vértices del pool** (§3.4.8) y los
     descriptores de parte de los arms completan la **partición del pool**.
4. **Los "arms" son punteros a descriptores de parte (Fase C, 2026-09-10)**:
   `arm = [bone, p1, 0, p2, 0]`; `p1` → **descriptor de parte** (label + rango de
   vértices `(start,count)` en `+0x38/+0x3C` + `data_off` en `+0x44`); `p2` →
   **matriz bind-pose** 4×4 (64 B) en la tabla `AWG0+0x1F80`. Refuta
   `CONSOLIDADO §13.5.13` y el `port_ps2_to_b3.py` (punteros corruptos). Sólo 7
   huesos del AWG0 tienen arm con datos (0,23,30,36,38,40,47).
5. **Layout sec34** (§3.2): stride 44. La plantilla usa SOLO bones 0-33 en el
   sec34 (los 34-47 van a vb2/otros AWG). ⚠️ **Formato C** (Babidi): el marker
   NO es FFFFFFFF, no hay align +2 y el bone NO está en +28 (va en +40). El
   pipeline debe AUTODETECTAR formato A vs C.
6. **vb2 de Cell F2** = layout B (§3.2) — aún no emitido correctamente por el port.
7. **El bin es AUTOCONTENIDO** (cada personaje con su formato A/B/C; el guest
   autodetecta). El nº de AWGs/huesos varía por personaje (Krillin 18 AWG/51
   bones; Bulma 2/43; Babidi 1/41).
8. **Conversión PS2→bone-local**: `local = inv(world[bone])·model` (verificado).

#### 3.4.3 LAS DOS VÍAS

- **Vía A — INYECCIÓN (FUNCIONA)**: mantener el ORDEN del pool de la plantilla
  y reescribir +12/+16/+20 (y normales `[nz,-ny,nx]`) con la geometría PS2
  convertida a bone-local. Parámetro crítico: **umbral binario** (0.8 bueno,
  2.0 malo; blends/soft SIEMPRE malos). Limitación: no es la topología PS2.
- **Vía B — PORT COMPLETO (❌ NO RENDERIZA, 2026-09-11)**: el "consumo posicional"
  (T3/T4/T9) era un bug de base de índices del tool; el GPU dibuja por el IB sobre
  las ventanas (§3.4.9). **Resuelto**: (a) emitir el IB como **strip**
  (`mod center hd/ports/port_b3_strip.py`); (b) la **2ª fuente de draw** = los
  **part-descriptores de los arms** (`+0x40/+0x44`), ya anulados. Tras eso queda
  **1 solo draw** con VB+IB correctos, **pero sigue deforme** ⇒ **bloqueo de
  fondo = SKINNING/RIG** (skin PS2 vs animación HD; `--hd-skin` empeora). El HD
  es un **RE-TRABAJO** del rig, no 1:1. Pipeline: `port_b3_windows.py` +
  `port_b3_strip.py` (+ `--fit` o `grow`). `grow()` OK (probado con `_grow_tpl`).
  ⚠️ NO usar como entrega.
  **Vía A** (inyección) sigue disponible pero su permutación `(lc[2],lc[0],lc[1])`
  es incorrecta (orden natural); se conserva como referencia.

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
| 26/08 | Reverse test (pool invertido) | deforma | lección **CONFIRMADA** por Fase B (no era artefacto) |
| 26/08 | bone0 test (bones→0) | colapsa a pies | **el guest usa el bone del vértice** |
| 10/09 | Fase B: census + T2 (swap 2 verts) | **IDÉNTICO** | relabeling consistente + IB ok = identidad geométrica |
| 10/09 | Fase B: T3 reverse limpio | **DEFORME** | el orden del pool SÍ importa |
| 10/09 | Fase B: T4 reverse intra-bloque A | **DEFORME (igual que T3)** | **hay consumo POSICIONAL; el IB no basta** |
| 10/09 | Fase B: IB-follow T2/T3/T4 | **same=5125 diff=0** | prueba dura: el guest NO dibuja solo por el IB |
| 10/09 | Fase B: huesos en A | 2–85 runs por bloque | **H3 insuficiente** (A no es "un hueso") |
| 10/09 | Fase B: T5 (pool rev, IB intacto) | **MUCHO peor** (cara extendida) | pool cambiado rompe el render |
| 10/09 | Fase B: T6 (solo rotar A) | **NORMAL** | **A NO se usa para dibujar** |
| 10/09 | Fase B: T7 (IB rev, pool intacto) | **MASIVO** (cabeza rota) | **el IB SÍ se usa**; hay consumo posicional extra |
| 10/09 | Fase B: "2ª tabla descriptores @AWG0+0x1F80" | era la tabla de matrices bind-pose | corregido en Fase C (real: descriptores 0x60) |
| 10/09 | Fase C: descriptores + arms | **A = rango de VÉRTICES; tesela el pool** | **consumidor posicional = rangos de parte** |
| 10/09 | Fase C: **T8** permutar 2 partes enteras (manos) + IB | **IDÉNTICO** | **permutar partes es SEGURO; cada run de hueso debe quedar contiguo** |
| 10/09 | Fase C: **T9** reordenar runs mono-hueso dentro de un bloque + IB | **DEFORME** (cara/brazo) | el orden intra-bloque importa; no hay tabla posición→hueso → dependencia GPU |

#### 3.4.5 BLOQUEADORES Y ERRORES CONOCIDOS

1. **El pool está PARTICIONADO por rangos de parte (LOCALIZADO en Fase C,
   2026-09-10)**: el pool no es reordenable libremente porque sus vértices están
   repartidos en **rangos `(start,count)` por parte**, declarados en **A de los
   descriptores 0x60** (`+0x50/+0x54`) y en los **descriptores de parte de los
   arms** (`+0x38/+0x3C`). La unión de ambos **tesela el pool entero `[0,n_pool)`
   sin solapes ni huecos** (Cell F2: 29 descriptores + 7 partes = 2937/2937). Los
   índices del IB (rango B) son **globales**. Éste es el "consumidor posicional".
   La "2ª tabla @AWG0+0x1F80" era en realidad la **tabla de matrices bind-pose**.
   Ver `docs/07_ports/SESION_FASE_C_CONSUMER_2026-09-10.md`. Queda por precisar el
   **mecanismo fino** (T4 deforma aunque A+IB consistentes) → test T8.
   **✅ T8 (2026-09-10)**: permutar **partes enteras** (2 manos) + remapear el IB
   → **IDÉNTICO en juego**. ⇒ **el orden GLOBAL de partes es libre**, pero cada
   **run de hueso debe quedar contiguo** (T4 deformaba por mezclar runs dentro de
   un bloque; T2 era idéntico porque NO cruzaba runs). ⇒ **Vía B = ingeniería**
   (emisión coherente pool/A/B/IB), ya no un misterio.
   **Verificado** (`awo_tools/awg_invariants.py`): las partes **NO** son
   homogéneas de hueso (Krillin 14/18 y Cell 28/35 mezclan huesos).
   **✅ T9 (2026-09-10)**: reordenar los **runs mono-hueso DENTRO de un bloque** +
   remapear el IB (identidad geométrica) → **DEFORME** (cara/brazo). ⇒ el **orden
   intra-bloque importa**; el "run" NO basta. Se buscó una **tabla posición→hueso**
   (`phase_c_find_bonemap.py`, u8/u16/u32) y **NO existe**; y el `+28` **sí** se usa
   (bone0). ⇒ la dependencia **no está en el bin**: es del **draw/vertex-fetch
   (GPU)**. ⇒ **Vía B NO reconstruible a ciegas**: requiere **RE del draw a nivel
   GPU**. Lo único seguro es **mover partes enteras** (T8).
2. **H3 insuficiente**: los bloques A contienen 2–85 runs de hueso; no son
   "un hueso por bloque". La partición A contigua es real pero no la causa.
3. **vb2 layout B** de Cell F2: aún no emitido correctamente por el port.
4. **`port_ps2_b3_inject.py` hardcodea `axes_base = mg+0x6E0`** (solo Cell F2).
   Debe usar el campo AWG `+0x14`. `port_ps2_b3_pack.py` conserva arms/mesh-ref
   de la plantilla (contradice la Vía B correcta).
5. **⚠️ Contaminación de tests**: `AfsFindModOverride` sirve el PRIMER mod
   activo (orden alfabético). Un mod olvidado invalida los tests del mismo slot.
   → **UN SOLO mod activo por test**.
6. **Crecimiento del AWG0/sec34**: en exceso → crash 0x856AC389 (histórico §27).
   Para el port usar conteos ≤ plantilla o resolver el crecimiento.
7. Los soft/blends (npm6/npm7) y umbral 2.0 SIEMPRE empeoran vs npm4.
8. **Fuente PS2**: los `ps2_games/*/data_cmn.afs` SON #AMO0/#AMG LE
   auténticos (B3 GH 558 #AMO0 / 0 #AWO). El "bloqueo de fuente" de
   `SESION_BABIDI` era un error de la herramienta que inspeccionó la entrada.

#### 3.4.6 PRÓXIMOS PASOS (orden de avance)

0. **RE del draw a nivel GPU (2026-09-11) — HECHA**. Instrumentado
   `rexglue-sdk-0.10/src/graphics/command_processor.cpp` (log `dbz3_draws.log` +
   `dbz3_vf.bin` junto al exe; marker `dbz3_drawlog.on`). Capturas: draws
   indexados (`src=0`, int16), el vertex buffer es **copia VERBATIM** de la región
   `[vb0, ib)` del AWO. Detalle: `docs/07_ports/SESION_GPU_DRAW_2026-09-11.md`.
   ✅ **Instrumentación REVERTIDA (2026-09-12)**: el edit de `command_processor.cpp`
   se deshizo (`rexglue_dbz3_path` + bloque de logging del caso `kDMA`), se
   recompiló `rexgpu-xenos` baseline (6165504 B, sin marcas `dbz3_draws.log`) y se
   reinstaló en el build del juego; artefactos de test archivados en
   `%TEMP%\opencode\draw_evidence\`. La DLL canónica ya **NO** está instrumentada.
1. **Vía B — ❌ NO RENDERIZA (2026-09-12)**. El vertex buffer del GPU es una
   **copia VERBATIM de la región `[vb0, ib)` del AWO** (`ib = awg0+g(0x30)`;
   `vb0 = ib - g(0x2C)`; `g(0x2C)` = **TAMAÑO del buffer en bytes**), formada por
   **N ventanas de 44 B autocontenidas**:
   ```
   +0 pos.xyz(3f) | +12 w | +16 bone(u32,1B) | +20 nrm.xyz(3f) | +32 FFFFFFFF | +36 uv.xy(2f)
   ```
   El IB (`g(0x30)`, int16/u16 BE) referencia índices de ventana (0..N-1). **T11**
   (reverse de las ventanas + `IB'=perm[IB]`) → **idéntico total** (el relabeling
   consistente es identidad). El "consumidor posicional" de T3/T4/T9 era un bug de
   base de índices del tool (rejilla `sec+2` desalineada +428 B).
   **Herramienta canónica**: `awo_tools/awg_vertex_buffer.py` (`info`/`permute`/
   `roundtrip`/`grow`/`selftest`; `bind_worlds()`/`bone_labels()`/
   `window_from_model()`; API `load().vertices/.indices/.emit()`).
   **SEMÁNTICA**: `pos = inv(world[bone])·model` y
   `nrm = inv(world[bone]).R·model_nrm`, **orden NATURAL (x,y,z)**.
   ⚠️ **El IB de la plantilla NO es lista sino STRIP (prim=6) en el cuerpo** →
   ver **§3.4.9 (definitivo, 2026-09-11)**.
   **Conversor**: `mod center hd/ports/port_b3_windows.py <extract.json> <tpl.bin>
   <out> [--fit|--no-grow|--hd-skin]` (mapea huesos por label).
   **🔴 ESTADO REAL → VER §3.4.9**: el port tiene **geometría + draw CORRECTOS**
   (1 draw `prim=6` strip, VB+IB verbatim) y el bloqueo restante es el
   **SKINNING/RIG**. La hipótesis antigua "hay que reconstruir los rangos A/B y
   los mesh-refs" quedó **SUPERADA** por §3.4.9.
   **`grow(new_n,new_nib)`** amplía ventanas+IB. **2 bugs corregidos 2026-09-12**:
   (a) la tabla AWG (relativa a `awo`) se reajustaba **dos veces** en el bucle de la
   cabecera `#AWO` → 16 entradas apuntaban a basura → crash parser `#AMB`; (b) **no
   se actualizaba `AWG0+0x2C`** (tamaño del buffer) → el GPU sólo hacía fetch de
   las ventanas viejas. Sigue **sin renderizar bien** tras ambos fixes.
   Test: PS2 Cell → plantilla `e147` → mod `cell_viab`/`cell_viab_grow` (147),
   `_grow327` (port en slot Krillin 327). `cell_native` (327) = swap nativo
   (renderiza perfecto, pero NO es el port).
   ⚠️ El pipeline antiguo `mod center hd/ports/port_ps2_b3_{geometry,draw,pack}.py`
   usaba descriptores A/B + buffers separados (modelo INCORRECTO) → reconstruir
   sobre `awg_vertex_buffer.py`. Detalle: `SESION_GPU_DRAW_2026-09-11.md` §6-9 +
   `SESION_VIA_B_RENDER_2026-09-12.md`.
2. **Vía A (práctica)**: reactivar/refinar `cell_npm4` (inyección NPM, umbral
   0.8). Es la entrega validada.
3. Corregir pipeline (si se retoma): `geometry.py`, `inject.py` (`axes_base`
   `+0x14`), autodetectar A/C.
4. Cerrar `vb2` (layout B) para cara/piernas.
5. Para estado jugable ya: **reactivar `cell_npm4`** (Vía A, mejor inyección).

#### 3.4.7 REFERENCIAS

- `docs/07_ports/SESION_FASE_B_ARMS_2026-09-10.md` (Fase B: arms, census, T2/T3/T4).
- `docs/07_ports/SESION_FASE_C_CONSUMER_2026-09-10.md` (Fase C: partición del
  pool por rangos de parte; corrección de la "2ª tabla").
- `docs/07_ports/SESION_GPU_DRAW_2026-09-11.md` (RE GPU del draw: instrumentación,
  captura 318 draws indexados, buffer derivado; pasos siguientes).
- `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` (**definitiva + RETOMO §0**:
  el guest usa el IB del port verbatim, draws por `B` de descriptores, prim por
  descriptor en `+0x48`; list-vs-strip = causa raíz; **2ª fuente de draw** =
  part-desc. de arms `+0x40/+0x44`; intentos `_desc_one`/`_strip2`/`_strip3`/
  `_hdskin_strip`; **bloqueo restante = SKINNING**; comandos de reproducción y
  siguiente paso en §0).
- Instrumentos: `awo_tools/awg_vertex_buffer.py` (⚠️ **canónico Vía B**: modelo
  de ventanas + IB; `info`/`permute`/`roundtrip`), `awo_tools/phase_c_make_t10.py`
  (reverse sec34 base guest), `awo_tools/phase_c_make_t11.py` (permutar ventanas),
  `awo_tools/phase_c_descriptors.py`, `phase_c_arms_targets.py`,
  `phase_c_meshgroup.py`, `phase_b_census.py`, `phase_b_consumer_scan.py`,
  `phase_b_make_t2.py`, `phase_b_make_t3.py`, `phase_b_make_t4.py`,
  `phase_b_make_t5.py`, `phase_b_make_t6.py`, `phase_b_make_t7.py`,
  `phase_b_desc_detail.py`, `phase_b_deep_scan.py`, `phase_b_ab_compare.py`,
  `phase_b_arms_dump.py`, `afs_extract_hd.py`.
- Mods de test: `mods/_t2_swap` (idéntico), `mods/_t3_reverse`/`_t4_inpart`
  (deforme), `mods/_t5_noremap` (mucho peor), `mods/_t6_adesc` (normal),
  `mods/_t7_ibrev` (masivo). UNO activo a la vez.
- `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md`, `SESION_INYECCION_2026-08-26.md`,
  `SESION_PORT_RE_2026-08-26.md`, `SESION_VIA_B_RENDER_2026-09-12.md`,
  `HOJA_DE_RUTA_PORT_PS2_B3.md`.
- Pipeline en `mod center hd/ports/` (`port_ps2_b3_extract/geometry/draw/pack/
  verify.py` + `port_ps2_b3_inject.py`).

#### 3.4.8 DESCRIPTORES 0x60 Y PARTICIÓN DEL POOL (Fase C, 2026-09-10)

**Descriptor 0x60** (localizado por el tag ASCII `"max N m"` en `+0x18`):
```
+0x00 label[]     +0x18 "max N m"   +0x44 type==0x2C00
+0x50 A_start<<8  +0x54 A_count<<8     A = rango de VÉRTICES del pool
+0x58 B_start<<8  +0x5C B_count<<8     B = rango de ÍNDICES del IB
```
- **A tesela el pool** `[0, n_pool)` **sin solapes**; los índices del IB (B) son
  **globales** (`min==A_start`, `max==A_start+A_count-1`).
- Los descriptores de parte (arms) tienen el rango en `+0x38/+0x3C` + label en
  `+0x48`; **completan los huecos** de la tabla principal.
- **Partición total (Cell F2)**: `[0,2937)` = 29 descriptores + 7 partes, 0
  solapes, 0 huecos. `awo_tools/phase_c_descriptors.py` hace el mapa.
- Matriz bind-pose 4×4 (64 B) de cada arm en la tabla `AWG0+0x1F80` (los `p2`).

#### 3.4.9 SEMÁNTICA DEL DRAW Y CAUSA RAÍZ DEL RENDER (2026-09-11)

> Detalle completo: `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md`.

- **El guest usa el IB del port VERBATIM** (33/33 draws del cuerpo coinciden con
  `AwgVertexBuffer.load(port).indices()` en `(dma-0x1BD00000)//2`). ⚠️ La nota
  previa "la IB del guest ≠ fichero" era un **error de offset** de comparación.
- **Draws = descriptores 0x60**: `(dma-0x1BD00000)//2 == B_start` (match exacto).
- **Prim POR DESCRIPTOR**: cuerpo `prim=6` (strip), manos/cara `prim=4` (list);
  campo **`+0x48`** del descriptor (`0x500`/`0x400`) y `+0x30` de los part-desc.
  de los arms (5/4). Enum SDK: `kTriangleList=4`, `kTriangleStrip=5`.
- **Fetch de vértices GLOBAL** (`VF[95] 0x1BD04000 size=5148×44`); **los rangos A
  no se usan para el fetch**.
- **🔴 CAUSA RAÍZ**: el port emitía el IB como **LISTA** pero el guest dibuja el
  cuerpo como **STRIP** ⇒ explosión. La geometría del port (ventanas + IB) es
  **correcta** (render offline perfecto: `%TEMP%\opencode\phaseb\view_port.png`).
- **Fix en curso**: emitir el IB como **strip preservando winding**
  (tool `mod center hd/ports/port_b3_strip.py`; valida `orient_mal=0`).
- **Intentos y resultados (NO repetir)**:
  - `_desc_one` (1 descriptor `B=[0,n_ib)`, `+0x48=4` list, resto `B_c=0`):
    **sigue explotando** ⇒ `+0x48` **no** basta para cambiar el prim del draw.
  - `_strip2` (IB como tira + 1 descriptor): **la deformidad cambió** (se
    reconocen cabeza/torso/brazo) **pero aún explotaba** (flap) ⇒ había **otra
    fuente de draw** (resuelta en `_strip3`: los arms `+0x40/+0x44`).
  - **`_strip3`** (strip + `desc[0]` + anular arms `+0x3C` **y `+0x44`**): captura
    "log all" ⇒ **queda UN SOLO draw real** (`prim=6 idx=8726 off=0`); el VB del
    guest es copia **verbatim** del fichero y el IB coincide ⇒ geometry/draw OK.
    **Pero sigue deforme ⇒ el bloqueo restante es el SKINNING.**
  - **🔴 2ª FUENTE DE DRAW LOCALIZADA (2026-09-11)**: los **part-descriptores de
    los arms** guardan `+0x38 vert_start`, `+0x3C vert_count`, **`+0x40 idx_start`,
    `+0x44 idx_count`** y label en `+0x48`. Los `+0x40/+0x44` producen draws
    EXTRA (manos/cara) en los offsets de la plantilla. Anular solo `+0x3C` NO
    basta: hay que anular **también `+0x44`**. (`port_b3_strip.py` ya lo hace.)
  - **Skinning = bloqueo de fondo**: el mesh lleva el **skin PS2**; la animación
    HD lo deforma (en bind no se ve). ~66% de huesos difieren del HD vecino, y
    **`--hd-skin` por vértice más cercano EMPEORA** (`_hdskin_strip`, probado).
    Es el problema de fondo: **el HD es un RE-TRABAJO del rig**, no 1:1. Próximo:
    (1) restringir a huesos 0-33 (cuerpo) para aislar manos/cara; (2) transfer de
    skin HD mejor que nearest; (3) port por regiones en los AWG1-16.
  - **AISLAMIENTO + EXPERIMENTO GPU (2026-09-13)**: la plantilla AWG0 usa SOLO
    huesos 0-33; el port mete 1638 refs a 34-47. Test `_body33` (≤33) → **también
    explota** ⇒ no es (solo) 34-47. Instrumentado el runtime para volcar por draw
    la **paleta `fc=94` + VB + IB** (`dbz3_capture.bin`): **el VB del guest es
    COPIA VERBATIM del bin** y **la paleta es IDÉNTICA entre el port (explota) y
    el nativo `_grow_tpl` (renderiza PERFECTO)** ⇒ **la paleta/draw/IB NO son el
    problema**. 🔴 **El fallo está en los datos `(pos, bone)` de las ventanas (el
    skin PS2).** La paleta (128×48 B/draw) tiene un mapeo `bone→slot` **NO
    decodificado** (no se indexa por bone crudo). Próximo: arreglar el skin PS2
    (tablas de peso, 2 influencias — ver `INVESTIGACION_PS2_HD_2026-09-13.md`).
    Instrumentación **REVERTIDA**; DLL limpia reinstalada. Mods: `_body33`,
    `_nottail`; detalle sesión §8/§10. Entrega usable = `cell_best2` (Vía A).
  - **AUDITORÍA DEL SKIN + `_bone0port` (2026-09-13b, sesión §12)**: el skin PS2
    (`port_ps2_b3_extract.py extract_skin`) solo cubre **3922/5148 verts** (el
    resto, manos/cara, cae al hueso de la PARTE); los pesos 0.2–1.0 son **2
    influencias colapsadas a 1**. `port_b3_windows.py` asigna `hb=bmap[ps2_bone]`
    (para Cell F2 labels PS2==HD ⇒ identidad). Test `_bone0port` (todo rígido al
    hueso 0) → **CRASHEA**; coincide con que el **record 0 de la paleta capturada
    es TODO CEROS** ⇒ **el slot de paleta NO se indexa por el bone crudo** (remapeo
    `bone→slot` sin decodificar; el layout de 48 B tampoco es el naive 3×vec4).
    **Próximo**: decodificar layout + `bone→slot` de `fc=94` con las capturas
    guardadas (`%TEMP%\opencode\port3\capture_native\`), validando contra el
    NATIVO; luego skin PS2 (2 influencias). ⚠️ **NO reintentar `_bone0port`**.
  - **🔴🔴 BUG DEL IB RESUELTO — el "explosion" NO era el skin (2026-09-13c)**:
    cruzando las capturas se vio que el IB que usa el guest **diverge del bin en el
    índice 6301**: a partir de ahí hay BASURA (`0xAAAA`, `0xFFFF`, índices hasta
    63891). Causa: **`awg+0x34` = TAMAÑO del IB en bytes** (`== 2*n_ib` en TODOS
    los AWGs de la plantilla: AWG1 816=2*408, …) y **`grow()` NO lo actualizaba**
    → seguía en 12602 (IB de la plantilla original) → el guest solo servía 6301
    índices y el resto era basura → vertex fetch fuera de rango → **explosión**.
    **FIX**: `awg_vertex_buffer.grow()` ahora hace
    `set32(awg0+0x34, new_nib*2)`. Reescribe el render: **ya NO explota** — sale un
    Cell conectado (aunque deforme). Pipeline (ventanas+IB+draw+paleta+grow)
    **VALIDADO**: test `_strip4r1` (TODO rígido al hueso 1, pos=inv(world[1])·model)
    → **Cell Forma 2 en T-pose PERFECTO con texturas** ⇒ el pipeline está bien.
    **REMAINING = SKIN/ANIMACIÓN**: con los huesos PS2 reales (`_strip4`) o con
    skin transferido del HD (`_strip4hds`) sigue deforme; con **`w=1.0`**
    (`_strip4w1`) mejora (pierna+cintura bien, torso/brazos mal). Los `world` PS2==HD
    (48/48) y los labels 48/48 ⇒ esqueleto y mapeo correctos. ⚠️ Las conclusiones
    previas de skin (hechas con el IB roto) quedan INVALIDAS. Mods de trabajo:
    `_strip4` (ps2 skin), `_strip4r1` (rígido hueso1 = OK), `_strip4w1`, `_strip4hds`,
    `_strip4b33`, `_strip4nt`. Docs: `SESION_DRAW_SEMANTICS_2026-09-11.md` §13.
  - **🔴🔴 VS DE SKINNING DECODIFICADO (2026-09-13e, sesión §15)**: activada la cvar
    **`dump_shaders`** (ya existe en el SDK; `flags.cpp`/`translator.cpp:339`/
    `shader.cpp:122 DumpUcode`; cualquier cvar del `dbz3_user.toml` se aplica vía
    `rex::cvar::LoadConfig`) → `%TEMP%\opencode\shaderdump\` con 91 VS + 54 FS
    (`.ucode.vert`, Xenos). Los **VS de skinning** son los que fetchean **Stride=11
    (VB) y Stride=12 (paleta)**: 6 ficheros. **Paleta (48 B/hueso)** =
    `[T.xyz][qA.x][qB.xyz][qA.y][qC.xyz][qA.z]` (3 vec4 entrelazados); skinning
    `model = R(qA)·pos + T`. `pos` (off 0) = **bone-local**; `bone` (off 4 dw =
    byte 16) = **índice DIRECTO a la paleta** (`vf1+bone*48`, SIN remapeo ni 2º
    hueso); `weight` (off 3) = **blend intra-hueso** (weight=1 ⇒ rígido); no hay
    escala. Después `c0..c3` = mundo·vista·proy. Validado (nativo ⇒ humanoide
    coherente con la paleta decodificada).
    **🔴 BUG REAL**: la paleta tiene **NaN en huesos 42-49** (el juego NO las
    define; el AWG0 de la plantilla solo anima ciertos huesos). El **nativo no las
    usa**; el **port SÍ usa 43-47 (cola)** → `R·pos+T = NaN` → geometría volando.
    Remapear esos huesos ⇒ bbox finito, **pero el render sigue fragmentado** ⇒ hay
    OTRA causa. **Próximo**: (1) remapear/clonar huesos 42-49 del port; (2) con la
    paleta decodificada, comparar `R(qA)·pos+T` port vs nativo vértice a vértice.
    ⚠️ **QUITAR `dump_shaders` del `dbz3_user.toml`** al terminar. Script:
    `%TEMP%\opencode\port3\decode_pal_repro.py`. Detalle: sesión §14/§15.
  - **🔴🔴 CAUSA RAÍZ: NUESTRO `world` ES INCORRECTO (2026-09-13f, sesión §16)**:
    reconstruir el modelo del **NATIVO** con **nuestro `world`** (`world_ours[b]·pos`,
    con la triangulación LISTA correcta) da un render **DESTROZADO**; con la paleta
    del juego (`R(qA)·pos+T`) da un humanoide coherente. Los `T` de la paleta (origen
    real del hueso) NO coinciden con nuestro `world`: hueso 1 (waist) paleta
    `(0.92,7.23,-0.11)` vs nuestro `(0,0,0)`; hueso 2 paleta y=7.80 vs nuestro y=0.65.
    ⇒ **`awg_vertex_buffer.bind_worlds()` NO da el bind real**: los ejes del AWG
    (stride 80) tienen **traslaciones ~0** → la acumulación por padre da frames
    erróneos. ⚠️ **El render de bind offline es SIEMPRE limpio** (`world·inv(world)·model
    = model`, auto-consistente) → **NO valida `world`**; y el test rígido (`_strip4r1`)
    tampoco (una sola transformación global mantiene el modelo coherente).
    **Consecuencia**: `window_from_model` hace `pos = inv(world_ours)·model` en un
    frame EQUIVOCADO → el guest (`R_anim·pos+T_anim`) desplaza cada pieza → **la
    deformación** (afecta a **Vía B y Vía A**). El skin PS2 NO era el problema.
    **Próximo**: encontrar el bind real (frame de animación identidad; convención
    alternativa de los ejes; zonas `AWG0+0x1F80`/`+0x2000`; o derivarlo de la
    animación frame 0). Ficheros: `nat_world_LIST.png` (destrozado) vs
    `dec_native.png` (coherente) en `%TEMP%\opencode\port3\`.
  - **`_grow_tpl`** (plantilla NATIVA Cell F2 pasada por `grow()` a 5148/8724,
    sin tocar nada más): **renderiza PERFECTO** ⇒ **`grow()` está BIEN**;
    ❌ **NO culpar a `grow()`**. El flap del port es de una **2ª fuente de draw**
    o de los datos del port.
  - `_fit_strip` (`--fit`): **inválido** — `cluster_fit` decima y deforma la
    malla; no sirve para validar render.
  - **🔴 INVESTIGACIÓN DEL BIND (2026-09-13g)** — ver `SESION_DRAW_SEMANTICS_2026-09-11.md` §17.
    Verificado: (1) layout de ventana correcto; (2) AWG0 usa huesos 0-33 con
    labels correctos; (3) la jerarquía `+0x40 rel awg0` de `bind_worlds()` es
    CORRECTA; (4) **NINGUNA convención de los ejes reconstruye el bind** (búsqueda
    exhaustiva; métrica de aristas de frontera ≥3.0 para un personaje de ~13 u);
    (5) **NO hay tabla de matrices bind** en el fichero; (6) la paleta `fc=94`
    (128×48 B, `model=R(qA)·pos+T`) es IDÉNTICA port/nativo = única fuente fiable
    y `P = M_anim·M_bind^-1`; (7) **`P[b]` NO es la transform del hueso `b`**
    ⇒ falta decodificar el **mapeo `bone→slot` de la paleta**; (8) el `pos` crudo
    también sale spiky ⇒ es bone-local y necesita el bind.
    **Siguiente (decisivo)**: volcar por draw la paleta + el `bone` crudo por
    vértice y correlacionar `bone→slot` con el render correcto del nativo; o RE de
    la función PPC que rellena la paleta. Vía corta a render: bakea la paleta
    capturada como bind (`pos_port = P_ref[b]^-1·model_ps2`).
    **🔴 CORRECCIÓN (2026-09-13g, 2ª parte)**: renderizando SOLO los orígenes de
    `bind_worlds()` (líneas hueso→padre, `skel_worldT.png`) se ve un **esqueleto
    T-POSE PERFECTO** ⇒ **`world`=bind y la jerarquía de `bind_worlds()` es
    CORRECTA** (antes se dio por rota por error). `skel_paletteT.png` NO es T-pose
    ⇒ **la paleta = `M_anim` (pose select)**, no el bind. Pero `world·pos` sigue
    deforme aun con la triangulación STRIP correcta ⇒ **el eslabón que falta es el
    mapeo `índice de hueso del vértice → hueso del esqueleto` (σ)**, no el bind.
    Solve greedy por aristas (`nat_world_solvedsigma.png`) baja la distorsión pero
    cae en mínimo local. **Siguiente óptimo**: volcar del guest el **buffer de
    inverse-bind real** (o la matriz `M_bind`), o resolver σ con mejor init.
- **🔴 Bug de tool corregido**: `awo_tools/awg_vertex_buffer.py` `_parse` usaba el
  **máximo índice del IB** para `n`/`vb0`; en ficheros crecidos con `grow` (IB que
  no referencia el tramo nuevo) calculaba mal `vb0`. Ahora usa **`g(0x2C)//44`**
  (tamaño real del buffer de ventanas, el que usa el guest).

### 3.4.10 CIERRE (2026-09-13i) — Vía B APARCADA; HD↔HD = entrega; LAYOUT corregido
**Decisión**: Vía B (bind real) **aparcada** (no imposible). Vía A documentada como
aproximada. La entrega es el **swap nativo B3 HD↔HD** (ya en el launcher).
Detalle completo: `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` §20.
- **🔴 LAYOUT REAL (los 17 AWGs)**: TODOS usan el mismo **layout de ventana**
  (`pos@0`, `w@12`, `bone@16`, `nrm@20`, `FFFFFFFF@32`, `uv@36`, stride 44); región
  `[ib−g(0x2C), ib)` con `g(0x2C)/44` slots exactos (verificado: FFFF@32 en el
  100% de los slots de AWG0..16). **El layout "sec34" (`FFFF@0`,`bone@28`) es un
  error** para estos bins y `port_ps2_b3_inject.py` escribía con **desfase de
  428 B (10 slots)** + conteo erróneo; `port_ps2_b3_inject_aux.py` usaba **6
  "familias" de layout incorrectas** (todos los AWG son el layout de ventana; el
  host 23/30/32 sí era correcto). Fixes: `%TEMP%\opencode\phaseb\make_winbody.py`
  + `make_winaux.py` (mods `cell_winbody`/`cell_win2`). Aun así **Vía A ≈ igual**
  (el fallo visible está en AWG0; la inyección no re-topologiza).
- **Vía B — bloqueo = `M_bind`**: `world` (ejes) da un T-pose correcto pero no es
  el bind exacto del skin; la paleta = transform aplicada a `pos` (bone-local).
  Para retomar: (1) RE de `sub_82087F58`; (2) capturar la paleta en el frame de
  BIND/T-pose. Herramientas comunitarias descartadas (nivel "model part").
- **SWAP HD↔HD (entrega)**: `mod center hd/swap_b3.py` + `catalog_b3.cat` (183),
  mid-insert virtual. En el launcher: pestaña "Cambio de modelo". Cierre/pulido:
  quitado log temporal `pipeline_cmd.log` y **guardia origen==destino** en
  `src/launcher/mod_pipeline.cpp`.

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
  - Baseline (único en uso): `rexglue-sdk-0.10/out/win-amd64-baseline/` →
    rexruntime 10863616 (con afstrace), rexgpu-xenos 6165504 (**sin**
    instrumentación de draw), amd_fidelityfx_dx12 5413888. ⚠️ El **SHA256 varía
    por build** (embebe timestamp) — comparar por **tamaño** o recompilar y
    copiar, no por hash fijo.
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
- **Model Swap (HD↔HD, cerrado/pulido 2026-09-14)**: catálogo
  `mod center hd/catalog_b3.cat` (183) → combos **con buscador** origen HD →
  slot destino, **tarjeta de vista previa**, aviso si origen==destino, y
  `swap_b3.py` extrae el bin #AMB, comprime LZX /N:2048 y lo instala como
  override por entrada (mid-insert virtual si excede `to_read`). El manifest
  generado usa **nombres del catálogo** (`name=Cell Forma 2 en Krillin`,
  `type=swap_b3`, source/target). Deshabilitado en modo ISO (ver abajo).
  `texture_b3.py` extract (PNG) / build (re-codifica DXT3/BC2, mantiene tamaño)
  + `--slot` destino + `--dir`.
- **Centro de mods (refactor 2026-09-14)**: lista **cacheada** (no re-escanea el
  disco cada frame), **buscador** (nombre/autor/origen/tipo), botones **Activar
  todos / Desactivar todos / Refrescar**, badges de tipo con color, filas
  alternas y estado vacío. Instalar mod desde `.zip` (PowerShell Expand-Archive
  vía `-EncodedCommand` base64, inmune a espacios; normaliza wrapper de una
  carpeta), perfiles (`mods/profiles.txt`, cvar `dbz3_mod_profile`), "Abrir
  carpeta".
- **⚠️ Modo disco (ISO) — los mods NO se aplican**: al jugar directamente del
  `.iso`, los overrides por entrada AFS resuelven a ficheros host de la carpeta
  extraída, así que **ningún mod tiene efecto**. La pestaña Mods y la de Model
  Swap muestran un **aviso ámbar** y el botón de swap queda **deshabilitado**.
  Para usar mods: origen "Carpeta extraida".
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
- **v1.2.1 = Latest** (2026-09-14, core dual 1.2.1, baseline, hotfix del launcher).
  **v1.2.0**, **v1.1.4 EX**, **v1.1.3**, **v1.1.2**, **v1.1.1**,
  **v1.1.0-clasico** = no-Latest. Tags v1.0.0..v1.0.9 + v1.0.5-EX conservados
  (código archivado; los zips binarios viejos NO existen).
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

- **Swap nativo HD→HD** (✅ **vía de entrega principal, validada 2026-09-10**
  con Cell Forma 2 (147) → slot Krillin (327), 100% funcional incl. boca):
  `python "mod center hd\swap_b3.py" --origen 147 --dest 327 --mod cell_native`
  (`--list` lista el catálogo). `bin == índice de entrada AFS`; catálogo
  `mod center hd/catalog_b3.cat`. LZX /N:2048 + override por entrada (mid-insert
  virtual si excede to_read). Detalle: `docs/07_ports/SESION_SWAP_NATIVO_2026-09-10.md`.
  **Regla**: si el personaje existe en HD → swap nativo (perfecto); el port
  PS2→HD solo aporta para modelos que NO están en HD.
- **Inyección PS2→HD** (Vía A, FUNCIONA): `mod center hd/ports/port_ps2_b3_
  inject.py <plantilla> <geometry.json> <umbral> <salida> [--npm] [--bone-aware]`
  — world-matching + conversión bone-local + normales `[nz,-ny,nx]`. Mejor: umbral
  binario 0.8. 🔴 **FIX 2026-09-10**: `port_ps2_b3_extract.py` ahora lee el PADRE
  del eje PS2 (`+0x40`, offset rel AMG; `axes_rel=0x20`) y transforma las partes
  "L00" (manos/cara/dientes/cola) de espacio LOCAL a model-space por el world del
  hueso → la inyección sube (1821→1962 @0.8) y **manos/cara alinean** (LHAND
  12.41→0.15). Mod de prueba `mods/cell_npm_fix`. El `axes_base` del inject ya no
  está hardcodeado (usa AWG+0x14). Detalle: `SESION_INYECCION_2026-08-26.md §7`.
- **🔴 LÍMITE ESTRUCTURAL de la Vía A (Cell F2, 2026-09-10)**: el bin HD tiene
  **17 AWGs**: AWG0 (48 huesos, 2661 verts, cuerpo) + **16 AWGs de 1 hueso =
  huesos 48-63** (cara/detalles). El PS2 solo tiene **48 huesos (0-47)** → los
  16 AWGs extra **NO tienen equivalente PS2** y quedan HD (por eso la cara sale
  en HD). La inyección actual solo toca el `sec34` del **primer AWG0**; el bone
  global de cada AWG de 1 hueso se lee en su arm `+0x34`→struct[0]. Para
  PS2-izar la cara hay que remapear por label los huesos PS2 33-40 a los AWGs
  48-63 (formats de vértice variables por AWG: FFFF@0/@12/@28/@32).
  Mod `mods/cell_best` = cuerpo corregido + **manos en HD real** (⚠️ el test
  previo `cell_npm_fix_nohand` revertía a npm4, NO a la plantilla; npm4 ya tenía
  las manos mangleadas: bone 23 con 228 slots movidos) + guardia anti-estirado
  (revierte triángulos con área inyectada >3× la HD; ~68-115 verts).
- **📋 PLAN CONSOLIDADO (2026-09-10)**: `docs/07_ports/PLAN_PS2_B3/PLAN.md` +
  4 informes (`01_WEB.md`, `02_MODS_INVENTARIO.md`, `03_DOCS.md`,
  `04_FORMATO_RE.md`). Corrige la premisa: los 16 AWGs de 1 hueso (48-63) son
  **10 manos + 6 cara** (no 16 de cara), con **6 familias de layout** y mapeo
  PS2→HD resuelto (spec en `04 §7`). Prioridad: (1) visor offline fiable,
  (2) extender la Vía A a esos 16 AWGs, (3) RE del consumo posicional
  (**RESUELTO en Fase C**: rangos de parte que teselan el pool — §3.4.8).
- **✅ FASE 1 EJECUTADA (2026-09-10)**: `mod center hd/ports/port_ps2_b3_inject_aux.py`
  extiende la Vía A a los **16 AWGs auxiliares** (10 manos `world[23]/[30]`,
  superficie PS2 bones 23/30; 6 cara `world[32]`, superficie bone 40) con las
  **6 familias de layout** de `04_FORMATO_RE.md`. 3079/3085 verts inyectados,
  distancias →0, sin NaN. Mods: `cell_best2` (16 AWGs; **ACTIVO** de prueba) y
  `cell_face_only` (solo 6 de cara; fallback). Flags `--only all|face|hands`,
  `--face-thr`, `--hand-thr`.
- **Port completo** (Vía B, ❌ **NO RENDERIZA 2026-09-11**): pipeline
  `port_b3_windows.py` (ventanas 44 B + IB) → `port_b3_strip.py` (IB a **strip** +
  anula arms `+0x3C/+0x44` + `desc[0]=B[0,n_ib)`). **Geometría + draw CORRECTOS**
  (1 solo draw `prim=6`; el guest usa VB+IB verbatim). **Bloqueo restante =
  SKINNING/RIG** (§3.4.9): mesh con skin PS2 vs animación HD; `--hd-skin` por
  vecino **empeora**. Mods de test: `_grow327`, `_desc_one`, `_strip2`,
  **`_strip3` (mejor)**, `_hdskin_strip` (peor) — UNO activo (slot 327).
  `cell_native` (327) = swap nativo (renderiza, NO es el port). `grow()` OK
  (probado con `_grow_tpl`). El pipeline viejo `port_ps2_b3_geometry/draw/pack`
  (descriptores A/B) está SUPERADO. **RETOMO + comandos**:
  `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` §0. Detalle:
  `SESION_DRAW_SEMANTICS_2026-09-11.md` + `SESION_VIA_B_RENDER_2026-09-12.md`.
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
- **Limpieza 2026-09-14 (~10.6 GB; 29.4 GB → 18.8 GB)**: borrados
  `out/build/_archivo_mods/` (mods de test de ago), `out/build/win-amd64-release/
  mods_archivo/` (los 83 tests archivados), `github/release-stage/` +
  `release-stage/` (regenerables con `make_release.ps1`), `rexglue_backup/` (DLLs
  `.old`), el build SDK **avx2** `out/build-win-vulkan/` (se conserva `out/win-amd64/`
  con las DLLs avx2), los AFS de B1/B2/B2V/Shin Budokai PSP **y sus ISOs** de
  `ps2_games/` (se conservan **B3 Greatest Hits** e **Infinite World**), y
  deduplicado `modding resources update*` (9 ítems idénticos + "Budokai 1 Models
  Converted to AMB" duplicado de `mod center`). **Se conserva** `mods/og_music`.
  Barrido de `__pycache__`/`*.pyc`/`.tmp`/`.bak`. **Sigue pendiente de decidir**:
  `modding resources` (2.2 GB) y `modding resources discord` (0.86 GB).
- El usuario habla español. Sesiones largas de juego.
