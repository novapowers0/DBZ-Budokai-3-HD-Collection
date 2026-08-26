# DBZ Budokai 3 HD Collection — Contexto del proyecto

> Documento de contexto para agentes/AI. Consolida el estado del proyecto,
> las decisiones tomadas y el trabajo realizado, para no perder información
> entre sesiones.

---

## 0. ✅ MIGRACIÓN A REXGLUE 0.10.0 — COMPLETADA (2026-08-25)

**Leer `docs/MIGRACION_REXGLUE_010.md` ANTES de tocar el SDK.** Documenta la
migración 0.9.0 → 0.10.0 (ya validada en juego).

- **Hecho**: SDK 0.10.0 clonado en `rexglue-sdk-0.10/` (tag v0.10.0) + submodulos;
  build configurado (D3D12+Vulkan+FFX, clang 22, x86-64-v3); 2 bugs del build
  resueltos (libmspack wrappers, ffx_api_dll.rc UTF-16); **parche del runtime
  portado a 0.10** (afs.h/afs.cpp recreados porque 0.10 los eliminó + host_path_*
  refactorizados + 3 cvars dbz1 restaurados); **rexruntime.dll 0.10.0 compilado
  (10933248 B) con los marcadores del parche**.
- **2026-08-25**: **rexgpu-xenos.dll 0.10.0 compilado (6207488 B)** + SDK
  completo; **SDK 0.10 instalado en `rexglue/`** (respaldo 0.9 → `rexglue_0.9/`,
  PACKAGE_VERSION 0.10.0 verificado); **dbz3.exe compilado contra 0.10**
  (17298432 B, Release) con el código generado **regenerado con el codegen 0.10**
  (`dbz3_codegen`, 44 recomp files; el 0.9 usaba `REX_WEAK_FUNC`, eliminado en
  0.10); los fixes del SDK (CallInUIThreadSynchronous timeout + presenter
  pacing) **ya son nativos en 0.10**; `github/patches/` actualizado a 0.10
  (9 archivos + README).
- **✅ VALIDADO EN JUEGO (2026-08-25, usuario)**: los mods `swap_96_on_327`
  (Babidi→Krillin, override simple) y `tex_91` (Gero, texturas) funcionan, y el
  **mid-insert virtual** funciona (mod `sw_vegeta424`, Vegeta armadura, bin
  126976 > to_read 106496 del slot 327). **Migración 0.10.0 COMPLETA.**
- **Estado del SDK**: `rexglue/` = 0.10 (activo). `rexglue_0.9/` = respaldo
  (mantener por si acaso, ya no es el default). `rexglue-sdk/` = fuente 0.9
  (histórico, no tocar).
- **⚠️ Compilar el juego**: el preset `win-amd64-release` resuelve `clang` al
  toolchain retcomm (MinGW, NO compila `rex/chrono/chrono.h`) — pasar SIEMPRE
  `-DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe"` y
  `-DCMAKE_PREFIX_PATH=.../rexglue` (ver MIGRACION §7.3).
- **⚠️ PENDIENTE (cuando se complete el plan general)**: actualizar
  release/README + README para el SDK 0.10 antes de subir a GitHub (el repo
  `github/` aún no se sube; los parches y docs ya están actualizados en local).

---

## 1. QUÉ ES ESTO

Port recompilado a PC de **DBZ Budokai 3 HD Collection (Xbox 360)** usando el
**ReXGlue SDK** (derivado de Xenia). El proyecto incluye el launcher custom
(`src/launcher/`), la lógica de región/mods, y el runtime (rexglue-sdk).

- **dbz3** (este proyecto): Budokai 3 HD Collection
- **dbz1**: `C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai HD Collection`
  (proyecto hermano, ya con los fixes de input aplicados)

## 2. UBICACIONES CLAVE

| Ruta | Contenido |
|------|-----------|
| `src/` | Código del launcher y del juego (main.cpp, launcher/, ingame/) |
| `rexglue-sdk/` | SDK fuente (runtime, GPU, filesystem) — compilable |
| `rexglue-sdk/out/build-win-vulkan/` | Build SDK (D3D12+Vulkan+FFX, clang) |
| `out/build/win-amd64-release/` | **Build del juego** (dbz3.exe, DLLs, mods/) |
| `out/build/win-amd64-tracy/` | Build instrumentado Tracy (profiling) |
| `eu/`, `us/` | Assets de región (AFS del juego) |
| `ps2_games/` | AFS de B1, B2, B2V, B3 GH, IW (referencias PS2) |
| `mod center/` | Herramientas de modding (36 programas) |
| `modding resources/` | Documentación + recursos (modelos, listas, arte) |
| `modding resources update/` | Buzón para nuevos archivos del usuario |
| `generated/` | Código recompilado del guest (dbz3_recomp.*.cpp) |
| `docs/` | **Documentación organizada (leer PRIMERO)** — índices en docs/README.md |

## 2.1 DOCUMENTACIÓN (docs/ — LECTURA PRIORITARIA)

El proyecto está documentado en `docs/`. **LEER `docs/README.md` primero** y luego:

- `docs/01_estructura/ARBOL.md` — qué es cada carpeta
- `docs/01_estructura/ESTADO.md` — qué funciona / qué falla (estado actual)
- `docs/02_mods/COMO_HACER_MODS.md` — pipeline de mods (override por entrada)
- `docs/02_mods/MODEL_SWAP.md` — investigación de model swap
- `docs/02_mods/TEXTURAS_MOD.md` — pestaña Texturas del launcher (funcionamiento)
- `docs/03_formatos/AMO_AWO.md` + `BIN_LAYOUT.md` — formato del bin
- `docs/04_herramientas/TOOLS.md` — inventario de herramientas
- `docs/05_build/COMO_COMPILAR.md` — compilar juego/SDK
- `docs/06_limpieza/PLAN_LIMPIEZA.md` — plan de limpieza pendiente

## 3. ESTADO ACTUAL (RESUMEN EJECUTIVO)

- **D3D12 = backend principal** (prebuilt 2.7MB, fluido en 3D)
- **Vulkan = experimental** (marcado en el launcher; 3D lento por el render
  path de Vulkan — 6.5x más lento que D3D12 en IssueSwap)
- **Mando**: `input_backend = "xinput"` (evita cuelgue con RTSS/OBS)
- **Config**: D3D12 + 2x + frame_cap 60 + región us + mod sw_goten_nativo (swap nativo validado)

### 3.1 ESTADO ACTUAL (2026-08-17) — VÍA VALIDADA PARA SWAPS

- **✅ SWAP B3→B3 NATIVO = FUNCIONA** (mod `sw_goten_nativo`): reemplazo del
  bin #AMB COMPLETO (AWO+AZT) del personaje en el AFS. Validado en juego por el
  usuario (calidad excelente, rig 100%, voz/parpadeo ok).
- **El método AFS correcto es MID-INSERT** (bin crece en su slot, entradas
  posteriores desplazadas +delta, delta redondeado a 0x800). **El modo --append
  está DESCARTADO**: rompe el orden de la tabla AFS (entrada 327 apunta al final
  pero 328+ vuelven al medio) y el guest usa BÚSQUEDA BINARIA sobre la tabla →
  devuelve entradas equivocadas → crash host (0xC0000005) o cuelgue.
- **CONSTRAINT CRÍTICO**: el guest lee la entrada 327 (Krillin) con `to_read`
  FIJO = 106496 bytes (el slot original). El bin comprimido del mod DEBE caber
  en el slot (Goten = 107006 tolerado; port B1 = 271668 → truncado → cuelgue).
- **El port B1 HD→B3 HD requiere que el bin quepa en 106496 bytes comprimidos**:
  el AWO del B1 (685856 B) es 2.4x más grande que el B3 (290784 B) → hay que
  DECIMAR la geometría del B1 a ~290KB o el LZX truncado cuelga.
- **Janemba (IW→B3) = FRACASO DOCUMENTADO**: ver §11.1. Eliminado y archivado.
  La geometría quedó corrupta (masa deforme) y provocaba crasheos. NO reintentar
  hasta tener un conversor de formato completo validado.
- **MATRICES HD == PS2 (47/47, verificadas zona a zona)**: los esqueletos son
  el MISMO → ambos modelos están en el mismo espacio world. El emparejamiento
  por world coords (v6) da el MISMO resultado que por locales (v5) porque la
  transformación por hueso es casi rígida. El problema no es el emparejamiento
  sino la COBERTURA (660 slots sin PS2 + 877 con match malo).
- **v7 con umbral = EL MÉTODO**: reescribir solo slots con match world ≤0.3
  (197 de 1296) y dejar el resto HD original → elimina la deformación.
  Instalado como `krillin_ps2` (v6_u03). Ver SESION_2026-08-17.md §4.
- **FEEDBACK EN JUEGO (v7)**: cuerpo bien, fallan oreja/cabeza/boca/hombro
  der/cinturón (mezcla HD+PS2) y rodilla der/pie izq (viven en vb2, no
  tocable). El v7 es el MÁXIMO de la inyección en slots. Ver §65.1c.
- **LA VÍA REAL = RECONSTRUCCIÓN COMPLETA** (inspirada en docs B1): sec34 +
  IB + arms + **zona de submesh data** regenerados desde el PS2. La zona
  submesh EXISTE en el B3 (labels + `max N m`, 0x2D61-0x3471 AWG0) y su
  layout YA ESTÁ MAPEADO (ver `awo_tools/SUBMESH_DATA_B3.md`): descriptores
  de 0x60, rango A contiguo en +50/+54, rango B en +58/+5C (en B1 estaba en
  +60/+64/+68/+6C). Ver SESION_2026-08-17.md §6.
- **VB2 = BLOQUEADOR DE CARA/PIERNAS** (verificado): el vb2 (226 slots)
  cubre el 15.4% del IB (789/5140 índices = cabeza/caras). Layout propio
  `[x, y, z=1.0, 0, 0, ?, ?, nan@+28, nx, ny, nz]`, posiciones 0..2 (no
  world). La inyección solo toca sec34 → NO puede arreglar cabeza/rodilla/
  pie. La reconstrucción completa debe incluir vb2 + arms + submesh.
- **PRÓXIMO PASO REAL** (vía óptima): 1) swap nativo (hecho), 2) mapear
  arms + vb2 del B3, 3) adaptar amo0_to_awo.py del B1 al B3, 4) primer port
  real = Pikkon/Pan de IW, 5) automatizar. Ver docs/VIABILIDAD_MODELOS_EXTERNOS.md §9.
- **⚠️ CASO DE PRUEBA DESCARTADO (2026-08-17)**: Pikkon IW tiene esqueleto
  PKH distinto a KLL (58 bones con falda SKIRT, orden distinto) → NO es 1:1.
  El retargeting de pose complejo es donde Janemba fracasó. Para la
  reconstrucción hay que buscar un personaje con esqueleto 1:1 con un HD del
  B3 (p.ej. traje alternativo de un personaje existente). Ver §65.1c/§2.7.

### 3.2 🔴🔴 LAYOUT REAL DEL VÉRTICE B3 (2026-08-17, VERIFICADO EMPÍRICAMENTE)

**Layout REAL del vértice sec34 del B3** (stride 44, alineación +2, verificado
leyendo el bin real b327_hd.bin + goten_298.bin):

```
+0   0xFFFFFFFF (nan marker)
+4   u (float, 0.1-1.0)
+8   v (float)
+12  z_local (float)
+16  x_local (float)
+20  y_local (float)
+24  peso (float, 0.1-1.0)
+28  BONE (u32, 0-35)
+32  nrm.z (float)
+36  nrm.y negado (float)
+40  nrm.x (float)
```

**Verificaciones** (b327_hd.bin): 36 bones únicos 0-35 en +28, normales con
|mag|≈1 en 1000/1000 en +32/+36/+40, peso 0.1-1.0 en +24, `0xFFFFFFFF` en +0.

⚠️ **El item 44 (antiguo) tenía razón y la corrección de §11.1 estaba EQUIVOCADA**:
el bone va en **+28**, NO en +0x10. Las herramientas que escribían en +4/+16
(la zona de u/pos) producían la masa deforme de Janemba.

**Implicación**: `mezclar_ps2_hd_v5.py` usa este layout correcto. El bone index
del vértice HD es el índice de ZONA del AWO (= el hueso PS2 directo, ver §3.3).

### 3.3 MAPEO DE HUESOS B3 / B1 (2026-08-17)

**B3 Krillin (51 huesos, índice = label)**:
```
0=XKLL_BODY 1=KLL_WAIST 2=KLL_STMC 3=KLL_OBI 4-7=KLL_ROBI1-4 8-11=KLL_LOBI1-4
12=KLL_CHEST 13=KLL_LCHN 14=KLL_LARMROT 15=KLL_LARM1 16=KLL_LARM2
17=KLL_LHANDROT 18=KLL_L00_LHAND 19=XKLL_NLA 20=KLL_RCHN 21=KLL_RARMROT
22=KLL_RARM1 23=KLL_RARM2 24=KLL_RHANDROT 25=KLL_L00_RHAND 26=XKLL_NRA
27=KLL_NECK 28=KLL_HEAD 29-37=XKLL_M_* (cara) 38=KLL_LLEGROT 39=KLL_LLEG1
40=KLL_LLEG2 41=KLL_LFOOT1 42=KLL_LFOOT2 43=XKLL_NLF 44=KLL_RLEGROT
45=KLL_RLEG1 46=KLL_RLEG2 47=KLL_RFOOT1 48=KLL_RFOOT2 49=XKLL_NRF 50=XKLL_NW
```
El sec34 usa bones 0-35 (36 bones, sin piernas/rostro → esos van al vb2).

**B1 Krillin (52 huesos, ORDEN DISTINTO al B3)**: mismos labels pero OBI/ROBI/
LOBI desplazados al final (29-37) y CHEST=3. El mapeo B1→B3 debe ser POR LABEL
(no por índice). Herramienta: `analyze_awo_b1.py` (estructura AWO B1).

## 4. HISTORIAL DE TRABAJO

### 4.1 Fixes del runtime (aplicados a dbz3 Y dbz1)
- Cuelgue del launcher por `SDL_INIT_GAMEPAD` con RTSS/OBS → `input_backend=xinput`
  (mando por XInput nativo, SDL solo teclado/ratón) + init gamepad async
- Timeout en `CallInUIThreadSynchronous` (windowed_app_context.cpp)
- Optimización de fences Vulkan en `CheckSubmissionFenceAndDeviceLoss`
  (esperar solo 1 fence — sin mejora medible, se documentó)

### 4.2 Launcher
- Tabs: Video / Upscaling / Audio / Input / Mods / Model Swap / Dev
- Selector de región (us/eu) + overlay `active_region/`
- Selector de mods (`dbz3_enabled_mods`, `mods/<mod>/`)
- Selector de backend GPU (D3D12 / Vulkan experimental)
- Mods de archivo completo (og_music) y por entrada AFS
- **✅ SWAP INTERNO B3→B3** (2026-08-17, imita al hermano dbz1):
  - Pestaña **Model Swap**: catálogo B3 (`mod center hd/catalog_b3.cat`,
    183 personajes con bins) → seleccionar origen HD y slot destino → genera
    el mod swap nativo y lo activa.
  - `mod center hd/swap_b3.py`: extrae bin #AMB origen del `us/data_cmn.afs`,
    comprime LZX /N:2048 y lo instala como **OVERRIDE POR ENTRADA**
    (`mods/<name>/us/data_cmn.afs/<dest>/geom.bin`, ~100KB) + `manifest.txt`.
    El runtime (`AfsFindModOverride`) sirve ese archivo por entrada → mods
    pequeños y 2+ mods activos simultáneos (cada uno toca entradas distintas).
    **Restricción**: el bin comprimido debe caber en `to_read = ceil(slot/0x1000)`
    (sin mid-insert); si lo excede el script aborta con aviso (usar otro slot
    más grande o decimar). Los mods generados antes (AFS completo ~280MB,
    mid-insert) se migran automáticamente borrando el AFS completo viejo.
  - `mod center hd/texture_b3.py build`: igual (override por entrada desde el
    inicio). `tex_91` migrado 280MB → 118KB (2026-08-18).
  - **🔴 OVERRIDE POR ENTRADA = EL MÉTODO (2026-08-18)**: swap_b3.py y
    texture_b3.py ya NO reconstruyen el AFS completo (~280MB por mod). Generan
    `mods/<mod>/us/<afs>/<entry>/geom.bin` (~100KB), que el runtime
    (`AfsFindModOverride`, rexglue-sdk/src/filesystem/afs.cpp) sirve por
    entrada del AFS original. Ventajas: (a) cada mod pesa ~100KB en vez de
    ~280MB; (b) **2+ mods de modelo/textura activos simultáneos** (cada uno
    toca entradas distintas del mismo AFS); (c) `active_region` ya no monta
    AFS completos de mods (solo linkea los originales de `us/` vía hardlink,
    que NO duplican espacio físico). Restricción: el bin comprimido debe
    caber en `to_read = ceil(slot/0x1000)*0x1000` (el guest lee FIJO, sin
    mid-insert). Ejemplos reales: tex_91 (bin 91: slot 112458 → to_read
    114688, bin 108312 → padded), tex_ovr (bin 327: slot 105296 → to_read
    106496). Los mods viejos con AFS completo se migran automáticamente al
    regenerarlos (el script borra el AFS viejo antes de crear el árbol de
    override).
  - **🔴 FIX OFF-BY-ONE EN LA TABLA AFS (2026-08-18)**: los scripts
    (`texture_b3.py`, `swap_b3.py`) leían la tabla del AFS en **offset 0x10**
    cuando el runtime (rexglue-sdk/src/filesystem/afs.cpp) la lee en
    **offset 8** (magic "AFS"(3B)+pad(1B)+count(4B)=8B, luego (addr u32,
    size u32) ×8B). Ese desfase de 8B = 1 entrada hacía que `bin N` extrajera
    la entrada física N+1. Consecuencias: (a) tex_91 "bin 91" extraía el bin
    del Gero **traje alternativo** (runtime 92, 11 texturas, 799616 B) en vez
    del Gero base (runtime 91, 10 texturas, 733920 B) → se servía un bin de
    estructura distinta en el slot 91 → **crash al llegar a Dr. Gero**;
    (b) el padding se calculaba con el to_read del slot equivocado.
    **Fix**: `read_afs_index` y `build_afs` corregidos a `f.seek(8)` /
    `base = 8`. Verificado: script bin 91 = loc 0x122A000 (Gero base),
    bin 327 = loc 0x3AAE000 (Krillin), bin 298 = Goten. tex_91 regenerado
    desde el bin 91 correcto (10 texturas, 733920 B, to_read 114688) con las
    ediciones del usuario restauradas en tex0-9 (los PNG del outfit
    alternativo son compatibles). `sw_goten_nativo` (AFS completo) tenía la
    tabla bien alineada en offset 8 → no afectado.
  - **🔴 TABLA AFS VIRTUAL EN EL RUNTIME (2026-08-18) — override de bins > slot**:
    el override por entrada no podía servir bins cuyo LZX excediera el
    `to_read` del slot original (el guest aloca el buffer según el size de la
    tabla AFS → truncaba el LZX → cuelgue). Para autorizar bins más grandes
    (p.ej. Goten 107006 > slot Krillin 106496) sin reconstruir el AFS de
    293MB, se añadió al runtime una **tabla virtual**:
    - `AfsServeVirtualHeader` (rexglue-sdk/src/filesystem/afs.cpp): construye
      la cabecera+tabla AFS donde cada entrada con override reporta el
      **tamaño real del override file** (en vez del size original del slot),
      manteniendo los addr intactos.
    - `host_path_file.cpp::ReadSync`: al leer `data_cmn.afs`, intercepta las
      lecturas que caen en la región cabecera+tabla [0, 8+count*8) y sirve la
      tabla virtual → el guest aloca buffers más grandes (to_read del override,
      p.ej. 110592) y el override sirve el bin completo sin truncar.
    - Compilar: `cmake --build rexglue-sdk/out/build-win-vulkan --target rexruntime`
      y copiar `rexglue-sdk/out/win-amd64/rexruntime.dll` →
      `out/build/win-amd64-release/rexruntime.dll`.
    - **Mod de prueba `goten_override_test`**: Goten (bin 298) → slot Krillin
      (327) por override con el bin completo (110592 B = 107006 comprimido +
      padding), activado; `sw_goten_nativo` desactivado para aislar el test.
      PENDIENTE probar en juego si la geometría se inyecta bien.
    - **🔴🔴 TABLA AFS VIRTUAL NAIVE = ERROR, REVERTIDA (2026-08-18)**: inflar el
      size de las entradas con override manteniendo los addr (`AfsServeVirtualHeader`)
      rompe el arranque: **el guest recalcula los offsets de las entradas
      posteriores ACUMULANDO los sizes** → entradas altas (3983/3986) leídas en
      offset equivocado → CRASH 0xC0000005 (0x7ff65aa2d1fa, misma dir con
      cualquier mod) o CUELGUE. REVERTIDA por completo.
    - **🔴✅ MID-INSERT VIRTUAL (2026-08-18) — swaps en cualquier dirección**:
      la vía correcta es presentar al guest una **tabla AFS virtual CONSISTENTE**,
      replicando exactamente un rebuild con mid-insert:
      - `VirtualAfsLayout` (`AfsGetVirtualTable`): si un override excede el
        `to_read = ceil(size/0x1000)*0x1000` del slot, la entrada **crece in-place**
        al nuevo slot (alineado 0x800) y **todas las entradas posteriores se
        desplazan** por el delta acumulado (como un AFS reconstruido). Los addr
        VIRTUALES son consistentes: la entrada crece y las siguientes se mueven
        → el guest las encuentra correctamente.
      - `AfsTranslateOffset`: para las lecturas de datos, traduce el offset
        virtual → físico (resta el delta de la entrada) y sirve el override
        (bin completo) o lee del archivo físico en el offset traducido.
      - **Criterio de crecimiento CORRECTO**: solo crece si el override >
        `to_read` (lo que el guest ya aloca), NO si excede el slot físico.
        tex_91 (114688 = to_read 114688) NO crece; goten (110592 > 106496)
        crece +4096.
      - Resultado verificado (script): tex_91 delta 0, goten entrada 327 crece
        in-place (virt=phys), 328+ desplazadas +4096. El guest aloca to_read
        110592 para el 327 → sirve el bin completo de Goten (antes truncado a
        106496 → crash). **Compilado, DLL copiado (11183104 B)**.  - Pestaña **Mods** con formato del hermano: `mods/<name>/manifest.txt`
    (key=value: name/description/author/version/type/source/target),
    toggle por `.disabled` marker, edición inline del manifest en la UI
    (Título/Descripción/Autor/Version — 2026-08-17 añadido campo Titulo/name).
  - **60fps con debug**: Dev tab → "Show FPS counter" (`dbz3_show_fps`) →
    overlay in-game condicional (estilo dbz1 `DebugOverlayDialog`).
  - **🔴 .bmp de diagnóstico gateados por Dev mode (2026-08-19)**: el toggle
    "GPU diagnostic logging" (`dbz3_diag_logging`) propagaba `dbz1_diag_logging`
    al SDK, y el runtime escribía `frontbuf_*.bmp`/`black_*.bmp` (~31.5MB c/u)
    cada 60 frames aunque Dev mode estuviera OFF → se acumulaban ~840MB. Fix en
    `src/launcher/settings.cpp`: la propagación ahora es `DiagLogging() && DevMode()`
    (en `SetDiagLogging`, `SetDevMode` y `ApplyRuntimeSettingsToSdk`). Los dumps
    SOLO se generan con Dev mode + toggle ambos ON. Limpiados los 28 .bmp del
    build.
  - **Adaptado a cualquier Hz de monitor + anti-bloqueo** (2026-08-17):
    - `DetectRefreshRate()` (Win32 `EnumDisplaySettingsW`) detecta los Hz del
      monitor y los muestra en la Video tab (calculado UNA vez, no por frame).
    - `SafeFrameCap()` valida el cap (0=uncapped, mínimo 15, máx 1000).
    - **Enfoque FINAL = replicar dbz1 (que funciona)**: NO se fuerza el frame
      cap a 60 ni se clampa a divisor limpio. El cap del usuario se aplica
      VERBATIM (`dbz3_frame_cap`, 0=uncapped) tanto en el launcher como en el
      juego. Forzar cap 60 en el launcher y divisor limpio (55/48...) en el
      juego causaba: (a) el cuelgue del launcher a >60Hz, y (b) lag/judder en
      el juego a >60FPS.
    - **VRR configurable** (`dbz3_vrr`, checkbox en la Video tab, DEFAULT TRUE
      como el default del SDK/dbz1): activa `d3d12_allow_variable_refresh_rate_
      and_tearing` (swapchain con ALLOW_TEARING → Present(0) no espera vblank →
      fluido a cualquier Hz). Con VRR OFF sin tearing, Present(0) espera vblank
      y puede dar lag a alta frecuencia → por eso el default es TRUE.
    - **🔴 CAUSA RAÍZ del cuelgue del launcher a >60Hz (SDK presenter.cpp)**: el
      ImGuiDrawer pide repaint CONTINUO mientras hay un diálogo (el launcher).
      Con un frame_cap NO nulo, el frame_cap skip limitaba la PRESENTACIÓN pero
      los repaints seguían a la frecuencia del monitor → en un panel de
      120/144/165Hz el hilo UI se saturaba sin tiempo para procesar mensajes.
      El fix correcto fue dejar el frame_cap del launcher en 0/uncapped (como
      dbz1) para que no haya skip de repaint, y además:
    - **FIX adicional (SDK presenter.cpp, 21:43)**: `Presenter::WaitForUITickFromUIThread`
      ahora hace pacing por TIEMPO (sleep_until a 1/frame_cap) en lugar de
      depender del vblank DXGI, por si el usuario fija un frame_cap NO nulo en
      el launcher. Nuevo miembro `ui_tick_last_paint_time_`. Requiere recompilar
      rexruntime.dll y copiarlo al build del juego (hecho).
- Código nuevo: `src/mods.{h,cpp}` (gestor de mods con manifest),
  `src/launcher/mod_pipeline.{h,cpp}` (catálogo B3 + swap asíncrono).
- **Model swap con selector de AFS** (2026-08-17): `swap_b3.py` ya aceptaba
  `--afs <ruta>`; ahora el launcher expone un campo de ruta custom del
  `data_cmn.afs` (además de la auto-detección `us/data_cmn.afs`), por si el
  juego está en otro directorio. `ModPipeline::SetAfsPath()` + input en la
  pestaña Model Swap.
- **Mod de música**: el override de `adx_jpn.afs`, `adx_usa.afs`, `opening.sfd`
  y `Ending00.sfd` YA FUNCIONA — el mod `og_music` (`mods/og_music/<region>/`)
  los reemplaza por región y está validado end-to-end. Solo hay que colocar los
  archivos en `mods/<mod>/us/` o `mods/<mod>/eu/`.
- **🔴 UNIFICACIÓN DEL SISTEMA DE MODS (2026-08-17 21:49)**: había DOS sistemas
  de activación en conflicto. `PrepareRegionData`/`IsModEnabled` (settings.cpp)
  usaban el cvar `dbz3_enabled_mods`, mientras que la pestaña Mods del launcher
  (mods.cpp) usaba el marker `.disabled`. Si el usuario desactivaba un mod en el
  launcher (marcaba `.disabled`), el cvar no se actualizaba y `PrepareRegionData`
  SEGUÍA montándolo → el mod del experimento de Krillin (`krillin_rec`) quedaba
  activo aunque apareciera desactivado → Krillin corrupto "no dejaba ponerse
  sobre él". **Fix**: `IsModEnabled` (settings.cpp) ahora usa SOLO el marker
  `.disabled` (igual que el launcher). El cvar `dbz3_enabled_mods` queda como
  código muerto. `SetModEnabled` del launcher (dbz3::SetModEnabled) ya es la
  única vía.
- **🔴 RESTAURACIÓN DEL AFS DE KRILLIN (2026-08-17 21:49)**: el
  `active_region/us/data_cmn.afs` estaba MODIFICADO (MD5 0094BA98 vs original
  354615B5) por los experimentos de inyección de geometría de Krillin PS2/rec,
  a pesar de que todos los mods estaban `.disabled`. Restaurado copiando el
  `us/data_cmn.afs` original (354615B5) sobre el activo. Verificado: los 15
  archivos de `active_region/us/` coinciden con los de `us/`. Al pulsar Play,
  `PrepareRegionData` reconstruye `active_region` desde `us/` (limpio) + mods
  realmente habilitados (ninguno).

### 4.3 Modding (estado)
- **✅ MOD DE TEXTURAS B3 HD→B3 HD FUNCIONA (2026-08-17)**: nueva pestaña
  "Texturas" en el launcher + `mod center hd/texture_b3.py`:
  - **extract**: extrae el bin del personaje del data_cmn.afs, localiza el
    bloque #AZT, y vierte cada textura como **SOLO `.png` editable** en
    `mods/<mod>/textures/` (o la carpeta que elijas con `--dir`). El header
    DDS original (128B) de cada textura se guarda en `textures_meta.json`
    para reconstruir el DDS en el build — **el usuario NO necesita el .dds**.
    Krillin (bin 327) = 13 texturas DDS DXT3 (64x64 a 256x256).
  - **build**: re-codifica los PNG editados a DXT3 (encoder BC2 propio en
    numpy, MANTIENE el tamaño exacto w*h bytes), reconstruye el DDS completo
    (header original del meta + bitmap nuevo), reemplaza en el #AZT (el bin
    NO cambia de tamaño → el AWO queda intacto), recompila LZX /N:2048, rellena
    al slot y genera el mod AFS (mid-insert). Valida la ruta del PNG (fallback
    si el meta es viejo y no guarda header).
  - **Formato #AZT**: header (tex_am@+0x10, index_loc@+0x14), tabla de offsets
    en index_loc, cada textura: idx@+0, type@+4, w@+16, h@+18, data_off@+0x14
    (offset RELATIVO al AZT del header DDS de 128B + bitmap DXT3). El bitmap
    DXT3 = w*h bytes (BC2, 16B/bloque 4x4, mipmaps=0).
  - **Pillow lee DDS DXT3 directo** (decodifica a RGBA); mi encoder DXT3
    (dxt3 encoder en texture_b3.py) re-codifica al tamaño exacto.
  - **Combinable con swap de modelo**: el mod de texturas se aplica sobre el
    slot destino (swap) — la textura se asigna por mesh part/material, así que
    un personaje swapado con su propio bin lleva sus texturas; editando el mod
    de texturas del slot destino se cambian.
  - La UI: seleccionar personaje → "Extraer texturas a PNG" → "Abrir carpeta"
    → editar PNG → "Reconstruir mod con texturas editadas" → el mod se activa
    solo y se lista en la pestaña Mods.
  - **Carpeta de texturas editable (2026-08-17)**: la pestaña Texturas tiene
    un campo "Carpeta de texturas (PNG)" que se auto-rellena al extraer con la
    ruta por defecto (`mods/<mod>/textures`), y el usuario puede cambiarla a
    cualquier carpeta donde esté editando, con botón **"Examinar..."** que abre
    el diálogo nativo de Windows (IFileOpenDialog + FOS_PICKFOLDERS, helper
    `PickFolder()` en launcher_state.cpp). El botón "Reconstruir" se habilita
    SOLO si la carpeta configurada contiene `textures_meta.json` (fix del bug
    en que el botón quedaba desactivado si el nombre del mod no coincidía con
    la carpeta del extract). `texture_b3.py` acepta `--dir` en extract y build.
  - **🔴 Fix del escape de argumentos (2026-08-17)**: el error "El nombre de
    archivo, el nombre de directorio o la sintaxis de la etiqueta del volumen
    no son correctos" al pulsar "Extraer" era porque `RunAsync` (mod_pipeline.cpp)
    concatenaba los argumentos SIN comillas → las rutas con espacios del
    proyecto ("DBZ Budokai 3 HD Collection\...") se rompían en tokens en cmd.exe.
    Fix: `RunAsync` ahora envuelve en comillas cualquier argumento con espacios.
    Verificado: el script funciona con rutas con espacios entre comillas (exit 0).
  - **🔴🔴 CAUSA RAÍZ DEFINITIVA del error (2026-08-17)**: el mensaje "El nombre
    de archivo, el nombre de directorio o la sintaxis de la etiqueta del
    volumen no son correctos. [exit code 1]" al pulsar "Extraer"/"Reconstruir"
    era de **`_popen` de MSVC + cmd.exe**, NO de Python (el script ni siquiera
    arrancaba: no se generaba `texture_b3_error.log`). `_popen` pasa el comando
    a `cmd.exe /c`, y cmd.exe **falla al parsear comillas cuando el comando
    empieza con `"`** (p.ej. `"python" "script" ...`). Reproducido con un test
    C++ `_popen` (error exacto) vs el mismo comando con `subprocess` de Python
    (funciona, exit 0). **Fix**: `RunAsync` (mod_pipeline.cpp) ya NO usa
    `_popen`; usa **`CreateProcessW`** directamente (lanza python sin cmd.exe,
    redirige stdout+stderr a un pipe con `CreatePipe`+`ReadFile`). Verificado
    con un test CreateProcess: extract de texturas exit 0.
  - **Tambien robustecido (2026-08-17)**: `texture_b3.py` usa workdir FIJO del
    proyecto (`out/build/win-amd64-release/.tex_work`, no depende del `TEMP`
    del entorno) y pasa a xbcompress/xbdecompress un entorno con `TEMP`/`TMP`
    corregidos (`_clean_env`) — robustez extra frente a un TEMP invalido.
    `sanitize_name()` quita caracteres invalidos del nombre del mod; el launcher
    ignora `--dir` si la ruta contiene caracteres invalidos. `RunAsync` escribe
    el comando exacto en `pipeline_cmd.log` y el script imprime TRACEBACK
    COMPLETO (y lo vuelca a `texture_b3_error.log`).
  - **Tambien robustecido (2026-08-17)**: `sanitize_name()` en texture_b3.py
    quita caracteres invalidos de Windows del nombre del mod; el launcher
    ignora `--dir` si la ruta configurada contiene caracteres invalidos.
    `RunAsync` escribe el comando exacto en `pipeline_cmd.log` (diagnostico) y
    el script imprime TRACEBACK COMPLETO ante cualquier excepcion.
  - **Solo PNG (2026-08-17)**: el extract ya NO genera `.dds` — solo PNG
    editables. El header DDS original se guarda en el meta y el build
    reconstruye el DDS completo (header + bitmap) desde el PNG.
  - **Lista de texturas en la UI (2026-08-17)**: la pestaña Texturas lista los
    PNG extraídos de la carpeta activa (nombres de archivo en grilla) para
    identificar las texturas sin abrir el explorador. Los combos de origen/
    destino muestran el bin de cada personaje `[bin N]` para distinguir
    variantes del mismo personaje (p.ej. Dr. Gero bin 91 vs 92).
  - **Selector de slot destino (2026-08-17)**: la pestaña Texturas permite
    elegir en qué personaje/slot aplicar las texturas editadas (además del
    origen del que se extraen). `texture_b3.py build --slot <bin>` toma el bin
    del ORIGEN (con sus texturas editadas) y lo coloca en el slot DESTINO del
    AFS → compatible con swaps de modelo: extraes texturas de A, editas, y las
    aplicas al bin de B (que puede venir de un swap).
  - **⚠️ Bins sin #AZT (2026-08-17)**: algunos bins del catálogo (p.ej. Dr. Gero
    bin 92 "traje alternativo", 38048 B) NO son modelos completos — no tienen
    bloque #AZT de texturas propias (reutilizan las de otro bin/variante).
    El extract ahora avisa con mensaje claro en vez de un traceback con error
    de ruta de Windows. Verificar con: `texture_b3.py extract --bin <n>`.
- **Sistema de mods del runtime FUNCIONA**: reconstrucción AFS + LZX `/N:2048`
  + overlay. Validado end-to-end (og_music, sw_goten_nativo).
- **✅ SWAP NATIVO B3→B3 FUNCIONA** (sw_goten_nativo): bin #AMB completo
  (AWO+AZT) de Goten en el slot de Krillin. Ver §3.1.
- **Janemba (IW→B3) = FRACASO ELIMINADO** (ver §11.1). El formato IW PS2
  (#AMO0/#AMG LE) NO es compatible directamente con el HD 360 (#AWO BE);
  requiere conversor de formato completo validado, que aún no existe.
- **Verificado por instrumentación**: Krillin carga bins 327-329 (la HD usa
  la numeración de bins de la GH PS2).
- **AWO_FORMAT.md** documenta el formato completo (ver sección 5).

## 5. FORMATO DE ARCHIVOS — VER `AWO_FORMAT.md`

**Resumen**: AFS (bins comprimidos LZX `/N:32` con magic `0F F5 12 EE`) →
#AMB big-endian → #AWO (modelo 360) vs #AMO0/#AMG (modelo PS2 LE). La HD 360
usa la numeración de bins de la **data_cmn de la Greatest Hits PS2** (Krillin
327-329, verificado por instrumentación). El #AWO ES el mismo modelo PS2
**(51 huesos, 18 mesh-groups, 68 labels idénticos — NO hay re-rigging)** solo
que en big-endian con magics renombrados (#AMO0→#AWO, #AMG→#AWG, #AMT→#AZT)
y layout de mesh-groups distinto (tabla de offsets en 0x690 vs bloques
secuenciales). Conversión = endianness + renombrado + re-layout.
Ver el documento completo para el layout campo a campo.

### 5.1 Hallazgos del escaneo profundo (modding resources + update)
- AFL = registros fijos de **32 bytes** (no strings), índice AFL = bin AFS.
- Dos numeraciones: data_cmn (modelos, GH=HD) vs DATA_ENG región (select/menús).
- **Personajes IW exclusivos con bins reales** (verificados en
  `ps2_games\Infinite World (USA)\USR\DATA_CMN.AFS`): Janemba 541-544,
  Pikkon 583-586, Pan 566-569, Super 17 606-609, Super Baby Vegeta 678-681.
- **Moveset ports IW→B3 ya existen** (8): Janemba→Krillin, Pikkon→Raditz,
  Pan→Nappa, Super 17→A17, Super Baby→Kid Trunks, Goku GT→Teen Gohan,
  Saiyawoman→Kid Gohan, Future Gohan (Shin Budokai).
- SDBH WM = mina de modelos Xenoverse (EMD/ESK/EAN/EMB): Janemba (bcbjn),
  Pikkon (bcbjk/bcpkk), Super 17 (bcs17), Pan (bcpan), Super Baby (bcvby).
  Estilo chibi — evaluar. Ecosistema EMD↔FBX disponible (EmdFbx/FbxEmd).
- Textura HD = `#AZT ` (no #AMT). DDS_PNG.exe convierte DDS↔PNG.
- `Tail AMO`: modelo de cola custom — **NO mergear WAIST (crash)**.

### 5.2 Verificación binaria (fase 3 — RE del conversor)
- **Krillin bin 327 es EL MISMO modelo en PS2 GH y HD 360**: 51 huesos, 18
  AMG/AWG, 68 labels de hueso idénticos (KLL_*, XKLL_*). Verificado leyendo
  ambos bins directamente (`ps2_games\Budokai 3 Greatest Hits (USA)\USR\data_cmn.afs`
  sin comprimir vs `us\data_cmn.afs` LZX descomprimido).
- Layout HD: tabla de 18 offsets AMG en `+0x1C` (0x690) del header #AWO →
  apunta a bloques `#AWG` (header 0x40). PS2: bloques `#AMG` secuenciales.
- Formatos de vértice idénticos: B5 (`01 B5` LE / `00 00 01 B5` BE), B4, 90.
- El header del #AWG es más largo que el #AMG (base 0x40 vs 0x20) con más
  campos de offsets — se mapea campo a campo en la fase 3.
- Archivos temporales de trabajo en `%TEMP%\opencode\`: b327_ps2.bin (812KB
  #AMO0 LE), b327_hd.bin (682KB #AWO BE descomprimido), b327_hd.lzx.

## 6. COMANDOS ÚTILES

```powershell
# Compilar el juego (release)
cmake --build "out\build\win-amd64-release"

# Compilar el SDK (D3D12+Vulkan+FFX, clang, ninja)
cmake -G Ninja -S rexglue-sdk -B rexglue-sdk\out\build-win-vulkan `
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" `
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" `
  -DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe" `
  -DREXGLUE_ENABLE_FIDELITYFX=ON -DREXGLUE_USE_VULKAN=ON `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64-v3"

# Compresión LZX de bins 360 (formato del juego)
xbcompress /N:32 <src> <dst>   # comprimir
xbdecompress <src> <dst>       # descomprimir

# Extraer/descomprimir bins de los AFS (scripts de trabajo)
#   %TEMP%\opencode\ contiene b327_ps2.bin, b327_hd.bin, b327_hd.lzx
```

**Herramientas del XDK**: `mod center\Xbox 360 Compression - Decompression tool...\`

## 8. FASE 3 (EN CURSO) — CONVERSOR #AMO0 → #AWO

Objetivo: escribir el conversor PS2→360 y añadir personajes IW al recomp.
**Ver `awo_tools/CONSOLIDADO.md` para el resumen completo de todo lo aprendido.**

Plan validado (ver AWO_FORMAT.md sección 8 y awo_tools/RE_PROGRESO.md):

1. [x] Verificar que PS2 GH y HD comparten numeración (Krillin = bin 327)
2. [x] Confirmar mismo esqueleto (51 huesos / 18 AMG / 68 labels idénticos)
3. [x] Mapear campo a campo #AMO0/#AMG (PS2) vs #AWO/#AWG (HD)
4. [x] Layout AWO determinista: header + relaciones + tabla AMG + labels + AWGs + axes-array (+0x34)
5. [x] Mapear estructura interna del AWG (ejes 80B, mesh groups, mesh-ref blocks, VB/IB)
6. [x] Layout del vértice HD (stride 0x2C, alineado +2): VT + V.z + pos local hueso + VN(Y negada)
7. [x] Formato de textura #AZT RESUELTO (A3T Analyzer, 14 texturas Krillin)
8. [x] Las 4 causas raíz del crash corregidas (estructura mod, +0x34, índices, textura AZT)
9. [x] Conversor v4 (build_awo_v4.py): AWO+AZT, mismo tamaño, estructura correcta
10. [x] **TRANSFORMACIÓN DE SKINNING (build_awo_v5.py)**: rig PS2 parseado (3056
        entradas), mapeo offset→vértice resuelto, vértices convertidos al layout HD
        [nan, VT.v, VT.u, V.z, pos_local, weight, 0, VN.z, -VN.y, VN.x]
11. [ ] **Re-layout de buffers**: deduplicar vértices PS2 (4331→~2189 únicos),
        reconstruir IB, re-layout de los 2 buffers (+0x34 y +0x2C) del AWG0
12. [ ] Validar: convertir Krillin GH → cargar en HD y comparar render/bytes
13. [ ] Aplicar a personajes IW (Janemba 541-544, Pikkon 583-586, Pan, Super 17...)
14. [ ] Añadir personaje nuevo: slot + modelo + moveset (ports existentes) + voz
15. [x] Explorar bins vacíos del data_cmn para slots de personajes nuevos
16. [x] **CAUSAS RAÍZ #5-7 resueltas**: compresión /N:2048 (no /N:32), bin padded
        al slot (106496), y solo UN mod activo por bin (orden alfabético).
17. [x] **✅ HITO: MOD DE TEXTURA FUNCIONA** — Krillin muestra píxeles rojos en el
        dogi con el mod de textura (DDS DXT3, colores RGB565). Validado end-to-end:
        override por entrada + LZX /N:2048 + padding al slot + textura #AZT.
18. [x] **✅ HALLAZGO: LA GEOMETRÍA ES MODIFICABLE** — desplazando los vértices
        del buffer principal (sec34) en X, Krillin aparece deforme (altísimo),
        conservando cabeza y zapatos. El runtime renderiza cambios de geometría
        sin re-layout. Layout vértice HD: [nan, VT.v, VT.u, V.z, pos.x_local,
        pos.y_local, peso, 0, VN.z, -VN.y, VN.x] (stride 44, +16/+20 = pos local).
19. [ ] Conversión de geometría completa (skinning) para añadir personajes
20. [ ] Aplicar a personajes IW (Janemba 541-544, Pikkon 583-586...)
21. [x] **MODO ARCHIVO COMPLETO VALIDADO**: AFS reconstruido (bin 327 original,
        tabla recalculada) funciona perfectamente. Permite bins > slot (override
        por entrada limitado a 106496). Script: `awo_tools/build_big_amb.py`.
22. [x] **RE-LAYOUT DE BUFFERS (estructura mapeada + bugs corregidos)**:
        - Tabla AMG apunta al magic #AWG (NO +0x40) — los offsets internos del
          AWG (+0x2C vb2, +0x30 ib, +0x38 restart) están EN el magic.
        - 51 punteros de zona ejes en header AWO (+0x34,+0x54,... cada +0x20).
        - **BUG #1**: `AWG = awg0_off + 0x40` (posición equivocada) → crash.
        - **BUG #2**: loop de punteros de zona ejes barria la tabla AMG (0x690)
          y aplicaba delta doble a AWG16/17 (≥ axes_base) → null deref guest.
        - **BUG #3**: header AMB duplicado en el repack.
23. [ ] **RE-LAYOUT DE BUFFERS DESCARTADO (2026-08-14)**: el runtime espera que
        sec34_count/vb2_count (derivados de los offsets del AWG header) sean
        EXACTAMENTE los originales. Agrandar sec34 a solo 1957 (+1) ya CRASHEA
        en combate. El guest usa los conteos para validar los índices dibujados
        por los arms. **El re-layout de buffers es incompatible con el runtime.**
        Instrumentación completada (AFS327 READ): el bin se lee correctamente
        (130752 bytes), el crash es al procesar el modelo.
        **VIABLE**: mantener buffers HD del mismo tamaño + decimar geometría PS2
        a ≤2190 vértices + reconstruir IB (4329 índices PS2 caben en 5140 HD) +
        re-mapear arms. El skin PS2 en sitio (sin cambiar tamaño) NO crashea.
24. [x] **HALLAZGO (2026-08-14)**: el runtime es SENSIBLE a cambios en sec34
        (buffer principal) pero TOLERANTE a cambios en vb2 (buffer secundario).
        - vb2 +1 vértice: ENTRA EN COMBATE (lag, texturas parpadeando, mano
          derecha a veces faltante, pero NO crashea).
        - sec34 +1 vértice: CRASHEA en combate.
        RESUELTO: sec34+1 sin re-mapear el IB TAMBIÉN crashea (en preview,
        mismo Addr de parseo). Los conteos sec34/vb2 son FIJOS — el guest los
        usa para deserializar toda la estructura. NO se pueden agrandar buffers
        de un modelo HD existente. Para añadir personajes IW (todos con
        3600-5000 vértices, >2x los 2190 de Krillin) hay que construir el AWO HD
        desde cero con los conteos del personaje.
25. [x] **✅ HITO (2026-08-14): JANEMBA ENTRA SIN CRASH** — bin 327 con
        geometría de Janemba (sec34=2386 decimada, IB=8484) CARGA sin crash.
        El runtime acepta conteos variables (sec34 hasta 2277 validado en bin
        329). PERO el modelo se ve como **masa deforme**: los mesh-ref
        blocks/arms de Krillin dibujan los triángulos de Janemba con rangos
        del IB equivocados. Bloqueado en el re-rigging fino (mapear huesos
        JNB→KLL + transformar posiciones al espacio local de Krillin) y la
        reconstrucción del mesh group (arms con nuevos offsets del IB).
        Herramientas: convert_personaje.py, decimar.py, build_janemba2.py.
27. [x] **✅ HITO 2 (2026-08-14): JANEMBA ARRANCA Y ENTRA EN COMBATE** —
        v6 con conteos IDENTICOS a Krillin (sec34=1956, IB=5140) arranca y
        entra en combate. Lecciones:
        - v4 (sec34=2386, IB=8484, AWG0 crece 142320): entra al select pero
          CRASH en combate.
        - v5 (sec34=1313, IB=5100, AWG0 se encoge 88352): NO arranca.
        - **v6 (sec34=1956, IB=5140, AWG0 mantiene tamaño 116720): FUNCIONA.**
        El guest deserializa la estructura por los offsets del AWG header;
        el AWG0 NO puede encogerse (cuélga) ni crecer en exceso (crash
        combate). Rellenar sec34/IB a los conteos EXACTOS de Krillin es la
        clave.
        - **Re-mapear arms (v6r) CRASHEA**: cambiar los offsets de los
          shadows a rangos nuevos [0,1275,2550,3825,5100] rompe el arranque
          (crash 0x7ff6180cf202 al procesar el modelo).
        - **HALLAZGO: los offsets de los arms NO son rangos del IB a
          dibujar**. En Krillin ORIGINAL todos los 5140 índices están en
          [0,3904); los rangos [3904,4936) de los shadows estan VACIOS. El
          IB se dibuja completo; los offsets de los arms definen otra info
          (skinning de huesos, no que triangulos dibujar).
        - **La masa deforme del v6 es por RE-RIGGING**: los vértices de
          Janemba tienen posiciones locales por hueso (y=0.358, y=-8.706,
          y=-8.374...) skinneadas con HUESOS DE JANEMBA (JNB). El guest las
          interpreta con los HUESOS DE KRILLIN (KLL) del arm → posiciones
          mal interpretadas → masa deforme. Fix: mapear huesos JNB→KLL +
          transformar posiciones al espacio local de Krillin.
28. [x] **HALLAZGO COMUNIDAD (2026-08-14)**: los modelos IW→B3 PS2 YA EXISTEN en
        `modding resources\All Character Models from IW into AMB format\`
        (241 .amb: Janemba, Pikkon, Pan, Super 17...). Herramientas de la
        comunidad en `mod center\` (AMO Decompiler/Compiler, B3_IW Model
        Converter, Model Rig Toolset, OBJ to AMG, EMD to AMG). Dato clave:
        Krillin HD tiene ~50% de la geometría PS2 (3216→1713 tri, 4252→2182
        pos) → el HD decima a la mitad. El bloqueador: los mesh-ref blocks +
        arms de Krillin dibujan la geometría de Janemba con rangos del IB
        equivocados (masa deforme). build_janemba3.py intenta re-mapear arms
        pero tiene un bug de offsets relativos del arm_ptr (el mesh group del
        AWG0 reconstruido se re-ubica). Ver HALLAZGO_COMUNIDAD.md.
29. [x] **✅ PIPELINE DE INYECCIÓN VALIDADO (2026-08-14, sesión 3)**:
        - **El bin de Krillin que el guest lee es la e326 del AFS** (682528
          bytes, = `b327_hd.bin`), NO la e327 (624000). La numeración del
          AGENTS.md "bins 327-329" era 1-off por el índice de tabla A.
        - **AFS reconstruido (build_afs.py)**: método VALIDADO = e326 loc
          intacto, bin crece en su lugar, e327+ desplazadas por delta
          redondeado a 0x100, entradas vacías (loc=0) preservadas. Reproduce
          byte a byte `data_cmn_janemba.afs` que funciona.
        - **Pipeline de vértices CORRECTO**: `convert_personaje.py` (skinning
          PS2→posiciones locales) → `decimar.py` (voxel) → `build_janemba2.py`
          (empaquetar AMB). NO usar posiciones absolutas (valores enormes
          cuelgan el arranque).
        - **v6 = FUNCIONA**: conteos idénticos a Krillin (sec34=1956, IB=5140),
          AWG0 mantiene tamaño 116720. Entra en combate. Masa deforme por
          re-rigging (vértices JNB interpretados con huesos KLL).
        - **Próximo paso**: rig_mapeo.py — mapear huesos JNB→KLL por labels
          y transformar posiciones locales de Janemba al espacio de Krillin.
30. [ ] **RE-RIGGING JNB→KLL (analizado, sesión 3 final)**: Janemba v6 entra
        en combate pero masa deforme. Análisis: 46/64 huesos mapean 1:1 por
        labels; poses por índice NO coinciden (orden distinto); el eje de 80B
        NO tiene la matriz de pose (solo identidad + child/sibling/parent +
        sello); AMG header PS2: +0x10 bone_am, +0x14 axes, +0x18 mesh_groups,
        +0x1C labels_off. **2 caminos**: (a) copiar ejes de Janemba al bin v6
        (barato, probar primero — si el guest skinea con los ejes del bin);
        (b) re-rigging completo (matrices bind de ambos esqueletos +
        transformación de vértices). Ver CONSOLIDADO.md §13.5.15.
31. [x] **✅ DESCUBRIMIENTO CLAVE: EL VÉRTICE HD LLEVA EL BONE INDEX EN +28**.
        Inspirado por el port B3→B1 (INVESTIGACION_FORMATO_B1_HD.md §9 del
        proyecto hermano): el layout del vértice HD es `[flag(nan), u, v,
        pos.z_local, pos.x_local, pos.y_local, peso, BONE_INDEX(u32),
        normal.xyz]`. **build_vertex_hd escribía f32(0.0) en +28 → todos los
        vértices apuntaban al hueso 0 → masa deforme.** Fix: escribir bone_idx
        como u32 en +28.
32. [x] **✅ v7: RE-RIGGING JNB→KLL POR BONE INDEX FUNCIONA** — cuerpo de
        Janemba RECONOCIBLE en combate (antes masa informe). Mapeo:
        bone_jnb→label (AMG0, bone_idx*16) → label_kll (AWO+0x24, bone*2*16)
        → bone_kll. 24 directos + manual. v7 (sin mapeo→bone 0): FUNCIONA
        pero corrupto (dedos/caras a BODY). v8 (dedos→18/25, caras→36):
        CRASH. Estado: v7 instalado. Siguiente: refinar mapeo de dedos/caras.
        Herramienta: rig_mapeo.py. Ver CONSOLIDADO.md §13.5.16.
33. [x] **✅ EXPERIMENTO VALIDACIÓN: KRILLIN B3 PS2 → B3 HD (2026-08-14)**:
        el conversor #AMO0→#AWO FUNCIONA. Krillin del B3 PS2 renderiza en HD
        con silueta reconocible (no masa deforme). Pipeline: convert_personaje
        → build_ib_from_ps2 (dedup+IB) → build_janemba2 → build_afs. v2
        (1443 verts) CUELGA; v3 (1109 verts) arranca y Krillin se distingue
        pero corrupto. **Hallazgo: el HD usa sec34 (1956, skinned con bone
        index) + vb2 (226, SIN skinning, bone=0xFFFFFFFF, posiciones
        absolutas) para la cabeza/rostro. El IB HD referencia ambos (max
        índice 2189). Nuestro conversor solo llena sec34 → cabeza corrupta.**
        Siguiente paso: llenar vb2 con la cabeza/rostro. Herramienta nueva:
        build_ib_from_ps2.py. Ver CONSOLIDADO.md §13.5.17.
34. [ ] **🔴 EL MODELO HD DE KRILLIN ES DISTINTO AL PS2 (revisión 2026-08-14)**:
        el conversor NO es funcional (masa deforme). Hallazgo: el HD original
        (e326) tiene sec34 con SOLO bones 0-35 (skinned) + vb2 (226) con
        bone=0xFFFFFFFF (sin skin, posiciones absolutas) para cabeza/caras.
        **NO usa bones 36-50 skinned.** El PS2 skinnea piernas (38-48) →
        al ponerlas en sec34 el guest CUELGA (no tiene esas matrices). El
        conversor NO es mecánico — el HD es un re-trabajo (cuerpo skinned
        0-35 + cabeza vb2 sin skin). Fixes válidos: IB (index mismatch) y UV
        (orden u,v). Para conversor funcional: mapear piernas/cabeza a
        bones 0-35 o vb2. Janemba v7 funcionaba porque IW usa rig completo.
        Ver CONSOLIDADO.md §13.5.18.
35. [x] **🔴 VERDAD FUNDAMENTAL: EL PARSER PS2 NO LEE EL IB DE TRIÁNGULOS**:
        el pipeline de conversión NUNCA fue correcto. extract_geometry.py
        lee verts ÚNICOS por part pero NO el index buffer de triángulos.
        build_ib_from_ps2 genera triángulos asumiendo 3 verts por triángulo
        (incorrecto). El janemba_ib.bin de "v7 funcional" es un artefacto
        ([0,256,512,...], max 65294) — NO triangle list real; v7 funcionaba
        por accidente (el guest dibujó un patrón pseudo-aleatorio que parecía
        cuerpo). Ver CONSOLIDADO.md §13.5.19.
36. [x] **✅ RESUELTO: FORMATO DEL IB PS2 (MaxScript budokai_updated.ms)** —
        hallado en `modding resources update 2\`. El mesh part PS2 NO tiene
        IB explícito: son submeshes con header 0x20 (FaceType en +0x10,
        VertCount en +0x14) + N vértices de 48B. FaceType 1 = triangle strip
        (winding alternado), FaceType 0 = tripletes. Vértice PS2: 48B
        (pos+null+normal+null+uv+skip). Herramienta: `parse_ps2_mesh.py`
        (Krillin AMG0: 3990 verts, 2392 tris; total 9144 verts, 5182 tris).
        `build_hd_pipeline.py`: parse→skin→HD→decimar→IB. El v7 (1018 verts,
        1700 tris) CUELGA — vértices sin skin (30%) usan pos absolutas+bone 0.
        Pendiente: mapear vértices sin skin. Ver CONSOLIDADO.md §13.5.20 y
        `modding resources update 2\INFORME_modding_resources_update_2.md`.
37. [x] **🔴 ESTRUCTURA DEL MESH PART PS2 CONFIRMADA (sesión docs B1/mod center)**:
        el part PS2 = header 0xA0 (MeshType[8] + Ukw3 + Ukw4 + matriz 0x30 +
        unknown 0x50) + flag tamaño 0x600000XX en +0x90 (mesh_size = XX*16) +
        submeshes en +0xA0. **El stride del vértice lo da MeshType[1]** (primer
        byte del part): 0xB5/0xB6/0xF5 = 48B, 0xB4/0xA4 = 32B faciales,
        0x199 = 32B sin UV, 0x90 = 16B sombras. El `VertBufferLength` del
        submesh header (byte en +0x0E * 0x10) es el TAMAÑO del buffer para
        saltar al siguiente submesh, NO el stride. Verificado en Krillin AMG0:
        15 parts B5 + 4 parts B4, flag correcto en todas.
38. [x] **🔴 EL VB2 (BUFFER SECUNDARIO) DEL HD USA LAYOUT DISTINTO (verificado)**:
        vb2 = `[pos.x_abs, pos.y, pos.z, 0,0,0, peso=0, 0xFFFFFFFF, nx, ny, nz]`
        (stride 44, posiciones ABSOLUTAS, bone=0xFFFFFFFF = sin skin). El sec34
        usa `[nan, u, v, z_local, x_local, y_local, peso, BONE_u32, nz, -ny, nx]`
        (posiciones LOCALES skinned). **El HD separa los vértices en 2 buffers
        por diseño**: sec34 = skinned (bones 0-35), vb2 = estático (cabeza/
        caras/manos, posiciones absolutas del modelo). El doc del B1
        (INVESTIGACION_FORMATO_B1_HD.md §9) dice que B1 usa
        `[pos,w,bone,normal,FFFF,uv]` "mismo que B3" pero NO es cierto — el B3
        verificado empíricamente usa el layout de arriba. NO copiar el layout B1.
39. [x] **🔴 KRILLIN PS2→HD: QUÉ VA A CADA BUFFER (mapeo skin por part)**:
        AMG0 parts 0-12 (cuerpo: torso/brazos/piernas) tienen skin 100% →
        sec34 (con bones>35 como piernas 38-48 → el HD NO las skinnea, van a
        vb2). AMG0 parts 13-18 (0x335C0+: cara XKLL_L00_FACE=36, manos
        KLL_L00_LHAND=18 estáticas) + AMG1-17 = 0% skin → **vb2**. Los voffs
        del skin del AMG0 cubren SOLO el AMG0 (max 0x26C70 < AMG1@0x527D0).
        El skin AMG0 = 3056 entries, match 100% en parts 0-12 (voffs = offsets
        reales de vértices rel AMG, NO fórmula contigua — los headers 0x20 de
        submeshes rompen la contigüidad).
40. [ ] **PENDIENTE: TRANSFORMACIÓN DE PARTES ESTÁTICAS PS2→ABSOLUTO HD**:
        las parts estáticas PS2 (manos part13 bone=18, cara part18 bone=36)
        tienen posiciones en ESPACIO LOCAL DEL HUESO (mags 0.27-1.35), pero el
        vb2 del HD espera posiciones ABSOLUTAS del modelo (el cuerpo PS2 mags
        ~5, el HD ~1). Requiere transformar las coords locales por la matriz de
        pose del hueso (mismo problema de re-rigging de Janemba). El eje de 80B
        del AMG NO tiene la matriz (solo identidad+jerarquía+sello). Alternativa
        probada: sec34+bone del hueso (pero el HD no skinnea bones 36-50).
41. [ ] **PENDIENTE: DECIMACIÓN DEL VB2**: con el split sec34/vb2, el vb2 PS2
        (parts 13-18 + AMG1-17) queda en ~3000-7000 verts vs 226 del HD. El
        runtime tolera vb2 +1 (item 24) pero no miles. Decimar el vb2 fuerte
        (voxel) y/o usar modo archivo completo (item 21, build_big_amb.py).
        Estado v12: sec34=265 (rellenado a 1956) + vb2=3116 (truncado a 226,
        pierde cabeza) + AWG0 se encoge -0x30 → probable cuelgue.
42. [x] **HERRAMIENTAS DE LA COMUNIDAD REVISADAS (mod center)**:
        - `B3_IW Model Converter/amb_model.py` = SOLO empaqueta/desempaqueta AMB
          (no conversor de geometría). `Files/functions.py` lee #AMB: +0x20 tabla
          loc+size, AMO en +0x20.
        - `Model-Rig Extractor.py` (Model Rig Toolset V0.6) = documenta el rig
          PS2: AMG +0x10 bone_am, +0x14 axes_loc; ejes de 80B con +0x34 ptr arm;
          arm +8 rig_ptr; rig +12 chunk_amnt; chunks 32B [weight, vvn_am, vvn_loc,
          v_am, v_loc]; vvn entries 32B (coords + voff en +12), v entries 16B.
          **Los voffs del rig = offsets ABSOLUTOS de los vértices** (comparados
          contra mp_points). Confirma nuestro SkinData.
        - `B3-IW AMO Converter + Shadows` = conversor B3/IW→B1 (exe, @Scoops999).
        - `Model-Rig Remover.py`, `Bone Addition Tool`, `AMBStudio`, `AMO
          Decompiler/Compiler` = editores de rig/AMB de la comunidad.
43. [x] **DOCS INSTRUCTIVOS DEL B1 REVISADOS (dbz1, hermano)**:
        - `docs/INVESTIGACION_FORMATO_B1_HD.md`: B1 usa bins separados por tipo
          (#ACM rig + #AWO + #AZT, sin #AMB). Vertex layout B1 ≠ B3 (ver item
          38). **§10.8/10.9**: los mesh parts del B3 (AWG version 4, fijos 0x50)
          NO son compatibles con B1 (version 2, tamaño variable) y el ORDEN DE
          HUESOS difiere entre juegos → glitches de skinning. Fix propuesto:
          reordenar huesos por label + re-mapear bone indices de vértices
          (exactamente nuestro rig_mapeo JNB→KLL).
        - `docs/TUTORIALES_MODDING.md`: pipeline OBJ editing (AMG to OBJ V2 de
          Nelson + Blender), texturas AZT/A3T (Paint.NET + NVIDIA DDS plugin,
          BC2/DXT3), compresión X360 LZX, SLXS (MDB 0x60/CDB 0x174) para añadir
          personajes, IDs SLXS.
44. [x] **🔴🔴 HITOS DE LA SESIÓN PS2→HD (2026-08-14, análisis profundo)**:
        - **El bin visible de Krillin en el juego es la e326** del data_cmn.afs
          (682528 bytes descomp., = `b327_hd.bin`, md5 b04b0741c4, n_sec=1956,
          n_vb2=226, n_ib=5140). La e327 (624000, 1791/208/5004) es OTRO bin de
          Krillin (otro traje/select). El logging del runtime (host_path_file.cpp
          `entry_index == 327`) apunta a un bin distinto al visible.
        - **Los AFS de trabajo previos (`data_cmn_original_rebuilt.afs`, md5
          f7e53b99) NO son el AFS real del juego** (md5 354615b5). Reconstruir
          desde el AFS REAL de `us\data_cmn.afs`.
        - **Layouts de los 2 buffers del bin real e326 (b327_hd.bin)**:
          sec34 = `[nan, u, v, z_local, x_local, y_local, peso, BONE@+28, nz,
          -ny, nx]` (stride 44, align +2, bone u32 en +28, TODOS con nan en +0).
          vb2 = `[pos.x_abs, pos.y, pos.z, 0,0,0, 0, 0xFFFFFFFF@+28, nx, ny,
          nz]` (posiciones ABSOLUTAS, bone=0xFFFFFFFF, stride 44).
          ⚠️ El doc B1 §9 afirma "el B3 usa [pos,w,bone@+16,normal,FFFF,uv]"
          pero es INCORRECTO para el sec34 del B3 (verificado: bone@+16 da
          valores absurdos; bone@+28 da 0-35 coherentes). El B1 usa ese layout,
          el B3 NO. NO copiar el layout del B1 al B3.
        - **⚠️ El bin e327 (real_e327.bin, 624000) tiene OTRO layout de vb2**:
          `[pos.x=1.0, pos.y, pos.z, w@+12, bone@+16, normal@+20, float@+32,
          uv@+36]` (layout tipo B1, bone=0). NO confundir con el e326.
        - **Mapeo de huesos HD→PS2: HD bone = PS2 bone × 2** (labels en índices
          pares del AWO: 0=XKLL_BODY, 2=KLL_WAIST, 4=KLL_STMC... 50=KLL_L00_RHAND;
          impares = slots estructurales sin label). El sec34 HD usa bones 0-32.
        - **El HD de Krillin ES un modelo RE-TRABAJADO, NO una conversión 1:1**
          del PS2: 0% match de coordenadas locales, conteos por hueso distintos
          (HD bone 20 necesita 420 verts, PS2 solo da 4). No hay correspondencia
          vértice-a-vértice ni por hueso.
        - **🔴 EL RUNTIME DIBUJA POR MESH-REF BLOCKS + ARMS (IB NATIVO)**:
          reconstruir el IB rompe el render. Los tests v16-v20 (IB reconstruido)
          colgaban. Janemba v7 funcionó porque mantuvo IB+arms nativos y solo
          llenó los slots sec34 con datos (bone 0 mayoritario). Ver B1 §10.13.
        - **La vía viable**: mantener el bin e326 COMPLETO (IB/arms/vb2/AZT
          nativos) y SOLO reescribir posiciones de vértices del sec34 en sus
          slots, manteniendo bone indices. `mezclar_ps2_hd.py` implementa esto
          (1254 slots reescritos). PERO las coords PS2/HD no comparten escala
          (ratios 0.12-7.7 por hueso) → mezcla directa deforma. Requiere escala
          por hueso o transformación de pose.
        - **El sec34 HD está intercalado por bones (412 runs)**, no agrupado.
          El IB define el orden; no reordenar.
45. [x] **✅✅ HITO (2026-08-14): KRILLIN PS2→HD ENTRA EN COMBATE Y MUESTRA
        SILUETA** — vía validada: mantener el bin e326 COMPLETO (IB/arms/vb2/
        AZT nativos) y SOLO inyectar posiciones PS2 en los slots del sec34
        manteniendo bone indices (`mezclar_ps2_hd.py`, 1254 de 1956 slots).
        Resultado visual (usuario): silueta de Krillin reconocible, BIEN pie
        derecho, mano derecha, brazo izquierdo, frente+rostro superior, parte
        del torso; DEFORME en el resto; ojos con textura normal; combate fluido.
        **Lecciones**:
        - NO reconstruir el IB (rompe labels→arms→IB, cuelga). El runtime exige
          el bin coherente (PLAN_RELAYOUT B3→B1 §91-99: usar el AWO como
          plantilla completa, inyectar solo geometría).
        - Los cuelgues previos (v16-v24) eran por: AFS corrupto de sesiones
          previas (5.4M entradas vs 3990 reales), reconstruir IB/arms, relleno
          con bone equivocado. Con AFS real + bin intacto + solo posiciones → OK.
        - Las coords locales PS2 y HD NO comparten escala (ratios 0.01-177 por
          hueso). La deformación restante es por mezclar coords de sistemas
          distintos. Las partes que se ven bien son las que coinciden.
        - `pose_matrix.py` transforma coords locales PS2 → world absoluto del
          modelo (mags ~5 coherentes). Pendiente: transformar world→local HD
          (el HD no guarda pose → requiere RE adicional).
46. [x] **EXPERIMENTOS DE MEZCLA (mix1/mix2/mix3, 2026-08-14) — resultado**:
        - **mix1** (cercano por posición, sin escala): MEJOR estado. Silueta
          reconocible, pie der/mano der/brazo izq/frente+rostro bien; resto
          deforme pero estructurado.
        - **mix2** (escala por hueso = mag HD/mag PS2): las partes buenas siguen
          bien pero las malas se COMPRIMEN más (espaguetizado). La escala por
          hueso empeora: el HD es un re-trabajo con proporciones distintas.
        - **mix3** (orden secuencial del skin, sin escala): buenas siguen bien,
          malas más corruptas con vértices disfuncionales.
        - **CONCLUSIÓN**: el HD de Krillin es un RE-TRABAJO (0% match coords,
          conteos por hueso distintos). No hay mapeo mecánico PS2→HD posible
          para un modelo ya existente. Las partes que coinciden son las que
          comparten estructura.
        - **El caso Krillin cumplió su propósito**: validó el pipeline de
          instalación end-to-end (AFS real + e326 + inyección de posiciones en
          slots + manteniendo IB/arms/vb2/AZT nativos). El juego entra en
          combate sin crash con el mod activo.
        - **Para personajes NUEVOS (IW)**: usar la misma técnica (bin como
          plantilla + inyección), o construir el AWO desde cero con los conteos
          del personaje (Janemba v6 funcionaba). El re-trabajo del HD solo
          afecta a personajes que ya existen en HD.
        - Estado final instalado: mix1 (mejor). Herramientas: `mezclar_ps2_hd.py`
          (v1 cercano), `mezclar_ps2_hd_v2.py` (escala), `mezclar_ps2_hd_v3.py`
          (secuencial).
        - Documentación B1: `docs/PLAN_RELAYOUT_B3_B1.md` (§91-99: AWO como
          plantilla completa, inyectar solo geometría), `INVESTIGACION_FORMATO_
          B1_HD.md` §10.14-10.16 (sistema labels+ejes+arms+IB interconectado).
47. [x] **🔴 VEREDICTO FINAL KRILLIN PS2→HD (RE matemática)**: NO es viable
        reproducir el PS2 en el HD. Verificado por mínimos cuadrados: NO existe
        transformación rígida (R,t) que mapee las coords locales HD a las world
        PS2 del mismo hueso (error RMS 1.7, inaceptable). El HD de Krillin es un
        modelo DISTINTO (re-trabajado con 0% correspondencia de vértices). No es
        un problema de matriz de skinning — es que no son el mismo modelo.
        **Lo que SÍ se aprendió (válido para personajes IW)**:
        (1) pipeline de instalación validado (AFS real 3990 entradas + bin e326
        + inyección de posiciones en slots + IB/arms/vb2/AZT nativos → sin crash);
        (2) layouts de los 2 buffers del B3 confirmados; (3) los 241 modelos
        IW→B3 PS2 (`modding resources\All Character Models from IW into AMB
        format\`) tienen el esqueleto B3 (Janemba.amb = 48 huesos JNB) y son la
        fuente pura para personajes que NO existen en HD.
        **Próximo paso real**: construir el AWO HD desde cero con los conteos del
        personaje IW (Janemba v6 lo hizo funcionar). No hay re-trabajo HD que
        interfiera para personajes ausentes.
48. [x] **🔴🔴 CORRECCIÓN CRÍTICA: LAS MATRICES DE POSE HD = PS2 (51/51)**:
        la conclusión del item 47 ("el HD es un re-trabajo, no convertible") era
        PARCIALMENTE ERRÓNEA por un error de mapeo. Verificado:
        - La estructura del AWO HD: la tabla en +0x34 del header tiene 51
          entradas que apuntan a ZONAS de hueso, cada zona con `+4 = índice real
          del hueso` y `+8 = ptr a la matriz local (12 floats: quat+pos)`.
        - El hueso 0 (BODY) está en 0x42360 (fuera de la tabla).
        - **Las matrices locales HD (leídas por índice de zona) son IDÉNTICAS a
          las PS2 (51/51, quat+pos exactos).** El esqueleto es el MISMO.
        - Con la misma jerarquía (pose_matrix), las matrices WORLD también son
          idénticas (51/51). Ambos modelos están en la MISMA pose y escala.
        - Los vértices world HD y PS2 tienen rangos de magnitud MUY similares
          por hueso (ej. hueso1: HD=[1.08..1.87] PS2=[1.08..1.87]) pero NO
          coinciden vértice-a-vértice (0% match exacto).
        - **CONCLUSIÓN REVISADA**: el HD de Krillin comparte pose/escala/esqueleto
          con el PS2, pero la geometría es re-trabajada/decimada (menos vértices
          por hueso). Las coords locales PS2 están en el espacio correcto.
        - **Implicación para el conversor**: la técnica de inyección (mix1) era
          correcta en esencia. El refinamiento pendiente: mapear los vértices
          PS2 a los slots HD del MISMO hueso con la pose correcta (ya tengo las
          matrices world por hueso en ambos). `pose_matrix.py` necesita la lectura
          correcta de zonas del AWO HD (el AGENTS decía "el eje no tiene matriz"
          — es FALSO, está en +8 de la zona).
49. [x] **HALLAZGO CRÍTICO: MAPEO DE HUESOS HD = PS2 DIRECTO (no ×2)**:
        el sec34 HD usa bones 0-35 que apuntan DIRECTAMENTE a las zonas del AWO
        (índice +4 de cada zona), y las matrices HD[zona] == PS2[idx]. El
        mapeo correcto es `bone_HD = bone_PS2` (NO ×2). `mezclar_ps2_hd.py`
        corregido a mapeo directo. Los coords locales PS2 del hueso B se
        inyectan en los slots HD del hueso B y producen el world correcto.
50. [x] **ANÁLISIS EXHAUSTIVO DE LAS 4 CARPETAS DE MODDING (2026-08-14)**:
        - **MOD EJEMPLO** (`modding resources update 2\MOD EJEMPLO`): la
          comunidad convierte IW→B3 PS2 y publica AMBOS formatos por personaje
          (`B3\XXX.AMB` = #AMO0 LE PS2 + `IW\XXX.AMO/.AMT`). Ginyu Force
          completa (Android 19, Burter, Chiaotzu, Cui, Dodoria, Guldo, Jeice,
          Zarbon). Los .AMB son el MISMO formato PS2 que sabemos parsear.
        - **EMD to AMG v0.90** (mod center): convierte Xenoverse EMD→AMG PS2.
        - **Bin to OBJ V3** (Nelson) + AMO_S: pipeline de edición de mallas de
          la comunidad (exporta AMG→OBJ, edita, reimporta).
        - **DBZ B3 (X360) Lesson 1/2**: confirma compresión X360 LZX 512KB
          (/N:2048), y que la comunidad edita bins HD con 010 Editor + template
          B3_AMB (no convierte PS2→HD).
        - **CONCLUSIÓN**: la comunidad NO tiene conversor PS2→HD. Trabajan el
          formato HD directamente (010 Editor) o convierten IW→B3 PS2. El salto
          PS2→HD es nuestro problema. Los MOD EJEMPLO + los 241 modelos
          IW→B3 PS2 son la fuente para personajes nuevos.
        - **Discord**: la comunidad "Dragon Ball Z Budokai Modding Community"
          (discord.gg/qUcDxNj de Nexus) tiene #modding-ressources, #tool-uploads
          y la B3_AMB template para 010 Editor. Acceder podría dar la template
          definitiva de la estructura HD.
51. [x] **✅ ESCANEO DEL DISCORD DE LA COMUNIDAD (2026-08-14, con token del
        usuario)**: accedí a "Dragon Ball Z Budokai Modding Community" (id
        349593493791965194). Hallazgos:
        - **`.aerithdevs`** (usuario RE) desarrolla un **conversor de modelos
          externos a Budokai** (Java, Windows/Linux/Android) que está añadiendo
          "Porto BT3p to Budokai" con "creación de lista de huesos compatibles"
          — EXACTAMENTE nuestro problema. Documenta el header AWG:
          `Offset subs, size subs, flag, Offset name, offset materials, size
          materials, offset vertices, size vertices, offset faces, size faces,
          offset bones, size bones` + "el AWO contiene más offsets y counters".
          Compartió `port_test.zip` y JSONs de esqueleto (0001-0001.AMO._skel).
          Su herramienta no tiene link público (en desarrollo).
        - **`samueldoesstuff`** documenta el RIG HD: ID de hueso, inicio del
          rig, chunks, weight, puntos, sub-puntos, ubicación del vértice —
          confirma nuestro SkinData. Y el port de modelos: header del model
          part (`B5 01 00 00 BD 29 00 00`), textura, shader, normales.
        - **`Zero Devs' Tool` universal** (mega.nz/folder/wZlAiCBQ) — la
          herramienta de la comunidad para conversión de modelos (BT3p/PS2).
        - **`Goku_B3_PS3.zip`** — modelos de Goku del B3 PS3 (A3T, cercano al 360).
        - **CONFIRMACIÓN CLAVE**: samueldoesstuff: "todos los tipos de archivo
          parecen ser los mismos que B3, solo con alteraciones leves como AWO
          en vez de AMO" + .aerithdevs: "el AWO contiene más offsets y
          counters". **El formato 360 (AWO) es casi igual al PS2 (AMO)** —
          valida nuestro pipeline (re-layout, no formato distinto).
        - La comunidad NO documenta públicamente PS2→360 (el 360 es oscuro);
          trabajan PS2→PS2 (BT3p/B2/B1→B3) o el 360 con 010 Editor. El token
          del usuario quedó en `%TEMP%\opencode\discord_token.txt` para futuras
          sesiones.
52. [x] **✅ DESCARGAS DEL DISCORD (2026-08-14) → `modding resources discord\`**:
        ~250 recursos descargados (tutoriales, tools, research). Los clave:
        - **`B3_AMB_PS3.bt`** = template 010 Editor del AWO/AWG (VALIDA nuestro
          RE): AWO header (+0x10 bones, +0x14 ptrConnections, +0x18 numAWGs,
          +0x1C ptrAWGoffsets, +0x24 ptrBoneNames); AWG header (+0x10 numBones,
          +0x14 rigging_data_ptr, +0x24 unk_Count, +0x2C ptrVertexBlock=vb2,
          +0x30 VertexBlockSize, +0x34 ptrFaceData=sec34, +0x38 FaceDataSize);
          riggingData = quaternion+pos+scale; BoneNames[32]. La template llama
          vb2 "vertexBlock" y sec34 "faceData".
        - **`00000002-00000002-b3.AMO.json`** = formato INTERMEDIO de .aerithdevs:
          descompone el AMO B3 en texto ($AMO→$AMG→$grp→$sub) con vértices
          `$v:F[pos] $u:F[uv] $n:F[normal] $c:B[color]` + peso, header mesh part
          `I&[000001B5 000029BD tex shader 00050401]`, matriz `$mtx:F[quat+pos]`,
          sello eje `&6000020F`. 10 AMGs, 8256 verts. ES LA REFERENCIA ESTRUCTURAL
          MÁS COMPLETA DEL AMO B3.
        - **`0001-0001.AMO._skel-1.json`** = esqueleto exportado por .aerithdevs.
        - **`ZERODEV_tool_tutorial_edit_rig_data_animations_etc.zip`** y
          **`Zero_Devs_Tool_Axis_Data_Editing.rar`** = tutoriales del Zero Tool.
        - **`AMO_Model_Separator_v1.01.zip`**, **`Model-Rig_Extractor.py`** (v0.9),
          **`AMG_to_OBJ_V2.zip`**, **`Model_Part_Addition_Tool.zip`**.
        - **`Goku_B3_PS3.zip`** (no descargado, era duplicado de los que ya
          teníamos). El resto: texturas, listas de bins, audio, shaders.
53. [x] **🔴 RB2 (Raging Blast 2) = REFERENCIA DEL SKINNING SPIKE CHUNSOFT**
        (`C:\Users\javie\Desktop\PROYECTOS IA\Raging Blast 2\`):
        - Recompile PC del RB2 (2010) con el MISMO SDK ReXGlue/xenia/D3D12.
        - Modloader acepta ZPAK PS3 (STPZ→bloques 0LCS): _i.zpak=IORAM (malla+
          esqueleto), _s.zpak=SPR (sprites), _v.zpak=VRAM (texturas).
        - **RB2.exe contiene el parser STPK/IORAM/VRAM/SPR3/TX2D** (strings
          verificados): "could not read PS3 IORAM asset size", "SPRP contains
          no TX2D descriptors". Es la implementación de referencia del formato
          de personajes de Spike Chunsoft (misma empresa/SDK que Budokai).
        - Formato distinto al AWO (RB2 usa STPK/IORAM), pero el CONCEPTO de
          skinning (IORAM = malla+esqueleto con rig por hueso) es la referencia
          para entender el skinning HD de Budokai.
        - Modloader: mods en `Modloader/Characters/<id>/` con `mod.toml`
          (kind=costume, base_character_id, form_id). Adaptador IORAM/VRAM
          "not enabled yet" (fase actual).
        - **Estructura IORAM verificada**: STPZ → bloques 0LCS + sub-bloque
          STPK, con nombre `XXX_PS3.ioram`/`XXX_X360.ioram`. Cada personaje
          tiene variante X360 (mismo formato Spike). El IORAM (malla+esqueleto)
          es el concepto de skinning de Spike, referencia para entender el AWO.
        - Copiado a `modding resources discord\rb2_reference\` (exe + goku ZPAK
          + README modloader).
54. [x] **CARPETA `modding resources discord\` (2026-08-14)**: 381 archivos en
        research/ (245), tools/ (58), tutorials/ (78), rb2_reference/. No se
        duplican mod center/modding resources (script de descarga filtra
        duplicados e imágenes sueltas). Archivos clave:
        B3_AMB_PS3.bt (template 010 AWO/AWG), 00000002-00000002-b3.AMO.json
        (formato intermedio .aerithdevs, estructura AMO B3 completa en texto),
        0001-0001.AMO._skel-1.json (esqueleto), Model-Rig_Extractor.py (v0.9),
        AMO_Model_Separator, AMG_to_OBJ_V2, Model_Part_Addition_Tool,
        ZERODEV tutorials, Zero_Devs_Tool_Axis_Data_Editing, Goku_B3_PS3.zip.
        Se creó `LEEME_PARA_SESION_B1.md` en el proyecto hermano dbz1 para
        traspasar estos hallazgos a la sesión de Budokai 1.
55. [x] **FORMATO INTERMEDIO DE .AERITHDEVS (análisis de los JSON)**:
        los archivos `00000002-00000002-b3.AMO.json` y `0001-0001.AMO._skel-1.json`
        son la descomposición del AMO B3 en texto:
        `$AMO/$model` → `$AMG000` → `$data000` (huesos/labels) → `$grp00` →
        `$data00` (mesh part header) → `$sub00` (submeshes).
        Vértice: `<$dataNNN, peso, $v:F[pos] $u:F[uv] $n:F[normal] $c:B[color]>`.
        Header mesh part: `I&[000001B5 000029BD tex shader 00050401]`.
        Matriz: `$mtx:F[quat+pos]`, sello eje `&6000020F`.
        El `00000002` es el formato B3 PS2 (18 AMG en skel, 10 AMG en el otro),
        pesos por bone (1.0 mayoritario). **Valida nuestra estructura del
        vértice B3 PS2 (pos+normal+uv+peso+bone)** y el header del mesh part.
        .aerithdevs no publicó su herramienta (en desarrollo, Java).
56. [x] **✅ PIPELINE JANEMBA v10 (2026-08-14, sesión 4)**: construido desde
        `Janemba.amb` (IW→B3 PS2, `modding resources\All Character Models
        from IW into AMB format\`). El AMO0 = 48 huesos JNB, 17 AMGs,
        AMG0@0x8480 (21 parts), total 8943 verts / 4905 tris (parse_ps2_mesh).
        Skin: AMG0 parts 0-13 = 3788 skinned (100%), parts 14-20 + AMG1-16 =
        5155 estáticos (0% skin). Mapeo JNB→KLL = 24 directos por label
        (0→0, 2→1, 4→38, 6→39, ... piernas 38-49, 28→2, 30→12, 44→18, 46→27).
        - Pipeline v8/v9 (TEMP): parse_ps2_mesh → SkinData (coords locales +
          bone) → split sec34(skinned)/vb2(estático absoluto via world_mats) →
          dedup exacto por (bone, coords local 4dp) → re-mapeo JNB→KLL →
          poda tris a ≤1713 → vb2 decimado a ≤226 → build_janemba2 →
          build_afs (entrada 326 = bin visible de Krillin).
        - Resultado v10: sec34=1956 (484 reales + 1472 pad bone 0), vb2=226,
          IB=5140 (5139 + FFFF), **delta AWGs=0x0** (AWG0 mantiene tamaño),
          AMB 1074208B. AFS: e326 loc intacto + bin crece (delta +8192
          redondeado). **IMPORTANTE: el AFS espera el bin COMPRIMIDO LZX**
          (magic 0F F5 12 EE) — usar `xbcompress /N:2048` ANTES de build_afs
          (el AMB crudo 1074208B → LZX 113484B). Sin comprimir el juego
          falla al leer.
        - Instalado como mod `janemba_v10` (mods/janemba_v10/us/data_cmn.afs,
          activo en dbz3_user.toml `dbz3_enabled_mods = "janemba_v10"`).
        - **PENDIENTE PROBAR EN JUEGO**: arrancar + entrar en combate.
        Herramientas (TEMP): build_janemba_v8/v9/v10.py, analyze_skeleton.py,
        janemba_axes.py, janemba_skin.py, janemba_ranges.py, janemba_bone_map.py.
57. [x] **REFERENCIAS PARA SESIÓN B1**: se creó `modding resources
        discord\LEEME_PARA_SESION_B1.md` en el proyecto hermano dbz1 con los
        hallazgos del Discord (template B3_AMB_PS3, formato intermedio
        .aerithdevs, RB2, recursos, cómo usar la API de Discord con el token).
58. [x] **INVESTIGACIÓN RETARGETING (2026-08-14, web)**: para portar geometría
        entre juegos HD con esqueletos de pose/rotación distintos:
        - Algoritmo de retargeting world-space (retargeting-threejs/sketchpunk):
          `trgLocal = invBindTrgWorldParent * bindSrcWorldParent * srcLocal *
          invBindSrcWorld * bindTrgWorld`. Transfiere ROTACIÓN RELATIVA AL PADRE,
          funciona entre convenciones de eje distintas.
        - Para vértices (no animación): `local_trg = inv(mat_trg_bone) *
          mat_src_bone * local_src` (solo rotación R, sin traslación).
        - Claves: escala homogénea positiva, quaterniones para rotación,
          mapeo de huesos por label o por POSICIÓN de bind pose (Unigine
          SkeletonRetargeterTranslations empareja por bind pose translations).
        - **Diagnóstico B1→B3 (verificado)**: las direcciones de hueso difieren
          90-180° (STMC/CHEST 90°, LARM 180°...). NO es solo escala/posición,
          es convención de eje distinta → inyección directa (v17) o transformación
          naive (v16) producen shear/estirado.
        - **v18**: retargeting de rotación `local_trg = inv(R3)·R1·local_src`
          aplicado al port Krillin B1→B3. Instalado para probar.
59. [x] **DIAGNÓSTICO FINAL PORT B1→B3 (2026-08-14, investigación)**: el
        enfoque "inyección de coords en slots" NUNCA puede importar el modelo B1
        porque **el IB (topología) es de B3** — cambiar coords = deformar B3.
        El B1 SI logró port (Goku PS2→B1 HD) porque **reconstruyó el IB**
        (build_ib_from_ps2, doc ESTADO_PORT_GOKU_SS2 §1: "ya no usa la topología
        del Gero"). En B3, reconstruir IB crasheaba (v6r) porque los arms/mesh-ref
        del B3 son más estrictos que los del B1.
        **EL VERDADERO BLOQUEADOR (común a B1 y B3)**: el mapeo skin→malla del
        PS2 solo cubre 11-49% de los vértices (nuestro SkinData usa fórmula
        contigua pero los headers 0x20 entre submeshes rompen la contigüidad).
        **Solución en Model-Rig_Extractor v0.9** (líneas 314-460): el rig de cada
        bone tiene ch_loc/sb_loc (rel AMG) → bloques de 32B/16B con el OFFSET del
        vértice de la malla en +12. Al comparar contra los offsets reales de los
        vértices (mp_extract: `160 + 32*cur_block + i*type_amnt`), se obtiene
        (bone, weight, coords) por vértice → 100% del cuerpo.
        **Investigación web**: Bing/DDG/zenhax/Noesis no tienen plugins públicos
        de Budokai; la doc práctica es LOCAL (Discord). El retargeting real usa
        world-space con pose auxiliar (retargeting-threejs/sketchpunk) y mapeo
        por posición de bind pose (Unigine SkeletonRetargeterTranslations).
        **Herramientas de la comunidad para el port manual**: AMO Decompiler/
        Compiler, OBJ to AMG v0.92, AMG to OBJ V2 (Nelson), Model Rig Toolset,
        EMD to AMG, Model Merger (todas en mod center/).
60. [x] **VEREDICTO FINAL PORT KRILLIN B1→B3 (2026-08-14)**: NO es viable como
        conversión binaria. Resumen de experimentos:
        - **v16** (B1 PS2 + transformación naive local→world→local): shear
          (alargado) porque las ROTACIONES de hueso difieren 90-180° entre
          juegos (verificado: STMC/CHEST 90°, LARM 180°).
        - **v17** (B1 HD inyección directa coords): 90% slots pero estirado/
          deforme — los espacios locales de hueso difieren (bone 0: B1
          x[-0.97..2.37] vs B3 x[-0.38..0.55]).
        - **v18** (retargeting rotación inv(R3)·R1·local): mejora brazos/manos/
          frente pero sigue B3 (solo 49% slots).
        - **v19** (world-matching 100% slots): DEFORME + triángulos gigantes +
          espacios negros (world B1 ≠ espacio del slot B3).
        - **CAUSA RAÍZ de "siempre se ve B3"**: el IB (topología) es de B3 —
          cambiar coords locales sin cambiar IB deforma B3, no importa B1.
          Reconstruir IB en B3 crashea (v6r); el B1 sí lo tolera (Goku port).
        - **LO QUE SÍ FUNCIONÓ HISTÓRICAMENTE**: mix1 (Krillin B3 PS2→B3 HD,
          silueta reconocible) = inyección de coords en slots con MISMA pose
          (mismo juego). El port B1→B3 fracasa por pose/escala distinta.
        - **Conclusión**: el port entre juegos con poses distintas requiere
          retopología 3D manual (OBJ→AMG + Blender + Model Rig Toolset), la
          vía que usa la comunidad. Conversión binaria pura NO es viable.
61. [x] **🔴 REVISIÓN DE VIABILIDAD (2026-08-14) — EL PORT SÍ ES VIABLE**:
        la conclusión del item 60 era incorrecta por un error de enfoque:
        - **La comunidad porta SDBH WM/B1/B2/IW→B3 PS2 con éxito**. El formato
          EMD de SDBH WM usa el MISMO esqueleto que Budokai (verificado:
          `bc18gb00.esk` de Android 18 → labels waist/llegrot/stmc/chest/neck/
          head → mapeo directo a bones KLL, 28 bones).
        - **El error fue "inyección en slots"** (v15-v19): el IB (topología) es
          de B3 → cambiar coords deforma B3. **La comunidad construye el bin
          DESDE CERO** con su propio IB reconstruido (EMD to AMG / OBJ to AMG:
          templates de mesh part + face index + vértices expandidos 48B).
        - **El salto PS2→360 es un RE-LAYOUT** (endianness + magics renombrados
          + re-layout mesh groups), no un formato distinto (AWO_FORMAT.md).
        - **EmdFbx (LibXenoverse) FUNCIONA**: `emdfbx.exe` convierte EMD de
          SDBH WM → FBX binario (3.1MB, malla+esqueleto). En `modding
          resources\EmdFbx-and-FbxEmd-LibXenoverse`. Es el puente universal
          (Blender edita FBX → exporta OBJ/FBX).
        - **Herramientas de la comunidad primitivas**: EMD to AMG (15KB py),
          OBJ to AMG (10KB py) — scripts Python tkinter hardcodeados a PS2.
          Refactorizables para HD → carpeta **`mod center hd\`** creada con
          README.md + `emd_to_awo_hd.py` (v1: parseo ESK + mapeo bones KLL).
        - **Próximo paso real**: completar el pipeline EMD→FBX→OBJ→AWO HD
          (re-layout del AMG PS2 al AWG 360) usando la plantilla estructural
          de Krillin (header+zonas+mesh group+arms) con geometría reconstruida.
62. [x] **✅ PIPELINE EMD→FBX→JSON FUNCIONA (2026-08-14, sesión SDBH)**:
        - `emdfbx.exe -ExportAscii` (LibXenoverse) convierte EMD de SDBH WM →
          FBX binario + FBX ASCII (4.3MB). Verificado con Android 18
          (`bc18gb00_x18g_body.emd` + `.esk`).
        - **`mod center hd\fbx_ascii.py`**: parser de FBX ASCII completo.
          Extrae: meshes (verts/tris/nrm/uv), 69 clusters de skinning
          (Indexes+Weights por hueso), 90 bones con poses, conexiones.
          Android 18: 14 meshes, 1682 verts, 1851 tris.
        - **`mod center hd\emd_to_awo_hd.py`**: v1 — parseo ESK + mapeo de
          labels SDBH→KLL (28 bones: waist→1, llegrot→38, stmc→2, chest→12,
          neck→27, head→28, rhand→25).
        - `%TEMP%\opencode\sdbh_test\SDBH_body.json` = datos intermedios
          (meshes + clusters + bones) para el build del AWO HD.
        - **Próximo**: build_awo desde el JSON (re-layout AMG→AWG 360).
63. [x] **✅ BUILD AWO HD DESDE JSON (2026-08-14, sesión SDBH)**: pipeline
        completo EMD→FBX→JSON→AWO HD→mod funcionando:
        - **`fbx_ascii.py` v2**: guarda ids de meshes/clusters/objects +
          connections. El mapeo cluster→mesh se resuelve via conexiones
          OO (cluster→Skin→mesh). Los índices de cluster son LOCALES al mesh.
        - **`build_awo_from_json.py`**: para cada mesh, skin por vértice local
          (bone+weight del cluster), transforma world→local KLL (matrices de
          Krillin PS2), genera verts HD (stride 44) + IB.
        - **Android 18 de SDBH WM**: 14 meshes, 1682 verts, 1851 tris,
          **100% skinned** (con skinning) al espacio KLL.
        - Empaquetado con build_janemba2 (sec34=1956, IB=5140, delta=0),
          AFS con delta +23296 (bin 128KB > slot 105KB).
        - **Instalado como mod en slot Krillin** (e326).
        - **PENDIENTE PROBAR**: arrancar + entrar en combate.
        - Herramientas en `mod center hd\`: emd_to_awo_hd.py, fbx_ascii.py,
          build_awo_from_json.py, README.md.
64. [x] **VEREDICTO FINAL DEL PIPELINE SDBH→HD (2026-08-14)**: la extracción
        EMD→FBX→JSON→AWO funciona perfectamente, pero el port al B3 HD está
        bloqueado por el retargeting de pose:
        - **Inyección en slots (v3/v21)**: muestra la topología del ANFITRIÓN
          (Krillin), nunca el personaje nuevo. V21 rellenó 100% de slots pero
          sigue siendo Krillin deforme.
        - **Re-layout completo (v20)**: IB reconstruido + conteos distintos
          (sec34=1296≠1956, IB=5144≠5140) → CRASH. El guest B3 exige conteos
          fijos (sec34=1956, vb2=226, IB=5140).
        - **Causa raíz**: los esqueletos difieren en ROTACIÓN (SDBH vs KLL:
          cluster transforms de Android 18 tienen orientaciones distintas).
          El retargeting world→local requiere matrices world completas del
          esqueleto SDBH, que el FBX de EmdFbx no exporta como jerarquía
          acumulada (solo cluster transforms 4x4 con traslaciones del modelo
          chibi ~1.75 de alto).
        - **La comunidad resuelve esto con retopología 3D manual** (OBJ→AMG +
          Blender + Model Rig Toolset), NO conversión binaria. Confirmado
          repetidamente.
        - **Herramientas de extracción SDBH: FUNCIONALES** (fbx_ascii.py,
          emd_to_awo_hd.py). **Herramientas de empaquetado HD: necesitan el
          retargeting de pose** (inviable binariamente entre esqueletos con
          convención de eje distinta).
65. [x] **RETOPOLOGÍA 3D + HERRAMIENTAS DE LA COMUNIDAD (2026-08-14)**: la vía
        viable es la retopología manual (Blender). Pipeline validado:
        - `emdfbx.exe -ExportAscii` (LibXenoverse): EMD→FBX. Plugin Blender
          2.78 en `modding resources\EmdFbx-and-FbxEmd-LibXenoverse\
          FBXImporterExporterFromBlender2.78`.
        - `OBJ to AMG v0.92` (Nexus-sama, source code.zip): lee OBJ (V/VN/VT),
          expande vértices por triángulo (48B V+VN+VT), construye mesh parts
          PS2 con templates binarios. Es EL pipeline de retopología.
        - `Model Rig Toolset V0.6` (Source/): Model-Rig Extractor/Remover.
          Documenta el rig PS2 (ch_loc/sb_loc → offsets de vértices) → mapeo
          skin→malla 100%.
        - `Bone Addition Tool v1.02` (.py): añade huesos al AMO.
        - `Budokai Modding Tool V1.5` (discord tools): AMO_LGBT (merge de
          modelos), AMG_C (crear AMG), Axis Editor. Templates `b3_amg_*.bin`.
        - **Documentado**: `mod center hd\RETOPOLOGIA_3D.md` (pipeline completo
          EMD→FBX→Blender→OBJ→AMG→AMB→HD).
        - **Ingeniería inversa del formato 3D** (resumen consolidado):
          AWO HD = header 0x30 + AWG0 (sec34 stride 44 + vb2 + IB) + mesh
          group (mesh-ref blocks 13×0x50 + arms). Vértice B3 = `[nan,u,v,z,x,
          y,peso,bone@28,nz,-ny,nx]`. Conteos FIJOS (sec34=1956, vb2=226,
          IB=5140) — cambiarlos rompe el parseo. Los shadow arms (sello 0x204)
          definen límites del IB en bytes.
        - `docs/INVESTIGACION_MODDING_BUDOKAI.md`: ecosistema AFS/AMO/AMT/AMB,
          mapeo abreviatura→personaje.
        - `docs/PERSONAJES_BINS.md`: mapa completo bins B1→personajes.

**Datos de referencia** (en `%TEMP%\opencode\`):
- Krillin: b327_ps2.bin (812KB #AMO0 LE), b327_hd.bin (682KB #AWO BE)
- Cell: b146_ps2.bin, b146_hd.bin | Goku: b352_hd.bin
- Janemba IW: bin 541 = #AMO0 (48 huesos/17 AMG), bin 542 = #AMT

**Estructura HD clave** (offsets rel AWO):
- +0x1C = tabla de offsets AMG | +0x24 = labels huesos
- Cada #AWG: labels en 0x40, ejes en +0x14 (rel AWG), +0x30 = index buffer,
  +0x38 = restart FFFF | mesh group +0x28 = tabla mesh-ref blocks (0x50B cada)
- **CORRECTO (2026-08-14)**: la tabla AMG apunta al **magic #AWG** (0xD40 rel
  AWO), y los offsets internos del AWG (+0x2C vb2, +0x30 ib, +0x34 sec34,
  +0x38 restart) están EN el magic. El header AWO tiene 51 entradas de 0x20
  (una por hueso) con punteros a la zona de ejes (0x42360+) en +0x34,+0x54,...

**Herramientas**: `awo_tools/` (parse_model.py, analyze_awg.py, analyze_mesh.py,
trace_bone.py, RE_PROGRESO.md, build_big_amb.py, relayout_awg.py,
pose_matrix.py, mezclar_ps2_hd.py).

### 65.1 ✅ PORT B3 PS2→B3 HD — PIPELINE DE INYECCIÓN FUNCIONA (2026-08-17)

**Resultado en juego** (mod `krillin_ps2`, usuario): Krillin entra y combate,
silueta reconocible, pero **DEFORME**: solo se conservan bien las manos, el
brazo derecho y la parte superior del rostro. El resto está deforme pero la
silueta no se sale de márgenes horribles (mejor que Janemba-masa).

**Qué se hizo**:
1. **Layout REAL del vértice B3 descubierto** (ver §3.2): el bone va en **+28**
   (u32), no +0x10 como decían las herramientas viejas. Verificado en
   b327_hd.bin + goten_298.bin: 36 bones únicos 0-35, normales mag≈1, peso
   0.1-1.0, marker 0xFFFFFFFF en +0.
2. **AFS --append DESCARTADO**: rompe el orden de la tabla → el guest usa
   búsqueda binaria → crash host (0xC0000005, minidump analizado: RIP en
   dbz3.exe RVA 0xA0967A, `movzbl (%r9,%rax)` = lectura de memoria guest
   inválida). El método correcto es MID-INSERT con delta redondeado a 0x800.
3. **`mezclar_ps2_hd_v5.py`** (layout correcto): inyecta coords PS2→HD por
   hueso en los slots del sec34 (1296/1956 slots reescritos; 660 sin cubrir =
   bones >35 que el HD no skinnea). Mantiene IB/arms/vb2/AZT nativos → el bin
   mantiene tamaño (682528) → LZX 101052 < slot 106496 → mid-insert delta=0.
4. **B1→B3 analizado**: esqueleto B1 y B3 comparten labels KLL pero en ORDEN
   DISTINTO (B1: 52 huesos con OBI/ROBI/LOBI al final; B3: 51 intercalados).
   El AWO B1 (685856 B) es 2.4x el B3 (290784 B) → NO cabe comprimido en el
   slot sin decimar.

**Por qué deforma (análisis)**:
- La inyección "cercano por coords locales" empareja vértices PS2 con slots HD
  del mismo hueso por mínima distancia euclidiana. Las partes que coinciden
  (manos, brazo der, rostro sup) son las que tienen correspondencia de escala.
- El resto deforma porque el HD de Krillin es un RE-TRABAJO decimado (0% match
  de coords, conteos por hueso distintos, item 46-47). No hay mapeo mecánico
  PS2→HD para un modelo ya existente en HD.
- Los 660 slots sin cubrir (bones 36-50: piernas/rostro vb2) quedan con las
  posiciones HD originales → mezcla de dos sistemas de coords → deforme.

**Próximo paso real (siguiente sesión)**: en vez de "cercano por coords",
transformar las coords locales PS2→world PS2→local HD usando las matrices de
pose (idénticas 51/51) y buscar el slot HD del MISMO hueso cuyo world coincida.
O aceptar la deformación parcial y usar personajes que NO existen en HD (IW),
donde no hay re-trabajo HD que interfiera (construir el AWO desde cero con los
conteos del personaje).

### 65.1b 🔴 RESULTADO SESIÓN 2 (2026-08-17 tarde) — UMBRAL + CONCLUSIONES

**v6/v7 con umbral instalado como `krillin_ps2`** (emparejamiento world + solo
matches buenos): ver awo_tools/SESION_2026-08-17.md §4.

Hallazgos clave:
1. **World == Local dentro del mismo hueso** (47/47 matrices idénticas, la
   transformación por hueso es casi rígida). v6 == v5 byte a byte. El
   emparejamiento nunca fue el problema.
2. **El problema es la COBERTURA**: 660 slots (34%) sin coords PS2 (bones
   36-50, el HD no los skinnea en sec34) + 877 slots con match world >0.5.
   Solo ~197 slots (10%) tienen correspondencia world real entre PS2 y HD.
3. **Conclusión de viabilidad**: inyectar PS2 en un modelo HD existente NO
   puede mejorar mucho (el HD es un re-trabajo, 0% correspondencia de
   vértices). El resultado óptimo = HD original con ~200 retoques puntuales.
   La vía REAL para meter modelos externos = personajes que NO existen en HD
   (IW, Pikkon, Pan, Super 17), construyendo el AWO desde cero con los conteos
   del personaje (no hay re-trabajo HD que interfiera).

**Para el usuario**: probar el mod `krillin_ps2` (activo). Debería verse el
Krillin HD original (sin deformación) con pequeñas mejoras PS2 en las zonas
de buena correspondencia (manos, brazo der, rostro sup). Si se ve bien, el
pipeline de instalación está completo y la vía siguiente es un personaje
nuevo (IW) con su propio bin desde cero.

### 65.1d ✅ PIPELINE DE RECONSTRUCCIÓN COMPLETA B3 (2026-08-17 noche) — POR PROBAR

**Herramienta nueva**: `awo_tools/port_ps2_to_b3.py` — port PS2→B3 HD con IB
REAL (FaceType), adaptada del `amo0_to_awo.py` del B1 al layout B3:

- **parse_ps2_full**: parsa TODOS los AMGs con submeshes encadenados →
  verts + triángulos REALES + skin (rig → coords locales). Krillin: 43 parts,
  9304 verts, 5294 tris.
- **build_buffers**: sec34 (44B layout B3) + IB desde triángulos reales.
  Sin decimar: 5890 únicos / 15882 índices.
- **decimate**: voxel por (bone, celda) — fusiona vértices del MISMO hueso.
  → 734 verts / 4908 índices ≤ conteos de Krillin (1956/5140).
- **Re-empaquetado TAMANO FIJO (delta=0)**: mantiene la estructura del bin
  plantilla (b327_hd.bin) y rellena EN SU POSICIÓN sec34, IB, descriptores
  (rangos A/B uniformes, anchors en +0x18) y arms. El bin mantiene 682528 B
  → LZX 90850 < slot 105296 → AFS mid-insert sin desplazar.
- **Instalado como mod `krillin_rec`** (reemplaza krillin_ps2):
  `out\build\win-amd64-release\mods\krillin_rec\us\data_cmn.afs`,
  config `dbz3_enabled_mods = "krillin_rec"`.
- **⚠️ PENDIENTE PROBAR EN JUEGO**: si funciona, valida la reconstrucción
  completa (la vía real para personajes IW). Si crashea, los descriptores/
  arms regenerados apuntan a rangos que el guest no espera.
- Diferencias vs Janemba (fracaso): IB REAL + layout B3 correcto (bone@+28)
  + tamaño fijo (delta=0) + descriptores regenerados.

### 65.1e 🔴 VERIFICACIÓN DEL LAYOUT REAL DEL VÉRTICE B3 (empírico, 2026-08-17)

Verificado leyendo b327_hd.bin: el vértice sec34 empieza con
`0xFFFFFFFF` en +0, u en +4, v en +8, z_local en +12, x_local en +16,
y_local en +20, peso en +24, BONE en +28, nz en +32, -ny en +36, nx en +40.
Normal v0 real: (-0.99, 0.146, -0.0077) |n|≈1, bone 29 (facial). La normal
HD es [nz, -ny, nx] (y negada). Descriptores submesh B3: labels en +00,
"max N m" en +18 (NO +30 como B1), rangos A en +50/+54 y B en +58/+5C,
todos con <<8 y contiguos.

### 65.1f 🔴 ANÁLISIS DEL CASO DE PRUEBA (Pikkon IW) — DESCARTADO

**Resultado del v7 probado (usuario)**: el cuerpo se ve SORPRENDENTEMENTE
BIEN, pero fallan 7 zonas: oreja, cabeza trasera, boca, hombro der, cinturón,
rodilla der, pie izq. Análisis (ver SESION_2026-08-17.md §2.5):
- **Rodilla der (bones 44-46) y pie izq (41-42) = 0 slots en sec34** → viven
  en el **vb2** (posiciones absolutas, bone=0xFFFFFFFF). La inyección PS2 solo
  toca sec34 → esas zonas quedan 100% HD SIEMPRE. Inherente a la inyección.
- **Oreja/boca/cabeza** = cara en bones 29-37 + vb2 → casi nada se reescribe
  con umbral 0.3 → quedan HD original → mezcla visible con el cuerpo PS2.
- **El v7 (umbral 0.3) es el MÁXIMO de la inyección en slots** para un modelo
  HD existente. Para arreglar cara/piernas hay que reconstruir el bin completo.

**Insight clave del proyecto hermano (docs B1, leídos esta noche)**:
- El B1 validó que el runtime dibuja el bin #AWO completo tal cual (swap
  nativo). La inyección deforma porque el HD es re-topologizado.
- **La vía correcta para port PS2→HD = RECONSTRUIR el bin completo**: sec34
  (44B) + IB + arms + **zona de submesh data** regenerados desde el PS2.
- **La zona de submesh data EXISTE también en el B3** (verificado: labels
  XKLL_BODY/L00_LHAND/L00_RHAND/M_DTEETH/L00_FACE + strings `max N m` en
  0x2D61-0x3471 del AWG0). **LAYOUT MAPEADO**: descriptor 0x60 bytes, rango A
  contiguo en +50/+54, rango B en +58/+5C (en B1 estaba en +60/+64/+68/+6C).
  Ver `awo_tools/SUBMESH_DATA_B3.md`.
- Pipeline real para añadir personajes al B3: swap nativo (hecho) + port
  PS2→HD solo para personajes SIN versión HD (IW: Pikkon, Pan, Super 17),
  usando el bin HD del mismo esqueleto como plantilla estructural + geometría
  reconstruida + submesh data regenerada.
- Recursos del B1 reutilizables: `amo0_to_awo.py`, `obj_to_awg_hd.py`,
  `port_b3_to_b1_v2.py` (en `DBZ Budokai HD\mod center hd\conversores\`).
- Ver documento completo: `awo_tools/SESION_2026-08-17.md` §6.

### 65.2 ⚠️ AFS --append DESCARTADO (2026-08-17) — DETALLE DEL CRASH

- **Síntoma**: con `build_afs.py --append`, el juego CRASHEA (Goten) o se
  CUELGA (port B1) en la pantalla de carga. El control (Krillin original en
  append) cargaba por casualidad (contenido idéntico).
- **Causa raíz (minidump)**: el guest usa **búsqueda binaria** sobre la tabla
  de entradas del AFS (asume offsets crecientes). Append pone la entrada 327 en
  el final (0x117D4800) pero las 328+ vuelven al medio (0x3AC8000) → tabla
  desordenada → la búsqueda devuelve entradas equivocadas → el guest lee un
  byte de memoria guest inválida (`movzbl (%r9,%rax)` en dbz3.exe RVA
  0xA0967A, excepción 0xC0000005).
- **Verificado**: en el AFS original la tabla tiene 0 desordenadas; con append
  hay 1 (entrada 328). El mid-insert preserva el orden.
- **Decisión**: mid-insert es EL método. El bin debe caber en el slot
  (comprimido ≤106496) o el LZX se trunca. Para bins grandes, decimar la
  geometría (no append).

## 9. NOTAS IMPORTANTES

- El build del juego usa el SDK instalado en `rexglue/` (instalación local).
- El build Tracy (`win-amd64-tracy`) usa DLLs instrumentados (`rexruntimerd.dll`,
  `rexgpu-xenosrd.dll`, `TracyClientrd.dll`).
- Para profilar: `tracy-capture.exe -o out.tracy` mientras juegas, luego
  `tracy-csvexport.exe` para análisis de zonas.
- El usuario habla español. Sesiones largas de juego.

### 9.1 🔴 CARPETA `github/` — REPO DE SUBIDA (sync manual)

`github/` es la copia **versionable** del proyecto para subir a GitHub (NO es un
repo git local; se sube manualmente). El SDK (`rexglue-sdk/`) NO se sube: el
`.gitignore` lo excluye. **Los cambios del runtime se distribuyen como parches**
en `github/patches/`.

**Cómo sincronizar tras un cambio** (replicar desde la raíz respetando
`.gitignore`):

```powershell
# Copiar carpetas versionables (git ignora bins/lzx/afs/pyc/etc.)
# 1) src/  2) mod center hd/  3) awo_tools/  4) docs/  5) tools/  6) mods/
# 7) archivos raíz: AGENTS.md, AWO_FORMAT.md, CMakeLists.txt, CMakePresets.json,
#    dbz3_config.toml, dbz3_manifest.toml
# 8) parches del SDK: github/patches/rexglue-sdk/{include,src}/... (los 3 archivos)
```

Reglas clave:
- **No subir** archivos del juego: `*.xex`, `*.afs`, `*.bin`, `*.awo`, `*.amb`,
  `*.amo`, `*.amg`, `*.azt`, `*.dds`, `*.iso`, `*.png`, `*.bmp`, `*.log`.
- **`generated/`**: solo `README.md` (el código derivado del .xex no se sube).
- **`mods/`**: se distribuye vacía con `README.md` (los mods reales no se suben;
  contienen binarios de personajes).
- **`tools/`**: `xbcompress.exe`/`xbdecompress.exe` SÍ se suben (excepción en
  `.gitignore` `!tools/*.exe`).
- El **parche del runtime** (mid-insert virtual) vive en `patches/` con su
  `README.md` de cómo aplicarlo. Si se toca el SDK, ACTUALIZAR los 3 archivos
  en `patches/` y recompilar + copiar `rexruntime.dll` al build.

## 10. 🔴🔴 PIPELINE DE MODS CORRECTO (2026-08-14) — override por entrada

**Hallazgo que cambia la piedra angular de los mods** (inspirado en
`docs/PLAN_AFS_OUT_RE_COMPARATIVA.md` del B1):

El runtime tiene un hook `AfsFindModOverride` (rexglue-sdk/src/filesystem/afs.cpp)
que sirve archivos por ENTRADA del AFS **sin reempaquetar el AFS completo**:

```
mods/<mod>/us/<afs>/<entry_index>            ← archivo directo
mods/<mod>/us/<afs>/<entry_index>/<archivo>  ← carpeta con archivo dentro
```

### Los 3 fixes descubiertos (causa de los cuelgues históricos)
1. **El hook del B3 solo soportaba archivo directo**, no carpeta. El B1 ya lo
   soportaba (por eso el Gero/Piccolo del B1 funcionaron y aquí nunca).
   → Fix: portar el manejo de carpetas (iterar y usar el primer archivo regular).
   → El log verificado: `AFS OVERRIDE HIT (folder)`.
2. **Compresión**: el juego usa LZX `/N:2048` (NO `/N:32`). Con `/N:32` el bin
   excede el slot → el guest trunca el LZX → crash.
3. **Padding**: el bin del mod debe **paddearse al tamaño exacto del slot** que
   el guest lee (`to_read=106496` para la entrada 327). Si es más corto, el
   guest recibe menos bytes de los esperados → crash.

### Verificación en logs
```
AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```
> `got=106496` = bin completo servido. Si `got < to_read` → falta padding.

### La entrada correcta
- El runtime lee la tabla del AFS en **offset 8** (NO 0x10).
- Krillin visible = **entrada 327** (105296 bytes → padded 106496).
- Error histórico: editábamos la 326 (tabla@0x10), que es otro bin.
- **🔴 2026-08-18**: los scripts (`texture_b3.py`, `swap_b3.py`) también
  leían la tabla en 0x10 → desfase de 1 entrada (bin N = física N+1). Corregido
  a offset 8 (ver §4.2 "FIX OFF-BY-ONE"). Esto era la causa del crash de
  tex_91 (servía el bin 92 en el slot 91).

### Estado del model swap
- El override FUNCIONA (el bin se sirve íntegro).
- El guest **crash al procesar un bin de otro personaje** (Goten→Krillin,
  cuerpo de Goten inyectado). El contenido/estructura del bin aún no se acepta.
- La comunidad (LGBT Method, tutorial Añadir AMG) NUNCA reemplaza el bin
  completo — intercambia ejes/parts selectivamente.
- Próximo paso: RE del parser guest en `generated/dbz3_recomp.*.cpp`
  (crash addr `0x7ff7...`), instrumentar qué offsets lee del bin.

### Herramienta nueva
`awo_tools/analyze_bin_hd.py` — parser del bin HD con la template B3_AMB_PS3.bt
(offsets oficiales). Uso: `python analyze_bin_hd.py <bin> --dump`.

### Documentación
Todo documentado en `docs/` (índice: `docs/README.md`). Empezar por ahí.
- `modding resources update` es el buzón: el usuario pone archivos nuevos ahí
  para que los integremos a `mod center`/`modding resources`.

## 11. 🔴 FRACASOS DOCUMENTADOS

### 11.1 JANEMBA (IW→B3) — FRACASO, ELIMINADO Y ARCHIVADO (2026-08-17)

**Decisión del usuario**: eliminar todo el trabajo de Janemba. La geometría
quedó corrupta (masa deforme) y provocaba crasheos. NO reintentar.

**Qué se intentó** (sesiones 2026-08-14): port del modelo IW de Janemba
(bins 541-544, #AMO0/#AMG LE de PS2, 48 huesos JNB) al slot de Krillin (bin
HD #AWO BE de 360). Pipeline: parse_ps2_mesh → skin PS2→coords locales →
decimar (voxel) → build_janemba2 (conteos de Krillin: sec34=1956, IB=5140) →
build_afs (mid-insert).

**Hitos parciales** (documentados en CONSOLIDADO.md §13.5): v6 entró en
combate (masa deforme), v7 con bone index en +28 mostró cuerpo reconocible,
pero el IB reconstruido era un artefacto ([0,256,512,...] — NO triangle list
real) y el parser PS2 no lee el IB de triángulos → el "v7 funcional" dibujaba
un patrón pseudo-aleatorio que parecía cuerpo. La geometría nunca fue válida.

**Lecciones aprendidas (validan la vía de los ports reales)**:
1. El runtime exige el bin #AMB COMPLETO coherente (IB/arms/vb2/AZT nativos).
   Reconstruir el IB rompe el render → cuelgue. Inyectar solo posiciones en
   slots del sec34 manteniendo IB/arms nativos SÍ funciona (Krillin PS2→HD
   mostró silueta, combate fluido).
2. El bone index del vértice HD va en **+28** (u32) — layout REAL verificado en
   §3.2: `[0xFFFFFFFF, u, v, z_local, x_local, y_local, peso, BONE@+28, nrm.z,
   -nrm.y, nrm.x]` (stride 44, align +2). Las herramientas antiguas que
   escribían en +4/+16 (zona de u/pos) producían la masa deforme. Ver
   `mezclar_ps2_hd_v5.py` (usuario actual con el layout correcto).
3. Los conteos sec34/vb2/IB son FIJOS en el runtime — NO se pueden agrandar
   buffers de un modelo HD existente. Para personajes NUEVOS hay que construir
   el AWO desde cero con conteos del personaje (Janemba v6 funcionó en este
   aspecto: sec34=1956, IB=5140 con relleno).
4. La decimación (voxel, decimar.py) funciona para reducir geometría, pero el
   resultado depende de un IB real (parsear el IB PS2 con FaceType del
   MaxScript budokai_updated.ms, NO asumir tripletes).

**Archivado**: scripts de Janemba en `awo_tools/historial_fallidos/`. Mod
`janemba_v10` eliminado del build. Documentación histórica en
`awo_tools/CONSOLIDADO.md` §13.5 (sesión 3) — mantener como referencia de
aprendizaje, no reintentar.

## 12. 🔴🔴 SESIÓN 2026-08-18 — VÍA CORRECTA: BINS HD AUTOCONTENIDOS (RE)

**Decisión del usuario**: ABANDONAR la inyección de geometría PS2 en la
plantilla HD de Krillin (falla SIEMPRE, documentado en 65.1 y 11.1). La nueva
vía: entender cómo funcionan los **swaps nativos** y construir bins HD
**AUTOCONTENIDOS** (como Bulma/Babidi) re-layouteando el #AMO0 PS2 al #AWO HD.
Los mods de prueba de Krillin quedaron desactivados (Krillin limpio; solo
`tex_91` activo).

### 12.1 HALLAZGO CLAVE: EL BIN HD ES AUTOCONTENIDO

**El swap nativo HD→HD funciona** (usuario logró poner Bulma y Babidi en el
slot de Krillin). El bin HD lleva TODO el personaje (esqueleto, geometría,
texturas, estructura de dibujo) → el runtime lo acepta en cualquier slot.

Análisis comparativo de 3 bins HD (descomprimidos del `us/data_cmn.afs`):

| Personaje | bin | bones | AWGs | estructura |
|---|---|---|---|---|
| Krillin | 327 | 51 | 18 | AWG0 cuerpo + 17 AWGs manos/cara |
| Bulma | 110 | 43 | 2 | AWG0 + 1 AWG separado |
| Babidi | 96 | 41 | 1 | un solo AWG0 |

→ **El nº de AWGs y huesos VARÍA por personaje.** No hay estructura fija.
Cada bin HD es independiente. (Krillin es el caso MÁS complejo: 18 AWGs.
Bulma y Babidi son mucho más simples.)

### 12.2 PS2 (#AMO0) y HD (#AWO) COMPARTEN ESTRUCTURA (RE-LAYOUT)

Verificado comparando el `Janemba.amb` (IW→B3 PS2) con `b327_hd.bin`:
- **Los ejes de 80B son IDÉNTICOS** en ambos formatos (mismos sellos):
  eje 0 (body) = `0x6000020F`, sub-bones = `0x9000020C` / `0x9800020C` /
  `0x9800020E`. El esqueleto es el MISMO.
- AMG0 PS2 header: `+0x10 n_bones, +0x14 axes, +0x18 mesh_groups, +0x1C
  labels_off` (labels al FINAL del AMG).
- AWG0 HD header: `+0x10 n_bones, +0x14 axes, +0x18 groups, +0x1C 0x40,
  +0x20 mg_off, +0x24, +0x28 mg_size, +0x2C vb2, +0x30 ib, +0x34 sec34,
  +0x38 end, +0x3C` (labels DESPUÉS del header, buffers en el header).

### 12.3 DIAGNÓSTICO DEFINITIVO DEL FRACASO DE JANEMBA/KRILLIN

Ambos se intentaron **INYECTANDO** geometría PS2 en la **plantilla de
Krillin** (conteos fijos sec34=1956/IB=5140, descriptores/arms de Krillin).
Esto SIEMPRE falla con polígonos deformes (polígonos estirados hacia el suelo)
porque la estructura de dibujo HD (mesh parts + descriptores + arms) no
coincide con la geometría inyectada. Los descriptores del cuerpo (0-11) de
Krillin apuntan a vértices del sec34 hasta 4440, pero la geometría PS2
reconstruida tiene menos → OOB → deformación. Regenerar solo los descriptores
del cuerpo (dejando manos intactas) NO lo arregló.

**El layout del vértice B3 ES CORRECTO** (verificado: 1956/1956 markers
0xFFFFFFFF, bones 0-35, pesos 0-1, normales |mag|≈1). El script
`mod center hd/awg_to_obj.py` usa el layout del **B1** (incorrecto para B3):
`[pos@+0, weight@+12, BONE@+16, nrm@+20, uv@+40]`. **NO copiar el layout B1
al B3.** El layout B3 real: `[0xFFFFFFFF, u@+4, v@+8, z@+0xC, x@+0x10,
y@+0x14, peso@+0x18, BONE@+0x1C, nz@+0x20, -ny@+0x24, nx@+0x28]` (sec34 en
`sec_rel+2` por el align).

### 12.4 EL RIG PS2 (mapeo bone→vértice) — PIEZA CLAVE DEL CONVERSOR

Del `Model-Rig Extractor.py` v0.6 (SamuelDBZMAAM), la estructura del rig PS2
para asignar el bone correcto a cada vértice:
- Cada bone (eje) apunta a su arm: `bone_loc = AMG0 + 32 + i*80 + 52`.
- `rig_start` = `read(bone_loc + 8)`.
- En el rig: `rig + 12` = `chunk_amnt`.
- Cada chunk de 32B: `[weight, ch_len, ch_loc, sb_len, sb_loc]`.
- Los **chunks (ch_loc)** son bloques de 32B con el OFFSET del vértice en
  `+12`; los **sub-chunks (sb_loc)** bloques de 16B con el offset en `+12`.
- El mapeo: para cada vértice PS2 (con su offset absoluto), buscar en qué
  chunk de cada bone aparece → ese bone + peso. (Algoritmo en
  `Model-Rig Extractor.py` líneas 314-460.)

**🔴 DESFASE DEL RIG RESUELTO (2026-08-18)**: los offsets de vértice que
apuntan los chunks del rig son **RELATIVOS al AMG0**, NO absolutos. Hay que
sumar el offset del AMG0 (`amg_abs`) al offset leído del chunk
(`off = le32(bin, amg_abs + ch_loc + k*32 + 12) + amg_abs`). Sin el desfase
solo coinciden 1059/4651 vértices; con `delta = amg_abs` coinciden **3788/4651**
(el máximo, coincide con §56 "parts 0-13 = 3788 skinned"). Implementado en
`awo_tools/ps2_rig_skin.py`. Janemba AMG0: 4651 verts, 34 huesos skinned
(bones 1-46), 863 estáticos (vb2).

### 12.5 LA VÍA CORRECTA Y EL CONVERSOR UNIVERSAL

**Construir el bin HD de Janemba como bin AUTOCONTENIDO** (re-layout del
#AMO0 PS2 al #AWO HD), NO inyectar en la plantilla de Krillin.

Pipeline del conversor universal (análogo al `amg_c.py` de SamuelDBZMAAM,
que construye AMGs PS2 desde cero con templates):
1. Parsear el modelo fuente (PS2 #AMO0, OBJ, o cualquier formato→OBJ).
2. Construir los AWGs HD: header + labels + ejes (reusar los del PS2, mismos
   sellos) + mesh parts + descriptores + arms + buffers (sec34/vb2/IB).
3. Convertir geometría PS2 (48B, rig→bone) → HD (sec34 44B skinned + vb2 44B
   estático + IB).
4. Convertir texturas #AMT→#AZT.
5. Empaquetar #AMB + comprimir LZX /N:2048 + override por entrada
   (mid-insert virtual permite bins de cualquier tamaño).

**Herramientas de la comunidad estudiadas**: `Budokai-Modding-Tool` de
SamuelDBZMAAM (descargado a `%TEMP%\budokai-tool` o `C:\budokai-tool`):
`amg_c.py` (AMG Creator PS2: header + ejes + mp chunks + parts + end, LE),
`amo_a.py`, `amb_c.py`, plantillas `Files/AMG/b3_amg_*.bin`. El patrón de
construcción PS2 es la guía para el HD.

**PROGRESO DEL CONVERSOR (2026-08-18)**:
- `awo_tools/ps2_rig_skin.py`: **rig PS2 resuelto**. Parsea la geometría PS2
  (mesh parts + submeshes encadenados con stride correcto por vtype) y asigna
  (bone, peso) vía el rig (chunks/sub-chunks con offsets **relativos al AMG**,
  hay que sumar `amg_abs`). Janemba AMG0: 4651 verts, 34 huesos skinned
  (bones 1-46), 863 estáticos (vb2).
- `awo_tools/ps2_to_hd_geometry.py`: **geometría PS2→HD**. Convierte los
  vértices PS2 (coords locales + bone) a los buffers HD: sec34 (44B skinned,
  layout correcto `[FFFF,u,v,z,x,y,peso,BONE,nz,-ny,nx]`), vb2 (44B estático)
  e IB (u16 BE). Janemba: 3832 skinned → sec34, 950 estáticos → vb2, 4782
  índices. Las coords locales PS2 y HD son las mismas (mismo esqueleto/pose).
- **PENDIENTE**: construir la estructura de dibujo HD (mesh parts +
  descriptores + arms) para generar el bin `#AMB` autocontenido, y la
  conversión #AMT→#AZT de texturas.

**Documentación RE**: `awo_tools/RE_AWO_HD_CONVERSOR.md` (estructura HD
completa, hallazgos, pipeline del conversor).

### 12.6 PRÓXIMO PASO (en curso)

Construir el conversor PS2/OBJ→bin HD autocontenido para Janemba:
1. Parsear el rig PS2 → bone por vértice.
2. Convertir geometría PS2 (48B) → sec34/vb2/IB HD (44B).
3. Reconstruir la estructura de dibujo HD (mesh parts + descriptores + arms)
   coherente — el patrón de `amg_c.py` en HD.
4. Instalar como override (swap) en el slot 327 y probar.

## 13. 🔴🔴 SESIÓN 2026-08-19 — OJO FRESCO + HALLAZGO: MULTIPLES FORMATOS DE VÉRTICE

**Revisión externa del proyecto (perspectiva nueva) + herramienta de feedback rápido
+ hallazgo que cambia el diagnóstico del port PS2→B3.**

### 13.1 🔴 HALLAZGO CLAVE: EL B3 HD TIENE VARIOS FORMATOS DE VÉRTICE, NO UNO

**El RE se hizo casi entero sobre el bin MÁS RARO del juego (Krillin b327, 18 AWGs).
Otros bins nativos usan formatos de vértice DISTINTOS.** Verificado empíricamente
leyendo bins frescos del mismo `us/data_cmn.afs`:

| Personaje | bin | AWGs | bones | marker buffer principal | layout |
|---|---|---|---|---|---|
| Krillin | 327 | 18 | 51 | `0xFFFFFFFF` en +0 | **Formato A**: sec34 skinned `[FFFF,u,v,z,x,y,peso,BONE@28,nz,-ny,nx]` + vb2 estático (pos absolutas, bone=FFFF) |
| Bulma | 110 | 2 | 43 | sin FFFF | formato B (posiciones en +0, otro campo en +24/+28) |
| Babidi | 96 | 1 | 41 | `0x00090000` | formato C: posiciones en +0, sin marker FFFF, bones basura en +28 |
| Goten | 298 | 21 | 56 | `0x20353320` | formato C/B: mezcla, NaN en algunos campos |
| Cell F2 | 147 | 17 | 48 | FFFF en +0 | formato A (igual que Krillin) |

- El bin e327 (otro Krillin, 624000 B) ya documentaba un layout "tipo B1" distinto
  (item 44). → **El guest autodetecta el formato de cada bin. Cada bin es
  autocontenido (se confirma la tesis de §12).**
- **Implicación**: los swaps nativos que funcionan (Bulma/Babidi/Goten → slot
  Krillin) llevan formatos DISTINTOS al de Krillin → el guest acepta cualquier
  formato coherente. **El port de un personaje nuevo NO tiene que forzar el
  formato A de Krillin: se puede emitir en el formato de una plantilla simple
  probada en juego (Babidi/Bulma).**
- La "inyección en plantilla de Krillin" fallaba porque forzaba geometría PS2 a
  la topología/mesh-ref específicos de Krillin (formato A). Causa raíz ahora clara.

### 13.2 HERRAMIENTA NUEVA: `awo_tools/awg_to_obj_b3.py` (exportador OBJ autocontenido)

Exporta un bin HD (#AMB/#AWO) a OBJ **sin necesidad del PS2 de referencia**:
- Matrices world reconstruidas desde los ejes del propio bin: quat+pos local en
  los 12 primeros floats del eje (80B), **puntero padre en +0x40 (rel AWG0)**.
  Verificado: world HD-from-axes == world PS2 **51/51 coincidencia exacta** en
  Krillin. (Corrige la creencia del item 30/40 de que "el eje no tiene matriz".)
- Layout AWG0 verificado empíricamente (dirime la discrepancia con la template
  010 "PS3" del Discord, que usa offsets+size y da conteos absurdos):
  `+0x2C vb2_rel | +0x30 ib_rel | +0x34 sec34_rel | +0x38 end_rel` (offsets, no
  pares off+size). Los conteos salen exactos (Krillin 1956/226/5140).
- **`analyze_bin_hd.py` está DESACTUALIZADO**: usa el layout de la template 010
  (offsets+size en +28/+2C/+30/+34) que es INCORRECTO para X360 → da n_sec
  absurdos (233 en Krillin). Corregir o marcar como PS3-only.
- Uso: `python awo_tools\awg_to_obj_b3.py <bin> [out.obj] [--no-skin]`.

**Este exportador es el bucle de feedback que faltaba**: permite ver la geometría
de cualquier bin construido en 2 segundos sin abrir el juego (detectó al instante
la diferencia de formatos de Babidi/Goten).

### 13.3 DIAGNÓSTICO RÁPIDO DE UN BIN (bounds/finitud)

Script de análisis de los OBJ exportados: comprueba NaN/inf y bounding box.
Los personajes nativos válidos (Krillin, Cell F2) dan bounds humanos plausibles
(~7-25 unidades) y 0 NaN. Los bins construidos de Janemba
(`janemba_from_cell.amb`, `janemba_v2.amb`) exportan limpios: **4782 verts, 2100
tris, 0 NaN, bounds plausibles** → la geometría del port está BIEN FORMADA; lo
pendiente es si el guest la acepta.

### 13.4 MOD DE PRUEBA INSTALADO: `janemba_from_cell` (slot 327)

- Bin: `awo_tools/bins_trabajo/janemba_from_cell.amb` (796996 B, #AMB con AWO 340KB
  + AZT 375KB, 48 huesos JNB, sec34=3832 + vb2=950 + ib=6302).
- Construido con `build_from_template.py` usando **Cell Forma 2 (bin 147, 48 huesos)
  como plantilla estructural** (mismo nº de huesos que Janemba) + geometría real de
  Janemba (ps2_to_hd_geometry) + descriptores regenerados cubriendo toda la geometría.
- Instalado como override por entrada (LZX /N:2048 = 122472 B, pad a 122880):
  `mods/janemba_from_cell/us/data_cmn.afs/327/geom.bin` + `manifest.txt`.
- **ACTIVO** (sin `.disabled`). Único otro mod activo: `tex_91` (entrada 91, sin
  conflicto). `dbz3_enabled_mods` del toml sigue siendo código muerto (la activación
  real es por ausencia de `.disabled`).
- **⚠️ PENDIENTE PROBAR EN JUEGO**: arrancar → selector de personaje → Krillin
  (slot 327) → si muestra silueta de Janemba, la reconstrucción autocontenida
  está VALIDADA y el pipeline PS2→B3 queda abierto. Si crashea/cuelga, el guest
  rechaza los descriptores/arms de Cell con geometría de Janemba.
- El bin excede el to_read del slot (122472 > 106496) → ejercita además el
  **mid-insert virtual** del runtime (pendiente de validación en juego desde
  el 18/08 con goten_override_test).

### 13.5 VEREDICTO DE VIABILIDAD (perspectiva fresca)

- **El port PS2→B3 es VIABLE y está MÁS cerca de lo que sugiere el historial.** Cada
  fracaso previo (Janemba, Krillin PS2) fue un bug concreto identificado y resuelto
  por separado: IB falso (→ FaceType), bone en +28 (→ layout A), estructura incoherente
  (→ template con mismo nº de huesos + descriptores regenerados), formatos (→ hallazgo
  13.1). El B1 hermano YA completó un port PS2→HD completo (Goku) reconstruyendo el IB.
- **El bloqueo real no es la investigación sino el feedback**: cada test costaba
  abrir el juego. `awg_to_obj_b3.py` + el chequeo de bounds lo reducen a segundos.
- **Conclusión de estrategia**: no forzar el formato de Krillin. Emitir el personaje
  nuevo en el formato de una plantilla simple probada (Babidi/Bulma) o validar la
  plantilla Cell F2 (48 huesos) con Janemba. Próximo paso = probar `janemba_from_cell`.

### 13.6 🔴🔴 CAUSA RAÍZ DEL "KRILLIN SIN CAMBIO" (2026-08-19): DLL DEL RUNTIME STALE

**El mod `janemba_from_cell` se probó en juego (13:37, log dbz3_056) y Krillin salía
100% normal.** Tras análisis del runtime (no del bin — el LZX descomprime exacto al
.amb fuente), la causa NO era el mod sino la **DLL del runtime desactualizada**:

- El `out/build/win-amd64-release/rexruntime.dll` era la versión **vieja** (11155968 B,
  del 14/08): contiene `AfsFindModOverride` + los debug `AFS327 READ`/`AFS MOD READ`
  pero **NO** el mid-insert virtual (`AfsGetVirtualTable`/`AfsTranslateOffset` y los
  logs `AFS OVERRIDE LOOKUP/HIT/MISS` AUSENTES en la DLL).
- La DLL **correcta** (11183104 B, 18/08 12:29, con mid-insert virtual) existía en
  `rexglue-sdk/out/win-amd64/rexruntime.dll` pero **no se había copiado** al build:
  el build tenía la copia vieja de `rexglue/bin`.
- **Consecuencia**: el override del slot 327 (geom.bin 122880 B > to_read 106496)
  se rechazaba en silencio → el guest leía el Krillin original → "0 cambios".
  El sistema de override simple (bins ≤ to_read, p.ej. tex_91 114688 ≤ 114688) sí
  funcionaba, por eso tex_91 seguía OK.
- **Esto explica también por qué `goten_override_test` (110592 > 106496) nunca se
  validó**: el mid-insert virtual JAMÁS había corrido en juego (AGENTS lo marcaba
  "PENDIENTE probar en juego").
- **Fix aplicado**: `Copy-Item rexglue-sdk/out/win-amd64/rexruntime.dll →
  out/build/win-amd64-release/rexruntime.dll`. Verificado: la DLL del build ahora
  tiene los strings del mid-insert virtual y el tamaño 11183104.
- **⚠️ LECCIÓN**: cualquier cambio en el SDK (afs.cpp/host_path_file.cpp) exige
  recompilar `rexruntime` Y copiar la DLL al build — si se omite la copia, el build
  usa la versión instalada en `rexglue/bin` (stale) y los overrides grandes fallan
  EN SILENCIO (se ve el personaje original, sin crash ni aviso). Verificar siempre:
  `Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"` debe dar PRESENTE.
- **ESTADO**: `janemba_from_cell` sigue activo y la DLL correcta está en el build.
  PENDIENTE re-test en juego (debería mostrar Janemba con colores de Cell).

### 13.7 🔴✅ FORMATO C DEL AWG0 RESUELTO (Goku 264 / Vegeta 424) — 2026-08-19

**Objetivo de la sesión**: "Goku con armadura saiyan" (cuerpo de Vegeta armadura 424 +
cabeza de Goku). Para localizar la cabeza se necesitaba exportar ambos bins → se
descifró el FORMATO C (el de la mayoría de bins: Goku, Vegeta, Babidi, Goten, Krillin
armadura 329), distinto del formato A (Krillin 327, Cell F2 147).

**Layout FORMATO C del AWG0 (stride 44, SIN align +2)** — verificado en Goku y Vegeta:
```
+0  x | +4  y | +8  z  (posición EN ESPACIO LOCAL DEL HUESO, bounds [-1,1])
+12 0xFFFFFFFF (marcador u32)
+16 u | +20 v  (UV, 0-1)
+24 n.x | +28 n.y | +32 n.z  (normal local)
+36 weight | +40 BONE (u32)
```
El `+0x2C` NO es un offset de vb2: es el TAMAÑO en bytes del buffer (2455×44 en Goku).
El IB es **triangle STRIP** (índices consecutivos, winding alternado, triángulos
degenerados como saltos), NO lista, y NO hay restarts 0xFFFF (a diferencia del formato A).

**⚠️ CLAVE: la ubicación del buffer sec34 del AWG0 VARÍA por bin** (no siempre en
`sec_rel`). El exportador `awg0_export.py` PRUEBA 3 ubicaciones (sec_rel, sec_rel+2,
fin del mesh group = mg+mg_size) × 7 offsets de marker (0,2,12,16,24,38,40) y elige la
que da ≥50% de markers FFFF:
- **Goku 264**: sec34 en `sec_rel` (0x37B4), marker en +12, n_sec=2418
- **Vegeta 424**: sec34 en `fin_mg` (0x44E0), marker en +38/+40, n_sec=2656
- **Krillin 327** (formato A): sec34 en `sec_rel+2`, marker en +0, n_sec=2183
Bounds verificados: Goku [-1,1] 0 NaN, Vegeta [-1,1] 0 NaN, Krillin [-1.76,2.49] 0 NaN.

**Herramienta nueva**: `awo_tools/awg0_export.py` — exporta el AWG0 de cualquier bin
a OBJ autodetectando formato (A/C) y ubicación del buffer. `awg_parts2.py` — por-AWG.

**Mapa de AWGs (identificados por labels del mesh group)**:
- **Goku** (23 AWG): AWG0 cuerpo+XGOK_L00_S00_FACE+dientes; AWG1-8 GOK_L*_LHAND (dedos
  izq); AWG9-15 GOK_R*_RHAND (dedos der); **AWG16-22 XGOK_Lxx_S00_FACE (cara/cabello)**
- **Vegeta** (26 AWG): AWG0 cuerpo+XVGT_HAIR+XVGT_L00_S00_FACE+dientes+armadura(RWPAT/
  RSPAT/SCOUT/TAIL); AWG1-18 dedos; **AWG19-25 XVGT_Lxx_S00_FACE (cara/cabello)**

**Correspondencia de cara Goku↔Vegeta** (por label numérico): Goku L01,L18,L04,L05,L06,
L42 ↔ Vegeta L01,L18,L04,L05,L06,L44 (Goku L09/L00 ↔ Vegeta L00/L00_S09).

**Esqueletos desplazados** (mismo orden base, armadura de Vegeta inserta huesos):
Goku HEAD=36, JAW=38, RMOUTH=40, LMOUTH=42, DTEETH=44 | Vegeta HEAD=44, JAW=50,
RMOUTH=52, LMOUTH=54 (+ RWPAT=16/18, RSPAT=26/28, HAIR=48, FAC=46). Mapeo POR LABEL.

**Estado del swap de cabeza**: los AWG de cara (nb=1) usan un layout de offset aún sin
resolver (distinto del AWG0 y de los AWG de dedos). Cada tipo de AWG tiene offsets
propios. PENDIENTE: RE fino del layout de los AWG de cara para completar el swap.

**Verificación de la tesis 13.1 confirmada**: cada bin es autocontenido con SU PROPIO
formato (Goku marker+12, Vegeta marker+38/+40, Babidi+16, Goten+10, Krillin-arm+24).
El guest autodetecta el formato de cada bin. Krillin 329 (armadura) usa formato C,
Krillin 327 formato A — el mismo personaje tiene bins en ambos formatos.

### 13.8 MOD DE PRUEBA: `sw_vegeta424` (validar formato C en runtime) — 2026-08-19

- Objetivo: confirmar que el runtime acepta un bin en **formato C** (Vegeta armadura
  424) vía swap bin-completo, antes de invertir en la mezcla fina de cabezas.
- Bin: `awo_tools/bins_trabajo/vegeta_424.bin` (#AMB, 894528 B, formato C, 26 AWG,
  55 huesos, sec34=2656). Comprimido LZX /N:2048 = 126030 B, pad a 126976 (0x1F000).
- Instalado: `mods/sw_vegeta424/us/data_cmn.afs/327/geom.bin` + manifest. **ACTIVO**
  (sin `.disabled`). `janemba_from_cell` **desactivado** (también tocaba la 327, se
  aisló para este test). Único otro mod activo: `tex_91` (entrada 91, sin conflicto).
- El bin excede el to_read del slot (126976 > 106496) → ejercita el mid-insert virtual.
- **⚠️ PENDIENTE PROBAR EN JUEGO**: selector → Krillin (slot 327) → ¿aparece Vegeta
  con armadura saiyan? Si SÍ, el formato C queda validado en runtime y la base para
  la mezcla de cabezas está confirmada. Si crashea, el runtime no acepta este bin C
  completo y hay que revisar.
- DLL correcta (11183104 B, con mid-insert virtual) VERIFICADA presente en el build
  tras el fix de 13.6.
- **✅ VALIDADO EN JUEGO (usuario)**: `sw_vegeta424` muestra **Vegeta con armadura
  saiyan en el slot de Krillin**. El formato C + el mid-insert virtual funcionan en
  runtime. Base confirmada para la mezcla de cabezas.

### 13.9 🔴✅ LAYOUT DE LOS AWG DE CARA/MANO (nb=1) RESUELTO — 2026-08-19

**El swap de cabeza requiere exportar los AWG de cara. Se descifró el layout de los
AWG con n_bones=1 (cara/cabello/manos)**, distinto del AWG0 (formato C) y del formato A.

**Layout vertice AWG de cara (44B, marker 0xFFFFFFFF en +0)**:
```
+0  u32 0xFFFFFFFF (marcador)
+4  f32 u | +8  f32 v        (UV, 0-1)
+12 f32 x | +16 f32 y | +20 f32 z  (posicion en espacio LOCAL del hueso cabeza)
+24 f32 weight (=1.0) | +28 f32 0.0 (pad)
+32 f32 nx | +36 f32 ny | +40 f32 nz  (normal unitaria)
```
Verificado: producto punto normal·(pos−centroide) positivo en 100% → esta es la
interpretacion unica (posiciones +12/+16/+20, normal +32/+36/+40).

**Estructura del AWG de cara (offsets rel header #AWG, h)** — los campos significan
OTRA cosa que en el AWG0:
```
+0x10 n_bones (=1) | +0x2C = TAMANO del buffer de vertices (n*44)
+0x30 ib_rel = offset IB | +0x34 sec_rel = TAMANO del IB en bytes
+0x38 end_rel | Descriptor en h+0x180: +0x1C = n_verts, +0x24 = n_tris
Buffer de vertices SIEMPRE en h+0x1F0.
```
⚠️ En los AWG de cara, `sec_rel` (+0x34) NO es offset: es el TAMAÑO del index buffer.
El buffer de vertices NO está en sec_rel sino en **h+0x1F0** fijo.
El IB es **lista de triangulos** (cada 3 indices = 1 triangulo, quad-strip), no strip
con winding alternado. El misterio del "IB max > n_sec" era un calculo sin sentido
(usaba sec_rel como offset).

**Herramienta nueva**: `awo_tools/awg_cara_export.py` — exporta un AWG de cara a OBJ.
Uso: `python awg_cara_export.py <bin> <awg_index> [out.obj]`. Verificado: Goku AWG16
101 verts/148 tris, Vegeta AWG25 120 verts/150 tris, todos 0 NaN, bounds plausibles.

**CLAVE PARA EL SWAP DE CABEZA**: las piezas de cara de Goku y Vegeta comparten el
espacio local del hueso cabeza (bounds casi identicos: Goku y[0..1.52] z[0..1.25],
Vegeta y[0..1.62] z[0..1.23]) → la geometria se copia 1:1 entre bins sin
transformacion.

**Mapa de AWG de cara confirmado**: Goku AWG16-22 = XGOK_L01/L18/L09/L04/L05/L06/L42
_S00_FACE ↔ Vegeta AWG19-25 = XVGT_L01/L18/L00_S09/L04/L05/L06/L44_S00_FACE.
Correspondencia por label numerico (L01↔L01, L04, L05, L06, L18, L42↔L44).

**PRÓXIMO PASO (swap de cabeza)**: script que tome el bin de Vegeta armadura (base,
funciona) y reemplace la geometria de sus AWG de cara (19-25 + face/dientes/HAIR del
AWG0) por la de los AWG de cara de Goku (16-22 + face/dientes del AWG0), por
correspondencia de label, manteniendo los huesos/offsets de Vegeta.

### 13.10 🔴✅ SWAP DE CABEZA GOKU→VEGETA FUNCIONA (reconstruccion de bloque) — 2026-08-19

**Script nuevo**: `awo_tools/swap_cabeza.py`. Toma el bin de Vegeta armadura (424) y
reemplaza su bloque de AWG de cara (AWG19-25, XVGT_Lxx_S00_FACE) por el de Goku
(AWG16-22, XGOK_Lxx_S00_FACE), por correspondencia de label numerico. Mantiene el
cuerpo/armadura de Vegeta.

**Correspondencia de AWG** (Goku → Vegeta): L01(16→19), L18(17→20), L09(18→21),
L04(19→22), L05(20→23), L06(21→24), L42(22→25). L09→L00_S09 mapeado explicitamente.

**Estrategia = RECONSTRUCCION DE BLOQUE** (no mid-insert por-AWG, que era fragil por el
solape buffer/IB): se leen los 7 AWG de cara de Goku, se re-empaquetan como AWG de
Vegeta (layout con buffer en h+0x1F0 + IB que solapa los ultimos 32B), y se sustituye
todo el bloque de cara de Vegeta de una vez, recalculando offsets de AWGs posteriores,
AZT y AWO size.

**CLAVE del solape buffer/IB en AWG de cara**: el buffer (n*44 bytes) y el IB (n*2
bytes) se solapan 32 bytes — el IB empieza en `ib_rel = 0x1F0 + n*44 - 32`, y el tamaño
total del AWG es `end_rel = ib_rel + n*2`. Al reconstruir, el bloque de datos desde
0x1F0 es `buffer[0 : n*44-32] + IB completo`. (Bug inicial: recortar 32B del final
corrompia el IB → indices OOB → muchos skipped.)

**Verificado por exportacion OBJ** (awg_cara_export.py): los 7 AWG de cara del bin
generado exportan 148/148 o 156/156 tris, 0 skipped, 0 NaN, bounds y[0..1.52] z[0..1.25]
(= cabeza/cabello de Goku). El AWG0 (cuerpo de Vegeta armadura) sigue intacto.

**Mod instalado**: `mods/goku_armadura` (slot 327, ACTIVO). `sw_vegeta424` desactivado.
Bin: `awo_tools/bins_trabajo/goku_armadura.bin` (892216 B, LZX 123788 B, pad 126976).
**⚠️ PENDIENTE PROBAR EN JUEGO**: selector → Krillin (slot 327) → ¿Goku con armadura
saiyan (cabeza de Goku + cuerpo de Vegeta)? Si funciona, el swap de cabeza HD→HD esta
VALIDADO. Si la cabeza sale rara/crashea, revisar el face/dientes del AWG0 (aun no
swapeado).

**🔴 CRASH DEL SWAP POR RECONSTRUCCION DE BLOQUE (2026-08-19)**: el bin `goku_armadura`
(reconstruccion de bloque con mid-insert) es ESTRUCTURALMENTE VALIDO (AWG de cara
contiguos, mesh group de Vegeta conservado, geometria de Goku con 0 skipped/0 NaN,
AWG0 intacto) pero el guest CRASHEA con 0xC0000005 al parsear el modelo (hilo GPU,
sin AFS327 READ = crash temprano). Causa probable: el AWG0 referencia los AWG de cara
por offsets que se rompen al mover el bloque de cara. 

**✅ VIA ALTERNATIVA: INYECCION IN-PLACE (no mover offsets)** — `swap_cabeza_inplace.py`:
parte del bin de Vegeta armadura y copia la geometria de los AWG de cara de Goku en los
buffers EXISTENTES de los AWG de cara de Vegeta, MANTENIENDO el tamano de cada AWG (sin
mid-insert, sin mover offsets, sin tocar el AWG0). Si el buffer de Goku es menor se
rellena con 0xFF; si es mayor se trunca (L04 106->102, L06 120->106). El bin mantiene
el tamano original (894528 B) y las referencias del AWG0 quedan validas. Verificado por
exportacion OBJ (0 NaN, 148-156 tris, pocos skipped por truncado). Instalado como
`mods/goku_armadura` v2.0. **PENDIENTE PROBAR EN JUEGO**.

**✅ FUNCIONA EN JUEGO (v2.0 in-place, usuario)**: el bin de inyeccion in-place CARGA y
entra en combate — Vegeta con armadura saiyan con la cabeza/cabello de Goku inyectado.
**PERO hay Z-FIGHTING localizado en la frente/ojos**: al parpadear se ve la geometria
y textura de la cara de Goku alternando. Causa: la cara/cabello de VEGETA sigue en el
AWG0 (descriptores XVGT_L00_S00_FACE, XVGT_HAIR A=[1992+87], XVGT_M_DTEETH/UTEETH) y se
superpone a la geometria de Goku inyectada en los AWG de cara independientes (19-25).

**FIX (v3.0)**: neutralizar los descriptores de cara/cabello/dientes de Vegeta del AWG0
poniendo sus A_size/B_size a 0 (XVGT_HAIR x2, XVGT_M_DTEETH x2, XVGT_M_UTEETH x1). Los
descriptores del AWG0 tienen label en +0x00 del bloque, 'max N m' en +0x18, rangos A/B
en +0x50/+0x54/+0x58/+0x5C. Verificado: 20 descriptores siguen dibujando (cuerpo XVGT_
BODY intacto), 5 neutralizados. Instalado como `mods/goku_armadura` v3.0.
**RESULTADO EN JUEGO (v3.0)**: desaparecen partes del pelo, pero SIGUE sin ser la cara
de Goku (no es la cabeza de Goku completa). El z-fighting de frente/ojos se redujo pero
no se resuelve: la cara base de Goku inyectada en los AWG de cara (19-25) no coincide
con lo que dibuja el runtime, y neutralizar el cabello de Vegeta dejó huecos.

**🔴 ESTADO FINAL DEL SWAP DE CABEZA (2026-08-19) — PAUSADO POR DECISIÓN DEL USUARIO**:
el swap Goku→Vegeta (cuerpo de Vegeta armadura 424 + cabeza de Goku 264) queda
**documentado pero SIN resolver** y se abandona para dedicar la sesión a otras tareas.
Lo aprendido queda archivado para retomarlo si se quiere:
- La vía de **inyección in-place** (copiar buffers de AWG de cara en los AWG de cara
  del destino sin mover offsets) SÍ carga y entra en combate (a diferencia de la
  reconstrucción de bloque que crashea).
- El bloqueo real: el runtime dibuja la cara del AWG0 (descriptores XVGT_L00_S00_FACE/
  HAIR/DTEETH/UTEETH) que NO se sustituye solo inyectando los AWG de cara separados →
  z-fighting y mezcla incompleta. Para una cara completa haría falta sustituir TAMBIÉN
  la geometría de cara/cabello del sec34 del AWG0 (no solo neutralizarla, que deja
  huecos), lo que requiere re-mapear vértices entre formatos A/C. No trivial, pausado.
- Mod `goku_armadura` v3.0 sigue activo (slot 327) pero el resultado no es el buscado.
- Herramientas: `swap_cabeza.py` (reconstrucción, crashea), `swap_cabeza_inplace.py`
  (vía in-place), `awg_cara_export.py`, `awg0_export.py`, `awg_to_obj_b3.py`.

## 14. 🔴✅ REFACTOR DE ASSETS: SIN OVERLAY `active_region` (2026-08-20)

**Problema**: el launcher construía un overlay `active_region/` al lado del exe
(`PrepareRegionData`) hardlinkeando/copiando los assets de la región elegida +
los mods, y borraba la carpeta completa en cada arranque. Esto duplicaba espacio
y obligaba al usuario final a tener los assets mal ubicados. El hermano dbz1 ya
usa el modelo limpio (leer assets directo, sin duplicar), replicado aquí.

**Solución (modelo dbz1, sin duplicados)**:
1. **`OnConfigurePaths`** (`src/main.cpp`): `paths.game_data_root = game_dir`
   directo (la carpeta que contiene `us/`/`eu/`). Ya NO se construye
   `active_region`. La detección de `game_dir` soporta DOS disposiciones:
   (a) `us/` junto al exe, y (b) `assets/{default.xex,us,eu}` junto al exe
   (`FindGameRoot` busca `base/us` o `base/assets/us`, con fallback al
   project root de desarrollo a 3 niveles y al padre del exe).
2. **`ApplyRegionMount()`** (`src/region.h`/`src/region.cpp`, nuevo): monta un
   `HostPathDevice` en `\Device\Harddisk0\Partition1\us` → `game_dir/<region>`
   (us/eu), leyendo los assets directamente sin copiar nada. Se llama en el
   Play handler y en `OnPreLaunchModule` (cubre también skip-launcher).
3. **`PrepareRegionData`** (`src/launcher/settings.cpp`): ahora devuelve
   `project_root` sin construir overlay (se mantiene la firma por compat).
4. **SDK — override de archivo completo** (`rexglue-sdk`):
   - `AfsFindModFileOverride()` (`afs.cpp`/`afs.h`): busca reemplazo de un
     archivo entero en `mods/<mod>/<filename>` o `mods/<mod>/us|eu/<filename>`.
   - `HostPathEntry::Open()` (`host_path_entry.cpp`): si existe reemplazo
     completo, abre el archivo del mod (así og_music y packs de audio/sfd
     funcionan sin overlay).

**Resultado**: el juego lee `game:\us\...` directo de la carpeta de assets (o de
la región montada), y los mods (por entrada AFS y por archivo completo) los
sirve el runtime desde `mods/`. Cero duplicación, cero staging. `active_region/`
eliminado del build.

**⚠️ LECCIÓN (AGENTS §13.6)**: al recompilar el juego (`cmake --build
out/build/win-amd64-release`), el cmake SOBRESCRIBE `rexruntime.dll` con la
versión instalada en `rexglue/bin` (stale, 11155968 B). Después de compilar el
juego hay que volver a copiar la DLL correcta del SDK
(`rexglue-sdk/out/win-amd64/rexruntime.dll`, 11188224 B, con
`AfsFindModFileOverride`) al build. Verificado presente en el build.

**Parches del SDK actualizados** en `github/patches/` (ahora son 4 archivos:
`afs.cpp`, `afs.h`, `host_path_file.cpp`, `host_path_entry.cpp`). README
actualizado. Release **v1.0.2**.

### 14.1 🔴✅ FIX: DETECCIÓN DE `assets/` EN EL PAQUETE STANDALONE (2026-08-20)

**Problema reportado**: con el paquete standalone desplegado como
`<carpeta>/dbz3.exe` + `<carpeta>/assets/{default.xex, us/, eu/}`, el launcher
buscaba `default.xex` en la raíz del disco (`D:\default.xex`) en vez de
`assets/default.xex`. En el log: `Game directory: D:\` y
`Entrypoint XEX not found: D:\default.xex`.

**Causa raíz**: `OnConfigurePaths` (src/main.cpp) solo detectaba `us/` junto al
exe o en el project root a 3 niveles; con los assets en `assets/` ninguna
existía y el fallback final era `exe_dir.parent_path()` (= raíz de disco si el
exe está en la raíz de una unidad).

**Fix aplicado** (src/main.cpp): helper `FindGameRoot(base)` que devuelve `base`
si `base/us` existe, o `base/assets` si `base/assets/us` existe. La prioridad es:
1) junto al exe (exe_dir o exe_dir/assets), 2) project root de dev (3 niveles
arriba, o su assets/), 3) padre del exe (o su assets/), 4) fallback exe_dir.

**Verificado en juego** (D:\Budokai 3, layout `assets/`): el log muestra
`Game directory: D:\Budokai 3\assets`, `Mounted D:\Budokai 3\assets at
\Device\Harddisk0\Partition1`, `Loading XEX image: game:\default.xex` sin error,
y el launcher se muestra (`launcher shown, waiting for Play`).

**Sync aplicado**: `src/main.cpp` → `github/`, `dbz3.exe`/`rexruntime.dll`
actualizados en `github/release-stage/`, `RELEASE_README.md` + `README.md`
documentan las dos disposiciones (us/ junto al exe o dentro de `assets/`).
La DLL correcta (11188224 B, con `AfsFindModFileOverride`) fue restaurada en el
build tras recompilar (el cmake la sobrescribe con la versión stale de
`rexglue/bin`).

### 14.2 🔴✅ TOOLKIT DE MODDING INTEGRADO AL PAQUETE DE RELEASE (2026-08-20)

**Problema reportado (test en el paquete standalone)**: al ejecutar el zip de
release (solo exe + DLLs, sin `mod center hd`), la pestaña **Model Swap** y
**Texturas** mostraban "Esperado en: mod center hd/catalog_b3.cat" (el catálogo
no existía junto al exe), y la pestaña **Mods** decía "no mods found...". El
usuario pedía: (a) que el toolkit funcione de forma nativa en el paquete,
(b) poder instalar un mod descargado de forma nativa (que el exe cree la
carpeta), y (c) explicar qué hace falta si falta el toolkit.

**Solución — toolkit integrado al zip de release**:
- El paquete de release ahora incluye `mod center hd/` junto al exe con el
  subconjunto de RUNTIME: `swap_b3.py`, `texture_b3.py`, `catalog_b3.cat` y
  `tools/` (`xbcompress.exe`/`xbdecompress.exe` + sus DLLs `MSVCR71.dll`,
  `MSVCP71.dll`, `xbdm.dll`). Añadido `MODDING_README.md` a la raíz del paquete
  (cómo instalar mods y usar el toolkit) y sección en `RELEASE_README.md`.
- **⚠️ DLLs del XDK**: `xbcompress/xbdecompress` son binarios del XDK antiguo
  que dependen de `MSVCR71.dll`/`MSVCP71.dll` **y también de `xbdm.dll`**
  (sin xbdm dan error 0xC0000135 = DLL not found). Las tres deben copiarse
  JUNTO a los .exe (las busca Windows en el directorio del exe). El proyecto
  hermano B1 ya las tenía en su `tools/`.

**Cambios de código**:
1. **`mod_pipeline.cpp::ProjectRoot()`**: ahora detecta también `probe/assets/us`
   y `probe/assets/eu`, para que en el paquete standalone (assets en `assets/`)
   el catálogo y los scripts se encuentren junto al exe.
2. **`launcher_state.cpp`**: el mensaje de catálogo faltante ahora es claro y
   accionable (explica que hay que tener `mod center hd/` junto al exe y la ruta
   esperada); la pestaña **Mods** con 0 mods muestra un botón **"Abrir carpeta
   de mods"** que crea `mods/` si no existe y la abre en el Explorador (para que
   el usuario deje ahí su mod descargado; el launcher lo lista y activa solo).
3. **`swap_b3.py` y `texture_b3.py`**: rutas PORTABLES (funcionan en dev y en el
   paquete):
   - `TOOLS_DIR` busca primero `HERE/tools` (paquete), luego el XDK del repo.
   - `DEFAULT_AFS` busca `ROOT/assets/us` y `ROOT/us` (en el paquete `ROOT` = el
     directorio del exe, porque `mod center hd/` vive dentro de él).
   - `workdir` y `mods_root` por defecto: en el paquete usan el TEMP corregido y
     `exe_dir/mods` (el runtime sirve los mods desde `exe_dir/mods`); en dev
     usan `out/build/...`.

**Verificado end-to-end (D:\Budokai 3, layout assets/)**: `swap_b3.py --origen
298 --dest 327` extrae Goten (XGTN_BODY), comprime (107006 B), aplica mid-insert
virtual (pad 110592) y genera el mod en `exe_dir/mods/...` sin errores. El
launcher arranca y carga (game dir, montaje, XEX, "launcher shown"). El catálogo
(183 personajes) se resuelve en `exe_dir/mod center hd/catalog_b3.cat`.

**Sync aplicado**: `mod center hd/{swap_b3.py,texture_b3.py}` y
`src/launcher/{mod_pipeline.cpp,launcher_state.cpp}` → `github/`;
`dbz3.exe`/`rexruntime.dll` y el toolkit `mod center hd/` → `github/release-stage/`;
`MODDING_README.md` nuevo; `RELEASE_README.md` actualizado. La DLL correcta
(11188224 B) restaurada en el build tras recompilar. Release v1.0.2 actualizada
con el zip regenerado (incluye el toolkit).

### 14.3 🔴✅ CRÍTICO: `dbz1_diag_logging` no se podía desactivar → .bmp en juego normal (2026-08-20)

**Problema reportado (crítico)**: el juego seguía generando `black_*.bmp` y
`frontbuf_*.bmp` (30 archivos, ~7.5MB c/u) **sin tener marcado nada en Dev**.
Esto ensuciaba la carpeta y era inaceptable.

**Causa raíz (doble)**:
1. **La propagación del flag por nombre NO funcionaba**: `src/launcher/settings.cpp`
   usaba `rex::cvar::SetFlagByName("dbz1_diag_logging", ...)` para apagar el
   diagnóstico. PERO `dbz1_diag_logging` se define SOLO en `rexruntime.dll`
   (`src/system/dbz1_diag_flags.cpp`), no en el exe. `SetFlagByName` resuelve en
   el **registro de cvars del exe**, donde ese flag NO existe → devuelve `false`
   y **no hacía nada**. El flag del runtime quedaba en su estado (true si una
   sesión lo dejó activo), y la GPU (rexgpu-xenos.dll, `command_processor.cpp`
   líneas 2284/2372/2934/2986) escribía los .bmp gated por
   `REXCVAR_GET(dbz1_diag_logging)`.
2. **La lib de enlace instalada estaba stale**: el exe enlaza con
   `rexglue/lib/rexruntime.lib` (instalada, 5471988 B, 09/08) que **NO exportaba**
   `FLAGS_dbz1_diag_logging_storage_`. La lib correcta del SDK
   (`rexglue-sdk/out/win-amd64/rexruntime.lib`, 5978500 B) sí. Al usar
   `REXCVAR_SET(dbz1_diag_logging, ...)` el enlace fallaba hasta copiar la lib.

**Fix aplicado** (`src/launcher/settings.cpp`):
- Añadido `REXCVAR_DECLARE(bool, dbz1_diag_logging)` (el símbolo está exportado
  por `WINDOWS_EXPORT_ALL_SYMBOLS` del runtime). Ahora el exe enlaza el accessor
  `FLAGS_dbz1_diag_logging_storage_()` y escribe la **misma storage** que lee la
  GPU.
- Sustituido `SetFlagByName("dbz1_diag_logging", ...)` por
  `REXCVAR_SET(dbz1_diag_logging, ...)` en los 3 puntos: `SetDevMode`,
  `SetDiagLogging` y `ApplyRuntimeSettingsToSdk`. Con Dev y Diag off (default),
  `false && false` = **false** → la GPU no genera .bmp.
- Copiada `rexglue-sdk/out/win-amd64/rexruntime.lib` → `rexglue/lib/rexruntime.lib`
  (para que el enlace encuentre el símbolo) y
  `rexglue-sdk/out/win-amd64/rexruntime.dll` → `rexglue/bin/rexruntime.dll`
  (para que futuras compilaciones usen la DLL correcta, ver §13.6).

**Verificado**: el exe compila (enlaza el símbolo), y en el paquete standalone
`D:\Budokai 3` el juego arranca **sin generar ningún .bmp** (0 archivos) con Dev
y Diag off. La generación de .bmp está gated exclusivamente por
`REXCVAR_GET(dbz1_diag_logging)` en `command_processor.cpp`.

**Sync aplicado**: `src/launcher/settings.cpp` → `github/`; `dbz3.exe` +
`rexruntime.dll` (11188224 B) → `github/release-stage/`. **Release v1.0.3**
nueva con el fix (zip regenerado).

### 14.6 🔴✅ P2.1 COMPATIBILIDAD: BOOTSTRAP DE ISA + VARIANTES AVX2/LEGACY (2026-08-25)

**Objetivo (HOJA_DE_RUTA_COMUNIDAD 2.1)**: CPUs sin AVX2 (Intel pre-Haswell,
AMD pre-Excavator) crasheaban con `0xc0000142`/`0xc000001d` al arrancar porque
el runtime se compila con `-march=x86-64-v3`.

**Hallazgo clave que simplifica el diseño**: el exe del juego (`dbz3.exe`) se
compila **sin** `-march` (CMAKE_CXX_FLAGS vacío = baseline x86-64). El AVX2
vive SOLO en las DLLs del SDK (rexruntime.dll, rexgpu-xenos.dll y la FFX).
Verificado en el cache del build. Por eso el core es UN solo binario y solo
cambian las DLLs por variante.

**Arquitectura (estándar de la industria, UE5-like)**:
```
<release>/
  dbz3.exe               <- bootstrap (baseline x86-64, 44544 B, SIN SDK)
  dbz3_avx2/dbz3_core.exe + rexruntime.dll(10934272 v3) + rexgpu-xenos.dll(6207488) + ffx dx12(5420544)
  dbz3_legacy/dbz3_core.exe + rexruntime.dll(10836480 v2) + rexgpu-xenos.dll(6162944) + ffx dx12(5414912)
  assets/ (o us/eu + default.xex), mods/, mod center hd/, docs
```
- **`src/bootstrap.cpp`** (`dbz3_bootstrap` target en CMakeLists): WinMain,
  `HasCpuX86V3()` = `__cpuid` leaf 7 EBX {AVX2 bit5, BMI1 bit3, BMI2 bit8} +
  leaf 1 ECX {FMA bit12, OSXSAVE bit27} + `_xgetbv(0)`&6 (gated a OSXSAVE).
  Lanza `dbz3_avx2\dbz3_core.exe` o `dbz3_legacy\dbz3_core.exe`, pasa los args
  del usuario, propaga el exit code, y muestra ventana de error clara si el
  child no se encuentra o falla con código de excepción (>=0x80000000).
- **dbz3_core.exe = el dbz3.exe actual** (baseline, MISMO binario en las dos
  carpetas). En dev se sigue lanzando directo (`out/build/.../dbz3.exe`).
- **Build v2 del SDK**: segundo build dir `rexglue-sdk-0.10/out/build-win-vulkan-legacy`
  con `-march=x86-64-v2` y `-DREXGLUE_OUTPUT_DIR=.../out/win-amd64-legacy`
  (nueva cache var en CMakeLists del SDK para no pisar `out/win-amd64`).
  Produjo rexruntime.dll v2 (10836480), rexgpu-xenos v2 (6162944),
  amd_fidelityfx_dx12 v2 (5414912). El FFX se compila desde el fuente FFX del
  SDK (no es solo el prebuilt firmado) → necesita su variante.
- **amd_fidelityfx_vk.dll** = el 0.9 heredado, igual en ambas variantes (el
  backend Vulkan es experimental; aceptable).

**Cambios de paths necesarios (el core vive en subcarpeta en release)**:
- **Mods walk-up** (parche en `afs.cpp::AfsModsRoot` + `settings.cpp::ModsRoot`
  + `mod_pipeline.cpp::ModsOutDir`): buscan `mods/` desde el exe subiendo hasta
  3 niveles (release: `dbz3_avx2\` → `<root>\mods`). En dev sin cambios.
- `ProjectRoot()` ya subía (cubre `mod center hd/` y `assets/`).
- `FindGameRoot` (main.cpp) ya tenía el fallback "padre del exe" (§14.1) →
  resuelve `<root>\assets` desde `dbz3_avx2\` sin cambios.
- logs/toml/user_data quedan en la carpeta de la variante (aceptable; el
  diálogo de crash muestra la ruta del log).

**⚠️ bootstrap sin AVX2 en su .text propio**: el `-march=x86-64` explicito del
target lo garantiza (verificado: el `-S` de bootstrap.cpp no emite AVX2). Las
únicas instrucciones AVX2/AVX en el exe final son del **wmemcpy del CRT MSVC,
protegido por dispatch** (`testb $0x20, [__isa_available]` + `je` fallback SSE2
en 0x1400050ac) — seguro en CPUs legacy. La DLL dinámica se linkea (importa
MSVCP140/VCRUNTIME140/api-ms-win-crt).

**Script de release**: `tools/make_release.ps1` — monta `github/release-stage`
(dbz3.exe bootstrap + dbz3_avx2/ + dbz3_legacy/ + mod center hd/ + mods/ +
docs) y genera el zip. Origen canónico de las CRT DLLs = `github/` (msvcp140,
msvcp140_atomic_wait, vcruntime140, vcruntime140_1 del redist VC; rexruntime
importa msvcp140_atomic_wait). `README_PRIMER_ARRANQUE.txt` nuevo (disposiciones
+ variantes ISA).

**Validado (test local, CPU AVX2)**: bootstrap → spawn `dbz3_avx2\dbz3_core.exe`
(confirmado por proceso + log en `dbz3_avx2\logs`), launcher muestra
"launcher shown, waiting for Play"; la variante **legacy** también arranca
directo (v2 DLLs OK). Pendiente: validar en una CPU sin AVX2 real (o forzar la
ruta legacy) y la confirmación visual de que los mods se listan desde la raíz.

**Sync**: `src/{bootstrap.cpp, launcher/{settings.cpp, mod_pipeline.cpp}}`,
`CMakeLists.txt`, `tools/make_release.ps1`, `patches/.../afs.cpp` (walk-up),
`RELEASE_README.md`, `README_PRIMER_ARRANQUE.txt`, `MODDING_README.md` →
`github/`. Release **v1.0.5** en `github/release-stage/` + zip (33112680 B).

### 14.7 🔴✅ P3.1 CONTROLES: TECLADO POR DEFECTO + MAPEO CONFIGURABLE + DEADZONE/RUMBLE REALES (2026-08-25)

**Objetivo (HOJA_DE_RUTA_COMUNIDAD 3.1)**: "teclado/botones no responden en
juego". Diagnóstico: el driver MnK del SDK (teclado→mando) ya existía completo,
pero `mnk_mode` estaba en **false** por defecto y **nadie en dbz3 lo activaba**
→ el teclado no hacía nada. Además los sliders de deadzone/rumble del launcher
eran **placebo** (se guardaban en el toml pero jamás llegaban al runtime: el SDK
0.10 eliminó esos cvars).

**Qué se hizo**:
1. **Parche SDK `input_system.cpp`** (nuevo archivo en `github/patches/`):
   - `REXCVAR_DEFINE_DOUBLE(deadzone, 0.1)` — aplicado en `InputSystem::GetState`
     al estado fusionado: los 4 ejes de sticks se anulan si su magnitud
     < `deadzone * INT16_MAX`. Cubre XInput+SDL+MnK en el único punto de salida.
   - `REXCVAR_DEFINE_BOOL(rumble, true)` — en `InputSystem::SetState` acepta la
     vibración sin llegar a ningún pad cuando está OFF.
   - Ambos se exportan por nombre; el exe los escribe vía `SetFlagByName` (el
     registro de cvars vive en rexruntime.dll y está compartido con el exe —
     verificado: rexruntime.dll exporta `SetFlagByName`/`RegisterFlag`/`Query`).
2. **Launcher `settings.{h,cpp}`** — cvars nuevos persistidos en dbz3_user.toml:
   - `dbz3_mnk_mode` (**default TRUE** → teclado funciona de serie en PC),
     `dbz3_mnk_mouse` (ratón → stick derecho), `dbz3_input_backend`
     ("xinput"/"sdl", default xinput).
   - `dbz3_keybind_*` (24 wrappers: a,b,x,y,lt,rt,lb,rb,ls 4d+press, rs 4d+press,
     dpad 4, back, start, guide) con defaults = los del SDK 0.10 (start corregido
     a "Return" para que la tecla X no duplique pausa).
   - `ApplyUserSettingsToSdk` ahora propaga: `input_backend`, `deadzone`,
     `rumble`, `mnk_mode`, `mnk_mouse` y los 24 `keybind_*` al runtime.
3. **Launcher `launcher_state.cpp`** — pestaña Input ampliada:
   - Selector "Controller backend" (XInput/SDL) con aviso de que SDL puede
     colgar con RTSS/OBS (por eso xinput es default).
   - Slider de deadzone + checkbox rumble (ahora con efecto real).
   - Checkbox "Enable keyboard/mouse emulation" + "Use mouse for right stick".
   - Sección "Keyboard (MnK) mapping": 24 campos de texto (helper `DrawKeybind`,
     formato `Tecla`, comas = alternativas, `Shift+/Ctrl+/Alt+` = modificadores).
   - Reset to defaults restaura los cvars nuevos.
4. **Detalle verificado**: el exe enlaza `rex::runtime` (la DLL) → sus llamadas
   a `rex::cvar::SetFlagByName` se resuelven a la exportación de rexruntime.dll
   → registro compartido. La advertencia de §14.3 sobre `SetFlagByName` era de
   la era 0.9/prebuilt; en 0.10 el path por nombre es el correcto (documentado
   en `cvar.h`: "Cross-DLL access path").

**Compilado y smoke test OK**: rexruntime.dll 10940928 B (parche deadzone/rumble),
dbz3.exe 17461248 B, "launcher shown, waiting for Play" sin errores. FFX v3
restaurada a 5420544 (la del release v1.0.5; un rebuild del SDK la regeneró en
5418496 y se prefirió no cambiar un binario que no tocaba la tarea).

**⚠️ PENDIENTE PROBAR EN JUEGO (usuario)**: (a) el teclado emula el mando por
defecto (menús + combate), (b) remapear teclas en la pestaña Input y que se
apliquen, (c) mando XInput sigue funcionando (deadzone/rumble). En el paquete de
release hay que regenerar `dbz3_avx2/` y `dbz3_legacy/` con el dbz3_core.exe y
rexruntime.dll nuevos.

**Sync**: `src/launcher/{settings.{h,cpp}, launcher_state.cpp}` y
`patches/.../src/input/input_system.cpp` (nuevo) + `patches/README.md` (10
archivos) → `github/`.

### 14.8 🔴✅ P2.3/P2.2 RENDIMIENTO: FRAME PACING REAL + PRESETS DE CALIDAD POR GPU (2026-08-25)

**Objetivo (HOJA_DE_RUTA_COMUNIDAD 2.3 y 2.2)**: "el juego corre acelerado" y
hacer las integradas jugables (presets automáticos).

**Hallazgo clave de 2.3 — el pacing del guest en 0.10 lo hace `vsync`, no
`frame_cap`**:
- El SDK 0.10 **eliminó el cvar `frame_cap`** del 0.9. Verificado: no existe en
  toda la fuente del SDK → `SetSdkInt("frame_cap", cap)` del launcher fallaba en
  silencio (placebo).
- El pacing real del guest es el worker `vsync` de `GraphicsSystem`
  (`graphics_system.cpp`): con `vsync` ON el vblank del guest corre a
  `1/video_mode_refresh_rate` (60 Hz) → el juego a su velocidad correcta. Con
  `vsync` OFF el vblank corre a ~1000 Hz (intervalo 1ms) → **la lógica del juego
  corre ~16x más rápida = el "juego acelerado"** de los reportes.
- `video_mode_refresh_rate` solo *reporta* el modo (xboxkrnl_video.cpp); no pacea.
- **Fixes**:
  1. **vsync forzado a true en el juego** (`ApplyRuntimeSettingsToSdk`): el guest
     DEBE correr a 60 Hz; si el usuario lo tenía off se fuerza con un warning.
     Se **eliminó el checkbox "VSync"** del launcher y del menú Dev (era un
     placebo/contraproducente) → ahora muestra "Game speed: fixed 60 FPS".
  2. **`frame_cap` REAL restaurado** (parche `d3d12_presenter.cpp`): cvar
     `frame_cap` (0=sin límite) + throttle de presentación en
     `PaintAndPresentImpl` (sleep hasta el slot de frame_cap FPS con
     `std::chrono::steady_clock` + `rex::thread::Sleep`; pintura serializada →
     timestamp file-scope seguro). Solo limita la tasa de presentación host
     (30 = media carga en integradas); NO toca el vblank del guest.
  3. **`dbz3_frame_cap` default 0 → 60** (instalaciones frescas obtienen pacing
     correcto; el ResetToDefaults ya ponía 60 — ahora coherente).
  4. La propagación `SetSdkInt("frame_cap", ...)` ahora SOLO en modo juego
     (`for_game`); el launcher mantiene sus repaints ImGui sin límite.

**2.2 — Presets de calidad + detección de GPU (lo práctico; OpenGL/D3D11 NO
viables, ver HOJA_DE_RUTA §2.2)**:
- **Detección DXGI** (`settings.cpp`): `DetectGpuName()`/`DetectGpuTier()`
  enumeran el primer adaptador no-software (`CreateDXGIFactory1` +
  `EnumAdapters1`, descripción + `DedicatedVideoMemory`), enlazado con
  `#pragma comment(lib, "dxgi.lib")`. Tier: 0=low, 1=medium, 2=high. Intel
  iGPU (sin "Arc") nunca auto-por encima de medium. RTX 4070 SUPER → tier 2.
- **Preset `dbz3_quality_preset`** (auto/low/medium/high/ultra/manual):
  - `auto` → detecta la GPU y aplica el perfil recomendado en cada arranque.
  - Perfiles: **low** (1x, sin MSAA, aniso off, bilinear), **medium** (1x, sin
    MSAA, 4x aniso, FSR), **high** (1x, MSAA on, 16x aniso, FSR),
    **ultra** (2x, MSAA on, 16x aniso, FSR — solo manual, nunca auto).
  - **Migración**: un toml existente SIN `dbz3_quality_preset` se migra a
    "manual" (LoadUserSettings se llama 2 veces: primero en OnConfigurePaths
    ANTES de que el logging esté activo — ahí corre la migración — y luego en
    OnPreSetup). Así un usuario con config hecha a mano nunca la ve cambiada.
  - Instalación fresca (sin toml) → "auto" → el launcher aplica la detección.
- **UI (pestaña Video)**: línea "GPU: <nombre> - detected tier: <X>", combo
  "Quality preset" (Auto/Low/Medium/High/Ultra/Manual), slider "Frame cap"
  ahora REAL con hint de 30 FPS para integradas, texto "Game speed: fixed 60".
  ResetToDefaults añade `dbz3_quality_preset=auto`.

**Verificado**: rexruntime.dll 10944000 B (cvar `frame_cap` presente), dbz3.exe
17479168 B, smoke test OK. Con el toml del usuario: `preset=manual` (2x, sin
cambios). Sin toml: `preset=auto` → tier 2 → `1x + fsr` (High). FFX v3
restaurada (5420544).

**⚠️ PENDIENTE**: (a) probar en juego el frame cap (60 y 30) y el preset auto en
una máquina con integrada; (b) el perfilado Tracy del path D3D12 (build
win-amd64-tracy) queda como tarea opcional de optimización fina — necesita
sesión de juego para datos reales.

**Sync**: `src/launcher/{settings.{h,cpp}, launcher_state.cpp}`,
`src/ingame/menu.cpp`, `patches/.../src/ui/d3d12/d3d12_presenter.cpp` (nuevo) +
`patches/README.md` (11 archivos) → `github/`.

### 14.9 🔴✅ BUGS DE CIERRE/ARRANQUE + I18N DEL LAUNCHER (2026-08-25 noche)

**Reportes del usuario**: (a) Alt+F4 en juego → "No responde" hasta matar el
proceso; (b) el launcher a veces se abría en negro + "No responde" y aparecía
"cuando pasa un rato"; (c) presets "no visibles"; (d) el selector Language debía
traducir TODO el launcher (estaba "spanglish"). **CAUSA RAÍZ DE (b) ENCONTRADA Y
REPRODUCIDA (marcadores de log)**: el arranque se colgaba en
`InputSystem::AttachWindow` → `SDLInputDriver::OnWindowAvailable` →
`CallInUIThreadSynchronous` → **`SDL_InitSubSystem(SDL_INIT_GAMEPAD)`** (el
cuelgue clásico con RTSS/OBS). El usuario tiene `dbz3_input_backend = "sdl"` en
su toml. **Fix**: init de SDL ASINCRONA (un `std::thread` hace la init SDL en
segundo plano; flags atómicas; hilo detached). Launcher pasa de colgarse a
"launcher shown" en **1.6 s**. El fix de `CallInUIThreadSynchronous` con timeout
del 0.9 NO se portó a 0.10 (fence.Wait sin timeout) — con la init async ya no
bloquea el arranque; anotado como follow-up.

**Fix (a) Alt+F4**: el log del usuario (dbz3_003) confirmó que Alt+F4 SÍ llegaba
a `Dbz3App::OnWindowCloseRequested` ("Window close requested") pero se colgaba
después (nunca llegaba a `OnClosing`). **Fix en `src/main.cpp`**:
`OnWindowCloseRequested` ahora hace `rex::FlushLogging()` + `std::_Exit(0)`
inmediatamente (mismo hard-exit que OnClosing; TerminateTitle/PerformClose/
focus-loss podían bloquearse con hilos guest rezagados). Cerrar el juego (X o
Alt+F4) siempre sale al instante.

**Fix (c) presets visibles**: la pestaña Video ahora muestra "Activo: <preset> →
<escala>x, MSAA, aniso, efecto" bajo el combo de preset → "auto" ya no es una
caja negra (aplica en memoria y re-evalúa en cada boot).

**Fix (d) i18n del launcher** (ES/EN, resto → EN):
- Nuevo `src/launcher/i18n.{h,cpp}`: `dbz3::i18n::SetLanguage(id)` +
  `T(es, en)` que devuelve el string según `dbz3_language` (5=ES, resto=EN).
- `launcher_state.cpp`: **172 cadenas** envueltas con `T(es,en)` (banner, tabs,
  Video/Upscaling/Audio/Input/Mods/Model Swap/Texturas/Dev, tooltips, diálogos).
  `OnDraw` llama `SetLanguage(Language())` cada frame → el cambio de idioma se
  aplica al instante. Los arrays de combos localizados se hicieron no-static
  (antes con `static` capturarían el idioma una sola vez).
- `CMakeLists.txt`: añadido `src/launcher/i18n.cpp`.
- La pestaña Video renombra el combo a "Idioma del launcher y del juego" y la
  ayuda explica que traduce todo el launcher.

**SDK parcheado (rexruntime, avx2+legacy)**: `src/ui/presenter.cpp`
(`WaitForUITickFromUIThread` con `wait_for(50ms)` → el hilo UI NUNCA se bloquea
esperando vblank → adiós ventana negra + siempre procesa mensajes) y
`src/input/sdl/sdl_input_driver.{h,cpp}` (init SDL async). Parches nuevos en
`github/patches/` (README → 14 archivos). **Binarios**: dbz3.exe 17488896 B,
rexruntime.dll 10945536 B (avx2) / 10847232 B (legacy). FFX validada intacta
(hashes idénticos).

**Verificado**: launcher "shown" en 1.6 s (antes colgaba 25 s+), 3 drivers de
input pasan AttachWindow al instante, juego compila y enlaza, FFX intacta.
Alt+F4 validado por análisis estático (el test automatizado no puede simular el
Alt+F4 real: SDL intercepta la tecla, no el SC_CLOSE posteado; el usuario
validará en juego). **Release v1.0.7** pendiente de empaquetar con estos
binarios.

**Sync**: `src/{main.cpp, launcher/{launcher_state.cpp, i18n.{h,cpp}}}`,
`CMakeLists.txt`, `patches/.../{presenter.cpp, sdl_input_driver.{h,cpp}}` +
`patches/README.md` (14 archivos), `RELEASE_README.md` → `github/`. NO subido a
GitHub (plan general aún en curso).

### 14.10 🔴✅ I18N IT/DE/FR + UI COMPACTA SIN SCROLLBARS + MARCADOR DE ARRANQUE (2026-08-26)

**Reportes del usuario**: (a) el selector de idioma solo traducía ES/EN (IT/DE/FR
caían a inglés "por algún motivo"); (b) el launcher sigue tardando en pasar de
pantalla negra a launcher; (c) la UI no cabe en la ventana (barras deslizantes);
(d) Alt+F4 ya funciona ✅.

**Fix (a) i18n completo EN/ES/IT/DE/FR** (`src/launcher/i18n.{h,cpp}`):
- Diseño por TABLA, no por call-site: `T(es,en)` mantiene su firma; internamente
  busca en `kTable[]` (161 entradas) la traducción según `dbz3_language`
  (3=DE, 4=FR, 5=ES, 6=IT; resto→EN, incluido 2=Japonés). La clave ES es la
  **cadena runtime EXACTA** (los literales concatenados de varias líneas cuentan
  como una sola clave; se genera con un script Python que parsea los 172 call
  sites de `launcher_state.cpp`, `extract_i18n.py`/`gen_i18n.py` en %TEMP%).
- Traducciones: 483 cadenas (161×3) escritas a mano (IT desde ES, DE/FR desde
  ES/EN), UTF-8 (clang la compila bien). Las cadenas con `\n`/`\\` se emiten
  verbatim en la clave y escapadas en las traducciones.
- El combo de idioma sigue listando los 6 idiomas del juego; el launcher solo
  tiene EN/ES/IT/DE/FR (Japonés→EN).

**Fix (b) UI compacta sin scrollbars** (`launcher_state.cpp`):
- Estilo global apretado: WindowPadding (16,16)→(12,8), FramePadding
  (10,6)→(7,4), ItemSpacing (10,8)→(7,4), CellPadding/ScrollbarSize menores.
- Banner: font scale 1.5→1.3, separadores reducidos.
- **Video tab en 2 columnas** (BeginChild izquierda/derecha): izquierda =
  Calidad de imagen + Idioma; derecha = Pantalla + Motor + Gamma. Las ayudas
  largas (preset, escala, MSAA, frame cap, VRR, monitor, Vulkan) pasan a
  `SetTooltip` al pasar el ratón (siempre `SetTooltip("%s", i18n::T(...))`
  para no trigger -Wformat-security).
- **Input tab: los 24 keybinds en rejilla de 3 columnas** (antes vertical).
  Ayudas de backend/MnK/mouse a tooltips.
- Dev tab: 4 ayudas a tooltips. Mods/ModelSwap/Texturas mantienen su scroll
  interno (listas naturalmente largas).
- Resultado: todos los tabs de ajustes caben en la ventana 1280x720 sin barras.

**Fix (c) diagnóstico del arranque lento** (`patches/.../d3d12_presenter.cpp`):
- Log del primer `Present` del presentador D3D12: `dbz3: first present OK
  (device/swapchain init + first paint took X ms)` → con los timestamps del log
  (que ya tienen hora), el log del usuario muestra cuánto tarda la pantalla
  negra (ventana creada→primer frame) en SU máquina. En mi máquina son ~7ms.
  PENDIENTE: leer el valor en la máquina del usuario para saber si el cuello de
  botella es el init D3D12/swapchain del primer paint (entonces: init temprano
  o ventana oculta hasta el primer frame) o algo anterior.

**Binarios**: dbz3.exe 17517568 B, rexruntime.dll 10947584 B (avx2) /
10849792 B (legacy) con el marcador. Copiada la DLL avx2 al build y a
`rexglue/bin` (evitar el stale de §13.6). FFX intacta.

**Sync**: `src/launcher/{i18n.{h,cpp}, launcher_state.cpp}` +
`patches/.../d3d12_presenter.cpp` + `patches/README.md` → `github/`. NO subido
a GitHub. **Release v1.0.8** pendiente de empaquetar.

### 14.11 🔴✅ IDIOMA QUE CONDICIONA EL JUEGO + FOOTER CON PLAY SIEMPRE VISIBLE (2026-08-26)

**Reportes del usuario**: (a) el selector de idioma solo afectaba al launcher
(esperaba que condicionara también el texto del juego); (b) la barra de scroll
derecha seguía en Video/Audio/Dev y "PLAY" no se veía de entrada; (c) hacer el
launcher más user-friendly (investigar buenas prácticas).

**Fix (a) idioma → juego REAL** (`patches/.../xam_info.cpp`, nuevo parche #10):
- El guest elige su idioma vía `XGetLanguage`. `XGetLanguage_entry` devolvía
  inglés fijo (basado en region). Ahora devuelve `REXCVAR_GET(user_language)`,
  el cvar que el launcher YA propagaba desde `dbz3_language`
  (`ApplyUserSettingsToSdk`: `REXCVAR_SET(user_language, Language())`).
- ⚠️ Scope: `user_language` se define en `xam_user.cpp` ANTES de abrir los
  namespaces → su accesor es GLOBAL. El `REXCVAR_DECLARE` de `xam_info.cpp`
  debe ir también fuera de `namespace rex::kernel::xam` (si no: link error
  `undefined symbol: rex::kernel::xam::FLAGS_user_language_storage_`).
- **Verificado por disassembly**: la DLL nueva tiene `XGetLanguage_entry` =
  `sub rsp,28; call FLAGS_user_language_storage_; mov eax,[rax]; add rsp,28; ret`.
  El selector del launcher ahora controla el texto del juego.

**Fix (b) scrollbars + PLAY invisible** (`launcher_state.cpp`):
- **CAUSA RAÍZ**: los tabs de ajustes usaban `BeginChild(..., ImVec2(0,0))`
  (llenan TODO el alto restante) → el footer se dibujaba FUERA de la ventana →
  la ventana ganaba una barra de scroll y PLAY quedaba bajo el pliegue.
- **Fix**: TODOS los tabs reservan el alto del footer con
  `ImVec2(0, -kFooterHeight)` (kFooterHeight = 76) → el footer siempre visible.
- **Verificado por logging de geometría** (debug temporal, luego eliminado):
  `win_scroll_y=0`, `video_left/right scroll_y=0`, y **PLAY rect y=[670,712]
  en una ventana de 720** → sin barras y PLAY a la vista.

**Fix (c) user-friendly** (aplicando buenas prácticas de launchers investigadas
online: PLAY = acción primaria siempre visible y de alto contraste; agrupar por
intención; feedback de qué se va a lanzar; utilidades secundarias discretas):
- **Footer rediseñado**: línea de resumen "Inicio: <región> - <backend> - <N>x -
  <efecto> - <idioma>" + combo de región (movido de la pestaña Mods, ahora
  siempre visible con tooltip) + botones secundarios [Restablecer][Guardar] +
  **PLAY grande y VERDE (300x42, estilo primary)** a la derecha.
- El selector de región se eliminó de la pestaña Mods (ya no hay duplicado).
- Tooltip del combo de idioma aclara que afecta al launcher Y al juego.
- Arreglada una cadena mojibake doble (`pequeÃƒÂ±a` → `pequena`, byte-level).
- Cadenas nuevas en la tabla i18n: "Inicio: ..." y el tooltip del idioma
  (añadidas a mano en i18n.cpp, que es GENERADO — regenerar con el script si se
  tocan más strings).

**Binarios**: dbz3.exe 17519104 B, rexruntime.dll 10947584 B (avx2) /
10849792 B (legacy) con el parche XGetLanguage. FFX intacta (avx2 5420544 A20438,
legacy 5414912 5FE146). DLL avx2 copiada a build + `rexglue/bin`.

**⚠️ LECCIÓN**: al recompilar el SDK avx2, el FFX de `rexglue-sdk-0.10/bin/`
se regenera (5415424, distinto de la validada). NO copiarlo al build: el build
y el release usan la FFX validada (5420544). La FFX del `out/win-amd64` del SDK
sigue siendo la validada (la regeneración va a `bin/`, no a `out/`).

**Sync**: `src/launcher/{i18n.{h,cpp}, launcher_state.cpp}` +
`patches/.../{xam_info.cpp}` + `patches/README.md` (15 archivos) → `github/`.
NO subido a GitHub. **Release v1.0.9** pendiente de empaquetar.

### 14.12 🔴✅ EL XEX EU/PAL NO ES COMPATIBLE + DETECCIÓN Y BLOQUEO (2026-08-26)

**Reporte del usuario**: "mucha gente reportó problemas con el `.xex` cuando
usan el de EU; en conversaciones previas se dijo que era el mismo que el USA".
Petición: verificar que eso esté 100% corregido.

**VERIFICACIÓN EMPÍRICA (boot real del guest, no solo hashes)**:
- `yae3_xenon.xex` (US/NA) vs `yae3_xenon_eu.xex` (EU/PAL): mismo tamaño
  (4890624 B), mismo title id (0x82) y media id (0xFF030000), MISMOS headers
  XEX2, mismo layout de imagen reportado por el runtime (code=82080000-
  8230EC00, image=82000000-826D0000) — pero **MD5 distinto** (A53E... vs
  C37E...). Los bytes crudos van cifrados con la clave de cada región → no
  comparar en crudo.
- **El xex EU NO puede arrancar en este port**: con `dbz3_skip_launcher`,
  `OnPostLaunchModule` se alcanza pero el guest muere al instante con
  `XThread::Execute - No function registered at 8221C570` (la misma dir tanto
  con assets EU como con assets US → el fallo es 100% del ejecutable EU).
  Causa: el **codegen (recompilador, 44 recomp files) se generó SOLO del xex
  US**; el código descifrado del EU difiere (call graph distinto) y llama a
  direcciones que la tabla de funciones del recompilador no cubre.
- **El xex US arranca el guest sin error** (control), con región EU y cualquier
  idioma vía el launcher (mount us/eu + `user_language`). → Un usuario EU no
  pierde nada usando el xex US: la región PAL (assets `eu/`) y el idioma los
  gestiona el launcher.

**Fixes aplicados**:
1. **`FindGameRoot` acepta `eu/`** (`src/main.cpp`): antes solo aceptaba `us/`
   → un layout con SOLO `eu/` (usuario EU sin carpeta us) fallaba la
   auto-detección y caía en "Entrypoint XEX not found" al Play. Ahora acepta
   `base/eu` y `base/assets/eu` (coherente con `IsValidGameDataDir` y el banner).
   (Este fue el bug secundario que EXPLICABA parte de los reportes "EU".)
2. **Detección del xex EU en el launcher** (`settings.{h,cpp}` +
   `launcher_state.cpp`): nuevo `XexStatus` (kMissing/kUs/kEu/kUnknown) +
   `CheckDefaultXex(root)` = MD5 de `default.xex` (CryptoAPI, caché por
   path+size+mtime para no re-hashear el ~4.9MB cada frame; fast-reject por
   tamaño). Hashes conocidos: US `A53E324B5D2A65EBCBF648E4F85A7271`, EU
   `C37EB979B762DA0AB5B8C9BA8037CE4E` (los de disco son fijos por región).
   - Banner: xex EU → **mensaje rojo claro y Play BLOQUEADO** (en vez del
     crash críptico); xex desconocido (modificado/otra región) → **nota ámbar**
     informativa sin bloquear; US → verde normal.
   - Guard también en `LaunchModule` con `skip_launcher` (path dev): si el xex
     es EU, error + MessageBox claro, no arranca el guest.
3. **Docs**: RELEASE_README + README_PRIMER_ARRANQUE explican "usa SIEMPRE el
   `default.xex` US/NA (`yae3_xenon.xex`); el EU/PAL no arranca; región e
   idioma se eligen en el launcher".

**Verificado**: con xex EU + skip_launcher → log `skip_launcher with EU/PAL
default.xex - the recompiled port only supports the US/NA executable` y NO crea
el guest; con xex US → `OnPostLaunchModule - guest thread created and resumed`
(sin error); launcher con xex EU se muestra sin crash (banner activo).
`dbz3.exe` 17528320 B.

**NOTA IMPORTANTE (verdad del asunto)**: "el xex EU es el mismo que el USA" es
**FALSO** a nivel de binario y de código — el port solo está recompilado del
US. Lo que SÍ es correcto: **no hace falta un xex EU** porque la región y el
idioma los gestiona el launcher sobre el xex US. Con el xex US + región eu +
idioma elegido, el usuario EU juega 100% (assets PAL + textos en su idioma).

**Sync**: `src/{main.cpp, launcher/{settings.{h,cpp}, launcher_state.cpp,
i18n.cpp}}` + `RELEASE_README.md` + `README_PRIMER_ARRANQUE.txt` → `github/`.
NO subido a GitHub. **Release v1.0.10** pendiente de empaquetar.

### 14.13 🔴✅ EL PORT EU/PAL YA FUNCIONA — SEGUNDA RECOMPILACIÓN (2026-08-26)

**Reporte del usuario (tras §14.12)**: "el xex EU debería ser 100% compatible, no
puede ser que hagamos la promesa de US o EU y no sea así". **Decisión: construir
el port del xex EU como segunda recompilación** (no solo bloquearlo). Resultado:
**ambos ejecutables funcionan** — el launcher elige el núcleo correcto solo.

**HALLAZGO CLAVE: el xex EU es una build distinta a nivel de código** (no solo
bytes): el codegen analiza el EU pero con **direcciones de funciones DIFERENTES**
(partition JSON: US partition 3 @0x820800A8 vs EU @0x82080158). El title id es el
MISMO (4E4D0856) en ambos → el discriminador es el **MD5** (US `A53E...`, EU
`C37E...`; los bytes crudos van cifrados por región).

**Pipeline del port EU (todo validado)**:
1. **Codegen EU converge** (`dbz3_manifest_eu.toml` + `dbz3_config_eu.toml`):
   `rexglue codegen` sobre `yae3_xenon_eu.xex` → `generated_eu/` (prefijo
   `dbz3_eu_*`). 95 funciones (mismo conteo que US). `dbz3_config_eu.toml` =
   el US + ajustes EU:
   - `0x82097F08` sin `size 0x10` (el EU ramifica más allá);
   - 4 name-only (`0x821ECA98, 0x82097EA0, 0x8213EB00, 0x821ED5C8`) para los
     5 unresolved calls;
   - ⚠️ las entradas adicionales deben ir DENTRO de `[functions]` (apendar
     tras `[[switch_tables]]` las mete en la tabla del switch → no surten efecto);
   - ramas condicionales no resueltas → **declarar con size del rango de la
     partición** (`0x8213E834 size 0x1CC`, `0x82296500 size 0x288` + quitar
     `0x82296528` del config US); el **bulk name-only de 298 candidatos NO
     funciona** (gap-fill caótico → nuevos unresolved en 0x82303xxx);
   - funciones por puntero de datos (dispatch) → declarar de a una (0x822A5AE0
     size 8, 0x82290F18 size 0xF8, 0x8228B8D8/E0/F0/B948 cluster,
     0x822922B8..0x82292300 tabla de handlers, 0x821F9D40 size 0x18).
2. **CMake variante** (`CMakeLists.txt`): `DBZ3_GENERATED_DIR` (cache,
   default `generated`). Build EU: `cmake -B out/build/win-amd64-release-eu
   -DDBZ3_GENERATED_DIR=generated_eu` (mismos compiladores/prefix path que el
   US). `main.cpp` incluye `generated_eu/dbz3_eu_init.h` si `DBZ3_EU_VARIANT`
   (define que pone el CMake); `hooks.cpp` se excluye del build EU (sus hooks
   US sub_820F2280/sub_820BB938 no existen en EU) y `hooks.h` se guarda con
   `#ifndef DBZ3_EU_VARIANT`.
3. **Boot EU validado**: el core EU con el xex EU arranca el guest y **ejecuta
   el juego real** (lee `data_eng.afs`, `data_cmn.afs` 3983-3985, `data_yah.afs`;
   60s estable sin FATAL). Iteración de funciones no registradas resuelta hasta
   el guest estable. **El runtime del SDK es agnóstico del juego** (mismas DLLs
   para ambos cores).
4. **Detección consciente de variante** (`settings.h XexIsExpected()` +
   `launcher_state.cpp` banner + `main.cpp` guard skip_launcher): cada core solo
   arranca SU xex; con el del otro lo bloquea con mensaje claro (banner rojo /
   MessageBox). Mensajes i18n añadidos para ambos casos.
5. **Bootstrap dispatch** (`src/bootstrap.cpp`): ahora también detecta el xex
   (`DetectXex` MD5 CryptoAPI, busca `default.xex` en exe_dir, exe_dir/assets,
   parent, parent/assets) → lanza `dbz3_eu_avx2|legacy` si EU, `dbz3_avx2|
   legacy` si US. **Validado end-to-end**: xex EU → dbz3_eu_avx2 → guest EU
   bootea; xex US → dbz3_avx2 → guest US bootea.
6. **Fix de prioridad FindGameRoot** (`main.cpp`): el fallback "3 niveles arriba"
   encontraba el project root dev (con `us/` y `default.xex` US) ANTES que el
   `assets/` del release cuando el core corre desde `github/release-stage/
   dbz3_eu_avx2` → el core EU clasificaba el xex EU como US. Reordenado: exe →
   **parent → 3-up (dev)**. Funciona para dev Y release.
7. **Empaquetado** (`tools/make_release.ps1`): 4 variantes (dbz3_avx2/legacy =
   US core, dbz3_eu_avx2/legacy = EU core; mismas DLL del SDK). Zip v1.0.10
   (66.4 MB).

**Diagnóstico útil**: `InvalidFunctionTrap` (rexglue-sdk-0.10/src/system/
function_dispatcher.cpp) con env `DBZ3_COLLECT_UNREGISTERED` → logea cada
función no registrada a `dbz3_unregistered.txt` y continúa (recolección masiva,
aunque el guest se corrompe rápido → de a una por boot). Volcado de imagen EU:
env `DBZ3_DUMP_IMAGE` (main.cpp) escribe la imagen descifrada para escanear
punteros de función (u32 BE → código) — sirvió para hallar los dispatch targets.

**Binarios**: core US 17529344 B, core EU 17508864 B, rexruntime 10951168 B
(avx2) / 10849792 B (legacy), bootstrap 46592 B. FFX validada intacta
(avx2 5420544 A20438, legacy 5414912 5FE146).

**⚠️ PENDIENTE (usuario)**: validar el core EU **en juego** (menús + combate).
El boot es estable, pero paths más profundos (combate/eventos) podrían revelar
más funciones no registradas (usar `DBZ3_COLLECT_UNREGISTERED` para
recolectarlas) y los hooks US (dispatches mal compilados) podrían tener
equivalentes EU a descubrir. Si algo crashea, iterar el `dbz3_config_eu.toml`
y re-codegen.

**Sync**: `src/{bootstrap.cpp, main.cpp, hooks.h, launcher/{settings.{h,cpp},
launcher_state.cpp, i18n.cpp}}`, `CMakeLists.txt`, `dbz3_manifest_eu.toml`,
`dbz3_config_eu.toml`, `tools/make_release.ps1`, `RELEASE_README.md`,
`README_PRIMER_ARRANQUE.txt`, `patches/.../function_dispatcher.cpp` (recolección
de funciones no registradas) + `patches/README.md` → `github/`. NO subido a
GitHub. `generated_eu/` NO se sube (código derivado). **Release v1.0.10**
empaquetada y verificada (dispatch EU end-to-end OK).

### 14.14 🔴✅ v1.0.6 — FIX DEL CIERRE EN LA INTRO (0xC0000409 / 0x82292A58) (2026-08-26)

**Reportes de usuarios (patrón):** "llegan a la intro del juego y luego les
cierra". Errores: **0xC0000409** (varios) y *"Call to invalid or unregistered
function at guest address **0x82292A58**"*. Un usuario más reportó que
desactivando el V-Sync el juego va super rápido (tarea derivada, ver abajo).

**CAUSA RAÍZ (1) — función no registrada en el intro EU**: `0x82292A58` es un
**thunk de despacho de vtable** (familia `lwz r11,0x48(r3); addi r3,r11,0x40;
lwz r11,0x40(r11); lwz r11,N(r11); mtctr r11; bctr`, tamaño 0x18). El guest EU
lo llama de forma INDIRECTA (vía puntero de dato de una vtable) durante la
intro, y al no estar compilado → `InvalidFunctionTrap` → `REX_FATAL` →
`std::abort()` → **0xC0000409** (en UCRT, abort = fastfail = 0xC0000409). Es el
MISMO crash para los dos reportes (el trap mensaje + el código de excepción).

**REPRODUCCIÓN (sin depender del usuario)**: el intro se reproduce sin input
(el guest avanza solo al título/intro tras ~90s). Con el core EU en una carpeta
de test (core EU + `assets/{default.xex EU, eu/}` + `dbz3_user.toml` con
`dbz3_skip_launcher=true`) el log da exactamente el FATAL:
`[FATAL] Call to invalid or unregistered function at guest address 0x82292A58`.

**⚠️ Diagnóstico**: la vía del colector (`DBZ3_COLLECT_UNREGISTERED=1`) NO
sirve: con la env var puesta el arranque muere con `std::terminate` antes del
intro (quirk de arranque, ver abajo). La vía productiva es estática: **escaneo
de thunks de vtable** en la imagen descifrada (`dbz3_eu_image.bin` via
`DBZ3_DUMP_IMAGE=1`; capstone PPC BE) y registro con size exacto.

**FIX (1)**: `dbz3_config_eu.toml` + `0x82292A58 = { name = "rex_sub_82292A58",
size = 0x18 }` (el hermano `0x82292A40` con offset +8 SÍ estaba registrado; los
3 thunks de la familia 0x44 en 0x82292500/528/550 también). Re-codegen EU
("5 written, 90 unchanged") + rebuild. **VALIDADO**: el core EU pasa la intro
(210 s sin FATAL; antes crasheaba ~90 s). El core US también pasa (210 s).

**CAUSA RAÍZ (2) — `std::terminate` intermitente en el arranque (otro
0xC0000409)**: a veces, justo tras `OnPostLaunchModule`, el hilo UI muere con
`std::terminate called!`. Se diagnosticó con stack (`RtlCaptureStackBackTrace`
en el handler de terminate): la excepción escapa del lambda diferido de
`LaunchModule` (`ExecutePendingFunctionsFromUIThread` invoca la función, y ahí
se lanza). Intermitente (~1/3 bajo carga, 0/6+ en frío). **No se ha podido
localizar el `throw` exacto** (no se reprodujo con el try/catch puesto).
**Medidas aplicadas**: (a) `rex_app.cpp` (se compila en el EXE desde
`rexglue/share/rexglue/rex_app.cpp`, NO en rexruntime.dll — verificar con el
string "Failed to launch module" en el exe) — el lambda de `LaunchModule` se
envuelve en try/catch que loguea `e.what()` y re-lanza; (b) el handler de
`std::terminate` en main.cpp loguea el stack del hilo. Así, si un usuario lo
pilla, el log tendrá el mensaje exacto.

**V-SYNC (tarea para la futura 1.0.6 EX, NO arreglada en 1.0.6)**: reporte de
que desactivar el V-Sync acelera el juego. En 1.0.6 se fuerza la sincronización
correcta en el arranque (vsync=true, ver §14.8), pero la tarea abierta es
encontrar QUÉ ajuste/opción permite desactivarlo (¿menú interno del guest? ¿un
cvar residual?) y blindarlo. Documentado en RELEASE_README "Bugs conocidos".

**Binarios 1.0.6**: core US 17532416 B, core EU 17511936 B, rexruntime
10951168 B (avx2, con trap+collect, sin cambios funcionales) / 10849792 B
(legacy), FFX validada intacta (avx2 5420544 A20438, legacy 5414912 5FE146).
Bootstrap sin cambios.

**⚠️ Constraint de build (repetido)**: al recompilar el juego, el cmake
sobrescribe `rexruntime.dll` con la versión stale de `rexglue/bin` → volver a
copiar `rexglue-sdk-0.10/out/win-amd64/rexruntime.dll` al build tras compilar.

**Sync**: `src/main.cpp`, `src/launcher/…` (sin cambios), `dbz3_config_eu.toml`,
`AGENTS.md`, `RELEASE_README.md` → `github/`. El `rex_app.cpp` editado (try/
catch en LaunchModule) se sincroniza a `rexglue/share/rexglue/rex_app.cpp` y,
si se distribuye como parche, a `github/patches/rexglue-sdk/src/ui/rex_app.cpp`.
NO subido a GitHub. **Release v1.0.6** empaquetada y subida (ver §14.15).

### 14.15 🔴✅ RELEASE v1.0.6 — SUBIDA A GITHUB (2026-08-26)

**Sync**: `src/main.cpp`, `dbz3_config_eu.toml`, `AGENTS.md`, `RELEASE_README.md`,
`patches/rexglue-sdk/src/ui/rex_app.cpp` (nuevo, #12 en patches/README → 17
archivos) → `github/`. Commit `cd4a368`, push a origin/master, **tag v1.0.6**.

**Release v1.0.6** creada en GitHub (Latest):
https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.0.6
Zip `DBZ-Budokai-3-HD-Collection-v1.0.6.zip` (66428050 B) con las 4 variantes
(US/EU × avx2/legacy). Cores: US 17532416 B, EU 17511936 B. FFX validada
(avx2 5420544 A20438, legacy 5414912 5FE146). rexruntime avx2 10951168 (trap +
collect), legacy 10849792.

**Smoke test final del paquete**: core EU empaquetado bootea y corre 100 s sin
FATAL (el intro completo validado con 210 s). Changelog en la release: fix del
cierre en la intro + diagnóstico reforzado + V-Sync anotado para 1.0.6 EX.

**PENDIENTE (usuario)**: validar 1.0.6 en juego (que el intro EU pase). Si algún
usuario vuelve a reportar 0xC0000409, el log ahora trae "LaunchModule deferred
threw std::exception: <msg>" + "terminate stack[...]" → resolver el throw.

### 14.5 🔴✅ P1 PRIMER USO: BANNER DE VALIDACIÓN + SELECCIÓN DE CARPETA + VENTANA DE CRASH (2026-08-25)

**Objetivo (HOJA_DE_RUTA_COMUNIDAD P1.1/P1.2)**: que un usuario con los assets
mal ubicados NO caiga en el crash "Entrypoint XEX not found", y que cualquier
crash muestre una ventana con la ruta del log en vez de cerrarse en silencio.

**Banner de validación en el launcher** (`launcher_state.cpp` OnDraw, tras el
header): comprueba cada frame `default.xex` + `us/`/`eu/` (región actual) sobre
la **raíz efectiva**:
- Verdes `[OK] Datos del juego en: <ruta>` cuando están; rojo con el detalle de
  qué falta (default.xex / us / eu) cuando no.
- **Botón "Seleccionar carpeta de datos..."**: `PickFolder` (IFileOpenDialog),
  valida con `IsValidGameDataDir` (contiene `us/`/`eu/` o `default.xex`),
  persiste en el cvar `dbz3_game_dir` (dbz3_user.toml) y **remonta el juego en
  caliente** sin reiniciar.
- **PLAY se bloquea** (`BeginDisabled`) mientras falten los assets.

**Remontaje en caliente del game data** (`src/region.{h,cpp}`):
- `dbz3::EffectiveGameRoot()`/`SetEffectiveGameRoot()`: raíz efectiva (la que
  contiene `us/`/`eu/`). Se registra en `OnConfigurePaths` y la usa
  `ApplyRegionMount` (antes usaba `rt->game_data_root()`, que es FIJO al Setup y
  queda stale tras un remontaje).
- `dbz3::RemountGameDrive(root)`: desregistra y re-registra el device
  `\Device\Harddisk0\Partition1` en la nueva raíz + re-crea los symlinks
  `game:`/`d:` (mismo patrón que `Runtime::SetupVfs`). `RelocateGameData` =
  remontar + re-aplicar región. Seguro porque el guest aún no ha lanzado.
- `OnConfigurePaths` prioridad: arg CLI > `dbz3_game_dir` (override) >
  auto-detección (exe / assets / proyecto / padre). Si el override no es válido
  se ignora con warning.

**Ventana de crash** (`src/main.cpp` SetupCrashHandler):
- Ante una excepción no controlada: minidump (si Dev "Write crash minidump"),
  log del crash, `rex::FlushLogging()`, y **MessageBox "DBZ Budokai 3 - Error"**
  con código de excepción, dirección, **ruta del log** (`LatestLogPath()`, el
  `logs/dbz3_*.log` más reciente) y ruta del minidump. Con depurador conectado
  delega en el depurador (`EXCEPTION_CONTINUE_SEARCH`).
- `std::terminate` también muestra la ventana.

**Compilado y smoke test OK**: dbz3.exe 17322496 B (25/08 18:23), el launcher
arranca ("launcher shown, waiting for Play"), DLLs 0.10 intactas en el build.
Pendiente: validación visual del banner (verde/rojo + picker) y del diálogo de
crash en el paquete standalone.

**Sync**: `src/{main.cpp, region.{h,cpp}, launcher/{settings.{h,cpp},
launcher_state.{h,cpp}}}` → `github/`. NO subido a GitHub (plan general aún en
curso). Cuando se haga release: añadir `README_PRIMER_ARRANQUE.txt` al zip y
reforzar RELEASE_README.

### 14.4 🔴✅ LAUNCHER: AUTO-GUARDADO DE AJUSTES AL CAMBIARLOS (2026-08-20)

**Problema reportado**: el usuario selecciona la **escala interna** (p.ej. 2x)
o el **efecto de upscaling** (CAS) en el launcher, pero **no siempre pulsa
"Save settings"** → al lanzar o cerrar el launcher, el cambio se pierde y el
juego arranca con la configuración anterior (se percibía "a 720p"). La
persistencia solo ocurría en los botones "Save settings" y "PLAY"
(`launcher_state.cpp` líneas 227-236).

**Causa raíz**: los combos del launcher solo llamaban al setter del cvar
(`SetResolutionScale`/`SetPresentEffect`) en memoria, sin persistir. El guardado
dependía de pulsar "Save settings" o "PLAY", que el usuario no siempre hace.

**Fix** (`src/launcher/launcher_state.{h,cpp}`):
1. **Auto-guardado inmediato al cambiar la escala interna** (DrawVideoTab):
   tras `SetResolutionScale`, se llama `SaveUserSettings()` → el toml
   `dbz3_user.toml` se escribe al instante y `draw_resolution_scale_x/y`
   quedan persistidos. Se aplica en el próximo boot.
2. **Auto-guardado inmediato al cambiar el efecto** (DrawUpscaleTab): ídem con
   `SetPresentEffect`.
3. **Auto-guardado en `OnClose()`** (override nuevo): red de seguridad que
   persiste TODOS los ajustes al cerrar el launcher (botón X o PLAY), cubriendo
   cualquier otra opción que el usuario haya marcado sin pulsar "Save".

**Resultado**: la escala/efecto seleccionados quedan guardados "al marcar", 100%
garantizado sin depender de "Save settings". Confirmado que el toml persiste
`draw_resolution_scale_x/y` (la propagación al plugin GPU funciona vía el
registro compartido + pending values, y el hash del `dbz3_user.toml` de D:
muestra los valores correctos).

**Verificado**: compila (launcher_state.cpp/h) y el `dbz3.exe` nuevo (17285120 B)
desplegado en `D:\Budokai 3` y `github/release-stage/`. `rexruntime.dll`
(11188224 B) correcta en build/release-stage/D: (hash 0A18EFAA, con
`AfsGetVirtualTable` + `AfsFindModFileOverride`).

**Sync aplicado**: `src/launcher/launcher_state.{h,cpp}` → `github/` (commit
`de40780`), push a origin/master, tag `v1.0.4`. **Release v1.0.4** creada
(Latest, zip `DBZ-Budokai-3-HD-Collection-v1.0.4.zip` 17222532 B).
⚠️ Nota git: el push https requería `git config --global credential.helper
"!gh auth git-credential"` (antes colgaba sin helper).

### 14.16 ✅ FIX DEL CRASH DE LA DEMO BATTLE EU + CORE DUAL + RELEASE v1.0.7 (2026-08-26)

**Contexto**: el core EU crasheaba en la batalla DEMO (menú "Press start" en
idle, ~40-50s) con 0xC000001D (UD2) o, tras avanzar, con
`[FATAL] Call to invalid or unregistered function`. La DEMO es el modo de
attract del juego: al dejar el "Press start" sin input salta una batalla AI
3D. El usuario estaba AFK → los tests debían ser automáticos (skip_launcher,
sin input). Se descartó (con evidencia) que el prefijo/la integración causaran
el crash: el core EU-only SIN prefijo también crasheaba → bug preexistente del
codegen EU.

**CAUSAS RAÍZ (4, todas encontradas y arregladas)**:
1. **UD2 en sub_820F2370 (bctr 0x820F2390)**: el guest hace un tail-call
   indirecto real leyendo punteros de función de la tabla 0x8201E348
   (`lwzx r11,r10,r11; mtctr r11; bctr`). El codegen lo auto-detectó como
   **jump table de un solo caso** (`switch(r11){case 0: goto loc; default:
   __builtin_trap();}`) → cualquier puntero real (p.ej. 0x820F24D8,
   registrada) → UD2 → 0xC000001D.
2. **UD2 en sub_820BB8C8 (call virtual)**: vtable en 0x82122B08, index
   `*(r3+82)<<2` → `lwzx; mtctr; bctr`. Misma clasificación errónea
   (single-case switch → trap). Target real del crash: 0x820BB178.
3. **Clobber de r31 (callee-saved) por mis-split del codegen en
   sub_8213E7D0**: el config `dbz3_config_eu.toml` tenía
   `0x8213E834 = { name="rex_sub_8213E834", size=0x1CC }` y
   `0x8213EA00 = { name="rex_sub_8213EA00" }` (añadidos en la iteración EU
   §14.13 para resolver branches "unresolved"). Son bloques MEDIO-FUNCIÓN de
   sub_8213E7D0 (loop header 0x8213E834 y bloque 0x8213EA00), NO funciones.
   El codegen convirtió el `blt 0x8213e834` de sub_8213E7D0 en un tail-call a
   rex_sub_8213E834 **sin epilogue** (no restaura r31/f30/f31/lr) → tras la
   llamada, el loop de la tabla de callbacks de sub_820FCF90 (0x8201ED74)
   leía r31 corrupto (0x8201ED94→0x823EE502) → target basura (0xB8DC45D7,
   0xDC0845A9...) → FATAL. **Fix**: declarar `0x8213E7D0 = { size = 0x304 }`
   (extent real 0x8213E7D0-0x8213EAD4, epilogue `addi r1,r1,0xa0; lfd
   f30/f31; b __restgprlr_28`) y ELIMINAR las 2 entradas de split → sub_8213E7D0
   se genera como UNA función con su loop y epilogue correctos.
4. **Función sin registrar 0x820BB178 (método de vtable)**: la tabla de
   funciones en 0x8201D300 (métodos virtuales, SIN RTTI) no la escanea el
   vtable_scanner (solo C++ vtables con typeinfo). 0x820BB178 se llamaba vía
   bctrl desde sub_820BB418 (`*(0x8201D314)`) y era el único método no
   registrado → `UNREGISTERED indirect call: target=0x820BB178`. **Fix**:
   `0x820BB178 = { size = 0x24 }` en el config.

**Instrumentación que lo resolvió**:
- Crash handler de main.cpp loguea el fault ctx + registros guest (r3/r4/r11)
  en cada excepción no controlada (SE MANTIENE en release, solo loguea en
  crash).
- `InvalidFunctionTrap` (function_dispatcher.cpp) loguea caller_lr + r3/r4/r11
  del call no registrado (SE MANTIENE).
- `tools/fix_eu_bctr.py` GENERALIZADO: regex que reemplaza cualquier bctr
  single-case (`switch(r11){case 0: ...; default: __builtin_trap();}`) por
  `REX_CALL_INDIRECT_FUNC(ctx.ctr.u32); return;` — cubre sub_820F2370 y
  sub_820BB8C8 (y futuros). Re-aplicar SIEMPRE tras re-codegen.
- `DBZ3_DUMP_IMAGE` (main.cpp) para volcar la imagen descifrada y analizar
  tablas/vtables offline; `%TEMP%\opencode\disppc.py` para desensamblar PPC.

**Escaneo de tablas de punteros**: script ad-hoc que recorre la imagen
buscando runs de 3+ punteros de código consecutivos → 186 tablas con miembros
sin registrar, pero la mayoría son case blocks de jump tables (falsos
positivos). Los métodos de vtable reales sin registrar se resuelven de a uno
(con el crash log) o registrando el target del UNREGISTERED.

**VALIDADO EN JUEGO (usuario, varias corridas)**: la batalla DEMO EU completa
pasa sin crash (tanto core EU-only como core dual). El usuario cerró manual
("la demo funcionó perfecto").

**CORE DUAL v1.0.7 (reconstruido)**:
- `win-amd64-dual` (DBZ3_DUAL_REGION=ON): US codegen (generated) + EU codegen
  (generated_eu PREFIXED con `tools/prefix_eu_codegen.py`) → **dbz3.exe
  33881088 B**. main.cpp: `ResolveImageInfo` elige PPCImageConfigEU/US según el
  MD5 del default.xex. El bootstrap solo elige dbz3_avx2/legacy por CPU.
- Verificado automáticamente (skip_launcher, D3D12 forzado, 180s): US y EU
  VIVOS sin crash; log "dual-region core detected US/NA|EU/PAL".
- **⚠️ Al recompilar el SDK, el rexruntime/FFX de `rexglue-sdk-0.10/bin/` se
  regenera distinto — usar SIEMPRE los canónicos de `rexglue-sdk-0.10/out/
  win-amd64` (rexruntime 10951168, rexgpu 6207488, ffx 5420544) y `out/
  win-amd64-legacy` (rexruntime 10849792, rexgpu 6162944, ffx 5414912).**

**RELEASE v1.0.7**:
- `tools/make_release.ps1` reescrito: **2 variantes** (dbz3_avx2/dbz3_legacy,
  MISMO core dual 33.8MB; DLLs v3/v2 de los out del SDK; shared DLLs de
  `out\build\win-amd64-release`), bootstrap nuevo (44544 B, 2-variante), UPX
  opcional `-UpxPath`.
- **UPX -9 del core dual: 33881088 → 6960640 B (20.5%)**. Escaneado con
  Windows Defender (MpCmdRun): **sin amenazas**. Zip 36778594 B.
- **⚠️ Bootstrap stale**: el release-stage tenía el bootstrap VIEJO (46592 B,
  4-variante, despachaba EU a dbz3_eu_avx2 que ya no existe) → el paquete EU
  fallaba sin logs. Reconstruido `dbz3_bootstrap.exe` (44544 B) desde
  `src/bootstrap.cpp` actual y copiado al stage. **LECCIÓN: al cambiar el
  diseño de variantes hay que reconstruir el bootstrap**.
- Verificado el PAQUETE real (bootstrap → dbz3_avx2\dbz3_core.exe UPX):
  US y EU arrancan, "first present OK", sin crash (US 70s, EU 120s).
- PENDIENTE (usuario): subir a GitHub (tag/release) y validar en máquinas
  reales (AV, variantes legacy). El VERSIONINFO del PE sigue pendiente
  (cosmético).
- **⚠️ NOTA DE VERSIÓN (corregida tras la sesión)**: la release empaquetada en
  esta sección se llamó inicialmente v1.0.11, pero la versión real por la que
  vamos es **v1.0.7** (el Latest de GitHub es v1.0.6; los zips locales
  1.0.7-1.0.10 se consolidaron en el tag "1.0.5 EX", commit d629f8c). El
  paquete y el zip se renombraron a v1.0.7 y make_release.ps1 tiene el default
  `$Version = "v1.0.7"`. Verificado además que el paquete, sin tocar nada,
  muestra el launcher y espera Play (no arranca el guest directo).

**Sync**: `src/main.cpp`, `tools/{make_release.ps1, fix_eu_bctr.py}`,
`dbz3_config_eu.toml`, `AGENTS.md` → `github/`. `generated_eu/` NO se sube
(código derivado). El cambio del SDK (logging en function_dispatcher.cpp) se
documenta en `github/patches/` si se distribuye.
