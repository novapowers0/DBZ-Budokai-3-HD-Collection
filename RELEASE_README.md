# DBZ Budokai 3 HD Collection — Release ejecutable

Copyright (c) 2026 **NovaPowers**. Released under the MIT License.

Este paquete contiene el ejecutable recompilado de *Dragon Ball Z: Budokai 3 HD
Collection* (Xbox 360) con el launcher, el sistema de mods y el pipeline de
modelos. **NO incluye los archivos del juego** (copyright): debes aportar los
de tu copia legal.

## Contenido

- `dbz3.exe` — **el único ejecutable** (núcleo DUAL US/NA + EU/PAL + launcher +
  sistema de mods). Contiene ambas recompilaciones y elige la correcta según el
  `default.xex` que pongas.
- `rexruntime.dll`, `rexgpu-xenos.dll`, `amd_fidelityfx_dx12.dll` — runtime
  compilado en ISA **baseline universal** (SSSE3): funciona en CUALQUIER CPU
  x64, desde Core 2 (2006) hasta las más modernas. No hay que elegir variante.
  **Coste de rendimiento**: al no poder usar AVX2, en CPUs modernas el runtime
  es ~5-10% más lento en trabajo de host (medido en kernels calientes). En la
  práctica el juego se mantiene a 60 FPS en cualquier GPU discreta; si notas
  algo de lentitud en tu equipo, la release de respaldo **`v1.1.0-clasico`**
  (runtime clásico AVX2) está disponible en GitHub.
- `TracyClient.dll` — profiling (requerido por el runtime)
- `amd_fidelityfx_vk.dll` — upscaling FSR para el backend Vulkan
- `SPIRV-Tools-shared.dll` — utilidades SPIR-V (backend Vulkan)
- `mod center hd/` — toolkit de modding (catálogo + scripts + herramientas XDK)
- `RELEASE_README.md` — este archivo

> **Un solo archivo**: ejecutas `dbz3.exe` y listo. El núcleo es **dual**:
> contiene las recompilaciones US/NA y EU/PAL, y al arrancar identifica por su
> MD5 el `default.xex` que has puesto para usar el código correspondiente. Tanto
> el ejecutable US como el EU funcionan: cada uno corre su recompilación.

## Cómo instalar y jugar (paso a paso)

El paquete **NO incluye los archivos del juego** (copyright). Tienes que
aportarlos de tu **copia legal**. Haz esto:

1. **Descomprime** el ZIP en una carpeta, por ejemplo `C:\Juegos\DBZ3\`.
   Quedará `dbz3.exe` y los demás archivos del paquete.

2. **Aporta los archivos del juego** en una de estas dos disposiciones (la que
   prefieras; el launcher las detecta ambas automáticamente):

   **Opción A — junto a `dbz3.exe`:**

   ```
   C:\Juegos\DBZ3\
   ├── dbz3.exe            ← ya viene aquí
   ├── default.xex         ← TÚ lo pones aquí
   └── us/                 ← TÚ la creas (y/o eu/)
   ```

   **Opción B — dentro de una carpeta `assets`:**

   ```
   C:\Juegos\DBZ3\
   ├── dbz3.exe            ← ya viene aquí
   └── assets/             ← TÚ la creas
       ├── default.xex     ← TÚ lo pones aquí
       └── us/             ← TÚ la creas (y/o eu/)
   ```

3. **Dentro de `us/`** copia los archivos de datos de tu copia legal del juego:
   - USA: `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`, `data_ger.afs`,
     `data_ita.afs`, `data_spn.afs`, `data_usi.afs`, `data_yah.afs`,
     `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`, `lang_usa.afs`,
     `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
   - EU/PAL: los mismos archivos pero en una carpeta `eu/`.

4. **Copia el ejecutable del juego** como `default.xex` en la misma carpeta que
   `us/` (es decir, junto a `dbz3.exe` en la Opción A, o dentro de `assets/` en
   la Opción B; **nunca** dentro de `us/`).

   > **Puedes usar el ejecutable US/NA (`yae3_xenon.xex`) o el EU/PAL
   > (`yae3_xenon_eu.xex`)**: cada uno tiene su núcleo recompilado dentro del
   > paquete, y el launcher elige el correcto solo. La región PAL (assets `eu/`)
   > y el idioma se eligen en el launcher, así que tampoco pierdes nada por usar
   > un ejecutable u otro.

5. **Ejecuta `dbz3.exe`** (doble clic).

6. En la ventana del launcher elige **Región** (USA / EU PAL), **Idioma**,
    **Vídeo**, **Audio** e **Input**, y pulsa **Play**.

> Si algo falla, comprueba que `default.xex` y `us/` (o `eu/`) están en la
> MISMA carpeta, o ambos dentro de `assets/`, tal como en los dibujos del paso 2.

> Para extraer los archivos de tu **ISO legal** usa una herramienta tipo
> `extract-xiso` (lee el FATX de Xbox 360). Ver `baserom.md` del repositorio
> para los tamaños y checksums SHA-256 de cada archivo.

## Novedades de esta release

### v1.1.1 — Depurado + bases para Linux (2026-08-28)

- **Sin datos del juego ya no hay crash**: si falta `default.xex`, el juego te
  avisa con un mensaje claro de cómo colocar tus archivos (en vez de abrir la
  ventana y morir con un cierre raro).
- **Diagnóstico de arranque**: el log registra cuántos milisegundos tarda cada
  fase de arranque (si el launcher tarda en aparecer, el log dice dónde).
- **El MD5 del `default.xex` ya no usa CryptoAPI de Windows** (implementación
  portable) — primer paso del port a Linux, sin cambios de comportamiento.
- **Bases de Linux**: el launcher ya compila conceptualmente en Linux
  (detección de GPU, diálogos y lanzamiento de scripts protegidos por
  plataforma; en Windows nada cambia). Ver `docs/PLAN_LINUX.md`.
- **Proceso interno**: sincronización automática del repo (`sync_github.ps1`)
  y verificación del paquete antes de publicar (`verify_release.ps1`).

### v1.1.0 — Un solo ejecutable universal (2026-08-28)

- **Un único `dbz3.exe` para todo el mundo**: se eliminó el arrancador de
  variantes y las carpetas `dbz3_avx2\` / `dbz3_legacy\`. El runtime se
  compila ahora en ISA **baseline universal** (SSSE3) → el mismo paquete
  funciona en CUALQUIER CPU x64 (Core 2 de 2006 en adelante), sin elegir nada.
- **Adiós a los 0xC000001D de CPUs antiguas**: antes había que lanzar la
  variante "compatible"; ahora no hay variantes. Un solo archivo, una sola
  carpeta, un solo doble clic.
- **Adiós al falso positivo de antivirus**: ya NO se comprime el ejecutable con
  UPX (patrón típico de malware). Paquete más grande, cero sustos.
- Se mantienen todos los fixes de la v1.0.10 (Duelo/Start, botón PLAY con el
  ratón, EU/PAL, GPU dedicada en portátiles).

### v1.0.9 — Blindaje del pacing del guest (V-Sync) + centro de mods

- **Bug cerrado: "el juego va super rápido al desactivar el V-Sync"**. La causa
  era el worker de vblank del guest: con el cvar `vsync` OFF el vblank caía a
  ~1000 Hz y la lógica corría ~16x. Ahora el SDK **clampa el intervalo** en
  `graphics_system.cpp` (parche #13): el vblank del guest nunca puede ser más
  corto que un frame de 60 Hz, sea cual sea el estado del cvar → `vsync=false`
  es un no-op y el juego siempre corre a su velocidad. Se distribuye en
  `rexgpu-xenos.dll` (avx2 + legacy).
- **Centro de mods (pestaña Mods)**:
  - **Instalar mod desde un `.zip`**: botón "Instalar mod (.zip)..." → diálogo
    nativo, descomprime (PowerShell nativo, sin ventana) y normaliza el layout
    a `mods/<nombre>/`.
  - **Perfiles de mods**: combo para guardar/aplicar/borrar conjuntos de mods
    activos de una vez; "vanilla" desactiva todos (juego original).
  - **Abrir carpeta** por mod desde la lista.

### v1.0.8 — Nombres unificados + documentación bilingüe

- **`dbz3_core.exe` ahora es `dbz3.exe`**: el ejecutable dentro de `dbz3_avx2\`
  y `dbz3_legacy\` se llama igual que el lanzador de la raíz. Solo ejecutas el
  `dbz3.exe` de la raíz; el de dentro de la variante lo abre él solo.
- **README bilingüe**: el repositorio tiene `README.md` (español) y
  `README_EN.md` (inglés), con enlaces entre ambos.
- **Documentación del paquete pulida**: `README_PRIMER_ARRANQUE.txt` aclara los
  dos `dbz3.exe` para una primera instalación sin sorpresas.

### v1.0.7 — Fix del crash de la demo battle EU + núcleo dual + tamaño reducido

- **Arreglado el cierre al reproducirse la DEMO** (el modo "attract" que salta
  si dejas el menú "Press start" sin pulsar nada: al llegar a la batalla 3D el
  juego crasheaba con `0xC000001D` o *"Call to invalid or unregistered
  function"* en la variante EU/PAL). Causas: tres clasificaciones erróneas del
  recompilador sobre el código EU (punteros de función tratados como jump
  tables de un solo caso → instrucción UD2) y una función de tabla virtual que
  no se había compilado. Todo registrado con sus tamaños exactos y validado en
  la batalla DEMO completa.
- **Núcleo dual**: antes el paquete llevaba dos núcleos separados (US/NA y
  EU/PAL). Ahora `dbz3.exe` (el de cada carpeta de variante) es UN solo binario
  que contiene AMBAS recompilaciones y elige la correcta según el `default.xex`
  que pongas. Esto simplifica el paquete (2 variantes de CPU en vez de 4).
- **Más ligero**: el núcleo dual comprimido pasa de ~33,9 MB a ~7 MB por
  variante (UPX -9, verificado sin amenazas por Windows Defender).
- **Arranque fiable verificado**: al abrir el paquete sin tocar nada, se
  muestra el launcher con sus opciones y el juego NO arranca hasta pulsar Play
  (el `default.xex` se detecta solo; la región y el idioma se eligen en el
  launcher).
- **Diagnóstico reforzado**: en cada indirect call no registrado se registra el
  target, el `caller_lr` del guest y los registros r3/r4/r11; el crash handler
  vuelca el contexto y los registros guest — todo solo se loguea en un crash.

### v1.0.6 — Fix del cierre en la intro (0xC0000409 / función no registrada)

- **Arreglado el cierre al llegar a la intro del juego** que reportaban varios
  usuarios con `0xC0000409` y, en los logs, el mensaje *"Call to invalid or
  unregistered function at guest address 0x82292A58"*. Era una **función de
  despacho de tabla virtual** (thunk de vtable, offset +0x14) a la que el
  ejecutable EU/PAL llama de forma indirecta durante la intro y que **no estaba
  compilada** en el port recompilado. Se ha registrado con su tamaño exacto,
  regenerado el codegen y recompilado el núcleo EU/PAL. **Validado**: la intro
  pasa sin cierre (antes crasheaba ~1:30 tras el arranque).
- **Diagnóstico de arranque reforzado**: si una excepción no controlada ocurre
  al lanzar el juego (el cierre intermitente que también produce `0xC0000409`),
  ahora el registro (`logs/`) incluye **el mensaje de la excepción y el stack
  del hilo** para poder localizarla con precisión en futuras versiones.
- El núcleo US/NA también validado en la intro (sin cierre).

### Novedades acumuladas (1.0.5 EX → 1.0.6)

- **Controles (pestaña Input)**: el **teclado funciona de serie** (emula al
  mando; menús + combate). Puedes **remapear las 24 teclas** (campo "Keyboard
  (MnK) mapping", formato `Tecla`, comas = alternativas, `Shift+/Ctrl+/Alt+` =
  modificadores) y activar el ratón como stick derecho. Mando: selector
  XInput/SDL, **deadzone** y **rumble** con efecto real (sliders en la pestaña).
- **Velocidad del juego fija**: el juego corre SIEMPRE a su velocidad correcta
  (60 FPS lógicos, sincronizados con el vblank del guest). Ya no se puede
  "acelerar" accidentalmente.
- **Frame cap** (pestaña Video): limita la tasa de presentación de tu PC
  (60 = por defecto, fluido; **30 = media carga** recomendado para gráficas
  integradas; 0 = sin límite). NO cambia la velocidad del juego.
- **Presets de calidad por GPU** (pestaña Video, "Quality preset"): `Auto`
  detecta tu gráfica (nombre + VRAM) y aplica el perfil recomendado en cada
  arranque. Perfiles manuales: Low / Medium / High / Ultra. Las instalaciones
  con ajustes hechos a mano se conservan intactas (se marcan como "Manual").
- **Máquinas sin AVX2**: el arrancador `dbz3.exe` detecta tu CPU y usa la
  variante correcta (`dbz3_avx2\` para CPU modernas, `dbz3_legacy\` para las
  demás).
- **Arranque fiable**: el launcher ya no se queda en negro/No responde al
  abrir. La causa era la inicialización del mando SDL (que puede bloquearse
  con software de captura como RTSS/OBS); ahora se inicializa en segundo plano
  y el juego arranca al instante.
- **Cierre fiable (Alt+F4 / botón X)**: cerrar el juego ya no lo deja colgado
  en "No responde"; sale al instante.
- **Detección del ejecutable**: si pones el `default.xex` EU/PAL, el launcher lo
  detecta y arranca el núcleo EU/PAL correspondiente (igual con el US/NA); si
  pones un núcleo con el ejecutable equivocado, te avisa y bloquea Play para que
  no veas un cierre raro. También se detecta la carpeta de datos aunque solo
  tengas `eu/` (sin `us/`).
- **Launcher en tu idioma (y el juego también)**: el selector "Idioma" traduce
  TODO el launcher (español, inglés, italiano, alemán, francés; el resto usa
  inglés) **y condiciona el texto del juego** al mismo idioma.
- **PLAY siempre visible**: el botón PLAY es grande y verde, siempre en
  pantalla sin necesidad de hacer scroll, con un resumen de la configuración
  que se va a lanzar (región, motor, escala, efecto, idioma). El selector de
  región está en la barra inferior.
- **UI compacta**: toda la interfaz cabe en la ventana sin barras de
  desplazamiento (pestañas Video y Controles en columnas; las ayudas largas
  se muestran al pasar el ratón).
- **Presets visibles**: la pestaña Video muestra qué perfil de calidad está
  activo y a qué valores resuelve (p.ej. "Auto → Alta: 1x, MSAA ON...").

## Mods (WIP)

> 🚧 **Estado: en desarrollo (WIP).** El sistema de mods es experimental y puede
> cambiar. Úsalo con copias de seguridad.

Los mods se gestionan desde las pestañas **Mods**, **Texturas** y **Model Swap**
del launcher. **No modifican** los archivos del juego: aplican un overlay sobre
entradas concretas del AFS, así que cada mod pesa solo ~100 KB.

- **Swap de modelo** B3→B3 nativo: reemplaza el personaje completo (geometría +
  texturas) por otro del catálogo (183 personajes). Funciona en cualquier
  dirección, incluso si el bin nuevo es más grande que el slot original
  (mid-insert virtual).
- **Texturas**: extrae las texturas de un personaje a PNG editables, las
  editas y reconstruyes el mod.
- **Música** (`og_music`): reemplaza los AFS de audio por región.

## Estado de la release (WIP)

El modo historia y los modos alternos se han verificado en una pasada completa
**sin errores, crasheos ni fallos conocidos** con la configuración por defecto
(D3D12 + upscaling 2x + 60 FPS). El sistema de mods es la parte experimental:
los swaps y texturas funcionan, pero al ser personalizables, úsalos con
copia de seguridad de tus AFS.

## Bugs conocidos

- **Runtime universal (SSSE3) vs clásico (AVX2)**: el ejecutable universal es
  ~5-10% más lento en CPUs modernas que el runtime AVX2. Si lo notas en tu
  equipo, usa la release de respaldo `v1.1.0-clasico`.
- **Vulkan experimental**: el backend Vulkan funciona pero el render 3D es
  ~6.5x más lento que D3D12. Usa **D3D12** (por defecto).
- **Port de personajes PS2/IW→B3**: la inyección (geometría PS2 en la plantilla
  HD) funciona y da siluetas reconocibles; el port con topología PS2 exacta
  sigue en investigación (requiere reconstruir la estructura de dibujo; ver
  `docs/`).

## Historial de versiones

- **v1.1.1** (2026-08-28): depurado (sin datos → mensaje claro, marcadores de
  arranque), bases de Linux (launcher portable), proceso interno (sync +
  verificación de release).
- **v1.1.0** (2026-08-28): **un solo ejecutable universal** — runtime baseline
  (SSSE3) para cualquier CPU x64, sin variantes ni carpetas, sin UPX.
- **v1.0.10** (2026-08-28):
  - **Crash 0xC000001D en Duelo/Start arreglado** (codegen US): el dispatch de
    vtable `sub_820BB938` estaba mal clasificado como jump table de 1 caso →
    UD2 al entrar en combate. Ahora es una llamada indirecta real.
  - **Botón PLAY con el ratón arreglado**: un `default.xex` desconocido o de la
    otra región deshabilitaba el botón (el Enter lo sorteaba). Ahora solo se
    bloquea un xex de variante conocida y equivocada; el desconocido avisa pero
    no bloquea, y Enter respeta el mismo gate.
  - **Detección de GPU en portátiles Optimus**: se elige el adaptador con más
    VRAM dedicada (antes el primero no-software = integrada).
  - **Antivirus**: el paquete ya NO se comprime con UPX (los empacadores UPX
    dan falsos positivos de virus). Descarga más grande, sin sustos.
  - **Mensaje de error del bootstrap** más claro (ruta del log real
    `dbz3_legacy\logs` + causa del 0xC000001D en CPUs antiguas).
- **v1.0.9** (2026-08-26): centro de mods (instalar .zip + perfiles),
  VERSIONINFO, fix V-Sync blindado.
- **v1.0.8** (2026-08-26): i18n EN/ES/IT/DE/FR, UI compacta sin scrollbars.
- **v1.0.7** (2026-08-26): core dual US+EU, fix de la demo battle EU (crash
  0xC000001D).
- **v1.0.6** (2026-08-26): fix del cierre en la intro (0xC0000409 /
  0x82292A58), diagnóstico reforzado.

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Bandai Namco, Shueisha, Toei Animation ni ningún
titular de los derechos de Dragon Ball. No se distribuye ningún `.xex`, AFS ni
dato del juego. Los archivos del juego son de tu copia legal.

Repositorio: https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection
