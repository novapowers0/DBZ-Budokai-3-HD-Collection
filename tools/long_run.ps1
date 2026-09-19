param(
  [ValidateSet('Start','Status','Stop')] [string]$Action = 'Start',
  [int]$Seconds = 1200,          # duracion maxima (minutos de margen para llegar a la demo)
  [string]$Label = "long",
  [string]$Overrides = "",       # "clave=valor;clave=valor" (se anaden a los base de abajo)
  [switch]$Unmuted               # por defecto SILENCIADO (audio_mute=true) para no oir el opening
)
$ErrorActionPreference = 'Stop'
$dst = (Resolve-Path 'out\build\win-amd64-release').Path
$toml = Join-Path $dst 'dbz3_user.toml'
$state = Join-Path $env:TEMP 'opencode\long_run_state.json'

if ($Action -eq 'Stop') {
  if (Test-Path -LiteralPath $state) {
    $s = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    if ($s.pid) { try { Stop-Process -Id $s.pid -Force -ErrorAction SilentlyContinue } catch {} }
    if ($s.backup -and (Test-Path -LiteralPath $s.backup)) {
      Copy-Item -LiteralPath $s.backup -Destination $toml -Force
      Remove-Item -LiteralPath $s.backup -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $state -Force -ErrorAction SilentlyContinue
    Write-Output "long_run: detenido y toml restaurado"
  } else { Write-Output "long_run: no hay ejecucion activa" }
  exit 0
}

if ($Action -eq 'Status') {
  if (-not (Test-Path -LiteralPath $state)) { Write-Output "long_run: sin estado (no hay prueba activa)"; exit 0 }
  $s = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
  $alive = $false; try { $alive = -not (Get-Process -Id $s.pid -ErrorAction Stop).HasExited } catch {}
  Write-Output "long_run: pid=$($s.pid) vivo=$alive inicio=$($s.started) log=$($s.log)"
  if (Test-Path -LiteralPath $s.log) {
    $txt = Get-Content -LiteralPath $s.log
    Write-Output ("lineas={0} perf={1} errores={2}" -f @($txt).Count,
      @($txt | Select-String -Pattern 'perf fps=').Count,
      @($txt | Select-String -Pattern '\[error\]|\[critical\]|FATAL').Count)
    $txt | Select-String -Pattern 'perf fps=' | Select-Object -Last 6 | ForEach-Object { "  " + $_.Line }
    $txt | Select-String -Pattern '\[error\]|\[critical\]|FATAL' | Select-Object -First 8 | ForEach-Object { "  ! " + $_.Line }
  }
  exit 0
}

# ---- Start ----
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinHide2 {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
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

$base = "dbz3_skip_launcher=true;dbz3_perf_logging=true;dbz3_diag_logging=false"
if (-not $Unmuted) { $base += ";audio_mute=true" }
$all = if ($Overrides) { "$base;$Overrides" } else { $base }

$backup = "$toml.longbak"
Copy-Item -LiteralPath $toml -Destination $backup -Force
$lines = Get-Content -LiteralPath $backup
$ov = @{}
foreach ($kv in $all.Split(';')) { if ($kv.Trim()) { $p = $kv.Split('=',2); $ov[$p[0].Trim()] = $p[1].Trim() } }
$out = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($l in $lines) {
  if ($l -match '^\s*([A-Za-z0-9_\-]+)\s*=') {
    $k = $Matches[1]; $seen[$k] = $true
    if ($ov.ContainsKey($k)) { $out.Add("$k = $($ov[$k])") } else { $out.Add($l) }
  } else { $out.Add($l) }
}
foreach ($k in $ov.Keys) { if (-not $seen.ContainsKey($k)) { $out.Add("$k = $($ov[$k])") } }
[System.IO.File]::WriteAllLines($toml, $out)

$logsBefore = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object Name)
$p = Start-Process -FilePath (Join-Path $dst 'dbz3.exe') -WorkingDirectory $dst -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 6
try { [WinHide2]::Hide([uint32]$p.Id) } catch {}
Start-Sleep -Seconds 2
$after = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object LastWriteTime -Descending)
$new = @($after | Where-Object { $logsBefore.Name -notcontains $_.Name })
$log = if ($new.Count -gt 0) { $new[0].FullName } else { $after[0].FullName }

@{
  pid = $p.Id
  backup = $backup
  log = $log
  started = (Get-Date).ToString('s')
  seconds = $Seconds
  label = $Label
} | ConvertTo-Json | Set-Content -LiteralPath $state

Write-Output "long_run: iniciado pid=$($p.Id) label='$Label' silenciado=$(-not $Unmuted)"
Write-Output "long_run: log=$log"
Write-Output "long_run: para parar -> tools\long_run.ps1 -Action Stop"
