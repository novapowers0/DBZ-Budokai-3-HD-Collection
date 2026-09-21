# Mods nativos

Los mods nativos cambian el progreso o el comportamiento del juego. Son una
categoria distinta de los mods AFS de modelos, texturas, audio y movesets.

## Catalogo inicial

El launcher ya reserva el catalogo para:

- `Guardar al 100%`: objetos, tecnicas y contenido permanente.
- `Vida infinita`: vida del jugador durante los combates.
- `Ki infinito`: ki del jugador durante los combates.

Estos candidatos aparecen como **En investigacion** hasta que se valide su
implementacion en la version Xbox 360/ReXGlue. No se aplican offsets de
GameShark de PS2 directamente: esos codigos parchean la memoria MIPS de PS2 y
no son offsets del save ni del guest PowerPC.

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

Por eso `Vida infinita` y `Ki infinito` aparecen como **Sin codigo encontrado**.
No existe actualmente una instruccion del tipo "activa este cheat y tendras
todo" que podamos recomendar honestamente para este port.

La via Xenia sigue siendo viable como referencia de formato: si se descubre un
parche para `4E4D0856`, habria que convertir sus escrituras a hooks/cambios del
guest recompilado, comprobar US y EU y encapsularlo en un mod nativo propio.

## Guardados

El runtime almacena el contenido del juego bajo `user_data/dbz3/`, con una
carpeta de perfil, title id, tipo de contenido y nombre de paquete. El archivo
de progreso observado es `DBZ3/data.bin` y comienza por `#SPF 1.0`.

Antes de implementar un modificador se deben obtener saves diferenciales del
port: partida nueva, compra de una capsula, compra de una tecnica, desbloqueo
de un personaje y save completo. Asi se pueden identificar flags y checksums
sin asumir que el formato PS2 sea reutilizable.

El launcher detecta los `data.bin` reconocibles y permite crear una copia
`data.bin.native.bak`. La copia de seguridad es previa a cualquier futuro
transformador y no modifica el save original.

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
