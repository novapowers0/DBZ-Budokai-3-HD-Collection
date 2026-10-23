param(
  [string]$Dir = "D:\tmp\D1",
  [int]$Mask = 15,
  [int]$Seconds = 120,
  [int]$Warmup = 20,
  [string]$Label = "d1",
  [string]$OutDir = "D:\tmp\D1_evidencias",
  [string]$BattleIds = "0,1,0,300"
)
$ErrorActionPreference = 'Stop'
$dst = (Resolve-Path -LiteralPath $Dir).Path
$exe = Join-Path $dst 'dbz3.exe'
$toml = Join-Path $dst 'dbz3_user.toml'
$logs = Join-Path $dst 'logs'
if (-not (Test-Path -LiteralPath $exe)) { throw "no existe $exe" }
if (-not (Test-Path -LiteralPath $toml)) { throw "no existe $toml" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $logs | Out-Null

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class D1Win {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static uint T; public static int N;
  public static void Hide(uint pid) {
    T = pid; N = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == T) { SetWindowPos(h, IntPtr.Zero, -4000, -4000, 0, 0, 0x0001|0x0004|0x0010); N++; }
      return true;
    }, IntPtr.Zero);
  }
}
"@

$backup = "$toml.d1bak"
Copy-Item -LiteralPath $toml -Destination $backup -Force
$set = [ordered]@{
  'dbz3_skip_launcher' = 'true'
  'dbz3_perf_logging' = 'true'
  'dbz3_diag_logging' = 'false'
  'audio_mute' = 'true'
  'dbz3_dim_unfocused' = 'false'
  'frame_cap' = '60'
  'dbz3_resolution_scale' = '1'
  'draw_resolution_scale_x' = '1'
  'draw_resolution_scale_y' = '1'
  'dbz3_native_2x_msaa' = 'false'
  'native_2x_msaa' = 'false'
  'dbz3_hd_textures' = '1'
  'dbz3_texture_packs_on' = 'false'
  'dbz3_present_effect' = '"bilinear"'
  'present_effect' = '"bilinear"'
}
$lines = Get-Content -LiteralPath $backup
$out = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($l in $lines) {
  if ($l -match '^\s*([A-Za-z0-9_\-]+)\s*=') {
    $k = $Matches[1]; $seen[$k] = $true
    if ($set.Contains($k)) { $out.Add("$k = $($set[$k])") } else { $out.Add($l) }
  } else { $out.Add($l) }
}
foreach ($k in $set.Keys) { if (-not $seen.ContainsKey($k)) { $out.Add("$k = $($set[$k])") } }
[System.IO.File]::WriteAllLines($toml, $out)

$env:DBZ3_TEST_BATTLE = $BattleIds
$env:DBZ3_HUB_MODE = '5'
$before = @(Get-ChildItem -LiteralPath $logs -Filter 'dbz3_*.log' | Sort-Object Name)
$oldAffinity = [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity
$p = $null
$log = $null
$sw = [Diagnostics.Stopwatch]::StartNew()
$sw2 = $null
$combate = $false
$startupS = $null
$menuS = $null
try {
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = [IntPtr]$Mask
  $p = Start-Process -FilePath $exe -WorkingDirectory $dst -WindowStyle Hidden -PassThru
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = $oldAffinity
  $waitLog = [Diagnostics.Stopwatch]::StartNew()
  while ($waitLog.Elapsed.TotalSeconds -lt 20 -and -not $log) {
    Start-Sleep -Milliseconds 300
    $after = @(Get-ChildItem -LiteralPath $logs -Filter 'dbz3_*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    $new = @($after | Where-Object { $before.Name -notcontains $_.Name })
    if ($new.Count -gt 0) { $log = $new[0].FullName }
  }
  if (-not $log) { throw "no aparece el log nuevo de la corrida" }
  while ($sw.Elapsed.TotalSeconds -lt ($Seconds + 240) -and -not $p.HasExited) {
    try { [D1Win]::Hide([uint32]$p.Id) } catch {}
    if (Test-Path -LiteralPath $log) {
      $txt = Get-Content -LiteralPath $log
      if (-not $combate -and ($txt | Select-String -Pattern 'tag: empieza un combate|hub: combate nativo')) { $combate = $true; $sw2 = [Diagnostics.Stopwatch]::StartNew() }
      if ($combate -and $sw2.Elapsed.TotalSeconds -ge $Seconds) { break }
    }
    Start-Sleep -Milliseconds 500
  }
} finally {
  [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = $oldAffinity
  if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {} }
  Copy-Item -LiteralPath $backup -Destination $toml -Force
  Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
if (-not $log) { throw "no encuentro el log de la corrida" }
$text = Get-Content -LiteralPath $log
$first = ($text | Select-Object -First 1)
$pre = $text | Select-String -Pattern "OnPreLaunchModule' at ([\d.]+) ms" | Select-Object -First 1
if ($pre) { $startupS = [double]$pre.Matches[0].Groups[1].Value / 1000.0 }
$menu = $text | Select-String -Pattern "menu principal estado 3" | Select-Object -First 1
if ($menu -and $first -match '^\[([^\]]+)\]') {
  try { $menuS = ([datetime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', $null) - [datetime]::ParseExact(($first -replace '^\[([^\]]+)\].*','$1'), 'yyyy-MM-dd HH:mm:ss.fff', $null)).TotalSeconds } catch {}
}
$perf = @($text | Select-String -Pattern 'perf fps=([\d.]+) p1=([\d.]+) p99_ms=([\d.]+)')
$rows = @()
foreach ($m in $perf) {
  $rows += [pscustomobject]@{ fps=[double]$m.Matches[0].Groups[1].Value; p1=[double]$m.Matches[0].Groups[2].Value; p99=[double]$m.Matches[0].Groups[3].Value }
}
if ($rows.Count -eq 0) { throw "sin lineas perf con p1/p99 en $log" }
$keep = if ($rows.Count -gt 2) { $rows | Select-Object -Skip 1 } else { $rows }
function Stat($v) {
  $a = @($v | Sort-Object)
  $med = $a[[int][math]::Floor(($a.Count - 1) * 0.5)]
  return [pscustomobject]@{ min=$a[0]; med=$med; max=$a[-1]; dev=(($a[-1]-$a[0])/[math]::Max($med,0.001)*100) }
}
$sf = Stat ($keep | ForEach-Object { $_.fps })
$sp = Stat ($keep | ForEach-Object { $_.p1 })
$s9 = Stat ($keep | ForEach-Object { $_.p99 })
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1)
$gpu = (Get-CimInstance Win32_VideoController | Where-Object { $_.Name -notmatch 'Basic|Remote' } | Select-Object -First 1)
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$copy = Join-Path $OutDir ("D1_{0}_{1}_{2}.log" -f $Label, $Mask, $stamp)
Copy-Item -LiteralPath $log -Destination $copy -Force
$line = "D1 equipo cpu='{0}' nucleos={1} logicos={2} gpu='{3}' mask=0x{4:X} arranque_s={5} menu_s={6} muestras={7}" -f `
  $cpu.Name.Trim(), $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors, $gpu.Name, $Mask, $startupS, $menuS, $keep.Count
$report = @(
  $line,
  ("D1 {0} mask=0x{1:X}: fps med={2:N2} min={3:N2} max={4:N2} dev={5:N2}% | p1 med={6:N2} min={7:N2} max={8:N2} dev={9:N2}% | p99 med={10:N2} min={11:N2} max={12:N2} dev={13:N2}% | combate={14}" -f `
    $Label, $Mask, $sf.med, $sf.min, $sf.max, $sf.dev, $sp.med, $sp.min, $sp.max, $sp.dev, $s9.med, $s9.min, $s9.max, $s9.dev, $combate),
  ("log=" + $copy)
)
$report | Tee-Object -FilePath (Join-Path $OutDir ("D1_{0}_{1}_{2}.txt" -f $Label, $Mask, $stamp)) | Write-Output
if (-not $combate) { exit 2 }
exit 0
