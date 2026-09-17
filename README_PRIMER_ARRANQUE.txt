PRIMER ARRANQUE - DBZ Budokai 3 HD Collection
================================================

Este paquete es de UN SOLO ARCHIVO: ejecutas dbz3.exe y listo. No hay variantes
ni carpetas que elegir: el runtime funciona en cualquier CPU x64 (desde Core 2,
2006, en adelante). Si tu maquina no lo arranca, no es por "falta de una
variante" (no las hay).

-----------------------------------------------------------------------
PASO 1 - Coloca los datos del juego
-----------------------------------------------------------------------
Tienes CUATRO formas validas (usa la que prefieras):

  Opcion A (recomendada) - carpeta "assets":
    <carpeta del juego>\
      dbz3.exe
      assets\
        default.xex
        us\
        eu\

  Opcion B - carpetas sueltas junto al ejecutable:
    <carpeta del juego>\
      dbz3.exe
      default.xex
      us\
      eu\

  Opcion C - el volcado del disco TAL CUAL (lo mas comodo):
    <carpeta del juego>\
      dbz3.exe
      DBZ3\
        yae3_xenon.xex     <- se llame como se llame, sin renombrar
        us\
        eu\
    El launcher encuentra el ejecutable de Budokai 3 solo (por tamano y
    checksum) y monta la carpeta DBZ3\ automaticamente.

  Opcion D - el ISO directamente (lo mas facil):
    Deja tu .iso de Budokai 3 HD Collection junto a dbz3.exe. El launcher lo
    detecta solo y juega directamente desde el disco: no hace falta extraer ni
    copiar nada. Tambien puedes elegir el archivo con "ISO (.iso)" en el
    launcher. Funciona con el ISO ORIGINAL completo (el que trae el menu de la
    HD Collection en la raiz): el launcher coge de dentro el ejecutable de
    Budokai 3. (Nota: los mods necesitan la carpeta extraida, opciones A o B.)

El launcher detecta cual usas. Tambien puedes elegir la fuente con los botones
"Carpeta extraida" o "ISO (.iso)" en el launcher si los datos estan en otra
ubicacion.

IMPORTANTE - el ejecutable:
- NO hace falta renombrar nada a "default.xex": el launcher busca el ejecutable
  de Budokai 3 por tamano y checksum (nombres tipicos: yae3_xenon.xex,
  yae3_xenon_eu.xex) y lo prepara el solo en su carpeta user_data\xex_cache.
  NUNCA escribe dentro de tu carpeta de juego.
- Puedes usar el ejecutable US/NA (yae3_xenon.xex) o el EU/PAL
  (yae3_xenon_eu.xex): el juego lleva la recompilacion de ambos dentro y elige
  el correcto automaticamente.
- La region EU/PAL (carpeta eu/) y el idioma se eligen en el launcher.
- Si pones el MENU de la HD Collection (el default.xex de la raiz del disco) el
  launcher te avisa y bloquea Play: ese ejecutable no esta en el nucleo.
- Si pones un ejecutable de DBZ Budokai HD (DBZ1) por error, el launcher te lo
  avisa y te pide que uses el launcher de DBZ1 (dbz1.exe).

-----------------------------------------------------------------------
PASO 2 - Instala mods (opcional)
-----------------------------------------------------------------------
Coloca los mods en la carpeta "mods" (cada mod en su carpeta, con manifest.txt).
El launcher los lista y activa en la pestana "Mods". Ver MODDING_README.md.

-----------------------------------------------------------------------
PASO 3 - Solucion de problemas
-----------------------------------------------------------------------
- Si el juego se cierra de golpe, te aparecera una ventana con la ruta del
  registro (logs\, junto al juego). Comparte ese archivo para diagnosticar.
- En logs\ se registra que ejecutable se ha detectado (ruta, tamano, checksum),
  el estado y la carpeta de datos: es lo primero que hay que mirar si algo falla.
- Si al pulsar PLAY no pasa nada: normalmente es un mensaje de "ejecutable no
  reconocido" en el banner del launcher (pon el ejecutable de Budokai 3, no el
  menu de la HD Collection).
- Los mods y ajustes se guardan en la carpeta del juego (junto a dbz3.exe).
- Necesitas las DLLs de runtime de C++ de Microsoft (msvcp140.dll,
  vcruntime140.dll) que ya vienen incluidas junto al juego.