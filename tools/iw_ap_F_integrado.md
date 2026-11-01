# AP solIWF — integracion en consumidores reales

Produccion modificada: `anim_iw.cpp/.h`, `iw_minijuegos.cpp/.h`. Sin cambios en
iw_ame, iwiop, aura, sus semillas, otras ramas de efectos, main, CMake ni net.
No se ha compilado ni arrancado el juego.

## Integrado

### AP9: ya llega a movimiento

`AvanzarEventos` usa `Evaluar`, actualiza `EstadoExterno` antes del despacho y
conserva el factor por actor/modelo principal. `VelocidadAP` publica la velocidad
de299EE0con mutex para el hilo del juego y la interfaz.

El campo usa `VelocidadCampoAp` en **andar, scatto y las dos ramas NPC**. El
factorB54sustituye0.4; `k/0.4` solo convierte las unidades del curso anfitrion.
No altera gravedad, salto, vibracion, dimensiones, texturas ni otros efectos.

Se reinicia al cambiar realmente la animacion (`Arrancar`/destino de mezcla),
no al rearmar B3el mismo clip. Al envolver se aplica el reinicio de514de
`AnimationFrames`: solo los9del principio del ciclo pueden quedar vigentes.
Al quitar el actor se libera su contexto. `DBZ3_IW_EVENTOS_OFF` tambien limpia
el factor y los impactos pendientes, sin reproducir despues los eventos omitidos.

### AP0: escena IW, no estado B3

`GroundMinigameContent_Messages`2F6630llama20E3D0. Este borra **8D30hbytes** del
trabajo, incluido8B8C: al entrar al MSNMW queda **0**. Se ha ejecutado esa rutina
original en el arnes partiendo de8B8C=7y se comprueba que queda0.

`EscenaApCampo()` devuelve ese0solo en el campo activo; fuera devuelve-1.
`DispararEvento` suprime AP0en ese contexto, como207180, que solo llama205660
en2/3/4/7. Fuera del campo no inventa una escena ni declara consumidos los
efectos de combate que siguen pendientes.

### AP5: generador real para los subtipos0/1

Se identifico el paquete, no una equivalencia numerica:

-1FC290abre **DATA_CMN813** (`addiu a0,32D`). Atlas0.
-1FB870(subtipo0) selecciona el recurso1y1FB720(subtipo1) el6, pasando ambos
 a1FA330. Los dos son **#AME reales**, con emisores/hijos, no un sprite simulado.
- El campo carga813/1y813/6con el lector existente y el atlas813/0con el cargador
 de texturas existente. Crea instancias, avanza **Cuadro** y dibuja **Pintar**.
- Las anclas1..21 se buscan por los nombres originales458ED4(EU)/4586D4(USA)
 en el AWOdel cuerpo anfitrion y se llevan a mundo con sus matrices.0usa la raiz
 de la instancia. Un nombre ambiguo o un rig/esquema no reconocido devuelve
 pendiente: no se elige por igualdad de indice con B3.
- Se conserva la posicion al crear el efecto. La orientacion corresponde a
13C690: referencia44BB90=(0,1,0,1), girada hacia objetivo-ojo de la camara.
 No se presupone referenciaZ. La simulacion del efecto se hace local y la
 transformacion fija se aplica a sus particulas antes del pintor real.

Los5se difieren desde la lectura del mando hasta `Fotograma`, llamado por el
gancho post-logica del campo. Asi se muestrean las matrices actualizadas del
cuerpo. Se drenan una sola vez y se anulan al reiniciar el clip/quitar el actor.
No se añade otro gancho820F9548, que ya esta ocupado por gigantes.cpp.

**Subtipo2no portado**:1FB5B0usa las anclas del cuadro ANTERIOR(+7A0), no las
actuales; no se sustituye. Otros subtipos tambien siguen pendientes. El cuerpo
PS2especial de Cell no entra en este consumidor. No se toca aura ni su rama RNG.

### RNG: llamadas a la API del coordinador

- `Leer` de animaciones y `Cargar` de campo llaman
  `iwazar::Global().Iniciar(P::LeerElf(iw))`. El contrato es idempotente: el
  contexto inicializa una vez; entrar en otro campo no reinicia la semilla.
- Voces: callback `return iwazar::Global().Memory(32)`; eliminada semilla=1privada.
- `PasosAp`: eliminado `semilla_pasos_ap_`; todo su bucle `ame::Cuadro` pasa por
  `Global().ConEstado([&](uint32_t& sem){...})`.
- Los nuevos impactosAP5usan tambien ConEstado. Se conservan literalmente las
  funciones AuraDeCuerpo/Polvo/Efectos y sus semillas anteriores.

**Dependencia aun ausente al entregar:** `src/iw_azar.h`, que esta creando el
coordinador. El codigo productivo lo incluye y llama su API real; **no se ha
instalado un reemplazo en src**. El ensayo de cableado usa un doble del contrato
solo en `D:/tmp/solIWF/ap/integrado/azar_contrato.h`. ElJSONmarca
`rng_header_real=false`: no es una prueba de la implementacion final thread-safe
del coordinador. Al existir el header, el mismo arnes lo selecciona en lugar del
doble y permite contrastarlo. Hace falta ese archivo para compilar agrupado.

### Audio

`DispararEvento` diferencia `secuencia_ausente` de volumen/velocidad cero.
No se afirma silencio intencional por un banco que no contiene el identificador.
Los gritos tipo3solicitan sustitucion en un slot del actor; impactos1/2no se
hacen exclusivos. El mezclador aplica el mute, manteniendo los relojes.
Los handles>=1000siguen pendientes: no se crean sin su liberacion original.

## Pendientes explicitos

- AP4: no se ejecutan sus pedidos como si existiese el generador de tecnicas/
  capsulas IW. Continuan diagnosticados pendientes; no se marca generacion.
- AP8: se conserva el pendiente en el consumidor. AMKes un selector, y falta
  cargar la paleta AMMde boca y enlazar sus nueve huesos al cuerpo local real.
  `CopiarBoca` existente usa layoutsIW, no un actorB3. No se escriben660/664ni
  otros offsetsPS2en la estructuraB3. Sin esos recursos/enlace verificados no se
  aplica una pose de boca fingida ni se mueve la jerarquia entera.
- Verificacion RNGfinal con iw_azar.hreal, imagen/GPU, cadencia final de efectos
  e identidad de los rigs vivos en la corrida agrupada. No se declara igualdad
  del orden completo del RNGguest por haber conectado la instancia compartida.

## Pruebas ejecutadas

```
python -B tools/iw_ap_F_integrado_pruebas.py
```

Codigo0. Ejecutables de prueba independientes, UBSan/trap, funciones **reales
extraidas** de los cuatro archivos y motor/pintorAMEexistentes:

- AP9alcanza andar/scatto/NPC; reinicio por clip/vuelta, mismo clip conserva
  estado y salida del actor restaura defecto0.4.
- AP0campo0suprimido; trabajo original20E3D0borra8B8C.
- Voz→AP6→voz continúan la misma semilla del contrato. AP6se compara con una
  instancia de referencia del MISMO AME905/3. No se reescribe Cuadro.
- Diagnostico `secuencia_ausente`, sin confundirlo con ganancia cero.
- Ancla por nombre y muestreo diferido: una matriz cambiada entre mando y
  post-logica modifica el punto generado; no se duplica al volver a drenar.
-813/1y813/6producen primitivas mediante **Pintar real**, con el numero de
  texturas del atlas. Receptor final: **964vertices/1446indices** en el ensayo
  combinado. No se usa un contador de llamadas como sustituto de esas primitivas.
- Fabricaoriginal1FB870/1FB720ejecutada hasta1FA330: confirma recursos1/6;
  los21nombres de roles coinciden en los ELFUS/EU y la referencia esY.
- El2no se encola y deja pendiente. El codigo de aura/polvo/otras ramas no cambia.

Limites del ensayo: audio es receptor sin dispositivo; el AWOvivo se representa
con un esquema de prueba y los nombres realesELF; las texturas son enlaces de
prueba, no una capturaGPU. No ha habido juego, build del proyecto ni `todo`.

Evidencia y comandos completos:
`D:/tmp/solIWF/ap/integrado/verificado_integrado.json`, `consumidores.log`,
`compilar.log`, `inventario.json` y `bundle813.json`. Backups de los cuatro
archivos en esa carpeta. Ningun dato del jugador se ha borrado.
