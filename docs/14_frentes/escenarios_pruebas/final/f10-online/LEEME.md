# F10: captura del cierre con dos ventanas (ronda 6, 10/11)

Ronda 6 autoriza dos ventanas. El unico escenario activo es `06_tareas_luchador.toml`.
Cada lado ejecuta el comando siguiente con `--max 1`, simultaneamente:

```powershell
$env:DBZ3_PRUEBAS_COPIAS="rb_a" # rb_b en el invitado
$env:DBZ3_PRUEBAS_ENV="DBZ3_NET_AUTO=host" # guest en el invitado
python tools/pruebas.py paralelo "docs/14_frentes/escenarios_pruebas/final/f10-online" --max 1
```

`src/net/f10_r6_capture.py --output <directorio-nuevo>` coordina esos dos comandos:
comprueba que los mods sean identicos y espera las dos plazas antes de lanzarlos.
El arnes compartido conserva los candados y cierra solamente sus propios juegos.
El ajuste `dbz3_net_cierre_sonda_f10` queda apagado de serie. La prueba lo enciende
para guardar la cadena PPC y los modulos/offsets nativos antes de salida/purecall;
tambien pide el volcado de fallo existente. No evita la trampa ni repara rollback.

El PASS del arnes no basta: contrastar los 25 resumenes por cada lado, sumas,
rollbacks reales y duracion. Una sala sin rival, una corrida de cache o un combate
corto no acreditan los 25 largos. Si cierra, la captura es evidencia, no un PASS.
Estado: `studies 5/ENTREGAS/ESTADO_F10.md`.

La captura02 del escenario `03_cierre_17840.toml` conecto con mods identicos y
rollback real, pero F10 corto por suma distinta en el tick 5710 (4081 iguales
antes, bloque del segundo luchador). Sus datos quedan conservados y el TOML
se aparta con `_`. No llego al cierre 17840 ni completo ningun resumen.
El escenario 04 cambia la validacion F10 a false solo en las copias para
reproducir las condiciones anteriores a esa proteccion y mantiene la sonda.
Tambien recoge CSV/volcados dirigidos y termina con FAIL al observar un cierre;
el estado desincronizado nunca sirve como aceptacion ni como arreglo.

Captura04 termino los 25 combates por cada lado: 54/54 checks con minimo
4000 ticks, sin sumas distintas ni cierre. No demuestra la causa del cierre.
El escenario 04 se conserva con `_`. El 05 vuelve a activar F10 y anade
`DBZ3_RB_DUMP=1`: `OnSumBadRb` conserva la instantanea exacta de cada lado
antes del corte. Tambien usa el diagnostico WLOG_DIFF existente. Si no falla,
contrastar todos los resumenes; si falla, comparar esos volcados antes de editar.

Captura05: FAIL,22resumenes por lado,44/54checks. Primera diferencia global173118,
relativo5370; los14bytes distintos se reducen a la direccion de tarea en
luch1+0x1AB0 tras aplicar las exclusiones existentes. Faltan los contenidos.
El06 anade `dbz3_net_tareas_sonda_f10=true` (defaultfalse), solo lectura:
guarda nodos del pool en los limites de tick y al resimular, sin restaurarlos.
`DBZ3_RB_DUMP=1` escribe tambien `tareas_{host,guest}_bad<relativo>.tsv`.
La prioridad comprobada de esta tarea es0x5380, callback82118598; aun no se
demuestra que sea una simple diferencia de direccion. SDK/mascaras intactos.
Captura06 termino PASS:25/25por lado,54/54checks, unos53min, sin reproducir.
Evidencia: `E:/DBZ3HD_trabajo/f10-online/ronda6_captura_06/validacion.json`.
La sonda estuvo activa pero no escribio TSV al no haber suma distinta; no hay
prueba viva nueva del contenido del nodo al fallar. No repetir06sin otro cambio
diagnostico; el cierre17840 y el desync05 siguen sin causa/arreglo demostrado.

## Historico: diagnostico de una ventana (ronda 3)

Los dos TOML de sala se conservan como `_01_ajustes_rollback.toml` y
`_02_ajustes_lockstep.toml`; el arnes omite nombres que empiezan por `_`.

```powershell
python tools/pruebas.py paralelo "docs/14_frentes/escenarios_pruebas/final/f10-online" --max 1
```

Dos arranques consecutivos del anfitrion US: primero pide rollback, luego lo
apaga explicitamente. Se comprueba en el log del juego el valor efectivo de las
cvars, la apertura de sala y que sigue esperando sin rival. El arnes cierra
su propio proceso entre escenarios. Cada corrida activa F10; sigue apagado
de serie. No usa sesion vanilla ni cambia los mods de la copia.

Estos PASS solo acreditan ajustes y apertura local: no son partidas, rollback
negociado, cancelacion desde la interfaz, desconexion en combate ni 50/50.

## Cuatro escenarios apartados, sin borrar

Respaldo: `D:/_PARA_BORRAR_CLAUDE/solF10_ronda3_20261110/`.
Contiene los cuatro TOML originales y `LEEME_parejas.md`, sin cambios de contenido.
No se recorren desde esta carpeta. Recuperarlos solo al autorizar dos ventanas
y tras igualar el contenido efectivo de las copias.

El lote anterior mezclo dos parejas: rollback host quedo en cola y su guest
arranco despues de cerrarlo. La pareja de desconexion si conecto, pero ambos
registraron `mods false`: el anfitrion no puede pedir combate en ese estado.
No hay evidencia de fallo de sintaxis TOML. El env archivado acredita lo pedido,
no el valor efectivo; los nuevos escenarios comprueban este ultimo.

Con F10 y el auto de pruebas activos, un bloqueo por mods ahora enumera una vez
los nombres que faltan, sobran o tienen otra version, tras recibir la lista
completa. Conserva el rechazo por contenido distinto.

## Pendiente de aceptacion online

Crash de tick 17840 sin pila ni causa confirmada; 25 largos, 50/50 por ambos
lados, suma alterada con aviso, desconexion real, Buu/Custom/Tag, revancha,
NAT y dos PCs. El banco offline previo (109328/109328) no prueba esos puntos.
Estado de esta ronda: `studies 5/ENTREGAS/ESTADO_lF10.md`.
