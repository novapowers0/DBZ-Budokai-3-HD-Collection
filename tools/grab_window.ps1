param(
  [string]$Out = "",
  [int]$TargetPid = 0,
  [int]$Index = 0
)
$ErrorActionPreference = 'Stop'
if ($TargetPid -eq 0) {
  $state = Join-Path $env:TEMP 'opencode\long_run_state.json'
  if (Test-Path -LiteralPath $state) { $TargetPid = (Get-Content -LiteralPath $state -Raw | ConvertFrom-Json).pid }
}
if (-not $TargetPid) { Write-Error "grab_window: sin PID"; exit 1 }
if (-not $Out) { $Out = Join-Path $env:TEMP 'opencode\game_shot.png' }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WinGrab {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
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
[WinGrab]::Collect([uint32]$TargetPid)
if ([WinGrab]::Hits.Count -eq 0) { Write-Error "grab_window: sin ventanas grandes"; exit 1 }
if ($Index -ge [WinGrab]::Hits.Count) { $Index = 0 }
$h = [WinGrab]::Hits[$Index]
$r = New-Object WinGrab+RECT
[WinGrab]::GetClientRect($h, [ref]$r) | Out-Null
$w = $r.R; $ht = $r.B
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# 2 = PW_RENDERFULLCONTENT (necesario para ventanas D3D12 / flip model)
$ok = [WinGrab]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "grab_window: hwnd=$h size=${w}x${ht} printwindow=$ok -> $Out"
