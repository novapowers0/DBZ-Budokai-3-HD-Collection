# Análisis de rendimiento — logs de SSGPrinceVegeta (parte 2, 2026-09-18)

> Reporte: el mismo usuario con **RTX 5090 + 9950X3D** (el del crash del menú HD)
> dice que ahora **le va MUY lento**. Logs en `Logs SSGPrinceVegeta/parte 2/`
> (`dbz3_012.log` … `dbz3_021.log`, sesiones de 7 s a 199 s).

## 1. Configuración detectada en los logs

| Ajuste | Valor observado |
|---|---|
| Versión | **v1.2.1** (ruta `DBZ-Budokai-3-HD-Collection-v1.2.1`) |
| Backend | `d3d12` |
| Escala interna | **`internal_scale=3x`** (9× píxeles) |
| MSAA | `msaa=true` (2x nativo) |
| Aniso | `aniso=5` (16x) |
| Efecto | `fsr` (quality), `fsr_sharpness=0.2` |
| Vsync / cap | `vsync=true`, `cap=60` (en otra sesión `cap=165`) |
| Preset | `ultra` (que en el launcher es **2x**, no 3x: el 3x es manual) |
| Audio | endpoint **`CABLE Input (VB-Audio Virtual Cable)`** |
| Ruta del juego | `E:\Game Roms\Old PC Games\...` (unidad sin confirmar) |
| GPU elegida | `NVIDIA GeForce RTX 5090` ✅ (no la iGPU) |

No hay ni un `[warning]`/`[error]` en las sesiones de juego (014-021).

## 2. Qué contienen los logs (lo que sí se puede medir)

Prácticamente **el 100 % de las líneas son lecturas AFS**:

- `dbz3_020.log`: 1.718 líneas / 145 s. De ellas, 833 `AFS OVERRIDE MISS` +
  ~830 `AFS OVERRIDE LOOKUP` (330 `data_usi`, 273 `data_cmn`, 201 `adx_usa`,
  26 `lang_usa`).
- **Bursts de hasta 480 líneas en 10 s** (≈24 lecturas AFS/s), y durante las
  transiciones una **cadencia casi perfecta de 50 ms** (20 Hz) repitiendo la
  misma entrada.
- Intervalo mediano entre líneas: 0,000 s (ráfagas back-to-back).

Es decir: los logs **no permiten cuantificar la lentitud** (no registran FPS ni
tiempos de frame). Solo dejan claro que el juego machaca el AFS.

## 3. Hallazgos accionables

### 3.1 El log de overrides AFS era incondicional (arreglado)

`AfsFindModOverride` (`rexglue-sdk-0.10/src/filesystem/afs.cpp`) escribía
**2 líneas con la ruta completa del host por CADA lectura AFS**, incluso sin
mods y sin diagnóstico. Con las ráfagas medidas (cientos de lecturas por
transición) eso son miles de líneas y formateo + I/O por sesión.

**Cambio**: los mensajes (`LOOKUP`/`HIT`/`MISS`/`mod_dir`) ahora solo se emiten
si **`dbz1_diag_logging`** está activo (modo Dev/diagnóstico). Efecto: logs
legibles y menos trabajo por lectura. (No era la causa principal de la lentitud,
pero era coste y ruido evitables, y escondía cualquier otro dato útil.)

### 3.2 Instrumentación de rendimiento (nueva)

Cvar **`dbz3_perf_logging`** (default `true`), medida **en el swap real del
guest** (`D3D12CommandProcessor::IssueSwap`, `rexgpu-xenos`): escribe una línea
cada 5 s con los FPS del juego y el peor frame del intervalo:

```
[gpu] dbz3: perf fps=60.0 frames=301 window=5.01s max_frame_ms=20.4
```

⚠️ El primer intento se puso en el **presentador de la UI**
(`D3D12Presenter::PaintAndPresentImpl`) y **no servía**: ese presentador solo
pinta el launcher (0 líneas en partida, y tampoco con la ventana oculta/off-screen
porque el present en juego depende del enmarcado `kConnectedPaintable`/`WM_PAINT`).
El contador del **swap del guest** mide el límite de frame del juego real y
funciona también **sin ventana visible** (imprescindible para las pruebas
automáticas).

Complemento: los ajustes que importan quedan registrados al arrancar la partida
(`applied runtime settings -> internal_scale=... msaa=...`) y el presentador de la
UI mantiene su propia línea (con `cap=`) para medir el launcher.


### 3.3 Hipótesis a verificar con el log nuevo (por orden)

1. **Escala interna 3x + MSAA** (config manual por encima del preset "ultra",
   que es 2x). Es la palanca de coste más directa del emulador: 3x = 9× píxeles
   de render + resolves de EDRAM; con MSAA, más. Prueba: **1x y 2x**.
2. **Dispositivo de audio virtual (VB-Audio Virtual Cable)**: un endpoint
   virtual puede bloquear el hilo de audio y arrastrar al guest. Prueba: salida
   de audio real (o subir el buffer).
3. **Unidad del juego (`E:` "Old PC Games")**: si es HDD, el AFS (286 MB) +
   `adx_usa` en streaming producen tirones/cargas lentas. Prueba: SSD.
4. **Backend**: D3D12 es el recomendado (Vulkan es ~6,5× más lento en
   `IssueSwap`; ver `PLAN_1.1.1.md`).
5. CPU del guest: el recompilado va en un hilo; con un 9950X3D no debería ser el
   cuello, pero la línea de perf lo dirá (fps bajos con `max_frame_ms` estable =
   coste sostenido; picos = stalls de carga/audio).

## 4. Qué se ha cambiado en el runtime

| Fichero | Cambio |
|---|---|
| `src/filesystem/afs.cpp` | Log de overrides condicionado a `dbz1_diag_logging` (por nombre, el cvar vive en otro módulo). |
| `src/graphics/d3d12/command_processor.cpp` | Contador de FPS del guest en `IssueSwap` (`dbz3_perf_logging`). Medición real en partida y sin ventana. |
| `src/ui/d3d12/d3d12_presenter.cpp` | Contador del presentador de la UI (launcher) con `cap=`. |
| `src/graphics/d3d12/texture_cache.*`, shaders | Upscale de texturas **desactivado** (`dbz3_texture_upscale=1`); ver `TEXTURAS_HD_RUNTIME_UPSCALE.md`. |

DLLs recompiladas e instaladas en `out/build/win-amd64-release/`
(`rexruntime.dll` 10.870.272 B, `rexgpu-xenos.dll` 6.184.448 B) y copiadas a
`github/patches/rexglue-sdk/`.

## 5. Petición al usuario (SSGPrinceVegeta)

1. Actualizar al build con el log de perf y **pasar los logs** de una sesión
   normal (con la config que use habitualmente).
2. Probar, midiendo con la línea `perf`: **escala 1x** y **2x** (con y sin
   MSAA), y decirnos con cuál va bien.
3. Decir si "lento" es en **menús, peleas, cargas o todo**, y si es caída de FPS
   o **cámara lenta**.
4. Probar con un **dispositivo de audio real** (no el cable virtual) y, si el
   juego está en HDD, moverlo a **SSD**.

## 6. Tests sintéticos (offscreen, 2026-09-18)

Arnés: **`tools/hidden_run.ps1`** (lanza el juego con la ventana fuera de
pantalla —no oculta, porque ocultarla detiene el present—, aplica overrides al
`dbz3_user.toml`, espera, mata el proceso, restaura el toml y resume el log).
Receta base: `dbz3_skip_launcher=true` (+ `dbz3_perf_logging=true`), 20-45 s por
caso. **Cuidado**: los valores de texto en el toml van entre comillas
(`dbz3_quality_preset="manual"`); sin comillas el parser **descarta el fichero
entero** y el juego arranca con los defaults (el log solo lo dice con un
`[error] Failed to parse config ... expected 'false', saw 'fs'`).

| Test | Config | Resultado |
|---|---|---|
| T1b | skip_launcher, diag OFF | 0 líneas `AFS OVERRIDE`, 0 errores, 37 líneas de log |
| T2 | skip_launcher, `dbz1_diag_logging=true` (directo) | 0 líneas → **el flag lo re-escribe el launcher** (`dbz1_diag_logging = dbz3_diag_logging && dbz3_dev_mode`) |
| T3 | `dbz3_dev_mode=true` + `dbz3_diag_logging=true` | **16 líneas `AFS OVERRIDE`** ✅ (gate OK en ambas direcciones) + FPS del guest |
| S1-S5 | escala 1x / 2x / 3x / 3x+MSAA / 4x | **60,0 FPS en todas** (`max_frame_ms` 19,7-22,4) en la pantalla de título (RTX 4070 SUPER) |
| U1 | `dbz3_texture_upscale=2` | `DBZ3 texture upscale pipeline ready (x2)` + 60 FPS + 0 errores ✅ (el feature aparcado inicializa bien) |
| P1 | `dbz3_perf_logging=false` | 0 líneas `perf` ✅ |

**Conclusión del barrido**: la **pantalla de título no es GPU-bound** ni a 4x, así
que no discrimina configuraciones. Para medir de verdad hay que hacerlo **en
pelea** (donde está el coste): repetir el barrido con la línea `perf` mientras se
juega, o pedir al usuario una sesión de pelea con los logs.

## 7. Resultado en combate real (2026-09-18, RTX 4070 SUPER)

Combate completo **CPU vs CPU** a `internal_scale=2x` (`dbz3_082.log` con MSAA
ON, `dbz3_083.log` con MSAA OFF; ~6,5 min de combate cada uno, 78-79 ventanas
de 5 s):

| Sesion | Config | fps min / mediana / max | peor frame | ventanas <58 fps |
|---|---|---|---|---|
| dbz3_082 | 2x + MSAA ON | 50,7 / **59,9** / 60,1 | **782 ms** (1 vez) | 1 |
| dbz3_083 | 2x + MSAA OFF | 51,1 / **60,0** / 60,1 | **773 ms** (1 vez) | 1 |

Conclusiones:
- **MSAA no cambia nada** en esta maquina a 2x (ambos a 60).
- El resto del combate es **60,0 FPS solido** (`max_frame_ms` alternando 16,7 /
  19-20 ms = jitter normal del pacing).
- **Unico evento real**: un frame de **~0,78 s** justo al empezar la pelea (la
  *primera* ventana de cada sesion), es decir una **carga/traba puntual**
  (texturas/modelos/escenario del combate), no lentitud sostenida. Si los
  usuarios reportan un tiron al empezar, investigar esa ruta de carga.
- Nota: en `dbz3_083` el toml salvado tenia `msaa=true`; el usuario lo apago en
  el launcher y el runtime lo re-aplico al pulsar PLAY (por eso el log tiene
  dos lineas de `applied runtime settings`, la segunda ya con `msaa=false`).
