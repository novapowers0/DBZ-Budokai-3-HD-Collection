# DBZ Budokai 3 HD Collection — Documentación del proyecto

Port nativo de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) con ReXGlue SDK. Este índice reúne guías del jugador, documentación técnica e historial del proyecto.

**[Descargar v1.5.7 — última versión](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7)** · **[Vídeo de lanzamiento](https://youtu.be/47XrgFQ8zaQ)** · [Guía de instalación y preguntas frecuentes](../RELEASE_README.md)

## Empieza aquí

- **Descargas:** en **Assets** están el ZIP de Windows x64 y el Kit de Modding opcional. Linux amd64 (Vulkan, BETA) y Mac con Apple Silicon (EXPERIMENTAL, SIN PROBAR) aún no tienen un archivo adjunto; consulta Assets.
- **Instalación:** necesitas tu propia copia legal de Budokai 3 HD Collection; el paquete no trae sus datos. En Windows, extrae el ZIP entero, abre `dbz3.exe`, selecciona tu ISO completa o la carpeta con `default.xex` y sus datos `us/` o `eu/`, y pulsa **JUGAR**. Para los modos de PS2 que requieren otro disco, usa **Juego → Elegir ISO** con tu propia copia.
- **Actualización:** respalda los ajustes, partidas y mods; sustituye juntos el ejecutable y las DLL con el ZIP completo. Solo se migran los ajustes listados que aún tienen el valor anterior de serie. Los archivos obsoletos de la lista pasan a `_respaldo_actualizacion/` sin borrarse.
- **Mods:** se gestionan en **Mods** del launcher o en `mods/` junto al ejecutable. Extrae el Kit opcional sobre la instalación de Windows y abre `DBZ3_ModKit.bat`. [Guía rápida del Kit](02_mods/KIT_GUIA_RAPIDA.md).

## Modos en v1.5.7

- **Dragon HUB:** más allá de la última fila del menú principal. Admite ratón; Retos clásicos mantiene su entrada.
- **Dragon Budokai Survivors (BETA):** hordas, cartas, ocho luchadores, seis escenarios, mejoras permanentes y récords locales.
- **Dragon Rogue (BETA):** ocho caminos, cartas, guardado y reanudación, entrenamiento de Kami y reto diario de El Abismo.
- **Modo Héroe y creador de personajes (BETA):** Habitación, Técnicas, Evolución y Colección renovadas; atuendos por forma compatible.
- **Dragon Tactics (EXPERIMENTAL):** tienda, carrusel e historial por rival al reanudar.
- **Dragon World (EXPERIMENTAL)** y **Nave de Babidi (BETA):** mejoras de tablero y diálogo. Llanura de Budokai 2 cambia de zona durante el combate local con núcleo HD USA y el escenario convertido desde tu propia copia.
- **Infinite World (EXPERIMENTAL):** mejoras de mapa y minijuegos; ruta traducida de arranque y dibujo en cursos compatibles.
- **Leyenda de Mr. Satán (BETA):** voces y música de la escena larga de la primera ronda si están disponibles en tu copia de Budokai 1.
- **Tag Team (BETA)** y **Online (BETA):** protección de relevos en las formas más altas e informes de conexión más claros. Las partidas largas entre dos PC y la compatibilidad Windows/Linux no están confirmadas.

---

## ÍNDICE

| Carpeta | Contenido |
|---|---|
| [HOJA_DE_RUTA_ACELERADA](HOJA_DE_RUTA_ACELERADA.md) | Hoja de ruta de septiembre (histórica; la vigente es `studies 5/01_ROADMAP.md`): ejecución acelerada (swap-first, reader-first, corpus, roster por memoria) con sprints S0-S4 y automatización |
| [HOJA_DE_RUTA_2026_09](HOJA_DE_RUTA_2026_09.md) | Madurez post-1.1.1 (superseded por la acelerada) |
| [EVALUACION_2026_09_PLAN_DEPURACION](EVALUACION_2026_09_PLAN_DEPURACION.md) | **Evaluación del proyecto + plan de depuración soberbia** (crash EU #4, issues, D0-D4) |
| [RE_MASTER_2026_09](RE_MASTER_2026_09.md) | **Plan rector de RE extremo a extremo**: laboratorio, capas, fases y validación |
| [DICTAMEN_GPT6_ASTRA](DICTAMEN_GPT6_ASTRA.md) | **Dictamen externo (GPT-6 Astra)**: plan 0-7 slots nativos + port PS2→B3 (guía vigente) |
| [BRIEFING_GPT6_ASTRA](BRIEFING_GPT6_ASTRA.md) | Briefing técnico que originó el dictamen |
| [HOJA_DE_RUTA](HOJA_DE_RUTA.md) | Plan modding original (histórico, superseded) |
| [HOJA_DE_RUTA_COMUNIDAD](HOJA_DE_RUTA_COMUNIDAD.md) | Feedback comunidad P0-P5 (histórico, todo completado) |
| [SESION_AUTODETECCION_XEX_2026-09-17](SESION_AUTODETECCION_XEX_2026-09-17.md) | **v1.2.2 EX**: auto-detección del ejecutable (volcado retail del disco, ISO original, `DBZ3\`), estados del xex y fix del TOML |
| [ANALISIS_RENDIMIENTO_LOGS_2026-09-18](ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md) | **Rendimiento (histórico):** diagnóstico de un reporte de lentitud con RTX 5090 |
| [SESION_LAUNCHER_AUDIT_2026-09-19](SESION_LAUNCHER_AUDIT_2026-09-19.md) | **v1.2.4/1.2.4 EX**: auditoría del launcher (controles muertos, `audio_gain` real, update check, FXAA/dither, palancas GPU, datos portables) |
| [SESION_IO_FOCO_2026-09-19](SESION_IO_FOCO_2026-09-19.md) | **v1.2.5**: camino de lectura (`dbz3_io_logging`, readahead) + QoL al perder el foco (`fg=`, mute/dim) |
| [SESION_TOML_Y_UX_2026-09-20](SESION_TOML_Y_UX_2026-09-20.md) | **v1.2.6**: autorreparación del TOML + UX anti-abuso de la escala interna |
| [SESION_PACING_WINDOWS_2026-09-20](SESION_PACING_WINDOWS_2026-09-20.md) | **Pacing Windows**: por qué el fix Vulkan de Linux no aplica a D3D12 (sin cambios de código) |
| [SESION_TEXTURAS_PACK_2026-09-20](SESION_TEXTURAS_PACK_2026-09-20.md) | **Packs de texturas tipo PCSX2 (Fase 1, dev)**: volcado DDS + importador a PNG por personaje |
| [SESION_FIX_VOLCADO_2026-09-21](SESION_FIX_VOLCADO_2026-09-21.md) | **v1.2.8**: fix del volcado de texturas (issue #11) - registro de cvars compartido, `REXCVAR_QUERY` |
| [SESION_VOLCADO_FORMATOS_2026-09-23](SESION_VOLCADO_FORMATOS_2026-09-23.md) | **v1.2.8.1**: volcado de los formatos del HUD/UI sin comprimir + packs RGBA8 + tope de versiones por identidad (issue #11) |
| [SESION_PERF_TEXTURAS_2026-09-24](SESION_PERF_TEXTURAS_2026-09-24.md) | **v1.2.8.2**: la mejora de texturas deja de hundir los FPS (solo nivel 0 en recargas dinámicas) + `cfg=`/`upx_dyn=`/`texload=` en la línea `perf` |
| [SESION_DIAGNOSTICO_2026-09-26](SESION_DIAGNOSTICO_2026-09-26.md) | **v1.2.9**: diagnostico que se explica solo (avisos de fps sostenido, disco lento e instalación mixta; `vram=`/`lim=` en `perf`; guardia de VRAM; línea `entorno` con versiones) |
| [LINUX](LINUX.md) | Build nativo Linux con Vulkan, SDL3 y CI usando codegen privado |
| [ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14](ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14.md) | Escalado/rendimiento: FSR1/CAS sí; FSR3/DLSS no viable a corto plazo |
| [VIABILIDAD_UPSCALING_TEMPORAL_2026-09-29](VIABILIDAD_UPSCALING_TEMPORAL_2026-09-29.md) | **DLSS/DLAA/FSR3/Frame Gen**: viabilidad real tras analizar `reblue` y `LostOdysseyRecomp`; qué adoptamos (DRED, gamecontrollerdb) y qué no |
| [01_estructura](01_estructura/ARBOL.md) | Árbol completo del proyecto, qué es cada carpeta |
| [01_estructura/ESTADO.md](01_estructura/ESTADO.md) | Estado actual, qué funciona, qué falla |
| [01_estructura/HISTORICO_AGENTS.md](01_estructura/HISTORICO_AGENTS.md) | Historial verbatim de sesiones hasta 2026-09-02 (solo bajo demanda) |
| [01_estructura/HISTORICO_RELEASES.md](01_estructura/HISTORICO_RELEASES.md) | Detalle verbatim extraído de `AGENTS.md` en la compactación 2026-09-26: §A releases 1.1.3→1.2.9, §B investigación del port PS2→B3, §C launcher (solo bajo demanda) |
| [02_mods](02_mods/COMO_HACER_MODS.md) | Pipeline de mods (override por entrada) |
| [Guía rápida del Mod Kit](02_mods/KIT_GUIA_RAPIDA.md) | Para jugadores: asistentes, deshacer y límites ([EN](02_mods/KIT_GUIA_RAPIDA_EN.md)) |
| [02_mods/MODEL_SWAP.md](02_mods/MODEL_SWAP.md) | Investigación de model swap (histórica; hoy el swap funciona) |
| [02_mods/TEXTURAS_MOD.md](02_mods/TEXTURAS_MOD.md) | **Pestaña Texturas del launcher** (extraer/editar/reconstruir) |
| [02_mods/PACKS_DE_TEXTURAS.md](02_mods/PACKS_DE_TEXTURAS.md) | **Packs de texturas (estilo PCSX2)**: volcado dev, cargador en runtime y guía para autores |
| [02_mods/SESION_MODS_LAUNCHER_2026-09-14.md](02_mods/SESION_MODS_LAUNCHER_2026-09-14.md) | Barrido de mods + cierre/pulido del **Model Swap HD↔HD** + aviso ISO + refactor del Centro de mods |
| [02_mods/STUDIO_CAMARAS.md](02_mods/STUDIO_CAMARAS.md) | **Studio de cámaras**: editar las cámaras de técnicas, plantillas, ida y vuelta a Blender, guardar como mod |
| [03_formatos](03_formatos/AMO_AWO.md) | Formato del modelo PS2 (#AMO0) vs HD (#AWO) |
| [03_formatos/BIN_LAYOUT.md](03_formatos/BIN_LAYOUT.md) | Layout del bin HD (headers, buffers, vértice) |
| [03_formatos/AWO_FORMAT.md](../AWO_FORMAT.md) | Formato #AWO HD campo a campo |
| [03_formatos/ACM_FORMAT.md](03_formatos/ACM_FORMAT.md) | Formato moveset HD (#AMB→#CSK→#ACM) + edición de habilidades |
| [03_formatos/STAGES_FORMAT.md](03_formatos/STAGES_FORMAT.md) | Bins de stage (#AMB→#ZDD/#CAD/#CAS/#SPX) + contenedor PS2 (ports IW) |
| [03_formatos/CAPSULAS_B3.md](03_formatos/CAPSULAS_B3.md) | Cápsulas: catálogo #SKC, fichas de habilidades, plazas extra 44-63 y ports de IW (modo hiper) |
| [03_formatos/CAMARA_ACC.md](03_formatos/CAMARA_ACC.md) | Cámaras #ACC/#AMC: 4 pistas (posición, punto de mira, giro, fov) |
| [03_formatos/SB_VS_B3_MOVESET.md](03_formatos/SB_VS_B3_MOVESET.md) | Movesets de Shin Budokai frente a B3 (AP 20→16, HR 160→128, códigos, daño ×0,85) |
| [03_formatos/FORMAS_Y_KI.md](03_formatos/FORMAS_Y_KI.md) | Transformaciones: barras exigidas, cadena de cápsulas, nivel base de ki |
| [03_formatos/TOON_Y_BRILLO_HD.md](03_formatos/TOON_Y_BRILLO_HD.md) | Shader toon (base − rampa, alfa = sin sombrear) y brillo HD de borde (c39.x) |
| [04_herramientas](04_herramientas/TOOLS.md) | Inventario de herramientas y su función |
| [05_build](05_build/COMO_COMPILAR.md) | Cómo compilar el juego y el SDK |
| [06_limpieza](06_limpieza/PLAN_LIMPIEZA.md) | Plan de limpieza/reorganización |
| [06_limpieza/INVENTARIO_FISICO_2026-09](06_limpieza/INVENTARIO_FISICO_2026-09.md) | Inventario físico y artefactos |
| [06_limpieza/INTEGRACION_MODDING_HD](06_limpieza/INTEGRACION_MODDING_HD.md) | Clasificación de herramientas y recursos para HD |
| [12_conectores](12_conectores/) | **Conectores nativos**: menu principal, cola de pantallas, combate nativo desde el menu y combates encadenados sin menu (`src/hub.cpp`, doc 10 s9.4) |
| [14_frentes](14_frentes/REGLAS.md) | **Frentes de trabajo 2026-10-15**: reglas comunes e informe de cada frente (HUB, modos, Tag Team, ports, escenarios, pasarela, QoL, bugs, vision futura). Ronda 2 ([RONDA2.md](14_frentes/RONDA2.md)): combate, rpg, transformaciones, online, grafico, modkit, campanas, eu (+ `eu_mapa.txt`) |
| [14_dragon_universe](14_dragon_universe/) | Dragon Universe por dentro: opcodes, casillas, guardado |
| [15_tecnicas](15_tecnicas/) | Tecnicas por dentro: codigos de energia, coste de ki, #CST/#CSE |
| [16_pasarela_modos](16_pasarela_modos/) | **Pasarela de modos PS2**: documentación técnica de Leyenda de Mr. Satán, tablero B2, misiones y retos de IW ([04_misiones_y_tablero.md](16_pasarela_modos/04_misiones_y_tablero.md); `src/pasarela*.cpp`, `mod center hd/pasarela/pasarela.py`) |
| [jugador/MODOS_Y_RETOS.md](jugador/MODOS_Y_RETOS.md) | Guia del jugador: HUB, retos y torre de la semana |
| [jugador/FORMAS_7_8_Y_TAG_TEAM.md](jugador/FORMAS_7_8_Y_TAG_TEAM.md) | Guía del jugador (12/11): empezar en las formas 7 y 8 y relevo del Tag Team en esas formas |
| [jugador/DRAGON_ROGUE.md](jugador/DRAGON_ROGUE.md) | Guía del jugador (12/11): Dragon Rogue (beta), sus 8 caminos, cartas y evoluciones/fusiones, jefes, reto diario y Entrenamiento de Kami |
| [jugador/KIT_DE_MODDING.md](jugador/KIT_DE_MODDING.md) | Guía del jugador (12/11): qué permite hoy el Kit de Modding (probado en el juego, en pruebas y lo que aún no se puede) |
| [jugador/CARGA_TACTICS_Y_SURVIVORS.md](jugador/CARGA_TACTICS_Y_SURVIVORS.md) | Guía del jugador (12/11): por qué Dragon Tactics y Dragon Budokai Survivors cargan mucho más rápido (y qué esperar la primera vez) |
| [02_mods/PLUGINS.md](02_mods/PLUGINS.md) | Plugins de código (DLL en `plugins/`, API `rex_mod_init`): vía oficial para mods como Budokai3Access |

En el árbol interno, `studies 5/01_ROADMAP.md` recoge la planificación; `docs/07_ports/` reúne las notas técnicas de conversión de modelos. Esas rutas no se incluyen en el espejo público.

---

## PUNTOS CLAVE DEL PROYECTO

1. **Runtime**: `rexglue-sdk-0.10\` (fuente) → se instala en `rexglue\` → el exe usa `rexruntime.dll`.
2. **Formato**: el bin de personaje es `#AWO` (big-endian 360), equivalente al `#AMO0` PS2 (little-endian).
3. **Mods**: el runtime tiene un hook (`AfsFindModOverride`) que sirve archivos por entrada del AFS sin reempaquetar.
4. **Compresión**: los bins del AFS van comprimidos LZX `/N:2048` (NO `/N:32`).
5. **Tamaño slot**: cada entrada del AFS tiene un tamaño fijo; el bin del mod debe caber (padded al slot) o usar mid-insert virtual.
6. **Contexto operativo**: `AGENTS.md` es la referencia operativa compactada; el detalle histórico está en `01_estructura/HISTORICO_AGENTS.md` (hasta 2026-09-02) y `01_estructura/HISTORICO_RELEASES.md` (releases, port y launcher).

---

## Referencias de la versión

Consulta las notas completas y los límites en la página de v1.5.7 y el [historial de cambios](../CHANGELOG.md) para versiones anteriores. El corte interno R23 se conserva en `studies 5/ENTREGAS/R23/`: describe pruebas previas y no acredita por sí mismo la versión publicada.

[Créditos del proyecto](../README_ES.md#créditos-y-apoyo) · [Ko-fi de NovaPowers](https://ko-fi.com/novapowers)

---

## MANTENIMIENTO DE LA DOCUMENTACIÓN

**Regla de oro**: `AGENTS.md` (lo que se carga en cada sesión) se mantiene
**≤ 60 KB** y solo con lo OPERATIVO (estado, offsets, constraints, comandos).
Todo relato histórico o blow-by-blow se mueve a los históricos, no se borra.

- **Archivos históricos** (no se cargan por defecto):
  `01_estructura/HISTORICO_AGENTS.md` (hasta 2026-09-02) y
  `01_estructura/HISTORICO_RELEASES.md` (§A releases, §B port, §C launcher,
  §D snapshot verbatim del AGENTS previo a la compactación 2026-09-26).
- **Ciclo de compaction**: cada release que engorde `AGENTS.md` por encima de
  ~60 KB → mover el detalle de esa release a `HISTORICO_RELEASES.md` §A y dejar
  una fila en la tabla §3.0 + los invariantes en §3.0b.
- **Sesiones**: cada release/sesión relevante tiene su `SESION_*_2026-09-*.md`;
  enlazarlo desde `AGENTS.md` §3.0 (columna "Doc de sesión") y desde este índice.
- **Verificación tras compactar**: (1) `AGENTS.md` ≤ 60 KB; (2) grep de tokens
  operativos (hashes, offsets, cvars, comandos) presentes; (3) sync con
  `tools/sync_github.ps1` y commit en `github/`.
