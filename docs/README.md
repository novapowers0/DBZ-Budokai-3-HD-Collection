# DBZ Budokai 3 HD Collection — Documentación del proyecto

> Guía accesible para agentes y humanos. Consolidación del estado del proyecto,
> estructura de carpetas, cómo hacer mods, formatos, herramientas y builds.
> Actualizado: 2026-09-09

---

## ÍNDICE

| Carpeta | Contenido |
|---|---|
| [HOJA_DE_RUTA_ACELERADA](HOJA_DE_RUTA_ACELERADA.md) | **Hoja de ruta activa**: ejecución acelerada (swap-first, reader-first, corpus, roster por memoria) con sprints S0-S4 y automatización |
| [HOJA_DE_RUTA_2026_09](HOJA_DE_RUTA_2026_09.md) | Madurez post-1.1.1 (superseded por la acelerada) |
| [EVALUACION_2026_09_PLAN_DEPURACION](EVALUACION_2026_09_PLAN_DEPURACION.md) | **Evaluación del proyecto + plan de depuración soberbia** (crash EU #4, issues, D0-D4) |
| [RE_MASTER_2026_09](RE_MASTER_2026_09.md) | **Plan rector de RE extremo a extremo**: laboratorio, capas, fases y validación |
| [DICTAMEN_GPT6_ASTRA](DICTAMEN_GPT6_ASTRA.md) | **Dictamen externo (GPT-6 Astra)**: plan 0-7 slots nativos + port PS2→B3 (guía vigente) |
| [BRIEFING_GPT6_ASTRA](BRIEFING_GPT6_ASTRA.md) | Briefing técnico que originó el dictamen |
| [HOJA_DE_RUTA](HOJA_DE_RUTA.md) | Plan modding original (histórico, superseded) |
| [HOJA_DE_RUTA_COMUNIDAD](HOJA_DE_RUTA_COMUNIDAD.md) | Feedback comunidad P0-P5 (histórico, todo completado) |
| [01_estructura](01_estructura/ARBOL.md) | Árbol completo del proyecto, qué es cada carpeta |
| [01_estructura/ESTADO.md](01_estructura/ESTADO.md) | Estado actual, qué funciona, qué falla |
| [01_estructura/HISTORICO_AGENTS.md](01_estructura/HISTORICO_AGENTS.md) | Historial verbatim de sesiones (solo bajo demanda) |
| [02_mods](02_mods/COMO_HACER_MODS.md) | Pipeline de mods (override por entrada) |
| [02_mods/MODEL_SWAP.md](02_mods/MODEL_SWAP.md) | Investigación de model swap (lo que sabemos/falla) |
| [02_mods/TEXTURAS_MOD.md](02_mods/TEXTURAS_MOD.md) | **Pestaña Texturas del launcher** (extraer/editar/reconstruir) |
| [02_mods/SESION_MODS_LAUNCHER_2026-09-14.md](02_mods/SESION_MODS_LAUNCHER_2026-09-14.md) | Barrido de mods + cierre/pulido del **Model Swap HD↔HD** + aviso ISO + refactor del Centro de mods |
| [03_formatos](03_formatos/AMO_AWO.md) | Formato del modelo PS2 (#AMO0) vs HD (#AWO) |
| [03_formatos/BIN_LAYOUT.md](03_formatos/BIN_LAYOUT.md) | Layout del bin HD (headers, buffers, vértice) |
| [03_formatos/AWO_FORMAT.md](03_formatos/AWO_FORMAT.md) | Formato #AWO HD campo a campo |
| [03_formatos/ACM_FORMAT.md](03_formatos/ACM_FORMAT.md) | Formato moveset HD (#AMB→#CSK→#ACM) + edición de habilidades |
| [03_formatos/STAGES_FORMAT.md](03_formatos/STAGES_FORMAT.md) | Bins de stage (#AMB→#ZDD/#CAD/#CAS/#SPX) + contenedor PS2 (ports IW) |
| [04_herramientas](04_herramientas/TOOLS.md) | Inventario de herramientas y su función |
| [05_build](05_build/COMO_COMPILAR.md) | Cómo compilar el juego y el SDK |
| [06_limpieza](06_limpieza/PLAN_LIMPIEZA.md) | Plan de limpieza/reorganización |
| [06_limpieza/INVENTARIO_FISICO_2026-09](06_limpieza/INVENTARIO_FISICO_2026-09.md) | Inventario físico y artefactos |
| [06_limpieza/INTEGRACION_MODDING_HD](06_limpieza/INTEGRACION_MODDING_HD.md) | Clasificación de herramientas y recursos para HD |
| [07_ports](07_ports/ESTRUCTURA_DIBUJO_HD.md) | **Estructura de dibujo HD mapeada (descriptores A/B, mesh-ref, arms)** |

---

## RESUMEN DE 30 SEGUNDOS

- **Qué es**: Port recompilado a PC de DBZ Budokai 3 HD Collection (Xbox 360) con ReXGlue SDK.
- **Jugar**: `out\build\win-amd64-release\dbz3.exe`
- **Config**: `out\build\win-amd64-release\dbz3_user.toml`
- **Mods**: carpeta `mods\<mod>\` junto al exe. Solo los que NO tienen `.disabled`.
- **Estado**: **v1.2.1 publicada (Latest)** — hotfix del launcher (crash al cerrar tras Model Swap/Texturas, etiqueta de nitidez FSR, refresco de la lista de mods) sobre la v1.2.0 (Centro de mods renovado + Model Swap HD↔HD pulido + aviso de modo ISO). Juego funcional (D3D12, 60fps, US+EU). Swap nativo HD↔HD y texturas funcionan (Vía A aproximada). Port completo PS2→HD **aparcado** (§3.4.10). Escalado: FSR1/CAS sí; FSR3/DLSS no viable a corto plazo.

---

## PUNTOS CLAVE DEL PROYECTO

1. **Runtime**: `rexglue-sdk-0.10\` (fuente) → se instala en `rexglue\` → el exe usa `rexruntime.dll`.
2. **Formato**: el bin de personaje es `#AWO` (big-endian 360), equivalente al `#AMO0` PS2 (little-endian).
3. **Mods**: el runtime tiene un hook (`AfsFindModOverride`) que sirve archivos por entrada del AFS sin reempaquetar.
4. **Compresión**: los bins del AFS van comprimidos LZX `/N:2048` (NO `/N:32`).
5. **Tamaño slot**: cada entrada del AFS tiene un tamaño fijo; el bin del mod debe caber (padded al slot) o usar mid-insert virtual.
6. **Contexto operativo**: `AGENTS.md` es la referencia operativa compactada; el detalle histórico está en `01_estructura/HISTORICO_AGENTS.md`.
