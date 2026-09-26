# Sesion 2026-09-26 - Diagnostico que se explica solo (v1.2.9)

> Punto de partida: **los ultimos logs de SSGPrinceVegeta** (`Logs
> SSGPrinceVegeta/parte 4/`, 2026-09-23). Objetivo del usuario: *"una iteracion
> de mejoras, pulido y optimizacion para que NO vuelvan a ocurrir errores como
> los que le pasaban"*.

## 1. Que dicen de verdad esos logs

Barrido de TODAS las lineas `[warning]`/`[error]` de las 4 partes (24 ficheros):

| Linea | N | Estado |
|---|---|---|
| `XThread::Execute - No function registered at 820D54C8` | 4 | Arreglado en v1.2.2 EX (era el `default.xex` de la raiz = menu HD) |
| `Failed to parse config ...: unknown escape sequence '\G'` | 1 | Arreglado en v1.2.2 (escapado) + v1.2.6 (autorreparacion) |

O sea: **en la parte 4 (los ultimos) no hay ni un solo error**. Lo que hay son
tres cosas mas sutiles, y las tres son las que se han atacado aqui:

1. **La instalacion no se puede identificar** (el problema de fondo de todo el
   analisis): las lineas 2-3 dicen
   `user settings loaded from ...\DBZ-Budokai-3-HD-Collection-v1.2.1\dbz3_user.toml`,
   pero el log contiene mensajes que **no existen en v1.2.1** (`fg=` -> v1.2.5,
   `upscale pausado` -> v1.2.6). O sea: carpeta v1.2.1 con DLLs nuevas. Se
   perdio una ronda entera de diagnostico averiguando *que build* produjo ese
   log.
2. **FPS clavado en 31 sostenido** (`upx` planchado en 665, `max_frame_ms`
   34-50 con `cap=60`): la mitad exacta del limite = **vsync a media tasa** (el
   frame no llega a 16,7 ms). Con `internal_scale=3x` + MSAA + mejora de
   texturas (3x) en una escena pesada. El log no lo dice en ninguna parte.
3. **Disco lento**: `dbz3: io SLOW 42614us afs=data_usi.afs ... pre=12us` (42 ms
   de lectura fisica con solo 12 us de trabajo del host) + `read_avg_us` de
   1,9-9,6 ms y `p95_us=8388` repitiendose. El juego esta en
   `E:\Game Roms\Old PC Games\...` (disco mecanico). Nada en el log lo señala.

Ademas se re-confirmo el **trampa de las DLL stale**: `cmake --build` del juego
copia a su carpeta de salida las DLL de `rexglue/bin` (avx2 y viejas), asi que
cualquier test lanzado sin volver a copiar las canonicas mide otro runtime
(paso dos veces durante esta sesion).

## 2. Que se ha implementado

### Runtime (rexgpu-xenos) - `command_processor.cpp` / `texture_cache.cpp`

- **Telemetria de VRAM** en la linea `perf`: `vram=uso/presupuesto` en MB, leida
  de `IDXGIAdapter3::QueryVideoMemoryInfo` (cache de 1 s). Nota de implementacion:
  el dispositivo de este runtime **no implementa `IDXGIDevice`** (hr
  `E_NOINTERFACE`), asi que el adaptador se conserva vivo en `D3D12Provider`
  (`GetAdapter()`, antes lo soltaba tras crear el dispositivo).
- **Guardia de VRAM** (`lim=2`): con el heap local al 92 % o mas **no se conceden
  upscales nuevos** (una linea `upscale limitado por VRAM (uso X MB de Y MB)`).
  Con la decision cacheada por key, como el resto del presupuesto: si cambiara
  entre la creacion del recurso Nx y sus recargas, el recurso quedaria sin
  rellenar.
- **`lim=` en `perf`**: motivo del ultimo bloqueo del presupuesto (0 = ninguno,
  1 = racha de recargas/video, 2 = VRAM). Un log de usuario ya dice *por que* dejo
  de mejorar texturas.
- **Aviso de fps sostenido** (SIEMPRE activo, max 3 por sesion): si el fps se
  queda por debajo del 55 % del limite efectivo (`frame_cap`, o 60) durante 3
  ventanas seguidas **y** hay ajustes caros activos (escala > 1x, MSAA o mejora
  de texturas), sale
  `dbz3: aviso - fps 31.0 sostenido con limite 60 (config: escala 3x3 msaa=1
  mejora_texturas=1) - el frame no llega al intervalo de presentacion (vsync a
  media tasa); baja la escala interna a 1x, ...`. Sin ajustes caros no avisa: en
  un equipo modesto ir por debajo de 60 es esperado, no un error.

### Runtime (rexruntime) - `afs.cpp` / `host_path_file.cpp`

- **Aviso de disco lento** (SIEMPRE activo, UNA linea por sesion, no depende de
  `dbz3_io_logging`): cuenta lecturas fisicas >= 50 ms y a la quinta avisa con el
  volumen y el peor caso: `dbz3: aviso - 5 lecturas de disco lentas (peor caso
  42000 ms, volumen E:) ...`. El reloj de la lectura se toma siempre (2 llamadas
  de ~20 ns por lectura fisica); los aciertos de la cache de readahead no cuentan.
- Esto cubre el caso 3 sin que el usuario tenga que activar nada: el log normal
  sigue limpio y solo aparece la linea cuando hay algo accionable.

### Launcher - `update_check.cpp` + `launcher_state.cpp`

- **Sello de version del runtime**: las DLL **no llevan VERSIONINFO**, asi que
  cada una publica su build por cvar (`dbz3_runtime_build` / `dbz3_gpu_build`)
  desde el nuevo `rex/dbz3_build.h` (`DBZ3_RUNTIME_BUILD`). El registro de cvars
  es compartido, asi que el launcher las lee directamente.
- **Comprobacion de instalacion mixta**: compara major.minor.patch del exe con
  cada componente; un componente sin sello (build viejo) **tambien** cuenta como
  mezcla (es justo el caso "exe nuevo sobre carpeta vieja"). Si hay mezcla:
  banner naranja arriba (con el fichero culpable) + `[warning]` en el log + linea
  en el tab Dev. La AMD FidelityFX se lista pero no se compara (es de terceros).
- **Linea `dbz3: entorno ...`** (una, al arrancar el launcher): sistema real
  (`RtlGetVersion`, no `GetVersionEx`), RAM y **todas** las versiones instaladas:
  `dbz3: entorno os=10.0.26200 ram=32589MB dbz3.exe=1.2.9.0 rexgpu-xenos=1.2.9
  rexruntime=1.2.9 amd_fidelityfx_dx12.dll=1.0.1.0`. Un reporte pasa a ser
  concluyente sin pedir otra ronda.
- **Tab Dev -> "Versiones"**: las mismas versiones en pantalla, con el aviso de
  mezcla si la hay.
- **Aviso de combinacion** en Video: con escala interna > 1x **y** mejora de
  texturas activada, se explica en la propia opcion que el coste se multiplica y
  que si baja a 30 hay que dejar la escala en 1x (es la causa mas habitual del
  reporte "va lento con texturas HD").

### Herramientas

- `tools/copy_sdk_dlls.ps1`: copia las DLL canonicas del SDK baseline al build y
  avisa si el sello no esta. Existe para que no se repita la trampa de las DLL
  stale (que costo dos tests invalidos en esta sesion).
- `tools/verify_release.ps1`: comprueba que `DBZ3_RUNTIME_BUILD` coincide con
  `src/version.rc` y que las DLL del stage llevan el sello (si no, la deteccion
  de instalacion mixta avisaria en TODAS las instalaciones nuevas).

## 3. Evidencia (local, RTX 4070 SUPER)

| Prueba | Resultado |
|---|---|
| Instalacion coherente (todo 1.2.9) | `dbz3: entorno ... dbz3.exe=1.2.9.0 rexgpu-xenos=1.2.9 rexruntime=1.2.9` y **sin** aviso de mezcla |
| Mezcla REAL: exe 1.2.9 + `rexgpu-xenos.dll` de 1.2.8.2 (extraida del zip de la release) | `rexgpu-xenos=? rexruntime=?` + `[warning] instalacion mixta: rexgpu-xenos no coincide con dbz3.exe ...` + banner |
| Partida con mejora de texturas (3x + MSAA + `hd_tex=3`) | 60,0 fps, 0 errores, `vram=2171MB/11231MB lim=0 upx_dyn=0 texload~1050` |
| Guardia de VRAM (fuerza bruta temporal) | `upscale limitado por VRAM (uso 545 MB de 11231 MB)` + `lim=2` + `upx=0`, 60 fps sin errores |
| Aviso de disco/fps (umbrales temporales) | `5 lecturas de disco lentas (peor caso ... ms, volumen C:)` (1 vez; con el umbral real el peor caso seria >= 50 ms) y el aviso de fps 3 veces como maximo |

## 4. Limites conocidos (honestos)

- El aviso de fps **no distingue** "vsync a media tasa" de "GPU saturada de
  verdad": dice que el frame no llega al intervalo de presentacion y que se baje
  escala/MSAA/texturas. Con `perf` activado, `vram=`+`lim=`+`texload=` permiten
  separar VRAM / comandos / GPU.
- La deteccion de mezcla solo funciona **a partir de 1.2.9**: un fichero sin
  sello se reporta como "no se puede identificar" (y eso ya es un aviso util),
  pero no se puede decir de que version es.
- El sello se sube a mano junto con `src/version.rc`; `verify_release.ps1` es el
  que lo vigila.
- El aviso de disco no se puede probar en local con el umbral real (SSD): se
  valido con un umbral temporal de 1 us y se restauro el de 50 ms.

## 5. Como comprobarlo

```powershell
# 1) Copiar las DLL canonicas (SIEMPRE tras compilar el juego)
powershell -ExecutionPolicy Bypass -File tools\copy_sdk_dlls.ps1
# 2) Instalacion coherente: linea `entorno` con las tres versiones iguales
powershell -ExecutionPolicy Bypass -File tools\long_run.ps1 -Action Start -Seconds 45 `
  -Label entorno -Overrides "dbz3_skip_launcher=false"
# 3) Partida con la mejora activada: `vram=`/`lim=` en la linea perf
powershell -ExecutionPolicy Bypass -File tools\long_run.ps1 -Action Start -Seconds 180 `
  -Label hd -Overrides "dbz3_texture_upscale=3;dbz3_hd_textures=3"
powershell -ExecutionPolicy Bypass -File tools\long_run.ps1 -Action Status
```
