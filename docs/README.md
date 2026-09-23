# DBZ Budokai 3 HD Collection — Documentación del proyecto

> Guía accesible para agentes y humanos. Consolidación del estado del proyecto,
> estructura de carpetas, cómo hacer mods, formatos, herramientas y builds.
> Actualizado: 2026-09-23 (tras la **v1.2.8.1**)

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
| [SESION_AUTODETECCION_XEX_2026-09-17](SESION_AUTODETECCION_XEX_2026-09-17.md) | **v1.2.2 EX**: auto-detección del ejecutable (volcado retail del disco, ISO original, `DBZ3\`), estados del xex y fix del TOML |
| [ANALISIS_RENDIMIENTO_LOGS_2026-09-18](ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md) | **Rendimiento**: análisis de los logs del reporte "va MUY lento" (RTX 5090) + instrumentación `dbz3_perf_logging` + gate del log AFS |
| [SESION_LAUNCHER_AUDIT_2026-09-19](SESION_LAUNCHER_AUDIT_2026-09-19.md) | **v1.2.4/1.2.4 EX**: auditoría del launcher (controles muertos, `audio_gain` real, update check, FXAA/dither, palancas GPU, datos portables) |
| [SESION_IO_FOCO_2026-09-19](SESION_IO_FOCO_2026-09-19.md) | **v1.2.5**: camino de lectura (`dbz3_io_logging`, readahead) + QoL al perder el foco (`fg=`, mute/dim) |
| [SESION_TOML_Y_UX_2026-09-20](SESION_TOML_Y_UX_2026-09-20.md) | **v1.2.6**: autorreparación del TOML + UX anti-abuso de la escala interna |
| [SESION_PACING_WINDOWS_2026-09-20](SESION_PACING_WINDOWS_2026-09-20.md) | **Pacing Windows**: por qué el fix Vulkan de Linux no aplica a D3D12 (sin cambios de código) |
| [SESION_TEXTURAS_PACK_2026-09-20](SESION_TEXTURAS_PACK_2026-09-20.md) | **Packs de texturas tipo PCSX2 (Fase 1, dev)**: volcado DDS + importador a PNG por personaje |
| [SESION_FIX_VOLCADO_2026-09-21](SESION_FIX_VOLCADO_2026-09-21.md) | **v1.2.8**: fix del volcado de texturas (issue #11) - registro de cvars compartido, `REXCVAR_QUERY` |
| [SESION_VOLCADO_FORMATOS_2026-09-23](SESION_VOLCADO_FORMATOS_2026-09-23.md) | **v1.2.8.1**: volcado de los formatos del HUD/UI sin comprimir + packs RGBA8 + tope de versiones por identidad (issue #11) |
| [LINUX](LINUX.md) | Build nativo Linux con Vulkan, SDL3 y CI usando codegen privado |
| [ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14](ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14.md) | Escalado/rendimiento: FSR1/CAS sí; FSR3/DLSS no viable a corto plazo |
| [07_ports/TEXTURAS_HD_RUNTIME_UPSCALE](07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md) | **Texturas HD en runtime (APARCADO)**: capa exterior D3D12, evidencia de por qué el override del bin no sirve y cómo retomarlo |
| [01_estructura](01_estructura/ARBOL.md) | Árbol completo del proyecto, qué es cada carpeta |
| [01_estructura/ESTADO.md](01_estructura/ESTADO.md) | Estado actual, qué funciona, qué falla |
| [01_estructura/HISTORICO_AGENTS.md](01_estructura/HISTORICO_AGENTS.md) | Historial verbatim de sesiones (solo bajo demanda) |
| [02_mods](02_mods/COMO_HACER_MODS.md) | Pipeline de mods (override por entrada) |
| [02_mods/MODEL_SWAP.md](02_mods/MODEL_SWAP.md) | Investigación de model swap (lo que sabemos/falla) |
| [02_mods/TEXTURAS_MOD.md](02_mods/TEXTURAS_MOD.md) | **Pestaña Texturas del launcher** (extraer/editar/reconstruir) |
| [02_mods/PACKS_DE_TEXTURAS.md](02_mods/PACKS_DE_TEXTURAS.md) | **Packs de texturas (estilo PCSX2)**: volcado dev, cargador en runtime y guía para autores |
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
- **Estado**: **v1.2.8.1 publicada (Latest)** - El volcado de texturas cubre ya los formatos del HUD/UI sin comprimir (RGBA8, RGB565, RGB5A1, RGB655, RGBA4, L8, L8A8, RGBA1010102; antes solo los DXT) y los packs aceptan **RGBA8**; tope de 4 versiones por identidad para que la textura de video no llene el disco (issue #11, seguimiento). Sobre la v1.2.8 (fix del volcado: la cvar del launcher no llegaba al plugin; ahora se lee con `REXCVAR_QUERY`). Sobre la v1.2.7 (packs de texturas estilo PCSX2, D3D12 y Vulkan), la v1.2.6 (mejora de texturas HD pulida, RGBA8 nativas, HUD limpio, UX de escala y autorreparacion del `dbz3_user.toml`), la v1.2.5 (foco/disco: `dbz3_io_logging`, readahead, mute/dim), la v1.2.4 EX (FXAA/dither, palancas GPU, datos de usuario portables, volumen real), la v1.2.3 y la v1.2.2 EX (auto-detección del ejecutable + modo ISO retail). Juego funcional (D3D12, 60 fps, US+EU). Swap nativo HD↔HD y texturas funcionan (Vía A aproximada). Port completo PS2→HD **aparcado** (§3.4.10). Escalado: FSR1/CAS sí; FSR3/DLSS no viable a corto plazo. El consumo alto es el **supersampling** (escala interna), no las texturas HD.

---

## PUNTOS CLAVE DEL PROYECTO

1. **Runtime**: `rexglue-sdk-0.10\` (fuente) → se instala en `rexglue\` → el exe usa `rexruntime.dll`.
2. **Formato**: el bin de personaje es `#AWO` (big-endian 360), equivalente al `#AMO0` PS2 (little-endian).
3. **Mods**: el runtime tiene un hook (`AfsFindModOverride`) que sirve archivos por entrada del AFS sin reempaquetar.
4. **Compresión**: los bins del AFS van comprimidos LZX `/N:2048` (NO `/N:32`).
5. **Tamaño slot**: cada entrada del AFS tiene un tamaño fijo; el bin del mod debe caber (padded al slot) o usar mid-insert virtual.
6. **Contexto operativo**: `AGENTS.md` es la referencia operativa compactada; el detalle histórico está en `01_estructura/HISTORICO_AGENTS.md`.
