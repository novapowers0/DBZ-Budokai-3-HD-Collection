param(
  [int]$TargetPid = 0,
  [int]$X = 0,
  [int]$Y = 0,
  [switch]$Foreground
)
# Click en coordenadas CLIENTE de la ventana grande del proceso (mismo criterio
# que grab_window.ps1). Util para pulsar controles del launcher en pruebas
# automaticas (pestanas, checkboxes).
$ErrorActionPreference = 'Stop'
if ($TargetPid -eq 0) {
  $state = Join-Path $env:TEMP 'opencode\long_run_state.json'
  if (Test-Path -LiteralPath $state) { $TargetPid = (Get-Content -LiteralPath $state -Raw | ConvertFrom-Json).pid }
}
if (-not $TargetPid) { Write-Error "click_window: sin PID"; exit 1 }
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WinClick {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static uint T; public static List<IntPtr> Hits = new List<IntPtr>();
  public static void Collect(uint pid) {
    Hits.Clear(); T = pid;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == T && IsWindowVisible(h)) {
        RECT r; GetClientRect(h, out r);
        if (r.R > 100 && r.B > 100) Hits.Add(h);
      }
      return true;
    }, IntPtr.Zero);
  }
}
"@
[WinClick]::Collect([uint32]$TargetPid)
if ([WinClick]::Hits.Count -eq 0) { Write-Error "click_window: sin ventanas grandes"; exit 1 }
$h = [WinClick]::Hits[0]
if ($Foreground) { [WinClick]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 250 }
$pt = New-Object WinClick+POINT
$pt.X = $X; $pt.Y = $Y
[WinClick]::ClientToScreen($h, [ref]$pt) | Out-Null
[WinClick]::SetCursorPos($pt.X, $pt.Y) | Out-Null
Start-Sleep -Milliseconds 120
# 0x0002 = LEFTDOWN, 0x0004 = LEFTUP
[WinClick]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 60
[WinClick]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Write-Output "click_window: hwnd=$h client=($X,$Y) screen=($($pt.X),$($pt.Y))"
