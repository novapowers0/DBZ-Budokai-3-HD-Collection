# F1: escenarios de disparo IW — R4, 2026-11-10

**Ejecutados: 3/3 PASS en USA, con una ventana como maximo.**

## Alcance

- `7d7.toml` y `7d8.toml`: rango original, entradas y cierre; no cambian Intro.
- `7de.toml`: activa `dbz3_iw_disparo_intro_original=true`, ademas de rango y
  logica originales. Exige activacion, Ready con reloj100/setup1 y llegada a Aim
  antes de probar entradas/rango/cierre. El ajuste nuevo esta OFF de serie.
- Cada escenario envia tres pulsaciones X (B del mando por configuracion),
  captura imagen y exige logs del clasificador y cierre. Freezer requiere
  FALLO/rango -1; Piccolo y Cell no presuponen victoria. FALLO de partida puede
  ser el resultado esperado de un escenario PASS.

`DBZ3_TEST_CFG` se escribe realmente en el TOML de la copia; en la evidencia
`ejecucion.json` conserva la configuracion efectiva. Los escenarios anteriores
solo activaban rango: sus PASS no probaban la nueva entrada Cell.

## Comandos autorizados de esta ronda

```bash
python tools/compilar.py
DBZ3_PRUEBAS_COPIAS=smoke_b python tools/pruebas.py paralelo "docs/14_frentes/escenarios_pruebas/final/f1-iw-disparo" --max 1
```

El primer intento sin seleccion de copia espero `smoke_a`, ocupada por otro
frente, sin ejecutar escenarios. Se uso la opcion existente de copia `smoke_b`.
La carpeta `final/` no se recorre recursivamente: pasar esta subcarpeta exacta.
WF/include ya estan integrados; no reaplicar el antiguo diff de F2.

## Resultados conservados

- Compilaciones: **2/2 PASS**, codigo0; segunda version usada en los escenarios.
- 7D7: **PASS**, 255,3 s, `D:/DBZ3HD/_pruebas/20261110-163738_smoke_b_7d7/`.
- 7D8: **PASS**, 191,6 s, `D:/DBZ3HD/_pruebas/20261110-164154_smoke_b_7d8/`.
- 7DE: **PASS**, 64,2 s, `D:/DBZ3HD/_pruebas/20261110-164506_smoke_b_7de/`.
  Log: ELF USA, clips1300/1303, Ready reloj100/setup1 y Aim/setup1.

## Pendientes y limites

Camara, actores y preparacion limitada siguen en el port; los servicios de
presentacion interceptados no reproducen integralmente HUD/voz/sonido. No se
ejecuta SetupRound completo sobre mundo EE. No se ha probado en esta ronda juego
EU, START/Setup doble, comparacion dirigida ON/OFF, mando fisico ni impactos o
victoria dirigidos. Las capturas no certifican equivalencia audiovisual.

Los bancos offline EU/US (incluido init+clasificador: Freezer3960/3960,
Piccolo1320/1320, Cell1302/1302 por region) tienen autoridad separada de esta
ejecucion C++. Motor completo y dorados B8 siguen pendientes. Detalle en
`studies 5/ENTREGAS/ESTADO_F1.md`; informe R4 de cinco lineas en
`E:/DBZ3HD_trabajo/coord/log/ronda2/r4_solF1.txt`.
