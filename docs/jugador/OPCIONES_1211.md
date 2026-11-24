# Opciones nuevas del 12/11 (pantalla, escenarios y clima)

Esta página reúne los ajustes que llegaron el 12/11. Unos están en el menú **F1** (en partida) y
otros solo en el archivo de ajustes, `dbz3_user.toml` (junto al `dbz3.exe`). No hay que tocar
ningún archivo del juego.

| Ajuste | Nombre para el jugador | Dónde | De serie |
|---|---|---|---|
| `dbz3_ultrapanoramico` | Pantalla ultra ancha (21:9, 32:9) | `dbz3_user.toml` | Apagado |
| `dbz3_ui_borde_nativo` | Borde de los menús del juego | `dbz3_user.toml` | Encendido |
| `dbz3_escenarios_238` | Hasta 238 escenarios añadidos | `dbz3_user.toml` | Encendido |
| `dbz3_esc_azar_todos` | El «?» entre todos | F1 > ESCENARIO | Encendido |
| `dbz3_escenarios_no_jugables` | Enseñar escenarios no jugables | `dbz3_user.toml` | Apagado |
| `dbz3_clima` | Clima vivo | F1 > ESCENARIO | Apagado |
| `dbz3_clima_tiempo` | Tiempo del clima vivo | F1 > ESCENARIO | Despejado |

Para editar el `dbz3_user.toml`: cierra el juego, ábrelo con el Bloc de notas y añade la línea en
la zona de ajustes (por ejemplo, al final). Los valores son `true` o `false` en minúscula. Algunos
cambios piden reiniciar; se indica en cada apartado.

## Pantalla ultra ancha (`dbz3_ultrapanoramico`)

El juego está pensado para 16:9, así que en monitores de 21:9 o 32:9 sobran franjas a los lados.
Con esta opción:

- en **combate**, la vista 3D ocupa todo el ancho de la ventana y el marcador se queda pegado a
  sus bordes, sin estirarse;
- en **menús**, la imagen sigue a 16:9 con franjas a los lados, como hasta ahora.

```
dbz3_ultrapanoramico = true
```

Solo cambia la presentación en pantalla; no toca el juego ni los archivos. Una vez cargado el
ajuste, se aplica al cambiar de pantalla o de tamaño de ventana.

Límites conocidos: en la rueda de selección de personaje todavía se ven la greca y los retratos
en su sitio de 16:9, y el centro de la imagen 3D de los menús aún no se recorta. Por eso sigue
siendo una opción para probar.

## Borde de los menús del juego (`dbz3_ui_borde_nativo`)

Cuando «Bordes dorados» está apagado (F1 > IMAGEN), antes desaparecían también las tiras
laterales de los menús del juego. Con este ajuste se conservan como en el original; nuestras
pantallas propias siguen llenando la ventana.

```
dbz3_ui_borde_nativo = true
```

Viene encendido de serie y **se aplica al reiniciar**. Necesita la DLL `rexgpu-xenos.dll` del
12/11 o posterior; con una DLL vieja el ajuste no hace nada.

## Escenarios: 238, «?» entre todos y no jugables

### Hasta 238 escenarios añadidos (`dbz3_escenarios_238`)

Permite cargar hasta **238** escenarios añadidos además de los 17 del juego (antes el tope era
110). La rueda del selector gira sin fin y cada escenario lleva su imagen. Se probó el selector
completo con 238.

```
dbz3_escenarios_238 = true
```

Viene encendido de serie. **Al reiniciar.** Apagado vuelve al tope antiguo de 110.

### El «?» entre todos (`dbz3_esc_azar_todos`)

La casilla «?» (escenario aleatorio) del Duelo elige entre **todos los escenarios visibles**, no
solo entre los 23 que enseña la rueda. En online, los dos PCs eligen lo mismo con la semilla
compartida.

Está en **F1 > ESCENARIO > «El “?” entre todos»**. Viene encendido de serie y se aplica al
momento.

### Escenarios no jugables (`dbz3_escenarios_no_jugables`)

El selector oculta los escenarios añadidos que no sirven para pelear (el interior de la nave, los
campos en vuelo sobre el mar o las nubes y el Camino de la Serpiente; además hay tres sin revisar).
Con este ajuste se muestran igualmente, para mirarlos o probarlos.

```
dbz3_escenarios_no_jugables = true
```

Apagado de serie. **Al reiniciar.** No borra nada: solo deja de ocultarlos. La lista está en
`res/datos/escenarios_no_jugables.toml`.

## Clima vivo: día, atardecer y noche

**F1 > ESCENARIO > «Clima vivo»** (`dbz3_clima`, apagado de serie). Durante el combate, a los
20 segundos de pelea el escenario empieza a cambiar y en 40 segundos llega a la noche:

1. **Día**: como siempre.
2. **Atardecer**: la bruma se vuelve cálida.
3. **Noche**: el cielo se vuelve azul marino, salen estrellas y la bruma lejana se tiñe de azul.
   El suelo y los luchadores apenas se oscurecen, para que la pelea se siga viendo igual.

Es solo visual: no cambia el combate y no se usa en el online.

### Tiempo del clima vivo (`dbz3_clima_tiempo`)

**F1 > ESCENARIO > «Tiempo del clima vivo»**, con «Clima vivo» encendido:

| Valor | Qué se ve |
|---|---|
| Despejado (0) | Solo el paso de día a noche |
| Lluvia (1) | Lluvia con profundidad, detrás de los luchadores y sin tapar el marcador |
| Nieve (2) | Nieve con copos mayores y halo |

Se aplica a los escenarios de Budokai 3. Despejado es lo de serie. En online no se usa.

Límites conocidos: el color del cielo y las nubes de noche aún no son los del original, y la nieve
se ve poco; se sigue trabajando en ello.
