# HALLAZGO: EL ECOSISTEMA DE LA COMUNIDAD PARA EL TRASPASO DE MODELOS

> Descubierto el 2026-08-14. La comunidad YA resolvió la conversión
> IW→B3 (PS2). El paso restante es PS2→HD 360 (formato del recomp).

---

## 1. LO QUE LA COMUNIDAD YA TIENE (verificado)

### 1.1 Modelos IW convertidos a B3 PS2 (#AMB)
`modding resources\All Character Models from IW into AMB format\`
- **241 modelos .amb** de TODOS los personajes IW: Janemba, Pikkon, Pan,
  Super 17, Super Baby Vegeta 2, Gogeta, Vegito, todos los Freeza/Buu, etc.
- Cada .amb es **#AMB PS2 LE** (3 entradas: #AMO0 + #AMT + padding).
- `Janemba.amb` (934KB) = idéntico al bin 541 del IW (48 huesos, 17 AMGs).
- Los movesets port IW→B3 ya existen (AGENTS.md).

### 1.2 Herramientas de la comunidad (mod center)
- **AMO Decompiler.py / AMO Compiler.py** (Model Compiling Tools): descompilan/
  recompilan #AMO0 PS2 (LE). Base del pipeline.
- **B3_IW Model Converter** (amb_model.py): empaqueta/desempaqueta #AMB.
- **Model Rig Toolset V0.6** (Model-Rig Extractor/Remover): maneja rigs.
- **Model Merger Tool**: fusiona modelos.
- **Bone Addition Tool**: añade huesos.
- **OBJ to AMG / Bin to OBJ**: pipeline OBJ (conversión a formatos editables).
- **EMD to AMG / FbxEmd**: ecosistema Xenoverse (EMD↔FBX).

## 2. EL FORMATO (lo que sabemos)

| | PS2 (B3/IW) | HD 360 (recomp) |
|---|---|---|
| Contenedor | #AMB LE | #AMB BE (AWO + AZT) |
| Modelo | #AMO0 LE | #AWO BE |
| Mesh | #AMG | #AWG |
| Textura | #AMT | #AZT |
| Esqueleto | idéntico | idéntico |
| Endian | little | big |

## 3. EL TRASPASO LÓGICO (lo que falta)

**Krillin PS2 vs HD** (verificado):
- PS2: 3216 triángulos, 4252 posiciones únicas
- HD: 1713 triángulos, 2182 slots (~50% reducción)

**El HD 360 reduce a la mitad la geometría PS2** (decima/re-topologiza).
Los desarrolladores hicieron esto para los personajes presentes.

**Para Janemba** (4415 posiciones, 3141 triángulos):
- El equivalente HD sería ~2200 posiciones, ~1700 triángulos.
- La geometría decimada YA funciona (entra sin crash, sec34=2386).
- El problema es que los **mesh-ref blocks + arms** de Krillin dibujan los
  triángulos de Janemba con rangos del IB equivocados → masa deforme.

## 4. EL BLOQUEADOR TÉCNICO REAL

El formato AWO HD usa un **mesh group con mesh-ref blocks + arms** que definen
CÓMO dibuja el runtime cada part:
- Arm = lista de huesos + offsets del IB (rangos de dibujo).
- Dat = material + cadena recursiva al siguiente part.
- El runtime dibuja cada part como [offset_previo, offset_bone).

Al inyectar geometría de Janemba con un IB distinto, los arms de Krillin
apuntan a rangos equivocados. **Hay que reconstruir los arms** para la
geometría de Janemba (agrupar sus triángulos por material, asignar huesos
JNB→KLL, actualizar offsets).

## 5. PRÓXIMOS PASOS VIABLES

1. **Reconstruir los arms del mesh group** para la geometría de Janemba
   (agrupar triángulos por material en el orden que el runtime dibuja).
2. **Re-rigging**: transformar posiciones de Janemba al espacio local de los
   huesos de Krillin (mapeo por labels JNB_HEAD→KLL_HEAD, etc.).
3. **Validar** con un part mínimo (solo el cuerpo) antes de los 13 parts.

## 6. RECURSOS CLAVE (rutas)

- Modelos IW ya convertidos: `modding resources\All Character Models from IW into AMB format\Janemba.amb`
- AMO Decompiler/Compiler: `mod center\Model Compiling Tools\`
- B3_IW Model Converter: `mod center\B3_IW Model Converter\`
- Formato HD mapeado: `awo_tools/CONSOLIDADO.md`, `awo_tools/RE_PROGRESO.md`
