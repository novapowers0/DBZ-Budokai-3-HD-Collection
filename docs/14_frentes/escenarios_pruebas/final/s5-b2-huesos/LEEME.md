# S5 B2 huesos: sin escenario .toml (verificación por el arnés)

La pieza B2-19 (IK de suelo de B2) se verifica con el arnés, no con un .toml:

```
python tools\b2_huesos.py parejas
```

Corre `tools/solb2x_lanzamiento.py` para las 29 parejas de la rueda (cada personaje con el siguiente),
una ventana a la vez. Resultado de la ronda 5: **29/29 PASS** (`E:\DBZ3HD_trabajo\S5\parejas_29.json`).
Cada PASS exige `dbz3 B2 HD: inicio por colision real`, las dos líneas `dbz3 B2 IK: lado` (ficha de cada
lado) y `dbz3 B2 IK suelo: actor=...`.

Motivo de no dejar .toml: el motor B2 solo arranca con un golpe fuerte contra una pared y el DSL de
`pruebas.py` no puede escribir las posiciones de los luchadores; el arnés sí (las deja junto a la pared y
pulsa L+P). Los intentos de .toml (tablero, Base y Templo, con `DBZ3_TEST_ESCENA_VOLCADO=1`,
`DBZ3_TEST_B2_MOTOR=1`, rival quieto y L+P) llegan a golpear pero no a la pared, y quedan apartados en
`docs/14_frentes/escenarios_pruebas/apartados/s5-b2-huesos/` con su respaldo.

Ajuste: `ik_b2` (token de `dbz3_original_on`, ya encendido de serie). La escala por ficha
(`dbz3_b2_ik_escala_ficha`) sigue APAGADA; no se enciende desde este carril.
