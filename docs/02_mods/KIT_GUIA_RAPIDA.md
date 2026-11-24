# Mod Kit: guía rápida

El Mod Kit es una ventana para hacer mods de DBZ Budokai 3 HD sin escribir comandos.
Cambia trajes, añade luchadores, escenarios y técnicas, y todo se puede deshacer.

> English version: [KIT_GUIA_RAPIDA_EN.md](KIT_GUIA_RAPIDA_EN.md)

**Aviso:** el Kit no trae ningún arte de otros juegos. Lo que uses de Budokai 1, Budokai 2,
Shin Budokai o Infinite World sale de **tu propia copia** (tus ISO). Lo que sea «en pruebas»
funciona en las comprobaciones, pero aún no se ha visto en el juego.

## 1. Cómo se abre

- Si bajaste el pack «Kit de modding»: doble clic en `DBZ3_ModKit.bat`.
- Desde la carpeta del proyecto: `python "mod center hd/modkit_gui.py"`.

Arriba a la derecha hay tres interruptores: **Fácil / Avanzado**, **ES / EN** y **B3 HD / B3 PS2**.

![Portada del Kit](img/kit/real_01_portada.png)

## 2. Modo fácil y modo avanzado

- **Fácil** (el de siempre): seis tarjetas grandes. Cada una te guía paso a paso y te enseña
  cómo quedará **antes** de tocar nada.
- **Avanzado**: todas las herramientas con sus opciones, el editor hex y el registro completo.
  Solo hace falta si sabes lo que buscas.

Abajo de la portada tienes accesos rápidos: **Mis mods**, **Instalar un mod (.zip)**, **Historia o reto**,
**Música**, **Probar en el juego** y **Algo no va bien** (te dice qué hacer).

## 3. Cambiar traje

Para dar otro aspecto a un luchador que ya existe.

1. Pulsa **Cambiar traje**.
2. Elige el personaje (por ejemplo, Goku).
3. Elige de dónde sale el aspecto: **mi archivo de modelo**, **otro personaje que ya tengo**,
   **otro traje del juego** o **solo cambiar colores**.
4. Verás una casilla por cada forma (Base, Kaioken, Super Saiyan...). Pulsa una y elige su modelo,
   o usa «el mismo para todas las formas».
5. Mira las marcas: ✓ está bien, ⚠ usará el de otra forma, ✕ falta y se queda como está.
6. Pulsa **Siguiente**, pon un nombre y guarda.
7. Pulsa **Montar y probar**.

![Cambiar traje](img/kit/real_02_traje.png)

Estado: **en pruebas** (funciona en las comprobaciones, aún sin ver en el juego).

## 4. Añadir personaje

Para traer un luchador de otro juego de Budokai de tu colección.

1. Pulsa **Añadir personaje**.
2. Elige el juego de origen (tus ISO se detectan solas).
3. Elige el personaje.
4. Ponle nombre y elige el ajuste (preset) y el personaje base.
5. Pulsa **Montar y probar**. Aparecerá en la rueda de selección, detrás de los de serie.

## 5. Nuevo escenario

Para añadir un escenario de Budokai 2 o Budokai 1 como casilla nueva del selector.
No cambia ninguno de los de serie.

1. Pulsa **Nuevo escenario**.
2. Elige por la foto (pestañas Budokai 2 y Budokai 1; las fotos salen de tu copia).
3. Fíjate en la etiqueta: «probado» o «sin probar». Los marcados «aún no funciona» cierran el juego y no se pueden elegir.
4. Pulsa **Siguiente**. Pon un nombre (hasta 15 letras) y un ambiente.
5. Pulsa **Guardar**. Se hace con copia y puedes **Deshacer**.
6. Si el escenario tiene una segunda zona, se añade sola. Para verla, activa en los ajustes del juego «zonas B de los escenarios nuevos».

![Nuevo escenario](img/kit/real_04_escenario.png)

## 6. Nueva técnica y generador de animaciones

Cambia el movimiento, la cámara y el color de una técnica. Estado: **en pruebas**.

1. Pulsa **Nueva técnica**.
2. Elige a quién (por ejemplo, Goku).
3. Elige la técnica (Kamehameha, Dragon Fist...). Los nombres son los oficiales de la cápsula.
4. En **Movimiento** pulsa **Abrir el generador de animaciones**.
5. Busca un movimiento de otro luchador en la biblioteca (hay filtros y buscador) y míralo en el visor.
6. Ponlo en la pista de abajo. Puedes recortarlo, juntar varios tramos y elegir la mezcla entre ellos.
7. Mira los avisos verdes: huesos compatibles, duración y golpes dentro del clip.
8. Pulsa **Guardar como técnica** (con copia) y luego **Probar en el juego**.
9. En **Cámara** eliges un estilo (órbita, acercamiento, travelling...). En **Color de aura y ki** cambias el tono.

![Nueva técnica](img/kit/real_05_tecnica.png)
![Generador de animaciones](img/kit/real_06_generador.png)

## 7. Fusiones

Elige cuánto dura cada fusión: **Corta**, **Como el original**, **Larga** o **Sin límite**.
En modo avanzado puedes poner los segundos exactos. No cambia las partidas online.

1. Pulsa **Fusiones**.
2. Mueve el selector general o elige una por una.
3. Pulsa **Guardar**. **Deshacer cambios** devuelve todo como estaba.

Estado: **en pruebas**. El juego necesita la versión nueva para respetar este ajuste.

**Gogeta como personaje suelto** (en pruebas): enciende «Gogeta» y pulsa **Guardar**; sale en la rueda como un luchador más y empieza ya fusionado. Necesita una casilla libre en la rueda; con muchos personajes instalados puede no haberla (entonces el Kit no lo crea y te lo dice). Apágalo y guarda para quitarlo; **Deshacer cambios** lo devuelve. Vegetto y Gotenks: **sin probar en el juego**, no se pueden activar.

![Fusiones](img/kit/real_05b_fusiones.png)

## 8. Pack de texturas

Para cambiar las imágenes de un personaje por las tuyas.

1. Pulsa **Pack de texturas**.
2. Elige el personaje.
3. Pulsa **Extraer texturas a PNG** y abre la carpeta.
4. Edita los PNG con tu programa de dibujo. **Mantén el mismo tamaño** de imagen.
5. Pon un nombre al mod.
6. Pulsa **Reconstruir mod con texturas editadas**.

## 9. Mis mods, probar y deshacer

- **Mis mods**: ves los que tienes, cuáles están activos, y puedes activarlos, apagarlos o deshacerlos.
  (Avisar de conflictos y validar un mod entero: **en pruebas**.)
- **Probar en el juego**: monta el mod, abre el juego y te dice en una frase si cargó. No pulsa nada
  en el juego y **tus partidas guardadas no se tocan**. Si el juego está abierto, te pide cerrarlo.
- **Deshacer**: cada asistente tiene su botón. Lo nuevo se aparta a la carpeta `modkit_backups`;
  nunca se borra nada.

## 10. Lo que aún no se puede

- **Vegetto y Gotenks como personajes sueltos** en la rueda: sin probar en el juego (Gogeta ya se puede, en pruebas).
  Mientras tanto se fusionan en combate.
- En un movimiento copiado, **los golpes y el daño son los del movimiento que sustituyes**,
  no los del que copias.
- **Cinemáticas con cámara** propia (definitivas animadas) no se pueden mover con el generador.
  Tampoco los clips que el juego comparte entre personajes, ni movimientos que el personaje no tiene.
- **Escenarios**: solo de Budokai 1 y Budokai 2, y de **tu copia**. Algunos aún cierran el juego.
- **El arte de otros juegos lo pone el jugador.** El Kit no reparte modelos, imágenes ni escenarios de nadie.

Qué está probado en el juego y qué sigue en pruebas: [KIT_DE_MODDING](../jugador/KIT_DE_MODDING.md).

Más ayuda: [Cómo hacer mods](COMO_HACER_MODS.md) · [Packs de texturas](PACKS_DE_TEXTURAS.md) ·
[Cambio de modelo](MODEL_SWAP.md) · [Studio de cámaras](STUDIO_CAMARAS.md)
