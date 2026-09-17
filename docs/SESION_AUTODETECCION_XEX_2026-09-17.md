# Sesión 2026-09-17 — Auto-detección del ejecutable (v1.2.2)

> Objetivo: que el launcher arranque el juego **siempre**, con el ejecutable en
> cualquier nombre/ubicación (volcado retail del disco, ISO original, carpetas
> anidadas), sin que el usuario tenga que renombrar ni entender nada.

---

## 1. Síntoma y causa raíz

Un usuario (RTX 5090 / 9950X3D) reportó **"pulso Play y no pasa nada"**. En sus
logs, TODOS los intentos morían igual:

```
XThread::Execute - No function registered at 820D54C8
```

Diagnóstico (a partir de `docs/07_ports/PLAN_PS2_B3/01_WEB.md`, que documenta el
contenido del disco):

| Archivo del disco | Tamaño | Qué es |
|---|---|---|
| `default.xex` (raíz) | 3 317 760 B | **MENÚ de la HD Collection** (elígelo: B1/B2/B3) |
| `DBZ1/yae1_xenon.xex` | 4 464 640 B | Budokai 1 |
| `DBZ3/yae3_xenon.xex` | 4 890 624 B | **Budokai 3 (el nuestro)** |

El launcher arrancaba `game:\default.xex` (la raíz del disco → el **menú**) y el
núcleo recompilado (sólo Budokai 3) no tiene la función de entrada de ese
ejecutable → el invitado moría con un error críptico.

Hallazgos secundarios de la misma sesión:

1. **Modo ISO inservible con ISO retail**: `ExtractGameXexFromIso` sólo probaba
   `default.xex` (el menú) y montaba la raíz, pero los datos viven en `DBZ3\`.
2. **Un xex desconocido no bloqueaba** el botón Play.
3. **Nada del xex se registraba en el log** (`OnConfigurePaths` corre antes de
   que el logging esté inicializado → sus líneas se pierden).
4. **`dbz3_user.toml` no parseaba**: `rex::cvar::SaveConfig` escribe los valores
   crudos, así que una ruta `E:\Game Roms\…` producía
   `unknown escape sequence '\G'` y **se perdían TODOS los ajustes** en cada
   arranque.

---

## 2. Diseño de la solución

**Fuente única de verdad**: `ResolveBootSource()` devuelve un `BootSource`
{ `xex` (lo que se sirve como `default.xex`), `data_root`, `status`, `redirect`,
`note` } que usan el pre-flight, el VFS (shims de dispositivo) y la UI.

Flujo dentro del launcher:

1. `CheckDefaultXex(<root>)` sobre las rutas canónicas (`<root>\default.xex`,
   `<root>\assets\…`). Si el status es válido → **no se copia nada**.
2. Si no hay ejecutable válido → `FindGameExecutable(<root>)`:
   - spots convencionales de los roots vecinos: `root`, `DBZ3`, `assets`,
     `assets/DBZ3` (y `default.xex`);
   - escaneo **acotado** del root elegido: profundidad ≤ 3, tope de 4000
     directorios, saltando `user_data`/`logs`/`mods`/`$RECYCLE.BIN`/…;
   - identificación por **tamaño + MD5** (US/EU), no por nombre.
3. `EnsureXexCache()` copia el ejecutable encontrado a
   `user_data/dbz3/xex_cache/default.xex` (**sólo si hace falta**; nunca se
   escribe en la carpeta del usuario) y fija el data root (`DBZ3\` si el xex vino
   de ahí).
4. `RegionDiscDevice` (ISO) y `GameDataHostDevice` (carpeta) sirven
   `game:\default.xex` desde la caché y resuelven `us\…`/`eu\…` bajo `DBZ3\`
   cuando los datos viven ahí (con fallback a la ruta original).
5. En modo ISO, `ExtractGameXexFromIso` prueba en orden:
   `default.xex`, `DBZ3/yae3_xenon.xex`, `DBZ3/yae3_xenon_eu.xex`,
   `yae3_xenon.xex`, `yae3_xenon_eu.xex`… y anota cuál se usó en
   `iso_cache/source.stamp` (junto a ruta+tamaño+fecha del ISO para invalidar).

**Estados del ejecutable** (`XexStatus`): `kUs`, `kEu`, `kDbz1`, **`kHdMenu`**
(nuevo: menú de la HD Collection, 3 317 760 B), `kUnknown`, `kMissing`.
`kHdMenu` y `kDbz1` **bloquean** Play con mensaje específico; `kUnknown` avisa en
ámbar pero deja jugar (puede ser un dump modificado).

**UI**: banner basado en `CurrentBootSource()` con nota azul
"Ejecutable detectado: `yae3_xenon.xex` → `default.xex` (no hay que renombrar
nada)"; PLAY se habilita con un único gate (`assets_ready`, que también cubre la
tecla Enter).

**Config**: `SaveUserSettings` pasa el fichero de `SaveConfig` por
`EscapeTomlStrings`, que escapa `\` y `"` dentro de valores entrecomillados y es
**idempotente** (si `SaveConfig` no reescribe nada, la segunda pasada no dobla
las barras).

---

## 3. Ficheros tocados

| Fichero | Cambio |
|---|---|
| `src/launcher/settings.h/.cpp` | `XexStatus::kHdMenu`, `ClassifyXexFile` (+ pública), `XexStatusLabel`, `GameExecutable`, `FindGameExecutable`, `EnsureXexCache`, `XexCacheDir`, `BootSource`, `ResolveBootSource`/`CurrentBootSource`/`SetCurrentBootSource`, `ExtractGameXexFromIso` (lista de candidatos), `IsoXexSourcePath`, `EscapeTomlStrings`, `IsValidGameDataDir` ampliado |
| `src/main.cpp` | `FindGameRoot` acepta `DBZ3`/`assets/DBZ3`; `OnConfigurePaths` resuelve+fija el boot source y loguea el diagnóstico; pre-flight sobre `boot.xex`; guard de `skip_launcher` usa el status resuelto |
| `src/region.cpp` | `GameDataHostDevice` (modo carpeta: sirve el xex de la caché + remapea a `DBZ3\`), `RegionDiscDevice` extendido (redirect del xex + prefijo `DBZ3\` con fallbacks), `MountIsoDrive`/`RemountGameDrive`/`RelocateGameData` usan el boot source |
| `src/launcher/launcher_state.cpp` | Banner con `CurrentBootSource`, mensaje del menú HD, aviso ámbar para desconocido, nota "Ejecutable detectado" |
| `src/version.rc` | 1.2.1 → **1.2.2** |
| Docs | `AGENTS.md` (§3, §8, §9.2), `docs/README.md`, `docs/01_estructura/ESTADO.md`, `docs/HOJA_DE_RUTA_2026_09.md`, `RELEASE_README.md`, `README.md`/`README_EN.md`, `README_PRIMER_ARRANQUE.txt`, `portforge/.forge.json` |

---

## 4. Verificación (local, 2026-09-17)

Entornos montados en `%TEMP%\opencode\` con **junctions** a `us/` (borrados al
terminar; los assets quedaron intactos: 15 ficheros en `us/`).

| Escenario | Resultado |
|---|---|
| **Dump retail**: raíz `default.xex` = menú 3 317 760 B (relleno) + `DBZ3\yae3_xenon.xex` (real) + `DBZ3\us\` | `FindGameExecutable: found yae3_xenon.xex (US/NA) … -> data root …\disc\DBZ3` → `staged at user_data\dbz3\xex_cache\default.xex` → `RemountGameDrive: MOUNTED …\disc\DBZ3` → **el juego arranca** (guest thread + lecturas AFS desde `DBZ3\us\`); proceso vivo a los 20 s, sin `No function registered` |
| **Layout clásico**: `default.xex` + `us\` en la raíz | Ruta canónica; **no copia nada**; arranca igual que antes |
| **Solo menú HD** (nada bootable) | No se cachea nada; sin crash. Con `skip_launcher` el SDK falla limpio ("Failed to load XEX"); con launcher, banner rojo + PLAY deshabilitado |
| **Escapado del TOML** | Pasada 1 escapa `E:\Game Roms\…` → `E:\\Game Roms\\…`; pasada 2 **idéntica** (idempotente); comillas y enteros intactos |
| `skip_launcher` (dev) | Guard nuevo: si el status no es US/EU, no arranca y avisa |

**Pendiente de verificar**: modo ISO con un **ISO retail real** (no había ninguno
disponible; la lógica está implementada y es la misma que la del modo carpeta,
gateada para no afectar a ISOs ya repackados).

---

## 4.bis Modo ISO — VALIDADO (v1.2.2 EX, 2026-09-17)

Sin un ISO original a mano, se validó montando un **XDVDFS sintético** con
`tools/make_test_iso.py` (generador nuevo: empaqueta una carpeta con el layout
retail — `default.xex` = menú 3317760 B en la raíz, `DBZ3/yae3_xenon.xex` real y
`DBZ3/us` con los 15 ficheros de datos, 2,3 GB).

Pruebas (todas con el exe dual 1.2.2.1):

| Escenario | Resultado |
|---|---|
| **A** — sólo el `.iso` junto a `dbz3.exe` (sin carpeta) | El launcher auto-detecta el ISO, extrae `DBZ3/yae3_xenon.xex` (rechaza el menú), monta el disco con `prefix_dbz3=yes`, crea el hilo del invitado y **el juego arranca** (vivo a los 45 s, sin `No function registered`). |
| **B** — carpeta "válida" pero con el **menú** como `default.xex` + ISO al lado (el caso del usuario) | Salta solo al ISO (`folder cannot boot (...) - using the disc image`), monta igual y **arranca**. |

**2 bugs reales encontrados y corregidos por estas pruebas** (los habría sufrido
cualquier usuario de ISO retail):

1. **`RegionDiscDevice` no normalizaba la ruta**: el VFS entrega la ruta con el
   prefijo desmontado pero **con el separador inicial** (`\us\data_cmn.afs`), y el
   remapeo de región + el prefijo `DBZ3\` exigían que no empezara por `\` → nunca
   se aplicaban → el invitado fallaba con
   `NtCreateFile FAILED: path='D:\us\data_cmn.afs' -> 0xc000000f`. Fix:
   `NormalizeGuestPath()` al entrar en `ResolvePath` (el redirect de `default.xex`
   ya lo hacía, por eso el módulo sí cargaba y sólo fallaban los datos).
2. **Modo ISO con un disco sin ejecutable bootable**: si las candidatas no dan un
   US/EU, ahora **no** se entra en modo ISO (`iso_boot.usable()`); se conserva la
   carpeta y el banner explica el motivo (antes se arrancaba un fichero que el
   runtime no podía cargar → `Unknown module magic: 00000000`).

Nota: en la captura queda un `NtCreateFile FAILED: path='D:\us\'` (petición de
directorio que el VFS canonicaliza a la raíz de la unidad); es inocuo — el juego
continúa y llega al menú.


---

## 5. Notas para el futuro

- **`tools/make_test_iso.py <out.iso> <carpeta>`** genera un XDVDFS de prueba
  desde una carpeta (para validar el modo disco sin un ISO real). El runtime lee
  XDVDFS crudo: descriptor de volumen en el sector 32 con el magic
  `MICROSOFT*XBOX*MEDIA`, entradas de directorio de 14 B + nombre enlazadas como
  árbol cuyos punteros van en unidades de 4 B. Si se añaden más ficheros a una
  carpeta, no olvidar que cada directorio debe caber en un sector.
- **El fallback carpeta→ISO** (v1.2.2 EX) se activa cuando la carpeta no tiene
  ejecutable bootable (`kHdMenu`, `kMissing`) y hay un `.iso` junto a la carpeta
  de datos o al ejecutable. Si el usuario quiere mods, en el launcher puede
   volver a "Carpeta extraida" (eso limpia `dbz3_iso_path`).
- ⚠️ **El exe de release se compila desde `out\build\win-amd64-dual`**
  (verificado: el SHA-256 del `dbz3.exe` del zip v1.2.1 coincide con el de ese
  build dir). `tools/make_release.ps1` ya lo toma de ahí.
- Los logs de `OnConfigurePaths` se pierden (el logging arranca después): el
  diagnóstico del xex aparece al pulsar Play (`RelocateGameData`). Si algún día
  hace falta el log completo, hay que bufferizar las líneas tempranas.
- `FindGameExecutable` sólo hace escaneo recursivo en el **primer** root (la
  carpeta elegida); en los roots vecinos sólo mira los spots convencionales
  (coste acotado).
- Un xex de otra tirada con MD5 distinto → `kUnknown` (aviso, no bloqueo). Si se
  identifican más tiradas, añadir sus MD5 a `ClassifyXexFile`.
