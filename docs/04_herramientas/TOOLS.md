# Herramientas — inventario

> Actualizado: 2026-10-06 (§0 Studio de cámaras y port de Shin Budokai / Heroes). Inventario de
> herramientas disponibles y su función.

---

Vídeo: `tools/grabar.py` graba un escenario de `pruebas.py` con su audio; `tools/grabar_vitrina.py` (`--dry-run`) graba cada traje girando con la cámara de prueba `DBZ3_TEST_VITRINA`.

**Equilibrio de Survivors:** `python tools/survivors_mando.py --help` — simulación sin abrir el juego. Compila un arnés temporal con Clang/lld, enlaza la lógica actual de Survivors y reutiliza el bot C10 como comparación. Los pilotos de palanca y cruceta son sintéticos: sus resultados sirven para comparar semillas iguales, no para estimar victorias de jugadores.

- `python tools/survivors_mando.py autoprueba`: zona muerta, diagonal de cruceta, latencia del piloto, repetibilidad y control negativo.
- `python tools/survivors_mando.py equilibrio --perfil mando_medio --dif 0 --reps 4 --json base.json`: referencia emparejada por mapa, personaje y semilla.
- `python tools/survivors_mando.py barrido --dif 1 --pares 1.50:1.30,1.15:0.90`: ensaya valores sin modificar el catálogo.

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

### Auditoría local de CaC y Modo Héroe (`tools/cac_heroe/`)

Desde la raíz, con Python 3.11+; cada comando admite `--help` y `--autoprueba`.

| Comando | Qué comprueba |
|---|---|
| `python tools/cac_heroe/censo_cac.py --json censo.json` | Datos, candidatos a colisión, localización y carga de piezas del AFS US. `--sin-afs` omite modelos; `--estricto` falla ante hallazgos altos o medios. |
| `python tools/cac_heroe/fuzzer_recetas_cac.py --iteraciones 200 --semilla 20261124` | Ida y vuelta de recetas y partidas; la ejecución completa necesita `g++`/Clang C++20 disponible como `g++`. |
| `python tools/cac_heroe/historial_creador.py --self-check` | Regresión del historial del creador B3 (`dbz3_cac_deshacer`, OFF de serie): 32 pasos, restauración del aspecto sin tocar el progreso, deduplicación y bifurcación. Usa C++20 (`g++` o `clang++`); `--help` muestra las opciones. No abre el juego. |
| `python tools/cac_heroe/juez_formas_cac.py --rapido --json formas.json` | Monta formas con el AFS del jugador y mide máscaras de pelo, ojos y cejas (NumPy/SciPy/Pillow). `--cejas-on` mide el candidato visual; por defecto mide OFF. Puede tardar varios minutos. |
| `python tools/cac_heroe/grafo_progreso.py --json grafo.json --mermaid rutas.mmd` | Modela rutas, premios y dependencias de los TOML; `--skip-afs` omite cápsulas y datos nativos. Un ciclo estructural no demuestra bloqueo en partida. |
| `python tools/cac_heroe/juez_coleccion_tecnicas.py --json coleccion.json` | Contrasta textos con datos y código. Los hallazgos son pistas de auditoría, no resultados de juego. |
| `python -B -X utf8 tools/cac_heroe/coleccion_heroe.py --help` | Auditoría offline de premios y modelo del candidato NHER2. `--self-check` prueba el modelo; `todo --skip-afs --json reporte.json` incluye rutas y migraciones simuladas. Requiere Python 3.11+; no abre el juego ni migra guardados reales. El «DESPUÉS» modela los cuatro parches propuestos, no la integración. |
| `python -B -X utf8 tools/cac_heroe/save100_coleccion.py --help` | Coteja tres copias locales: `antes.toml despues.toml coleccion.toml.bak`. Comprueba que solo se establece `todo = true`, que el resto de valores TOML se conserva y que el respaldo es idéntico al original. Python 3.11+; no escribe en los guardados. El escenario `python tools/pruebas.py correr sol save100_coleccion` genera evidencia pasando por el botón real del lanzador, en copia fija aislada. |
| `python -B -X utf8 tools/cac_heroe/heroe_vueltas_test.py --self-check` | Regresión offline C++/UI de `dbz3_heroe_coleccion_vueltas` (OFF de serie, AP_NHER4): enlaza `src/heroe.cpp`, `src/cac/cac.cpp` y `heroe_v2_{coleccion,mapa,habitacion}.inl` integrados con `clang++` (C++20) y ejecuta 8 escenarios C++ (338 controles: OFF por defecto, protección de `coleccion.toml` corrupto, claves extra + `.bak`, colección compartida y consumidores, 4 vueltas/3 finales + rollback, +500 HP DU y espejos, paridad 1:1 de pestañas 0 y 3) y 38 controles de UI. Admite `--help`, `--cxx` y `--json`; no abre el juego ni toca guardados reales. |

El ajuste `dbz3_cac_cejas_ssj3 = false` (por defecto) afecta solo a la vista previa de formas del creador. `true` permite comparar la corrección candidata; no cambia el modelo que se guarda o se usa en combate.

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

### Infinite World: censo y cotejo offline (HIWB)

Ambas herramientas ofrecen `--help`.

```powershell
python -X utf8 tools/iw_censo.py --md censo.md --json censo.json
python -X utf8 tools/iw_cotejo.py --listar
python -X utf8 tools/iw_cotejo.py 7DE --json cell.json
python -X utf8 tools/iw_censo.py --autoprueba
python -X utf8 tools/iw_cotejo.py --autoprueba
```

- El censo lee los booleanos `dbz3_iw_*` declarados en `src/` y las filas de `#DMD 1093`. La ruta por defecto es un catálogo revisado, no una medición en partida. `--raiz` selecciona el árbol; `--afs` permite indicar otro `DATA_CMN.AFS`.
- Los cotejos focales usan el ELF PS2 con entradas de laboratorio y modelos de ramas C++; `--todos` ejecuta los bancos automáticos. `--elf-eu` y `--elf-us` seleccionan los ELF para botones, vuelo y disparo; 7DD usa además `--wcf` y `--rayo`. MAPA/ROAD comprueban el espejo, no una partida. El JSON registra el alcance y cada resultado.
- Para capturas: `7D3 --ladrillos DIR_ORIG DIR_PORT` llama a Astra (`medida.json` y `visor.csv`); `CURSO --gs ORACULO VOLCADO` compara solo GS/VU1. `CURSO --trazas A.jsonl B.jsonl` exige un objeto por línea con `i` entero único, al menos un campo `in_*` idéntico y al menos una salida. Campos y cuadros deben coincidir en ambos ficheros; quien prepara las trazas debe emparejar curso, región y fase.
- Códigos de `iw_cotejo.py`: `0` coincide en el alcance probado, `1` difiere, `2` faltan datos o hay uso inválido, `3` error de ejecución. Una coincidencia focal no equivale a paridad del curso.
- Stop Cell (`7DE`): `dbz3_iw_7de_pausa_reloj_original` está activada de serie. Con el reloj continuo, pausa la cuenta también si se usa la mira de respaldo; `false` recupera el descuento anterior en esa rama. El banco `7DE` sigue mostrando las diferencias de mira y de fases: su modelo de reloj no lee el HUD del juego.

### Infinite World: pulsación, palancas y cierres de 7D5/7DD (NIWB)

`python -B -X utf8 tools/iw_niwb/cotejo.py --help` muestra los modos. Ejemplos: `--listar`, `--autoprueba`, `7D5 --foco rango --ajuste on`, `7DD --ajuste todos` y `--legado --help` (delega en `iw_cotejo.py`). Ejecuta el ELF PAL `SLES_553.47` del banco HIWB (`--elf-eu`) y compila fragmentos del árbol actual con `g++` o un compilador C++17 compatible (`--cxx`). `--json` guarda los pares en un archivo nuevo. Códigos: 0 coincide, 1 difiere, 2 datos/uso inválidos, 3 error de ejecución.

- `dbz3_iw_7d5_rango_pal`: aplica los intervalos PAL al rango de respaldo de 7D5. Apagada de serie; las 19 entradas de laboratorio conservan la salida anterior con el ajuste apagado.
- `dbz3_iw_7dd_captura_derrota`: da la derrota si Piccolo alcanza al jugador en la ruta de respaldo. Apagada de serie hasta verificar en juego la confirmación del resultado; el detector sigue usando distancia horizontal.
- El cotejo se limita al flanco de pulsación (sin Start/Select), la lectura de palancas de cámara con Y normal, el cálculo de rango y el resultado una vez decidida la captura. No valida trayectorias 3D, la cámara renderizada ni partidas completas. `--ajuste serie` usa los valores declarados en C++; `on`/`off` los fuerzan solo en las sondas.

### Infinite World: inventario de recursos visuales (NIWC)

`python -B -X utf8 tools/iw_niwc/iw_activos.py --help` enumera las opciones. `censo 0x7DE --idioma ENG` inventaría un curso; sin curso se revisan los 18 minijuegos, MAPA y ROAD. `original` comprueba la apertura del banco de efectos y la selección de filas de ayuda con el ELF PAL `SLES_553.47`, y lista las referencias en los AME. `--pal` selecciona la copia extraída (`USR/` y el ELF); la copia USA se busca bajo `ps2_games/`. `--salida` guarda el JSON en un archivo nuevo y `--autoprueba` comprueba el lector sin los discos.

- `dbz3_iw_disparo_texturas_directas` recupera t35 y t56 del atlas de partículas de la ruta C++ de disparo. Está apagada de serie. El inventario acredita recursos y anclas de carga, no cuándo se dibujan los efectos ni su aspecto final.

### Infinite World: vuelta de Dragon Mission (NIWA)

`python -B tools/iw_niwa/niwa.py --help` lista `autoprueba`, `original`, `log` y `mapa-log`. Usar desde la raíz del proyecto o pasar `--repo` antes del modo. El banco requiere el ELF PAL `SLES_553.47` y su `DATA_CMN.AFS` (entradas 1093 y 1127); los modos de cotejo y `original` admiten `--afs`. El banco sustituye servicios de presentación e inyecta el desenlace del combate.

- `log RUTA --dificultad N --rango N`: coteja una vuelta de la misión 11. Usa los parámetros observados del port para el premio; `mapa-log RUTA` repite el mando de los cuadros libres y coteja la telemetría disponible. Un resultado `0` coincide en los campos cubiertos, `1` indica diferencias, `2` indica falta de datos y `3`, un error de ejecución. Los segundos del log no son un conteo de cuadros.
- `DBZ3_TEST_IW_FLUJO=1` añade reloj y fila del globo a la traza por cuadro y registra el cierre del panel de resultado. Usar en pruebas cortas.
- `dbz3_iw_reloj_vuelta_original` separa las dos esperas del mapa tras ganar un combate de Dragon Mission; se activa de serie (`false` recupera el reloj compartido). Desde AP_NIWJ, `dbz3_iw_vuelta_tras_resultado` también se activa de serie: aplaza el inicio de la vuelta hasta cerrar el resultado de combate.

### Infinite World: censo y cotejos en un solo comando (NIWD)

`python -B -X utf8 tools/iw.py --help` ofrece `censo`, `activos`, `cotejo` y `autoprueba`. Usar desde la raíz del proyecto, o indicar `--repo` antes del subcomando. La autoprueba no necesita los discos ni un compilador.

- `censo --con-activos --idioma SPN --idioma ENG` enlaza cursos, bancos y recursos USA/PAL; `activos 7DE` filtra un curso y `activos --original` coteja el selector PAL y las referencias AME.
- `cotejo limites [todos|intro|low|rango-7d7] [--ajuste serie|on|off|todos]` compara fragmentos del C++ actual con el original. Requiere `g++` (o `--cxx`) y los ELF locales (`--elf-pal`, `--elf-usa`). `serie` lee el valor declarado en C++; `todos` incluye los modos apagados. Salida: 0 coincide en el alcance cubierto, 1 difiere, 2 faltan datos/uso, 3 error. `censo`, `activos` y `cotejo limites` aceptan `--json` con una ruta nueva. `cotejo mapa`, `focos` y `legado` conservan la ayuda y opciones de sus herramientas respectivas.
- `dbz3_iw_mapa_intro_limite_original` espera hasta superar 1,0 en la entrada y `dbz3_iw_7d7_siete_aciertos_original` exige siete aciertos para dar rango también con el reloj antiguo. Ambos activos de serie; `false` recupera el comportamiento anterior. Desde AP_NIWG, `dbz3_iw_7de_campo_low_original` también está activo de serie: espera hasta el campo 221 con la mira de respaldo; `false` recupera 220 con esa mira. El ajuste de pausa del reloj no cambia. Los bancos miden las transiciones y el índice de rango, no la presentación completa.

### Infinite World: comprobar la espera de Stop Cell (AP_NIWG)

- `python -B -X utf8 tools/iw_7de_log.py --help` muestra el auditor de un `dbz3.log` archivado. Ejemplo: `python -B -X utf8 tools/iw_7de_log.py "RUTA\dbz3.log" --semilla 603F0359`. Sin opciones comprueba una sola transición a Aim en el campo 221 con mira de respaldo. `--campos` y `--mira` cambian lo esperado; `--semilla` exige que coincidan la semilla fijada y la efectiva. Emite JSON y devuelve 0 si coincide, 1 si discrepa, 2 si faltan datos o el uso no es válido, 3 si no puede leer o interpretar el registro. No abre el juego ni requiere el disco.
- En el arnés, `DBZ3_TEST_IWMINI=0x7DE` admite `DBZ3_TEST_IW_7DE_SEMILLA_HEX` sin `,auto`. La semilla debe ser impar y tener ocho dígitos hexadecimales. El ajuste no interviene en una partida normal.

### Infinite World: entrada única, autopruebas y tabla de cvars OFF (NIWH)

- `python -B -X utf8 tools/iw.py --help` reúne `censo`, `activos`, los bancos de `cotejo`, `log-7de`, `campana`, `ladrillos`, `comparar`, `comparar-trad`, `cierre-r23`, `inventario`, `cvars` y `autoprueba`. Los delegados opcionales `niwe`, `ventanas` (`ventanas_iw.py`) y `validar` (`iw_validar.py`) prefieren `tools/` y, en su defecto, buscan los lotes NIWE/NIWF/NIWG bajo `--trabajo` (antes del subcomando; por defecto `DBZ3_TRABAJO`). Si faltan, su invocación devuelve 2.
- `python -B -X utf8 tools/iw.py --self-check` (alias `--autoprueba` y subcomando `autoprueba`) ejecuta controles sin discos, ELF, compilador ni juego. Con `autoprueba --exigir-todas` falla si falta alguna de las herramientas con autoprueba registrada, incluidas las opcionales. `iw_censo.py`, `iw_cotejo.py`, `iw_niwa/niwa.py`, `iw_niwb/cotejo.py`, `iw_niwc/iw_activos.py` e `iw_7de_log.py` también aceptan ambos flags y `--help`. El subcomando `iw_niwa/niwa.py autoprueba` conserva su banco con ELF PAL.
- `python -B -X utf8 tools/iw.py inventario --comprobar-help --comprobar-autoprueba` comprueba presencia, menciones en `TOOLS.md`, ayuda y controles registrados. La entrada de `iw.py` comprueba sus atajos en modo base para evitar recursión; el agregado se ejecuta aparte con `autoprueba`. `astra_iw_auditoria.py` solo figura en el inventario: no tiene CLI de ayuda y ejecutarla escribe resultados en una ruta fija.
- `python -B -X utf8 tools/iw.py cvars --solo-off` lista los booleanos de IW apagados de serie; `--ambito todos` incluye auxiliares. `--json` y `--md` (también en `inventario`) escriben archivos nuevos. El conteo de menciones de `REXCVAR_GET` es textual: no prueba uso en el ejecutable ni en partida. AP_NIWH no cambia ningún valor de serie.

### Infinite World: persecución de Piccolo, 7DD / mapa 906 (AP_NIWI)

- `python -B -X utf8 tools/iw_7dd.py --help` muestra el comparador de registros, capturas PAL y banco offline. Para resumir una sola partida: `python -B -X utf8 tools/iw_7dd.py log --log "RUTA\dbz3.log"`.
- Para comparar la base con el eje de cámara real, usa `python -B -X utf8 tools/iw_7dd.py banco --log "RUTA\dbz3.log" --ticks N --variante base --variante ejes-log`. Usa registros contiguos desde `t=1`; `ejes-log` exige `cam7dd` en cada registro. `--self-check` (alias `--autoprueba`) reproduce NIWG-01 y su control negativo.
- `original`, `banco` y la autoprueba dependen del corpus local de `E:\DBZ3HD_trabajo` que usa `tools/iw_mips/persecucion.py`; el banco usa además el ejecutable de `rayo7dd.cpp` con el WCF. La distancia entre anclas del banco no certifica captura ni cierre. La corrección de la embestida solo opera con `dbz3_iw_7dd_original`; ese ajuste y `dbz3_iw_7dd_captura_original` siguen apagados de serie.

### Infinite World: comparar capturas OFF/ON (AP_NIWJ)

- `python -B -X utf8 tools/iw_evidencia.py --help` muestra el comparador offline de NIWJ adaptado a dos PNG de igual tamaño. Ejemplo: `python -B -X utf8 tools/iw_evidencia.py "off.png" "on.png" --roi 0.24 0.72 0.76 0.90 --lamina "comparativa.png"`. Requiere Pillow; `--self-check` ejecuta controles sintéticos.
- Devuelve error medio por canal y porcentaje de píxeles con algún canal que difiera en más de 12, globalmente y dentro de la región indicada. La lámina es opcional y no sobrescribe archivos. La sincronización y la fidelidad al original se comprueban por separado. Los escenarios históricos de NIWJ se conservan en su entrega; el comparador no lanza partidas.

### Infinite World: oráculos ELF EU/USA sin emulador (AP_NIWL)

- `python -B -X utf8 tools/iw_oraculos_elf.py --help` muestra el banco offline de cuatro oráculos contra los ELF originales EU (`SLES_553.47`) y USA (`SLUS_218.42`) reutilizando `tools/iw_mips/ee.py` y `tools/iw_mips/disparo_banco.py`, sin abrir el juego ni PCSX2.
- Comprueba prioridades `Task_Create` y retardo del objeto 8 en vuelo (`33` PAL / `39` NTSC), cierre por hito `#ITM+0x30` de `s_SPACE` en `0x7D0`/`0x7D9`, `Char_ComputeStatGrade` (Fight `39 -> 0` / `40 -> 1` y notas `1..4`), y `RangoDisparo` en `0x7D7`/`0x7D8`/`0x7DE` (303 casos). `--self-check` ejecuta controles rápidos y `--guardar RUTA.json` guarda el resultado. El auditor ligado al lote NIWL (`iw_cierre13.py`) permanece en `E:\DBZ3HD_trabajo\fabrica\NIWL\scripts\`.

## Tablero B2: cotejo de carga y guiones dirigidos (HB2B)

- `python -B tools/b2_pm23_carga.py --salida "RUTA NUEVA" [--slot-pal] [--sstates DIR] [--src CPP] [--cxx CLANG]`: compara las fichas iniciales de las nueve fases con el ELF PAL. Requiere los estados `fase1.p2s`…`fase9.p2s` y Clang con C++17. Compila fragmentos de `pasarela_b2.cpp` y deja los pares, las diferencias y el resultado de `b2_juez_bateria.py`. `--slot-pal` prueba la opción `dbz3_b2_pm23_slot_inicial_pal` (apagada de serie). Código 1: diferencias; 2: faltan datos (esperable aun si toda la carga coincide); 3: error de ejecución. No mide órdenes, relojes, objetos ni el cierre.
- `python -B tools/b2_escenario.py pm23 --fase 7 --salida "RUTA NUEVA.toml" --preflight`: prepara un guion de carga sin lanzar el juego. Para ejecutarlo en copia fija: `python tools/pruebas.py correr smoke_c "RUTA NUEVA.toml"`. Los modos `radar`, `cierre11` y `shenron` son recetas de diagnóstico; fuerzan condiciones y no acreditan recogida, cierre natural ni paridad visual. `radar` y `cierre11` se diseñaron para parches que no se integraron.
- `python -B tools/b2_diag_recursos.py --iso "B2 PAL.iso" --estado "fase1.p2s" --salida "RUTA NUEVA.json"`: coteja el ELF, localiza el radar en los nueve tableros e inventaría recursos del cielo y la luz de Shenron. No prueba su aspecto en pantalla.

Las tres herramientas ofrecen `--help`.

### Tablero B2: selección de capas PAL (NB2A)

- `python -B tools/b2_nb2a/censo.py --help` lista las entradas; `--autoprueba` ejecuta seis controles sin datos del juego. El censo usa una ISO PAL propia (SLES-51839), un estado `fase1.p2s` del oráculo y el listado `b2.c` de Ghidra; indicar sus rutas con `--iso`, `--estado` y `--ghidra` si no están en las rutas locales de laboratorio.
- `--fase 7 --loc DATA_SP.AFS --json "NUEVO.json" --md "NUEVO.md" --cotejo "CARPETA NUEVA"` limita el censo a la fase VII y compara los selectores del C++ con las solicitudes PAL; sin `--fase` recorre las nueve fases. El cotejo es opcional y requiere Clang C++23 (`--clang`). Código 0: informe generado, aunque haya diferencias; código 2: entradas inválidas o censo incompleto. No abre el juego ni valida píxeles, el ciclo completo de animación, o el orden de dibujo global.
- `dbz3_b2_capas_tablero_pal` queda desactivada de serie. Si se activa, usa las capas del CMN 397/398 para casillas sin condición de los tipos 0/2/3/4 y para las Bolas en el suelo. Si no hay una textura necesaria, conserva el dibujo anterior para todo el conjunto.

### Tablero B2: avance y fundidos PAL (NB2B)

- `python -B tools/b2_nb2b/b2_anim.py --help` lista las opciones; `--self-check` prueba el comparador sin archivos del juego. Ejemplo: `python -B tools/b2_nb2b/b2_anim.py movimiento aliados --estado "fase1.p2s" --iso "B2 PAL.iso" --salida "CARPETA NUEVA" --paso-4px --color-ama-pal`.
- Requiere el estado del oráculo PAL SLES-51839, Clang con C++20 y el entorno local de `b2_oraculo`. La ISO propia se requiere para los bancos de aliados, radar, viñetas y Shenron. Compara funciones del ELF PAL con un arnés compilado de fragmentos del port; permite otro C++ con `--src`. Sin focos explícitos mide los ocho. Guarda las muestras, tolerancias y divergencias en `cotejo.json`. Código 0: sin diferencias ni pendientes declarados; 1: diferencias; 2: datos pendientes sin divergencias medidas; 3: error del banco. Los argumentos inválidos también devuelven 2 (`argparse`). No abre el juego ni verifica la composición en pantalla.
- `dbz3_b2_paso_4px` y `dbz3_b2_color_ama_pal` están **desactivadas de serie**. Activadas, aplican el avance de 4 unidades por cuadro a recorridos sin contacto ni cambio de hueco y la curva PAL al color flotante de la lista B de `#AMA`, respectivamente. Las opciones de la herramienta solo conmutan su arnés, no ajustan el juego.

### Tablero B2: cotejo de eventos (NB2C)

- `python -B tools/b2_nb2c/b2_eventos.py --help` lista los modos; `listar` enumera los 18 casos y `--autoprueba` comprueba el comparador sin datos del juego. `pal --estado "fase9.p2s" --salida "PAL.json"` usa el ELF PAL SLES-51839 del oráculo local y ese estado; `--caso popo/5-a-24` selecciona solo ese caso.
- `cpp "PAL.json" --ajuste serie --salida "port.json"` compila fragmentos del C++ actual con Clang C++20 (`--compilador`). `--ajuste on|off` solo cambia la sonda; `--trabajo` elige dónde conservarla. `juzgar "PAL.json" "port.json"` da 0: coincide, 1: difiere, 2: faltan datos comparables, 3: error. `pm23` delega en el juez histórico de volcados de estado.
- `dbz3_b2_popo_origen_pal` está **activada de serie**: al aterrizar, el destino pasa a ser también el origen de la ficha. `false` recupera el comportamiento anterior. El cotejo cubre la ruta 5→24 sin ocupantes; no valida el reparto de huecos con ocupantes ni la equivalencia visual o de una partida completa.

### Tablero B2: batería offline unificada (NB2D)

- `python -B tools/b2_nb2d/b2.py --help` lista `recursos`, `animacion`, `eventos`, `radar`, `popo`, `bateria`, `casos` y `pm23`. `--self-check` ejecuta los controles sin datos del juego. Ejemplo: `python -B tools/b2_nb2d/b2.py bateria --banco radar --banco popo --estados "RUTA A LOS ESTADOS" --salida "CARPETA NUEVA"`. Sin `--salida` usa una carpeta temporal nueva.
- Requiere Python 3.11+, las herramientas NB2A/B/C instaladas, Clang (`--cxx`) y el oráculo PAL SLES-51839 con `fase1.p2s` y `fase9.p2s` según el banco. Para recursos y parte de animación se requiere la ISO propia (`--iso`); para recursos también el decompilado local (`--ghidra`). Los informes JSON conservan el alcance de cada banco. Códigos: 0 = coincide en lo medido, 1 = diferencias, 2 = faltan datos comparables, 3 = error de ejecución. La batería completa aún contiene diferencias y pendientes. No abre el juego.
- `radar` y `popo` leen el C++ integrado y juzgan ON; OFF es el control. `dbz3_b2_radar_indicadores_pal` limita flechas y puntos a la ficha del jugador, conservándolos durante la espera inicial si esa ficha sigue activa. `dbz3_b2_popo_hueco_pal` asigna el hueco de llegada sin recolocar a las demás fichas si el destino es válido. Ambos ajustes están **desactivados de serie**: los bancos verifican solicitudes de sprites y huecos en casos dirigidos, sin validar el cambio de turno en pantalla ni un aterrizaje con aliados en partida.

### Tablero B2: perfil conjunto y diagnóstico (NB2E)

- `python -B tools/b2_nb2e.py --help` lista `diagnostico` y `capturas`. `--self-check` comprueba las láminas con imágenes de prueba (Pillow 9.1+). `diagnostico --salida "CARPETA NUEVA"` repite los bancos de NB2D, el cotejo de fichas iniciales y el juez de registros históricos. Requiere los datos de esos bancos y los archivos AP_HB2B/HB2 bajo `--trabajo` (por defecto `E:\DBZ3HD_trabajo`). Códigos: 0 = sin diferencias ni pendientes en los bancos, 1 = diferencias, 2 = pendientes, 3 = error de ejecución.
- `capturas --manifiesto "PANELES.json" --salida "CARPETA NUEVA"` genera láminas sin abrir el juego (0 si se generan aunque falten tomas). Cada caso lleva `id`, `titulo`, `limite`, `capturas` (etiquetas a rutas relativas al JSON, `repo:`, `trabajo:` o `null`) y, opcionalmente, `sha256` por etiqueta. Sin manifiesto usa el de `fabrica/NB2E`. Ni las láminas ni el diagnóstico certifican paridad visual en juego.
- `dbz3_b2_presentacion_pal` está **desactivada de serie**. `true` activa a la vez las capas originales, el avance de 4 unidades y el fundido de color de `#AMA` lista B. Con `false` siguen mandando las tres opciones individuales; el perfil no selecciona el hueco inicial de PM-23, el filtro de indicadores del radar ni el hueco de Popo.
- El banco de capas coteja también el perfil encendido con la opción individual apagada. En `b2_nb2b/b2_anim.py`, usa `--presentacion-pal` **en vez de** `--paso-4px --color-ama-pal` para medir el avance y los fundidos con solo el perfil encendido en el arnés.

### Tablero B2: música de combate y tarjeta «Battle!» (NB2F)

- `python -B -X utf8 tools/b2_nb2f/b2_batalla.py --help` lista los modos. `--self-check` prueba el comparador sin datos del juego. `censo` inventaría casillas, recursos y pistas PAL. Usa tu ISO PAL (`--iso`), el ELF local SLES-51839 y el decompilado (`--ghidra`); `--fase` selecciona fases. No abre el juego.
- `cotejo --base "E:\DBZ3HD_trabajo\respaldo_integracion_AP_NB2F"` compara los fragmentos de C++ integrados con las fuentes previas (respaldo con `src/`). Requiere también Clang C++20 (`--cxx`), las herramientas NB2A/B/C y el estado PAL de fase I (`--estado`). Define `--salida "CARPETA NUEVA"` antes del subcomando para guardar el JSON y las sondas ahí; si se omite se usa una carpeta temporal nueva.
- `juzgar "PAL.json" "PORT.json"` compara trazas `nb2f-traza-v1` por ID, entrada y campo. En `cotejo` y `juzgar`, 0 indica coincidencia focal; 1, diferencias; 2, solo datos pendientes; 3, error de ejecución. El cotejo integrado da 1 por las diferencias del prototipo de música traducida, que no se integró porque esa ruta está deshabilitada. El banco no mide la mezcla ni compone la tarjeta en pantalla.
- `dbz3_b2_tarjeta_cond_pal=true` activa el rebote original del texto de condición en la tarjeta nativa si hay claves de `CMN 410`; está **apagada de serie** y es independiente de `dbz3_b2_presentacion_pal`. El mod de música de la ruta nativa `b2.musica_combate` sigue como antes.

### Tablero B2: cierre 11 y aparición de Shenron (NB2G)

- `python -B -X utf8 tools/b2_cierre11.py --help` lista los modos. `--self-check` prueba el generador y el juez sin datos del juego. `ruta --salida "RUTA NUEVA.toml" --preflight` genera el salto dirigido a cierre 11 a velocidad normal, sin arrancar el juego. `--ps2-games` indica la carpeta de tu copia de B2 PAL y `--alfa-pal` activa la transparencia de la luz en el guion.
- `python -B -X utf8 tools/b2_cierre11.py juzgar --log "dbz3.log" --exigir-alfa-pal` comprueba la transición, la carga de cinco texturas con alfa PAL, la firma AMP, el arranque del sonido y el marcador UV. Quita `--exigir-alfa-pal` para guiones sin el ajuste. Códigos: 0 = hitos confirmados; 1 = divergencia; 2 = faltan hitos; 3 = error de lectura/escritura. El salto dirigido no acredita la ejecución del script 303 ni la equivalencia de imagen o mezcla con PS2.
- `dbz3_b2_shenron_luz_alfa_pal` está **apagada de serie**. Al iniciar el juego con `true`, la columna de luz usa las transparencias de la paleta de `CMN 328/12`. El ajuste se lee al cargar la escena por primera vez.

### Tablero B2: entrada única, autopruebas y tabla de cvars OFF (NB2H)

- `python -B -X utf8 tools/b2.py --help` reúne 13 herramientas instaladas de HB2B, NB2A–G y base, sin abrir el juego ni PCSX2. Usa `tools/b2.py <subcomando> --help` para ver las opciones de cada una. Los subcomandos delegados se ejecutan desde la raíz del proyecto; usa rutas absolutas para archivos externos.
- `python -B -X utf8 tools/b2.py --self-check` (alias `--autoprueba`) ejecuta los controles propios y 11 autopruebas sin ISO, ELF, compilador ni juego. El subcomando `autoprueba` entrega el mismo resultado como JSON. Las nueve herramientas de HB2B y NB2A–F aceptan ambos nombres; `tools/b2_juez.py --help` también termina con código 0.
- `python -B -X utf8 tools/b2.py inventario --comprobar-help --comprobar-autoprueba` muestra presencia en `tools/`, referencias en esta guía y los resultados individuales. El 0 del inventario indica que pudo listar todas las herramientas, no que pasaron sus comprobaciones. La batería sin datos deja fuera a `juez-bateria` (sin autoprueba propia) y `oraculo` (requiere datos locales y fuentes revisadas). Si el oráculo rechaza la huella de `inicio_piezas`, revisa su modelo antes de actualizarla.
- `python -B -X utf8 tools/b2.py cvars --solo-off` lee los valores de serie de `src/` y cuenta llamadas literales a `REXCVAR_GET` (sin preprocesar ni medir ejecución). Las notas de prueba son evidencia histórica de los lotes indicados. Los nueve ajustes experimentales de la ronda siguen apagados mientras falta el cotejo visual; `dbz3_b2_popo_origen_pal` conserva la activación validada en AP_NB2C.

### Tablero B2: premios en banco y atribución del cierre (NB2I)

- `python -B -X utf8 tools/b2_nb2i/nb2i.py --help` lista `medir`, `rutas` y `cierre`. `--self-check` prueba el comparador y la atribución del cierre sin lanzar el juego. `sondas.py` es su módulo interno.
- `medir --estado "fase9.p2s" --salida "CARPETA NUEVA"` coteja el ELF PAL SLES-51839 con fragmentos del C++ actual; requiere el oráculo local de NB2C y Clang C++20 (`--compilador`). `--caso premio/8` limita el banco a ese caso. Las 15 coincidencias nuevas cubren tres fixtures de premios; no prueban su aspecto, distribución aleatoria ni persistencia real. El cotejo completo conserva nueve pendientes (código 2). Códigos: 0 = OK en el alcance, 1 = divergencia, 2 = datos pendientes, 3 = error.
- `rutas --snapshot "progreso_fase9.toml" --salida "CARPETA NUEVA"` genera guiones y ejecuta `preflight` sin abrir el juego. Usa un progreso de prueba compatible de fase IX y, si es necesario, `--ps2-games` para indicar la carpeta del disco PAL. `cierre --log "dbz3.log" --exigir-alfa-pal --salida "cierre.json"` comprueba la aparición y clasifica la atribución por separado; un código 0 no basta para acreditar la orden 42. `orden_42_observada` exige despacho, volcado de cierre y escena en ese orden; si el script 303 se inyectó, no acredita un cierre natural.
- El volcado optativo `DBZ3_TEST_B2_STATE` añade `premio_pendiente` (0 = ninguno) y `vida_b2` por pieza (-1 = sin override). Son valores internos del port, no el stock ni una equivalencia con la vida PAL.

### Tablero B2: combate, Dende PAL y diálogo en banco (NB2J)

- `python -B -X utf8 tools/b2_nb2j/nb2j.py --help` lista `medir`, `relevo` y `juzgar-dende`. `--self-check` (alias `--autoprueba`) comprueba el comparador, el juez de Dende y las herramientas reutilizadas sin abrir el juego. `pal_nb2j.py` y `sonda.cpp` son sus módulos internos.
- `medir --estado "fase9.p2s" --salida "CARPETA NUEVA"` coteja los nueve pendientes de NB2I (combate, Dende 4/5 barras y diálogo T6/301) con el ELF PAL SLES-51839 y fragmentos del `src/pasarela_b2.cpp` integrado; requiere el oráculo local de NB2C y Clang C++20 (`--compilador`). `--grupo combate|dende|dialogo` limita el banco; `--base "pasarela_b2_previo.cpp"` comprueba además que OFF coincide con la versión anterior.
- `relevo --salida "CARPETA NUEVA" [--ps2-games DIR] [--log "dbz3.log"]` genera el guion breve de Dende (`NB2J_dende_efecto.toml`) con `preflight` y, si se pasa `--log`, vuelve a juzgar la atribución de cierre 11. `juzgar-dende --log "dbz3.log"` exige exactamente una línea `Dende postefecto: pieza N barras 5 vida_b2 500 banda 0` (códigos: 0 = igual, 1 = diferencia, 2 = faltan datos, 3 = error).
- `dbz3_b2_dende_vida_pal` está **apagada de serie**. Activada, `CurarDende` suma una barra (tope 5), conserva la vida de combate (`vida` / `vida_b2`) y recalcula la banda (`+0x1F`), serializada opcionalmente en el progreso de fase y consumida en `ComponerVidaKi` para el rival. No modifica la regla de serie mientras siga en `false`; las dos divergencias de diálogo (`abre.termina` y `abierto`) quedan documentadas sin parche.

### Infinite World y Budokai 2: comparación de peticiones de audio (AP_NAUD)

- `python -B -X utf8 tools/naud_audio.py --help` muestra las opciones; `--self-check` prueba el juez sin abrir el juego ni leer sus discos.
- `python -B -X utf8 tools/naud_audio.py comparar "original.json" "port.json" --salida "cotejo.json"` alinea trazas `naud-audio-v1` por `escena`, `evento` y `vez`. Cada evento lleva `bus`, `recurso`, `ms` desde un ancla común, `ganancia`, `unidad` y `prueba`. Escribe en un archivo nuevo; sin `--salida`, imprime el cotejo completo. Devuelve 0 si coinciden todas las peticiones, 1 si hay diferencias o eventos de más/de menos, 2 si faltan datos o no hay eventos, y 3 si hay error. Los niveles sin datos o en unidades distintas no se dan por iguales.
- El banco de ELF/C++ y el montaje de escucha requieren datos locales y siguen en `E:\DBZ3HD_trabajo\fabrica\NAUD`. El juez compara peticiones registradas; no mide mezcla, latencia ni reproducción efectiva. Los ajustes `dbz3_iw_disparo_voces_poses_original`, `dbz3_b2_audio_fundido` y `dbz3_b2_dende_audio_original` quedan apagados de serie; los dos cambios de B2 se midieron con datos PAL.

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

## Mod Kit rehecho (KIT-04 a KIT-09, 2026-11-12)

Guía para jugadores: [KIT_GUIA_RAPIDA.md](../02_mods/KIT_GUIA_RAPIDA.md). Registro y límites: `studies 5/ENTREGAS/KIT_REGISTRO.md`. Pruebas del Kit: `studies 5/ENTREGAS/KIT_PRUEBAS.md`.

| Herramienta | Qué hace |
|---|---|
| `mod center hd/escenarios_convertir.py` | Conversor de escenarios de Budokai 1/2 de la ISO del jugador a escenarios del selector (`comprobar` lo valida contra los nativos). `tools/escenarios_convertir.py` es solo un lanzador hacia este. |
| `mod center hd/kit/base.py` | Base común del Kit: rutas, búsqueda del juego y de `us/` (con validación del `data_cmn`), LZX `/N:2048`, índice AFS. Prueba: `kit/prueba_base.py`. |
| `mod center hd/kit/nucleo.py` | Núcleo de la ventana (idioma, entorno, trabajos, widgets, `Page`). Las pantallas viven en `kit/paginas/` (una por fichero). |
| `mod center hd/kit/portada.py` | Portada del modo fácil: 6 tarjetas con render del juego del jugador (caché del Kit) y accesos. |
| `mod center hd/kit/pelicula.py` | Película de fotogramas de una animación con el modelo real, con caché por clave. Prueba: `kit/prueba_pelicula.py`. |
| `mod center hd/biblioteca_anim.py` | Biblioteca de clips y recetas (`listar`, `resumen`, `receta-guardar`, `aplicar`, `selftest`); `aplicar_receta` escribe solo en la carpeta del mod. |
| `mod center hd/kit/generador.py` | Lógica del generador de animaciones (nombres oficiales de cápsula, recetas, tramos y mezcla). Prueba: `kit/prueba_generador.py`. |
| `mod center hd/kit/traje.py` | «Cambiar traje»: formas reales, un modelo por forma, validador antes de escribir. Prueba: `kit/prueba_traje.py`. |
| `mod center hd/kit/escenarios.py` + `kit/escenarios_estado.toml` | «Nuevo escenario»: llama al CLI del conversor; el `.toml` dice qué escenarios están probados o aún no funcionan. Prueba: `kit/prueba_escenario.py`. |
| `mod center hd/kit/fusiones.py` | Duración de las fusiones (`dbz3_fusion_duracion` en `dbz3_user.toml`, con copia y «Deshacer»). Prueba: `kit/prueba_fusiones.py`. |
| `mod center hd/kit/probar.py` | «Probar en el juego»: lee el registro y resume (`resumen()` cuenta trajes, escenarios y personajes). |
| `mod center hd/tools_manifest_check.py` | Valida `tools_manifest.json` (solo metadatos y rutas). |

## Infraestructura común (carril INFRA, 2026-11-11)

Detalle y cifras: `studies 5/ENTREGAS/INFORME_INFRA.md`. Notas por paquete: `E:\DBZ3HD_trabajo\infra\INFRA-0x\NOTA.md`.

| Herramienta | Para qué | Uso |
|---|---|---|
| `tools/infra.py` | Biblioteca común: rutas (`RAIZ`, `TRABAJO`, `JUEGOS`, `COORD`, `PAPELERA`, `tmp()`), `cronometro`, `turno`, `ejecutar`, `mover_a_papelera`, `u16/u32/f32/be32…` | `from infra import cronometro, TRABAJO` · `python tools/infra.py tiempos [--desde AAAA-MM-DD]` (tabla de tiempos por flujo) · `--autoprueba` |
| `tools/arranque_lento.py` | Mide el arranque del juego en los `dbz3.log` de `D:\DBZ3HD\_pruebas` sin lanzar nada: crear D3D12, presentación, lanzar el módulo y cuántos juegos arrancaban a la vez; tabla de los lentos. `python tools/arranque_lento.py [carpeta] [--desde AAAAMMDD]` (POST3, 12/11). |
| `tools/chequeo.py` | Portero: sintaxis y nombres sin definir de las herramientas compartidas. Pásalo ANTES de guardar un cambio en `pruebas.py`, `compilar.py`, `tanda.py`, `pcsx2_pool.py`, `roster_build.py`, `importar.py` | `python tools/chequeo.py [archivos]` (1-2 s; código ≠ 0 si falla) |
| `tools/escenario_nuevo.py` | Crear y validar un escenario de `pruebas.py` sin abrir el juego | `crear <nombre> [--desde <existente>]` · `validar <nombre o ruta>` (errores con línea) · `lista` (órdenes del guion) |
| `tools/oc.py` | Puerta única para encargos en opencode | `lanzar <encargo.md> --modelo sol|ds|<prov/modelo> [--nombre X]` · `estado` |

Reglas nuevas:
- Código nuevo: rutas y temporales desde `infra.py` (nada de `D:\tmp` ni rutas a mano); operaciones largas dentro de `with cronometro("flujo", detalle=…)`.
- Los tiempos reales de cada flujo quedan en `E:\DBZ3HD_trabajo\infra\tiempos.jsonl`.
- Interruptores para volver al comportamiento anterior: `DBZ3_PRUEBAS_ARRANQUE_LENTO=1` (arranque sin acelerar), `DBZ3_PRUEBAS_REINTENTO=0` (sin reintento por fallo del banco), `DBZ3_PRUEBAS_PREPARAR_LENTO=1` (preparar la copia sin atajos), `DBZ3_ROSTER_SIN_CACHE=1` (`roster_build` sin atajos).
- `compilar.py`: si falla, la última línea dice el archivo y su dueño (`DBZ3_PAQUETE=<id>` para saber si es tuyo); «AVISO DLL: SIN RECOPIAR» significa que esa carpeta mide otro runtime hasta la siguiente compilación.
