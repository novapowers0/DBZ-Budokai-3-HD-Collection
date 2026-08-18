# Cómo hacer mods en DBZ Budokai 3 HD Collection

> Actualizado: 2026-08-18. Pipeline CORRECTO validado (override por entrada +
> mid-insert virtual).

---

## 1. LOS DOS TIPOS DE MOD

### 1.1 Override de archivo completo (reemplaza un AFS entero)
```
mods/<mod>/us/data_cmn.afs        ← archivo AFS completo (293MB)
mods/<mod>/eu/data_cmn.afs
```
- Usado por `og_music`.
- El hook `AfsFindModFileOverride` lo sirve.
- **Desventaja**: hay que reconstruir el AFS entero (build_afs.py). Ya NO es
  necesario para swaps de modelo/textura.

### 1.2 Override por entrada (reemplaza UN bin dentro del AFS) — RECOMENDADO
```
mods/<mod>/us/data_cmn.afs/327/geom.bin     ← formato CARPETA
mods/<mod>/us/data_cmn.afs/327              ← formato ARCHIVO DIRECTO
```
- El hook `AfsFindModOverride` lo sirve.
- **No hay que reconstruir el AFS** — solo el bin de una entrada (~100KB).
- **Cualquier tamaño de bin**: si excede el slot, el runtime aplica el
  **mid-insert virtual** (ver §2.4). 2+ mods simultáneos (entradas distintas).

---

## 2. PASOS PARA HACER UN MOD DE MODELO

### Paso 0: conocer la entrada correcta
La entrada = índice de la tabla del AFS (offset 8 del archivo, `entry_count` en +4).
- Krillin visible = **entrada 327** (NO 326). Verificado por instrumentación.
- El runtime loguea las lecturas: `AFS327 READ` en `logs/dbz3_*.log`.

### Paso 1: extraer el bin original
```powershell
# 1. Localizar la entrada en el AFS (tabla en offset 8)
# 2. Extraer los bytes comprimidos LZX
# 3. Descomprimir con xbdecompress
xbdecompress.exe <entrada.lzx> <entrada.bin>
```

### Paso 2: modificar el bin
- Con `awo_tools/analyze_bin_hd.py` puedes ver la estructura (AWGs, vértices).
- Para swaps nativos usa `swap_b3.py` o la pestaña Model Swap del launcher.

### Paso 3: comprimir con /N:2048 (IMPORTANTE)
```powershell
xbcompress.exe /N:2048 <bin_plano> <bin_comprimido.lzx>
```
> ⚠️ NO usar `/N:32` ni `/N:64` — producen bins más grandes que el slot → crash.

### Paso 4: paddear al to_read
El guest aloca `to_read = ceil(size_tabla/0x1000)*0x1000`. El bin comprimido se
rellena con ceros hasta ese tamaño:
- **Si cabe** en el slot (bin ≤ to_read): pad al to_read del slot.
- **Si es mayor** (p.ej. Goten 107006 > Krillin 106496): pad al
  `to_read_virtual = ceil(bin/0x1000)*0x1000` (110592). El runtime lo autoriza
  con el mid-insert virtual: la entrada crece in-place en la tabla virtual y las
  posteriores se desplazan +delta. El guest aloca el buffer correcto y recibe el
  bin completo sin truncar.

`swap_b3.py` hace este padding automáticamente.

### Paso 5: instalar
```powershell
# Crear la estructura de carpeta del mod
New-Item -ItemType Directory -Path "mods/<mod>/us/data_cmn.afs/327" -Force
Copy-Item padded.bin "mods/<mod>/us/data_cmn.afs/327/geom.bin"

# Activar (eliminar .disabled si existe)
Remove-Item "mods/<mod>/.disabled"
```

### Paso 6: verificar en logs
```
logs/dbz3_001.log:
  AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
  AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```
> `got=to_read` = el guest recibió el bin completo. Si `got < to_read` → falta padding.

---

## 2.4 🔴 MID-INSERT VIRTUAL (2026-08-18)

El guest lee cada entrada del `data_cmn.afs` con un buffer de
`to_read = ceil(size/0x1000)*0x1000` derivado de la tabla AFS. Un bin de mod
mayor que ese to_read se truncaba al servirse por override → crash.

La solución del runtime (parche en `patches/`, archivos `afs.cpp`/`afs.h`/
`host_path_file.cpp`) presenta al guest una **tabla AFS virtual CONSISTENTE**:

- `AfsGetVirtualTable()`: si un override excede el `to_read` del slot, la
  entrada **crece in-place** (slot alineado a 0x800) y **todas las entradas
  posteriores se desplazan** por el delta acumulado — replicando exactamente un
  rebuild con mid-insert. Los addr virtuales son coherentes → el guest las
  encuentra correctamente (a diferencia del intento "naive" que inflaba sizes
  manteniendo addr: el guest recalcula offsets acumulando sizes → crash).
- `AfsTranslateOffset()`: para las lecturas de datos, traduce virtual → físico
  (resta el delta de la entrada) y sirve el override (bin completo) o lee del
  archivo físico en el offset traducido.

**Criterio de crecimiento**: solo crece si el override > `to_read` (lo que el
guest ya aloca), NO si excede el slot físico. Un mod que cabe (p.ej. texturas
de Gero, 114688 = to_read) no desplaza nada.

**Resultado**: swaps nativos B3→B3 que pesan ~100KB, en **cualquier dirección**
(el bin puede ser mayor o menor que el slot), y 2+ mods de modelo/textura
activos simultáneamente.

---

## 3. TAMAÑOS DE SLOT DE ENTRADAS CLAVE

| Entrada | Personaje | Slot comprimido | to_read |
|---|---|---|---|
| 327 | Krillin (visible) | 105296 | 106496 |
| 328 | Krillin Buu Saga | 104404 | 106496 |
| 329 | Krillin Namek | 101268 | 102400 |
| 298 | Goten | 107006 | 110592 |
| 270 | Goku | 128062 | 130048 |

> Con el mid-insert virtual el bin YA NO tiene que caber en el slot de la
> entrada destino: si lo excede, la tabla virtual hace crecer la entrada y
> desplaza las posteriores automáticamente.

---

## 4. CÓMO ACTIVAR/DESACTIVAR MODS

- **Activo**: carpeta `mods/<mod>/` SIN archivo `.disabled`.
- **Desactivado**: con `.disabled`.
- **Orden**: los mods se ordenan alfabéticamente; el primer match gana.
- El toml `dbz3_user.toml` → `dbz3_enabled_mods` controla el overlay del launcher
  (para AFS completos). El override por entrada es independiente del toml.

