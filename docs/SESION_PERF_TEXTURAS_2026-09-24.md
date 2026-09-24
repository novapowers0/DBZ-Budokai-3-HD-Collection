# Sesión 2026-09-24 — La mejora de texturas deja de hundir los FPS (v1.2.8.2)

> Seguimiento de los reportes de **bajones de FPS con "Mejora de texturas
> (experimental)"** activada (logs de SSGPrinceVegeta, RTX 5090/9950X3D, en
> `Logs SSGPrinceVegeta/parte 4/`). Camino del feature:
> `docs/07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md`.

## 1. El reporte

- `parte 4/dbz3_036.log` (launcher) y `dbz3_037.log` (partida). Config activa
  (leída de la propia línea de arranque): `internal_scale=3x`, `msaa=true`,
  `hd_tex=3x`, `vrr=true`, `fsr=quality`, `aniso=5`, backend D3D12, salida de
  audio VB-Audio Virtual Cable. El gancho de área acepta `1024x1024` y
  `2048x512` → `dbz3_hd_texture_max_texels` = **1048576 (Alto)**, no el default.
- Síntoma: el presentador (`[core]`) sigue a 60, el swap del guest (`[gpu]`)
  empieza a ~59 y **se degrada hasta 31** conforme el contador `upx` sube
  (16→48→71→109→310→534→…→**665**), se queda plano en **~31 fps con
  `max_frame_ms` 34-38** (distribución estrecha: nada de picos) y **recupera
  ~55 fps tras perder/recuperar el foco** (22:02:53-57) sin que `upx` cambie.
- `parte 3` (19/09, `hd_tex=4x`): FPS **oscilando 30/60**. `parte 2` (v1.2.1) es
  anterior a `upx`/`fg=`. ⇒ el fenómeno **no es regresión** de una versión.

## 2. La reproducción (y por qué no era la carga)

Tres sesiones locales con **la configuración exacta** del reporter
(`dbz3_hd_textures=3`, `dbz3_hd_texture_max_texels=1048576`, escala 3x + MSAA) en
una **RTX 4070 SUPER** (más lenta que la 5090):

| Run | fps mín/máx | `upx` final | `fmt=22` (escena 3D) |
|---|---|---|---|
| `dbz3_248` | 59,8 / 60,2 | 48 | no |
| `dbz3_249` | 59,6 / 60,1 | 194 | no |
| `dbz3_250` | 59,2 / 60,2 | 328 | no |

- **No se reproduce**: 60,0 fps constantes con `max_frame_ms` 19-20.
- Descartado el *thrash* de la caché de texturas: forzando
  `texture_cache_memory_limit_soft=128; _hard=256`, `upx` sube 153→163 a ~1,5/s
  y sigue a **60,0 fps**.
- ⚠️ **Los runs locales nunca llegan a la demo 3D** (`fmt=22`, resolve de
  profundidad, que sí tiene el log del reporter): la automatización de teclas
  (`tools/press_key.ps1`) **no navega los menús SDL3** del juego. La comparación
  de escenas no es 1:1; lo comparable es la **mecánica** del feature.

## 3. La causa

`D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl`
(`src/graphics/d3d12/texture_cache.cpp`): cuando la carga trae el nivel 0,
regenera **la cadena de mips completa** con `UpscaleTextureData` nivel a nivel.
Cada nivel cuesta **dos barreras + dos descriptores de un solo uso + un cambio de
pipeline + un dispatch**, todo **en serie** dentro del command list.

Hay texturas que el juego **reescribe cada fotograma** (vídeo de la intro,
render targets de efectos). Con el feature activo se regeneran 12 niveles por
recarga ⇒ **~1 textura re-escalada por fotograma** (el `upx` del log) ⇒ coste de
**comandos/CPU**, no de GPU:

- Explica que **una GPU más potente no ayude** (y por qué no se ve en el uso de
  GPU ni en la VRAM).
- Explica el `max_frame_ms` plano (coste uniforme, sin picos).
- La guardia existente (`UpscaleBudgetAllows`) acota los **concesión de texturas
  nuevas** (24 por 0,5 s + 3 s de pausa), pero **no tocaba las recargas** de una
  key ya concedida, que es el caso del reporter (identidades repetidas).

## 4. El arreglo

En la carga del nivel 0 se distingue el relleno **completo** del relleno
**abarato**:

1. **Recarga solo-base sobre el MISMO recurso** (`load_mips == false` y la cadena
   ya se generó sobre ese `ID3D12Resource*`).
2. **Recarga crónica**: la misma identidad 4+ veces en 1,5 s (contador por key).

En ambos casos la textura es **dinámica** y solo se regenera **el nivel 0**:

- La imagen visible es exacta (el nivel 0 es la textura completa).
- Los mips se conservan de la última generación completa (solo afectan a la
  minificación; un fotograma de desfase no se ve).
- `upscale_chain_resources_` garantiza que, **tras un desalojo** (recurso nuevo,
  mips sin inicializar), se regenere la cadena entera.
- Las texturas **estáticas** siguen escalándose con **toda su cadena**, igual que
  antes. Se avisa una vez por identidad
  (`dbz3: upscale textura dinamica WxH fmt=F mips=M - solo nivel 0 por recarga`).

⚠️ **No se puede "des-conceder"** el factor de una textura ya escalada: el
recurso Nx existe y el camino de subida lee el mismo factor (si cambiara, se
rellenaría 1x en un recurso Nx → basura). La decisión de **tamaño** sigue siendo
estable por key; lo que se abarata es el **relleno**.

## 5. Diagnóstico nuevo en la línea `perf`

```
dbz3: perf fps=60.0 frames=300 window=5.00s max_frame_ms=20.1 fg=1
       cfg=scale:3x3 msaa:true hdtex:3 area:1048576 min:16 aniso:5 upx=137 upx_dyn=0 texload=602
```

- `cfg=` ajustes que más pesan (escala interna X/Y, MSAA, mejora de texturas,
  área máxima, tamaño mínimo, anisotrópico), leídos del **registro compartido**
  (`rex::cvar::GetFlagByName`) cada ventana de 5 s.
- `upx_dyn=` recargas de texturas dinámicas regeneradas a nivel 0 (acumulado).
- `texload=` cargas de textura de la ventana. En la intro local sale
  **600-1055 por 5 s (120-210/s)**: el juego es un *streaming* de texturas muy
  agresivo, y por eso el feature tiene que ser barato en recargas.

Con esto, **un solo log de usuario** dice qué tiene configurado y si el juego
está re-escalando dinámicas: se cierra el caso en una ronda.

## 6. Validación

- **Smoke test del camino nuevo** (build temporal con el umbral en
  `count >= 1`, para que lo ejecutara *cada* textura con mips): `upx_dyn` = 114,
  aviso por identidad por textura, **0 errores**, 60 fps, `max_frame_ms` 19-20.
  Prueba que el relleno a nivel 0 no rompe el recurso ni el presentador.
- **Build final** (`count > 3`) con la config del reporter: 3 sesiones,
  **60,0 fps**, `max_frame_ms` 19,2-20,4, **0 errores/avisos**, `upx_dyn=0`
  (en local no se alcanza el escenario del reporter), `texload` 600-1055/5 s.
- `verify_release.ps1 -Version v1.2.8.2` = **VERIFICACION OK**.

## 7. Release

- `src/version.rc` → `1.2.8.2` (misma convención: seguimiento del mismo PATCH
  sube BUILD). Release **Latest** con zip Windows (**22.147.958 B**) + tarball
  Linux de la CI.
- DLL canónica: `rexgpu-xenos.dll` **6.346.240 B**, `rexruntime.dll`
  **10.910.720 B** (baseline SSSE3).
- PortForge: `defaultVersion 1.2.8.2` (visibles 1.2.8.2 / 1.2.8.1 / 1.2.8; la
  1.2.7 pasa al archivo en `portforge/archive/`).
- Parches del SDK actualizados (`patches/rexglue-sdk/.../{texture_cache.cpp,
  texture_cache.h,d3d12/command_processor.cpp}`); son ficheros D3D12, así que la
  build Linux (Vulkan) no cambia funcionalmente.
- `github/RELEASE_README.md` + `release-stage/RELEASE_README.md`: sección nueva
  de v1.2.8.2.

## 8. Pendiente

- **Confirmar con el log del reporter** (activando "Registro de rendimiento"):
  si `upx_dyn` sube y los FPS van bien → cerrado; si `upx_dyn` sube y sigue
  lento → el coste está en el nivel 0 y habría que abaratar el dispatch; si
  `upx_dyn=0` y sigue a 31 → **otra causa** (pedir CPU/GPU: relojes, temperaturas,
  % de carga, y el `dbz3_user.toml`).
- Llegar a la **demo 3D** en local sigue sin ser posible por automatización
  (las teclas no llegan a SDL3): si hiciera falta, hay que hacerlo a mano.
- La demo 3D en local medía 60 fps a 2x/3x + MSAA (2026-09-19, sin `hd_tex`), así
  que sigue pendiente medir `hd_tex` en la demo con el arreglo.
