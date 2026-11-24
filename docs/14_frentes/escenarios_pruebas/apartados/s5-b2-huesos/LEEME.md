# S5 B2 huesos: escenarios apartados (ronda 2, 10/11)

Movidos aqui desde `final/s5-b2-huesos/` porque fallan en el juego y no se pueden cerrar con el DSL de
escenarios. Respaldo: `E:\DBZ3HD_trabajo\S5\escenarios_parked_20261110\s5-b2-huesos\`.

## Que se arreglo (escenario, no codigo)
- Faltaba `DBZ3_TEST_ESCENA_VOLCADO=1`: sin el, `g_b2_actor_colision` no se pone y `B2ContactoReal` nunca
  captura la colision -> el motor B2 (donde vive el IK) no arranca. Es la causa del primer fallo.
- Faltaba `DBZ3_TEST_B2_MOTOR=1` para escenas que no son Llanura.
- El lado A pedia la linea `dbz3 B2 IK: lado`, que solo sale con la tabla pie/mano ENCENDIDA; con FICHA=0
  la prueba correcta es `dbz3 B2 IK suelo:`.

## Lo que falta (por que estan apartados)
El motor B2 arranca solo con un golpe FUERTE que lleve a la victima contra una superficie valida
(`B2ContactoReal`: `6196 >= 6192`). Los logs de las corridas 12:22 y 12:24 enseñan la captura funcionando
(`dbz3 B2 consulta REAL: ... superficie=1 entrada=0`), pero con golpes debiles (`umbral=15.000 tiempo=11.000`)
o lejos de una pared. El arnes que si lo consigue posiciona a los dos luchadores junto a una pared antes de
pulsar L+P; el DSL de .toml solo puede pulsar teclas, no escribir posiciones.

## Como validarlo en fase 3 (arnes ya probado)
```
python tools\solb2x_lanzamiento.py --copia smoke_e --grupo 4 --ids 0,0 \
  --original-on ik_b2,caida_pal,pose_ya,capa_b2,idle_b2,ataque_b2,levantar_b2,mirada_b2 \
  --ik-suelo --ik-tabla --huesos --finalizar
```
Ese arnes escribe posiciones/pared, pulsa L+P y llega a "dbz3 B2 HD: inicio por colision real"; con
`DBZ3_TEST_B2_IK_ESCALA_FICHA=1` (o la cvar `dbz3_b2_ik_escala_ficha`) sale ademas
"dbz3 B2 IK: escala de modelo por ficha ...". Para volver a los .toml hace falta un verbo de posicion en
`tools/pruebas.py` (o que el escenario arranque desde un estado guardado ya colocado).

## Ronda 5 (10/11): cubierto con el arnes
`python tools/b2_huesos.py parejas` -> **29/29 PASS** (`E:\DBZ3HD_trabajo\S5\parejas_29.json`).
Tres parejas de la rueda no arrancaban el motor con el atacante natural y se cambió solo el orden/atacante:
Buu M/Buu S (36,35), Piccolo/Krillin (10,11) y Videl/Piccolo -> Goku/Videl (0,15) para cubrir a Videl.
Los .toml `ik_v2_templo.toml` / `ik_escala_ficha_v2_templo.toml` son el ultimo intento (Templo, alta 25):
tambien fallan porque el DSL no coloca a los luchadores junto a la pared. No hay .toml valido; `final/s5-b2-huesos/LEEME.md` lo explica.
