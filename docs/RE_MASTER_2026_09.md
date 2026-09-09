# RE MASTER — DBZ Budokai 3 HD Collection

> Documento rector para la ingeniería inversa de extremo a extremo.
> Creado: 2026-09-08.

## 1. Objetivo

Construir un modelo reproducible de todo el camino `XEX -> guest -> AFS ->
contenido -> parser -> GPU -> menus/roster`. Un resultado offline no se
considera validado en juego; un crash no se atribuye al bin si el log no
demuestra que el bin fue servido y consumido.

Preguntas que deben quedar respondidas con evidencia:

1. Qué función guest solicita cada recurso y cuándo.
2. Cómo se transforma un índice AFS en un bin, modelo, mesh y draw.
3. Qué datos identifican un personaje, forma, stage, habilidad y slot.
4. Qué cambios funcionan por override y cuáles requieren datos guest o hooks.
5. Por qué un cambio produce render válido, deformación, crash o ningún efecto.

## 2. Estado consolidado

### Confirmado

- US/EU arrancan y aceptan overrides por entrada AFS.
- La tabla AFS efectiva se interpreta desde offset 8.
- Los modelos son bins autocontenidos `#AMB/#AWO/#AWG/#AZT`.
- El guest acepta swaps nativos HD→HD en slots existentes.
- Hay varios layouts de vértices; el offset del bone depende del layout.
- El roster de selección no vive en un SLXS HD: hay tablas en la imagen guest.
- `0xFFFF` significa celda vacía en la ruta de selección observada.
- La inyección que conserva el pool de la plantilla es la única vía PS2→HD con
  resultado visual positivo hasta ahora.
- Tien con capa es una fuente PS2 real; sus 42 huesos comunes coinciden con
  Tenshinhan HD y hay 10 huesos adicionales de capa.

### No confirmado

- Regeneración general de mesh-ref, zonas, bboxes y descriptores con pool nuevo.
- Formato completo de stages y sus enlaces con selección.
- Tabla completa de movimientos, habilidades y parámetros.
- Identidad completa de un personaje nuevo: slot, recursos, voz y persistencia.
- Causa de los crashes `0x85CBD643` de `dbz3_060.log` y `dbz3_062.log`.
  Esos logs no contienen `AFS OVERRIDE HIT` de entry 327 y no son evidencia
  contra el bin Tien.

## 3. Arquitectura a investigar

```text
XEX/imagen descifrada
  -> tablas guest y enumeración de slots
  -> identidad/personaje
  -> resolución de recursos AFS
  -> tabla física/virtual y LZX
  -> #AMB -> #AWO/#AWG/#AZT/#ACM
  -> axes, arms, mesh-ref, zonas, bboxes, descriptores
  -> vertex/index buffers y transformaciones
  -> GPU
  -> menú, combate, animación, voz y guardado
```

Cada enlace debe registrar dirección guest, función consumidora, entrada AFS,
offset, tamaño, endian, estructura, precondiciones y resultado.

## 4. Protocolo de laboratorio

### Baseline congelado

Antes de cada experimento guardar región, idioma, backend, cvars, hashes de
`dbz3.exe`, DLLs, `default.xex`, AFS y mods, además de un log nuevo. No se
mezclan US/EU. Un experimento tiene como máximo un cambio causal.

### Clasificación

- `NO_EFFECT`: no hubo solicitud o hit del override.
- `INFRA_CRASH`: crash sin evidencia de lectura del recurso probado.
- `PARSE_CRASH`: hit confirmado y crash durante deserialización.
- `DRAW_CRASH`: recurso parseado y crash preparando/dibujando.
- `RENDER_BAD`: carga completa con geometría/material/rig incorrectos.
- `RENDER_OK`: visible y estable en la escena mínima.
- `FLOW_OK`: selección, combate, victoria/revancha y salida estables.

### Manifest obligatorio

Cada experimento conservará un JSON con:

```text
experiment_id, region, xex_hash, dll_hashes, mod_hashes,
afs_entry, physical_size, virtual_size, compressed_size,
expected_hits, observed_hits, last_guest_pc, result, notes
```

También se conservarán bin descomprimido, comprimido, JSON intermedio, OBJ,
log y capturas.

## 5. Fases

### F0 — Infraestructura y trazabilidad

- Registrar región y hashes al inicio.
- Correlacionar `AFS LOOKUP`, `OVERRIDE HIT`, `MOD READ`, tamaño pedido y servido.
- Capturar PC guest y contexto de fallo.
- Probar flujo sin mods y un swap HD→HD de control en entry 327.

Aceptación: distinguir `NO_EFFECT`, `INFRA_CRASH` y `PARSE_CRASH` sin depender
de la imagen del juego.

### F1 — Imagen guest y tablas

- Volcar US/EU con hashes y rangos.
- Etiquetar tablas de slots, retratos, records de 184 bytes, disponibilidad,
  formas y runs AFS.
- Seguir lectores y escritores en `generated/`.
- Encontrar conteos, límites y validaciones.

Aceptación: describir `cursor -> slot -> record -> modelo -> retrato` con
funciones y offsets concretos en ambas regiones.

### F2 — AFS y recursos

- Mapear solicitud guest a AFS, entry, offset y tamaño.
- Comparar lectura física, tabla virtual y mid-insert.
- Confirmar bins mayores que `to_read`, incluidos mappings.
- Determinar si la resolución usa entry, offset, AFL, grupo o descriptor.

Aceptación: seguir un override de control desde la llamada guest hasta los
bytes descomprimidos consumidos.

### F3 — Parser y render

- Separar layouts A, B, C, cara, vb2 y stages.
- Modelar contenedores, punteros relativos y endianness.
- Decodificar pool → mesh-ref → zonas → bboxes → descriptores.
- Hacer round-trip de bins originales sin cambios semánticos.
- Ejecutar permutaciones unitarias, una estructura por experimento.
- Capturar la primera lectura guest divergente.

Aceptación: round-trip estable y una permutación mínima que explique la primera
deformación o crash.

### F4 — PS2 → HD

- Vía A: inyección por zona/material sobre pool HD conservado, con fallback al
  vértice HD si el matching no es seguro.
- Vía B: port completo con pool nuevo solo después de cerrar F3.
- Validar primero rig común sin capa; Tien con capa será otro experimento.
- Usar recursos y texturas de plantilla hasta validar el draw.

Aceptación: `RENDER_OK` offline, `RENDER_OK` en escena mínima y después
`FLOW_OK`.

### F5 — Roster y contenido nuevo

- Alias de slot que reutilice recursos originales.
- Modelo y retrato independientes.
- Record, formas y moveset.
- Voz, textos, aura y persistencia.
- Perfil experimental separado de partidas normales.

Cada dependencia se activa en una prueba separada.

### F6 — Stages, habilidades y herramientas

Investigar layout de stages, colisión/animación, `#ACM`, parámetros de combate
y un pipeline genérico de duplicación con manifest y rollback.

## 6. Primera batería

1. Baseline sin mods, US, menú y combate con Krillin.
2. Swap HD→HD conocido en entry 327 con hit confirmado.
3. Bin Tien temporal, solo US y con logs de hit/read.
4. Tien sobre Tenshinhan, no Krillin, para eliminar cambio de rig.
5. Tien sin capa, solo los 42 huesos comunes.
6. Tien con capa, añadiendo los 10 huesos extra.
7. Solo después, reconstruir pool y estructura de dibujo.

La prueba que crasheó al situarse sobre Krillin queda clasificada como
`INFRA_CRASH` provisional hasta demostrar que entry 327 fue servido.

## 7. Referencias

- Formato: `docs/03_formatos/AWO_FORMAT.md`, `BIN_LAYOUT.md` y
  `docs/07_ports/ESTRUCTURA_DIBUJO_HD.md`.
- Roster: `MAPA_ROSTER_HD.md` y `AUDITORIA_DATA_CMN.md`.
- Port: `docs/07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md` y sesiones fechadas.
- RE histórico: `awo_tools/RE_PROGRESO.md` y `awo_tools/CONSOLIDADO.md`.
- Operación: `AGENTS.md` y `docs/05_build/COMO_COMPILAR.md`.

Si un documento antiguo contradice una prueba reproducible posterior, prevalece
la prueba posterior y se marca la corrección con fecha; el historial no se borra.
