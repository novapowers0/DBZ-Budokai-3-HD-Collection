# INVESTIGACIÓN PS2→B3 HD — 2026-09-13 (subagentes: web + RE + mods)

> Consolidado de 4 investigaciones paralelas para intentar cerrar el port PS2→HD
> (Vía B). Leer junto a `SESION_DRAW_SEMANTICS_2026-09-11.md` y `AGENTS.md §3.4`.

## 0. RESUMEN EJECUTIVO

- **Nadie en público ha hecho un port PS2→HD Collection de modelos Budokai.** El
  proyecto está en el estado del arte. Los que moddean Budokai lo hacen en PS2;
  los que tocan HD lo hacen solo en texturas/audio/swaps HD→HD.
- La comunidad sí tiene: (a) un **pipeline de skinning PS2** (budokai ps2 2025.ms,
  `lean bone tutorial`, `OBJ_to_AMG`), y (b) **~430 modelos `#AMB` ya convertidos
  de PS2 (IW→B3, B1→B3)** — pero en formato **PS2**, no HD.
- **Hallazgo técnico decisivo (web)**: "correcto en bind, explota animado" es el
  síntoma canónico de **índices de hueso / paleta incorrectos** (en bind la
  matriz de skin es la identidad para TODOS los huesos ⇒ un índice erróneo es
  invisible en bind y explosivo al animar).
- **Dato duro nuevo**: `fc=94` = paleta de **128 matrices × 48 B** (3×vec4),
  **por draw**; el bone se lee como **1 byte con `endian=2` (k8in32)**, o sea el
  byte **+19** de la ventana (no +16). El fetch es global (`fc=95`, stride 11).

## 1. WEB — personas/proyectos que importan

| Quién | Dónde | Por qué |
|---|---|---|
| **WistfulHopes** | github.com/WistfulHopes/DBZ1 (+RB2) | **Otro recompilado ReXGlue del MISMO juego**. Mismo path AWO/AWG. Mejor colaborador potencial. |
| **h3x3r** | ResHax topic/18350 | Autor de la única plantilla pública **#AWG** (010 Editor). Confirma NUESTRO layout de 44 B (pos/weight/bone/normal/FFFFFFFF/uv) y que manos/cara NO usan strip. |
| **NocturnalRhys** | ResHax | Trabajando explícitamente en **OBJ→AWG→AWO** para B3HD (+ reimportador A3T). Sin publicar aún. |
| **killercracker / SleepyZay** | github.com/sleepyzay/Maxscript-Projects | `budokai ps2 2025.ms`: parsea huesos, AMGs, **tablas de peso (weightData)** y aplica skinning en 3ds Max. |
| Comunidad | Discord B3 modding, ReXGlue Discord | Venues de modders. |

**Veredicto**: contactar WistfulHopes (mismo stack) y NocturnalRhys (mismo objetivo).
No hay converter PS2→HD público.

## 2. WEB — skinning X360/Xenos (por qué explota)

- Xenos **no tiene unidad de matriz paleta**: el "palette" es un buffer que el VS
  lee con `vfetch` indexado por el bone del vértice. Sin magia del emulador.
- **`fc=N` = fetch constant** (0-95). `fc=95` = vertex stream (stride 44 B);
  `fc=94` = **paleta 48 B/matriz** (3×16, fila comprimida de una 4×4).
- El shader lee **1 byte** del campo bone (`fmt=6` = 8_8_8_8, `used=.x`),
  swapeado por `endian=2` (k8in32) ⇒ afecta al byte **+19**.
- Formato/hints del SDK: `36=FMT_32_FLOAT, 37=FMT_32_32_FLOAT, 38=FMT_32_32_32_32_FLOAT, 57=FMT_32_32_32_FLOAT, 6=FMT_8_8_8_8`.
- **Ranking de causas de "bind OK / animado explota"**:
  1. **Espacio de índices de paleta / base por draw** (paleta local vs global).
  2. **Byte equivocado** en el campo bone (posición/endian).
  3. **Frame bone-local calculado contra el bind de OTRO esqueleto**
     (`local = inv(world_PS2)·model` cuando el juego anima con `world_HD`).
  4. Endianness/format del registro entero.
  5. Asumir 4 influencias cuando solo hay 1 peso=1.0.
  6. Índice fuera del `size` de la fetch constant.
- Acción recomendada: **volcar la paleta `fc=94` en el draw** y compararla por
  slot con los huesos del modelo + hexdump del byte bone del vértice.

## 3. RE DEL GUEST (código recompilado) — ubicaciones exactas

| Concern | Ubicación |
|---|---|
| Handler de tag AWO (relocaliza árboles de punteros arms/mesh-group + registra) | `generated\dbz3_recomp.11.cpp:3` (`sub_82080A40`) |
| Dispatcher de tags (AWO/AMG/AZT/ACM/ACC/ACL/ACP) | `generated\dbz3_recomp.3.cpp:3` (`sub_820800A8`); registro en guest `0x82310110` |
| Walker del esqueleto (80 B, quat+pos+hijos) | `generated\dbz3_recomp.16.cpp:248` (`sub_82087F58`) |
| Builders de comandos GPU (packets PM4, `stwu`) | `generated\dbz3_recomp.41.cpp:23855` (`sub_82241848`), `...23.cpp:23639`, `...36.cpp:8571` |
| Decode de draw en el runtime | `rexglue-sdk-0.10\src\graphics\command_processor.cpp:1301-1397`; `packet_disassembler.cpp:213-251` |
| vfetch/formatos en el runtime | `...\pipeline\shader\translator.cpp:379-453`, `translator_disasm.cpp:253-318` |

**Conclusión RE**: skinning **rígido de un solo hueso por vértice** (`+16`, 44 B)
contra una **paleta de 48 B** construida en CPU desde los ejes 80 B
(`sub_82087F58`). No hay código (ni hidden path) que compense un índice malo.

## 4. INVENTARIO DE RECURSOS (mods + comunidad)

### 4.1 Recursos de comunidad más valiosos (en `modding resources*`)
- `All Character Models from IW into AMB format\` — ~200 `.amb` IW→B3 (**formato PS2**).
- `Budokai 1 Models Converted to AMB\` — ~230 B1→B3 `.bin` (**PS2**).
- `Budokai Models\` (Son Swag) — `.amo/.amt/.amb` por slot + B3GHC exclusivos.
- `update 2\MOD EJEMPLO\` — mods de ejemplo en ambos formatos (Ginyu Force, etc.).
- `update 2\lean bone tutorial\` — **workflow de RE-RIG completo** (`budokai_updated.ms`, `Rig Data Tool`, `Goku_Skeleton.FBX`).
- `discord\research\00000002-00000002-b3.AMO.json` — descomposición **AMO B3** más completa (aerithdevs).
- `discord\research\B3_AMB_PS3.bt` — plantilla 010 del **AWO/AWG HD**.
- `discord\tools\` — `Model-Rig_Extractor`, `AMG_to_OBJ_V2`, `OBJ_to_AMG_v0.92`, `Bone_Addition_Tool`, `B3_IW_Model_Converter`, `Budokai_B3_IW_B1_AMO_Converter`, `axis_data.py`.
- `update 2\INFORME_modding_resources_update_2.md` — **mejor referencia única** de formatos PS2 (AMB/AMO0/AMG/AMT, FaceType, bone/axis 0x20).

### 4.2 Estado de los mods del proyecto
- **Solo `_body33` activo**; 77 mods `.disabled` (todos first-party; `NovaPowers` = el autor).
- Vía B: `_strip3` = mejor (1 draw strip, VB+IB correctos, aún deforme). `_grow_tpl` descarta `grow()`. `_body33`/`_nottail` = aislamiento por hueso.
- Vía A: `cell_npm4_test` (mejor inyección) / `cell_best2` / `cell_npm_fix` = **entrega usable validada**.
- Diagnóstico: `cell_bone0_test` (prueba que el guest usa el bone del vértice), `cell_clamp33_test`, `cell_boneclamp_test`.

## 5. HECHOS DUROS NUEVOS (captura `%TEMP%\opencode\draw_evidence\`)

- `VF[94]` (paleta) **siempre `size=1536`** dwords = **6144 B = 128 matrices** de
  48 B. `endian=2`. **Por draw** (cambia de dirección entre draws).
- `VF[95] size=56628` (= **5148×44**, el AWG0 del port) aparece **33 veces** ⇒ la
  captura ES del port (no del swap nativo).
- `dbz3_vf.bin` = [VF95 de 6776 B (=154 verts×44)][**paleta de 6144 B** desde el
  offset 6776]. La paleta tiene **slot 0 (bone 0) = matriz CERO** y el resto
  matrices afines.
- El registro del bone se lee del byte **+19** (por `k8in32`), no +16.

## 6. HIPÓTESIS PRINCIPAL

El síntoma (bind OK / animado explota) + paleta por-draw + skin PS2 traducido por
etiqueta apuntan a: **los índices de hueso por vértice del port no seleccionan la
misma matriz que el mesh HD original seleccionaba**. Candidatos, por orden:
1. La paleta del draw del AWG0 está construida para el mesh HD y el port conserva
   el índice de hueso PS2 (por etiqueta) — puede no ser el mismo "slot" que el HD
   usa para esa zona (p. ej. el HD manda torso a bone 0/23; el port a 1/16/32).
2. Algún vértice con hueso cuyo eje en AWG0 es placeholder (34-47) — pero `_body33`
   descarta que sea lo único.
3. `local` calculado contra un bind que no coincide exactamente con el animado.

## 7. PLAN PROPUESTO (2 vías)

**Vía B (investigación, decisiva):** re-instrumentar el runtime para volcar, por
draw del AWG0, la **paleta completa (6144 B)** + el VB + el IB. Con eso:
reproducir el skinning **offline** (`skinned = P[bone]·[pos,1]`), localizar los
vértices que explotan y qué slot de paleta es el culpable; comparar con el
template NATIVO (que renderiza bien) para hallar la diferencia exacta.
Coste: recompilar `rexgpu-xenos` + 1 partida del usuario.

**Vía A (producto, cierre):** mantener la inyección (`cell_npm4`/`cell_best2`)
como entrega; documentar Vía B como investigación abierta con este informe.
