# Escenarios de prueba de las CAC pendientes (carril dsCAC3, 21/11)

Solo hay archivos nuevos en `tools/pruebas_heroe/cac_pendientes/`: estos `.toml`, sus fixtures y este LEEME. No se ha
tocado `src/`, ni `tools/pruebas.py`, ni otros escenarios; no se ha compilado ni lanzado el juego.

Los escenarios están escritos contra el diseño de `studies 5/ENTREGAS/R23/MEDIDA_CAC_DS1.md` y `MEDIDA_CAC_DS2.md`.
Como esas CAC **todavía no están implementadas**, los `.toml` usan a propósito algunos ganchos y líneas de registro
que hay que añadir al implementarlas: están todos listados en §3 y §4. Los `log` esperan el texto exacto que se
propone; el prefijo `dbz3 heroe: ` es el ancla que ya existe en `src/` y por eso `preflight` no da avisos.

## 1. Cómo se lanza

```
python tools\pruebas.py preflight tools\pruebas_heroe\cac_pendientes
python tools\pruebas.py correr smoke_b tools\pruebas_heroe\cac_pendientes\cac10_cuatro_trajes.toml
```

Cada `.toml` se prepara solo: `poner` copia su fixture de `cac.toml` a la copia de prueba y `apartar` deja a un lado
la partida y el `mods/heroe_propio` anteriores. Al acabar, el arnés lo restaura: **lo que haya creado la prueba queda
en `<copia>\_apartado\<fecha>\despues\`** (de ahí se sacan `cac.toml`, `mods/heroe_propio\ficha.txt`,
`modelos\base.bin` y `ui\retrato_*.png` para las comprobaciones de §5).

`preflight` a la entrega: **10 guiones, 0 avisos**. Ojo: `cac12_pantallas.toml` se lanza **dos veces** (ver §2.3);
el `--env` manda sobre el `env` del `.toml`.

## 2. Qué comprueba cada escenario

### 2.1 CAC-10 · hasta 4 trajes por héroe

- `.toml`: `cac10_cuatro_trajes.toml`. Fixture: `fixtures/cac10_cuatro_trajes_cac.toml` (4 recetas: Rojo, Azul, Verde, Oro).
- Criterio del tablero: `heroe_trajes.toml` PASS. Este `.toml` prueba la **interfaz** que faltaba: la fila
  «Traje guardado» del creador (aplicar, recorrer, guardar) y el montaje del héroe.
- Pasos: abre Create con el cursor en la fila (`DBZ3_CAC_FILA=traje_guardado`), recorre al hueco 2, aplica (A),
  guarda la apariencia actual en el hueco 4 (X), vuelve a aplicar y confirma (RB) para montar.
- Capturas: `cac10_a_fila_traje`, `cac10_b_receta_aplicada`, `cac10_c_receta_guardada`, `cac10_d_combate_montado`.
- La mitad de combate (que el modelo salga de la receta) la sigue midiendo el escenario integrado `heroe_trajes`
  (`python tools\pruebas.py correr <copia> heroe_trajes --par receta=0|1|2`), ya escrito en `tools/pruebas.py`.

### 2.2 CAC-11 · piezas propias (3000+) por la misma receta

- `.toml`: `cac11_receta_mixta.toml`. Fixture: `fixtures/cac11_vegeta_mixta_cac.toml`, Vegeta (cuerpo 7) con la receta
  `orig3=3040` (placa propia) + `b3_botas=99` (botas B3) en zonas distintas.
- Criterio: receta mixta sobre Vegeta **aprobada por juez**. El `.toml` saca las capturas para el juez y comprueba
  que el montaje resume las dos listas; el juicio se hace fuera del juego (§5.2).
- Pasos: aplica la receta al abrir el Héroe (`DBZ3_TEST_HEROE_RECETA=0`), Habitación, Aspecto (creador), cambia la fila
  de botas para forzar un montaje, y confirma.
- Capturas: `cac11_a_habitacion_mixta`, `cac11_b_creador_mixto`, `cac11_c_previa_mixta`, `cac11_d_postura_confirmada`.

### 2.3 CAC-12 · flujo del creador (6 pantallas juzgadas)

- `.toml` principal: `cac12_flujo_creador.toml` (4 capturas): `cac12_1_crear_pestanas`, `cac12_2_pestana_activa`,
  `cac12_3_color_muestras`, `cac12_4_nombre_titulo`.
- `.toml` de las dos restantes: `cac12_pantallas.toml` (3 capturas). Se lanza dos veces:
  ```
  python tools\pruebas.py correr smoke_b tools\pruebas_heroe\cac_pendientes\cac12_pantallas.toml
  python tools\pruebas.py correr smoke_b tools\pruebas_heroe\cac_pendientes\cac12_pantallas.toml --env DBZ3_TEST_HEROE_PANTALLA=foto
  ```
  La primera deja `cac12_5_pantalla`, `cac12_6_pantalla_movida`, `cac12_7_pantalla_otra` de **Pruébatelo**; la segunda,
  lo mismo de **Modo foto**. Entre las dos corridas salen las 6 pantallas del criterio.
- Juicio: subagente distinto del autor, cada captura al lado de la maqueta y de la referencia ZW/XV2, ≥ 8/10 por pantalla.

### 2.4 CAC-13 · montaje rápido, progreso y tope

- `cac13_montaje_rapido.toml`: 5 cambios de color (fila 1, lista plana) y 5 de pieza (torso B3), con las líneas de
  **caché** y **progreso**; capturas `cac13_a_progreso`, `cac13_b_color`, `cac13_c_fila_pieza`, `cac13_d_pieza`.
- `cac13_tope_avisado.toml`: con `DBZ3_CAC_TOPE_TEST=1000` cualquier mezcla pasa del tope; exige el aviso ámbar y que
  Confirmar no monte. Capturas `cac13_tope_a_aviso` y `cac13_tope_b_confirmar_bloqueado`.
- Medición numérica: sobre `dbz3.log` (§5.3). Base de hoy (DS2): color 2,5-4,3 s; pieza ~2,2 s; montaje de combate 11 s.

### 2.5 CAC-18 · postura por hueso y retrato HD

- `.toml`: `cac18_postura_retrato.toml`. Fixture: `fixtures/cac18_postura_cac.toml` con 6 huesos girados
  (`CHEST`, `LARM1`, `RARM1`, `HEAD`, `LLEG1`, `RLEG1`) y retrato con pose 0, giro 22, zoom 100.
- Pasos: Habitación (se ve el modelo con la postura) → Aspecto (creador) → cambia la pose del retrato → confirma y
  vuelve a la Habitación, donde se comprueba el montaje del moveset propio.
- Capturas: `cac18_a_postura_habitacion`, `cac18_b_postura_previa`, `cac18_c_retrato`, `cac18_d_postura_confirmada`.
- La previa y el combate se comparan numéricamente con el self-test del diseño (matrices de los huesos girados,
  diferencia < 1 mm / 0,5°); el retrato en selector/HUD se remata con `heroe_exportado` (§5.4), porque el héroe propio
  no sale en la rueda normal (`celda=false`).

### 2.6 CAC-19 · formas que son otra receta propia

- `.toml`: `cac19_formas_receta.toml`. Fixture: `fixtures/cac19_formas_cac.toml`, forma 1 = receta «Azul» (traje y pelo
  distintos), estallido `#FFAA00`.
- Pasos: Habitación → Aspecto → fila «Forma 1» (recorre nativa → receta 1) → confirma y comprueba el montaje.
- Capturas: `cac19_a_habitacion`, `cac19_b_forma_receta`, `cac19_c_montado`.
- «Transforma y vuelve» se remata con `heroe_exportado --par fase=forma` (§5.5): manda P+K+G y saca una foto por forma.

### 2.7 CAC-20 · compartir y pegar por código de texto

- `cac20_codigo.toml`: código bueno (generado offline, ver §6). La acción «Pegar código» lo usa desde
  `DBZ3_CAC_CODIGO` (plan B sin portapapeles) y debe aplicar **exactamente** la ficha de `fixtures/cac20_ficha.txt`.
  Capturas `cac20_a_creador_antes`, `cac20_b_codigo_pegado`, `cac20_c_montado`.
- `cac20_codigo_roto.toml`: `DBZ3_CAC_CODIGO=hola`; exige el rechazo y que no se cree nada.
  Capturas `cac20r_a_antes`, `cac20r_b_aviso`, `cac20r_c_creador_intacto`.
- Criterio: código pegado en otra copia = mismo bin; código roto avisa. El `.toml` comprueba la ficha aplicada y el
  aviso; la igualdad de bin y la no-modificación se comprueban fuera del juego (§5.6).

## 3. Líneas de registro que hay que añadir (texto exacto y función)

Todas van con `REXLOG_INFO` (o `REXLOG_WARN` donde se dice) y el prefijo `dbz3 heroe: `. Los `log` de los `.toml`
esperan estas cadenas; si se cambia una palabra, hay que cambiar el `.toml`.

| CAC | Línea exacta (plantilla) | Dónde (fichero · función) |
|---|---|---|
| 10 | `dbz3 heroe: CAC-10: hueco {i}/4 ({nombre})` / `(vacio)` | `heroe_ui.cpp` · manejador de la fila «Traje guardado» dentro de `Crear`/`DibujarCreadorV2` (al recorrer) |
| 10 | `dbz3 heroe: CAC-10: receta {i} guardada ({nombre})` | `heroe_ui.cpp` · misma fila, tras `H::RecetaGuardar(i, ...)` devolver `true` |
| 10 | `dbz3 heroe: CAC-10: receta {i} aplicada ({nombre})` | `heroe.cpp` · `RecetaAplicar(i)`, tras `AplicarFicha(ficha)` devolver `true` |
| 10 | `dbz3 heroe: CAC-10: receta {i} borrada` | `heroe.cpp` · `RecetaBorrar(i)` (nuevo, si se añade) |
| 10 | `dbz3 heroe: CAC-10: trajes del heroe propio: {n}` | `heroe.cpp` · tras montar el héroe (Vía B) o `roster_build` desde C++ |
| 11 | `dbz3 heroe: CAC-11: montaje mixto: originales [{...}]; piezas_b3 [{...}]` | `heroe_ui.cpp` · `GenerarCon`, al leer el sidecar/JSON del montaje `combate_*` (o `TextoCombate`) |
| 11 | `dbz3 heroe: CAC-11: original {grupo} = {fid} ({nombre})` | `heroe_ui.cpp` · fila «Original 1-4» al cambiar (CAC-11b) |
| 11 | `dbz3 heroe: CAC-11: error: {mensaje}` (`REXLOG_WARN`) | `vista_heroe.py` → JSON de error; lo pinta `heroe_ui.cpp` · `TextoCombate`/`MontajeFallido` |
| 12 | `dbz3 heroe: CAC-12: pestana {nombre} ({i}/{n})` | `heroe_ui.cpp` / `heroe_v2_creador.inl` · tira de pestañas (`CategoriaDeFila`/`FilasDeCategoria`) |
| 12 | `dbz3 heroe: CAC-12: guia: {frase}` | `heroe_ui.cpp` · guía con bocadillo (§3.2.5) |
| 12 | `dbz3 heroe: CAC-12: deshacer {n}/{max}` y `rehacer {n}/{max}` | `heroe_ui.cpp` · pila de estados (§3.2.6) |
| 12 | `dbz3 heroe: CAC-12: al azar: {zona} = {fid}` | `heroe_ui.cpp` · fila «Al azar» |
| 12 | `dbz3 heroe: CAC-12: pruebatelo abierto` | `rpg.cpp` · nuevo `DBZ3_TEST_HEROE_PANTALLA=pruebatelo` → `AbrirPruebatelo` |
| 12 | `dbz3 heroe: CAC-12: modo foto` | `rpg.cpp` · nuevo `DBZ3_TEST_HEROE_PANTALLA=foto` → `AbrirModoFoto` |
| 12 | `dbz3 heroe: CAC-12: foto guardada {ruta}` | `heroe_ui.cpp` · modo foto, al guardar el BMP |
| 13 | `dbz3 heroe: CAC-13: progreso {n}/{total} {texto}` | `heroe_ui.cpp` · `MallaFicha`/`GenerarCon`, leyendo `<clave>.progreso` |
| 13 | `dbz3 heroe: CAC-13: cache {tipo} {clave} ({ms} ms)` | `heroe_ui.cpp` · mismo sitio, leyendo el sidecar de la vista |
| 13 | `dbz3 heroe: CAC-13: tope de ventanas {n}/3261 ({detalle})` (`REXLOG_WARN`) | `heroe_ui.cpp` · al recibir `ventanas` del sidecar |
| 13 | `dbz3 heroe: CAC-13: Confirmar bloqueado por el tope ({n}/3261)` (`REXLOG_WARN`) | `heroe_ui.cpp` · `Crear`, botón Confirmar; no llama a `PedirCombate` |
| 18 | `dbz3 heroe: CAC-18: postura: {n} huesos ({sufijo:dx:dy:dz;...})` | `heroe.cpp` · `Leer` (al cargar la ficha) o `heroe_ui.cpp` · `AbrirHabitacion`/`AbrirCrear` |
| 18 | `dbz3 heroe: CAC-18: postura: hueso {sufijo} sin equivalente (ignorado)` (`REXLOG_WARN`) | `vista_heroe.py` → JSON; lo pinta `heroe_ui.cpp` · `TextoCombate` |
| 18 | `dbz3 heroe: CAC-18: retrato: pose {pose} giro {yaw} zoom {zoom}` | `heroe_ui.cpp` · fila «Retrato» (§4.2.4) |
| 18 | `dbz3 heroe: CAC-18: retrato: se aplica al confirmar` | `heroe_ui.cpp` · misma fila |
| 18 | `dbz3 heroe: CAC-18: moveset propio: {ruta}` | `heroe_ui.cpp` · `GenerarCon`, al terminar el montaje `combate_*` |
| 19 | `dbz3 heroe: CAC-19: forma {n} = receta {i} ({nombre}); estallido {color}` | `heroe_ui.cpp` · fila «Forma 1..7» al cambiar |
| 19 | `dbz3 heroe: CAC-19: forma {n} = nativa` | `heroe_ui.cpp` · misma fila, al volver a nativa |
| 19 | `dbz3 heroe: CAC-19: aviso: la receta de la forma {n} pide otro cuerpo; se queda la nativa` (`REXLOG_WARN`) | `vista_heroe.py` → JSON; lo pinta `heroe_ui.cpp` · `TextoCombate` |
| 19 | `dbz3 heroe: CAC-19: montaje: formas 0..{max} ({k} de receta)` | `heroe_ui.cpp` · `GenerarCon`, al terminar `combate_*` |
| 20 | `dbz3 heroe: CAC-20: codigo {n} caracteres` | `heroe_ui.cpp` · `TextoCompartir` (nuevo), al llegar `estado=ok` y copiar al portapapeles |
| 20 | `dbz3 heroe: CAC-20: codigo pegado: ficha de {n} bytes` | `heroe_ui.cpp` · `TextoPegar` (nuevo), al llegar `estado=ok` |
| 20 | `dbz3 heroe: CAC-20: codigo rechazado: {clave} ({mensaje})` (`REXLOG_WARN`) | `heroe_ui.cpp` · `TextoPegar`, al llegar `estado=error` |
| 20 | `dbz3 heroe: CAC-20: codigo: creando ficha nueva` / `archivando ficha` | `heroe_ui.cpp` · `PedirPegar`, antes de `C::NuevoSlot`/`C::ArchivarSlot` |

Las líneas que **ya existen** y los `.toml` aprovechan (no hay que añadirlas):

- `dbz3 heroe: receta aplicada a la ficha {slot}: {ficha}` — `heroe.cpp` · `AplicarFicha` (final).
- `dbz3 heroe: el heroe propio de la ficha {n}: se monta ({args})` — `heroe_ui.cpp` · `PedirCombate`.
- `dbz3 heroe: Create abierto (...)`, `dbz3 heroe: pasa a Edit/Create`, `panel de color de ...`, `Habitacion abierta`,
  `vista previa 3D ... cargada`, `vista previa ... generada (N ms)` — `heroe_ui.cpp`.

## 4. Ganchos de prueba que hay que añadir

Los nombres nuevos **no** empiezan por `DBZ3_TEST_` a propósito: así `preflight` no los confunde con una errata de una
variable que aún no existe. Todos van en el `env` del `.toml`, como manda el encargo.

| Gancho | Valores usados | Dónde y qué hace |
|---|---|---|
| `DBZ3_CAC_FILA` | `traje_guardado`, `pieza_botas`, `retrato`, `forma1`, `pegar` | `heroe_ui.cpp` · `AbrirCrear`: pone `g_fila` en la fila de esa clave (un `switch`; evita depender del orden de pestañas) |
| `DBZ3_CAC_CODIGO` | el código de §6 o `hola` | `heroe_ui.cpp` · acción «Pegar código»: si la variable existe y no está vacía, se usa en lugar de `ImGui::GetClipboardText()` (mismo camino: decodificar + `AplicarFicha`). Alias recomendado: aceptar también `DBZ3_TEST_CODIGO` (nombre del diseño DS1) |
| `DBZ3_CAC_TOPE_TEST` | `1000` | `vista_heroe.py` · comprobación previa del tope (y `recetas.montar_bins`): sustituye 3261 por este valor **solo en pruebas** |
| `DBZ3_CAC_B3_TEST_TODOS` | `1` | `vista_heroe.py` · `_aplicar_piezas_b3`: salta la aprobación por anfitrión (ya previsto en el diseño) |
| `DBZ3_TEST_HEROE_PANTALLA` | `pruebatelo`, `foto` | `rpg.cpp` · añadir estos dos valores al bloque de `DBZ3_TEST_HEROE_PANTALLA` que ya abre `crear`/`editar`/`fichas` |

`DBZ3_TEST_HEROE_ABRIR`, `DBZ3_TEST_HEROE_PANTALLA=habitacion`, `DBZ3_TEST_HEROE_RECETA`, `DBZ3_TEST_MODO`,
`DBZ3_HUB_MODE`, `DBZ3_TEST_CFG`, `DBZ3_TEST_HEROE_CUERPO` ya existen y no cambian.

## 5. Comprobaciones fuera del juego (lo que el `.toml` no puede hacer)

Los resultados de la prueba quedan en `D:\DBZ3HD\_pruebas\<fecha-hora>_<copia>_<escenario>\` y lo que creó, en
`<copia>\_apartado\<fecha>\despues\`. Capturas = `*.png` de la carpeta de la prueba.

### 5.1 CAC-10

- `despues\user_data\dbz3\cac.toml`: `receta_nombres` tiene 4 nombres y (si se añade) `receta_activa = 3` tras aplicar
  el hueco 4.
- `despues\mods\heroe_propio\ficha.txt`: contiene el `partes=` de la receta aplicada (la 4). Compáralo con
  `fixtures\cac10_cuatro_trajes_cac.toml`.

### 5.2 CAC-11

```
python "mod center hd\cac\vista_heroe.py" --juego <copia> \
  --ficha "cuerpo=7;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0;orig3=3040;b3_botas=99" \
  --clave cac11_mixta
python tools\pruebas_heroe\cac_juez.py --lamina "cuerpo=7;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0;orig3=3040;b3_botas=99" --salida lamina_cac11.png
```

Debe salir el resumen con **`originales`** (placa 3040) **y `piezas_b3`** (botas) a la vez. El juez independiente ve
`lamina_cac11.png` con las 4 vistas y las 4 capturas del `.toml`; ≥ 8/10 o rechazo medido. Caso de no-mezcla:
`orig3=3040` + `b3_torso=416` debe dar error claro («elige una»), no un modelo a medias.

### 5.3 CAC-13

```
python -c "import re,pathlib,statistics; log=pathlib.Path(r'<carpeta de la prueba>\dbz3.log').read_text(encoding='utf-8',errors='replace'); ms=[int(x) for x in re.findall(r'vista previa .* generada \((\d+) ms\)',log)]; print('muestras:',ms,'mediana:',statistics.median(ms) if ms else None)"
```

- Color: mediana de los 5 cambios **< 3000 ms**.
- Pieza: mediana de los 5 cambios **< 50 %** de la base de hoy (base en el informe: 2,2 s por pieza, 2,5-4,3 s por
  color; si cambia, guarda la base junto al informe).
- Tope: en `cac13_tope_avisado.toml` no debe existir `despues\mods\heroe_propio` (Confirmar no montó) y sí deben
  estar `cac13_tope_a_aviso.png` (aviso ámbar) y `cac13_tope_b_confirmar_bloqueado.png`.

### 5.4 CAC-18

- `despues\mods\heroe_propio\moveset\anm.bin` existe y `personaje.toml` lo referencia.
- `despues\mods\heroe_propio\ui\retrato_p1.png`, `retrato_p2.png` (512×512) y `hud.png` (256×128) existen; la pose es
  la del fixture (pose 0, giro 22, zoom 100).
- Paridad previa ↔ combate: el self-test del diseño (matrices de los 6 huesos, diferencia < 1 mm / 0,5°). Si falla,
  no sigas a capturas: la convención de ejes no coincide.
- Retrato en selector y HUD: como el héroe propio no sale en la rueda, se remata con el escenario integrado (que monta
  su propio exportado, no depende de lo que deje este `.toml`):
  ```
  python tools\pruebas.py correr smoke_b heroe_exportado --par fase=rueda
  python tools\pruebas.py correr smoke_b heroe_exportado --par fase=esp
  ```
  El juez compara el retrato nuevo con `cac18_b_postura_previa.png`; la cara del HUD debe ser la misma entrada `hud_cara`.

### 5.5 CAC-19

La transformación y la vuelta no caben en este `.toml` (haría falta elegir el héroe en la rueda, y `celda=false`); se
prueban con el escenario que ya existe, como pide el diseño:

```
python tools\pruebas.py correr smoke_b heroe_exportado --par fase=forma --par cuerpo=7 --par cada=1 --par seg=240
python tools\pruebas_heroe\formas_fotos.py "D:\DBZ3HD\_pruebas\<carpeta de la corrida>"
```

Debe haber `f_forma1.png` con el aspecto de la receta «Azul» (no el SSJ nativo), la foto siguiente con el aspecto base
tras volver, y en el log `formas: prueba: sube a la forma 1` (ya existe) y `dbz3 heroe: CAC-19: montaje: formas 0..1`.

### 5.6 CAC-20

- Código bueno: `despues\mods\heroe_propio\ficha.txt` debe contener la apariencia de `fixtures\cac20_ficha.txt`
  (`cuerpo=7`, `partes=200,30,10|...`, `pelo=2`). El fichero puede llevar además `;nombre=` y `;donante=` que añade
  `ArgsPropio`; compara las claves de la ficha, no el texto entero. El log exacto es
  `dbz3 heroe: receta aplicada a la ficha 1: <ficha>`.
- Mismo bin en otra copia: monta la ficha con `vista_heroe.py --combate 9` en una carpeta temporal y compara el
  SHA-256 de `modelos\base.bin` con el de `despues\mods\heroe_propio\modelos\base.bin`; deben ser iguales.
- Código roto: `despues\user_data\dbz3\cac.toml` y `despues\mods\heroe_propio` **no** deben existir o deben ser
  idénticos a los de antes (el fixture); no se crea una ficha nueva. El aviso se ve en `cac20r_b_aviso.png`.
- Otros casos del diseño: `DBZ3R9...` (otra versión) y un código cortado deben dar el mismo rechazo.

## 6. Código y ficha de CAC-20 (generados offline)

- Ficha (`fixtures\cac20_ficha.txt`):
  `cuerpo=7;traje=0;partes=200,30,10|0,0,0|0,0,0|0,0,0;pelo=2;c1=0,0,0;c2=0,0,0;logo=0;scouter=0`
- Receta (`fixtures\cac20_traje.toml`) generada con `python "mod center hd\cac\recetas.py" a-receta "<ficha>"`.
- Código (112 caracteres) generado con `python "mod center hd\cac\recetas.py" codigo fixtures\cac20_traje.toml`:
  `DBZ3R1.eNpLLk0tKsi3NbcuKUrMSrU1sC5ILCpJLbY1MjDQMTbQMTSoMdABQmTSuiA1J9_WyDrZ0BbCTzaCMnLy0_OBRhQn55eWpBbZGgAAD_Ea6w.1c720cc9`

Si al implementar se quita `cv=` del código (DS1 §1.3.7), este código sigue decodificando a la misma ficha: el CRC y el
cuerpo no cambian. Vuelve a generarlo solo si cambia el formato `DBZ3R1` o la ficha.

## 7. Fixtures y campos nuevos de `cac.toml`

Los fixtures son `cac.toml` v3 válidos con los campos de hoy más los que añaden las CAC:

- CAC-10: `receta_nombres`, `receta_fichas` (4) y `receta_activa` (aditivo; `-1` = apariencia libre).
- CAC-11: `orig1..orig4` dentro de la ficha de la receta (`orig3=3040`); el fixture no los pone en el héroe porque la
  receta se aplica al abrir. En `cac.toml` el campo nuevo sería `orig = [..]` (DS2 §2.2).
- CAC-18: `postura_de`, `postura`, `postura_eje`, `retrato_pose`, `retrato_yaw`, `retrato_zoom`, `retrato_dx`,
  `retrato_dy`. `postura` se guarda con el mismo texto de la ficha:
  `CHEST:0:10:0|LARM1:-15:0:0|RARM1:15:0:0|HEAD:0:8:0|LLEG1:0:0:8|RLEG1:0:0:-8`.
- CAC-19: `forma_receta = [-1, 1, -1, ...]` y `forma_estallido = ["", "#FFAA00", ...]` (ya existen en v3).
- CAC-20: no añade campos.

Nada de esto sube la versión de `cac.toml` (siguen siendo v3 y aditivos); un guardado viejo se lee con los valores por
defecto (`-1`/vacío), como ya hace `AjustarSlot`.

## 8. Límites y avisos

- No se ha lanzado el juego: los tiempos de espera (120 s de montaje, 4-5 s de vista) son los medidos en el diseño; si
  un PC tarda más, subir la espera no cambia la lógica.
- El tramo `.toml` cubre la interfaz, el montaje y las capturas; la parte que exige elegir el héroe propio en la rueda
  (combate de CAC-10/18/19) se hace con `heroe_exportado`, que monta su propio exportado y no depende del estado que
  deje la prueba.
- `cac12_pantallas.toml` se lanza dos veces con `--env`; no lo cambies a dos `.toml` salvo que quieras duplicar pasos.
- `preflight` da 0 avisos hoy porque los `log` usan el ancla `dbz3 heroe: ` que ya existe. Si al implementar se elige
  otro prefijo, hay que actualizar el `log` y volver a pasar `preflight`.
