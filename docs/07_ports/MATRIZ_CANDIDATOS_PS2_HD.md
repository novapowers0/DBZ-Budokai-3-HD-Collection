# Matriz de candidatos PS2 → B3 HD

> Preparación de la siguiente fase: 2026-09-08. No se ha generado ningún bin
> ni mod nuevo. La selección se basa en el rig, no en el parecido visual.
> **Babidi es el conejillo de indias técnico**: no es jugable ni candidato de
> slot; se instala temporalmente sobre Krillin para validar el conversor.

> **Bloqueo de fuente detectado 2026-09-08**: los `data_cmn.afs` disponibles
> bajo `ps2_games/Budokai 3 Greatest Hits` y `ps2_games/Budokai 2` devuelven
> entradas `#AMB/#AWO` big-endian, no `#AMO0/#AMG` PS2 LE. No usar esas entradas
> como fuente PS2 hasta localizar/examinar el AFS correcto.

## Objetivo

Resolver primero un caso **1:1 estructural** para validar el conversor. Babidi
es un control técnico sobre Krillin, no un personaje nuevo seleccionable. El
primer modelo jugable será una fase posterior y separada.

## Orden de candidatos

| Prioridad | Candidato | Origen | Plantilla HD | Motivo | Estado |
|---:|---|---|---|---|---|
| 1 | Babidi | B3 PS2 Greatest Hits | Babidi HD, entry 96 → prueba sobre Krillin | 1 AWG/41 huesos; control no jugable | Pendiente verificar rig |
| 2 | Bulma | B3 PS2 Greatest Hits | Bulma HD → prueba sobre Krillin | 2 AWGs; control técnico | Pendiente verificar rig |
| 3 | Tien con capa | IW → B3 PS2 (`Tien (With Cape).amo`) | Tenshinhan HD, entry 400 | 42 labels comunes en mismo orden; 10 bones extra de capa | **Pasa rig 1:1 base** |
| 4 | Pan | Infinite World | anfitrión HD compatible | Candidato de contenido nuevo | No comprometer sin scan |
| 5 | Super 17 | Infinite World | anfitrión HD compatible | Moveset existente | No comprometer sin scan |
| X | Pikkon | Infinite World | Krillin/KLL | 58 huesos y `SKIRT`, rig PKH distinto | **Descartado** |
| X | Janemba | Infinite World | Krillin/KLL | Retargeting y estructura incompatibles | **Archivado/descartado** |

## Criterio de aceptación del rig

Antes de tocar geometría, el candidato debe producir un informe con:

- labels de huesos normalizados;
- número de huesos y AWGs;
- jerarquía y matrices de bind;
- correspondencia 1:1 por label y por orden;
- lista de bones usados por vértices y por `vb2`;
- ausencia de huesos extra sin destino HD.

El criterio mínimo para el primer validador es **mismos labels, mismo orden y
misma numeración de huesos**. Un mapeo manual no 1:1 se reserva para una fase
posterior de retargeting.

## Protocolo de prueba

1. Extraer el bin PS2 del candidato desde el AFS de referencia.
2. Ejecutar solo `port_ps2_b3_extract.py` y conservar el JSON.
3. Comparar el informe de rig contra el bin HD de destino.
4. Abortar si el rig no es 1:1; no intentar arreglarlo con geometría.
5. Generar geometría bone-local con el pipeline existente.
6. Reconstruir estructura de dibujo solo cuando el pool y sus referencias estén
   documentados; no reutilizar descriptores por índice sin prueba.
7. Empaquetar un bin autocontenido en un slot de prueba aislado.
8. Ejecutar `port_ps2_b3_verify.py` y exportar OBJ antes del juego.
9. Probar con un único mod activo, hash del bin registrado y perfil de guardado
   desechable.
10. Para Babidi/Bulma validar carga y render sobre el anfitrión; no exigir
    select, combate autónomo ni guardado.
11. Para el primer candidato jugable posterior validar select, carga, combate,
    animación, transformaciones y revancha.

## Diagnóstico por capas

| Resultado | Interpretación probable |
|---|---|
| JSON incorrecto | Parser PS2/FaceType/rig |
| OBJ incorrecto | Geometría, matrices, normales o índices |
| OBJ correcto, juego amorfo | Pool/referencias mesh-ref, zonas, arms o descriptores |
| Modelo correcto, animación incorrecta | Rig/arms/orden de huesos |
| Combate correcto, cara/piernas HD | `vb2` todavía no convertido |
| Crash al cargar | AFS, LZX, padding, bin autocontenido o descriptor |

## Decisión

La inyección sobre una plantilla de otro personaje queda como técnica de mejora
local, no como port general. El primer intento técnico será Babidi sobre
Krillin si pasa el scan 1:1. Solo después se estudiará un personaje jugable
nuevo de IW o un candidato con identidad propia.

## Resultado ejecutado 2026-09-08

El primer candidato PS2 real disponible no fue Babidi, sino
`modding resources/All Character Models from IW into AMB format` /
`modding resources update 2/MOD EJEMPLO/Tien With Cape/IW/Tien (With Cape).amo`.

Extracción realizada:

```text
PS2: n_bones=52 parts=15 verts=4565 skinned=3346
```

Comparación contra Tenshinhan HD entry 400:

- HD: 42 bones.
- PS2: 52 bones.
- Los 42 labels HD están presentes en PS2.
- Los 42 labels comunes conservan el mismo orden.
- Los 10 extras PS2 son `MANT`, `RMANT` y `LMANT`.
- Veredicto: **rig base 1:1 aprobado**, con accesorios/capa extra aislables.

Siguiente paso: convertir la geometría del Tien con capa usando Tenshinhan HD
como plantilla, manteniendo inicialmente solo los 42 bones comunes y tratando
los 10 bones de capa como una parte separada. No instalar todavía.
