# F6 — campana final, pendiente de Z y datos originales

Un TOML por carril segun el plan vigente. `tablero.toml` solo prueba la entrada manual y activacion; pasar ese TOML NO cierra las piezas. En fase1 F6 trabajo sin juego, emulador ni build; la ronda de integracion posterior se detalla debajo.

## Ronda de integracion 10/11
El primer escenario permanecia en `kPrepara`: las capturas00/zz del fallo094444 muestran el cartel de objetivo, sin pulsar A. `SonidosPasosB2` solo se llama en `kJuego` sin escena abierta; esperar180s no podia producir su log. El TOML corregido pulsa Space para empezar, comprueba fase1/T155 y pulsa Return tras superar100cuadros para saltar esa intro. Conserva DBZ3_TEST_CFG y exige el log real de activacion, sin moverlo a una ruta anterior ni activar el piloto. Respaldo del TOML anterior: E:/DBZ3HD_trabajo/F6/ronda2/tablero_antes.toml. Resultado de esta ronda en studies 5/ENTREGAS/ESTADO_lF6.md y coord/log/ronda2/solF6.txt.
Validacion12:06: build autorizado sin errores, escenario1/1 PASS en32,3s (una ventana). Evidencia D:/DBZ3HD/_pruebas/20261110-120621_smoke_a_tablero: fase1 empieza/T155 y una aparicion del log de pasos a12:06:51.782. El arnes retiro la clave experimental del TOML de smoke_a al cerrar. No demuestra aun igualdad de eventos/relojPAL ni las11escenas de imagen.

## Preparacion
1. Z compila `src/pasarela_b2.cpp`, aplica solo los diffs aceptados y registra SHA256 de exe/DLL/guardado/config/log. No mezclar builds entre fases.
2. Copia y guardado propios; controles de la referencia, `DBZ3_TEST_MISIONES=1,reset,desde=N-1`, N de1 a9. Para fase1 basta `1,reset`. Sin `auto`, sin `DBZ3_TEST_RINGOUT`, turbo1.
3. Pasos opt-in `DBZ3_TEST_CFG=dbz3_b2_pasos_estrictos=true`; log de activacion una vez. OFF conserva frontera anterior. La prueba fina14/14 no prueba PAL.
4. Los `DBZ3_TEST_*` son del proceso de prueba, no config de entrega. Z verifica/restaura la copia y no publica esos ajustes. Si falta dato, detener esa aceptacion en PENDIENTE_DATO, no rellenarlo ni regrabar por rutina.

## Matriz que Z agenda (no ejecutada por F6)
| Caso | Accion manual / estado | Criterio / validador posterior |
|---|---|---|
| Fases1/2/3/5/7/8 | Reproducir controles de cada referencia en seis partidas separadas | 6/6: subsecuencia completa, bloque identico, cero extras; sitios/tiempos completos y mismo hash |
| IV/VI/IX | Ganar primer combate con fixture/seed observados, continuar rama | >=3 opcodes distintos POScombate por fase; mismos sitios/argumentos/orden, cero extras |
| Imagen once escenas | Nueve fases + intro + rueda, campo e instancia equivalentes | identidad AMA/anim/cuadro/instancia, cuatro vertices y ancho/alto<=1px; validos/total; ninguna instancia extra/faltante |
| Arte y paneles | Camara/clip por rival, IV/IX y resto con panel | SSIM>=0,6 solo arte comparable, mascaras3D declaradas sin tapar HUD; camara3D medida aparte; nada de ORB |
| HUD | Reserva, M/P/T, rombo/turno, barra/Bolas/radar; transiciones | Consumidor y estado originales; aplicar el mismo criterio de quads, sin borrar marcas por apariencia |
| Requisitos deseos | Partidas por saga, premio/negativo, guardar/cargar | Mismos31 IDs461..489/253/220 en inventario; negativos no habilitan, sorteo/seed demostrados; parcheF7 si se acepta |
| Audio | Tablero/rueda/NaveI/II/III/IV, incluido repetir draw | Misma secuencia canal/id y desfase<=1campoPAL; no convertir cuadros de animacion en campos |
| USA e idiomas | FuenteUSA demostrada:9fases+4plantas; dos idiomasEU | Reloj/pago/texto medidos por region. UK de ISO EU no es USA. Sin fuente: pendiente |

## Comandos de comparacion de archivos (no lanzan el juego)
Siempre sobre los logs ya capturados por Z, por separado:
```
python tools/comparar_modo.py b2 faseN --manual --b2-estricto --log <log_final_faseN>
python tools/comparar_modo.py b2-quads <original.json> <port.json>
python tools/comparar_modo.py b2-sonidos <original.json> <port.json>
```
Sin `--log`, el primer comando lanza el port; `--manual` desactiva piloto, no el lanzamiento.
El formato esta en `E:/DBZ3HD_trabajo/F6/CONTRATO_B2_03.md` y las solicitudes en `SOLICITUD_DATOS_F6.md` de esa carpeta.
Audio exige `b2-sonidos-v1`, regionPAL/hz50, lista completa no vacia y campos enteros. NTSC requiere perfil propio, no escalado supuesto.
Exit0 solo vale para las filas medidas; exit1 diferencia, exit2 pendiente. Ninguno acredita hashes/procedencia o escenas no incluidas.
Estabilidadx3 solo sobre candidato verde; no repetir fallo sin cambio/hipotesis. Sin datos comparables, el estado actual sigue siendo0/11 imagen y PENDIENTE_JUEGO de regresion.
