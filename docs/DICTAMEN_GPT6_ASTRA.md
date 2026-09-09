# DICTAMEN GPT-6 ASTRA — Slots nativos + Port PS2→B3 HD

> **Fecha**: 2026-09-07.
> **Entrada**: `docs/BRIEFING_GPT6_ASTRA.md` (briefing completo del proyecto).
> **Autor**: GPT-6 Astra (asesor externo). Reproducido VERBATIM + anexo de
> análisis local (§0) con lo que es nuevo respecto a nuestro estado.
> **Valor**: estrategia y método de experimentación que no habíamos diseñado;
> corrige 3 supuestos y propone un plan de ejecución 0-7 priorizado por riesgo.

---

## 0. ANEXO LOCAL — Qué aporta y qué corrige (resumen de lectura)

Antes del dictamen literal, la lectura operativa para nosotros:

### 0.1 Tres correcciones a supuestos nuestros
1. **`0xFFFF` = celda vacía, NO personaje libre.** El mecanismo de slot vacío
   demostrado no implica que existan personajes disponibles. Distinguir
   **celda de interfaz / ID de retrato / ID de personaje / ID de forma**.
2. **`bone@+28` no es universal.** Está documentado para sec34 (Krillin); en
   formato C el bone va en `+40`. **Toda prueba debe seleccionar el layout por
   AWG**, nunca aplicar un offset global.
3. **El mid-insert amplía una entrada AFS existente; NO demuestra poder añadir
   índices nuevos.** Aumentar el conteo del AFS (3990 entradas) exige validar
   por separado: conteo, tabla virtual y sus consumidores.

### 0.2 Hallazgos nuevos (no habíamos llegado)
- **El byte en `r30+12` NO es necesariamente el nº global de personajes**: el
  límite puede ser de página/fila/modo. Hay que instrumentar `sub_82180AA0`
  (registrar `r30`, `r30+12`, llamador, modo, `r29` inicial/final) y rastrear
  **quién escribe `r30+12`** (watchpoint sobre memoria guest).
- **Prueba barata y segura**: reducir el valor observado en 1 (NO probar
  39→40): si desaparece exactamente una celda sin tocar otras páginas, se
  identifica el alcance del byte.
- **Orden de preferencia de vías**: (1) reutilizar celda reservada real si
  existe → (2) parche de datos en memoria guest → (3) híbrido (tablas ampliadas
  en memoria + hooks mínimos). **No recomienda re-codegen ni tocar
  `generated/` como primer paso.**
- **El parche de imagen vale para DATOS, no para código ya traducido**: el
  límite literal traducido a C++ no cambia al parchear instrucciones PPC; y no
  es seguro añadir bytes después de las tablas (puede haber datos detrás).
  La dirección de retratos se construye DIRECTA en `sub_8217F3F0` (sin
  indirección modificable) → trasladar tablas exige cambiar ese consumidor.
- **Módulo propuesto fuera de `generated/`**: `src/mods/native_roster`, con
  manifest por región+hash, verificación de bytes originales, opt-in, abort si
  discrepancia. Sin reemplazo de funciones en runtime → postproceso
  reproducible del código generado (match único).
- **Primer personaje = duplicar comportamiento, no archivos**: celda extra que
  **resuelve al personaje ORIGINAL** (p.ej. Android 16), selección/combate
  simultáneos original+duplicado, registro independiente que REUTILIZA sus
  recursos. NO duplicar CAM/ANM/voz/aura inicialmente. Modelo/retrato propios
  SOLO cuando la resolución independiente esté demostrada.
- **Mapear el registro de 184 B por comportamiento**, no por contenido:
  tabla `offset | ancho | lectores | escritores | valor | hipótesis | prueba`.
  **No asumir que `r3+64` pertenece al registro de 184 B** (puede ser otro
  objeto de UI). No clonar registros runtime con punteros propietarios: copiar
  configuración y pasar por el inicializador original.
- **Inventario de personaje jugable incompleto**: falta nombre localizado/
  anuncio, voz/audio, stats, formas/fusiones/trajes, efectos por técnica, IA/
  colisión, índices de guardado. El mapa de bins es base, no contrato completo.
  **Guardado**: perfil desechable + slot no persistente; NO escribir IDs nuevos
  en partidas normales.
- **El reverse test NO prueba que haya que reconstruir toda la estructura**:
  demuestra que la transformación no conservó invariantes. Antes de regenerar
  arms/zonas: descartar índices rel/abs, base de vértices, rangos A con otra
  base, streams paralelos no permutados, paletas de skinning de carga.
- **Matriz de permutaciones T0-T6** (round-trip sin cambios → swap 2 vértices
  mismo descriptor/hueso/zona → mismo descriptor entre huesos → permutar dentro
  de cada rango A → mover bloques contiguos → inversión global). Verificar
  geometría reconstruida por índices **equivalente al original ANTES de abrir el
  juego** (posiciones, UV, normales, huesos, pesos, winding, degenerados).
- **Orden de sospecha en el guest**: (1) resolución de rangos/bases/streams →
  (2) arms/tablas de skinning → (3) mesh-ref parte→zona/hueso → (4) matriz de
  zonas → (5) bboxes (solo si aparecen recortes, no desplazamientos).
- **Regenerar una pieza sin falsos negativos**: identificar el lector + su
  cálculo de dirección, clasificar campo (índice/offset/conteo/rango/puntero),
  aplicar la transformación correcta, probar en el caso mínimo que falla, y
  confirmar en otra permutación/modelo. Si ninguna pieza sola arregla → probar
  combinaciones justificadas por trazas (puede haber 2 dependencias).
- **Veredicto port**: **Vía A es la vía de entrega**, no un conversor
  universal. Invertir en: correspondencias por hueso/zona/material, preservación
  de costuras, umbrales por región (medidos contra 0.8), rechazo de
  correspondencias dudosas conservando el vértice HD. **Vía B = investigación
  acotada con entregables cerrados** (round-trip, permutación mínima fallida,
  primera divergencia runtime); si no produce mecanismo verificable → pausar.
- **Esqueletos**: "mismo nº de huesos" NO valida compatibilidad. Inventario
  automático de esqueletos HD/B3-GH/IW (labels, jerarquía, matrices de bind,
  convención, huesos de skinning/cara/accesorios). Clases A-D
  (identidad/reindexable/retargetable/incompatible). Hashes filtran, no deciden.
- **Retargeting de menor riesgo**: conservar esqueleto+animaciones HD del
  donante, adaptar la malla IW a ese rig. Accesorios rígidos→padre compatible;
  huesos auxiliares→redistribución con validación; falda/cola/cara sin
  equivalente→limitar/descartar explícito, NO colapsar en silencio. **No asumir
  que un único campo bone describe todas las influencias** (comprobar formato de
  skinning y arms del destino). Pruebas mínimas: bind pose, brazos elevados,
  codos/rodillas, torso, raíz, poses de ataque.
- **Texturas**: separar 4 etapas (decode PS2 swizzle/paleta → imagen
  intermedia verificable → compresión BC2 → DDS+`#AZT` conforme a referencia
  HD). **NO imponer `mipmaps=0` global** (ausencia de cadena ≠ campo cero).
  Mantener tamaño = simplificación inicial, no permanente (mid-insert cubre el
  crecimiento una vez validada la tabla virtual).
- **Validación offline**: OBJ solo valida geometría estática. Añadir
  **validador de skinning CPU en varias poses** + checks de offsets/alineación/
  stride/huesos/pesos/IB/rango B dentro del IB/preservación de flags/bboxes/
  round-trip de compresión + comparación geométrica por atributos.

### 0.3 Plan de ejecución (del dictamen, §5)

| Orden | Trabajo | Criterio de aceptación |
|---|---|---|
| 0 | Congelar baseline, DLL y override efectivo | Resultados repetibles y hashes registrados |
| 1 | Restaurar inyección conocida y ejecutar Afix aislado | Modelo visible estable; diagnóstico sin contaminación |
| 2 | Trazar conteo, celdas y registro de 184 B | Distinguir capacidad, identidad y enumeración |
| 3 | Añadir una celda alias de un HD existente | Original y duplicado seleccionables, sin sustitución |
| 4 | Crear identidad independiente y resolución de recursos | Ambos combaten simultáneamente sin compartir estado indebido |
| 5 | Escanear rigs y producir primer IW compatible | Silueta y animación aceptables sobre rig HD |
| 6 | Integrar ese modelo en el slot independiente | Select→combate→victoria→revancha estable |
| 7 | Completar voz, textos, formas y persistencia | Guardado probado con perfil desechable; regresión del roster |
| Paralelo acotado | Discriminador de Vía B | Primera dependencia demostrada antes del regenerador general |

Primera entrega: **un slot adicional real con comportamiento HD reutilizado y,
después, apariencia propia** (sin prometer moveset IW ni topología exacta).

---

# DICTAMEN (VERBATIM)

## Dictamen

**Recomiendo dos líneas separadas: un slot adicional que inicialmente reutilice
un luchador HD completo, y la inyección como vía de contenido visible. El port
exacto debe avanzar como investigación acotada, sin bloquear ambas.**

He leído los cuatro documentos. El plan siguiente distingue los hechos
documentados de lo que todavía exige instrumentar el ejecutable; no doy por
inspeccionados el código ni la imagen binaria, que no están adjuntos.

### Tres correcciones importantes antes de empezar

- **`0xFFFF` demuestra un mecanismo de celda vacía, no la existencia de un
  personaje libre.** Las 39 filas de retratos mostradas están ocupadas. Hay que
  distinguir celda de interfaz, ID de retrato, ID de personaje e ID de forma.
- **`bone@+28` no es universal.** Está documentado para sec34 de Krillin; en
  formato C figura en `+40`. Toda prueba debe seleccionar el layout por AWG,
  nunca aplicar un offset global.
- **Mid-insert permite ampliar una entrada AFS existente; no demuestra que
  permita añadir índices nuevos.** Aumentar las 3990 entradas requiere validar
  por separado el conteo, la tabla virtual y sus consumidores.

---

## 1. Slot nativo: estrategia y experimento inicial

### 1.1 Localizar el conteo sin introducir accesos fuera de rango

La comparación contra el byte de `r30+12` indica que **ese bucle obtiene el
límite desde memoria**. No demuestra que sea el número global de personajes:
podría ser el número de celdas de una página, fila o modo.

Instrumentaría `sub_82180AA0` para registrar:

- Dirección de `r30`, valor de `r30+12`, llamador y modo de juego.
- Valores inicial/final de `r29`.
- Base indexada con stride 184 e índices efectivamente utilizados.
- Correspondencia entre registro de personaje, celda creada e ID escrito en
  `r3+64`.

Después, rastrearía **quién escribe `r30+12`** mediante un watchpoint sobre
memoria guest o instrumentación del escritor en el C++ recompilado.

**Prueba barata y segura:** reducir temporalmente el valor observado en uno,
después de inicializarlo y antes de enumerar. Si desaparece exactamente una
celda, sin cambiar otras páginas, habremos identificado su alcance. **No
comenzar con 39→40:** puede leer más allá de las tablas o escribir fuera del
array de widgets.

Resultados posibles:

| Origen del byte | Intervención |
|---|---|
| Tabla estática copiada al objeto | Parchear la fuente antes de la copia |
| Valor calculado desde una lista | Extender la lista y comprobar capacidad |
| Inmediato en una función recompilada | Hook o modificación reproducible de esa función |
| Conteo por página/fila | Extender también navegación y distribución |

### 1.2 Decisión entre las tres vías

**Orden de preferencia:**

1. **Reutilizar una celda realmente reservada**, si existe y admite navegación
   y confirmación.
2. **Parche de datos en memoria guest**, si existen capacidad y referencias
   suficientes.
3. **Solución híbrida: tablas ampliadas en memoria guest + hooks mínimos del
   código recompilado.**

No recomiendo una modificación general del código generado ni re-codegen como
primer paso.

#### A. Reutilizar vacíos: oportunidad, no supuesto

Volcaría todas las celdas del select en varios estados de desbloqueo y modos.
Para cada `0xFFFF`, comprobaría:

- ¿Es relleno de la cuadrícula, personaje bloqueado o espacio reservado?
- ¿El cursor puede alcanzarlo?
- ¿La confirmación y la carga aceptan un ID válido?
- ¿Su activación desplaza o elimina otro personaje?

**Desbloquear un personaje existente no cuenta como añadir uno.** Una celda
extra que aliasa un personaje sí sirve como primer hito, pero todavía no
demuestra un ID de luchador nuevo.

#### B. Parche de imagen: válido para datos, no para código ya traducido

> **⚠️ ESTADO 2026-09-07**: la **vía 2 (parche de datos memoria guest)** fue
> probada sobre la celda "?" (slot 38 → Android 16, tag 28) y **FALLÓ con 3
> crashes idénticos** `0xC0000005` (`write of guest 0x82020664`). Causa raíz:
> la tabla slot→tag `0x82020618` está en una sección `XEX_SECTION_READONLY_DATA`
> de la imagen, y el guest **persiste el tag elegido escribiendo ahí al
> confirmar** el "?" — no es un write del hook. Desproteger la página desde un
> hook no basta (el crash es del codegen guest). Para que esta vía funcione
> haría falta hacer la sección writable en el **loader/SDK** (`xex_module.cpp`),
> no en el hook. **Detalle completo: HISTORICO §16.**

Es viable cambiar valores dentro de las tablas actuales. **No es seguro añadir
bytes después de ellas:** puede haber otros datos inmediatamente detrás.

Si hay que crecer:

- Reservar memoria **guest** con vida útil suficiente.
- Copiar y ampliar las tablas necesarias.
- Redirigir sus consumidores.
- Mantener offsets, punteros y enteros en el formato guest correspondiente.

La dirección de retratos se construye directamente en `sub_8217F3F0`. Si no
existe una indirección modificable, **el traslado exige cambiar ese consumidor**.

Además, parchear instrucciones PPC de la imagen no cambia automáticamente las
instrucciones C++ ya recompiladas. Un límite literal traducido requiere hook,
transformación del código generado o re-codegen.

#### C. Implementación que elegiría

Un módulo mantenido fuera de `generated/`, por ejemplo `src/mods/native_roster`,
con:

- Manifest por región y hash de ejecutable.
- Verificación de bytes/valores originales antes de aplicar cambios.
- Tablas ampliadas en memoria guest.
- Hooks pequeños para bases, límites o resolución de IDs que realmente lo
  necesiten.
- Activación opt-in; ante discrepancias, no aplicar el mod.

Si el runtime no ofrece reemplazo de funciones, usaría un **postproceso
reproducible del código generado**, con comprobaciones de coincidencia única.
Evitaría editar manualmente archivos que se sobrescriben.

### 1.3 Primer personaje: duplicar comportamiento, no todos los archivos

Elegiría un luchador HD sencillo y plenamente jugable, sin transformaciones
complejas —por ejemplo, Android 16— y haría:

1. Celda adicional que resuelve al personaje original.
2. Selección y combate simultáneos del original y su duplicado.
3. Registro de personaje independiente que reutiliza sus recursos.
4. Modelo y retrato propios, cuando la resolución independiente esté
   demostrada.

**No duplicaría CAM, ANM, voz y aura inicialmente:** compartir referencias
reduce variables.

Para recursos exclusivos hay dos opciones posteriores:

- Reutilizar entradas AFS cuya falta de uso esté demostrada.
- Implementar extensión real del directorio AFS virtual, incluyendo nuevos
  índices y conteo.

Un override global del modelo original cambiaría ambos personajes; **no
demuestra independencia del slot nuevo**.

### 1.4 Mapear el registro de 184 bytes por comportamiento

No hace falta entender los 184 bytes antes del primer alias. Sí hay que
identificar los campos consumidos por selección y combate.

Construiría una tabla:

`offset | ancho | lectores | escritores | valor por personaje/forma | hipótesis | prueba`

Prioridad:

1. Origen y vida útil de la base; inicialización y copias.
2. Campos `+14`, `+18`, `+114`, sin atribuirles semántica anticipadamente.
3. Identidad, formas/trajes, referencias de recursos y condiciones de selección.
4. Dependencias usadas durante carga, combate y salida.

Compararía registros de distintos personajes y distintas formas del mismo
personaje; luego cambiaría **un campo cada vez**, con valores válidos de un
donante.

No asumiría que `r3+64` pertenece al registro de 184 bytes: puede ser otro
objeto de interfaz. Tampoco clonaría a ciegas un registro runtime con punteros
propietarios; preferiría copiar su configuración y pasar por el inicializador
original.

### 1.5 Inventario que todavía falta

El mapa de bins es una excelente base, pero **no es un contrato completo de
personaje jugable**. Hay que capturar accesos durante selección, intro, combate,
técnicas y victoria para localizar:

- Nombre localizado y anuncio de selección.
- Bancos de voz y eventos de audio.
- Estadísticas, habilidades/equipamiento y reglas de desbloqueo.
- Formas, fusiones, transformaciones y trajes.
- Efectos y accesorios referenciados por técnicas.
- Tablas de IA y cualquier configuración de colisión específica.
- Índices persistidos, bitsets y límites del guardado.

Los retratos ya funcionan mediante `data_cmn.afs`; no tocaría `data_eng.afs`
para sustituirlos. Los textos localizados son otra investigación.

**Guardado:** primero perfil desechable y slot experimental no persistente. No
escribir un ID nuevo en partidas normales hasta conocer lectores, tamaños y
validación.

---

## 2. Port exacto: experimento discriminador

### 2.1 Qué demuestra realmente el reverse test

Demuestra que **la transformación realizada no conservó todos los invariantes
del runtime**. No identifica todavía el enlace oculto ni prueba que haya que
reconstruir toda la estructura.

Antes de regenerar arms o zonas, descartaría:

- Índices relativos frente a absolutos.
- Base de vértices y offset efectivo del buffer.
- Rangos A correctos numéricamente pero interpretados con otra base.
- Streams paralelos que no se permutaron.
- Paletas o buffers de skinning construidos durante la carga.

El draw log puede confirmar índices y conteos correctos mientras el draw
consume **otro buffer o una base incorrecta**.

### 2.2 Preparación: un único caso reproducible

Primero ejecutaría `cell_port_Afix_test` en solitario, como pide la hoja de
ruta actualizada. No reactivaría el antiguo experimento de Janemba.

Para el discriminador usaría un **HD nativo**, sin conversión PS2 ni cambio de
esqueleto:

- Preferentemente el mismo Cell y AWG donde ya se reproduce el fallo.
- Babidi como segundo caso simple y de formato diferente, **solo como conejillo
  técnico sobre Krillin**; no es un personaje jugable ni candidato de slot.

Congelar:

- Hash del bin original, bin modificado y DLL cargada.
- Un solo mod activo.
- Registro del override realmente servido.
- Pose, cámara y secuencia de animación.
- Reinicio completo para cada variante que cambie datos procesados al cargar.

### 2.3 Permutaciones que aíslan el problema

Definir explícitamente una permutación **índice antiguo→índice nuevo**. Mover
registros de vértice completos y remapear cada valor del IB, conservando su
secuencia.

| Prueba | Modificación | Qué permite aislar |
|---|---|---|
| T0 | Original | Referencia |
| T1 | Round-trip sin cambios semánticos | Errores del serializador |
| T2 | Intercambiar dos vértices del mismo descriptor, hueso y zona | Dependencia estricta de índice |
| T3 | Intercambiar dentro del mismo descriptor, entre huesos | Skinning o agrupación por hueso |
| T4 | Permutar dentro de cada rango A | Dependencia interna sin mover límites |
| T5 | Mover bloques completos, manteniendo cada rango contiguo | Bases y rangos entre partes |
| T6 | Inversión global | Reproducir el fallo amplio conocido |

En T2–T4 elegiría vértices con **la misma pertenencia a descriptores**, si hay
rangos solapados.

Antes de abrir el juego, verificar que la geometría reconstruida por índices es
equivalente al original: posiciones, UV, normales, huesos, pesos, winding y
degenerados. No basta con que el OBJ "se parezca".

**Ventaja:** una permutación equivalente no cambia las bboxes geométricas. Si
recalcularlas altera el resultado, habría que investigar su interpretación o
sus referencias, no atribuirlo simplemente a nuevos bounds.

### 2.4 Localizar la primera divergencia en el guest

Capturaría tres puntos:

1. Pool y estructuras recién descomprimidos.
2. Buffers/paletas producidos durante inicialización y skinning.
3. Buffer, offset, stride, base de vértices, IB y constantes enviados al draw.

Compararía los datos teniendo en cuenta la permutación inversa. La primera
divergencia indica dónde instrumentar lecturas adicionales.

**Orden de sospecha:**

1. Resolución efectiva de rangos, bases y streams.
2. Arms o tablas auxiliares usadas para construir skinning/paletas.
3. Mesh-ref y asociación parte→zona/hueso.
4. Matriz de zonas.
5. Bboxes, principalmente si aparecen desapariciones o recortes, no
   desplazamientos anatómicos.

Es una priorización, no una conclusión sobre su semántica.

### 2.5 Regenerar una pieza: cómo evitar falsos negativos

No haría "reverse + modificar campos sospechosos" sin conocer qué representan.
Para cada candidato:

1. Identificar el lector y su cálculo de dirección.
2. Clasificar el campo: índice, offset, conteo, rango o puntero.
3. Aplicar la transformación correspondiente.
4. Probarlo sobre el **caso mínimo que falla**, no primero sobre la inversión
   global.
5. Confirmar en otra permutación y otro modelo.

Si ninguna corrección aislada funciona, probar combinaciones justificadas por
las trazas: puede haber dos dependencias simultáneas. **Que ninguna pieza por
separado arregle el modelo no prueba que ninguna participe.**

### 2.6 Veredicto: inyección ahora, exactitud como siguiente capacidad

**Vía A es la vía de entrega actual, pero no un conversor universal.**
Conserva topología HD; no puede reproducir de forma exacta siluetas,
accesorios y superficies ausentes en la plantilla.

Invertiría primero en:

- Correspondencias restringidas por hueso, zona y material.
- Preservación de costuras y separación entre superficies cercanas.
- Umbrales duros por región, medidos contra el resultado conocido de 0.8.
- Rechazo de correspondencias dudosas y conservación del vértice HD original.

No volvería a blends suaves por defecto: ya empeoraron los casos probados.

**Vía B sigue siendo necesaria para personajes arbitrarios con fidelidad alta.**
Le asignaría una primera investigación con entregables cerrados: round-trip,
permutación mínima fallida y primera divergencia runtime. Si no produce un
mecanismo verificable, se pausa; no se reescribe `draw` entero a ciegas.

---

## 3. Esqueletos 1:1 y personajes de Infinite World

### 3.1 "Mismo número de huesos" no valida compatibilidad

Generaría un inventario automático de todos los esqueletos HD, B3 GH e IW:

- Labels originales e índices.
- Padre, hijos, raíces y jerarquía.
- Matrices locales y globales de bind.
- Convención de coordenadas, escala y orientación.
- Huesos usados por skinning, cara y accesorios.
- Correspondencia de canales de animación, cuando pueda extraerse.

Clasificación:

| Clase | Condición | Acción |
|---|---|---|
| A: identidad | Mismos huesos, orden, jerarquía y bind compatible | Reutilización directa |
| B: reindexable | Mismo rig, orden diferente | Remapeo de todas las referencias |
| C: retargetable | Jerarquía/proporciones diferentes, equivalencias claras | Adaptación al rig HD |
| D: incompatible | Huesos esenciales o deformaciones sin equivalente | Posponer o ampliar capacidades |

Usaría tolerancias documentadas para comparar matrices, con comprobación
visual de ejes y articulaciones. Los hashes sirven para filtrar, no para
decidir equivalencia aproximada.

Normalizar prefijos de nombres solo ayuda a encontrar candidatos: **no
demuestra que dos huesos tengan la misma función**.

### 3.2 Retargeting de menor riesgo

Para el primer IW no-1:1, conservaría **esqueleto y animaciones HD del
donante**. Adaptaría la malla IW a ese rig; no intentaría además portar su
moveset.

Proceso:

1. Elegir donante por jerarquía, proporciones y articulaciones, no solo
   apariencia.
2. Alinear escala, orientación y pose de reposo.
3. Definir correspondencias semánticas explícitas.
4. Adaptar la malla a la bind pose destino.
5. Transferir pesos y convertir posiciones a espacio local destino.
6. Transformar correctamente normales y verificar deformaciones.

Huesos extra:

- Accesorios rígidos: asignación al padre compatible, aceptando la pérdida de
  movimiento.
- Huesos auxiliares: redistribución de influencias con validación.
- Falda, cola o cara sin equivalente: limitación explícita o candidato
  descartado; no colapsarlos silenciosamente.

Tampoco asumiría que un único campo `bone` describe todas las influencias
admitidas: hay que comprobar el formato de skinning y los arms del destino.

**Pruebas mínimas:** bind pose, brazos elevados, flexión de codos/rodillas,
giro de torso, desplazamiento de raíz y poses de ataques. Un modelo correcto en
reposo puede estar completamente mal animado.

Janemba queda después de superar estas pruebas con un caso sencillo.
Pikkon/Pan son candidatos a escanear, no compromisos previos.

---

## 4. Texturas y validación offline

### Texturas

El conversor debe separar:

1. Decodificación PS2: swizzle, paleta, formato y alpha.
2. Imagen intermedia verificable.
3. Compresión BC2/DXT3.
4. DDS y envoltorio `#AZT` conforme a una referencia HD.

Para el primer caso mantendría dimensiones y política de mipmaps de la
plantilla. **No impondría `mipmaps=0` a todos los DDS:** comprobaría flags,
conteo y payload; ausencia de cadena adicional y campo numérico cero no son
equivalentes en cualquier lector.

El bitmap BC2 ocupa bloques de 4×4 y 16 bytes por bloque, incluyendo
dimensiones redondeadas a bloques. Validaría cada nivel, alpha y
correspondencia material→textura.

Mantener tamaño es una simplificación inicial, no una restricción permanente:
el crecimiento puede usar mid-insert una vez comprobadas la tabla virtual y la
lectura completa.

### Checks adicionales

Incorporaría al verificador:

- Offsets y punteros dentro de su sección, con base relativa correcta.
- Alineación y ausencia de solapamientos no permitidos.
- Stride, tamaño y cantidad de vértices coherentes por formato.
- IDs de hueso válidos y jerarquía sin ciclos.
- Pesos e inversas de bind válidos.
- Valores finitos y normales plausibles.
- IB válido, interpretación correcta de strips y degenerados.
- Rango B dentro del IB; índices efectivos compatibles con A y su base.
- Preservación de flags en campos A/B empaquetados.
- Bboxes en el espacio correcto.
- Referencias válidas a materiales y texturas.
- Round-trip de compresión/descompresión y tamaños de lectura.
- Comparación geométrica por atributos, no solo conteo de triángulos.

Añadiría un **validador de skinning CPU para varias poses**. OBJ solo valida
geometría estática y puede ocultar precisamente el fallo que interesa.

---

## 5. Prioridad y orden de ejecución

| Orden | Trabajo | Criterio de aceptación |
|---|---|---|
| 0 | Congelar baseline, DLL y override efectivo | Resultados repetibles y hashes registrados |
| 1 | Restaurar inyección conocida y ejecutar Afix aislado | Modelo visible estable; diagnóstico sin contaminación |
| 2 | Trazar conteo, celdas y registro de 184 B | Distinguir capacidad, identidad y enumeración |
| 3 | Añadir una celda alias de un HD existente | Original y duplicado seleccionables, sin sustitución |
| 4 | Crear identidad independiente y resolución de recursos | Ambos combaten simultáneamente sin compartir estado indebido |
| 5 | Escanear rigs y producir primer IW compatible | Silueta y animación aceptables sobre rig HD |
| 6 | Integrar ese modelo en el slot independiente | Select→combate→victoria→revancha estable |
| 7 | Completar voz, textos, formas y persistencia | Guardado probado con perfil desechable; regresión del roster |
| Paralelo acotado | Discriminador de Vía B | Primera dependencia demostrada antes del regenerador general |

Con una sola persona, haría primero los hitos 0–3 y después elegiría el IW por
resultados del escáner. Con dos, separaría roster/runtime de
geometría/rigging; **no cambiaría ambos sistemas en un mismo experimento**.

La primera entrega debería ser **un slot adicional real con comportamiento HD
reutilizado y, después, apariencia propia**, sin prometer aún moveset IW ni
topología exacta. Así se obtiene valor visible sin convertir la RE más incierta
en requisito de todo el proyecto.

---

## REFERENCIAS CRUZADAS

| Tema | Dónde |
|---|---|
| Briefing que originó el dictamen | `docs/BRIEFING_GPT6_ASTRA.md` |
| Tablas del roster en la imagen guest | `docs/03_formatos/MAPA_ROSTER_HD.md` §7 |
| Estructura de dibujo HD | `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md` |
| Pipeline de port (inyección/port) | `docs/07_ports/` + `mod center hd/ports/` |
| Plan de ejecución integrado | `docs/HOJA_DE_RUTA_2026_09.md` |
| Contexto operativo | `AGENTS.md` |
