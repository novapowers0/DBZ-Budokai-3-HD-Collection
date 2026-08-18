# mod center hd — Guía de retopología 3D (la vía de la comunidad)

> Actualizado 2026-08-14. La conversión binaria entre esqueletos con poses
> distintas NO es viable (rotaciones 90-180° entre juegos). La comunidad usa
> **retopología 3D manual** con Blender. Este documento consolida el pipeline.
>
> **⚠️ ACTUALIZADO 17/08**: para swaps ENTRE JUEGOS HD (B1↔B3) la retopología
> **ya no es necesaria** — el runtime dibuja el bin `#AWO` completo tal cual
> (sin validar conteos). Port = convertir sellos + material + AZT e instalar
> el bin completo (lección 9 del B1). **Ver `GUIA_SWAPS_Y_PORTS.md`.** Esta
> guía queda como vía secundaria (edición de mallas existentes).

---

## 1. POR QUÉ LA RETOPOLOGÍA ES LA VÍA

Verificado repetidamente:
- **Inyección en slots** (v3/v21): muestra la topología del anfitrión (Krillin),
  nunca el personaje nuevo. El IB (topología) es del anfitrión.
- **Re-layout con IB propio** (v20): crash. El guest B3 exige conteos fijos.
- **Retargeting binario** (v16-v19): shear/deformación por rotaciones 90-180°
  entre esqueletos (SDBH vs KLL).
- **La comunidad porta SDBH/B1/B2/IW→B3 PS2 con retopología manual**:
  EMD→FBX→Blender (re-riggear al esqueleto destino)→OBJ→AMG.

## 2. PIPELINE DE RETOPOLOGÍA (validado)

```
1. SDBH WM (EMD/ESK) → FBX          emdfbx.exe -ExportAscii (LibXenoverse)
2. FBX → Blender 2.78                plugin FBXImporterExporterFromBlender2.78
3. En Blender: re-riggear el modelo  al esqueleto de Budokai (labels KLL),
   pintar weights por hueso, alinear pose
4. Exportar OBJ                       (vértices V + normales VN + UVs VT)
5. OBJ → mesh parts PS2 (AMG)         OBJ to AMG v0.92 (Nexus-sama)
6. AMG → empaquetar AMB PS2           Budokai AMB Packer-Unpacker
7. (opcional) re-layout a HD          NUESTRO pipeline (awo_tools)
```

### PIPELINE HD (nuestras herramientas, `mod center hd\`)

```
1. Extraer Krillin HD a OBJ:          python awg_to_obj.py <e326.bin> <b327_ps2.bin> krillin.obj
   (1956 verts, world space, listo para Blender)
2. Extraer modelo fuente a OBJ:       python json_to_obj.py <model_v2.json> android18.obj
   (Android 18 SDBH, 1682 verts, altura 1.75 chibi)
3. Blender: importar krillin.obj + android18.obj,
   ESCALAR android18 a la altura de krillin (x7.25 approx),
   superponer y re-riggear la forma de Android 18 sobre el esqueleto de Krillin
   (mantener 1956 verts! solo mover posiciones world)
4. Reimportar al bin HD:              python obj_to_awg.py <e326.bin> <b327_ps2.bin> krillin_editado.obj salida.amb
5. Comprimir LZX + instalar como mod   (xbcompress /N:2048 + build_afs)
```

**CRÍTICO**: el OBJ editado debe mantener EXACTAMENTE 1956 vértices (los del
sec34 de Krillin). El usuario mueve las posiciones world de los vértices en
Blender para que tomen la forma de Android 18, pero NO agrega/elimina vértices.
El importador reescribe las posiciones locales (inv(mat_world[bone]) * world).

## 3. HERRAMIENTAS CLAVE (y dónde están)

| Herramienta | Ruta | Función |
|---|---|---|
| `emdfbx.exe` | `modding resources\EmdFbx-and-FbxEmd-LibXenoverse` | EMD→FBX |
| `fbxemd.exe` | ídem | FBX→EMD |
| Plugin Blender 2.78 | ídem `FBXImporterExporterFromBlender2.78` | Importar/exportar FBX |
| `OBJ to AMG v0.92` | `mod center\OBJ to AMG v0.92` (source code.zip) | OBJ→mesh parts PS2 |
| `AMG to OBJ V2` | `modding resources discord\tools\AMG_to_OBJ_V2.zip` | mesh parts PS2→OBJ |
| `Model Rig Toolset V0.6` | `mod center\Model Rig Toolset V0.6` (Source) | Rig extractor/remover |
| `Bone Addition Tool v1.02` | `mod center\Bone Addition Tool v1.02` (.py) | Añadir huesos |
| `Model Merger Tool` | `mod center\Model Merger Tool` (Source) | Fusionar modelos |
| `AMBStudio` | `mod center\AMBStudio` | Editor AMB |
| `Budokai Model Editor Preview` | `mod center\Budokai Model Editor Preview` | Editor visual |

## 4. LO QUE APRENDIMOS PARA EL RE-LAYOUT HD

El OBJ to AMG genera mesh parts PS2 expandiendo vértices por triángulo (48B:
V+VN+VT) con templates binarios. Para el HD (AWG) el layout es stride 44:
`[nan,u,v,z,x,y,peso,bone@28,nz,-ny,nx]`.

**Clave del mapeo skin→malla** (Model-Rig Extractor v0.9): el rig de cada bone
tiene `ch_loc`/`sb_loc` → bloques con el OFFSET del vértice. Esto resuelve el
100% del cuerpo (nuestro SkinData solo cubría 49-76%).

## 5. INGENIERÍA INVERSA DEL FORMATO 3D (para futuros ports)

- **AWO HD**: header 0x30 (bones, amg_count, tabla, labels) + AWG0 (sec34
  stride 44 + vb2 + IB) + mesh group (mesh-ref blocks + arms). Ver
  `awo_tools\AWO_FORMAT.md`.
- **Vértice B3**: `[nan,u,v,z_local,x_local,y_local,peso,bone@28,nz,-ny,nx]`.
  El sec34 SOLO usa bones 0-35 (piernas 38-49 van al vb2 estático).
- **Conteos fijos**: el guest B3 exige sec34=1956, vb2=226, IB=5140. Cambiarlos
  rompe el parseo (crash).
- **Mesh-ref blocks**: en AWG0+0x1ED8 (13×0x50). Los shadow (sello 0x204)
  definen límites del IB en bytes. Re-mapearlos con conteos distintos → crash.
