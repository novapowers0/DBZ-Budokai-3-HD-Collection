# Sesión Tien con capa — rig PS2 contra Tenshinhan HD — 2026-09-08

## Fuente

```text
modding resources update 2/MOD EJEMPLO/Tien With Cape/IW/Tien (With Cape).amo
```

El archivo empieza por `#AMO0` y el extractor real PS2 funciona.

```text
PS2: base=0x0 n_bones=52 parts=15 verts=4565 skinned=3346
```

JSON temporal de extracción:

```text
C:\Users\javie\AppData\Local\Temp\opencode\ps2_candidates\tien_with_cape.json
```

## Plantilla HD

```text
us/data_cmn.afs entry 400
label raíz: TSH_BODY
AWG: primer grupo Tenshinhan HD
bones HD: 42
```

## Comparación

- Los 42 labels de Tenshinhan HD existen en la fuente PS2.
- Los 42 labels comunes mantienen exactamente el mismo orden.
- PS2 añade 10 labels de capa: `XTSH_MANT*`, `XTSH_RMANT`, `XTSH_LMANT`.
- No hay labels HD ausentes en PS2.
- Veredicto: **pasa rig base 1:1**.

## Decisión técnica

Este candidato es mucho mejor que Krillin para validar el port completo:

1. Mantener Tenshinhan HD como plantilla estructural.
2. Convertir primero solo geometría de los 42 bones comunes.
3. Aislar la capa como segundo experimento; no mezclarla con el primer
   diagnóstico.
4. Verificar OBJ y bounds antes de empaquetar.
5. Instalar temporalmente sobre Krillin, slot 327, con un único mod activo.

No se ha generado todavía bin HD ni mod jugable.
