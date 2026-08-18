# Árbol del proyecto — qué es cada carpeta

> Actualizado: 2026-08-14. Rutas relativas a `<proyecto>/`.

---

## ARBOL GENERAL

```
DBZ Budokai 3 HD Collection/
├── src/                     ← Código del launcher y del juego (main.cpp, launcher/, ingame/)
├── rexglue-sdk/             ← SDK FUENTE (runtime, GPU, filesystem, kernel) — compilable
├── rexglue/                 ← SDK INSTALADO (bin/lib/include) — lo que usa el build del juego
├── generated/               ← Código recompilado del guest (dbz3_recomp.*.cpp) — 23 archivos ~2MB c/u
├── out/build/               ← Builds del juego (4 configuraciones, ver abajo)
├── docs/                    ← ESTA documentación (organizada por tema)
├── us/                      ← Assets región US (AFS del juego, ~2.4GB)
├── eu/                      ← Assets región EU (AFS del juego, ~2.2GB)
├── afs_out/                 ← Bins de personajes DESCOMPRIMIDOS del AFS (para RE)
├── ps2_games/               ← AFS de B1, B2, B2V, B3 GH, IW (referencias PS2, ~8.4GB)
├── SDBH_body/               ← Extracciones de Super Dragon Ball Heroes (modelos EMD)
├── awo_tools/               ← SCRIPTS de RE y conversión (parse_model.py, build_*.py...)
├── mod center/              ← Herramientas de la comunidad (36 programas, ~1.7GB)
├── mod center hd/           ← Herramientas HD adaptadas/creadas por nosotros
├── modding resources/       ← Documentación + recursos de modding (modelos, listas, arte)
├── modding resources update/       ← Buzón para archivos nuevos del usuario
├── modding resources update 2/     ← Más recursos (tutoriales, modelos)
├── modding resources discord/      ← Recursos descargados del Discord de la comunidad
├── default.xex / yae3_xenon.xex    ← Imágenes del juego (US/EU)
├── CMakeLists.txt, CMakePresets.json ← Config de build
├── dbz3_config.toml, dbz3_manifest.toml ← Config/metadata del juego
├── bin/ tools/             ← Utilidades varias
```

---

## out/build/ — Los builds

| Build | Uso | Tamaño | Necesario |
|---|---|---|---|
| `win-amd64-release` | **El build principal** (dbz3.exe, juego jugable) | 4.5GB | ✅ SÍ |
| `win-amd64-tracy` | Instrumentado con Tracy (profiling) | 10.7GB | 🔸 Ocacional |
| `_archivo_builds/` | Builds archivados (relwithdebinfo, sdk-test) | 266MB | 🗄 Archivo |
| `_archivo_dlls/` | Backups de DLLs | 62MB | 🗄 Archivo |
| `_archivo_mods/` | Mods de experimentos pasados | 2.7GB | 🗄 Archivo |

> Ver [06_limpieza/PLAN_LIMPIEZA.md](../06_limpieza/PLAN_LIMPIEZA.md) para el detalle.

---

## out/build/win-amd64-release/ — El build principal

```
win-amd64-release/
├── dbz3.exe              ← El juego (lanza desde aquí)
├── dbz3_user.toml        ← CONFIG del usuario (mods, región, backend GPU, frame cap)
├── rexruntime.dll        ← Runtime (el hook de mods vive aquí) — se actualiza desde rexglue-sdk/out/win-amd64/
├── rexruntimerd.dll      ← Runtime debug (para Tracy)
├── rexgpu-xenos.dll      ← Backend GPU
├── amd_fidelityfx_*.dll  ← FidelityFX (FSR/CAS)
├── mods/                 ← MODS (ver abajo)
├── active_region/        ← Overlay de región que se monta como game: (se reconstruye en cada arranque)
├── logs/                 ← Logs del runtime (dbz3_001.log...)
├── user_data/            ← Datos de guardado
├── *_backup*.bmp / frontbuf_*.bmp / black_*.bmp ← DUMPS DE FRAMEBUFFER (debug, ~1GB) — LIMPIABLES
├── _backup_d3d12/ _backup_pre_opt/ _backup_tracy/ ← Backups de DLLs
├── crash_*.dmp           ← Dumps de crash
```

---

## out/build/win-amd64-release/mods/ — Los mods

| Mod | Contenido | Estado |
|---|---|---|
| `og_music` | Música original (ADX/SFD) — solo us/ (eu deduplicado) | ✅ Funciona (activarlo: quitar `.disabled`) |
| `janemba_v10` | AFS con bin de Krillin ORIGINAL (referencia) | ✅ Es el AFS intacto |
| `goten_body` | Cuerpo de Goten inyectado en Krillin (vía override) | 🔴 Crashea (en investigación) |

> Los mods viejos (janemba, krillin_*, afstest, janemba_v11) están archivados en
> `out/build/_archivo_mods/` — fuera de `mods/` para que el runtime no los escanee.

---

## awo_tools/ — Scripts de RE (nuestros)

| Script | Función |
|---|---|
| `analyze_bin_hd.py` | Parser del bin HD con la template B3_AMB_PS3.bt (RECOMENDADO) |
| `build_awo_desde_cero.py` | Parsear Janemba.amb → AMGs (extracción) |
| `build_janemba_final.py` | Inyectar geometría de Janemba en slots de Krillin |
| `swap_cuerpo_hd.py` | Inyectar cuerpo de Goten en Krillin |
| `parse_ps2_mesh.py` | Parser de malla PS2 (submeshes, FaceType) |
| `pose_matrix.py` | Matrices world de huesos PS2 |
| `rig_mapeo.py` | Re-mapeo JNB→KLL por labels |
| `build_janemba2.py`, `build_afs.py`, `mezclar_ps2_hd.py` | Experimentos previos |
