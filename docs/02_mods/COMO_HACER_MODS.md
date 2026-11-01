# Cómo hacer mods en DBZ Budokai 3 HD Collection

> Actualizado: 2026-10-06 (§5 portar un personaje de Shin Budokai, §6 cámaras con el Studio).
> Pipeline CORRECTO validado (override por entrada + mid-insert virtual).

---

## 0. ATAJO PARA JUGADORES: LOS ASISTENTES DEL MOD KIT (fase 2)

Si no quieres tocar bins ni consola, la portada del Mod Kit tiene cuatro tarjetas «guiadas». Cada una pregunta
paso a paso, enseña un resumen con «Cambiar» en cada línea (el ajuste/preset y el personaje base se ven ahí) y
tiene un único botón **Montar y probar**:

- **Añadir un personaje**: juego de origen → personaje → nombre, ajuste y personaje base → importa y monta.
- **Cambiar aspecto y aura**: personaje del juego → color de aura y de ki → (opcional) tus PNG; puedes sacar los PNG
  desde el propio asistente. Crea un mod de colores y otro de texturas.
- **Dar un traje a alguien**: personaje del juego → modelo (de un personaje ya importado o tu `.amb`/`.bin`) → nombre.
- **Instalar un mod (.zip)**: elige el zip, marca qué mods quieres y se instala. Si ya tenías uno con ese nombre, el
  viejo se guarda en `modkit_backups`. Los archivos de programa del zip (`.dll`, `.py`, `.exe`...) NO se instalan salvo
  que marques que confías en el autor. Rechaza zips con rutas peligrosas.

Todo se puede **Deshacer** (botón del asistente): los mods nuevos se apartan a `modkit_backups` y el montaje
anterior (`_roster`) vuelve a su sitio; nunca se borra nada. Se instalan en `mods/`; el juego los usa al pulsar JUGAR.
Prueba sin ventanas: `python modkit_gui.py --selftest-asistentes <copia de us> <carpeta nueva>`.

**Montar y probar de verdad (fase 3).** Al terminar el montaje, el asistente arranca el juego y te dice en una frase si
el mod ha cargado («Funciona…» o la causa del fallo, sin jerga). No pulsa nada en el juego: se queda en el menú
inicial, así que **tus partidas guardadas no se tocan**. Si el juego ya está abierto te pide cerrarlo. En el equipo de
desarrollo la prueba va a la copia fija `D:\DBZ3HD\smoke_c` mediante `tools/pruebas.py` (lo que añade vuelve a
`D:\_PARA_BORRAR_CLAUDE\kit`); en el de un jugador arranca su propio `dbz3.exe` sin variables de prueba.

**Modo Experto.** La antigua pestaña «Herramientas» (todas las herramientas con sus opciones; Texturas, Crear y
Hex duplican lo que hacen los asistentes) ahora se llama **Experto**, en el modo Avanzado. Ninguna tarea común la
necesita. Más detalle en [TOOLS.md](../04_herramientas/TOOLS.md), [packs de texturas](PACKS_DE_TEXTURAS.md) y
[cambio de modelo](MODEL_SWAP.md).

## 1. LOS DOS TIPOS DE MOD

### 1.1 Override de archivo completo (reemplaza un AFS entero)
```
mods/<mod>/us/data_cmn.afs        ← archivo AFS completo (293MB)
mods/<mod>/eu/data_cmn.afs
```
- Usado por `og_music`.
- El hook `AfsFindModFileOverride` lo sirve.
- **Desventaja**: hay que reconstruir el AFS entero (build_afs.py). Ya NO es
  necesario para swaps de modelo/textura.

### 1.2 Override por entrada (reemplaza UN bin dentro del AFS) — RECOMENDADO
```
mods/<mod>/us/data_cmn.afs/327/geom.bin     ← formato CARPETA
mods/<mod>/us/data_cmn.afs/327              ← formato ARCHIVO DIRECTO
```
- El hook `AfsFindModOverride` lo sirve.
- **No hay que reconstruir el AFS** — solo el bin de una entrada (~100KB).
- **Cualquier tamaño de bin**: si excede el slot, el runtime aplica el
  **mid-insert virtual** (ver §2.4). 2+ mods simultáneos (entradas distintas).

---

## 2. PASOS PARA HACER UN MOD DE MODELO

### Paso 0: conocer la entrada correcta
La entrada = índice de la tabla del AFS (offset 8 del archivo, `entry_count` en +4).
- Krillin visible = **entrada 327** (NO 326). Verificado por instrumentación.
- El runtime loguea las lecturas: `AFS327 READ` en `logs/dbz3_*.log`.

### Paso 1: extraer el bin original
```powershell
# 1. Localizar la entrada en el AFS (tabla en offset 8)
# 2. Extraer los bytes comprimidos LZX
# 3. Descomprimir con xbdecompress
xbdecompress.exe <entrada.lzx> <entrada.bin>
```

### Paso 2: modificar el bin
- Para verificar la estructura B3 HD usa `awo_tools/awg_to_obj_b3.py`,
  `awo_tools/awg0_export.py` o `awo_tools/awg_cara_export.py`.
- Para swaps nativos usa `swap_b3.py` o la pestaña Model Swap del launcher.

### Paso 3: comprimir con /N:2048 (IMPORTANTE)
```powershell
xbcompress.exe /N:2048 <bin_plano> <bin_comprimido.lzx>
```
> ⚠️ NO usar `/N:32` ni `/N:64` — producen bins más grandes que el slot → crash.

### Paso 4: paddear al to_read
El guest aloca `to_read = ceil(size_tabla/0x1000)*0x1000`. El bin comprimido se
rellena con ceros hasta ese tamaño:
- **Si cabe** en el slot (bin ≤ to_read): pad al to_read del slot.
- **Si es mayor** (p.ej. Goten 107006 > Krillin 106496): pad al
  `to_read_virtual = ceil(bin/0x1000)*0x1000` (110592). El runtime lo autoriza
  con el mid-insert virtual: la entrada crece in-place en la tabla virtual y las
  posteriores se desplazan +delta. El guest aloca el buffer correcto y recibe el
  bin completo sin truncar.

`swap_b3.py` hace este padding automáticamente.

### Paso 5: instalar
```powershell
# Crear la estructura de carpeta del mod
New-Item -ItemType Directory -Path "mods/<mod>/us/data_cmn.afs/327" -Force
Copy-Item padded.bin "mods/<mod>/us/data_cmn.afs/327/geom.bin"

# Activar (eliminar .disabled si existe)
Remove-Item "mods/<mod>/.disabled"
```

### Paso 6: verificar en logs
```
logs/dbz3_001.log:
  AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
  AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```
> `got=to_read` = el guest recibió el bin completo. Si `got < to_read` → falta padding.

---

## 2.4 🔴 MID-INSERT VIRTUAL (2026-08-18)

El guest lee cada entrada del `data_cmn.afs` con un buffer de
`to_read = ceil(size/0x1000)*0x1000` derivado de la tabla AFS. Un bin de mod
mayor que ese to_read se truncaba al servirse por override → crash.

La solución del runtime (parche en `patches/`, archivos `afs.cpp`/`afs.h`/
`host_path_file.cpp`) presenta al guest una **tabla AFS virtual CONSISTENTE**:

- `AfsGetVirtualTable()`: si un override excede el `to_read` del slot, la
  entrada **crece in-place** (slot alineado a 0x800) y **todas las entradas
  posteriores se desplazan** por el delta acumulado — replicando exactamente un
  rebuild con mid-insert. Los addr virtuales son coherentes → el guest las
  encuentra correctamente (a diferencia del intento "naive" que inflaba sizes
  manteniendo addr: el guest recalcula offsets acumulando sizes → crash).
- `AfsTranslateOffset()`: para las lecturas de datos, traduce virtual → físico
  (resta el delta de la entrada) y sirve el override (bin completo) o lee del
  archivo físico en el offset traducido.

**Criterio de crecimiento**: solo crece si el override > `to_read` (lo que el
guest ya aloca), NO si excede el slot físico. Un mod que cabe (p.ej. texturas
de Gero, 114688 = to_read) no desplaza nada.

**Resultado**: swaps nativos B3→B3 que pesan ~100KB, en **cualquier dirección**
(el bin puede ser mayor o menor que el slot), y 2+ mods de modelo/textura
activos simultáneamente.

---

## 3. TAMAÑOS DE SLOT DE ENTRADAS CLAVE

| Entrada | Personaje | Slot comprimido | to_read |
|---|---|---|---|
| 327 | Krillin (visible) | 105296 | 106496 |
| 328 | Krillin Buu Saga | 104404 | 106496 |
| 329 | Krillin Namek | 101268 | 102400 |
| 298 | Goten | 107006 | 110592 |
| 270 | Goku | 128062 | 130048 |

> Con el mid-insert virtual el bin YA NO tiene que caber en el slot de la
> entrada destino: si lo excede, la tabla virtual hace crecer la entrada y
> desplaza las posteriores automáticamente.

---

## 4. CÓMO ACTIVAR/DESACTIVAR MODS

- **Activo**: carpeta `mods/<mod>/` SIN archivo `.disabled`.
- **Desactivado**: con `.disabled`.
- **Orden**: los mods se ordenan alfabéticamente; el primer match gana.
- La activación real usa únicamente el marker `.disabled`; `dbz3_enabled_mods`
  quedó obsoleto y no controla los mods actuales. El override por entrada es
  independiente del perfil visualizado por el launcher.

---

## 4b. MAS PERSONAJES Y FORMAS CON MOVESET PROPIO (31/10)

- **Mas de 24 personajes nuevos:** no hay que hacer nada. Los 14 primeros quedan fijos y el resto va por paginas: L1/R1 en la rueda (tecla 1/3 con teclado). Lo elegido sigue valiendo al cambiar de pagina. Online solo se ve la pagina 1.
- **Una forma con el moveset de otro port:** en `personaje.toml`, `formas_port = ["", "imp_iw_gogeta_ss4"]` (una entrada por forma, `""` = la suya). Esa forma usa el moveset, el guion y las tecnicas del otro port, que debe estar activo. Las capsulas se emparejan por tipo y clave IW (o nombre).
- **Mas de 64 recursos en un BSP:** ya no es un problema, el juego los guarda en una tabla ampliada.

## 4c. EXPORTAR TU HEROE COMO PERSONAJE (1.5.6, solo PC)

En el creador del Modo Heroe, el boton **Exportar** deja `mods/heroe_<nombre>/` como un mod normal (activo, con su celda y nombre en la
seleccion tras JUGAR): el aspecto, las formas Super Saiyan, el moveset (donante) y las tecnicas equipadas (la definitiva primero).
Es independiente del heroe (no toca su guardado); dos nombres distintos conviven y el mismo nombre da `heroe_<nombre>_2`. Si no queda
plaza de personaje nuevo, el mod queda con `.disabled` y la pantalla lo dice.
- **No compartas esa carpeta**: el modelo lleva partes de tu juego (y quiza de Xenoverse 2 convertido). Comparte **`receta.toml`**
  (IDs de piezas, colores, donante, tecnicas; sin arte).
- Rehacerlo desde una receta, con el juego de quien la abre: `python "mod center hd/cac/vista_heroe.py" --juego <carpeta> --receta receta.toml`
  (si la receta usa Xenoverse 2 y no esta instalado, usa piezas de B3 y lo apunta en `ingame_ui/heroe_vista/montaje.log`).

## 5. PORTAR UN PERSONAJE DE SHIN BUDOKAI (de principio a fin)

Fuentes: las ISOs de PSP en `ps2_games/` (Shin Budokai = `sb1`, Another Road = `sb2`) y, para
modelos HD mejores, la carpeta de Super Dragon Ball Heroes World Mission en `modding resources/`
(`sdbh`). Formatos y diferencias: `docs/03_formatos/SB_VS_B3_MOVESET.md`.

### 5.1 Lo rápido (sin consola)
Launcher → **Personajes nuevos** → «Importar personaje» → *Shin Budokai: Another Road* → el
personaje → nombre → **Importar** → JUGAR. En el Mod Kit es la tarjeta «Importar personaje».

Qué hace el importador (`mod center hd/importar.py importar sb2 GHF --mod … --nombre …`):
1. **Modelos:** cada `BC<XXX>B0n.amb` → bin HD (`awo_tools/psp_amo.py`); con `sdbh`, cada
   `bc<xxx>bNN` → bin HD con boca, 7 caras y rampas (`awo_tools/sdbh_model.py`, plantilla = el modelo
   del donante). Van como **formas** de un traje (`modelos_por_traje`).
2. **Moveset y cámara:** `awo_tools/sbport.py` (AP 20 → 16 B, HR 160 → 128 B, daño × 0,85, códigos
   remapeados, almacén global por tabla, hiper / agarre / Dragon Rush / definitiva del donante).
   Deja `moveset/anm_forma1.bin` y `moveset/camara.bin`.
3. **Técnicas:** `awo_tools/sb_tecnicas.py` (BSP híbrido, nombres oficiales) cuando esté listo →
   `moveset/tecnicas.bin`.
4. **Formas:** `formas = n`, `transformacion = "donante"` (P+K+G del donante, quita la abajo+E de SB),
   `fisica = "donante"` y una cápsula de transformación por forma con el nombre de la del donante
   (sus barras de ki). Ver `docs/03_formatos/FORMAS_Y_KI.md`.
5. **Voces:** las del donante hasta que el módulo de voces entienda `sb2:XXX`.

Si `sbport.py` falla o no está, el personaje se importa igual con **los golpes del donante** (y
tantas formas como tenga el donante) y el registro lo dice.

### 5.1b Presets (qué se trae del juego de origen)
Cada fuente tiene presets estándar en `mod center hd/presets_importar.toml`; el launcher y el Mod
Kit los muestran en un desplegable (con su descripción) y cualquier opción se cambia al crear el
personaje (`--opcion clave=valor`):

| Fuente | `original` | `solo_modelos` |
|---|---|---|
| Budokai 1 (`b1`) | golpes, combos, gritos y definitiva de B1 | solo el modelo |
| Budokai 2 (`b2`) | golpes y combos de B2 sobre el cuerpo del donante; pelo y cara (normal y Super Saiyan) pegados a la cabeza; técnicas, definitiva y voz del donante | solo el modelo (golpes, técnicas y voz del donante) |
| Shin Budokai (`sb1`) / Another Road (`sb2`) | formas, golpes, técnicas que evolucionan, definitiva y voces | solo los modelos (voces de SB) |
| Infinite World (`iw`) | formas, golpes, técnicas, hiper, definitiva y voces de IW | solo los modelos y sus formas |
| Budokai 3 comunidad (`b3`), Heroes (`sdbh`) | lo que traiga el port / el modelo sobre el donante | — |

> Modelos de Budokai 2: al montar el roster, el Kit convierte solos sus rampas de sombreado al estilo de B3 (`awo_tools/rampas.py`); la piel ya no sale blanca ni con manchas negras. No hay que hacer nada.

Opciones: `golpes` (fuente/donante), `voces` (fuente/donante/ninguna), `fisica`
(auto/donante/ninguna), `altura`, `pose_select`, `combos_tecnica`. El preset y las opciones usadas
quedan apuntados en el `personaje.toml` del mod (`preset`, `preset_opciones`, y de dónde sale:
`origen_fuente`, `origen_clave`, `origen_donante`), así otra persona puede recrear el mismo port:
```powershell
python "mod center hd/importar.py" presets iw                     # presets y sus opciones
python "mod center hd/importar.py" importar iw <ID> --mod <mod> --nombre "<nombre>" --preset original --opcion voces=donante
python "mod center hd/importar.py" rehacer <mod>                  # lo vuelve a crear igual
python "mod center hd/importar.py" rehacer <mod> --preset solo_modelos   # o con otro preset
```
`rehacer` nunca borra: el mod anterior se mueve a `mods_respaldo/<mod>_<fecha>` (junto a `mods/`) (y si falla, se
devuelve tal cual).

### 5.2 Ajustar después (Mod Kit → Personajes)
- **Formas, física y aspecto:** formas, ki base por forma, modelo por forma, física de pelo y
  cinturón, transformación del donante.
- **Cápsulas:** nombres oficiales, orden, **«Fijar ki»** de cada transformación (barras que hay que
  tener; no se gastan).
- **Cámaras (Studio):** las definitivas de SB no traen cámara propia: hazla con el Studio (§6).
- **Imágenes:** icono, rótulo y retratos se generan del modelo; «Regenerar vista previa».

### 5.3 A mano (consola)
```powershell
python awo_tools/sbport.py --lista --juego sb2                       # códigos de 3 letras
python "mod center hd/importar.py" lista sb2                          # lo que ve el launcher
python "mod center hd/importar.py" importar sb2 GHF --mod imp_sb2_future_gohan --nombre "Future Gohan" --mods <carpeta de mods>
python "mod center hd/roster_build.py" construir --mods <carpeta de mods>    # o pulsar JUGAR
```
Para probar sin tocar tus mods, usa una carpeta aparte con `--mods` (`construir` reescribe el
`_roster` de la carpeta que le pases).

### 5.4 Qué mirar en el juego
Golpes y daño normales; P+K+G con 3/4/5/6 barras; volver a normal con menos de 1 barra; la ficha
de pausa («With over N Ki gauges»); especiales y definitiva; el cinturón en reposo y al andar; la
sombra toon y el brillo HD al 0 % y al 100 %.

---

## 6. CÁMARAS DE TÉCNICAS (Studio)

Guía para usuarios: `docs/02_mods/STUDIO_CAMARAS.md`. Formato: `docs/03_formatos/CAMARA_ACC.md`.

- **Personaje del juego:** el Studio escribe `mods/studio_<personaje>/us/data_cmn.afs/<fid CAM>/geom.bin`
  (LZX `/N:2048`, relleno a un tamaño **reservado** para poder recargar sin reiniciar) y un
  `studio.json` con los clips editados.
- **Personaje nuevo:** reescribe su `moveset/camara.bin` (se monta al pulsar JUGAR).
- Respaldos en `mods/<mod>/respaldo/<fecha>/` (nunca dentro de `us/`: el runtime sirve el primer
  fichero de la carpeta de la entrada).
- Ciclo de prueba: guardar → Pausa → «Reelegir personajes» → lanzar la técnica. La primera vez,
  reiniciar el juego.
- Consola: `python "mod center hd/studio/studio_core.py" info 0` (clips y guiones de Goku),
  `exportar-glb`, `importar-glb`, `selftest`.


## Historias propias de Dragon Universe

0. Lo más fácil: abre el **Editor de historias** del Mod Kit (categoría "Campañas de Dragon Universe", `editor_historias.py`). Ahí eliges rival, formas, trajes, premios, retratos y textos con listas y botones, y "Montar mod" hace el resto. Si prefieres editar a mano:
1. Copia `mod center hd/campanas/plantilla_historia.toml` con otro nombre (por ejemplo `mi_historia.toml`).
2. Cambia los textos. Cada `[[capitulo]]` es un combate con una charla antes (`dialogo_es`/`dialogo_en`) y otra después (`victoria_es`/`victoria_en`). Un capítulo con `saga = true` abre una saga nueva: antes salen el cierre de la anterior (`cierre_es`), la pantalla de "¿guardar?" y el mapa nuevo. El último capítulo, sin combate, es el final.
3. Monta el mod: en el Mod Kit, categoría "Campañas de Dragon Universe", herramienta `campana.py`, orden `montar` con tu archivo. Desde la consola es `python campana.py montar campanas/mi_historia.toml`.
4. Activa el mod en el launcher como cualquier otro.

Los textos se convierten en imágenes en los 6 idiomas; si falta un idioma se usa el inglés. Si prefieres dibujar tú una página o un bocadillo, pon en la lista el nombre de un PNG (por ejemplo `"pagina1.png"`, junto al `.toml`) y se usa tal cual. Tienes un ejemplo completo y probado en `campanas/historia_freezer.toml` (la historia recortada de Freezer). El detalle técnico está en `docs/11_campanas/CAMPANAS.md`.

Novedades para historias propias (la plantilla trae ejemplos comentados de cada una):
- **Idioma único**: `idioma = "en"` en `[historia]` muestra ese idioma en todos, útil mientras faltan traducciones.
- **Imágenes en lugar de paneles**: en una saga, `imagen` (intro) o `cierre_imagen` (cierre) pueden ser una foto .png, .webp, .jpg o .avif tuya, que se recorta sola a 660x420.
- **Estilo del texto**: `[historia.estilos.dialogo]`, `narracion` y `final` cambian fuente, tamaño, color y borde de cada tipo de texto. Por defecto narración y final salen como los del juego.
- **Fondo del final**: `fondo = "heroe"` (Goku) o `"villano"` (Broly).
- **Editor de historias**: en el Mod Kit, pestaña «Historias». Tiene selector de idioma, el botón «Rellenar traducciones que faltan» (marca como pendientes las que copia), la pestaña «Estilos de texto» con vista previa en vivo, selector de imagen para la narración y vista previa de los retratos.


## Poner música propia (Mod Kit)
En el Mod Kit, «Poner música»: eliges un mp3, wav, ogg, flac o adx tuyo, le das un nombre y el Kit lo convierte (con ffmpeg si no es .adx) y lo deja como un mod nuevo `musica_<nombre>` en `mods/` (con «Deshacer»). Para oírla, en el selector de combate pulsa L1/R1 (o `[` `]`) o usa F1 > Música de combate y elige «Mi música»; también vale `dbz3_musica_combate = fija:propia:<n>`. Solo se añade: la música original del juego no se toca. Usa solo canciones que tengas derecho a usar; el proyecto no reparte audio de juegos.

## Modo PS2: mods para el Budokai 3 de PS2

En el Mod Kit, arriba a la derecha, **Juego > B3 PS2**. Crea mods para tu copia de PS2 (Greatest Hits USA, SLUS-20998: una .iso o la carpeta extraída del disco; un .chd hay que convertirlo antes) y genera una ISO nueva. Tu copia nunca se modifica ni se reparte nada.

1. **Textura**: elige personaje y forma, «Sacar texturas», edita los PNG sin cambiar su tamaño y «Crear mod con mis cambios». La PS2 usa paletas de 16 o 256 colores: al guardar se reducen los colores de tu imagen; lo que no toques queda igual.
2. **Personaje**: un personaje con el aspecto de otro. Conserva nombre, icono y golpes. No añade personajes a la rueda.
3. **Música**: sustituye una pista (se puede escuchar antes) y se repite al acabar.
4. Enciende tus mods y pulsa **Montar ISO** (unos segundos, 2,4 GB). Ábrela en PCSX2. Probado en PCSX2 2.9; en consola real no se ha probado.

Qué no está: personajes nuevos, cápsulas, colores de aura, historias, Studio y packs HD. Necesitan el motor del port; la pantalla lo explica. Por línea de comandos: `python "mod center hd/ps2_b3.py"`.

## Arte opcional y lo aprendido en la ronda 31/10
**Revisar este mod (Mod Kit, Mis mods):** avisa de archivos que faltan, errores de escritura en `personaje.toml` o `traje.toml`, `.wav` que no sirven y mods metidos en una carpeta de más. No abre el juego, así que no prueba cómo se ve.

**Arte opcional (Mod Kit):** elige la carpeta con tus PNG; dice qué pieza es cada uno, avisa de forma, tamaño y transparencia, corrige la doble extensión (`x.png.png`) y sugiere el nombre si te has equivocado por poco. Instala en `mods/pack_hub_mio/ui_ingame/hub/`: es un mod normal, se apaga y se deshace. Cualquier mod puede traer `ui_ingame/hub/<pieza>.png`.

**Trajes desde DAE:** `awo_tools/dae_a_b3.py` convierte un modelo DAE con huesos de estilo Xenoverse/Legends a traje (`--mod CARPETA` escribe `traje.toml` y `modelo.lzx`). La boca se repesa a `M_JAW`; compruébala siempre en juego, sobre todo si el modelo lleva máscara.

**Rueda y trajes bloqueados:** un traje extra se añade detrás de los nativos. Si en esa partida hay nativos bloqueados, el selector se los salta y llega al añadido. Pruébalo con una partida que no lo tenga todo desbloqueado.

**Comprobar un personaje:**
- Una foto en combate no demuestra que flote: puede estar saltando. Mide los pies o usa un plano lateral en reposo.
- Un brazo plano no siempre es culpa del sombreado: mira primero si la textura ya viene plana.
- Una métrica contra tu propio render no prueba nada: compara con una captura del juego o del original.
- Sin moveset propio, un personaje se mueve como su donante. Renombrar las técnicas del donante no las hace propias.

**Voces:** `voces = "t3:<banco>|ninguna"` toma las voces de tu Tenkaichi 3 y sigue mudo sin la ISO. Esos clips no traen tabla de situaciones: asignarlos por duración puede soltar una frase a destiempo, así que comprueba cada uno. Mejor mudo que una frase equivocada.
