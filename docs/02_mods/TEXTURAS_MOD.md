# Pestaña "Texturas" del launcher — Guía de funcionamiento

> Cómo funciona el mod de texturas B3 HD → B3 HD desde el launcher
> (script `mod center hd/texture_b3.py` + pestaña Texturas en `launcher_state.cpp`).

---

## 1. Qué hace

Permite editar las **texturas de un personaje** (los `.dds` DXT3 embebidos en el
bin #AMB / bloque #AZT) y generar un **mod activo** que las sustituye en el
juego, sin tocar la geometría (el bin no cambia de tamaño).

Flujo general:
1. **Extraer** las texturas de un personaje como **PNG editables**.
2. **Editar** los PNG (en la carpeta que elijas).
3. **Reconstruir** el mod: re-codifica los PNG a DXT3 y genera el AFS del mod.

---

## 2. Elementos de la pestaña

### 2.1 Personaje (origen de las texturas)
Combo con el catálogo `mod center hd/catalog_b3.cat` (183 personajes). Cada
entrada muestra `Nombre [bin N]` para distinguir variantes del mismo personaje
(p.ej. Dr. Gero `[bin 91]` vs `Dr. Gero (Traje alternativo) [bin 92]`).

> ⚠️ Algunos bins NO tienen texturas propias (modelos parciales/variantes que
> reutilizan las de otro bin, p.ej. el bin 92 de Dr. Gero). El extract avisa con
> un mensaje claro en vez de fallar.

### 2.2 Slot destino (donde se aplican las texturas)
- **"El mismo personaje (sin swap)"** (por defecto): las texturas se aplican al
  personaje del que se extrajeron.
- **Otro personaje**: el bin del origen (con sus texturas editadas) se coloca en
  el slot del destino → compatible con **swaps de modelo**: extraes texturas de
  A, editas, y las aplicas al bin de B (que puede venir de un swap).

### 2.3 Nombre del mod
Define la carpeta del mod (`mods/<nombre>/`). Si se deja vacío, se auto-genera
`tex_<bin>`. Los caracteres inválidos de Windows (`<>:"|?*`) se sanitizan.

### 2.4 Carpeta de texturas (PNG)
- **Se auto-rellena** al extraer con la ruta por defecto
  (`mods/<mod>/textures`).
- **Editable**: puedes escribir cualquier otra carpeta donde estés editando.
- Botón **"Examinar..."**: abre el diálogo nativo de Windows para elegir carpeta
  (helper `PickFolder()` en `launcher_state.cpp`, `IFileOpenDialog` +
  `FOS_PICKFOLDERS`).

> El botón "Reconstruir" se habilita SOLO si la carpeta configurada contiene
> `textures_meta.json` (generado por el extract). No depende del nombre del mod.

---

## 3. Botones

| Botón | Qué hace |
|-------|----------|
| **Extraer texturas a PNG** | Extrae el bin, localiza el bloque #AZT, y vierte cada textura como PNG editable en la carpeta configurada. Genera `textures_meta.json` (con el header DDS original de cada textura). |
| **Abrir carpeta de texturas** | Abre en el Explorador de Windows la carpeta activa (para ver/editar los PNG). |
| **Reconstruir mod con texturas editadas** | Re-codifica los PNG a DXT3, reconstruye el DDS (header + bitmap), reemplaza en el #AZT, comprime LZX `/N:2048`, rellena al slot y genera el mod AFS activo. |

---

## 4. Formato #AZT (resumen)

- El bin #AMB → #AWO (modelo) + #AZT (texturas).
- Header #AZT: `tex_am` en +0x10, `index_loc` en +0x14 (tabla de offsets).
- Cada textura: `idx@+0`, `type@+4`, `w@+16`, `h@+18`, `data_off@+0x14`
  (offset relativo al AZT del header DDS de 128B + bitmap DXT3).
- Bitmap DXT3 = `w*h` bytes (BC2, 16B/bloque 4x4, mipmaps=0).
- Pillow decodifica DDS DXT3 → RGBA; el encoder DXT3 propio (numpy) re-codifica
  al tamaño exacto.

---

## 5. Script `texture_b3.py` (CLI)

```
# Extraer (carpeta por defecto: mods/<mod>/textures)
python "mod center hd/texture_b3.py" extract --bin <n> [--mod <nombre>] [--afs <afs>] [--out <mods>] [--dir <carpeta>]

# Reconstruir
python "mod center hd/texture_b3.py" build --mod <nombre> [--afs <afs>] [--out <mods>] [--dir <carpeta>] [--slot <destino>]
```

Argumentos clave:
- `--bin N` (extract): entrada AFS del personaje.
- `--dir <carpeta>`: carpeta de PNG (extract: salida; build: lectura). Útil
  para editar fuera del árbol del mod.
- `--slot N` (build): slot destino en el AFS (por defecto el mismo bin del
  extract). Permite aplicar las texturas de un personaje sobre otro (swap).
- `--afs <ruta>`: data_cmn.afs a operar (por defecto `us/data_cmn.afs`).

El build genera el `manifest.txt` del mod (name/description/type/source/target)
y el AFS reconstruido con **mid-insert** (el bin puede crecer en su slot sin
romper la tabla AFS).

---

## 6. Notas de robustez / errores

- **Error "sintaxis de la etiqueta del volumen"**: estaba causado por `_popen`
  de MSVC + cmd.exe al lanzar Python con un comando que empieza por `"`. Se
  resolvió usando `CreateProcessW` (mod_pipeline.cpp) que lanza Python
  directamente sin cmd.exe.
- **Workdir fijo**: el script usa `out/build/win-amd64-release/.tex_work` (no
  depende del `TEMP` del entorno, que el launcher puede heredar inválido) y
  pasa a xbcompress/xbdecompress un entorno con `TEMP`/`TMP` corregidos.
- **Diagnóstico**: `RunAsync` escribe el comando exacto en
  `out/build/win-amd64-release/pipeline_cmd.log`; el script vuelca cualquier
  traceback a `texture_b3_error.log` e imprime el traceback completo en el
  output del launcher.
- **Bins sin #AZT**: aviso claro en vez de traceback (p.ej. Dr. Gero bin 92).
