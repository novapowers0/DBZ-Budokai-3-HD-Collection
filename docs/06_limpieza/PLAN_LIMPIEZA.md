# PLAN DE LIMPIEZA Y REORGANIZACIÓN

> Actualizado: 2026-08-14. Este documento cataloga la limpieza pendiente.
> **NADA SE HA BORRADO AÚN** — este es el plan; se ejecutará con tu aprobación.

---

## 1. RESUMEN DE DESORDEN DETECTADO

| Área | Problema | Espacio |
|---|---|---|
| Builds | 4 builds, algunos dudosos | ~19GB |
| BMPs debug | ~30 frontbuf_*.bmp + black_*.bmp (31.5MB c/u) | ~1GB |
| Backups DLL | _backup_d3d12/_backup_pre_opt/_backup_tracy | ~50MB |
| Mods viejos | 5 mods con AFS completos (293MB c/u) de experimentos pasados | ~1.5GB |
| og_music | EU y US duplicados (mismos ADX/SFD) | ~3GB (redundante) |
| modding resources | 4 carpetas con duplicados posibles | ~4.3GB |
| Crash dumps | 8 archivos crash_*.dmp | ~2.4MB |

---

## 2. PLAN POR ÁREA

### 2.1 BMPs de debug (seguro borrar — ya gateados por Dev mode, 2026-08-19)
**Qué**: `frontbuf_*.bmp`, `black_*.bmp` (31.5MB c/u, ~30 archivos) en el build release.
**Qué son**: dumps de framebuffer del shader dump / debug.
**Acción**: borrarlos. No afectan al juego.
**Riesgo**: nulo.
**Estado**: ✅ los 28 .bmp (~840MB) ya se borraron el 2026-08-19. Además, el toggle
"GPU diagnostic logging" ahora SOLO genera estos dumps si el **Dev mode** está
también activo (fix en `src/launcher/settings.cpp`: `dbz1_diag_logging` se propaga
como `DiagLogging() && DevMode()`), así que no reaparecen en juego normal aunque
el checkbox quede activado por accidente.

### 2.2 Crash dumps (seguro borrar)
**Qué**: `crash_*.dmp` (8 archivos).
**Acción**: borrarlos (son de las pruebas de hoy).

### 2.3 Backups de DLLs
**Qué**: `_backup_d3d12/`, `_backup_pre_opt/`, `_backup_tracy/`, `rexruntime.dll.bak_afstest`.
**Acción**: conservar SOLO el `.bak_afstest` (referencia del runtime). Los demás
  archivar o borrar tras confirmar que el runtime actual funciona.

### 2.4 Builds redundantes
| Build | Acción propuesta |
|---|---|
| `win-amd64-release` | CONSERVAR (principal) |
| `win-amd64-tracy` | CONSERVAR (profiling) |
| `win-amd64-sdk-test` | VERIFICAR si se usa; si no, archivar |
| `win-amd64-relwithdebinfo` | VERIFICAR; probablemente archivable |

### 2.5 Mods viejos (experimentos)
| Mod | Contenido | Acción |
|---|---|---|
| `janemba_v10` | AFS con Krillin ORIGINAL | CONSERVAR (referencia del AFS intacto) |
| `og_music` | Música (funciona) | CONSERVAR, deduplicar EU/US |
| `janemba` | AFS Janemba viejo | Archivar |
| `krillin_1byte` | AFS Krillin variante | Archivar |
| `krillin_afs` | AFS Krillin variante | Archivar |
| `krillin_control` | AFS Krillin variante | Archivar |
| `krillin_test` | Textura | Archivar |
| `krillin_texture` | Textura | Archivar |
| `afstest` | Override prueba | Archivar |
| `goten_body` | Cuerpo Goten (crashea) | CONSERVAR (investigación activa) |

**Propuesta**: mover los archivados a `mods/_archivo/` (fuera de `mods/` para que
el runtime no los escanee), o simplemente mantenerlos `.disabled`.

### 2.6 og_music (deduplicación)
- `us/adx_jpn.AFS` == `eu/adx_jpn.afs`? (728MB c/u, probablemente idénticos)
- `us/` y `eu/` tienen los mismos 4 archivos.
- **Propuesta**: verificar hashes; si son iguales, conservar solo la variante de
  la región que se usa (us).

### 2.7 modding resources (4 carpetas)
| Carpeta | Contenido |
|---|---|
| `modding resources` | Base (modelos, listas, SDBH) — CONSERVAR |
| `modding resources update` | Buzón del usuario — CONSERVAR |
| `modding resources update 2` | Tutoriales/modelos nuevos — CONSERVAR |
| `modding resources discord` | Descargas del Discord — CONSERVAR |

**Acción**: crear un INVENTARIO de qué hay en cada una (evitar duplicados
futuros). NO mover los contenidos aún (riesgo de romper rutas de scripts).

---

## 3. REORGANIZACIÓN DE DOCUMENTACIÓN (ya hecha)

Se creó la estructura `docs/`:
```
docs/
├── README.md                  ← índice general
├── 01_estructura/
│   ├── ARBOL.md               ← qué es cada carpeta
│   └── ESTADO.md              ← qué funciona / qué falla
├── 02_mods/
│   ├── COMO_HACER_MODS.md     ← pipeline de mods
│   └── MODEL_SWAP.md          ← investigación de model swap
├── 03_formatos/
│   ├── AMO_AWO.md             ← formato PS2 vs HD
│   └── BIN_LAYOUT.md          ← layout del bin campo a campo
├── 04_herramientas/
│   └── TOOLS.md               ← inventario de herramientas
├── 05_build/
│   └── COMO_COMPILAR.md       ← compilar juego/SDK
└── 06_limpieza/
    └── PLAN_LIMPIEZA.md       ← este documento
```

---

## 4. TAREAS DE LIMPIEZA (por orden de prioridad)

- [x] **A**: Borrar BMPs de debug + crash dumps (1GB, riesgo nulo) — *ya no estaban*
- [x] **B**: Verificar hashes og_music EU/US y deduplicar — *3/4 idénticos, archivados los duplicados*
- [x] **C**: Archivar mods viejos (mover a `out/build/_archivo_mods/`) — *8 mods archivados*
- [x] **D**: Verificar builds sdk-test/relwithdebinfo (¿se usan?) — *movidos a `out/build/_archivo_builds/`*
- [x] **F**: Mover backups de DLLs a un lugar central — *`out/build/_archivo_dlls/`*
- [x] **E**: Crear inventario de modding resources (4 carpetas) — *`INVENTARIO_MODDING.md`*

### 4.1 Resultado de la limpieza ejecutada (2026-08-14)

```
out/build/
├── win-amd64-release/          ← BUILD PRINCIPAL (4.5GB, funciona)
├── win-amd64-tracy/            ← Build Tracy (10.7GB)
├── _archivo_builds/            ← relwithdebinfo + sdk-test (266MB)
├── _archivo_dlls/              ← backups de DLLs (62MB)
└── _archivo_mods/              ← mods de experimentos pasados (2.7GB)
```

- El build release bajó de 8.3GB → 4.5GB.
- Mods conservados: `goten_body` (investigación), `janemba_v10` (AFS original referencia), `og_music` (funciona).
- Verificado: dbz3.exe, runtime con fix de override, toml intacto.

---

## 5. IMPORTANTE

- **El juego funciona AHORA** (todos los mods desactivados). No romperlo.
- **El `active_region/` se reconstruye en cada arranque** — no tocarlo.
- **`us/` y `eu/`** son los assets originales — NO borrar.
- Los scripts en `awo_tools/` referencian rutas absolutas — NO mover sin actualizar.
