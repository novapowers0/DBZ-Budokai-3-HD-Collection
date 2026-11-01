# sol-IW9: referencias verificadas y límites de las medidas

Fuente estática: `E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c` y recursos del jugador. Direcciones EU salvo indicación. La numeración de líneas cambia al anotar el decompilado: buscar por dirección/nombre. Evidencia de ejecución en `D:/tmp/solIW9/`; cada banco contiene fuentes, ejecutable, DLL y SHA256.

## Recompensa y paneles
- `285F30 LoadWaitRewardScreen`: EARNED fija cifras desde `t>120`, TOTAL desde `t>=320`, intervalos20; A salta420 y otra A confirma. El registro B3+12 representa A con4000, no1000 de XInput.
- `287040 s_FIX_MSN_RESULT`: AMA5/4 objetos0,5-rango,7,8,10,12 y filas por tipo. Los árboles AMA siguen avanzando después del conteo420.
- NB006 `377A00` y NB014 `32F0F0`: tres registros tipo3, ofensiva/defensa/total%. +40 es metadato, NO una cuarta fila RANK.
- AMA5/4 objeto7: su raíz360 no tiene hermano; partir de hijo380. Flechas:580/5A0/5C0/5E0; etiquetas600/620/640/660. Etiquetas en5/7: NB00620/21/22; NB01452/53/54.
- Porcentajes: objeto17, raízF20, signoF40, cifrasF60/F80/FA0; glifos segmento78. Separación65 obtenida de las flechas. Las filas estadísticas tienen su valor final desde el principio.
- Marco y bandas salen del AMA; no añadir rectángulos de alfa escogido. Los FX08/10/20 sin claves también heredan muestras.
- READY campo: `2A4BC0` usa código0 del banco915,100cuadros; 204/0 es otra guardia de61. Raíz915/0 coincide a1,237° con el final922/51: NO aplicarle un giro de90°.
- `299CA0/2AA0A0`: matriz del actor, +Z→normalizar(-.4,0,.92). La mirada B3 no basta para fijar la matriz dibujada.
- UI: lienzo600x450; GS nativo512x448, coeficientes512/600 y448/450. Los PNG originales del nuevo vuelo miden320x240, no400x300. No aplicar una corrección global0,8 desde el tamaño mostrado por el visor.

## Vuelo/AME
- Edad: G=`*(49CE3C)`, G+518 milisegundos; transición G+550→0 libera READY. G+52C es la voz: acaba sustituyéndose por99999 y no sirve como condición de salida.
- Original PAL:50campos/s,1,2unidades lógicas/campo. El nuevo original conserva51 lecturas consecutivas,0..1000ms. El Screenshot del estado puede corresponder a un campo de presentación anterior a la RAM (TIME difiere20ms).
- Original vuelo025 es una toma de vuelo libre. En050 ya se recoge el primer aro: estado5, código330 desdecampo49, efectos10/14 y mensaje rival. Su fondo gris NO se explica por cámara/FOV degenerados; no reproducir ese blanco por suposición.
- Cuaterniones/vista originales en G+650, proyecciónG+690. `*(49CBBC)` vale0 en estas tomas: no usarlo como cámara del vuelo.
- Paquete891/28:13 nodos afector, tipos0/2/3/4=5/1/2/5. Ocho pendientes eran nodos, no ocho tipos. Hay emisores anidados: conservar instancias/acumuladores propios, no aplanarlos como emisores raíz.
- `2715D0`: a=v*fuerza, rotación si flags2; p+=a*dt²/2, v+=a*dt. `271510`: p+=v*fuerza*dt.
- `271440`: r=p-centro, centro+=origen si flags1; p+=normalizar(r)*fuerza*dt/pow(longitud(r),exponente+44).
- `2712F0`: eje=vec3(+40), rotar si flags2; w=eje×r; v+=dt*w+dt*(eje×w). NO normalizar el eje ni sustituir el segundo dt por dt².
- `271210`: a=normalizar(r)*fuerza; p+=a*dt²/2, v+=a*dt. Vector exactamente cero queda cero en VU; no introducir un radio epsilon.
- Flags1 no habilita/deshabilita afectores: significa centro relativo. El tipo5 no existe en este paquete.
- Emisor2F0 bajo270 del efecto17: dura2, emite solo mientras edad<2; conserva sus partículas después de morir. `+58` hereda la velocidad del padre, no multiplica la velocidad aleatoria generada.
- Efectos fijos nacen antes de la intro. `Reiniciar` al acabarla no debe volver a crearlos. Mando digital arriba equivale a invertir s8=-128, no127: lo segundo reduce al cuadrado el empuje.
- GS44=Cs*As+Cd*(1-As);48=Cs*As+Cd;42=Cd-Cs*As;06=Cs-Cd*As. Negro/alfa no reproduce42. `iw_gs.h` usa pipelines propios D3D12 y recursos/descriptor con vida ligada a envío GPU; orden mundo→AME→HUD, debajo del menú del SDK.
- ABI R15: el header instalado de D3D12UIDrawContext omite los campos públicos de profundidad; el header del SDK activo sí coincide con las DLL canónicas. Leer el antiguo produjo el fallo de punto3; corregido en punto3b. No convertir Tex::id en un recurso D3D privado.
- Sigue sin demostrarse equivalencia del generador aleatorio completo, singularidades/subdesbordamientos VU ni todos los emisores/mezclas en otros paquetes. No deducir igualdad visual de13 recursos cargados.

## Combo, CHRFADE e IOP
- `330410`: combo acertado→poses915/57C y57D, zoom, comando1D y sonido grupo1/id21. No resta HP ni llama CHRFADE.
- Zoom `2EDC70/2EDBA0 →2EDDD0/2EDF00`: dos fases5/5; paso angular2*0,017444444444444446, noπ/180. El primer campo del cuerpo aún conserva FOV previo. Original mínimo a cuerpo7,2PAL; port anterior5NTSC y nuevo6NTSC. Comparar unidades y orden, no presentar7,2como segundos. Profundidad medida nueva0,441977620 frente0,441977650.
- Comando1D→tabla4987B0→2F7480→recurso0F: AME933/15, atlas933/0, posición(1,423900008;41,864269257;0)+traslación común. Sus emisores retrasados14/16 deben permanecer vivos antes de la primera emisión.
- CHRFADE `2EF640/2F2D70` es una tarea de color15/15/0,95, no copias de malla. B3US82109D90/EU82109E90 consume intensidad6260,modo6264,colores6268/6272 y eleva la intensidad al cuadrado. La transición nativa anterior US82111D58/EU82111E58 puede pisarlos; el nuevo gancho reaplica el valor antes de la lectura del dibujado.
- IOP954 hijos0/1/2: cuerpo/índice/SESQ. Sonido21→secuencia21→programa0,tecla33,velocidad65→VAG49(73). 5068muestras fuente, frecuencia nominal16000, pitch555, ganancia seca4225/16384, ADSR80FF/5FCB.
- El flujo SESQ es `00 90 00 21 41 00 FF`. Tabla de secuencia incluye su máximo4D. Setb410→banco424→programa42C; stride50, tonoAA6. Vagi50→registro3E0→cuerpo9FE00. FinalA0940, excluir rellenoA0950.
- El lector reproduce esta secuencia real y rechaza flujos no soportados. La interpolación, ADSR y reverb del sintetizador IOP completo NO quedan certificados por decodificar el VAG; las pruebas silenciosas tampoco certifican audio audible.

## Rectificación SPX993/994
- Son SPX directos de3088/2148B, no AMB ni guiones de agarre. Su tabla externa es484930:75→283130→13FA20→RPC13 de AMSOUND, arrancar MIDI0 del slot.74 es nulo. Hang/Ground son121/120 de la tabla de combate4582C0.
- VM145CD0: código enU32(+14)=24, una entrada pública(+18=1), entrada0(+20=0). 993/994 requieren19 opcodes:01,06,08,09,0A,0B,12,13,33,51,52,72,73,76,80,90,91,95,96.
- Se necesitan32hilos, prioridades3000+desplazamiento en[SP+4], inserción estable y ejecución de hijos posteriores en el mismo pase.96 reinicia TODA la instancia. Las llamadas pendientes deben volver a su callback conservando argumentos/locales: no resolverlas como una pausa genérica de un cuadro.
- 993:71(0) en66,73 en70,72(15) en79,73 en83,119-ready en8A; crea hijoAAC con prioridad+22 en255; hijoAB3 ejecuta75(0). 994:71/73/72/73 en2B/35/3E/48; hijo6A3 creadoen271 ejecuta75(0) en6AA.
- No existe una espera777/850/880 delante del sonido. Empieza en el primer pase de hijos después de registrar de verdad audio. A0 arranca reloj49CB9C, A1 consulta, A3 espera ABSOLUTA; los hijos mantienen animaciones y callbacks.
- 993 observa Start y reinicia en25C/3A5; la UI confirma, detiene993 y arranca994 entrada0. Consumidores40/42/44/45/46/47/48/50/60/62/63/67/90/93/94/95/96/97/A0/A1/A3/D4/D5/D6/116/118/119 tienen estado real: devolver1 siempre cambia la escena.
- SOUND_REAL5C08A0 tiene registros24h. Registro físico15: 993→MIDI957/instrumentos956/muestras955;994→960/959/958.71(0) usa registro0+15;72(15), instrumentos y muestras de15.
- MIDI957/960: chunkIECSidiM30, secuencia0en44,flujo4A,480ticks/negra,tempo800000µs/negra. Órdenes80/90/B0(controles7/10)/C0/E0/FF51/FF2F con running status. Byte de datos con bit7 omite el delta siguiente; note-off lleva solo tecla. NO es SMF estándar ni el SESQ de siete bytes de954.
- 957 tiene6 notas;960tiene5, todas tecla60, bancos22050Hz. Resolver Prog→región→Sset→Smpl→Vagi; aquí termina en VAG del mismo índice, pero por tablas. Falta portar ese secuenciador polifónico y los consumidores de pantalla para certificar ambos SPX. No inventar un ancla Hang ni disparar el audio a un tiempo escogido.

## Alternativas
-30 entradas de campo: bancos925/929/932 para118..11A y17C..17E;920/924/928/931 para6F4..6F6. AliasA40..A5D, sin sobrescribir códigos repetidos.
-27 entradas NB012:924/929/932, códigos4B0/4BA/4B1/4B2/4B3/4C4/500/501/502. Las cinco primeras deKrilin usan el almacén común204 ORIGINALMENTE; no son ausentes ni animaciones prestadas deVegeta.
-12 entradas MSN019:926,76C..770/776..778/780/7BC..7BE. AliasE15..E68 para las39 de botones. Se comprueban69 recursos y el alias resuelto en el cuerpo del actor real.
- Cámaras por personaje de915/2, índice y actor elegidos antes de cargar. Bubbles Goku28A/Piccolo293/Ten28D/Yamcha290;Snake2BC/2C5/2BF/2C2;RadarGoku546/Gohan54F/Krilin558/Trunks552/Vegeta55B. Éxito+1,fracaso+2. Antes varias cámaras de éxito eran las deYamcha paraGoku.
- NB012 READY452/44F/455 paraKrilin/Ten/Yamcha; rondas169/15F/155..+8, final favorable+9; resultadosREADY+1/+2. Mapa12 paraKrilin/Yamcha,4paraVegeta/Ten. Golpe66/123/185, recuperación36/34/34.
- MSN019: Videlneutral5AA/mal5AB/bien5AC;Satán5AD/5AE/5AF. Estado4→7BC,5→7BD,7→7BE; conservar las dos fases y esperar su duración nativa antes de entregar al conector.
- Remapeo por sufijo de hueso, no índice. La carga y entrada real no certifican por sí solas capa/ropa/espada/jet ni cada instante de ambos finales. El ACM global no contiene todos los sufijos auxiliares propios.

## Operación y comandos
- Compilación única: `python tools/compilar.py`.
- P1: `python -B tools/iw_sol9.py lanzar1`.
- Tandas y capturas: `python -B tools/iw_sol9.py --help`; todos los juegos usan las copias fijas del arnés y su cola compartida. No contar procesos ajenos como parte del límite propio.
- PCSX2: `tools/iw_sol9_original.py`; la configuración se respalda/restaura incluso si falla la conexión después de arrancar. Fotografías y estados se conservan; no hay publicación ni sincronización a github.

## Actualización final que sustituye hipótesis anteriores
- Proyección del vuelo demostrada con `12A820→12AC30→12B820→12C9E0` y el estado025: `u=300+512X/Z`, `v=225+512Y/Z`; FOVvertical `2atan(225/512)=47,44647567°`, horizontal4:3 `60,73527397°`. Aplicada en cámara activa antes del dibujo; el40 de297140 no era la proyección del visor.
- Cámara339620: `A=P+(0,7,0)`, `H=(sin(yaw),0,cos(yaw))`, `F=(sin(yaw)cos(pitch),-sin(pitch),cos(yaw)cos(pitch))`, `ojo=A−52,2F−zoomH`, `objetivo=A−zoomH`. No sustituir ambas traslaciones por una distancia52,2+zoom ni corregir silueta/escala de objetos.
- Pareja de cámaraUS820F4300/EU820F43F0 verificada en28 instrucciones y sus llamadores; colorUS82111D58/EU82111E58 en179 instrucciones. No desplazar6260 por el cambio de dirección de función. PruebasPPC-EU comprueban MD5 y `using EU image config`, no solo el selector de AFS.
- AMA6/1 objeto24 no tiene un tinte oscuro que recuperar: raíz/herencia neutras. El gris sobre la cara procede del aroOPACO, Z30 sobre cara/marcoZ50. Se dibuja marco→cara→aro→relleno190/sesgo6. Selector de caraE00 debe fijar el fotograma por personaje, no avanzar su tabla29..38 con el relojREADY.
- `Memory(n)`127CA0: repetir n veces `S=(S<<1)|(((S>>31)^(S>>4))&1)` y devolverS−1; byte=`Memory(8)&255`. Sustituido el LCG; no se afirma que la semilla efectiva coincida con el RNG global del juego original.
- Emisor radial275350: `w=(R sinφ,0,R cosφ)`, `d=(sinα sinφ,cosα,sinα cosφ)`, `offset=R(q)w+(base+azar·variación)R(q)d`; velocidad sobre `R(q)d`. R=N7C, inclinación=N80. Azar independiente por hijo, acumulador/tope propios y ningún consumo sin plaza.
- Tipo0 es sprite con proporción `1+N64`; tipo1 añade giro y signo aleatorio. Tipo2 usa longitudN5C→60, semianchos64→68/6C→70, colorbase80→90 yextremoA0→B0, UV C8..D4 yderivaD8/DC; gradiente, dirección positiva de velocidad y recorte del plano cercano implementados. No desplazar el color base aA0 ni leer sus componentes comoUV.
- El efecto0 de impulso se simula local: marco de dibujo `G+5D0`, Y400 y su giro; no añadir403 ni trasladar todas las partículas cada tick. Entrada0/3→5 lanza SOLO0;6→5 lleva0+2;33ECC0 sí lanza1+2 al entrar6, matando0 solo desde5.
- RAW EU/USA de891/28 yAME0/1 iguales byte a byte. AME0 SHA85f5150522f81842918899c8789743a3197ed8328085c1d8b4db6054d1a5e108; AME1 SHAb12bbcfd3a1066487e1f3e5fd31fa71ce82b25c6383a2d8cee542b3ab05d80f3. Vidas30 de los primeros modelos e inclinación−10 ya están enRAW; no atribuir20/0 a una diferencia regional o modificación CPU.
- Pase GS propio: texturasREPEAT como `ps2::Crear`; CLUT enteramente sin alfa se trata RGBopaco, también al subir los recursos propios. Reutiliza descriptores solo después de completar el envíoGPU. Quedan límites del renderer host: UV afines tras proyección, algunas orientaciones/flags de otras familias y falta de igualdad del estado RNG; no se certifica toda la pantalla por el hito1s.
- IOP: añadido ADSR entero15bits a48k y dos canales explícitos para pan64. Los ficheros5f prueban resolución y generación dePCM con ADSR; la salida queda silenciada y no se certifica waveOut/SPU2. Interpolación lineal del host y reverb no equivalen todavía al sintetizador SPU2 completo.
- Recompensa: el contador permanece420 mientras el árbolAMA alcanza450; tres recorridos5e PASS. NB006 original usa actor49CECC, no49CE08(NB014): condición del helper corregida y original90/320/420 capturado en `original_panel_0x7D4_1793412044587753500`. Un fallo de importación del helper dejó su proceso21764; se comprobó padre24524, se conservó la imagen y se cerró exclusivamente ese proceso, restaurando configuración.
- `audio_punto5f.json`: combo/ADSR, actor/AP y comando literal `python tools/pruebas.py todo --max 1 --forzar`, todos código0, general5/5 en464s. La comprobación inicial dejó exe/DLLs iguales a5f y todas las fuentesIW iguales; el log de la tanda conserva ese JSON. Una compilación posterior ajena cambió los dos exe de trabajo, sin cambios de fuentesIW ni DLL; se conserva5f, no se restaura un binario viejo sobre el trabajo ajeno.
