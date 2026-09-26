# dbz3 - Copia las DLLs canonicas del SDK baseline al build del juego.
#
# Por que existe: `cmake --build out\build\win-amd64-release` copia a la carpeta
# de salida las DLLs STALE de `rexglue/bin` (que ademas son del build avx2), asi
# que despues de CADA compilacion del juego el build vuelve a tener DLLs viejas.
# Un test lanzado asi no mide el cambio que se esta probando (y un release hecho
# a mano podria empaquetar DLLs equivocadas). Ver AGENTS.md seccion 7.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\copy_sdk_dlls.ps1
#   ... -BuildDir out\build\win-amd64-dual      (otro destino)
#
# El release de verdad no depende de esto: tools\make_release.ps1 toma las DLLs
# del SDK baseline por su cuenta (y verify_release.ps1 lo comprueba por SHA256).

param(
  [string]$BuildDir = "out\build\win-amd64-release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-baseline"
$dst = Join-Path $root $BuildDir

if (-not (Test-Path -LiteralPath $src)) { Write-Error "No existe el SDK baseline: $src"; exit 1 }
if (-not (Test-Path -LiteralPath $dst)) { Write-Error "No existe el build destino: $dst"; exit 1 }

if (Get-Process dbz3 -ErrorAction SilentlyContinue) {
  Write-Error "dbz3.exe esta en ejecucion: cierralo antes de copiar las DLLs (ficheros bloqueados)"
  exit 1
}

foreach ($dll in @("rexgpu-xenos.dll", "rexruntime.dll", "amd_fidelityfx_dx12.dll")) {
  $from = Join-Path $src $dll
  $to = Join-Path $dst $dll
  if (-not (Test-Path -LiteralPath $from)) { Write-Error "Falta el canonico: $from"; exit 1 }
  $before = if (Test-Path -LiteralPath $to) { (Get-Item -LiteralPath $to).Length } else { 0 }
  Copy-Item -LiteralPath $from -Destination $to -Force
  $after = (Get-Item -LiteralPath $to).Length
  $flag = if ($before -ne 0 -and $before -ne $after) { " (reemplazada: $before -> $after)" } else { "" }
  Write-Output ("{0}: {1} bytes{2}" -f $dll, $after, $flag)
}

# Marca de coherencia: el sello de version del runtime tiene que estar dentro.
$markers = @{ "rexruntime.dll" = "Build de rexruntime"; "rexgpu-xenos.dll" = "Build de rexgpu" }
foreach ($dll in $markers.Keys) {
  $txt = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Join-Path $dst $dll)))
  if ($txt -notmatch $markers[$dll]) {
    Write-Output ("AVISO: {0} no contiene {1} - ¿es una DLL vieja?" -f $dll, $markers[$dll])
  }
}
Write-Output "copy_sdk_dlls: DLLs canonicas copiadas a $BuildDir"
