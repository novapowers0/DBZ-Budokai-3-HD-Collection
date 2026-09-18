param(
  [string]$Label = "run",
  [int]$Seconds = 30,
  [int]$HideMode = 0,       # 0 = mover fuera de pantalla, 1 = ocultar
  [string]$Overrides = ""   # "clave=valor;clave=valor"
)
$ErrorActionPreference = 'Stop'
$dst = (Resolve-Path 'out\build\win-amd64-release').Path
$toml = Join-Path $dst 'dbz3_user.toml'
# Backup autocontenido: se copia el toml ACTUAL y se restaura al terminar. Si una
# ejecucion anterior se interrumpio, el aviso de abajo lo detecta.
$pristine = "$toml.bak"
if ((Get-Content -LiteralPath $toml -Raw) -match 'dbz3_skip_launcher\s*=\s*true') {
  Write-Host "AVISO: el toml ya contiene overrides de una prueba anterior (run interrumpido)."
}
Copy-Item -LiteralPath $toml -Destination $pristine -Force

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinHide {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static uint Target;
  public static int Hidden;
  public static int Mode; // 0 = mover fuera de pantalla (sigue "visible"), 1 = ocultar
  public static void HideFor(uint pid) {
    Target = pid; Hidden = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == Target) {
        if (Mode == 1) {
          ShowWindow(h, 0);
          SetWindowPos(h, IntPtr.Zero, -4000, -4000, 0, 0, 0x0001 | 0x0004 | 0x0080);
        } else {
          // SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE: mover sin tocar visibilidad
          // (si se oculta, Windows deja de mandar WM_PAINT y el present en juego
          // se para, que es justo lo que queremos medir).
          SetWindowPos(h, IntPtr.Zero, -4000, -4000, 0, 0, 0x0001 | 0x0004 | 0x0010);
        }
        Hidden++;
      }
      return true;
    }, IntPtr.Zero);
  }
}
"@

# Construir el toml a partir del original + overrides
$lines = Get-Content -LiteralPath $pristine
$ov = @{}
if ($Overrides) {
  foreach ($kv in $Overrides.Split(';')) {
    if ($kv.Trim()) { $p = $kv.Split('=',2); $ov[$p[0].Trim()] = $p[1].Trim() }
  }
}
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

# Lanzar oculto
[WinHide]::Mode = $HideMode
$logsBefore = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object Name)
$p = Start-Process -FilePath (Join-Path $dst 'dbz3.exe') -WorkingDirectory $dst -WindowStyle Hidden -PassThru
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
  try { [WinHide]::HideFor([uint32]$p.Id) } catch {}
  Start-Sleep -Milliseconds 400
}
$exited = $p.HasExited
if (-not $exited) { try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {} }
Start-Sleep -Milliseconds 800

# Restaurar el toml original y limpiar el backup
Copy-Item -LiteralPath $pristine -Destination $toml -Force
Remove-Item -LiteralPath $pristine -Force -ErrorAction SilentlyContinue

$logsAfter = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object Name)
$new = @($logsAfter | Where-Object { $logsBefore.Name -notcontains $_.Name })
$all = $logsAfter | Sort-Object LastWriteTime -Descending
$log = if ($new.Count -gt 0) { $new[0] } else { $all[0] }
$txt = Get-Content -LiteralPath $log.FullName
$count = {
  param($pat)
  @($txt | Select-String -Pattern $pat -SimpleMatch:$false).Count
}
"===== $Label ====="
"log: $($log.Name)  lineas: $(@($txt).Count)  exited=$exited  hidden_windows=$([WinHide]::Hidden)"
"AFS OVERRIDE lines : $((@($txt | Select-String -Pattern 'AFS OVERRIDE') | Measure-Object).Count)"
"perf lines         : $((@($txt | Select-String -Pattern 'perf fps=') | Measure-Object).Count)"
"upscale ready      : $((@($txt | Select-String -Pattern 'upscale pipeline ready') | Measure-Object).Count)"
"[error]/[critical] : $((@($txt | Select-String -Pattern '\[error\]|\[critical\]') | Measure-Object).Count)"
"--- lineas clave ---"
$txt | Select-String -Pattern 'skip_launcher|booting directly|applied runtime settings|first present|quality preset|upscale pipeline ready|OnShutdown' | ForEach-Object { "  " + $_.Line }
"--- primeras 4 perf ---"
$txt | Select-String -Pattern 'perf fps=' | Select-Object -First 4 | ForEach-Object { "  " + $_.Line }
"--- ultimas 3 lineas ---"
$txt | Select-Object -Last 3 | ForEach-Object { "  " + $_ }
