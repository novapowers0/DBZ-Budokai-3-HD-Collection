# F2 — escenario 7DD integrado; 7D1 pendiente

## Vigente — ronda6, 11/11
- Único escenario activo: `7dd.toml`. PASS 1/1,169.1s con `--max 1`.
- Comprueba continuidad CPU, captura original CBC9 aplicada al jugador/clip5,
  fin del aviso, tabla de derrota, confirmación real con Space y resultado false.
- Ajustes CPU7DD/captura/aislamiento ON solo en el TOML; siguen OFF de serie.
- Evidencia: `D:/DBZ3HD/_pruebas/20261111-010455_smoke_a_7dd/`.
  USA:t5283,88.050s; aviso terminado t5434,reloj152; resultado8804cs.
- El intento anterior falló por faltar la confirmación de la tabla en el TOML;
  se conserva en `D:/DBZ3HD/_pruebas/20261111-000542_smoke_a_7dd/`.
- PASS funcional, no aceptación de la distribución temporal: quedan nueve partidas
  originales en condiciones emparejadas. No habilitar de serie con este humo.
- `_7d1.toml` sigue apartado por falta de referencia del motor completo.
  `_7dd.toml` conserva el contrato histórico; no forma parte del lote.

## Histórico — ronda3 y ronda5 (superado en 7DD por la conexión de ronda6)

`_7d1.toml` y `_7dd.toml` conservan sus contratos completos y ajustes de prueba.
El prefijo `_` los excluye del lote automático: **no son pruebas aprobadas**.
Este carril queda sin escenarios activos; un lote vacío no significa PASS de F2.
Respaldo de las versiones activas probadas:
`D:/_PARA_BORRAR_CLAUDE/solF2_ronda3_20261110/`.
El respaldo ronda2 también se conserva.

- Build: `python tools/compilar.py` terminó con código0 y copió exe/DLL.
- 7D1: FAIL100.8s. Aislamiento confirmado; llega a resultado,
  pero falta `dbz3 iwmips campo7d1 completo=1 dif=0` (motor completo pendiente).
  Evidencia: `D:/DBZ3HD/_pruebas/20261110-131957_smoke_a_7d1/`.
- 7DD: FAIL145.5s. Campo aislado y Piccolo continuo al menos hasta tick5200;
  sin reinicios ni avisos de espejo ajeno en esta ejecución. Falta la captura
  original del jugador9: el contrato de aceptación sigue fallando.
  Evidencia: `D:/DBZ3HD/_pruebas/20261110-132138_smoke_a_7dd/`.
- Una invocación del carril con `--max 1`, dos escenarios secuenciales, sin
  repeticiones. La espera de la copia smoke_a fue de unos14min.

`dbz3_iw_campo_aislado=false` de serie; los TOML lo prueban con `true`.
Reactivar por coordinación cuando haya integración y referencia suficientes.
Detalle: `studies 5/ENTREGAS/ESTADO_lF2.md`.

Ronda5: API de tarea/captura7DD entregada a F3 y compilada con código0;
bancos continuos sintéticosEU144/144 yUSA144/144. Nuevo ajuste
`dbz3_iw_7dd_captura_original=false`. Tras conectar el host, añadir su opt-in al
TOML7DD y conservar el contrato. Sigue pendiente la distribución original y la
referencia completa7D1: no reactivar para repetir un fallo con la conexión ausente.
Entrega: `E:/DBZ3HD_trabajo/coord/parches/f3-iw-campo/06_solF2_ronda5_objetivo7dd.md`.
