# Lanza dbz3 con la ventana fuera de pantalla, inyecta una secuencia de teclas y
# resume el log (huella de pantalla via `AFS OVERRIDE LOOKUP ... entry=`, FPS y
# upscale de texturas). Pensado para pruebas automaticas sin ventana visible.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\drive_game.ps1 `
#     -Label "select" -Seconds 60 -Keys "Return:6000,Right:800,a:6000" `
#     -Overrides "dbz3_skip_launcher=true;dbz3_dev_mode=true;dbz3_diag_logging=true"
#
# Teclas soportadas: Return, Space, Backspace, Up, Down, Left, Right, W, A, S, D,
#   Z, X, C, L, P, K, F, Q, E, 1, 3, Shift+<tecla>. Formato "Tecla:ms,...".
param(
  [string]$Label = "drive",
  [int]$Seconds = 45,
  [string]$Keys = "",
  [int]$HideMode = 0,          # 0 = mover fuera de pantalla, 1 = ocultar
  [string]$Overrides = ""      # overrides del dbz3_user.toml
)
$ErrorActionPreference = 'Stop'
$dst = (Resolve-Path 'out\build\win-amd64-release').Path
$toml = Join-Path $dst 'dbz3_user.toml'
$bak = "$toml.bak"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Drv {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static uint Target; public static int Found; public static IntPtr Game;
  public static string Title = ""; public static string Cls = ""; public static int W, H;
  public static void Locate(uint pid, int mode) {
    Target = pid; Found = 0;
    long bestArea = -1; IntPtr best = IntPtr.Zero; string bestCls = "", bestTitle = "";
    int bestW = 0, bestH = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == Target) {
        if (mode == 1) { ShowWindow(h, 0); SetWindowPos(h, IntPtr.Zero, -4000, -4000, 0, 0, 0x0001 | 0x0004 | 0x0080); }
        else { SetWindowPos(h, IntPtr.Zero, -4000, -4000, 0, 0, 0x0001 | 0x0004 | 0x0010); }
        var sb = new System.Text.StringBuilder(256);
        GetClassName(h, sb, sb.Capacity);
        string cls = sb.ToString();
        sb = new System.Text.StringBuilder(256);
        GetWindowText(h, sb, sb.Capacity);
        string title = sb.ToString();
        Found++;
        if (cls.Contains("IME")) { return true; }
        if (!IsWindowVisible(h)) { return true; }
        RECT r; GetClientRect(h, out r);
        long area = (long)(r.R - r.L) * (long)(r.B - r.T);
        if (area > bestArea) {
          bestArea = area; best = h; bestCls = cls; bestTitle = title;
          bestW = r.R - r.L; bestH = r.B - r.T;
        }
      }
      return true;
    }, IntPtr.Zero);
    Game = best; Cls = bestCls; Title = bestTitle; W = bestW; H = bestH;
  }
  public static bool ForceFocus(IntPtr h) {
    if (h == IntPtr.Zero) { return false; }
    uint tmpPid;
    uint fgThread = GetWindowThreadProcessId(GetForegroundWindow(), out tmpPid);
    uint curThread = GetCurrentThreadId();
    if (fgThread != curThread) { AttachThreadInput(curThread, fgThread, true); }
    BringWindowToTop(h);
    bool ok = SetForegroundWindow(h);
    if (fgThread != curThread) { AttachThreadInput(curThread, fgThread, false); }
    System.Threading.Thread.Sleep(120);
    return ok || GetForegroundWindow() == h;
  }
  public static bool IsForeground(IntPtr h) { return GetForegroundWindow() == h; }
  public static void Key(byte vk) {
    bool ext = (vk == 0x25 || vk == 0x26 || vk == 0x27 || vk == 0x28 || vk == 0x2D || vk == 0x2E ||
                vk == 0x24 || vk == 0x23 || vk == 0x22 || vk == 0x21);
    byte scan = (byte)MapVirtualKey(vk, 0);
    uint down = ext ? 0x0001u : 0u;
    uint up = ext ? 0x0003u : 0x0002u;
    keybd_event(vk, scan, down, UIntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(vk, scan, up, UIntPtr.Zero);
    System.Threading.Thread.Sleep(60);
  }
}
"@

function Get-Vk([string]$name) {
  switch ($name.ToLower()) {
    'return' { 0x0D } 'space' { 0x20 } 'backspace' { 0x08 } 'escape' { 0x1B }
    'up' { 0x26 } 'down' { 0x28 } 'left' { 0x25 } 'right' { 0x27 }
    'shift' { 0x10 } 'tab' { 0x09 }
    'w' { 0x57 } 'a' { 0x41 } 's' { 0x53 } 'd' { 0x44 }
    'z' { 0x5A } 'x' { 0x58 } 'c' { 0x43 } 'l' { 0x4C } 'p' { 0x50 }
    'k' { 0x4B } 'f' { 0x46 } 'q' { 0x51 } 'e' { 0x45 }
    '1' { 0x31 } '3' { 0x33 }
    default { throw "Tecla no soportada: $name" }
  }
}

# --- toml: backup + overrides ---
$ov = @{}
if ($Overrides) { foreach ($kv in $Overrides.Split(';')) { if ($kv.Trim()) { $p = $kv.Split('=',2); $ov[$p[0].Trim()] = $p[1].Trim() } } }
Copy-Item -LiteralPath $toml -Destination $bak -Force
$out = New-Object System.Collections.Generic.List[string]; $seen = @{}
foreach ($line in (Get-Content -LiteralPath $bak)) {
  if ($line -match '^\s*([A-Za-z0-9_\-]+)\s*=') {
    $k = $Matches[1]; $seen[$k] = $true
    if ($ov.ContainsKey($k)) { $out.Add("$k = $($ov[$k])") } else { $out.Add($line) }
  } else { $out.Add($line) }
}
foreach ($k in $ov.Keys) { if (-not $seen.ContainsKey($k)) { $out.Add("$k = $($ov[$k])") } }
[System.IO.File]::WriteAllLines($toml, $out)

# --- lanzar y pilotar ---
$logsBefore = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object Name)
$prevFg = [Drv]::GetForegroundWindow()
$p = Start-Process -FilePath (Join-Path $dst 'dbz3.exe') -WorkingDirectory $dst -WindowStyle Hidden -PassThru
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sent = New-Object System.Collections.Generic.List[string]
$script:keyIndex = 0

# Secuencia: "Tecla:ms,Tecla:ms,..." -> lista [(vk, espera tras pulsar)]
$steps = New-Object System.Collections.Generic.List[object]
foreach ($part in ($Keys -split ',')) {
  if (-not $part.Trim()) { continue }
  $kv = $part.Split(':')
  $name = $kv[0].Trim(); $wait = if ($kv.Count -gt 1) { [int]$kv[1] } else { 500 }
  $steps.Add([pscustomobject]@{ Name = $name; Wait = $wait })
}

$stepIndex = 0
$focusOk = $false
$lastFocusTry = [DateTime]::MinValue
$startSending = $false
while ($sw.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
  [Drv]::Locate([uint32]$p.Id, $HideMode)
  if ([Drv]::Game -ne [IntPtr]::Zero) {
    if ((([DateTime]::Now - $lastFocusTry).TotalSeconds -gt 1.5) -and -not [Drv]::IsForeground([Drv]::Game)) {
      $focusOk = [Drv]::ForceFocus([Drv]::Game)
      $lastFocusTry = [DateTime]::Now
    } elseif ([Drv]::IsForeground([Drv]::Game)) {
      $focusOk = $true
    }
    # Dar ~1.5 s tras conseguir el foco antes de la primera tecla.
    if (-not $startSending -and $focusOk -and -not $sendingSince) {
      $sendingSince = [DateTime]::Now
    }
    if ($sendingSince -and (([DateTime]::Now - $sendingSince).TotalSeconds -gt 1.0)) {
      $startSending = $true
    }
  }
  if ($startSending -and $stepIndex -lt $steps.Count) {
    $step = $steps[$stepIndex]
    if ($step.Name -like 'Shift+*') {
      [Drv]::Key(0x10)
      [Drv]::Key([byte](Get-Vk ($step.Name -replace '^Shift\+', '')))
      [Drv]::Key(0x10)
    } else {
      [Drv]::Key([byte](Get-Vk $step.Name))
    }
    $sent.Add($step.Name)
    Start-Sleep -Milliseconds $step.Wait
    $stepIndex++
  } else {
    Start-Sleep -Milliseconds 300
  }
}
try { [void][Drv]::SetForegroundWindow($prevFg) } catch {}
$exited = $p.HasExited
if (-not $exited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 700
Copy-Item -LiteralPath $bak -Destination $toml -Force
Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue

# --- informe ---
$logsAfter = @(Get-ChildItem -LiteralPath "$dst\logs" -Filter 'dbz3_*.log' | Sort-Object Name)
$new = @($logsAfter | Where-Object { $logsBefore.Name -notcontains $_.Name })
$log = if ($new.Count -gt 0) { $new[0] } else { ($logsAfter | Sort-Object LastWriteTime -Descending)[0] }
$txt = Get-Content -LiteralPath $log.FullName
"===== $Label ====="
"log: $($log.Name)   teclas enviadas: $(if ($sent.Count) { $sent -join ' ' } else { '(ninguna)' })   ventanas=$([Drv]::Found)"
"foco: ok=$focusOk  ventana clase='$([Drv]::Cls)' titulo='$([Drv]::Title)' $([Drv]::W)x$([Drv]::H)  en_primer_plano=$([Drv]::IsForeground([Drv]::Game))"
$entries = $txt | Select-String -Pattern 'AFS OVERRIDE LOOKUP: afs=(\S+) entry=(\d+)' |
  ForEach-Object { $m = $_.Matches[0].Groups; "$($m[1].Value):$($m[2].Value)" }
"AFS lookups: $($entries.Count)   distintos: $((($entries | Sort-Object -Unique)).Count)"
"  por AFS: " + (($entries | Group-Object { $_.Split(':')[0] } | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join '  ')
"  entrada 3983/3984 (pantalla de titulo) presentes: " + (($entries | Where-Object { $_ -match ':398[34]$' } | Sort-Object -Unique) -join ' ')
"  ultimas 12 entradas distintas: " + ((($entries | Select-Object -Last 40 | Sort-Object -Unique) -join ' '))
Select-String -LiteralPath $log.FullName -Pattern 'upscale ACCEPT|upscale pipeline ready|upx=|dbz3: perf' |
  ForEach-Object { "  " + ($_.Line -replace '^\[[^\]]+\] \[([^\]]+)\] \[[^\]]+\] ', '[$1] ') } | Select-Object -First 10
"errores: $((@($txt | Select-String -Pattern '\[error\]|\[critical\]')).Count)"
