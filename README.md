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
| Versión | v1.1.3 |

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

3. Ejecuta `dbz3.exe`. El launcher comprueba qué hay y, si falta algo, te lo
   dice. Puedes buscar la carpeta de datos con "Seleccionar carpeta de datos...".
4. Elige **Región**, **Idioma**, **Vídeo** y **Audio** y pulsa **Play**.

**Opción B — el ISO directamente (para jugar sin extraer nada)**

Deja el `.iso` del juego junto a `dbz3.exe` (o usa "Seleccionar ISO..." en el
launcher). El launcher lo detecta, saca el `default.xex` del disco (solo ese
archivo, unos pocos MB) y monta el resto directamente desde la imagen: no hace
falta descomprimir ni copiar los AFS. La región se detecta sola a partir del
ejecutable del propio disco.

> Los mods necesitan la carpeta extraída (opción A). En modo disco se juega
> tal cual del ISO.

> **Un solo `dbz3.exe`**: desde v1.1.0 no hay variantes. Un único ejecutable
> universal (runtime baseline SSSE3) que funciona en cualquier CPU x64 (Core 2
> 2006 en adelante), con las recompilaciones USA y EU dentro y autodetección
> del `default.xex` que pongas.

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