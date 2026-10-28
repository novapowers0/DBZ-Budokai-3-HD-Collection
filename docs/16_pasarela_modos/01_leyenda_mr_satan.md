# 01 · La leyenda de Mr. Satán (Budokai 1)

## 1. Qué es

Modo de Budokai 1 (se desbloquea al acabar la historia). Mr. Satán se enfrenta en los Juegos de Cell a **11
retadores seguidos**. Antes de cada uno (desde el 2.º) hay una charla cómica de Mr. Satán (lo que dice en alto y
lo que piensa) y un aviso con la regla especial del combate. Al final, puntos (vida y tiempo que sobran) y una
contraseña. Tres dificultades: Novato, Experto, Avanzado.

## 2. Dónde están los datos (ISO europea `SLES_512.33`; la americana sin probar)

ELF: un solo segmento, fichero `0x80` → dirección `0x100000`. Código del modo: `satanmode.c`, 0x22D000-0x234E00
(etiquetas "SATAN MODE MAIN", "SATAN DEMO MAIN", "SATAN RESULT MAIN", "SATAN CLEAR MAIN"). Escenas del modo en
0x21EFF0-0x21F2B0 (título, demo, combate, resultado, final).

### 2.1 Tabla de combates (ELF, fichero `0x25E040`, dirección `0x35DFC0`)

11 registros de 128 B, uno por retador (la misma estructura que los combates de la historia, tabla `0x35CA40`).
Se encuentra por firma (vale cualquier región): personaje 21 (Mr. Satán) en `+0x20` y modo 3 en `+0x70` en los 11.

| Offset | Tipo | Campo |
|---|---|---|
| +0x00 | vec4 | (sin usar en el modo) |
| +0x10 | vec4 | posición del lado 1 (x = -15) |
| +0x20 | u8 | personaje del lado 1 (21 Mr. Satán) |
| +0x21 / +0x22 | u8 | variante (sin confirmar) / **forma inicial** (Goku 4 = SSJ, Gohan 1 = SSJ, Cell 2 = perfecto) |
| +0x26 | u16 | cantidad de luchadores seguidos del lado (la de los rivales es **+0x56**: 5 Cell Jr.; la del lado 1 siempre 1) |
| +0x28 | s16 | (-1 en el modo; en la historia, número de capítulo) |
| +0x2A | u16 | **vida**; `0xFFFF` = no se recupera (sigue la del combate anterior) |
| +0x2C | u16 | ki inicial |
| +0x2E, +0x30 | u16, n x u16 | técnicas (cápsulas) |
| +0x40 | vec4 | posición del lado 2 (x = +15) |
| +0x50..+0x6F | | lado 2, mismos campos que +0x20..+0x3F |
| +0x70 | u8 | modo (3 = Mr. Satán; 0 = historia) |
| +0x71 | u8 | escenario (14 = `CHG_CELL RING`) |
| +0x72 | u8 | música (hipótesis) |
| +0x74 | s32 | tiempo en cuadros: 5940 = 99 s, 3600 = 60 s, -1 sin límite, -2 el de las opciones |

Escenarios de B1: registros de 0x98 B con el nombre al principio (0x337460: BOKUSOUTI, SUMMER SAN, IWAYAMA,
NAMEC, RETTOU, CELL RING, SEISHIN, SHINDEN, TENKAITI, TIKYUU HAK, NAMEC HAK, CHG_..., SANKAN, CELL RING HAK...).
Personajes de B1: registros de 564 B en `0x259390` (Mr. Satán = 21; ojo: en B3 es el 14).

Lo que sale de la tabla (`python "mod center hd/pasarela/pasarela.py" info`):

| # | Rival (B1 → B3) | Regla del original (aviso y tabla) |
|---|---|---|
| 1 | Yamcha (8 → 13) | normal, 99 s |
| 2 | Ten Shin Han (7 → 12) | 99 s; el aviso anuncia que la vida no se recuperará en el siguiente |
| 3 | Krilin (4 → 10) | **vida de Mr. Satán sin recuperar** (`0xFFFF`) |
| 4 | Androide 16 (16 → 28) | ki 400; "armadura" (difícil de dañar; regla en código, sin localizar) |
| 5 | Trunks (5 → 8) | ki 400; Mr. Satán herido: atacar con ki (regla en código) |
| 6 | Vegeta (3 → 7) | **60 s** |
| 7 | Piccolo (6 → 11) | forma 2; cuesta cargar ki (regla en código) |
| 8 | Cell Jr. (23 → 43) | **5 seguidos de 1000 de vida**, tiempo -2 = **el de las opciones** (G+0x2C08C) |
| 9 | Goku (0 → 0) | forma 4 (SSJ), ki 600 |
| 10 | Gohan adolescente (2 → 3) | forma 1 (SSJ), **vida sin recuperar** |
| 11 | Cell (20 → 33) | forma 2 (perfecto), ki 700, **sin límite de tiempo** |

Dificultad: `0x5E9C10` (0 Novato, 1 Experto, 2 Avanzado); el nivel del COM sale de tablas por personaje
(`0x26C698`, puntero `+0x48` del fichero del personaje, por dificultad). Ronda en curso: `0x5E9C14` (0..10; al
ganar sube y con 11 va al final, `0x2175B0`).

**Puntos** (desensamblado 2026-10-16, rabbitizer; script `E:\DBZ3HD_trabajo\P3\dis.py`): `0x1AF6F0(luchador, puntos,
etiqueta)` solo actúa en el modo de Mr. Satán: suma en `luchador+0x4F4C` (sin bajar de 0) y mete {puntos, etiqueta}
en un anillo de 8 avisos (`+0x63C0`). Sus 18 llamadas: HIT 30 × min(n, 10) al que pega (n = golpe n-ésimo del combo,
`+0x4F40` del que recibe; solo si n < 21), DAMAGE... −20 × n al que recibe (`0x1AB700`), COMBO FIN 60 × n y SPECIAL
FIN 2000 en el KO final (`0x1AD670`), THROW 300 y SPECIAL 450 (`0x19F6C4`), ANTI-THROW 400, REFLECT 300, BURST! 400,
CUT-OFF 150, RECOVERY 100, AROUSE ;-) 500, ESCAPE... −300 × (n − 1), DULL... −200 × k. Al acabar (`0x198E18`) el
ganador suma LIFE (su vida − la del otro) y TIME (segundos que sobran × 20), y la leyenda se queda con **la mitad
de todo** su marcador (`0x198F00`).

**Voces** (`satanmode.c` → `0x1369D0(canal, entrada ADX)` → `ADXT_StartAfs`): charla del retador r (2.º..11.º) =
`ADX_JP.AFS` 1269 + 2 (r − 2) (lo que dice) y la siguiente (lo que piensa), tabla del ELF `0x3507F0`; título 1261
(19 s); 1262-1268 los consejos del título; 1259/1263 música (canal 0), 1178/1180 jingles del resultado y del final.

### 2.2 Imágenes (AFS de cada idioma, `/USR/DATA_EN|FR|GE|SP|IT.AFS`, entradas 665-669)

| Entrada | Tipo | Qué es |
|---|---|---|
| 665 | #AMA (30) | disposición de la pantalla del modo |
| 666 | #AMT (10) | pantalla del modo: 0 fondo azul (640x448), 1 dibujo de Mr. Satán (536x448), 2 logo **traducido** ("La Leyenda de Mr. Satan"), 4 copyright, 5 "Pulsa START", 6 cartel, 7-9 Novato/Experto/Avanzado |
| 667 | #AMA (141) | disposición del resultado |
| 668 | #AMT (26) | resultado: "Challenger 1st..11th", "NEXT BATTLE", puntos, contraseña, caras chibi de los 11 retadores, consejos de Mr. Satán, letras y cifras |
| 669 | #AMT (22) | **charlas**: 2 por retador desde el 2.º (0, 2, 4...: charla de Mr. Satán de ~528x136, texto negro y gris; 1, 3, 5...: aviso de la regla, ~380x64, texto claro), 20 = puntos y contraseña, 21 = web |

El banco de charlas se busca por firma (#AMA, #AMT(10), #AMA, #AMT(26), #AMT(22) seguidos), así vale otra región.
#AMT de PS2: entradas de 0x30 (+0x08 PSM 0x14 = 4 bpp / 0x13 = 8 bpp, +0x10 ancho/alto, +0x14/+0x18 datos,
+0x24/+0x28 CLUT); 32 B de cabecera GIF delante de datos y CLUT; 4 bpp lineal (nibble bajo = píxel par); alfa
0..0x80; la CLUT de 256 en orden CSM1. Las mismas reglas que `awo_tools/amt_ps2.py`.

## 3. Qué se reproduce ya (prototipo)

Entrada **"Leyenda de Mr. Satán"** del HUB (clave `satan`, página 2). Con la ISO de B1 en `ps2_games`:

1. Título como el de B1: fondo azul (a toda la ventana sin "Bordes dorados"), logo, dibujo de Mr. Satán y "Pulsa
   START" (666 #5) parpadeando; con START, el letrero de la dificultad y un texto nuestro en el bocadillo.
2. Por cada retador: charla del original (imagen de texto en el bocadillo del juego) con la cara del rival a la
   derecha; tarjeta "Retador N de 11" con las dos caras del marcador de B3, "VS" y el aviso del original en una
   franja oscura; combate nativo (Mr. Satán contra el equivalente de B3, en el ring de los Juegos de Cell de B3:
   mapa 50, registro 8). Cell Jr.: 5 combates seguidos ("1/5"...).
   Reglas del original por `dbz3::hub::RequestBattleEx` (hub.cpp): tiempo de la tabla (99 s; 60 s contra Vegeta;
   sin límite contra Cell), Cell Jr. con 1000 de vida, vida de Mr. Satán que no se recupera (Krilin, Gohan y del
   2.º al 5.º Cell Jr.: la que le quedó al ganar el anterior) y formas iniciales medidas en B3 (Goku 2 = SSJ, la 1
   es el Kaioken; Gohan adolescente 1 = SSJ; Cell 2 = perfecto). La forma 2 de Piccolo en B1 no tiene equivalente
   visible en B3: sale en su forma base.
3. Ganar: siguiente paso (se guarda en `user_data/dbz3/pasarela/progreso.toml`). Perder (o salir desde la pausa):
   "¡Mr. Satán ha caído!" con A reintentar / B salir. B en cualquier pantalla: sale al HUB y guarda el avance.
4. Final: "¡Leyenda completada!" y vuelta al HUB (el avance vuelve a 0).

Sin la ISO la entrada no sale (con el parche del HUB, sale en gris y el bocadillo explica qué falta y dónde ponerla).

## 4. Assets

| Qué | Propio (va en la versión) | Del juego de origen (de la ISO del jugador, en memoria) |
|---|---|---|
| Lista de combates y reglas | tabla B1→B3 de personajes y escenarios (datos) | tabla del ELF |
| Textos | presentación, tarjeta, derrota y final (es/en; el resto de idiomas, en inglés por ahora) | charlas y avisos (imágenes, 5 idiomas) |
| Arte | marco, bocadillo, tiras doradas, botones, caras del marcador (de B3, `native_ui`, también en tiempo de ejecución) | fondo azul, logo, dibujo de Mr. Satán |

Sin las imágenes del banco (ISO de otra región con otra disposición) el modo sigue con los combates y nuestros textos.

## 5. Qué falta (y cuánto cuesta)

| Falta | Cómo | Coste |
|---|---|---|
| ~~Vida que no se recupera, 5 Cell Jr. de 1000, 60 s, sin tiempo, formas~~ | hecho con `RequestBattleEx` (2026-10-15) | — |
| ~~Ki inicial del rival (300-700 en B1)~~ | hecho (ronda 2): `VigilarCombate` lo escribe al llegar al READY (`X+6100`; 1 barra = 1000 en B3, B1 300 = 3 barras) | — |
| ~~La vida arrastrada acorta también la barra~~ | hecho antes: vida `< 0` = solo la inicial | — |
| ~~Reglas en código (armadura de A16, Mr. Satán herido, ki lento)~~ | **DS143: no existen en B1**. Son los avisos 669 #5 (A16), #7 (Trunks) y #11 (Piccolo), texto de Mr. Satán: en `b1.c` no hay ninguna comparación con los IDs 5/6/16 (el único caso por personaje es Cell Jr. 0x17/forma 3) y el daño pasa por la fórmula general `0x1AD0D0` sin multiplicadores del modo. El puerto ya no inventa armadura/solo-ki/ki lento | — |
| ~~Dificultad Novato/Experto/Avanzado~~ | hecho (ronda 2): el título lleva el letrero de B1 (666 #6) con las 3 dificultades (666 #7-9); la elegida es el **nivel de la IA del COM** (`mgr+35573`: `sub_820FC9B0` lo pone de las opciones en el Duelo y del nivel en el Torneo; lo leen las tablas por personaje de `sub_8213E6C8`, como B1 `charfile+0x48[dificultad]`) | — |
| ~~Puntos y pantalla de resultado~~ | hecho (ronda 2): puntos como B1 (`0x198E18`: LIFE = vida del ganador − la del otro, TIME = segundos que sobran × 20; la leyenda suma la mitad, `0x198F00`) y pantalla de resultado con las piezas de la 668 (#15 tablón y NEXT BATTLE, #16 "Challenger Nth", #19 "Now!!", #21 "You have", #22 cifras, #24 caras de los 11: vencidos en color, el resto en silueta) y total con récord al final (#18). Sin los puntos por golpe de B1 (HIT, COMBO FIN, THROW...: 14 sucesos más con `0x1AF6F0`; necesitan ganchos en el combate) | — |
| ~~Escena larga de la ronda 1 / charla 3D~~ | **DS143**: la ronda 1 usa los **11 clips reales** del `#AMC` de `DATA_EN 2372` hijo 20 (5322 cuadros = 88,70 s) con la cámara y las trayectorias `#AMM` de B1 (`GOK/THL/STN_WAIST`), leídas en marcha de la ISO; los actores son modelos de B3. La **charla 3D añadida** de las rondas 2-11 se quitó (no existe en B1: la charla va en el tablero 0x1C y antes del READY solo están las dos tomas de `FUN_001920b0`) | — |
| ~~Cinemáticas de entrada y final~~ | revisado (ronda 3): la ISO europea **no trae ningún vídeo** (ni PSS/SFD/IPU/M2V, ni dentro de los AFS; el ELF no nombra ninguno; listado en `E:\DBZ3HD_trabajo\D\`). La "entrada" (`SATAN DEMO MAIN`) y el final (`SATAN CLEAR MAIN`, "# CLEAR #") son pantallas del motor con las imágenes 666/668/669, que ya se enseñan: título con "Pulsa START" (666 #5) → letrero de dificultad → charlas → resultado → total (668 #18). Los créditos (`endcredits.c`) son los generales del juego, no del modo. La 669 #20 ("Estos son tus puntos y la contraseña") y #21 (web de 2002) no se usan: la contraseña era para una web que ya no existe | — |
| ~~Voces de Mr. Satán en las charlas~~ | hecho (P3, 2026-10-16): la voz japonesa del original (mapa en §2.1) suena en cada charla y en el título (ADX decodificado en marcha; Windows, por winmm, aparte de la mezcla del juego) | — |
| ISO americana de B1 | probar la firma de la tabla y del banco (no hay copia en disco) | bajo |

## Escenas 3D de introducción (s9b, 2026-10-19; medidas con PCSX2 a cámara lenta)
La entrada de B1 sí tiene escenas 3D (la nota "no trae vídeos" de arriba solo habla de vídeos). La ronda 1 lleva la escena larga de
los Juegos de Cell (~60 s: reportero, ring, Cell, Goku, el coche y Mr. Satán con mochila y humo) y de la 2 a la 11 el combate trae la
presentación normal de B1: Mr. Satán desde abajo con los brazos abiertos (~2,6 s, sube desde negro) y el retador de cerca (~2,3 s) y
"READY?". En el puerto (`pasarela.cpp`, `TomasPresentacion`): de la 2 a la 11 van solo esas dos tomas (sin charla 3D: B1 la
pone en el tablero) con la cámara de `FUN_001920b0`; la ronda 1 lleva la escena larga completa (88,70 s) con los 11 clips del
`#AMC` de `DATA_EN 2372` hijo 20 y la trayectoria de cada actor (Goku/Cell en la previa, Satan en la llegada), con la cámara
anclada al luchador para no sacarlo del ring. **DS145**: la llegada dibuja el **coche** (hijos 10/11/12: `#AMO STC_`, su
`#AMM` de 400 cuadros y su `#AMT`) y el **humo** (13/14/15/16: `#AMO KEMURI_`, `#AMT`, `#AMM` y `#AMP` con el tramo
119..399) leídos en marcha de la misma ISO; las pistas de los huesos quietos son cero y se dejan en su eje de reposo
(si no, el coche se desmonta). **DS148**: coche y humo son la **toma 4** del `#AMC` (cuadros globales 2030..2430; los
dos `#AMM` duran 400 y el `#AMP` da 119..399), así que se cortan en el cuadro 400 y no siguen en las tomas de Satan; la
entrada de B3 (estados 1-3, Cell de espaldas con las alas en primer plano) se tapa a negro hasta que manda la cámara de
B1. El objeto negro en V de las tomas del ring es el **ala de Cell perfecto** del modelo de B3 (`XCEL_LWING/RWING`, rígida
al hueso 0), como en la toma 1 de B1; no es el coche ni el humo. Sus pistas de posición son un desplazamiento sobre el eje
de reposo (el coche está modelado en la carretera, `STC_BASE` p = (-368,2; 11,5; 155,1)): `SumarReposo()` las suma para
que `ps2::Posar` no mande el coche al origen del ring. También se corrigió el **encuadre de Cell**: su cadera de B1 (16,63) es su altura, no una
plataforma, así que el ancla resta la altura real del modelo de B3 (Cell 15,49; Goku 10,45; Satan 11,44) en vez de 10,0
para todos (antes la cámara de sus tomas iba 6,6 u alta), y el primer plano se gira +90° con el eje de B1 (Cell mira a lo
largo de Z) al de B3 (los lados se miran a lo largo de X). Queda el arte del ring de B1
(`escenarios_convertir.py mod 11 --juego b1`) y la posición del coche/humo en pantalla, que es `[VERIFICAR]`: se puede
mover sin compilar con `DBZ3_TEST_PROPS_LLEGADA_FILE` (`car dx dy dz`, `humo dx dy dz`).
