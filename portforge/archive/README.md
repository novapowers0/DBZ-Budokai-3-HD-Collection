# Archivo de versiones antiguas de PortForge

Historico de las entradas que han pasado por el instalador de 1 clic
(`../.forge.json`). **PortForge solo lee `../.forge.json`**, asi que este
directorio no afecta a la instalacion: es un registro para no perder las
entradas retiradas.

- `forge-versions-old.json` — entradas retiradas, listas para copiar/pegar de
  vuelta al array `builds` de `../.forge.json`.
- `_retired` (dentro del JSON) — motivo de retirada de cada version.

## Estado actual (2026-09-26)

Versiones **visibles** en PortForge (las tres ultimas funcionales):

| Version | Notas |
|---|---|
| `1.2.9` | Actual (Latest). Default. Diagnostico que se explica solo: avisos de fps sostenido, disco lento e instalacion mixta; `vram=`/`lim=` en la linea `perf`; guardia de VRAM. |
| `1.2.8.2` | La mejora de texturas deja de hundir los FPS (throttle de texturas dinamicas) + diagnostico en la linea `perf`. Respaldo inmediato. |
| `1.2.8.1` | Volcado de los formatos del HUD/UI + packs RGBA8 (issue #11). |

**Archivadas**: `1.2.8`, `1.2.7`, `1.2.5`, `1.2.6`, `1.2.4-EX`, `1.2.4`, `1.2.3`, `1.2.2-EX`, `1.2.2` (retirada), `1.2.1`, `1.1.4`, `1.1.2`.

## Como rehabilitar una version

1. Abre `forge-versions-old.json` y copia el objeto de `builds` de la version.
2. Pegalo en el array `builds` de `../.forge.json` (el orden es el de la lista;
   la primera entrada es la que se ofrece por defecto junto a
   `defaultVersion`).
3. Comprueba que la URL `releases/download/<tag>/...` responde 200.

> Nota: las versiones marcadas **NO reusar** en `_retired` fallan o son
> incompatibles con el layout actual; no las rehabilites sin arreglarlas.
