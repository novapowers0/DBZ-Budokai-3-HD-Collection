# Mods nativos

Los mods nativos cambian el progreso o el comportamiento del juego. Son una
categoria distinta de los mods AFS de modelos, texturas, audio y movesets.

## Catalogo actual

- `Desbloquear todo`: **disponible**. Instala una partida completa de Xbox 360
  (personajes, trajes, capsulas y modos) y permite restaurar la partida anterior.
- `Vida infinita` y `Ki infinito`: **sin codigo encontrado**. No aparecen en el
  launcher actual porque no hay un parche validado para este port.

No se aplican offsets de GameShark de PS2 directamente: esos codigos parchean la
memoria MIPS de PS2 y no son offsets del save ni del guest PowerPC.

## Desbloquear todo y restaurar tu partida (jugador)

Con el juego cerrado, pulsa **Desbloquear todo** en la parte inferior del
launcher y confirma. Instala la partida completa de Budokai 3 para US o EU:
personajes, trajes, capsulas y modos. **Sustituye tu progreso del juego
original**, pero antes guarda una copia automatica. El progreso del **Heroe y de
Tag Team se conserva**.

Para volver a tu partida anterior, abre el mismo boton, elige **Restaurar mi
partida** y confirma. La partida que tenias en ese momento tambien se guarda
aparte antes de sustituirla.

- Las copias estan en `user_data/dbz3/respaldos_partida` (o en la carpeta de
  datos que indique el launcher). La restauracion guarda la partida sustituida
  en `antes_restaurar`.
- Si antes de desbloquear no tenias partida, restaurar deja otra vez la
  instalacion sin partida.
- Si falta `mods_nativos\partida_100` junto a `dbz3.exe`, reinstala la version
  completa.
- No se puede aplicar ni restaurar con el juego abierto o con otra ventana de
  `dbz3.exe` de la misma instalacion en marcha.
- Si el cambio se interrumpe, el launcher pide usar **Restaurar mi partida**
  antes de volver a desbloquear. Un respaldo danado o no verificable no se
  restaura; los respaldos antiguos sin checksum quedan para recuperacion manual.
- Si falta espacio o un programa bloquea los archivos, el launcher lo indica y
  no deja la partida a medias.

## Unlock everything and restore your save (player guide)

Close the game, click **Unlock everything** at the bottom of the launcher and
confirm. This installs the complete Budokai 3 save for US or EU: characters,
costumes, capsules and modes. It **replaces your original-game progress**, but
an automatic backup is made first. **Hero and Tag Team progress is preserved.**

To return to your previous save, open the same button, choose **Restore my
save** and confirm. The save you had at that moment is also backed up before it
is replaced.

- Backups are stored in `user_data/dbz3/respaldos_partida` (or the data folder
  shown by the launcher). Restoring also backs up the replaced save in
  `antes_restaurar`.
- If you had no save before unlocking, restoring leaves the installation with
  no save again.
- If `mods_nativos\partida_100` is missing next to `dbz3.exe`, reinstall the
  complete release.
- You cannot apply or restore while the game is running or while another
  `dbz3.exe` window from the same installation is open.
- If the change is interrupted, the launcher asks you to use **Restore my save**
  before unlocking again. A damaged or unverifiable backup is not restored;
  older backups without checksums are kept for manual recovery.
- If disk space is low or a program locks the files, the launcher says so and
  does not leave the save half-written.

## Investigacion Xenia

Xenia Canary mantiene un repositorio separado de parches:

- `xenia-canary/game-patches`
- Formato: `patches/<TITLE_ID> - <nombre>.patch.toml`
- Las entradas contienen escrituras `be8`, `be16`, `be32`, `be64`, `array`,
  `f32`, `f64` o strings en direcciones del guest.

El juego de este proyecto tiene el Title ID `4E4D0856`. La busqueda del
repositorio oficial no contiene ninguna entrada `4E4D0856` ni un parche de
Budokai 3. Las entradas de Dragon Ball encontradas son de *Burst Limit*
(`424107DC`) y no son reutilizables.

El ReXGlue usado por este proyecto tampoco incluye el lector de
`patch.toml` de Xenia Canary. El Xenia original tiene ademas un mecanismo
distinto de parche XEX (`default.xexp`), pero eso no equivale a un cheat y no se
puede copiar directamente al codegen dual de este proyecto.

Por eso `Vida infinita` y `Ki infinito` siguen **sin codigo encontrado** y no se
ofrecen en el launcher. No existe actualmente una instruccion del tipo "activa
este cheat y tendras todo" que podamos recomendar honestamente para este port.

La via Xenia sigue siendo viable como referencia de formato: si se descubre un
parche para `4E4D0856`, habria que convertir sus escrituras a hooks/cambios del
guest recompilado, comprobar US y EU y encapsularlo en un mod nativo propio.

## Guardados

El runtime almacena el contenido del juego bajo `user_data/dbz3/`, con una
carpeta de perfil, title id, tipo de contenido y nombre de paquete. El archivo
de progreso observado es `DBZ3/data.bin` y comienza por `#SPF 1.0`.

El desbloqueo actual sustituye solo `data.bin` y `DBZ3.header` del titulo
`4E4D0856`; no toca el Heroe/HUB ni el historial de Tag Team. Antes de escribir
crea una carpeta `respaldos_partida/v2_*` con inventario, tamano y checksum
XXH3-128 de cada archivo, y recuerda con `.absent` si la partida no existia.
El marcador `pending` permite recuperar un corte antes de la primera
sustitucion. Restaurar valida todos los archivos, guarda la partida sustituida
en `antes_restaurar` y rechaza un respaldo danado. Los respaldos antiguos sin
checksum se conservan, pero no se restauran automaticamente.

## Regla de seguridad

Un mod nativo no se considerara listo hasta que:

1. Cree una copia de seguridad automatica.
2. Valide el formato y la region del save.
3. Escriba de forma atomica.
4. Permita restaurar la copia anterior.
5. Se compruebe que el juego carga, guarda y vuelve a leer el progreso.

Para cheats de memoria, ademas:

6. El parche debe corresponder al Title ID `4E4D0856`, al hash/versión exacta
   del XEX y a la región correcta.
7. Debe probarse en memoria del guest; una dirección PS2 o una dirección de
   otro juego de Xenia no sirve.
