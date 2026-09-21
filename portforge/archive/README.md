# Archivo de versiones antiguas de PortForge

Historico de las entradas que han pasado por el instalador de 1 clic
(`../.forge.json`). **PortForge solo lee `../.forge.json`**, asi que este
directorio no afecta a la instalacion: es un registro para no perder las
entradas retiradas.

- `forge-versions-old.json` — entradas retiradas, listas para copiar/pegar de
  vuelta al array `builds` de `../.forge.json`.
- `_retired` (dentro del JSON) — motivo de retirada de cada version.

## Estado actual (2026-09-21)

Versiones **visibles** en PortForge (las tres ultimas funcionales):

| Version | Notas |
|---|---|
| `1.2.8` | Actual (Latest). Default. Fix del volcado de texturas (issue #11). |
| `1.2.7` | Packs de texturas (D3D12 + Vulkan). Respaldo inmediato. |
| `1.2.5` | Base estable anterior (foco y disco). |

**Archivadas**: `1.2.6`, `1.2.4-EX`, `1.2.4`, `1.2.3`, `1.2.2-EX`, `1.2.2` (retirada), `1.2.1`, `1.1.4`, `1.1.2`.

## Como rehabilitar una version

1. Abre `forge-versions-old.json` y copia el objeto de `builds` de la version.
2. Pegalo en el array `builds` de `../.forge.json` (el orden es el de la lista;
   la primera entrada es la que se ofrece por defecto junto a
   `defaultVersion`).
3. Comprueba que la URL `releases/download/<tag>/...` responde 200.

> Nota: las versiones marcadas **NO reusar** en `_retired` fallan o son
> incompatibles con el layout actual; no las rehabilites sin arreglarlas.
