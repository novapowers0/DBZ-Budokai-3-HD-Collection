# Formas, ki, física y transformación de los personajes nuevos

Claves de `personaje.toml` añadidas el 2026-10-06 (agente de formas; guía completa en
`D:\DBZ3HD\work\ghf\formas\CLAVES.md`). Las implementan `mod center hd/roster_build.py`,
`mod center hd/capsulas.py` y `src/roster_ext.cpp`. Se editan en el Mod Kit: **Personajes →
«Formas, física y aspecto»** y en la lista de cápsulas (**«Fijar ki»**).

## Reglas del juego (RE de B3, como Gohan adulto)

- Transformarse **no gasta ki**: exige **tener** N barras (1 barra = 1000; máximo 7).
- P+K+G salta a la forma más alta posible para la que haya ki y cápsulas.
- Con menos de 1 barra, un golpe te devuelve a la forma normal.
- Cada forma tiene un nivel de ki al que tiende estando quieto (`ki_base`).
- El runtime solo puede **reducir** las formas del donante (copia el registro de cada forma).

## Contrato de `formas` (2026-11-09)

`roster_build.py` **escribe siempre** `formas` en el `roster.toml` generado (el runtime lo usa para
recortar las filas del donante que sobren). Reglas:

- **Sin `formas` declarado**: se usan las formas del donante. Si el traje tiene **menos modelos que
  formas**, las formas sobrantes **comparten el último modelo** (el runtime limita el modelo de cada
  forma al último del traje); no se recortan. Para fijar cuántas hay, declara `formas`.
- **Excepción**: un traje de **1 solo modelo** y sin `modelo_forma` (o con una sola entrada) usa
  **1 forma**; si no, el donante dejaría una transformación con su cápsula para una forma sin modelo
  detrás (caso Super 17 con el Androide 17).
- **`formas_donante` / `formas_de`** mandan sobre `formas`.
- Una cápsula propia de transformación para una forma que el personaje **no tiene**
  (`forma >= formas`) se descarta: no se pone en `capsulas`, ni en `capsulas_forma`, ni en `hereda`,
  y el log lo avisa.

## `[personaje]`

| Clave | Valor | Por defecto | Qué hace |
|---|---|---|---|
| `formas` | 1..8 | las del donante | número de formas (no más que el donante). Con menos modelos que formas, las formas comparten el último modelo |
| `modelos_por_traje` | entero | 1 | modelos de cada traje, en orden (4 formas, 1 traje: `modelos = [normal, ssj, ssj2, pu]`) |
| `modelo_forma` | lista | ver nota | modelo del traje que usa cada forma, p. ej. `[0, 1, 2, 3]`. Si hay tantos modelos por traje como formas no hace falta |
| `ki_base` | lista de barras 0..7 | las del donante | barras a las que tiende cada forma; Gohan adulto `[3, 4, 4, 5]` |
| `fisica` | `"donante"` o ID | ninguna | física de cadenas del pelo y de las colas del cinturón. **Sin ella los modelos nuevos quedan rígidos** |
| `transformacion` | `"donante"` | — | pone en el moveset propio la P+K+G del donante (0x2E0/0x3E0, destello, grito) y **quita** la abajo+E de Shin Budokai |
| `moveset` | lista | la del donante | con un solo fichero, todas las formas lo usan |

## `[[capsula]]` de transformación

| Clave | Valor | Qué hace |
|---|---|---|
| `tipo` | `"transformacion"` | |
| `forma` | 1.. | forma a la que lleva; exige la cápsula anterior de la lista |
| `ki` | barras 0..7 | barras que hay que **tener** (byte +15 del `#SKC`, en décimas). Sin la clave, las de la cápsula nativa del donante para esa forma (si no tiene, 5) |
| `equipada` | `false` | fuera de la lista «Original» (máximo 7 cápsulas) |

Línea de comandos (la usa el Mod Kit):
```
python roster_build.py capsulas --mod port_gohan_futuro --ki 6 5        # capsula 6 pide 5 barras
python roster_build.py capsulas --mod port_gohan_futuro --anadir "Super Saiyan" transformacion 1
```

## Física de cadenas (qué necesita el modelo)

- Colas del cinturón: `*_LOBI1 → *_LOBI2 → *_LOBI3 (→ 4)` encadenados por **primer hijo**, colgando
  de `*_OBI`; igual con ROBI. El pelo (`HAIR1-3`) es opcional: si falta, esa cadena se salta.
- Huesos `WAIST`, `LLEG1`, `LLEG2` para las colisiones. Se buscan por **sufijo**.
- La física parte de la pose de reposo del modelo (las colas de PSP están horizontales: sin
  comprobar en el juego).

## Ejemplo: Gohan del Futuro (4 formas)

```toml
[personaje]
donante = 4
modelos = ["modelos/t1_normal.bin", "modelos/t1_ssj.bin", "modelos/t1_ssj2.bin", "modelos/t1_pu.bin"]
modelos_por_traje = 4
formas = 4
ki_base = [3, 4, 4, 5]
fisica = "donante"
transformacion = "donante"

[[capsula]]
nombre = "Super Saiyan"
tipo = "transformacion"
forma = 1
ki = 4
```

El importador (`importar.py`, fuentes `sb1`/`sb2`/`sdbh`) ya escribe `formas`, `transformacion`,
`fisica` y las cápsulas de transformación con los nombres de las del donante cuando el moveset de
Shin Budokai se porta con `sbport.py`.

Requiere el exe con el `roster_ext.cpp` nuevo; uno antiguo ignora `modelo_forma`, `ki_base` y
`fisica` sin cerrarse.
