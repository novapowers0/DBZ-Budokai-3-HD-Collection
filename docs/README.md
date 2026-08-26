# DBZ Budokai 3 HD Collection — Documentación del proyecto

> Guía accesible para agentes y humanos. Consolidación del estado del proyecto,
> estructura de carpetas, cómo hacer mods, formatos, herramientas y builds.
> Actualizado: 2026-08-14

---

## ÍNDICE

| Carpeta | Contenido |
|---|---|
| [HOJA_DE_RUTA](HOJA_DE_RUTA.md) | **Plan estratégico**: model swaps → costumes → roster |
| [01_estructura](01_estructura/ARBOL.md) | Árbol completo del proyecto, qué es cada carpeta |
| [01_estructura/ESTADO.md](01_estructura/ESTADO.md) | Estado actual, qué funciona, qué falla |
| [02_mods](02_mods/COMO_HACER_MODS.md) | Pipeline de mods (override por entrada) |
| [02_mods/MODEL_SWAP.md](02_mods/MODEL_SWAP.md) | Investigación de model swap (lo que sabemos/falla) |
| [02_mods/TEXTURAS_MOD.md](02_mods/TEXTURAS_MOD.md) | **Pestaña Texturas del launcher** (extraer/editar/reconstruir) |
| [03_formatos](03_formatos/AMO_AWO.md) | Formato del modelo PS2 (#AMO0) vs HD (#AWO) |
| [03_formatos/BIN_LAYOUT.md](03_formatos/BIN_LAYOUT.md) | Layout del bin HD (headers, buffers, vértice) |
| [04_herramientas](04_herramientas/TOOLS.md) | Inventario de herramientas y su función |
| [05_build](05_build/COMO_COMPILAR.md) | Cómo compilar el juego y el SDK |
| [06_limpieza](06_limpieza/PLAN_LIMPIEZA.md) | Plan de limpieza/reorganización pendiente |
| [07_ports](07_ports/ESTUDIO_ECOSISTEMA_MODS.md) | **Estudio del ecosistema de mods + comparativa de herramientas (2026-08-26)** |
| [07_ports/HOJA_DE_RUTA_PORT](07_ports/HOJA_DE_RUTA_PORT_PS2_B3.md) | **Hoja de ruta del port PS2→B3 HD (pipeline nombrado `port_ps2_b3_`)** |
| [07_ports/ESTRUCTURA_DIBUJO_HD](07_ports/ESTRUCTURA_DIBUJO_HD.md) | **Estructura de dibujo HD mapeada (descriptores A/B, mesh-ref, arms)** |

---

## RESUMEN DE 30 SEGUNDOS

- **Qué es**: Port recompilado a PC de DBZ Budokai 3 HD Collection (Xbox 360) con ReXGlue SDK.
- **Jugar**: `out\build\win-amd64-release\dbz3.exe`
- **Config**: `out\build\win-amd64-release\dbz3_user.toml`
- **Mods**: carpeta `mods\<mod>\` junto al exe. Solo los que NO tienen `.disabled`.
- **Estado**: el juego funciona (D3D12, 60fps). El mod de texturas funciona. El model swap aún NO.
- **Prioridad actual**: hacer funcionar el model swap (injectar modelos de otros personajes).

---

## PUNTOS CLAVE DEL PROYECTO

1. **Runtime**: `rexglue-sdk\` (fuente) → se instala en `rexglue\` → el exe usa `rexruntime.dll`.
2. **Formato**: el bin de personaje es `#AWO` (big-endian 360), equivalente al `#AMO0` PS2 (little-endian).
3. **Mods**: el runtime tiene un hook (`AfsFindModOverride`) que sirve archivos por entrada del AFS sin reempaquetar.
4. **Compresión**: los bins del AFS van comprimidos LZX `/N:2048` (NO `/N:32`).
5. **Tamaño slot**: cada entrada del AFS tiene un tamaño fijo; el bin del mod debe caber (padded al slot).
