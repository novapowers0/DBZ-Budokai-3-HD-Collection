# Fija la configuracion de video para las pruebas de rendimiento (escala interna
# y MSAA) en dbz3_user.toml, de forma reproducible y sin romper el TOML.
# Uso: powershell -ExecutionPolicy Bypass -File tools\perf_test_config.ps1 -Scale 3 -Msaa on
#      powershell -ExecutionPolicy Bypass -File tools\perf_test_config.ps1 -Show
param(
  [ValidateSet(1, 2, 3, 4)][int]$Scale = 2,
  [ValidateSet('on', 'off')][string]$Msaa = 'on',
  [ValidateSet(1, 2, 3, 4)][int]$Upscale = 1,
  [switch]$Show
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$toml = Join-Path $root 'out\build\win-amd64-release\dbz3_user.toml'
if (-not (Test-Path $toml)) { Write-Host "No existe: $toml"; exit 1 }

if ($Show) {
  Write-Host "=== $toml ==="
  Get-Content -LiteralPath $toml
  exit 0
}

$msaaBool = ($Msaa -eq 'on')
# Los valores de texto van entre comillas: sin comillas el parser descarta TODO
# el fichero y el juego arranca con los valores por defecto.
$set = [ordered]@{
  'dbz3_quality_preset'    = '"manual"'
  'dbz3_resolution_scale'  = "$Scale"
  'dbz3_native_2x_msaa'    = "$($msaaBool.ToString().ToLower())"
  'draw_resolution_scale_x' = "$Scale"
  'draw_resolution_scale_y' = "$Scale"
  'native_2x_msaa'         = "$($msaaBool.ToString().ToLower())"
  'dbz3_hd_textures'       = "$Upscale"  # 1 = off; 2-4 = texturas HD (filtro interno)
  'dbz3_perf_logging'      = 'true'   # linea dbz3: perf cada 5 s
}

Copy-Item -LiteralPath $toml -Destination "$toml.bak" -Force
$out = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($line in (Get-Content -LiteralPath $toml)) {
  if ($line -match '^\s*([A-Za-z0-9_\-]+)\s*=') {
    $k = $Matches[1]; $seen[$k] = $true
    if ($set.Contains($k)) { $out.Add("$k = $($set[$k])") } else { $out.Add($line) }
  } else { $out.Add($line) }
}
foreach ($k in $set.Keys) { if (-not $seen.ContainsKey($k)) { $out.Add("$k = $($set[$k])") } }
[System.IO.File]::WriteAllLines($toml, $out)
Remove-Item -LiteralPath "$toml.bak" -Force

Write-Host "Configuracion de prueba aplicada:"
Write-Host ("  escala interna : {0}x" -f $Scale)
Write-Host ("  MSAA nativo 2x : {0}" -f $Msaa)
if ($Upscale -gt 1) {
  Write-Host ("  texturas HD    : x{0} (upscale en runtime)" -f $Upscale)
} else {
  Write-Host "  texturas HD    : off (x1)"
}
Write-Host ""
Write-Host "Ahora ejecuta out\build\win-amd64-release\dbz3.exe, juega un combate"
Write-Host "de ~60 s, sal del juego y ejecuta tools\perf_report.cmd"
Write-Host ""
Get-Content -LiteralPath $toml | Select-String -Pattern 'resolution_scale|native_2x|quality_preset|texture_upscale|perf_logging'
