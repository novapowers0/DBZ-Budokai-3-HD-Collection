# Integración de herramientas de modding al entorno HD

> Auditoría inicial: 2026-09-08. No se han movido ni borrado herramientas.
> Se conservan las fuentes PS2 como referencia y se exponen solo los flujos
> validados para X360/HD.

## Principio

`mod center/` mezcla herramientas PS2, GameCube, Shin Budokai, B3/IW y X360.
`modding resources*` mezcla recursos, tutoriales y herramientas de
Xenoverse/SDBH. No son un toolkit homogéneo. Copiarlo entero al release
introduciría formatos erróneos, runtimes Python duplicados y ejecutables no
relacionados con HD.

La integración debe tener tres niveles:

| Nivel | Contenido | Acción |
|---|---|---|
| `hd/` | Herramientas validadas sobre B3 HD/X360 | Integrar y documentar |
| `bridge/` | Conversores de entrada hacia el pipeline HD | Adaptar con pruebas |
| `reference/` | PS2/SLXS/BPL/IW y herramientas históricas | Conservar fuera del flujo HD |

## Candidatos HD

| Herramienta | Estado | Integración propuesta |
|---|---|---|
| `xbcompress.exe` / `xbdecompress.exe` | Validada | Wrapper con `/N:2048`, magic y tamaño |
| `swap_b3.py` | Validada | Instalador de override por entrada |
| `texture_b3.py` | Validada | Pipeline AZT/DXT3/BC2 |
| `awg_to_obj_b3.py` | Validada | Exportador principal B3 HD |
| `awg0_export.py` | Validada | Verificación de formatos A/C |
| `awg_cara_export.py` | Validada | Verificación de AWGs faciales |
| `afs_scan.py` / `stage_analyze.py` | Validada para RE | Auditores, no editores destructivos |
| `catalog_b3.cat` + `data_cmn_map.txt` | Datos del proyecto | Catálogo estructurado/versionado |

## Candidatos bridge

| Herramienta/recurso | Entrada | Utilidad HD | Trabajo necesario |
|---|---|---|---|
| `Model-Rig Extractor` | PS2/Budokai | Labels, huesos y correspondencias | Salida JSON, sin escribir AWO |
| `EMD/ESK → FBX` | SDBH/Xenoverse | Geometría fuente | Validar ejes y nombres |
| `EMD to AMG` / `OBJ to AMG` | PS2 AMG | Etapa intermedia | Separarla del empaquetado HD |
| Blender 2.78 FBX bridge | FBX | Edición de fuente | Entrada opcional documentada |
| `parse_ps2_mesh.py`, `pose_matrix.py`, `rig_mapeo.py` | PS2 | Investigación del port | JSON reproducible |

## No presentar como HD

- SLXS Editor, BPL Editor y editores SLUS: estructuras PS2, no tablas del XEX HD.
- AMO/AMG/AMT packers, Model Part Editor y Bone Addition Tool: escriben PS2,
  no `#AWO/#AWG/#AZT` de X360.
- Shin Budokai, GameCube y herramientas IW PS2: referencia o bridge, no flujo HD.
- `analyze_bin_hd.py`: parser histórico de layout PS3, obsoleto.
- `build_awo_v20.py`, `build_awo_v22.py`, `build_awo_from_json.py` e
  `inject_a18*.py`: experimentales; no son flujo de entrega.

## Recursos con valor

### Alta prioridad

- `modding resources update/`: listas B3/GH, IDs de cápsulas y breakdowns de
  `data_usa.afs`; consolidar en `docs/03_formatos/` sin borrar originales.
- Tutorial X360 de compresión: cruzarlo con el uso validado de LZX `/N:2048`.
- Tutorial B3HD de texturas: cruzarlo con `texture_b3.py` y AZT.
- Investigación Discord: bin lists, AFL y breakdowns para RE, no automatización HD.

### Prioridad media

- Notas `Infinite World to Budokai 3 Moveset Ports`: conservar correspondencias,
  sin asumir conversión directa IW→B3 HD.
- `EmdFbx-and-FbxEmd-LibXenoverse`: evaluar como bridge de geometría.
- `lean bone tutorial`: extraer documentación útil, no su runtime Python completo.

### Baja prioridad

- SDBH World Mission (`.emm/.emd/.emb/.esk/.ean`): fuente para candidatos de port.
- ZIP/RAR, vídeos, PDFs y ejecutables de Discord: catalogar por hash antes de duplicar.

## Limpieza e integración

1. Excluir `__pycache__`, `.pyc`, `Temp`, logs y outputs de tutoriales.
2. No copiar runtimes Python embebidos; usar Python del proyecto para bridges.
3. Añadir a `mod center hd/` solo scripts fuente pequeños, CLI y rutas relativas.
4. Cada herramienta integrada debe declarar entrada, salida, región, formato,
   compresión y reversibilidad.
5. Calcular hash y registrar origen antes de mover o deduplicar recursos.

## Primer entregable

`mod center hd/tools_manifest.json` ya contiene las categorías `hd`, `bridge` y
`reference`. Validarlo con:

```powershell
python "mod center hd/tools_manifest_check.py"
```

El siguiente paso es añadir wrappers para compresión, exportación OBJ, texturas,
swaps y verificación. `mod center/` seguirá siendo el archivo completo de
referencia.
