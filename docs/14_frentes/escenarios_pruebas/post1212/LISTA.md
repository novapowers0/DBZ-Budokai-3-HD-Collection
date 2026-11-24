# Guiones de prueba nuevos (12/11) — carril D2-GUIONES

Todos se pasan uno a uno con:

```
python tools\pruebas.py correr <copia> "docs\14_frentes\escenarios_pruebas\post1212\<archivo>.toml"
```

Cada `.toml` lleva arriba su clave, la cvar/entorno, el criterio y las líneas de registro exactas que espera.
Las líneas se sacaron con `Grep REXLOG_INFO` de `src/` (y se cruzaron con los `dbz3.log` de pruebas ya hechas).

## Selector con 238 escenarios

La copia fija `esc_n` se prepara una vez (clona 238 altas reales; lo anterior se aparta, no se borra):

```
python tools\escenarios_copia_n.py preparar --n 238
```

| Archivo | Copia | Qué comprueba |
|---|---|---|
| `sel238_rueda.toml` | `esc_n` | Arranque con 238 altas (`alta 254 = 'COPIA 0238'` y la lista de nombres acaba en 254), rueda continua (`243 escenarios en 16 casillas`), el cursor llega a la última alta (registro 254) y se pelea en ella. |
| `sel238_azar.toml` | `esc_n` | La casilla «?» (12) elige entre TODAS las altas: `"?" entre todos (249)` (11 nativos + 238) y empieza el combate. |

Nota: `hasta_log` repite `A` hasta que la sonda diga el registro 254; con la rueda al revés suele caer en 2-4 pulsaciones, pero el tope es 240 s. El registro 254 = 17 + 238 − 1 (`kSerie` = 17).

## Clima vivo (F1 / POST2)

| Archivo | Copia | Qué comprueba |
|---|---|---|
| `clima_noche.toml` | `smoke_a` | Día → atardecer → noche en la Llanura: sonda de clima, oscurecimiento de noche y capa de estrellas activa (`clima estrellas: activa -1`). Capturas 04/05: cielo azul marino. |
| `clima_lluvia.toml` | `smoke_b` | Lo mismo con `DBZ3_CLIMA_TIEMPO=1`: lluvia desde el primer cuadro de pelea (capturas) + noche. |
| `clima_nieve.toml` | `smoke_c` | Lo mismo con `DBZ3_CLIMA_TIEMPO=2`: nieve (copo mayor con halo) + noche. |

Límite conocido: el emisor de lluvia/nieve (`escenarios.cpp`, `TickClimaNativo`) y el pase del cielo de noche (`rexgpu-xenos`, `dbz3_night.hlsl`) **no escriben línea de registro**; el registro prueba que el clima está activo, oscurece y la capa de estrellas entra, y la lluvia/nieve/cielo se comprueban en las capturas. El clima cuenta cuadros de pelea: no se pone `DBZ3_TEST_RINGOUT` para que dure los 11 s de la noche.

## Tactics: carga y sonido (TACTICS-10 / TACTICS-09)

| Archivo | Copia | Qué comprueba |
|---|---|---|
| `tactics_carga_sonido.toml` | `smoke_d` | La arena pide el combate de fondo al empezar la carga, la precarga de 34 figuras termina con su tiempo, las voces de la ronda quedan listas en su ranura, el combate 6v6 suena (golpes/técnicas/KO, juntados) con **0 gritos sin banco propio** y deja el ritmo medido; al cerrar, la arena se cierra sin cuelgue. |

Usa `DBZ3_TEST_TACTICS_LADO=6` + `VEL=1` + `SIN_FOCO=1` (ronda de demostración 6v6, sin preparación, sin necesitar el foco). La primera vez genera modelos y clips de B3: la precarga espera hasta 300 s.

## Survivors: una partida

| Archivo | Copia | Qué comprueba |
|---|---|---|
| `survivors_partida.toml` | `opus2` | `DBZ3_TEST_SURVIVORS_AUTO=1`: elección de personaje → mapa → partida con piloto → subida de nivel → resultados (`fin resultado=`, victoria o derrota). `DURA=90` comprime la partida a 90 s. |

El primer arranque genera modelos y clips de B3 (minutos): el primer `log` espera hasta 900 s.

## Creador del Héroe, por partes

| Archivo | Copia | Qué comprueba |
|---|---|---|
| `heroe_01_apariencia.toml` | `sol` | Create abierto, `Create: traje N / M` al cambiar de traje y `Create: confirmar (traje N, ...)` al guardar. |
| `heroe_02_nombre.toml` | `sol` | Botón Nombre → rejilla de letras → `heroe: nombre = ...` al aceptar y confirmar la ficha. |
| `heroe_03_tecnicas.toml` | `sol` | Moveset → pestaña Técnicas (`tecnicas abiertas ...`), equipar 2 cartas y `tecnicas equipadas en el combate: +N` al empezar la misión. |
| `heroe_04_deseo_abierto.toml` | `sol` | Con 7 Esferas (`DBZ3_TEST_HEROE_ESFERAS=127`) y aventura empezada: `deseo concedido: cambio de raza/cuerpo abierto`. |
| `heroe_05_deseo_cerrado.toml` | `sol` | Sin Esferas: `raza/cuerpo cerrados: hace falta un deseo de Shenron (7 Esferas; llevas 0)`. |
| `heroe_06_fichas.toml` | `sol` | `fichas abiertas`, crear el 2.º y 3.º (`heroe nuevo en la ficha 2/3` y `3/3`) y tope con `fichas: maximo 3 heroes`. |
| `heroe_07_piezas_b3.toml` | `smoke_c` | Create con cuerpo/traje base (0), cambiar el torso: `pieza B3 torso = ...`, vista previa 3D, Edit y Confirmar. |

Notas:
- `heroe_07` pide cuerpo 0 y traje 0 (`smoke_c` los tiene; `sol` tiene el cuerpo 11 de la prueba de deseo, y ahí la lista de donantes B3 sale vacía).
- `heroe_06` necesita una copia con **1 héroe** (`sol` ahora mismo): crea el 2.º y el 3.º y la deja con 3. Para repetirlo, restaura `user_data/dbz3/cac.toml` de la copia (hay `.bak`) o usa otra copia limpia.
- Los guiones del creador modifican `user_data/dbz3/cac.toml` de la copia (traje, nombre, técnicas, deseo, fichas). No tocan la partida real.

## Resumen de copias

| Copia | Guiones |
|---|---|
| `esc_n` (preparada con `--n 238`) | `sel238_rueda`, `sel238_azar` |
| `smoke_a` | `clima_noche` |
| `smoke_b` | `clima_lluvia` |
| `smoke_c` | `clima_nieve`, `heroe_07_piezas_b3` |
| `smoke_d` | `tactics_carga_sonido` |
| `opus2` | `survivors_partida` |
| `sol` | `heroe_01` a `heroe_06` |
