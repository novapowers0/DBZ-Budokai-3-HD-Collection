# Inventario físico y de artefactos — 2026-09-08 (ACTUALIZADO 2026-09-09)

> Medición del árbol de trabajo. Este documento no autoriza borrados: separa
> datos funcionales, referencias históricas y artefactos regenerables.

## Resumen (tras limpieza 2026-09-09: ~46 GB → ~28.4 GB)

| Área | Tamaño aprox. | Decisión inicial |
|---|---:|---|
| `ps2_games/` | 10.58 GB | Conservar: referencias PS2/NGC/PSP usadas por RE |
| `out/` | 5.10 GB | Limpiar por subcarpeta, nunca borrar globalmente |
| `rexglue-sdk-0.10/` | 2.35 GB | Conservar: SDK activo y sus builds |
| `us/` | 2.29 GB | Conservar: assets US originales |
| `modding resources/` | 2.23 GB | Conservar: recursos de modding |
| `eu/` | 2.08 GB | Conservar: assets EU originales |
| `mod center/` | 1.60 GB | Conservar: herramientas externas |
| `modding resources discord/` | 0.86 GB | Inventariar; no borrar duplicados aún |
| `modding resources update 2/` | 0.65 GB | Inventariar; no borrar duplicados aún |
| `modding resources update/` | 0.27 GB | Inventariar; no borrar duplicados aún |
| `rexglue/` | 0.12 GB | Conservar: SDK 0.10 instalado (linkea el build del juego) |

## Borrado ejecutado 2026-09-09 (aprobado por usuario)

| Elemento | Tamaño | Razón |
|---|---:|---|
| `out/analysis/corpus/.work/` | 16.18 GB | Caché de bins extraídos (81.3k); regenerable con `corpus_scan.py`. Se conservan `corpus_all.db` + JSONs |
| `rexglue-sdk/` (0.9 histórico) | 1.66 GB | Supersedido por `rexglue-sdk-0.10/`; las DLL canónicas avx2 viven en 0.10/out/win-amd64 |
| `rexglue_0.9/` | 0.20 GB | Respaldo del SDK 0.9, ya migrado |
| `out/build/_archivo_builds/` | 0.25 GB | Builds antiguos (sdk-test, relwithdebinfo) regenerables |
| `out/build/_archivo_dlls/` | 0.06 GB | Backups de DLL obsoletos |
| Duplicados exactos en `modding resources discord/tutorials/` | ~5 MB | PDF/DOCX ya presentes en `update 2` |

## `out/`

| Subcarpeta | Tamaño aprox. | Estado |
|---|---:|---|
| `build/win-amd64-release/` | 2.44 GB | Build principal; conservar (incluye `mods/` con 50+ mods desactivados, útiles como referencia) |
| `build/_archivo_mods/` | 2.49 GB | Archivo de experimentos; conservar por decisión del usuario |
| `build/win-amd64-dual/` | 0.11 GB | Build dual; conservar hasta validación del release |
| `analysis/` | ~0.02 GB | Corpus: solo DB + JSONs (los bins se regeneran bajo demanda) |

## Artefactos regenerables detectados

- `out/build/win-amd64-release/`: 5 AFS, 3 SFD, 6 DLL, 3 EXE, logs y dumps
  pequeños. Los AFS/SFD son funcionales y no deben borrarse como limpieza.
- Se encontraron 8 BMP, 71 logs, 10 TMP y 1 BAK en el árbol medido. Los logs y
  TMP son candidatos a rotación después de guardar los relevantes.
- Los archivos individuales más grandes son imágenes/discos de `ps2_games`,
  assets `us/`/`eu/` y copias ya archivadas de mods. No son basura por tamaño.

## Próxima limpieza física segura

1. Comparar hashes de `out/build/_archivo_mods/` contra los mods activos y
   comprimir el archivo, sin eliminarlo todavía.
2. Revisar `out/build/_archivo_builds/` y eliminar solo builds que no estén en
   la matriz de validación.
3. Rotar logs antiguos y borrar únicamente BMP/TMP sin valor diagnóstico.
4. Auditar duplicados de `modding resources*` antes de mover o borrar cualquier
   recurso.

## Prohibiciones

- No borrar `us/`, `eu/`, `ps2_games/`, `rexglue-sdk-0.10/` ni `mod center/`.
- No borrar `out/build/win-amd64-release/` ni sus AFS/SFD funcionales.
- No modificar `active_region/` si aparece: se regenera durante el arranque.
