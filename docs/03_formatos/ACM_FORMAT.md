# FORMATO #AMB / #CSK / #ACM HD — MOVESET Y CONTENEDOR (RE 2026-09-08)

> RE estática del bin de moveset HD 360 (Krillin bin 333, verificada sobre el
> corpus: los 120 bins `cluster=moveset` de `data_cmn.afs` comparten estructura).
> El #ACM HD es el equivalente big-endian del AMM PS2; el #CSK del BSK PS2
> (renombrado, como #AMO0→#AWO). La RE partió del conocimiento PS2
> (`IW_moveset_editing_notes`, PDFs NIM) + volcado de bins reales HD.

## 1. CONTENEDOR BIG-ENDIAN GENÉRICO (#AMB, #CSK, #ACM)

Todos los contenedores de la HD comparten el MISMO layout de cabecera:

```
+0x00  magic (4 B)                "#AMB" | "#CSK" | "#ACM" | ...
+0x04  0x00000020                 (header/tabla: tamaño fijo de cabecera)
+0x08  0x00000000                 (reservado)
+0x0C  0x00000002                 (n tipos / flag; en #CSK vale 4)
+0x10  u32  n_sub                 NÚMERO DE SUB-ENTRADAS de la tabla
+0x14  [off, size, 0, 0]          descriptor de la propia tabla (off=0x20, size=n_sub*0x10)
+0x20  tabla: n_sub × [u32 off, u32 size, u32 type, u32 pad]
```

- Los offsets de los sub-bloques son **relativos al inicio del contenedor**.
- `type`: en el moveset, `0xFFFFFFFF` = bloque #CSK; `3` = bloque #ACM.
- No hay compresión interna; los bloques van pegados.
- Ejemplo bin 333 (Krillin): `+0x10=4` → 4 sub-bloques:
  `[0x60, 0x2577C, 0xFFFFFFFF]` = #CSK · `[0x257E0, 0x140280, 3]` = #ACM1 ·
  `[0x165A60, 0x42560, 3]` = #ACM2 · `[0x1A7FC0, 0x2D60, 3]` = #ACM3.

## 2. BIN DE MOVESET / ANM (estructura completa)

```
data_cmn.afs entry (p.ej. 333 Krillin, 1.75 MB descomp.) =
  #AMB contenedor {
    #CSK   (153 KB)  = BSK PS2 renombrado  -> propiedades de animación + hit reactions
    #ACM1  (1.25 MB) = pool de animaciones (AMM PS2) -> pose/basic/ataques
    #ACM2  (271 KB)  = segundo pool de animaciones
    #ACM3  (11 KB)   = tercer pool (pequeño: cara/auxiliar)
  }
```

- **120 bins** `cluster=moveset` con firma `{#AMB:1, #CSK:1, #ACM:2..3}`.
  Personajes con 2 #ACM: Buu Gohan/Gotenks/Ghost/Piccolo (pool 137-140),
  Cell Jr. (165), etc.
- El bin **LIPS** (animación de boca, ~5 KB) es `#AMB → #AWO#AWG#ACM` (1 #ACM).
- El bin **CAM** es solo `#AMB` (cámara).
- Los **stages** (44-69) y **efectos** (504-555) son #AMB con sub-bloques
  `#AWO/#AWG/#AZT/#ACM/#SPX/#ACC` (ver corpus).

## 3. BLOQUE #ACM — POOL DE ANIMACIONES (AMM PS2)

```
+0x00  "#ACM" ; +0x04 0x20 ; +0x08 0 ; +0x0C 2 ; +0x10 n_anim ; +0x14 0x20 ; +0x18 ? ; +0x1C ?
+0x20  tabla de animaciones: n_anim × 0x10
       cada entrada: [0x09, variante, ?, offset]  (0x19 si hay scale)
       -> offset relativo al inicio del #ACM; stride tipicamente constante
+0x20 + n_anim*0x10
       bloques de animación: cada uno = tabla por hueso (0x190 B = 50 huesos × 8 B)
       por hueso: [ptr_datos_angulares u32, ptr_datos_posicionales u32]
```

- Datos angulares (PS2 AMM, misma estructura BE): por frame
  `frame_no u16 + roll u16 + pitch u16 + yaw u16` (0x0000=0°, 0xFFFF=360°),
  rotación aplicada yaw→pitch→roll.
- Datos posicionales: por frame `frame_no u32 + 3×f32` (roll/pitch/yaw).
- Krillin #ACM1: `n_anim=99` (0x63), entradas con offsets `0x650, 0x7E0, 0x970,
  0xB00, 0xC90, ...` (stride 0x190 = tabla de 50 huesos).

## 4. BLOQUE #CSK — PROPIEDADES DE ANIMACIÓN + HIT REACTIONS (BSK PS2)

> 🔴 **Formato CORREGIDO 2026-09-08** (tras crash de `krillin_dmg_test`):
> la interpretación previa (entradas 0x10 B y "dato HR [damage<<16|code]") era
> INCORRECTA: la "lista HR" es el bloque de **AP addresses** (types 0-7) y los
> "datos HR" son **bloques AP** (frames). El DAÑO real vive en los bloques HR
> del final (sección +0x1C), indexados por el **HR code** de las AP type 1.

```
+0x00  "#CSK" ; +0x04 0x20 ; +0x08 0 ; +0x0C 4 ; +0x10 n_attack_codes (0x7C7=1991)
+0x14  offset lista direcciones de animacion (0x20) ; +0x18 n_hr_blocks (0xF1=241)
+0x1C  offset seccion HR (p.ej. 0x1DEFC)
+0x20  lista de direcciones (4 B/entry): POSICION = attack code (0x20 + code*4)
+0x20 + n*4  bloques de animacion
```

### 4.1 Cadena CORRECTA de edición de una habilidad (verificada bin 333 Krillin)

1. **Attack code → dirección del bloque de animación**: `list[code]` (u32 en
   `0x20 + code*4`). Attack code = posición en la lista (igual que el BCM).
   Krillin usa códigos 0, 2, 0x38-0x3C, 0xEA-0xFF, 0x200+... (206 con hitbox).
2. **Bloque de animación** (p.ej. attack 0x21b → @0x28CC): uno o varios
   sub-bloques `[anim u16][amm u16] + params + 0xFFFFFFFF×3 (sentinel) +
   [0000][n_ap u32][ap_addrs_off u32]`. AMM 3 = tercer #ACM del bin.
3. **AP addresses block** (@ap_addrs_off, n_ap entradas × 8 B):
   `[AP_type u16][n_lineas u16][data_off u32]`. AP types 0-7 (0=head tracking,
   1=**HIT properties**, 2=airborne, 3=turnaround, 4=speed, 5=limb, 6=hands,
   7=misc). Varios sub-bloques por ataque (multi-hit, ground/air).
4. **AP type 1 (Hit properties)** — línea de 16 B:
   `[frame u16][ID u16][act u8][pad u8][HR_code u16][props u16][body u8][radius u8][pos x i8][pos y i8][pos z i8][pad u8]`
   - Las líneas de cierre de ventana tienen `HR=0xFFFF`.
   - `HR_code` (u16 en bytes 6-7) = índice del bloque HR.
   - `props`: 0x10=normal, 0x01=ya usado, 0x04=armor, 0x20=cargado, 0x40=inesquivable...
   - `body`: 0=WAIST, 1=STMC, 2=NECK, 3=HEAD, 16=LARM1, 17=RARM1, 18=CHEST...
5. **Bloque HR** (8 líneas × 16 B, en `hr_off + HR_code*128`), una línea por
   situación de hit:
   `[damage u16][grunt u8][visual u8][stun_type u16][stun_code u16][pushback f32][specific f32]`
   - Líneas: 1=normal, 2=counter, 3=juggle, 4=back, 5=grounded, 6=blocking,
     7=??, 8=stunned.
   - `stun_type`: 00=normal, 01=juggle, 02=knockaway, 03=scripted (SPX), 04/05=block.
   - `specific`: altura de juggle (type 01) / duración blockstun (04/05) /
     dirección knockaway (02: 2×s16).

Ejemplo real (Krillin attack 0x21b, >P): HR blocks 28/29/30/31 con daño
59/78/68/98 (cada golpe del combo), type 01 (juggle), juggle height 1.4/1.2/1.0.
Blocking (linea 6) siempre dmg=0. HR 0x5C = knockaway: dmg 120, pushback 70.

> ⚠️ **NO usar la "cadena de 5 niveles" previa** (era la cadena AP, que termina
> en FRAMES, no en daño). Editar frames → crash en combate (llamada a NULL en
> `sub_820800A8`).

### 4.2 Herramientas

- `awo_tools/csk_chain.py` — analiza la cadena CORRECTA: `--scan` lista los
  attack codes con hitbox, `--code <hex>` muestra la cadena completa (AP y HR
  codes), `--hr <hex>` muestra el bloque HR (8 líneas con daño/stun/pushback).
- `awo_tools/csk_edit.py` — edita el DAÑO: `--attack <hex> --damage N [--line]`
  o `--hr <hex> --damage N`; `--install` empaqueta el override (LZX /N:2048).
  Verificado: attack 0x21b → daño 100 en bloques HR 28/29/... con estructura
  intacta (la cadena sigue parseando tras el patch).

## 5. 🔴 CORRESPONDENCIA PS2 → HD (IDENTIDAD CONFIRMADA 2026-09-08)

**El bin de moveset PS2 (GH, LE) y el HD (BE) son el MISMO archivo.**
Verificado con Krillin e333: tamaños idénticos (BSK 153468 = #CSK 153468;
AMM1 1311344 ≈ #ACM1 1311360; AMM2 271712 = #ACM2 271712) y offsets
internos iguales. La numeración PS2 GH = numeración HD (3990 entradas en
ambos `data_cmn.afs`). El diff LE/BE ES el parser (A4):

| PS2 (LE) | HD 360 (BE) | Contenido |
|---|---|---|
| AMB | #AMB | contenedor genérico (misma cabecera) |
| BSK | #CSK | animation properties + hit reactions (daño/stun/pushback) |
| AMM | #ACM | animaciones crudas (roll/pitch/yaw por hueso) |
| BCM/SPX/AMC | (a RE) | movelist/scripts/cámara — pendiente de localizar (¿en #CSK?) |

> El #ACM1 HD difiere del AMM1 PS2 en 16 B (1311360 vs 1311344): un campo de
> cabecera distinto (versión o pad). Verificar antes de un port byte-exacto.
> Extraer el BSK/AMM PS2 de cualquier personaje GH da la referencia LE para
> decodificar el #CSK/#ACM HD del mismo personaje (mismos indices).

## 6. LO QUE ESTO HABILITA

1. **Swap de moveset por blob** (ya en `swap_matrix.py --type moveset`): mover el
   bin #AMB completo entre slots — funciona sin crash, PERO los combos/ataques
   quedan mapeados a los attack codes del destino (si el moveset y el BCM no
   comparten la numeración, los ataques no corresponden: validado 2026-09-08,
   Krillin→Tenshinhan sin crash pero con combos "corruptos"). Para un swap
   correcto haría falta mapear los attack codes (BCM) o editar el #CSK del
   destino.
2. **Editar una habilidad** (S3, CORREGIDO 2026-09-08): `awo_tools/csk_edit.py`
   parchea el **daño** de un attack code siguiendo la cadena correcta
   (attack → AP type 1 → HR code → bloque HR → `damage u16` de las líneas 1-8).
   Verificado: attack 0x21b (combo >P) → bloques HR 28/29/... con daño 100 y
   estructura intacta (la cadena sigue parseando).
   ```powershell
   python awo_tools/csk_edit.py --entry 333 --attack 21b --damage 100 --mod krillin_dmg_test --install
   python awo_tools/csk_edit.py --entry 333 --hr 5c --damage 80   # bloque HR directo
   ```
   Previsualizar antes: `python awo_tools/csk_chain.py --entry 333 --scan`
   (lista los attack codes con hitbox) y `--code <hex>` (cadena) / `--hr <hex>`.
   ⚠️ La versión previa de csk_edit (parcheaba FRAMES de AP) crasheó combate
   (NULL en sub_820800A8) — ya corregida, no reutilizar el bin viejo.
   ⚠️ Si el bin recompreso CREA la entrada (mayor que `to_read`), el runtime
   reconstruye físicamente el AFS (fix 2026-09-09, `AfsRebuildPath`); sin ese
   fix el crash aparecía en el select (lectura con offset viejo → magic basura
   #ACP sin handler).
3. **Añadir animaciones**: crear bloques #ACM nuevos en el contenedor (n_sub y
   tabla), siempre que el bin quepa en `to_read` o use mid-insert virtual.
4. **Corpus**: los bins con #CSK ahora se clasifican como moveset real
   (120 en data_cmn); #CSK es la firma que distingue moveset de escenario.

## 7. HERRAMIENTAS

- `awo_tools/acm_parse.py` — extrae el bin, descomprime LZX, lista el
  contenedor #AMB y vuelca sub-bloques (#CSK/#ACM).
- `awo_tools/acm_analyze.py` — analiza un bloque #ACM (tabla de animaciones,
  bloques de huesos).
- `awo_tools/csk_chain.py` — RE del #CSK (formato CORRECTO): `--scan` attack
  codes con hitbox, `--code <hex>` cadena completa (AP + HR codes), `--hr <hex>`
  bloque HR con daño/stun/pushback por situación.
- `awo_tools/csk_edit.py` — EDITA el daño (attack → HR block → damage u16) y
  empaqueta el override (`--install`). Ver §6.
- `awo_tools/csk_analyze.py` — ⚠️ DESACTUALIZADO (cadena AP/Frames, NO daño).
  Usar `csk_chain.py`.
- `awo_tools/corpus_scan.py` — clasifica bins por magics (#CSK/#ACM/#SPX/...).

## 8. PENDIENTE

- ✅ Cadena de edición CORRECTA decodificada (attack → AP1 → HR code → bloque
  HR → daño) + herramienta `csk_edit.py` (§4.1/§4.2).
- ✅ Identidad PS2 BSK/AMM = HD #CSK/#ACM confirmada por dif de tamaños (§5).
- Semántica fina de stun_code (animaciones de stun/juggle) y del `specific` de
  cada tipo (knockaway = dirección 2×s16): cruzar con BSK_breakdown.pdf.
- Localizar el **BCM** (movelist) para mapear combos → attack codes (necesario
  para moveset swaps correctos entre personajes).
- Verificar los 2 bloques #ACM adicionales y el #CSK de los pools (137-140).
- Documentar el #SPX (scripts) y #ACC (audio?) cuando se confirme su rol en stages.