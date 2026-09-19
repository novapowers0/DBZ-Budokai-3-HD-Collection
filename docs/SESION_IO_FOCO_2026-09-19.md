# Sesion 2026-09-19 (2) — E/S de disco, diagnostico de foco y QoL al perder foco

> Origen: reporte de lentitud de SSGPrinceVegeta (v1.2.1, RTX 5090, `internal_scale=3x`
> + MSAA, audio VB-Audio Virtual Cable, juego en `E:` — ver
> `docs/ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md`). Se investigo **el camino de
> lectura real del runtime** (no solo sus logs) y se cerro con una tanda de
> protecciones + QoL. Release: **v1.2.5**.

## 1. Que dicen sus logs (y que NO dicen)

Sus logs (parte 1 y parte 2) son **v1.2.1** y **no pueden medir la lentitud**: en
esa version el log de overrides AFS era incondicional (2 lineas + ruta completa
por CADA lectura), asi que el 100% son lecturas AFS y no hay ni un dato de
frametime. Cuantificado: `dbz3_020.log` = 1718 lineas / 145 s, rafagas de ~24
lecturas/s y, en transiciones, una **cadencia casi perfecta de 50 ms repitiendo
la MISMA entrada** (`adx_usa.afs entry=4276`, ~0,5-0,9 s entre lecturas: el
streaming de audio del guest). Es decir: comportamiento normal del guest, no un
bucle patologico nuestro. Lo que si era coste real: el log incondicional
(arreglado en v1.2.3, gateado por `dbz1_diag_logging`).

## 2. El camino de lectura del runtime (hallazgos concretos)

`HostPathFile::ReadSync` (`rexglue-sdk-0.10/src/filesystem/devices/host_path_file.cpp`)
es **SINCRONO**: el hilo del guest se bloquea en `FileHandle::Read` →
`ReadFile` durante toda la lectura fisica. Hallazgos:

1. **Se hacia trabajo caro en CADA lectura aunque no hubiera mods**:
   `AfsModsPresent`-style lookups con **clave `std::string` por lectura** (asigna),
   dos locks del indice AFS y, para `data_cmn.afs`, **copia COMPLETA de la tabla
   AFS virtual** (`AfsGetVirtualTable` devolvia el vector por valor) mas
   `AfsFindEntry` + `AfsFindModOverride` que **no pueden encontrar nada** si no
   hay mods.
2. El flag de diagnostico se consultaba con `rex::cvar::GetFlagByName(...) ==
   "true"` (lookup + conversion a string + compare por lectura).
3. **No habia ninguna instrumentacion de tiempos de E/S** en el producto: era
   imposible distinguir "disco lento" de "overhead del host".
4. `FileHandle` abre con `FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS`
   (sin `NO_BUFFERING`: la cache del SO funciona; sin `SEQUENTIAL_SCAN`, que
   deliberadamente **no** se anadio: el guest relee la misma entrada de audio,
   y ese hint tira las paginas por detras).

## 3. Cambios (v1.2.5)

### Runtime (SDK)
- **`dbz3_io_logging`** (default true) + **`dbz3_io_slow_ms`** (25): resumen cada
  5 s (`dbz3: io reads=… phys=… cache=… mb=… pre_avg_us=… read_avg_us=…
  p95_us=… p99_us=… max_us=… slow=… opens=…`, percentiles por histograma log2)
  y una linea por lectura lenta. Es la unica fuente de tiempos de E/S del
  producto → sin ella, un reporte de "va lento" no es medible.
- **Camino rapido sin mods**: `AfsModsPresent()` → si no hay mods se saltan
  entero el lookup de overrides y la tabla virtual (nada puede coincidir).
- **`AfsGetVirtualTableFast`** (puntero al vector cacheado, sin copia) y
  `AfsFindEntry` una sola vez por lectura.
- **`dbz1_diag_logging` leido como bool** (`REXCVAR_DECLARE`+`REXCVAR_GET`) en
  vez de `GetFlagByName` por lectura.
- **`AfsIoRecordOpen`**: cuenta aperturas de fichero (una tormenta de opens en
  un disco lento es una senal).
- **Lectura anticipada secuencial** (`dbz3_io_readahead`, `dbz3_io_readahead_kb`
  = 2048): 4 streams LRU por fichero; si el guest lee de forma consecutiva, se
  lee un bloque mayor de una vez (256 KB..2 MB, adaptativo) y las lecturas
  siguientes se sirven de RAM. **Solo se activa si NO hay mods** (con mods el
  mapeo byte→fichero cambia) y se invalida al escribir. En mi SSD **no acelera**
  (mismo tiempo total: menos lecturas pero mas grandes); esta pensado para
  **discos mecanicos**, donde convierte N seeks en 1 stream secuencial.

### La pista mas importante: foco de ventana
`dbz3: perf ... fg=0/1` — el estado de foco. Motivo: **Windows/DWM limita a la
mitad la presentacion de una ventana visible sin foco** (firma inequivoca:
`fps=60.0` → `fps=30.0` exacto, no gradual; y con la ventana **fuera de pantalla**
vuelve a 60 porque no se compone). Sin este campo, un log con `fg=0` a 30 fps se
lee como "el juego va lento" cuando en realidad es "el jugador hizo alt-tab".
**No es un bug nuestro** y no se parchea (la unica via seria robar el primer
plano, que rompe alt-tab/OBS/multi-monitor).

### QoL al perder el foco (validado contra otros emuladores)
- **Silenciar el audio** (`dbz3_mute_unfocused`, default ON): la app escribe
  `dbz3_window_focused` en cada cambio de foco (`Dbz3App::OnWindowFocusChanged`)
  y el callback SDL silencia si `dbz3_mute_unfocused && !dbz3_window_focused`.
  Es lo estandar (Dolphin/RetroArch/PCSX2; Unreal lo trae como
  `UnfocusedVolumeMultiplier`).
- **Oscurecer la pantalla** (`dbz3_dim_unfocused`, default ON): overlay ImGui a
  pantalla completa ("Juego en segundo plano" + "El audio esta silenciado." +
  "Vuelve a la ventana para seguir jugando."). Deja claro el estado de un
  vistazo y evita que la pantalla se pueda "leer" desde lejos.
- **NO hay pausa real**: este runtime no tiene un mecanismo de pausa seguro
  (`GraphicsSystem::Pause` existe pero esta muerto y no detiene al guest; la
  alternativa — suspender los hilos del guest — puede colgar). Se deja para otra
  sesion, con pruebas dedicadas.

### Launcher
- Seccion **"Al salir de la ventana"** al principio del tab **Video** (las dos
  casillas), pensada para usuarios no tecnicos, con tooltips que explican el
  comportamiento sin jerga (y aclarando que **el juego sigue en marcha**).

### i18n (ronda completa)
- Auditoria de 270 `i18n::T()` / 271 entradas: se cerraron **2 claves que se
  veian en ingles en IT/DE/FR** (mensajes de "Ejecutable detectado…" y del menu
  HD) y se tradujeron **13 strings nuevas**.
- `GpuTierLabel` ("Low/Medium/High") y `ModTypeLabel` ("swap B3"…) se generaban
  ya en ingles y se pintaban crudos: ahora se traducen en la UI
  (`ModTypeLabelText`, tier inline). El buscador de mods conserva los ids
  crudos.
- Quedan **5 entradas muertas** en `kTable[]` (sliders de musica/SFX/voz
  eliminados en v1.2.4) — cosmetico.
- ⚠️ La cabecera de `i18n.cpp` dice "GENERATED from …" pero **no hay generador
  versionado**: la tabla se mantiene a mano. Conviene recuperar uno en `tools/`.

## 4. Bugs encontrados y corregidos durante la verificacion

- **`out_bytes_read` no se escribia** (regresion propia al reescribir el camino
  fisico: el original pasaba el puntero directo a `FileHandle::Read`). El guest
  recibia bytes validos pero un contador sin inicializar y **se quedaba colgado
  en la pantalla de carga**. Corregido antes de publicar (leccion: al envolver
  una llamada, conservar TODOS sus efectos observables).
- La prueba inicial (offscreen, 60-150 s) no veia I/O porque el opening es un
  video que no pasa por AFS: hay que **llegar al menu** para leer los
  contenedores.

## 5. Verificacion

- Log: `mute_unfocused=true dim_unfocused=true` en
  `applied runtime settings` y eventos `window focused` / `window in the
  background` en cada cambio.
- El overlay ImGui **se dibuja en partida** (captura con el contador FPS activo,
  `Debug##overlay`). La captura de la ventana **sin foco** no es fiable con
  `PrintWindow` (reactiva la ventana), por eso la atenuacion se valida por su
  condicion (cvar + flag de foco) y por el overlay conocido.
- Launcher: seccion nueva visible, sin recortes, con "nivel detectado: **Alta**"
  ya traducido.
- I/O medido (menu, SSD): sin lectura anticipada `phys=112/112`; con ella
  `phys=60 cache=52` en la misma ventana de 5 s.
