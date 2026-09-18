# Analiza un log (o los ultimos N) centrandose en el upscale de texturas HD:
# pipeline, texturas aceptadas (formato/tamano), progreso de `upx=`, rendimiento
# y errores. Uso: powershell -ExecutionPolicy Bypass -File tools\upscale_report.ps1 [-Count 3] [-Log <ruta>]
param(
  [int]$Count = 3,
  [string]$Log = ""
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logsDir = Join-Path $root 'out\build\win-amd64-release\logs'
if ($Log) { $logs = @(Get-Item -LiteralPath $Log) }
else {
  $logs = Get-ChildItem -LiteralPath $logsDir -Filter 'dbz3_*.log' |
    Sort-Object LastWriteTime -Descending | Select-Object -First $Count
}

$rxPerf = [regex]'dbz3: perf fps=([\d.]+) frames=(\d+) window=([\d.]+)s max_frame_ms=([\d.]+)(?: upx=(\d+))?'
foreach ($f in $logs) {
  $txt = @(Get-Content -LiteralPath $f.FullName)
  $cfg = ($txt | Select-String -Pattern 'applied runtime settings ->' | Select-Object -Last 1)
  $cfgTxt = if ($cfg) { ($cfg.Line -replace '^.*applied runtime settings -> ', '') } else { '(sin config)' }
  $pipe = ($txt | Select-String -Pattern 'upscale pipeline ready')
  $acc = $txt | Select-String -Pattern 'upscale ACCEPT fmt=(\d+) dim=(\d+) (\d+)x(\d+) factor=(\d+)'
  $accFirst = ($txt | Select-String -Pattern 'upscale skip \[' | Group-Object { $_.Matches } | Out-Null)
  $skips = $txt | Select-String -Pattern 'upscale skip \[(\w+)\]'
  $perf = foreach ($m in $rxPerf.Matches([string]::Join("`n", $txt))) {
    [pscustomobject]@{ Fps = [double]$m.Groups[1].Value; Max = [double]$m.Groups[4].Value;
                       Upx = $m.Groups[5].Value }
  }
  $errs = @($txt | Select-String -Pattern '\[error\]|\[critical\]')

  Write-Host ("=== {0}   {1:yyyy-MM-dd HH:mm}   lineas={2} ===" -f $f.Name, $f.LastWriteTime, $txt.Count)
  Write-Host ("  config    : {0}" -f $cfgTxt)
  if ($pipe) { Write-Host ("  upscale   : {0}" -f ($pipe[0].Line -replace '^.*(DBZ3 texture upscale.*)$', '$1')) }
  else { Write-Host "  upscale   : pipeline NO inicializado (x1 / off)" }
  # Motivos de descarte (agrupados)
  if ($skips.Count) {
    $g = $skips | ForEach-Object { ($_.Line -replace '^.*upscale skip \[(\w+)\].*$', '$1') } | Group-Object
    Write-Host ("  descartes : {0}" -f (($g | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join '  '))
  }
  if ($acc.Count) {
    Write-Host ("  ACCEPT    : {0} texturas" -f $acc.Count)
    $acc | ForEach-Object {
      Write-Host ("      {0}" -f ($_.Line -replace '^.*(upscale ACCEPT.*)$', '$1'))
    }
  } else {
    Write-Host "  ACCEPT    : ninguna textura escalada (formato/tamano no elegibles, o no se abrio ningun combate)"
  }
  if ($perf.Count) {
    $fps = $perf | ForEach-Object { $_.Fps }
    $mx = $perf | ForEach-Object { $_.Max }
    $upx = $perf | Where-Object { $_.Upx } | ForEach-Object { [int]$_.Upx }
    $slow = @($fps | Where-Object { $_ -lt 58 }).Count
    Write-Host ("  perf      : {0} ventanas  fps min/med/max {1:N1}/{2:N1}/{3:N1}  peor frame {4:N1} ms  ventanas<58: {5}" -f `
      $perf.Count, ($fps | Measure-Object -Minimum).Minimum, ($fps | Measure-Object -Average).Average, `
      ($fps | Measure-Object -Maximum).Maximum, ($mx | Measure-Object -Maximum).Maximum, $slow)
    if ($upx.Count) {
      Write-Host ("  upx       : primero={0}  ultimo={1}  max={2}  (texturas escaladas acumuladas)" -f `
        $upx[0], $upx[$upx.Count - 1], ($upx | Measure-Object -Maximum).Maximum)
    }
  } else { Write-Host "  perf      : sin lineas (sesion en el launcher)" }
  Write-Host ("  errores   : {0}" -f $errs.Count)
  foreach ($e in ($errs | Select-Object -First 3)) { Write-Host ("      " + ($e.Line -replace '^\[[^\]]+\] \[[^\]]+\] \[[^\]]+\] ', '')) }
  Write-Host ""
}
