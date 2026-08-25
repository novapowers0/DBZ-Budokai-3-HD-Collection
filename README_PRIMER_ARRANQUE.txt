PRIMER ARRANQUE - DBZ Budokai 3 HD Collection
================================================

El paquete incluye dos variantes del runtime:
  - dbz3_avx2\   -> optimizada (requiere CPU con AVX2, Intel 4a gen+ / AMD Ryzen+)
  - dbz3_legacy\ -> compatible (funciona en cualquier CPU x64)

NO elijas una: dbz3.exe (este archivo) detecta automaticamente tu CPU y lanza
la variante correcta. En CPUs sin AVX2, dbz3.exe usara dbz3_legacy\ por si solo.

-----------------------------------------------------------------------
PASO 1 - Coloca los datos del juego
-----------------------------------------------------------------------
Tienes DOS disposiciones validas (usa la que prefieras):

  Opcion A (recomendada) - carpeta "assets":
    <carpeta del juego>\
      dbz3.exe
      dbz3_avx2\
      dbz3_legacy\
      assets\
        default.xex
        us\
        eu\

  Opcion B - carpetas sueltas junto al ejecutable:
    <carpeta del juego>\
      dbz3.exe
      dbz3_avx2\
      dbz3_legacy\
      default.xex
      us\
      eu\

El launcher detecta cual usas. Tambien puedes pulsar "Seleccionar carpeta de
datos..." en la pestana principal si los datos estan en otra ubicacion.

IMPORTANTE - default.xex:
- Puedes usar el ejecutable US/NA (yae3_xenon.xex) o el EU/PAL
  (yae3_xenon_eu.xex): el paquete incluye el nucleo recompilado de cada uno y
  el launcher elige el correcto automaticamente.
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
  registro (logs\ junto a la variante usada). Comparte ese archivo para
  diagnosticar.
- Los mods y ajustes se guardan en la carpeta de la variante usada
  (dbz3_avx2\ o dbz3_legacy\).
- Necesitas las DLLs de runtime de C++ de Microsoft (msvcp140.dll,
  vcruntime140.dll) que ya vienen incluidas junto al juego.