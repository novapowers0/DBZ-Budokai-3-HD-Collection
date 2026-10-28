# Frente: Pasarela de modos (modos de B1, B2 e IW en el HUB)

> Ronda 2 (2026-10-15): los ficheros de la pasarela pasan al frente campañas, que hizo lo que quedaba de la leyenda
> (reglas en código, dificultad, puntos y resultado de B1) y la entrada **Pasarela** (saga de B1, tablero, misiones de B2,
> retos de IW). Estado y pruebas: `docs/14_frentes/campanas.md` §0 y `docs/16_pasarela_modos/04_misiones_y_tablero.md`.

Fecha: 2026-10-15. Solo US (con `g_guest_eu` no hay modo; los dos .cpp están en el `REMOVE_ITEM` solo-EU).
Estudio por modo y formatos: `docs/16_pasarela_modos/` (README con la tabla de viabilidad).

## Ficheros del frente
- `src/pasarela.cpp`: motor del modo y su pantalla (ImGui con piezas del juego). Sin ganchos `REX_HOOK_RAW`:
  combates por el conector `dbz3::hub::RequestBattle` (hub.cpp) y resultado leído de memoria (estado
  `mgr+35128`, ganador `mgr+35136`, los de `tagteam.cpp` LabTick). Entrada del HUB `satan` (la reserva
  `hub_panel.h`: "Leyenda de Mr. Satán").
- `src/pasarela_b1.cpp`, `src/pasarela.h`: lectura en memoria de la ISO de Budokai 1 del jugador (ISO 9660,
  AFS, #AMT de PS2, tabla del ELF por firma). Nada se escribe en disco salvo el avance.
- `mod center hd/pasarela/pasarela.py`: herramienta reproducible (`info`, `extraer --salida DIR`, `prueba`), misma
  lógica que el lector del juego; lo que extrae es de la copia del jugador (no va al repo).
- `docs/16_pasarela_modos/` (estudio, `pruebas/*.toml`, `parche_hub_satan.diff`).
- `CMakeLists.txt`: `src/pasarela.cpp` y `src/pasarela_b1.cpp` tras `src/hub.cpp`, y en el `REMOVE_ITEM` solo-EU.

## Qué ve el jugador
- Antes: nada de otros juegos en el HUB.
- Ahora: con su ISO de **Budokai 1** en `ps2_games` (junto a `dbz3.exe`), el HUB tiene en su página 2 la entrada
  **"Leyenda de Mr. Satán"**: presentación con el logo, el fondo y el dibujo del original (en el idioma del juego),
  11 retadores con las charlas y avisos del original (imágenes de texto de su copia, 5 idiomas), tarjeta
  "Retador N de 11", combate nativo en el ring de los Juegos de Cell **con las reglas del original** (60 s contra
  Vegeta, sin tiempo contra Cell, Cell Jr. de 1000, vida que no se recupera, Goku y Gohan en SSJ, Cell perfecto),
  reintento al perder, avance guardado y final. Sin la ISO, la entrada sale en gris y el bocadillo explica qué falta
  y dónde ponerlo (parche del HUB aplicado por el coordinador).
- La ISO se lee en ~15 ms al llegar al menú principal (registro: `dbz3 pasarela: ... leida en N ms`).

## Pruebas (copia `D:\DBZ3HD\pasarela_test`, juego en español, `ps2_games` = enlace a `D:\DBZ3HD\ps2_games`)
`python tools\pruebas.py correr pasarela_test docs\16_pasarela_modos\pruebas\<escenario>.toml`. Con
`DBZ3_TEST_PASARELA=auto[,desde=N]` el modo avanza solo (4 s por pantalla); `DBZ3_TEST_RINGOUT=1` acaba cada
combate a los 5 s; `DBZ3_PS2_GAMES=<carpeta>` sustituye a `ps2_games` (una vacía = sin ISO).

| Escenario | Resultado | Capturas (`D:\DBZ3HD\_pruebas\`, pasada con las reglas, 04:06-04:15) |
|---|---|---|
| `leyenda_inicio` (presentación, Yamcha, charla, Ten Shin Han) | PASS | `20261015-040644_pasarela_test_leyenda_inicio` |
| `leyenda_final` (charla y tarjeta de Cell, Cell perfecto sin tiempo, final, vuelta) | PASS | `20261015-040858_pasarela_test_leyenda_final` |
| `leyenda_derrota` (sin ring out: gana el COM, "ha caído", reintento, otro combate) | PASS | `20261015-041003_pasarela_test_leyenda_derrota` |
| `leyenda_sin_iso` (carpeta vacía: no disponible, no empieza) | PASS | `20261015-041239_pasarela_test_leyenda_sin_iso` |
| `leyenda_hub` (como un jugador: fila HUB, entrada, presentación, tarjeta, B, vuelta al HUB) | PASS | `20261015-041509_pasarela_test_leyenda_hub` |
| `leyenda_hub_gris` (sin ISO: entrada en gris con "Necesita tu copia...") | PASS | `20261015-041422_pasarela_test_leyenda_hub_gris` |
| `leyenda_reglas` (Vegeta 60 s, Piccolo, Cell Jr. 1000 y su vida que sigue) | PASS | `20261015-040350_pasarela_test_leyenda_reglas` |
| `leyenda_formas` (Goku SSJ, Gohan SSJ con la vida que le quedó a Mr. Satán: 2803) | PASS | `20261015-040233_pasarela_test_leyenda_formas` |
| formas de B3 medidas con `DBZ3_TEST_BATTLE` (Goku 2, Gohan 1, Cell 2, Piccolo 2 sin cambio) | capturas | `20261015-035731_pasarela_test_forma_a`, `20261015-035828_pasarela_test_forma_b` |

Además: `python "mod center hd\pasarela\pasarela.py" prueba` OK (prueba sintética + ISO europea); los 3 ficheros
del parche del HUB compilan (`clang++ -fsyntax-only` con las opciones del build) y el parche se aplica limpio.
Compilado con `tools/compilar.py` (sin errores). 0 `UNHANDLED` en los registros de las pruebas.

## Propuestas para otros frentes (no tocados)
1. **HUB: entrada en gris con explicación** (APLICADO por el coordinador 2026-10-15). Diff:
   `docs/16_pasarela_modos/parche_hub_satan.diff` (aplicar con `patch -p1 --binary` desde la raíz; ~40 líneas).
   Añade `RegisterGate(clave, ready, why)` y `EntryWhy(e)`: `EntryReady` = registrada y `ready()`; el bocadillo de una
   entrada en gris dice `why()` en vez de "Muy pronto". Incluye el cambio en `pasarela.cpp` (registro al arrancar
   con la puerta, en vez de esperar a la ISO). Probar con `pruebas/leyenda_hub_gris.toml`.
   Nota: `hub.md` dice que `pasarela.cpp` registra `pasarela`; ya no: registra `satan`. El texto de `pasarela`
   ("Trae luchadores de B1/B2/IW") es del importador, no de este frente.
2. **Conector (`hub.cpp`): tiempo y vida por combate** (HECHO: `RequestBattleEx`, ya en uso) (para las reglas del original: 60 s contra Vegeta, sin tiempo
   contra Cell, vida que no se recupera, 5 Cell Jr. de 1000). Boceto:
   ```cpp
   struct Request { ...; int time_s = 0; int hp[2] = {0, 0}; };   // 0 = lo de serie
   bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2);
   // en el gancho de sub_821D7C30, tras escribir ID/traje/mando de cada lado:
   if (g_req.time_s) REX_STORE_U32(kCfg + 348, uint32_t(g_req.time_s));   // s; <= 0 sin tiempo (doc 10 s2.2)
   if (g_req.hp[k] > 0) { REX_STORE_U32(rec + 36, g_req.hp[k]); REX_STORE_U32(rec + 40, g_req.hp[k]); }
   ```
   (`modos.cpp` escribe la vida en la espera de la escena 24; medir si escrita aquí se respeta.) Con eso, la
   pasarela pasaría `vida_jugador` (la que quedó, `mgr+2096+6080` al acabar) y el tiempo de la tabla de B1.
3. **Forma inicial pedida por código** (HECHO: `RequestBattleEx`, campo forma = rec+20) (`tagteam_panel.cpp`, dueño del selector LT/RT): un `SetStartForm(lado, forma)`
   que valga para el siguiente combate; la tabla de B1 trae Goku SSJ, Gohan SSJ y Cell perfecto (hoy Cell sale en
   su primera forma).
4. **Índices de docs**: una línea en `docs/README.md` (`16_pasarela_modos/`) y otra en `docs/04_herramientas/TOOLS.md`
   (`mod center hd/pasarela/pasarela.py`).
5. **Release / Kit** (`tools/modpacks/LEEME_PS2_GAMES*.txt`): decir que la ISO de Budokai 1 en `ps2_games` activa
   la "Leyenda de Mr. Satán" del HUB (y `make_modpacks.py`: incluir `mod center hd/pasarela/`).
6. **Ideas del coordinador** (`vision_futura.md` G2/G3): el motor de pasos de `pasarela.cpp` (charla → tarjeta →
   combate → resultado) sirve tal cual para una misión con Gotenks gordo y para un "tablero de 6 casillas" propio
   (falta solo la pantalla del tablero); ver `docs/16_pasarela_modos/02_budokai2.md` §2.

## Reglas, dificultad, puntos y cinemáticas de la Leyenda (ronda 5, 2026-10-17)

Fuentes: decompilación Ghidra de B1 europeo (`E:\DBZ3HD_trabajo\ghidra\salida\b1.c`, direcciones del ELF; las mismas
que usan los scripts de `E:\DBZ3HD_trabajo\P3\dis.py`). Pruebas con la copia `D:\DBZ3HD\satan_fix` y
`DBZ3_PS2_GAMES=D:/DBZ3HD/ps2_games`.

### 1. Reglas por combate (armadura de A16, Mr. Satán herido, ki lento): NO existen en B1 (cerrado en DS143)
**DS143 (2026-10-27)**: se han quitado los tres valores inventados. La revisión del decompilado confirma que son solo los
avisos 669 #5/#7/#11 (texto de Mr. Satán): no hay ninguna comparación con los IDs 5 (Trunks), 6 (Piccolo) ni 16 (A16)
en `b1.c` (el único caso por personaje es Cell Jr. 0x17/forma 3, en las voces/`0x1AD0D0`), el modo 3 (`+0xD0B0`) no
entra en daño ni ki, y el daño general `0x1AD0D0` no tiene rama del modo. Lo que sigue es la investigación de la ronda 5,
que ya no se aplica: el puerto pelea como B1.

Qué había en el juego (de la ronda 2, `pasarela.cpp` `ReglasDe` + `VigilarCombate`): A16 anulaba el 50 % del daño que recibe;
contra Trunks el daño que no viene de gastar ki en los 1,5 s anteriores se queda en un 25 % (se anula el 75 %); contra
Piccolo Mr. Satán gana la mitad del ki. Los avisos del original (669 impares, en inglés): "You can't hurt Android 16
easily! Don't give it a chance to attack!", "Your hands and legs are injured! Use your Ki to attack!!", "Piccolo the Evil
King?! Fight even though it's tough to gather Ki!".

Dónde se buscó el original (sin encontrarlo):
- La tabla de 11 registros (`0x35DFC0`) solo trae vida, ki, tiempo, escenario, formas, cápsulas y la música (+0x72):
  nada de reglas (el vec4 de +0x00 es el cuaternio 0,0,0,1 en los 11; las cápsulas de Mr. Satán, 100-106, son iguales en
  los 11; las del rival son su lista de técnicas).
- La ronda (`0x5E9C14`, y su copia `base+0xE4CA` al empezar el combate, `0x196900`) solo la leen el código de las
  escenas del modo (`0x22D000-0x234E00`), el final del combate (`0x198F2C`, la mitad de la puntuación) y la tabla de nivel
  de la IA (`0x26C400`). Ninguna función de mecánica de luchador (daño `0x1AD0D0`, ki, golpes `0x19E840`, acciones
  `0x1A0000-0x1B0000`) mira la ronda ni la dificultad.
- El modo 3 (`base+0xD0B0`) solo se comprueba en la puntuación, el final del combate y la cámara; ni el daño ni el ki
  tienen rama propia del modo.
- Cápsulas: la tabla es la entrada 2042 de `DATA_xx.AFS` (`#SKA`, 280 fichas de 0x1C desde +0x20; efecto pasivo en +0xF,
  parámetros en +0x10/+0x11). Las cápsulas de los registros (Mr. Satán 100-106, A16 118-122, Cell Jr. 269-272...) son
  técnicas (efecto 0): no hay defensa ni ataque pasivos. El daño `0x1AD0D0` solo usa el ataque y la defensa de cápsulas
  pasivas (`0x1AEA50`) y la forma; la carga de ki (`0x1A1xxx`) solo el reloj propio `+0x4EA4`.
- Tampoco hay cadena de texto de esas reglas en el ejecutable.

Conclusión (DS143): al no existir en B1, el puerto **no** aplica armadura, solo-ki ni ki lento. Los valores propios
(50 %, 75 %, 50 %) se retiraron de `ReglasDe`; los campos siguen en `Reglas` porque los usan los entrenamientos de IW.

Verificado en el juego (registro `dbz3 pasarela: paso N: regla: ...`, con el muñeco quieto):
- A16 (`20261017-060802_satan_fix__r3_golpes`, `desde=7`): "golpe de 98 al rival, se anula el 50 % (49); armadura".
- Trunks (`20261017-060907_...`, `desde=9`): "golpe de 98 al rival, se anula el 75 % (74); sin gastar ki (herido)".
- Piccolo (`20261017-061007_...`, `desde=13`): "ki lento, el jugador gana 234.0 de ki y pierde 117.0".

### 2. Dificultad Novato / Experto / Avanzado -> nivel del COM de B3
- Campo: `mgr+35573` (u8). Con `mgr+2018` = 1 (Duelo, como pide el conector), `sub_8213E5C0` (llamada desde `sub_820F8770`
  al empezar el combate) elige el nivel interno de IA de cada luchador con la tabla `13 + n` del fichero de IA, igual que B1
  (`0x26C400`, caso 3, `fichero+0x48[dificultad]`). Medido en el juego (estructura de IA por luchador en
  `0x823EE440 + 192 k`, nivel en +22; `DBZ3_PASARELA_TRAZA=1` + `DBZ3_PASARELA_NIVEL=n`): `mgr+35573` 0 / 1 / 2 -> nivel
  interno **1 / 4 / 7** (capturas `20261017-061931`, `-062120` y `-062016`, `..._satan_fix_satan_nivel2`).
- Se escribe en cuanto existe el combate (estado >= 1), antes de que la IA lo lea (unos 20 s después, al cargar). Escribirlo
  antes en la CFG (+344) no sirve: el juego la pisa (el registro dice "nivel del COM 1 -> N").
- B1 no usa un nivel fijo por dificultad: el nivel de IA sube con el retador. Tabla de B1 (entrada 0x176 `#CPU`,
  desplazamientos en +0x48/+0x4C/+0x50; `pasarela.py info` la imprime): Novato `2 1 2 3 4 5 6 2 4 5 6`, Experto
  `3 2 3 4 6 7 9 3 6 7 10`, Avanzado `4 2 4 6 8 10 12 4 8 10 12` (índices de las 35 fichas de IA, 1-12).
  `NivelComB3` (`pasarela.cpp`) toma el nivel interno de B3 más cercano (1-2 -> 0, 3-5 -> 1, 6-12 -> 2): Novato
  `0 0 0 1 1 1 2 0 1 1 2`, Experto `1 0 1 1 2 2 2 1 2 2 2`, Avanzado `1 0 1 2 2 2 2 1 2 2 2`.
- No medido: el efecto en el comportamiento (una prueba de 40 s con Mr. Satán quieto, un solo intento por nivel, dio 1580 de
  daño con el nivel 0 y 920 con el 2: demasiado ruido). Solo se confirma que la IA coge el nivel pedido.
- Límite: B3 solo deja tres niveles por este campo (las demás entradas de la tabla son de otros modos); los 12 de B1 no se
  pueden reproducir uno a uno sin tocar la estructura de IA (no hecho).

### 3. Puntos y pantalla de resultado (668, 666)
Reglas de B1 (`0x1AF6F0`, `0x198E18`; detalle en `docs/16_pasarela_modos/01_leyenda_mr_satan.md` §2.1) ya en `pasarela.cpp`:
HIT, DAMAGE..., THROW, SPECIAL, COMBO FIN, SPECIAL FIN y, al acabar, LIFE y TIME. Esta ronda:
- Orden y topes exactos: `0x1AF6F0` suma cada suceso y deja el marcador en 0 si baja de 0; LIFE (vida del ganador - la del
  otro) **puede ser negativa** (ring out con menos vida) y se aplica antes de TIME (+20 por segundo que sobra, solo si el
  combate tiene tiempo). `PuntosB1` ya lo hace así (antes ignoraba el signo de LIFE). La leyenda se lleva la mitad.
- Pantalla de resultado y del final: bajo el tablón sale de dónde vienen los puntos: "Golpes y técnicas N + Vida N + Tiempo N:
  la mitad cuenta" (B1 los apunta como avisos LIFE/TIME sobre la barra durante el combate; aquí, la suma al final). Con el
  tope de 0, el término Vida es lo que cuenta de verdad. Captura `20261017-062241_satan_fix_satan/d_resultado.png`
  (ring out: Vida -219 -> 0, Tiempo 1900 -> 950 puntos).
- Los 8 sucesos que faltaban (2026-10-17, medidos en el juego con el registro de acciones del jugador y del rival:
  `DBZ3_PASARELA_TRAZA=1` escribe cada cambio de acción `X+3244` / código `X+2928 & 0xFFF`; capturas en
  `D:\DBZ3HD\_pruebas\20261017-07*_satan_fix_{ondas,escape,contra,agarre,choque2,resultado}`). B3 numera las acciones
  como B1 en las de movimiento (0 reposo, 1 guardia, 2/3 andar, **5 paso rápido adelante, 6 atrás**, 7/8 paso lateral) y
  usa la 15 + código para todo ataque; el resto de la tabla es propio. Tabla (B1 -> B3), todo para el lado del jugador:

  | Suceso B1 | Qué es en B1 | Qué es en B3 (medido) | Estado |
  |---|---|---|---|
  | REFLECT +300 | onda de ki devuelta (acción 0x1B, `FUN_0019e280`: la rutina de la bola da 5) | `sub_82119B38` -> `sub_82116FA0` da 5/6 y llama `sub_82122488(1, 26)`; el defensor pasa a la **acción 32 (código 0x15)**. G pulsada 1-4 cuadros antes del impacto, o con Deflect-Back (`X+8141`) y G mantenida. Contado con el gancho de `combate.cpp` (`OndasEvento(lado, 0/1)`; 1 = devuelta con un golpe, sin medir) | **PASS**: `20261017-071028_satan_fix_ondas` (16 devueltas, "REFLECT +300" cada una, con habilidad y a mano) |
  | CUT-OFF +150 | onda desviada (acción 0x1A; la bola da 3) | el mismo `sub_82119B38` con resultado 3/4 -> `sub_82122488(1, 25)` desde `0x82119D90` US / `0x82119E90` EU; el defensor pasa a la **acción 31 (código 0x14)**. G a 5-12 cuadros del impacto, o Deflect (`X+8140`). `OndasEvento(lado, 2)` | **PASS** (mismo registro, "onda desviada ... resultado 3", 17 seguidas) |
  | ESCAPE... -300 x (n-1) | retroceso rápido (acción 6, doble toque hacia atrás; el contador `+0x65CD` sube en cada uno y se pone a 0 en la acción 5, la 9 y al atacar) | **acción 6, código 006** (el doble toque da 3/004 y luego 6/006); avanzar da 5/005. Cuenta `n` retrocesos seguidos; vuelve a 0 con la acción 5 o 9 o un ataque propio | **PASS**: `...071758_satan_fix_escape` (3 atrás: -300, -600; tras 3 adelante, otra vez -300, -600) |
  | DULL... -200 x n | historial de 64 ataques (`FUN_001af8e0` apunta, `FUN_001af960` cuenta las repeticiones; marcador -2 al acabar la cadena): repetir siempre lo mismo | port exacto de las dos rutinas (`Historial` en `pasarela.cpp`). Un golpe propio = acción 15 con código 0x200-0x3FF (medido: puñetazo 0x200, patada 0x20E, rayo 0x23C, 0x300/0x30E...); acaba al volver a una acción de reposo o paso (0-10, 38, 39); si lo cortan un golpe o una escena no se apunta marcador | **PASS**: mismo registro (X repetida 15 veces: -200, -200, -400, -400 ... tope -800; X/Y alternas: -200..-600) |
  | RECOVERY +100 | pulsar G en la ventana de ±6 cuadros del golpe en el aturdimiento (acción 0x1E, `FUN_001a48d0`) | **acción 39** (Z-Counter: G con la palanca hacia el rival, ventana `sub_82102DF8`, cuesta ki): se teletransporta sin recibir el golpe, también desde el suelo (18/02A -> 39/0ED medido: sale de una paliza tumbado) | **PASS**: `...071937_satan_fix_contra` ("RECOVERY +100" en 18 -> 39/0ED); la esquiva por toque de G (acción 38, código 0xBC-0xCA) no puntúa en B1 y no se cuenta |
  | BURST! +400 | gana el choque de rayos (`FUN_001a6b90`: el que suma más (> 100 de diferencia o 70 cuadros de choque) pasa a la animación **0xEA**, el otro a otra acción) | **choque de rayos de B3 = acción 27 en los dos** (códigos 0xD2-0xE9 que cambian cada 9 cuadros, 5-9 s); el que gana pasa a **15 / código 0x0EA** (¡el mismo 0xEA de B1!) y el que pierde a **28 / 0x100**. Provocado con el muñeco `DBZ3_COMBATE_MUNECO=pk` (P+K a la vez) y el jugador disparando 23C a la vez; el piloto machaca X/Y en la acción 27 | **PASS** del lado del jugador: `...073222_satan_fix_choque2` ("BURST! +400" al ganar el choque, 61,37 s; el rival lo había ganado antes en `...072206_satan_fix_agarre` con 15/0EA del lado 2) |
  | ANTI-THROW +400 | contraataque a un agarre (`FUN_0019e840`): el agarrado pulsa el agarre en la ventana mientras el otro está en la acción 13 y los dos acaban empujados | **sin equivalente medido**. El rival agarró al jugador una vez (`...071402_satan_fix_contra`: rival 15/480, jugador 15/481, sin escape: el jugador acaba tumbado). No se encontró la entrada de agarre propia (5 combinaciones X/Y/A/B probadas a lo largo de 100 s: ningún 0x480) ni un choque agarre contra agarre | **no medible con la IA** (necesita dos agarres a la vez): sin contar |
  | AROUSE ;-) +500 | evento 0x2A de la animación (`FUN_001b1080`): suma 500 y da 100 de ki al rival (una burla) | B3 no tiene burla (tampoco los entrenamientos de IW: "burla no existe en B3") ni otro movimiento que dé ki al rival | **sin equivalente**: sin contar |

- Arreglo de paso, **THROW**: el código 0x480-0x48F del jugador lo llevan los dos del agarre (el que agarra 0x480, la víctima
  0x481: rival 15/480 y jugador 15/481 a la vez) y el jugador cobraba +300 al ser agarrado. Ahora solo cuenta si el código del
  rival no es un agarre menor. Sin probar el agarre del jugador (no se encontró su entrada).
- El desglose del resultado dice ahora "Sucesos del combate N + Vida N + Tiempo N: la mitad cuenta" y, debajo, los
  sucesos nuevos con su etiqueta de B1 y su suma ("REFLECT +600  ESCAPE... -300"; captura `...073525_satan_fix_resultado`).
  `PuntosB1` no cambia: todos entran en `Fin::puntos` por `Anotar` (tope de 0 tras cada suma, como `0x1AF6F0`).
- Herramientas de prueba nuevas (solo para medir, con variable de entorno): `DBZ3_PASARELA_PILOTO=1..5` (el jugador juega
  solo por el mando virtual: 1 ondas con habilidades y a mano contra un muñeco de bolas de ki, 2 esquivas/contraataques contra la IA, 3
  retrocesos/avances y ataques repetidos, 4 búsqueda del agarre, 5 disparos continuos con machaque en el choque; los dos
  luchadores no mueren y llevan el ki lleno), `DBZ3_COMBATE_MUNECO=ki` (el rival quieto tira una bola de ki cada 0,75 s)
  y `DBZ3_COMBATE_MUNECO=pk`. Escenarios `.toml` de las pruebas en `D:\tmp\satan2\` (`ondas`, `escape`, `contra`, `agarre`,
  `choque2`, `resultado`); se lanzan con `python tools\pruebas.py correr satan_fix D:/tmp/satan2/ondas.toml`.

### 4. Cinemáticas y mapas: todo es de B3, no hace falta ningún mapa de B1
- Los 11 registros de la tabla llevan el escenario 14 de B1 (`CHG_CELL RING`, +0x71 = 0x0E en los 11); el modo lo pone en el
  mapa 50 de B3 (ring de los Juegos de Cell, `EscenarioB3`). No hay ningún otro mapa en el modo.
- Las escenas (`cinematica.cpp`) corren en el motor de combate de B3: luchadores y poses de B3 (entrada `0x280`, victoria
  `0x2A0`), cámara de `cine.cpp`, el mismo mapa 50. De la ISO de B1 solo salen las imágenes de las charlas (669), las voces
  ADX y las pantallas del modo (666/668); la ISO europea no trae ningún vídeo (ronda 3).
- Comprobado con capturas: `_r3_cine` (`20261017-062532_satan_fix__r3_cine`: tablero, cuatro planos 3D con Mr. Satán y Ten Shin
  Han de B3 en el ring de B3, y el combate) y `satan` (`20261017-062241_satan_fix_satan/f_escena.png`): PASS.

## Lo que queda (de este frente)
- Los multiplicadores de A16, Mr. Satán herido y ki lento de B1 (datos por personaje sin descifrar), ANTI-THROW y AROUSE de B1 (sin equivalente medido en B3, §3), el REFLECT con un golpe y el THROW del jugador (sin probar), el efecto real del nivel de IA en el comportamiento (§2) y el reparto de los 12 niveles de B1.
- Textos propios en francés, alemán e italiano (hoy salen en inglés; las charlas del original sí van en los 5).
- ISO americana de B1 (sin copia en disco: la firma de la tabla y del banco deberían valer; sin probar; la entrada 0x176
  del AFS para los niveles de IA tampoco).
- Pruebas: `DBZ3_PASARELA_TRAZA=1` (vida y ki cada segundo y nivel interno de la IA, solo US) y `DBZ3_PASARELA_NIVEL=0..2`
  (fuerza el nivel del COM) para medir. Reglas: `python tools\pruebas.py correr satan_fix _r3_golpes --env
  DBZ3_TEST_PASARELA=auto,seg=8,desde=7,dificultad=2` (A16; 9 Trunks; 13 Piccolo).
- Siguientes modos: Torneo de B2 (bajo, con el torneo nativo), misiones de IW que son combates (medio), Dragon World
  (alto). Detalle en `docs/16_pasarela_modos/`.
