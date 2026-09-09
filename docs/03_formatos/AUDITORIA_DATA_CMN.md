# AUDITORÍA DE CONTENIDO — data_cmn.afs (mapeo completo)

> Fecha: 2026-09-02. Resultado de la Fase 3.1 de `docs/HOJA_DE_RUTA_2026_09.md`.
> Método: se descomprimió (xbdecompress XDK) y clasificó cada entrada >100 KB
> del AFS por sus magics internos (#AWO/#AWG/#AZT/#ACM/#AMB). Mapa crudo:
> `mod center hd/data_cmn_map.txt` (3990 líneas: `entrada | comprimido | clase`).

## RESUMEN

| Rango de entradas | Contenido | Firma típica |
|---|---|---|
| 44-69 | **STAGES (candidatos)** — entornos multi-modelo | `#AMB2 #AWO5-16 #AWG7-24 #AZT2-8 #ACM1-7` (1.8-4.3 MB descomp.) |
| 70-505 | **Personajes** — modelos + textura (1 #AWO cada) | `#AMB1 #AWO1 #AWG15-26 #AZT1` (~0.7-1 MB) |
| 504-555 | **Efectos** (Kamehameha, Final Shine, etc.) | `#AMB3 #AWO1-4 #AWG1-7 #AZT4-9 #ACM1-2` |
| 127, 358, 435, 444, 2208, 2209, 3881 | **Animaciones/movesets** (#ACM) | `#ACM` (1-3 bloques) |
| 481, 3877, 3884-3899 | **Texturas sueltas** | `#AZT1` (~1 MB) |
| 3735-3847 | **STAGES (candidatos)** — 1 #AWO gigante | `#AWO1 #AWG1` (0.9-6.2 MB) |
| 3979-3982 | **Vídeos** (intro/ending) | MPEG (`00 00 01 BA`) |
| 3983-3989 | Audio DRM (IECS/XMA) + "dummy" | `IECS...` / `xma` |

## 1. STAGES — localización VALIDADA (2026-09-02)

Validación con `awo_tools/stage_analyze.py` (reutiliza el parser de
`awg_to_obj_b3.py`): se contaron los bloques #AWO por bin y los vértices/
triángulos de cada uno. **Ningún personaje supera ~2-3K vértices; los stages
tienen 7K-102K.** Conclusión: los stages del B3 viven en DOS zonas:

### Zona A (44-69) — entornos multi-pieza (13 stages)
Cada bin = contenedor `#AMB` con **5-16 bloques #AWO** (plataformas, props,
elementos animados) + texturas #AZT + animación #ACM:

| entry | #AWO | verts | tris | descomp. |
|---|---|---|---|---|
| 44 | 6 | 16.5K | 8.9K | 1.9 MB |
| 46 | 6 | 16.6K | 8.9K | 1.8 MB |
| 48 | 6 | 13.4K | 12.3K | 1.9 MB |
| 50 | 8 | 26.5K | 20.4K | 3.1 MB |
| 52 | 11 | 27.1K | 20.2K | 3.5 MB |
| 54 | 5 | 14.2K | 12.5K | 2.1 MB |
| 56 | 16 | 34.4K | 27.8K | 4.0 MB |
| 58 | 3 | 7.4K | 6.5K | 1.1 MB |
| 60 | 14 | 33.9K | 28.4K | 4.2 MB |
| 62 | 14 | 28.2K | 19.1K | 3.9 MB |
| 64 | 10 | 26.0K | 20.8K | 3.6 MB |
| 66 | 5 | 21.2K | 14.5K | 2.1 MB |
| 68 | 12 | 29.3K | 20.5K | 4.2 MB |

Los bins pequeños intercalados **53/57/59/61/63/65/69** (~260 KB, #AMB SIN
#AWO) = **colisión/física** de cada stage (patrón: 1 bin de modelo + 1 de
colisión por stage).

### Zona B (3735-3847) — mesh único gigante (7 stages)
Cada bin = **#AWO raw de un solo mesh** (sin wrapper #AMB):

| entry | verts | tris | descomp. |
|---|---|---|---|
| 3735 | — (parse layout distinto) | — | 0.9 MB |
| 3786 | 102.2K | 138.2K | 6.0 MB |
| 3788 | 16.3K | 11.9K | 0.9 MB |
| 3821 | 102.2K | 138.7K | 6.0 MB |
| 3823 | 16.3K | 11.9K | 0.9 MB |
| 3845 | 51.8K | 37.9K | 2.7 MB |
| 3847 | 19.2K | 15.1K | 1.0 MB |

Total: **~20 stages** (coincide con el roster del B3).

### ⚠️ Nota técnica
El layout de vértice de los stages **difiere del de personajes** (varias
lecturas dan posiciones FLT_MAX/0): los stages usan mayoritariamente vb2
(estático) y un esquema de ejes distinto. Para EDITAR stages (F3.4) hace
falta RE del layout de vértice de stage (ver `docs/07_ports/`).

### Pendiente
- Cruzar cada bin con el nombre del stage (la pantalla de select vive en
  `data_eng/ger/spn/fra/ita/usi.afs`).
- Localizar los **#AZT sueltos 3877/3884-3899** (¿texturas compartidas de
  stage?) y el 481.

## 2. PERSONAJES (70-505)

- **Mapa definitivo personaje→bins**: `docs/03_formatos/MAPA_ROSTER_HD.md`
  (2026-09-07) consolida catálogo HD + probe real + nombres AFL. Cada personaje
  tiene: modelos (`#AMB1 #AWO1 #AWG* #AZT1`) + CAM + LIPS + ANM (`#ACM` grande)
  + opcional SCOUT + AURA (0-43).
- La numeración coincide con la **data_cmn de la Greatest Hits PS2**. La lista
  de nombres `data_cmn.afl` (Pal) coincide con la HD hasta 286 y con desfase
  **+6** desde ~287 (la GH añadió 6 modelos angelicales de Goku 281-287). El
  desfase deriva localmente (Recoome/Raditz/Saibaman, Trunks): el catálogo HD
  (`catalog_b3.cat`) es la autoridad para modelos.
- **Un slot de personaje = 1 bin #AMB** con `#AWO1 #AWG15-26 #AZT1`. El swap
  nativo (AGENTS §3.4) ya aprovecha esto: bin completo #AMB en slot ajeno.
- **El moveset/animación de cada personaje YA está localizado** (columna ANM del
  mapa, p.ej. Krillin = 332/333, Goku = 290-292, Vegeta = 433-435/437): son los
  bins `#ACM#AMB` grandes intercalados tras cada grupo de modelos, identificados
  por nombre AFL (CAM/LIPS/ANM) y tamaño (1.2-2.4 MB). ⚠️ La firma #ACM sola no
  distingue moveset de animación de escenario; el nombre + la posición en el
  grupo de personaje sí.

## 3. SLXS / ROSTER — siguiente paso RE

- **PS2**: el roster→trajes→bins se edita vía SLXS (bloques MDB 0x60 / CDB
  0x174) con `SLXS Editor v0.50` (mod center/) y tutoriales en `modding
  resources update 2/SLXS Edit Tutorial - Lesson *` (1-1, 1-2 añadir modelos a
  trajes, 2-1 bloques de personaje, 2-2 transformaciones, 3-1 auras, 4-1
  pantalla de select).
- **HD 360 — 🔴 NO existe archivo SLXS**: el equivalente NO está en data_cmn
  (3983-3989 = audio DRM IECS) ni como archivo suelto en los AFS. El roster HD
  vive en el **código del guest** (generated/) + los composites de `data_eng`.

### 3.1 Auditoría de `data_eng.afs` (2709 entradas, select/menús) — 2026-09-02
- **Entry 0** = composite del **character select** (`#AMB` 28 MB: 5 secciones
  `#ACA` + 5 texturas `#AZT` + 1 modelo `#AWO` con 3 `#AWG`). Las 5 secciones
  #ACA = páginas del menú (personajes / stages / modos).
- **1976, 2043** = texturas gigantes `#AZT` (29-34 MB: retratos/fondos del
  select).
- **`#SKC`** (entradas pequeñas, p.ej. 4) = config de UI/rects de pantalla (no
  roster).
- Composites `#AMB+#ACA` (1-3, 5-7...) = otras pantallas (VS, resultados...).
- ~2600 entradas pequeñas (2-16 KB) = UI/textos.
- **Conclusión para F3.3**: "añadir slot de personaje" en HD = duplicar la
  entrada del guest (la que referencia el bin en data_cmn) + duplicar su
  retrato en el composite del select (entry 0). El mapeo exacto
  (slot→bin) requiere **instrumentar el guest en runtime** (loguear qué bins
  de data_cmn carga al abrir el select con cada personaje).

### 3.2 Trace de reads del guest — captura parcial 2026-09-07

Se capturó `out/build/win-amd64-release/dbz1_afs_reads.log` con el trace de
`HostPathFile::ReadSync`, mientras se recorrieron los personajes y stages
disponibles en la partida. El log tiene 663 reads totales, de los cuales 417
son de `data_cmn.afs`.

#### Modelos confirmados por firma `#AWO1#AWG*#AZT1#AMB1`

| Entry | Identificación | Observación |
|---:|---|---|
| 91 | Dr. Gero | modelo, no jugable según catálogo |
| 141 | Kid Buu | modelo |
| 181 | Freeza forma 1 | modelo |
| 258 | Ginyu | modelo |
| 264 | Goku normal | modelo |
| 270 | Goku normal alternativo | modelo |
| 327 | Krillin | modelo; entrada de referencia conocida |
| 345 | Nappa | modelo |
| 350 | Piccolo normal | modelo |
| 360 | Raditz | modelo |
| 366 | modelo sin nombre en el catálogo actual | requiere label interno |
| 400 | Tenshinhan | modelo |
| 416 | Vegeta sin armadura | modelo |
| 445 | Yamcha pelo corto | modelo |

Además aparecen entradas pequeñas intercaladas inmediatamente después de
varios modelos: `91→94`, `141→144`, `181→195`, `258→261`, `264/270→289`,
`327→331`, `345→348`, `350→355`, `360→359/363`, `366→365/369`,
`400→403`, `416→431` y `445→449`. El trace demuestra que son recursos leídos
en la misma secuencia de carga, pero **no permite afirmar todavía** que cada
bin pequeño sea exclusivamente el moveset o la voz de ese personaje; algunos
pueden ser tablas/configuración compartida.

#### Contenido no relacionado con roster

- `3881` es `#ACM1` y se lee tres veces: recurso de animación/moveset común,
  todavía sin asignación inequívoca a un personaje.
- `3971`, `3973`, `3976-3981` generan lecturas grandes repetidas; corresponden
  al bloque tardío de vídeo/cinemática del AFS, no a modelos de personaje.
- `3983-3988` son las entradas DRM/audio ya identificadas.
- `data_spn.afs`, `adx_jpn.afs` y `adx_usa.afs` aparecen por localización y
  audio, no deben mezclarse con el mapa de modelos.

#### 3.2.1 Relectura del orden de carga y candidatos de retrato

La captura sí contiene la información útil para F3.3, aunque no aparecen
líneas `mapped entry=`. La información está en los `ReadSync` por páginas: el
guest carga primero grupos de entradas `3884-3951`, que el mapa clasifica como
`#AZT1` sueltos de tamaño descomprimido uniforme (~1,048,800 bytes), y después
lee el modelo de personaje de `70-505`. Por tanto, esos grupos no deben
clasificarse como vídeo. Las lecturas paginadas de vídeo son las entradas
`3971-3981`, clasificadas como `ERR` por el escáner de contenido.

La siguiente tabla es un mapa **candidato** derivado del orden de la captura,
no una referencia interna explícita. La confianza sube cuando un grupo `39xx`
precede a un modelo conocido y se repite con la misma variante:

| Grupo #AZT candidato | Modelo leído después | Identificación | Confianza |
|---:|---:|---|---|
| 3924-3925 | 264, después 270 | Goku normal y variante | media |
| 3918 | 246 | Gohan niño | media |
| 3958 | 416 | Vegeta sin armadura | media |
| 3930 | 327 | Krillin | media |
| 3938 | 350 | Piccolo normal | media |
| 3952 | 400 | Tenshinhan | media |
| 3960 | 445 | Yamcha pelo corto | media |
| 3940 | 360 | Raditz | media |
| 3934 | 345 | Nappa | media |
| 3922 | 258 | Ginyu | media |
| 3942 | 366 | modelo aún sin label | media |
| 3912 | 181 | Freeza forma 1 | media |
| 3892 | 91 | Dr. Gero | media |
| 3898 | 128 | Majin Buu gordo | media |
| 3900 | 133 | Super Buu | media |
| 3902 | 141 | Kid Buu | media |

La asociación observada es más amplia que la lista inicial de 14 modelos: la
captura también carga `246` (Gohan niño), `128` (Majin Buu gordo) y `133`
(Super Buu). En los casos con dos variantes, como `264→270`, el mismo grupo de
texturas de 1 MB puede ser compartido por varias apariencias; no se debe crear
un retrato nuevo por cada bin hasta comprobar el contenido visual.

La secuencia no demuestra todavía si un grupo `39xx` es un retrato único, una
pareja normal/alternativa, o un bloque compartido por varias entradas del
select. Para confirmarlo hay que descomprimir/exportar cada `#AZT1` y comparar
la imagen, no inferirlo únicamente por proximidad temporal. `3881` sigue siendo
un `#ACM1` y aparece entre las dos variantes de Goku; puede ser una tabla o
animación común, pero no debe asignarse aún como moveset de Goku.

La captura no contiene lecturas de los stages conocidos (`44-69` o
`3735-3847`), por lo que esta sesión fue una captura del flujo de personajes,
no una captura válida para mapear stage→bin.

#### 3.2.2 Exportación de candidatos `#AZT1` (2026-09-07)

Las entradas candidatas se probaron directamente, sin pasar por
`texture_b3.py`: esa herramienta está diseñada para bins de personaje que
empiezan por `#AMB`, mientras que estas entradas son bloques `#AZT` autónomos.
La utilidad de análisis `awo_tools/extract_azt_afs.py` descomprime y exporta
cada entrada a PNG.

Resultado para las 17 entradas candidatas `3892, 3898, 3900, 3902, 3912,
3918, 3922, 3924, 3925, 3930, 3934, 3938, 3940, 3942, 3952, 3958, 3960`:

- Cada entrada contiene exactamente una textura.
- Todas las texturas son de `288x352`.
- Los 17 PNG tienen hashes SHA-256 distintos; no son copias binarias del
  mismo retrato.
- Los PNG de análisis están en `out/analysis/azt_exports/` y no forman parte
  de ningún mod activo.

Esto confirma que los `#AZT1` son recursos gráficos independientes, pero no
permite asignar nombres de personaje sin inspección visual. Por tanto, las
parejas `#AZT1 → modelo` de la tabla anterior siguen siendo asociaciones
temporales de confianza media, no un mapeo confirmado de slot.

#### Limitación del primer trace

 El entry 0 de `data_eng.afs` (composite del select) no aparece como read
normal. El guest puede accederlo mediante `HostPathEntry::OpenMapped`, que no
pasa por `HostPathFile::ReadSync`. El runtime ya fue actualizado para registrar
también la creación del mapping (`mapped entry=...`), pero esto solo identifica
el rango inicial que se mapea; no garantiza un evento por cada página que el
guest toque después. Por ello esta captura resuelve parte del mapa
**personaje→bin de modelo**, pero no el layout slot→retrato del select ni todos
los stages bloqueados. El siguiente trace fino tendría que instrumentar los
faults/lecturas del wrapper `MappedMemory`, si el backend elegido los expone.

## 4. HERRAMIENTAS DE LA AUDITORÍA (awo_tools/)

- `afs_list.py <afs>` — tabla completa (entrada, addr, tamaño, magic).
- `afs_probe.py <e1 e2 ...>` — extrae y descomprime entradas; lista magics en
  los primeros 128 KB.
- `afs_scan.py <rango>` — descomprime y clasifica (conteo de magics en todo el
  buffer) todos los bins >100 KB de un rango; append a
  `%TEMP%\opencode\afs_classified.txt`.
- `extract_azt_afs.py <afs> --tool <xbdecompress> --entries ... --out ...`
  — exporta entradas autónomas `#AZT` a PNG para comparar retratos/texturas.

## 5. REFERENCIAS

| Tema | Dónde |
|---|---|
| **Mapa definitivo roster HD** | `docs/03_formatos/MAPA_ROSTER_HD.md` |
| Mapa crudo (3990 líneas) | `mod center hd/data_cmn_map.txt` |
| Catálogo Model Swap (183 personajes) | `mod center hd/catalog_b3.cat` |
| Nombres AFL Pal (parseado) | `out/analysis/data_cmn_pal_afl.txt` (fuente: `modding resources/Data_CMN file name list (Budokai 3 Pal)/data_cmn.afl`) |
| Tabla anotada (probe + AFL + catálogo) | `out/analysis/data_cmn_annotated.txt` |
| Listado comunitario PS2 | `modding resources/DBZ_B3_Character_Bin_List.txt` |
| Formato bin | `docs/03_formatos/AWO_FORMAT.md`, `BIN_LAYOUT.md` |
| Swap nativo / port | `AGENTS.md` §3.4, `docs/07_ports/` |
| Hoja de ruta | `docs/HOJA_DE_RUTA_2026_09.md` (Fase 3) |
