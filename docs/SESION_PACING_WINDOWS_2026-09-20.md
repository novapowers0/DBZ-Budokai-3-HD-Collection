# Pacing Windows tras la corrección Linux (2026-09-20)

> Origen: tras arreglar el pacing Vulkan de Linux (`frame_cap` + FIFO por
> defecto, ver `docs/LINUX.md`), se revisó si esa lección aplicaba a la build
> Windows (D3D12). **Conclusión: no había nada que importar.** Este documento
> deja registrado el análisis, qué se descartó y por qué el código queda igual.

## Resumen

- Linux (Vulkan) **sí** tenía un bug real: `IMMEDIATE`/`MAILBOX` permitían un
  bucle de presentación sin límite y el contador de Steam medía presents del
  swapchain (cientos/miles de FPS). Se corrigió con FIFO por defecto y
  `frame_cap` en `vulkan_presenter.cpp`.
- Windows (D3D12) **no** sufre ese bug. No se cambió ni una línea.
- Se consideró y **se descartó** mover el sleep de `frame_cap` a justo antes de
  `Present`. Motivo abajo.

## Por qué el bug de Linux no aplica a D3D12

1. **No hay selección de present mode.** `IMMEDIATE`/`MAILBOX` son conceptos de
   swapchain Vulkan. El presenter D3D12 usa siempre `Present(0)` con
   `DXGI_SWAP_EFFECT_FLIP_DISCARD` (y `ALLOW_TEARING` si VRR está activo,
   `d3d12_allow_variable_refresh_rate_and_tearing`).
2. **La presentación la marca el guest, no un bucle libre.** En partida el
   `D3D12CommandProcessor::IssueSwap` de `rexgpu-xenos` pide el frame al ritmo
   del guest (60 Hz fijos, cvar `vsync` blindado). No hay un repaint continuo
   que dispare presents sin tope.
3. **El `frame_cap` real ya existía.** `PaintAndPresentImpl` ya aplica
   `frame_cap` (`dbz3_frame_cap`, default 60 en juego) durmiendo hasta el slot
   de 1/FPS. Es exactamente el equivalente del fix de Linux.

Por tanto, un overlay en Windows (Steam/DXGI) no puede observar el mismo
"contador inflado" que en Linux.

## Cambio considerado y descartado

Se evaluó mover el sleep de `frame_cap` desde el inicio de
`PaintAndPresentImpl` a justo antes de `IDXGISwapChain::Present`, imitando la
posición del fix Vulkan.

**Se descartó**:

- **No cambia la cadencia observable.** El throttle es de tasa fija en un bucle
  serializado; adelantarlo o atrasarlo da los mismos presents/segundo.
- **Empeora la latencia.** Con el sleep al principio, el frame se construye,
  se envía y se presenta de inmediato. Con el sleep justo antes de `Present`, se
  construye y envía el frame, y **después** se espera; eso añade retardo entre
  el envío del command list y la presentación.
- En Vulkan la position importa porque `vkQueuePresentKHR` es la frontera que
  interceptan MangoHud/Steam; en D3D12 el equivalente conceptual (`Present`) ya
  queda tras el throttle sin necesidad de moverlo.

El código de `d3d12_presenter.cpp` (SDK y `github/patches/`) queda **idéntico**
al que ya se publicó en v1.2.6.

## Qué no se puede transferir de Linux

- **MangoHud es un overlay Linux** (`vkQueuePresentKHR`). No aplica a Windows;
  no usarlo como prueba de la build D3D12.
- **FIFO** es una decisión exclusiva del swapchain Vulkan.
- El **contador de un overlay mide presents del host**, no swaps lógicos del
  guest. Para rendimiento del juego, la referencia fiable es el diagnóstico
  interno `dbz3: perf fps=... frames=... max_frame_ms=...` (`dbz3_perf_logging`,
  tab Dev), no el overlay.

## Notas de diagnóstico en Windows

- **En partida**: `frame_cap=60` en juego (lo fija el launcher). No hay bucle
  libre.
- **En el launcher**: `frame_cap` se mantiene a **0/uncapped** a propósito
  (`settings.cpp`); es lo que evita el cuelgue a >60 Hz documentado en el
  histórico. Si el contador de Steam muestra FPS altos **en el launcher** (menú
  de configuración), es esperado y no afecta al juego.
- `fg=0/1` en la línea `perf` distingue "ventana sin foco" (Windows/DWM limita a
  la mitad, 60→30) de "va lento" real.

## Resultado

- **Sin cambios de código en Windows.**
- **Sin recompilación y sin tocar el release v1.2.6** (el exe y las DLL siguen
  siendo los publicados).
- Documentación: este fichero + la sección "MangoHud y Steam FPS" de
  `docs/LINUX.md`.
