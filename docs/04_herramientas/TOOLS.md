# Herramientas — inventario

> Actualizado: 2026-08-14. Inventario de herramientas disponibles y su función.

---

## 1. NUESTRAS HERRAMIENTAS (awo_tools/ + mod center hd/)

### awo_tools/ — scripts de RE y conversión
| Herramienta | Función | Estado |
|---|---|---|
| `analyze_bin_hd.py` | Parser del bin HD (#AWO) con template oficial | ✅ Recomendada |
| `parse_ps2_mesh.py` | Parser de malla PS2 (verts+IB) | ✅ |
| `pose_matrix.py` | Matrices world de huesos PS2 | ✅ |
| `rig_mapeo.py` | Re-mapeo JNB→KLL por labels | ✅ |
| `build_awo_desde_cero.py` | Parsear Janemba.amb → AMGs | ✅ (extracción) |
| `build_janemba_final.py` | Inyectar geometría de Janemba en Krillin | 🔸 en investigación |
| `swap_cuerpo_hd.py` | Inyectar cuerpo de Goten en Krillin | 🔸 en investigación |
| `build_janemba2.py`, `build_afs.py`, `mezclar_ps2_hd.py` | Experimentos previos | 🔸 archivable |

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

## 4. HERRAMIENTAS DEL SDK (rexglue-sdk/)

- `rexruntime.dll` — runtime (hook de mods, filesystem, logging)
- `rexgpu-xenos.dll` — backend GPU
- Tracy — profiling (build win-amd64-tracy)

---

## 5. FLUJO RECOMENDADO PARA ESTUDIAR UN MODELO

```powershell
# 1. Descomprimir el bin del AFS
xbdecompress.exe entrada.lzx entrada.bin

# 2. Ver la estructura con el parser
python awo_tools/analyze_bin_hd.py entrada.bin --dump

# 3. Si es PS2, extraer la malla
python awo_tools/parse_ps2_mesh.py entrada.amb 0 salida
```
