# Perfil de CPU por función (sin PDB)

Herramientas para saber **qué funciones del código recompilado y del runtime** gastan la
CPU del hilo del guest, sin compilar con símbolos.

- `sampler.cpp` — muestreador externo: suspende un hilo del juego ~1 kHz y guarda su RIP
  (`ms tid rip`). No inyecta nada ni necesita instrumentación. Reanuda el hilo antes de
  escribir; si no puede confirmar la reanudación o falla la salida, termina con error en vez
  de seguir (no garantiza la reanudación si el hilo es inaccesible: en ese caso avisa y para).
- `mkrsp.py` — prepara el relink del exe con `/MAP` (mismos objetos) para tener símbolos.
  **El enlace debe ir a una copia propia del juego**, nunca al `dbz3.exe` de
  `out/build/...` (árbol compartido). El script no enlaza.
- `sym.py` — resuelve cada muestra contra el mapa: módulo y función. La atribución es
  **aproximada**: el mapa solo lista símbolos públicos, así que el código interno (estático,
  clon de LTO, plegado por ICF) cae en el símbolo público anterior; las entradas con `~`
  marcan tramos de más de 4 KB donde eso es probable.
- `perfilar.ps1` — arnés: arranca el juego con el combate de prueba, espera el marcador
  `empieza un combate`, muestrea N segundos y saca la CPU por hilo y las ventanas `perf`.

## Ejemplo

```powershell
python tools\cpu_perfil\mkrsp.py out\build\win-amd64-dual\build.ninja D:\tmp\cpu\dbz3.rsp D:\tmp\cpu\link.cmd D:\tmp\cpu\game\dbz3.exe D:\tmp\cpu\dbz3.map
cmd /c D:\tmp\cpu\link.cmd        # desde out\build\win-amd64-dual; sale en D:\tmp\cpu\game
clang++ -O2 -o D:\tmp\cpu\sampler.exe tools\cpu_perfil\sampler.cpp -lpsapi
powershell -File tools\cpu_perfil\perfilar.ps1 -Dir D:\tmp\cpu\game -Sampler D:\tmp\cpu\sampler.exe -Out D:\tmp\cpu -Mask 5 -Seconds 30
python tools\cpu_perfil\sym.py D:\tmp\cpu\S_*.txt D:\tmp\cpu\dbz3.map
```

`-Mask 5` = CPU0+CPU2, que en el equipo medido (i5-12600KF, 16 hilos) son dos **núcleos
físicos** distintos. En otra topología la máscara no garantiza eso: comprueba el reparto
de núcleos antes de comparar cifras.

El muestreo da cientos de muestras por segundo (≈250–1.000 según la carga; `Sleep(1)` con
resolución de 1 ms); en 30 s son >7.000 muestras, suficiente para ver funciones del 0,5 %.
El muestreador de referencia usado en el informe daba ~1.000/s con un limitador por QPC. El exe no lleva PDB: por eso se relinká con `/MAP`, y el mapa **solo** vale para el
exe de ese enlace.
