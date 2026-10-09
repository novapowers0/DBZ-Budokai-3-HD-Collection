# DBZ Budokai 3 HD Collection — Mods y toolkit

Este paquete incluye el **toolkit de modding** (`mod center hd/`) junto al
ejecutable, de modo que **Model Swap** y **Texturas** del launcher funcionan
directamente, sin pasos extra.

## Qué incluye

- `mod center hd/swap_b3.py` — swap de modelo nativo B3 HD -> B3 HD.
- `mod center hd/texture_b3.py` — extracción/reconstrucción de texturas (PNG).
- `mod center hd/catalog_b3.cat` — catálogo de 183 personajes del B3.
- `mod center hd/tools/` — xbcompress/xbdecompress (compresión LZX del juego)
  y sus DLLs (MSVCR71/MSVCP71/xbdm).

Estos scripts requieren **Python** instalado en el sistema (`python` en el PATH,
o la variable `DBZ3_PYTHON` apuntando al ejecutable). El mod de texturas además
requiere las librerías `Pillow` y `numpy` (`pip install pillow numpy`).

## Model Swap / Texturas desde el launcher

1. Ejecuta `dbz3.exe` y ve a la pestaña **Model Swap** o **Texturas**.
2. Elige el personaje origen y el slot destino.
3. Pulsa el botón. El mod se genera en `mods/` y se activa solo.

## Cómo instalar un mod descargado

Los mods son carpetas dentro de `mods/` (junto a `dbz3.exe`), cada uno con su
`manifest.txt`:

```
mods/
└── mi_mod/
    ├── manifest.txt          # metadatos (name, description, author...)
    ├── .disabled             # si existe, el mod está desactivado
    └── us/
        └── data_cmn.afs/
            └── 327/
                └── geom.bin  # override de una entrada del AFS
```

Para instalar un mod:
1. Abre la pestaña **Mods** del launcher y pulsa **"Abrir carpeta de mods"**
   (crea `mods/` si no existe y la abre en el Explorador).
2. Copia ahí la carpeta de tu mod (o descomprime su ZIP).
3. El launcher lo lista automáticamente; actívalo con su casilla.

> Los mods de música (og_music) reemplazan archivos completos:
> `mods/<mod>/us/adx_usa.afs`, `us/opening.sfd`, etc.

## Personajes nuevos (v1.4.0)

La pestaña **Personajes nuevos** del launcher añade personajes en casillas
propias del select, sin sustituir a nadie (experimental; versión USA y datos en
carpeta). Hay dos descargas opcionales:

- **`DBZ3HD-1.4.3-Personajes.zip`** ([Google Drive](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing)) — Janemba, Androide 19, Zarbon, Dodoria,
  Guldo, Jeice y Burter, listos para jugar (las definitivas de la 1.4.3 son WIP:
  pueden tener fallos). Copia la carpeta `mods` del ZIP
  junto a `dbz3.exe` (acepta combinar carpetas), abre el launcher y pulsa
  **JUGAR**. Para quitar uno, desmárcalo en **Personajes nuevos → Instalados**.
- **`DBZ3HD-1.4.3-Kit-Modding.zip`** — las herramientas para crear e
  **importar** personajes (Budokai 1, Budokai 2, Infinite World y modelos de la
  comunidad), cápsulas, voces y gritos. Copia todo su contenido junto a
  `dbz3.exe`, instala Python 3.11+ y ejecuta `instalar_requisitos.bat` una vez
  (instala numpy, Pillow y scipy). Luego: **Personajes nuevos → Importar
  personaje de otro juego**.

Tus partidas guardadas no se tocan en ningún caso.

## Nota sobre las herramientas

El toolkit del paquete es el subconjunto **necesario para el runtime** (swap y
texturas). Las herramientas de personajes nuevos van en el **Kit de modding**
(descarga aparte). El repositorio de GitHub contiene el resto de herramientas de
investigación/RE de `mod center hd` y `awo_tools`, por si quieres profundizar.
