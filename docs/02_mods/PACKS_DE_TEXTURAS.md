# Packs de texturas (estilo PCSX2)

> Un **pack de texturas** reemplaza las texturas del juego por versiones propias
> (normalmente escaladas con IA) **sin tocar ficheros ni la memoria del guest**:
> el runtime intercepta cada textura al cargarla y, si el pack tiene una version
> suya, sube esa en lugar de la del juego.
>
> Estado: **Fase 1 (volcado) + Fase 2 (cargador) implementadas y validadas.**
> El cargador soporta DDS (DXT1/3/5 y 32bpp sin comprimir) y PNG, con factor
> x1..x4 y generacion de mips.
>
> **v1.2.8.1 — el volcado cubre tambien los formatos del HUD/UI**: ademas de los
> comprimidos (DXT1/DXT3/DXT5) se vuelcan los SIN comprimir que el juego usa de
> verdad (RGBA8, RGB565, RGB5A1, RGB655, RGBA4, L8, L8A8, RGBA1010102). Antes
> solo se volcaban los DXT y casi todo el HUD desaparecia del volcado.
> **Reemplazables por un pack**: DXT1/3/5 y **RGBA8** (`k_8_8_8_8`); los de
> 8/16 bits se vuelcan como referencia pero su pack todavia **no** se aplica.

---

## 1. Como funciona (resumen)

1. El juego tiene texturas en memoria. El runtime calcula un **hash XXH3-64** del
   bitmap original de cada textura (el mismo dato que guarda el `#AZT`).
2. Un pack es una carpeta en `mods/` con ficheros nombrados con ese hash.
3. Al cargar una textura, si su hash esta en el pack, se usa la imagen del pack
   (decodificada a RGBA8) a la resolucion que tenga el fichero (x1..x4) y se
   generan los mips. Si no, se usa la textura del juego.

No hay override de ficheros ni reempaquetado: es una capa host, igual que la
"mejora de texturas HD" experimental (con la que **no** se combina: el pack tiene
prioridad).

## 2. Formato de un pack

```
mods/
  MiPack/
    1F1ED618559910C0_256x1024_DXT3.dds     <- textura escalada x2
    0129771C81A045B0_128x128_RGBA.dds      <- x2 sin comprimir
    04037CE1AFA993E9_1024x512_DXT3.png     <- tambien vale PNG
    pack.json                              <- opcional (metadatos)
```

- Nombre: **`<hash:16 hex>_<Ancho>x<Alto>_<sufijo>.dds`** (o `.png`).
  - `hash` = el del volcado dev (identifica la textura ORIGINAL).
  - `Ancho`/`Alto` = tamano de **esta** imagen (la del pack).
  - `sufijo` = libre (`DXT3`, `RGBA8`, `PNG`...); informativo.
- El **factor** se deduce: `factor = Ancho_pack / Ancho_original`, y debe ser
  entero, igual en X e Y, y estar entre **1 y 4**.
- Formatos admitidos: **DDS** (DXT1/BC1, DXT3/BC2, DXT5/BC3, o 32bpp sin
  comprimir) y **PNG**. Cualquier otro se ignora con un aviso en el log.
  Como el pack se sube siempre como **RGBA8**, lo normal es exportar el PNG (asi
  el factor y el contenido son libres) o el DDS de 32bpp.
- `pack.json` (opcional):
  ```json
  { "name": "Mi pack", "author": "tu nombre", "version": "1.0",
    "description": "Caras y escenarios a 4x" }
  ```

## 3. Como crear un pack (paso a paso)

### 3.1 Volcar las texturas del juego (modo dev)

1. Launcher → pestaña **Desarrollo** → activa **"Volcado de texturas para mods
   (dev)"** y elige una carpeta (por defecto `D:\Proyectos IA\DBZ B3 DDS`).
2. Reinicia y juega. Se escriben los DDS + `index.jsonl` (hash, tamano, formato,
   sufijo DDS, mips) de cada textura unica. El nombre del fichero lleva el
   sufijo del formato: `DXT1`/`DXT3`/`DXT5` (comprimidos) o `RGBA8`, `RGB565`,
   `RGB5A1`, `RGB655`, `RGBA4`, `L8`, `L8A8`, `RGBA1010102`.

Notas del volcado:

- **Formatos no soportados**: se omiten y queda **un aviso por formato** en el
  log (`dbz3: volcado: formato k_24_8 (fmt=22) no soportado, texturas omitidas`).
  Asi se ve de un vistazo que queda pendiente (hoy: `k_DXN`/normales,
  `k_DXT5A`/alpha, `k_24_8`, 16_16_16_16...).
- **Video/render targets**: una textura cuyo contenido cambia en cada uso (el
  video de la intro) se volca como maximo **4 versiones** y luego se deja de
  volcar (aviso en el log). Sin ese tope el volcado se llenaba de fotogramas
  (medido: 4096 ficheros / 1,4 GB en 5 min; con el tope, 194 / 51 MB).
- **Limite total**: `dbz3_texture_dump_max` (por defecto 4096, `0` = sin limite)
  en el tab Dev.

### 3.2 Convertir y organizar

```powershell
python awo_tools\texture_dump_import.py "D:\Proyectos IA\DBZ B3 DDS" "D:\pack_png" --afs us\data_cmn.afs
```

Convierte los DDS a PNG y los coloca en `<personaje>\texNN_WxH.png`; los que no
casan con ningun `#AZT` van a `_unknown\`.

> **Cuadrados negros**: algunas texturas del juego tienen el canal alpha **todo a
> cero** (el juego dibuja esas texturas ignorando su alpha, pero un visor las
> muestra transparentes, es decir, negras). No es un fallo del volcado: el DDS es
> el dato exacto del juego. Si quieres verlas/utilizarlas, anade
> **`--opaque-alpha`** y el PNG se escribe opaco (y el `manifest.json` marca
> `alpha_all_zero`). En un pack da igual: el juego ya ignora ese alpha.

### 3.3 Escalar

Pasa los PNG por tu escalador favorito (waifu2x, Real-ESRGAN, Topaz...). Deja
**el mismo nombre de fichero**; solo cambia el contenido y el tamano.

### 3.4 Empaquetar

Crea `mods\MiPack\` y guarda ahi las imagenes escaladas **renombradas al
formato del pack**: `<hash>_<nuevoAncho>x<nuevoAlto>_<sufijo>.dds` (o `.png`).
El hash y el factor salen del `index.jsonl` / del nombre original:

```
original:  1F1ED618559910C0_128x512_DXT3.dds
x2:        mods\MiPack\1F1ED618559910C0_256x1024_DXT3.dds
```

### 3.5 Validar

```powershell
python "mod center hd\texture_pack.py" validar "mods\MiPack" --dump "D:\Proyectos IA\DBZ B3 DDS"
```

Comprueba nombres, que las dimensiones del fichero coincidan con el nombre y,
si le pasas el volcado, que el hash exista y que el factor sea valido (1..4).

### 3.6 Activar

Launcher → pestaña **Mods**: la carpeta aparece como mod (se puede activar/
desactivar). El launcher la detecta como pack (contiene `.dds` con el nombre
correcto) y se lo pasa al runtime. Reinicia.

## 4. Reglas y limites

- Solo texturas **2D de una sola rebanada** (no cubemaps/3D/arrays), igual que la
  mejora HD. El **frontbuffer** (la imagen que se presenta) nunca se reemplaza.
- **Formatos reemplazables**: DXT1/DXT3/DXT5 y **RGBA8** (`k_8_8_8_8`). Los
  formatos de 8/16 bits (`k_8`, `k_8_8`, `k_5_6_5`, `k_1_5_5_5`, `k_4_4_4_4`...)
  **si** se vuelcan (referencia/edicion) pero su pack se **ignora**: su recurso
  host no es RGBA8 y su swizzle es propio del formato. Es el siguiente paso
  pendiente (ver `docs/SESION_VOLCADO_FORMATOS_2026-09-23.md`).
- El pack **tiene prioridad** sobre la "mejora de texturas HD" en runtime; se
  recomienda no activar ambas a la vez.
- **Conflictos**: si dos packs definen el mismo hash, gana el primero por orden
  alfabetico de carpeta; el runtime lo avisa en el log.
- Los packs **no se aplican en modo disco (ISO)**: requieren la carpeta extraida
  (igual que el resto de mods).
- El factor maximo es **x4**. Un fichero cuyo tamano no sea multiplo entero del
  original se ignora (aviso en el log).
- VRAM: el reemplazo se sube como **RGBA8** (4 bytes/texel). Un pack a x4 de una
  textura 1024x1024 son ~64 MB con mips; un pack completo a x4 puede pedir varios
  GB. Empieza por x2 y por texturas concretas.

## 5. Diagnostico

En `logs\dbz3_NNN.log`:

```
dbz3: pack de texturas 'MiPack' -> 12 texturas
dbz3: pack 'MiPack' reemplaza 128x512 (fmt 19) -> 256x1024 (x2)
dbz3: pack 'MiPack' subido 256x1024 (10 niveles, 1441280 B)
```

- `packs cvar = ''` → el launcher no detecto ningun pack (revisa la carpeta).
- `no es multiplo de ...` → el tamano del fichero no cuadra con el original.
- `factor invalido` → el factor no es entero o pasa de x4.
- `no se pudo decodificar` → formato no soportado o fichero corrupto.

Y del volcado:

```
dbz3: volcado de texturas activado en '...'
dbz3: volcado: formato k_24_8 (fmt=22) no soportado, texturas omitidas
dbz3: volcado: textura en 0x1D633000 (960x720, fmt=2) cambia de contenido en cada uso
      (video/render target): solo se volcaron 4 versiones
dbz3: volcado de texturas: alcanzado el limite de 4096 texturas
```

## 6. Ficheros implicados

- Runtime (SDK): `rexglue-sdk-0.10/src/graphics/dbz3_texture_pack.{h,cpp}`
  (modulo COMUN a los dos backends: indice, tabla de formatos volcables
  `Dbz3DumpFormatFor`, `Dbz3PackReplaceableFormat`, decodificacion DDS/BC) y
  `d3d12/texture_cache.cpp` + `vulkan/texture_cache.cpp` (hash, factor, subida,
  y el volcado dev en D3D12).
- Launcher: `src/launcher/settings.cpp` (`RefreshTexturePacks`, cvar
  `dbz3_texture_packs`), `src/launcher/launcher_state.cpp` (pestaña Mods).
- Herramientas: `awo_tools/texture_dump_import.py` (DDS→PNG y organizacion),
  `mod center hd/texture_pack.py` (validar/listar).
- Cvar del runtime: `dbz3_texture_packs` (carpetas separadas por `;`).
