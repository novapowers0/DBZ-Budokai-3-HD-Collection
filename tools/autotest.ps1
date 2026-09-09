# autotest.ps1 - Harness de validacion automatizada del juego dbz3.exe.
# Lanza el juego con cvars controladas, activa su ventana, inyecta teclado
# (navegacion de menus), captura pantalla, cierra limpio y evalua logs
# (crash / override servido / reads AFS) contra criterios. Todo sin intervencion.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\autotest.ps1 -Scenario boot
#   powershell -ExecutionPolicy Bypass -File tools\autotest.ps1 -Scenario select -Steps nav.json -ShotEvery 5
#   powershell -ExecutionPolicy Bypass -File tools\autotest.ps1 -Scenario mod -ModName sw_goten_nativo -RequireOverride
#
# Salidas en out/analysis/autotest/<scenario>_<timestamp>/:
#   summary.json|txt   : aserciones y metricas
#   step_*.png         : capturas tras cada paso
#   nav.log            : teclas inyectadas
# Exit code 0 = todo OK (sin crash y con los criterios cumplidos), 1 = fallo.

param(
    [string]$Scenario = "boot",
    [string]$ModName = "",
    [string]$EnableMod = "",      # quita el marker .disabled (activa el mod)
    [string]$DisableMod = "",     # crea el marker .disabled (desactiva el mod)
    [switch]$RequireOverride,     # exige al menos un AFS OVERRIDE HIT en el log
    [string]$Steps = "",          # JSON opcional: secuencia de pasos
    [int]$ShotEvery = 0,          # captura cada N segundos durante la espera final (0=off)
    [int]$BootWaitSec = 35,       # espera inicial de arranque antes de navegar
    [int]$FinalWaitSec = 5,       # espera final tras los pasos
    [int]$FindWindowTimeoutSec = 60,
    [int]$KeyHoldMs = 120,
    [switch]$NoLaunch,
    [string]$Build = "out\build\win-amd64-release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root $Build
$Exe = Join-Path $BuildDir "dbz3.exe"
$LogsDir = Join-Path $BuildDir "logs"
$OutBase = Join-Path $Root "out\analysis\autotest"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutDir = Join-Path $OutBase ("{0}_{1}" -f $Scenario, $Stamp)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$NavLog = Join-Path $OutDir "nav.log"

# ---------------- P/Invoke ----------------
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hwnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern ushort GetAsyncKeyState(int vKey);
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  public static void Tap(int vk, bool down) {
    INPUT[] arr = new INPUT[1];
    arr[0].type = INPUT_KEYBOARD;
    arr[0].U.ki.wVk = (ushort)vk;
    arr[0].U.ki.wScan = 0;
    arr[0].U.ki.dwFlags = down ? 0u : KEYEVENTF_KEYUP;
    arr[0].U.ki.time = 0;
    arr[0].U.ki.dwExtraInfo = IntPtr.Zero;
    SendInput(1, arr, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
  }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public InputUnion U; }
  [StructLayout(LayoutKind.Explicit)]
  public struct InputUnion {
    [FieldOffset(0)] public KEYBDINPUT ki;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  public const uint INPUT_KEYBOARD = 1;
  public const uint KEYEVENTF_KEYUP = 0x0002;
  public const uint WM_CLOSE = 0x0010;
}
"@

# ---------------- helpers input ----------------
$VK = @{
  "SHIFT"=0x10; "CONTROL"=0x11; "MENU"=0x12;
  "UP"=0x26; "DOWN"=0x28; "LEFT"=0x25; "RIGHT"=0x27;
  "SPACE"=0x20; "SEMICOLON"=0xBA; "QUOTE"=0xDE; "RETURN"=0x0D; "TAB"=0x09;
  "Z"=0x5A; "W"=0x57; "A"=0x41; "S"=0x53; "D"=0x44;
  "F4"=0x73; "F10"=0x79; "ESC"=0x1B; "BACK"=0x08; "X"=0x58; "Y"=0x59;
  "L"=0x4C; "P"=0x50; "Q"=0x51; "I"=0x49; "E"=0x45; "O"=0x4F; "ONE"=0x31; "THREE"=0x33; "K"=0x4B; "F"=0x46;
}
# Cada accion -> lista de teclas (posibles modificadores)
$ACTIONS = @{
  "A"        = @("SPACE")
  "B"        = @("QUOTE")
  "X"        = @("L")
  "Y"        = @("P")
  "START"    = @("RETURN")
  "BACK"     = @("Z")
  "DPAD_UP"  = @("SHIFT","UP")
  "DPAD_DOWN"= @("SHIFT","DOWN")
  "DPAD_LEFT"= @("SHIFT","LEFT")
  "DPAD_RIGHT"= @("SHIFT","RIGHT")
  "L_UP"     = @("W")
  "L_DOWN"   = @("S")
  "L_LEFT"   = @("A")
  "L_RIGHT"  = @("D")
  "LT"       = @("Q")
  "RT"       = @("E")
  "LB"       = @("ONE")
  "RB"       = @("THREE")
  "F10"      = @("F10")
  "F4"       = @("F4")
  "ESC"      = @("ESC")
}

function Send-Vk([int]$vk, [bool]$down) {
  [W32]::Tap($vk, $down)
}

function Send-Action([string]$action, [int]$holdMs) {
  if (-not $ACTIONS.ContainsKey($action)) { throw "Accion desconocida: $action" }
  $keys = $ACTIONS[$action]
  Add-Content -LiteralPath $NavLog -Value ("{0} -> {1} (hold {2}ms)" -f $action, ($keys -join "+"), $holdMs)
  foreach ($k in $keys) { Send-Vk $VK[$k] $true }
  Start-Sleep -Milliseconds $holdMs
  for ($i = $keys.Count - 1; $i -ge 0; $i--) { Send-Vk $VK[$keys[$i]] $false }
  Start-Sleep -Milliseconds 80
}

# ---------------- ventana ----------------
function Get-GameWindow() {
  $proc = Get-Process -Name "dbz3" -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($proc) { return [IntPtr]$proc.MainWindowHandle }
  return [IntPtr]::Zero
}

function Activate-Window([IntPtr]$hwnd) {
  if ($hwnd -eq [IntPtr]::Zero) { return $false }
  [void][W32]::ShowWindowAsync($hwnd, 9)   # SW_RESTORE
  Start-Sleep -Milliseconds 300
  $ws = New-Object -ComObject WScript.Shell
  for ($i = 0; $i -lt 3; $i++) {
    [void][W32]::SetForegroundWindow($hwnd)
    if (-not ($ws.AppActivate($hwnd.ToInt64()) -or $ws.AppActivate([int]$hwnd.ToInt32()))) {
      # fallback: Alt-trick para esquivar el foreground lock
      [W32]::keybd_event([byte]0x12, 0, 0, [UIntPtr]::Zero)
      Start-Sleep -Milliseconds 60
      [W32]::keybd_event([byte]0x12, 0, 2, [UIntPtr]::Zero)
      Start-Sleep -Milliseconds 60
      [void][W32]::SetForegroundWindow($hwnd)
    }
    Start-Sleep -Milliseconds 400
    if ([W32]::GetForegroundWindow() -eq $hwnd) { return $true }
  }
  return ([W32]::GetForegroundWindow() -eq $hwnd)
}

# ---------------- captura ----------------
function Save-Screen([IntPtr]$hwnd, [string]$path) {
  Add-Type -AssemblyName System.Drawing
  $r = [W32+RECT]::new()
  [void][W32]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  if ($w -le 0 -or $h -le 0) { $w = 1280; $h = 720 }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}

# ---------------- paso por paso ----------------
function Invoke-Steps([System.Collections.IList]$steps, [IntPtr]$hwnd) {
  $i = 0
  foreach ($s in $steps) {
    $i++
    $name = "step_{0:D2}_{1}.png" -f $i, $s.action
    $hold = if ($s.hold) { [int]$s.hold } else { $KeyHoldMs }
    $wait = if ($s.wait) { [int]$s.wait } else { 2000 }
    Send-Action $s.action $hold
    Start-Sleep -Seconds $wait
    Save-Screen $hwnd (Join-Path $OutDir $name)
  }
}

# ---------------- parseo de logs ----------------
function Get-LatestLogPath() {
  $logs = Get-ChildItem -LiteralPath $LogsDir -Filter "dbz3_*.log" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending
  if ($logs) { return $logs[0].FullName }
  return $null
}

function Get-LogFacts([string]$logPath) {
  $facts = @{ crash = $false; override_hits = @(); override_misses = @(); mod_reads = @(); lines = 0 }
  if (-not $logPath -or -not (Test-Path -LiteralPath $logPath)) { return $facts }
  $facts.lines = (Get-Content -LiteralPath $logPath).Count
  foreach ($ln in Get-Content -LiteralPath $logPath) {
    if ($ln -match "UNHANDLED EXCEPTION|0xC0000005|CRASH|FATAL") { $facts.crash = $true }
    elseif ($ln -match "AFS OVERRIDE HIT") { $facts.override_hits += $ln.Trim() }
    elseif ($ln -match "AFS OVERRIDE MISS|OVERRIDE MISS") { $facts.override_misses += $ln.Trim() }
    elseif ($ln -match "AFS MOD READ") { $facts.mod_reads += $ln.Trim() }
  }
  return $facts
}

# ---------------- main ----------------
Write-Host "== autotest: scenario=$Scenario out=$OutDir =="

if ($EnableMod -or $DisableMod) {
  $modsRoot = Join-Path $BuildDir "mods"
  foreach ($m in @($EnableMod)) {
    if ($m) {
      $marker = Join-Path $modsRoot "$m\.disabled"
      if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker -Force }
      Write-Host "  mod activado: $m"
    }
  }
  foreach ($m in @($DisableMod)) {
    if ($m) {
      $dir = Join-Path $modsRoot $m
      if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
      New-Item -ItemType File -Path (Join-Path $dir ".disabled") -Force | Out-Null
      Write-Host "  mod desactivado: $m"
    }
  }
}

if (-not $NoLaunch) {
  if (-not (Test-Path -LiteralPath $Exe)) { throw "No existe $Exe" }
  # 0a. limpiar procesos previos
  Get-Process -Name "dbz3" -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 800
  # 0. archivar logs previos para tener el "latest" limpio
  $LogsArchive = Join-Path $BuildDir "logs\archive\$Stamp"
  if (Test-Path -LiteralPath $LogsDir) {
    New-Item -ItemType Directory -Path $LogsArchive -Force | Out-Null
    Get-ChildItem -LiteralPath $LogsDir -Filter "*.log" | Move-Item -Destination $LogsArchive -Force
  }
  Remove-Item -LiteralPath (Join-Path $BuildDir "dbz1_afs_reads.log") -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath (Join-Path $BuildDir "dbz1_roster_trace.log") -ErrorAction SilentlyContinue

  # 1. lanzar el juego (guest directo, sin launcher, mnk + diag)
  $argsList = @(
    "--dbz3_skip_launcher",
    "--dbz3_region=us",
    "--dbz3_language=1",
    "--dbz3_mnk_mode=true",
    "--dbz3_input_backend=xinput",
    "--dbz3_dev_mode",
    "--dbz3_diag_logging"
  )
  if ($ModName) { $argsList += "--dbz3_mod_profile=$ModName" }
  $p = Start-Process -FilePath $Exe -WorkingDirectory $BuildDir -ArgumentList $argsList -PassThru
  Write-Host "  pid=$($p.Id)"
} else {
  $p = Get-Process -Name "dbz3" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { throw "-NoLaunch y no hay proceso dbz3" }
}

$fg = $false
$closed = "error"
try {
# 2. esperar ventana
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds($FindWindowTimeoutSec)
while ((Get-Date) -lt $deadline) {
  $hwnd = Get-GameWindow
  if ($hwnd -ne [IntPtr]::Zero) { break }
  if ($p.HasExited) { throw "dbz3 salio antes de crear ventana (exit $($p.ExitCode))" }
  Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { throw "No se encontro la ventana del juego en $FindWindowTimeoutSec s" }
Write-Host "  ventana OK hwnd=$hwnd"

# 3. espera de arranque + captura inicial
Start-Sleep -Seconds $BootWaitSec
$fg = Activate-Window $hwnd
Write-Host "  foreground=$fg"
Save-Screen $hwnd (Join-Path $OutDir "boot.png")

# 4. navegacion
if ($Steps) {
  $nav = Get-Content -LiteralPath $Steps -Raw | ConvertFrom-Json
  Invoke-Steps $nav $hwnd
} elseif ($Scenario -eq "title") {
  Invoke-Steps @(@{action="START"; wait=12}) $hwnd
}

# 5. espera final con capturas periodicas
$se = [int]$ShotEvery; $fw = [int]$FinalWaitSec
if ($se -gt 0) {
  $t = 0
  while ($t -lt $fw) {
    Start-Sleep -Seconds ([Math]::Min($se, $fw - $t))
    $t += $se
    Save-Screen $hwnd (Join-Path $OutDir ("final_{0:D2}s.png" -f $t))
  }
} else {
  Start-Sleep -Seconds $fw
  Save-Screen $hwnd (Join-Path $OutDir "final.png")
}

# 6. cerrar limpio (WM_CLOSE) con watchdog
[void][W32]::PostMessage($hwnd, [W32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
if (-not $p.WaitForExit(10000)) {
  Stop-Process -Id $p.Id -Force
  $closed = "force"
} else {
  $closed = "wm_close"
}
Write-Host "  cerrado por: $closed"
} catch {
  $err = $_.Exception.Message
  Write-Host "  ERROR en ejecucion: $err"
  Get-Process -Name "dbz3" -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  $notesErr = @("HARNESS ERROR: $err")
}

# 7. evaluar logs
$latest = Get-LatestLogPath
$facts = Get-LogFacts $latest
$af = Join-Path $BuildDir "dbz1_afs_reads.log"
$afsLines = if (Test-Path -LiteralPath $af) { (Get-Content -LiteralPath $af).Count } else { 0 }
$rt = Join-Path $BuildDir "dbz1_roster_trace.log"
$rtLines = if (Test-Path -LiteralPath $rt) { (Get-Content -LiteralPath $rt).Count } else { 0 }

$ok = (-not $facts.crash) -and (-not $notesErr)
$notes = @()
if ($notesErr) { $notes += $notesErr }
if ($facts.crash) { $notes += "CRASH detectado en $latest" }
if ($RequireOverride -and $facts.override_hits.Count -eq 0) { $ok = $false; $notes += "NO hubo AFS OVERRIDE HIT (requerido)" }
if ($ModName -and $facts.override_hits.Count -eq 0) { $notes += "AVISO: mod $ModName sin override hit" }
if (-not $latest) { $notes += "Sin log dbz3_*.log" }

$summary = [ordered]@{
  scenario = $Scenario; stamp = $Stamp; out_dir = $OutDir
  pid = $p.Id; closed_by = $closed; foreground = $fg
  log = $latest; log_lines = $facts.lines
  crash = $facts.crash
  override_hits = $facts.override_hits.Count
  override_misses = $facts.override_misses.Count
  mod_reads = $facts.mod_reads.Count
  afs_reads_lines = $afsLines
  roster_trace_lines = $rtLines
  pass = $ok
  notes = $notes
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir "summary.json") -Encoding UTF8
$summary.GetEnumerator() | ForEach-Object { "{0}: {1}" -f $_.Key, $_.Value } |
  Set-Content -LiteralPath (Join-Path $OutDir "summary.txt") -Encoding UTF8

Write-Host "== RESULTADO: $(if ($ok) {'PASS'} else {'FAIL'}) =="
Write-Host "  crash=$($facts.crash) override_hits=$($facts.override_hits.Count) mod_reads=$($facts.mod_reads.Count) afs_reads=$afsLines roster=$rtLines"
Write-Host "  resumen: $outDir\summary.json"
exit $(if ($ok) { 0 } else { 1 })