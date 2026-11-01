# solIWF — reverb3/050A y salida comun implementadas

Esta continuacion sustituye los apartados «reverb sin implementar» y «waveOut
independiente por voz» de `iw_audio_F_evidencias.md`. No modifica el RNG del
llamador, `anim_iw.cpp` ni `iw_ap_eventos.h`.

## Cambios efectivos

- Nuevo `src/iw_sonido.h`: DSP SPU2 persistente por core, mezclador IOP+ADX,
  slots/handles y una salida WinMM48k de tres buffers256cuadros. Sin hilo ni
  dispositivo por voz; cierre con solicitud de parada, join, reset/unprepare/close.
- Reverb **Studio Medium, efecto3**, con los32parametros programados por el
  `LIBSD.IRX` del jugador: memoria4840h bytes por core (9248palabras), reflexiones
  SAME/DIFF, cuatro COMB, dos APF, saturacion/Q15, alternanciaL/R a24k y FIR39taps
  de bajada/subida48k. Profundidad050A/050A=1290/32768 por canal.
- `iw_iop.h` conserva el enrutado original **tono+0D** en `Voz.rutas` y lo
  transmite desde `Oir(voz)`: bit0/1secoL/R, bit2/3envioL/R. La salida legacy
  asegura3/050A si el tono envia a efecto; **no reinicia la cola por evento**.
  La ruta MIDI Smpl+29 es distinta; no se usa ese offset en el Setb compacto.
- `iw_adx.h` conserva el decoder y comparte `Canal`/salida con IOP. ADX sin
  metadatos de envio entra por seco.32k→48k se remuestrea con fase entera y
  interpolacion lineal del host; no se atribuye a CRI una equivalencia no medida.
- `MasterVolume` **ya se aplicaba dentro del Oir anterior**. Ahora se aplica una
  sola vez **despues** de sumar/saturar ambos cores y su cola. El proveedor del
  launcher se lee por bloque; mute avanza voces y efecto, emitiendo ceros.
  Cambiar del proveedor a volumen explicito esta sincronizado para que una
  lectura antigua no pise la nueva ganancia. Ganancia de fuente va ANTES del
  efecto y es un parametro diferente.

## Mezclador existente: resultado de la inspeccion

`rex/audio/AudioSystem::SubmitFrame(index, samples_ptr)` y
`rex/audio/sdl/SDLAudioDriver::SubmitFrame(frame_ptr)` reciben direcciones de
memoria Xbox y muestras multicanal guest, con clientes/callbacks propios. El
streamSDL y su callback son privados; no hay entrada publica de PCM host para
sumar IW. `tactics/audio.h::MezclaSe` solo arbitra voces, no suma PCM. Los otros
reproductores revisados (`video_sfd.cpp`, `pasarela_b1.cpp`) usan otros waveOut.
No se han modificado SDK, main ni CMake. La nueva salida unifica IOP y ADX de IW;
la mezcla con B3 sigue siendo la del sistema operativo.

Se ofrece un punto reutilizable: `Reproductor::DispositivoAutomatico(false)`
ANTES de enviar, y `Procesar(destino_estereo16, cuadros48k)` desde un callback
externo. No se puede cambiar de backend mientras el hilo esta activo. En una
plataforma sin WinMM se devuelve handle0/errorFFFFFFFF si no se elige callback,
evitando acumular voces que nunca se reproducen.

## Evidencia contra el original

1. EU `iw_sles55347.c`:13FF40forma RPC22;Battle_InitSoundRegulator240D90 y
   s_BTL_SOUND_TRANS302CC0piden ambos cores/modo3/profundidad050A.
2. AMSOUND: tabla4698→2DCC para RPC22; llamada4388→modhsyn ordinal9.
   MODHSYN1E10mapea3→3, propaga profundidades, invocaLIBSD.
3. `tools/iw_audio_F_reverb_original.py` carga el **IRX del jugador** y ejecuta
   con interprete R3000 su export23(LIBSD0C2C). Las escrituras MMIO reales
   producidas por esas instrucciones se guardan en `configuracion_irx.json`.
   IRQ y la llamada de vaciado DMA se registran aparte, sin fingir una emulacion
   del kernel/DMA. Son pruebas de configuracion, no ejecucion del hardwareSPU2.
4. MODHSYN5D5C..5F2Clee tono+0D y actualiza VMIXL/R/EL/ER. Se ejecuta ese
   bloque para las16mascaras×2cores×2estadosprevios: **64casos**, comprobando
   tanto activacion como borrado de bits.5CA4/5CB0leen ADSR+0E/+10 del mismo tono.
5. Formula/FIR: [psx-spx: SPU Reverb Formula/Buffer Resampling/Studio Medium](https://psx-spx.consoledev.net/soundprocessingunitspu/).
   Se verifico tambien la disposicion de registros/alternancia en las referencias
   SPU2 publicas de PCSX2; no se incorpora su codigo ni se instala nada.

## Pruebas de salida

```powershell
python -B tools/iw_audio_F_reverb_pruebas.py
python -B tools/iw_audio_F_pruebas.py --solo-nucleo --elf-eu "E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47"
```

Resultados en `D:/tmp/solIWF/reverb/`:

-32parametros×2cores coinciden con MMIO del original.64casos de enrutado PASS.
- Impulso izquierdo, derecho negativo y rafaga extrema: **96000cuadros estereo
  cada uno (2s)**, cero diferencias contra referencia independiente. Esta usa
  los offsets/coeficientes MMIO del IRX, FIR por convolucion y RAM circular
  separada del codigo C++. Las WAV de cola se conservan en ese directorio.
- Cola por bloques y al terminar voz, otro evento sin reinicio, core1aislado,
  volumen tras saturacion, sustitucion, handles obsoletos, pausa/reanudacion,
  mute que avanza el reloj, remuestreo y entradas invalidas: PASS.
- Receptor WinMM (sin altavoz): **un dispositivo/tres buffers**, IOP1000+ADX2000
  con MasterVolume0,5→1500; cambio en vivo a0,25→750; silencio/pausa/corte;
  API explicita1000×gananciafuente0,5×maestro0,25→125 aunque el launcher valga0,9;
  slot compartidoIOP/ADX, erroresOpen/Write y liberacion de buffers: PASS.
- `Oir` legacy con envio original hereda reverb3 y produce la misma cola de
 2s que la referencia, aunque la voz termine despues de un solo cuadro.
- Deteccion de comportamiento indefinido activa en pruebasDSP/salida y corpus.
  El corpus anterior permanece109=70PCM+39sin emision;44pares se contrastan
  muestra a muestra y sus mascaras con el tono del archivo del jugador.

`reverb_verificado.json` contiene hashes de fuentes, comandos, resultados y
limites. Los respaldos de esta continuacion son `iw_iop.h.pre_reverb` y
`iw_adx.h.pre_reverb`; los respaldos `.antes` de la primera entrega se conservan.

## Integracion disponible para el coordinador

La llamada existente `iwiop::Oir(voz)` ya usa la mezcla comun, las rutas del
tono y la reverb; no necesita pasarle MasterVolume. Para leer volumen en vivo
incluso si la escena solo dispara ADX, llamar una vez al entrar:

```cpp
dbz3::iwiop::ConfigurarSalidaLauncher();
auto& sonido = dbz3::iwsonido::Salida();
// Al ejecutar el equivalente de RPC22 (una vez al cambiar de escena/contexto):
sonido.Mezcla().ConfigurarEfecto3(0, 0x050A, 0x050A);
sonido.Mezcla().ConfigurarEfecto3(1, 0x050A, 0x050A);
```

Al implementar un evento que **deba sustituir** la voz del actor:

```cpp
dbz3::iwsonido::Opciones o;
o.canal = sonido.Mezcla().Slot(idActor); // identidad estable del contexto
// rutas=FF hereda del tono; core=0 por defecto, core1 si lo decide el asignador.
auto handle = dbz3::iwiop::Oir(voz, o); // volumen/mute vivos del launcher
// ADX puede ocupar el mismo slot y sustituir el IOP anterior:
auto adxHandle = dbz3::iwadx::Oir(pcmAdx, o);
sonido.Mezcla().PausarSlot(idActor, true); // false=reanudar, cursor preservado
sonido.Mezcla().PararSlot(idActor);
sonido.Mezcla().Parar(handle); // handle antiguo no corta una voz nueva
```

Para sonidos que deban solaparse: sin canal, o `o.sustituir=false`. No asignar
un slot exclusivo a todos los golpes indiscriminadamente. `o.ganancia` es local;
`o.rutas=3` fuerza seco,12solo envio,15seco+envio;FFhereda del decoder.

Volumen final explicito (sin volver a multiplicar por el launcher):

```cpp
auto handle = dbz3::iwiop::Oir(voz, volumenFinal, o);
// Para regresar a los ajustes vivos: iwiop::ConfigurarSalidaLauncher().
```

Al abandonar el contextoIW, `sonido.Cerrar()` detiene/junta el hilo y limpia
voces/cores. `Parar` individual conserva la cola del core. Para cronologia
continua bajo mute, el llamador debe enviar igualmente los eventos: un filtro
previo en `anim_iw.cpp` impide crear una voz que el mezclador no puede conocer.
Consultar `ErrorSalida()` despues de una apertura/envio asincronos si se desea
mostrar un error; no se realizan reintentos automaticos.

## Limites precisos

No hay captura de salida de consola/PCSX2: el usuario excluye ejecutar el juego.
La comparacion es con **instrucciones de configuracion originales + modelo de
hardware independiente**. No certifica la fase global delSPU2, el planificador
IOP4167us, el asignador/prioridades de las48voces entre cores ni la mezcla global
delB3. `Parar(handle)` corta la fuente al mezclador; no representa por si solo
el secuenciador/key-offADSR de todos los handlesAP>=1000. Los pasos de integracion
en los archivos del coordinador, el RNG compartido y esa certificacion siguen
fuera de esta entrega. No se ha compilado/arrancado juego ni ejecutado `todo`.
