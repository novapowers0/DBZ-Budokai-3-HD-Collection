# solIWF — audio, entrega parcial comprobada (31/10/2026)

> Continuacion: `iw_audio_F_reverb.md` sustituye el estado de reverb/salida de
> este informe inicial: efecto3/050A, routing por tono y mezclador comun ya
> implementados y probados. Aqui se conserva la evidencia de la primera vuelta.

## Resultado medido y alcance

Solo se editan `src/iw_iop.h`, `src/iw_adx.h` y los archivos propios
`tests/iw_audio_F*` / `tools/iw_audio_F*`. El informe de entrega queda aquí para
respetar ese ámbito. Los artefactos y respaldos están en `D:/tmp/solIWF/`.

- Corpus original: **109 eventos**; antes **68 PCM, 7 sin emisión, 34 pendientes**;
  después **70 PCM, 39 sin emisión, 0 pendientes de lectura**. Identidad Goku para
  los clips comunes204/915, idioma USA y semilla fija del ensayo anterior:
  **no equivale a certificar todos los personajes/escenas ni el RNG global**.
- De los34: **32** son peticiones fuera del máximo del banco actualmente elegido
  (954 máximo77;1472 y1476 máximo34); **2** usan954/58, flujo con apagado de nota.
  No se inventan muestras para las peticiones ausentes. La identidad/contexto de
  banco en otra escena podría ser distinta y sigue siendo responsabilidad del
  integrador.
-954/58: programa0, tecla58, VAG27, pitch1365; deltaVLQ`8F50`=2000ticks;
  apagado lógico al cuadro de audio96000, liberación hasta112384cuadros
  (2,341333s). Antes se rechazaba; ahora genera salida seca y llega al receptor
  de `waveOut` con formato/volumen/orden de muestras comprobados.
- Interpolación lineal sustituida por ROM de512coeficientes y cuatro muestras,
  con los desplazamientos y truncamientos por producto del SPU/SPU2. ADSR añade
  liberación, retención de tasas máximas y corrección del ataque exponencial
  rápido por encima de6000h. La referencia es la especificación de hardware;
  **no se presenta el PCM seco como una captura bit a bit del mezclador original**.
- ADX: si el slot cambia mientras se abre el dispositivo, se cancela antes de
  enviar el primer buffer. Pausa previa al primer envío respetada. La carrera
  reproduce FAIL en el código anterior y PASS en el actual con receptor silencioso.

## Evidencia del original

1. Decomp EU `E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c`:
   `127C90` intercambia la semilla; `127CA0 Memory(n)` escribe el mismo estado;
   `22FEA0` selecciona gritos con Memory(32), filtro de idioma/probabilidad;
   `240740` despacha el identificador del banco cargado; `302CC0` configura
   el sonido del minijuego y llama `13FF40 KernelLevelShit5`.
2. `MODSESQ.IRX` del jugador:21F8despacha la secuencia;2308..2318comprueba
   `id<=máximo`,2334..2350rechaza punterosFFFFFFFF sin emitir;
   8BC..8FClee deltaVLQ;7F4..8B8obtiene periodo128000 con los seis
   factores1000;1AEC..1D30actualiza el acumulador y despacha por ticks.
3. `MODHSYN.IRX`:2C40..2D38distingue90/A0;A0velocidad0→2E40→785C
   (apagado, no segundo one-shot).4BE8..4BFCsatura pitch a3FFF.
4. Reverb **investigada, no implementada**: `13FF40` forma el paqueteRPC22;
   la tabla de `AMSOUND.IRX` en4698contiene2DCC para ese índice.
   2DCC..2F4Crecorre los dos cores, modo`3|100h`, profundidades050A/050A,
   delay/feedback0, llama la importación `modhsyn` ordinal9 (4388) y la
   operación `libsd` ordinal25 (4344). Los datos pertenecen al estado de los
   cores, no a un único VAG. Sin mezcla/envío húmedo persistente y captura de
   su cola no hay evidencia suficiente para añadir una reverb aproximada.
5. RNG: `.reginfo` da GP USA4A35F0, dirección GP-7FE8=49B608; EU GP4A3DF0,
   dirección49BE08. Ambos ELF contienen **87654321**, no1. Las referencias
   directas GP-7FE8 localizadas en el código USA son127C90/94/CA0/CCC.
   `rng_global.json` conserva la búsqueda acotada; no prueba todos los accesos
   indirectos. `LeerSemillaAzar` localiza el dato sin asumir el delta regional.
6. ROM/ADSR: [psx-spx, Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/),
   apartados4-Point Gaussian Interpolation y Envelope Operation. Documento
   conservado como `spu_especificacion.txt`; hashes de fuentes/resultados enJSON.

## Integración necesaria en los archivos del coordinador

- `anim_iw.cpp`, bloque del tipo3 (en esta entrega líneas691..698): sustituir
  el `static uint32_t semilla=1` por **la instancia compartida del contexto IW**.
  Obtener su semilla inicial con `iwiop::LeerSemillaAzar(elf, semilla)`; crear
  `iwiop::Azar{semilla}` una sola vez por contexto y pasar
  `[&] { return azarCompartido.Memory(32); }` a `apev::ResolverVoz`.
  **No reiniciar por evento, actor o efecto.** Restaurar `estado` al cargar estado.
- `iw_ame.h:Mem` y los demás consumidores deben avanzar el MISMO estado
  (`Memory(8)`, `Memory(1)`, `Memory(32)` según el original), conservando el orden
  de llamadas. `Azar::Cambiar` permite salvar/restaurar como127C90. La API está
  implementada; conectar únicamente voces a una nueva instancia no cierra RNG.
- `anim_iw.cpp`, rama `voz.silencio` (líneas744..746 al leerla): distinguir
  `voz.secuencia_ausente` del volumen cero en el diagnóstico. El mensaje actual
  «volumen/velocidad cero» sería falso para esas32 peticiones. No cambiar su
  banco por otro para hacerlas sonar sin evidencia de carga/contexto.
- Reverb requiere un mezclador común de48k con estado de ambos cores y los
  envíos reales; los `waveOut` independientes actuales no conservan ese estado.
  También faltan sincronización de la fase del callbackIOP4167us (AMSOUND944,
  initmodsesq ordinal4) y controles/cortes de las voces IOP. El apagado nuevo
  respeta el tiempo lógico del flujo, no certifica esa fase de planificación.

## Verificación

Comando focalizado (solo ejecutables de prueba, nunca el juego):

```powershell
python -B tools/iw_audio_F_pruebas.py --elf-eu "E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47"
```

El primer ensayo necesita `--especificacion <texto local de psx-spx>`; luego
reutiliza `D:/tmp/solIWF/spu_especificacion.txt`. Antes de las pruebas se guardó
el código inicial en `iw_iop.h.antes` / `iw_adx.h.antes` mediante el inventario
`python -B tools/iw_audio_F_investigar.py`; no se sobrescriben esos respaldos.

Pruebas: corpus con detección de comportamiento indefinido; pares banco/secuencia
contra un oráculo separado de PCM;1280guardias ejecutando instrucciones IOP;
1024impulsos (las256fases×4taps) contra la ROM documentada;Memory(n) intercalado
contra instrucciones EE, incluida la semilla del ELF; lectura USA/EU y rechazos
de ELF truncados. Salida ADX comparada con FFmpeg y receptor de `waveOut`:
bytes/formato/volumen, sustitución, pausa inicial/reanudación/corte y mute.

`resultado_F.json`, `antes_resultado.log`, `despues_resultado.log`,
`sanitizador_resultado.log`, `salida_antes.log`, `salida_despues.log` y
`salida_seca_954_58.wav` conservan la evidencia. **No hay prueba de altavoz,
captura original de mezcla final, integración del RNG ni reverb completa.**
No se ha compilado el juego ni ejecutado `pruebas.py todo`.
