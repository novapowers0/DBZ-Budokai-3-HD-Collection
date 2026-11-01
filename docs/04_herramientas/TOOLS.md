# Herramientas — inventario

> Actualizado: 2026-10-06 (§0 Studio de cámaras y port de Shin Budokai / Heroes). Inventario de
> herramientas disponibles y su función.

---

Vídeo: `tools/grabar.py` graba un escenario de `pruebas.py` con su audio; `tools/grabar_vitrina.py` (`--dry-run`) graba cada traje girando con la cámara de prueba `DBZ3_TEST_VITRINA`.

---

## 0. KIT DE MODS: PERSONAJES NUEVOS Y STUDIO (2026-10-06)

Todo esto se usa sin consola desde el Mod Kit (`mod center hd/modkit_gui.py`) y el launcher; la
consola es para modders. Cada herramienta con lógica tiene su autocomprobación.

### Studio de cámaras (`mod center hd/studio/`)
| Herramienta | Función | Comprobación |
|---|---|---|
| `studio_gui.py` | Ventana del Studio: personaje + técnica, línea de tiempo del guion, vistas 2D, plantillas (órbita, travelling, temblor, giro), vista previa toon, GIF, Blender, guardar como mod | `--selftest` (sin ventana), `--captura out.png` |
| `studio_core.py` | Núcleo: `#ACC/#AMC` (clips de cámara), guion `#SPX` (clip ↔ técnica, esperas), marcas de golpe del `#CSK`, validación, plantillas, glTF de ida y vuelta, guardar con LZX + reserva + respaldo | `selftest` (633 clips HD + 633 PS2 byte a byte), `info ID`, `exportar-glb`, `importar-glb` |
| `blender_puente.py` | Script que ejecuta Blender (sin add-on): `abrir` (importa la toma y guarda un .blend) y `exportar` (en segundo plano, `.blend` → `.glb`) | ida y vuelta con `blender -b` |

Guía: `docs/02_mods/STUDIO_CAMARAS.md`. Formato: `docs/03_formatos/CAMARA_ACC.md`.

### Personajes de Shin Budokai (PSP) y Super Dragon Ball Heroes (PC)
| Herramienta | Función | Comprobación |
|---|---|---|
| `mod center hd/importar.py` | Importador del launcher y del Mod Kit. Fuentes `b1`, `b2`, `b3`, `iw`, **`sb1`, `sb2`, `sdbh`** | `fuentes`, `lista FUENTE`, `presets FUENTE`, `importar … [--preset P] [--opcion k=v]`, `rehacer MOD [--preset P] [--opcion k=v]` (vuelve a importar con la receta `origen_*`/`preset` de su `personaje.toml`; el mod anterior va a `mods_respaldo/`) |
| `awo_tools/sbport.py` | Moveset de SB1/SB2 → B3 (AP 20→16 B, HR 160→128 B, daño × 0,85, códigos, almacén global, injertos del donante) | `--prueba`, `--oraculo` |
| `awo_tools/sb_tablas.py` | Tablas medidas SB → B3 (almacén global, efectos AP7) | (módulo) |
| `awo_tools/sb_amm.py` | Descompresor de animaciones de SB | (módulo) |
| `awo_tools/sb_tecnicas.py` | Técnicas de SB: BSP híbrido, nombres oficiales (`bsp`, `aplicar`, `nombres`). Técnicas que **evolucionan** con la forma (misma entrada, máscaras de formas disjuntas: Masenko → Spirit Shot → Kamehameha) = UNA cápsula + `anm_formaK.bin` por forma (2026-10-08) | `prueba` |
| `mod center hd/auditar_importar.py` | Importa y monta en una carpeta aparte todos los personajes de una fuente y anota avisos/errores (TSV) | `--fuente sb2 --mods D:/tmp/aud --construir` |
| `awo_tools/cinematica.py` | Definitiva con animaciones propias sobre la cinemática del donante: cada código 0x4A0+ elegido recibe un tramo (o varios seguidos) re-temporizado de otras animaciones (giros de esas, cadera y huesos de anclaje del original); el guion #SPX, la cámara y la versión «defiende» no cambian. Clave `definitiva_animaciones` de personaje.toml (tarjeta «Definitiva» del Mod Kit) | `prueba` |
| `awo_tools/b1port.py` (definitiva) | Traduce la definitiva de B1 (el especial con guion SPX más largo: ráfaga, lanzamiento) a la cinemática del donante: `b1_ultimate`, `cine_win` (versión «gana»: desde el 1er código del guion hasta el hueco o la repetición), `receta_definitiva` (a su velocidad, hasta 1,5× más lenta). El importador escribe `moveset/definitiva.json` y la cápsula con el nombre oficial (`mod center hd/b1_capsulas.txt`) | `prueba`, `--salida DIR` deja `definitiva.json` |
| `mod center hd/iso.py` | Lee ISOs de PS2/PSP sin extraer; `find_game` (B1/B2/SB1/SB2 de cualquier región), `boot_elf` (SYSTEM.CNF), `find_region` (`_us` ↔ `_eu`) | `prueba` |
| `mod center hd/colores.py` | Color del aura (bin `aura` de data_cmn: #AZT + #CTR wk0/wk1 RGB) y del ki (BSP: #AZT + partículas #ACE, colores RGBA f32 en +0x80): solo el tono. Claves `aura_color` / `ki_color` de personaje.toml (roster_build) y tarjeta «Colores» del Mod Kit; `nativo ID --aura --ki --tecnica CAPSULA=COLOR` para personajes del juego (solo esa técnica: copias de sus #ACE/texturas) | `prueba` |
| `mod center hd/hexedit.py` | Editor hex para expertos (pestaña **Hex** del Mod Kit avanzado): árbol #AMB LE/BE, partes de #AMO con «ocultar parte» (vtype/vflags `FFFFFF00 FFFF0000`, el truco de trajes alternativos; `amo2awo` las omite), inspector LE/BE, copia al guardar | `--selftest` |
| `awo_tools/psp_amo.py` | Modelo de PSP → bin HD | byte a byte con los trajes del port de Gohan del Futuro |
| `awo_tools/sdbh_model.py` | Modelo de Heroes (EMD/ESK/EMB) → bin HD con boca, 7 caras y rampas nativas | `--selftest` |
| `mod center hd/roster_build.py` | Monta los personajes nuevos; claves `formas`, `ki_base`, `modelo_forma`, `fisica`, `transformacion`; `capsulas --ki` | `construir --mods <copia>` |

Formatos: `docs/03_formatos/SB_VS_B3_MOVESET.md`, `FORMAS_Y_KI.md`, `TOON_Y_BRILLO_HD.md`.
Receta completa: `docs/02_mods/COMO_HACER_MODS.md` §5.

---

## 1. NUESTRAS HERRAMIENTAS (awo_tools/ + mod center hd/)

### awo_tools/ — scripts de RE y conversión
| Herramienta | Función | Estado |
|---|---|---|
| `analyze_bin_hd.py` | Parser histórico basado en template PS3 | ⚠️ Obsoleta; solo referencia |
| `awg_to_obj_b3.py` | Exportador OBJ de bins B3 completos | ✅ Recomendada |
| `awg0_export.py` | Exportador AWG0 con autodetección A/C | ✅ Recomendada |
| `awg_cara_export.py` | Exportador de AWGs de cara | ✅ Recomendada |
| `parse_ps2_mesh.py` | Parser de malla PS2 (verts+IB) | ✅ |
| `pose_matrix.py` | Matrices world de huesos PS2 | ✅ |
| `rig_mapeo.py` | Re-mapeo JNB→KLL por labels | ✅ |
| `build_awo_desde_cero.py` | Parsear Janemba.amb → AMGs | ✅ (extracción) |
| `build_janemba_final.py` | Inyectar geometría de Janemba en Krillin | 🔸 en investigación |
| `swap_cuerpo_hd.py` | Inyectar cuerpo de Goten en Krillin | 🔸 en investigación |
| `build_janemba2.py`, `build_afs.py`, `mezclar_ps2_hd.py` | Experimentos previos | 🔸 archivable |

### Xenoverse 1/2 — lectores e importación local GOK/VGT/PIC (sol-XV4, 2026-10-30)

Todo se lee de la copia del jugador; **no se distribuyen modelos, texturas ni arte convertido**.
Lectores PC little-endian y conversión local de Goku, Vegeta y Piccolo XV2 (traje 0) a B3.
La prueba local carga el port y usa el moveset de B3; las limitaciones del material y la animación fina se detallan abajo.

| Herramienta | Función | Comprobación |
|---|---|---|
| `awo_tools/xeno_cpk.py` | Índice CRI CPK: `info`, `catalogo`, `buscar`, `extraer`. Extrae el blob original; `extraer --descomprimir` aplica CRILAYLA. Rechaza nombres cortos ambiguos y no sobreescribe salidas | `--selftest`: CPK/@UTF sintéticos (claro y cifrado), rutas, offsets, búsqueda y lectura truncada |
| `awo_tools/xeno_model.py` | `Cpk.get()` descomprime en memoria; admite `with Cpk(...)`. ESK con TRS completa, padres posteriores y detección de ciclos; EMD f16/f32, pesos, paletas, espejo Z y escala ×10; EMB/DDS a RGBA | `--selftest`: los cinco lectores y resumen integrado con datos inventados; `--cpk … --personaje GOK` comprueba datos reales |
| `awo_tools/xeno_emm.py` | `leer_emm(bytes)` → materiales/shaders/parámetros tipados. Conserva índice, huecos, tipo desconocido y bits crudos; `Float2` es un escalar, no un vector | `--selftest`; archivo EMM local o `--cpk … --entry … [--salida nuevo.json]` |
| `awo_tools/xeno_bcs.py` | `leer_bcs(bytes)` → conjuntos y sus diez partes, IDs originales, modelos/rutas, selectores de tinte, paletas RGBA, físicas declaradas, escalas corporales y tablas de esqueleto. Cabeceras XV1/XV2 y variante XV2 sin firma | `--selftest`; archivo BCS local o `--cpk … --entry … [--salida nuevo.json]` |
| `awo_tools/xeno_rig.py` | Informe ESK → B3 con alias anatómicos explícitos **[H]**; separa candidatos, no casados y posibles ancestros. Lee los nombres reales del #AWG de B3 | `--selftest`; tres personajes con `--cpk`, `--b3-afs`, `--xbdecompress`, `--trabajo` y `--salida` |
| `mod center hd/xv2/xv2_cerca.py` | Primeros planos EN JUEGO de un bin (cara, pelo, manos, ropa, golpe, dano) con la camara de prueba `dbz3_cam.req`; `BIN --salida DIR [--entrada N --ids A,B,E,T]`. |
| `awo_tools/xeno_binary.py` | Utilidades compartidas: comprobación de rangos/firmas, cadenas, JSON de creación exclusiva y constructores N0 en memoria | Comprobadas por los selftests anteriores |
| `awo_tools/xeno_a_b3.py` | BCS/EMD/ESK/EMB/EMM GOK/VGT/PIC → #AMB HD mediante `sdbh_model`/`amo2awo`. Conserva ejes, jerarquía y paleta B3; rebind anatómico, pesos remapeados, DYT convertidos a resta B3, variantes de manos | `--selftest`: geometría, pesos, normales, DYT, dedos y selección de variantes; roundtrip sintético AMB/AWO/AWG/AZT, sin datos del juego |
| `awo_tools/xeno_prueba.py` | Comprime LZX `/N:2048`, instala override con padding en una copia fija y prueba reposo, desplazamiento, golpe y fin de combate. Capturas ×0,5; compara el pool de vértices en memoria, poses y daño. Reserva la copia hasta restaurar sus mods; deja el port desactivado | `--selftest` sin juego; `--modelo … --copia smoke_a`. Rechaza errores del log, un modelo distinto o un golpe sin confirmar |
| `awo_tools/xeno_importar.py` | Fuente local del Kit: detecta Steam por registro, bibliotecas y manifiesto; convierte GOK/VGT/PIC sobre un ID B3 y crea un mod desactivado (US/EU). Respalda los reemplazos y protege la carpeta XV2 contra escrituras | `python -B "mod center hd/importar.py" xv2 --selftest`: Steam simulado, ausencia, rutas e IDs; no abre juegos ni registro |

Pruebas N0 (no necesitan datos de ningún juego ni crean temporales):

```powershell
python -B awo_tools/xeno_cpk.py --selftest
python -B awo_tools/xeno_model.py --selftest
python -B awo_tools/xeno_emm.py --selftest
python -B awo_tools/xeno_bcs.py --selftest
python -B awo_tools/xeno_rig.py --selftest
python -B awo_tools/xeno_a_b3.py --selftest
python -B awo_tools/xeno_prueba.py --selftest
python -B "mod center hd/importar.py" xv2 --selftest
```

Ejemplo real, con `D:\tmp\xv` ya creada (los JSON deben tener nombres nuevos):

```powershell
$xv = 'D:\SteamLibrary\steamapps\common\DB Xenoverse 2\cpk\data.cpk'
python -B awo_tools/xeno_model.py --cpk "$xv" --personaje GOK --salida D:/tmp/xv/GOK_resumen.json
python -B awo_tools/xeno_emm.py --cpk "$xv" --entry data/chara/GOK/GOK_000_Bust.emm --salida D:/tmp/xv/GOK_bust_emm.json
python -B awo_tools/xeno_bcs.py --cpk "$xv" --entry data/chara/GOK/GOK.bcs --salida D:/tmp/xv/GOK_bcs.json
$xdk = 'mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\xbdecompress.exe'
python -B awo_tools/xeno_rig.py --cpk "$xv" --b3-afs us/data_cmn.afs --xbdecompress "$xdk" --trabajo D:/tmp/xv --salida D:/tmp/xv/mapa_3_personajes.json
```

La API histórica `Cpk()` sigue buscando XV1 (`DBZ3_XENO_CPK`); el comando de resumen busca XV2
(`DBZ3_XV2_CPK`, después las bibliotecas Steam). `--cpk` explícito siempre tiene prioridad.
Dependencias ya presentes: NumPy y Pillow para geometría/imágenes; EMM/BCS sueltos solo usan Python.
Se procesa el CPK elegido: no se resuelve todavía la precedencia entre archivos base, actualizaciones y DLC.

**Medido en XV2 (CPK base del jugador, 104.057 entradas):** Goku: 34 conjuntos, 230 piezas,
20 paletas, 53 referencias de física; 94 EMM con 238 materiales y 4.833 parámetros.
Su cuerpo `GOK_000_Bust` contiene 4 submallas, 3.776 vértices y 5.568 triángulos; su EMB trae 2 DDS.
El resumen comprueba que cada submalla encuentra su material EMM y sus índices de textura en EMB.
También se leyeron sin errores los BCS de VGT, PIC y las seis razas CaC (HUM/HUF/NMC/FRI/MAM/MAF).

| ESK XV2 → plantilla B3 | Huesos XV2 / B3 | Candidatos [H] / sin casar | Error máx. TRS frente a inversa bind almacenada |
|---|---|---|---|
| Goku → entrada AFS 264 | 122 / 46 | 23 / 99 | 1,32 × 10⁻⁶ |
| Vegeta → entrada AFS 416 | 132 / 43 | 23 / 109 | 9,58 × 10⁻⁷ |
| Piccolo → entrada AFS 350 | 142 / 55 | 23 / 119 | 1,91 × 10⁻⁶ |

Estos resultados de `xeno_rig.py` son **candidatos**, no una validación del retarget; la prueba GOK de `xeno_a_b3.py` es independiente.
Los ancestros no se cuentan como coincidencias. Los archivos B3 temporales del informe quedan en
`D:\tmp\xv\b3_*`; XV2 se mantiene en memoria. Los JSON son diagnósticos locales, no assets de una release.
EMM no evalúa shaders ni aplica tintes y BCS no simula físicas. EMB conserva `None` para entradas no DDS;
EMD rechaza vértices compartidos entre paletas incompatibles en vez de asignarles huesos silenciosamente.
Referencia de layout (consulta 2026-10-30):
[XV2-Tools, EMM](https://github.com/LazyBone152/XV2-Tools/tree/master/Xv2CoreLib/EMM) y
[BCS](https://github.com/LazyBone152/XV2-Tools/tree/master/Xv2CoreLib/BCS) (MIT); lectores escritos aquí en Python.

**Conversión y prueba GOK (sol-XV2):** crea primero `D:\tmp\xv2b3`. La salida `.bin` es un
`#AMB` descomprimido, no un override instalable directamente. Tanto el `.bin` como el JSON deben
tener nombres nuevos: el conversor no sobreescribe archivos.

```powershell
$xv = 'D:\SteamLibrary\steamapps\common\DB Xenoverse 2\cpk\data.cpk'
$xdk = 'mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\xbdecompress.exe'
python -B awo_tools/xeno_a_b3.py --cpk "$xv" --b3-afs us/data_cmn.afs --xbdecompress "$xdk" --trabajo D:/tmp/xv2b3 --salida D:/tmp/xv2b3/GOK.bin --informe D:/tmp/xv2b3/GOK.json
python -B awo_tools/xeno_prueba.py --modelo D:/tmp/xv2b3/GOK.bin --copia smoke_a
```

- Alternativas de entrada: `--fuente <carpeta GOK extraída>` en lugar de `--cpk`, y
  `--plantilla <AMB Goku B3 descomprimido>` en lugar de `--b3-afs`/`--xbdecompress`.
- Valores por defecto medidos: LOD3, `--reduccion 0.5` con el reductor existente que protege
  costuras/pesos, `--textura 128`. Resultado: **635.424 B**, 5.078 ventanas, 23 draws,
  36 texturas; AZT 338.304 B. Esta cifra de ventanas está comprobada para este port y escritor.
- Retarget: **22 huesos anatómicos directos de 122**, sobre los 46 del donante. Los otros 100
  se describen individualmente en `retarget.unmatched`: helpers, dedos, rostro, segunda vértebra,
  nodos de exportación/cámara. La mandíbula candidata anterior se hornea con la cabeza;
  no se presenta como un controlador facial validado. Los 16 huesos SCD de pelo/cinturón se
  informan aparte. Las piezas SCD se llevan desde su marco de attachment al espacio del cuerpo.
- No se copian las orientaciones de las extremidades ESK: difieren de B3. El rebind alinea
  segmentos y conserva los ejes B3 para sus animaciones. Error máximo de extremos: 1,99 × 10⁻¹⁵;
  ida/vuelta tras empaquetar posiciones f32: 2,37 × 10⁻⁷. B3 admite hueso + mezcla con padre:
  `weights` documenta la masa descartada al reducir las cuatro influencias XV2 y los slots usados.
- Material: base blanca con detalle alfa DFD horneado y rampa `255 − DYT`, de modo que la
  resta de B3 reproduce la curva de color. `MatScale1X` selecciona el bloque; el segundo
  texdef EMD **no** es el índice de variante DYT. El iris usa su máscara y pupila.
  No equivale a los shaders, manchas ni iluminación de XV2; **SSIM contra XV2 pendiente**.
- Comprobación real: **4 combates completos**, sin `[error]`, Goku nativo ID 0 contra Vegeta ID 7;
  reposo `0x000`, caminar `0x003`, golpe `0x200` con daño confirmado de 30. Los **223.432 B**
  del pool coinciden íntegramente en memoria guest y las matrices de pose cambian.
  La ruta de AFS virtual es silenciosa: exigir `AFS OVERRIDE HIT` daría un falso negativo.
- Evidencias finales: `D:\tmp\xv2b3\prueba_20261030_163155_753542\` (`resultado.json`,
  `runtime.json`, log y capturas 640×360). Hay tres combates completos anteriores bajo el mismo
  directorio de trabajo. Las pruebas de arranque anteriores fallidas también se conservan.
- Mod final: `D:\DBZ3HD\smoke_a\mods\xv2_GOK_local\.disabled`. Sus otros mods recuperan
  su estado; respaldos/reemplazos en `D:\_PARA_BORRAR_CLAUDE\xv\`. El arnés usa como máximo
  las dos plazas visibles del proyecto y ejecuta una sola instancia propia.
- **Límite de aquella entrega (sol-XV2):** cara y manos tenían una única variante.
  Las manos se amplían en sol-XV3, explicado a continuación; E3 sigue abierto.

**Importación para el jugador (sol-XV3):** desde el Kit instalado junto a B3, con tu copia
de Xenoverse 2 instalada en Steam:

```powershell
python -B "mod center hd/importar.py" xv2 GOK --sobre 0
python -B "mod center hd/importar.py" xv2 VGT --sobre 7
python -B "mod center hd/importar.py" xv2 PIC --sobre 11
```

- `--sobre` es el **ID del personaje de B3**, no la entrada del AFS. Se sustituye únicamente
  su modelo base; se conservan los movimientos y voces de B3. No añade casillas ni transforma
  las otras formas del personaje. La interfaz anterior `importar importar xv2 …` sigue disponible.
- El resultado queda en `mods/xv2_<código>_sobre_<ID>_local/`, **desactivado**. Se activa/desactiva
  desde el Centro de mods. Conviene activar un solo reemplazo para cada personaje.
- La búsqueda lee registro de Steam, `libraryfolders.vdf` y `appmanifest_454650.acf`, incluso
  bibliotecas fuera de las unidades habituales. `--cpk <archivo>` tiene prioridad; también
  `DBZ3_XV2_CPK` y el alias anterior `DBZ3_XENO2_CPK`. Una variable explícita inexistente no
  cae silenciosamente en otra instalación: informa «No encuentro Xenoverse 2…».
- Opciones: `--mods <carpeta>`, `--us <datos B3>`, `--trabajo <carpeta>` y `--respaldo <carpeta>`.
  Los temporales/JSON se conservan localmente; si el mod ya existe, el anterior se mueve a
  `mods_respaldo/`. Las salidas dentro de XV2 se rechazan. **Nunca distribuir los modelos,
  texturas o mods resultantes:** cada jugador convierte desde su propia copia, solo leída.
- Piezas elegidas por el conjunto 0 del BCS, incluidos pelo/cinturón/falda/capa SCD y sus
  attachments. Admite las mayúsculas mezcladas del CPK y DYT compartido con la pieza propietaria.
- Manos: se usa **Rist LOD0**, porque LOD3 elimina los pesos de los dedos. Se hornean reposo
  relajado, **L01 puño** y **L02/04/05 abierta**, seleccionados por las listas nativas B3;
  los demás índices vuelven al reposo. Los ángulos son aproximaciones semánticas **[H]**, no EAN.
- Cara: expresión neutra original de XV2, **boca cerrada**, también cuando B3 pide otros gestos.
  Faltan expresiones de daño/grito, parpadeo y labios; señalar/garra/accesorios de manos,
  físicas SCD dinámicas y comparación del material contra XV2. CaC no se modifica en esta entrega.
- Prueba de un mod ya importado (sustituye la ruta de `VGT.bin` por la del diagnóstico):

  ```powershell
  python -B awo_tools/xeno_prueba.py --personaje VGT --modelo D:/tmp/xv3/importar_revg9tkr/VGT.bin --mod xv2_VGT_sobre_7_local --copia smoke_a --trabajo D:/tmp/xv3
  ```

  `--mod` exige coincidencia exacta con el modelo comprimido instalado; al acabar queda
  desactivado y se restaura el estado de los otros mods.
- Validación sol-XV3: **8/8 selftests N0**; **3/3 combates US completos**, uno por GOK/VGT/PIC,
  con cambio de pose, golpe confirmado, pool exacto en memoria y cero `[error]`. Quince capturas
  640×360 y resultados en `D:/tmp/xv3/ENTREGA.json`. Los modelos finales son idénticos por SHA256
  a los probados; instalación US/EU y `.disabled` comprobados (el combate EU no se ha probado).
  Tamaños finales: GOK **736.096 B**, VGT **600.672 B**, PIC **627.168 B**; cuatro AWG auxiliares
  de manos por modelo. Los respaldos de esta sesión están en `D:/_PARA_BORRAR_CLAUDE/xv/`.

**Actualización sol-XV4 (vigente; los tamaños/valores anteriores son históricos):**

- `xeno_gestos.py`: lector FCE EAN PC acotado, **IDs zero-based** y cuatro componentes
  por canal. No reutiliza el lector SDBH que resta 1 al ID. Hornea la instantánea de máxima
  desviación de `BHV_BLINK`, `SPK_A`, `DMG_HEAD_F` y `EMO_SHOUT`; no inventa los giros faciales.
  Conserva los alias de las listas del donante B3; asociaciones de gesto entre juegos **[H]**.
  Los cuatro gestos están en GOK; VGT/PIC utilizan los que piden sus listas nativas (no se
  sustituye un alias suyo por un grito arbitrario).
  No reproduce el reloj EAN ni lipsync continuo. Manos: reposo, puño, abierta, señalar (lista 6)
  y garra; 4/5→abierta, 11→garra por presupuesto. No incluye accesorios de mano.
- Valores por defecto (31/10): texturas hasta **1024**, LOD0, sin reduccion; tinta negra del alfa DFD, rampa de la parte alta de la curva DYT, boca pielada a M_JAW/comisuras, gestos de la mitad alta. GOK **5,9 MB**, VGT **6,4 MB**, PIC **9,2 MB**, CaC **5-8 MB**; probado en juego.
  (previsualiza), `-Yes` (L0 sin preguntar), `-Full -Yes` (+ L1 archivados).
  Nunca toca docs/src/assets/mods/SDK/ps2_games. Reporta el top-12 de
  consumidores. Ver cabecera del script.
- `awo_tools/corpus_scan.py` — desde 2026-09-09 **limpia sus temporales** tras
  cada descompresión (antes dejaba ~16 GB en `out/analysis/corpus/.work`).
  Solo `--keep-bin` conserva copias en `.work/bins/`.
- `tools/make_test_iso.py <out.iso> <carpeta> [--quiet]` — genera un **XDVDFS de
  prueba** (lo que el runtime lee como "GDFX") empaquetando una carpeta con el
  layout retail: sirve para validar el **modo disco (ISO)** sin un ISO real
  (v1.2.2 EX, ver `docs/SESION_AUTODETECCION_XEX_2026-09-17.md` §4.bis). Solo
  empaqueta lo que le des: no contiene datos del juego.
- `tools/perf_report.ps1 [-Count N]` (+ `perf_report.cmd` de doble clic) -
  **informe de rendimiento** de los ultimos N logs: config aplicada, ventanas de
  5 s, fps min/med/max, peor `max_frame_ms`, ventanas <58 fps, errores y
  veredicto. Guarda el texto en `%TEMP%\opencode\perf_report.txt`.
- `tools/perf_test_config.ps1 -Scale 1..4 -Msaa on|off [-Show]` - fija escala
  interna + MSAA nativo en `dbz3_user.toml` para las pruebas A/B de rendimiento
  (deja `dbz3_texture_upscale=1` y `dbz3_perf_logging=true`).
- `tools/hidden_run.ps1 -Label <txt> -Seconds <n> [-HideMode 0|1] -Overrides
  "cvar=valor;cvar=valor"` — **arnés de pruebas offscreen** (2026-09-18): lanza
  `dbz3.exe` con la ventana movida fuera de pantalla, aplica overrides al
  `dbz3_user.toml` (backup/restore automático), mata el proceso y resume el log
  (líneas de `AFS OVERRIDE`, `perf fps=`, `upscale pipeline ready`, errores).
  Usar `dbz3_skip_launcher=true` para bootear directo a la partida. Los valores
  de texto del toml van **entre comillas** (`"manual"`, `"fsr"`); sin comillas el
  parser descarta el fichero entero. Ver
  `docs/ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md` §6.

### mod center hd/ — herramientas HD adaptadas
| Herramienta | Función |
|---|---|
| `fbx_ascii.py` | Parser FBX ASCII (SDBH→FBX→datos) |
| `emd_to_awo_hd.py` | Parseo ESK + mapeo SDBH→KLL |
| `build_awo_from_json.py` | JSON→AWO HD |
| `awg_to_obj.py` | Exportar AWG HD→OBJ |
| `obj_to_awg.py` | Importar OBJ→AWG HD |
| `json_to_obj.py` | JSON SDBH→OBJ |
| `RETOPOLOGIA_3D.md` | Documento del pipeline de retopología |

---

## 2. HERRAMIENTAS DE LA COMUNIDAD (mod center/) — 36 programas

### Conversión de modelos
| Herramienta | Función | Formato |
|---|---|---|
| `OBJ to AMG v0.92` | OBJ→mesh parts PS2 (con templates) | PS2 |
| `EMD to AMG v0.90` | Xenoverse EMD→AMG PS2 | PS2 |
| `B3-IW AMO Converter + Shadows` | B3/IW→B1 (exe) | PS2 |
| `Bin to OBJ (English) V3` | Bins→OBJ | PS2 |
| `AMG to OBJ V2` | AMG→OBJ | PS2 |
| `Model Merger Tool` | Fusionar 2 modelos (AMO_LGBT) | PS2 |

### Edición de rig/modelos
| Herramienta | Función |
|---|---|
| `Model Rig Toolset V0.6` | Extractor/Remover de rig (documenta el formato) |
| `Model-Rig Extractor Tool V1.0` | Idem standalone |
| `Bone Addition Tool v1.02` | Añadir huesos al AMO |
| `Model Part Editor` | Convertir partes B3↔B1 |
| `Axis Line Tool` | Generar axis lines del AMO |
| `BoneAxis Display` | Ver posiciones de huesos |

### AMB / AFS
| Herramienta | Función |
|---|---|
| `AFS Toolset v0.90` | Empaquetar/desempaquetar AFS |
| `AMB Tool` / `AMBStudio` | Editor de AMB |
| `Budokai AMB Packer-Unpacker` | Packer/Unpacker AMB |
| `AMB_AMT Manipulator 1.5` | Manipular AMB/AMT |
| `B3_IW Model Converter` | Empaquetar AMB (no conversor) |

### Compresión (CRÍTICO)
| Herramienta | Función |
|---|---|
| `Xbox 360 Compression tool` | **`xbcompress.exe /N:2048`** y `xbdecompress.exe` |

> ⚠️ USAR `/N:2048` SIEMPRE (el juego usa ese blocksize). `/N:32` produce bins
> que exceden el slot → crash.

### Otros
| Herramienta | Función |
|---|---|
| `A3T Analyzer` | Analizar texturas A3T/AZT |
| `CRI Middleware ADX Tools` | Audio ADX |
| `PSound` | Editor de audio |
| `SLXS Editor v0.50` | Añadir personajes (SLXS) |
| `Budokai3_SLUS_Editor` | Editar SLUS (select) |
| `Set Unlimited Fusion` | Fusiones ilimitadas |
| `Transformation Input Stuff` | Transformaciones |
| `Zero Devs' Tool` | Herramienta universal de la comunidad (BT3p→Budokai) |

---

## 3. HERRAMIENTAS DEL DISCORD (modding resources discord/tools/)

| Herramienta | Función |
|---|---|
| `Budokai Modding Tool V1.5` | AMO_LGBT, AMG_C, AXIS_E, SLXS... |
| `AMO Model Separator v1.01` | Separar parts del AMO |
| `Model Part Addition Tool` | Añadir parts |
| `AMG to OBJ V2` | Exportar OBJ |

---

## 4. HERRAMIENTAS DEL SDK (rexglue-sdk-0.10/)

- `rexruntime.dll` — runtime (hook de mods, filesystem, logging)
- `rexgpu-xenos.dll` — backend GPU
- Tracy — profiling (build win-amd64-tracy)

---

## 5. FLUJO RECOMENDADO PARA ESTUDIAR UN MODELO

```powershell
# 1. Descomprimir el bin del AFS
xbdecompress.exe entrada.lzx entrada.bin

# 2. Exportar/verificar la estructura con las herramientas B3 actuales
python awo_tools/awg_to_obj_b3.py entrada.bin salida.obj

# 3. Si es PS2, extraer la malla
python awo_tools/parse_ps2_mesh.py entrada.amb 0 salida
```

---

## 6. ARNES DE PRUEBAS DEL JUEGO EN EJECUCION (tools/, 2026-09-19)

| Herramienta | Funcion |
|---|---|
| `tools/long_run.ps1` | Lanza/para/consulta pruebas **largas** desacopladas; silencia el juego (`audio_mute=true`), aplica overrides al `dbz3_user.toml` y lo restaura. Estado en `%TEMP%\opencode\long_run_state.json`. |
| `tools/press_key.ps1` | Inyecta teclas por `PostMessage` (`-TargetPid`; mapa W/A/S/D/Backspace/Tab/Space/Return). |
| `tools/grab_window.ps1` | Captura PNG de la ventana (`PrintWindow` PW_RENDERFULLCONTENT). Requiere la ventana **on-screen** (fuera de pantalla la presentacion se congela y las capturas salen identicas). |
| `tools/click_window.ps1` | Click por coordenadas **cliente** de la ventana (launcher ImGui). |
| `tools/hidden_run.ps1` | Igual que long_run pero moviendo la ventana fuera de pantalla (solo para medir el swap rate del guest, no para capturas). |

**Receta para llegar al combate 3D**: boot con `dbz3_skip_launcher=true` →
opening (~90-100 s) → pulsar `Return` (Start) → titulo/menu → idle ~2-2,5 min →
salta la attract demo battle 3D.

## Herramientas nuevas (2026-10-15)

- `mod center hd/pasarela/pasarela.py` (`info`, `extraer --salida DIR`, `prueba`): lee de la ISO de Budokai 1 del jugador la Leyenda de Mr. Satan (misma logica que `src/pasarela_b1.cpp`); lo extraido no va al repo.
- `mod center hd/retos_predefinidos.py`: pone o quita un reto de `campanas/retos/` (torre, supervivencia, kumite) y la torre de la semana; tambien desde el Mod Kit (Historias > Retos predefinidos).
- `tools/compilar.py`: compila el exe dual con candado compartido y copia las DLL canonicas.
- `tools/pruebas.py`: pruebas en el juego por escenarios (.toml de pasos), hasta 3 copias a la vez y una prueba por copia (candado `D:\DBZ3HD\_en_uso_<copia>`); ver `docs/14_frentes/pruebas.md`.
- `tools/eu_map.py`: mapa US->EU verificado de funciones y datos que usan los ficheros de `src/` (genera `src/eu_map_gen.h` y `docs/14_frentes/eu_mapa.txt`); `tools/eu_convertir.py`: pasa un fichero a ganchos/direcciones con pareja EU (`src/eu_map.h`). Ver `docs/14_frentes/eu.md`.
- `studies/tools/frame_data.py`: tabla de frame data estilo Budokai 4 (entradas, cuadros, dano por situacion) desde los ficheros del juego; ver `docs/14_frentes/combate.md`.
- `mod center hd/forma_unica.py`: respaldo de la capsula "Todas las transformaciones" (hoy la genera el propio juego al arrancar, `src/transformaciones.cpp`); ver `docs/14_frentes/transformaciones.md`.

## Tenkaichi 3 (PS2, copia del jugador) — estudio 2026-10-17
- El `.chd` de Retrobat es de tipo CD (MODE1): `chdman extractcd -i <chd> -o t3.cue -ob t3.bin` da una ISO 9660 valida (sectores de 2048). `extractdvd` termina sin error pero NO da una ISO (unidades de 2448 B).
- Contenido (PAL SLES_549.45): `DATA/PZS3EU1.AFS` (3645 entradas: modelos `pmdl`, mapas `MAPD`, textos `TXT` UTF-16 en 5 idiomas, animaciones), `DATA/PZS3EU2.AFS` (65201 ADX: voces por personaje, 15000 por idioma). Informe completo y scripts en `E:\DBZ3HD_trabajo\t3\` (no copiar datos del juego al proyecto).
- Importable en tiempo de ejecucion (como B1/B2/IW), por valor/esfuerzo: voces ADX (lector ya existe en gritos.py), textos de historia, texturas GS de interfaz; modelos/mapas/guiones bloqueados (formato o compresion sin identificar).

## Mod Kit: modo Experto y Montar y probar (2026-10-30)
- La pestaña «Herramientas» del Mod Kit avanzado se llama ahora **Experto** (mismas herramientas; los asistentes de la portada cubren las tareas comunes).
- `mod center hd/kit/probar.py`: «Montar y probar». Copia fija `smoke_c` vía `tools/pruebas.py correr smoke_c menu` en desarrollo; en el juego del jugador, arranque sin `DBZ3_TEST_*` y lectura de `logs/dbz3_*.log` (sin tocar partidas). `--selftest` del Kit comprueba el analizador.

## Modo PS2 del Kit (31/10)

- `mod center hd/ps2_b3.py`: mods y ISO para B3 de PS2 (`info`, `texturas`, `textura`, `personaje`, `musica`, `montar`, `prueba`).
- `mod center hd/kit/pagina_ps2.py`: su pantalla. `kit/ayudas.py`: tooltips y «¿Cómo se hace?». `kit/vistas.py`: galería, cuerpo y escuchar.
- Medido en el juego: el lector CRI sitúa cada entrada del AFS sumando tamaños, así que deben seguir contiguas; la BIOS busca la tabla de carpetas en el sector 257 y PCSX2 trata como CD una raíz de 2048 bytes.

## Arte opcional y DAE (31/10)
- `mod center hd/kit/arte.py` (motor y prueba: `python kit/arte.py`), `kit/pagina_arte.py` (pantalla). Lista de piezas en `PIEZAS`: si el juego pide una pieza `hub/` nueva, añádela ahí.
- `awo_tools/dae_lector.py` y `dae_a_b3.py`: DAE con huesos de estilo Xenoverse a traje de B3; reutilizan `xeno_a_b3`.

## Herramientas nuevas sin inventariar (1.5.6, revisión 2026-11-01)
| Herramienta | Qué hace |
|---|---|
| `awo_tools/t3_anim.py` | Pistas y esqueleto de Tenkaichi 3 PS2 (solo lectura; no instala mods). |
| `awo_tools/t3_effects.py` | Efectos originales de T3: paquete de ranura → recursos tipados y texturas RGBA (salida TCB2). No clona efectos de un donante. |
| `awo_tools/t3_retarget.py` | Conversión experimental de pistas T3 a #ACM HD con mapa de huesos explícito. No convierte efectos, cámaras ni técnicas. |
| `mod center hd/tactics_fx.py` | Textura y color de la técnica de energía de cada unidad de Dragon Tactics, leídos de `data_cmn.afs` del jugador (no hay arte nuevo). |
| `mod center hd/kit/revisar.py` | «Revisar este mod»: mira una carpeta de mod sin abrir el juego y devuelve errores (no cargará) o avisos. |
| `tools/fault_proxy.py` | Inyector de fallos UDP en loopback, solo para pruebas (no es un servicio del juego). |
