PRIMER ARRANQUE - DBZ Budokai 3 HD Collection
================================================

Este paquete es de UN SOLO ARCHIVO: ejecutas dbz3.exe y listo. No hay variantes
ni carpetas que elegir: el runtime funciona en cualquier CPU x64 (desde Core 2,
2006, en adelante). Si tu maquina no lo arranca, no es por "falta de una
variante" (no las hay).

-----------------------------------------------------------------------
PASO 1 - Coloca los datos del juego
-----------------------------------------------------------------------
Tienes DOS disposiciones validas (usa la que prefieras):

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

El launcher detecta cual usas. Tambien puedes pulsar "Seleccionar carpeta de
datos..." en la pestana principal si los datos estan en otra ubicacion.

IMPORTANTE - default.xex:
- Puedes usar el ejecutable US/NA (yae3_xenon.xex) o el EU/PAL
  (yae3_xenon_eu.xex): el juego lleva la recompilacion de ambos dentro y elige
  el correcto automaticamente.
- La region EU/PAL (carpeta eu/) y el idioma se eligen en el launcher.

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