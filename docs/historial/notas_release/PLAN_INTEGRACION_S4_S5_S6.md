# Plan de integración: studies 4 (15-16), studies 5 y studies 6

Fecha: 2026-10-18. Nada aplicado todavía por este plan; espera luz verde.

## Dónde estamos (comprobado parche a parche)

| Bloque | Estado en el proyecto |
|---|---|
| s4-15 (arreglo del cierre del selector con música, sonda) | **Ya puesto** en el código. Sin compilar. |
| s4-16 (sonido de definitivas, Spike Hell, roster) | **Ya puesto** en tu instalación (con copias .bak). Los 3 zips de Nova's, sin subir. |
| s5 01-03 (nave, IW, escenarios) | **Pendiente**: 7 parches que aplican limpios. |
| s5 04 (equilibrio del Héroe) | **Choca** con s6-03 en `src\heroe.cpp` (vida y daño). Se funden a mano: se suman las dos mejoras (prestigio y puntos de habilidad). |
| s6 01-05 (assets, creador, Héroe, SDBH/FighterZ, Kit y pantalla ancha) | **Ya puesto** en el código. Sin compilar. |

Riesgo principal: hay mucho código nuevo que **nunca se ha compilado junto** (solo se revisó la sintaxis).
Por eso se compila una sola vez con todo y se prueba antes de tocar tu instalación.

## Fases

**F0. Copia de seguridad** de `src`, `mod center hd`, `awo_tools` y los mods instalados en `D:\_PARA_BORRAR_CLAUDE`.

**F1. Aplicar studies 5** en orden 01 → 02 → 03, y fundir a mano el parche del Héroe (04).

**F2. Compilar todo una vez**, recopiar las DLL y pasar las 5 pruebas básicas. Si algo no compila, se arregla aquí.

**F3. Pruebas en copias** (`D:\DBZ3HD`), una ventana cada vez y nunca con tu partida. Un enjambre Haiku lee los logs y yo decido.
- s4-15: 16 escenarios (tablero B2, IW, revancha aleatoria, el selector con música que ya no debe cerrarse).
- s4-16: la pasada de la rueda (40 casillas). **Decide el arreglo de los nombres vacíos**: o todas las casillas nuevas (arreglo barato) o solo las que comparten nombre (más caro).
- s5: 11 escenarios (nave, tablero, misión 13 y 77 de IW, g1/g5 de B2, Héroe).
- s6: 14 escenarios (creador, historia del Héroe, manos/texturas de SDBH, pantalla ancha).

**F4. Arreglos que salgan de F3**: nombres de la rueda, suelo negro de g5 y lo que falle.

**F5. Datos nuevos, primero en una copia y luego en tu instalación:**
- reimportar los 7 ports `imp_iw_*` con sus voces de IW, más Gogeta SS4 nuevo; Janemba no se toca;
- reconstruir los escenarios de IW y B2 (colisión y escala 1:1);
- montar la historia del Héroe (8 sagas, 25 combates, maestros y relevo en Tag Team);
- reconstruir el roster.

**F6. Tu revisión a ojo y oído** (lista corta en cada RUNBOOK):
- voces de los ports de IW y definitivas con sonido;
- nombres de la rueda;
- creador y Héroe;
- pantalla ancha;
- nave y tablero.

**F7. Publicar la 1.5.2** y los zips de Nova's, **solo si me lo dices**.

## Lo que queda para otra ronda (estudiado, con coste)
- **Mecánicas de IW que B3 no tiene**: ataque cargado y dash (baratos), agarre y burla (medios), voltereta y vuelo rápido (caros).
- **Tablero de B2**: panel de aliados, ficha del premio, sonidos IECS y selección nativa (medios); modo 2 jugadores y Shenron (caros).
- **Creador**: 10 pelos sin localizar, caras y ropa suelta.
- **Kit v2**: biblioteca de animaciones con buscador y recetas de moveset (guía `GUIA_KIT_V2.md`).

## Decisiones tuyas (pongo mi recomendación)
1. **Historia del Héroe**: montarla tras probarla en copia. *Recomiendo sí.*
2. **Manos "en reposo" para los ports de SDBH**: solo en importaciones nuevas, sin rehacer los oficiales. *Recomiendo así.*
3. **Quitar "EXPERIMENTAL" de la nave** y dejar "BETA". *Recomiendo sí.*
4. **Publicar** los 3 zips de Nova's y la 1.5.2 al final de F6. Lo decides tú.

## Coste
- Uso semanal al 89 % (tope 95 %). F0-F3 caben ahora.
- F4-F5 pueden necesitar esperar al reinicio del 22/10.
- Las pruebas de F3 piden el PC libre durante un buen rato: dime cuándo.
