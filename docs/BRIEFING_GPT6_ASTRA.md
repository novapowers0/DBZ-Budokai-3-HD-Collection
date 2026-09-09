# BRIEFING TÉCNICO — DBZ Budokai 3 HD Collection (ReXGlue recompilado)

> **Para**: GPT-6 Astra (asesor de diseño e ingeniería).
> **Autor**: proyecto `dbz3` (port PC recompilado de DBZ Budokai 3 HD Collection, Xbox 360).
> **Fecha**: 2026-09-07.
> **Objetivos que se piden**: (1) **añadir SLOTS DE PERSONAJE NATIVOS** al select
> del juego, y (2) **portar modelos PS2/3D en general** al formato HD del juego,
> incluyendo personajes que NO existen en versión HD.

---

## 1. QUÉ ES EL PROYECTO

DBZ Budokai 3 HD Collection es un port de **Xbox 360 → PC** hecho por
**recompilación estática** con el **ReXGlue SDK** (derivado de Xenia):

- El ejecutable Xbox 360 (`yae3_xenon.xex`, PPC32) fue recompilado a C++ x86
  (`generated/dbz3_recomp.*.cpp`, ~44 archivos). El código recompilado es el
  **parser/ejecutor real del juego**: todo lo que el juego lee está validado por
  las instrucciones del guest.
- El runtime host (`src/`, `rexglue-sdk-0.10/`) emula kernel, memoria guest,
  entrada, audio y gráficos (D3D12 principal, Vulkan experimental).
- Los datos del juego están en archivos **AFS** (contenedores por entrada):
  `data_cmn.afs` (personajes + contenido principal, 3990 entradas), `data_eng/
  ger/spn/fra/ita/usi.afs` (select/menús por idioma), `adx_*.afs` (audio).
- Los bins dentro del AFS están **comprimidos LZX** (`xbcompress /N:2048`).
- **La imagen descifrada del guest** se puede volcar a disco con un mini-tool
  (`out/analysis/guest_image/`): `dbz3_us_image.bin` (7 MB, mapa de memoria
  guest 0x82000000-0x826D0000). **El roster del juego vive AHÍ** (tablas
  estáticas de datos del guest), no en los archivos.

---

## 2. FORMATOS DE ARCHIVO (hechos validados empíricamente)

### AFS
- Header: magic `AFS`(3B)+pad(1B)+count u32 → tabla `(addr u32, size u32)` ×N.
- La tabla se lee en **offset 8** (NO en 0x10).
- El runtime host soporta **override por entrada** sin reempaquetar:
  `mods/<mod>/us/data_cmn.afs/<entry_index>/geom.bin` (bin LZX comprimido).
  Si el bin crece más que el slot que lee el guest (`to_read = ceil(slot/0x1000)*0x1000`),
  el runtime aplica **MID-INSERT VIRTUAL** (`AfsGetVirtualTable`): la entrada
  crece in-place (align 0x800) y las posteriores se desplazan — tabla virtual
  consistente. Esto permite servidores de bins MÁS GRANDES que el original.

### Bin de personaje (#AMB)
- `#AMB` (big-endian) → contiene `#AWO` (modelo), `#AWG*` (mesh groups),
  `#AZT` (textura DDS DXT3/BC2).
- **El bin es AUTOCONTENIDO**: cada personaje trae su propio formato de vértice;
  el guest autodetecta. Hay formatos A/B/C de vértice distintos.

### Layouts de vértice verificados (¡crítico!)
- **Formato C (mayoría: Goku, Vegeta, Babidi, Goten)** — AWG0, stride 44:
  ```
  +0 x | +4 y | +8 z (local del hueso, [-1,1]) | +12 0xFFFFFFFF
  +16 u | +20 v | +24 n.x | +28 n.y | +32 n.z | +36 weight | +40 BONE
  ```
  IB = triangle STRIP (índices consecutivos, winding alternado, degenerados como
  saltos, SIN restarts 0xFFFF). `+0x2C` = tamaño del buffer en bytes.
- **Formato sec34 (Krillin, stride 44, align +2)**:
  ```
  +0 FFFF | +4 u | +8 v | +12 z_local | +16 x_local | +20 y_local
  +24 peso | +28 BONE (u32 0-35) | +32 n.z | +36 n.y negado | +40 n.x
  ```
- **vb2 (estático)**: posiciones ABSOLUTAS, bone=0xFFFFFFFF (sin skin).
- **AWG de cara** (Goku/Vegeta): buffer en h+0x1F0, stride 44, bone cabeza.
- **Mapeo de huesos**: 51 huesos Krillin con labels; el sec34 usa bones 0-35
  (piernas/rostro van al vb2). Los esqueletos **HD == PS2** (mismo juego, misma
  numeración de bins que la Greatest Hits PS2).

### Estructura de dibujo (mesh group del AWG0)
- **Mesh-ref blocks** (0x50 c/u): tipo de vértice (B5 cuerpo / B4 facial) +
  textura/shader + `X,Y` = índice de parte y hueso primario.
- **Ejes** (80B c/u): quat+pos (bind pose) + sello + arm_ptr + hijo/hermano/padre.
- **Matriz de zonas** (0x28E0): diagonal de huesos + punteros a bboxes.
- **Bboxes** (AABB por zona, 0x40).
- **Descriptores** (0x60 c/u): label + `max N m` + **rango A** (vértices del
  pool sec34) + **rango B** (índices del IB). Verificado: los índices del IB en
  rango B caen SIEMPRE en el rango A.
- El guest dibuja los strips por **descriptores A/B + IB**; el transform usa el
  **bone del vértice (+28)**. **El orden del pool sec34 importa**: la estructura
  referencia el pool por índice (mesh-ref/zonas/bboxes).

---

## 3. OBJETIVO 1 — SLOTS DE PERSONAJE NATIVOS

### Estado actual (qué sabemos)

**El roster del select vive en la IMAGEN del guest** (no en los archivos AFS).
Localizadas dos tablas estáticas:

1. **Tabla de retratos/slots del select** en `0x82372818` (offset imagen
   `0x372818`): **78 u32 = 39 slots × 2 entradas** (par por slot; el segundo
   valor suele ser `primer+1`). El slot 10 = `[3930, 3931]` = Krillin y el
   slot 19 = `[3934, 3935]` = Nappa (ambos **CONFIRMADOS por experimento** de
   sustitución: override 3934→slot 3930 mostró el retrato de Nappa en Krillin).
   Las 39 filas corresponden a los personajes seleccionables del select.
2. **Tabla de bins por personaje** en `0x823268C0` (offset `0x3268C0`): runs de
   índices AFS separados por `0xFFFFFFFF` (modelos → CAM → LIPS/ANM). El run
   `[323..329]` contiene el grupo de Krillin (327/328/329).

**Funciones del guest identificadas** (en el código recompilado):
- `sub_8217F3F0` (`.15.cpp:13877`): consumidora de la tabla de retratos. Hace
  `lis -32201; addi r9,r9,10264` → `0x82372818`; índice `slot*8 + flag*4`
  (slot id leído como u16 en `r3+64`; un bit flag en `r3+62`).
- `sub_8217F478` (`.14.cpp:14437`): lee slot id u16 en `r3+64`; **si es `0xFFFF`
  = slot vacío** (retorna sin actuar); si no, llama a `sub_8217F3F0` para
  resolver el retrato. **Este es el mecanismo de "slot vacío" — un gancho
  natural para slots nativos.**
- `sub_82180AA0` (`.15.cpp:13916`): indexa una **base de personaje con
  `mulli r10,r10,184`** → struct de 184 bytes por personaje, leyendo offsets
  `+14/+18/+114` (u16). Los slots se enumeran con un contador (`r29`) comparado
  contra un byte en `r30+12`.

**Cómo se hace un swap hoy (validado)**: extraer el bin `#AMB` de un personaje
(origen), comprimirlo LZX `/N:2048`, instalarlo como override por entrada en el
slot destino (`mods/<mod>/us/data_cmn.afs/<dest>/geom.bin`). Funciona porque el
guest acepta bins autocontenidos. **Pero esto REEMPLAZA un slot existente; no
AÑADE un slot nuevo.**

### Preguntas de diseño para ti (Objetivo 1)

1. **¿Cómo se decide el conteo de slots (39) en el guest?** El contador en
   `sub_82180AA0` compara contra un byte en `r30+12`. ¿Es un límite fijo en el
   código, o un valor que vive en una tabla de la imagen del guest que podemos
   parchear? ¿Cómo lo confirmamos de forma barata?
2. **Estrategia de "slot nativo": ¿hook host en runtime vs re-codegen?**
   El host controla la imagen descifrada en memoria antes de lanzar el guest.
   Opciones:
   - **(a) Patch de la imagen en memoria** al arranque: modificar la tabla
     `0x82372818` (añadir filas de retrato) y la tabla de bins `0x823268C0`,
     sin tocar código. ¿Es viable si el conteo está hardcodeado?
   - **(b) Patch del código recompilado**: modificar las funciones recompiladas
     (p.ej. el límite del bucle) en `src/` y recompilar. Más invasivo pero
     deterministic.
   - **(c) Reutilizar el mecanismo de slot vacío**: `0xFFFF` = vacío. ¿Hay slots
     vacíos entre los 39? ¿Podemos "despertar" uno apuntándolo a un personaje
     nuevo (modelo + retrato + auxiliares) por override?
3. **¿Qué más necesita un slot además de retrato y modelo?** Para que un
   personaje sea JUGABLE hace falta: modelo (bin `#AMB`), retrato (tabla del
   select + textura en `data_eng.afs`), CAM/LIPS/SCOUT, moveset/animación (bin
   `#ACM` grande), voz, aura (0-43) y la entrada en el struct de 184 B/slot.
   ¿Cómo mapear completo el struct de 184 B y qué campos distinguen un slot
   "jugable" de un "vacío"?
4. **Mapeo personaje→bins completo**: tenemos el mapa definitivo
   (`docs/03_formatos/MAPA_ROSTER_HD.md`): modelos + CAM + LIPS + ANM + aura por
   personaje, validado con catálogo + probe real + nombres AFL (desfase +6
   con la Pal). ¿Basta como inventario de dependencias para un slot nuevo, o
   hace falta más (voz, stats, paleta de colores)?
5. **¿Hay una vía intermedia de menor riesgo?** Por ejemplo: primero añadir un
   slot que reutilice un personaje EXISTENTE (doble slot = traje alternativo
   duplicado) y después un personaje NUEVO (portado). ¿Qué validación por capas
   recomiendas (modelo en combate → retrato → transformaciones → voz → guardado)?

---

## 4. OBJETIVO 2 — PORTAR MODELOS PS2/3D EN GENERAL A B3 HD

### Contexto
El B3 HD 360 **ES el mismo modelo PS2** (51 huesos, 18 mesh-groups, labels
idénticos) solo en big-endian con magics renombrados (`#AMO0→#AWO`, `#AMG→#AWG`,
`#AMT→#AZT`) y layout de mesh-groups distinto. La numeración de bins HD =
`data_cmn` de la **Greatest Hits PS2**. Tenemos acceso a los AFS PS2
(`ps2_games/`): B1, B2, B2V, **B3 GH**, IW (Infinite World).

### Vías investigadas (todo documentado, verificado en juego)

| Vía | Estado | Resultado |
|---|---|---|
| Swap nativo B3→B3 (HD→HD) | ✅ FUNCIONA | bin `#AMB` completo en slot ajeno |
| Inyección PS2→HD (Vía A) | ✅ FUNCIONA (reconocible) | cuerpo PS2 + extremidades/cabeza HD; parámetro **umbral binario 0.8** |
| Port completo (topología PS2, Vía B) | ❌ NO (amorfo) | pool reordenado rompe la estructura de dibujo |
| Swap de cabeza HD→HD | ◑ parcial | z-fighting, pausado |
| Janemba IW→B3 | ❌ FRACASO documentado | masa deforme; NO reintentar sin conversor completo |

### Pipeline de port PS2→HD (existe, `mod center hd/ports/`)
`port_ps2_b3_extract → geometry → draw → pack → verify`. Ya resuelto:
- **extract**: parsea malla PS2 + IB real (FaceType) + rig (bone+peso) +
  esqueleto (labels + jerarquía + matrices world). Geometría verificada punto a
  punto = PS2 exacto.
- **geometry**: coords locales + bone → buffers HD (sec34 44B skinned + vb2 44B
  estático + IB u16 BE). **Conversión**: `local = inv(world[bone])·model`;
  normales `[nz,-ny,nx]`.
- **pack + verify**: empaqueta `#AMB` autocontenido + LZX + override + export
  OBJ para feedback sin abrir el juego.

**El BLOQUEO real del port completo (Vía B)**:
> El **orden del pool sec34 está atado a la estructura de dibujo por índice**
> (mesh-ref blocks, matriz de zonas, bboxes, descriptores A/B). Al reordenar el
> pool a topología PS2, esa estructura queda incoherente → deformación en juego.
> La **inyección funciona porque mantiene el orden del pool de la plantilla**.
> (Verificado: reverse test con pool invertido DEFORMA; bone0 test: el guest usa
> el bone del vértice +28.)

**La inyección (Vía A) = el port PRÁCTICO hoy**: template HD (mantener orden
del pool) + posiciones/normales PS2 convertidas a bone-local. Limitación: no es
la topología PS2 exacta (mallas de cara/mezclas imperfectas; parámetro umbral
binario crítico: 0.8 bueno, 2.0 malo; los soft/blends SIEMPRE empeoran).

### Preguntas de diseño para ti (Objetivo 2)

1. **¿Cómo desbloquear el port completo (Vía B)?** El discriminador pendiente:
   hacer reverse + regenerar **UNA pieza a la vez** (arms / mesh-ref X,Y /
   matriz de zonas+bboxes / descriptores) para hallar cuál, al corregirse, deja
   de deformar. ¿Cómo diseñarías ese experimento para aislar el enlace
   estructura→pool? ¿Qué estructura sospechas (arms con offsets de vértice,
   matriz de zonas, mesh-ref)?
2. **¿Vale la pena Vía B, o la inyección es el destino correcto?** El retorno
   de Vía B es la topología PS2 exacta (cara/piernas correctas). El coste es
   reconstruir toda la estructura de dibujo. ¿Recomiendas invertir en Vía B o en
   mejorar la calidad de Vía A (umbral por zona para cabeza, costuras,
   blending)?
3. **Portar personajes que NO existen en HD (IW)**: el objetivo final es traer
   personajes de Infinite World / PS2 3D en general. Requisito identificado:
   esqueleto **1:1** (mismos labels/orden) con un bin HD destino — si no, hay
   que hacer retargeting (donde Janemba fracasó). ¿Cómo encontrar/validar
   esqueletos 1:1 de forma barata y qué estrategia de retargeting recomiendas
   para el caso no-1:1?
4. **Requisitos de texturas**: `#AMT` (PS2 LE) → `#AZT` (HD DXT3/BC2, header DDS
   128B + bitmap). ¿Recomendaciones para el conversor automático de texturas
   (mipmaps=0, mantener tamaño para no romper to_read)?
5. **Cadena de validación sin abrir el juego**: ya existe export OBJ
   (`awg_to_obj_b3.py`, `awg0_export.py`, `awg_cara_export.py`). ¿Qué checks
   adicionales (bounds, NaN, conteo de tris, coherencia A/B) recomiendas para
   iterar más rápido?

---

## 5. RESTRICCIONES DE INGENIERÍA DUROS (no adivinables)

- El layout del vértice B3 **NO** es el del B1: el **bone va en +28**, no en
  +0x10/+16. Las herramientas viejas que usan layout B1 producen masa deforme.
- Compresión LZX del juego: **`/N:2048`** (NO `/N:32`; con /N:32 el bin excede
  el slot y el guest trunca → crash).
- Padding al **tamaño exacto** que lee el guest (`to_read = ceil(slot/0x1000)*0x1000`),
  o usar el mid-insert virtual si el bin crece más.
- Un mod está activo si NO tiene el marker `.disabled`; **un solo mod activo por
  test** (el override sirve el primer mod por orden alfabético).
- Tras recompilar el SDK, copiar las DLL canónicas al build (el build
  sobrescribe `rexruntime.dll` con una versión stale). Verificar siempre
  `Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"`.
- No tocar `github/` (copia de subida manual, sin commits automáticos).
- Código recompilado: `generated/` (US) + `generated_eu/` (EU); el ejecutable es
  dual-región (detecta el xex por MD5).

---

## 6. LO QUE NECESITAMOS DE TI (resumen ejecutivo)

1. **Slots nativos**: estrategia para añadir UN slot de personaje nuevo al
   select sin romper el guest: ¿patch de imagen en memoria (hook host) vs
   patch de código recompilado vs reutilizar slots vacíos (`0xFFFF`)? ¿Cómo
   localizar el límite/conteo de slots y el struct de 184 B/slot completo?
2. **Port completo**: diseño del experimento discriminador para localizar el
   enlace estructura→pool (qué pieza hay que regenerar al reordenar el pool) y
   veredicto: ¿Vía B (topología exacta) o Vía A (inyección) como destino?
3. **Port de personajes nuevos (IW)**: cómo validar esqueletos 1:1 de forma
   barata y estrategia de retargeting para el caso no-1:1.
4. **Priorización**: dado el estado actual, qué orden de trabajo recomiendas
   (slots nativos vs port de personajes vs ambas en paralelo) y qué hitos de
   validación por capas propones para cada uno.

---

## 7. ARCHIVOS CLAVE PARA PROFUNDIZAR (si te los pueden adjuntar)

| Tema | Ruta |
|---|---|
| Mapa definitivo roster HD (slot→personaje→bins) | `docs/03_formatos/MAPA_ROSTER_HD.md` |
| Auditoría de contenido completa | `docs/03_formatos/AUDITORIA_DATA_CMN.md` |
| Estructura de dibujo HD (mapeada) | `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md` |
| Hoja de ruta del port PS2→B3 | `docs/07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md` |
| Sesión de inyección (Vía A, umbral 0.8) | `docs/07_ports/SESION_INYECCION_2026-08-26.md` |
| RE del port (conv2, amorfo) | `docs/07_ports/SESION_PORT_RE_2026-08-26.md` |
| Imagen descifrada del guest (tablas del roster) | `out/analysis/guest_image/dbz3_us_image.bin` |
| Código recompilado (parser real) | `generated/` (tablas en `recomp.14/15.cpp`) |
| Contexto operativo completo | `AGENTS.md` |
| Hoja de ruta 2026-09 | `docs/HOJA_DE_RUTA_2026_09.md` |
| Pipeline de port (scripts) | `mod center hd/ports/` |
| Formato bin | `docs/03_formatos/AWO_FORMAT.md`, `BIN_LAYOUT.md` |