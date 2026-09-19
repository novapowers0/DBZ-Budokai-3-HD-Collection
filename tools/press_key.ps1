param(
  [string]$Key = "Return",     # Return | Space | A | B | X | Y | Up | Down | Left | Right | Escape
  [int]$Count = 1,
  [int]$HoldMs = 80,
  [int]$IntervalMs = 800,
  [int]$TargetPid = 0                # 0 = leer del estado de long_run
)
$ErrorActionPreference = 'Stop'
if ($TargetPid -eq 0) {
  $state = Join-Path $env:TEMP 'opencode\long_run_state.json'
  if (Test-Path -LiteralPath $state) { $TargetPid = (Get-Content -LiteralPath $state -Raw | ConvertFrom-Json).pid }
}
if (-not $TargetPid) { Write-Error "press_key: sin PID (pasa -TargetPid o arranca long_run)"; exit 1 }

$vk = @{ Return=0x0D; Space=0x20; Escape=0x1B; A=0x41; B=0x42; X=0x58; Y=0x59;
         Up=0x26; Down=0x28; Left=0x25; Right=0x27; Z=0x5A; L=0x4C; P=0x50; W=0x57; S=0x53; D=0x44; Q=0x51; E=0x45; I=0x49; O=0x4F; Backspace=0x08; Tab=0x09 }
if (-not $vk.ContainsKey($Key)) { Write-Error "press_key: tecla no soportada '$Key'"; exit 1 }
$code = $vk[$Key]

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KeyPost {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static uint T; public static int N;
  public static int Send(uint vk) {
    N = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == T) {
        PostMessage(h, 0x0100, (IntPtr)vk, (IntPtr)1);   // WM_KEYDOWN
        System.Threading.Thread.Sleep(60);
        PostMessage(h, 0x0101, (IntPtr)vk, (IntPtr)1);   // WM_KEYUP
        N++;
      }
      return true;
    }, IntPtr.Zero);
    return N;
  }
}
"@
[KeyPost]::T = [uint32]$TargetPid
for ($i = 1; $i -le $Count; $i++) {
  $n = [KeyPost]::Send($code)
  Write-Output "press_key: $Key -> $n ventanas (pid $TargetPid) [$i/$Count]"
  if ($i -lt $Count) { Start-Sleep -Milliseconds $HoldMs; Start-Sleep -Milliseconds $IntervalMs }
}
