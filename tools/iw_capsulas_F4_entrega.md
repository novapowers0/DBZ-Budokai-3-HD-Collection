# solIWF / F4 — simulacion AME972 integrada; dibujo completo pendiente

## Resultado medido

- `src/iw_capsulas_render.h`: consumidor propio del formato de AME972, incluyendo hijos de particula en `+24`, orden de nacimiento/retirada, atlas inicial aleatorio, flips, vida negativa y herencia. No modifica `iw_ame.h` ni efectos de combate.
- Comparacion contra **spawn277EA0 y update277830 del ELF original**, ejecutados en RAM controlada: 20 muestras entre cuadros0 y1200, **25.219 comprobaciones PASS**. Se comparan orden/tipo/cantidad de nodos, edad, flags, posicion, velocidad, RGBA, UV, dimensiones, angulo, quaternion y estado RNG. Floats con tolerancia absoluta3e-6; edad/RNG/flags/orden/conteos exactos. No se afirma soporte de todos los formatos AME ni de pasos temporales distintos de60Hz.
- La comprobacion ampliada encontro una diferencia real que no aparecia mirando solo posicion/color: tipo20000 guarda **ancho/alto** en `+50/+54`, bases en `+58/+5c`; tipos20001/20003 guardan semitamano y escala separada. Se corrigio conforme a275080/2747F0/272A30; no se ajusto la tolerancia.
- Cuadros relevantes:0=101nodos;1=51particulas/RNGD98EE273;294=58nacidas/RNGA9D2E86A;307=65nacidas/RNG3176472E;420=93nacidas;480..1200=44nodos y44particulas de vida negativa, RNGE657B7E.
- En **solo CapsulePick** de `src/pasarela_iw.cpp` se instala el nuevo `ObjetoAme`. D5 actualiza mediante `iwazar::Global().ConEstado`; cargar otra escena no reinicia el RNG. D6 captura una copia autocontenida sin avanzar, y D4 retira incluso en pausa. AME fuera del formato probado bloquea con mensaje y excepcion capturada.
- El puente real extraido de produccion pasa **329 comprobaciones**, incluida la cadena hasta310, pausa, captura seguida deD4, permanencia del estado RNG al cargar994 y rechazo de formato no soportado. Solo fronteras SDK/GPU/lecturaAFS se sustituyen en ese arnes; el bloque CapsulePick y el motor AME son los de produccion.

## VU/GIF/GS: evidencia acotada

- El mismo header contiene lector de tablas DMA/VIF/MPG del ELF, interprete **funcional experimental** de VU, materiales y rasterizador CPU GS. La UI **no usa** aun ese renderer para aceptar la escena completa.
- Pruebas controladas de familia7, indices5/53/61: **21 comprobaciones PASS cada una**. Cargan los microprogramas originales (591/595/615 pares), ejecutan bootstrap y un triangulo, obtienen XGKICK/GIF y verifican proyeccion fija4, Z, RGBA, niebla y pixel resultante. Cada caso rasteriza1triangulo/3200pixeles. El caso61 comprueba luz direccional roja y niebla intermedia; no solo geometria plana.
- Se corrigieron errores de compilacion (declaracion `auto` heterogenea y `else` ambiguo). Otro fallo era DIV del vertice de precarga en el delay slot final: VU no lanza excepcion por dividir entre cero. Se conservan patronQ/flags; usar ese patron no-IEEE en una ALU aun no soportada sigue rechazado.
- Esto **no es una comparacion de imagen/GIF con una ejecucion VU/GS de PCSX2 o consola**. Quedan latencias/banderas, aritmetica no-IEEE, recorte y precision completa de GS por contrastar. Pasar un triangulo controlado no demuestra la pantalla993/994.

## Bloqueos honestos

1. D6 con particulas visibles ahora informa `AME972: simulacion nativa disponible; falta dibujo GS de particulas/atlas contrastado`. No reenvia el estado nuevo al pintor reducido ni omite las particulas encadenadas. Una captura sin particulas es un no-op real.
2. Falta consumir y contrastar geometria de particulas/atlas, las matrices/materiales de todos los actores, selector EE1B1130, doble contexto44/42 y expansion0.025, enlaceAMT c8/cc y presentacion final. `flags` y `flags2` no son intercambiables: AMO970 usa1B5 y29BD.
3. En las ultimas lecturas de disco **siguen ausentes** `Mezclador::Posicion`, `VolumenSpu2` e `iwadx::PararTodos`. El puente protegido por `DBZ3_IW_SPU2_LISTO` no se habilita sin esas API. Se conserva el bloqueo; no se editan archivos de audio compartidos.
4. Pantallas normales sin activar. No build del juego ni `pruebas.py todo`; no se abrio juego, ventana, dispositivo de audio ni se publico nada. Para cerrar igualdad grafica hace falta evidencia del VU/GS original mas alla del interprete funcional de este arnes.

## Comandos ejecutados y evidencia conservada

```text
python -B tools/iw_capsulas_F4_original.py --frames 1200
  -> D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json --solo-ame
  -> PASS25219; D:/tmp/solIWF/f4_prueba_1793460684935896500/
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json --puente D:/tmp/solIWF/midi_F3_prueba_1793456498795932700/bancos_midi.bin
  -> PASS329; D:/tmp/solIWF/f4_prueba_1793461166818617700/
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json --solo-vu --vu-indice 5
  -> PASS21; D:/tmp/solIWF/f4_prueba_1793461302352941500/
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json --solo-vu --vu-indice 53
  -> PASS21; D:/tmp/solIWF/f4_prueba_1793461166838410600/
python -B tests/iw_capsulas_F4_prueba.py D:/tmp/solIWF/ame_F4_1793460483872588800/original_ame.json --solo-vu --vu-indice 61
  -> PASS21; D:/tmp/solIWF/f4_prueba_1793460581362876700/
```

Compilaciones standalone con Clang/C++23 y UBSan-trap; ejecuciones anteriores con FAIL conservadas. El runner comprueba SHA256 del ELF/AME contra el oraculo y registra comandos/codigos de salida/fuentes. No se repitio la prueba MIDI/PCM F3 ni pruebas del juego.

## Archivos de esta continuacion

- Modificados: `src/iw_capsulas_render.h`, `src/pasarela_iw.cpp` (solo CapsulePick), `tools/iw_capsulas_F4_original.py`, `tests/iw_capsulas_F4_prueba.{cpp,py}`, `tests/iw_capsulas_F2_original.py` (solo dependencias globales del extractor).
- Nuevos: `tests/iw_capsulas_F4_motor.cpp`, este informe y `studies 5/ENTREGAS/solIW/INFORME_F.md`.
- Previos F4 reutilizados: `tools/iw_capsulas_F4_datos.py`, `tools/iw_capsulas_F4_vu.py`; F1/F2/F3 se describen en `iw_capsulas_F_integracion.md` e `iw_midi_F_entrega.md` (historicos, sus pendientes de cadenas quedan sustituidos por esta entrega).
- Ningun archivo borrado; no cambios en `iw_sonido/iw_iop/iw_adx/iw_azar/iw_ame`, `anim_iw*`, minijuegos, SDK, CMake, main, net ni github.
