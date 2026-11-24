# Oráculo B2 PAL del tablero

Ejecuta funciones del ELF original con el intérprete MIPS existente y compara
sus salidas con modelos Python revisados del C++. Informe y límites:
[`B2_ORACULO.md`](../../studies%205/ENTREGAS/B2_ORACULO.md).

## Reproducir

Desde la raíz del proyecto, con el Python que ya tiene `rabbitizer`:

```powershell
python -B "tools/b2_oraculo/banco.py" --autoprueba
python -B "tools/b2_oraculo/banco.py" --todos-estados --salida "E:/DBZ3HD_trabajo/oc/B2_oraculo_nueva_corrida.json"
python -B "tools/b2_oraculo/verificar.py" "E:/DBZ3HD_trabajo/oc/B2_oraculo_nueva_corrida.json"
```

La carpeta de salida debe existir y el JSON debe ser nuevo. El banco no
sobrescribe evidencias. `--todos-estados` añade la selección de aliados en
los demás `.p2s` de la carpeta del estado inicial.

Casos mínimos, con entrada y resultados impresos:

```powershell
python -B "tools/b2_oraculo/banco.py" --caso "aliados/id28/abierto0"
python -B "tools/b2_oraculo/banco.py" --caso "aliados/id28/abierto1"
python -B "tools/b2_oraculo/banco.py" --caso "deseos/inventario/0"
python -B "tools/b2_oraculo/banco.py" --caso "deseos/inventario/1"
python -B "tools/b2_oraculo/banco.py" --caso "rivales/reloj_distinto"
python -B "tools/b2_oraculo/banco.py" --caso "fase/limite/9"
```

También admite `--area fase|rivales|aliados|capsulas|deseos`, `--estado RUTA`
y `--ayuda`. `--autoprueba` se ejecuta sin filtros.

## Entradas existentes

- ELF: `E:\DBZ3HD_trabajo\ghidra\elfs\b2.elf`, B2 PAL `SLES-51839`.
- Estado predeterminado:
  `E:\DBZ3HD_trabajo\grabaciones_original\b2_tablero_f4_mov\m00.p2s`.
- Cargador: `tools/mips_perfil.py`; intérprete: `tools/iw_mips/ee.py`.

No descarga dependencias. Cada caso usa una copia de RAM. El ELF se fija por
SHA-256; se comprueba cada instrucción ejecutada contra sus bytes originales.
Una instrucción desconocida, E/S o llamada sin contrato invalida el caso.
Los servicios sustituidos se declaran en `original.py` y en las trazas.

## Archivos y mantenimiento

- `original.py`: ejecución del original y contratos de servicios.
- `modelo_port.py`: modelos de los fragmentos C++ indicados en `fuentes.py`.
- `fuentes.py`: hashes revisados; rechaza un modelo que quedó desactualizado.
- `banco.py`: corpus, controles, filtros y evidencia JSON.
- `verificar.py`: comprueba una evidencia retenida contra sus entradas actuales
  y cuenta sus resultados; no vuelve a ejecutar las funciones.

Tras cambiar el C++, revisar el modelo y actualizar **solo los hashes de los
fragmentos revisados** en `fuentes.py`. Ejecutar de nuevo controles y corpus,
conservar la evidencia anterior. La salida informativa de `fuentes.py` no
autoriza a aceptar hashes nuevos sin esa revisión.

## Interpretar el resultado

- Banco: código **0** sin diferencias; **1** con diferencias; **2** incompleto
  o con error. La corrida entregada devuelve **1**, deliberadamente.
- Autoprueba: **0** si pasan los once controles, incluidos contraejemplos
  conocidos y rechazo de código/modelos alterados. No certifica el port.
- Verificador: **0** si evidencia, trazas, recuentos y entradas actuales
  coinciden; **2** si no. Puede validar una evidencia que contiene diferencias.
- `logica`: resultado con las mismas entradas efectivas.
- `logica_con_azar_acoplado`: misma dirección/rango sorteado; generadores y
  reloj del host quedan fuera.
- `integracion_modelada`: comparación del adaptador de inventario revisado,
  sin ejecutar la persistencia C++.
- `robustez_fuera_dominio` y `frontera_azar`: diferencias deliberadamente
  separadas de los fallos de lógica jugable.

El original sí se ejecuta. El lado del port es un modelo revisado, **no el
binario C++**. Rivales cubre orden de actuación y movimiento aleatorio tipo 3;
no toda la IA ni la aparición de enemigos.
