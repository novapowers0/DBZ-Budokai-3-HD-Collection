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

- La numeración coincide con la **data_cmn de la Greatest Hits PS2** (el listado
  comunitario `modding resources/DBZ_B3_Character_Bin_List.txt` mapea modelos
  70-441; la HD 360 usa el mismo orden, p.ej. Krillin = 327).
- **Un slot de personaje = 1 bin #AMB** con `#AWO1 #AWG15-26 #AZT1`. El swap
  nativo (AGENTS §3.4) ya aprovecha esto: bin completo #AMB en slot ajeno.
- Los bins de **animaciones/movesets** de cada personaje están en rangos
  intercalados (p.ej. Krillin 324-327, Vegeta 420-427 según el listado PS2) —
  binarios `#ACM`/`#AWM`. ⚠️ La firma #ACM no basta para distinguir moveset de
  animación de escenario: ambos usan #ACM. **Para el roadmap: localizar el
  moveset EXACTO de un personaje requiere RE del SLXS/roster HD (qué entradas
  lee el guest para un personaje dado).**

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

## 4. HERRAMIENTAS DE LA AUDITORÍA (awo_tools/)

- `afs_list.py <afs>` — tabla completa (entrada, addr, tamaño, magic).
- `afs_probe.py <e1 e2 ...>` — extrae y descomprime entradas; lista magics en
  los primeros 128 KB.
- `afs_scan.py <rango>` — descomprime y clasifica (conteo de magics en todo el
  buffer) todos los bins >100 KB de un rango; append a
  `%TEMP%\opencode\afs_classified.txt`.

## 5. REFERENCIAS

| Tema | Dónde |
|---|---|
| Mapa crudo (3990 líneas) | `mod center hd/data_cmn_map.txt` |
| Listado comunitario PS2 | `modding resources/DBZ_B3_Character_Bin_List.txt` |
| Catálogo Model Swap (183 personajes) | `mod center hd/catalog_b3.cat` |
| Formato bin | `docs/03_formatos/AWO_FORMAT.md`, `BIN_LAYOUT.md` |
| Swap nativo / port | `AGENTS.md` §3.4, `docs/07_ports/` |
| Hoja de ruta | `docs/HOJA_DE_RUTA_2026_09.md` (Fase 3) |