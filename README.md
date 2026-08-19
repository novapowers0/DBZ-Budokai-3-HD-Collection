# DBZ Budokai 3 HD Collection — Recompile ReXGlue + Modding

**Native PC port of *Dragon Ball Z: Budokai 3 HD Collection* for the Xbox 360 (Windows).**
The game's original PowerPC machine code is statically recompiled and built into
a standalone executable — this is a real PC port, not an emulator.

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![License](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Stars](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)
[![Built with](https://img.shields.io/badge/built%20with-ReXGlue-8A2BE2?style=flat-square)](https://github.com/rexglue/rexglue-sdk)

| | |
|---|---|
| **Players** | 1–2 (versus) |
| **Platform** | Windows |
| **Engine** | Xbox 360 (ReXGlue SDK) |
| **Genre** | 3D fighting |
| **Framework** | [ReXGlue](https://github.com/rexglue/rexglue-sdk) |

Copyright (c) 2026 **NovaPowers**. Released under the MIT License (see `LICENSE`).

Static recompilation of **Dragon Ball Z: Budokai 3 HD Collection** (Xbox 360) for
Windows, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk),
with a launcher and a model/texture modding system validated in-game.

> ⚠️ **Origen**: este proyecto se **rehízo desde cero** sobre ReXGlue. El
> repositorio [`WistfulHopes/DBZ1`](https://github.com/WistfulHopes/DBZ1) se
> tomó **solo como referencia** (para entender la API del SDK), **NO como base
> ni copia de código**. El launcher, mods, regiones y herramientas son trabajo
> original de **NovaPowers**.

---

## ⚖️ Copyright / Legal

**El juego y sus datos NO se distribuyen.** Debes aportar los archivos de tu
**copia legal** del juego (el `.xex` y los `data_*.afs`). Este proyecto sigue la
convención "copyright-friendly" de la comunidad de recompilación estática
(como `mstan/DragonBallZBuusFuryRecomp`): el código y el launcher se
distribuyen, **el contenido del juego no**.

- Ver `baserom.md` para la identidad exacta de los archivos (tamaño y checksums)
  y cómo extraerlos de la ISO.
- El código recompilado (`generated/`) se genera **localmente** a partir de tu
  `.xex` y **no se sube** al repositorio.

Proyecto no oficial, sin fines comerciales, de investigación y preservación.
No está afiliado ni avalado por Bandai Namco, Shueisha, Toei Animation ni ningún
titular de los derechos de Dragon Ball.

---

## Estructura de carpetas

```
DBZ-Budokai-3-HD-Collection/
├── default.xex              # NO incluido. El ejecutable del juego (USA y EU son idénticos)
├── us/                      # NO incluido. Datos región USA (data_cmn.afs, adx_usa.afs...)
├── eu/                      # NO incluido. Datos región EU/PAL (data_cmn.afs, adx_jpn.afs...)
├── src/                     # Código fuente del recompilador/launcher/mods
│   ├── main.cpp             #   entrada, ventana, crash handler
│   ├── mods.cpp             #   sistema de mods (overlay AFS)
│   ├── hooks.cpp            #   hooks del runtime
│   ├── launcher/            #   UI del launcher + pipeline de modelos
│   └── ingame/              #   menú in-game (F10)
├── generated/               # NO incluido. Código generado del .xex (ver README ahí)
├── mod center hd/           # Herramientas Python de modding (propias)
│   ├── swap_b3.py           #   swaps B3→B3 nativos
│   ├── texture_b3.py        #   extracción/reconstrucción de texturas
│   ├── catalog_b3.cat       #   catálogo de personajes del B3
│   └── ...                  #   conversores, analizadores, exportadores
├── awo_tools/               # Herramientas de RE del formato AWO/AWG (propias)
├── patches/                 # Parches del ReXGlue SDK (mid-insert virtual AFS)
├── mods/                    # Carpeta de mods de usuario (vacía)
├── tools/                   # xbcompress.exe / xbdecompress.exe + utilidades
├── docs/                    # Documentación completa
├── CMakeLists.txt           # Build (REXSDK_DIR o rexglue/ junto al proyecto)
├── baserom.md               # Archivos del juego requeridos + cómo extraerlos
└── LICENSE                  # MIT (NovaPowers)
```

---

## Quick start (jugadores)

1. **Descarga** el ZIP de Windows desde la pestaña **Releases** y extráelo.
2. **Aporta los archivos del juego**: junto a `dbz3.exe` coloca `default.xex`
   y las carpetas `us/` y/o `eu/` con los datos de tu copia legal (ver
   `baserom.md` y "Instalar el juego" abajo).
3. **Ejecuta** `dbz3.exe`.
4. En el launcher elige **Región** (USA / EU PAL), **Idioma**, **Vídeo** y
   **Audio**, y pulsa **Play**.

> El launcher recuerda la configuración. Si marcas "Skip launcher on boot",
> los siguientes arranques van directos al juego.

### Instalar el juego (aportar los archivos)

El paquete de release **no trae** los archivos del juego (copyright). Tienes
que extraerlos de tu **ISO legal** de *Dragon Ball Z: Budokai 3 HD Collection*
(Xbox 360):

1. Extrae la ISO con una herramienta tipo `extract-xiso` (lee el sistema de
   archivos FATX de Xbox 360).
2. Copia a `default.xex` el ejecutable del juego (junto a `dbz3.exe`).
3. Copia la carpeta de datos de tu región:
   - **USA**: a `us/` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
     `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
     `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
     `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
   - **EU/PAL**: a `eu/` → los mismos archivos de la región.
4. Verifica los archivos contra `baserom.md` (tamaños y checksums SHA-256).

Solo necesitas el ejecutable y los archivos de datos, **no toda la ISO**.

---

## Regiones EU/US

- Los `.xex` USA y EU son **byte-idénticos** (misma región lógica; la región
  está en los datos).
- El launcher (pestaña *Video* → *Region*: `USA` / `EU (PAL)`) o el cvar
   `dbz3_region` montan `us` o `eu` en `game:\us`.
- El guardado es compartido entre regiones.

---

## Mods

Los mods viven en `mods/<nombre>/` (carpeta `mods/` se distribuye **vacía**) y
reemplazan entradas del AFS por overlay, sin tocar los AFS originales:

```
mods/<mod>/us/data_cmn.afs/<entry>/geom.bin   # override de una entrada del AFS
mods/<mod>/manifest.txt                       # metadatos del mod (nombre, autor...)
mods/<mod>/.disabled                          # si existe, el mod está OFF
```

El override se instala en todos los AFS de personaje. Gestión visual en el
launcher (pestañas **Mods**, **Texturas** y **Model Swap**) o con
`mod center hd/`. Guías: `docs/02_mods/`.

### Model swaps en cualquier dirección (mid-insert virtual)

Un swap de modelo B3→B3 es un override por entrada (~100 KB). El bin comprimido
del personaje origen se sirve en el slot destino; puede ser **más grande o más
pequeño** que el slot original:

- Si el bin **cabe** en el `to_read` del slot (`ceil(size/0x1000)*0x1000`), el
  override clásico lo sirve sin más.
- Si el bin es **más grande** (p.ej. Goten 107006 B en el slot de Krillin, cuyo
  to_read es 106496 B), el runtime aplica un **mid-insert virtual**: presenta al
  guest una tabla AFS consistente donde la entrada crece in-place y las
  posteriores se desplazan (igual que un AFS reconstruido), y traduce las
  lecturas al archivo físico. Así el guest aloca el buffer correcto y recibe el
  bin completo sin truncar.

Este comportamiento requiere el **parche del ReXGlue SDK** incluido en
`patches/` (ver `patches/README.md`): `afs.cpp`, `afs.h` y
`host_path_file.cpp` añaden `AfsGetVirtualTable`/`AfsTranslateOffset`.

Genera un swap desde el launcher (pestaña **Model Swap**) o:

```powershell
python "mod center hd/swap_b3.py" --origen <bin> --dest <slot> --mod <nombre>
python "mod center hd/texture_b3.py" build --bin <bin> --slot <slot> --dir <carpeta png>
```

### Funcionalidades del launcher

- **Video**: resolución, región, idioma, VRR, frame cap (0 = sin tope).
- **Upscaling**: escala de resolución interna, FSR/CAS.
- **Audio**: volúmenes maestro/música/efectos/voz.
- **Input**: backend (xinput recomendado), deadzone, vibración.
- **Mods**: activar/desactivar mods y editar su manifest (título/autor/descripción).
- **Texturas**: extraer texturas de un personaje a PNG, editarlas y reconstruir
  el mod (cambia el slot destino si quieres).
- **Model Swap**: swap nativo B3→B3 (catálogo de 183 personajes).
- **Dev**: FPS counter, diagnóstico GPU (logs + .bmp) y minidumps — **todo OFF
  por defecto** para no ensuciar la carpeta.

---

## Estado (19/08/2026)

| Técnica | Estado |
|---|---|
| Swap nativo B3→B3 (override ~100KB) | ✅ **100% funcional, cualquier dirección** (bins > o < slot) |
| Mod de texturas B3 HD | ✅ **100% funcional** (override por entrada, ~118KB) |
| 2+ mods de modelo/textura simultáneos | ✅ **100% funcional** (mid-insert virtual) |
| Mod de música (og_music) | ✅ **100% funcional** |
| Diagnóstico GPU (logs + .bmp) | ✅ Gateado por Dev mode — OFF en juego normal |
| Port PS2→HD | ⚠️ Investigado, requiere reconstrucción completa |
| Port de personajes IW→B3 | 🔴 Descartado (Janemba fracasó, archivado) |

---

## Building from source (desarrolladores)

Requisitos: compilador C++23, CMake ≥ 3.25, y el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
(`REXSDK_DIR` o una carpeta `rexglue/` junto al proyecto).

> ⚠️ **Aplica los parches del runtime** (carpeta `patches/`) al SDK antes de
> compilar: copia los 3 archivos sobre su ruta equivalente del SDK y recompila
> el runtime (ver `patches/README.md`). Sin ellos los swaps de modelo con bins
> más grandes que el slot no funcionan.

```
git clone --recurse-submodules https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection.git
cd DBZ-Budokai-3-HD-Collection
# 0) aplica patches/ sobre tu ReXGlue SDK (afs.cpp, afs.h, host_path_file.cpp)
# 1) aporta tu .xex legal en default.xex
# 2) regenera el código derivado del xex
cmake --build out/build/win-amd64-release --target dbz3_codegen
# 3) compila
cmake -S . -B out/build/win-amd64-release --preset win-amd64-release
cmake --build out/build/win-amd64-release
# 4) ejecuta
out\build\win-amd64-release\dbz3.exe
```

> El código recompilado (`generated/`) se deriva de tu `.xex` y **nunca se
> sube** (ver `generated/README.md` y `.gitignore`).

---

## Rutas portables

- Las herramientas detectan los AFS desde `us/`/`eu/` o desde la ruta que les
  pases en el launcher (pestaña Mods → Archivos fuente).
- El proyecto B1 se localiza con la variable `DBZ1_ROOT` o como carpeta
  hermana `DBZ Budokai HD Collection`.
- `xbcompress.exe`/`xbdecompress.exe` viven en `tools/` (o vía
  `DBZ3_XBCOMP_DIR`).

---

## Créditos

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — herramientas de
  recompilación.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — referencia de la
  API del SDK (no usado como base).
- Comunidad de modding de Budokai — herramientas y modelos de referencia.
- **NovaPowers** — autor del sistema de mods, launcher y herramientas.
