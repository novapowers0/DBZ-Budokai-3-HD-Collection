# DBZ Budokai 3 HD Collection

**Windows | Linux**

[English](README_EN.md) · Español

Un port de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) a PC, hecho con
el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). El código PowerPC del
juego se recompila de forma estática y queda integrado en un solo ejecutable
con su propio launcher y sistema de mods. No es un emulador: el juego corre
nativo en Windows y Linux (Vulkan).

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Plataforma](https://img.shields.io/badge/Plataforma-Windows%20%7C%20Linux-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Licencia](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Estrellas](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)

| | |
|---|---|
| Jugadores | 1–2 (versus) |
| Plataforma | Windows / Linux |
| Motor | Xbox 360 (ReXGlue SDK) |
| Género | Lucha 3D |
| Versión | v1.4.3 EX |

Copyright (c) 2026 **NovaPowers**. Licencia MIT (ver `LICENSE`).

**[⬇ Descargar la última versión](../../releases/latest)** ·
[Novedades (CHANGELOG)](CHANGELOG.md) · [Cómo instalar](#cómo-instalar-paso-a-paso) ·
[Mods y personajes nuevos](#mods-y-personajes-nuevos)

---

## Novedades de la 1.4.3 EX

- **Rendimiento:** arreglados los 30 FPS y los tirones en equipos potentes, ritmo de imagen más regular y un launcher mucho más ligero.
- **Compatibilidad con cualquier gráfica:** los portátiles usan la gráfica dedicada, arreglado el cierre en AMD RX 6000/7000/9000, Intel Arc más rápida y cambio automático a Vulkan si Direct3D 12 falla.
- **Estable con programas pesados abiertos** (navegador, grabación, antivirus).
- **Packs de texturas HD** más fiables y textos traducidos al francés, alemán e italiano.

## Novedades de la 1.4.3

- **Definitivas para todos (WIP):** Zarbon, Dodoria y Androide 19 tienen su definitiva de Budokai 1 adaptada a Budokai 3; Androide 19 estrena sus golpes de Budokai 1. Es una primera versión: puede tener fallos.
- **Edit Skills arreglado:** con personajes nuevos instalados, la bandeja de cápsulas salía desplazada y no respondía.
- **Infinite World completo:** los personajes importados traen sus golpes, técnicas y definitiva originales, y todos entran en modo hiper.
- **Copias USA y Europa** de Budokai 1, Budokai 2, Infinite World y Shin Budokai para el importador.
- **Launcher y Kit más fáciles:** confirmaciones antes de borrar o sustituir, JUGAR sin congelarse, errores en claro, inglés completo y tarjeta *Definitiva* en el Kit.

## Novedades de la 1.4.2 EX

- **Texturas fáciles en PNG:** capturas las texturas jugando, editas el PNG y se aplica solo.
- **Arrastrar y soltar mods:** suelta un `.zip` sobre el launcher y se instala.
- **Aviso de mods rotos:** el launcher explica qué falla en cada mod y avisa junto a JUGAR.
- **Kit de modding con Python incluido:** ya no hay que instalar nada.
- **Mods nativos traducidos** al idioma del launcher.
- **Personajes de Shin Budokai:** voces, Burning Attack y ráfagas de ki arreglados.
- La 1.4.2 trajo **NVIDIA DLSS y AMD FSR 3 (beta)**, brillo HD regulable y el importador de Shin Budokai.

## Novedades de la v1.4.1

- **Arreglos contra los 30 FPS clavados en equipos potentes** (issue #8): temporización precisa y
  Windows deja de poner el juego en modo ahorro (núcleos de eficiencia).
- El diagnóstico de fallos de la gráfica (DRED) ya no cuesta rendimiento: solo
  se activa en la partida siguiente a un fallo de la gráfica.
- El registro dice dónde se va el tiempo (`gpu_wait`, `cp_wait`) y apunta los
  tirones de más de 50 ms.
- Aviso en el launcher si tu juego es la versión europea y tienes personajes
  nuevos (solo funcionan con la versión US/NA).
- Arreglado el cierre al cambiar de traje sobre un personaje nuevo (Janemba) y la
  música que se apagaba con el mod de personajes y un pack de música.
- Kit de modding 1.4.1: página **Diagnóstico** que lee el registro del juego y
  explica qué pasa y qué hacer.

El pack de personajes sigue siendo `DBZ3HD-1.4.0-Personajes.zip` (vale tal cual).

## Novedades de la v1.4.0

- **Menú rápido dentro del juego**: pulsa **F1** o **Back + Start** en el mando y
  cambia la imagen, el sonido o la pantalla sin salir del combate. Los cambios se
  aplican al momento y se guardan solos.
- **Más FPS con FSR**: el juego puede dibujarse a menos resolución y FSR la
  reescala, para ganar fluidez en equipos modestos.
- **Contador de FPS nuevo (F3)**: FPS del juego y de la pantalla, con gráfica.
- **Launcher renovado**: diseño nuevo, se abre en **menos de un segundo** (antes
  ~30 s) y se maneja entero **con el mando**.
- **Personajes nuevos** en casillas propias del select, sin sustituir a nadie:
  Janemba, Androide 19, Zarbon, Dodoria, Guldo, Jeice y Burter (pack opcional).
- **Importador de personajes** desde Budokai 1, Budokai 2, Infinite World y
  modelos de la comunidad, todo desde el launcher.

Lista completa en el [CHANGELOG](CHANGELOG.md).

---

## Capturas

<!--
TODO(capturas v1.4.0): guardar las capturas como .jpg en docs/screenshots/ (las .png
estan en .gitignore y publish_check las marca como FAIL) y quitar este comentario.

![Launcher v1.4.0](docs/screenshots/launcher_inicio.jpg)
![Pestaña Personajes nuevos con el importador](docs/screenshots/launcher_personajes_nuevos.jpg)
![Menú rápido dentro del juego (F1 / Back + Start)](docs/screenshots/menu_rapido.jpg)
![Contador de FPS (F3)](docs/screenshots/fps_f3.jpg)
![Select con los personajes nuevos](docs/screenshots/select_personajes_nuevos.jpg)
![Combate con un personaje nuevo](docs/screenshots/combate_personaje_nuevo.jpg)
-->

*Capturas próximamente.*

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

## Cómo instalar (paso a paso)

### Qué descargar

En la página de [**Releases**](../../releases/latest):

| Archivo | Para qué |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.4.3.1.zip` | **El juego para Windows** (obligatorio). |
| `DBZ-Budokai-3-HD-Collection-v1.4.3.1-linux-amd64.tar.gz` | El juego para Linux (Vulkan). |
| [`DBZ3HD-1.4.3-Personajes.zip`](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) | Opcional: los 7 personajes nuevos (**Google Drive**, actualizado en la 1.4.3; WIP). |
| `DBZ3HD-1.4.3.1-Kit-Modding.zip` | Opcional: herramientas para crear e importar personajes. |

### Windows

1. **Descomprime** `DBZ-Budokai-3-HD-Collection-v1.4.3.1.zip` en una carpeta, por
   ejemplo `C:\Juegos\DBZ3\`.
2. **Añade los datos de tu copia del juego** de una de estas dos formas:
   - **Lo más fácil — el ISO**: deja el `.iso` del juego junto a `dbz3.exe`. No
     hace falta extraer nada. (Los mods y los personajes nuevos necesitan la
     forma siguiente.)
   - **La carpeta extraída** (necesaria para mods): pon junto a `dbz3.exe` la
     carpeta `DBZ3\` tal cual sale del disco, o bien `default.xex` + la carpeta
     `us\` (o `eu\`).
3. **Abre `dbz3.exe`**. El launcher comprueba qué tienes y, si falta algo, te
   dice qué es. También puedes buscar los datos con los botones de carpeta o ISO.
4. Elige región, idioma, vídeo y sonido, y pulsa **JUGAR** (o **START** en el
   mando).

> Si algo falla, el launcher te enseña dónde está el registro (`logs\`).
> Adjúntalo al abrir un *issue*.

### Linux

El paquete incluye `dbz3` y `librexgpu-xenos.so`, pero no el juego. Extrae el
tarball, coloca tu `default.xex` legal y `us/` o `eu/` junto a `dbz3`, y
ejecuta `./dbz3`. Consulta [`docs/LINUX.md`](docs/LINUX.md) para dependencias y
build local.

### Disposiciones de archivos válidas

```
C:\Juegos\DBZ3\                C:\Juegos\DBZ3\                C:\Juegos\DBZ3\
├── dbz3.exe                   ├── dbz3.exe                   ├── dbz3.exe
├── default.xex                └── assets\                    └── DBZ3\            ← tal cual del disco
└── us\ (y/o eu\)                  ├── default.xex                ├── yae3_xenon.xex
                                   └── us\ (y/o eu\)              └── us\ (y/o eu\)
```

No hace falta renombrar el ejecutable: el launcher lo busca por **tamaño y
checksum**. Si pones el menú de la HD Collection en vez del ejecutable de
Budokai 3, te avisa y bloquea JUGAR. El ISO original completo (con el menú en la
raíz) también vale: el launcher coge de dentro el ejecutable de Budokai 3.

### Qué archivos necesitas (carpeta extraída)

Solo el ejecutable y los datos de tu región, no toda la ISO:

- **USA**: en `us\` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
  `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
  `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
  `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
- **EU/PAL**: los mismos archivos en `eu\`.

Puedes verificarlos contra `baserom.md`. Para extraerlos de tu ISO legal usa
una herramienta tipo `extract-xiso` (lee el sistema de archivos FATX de Xbox
360).

---

## Controles rápidos

| Tecla / botón | Qué hace |
|---|---|
| **F1** o **Back + Start** | Menú rápido en partida (imagen, sonido y mando, pantalla). Se puede cambiar a L3 + R3 o solo teclado. |
| **F3** | Contador de FPS (del juego y de la pantalla). |
| **F4** | Todos los ajustes (el launcher, encima del juego). |
| **LB / RB** o **Ctrl + Tab** | Cambiar de pestaña en el launcher. |
| **START** | JUGAR desde el launcher. |

La barra de ayudas del launcher muestra teclas o botones (Xbox, PlayStation o
Switch) según lo último que hayas usado.

---

## Mods y personajes nuevos

> Los mods necesitan los datos del juego en una **carpeta** (en modo ISO se
> juega tal cual). Los personajes nuevos necesitan además la **versión USA**.

### Pack de personajes (`DBZ3HD-1.4.3-Personajes.zip`)

Siete personajes **nuevos** en casillas propias de la rueda del select (no
sustituyen a nadie): **Janemba, Androide 19, Zarbon (con su transformación),
Dodoria, Guldo, Jeice y Burter**. Zarbon y Dodoria traen los golpes, combos y
gritos de su versión de Budokai 1, y Zarbon, Dodoria y Androide 19 su definitiva.

> **WIP:** las definitivas y los movesets traducidos de Budokai 1 son una primera
> versión y pueden tener fallos.

1. Cierra el juego.
2. Descarga el ZIP desde [Google Drive](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) y copia su carpeta
   `mods` junto a `dbz3.exe` (si Windows pregunta, acepta combinar carpetas: no
   se borra nada tuyo).
3. Abre `dbz3.exe`, mira la pestaña **Personajes nuevos** y pulsa **JUGAR**.

Para quitar uno, desmárcalo en **Personajes nuevos → Instalados**. Tus partidas
guardadas no se tocan.

### Kit de modding (`DBZ3HD-1.4.3.1-Kit-Modding.zip`)

Las herramientas que hay detrás de las pestañas de modding del launcher: crear
e **importar personajes** (Budokai 1, Budokai 2, Infinite World y modelos de la
comunidad), cápsulas propias, voces y gritos, texturas y cambio de modelo.

1. Instala [Python 3.11 o superior](https://www.python.org/downloads/) y marca
   *Add python.exe to PATH*.
2. Copia todo el contenido del ZIP en la carpeta del juego (deben quedar
   `mod center hd` y `awo_tools` junto a `dbz3.exe`).
3. Haz doble clic en `instalar_requisitos.bat` (una sola vez).
4. Abre `dbz3.exe` → **Personajes nuevos → Importar personaje de otro juego**:
   elige el juego, busca el personaje, revisa el nombre (se ve en directo cómo
   quedará en el select) y pulsa **Importar**.

Para importar desde otros juegos usa **tus copias** en la carpeta `ps2_games`
(lee el `LEEME.txt` que trae). Super Dragon Ball Heroes y Shin Budokai 1/2 se
detectan, pero todavía están en desarrollo.

### Otros mods

Los mods viven en `mods\<nombre>\` y reemplazan entradas del AFS por overlay,
sin tocar los AFS originales:

```
mods/<mod>/us/data_cmn.afs/<entrada>/geom.bin   # override de una entrada
mods/<mod>/manifest.txt                         # metadatos (nombre, autor...)
mods/<mod>/.disabled                            # si existe, el mod está OFF
```

Se gestionan desde el launcher (pestañas **Mods**, **Texturas**, **Model Swap**
y **Personajes nuevos**) o con las herramientas de `mod center hd/`. Guías en
[`docs/02_mods/`](docs/02_mods/) y formato de cápsulas en
[`docs/03_formatos/CAPSULAS_B3.md`](docs/03_formatos/CAPSULAS_B3.md).

**Swaps de modelo en cualquier dirección (mid-insert virtual).** Un swap B3→B3
es un override por entrada (~100 KB) que se sirve aunque el bin sea **más
grande** que el slot original: el runtime presenta al juego una tabla AFS
consistente y traduce las lecturas. Así funciona, por ejemplo, meter a Goten en
el slot de Krillin.

**Casillas nuevas en el select.** Desde la v1.4.0 el runtime también puede
**añadir** entradas al final de los AFS, y el exe amplía la rueda del select
con casillas nuevas. El constructor `mod center hd/roster_build.py`
convierte las carpetas `personaje.toml` en el mod generado `mods/_roster`
(iconos, rótulos, retratos, cara de la barra de vida, cápsulas y gritos), y el
launcher lo reconstruye solo al pulsar JUGAR.

---

## Características

**Un solo ejecutable universal**

- Un único `dbz3.exe` (runtime baseline SSSE3) que funciona en cualquier CPU
  x64 (Core 2 2006 en adelante), sin variantes.
- Núcleo **dual USA/EU** con las dos recompilaciones dentro y **autodetección
  del ejecutable** por tamaño y checksum.
- **Modo disco (ISO)**: juega directamente del `.iso` sin extraer nada.

**Vídeo y rendimiento**

- Escalado **FSR 1 / CAS**, **FXAA** y *dither*; resolución interna, MSAA y
  filtro anisotrópico. **Cambios en vivo**, sin reiniciar.
- **Más FPS con FSR** (Calidad / Equilibrado / Rendimiento / Ultra rendimiento):
  dibuja por debajo de la resolución y FSR la reescala.
- **Panel de FPS (F3)** con FPS del juego, FPS de pantalla y gráfica de tiempo
  de frame.
- **VRR** y **frame cap** real; **presets de calidad por GPU**.
- **Mejora de texturas (experimental)** en tiempo real y **packs de texturas**.

**Audio y controles**

- **Volumen general** y **Silenciar** reales, aplicados en caliente.
- **Mando (XInput / SDL)** o teclado, con teclas configurables, *deadzone*,
  vibración y nombres de botón de Xbox, PlayStation y Switch.
- Al salir de la ventana: **silenciar** y/o **atenuar** (opcional).

**Launcher**

- Diseño nuevo con tarjetas e iconos, botón **JUGAR** grande y **arranque en
  menos de un segundo**.
- **Navegable con el mando** y con barra de ayudas según el dispositivo.
- **5 idiomas** (ES/EN/IT/DE/FR), mensajes claros si el ejecutable no es el
  correcto, **Reparar instalación** en un clic y aviso de nueva versión.
- Ajustes **portables y autorreparables**.

**Mods**

- **Override por entrada AFS** con **mid-insert virtual** y **entradas
  añadidas** (personajes nuevos).
- **Personajes nuevos** con icono, rótulo, retratos y cara de la barra de vida
  generados desde el propio modelo, con vista previa en el launcher.
- **Importador** de Budokai 1, Budokai 2, Infinite World y modelos de la
  comunidad (`.amb`, `.amo` + `.amt`).
- **Cápsulas** propias por personaje, **gritos y voces** propios y **trajes
  extra** para personajes existentes (`traje.toml`).
- **Cambio de modelo B3↔B3** (catálogo de 183 personajes) y **texturas** (PNG).

**Diagnóstico (opcional)**

- Pestaña Desarrollo: registro de E/S y de rendimiento y palancas de
  diagnóstico de GPU. Todo **desactivado por defecto**.

---

## Estado

| Técnica | Estado |
|---|---|
| Swap nativo B3→B3 (override ~100 KB) | Funcional en cualquier dirección (bins > o < slot) |
| Mod de texturas B3 HD | Funcional (override por entrada) |
| 2+ mods de modelo/textura simultáneos | Funcional (mid-insert virtual) |
| Mod de música (og_music) | Funcional |
| Jugar desde el ISO (modo disco) | Funcional (juego base; mods requieren carpeta) |
| Núcleo dual USA/EU (un solo binario) | Funcional (validado en juego) |
| Personajes nuevos en casillas propias | Funcional, experimental (versión USA) |
| Port de personajes B1 / B2 / IW → B3 | Funcional, experimental (importador) |
| Shin Budokai 1/2 y Super Dragon Ball Heroes | En desarrollo |

---

## Estructura del repositorio

```
DBZ-Budokai-3-HD-Collection/
├── default.xex               # NO incluido. Ejecutable del juego (USA o EU)
├── us/  eu/                  # NO incluidos. Datos de cada región
├── src/                      # Recompilador + launcher + sistema de mods
│   ├── main.cpp              #   entrada, ventana, gestor de crash
│   ├── mods.cpp              #   sistema de mods (overlay AFS)
│   ├── roster_ext.cpp        #   personajes nuevos (plantilla, cápsulas, gritos)
│   ├── select_ext.cpp        #   rueda del select ampliada
│   ├── launcher/             #   interfaz del launcher (ui_kit) + pipeline de mods
│   └── ingame/               #   menú in-game y menú rápido (quick_settings)
├── generated/                # NO incluido. Código derivado de tu .xex
├── mod center hd/            # Herramientas Python de modding (roster_build, importar...)
├── awo_tools/                # Conversores y RE de formatos (AWO/AWG, PS2→HD, B1)
├── patches/                  # Overlay del ReXGlue SDK (ver su README)
├── mods/                     # Mods de usuario (vacía)
├── tools/                    # Scripts de release, modpacks y utilidades
├── docs/                     # Documentación completa
├── CHANGELOG.md              # Novedades por versión
├── baserom.md                # Archivos del juego requeridos + cómo extraerlos
└── LICENSE                   # MIT (NovaPowers)
```

---

## Regiones USA / EU

Los ejecutables USA (`yae3_xenon.xex`) y EU (`yae3_xenon_eu.xex`) son builds
distintas y el núcleo dual incluye la recompilación de cada uno. El launcher
identifica cuál has puesto por su checksum y usa el código correcto; si no
coincide, te avisa y bloquea JUGAR.

La región de **datos** (carpeta `us\` o `eu\`) y el **idioma** se eligen en el
launcher. El guardado es compartido entre regiones. Los personajes nuevos de la
v1.4.0 solo se aplican con el ejecutable y los datos **USA**.

---

## Compilar desde el código

Necesitas un compilador C++23 (clang de LLVM en Windows), CMake ≥ 3.25 y el
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) **v0.10.0** con nuestro
overlay aplicado.

> Copia `patches/rexglue-sdk/.` encima de un checkout limpio del tag `v0.10.0`
> del SDK (ver `patches/README.md`) y compila el runtime. Sin el overlay no
> funcionan los swaps grandes, los personajes nuevos ni el menú rápido.

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
`generated/README.md` y `.gitignore`). El paquete de release lo monta
`tools/make_release.ps1` y los modpacks `tools/make_modpacks.py`.

---

## Créditos

- **@ArielPVB**: modelos de Guldo, Jeice, Burter, Zarbon (normal y forma
  monstruo), Dodoria y Androide 19 del pack de personajes.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — herramientas de
  recompilación estática y runtime (derivado de [Xenia](https://xenia.jp)).
- [DBZ Burst Limit Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled)
  de **iExplosiveRage** — inspiración y origen de las mejoras del SDK adaptadas
  en la v1.4.0 (menú rápido con mando, panel de FPS, ajustes de vídeo en vivo,
  FSR por debajo de la resolución), tomadas de su rama `burstlimit` de
  [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk).
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — referencia de la
  API del SDK (solo referencia, no es base ni copia de código).
- Comunidad de modding de Budokai — herramientas, listas de referencia y los
  modelos y ports de Infinite World en los que se basa el pack de personajes.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) —
  mapeos de mandos (`gamecontrollerdb.txt`).
- **NovaPowers** — autor del launcher, el sistema de mods y las herramientas.
