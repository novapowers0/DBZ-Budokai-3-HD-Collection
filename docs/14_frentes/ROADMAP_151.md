# Roadmap 1.5.1 (2026-10-17)

Encargo del usuario: dejar pulido el tablero de Budokai 2 (scripts, Saibaman, aparicion de Goku), los entrenamientos de
Infinite World (animaciones), cubrir de extremo a extremo el mapa de IW y los 9 tableros de B2, mejorar el Kit de Modding
(referencia: Budokai Modding Tool y la herramienta "Shin Character Studio" de la captura), mod de Saibaman de colores,
arreglo de red (hecho) y bug de iconos en blanco (US y EU). Informes de los enjambres Haiku: `E:/DBZ3HD_trabajo/R6/`.

## Hecho
- Red del launcher: 3 vias de WinHTTP + TLS 1.2/1.3 + curl de respaldo, version por la redireccion de github.com, Linux
  con curl, "Detalle tecnico" visible (`src/launcher/update_check.cpp`).

## Tablero de Budokai 2 (lo que falla y por que)
Datos de la fase I (volcado `DBZ3_TEST_B2_DUMP=1`): Goku pieza 1, Raditz 15 (sigue a Goku), Nappa 16 (avanza por guion en
los turnos 2, 4, 6...), 9 Saibaman (17-25, quietos). El guion de apertura (301) hace aterrizar las capsulas, **activa** a
Raditz y Nappa (orden 32), lanza una escena (orden 4) y **siembra** los Saibaman con efectos 29-43 en sus casillas (ordenes
18/19/27/28). Los guiones 352-356 hacen brotar mas Saibaman (orden 55 + efectos + 32) cuando una pieza tuya se para en
ciertas casillas (T3 con banderas). La escena 155 (DATA_<idioma> 155) es la intro con Goku.
El port solo ejecutaba 1-3, 38, 44, 50, 55, 57, 63/64, 72/73 y todo a la vez. Pendiente, por impacto (enjambre: 9 fases):
1. Grupos de ordenes (el primer campo es el paso: las del mismo paso a la vez, el siguiente al acabar) y espera (7).
2. Piezas que empiezan ocultas hasta la orden 32 (aparecen) y brote animado de los Saibaman (efectos 18/19/27/28).
3. Escenas: 4/5/6 (dialogos con texto en DATA_<idioma>; el mapeo exacto lo esta sacando un agente) y 41/42.
4. Camara (9, 11) sobre la pieza; mensajes en cola (75/76).
5. Disparadores T3 "pieza parada en casilla" con sus banderas (FUN_0027F7E0).

## Infinite World
- Auditoria (iw_cobertura.md): Dragon Mission 63 portadas, 22 parciales, 11 faltan; Fighter's Road 99/105.
- Personajes que B3 no trae: Goku GT, Baby, Janemba, Vegeta GT, Pan (receta oficial en el Kit), Super 17, Pikkon, Gran
  Saiyaman 2 (sin receta), Gogeta SS4 (falta en `IWaB3`). Hace falta: aviso "requiere descargar el personaje" en la ficha
  de la mision y un Native Mod por personaje (aunque este por pulir) para cubrir todo IW.
- NB014 (Goku contra Gohan): los codigos de pose cuadran con el ELF; el fallo es de colocacion/encadenado (Gohan cae fuera
  de plano en las transiciones). En curso.

## Kit de Modding (kit_bmt.md)
Budokai Modding Tool: Python/Tkinter 2019 sin licencia (solo referencia de formatos, no copiar codigo). Mejoras propuestas:
biblioteca de animaciones con buscador y vista previa, recetas de movesets guardadas (.toml), dano y cuadro de golpe por
movimiento, validacion antes de escribir, copia de seguridad.

## Saibaman de colores
Saibaman = ID 42 nativo de B3. Plan: trajes extra (traje.toml) con el modelo del jugador con el tono girado (sin arte del
juego en el mod) y el tablero elige traje al azar en cada combate contra un Saibaman (`CombateTablero`, `traje_rival`).

## Iconos en blanco en la seleccion (US y EU)
En estudio (agente). Sospecha: las rutas de los iconos generados frente a `HostDataRoot()` en modo ISO.
