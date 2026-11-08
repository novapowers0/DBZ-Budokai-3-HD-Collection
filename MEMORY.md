# MEMORY.md — DBZ Budokai 3 HD Collection (memoria operativa viva)

> Complemento de `AGENTS.md`. **AGENTS.md = referencia estable** (formato,
> constraints, comandos). **MEMORY.md = estado vivo** (qué se hizo, qué está en
> curso, qué falta, y el **protocolo de colaboración con Claude**). Actualizar
> este fichero al cerrar cada sesión; no duplicar aquí lo que ya está en AGENTS.

Última actualización: **2026-11-05** (cierre solLAT3; se conservan los cierres previos de la ronda 1.5.6.1).

## Cierre solLAT3 — 2026-11-05

- Reanudación04:59 a petición del usuario, límite07:40: informe/TODO releídos, todos completados. Revalidación actual **63/63PASS**, mismo EXE limpio y configuraciones restauradas, sin procesos/ETW propios; `D:/tmp/solLAT3/auditoria_reanudacion_20261105_0459.json`. Conservados build y batería5/5 ya ejecutados, sin cambios de código ni experimentos nuevos; cierre anticipado.
- **Ningún candidato aprobado; mantener `dbz3_espera_dormir_us=0`**, opt-in100 anterior intacto. Probadas en orden cuatro esperas con giro inicial200/500/1000/2000µs, temporizador50µs+giro hasta100µs, protección de entrada/vblank y combinación. Todas ahorran CPU, pero ninguna cumple respuesta simultáneamente en combate y menú: delta mediana/p95 ≤+0,2ms y ≤ruido de tres controles0. No extrapolar un rechazo por pocasµs a latencia física: sondeo virtual1ms. Detalle en `studies 5/ENTREGAS/INFORME_solLAT2.md` §Continuación solLAT3.
- Evidencia definitiva **`D:/tmp/solLAT3/comparacion/`**: **14comparaciones,84ventanas,315respuestas válidas por lado/candidato/escenario**, 8.820válidas/8.854intentos,34nulos conservados; tres controles alternados por pareja. Combate completo de`combate_acotado/`, menú completo de`matriz/`. Cuatro grupos/escenario,8/8PASS funcionales; esto no acepta las siete variantes. Incidencias previas de deriva D/A, separación insuficiente y un error de indentación conservadas. `final/` es mezcla intermedia incompleta, no resultado definitivo; arnés válido `medir_robusto.py` + `instrumentado/medir.py`.
- Ejemplos de rechazo: giro200menú p95+0,4330ms; giro1000menú p95+0,1461ms frente a ruido0,0760; timer50combate mediana+0,3514ms; protecciónmenú mediana+0,0271ms frente a0,0084; combinacióncombate p95+9,8899ms. Control combate≈196–197% CPU (100%=un lógico)/FPS43,1–47,6; afinidad5, dos lógicos. Sanidad posterior con EXE limpio reproduce CPU196,7653%/FPS43,75 en15s; no acredita la causa ni permite comparar directamente con el125,90% de solLAT2. Las bajadas de p95 de controles muy variables no son mejora causal certificada.
- Audio esta vez **sin mute**, volumen/ganancia0,01: ETW en ventanas medidas **control1starvation+6otros diagnósticos/2.897,964s; candidatos0/0/2.898,022s**,0perdidos/errores. Único episodio en controlmenú/combinación; ningún caso añade starvation. Motor compartido, no exclusivo del PID; no demuestra ausencia de huecos guest rellenados por SDL ni escucha física. Timer50 efectivo antes de100µs sólo1543/290358esperas combate y1591/283182menú,0fallosAPI; el SDK ya usaba HIGH_RESOLUTION. Guardas3/4 usan aviso ideal del inyector, no una notificación de entrada integrada. Prueba vivaUS, EU sin aceptación dinámica nueva.
- Instrumentación/EXE medido conservados en`D:/tmp/solLAT3/retirado/` y`instrumentado/`; fuentes activas restauradas **exactas** con mtime nuevo, SDK y`github/` intactos. Build oficial **`python tools/compilar.py`: código0,10,81s**; DLL canónicas runtime11.197.440/GPU6.679.040/FFX5.413.888B. EXE dual=release55.102.464B/SHA256 **`c990311331c50ec73f42fe1a0169987ab2c1ddf8ceda7b03645aa2d66dd87ab8`**, sin marcadores solLAT3/perfilsolLAT2.
- Batería final **`python tools/pruebas.py todo --max 1 --forzar`:5/5PASS,251,38s**, sin reintentos; auditoría **63/63PASS**, TOML/música restaurados, procesos/ETW propios cerrados. Sanidad adicional: escenarioPASS y datosCPU/FPS guardados, pero su envoltorio terminó **código1** al exportar un `WindowsPath` aJSON; no confundir con la batería final exitosa ni omitir la incidencia. Online`--lockstep`/IW/B2 nuevos eran condicionales a aceptar candidato y no se ejecutaron.
- Informe y borradores **`D:/tmp/solLAT2/release/CHANGELOG_1561.md`**, **`cifras_promo.md`** actualizados; anteriores respaldados en`D:/tmp/solLAT3/respaldo/`. Sin incorporar prototipos, cambiar versiones, empaquetar ni publicar. El ahorro experimental no es novedad automática aprobada de1.5.6.1.

## Cierre solLAT2 — 2026-11-05

- `studies 5/ENTREGAS/INFORME_solLAT2.md`: **no activar por defecto**, continúa `dbz3_espera_dormir_us=0`, opt-in100 intacto. La premisa de seis llamadores dormidos estaba superada por solCPU2: filtro US `0x822311A8` / EU `0x8222F958` ya integrado. Ningún delta nuevo en `github/`; SHA de su `src/hooks.cpp` `6f8e8f3b97ff065f39916fd6b23466480769ad0aa006fb89b67ddca0b0d37054`. Fuente activa y SDK restaurados al contenido de entrada.
- Perfil con0: solo LR822311A8, guest6/Main XThread, host25860 combate/24948 menú. En25s por escenario: **14,96M /19,35M llamadas/s**, giro agregado18,1265/24,9588s (aproximación perturbada, no CPU exacta). Otros cinco sin filas: no asignarles roles audio/entrada ni inferir inexistencia. Perfil vivo US, pareja EU estática.
- Repetición **limpia** de seis series0/100/0/100/0/100, dos lógicos/afinidad5: **12/12 PASS,825,01s**. CPU combate **125,9004→63,9222%** (−49,23% relativo), menú110,5187→25,6935% (100%=un lógico). Respuesta combate tras consumo mediana5,6407→5,6662ms, **p957,9465→10,5498ms (+2,6033; margen+0,2)**, máximo10,8922→11,9801ms; menú máximo1,8152→5,2427ms. Entrada virtual→estado guest, no mando físico→fotones. Es el opt-in existente, no el prototipo `combinado100a` de solLAT. Datos `D:/tmp/solLAT2/seis_limpio_resumen.json`.
- Audio Windows ETW, evento33/starvation: positivo tres pausas propias300ms→29eventos/pausa,0perdidos. **0eventos33 en las24ventanas A/B**,0perdidos/errores; no filtrar al PID juego ni contar arranque como ventana estable. Endpoint no resuelto por stream. Arnés con`audio_mute=true`: mezcla/consumo ocurren antes de silenciar; salida SDL sigue activa, pero **no acredita ausencia de huecos guest rellenados por SDL ni sonido audible físico**. Rechazado ya por respuesta; no declarar aceptación de audio. Reloj por FILETIME, no sufijo+00:59 de tracerpt.
- **Trampa incremental corregida:** `copy2` restauró mtime antiguo; el primer build0 conservó el objeto de perfil. Primera serie12/12 y batería5/5 quedan solo como antecedente en`seis_etw/`/`respaldo_incremental_residual/`. Buscar también marcadores en ambos EXE, no solo verificar fuente. Se renovó mtime sin cambiar contenido, se recompiló y repitieron todas las series y batería.
- Build limpio `python tools/compilar.py` **código0,6,60s**; EXE dual=release55.102.464B/SHA256 `a143223ea93a69dcbea16d2f0b02f4dc9c98921f054c900ab709417ad407cd3c`, sin marcadores y estable hasta cierre. DLL canónicas runtime11.197.440/GPU6.679.040/FFX5.413.888B. Batería `python tools/pruebas.py todo --max 1 --forzar` **5/5PASS,290,96s,una ejecución sin reintentos**, campaña incluida. Auditoría**30/30PASS**, configuraciones smoke_b restauradas exactamente; worker terminado, sin dbz3/ETW propios activos. Online/IW/B2 nuevos eran condicionales a aceptar el candidato y no se ejecutaron.
- Borradores revisados en`D:/tmp/solLAT2/release/`: **`CHANGELOG_1561.md`**, **`cifras_promo.md`**, **`version.md`**. Solo novedades integradas; fuentes de cifras y límites de simulación/hardware real explícitos. RC ya1.5.6.1, sello SDK/parche1.5.6 compatible por primeros3componentes; propuesto resellado y actualización de tamaños del verificador, **sin aplicar ni empaquetar**. La condición de publicación sigue incumplida. Sin commit/push/publicación/issues.

## Cierre solLAT — 2026-11-04

- `studies 5/ENTREGAS/INFORME_solLAT.md`: **cierre definitivo tras seis series; no activar por defecto**, sigue `dbz3_espera_dormir_us=0` y opt-in anterior intacto. Restaurado para medir `combinado100a`: timer privado HIGH_RESOLUTION, protección17ms y giro inicial1000µs. El SDK ya emplea HIGH_RESOLUTION≥100µs; el microbenchmark de `Sleep(1)` no demuestra la causa en el juego.
- Repetición virtual con afinidad5/dos lógicos, orden **0/100/0/100/0/100**, combate+menú por serie: **8/12 PASS**, cuatro fallos conservados, sin sustituirlos por reintentos. CPU media combate **122,36→64,20%**; p95 tras consumo **4,796→7,751ms (+2,955ms, límite+0,2)**. Solo PASS conserva el mismo p95 (n39→59; mediana4,545→4,596ms). Menú solo PASS p951,564→10,852ms. Fallaron0 y100; no hay tres controles completos estables. Son resultados descriptivos, no latencia física botón→fotones ni prueba causal universal; no acreditan aceptación.
- Sonda SDL temporal separa cola vacía de silencio natural:12/12 escenarios, dos streams, sin errores. **0 bloques faltantes con0; cinco con100**, todos serie6/menú/entradas/stream1,26,667ms acumulados de muestras (no un único corte físico). Combate y tramos estables:0. No hubo bloques completos de silencio natural en las ventanas. No certificar corte audible físico ni usar el f32~2×como reloj. Detalle y filas en `D:/tmp/solLAT/vuelta_final/detalles_seis.json`.
- Prototipo+sonda archivados, retirados del activo. Restaurados tres archivos `src` al contenido de entrada (solo EOL distinto en `src/net/netplay_hooks.cpp`), fuente/header SDL y DLL/lib canónicas; tres destinos `github/src/` y parche SDL intactos. EXE medido archivado55109120B, SHA256 `ad00c7b8087b60b8b011621761aa2f643d9216201769ac44f8437622ddf687ff`; no es candidato aprobado. Fuente en `D:/tmp/solLAT/prototipo_descartado/`; sonda y EXE en `vuelta_final/instrumentado/`.
- Cierre restaurado/default0: `python tools/compilar.py` **código0**, DLLcanónicasOK; EXEdual==release55102464B, SHA256 `21ad45806d1010bdcef3c55c1e6b4508d719c350d70d2369f2a324673616e3e9`. `python tools/pruebas.py online-rb --lockstep` **2/2PASS,3000ticks,cero diferencias**, dos copias locales. `python tools/pruebas.py todo --max 1 --forzar` **5/5PASS,274,42s,sin reintentos**, campaña incluida. IW/B2 y online del nuevo prototipo no ejecutados al no superar A/B.
- Evidencias `D:/tmp/solLAT/vuelta_final/`: `seis_resultados.json`, `resumen_seis.json`, `detalles_seis.json`, respaldos y hashes; logs `D:/tmp/solLAT/vuelta_{build,online,bateria}_restaurado.log` y `_fin.json`. Auditoría `vuelta_final/auditoria_cierre.json` **77/77PASS**. TOML y selección musical de smoke_b restaurados exactamente, DLL temporal retirada, juegos propios cerrados. Cierre anterior respaldado y conservado como antecedente. Ningún commit/push/publicación.

## Cierre solCIERRE — 2026-11-04

- `studies 5/ENTREGAS/INFORME_solCIERRE.md`: contrastadas 26 rutas de las entregas de tarde; único delta nuevo aprobado, `github/tools/pruebas.py`, timeout de fases B2 **1200→1500 s**. SHA256 `3cb7f605bbcf657e8840847a5f10e64176f08ba20c7cc1f76ca1694aaf3f61f7`. #68/rendimiento/sondas IW ya integrados; hooks solo EOL, sin recopia. Ningún C++/SDK propio nuevo ni entrega tardía sin prueba integrada.
- Borradores `studies 5/ENTREGAS/ISSUES_1561/RESPUESTAS.md` actualizados: #68 requiere reconstruir el mod con herramientas corregidas; #63 pregunta sobre distancia tras Rush R1+B; #70 sigue parcial (definitiva/Hyper); #29 ahorro de memoria simulado. Sin publicación ni comentarios en issues.
- CPU: **se mantiene default 0**, opt-in100 intacto. A/B propio con controles virtuales y afinidad5: combate CPU124,02→52,83%, menú113,16→23,10%; respuesta tras consumo en combate mediana4,54→6,07ms, p956,08→8,01ms, máximo1cuadro ambos. No cumple la condición de no empeorar; no equivale a latencia física botón-pantalla. Contraste inverso conserva la tendencia de mediana, pero su control0 coincidió parcialmente con arranque online y no es pareja aislada. Sonda nueva máxima9,888ms; no reaparecieron16,276ms ni se garantiza un tope. Mezcla SDL sin huecos de ceros≥5ms; volcado ~2×tiempo nominal, no usarlo como reloj ni certificar equivalencia auditiva.
- Online lockstep propio **2/2PASS con100 y2/2PASS con0**, cero discrepancias (últimos ticks3000/2400); copias locales, no dos PC. Build oficial **código0/DLLcanónicasOK**; batería final ejecutada una vez **5/5PASS,263s,sin reintentos**, `D:/DBZ3HD/_pruebas/20261104-213103_todo.txt`. Integridad **83/83PASS**, `D:/tmp/solCIERRE/auditoria_cierre.json`; configuraciones sin espera experimental ni volcado y smoke_b restaurado al TOML previo. Arneses propios y juegos cerrados.
- Evidencias, respaldos y hashes en `D:/tmp/solCIERRE/`; EXE probado `8531a04f…` (55 102 464B), runtime11 197 440B/GPU6 679 040B, dual/release iguales a canónicas. Pendientes visuales/Tien/cámaras y definitiva/Hyper de Gotenks siguen abiertos. Cielo/Serpiente/Bubbles sin activación nueva. Sin commit/push/publicación.

## Cierre solCPU2 — 2026-11-04, 09:21

- `studies 5/ENTREGAS/INFORME_solCPU2.md`: espera de CPU acotada al único llamador medido, LR guardado antes del original: US `0x822311A8` / EU `0x8222F958`; los otros cinco por región quedan intactos. Integrados **`github/src/hooks.cpp` y `github/tools/solcpu2_medir.py`**, con respaldo/hash en `D:/tmp/solCPU2/`. **Default 0**, opt-in `dbz3_espera_dormir_us=100` (reiniciar). No activar de serie todavía: el planificador bloqueó hasta16276us al solicitar100us; no hay medida de latencia botón-pantalla ni equivalencia auditiva.
- A/B propio, sin sonda, combate35s/caso y afinidad verificada: dos CPU máscara5 **123.7→51.1% CPU,60→60FPS**; una máscara1 **96.7→50.8%,43.5→60FPS** (100%=un núcleo). Primera serie diagnóstica con sonda y cvar textura inválido conservada aparte; no mezclar ni atribuir causalidad. Resultado vigente `D:/tmp/solCPU2/sin_sonda/resultados.json`.
- Dos builds oficiales **código0/DLLcanónicasOK**. Con opt-in100us: campaña, IW0x7D0 hasta resultado y B2fase3 **PASS**; online lockstep **2/2PASS,2400ticks,cero discrepancias**. IW falló primero por fuente ausente; pasó indicando el banco EU archivado. Batería final, default0 sin override: **5/5PASS,235s**, `D:/DBZ3HD/_pruebas/20261104-091853_todo.txt`; cierre de evidencias **30/30PASS**, `D:/tmp/solCPU2/cierre.json`. Pruebas vivasUS; parejaEU estática, sin buildLinux propio.
- **Cielo/Serpiente/Bubbles siguen apagados.** Rehecho contraste de Serpiente:707poses/706parejas temporales,0errores sobre.001, pero85/792filas fuera de alcance y sin paridad de FOV/cielo/audio/juego completo. Bubbles: referencia241campos y pruebas funcionales no equivalen a paridad integral con IC/RNG/cadencia/voz iguales. `kBubblesProbado=false`; solo comentario local aclarado, no integrado. SDK,PNG,workflowLinux y partidas reales sin cambios propios; sin commit/push/publicación.

## Cierre solUI — 2026-11-03, 21:16

- `studies 5/ENTREGAS/INFORME_solUI.md`: launcher/desbloqueo/confirmación comprobados en ES/DE/JA a1280×720 y1600×900, capturas0,5 y partidas de prueba intactas porSHA256. Pestañas JA truncadas corregidas con `ImGuiTabBarFlags_FittingPolicyScroll` (`launcher_state.cpp`, una línea).
- Rechazos UDP locales reales: DE/sistema y build, JA/protocolo, ES/timeout. Detectado que la ayuda inicial ocultaba el motivo al fallar desde el primer paso: corregidas dos condiciones de texto en `online_panel.cpp`; motivo e informe visibles, sin cortes en ambas resoluciones. No equivale a prueba entre dos PC físicos.
- `tools/test_i18n.py` incluye ahora `.inl`:0ausencias. Auditor online:12textos×7idiomasPASS,2486claves; desbloqueo y cuatro errores nuevos completos. Escenarios visuales en nuevo `tools/pruebas_solui.py`, registro mínimo en `pruebas.py`. Dos builds oficiales código0/DLLcanónicasOK. Batería final propia una vez: **5/5PASS,276s**, `D:/DBZ3HD/_pruebas/20261103-211602_todo.txt`. Sin `github/` ni partidas reales.

## Cierre solNOVA — 2026-11-03

- `studies 5/ENTREGAS/NOVA_1561.md` e `INFORME_solNOVA.md`: tabla38recetas/37IDs. **Ningún ZIP publicado requiere reconstrucción/resubida por formas/Hyper/Rush; cola de publicación vacía.** Vegeta Saiyan IW y Goku Black SDBH usan `formas_donante`: basta exe nuevo. Rush se corrige en runtime para clones, sin afirmar prueba individual de cada mod.
- Auditoría propia:35ZIP con SHA256/ruta/versión del catálogo y sin `_roster` empaquetado. Hyper antiguo/nuevo idéntico para Vegeta IW/Yamcha IW/Janemba; control positivo GT IW cambia deltas3/4, pero GT está fuera del catálogo. Dos packs de texturas no descargados; Gohan Futuro solo receta/fuente local, no publicación. Evidencias/scripts en `D:/tmp/solNOVA`; sin C++, build nuevo, `github/` ni publicación.
- Batería propia: cuatroPASS de `todo --max 1 --forzar`; terminal cortó a600s esperando menú. PID propio18160 terminó; no se cerraron procesos ajenos. Recuperado solo menú:1/1PASS,134s con cola. **Cinco escenariosPASS conjuntos, con interrupción y recuperación**, detalle/rutas en entrega. No atribuir a solNOVA las pruebas dirigidas de solMODS.

## Cierre solREL — 2026-11-03

- `studies 5/ENTREGAS/INFORME_solREL.md`: cinco puntos del ensayo de instalación pasan en candidato02 aislado de `github/`: launcher/juego EU, Héroe nuevo guardado con modelo visible sin XV2 desde ruta acentuada, Kit por BAT/Python portátil, verificador y avisos al faltar XEX/carpetas. ZIP/evidencias bajo `D:/tmp/solREL`, sin publicar ni tocar partidas reales.
- Cambios mínimos: `src/version.rc`→1.5.6.1 y `tools/verify_release.ps1`→versión/referencia runtime11194880B; build oficial código0, verificadorOK. Python completo reconstruido con módulo revisado (el slim reutilizado inicialmente carecía de scipy).
- Batería: cuatro escenariosPASS; llamada cortada a600s esperando plaza para menú. Retomado solo menú:1/1PASS,161s (105scola); total5/5 propios sin repetir los aprobados. Logs `bateria_final.log`/`bateria_menu_continuacion.log`; procesos propios cerrados. Cierre de integridad3240/3240PASS.
- **Candidato sellado no equivale al EXE compartido posterior**: paqueteEXE `43a49d6b…`, actual al cierre `7982938e…` por recompilación concurrente. Hashes completos y ZIP en informe; no sustituirlo sin validación del coordinador. No se sincronizó `github/`.

## Cierre solREV2 — 2026-11-03

- Revisión independiente final en `studies 5/ENTREGAS/REVISION_solREV2.md` e `INFORME_solREV2.md`, contra foto1450 y siete incorporaciones concurrentes de partidas/empaquetado. Ningún bloqueante nuevo de código demostrado; experimentales permanecen optativos. **248 archivos aptos /20PNG excluidos del pase automático** por la regla de arte de juegos (tres ya idénticos a GitHub); nueve cachés y junction aparte. SDK solarZ requiere los cuatro parches juntos. No se tocó código ni `github/`.
- Pruebas propias: `DBZ3_AGENTE=solREV2 PYTHONUTF8=1 python tools/pruebas.py todo --max 1 --forzar` **5/5PASS,456s,sin reintentos**; `python -m unittest discover -s tools -p "test_solb2x_*.py" -v` **53/53PASS**; guardasUV11PASS. Copias oficiales, procesos propios cerrados. Sin buildLinux propio ni aceptación visual nueva.
- Evidencia en `D:/tmp/solREV2`: manifiestoSHA256 `inventario_cierre.json`, batería/unitarias/guardas, contraste de cuatro mensajes7idiomas y28registrosUS/EU de solUNL2 (autor, no pruebas propias). `verifica_entrega.py` comprueba cobertura exacta268archivos y ausencia de cambios frente al cierre; PASS en `verificacion_entrega.json`. Respaldos antiguos sin checksum siguen limitados a recuperación manual. Cambios posteriores requieren contraste contra este manifiesto.

## Cierre solUNL2 — 2026-11-03, 20:22

- `studies 5/ENTREGAS/INFORME_solUNL2.md`: fallos de Desbloquear todo cerrados. Reproducida restauración de respaldo dañado con falso éxito; corregida validación previa con inventario/checksum, copia duradera y marcador previo a las sustituciones para recuperar cortes.
- US/EU: cuota simulada, bloqueo Windows real, muerte a mitad de escritura y respaldo dañado/incompleto: **28 comprobaciones SHA256 PASS**; Héroe/Tag Team intactos. Ausencia inicial US/EU y contenido nativo cargado también PASS.
- Build oficial código0; batería general **5/5 PASS**, `D:/DBZ3HD/_pruebas/20261103-202118_todo.txt` (un reintento documentado tras incidencia de la primera corrida); idiomas0 faltantes. Sondas solo con ambas variables `DBZ3_TEST_SAVE100_UI` y `DBZ3_TEST_SAVE100_FAULTS` desde el arnés. Respaldos antiguos sin checksum se conservan pero requieren recuperación manual, no se restauran automáticamente sin verificación.
- Archivos: `src/native_mods.cpp`, `src/native_save100.inl`, cuatro textos7idiomas al final de `res/ingame_ui/i18n_modos.toml`, registro en `tools/pruebas.py` y nuevo `tools/pruebas_save100_fallos.py`. Sin `github/`, SDK, main ni CMake.

## Continuidad solDERX — 2026-11-03, después de las 14:50

- **Cierre 19:40, Heat Dome original:** ejecutado en Another Road PSP mediante PPSSPP propio aislado (no PCSX2), Trunks transformado contra Goku en entrenamiento. Evidencia `D:/tmp/solDERX/artifacts/20261103-191807-741229-ppsspp-reference/`. Medidos `0x476`, HR3, constructor default y despacho SPX; atacante `0x818`/anim `0x40064`, rival `0x2f29`/anim `0x90065`, BSK compartido `0x08eae080`. Cámara efectiva `0x08ab2cd0`, seis float +110/+120 preservados. Pendientes decodificación de stack+d0 para ranura/player, cruce de `heat_native_trace.jsonl` (626 eventos, no 626 llamadas acreditadas), escritores/semántica de cámara y traducción de control/tiempos/ambos actores. Usar `heat_constructor_efectivo.json`, no el intento anterior. Alcance: una ejecución SB2, sin otras ramas ni aceptación nueva del port; `0x476` sigue omitido por receta. Cierre propio registrado 19:40:08, ejecutable/partida fuente preservados (true/true). Sin build C++ ni batería general final. Informe y censo ampliados al cerrar; detalles en sus encabezados.
- Referencias: `studies 5/ENTREGAS/INFORME_solDERX.md` y `CENSO_solDERX.md`;
  evidencias propias `D:/tmp/solDERX`. **Ninguna escena completa nueva aceptada.**
  Donante B3 PS2 SLUS-20998 completo y Ghidra instalado: bloqueos resueltos.
- AMM PSP: pista `<4H2I>`, canal/selector/cantidad/tipo/ptrframes/ptrvalores;
  selector 0/1/2 = frames u8/u16/u32, demostrado por asm en SB1 y SB2.
  `sb_amm.py`/`sbport.py`: `wide_frames=False`, CLI `--fotogramas-amplios`,
  experimental/desconectada del importador. Auditor `tools/solderx_amm.py`:
  123 AMM, 8001290 claves hasta HD antes del retarget; 2204 pistas/225533 claves
  recuperadas; 123 salidas predeterminadas idénticas al censo previo.
  `amm_verificado.json`; 19 comprobaciones PASS en `pruebas_amm.json`.
  Preservada anomalía original Piccolo SB2 M1 clip7/hueso23 (frame23→0).
- Diez reconstrucciones aisladas en `reconstruidos/resultado.json`: ocho TRX
  SB1/SB2/base/M1/anterior/amplio con modelos HD390/391, 94 pistas/8410 claves
  recuperadas por caso amplio; regresión baseline SB2 idéntica. Sin aceptación
  de interpolación/retarget/juego. B1 Zarbon/Dodoria: **ANM publicado idéntico
  al aplicar `drop_effects=[0x64]` del importador**, `acotar_b1.json`.
  Cámara: **también idéntica byte a byte** al resolver intervalos float32.
  Zarbon escala1.1102278/dy-1.0387843; Dodoria escala0.7021208/dy0.00063.
  `camara_intervalos_b1_v2.json` supera el ajuste aproximado inicial de
  `acotar_camara_b1.json`: cero componentes distintos y archivo completo idéntico.
  No demuestra historial ni aceptación visual; conservar `ult_out={}` al comparar.
- SPX: `b1port.spx_slot_span` omite sumar base+0x14. Sin parche de producción;
  25 SPX B1/222 ranuras/111 cronologías heurísticas afectadas. La ablación no
  cambia ANM/cámara B1. Spans por ranura de `heatdome_original.json` INVÁLIDOS;
  usar `spx_original.json`/`medir_spx.json` (BC/BSK/AP/AMM anteriores sí válidos).
- Heat Dome 0x476 sigue omitido por receta. VM/dispatch originales localizados;
  `spx_contraste.json`:34 formas coincidentes/15 asm normalizados idénticos,
  no equivalencia de flujo. `spx_control_v2.json` es provisional: retornos por
  pila/continuaciones/tareas impiden afirmar CFG exhaustivo o tiempos ejecutados.
  SB2 HR3 usa rival, tabla default1b78d8 (override real1b9a78 en otra ruta),
  hijo1SPX; variante+100 depende de rival+cfa y 87e2c(p). Esta devuelve1 si
  estado890 en{1,2a} y byte93c!=0. `spx_seleccion.json` enlaza banco3→hijo5AMM
  y prioridad BSK: 0x471/0x476 en hijo3 de BCTRX/BCTRXM1, no overlay4.
  SB2 Heat Dome anim99/banco3/AP1frame26→HR208/slot11; rama HR3 exige además
  global+88 en{3,4}, rival+890=1f y otras condiciones. **No acredita rama/impacto
  efectivos ni slot11 frente111**; no extender automáticamente el contrato a SB1.
  Exports `sb2_spx_{impacto,seleccion}.txt`, `sb2_amm_binding.txt`.
- **SB1 contrastado aparte:** `sb1_bancos_v2.json`, `sb1_amm_binding.txt` y
  `sb1_spx_hr3.txt`. Banco3→hijo5 por4c448/4d44c/ec90; 471/476 en BSKhijo3
  de BCTRX/M1 (AFS96/97), anim87/90,170/80frames. HR3 usa rival, hijo1SPX,
  selector0→47dcc/default144038. Envío62068..62080 copia slot directamente,
  **sin suma100 en esa ruta**: no extrapolar SB2. Condiciones global+80 en{3,4},
  rival+840=1f y retornos anteriores; no acredita impacto ni escena ejecutados.
  Usar _v2: primera salida confundía nombre de campo bones con puntero;
  ahora bone_count39 y bone_table_offset separados, recursos intactos.
- `nativas_comparar.json`:20 destinos candidatos/juego en tablas por defecto.
  Exports SB1 `sb1_spx_nativas.txt`/`sb1_selector_actor.txt` contrastan despacho,
  selector y parcialmente IDs17/18/26/54/64; no ABI completo ni pila ejecutada.
  `verificar_nativas.json`:14/14 PASS, 12 lectores float completos de56B y
  despacho/selector SB1. IDs106..111: base+110, offsets0,4,8,16,20,24,
  **no seis floats contiguos**; estado global [8bfc0]/[9f5b8], salida param2+40,
  cero si nulo. Falta seguir todos los escritores antes de llamarlos ojo/objetivo.
  Cierre propio SB1 `sb1_camara_estado.txt`/`verificar_camara_estado.json`:4/4
  grupos PASS,32 instrucciones y literal [CAM] MAIN cotejados. Inicializador3fcbc
  enlaza +110 con puntero+158; actualización3fbc0 entrega destino158/fuente154,
  tamaño40 a11799c y destino a f38/ea0. Enlace con ruta inicializada de cámara
  principal demostrado; no todos los escritores, semántica completa ojo/objetivo
  ni ejecución de Heat Dome. No extrapolar este enlace a SB2.
- `python awo_tools/sbport.py --prueba`: OK. Batería general anterior5/5 PASS,
  256s, en `bateria_final.txt`, anterior al experimento AMM; no repetida.
  `verificar_cierre.py`/`verificar_cierre_v2.json`:15/15 PASS, reconstruye juntos
  los cuatro ANM/cámara B1 publicados idénticos, coteja ocho referencias SB1/SB2
  y 14 palabras de instrucciones/tablas directamente con ambos ELF; confirma
  opción amplia apagada en las tres API y publicados intactos. Cierre9/9 anterior
  conservado en verificar_cierre.json; no confundirlo con el resultado vigente.
  No cambios C++ ni build nuevo; ampliar fidelidad exige comparación original/port
  por fotograma, cámaras/actores/ramas, modos no combativos y Satán completo.

## Continuidad solB2X — 2026-11-03

- Referencias: `studies 5/ENTREGAS/INFORME_solB2X.md` y `CENSO_solB2X.md`.
  Ninguna escena completa nueva aceptada; `DBZ3_TEST_B2_MOTOR` apagado en uso normal.

### Continuación19:11–19:40,3/11 — solarZ optativo

- `DBZ3_TEST_B2_SOLAR_Z=1`/`--solar-z`, **off por defecto**. Fuente nueva
  `src/escenarios_b2_solar_z.inl`, canales de fotoSinZ/PruebaZ en solar_foto,
  arnés y auditor`tools/solb2x_solar_z.py`/test nuevo. SDK añade pipeline
  **aditiva+profundidad estricta**, porque el anterior ignoraba aditiva al
  usarZ. Sin escrituraZ ni fallback oculto; estado`dbz3_solar_depth_estado=2`
  exige recurso real. Header shader nuevo`dbz3_solar_depth_ps.h`.
- SDK compilado por paso optativo **`DBZ3_COMPILAR_SOLAR_Z=1 python tools/compilar.py`**,
  dentro del turno habitual; helper`tools/compilar_solar_z.py`. Copia anterior
  DLL/lib y logs en`D:/tmp/solB2X/sdk_solar_z_20261103_191943/`.
  **Runtime canónico11194880B**,SHA256`80ae9cc449322e873623926e8fb675540e40a79fbcac3d78b10369acd6b6c86f`.
  Último build oficial0/dual+release+DLLsOK; **53/53 específicasPASS,0,061s**.
- Última prueba tras el últimoC++: `_pruebas/20261103-193344_solB2X_lanzamientos_grupo4`.
  Mismo publicado2469,SDK2:118357píxeles de aporte/0oscurecidos/0fuera.
  Zcandidato0 vs omitirZ:0diferencias. **Sonda.9995 recorta2024 píxeles a la
  base exacta;116333 sobreviven idénticos**. Primera.98 sin recorte conservada.
  Defaultoff confirmado192745; entre partidas los327 píxeles de aporte distinto
  coinciden todos con base distinta, no se atribuyen aZ.
- **No equivalenciaPAL aceptada:** Zfijo16770000 de los4envíos NO es
  `estado.profundidad`. Invertir proyecciónPAL da d≈.100000006, limitado al
  nearHD1→Z0; candidato derivado. GSdump contiene6591vértices conZcrudo superior
  y escrituraZ habilitada: falta descarte/rasterización y Zefectivo bajoquad.
  Resultados`solar_z_final_20261103.json`,`solar_z_cierre_final_20261103.json`,
  `solar_z_rangos_gs_20261103.json` enD:/tmp/solB2X. JSONde foto declara
  `z_equivalencia_pal:false`. Informe/censo actualizados; ninguna escena nueva.
- **No batería general final: la pasa el coordinador**, por encargo. El5/5
  de18:49 inferior es histórico y no valida este código. SDKpatches pendientes
  de sincronización por coordinador; no se tocógithub.

### Histórico — continuación14:50–19:00,3/11

- Informe actualizado en **«Cambios desde las14:50»**: archivos/interruptores/
  pruebas y evidencias. **49/49tests específicosPASS,0,072s;5/5generalesPASS,
  18:49–18:53/268s**, una batería final. ÚltimoC++ compilado oficialmente
  código0/dual+release+DLLs; luego solo adquisición/auditores/documentación.
  Resultados generales`D:/DBZ3HD/_pruebas/20261103-{184916_smoke_a_tag_swap,
  185044_smoke_a_campana,185149_smoke_a_du_continuar,185237_smoke_a_batalla,
  185318_smoke_a_menu}`. Las cifras44tests inferiores son histórico anterior.
- Nuevos optativos/off: `DBZ3_TEST_B2_ACTORES_1450` (Nieve0..60/440..460,
  Ciudad265..305;giros6064/6072), `DBZ3_TEST_B2_TRAYECTORIA_NATIVA` (ACC nativo,
  snapshot18B restaurado ambos actores), `DBZ3_TEST_B2_REACCION_PAL` (grupos7/8,
  PAL1DACA0/1DA7F0,descriptor152+selector temporal/restaurado),
  `DBZ3_TEST_B2_SOLAR_CAPAS` (foto fija5/0quads/mismo guest).
- Nieve173512: víctima zona0máx≈620→191; zona1[2,54080;0,67447] sin mejora.
  Nueva182104 mide6064/6072. PALinicioexacto porScreenshot:564vsHD574;
  `direccion_formula_1840_20261103.json` reproducevelHDorden13 conerror<2e-7,
  pero suma giroPAL19114/HD15913 y preparación distintas. **No parchear ángulo
  por parecido**; seguir origen de giros/animación anteriores. Ciudad174217:
  error finalvíctima184,04921→181,06095/códigos40vs42; sigue sin aceptar.
- Solar170411:118357píxeles cambian5→0capas,0fuera/0oscurecidos; no parPAL.
  **GS observado184104:80vértices=4×20,PRIM54/Z16770000/ALPHA48,
  ZTE1/ZTST2/ZMSK1 durante efecto**, TESTbajo5040A. UIHD sigue sinZ;
  diferencia funcional confirmada, impacto visual/composición sin cerrar.
  `solar_envio_consumido_20261103.json` y`solar_gsdump_ad_20261103.json` enD:/tmp.
  `solar_contrato_cierre_20261103.json`:4/4envíosPASS;PRMODECONT1 y ZMSK0
  restablecido tras cada bloque4+16. Sigue sin probar píxeles/oclusión visible.
  **Rectificación lector:** omitía XYZ víaA+D, falsa ausenciaPRIM54; sustituido.
  `tools/solb2x_original.py --estado-pausado --estado ... --gs-dump` verifica
  restauración17688==17688 porStartPaused/PINE. Sin frame presentado no hayPNG
  inicial; no avanzar para fabricarlo. DumpF9 registra campos posteriores.
- RobotGS182742 reloj418,79694, nofoto300equivalente. CLUTISO idéntica en
  TBP10944/CBP16083, pero4022/4096índices distintos en memoria inicial;
  `robot_rampa_consumida_1840_20261103.json`. No atribuirTBP11104 retenido a
  dibujos de esa captura ni aceptar por paleta igual. Resolver transferencias/
  índices y consumo antes del cambio de sombreado. Renderer13 solicitado,
  rasterizador efectivo no certificado por la opción ni logD3D12 de presentación.
  RelecturaA+D final`robot_registros_cierre_20261103.json` confirma0vértices11104,
  0subidas11104/16082 y9288vértices10944/16083; inventario corregido
  `robot_gsdump_ad_cierre_20261103.json`.
- **Ninguna nueva escena encendida.** RR robot/Llanura/Nieve/Ciudad siguen
  pendientes de coincidencia integral incluyendo FX/pantallas/audio. No cambios
  deSDK ni parcheZsolar en esta continuación.

### Cierre de revisión — envío y profundidad solar,3/11

- SolarPAL132F70(2,1)/(2,0):ZMSK1→0 inhibe/restaura escrituraZ; no desactiva
  ZTE/ZTST. ColaVIF1 retenida1DED2C0,256entradas24B: índices65/68 conZBUF1/2
  1010000C0/10000C0; raíces66=1E1E280 y67=1E1E3D0 reúnen4+16XYZ2, Z16770000.
- **Corrección del límite anterior:** CHCR145/ASP0/TTE1/canal1; RET conASP0
  finaliza válidamente source-DMA. No exigirCALLexterno como prueba ausente.
  Dos recorridos estáticos completos, no orden histórico ni ejecuciónGS/GPU.
  Las entradas retenidas pueden estar consumidas; ZfinalGS no es Zduranteefecto.
- Auditores`solar_zbuf_fragmento_1320_20261103`,`solar_dma_cola_1323_20261103`,
  `solar_cola_resumen_1326_20261103` (`.py/.json`,D:/tmp/solB2X), ejecutados0;
  validación20XYZ2/máscaras1→0/CHCR yRAMSHA. No nuevos cambios de producción.
- **44/44tests y batería5/5PASS** posteriores al último código; no repetida.
  Informe/censo actualizados. Ninguna escena completa activada. Robot conserva
  manos grises/contorno corregidos; rampa alfa/mezcla/FX/reorientación y plano
  final pendientes. Solar conserva contraste parcial geometría/RGBA/UV;
  profundidad efectiva/composición e instante completo equivalente pendientes.

### Continuación — rampa instalada y batería final,13:10,3/11

- Robot123758: fin750/bancos=True,36dibujos por pasada/72claves de material
  contrastadas (RGB/escalaXYZ/textura/rampa/vtype/vflags), no80B completos ni GPU.
  `tools/solb2x_robot_materiales.py`+integración en foto300 del arnés; nuevo
  `tools/test_solb2x_robot_materiales.py` cubre adquisición y errores de lectura.
- WindowsSDK `xmemory.h::PhysicalHostOffset/GuestPtr` añade1000 a direcciones>=E:
  el lector externo bruto no lo hacía; aliasA+(puntero−E)+1000 sí. Ucode completo
  del alias vincula12/46/5→8B5902/A4A965(contorno),14/23/5→E9DD8C/5F27(toon),
 16/5/5→FDF960/1BFD08 y16/27/5→E7F844/CB0605. MemoriaCPU, no ejecuciónGPU.
- `D:/tmp/solB2X/robot_toon_contrato_1253_20261103.json`:rampa1 original64²,
  RGBconstante(1,0,0)/alfa0..160; DXT3 tieneRGBnegro/alfavariable. PS5F27 muestrea
  soloXYZ de tf1/restaRGB: no consume esa variación de alfa. Auditor
  `robot_rampa_instalada_1303_20261103.json` confirma DXT3 instalado idéntico,
  SHA1658607e8498d93498d69f50b7a8541b87d9468cf2e00f0b6cf89467b642a74d.
  Falta traducir consumoVU/mezcla original, no oscurecer a ojo ni habilitar la
  ablación como solución. Manos/contorno corregidos, iluminación completa no.
- Solar`solar_uv_muestreo_1307_v2_20261103.json`:20UV exactos;ALPHA48/CLAMP0
  compatibles con UI;TEX1 LCM1/Kfirmado−4,5 corresponde a magnificación lineal.
  No culpar aMMIN0; JSONsin`_v2` omitía signoK y queda superado. TESTZTE1/ZTST2
  frenteUI sinZ, pero sin estado efectivo/impacto de oclusión acreditados.
- **44/44testsPASS,0,054s; batería5/5PASS,304s,código0,una vez**, posteriores
  al último código. Evidencias130345tag_swap/130505campana/130605du_continuar/
 130645batalla/130720menu en`D:/DBZ3HD/_pruebas/` (prefijo20261103-).
  Menú esperó60s por plaza, sin repetir; partidas respaldadas/cierres0,2s.
  Último build oficial posterior a12:06,código0/dual→release/DLLsOK; después solo
  lectores/tests/auditores. Informe/censo actualizados. Corte del encargo13:30.

### Continuación — solar fijo y cuantización,12:15,3/11

- Nueva sonda optativa `--solar-foto-fija`, solo `--solo-solar`: eventos propios
  del PID detienen pasos guest y conservan la UI durante la foto, máximo30s.
  Registra GuestImage/quads/UV/RGBA; no acredita sincronización de la colaGPU.
-120128 y120609: estado antes/después idéntico, bancos=True. Última:
  guest2463/publicado2462. Los20vértices UI contra PAL17688/proyección17687
  difieren≤0,06118168pxGS; matriz7,63e-6. Corregida truncación previa GS128 antes
  de convertir a color UI255: errorRGBA normalizado0,00836397→0,00189951.
  Auditor `D:/tmp/solB2X/solar_contraste_rgba_1206_20261103.json`.
- Build oficial0/dual→release/DLLsOK y39testsPASS,0,036s, posteriores al cambio.
  Imagen completa incluye HUD/actores/fondo distintos: no usar su MAE como prueba
  aislada del solar. Ninguna escena aceptada. Materiales originales del robot:
  `robot_materiales_1155_20261103.{py,json}`,38partes/textura0 alfa0 en65536píxeles.
  Pendientes iluminación/FX/composición y batería final. Continuar hasta13:30.

### Continuación — sonda y ablación de brillo HD,11:42,3/11

- `escenarios_b2_robot_sonda.inl` observa constantesCPU de las dos rutas:
  A5798→822334D0 (vtable+168) yA57D8→822335A8. No es captura del shader ejecutado.
  Control113142 y ablación114106: fin750/bancos=True. La ablación explícita
  `--robot-sin-brillo-hd` fija temporalmente parte+22=1 solo en la primera pasada
  del robot, usa rama nativa c39.x=0 y restaura descriptor; mantiene dos pasadas.
- Foto300640×360 cambia34142píxeles; claros en región fija2108→548, bordes blancos
  reducidos/manos grises conservadas. PAL aún más oscuro/FX ausentes. **Ablación
  apagada por defecto; ninguna escena aceptada.** Auditor/SHA256:
  `D:/tmp/solB2X/robot_sonda_resultados_20261103.{py,json}`. Build oficial0/DLLsOK,
 39testsPASS0,038s. Batería final pendiente; seguir robot/solar hasta13:30.

### Continuación — contraste posterior a11:03,3/11

- Robot103110/110206: manos grises y contorno con dos pasadas,fin750/bancos=True.
  Inicializado Wcuarta dirección1(robot_mem+92); banco ambiental antes de restaurar
  ahora(0,8;0,8;0,8). Build oficial0/DLLsOK. **Foto300 antes/después idéntica,
  cero píxeles distintos: no declarar corregido el exceso de brillo.**720difiere
  451/230400píxeles en zonaactores; PAL013200 aún difiere en FX y plano final.
  Auditor estático ambiental tiene0estadosPAL vinculados; no llamarlo replay.
- Solar: fixture de cámara PAL de laboratorio (`--solar-camara`,grupo4,
  `--solo-solar`) integrado/compilado; descripción de escrituras y docstring
  corregidos para resultados nuevos.103843 repite muestra17687, matriz antes con
  error7,62939453125e-6; captura no atómica2492→2526. El paquete del estadoPAL17688
  con Screenshot idéntica reproduce17687: no introducir desfase global.
 20vértices/RGBA dentro1/16px/exactos y texturas8/9 ISO==RAM. No acredita GS ni
  imagen final. DMA termina RETsinCALL observado; GS.bin v9 es estadofinal,
  readbackVRAM no probado. Composición/oclusión y escena completa pendientes.
- Evidencias nuevas en informe/censo y
  `D:/tmp/solB2X/contraste_revision_1115_20261103.{py,json}`. **39/39testsPASS,
  0,040s** posteriores al último cambio; batería5/5 de10:04 es anterior a cámara
  y ambiente. Seguir hasta13:30; no activar escenas por arreglos localizados.

### Continuación — 10:04,3/11

- Solar inicial: `escenarios_b2_motor.inl::PrepararSolar` crea antes del contacto
  y solicita panel UI. Crear quads no bastaba: el panel solo se abría al iniciar
  transición. AntesHD094909 no había destello ni subida; después095519 el PNG
  muestra destello/fantasmas y el log confirma texturas originales8/9 subidas,
  120pasos/dibujos,alfa1,bancos=True. Corrección localizada comprobada.
- Arneses `solb2x_original.py`/`solb2x_lanzamiento.py`: `--solar-previo 120
  --solo-solar`,grupo4,sin ataque; `solb2x_solar.py::posiciones_solar` mantiene
  centro0/separación120. PAL094909 también positivo. No igualar fotos: cámara y
  momento distintos; HD no atómica/cuadros2648→2690, estados antes/después guardados.
  Fixtures094351/094559 miraban al otro lado;1CD000 decide por vista anterior.
- Transición normal posterior `20261103-095642_solB2X_lanzamientos_grupo4`:
  fin527/bloqueo vacío/VMsinerror/bancos=True, solar zona0 antes del evento y
  dos tareas zona1 después,133/63pasos y dibujos,alfa0/ceroquads. No deduplicar
  por posición. ResumenSHA256 `D:/tmp/solB2X/cierre_solar_visible_20261103.json`.
  Mezcla/UV/viewport/imagen equivalente y combates consecutivos sin comprobar;
  ninguna escena completa aceptada. Robot conserva iluminación/FX/plano pendientes.
- Build oficial0/dual→release/DLLsOK; **37/37tests PASS,0,042s**, comando
  `python -m unittest discover -s tools -p 'test_solb2x_*.py'` con PYTHONDONTWRITEBYTECODE=1,
  PYTHONUTF8=1,DBZ3_AGENTE=solB2X. Una batería final de revisión nueva:
  `python tools/pruebas.py todo --max 1 --forzar` con las mismas variables y
  DBZ3_TEST_CFG='gpu_backend="d3d12"': **5/5 PASS,251s,código0**, evidencias
  095911tag_swap/100031campana/100136du_continuar/100219batalla/100254menu en
  `D:/DBZ3HD/_pruebas/`. Posteriores a arreglos robot/solar; partidas respaldadas.

### Continuación — 09:32,3/11

- Robot: corregidas manos/articulaciones negras **conservando dos pasadas**.
  `escenarios_b2_robot.inl`: contornoHDmodo1/400000 en lugar de copiar modo2PAL;
  textura desactivada FIJANDO10000000 (`94788`:!10000000&&20),como C8068.
  Build oficial código0/dual→release/DLLsOK. Una dirigida normal092954:
  fin750/bancos=True/569pares;segundaEC3FF8FF/38400A00. Foto300gris+contorno
  frente a negro082253. No acepta escena: iluminación/fogonazo/llama y plano
  final720 siguen distintos aPAL013200. Motor apagado. Batería5/5 y37tests de
  abajo son anteriores a este cambio; pendiente cierre de revisión.

### Cierre anterior — 08:25,3/11

- ENV0 original incorporado a B2TZ (13componentes,528B,perfiles/posiciones/
  reorientación/tablas solares); sello `|env1`. Consumidores nuevos:
  `src/escenarios_b2_robot.inl`, `escenarios_b2_fundido.inl`,
  `escenarios_b2_solar.inl`, integrados en motor/escenarios. Los bloqueos90/78/64
  de los antecedentes de abajo están superados como recorrido de guion.
- Robot071447: fin750/bancos=True/doble dibujo; fundidos61/62/64 implementados,
  480cuadros registrados. Aún exceso de iluminación/manos negras/fogonazo ausente.
  Snapshot material corregido: **A823106B0/B82389418**, no82399418. Ablación real
  **080158** (`--robot-sin-segunda`,grupo0/SPX0):fin750/bancos=True/pasadas1;
  foto300 tiene manos/articulaciones grises donde071447 las tiene negras.
  Acota defecto a segunda pasada, **no la corrige fielmente**; se mantienen dos
  por defecto. Estados A/B FFFFFFFF/38C00A00→EC3FF8FF/28800A00.075903no fue
  ablación (pasadas2):exportar variable de shell no llega al env explícito del
  arnés; usar la opción. Reorientación/mezcla/FX siguen sin acreditar. Tablas
  estáticas824E6540/824E6500 en cero no equivalen a estado runtime del render.
- Control robot **082253**,misma compilación/snapshotB, sin opción de ablación:
  fin750/bancos=True/pasadas2;foto300 confirma manos/articulaciones negras frente
  al gris de080158. Mismos A/B,huesos/distancias/escala registrados;569pasos y
  569pares/tarea ya eliminada al cierre. No hubo cambio de código para este control.
- SolarLlanura:074308 tenía fin527 pero dibujos0. Identificada pasada PAL1ADEA0
  exclusiva flags4;la normal1AE220 los excluye. `B2SolarHD::Publicar` después de
  BD328 selecciona tareas4, deduplica por guestframe y publica quads bajo mutex.
  Posición inicial/payload limpio/valores finitos, eliminación diferida hasta
  después del recorrido nativo. **Corrida final080741**:fin527/bancos=True,
  trasfin1s dos tareas zona1 con154/84pasos y dibujos;alpha0/ceroquads,fuera de
  pantalla. Despacho comprobado;efecto visible/UV/mezcla y dibujo inicialzona0
  pendientes. Duplicaciónzona1 también observada en original, no deduplicar por
  posición. PAL072456:498muestras/118con tareas;139comparaciones/errorproyección
  ≤0,000527 en `D:/tmp/solB2X/solar_auditoria_20261103.json`,solo álgebra, no VU.
- Último build oficial `PYTHONDONTWRITEBYTECODE=1 PYTHONUTF8=1 DBZ3_AGENTE=solB2X
  python tools/compilar.py`:código0/dual→release/DLLs canónicasOK. **37/37tests
  Python PASS,0,118s** (`python -m unittest discover -s tools -p 'test_solb2x_*.py'`),
  incluidos seis de lectores solares. Sin cambios C++/Python posteriores.
- Batería FINAL de esta revisión,08:19: `PYTHONDONTWRITEBYTECODE=1 PYTHONUTF8=1
  DBZ3_AGENTE=solB2X DBZ3_TEST_CFG='gpu_backend="d3d12"' python tools/pruebas.py
  todo --max 1 --forzar`: **5/5 PASS,258s,código0,una vez**. Evidencias en
  D:/DBZ3HD/_pruebas/20261103-081459_smoke_a_tag_swap,081634_smoke_a_campana,
  081737_smoke_a_du_continuar,081821_smoke_a_batalla,081854_smoke_a_menu.
  Partidas de las copias respaldadas. Batería06:32 y31tests son históricos;
  no confundir regresión general con aceptación de escenas B2. Motor apagado.
- Continuidad: corregir la equivalencia de estado de la segunda pasada robot
  con datos de render runtime; completar reorientación/FX. Solar necesita toma
  original/HD comparable con intensidad positiva y cobertura inicial. Detalle,
  comandos y archivos propios en informe/censo. Sin publicar cambios.

### Antecedentes de la sesión (estado histórico; prevalece el cierre anterior)

- RR tiene dos guiones: **SPX0 robot, zona0→0**, y **SPX1 puerta, zona0→1**.
  No exigir cambio de zona al robot. Curvas originales completas en
  `D:/tmp/solB2X/original_g0_e0_20261103_013200` y `original_g0_e1_20261103_013016`.
- Arranque RR corregido: suelo original MAD&1 (HD consulta&2) y consumo del
  lanzamiento20 antes de que82103908 lo cambie a21. P+K real=teclasL+P;
  HD20 usa reloj actor+6196, HD22 usa+3256, umbral estricto+6192.
- Puerta (medida anterior al nuevo reloj de Nieve): cámara llevaba un cuadro de
  retraso; se movió el incremento después deTick/antes del muestreo. Esa revisión
  ha sido reemplazada por reloj antes deTick y cámara independiente (ver abajo).
  Medido en `20261103-014946_solB2X_lanzamientos_grupo0`:
  fin217/bancos restaurados;35 pares observados sin ajuste de fase, error máximo
  de ojo7,11321→0,00259332. Las216 muestras HD coinciden con AMC en relojVM.
  No acredita aún actores/sonido/escena completa. Usar reloj observado: PAL avanza1,2.
- Robot inicia, bloquea0x90/PC4F/cuadro0. Tabla PAL375190 usa dos niveles por nibbles.
  Nativa90→1B77B0→1B2BC0; paso1B2990/dibujo1B29D0: luz desde hueso10 y doble
  dibujo con estados temporales. La lista1AE220 admite flags400; eliminación1AF9B0.
  Falta ese consumidorHD; compartir prioridad/flags no prueba equivalencia con
  820BF768 ni820BFA20. Llanura sigue bloqueando78/cuadro457 por solar sin adaptar.
- Auditor robot: `tools/solb2x_robot_luces.py`, tres estados PAL/perfil6/tipo0/
  flags1003, GP3ABCF0; color/ambiente y dirección normalizada contrastados conELF.
  HD real `20261103-035903_solB2X_lanzamientos_grupo0`: perfil0 del anfitrión16
  vaciado, dirección[.58,-.58,-.58] distinta dePAL[.58,-.8,-.17], cinco luces
  0103/0203/0403/0803/1003. MáscaraHD0100 **sí admite la primera**. No concluir
  fallo de máscara aplicándola a flagsPAL. Color/ambiente cero solo en los dos
  puntos leídos, no en un dibujo del robot. CallbackFB8A8→136538→tabla8201FA84.
  TablasFA84/FA9C extraídas en `D:/tmp/solB2X/actualizacion_luces_hd_20261103.json`;
  tipo5 actualiza por82136BF8→88EC0→88898/88B40, con escritura de color+32.
  Setter ambiental821B2278 y caller821B3F98 localizados, sin ejecución acreditada.
- Nueva medida `20261103-042932_solB2X_lanzamientos_grupo0`: cinco controles
  tipo0, todos+12=0, destinos367A8/367E0 y enlaces correctos, idénticos en ambas
  lecturas. Con ese parámetro367E0 retorna sin copia; no hay tipo5 seleccionado
  en esos puntos. No es traza de ejecución ni dibujo. `--luces` lee también
  controles US; **18/18 tests HD +15/15 PAL**. Resumen con hashes en
  `D:/tmp/solB2X/controles_luces_hd_cierre_20261103.json`. Bloqueo90 intacto,
  bancos restaurados, proceso cerrado; sin cambio de runtime ni escena aceptada.
- Contrato robot: `tools/solb2x_robot_contrato.py` y su test, **9/9 PASS**;
  `D:/tmp/solB2X/robot_contrato_final_20261103.json`: perfiles PAL/US porSHA,
  dos COP1.S/ACC y574 instrucciones PPC, tres llamadas indirectas sin destino
  inferido. PAL266A50 usa .015/.35 y término bajo fuera del factor de cámara;
  HD139BD8 usa .01/.30 y aplica el factor a toda la mezcla. S/T simbólicos,
  ejemplos algebraicos, no replay FPU. **140A40↔8F438 es candidato estructural**,
  no8F7F8; matrices/modelos/estados aún abiertos. C8068 no sustituye1B29D0:
  pasadas adicionales;1391A0 vuelve a dibujar8F438 y139430 llama porCTR en82139514.
  Contraste anterior:1005 instrucciones HD/0 discrepancias ASM/27 hashes de texto,
  `robot_contrato_binario_20261103.json`; solapa cuerpos, no sumar cobertura.
  Siguiente: matrices y estados por separado, conservar fórmula PAL y cerrar94
  antes de adaptar90. Ninguna escena nueva aceptada; sin nueva partida/build.
- Continuación94 (05:24): `tools/solb2x_control94.py` y su test;26 guardas PAL,
  496 palabras/80 instrucciones PPC, `D:/tmp/solB2X/control94_contrato_final_20261103.json`.
  Sentinel **enteroFFFFFFFF**:1B9020 compara registros; el «-NAN» de Ghidra engaña.
  1B908C guarda float sin truncar: corregido `escenarios_b2_fondo.inl::Controlar`
  para escribir `p[1]` directamente. PAL1AF9B0 elimina todas las coincidencias
  usuario/flag400; HD BFBA8 solo la primera por usuario.90 comparte usuario/flag400
  con94: integrar su eliminación antes de habilitarlo. UV sin AMM/restauración
  aún pendientes. Build0/DLLs OK, **31tests PASS**, una corrida SPX1/puerta
  `20261103-052359_solB2X_lanzamientos_grupo0`:94 ejecutada, fin217/bancos restaurados,
  juego cerrado. Cámara216muestras/35pares, errores idénticos a la anterior;
  no prueba un argumento fraccionario capturado ni escena completa. Manifiesto
   `D:/tmp/solB2X/control94_cierre_20261103.json`. Robot90 sigue faltando/apagado.
- Continuación94/UV (05:43): ya integrado `src/escenarios_b2_tareas.inl`, selección
  de TODAS las tareas usuario/flag100 con prevalidación y liberación BD570; seis
  `static_assert` del mismo selector. `Vincular/Controlar/Devolver` conservan y
  procesan UV incluso sin AMM. Build0/DLLs OK,31tests PASS. Puerta053643 fin217,
  bancos=True, raíz5 elimina0/raíz3 elimina1; no caso real múltiple ni UV aislado.
  Cámara216muestras/35pares sin regresión. Manifiesto `tareas94_cierre_20261103.json`
  en D:/tmp/solB2X; el manifiesto anterior corresponde a otra revisión. Robot90
  y dibujo aún faltan/apagados. Kami PAL `original_g2_e0_20261103_053834`:246muestras,
  fin249,599426; HD001652 sin traza, necesita curva dirigida. Ciudad PAL053942:
  510muestras sin disparo; no repetir igual, diagnosticar fixture. Informe actualizado.
- Nieve (HD060424): `b2_transicion.h` avanza reloj ANTES deTick (PAL1D4630),
  cámara con reloj propio/inicio al delta (1CDED0/1CDC80), sin cambiar espera4.
  Build0/31tests PASS; una corrida real, fin720/bancos=True. Cortes HD30/450,
  PAL29,999985/449,99646;115pares, zonas discordantes1→0, ojo549,73→8,43869014.
  690muestras HD contra AMC:ojo≤1,58e−5. Primer replay PAL al más cercano falló
  límite0,001 (0,1033253); auditoría hacia cero posterior reproduce600/600 relojes
  exactamente y575cámaras con ojo≤6e−5; no lee FCR31. Diferencia real inicio/paso
  PAL1,2 frenteHD1 explica los115pares: residuo vectorial≤5,79e−5 ojo/1,01e−4
  objetivo, sin mover muestras. Evidencias D:/tmp/solB2X/
  `nieve_reloj_cierre_20261103.json`, `nieve_redondeo_ciudad_diagnostico_20261103.json`,
  `nieve_reloj_descomposicion_20261103.json`. No acepta escena ni habilita motor.
- Kami: ya medida con curvaHD054630 contraPAL053834,41pares/zonas0,
  ojo≤0,002662/objetivo≤0,000513. Es ANTERIOR al nuevo reloj compartido; puerta y
  Kami no tienen regresión posterior acreditada. Ciudad PAL053942 diagnosticada:
  rival ya en540/estado13 al preparar, atacante573→71, víctima nunca cruza plano
  (mínimo13,9083); no se activó VM. Preparar un ataque en reposo sin forzar acciones.
- Ciudad resuelta la adquisición: `solb2x_original.py --esperar-neutros 300`
  exige dos lecturas0/0/−1/evento0; llega tras42campos. PAL061507:298campos activos,
  fin357,59787/zona1. HD061643:fin357/bancos=True.53pares/zonas0; ojo crudo32,5888,
  objetivo15,3657 por pasos propios PAL1,2/HD1.298/298relojesPAL exactos hacia cero,
  265PAL/317HD contra AMC≤0,000139; descomposición53pares sin fase ajustada:
  residuo ojo≤0,000115/objetivo≤0,000138. D:/tmp/solB2X/
  `ciudad_camara_cierre_20261103.json`. No valida escena completa ni lee FCR31.
- Solar Llanura: `solar_contrato_binario_20261103.json` (D:/tmp/solB2X),704palabras
  PAL/5rangos. Índices0/1:pos[−820,450,−650]/[496,660,−1375]. Tarea90000000,
  paso1AC3E0/dibujo1ABCD0:5quads/texturas8/9,alpha±0,15 y dibujo soloevento0;
  exige actualizar estado durante transición. El flare IW de círculos no equivale.
  Sigue bloqueo78/cuadro457; robot90 aún sin consumidor. Censo separa fallback
  manual FacDe/cámara fija de rueda y menús B2 sustituidos por B3, sin aceptación global.
- Compilado por `DBZ3_AGENTE=solB2X python tools/compilar.py`: código0, dual/release
  y DLL canónicas OK. Batería FINAL posterior a94/UV/nuevo reloj/Ciudad,06:32:
  `PYTHONUTF8=1 DBZ3_AGENTE=solB2X DBZ3_TEST_CFG='gpu_backend="d3d12"' python tools/pruebas.py todo --max 1 --forzar`:
  **5/5 PASS en241s**, una corrida. Evidencias D:/DBZ3HD/_pruebas/20261103-
  062822_smoke_a_tag_swap,062942_smoke_a_campana,063046_smoke_a_du_continuar,
  063128_smoke_a_batalla,063201_smoke_a_menu. Además31testsPython PASS/0,040s.
  La batería235s de `pruebas_cierre_camara.log` es histórica. No valida escenasB2
  completas;Tablero/Babidi/historia siguen con censo estático parcial.
- Herramientas nuevas `tools/solb2x_{original,estados,tabla,suelo,curvas,camara}.py`;
  `solb2x_lanzamiento.py --curva` registra VM/cámara antes de dibujo y conserva
  traza previa. Ese punto no prueba la imagen final ni audio. Todos los archivos
  propios y comandos/evidencias están enumerados en el informe; sin publicar.

## Continuidad solIWX — 2026-11-03,13:20

- **19:37 cierre visual884: NO ACTIVAR por defecto.** Ojo/objetivo de camara
  reproducidos en2estados (error max .000244140625); solo1pareja visual util,
  n17215. n17216 es color uniforme y n16875 muestra carga: descartadas.
  Archivo PNG/RAM conjunto NO garantiza fase EE/GS. Camara+UV original retenida
  PASS60.7s en `D:/tmp/solIWX/fondo_uv_focal_1793730545789743300/resultado.json`;
  camara con UV natural PASS51.7s en `...1793730371112385700/resultado.json`.
  Cielo mas oscuro/nubes aplanadas aun reteniendo UV; no atribuir todo al reparto.
  Sincronismo natural, FOV/aspecto y contratoXYZ/ambosmodelos siguen pendientes.
  Imagen revisada y hashes: `D:/tmp/solIWX/fondo_comparacion_20261103_02/`.
  _01 preliminar no descarto borde de imagen plana; usar _02. Nuevas herramientas
  `iwx_fondo_imagenes.py` y ampliacion `iwx_fondo_uv_focal.py`; sin C++ nuevo.
  Stage/user_data restaurados. Build oficial PASS/codigo0/copia exe y DLL OK;
  SHA dual/release c355d92f02a7e16b1ac665fbda9eeeddc39b0f3382244e85e2fdf9baad83ae54.
  Es distinto del exe focal; no extenderle esos resultados. Sin bateria general
  final: corresponde al coordinador. Detalle/comandos en informe19:37.

- **18:56 focal UV884 PASS:** `tools/iwx_fondo_uv_focal.py --indice 30
  --montaje D:/tmp/solIWX/fondo_montaje_20261103_03.json`. En smoke_b se aplico
  SOLO temporalmente el reparto externo:111partes autenticadas contra ZDD,
  254muestras intro/43combate;110UVquietas y1movil (hijo1/offset7712/canal6,
  295valores). RAM asincrona del primer vertice, NO pareja PS2 ni dibujo completo.
  stage.toml y contenido user_data restaurados byteidenticos; geom/id_SE55
  intactos. Captura intro disponible; sin comparacion sincronizada ni aceptacion.
  Evidencia `D:/tmp/solIWX/fondo_uv_focal_1793728479334653900/resultado.json`,
  run `20261103-185441_smoke_b_solIWX_uv_materiales` (65.6s). Exe compartido
  SHAef3ba3dbe4a3430f7ce44aea616017cbb3e2eda21f1e1b745ea7bcbde281aac7,
  distinto del usado por bateria17:19..17:23: esos5/5son historicos, no prueba
  general de este exe nuevo. Sin editar/recompilar C++; siguiente: pareja visual
  PS2/HD misma camara/tiempo y contratoXYZ/ambosmodelos, precision/mezcla/sonido.

- **18:45 fondo884, reparto UV:** modelos reales hijos1/3 emparejados con3RAM
  (`fondo_modelos_20261103_03.json`); vertices VIF/32B de ejes byteidenticos.
  `iwx_fondo_uv.py`:48casos/877instrucciones ELF PASS. Hijo3:53partes sin canal;
  hijo1:58partes, SOLO offset7712 canal6. `iwx_fondo_montaje.py`:111registros,
  **110diferencias default,0candidato**, default byteidentico al anterior (_03).
  _montaje_01 es INVALIDO por emparejar firmas en minusculas; usar _02/_03.
  Nuevo opt-in `campos_iw.py mod 884 --uv-materiales-serpiente`, no auto/sello:
  selector6..9->capas0..3;sin canal->capa-1 con du/dv/px/py=0. No reutilizar bin.
  `iwx_fondo_uv_guardas.py`:11guardas PASS sin abrir fuente/instalar.
  `iwx_fondo_bin.py`:conversion completa en memoria PASS;bin3920928B/ZDD3031264B,
  110registros corregidos, bin default/candidato byteidenticos SHA1ccef5324c33...
  Evidencias `D:/tmp/solIWX/fondo_{uv_guardas,bin}_20261103_01.json` e informe.
  **Sin mod instalado ni GS comparado**: sigue abierta la diferencia original
  flags8/anulaXYZ/ambosmodelos frente port XZ/solo hijo1, precision/orden/mezcla/
  sonido. No activar como aceptado ni cambiar id_SE55.5/5generales vigentes.

- **18:10 audio por fases:** `tools/iwx_audio_fases.py`, codigo0;
  `D:/tmp/solIWX/audio_fases_20261103_01.json`. Cuarenta entradas sinteticas
  (cinco estados, perfiles0/B/C/D y lados0/1),513instrucciones distintas
  autenticadas:PASS. Indice2->recurso941; solicita sus siete hojas entre las
  distintas fases. Entrada31:hoja0/slot1;2C para slot1, perfilC pide4/slot0;
  2E pide1(perfil0)/5(D);30 pide2(0)/3(B,C)/6(D);2F pide2. Audio solo lado0;
  camaras y estado final tambien contrastados. Servicios externos interceptados;
  no clasifica contenido, ambiente o ejecucion natural. Amplia el prefijo2C
  anterior, no lo convierte en llamada natural. id_SE55/gates sin cambios;
  bateria general5/5 vigente. Detalle y comando en informe.

- **18:06 fondo original, transformacion acotada:** nuevo
  `tools/iwx_fondo_transformacion.py`, codigo0; evidencia final
  `D:/tmp/solIWX/fondo_transformacion_20261103_02.json`. Ejecuta306320 con3RAM
  autenticadas y6traslaciones cada una:18casos PASS +3controles positivos12B820.
  En flags8, las DOS selecciones de recurso141390 conservan la base de vista
  byteidentica y anulan XYZ, incluida Y; vista/pila restauradas byte a byte.
  686instrucciones distintas autenticadas contra ELF. Dibujo GS/regulador
  omitidos, memset local; extensiones VMOVE/SUB/SLLV locales, GP2 intacto.
  Seleccion de recurso NO es dibujo observado. El candidato actual solo sigue
  XZ de partes `sigue=1`: no reproduce este contrato completo. Sigue apagado;
  falta mapear ambos modelos/origen/altura/proyeccion/UV y comparar GS.
  _01 conservado; _02 elimina UV de direcciones sin justificar y exige igualdad
  binaria de la base. Sin cambios runtime ni build nuevo; bateria5/5 vigente.

- **17:51 consumidor ADX original:** nuevo `tools/iwx_audio_consumidor.py`,
  codigo0; `D:/tmp/solIWX/audio_consumidor_20261103_04.json`. Autentica ELF EU,
  referencias y3guardados;12rangos de codigo byteidenticos al ELF en cada RAM.
  Slot1->decodificador2->fuenteCRI43A9B8->941/0 por SHA de hoja COMPLETA en
  n17215/17216 (estado31); no enlace identificado en17666 (estado2C).
  Contraste EE sintetico154casos PASS:38entrada31,54wrapperADX,38prefijos de
  presentacion hasta parada,24paradas de slots aisladas. Entrada31 pide(hoja0,
  slot1) en lado0/indice!=1; prefijo29BFF0 pide parar slot1. No se ejecuta el
  resto de presentacion ni motor ADX real; servicios externos interceptados.
  EstadoCRI[1,3,2,2] no se interpreta como reproduccion audible. Origen retenido
  supera banco cargado, pero voz/ambiente/PCM/mezcla/salida siguen abiertos;
  id_SE55 no cambia. PNG originales extraidos con SHA en _03, carpeta
  audio_consumidor_capturas_20261103_01. Sin cambios runtime/build/juegos nuevos;
  bateria17:19..17:23 sigue5/5 PASS. Detalle/comando en informe/cambios14:50.

- **17:27 cierre posterior:** `PYTHONUTF8=1 DBZ3_AGENTE=solIWX python tools/pruebas.py todo --max 1 --forzar`,
  5/5 PASS,228.11838507652283s, runs17:19..17:23 smoke_a. Auditoria offline
  D:/tmp/solIWX/cierre_tarde_20261103_01.json,codigo0: las cinco claves completas
  de cache recalculadas coinciden exactamente. Exe dual/release/smoke_a actual
  fbe2b4d0babd66c1507a6e811fa62266375840cf56dcc0e0a47cc8677b36f4b8,
  55016448B; distinto del d0f8f5f3 de las focales del fondo por build compartido.
  No repetir PASS, no revertir build ajeno; no se atribuye escena audiovisual
  integral. Bateria de nota17:24 ya cerrada. Sigue ambiente IW por consumidor,
  precision/orden/imagen del fondo y limites de cada minijuego; gates apagados.

- **17:24 fondo/audio original:** gate0 con el mismo build nuevo PASS65.0s,
  ...fondo_focal_1793721012884020000/:686UV/1XZ/551ojos,0errores. Control cerrado.
  tools/iwx_fondo_original.py:7consumidores/3guardados propios autenticados;
  D:/tmp/solIWX/fondo_original_discreto_20261103_03.json. n17215/17216/17666 es
  libpad/DMA, no video. EU1E2880 combate NO acredita minijuegos306320;
  306500 lo llama estaticamente, ejecucion/dibujo GS no observados. VSUB.yz,
  flags8,constante47DEC0=(0,0,0,1)/factor489FF0=(1,-1,-1,1) retenidos;
  no equivalencia integral del candidatoXZ ni transformacion completa.
  tools/iwx_fondo_bancos.py, ejecucion codigo0: fondo_bancos_20261103_02.json;
  941derivado de35EDCC/tabla484760 indice2,7/7ADX mono24kHz byteidenticos con
  RAM/contexto49CEA0 en3guardados. 938/0,2 y954/0,2 coinciden en slots0/1.
  Son recursos retenidos, NO ambiente/voz/contenido oido ni mezcla/salida.
  AmbientEffects?13A6B0 es matriz, no evidencia sonora. id_SE55 del port sigue
  donante B3; no sustituir por941/954. Gates integrales apagados; sin runtime
  posterior al ultimo build del fondo. Bateria general nueva pendiente.
  Informe actualizado en «Cambios desde las 14:50», tabla/abiertos tambien.

- **16:46 fondo intro Serpiente:** UV activas693/1XZ fijo, camara seleccionada
  movil561; cache mgr+190 en(0,13,50). Nuevo candidato en PasoCapas, apagado:
  exige DBZ3_TEST_IWX_FONDO_CAMARA=1 y DBZ3_TEST_INTRO_SONDA=1 exactos,
  estado1/modo7/pose local dentro de36000B/XZ finitos. Build oficial0/DLLsOK;
  focal candidata PASS55.9s,692UV/554XZ/561ojos,0errores. Evidencia
  D:/tmp/solIWX/fondo_focal_1793720698132909100/; anterior ...1793720240707203700/.
  tools/iwx_fondo_focal.py conserva combate_sonda.csv y autentica offset relativo.
  Ambiente55 del donante B3 observado en823A3E40: no sonido audible IW.
  No normal ni escena integral aceptados. Falta control gate apagado/contraste
  original/audio y bateria nueva posterior. Detalle informe/cambios14:50.

- **16:19 objetos Bubbles:** callback31A2F0 localizado (499630 es nombreASCII).
  `iw_bubbles_objetos.h` candidato SIN incluir en runtime; fase anterior al
  dibujo540casos/0diferencias con helpers142520/1424A0 ejecutados desde ELF,
  W conservado/distancia2<80/caducidad estricta/pausa/frenado/giro. Evidencia
  D:/tmp/solIWX/bubbles_proyectiles_20261103_03/. No anclas/creador/wrap296370,
  contacto natural, imagen o sonido aceptados. Detalle informe/cambios14:50.

- **16:02 Bubbles, candidato apagado:** Emitir corregido contra instrucciones
  originales318740/318A60/318E00: extras7/9 dificultad3=.5, literal.7 sin suma,
  extras solo niveles3..5. Antes144casos/8diferencias; despues168/0,78solicitudes,
  0duplicados. tools/iwx_bubbles_eventos.py + runner_cpp, evidencia en
  D:/tmp/solIWX/bubbles_eventos_{antes,despues}_20261103/. Build oficial0/DLLsOK.
  Oraculo GP2 solo lectura, entradas sinteticas tras reloj; no escena completa.
  Hueco siguiente medido: creador3177F0 emite0/2 desdeactor+730 a velocidad5;
  adaptador coloca0/1/2 inmediatamente y solo traza3/4. No trayectoria/dibujo/
  sonido aceptados; kBubblesProbado=false. Encargo siguehasta19:00.

- **Continuacion15:38, encargo hasta19:00**; cierre13:30 supersedido.
  Informe seccion «Cambios desde las 14:50». Piccolo acuse nuevo opt-in
  `DBZ3_TEST_IWX_PICCOLO_ACUSE` ademas de testmini7D8/reaccion; sin el nuevo
  interruptor sigue el camino anterior en revision. `anim_iw::{ReiniciarPoseConAcuse,
  LeerEstadoConAcuse}` vincula pedido al reinicio realmente aplicado/validado
  en Tick.44guardas C++/0fallos; build oficial0/copia dual/DLLsOK.
  Focal --rol9 --acuse PASS66.2s:14reinicios A0/dur30,2reimpactos iguales
  generaciones11/17->ordenes8/12;B ausente. Evidencia
  D:/tmp/solIWX/piccolo_reaccion_focal_1793716614935898900/.
  No paridad integral/temporal, normalfalse. Siguiente Bubbles, Serpiente y
  minijuegos visibles; incluir fondo/sonido de intros donadas, cierre19:00.

- Referencia: studies 5/ENTREGAS/INFORME_solIWX.md. Reaccion Piccolo7D8
  candidata opt-in:925/406..40B->FC8..FCD, entrada por rol/estado3 y regreso1.
  Original propio201campos VSync/impacto40A. Tabla22 y giros6 byteidenticos ELF;
 248guardas C++/0fallos,25pares naturales/error4.90e-11. Build oficial0,
  dual->release/DLLsOK. Focal PASS85.0s/2123muestras/30del clipFCC,dur30,
  retornoFB0 y22roles B3 por nombre iguales. Evidencia
  D:/tmp/solIWX/piccolo_reaccion_focal_1793704339020971400/.
- Normal=false: exige DBZ3_TEST_IWMINI y DBZ3_TEST_IWX_PICCOLO_REACCION.
  Scheduler aproximado/cadencia controlador-clip/reimpactos/imagen y efectos
  integrales abiertos; gates Bubbles/Serpiente sin cambios. Primer FAIL era
  evaluador de generacion de salida, corregido y retenido. Cierre antes13:30
  segun aviso del coordinador; bateria general posterior registrada abajo.
- Ampliacion: corregido centro orbital del candidatoZ0->ojoZ37, radio60+
  expansion;54pares originales/error1.17e-5. Build oficial posterior0/DLLsOK.
  Focales rol9/rol3 PASS54.9/56.3s, radios60..69.996. Seis variantes FC8..FCD
  observadas en motor B3; originales naturales406/40A/40B. Objetivo del piloto
  no fuerza hueso acertado. Reimpactos mismo/distinto codigo observados; logs
  completos118/85despachos,21/19repeticiones y19/20saltos: no paridad temporal.
- Corregido analizador de captura: empezar en estado3 es reaccion inicial,
  no un golpe nuevo.6capturas retenidas/0fallos; guarda de punteria corregida
  con80estados autenticados y4degeneraciones rechazadas. Auditor/resumen:
  D:/tmp/solIWX/cierre_piccolo_20261103_01.json. Bateria posterior5/5 PASS.
  Orden real de reinicio/poses/scheduler/colision/efectos completos abiertos;
  candidato apagado. No se ha implementado API de acuse de orden del motor.
- Cierre: bateria UNA vez `PYTHONUTF8=1 DBZ3_AGENTE=solIWX python tools/pruebas.py todo --max 1 --forzar`,
  5/5 PASS/209.57094s sumados de pruebas, sin colas. Runs130705smoke_b_tag_swap,
  130820smoke_b_campana,130938smoke_a_du_continuar,131019smoke_a_batalla y
  131053smoke_a_menu del3/11. Datos recuperados de los mismos registros, sin
  repetir. Auditor offline codigo0: D:/tmp/solIWX/cierre_final_20261103_01.json,
  con hashes de caches/logs/escenarios/fuentes y diagnostico aislado.
- Contraejemplo C++ sintetico compilado/ejecutado codigo0: tras nuevo golpe del
  mismo codigo, una espera no impide aceptar cuadros11/29 de la reaccion vieja
  y salir antes del reinicio nuevo. Controles terminal y codigo distinto correctos.
  No prueba retraso real de cola en juego; reproduccion_viva_del_fallo=false.
  Fuente D:/tmp/solIWX/piccolo_cola_reimpacto_20261103.cpp; resultado es
  transcripcion del stdout, expresamente identificado. Normal siguefalse.
  Proximo cambio: identidad del reinicio realmente aplicado/garantia equivalente,
  rechazar muestras anteriores y medir reimpactos vivos con esa identidad. No
  cambiarlo por mas esperas fijas ni exigir cuadro0 que puede no observarse.

## Cierre solMODS — 2026-11-03

- Referencia actual del frente: `studies 5/ENTREGAS/INFORME_solMODS.md` (evidencias, límites y borradores por issue). Las secciones fechadas 2026-10-03 más abajo conservan el contexto histórico de la migración y del skinning; no describen la versión actual.
- Corregidos y medidos: especiales de clones con `formas_donante`, cola de formas heredadas, Hyper de Goku GT SSJ4 IW (requiere regenerar roster) y remate/bucle de Dragon Rush del clon DBS con control nativo.
- Goku GT: su definitiva sí causa848 de daño en la prueba; el registro se cerraba tres cuadros antes del impacto. Corregido solo el registro y añadida expectativa de daño real al arnés.
- Archivos de implementación: `src/roster_ext.cpp`, `src/ports_guard.cpp`, `src/combate.cpp`, `mod center hd/roster_build.py`, `tools/solmods_verificar.py`. No se han sincronizado a `github/`.
- Compilación por `python tools/compilar.py`: código0, dual/release y DLLs OK. Batería final `DBZ3_AGENTE=solMODS PYTHONUTF8=1 python tools/pruebas.py todo --max 1 --forzar`: **5/5 PASS**, una vez,267 s. Registro `D:\tmp\solMODS\bateria_final.txt`; evidencias completas en el informe.
- Pendientes con límite medido: Gotenks IW sin fuente `USR/DATA_CMN.AFS`; especial de Super 17 sin identificar; Broly tiene ramas intacto/destruido trazadas pero el flash no está corregido. Falta configuración exacta del jugador para los demás síntomas DBS.
- No confundir Dragon Rush con `DBZ3_TEST_ULT`; un PASS sin expectativa de remate/daño no acredita una técnica. Una traza SPX larga puede rotar el log: preservar todos los segmentos de la corrida, como `spx_completo.log` en `20261103-001703_sub66_solmods_formas`.

---

## 0. PROTOCOLO DE MIGRACIÓN TEMPORAL A CLAUDE (LEER PRIMERO)

El mantenedor trabaja alternando **opencode** (local, este repo) y **Claude**
(migración temporal). El repo canónico es **este disco**; `github/` es la copia
versionable (sync manual con `tools/sync_github.ps1`).

### 0.1 Regla de oro
**Todo cambio que haga Claude debe volver a este disco como UN `.md` de
"handback"**. Claude **no** sube a GitHub. El mantenedor revisa el `.md` aquí y
aplica/sube desde opencode.

### 0.2 Al **entregar** el proyecto a Claude (lo que hace opencode)
1. Asegurar `git -C github status` limpio y commit local hecho (aunque no se suba).
2. Entregar: `AGENTS.md`, `MEMORY.md`, `docs/MIGRACION_CLAUDE.md` y el estado de
   `github/` (branch + commits ahead).
3. Indicar a Claude el **alcance exacto** de la sesión (p.ej. "RE rutina CPU de
   skinning") y pedirle el handback (§0.3).

### 0.3 Al **recibir** de Claude (instrucción a Claude, copiar tal cual)
> **INSTRUCCIÓN OBLIGATORIA PARA CLAUDE**: al terminar tu trabajo, genera **un
> único fichero Markdown** llamado `HANDOFF_CLAUDE_<AAAA-MM-DD>_<tema>.md` con:
> 1. **Resumen** de lo hecho (qué, para qué).
> 2. **Lista EXACTA de ficheros** creados/modificados/borrados (rutas relativas al
>    repo) y, por cada uno, **qué cambia** (idealmente un `diff` unificado o el
>    contenido completo si es nuevo).
> 3. **Comandos** que ejecutaste y su resultado (build/tests).
> 4. **Qué NO pudiste hacer** o quedó a medias + riesgos/suposiciones.
> 5. **Siguiente paso recomendado**.
> - **NO** hagas push a GitHub. **NO** reescribas `AGENTS.md`/`MEMORY.md` salvo que
>   esta memoria lo pida explícitamente; en su lugar, propón el cambio en el
>   handback. Entrega ese `.md` como único artefacto accionable.
> - Usa rutas relativas y evita datos personales (rutas `C:\Users\...`).

### 0.4 Al **volver** de Claude a opencode (lo que hace opencode)
1. Leer el `HANDOFF_CLAUDE_*.md`, aplicar los diffs/ficheros indicados.
2. Actualizar `MEMORY.md` (esta sección + §2) y, si aplica, `AGENTS.md`.
3. `powershell -ExecutionPolicy Bypass -File tools\sync_github.ps1` (corre
   `publish_check.ps1`; exit 1 = fallo) → commit → **push solo si el mantenedor lo pide**.

---

## 1. ESTADO A 2026-10-03 (histórico: el vigente está en los cierres de arriba y en `studies 5/`)

### 1.1 Git
- Repo versionable: `github/` (NO es el repo de trabajo; se sincroniza manual).
- **`master` ahead 10** respecto a `origin/master`; **sin push** (esperar OK del usuario).
- Últimos commits locales: `f753948` (RE skinning), `a30c8ac` (publish_check),
  `5fc4821` (Vía A + fix normales), `7c2fd6c`, `0394903`.
- Entorno de build limpio: sin procesos, toml sin flags dev, DLLs canónicas
  (`rexgpu-xenos.dll` 6360064 B / `rexruntime.dll` 10920448 B).

### 1.2 Releases
- **v1.3.0 = Latest** (2026-09-30). Todo lo demás no-Latest (ver AGENTS §3.0/§9.2).

### 1.3 Vía A (inyección PS2→HD) — entrega aproximada
- Funciona; mejor resultado `cell_win2`.
- **Defecto brazo/cabeza = sombreado de geometría aproximada** (NO bug de
  rotación de normales). §23.
- **Fix de normales**: `awo_tools/awg_normal_fix.py` (copia `nrm` nativo HD sobre
  el bin del port). Mod `cell_nfix` validado en runtime (demo limpia). §23.1.
- Comparador: `awo_tools/awg_diff.py`.

### 1.4 Vía B (port completo) — 🔴 DESBLOQUEO DIAGNÓSTICO 2026-10-03
- **RE del skinning (nuevo, §24)**: se instrumentó `rexgpu-xenos` (REVERTIDO) y
  se volcaron shaders + constantes por draw. Resultado **duro**:
  - **Ningún VS indexa constantes dinámicamente** (sin `a0`/`arl`/`lc`) → **sin
    paleta de huesos en GPU**.
  - **Ningún shader usa `memexport`** → sin skinning GPU/“compute”.
  - El VS del cuerpo (`FDF960B5D7869030`) es **transform rígida** con **una sola
    matriz 4×4 en `c0..c3`**, **idéntica** en los ~33 chunks (`indx_offset=0`,
    counts 3..1995).
  - ⇒ **El skinning es CPU-side (Xenon)**: el guest lee bind-pose, aplica
    matrices de hueso y escribe **world-space**; el VS solo proyecta.
  - **Refuta la hipótesis "causa = shader de skinning"** (§3.4.6/§10 y §22).
- **Nuevo foco**: RE de la **rutina CPU de skinning en el código recompilado**
  (qué espacio/orden de vértices espera el guest). Ver §2 "SIGUIENTE".

### 1.5 Bloque C (lint pre-release)
- `tools/publish_check.ps1` (reimpl. de `um publish check`): FAIL por ficheros de
  juego/build/secretos; WARN por huellas de decompilador, rutas personales, sin
  README. Integrado al final de `tools/sync_github.ps1`. Auto-excluye su propio
  fichero. Estado: **PASS (44 WARN, 0 FAIL)** sobre `github/`.

---

## 2. SIGUIENTE A 2026-10-03 (histórico)

1. **RE de la rutina CPU de skinning** (Vía B) — el bloqueo real. Localizar en
   `generated/` (US) / `generated_eu/` (EU) el código que lee los vértices del
   bin y aplica matrices de hueso; determinar el **espacio/orden** esperado
   (bind-pose por hueso). Candidato histórico: `sub_82087F58` (AGENTS §3.4.10).
   - Medida auxiliar: volcar el **VB final world-space** del draw del cuerpo
     (no el bin) para comparar port vs nativo en el mismo frame/pose.
2. **Decidir `cell_nfix`** (adoptar como mejora Vía A reproducible vs dejarlo en
   línea de trabajo).
3. **Push** de los 10 commits locales (espera OK del mantenedor).
4. Vía A opcional: refinar inyección (`--bone-aware`/`--normal-only` por zona).

---

## 3. NO REPETIR / TRAMPAS (derivadas de esta sesión)

- La hipótesis "el problema del port es el shader de skinning" está **REFUTADA**
  (§24). No volver a RE del shader para Vía B.
- `dump_shaders` (cvar) vuelca el ucode **bin + disasm**; `DumpUcode` ya está
  cableado en `translator.cpp:340` (no hace falta cablearlo).
- El game NO avanza de pantalla con `PostMessage` (falta `has_focus_`). Para
  navegar: ventana **visible + `SetForegroundWindow` + `keybd_event` real**
  (`SendInput`); con `long_run.ps1` la ventana va oculta → los Return no entran.
- Los logs de captura por draw pueden ser **cientos de MB** (656–730 MB);
  leerlos **en streaming** (`Select-String -Context`), nunca con arrays completos.
- Cualquier instrumentación del SDK se **revierte** y se recopian las DLLs
  canónicas (`tools\copy_sdk_dlls.ps1`); verificar el tamaño contra `AGENTS.md` §7.

---

## 4. REFERENCIAS CLAVE
- `AGENTS.md` — referencia operativa completa (formato, comandos, invariantes).
- `docs/MIGRACION_CLAUDE.md` — handoff detallado + plantilla de handback.
- `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` — §21 (oráculo), §22 (VB
  runtime), §23/§23.1 (Vía A + fix normales), **§24 (RE skinning, nueva)**.
- `docs/07_ports/UNIVERSAL_MODDER_2026-09-30.md` — origen del Bloque C.
- `docs/RE_MASTER_2026_09.md`, `docs/HOJA_DE_RUTA_2026_09.md`.
