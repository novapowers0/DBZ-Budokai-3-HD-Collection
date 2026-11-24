# c4-plataforma — escenarios de la fase 3 (los escribe el carril, NO los ejecuta)

Todos se lanzan con `python tools\pruebas.py correr smoke <fichero>`. Cada uno lleva en la cabecera su
cvar, su log esperado y su criterio. Lo que solo se puede ver en juego queda `PENDIENTE_JUEGO`.

## DR04 (#15) — fundido cruzado del corte de la intro
- `fundido_off.toml` / `fundido_on.toml` (A/B, mismo combate y mismas capturas).
- Cvar `dbz3_fundido_cruzado` (apagada de serie, `src/main.cpp`). Guarda el ultimo cuadro bueno de la
  presentacion (estados 1-3) y lo funde sobre el nuevo cuando la presentacion se va a negro (corte de
  plano o entrada en pelea); `dbz3_fundido_captura_cada=6` fija cada cuantos cuadros muestrea.
- Banco sin juego ya en verde: `studies 5/ENTREGAS/C4PLAT/evidencias/banco_fundido.json`
  (peso del plano viejo MAE 0,569 -> 0,000 en los 15 cuadros medidos del PS2; primera diferencia:
  cuadro 1 -> ninguna; luminancia minima 2,85 -> 102,0; el PS2 mide 119,18 en el corte).
- `PENDIENTE_JUEGO`: pareja de capturas y ajuste fino de `dbz3_fundido_cuadros` (30 de serie; 38 si el
  arranque de la pelea del port es mas lento que el original).

## #59 — retraso del mando
- **APARTADO en la ronda 10/11**: `_input_off_PENDIENTE_SDK.toml` / `_input_PENDIENTE_SDK.toml`
  (empiezan por `_` y no se ejecutan). La sonda y la cola de 1 cuadro viven en `rexruntime.dll`; `compilar.py`
  compila el juego pero NO el SDK, y la DLL de la ronda no las lleva. Falta que Z recompile el SDK baseline.
- Cvars nuevas: `dbz3_baja_latencia` (cola de presentacion a 1 cuadro, D3D12) y `dbz3_latencia_sonda`
  (linea `dbz3 latencia: entrada->present X ms`; dev, sin coste apagada).
- `PENDIENTE_JUEGO` (hardware del usuario, mando fisico): protocolo de camara para la latencia real
  (esto solo mide host): movil a 240-960 fps grabando el mando y la pantalla en el mismo encuadre; 10
  pulsaciones por lado (A/B), se cuenta el primer fotograma distinto tras el contacto del boton y se
  informa la mediana. Un mando fisico, no teclas.

## PLT02 (#35) — ultrancho
- `ultraancho_off.toml` / `ultraancho_on.toml` (A/B, ACTIVOS desde la ronda 6). La ventana se ensancha con
  `window_width/height` (window_sdl los aplica al crearla) y el guest se queda en 16:9 con `video_mode_*` no por
  defecto (si no, `window_*` tambien manda en el modo del guest: era el fallo de la ronda anterior). Se abre la
  pantalla ONLINE, que pinta `hub/fondo_online` con `Backdrop`.
- Medido en la ronda 6: capturas 3200x1350 (2560x1080 con DPI 125 %). OFF: banda negra pura hasta x=400 y fondo
  desde ahi (16:9 centrado). ON: fondo desde x=0 en todo el ancho. Mismo par de capturas en las dos tomas.
- Cvar `dbz3_ui_ultraancho` (APAGADA de serie; `src/ingame/game_style.cpp`). La enciende el coordinador.
  La imagen 3D y el HUD del guest siguen en 16:9: extender la escena pide cambiar su proyeccion (pospuesto).
- `PENDIENTE_JUEGO`: capturas reales en 21:9 y 32:9; informe de FPS en una APU/portatil flojo
  (el usuario no tiene esa maquina esta noche). `dbz3_intel_rapido` (hecho el 9/11, sin publicar) es la
  palanca para Intel UHD; no se inventa un preset de APU sin medirlo.

## PLT01 — Linux
- No hay escenario ejecutable aqui: necesita la maquina Linux del usuario. Ver `PLT03_DECISIONES.md`
  y el informe. Lo demostrable por codigo ya esta: el `Reparar instalacion` de Linux comprueba las
  `.so` y no las DLL de Windows (#62), los hitos de creditos (`creditos_trace.inl`) ya estan puestos y
  el reproductor HD es solo Windows (Linux usa el reproductor del guest). Falta el log real de un
  final de historia en Linux (`dbz3 creditos:` / `dbz3 video nativo:`), que da el usuario.
