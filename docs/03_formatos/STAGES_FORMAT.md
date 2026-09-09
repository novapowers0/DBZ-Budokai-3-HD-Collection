# FORMATO DE STAGES HD + SCRIPT #SPX + CONTENEDOR PS2 (RE 2026-09-08)

> RE estática de los bins de stage de `data_cmn.afs` (44-69 us / 3735-3847 HD)
> y de los ports de moveset IW→B3 (PS2) para fijar la correspondencia PS2↔HD.
> Verificado sobre bin 44 (stage us, 1.9 MB descomp.) y los bins del port
> Goku GT (`modding resources/Infinite World to Budokai 3 Moveset Ports/`).

## 1. BIN DE STAGE = CONTENEDOR #AMB DE 5 NIVELES

Bin 44 (stage us, 1.94 MB descomp.):

```
#AMB contenedor BE (header estandar, ver ACM_FORMAT.md §1) n_sub=5:
  sub 0  @0x80     1.48 MB  type=0xFFFFFFFF  #ZDD   <- geometria/efectos del stage
  sub 1  @0x16ACC0 240 B    type=0xFFFFFFFF  #CAD   <- tabla de camara (n=4)
  sub 2  @0x16ADC0 256 B    type=0xFFFFFFFF  #CAS   <- tabla de camara (n=5)
  sub 3  @0x16AEC0 9752 B   type=0x8         #SPX   <- SCRIPT del stage (LE)
  sub 4  @0x16D4E0 443 KB   type=0xFFFFFFFF  #AMB   <- contenedor ANIDADO
```

El **#AMB anidado** (sub 4) es un contenedor ESPARSO: n_sub=641, casi todo
entradas vacías (off=0 size=0), con bloques reales agrupados por "elemento":

```
  #ACE (Collision/Animated Collision Element)  ~25 bloques, 352-4912 B
  #ACM (animaciones de escenario)               2-4 bloques, 2944 B
  #AWO (modelos) + #AZT (texturas)              por elemento (DXT3/BC2)
  #AZT  final de 197 KB (sub 640, textura grande)
```

> Interpretación: el array de 641 entradas está indexado por elemento del
> stage (p.ej. "zonas"): cada elemento tiene colisión #ACE + modelo #AWO +
> textura #AZT + animación #ACM. Los huecos vacíos = elementos sin datos.

El **#ZDD** (sub 0, 1.48 MB) es otro contenedor: cabecera propia
(`#ZDD 01 01 00 00 00 00 00 00 FFFFFFFF 40 16AADC 16AC0C ...`) que envuelve
#AZT (a 0x40) + #AWO/#AWG (a ~0xC1000). Geometría grande del stage.

## 2. BLOQUE #SPX — SCRIPT (LE, NO BE)

```
+0x00  "#SPX ver 0.01\0"  (string de version ASCII, 16 B con padding)
+0x10  u32 LE  0x20   = header size
+0x14  u32 LE  0x44   = offset del bytecode (0x44 = 68)  ✅
+0x18  u32 LE  0x09   = count (9 en stage 44; 0x33=51 en el port IW)
+0x1C  u32 LE  0x00
+0x20  ...            = 2ª copia del header (0x20 0x44 0x09 0x00)
+0x30  ...            = 16 B 0xFF (sentinel)
+0x38  00 00 00 00    = pad
+0x44  ...            = STREAM de bytecode (hasta el final ~0x25D0)
+~0x25D0              = tabla final (6×u16 LE: 2 2 2 1 3 3) + strings debug:
+                       "INPUT REST = " / "INPUT WAIT = " / "SCRIPT FRAME = "
```

### 2.1 ENCODING DEL BYTECODE (parcial, 2026-09-08)

Stream de opcodes 1 byte + inmediatos tipados. Encoding verificado:

| Opcode | Formato | Significado |
|---|---|---|
| 0x08 0x10 `u8` | 3 B | load const u8 (0x00/0x01/0xff/0x15/0x3c/0xfe...) |
| 0x08 0x20 `u16` | 4 B | load const u16 (0x00c8=200...) |
| 0x09 0x30 `u32` | 6 B | load const float LE (0x3f800000=1.0, 0x00000000=0.0) |
| 0x01 0x20 `u16` | 4 B | op con inmediato u16 (0x00c6=198, 0x00a7=167, 0x0106=262...) |
| 0x01 0x30 `u32` | 6 B | op con inmediato u32 (0x2b9=697, 0xe4=228...) |
| 0x02 `u16` | 3 B | op con inmediato u16 (0x0273/0x0280/0x0270/0x22d6...) |
| 0x12 0x10 `u8` | 3 B | sub-rutina / op con u8 (0x0c, 0x04, 0x1c) |
| 0x13 0x10 `u8` | 3 B | idem (0x04, 0x00) |
| 0x0b | 1 B | marcador de bucle/retorno (aparece tras 0x12/0x13) |
| 0x0a / 0x00 / 0x4b / 0x51... | 1 B | opcodes cortos (frecuentes 0x0a=446, 0x0b=382) |

- El stream es una VM de comandos de escenario (cámara/eventos): los strings
  debug ("INPUT REST/WAIT", "SCRIPT FRAME") revelan un bucle por frame con
  polling de input.
- Estructura típica por sub-bloque: `[13 10 xx] [0b] [08 10 xx]... [01/09 30...]
  [02 u16] [12 10 xx] [0b]` → set-up + carga de consts + llamadas.
- Los floats 1.0/0.0 repetidos en stage 44 (luego `01 20 a7 00` = cámara a
  posición 167?) sugieren parámetros de cámara por-frame.
- Verificación del encoding: el stream de stage 44 parsea limpio ~48-49
  instrucciones antes del primer caso ambiguo (0x08 0x4b / opcodes 0x4b 0x51
  0x52 0x33 0x5e 0x50 = 1 byte). No se ha alcanzado disassembly completo.

- **El #SPX es LITTLE-ENDIAN** (a diferencia del contenedor BE) — es el script
  PS2 portado sin conversión. Histograma de bytes: 00 (24%), 01, 08, 10, 02,
  0A, 0B → opcodes compactos de VM.
- El mismo magic "#SPX ver 0.01" aparece en los **ports de moveset IW (PS2)**:
  la cadena de version no cambia entre juegos.
- `type=0x8` en el contenedor #AMB identifica el bloque #SPX (tanto en stage
  HD como en el port PS2: sub type 8 = #SPX).
- En los movesets HD (bins #CSK+#ACM) NO hay bloque #SPX: el script HD de
  movimientos vive en el #CSK (ver ACM_FORMAT.md §4) o el gamepad lo fusiona.

> PENDIENTE: disassembly completo de opcodes (falta el intérprete: no hay
> referencias a "#SPX" en `generated/` — el guest despacha por type=8). Valor
> bajo para modding (scripts de cámara/eventos de escenario); la estructura y
> el encoding ya están documentados.

## 3. CONTENEDOR PS2 (LE) vs HD (BE) — PORTS IW→B3

Los ports IW→B3 (`modding resources/.../Goku GT/IW/unnamed_359.bin` etc.) son
los contenedores PS2 originales:

```
PS2 #AMB (LITTLE-ENDIAN):
+0x00 "#AMB" ; +0x04 0x20 (hdr size) ; +0x08 0 ; +0x0C n_files ;
+0x10 n_lista ; +0x14 0x20 (tabla) ; +0x18 first_sub_offset
+0x20  tabla [off u32, size u32, type u32, pad u32] x n
```

**Correspondencia exacta con el HD** (misma estructura, LE↔BE, tabla en +0x20).
Ejemplo Goku GT IW (359): #AMC (0x50, type=5) + #BCMg (0x12E30, type=FFFFFFFF)
+ #SPX (0x149A0, type=8).
Ejemplo Goku GT B3 (241): #AMC (type=5) + #AML stub (type=6) + #BCMg
(type=FFFFFFFF) + #SPX (type=8).

**Tipos de bloque del contenedor (constantes):**

| type | Bloque | Función | HD |
|------|--------|---------|----|
| 0xFFFFFFFF | #BCMg / #CSK | movelist + propiedades + hit reactions | #CSK |
| 3 | #AMM | animaciones crudas | #ACM |
| 5 | #AMC | cámara | bin CAM pequeño |
| 6 | #AML | stub (vacio, 5 B) | — |
| 8 | #SPX | script de combate/escenario (LE) | #SPX (stage) |

> Para el port de moveset IW→HD: el BCMg+BSK+AMM PS2 → #CSK+#ACM HD
> (conversión LE→BE + rename de magics + tabla), y el #AMC → bin CAM.

## 4. NOMBRES DE STAGES CONFIRMADOS (AFL Pal, alineado 1:1 con US 44-69)

Los bins de stage de `data_cmn.afs` (US, mismo índice que Pal en este rango)
son 13 mapas en los pares y 13 "SE_" (sounds/effects del mapa) en los impares:

| Entrada | Nombre AFL | | Entrada | Nombre AFL |
|---:|---|---|---:|---|
| 44 | RED_RIBBON_BASE_MAP.amb | | 58 | TIME_BOLIC_CHAMBER_MAP.amb |
| 45 | SE_RED_RIBBON_BASE_MAP | | 59 | SE_TIME_BOLIC_CHAMBER |
| 46 | WORLD_TOUR_MAP.amb | | 60 | NAMEK_MAP.amb |
| 47 | SE_WORLD_TOUR_MAP | | 61 | SE_NAMEK_MAP |
| 48 | BUU_BODY_MAP.amb | | 62 | PLAINS_MAP.amb |
| 49 | SE_BUU_BODY_MAP | | 63 | SE_PLAINS_MAP |
| 50 | CELL_RING_MAP.amb | | 64 | DESERT_MAP.amb |
| 51 | SE_CELL_RING_MAP | | 65 | SE_DESERT_MAP |
| 52 | GRANDPA_HOUSE_MAP.amb | | 66 | CITY_MAP.amb |
| 53 | SE_GRANDPA_HOUSE_MAP | | 67 | SE_CITY_MAP |
| 54 | KAI_WORLD_MAP.amb | | 68 | DAMAGED_CITY_MAP.amb |
| 55 | SE_KAI_WORLD_MAP | | 69 | SE_DAMAGED_CITY_MAP |
| 56 | ISLAND_MAP.amb | | | |
| 57 | SE_ISLAND_MAP | | | |

> Los vídeos del select de stage (`*_SELEC.sfd`) están en 3930-3937 Pal →
> en US con el desfase +6 (≈3936-3943). NO confundir con los retratos del
> select (US 3884-3960, tabla del guest `0x82372818`).

## 5. MAGICS NUEVOS REGISTRADOS EN EL CORPUS

`#ZDD` (contenedor de escenario), `#CAD` (tabla cámara n=4), `#CAS` (tabla
cámara n=5), `#ACE` (elemento de colisión/animado), `#AML` (stub), `#MTC`
(bloque de transform/matriz, 2160 B, en bins 3796/3808/3812/3816/3831).
Estos se suman a #CSK/#BFC/#BCM/#BSK/#AMM/#AMC/#AST/#ASE/#AME/#AWA/#AWB/#AWBK.
Añadir al escáner `awo_tools/corpus_scan.py`.

## 6. REFERENCIAS

- `docs/03_formatos/ACM_FORMAT.md` — contenedor BE + #CSK/#ACM (moveset HD).
- `modding resources/Infinite World to Budokai 3 Moveset Ports/Goku GT/{IW,B3}/`
  — bins PS2 de referencia del port.
- Volcados: `out/analysis/acm/entry_44.bin` (stage) + `stage44/sub0-4.bin`.