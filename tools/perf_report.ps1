# Informe de rendimiento a partir de los logs de dbz3 (lineas `dbz3: perf`).
# Uso: powershell -ExecutionPolicy Bypass -File tools\perf_report.ps1 [-Count 3]
# Muestra el resumen de los ultimos N logs y guarda el texto en
# %TEMP%\opencode\perf_report.txt para pegarlo tal cual.
param(
  [int]$Count = 3,
  [switch]$Here
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logsDir = Join-Path $root 'out\build\win-amd64-release\logs'
if (-not (Test-Path $logsDir)) { Write-Host "No existe: $logsDir"; exit 1 }

$logs = Get-ChildItem -LiteralPath $logsDir -Filter 'dbz3_*.log' |
  Sort-Object LastWriteTime -Descending | Select-Object -First $Count
if (-not $logs) { Write-Host "No hay logs en $logsDir"; exit 1 }

$rx = [regex]'dbz3: perf fps=([\d.]+) frames=(\d+) window=([\d.]+)s max_frame_ms=([\d.]+)'
$report = New-Object System.Collections.Generic.List[string]
function Add([string]$s) { Write-Host $s; $report.Add($s) }
$verdict = 'sin datos'

foreach ($log in $logs) {
  $txt = @(Get-Content -LiteralPath $log.FullName)
  $cfgLine = ($txt | Select-String -Pattern 'applied runtime settings ->' | Select-Object -First 1)
  $cfg = if ($cfgLine) { ($cfgLine.Line -replace '^.*applied runtime settings -> ', '') } else { '(sin linea de configuracion)' }

  $fps = @(); $frames = @(); $mx = @()
  foreach ($m in $rx.Matches([string]::Join("`n", $txt))) {
    $fps += [double]$m.Groups[1].Value
    $frames += [int]$m.Groups[2].Value
    $mx += [double]$m.Groups[4].Value
  }
  $errs = @($txt | Select-String -Pattern '\[error\]|\[critical\]').Count

  Add ("=== {0}   {1:yyyy-MM-dd HH:mm} ===" -f $log.Name, $log.LastWriteTime)
  Add ("  config : {0}" -f $cfg)
  if ($fps.Count -eq 0) {
    Add "  perf   : SIN LINEAS de perf (sesion en el launcher, muy corta, o dbz3_perf_logging=false)"
    Add ""
    continue
  }
  $min = ($fps | Measure-Object -Minimum).Minimum
  $avg = ($fps | Measure-Object -Average).Average
  $max = ($fps | Measure-Object -Maximum).Maximum
  $worst = ($mx | Measure-Object -Maximum).Maximum
  $slow = @($fps | Where-Object { $_ -lt 58 }).Count
  Add ("  ventanas de 5 s: {0}   fps min/med/max: {1:N1} / {2:N1} / {3:N1}   peor frame: {4:N1} ms   ventanas<58fps: {5}   errores: {6}" -f `
      $fps.Count, $min, $avg, $max, $worst, $slow, $errs)
  for ($i = 0; $i -lt $fps.Count; $i++) {
    Add ("    fps={0:N1}  max_frame_ms={1:N1}  frames={2}" -f $fps[$i], $mx[$i], $frames[$i])
  }
  Add ""
  if ($slow -gt 0) { $verdict = 'HAY CAIDAS (alguna ventana por debajo de 58 fps)' }
  elseif ($verdict -ne 'HAY CAIDAS') { $verdict = 'todo a 60 fps' }
}

Add "VEREDICTO: $verdict"
$out = Join-Path $env:TEMP 'opencode\perf_report.txt'
[System.IO.File]::WriteAllLines($out, $report)
Add "Resumen guardado en: $out"
if ($Here) { Start-Process notepad $out }
