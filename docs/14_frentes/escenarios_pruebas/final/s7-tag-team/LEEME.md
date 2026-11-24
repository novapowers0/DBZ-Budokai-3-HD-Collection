# S7 — escenarios de fase 3 (Tag Team, formas, torneo y gigantes)

Se ejecutan con `python tools\pruebas.py paralelo "docs\14_frentes\escenarios_pruebas\final\s7-tag-team" --max 3`
(la subcarpeta `manual/` NO entra en esa pasada). Todos dejan escrito el criterio y el log esperado en su cabecera.

| Archivo | Pieza | Qué mide |
|---|---|---|
| `tag.toml` | TAG01 + TAG04 (TIME UP) | 2v2, `dbz3_tag_precarga=true`, relevos automáticos cada 2 s y fin por tiempo; el criterio de 20 relevos (máx ≤ 2× mediana) lo comprueba `python tools\s7_tag_bench.py <dbz3.log>` |
| `formas.toml` | TAG02 | Goku sube por sus formas; traza de `sub_8210E100`, `sub_82115330` y de la precarga (`dbz3_forma_precarga=true`). Windows; Linux (#14) es PENDIENTE_LINUX |
| `tag_ko_2v1/2v2/3v3.toml` | TAG04 | KO real del activo con compañero vivo (golpe letal `DBZ3_TEST_TAGKO`), caída y entrada visibles, en los 3 formatos |
| `tag_ringout_2v1/3v3.toml` y `tag_ringout.toml` | TAG04 | Ring out del jugador 1 (`DBZ3_TEST_RINGOUT=2`) convertido en relevo, en los 3 formatos |
| `tag_time_2v1/3v3.toml` y `tag.toml` | TAG04 (TIME UP) | Fin por tiempo en los 3 formatos (`DBZ3_TEST_TAGTIME`); `tag.toml` además mide 20 relevos |
| `kumite_ranking.toml` | TOR01 | Kumite completo y ranking escrito |
| `gigante_off.toml` | GIG02 | Combate normal con el modo apagado (control; sin líneas `gigante:`) |
| `gigante_relevo.toml` | GIG01/02 | Gigante 1.8 + Tag + relevos; FOV y restauración |
| `manual/coop.toml` | TAG05 | Cooperativo con dos mandos (manual; fila 2P COOP con `dbz3_tag_coop=true`) |
| `manual/torneo*.toml` | TOR01 | Torneo Novato/3 niveles (manual: el bracket no se sincroniza con el DSL sin bucles; la prueba automática de TOR01 es `python tools\pruebas.py correr smoke_a torneo`) |

- `DBZ3_TAG_TEST_PICKS=1` reduce la rueda a una elección por lado; el equipo real lo ponen `DBZ3_TAG_TEST`/`DBZ3_TAG_TEST2`.
- `dbz3_combate_prueba_muneco=quieto` deja al rival quieto (menos KO imprevistos) salvo en el gigante, donde el modo no actúa sobre un COM sin mando.
- Cvars nuevas (apagadas de serie): `dbz3_tag_precarga`, `dbz3_tag_coop`, `dbz3_forma_precarga`, `dbz3_tag_trace`, `dbz3_forma_traza`. Fila para Z en `E:\DBZ3HD_trabajo\coord\parches\Z\01_s7-tag-team_quick_settings.diff`.
