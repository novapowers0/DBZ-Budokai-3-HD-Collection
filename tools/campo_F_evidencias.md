# Campo solIWF — correcciones fundadas, verificacion visual pendiente

## Imagenes y medida que se han leido

- Original: `D:/tmp/solIW7/original_campo_083529/campo_040_05.png`.
- Port: run de `D:/tmp/solIW9c/iw9_visual_cierre9e_0x7D0.json`,
  `D:/DBZ3HD/_pruebas/20261031-073126_sol_iw9_visual_cierre9e_0x7D0/campo_048_05.png`.
- Tambien se leyeron la pareja640x240, el estado del actor/luz y el log de esa
  corrida. En las imagenes se distinguen el encuadre distinto del terreno y la
  sombra larga del port hacia delante, frente a la sombra bajo el original.
- Se reprodujo offline la medida **anterior**, usando exactamente RGB320x240,
  luminancia paraSSIM y phash del arnes: **0.5040086818415496 / 22, FAIL**.
  Umbrales **0.45 / 18** intactos. No se ha generado una falsa foto «despues»,
  recortado/registrado la imagen ni cambiado `iw_sol9.py`/`iw_sol9c.py`.

## 1. Proyeccion de campo: no copiar el parametro40 como vertical40

Original EU `E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c`:

- `297140 Camera_InitFieldDefault`: parametro40grados -> camara+B4 en radianes.
- `297DE0 Camera_UpdateFollow`, instrucciones298174..298184: pasa **480**
  (`lui v0,43F0;mtc1 v0,f12`) a `126F50`.
- `126F50`: lee el angulo, llama tangente sobre la mitad y escribe en el contexto
  `+3C` la focal `480/(2*tan(angulo/2))` (division126F98).
- La configuracion de pantalla que copia `1491F0 ModulesReader` desde420300,
  consumida por143FF0/129CB0, contiene nativo512x448 y lienzo **600x450**.
  `129CB0` establece las razones nativo/logico. `12AC30` y `12C9E0` producen
  por tanto una focal igual en ambos ejes del lienzo logico de4:3.

B3 espera un angulo vertical relativo a toda la altura. Para igualar esa focal:

```
450/(2*tan(verticalB3/2)) = 480/(2*tan(parametroIW/2))
verticalB3 = 2*atan((450/480)*tan(parametroIW/2))
```

La copia directa anterior dejaba la focal un **6.25% por debajo** del original.
El parametro40corresponde a **0.6576671rad (~37.68grados verticales)** en B3,
no a0.6981317rad. No se ha elegido ese valor mirando la imagen.

Cambios en `src/iw_minijuegos.h/.cpp`:

- `FovVerticalCampoIW`, helper de conversion.
- Inicializacion de `g_fov_campo` y `FovIW` (seguimiento/scatto).
- FOV de clips de `CamaraIw`: el original tambien los consume por126F50.
  Ojo, objetivo, anclaje, tiempo, amplitud y geometria no se ajustan.
- El respaldo/restauracion del FOV nativo permanece y se prueba.

El vuelo usa otra ruta y sus archivos/constantes no se han editado.

## 2. Sombra de campo: direccion propia, no luz del personaje

- EU `2F6630 GroundMinigameContent_Messages`: asigna y borra68hbytes de contexto
  (memset11DEF8, instrucciones2F6664..2F6670), incluido su puntero+8.
- `2F3FE0 Minigame_RenderShadows`: pasa ese puntero a
  `2636B0 Gfx_SetLightDirection`; si es nulo, la rama2636DC..263700 escribe
  **(0,-1,0,1)**. La opacidad para los campos sigue siendo0.3.
- El original usa esa direccion para construir la vista de la sombra en
  `2627A0 Shadow_RenderToTexture -> Gfx_GetLightDirectionPtr ->138B10`.
- En B3 la ruta equivalente es
  `82139578 ->821391A0 ->82139760 ->8208F588 ->8208EAC0`.
  El getter de direccion devuelve **8234D3D0**. Se ejecutaron offline los getters
  **US82139760 y EU82139868**: ambos devuelven exactamente ese dato. No se
  presupone un desplazamiento regional ni se modifica `eu_map_gen.h`.
- La base de proyeccion nativa8208EAC0selecciona como referencia el eje con
  componente absoluta minima; la direccion vertical es un caso soportado,
  no un cruce degenerado contra el ejeY.

Se amplia el gancho de sombra que ya existia en `iw_minijuegos.cpp`: conserva
la direccion nativa, aplica la vertical **solo dentro de ese consumidor de
campo**, conserva el alfa0.3 y restaura ambos al volver. No se modifican los
20floats de iluminacion del personaje. El filtro de escena, LRUS/EU y modulo
externo se conserva; el vuelo y los otros modulos quedan fuera de la rama.

## Pruebas ejecutadas (solo ejecutables independientes)

```powershell
python -B tools/campo_F_pruebas.py
```

Resultado: **codigo0**, antes y despues con deteccion de comportamiento
indefinido (`-fsanitize=undefined -fsanitize-trap=all`).

-81parametros de5..85grados: focal contra el calculo del original y su lienzo;
  error relativo tras el cambio<3e-7. La version anterior imprime el parametro
  sin convertir y conserva su desviacion medible.
- Consumidores **reales extraidos de la fuente**: FovIW, CamaraIw y gancho de
  sombra. Clips con ojo/objetivo sin desplazamiento; clip ausente no altera FOV;
  FOV nativo restaurado bit a bit.
-12contextos de sombra: US/EU, campo activo, inactivo, estado distinto, modulo
  externo, otro LR y bypass de combate. Se comprueba el vector que ve el
  consumidor, una unica llamada al original, restauracion exacta de direccion y
  opacidad y preservacion de otros efectos del consumidor.
- La direccion default se obtiene ejecutando las instrucciones de la rama del
  ELF, no copiando un valor elegido en la prueba. Los gettersPPCse ejecutan sobre
  las imagenes del jugador; las instrucciones y hashes quedan documentados.
- La medida anterior se reproduce sin cambiar su normalizacion ni umbral.

Los compilados de prueba emitieron avisos de deprecacion de `getenv` de la CRT
de Windows (tambien en el antes) y una constante no usada en la variante antes;
ningun error ni trap. No se han silenciado errores ni cambiado opciones del juego.

Artefactos: `D:/tmp/solIWF/campo/campo_F_verificado.json`,
`antes_prueba.log`, `despues_prueba.log`, `*_compilar.log` y los disassembly
`original_*.asm.txt`/`ppc_*.asm.txt`. Los respaldos `.antes` se conservan.

## Para la compilacion/corrida agrupadas del coordinador

Archivos de produccion modificados: **solo `src/iw_minijuegos.cpp` y
`src/iw_minijuegos.h`**. `suelo_iw.*` se investigo pero no requirio cambio.
Se han preservado las ediciones concurrentes del archivo compartido.

La siguiente verificacion visual debe seguir siendo la ruta existente
`tools/iw_sol9c.py visual -> tools/iw_sol9.py visual`, clave0x7D0, sobre la
compilacion agrupada. La medida nueva queda **pendiente/null** en elJSON:
sin compilar/ejecutar juego no se afirma que SSIM/phash hayan pasado.
Las pruebas de sombra simulan RAM/consumidor y no certifican solas la salidaGPU.
