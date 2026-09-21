# Fix del volcado de texturas (issue #11) — 2026-09-21

> Publicado en **v1.2.8**. Afecta a **Windows/D3D12** (el volcado es una funcion
> de la capa D3D12; en Linux/Vulkan no existe).

## Sintoma

El usuario `mellisxboxkp` (issue #11, "Error al dumpear texturas") activaba el
modo dev, activaba el volcado de texturas, elegia una carpeta y jugaba: **no
aparecia ningun fichero**. La carpeta se quedaba vacia.

## Causa raiz: registro de cvars COMPARTIDO + registro duplicado descartado

El registro de cvars (`rex::cvar::GetRegistry()`) es **unico y compartido** entre
el ejecutable (`dbz3.exe`) y los plugins, porque `cvar.cpp` vive en `rexcore`,
una libreria **OBJECT** cuyos objetos se compilan dentro de `rexruntime.dll`, que
es lo que enlazan tanto el exe como `rexgpu-xenos.dll`.

En cambio, el **storage** de cada cvar (`FLAGS_<nombre>_storage_()`) es un
`static` **por modulo**: cada DLL que define la cvar tiene el suyo.

`RegisterFlag` **rechaza la segunda definicion del mismo nombre**:

```
[error] cvar: duplicate registration of 'dbz3_texture_dump'; second registration ignored
```

Como el launcher se carga antes que el plugin, la entrada que queda en el
registro es la **del launcher**, y su setter escribe el storage **del launcher**.

Pero el plugin leia el volcado con `REXCVAR_GET`:

```cpp
const std::string dump_dir = REXCVAR_GET(dbz3_texture_dump);  // storage del plugin
```

Ese storage **no lo escribia nadie** (su registro fue descartado), asi que
siempre valia `""` y `DumpTextureToDds` salia por la primera rama:

```cpp
if (dump_dir.empty()) return;   // nunca se volcaba nada
```

Lo mismo pasaba con `dbz3_texture_packs`, pero los packs **no** fallaban porque
se leen con `REXCVAR_QUERY` (resolucion por nombre a traves del registro, que
devuelve el valor del launcher):

```cpp
const std::string list = REXCVAR_QUERY(std::string, dbz3_texture_packs);
```

> ⚠️ La nota historica "el plugin lee su propio registro del TOML" era
> **incorrecta**: el plugin nunca lee el TOML. Lo que persistia la ruta era el
> launcher (su storage -> `SaveConfig`), y el plugin no la veia.

## Fix

1. `rexglue-sdk-0.10/src/graphics/d3d12/texture_cache.cpp`: el volcado lee la
   ruta con **`REXCVAR_QUERY(std::string, dbz3_texture_dump)`** (igual que los
   packs) y **ya no define** la cvar (la define el launcher).
2. `rexglue-sdk-0.10/src/graphics/dbz3_texture_pack.cpp`: se elimina tambien la
   definicion duplicada de `dbz3_texture_packs` (solo generaba el error de
   registro duplicado; el valor se lee por `REXCVAR_QUERY`).

`dbz3_texture_dump_max` no cambia: solo lo define el plugin, asi que su
`REXCVAR_GET` si funciona.

## Verificacion (medida, mismo TOML y mismo arnés)

Arnes: `%TEMP%\opencode\dump_test.ps1` (escribe `dbz3_texture_dump` en el
`dbz3_user.toml`, arranca el juego con `dbz3_skip_launcher=true`, oculta la
ventana, espera y cuenta los `.dds`).

| Build | DDS | Log |
|---|---|---|
| **DLL publicada de v1.2.7** | **0** | `duplicate registration of 'dbz3_texture_dump'` |
| **DLL con el fix** | **96** (2,5 MB) | `dbz3: volcado de texturas activado en '...'` |

Tras el fix, con el pack de prueba hecho con los propios DDS volcados
(`mods/_vtest`, 5 ficheros en formato de pack):

```
dbz3: texture packs detected ... : 1 -> '...\mods\_vtest'
dbz3: pack de texturas '_vtest' -> 5 texturas
dbz3: pack '_vtest' reemplaza 128x512 (fmt 19) -> 128x512 (x1)
dbz3: pack '_vtest' subido 128x512 (10 niveles, 523776 B)
```

Y el log queda **sin** lineas `duplicate registration` ni `[error]`.

## Nota para el futuro

Si se anade una cvar `dbz3_*` que **tambien** defina el launcher, en el plugin
hay que leerla con **`REXCVAR_QUERY`**, nunca con `REXCVAR_GET`: la definicion del
plugin se descarta y su storage se queda en el valor por defecto.
