# Escenarios de prueba finales (carpeta `final/`)

Aquí deja cada carril **un** escenario `.toml` con su prueba en el juego (regla (c) del roadmap): clave de la
pieza, cvar que la enciende, criterio y línea de log esperada. La fase de verificación corre la carpeta entera:

```
python tools\pruebas.py paralelo "docs\14_frentes\escenarios_pruebas\final" --max 3
```

- Se ejecutan de 3 en 3, **cada uno en su copia** (`smoke_a/b/c`; `--copias a,b,c` para usar otras).
- Los ficheros que empiezan por `_` no se ejecutan: `_PLANTILLA.toml` es la plantilla.
- `DBZ3_TEST_CFG` (en `--env` o en el `env` del `.toml`) se escribe en el `dbz3_user.toml` de la copia antes de
  lanzar el juego y se quita al acabar: el ajuste llega al juego y la copia no se queda contaminada.
- Ejemplo real: `7d5_botones_original.toml` (IW 0x7D5 con `dbz3_iw_7d5_original=true`, 1843/1853 del dorado).
