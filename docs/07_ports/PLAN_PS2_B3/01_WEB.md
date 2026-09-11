# 01_WEB — Investigación web: port de modelos PS2 → B3 HD Collection (360) y problema de los 16 AWGs de cara (huesos 48–63)

> Fecha: 2026-09-10. Autor: investigador técnico (opencode).
> Objetivo: recopilar TODO lo público que pueda servir para (a) convertir/portar modelos del
> Budokai PS2 (`#AMO0`/`#AMG`, LE) al HD de 360 (`#AMB`/`#AWO`/`#AWG`, BE), (b) entender el
> formato de vértice/AWG/AWO de la HD Collection, (c) conocer la comunidad de modding de Budokai,
> (d) técnicas de retargeting de mallas entre rigs distintos y (e) cualquier mención a Cell
> (Semi-Perfect) en mods de Budokai HD.
>
> **Advertencia de alcance**: no se ha tocado código del proyecto. Todo es investigación externa.
> Cada URL lleva valoración de utilidad (**Alta / Media / Baja / Nula**) y por qué.

---

## 0. RESUMEN EJECUTIVO (lo que importa de verdad)

1. **NO existe ninguna herramienta pública que convierta `#AWO`/`#AWG` (HD 360) ↔ `#AMO0`/`#AMG`
   (PS2).** No hay parser, ni wiki, ni plugin de Noesis para el formato HD de 360. El proyecto
   está literalmente en la frontera de lo público. Esto reencuadra la búsqueda: lo útil no es
   "el conversor que ya existe" (no existe) sino (a) herramientas PS2 que producen geometría y
   huesos limpios, (b) referencias genéricas de layout de vértice/skinning de la época
   PS3/X360, y (c) herramientas de retargeting de mallas.
2. **El hallazgo más accionable**: las referencias de skinning por "matrix palette" (NVIDIA)
   incluyen el **"Vertex Offset Method"**: los vértices se guardan *en el espacio local del hueso*,
   con **hasta 4 posiciones-offset por vértice** y el pool se consume **secuencialmente como
   offsets**, no solo por el índice buffer. Esto **encaja con la observación empírica** de que el
   guest consume el pool **posicionalmente** aunque el IB sea consistente (tests T4/T5/T7). Es la
   hipótesis de RE más prometedora que aporta la web.
3. **El port de PS3 (`gnome41/dbz-budokai-hd`) demuestra que el HD usa la librería Sony EDGE
   (geometría en SPU) + RSX/cellGcm**, no un pipeline propio de Dimps. Eso explica que el formato
   de vértice sea "raro" (bloques por hueso preparados para batching/EDGE). El port de PS3 es la
   mejor pista de RE transversal (mismo pipeline que 360, distinto endianness/GPU).
4. **Herramienta PS2 más valiosa**: `SamuelDBZMAAM/Budokai-Modding-Tool` (Python) — tiene módulos
   de **AMB combiner**, **AMG Creator/Addition**, **AMO0 Editor/Creator**, **"Removing face AMGs"**
   y un editor de partes de modelo. Es exactamente el dominio del problema: separar/añadir AMGs
   (incl. los AMG de cara) dentro de un AMO0. Sus fuentes (`amo_s.py`, `amg_a.py`, `amg_c.py`)
   documentan offsets concretos del formato PS2.
5. **El ecosistema de extracción PS2 está bien cubierto** (QuickBMS `DragonballBin/AMO/AMG`,
   maxscript de killercracker para 3ds Max, unpacker AMO + importer AMG de Szkaradek123 para
   Blender 2.49). Limitación recurrente: **no importan bien los pesos/rigging** ("figuered out
   bones but not the rigging"). Sirven para geometría y escalas, no para skinning completo.
6. **Comunidades**: el ecosistema fuerte es el de *Budokai Tenkaichi* (Sparking), no el de Budokai
   clásico. Hay una "Budokai Modding Community" y hubs en GameBanana, pero la documentación
   técnica pública del formato es escasa/cerrada (mayormente Discord).
7. **Cell (Semi-Perfect)**: no se encontró NINGÚN mod público concreto de "Cell Semi-Perfect" en
   la HD Collection. Solo menciones tangenciales (edición de auras mezclando "Cell Super Perfect"
   con "SSJ2 Gohan" en la herramienta de modding PS2; Cell 2nd Form existe como forma jugable
   por absorción en Budokai 3). Baja prioridad como fuente de información.

---

## 1. HERRAMIENTAS / REPOS PARA CONVERTIR O PORTAR MODELOS BUDOKAI

### 1.1 Conversión directa HD 360 ↔ PS2
**Conclusión: no existe.** Búsquedas realizadas: "Budokai 3 AWO format", "Budokai HD model
swap", "AMO converter", "PS2 to Xbox360 Budokai model", "GitHub Budokai HD .amb", etc. No hay
repositorio, foro ni plugin que lea/escriba `#AWO`/`#AWG`.

| URL | Utilidad | Notas |
|---|---|---|
| (no hay) | — | No se ha localizado ningún conversor HD↔PS2. |

### 1.2 Port de recompilación PS3 (mismo juego, otro platform) — **ALTA**
- `https://github.com/gnome41/dbz-budokai-hd` — **Port de recompilación estática de la HD
  Collection PS3 (BLES01658)** sobre `ps3recomp`. Describe la carga de assets (`LAUNCH/data.afs`),
  el decodificador genérico de texturas `#A3T`, y (muy importante) que la geometría la procesa la
  **librería Sony EDGE en las SPU**. `CLAUDE.md` del repo es un tratado de RE del arranque del
  juego. **Valor: Alto** para entender el pipeline de contenido HD; **no** documenta el layout de
  vértice, pero da el contexto de por qué el formato tiene esa estructura.
- `https://github.com/sp00nznet/ps3recomp` — SDK de recompilación PS3 PPU→C++ en el que se basa el
  anterior. Referencia del runtime/HLE. **Valor: Medio** (contexto, no formato).

### 1.3 Herramientas de modding PS2 (origen de los datos) — **ALTA**
- `https://github.com/SamuelDBZMAAM/Budokai-Modding-Tool` — **La herramienta más relevante del
  ecosistema PS2.** Python. Módulos: `amb_c.py` (AMB Combiner), `amg_a.py` (AMG Addition),
  `amg_c.py` (AMG Creator), `amo_a.py` (AMO Addition), `amo_s.py` (separa un AMO en un AMG por
  parte), `amo_lgbt.py` (merge "LGBT"), `m_p_e.py` (Model Part Editor), importadores/exportadores
  Budokai 1 / Shin Budokai. El README menciona explícitamente **"Removing face AMGs"** y la
  creación de AMGs nuevos: es el mismo problema que los 16 AWGs de cara del HD.
  **Valor: Alto** (lógica de formato y offsets PS2 reutilizables).
- `https://github.com/SamuelDBZMAAM/DBZ-Budokai-3-Modding-Tool` — Versión/esfuerzo previo del
  mismo autor. `B3 Mod Tool.py`, `amb.bin` de ejemplo. Menciona shaders, auras y AMT.
  **Valor: Medio-Alto**.
- `https://github.com/SamuelDBZMAAM/Budokai-Modding-Tool/blob/master/amo_s.py` — **Código clave**:
  - Detecta partes con `chunk[0] == 0x01 and chunk[8] == 0x46`.
  - `mesh_size = (hex_to_int(hti) - 1610612736) * 16` (1610612736 = 0x60000000).
  - Offsets de plantilla AMG: escribe en `84`, `116`, `144`, y cabecera `amg_head`.
  Sirve para entender cómo se "corta" un AMO0 en AMGs independientes. **Valor: Alto**.
- `https://steamcommunity.com/sharedfiles/filedetails?id=1570869204` y
  `https://steamcommunity.com/sharedfiles/filedetails?id=2941023657` — Guías de la Workshop que
  documentan el pipeline de **rip de modelos Budokai 1–3** paso a paso. **Valor: Medio**.
- `https://www.youtube.com/watch?v=jUArVyOAn7s` — "Modding Tutorial - Blender Model Editing for
  Budokai 3" (vídeo, del entorno del autor de la herramienta). **Valor: Medio**.

### 1.4 Cadena de extracción PS2 clásica (QuickBMS/3ds Max/Blender) — **MEDIA**
- `https://archive.vg-resource.com/thread-29785-post-622917.html` (The VG Resource, hilo
  "Budokai 3", 2016) — Tutorial completo de rip: Noesis + AFS Explorer + Game Graphic Studio +
  3ds Max + QuickBMS. Scripts: `2DragonballBin.bms` (descomprime bin→AMO/AMT),
  `3DragonballAMO.bms`, `4DragonballAMG.bms`. Maxscript de **killercracker** para importar AMO
  **con huesos, pero sin rigging** (`http://www.mediafire.com/download/bdi86yev83hzwm0/budokai_updated.ms`
  — enlace antiguo, verificar; probablemente muerto). **Valor: Medio-Alto** (pipeline PS2).
- `https://zenhax.com/viewtopic.php@t=1950.html` — "Dragonball Z Budokai 1 PS2 ?" (ZenHAX):
  scripts `3DragonballAMO.bms`, `4DragonballAMG.bms`, `DragonBallB1MeshFixed.bms` + aporte de
  **Szkaradek123**: unpacker `.amo` + importer `.amg` para **Blender 2.49**, "**No weights**".
  **Valor: Medio-Alto** (evidencia de que los pesos son el punto débil histórico).
- `https://github.com/DKDave/Scripts` — Colección enorme de scripts QuickBMS/Python/Noesis del
  autor (ex-XeNTaX/ZenHAX). No confirmé un script de Budokai, pero es el repositorio de referencia
  para localizar/subir parsers de formatos raros. **Valor: Medio**.
- `https://github.com/MatrixDJ96/DBZBT3` — AFL-Converter + AFS-Manager ("sucesor de AFSExplorer
  sin crashes"). Es de **Budokai Tenkaichi 3**, pero las herramientas de contenedor AFS/AFL son
  reutilizables. **Valor: Medio**.
- `https://github.com/hopesgit/Budokai3AP` — Archipelago (randomizer) para Budokai 3 PS2; avisa
  de que "nunca funcionará con la versión PS3/360 HD". Útil sólo para confirmar diferencias de
  build. **Valor: Bajo**.

### 1.5 Recompilación del juego hermano — **BAJO (contexto)**
- `https://github.com/WistfulHopes/DBZ1` — "Dragon Ball Z Budokai HD Recompiled" (ReXGlue SDK),
  123 estrellas. Repo mínimo (2 commits, sin docs), pero es el mismo pipeline y puede tener issues
  útiles. **Valor: Bajo-Medio**.

---

## 2. REVERSE ENGINEERING DEL FORMATO #AWG / #AWO DE LA HD

### 2.1 Estado público: prácticamente inexistente
No hay wiki, ni hilo de XeNTaX/ResHax, ni plugin de Noesis dedicado a `#AWO`/`#AWG`/`#AMB` de la
HD Collection. La documentación que existe es **privada** (Discords, herramientas internas de
modders). La fuente pública más cercana es la herramienta PS2 (§1.3) para el formato **original**.

### 2.2 Referencias de la capa gráfica HD (PS3/X360) — **ALTA**
- `https://github.com/FBobDev/PS3-recomp/blob/master/docs/RSX_GRAPHICS.md` — **Documento de oro**
  sobre el pipeline de dibujo de la era PS3/360 en estos ports:
  - Formato del FIFO NV47xx (cabecera: type/count/subchannel/method).
  - **`rsx_vertex_formats.h`**: tabla de tipos de atributo de vértice → DXGI:
    `None(0)`, `S1=snorm16(1)`, `F=float32(2)`, `SF=float16(3)`, `UB=unorm8(4)`,
    `S32K=s16(5)`, `CMP=packed 11-11-10(6)`, `UB256=uint8(7)`.
  - Logging de `NV4097_SET_VERTEX_DATA_ARRAY_FORMAT` → type/size/stride/offset y
    `SET_BEGIN_END` con prólogo `prim=5` (triangles) / `prim=6` (triangle strip).
  - **Valor: Alto**: es la referencia canónica para leer un vertex declaration de PS3/X360 y
    mapear el "tipo" de atributo que se ve en el bin HD (los `format` por AWG que observa el
    proyecto deberían encajar con S1/SF/UB/CMP).
  - También en `runtime_glue.cpp`/`rsx_commands.c` del SDK hay estado de vértices,
    `SET_TRANSFORM_PROGRAM_LOAD`, etc.
- `https://github.com/gnome41/dbz-budokai-hd/blob/master/CLAUDE.md` — Confirma:
  - El juego usa **EDGE (SPURS SPU geometry library)** para geometría, con MFC DMA LS→RSX.
  - Textura genérica `#A3T` con struct `CellGcmTexture` en `gcm_off+0x68`; formatos R5G6B5 (0xA4/0x84)
    y A8R8G8B8 (0xA5/0x85); swizzle Morton (Z-order) cuando el bit 5 del format está clear.
  - **Valor: Alto** como mapa mental del pipeline y como espejo de la versión PS3 para comparar
    con los bins 360.
- `https://wiki.cloudmodding.com/zgcn/BMD_and_BDL` — Formato BMD/BDL de GameCube (chunks INF1/
  VTX1/EVP1/DRW1/JNT1/SHP1). **Muy útil conceptualmente**: describe cómo un motor de la época
  separa *vertex data*, *skinning envelopes* (EVP1), **"Draw Matrix Array" (DRW1)** y
  *matrix groups* (SHP1). La noción de DRW1 —una tabla de matrices que **referencia geometría
  por rango/offset**— es exactamente el patrón que el proyecto intuye ("consumo posicional del
  pool" + "dos tablas de descriptores"). **Valor: Alto** (modelo mental de skinning/arms).
- `https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_020_Skins.md`
  — Referencia limpia de skinning: `inverseBindMatrices`, `joints`, `WEIGHTS_0`, "bind shape
  matrix". Útil para formalizar la matemática del pool por hueso. **Valor: Medio**.

### 2.3 Skinning por "matrix palette" y el **Vertex Offset Method** — **ALTA (hipótesis clave)**
- `https://download.nvidia.com/developer/SDK/Individual_Samples/DEMOS/Direct3D9/src/HLSL_PaletteSkin/docs/HLSL_PaletteSkin.pdf`
  — Matrix-palette skinning: bone transforms en constant registers, **índices de hueso embebidos
  en el vertex stream**, hasta 4 huesos/vértice, pesos embebidos.
- `https://developer.download.nvidia.com/assets/gamedev/docs/skinning.pdf` (Mesh Skinning, S.
  Dominé, GDC) — **El documento clave**: describe el **"Vertex Offset Method"**:
  - Hasta **12 matrices por primitiva, 4 por vértice, 28 accesibles**.
  - "Needs to send vertices in bone's space, i.e. **multiple versions of the same vertex, but each
    in the local bone space that the vertex is referencing**".
  - Datos por vértice: hasta 4 *vertex offsets*, 4 *weights*, 4 *indices*, y normales/bi-normales/
    tangentes por hueso.
  - **Esto encaja exactamente con**: (i) el layout sec34 tiene posición **bone-local** + peso +
    bone; (ii) tests T4/T5 (pool reordenado, IB consistente) **deforman** → hay consumo
    posicional/por offset; (iii) T6 (solo rango A) normal y T7 (IB invertido) masivo → el IB
    gobierna conectividad pero existe una vía adicional por offset.
  - **Valor: Alto**: es la mejor explicación pública para el bloqueador actual; sugiere buscar
    en el AWG una tabla de **offsets por hueso/por primitiva** (no sólo el IB y el descriptor A).
- `https://developer.download.nvidia.com/assets/gamedev/docs/GDC2001_EfficientAnimation.pdf` —
  Comparativa de técnicas de skinning (D3D7 vertex blend, fixed-function matrix palette, VS
  matrix palette) con el **address register `a0.x`** para indexar la paleta. Contexto de por qué
  el motor guarda "arms"/matrices y las indexa por vértice. **Valor: Medio-Alto**.

### 2.4 Estructura del juego 360 (para situar los bins) — **MEDIA**
- `https://archive.org` / listado 7z de la HD Collection USA (visto vía
  `https://ia801903.us.archive.org/view_archive.php?...Dragon%20Ball%20Z%20-%20Budokai%20HD%20Collection%20(USA).7z`)
  — Inventario de la build: `default.xex` (3 317 760 B), `DBZ3/yae3_xenon.xex` (4 890 624 B),
  `DBZ3/us/data_cmn.afs` (293 423 104 B), `adx_jpn/data_usi/data_fra/data_spn/data_yah`,
  `lang_jpn/lang_usa`, `DBZ1/*`. **Valor: Medio** (verifica tamaños/estructura; el proyecto ya
  tiene esto).
- `https://gamefaqs.gamespot.com/xbox360/676304-dragon-ball-z-budokai-hd-collection/data` y
  `https://gamefaqs.gamespot.com/ps3/676303-dragon-ball-z-budokai-hd-collection/data` —
  fichas de release (IDs, regiones, fecha). **Valor: Bajo**.
- `http://redump.org/disc/29970` — Datos de dump 360 (regiones, pistas). **Valor: Bajo**.

---

## 3. COMUNIDADES DE MODDING DE BUDOKAI Y DOCUMENTACIÓN PÚBLICA

### 3.1 Dónde está la gente — **MEDIA**
- `https://gamebanana.com/games/16989` — **"Dragon Ball Z: Budokai — Mods and Modding Resources
  by the Budokai Modding Community | Budokai Hub"**. Hub oficial de mods/tutoriales de la saga
  Budokai (carga por JS; navegar desde el sitio). **Valor: Medio-Alto** (punto de entrada a la
  comunidad y a recursos).
- `https://www.facebook.com/BudokaiCorp` — "Budokai Modding Community Discord" (página que
  redistribuye mods; enlace a servidor `discord.gg/feW` en el snippet, probablemente caducado).
  **Valor: Medio**.
- `https://top.gg/discord/servers/542888166685040642` — **Tenkaichi Modding Community** (136
  miembros). Es de *Budokai Tenkaichi* (Sparking), no del Budokai clásico, pero comparte técnicas
  de AFS/PS2 y tiene canales EN/ES. **Valor: Medio**.
- `https://discord.gg/9zT7NHP` — "Programming Discussions" citado en la herramienta de
  SamuelDBZMAAM. **Valor: Bajo**.
- `https://reshax.com/` — **Foro sucesor de XeNTaX** (los hilos de XeNTaX/ZenHAX viven
  migrándose aquí). Es DONDE preguntar por el formato `#AWO`. Ejemplos de hilos de RE de
  esqueletos: `https://reshax.com/topic/18092-how-to-reverse-boneskeleton-file/` (cómo inferir
  la jerarquía de huesos a partir de pesos y matrices — muy pertinente al problema de
  skinning/arms). **Valor: Alto** como canal de soporte de RE.
- `https://archive.vg-resource.com/thread-29785-post-622917.html` — Hilo histórico de The VG
  Resource con toda la cadena de herramientas PS2 (ver §1.4). **Valor: Medio-Alto**.

### 3.2 Documentación pública del formato de vértices/huesos extra/skinning
**No existe una wiki pública del `#AWO`/`#AWG`.** Lo más cercano:
- Herramienta Python PS2 (§1.3) con la lógica de AMO/AMG y caras.
- Hilos PS2 de ZenHAX/ResHax (endianness LE, huesos sin rigging).
- `ps23dformat.wikispaces.com` (la vieja wiki de formatos PS2, que tenía `Dragon+Ball+Z+Budokai+2`)
  está **MUERTA**: hoy redirige a `site-closed.wikispaces.com`. Solo quedan capturas parciales en
  Wayback y los `.bms` que circularon. **Valor: Nulo (caída)** — buscar los `.bms` en mirrors
  (ResHax / archive.org) si se necesitan.

---

## 4. TÉCNICAS DE REORIENTACIÓN DE MALLAS / NEAREST POINT ON SURFACE PRESERVANDO TOPOLOGÍA

> Contexto de uso: para eludir el bloqueador estructural del port completo, se puede **transferir
> la geometría PS2 sobre la topología/orden de pool de la plantilla HD** (mantener el orden del
> pool = constraint de la Vía A) usando herramientas de "transferencia de topología" en lugar de
> reordenar el pool. Esto da silueta PS2 con la conectividad/skinning HD intactos.

### 4.1 Herramientas comerciales / pro — **ALTA para el principio**
- **R3DS Wrap / Faceform Wrap** — Transfiere la topología limpia de una malla de referencia a otra
  (escaneo o malla) mediante *landmarks*. Es EL estándar para "poner topología A sobre forma B".
  - `https://www.versluis.com/2021/11/r3ds-wrap` (explicación divulgativa con caso de personaje).
  - `https://www.cgchannel.com/2019/06/r3ds-ships-wrap-3-4` (notas de versión: BlendWrapping, etc.).
  - `https://texturing.xyz/pages/vface-docs-2-2-a-wrap-r3ds` (workflow nodos `Loadgeo` +
    `SelectPointPairs`, selección de landmarks). **Valor: Alto** (proceso reproducible; de pago).
- **Houdini — Topo Transfer** — "Non-rigidly deforms a surface to match the size and shape of a
  different surface". Retargeting de topología con **landmarks** sobre dos mallas con solape.
  `https://www.sidefx.com/docs/houdini/nodes/sop/topotransfer.html`. **Valor: Alto** (procedural,
  exporta malla deformada; ideal para bake off-line).
- **Autodesk Maya — Mesh > Transfer Attributes** — Transfiere UV/CPV/posición entre mallas de
  **topología distinta** ("spatially based", diferentes count de vértices/aristas).
  `https://download.autodesk.com/global/docs/maya2013/en_us/files/Mesh__Transfer_Attributes.htm`.
  **Valor: Medio-Alto**.

### 4.2 Blender (gratis) — **ALTA**
- **Shrinkwrap Modifier** (Nearest Surface Point / Project / Nearest Vertex / Target Normal
  Project): mueve cada vértice al punto más cercano de la superficie objetivo.
  `https://docs.blender.org/manual/en/latest/modeling/modifiers/deform/shrinkwrap.html`.
  **Valor: Alto** (nearest point on surface nativo).
- **Data Transfer** (Transfer Mesh Data): transfiere **vertex groups (pesos), UVs, colores,
  normales** entre mallas con topologías distintas (mapeo 1-a-1 o varios-a-uno interpolado).
  `https://docs.blender.org/manual/en/2.80/modeling/meshes/editing/data_transfer.html`.
  **Valor: Alto** (permite fijar la malla HD y *bake* la forma/pesos PS2 encima).
- Discusión práctica de proyectar pesos de una malla a otra con Data Transfer:
  `https://blenderartists.org/t/project-weight-painting-from-one-mesh-to-another/1474243`.
  **Valor: Medio**.
- **Mesh Data Transfer** (addon, Maurizio Memoli) — transfiere forma/UV/shape keys/vertex groups
  sin necesidad de igual número de vértices. `https://blender-addons.org/mesh-data-transfer-addon`.
  **Valor: Medio**.
- **Import Export Skin Weights** (extensión oficial, Nguyen-Phuc-Nguyen, 2025) — exporta/importa
  pesos de vértice a JSON usando posición o UV (funciona mejor con misma topología o misma UV).
  `https://extensions.blender.org/add-ons/import-export-skin-weights`. **Valor: Medio-Alto**
  (útil para mover pesos entre formatos/plantillas vía script).
- **Rigify Mesh Retargetter** (addon) — retargeting de pesos de metarig a huesos DEF en Rigify.
  `https://github.com/cubedparadox/Rigify-Mesh-Retargetter-`. **Valor: Bajo-Medio**.

### 4.3 Algoritmos / papers — **MEDIA**
- **Rig Retargeting for 3D Animation** (Poirier, 2009) — adapta esqueletos complejos a mallas
  distintas usando *topology graphs* (Reeb graphs) y matching de arcos.
  `http://profs.etsmtl.ca/epaquette/Research/Papers/Poirier.2009/Poirier.2009.gi.pdf`.
  **Valor: Medio** (método de correspondencia esqueleto↔malla; conceptual).
- **Motion2Motion: Cross-topology Motion Transfer with Sparse Correspondence** (arXiv 2508.13139)
  — transferencia de animación con topologías muy distintas y **correspondencias dispersas de
  huesos** (`https://arxiv.org/html/2508.13139v1`). **Valor: Bajo-Medio** (animación, no mesh
  retargeting; útil si se quiere retargetear animaciones).
- **HuMoT** (arXiv 2305.18897) — representación de movimiento agnóstica a topología.
  `https://arxiv.org/html/2305.18897v3`. **Valor: Bajo**.
- **Retopology Tools (3ds Max)** — QuadriFlow, target face count, sharp edges.
  `https://help.autodesk.com/cloudhelp/2025/ENU/3DSMax-Retopology/files/GUID-5A960813-FBCC-4A5D-A423-3FCD60825B10.html`.
  **Valor: Bajo** (retopo, no transfer).
- **Shrinkwrap industrial** (HyperMesh/Altair, Rhino) — útiles como referencia del algoritmo
  "loose/tight wrap" y preservación de features. `https://2023.help.altair.com/...` y
  `http://docs.mcneel.com/rhino/8/help/...`. **Valor: Bajo** (ingeniería, no mallas de juego).

### 4.4 Referencia de pesos/skinning de motores — **MEDIA-ALTA**
- **PMX/MMD 2.0** (`https://gist.github.com/lordscales91/47ae1b7577e52a2babee` mirror) — spec
  clara de vértice: posición/normal/UV, tipos de peso `BDEF1/BDEF2/BDEF4/SDEF`, índices de hueso
  y pesos. Útil como plantilla para *re-emitir* skinning si alguna vez se reconstruye el formato.
  **Valor: Medio**.
- **Unity BoneWeight** (`https://docs.unity3d.com/ScriptReference/BoneWeight.html`) — 4 pesos
  ordenados descendentes, suma = 1. Recordatorio de la normalización que valida el proyecto.
  **Valor: Bajo**.
- **DirectXTK VertexTypes** (`https://github.com/Microsoft/DirectXTK/wiki/VertexTypes`) — ejemplo
  de vertex decl con blend weights + indices (formato D3D típico de la época). **Valor: Bajo**.

---

## 5. CELL (SEMI-PERFECT) EN MODS DE BUDOKAI HD

**Conclusión: no hay ningún mod público documentado de "Cell Semi-Perfect" para la HD Collection.**

Hallazgos tangenciales:
- `https://github.com/SamuelDBZMAAM/DBZ-Budokai-3-Modding-Tool` (README) — la lista de "to be
  added" menciona edición de auras y pone como ejemplo **"Cell's Super perfect aura mix with SSJ2
  Gohan aura"**. Es decir, Cell aparece como caso de uso de *aura*, no de modelo/malla.
  **Valor: Bajo** (pero confirma que Cell es objeto de modding PS2).
- `https://gamefaqs.gamespot.com/ps2/920505-dragon-ball-z-budokai-3/faqs/33828` y
  `https://gamefaqs.gamespot.com/ps2/939644-dragon-ball-z-budokai-tenkaichi-3/faqs/50979` —
  Confirman las formas de Cell en Budokai 3: transformación por absorción
  (`#17 Absorption` → Perfect Form). **Semi-Perfect = "2nd Form"**; no aparece como slot/modelo
  independiente en los listados de assets. **Valor: Bajo** (contexto de diseño de personaje).
- `https://dragonball.fandom.com/wiki/Dragon_Ball_Z:_Budokai_HD_Collection` — capturas de "Cell in
  Budokai HD" (galería), sin datos técnicos. **Valor: Bajo**.
- `https://en.wikipedia.org/wiki/Cell_(Dragon_Ball)` / `https://simple.wikipedia.org/wiki/Cell_(Dragon_Ball)`
  — descripción de la forma Semi-Perfect (sin alas, más humanoides, pies tipo bota, placa metálica
  en tobillos). **Valor: Bajo** (referencia visual si se reconstruye la malla).
- Nota interna del proyecto (AGENTS.md §10): el mejor port validado es **Cell (F2)** con
  `cell_npm4` / `cell_npm_fix` / `cell_best`. La limitación documentada es que Cell HD tiene
  **17 AWGs** (AWG0 cuerpo + 16 AWGs de 1 hueso = 48–63) y el PS2 sólo 48 huesos. Por tanto, el
  trabajo "Cell" es un caso de prueba, no hay mod de la comunidad que aporte.

---

## 6. TABLA-RESUMEN DE RECURSOS POR UTILIDAD

### Utilidad ALTA (leer/incorporar ya)
| Recurso | URL |
|---|---|
| Skinning "Vertex Offset Method" (hipótesis del consumo posicional) | https://developer.download.nvidia.com/assets/gamedev/docs/skinning.pdf |
| Matrix-palette skinning (bone index/weights en el vertex stream) | https://download.nvidia.com/developer/SDK/Individual_Samples/DEMOS/Direct3D9/src/HLSL_PaletteSkin/docs/HLSL_PaletteSkin.pdf |
| RSX/PS3 vertex formats + FIFO + draw (referencia de vertex decl) | https://github.com/FBobDev/PS3-recomp/blob/master/docs/RSX_GRAPHICS.md |
| Port PS3 de la HD (EDGE/SPU, #A3T, pipeline) | https://github.com/gnome41/dbz-budokai-hd |
| Herramienta modding PS2 (AMG cara, AMO0, offsets) | https://github.com/SamuelDBZMAAM/Budokai-Modding-Tool |
| Blender Shrinkwrap (nearest surface point) | https://docs.blender.org/manual/en/latest/modeling/modifiers/deform/shrinkwrap.html |
| Blender Data Transfer (pesos/UV entre topologías) | https://docs.blender.org/manual/en/2.80/modeling/meshes/editing/data_transfer.html |
| BMD/BDL (DRW1 = tabla de matrices que referencia geometría por rango) | https://wiki.cloudmodding.com/zgcn/BMD_and_BDL |
| Herramienta PS2: `amo_s.py` (offsets AMG) | https://github.com/SamuelDBZMAAM/Budokai-Modding-Tool/blob/master/amo_s.py |
| Houdini Topo Transfer | https://www.sidefx.com/docs/houdini/nodes/sop/topotransfer.html |
| Foro de RE (sucesor XeNTaX) para preguntar por el `#AWO` | https://reshax.com/ |

### Utilidad MEDIA
| Recurso | URL |
|---|---|
| Cadena extracción PS2 QuickBMS/3ds Max | https://archive.vg-resource.com/thread-29785-post-622917.html |
| ZenHAX Budokai 1 (Blender 2.49, sin pesos) | https://zenhax.com/viewtopic.php@t=1950.html |
| GameBanana Budokai Hub | https://gamebanana.com/games/16989 |
| R3DS/Faceform Wrap | https://www.versluis.com/2021/11/r3ds-wrap |
| Maya Transfer Attributes | https://download.autodesk.com/global/docs/maya2013/en_us/files/Mesh__Transfer_Attributes.htm |
| glTF skins (inverse bind, joints, weights) | https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_020_Skins.md |
| Rig Retargeting (topology graphs) | http://profs.etsmtl.ca/epaquette/Research/Papers/Poirier.2009/Poirier.2009.gi.pdf |
| Import Export Skin Weights (Blender ext.) | https://extensions.blender.org/add-ons/import-export-skin-weights |
| DKDave/Scripts (parsers QuickBMS/Noesis) | https://github.com/DKDave/Scripts |
| DBZBT3 AFS tools | https://github.com/MatrixDJ96/DBZBT3 |
| Tenkaichi Modding Community (Discord) | https://top.gg/discord/servers/542888166685040642 |
| Budokai Modding Community (Facebook) | https://www.facebook.com/BudokaiCorp |
| steam guides de rip Budokai | https://steamcommunity.com/sharedfiles/filedetails?id=1570869204 |
| YouTube: Blender model editing Budokai 3 | https://www.youtube.com/watch?v=jUArVyOAn7s |
| Hilo RE de esqueletos (inferir jerarquía desde pesos) | https://reshax.com/topic/18092-how-to-reverse-boneskeleton-file/ |

### Utilidad BAJA / NULA
- `ps23dformat.wikispaces.com` — **MUERTA** (redirige a `site-closed.wikispaces.com`).
- `https://github.com/WistfulHopes/DBZ1` — contexto, sin docs.
- Fichas de release (GameFAQs/redump/GameTDB) — sin valor de formato.
- Papers de retargeting de animación (HuMoT, Motion2Motion) — tangenciales.
- Menciones a Cell en guías de juego — sin valor técnico.

---

## 7. RECOMENDACIONES ACCIONABLES (derivadas de la web)

1. **Buscar la tabla de offsets por hueso/primitiva en el AWG** (hipótesis "Vertex Offset Method"
   de NVIDIA). El proyecto ya descartó el descriptor A (T6) y el IB solo (T7). El siguiente scan
   debería buscar **arrays de offsets/índices que apunten a `sec34 + k*stride`** en las
   inmediaciones del mesh group `0x1F80`/`0x2D49` (los "arms" `[bone, ptr, 0, ptr_matriz, 0]`),
   contrastando con la descripción de NVIDIA (4 offsets/vértice en espacio de hueso).
2. **Espejar el port PS3** (`gnome41/dbz-budokai-hd`): extraer el `data.afs` PS3 y comparar el
   layout del modelo PS3 vs 360. Si el pipeline es EDGE en ambos, la estructura de "arms"/batching
   debería ser análoga y el RE del código EDGE (aunque sea SPU) puede revelar cómo consume el pool.
3. **Usar herramientas de retargeting para mantener el orden del pool** (constraint de la Vía A):
   Blender Shrinkwrap (Nearest Surface Point) + Data Transfer de UV/pesos para *bakear* la forma
   PS2 sobre la topología HD, en lugar de reordenar el pool (que es lo que bloquea la Vía B). Esto
   convierte el problema "reordenar pool" en "mover vértices", que el guest sí acepta.
4. **Canibalizar la herramienta PS2 `SamuelDBZMAAM/Budokai-Modding-Tool`**: sus módulos de
   AMG Creation/Addition y "Removing face AMGs" documentan cómo se estructuran los AMGs de cara en
   PS2 (huesos 33–40). Ese es el mapa directo para **remapear por label los huesos PS2 33–40 a los
   AWGs 48–63 del HD** (objetivo declarado en AGENTS.md §10).
5. **Preguntar en ResHax** (`https://reshax.com/`) por el formato `#AWO`/`#AWG` de la HD Collection,
   adjuntando un bin y el layout de vértice ya deducido. Es el foro con más probabilidad de que
   alguien haya tocado el formato o pueda ayudar.
6. **No gastar más tiempo en encontrar un conversor público**: no existe. La estrategia correcta
   es RE propio + herramientas de retargeting + canibalizar tooling PS2.

---

## 8. NOTAS SOBRE LO QUE **NO** SE ENCONTRÓ (para evitar repetir búsquedas)

- No hay plugin de Noesis para `#AWO`/`#AWG`/`#AMB`.
- No hay wiki (Fandom/VG Resource/CloudModding) del formato HD 360.
- No hay hilo de XeNTaX/ResHax dedicado a extraer modelos de la HD Collection (360 o PS3).
- No hay conversor AMO↔AWO, ni siquiera experimental.
- No hay mod público de Cell Semi-Perfect, ni documentación de los "16 AWGs de cara" en 360.
- La vieja wiki `ps23dformat.wikispaces.com` ya no existe (solo Wayback parcial).
- Los `.ms`/`.bms` de la comunidad PS2 circulan por mirrors/MediaFire y muchos enlaces están
  muertos; buscarlos en archive.org o en ResHax si se necesitan.

---

*Fin del informe 01_WEB.md*
