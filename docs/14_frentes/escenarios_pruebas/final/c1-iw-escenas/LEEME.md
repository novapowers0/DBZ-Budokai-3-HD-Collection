# c1-iw-escenas — escenarios de fase 3

Carril C1 (IW-V04, IW-E02, IW13). Los deja escritos el carril; **los ejecuta Z** en la fase 3.
Cada `.toml` se juega en su propia copia (smoke_a/b/c):

```
python tools\pruebas.py paralelo "docs\14_frentes\escenarios_pruebas\final" --max 3
```

| Fichero | Pieza | Qué captura |
|---|---|---|
| `escenas.toml` | IW-V04 | 7DB (NB013): escena MIND & TIME, aura SS de reposo |
| `7dc_escena.toml` | IW-V04 | 7DC (NB014): escena y Gohan adolescente SSJ |
| `bubbles.toml` | IW-E02 | 7D1: WCF/colisión, cámara y partículas |
| `lava.toml` | IW-E02 | 7D9: predicado de contacto y superficie |
| `kikoho.toml` | IW-E02 | 7DE: cono de Kikoho (935/6 + malla 935/1) |
| `ast02_boca.toml` | IW-E02 | 7D5: boca por evento tipo 8 (hoy NO conectada) |
| `ast07_tecnica.toml` | IW-E02 | 7D5: efecto de técnica tipo 4 (hoy NO conectado) |

Las capturas del port se llaman `t01_…`, `t02_…` para que `tools/comparar_iw_lote.py` las empareje con las
del original (`b4_0x<clave>_*.png`, de `tools/iw_visual_original.py`). Criterio: **SSIM ≥ 0,45 y pHash ≤ 18**
en el mismo cuadro; `lado_<clave>.png` deja el original | port | diferencias.

Bancos sin juego que ya están en verde (no repetir): ver `studies 5/ENTREGAS/C1_IW_ESCENAS_ANEXO.md`.
No hay cvar nueva; se usan `dbz3_iw_7db_original`, `dbz3_iw_7dc_original` y `dbz3_iw_logica_original`.
