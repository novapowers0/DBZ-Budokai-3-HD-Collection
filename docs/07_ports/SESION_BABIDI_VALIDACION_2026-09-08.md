# Sesión Babidi — validación de fuente PS2 — 2026-09-08

## Resultado

La fase no llegó a generar geometría ni mod. La entrada seleccionada no es una
fuente PS2 compatible con el extractor.

| Archivo | Entrada | Resultado descomprimido |
|---|---:|---|
| `ps2_games/Budokai 3 Greatest Hits (USA)/USR/data_cmn.afs` | 96 | `#AMB` + `#AWO` + `#AWG` + `#AZT`, big-endian |
| `ps2_games/Budokai 2 (USA)/USR/data_cmn.afs` | 282 | `#AMB` + `#AWO` + `#AWG`, big-endian |

El extractor `port_ps2_b3_extract.py` esperaba `#AMO0/#AMG` little-endian y
fallaba al interpretar offsets HD como punteros PS2. Se añadió una detección
explícita para abortar con un mensaje claro en vez de producir un traceback de
buffer.

## Conclusión

- No se puede verificar todavía el rig de Babidi PS2 desde estos AFS.
- No se generó ningún JSON, bin o mod.
- No se debe tratar una entrada `#AWO` como si fuera `#AMO0` mediante swaps de
  endianness: son layouts diferentes.
- Próximo paso: localizar una fuente PS2 real (`#AMO0/#AMG`) o recuperar los
  bins PS2 de referencia documentados (`b327_ps2.bin`, etc.).
