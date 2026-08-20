# DBZ Budokai 3 HD Collection — Release ejecutable

Copyright (c) 2026 **NovaPowers**. Released under the MIT License.

Este paquete contiene el ejecutable recompilado de *Dragon Ball Z: Budokai 3 HD
Collection* (Xbox 360) con el launcher, el sistema de mods y el pipeline de
modelos. **NO incluye los archivos del juego** (copyright): debes aportar los
de tu copia legal.

## Contenido

- `dbz3.exe` — recompilador + launcher + sistema de mods
- `rexruntime.dll` — runtime ReXGlue
- `rexgpu-xenos.dll` — plugin GPU (Xenos)
- `TracyClient.dll` — profiling (requerido por el runtime)
- `amd_fidelityfx_dx12.dll` / `amd_fidelityfx_vk.dll` — upscaling FSR (D3D12/Vulkan)
- `SPIRV-Tools-shared.dll` — utilidades SPIR-V (backend Vulkan)
- `RELEASE_README.md` — este archivo

## Cómo instalar y jugar (paso a paso)

El paquete **NO incluye los archivos del juego** (copyright). Tienes que
aportarlos de tu **copia legal**. Haz esto:

1. **Descomprime** el ZIP en una carpeta, por ejemplo `C:\Juegos\DBZ3\`.
   Quedará un archivo `dbz3.exe` junto a varias DLLs.

2. **Aporta los archivos del juego** en una de estas dos disposiciones (la que
   prefieras; el launcher las detecta ambas automáticamente):

   **Opción A — junto a `dbz3.exe`:**

   ```
   C:\Juegos\DBZ3\
   ├── dbz3.exe            ← ya viene aquí
   ├── rexruntime.dll      ← ya viene aquí
   ├── ... (DLLs)          ← ya vienen aquí
   ├── default.xex         ← TÚ lo pones aquí
   └── us/                 ← TÚ la creas (y/o eu/)
   ```

   **Opción B — dentro de una carpeta `assets`:**

   ```
   C:\Juegos\DBZ3\
   ├── dbz3.exe            ← ya viene aquí
   ├── rexruntime.dll      ← ya viene aquí
   ├── ... (DLLs)          ← ya vienen aquí
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

5. **Ejecuta `dbz3.exe`** (doble clic).

6. En la ventana del launcher elige **Región** (USA / EU PAL), **Idioma**,
   **Vídeo** y **Audio**, y pulsa **Play**.

> Si algo falla, comprueba que `default.xex` y `us/` (o `eu/`) están en la
> MISMA carpeta, o ambos dentro de `assets/`, tal como en los dibujos del paso 2.

> Para extraer los archivos de tu **ISO legal** usa una herramienta tipo
> `extract-xiso` (lee el FATX de Xbox 360). Ver `baserom.md` del repositorio
> para los tamaños y checksums SHA-256 de cada archivo.

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

- **Vulkan experimental**: el backend Vulkan funciona pero el render 3D es
  ~6.5x más lento que D3D12. Usa **D3D12** (por defecto).
- **Port de personajes PS2/IW→B3**: investigado pero descartado (requiere
  reconstrucción completa del formato; ver `docs/`).

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Bandai Namco, Shueisha, Toei Animation ni ningún
titular de los derechos de Dragon Ball. No se distribuye ningún `.xex`, AFS ni
dato del juego. Los archivos del juego son de tu copia legal.

Repositorio: https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection
