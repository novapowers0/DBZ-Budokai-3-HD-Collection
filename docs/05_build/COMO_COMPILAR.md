# Cómo compilar

> Actualizado: 2026-08-14.

---

## 1. COMPILAR EL JUEGO (release)

```powershell
cmake --build "out\build\win-amd64-release"
```

El build usa el SDK **instalado** en `rexglue\` (no el fuente `rexglue-sdk\`).

---

## 2. COMPILAR EL SDK (rexglue-sdk) → produce rexruntime.dll

```powershell
cmake -G Ninja -S rexglue-sdk -B rexglue-sdk\out\build-win-vulkan `
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" `
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" `
  -DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe" `
  -DREXGLUE_ENABLE_FIDELITYFX=ON -DREXGLUE_USE_VULKAN=ON `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64-v3"

# Compilar solo el runtime (rápido, incremental)
ninja -C rexglue-sdk\out\build-win-vulkan rexruntime
```

El DLL resultante está en `rexglue-sdk\out\win-amd64\rexruntime.dll`.

### Instalarlo en el build del juego
```powershell
Copy-Item "rexglue-sdk\out\win-amd64\rexruntime.dll" "out\build\win-amd64-release\rexruntime.dll" -Force
```

> ⚠️ Si modificas `rexglue-sdk/src/filesystem/afs.cpp` (el hook de mods), tienes
> que recompilar el SDK Y copiar el DLL al build. El build del juego NO se
> recompila solo para cambios del SDK.

---

## 3. COMPRIMIR/DESCOMPRIMIR BINS (LZX)

Herramientas en `mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\`.

```powershell
# Comprimir (¡usar /N:2048! el tamaño del bloque del juego)
xbcompress.exe /N:2048 <src.bin> <dst.lzx>

# Descomprimir
xbdecompress.exe <src.lzx> <dst.bin>
```

---

## 4. BUILD TRACY (profiling)

```powershell
cmake --build "out\build\win-amd64-tracy"
```
- Usa DLLs instrumentadas (rexruntimerd.dll, rexgpu-xenosrd.dll, TracyClientrd.dll).
- Para profilar: `tracy-capture.exe -o out.tracy` mientras juegas, luego
  `tracy-csvexport.exe` para análisis.

---

## 5. PRECAUCIÓN

- El build release es el que se usa para jugar. Modifica el SDK con cuidado.
- Hacer backup del `rexruntime.dll` antes de reemplazar (ya hay `.bak_afstest`).
- Ver `docs/01_estructura/ARBOL.md` para la ubicación de cada cosa.
