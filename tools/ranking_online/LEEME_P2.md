# Clasificación de Dragon Budokai Survivors desde el menú (P2 / A15): qué hay que subir a GitHub

**Nada.** La Action actual (`validar.py` + `ranking.yml`) ya sirve para los filtros: cada fila de `survivors/semana.json` y
`survivors/siempre.json` lleva el luchador (`j`), el escenario (`e`) y la dificultad (`df`), y las tablas guardan hasta 2.000 filas
(`TOPE_FILAS`). El juego filtra por luchador, escenario y dificultad en el cliente (`src/ranking_online.cpp`, `DibujarTabla`), así que
NO hacen falta tablas por filtro ni cambios en el repo `novapowers0/DBZ3HD-Ranking`.

Si en el futuro hay más de ~2.000 partidas por semana y se quiere que un filtro estrecho (p. ej. Glaciar + Difícil + Piccolo) tenga
suficientes filas, entonces sí: que `validar.py` escriba además `survivors/<semana|siempre>_<escenario>_<df>.json` (mismo formato que
las dos tablas) y que el juego pida ese archivo cuando haya un filtro puesto. No está hecho; no se necesita hoy.

Prueba sin red: `DBZ3_TEST_SURVIVORS_RANKING=local` pone en Survivors una tabla de 14 filas inventadas (sin hilo de red ni cache).
Textos: es/en/fr/de/it en el código (`M::L`) y pt en `res/ingame_ui/i18n_modos.toml`.
