# tools/cleanup.ps1 - Limpieza manual para el entorno de desarrollo de DBZ3.
#
# Herramienta SOLO para el desarrollador (no se distribuye con el juego, no
# se ejecuta en segundo plano). Clasifica el contenido en niveles y NUNCA toca
# las rutas protegidas (docs, src, assets, mods, SDK, ps2_games, etc.).
#
#   Nivel 0 (L0, seguro):
#     - Caches regenerables: out/analysis/**/.work (los bins extraidos se
#       regeneran con awo_tools/corpus_scan.py; el corpus_all.db se conserva)
#     - Logs rotados (se conservan los N mas recientes por carpeta)
#     - Crash dumps (crash_*.dmp, *.mdmp)
#     - Bitmaps de debug (frontbuf_*.bmp, black_*.bmp)
#     - Temporales (*.tmp, *.bak) solo dentro de out/
#
#   Nivel 1 (L1, manual, requiere -Full -Yes):
#     - release-stage/ (stage de empaquetado antiguo)
#     - out/build/_archivo_* (builds/mods/DLLs archivados)
#
#   Nivel 2 (L2, solo reporte): los 12 mayores consumidores de espacio.
#
# Uso (manual):
#   .\cleanup.ps1 -DryRun        # muestra que se borraria, no borra nada
#   .\cleanup.ps1                # limpia L0 (pide confirmacion si hay)
#   .\cleanup.ps1 -Yes           # L0 sin preguntar
#   .\cleanup.ps1 -Full -Yes     # L0 + L1 (archivados) sin preguntar

param(
  [switch]$DryRun,
  [switch]$Full,
  [switch]$Yes,
  [int]$KeepLogs = 5,
  [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Continue'
$script:LogFile = Join-Path $PSScriptRoot 'cleanup.log'

function Write-Log($msg) {
  $line = "[{0:yyyy-MM-dd HH:mm:ss}] {1}" -f (Get-Date), $msg
  Add-Content -LiteralPath $script:LogFile -Value $line -ErrorAction SilentlyContinue
  Write-Host $msg
}

function Get-DirSizeMB($path) {
  if (-not (Test-Path -LiteralPath $path)) { return 0 }
  $s = (Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum
  if (-not $s) { return 0 }
  return [math]::Round($s / 1MB, 1)
}

# ---------- Rutas protegidas (nunca se tocan) ----------
$protected = @(
  'docs', 'src', 'us', 'eu', 'ps2_games', 'mod center', 'mod center hd',
  'modding resources', 'modding resources update', 'modding resources update 2',
  'modding resources update 3', 'modding resources discord',
  'generated', 'generated_eu', 'awo_tools', 'tools', 'bin', 'github',
  'rexglue', 'rexglue-sdk-0.10'
) | ForEach-Object { (Join-Path $Root $_) }

# Los mods del usuario son sagrados (activos y desactivados, enseñan cómo se hizo).
$modsProtected = @(
  (Join-Path $Root 'mods'),
  (Join-Path $Root 'out\build\win-amd64-release\mods'),
  (Join-Path $Root 'out\build\win-amd64-dual\mods')
)

# ---------- Medir total ----------
Write-Host ""
Write-Host "== Red de seguridad de peso DBZ3 =="
Write-Host ("Root: {0}" -f $Root)
$total = (Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue |
          Measure-Object -Property Length -Sum).Sum
$totalGB = [math]::Round($total / 1GB, 2)
Write-Host ("Total actual: {0} GB" -f $totalGB)

# ---------- Recolectar limpiables ----------
$clean = @()  # items con Path, MB, Categoria
function Add-Clean($path, $cat) {
  if (-not (Test-Path -LiteralPath $path)) { return }
  $mb = Get-DirSizeMB $path
  if ($mb -le 0) { return }
  $clean += [PSCustomObject]@{ Path = $path; MB = $mb; Cat = $cat }
}

# L0: caches .work regenerables bajo out/analysis
Get-ChildItem -LiteralPath (Join-Path $Root 'out\analysis') -Directory -Recurse -Filter '.work' -ErrorAction SilentlyContinue |
  ForEach-Object { Add-Clean $_.FullName 'L0 corpus .work' }

# L0: logs (rotar: conservar los KeepLogs mas recientes por carpeta)
$logDirs = Get-ChildItem -LiteralPath $Root -Recurse -Directory -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -match '\\logs$' -or $_.FullName -match '\\out\\build' } |
           Select-Object -First 20
foreach ($d in $logDirs) {
  $logs = Get-ChildItem -LiteralPath $d.FullName -Filter '*.log' -File -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime -Descending
  $toDel = $logs | Select-Object -Skip $KeepLogs
  foreach ($l in $toDel) { Add-Clean $l.FullName 'L0 logs' }
}

# L0: dumps, bitmaps de debug, temporales dentro de out/
Get-ChildItem -LiteralPath (Join-Path $Root 'out') -Recurse -Include '*.dmp','*.mdmp','*.bmp','*.tmp','*.bak' -File -ErrorAction SilentlyContinue |
  ForEach-Object { Add-Clean $_.FullName 'L0 dumps/bmp/tmp' }
Get-ChildItem -LiteralPath $Root -Filter 'crash_*.dmp' -File -ErrorAction SilentlyContinue |
  ForEach-Object { Add-Clean $_.FullName 'L0 crash dump' }

# L1 (solo -Full): staging y archivados
if ($Full) {
  Add-Clean (Join-Path $Root 'release-stage') 'L1 release-stage'
  Get-ChildItem -LiteralPath (Join-Path $Root 'out\build') -Directory -Filter '_archivo_*' -ErrorAction SilentlyContinue |
    ForEach-Object { Add-Clean $_.FullName 'L1 archivo' }
}

# ---------- Reporte ----------
$sumMB = ($clean | Measure-Object -Property MB -Sum).Sum
if (-not $sumMB) { $sumMB = 0 }
Write-Host ""
Write-Host ("Limpiable: {0:N1} MB en {1} items" -f $sumMB, $clean.Count)
if ($clean.Count -gt 0) {
  $clean | Sort-Object MB -Descending | Select-Object -First 15 |
    Format-Table -AutoSize @{N='MB';E={$_.MB}}, Cat, Path | Out-String | Write-Host
}

# ---------- L2: mayores consumidores (reporte, siempre visible) ----------
Write-Host ""
Write-Host "== Top 12 consumidores (para vigilancia) =="
Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue |
  ForEach-Object {
    $mb = Get-DirSizeMB $_.FullName
    [PSCustomObject]@{ MB = $mb; Dir = $_.Name }
  } | Sort-Object MB -Descending | Select-Object -First 12 |
  Format-Table -AutoSize @{N='GB';E={[math]::Round($_.MB/1024,2)}}, Dir | Out-String | Write-Host

if ($DryRun) {
  Write-Host "DRY RUN: nada borrado."
  exit 0
}

if ($clean.Count -eq 0) {
  Write-Host "Nada que limpiar."
} else {
  if (-not $Yes) {
    $r = Read-Host "Borrar estos $($clean.Count) items? [y/N]"
    if ($r -notin @('y','Y')) { Write-Host "Cancelado."; exit 1 }
  }
  $freed = 0
  foreach ($c in $clean) {
    Remove-Item -LiteralPath $c.Path -Recurse -Force -ErrorAction SilentlyContinue
    $freed += $c.MB
    Write-Log ("borrado {0} ({1:N1} MB, {2})" -f $c.Path, $c.MB, $c.Cat)
  }
  Write-Host ("Liberado: {0:N1} MB" -f $freed)
  $total2 = (Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue |
             Measure-Object -Property Length -Sum).Sum
  Write-Host ("Nuevo total: {0:N2} GB" -f ($total2 / 1GB))
}