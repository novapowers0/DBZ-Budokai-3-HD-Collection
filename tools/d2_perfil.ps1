# D2: perfil de hilos + FPS de un combate, con la afinidad fijada (por defecto 2 nucleos).
# Uso: powershell -ExecutionPolicy Bypass -File tools\d2_perfil.ps1 -Dir D:\tmp\D2 -Mask 5 -Seconds 40 -Label d2_2c
# Deja en OutDir: el log de la corrida, el informe .txt y la tabla de hilos.
# Mide, por hilo: CPU consumida en la ventana (segundos), % de un nucleo y % del proceso.
param(
  [string]$Dir = "D:\tmp\D2",
  [int]$Mask = 5,
  [int]$Seconds = 40,
  [string]$Label = "d2",
  [string]$OutDir = "D:\tmp\D2_evidencias",
  [string]$BattleIds = "0,1,0,-1,100000,100000"
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
public class D2Win {
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

$backup = "$toml.d2bak"
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
$muestras = New-Object System.Collections.Generic.List[object]
$anterior = @{}
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
  $ultimaMuestra = $null
  while ($sw.Elapsed.TotalSeconds -lt ($Seconds + 240) -and -not $p.HasExited) {
    try { [D2Win]::Hide([uint32]$p.Id) } catch {}
    if (Test-Path -LiteralPath $log) {
      $txt = Get-Content -LiteralPath $log
      if (-not $combate -and ($txt | Select-String -Pattern 'tag: empieza un combate|hub: combate nativo')) {
        $combate = $true; $sw2 = [Diagnostics.Stopwatch]::StartNew()
      }
      if ($combate) {
        if ($sw2.Elapsed.TotalSeconds -lt $Seconds) {
          # Muestra de CPU por hilo (una por segundo). Se guarda el acumulado de cada hilo.
          try {
            $p.Refresh()
            $ahora = @{}
            foreach ($th in $p.Threads) {
              try { $ahora[$th.Id] = $th.TotalProcessorTime.TotalSeconds } catch {}
            }
            $muestras.Add([pscustomobject]@{ t = $sw2.Elapsed.TotalSeconds; cpu = $ahora; total = $p.TotalProcessorTime.TotalSeconds })
            $anterior = $ahora
          } catch {}
        }
        if ($sw2.Elapsed.TotalSeconds -ge $Seconds) { break }
      }
    }
    Start-Sleep -Milliseconds 1000
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
$perf = @($text | Select-String -Pattern 'perf fps=([\d.]+) p1=([\d.]+) p99_ms=([\d.]+)')
$rows = @()
foreach ($m in $perf) {
  $rows += [pscustomobject]@{ fps=[double]$m.Matches[0].Groups[1].Value; p1=[double]$m.Matches[0].Groups[2].Value; p99=[double]$m.Matches[0].Groups[3].Value }
}
$keep = if ($rows.Count -gt 2) { $rows | Select-Object -Skip 1 } else { $rows }
function Stat($v) {
  if (@($v).Count -eq 0) { return [pscustomobject]@{ min=0; med=0; max=0; dev=0 } }
  $a = @($v | Sort-Object)
  $med = $a[[int][math]::Floor(($a.Count - 1) * 0.5)]
  return [pscustomobject]@{ min=$a[0]; med=$med; max=$a[-1]; dev=(($a[-1]-$a[0])/[math]::Max($med,0.001)*100) }
}
$sf = Stat ($keep | ForEach-Object { $_.fps })
$sp = Stat ($keep | ForEach-Object { $_.p1 })
$s9 = Stat ($keep | ForEach-Object { $_.p99 })

# Tabla de hilos: delta de CPU entre la primera y la ultima muestra.
$tabla = @()
if ($muestras.Count -ge 2) {
  $a = $muestras[0]; $b = $muestras[$muestras.Count - 1]
  $dt = [math]::Max($b.t - $a.t, 0.001)
  $ids = @(($a.cpu.Keys + $b.cpu.Keys) | Sort-Object -Unique)
  foreach ($id in $ids) {
    $c0 = if ($a.cpu.ContainsKey($id)) { $a.cpu[$id] } else { 0 }
    $c1 = if ($b.cpu.ContainsKey($id)) { $b.cpu[$id] } else { 0 }
    $d = $c1 - $c0
    if ($d -gt 0.02) {
      $tabla += [pscustomobject]@{ Hilo=$id; CPU_s=[math]::Round($d,2); PctNucleo=[math]::Round($d/$dt*100,1); PctProceso=[math]::Round($d/$dt/[math]::Max([int]([math]::Log($Mask,2))+1,1)*100,1) }
    }
  }
  $tabla = @($tabla | Sort-Object CPU_s -Descending)
}
$totalProc = 0
if ($muestras.Count -ge 2) {
  $a = $muestras[0]; $b = $muestras[$muestras.Count - 1]
  $totalProc = ($b.total - $a.total) / [math]::Max($b.t - $a.t, 0.001) * 100
}

$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1)
$gpu = (Get-CimInstance Win32_VideoController | Where-Object { $_.Name -notmatch 'Basic|Remote' } | Select-Object -First 1)
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$copy = Join-Path $OutDir ("D2_{0}_{1}_{2}.log" -f $Label, $Mask, $stamp)
Copy-Item -LiteralPath $log -Destination $copy -Force
$report = New-Object System.Collections.Generic.List[string]
$report.Add(("D2 equipo cpu='{0}' nucleos={1} logicos={2} gpu='{3}' mask=0x{4:X}" -f $cpu.Name.Trim(), $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors, $gpu.Name, $Mask))
$report.Add(("D2 {0} mask=0x{1:X}: fps med={2:N2} min={3:N2} max={4:N2} | p1 med={5:N2} min={6:N2} max={7:N2} | p99 med={8:N2} min={9:N2} max={10:N2} | combate={11} muestras={12}" -f `
  $Label, $Mask, $sf.med, $sf.min, $sf.max, $sp.med, $sp.min, $sp.max, $s9.med, $s9.min, $s9.max, $combate, $keep.Count))
$report.Add(("D2 {0}: proceso {1:N1}% de CPU en la ventana (100% = un nucleo)" -f $Label, $totalProc))
if ($tabla.Count -gt 0) {
  $report.Add("D2 hilos (ventana de $([math]::Round($muestras[$muestras.Count-1].t - $muestras[0].t,1)) s):")
  foreach ($r in ($tabla | Select-Object -First 12)) {
    $report.Add(("  hilo {0,-7} CPU {1,6:N2} s  {2,6:N1}% de un nucleo  {3,6:N1}% del proceso" -f $r.Hilo, $r.CPU_s, $r.PctNucleo, $r.PctProceso))
  }
}
$report.Add(("log=" + $copy))
$report | Tee-Object -FilePath (Join-Path $OutDir ("D2_{0}_{1}_{2}.txt" -f $Label, $Mask, $stamp)) | Write-Output
if (-not $combate) { exit 2 }
exit 0
