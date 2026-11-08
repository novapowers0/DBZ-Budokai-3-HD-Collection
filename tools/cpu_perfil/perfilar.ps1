param(
  [Parameter(Mandatory=$true)][string]$Dir,
  [string]$Sampler = "D:\tmp\cpu\sampler.exe",
  [string]$Out = "D:\tmp\cpu",
  [int]$Mask = 5,
  [int]$Seconds = 30,
  [string]$Label = "perfil",
  [string]$Thread = "Main XThread",
  [int]$Warm = 2
)
# Arranca una copia propia del juego con el combate de prueba fijo, espera el marcador,
# deja calentar y muestrea el hilo indicado. Deja S_*.txt (muestreo) y R_*.log (log) en -Out.
$ErrorActionPreference = 'Stop'
$dir = $Dir
$logs = Join-Path $dir 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$before = @(Get-ChildItem $logs -Filter 'dbz3_*.log' -EA SilentlyContinue | Select-Object -ExpandProperty Name)
$env:DBZ3_TEST_BATTLE = "0,1,0,-1,100000,100000"
$env:DBZ3_HUB_MODE = "5"
$env:DBZ3_NO_DIALOGS = "1"
$env:DBZ3_AGENTE = "cpu_perfil"
$old = [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity
$p = $null; $log = $null
try {
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = [IntPtr]$Mask
  $p = Start-Process -FilePath (Join-Path $dir 'dbz3.exe') -WorkingDirectory $dir -WindowStyle Hidden -PassThru
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = $old
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalSeconds -lt 90 -and -not $log) {
    Start-Sleep -Milliseconds 250
    $after = @(Get-ChildItem $logs -Filter 'dbz3_*.log' -EA SilentlyContinue | Sort-Object LastWriteTime -Descending)
    $new = @($after | Where-Object { $before -notcontains $_.Name })
    if ($new.Count -gt 0) { $log = $new[0].FullName }
    if ($p.HasExited) { break }
  }
  if (-not $log) { Write-Output "$Label NO_LOG"; exit 3 }
  $battle = $false
  while ($sw.Elapsed.TotalSeconds -lt 180 -and -not $p.HasExited) {
    $t = Get-Content $log -EA SilentlyContinue
    if ($t | Select-String -Pattern 'hub: combate nativo|tag: empieza un combate') { $battle = $true; break }
    Start-Sleep -Milliseconds 250
  }
  if (-not $battle) { Write-Output "$Label NO_BATTLE"; exit 4 }
  Start-Sleep -Milliseconds ($Warm * 1000)
  $p.Refresh(); $c0 = @{}; foreach ($th in $p.Threads) { try { $c0[$th.Id] = $th.TotalProcessorTime.TotalSeconds } catch {} }
  $smp = Join-Path $Out ("S_{0}_{1}_{2}.txt" -f $Label, $Mask, (Get-Date -Format 'yyyyMMdd_HHmmss'))
  $code = -1
  try { & $Sampler $p.Id $Seconds $Thread $smp | Out-Null; $code = $LASTEXITCODE } catch { $code = -1 }
  $lines = 0
  if ($smp -and (Test-Path $smp)) {
    $lines = @(Get-Content $smp | Where-Object { $_ -and -not $_.StartsWith('#') }).Count
  }
  if ($code -ne 0 -or $lines -lt 1) {
    Write-Output ("{0} SAMPLER_FALLO code={1} muestras={2}" -f $Label, $code, $lines)
    exit 8
  }
  $p.Refresh(); $c1 = @{}; foreach ($th in $p.Threads) { try { $c1[$th.Id] = $th.TotalProcessorTime.TotalSeconds } catch {} }
  $thr = @(); $tot = 0.0
  foreach ($id in $c0.Keys) {
    if ($c1.ContainsKey($id)) {
      $d = $c1[$id] - $c0[$id]
      if ($d -gt 0.05) { $thr += [pscustomobject]@{ id = $id; pct = [math]::Round($d / $Seconds * 100, 1) }; $tot += $d }
    }
  }
  $thr = @($thr | Sort-Object pct -Descending)
  Start-Sleep -Milliseconds 300
} finally {
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = $old
  if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force -EA SilentlyContinue } catch {} }
}
$perf = @()
foreach ($l in (Get-Content $log)) {
  if ($l -match 'perf fps=([\d.]+) p1=([\d.]+) p99_ms=([\d.]+)') {
    $perf += [pscustomobject]@{ fps = [double]$Matches[1]; p1 = [double]$Matches[2]; p99 = [double]$Matches[3] }
  }
}
$vals = @($perf | Select-Object -Skip 1)
function Med($a) { if (@($a).Count -eq 0) { return 0 }; $s = @($a | Sort-Object); $s[[int][math]::Floor(($s.Count - 1) * 0.5)] }
$copy = Join-Path $Out ("R_{0}_{1}_{2}.log" -f $Label, $Mask, (Get-Date -Format 'yyyyMMdd_HHmmss'))
Copy-Item $log $copy -Force
Write-Output ("RES {0} mask=0x{1:X} fps_med={2:N2} p1={3:N2} p99={4:N2} proc={5}% muestra={6}" -f $Label, $Mask,
  (Med ($vals | % { $_.fps })), (Med ($vals | % { $_.p1 })), (Med ($vals | % { $_.p99 })),
  [math]::Round($tot / $Seconds * 100, 1), $smp)
foreach ($r in ($thr | Select-Object -First 6)) { Write-Output ("  hilo {0,-7} {1,6:N1}%" -f $r.id, $r.pct) }
Write-Output ("log=" + $copy)
