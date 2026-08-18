# MODEL SWAP — Investigación completa

> Actualizado: 2026-08-14. Qué sabemos, qué falla, y qué dice la documentación
> de la comunidad sobre cómo se hacía el model swap en el B3 original.

---

## RESUMEN

El **model swap** (poner el modelo de un personaje en el slot de otro) es la meta.
Estado actual: **el mecanismo de override funciona** (el bin se sirve íntegro),
pero **el guest crashea al procesar un bin de otro personaje**. La investigación
sigue abierta.

---

## 1. LO QUE SABEMOS QUE FUNCIONA

| Técnica | Resultado |
|---|---|
| Reemplazar el bin de Krillin por **el mismo bin** (afstest) | ✅ Carga perfecto |
| Mod de **textura** (solo #AZT) | ✅ Funciona |
| **Override por entrada** (mecanismo) | ✅ El bin se sirve íntegro |
| Reemplazar el bin por el de **otro personaje** (Goten→Krillin) | 🔴 Crashea |
| Inyectar **cuerpo de otro personaje** en los slots (Goten body→Krillin) | 🔴 Crashea |

> La conclusión clave: el guest NO acepta un bin de otro personaje sin más.
> El problema NO es el mecanismo, es el CONTENIDO/estructura del bin.

---

## 2. LO QUE DICE LA DOCUMENTACIÓN DE LA COMUNIDAD

### 2.1 LGBT Method (Lean's Ginyu Bodyswap Technique) — `modding resources discord\tutorials\LGBT_Method.zip`

El método de la comunidad para bodyswap en B3 PS2:

1. **NUNCA reemplazan el bin completo del personaje**.
2. Se identifica qué **ejes (axis)** se necesitan: para piernas `WAIST STMC RLEGROT RLEG1 RLEG2 RFOOT1 RFOOT2 LLEGROT LLEG1 LLEG2 LFOOT1 LFOOT2`; para cuerpo `WAIST CHEST STMC RCHN RARMROT...`.
3. Se localizan los **model parts** de esos ejes en el donante.
4. Se copian los parts al receptor, ajustando offsets y punteros.
5. Se ajusta la textura/shader.

**Lección**: el swap es selectivo por ejes, no de bin completo.

### 2.2 Tutorial "Añadir AMG manualmente" (JaromSc) — `modding resources update 2\Tutorial #1 Añadir AMG manualmente`

1. Copiar el AMG (mesh part) del donante → pegarlo al final del modelo base.
2. Editar **longitud del archivo** y **conteo de partes** en el header.
3. Copiar los **nombres de huesos** (desde "Body" al último).
4. Buscar el **offset del hueso** destino (ej: NH=0x1E) y reemplazar su puntero por la ubicación del nuevo AMG.
5. Asignar textura y shader.

**Lección**: añadir una parte implica ajustar: longitud, conteo de parts, punteros de huesos, textura/shader.

### 2.3 La comunidad NO tiene conversor PS2→HD

- Todo el tooling de la comunidad (OBJ to AMG, Model Rig Toolset, AMO Decompiler)
  trabaja con el formato **PS2 (#AMO0 LE)**.
- El formato HD (#AWO BE) lo editan con 010 Editor + template `B3_AMB_PS3.bt`.
- El salto PS2→HD es un **re-layout** (endianness + magics + tabla de offsets),
  no un formato distinto. Documentado en AWO_FORMAT.md.

---

## 3. LO QUE HEMOS VERIFICADO POR RE

### 3.1 Estructura del bin HD (con template B3_AMB_PS3.bt)

```
AMB: #AMB + tabla de entradas (loc+size)
  entry0: #AWO (modelo)
  entry1: #AZT (texturas)

AWO: +0x10 numberOfBones, +0x14 ptrConnections, +0x18 numberOfAWGs,
     +0x1C pointerAWGoffsets, +0x24 ptrBoneNames,
     +0x30 AWOunk[bones](32B) → zonas de hueso, + tabla AWGptr + BoneNames

AWG (por mesh group): +0x10 numberOfBones, +0x14 rigging_data_ptr,
     +0x1C ptrBones, +0x24 unk_Count(80B blocks), +0x28 ptrVertexBlock,
     +0x2C VertexBlockSize, +0x30 ptrFaceData, +0x34 FaceDataSize,
     +0x38 unk_ptr_28, +0x3C sizeOfunk_ptr_28
```

### 3.2 Comparativa Krillin vs Goten (ambos HD, mismo juego)

| | Krillin (entrada 327) | Goten (entrada 298) |
|---|---|---|
| Huesos | 51 | 56 |
| AWGs | 18 | 21 |
| AWG0 (cuerpo) | vb=2190, face=233 | vb=2035, face=225 |
| Dedos | 10 (L01-L10 L/R) | 12 (L01-L38 L/R) |
| Caras | 7 (L01-L23) | 8 (L01-L41 S00) |
| Labels | KLL_*/XKLL_* | GTN_*/XGTN_* |

**Conclusión**: estructura casi idéntica (mismo patrón), difieren en conteos y labels.
Un humanode del mismo juego debería ser compatible... pero crashea.

### 3.3 El vértice HD (stride 44)

```
+00 nan (flag)  +04 u  +08 v
+12 z_local     +16 x_local   +20 y_local
+24 peso        +28 BONE(u32)  +32 nz  +36 -ny  +40 nx
```

### 3.4 Los 3 fixes del override (críticos)

Ver [02_mods/COMO_HACER_MODS.md](COMO_HACER_MODS.md). Resumen:
1. El hook debe soportar carpetas (portado del B1).
2. Compresión `/N:2048` (no /N:32).
3. Padding al tamaño exacto del slot.

---

## 4. HIPÓTESIS DEL CRASH (por investigar)

Tras confirmar que el override funciona y el bin se sirve íntegro, el crash al
cargar un bin de otro personaje podría deberse a:

| Hipótesis | Explicación | Cómo verificarla |
|---|---|---|
| **A. El guest valida labels/conteos** | El moveset/animaciones de Krillin referencian huesos KLL_* por índice; el bin de Goten usa GTN_* | Comparar cómo el guest indexa huesos |
| **B. El guest usa el mesh group del slot** | El slot 327 espera cierta estructura de mesh-ref blocks | Instrumentar el parser del guest |
| **C. El crash es en otro campo** | Algún offset interno del AWG no coincide | Instrumentar el guest |

### 4.1 Confirmado: el crash NO es por truncado LZX (2026-08-14)

El último test de `goten_body` (cuerpo de Goten en Krillin, LZX `/N:2048`,
padding a 106496) mostró en logs:
```
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=106496
UNHANDLED EXCEPTION: Code=0xC0000005 Addr=0x7ff7bdfe87ee
```
→ El bin se sirvió **íntegro** (got=106496, el LZX completo) y aun así crasheó.
→ El crash es por el **contenido del bin** (la geometría/estructura del modelo),
  no por el mecanismo de override ni la compresión.

### 4.2 Hallazgo: el rigData difiere entre personajes (2026-08-14)

Comparando el `rigData` (matrices de pose) del AWG0 de Krillin vs Goten:

| Bone | Krillin | Goten |
|---|---|---|
| bone 2 | scale=(0, 0, 0) | scale=(-0.7071, -0.7071, 0) |
| bone 5 | pos=(-0.33, 0, 0.52) | pos=(2.30, 0, 0) |

→ **Cada personaje tiene su propio rigData** (posición/rotación/escala de huesos).
→ El guest del slot 327 espera el esqueleto de Krillin. Un bin de Goten trae
  otro rig → desajuste → crash.
→ **El model swap NO es copiar geometría**: hay que transformar la geometría del
  donante al ESPACIO del esqueleto del receptor (`local_donante → world → local_receptor`).
→ Esto invalida la suposición previa de "matrices world idénticas" (era solo
  para el MISMO personaje PS2 vs HD).

---

## 5. PRÓXIMOS PASOS (RE real del guest)

La documentación de la comunidad NO documenta el swap de bin completo entre
personajes del B3 (nunca lo hicieron). Para avanzar necesitamos **ingeniería
inversa del parser del guest**, que vive en `generated/dbz3_recomp.*.cpp`:

1. **Localizar la función que parsea el bin 327** en el código guest recompilado
   (el crash addr `0x7ff7...` apunta ahí).
2. **Instrumentar**: loguear qué offsets lee el guest del bin (como hicimos con
   `AFS327 READ` pero a nivel de parseo del modelo).
3. **Comparar** el flujo de parseo del bin original vs el bin de Goten: ver
   exactamente qué campo provoca el crash.

### Herramientas disponibles para esta RE
- `awo_tools/analyze_bin_hd.py` — parser del bin con template oficial
- `generated/dbz3_recomp.*.cpp` — código guest recompilado (el parser real)
- `rexglue-sdk/` — runtime instrumentable (C++), donde vive el hook
- Tracy profiling (build `win-amd64-tracy`)
- `mod center hd/` — herramientas HD que hemos creado

---

## 6. REFERENCIAS

- `AWO_FORMAT.md` (raíz) — formato completo AFS/AFL/LZX/#AMB/#AWO
- `modding resources discord\tutorials\LGBT_Method.zip` — método bodyswap
- `modding resources update 2\Tutorial #1 Añadir AMG manualmente` — añadir AMG
- `modding resources discord\research\B3_AMB_PS3.bt` — template 010 del formato
- `modding resources discord\research\00000002-00000002-b3.AMO.json` — formato intermedio aerithdevs
- `mod center\OBJ to AMG v0.92` — pipeline OBJ→AMG PS2
- `mod center\Model Rig Toolset V0.6` — rig PS2
