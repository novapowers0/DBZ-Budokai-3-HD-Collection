# Herramientas — inventario

> Actualizado: 2026-10-06 (§0 Studio de cámaras y port de Shin Budokai / Heroes). Inventario de
> herramientas disponibles y su función.

---

## 0. KIT DE MODS: PERSONAJES NUEVOS Y STUDIO (2026-10-06)

Todo esto se usa sin consola desde el Mod Kit (`mod center hd/modkit_gui.py`) y el launcher; la
consola es para modders. Cada herramienta con lógica tiene su autocomprobación.

### Studio de cámaras (`mod center hd/studio/`)
| Herramienta | Función | Comprobación |
|---|---|---|
| `studio_gui.py` | Ventana del Studio: personaje + técnica, línea de tiempo del guion, vistas 2D, plantillas (órbita, travelling, temblor, giro), vista previa toon, GIF, Blender, guardar como mod | `--selftest` (sin ventana), `--captura out.png` |
| `studio_core.py` | Núcleo: `#ACC/#AMC` (clips de cámara), guion `#SPX` (clip ↔ técnica, esperas), marcas de golpe del `#CSK`, validación, plantillas, glTF de ida y vuelta, guardar con LZX + reserva + respaldo | `selftest` (633 clips HD + 633 PS2 byte a byte), `info ID`, `exportar-glb`, `importar-glb` |
| `blender_puente.py` | Script que ejecuta Blender (sin add-on): `abrir` (importa la toma y guarda un .blend) y `exportar` (en segundo plano, `.blend` → `.glb`) | ida y vuelta con `blender -b` |

Guía: `docs/02_mods/STUDIO_CAMARAS.md`. Formato: `docs/03_formatos/CAMARA_ACC.md`.

### Personajes de Shin Budokai (PSP) y Super Dragon Ball Heroes (PC)
| Herramienta | Función | Comprobación |
|---|---|---|
| `mod center hd/importar.py` | Importador del launcher y del Mod Kit. Fuentes `b1`, `b2`, `b3`, `iw`, **`sb1`, `sb2`, `sdbh`** | `fuentes`, `lista FUENTE`, `importar …` |
| `awo_tools/sbport.py` | Moveset de SB1/SB2 → B3 (AP 20→16 B, HR 160→128 B, daño × 0,85, códigos, almacén global, injertos del donante) | `--prueba`, `--oraculo` |
| `awo_tools/sb_tablas.py` | Tablas medidas SB → B3 (almacén global, efectos AP7) | (módulo) |
| `awo_tools/sb_amm.py` | Descompresor de animaciones de SB | (módulo) |
| `awo_tools/sb_tecnicas.py` | Técnicas de SB: BSP híbrido, nombres oficiales (`bsp`, `aplicar`, `nombres`). Técnicas que **evolucionan** con la forma (misma entrada, máscaras de formas disjuntas: Masenko → Spirit Shot → Kamehameha) = UNA cápsula + `anm_formaK.bin` por forma (2026-10-08) | `prueba` |
| `mod center hd/auditar_importar.py` | Importa y monta en una carpeta aparte todos los personajes de una fuente y anota avisos/errores (TSV) | `--fuente sb2 --mods D:/tmp/aud --construir` |
| `awo_tools/cinematica.py` | Definitiva con animaciones propias sobre la cinemática del donante: cada código 0x4A0+ elegido recibe un tramo (o varios seguidos) re-temporizado de otras animaciones (giros de esas, cadera y huesos de anclaje del original); el guion #SPX, la cámara y la versión «defiende» no cambian. Clave `definitiva_animaciones` de personaje.toml (tarjeta «Definitiva» del Mod Kit) | `prueba` |
| `awo_tools/b1port.py` (definitiva) | Traduce la definitiva de B1 (el especial con guion SPX más largo: ráfaga, lanzamiento) a la cinemática del donante: `b1_ultimate`, `cine_win` (versión «gana»: desde el 1er código del guion hasta el hueco o la repetición), `receta_definitiva` (a su velocidad, hasta 1,5× más lenta). El importador escribe `moveset/definitiva.json` y la cápsula con el nombre oficial (`mod center hd/b1_capsulas.txt`) | `prueba`, `--salida DIR` deja `definitiva.json` |
| `mod center hd/iso.py` | Lee ISOs de PS2/PSP sin extraer; `find_game` (B1/B2/SB1/SB2 de cualquier región), `boot_elf` (SYSTEM.CNF), `find_region` (`_us` ↔ `_eu`) | `prueba` |
| `mod center hd/colores.py` | Color del aura (bin `aura` de data_cmn: #AZT + #CTR wk0/wk1 RGB) y del ki (BSP: #AZT + partículas #ACE, colores RGBA f32 en +0x80): solo el tono. Claves `aura_color` / `ki_color` de personaje.toml (roster_build) y tarjeta «Colores» del Mod Kit; `nativo ID --aura --ki --tecnica CAPSULA=COLOR` para personajes del juego (solo esa técnica: copias de sus #ACE/texturas) | `prueba` |
| `mod center hd/hexedit.py` | Editor hex para expertos (pestaña **Hex** del Mod Kit avanzado): árbol #AMB LE/BE, partes de #AMO con «ocultar parte» (vtype/vflags `FFFFFF00 FFFF0000`, el truco de trajes alternativos; `amo2awo` las omite), inspector LE/BE, copia al guardar | `--selftest` |
| `awo_tools/psp_amo.py` | Modelo de PSP → bin HD | byte a byte con los trajes del port de Gohan del Futuro |
| `awo_tools/sdbh_model.py` | Modelo de Heroes (EMD/ESK/EMB) → bin HD con boca, 7 caras y rampas nativas | `--selftest` |
| `mod center hd/roster_build.py` | Monta los personajes nuevos; claves `formas`, `ki_base`, `modelo_forma`, `fisica`, `transformacion`; `capsulas --ki` | `construir --mods <copia>` |

Formatos: `docs/03_formatos/SB_VS_B3_MOVESET.md`, `FORMAS_Y_KI.md`, `TOON_Y_BRILLO_HD.md`.
Receta completa: `docs/02_mods/COMO_HACER_MODS.md` §5.

---

## 1. NUESTRAS HERRAMIENTAS (awo_tools/ + mod center hd/)

### awo_tools/ — scripts de RE y conversión
| Herramienta | Función | Estado |
|---|---|---|
| `analyze_bin_hd.py` | Parser histórico basado en template PS3 | ⚠️ Obsoleta; solo referencia |
| `awg_to_obj_b3.py` | Exportador OBJ de bins B3 completos | ✅ Recomendada |
| `awg0_export.py` | Exportador AWG0 con autodetección A/C | ✅ Recomendada |
| `awg_cara_export.py` | Exportador de AWGs de cara | ✅ Recomendada |
| `parse_ps2_mesh.py` | Parser de malla PS2 (verts+IB) | ✅ |
| `pose_matrix.py` | Matrices world de huesos PS2 | ✅ |
| `rig_mapeo.py` | Re-mapeo JNB→KLL por labels | ✅ |
| `build_awo_desde_cero.py` | Parsear Janemba.amb → AMGs | ✅ (extracción) |
| `build_janemba_final.py` | Inyectar geometría de Janemba en Krillin | 🔸 en investigación |
| `swap_cuerpo_hd.py` | Inyectar cuerpo de Goten en Krillin | 🔸 en investigación |
| `build_janemba2.py`, `build_afs.py`, `mezclar_ps2_hd.py` | Experimentos previos | 🔸 archivable |

### Mantenimiento del entorno (solo desarrollo, no se distribuye)
- `tools/cleanup.ps1` — limpieza manual del peso del proyecto: `-DryRun`
  (previsualiza), `-Yes` (L0 sin preguntar), `-Full -Yes` (+ L1 archivados).
  Nunca toca docs/src/assets/mods/SDK/ps2_games. Reporta el top-12 de
  consumidores. Ver cabecera del script.
- `awo_tools/corpus_scan.py` — desde 2026-09-09 **limpia sus temporales** tras
  cada descompresión (antes dejaba ~16 GB en `out/analysis/corpus/.work`).
  Solo `--keep-bin` conserva copias en `.work/bins/`.
- `tools/make_test_iso.py <out.iso> <carpeta> [--quiet]` — genera un **XDVDFS de
  prueba** (lo que el runtime lee como "GDFX") empaquetando una carpeta con el
  layout retail: sirve para validar el **modo disco (ISO)** sin un ISO real
  (v1.2.2 EX, ver `docs/SESION_AUTODETECCION_XEX_2026-09-17.md` §4.bis). Solo
  empaqueta lo que le des: no contiene datos del juego.
- `tools/perf_report.ps1 [-Count N]` (+ `perf_report.cmd` de doble clic) -
  **informe de rendimiento** de los ultimos N logs: config aplicada, ventanas de
  5 s, fps min/med/max, peor `max_frame_ms`, ventanas <58 fps, errores y
  veredicto. Guarda el texto en `%TEMP%\opencode\perf_report.txt`.
- `tools/perf_test_config.ps1 -Scale 1..4 -Msaa on|off [-Show]` - fija escala
  interna + MSAA nativo en `dbz3_user.toml` para las pruebas A/B de rendimiento
  (deja `dbz3_texture_upscale=1` y `dbz3_perf_logging=true`).
- `tools/hidden_run.ps1 -Label <txt> -Seconds <n> [-HideMode 0|1] -Overrides
  "cvar=valor;cvar=valor"` — **arnés de pruebas offscreen** (2026-09-18): lanza
  `dbz3.exe` con la ventana movida fuera de pantalla, aplica overrides al
  `dbz3_user.toml` (backup/restore automático), mata el proceso y resume el log
  (líneas de `AFS OVERRIDE`, `perf fps=`, `upscale pipeline ready`, errores).
  Usar `dbz3_skip_launcher=true` para bootear directo a la partida. Los valores
  de texto del toml van **entre comillas** (`"manual"`, `"fsr"`); sin comillas el
  parser descarta el fichero entero. Ver
  `docs/ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md` §6.

### mod center hd/ — herramientas HD adaptadas
| Herramienta | Función |
|---|---|
| `fbx_ascii.py` | Parser FBX ASCII (SDBH→FBX→datos) |
| `emd_to_awo_hd.py` | Parseo ESK + mapeo SDBH→KLL |
| `build_awo_from_json.py` | JSON→AWO HD |
| `awg_to_obj.py` | Exportar AWG HD→OBJ |
| `obj_to_awg.py` | Importar OBJ→AWG HD |
| `json_to_obj.py` | JSON SDBH→OBJ |
| `RETOPOLOGIA_3D.md` | Documento del pipeline de retopología |

---

## 2. HERRAMIENTAS DE LA COMUNIDAD (mod center/) — 36 programas

### Conversión de modelos
| Herramienta | Función | Formato |
|---|---|---|
| `OBJ to AMG v0.92` | OBJ→mesh parts PS2 (con templates) | PS2 |
| `EMD to AMG v0.90` | Xenoverse EMD→AMG PS2 | PS2 |
| `B3-IW AMO Converter + Shadows` | B3/IW→B1 (exe) | PS2 |
| `Bin to OBJ (English) V3` | Bins→OBJ | PS2 |
| `AMG to OBJ V2` | AMG→OBJ | PS2 |
| `Model Merger Tool` | Fusionar 2 modelos (AMO_LGBT) | PS2 |

### Edición de rig/modelos
| Herramienta | Función |
|---|---|
| `Model Rig Toolset V0.6` | Extractor/Remover de rig (documenta el formato) |
| `Model-Rig Extractor Tool V1.0` | Idem standalone |
| `Bone Addition Tool v1.02` | Añadir huesos al AMO |
| `Model Part Editor` | Convertir partes B3↔B1 |
| `Axis Line Tool` | Generar axis lines del AMO |
| `BoneAxis Display` | Ver posiciones de huesos |

### AMB / AFS
| Herramienta | Función |
|---|---|
| `AFS Toolset v0.90` | Empaquetar/desempaquetar AFS |
| `AMB Tool` / `AMBStudio` | Editor de AMB |
| `Budokai AMB Packer-Unpacker` | Packer/Unpacker AMB |
| `AMB_AMT Manipulator 1.5` | Manipular AMB/AMT |
| `B3_IW Model Converter` | Empaquetar AMB (no conversor) |

### Compresión (CRÍTICO)
| Herramienta | Función |
|---|---|
| `Xbox 360 Compression tool` | **`xbcompress.exe /N:2048`** y `xbdecompress.exe` |

> ⚠️ USAR `/N:2048` SIEMPRE (el juego usa ese blocksize). `/N:32` produce bins
> que exceden el slot → crash.

### Otros
| Herramienta | Función |
|---|---|
| `A3T Analyzer` | Analizar texturas A3T/AZT |
| `CRI Middleware ADX Tools` | Audio ADX |
| `PSound` | Editor de audio |
| `SLXS Editor v0.50` | Añadir personajes (SLXS) |
| `Budokai3_SLUS_Editor` | Editar SLUS (select) |
| `Set Unlimited Fusion` | Fusiones ilimitadas |
| `Transformation Input Stuff` | Transformaciones |
| `Zero Devs' Tool` | Herramienta universal de la comunidad (BT3p→Budokai) |

---

## 3. HERRAMIENTAS DEL DISCORD (modding resources discord/tools/)

| Herramienta | Función |
|---|---|
| `Budokai Modding Tool V1.5` | AMO_LGBT, AMG_C, AXIS_E, SLXS... |
| `AMO Model Separator v1.01` | Separar parts del AMO |
| `Model Part Addition Tool` | Añadir parts |
| `AMG to OBJ V2` | Exportar OBJ |

---

## 4. HERRAMIENTAS DEL SDK (rexglue-sdk-0.10/)

- `rexruntime.dll` — runtime (hook de mods, filesystem, logging)
- `rexgpu-xenos.dll` — backend GPU
- Tracy — profiling (build win-amd64-tracy)

---

## 5. FLUJO RECOMENDADO PARA ESTUDIAR UN MODELO

```powershell
# 1. Descomprimir el bin del AFS
xbdecompress.exe entrada.lzx entrada.bin

# 2. Exportar/verificar la estructura con las herramientas B3 actuales
python awo_tools/awg_to_obj_b3.py entrada.bin salida.obj

# 3. Si es PS2, extraer la malla
python awo_tools/parse_ps2_mesh.py entrada.amb 0 salida
```

---

## 6. ARNES DE PRUEBAS DEL JUEGO EN EJECUCION (tools/, 2026-09-19)

| Herramienta | Funcion |
|---|---|
| `tools/long_run.ps1` | Lanza/para/consulta pruebas **largas** desacopladas; silencia el juego (`audio_mute=true`), aplica overrides al `dbz3_user.toml` y lo restaura. Estado en `%TEMP%\opencode\long_run_state.json`. |
| `tools/press_key.ps1` | Inyecta teclas por `PostMessage` (`-TargetPid`; mapa W/A/S/D/Backspace/Tab/Space/Return). |
| `tools/grab_window.ps1` | Captura PNG de la ventana (`PrintWindow` PW_RENDERFULLCONTENT). Requiere la ventana **on-screen** (fuera de pantalla la presentacion se congela y las capturas salen identicas). |
| `tools/click_window.ps1` | Click por coordenadas **cliente** de la ventana (launcher ImGui). |
| `tools/hidden_run.ps1` | Igual que long_run pero moviendo la ventana fuera de pantalla (solo para medir el swap rate del guest, no para capturas). |

**Receta para llegar al combate 3D**: boot con `dbz3_skip_launcher=true` →
opening (~90-100 s) → pulsar `Return` (Start) → titulo/menu → idle ~2-2,5 min →
salta la attract demo battle 3D.

