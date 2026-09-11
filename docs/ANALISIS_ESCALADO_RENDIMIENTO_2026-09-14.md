# Análisis: escalado (FSR3/DLSS), rendimiento y estado del repo (2026-09-14)

Preguntas del usuario: (1) ¿es viable poner FSR3 o DLSS?; (2) hay reportes de
usuarios con falta de rendimiento; (3) si son cosas de mucho tiempo, publicar
los cambios actuales como nuevo release.

---

## 1. FSR3 / DLSS — viabilidad

### Lo que YA existe hoy en el SDK (0.10)
- **FSR1 (EASU + RCAS) y CAS: IMPLEMENTADOS DE VERDAD** (shaders vendorizados
  + `ffx_api`). Es el upscaling que usa el launcher (`present_effect=fsr|cas`).
  - `rexglue-sdk-0.10/src/ui/presenter.cpp:45` (cvars), `:1069-1147` (cadena
    de pases), `src/ui/d3d12/d3d12_presenter.cpp`.
- **FSR2/FSR3 "temporal": presente pero DEGRADADO a espacial.** El código existe
  (`DispatchTemporalUpscaler`) pero por frame hace `reset=true` y pasa el color
  como depth **y** como motion vectors, con `jitter=0`
  (`d3d12_presenter.cpp:165-192`; `vulkan_presenter.cpp:366-403`). El propio
  código lo advierte (`presenter.cpp:236-243`).
- **FidelityFX vendorizado**: fork `rexglue/FidelityFX-SDK`, SDK **1.1.3**,
  upscaler **FSR 3.1.4** (`cmake/rexglue_fidelityfx.cmake:52-53`).
- **Frame Generation: NO compilado** —
  `FFX_API_ENABLE_FRAMEGEN_PROVIDER OFF` (`cmake/rexglue_fidelityfx.cmake:107`).
- **DLSS/NGX/XeSS: AUSENTES** (grep vacío en todo el SDK).

### El bloqueador de fondo (común a FSR3-temporal, FSR3-FG y DLSS)
El renderer es **traducción/replay de comandos Xenos**, no un motor con G-buffer
propio. **No hay motion vectors ni jitter** en `src/graphics` (búsqueda: 1 solo
match irrelevante). Un upscaler temporal (FSR3/DLSS/XeSS/FG) necesita
**depth + motion vectors + jitter**; hoy no se capturan. Sin eso, `fsr2/fsr3`
son un alias caro de FSR1.

### Veredicto

| Objetivo | Viable ahora | Esfuerzo |
|---|---|---|
| FSR1 / CAS espacial | ✅ ya funciona | — |
| FSR3.1 upscaler temporal de verdad | ⚠️ | **semanas-meses** (exportar depth+motion+jitter) |
| FSR3 Frame Generation | ❌ | **muy alto** (FG off + proxy-swapchain + inputs temporales) |
| DLSS (SR/FG) | ❌ | **muy alto** (vendorizar NGX/Streamline + inputs temporales) |
| XeSS | ❌ | **muy alto** |

**Conclusión**: **NO** es viable a corto plazo. La parte "administrativa" (ffx_api)
ya está; el cuello real es **exportar los inputs temporales** desde
`src/graphics` al presentador. Es un proyecto de investigación grande, no una
tarea de release.

**Vía pragmática (sin tocar el SDK)**: usar lo que ya hay — escala interna
`draw_resolution_scale` 2x-3x + FSR1 + CAS + aniso. Es lo que el launcher ofrece.

### Mejora aplicada en esta sesión
El launcher cableaba `dbz3_fsr_sharpness` y `dbz3_cas_sharpness` pero **no los
exponía**. Ahora la pestaña **Escalado** muestra:
- FSR → slider **Nitidez RCAS** [0,2] (`present_fsr_sharpness_reduction`).
- CAS → slider **Nitidez adicional** [0,1] (`present_cas_additional_sharpness`).

Ambos son knobs **reales** de FSR1/CAS (a diferencia de `present_fsr_quality_mode`,
que solo afecta a fsr2/fsr3 y por eso queda oculto).

---

## 2. Rendimiento — reportes y estado

### Reportes revisados (issues del repo, 2026-09-02..09-10)
- **#1 (v1.0.4, Linux/AMD)**: *"frame cap roto: no fijaba 60/120/240"* y
  *"ralentizaciones fuera de Uncapped"*. **Ya abordado**: el `frame_cap` real
  del presentador (`d3d12_presenter.cpp:571-588`, cvar `frame_cap`, launcher
  `dbz3_frame_cap` con `SafeFrameCap`) y la velocidad del guest fija a 60 Hz.
- **#3 (v1.1.1, Mac/CrossOver)**: reporta **60 FPS bloqueados, ~4 ms de frame
  time** (rendimiento bueno). Sus 2 hallazgos: región EU por defecto (ya hay
  `ResolveRegion`) y un canal rojo en un splash (formato de textura 16-bit bajo
  D3DMetal, cosmético). El switch de backend que pedía **ya existe** en 1.1.2+
  (`dbz3_gpu_backend` → cvar `gpu_backend`).

### Palancas de rendimiento actuales
- Escala interna (`draw_resolution_scale_x/y`, 1..8) + presets por GPU
  (low/medium/high/ultra) en el launcher.
- FSR1/CAS/bilinear; MSAA 2x; anisótropo; `frame_cap` (0/15-1000); VRR.
- Backend **D3D12** (recomendado) vs **Vulkan** (experimental, ~6.5× más lento
  en `IssueSwap`; ver `docs/PLAN_1.1.1.md`).

### Conclusión de rendimiento
No hay un problema abierto y accionable en las versiones actuales: el reporte
duro era el `frame_cap` (ya resuelto) y el resto son equipos modestos (para eso
están los presets) o Vulkan (experimental). **No se identifica una optimización
grande pendiente** sin RE del renderer.

---

## 3. Qué se hace con esto
- **FSR3/DLSS**: se documenta como **fuera de alcance a corto plazo** (este doc).
  La vía práctica es la ya implementada (escala interna + FSR1/CAS), ahora con
  nitidez ajustable.
- **Rendimiento**: sin acción grande pendiente.
- **Release**: se publican los cambios/mejoras acumulados (launcher de mods,
  Model Swap HD↔HD, aviso ISO, limpieza de mods, mod de textura de referencia).
