# EVALUACIÓN 2026-09 + PLAN DE DEPURACIÓN SOBERBIA

> Fecha: 2026-09-09 (tras publicar v1.1.3 "El parche de la ISO").
> Objetivo: evaluar el estado real del proyecto, diagnosticar los issues de
> GitHub con mensajes nuevos, y trazar un plan de depuración exhaustivo.

---

## 1. EVALUACIÓN DEL PROYECTO (estado real)

### 1.1 Qué está SÓLIDO (verificado)

| Área | Estado | Evidencia |
|---|---|---|
| **Juego** | Muy funcional | D3D12 60fps, teclado default, presets GPU, frame_cap real, US+EU (core dual) |
| **ISO / disco** | Funcional | v1.1.3: selector siempre visible, extrae solo default.xex, smoke test validado |
| **Launcher** | Pulido | i18n auditada 0 gaps (ES/EN/IT/DE/FR), 0 warnings, mensajes para no técnicos |
| **Mods override** | Funcional | AfsFindModOverride + mid-insert virtual (100% ligero, en memoria) |
| **Swap B3→B3** | Validado | Goten, Vegeta 424, Babidi, Bulma |
| **Inyección PS2→HD** | Funcional | cell_npm4 (umbral binario 0.8) = mejor resultado |
| **RE de contenido** | Avanzado | data_cmn auditada (3990 entradas), stages localizados, roster mapeado (0x82372818/0x823268C0) |
| **Release pipeline** | Sólido | make_release + verify_release (zip limpio, DLLs baseline, sin residuos) |

### 1.2 Deuda técnica / puntos débiles

1. **Crash EU en Dragon Universe sigue vivo** (issue #4, mensaje NUEVO de Goten46).
2. **`std::terminate` intermitente de `LaunchModule`** (mitigado con try/catch, throw exacto sin localizar).
3. **`roster_trace.cpp`** es diagnóstico de F3.1; gateado por F10/dev (no molesta).
4. **`out/analysis/guest_image/build/`** (dump_image) dejado como artefacto — útil, mantener.
5. **Codegen EU**: se desconoce qué xex EU exacto lo generó (ver §3.2). Riesgo de reproducibilidad.
6. `docs/01_estructura/ESTADO.md` está **desactualizado** (dice "2026-08-18" y no refleja v1.1.2/1.1.3 ni el ISO).

---

## 2. ISSUES DE GITHUB — AUDITORÍA COMPLETA

| # | Título | Estado | Mensaje nuevo tras respuesta? | Acción |
|---|---|---|---|---|
| 6 | ¿Soundtrack PS2 en vez del HD? + save states | OPEN | **SIN respuesta del owner** | **Responder** (§4.1) |
| 5 | Add to PortForge? | OPEN | zamiba (owner de PortForge) prometió responder "in a day or two" (2026-09-09) | **Esperar/recordar**; responder si aparece (§4.2) |
| 4 | [BUG] Crash Dragon Universe + START (EU) | OPEN | **SÍ — Goten46: START YA NO crashea, pero Dragon Universe SIGUE crasheando con `0x8215B378`** | **Fix de codegen EU** (§3) |
| 3 | Apple Silicon Mac CrossOver | OPEN | No (solo respuesta del owner; el splash rojo "Press START" queda abierto) | Esperar confirmación Vulkan en Mac; el splash es cosmético |
| 2 | It does not start | CLOSED | Resuelto por la comunidad (colocar assets en raíz) | Cerrar ✓ |
| 1 | [v.1.0.4] Potential Bugs | OPEN | No (owner pidió re-test en v1.1.2) | Esperar repro en v1.1.2/v1.1.3 |

### Prioridad de acción
1. **#4**: bug real de codegen EU con reporte nuevo → fix técnico (§3).
2. **#6**: pregunta legítima de la comunidad sin respuesta → responder (soundtrack PS2 + save states).
3. **#5**: seguimiento con zamiba (PortForge) — pendiente de su respuesta prometida.

---

## 3. DIAGNÓSTICO DEL CRASH EU (#4) — `0x8215B378`

### 3.1 El reporte (Goten46, 2026-09-09)

```
[critical] UNREGISTERED indirect call: target=0x8215B378 caller_lr=0x8209F390
[critical] [FATAL] Call to invalid or unregistered function at guest address 0x8215B378
```

- Confirma que el fix de v1.1.2 (`sub_820F2398` registrada) **sí resolvió el crash de START en pelea**.
- Dragon Universe (selección de personaje) sigue crasheando: **otra función despachada por tabla de punteros que el recompilador no registró**.

### 3.2 Hallazgos del análisis

1. **`0x8215B378` NO estaba registrada** en `generated_eu/` (ni `dbz3_eu_register.cpp`, ni `dbz3_eu_init.cpp`).
2. Cae entre dos funciones registradas: `0x8215B368` y `0x8215B388` → hueco de **0x20 bytes (8 instrucciones)** → misma categoría que `0x820F2398` (función plegada como dead fall-through, solo alcanzable vía puntero de función).
3. El caller `0x8209F390` está en `dbz3_eu_recomp.19.cpp:1718` — un `REX_CALL_INDIRECT_FUNC` (despacho por vtable: `lwz r11,0(r31)` → `+68` → `slot*8` → `bctrl`).
4. **Cuerpo PPC de `0x8215B378`** (extraído del xex EU real): `lis r11,-32234; addi r11,r11,-28880; stw r11,16(r3); b 0x82158F88` (0x10 bytes) — escribe un puntero de vtable en `+16(r3)` y ramifica. Idéntico en patrón a su hermana `sub_8215B368`.
5. **El xex `yae3_xenon_eu.xex` (C37EB979) SÍ es el correcto**: el diff de direcciones registradas entre el codegen probado y el re-codegen del xex actual mostró que la ÚNICA diferencia era `0x820f2398` (perdida por config mal ubicado). El hallazgo previo del "dump no coincide" era un artefacto del dump mal generado (`dbz3_us_image.bin`).

### 3.3 Vía de fix — EJECUTADA (2026-09-10)

1. **Config corregido**: `0x820F2398` y la nueva `0x8215B378` movidas DENTRO de `[functions]` (antes de `[[switch_tables]]`) en `dbz3_config_eu.toml`. ⚠️ Regla del histórico L2726-27: entradas tras `[[switch_tables]]` se ignoran.
2. **Re-codegen probado pero DESCARTADO**: el recompilador actual genera nombres SIN prefijo `dbz3eu_` → colisión de símbolos con el codegen US en el build dual. El codegen probado (con prefijo) vino de un rexglue.exe anterior.
3. **Fix MANUAL aplicado en 4 sitios del codegen EU** (patrón idéntico al fix `0x820F2398`):
   - `generated_eu/dbz3_eu_recomp.16.cpp`: `DEFINE_REX_FUNC(dbz3eu_sub_8215B378)` (tras `sub_8215B368`).
   - `generated_eu/dbz3_eu_funcs.16.h`: declaración `DECLARE_REX_FUNC(dbz3eu_sub_8215B378)` + `DECLARE_REX_FUNC(dbz3eu_sub_82158F88)` (la llamada cruzada a la partición 18).
   - `generated_eu/dbz3_eu_funcs.h`: `DECLARE_REX_FUNC(dbz3eu_sub_8215B378)`.
   - `generated_eu/dbz3_eu_register.cpp`: `SetFunction(0x8215B378, dbz3eu_sub_8215B378)`.
   - `generated_eu/dbz3_eu_init.cpp`: `{ 0x8215B378, dbz3eu_sub_8215B378 }`.
4. **Problema encontrado en el camino — cvar `dbz1_diag_logging`**: el SDK instalado en `rexglue/bin` (recompilado 2026-09-09 23:29) NO tenía el cvar → el build dual fallaba al enlazar `roster_trace.cpp`. Solución: recompilar el runtime baseline (`rexglue-sdk-0.10/out/build-win-vulkan-baseline`, targets `rexruntime rexgpu-xenos`) y reinstalar DLL+lib en `rexglue/`.
5. **Build dual + release compilados y enlazados** (11792 funciones EU, +1). Boot EU validado sin FATAL (lee `data_eng.afs`, `data_cmn.afs` 3983-3985, `data_yah.afs`).

### 3.4 Validación
- Boot EU headless (skip_launcher): proceso vivo 25s+, sin `UNREGISTERED indirect call`, guest cargando AFS normales.
- **Pendiente**: validación en juego real del usuario del issue #4 (Dragon Universe EU + START). Para v1.1.4.

---

## 4. RESPUESTAS A ISSUES

### 4.1 Issue #6 — Soundtrack PS2 + save states (responder)

Respuesta propuesta (traducción natural):

- **Soundtrack PS2**: técnicamente SÍ es posible. La música es audio ADX en los AFS de audio del juego (`adx_*.afs`); el runtime ya soporta mod de música (`og_music`, reemplaza AFS de audio por región). Habría que extraer el ADX del Budokai 3 PS2 (o del mod de RPCS3) y empaquetarlo como mod de música. Prometer guía / probarlo.
- **Save states**: NO es viable a corto plazo. El port es una **recompilación estática** (no un emulador): no hay estado de CPU/memoria que congelar como en un emulador. Lo que SÍ se puede ofrecer: el guardado nativo del juego (memcards) ya funciona; y para "no perder el torneo" sugerir retry desde el guardado del juego. Explicar la diferencia emulador vs recompilación.

### 4.2 Issue #5 — PortForge

- zamiba (dueño de PortForge) respondió que actualizará `portforge-mediaitems` y volverá "in a day or two" (2026-09-09). El owner ya dio el `.forge.json`/`.mediaitem.json` en `portforge/`.
- **Acción**: esperar su respuesta; si no aparece en ~5-7 días, hacer ping cortés. No hay nada que arreglar en nuestro repo por ahora.

### 4.3 Issue #3 — Mac CrossOver

- Los dos hallazgos están corregidos en v1.1.2/v1.1.3 (ResolveRegion + gpu_backend). El splash rojo "Press START" sigue abierto (cosmético, D3DMetal/16-bit).
- **Acción**: no hay mensaje nuevo. Si el tester de Mac prueba el backend Vulkan en v1.1.3 y responde, evaluar el splash. No responder hasta que respondan ellos.

### 4.4 Issue #1 — v1.0.4 bugs

- Owner pidió repro en v1.1.2. Sin respuesta nueva.
- **Acción**: no responder aún. Si el usuario confirma en v1.1.3 (frame cap real, idioma), atender punto a punto.

---

## 5. PLAN DE DEPURACIÓN SOBERBIA (diseño)

### Principio rector
*No adivinar: leer del guest.* El codegen `generated/` + `generated_eu/` es el parser REAL. Cada diagnóstico se valida con logs del runtime (PC guest, reads AFS, indirect calls).

### Fase D0 — Instrumentación de diagnóstico (base de todo)
1. **Módulo de captura de indirect calls no registrados**: el runtime ya loguea `UNREGISTERED indirect call` con target/caller_lr/r3/r4/r11. Añadir opción de **volcar el contexto completo** (callstack guest, tabla de punteros origen) para acelerar el diagnóstico de cada nueva aparición.
2. **Dump de imagen por MD5**: automatizar la herramienta `dump_image` para que el log incluya el MD5 del xex fuente y el mapeo de secciones → evita el bloqueador de §3.2 (xex equivocado).
3. **Trace de la tabla 0x8201E348**: loguear qué punteros de la tabla de eventos/combate se despachan, para detectar TODAS las funciones plegadas de una pasada (no de crash en crash).

### Fase D1 — Cerrar el crash EU (#4)
1. Conseguir el xex EU correcto (del que se generó `generated_eu/`).
2. Aplicar Opción A (re-codegen) o B (manual) de §3.3.
3. Validar Dragon Universe EU + START + demo battle EU (regresión).

### Fase D2 — `std::terminate` intermitente de `LaunchModule`
1. El try/catch ya existe; falta el throw exacto. Instrumentar: loggear la excepción + stack en el catch (ya hay minidump). 
2. Estrategia: pedir a la comunidad el `logs/` + minidump cuando ocurra (raro); mientras tanto, revisar el código de `LaunchModule` en `src/` y el SDK (`rex_app.cpp`) buscando throws posibles no esperados.

### Fase D3 — Higiene de estado
1. **Actualizar `docs/01_estructura/ESTADO.md`** (refleja 2026-08-18; debe decir v1.1.3, ISO, i18n auditada, crash EU pendiente).
2. Decidir el destino de `out/analysis/guest_image/build/` (dump_image es útil → documentarlo en `docs/04_herramientas/TOOLS.md` o dejar como está).
3. Limpieza de cvars/código muerto pendiente de la Fase 2.1 del roadmap (si no se hizo: `dbz3_enabled_mods`, `PrepareRegionData`, `analyze_bin_hd.py`).

### Fase D4 — Regresión del juego (validación de comunidad)
1. Con v1.1.3 publicado, pedir re-test en los issues abiertos (#1 framerate/idioma, #3 Vulkan Mac, #4 EU).
2. Establecer una **plantilla de reporte** (log + MD5 del xex + pasos) para que los usuarios den datos accionables.

---

## 6. ORDEN DE EJECUCIÓN SUGERIDO

1. ✅ **D1 (crash EU #4)** — RESUELTO (2026-09-10): `0x8215B378` registrada manualmente; builds compilados; boot EU validado. Pendiente validación del usuario.
2. ✅ **Responder issues** — #6 (PS2 soundtrack + save states) respondido; #4 respondido; #5 en espera de zamiba (PortForge, prometió "a day or two").
3. **D0** (instrumentación) — dump por MD5 + callstack en indirect call: sigue pendiente (útil para el próximo crash de este patrón).
4. **D2** (terminate) — pasivo (esperar logs), baja prioridad.
5. **D3** (higiene docs/estado) — ESTADO.md actualizado (2026-09-10); falta limpiar `out/analysis/codegen_backup_20260909` cuando se confirme el fix o mantenerlo como referencia del codegen EU probado.
6. **D4** (regresión comunidad) — tras publicar v1.1.4 (con el fix #4), re-test masivo con plantilla de reporte (log + MD5 del xex + pasos).

## 7. CRITERIOS DE ÉXITO

- Crash EU en Dragon Universe cerrado (validado por el usuario del issue #4).
- Respuesta dada a #6 (y seguimiento de #5).
- Instrumentación D0 operativa (dump por MD5 + callstack en indirect call).
- `ESTADO.md` al día (v1.1.3 + deuda conocida).
- 0 regresiones en US (validación en juego tras tocar el codegen EU).