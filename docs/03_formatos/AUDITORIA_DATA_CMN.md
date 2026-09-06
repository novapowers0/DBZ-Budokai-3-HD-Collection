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

## 1. STAGES — localización preliminar

Hay DOS zonas de bins con geometría de entorno:

- **Zona A (44-69)**: contenedores `#AMB` grandes con múltiples modelos (#AWO
  5-16), múltiples texturas y bloques #ACM (animaciones de entorno). Los pares
  `53/57/59/61/63/65/69` (~270 KB) son bins #AMB pequeños intercalados —
  piezas o shells de bajo detalle. **12-13 stages**.
- **Zona B (3735, 3786, 3788, 3821, 3823, 3845, 3847)**: **#AWO raw gigante**
  (un solo mesh de 2.8-6.2 MB descomprimido), SIN wrapper #AMB y SIN textura
  interna (los `#AZT` sueltos 3877/3884-3899 podrían ser sus texturas). **7
  stages**.

Total candidatos: **~19-20 bins de stage** — coincide con los ~20 stages del
B3. **Pendiente de validar**: abrir cada bin con `awg0_export.py`/OBJ y
confirmar que la geometría es un entorno (no un personaje). Cruce con la
pantalla de select de stages (vive en `data_eng/ger/spn/fra/ita/usi.afs`).

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
- **HD 360**: el equivalente del SLXS aún NO está localizado. NO está en
  data_cmn (3983-3989 son audio DRM). Candidato: **`data_eng.afs`** (2709
  entradas; los bins grandes 1976-2062 son ~2-6 MB = retratos/select).
  Pendiente: auditar `data_eng.afs` y ver qué entradas lee el guest al abrir
  el select de personajes (instrumentar `generated/`).

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