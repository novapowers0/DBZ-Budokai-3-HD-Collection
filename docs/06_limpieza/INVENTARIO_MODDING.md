# Inventario de modding resources (4 carpetas)

> Actualizado: 2026-08-14. Qué hay en cada carpeta para no duplicar trabajo.
> Rutas relativas a `<proyecto>/`.

---

## 1. `modding resources/` — BASE (31.629 archivos, 2.4GB)

La carpeta principal de recursos.

| Contenido | Uso |
|---|---|
| `All Character Models from IW into AMB format/` (241) | **Modelos IW→B3 PS2** (Janemba, Pikkon, Pan, Super 17...) |
| `Budokai Models/` (569) | Modelos Budokai PS2 |
| `Super Dragon Ball Heroes World Mission/` (30.402) | Modelos SDBH WM (EMD/ESK, mina de Xenoverse) |
| `Infinite World to Budokai 3 Moveset Ports/` (70) | Ports de moveset IW→B3 |
| `EmdFbx-and-FbxEmd-LibXenoverse/` (20) | Convertidor EMD↔FBX (funciona) |
| `Tail AMO/` (4) | Modelos de cola custom |
| `map swap in b1/` (97) | Mapas del B1 |
| `Budokai 3 Story Art/` (190) | Arte del juego |
| Listas de bins (DBZ_B3_Character_Bin_List.txt, etc.) | Mapeo bin→personaje |

## 2. `modding resources update/` — BUZÓN (432 archivos, 292MB)

Buzón para archivos nuevos del usuario. Contiene DUPLICADOS de la base
(All_Character_Slots, Budokai_3_Capsules_IDs, etc.) + `Budokai 1 Models
Converted to AMB/` (230, únicos).

## 3. `modding resources update 2/` — TUTORIALES (5.198 archivos, 694MB)

La carpeta más valiosa para model swap (tutoriales de la comunidad):

| Contenido | Uso |
|---|---|
| `lean bone tutorial/` (4.681) | Herramientas + docs del rig (Budokai Toolset, amo_lgbt.exe) |
| `Tutorial #1 Añadir AMG manualmente/` | **Cómo añadir un AMG por hex** (JaromSc) |
| `SLXS Edit Tutorial/` (varios) | Añadir personajes/transformaciones (SLXS) |
| `MOD EJEMPLO/` (115) | **Ejemplos de conversión IW→B3** (Ginyu Force completa) |
| `- Budokai OBJ Editing Tutorial` / `- Tutorial de Edición de OBJ` | Retopología con OBJ |
| `DBZ B3 (X360) - Lesson 1/` | **Compresión X360 LZX 512KB (/N:2048)** |
| `DBZ B3HD - Lesson 2 (Texture Edition)/` | Edición de texturas HD (AZT) |
| `Tutorial remove parts of the model whit LGBT` | Método LGBT (bodyswap) |
| `SB2 Breakdowns/` | Desglose del formato SB2 |
| `goku_all_animations_b3_and_iw.txt` | Lista de animaciones |
| `INFORME_modding_resources_update_2.md` | Informe previo de esta carpeta |

## 4. `modding resources discord/` — DESCARGAS DEL DISCORD (384 archivos, 924MB)

Recursos descargados de la comunidad:

| Carpeta | Contenido |
|---|---|
| `research/` (245) | **Template B3_AMB_PS3.bt** (formato AWO/AWG), formato intermedio aerithdevs, docs del rig |
| `tools/` (58) | **Budokai Modding Tool V1.5** (amo_lgbt, amg_c, axis_e), Zero Devs tutorials |
| `tutorials/` (78) | **LGBT Method**, How_to_combine_model_parts, CREATE_AMG_WITH_2_MODEL |
| `rb2_reference/` (3) | Referencia del RB2 (Raging Blast 2, mismo SDK Spike) |
| `models/` (0) | Vacía |

---

## DUPLICADOS DETECTADOS

- `modding resources/` y `modding resources update/` comparten: listas de bins,
  story art, capsules IDs, Voice list.
- `modding resources discord/` y `modding resources update 2/` comparten:
  tutoriales del Zero Devs, LGBT.

> No se ha borrado nada de modding resources (riesgo de romper rutas de scripts).
> La limpieza futura puede consolidar los txt de listas de bins en `docs/` como
> referencia, y deduplicar los tutoriales.
