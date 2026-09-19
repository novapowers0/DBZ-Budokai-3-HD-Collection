# DBZ Budokai 3 HD Collection

[English](README_EN.md) · Español

Un port de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) a PC, hecho con
el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). El código PowerPC del
juego se recompila de forma estática y queda integrado en un solo ejecutable
con su propio launcher y sistema de mods. No es un emulador: el juego corre
nativo en Windows.

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Plataforma](https://img.shields.io/badge/Plataforma-Windows-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Licencia](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Estrellas](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)

| | |
|---|---|
| Jugadores | 1–2 (versus) |
| Plataforma | Windows |
| Motor | Xbox 360 (ReXGlue SDK) |
| Género | Lucha 3D |
| Versión | v1.2.2 EX |

Copyright (c) 2026 **NovaPowers**. Licencia MIT (ver `LICENSE`).

---

## Aviso legal

Este proyecto no incluye el juego. Para jugar necesitas aportar los archivos de
**tu copia legal**: el ejecutable (`default.xex`) y los `data_*.afs` de la
región que uses. Es la convención habitual en la comunidad de recompilación
estática (por ejemplo `mstan/DragonBallZBuusFuryRecomp`): se distribuye el
código y el launcher, no el contenido del juego.

- En `baserom.md` está la identidad exacta de cada archivo (tamaños y
  checksums SHA-256) y cómo extraerlos de tu ISO.
- El código recompilado (`generated/`) se genera **localmente** a partir de tu
  `.xex` y no se sube al repositorio.

Es un proyecto no oficial, sin ánimo de lucro, de investigación y preservación.
No tiene relación con Bandai Namco, Shueisha, Toei Animation ni ningún titular
de los derechos de Dragon Ball.

---

## Cómo jugar

Tienes dos formas de aportar los datos del juego: con la carpeta extraída o
directamente con el ISO. Las dos se detectan solas, no hay que configurar nada.

**Opción A — la carpeta extraída (para usar mods)**

1. Descarga el ZIP de **Releases** y descomprímelo donde quieras.
2. Pon junto a `dbz3.exe` el `default.xex` y la carpeta `us\` (o `eu\`). Valen
   estas dos disposiciones:

   ```
   C:\Juegos\DBZ3\                C:\Juegos\DBZ3\
   ├── dbz3.exe                   ├── dbz3.exe
   ├── default.xex                └── assets\
   └── us\ (y/o eu\)                  ├── default.xex
                                     └── us\ (y/o eu\)
   ```

   **También vale el volcado tal cual del disco** (con la carpeta `DBZ3\` y su
   ejecutable sin renombrar): el launcher busca el ejecutable de Budokai 3 solo.

   ```
   C:\Rom\Budokai HD Collection\
   ├── dbz3.exe
   └── DBZ3\                      ← tal cual sale de tu ISO
       ├── yae3_xenon.xex
       └── us\ (y/o eu\)
   ```

3. Ejecuta `dbz3.exe`. El launcher comprueba qué hay y, si falta algo, te lo
   dice. Puedes buscar la carpeta de datos con "Seleccionar carpeta de datos...".
4. Elige **Región**, **Idioma**, **Vídeo** y **Audio** y pulsa **Play**.

**Opción B — el ISO directamente (para jugar sin extraer nada)**

Deja el `.iso` del juego junto a `dbz3.exe` (o usa "Seleccionar ISO..." en el
launcher). El launcher lo detecta, saca el ejecutable de Budokai 3 del disco
(`DBZ3\yae3_xenon.xex`, unos pocos MB) y monta el resto directamente desde la
imagen: no hace falta descomprimir ni copiar los AFS. La región se detecta sola
a partir del ejecutable del propio disco.

> Funciona con el ISO original completo (el que trae el menú de la HD Collection
> en la raíz): el launcher coge el ejecutable de Budokai 3 de dentro del disco,
> no el menú.

> Los mods necesitan la carpeta extraída (opción A). En modo disco se juega
> tal cual del ISO.

> **Un solo `dbz3.exe`**: desde v1.1.0 no hay variantes. Un único ejecutable
> universal (runtime baseline SSSE3) que funciona en cualquier CPU x64 (Core 2
> 2006 en adelante), con las recompilaciones USA y EU dentro y autodetección
> del ejecutable que pongas (por tamaño y checksum, sin importar cómo se llame
> ni dónde esté).

### Qué archivos necesitas (opción A)

Solo el ejecutable y los datos de tu región, no toda la ISO:

- **USA**: a `us\` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
  `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
  `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
  `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
- **EU/PAL**: los mismos archivos en `eu\`.

Todo puede ir junto a `dbz3.exe` o dentro de `assets\` (con `default.xex`).
Puedes verificar los archivos contra `baserom.md`.

Para extraerlos de tu ISO legal usa una herramienta tipo `extract-xiso` (lee
el sistema de archivos FATX de Xbox 360).

---

## Estructura del repositorio

```
DBZ-Budokai-3-HD-Collection/
├── default.xex               # NO incluido. Ejecutable del juego (USA o EU)
├── us/                       # NO incluido. Datos región USA
├── eu/                       # NO incluido. Datos región EU/PAL
├── src/                      # Recompilador + launcher + sistema de mods
│   ├── main.cpp              #   entrada, ventana, gestor de crash
│   ├── mods.cpp              #   sistema de mods (overlay AFS)
│   ├── launcher/             #   interfaz del launcher + pipeline de modelos
│   └── ingame/               #   menú in-game
├── generated/                # NO incluido. Código derivado de tu .xex
├── mod center hd/            # Herramientas Python de modding (propias)
├── awo_tools/                # Herramientas de RE del formato AWO/AWG
├── patches/                  # Parches del ReXGlue SDK (ver su README)
├── mods/                     # Mods de usuario (vacía)
├── tools/                    # xbcompress/xbdecompress + utilidades
├── docs/                     # Documentación completa
├── CMakeLists.txt            # Build
├── baserom.md                # Archivos del juego requeridos + cómo extraerlos
└── LICENSE                   # MIT (NovaPowers)
```

---

## Regiones USA / EU

Los ejecutables USA (`yae3_xenon.xex`) y EU (`yae3_xenon_eu.xex`) son builds
distintas, no dos copias iguales, y el núcleo dual incluye la recompilación de
cada uno. El launcher identifica cuál has puesto por su checksum y usa el
código correcto; si no coincide, te avisa y bloquea Play para que no acabes con
un cierre raro en pantalla.

No hace falta que el archivo se llame `default.xex` ni que esté en la raíz: el
launcher lo busca por **tamaño + checksum** (los nombres típicos son
`yae3_xenon.xex` / `yae3_xenon_eu.xex`) y lo prepara él solo. Si pones el menú de
la HD Collection en vez del ejecutable de Budokai 3, te lo dice y bloquea Play.

La región de **datos** (carpeta `us\` o `eu\`) y el **idioma** se eligen en el
launcher y no dependen del ejecutable. El guardado es compartido entre
regiones.

---

## Mods

Los mods viven en `mods\<nombre>\` (la carpeta se distribuye vacía) y reemplazan
entradas del AFS por overlay, sin tocar los AFS originales:

```
mods/<mod>/us/data_cmn.afs/<entrada>/geom.bin   # override de una entrada
mods/<mod>/manifest.txt                         # metadatos (nombre, autor...)
mods/<mod>/.disabled                            # si existe, el mod está OFF
```

Se gestionan visualmente desde el launcher (pestañas **Mods**, **Texturas** y
**Model Swap**) o con las herramientas de `mod center hd/`. Guías en
`docs/02_mods/`.

### Swaps de modelo en cualquier dirección (mid-insert virtual)

Un swap B3→B3 es un override por entrada (~100 KB) que se sirve en el slot
destino aunque el bin sea **más grande** que el slot original: el runtime
presenta al juego una tabla AFS consistente (la entrada crece en su sitio y las
siguientes se desplazan) y traduce las lecturas. Así funciona, por ejemplo,
meter a Goten en el slot de Krillin.

Esto requiere el **parche del ReXGlue SDK** incluido en `patches/` (ver
`patches/README.md`).

### Qué hace el launcher

- **Video**: resolución interna, región, idioma, VRR, frame cap (0 = sin tope),
  presets de calidad por GPU.
- **Upscaling**: FSR / CAS.
- **Audio**: volúmenes maestro / música / efectos / voz.
- **Input**: teclado y mando (XInput), remapeo de teclas, deadzone, vibración.
- **Mods**: activar/desactivar mods y editar su manifest.
- **Texturas**: extraer texturas a PNG, editarlas y reconstruir el mod.
- **Model Swap**: swap nativo B3→B3 (catálogo de 183 personajes).
- **Dev**: contador de FPS y diagnóstico GPU, todo OFF por defecto.

---

## Compilar desde el código

Necesitas un compilador C++23, CMake ≥ 3.25 y el
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (`REXSDK_DIR` o una
carpeta `rexglue/` junto al proyecto).

> Aplica primero los parches del runtime (`patches/`) sobre tu copia del SDK,
> tal y como explica `patches/README.md`, y recompila el runtime. Sin ellos los
> swaps con bins más grandes que el slot no funcionan.

```
git clone --recurse-submodules https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection.git
cd DBZ-Budokai-3-HD-Collection

# 1) pon tu .xex legal en default.xex
# 2) regenera el código derivado del xex
cmake --build out/build/win-amd64-release --target dbz3_codegen
# 3) compila
cmake -S . -B out/build/win-amd64-release
cmake --build out/build/win-amd64-release
# 4) ejecuta
out\build\win-amd64-release\dbz3.exe
```

El código recompilado (`generated/`) se deriva de tu `.xex` y no se sube (ver
`generated/README.md` y `.gitignore`). La estructura del paquete de release la
monta `tools/make_release.ps1`.

---

## Estado

| Técnica | Estado |
|---|---|
| Swap nativo B3→B3 (override ~100 KB) | Funcional en cualquier dirección (bins > o < slot) |
| Mod de texturas B3 HD | Funcional (override por entrada, ~118 KB) |
| 2+ mods de modelo/textura simultáneos | Funcional (mid-insert virtual) |
| Mod de música (og_music) | Funcional |
| Jugar desde el ISO (modo disco) | Funcional (juego base; mods requieren carpeta) |
| Núcleo dual USA/EU (un solo binario) | Funcional (validado en juego) |
| Port PS2→HD | En investigación; requiere reconstrucción completa |
| Port de personajes IW→B3 | Descartado (Janemba fracasó, archivado) |

---

## Novedades de v1.2.4

- **Volumen real en el launcher**: el slider "Volumen general" y el nuevo
  interruptor **"Silenciar todo el audio"** ahora SI hacen algo. Antes escribian
  una variable que el runtime no reconocia, asi que no tenian efecto y el juego
  nunca se podia silenciar desde la interfaz.
- **Aviso de nueva version**: al abrir, el launcher consulta la ultima release
  publicada en GitHub y, si hay una mas nueva, muestra **"Nueva version
  disponible: vX"** con un boton para descargarla (nunca bloquea PLAY). Se puede
  desactivar en la pestana Dev.
- **Limpieza de controles muertos**: se quitaron el slider de **Gamma** y los de
  **musica/SFX/voces** (el juego mezcla todo el audio en una sola pista: no son
  separables). "Restablecer valores" ahora restaura tambien VRR y Texturas HD.
- Base: v1.2.3 (contador de FPS en partida + logs limpios + Texturas HD WIP).

## Novedades de v1.2.3

- **Contador de rendimiento en partida**: cvar `dbz3_perf_logging` (por defecto ON)
  escribe una linea cada 5 s en el log con los FPS reales del juego y el peor frame
  del intervalo, medidos en el swap del guest. Sirve para diagnosticar "va lento"
  con datos (tambien funciona en pruebas sin ventana visible).
- **Log de overrides AFS silenciado**: ya no escribe 2 lineas con la ruta completa en
  CADA lectura del AFS (miles de lineas por sesion). El detalle vuelve activando
  `dbz1_diag_logging` en la pestana Dev.
- **Texturas HD (WIP, OFF por defecto)**: filtro interno tipo emulador que reescala
  las texturas del juego en runtime (x2/x3/x4, generando tambien la cadena de mips),
  **sin tocar los ficheros del juego ni su memoria**. Se nota en la intro, pero
  provoca tirones al cargar texturas nuevas, asi que viene **desactivado por
  defecto** como experimental. Se elige en Video -> "Texturas HD (WIP)".
- Base: v1.2.2 EX (auto-deteccion del ejecutable + arreglos del modo ISO).

## Novedades de v1.2.2 EX

- **El launcher encuentra el ejecutable del juego esté como esté**: ya no hace
  falta renombrar nada a `default.xex` ni tenerlo en la raíz. Lo busca por
  **tamaño + checksum** en la carpeta que elijas (y en las típicas `DBZ3\`,
  `assets\`, `assets\DBZ3\`) y lo prepara él solo en una caché interna
  (`user_data\xex_cache\`), sin escribir nada en tu carpeta de juego.
- **Arreglado el arranque con el volcado del disco original** (el caso «pulso
  Play y no pasa nada»): antes se arrancaba el menú de la HD Collection de la
  raíz del disco, que no existe en el núcleo de Budokai 3 y moría con un error
  críptico. Ahora se usa el ejecutable correcto (`DBZ3\yae3_xenon.xex`) y se
  monta la carpeta `DBZ3\` como unidad del juego.
- **Modo disco (ISO) con ISO original completo, validado**: se extrae el
  ejecutable de Budokai 3 de dentro del disco (no el menú) y los datos
  (`DBZ3\us\`, `DBZ3\eu\`) se resuelven solos. Se corrigió además un fallo de
  normalización de rutas que impedía leer los datos desde el disco.
- **Si tu carpeta no puede arrancar, se usa el ISO que tengas al lado**: con el
  volcado del disco tal cual (carpeta `us\` + el menú como `default.xex`) y el
  `.iso` junto a `dbz3.exe`, el launcher salta solo al disco y arranca.
- **Mensajes claros si el ejecutable no es el correcto**: el menú de la HD
  Collection se detecta y se explica; un ejecutable desconocido avisa pero no
  bloquea (puede ser un dump modificado).
- **Arreglado un fallo de configuración**: si tu carpeta o tu ISO tienen `\` en
  la ruta, el `dbz3_user.toml` se guardaba mal y se perdían los ajustes en cada
  arranque (`unknown escape sequence`). Ahora se escapa correctamente.
- **Log de diagnóstico del arranque**: se registran la ruta del ejecutable, su
  tamaño y checksum, el estado y la carpeta de datos elegidos.

## Novedades de v1.2.1

- **Fix de un crash al cerrar el launcher tras usar Model Swap o Texturas**: el
  hilo del pipeline Python quedaba sin unir y al destruir el launcher se llamaba
  a `std::terminate()`. Ahora se une correctamente al cerrar.
- **Nitidez de FSR aclarada**: la etiqueta del slider estaba invertida (0 es más
  nítido, 2 más suave).
- **La lista de mods se refresca sola** al terminar un swap/textura (el mod nuevo
  aparece sin pulsar "Refrescar"); el botón "Restablecer valores" también la
  actualiza.
- **Robustez**: lectura de la carpeta de texturas sin excepciones.

## Novedades de v1.2.0

- **Centro de mods renovado (QoL + visual)**: lista cacheada, buscador (nombre,
  autor, origen, tipo), botones Activar todos / Desactivar todos / Refrescar /
  Abrir carpeta, badges de tipo con color y filas alternas.
- **Model Swap B3 HD↔HD pulido**: desplegables de personaje con buscador (183
  personajes, con `[bin N]` y aviso `[NO JUGABLE]`), tarjeta de vista previa,
  aviso/bloqueo si origen==destino, y el mod generado se nombra con los nombres
  del catálogo (p. ej. «Cell Forma 2 en Krillin»).
- **Nitidez ajustable en Escalado**: sliders de nitidez RCAS (FSR) y nitidez
  adicional (CAS).
- **Modo disco (ISO)**: aviso explícito en Mods y Model Swap (los mods no
  aplican al jugar del `.iso`); el botón de swap se deshabilita.
- **Notas de escalado/rendimiento**: FSR3/DLSS no son viables a corto plazo (el
  renderer no expone motion vectors/jitter); FSR1/CAS sí. Ver
  `docs/ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14.md`.

## Novedades de v1.1.3

- **Selector de fuente siempre visible**: botones "Carpeta extraida" / "ISO
  (.iso)" en el launcher para elegir el origen en cualquier momento.
- **Detección de DBZ1**: si pones el xex de *DBZ Budokai HD Collection* (proyecto
  hermano), el launcher bloquea Play y avisa "usa el launcher dbz1.exe" (antes
  crasheaba).
- **i18n completa auditada**: 0 cadenas sin traducir en ES/EN/IT/DE/FR.
- **Preparado para usuarios no técnicos**: mensajes accionables y sugerencias
  al elegir la carpeta equivocada.
- **Pulido**: `-Wall -Wextra` 0 warnings, código muerto eliminado, empaquetador
  más estricto (rechaza residuos de ejecución en el ZIP).

## Novedades de v1.1.2

- **Fix crash en Dragon Universe / START (núcleo EU)**: función de despacho
  `sub_820F2398` no registrada → extraída y registrada como `dbz3eu_sub_820F2398`.
- **Regiones incompletas**: `ResolveRegion()` auto-cae a `us/` o `eu/` según lo
  que exista; datos solo-EU o solo-US funcionan sin tocar nada.
- **Backend Vulkan de verdad**: cvar `gpu_backend` añadido al SDK y conectado a
  `LoadGpuPlugin`; antes la elección del launcher se ignoraba (siempre D3D12).
- **Pulido**: builds sin warnings, traces temporales eliminados, footer muestra
  "Japanese" correctamente.
- **Modo disco**: juega directamente desde el `.iso` sin extraer nada (v1.1.2).

---

## Créditos

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — herramientas de
  recompilación.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — referencia de la
  API del SDK (solo referencia, no es base ni copia de código).
- Comunidad de modding de Budokai — herramientas y modelos de referencia.
- **NovaPowers** — autor del launcher, el sistema de mods y las herramientas.