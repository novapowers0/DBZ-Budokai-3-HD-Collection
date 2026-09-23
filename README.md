# DBZ Budokai 3 HD Collection

**Windows | Linux**

[English](README_EN.md) Â· EspaÃ±ol

Un port de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) a PC, hecho con
el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). El cÃ³digo PowerPC del
juego se recompila de forma estÃ¡tica y queda integrado en un solo ejecutable
con su propio launcher y sistema de mods. No es un emulador: el juego corre
nativo en Windows y Linux (Vulkan).

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Plataforma](https://img.shields.io/badge/Plataforma-Windows%20%7C%20Linux-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Licencia](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Estrellas](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)

| | |
|---|---|
| Jugadores | 1â€“2 (versus) |
| Plataforma | Windows / Linux |
| Motor | Xbox 360 (ReXGlue SDK) |
| GÃ©nero | Lucha 3D |
| VersiÃ³n | v1.2.8.1 |

Copyright (c) 2026 **NovaPowers**. Licencia MIT (ver `LICENSE`).

---

## Aviso legal

Este proyecto no incluye el juego. Para jugar necesitas aportar los archivos de
**tu copia legal**: el ejecutable (`default.xex`) y los `data_*.afs` de la
regiÃ³n que uses. Es la convenciÃ³n habitual en la comunidad de recompilaciÃ³n
estÃ¡tica (por ejemplo `mstan/DragonBallZBuusFuryRecomp`): se distribuye el
cÃ³digo y el launcher, no el contenido del juego.

- En `baserom.md` estÃ¡ la identidad exacta de cada archivo (tamaÃ±os y
  checksums SHA-256) y cÃ³mo extraerlos de tu ISO.
- El cÃ³digo recompilado (`generated/`) se genera **localmente** a partir de tu
  `.xex` y no se sube al repositorio.

Es un proyecto no oficial, sin Ã¡nimo de lucro, de investigaciÃ³n y preservaciÃ³n.
No tiene relaciÃ³n con Bandai Namco, Shueisha, Toei Animation ni ningÃºn titular
de los derechos de Dragon Ball.

---

## CÃ³mo jugar

### Descargas por plataforma

- **Windows:** `DBZ-Budokai-3-HD-Collection-v1.2.8.1.zip`
- **Linux amd64:** `DBZ-Budokai-3-HD-Collection-v1.2.8.1-linux-amd64.tar.gz` (Vulkan)

El paquete Linux incluye `dbz3` y `librexgpu-xenos.so`, pero no incluye el juego
ni sus assets. Extrae el tarball, coloca tu `default.xex` legal y `us\` o `eu\`
junto a `dbz3`, y ejecuta `./dbz3`. Consulta [`docs/LINUX.md`](docs/LINUX.md)
para dependencias y build local.

Tienes dos formas de aportar los datos del juego: con la carpeta extraÃ­da o
directamente con el ISO. Las dos se detectan solas, no hay que configurar nada.

**OpciÃ³n A â€” la carpeta extraÃ­da (para usar mods)**

1. Descarga el ZIP de **Releases** y descomprÃ­melo donde quieras.
2. Pon junto a `dbz3.exe` el `default.xex` y la carpeta `us\` (o `eu\`). Valen
   estas dos disposiciones:

   ```
   C:\Juegos\DBZ3\                C:\Juegos\DBZ3\
   â”œâ”€â”€ dbz3.exe                   â”œâ”€â”€ dbz3.exe
   â”œâ”€â”€ default.xex                â””â”€â”€ assets\
   â””â”€â”€ us\ (y/o eu\)                  â”œâ”€â”€ default.xex
                                     â””â”€â”€ us\ (y/o eu\)
   ```

   **TambiÃ©n vale el volcado tal cual del disco** (con la carpeta `DBZ3\` y su
   ejecutable sin renombrar): el launcher busca el ejecutable de Budokai 3 solo.

   ```
   C:\Rom\Budokai HD Collection\
   â”œâ”€â”€ dbz3.exe
   â””â”€â”€ DBZ3\                      â† tal cual sale de tu ISO
       â”œâ”€â”€ yae3_xenon.xex
       â””â”€â”€ us\ (y/o eu\)
   ```

3. Ejecuta `dbz3.exe`. El launcher comprueba quÃ© hay y, si falta algo, te lo
   dice. Puedes buscar la carpeta de datos con "Seleccionar carpeta de datos...".
4. Elige **RegiÃ³n**, **Idioma**, **VÃ­deo** y **Audio** y pulsa **Play**.

**OpciÃ³n B â€” el ISO directamente (para jugar sin extraer nada)**

Deja el `.iso` del juego junto a `dbz3.exe` (o usa "Seleccionar ISO..." en el
launcher). El launcher lo detecta, saca el ejecutable de Budokai 3 del disco
(`DBZ3\yae3_xenon.xex`, unos pocos MB) y monta el resto directamente desde la
imagen: no hace falta descomprimir ni copiar los AFS. La regiÃ³n se detecta sola
a partir del ejecutable del propio disco.

> Funciona con el ISO original completo (el que trae el menÃº de la HD Collection
> en la raÃ­z): el launcher coge el ejecutable de Budokai 3 de dentro del disco,
> no el menÃº.

> Los mods necesitan la carpeta extraÃ­da (opciÃ³n A). En modo disco se juega
> tal cual del ISO.

> **Un solo `dbz3.exe`**: desde v1.1.0 no hay variantes. Un Ãºnico ejecutable
> universal (runtime baseline SSSE3) que funciona en cualquier CPU x64 (Core 2
> 2006 en adelante), con las recompilaciones USA y EU dentro y autodetecciÃ³n
> del ejecutable que pongas (por tamaÃ±o y checksum, sin importar cÃ³mo se llame
> ni dÃ³nde estÃ©).

### QuÃ© archivos necesitas (opciÃ³n A)

Solo el ejecutable y los datos de tu regiÃ³n, no toda la ISO:

- **USA**: a `us\` â†’ `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
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
â”œâ”€â”€ default.xex               # NO incluido. Ejecutable del juego (USA o EU)
â”œâ”€â”€ us/                       # NO incluido. Datos regiÃ³n USA
â”œâ”€â”€ eu/                       # NO incluido. Datos regiÃ³n EU/PAL
â”œâ”€â”€ src/                      # Recompilador + launcher + sistema de mods
â”‚   â”œâ”€â”€ main.cpp              #   entrada, ventana, gestor de crash
â”‚   â”œâ”€â”€ mods.cpp              #   sistema de mods (overlay AFS)
â”‚   â”œâ”€â”€ launcher/             #   interfaz del launcher + pipeline de modelos
â”‚   â””â”€â”€ ingame/               #   menÃº in-game
â”œâ”€â”€ generated/                # NO incluido. CÃ³digo derivado de tu .xex
â”œâ”€â”€ mod center hd/            # Herramientas Python de modding (propias)
â”œâ”€â”€ awo_tools/                # Herramientas de RE del formato AWO/AWG
â”œâ”€â”€ patches/                  # Parches del ReXGlue SDK (ver su README)
â”œâ”€â”€ mods/                     # Mods de usuario (vacÃ­a)
â”œâ”€â”€ tools/                    # xbcompress/xbdecompress + utilidades
â”œâ”€â”€ docs/                     # DocumentaciÃ³n completa
â”œâ”€â”€ CMakeLists.txt            # Build
â”œâ”€â”€ baserom.md                # Archivos del juego requeridos + cÃ³mo extraerlos
â””â”€â”€ LICENSE                   # MIT (NovaPowers)
```

---

## Regiones USA / EU

Los ejecutables USA (`yae3_xenon.xex`) y EU (`yae3_xenon_eu.xex`) son builds
distintas, no dos copias iguales, y el nÃºcleo dual incluye la recompilaciÃ³n de
cada uno. El launcher identifica cuÃ¡l has puesto por su checksum y usa el
cÃ³digo correcto; si no coincide, te avisa y bloquea Play para que no acabes con
un cierre raro en pantalla.

No hace falta que el archivo se llame `default.xex` ni que estÃ© en la raÃ­z: el
launcher lo busca por **tamaÃ±o + checksum** (los nombres tÃ­picos son
`yae3_xenon.xex` / `yae3_xenon_eu.xex`) y lo prepara Ã©l solo. Si pones el menÃº de
la HD Collection en vez del ejecutable de Budokai 3, te lo dice y bloquea Play.

La regiÃ³n de **datos** (carpeta `us\` o `eu\`) y el **idioma** se eligen en el
launcher y no dependen del ejecutable. El guardado es compartido entre
regiones.

---

## Mods

Los mods viven en `mods\<nombre>\` (la carpeta se distribuye vacÃ­a) y reemplazan
entradas del AFS por overlay, sin tocar los AFS originales:

```
mods/<mod>/us/data_cmn.afs/<entrada>/geom.bin   # override de una entrada
mods/<mod>/manifest.txt                         # metadatos (nombre, autor...)
mods/<mod>/.disabled                            # si existe, el mod estÃ¡ OFF
```

Se gestionan visualmente desde el launcher (pestaÃ±as **Mods**, **Texturas** y
**Model Swap**) o con las herramientas de `mod center hd/`. GuÃ­as en
`docs/02_mods/`.

### Swaps de modelo en cualquier direcciÃ³n (mid-insert virtual)

Un swap B3â†’B3 es un override por entrada (~100 KB) que se sirve en el slot
destino aunque el bin sea **mÃ¡s grande** que el slot original: el runtime
presenta al juego una tabla AFS consistente (la entrada crece en su sitio y las
siguientes se desplazan) y traduce las lecturas. AsÃ­ funciona, por ejemplo,
meter a Goten en el slot de Krillin.

Esto requiere el **parche del ReXGlue SDK** incluido en `patches/` (ver
`patches/README.md`).

---

## Compilar desde el cÃ³digo

Necesitas un compilador C++23, CMake â‰¥ 3.25 y el
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (`REXSDK_DIR` o una
carpeta `rexglue/` junto al proyecto).

> Aplica primero los parches del runtime (`patches/`) sobre tu copia del SDK,
> tal y como explica `patches/README.md`, y recompila el runtime. Sin ellos los
> swaps con bins mÃ¡s grandes que el slot no funcionan.

```
git clone --recurse-submodules https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection.git
cd DBZ-Budokai-3-HD-Collection

# 1) pon tu .xex legal en default.xex
# 2) regenera el cÃ³digo derivado del xex
cmake --build out/build/win-amd64-release --target dbz3_codegen
# 3) compila
cmake -S . -B out/build/win-amd64-release
cmake --build out/build/win-amd64-release
# 4) ejecuta
out\build\win-amd64-release\dbz3.exe
```

El cÃ³digo recompilado (`generated/`) se deriva de tu `.xex` y no se sube (ver
`generated/README.md` y `.gitignore`). La estructura del paquete de release la
monta `tools/make_release.ps1`.

---

## Estado

| TÃ©cnica | Estado |
|---|---|
| Swap nativo B3â†’B3 (override ~100 KB) | Funcional en cualquier direcciÃ³n (bins > o < slot) |
| Mod de texturas B3 HD | Funcional (override por entrada, ~118 KB) |
| 2+ mods de modelo/textura simultÃ¡neos | Funcional (mid-insert virtual) |
| Mod de mÃºsica (og_music) | Funcional |
| Jugar desde el ISO (modo disco) | Funcional (juego base; mods requieren carpeta) |
| NÃºcleo dual USA/EU (un solo binario) | Funcional (validado en juego) |
| Port PS2â†’HD | En investigaciÃ³n; requiere reconstrucciÃ³n completa |
| Port de personajes IWâ†’B3 | Descartado (Janemba fracasÃ³, archivado) |

---

## CaracterÃ­sticas

**Un solo ejecutable universal**

- Un Ãºnico `dbz3.exe` (runtime baseline SSSE3) que funciona en cualquier CPU
  x64 (Core 2 2006 en adelante), sin variantes.
- NÃºcleo **dual USA/EU** con las dos recompilaciones dentro y **autodetecciÃ³n
  del ejecutable** por tamaÃ±o y checksum: no hay que renombrarlo ni dejarlo en
  la raÃ­z.
- **Modo disco (ISO)**: juega directamente del `.iso` sin extraer nada, incluido
  el ISO original completo con el menÃº de la HD Collection.

**VÃ­deo y rendimiento**

- Escalado **FSR 1 / CAS**, **FXAA** y *dither*; resoluciÃ³n interna, MSAA y
  filtro anisotrÃ³pico.
- **VRR** y **frame cap** real (0 = sin tope).
- **Presets de calidad por GPU**: el modo AutomÃ¡tico detecta tu GPU y elige el
  perfil; ningÃºn preset sube la escala interna (1x es lo recomendado).
- **Mejora de texturas (experimental)**: reescala las texturas en tiempo real
  (Nitidas x2 / Muy nÃ­tidas x3) sin tocar los archivos del juego, con **HUD
  limpio**.
- **Aviso de coste** al subir la escala interna y botÃ³n **"Volver a nativo
  (1x)"**.

**Audio y controles**

- **Volumen general** y **Silenciar** reales, aplicados en caliente.
- **Mando (XInput)** o teclado, con teclas configurables, *deadzone*, vibraciÃ³n
  y sensibilidad del ratÃ³n.
- Al salir de la ventana: **silenciar** y/o **atenuar** (opcional).

**Launcher**

- Interfaz con pestaÃ±as, **selector de fuente siempre visible** (carpeta o ISO)
  y **Play** que avisa si falta algo en vez de fallar en silencio.
- **5 idiomas** (ES/EN/IT/DE/FR) y **mensajes claros** si el ejecutable no es el
  correcto (menÃº de la HD Collection, ejecutable de DBZ1, dump desconocido).
- **Ajustes portables y autorreparables**: si la carpeta no es escribible se usa
  `Documents/dbz3`, y si `dbz3_user.toml` se daÃ±a se repara solo o se guarda una
  copia `.bak`.
- **Aviso de nueva versiÃ³n** al abrir (se puede desactivar).

**Mods**

- **Override por entrada AFS** (sin tocar los AFS originales) con **mid-insert
  virtual**: funciona aunque el modelo sea mÃ¡s grande que el hueco.
- **Cambio de modelo nativo B3â†”B3**: catÃ¡logo de 183 personajes, buscador, vista
  previa y en cualquier direcciÃ³n.
- **Texturas**: extraer a PNG, editar y reconstruir el mod; tambiÃ©n se pueden
  reemplazar la mÃºsica y ficheros completos.
- Centro de mods con buscador, activar/desactivar todos, badges de tipo e
  instalaciÃ³n desde ZIP.

**DiagnÃ³stico (opcional)**

- PestaÃ±a Desarrollo: contador de FPS, registro de E/S y de rendimiento y
  palancas de diagnÃ³stico de GPU. Todo **desactivado por defecto**.

---

## CrÃ©ditos

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) â€” herramientas de
  recompilaciÃ³n.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) â€” referencia de la
  API del SDK (solo referencia, no es base ni copia de cÃ³digo).
- Comunidad de modding de Budokai â€” herramientas y modelos de referencia.
- **NovaPowers** â€” autor del launcher, el sistema de mods y las herramientas.
