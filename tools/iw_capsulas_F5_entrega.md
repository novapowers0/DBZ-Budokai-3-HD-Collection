# solIWF / F5 — audio real conectado; referencia PCSX2 y correcciones AME/GS

Estado: **audio del bloque conectado y probado; AME simulado contrastado con RAM real; dibujo GS completo pendiente**. Esta entrega sustituye los pendientes de API de audio de F3/F4. No se activan las pantallas normales incompletas.

## 1. Audio: API de produccion, sin dobles

- `PruebaCapsulasSpx` conecta siempre `VolumenSpu2(0,v,v)` y `(1,v,v)`; se retira el `#ifdef DBZ3_IW_SPU2_LISTO`. `detener_adx_anteriores` devuelve el **bool real** de `iwadx::PararTodos()`.
- RPC14 utiliza automaticamente `Mezclador::Posicion(handle,uint64&)`; la prueba verifica que `midi.posicion_audio` esta vacio. No se sustituye cursor, mezclador, MVOL ni registro ADX.
- **PASS115**: RPC13/14 de993 y994 con PCM real; cursor0->48000, pausa sin avance, cola ADSR y silencio posterior; cursor de fuente24k; MVOL de ambos cores/canales frente a los9 registros de AMSOUND originales; ganancia host0.25 independiente; ADX iniciados por `iwadx::Oir` retirados mediante el registro real, preservando MIDI y otra voz noADX; nueva insercion y retirada idempotente. Incluye el nuevo paso RNG del kernel con valores de RAM reales.
- Sin dispositivo/altavoces. Se compila el bloque **actual de produccion**, extraido por el arnes previo; solo las fronteras SDK/GPU y carga de fixture AFS son soporte del test. El task quieto de transicion es estimulo de control, no un sustituto de las API de audio.
- No se editaron `iw_sonido.h`, `iw_adx.h`, `iw_iop.h` ni sus registros/decodificadores. No se repitio el oraculo DSP/MIDI F3 completo.

## 2. Original real: PCSX2, instancia propia y cerrada

- PCSX2 **v2.9.103**, **kit/PINE28017**, `DBZ3_PCSX2_INSTANCIA=kit`, `DBZ3_AGENTE=solIWF`. Nunca principal ni adopcion/cierre de instancias ajenas. Se respeta el cupo de pruebas.
- `tools/iw_capsulas_F5_pcsx2.py` usa `tools/pcsx2_cuadros.py`: `Cuadros`, contador IW PAL validado `0x4A4D40`, FrameAdvance y SaveState PINE sin avance durante captura. BackendNull, OutputMuted y StandardVolume0 verificados; ventana colocada explicitamente en DISPLAY2, capturas a0.5.
- Preparacion: estado8 de la copia propia; se escriben **datos RAM** de la peticion13/parametro0 y callback de fin listo, equivalentes a la salida de `Transition_ApplyQueuedParams2ADDE0`. Cleanup,45cargas,281D20 y guiones se ejecutan nativos. Se registran las tres escrituras; no se modifica codigo ni se instala una pose/guion alternativo. Esta preparacion no prueba la navegacion completa hasta el deseo.
- Tras6s de avance nativo para CDVD/IOP, se vuelve a pausa y pasos exactos. Capturas en **19653,19654,19678**; contador antes/despues y contador del estado guardado iguales. Las dos primeras son negras (arranque); la tercera muestra las **siete bolas y el suelo**. No son tres imagenes distintas ni una comparacion final con el port.
- Bancos SPX993/994 en RAM coinciden con los del AFS, salvo la comprobacion separada del magic. La secuencia inicial se enlaza por work+D8 (993); no se afirma que se haya recorrido visualmente994 completo.
- Codigo en RAM de los tramos1B09D0/133770/277150 coincide con el ELF original. Shader activo en19678: **0x706**;4752B de `vu1MicroMem.bin` son identicos al microprograma extraido por nuestro cargador (tambien existe un alias de esa misma carga). Los programas residuales de las dos capturas negras no se toman como dibujo de CapsulePick.
- **Cierre returncode0, `pid_final=null` y configuracion original restaurada**. Comprobacion posterior: solo quedaba una instancia `esc` ajena, que no se toco.
- Evidencia principal: `D:/tmp/solIWF/pcsx2_F5_1793463609132986800/{medida.json,ejecucion.log,estado_detallado.json,capsulas_*_05.png,respaldo/captura_estados/*.p2s}`. Se conservan los estados EE/IOP/VU/GS originales; no se copian al repo.

## 3. Diferencias reales corregidas

1. **PAL**: RAM original `5509E8=1.2000000476837158`, `5509EC=0.020000001415610313`. `Ame::Cuadro` admite paso/dt originales; default del host sigue siendo un paso logico60Hz. No se confunde campoPAL con cuadro logico.
2. **Precision**: comparar contra RAM real revelo64 diferencias con el orden RNG correcto. Se corrigen las operaciones finitas COP2 hacia cero y el producto de MUL/MADD usado en posiciones, dimensiones y angulos, localmente en `iw_capsulas_render.h`; no se cambia el modo flotante del hilo. Tambien se corrige el interprete EE de prueba: antes MADD.S2747FC seguia redondeando el producto como IEEE. **No se amplia la tolerancia3e-6** ni se declara implementada toda la ISA/aritmetica PS2.
3. **Kernel RNG**: entre19653/19654, sin particulas, la semilla avanza exactamente32bits. Entre19654/19678:3790bits =24*32 +3022 del nacimiento AME. La rama que aplica Memory32 **despues** deD5 coincide con RAM; antes deD5 deja365 diferencias. El ELF confirma el JAL14993C al final del bucle1498D0. `PasoCapsulas` consume ese Memory32 despues de los tasks, usando `iwazar::Global`, sin reiniciarlo ni avanzar en D6.
4. El cargador nativo marca el recurso resuelto como **`!AME`**. El analizador de estados lo reconoce y conserva su base cuando desaparece el primer nodo emisor; no confunde el primer hijo activo con la cabecera del archivo.

**PASS1455 contra RAM PCSX2 real**:93nodos activos/51particulas; orden, tipos, conteos, edad, flags, posicion, velocidad, RGBA, UV, dimensiones, angulo, quaternion y semilla. Se reconstruyen24campos y13actualizaciones AME; el primerD5 en el campo12 se infiere de la edad de las particulas, no de una traza de llamadas. La prueba distingue las dos posiciones del paso RNG y exige una unica coincidencia. Floats3e-6; enteros/semilla exactos.

Prueba larga con instrucciones EE originales e interprete numerico actualizado: **PASS25219 hasta1200 pasos1.0**; **PASS20867 hasta420 campos con paso1.2**. Cadenas, retirada y44particulas de vida negativa permanecen contrastadas. Son pruebas separadas de la captura RAM, no1200campos observados en PCSX2.

## 4. GS/VU: lo portado y el limite que sigue abierto

- `Programas::Elegir` consume el selector1B1130 y su tabla del ELF, con interprete escalar acotado a esa funcion; preserva delay slots, branches likely, registro de carga, callback y bandera de omision de segundo contexto. `CargarRegistro` carga tambien los programas especiales, no solo familia7 por indice.
- **PASS13574 frente a133770 original**,1802casos/modos:224omisiones nativas,543casos con callback;49registros con MPG cargados y4RET vacios detectados/rechazados como cargas sin programa. Cargar/seleccionar un programa no equivale a ejecutar toda la escena.
- Se portan los13callbacks no nulos de uniformes (y no-op), incluida expansion1B0A40 y tablas de color/parametros4C4E*/4C4F*. **PASS8961**:28casos,320B de cada bloque exactos contra instrucciones originales, con factores no binarios para ejercitar truncamiento, no solo copias y potencias de dos.
- El interprete VU/rasterizador CPU sigue **experimental y sin integrar como sustituto del pase completo**. Falta el consumidor VIF/vertices y uniformes por material/pose; contrastar latencias MAC/Q/recorte y precision GS; ejecutar ambos contextos44/42 y expansion; resolver AMT c8/cc y el dibujo AME/atlas; presentar y comparar imagen completa993/994. En particular, el lector comun de mallas puede normalizar normales/cuantizar colores: no basta repintarlo plano con un shader nuevo.
- Por ello D6 conserva el bloqueo `AME972: simulacion nativa disponible; falta dibujo GS de particulas/atlas contrastado` y los pases de actor conservan sus bloqueos GS. **No hay medida de error de imagen del port ni certificacion1:1**. Esta es la parte pendiente, no audio/RNG/cadenas.

## 5. Arnes: fallos observados y resueltos

- La sonda admitia kit pero el CLI de `pcsx2_cuadros.py` no: se anade kit a sus opciones.
- TogglePause llego casi3s despues de solicitarlo durante cargaGS: el arnes esperaba2s. Espera hasta10s **el mismo evento**, sin reenviar la tecla.
- `taskkill /F` podia retornar antes de desaparecer el proceso: el cierre espera el evento de salida de ese PID y libera su cerrojo antes de restaurar el INI. La primera instancia propia se cerro y restauro tras ese fallo; ninguna quedo abierta.
- Los primeros intentos de peticion/carga no llegaron a AME dentro de240campos: se corrigio el dato de fin de transicion y se permitio avance nativo de carga antes del muestreo. Sus FAIL se conservan; no se cuentan como referencias validas.

## 6. Comandos y ultimas evidencias

```text
DBZ3_AGENTE=solIWF DBZ3_PCSX2_INSTANCIA=kit python -B tools/iw_capsulas_F5_pcsx2.py --instancia kit --fondo medir
  PASS_REFERENCIA; pcsx2_F5_1793463609132986800/; cerrado/restaurado
python -B tools/iw_capsulas_F5_estado.py D:/tmp/solIWF/pcsx2_F5_1793463609132986800/medida.json --micros D:/tmp/solIWF/shader_F5_1793463903582062900
  RAM/microprogramas/guiones identificados; estado_detallado.json de esa corrida
python -B tests/iw_capsulas_F5_runtime.py D:/tmp/solIWF/pcsx2_F5_1793463609132986800/estado_detallado.json
  PASS1455; runtime_F5_1793466150827248200/
python -B tests/iw_capsulas_F5_audio.py D:/tmp/solIWF/midi_F3_prueba_1793456498795932700/bancos_midi.bin
  PASS115; audio_real_F5_1793466341773462100/
python -B tools/iw_capsulas_F4_original.py --frames 1200
  oraculo actualizado: ame_F4_1793465941915029300/original_ame.json
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793465941915029300/original_ame.json --solo-ame
  PASS25219; f4_prueba_1793466150851368700/
python -B tools/iw_capsulas_F4_original.py --frames 420 --paso 1.2
  oraculo actualizado: ame_F4_1793466264799898600/original_ame.json
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793466264799898600/original_ame.json --solo-ame
  PASS20867; f4_prueba_1793466341767388800/
python -B tests/iw_capsulas_F5_shader.py --oraculo D:/tmp/solIWF/shader_F5_1793462566164658300/original.json
  PASS13574; shader_F5_1793463903582062900/
python -B tests/iw_capsulas_F5_shader.py --solo-callbacks
  PASS8961; shader_F5_1793465941938166800/
```

Compilaciones **standalone** Clang/C++23/UBSan-trap; logs y codigos de salida en cada carpeta. No build del juego ni `pruebas.py todo`; la compilacion conjunta queda para el coordinador.

## 7. Archivos de esta continuacion F5

- Modificados: `src/pasarela_iw.cpp` (**solo CapsulePick**), `src/iw_capsulas_render.h`, `tools/pcsx2_cuadros.py`, `tools/iw_capsulas_F4_original.py`, `tools/iw_capsulas_F4_vu.py`, `tests/iw_capsulas_F4_prueba.cpp`, `tests/iw_capsulas_F4_prueba.py`, `studies 5/ENTREGAS/solIW/INFORME_F.md`.
- Nuevos: `tools/iw_capsulas_F5_{pcsx2,estado,rng}.py`, `tests/iw_capsulas_F5_{audio,runtime,shader}.{cpp,py}`, este informe.
- No se editaron `anim_iw*`, minijuegos, `iw_azar/iw_sonido/iw_adx/iw_iop/iw_ame`, `ps2_modelo.*`, main, net, CMake, SDK ni github. No instalaciones, publicaciones ni borrado de datos.
- Para continuar GS, reutilizar los estados de19678 y su micro/VUMemory/EE/GS guardados; el arnes ya tiene referencia valida. No partir de las capturas negras ni presentar el triangulo funcional F4 como prueba de la pantalla completa.
