# AP0/4/5/8/9 solIWF — codigo y contraste offline, integracion pendiente

> Continuacion en `iw_ap_F_integrado.md`: ya se conectaron AP9, filtro AP0,
> generadores AP5 0/1 y las llamadas al RNG compartido en los consumidores.
> Aqui queda el contrato/research previo; los pendientes actuales estan alli.

Esto trata los **tipos0..9 de la pista AP7** del #BSK, no los gestoresAP0..6
de las hitboxes que ya convierte `tests/iw_ap_prueba.h`. No se ha editado
`anim_iw.cpp`, ni ejecutado/compilado el juego.

## Cambios de produccion

- `iw_eventos.h`: separa `Evaluar`(encolar) de `Despachar`(consumir). Mantiene
  la firma de `Avanzar` y las nueve comprobaciones AP anteriores. Orden de los
  consumidores: **0,1,2,3,7,4,6,8**;5se conserva para lector externo;9notifica
  solo su ultima escritura. La cola6 conserva hasta7registros, pero el despacho
  de campo entrega solo el primero. Se mantienen limites1para3/7/8 y7paraelresto.
- `iw_ap_consumidores.h`: estado AP0, preparacion contextual AP4, lectorAP5 con
  geometria decimal y estado/velocidad AP9. No escribe memoria/IDs equivalentesB3.
- `iw_ap_boca.h`: lector AMK, reloj/selector, snapshot de nueve registros128B,
  nlerp de quaternion por hemisferio, normalizacion, mezcla de posicion/escala,
  matriz local y aplicacion de los nueve registros al cuerpo.
- `iw_ap_eventos.h`: `BancoBoca` lee la columna ya validada del ELF; se corrige
  la informacion de lectores. La tabla no declara resueltos los generadores que
  aun faltan.

## Lectores originales encontrados por alias (NO estan ausentes)

`Animations2(207180)` evalua un contexto en **modelo+2D0**. Para el modelo
principal, **modelo=personaje+640**:

```
cola5:  contexto+20C/210 = modelo+4DC/4E0 = personaje+B1C/B20
valor9: contexto+244     = modelo+514     = personaje+B54
cola6:  contexto+22C/22E = modelo+4FC/4FE = personaje+B3C/B3E
```

- **AP5:** `22F090 Battle_UpdateGroundHeightAndFilter`, bloque22F180..22F1B4,
  recorre TODOS los u32 de B20 y llama **1FBE00**. No vacia esa cola; se renueva
  en la siguiente evaluacion. Queda demostrado ejecutando el lector dos veces.
- **AP9:** `299EE0 Minigame_MovePlayerByStick`,299F60..299F88leeB54. Si es
  cero usa0.4; si no, el valor almacenado. La velocidad es
  `velocidad * (escala*cac) * factor`. Otros lectores reales:211930(y variantes)
  usan defecto0.8;316DA0/316EB0(y variantes) usan0.4. `FactorMovimiento(defecto)`
  conserva esa distincion, sin imponer0.4a todos los modos.
- La escritura es `float(int32(valor))/10`, NO reinterpretacion como float.
  Persiste cuando no hay9en el cuadro. `AnimationsStatic(207CE0)` y el cambio
  de ciclo que reinicia el contexto ponen modelo+514a0.
- No aplicar al modelo secundario el alias del principal: su contexto esta en
  otro sitio y no es el B54 que lee el movimiento del actor.

Fuente: `E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c`; coincidencias acotadas
de offsets/funciones en `D:/tmp/solIWF/ap/aliases_decomp.json`. No se necesita
afirmar ausencia de lector: los consumidores positivos quedan ejecutados.

## AP0: que se ha portado y cuando se suprime

`207550..2075AC` solo llama `205660 PlayerVsCpuGeneralScene3` en escenas IW
**2/3/4/7**. `Consumir0` porta los estados puros29/2B(contadores s16 saturados),
3C/3D(flag206E),50/51(ocultar/mostrarCCD) y47(cuadro objetivo18E4→C68,velocidad0.1).
El resto de efectos conocidos dentro de esas escenas sigue `Pendiente`; no se
ha reutilizado un numero de efectoB3 ni activado aura/rayos ajenos.

Fuera de esas escenas devuelve `SinEfectoOriginal` sin modificar estado.
**Escena desconocida(-1) devuelve `Pendiente`**, no un supuesto silencio. El
coordinador debe aportar la escena IW real; el hecho de que sea un campo no
autoriza a pasar el estado numerico de B3 como si fuese el de IW.

## AP4: ABI y contexto que evita un port erroneo

`207674..20768C` construye **{actor,0}** y lo pasa como argumento64bits a
`1BF610 Super_Ult_Ki_AttackEffects`. Su rama depende de la palabra alta del
paquete: los eventosAP4de valor5/6/7 NO son sus ramas5/6/7.

Para AP4 se llama `1BEC60 Battle_DispatchEffectEvent`:

-200..299y400..499seleccionan el rival; el resto, el propio.
- Si registro24=-1, guarda el codigoB60. En el primer300..303tambien guarda48.
- ID<4o>349→proyectilIW;4..299→tabla de efectosIW;300..349→comandoIW.
- Proyectil/tabla reciben directamente el actor seleccionado. El comando recibe
  **el paquete original**, no ese puntero de actor: `Pedido4.paquete_actor` lo
  distingue.

`Preparar4` conserva esas mutaciones/pedidos. **No dibuja por si solo las
tecnicas.** Queda integrar su generador IW y la rama adicional de capsulas/
KiBarsEffect que Animations2ejecuta en estados13/32con1AD0>0y1AD2=0. No se
declaran renderizados los veintepedidos del ensayo por resolver su ruta.

## AP5: geometria real antes del generador

`Preparar5` reproduce1FBE00:

- componente decimal de millones==1invierte direccion; no es un bit.
- decenas de millar seleccionan ancla del rival(valor-1), o-1si cero.
- centenas seleccionan ancla propia(valor-1), o raiz0si cero.
- ultimas dos cifras son subtipo0..19.
- Sin ancla rival: posicion en ancla propia, direccion **raiz rival-raiz propia**.
  Con ancla rival: punto medio de ambas anclas y direccion entre ellas. Inversion
  y normalizacion despues, no antes del punto medio.

`FuncionImpacto5` lee el puntero de la tabla original del ELF. **El subtipo NO es
un indice AME/B3.** La preparacion/lector estan portados; las veintefunciones
finales(renderer/recursos IW y casos especiales) no se dan por implementadas.
La direccion devuelta identifica la funcion EE original para elegir su port;
no es una funcion ejecutable del host ni se puede pasar a una llamada PPC.
Si falta un ancla no se emite una geometria inventada.

## AP8: AMK, matrices y recursos que debe aportar el cargador

Los bancos de boca del actor son #AMK directos en ADX_JPN/USA;Goku1471y el
actor deCMN924→1475se contrastan en ambos idiomas. `BancoBoca(elf,cmnActor)`
lee la identidad del cuerpo, no del clip comun204/915.

AMK no contiene rotaciones/matrices: directorio+18, numero+14, registros16B
{duracion,nClaves,flags,offset}; claves4B{s16tiempo,u8visema,u8variante}.
`1BA850` usa tiemposRAWpara claves internas y **ultimoTiempo-1** en su atajo
final. `Estado` conserva esa diferencia. Fin206E20al llegar a duracion-1;
pausa8B7Cse ignora solo si668esta puesto; congelado68Cno avanza.

El destino de pose es `variante*6+visema`, acotando variante por cantidadAMM/6.
Este numero es una fila del **AMM del modelo de boca IW**, NO de la animacionB3.
`Motor` requiere una paleta de poses reales: una `NueveMatrices` por fila de ese
AMM. Cada registro conserva128B. Una paleta ausente produce-1, no una boca falsa.

`Motor.Iniciar` valida el AP8firmado, respeta68C/corte8BD8, reinicia el reloj y
captura cuerpo→hablante. `Aplicar` evalua los selectores y el cache original:
mezcla0.6si la variante anterior/actual coincide,0.75si difiere. Invalida el
cursor cuando avanza el cuadro, como206EE0. Reconstruye la matriz, conserva los
otros campos y copia los nueve registros. `PorNombres` ayuda a enlazar los
huesos; negativos se omiten, indices fuera de rango fallan antes de escribir.

Infraestructura existente inspeccionada: `anim_iw.h:efecto::CopiarBoca` y su
implementacion25FA90en `anim_iw.cpp`. Usarla es valido **con estructuras del
layoutIW**:1c/4c→modelo,45c→matrices e indices0/30. No pasarle un actorB3 ni
trasladar esos offsets por igualdad numerica. Para un rig host usar las spans
de `Motor`/`Copiar9`, con enlace de huesos real. Falta que el cargador entregue
la paleta AMM y ese enlace; no se afirma salida GPU sin dicha integracion.

## Esquema de integracion para anim_iw (no se ha editado)

```cpp
// Estado por modelo. Reiniciar externo SOLO cuando se reinicie AnimationsStatic.
auto lote = eventos::Evaluar(a->eventos, anterior, actual, duracion);
externo.Actualizar(lote); // latchea5/9 ANTES de consumidores, como el parser IW
eventos::Despachar(lote, [&](const eventos::Registro& ev) {
  if (ev.tipo == 5 || ev.tipo == 9) return; // ya latcheados, no son audios
  if (ev.tipo == 0) { /* Consumir0(escenaIW, ev.valor, estadoActor0) */ }
  else if (ev.tipo == 4) { /* Preparar4 + generador IW/contexto de capsulas */ }
  else if (ev.tipo == 8) {
    motorBoca.Iniciar(bancoAMK, ev.valor, matricesCuerpo, indicesBoca,
                     congeladoIW, corte8BD8);
  } else DispararEvento(/* argumentos actuales */);
});
// Fase externa equivalente a22F090, con anclas IW reales y generador disponible:
externo.Leer5([&](uint32_t valor) {
  apev::Impacto5 p;
  if (apev::Preparar5(valor, anclasPropias, anclasRival, ladoIW, p)) {
    /* despachar FuncionImpacto5(elf,p.tipo); si no existe el motor, pendiente */
  }
});
// En movimiento, sustituir el factor fijo, no multiplicarlo una segunda vez:
float v = externo.VelocidadCampo(velocidadIW, pasoIW, cacIW);
```

`Avanzar` sigue siendo compatible, pero el esquema por `Lote` permite disponer
del9antes de los demas consumidores. La notificacion9ultima no es la ejecucion
de un noveno efecto visual;5se procesa en su fase externa. El filtro anterior
de primera cola6del llamador queda redundante pero no rompe esta API.

Orden de boca: aplicar la boca ya activa en la fase de pose; despues consumir
AP8(validar y capturar, **sin aplicar inmediatamente la nueva pose**); avanzar
su reloj en la fase de `Char_AnimTimerUpdateReset`. El nuevo AMK se vera en la
siguiente fase de pose. Crear estado de boca nuevo al cambiar banco/idioma/rig.
La copia debe preceder al paso normal de jerarquia del cuerpo: el original
llama `LLEGROT_RARMROT_LRARMROT_Poses_` despues; no presentar las matrices locales
reconstruidas como si ya fueran todas las matrices mundo finales del renderer.
Estado0y registro4pertenecen al actor/efectos y no se reinician en cadaevento.

## Verificacion ejecutada

```powershell
python -B tools/iw_ap_F_pruebas.py
python -B tests/iw_ap_F_cola6.py
```

Resultado codigo0, con `-fsanitize=undefined -fsanitize-trap=all`:

- Las9pruebas AP existentes se compilan/ejecutan sin editar sus aserciones.
- **302clips/735eventos** de204y912..932: colas contra23F520retail.
- Orden contra el tramo real207538..2079B0en ocho escenas;9ultima y6primera.
  El lector completo2A8940se prueba tambien con cola[7,5]: lee B3Euna vez y
  nunca B40, por lo que el segundo5no se consume(`cola6_original.json`).
- **63casos AP0**, **20AP4**, **10desempaquetados AP5**, **7AP9**, y lector5
  ejecutado dos veces(**14pedidos**, sin vaciado).
- **21178muestras AMK** de4bancos/idiomas contra1BA850/1BA5D0originales.
  **8casos de reloj** contra206E20: pausa, override, congelado y fin.
- Copia25FA90real en ambos sentidos, indices negativos y nueve positivos:
  **23registros128B** coincidentes byte a byte.
- Mezcla quaternion13CEA0y matriz13C7A0originales, normalizacion independiente;
  diferencias float<2e-6en ese ensayo. Estado/cache y factores0.75/0.75/0.6
  del motor de boca comparados con206EE0/1BA720, nueve matrices por paso.

Las instrucciones se ejecutan en `tests/iw_ap_F_original.py`, que falla ante
opcodes no soportados. En el ensayo de orden se observan llamadas sin ejecutar
los generadores; en el del motor de boca se sustituyen el evaluador AMM y su
salida por los pasos de matriz contrastados aparte. Eso NO es una prueba de
renderer ni de las tecnicas/capsulas completas.

Evidencia: `D:/tmp/solIWF/ap/verificado.json`, logs, matrices y corpus binarios.
Backups de los dos headers previos en el mismo directorio. Sin juego, build de
juego, `todo`, publicaciones ni cambios en anim_iw. Los generadores AP4/AP5 y
la carga/enlace de la paleta de boca siguen siendo pendientes concretos.
