# solIWF / F3 — RPC13 real; RPC14 y transición10 con contratos de host explícitos

## Producción
- Nuevo `src/iw_midi.h`: parser IECSidiM comprimido, running status, bit80=delta omitido, note-off de dos bytes, tempo/PPQ, fin de pista y all-notes-off; banco HSYN sin comprimir gorP/tesS/lpmS/igaV y ADPCM/pitch nativos. Reutiliza ADSR y filtro ROM del decoder existente.
- Se portan las capas/curvas/afinación de los bancos956/959 y sus pistas957/960. Otros parámetros no resueltos se rechazan; no se elige otra muestra como sustituto.
- Pulso original confirmado ejecutando AMSOUND31F8: `MODMIDI.Init(...,0x1047)` =4167us (~240Hz), NO1000us. El reloj conserva los redondeos nativos; el origen del primer callback del arnés es0.
- En `src/pasarela_iw.cpp`, SOLO CapsulePick: carga real de bancos, preparación MIDI, RPC13->handle real de `iw_sonido::Salida().Enviar`, corte/cola RPC14 y cancelación de nuestros handles al aparcar una escena incompleta. No se toca audio de otros slots.
- RPC14 implementa KeyOff/cola ADSR sin cortar la onda a cero. Desde la UI no supone un reloj de ventana: si no existe cursor de AUDIO devuelve fallo explícito. El arnés aporta el cursor que acaba de consumir con `Procesar` y prueba el puente real13/14.
- Transición10 host: progreso heredado, quad negro, RPC de volumen y stop ADX al cruzar1. CORRECCIÓN:13FBD0 usa RPC**1D**, no1C ni volumen MIDI: AMSOUND2A7C escribe MVOL L/R de ambos cores (`980,A80,981,A81`). ToDefine126880 detiene ADX, no tasks SPX.
- El host se niega a iniciar esa transición si faltan MVOL/registro ADX. No se activan las pantallas incompletas. En `src/iw_spx_capsulas.h` se corrigió solo el comentario de ToDefine.

## API que necesita el integrador de audio (no se editaron sus archivos)
```cpp
// En Mezclador: frames consumidos de ESA fuente48k, NO reloj UI ni muestras aún en cola del SO.
bool Posicion(iwsonido::Handle, uint64_t& frames) const;
// MVOL nativo SPU2, independiente de MasterVolume/AudioMute del usuario.
bool VolumenSpu2(unsigned core, uint16_t izquierda, uint16_t derecha);
```
- `Sesion` detecta automáticamente `Posicion` con retorno bool; también admite `posicion_audio(handle,frames)` si el anfitrión ya dispone del cursor. Una API presente que no encuentra un handle nuestro significa fuente finalizada/cancelada y stop no-op.
- En `PruebaCapsulasSpx::volumen_spu2(uint16_t v)`, enlazar `VolumenSpu2(0,v,v)` y `(1,v,v)`. No sustituirlo por ganancia de un handle ni por MasterVolume de preferencias: son dominios distintos.
- `detener_adx_anteriores` debe recorrer el registro heredado de ADX del port y detenerlos (los ya finalizados son no-op). No usar `Mezcla().Limpiar()`: también borraría el MIDI y actores ajenos.

## Pruebas y evidencia
- `python -B tests/iw_midi_F_original.py`: ejecuta MODMIDI, MODHSYN, LIBSD.Note2Pitch y EE13FBD0/AMSOUND2A7C originales; el hardware LIBSD es frontera observada. No instala ni descarga nada.
- Última corrida: `python -B tests/iw_midi_F_original.py --oraculo D:/tmp/solIWF/midi_F3_prueba_1793454620573294300/original_midi.json` → PASS104 MIDI/PCM y PASS101 puente/host10, compilación standalone y sanitizador0.
- Coinciden los74 eventos completos hasta EOT, sus28 ráfagas/tiempos y las9 combinaciones programa/velocidad contra registros HSYN originales (onda, pitch, ADSR, volumen, core y rutas). PCM estéreo48k de1s por banco y cola/corte de0.5s coinciden byte a byte con DSP independiente dirigido por esos eventos/registros y la ROM documentada.
- El mezclador REAL produce señal no nula tras RPC13; RPC14 deja cola y después silencio, conservando otro actor real. Host10 prueba los9 valores originales de MVOL, descenso de PCM de un ADX de estímulo en core1, quad negro y retirada real del handle UNA vez.
- Evidencia final: `D:/tmp/solIWF/midi_F3_prueba_1793456498795932700/{evidencia_midi.json,original_midi.json,midi.log,puente.log}`; hay4 WAV generados desde la salida del mezclador. No se abrió dispositivo ni se certifica escucha/loopback del SO.
- El PCM enviado es una submezcla MIDI de core0:957 contiene476 muestras saturadas y960 ninguna. No se certifica la convivencia matemática de esa submezcla ya limitada con actores adicionales del MISMO core0; para ese caso hace falta un bus/fuente de mayor margen o envío atómico de stems sin limitar previamente. No se proclama SPU2 completo.
- F3 sustituye el ejecutor antiguo F2 de CapsulePick, cuyas expectativas aún eran «RPC13 bloqueado». No se ha editado ese test antiguo. No build del juego ni `pruebas.py todo`.

## Bloqueos que quedan
- UI RPC14: API/callback de posición real del mezclador aún ausente; el núcleo y su salida ya pasan con cursor real contado por el arnés.
- UI transición10: faltan MVOL de ambos cores y el registro ADX heredado. El escenario host controlado pasa sin fingir esos consumidores.
- Tras superar RPC13, SPX993 bloquea en **AME972: cadena20000/+24** que el motor reducido omite; SPX994 en **GS: shader VU de luz/AML**. Doble contexto44/42/expansión y enlace AMT siguen pendientes. No se editaron iw_iop/iw_adx/iw_sonido/anim_iw/minijuegos/iw_azar/iw_ame/aura/rayos.
