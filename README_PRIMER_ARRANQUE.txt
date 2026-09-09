PRIMER ARRANQUE - DBZ Budokai 3 HD Collection
================================================

Este paquete es de UN SOLO ARCHIVO: ejecutas dbz3.exe y listo. No hay variantes
ni carpetas que elegir: el runtime funciona en cualquier CPU x64 (desde Core 2,
2006, en adelante). Si tu maquina no lo arranca, no es por "falta de una
variante" (no las hay).

-----------------------------------------------------------------------
PASO 1 - Coloca los datos del juego
-----------------------------------------------------------------------
Tienes TRES formas validas (usa la que prefieras):

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

  Opcion C - el ISO directamente (lo mas facil):
    Deja tu .iso de Budokai 3 HD Collection junto a dbz3.exe. El launcher lo
    detecta solo y juega directamente desde el disco: no hace falta extraer ni
    copiar nada. Tambien puedes elegir el archivo con "ISO (.iso)" en el
    launcher. (Nota: los mods necesitan la carpeta extraida, opciones A o B.)

El launcher detecta cual usas. Tambien puedes elegir la fuente con los botones
"Carpeta extraida" o "ISO (.iso)" en el launcher si los datos estan en otra
ubicacion.

IMPORTANTE - default.xex:
- Puedes usar el ejecutable US/NA (yae3_xenon.xex) o el EU/PAL
  (yae3_xenon_eu.xex): el juego lleva la recompilacion de ambos dentro y elige
  el correcto automaticamente.
- La region EU/PAL (carpeta eu/) y el idioma se eligen en el launcher.
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
- Los mods y ajustes se guardan en la carpeta del juego (junto a dbz3.exe).
- Necesitas las DLLs de runtime de C++ de Microsoft (msvcp140.dll,
  vcruntime140.dll) que ya vienen incluidas junto al juego.